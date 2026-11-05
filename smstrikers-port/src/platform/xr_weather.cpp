// strikers-quest r137: weather + night games, looks only (see xr_weather.h). This file is built with the game (its
// renderer's headers, like franchise_show.cpp): the setting, the pick per match, and what the game's own renderer draws.
// The headset's parts (eye-layer grade, the effects layer) are in xr_quest.cpp / xr_fx.cpp and read the C API below.
//
// r148 (Lance's r147 snow match, Jumbotron · Stadium at Konga Coliseum, 57 fps): the game-side looks made as cheap as
// they go. Per pass now (draws): the broadcast picture 2-6 (was 6-9 plus a depth clear of the whole picture), the
// Tabletop field 1, the life-size stadium 0 (was 1-3, the frost a translucent layer over the whole pitch round you).
// - one texture (an atlas: soft dot | ring | snow lumps), so every batch below is one draw
// - the pitch's dressing (splashes, drifts, frost, floodlit pools, the wet sheen) one batch in GLV_UnsortedPerspective,
//   a view the life-size stadium pass leaves out (its "actors" views): on the big screen and the Tabletop field only
// - the camera's rain / snow and the night's floodlight glows + beams as 2D quads in GLV_UnsortedOrtho (projected
//   here): no GLV_CameraSpace any more, so no extra view and no depth clear of the whole picture
// - the grade: 1-3 full-picture passes on the 2D picture only (night 1, snow 1, rain 2), none while the Tabletop's
//   normal view is its HUD-only scoreboard
// - counts follow the effects layer's load scale (PortWeatherSetLoad)
#include "xr_weather.h"
#include "xr_weather_cal.h"

#include "NL/nlMemory.h"
#include "NL/gl/glDraw2.h"
#include "NL/gl/glMatrix.h"
#include "NL/gl/glState.h"
#include "NL/gl/glView.h"
#include "NL/glx/glxTexture.h"
#include "Game/GL/gluMeshWriter.h"
#include "Game/Render/FlareHandler.h"
#include "port/xr_layers.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <mutex>
#include <new>

extern "C" int PortXrHudOnlyNormalView(void);

