#include "Game/Drawable/DrawableBall.h"
#include "Game/Drawable/DrawableCharacter.h"
#include "Game/Drawable/DrawableModel.h"
#include "Game/RenderSnapshot.h"
#include "Game/Ball.h"
#include "Game/CharacterTemplate.h"
#include "Game/Player.h"
#include "NL/gl/glModel.h"
#include "NL/gl/glState.h"
#include "NL/gl/glUserData.h"
#include "port/xr_quest.h" // strikers-quest: a bigger ball on the Tabletop field
#include "port/xr_layers.h" // strikers-quest r43b: the ball always on the field
#include "port/ball_skin.h" // strikers-quest r168: BALL SKINS (src/platform/ball_skin.cpp)
#include "Game/Drawable/DrawableModel.h"
#include "Game/Team.h"

// strikers-quest r168: BALL SKINS, looks only (src/platform/ball_skin.cpp): the ball's per-frame model copy gets our
// sphere and a frame of our living texture; PLANET EARTH also turns on its pole as drawn (the drawn orientation only -
// the ball's own m_qOrientation, its physics and its size are the game's)
namespace portballskin
{
int begin_frame(int lastTouchSide, const glModel* srcModel, float boundingRadius);
float spin_angle(int skin);  // r170: -1 none
bool emissive(int skin);
glModel* apply(glModel* m, int pass);
void feed(bool visible, const float drawnPos[3], const float gamePos[3], const float vel[3], float scale, float boundingRadius);
} // namespace portballskin
static int s_r168Skin = 0;               // the skin drawn this frame (0 the game's own ball)
static nlQuaternion s_r168Spin; // PLANET EARTH's turn this frame (set before use)
static bool s_r168Spinning = false; // r170: the skin drawn turns on its pole
static glModel* (*s_r168BlurCB)(glModel*, eGLView&, unsigned long&) = NULL;
static glModel* (*s_r168LightCB)(glModel*, eGLView&, unsigned long&) = NULL;
static nlQuaternion r168_mul(const nlQuaternion& a, const nlQuaternion& b)
{
    nlQuaternion q;
    q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return q;
}
static glModel* R168MainCB(glModel* pModel, eGLView& view, unsigned long& uLayer)
{
    (void)view;
    (void)uLayer;
    return portballskin::apply(pModel, 0);
}
static glModel* R168BlurCB(glModel* pModel, eGLView& view, unsigned long& uLayer)
{
    pModel = portballskin::apply(pModel, 1);
    return s_r168BlurCB != NULL ? s_r168BlurCB(pModel, view, uLayer) : pModel;
}
static glModel* R168LightCB(glModel* pModel, eGLView& view, unsigned long& uLayer)
{
    if (portballskin::emissive(s_r168Skin))
        return NULL; // (a ball that is its own light - fire, lava, neon ...: no lit sheen over it; r170: any such skin)
    pModel = portballskin::apply(pModel, 2);
    return s_r168LightCB != NULL ? s_r168LightCB(pModel, view, uLayer) : pModel;
}

static int g_nMotionBlurDivs = 5;
static float g_fMotionBlurAlpha0 = 0.1f;
static float g_fMotionBlurAlphaScale = 0.22f;

static float g_fBallBlur;

namespace
{
extern "C"
{
    extern const unsigned long eOC_NO_LIGHT;
}
} // namespace

static glModel* BallLightingCB(glModel* pModel, eGLView& view, unsigned long& uLayer);
static glModel* BallBlurCB(glModel* pModel, eGLView& view, unsigned long& uLayer);

/**
 * Offset/Address/Size: 0x0 | 0x8011DD50 | size: 0x80
 */
void DrawableBall::EvaluateFrom(DrawableCharacter& character)
{
    mPosition = character.GetBallPosition();
    mOrientation = character.GetBallOrientation();
}

/**
 * Offset/Address/Size: 0x80 | 0x8011DDD0 | size: 0x140
 */
