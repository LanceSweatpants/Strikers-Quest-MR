// See xr_balls.h. Built with xr_quest.cpp in strikers_xr (C++20, no port prelude). strikers-quest r168.
//
// The pieces are this file's own (not the celebration's, not the goal explosions'): at most kMaxParts, quads drawn
// with the effects' shaders (xr_fx.frag), after the goal explosions' (xr_fx.cpp reserves kBallFxParts). Their shapes:
// a fog puff (29: the glows, the flame puffs, the clouds), a flame (76: the tongues), a spark (13: the embers), a glowing
// ring (32: the fire ring, the atmosphere's rim, the ripples), a wisp (73: the atmosphere trail), a drop (70: the
// splash), a meteor's crater (156: the scorch), a shockwave (148: the ripple's front), and the preview's ball itself
// (160 FIREBALL / 161 PLANET EARTH: a sphere drawn per pixel from ball_shade.h's twin in xr_fx.frag).

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#define VK_USE_PLATFORM_ANDROID_KHR 1
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include "xr_balls.h"

#include "xr_fx.h"
#include "ball_shade.h"
#include "xr_home.h"
#include "xr_net.hpp"
#include "../../include/port/ball_skin.h"

#include <android/log.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define BLOG(...) __android_log_print(ANDROID_LOG_INFO, "strikers_xr", __VA_ARGS__)

