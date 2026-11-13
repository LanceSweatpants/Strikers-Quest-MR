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
#include "xr_snow.h"  // r214: the snow's pure part (the pitch's grid, the snow texture, the stages)

#include "NL/nlMemory.h"
#include "NL/gl/glDraw2.h"
#include "NL/gl/glMatrix.h"
#include "NL/gl/glState.h"
#include "NL/gl/glView.h"
#include "NL/glx/glxTexture.h"
#include "dolphin/gx/GXTexture.h"  // r214: the snow texture's GX object (RGBA8 with mips, as ball_skin.cpp)
#include "Game/GL/gluMeshWriter.h"
#include "Game/Render/FlareHandler.h"
#include "port/xr_layers.h"
// r196: read-only looks at where the players / the ball are (the snow trampled there) - nothing is written
#include "Game/Ball.h"
#include "Game/Character.h"
#include "Game/CharacterTemplate.h"
#include "Game/Game.h"
#include "Game/Team.h"
#include "Game/Sys/audio.h"
#include "NL/platpad.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

extern "C" int PortXrHudOnlyNormalView(void);
extern "C" void* port_region_reserve(size_t size, size_t align);  // memalloc.cpp (glx_GetTex's own region; r214)
extern "C" int PortNetOnline(void);                                // xr_net.cpp
extern "C" void PortXrPulseBuzz(float amplitude, float seconds);  // xr_quest.cpp (a no-op off the headset)
namespace Audio
{
extern bool gbGameIsPaused;  // audio.cpp
}