void DrawableBall::Blend(const float* blendFactors, const DrawableBall& lhs, const DrawableBall& rhs)
{
    mPrevOrientation = mOrientation;
    nlQuatNLerp(mOrientation, lhs.mOrientation, rhs.mOrientation, *blendFactors);

    const f32 t = *blendFactors;

    mPosition.x = (1.0f - t) * lhs.mPosition.x + t * rhs.mPosition.x;
    mPosition.y = (1.0f - t) * lhs.mPosition.y + t * rhs.mPosition.y;
    mPosition.z = (1.0f - t) * lhs.mPosition.z + t * rhs.mPosition.z;

    mVelocity.x = (1.0f - t) * lhs.mVelocity.x + t * rhs.mVelocity.x;
    mVelocity.y = (1.0f - t) * lhs.mVelocity.y + t * rhs.mVelocity.y;
    mVelocity.z = (1.0f - t) * lhs.mVelocity.z + t * rhs.mVelocity.z;

    mVisible = lhs.mVisible && rhs.mVisible;

    mOwnerIndex = rhs.mOwnerIndex;
    mPrevOwnerIndex = rhs.mPrevOwnerIndex;
    mPassTargetIndex = rhs.mPassTargetIndex;
}

inline void DrawableBall::RenderMotionBlur(DrawableObject& obj) const
{
    glModel* (*savedBlurCB)(glModel*, eGLView&, unsigned long&);
    u8 savedBallShadowDisabled;
    float t;
    nlQuaternion q;
    nlMatrix4 mView;
    nlMatrix4 mWorld;
    nlMatrix4 mSaved;

    glViewGetViewMatrix(GLV_Unshadowed, mView);

    const float blurOffsetScale = 0.0078125f;
    nlVector3 blurOffset;
    nlVec3Set(blurOffset,
        blurOffsetScale * mView.m13,
        blurOffsetScale * mView.m23,
        blurOffsetScale * mView.m33);

    savedBallShadowDisabled = DrawableModel::GetBallShadowDisabled();
    DrawableModel::SetBallShadowDisabled(1);

    mSaved = obj.GetWorldMatrix();
    savedBlurCB = obj.GetCallback();
    obj.m_CB = BallBlurCB;
    if (s_r168Skin != 0) // r168: the blur copies in the skin too
    {
        s_r168BlurCB = BallBlurCB;
        obj.m_CB = R168BlurCB;
    }

    int i;
    unsigned long uSavedFlags = obj.m_uObjectCreationFlags;
    obj.m_uObjectCreationFlags = uSavedFlags | eOC_NO_LIGHT;

    for (i = 0; i < g_nMotionBlurDivs; i++)
    {
        t = (float)i / (float)g_nMotionBlurDivs;
        g_fBallBlur = g_fMotionBlurAlphaScale * (1.0f - t) + g_fMotionBlurAlpha0;

        nlQuatNLerp(q, mPrevOrientation, mOrientation, t);
        if (s_r168Spinning)
            q = r168_mul(q, s_r168Spin); // r168: as the ball itself is turned (r170: any turning skin)
        obj.m_orientation = q;
        obj.m_worldMatrixUpToDate = 0;

        mWorld = obj.GetWorldMatrix();
        mWorld.m41 += blurOffset.x;
        mWorld.m42 += blurOffset.y;
        mWorld.m43 += blurOffset.z;
        obj.m_worldMatrix = mWorld;

        obj.Draw();
    }

    DrawableModel::SetBallShadowDisabled(savedBallShadowDisabled);
    obj.m_worldMatrix = mSaved;
    obj.m_CB = savedBlurCB;
    obj.m_uObjectCreationFlags = uSavedFlags;
}

void DrawableBall::RenderLighting(DrawableObject& obj) const
{
    u8 savedBallShadowDisabled = DrawableModel::GetBallShadowDisabled();
    DrawableModel::SetBallShadowDisabled(1);

    glModel* (*savedLightingCB)(glModel*, eGLView&, unsigned long&) = obj.GetCallback();
    obj.m_CB = BallLightingCB;
    if (s_r168Skin != 0) // r168: the lit sheen on our sphere (none on the fireball)
    {
        s_r168LightCB = BallLightingCB;
        obj.m_CB = R168LightCB;
    }

    unsigned long uSavedFlags = obj.m_uObjectCreationFlags;
    // PORT: BallLightingCB does `uLayer += 2`, and DrawModel hands it m_uRenderLayer itself rather than a copy.
    unsigned long uSavedLayer = obj.m_uRenderLayer;
    obj.m_uObjectCreationFlags &= ~0x80;
    obj.Draw();

    DrawableModel::SetBallShadowDisabled(savedBallShadowDisabled);
    obj.m_CB = savedLightingCB;
    obj.m_uObjectCreationFlags = uSavedFlags;
    obj.m_uRenderLayer = uSavedLayer;
}