namespace xrballs {
namespace {

// ------------------------------------------------------------------------------------------------------------------
// Little vector kit
// ------------------------------------------------------------------------------------------------------------------
struct V3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
};
V3 operator+(V3 a, V3 b) { return V3{a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return V3{a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return V3{a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return V3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(V3 a) { return std::sqrt(dot(a, a)); }
V3 norm(V3 a, V3 fallback = V3{0.0f, 1.0f, 0.0f}) {
  const float l = len(a);
  return l > 1e-6f ? a * (1.0f / l) : fallback;
}
V3 v3(const XrVector3f& p) { return V3{p.x, p.y, p.z}; }
float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
V3 qrot(const XrQuaternionf& q, V3 v) {
  const V3 u{q.x, q.y, q.z};
  const V3 t = cross(u, v) * 2.0f;
  return v + t * q.w + cross(u, t);
}
void quat_from_basis(V3 x, V3 y, V3 z, float out[4]) {
  const float tr = x.x + y.y + z.z;
  float qw, qx, qy, qz;
  if (tr > 0.0f) {
    const float s = std::sqrt(tr + 1.0f) * 2.0f;
    qw = 0.25f * s, qx = (y.z - z.y) / s, qy = (z.x - x.z) / s, qz = (x.y - y.x) / s;
  } else if (x.x > y.y && x.x > z.z) {
    const float s = std::sqrt(1.0f + x.x - y.y - z.z) * 2.0f;
    qw = (y.z - z.y) / s, qx = 0.25f * s, qy = (y.x + x.y) / s, qz = (z.x + x.z) / s;
  } else if (y.y > z.z) {
    const float s = std::sqrt(1.0f + y.y - x.x - z.z) * 2.0f;
    qw = (z.x - x.z) / s, qx = (y.x + x.y) / s, qy = 0.25f * s, qz = (z.y + y.z) / s;
  } else {
    const float s = std::sqrt(1.0f + z.z - x.x - y.y) * 2.0f;
    qw = (x.y - y.x) / s, qx = (z.x + x.z) / s, qy = (z.y + y.z) / s, qz = 0.25f * s;
  }
  const float l = std::sqrt(qw * qw + qx * qx + qy * qy + qz * qz);
  out[0] = qx / l, out[1] = qy / l, out[2] = qz / l, out[3] = qw / l;
}
// a quad whose length (its +Y) runs along `along` and whose face turns to `eye`
void q_along(V3 p, V3 along, V3 eye, float out[4]) {
  const V3 y = norm(along);
  V3 z = eye - p;
  z = norm(z - y * dot(z, y), V3{0.0f, 0.0f, 1.0f});
  quat_from_basis(norm(cross(y, z)), y, z, out);
}
// a quad lying flat, its face along `up`, turned `roll`
void q_flat(V3 up, float roll, float out[4]) {
  const V3 z = norm(up);
  V3 a = std::fabs(z.y) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f};
  V3 x = norm(cross(a, z));
  V3 y = cross(z, x);
  const float c = std::cos(roll), s = std::sin(roll);
  const V3 xr = x * c + y * s, yr = y * c - x * s;
  quat_from_basis(xr, yr, z, out);
}
uint32_t pack(uint32_t rgb, uint8_t a) {
  return ((rgb >> 16) & 0xFF) | (((rgb >> 8) & 0xFF) << 8) | ((rgb & 0xFF) << 16) | (static_cast<uint32_t>(a) << 24);
}
struct Rng {
  uint32_t x = 0x2545F491u;
  uint32_t next() {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
  }
  float f(float a, float b) { return a + (b - a) * static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
  V3 unit() {
    const float z = f(-1.0f, 1.0f), a = f(0.0f, 6.2831853f), r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return V3{r * std::cos(a), r * std::sin(a), z};
  }
};
uint64_t mono_ns() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ------------------------------------------------------------------------------------------------------------------
// The catalogue, owned / on
// ------------------------------------------------------------------------------------------------------------------
const std::vector<Item>& cat() {
  static const std::vector<Item> k = {
      {"classic", "CLASSIC", "THE GAME'S OWN MATCH BALL", 0, 0, 0xF2F2F2, 0x2A2A30},
      {"fireball", "FIREBALL", "A BALL OF LIVING FIRE - FLAMES LICK OFF IT, EMBERS TRAIL IT, IT SCORCHES WHERE IT LANDS", 600, 3,
       0xFF7A1A, 0x7A1004},
      {"earth", "PLANET EARTH", "THE WHOLE WORLD AT YOUR FEET - IT SPINS, ITS CLOUDS DRIFT, ITS CITIES LIGHT UP", 1200, 4, 0x2F7BFF,
       0x1E9E4A},
      // r170: the other 21 (ball_shade.h skin_defs() - the same order). Prices by kind: sports 300 (RARE), elemental /
      // retro / weird 600 (EPIC), space 1200 (LEGENDARY), flex 2500 (MYTHIC)
      {"snowball", "SNOWBALL", "PACKED SNOW THAT GLINTS - POWDER TRAILS IT, IT BURSTS INTO SNOW", 600, 3, 0xEAF6FF, 0x7AB8E8},
      {"lava", "LAVA ROCK", "BASALT WITH LAVA FLOWING IN ITS CRACKS - FIRE, EMBERS, SMOKE", 600, 3, 0xFF5A10, 0x241A16},
      {"thunder", "THUNDER BALL", "A STORM IN A BALL - LIGHTNING CRACKLES OFF IT", 600, 3, 0x7FD8FF, 0x1A1E44},
      {"galaxy", "GALAXY", "A SPIRAL GALAXY TURNING - STARS ORBIT IT, NEBULAE TRAIL IT", 1200, 4, 0x9A6BFF, 0x14082E},
      {"moon", "MOON", "CRATERED MOON ROCK - MOON DUST WHERE IT LANDS", 1200, 4, 0xDDE4F0, 0x5A5E68},
      {"pixel", "8-BIT PIXEL", "CHUNKY PIXELS IN CYCLING COLOURS - IT STEPS IN PIXELS", 600, 3, 0xFFD800, 0xE52521},
      {"neon", "NEON WIREFRAME", "A GRID OF NEON TUBES, A PULSE RACING ROUND IT", 600, 3, 0xFF2AD8, 0x00F0FF},
      {"disco", "DISCO BALL", "MIRROR TILES FLASHING - SPARKLES EVERYWHERE IT GOES", 600, 3, 0xE8ECF5, 0xFF7AD8},
      {"basketball", "BASKETBALL", "PEBBLED LEATHER AND BLACK CHANNELS - SWISH", 300, 2, 0xE06A1A, 0x2A140A},
      {"eightball", "8-BALL", "GLOSSY BLACK, ITS WHITE 8 - CHALK DUST ON EVERY HIT", 300, 2, 0x1A1A1E, 0xF2F2F2},
      {"bowling", "BOWLING BALL", "SWIRLED RESIN, THREE FINGER HOLES - A STRIKE ON EVERY HIT", 300, 2, 0x3A2AD8, 0x10062E},
      {"leather", "OLD LEATHER BALL", "WORN STITCHED LEATHER FROM THE OLD DAYS", 300, 2, 0x8A5A2E, 0x3E2414},
      {"gold", "GOLD TROPHY BALL", "POLISHED GOLD, A SHEEN SWEEPING OVER IT - GLITTER TRAIL", 2500, 5, 0xFFD34A, 0x8A6A14},
      {"diamond", "DIAMOND BALL", "CUT FACETS FLASHING RAINBOW FIRE", 2500, 5, 0xBFE8FF, 0x5A8AC8},
      {"chrome", "CHROME TIZM", "A MIRROR BALL - THE WORLD IN ITS CHROME", 2500, 5, 0xE8ECF5, 0x6A707A},
      {"black11", "BLACK 1/1", "MATTE BLACK, GOLD FOIL 1/1, A HOLO SWIRL - BLACK SMOKE", 2500, 5, 0x1C1C20, 0xC9A227},
      {"eyeball", "EYEBALL", "IT LOOKS AROUND - ITS PUPIL BREATHES", 600, 3, 0xF2EEE8, 0x2A6A8A},
      {"bomb", "BOB-OMB BALL", "ITS FUSE BURNS - IT BLOWS UP ON EVERY HIT", 600, 3, 0x1A1A20, 0xFF3A1A},
      {"cannon", "CANNONBALL", "CAST IRON - FIRED IN A BLAST, SMOKE BEHIND IT", 600, 3, 0x2A2A2E, 0x6A3A1A},
      {"duck", "RUBBER DUCK", "SQUEAKY YELLOW - BUBBLES AND SPLASHES", 600, 3, 0xFFD820, 0xFF7A10},
      {"blackhole", "BLACK HOLE", "A HOT DISC SWIRLING ROUND ITS DARK - IT PULLS EVERYTHING IN", 1200, 4, 0x9A4BFF, 0x0A0414},
  };
  return k;
}

std::mutex g_mutex;
bool g_fileRead = false, g_cardsRead = false;
std::vector<std::string> g_owned;
std::string g_on = "classic";
std::string g_friend;
std::atomic<int> g_gen{1};

std::string home_dir() {
  const char* home = std::getenv("STRIKERS_HOME");
  return home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers";
}
void line_l(const char* key, const char* val) {
  if (std::strcmp(key, "ball") == 0 && find(val) >= 0) g_on = val;
  if (std::strcmp(key, "ball_owned") == 0 && find(val) > 0 &&
      std::find(g_owned.begin(), g_owned.end(), std::string(val)) == g_owned.end())
    g_owned.push_back(val);
}
// (until the cards have read looks.txt: read it here, read-only, so a match before the hub still wears yours)
void read_l() {
  if (g_fileRead || g_cardsRead) return;
  g_fileRead = true;
  FILE* f = std::fopen((home_dir() + "/cards/looks.txt").c_str(), "r");
  if (f == nullptr) return;
  char line[160];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    char key[24] = {}, val[48] = {};
    if (std::sscanf(line, "%23s %47s", key, val) == 2) line_l(key, val);
  }
  std::fclose(f);
}
bool owned_l(const char* id) {
  const int i = find(id);
  if (i < 0) return false;
  if (cat()[static_cast<size_t>(i)].price == 0) return true;
  return std::find(g_owned.begin(), g_owned.end(), std::string(id)) != g_owned.end();
}
int skin_index(const std::string& id) {  // -> include/port/ball_skin.h's PORT_BALL_* (r170: the catalogue's place)
  const int i = find(id.c_str());
  return i > 0 && i < PORT_BALL_COUNT ? i : PORT_BALL_CLASSIC;
}

// ------------------------------------------------------------------------------------------------------------------
// Pieces
// ------------------------------------------------------------------------------------------------------------------
constexpr int kShSpark = 13, kShPuff = 29, kShRing = 32, kShDrop = 70, kShWisp = 73, kShFlame = 76, kShWave = 148,
              kShCrater = 156, kShBallFire = 160, kShBallEarth = 161;
enum Orient : uint8_t { kFace, kUpright, kAlong, kFlat, kScreen, kAxis };  // (r169: kAxis - its length along P::axis)
struct P {
  V3 p, v;
  float size = 0.01f, size1 = 0.01f;   // its size at birth / at the end of its life
  float age = 0.0f, life = 0.5f, fadeIn = 0.02f, fadeOut = 0.2f, peak = 1.0f, glow = 1.0f;
  float drag = 0.0f, grav = 0.0f;      // 1/s, m/s^2 (along -up)
  float aspect = 1.0f, roll = 0.0f;
  V3 up{0.0f, 1.0f, 0.0f};
  uint32_t c1 = 0xFFFFFF, c2 = 0xFFFFFF;
  uint8_t c2a = 255;
  int shape = kShSpark;
  Orient orient = kFace;
  bool ringFade = false;  // fades over its whole life
  bool heat = false;      // a scorch: its glow (colour 2's alpha) cools over its life
  float floorY = -1e9f;   // embers / drops die where they land (room Y), -1e9 none
  // r169: a real-fire flipbook flame (shape 163): its temperature and smoke over its life (start -> end), how bright,
  // its first frame (+16 mirrored) and frames a second
  float temp0 = 1.0f, temp1 = 1.0f, smoke0 = 0.2f, smoke1 = 0.2f, bright = 0.5f, fps = 18.0f;
  int frame0 = 0;
  V3 axis{0.0f, 1.0f, 0.0f};  // (kAxis)
};
constexpr int kShFire = 163;  // r169
constexpr uint32_t kMaxParts = 400;  // xr_fx.cpp reserves this many instances (kBallFxParts)
std::vector<P> g_parts;
Rng g_rng;

// Where the ball is this display frame, in this view
struct Anchor {
  bool ok = false;
  int mode = 0;        // 0 the Tabletop field, 1 the life-size stadium, 2 the big screen's picture, 3 the preview
  V3 c;                // its middle (LOCAL)
  float r = 0.01f;     // its radius (m)
  V3 vel;              // how it moves (m/s)
  V3 up{0.0f, 1.0f, 0.0f};    // the view's up (the picture's up on the big screen)
  V3 out{0.0f, 0.0f, 1.0f};   // the big screen's face, toward you
  float groundY = -1e9f;      // the ground under it (room Y: the field's grass), -1e9 none (the picture)
  float unit = 1.0f;          // metres a game unit here
};

struct State {
  // the game's ball
  PortBallFeed feed{};
  bool haveFeed = false;
  unsigned lastFrame = 0, kicks = 0, bounces = 0, goals = 0;
  bool countsKnown = false;
  uint64_t feedNs = 0;
  Anchor a, prevA;
  float trailAcc = 0.0f, tongueAcc = 0.0f, emberAcc = 0.0f, cloudAcc = 0.0f;
  float t = 0.0f;  // seconds, the aura's clock
  int skinShown = 0;
  int sentMine = -1, sentTheirs = -1;
  // the preview
  bool previewOn = false;
  int previewSkin = 0;
  XrPosef where{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  float previewAge = 0.0f, lastHop = 0.0f;
  float ballQ[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};
State g_s;
std::mutex g_previewMutex;
bool g_previewAsk = false, g_previewStop = false;
int g_previewAskSkin = 0;
XrPosef g_previewAskWhere{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
std::atomic<bool> g_previewing{false};

V3 game_to_room(float scale, float yaw, const XrVector3f& origin, float gx, float gy, float gz) {
  const float a = gx, b = gz, c = -gy;
  const float cs = std::cos(yaw), sn = std::sin(yaw);
  return V3{(a * cs + c * sn) * scale + origin.x, b * scale + origin.y, (-a * sn + c * cs) * scale + origin.z};
}
V3 game_dir_to_room(float scale, float yaw, float gx, float gy, float gz) {
  const float a = gx, b = gz, c = -gy;
  const float cs = std::cos(yaw), sn = std::sin(yaw);
  return V3{(a * cs + c * sn) * scale, b * scale, (-a * sn + c * cs) * scale};
}
V3 head_of(const xrfx::Scene& sc) { return (v3(sc.views[0].pose.position) + v3(sc.views[1].pose.position)) * 0.5f; }

P& spawn() {
  if (g_parts.size() >= kMaxParts) {  // (full: the oldest goes)
    size_t oldest = 0;
    float best = -1.0f;
    for (size_t i = 0; i < g_parts.size(); ++i) {
      const float k = g_parts[i].age / std::max(0.01f, g_parts[i].life);
      if (k > best) best = k, oldest = i;
    }
    g_parts[oldest] = P{};
    return g_parts[oldest];
  }
  g_parts.push_back(P{});
  return g_parts.back();
}

// a direction in the view's frame: `u` along up, the rest round it
V3 around(const Anchor& a, float upShare) {
  V3 d = g_rng.unit();
  d = d + a.up * upShare;
  if (a.mode == 2) d = d - a.out * dot(d, a.out) * 0.8f;  // (on the picture: mostly in its plane)
  return norm(d);
}

// ------------------------------------------------------------------------------------------------------------------
// FIREBALL
// ------------------------------------------------------------------------------------------------------------------
// r169 (Lance: "the fire on there looks fake as fuck i need more realistic flames like the ones charmander has"): the
// fire is the real-fire flipbook now (shape 163: tools/outside/balls169/flame169.py, the method of Colosseum's Charmander
// flame - soft noisy puffs, no hard edge, brightness = see-through, frames flipped quickly). A flame: rooted on the ball,
// licking up (or streaming back off it in flight), cooling to smoke as it goes.
P& fire_sprite(const Anchor& a, V3 at, V3 v, float size, float life) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up;
  q.shape = kShFire, q.size = size, q.size1 = size, q.aspect = 0.55f, q.life = life;
  q.c2 = 0x1C1714;  // (its smoke)
  q.frame0 = static_cast<int>(g_rng.next() & 31), q.fps = g_rng.f(16.0f, 22.0f);
  q.fadeIn = 0.06f, q.fadeOut = life * 0.45f, q.glow = 1.0f;
  return q;
}
void fire_tongue(const Anchor& a, bool trail) {
  const V3 side = around(a, trail ? 0.2f : 1.1f);
  const float sp = len(a.vel);
  if (trail && sp > 1e-4f) {
    // fire torn off the ball in flight: left behind, it swells, rises, cools and goes to smoke
    const V3 back = norm(a.vel) * -1.0f;
    P& q = fire_sprite(a, a.c + side * (a.r * 0.5f) + back * (a.r * g_rng.f(0.4f, 1.6f)), a.vel * 0.35f + a.up * (a.r * 2.0f),
                       a.r * g_rng.f(2.0f, 2.8f), g_rng.f(0.3f, 0.55f));
    q.size1 = q.size * 1.6f, q.orient = a.mode == 2 ? kScreen : kAxis;
    q.axis = norm(back + a.up * 0.6f);  // (streaming back along its path, licking up)
    q.temp0 = 0.9f, q.temp1 = 0.05f, q.smoke0 = 0.25f, q.smoke1 = 0.9f, q.bright = 0.42f, q.drag = 1.5f;
    return;
  }
  // a tongue licking off the ball: its foot on the surface (the quad's foot is its picture's base), streaming back
  // against the motion as it flies
  const V3 dir = norm(a.up * 1.0f + side * 0.35f - a.vel * (0.6f / std::max(0.05f, a.r * 25.0f)));
  const float size = a.r * g_rng.f(1.6f, 2.4f);
  P& q = fire_sprite(a, a.c + side * (a.r * 0.55f) + dir * (size * 0.38f), a.vel * 0.9f + dir * (a.r * g_rng.f(1.0f, 2.2f)), size,
                     g_rng.f(0.3f, 0.55f));
  q.v = q.v + side * (a.r * 0.4f);
  q.size1 = size * 0.7f;
  q.orient = a.mode == 2 ? kScreen : kAxis;
  q.axis = dir;
  q.temp0 = 1.0f, q.temp1 = 0.45f, q.smoke0 = 0.1f, q.smoke1 = 0.45f, q.bright = 0.5f, q.drag = 1.2f;
  if (len(q.v) < 1e-5f) q.v = dir * 0.01f;
}void fire_ember(const Anchor& a, V3 at, V3 v, float life) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up;
  q.shape = kShSpark;
  q.size = std::max(0.0025f, a.r * g_rng.f(0.12f, 0.22f)), q.size1 = q.size * 0.4f;
  q.life = life, q.fadeOut = 0.35f, q.glow = 1.0f;
  q.c1 = 0xFFF2B0, q.c2 = g_rng.next() & 1 ? 0xFF8A1A : 0xFF4A10;
  q.drag = 0.9f, q.grav = a.r * 16.0f;
  q.floorY = a.groundY;
}
void fire_puff(const Anchor& a, V3 at, float s0, float s1, float life, uint32_t c1, uint32_t c2, float peak) {
  P& q = spawn();
  q.p = at, q.v = a.vel * 0.12f + a.up * (a.r * 1.4f), q.up = a.up;
  q.shape = kShPuff, q.size = s0, q.size1 = s1, q.life = life, q.peak = peak, q.glow = 0.9f;
  q.c1 = c1, q.c2 = c2, q.c2a = static_cast<uint8_t>(g_rng.next() & 0xFF), q.fadeOut = life * 0.6f, q.drag = 2.0f;
}
void ring(const Anchor& a, V3 at, V3 face, float s0, float s1, float life, uint32_t c1, uint32_t c2, int shape, bool flat) {
  P& q = spawn();
  q.p = at, q.up = face;
  q.shape = shape, q.size = s0, q.size1 = s1, q.life = life, q.glow = 1.0f, q.ringFade = true;
  q.c1 = c1, q.c2 = c2;
  q.orient = flat ? kFlat : (a.mode == 2 ? kScreen : kFace);
  q.roll = g_rng.f(0.0f, 6.28f);
}
void fire_hit(const Anchor& a, V3 at, int kind /*0 kick, 1 bounce, 2 goal*/, float strength) {
  const float k = kind == 2 ? 2.2f : (kind == 1 ? 1.0f : 0.7f) * clampf(strength, 0.6f, 1.6f);
  const int embers = static_cast<int>((kind == 2 ? 80 : (kind == 1 ? 36 : 22)) * std::min(1.4f, k));
  for (int i = 0; i < embers; ++i) {
    V3 d = around(a, kind == 0 ? 0.3f : 1.2f);
    fire_ember(a, at + d * (a.r * 0.3f), d * (a.r * g_rng.f(8.0f, 16.0f) * k), g_rng.f(0.5f, 1.0f));
  }
  fire_puff(a, at, a.r * 3.0f * k, a.r * 5.0f * k, 0.18f, 0xFFF0C0, 0xFF6A10, 0.7f);  // (the flash of heat)
  // r169: fire bursting out of it - real flames flung out, each streaming back along its flight, cooling to smoke
  for (int i = 0; i < (kind == 2 ? 26 : (kind == 1 ? 12 : 9)); ++i) {
    const V3 d = around(a, kind == 0 ? 0.4f : 0.9f);
    P& q = fire_sprite(a, at + d * (a.r * 0.6f), d * (a.r * g_rng.f(5.0f, 10.0f) * k) + a.up * (a.r * 1.5f), a.r * 2.4f * k,
                       g_rng.f(0.45f, 0.75f));
    q.size1 = q.size * 1.6f, q.orient = a.mode == 2 ? kScreen : kAlong, q.drag = 3.5f;
    q.temp0 = 1.0f, q.temp1 = 0.1f, q.smoke0 = 0.2f, q.smoke1 = 0.95f, q.bright = 0.5f;
  }
  if (kind != 0) {
    // a ring of fire running out along the ground (flames, upright, racing outward and dying down)
    const int n = kind == 2 ? 18 : 12;
    V3 e1 = norm(cross(a.up, std::fabs(a.up.y) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f}));
    V3 e2 = cross(a.up, e1);
    if (a.mode == 2) e1 = norm(cross(a.up, a.out)), e2 = a.out * 0.0f;
    for (int i = 0; i < n; ++i) {
      const float ang = 6.2831853f * i / n + g_rng.f(-0.12f, 0.12f);
      const V3 d = norm(e1 * std::cos(ang) + e2 * std::sin(ang), e1);
      const float sz = a.r * g_rng.f(1.6f, 2.2f) * std::min(k, 1.6f);
      P& q = fire_sprite(a, at + d * (a.r * 0.8f) + a.up * (sz * 0.38f), d * (a.r * 9.0f * k) + a.up * (a.r * 0.6f), sz,
                         g_rng.f(0.4f, 0.6f));
      q.size1 = sz * 0.5f, q.orient = a.mode == 2 ? kScreen : kUpright, q.drag = 3.0f;
      q.temp0 = 1.0f, q.temp1 = 0.2f, q.smoke0 = 0.15f, q.smoke1 = 0.8f, q.bright = 0.55f;
    }
  }
  if (kind != 0 && a.groundY > -1e8f) {  // the scorch where it landed, a few small flames still burning on it
    P& q = spawn();
    q.p = V3{at.x, a.groundY + std::max(0.0008f, a.r * 0.02f), at.z}, q.up = a.up;
    q.shape = kShCrater, q.orient = kFlat, q.roll = g_rng.f(0.0f, 6.28f);
    q.size = a.r * 3.2f * std::min(k, 1.6f), q.size1 = q.size * 1.05f, q.life = kind == 2 ? 3.5f : 2.6f;
    q.fadeIn = 0.05f, q.fadeOut = 1.2f, q.peak = 0.85f, q.glow = 0.0f, q.heat = true;
    q.c1 = 0xFF6A10, q.c2 = 0x1A120C;
    for (int i = 0; i < (kind == 2 ? 7 : 4); ++i) {
      const float ang = g_rng.f(0.0f, 6.2831853f), rr = g_rng.f(0.2f, 1.1f) * a.r;
      const V3 base{at.x + std::cos(ang) * rr, a.groundY, at.z + std::sin(ang) * rr};
      const float sz = a.r * g_rng.f(0.7f, 1.2f);
      P& f = fire_sprite(a, base + a.up * (sz * 0.38f), a.up * (a.r * 0.3f), sz, g_rng.f(0.9f, 1.6f));
      f.size1 = sz * 0.3f, f.orient = kUpright, f.fadeIn = 0.15f;
      f.temp0 = 0.9f, f.temp1 = 0.3f, f.smoke0 = 0.2f, f.smoke1 = 0.7f, f.bright = 0.45f;
    }
  }
}
// ------------------------------------------------------------------------------------------------------------------
// PLANET EARTH
// ------------------------------------------------------------------------------------------------------------------
void earth_cloud(const Anchor& a, V3 at, V3 v, float s0, float s1, float life, float peak) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up;
  q.shape = kShPuff, q.size = s0, q.size1 = s1, q.life = life, q.peak = peak, q.glow = 0.15f;
  q.c1 = 0xFFFFFF, q.c2 = 0xC8D8EC, q.c2a = static_cast<uint8_t>(g_rng.next() & 0xFF), q.fadeOut = life * 0.6f, q.drag = 2.5f;
}
void earth_wisp(const Anchor& a) {
  P& q = spawn();
  const V3 side = around(a, 0.0f);
  q.p = a.c + side * (a.r * 0.6f);
  q.v = a.vel * -0.25f;
  q.up = a.up;
  q.orient = kAlong, q.shape = kShWisp;
  q.size = a.r * 2.6f, q.size1 = a.r * 1.0f, q.aspect = 0.5f, q.life = g_rng.f(0.25f, 0.4f);
  q.c1 = 0xCFEFFF, q.c2 = 0x2A6BFF, q.glow = 1.0f, q.peak = 0.45f, q.fadeOut = 0.2f;  // (r170: subtler)
}
void earth_hit(const Anchor& a, V3 at, int kind, float strength) {
  const float k = kind == 2 ? 2.2f : (kind == 1 ? 1.0f : 0.7f) * clampf(strength, 0.6f, 1.6f);
  const bool flat = kind != 0 && a.mode != 2;
  if (kind != 0) {  // the ocean splash
    const int drops = static_cast<int>((kind == 2 ? 60 : 28) * std::min(1.4f, k));
    for (int i = 0; i < drops; ++i) {
      const V3 d = around(a, 1.6f);
      P& q = spawn();
      q.p = at + d * (a.r * 0.4f), q.v = d * (a.r * g_rng.f(7.0f, 13.0f) * k), q.up = a.up;
      q.shape = kShDrop, q.orient = kAlong, q.size = std::max(0.003f, a.r * g_rng.f(0.25f, 0.45f)), q.size1 = q.size * 0.6f;
      q.life = g_rng.f(0.45f, 0.8f), q.c1 = 0xD8F4FF, q.c2 = 0x2A7BFF, q.glow = 0.35f, q.fadeOut = 0.2f;
      q.drag = 0.4f, q.grav = a.r * 30.0f, q.floorY = a.groundY;
    }
  }
  const int puffs = kind == 2 ? 16 : (kind == 1 ? 12 : 9);  // the cloud puff ring
  for (int i = 0; i < puffs; ++i) {
    const float ang = 6.2831853f * i / puffs + g_rng.f(-0.15f, 0.15f);
    V3 e1 = norm(cross(a.up, std::fabs(a.up.y) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f}));
    V3 e2 = cross(a.up, e1);
    if (a.mode == 2) e1 = norm(cross(a.up, a.out)), e2 = a.up;  // (on the picture: round it in the picture's plane)
    if (kind == 0 && a.mode != 2 && len(a.vel) > 1e-4f) {      // (a kick: round its new flight)
      const V3 f = norm(a.vel);
      e1 = norm(cross(f, std::fabs(f.y) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f}));
      e2 = cross(f, e1);
    }
    const V3 d = e1 * std::cos(ang) + e2 * std::sin(ang);
    earth_cloud(a, at + d * (a.r * 0.6f), d * (a.r * g_rng.f(5.0f, 8.0f) * k) + a.up * (a.r * 0.6f), a.r * 0.9f * k, a.r * 1.9f * k,
                g_rng.f(0.6f, 0.9f), 0.85f);
  }
  ring(a, at + a.up * (a.r * 0.06f), a.up, a.r * 1.2f, a.r * 8.0f * k, 0.5f, 0xCFEFFF, 0x2A7BFF, kShRing, flat);
  if (kind != 0) {  // the ripples
    ring(a, at + a.up * (a.r * 0.05f), a.up, a.r * 0.8f, a.r * 11.0f * k, 0.8f, 0xE8F8FF, 0x3A8BFF, kShWave, flat);
    if (kind == 2) ring(a, at + a.up * (a.r * 0.05f), a.up, a.r * 0.5f, a.r * 18.0f, 1.2f, 0xE8F8FF, 0x3A8BFF, kShWave, flat);
  }
  P& f = spawn();  // the flash: blue
  f.p = at, f.up = a.up, f.shape = kShPuff, f.size = a.r * 3.0f * k, f.size1 = a.r * 5.0f * k, f.life = 0.25f, f.glow = 1.0f;
  f.c1 = 0xE0F6FF, f.c2 = 0x3A8BFF, f.peak = 0.75f, f.fadeOut = 0.18f;
}

// ------------------------------------------------------------------------------------------------------------------
// r170: the other 21 skins' effects - each alive round the ball (its aura and idle pieces), its trail in flight, its
// reactions (kick 0, bounce 1, goal 2). Real fire / smoke wherever there is any (the r169 flipbook: lava, the
// bob-omb's fuse and blast, the cannonball's smoke, the BLACK 1/1's black smoke).
// ------------------------------------------------------------------------------------------------------------------
constexpr int kShPaper = 0, kShStar = 3, kShStreak = 30, kShBolt = 31, kShBubble = 34, kShTwinkle = 77, kShPixel = 147;
constexpr uint8_t kPixBlock = 40, kPixBurst = 128, kPixRing = 220;
enum Skin : int {
  kSnow = 3, kLava, kThunder, kGalaxy, kMoon, kPixelB, kNeon, kDisco, kBasket, kEight, kBowl, kLeather, kGold, kDiamond,
  kChrome, kBlack11, kEye, kBomb, kCannon, kDuck, kHole
};
V3 tangent(const Anchor& a, V3 d) {  // (round the ball, across the view's up)
  V3 t = cross(a.up, d);
  return len(t) > 1e-4f ? norm(t) : V3{1.0f, 0.0f, 0.0f};
}
void e_spark(const Anchor& a, V3 at, V3 v, float life, uint32_t c1, uint32_t c2, float sz, float grav) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up, q.shape = kShSpark;
  q.size = std::max(0.002f, a.r * sz), q.size1 = q.size * 0.3f, q.life = life, q.fadeOut = life * 0.5f, q.glow = 1.0f;
  q.c1 = c1, q.c2 = c2, q.drag = 1.0f, q.grav = a.r * grav, q.floorY = a.groundY;
}
void e_twinkle(const Anchor& a, V3 at, V3 v, float life, uint32_t c1, uint32_t c2, float sz) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up, q.shape = kShTwinkle;
  q.size = std::max(0.003f, a.r * sz), q.size1 = q.size * 0.2f, q.life = life, q.fadeIn = life * 0.3f, q.fadeOut = life * 0.6f;
  q.glow = 1.0f, q.c1 = c1, q.c2 = c2, q.drag = 1.5f;
}
void e_puff(const Anchor& a, V3 at, V3 v, float s0, float s1, float life, uint32_t c1, uint32_t c2, float peak, float glow) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up, q.shape = kShPuff, q.size = s0, q.size1 = s1, q.life = life, q.peak = peak, q.glow = glow;
  q.c1 = c1, q.c2 = c2, q.c2a = static_cast<uint8_t>(g_rng.next() & 0xFF), q.fadeOut = life * 0.6f, q.drag = 2.2f;
}
void e_streak(const Anchor& a, V3 at, V3 v, float lenR, float life, uint32_t c1, uint32_t c2, float peak) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up, q.shape = kShStreak, q.orient = kAlong;
  q.size = a.r * lenR, q.size1 = a.r * lenR * 0.5f, q.aspect = 0.18f, q.life = life, q.peak = peak, q.glow = 1.0f;
  q.c1 = c1, q.c2 = c2, q.fadeOut = life * 0.6f, q.drag = 3.0f;
}
void e_bolt(const Anchor& a, V3 from, V3 dir, float lenR, float life) {
  P& q = spawn();
  q.p = from + dir * (a.r * lenR * 0.5f), q.up = a.up, q.shape = kShBolt, q.orient = kAxis, q.axis = dir;
  q.size = a.r * lenR, q.size1 = q.size, q.aspect = 0.22f, q.life = life, q.glow = 1.0f, q.fadeIn = 0.0f, q.fadeOut = life * 0.7f;
  q.c1 = 0xFFFFFF, q.c2 = 0x5AC8FF;
}
void e_chunk(const Anchor& a, V3 at, V3 v, uint32_t c1, uint32_t c2, float sz, float grav) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up, q.shape = kShPaper, q.size = std::max(0.002f, a.r * sz), q.size1 = q.size * 0.6f;
  q.life = g_rng.f(0.5f, 0.9f), q.glow = 0.0f, q.c1 = c1, q.c2 = c2, q.fadeOut = 0.25f, q.drag = 0.5f, q.grav = a.r * grav;
  q.floorY = a.groundY;
}
void e_pixel(const Anchor& a, V3 at, V3 v, uint32_t c1, float sz, uint8_t look, float life) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up, q.shape = kShPixel, q.size = std::max(0.003f, a.r * sz), q.size1 = q.size;
  q.life = life, q.glow = 0.6f, q.c1 = c1, q.c2 = c1, q.c2a = look, q.fadeIn = 0.0f, q.fadeOut = life * 0.3f;
  q.orient = a.mode == 2 ? kScreen : kFace;
}
void e_drop(const Anchor& a, V3 at, V3 v, uint32_t c1, uint32_t c2, float sz, float glow) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up, q.shape = kShDrop, q.orient = kAlong, q.size = std::max(0.003f, a.r * sz), q.size1 = q.size * 0.6f;
  q.life = g_rng.f(0.45f, 0.8f), q.c1 = c1, q.c2 = c2, q.glow = glow, q.fadeOut = 0.2f, q.drag = 0.4f, q.grav = a.r * 30.0f;
  q.floorY = a.groundY;
}
void e_bubble(const Anchor& a, V3 at, V3 v, float sz, float life) {
  P& q = spawn();
  q.p = at, q.v = v, q.up = a.up, q.shape = kShBubble, q.size = a.r * sz, q.size1 = q.size * 1.2f, q.life = life, q.glow = 0.5f;
  q.c1 = 0xFFFFFF, q.c2 = 0x9ADFFF, q.fadeOut = 0.15f, q.drag = 1.8f, q.grav = -a.r * 3.0f;
}
// smoke only (the flipbook gone cold): its colour `c`
P& e_smoke(const Anchor& a, V3 at, V3 v, float s0, float s1, float life, uint32_t c, float amount) {
  P& q = fire_sprite(a, at, v, s0, life);
  q.size1 = s1, q.orient = a.mode == 2 ? kScreen : kUpright, q.c2 = c, q.drag = 1.6f;
  q.temp0 = q.temp1 = 0.0f, q.bright = 0.0f, q.smoke0 = amount, q.smoke1 = amount * 0.8f;
  if (a.mode != 2 && len(a.vel) > a.r * 25.0f) q.orient = kAxis, q.axis = norm(norm(a.vel) * -1.0f + a.up * 0.6f);  // (streaming back in flight)
  return q;
}
P& e_fire(const Anchor& a, V3 at, V3 v, float size, float life, float bright) {
  P& q = fire_sprite(a, at, v, size, life);
  q.size1 = size * 0.5f, q.orient = a.mode == 2 ? kScreen : kUpright, q.drag = 1.4f;
  q.temp0 = 1.0f, q.temp1 = 0.3f, q.smoke0 = 0.15f, q.smoke1 = 0.6f, q.bright = bright;
  return q;
}
void e_decal(const Anchor& a, V3 at, float sz, float life, uint32_t c1, uint32_t c2, bool hot) {
  if (a.groundY < -1e8f) return;
  P& q = spawn();
  q.p = V3{at.x, a.groundY + std::max(0.0008f, a.r * 0.02f), at.z}, q.up = a.up;
  q.shape = kShCrater, q.orient = kFlat, q.roll = g_rng.f(0.0f, 6.28f), q.size = a.r * sz, q.size1 = q.size * 1.05f;
  q.life = life, q.fadeIn = 0.05f, q.fadeOut = life * 0.45f, q.peak = 0.8f, q.glow = 0.0f, q.heat = hot;
  q.c1 = c1, q.c2 = c2, q.c2a = hot ? 255 : 0;
}
uint32_t pick(const uint32_t* c, int n) { return c[g_rng.next() % static_cast<uint32_t>(n)]; }
const uint32_t kRainbow[] = {0xFF5A5A, 0xFFB43A, 0xFFF05A, 0x6AF07A, 0x5AC8FF, 0x9A7BFF, 0xFF7AD8};
const uint32_t kDiscoC[] = {0xFFFFFF, 0xFF7AD8, 0x7AD8FF, 0xFFD34A, 0xB08AFF};
const uint32_t kPixC[] = {0xE52521, 0xFFD800, 0x2ECC40, 0x00B4FF, 0xB14AED, 0xFF7A00, 0xFFFFFF};

