// strikers-quest r42: the life-size goal scorer (Lance, after alpha 1.2: a really good looking life
// size scorer that runs up beside you, 5 ft for every character (r44h: each its own height,
// xr_quest.cpp scorer_height_m), celebrates, then runs back to the
// field; every goal but a Super Strike; the replay keeps going until it is over, which
// Presentation.cpp's held replay sees to).
//
// A second copy of the scorer's model, posed here every game frame with the game's own animations
// and sent with a matrix handle of its own that only the scorer eye pass draws (xr_layers.cpp,
// glPlat.cpp). xr_quest.cpp places that pass in the room, scaled to PORT_SCORER_HEIGHT. Rendering
// only: no game state is written and no shared random number drawn; the game's characters and its
// cut-scene are only read. The run is the character's own jog; the celebration is the clip the
// goal cut-scene plays for this character (taken while it plays), else its wave; then idle.

#include "port/xr_scorer.h"
#include "port/xr_layers.h"
#include "port/xr_quest.h"

#include "Game/AnimInventory.h"
#include "Game/Character.h"
#include "Game/CharacterEffects.h"
#include "Game/CharacterTemplate.h"
#include "Game/DB/StatsTracker.h"
#include "Game/Player.h"
#include "Game/Drawable/DrawableCharacter.h"
#include "Game/GL/ShaderSkinMesh.h"
#include "Game/NisPlayer.h"
#include "Game/PoseAccumulator.h"
#include "Game/Render/Nis.h"
#include "Game/Render/Presentation.h"
#include "Game/SAnim.h"
#include "Game/SAnim/AnimRetargeter.h"
#include "Game/SAnim/pnSAnimController.h"
#include "Game/Team.h"
#include "NL/gl/glModel.h"
#include "NL/nlString.h"  // r53: joints by name (nlStringLowerHash)
#include "NL/nlTask.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>  // r64: clock seed for the celebration bag