namespace
{
extern "C"
{
    const unsigned long eOC_NO_LIGHT = 0x80;
}
} // namespace

/**
 * Offset/Address/Size: 0x1C0 | 0x8011DF10 | size: 0x47C
 */
void DrawableBall::Render() const
{
    DrawableObject* drawable = g_pBall->GetDrawableBall();
    if (mVisible)
    {
        drawable->m_uObjectFlags |= 1;
    }
    else
    {
        drawable->m_uObjectFlags &= ~1;
    }
    if (!mVisible)
    {
        // r168: no ball this frame (the effects stop following it)
        const float z3[3] = { 0.0f, 0.0f, 0.0f };
        s_r168Skin = 0;
        s_r168Spinning = false;
        portballskin::feed(false, z3, z3, z3, 1.0f, 0.0f);
        return;
    }

    DrawableObject* pDrawableBall = g_pBall->GetDrawableBall();
    // r168: BALL SKINS - which one this frame (the side that touched it last wears its own; 0 the game's ball)
    {
        int lastSide = -1;
        if (g_pBall->m_pLastTouch != NULL && g_pBall->m_pLastTouch->m_pTeam != NULL)
            lastSide = g_pBall->m_pLastTouch->m_pTeam->m_nSide;
        DrawableModel* pModel = pDrawableBall->AsDrawableModel();
        s_r168Skin = (pModel != NULL && !pModel->m_bVertexAnimated)
                         ? portballskin::begin_frame(lastSide, pModel->m_pModel, pDrawableBall->m_fBoundingRadius)
                         : 0;
        const float spin = portballskin::spin_angle(s_r168Skin);  // r170: every turning skin (EARTH, GALAXY, DISCO ...)
        s_r168Spinning = spin >= 0.0f;
        if (s_r168Spinning)
        {
            const nlVector3 pole = { 0.0f, 0.0f, 1.0f };
            nlMakeQuat(s_r168Spin, pole, spin);
        }
    }
    pDrawableBall->m_orientation = s_r168Spinning ? r168_mul(mOrientation, s_r168Spin) : mOrientation;
    pDrawableBall->m_worldMatrixUpToDate = 0;
    // PORT (strikers-quest): on the Tabletop field the ball grows with the players (Lance,
    // 2026-10-10), drawn only. Resting, its centre is one radius above the grass, so it is lifted by
    // the extra radius to keep sitting on it (Ball.cpp rests it at z 0.18); in the air the lift stays
    // that small.
    const float fScale = PortXrCharacterScale();
    nlVector3 v3Drawn = mPosition;
    if (fScale != 1.0f)
    {
        const float fRest = v3Drawn.z < 0.0f ? 0.0f : (v3Drawn.z > 0.18f ? 0.18f : v3Drawn.z);
        v3Drawn.z += (fScale - 1.0f) * fRest;
    }
    pDrawableBall->m_scale = fScale;
    pDrawableBall->m_translation = v3Drawn;
    pDrawableBall->m_worldMatrixUpToDate = 0;
    PortXrSetBall(pDrawableBall);  // r43b: always on the Tabletop field (port/xr_layers.h)

    unsigned long uSavedFlags = pDrawableBall->m_uObjectCreationFlags;
    pDrawableBall->m_uObjectCreationFlags &= ~0x80;
    // r168: a skin's ball: our sphere and picture (DrawModel's copy only); the FIREBALL unlit - it is its own light
    glModel* (*savedMainCB)(glModel*, eGLView&, unsigned long&) = pDrawableBall->m_CB;
    if (s_r168Skin != 0)
    {
        pDrawableBall->m_CB = R168MainCB;
        if (portballskin::emissive(s_r168Skin))  // r170: any skin that is its own light
            pDrawableBall->m_uObjectCreationFlags |= 0x80;
    }
    pDrawableBall->Draw();
    pDrawableBall->m_CB = savedMainCB;
    pDrawableBall->m_uObjectCreationFlags = uSavedFlags;

    RenderMotionBlur(*pDrawableBall);
    RenderLighting(*pDrawableBall);

    // r168: where it is, for the skins' trail and hits in the headset's effects layer (read only)
    {
        const float drawn[3] = { v3Drawn.x, v3Drawn.y, v3Drawn.z };
        const float game[3] = { mPosition.x, mPosition.y, mPosition.z };
        const float vel[3] = { mVelocity.x, mVelocity.y, mVelocity.z };
        portballskin::feed(true, drawn, game, vel, fScale, pDrawableBall->m_fBoundingRadius);
    }
}