// idle + trail, every frame
void live_other(int skin, const Anchor& a, float dt, bool flying, float sp) {
  auto every = [&](float& acc, float rate, auto fn) {
    acc += dt * rate;
    while (acc >= 1.0f) {
      acc -= 1.0f;
      fn();
    }
  };
  // r170 (Lance on r168's EARTH: the ring round it "was OD ... almost made the ball harder to see"): nothing round a ball at
  // rest or rolling - its effects come with its flight and its hits only. (What is part of the ball itself stays: LAVA
  // ROCK's flames off its cracks, the BOB-OMB's burning fuse; FIREBALL's tongues above.)
  if (!flying && skin != kLava && skin != kBomb) return;
  const V3 back = len(a.vel) > 1e-5f ? norm(a.vel) * -1.0f : a.up * -1.0f;
  const float tr = flying ? std::min(50.0f, 12.0f + sp * 0.4f) : 0.0f;
  switch (skin) {
    case kSnow:
      every(g_s.emberAcc, 5.0f, [&] { const V3 d = around(a, 0.3f); e_twinkle(a, a.c + d * a.r, d * (a.r * 0.6f) - a.up * (a.r * 1.5f), 0.9f, 0xFFFFFF, 0xBFE8FF, 0.35f); });
      every(g_s.trailAcc, tr * 0.8f, [&] {
        e_puff(a, a.c + back * (a.r * g_rng.f(0.2f, 1.2f)) + around(a, 0.0f) * (a.r * 0.4f), a.vel * 0.15f, a.r * 1.2f, a.r * 2.6f, g_rng.f(0.4f, 0.7f), 0xFFFFFF, 0xD8E8F8, 0.55f, 0.1f);
        e_twinkle(a, a.c + around(a, 0.0f) * a.r, a.vel * 0.2f, 0.4f, 0xFFFFFF, 0x9AD8FF, 0.3f);
      });
      break;
    case kLava:
      every(g_s.tongueAcc, flying ? 16.0f : 9.0f, [&] {
        const V3 d = around(a, 1.0f);
        e_fire(a, a.c + d * (a.r * 0.6f) + a.up * (a.r * 0.4f), a.vel * 0.85f + a.up * (a.r * 1.5f), a.r * g_rng.f(0.9f, 1.4f), g_rng.f(0.3f, 0.5f), 0.38f);
      });
      every(g_s.emberAcc, flying ? 22.0f : 7.0f, [&] { const V3 d = around(a, 1.0f); fire_ember(a, a.c + d * (a.r * 0.9f), a.vel * 0.3f + d * (a.r * g_rng.f(1.5f, 4.0f)) + a.up * (a.r * 3.0f), g_rng.f(0.5f, 1.0f)); });
      every(g_s.cloudAcc, flying ? 30.0f : 3.0f, [&] { e_smoke(a, a.c + a.up * (a.r * 0.6f) + back * (a.r * g_rng.f(0.3f, 1.2f)), a.vel * 0.3f + a.up * (a.r * 2.0f), a.r * 1.6f, a.r * 3.2f, g_rng.f(0.5f, 0.8f), 0x1E1A18, 0.8f); });
      break;
    case kThunder:
      every(g_s.tongueAcc, flying ? 14.0f : 6.0f, [&] { const V3 d = around(a, 0.2f); e_bolt(a, a.c + d * (a.r * 0.8f), norm(d + tangent(a, d) * g_rng.f(-0.6f, 0.6f)), g_rng.f(1.2f, 2.4f), 0.09f); });
      every(g_s.emberAcc, flying ? 30.0f : 10.0f, [&] { const V3 d = around(a, 0.3f); e_spark(a, a.c + d * a.r, a.vel * 0.3f + d * (a.r * g_rng.f(2.0f, 6.0f)), 0.35f, 0xFFFFFF, 0x5AC8FF, 0.15f, 4.0f); });
      every(g_s.trailAcc, tr, [&] { e_streak(a, a.c + around(a, 0.0f) * (a.r * 0.6f), a.vel * 0.25f, 3.0f, 0.25f, 0xDFF6FF, 0x3A8BFF, 0.9f); });
      break;
    case kGalaxy:
      every(g_s.emberAcc, 9.0f, [&] {
        const V3 d = around(a, 0.0f);
        e_twinkle(a, a.c + d * (a.r * g_rng.f(1.3f, 2.2f)), tangent(a, d) * (a.r * 2.5f) + a.vel * 0.9f, g_rng.f(0.6f, 1.2f), pick(kDiscoC, 5), 0x9A6BFF, 0.3f);
      });
      every(g_s.cloudAcc, flying ? 18.0f : 2.0f, [&] { e_puff(a, a.c + back * (a.r * g_rng.f(0.3f, 1.4f)), a.vel * 0.1f, a.r * 1.6f, a.r * 3.0f, g_rng.f(0.5f, 0.8f), g_rng.next() & 1 ? 0x9A6BFF : 0x5A7BFF, 0x2A104A, 0.45f, 1.0f); });
      every(g_s.trailAcc, tr, [&] { e_spark(a, a.c + around(a, 0.0f) * (a.r * 0.8f), a.vel * 0.3f, 0.5f, 0xFFFFFF, 0xC8B0FF, 0.12f, 0.0f); });
      break;
    case kMoon:
      every(g_s.emberAcc, 1.5f, [&] { const V3 d = around(a, 0.5f); e_spark(a, a.c + d * a.r, d * (a.r * 0.8f), 0.8f, 0xE8ECF0, 0x8A8E98, 0.08f, 2.0f); });
      every(g_s.trailAcc, tr * 0.6f, [&] { e_puff(a, a.c + back * (a.r * g_rng.f(0.3f, 1.2f)), a.vel * 0.1f, a.r * 1.0f, a.r * 2.2f, g_rng.f(0.4f, 0.7f), 0xC8CCD4, 0x7A7E88, 0.35f, 0.1f); });
      break;
    case kPixelB:
      every(g_s.emberAcc, flying ? 10.0f : 5.0f, [&] { const V3 d = around(a, 0.6f); e_pixel(a, a.c + d * (a.r * 1.3f), d * (a.r * 1.5f), pick(kPixC, 7), 0.35f, kPixBlock, 0.35f); });
      every(g_s.trailAcc, tr * 0.7f, [&] { e_pixel(a, a.c + back * (a.r * g_rng.f(0.5f, 1.5f)), V3{}, pick(kPixC, 7), g_rng.f(0.4f, 0.8f), kPixBlock, 0.4f); });
      break;
    case kNeon:
      every(g_s.emberAcc, flying ? 14.0f : 6.0f, [&] { const V3 d = around(a, 0.3f); e_spark(a, a.c + d * a.r, a.vel * 0.3f + d * (a.r * 2.0f), 0.45f, 0xFFFFFF, g_rng.next() & 1 ? 0x00F0FF : 0xFF2AD8, 0.14f, 0.0f); });
      every(g_s.trailAcc, tr * 1.2f, [&] {
        e_streak(a, a.c + around(a, 0.0f) * (a.r * 0.7f), a.vel * 0.2f, 3.5f, 0.3f, 0xFFFFFF, g_rng.next() & 1 ? 0x00F0FF : 0xFF2AD8, 1.0f);
      });
      break;
    case kDisco:
      every(g_s.emberAcc, flying ? 16.0f : 10.0f, [&] {
        const V3 d = around(a, 0.2f);
        e_twinkle(a, a.c + d * (a.r * g_rng.f(1.1f, 2.6f)), d * (a.r * 0.5f) + a.vel * 0.6f, g_rng.f(0.25f, 0.5f), pick(kDiscoC, 5), 0xFFFFFF, g_rng.f(0.3f, 0.6f));
      });
      every(g_s.trailAcc, tr * 0.6f, [&] { e_spark(a, a.c + around(a, 0.0f) * (a.r * 0.8f), a.vel * 0.2f, 0.5f, 0xFFFFFF, pick(kDiscoC, 5), 0.14f, 3.0f); });
      break;
    case kBasket:
    case kEight:
    case kBowl:
    case kLeather:
    case kChrome: {
      const uint32_t c = skin == kBasket ? 0xFFB070 : (skin == kEight ? 0xFFFFFF : (skin == kBowl ? 0xB08AFF : (skin == kLeather ? 0xD8B890 : 0xF0F4FF)));
      every(g_s.trailAcc, tr * 0.5f, [&] { e_streak(a, a.c + around(a, 0.0f) * (a.r * 0.6f), a.vel * 0.3f, 2.5f, 0.22f, 0xFFFFFF, c, 0.45f); });
      if (skin == kLeather) every(g_s.cloudAcc, tr * 0.3f, [&] { e_puff(a, a.c + back * a.r, a.vel * 0.1f, a.r * 0.8f, a.r * 1.8f, 0.5f, 0xC8A878, 0x7A6040, 0.3f, 0.0f); });
      if (skin == kChrome) every(g_s.emberAcc, 3.0f, [&] { const V3 d = around(a, 0.8f); e_twinkle(a, a.c + d * (a.r * 0.95f), a.vel * 0.95f, 0.3f, 0xFFFFFF, 0xD8E0F0, 0.6f); });
      break;
    }
    case kGold:
      every(g_s.emberAcc, flying ? 12.0f : 6.0f, [&] { const V3 d = around(a, 0.4f); e_twinkle(a, a.c + d * (a.r * g_rng.f(0.95f, 1.6f)), a.vel * 0.8f + d * (a.r * 0.4f), 0.45f, 0xFFF4C8, 0xFFC830, 0.45f); });
      every(g_s.trailAcc, tr, [&] { e_spark(a, a.c + around(a, 0.0f) * (a.r * 0.8f), a.vel * 0.2f + around(a, 0.0f) * (a.r * 1.0f), 0.6f, 0xFFF4C8, 0xE8B030, 0.12f, 6.0f); });
      break;
    case kDiamond:
      every(g_s.emberAcc, flying ? 14.0f : 8.0f, [&] { const V3 d = around(a, 0.3f); e_twinkle(a, a.c + d * (a.r * g_rng.f(1.0f, 1.8f)), a.vel * 0.8f, 0.4f, 0xFFFFFF, pick(kRainbow, 7), 0.45f); });
      every(g_s.trailAcc, tr, [&] { e_streak(a, a.c + around(a, 0.0f) * (a.r * 0.7f), a.vel * 0.25f, 3.0f, 0.3f, 0xFFFFFF, pick(kRainbow, 7), 0.8f); });
      break;
    case kBlack11:
      every(g_s.emberAcc, flying ? 12.0f : 5.0f, [&] { const V3 d = around(a, 0.4f); e_twinkle(a, a.c + d * (a.r * g_rng.f(1.0f, 1.6f)), a.vel * 0.8f, 0.5f, 0xFFE38A, 0xC9A227, 0.4f); });
      every(g_s.cloudAcc, flying ? 20.0f : 3.0f, [&] { e_smoke(a, a.c + back * (a.r * g_rng.f(0.2f, 1.0f)) + around(a, 0.0f) * (a.r * 0.4f), a.vel * 0.15f + a.up * (a.r * 1.2f), a.r * 1.6f, a.r * 3.4f, g_rng.f(0.6f, 1.0f), 0x08080A, 0.9f); });
      break;
    case kEye:
      every(g_s.trailAcc, tr * 0.5f, [&] { e_puff(a, a.c + back * a.r, a.vel * 0.1f, a.r * 1.0f, a.r * 2.2f, 0.5f, 0xFFD0D8, 0xC86070, 0.25f, 0.6f); });
      break;
    case kBomb: {
      const V3 fuse = a.c + a.up * (a.r * 1.05f);  // (its fuse burning on top)
      every(g_s.tongueAcc, 14.0f, [&] { e_fire(a, fuse + a.up * (a.r * 0.25f) + around(a, 0.0f) * (a.r * 0.1f), a.vel * 0.9f + a.up * (a.r * 1.5f), a.r * g_rng.f(0.5f, 0.8f), g_rng.f(0.2f, 0.35f), 0.45f); });
      every(g_s.emberAcc, 22.0f, [&] { const V3 d = around(a, 1.5f); e_spark(a, fuse, a.vel * 0.8f + d * (a.r * g_rng.f(3.0f, 7.0f)), g_rng.f(0.3f, 0.6f), 0xFFF2B0, 0xFF8A1A, 0.1f, 14.0f); });
      every(g_s.cloudAcc, flying ? 16.0f : 3.0f, [&] { e_smoke(a, fuse + a.up * (a.r * 0.4f), a.vel * 0.3f + a.up * (a.r * 1.8f), a.r * 0.8f, a.r * 2.4f, g_rng.f(0.6f, 1.0f), 0x5A5A60, 0.6f); });
      break;
    }
    case kCannon:
      every(g_s.cloudAcc, tr * 1.4f, [&] { e_smoke(a, a.c + back * (a.r * g_rng.f(0.4f, 1.4f)), a.vel * 0.3f + a.up * (a.r * 0.8f), a.r * 1.8f, a.r * 4.0f, g_rng.f(0.6f, 1.0f), 0x6A6A70, 0.75f); });
      every(g_s.emberAcc, flying ? 8.0f : 0.0f, [&] { fire_ember(a, a.c + back * a.r, a.vel * 0.2f + around(a, 1.0f) * (a.r * 2.0f), 0.5f); });
      break;
    case kDuck:
      every(g_s.emberAcc, flying ? 16.0f : 2.5f, [&] { const V3 d = around(a, 0.5f); e_bubble(a, a.c + d * (a.r * 1.1f), a.vel * 0.3f + d * (a.r * 0.5f), g_rng.f(0.25f, 0.5f), g_rng.f(0.8f, 1.4f)); });
      break;
    case kHole:
      every(g_s.emberAcc, 16.0f, [&] {  // matter spiralling in
        const V3 d = norm(around(a, 0.0f) - a.up * dot(around(a, 0.0f), a.up));
        e_spark(a, a.c + d * (a.r * g_rng.f(2.4f, 3.4f)), tangent(a, d) * (a.r * 4.0f) - d * (a.r * 4.5f) + a.vel * 0.9f, g_rng.f(0.5f, 0.7f), 0xFFE0B0, g_rng.next() & 1 ? 0xB06AFF : 0xFF8A3A, 0.14f, 0.0f);
      });
      every(g_s.trailAcc, tr, [&] { e_streak(a, a.c + around(a, 0.0f) * (a.r * 0.8f), a.vel * 0.3f, 3.0f, 0.3f, 0xC8A0FF, 0x3A0A6A, 0.7f); });
      break;
    default: break;
  }
}