namespace
{

// ------------------------------------------------------------------------------------------------ the setting + pick
const char* const kModeNames[PORT_WX_MODES] = {"RANDOM", "CLEAR", "RAIN", "SNOW", "NIGHT", "OFF"};

int mode_from_text(const char* t)
{
    if (t == NULL || t[0] == '\0')
        return PORT_WX_MODE_RANDOM;
    if (t[0] >= '0' && t[0] <= '9')
    {
        const int v = atoi(t);
        return v >= 0 && v < PORT_WX_MODES ? v : PORT_WX_MODE_RANDOM;
    }
    for (int m = 0; m < PORT_WX_MODES; m++)
    {
        const char* n = kModeNames[m];
        int i = 0;
        while (n[i] != '\0' && t[i] != '\0' && (t[i] == n[i] || t[i] == n[i] - 'A' + 'a'))
            i++;
        if (n[i] == '\0' && (t[i] == '\0' || t[i] == ' ' || t[i] == '\r' || t[i] == '\n'))
            return m;
    }
    return PORT_WX_MODE_RANDOM;
}

std::atomic<int> s_mode(-1);
std::atomic<int> s_bits(0);
std::atomic<float> s_fade(0.0f);
std::atomic<float> s_cover(0.0f);
std::atomic<float> s_load(1.0f);  // r148: the effects layer's load scale (0.25..1)
int s_stadium = -1;
int s_franchiseBits = -1;  // the calendar's pick for the match on (-1 not a franchise match)
bool s_quickRandom = false;
uint32_t s_dice = 0;

uint32_t roll()
{
    if (s_dice == 0)
        s_dice = wxcal::mix((uint32_t)time(NULL) ^ (uint32_t)(uintptr_t)&s_dice) | 1u;
    s_dice = wxcal::mix(s_dice + 0x9E3779B9u);
    return s_dice;
}
float roll01() { return (roll() & 0xFFFFu) / 65535.0f; }

int stadium_climate(int stadium) { return stadium == 6 ? wxcal::kClimateCovered : wxcal::kClimateNormal; }

// A quick match's weather for a setting.
int quick_pick(int mode)
{
    switch (mode)
    {
    case PORT_WX_MODE_RAIN:
        return PORT_WX_RAIN;
    case PORT_WX_MODE_SNOW:
        return PORT_WX_SNOW;
    case PORT_WX_MODE_NIGHT:
        return PORT_WX_NIGHT;
    case PORT_WX_MODE_RANDOM:
    {
        const int r = (int)(roll() % 100u);
        int bits = 0;
        if (r < 60)
            bits = 0;
        else if (r < 75)
            bits = PORT_WX_RAIN;
        else if (r < 85)
            bits = PORT_WX_SNOW;
        else
            bits = PORT_WX_NIGHT;
        if ((bits & (PORT_WX_RAIN | PORT_WX_SNOW)) != 0 && roll() % 100u < 30u)
            bits |= PORT_WX_NIGHT;
        return bits;
    }
    default:
        return 0;
    }
}

const char* bits_text(int bits)
{
    switch (bits & 7)
    {
    case 0:
        return "clear";
    case PORT_WX_RAIN:
        return "rain";
    case PORT_WX_SNOW:
        return "snow";
    case PORT_WX_NIGHT:
        return "night";
    case PORT_WX_RAIN | PORT_WX_NIGHT:
        return "rain at night";
    case PORT_WX_SNOW | PORT_WX_NIGHT:
        return "snow at night";
    default:
        return "rain + snow";
    }
}

void apply(int bits, const char* why)
{
    const int was = s_bits.load();
    s_bits.store(bits);
    if (was != bits)
        s_fade.store(0.0f);
    fprintf(stderr, "[weather] %s (%s) at stadium %d\n", bits_text(bits), why, s_stadium);
    fflush(stderr);
}

// ------------------------------------------------------------------------------------------------ the atlas
// One small RGBA8 picture of our own (nlMalloc'd, kept for good like franchise_show.cpp's logos), 128 x 64:
// a soft dot (u 0-0.25, v 0-0.5), a ring (u 0.25-0.5, v 0-0.5), lumpy snow (u 0.5-1, v 0-1). Clamped, never tiled.
const int kAtlasW = 128, kAtlasH = 64;
struct Rect
{
    float u0, v0, u1, v1;
};
const Rect kDot = {0.5f / kAtlasW, 0.5f / kAtlasH, 31.5f / kAtlasW, 31.5f / kAtlasH};
const Rect kRing = {32.5f / kAtlasW, 0.5f / kAtlasH, 63.5f / kAtlasW, 31.5f / kAtlasH};
const Rect kSnow = {64.5f / kAtlasW, 0.5f / kAtlasH, 127.5f / kAtlasW, 63.5f / kAtlasH};

float snow_lumps(int x, int y, int w, int h)
{
    float n = 0.0f, amp = 0.55f;
    for (int o = 0; o < 4; o++)
    {
        const int cells = 4 << o;
        const float fx = (float)x / w * cells, fy = (float)y / h * cells;
        const int ix = (int)fx, iy = (int)fy;
        const float tx = fx - ix, ty = fy - iy;
        float c[4];
        for (int k = 0; k < 4; k++)
        {
            const int cx = (ix + (k & 1)) % cells, cy = (iy + (k >> 1)) % cells;
            c[k] = (wxcal::hash3((uint32_t)cx, (uint32_t)cy, (uint32_t)o) & 1023u) / 1023.0f;
        }
        const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
        n += amp * ((c[0] * (1 - sx) + c[1] * sx) * (1 - sy) + (c[2] * (1 - sx) + c[3] * sx) * sy);
        amp *= 0.5f;
    }
    const float a = n * 1.6f - 0.35f;
    return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
}

PlatTexture* make_atlas()
{
    void* mem = nlMalloc(sizeof(PlatTexture), 32, false);
    if (mem == NULL)
        return NULL;
    PlatTexture* tex = new (mem) PlatTexture();
    tex->Create(kAtlasW, kAtlasH, GXTex_RGBA8, 1, true, true);
    if (tex->m_LinearData == NULL || tex->m_SwizzledData == NULL)
        return NULL;
    unsigned char* px = (unsigned char*)tex->m_LinearData;
    memset(px, 0, (size_t)kAtlasW * kAtlasH * 4);
    for (int y = 0; y < kAtlasH; y++)
        for (int x = 0; x < kAtlasW; x++)
        {
            unsigned char* p = px + 4 * (y * kAtlasW + x);
            float a = 0.0f;
            unsigned char shade = 255, blue = 255;
            if (x < 64 && y < 32)
            {
                const float u = ((x & 31) + 0.5f) / 32.0f * 2.0f - 1.0f, v = (y + 0.5f) / 32.0f * 2.0f - 1.0f;
                const float r = sqrtf(u * u + v * v);
                if (x < 32)
                    a = expf(-r * r * 4.5f) * (r < 1.0f ? 1.0f : 0.0f);  // the soft dot
                else
                {
                    const float d = (r - 0.72f) / 0.13f;  // the ring
                    a = expf(-d * d) * (r < 1.0f ? 1.0f : 0.0f);
                }
            }
            else if (x >= 64)
            {
                a = snow_lumps(x - 64, y, 64, 64);
                shade = (unsigned char)(235 + 20 * a);
            }
            p[0] = shade;
            p[1] = shade;
            p[2] = blue;
            p[3] = (unsigned char)(a * 255.0f + 0.5f);
        }
    tex->Swizzle(true);
    tex->Prepare();
    return tex;
}

PlatTexture* s_atlas = NULL;
bool s_atlasTried = false;

bool atlas_ready()
{
    if (!s_atlasTried)
    {
        s_atlasTried = true;
        s_atlas = make_atlas();
        fprintf(stderr, "[weather] texture %s\n", s_atlas != NULL ? "ready" : "MISSING (no weather looks)");
        fflush(stderr);
    }
    return s_atlas != NULL;
}

// ------------------------------------------------------------------------------------------------ floodlight banks
// The stadium's floodlights: its own flare helpers (FlareHandler: the lamps' glows and halos), high ones gathered into up
// to 8 banks by their direction from the centre spot.
const int kMaxBanks = 8;
const int kMaxLamps = 32;
std::mutex s_lightLock;
float s_bank[kMaxBanks][3];
int s_bankCount = 0;
float s_lamp[kMaxLamps][3];
int s_lampCount = 0;
bool s_lightsFound = false;

void find_lights()
{
    float sum[kMaxBanks][4];
    memset(sum, 0, sizeof sum);
    int lamps = 0;
    float lamp[kMaxLamps][3];
    for (int list = 0; list < 2; list++)
    {
        const efList& l = list == 0 ? FlareHandler::instance.halos : FlareHandler::instance.glows;
        for (const efBaseNode* n = l.m_headNode; n != NULL; n = n->m_nextNode)
        {
            const FlareStruct* f = (const FlareStruct*)n;
            const nlVector3& p = f->worldPosition;
            if (p.z < 5.0f)
                continue;
            const float a = atan2f(p.y, p.x);
            int sector = (int)floorf((a + 3.14159265f) / 6.2831853f * kMaxBanks);
            sector = sector < 0 ? 0 : (sector >= kMaxBanks ? kMaxBanks - 1 : sector);
            const float w = 1.0f + 0.05f * p.z;
            sum[sector][0] += p.x * w;
            sum[sector][1] += p.y * w;
            sum[sector][2] += p.z * w;
            sum[sector][3] += w;
            if (lamps < kMaxLamps)
            {
                lamp[lamps][0] = p.x;
                lamp[lamps][1] = p.y;
                lamp[lamps][2] = p.z;
                lamps++;
            }
        }
    }
    std::lock_guard<std::mutex> lock(s_lightLock);
    s_bankCount = 0;
    for (int k = 0; k < kMaxBanks; k++)
        if (sum[k][3] > 0.0f)
        {
            s_bank[s_bankCount][0] = sum[k][0] / sum[k][3];
            s_bank[s_bankCount][1] = sum[k][1] / sum[k][3];
            s_bank[s_bankCount][2] = sum[k][2] / sum[k][3];
            s_bankCount++;
        }
    if (s_bankCount == 0)
    {
        // no flares of its own: four towers at the corners
        const float c[4][2] = {{-27.0f, -19.0f}, {27.0f, -19.0f}, {27.0f, 19.0f}, {-27.0f, 19.0f}};
        for (int k = 0; k < 4; k++)
        {
            s_bank[k][0] = c[k][0];
            s_bank[k][1] = c[k][1];
            s_bank[k][2] = 22.0f;
        }
        s_bankCount = 4;
    }
    memcpy(s_lamp, lamp, sizeof(float) * 3 * lamps);
    s_lampCount = lamps;
    s_lightsFound = true;
    fprintf(stderr, "[weather] floodlights: %d lamps high up -> %d banks\n", lamps, s_bankCount);
    fflush(stderr);
}

// Where a bank's light falls on the pitch.
void bank_target(const float* b, float* t)
{
    float x = b[0] * 0.5f, y = b[1] * 0.45f;
    x = x < -16.0f ? -16.0f : (x > 16.0f ? 16.0f : x);
    y = y < -8.5f ? -8.5f : (y > 8.5f ? 8.5f : y);
    t[0] = x;
    t[1] = y;
    t[2] = 0.0f;
}

// ------------------------------------------------------------------------------------------------ drawing helpers
struct V3
{
    float x, y, z;
};
V3 v3(float x, float y, float z)
{
    V3 r = {x, y, z};
    return r;
}

unsigned char byte(float f)
{
    return (unsigned char)(f <= 0.0f ? 0 : (f >= 1.0f ? 255 : (int)(f * 255.0f + 0.5f)));
}

// This frame's identity matrix handle of our own (glAllocMatrix): never one the Tabletop split has marked as stadium.
uintptr_t s_frameMatrix = 0;

// A batch of quads in one draw (one packet per send): the state is set first, then the vertices. Every batch uses the
// atlas; `twoD` = in a 2D (orthographic) view, positions in its pixels (top-left anchored), z -1.
struct Batch
{
    GLMeshWriter w;
    bool open;
    Batch(int quads, int blend, bool depthTest, bool twoD)
        : open(false)
    {
        if (quads <= 0 || s_atlas == NULL)
            return;
        glSetDefaultState(true);
        if (s_frameMatrix != 0 && !twoD)
            glSetCurrentMatrix(s_frameMatrix);
        glSetCurrentProgram(glGetProgram(twoD ? "2d unlit" : "3d unlit"));
        glSetRasterState(GLS_DepthWrite, 0);
        glSetRasterState(GLS_DepthTest, depthTest ? 1 : 0);
        glSetRasterState(GLS_Culling, 0);
        glSetRasterState(GLS_AlphaBlend, (unsigned long)blend);
        glSetRasterState(GLS_AlphaTest, 1);
        glSetCurrentRasterState(glHandleizeRasterState());
        glSetCurrentTexture((uintptr_t)s_atlas, GLTT_Diffuse);
        glSetTextureState(GLTS_DiffuseWrap, 3);  // clamped
        glSetCurrentTextureState(glHandleizeTextureState());
        const eGLStream streams[3] = {GLStream_Position, GLStream_Colour, GLStream_Diffuse};
        open = w.Begin(quads * 4, GLP_QuadList, 3, streams, false);
    }
    void vert(const V3& p, float u, float v, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
    {
        nlVector2 uv;
        uv.x = u;
        uv.y = v;
        w.Texcoord(uv);
        nlColour c;
        c.c[0] = r;
        c.c[1] = g;
        c.c[2] = b;
        c.c[3] = a;
        w.Colour(c);
        nlVector3 pos;
        pos.x = p.x;
        pos.y = p.y;
        pos.z = p.z;
        w.Vertex(pos);
    }
    // corners in order round the quad; the picture's rect (u0 v0 at corner 0, u1 at 1, v1 at 2)
    void quad(const V3* c, const Rect& t, unsigned char r, unsigned char g, unsigned char b, const unsigned char* a)
    {
        if (!open || w.currentIndex + 4 > w.maximumVerts)
            return;
        vert(c[0], t.u0, t.v0, r, g, b, a[0]);
        vert(c[1], t.u1, t.v0, r, g, b, a[1]);
        vert(c[2], t.u1, t.v1, r, g, b, a[2]);
        vert(c[3], t.u0, t.v1, r, g, b, a[3]);
    }
    void end(eGLView view)
    {
        if (!open)
            return;
        // GLMeshWriter only ends a mesh with every vertex it was begun with: quads left out (behind the camera ...)
        // become empty ones (no area, nothing drawn)
        const V3 zero = {0.0f, 0.0f, 0.0f};
        while (w.currentIndex < w.maximumVerts)
            vert(zero, 0.0f, 0.0f, 0, 0, 0, 0);
        if (w.End())
            glViewAttachModel(view, w.GetModel());
        open = false;
    }
};

// A full-frame 2D quad in a game 2D view: colours top / bottom, blend 4 = multiply (src * dst), 2 = add.
void screen_quad(eGLView view, int blend, const float* top, const float* bottom)
{
    glSetDefaultState(true);
    glSetRasterState(GLS_DepthTest, 0);
    glSetRasterState(GLS_DepthWrite, 0);
    glSetRasterState(GLS_AlphaBlend, (unsigned long)blend);
    glSetCurrentTexture(glGetTexture("global/white"), GLTT_Diffuse);
    glSetCurrentTextureState(glHandleizeTextureState());
    glSetCurrentRasterState(glHandleizeRasterState());
    glPoly2 poly;
    const float W = glGetOrthographicWidth(), H = glGetOrthographicHeight();
    poly.SetupRectangle(0.0f, 0.0f, W, H, -1.0f);
    for (int i = 0; i < 4; i++)
    {
        const float t = H > 0.0f ? poly.m_pos[i].y / H : 0.0f;  // 0 top .. 1 bottom (top-left anchored)
        const float k = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        poly.m_colour[i].c[0] = byte(top[0] + (bottom[0] - top[0]) * k);
        poly.m_colour[i].c[1] = byte(top[1] + (bottom[1] - top[1]) * k);
        poly.m_colour[i].c[2] = byte(top[2] + (bottom[2] - top[2]) * k);
        poly.m_colour[i].c[3] = 255;
    }
    poly.Attach(view, 0, NULL, (unsigned long)-1);
    glSetDefaultState(false);
}

// The game camera's projection for this frame's 2D overlay: camera space (looking down -Z, Y up) -> 2D view pixels.
struct Cam
{
    float tanX, tanY, W, H;
    bool ok;
};
Cam camera_proj()
{
    Cam c;
    nlMatrix4 p;
    glViewGetProjectionMatrix(GLV_Characters, p);
    // a perspective matrix's diagonal holds 1/tan(half fov) either way it is stored
    c.ok = fabsf(p.e2[0][0]) > 1e-4f && fabsf(p.e2[1][1]) > 1e-4f;
    c.tanX = c.ok ? 1.0f / fabsf(p.e2[0][0]) : 0.6f;
    c.tanY = c.ok ? 1.0f / fabsf(p.e2[1][1]) : 0.45f;
    c.W = glGetOrthographicWidth();
    c.H = glGetOrthographicHeight();
    return c;
}
// false behind the camera
bool to_screen(const Cam& c, const V3& p, float* sx, float* sy)
{
    if (p.z > -0.05f)
        return false;
    const float d = -p.z;
    *sx = (0.5f + 0.5f * p.x / (d * c.tanX)) * c.W;
    *sy = (0.5f - 0.5f * p.y / (d * c.tanY)) * c.H;
    return true;
}
float px_per_m(const Cam& c, float depth) { return 0.5f * c.W / (depth * c.tanX); }

V3 to_camera(const nlMatrix4& m, float x, float y, float z)
{
    return v3(x * m.e2[0][0] + y * m.e2[1][0] + z * m.e2[2][0] + m.e2[3][0],
              x * m.e2[0][1] + y * m.e2[1][1] + z * m.e2[2][1] + m.e2[3][1],
              x * m.e2[0][2] + y * m.e2[1][2] + z * m.e2[2][2] + m.e2[3][2]);
}

// ------------------------------------------------------------------------------------------------ the camera's rain
// Falling in front of the game camera (simulated in its own space), so the broadcast picture shows the weather the way TV
// does: streaks / flakes between the lens and the play. Drawn as one batch of 2D quads in GLV_UnsortedOrtho (after the
// world and the grade, before the HUD), never in the headset's eye passes.
struct Drop
{
    float p[3];
    float speed, size, phase;
};
const int kCamDrops = 260;
Drop s_cam[kCamDrops];
int s_camKind = -1;  // which weather the drops were made for

void cam_spawn(Drop& d, bool rain, bool anywhere, const Cam& c)
{
    const float depth = 1.6f + 16.0f * powf(roll01(), 1.6f);
    const float u = (roll01() * 2.0f - 1.0f) * 1.15f;
    const float v = anywhere ? (roll01() * 2.0f - 1.0f) * 1.1f : 1.12f;
    d.p[0] = u * depth * c.tanX;
    d.p[1] = v * depth * c.tanY;
    d.p[2] = -depth;
    d.speed = rain ? 9.0f + 3.0f * roll01() : 0.9f + 0.6f * roll01();
    d.size = rain ? 0.0045f + 0.003f * roll01() : 0.018f + 0.022f * roll01();
    d.phase = roll01() * 6.2831853f;
}

void camera_weather(int bits, float fade, float dt, float load, const nlMatrix4& view, const Cam& c)
{
    const bool rain = (bits & PORT_WX_RAIN) != 0;
    const bool snow = (bits & PORT_WX_SNOW) != 0;
    if (!rain && !snow)
        return;
    const int kind = rain ? 1 : 2;
    int n = (int)((rain ? 220.0f : 260.0f) * load);
    n = n < 60 ? 60 : (n > kCamDrops ? kCamDrops : n);
    if (s_camKind != kind)
    {
        s_camKind = kind;
        for (int i = 0; i < kCamDrops; i++)
            cam_spawn(s_cam[i], rain, true, c);
    }
    // the world's down in the camera's space (row-vector view: the world Z axis is row 2)
    float down[3] = {-view.e2[2][0], -view.e2[2][1], -view.e2[2][2]};
    const float dl = sqrtf(down[0] * down[0] + down[1] * down[1] + down[2] * down[2]);
    if (dl < 1e-4f)
    {
        down[0] = 0.0f;
        down[1] = -1.0f;
        down[2] = 0.0f;
    }
    else
        for (int k = 0; k < 3; k++)
            down[k] /= dl;
    const float wind = rain ? 0.9f : 0.35f;
    const bool night = (bits & PORT_WX_NIGHT) != 0;
    Batch b(n, 1, false, true);
    if (!b.open)
        return;
    static float s_t = 0.0f;
    s_t += dt;
    unsigned char al[4];
    for (int i = 0; i < n; i++)
    {
        Drop& d = s_cam[i];
        const float sway = snow ? 0.35f * sinf(s_t * 1.3f + d.phase) : 0.0f;
        d.p[0] += (down[0] * d.speed + wind + sway) * dt;
        d.p[1] += (down[1] * d.speed) * dt;
        d.p[2] += (down[2] * d.speed) * dt;
        const float depth = -d.p[2];
        if (depth < 1.2f || depth > 19.0f || fabsf(d.p[0]) > depth * c.tanX * 1.25f || d.p[1] < -depth * c.tanY * 1.2f ||
            d.p[1] > depth * c.tanY * 1.3f)
            cam_spawn(d, rain, false, c);
        const float a = fade * (rain ? (night ? 0.42f : 0.32f) : 0.85f) *
                        (depth < 3.0f ? 0.6f + 0.4f * (depth - 1.2f) / 1.8f : 1.0f);
        float hx, hy;
        if (!to_screen(c, v3(d.p[0], d.p[1], d.p[2]), &hx, &hy))
            continue;
        if (rain)
        {
            // a streak: the drop's path over a 1/30 s shutter, the soft dot stretched along it
            const float len = d.speed * 0.033f;
            float tx, ty;
            if (!to_screen(c, v3(d.p[0] - down[0] * len, d.p[1] - down[1] * len, d.p[2] - down[2] * len), &tx, &ty))
                continue;
            float ax = ty - hy, ay = -(tx - hx);
            const float al2 = sqrtf(ax * ax + ay * ay);
            if (al2 < 1e-3f)
                continue;
            const float half = d.size * px_per_m(c, depth);
            const float wpx = half < 0.6f ? 0.6f : half;
            ax = ax / al2 * wpx;
            ay = ay / al2 * wpx;
            V3 q[4] = {v3(tx - ax, ty - ay, -1.0f), v3(tx + ax, ty + ay, -1.0f), v3(hx + ax, hy + ay, -1.0f),
                       v3(hx - ax, hy - ay, -1.0f)};
            const Rect r = {kDot.u0, kDot.v0 + 0.15f * (kDot.v1 - kDot.v0), kDot.u1, kDot.v1 - 0.15f * (kDot.v1 - kDot.v0)};
            al[0] = al[1] = byte(a * 0.35f);
            al[2] = al[3] = byte(a);
            b.quad(q, r, night ? 230 : 205, night ? 236 : 216, 255, al);
        }
        else
        {
            const float s = d.size * px_per_m(c, depth);
            const float r = s < 0.8f ? 0.8f : s;
            V3 q[4] = {v3(hx - r, hy - r, -1.0f), v3(hx + r, hy - r, -1.0f), v3(hx + r, hy + r, -1.0f), v3(hx - r, hy + r, -1.0f)};
            al[0] = al[1] = al[2] = al[3] = byte(a);
            b.quad(q, kDot, 250, 252, 255, al);
        }
    }
    b.end(GLV_UnsortedOrtho);
}

// ------------------------------------------------------------------------------------------------ night: the lights
// The floodlights on, for the broadcast picture: each bank a big glow and a beam down to the pitch, each lamp its own
// small glow - 2D quads (additive) after the world was darkened, never in the eye sends (the effects layer draws the
// headset's own).
void camera_lights(float fade, const nlMatrix4& view, const Cam& c, bool wet)
{
    float bank[kMaxBanks][3];
    float lamp[kMaxLamps][3];
    int nb, nl;
    {
        std::lock_guard<std::mutex> lock(s_lightLock);
        nb = s_bankCount;
        nl = s_lampCount;
        memcpy(bank, s_bank, sizeof(float) * 3 * nb);
        memcpy(lamp, s_lamp, sizeof(float) * 3 * nl);
    }
    Batch b(nb * 3 + nl, 3, false, true);
    if (!b.open)
        return;
    unsigned char al[4];
    for (int i = 0; i < nb; i++)
    {
        const V3 L = to_camera(view, bank[i][0], bank[i][1], bank[i][2]);
        float t[3];
        bank_target(bank[i], t);
        const V3 T = to_camera(view, t[0], t[1], t[2]);
        float lx, ly, px, py;
        const bool lOn = to_screen(c, L, &lx, &ly);
        if (lOn)
        {
            // the glow (a halo round it, and the hot middle)
            for (int k = 0; k < 2; k++)
            {
                const float s = (k == 0 ? 9.0f : 3.2f) * px_per_m(c, -L.z);
                V3 q[4] = {v3(lx - s, ly - s, -1.0f), v3(lx + s, ly - s, -1.0f), v3(lx + s, ly + s, -1.0f), v3(lx - s, ly + s, -1.0f)};
                al[0] = al[1] = al[2] = al[3] = byte(fade * (k == 0 ? 0.30f : 0.85f));
                b.quad(q, kDot, 255, k == 0 ? 238 : 250, k == 0 ? 205 : 235, al);
            }
        }
        // the beam: narrow at the lamp, wide on the grass, fading as it falls (brighter in rain / snow: it catches)
        if (lOn && to_screen(c, T, &px, &py))
        {
            float ax = py - ly, ay = -(px - lx);
            const float l2 = sqrtf(ax * ax + ay * ay);
            if (l2 > 1.0f)
            {
                ax /= l2;
                ay /= l2;
                const float w0 = 1.4f * px_per_m(c, -L.z), w1 = 6.5f * px_per_m(c, -T.z);
                V3 q[4] = {v3(lx - ax * w0, ly - ay * w0, -1.0f), v3(lx + ax * w0, ly + ay * w0, -1.0f),
                           v3(px + ax * w1, py + ay * w1, -1.0f), v3(px - ax * w1, py - ay * w1, -1.0f)};
                // across the beam the dot's middle row: soft edges, even along it
                const float vm = 0.5f * (kDot.v0 + kDot.v1);
                const Rect r = {kDot.u0, vm, kDot.u1, vm};
                al[0] = al[1] = byte(fade * (wet ? 0.30f : 0.20f));
                al[2] = al[3] = 0;
                b.quad(q, r, 255, 244, 220, al);
            }
        }
    }
    for (int i = 0; i < nl; i++)
    {
        const V3 L = to_camera(view, lamp[i][0], lamp[i][1], lamp[i][2]);
        float lx, ly;
        if (!to_screen(c, L, &lx, &ly))
            continue;
        const float s = 1.6f * px_per_m(c, -L.z);
        V3 q[4] = {v3(lx - s, ly - s, -1.0f), v3(lx + s, ly - s, -1.0f), v3(lx + s, ly + s, -1.0f), v3(lx - s, ly + s, -1.0f)};
        al[0] = al[1] = al[2] = al[3] = byte(fade * 0.6f);
        b.quad(q, kDot, 255, 248, 230, al);
    }
    b.end(GLV_UnsortedOrtho);
}

// ------------------------------------------------------------------------------------------------ on the pitch
// Game world: metres, Z up, the centre spot at the origin, goals along X (half-length 20.6), touchlines at Y = +-12.08.
// One batch in GLV_UnsortedPerspective: on the broadcast picture (after the night's darkening, so the snow is darkened
// by hand at night) and on the Tabletop field; the life-size stadium pass leaves that view out.
const float kHalfX = 20.6f, kHalfY = 12.08f;
const int kSplashes = 44;
struct Splash
{
    float x, y, age, life;
};
Splash s_splash[kSplashes];
bool s_splashInit = false;

void flat_quad(Batch& b, float x, float y, float z, float rx, float ry, const Rect& t, unsigned char r, unsigned char g,
               unsigned char bl, float alpha)
{
    V3 c[4] = {v3(x - rx, y - ry, z), v3(x + rx, y - ry, z), v3(x + rx, y + ry, z), v3(x - rx, y + ry, z)};
    unsigned char al[4];
    al[0] = al[1] = al[2] = al[3] = byte(alpha);
    b.quad(c, t, r, g, bl, al);
}

void pitch_dressing(int bits, float fade, float dt, float cover, float load)
{
    const bool rain = (bits & PORT_WX_RAIN) != 0, snow = (bits & PORT_WX_SNOW) != 0, night = (bits & PORT_WX_NIGHT) != 0;
    float bank[kMaxBanks][3];
    int nb = 0;
    if (night)
    {
        std::lock_guard<std::mutex> lock(s_lightLock);
        nb = s_bankCount;
        memcpy(bank, s_bank, sizeof(float) * 3 * nb);
    }
    int splashes = rain ? (int)(kSplashes * load) : 0;
    splashes = rain && splashes < 16 ? 16 : splashes;
    const int kAlong = 8, kEnds = 4;
    const int drifts = snow ? 2 * kAlong + 2 * kEnds + 1 : 0;
    const int pools = night ? nb + 1 : 0;
    const int sheen = rain ? 1 : 0;
    Batch b(splashes + drifts + pools + sheen, 1, true, false);
    if (!b.open)
        return;
    // the floodlit pools and the wet sheen first (under the rest): warm light laid over the grass
    for (int i = 0; i < pools - 1; i++)
    {
        float t[3];
        bank_target(bank[i], t);
        flat_quad(b, t[0], t[1], 0.045f, 11.0f, 9.0f, kDot, 255, 238, 205, fade * (rain ? 0.22f : 0.17f));
    }
    if (night)
        flat_quad(b, 0.0f, 0.0f, 0.045f, 19.0f, 12.0f, kDot, 255, 244, 225, fade * 0.10f);
    if (rain)
        flat_quad(b, 0.0f, 3.0f, 0.04f, 26.0f, 15.0f, kDot, 200, 215, 235, fade * (night ? 0.05f : 0.07f));
    // snow: drifts in from the boards (thick there, nothing toward the middle), a light frost over the grass
    if (snow)
    {
        const float a = fade * (0.25f + 0.65f * cover);
        const unsigned char sh = night ? 150 : 255;  // at night the snow lies in the dark like the rest
        const float depth = 1.1f + 1.2f * cover;
        const float du = 0.25f * (kSnow.u1 - kSnow.u0), dv = 0.3f * (kSnow.v1 - kSnow.v0);
        unsigned char al[4];
        for (int side = -1; side <= 1; side += 2)
        {
            for (int k = 0; k < kAlong; k++)
            {
                const float x0 = -kHalfX + 2.0f * kHalfX * k / kAlong, x1 = -kHalfX + 2.0f * kHalfX * (k + 1) / kAlong;
                const float yOut = side * kHalfY, yIn = side * (kHalfY - depth);
                V3 c[4] = {v3(x0, yOut, 0.03f), v3(x1, yOut, 0.03f), v3(x1, yIn, 0.03f), v3(x0, yIn, 0.03f)};
                const float o = (float)((k * 5 + (side > 0 ? 3 : 0)) % 4) / 4.0f;
                const Rect r = {kSnow.u0 + o * (kSnow.u1 - kSnow.u0 - du) , kSnow.v0 + o * 0.5f * (kSnow.v1 - kSnow.v0),
                                kSnow.u0 + o * (kSnow.u1 - kSnow.u0 - du) + du, kSnow.v0 + o * 0.5f * (kSnow.v1 - kSnow.v0) + dv};
                al[0] = al[1] = byte(a);
                al[2] = al[3] = 0;
                b.quad(c, r, sh, sh, (unsigned char)(night ? 175 : 255), al);
            }
            for (int k = 0; k < kEnds; k++)
            {
                const float y0 = -kHalfY + 2.0f * kHalfY * k / kEnds, y1 = -kHalfY + 2.0f * kHalfY * (k + 1) / kEnds;
                const float xOut = side * kHalfX, xIn = side * (kHalfX - depth);
                V3 c[4] = {v3(xOut, y0, 0.03f), v3(xOut, y1, 0.03f), v3(xIn, y1, 0.03f), v3(xIn, y0, 0.03f)};
                const float o = (float)((k * 3 + (side > 0 ? 1 : 2)) % 4) / 4.0f;
                const Rect r = {kSnow.u0 + o * (kSnow.u1 - kSnow.u0 - du), kSnow.v0 + o * 0.5f * (kSnow.v1 - kSnow.v0),
                                kSnow.u0 + o * (kSnow.u1 - kSnow.u0 - du) + du, kSnow.v0 + o * 0.5f * (kSnow.v1 - kSnow.v0) + dv};
                al[0] = al[1] = byte(a);
                al[2] = al[3] = 0;
                b.quad(c, r, sh, sh, (unsigned char)(night ? 175 : 255), al);
            }
        }
        flat_quad(b, 0.0f, 0.0f, 0.028f, kHalfX, kHalfY, kSnow, sh, sh, (unsigned char)(night ? 175 : 255),
                  fade * (0.10f + 0.16f * cover));
    }
    // rain: rings on the grass
    if (rain)
    {
        if (!s_splashInit)
        {
            s_splashInit = true;
            for (int i = 0; i < kSplashes; i++)
            {
                s_splash[i].life = 0.28f + 0.12f * roll01();
                s_splash[i].age = s_splash[i].life * roll01();
                s_splash[i].x = (roll01() * 2.0f - 1.0f) * kHalfX;
                s_splash[i].y = (roll01() * 2.0f - 1.0f) * kHalfY;
            }
        }
        for (int i = 0; i < splashes && i < kSplashes; i++)
        {
            Splash& s = s_splash[i];
            s.age += dt;
            if (s.age >= s.life)
            {
                s.age = 0.0f;
                s.x = (roll01() * 2.0f - 1.0f) * kHalfX;
                s.y = (roll01() * 2.0f - 1.0f) * kHalfY;
            }
            const float t = s.age / s.life;
            const float r = 0.08f + 0.42f * t;
            flat_quad(b, s.x, s.y, 0.035f, r, r, kRing, night ? 180 : 215, night ? 188 : 228, night ? 210 : 255,
                      fade * 0.55f * (1.0f - t));
        }
    }
    b.end(GLV_UnsortedPerspective);
}

// ------------------------------------------------------------------------------------------------ the grade
// The broadcast picture's grade, as few full-picture passes as the weather needs: night darkens the stadium (sky, stands,
// pitch: drawn before the players) in GLV_BigBlackPolygon; rain multiplies the whole picture cooler (players too, never
// the HUD) in GLV_UnsortedOrtho; rain and snow add a haze there. top = the picture's top (the sky, the far stands),
// bottom = the near pitch.
struct Grade
{
    bool stad, all, haze;
    float stadTop[3], stadBot[3], allTop[3], allBot[3], hazeTop[3], hazeBot[3];
};

void set3(float* a, float r, float g, float b)
{
    a[0] = r;
    a[1] = g;
    a[2] = b;
}

Grade grade_for(int bits, float fade)
{
    Grade g;
    const bool night = (bits & PORT_WX_NIGHT) != 0, rain = (bits & PORT_WX_RAIN) != 0, snow = (bits & PORT_WX_SNOW) != 0;
    g.stad = night;
    g.all = rain;
    g.haze = rain || snow;
    set3(g.stadTop, 0.22f, 0.25f, 0.38f);  // the sky and the far stands dark
    set3(g.stadBot, 0.56f, 0.59f, 0.72f);  // the pitch in front still readable (+ the floodlit pools)
    set3(g.allTop, 0.76f, 0.80f, 0.89f);
    set3(g.allBot, 0.84f, 0.87f, 0.94f);
    set3(g.hazeTop, 0.0f, 0.0f, 0.0f);
    set3(g.hazeBot, 0.0f, 0.0f, 0.0f);
    if (rain)
    {
        const float h = night ? 0.35f : 1.0f;
        set3(g.hazeTop, 0.050f * h, 0.056f * h, 0.068f * h);
        set3(g.hazeBot, 0.012f * h, 0.014f * h, 0.018f * h);
    }
    if (snow)
    {
        const float h = night ? 0.4f : 1.0f;
        set3(g.hazeTop, g.hazeTop[0] + 0.13f * h, g.hazeTop[1] + 0.135f * h, g.hazeTop[2] + 0.15f * h);
        set3(g.hazeBot, g.hazeBot[0] + 0.05f * h, g.hazeBot[1] + 0.052f * h, g.hazeBot[2] + 0.06f * h);
    }
    for (int k = 0; k < 3; k++)
    {
        g.stadTop[k] = 1.0f + (g.stadTop[k] - 1.0f) * fade;
        g.stadBot[k] = 1.0f + (g.stadBot[k] - 1.0f) * fade;
        g.allTop[k] = 1.0f + (g.allTop[k] - 1.0f) * fade;
        g.allBot[k] = 1.0f + (g.allBot[k] - 1.0f) * fade;
        g.hazeTop[k] *= fade;
        g.hazeBot[k] *= fade;
    }
    return g;
}

void mul3(float* a, float r, float g, float b)
{
    a[0] *= r;
    a[1] *= g;
    a[2] *= b;
}
void add3(float* a, float r, float g, float b)
{
    a[0] += r;
    a[1] += g;
    a[2] += b;
}

}  // namespace

// ================================================================================================ the C API
extern "C" int PortWeatherMode(void)
{
    int m = s_mode.load();
    if (m < 0)
    {
        // strikers.ini `weather = random | clear | rain | snow | night | off` (STRIKERS_WEATHER); the pause row's
        // choice (xr_room_options.txt) replaces it once made
        m = mode_from_text(getenv("STRIKERS_WEATHER"));
        int expected = -1;
        if (!s_mode.compare_exchange_strong(expected, m))
            m = s_mode.load();
    }
    return m;
}

extern "C" const char* PortWeatherModeName(int mode) { return mode >= 0 && mode < PORT_WX_MODES ? kModeNames[mode] : "RANDOM"; }

extern "C" void PortWeatherSetMode(int mode)
{
    if (mode < 0 || mode >= PORT_WX_MODES)
        mode = PORT_WX_MODE_RANDOM;
    const int was = PortWeatherMode();
    s_mode.store(mode);
    if (was == mode || s_stadium < 0)
        return;
    // the match on now follows at once: a franchise match keeps its calendar (unless OFF), a quick one takes the new
    // pick (RANDOM keeps a random pick it already had)
    if (mode == PORT_WX_MODE_OFF)
        apply(0, "setting: OFF");
    else if (s_franchiseBits >= 0)
        apply(s_franchiseBits, "setting changed: franchise calendar");
    else if (mode == PORT_WX_MODE_RANDOM && s_quickRandom)
        return;
    else
    {
        s_quickRandom = mode == PORT_WX_MODE_RANDOM;
        apply(wxcal::filter(quick_pick(mode), stadium_climate(s_stadium)), "setting changed");
    }
}

extern "C" void PortWeatherNewMatch(int stadium)
{
    s_stadium = stadium;
    s_fade.store(0.0f);
    s_cover.store(0.0f);
    s_camKind = -1;
    s_splashInit = false;
    {
        std::lock_guard<std::mutex> lock(s_lightLock);
        s_lightsFound = false;
        s_bankCount = 0;
        s_lampCount = 0;
    }
    const int mode = PortWeatherMode();
    s_franchiseBits = PortFranchiseMatchWeather();
    s_quickRandom = false;
    int bits;
    const char* why;
    if (mode == PORT_WX_MODE_OFF)
    {
        bits = 0;
        why = "weather OFF";
    }
    else if (s_franchiseBits >= 0)
    {
        bits = s_franchiseBits;
        why = "franchise calendar";
    }
    else
    {
        s_quickRandom = mode == PORT_WX_MODE_RANDOM;
        bits = wxcal::filter(quick_pick(mode), stadium_climate(stadium));
        why = mode == PORT_WX_MODE_RANDOM ? "quick match, random" : "quick match, set";
    }
    s_bits.store(-1);  // so apply() logs and fades in afresh
    apply(bits, why);
}

extern "C" int PortWeatherCurrent(void)
{
    const int b = s_bits.load();
    return b < 0 ? 0 : b;
}
extern "C" float PortWeatherFade(void) { return s_fade.load(); }
extern "C" float PortWeatherSnowCover(void) { return s_cover.load(); }
extern "C" void PortWeatherSetLoad(float load)
{
    s_load.store(load < 0.25f ? 0.25f : (load > 1.0f ? 1.0f : load));
}

extern "C" const char* PortWeatherLabel(int bits)
{
    const bool night = (bits & PORT_WX_NIGHT) != 0;
    if (bits & PORT_WX_SNOW)
        return night ? "SNOW UNDER THE LIGHTS" : "SNOW GAME";
    if (bits & PORT_WX_RAIN)
        return night ? "RAIN UNDER THE LIGHTS" : "RAIN";
    return night ? "UNDER THE LIGHTS" : "";
}

extern "C" void PortWeatherRenderGame(float dt)
{
    const int bits = PortWeatherCurrent();
    if (bits == 0)
        return;
    if (dt < 0.0f || dt > 0.1f)
        dt = dt < 0.0f ? 0.0f : 0.1f;
    float fade = s_fade.load();
    if (fade < 1.0f)
    {
        fade = fade + dt / 1.5f > 1.0f ? 1.0f : fade + dt / 1.5f;
        s_fade.store(fade);
    }
    if (bits & PORT_WX_SNOW)
    {
        const float c = s_cover.load();
        if (c < 1.0f)
            s_cover.store(c + dt / 150.0f > 1.0f ? 1.0f : c + dt / 150.0f);
    }
    if (!atlas_ready())
        return;
    if (!s_lightsFound && (bits & PORT_WX_NIGHT) != 0 &&
        (FlareHandler::instance.halos.m_headNode != NULL || FlareHandler::instance.glows.m_headNode != NULL || fade >= 1.0f))
        find_lights();
    const float cover = s_cover.load();
    const float load = s_load.load();
    nlMatrix4 view;
    glViewGetViewMatrix(GLV_Unshadowed, view);
    // our 3D quads go with an identity matrix of their own: never one the Tabletop split marks as stadium
    const uintptr_t prev = glSetCurrentMatrix(glGetIdentityMatrix());
    glSetCurrentMatrix(prev);
    const uintptr_t h = glAllocMatrix();
    s_frameMatrix = 0;
    if (h != 0xFFFFFFFF)
    {
        nlMatrix4 identity;
        identity.SetIdentity();
        glSetMatrix(h, identity);
        s_frameMatrix = h;
    }
    // on the pitch: the broadcast picture and the Tabletop field (one draw)
    pitch_dressing(bits, fade, dt, cover, load);
    // the broadcast picture only, and not while it is the Tabletop's HUD-only scoreboard
    if (PortXrHudOnlyNormalView() == 0)
    {
        const Grade g = grade_for(bits, fade);
        if (g.stad)
            screen_quad(GLV_BigBlackPolygon, 4, g.stadTop, g.stadBot);  // before the players
        if (g.all)
            screen_quad(GLV_UnsortedOrtho, 4, g.allTop, g.allBot);  // after them, before the HUD
        if (g.haze)
            screen_quad(GLV_UnsortedOrtho, 2, g.hazeTop, g.hazeBot);
        const Cam c = camera_proj();
        if (bits & PORT_WX_NIGHT)
            camera_lights(fade, view, c, (bits & (PORT_WX_RAIN | PORT_WX_SNOW)) != 0);
        camera_weather(bits, fade, dt, load, view, c);
    }
    glSetDefaultState(false);
    glSetCurrentMatrix(prev);
    s_frameMatrix = 0;
}

extern "C" int PortWeatherLayerGrade(int kind, float scale[4], float bias[4])
{
    const int bits = PortWeatherCurrent();
    const float fade = s_fade.load();
    if (bits == 0 || fade <= 0.0f)
        return 0;
    float s[3] = {1.0f, 1.0f, 1.0f}, b[3] = {0.0f, 0.0f, 0.0f};
    const bool night = (bits & PORT_WX_NIGHT) != 0;
    const bool stadium = kind == 2;
    if (bits & PORT_WX_RAIN)
    {
        if (stadium)
        {
            mul3(s, 0.70f, 0.74f, 0.80f);
            add3(b, 0.035f, 0.040f, 0.050f);
        }
        else
            mul3(s, 0.86f, 0.89f, 0.96f);
    }
    if (bits & PORT_WX_SNOW)
    {
        if (stadium)
        {
            mul3(s, 0.82f, 0.85f, 0.90f);
            add3(b, 0.10f, 0.11f, 0.13f);
        }
        else
        {
            mul3(s, 0.97f, 0.98f, 1.0f);
            if (kind == 1)
                add3(b, 0.03f, 0.032f, 0.036f);
        }
    }
    if (night)
    {
        if (stadium)
        {
            mul3(s, 0.40f, 0.44f, 0.62f);
            for (int k = 0; k < 3; k++)
                b[k] *= 0.4f;
        }
        else
            mul3(s, 0.72f, 0.75f, 0.88f);  // the players stay readable
    }
    if (kind == 0)
        b[0] = b[1] = b[2] = 0.0f;  // over your room: a bias would tint the room itself
    for (int k = 0; k < 3; k++)
    {
        scale[k] = 1.0f + (s[k] - 1.0f) * fade;
        bias[k] = b[k] * fade;
    }
    scale[3] = 1.0f;
    bias[3] = 0.0f;
    return 1;
}

extern "C" int PortWeatherLights(float* xyz, int max)
{
    std::lock_guard<std::mutex> lock(s_lightLock);
    const int n = s_bankCount < max ? s_bankCount : max;
    memcpy(xyz, s_bank, sizeof(float) * 3 * n);
    return n;
}