/**
 * Offset/Address/Size: 0x63C | 0x8011E38C | size: 0xB4
 */
static glModel* BallLightingCB(glModel* pModel, eGLView& view, unsigned long& uLayer)
{
    u32 tex;
    glModelPacket* pPacket;

    static u32 WhiteTexture = glGetTexture("global/white");

    pPacket = pModel->packets;
    tex = WhiteTexture;
    while (pPacket < &pModel->packets[pModel->numPackets])
    {
        pPacket->state.texture[0] = tex;
        glSetRasterState(pPacket->state.raster, GLS_AlphaBlend, 4);
        pPacket++;
    }

    uLayer += 2;
    return pModel;
}

/**
 * Offset/Address/Size: 0x6F0 | 0x8011E440 | size: 0x10C
 */
glModel* BallBlurCB(glModel* pModel, eGLView& view, unsigned long& uLayer)
{
    glModelPacket* pPacket;
    if (g_fBallBlur == 0.0f)
    {
        return nullptr;
    }

    void* pUserDataHandle = glUserAlloc(GLUD_ConstantColour, 4, false);
    u8* pColorData = (u8*)glUserGetData(pUserDataHandle);

    const float alphaFloat = 255.0f * g_fBallBlur;

    pColorData[0] = 0xC8;
    pColorData[1] = 0xC8;
    pColorData[2] = 0xC8;
    pColorData[3] = (u8)(int)alphaFloat;

    pPacket = pModel->packets;
    while (pPacket < &pModel->packets[pModel->numPackets])
    {
        if (g_fBallBlur != 1.0f)
        {
            glSetRasterState(pPacket->state.raster, GLS_AlphaBlend, 1);
            glSetRasterState(pPacket->state.raster, GLS_DepthWrite, 0);
            glUserAttach(pUserDataHandle, pPacket, false);
        }
        pPacket++;
    }

    return pModel;
}

/**
 * Offset/Address/Size: 0x7FC | 0x8011E54C | size: 0xCC
 */
void DrawableBall::Grab()
{
    mOrientation = g_pBall->m_qOrientation;
    mPosition = g_pBall->m_v3Position;
    mVelocity = g_pBall->m_v3Velocity;
    mOwnerIndex = GetCharacterIndex(g_pBall->m_pOwner);
    mPrevOwnerIndex = GetCharacterIndex(g_pBall->m_pPrevOwner);
    mPassTargetIndex = GetCharacterIndex(g_pBall->m_pPassTarget);
    mLastTouchIndex = GetCharacterIndex(g_pBall->m_pLastTouch);
    mVisible = true;
}

/**
 * Offset/Address/Size: 0x8C8 | 0x8011E618 | size: 0x24
 */
DrawableCharacter* DrawableBall::IndexToPlayer(int index) const
{
    if (index == -1)
    {
        return nullptr;
    }
    return &mRenderSnapshot->mCharacters[index];
}

/**
 * Offset/Address/Size: 0xA24 | 0x8011E774 | size: 0x138
 */