// a kick (0), a bounce (1), a goal (2)
void hit_other(int skin, const Anchor& a, V3 at, int kind, float strength) {
  const float k = kind == 2 ? 2.2f : (kind == 1 ? 1.0f : 0.7f) * clampf(strength, 0.6f, 1.6f);
  const bool flat = kind != 0 && a.mode != 2;
  const int n = static_cast<int>((kind == 2 ? 50 : (kind == 1 ? 26 : 16)) * std::min(1.4f, k));
  auto burst = [&](auto fn) {
    for (int i = 0; i < n; ++i) fn(around(a, kind == 0 ? 0.3f : 1.1f));
  };
  auto ringf = [&](float s1, float life, uint32_t c1, uint32_t c2, int shape) {
    ring(a, at + a.up * (a.r * 0.06f), a.up, a.r * 1.2f, a.r * s1 * k, life, c1, c2, shape, flat);
  };
  switch (skin) {
    case kSnow:
      burst([&](V3 d) { e_chunk(a, at + d * (a.r * 0.4f), d * (a.r * g_rng.f(6.0f, 11.0f) * k), 0xF4F8FF, 0xBFD8F0, g_rng.f(0.15f, 0.3f), 22.0f); });
      for (int i = 0; i < 10; ++i) { const V3 d = around(a, 0.2f); e_puff(a, at + d * (a.r * 0.5f), d * (a.r * 6.0f * k), a.r * 1.0f * k, a.r * 2.4f * k, 0.8f, 0xFFFFFF, 0xD8E8F8, 0.8f, 0.1f); }
      ringf(8.0f, 0.5f, 0xE8F8FF, 0x7AC8FF, kShRing);
      if (kind != 0) e_decal(a, at, 3.0f, 2.5f, 0xFFFFFF, 0xE8F0FA, false);
      break;
    case kLava:
      burst([&](V3 d) { e_drop(a, at + d * (a.r * 0.4f), d * (a.r * g_rng.f(7.0f, 12.0f) * k), 0xFFC050, 0xFF3A08, g_rng.f(0.25f, 0.45f), 1.0f); });
      for (int i = 0; i < (kind == 2 ? 14 : 7); ++i) { const V3 d = around(a, 0.8f); e_fire(a, at + d * (a.r * 0.5f), d * (a.r * 6.0f * k), a.r * 1.8f * k, 0.5f, 0.5f); }
      for (int i = 0; i < 6; ++i) e_smoke(a, at + around(a, 1.0f) * (a.r * 0.6f), a.up * (a.r * 2.0f) + around(a, 0.0f) * (a.r * 2.0f), a.r * 2.0f * k, a.r * 4.0f * k, 1.0f, 0x1E1A18, 0.85f);
      if (kind != 0) e_decal(a, at, 3.2f, 3.0f, 0xFF6A10, 0x1A120C, true);
      break;
    case kThunder:
      for (int i = 0; i < (kind == 2 ? 14 : 8); ++i) { const V3 d = around(a, 0.4f); e_bolt(a, at, d, g_rng.f(2.5f, 5.0f) * k, 0.14f); }
      burst([&](V3 d) { e_spark(a, at, d * (a.r * g_rng.f(8.0f, 14.0f) * k), 0.4f, 0xFFFFFF, 0x5AC8FF, 0.15f, 4.0f); });
      ringf(9.0f, 0.35f, 0xFFFFFF, 0x5AC8FF, kShRing);
      e_puff(a, at, V3{}, a.r * 3.0f * k, a.r * 6.0f * k, 0.15f, 0xFFFFFF, 0x7AD8FF, 0.9f, 1.0f);
      if (kind != 0) e_decal(a, at, 2.4f, 1.6f, 0x7FD8FF, 0x101420, true);
      break;
    case kGalaxy:
    case kHole:
      burst([&](V3 d) {
        if (skin == kHole) e_spark(a, at + d * (a.r * 4.0f * k), d * (-a.r * 9.0f * k), 0.45f, 0xFFE0B0, 0xB06AFF, 0.15f, 0.0f);  // (pulled in)
        else e_twinkle(a, at, d * (a.r * g_rng.f(6.0f, 12.0f) * k), g_rng.f(0.5f, 0.9f), pick(kDiscoC, 5), 0x9A6BFF, 0.5f);
      });
      ringf(10.0f, 0.6f, skin == kHole ? 0xC8A0FF : 0xD8C8FF, skin == kHole ? 0x3A0A6A : 0x6A3AFF, kShWave);
      ringf(6.0f, 0.4f, 0xFFFFFF, 0x9A6BFF, kShRing);
      e_puff(a, at, V3{}, a.r * 3.0f * k, a.r * 5.0f * k, 0.3f, 0xE8D8FF, 0x6A3AFF, 0.7f, 1.0f);
      break;
    case kMoon:
      for (int i = 0; i < 12; ++i) { const V3 d = around(a, 0.2f); e_puff(a, at + d * (a.r * 0.5f), d * (a.r * 5.0f * k) + a.up * (a.r * 0.5f), a.r * 1.0f * k, a.r * 2.6f * k, 1.1f, 0xC8CCD4, 0x7A7E88, 0.7f, 0.0f); }
      burst([&](V3 d) { e_chunk(a, at + d * (a.r * 0.3f), d * (a.r * g_rng.f(5.0f, 9.0f) * k), 0x9A9EA8, 0x5A5E68, g_rng.f(0.12f, 0.25f), 10.0f); });
      if (kind != 0) e_decal(a, at, 3.4f, 3.5f, 0x000000, 0x5A5A60, false);
      break;
    case kPixelB:
      e_pixel(a, at, V3{}, pick(kPixC, 7), 4.0f * k, kPixBurst, 0.35f);
      e_pixel(a, at, V3{}, 0xFFFFFF, 7.0f * k, kPixRing, 0.45f);
      burst([&](V3 d) { e_pixel(a, at, d * (a.r * g_rng.f(5.0f, 10.0f) * k), pick(kPixC, 7), g_rng.f(0.3f, 0.6f), kPixBlock, 0.6f); });
      break;
    case kNeon:
      ringf(9.0f, 0.45f, 0xFFFFFF, 0x00F0FF, kShRing);
      ring(a, at + a.up * (a.r * 0.08f), a.up, a.r * 0.8f, a.r * 6.0f * k, 0.55f, 0xFFFFFF, 0xFF2AD8, kShRing, flat);
      burst([&](V3 d) { e_streak(a, at, d * (a.r * g_rng.f(7.0f, 12.0f) * k), 2.5f, 0.3f, 0xFFFFFF, g_rng.next() & 1 ? 0x00F0FF : 0xFF2AD8, 1.0f); });
      break;
    case kDisco:
    case kDiamond:
      burst([&](V3 d) {
        if (skin == kDiamond && (g_rng.next() & 1)) e_chunk(a, at, d * (a.r * g_rng.f(6.0f, 11.0f) * k), 0xFFFFFF, pick(kRainbow, 7), g_rng.f(0.12f, 0.25f), 12.0f);
        else e_twinkle(a, at, d * (a.r * g_rng.f(5.0f, 11.0f) * k), g_rng.f(0.4f, 0.8f), skin == kDisco ? pick(kDiscoC, 5) : 0xFFFFFF, skin == kDisco ? 0xFFFFFF : pick(kRainbow, 7), 0.5f);
      });
      ringf(8.0f, 0.45f, 0xFFFFFF, skin == kDisco ? 0xFF7AD8 : 0x9ADFFF, kShRing);
      break;
    case kBasket:
    case kEight:
    case kBowl:
    case kLeather:
    case kEye:
    case kDuck: {
      const uint32_t dust = skin == kEight ? 0x6FA8FF : (skin == kEye ? 0xFFD8E0 : (skin == kDuck ? 0xCFEFFF : 0xD8C8A8));
      for (int i = 0; i < (skin == kBowl ? 14 : 9); ++i) {
        const V3 d = around(a, 0.2f);
        e_puff(a, at + d * (a.r * 0.5f), d * (a.r * 5.0f * k), a.r * 0.8f * k, a.r * (skin == kBowl ? 2.8f : 2.0f) * k, 0.7f, dust, dust & 0x7F7F7F, 0.6f, 0.05f);
      }
      ringf(skin == kBowl ? 10.0f : 6.0f, 0.4f, 0xFFFFFF, dust, skin == kBowl ? kShWave : kShRing);
      if (skin == kEye || skin == kDuck)
        burst([&](V3 d) { e_drop(a, at + d * (a.r * 0.4f), d * (a.r * g_rng.f(6.0f, 11.0f) * k), skin == kDuck ? 0xD8F4FF : 0xFFFFFF, skin == kDuck ? 0x2A7BFF : 0x9AD8FF, g_rng.f(0.2f, 0.35f), 0.3f); });
      if (skin == kDuck) for (int i = 0; i < 8; ++i) e_bubble(a, at + around(a, 1.0f) * (a.r * 0.6f), around(a, 1.0f) * (a.r * 2.0f), g_rng.f(0.3f, 0.6f), 1.2f);
      if (skin == kBowl) burst([&](V3 d) { e_chunk(a, at, d * (a.r * g_rng.f(5.0f, 9.0f) * k), 0xFFFFFF, 0xE83A3A, 0.2f, 18.0f); });
      break;
    }
    case kGold:
    case kChrome:
    case kBlack11: {
      const uint32_t c1 = skin == kChrome ? 0xFFFFFF : 0xFFF4C8, c2 = skin == kChrome ? 0xC8D0E0 : 0xE8B030;
      burst([&](V3 d) { e_spark(a, at, d * (a.r * g_rng.f(7.0f, 13.0f) * k), g_rng.f(0.5f, 0.9f), c1, c2, 0.15f, 10.0f); });
      for (int i = 0; i < 10; ++i) e_twinkle(a, at + around(a, 0.5f) * (a.r * g_rng.f(0.5f, 2.0f) * k), V3{}, 0.5f, c1, c2, 0.6f);
      ringf(8.0f, 0.45f, c1, c2, kShRing);
      if (skin == kBlack11)
        for (int i = 0; i < 8; ++i) e_smoke(a, at + around(a, 0.5f) * (a.r * 0.5f), around(a, 0.6f) * (a.r * 4.0f * k), a.r * 1.8f * k, a.r * 4.0f * k, 1.0f, 0x08080A, 0.9f);
      break;
    }
    case kBomb:
    case kCannon: {
      // a blast: real fire flung out, cooling to smoke; embers; the smoke rolling up; a scorch where it was on the ground
      const float b = skin == kBomb ? 1.4f : 0.9f;
      for (int i = 0; i < static_cast<int>((kind == 2 ? 26 : 12) * b); ++i) {
        const V3 d = around(a, 0.6f);
        P& q = fire_sprite(a, at + d * (a.r * 0.6f), d * (a.r * g_rng.f(5.0f, 10.0f) * k * b) + a.up * (a.r * 1.5f), a.r * 2.4f * k * b, g_rng.f(0.45f, 0.75f));
        q.size1 = q.size * 1.6f, q.orient = a.mode == 2 ? kScreen : kAlong, q.drag = 3.5f;
        q.temp0 = 1.0f, q.temp1 = 0.1f, q.smoke0 = 0.2f, q.smoke1 = 0.95f, q.bright = 0.55f;
      }
      for (int i = 0; i < 8; ++i) e_smoke(a, at + around(a, 1.0f) * (a.r * 0.8f), a.up * (a.r * 2.0f) + around(a, 0.0f) * (a.r * 3.0f * k), a.r * 2.5f * k * b, a.r * 5.5f * k * b, 1.3f, 0x3A3A40, 0.9f);
      burst([&](V3 d) { fire_ember(a, at + d * (a.r * 0.3f), d * (a.r * g_rng.f(8.0f, 15.0f) * k * b), g_rng.f(0.5f, 1.0f)); });
      fire_puff(a, at, a.r * 3.0f * k * b, a.r * 6.0f * k * b, 0.18f, 0xFFF0C0, 0xFF6A10, 0.8f);
      if (kind != 0) e_decal(a, at, 3.4f * b, 3.0f, 0xFF6A10, 0x14100C, true);
      break;
    }
    default: break;
  }
}
// the aura of the other skins (drawn where the ball is, every frame): `put` as write()'s
template <typename Put>
void aura_other(int skin, const Anchor& a, float t, Put put) {
  const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const uint32_t g = ballshade::skin_defs()[skin].glow;
  const float flick = 0.85f + 0.10f * std::sin(t * 23.0f) + 0.05f * std::sin(t * 37.0f);
  float al = 0.30f, sz = 3.0f;
  switch (skin) {
    case kLava: al = 0.42f * flick, sz = 3.8f; break;
    case kThunder: al = (0.3f + 0.35f * std::fabs(std::sin(t * 31.0f) * std::sin(t * 7.0f))), sz = 3.6f; break;
    case kGalaxy: al = 0.40f, sz = 3.6f; break;
    case kNeon: al = 0.38f, sz = 3.0f; break;
    case kBomb: al = 0.15f + 0.4f * std::pow(0.5f + 0.5f * std::sin(t * 25.0f), 4.0f), sz = 3.0f; break;
    case kHole: al = 0.45f, sz = 4.0f; break;
    case kSnow: case kMoon: case kChrome: case kDisco: al = 0.18f, sz = 2.6f; break;
    default: break;
  }
  (void)g, (void)al, (void)sz, (void)zero;
  return;  // r170: no glow / ring round a ball (Lance: Earth's "was OD ... made the ball harder to see") - events only
  if (g != 0) put(a.c, a.r * sz, zero, g, g & 0x3F3F3F, 160, kShPuff, 1.0f, al, 1.0f);
  if (skin == kNeon) put(a.c, a.r * 2.0f * 1.08f / 0.82f, zero, 0xFFFFFF, 0xFF2AD8, 255, kShRing, 1.0f, 0.55f, 1.0f);
  if (skin == kHole) {  // its accretion disc, flat round its middle
    float q[4];
    q_flat(a.up, t * 0.8f, q);
    put(a.c, a.r * 4.6f, q, 0xFFD8A0, 0x7A2AD8, 255, kShRing, 1.0f, 0.75f, 1.0f);
    put(a.c, a.r * 3.2f, q, 0xFFF0D0, 0xFF8A3A, 255, kShRing, 1.0f, 0.6f, 1.0f);
  }
}