namespace
{

// ------------------------------------------------------------------------------------------------ the setting + pick
const char* const kModeNames[PORT_WX_MODES] = {"RANDOM", "CLEAR", "RAIN", "SNOW", "NIGHT", "OFF",
                                                "DRIZZLE", "THUNDERSTORM", "FLURRY", "BLIZZARD"};

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

// r196: the match's dice - the local roll offline, the shared seed's online (both headsets roll the same)
struct Dice
{
    bool shared;
    wxcal::Rng r;
    uint32_t next() { return shared ? r.next() : roll(); }
};

// A quick match's weather for a setting.
int quick_pick(int mode, Dice& d)
{
    switch (mode)
    {
    case PORT_WX_MODE_RAIN:
    case PORT_WX_MODE_DRIZZLE:
    case PORT_WX_MODE_STORM:
        return PORT_WX_RAIN;
    case PORT_WX_MODE_SNOW:
    case PORT_WX_MODE_FLURRY:
    case PORT_WX_MODE_BLIZZARD:
        return PORT_WX_SNOW;
    case PORT_WX_MODE_NIGHT:
        return PORT_WX_NIGHT;
    case PORT_WX_MODE_RANDOM:
    {
        const int r = (int)(d.next() % 100u);
        int bits = 0;
        if (r < 60)
            bits = 0;
        else if (r < 75)
            bits = PORT_WX_RAIN;
        else if (r < 85)
            bits = PORT_WX_SNOW;
        else
            bits = PORT_WX_NIGHT;
        if ((bits & (PORT_WX_RAIN | PORT_WX_SNOW)) != 0 && d.next() % 100u < 30u)
            bits |= PORT_WX_NIGHT;
        return bits;
    }
    default:
        return 0;
    }
}
int quick_pick(int mode)
{
    Dice d = {false, {0}};
    return quick_pick(mode, d);
}

// r196: the tier for a match's bits (a tier setting forces its own; else the odds of wxcal::rain_tier / snow_tier -
// r198: rain DRIZZLE 35 / RAIN 45 / THUNDERSTORM 20, snow FLURRY 65 / BLIZZARD 35, was 30/40/30 and 40/60)
int pick_tier(int bits, int mode, Dice& d)
{
    const uint32_t r = d.next();
    if (bits & PORT_WX_RAIN)
    {
        if (mode == PORT_WX_MODE_DRIZZLE)
            return PORT_WX_TIER_DRIZZLE;
        if (mode == PORT_WX_MODE_STORM)
            return PORT_WX_TIER_STORM;
        static const int kRainTiers[3] = {PORT_WX_TIER_DRIZZLE, PORT_WX_TIER_RAIN, PORT_WX_TIER_STORM};
        return kRainTiers[wxcal::rain_tier(r)];
    }
    if (bits & PORT_WX_SNOW)
    {
        if (mode == PORT_WX_MODE_FLURRY)
            return PORT_WX_TIER_FLURRY;
        if (mode == PORT_WX_MODE_BLIZZARD)
            return PORT_WX_TIER_BLIZZARD;
        return wxcal::snow_tier(r) == 0 ? PORT_WX_TIER_FLURRY : PORT_WX_TIER_BLIZZARD;
    }
    return PORT_WX_TIER_NONE;
}

const char* const kTierNames[6] = {"none", "drizzle", "rain", "THUNDERSTORM", "flurry", "BLIZZARD"};

// ------------------------------------------------------------------------------------------------ r196: the tiers
// Everything here is the match's looks / sound: a clock of its own (the game's frame time, 0 while paused), the wind,
// the lightning, the snow lying on the pitch. Written on the game thread (PortWeatherRenderGame), read by the headset
// side through atomics / a lock. Online both headsets have the same seed, so the same wind and strikes at the same
// match time (lockstep: the same game frames).
std::atomic<int> s_tier(0);
std::atomic<uint32_t> s_seed(1u);
std::atomic<float> s_inten(0.0f);
std::atomic<float> s_windX(0.0f), s_windY(0.0f), s_gust(0.0f);
std::atomic<float> s_flash(0.0f);
std::atomic<long long> s_renderNs(0);  // the last PortWeatherRenderGame (the sound / flash stop when it stops)
float s_clock = 0.0f;
std::atomic<bool> s_sharedOn(false);
std::atomic<uint32_t> s_sharedSeed(0u);
std::atomic<int> s_sharedMode(0);

long long mono_now_ns()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

float h01(uint32_t a, uint32_t b, uint32_t c) { return (wxcal::hash3(a, b, c) & 0xFFFFFFu) / 16777215.0f; }
float smooth01(float u) { return u <= 0.0f ? 0.0f : (u >= 1.0f ? 1.0f : u * u * (3.0f - 2.0f * u)); }
float cosmix(float a, float b, float u) { return a + (b - a) * (0.5f - 0.5f * cosf(3.14159265f * (u < 0 ? 0 : (u > 1 ? 1 : u)))); }

// The blizzard: a flurry for its first 20 s, building to the full blizzard by 80 s (the preview's ramp, stretched to a
// match), then its strength rides the gusts.
float tier_intensity(int tier, float t, float gust)
{
    switch (tier)
    {
    case PORT_WX_TIER_DRIZZLE:
        return 0.35f;
    case PORT_WX_TIER_RAIN:
        return 0.7f;
    case PORT_WX_TIER_STORM:
        return 0.92f + 0.08f * gust;
    case PORT_WX_TIER_FLURRY:
        return 0.14f;
    case PORT_WX_TIER_BLIZZARD:
    {
        const float ramp = cosmix(0.14f, 1.0f, (t - 20.0f) / 60.0f);
        return t > 80.0f ? 0.85f + 0.15f * gust : ramp;
    }
    default:
        return 0.0f;
    }
}

// The wind for the tier at match time t (wx_common.py storm_wind / bliz_wind, the blizzard's keys made from the seed:
// every 2.4 s a new target, mostly one way with swings the other), in the match's wind frame, then turned by the
// match's wind direction. Game world m/s.
void tier_wind(int tier, uint32_t seed, float t, float inten, float* vx, float* vy)
{
    float a = 0.0f, b = 0.0f;  // along / across the wind's own direction
    switch (tier)
    {
    case PORT_WX_TIER_DRIZZLE:
        a = 0.35f + 0.15f * sinf(t * 0.4f);
        b = 0.1f;
        break;
    case PORT_WX_TIER_RAIN:
        a = 1.1f + 0.4f * sinf(t * 0.55f) + 0.2f * sinf(t * 1.7f + 1.0f);
        b = 0.3f;
        break;
    case PORT_WX_TIER_STORM:
        a = 2.6f + 0.9f * sinf(t * 0.55f) + 0.5f * sinf(t * 1.7f + 1.0f) + 0.25f * sinf(t * 4.1f);
        b = 0.6f + 0.3f * sinf(t * 0.8f + 2.0f);
        break;
    case PORT_WX_TIER_FLURRY:
        a = 0.25f + 0.15f * sinf(t * 0.3f);
        b = 0.05f + 0.08f * sinf(t * 0.21f);
        break;
    case PORT_WX_TIER_BLIZZARD:
    {
        const float step = 2.4f;
        const int k = (int)floorf(t / step);
        const float u = t / step - k;
        float va[2], vb[2];
        for (int j = 0; j < 2; j++)
        {
            const uint32_t kk = (uint32_t)(k + j + 1000);
            const float h = h01(seed, kk, 0xB11Au);
            va[j] = h < 0.16f ? -(1.5f + 14.0f * h) : 1.0f + 6.2f * (h - 0.16f) / 0.84f;  // a swing round / a gust
            vb[j] = -0.6f + 2.2f * h01(seed, kk, 0xB11Bu);
        }
        const float k2 = inten * 1.1f > 1.0f ? 1.0f : inten * 1.1f;
        a = cosmix(va[0], va[1], u) * k2 + 0.25f * (1.0f - k2) + k2 * (0.45f * sinf(t * 2.3f) + 0.3f * sinf(t * 5.1f + 1.3f));
        b = cosmix(vb[0], vb[1], u) * k2 + k2 * 0.3f * sinf(t * 1.9f + 0.4f);
        break;
    }
    default:
        break;
    }
    const float ang = h01(seed, 7u, 0xD1Bu) * 6.2831853f;
    const float c = cosf(ang), s = sinf(ang);
    *vx = a * c - b * s;
    *vy = a * s + b * c;
}

// ---- lightning (THUNDERSTORM): strikes every 7-18 s from the seed (the first 6-10 s in), each far / mid / close: the
// flash (its return strokes, wx_common.py flash()), the bolt (hangs a little longer), the thunder after distance / 343
// m/s (far 2.2-3.5 s: a long rolling rumble, mid 0.9-1.6 s, close 0.05-0.15 s: the crack + boom, a light rumble in
// your hands).
struct Strike
{
    int id;
    float t, delay, power, x;
    int kind;  // 0 far, 1 mid, 2 close
    int npulse;
    float off[5], amp[5];
    bool thundered;
};
const int kStrikesLive = 4;
Strike s_strike[kStrikesLive];
int s_strikeN = 0;
int s_nextStrike = 0;
float s_nextStrikeT = 0.0f;

Strike make_strike(uint32_t seed, int id, float t)
{
    Strike s;
    memset(&s, 0, sizeof s);
    s.id = id;
    s.t = t;
    const float hk = h01(seed, (uint32_t)id, 0x57A1u);
    // never two close ones running (the first is always far: the storm is coming)
    s.kind = id == 0 ? 0 : (hk < 0.40f ? 0 : (hk < 0.76f ? 1 : 2));
    const float hd = h01(seed, (uint32_t)id, 0x57A2u);
    static const float kOff[3][5] = {{0.0f, 0.11f, 0.30f, 0, 0}, {0.0f, 0.12f, 0.25f, 0, 0}, {0.0f, 0.07f, 0.16f, 0.33f, 0.52f}};
    static const float kAmp[3][5] = {{0.7f, 1.0f, 0.45f, 0, 0}, {0.9f, 0.5f, 0.8f, 0, 0}, {1.0f, 0.55f, 0.95f, 0.6f, 0.3f}};
    static const int kN[3] = {3, 3, 5};
    s.npulse = kN[s.kind];
    for (int i = 0; i < s.npulse; i++)
    {
        s.off[i] = kOff[s.kind][i] * (0.85f + 0.3f * h01(seed, (uint32_t)id, 0x57B0u + i));
        s.amp[i] = kAmp[s.kind][i] * (0.85f + 0.3f * h01(seed, (uint32_t)id, 0x57C0u + i));
    }
    s.delay = s.kind == 0 ? 2.2f + 1.3f * hd : (s.kind == 1 ? 0.9f + 0.7f * hd : 0.05f + 0.1f * hd);
    s.power = s.kind == 0 ? 0.45f : (s.kind == 1 ? 0.7f : 1.0f);
    s.x = (h01(seed, (uint32_t)id, 0x57A3u) * 2.0f - 1.0f) * 0.75f;
    return s;
}

float strike_flash(const Strike& s, float t)
{
    float f = 0.0f;
    for (int i = 0; i < s.npulse; i++)
    {
        const float dt = t - s.t - s.off[i];
        if (dt >= 0.0f && dt < 0.6f)
        {
            const float att = dt / 0.012f < 1.0f ? dt / 0.012f : 1.0f;
            f += s.power * s.amp[i] * att * expf(-dt / 0.065f);
        }
    }
    return f;
}

float strike_bolt(const Strike& s, float t)
{
    const float last = s.t + s.off[s.npulse - 1];
    if (t < s.t || t > last + 0.22f)
        return 0.0f;
    float best = 0.0f;
    for (int i = 0; i < s.npulse; i++)
    {
        const float dt = t - s.t - s.off[i];
        if (dt >= 0.0f && dt < 0.25f)
        {
            const float v = s.amp[i] * expf(-dt / 0.09f);
            best = v > best ? v : best;
        }
    }
    return best > 1.0f ? 1.0f : best;
}

std::mutex s_boltLock;
int s_boltId = -1, s_boltKind = 0;
float s_boltVis = 0.0f, s_boltX = 0.0f;

// ---- the sound: beds (looping) + thunder (one-shots), mixed by PortWeatherAudioMix on the audio side
struct Pcm
{
    std::vector<short> lr;  // interleaved stereo, 32 kHz
};
enum { kBedRainLight = 0, kBedRainHeavy, kBedBreeze, kBedHowl, kBeds };
const char* const kBedFile[kBeds] = {"rain_light", "rain_heavy", "wind_breeze", "wind_howl"};
const char* const kThunderFile[6] = {"thunder_far_0", "thunder_far_1", "thunder_mid_0", "thunder_mid_1", "thunder_close_0",
                                     "thunder_close_1"};
Pcm s_bed[kBeds];
Pcm s_thunder[6];
std::atomic<int> s_soundState(0);  // 0 not loaded, 1 loading, 2 ready, 3 failed
std::atomic<float> s_bedGain[kBeds];
std::atomic<float> s_bedPan[kBeds];
struct ThunderEvent
{
    int clip;
    float gain, pan;
};
std::mutex s_thunderLock;
ThunderEvent s_thunderQ[4];
int s_thunderQN = 0;
float s_listenRX = 1.0f, s_listenRZ = 0.0f, s_listenYaw = 0.0f;

std::string weather_dir()
{
    const char* home = getenv("STRIKERS_HOME");
    std::string h = home != NULL && *home != '\0' ? home : "/storage/emulated/0/Strikers";
    return h + "/franchise/weather/";
}

bool read_wav_stereo(const std::string& path, Pcm& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (f == NULL)
        return false;
    unsigned char hdr[12];
    bool ok = fread(hdr, 1, 12, f) == 12 && memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0;
    int channels = 0, bits = 0;
    while (ok)
    {
        unsigned char ch[8];
        if (fread(ch, 1, 8, f) != 8)
            break;
        const uint32_t size = ch[4] | (ch[5] << 8) | (ch[6] << 16) | ((uint32_t)ch[7] << 24);
        if (memcmp(ch, "fmt ", 4) == 0)
        {
            unsigned char fm[16];
            if (size < 16 || fread(fm, 1, 16, f) != 16)
            {
                ok = false;
                break;
            }
            channels = fm[2] | (fm[3] << 8);
            bits = fm[14] | (fm[15] << 8);
            if (size > 16)
                fseek(f, (long)(size - 16), SEEK_CUR);
        }
        else if (memcmp(ch, "data", 4) == 0)
        {
            if (bits != 16 || (channels != 1 && channels != 2))
            {
                ok = false;
                break;
            }
            std::vector<short> raw(size / 2);
            const size_t got = fread(raw.data(), 2, raw.size(), f);
            raw.resize(got);
            if (channels == 2)
                out.lr.swap(raw);
            else
            {
                out.lr.resize(raw.size() * 2);
                for (size_t i = 0; i < raw.size(); i++)
                    out.lr[2 * i] = out.lr[2 * i + 1] = raw[i];
            }
            break;
        }
        else
            fseek(f, (long)(size + (size & 1)), SEEK_CUR);
    }
    fclose(f);
    return ok && !out.lr.empty();
}

void load_sounds()
{
    int expected = 0;
    if (!s_soundState.compare_exchange_strong(expected, 1))
        return;
    std::thread([] {
        const std::string dir = weather_dir();
        int n = 0;
        for (int i = 0; i < kBeds; i++)
            n += read_wav_stereo(dir + kBedFile[i] + ".wav", s_bed[i]) ? 1 : 0;
        for (int i = 0; i < 6; i++)
            n += read_wav_stereo(dir + kThunderFile[i] + ".wav", s_thunder[i]) ? 1 : 0;
        fprintf(stderr, "[weather] r196 sounds: %d of 10 in %s\n", n, dir.c_str());
        fflush(stderr);
        s_soundState.store(n > 0 ? 2 : 3);
    }).detach();
}

// ---- r196: the snow lying on the pitch (a grid over it): piles up where nobody has played for a while (faster at the
// boards and in the corners: it drifts there), trampled where the players and the ball go (a slushy scuff left behind
// that fills in again). Looks only: positions are read, never written.
// r214: 48 x 28 cells (xr_snow.h pitch_step: 0.86 m; r196's 32 x 18 were 1.3 m - a run was a 2.6 m smear); the
// life-size stadium still reads r196's 32 x 18 (PortWeatherSnowGrid samples this down)
const int kSnowCols = xrsnow::kPitchCols, kSnowRows = xrsnow::kPitchRows;
float s_snowD[kSnowRows][kSnowCols];  // how deep 0..1
float s_snowS[kSnowRows][kSnowCols];  // the scuff (trampled lately) 0..1
std::mutex s_snowLock;
float s_snowCopy[kSnowRows * kSnowCols];
bool s_snowAny = false;

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

// ---- r214: the snow's own texture (xr_snow.h game_atlas: the coverage stages, the powder, slush, flake sprites, a glint;
// 1024 x 512 RGBA8 straight alpha + 3 mips, 2.7 MB), made on a thread at the first snow - never on the game's frame - then
// one GX texture object of ours (ball_skin.cpp's way: GX_TF_RGBA8_PC, the pixels in our own vector, uploaded by Aurora once).
// The rain keeps r148's small atlas above, untouched.
std::atomic<int> s_snowTexState(0);  // 0 not asked, 1 making, 2 pixels ready, 3 ready, -1 failed
std::vector<uint8_t> s_snowPx;
PlatTexture* s_snowTex = NULL;

bool snow_tex_ready()
{
    const int st = s_snowTexState.load();
    if (st == 3)
        return true;
    if (st == 0)
    {
        int expect = 0;
        if (s_snowTexState.compare_exchange_strong(expect, 1))
            std::thread([]() {
                struct timespec a, b;
                clock_gettime(CLOCK_MONOTONIC, &a);
                std::vector<uint8_t> px(xrsnow::atlas_bytes());
                std::vector<float> scratch((size_t)3 * xrsnow::kTile * xrsnow::kTile);
                xrsnow::game_atlas(px.data(), scratch.data());
                s_snowPx.swap(px);
                clock_gettime(CLOCK_MONOTONIC, &b);
                fprintf(stderr, "[weather] r214 snow texture made on its thread: %dx%d, %d mips, %.1f MB in %.0f ms\n", xrsnow::kAtlasW,
                        xrsnow::kAtlasH, xrsnow::kAtlasLevels, s_snowPx.size() / 1048576.0,
                        (b.tv_sec - a.tv_sec) * 1000.0 + (b.tv_nsec - a.tv_nsec) / 1e6);
                fflush(stderr);
                s_snowTexState.store(2);
            }).detach();
        return false;
    }
    if (st != 2)
        return false;
    void* mem = port_region_reserve(sizeof(PlatTexture), 32);
    if (mem == NULL)
    {
        s_snowTexState.store(-1);
        fprintf(stderr, "[weather] r214 no room for the snow texture's header - no snow lying / flakes\n");
        fflush(stderr);
        return false;
    }
    PlatTexture* t = new (mem) PlatTexture();
    t->m_Width = (u16)xrsnow::kAtlasW;
    t->m_Height = (u16)xrsnow::kAtlasH;
    t->m_Levels = (u8)xrsnow::kAtlasLevels;
    t->m_MaxLevel = (u8)xrsnow::kAtlasLevels;
    t->m_Format = GXTex_RGBA8;  // (the game's own bookkeeping; the GX object says what the pixels really are)
    t->m_SwizzledData = s_snowPx.data();
    GXInitTexObj(&t->m_TexObj, s_snowPx.data(), (u16)xrsnow::kAtlasW, (u16)xrsnow::kAtlasH, GX_TF_RGBA8_PC, GX_CLAMP, GX_CLAMP, GX_TRUE);
    GXInitTexObjMaxLOD(&t->m_TexObj, (float)(xrsnow::kAtlasLevels - 1));
    GXInitTexObjFilter(&t->m_TexObj, GX_LIN_MIP_LIN, GX_LINEAR);
    s_snowTex = t;
    s_snowTexState.store(3);
    return true;
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
    Batch(int quads, int blend, bool depthTest, bool twoD, PlatTexture* tex = NULL)
        : open(false)
    {
        if (tex == NULL)
            tex = s_atlas;  // (r214: the snow's batches hand their own texture)
        if (quads <= 0 || tex == NULL)
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
        glSetCurrentTexture((uintptr_t)tex, GLTT_Diffuse);
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
const int kCamDrops = 640;  // r196: 260 -> 420 (THUNDERSTORM 330, BLIZZARD 420 at full load); r215: 640 (the snow's up to 600; rain's counts unchanged)
Drop s_cam[kCamDrops];
int s_camKind = -1;  // which weather the drops were made for

void cam_spawn(Drop& d, bool rain, bool anywhere, const Cam& c, int tier)
{
    const float depth = 1.6f + 16.0f * powf(roll01(), 1.6f);
    const float u = (roll01() * 2.0f - 1.0f) * 1.15f;
    const float v = anywhere ? (roll01() * 2.0f - 1.0f) * 1.1f : 1.12f;
    d.p[0] = u * depth * c.tanX;
    d.p[1] = v * depth * c.tanY;
    d.p[2] = -depth;
    // r196: the tier's drops - DRIZZLE thin and slower, THUNDERSTORM heavy and fast; FLURRY slow fat flakes
    const float rs = tier == PORT_WX_TIER_DRIZZLE ? 0.8f : (tier == PORT_WX_TIER_STORM ? 1.12f : 1.0f);
    d.speed = rain ? (9.0f + 3.0f * roll01()) * rs : (tier == PORT_WX_TIER_FLURRY ? 0.6f : 0.9f) + 0.6f * roll01();
    d.size = rain ? (0.0045f + 0.003f * roll01()) * (tier == PORT_WX_TIER_DRIZZLE ? 0.75f : (tier == PORT_WX_TIER_STORM ? 1.2f : 1.0f))
                  : 0.018f + 0.022f * roll01();
    d.phase = roll01() * 6.2831853f;
}

// r214 (Lance: "make the snow wayyyyy more realistic ... from texture to behavior"): the camera's SNOW - soft lumpy flakes
// (the snow texture's sprites, xr_snow.h flake_alpha) of every size (log-normal: most small, a few big clumps; a flurry
// a few big lazy ones), each tumbling (turned and squashed as it flutters) and swaying at its own pace, swirled by an eddy
// field in the gusts, the nearest out of focus (a soft bigger disc, fainter), the far ones smaller; a blizzard's blown
// ones streak (r196); a flake that reaches the pitch melts into it (faded over its last 35 cm, then gone). One batch of
// 2D quads as before (its own texture). The rain is r196's, untouched.
void cam_spawn_snow(Drop& d, bool anywhere, const Cam& c, int tier)
{
    cam_spawn(d, false, anywhere, c, tier);
    const float g = roll01() + roll01() + roll01() - 1.5f;  // (~N(0, 0.5))
    const bool flurry = tier == PORT_WX_TIER_FLURRY;
    // r215 (Lance on r214: "it truly looks like less snow is falling or its not falling hard like before"): measured
    // (tools/outside/snow215/measure215.py) r214's blizzard showed half r213's snow - flakes 2.0 cm (r213 ~2.9), slower; now
    // 3.0 cm (flurry 4.2) with less spread, falling 1.25-2.0 m/s (flurry 0.75-1.25; r213 0.9-1.5 / 0.6-1.2)
    float s = (flurry ? 0.042f : 0.030f) * expf((flurry ? 0.7f : 0.55f) * g);
    d.size = s < 0.008f ? 0.008f : (s > 0.09f ? 0.09f : s);
    // big flakes fall a little slower (more air for their weight)
    const float v = flurry ? 0.75f + 0.5f * roll01() : 1.25f + 0.75f * roll01();
    d.speed = v * (1.0f - (d.size - 0.03f) * 3.0f > 0.8f ? 1.0f - (d.size - 0.03f) * 3.0f : 0.8f);
}

void camera_snow(int bits, int tier, float inten, float fade, float dt, float load, const float* windW, float gust, float flash,
                 const nlMatrix4& view, const Cam& c)
{
    if (!snow_tex_ready())
        return;
    const bool flurry = tier == PORT_WX_TIER_FLURRY, blizzard = tier == PORT_WX_TIER_BLIZZARD;
    const int kind = 2 + 4 * tier + 64;  // (r214's flakes: a fresh set of them)
    // r215: more of them (r213: 60 + 360 x the blizzard's strength, r214 a flurry 75): a flurry 140, snow 180 .. a blizzard 600
    const float want = flurry ? 140.0f : 180.0f + 420.0f * inten;
    int n = (int)(want * load);
    n = n < 30 ? 30 : (n > kCamDrops ? kCamDrops : n);
    if (s_camKind != kind)
    {
        s_camKind = kind;
        for (int i = 0; i < kCamDrops; i++)
            cam_spawn_snow(s_cam[i], true, c, tier);
    }
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
    float wcam[3];
    for (int k = 0; k < 3; k++)
        wcam[k] = windW[0] * view.e2[0][k] + windW[1] * view.e2[1][k];
    if (blizzard)  // r215: a blizzard driven harder by its wind (the slant + the streaks)
        for (int k = 0; k < 3; k++)
            wcam[k] *= 1.25f;
    wcam[0] += 0.15f;
    Batch b(n, 1, false, true, s_snowTex);
    if (!b.open)
        return;
    static float s_t = 0.0f;
    s_t += dt;
    unsigned char al[4];
    const float lit = 1.0f + 1.3f * (flash > 1.0f ? 1.0f : flash);
    const bool night = (bits & PORT_WX_NIGHT) != 0;
    const unsigned char cr = night ? 225 : 250, cg = night ? 230 : 252, cb = 255;
    for (int i = 0; i < n; i++)
    {
        Drop& d = s_cam[i];
        const float h = d.phase / 6.2831853f;
        const float ff = 0.7f + 0.9f * fmodf(h * 7.31f, 1.0f);
        const float flut = (flurry ? 0.5f : 0.3f) * (0.4f + 20.0f * d.size) * (0.6f + 0.6f * gust);
        const float fx = 0.85f + 0.3f * h;
        float mv[3];
        for (int k = 0; k < 3; k++)
            mv[k] = down[k] * d.speed + wcam[k] * fx;
        mv[0] += flut * sinf(s_t * ff * 2.4f + d.phase);
        mv[1] += 0.5f * flut * cosf(s_t * ff * 1.9f + d.phase * 1.3f);
        const float sw = gust * (blizzard ? 1.4f : 0.6f);  // the eddies in the gusts
        mv[0] += sw * sinf(d.p[1] * 0.8f + s_t * 1.1f + h * 2.0f);
        mv[1] += 0.6f * sw * sinf(d.p[0] * 0.7f - s_t * 0.9f);
        d.p[0] += mv[0] * dt;
        d.p[1] += mv[1] * dt;
        d.p[2] += mv[2] * dt;
        const float depth = -d.p[2];
        // how high over the pitch (game world Z) it is: the view's rows turned back
        // (r215: no longer culled / faded at the pitch's height - r214's ground test thinned the picture's snow; r213 had none)
        if (depth < 1.2f || depth > 19.0f || fabsf(d.p[0]) > depth * c.tanX * 1.25f || d.p[1] < -depth * c.tanY * 1.2f ||
            d.p[1] > depth * c.tanY * 1.3f)
        {
            cam_spawn_snow(d, false, c, tier);
            if (fabsf(wcam[0]) > 1.2f && roll01() < 0.55f)
            {
                const float dd = -d.p[2];
                d.p[0] = (wcam[0] > 0.0f ? -1.0f : 1.0f) * dd * c.tanX * 1.2f;
                d.p[1] = (roll01() * 2.0f - 1.0f) * dd * c.tanY;
            }
            continue;
        }
        const bool nearF = depth < 2.4f;  // out of focus
        float a = fade * 0.95f * lit * (nearF ? 0.7f : 1.0f);  // r215 (r214: 0.85, near x0.55, far x0.8)
        a = a > 1.0f ? 1.0f : a;
        float hx, hy;
        if (!to_screen(c, v3(d.p[0], d.p[1], d.p[2]), &hx, &hy))
            continue;
        const float spd = sqrtf(mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2]);
        float r = d.size * px_per_m(c, depth) * (nearF ? 1.5f : 1.0f);
        r = r < 1.4f ? 1.4f : r;
        if (blizzard && spd > 2.5f)
        {
            // a blown flake (r196): its path over 1/80 s, a soft tail to its head
            const float len = spd / 50.0f;  // r215: a longer, harder streak (r196-r214: 1/80 s)
            float tx, ty;
            if (!to_screen(c, v3(d.p[0] - mv[0] / spd * len, d.p[1] - mv[1] / spd * len, d.p[2] - mv[2] / spd * len), &tx, &ty))
                continue;
            float ax = ty - hy, ay = -(tx - hx);
            const float al2 = sqrtf(ax * ax + ay * ay);
            if (al2 > 1e-3f)
            {
                ax = ax / al2 * r * 0.9f;  // r215: as wide as r213's (r214 0.6)
                ay = ay / al2 * r * 0.9f;
                V3 q[4] = {v3(tx - ax, ty - ay, -1.0f), v3(tx + ax, ty + ay, -1.0f), v3(hx + ax, hy + ay, -1.0f), v3(hx - ax, hy - ay, -1.0f)};
                Rect rr;
                xrsnow::sprite_rect(xrsnow::kSprDot, &rr.u0, &rr.v0, &rr.u1, &rr.v1);
                const float m = 0.2f * (rr.v1 - rr.v0);
                rr.v0 += m;
                rr.v1 -= m;
                al[0] = al[1] = 0;
                al[2] = al[3] = byte(a * 0.85f);
                b.quad(q, rr, cr, cg, cb, al);
            }
            continue;
        }
        // tumbling: turned, and squashed as it turns edge-on now and then
        const float th = d.phase * 3.0f + s_t * (0.6f + 1.6f * h);
        const float sq = 0.7f + 0.3f * fabsf(cosf(s_t * (1.1f + 1.7f * fmodf(h * 3.7f, 1.0f)) + d.phase));  // r215 (r214: 0.45 +)
        const float ex = cosf(th) * r, ey = sinf(th) * r;
        const float fxx = -sinf(th) * r * sq, fyy = cosf(th) * r * sq;
        V3 q[4] = {v3(hx - ex - fxx, hy - ey - fyy, -1.0f), v3(hx + ex - fxx, hy + ey - fyy, -1.0f), v3(hx + ex + fxx, hy + ey + fyy, -1.0f),
                   v3(hx - ex + fxx, hy - ey + fyy, -1.0f)};
        Rect rr;
        xrsnow::sprite_rect((nearF ? xrsnow::kSprBokeh : xrsnow::kSprFlake) + ((int)(h * 16.0f) & 3), &rr.u0, &rr.v0, &rr.u1, &rr.v1);
        al[0] = al[1] = al[2] = al[3] = byte(a);
        b.quad(q, rr, cr, cg, cb, al);
    }
    b.end(GLV_UnsortedOrtho);
}

void camera_weather(int bits, int tier, float inten, float fade, float dt, float load, const float* windW, float gust,
                    float flash, const nlMatrix4& view, const Cam& c)
{
    const bool rain = (bits & PORT_WX_RAIN) != 0;
    const bool snow = (bits & PORT_WX_SNOW) != 0;
    if (!rain && !snow)
        return;
    if (!rain)
    {
        camera_snow(bits, tier, inten, fade, dt, load, windW, gust, flash, view, c);  // r214
        return;
    }
    const int kind = (rain ? 1 : 2) + 4 * tier;
    // r196: the count by tier (drizzle 110, rain 220, storm 330; flurry 90 .. blizzard 420 as it builds)
    const float want = rain ? (tier == PORT_WX_TIER_DRIZZLE ? 110.0f : (tier == PORT_WX_TIER_STORM ? 330.0f : 220.0f))
                            : 60.0f + 360.0f * inten;
    int n = (int)(want * load);
    n = n < 40 ? 40 : (n > kCamDrops ? kCamDrops : n);
    if (s_camKind != kind)
    {
        s_camKind = kind;
        for (int i = 0; i < kCamDrops; i++)
            cam_spawn(s_cam[i], rain, true, c, tier);
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
    // r196: the match's wind (game world, Z up) in the camera's space: rain slants with it, snow is blown by it (the
    // row-vector view: world X is row 0, Y row 1). The old fixed drift stays as the floor of it.
    float wcam[3];
    for (int k = 0; k < 3; k++)
        wcam[k] = windW[0] * view.e2[0][k] + windW[1] * view.e2[1][k];
    wcam[0] += rain ? 0.35f : 0.15f;
    const bool night = (bits & PORT_WX_NIGHT) != 0;
    const bool blizzard = tier == PORT_WX_TIER_BLIZZARD;
    Batch b(n, 1, false, true);
    if (!b.open)
        return;
    static float s_t = 0.0f;
    s_t += dt;
    unsigned char al[4];
    const float lit = 1.0f + 1.3f * (flash > 1.0f ? 1.0f : flash);  // the lightning lights the drops up
    for (int i = 0; i < n; i++)
    {
        Drop& d = s_cam[i];
        const float sway = snow ? (0.35f + 0.5f * gust * inten) * sinf(s_t * 1.3f + d.phase) : 0.0f;
        const float fx = 0.85f + 0.3f * (d.phase / 6.2831853f);
        float mv[3];
        for (int k = 0; k < 3; k++)
            mv[k] = down[k] * d.speed + wcam[k] * fx;
        mv[0] += sway;
        if (blizzard)
            mv[1] += 0.25f * gust * sinf(s_t * 2.1f + d.phase);
        d.p[0] += mv[0] * dt;
        d.p[1] += mv[1] * dt;
        d.p[2] += mv[2] * dt;
        const float depth = -d.p[2];
        if (depth < 1.2f || depth > 19.0f || fabsf(d.p[0]) > depth * c.tanX * 1.25f || d.p[1] < -depth * c.tanY * 1.2f ||
            d.p[1] > depth * c.tanY * 1.3f)
        {
            cam_spawn(d, rain, false, c, tier);
            // blown in from the side the wind comes from, not only from the top
            if (fabsf(wcam[0]) > 1.2f && roll01() < 0.55f)
            {
                const float dd = -d.p[2];
                d.p[0] = (wcam[0] > 0.0f ? -1.0f : 1.0f) * dd * c.tanX * 1.2f;
                d.p[1] = (roll01() * 2.0f - 1.0f) * dd * c.tanY;
            }
        }
        const float tierA = rain ? (tier == PORT_WX_TIER_DRIZZLE ? 0.7f : (tier == PORT_WX_TIER_STORM ? 1.25f : 1.0f)) : 1.0f;
        float a = fade * tierA * (rain ? (night ? 0.42f : 0.32f) : 0.85f) *
                  (depth < 3.0f ? 0.6f + 0.4f * (depth - 1.2f) / 1.8f : 1.0f) * lit;
        a = a > 1.0f ? 1.0f : a;
        float hx, hy;
        if (!to_screen(c, v3(d.p[0], d.p[1], d.p[2]), &hx, &hy))
            continue;
        const float spd = sqrtf(mv[0] * mv[0] + mv[1] * mv[1] + mv[2] * mv[2]);
        if (rain || (blizzard && spd > 2.5f))
        {
            // a streak: the drop's path over a 1/30 s shutter (a blown flake's over 1/80 s), the soft dot stretched along it
            const float len = rain ? spd * (tier == PORT_WX_TIER_STORM ? 0.04f : 0.033f) : spd / 80.0f;
            float tx, ty;
            if (spd < 1e-3f ||
                !to_screen(c, v3(d.p[0] - mv[0] / spd * len, d.p[1] - mv[1] / spd * len, d.p[2] - mv[2] / spd * len), &tx, &ty))
                continue;
            if (!rain)
            {
                // the blown flake: its head a soft dot, its tail a fainter streak
                float ax = ty - hy, ay = -(tx - hx);
                const float al2 = sqrtf(ax * ax + ay * ay);
                const float s = d.size * px_per_m(c, depth);
                const float r = s < 0.8f ? 0.8f : s;
                if (al2 > 1e-3f)
                {
                    ax = ax / al2 * r * 0.9f;
                    ay = ay / al2 * r * 0.9f;
                    V3 q[4] = {v3(tx - ax, ty - ay, -1.0f), v3(tx + ax, ty + ay, -1.0f), v3(hx + ax, hy + ay, -1.0f),
                               v3(hx - ax, hy - ay, -1.0f)};
                    const Rect rr = {kDot.u0, kDot.v0 + 0.2f * (kDot.v1 - kDot.v0), kDot.u1, kDot.v1 - 0.2f * (kDot.v1 - kDot.v0)};
                    al[0] = al[1] = 0;
                    al[2] = al[3] = byte(a * 0.85f);
                    b.quad(q, rr, 250, 252, 255, al);
                }
                continue;
            }
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

// ------------------------------------------------------------------------------------------------ r196: the bolt
// One shape per strike from its seed (wx_preview.py make_bolt: midpoint displacement, 6 levels for the main channel,
// 2-4 branches): segments in the bolt's own space - x across (0 its top), y 0 at the cloud .. 1 where it ends - and a
// width (1 the main channel, 0.5 a branch). The same shape in every view (the picture here, the life-size stadium's sky).
struct BoltSeg
{
    float x0, y0, x1, y1, w;
};
const int kBoltSegs = 200;

int bolt_shape(uint32_t seed, int kind, BoltSeg* out, int max)
{
    wxcal::Rng r = {wxcal::hash3(seed, 0xB017u, (uint32_t)kind)};
    auto nrm = [&r]() {
        // ~N(0, 1) from four uniforms
        float s = 0.0f;
        for (int k = 0; k < 4; k++)
            s += (r.next() & 0xFFFFu) / 65535.0f;
        return (s - 2.0f) * 1.732f;
    };
    float px[65], py[65];
    float tmpx[65], tmpy[65];
    int n = 2;
    px[0] = 0.0f;
    py[0] = 0.0f;
    px[1] = ((r.next() & 0xFFFFu) / 65535.0f - 0.5f) * 0.35f;
    py[1] = 1.0f;
    const float rough = 0.21f;
    for (int it = 0; it < 6; it++)
    {
        int m = 0;
        tmpx[m] = px[0];
        tmpy[m++] = py[0];
        for (int i = 0; i + 1 < n; i++)
        {
            const float dx = px[i + 1] - px[i], dy = py[i + 1] - py[i];
            const float L = sqrtf(dx * dx + dy * dy) + 1e-6f;
            const float off = nrm() * rough * L;
            tmpx[m] = 0.5f * (px[i] + px[i + 1]) - dy / L * off;
            tmpy[m++] = 0.5f * (py[i] + py[i + 1]) + dx / L * off;
            tmpx[m] = px[i + 1];
            tmpy[m++] = py[i + 1];
        }
        n = m;
        memcpy(px, tmpx, sizeof(float) * n);
        memcpy(py, tmpy, sizeof(float) * n);
    }
    int k = 0;
    for (int i = 0; i + 1 < n && k < max; i++)
    {
        BoltSeg s = {px[i], py[i], px[i + 1], py[i + 1], 1.0f};
        out[k++] = s;
    }
    const int branches = kind == 0 ? 2 : 4;
    for (int bI = 0; bI < branches; bI++)
    {
        const int j = 8 + (int)(r.next() % 34u);  // where it forks (0.15-0.7 of the way)
        float bx[33], by[33], cx[33], cy[33];
        int bn = 2;
        bx[0] = px[j];
        by[0] = py[j];
        const float L = (0.2f + 0.25f * (r.next() & 0xFFFFu) / 65535.0f);
        const float side = (r.next() & 1u) ? 1.0f : -1.0f;
        bx[1] = bx[0] + side * L * (0.4f + 0.5f * (r.next() & 0xFFFFu) / 65535.0f);
        by[1] = by[0] + L;
        for (int it = 0; it < 5; it++)
        {
            int m = 0;
            cx[m] = bx[0];
            cy[m++] = by[0];
            for (int i = 0; i + 1 < bn; i++)
            {
                const float dx = bx[i + 1] - bx[i], dy = by[i + 1] - by[i];
                const float LL = sqrtf(dx * dx + dy * dy) + 1e-6f;
                const float off = nrm() * rough * 1.2f * LL;
                cx[m] = 0.5f * (bx[i] + bx[i + 1]) - dy / LL * off;
                cy[m++] = 0.5f * (by[i] + by[i + 1]) + dx / LL * off;
                cx[m] = bx[i + 1];
                cy[m++] = by[i + 1];
            }
            bn = m;
            memcpy(bx, cx, sizeof(float) * bn);
            memcpy(by, cy, sizeof(float) * bn);
        }
        for (int i = 0; i + 1 < bn && k < max; i++)
        {
            // a branch thins out toward its end
            BoltSeg s = {bx[i], by[i], bx[i + 1], by[i + 1], 0.55f * (1.0f - 0.6f * (float)i / bn)};
            out[k++] = s;
        }
    }
    return k;
}

// The bolt in the broadcast picture's sky: the far ones small and high (behind the far stands), the close one from
// above the picture down to the stands' line. Additive: a wide blue-white glow, then the white-hot channel. Drawn after
// the grade, before the HUD.
void camera_bolt(uint32_t seed, int kind, float x, float vis, const Cam& c)
{
    if (vis <= 0.02f || c.W <= 0.0f || c.H <= 0.0f)
        return;
    static BoltSeg seg[kBoltSegs];
    const int n = bolt_shape(seed, kind, seg, kBoltSegs);
    // its box in the picture (fractions of W / H): the preview's placements
    const float cx = (0.5f + 0.44f * x) * c.W;
    const float top = (kind == 2 ? -0.03f : (kind == 1 ? -0.01f : 0.10f)) * c.H;
    const float bot = (kind == 2 ? 0.42f : (kind == 1 ? 0.30f : 0.29f)) * c.H;
    const float scale = bot - top;
    const float wpx = (kind == 2 ? 3.4f : (kind == 1 ? 2.4f : 1.6f)) * c.H / 720.0f;
    Batch b(2 * n, 3, false, true);
    if (!b.open)
        return;
    const float vm = 0.5f * (kDot.v0 + kDot.v1);
    const Rect across = {kDot.u0, vm, kDot.u1, vm};
    unsigned char al[4];
    for (int pass = 0; pass < 2; pass++)
    {
        const bool glow = pass == 0;
        for (int i = 0; i < n; i++)
        {
            const BoltSeg& s = seg[i];
            const float x0 = cx + s.x0 * scale, y0 = top + s.y0 * scale, x1 = cx + s.x1 * scale, y1 = top + s.y1 * scale;
            float ax = y1 - y0, ay = -(x1 - x0);
            const float l = sqrtf(ax * ax + ay * ay);
            if (l < 1e-3f)
                continue;
            const float w = (glow ? 7.0f : 1.0f) * wpx * s.w;
            ax = ax / l * w;
            ay = ay / l * w;
            // a little overlap along the channel so the joints never show gaps
            const float ex = (x1 - x0) / l * w * 0.3f, ey = (y1 - y0) / l * w * 0.3f;
            V3 q[4] = {v3(x0 - ax - ex, y0 - ay - ey, -1.0f), v3(x0 + ax - ex, y0 + ay - ey, -1.0f),
                       v3(x1 + ax + ex, y1 + ay + ey, -1.0f), v3(x1 - ax + ex, y1 - ay + ey, -1.0f)};
            al[0] = al[1] = al[2] = al[3] = byte(vis * (glow ? 0.32f : 1.0f) * (s.w < 0.9f ? 0.85f : 1.0f));
            if (glow)
                b.quad(q, across, 150, 172, 255, al);
            else
                b.quad(q, across, 240, 244, 255, al);
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

// r214 (Lance: "make the snow wayyyyy more realistic on the field, from texture to behavior ... it looks like white
// pixels"): the snow LYING on the pitch, from the grid (xr_snow.h pitch_step: piled where nobody played - the boards, the
// corners, behind the goals, the downwind side - trampled where they ran), drawn with the snow texture's coverage stages:
// each grid vertex's depth says how far the snow has grown there (xr_snow.h stage_level / stage_alpha) - a powder first,
// then patches from the surface's high points that spread and join into the blanket, a deep drift with its wind ripples;
// stages drawn in order with the vertex alphas, so it grows smoothly and never goes thin while it does (nested coverage:
// a quad of 2 x 2 cells draws its highest stage begun over the one below). Each shows its share of a 6 x 6-cell tile (5.2 m,
// 2.1 cm a texel). Over it the slush where they ran (grey-blue mush, boot marks, the grass showing a little), then a few glints
// on the deep snow that come and go as the camera moves (additive). Two draws (r196: in the pitch batch).
void pitch_snow(int bits, float fade, const nlMatrix4& view, float dt)
{
    if (!snow_tex_ready())
        return;
    static float gd[kSnowRows][kSnowCols], gs[kSnowRows][kSnowCols];
    {
        std::lock_guard<std::mutex> lock(s_snowLock);
        memcpy(gd, s_snowD, sizeof(gd));
        memcpy(gs, s_snowS, sizeof(gs));
    }
    const bool night = (bits & PORT_WX_NIGHT) != 0;
    const unsigned char sh = night ? 150 : 255, sb = night ? 175 : 255;  // at night the snow lies in the dark like the rest
    // the stages on quads of 2 x 2 cells (24 x 14: the texture's own patches carry the detail), at the grid's even vertices
    const int DC = kSnowCols / xrsnow::kPitchDraw, DR = kSnowRows / xrsnow::kPitchDraw;
    static float lv[kSnowRows / 2 + 1][kSnowCols / 2 + 1], vs[kSnowRows + 1][kSnowCols + 1];
    for (int vj = 0; vj <= DR; vj++)
        for (int vi = 0; vi <= DC; vi++)
        {
            const float d = xrsnow::pitch_vertex(gd, vi * xrsnow::kPitchDraw, vj * xrsnow::kPitchDraw);
            const float nz = xrsnow::h01((uint32_t)vi, (uint32_t)vj, 0x5A0Eu) - 0.5f;  // (a touch of roughness: never a grid)
            lv[vj][vi] = xrsnow::stage_level(d > 0.02f ? d + 0.07f * nz : d);
        }
    for (int vj = 0; vj <= kSnowRows; vj++)
        for (int vi = 0; vi <= kSnowCols; vi++)
            vs[vj][vi] = xrsnow::pitch_vertex(gs, vi, vj);
    // which stages each quad draws: from its highest stage grown full at all 4 corners (what's under it is hidden: the stages are
    // nested) to its highest one begun - every stage between, so a quad's edge always matches its neighbour's
    static signed char lo[kSnowRows / 2][kSnowCols / 2], hi[kSnowRows / 2][kSnowCols / 2];
    int quads = 0;
    for (int j = 0; j < DR; j++)
        for (int i = 0; i < DC; i++)
        {
            const float l4[4] = {lv[j][i], lv[j][i + 1], lv[j + 1][i + 1], lv[j + 1][i]};
            int h = -1, l = 0;
            for (int k = xrsnow::kStages - 1; k >= 0 && h < 0; k--)
                for (int c = 0; c < 4; c++)
                    if (xrsnow::stage_alpha(l4[c], k) > 0.004f)
                    {
                        h = k;
                        break;
                    }
            for (int k = h; k > 0; k--)
            {
                bool full = true;
                for (int c = 0; c < 4; c++)
                    full = full && xrsnow::stage_alpha(l4[c], k) >= 0.999f;
                if (full)
                {
                    l = k;
                    break;
                }
            }
            hi[j][i] = (signed char)h;
            lo[j][i] = (signed char)(l < 0 ? 0 : l);
            quads += h >= 0 ? h - lo[j][i] + 1 : 0;
        }
    for (int j = 0; j < kSnowRows; j++)
        for (int i = 0; i < kSnowCols; i++)
            quads += (vs[j][i] > 0.02f || vs[j][i + 1] > 0.02f || vs[j + 1][i] > 0.02f || vs[j + 1][i + 1] > 0.02f) ? 1 : 0;
    const float cw = 2.0f * kHalfX / kSnowCols, ch = 2.0f * kHalfY / kSnowRows;
    const float tpc = (float)xrsnow::kTile / (float)xrsnow::kTileCells;  // texels a cell
    const int tq = xrsnow::kTileCells / xrsnow::kPitchDraw;              // quads a tile
    unsigned char al[4];
    {
        Batch b(quads, 1, true, false, s_snowTex);
        if (!b.open)
            return;
        for (int j = 0; j < DR; j++)
            for (int i = 0; i < DC; i++)
            {
                const float x0 = -kHalfX + cw * 2 * i, x1 = x0 + cw * 2, y0 = -kHalfY + ch * 2 * j, y1 = y0 + ch * 2;
                const int vi[4] = {i, i + 1, i + 1, i}, vj[4] = {j, j, j + 1, j + 1};
                const int ti = (i % tq) * 2, tj = (j % tq) * 2;
                for (int k = lo[j][i]; k <= hi[j][i]; k++)
                {
                    int ox, oy;
                    xrsnow::slot_origin(k, &ox, &oy);
                    Rect r;
                    xrsnow::atlas_uv(ox + tpc * ti, oy + tpc * tj, &r.u0, &r.v0);
                    xrsnow::atlas_uv(ox + tpc * (ti + 2), oy + tpc * (tj + 2), &r.u1, &r.v1);
                    const float z = 0.03f + 0.001f * k;
                    V3 c[4] = {v3(x0, y0, z), v3(x1, y0, z), v3(x1, y1, z), v3(x0, y1, z)};
                    for (int q = 0; q < 4; q++)
                        al[q] = byte(fade * xrsnow::stage_alpha(lv[vj[q]][vi[q]], k));
                    b.quad(c, r, sh, sh, sb, al);
                }
            }
        // the slush where they ran (per cell)
        for (int j = 0; j < kSnowRows; j++)
            for (int i = 0; i < kSnowCols; i++)
            {
                if (!(vs[j][i] > 0.02f || vs[j][i + 1] > 0.02f || vs[j + 1][i] > 0.02f || vs[j + 1][i + 1] > 0.02f))
                    continue;
                const float x0 = -kHalfX + cw * i, x1 = x0 + cw, y0 = -kHalfY + ch * j, y1 = y0 + ch;
                const int vi[4] = {i, i + 1, i + 1, i}, vj[4] = {j, j, j + 1, j + 1};
                const int ti = i % xrsnow::kTileCells, tj = j % xrsnow::kTileCells;
                int ox, oy;
                xrsnow::slot_origin(xrsnow::kSlotSlush, &ox, &oy);
                Rect r;
                xrsnow::atlas_uv(ox + tpc * ti, oy + tpc * tj, &r.u0, &r.v0);
                xrsnow::atlas_uv(ox + tpc * (ti + 1), oy + tpc * (tj + 1), &r.u1, &r.v1);
                V3 c[4] = {v3(x0, y0, 0.036f), v3(x1, y0, 0.036f), v3(x1, y1, 0.036f), v3(x0, y1, 0.036f)};
                for (int q = 0; q < 4; q++)
                {
                    const float s = vs[vj[q]][vi[q]];
                    al[q] = byte(fade * 0.9f * (s > 1.0f ? 1.0f : s));
                }
                b.quad(c, r, night ? 120 : 255, night ? 124 : 255, night ? 140 : 255, al);
            }
        b.end(GLV_UnsortedPerspective);
    }
    // the glints: up to 64 on the deep untrodden snow (a hashed spot per cell), each flashing as the camera moves past
    // its angle (+ a slow shimmer), white; facing the camera (its right / up: the view's columns)
    const float rx = view.e2[0][0], ry = view.e2[1][0], rz = view.e2[2][0];
    const float ux = view.e2[0][1], uy = view.e2[1][1], uz = view.e2[2][1];
    const float px = -(view.e2[3][0] * rx + view.e2[3][1] * ux + view.e2[3][2] * view.e2[0][2]);
    const float py = -(view.e2[3][0] * ry + view.e2[3][1] * uy + view.e2[3][2] * view.e2[1][2]);
    static float s_gt = 0.0f;
    s_gt += dt;
    int n = 0;
    float gx[64], gy[64], ga[64];
    for (int j = 0; j < kSnowRows && n < 64; j++)
        for (int i = 0; i < kSnowCols && n < 64; i++)
        {
            if (gd[j][i] < 0.6f || gs[j][i] > 0.15f)
                continue;
            const float h = xrsnow::h01((uint32_t)i, (uint32_t)j, 0x611Fu);
            if (h > 0.30f)
                continue;
            const float tw = fmodf(h * 37.0f + px * 0.21f + py * 0.17f + s_gt * 0.25f, 1.0f);
            const float g = powf(fabsf(tw - 0.5f) < 1.0f / 6.0f ? 1.0f - fabsf(tw - 0.5f) * 6.0f : 0.0f, 3.0f);
            if (g < 0.02f)
                continue;
            gx[n] = -kHalfX + cw * (i + xrsnow::h01((uint32_t)i, (uint32_t)j, 0x6120u));
            gy[n] = -kHalfY + ch * (j + xrsnow::h01((uint32_t)i, (uint32_t)j, 0x6121u));
            ga[n] = g * fade * (night ? 1.0f : 0.8f);
            n++;
        }
    if (n == 0)
        return;
    Batch g(n, 3, true, false, s_snowTex);  // (blend 3: its alpha x its light, added)
    if (!g.open)
        return;
    Rect r;
    xrsnow::sprite_rect(xrsnow::kSprGlint, &r.u0, &r.v0, &r.u1, &r.v1);
    const float s = 0.14f;
    for (int k = 0; k < n; k++)
    {
        const float z = 0.06f;
        V3 c[4] = {v3(gx[k] - (rx + ux) * s, gy[k] - (ry + uy) * s, z - (rz + uz) * s),
                   v3(gx[k] + (rx - ux) * s, gy[k] + (ry - uy) * s, z + (rz - uz) * s),
                   v3(gx[k] + (rx + ux) * s, gy[k] + (ry + uy) * s, z + (rz + uz) * s),
                   v3(gx[k] - (rx - ux) * s, gy[k] - (ry - uy) * s, z - (rz - uz) * s)};
        al[0] = al[1] = al[2] = al[3] = byte(ga[k]);
        g.quad(c, r, 255, 255, 255, al);
    }
    g.end(GLV_UnsortedPerspective);
}

void pitch_dressing(int bits, int tier, float fade, float dt, float cover, float load)
{
    const bool rain = (bits & PORT_WX_RAIN) != 0, snow = (bits & PORT_WX_SNOW) != 0;
    const bool night = (bits & PORT_WX_NIGHT) != 0 || tier == PORT_WX_TIER_STORM;  // r196: a storm has the lights on
    float bank[kMaxBanks][3];
    int nb = 0;
    if (night)
    {
        std::lock_guard<std::mutex> lock(s_lightLock);
        nb = s_bankCount;
        memcpy(bank, s_bank, sizeof(float) * 3 * nb);
    }
    const float splashK = tier == PORT_WX_TIER_DRIZZLE ? 0.45f : (tier == PORT_WX_TIER_STORM ? 1.0f : 0.8f);
    int splashes = rain ? (int)(kSplashes * load * splashK) : 0;
    splashes = rain && splashes < 12 ? 12 : splashes;
    const int drifts = 0;  // r214: (the snow lying: pitch_snow)
    (void)snow;
    (void)cover;
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
        flat_quad(b, 0.0f, 3.0f, 0.04f, 26.0f, 15.0f, kDot, 200, 215, 235,
                  fade * (night ? 0.05f : 0.07f) * (tier == PORT_WX_TIER_DRIZZLE ? 0.6f : (tier == PORT_WX_TIER_STORM ? 1.5f : 1.0f)));
    // r214: the snow lying is pitch_snow's (its own texture, drawn after this batch)
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

Grade grade_for(int bits, int tier, float inten, float gust, float fade)
{
    Grade g;
    const bool night = (bits & PORT_WX_NIGHT) != 0, rain = (bits & PORT_WX_RAIN) != 0, snow = (bits & PORT_WX_SNOW) != 0;
    const bool storm = tier == PORT_WX_TIER_STORM;
    g.stad = night || storm;
    g.all = rain;
    g.haze = rain || snow;
    set3(g.stadTop, 0.22f, 0.25f, 0.38f);  // the sky and the far stands dark
    set3(g.stadBot, 0.56f, 0.59f, 0.72f);  // the pitch in front still readable (+ the floodlit pools)
    if (storm && !night)
    {
        // r196: a THUNDERSTORM by day: the storm's dark sky (the lights on), not quite night
        set3(g.stadTop, 0.34f, 0.37f, 0.48f);
        set3(g.stadBot, 0.66f, 0.69f, 0.80f);
    }
    set3(g.allTop, 0.76f, 0.80f, 0.89f);
    set3(g.allBot, 0.84f, 0.87f, 0.94f);
    if (tier == PORT_WX_TIER_DRIZZLE)
    {
        set3(g.allTop, 0.86f, 0.89f, 0.94f);
        set3(g.allBot, 0.92f, 0.94f, 0.97f);
    }
    set3(g.hazeTop, 0.0f, 0.0f, 0.0f);
    set3(g.hazeBot, 0.0f, 0.0f, 0.0f);
    if (rain)
    {
        const float h = (night ? 0.35f : 1.0f) * (tier == PORT_WX_TIER_DRIZZLE ? 0.6f : (storm ? 1.3f : 1.0f));
        set3(g.hazeTop, 0.050f * h, 0.056f * h, 0.068f * h);
        set3(g.hazeBot, 0.012f * h, 0.014f * h, 0.018f * h);
    }
    if (snow)
    {
        // r196: the blizzard's whiteout rides its gusts (light: one add pass, never a fog sheet that hides the play)
        const float w = tier == PORT_WX_TIER_FLURRY ? 0.55f : (0.55f + 1.1f * inten * (0.45f + 0.55f * gust));
        const float h = (night ? 0.4f : 1.0f) * w;
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

namespace
{
// r196: a (new) weather's tier and everything that runs on its clock: the clock, the strikes, the snow on the pitch
void start_tier(int bits, int mode, Dice& d)
{
    const int tier = pick_tier(bits, mode, d);
    const uint32_t seed = d.next() | 1u;
    s_tier.store(tier);
    s_seed.store(seed);
    s_clock = 0.0f;
    s_strikeN = 0;
    s_nextStrike = 0;
    s_nextStrikeT = 6.0f + 4.0f * h01(seed, 0u, 0x57A0u);
    s_flash.store(0.0f);
    {
        std::lock_guard<std::mutex> lock(s_boltLock);
        s_boltId = -1;
        s_boltVis = 0.0f;
    }
    {
        std::lock_guard<std::mutex> lock(s_snowLock);
        memset(s_snowD, 0, sizeof(s_snowD));
        memset(s_snowS, 0, sizeof(s_snowS));
        s_snowAny = false;
    }
    fprintf(stderr, "[weather] r196 tier %s (seed %08x%s)\n", kTierNames[tier < 0 || tier > 5 ? 0 : tier], seed,
            d.shared ? ", shared online" : "");
    fflush(stderr);
}
}  // namespace

extern "C" void PortWeatherSetMode(int mode)
{
    if (mode < 0 || mode >= PORT_WX_MODES)
        mode = PORT_WX_MODE_RANDOM;
    const int was = PortWeatherMode();
    s_mode.store(mode);
    if (was == mode || s_stadium < 0)
        return;
    // r196: online the match keeps the weather both headsets agreed on (the setting counts from the next match)
    if (PortNetOnline() != 0)
        return;
    // the match on now follows at once: a franchise match keeps its calendar (unless OFF), a quick one takes the new
    // pick (RANDOM keeps a random pick it already had)
    Dice d = {false, {0}};
    if (mode == PORT_WX_MODE_OFF)
        apply(0, "setting: OFF");
    else if (s_franchiseBits >= 0)
        apply(s_franchiseBits, "setting changed: franchise calendar");
    else if (mode == PORT_WX_MODE_RANDOM && s_quickRandom)
        return;
    else
    {
        s_quickRandom = mode == PORT_WX_MODE_RANDOM;
        apply(wxcal::filter(quick_pick(mode, d), stadium_climate(s_stadium)), "setting changed");
    }
    start_tier(PortWeatherCurrent(), mode, d);
}

extern "C" void PortWeatherSetShared(int on, unsigned seed, int hostMode)
{
    s_sharedSeed.store(seed);
    s_sharedMode.store(hostMode < 0 || hostMode >= PORT_WX_MODES ? PORT_WX_MODE_RANDOM : hostMode);
    s_sharedOn.store(on != 0);
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
    // r196: online, both headsets roll the same dice (the match's shared seed) on the host's setting: the same weather,
    // tier, wind and strikes on both (the franchise calendar is the shared career's on both already)
    const bool shared = s_sharedOn.load() && PortNetOnline() != 0;
    Dice d = {shared, {wxcal::hash3(s_sharedSeed.load(), 0x57EA7E12u, (uint32_t)stadium)}};
    const int mode = shared ? s_sharedMode.load() : PortWeatherMode();
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
        bits = wxcal::filter(quick_pick(mode, d), stadium_climate(stadium));
        why = shared ? "online, the host's setting + the match's seed"
                     : (mode == PORT_WX_MODE_RANDOM ? "quick match, random" : "quick match, set");
    }
    s_bits.store(-1);  // so apply() logs and fades in afresh
    apply(bits, why);
    start_tier(bits, mode, d);
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
    // r198 (Lance: "it can remain 'snow game' or 'rain game'"): the hub's, the magazine's and the match card's words
    const bool night = (bits & PORT_WX_NIGHT) != 0;
    if (bits & PORT_WX_SNOW)
        return night ? "SNOW NIGHT GAME" : "SNOW GAME";
    if (bits & PORT_WX_RAIN)
        return night ? "RAIN NIGHT GAME" : "RAIN GAME";
    return night ? "NIGHT GAME" : "";
}

extern "C" int PortWeatherTier(void) { return PortWeatherCurrent() != 0 ? s_tier.load() : 0; }
extern "C" float PortWeatherIntensity(void) { return PortWeatherCurrent() != 0 ? s_inten.load() : 0.0f; }

// r198 (Lance: "it doesn't need to be specified"): no tier names on the card any more - RAIN GAME / SNOW GAME /
// NIGHT GAME, whatever the tier (the pause row's DRIZZLE ... BLIZZARD stay, as a manual pick for testing)
extern "C" const char* PortWeatherMatchLabel(void) { return PortWeatherLabel(PortWeatherCurrent()); }

extern "C" void PortWeatherWind(float* vx, float* vy, float* gust)
{
    const bool on = PortWeatherCurrent() != 0;
    *vx = on ? s_windX.load() : 0.0f;
    *vy = on ? s_windY.load() : 0.0f;
    *gust = on ? s_gust.load() : 0.0f;
}

// (stale after a quarter second without a game frame: a paused / gone match never holds a flash)
static bool wx_live() { return mono_now_ns() - s_renderNs.load() < 250000000LL; }

extern "C" float PortWeatherFlash(void) { return PortWeatherCurrent() != 0 && wx_live() ? s_flash.load() : 0.0f; }

extern "C" int PortWeatherBolt(int* id, float* vis, int* kind, float* x, unsigned* seed)
{
    if (PortWeatherCurrent() == 0 || !wx_live())
        return 0;
    std::lock_guard<std::mutex> lock(s_boltLock);
    if (s_boltId < 0 || s_boltVis <= 0.0f)
        return 0;
    *id = s_boltId;
    *vis = s_boltVis;
    *kind = s_boltKind;
    *x = s_boltX;
    *seed = wxcal::hash3(s_seed.load(), (uint32_t)s_boltId, 0xB0u);
    return 1;
}

extern "C" int PortWeatherBoltShape(float* xyw, int max)
{
    // the bolt showing's segments (x0 y0 x1 y1 w each), for the life-size stadium's sky; returns the count
    int id, kind;
    float vis, x;
    unsigned seed;
    if (!PortWeatherBolt(&id, &vis, &kind, &x, &seed))
        return 0;
    static BoltSeg seg[kBoltSegs];
    const int n = bolt_shape(seed, kind, seg, kBoltSegs < max ? kBoltSegs : max);
    for (int i = 0; i < n; i++)
    {
        xyw[5 * i] = seg[i].x0;
        xyw[5 * i + 1] = seg[i].y0;
        xyw[5 * i + 2] = seg[i].x1;
        xyw[5 * i + 3] = seg[i].y1;
        xyw[5 * i + 4] = seg[i].w;
    }
    return n;
}

extern "C" int PortWeatherSnowGrid(float* out, int max, int* cols, int* rows)
{
    // r214: the life-size stadium keeps r196's 32 x 18 (the fine 48 x 28 grid sampled at its cells' centres)
    *cols = 32;
    *rows = 18;
    if ((PortWeatherCurrent() & PORT_WX_SNOW) == 0 || max < 32 * 18)
        return 0;
    std::lock_guard<std::mutex> lock(s_snowLock);
    if (!s_snowAny)
        return 0;
    xrsnow::pitch_export32(s_snowD, out);
    return 32 * 18;
}

extern "C" void PortWeatherSetListener(float rightX, float rightZ, float worldYaw)
{
    s_listenRX = rightX;
    s_listenRZ = rightZ;
    s_listenYaw = worldYaw;
}

namespace
{
// r196: the tier's clock, wind, strikes + thunder, the beds' levels and the snow on the pitch - once a game frame
void tier_step(int bits, float dt, float fade)
{
    const int tier = s_tier.load();
    const uint32_t seed = s_seed.load();
    s_clock += dt;
    const float t = s_clock;
    float vx, vy;
    tier_wind(tier, seed, t, s_inten.load(), &vx, &vy);
    const float speed = sqrtf(vx * vx + vy * vy);
    const float gust = tier == PORT_WX_TIER_BLIZZARD ? (speed / 6.8f > 1.0f ? 1.0f : speed / 6.8f)
                                                     : (speed - 1.0f) / 3.5f < 0.0f ? 0.0f : ((speed - 1.0f) / 3.5f > 1.0f ? 1.0f : (speed - 1.0f) / 3.5f);
    const float inten = tier_intensity(tier, t, gust);
    s_windX.store(vx);
    s_windY.store(vy);
    s_gust.store(gust);
    s_inten.store(inten);

    // lightning
    float flash = 0.0f;
    if (tier == PORT_WX_TIER_STORM)
    {
        while (t >= s_nextStrikeT)
        {
            const Strike st = make_strike(seed, s_nextStrike, s_nextStrikeT);
            if (s_strikeN == kStrikesLive)
            {
                memmove(&s_strike[0], &s_strike[1], sizeof(Strike) * (kStrikesLive - 1));
                s_strikeN--;
            }
            s_strike[s_strikeN++] = st;
            s_nextStrike++;
            s_nextStrikeT += 7.0f + 11.0f * h01(seed, (uint32_t)s_nextStrike, 0x57A9u);
        }
        int boltI = -1;
        float boltV = 0.0f;
        for (int i = 0; i < s_strikeN; i++)
        {
            Strike& st = s_strike[i];
            flash += strike_flash(st, t);
            const float bv = strike_bolt(st, t);
            if (bv > boltV)
            {
                boltV = bv;
                boltI = i;
            }
            if (!st.thundered && t >= st.t + st.delay)
            {
                st.thundered = true;
                // the thunder: which clip (2 of each kind, the seed's pick), how loud, panned toward the bolt's side
                if (t < st.t + st.delay + 1.0f)  // (never a pile of old ones after a long pause)
                {
                    ThunderEvent e;
                    e.clip = st.kind * 2 + (int)(wxcal::hash3(seed, (uint32_t)st.id, 0x7DEu) & 1u);
                    e.gain = st.kind == 2 ? 1.0f : (st.kind == 1 ? 0.85f : 0.75f);
                    e.pan = st.x * (st.kind == 2 ? 0.25f : 0.55f);
                    std::lock_guard<std::mutex> lock(s_thunderLock);
                    if (s_thunderQN < 4)
                        s_thunderQ[s_thunderQN++] = e;
                    // a close boom: a light rumble in your hands (the crack and the boom), never a shake
                    if (st.kind == 2 && !cPlatPad::m_bDisableRumble)
                        PortXrPulseBuzz(0.32f, 0.28f);
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(s_boltLock);
            if (boltI >= 0)
            {
                s_boltId = s_strike[boltI].id;
                s_boltKind = s_strike[boltI].kind;
                s_boltX = s_strike[boltI].x;
            }
            s_boltVis = boltV * fade;
        }
    }
    flash = flash > 1.3f ? 1.3f : flash;
    s_flash.store(flash * fade);

    // the beds (targets; the mixer glides to them): rain by tier, the wind's breeze / howl with the gusts, panned the
    // way the wind blows across your head (game x turned into your room by worldYaw)
    float g[kBeds] = {0.0f, 0.0f, 0.0f, 0.0f};
    switch (tier)
    {
    case PORT_WX_TIER_DRIZZLE:
        g[kBedRainLight] = 0.85f;
        g[kBedBreeze] = 0.25f;
        break;
    case PORT_WX_TIER_RAIN:
        g[kBedRainLight] = 0.35f;
        g[kBedRainHeavy] = 0.65f;
        g[kBedBreeze] = 0.35f;
        break;
    case PORT_WX_TIER_STORM:
        g[kBedRainHeavy] = 0.85f + 0.15f * gust;
        g[kBedRainLight] = 0.3f;
        g[kBedBreeze] = 0.35f + 0.5f * gust;
        g[kBedHowl] = 0.12f * gust;
        break;
    case PORT_WX_TIER_FLURRY:
        g[kBedBreeze] = 0.6f;
        break;
    case PORT_WX_TIER_BLIZZARD:
        g[kBedBreeze] = 0.7f * (1.0f - inten) + 0.25f;
        g[kBedHowl] = inten * (0.18f + 0.95f * powf(gust, 1.3f));
        break;
    default:
        break;
    }
    const float yaw = s_listenYaw;
    const float rx = vx * cosf(yaw) - vy * sinf(yaw);  // (vx, 0, -vy) turned by yaw about +Y
    const float rz = -vx * sinf(yaw) - vy * cosf(yaw);
    float pan = speed > 0.05f ? (rx * s_listenRX + rz * s_listenRZ) / 6.0f : 0.0f;
    pan = pan < -0.75f ? -0.75f : (pan > 0.75f ? 0.75f : pan);
    for (int i = 0; i < kBeds; i++)
    {
        s_bedGain[i].store(g[i] * fade);
        s_bedPan[i].store(i >= kBedBreeze ? pan : pan * 0.3f);
    }

    // the snow on the pitch
    if (bits & PORT_WX_SNOW)
    {
        float pos[11][3];
        int np = 0;
        // (the same guard as the debug overlay's snapshot, Game.cpp PortDebugMatch: a match's teams and ball exist)
        if (g_pGame != NULL && g_pTeams[0] != NULL && g_pTeams[1] != NULL && g_pBall != NULL)
        {
            for (int i = 0; i < 10; i++)
                if (g_pCharacters[i] != NULL)
                {
                    const nlVector3& p = g_pCharacters[i]->GetPosition();
                    pos[np][0] = p.x;
                    pos[np][1] = p.y;
                    pos[np][2] = 0.75f;  // a player's feet (r214: 0.75 m on the finer grid; r196 1 m)
                    np++;
                }
            if (g_pBall != NULL)
            {
                const nlVector3& p = g_pBall->GetPosition();
                if (p.z < 0.6f)  // the ball on the ground (in the air it leaves the snow be)
                {
                    pos[np][0] = p.x;
                    pos[np][1] = p.y;
                    pos[np][2] = 0.45f;  // (r214; r196 0.6 m)
                    np++;
                }
            }
        }
        // r214: xr_snow.h pitch_step (r196's rule on a 48 x 28 grid: + behind the goals and the downwind boards; a slush
        // left where they ran that fades in 30 s)
        std::lock_guard<std::mutex> lock(s_snowLock);
        const bool any = xrsnow::pitch_step(s_snowD, s_snowS, pos, np, dt, tier == PORT_WX_TIER_FLURRY, inten, vx, vy);
        s_snowAny = any;
    }
}
}  // namespace

extern "C" void PortWeatherAudioMix(int* left, int* right, int frames)
{
    static float gainNow[kBeds] = {0, 0, 0, 0}, panNow[kBeds] = {0, 0, 0, 0};
    static size_t bedPos[kBeds] = {0, 0, 0, 0};
    struct Voice
    {
        int clip;
        size_t pos;
        float gl, gr;
    };
    static Voice thunder[3] = {{-1, 0, 0, 0}, {-1, 0, 0, 0}, {-1, 0, 0, 0}};
    if (s_soundState.load() == 0)
    {
        // nothing to load before the first weather
        if (PortWeatherCurrent() & (PORT_WX_RAIN | PORT_WX_SNOW))
        {
            for (int i = 0; i < kBeds; i++)
            {
                s_bedGain[i].store(0.0f);
                s_bedPan[i].store(0.0f);
            }
            load_sounds();
        }
        return;
    }
    if (s_soundState.load() != 2)
        return;
    // the targets: off when no match frame has come for a quarter second (menus, a finished match), down to a third
    // while paused (the rain doesn't stop because you paused - it just goes quiet)
    const bool live = wx_live() && (PortWeatherCurrent() & (PORT_WX_RAIN | PORT_WX_SNOW)) != 0;
    const float vol = Audio::MasterVolume::GetVolume(Audio::MasterVolume::VG_SFX) * (Audio::gbGameIsPaused ? 0.35f : 1.0f);
    {
        std::lock_guard<std::mutex> lock(s_thunderLock);
        for (int q = 0; q < s_thunderQN; q++)
        {
            const ThunderEvent& e = s_thunderQ[q];
            if (e.clip < 0 || e.clip >= 6 || s_thunder[e.clip].lr.empty())
                continue;
            int slot = 0;
            for (int k = 0; k < 3; k++)
                if (thunder[k].clip < 0)
                {
                    slot = k;
                    break;
                }
            const float gn = e.gain * vol;
            thunder[slot].clip = e.clip;
            thunder[slot].pos = 0;
            thunder[slot].gl = gn * sqrtf(0.5f * (1.0f - e.pan)) * 1.414f;
            thunder[slot].gr = gn * sqrtf(0.5f * (1.0f + e.pan)) * 1.414f;
        }
        s_thunderQN = 0;
    }
    const float glide = 1.0f / (0.25f * 32000.0f);  // ~0.25 s to a new level: never a click
    for (int b = 0; b < kBeds; b++)
    {
        const std::vector<short>& pcm = s_bed[b].lr;
        const float want = live ? s_bedGain[b].load() * vol : 0.0f;
        const float wantPan = s_bedPan[b].load();
        if (pcm.size() < 4 || (want <= 0.0f && gainNow[b] <= 1e-4f))
        {
            gainNow[b] = 0.0f;
            continue;
        }
        const size_t n = pcm.size() / 2;
        for (int f = 0; f < frames; f++)
        {
            gainNow[b] += (want - gainNow[b]) * glide * 4.0f;
            panNow[b] += (wantPan - panNow[b]) * glide;
            const float gl = gainNow[b] * sqrtf(0.5f * (1.0f - panNow[b])) * 1.414f;
            const float gr = gainNow[b] * sqrtf(0.5f * (1.0f + panNow[b])) * 1.414f;
            const size_t p = bedPos[b];
            left[f] += (int)(pcm[2 * p] * gl);
            right[f] += (int)(pcm[2 * p + 1] * gr);
            bedPos[b] = p + 1 >= n ? 0 : p + 1;
        }
    }
    for (int k = 0; k < 3; k++)
    {
        Voice& v = thunder[k];
        if (v.clip < 0)
            continue;
        const std::vector<short>& pcm = s_thunder[v.clip].lr;
        const float off = live || Audio::gbGameIsPaused ? 1.0f : 0.0f;
        for (int f = 0; f < frames && 2 * v.pos + 1 < pcm.size(); f++, v.pos++)
        {
            left[f] += (int)(pcm[2 * v.pos] * v.gl * off);
            right[f] += (int)(pcm[2 * v.pos + 1] * v.gr * off);
        }
        if (2 * v.pos + 1 >= pcm.size() || off == 0.0f)
            v.clip = -1;
    }
}

extern "C" void PortWeatherRenderGame(float dt)
{
    const int bits = PortWeatherCurrent();
    if (bits == 0)
        return;
    if (dt < 0.0f || dt > 0.1f)
        dt = dt < 0.0f ? 0.0f : 0.1f;
    s_renderNs.store(mono_now_ns());
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
    tier_step(bits, dt, fade);  // r196
    if (!atlas_ready())
        return;
    const int tier = s_tier.load();
    const float inten = s_inten.load(), gust = s_gust.load(), flash = s_flash.load();
    const float windW[2] = {s_windX.load(), s_windY.load()};
    if (!s_lightsFound && ((bits & PORT_WX_NIGHT) != 0 || tier == PORT_WX_TIER_STORM) &&
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
    pitch_dressing(bits, tier, fade, dt, cover, load);
    if (bits & PORT_WX_SNOW)
        pitch_snow(bits, fade, view, dt);  // r214: the snow lying (its own texture), over the pools / sheen
    // the broadcast picture only, and not while it is the Tabletop's HUD-only scoreboard
    if (PortXrHudOnlyNormalView() == 0)
    {
        const Grade g = grade_for(bits, tier, inten, gust, fade);
        if (g.stad)
            screen_quad(GLV_BigBlackPolygon, 4, g.stadTop, g.stadBot);  // before the players
        if (g.all)
            screen_quad(GLV_UnsortedOrtho, 4, g.allTop, g.allBot);  // after them, before the HUD
        if (g.haze)
            screen_quad(GLV_UnsortedOrtho, 2, g.hazeTop, g.hazeBot);
        const Cam c = camera_proj();
        if ((bits & PORT_WX_NIGHT) || tier == PORT_WX_TIER_STORM)
            camera_lights(fade, view, c, (bits & (PORT_WX_RAIN | PORT_WX_SNOW)) != 0);
        camera_weather(bits, tier, inten, fade, dt, load, windW, gust, flash, view, c);
        if (flash > 0.01f)
        {
            // r196: the lightning lights the whole picture - the sky most, the pitch and the players too (one add
            // pass, only while it flashes)
            const float f = flash > 1.0f ? 1.0f : flash;
            const float top[3] = {0.50f * f, 0.54f * f, 0.70f * f}, bot[3] = {0.26f * f, 0.28f * f, 0.36f * f};
            screen_quad(GLV_UnsortedOrtho, 2, top, bot);
        }
        int bid, bkind;
        float bvis, bx;
        unsigned bseed;
        if (tier == PORT_WX_TIER_STORM && PortWeatherBolt(&bid, &bvis, &bkind, &bx, &bseed))
            camera_bolt(bseed, bkind, bx, bvis, c);
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
    const int tier = s_tier.load();
    if ((bits & PORT_WX_SNOW) && stadium && tier == PORT_WX_TIER_BLIZZARD)
    {
        // r196: the blizzard's whiteout round you, riding the gusts (the compositor's bias: free)
        const float w = s_inten.load() * (0.4f + 0.6f * s_gust.load());
        add3(b, 0.07f * w, 0.075f * w, 0.085f * w);
        mul3(s, 1.0f - 0.10f * w, 1.0f - 0.10f * w, 1.0f - 0.08f * w);
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
    else if (tier == PORT_WX_TIER_STORM)
    {
        // r196: a storm by day: the dark sky (not quite night)
        if (stadium)
            mul3(s, 0.62f, 0.66f, 0.78f);
        else
            mul3(s, 0.85f, 0.87f, 0.93f);
    }
    // r196: the lightning lights the stadium round you / the field (the flash, blue-white)
    const float fl = PortWeatherFlash();
    if (fl > 0.0f)
    {
        const float f = fl > 1.0f ? 1.0f : fl;
        const float k = stadium ? 1.1f : (kind == 1 ? 0.7f : 0.55f);
        mul3(s, 1.0f + k * f, 1.0f + k * 1.05f * f, 1.0f + k * 1.25f * f);
        if (stadium)
            add3(b, 0.06f * f, 0.07f * f, 0.11f * f);
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