template <>
void DrawableBall::Replay<SaveFrame>(SaveFrame& frame)
{
    Replayable<1, SaveFrame, FloatCompressor<-127, 127, 7> >(frame, FloatCompressor<-127, 127, 7>(mPosition.x));
    Replayable<1, SaveFrame, FloatCompressor<-127, 127, 7> >(frame, FloatCompressor<-127, 127, 7>(mPosition.y));
    Replayable<1, SaveFrame, FloatCompressor<-127, 127, 7> >(frame, FloatCompressor<-127, 127, 7>(mPosition.z));

    Replayable<1, SaveFrame, FloatCompressor<-1, 1, 13> >(frame, FloatCompressor<-1, 1, 13>(mOrientation.x));
    Replayable<1, SaveFrame, FloatCompressor<-1, 1, 13> >(frame, FloatCompressor<-1, 1, 13>(mOrientation.y));
    Replayable<1, SaveFrame, FloatCompressor<-1, 1, 13> >(frame, FloatCompressor<-1, 1, 13>(mOrientation.z));
    Replayable<1, SaveFrame, FloatCompressor<-1, 1, 13> >(frame, FloatCompressor<-1, 1, 13>(mOrientation.w));

    Replayable<1, SaveFrame, FloatCompressor<-127, 127, 5> >(frame, FloatCompressor<-127, 127, 5>(mVelocity.x));
    Replayable<1, SaveFrame, FloatCompressor<-127, 127, 5> >(frame, FloatCompressor<-127, 127, 5>(mVelocity.y));
    Replayable<1, SaveFrame, FloatCompressor<-127, 127, 5> >(frame, FloatCompressor<-127, 127, 5>(mVelocity.z));

    Replayable<1, SaveFrame, bool>(frame, mVisible);
    Replayable<1, SaveFrame, char>(frame, (char&)mOwnerIndex);
    Replayable<1, SaveFrame, char>(frame, (char&)mPrevOwnerIndex);
    Replayable<1, SaveFrame, char>(frame, (char&)mPassTargetIndex);
    Replayable<1, SaveFrame, char>(frame, (char&)mLastTouchIndex);
}

/**
 * Offset/Address/Size: 0x8EC | 0x8011E63C | size: 0x138
 */
template <>
void DrawableBall::Replay<LoadFrame>(LoadFrame& frame)
{
    Replayable<1, LoadFrame, FloatCompressor<-127, 127, 7> >(frame, FloatCompressor<-127, 127, 7>(mPosition.x));
    Replayable<1, LoadFrame, FloatCompressor<-127, 127, 7> >(frame, FloatCompressor<-127, 127, 7>(mPosition.y));
    Replayable<1, LoadFrame, FloatCompressor<-127, 127, 7> >(frame, FloatCompressor<-127, 127, 7>(mPosition.z));

    Replayable<1, LoadFrame, FloatCompressor<-1, 1, 13> >(frame, FloatCompressor<-1, 1, 13>(mOrientation.x));
    Replayable<1, LoadFrame, FloatCompressor<-1, 1, 13> >(frame, FloatCompressor<-1, 1, 13>(mOrientation.y));
    Replayable<1, LoadFrame, FloatCompressor<-1, 1, 13> >(frame, FloatCompressor<-1, 1, 13>(mOrientation.z));
    Replayable<1, LoadFrame, FloatCompressor<-1, 1, 13> >(frame, FloatCompressor<-1, 1, 13>(mOrientation.w));

    Replayable<1, LoadFrame, FloatCompressor<-127, 127, 5> >(frame, FloatCompressor<-127, 127, 5>(mVelocity.x));
    Replayable<1, LoadFrame, FloatCompressor<-127, 127, 5> >(frame, FloatCompressor<-127, 127, 5>(mVelocity.y));
    Replayable<1, LoadFrame, FloatCompressor<-127, 127, 5> >(frame, FloatCompressor<-127, 127, 5>(mVelocity.z));

    Replayable<1, LoadFrame, bool>(frame, mVisible);
    Replayable<1, LoadFrame, char>(frame, (char&)mOwnerIndex);
    Replayable<1, LoadFrame, char>(frame, (char&)mPrevOwnerIndex);
    Replayable<1, LoadFrame, char>(frame, (char&)mPassTargetIndex);
    Replayable<1, LoadFrame, char>(frame, (char&)mLastTouchIndex);
}