// r175 (Lance on r174: "lets actually just stick to the ball itself, no ball trail or shit falling off the ball. maybe
// something when the ball impacts something"): every ball is its own surface only - no trail, nothing on or round it, no
// kick reaction. A bounce (1) / a goal (2) gets a small quick touch at the contact point: a soft flash, a few sparks in
// the ball's own colours, a little ripple on the ground - all gone within ~0.35 s. (fire_hit / earth_hit / hit_other stay
// in the file, unused: r170's.)
void hit_small(int skin, const Anchor& a, V3 at, int kind) {
  if (skin <= 0 || kind == 0) return;
  const Item& it = cat()[static_cast<size_t>(std::min(skin, static_cast<int>(cat().size()) - 1))];
  const float k = kind == 2 ? 1.5f : 1.0f;
  const bool flat = a.mode != 2;
  ring(a, at + a.up * (a.r * 0.04f), a.up, a.r * 0.8f, a.r * 3.2f * k, 0.3f, 0xFFFFFF, it.rgb1, kShRing, flat);
  for (int i = 0; i < (kind == 2 ? 10 : 6); ++i) {
    const V3 d = around(a, 1.0f);
    P& q = spawn();
    q.p = at + d * (a.r * 0.2f), q.v = d * (a.r * g_rng.f(4.0f, 7.0f) * k), q.up = a.up, q.shape = kShSpark;
    q.size = std::max(0.002f, a.r * 0.14f), q.size1 = q.size * 0.3f, q.life = g_rng.f(0.2f, 0.34f), q.fadeOut = 0.15f;
    q.glow = 1.0f, q.c1 = 0xFFFFFF, q.c2 = (i & 1) ? it.rgb1 : it.rgb2, q.drag = 3.0f, q.grav = a.r * 10.0f, q.floorY = a.groundY;
  }
  P& f = spawn();  // (a soft flash at the touch)
  f.p = at + a.up * (a.r * 0.3f), f.up = a.up, f.shape = kShPuff, f.size = a.r * 1.6f * k, f.size1 = a.r * 2.4f * k;
  f.life = 0.18f, f.glow = 1.0f, f.c1 = 0xFFFFFF, f.c2 = it.rgb1, f.peak = 0.45f, f.fadeIn = 0.02f, f.fadeOut = 0.12f;
}
void hit_any(int skin, const Anchor& a, V3 at, int kind, float k) {  // r170; r175: the small touch only
  (void)k;
  hit_small(skin, a, at, kind);
}