namespace
{

// GLOBALAnimProperties indices (globalanimproperties.cpp): every fielder has these, always loaded.
enum
{
    kAnimIdle = 0,
    kAnimJog = 12,
    kAnimWave = 99,
};

struct Show
{
    bool armed;          // a goal that gets the show
    bool started;        // its goal script has started counting
    const cCharacter* who;
    int index;           // g_pCharacters
    int side;
    int captainClass;
    bool prevHold;
    float leaveAt;       // when the run back starts (a skip brings it forward)
    cSAnim* celebrate;   // the cut-scene's clip for this character, or NULL
    bool celebrateMirror;
    bool celebrateBad;   // r43d: the clip stopped giving a sane pose (pose_sane); waves instead
    bool logged;
    int procedural;      // r53: the celebration it does (Celebration), -1 none
    bool poseLogged;     // r53: its main pose written to the log
};

Show s_show;
DrawableCharacter* s_copy = NULL; // the copy (its accumulator is made per show)
cPN_SAnimController s_ctrl;
float s_height[NUM_FIELDER_CLASSES]; // game units, measured once per class (0 = not yet)

// r44i (Lance: Waluigi's "legs go through the map"): per class, from the idle pose: how high its
// lowest joint stands (the toes, about) and which joints sit at the origin there (the root nodes,
// which follow the pose's placement and are no part of the body), so no later pose sinks below it.
const int kMaxGroundNodes = 160;
bool s_groundMeasured[NUM_FIELDER_CLASSES];
float s_restZ[NUM_FIELDER_CLASSES];
bool s_rootNode[NUM_FIELDER_CLASSES][kMaxGroundNodes];

// The lowest body joint of the copy's last pose (root nodes left out).
float lowest_joint(int cls)
{
    const cPoseAccumulator* acc = s_copy->mPoseAccumulator;
    const int n = acc->GetNumNodes() < kMaxGroundNodes ? acc->GetNumNodes() : kMaxGroundNodes;
    float lo = 1e9f;
    for (int i = 0; i < n; i++)
    {
        if (s_rootNode[cls][i])
            continue;
        const float z = acc->GetNodeMatrix(i).m43;
        if (z == z && z < lo)
            lo = z;
    }
    return lo;
}

// r44i: the copy's last pose, built with `world`, raised back onto the ground if any of its joints
// went lower than the character's own standing feet. A jump is left alone; only sinking is undone.
void keep_on_ground(cCharacter* ch, const nlMatrix4& world)
{
    const int cls = (int)ch->m_eCharacterClass;
    if (cls < 0 || cls >= NUM_FIELDER_CLASSES || !s_groundMeasured[cls])
        return;
    const float lo = lowest_joint(cls);
    if (!(lo < s_restZ[cls] - 0.002f) || lo < -100.0f)
        return;
    nlMatrix4 lifted = world;
    lifted.m43 += s_restZ[cls] - lo;
    s_copy->mPoseAccumulator->BuildNodeMatrices(lifted);
}

cCharacter* captain_of(int side)
{
    if (side < 0 || side > 1 || g_pTeams[side] == NULL)
        return NULL;
    return (cCharacter*)g_pTeams[side]->GetCaptain();
}

void release_copy()
{
    if (s_copy != NULL && s_copy->mPoseAccumulator != NULL)
    {
        delete s_copy->mPoseAccumulator;
        s_copy->mPoseAccumulator = NULL;
    }
}

// Poses the copy with anim at `seconds` (looped or held), feet at the origin, facing +X.
void pose(cCharacter* ch, cSAnim* anim, const AnimRetarget* retarget, ePlayMode mode, bool mirror, float seconds,
          bool loop)
{
    cPoseAccumulator* acc = s_copy->mPoseAccumulator;
    const float duration = anim->GetDuration();
    float tn = duration > 0.0f ? seconds / duration : 1.0f;
    tn = loop ? tn - floorf(tn) : (tn > 1.0f ? 1.0f : (tn < 0.0f ? 0.0f : tn));
    // Placement new: the class's own operator new takes from the game's shared slot pool. Never
    // Update(): that would fire the clip's trigger scripts (sounds, effects).
    ::new (&s_ctrl) cPN_SAnimController(anim, retarget, mode, NULL, 0, mirror);
    s_ctrl.m_bIgnoreTriggers = true;
    s_ctrl.SetTime(tn);
    acc->InitAccumulators();
    s_ctrl.Evaluate(1.0f, acc);
    nlMatrix4 world;
    world.SetIdentity();
    acc->BuildNodeMatrices(world);
    keep_on_ground(ch, world);  // r44i
}

// ---------------------------------------------------------------------------------------------
// r53 (Lance: "i want all original celebration animations, taken from famous soccer and NFL
// celebrations"): celebrations posed by the port on each character's own skeleton. The idle pose is
// the base; chosen bones are then aimed in the character's own space (+X the way it faces, which is
// toward you; +Y its left; +Z up) while the pose is built (build-node callbacks, so the children
// follow and keep_on_ground's rebuild keeps them). Aiming a bone needs no knowledge of how each
// rig's joint axes run: the bone's own direction (toward its child joint) is measured once per class
// in the idle pose. The body's jump and spin go in the pose's root placement.
// ---------------------------------------------------------------------------------------------

struct V3
{
    float x, y, z;
};
inline V3 v3(float x, float y, float z)
{
    V3 v = { x, y, z };
    return v;
}
inline V3 v_add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 v_scale(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
inline float v_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 v_cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline V3 v_norm(V3 a)
{
    const float l = sqrtf(v_dot(a, a));
    return l > 1e-8f ? v_scale(a, 1.0f / l) : v3(0.0f, 0.0f, 1.0f);
}
inline V3 v_lerp(V3 a, V3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
// Rodrigues: v turned by `angle` about the unit axis k (right-handed).
inline V3 v_rotate(V3 v, V3 k, float angle)
{
    const float c = cosf(angle), s = sinf(angle);
    return v_add(v_add(v_scale(v, c), v_scale(v_cross(k, v), s)), v_scale(k, v_dot(k, v) * (1.0f - c)));
}
inline V3 row_of(const nlMatrix4& m, int r) { return v3(m.e2[r][0], m.e2[r][1], m.e2[r][2]); }
inline V3 pos_of(const nlMatrix4& m) { return v3(m.m41, m.m42, m.m43); }
inline float ease(float t)
{
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    return t * t * (3.0f - 2.0f * t);
}
// 0 -> 1 over [a, b], held at 1, then back to 0 over [c, d].
inline float window(float t, float a, float b, float c, float d) { return ease((t - a) / (b - a)) * (1.0f - ease((t - c) / (d - c))); }

void pose_inventory(cCharacter* ch, int id, float seconds, bool loop, bool flip);

// A matrix's rotation rows turned about the axis k (unit) by `angle`; its translation stays.
void turn_rows(nlMatrix4& m, V3 k, float angle)
{
    for (int r = 0; r < 3; r++)
    {
        const V3 v = v_rotate(row_of(m, r), k, angle);
        m.e2[r][0] = v.x;
        m.e2[r][1] = v.y;
        m.e2[r][2] = v.z;
    }
}

enum
{
    kBoneUpper,
    kBoneFore,
    kBoneThigh,
    kBoneCalf,
    kBoneKinds
};

// Per class: the joints a celebration moves, and each aimed bone's direction in its own frame.
struct Rig
{
    bool tried, ok;
    int spine, neck, head;
    int bone[kBoneKinds][2];  // [kind][0 left, 1 right]
    V3 axis[kBoneKinds][2];
    // r54 (Lance on the 6-7: the hands "flipped in a way that it looked like he was running"; palms
    // up, as in his photo): each hand's joint and its palm's outward normal in the hand's own frame,
    // from the fingers in the idle pose (the thumb's side and the fingers' way), to turn the forearm
    // until the palm faces where a celebration wants it.
    int hand[2];
    bool palmOk[2];
    V3 palm[2];
};
Rig s_rig[NUM_FIELDER_CLASSES];

// This frame's targets, in the character's own space. A weight of 0 leaves the idle pose's bone.
struct Aim
{
    float w;
    V3 dir;
    float palmW;  // r54, forearms: turned about themselves so the palm faces `palm` (character space)
    V3 palm;
};
struct Targets
{
    bool active;
    Aim bone[kBoneKinds][2];
    float spineBack, spineSide, spineTwist;  // radians: lean back (chest out), lean to its left, turn left
    float headUp, headSide;                  // radians: chin up, tilt to its left
    float headTurn;                          // r55: radians, the head turned to its left
    float plantLift;                         // r55: in heights, raised above where the feet plant it (sitting)
    float rootZ;                             // the body raised (jumps), in heights
    float spin;                              // the body turned about +Z, radians (left = positive)
    bool airborne;                           // feet off the ground: not planted
};
Targets s_tg;
int s_tgClass = -1;

// Build callback, per moved joint: `which` = kind * 2 + side, or 100 spine, 101 head.
void celebrate_cb(uintptr_t, unsigned int which, cPoseAccumulator* acc, unsigned int node, int)
{
    if (!s_tg.active || s_tgClass < 0)
        return;
    const Rig& rig = s_rig[s_tgClass];
    nlMatrix4& m = acc->m_NodeMatrices.mData[node];
    const float cs = cosf(s_tg.spin), sn = sinf(s_tg.spin);
    // Character space -> the built pose's space (turned by the spin).
    const auto to_pose = [cs, sn](V3 v) { return v3(v.x * cs - v.y * sn, v.x * sn + v.y * cs, v.z); };
    if (which == 100)
    {
        if (s_tg.spineTwist != 0.0f)
            turn_rows(m, v3(0.0f, 0.0f, 1.0f), s_tg.spineTwist);
        if (s_tg.spineBack != 0.0f)
            turn_rows(m, to_pose(v3(0.0f, 1.0f, 0.0f)), -s_tg.spineBack);
        if (s_tg.spineSide != 0.0f)
            turn_rows(m, to_pose(v3(1.0f, 0.0f, 0.0f)), -s_tg.spineSide);
        return;
    }
    if (which == 101)
    {
        if (s_tg.headTurn != 0.0f)
            turn_rows(m, v3(0.0f, 0.0f, 1.0f), s_tg.headTurn);
        if (s_tg.headUp != 0.0f)
            turn_rows(m, to_pose(v3(0.0f, 1.0f, 0.0f)), -s_tg.headUp);
        if (s_tg.headSide != 0.0f)
            turn_rows(m, to_pose(v3(1.0f, 0.0f, 0.0f)), -s_tg.headSide);
        return;
    }
    const int kind = (int)which / 2, side = (int)which % 2;
    if (kind < 0 || kind >= kBoneKinds)
        return;
    const Aim& aim = s_tg.bone[kind][side];
    if (aim.w <= 0.0f)
        return;
    const V3 a = rig.axis[kind][side];
    const V3 cur = v_norm(v_add(v_add(v_scale(row_of(m, 0), a.x), v_scale(row_of(m, 1), a.y)), v_scale(row_of(m, 2), a.z)));
    const V3 want = v_norm(to_pose(aim.dir));
    const V3 cr = v_cross(cur, want);
    const float sl = sqrtf(v_dot(cr, cr)), cl = v_dot(cur, want);
    if (sl >= 1e-5f)
    {
        const float angle = atan2f(sl, cl) * (aim.w > 1.0f ? 1.0f : aim.w);
        turn_rows(m, v_scale(cr, 1.0f / sl), angle);
    }
    // r54: a forearm turned about itself (pronation / supination) until its hand's palm faces the
    // wanted way. The hand is built after this joint, from its own local turn (the idle pose's, in
    // the accumulator) times this matrix: the palm is worked out ahead from that.
    if (kind != kBoneFore || aim.palmW <= 0.0f || !rig.palmOk[side])
        return;
    const int hand = rig.hand[side];
    const RotAccum& ra = acc->m_rot.mData[hand];
    nlMatrix4 local;
    local.SetIdentity();
    if (!ra.bIdentity)
    {
        if (ra.quatAccumulatedWeight == 0.0f)
            nlMakeRotationMatrixZ(local, 0.0000958738f * ra.rotAroundZ);
        else
            nlQuatToMatrix(local, ra.q);
    }
    const V3 pl = rig.palm[side];
    // The palm in the hand's local frame -> the forearm's frame (local rows) -> the pose's space.
    const V3 inFore = v_add(v_add(v_scale(row_of(local, 0), pl.x), v_scale(row_of(local, 1), pl.y)), v_scale(row_of(local, 2), pl.z));
    const V3 palmNow = v_add(v_add(v_scale(row_of(m, 0), inFore.x), v_scale(row_of(m, 1), inFore.y)), v_scale(row_of(m, 2), inFore.z));
    const V3 b = v_norm(v_add(v_add(v_scale(row_of(m, 0), a.x), v_scale(row_of(m, 1), a.y)), v_scale(row_of(m, 2), a.z)));
    const V3 target = v_norm(to_pose(aim.palm));
    const V3 p0 = v_add(palmNow, v_scale(b, -v_dot(palmNow, b)));
    const V3 p1 = v_add(target, v_scale(b, -v_dot(target, b)));
    if (v_dot(p0, p0) < 1e-10f || v_dot(p1, p1) < 1e-10f)
        return;
    const float roll = atan2f(v_dot(b, v_cross(p0, p1)), v_dot(p0, p1));
    turn_rows(m, b, roll * (aim.palmW > 1.0f ? 1.0f : aim.palmW));
}

// Finds the joints (by their Biped names, the arm and leg chains up from the hands and feet) and
// measures each bone's direction in the idle pose; registers the callbacks on the copy's accumulator.
// False for a rig it cannot read (the show then waves, as before).
bool ensure_rig(cCharacter* ch, int cls)
{
    if (cls < 0 || cls >= NUM_FIELDER_CLASSES)
        return false;
    Rig& rig = s_rig[cls];
    cPoseAccumulator* acc = s_copy->mPoseAccumulator;
    const cSHierarchy* h = acc->m_BaseSHierarchy;
    if (!rig.tried)
    {
        rig.tried = true;
        rig.ok = false;
        const int n = h->GetNumNodes();
        const int hands[2] = { h->GetNodeIndexByID(nlStringLowerHash("bip01 l hand")),
                               h->GetNodeIndexByID(nlStringLowerHash("bip01 r hand")) };
        const int feet[2] = { h->GetNodeIndexByID(nlStringLowerHash("bip01 l foot")),
                              h->GetNodeIndexByID(nlStringLowerHash("bip01 r foot")) };
        rig.head = ch->m_nHeadJointIndex;
        rig.spine = h->GetNodeIndexByID(nlStringLowerHash("bip01 spine"));
        if (rig.spine < 0)
            rig.spine = h->GetSpineNodeIndex();
        rig.neck = rig.head >= 0 && rig.head < n ? h->GetParent(rig.head) : -1;
        bool ok = rig.head >= 0 && rig.head < n && rig.spine >= 0 && rig.spine < n;
        for (int s = 0; s < 2 && ok; s++)
        {
            ok = hands[s] > 0 && hands[s] < n && feet[s] > 0 && feet[s] < n;
            if (!ok)
                break;
            rig.bone[kBoneFore][s] = h->GetParent(hands[s]);
            rig.bone[kBoneUpper][s] = rig.bone[kBoneFore][s] > 0 ? h->GetParent(rig.bone[kBoneFore][s]) : -1;
            rig.bone[kBoneCalf][s] = h->GetParent(feet[s]);
            rig.bone[kBoneThigh][s] = rig.bone[kBoneCalf][s] > 0 ? h->GetParent(rig.bone[kBoneCalf][s]) : -1;
            for (int k = 0; k < kBoneKinds; k++)
                ok = ok && rig.bone[k][s] > 0 && rig.bone[k][s] < n;
        }
        if (ok)
        {
            // The bones' own directions, in the idle pose: toward the next joint down the chain.
            pose_inventory(ch, kAnimIdle, 0.0f, true, false);
            for (int s = 0; s < 2 && ok; s++)
            {
                const int child[kBoneKinds] = { rig.bone[kBoneFore][s], hands[s], rig.bone[kBoneCalf][s], feet[s] };
                for (int k = 0; k < kBoneKinds; k++)
                {
                    const nlMatrix4& m = acc->GetNodeMatrix(rig.bone[k][s]);
                    const V3 d = v_add(pos_of(acc->GetNodeMatrix(child[k])), v_scale(pos_of(m), -1.0f));
                    float local[3];
                    for (int r = 0; r < 3; r++)
                    {
                        const V3 row = row_of(m, r);
                        const float rr = v_dot(row, row);
                        local[r] = rr > 1e-12f ? v_dot(d, row) / rr : 0.0f;
                    }
                    rig.axis[k][s] = v3(local[0], local[1], local[2]);
                    ok = ok && v_dot(d, d) > 1e-8f;
                }
            }
            // r54: the palms (still the idle pose). The fingers' way from the wrist (the hand's
            // children, or the Biped fingers by name), the thumb's side (the child farthest off that
            // line, or "finger0"); the palm faces d x t on the left hand and t x d on the right (palm
            // down, fingers ahead: the left thumb points right, the right one left).
            for (int s = 0; s < 2 && ok; s++)
            {
                rig.hand[s] = hands[s];
                rig.palmOk[s] = false;
                const nlMatrix4& hm = acc->GetNodeMatrix(hands[s]);
                const V3 hp = pos_of(hm);
                const char* thumbName = s == 0 ? "bip01 l finger0" : "bip01 r finger0";
                const char* fingerNames[3] = { s == 0 ? "bip01 l finger1" : "bip01 r finger1",
                                               s == 0 ? "bip01 l finger2" : "bip01 r finger2",
                                               s == 0 ? "bip01 l finger3" : "bip01 r finger3" };
                int thumb = h->GetNodeIndexByID(nlStringLowerHash(thumbName));
                V3 fingers = v3(0.0f, 0.0f, 0.0f);
                int nf = 0;
                for (int f = 0; f < 3; f++)
                {
                    const int fi = h->GetNodeIndexByID(nlStringLowerHash(fingerNames[f]));
                    if (fi > 0 && fi < n)
                    {
                        fingers = v_add(fingers, v_add(pos_of(acc->GetNodeMatrix(fi)), v_scale(hp, -1.0f)));
                        nf++;
                    }
                }
                const char* how = "named fingers";
                const int kids = h->GetNumChildren(hands[s]);
                if (nf == 0 || thumb <= 0 || thumb >= n)
                {
                    // By the hand's children instead.
                    how = "hand's children";
                    fingers = v3(0.0f, 0.0f, 0.0f);
                    nf = 0;
                    for (int c = 0; c < kids; c++)
                    {
                        const int ci = h->GetChild(hands[s], c);
                        if (ci > 0 && ci < n)
                        {
                            fingers = v_add(fingers, v_add(pos_of(acc->GetNodeMatrix(ci)), v_scale(hp, -1.0f)));
                            nf++;
                        }
                    }
                    thumb = -1;
                    if (nf >= 2)
                    {
                        const V3 dd = v_norm(fingers);
                        float best = 0.0f;
                        for (int c = 0; c < kids; c++)
                        {
                            const int ci = h->GetChild(hands[s], c);
                            if (ci <= 0 || ci >= n)
                                continue;
                            const V3 off = v_add(pos_of(acc->GetNodeMatrix(ci)), v_scale(hp, -1.0f));
                            const V3 perp = v_add(off, v_scale(dd, -v_dot(off, dd)));
                            if (v_dot(perp, perp) > best)
                            {
                                best = v_dot(perp, perp);
                                thumb = ci;
                            }
                        }
                    }
                }
                if (nf > 0 && thumb > 0 && thumb < n)
                {
                    const V3 d = v_norm(fingers);
                    const V3 off = v_add(pos_of(acc->GetNodeMatrix(thumb)), v_scale(hp, -1.0f));
                    const V3 t = v_add(off, v_scale(d, -v_dot(off, d)));
                    if (v_dot(t, t) > 1e-10f)
                    {
                        const V3 palm = s == 0 ? v_norm(v_cross(d, v_norm(t))) : v_norm(v_cross(v_norm(t), d));
                        float local[3];
                        for (int r = 0; r < 3; r++)
                        {
                            const V3 row = row_of(hm, r);
                            const float rr = v_dot(row, row);
                            local[r] = rr > 1e-12f ? v_dot(palm, row) / rr : 0.0f;
                        }
                        rig.palm[s] = v3(local[0], local[1], local[2]);
                        rig.palmOk[s] = true;
                        fprintf(stderr,
                                "[port] scorer: class %d %s palm from the %s (%d children): idle palm faces (%.2f %.2f "
                                "%.2f), fingers (%.2f %.2f %.2f)\n",
                                cls, s == 0 ? "left" : "right", how, kids, palm.x, palm.y, palm.z, d.x, d.y, d.z);
                    }
                }
                if (!rig.palmOk[s])
                    fprintf(stderr, "[port] scorer: class %d %s palm not found (%d children)\n", cls,
                            s == 0 ? "left" : "right", kids);
            }
        }
        rig.ok = ok;
        fprintf(stderr,
                "[port] scorer: class %d rig %s: spine %d neck %d head %d | L upper %d fore %d thigh %d calf %d | R upper %d "
                "fore %d thigh %d calf %d\n",
                cls, ok ? "ok" : "NOT readable (waves)", rig.spine, rig.neck, rig.head, rig.bone[kBoneUpper][0],
                rig.bone[kBoneFore][0], rig.bone[kBoneThigh][0], rig.bone[kBoneCalf][0], rig.bone[kBoneUpper][1],
                rig.bone[kBoneFore][1], rig.bone[kBoneThigh][1], rig.bone[kBoneCalf][1]);
    }
    if (!rig.ok)
        return false;
    acc->SetBuildNodeMatrixCallback(rig.spine, celebrate_cb, 0, 100);
    acc->SetBuildNodeMatrixCallback(rig.head, celebrate_cb, 0, 101);
    for (int s = 0; s < 2; s++)
        for (int k = 0; k < kBoneKinds; k++)
            acc->SetBuildNodeMatrixCallback(rig.bone[k][s], celebrate_cb, 0, (unsigned int)(k * 2 + s));
    return true;
}

// The celebrations. Each fills s_tg for `t` seconds in (on its spot, facing you at 0) and gives its
// length; after it the scorer waves and stands as before.
enum Celebration
{
    kCelebrateSiu,      // Cristiano Ronaldo: run, jump with a half turn, land feet wide, arms down and out
    kCelebrateSixSeven, // "6 7": palms up, the hands bobbing up and down in turn
    // r55 (Lance: "now lets add those other celebrations"):
    kCelebrateZen,      // Erling Haaland: sits down cross-legged, hands on the knees palms up, eyes up
    kCelebrateGriddy,   // the Griddy: heel taps in turn, arms pumping, then the binoculars
    kCelebrateRobot,    // Peter Crouch: the robot, stiff arms in jerky steps, head clicking round
    kCelebrateBaby,     // Bebeto: rocking a baby in his arms side to side
    kCelebrationCount
};
const char* const kCelebrationNames[kCelebrationCount] = { "SIU", "6-7", "Zen", "Griddy", "Robot", "Rock the baby" };
// What the functions below return, and when their main pose is (logged, log_pose).
const float kCelebrationLength[kCelebrationCount] = { 3.3f, 4.1f, 4.6f, 4.2f, 4.2f, 4.0f };
const float kCelebrationMainPose[kCelebrationCount] = { 1.6f, 1.0f, 2.2f, 1.2f, 1.4f, 1.5f };
// r64 (Lance: "make the goal dances/celebrations rotate randomly, dont have it follow the same routine each
// match forever"): a shuffled bag of all six, reshuffled when used up, never the same one twice in a row.
// Its own random numbers (seeded from the clock): the game's nlDefaultSeed is never touched (lockstep).
int s_celebrationBag[kCelebrationCount];
int s_celebrationLeft = 0;  // still in the bag (taken from the end)
int s_celebrationLast = -1;
unsigned int s_celebrationRng = 0;

unsigned int celebration_rand()
{
    if (s_celebrationRng == 0)
    {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        s_celebrationRng = ((unsigned int)ts.tv_nsec * 2654435761u) ^ (unsigned int)ts.tv_sec ^ 0x9E3779B9u;
        if (s_celebrationRng == 0)
            s_celebrationRng = 1;
    }
    s_celebrationRng ^= s_celebrationRng << 13;
    s_celebrationRng ^= s_celebrationRng >> 17;
    s_celebrationRng ^= s_celebrationRng << 5;
    return s_celebrationRng;
}

int next_celebration()
{
    if (s_celebrationLeft <= 0)
    {
        for (int i = 0; i < kCelebrationCount; i++)
            s_celebrationBag[i] = i;
        for (int i = kCelebrationCount - 1; i > 0; i--)
        {
            int j = (int)(celebration_rand() % (unsigned int)(i + 1));
            int t = s_celebrationBag[i];
            s_celebrationBag[i] = s_celebrationBag[j];
            s_celebrationBag[j] = t;
        }
        // The next one out (the end) mustn't be the one just shown.
        if (s_celebrationBag[kCelebrationCount - 1] == s_celebrationLast)
        {
            int j = (int)(celebration_rand() % (unsigned int)(kCelebrationCount - 1));
            s_celebrationBag[kCelebrationCount - 1] = s_celebrationBag[j];
            s_celebrationBag[j] = s_celebrationLast;
        }
        s_celebrationLeft = kCelebrationCount;
    }
    s_celebrationLast = s_celebrationBag[--s_celebrationLeft];
    return s_celebrationLast;
}

void aim(Aim& a, float w, V3 dir)
{
    a.w = w;
    a.dir = v_norm(dir);
}
// Both sides at once: `dir` given for the left (+Y); the right mirrors it.
void aim_both(int kind, float w, V3 dirLeft)
{
    aim(s_tg.bone[kind][0], w, dirLeft);
    aim(s_tg.bone[kind][1], w, v3(dirLeft.x, -dirLeft.y, dirLeft.z));
}

float celebrate_siu(float t)
{
    // 0.00-0.30 turn your back and crouch; 0.30-0.95 the jump, turning back round in the air;
    // 0.95-1.15 the landing, feet wide; 1.15-2.70 "SIUUU": arms down and out, chest out, chin up;
    // 2.70-3.30 easing back to standing.
    const float kTurn = 0.30f, kTakeoff = 0.30f, kLand = 0.95f, kHoldEnd = 2.70f, kEnd = 3.30f;
    s_tg.spin = 3.14159265f * ease(t / kTurn) + 3.14159265f * ease((t - kTakeoff) / (kLand - kTakeoff));
    s_tg.airborne = t > kTakeoff && t < kLand;
    if (s_tg.airborne)
    {
        const float u = (t - kTakeoff) / (kLand - kTakeoff);
        s_tg.rootZ = 0.32f * 4.0f * u * (1.0f - u);
    }
    // Crouch and arms back before the jump; up and out in the air; down and out on landing.
    const float crouch = window(t, 0.05f, kTurn, kTakeoff, kTakeoff + 0.1f);
    const float air = window(t, kTakeoff - 0.05f, kTakeoff + 0.15f, kLand - 0.12f, kLand);
    const float siu = window(t, kLand - 0.1f, kLand + 0.08f, kHoldEnd, kEnd);
    const V3 armsBack = v3(-0.55f, 0.25f, -0.8f), armsUp = v3(0.1f, 0.85f, 0.35f), armsSiu = v3(-0.25f, 0.5f, -0.83f);
    if (crouch > 0.0f)
    {
        aim_both(kBoneUpper, crouch, armsBack);
        aim_both(kBoneFore, crouch, v3(-0.6f, 0.2f, -0.75f));
        aim_both(kBoneThigh, crouch, v3(0.45f, 0.12f, -0.88f));
        aim_both(kBoneCalf, crouch, v3(-0.35f, 0.08f, -0.93f));
        s_tg.spineBack = -0.25f * crouch;  // leaning forward
    }
    if (air > 0.0f)
    {
        aim_both(kBoneUpper, air, armsUp);
        aim_both(kBoneFore, air, v3(0.2f, 0.75f, 0.6f));
        aim_both(kBoneThigh, air, v3(0.35f, 0.18f, -0.92f));
        aim_both(kBoneCalf, air, v3(-0.45f, 0.12f, -0.88f));
        s_tg.spineBack = 0.1f * air;
    }
    if (siu > 0.0f)
    {
        // The shout: a little extra lean back at the start of the hold.
        const float shout = window(t, kLand, kLand + 0.25f, kLand + 0.9f, kHoldEnd);
        aim_both(kBoneUpper, siu, armsSiu);
        aim_both(kBoneFore, siu, v3(-0.32f, 0.52f, -0.79f));
        aim_both(kBoneThigh, siu, v3(0.05f, 0.42f, -0.9f));
        aim_both(kBoneCalf, siu, v3(0.0f, 0.24f, -0.97f));
        s_tg.spineBack = siu * (0.2f + 0.1f * shout);
        s_tg.headUp = siu * (0.25f + 0.15f * shout);
    }
    return kEnd;
}

float celebrate_six_seven(float t)
{
    // Forearms out in front, palms up, the hands bobbing up and down in turn (about 2.2 a second),
    // the shoulders and body swaying with them and the knees giving a little; 0.35 s in, 0.5 s out.
    const float kIn = 0.35f, kHoldEnd = 3.6f, kEnd = 4.1f;
    const float w = window(t, 0.0f, kIn, kHoldEnd, kEnd);
    const float phase = 2.0f * 3.14159265f * 2.2f * t;
    const float bob = sinf(phase);
    // r54 (Lance's photo): arms reaching out in front and a little apart, nearly straight, hands open
    // with the palms up (tipped a little toward you); r53 bent the elbows square with the palms in, and
    // in turn that read as running.
    for (int s = 0; s < 2; s++)
    {
        const float side = s == 0 ? 1.0f : -1.0f;
        const float up = side * bob;  // left up while the right is down
        aim(s_tg.bone[kBoneUpper][s], w, v3(0.72f, side * 0.42f, -0.5f + 0.1f * up));
        aim(s_tg.bone[kBoneFore][s], w, v3(0.88f, side * 0.3f, -0.05f + 0.32f * up));
        s_tg.bone[kBoneFore][s].palmW = w;
        s_tg.bone[kBoneFore][s].palm = v_norm(v3(0.35f, 0.0f, 0.94f));
    }
    s_tg.spineSide = w * 0.07f * bob;
    s_tg.headSide = -w * 0.08f * bob;
    s_tg.headUp = w * (0.05f + 0.05f * sinf(2.0f * phase));
    // Knees: a small bounce on every beat (both bones, so the planting lowers the body).
    const float knee = 0.5f + 0.5f * cosf(2.0f * phase);
    aim_both(kBoneThigh, w, v3(0.18f + 0.12f * knee, 0.1f, -0.97f));
    aim_both(kBoneCalf, w, v3(-0.12f - 0.1f * knee, 0.08f, -0.98f));
    return kEnd;
}

// r55: a palm turned (forearm roll) to face `palmLeft` (given for the left hand; the right mirrors).
void palms(float w, V3 palmLeft)
{
    s_tg.bone[kBoneFore][0].palmW = w;
    s_tg.bone[kBoneFore][0].palm = v_norm(palmLeft);
    s_tg.bone[kBoneFore][1].palmW = w;
    s_tg.bone[kBoneFore][1].palm = v_norm(v3(palmLeft.x, -palmLeft.y, palmLeft.z));
}

float celebrate_zen(float t)
{
    // Erling Haaland's zen: down onto the grass cross-legged (0.9 s), hands resting out on the knees
    // with the palms up, back straight, face turned up to the sky, breathing slowly; up again at the end.
    const float kEnd = 4.6f;
    const float sit = window(t, 0.0f, 0.9f, 3.8f, kEnd);
    const float breath = sinf(2.0f * 3.14159265f * 0.35f * t);
    // Thighs out to the sides and forward, nearly flat; shins folded across toward the other side.
    aim_both(kBoneThigh, sit, v3(0.55f, 0.78f, -0.12f));
    aim_both(kBoneCalf, sit, v3(0.25f, -0.95f, -0.08f));
    // Arms down and out, the forearms reaching to the knees, palms up.
    aim_both(kBoneUpper, sit, v3(0.3f, 0.5f, -0.81f));
    aim_both(kBoneFore, sit, v3(0.55f, 0.62f, -0.56f));
    palms(sit, v3(0.0f, 0.0f, 1.0f));
    s_tg.spineBack = sit * (0.04f + 0.025f * breath);
    s_tg.headUp = sit * (0.2f + 0.03f * breath);
    // Sitting on the grass, not in it (the feet and knees alone would plant the hips at toe height).
    s_tg.plantLift = 0.06f * sit;
    return kEnd;
}

float celebrate_griddy(float t)
{
    // The Griddy: heel taps out in front, one foot then the other, on a little hop, the arms pumping
    // against the legs and the shoulders turning with them; it ends with the hands up round the eyes
    // as binoculars.
    const float kEnd = 4.2f;
    const float w = window(t, 0.0f, 0.25f, 3.65f, kEnd);
    const float bino = window(t, 2.75f, 2.95f, 3.65f, 4.0f);
    const float dance = w * (1.0f - bino);
    const float phase = 2.0f * 3.14159265f * 1.8f * t;
    const float sw = sinf(phase);
    for (int s = 0; s < 2; s++)
    {
        const float side = s == 0 ? 1.0f : -1.0f;
        const float tap = fmaxf(0.0f, side * sw);  // left taps while sw > 0, right while sw < 0
        // A tapping leg reaches its heel out in front; the other stands.
        aim(s_tg.bone[kBoneThigh][s], w, v_lerp(v3(0.05f, side * 0.1f, -1.0f), v3(0.45f, side * 0.08f, -0.89f), tap * dance));
        aim(s_tg.bone[kBoneCalf][s], w, v_lerp(v3(-0.05f, side * 0.08f, -1.0f), v3(0.62f, side * 0.05f, -0.78f), tap * dance));
        // The arm on the other side swings forward with it (left arm forward while the right taps).
        const float fwd = 0.5f - 0.5f * side * sw;
        V3 upper = v_lerp(v3(-0.45f, side * 0.25f, -0.86f), v3(0.55f, side * 0.25f, -0.8f), fwd);
        V3 fore = v_lerp(v3(-0.25f, side * 0.2f, -0.95f), v3(0.75f, side * 0.15f, 0.3f), fwd);
        // The binoculars: elbows out, hands up in front of the eyes.
        upper = v_lerp(upper, v3(0.5f, side * 0.45f, -0.74f), bino);
        fore = v_lerp(fore, v3(0.1f, -side * 0.35f, 0.93f), bino);
        aim(s_tg.bone[kBoneUpper][s], w, upper);
        aim(s_tg.bone[kBoneFore][s], w, fore);
    }
    palms(w * bino, v3(-1.0f, 0.0f, 0.0f));
    s_tg.spineTwist = 0.18f * sw * dance;
    s_tg.spineBack = 0.06f * bino * w;
    s_tg.headUp = w * (0.05f * fabsf(sw) * (1.0f - bino) + 0.05f * bino);
    return kEnd;
}

float celebrate_robot(float t)
{
    // Peter Crouch's robot: elbows tight, forearms out in front, one up and one down, switching in
    // jerky steps (snapped in 70 ms every 0.42 s) with the shoulders and head clicking round with them;
    // legs stiff, a little step on each beat.
    const float kEnd = 4.2f, kStep = 0.42f;
    const float w = window(t, 0.0f, 0.2f, 3.8f, kEnd);
    const int step = (int)floorf(t / kStep);
    const float snap = ease((t - step * kStep) / 0.07f);
    // Pose a: left forearm up, right down, turned left. Pose b mirrors it.
    const float now = (step & 1) ? -1.0f : 1.0f, before = -now;
    const float turn = before + (now - before) * snap;  // -1..1, left = +1
    for (int s = 0; s < 2; s++)
    {
        const float side = s == 0 ? 1.0f : -1.0f;
        const float up = 0.5f + 0.5f * side * turn;  // 1 = this forearm up
        aim(s_tg.bone[kBoneUpper][s], w, v3(0.25f, side * 0.22f, -0.94f));
        aim(s_tg.bone[kBoneFore][s], w, v_lerp(v3(0.85f, side * 0.05f, -0.5f), v3(0.7f, side * 0.05f, 0.7f), up));
        // Stiff legs, the up arm's side stepping a little.
        aim(s_tg.bone[kBoneThigh][s], w, v_lerp(v3(0.02f, side * 0.1f, -1.0f), v3(0.25f, side * 0.1f, -0.97f), up * 0.6f));
        aim(s_tg.bone[kBoneCalf][s], w, v_lerp(v3(0.0f, side * 0.08f, -1.0f), v3(-0.15f, side * 0.08f, -0.99f), up * 0.6f));
    }
    palms(w, v3(0.0f, -1.0f, 0.0f));  // palms in, facing each other
    s_tg.spineTwist = 0.2f * turn * w;
    s_tg.headTurn = 0.35f * turn * w;
    return kEnd;
}

float celebrate_baby(float t)
{
    // Bebeto's rock the baby: arms cradled across the stomach, palms up, swaying the "baby" side to
    // side (just under one sway a second), the body turning and leaning with it, looking down at it.
    const float kEnd = 4.0f;
    const float w = window(t, 0.0f, 0.4f, 3.4f, kEnd);
    const float rock = sinf(2.0f * 3.14159265f * 0.9f * t);
    for (int s = 0; s < 2; s++)
    {
        const float side = s == 0 ? 1.0f : -1.0f;
        aim(s_tg.bone[kBoneUpper][s], w, v3(0.45f, side * 0.35f, -0.82f));
        // Across to the other side, the left forearm over the right; the cradle tips with the rock.
        aim(s_tg.bone[kBoneFore][s], w, v3(0.3f, -side * 0.92f, 0.12f + side * 0.05f + side * 0.18f * rock));
    }
    palms(w, v3(0.0f, 0.0f, 1.0f));
    s_tg.spineTwist = 0.22f * rock * w;
    s_tg.spineSide = 0.05f * rock * w;
    s_tg.headSide = -0.06f * rock * w;
    s_tg.headUp = -0.12f * w;  // looking down at the baby
    aim_both(kBoneThigh, w, v3(0.15f, 0.12f, -0.98f));
    aim_both(kBoneCalf, w, v3(-0.1f, 0.1f, -0.99f));
    return kEnd;
}

// Poses the copy for celebration `which` at `t`: idle underneath, the targets on top, the feet on
// the ground unless it is in the air. Returns the celebration's length.
float pose_procedural(cCharacter* ch, int cls, int which, float t, float height)
{
    memset(&s_tg, 0, sizeof(s_tg));
    s_tg.active = true;
    s_tgClass = cls;
    float length = 0.0f;
    switch (which)
    {
    case kCelebrateSixSeven: length = celebrate_six_seven(t); break;
    case kCelebrateZen: length = celebrate_zen(t); break;
    case kCelebrateGriddy: length = celebrate_griddy(t); break;
    case kCelebrateRobot: length = celebrate_robot(t); break;
    case kCelebrateBaby: length = celebrate_baby(t); break;
    default: length = celebrate_siu(t); break;
    }
    cAnimInventory* inv = ch->m_pAnimInventory;
    cSAnim* idle = inv != NULL ? inv->GetAnim(kAnimIdle) : NULL;
    cPoseAccumulator* acc = s_copy->mPoseAccumulator;
    if (idle != NULL)
    {
        const AnimRetarget* retarget =
            ch->m_pAnimRetargetList != NULL ? ch->m_pAnimRetargetList->GetAnimRetargetWithSignature(idle) : NULL;
        const float duration = idle->GetDuration();
        float tn = duration > 0.0f ? t / duration : 0.0f;
        tn -= floorf(tn);
        ::new (&s_ctrl) cPN_SAnimController(idle, retarget, inv->GetPlayMode(kAnimIdle), NULL, 0, inv->GetMirrored(kAnimIdle));
        s_ctrl.m_bIgnoreTriggers = true;
        s_ctrl.SetTime(tn);
        acc->InitAccumulators();
        s_ctrl.Evaluate(1.0f, acc);
    }
    nlMatrix4 world;
    nlMakeRotationMatrixZ(world, s_tg.spin);
    world.m41 = 0.0f;
    world.m42 = 0.0f;
    world.m43 = s_tg.rootZ * height;
    world.m44 = 1.0f;
    acc->BuildNodeMatrices(world);
    if (!s_tg.airborne && s_groundMeasured[cls])
    {
        // Feet on the ground: a wide stance or bent knees bring the body down, as they would.
        const float lo = lowest_joint(cls);
        const float lift = s_tg.plantLift * height;  // r55: sitting (the zen)
        if (lo > -100.0f && lo < 100.0f && (fabsf(lo - s_restZ[cls]) > 0.002f || lift > 0.0f))
        {
            world.m43 += s_restZ[cls] - lo + lift;
            acc->BuildNodeMatrices(world);
        }
    }
    else
    {
        keep_on_ground(ch, world);
    }
    s_tg.active = false;
    return length;
}

// Where the hands, feet and head are (in heights, character space before the spin), once per
// celebration at its main pose: to check a celebration's shape from a player's log.
void log_pose(const char* what, int cls, float t, float height)
{
    const Rig& rig = s_rig[cls];
    const cPoseAccumulator* acc = s_copy->mPoseAccumulator;
    const int joints[5] = { rig.head, rig.bone[kBoneFore][0], rig.bone[kBoneFore][1], rig.bone[kBoneCalf][0], rig.bone[kBoneCalf][1] };
    const char* names[5] = { "head", "L elbow", "R elbow", "L knee", "R knee" };
    char line[640];
    int used = snprintf(line, sizeof(line), "[port] scorer: %s pose at %.2f s (class %d, heights):", what, t, cls);
    for (int i = 0; i < 5 && used > 0 && used < (int)sizeof(line); i++)
    {
        const nlMatrix4& m = acc->GetNodeMatrix(joints[i]);
        used += snprintf(line + used, sizeof(line) - used, " %s (%.2f %.2f %.2f)", names[i], m.m41 / height,
                         m.m42 / height, m.m43 / height);
    }
    // r54: which way each palm faces (unit, character space; +z up).
    for (int s = 0; s < 2 && used > 0 && used < (int)sizeof(line); s++)
    {
        if (!rig.palmOk[s])
            continue;
        const nlMatrix4& hm = acc->GetNodeMatrix(rig.hand[s]);
        const V3 pl = rig.palm[s];
        const V3 pw = v_norm(v_add(v_add(v_scale(row_of(hm, 0), pl.x), v_scale(row_of(hm, 1), pl.y)), v_scale(row_of(hm, 2), pl.z)));
        used += snprintf(line + used, sizeof(line) - used, " %s palm (%.2f %.2f %.2f)", s == 0 ? "L" : "R", pw.x, pw.y, pw.z);
    }
    fprintf(stderr, "%s\n", line);
}

// The goal cut-scene's clip, played the way the cut-scene plays it (Nis::Render): with the clip's
// own root height and turn, so dives and knee slides stay on the ground (Lance, r42: "spun around
// inside the grass" when only the body moved). Its travel is kept to 0.6 m of the spot, so a slide
// toward you never reaches you, and it starts facing you.
void pose_celebration(cCharacter* ch, float seconds, float height)
{
    cSAnim* anim = s_show.celebrate;
    const bool mirror = s_show.celebrateMirror;
    cPoseAccumulator* acc = s_copy->mPoseAccumulator;
    const float duration = anim->GetDuration();
    float tn = duration > 0.0f ? seconds / duration : 1.0f;
    tn = tn < 0.0f ? 0.0f : (tn > 1.0f ? 1.0f : tn);
    ::new (&s_ctrl) cPN_SAnimController(anim, NULL, PM_HOLD, NULL, 0, mirror);
    s_ctrl.m_bIgnoreTriggers = true;
    s_ctrl.SetTime(tn);
    acc->InitAccumulators();
    s_ctrl.Evaluate(1.0f, acc);

    nlVector3 rt = { 0.0f, 0.0f, 0.0f }, rt0 = { 0.0f, 0.0f, 0.0f };
    unsigned short a = 0, a0 = 0;
    anim->GetRootTrans(tn, &rt);
    anim->GetRootTrans(0.0f, &rt0);
    anim->GetRootRot(tn, &a);
    anim->GetRootRot(0.0f, &a0);
    if (mirror)
    {
        rt.x = -rt.x;
        rt0.x = -rt0.x;
        a = (unsigned short)(a + (0x4000 - a) * 2);
        a0 = (unsigned short)(a0 + (0x4000 - a0) * 2);
    }
    // The travel since the clip's start, turned so its start faces +X (toward you), and capped.
    const float start = 0.0000958738f * (float)a0;
    const float dx = rt.x - rt0.x, dy = rt.y - rt0.y;
    const float c = cosf(-start), s = sinf(-start);
    float lx = dx * c - dy * s, ly = dx * s + dy * c;
    const float maxUnits = 0.6f * height / PORT_SCORER_HEIGHT;
    const float d = sqrtf(lx * lx + ly * ly);
    if (d > maxUnits)
    {
        lx *= maxUnits / d;
        ly *= maxUnits / d;
    }
    nlMatrix4 world;
    nlMakeRotationMatrixZ(world, 0.0000958738f * (float)(unsigned short)(a - a0));
    world.m41 = lx;
    world.m42 = ly;
    world.m43 = rt.z;
    world.m44 = 1.0f;
    acc->BuildNodeMatrices(world);
    // r44i: the cut-scene's root height is its own stage's: on Waluigi it put his legs under the pitch.
    keep_on_ground(ch, world);
}

// `flip`: mirrored from how the inventory plays it (r43: the wave with the other hand).
void pose_inventory(cCharacter* ch, int id, float seconds, bool loop, bool flip = false)
{
    cAnimInventory* inv = ch->m_pAnimInventory;
    cSAnim* anim = inv != NULL ? inv->GetAnim(id) : NULL;
    if (anim == NULL)
        return;
    const AnimRetarget* retarget =
        ch->m_pAnimRetargetList != NULL ? ch->m_pAnimRetargetList->GetAnimRetargetWithSignature(anim) : NULL;
    pose(ch, anim, retarget, inv->GetPlayMode(id), inv->GetMirrored(id) != flip, seconds, loop);
}

// r43d: whether the copy's last pose looks like a character on its spot: the head joint finite,
// between knee and well over head height, and within two heights of the spot sideways.
bool pose_sane(cCharacter* ch, float height)
{
    const nlMatrix4& m = s_copy->mPoseAccumulator->GetNodeMatrix(ch->m_nHeadJointIndex);
    const float x = m.m41, y = m.m42, z = m.m43;
    if (!(x == x && y == y && z == z))  // NaN
        return false;
    const float row = m.m11 * m.m11 + m.m12 * m.m12 + m.m13 * m.m13;
    if (!(row > 0.01f && row < 100.0f))
        return false;
    return z > 0.15f * height && z < 2.5f * height && fabsf(x) < 2.0f * height && fabsf(y) < 2.0f * height;
}

// The model's own height in game units: its skinned vertices in the bind pose, feet to the top of
// the head (hats, Toad's cap and all). The head joint in the idle pose checks it (a model that is
// not Z-up, or has no such vertices, falls back to that).
float measure_height(cCharacter* ch, int cls)
{
    if (cls >= 0 && cls < NUM_FIELDER_CLASSES && s_height[cls] > 0.0f)
        return s_height[cls];
    float lo = 1e9f, hi = -1e9f;
    const ShaderSkinMesh* mesh = (const ShaderSkinMesh*)ch->GetSkinMesh();
    if (mesh != NULL && mesh->softwareVertices != NULL)
    {
        for (int i = 0; i < mesh->numSoftwareVerts; i++)
        {
            const float z = mesh->softwareVertices[i].position.z;
            lo = z < lo ? z : lo;
            hi = z > hi ? z : hi;
        }
    }
    float h = hi - lo;
    pose_inventory(ch, kAnimIdle, 0.0f, true);
    const float head = s_copy->mPoseAccumulator->GetNodeMatrix(ch->m_nHeadJointIndex).m43;
    // r44i: the standing pose's ground, for keep_on_ground (before it is measured, nothing is moved).
    if (cls >= 0 && cls < NUM_FIELDER_CLASSES)
    {
        const cPoseAccumulator* acc = s_copy->mPoseAccumulator;
        const int n = acc->GetNumNodes() < kMaxGroundNodes ? acc->GetNumNodes() : kMaxGroundNodes;
        float rest = 1e9f;
        for (int i = 0; i < n; i++)
        {
            const nlMatrix4& m = acc->GetNodeMatrix(i);
            s_rootNode[cls][i] = fabsf(m.m41) < 1e-4f && fabsf(m.m42) < 1e-4f && fabsf(m.m43) < 1e-4f;
            if (!s_rootNode[cls][i] && m.m43 == m.m43 && m.m43 < rest)
                rest = m.m43;
        }
        s_restZ[cls] = rest < 1e8f ? rest : 0.0f;
        s_groundMeasured[cls] = true;
        fprintf(stderr, "[port] scorer: class %d stands with its lowest joint at %.3f units\n", cls, s_restZ[cls]);
    }
    // (Toad's skin is 2.2 x his head joint, Donkey Kong's 2.4: big heads and hunched backs.)
    if (!(h > head * 0.9f && h < head * 3.2f))
        h = head > 0.1f ? head * 1.3f : 1.4f;
    fprintf(stderr, "[port] scorer: class %d height %.3f units (skin %.3f, head joint %.3f)\n", cls, h, hi - lo,
            head);
    if (cls >= 0 && cls < NUM_FIELDER_CLASSES)
        s_height[cls] = h;
    return h;
}

// The cut-scene's clip for this character, while the goal cut-scene plays. It stays in NisPlayer's
// memory until the next-but-one cut-scene loads, well after the show; not for GoalSuddenDeath, whose
// end-of-game cut-scenes follow at once.
void take_celebration(const Presentation& pres)
{
    if (s_show.celebrate != NULL || nlTaskManager::m_pInstance->m_CurrState != 0x100 ||
        strcmp(pres.mCurrentFunction, "GoalSuddenDeath") == 0)
        return;
    NisPlayer* nis = NisPlayer::Instance();
    for (int i = 0; i < 4; i++)
    {
        Nis* n = nis->mPlaying[i];
        if (n == NULL || s_show.index < 0 || s_show.index >= Nis::MAX_NUM_CHARACTERS)
            continue;
        cPN_SAnimController* c = n->mCharacterControllers[s_show.index];
        if (c != NULL && c->m_pSAnim != NULL)
        {
            s_show.celebrate = c->m_pSAnim;
            s_show.celebrateMirror = c->m_bMirror;
            fprintf(stderr, "[port] scorer: celebration clip %.2f s from the cut-scene\n", c->m_pSAnim->GetDuration());
            return;
        }
    }
}

} // namespace

void XrScorerGoal(const cCharacter* scorer, int side, bool superStrike)
{
    release_copy();
    memset(&s_show, 0, sizeof(s_show));
    s_show.index = -1;
    if (superStrike)
        return;
    int index = scorer != NULL ? GetCharacterIndex(scorer) : -1;
    // Keepers (Kritter) and anyone unknown: the team's captain comes over instead.
    if (index < 0 || index >= 8 || scorer->m_eClassType == GOALIE)
    {
        const cCharacter* captain = captain_of(side);
        index = captain != NULL ? GetCharacterIndex(captain) : -1;
    }
    if (index < 0 || index >= 8 || g_pCharacters[index] == NULL)
        return;
    const cCharacter* captain = captain_of(side);
    s_show.armed = true;
    s_show.who = g_pCharacters[index];
    s_show.index = index;
    s_show.side = side;
    s_show.captainClass = captain != NULL ? (int)captain->m_eCharacterClass : -1;
    s_show.prevHold = true;
    s_show.leaveAt = PORT_SCORER_LEAVE;
    // r64: a random one from the shuffled bag (render only: no shared random number is drawn).
    s_show.procedural = next_celebration();
}

void XrScorerRender()
{
    const Presentation& pres = Presentation::Instance();
    const bool goalFn = strncmp(pres.mCurrentFunction, "Goal", 4) == 0;
    const int frames = pres.mGoalShowFrames;
    int phase = 0;
    float phaseTime = 0.0f;

    if (s_show.armed && goalFn && frames >= 0)
    {
        s_show.started = true;
        if (s_show.who != g_pCharacters[s_show.index])
        {
            s_show.armed = false; // the match went away under us
        }
        else
        {
            const float t = frames / 60.0f;
            // A skip (the hold dropped) sends the scorer straight back, or, before it came, calls the
            // show off.
            if (s_show.prevHold && !pres.mGoalShowHold && t < s_show.leaveAt)
            {
                if (t < PORT_SCORER_APPEAR)
                    s_show.armed = false;
                s_show.leaveAt = t;
            }
            s_show.prevHold = pres.mGoalShowHold;
            take_celebration(pres);
            if (t >= PORT_SCORER_APPEAR)
            {
                if (t < PORT_SCORER_ARRIVE && t < s_show.leaveAt)
                {
                    phase = 1;
                    phaseTime = t - PORT_SCORER_APPEAR;
                }
                else if (t < s_show.leaveAt)
                {
                    phase = 2;
                    phaseTime = t - PORT_SCORER_ARRIVE;
                }
                else if (t < s_show.leaveAt + PORT_SCORER_RUNOUT)
                {
                    phase = 3;
                    phaseTime = t - s_show.leaveAt;
                }
            }
        }
    }
    else if (s_show.armed && s_show.started && !goalFn)
    {
        s_show.armed = false; // the goal script is over
    }

    if (phase == 0)
    {
        if (!s_show.armed)
            release_copy();
        PortXrScorerFrame(0, 0.0f, 0.0f, -1, 0, -1);
        return;
    }

    cCharacter* ch = g_pCharacters[s_show.index];
    if (s_copy == NULL)
        s_copy = new DrawableCharacter();
    if (s_copy->mPoseAccumulator == NULL)
        s_copy->mPoseAccumulator = new cPoseAccumulator(ch->m_pPoseAccumulator->m_BaseSHierarchy, false);
    const int cls = (int)ch->m_eCharacterClass;
    const float height = measure_height(ch, cls);

    if (phase == 1 || phase == 3)
    {
        // The jog at the pace the life-size copy covers the ground (PORT_SCORER_SPEED): no foot slide.
        cSAnim* jog = ch->m_pAnimInventory != NULL ? ch->m_pAnimInventory->GetAnim(kAnimJog) : NULL;
        const float unitsPerSecond = PORT_SCORER_SPEED * height / PORT_SCORER_HEIGHT;
        float rate = jog != NULL && jog->m_fLinearSpeed > 0.01f ? unitsPerSecond / jog->m_fLinearSpeed : 1.0f;
        rate = rate < 0.6f ? 0.6f : (rate > 1.8f ? 1.8f : rate);
        pose_inventory(ch, kAnimJog, phaseTime * rate, true);
    }
    else
    {
        // r43 (Lance: "simply approach the player and wave and/or dance"): the character's own goal
        // celebration as its dance (from the cut-scene, kept on its spot), a wave, a wave with the
        // other hand, then idle until it runs back. Without a cut-scene clip, two waves.
        // r43d: the dance first, on arrival, and checked every frame. In r43c it came after the first
        // wave and the copy vanished for the length of the clip (Lance's video: Koopa gone 47.9-51.0 s),
        // so a clip that no longer gives a sane pose is dropped for the rest of the show.
        cSAnim* wave = ch->m_pAnimInventory != NULL ? ch->m_pAnimInventory->GetAnim(kAnimWave) : NULL;
        const float wd = wave != NULL ? wave->GetDuration() : 0.0f;
        float t = phaseTime;
        bool posed = false;
        // r53: the port's own celebration first (SIU, 6-7, ...), checked every frame like the clip was;
        // the cut-scene's clip is no longer used.
        if (s_show.procedural >= 0 && s_show.procedural < kCelebrationCount && !s_show.celebrateBad &&
            ensure_rig(ch, cls))
        {
            const int which = s_show.procedural;
            const float length = kCelebrationLength[which];
            if (t < length)
            {
                pose_procedural(ch, cls, which, t, height);
                if (pose_sane(ch, height))
                {
                    posed = true;
                    if (!s_show.poseLogged && t >= kCelebrationMainPose[which])
                    {
                        s_show.poseLogged = true;
                        log_pose(kCelebrationNames[which], cls, t, height);
                    }
                }
                else
                {
                    s_show.celebrateBad = true;
                    fprintf(stderr, "[port] scorer: %s gave no sane pose at %.2f s, waving instead\n",
                            kCelebrationNames[which], t);
                }
            }
            if (!s_show.celebrateBad)
                t -= length;
        }
        else if (s_show.celebrate != NULL && !s_show.celebrateBad && s_show.procedural < 0)
        {
            const float cd = s_show.celebrate->GetDuration();
            if (t < cd)
            {
                pose_celebration(ch, t, height);
                if (pose_sane(ch, height))
                {
                    posed = true;
                }
                else
                {
                    s_show.celebrateBad = true;
                    fprintf(stderr, "[port] scorer: celebration clip gave no sane pose at %.2f s, waving instead\n", t);
                }
            }
            if (!s_show.celebrateBad)
                t -= cd;
        }
        if (!posed)
        {
            if (t < wd)
                pose_inventory(ch, kAnimWave, t, false);
            else if ((t -= wd) < wd)
                pose_inventory(ch, kAnimWave, t, false, true);
            else
                pose_inventory(ch, kAnimIdle, t - wd, true);
        }
    }

    // Sent like the game's own characters, but with a matrix handle of its own (the scorer layer
    // draws it alone; every other send leaves it out), no camera test and no shadow.
    s_copy->mCharacter = ch;
    s_copy->mVisible = true;
    s_copy->mPosition.x = s_copy->mPosition.y = s_copy->mPosition.z = 0.0f;
    s_copy->mBip01Position = s_copy->mPosition;
    s_copy->mFacingDirection = 0;
    s_copy->mHeadSpin = s_copy->mHeadTilt = 0;
    s_copy->mDirt = 0;
    s_copy->mEffectsTexturing = fxGetTexturing(eFXTex_Nothing);
    const unsigned char shadows = DrawableCharacter::sShadowRenderingDisabled;
    DrawableCharacter::sShadowRenderingDisabled = 1;
    DrawableCharacter::sXrIdentity.SetIdentity();
    DrawableCharacter::sXrOwnMatrix = true;
    ch->PoseSkinMesh(s_copy->mPoseAccumulator);
    s_copy->SendToGl(*ch);
    DrawableCharacter::sXrOwnMatrix = false;
    DrawableCharacter::sShadowRenderingDisabled = shadows;
    const GLSkinMesh* mesh = ch->GetSkinMesh();
    PortXrSetScorerHandle(mesh != NULL && mesh->pModel != NULL && mesh->pModel->numPackets > 0
                              ? mesh->pModel->packets[0].state.matrix
                              : 0);

    if (!s_show.logged)
    {
        s_show.logged = true;
        fprintf(stderr, "[port] scorer: index %d class %d side %d, %s\n", s_show.index, cls, s_show.side,
                s_show.procedural >= 0 && s_show.procedural < kCelebrationCount
                    ? kCelebrationNames[s_show.procedural]
                    : (s_show.celebrate != NULL ? "cut-scene celebration" : "wave"));
    }
    // r43d (Lance: stats per player per game on the banner; a hat trick is three regular goals, Super
    // Strike goals count in the stats only): the game's own tally (StatsTracker, the same on both
    // headsets online). A keeper's goal shows the captain, and so the captain's numbers.
    int goals = -1, assists = 0, hatTrick = 0;
    StatsTracker* stats = nlSingleton<StatsTracker>::s_pInstance;
    const cPlayer* player = ch->m_eClassType != GOALIE ? static_cast<const cPlayer*>(ch) : NULL;
    if (stats != NULL && player != NULL && player->m_pTeam != NULL)
    {
        const int team = player->m_pTeam->m_nSide, id = player->m_ID;
        if (team >= 0 && team < 2 && id >= 0 && id < 5)
        {
            const PlayerStats& ps = stats->mCurrentPlayerStats[team][id];
            goals = ps.mNumGoalsFor;
            assists = ps.mNumAssists;
            hatTrick = (int)ps.mNumGoalsFor - (int)ps.mNumShootToScoreGoals == 3 ? 1 : 0;
        }
    }
    PortXrScorerStats(goals, assists, hatTrick);
    PortXrScorerFrame(phase, phaseTime, height, cls, s_show.side, s_show.captainClass);
}