// ------------------------------------------------------------------------------------------------------------------
// Where the ball is in this view
// ------------------------------------------------------------------------------------------------------------------
Anchor anchor_of(const xrfx::Scene& sc, const PortBallFeed& f, float lead) {
  Anchor a;
  if (!f.visible) return a;
  const float gx = f.pos[0] + f.vel[0] * lead, gy = f.pos[1] + f.vel[1] * lead, gz = f.pos[2] + f.vel[2] * lead;
  if (sc.tabletop) {
    a.mode = 0;
    a.unit = sc.tableScale;
    a.c = game_to_room(sc.tableScale, sc.tableYaw, sc.tablePos, gx, gy, gz);
    a.vel = game_dir_to_room(sc.tableScale, sc.tableYaw, f.vel[0], f.vel[1], f.vel[2]);
    a.r = std::max(0.003f, f.radius * sc.tableScale);
    a.groundY = sc.tablePos.y;
    a.ok = true;
  } else if (!sc.mr && !sc.suite && !sc.roomShown && !sc.screenValid) {
    a.mode = 1;
    a.unit = sc.stadiumScale;
    a.c = game_to_room(sc.stadiumScale, 0.0f, sc.stadiumOrigin, gx, gy, gz);
    a.vel = game_dir_to_room(sc.stadiumScale, 0.0f, f.vel[0], f.vel[1], f.vel[2]);
    a.r = std::max(0.02f, f.radius * sc.stadiumScale);
    a.groundY = sc.stadiumOrigin.y;
    a.ok = true;
  } else if (sc.screenValid && f.ndcValid) {
    a.mode = 2;
    const XrPosef& p = sc.screenPose;
    const V3 S1 = qrot(p.orientation, V3{1.0f, 0.0f, 0.0f}), S2 = qrot(p.orientation, V3{0.0f, 1.0f, 0.0f});
    a.out = qrot(p.orientation, V3{0.0f, 0.0f, 1.0f});
    a.up = S2;
    const float nx = clampf(f.ndc[0], -1.0f, 1.0f), ny = clampf(f.ndc[1], -1.0f, 1.0f);
    a.c = v3(p.position) + S1 * (nx * 0.5f * sc.screenWidth) + S2 * (ny * 0.5f * sc.screenHeight) + a.out * 0.006f;
    a.r = clampf(f.ndcRadius * 0.5f * sc.screenHeight, 0.004f, 0.08f);
    a.unit = a.r / std::max(0.05f, f.radius);
    // (its velocity in the picture: from where it was a moment ago, in the picture's plane)
    a.vel = V3{};
    a.ok = std::fabs(f.ndc[0]) < 1.05f && std::fabs(f.ndc[1]) < 1.05f;
  }
  return a;
}
V3 hit_point(const xrfx::Scene& sc, const Anchor& a, const float gp[3], int ndcValid, const float ndc[2]) {
  if (a.mode == 0) return game_to_room(sc.tableScale, sc.tableYaw, sc.tablePos, gp[0], gp[1], gp[2]);
  if (a.mode == 1) return game_to_room(sc.stadiumScale, 0.0f, sc.stadiumOrigin, gp[0], gp[1], gp[2]);
  if (a.mode == 2 && ndcValid) {
    const XrPosef& p = sc.screenPose;
    const V3 S1 = qrot(p.orientation, V3{1.0f, 0.0f, 0.0f}), S2 = qrot(p.orientation, V3{0.0f, 1.0f, 0.0f});
    return v3(p.position) + S1 * (clampf(ndc[0], -1.0f, 1.0f) * 0.5f * sc.screenWidth) +
           S2 * (clampf(ndc[1], -1.0f, 1.0f) * 0.5f * sc.screenHeight) + a.out * 0.008f;
  }
  return a.c;
}

// the aura and the trail round the ball this frame (spawned), dt seconds
void live(int skin, const Anchor& a, float dt, bool preview) {
  // r175: nothing on, off or round the ball - its own surface is the whole look (Lance: "no ball trail or shit falling
  // off the ball"). The r168-r170 emitters below are kept unused.
  (void)skin, (void)a, (void)dt, (void)preview;
  if (skin >= 0) return;
  const float sp = len(a.vel) / std::max(1e-4f, a.r);  // (ball radii a second)
  const bool flying = sp > (preview ? 1.0f : 25.0f);
  if (skin == PORT_BALL_FIREBALL) {
    g_s.tongueAcc += dt * (flying ? 42.0f : 24.0f);  // r169
    while (g_s.tongueAcc >= 1.0f) {
      g_s.tongueAcc -= 1.0f;
      fire_tongue(a, false);
    }
    if (flying) {
      g_s.trailAcc += dt * std::min(60.0f, 14.0f + sp * 0.5f);  // r169
      while (g_s.trailAcc >= 1.0f) {
        g_s.trailAcc -= 1.0f;
        fire_tongue(a, true);  // (r169: real fire torn off it, going to smoke - no cartoon puffs)
      }
    }
    g_s.emberAcc += dt * (flying ? 26.0f : 6.0f);
    while (g_s.emberAcc >= 1.0f) {
      g_s.emberAcc -= 1.0f;
      const V3 d = around(a, 1.0f);
      fire_ember(a, a.c + d * (a.r * 0.9f), a.vel * 0.3f + d * (a.r * g_rng.f(1.5f, 4.0f)) + a.up * (a.r * 3.0f),
                 g_rng.f(0.5f, 1.0f));
    }
  } else if (skin >= 3) {
    live_other(skin, a, dt, flying, sp);  // r170
  } else if (skin == PORT_BALL_EARTH) {
    if (flying) {
      g_s.trailAcc += dt * std::min(50.0f, 10.0f + sp * 0.45f);
      while (g_s.trailAcc >= 1.0f) {
        g_s.trailAcc -= 1.0f;
        earth_wisp(a);
        P& q = spawn();  // (a soft blue puff of its atmosphere left behind)
        q.p = a.c - norm(a.vel) * (a.r * g_rng.f(0.0f, 1.0f)), q.v = a.vel * 0.05f, q.up = a.up;
        q.shape = kShPuff, q.size = a.r * 2.0f, q.size1 = a.r * 3.0f, q.life = g_rng.f(0.3f, 0.45f), q.peak = 0.2f, q.glow = 1.0f;  // (r170: subtler)
        q.c1 = 0x9AD8FF, q.c2 = 0x2A5BFF, q.c2a = static_cast<uint8_t>(g_rng.next() & 0xFF), q.fadeOut = 0.3f, q.drag = 2.0f;
      }
    }
    g_s.cloudAcc += dt * (flying ? 8.0f : 0.0f);  // r170: in flight only
    while (g_s.cloudAcc >= 1.0f) {
      g_s.cloudAcc -= 1.0f;
      const V3 d = around(a, 0.2f);
      earth_cloud(a, a.c + d * (a.r * 1.05f), a.vel * 0.5f + d * (a.r * 0.8f), a.r * 0.25f, a.r * 0.6f, g_rng.f(0.6f, 1.0f), 0.7f);
    }
  }
}

void step_parts(float dt, V3 head) {
  (void)head;
  for (size_t i = 0; i < g_parts.size();) {
    P& q = g_parts[i];
    q.age += dt;
    if (q.age >= q.life) {
      q = g_parts.back();
      g_parts.pop_back();
      continue;
    }
    if (q.drag > 0.0f) q.v = q.v * std::exp(-q.drag * dt);
    if (q.grav != 0.0f) q.v = q.v - q.up * (q.grav * dt);
    q.p = q.p + q.v * dt;
    if (q.floorY > -1e8f && q.p.y < q.floorY) {  // (landed: dies out quickly there)
      q.p.y = q.floorY;
      q.v = V3{};
      q.life = std::min(q.life, q.age + 0.12f);
    }
    ++i;
  }
}

}  // namespace

// ------------------------------------------------------------------------------------------------------------------
// The shop's side
// ------------------------------------------------------------------------------------------------------------------
const std::vector<Item>& catalogue() { return cat(); }
int find(const char* id) {
  if (id == nullptr) return -1;
  for (size_t i = 0; i < cat().size(); ++i)
    if (std::strcmp(cat()[i].id, id) == 0) return static_cast<int>(i);
  return -1;
}
bool owned(const char* id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  read_l();
  return owned_l(id);
}
void add_owned(const char* id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  read_l();
  if (find(id) > 0 && !owned_l(id)) g_owned.push_back(id);
  ++g_gen;
}
bool equip(const char* id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  read_l();
  if (!owned_l(id)) return false;
  if (g_on != id) BLOG("balls: %s on", id);
  g_on = id;
  ++g_gen;
  return true;
}
const char* equipped() {
  std::lock_guard<std::mutex> lock(g_mutex);
  read_l();
  static thread_local std::string out;
  out = g_on;
  return out.c_str();
}
void load_line(const char* key, const char* value) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_cardsRead && g_fileRead) {  // (the cards read it now: start from theirs, not this file's earlier read)
    g_owned.clear();
    g_on = "classic";
    g_fileRead = false;
    g_cardsRead = true;
  }
  g_cardsRead = true;
  line_l(key, value);
}
void loaded() {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (!g_cardsRead) {  // (a file with no ball lines at all)
    g_owned.clear();
    g_on = "classic";
  }
  g_cardsRead = true;
  ++g_gen;
}
std::string save_lines() {
  std::lock_guard<std::mutex> lock(g_mutex);
  read_l();
  std::string out = "ball " + g_on + "\n";
  for (const std::string& o : g_owned) out += "ball_owned " + o + "\n";
  return out;
}
int generation() { return g_gen.load(); }

std::vector<uint8_t> tile_rgba(int i, int size) {
  // r170: any skin - its surface (ball_shade.h, its first frame), lit from the top left (or its own light), turned so its
  // face reads (the 8, the eye, the duck's face, the holes look out at +X), its glow round it
  std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4, 0);
  const int skin = i < 0 || i >= ballshade::kSkinCount ? 0 : i;
  const ballshade::SkinDef& def = ballshade::skin_defs()[skin];
  const float R = 0.36f * size, cx = 0.5f * size, cy = 0.5f * size;
  const ballshade::V L = ballshade::v(-0.45f, 0.55f, 0.70f);
  const float tilt = 0.42f, ct = std::cos(tilt), st = std::sin(tilt), yaw = -1.15f, cyw = std::cos(yaw), syw = std::sin(yaw);
  const int frame = def.frames > 2 ? def.frames / 3 : 0;
  for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x) {
      const float dx = (x + 0.5f - cx) / R, dy = (cy - (y + 0.5f)) / R;
      const float r2 = dx * dx + dy * dy;
      uint8_t* o = &px[(static_cast<size_t>(y) * size + x) * 4];
      float rgb[3] = {0, 0, 0}, alpha = 0.0f;
      if (r2 <= 1.0f) {
        const float dz = std::sqrt(1.0f - r2);
        const float vx = dx, vy = dy * ct + dz * st, vz = -dy * st + dz * ct;
        const ballshade::V n = ballshade::v(vx * cyw - vz * syw, vx * syw + vz * cyw, vy);  // (Z up: its pole)
        const ballshade::V c = ballshade::shade_frame(skin, n, frame);
        const float lam = std::max(0.0f, dx * L.x + dy * L.y + dz * L.z);
        const float spec = std::pow(std::max(0.0f, dx * -0.3f + dy * 0.4f + dz * 0.86f), 40.0f) * 0.35f;
        const float lit = def.emissive ? 0.85f + 0.3f * std::sqrt(dz) : 0.22f + 0.9f * lam;
        rgb[0] = c.x * lit + (def.emissive ? 0.0f : spec), rgb[1] = c.y * lit + (def.emissive ? 0.0f : spec), rgb[2] = c.z * lit + (def.emissive ? 0.0f : spec);
        alpha = clampf((1.0f - std::sqrt(r2)) * R / 1.2f, 0.0f, 1.0f);
      }
      const float d = std::sqrt(r2);
      if (d > 0.98f && def.glow != 0) {
        const float ang = std::atan2(dy, dx);
        const float lick = ballshade::fbm(ballshade::v(std::cos(ang) * 2.0f, std::sin(ang) * 2.0f, 1.7f), 3);
        const float g = clampf(std::exp(-(d - 1.0f) * 6.0f) * (0.55f + 0.7f * lick), 0.0f, 1.0f);
        const uint32_t col = def.glow;
        const float cr = ((col >> 16) & 0xFF) / 255.0f, cg = ((col >> 8) & 0xFF) / 255.0f, cb = (col & 0xFF) / 255.0f;
        const float a2 = alpha + g * (1.0f - alpha);
        if (a2 > 1e-4f) {
          rgb[0] = (rgb[0] * alpha + cr * g * (1.0f - alpha)) / a2;
          rgb[1] = (rgb[1] * alpha + cg * g * (1.0f - alpha)) / a2;
          rgb[2] = (rgb[2] * alpha + cb * g * (1.0f - alpha)) / a2;
        }
        alpha = a2;
      }
      for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(clampf(rgb[k], 0.0f, 1.0f) * 255.0f + 0.5f);
      o[3] = static_cast<uint8_t>(clampf(alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
  return px;
}

// r170: the shop preview's frames - the skin's own bake (ball_shade.h), 32 cells of 128 x 64 in an 8 x 4 atlas (fewer
// frames: each held over several cells), baked on a thread and handed to the effects layer (xr_fx.frag 160)
std::atomic<int> g_atlasSkin{-1};   // the skin whose frames are up (or going up)
std::atomic<bool> g_atlasBaking{false};
void bake_atlas(int skin) {
  int expect = -1;
  (void)expect;
  if (g_atlasSkin.load() == skin || g_atlasBaking.exchange(true)) return;
  std::thread([skin] {
    constexpr int kW = 128, kH = 64, kCols = 8, kRows = 4, kCells = kCols * kRows;
    const ballshade::SkinDef& def = ballshade::skin_defs()[skin];
    const int nf = std::min(kCells, std::max(1, def.frames));
    std::vector<uint8_t> px(static_cast<size_t>(kW) * kCols * kH * kRows * 4, 255);
    std::vector<ballshade::V> dirs(static_cast<size_t>(kW) * kH);
    for (int j = 0; j < kH; ++j)
      for (int i = 0; i < kW; ++i) dirs[static_cast<size_t>(j) * kW + i] = ballshade::texel_dir(i, j, kW, kH);
    for (int c = 0; c < kCells; ++c) {
      const int f = (c % nf) * def.frames / nf;
      const int ox = (c % kCols) * kW, oy = (c / kCols) * kH;
      for (int j = 0; j < kH; ++j)
        for (int i = 0; i < kW; ++i) {
          const ballshade::V col = c < nf ? ballshade::shade_frame(skin, dirs[static_cast<size_t>(j) * kW + i], f)
                                          : ballshade::V{0, 0, 0};
          uint8_t* o = &px[(static_cast<size_t>(oy + j) * kW * kCols + ox + i) * 4];
          o[0] = static_cast<uint8_t>(col.x * 255.0f + 0.5f), o[1] = static_cast<uint8_t>(col.y * 255.0f + 0.5f);
          o[2] = static_cast<uint8_t>(col.z * 255.0f + 0.5f);
        }
    }
    xrfx::set_ball_atlas(std::move(px), kW * kCols, kH * kRows);
    g_atlasSkin = skin;
    g_atlasBaking = false;
    BLOG("balls: r170 preview frames for %s (%d)", def.id, nf);
  }).detach();
}

void preview(const char* id, const XrPosef& where) {
  const int i = find(id);
  std::lock_guard<std::mutex> lock(g_previewMutex);
  g_previewAsk = true;
  g_previewStop = false;
  g_previewAskSkin = i < 0 ? 0 : i;
  g_previewAskWhere = where;
  g_previewing = true;
  BLOG("balls: preview %s", id != nullptr ? id : "?");
}void stop_preview() {
  std::lock_guard<std::mutex> lock(g_previewMutex);
  g_previewStop = true;
  g_previewAsk = false;
  g_previewing = false;
}
bool previewing() { return g_previewing.load(); }

void set_friend_equipped(const char* id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::string v = id != nullptr && find(id) >= 0 ? id : "";
  if (v != g_friend) BLOG("balls: the friend's ball: %s", v.empty() ? "(none)" : v.c_str());
  g_friend = v;
}
const char* friend_equipped() {
  std::lock_guard<std::mutex> lock(g_mutex);
  static thread_local std::string out;
  out = g_friend;
  return out.c_str();
}

// ------------------------------------------------------------------------------------------------------------------
// The game's side
// ------------------------------------------------------------------------------------------------------------------
bool update(const xrfx::Scene& sc) {
  State& s = g_s;
  const float dt = clampf(sc.dt, 0.0f, 0.1f);
  s.t += dt;
  if (g_parts.capacity() < kMaxParts) g_parts.reserve(kMaxParts);
  // which skins the game wears (yours; online, the friend's for their side's touches)
  int mine = 0, theirs = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    read_l();
    mine = skin_index(g_on);
    theirs = skin_index(g_friend);
  }
  const bool online = xrnet::lockstep();
  PortBallSkinSetIds(mine, theirs, xrhome::human_side(), online ? 1 : 0);
  if (mine != s.sentMine || theirs != s.sentTheirs) {
    s.sentMine = mine, s.sentTheirs = theirs;
    BLOG("balls: wearing %s (the friend's %s)", cat()[static_cast<size_t>(mine)].id, theirs > 0 ? cat()[static_cast<size_t>(theirs)].id : "none");
  }
  // the preview asked / stopped
  {
    std::lock_guard<std::mutex> lock(g_previewMutex);
    if (g_previewStop) {
      g_previewStop = false;
      s.previewOn = false;
      for (P& q : g_parts) q.life = std::min(q.life, q.age + 0.25f);
    }
    if (g_previewAsk) {
      g_previewAsk = false;
      s.previewOn = true;
      s.previewSkin = g_previewAskSkin;
      s.where = g_previewAskWhere;
      s.previewAge = 0.0f;
      s.lastHop = 0.0f;
      g_parts.clear();
    }
  }
  const V3 head = head_of(sc);
  if (s.previewOn) {
    s.previewAge += dt;
    // the ball in front of you: bobbing, a hop every 3 s onto an invisible floor 0.16 m under it, turning
    const float hopT = std::fmod(s.previewAge, 3.0f);
    const float hop = hopT < 0.6f ? 0.0f : std::sin((hopT - 0.6f) / 2.4f * 3.14159265f);
    Anchor a;
    a.ok = true, a.mode = 3, a.r = 0.11f;
    a.c = v3(s.where.position) + V3{0.0f, 0.16f * hop - 0.05f, 0.0f};
    const float hopV = hopT < 0.6f ? 0.0f : 0.16f * std::cos((hopT - 0.6f) / 2.4f * 3.14159265f) * (3.14159265f / 2.4f);
    const float orbit = s.previewAge * 0.9f;
    a.vel = V3{-std::sin(orbit) * 0.35f, hopV, std::cos(orbit) * 0.35f};
    a.groundY = v3(s.where.position).y - 0.05f - a.r - 0.0f;
    s.a = a;
    if (s.previewSkin > 0) {
      live(s.previewSkin, a, dt, true);
      if (hopT < s.lastHop) {  // (it touched down)
        const V3 at = V3{a.c.x, a.groundY, a.c.z};
        hit_any(s.previewSkin, a, at, 1, 1.0f);  // r170
      }
    }
    s.lastHop = hopT;
    // its turn: tipped 23 degrees, round its pole
    const float spinS = ballshade::skin_defs()[s.previewSkin].spin;  // r170: its own turn, or a slow one to show it round
    const float spin = 2.0f * ballshade::kPi * s.previewAge / (spinS > 0.0f ? std::min(spinS, 8.0f) : 6.0f);
    if (g_atlasSkin.load() != s.previewSkin && !g_atlasBaking.load()) bake_atlas(s.previewSkin);  // r170
    const float tilt = 0.40f;
    // (the ball's own frame: its pole = world up tipped toward you; xr_fx.frag turns your view into it with this quat)
    const float qt[4] = {std::sin(tilt * 0.5f), 0.0f, 0.0f, std::cos(tilt * 0.5f)};
    const float qs[4] = {0.0f, std::sin(spin * 0.5f), 0.0f, std::cos(spin * 0.5f)};
    // q = qt * qs
    s.ballQ[3] = qt[3] * qs[3] - qt[0] * qs[0] - qt[1] * qs[1] - qt[2] * qs[2];
    s.ballQ[0] = qt[3] * qs[0] + qt[0] * qs[3] + qt[1] * qs[2] - qt[2] * qs[1];
    s.ballQ[1] = qt[3] * qs[1] - qt[0] * qs[2] + qt[1] * qs[3] + qt[2] * qs[0];
    s.ballQ[2] = qt[3] * qs[2] + qt[0] * qs[1] - qt[1] * qs[0] + qt[2] * qs[3];
  } else {
    // the game's ball
    PortBallFeed f{};
    const bool have = PortBallSkinFeed(&f) != 0;
    const uint64_t now = mono_ns();
    if (have && f.frame != s.lastFrame) {
      s.lastFrame = f.frame;
      s.feedNs = now;
    }
    const float since = static_cast<float>(now - s.feedNs) * 1e-9f;
    const float lead = clampf(since, 0.0f, 0.05f);  // (between the game's frames)
    s.prevA = s.a;
    // (no ball drawn for a quarter second - out of the match, a loading screen: nothing follows it)
    s.a = have && s.feedNs != 0 && since < 0.25f ? anchor_of(sc, f, lead) : Anchor{};
    if (s.a.ok && s.a.mode == 2 && s.prevA.ok && s.prevA.mode == 2 && dt > 1e-4f) {
      // (in the picture: how it moves on the screen - smoothed, the picture moves at the game's frame rate)
      V3 meas = (s.a.c - s.prevA.c) * (1.0f / dt);
      if (len(meas) > 20.0f) meas = V3{};
      s.a.vel = s.prevA.vel + (meas - s.prevA.vel) * 0.25f;
    }
    s.skinShown = have ? f.skin : 0;
    if (have) {
      if (!s.countsKnown) {
        s.kicks = f.kicks, s.bounces = f.bounces, s.goals = f.goals;
        s.countsKnown = true;
      }
      if (s.skinShown > 0 && s.a.ok) {
        live(s.skinShown, s.a, dt, false);
        if (f.kicks != s.kicks) {
          const V3 at = hit_point(sc, s.a, f.kickPos, f.kickNdcValid, f.kickNdc);
          const float k = clampf(f.kickSpeed / 18.0f, 0.6f, 1.6f);
          hit_any(s.skinShown, s.a, at, 0, k);  // r170
        }
        if (f.bounces != s.bounces) {
          const V3 at = hit_point(sc, s.a, f.bouncePos, f.bounceNdcValid, f.bounceNdc);
          const float k = clampf(f.bounceSpeed / 6.0f, 0.6f, 1.6f);
          hit_any(s.skinShown, s.a, at, 1, k);  // r170
        }
        if (f.goals != s.goals) {
          const V3 at = hit_point(sc, s.a, f.goalPos, f.goalNdcValid, f.goalNdc);
          hit_any(s.skinShown, s.a, at, 2, 1.0f);  // r170
        }
      }
      s.kicks = f.kicks, s.bounces = f.bounces, s.goals = f.goals;
    }
  }
  world_take();  // r179: the cards' 1/1 worlds' real effects asked since the last frame
  if (!g_parts.empty()) step_parts(dt, head);
  return pieces_up();
}

bool pieces_up() {
  const State& s = g_s;
  return !g_parts.empty() || s.previewOn;  // (r170: no always-on aura: only pieces need the layer)
}

uint32_t write(const xrfx::Scene& sc, xrgoalfx::Piece* out, uint32_t maxPieces) {
  State& s = g_s;
  uint32_t n = 0;
  const V3 head = head_of(sc);
  auto put = [&](V3 p, float size, const float q[4], uint32_t c1, uint32_t c2, uint8_t c2a, int shape, float aspect, float alpha,
                 float glow) {
    if (n >= maxPieces || alpha <= 0.003f || size <= 0.0f) return;
    xrgoalfx::Piece& o = out[n++];
    o.posSize[0] = p.x, o.posSize[1] = p.y, o.posSize[2] = p.z, o.posSize[3] = size;
    for (int k = 0; k < 4; ++k) o.quat[k] = q != nullptr ? q[k] : 0.0f;
    o.color1 = pack(c1, 255);
    o.color2 = pack(c2, c2a);
    o.params[0] = static_cast<float>(shape);
    o.params[1] = aspect;
    o.params[2] = alpha;
    o.params[3] = glow;
  };
  const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  const int skin = s.previewOn ? s.previewSkin : s.skinShown;
  const Anchor& a = s.a;
  // the preview's ball itself (per pixel: xr_fx.frag 160 from its baked frames - r170: any skin; its turn in the
  // quaternion, where it is in its frames in the glow slot, its own light / how many frames in colour 1, its glow colour 2)
  if (s.previewOn && a.ok && g_atlasSkin.load() == s.previewSkin && n < maxPieces) {
    const ballshade::SkinDef& def = ballshade::skin_defs()[s.previewSkin];
    const int nf = std::min(32, std::max(1, def.frames));
    xrgoalfx::Piece& o = out[n++];
    o.posSize[0] = a.c.x, o.posSize[1] = a.c.y, o.posSize[2] = a.c.z, o.posSize[3] = 2.0f * a.r / 0.62f;
    std::memcpy(o.quat, s.ballQ, sizeof(o.quat));
    o.color1 = pack((def.emissive ? 0xFF0000u : 0u) | (static_cast<uint32_t>(nf) << 8), 255);
    o.color2 = pack(def.glow, 0);  // (r170: no halo round it - the ball itself, clean)
    o.params[0] = 160.0f, o.params[1] = 1.0f, o.params[2] = 1.0f;
    o.params[3] = std::fmod(s.previewAge / std::max(0.05f, def.loop), 1.0f) * static_cast<float>(nf);
  }  // the aura (not pieces: drawn where the ball is, every frame)
  if (skin >= 3 && a.ok) aura_other(skin, a, s.t, put);  // r170
  // r170 (Lance on r168's EARTH: its always-on light-blue ring "was OD ... almost made the ball harder to see"): no glow,
  // halo or ring round any ball - FIREBALL's tongues are its own fire (live()), the rest come with flight and hits
  // the pieces
  for (const P& q : g_parts) {
    if (n >= maxPieces) break;
    float alpha = q.peak;
    const float u = q.age / std::max(0.01f, q.life);
    if (q.ringFade) {
      alpha *= std::max(0.0f, 1.0f - u);
    } else {
      if (q.fadeIn > 0.0f) alpha *= std::min(1.0f, q.age / q.fadeIn);
      alpha *= clampf((q.life - q.age) / std::max(0.02f, q.fadeOut), 0.0f, 1.0f);
    }
    // (nothing in your face)
    const float d = len(q.p - head);
    alpha *= clampf((d - 0.12f) / 0.2f, 0.0f, 1.0f);
    const float size = q.size + (q.size1 - q.size) * (q.ringFade ? 1.0f - (1.0f - u) * (1.0f - u) : u);
    float qq[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    switch (q.orient) {
      case kFace: break;
      case kUpright: q_along(q.p, q.up, head, qq); break;
      case kAlong: {
        const float sp = len(q.v);
        if (sp > 1e-5f) q_along(q.p, q.v, head, qq);
        else q_along(q.p, q.up, head, qq);
        break;
      }
      case kFlat: q_flat(q.up, q.roll, qq); break;
      case kAxis: q_along(q.p, q.axis, head, qq); break;  // r169
      case kScreen: {
        // on the big screen: in the picture's plane, upright in it (its up: q.up)
        const V3 y = norm(q.up);
        V3 z = head - q.p;
        z = norm(z - y * dot(z, y), V3{0.0f, 0.0f, 1.0f});
        quat_from_basis(norm(cross(y, z)), y, z, qq);
        break;
      }
    }
    uint8_t c2a = q.c2a;
    uint32_t c1 = q.c1;
    if (q.shape == kShFire) {  // r169: the flipbook's frame, its temperature / smoke / brightness now
      const int f = ((q.frame0 & 15) + static_cast<int>(q.age * q.fps)) & 15;
      c2a = static_cast<uint8_t>(f | (q.frame0 & 16));
      const float tp = q.temp0 + (q.temp1 - q.temp0) * u, sm = q.smoke0 + (q.smoke1 - q.smoke0) * u;
      c1 = (static_cast<uint32_t>(clampf(tp, 0.0f, 1.0f) * 255.0f) << 16) | (static_cast<uint32_t>(clampf(sm, 0.0f, 1.0f) * 255.0f) << 8) |
           static_cast<uint32_t>(clampf(q.bright, 0.0f, 1.0f) * 255.0f);
    }
    if (q.heat) c2a = static_cast<uint8_t>(clampf(1.0f - u * 1.4f, 0.0f, 1.0f) * 255.0f);  // (the scorch cools)
    put(q.p, size, qq, c1, q.c2, c2a, q.shape, q.aspect, alpha, q.glow);
  }
  return n;
}

// ------------------------------------------------------------------------------------------------------------------
// r179 (Lance on his BLACK 1/1 Combusken: "the flames on the card looked childish"): the cards' 1/1 worlds borrow these
// real effects - the r169 fire flipbook, its embers and smoke, crackling bolts, drops, bubbles, glints, mist - spawned
// into this pool (they live and draw as the ball's own). Queued from the cards (game thread), taken at the next update.
// ------------------------------------------------------------------------------------------------------------------
namespace {
struct WorldAsk {
  int kind;
  V3 at, v, up, axis;
  float size, life;
  uint32_t c1, c2;
};
std::mutex g_worldMutex;
std::vector<WorldAsk> g_worldAsks;
void world_spawn(const WorldAsk& w) {
  Anchor a;
  a.ok = true, a.r = 1.0f, a.up = w.up;
  switch (w.kind) {
    case kWxFlame: {  // a real flame standing up, cooling to smoke as it rises
      P& q = e_fire(a, w.at, w.v, w.size, w.life, 0.5f);
      q.size1 = w.size * 0.55f, q.orient = kUpright;
      q.temp0 = 1.0f, q.temp1 = 0.25f, q.smoke0 = 0.1f, q.smoke1 = 0.55f;
      break;
    }
    case kWxEmber:  // a hot ember drifting up and cooling
      e_spark(a, w.at, w.v, w.life, w.c1, w.c2, w.size, -0.15f);
      break;
    case kWxSmoke:  // soft rolling smoke (the flipbook gone cold) in its colour
      e_smoke(a, w.at, w.v, w.size, w.size * 2.2f, w.life, w.c1, 0.75f);
      break;
    case kWxBolt: {  // a crackling bolt along `axis`, `size` long
      P& q = spawn();
      q.p = w.at + w.axis * (w.size * 0.5f), q.up = w.up, q.shape = kShBolt, q.orient = kAxis, q.axis = w.axis;
      q.size = w.size, q.size1 = w.size, q.aspect = 0.22f, q.life = w.life, q.glow = 1.0f, q.fadeIn = 0.0f, q.fadeOut = w.life * 0.7f;
      q.c1 = w.c1, q.c2 = w.c2;
      break;
    }
    case kWxDrop:
      e_drop(a, w.at, w.v, w.c1, w.c2, w.size, 0.35f);
      break;
    case kWxBubble: {
      P& q = spawn();
      q.p = w.at, q.v = w.v, q.up = w.up, q.shape = kShBubble, q.size = w.size, q.size1 = w.size * 1.2f, q.life = w.life, q.glow = 0.5f;
      q.c1 = w.c1, q.c2 = w.c2, q.fadeOut = 0.2f, q.drag = 1.2f, q.grav = -0.06f;
      break;
    }
    case kWxGlint:
      e_twinkle(a, w.at, w.v, w.life, w.c1, w.c2, w.size);
      break;
    default:  // kWxMist: a soft lit puff
      e_puff(a, w.at, w.v, w.size, w.size * 2.0f, w.life, w.c1, w.c2, 0.45f, 0.35f);
      break;
  }
}
}  // namespace

void world_fx(int kind, const XrVector3f& at, const XrVector3f& v, const XrVector3f& up, float size, float life, uint32_t c1,
              uint32_t c2, const XrVector3f& axis) {
  std::lock_guard<std::mutex> lock(g_worldMutex);
  if (g_worldAsks.size() > 256) return;  // (never more than the pool takes)
  g_worldAsks.push_back(WorldAsk{kind, v3(at), v3(v), v3(up), v3(axis), size, life, c1, c2});
}

void world_take() {
  std::vector<WorldAsk> asks;
  {
    std::lock_guard<std::mutex> lock(g_worldMutex);
    asks.swap(g_worldAsks);
  }
  for (const WorldAsk& w : asks) world_spawn(w);
}

}  // namespace xrballs

#else  // not the Quest's OpenXR build: no headset effects (the game's side, ball_skin.cpp, still builds everywhere)

#endif
