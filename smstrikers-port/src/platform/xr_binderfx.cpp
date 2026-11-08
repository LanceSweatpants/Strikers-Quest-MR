// See xr_binderfx.h. r163 (Lance: "heavyyyyy on every special skin is moving and alive"; "theres gotta be an alternative
// solution ... that would enhance something glowing like the lava binder"; the slime "constantly dripping from it ... just
// a state of animation"). Quest has no bloom, so glow is built in layers: the hot emissive core in the skin's shader
// (xr_fx_model.frag binder_main), soft halos of light hugging its glowing outline and faces, a pool of its light on the
// floor under it (quads here, drawn by xr_fx_model.frag bfx_main: ModelDraw::under -7.5) and its particles - embers rising
// and lava dripping off lava, slime drops forming on its lowest edge / crawling down its faces, stretching, falling and
// splatting on the floor, cold mist sliding off ice, stars twinkling round the galaxy, glints riding the metals' sweep,
// smoke puffing off dragon scales. And its fur moves: swaying as the binder moves and turns, parting where a hand brushes
// it and springing back (its shell vertices moved); slime's and the puffer's covers wobble / breathe (r163 fixes: in
// xr_fx_model.vert - the binder's poses are baked, only a furry skin's vertices come here each frame).
// tools/outside/binders163/sim163.py ports this line for line (the PC proof videos, living163.py).
//
// GPU (Quest 3, per eye): at most ~80 quads; the big ones are the floor pool (~0.6 m across), 8 edge strips (5 cm wide)
// and 4 face glows (15 cm), all a few ALU a pixel; particles are 0.5-7 cm. ~1M light pixels an eye at 0.5 m.
#if defined(__ANDROID__) && defined(STRIKERS_XR)

#define VK_USE_PLATFORM_ANDROID_KHR 1
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include "xr_binderfx.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace xrbfx {
namespace {

using V3 = XrVector3f;
inline V3 mk(float x, float y, float z) { return V3{x, y, z}; }
inline V3 add(V3 a, V3 b) { return mk(a.x + b.x, a.y + b.y, a.z + b.z); }
inline V3 sub(V3 a, V3 b) { return mk(a.x - b.x, a.y - b.y, a.z - b.z); }
inline V3 mul(V3 a, float k) { return mk(a.x * k, a.y * k, a.z * k); }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return mk(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline float len(V3 a) { return std::sqrt(dot(a, a)); }
inline V3 nz(V3 a) {
  const float l = len(a);
  return l > 1e-9f ? mul(a, 1.0f / l) : mk(0.0f, 0.0f, 0.0f);
}
inline float clampf(float x, float a, float b) { return std::min(b, std::max(a, x)); }
inline float fractf(float x) { return x - std::floor(x); }
inline float sstep(float e0, float e1, float x) {
  const float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
// xr_fx_model.frag's hash (the neon's flicker in step with its halos)
float bh(float x, float y) {
  float p0 = fractf(x * 0.1031f), p1 = fractf(y * 0.1031f), p2 = fractf(x * 0.1031f);
  const float d = p0 * (p1 + 33.33f) + p1 * (p2 + 33.33f) + p2 * (p0 + 33.33f);
  p0 += d, p1 += d, p2 += d;
  return fractf((p0 + p1) * p2);
}
V3 rainbow6(float h) {
  h = fractf(h) * 6.0f;
  return mk(clampf(std::fabs(h - 3.0f) - 1.0f, 0.0f, 1.0f), clampf(2.0f - std::fabs(h - 2.0f), 0.0f, 1.0f),
            clampf(2.0f - std::fabs(h - 4.0f), 0.0f, 1.0f));
}

constexpr float kW = 0.275f, kH = 0.31f, kT = 0.055f, kCT = 0.0045f, kST = kCT + 0.004f, kMDrop = 1.4f;
constexpr float kPi = 3.14159265f;
enum Kind { kGlow = 0, kStar = 1, kRim = 2, kDrop = 3, kSplat = 4, kPuff = 5, kShimmer = 6, kStreak = 7, kPool = 8 };
// skins (xr_cards.cpp kSkins' order)
enum : int {
  kSkSnake = 9, kSkZebra = 10, kSkLeopard = 11, kSkPuffer = 15, kSkPink = 16, kSkYeti = 17, kSkTeddy = 18, kSkShag = 19,
  kSkChrome = 21, kSkDiamond = 22, kSkDragon = 24, kSkShark = 25, kSkGalaxy = 26, kSkHolo = 27, kSkNeon = 28, kSkLava = 30,
  kSkIce = 31, kSkSlime = 32, kSkGold = 33, kSkStone = 34
};
float fur_mm(int si) {
  switch (si) {
    case kSkZebra: case kSkLeopard: return 3.5f;
    case kSkPink: return 10.0f;
    case kSkYeti: return 11.0f;
    case kSkTeddy: return 5.0f;
    case kSkShag: return 14.0f;
    default: return 0.0f;
  }
}
float fur_droop(int si) {
  switch (si) {
    case kSkPink: return 0.15f;
    case kSkYeti: return 0.2f;
    case kSkTeddy: return 0.05f;
    case kSkShag: return 0.3f;
    default: return 0.0f;
  }
}
// its halo: colour, strength, the face glows' colour, the floor light's strength (0: none)
struct Halo {
  V3 rgb;
  float k;
  V3 blob;
  float pool;
};
bool halo_of(int si, Halo* h) {
  switch (si) {
    case kSkLava: *h = {mk(1.0f, 0.32f, 0.05f), 1.0f, mk(1.0f, 0.40f, 0.08f), 0.55f}; return true;
    case kSkNeon: *h = {mk(0.15f, 0.90f, 1.0f), 0.9f, mk(1.0f, 0.25f, 0.85f), 0.3f}; return true;
    case kSkGalaxy: *h = {mk(0.55f, 0.40f, 1.0f), 0.3f, mk(0.45f, 0.55f, 1.0f), 0.15f}; return true;
    case kSkIce: *h = {mk(0.60f, 0.85f, 1.0f), 0.22f, mk(0.70f, 0.90f, 1.0f), 0.12f}; return true;
    case kSkSlime: *h = {mk(0.40f, 1.00f, 0.20f), 0.18f, mk(0.40f, 1.00f, 0.20f), 0.12f}; return true;
    case kSkDragon: *h = {mk(1.0f, 0.35f, 0.08f), 0.16f, mk(1.0f, 0.35f, 0.08f), 0.12f}; return true;
    case kSkStone: *h = {mk(1.0f, 0.60f, 0.25f), 0.10f, mk(1.0f, 0.60f, 0.25f), 0.08f}; return true;
    default: return false;
  }
}

// the quads' groups, in draw order (pool and halos first, then what covers - splats, smoke, drops -, then light)
enum Group { gPool, gRim, gBlob, gSplat, gSmoke, gDrop, gMist, gEmber, gStar, gShoot, gShimmer, gCount };
const int kGroupKind[gCount] = {kPool, kRim, kGlow, kSplat, kPuff, kDrop, kPuff, kStreak, kStar, kStreak, kShimmer};
void group_sizes(int si, int n[gCount]) {
  for (int g = 0; g < gCount; ++g) n[g] = 0;
  Halo h;
  if (halo_of(si, &h)) n[gPool] = 1, n[gRim] = 8, n[gBlob] = 4;
  switch (si) {
    case kSkLava: n[gSplat] = 2, n[gDrop] = 3, n[gEmber] = 36, n[gShimmer] = 2; break;
    case kSkSlime: n[gSplat] = 12, n[gDrop] = 24; break;  // (r163 fixes: 8 hanging, 10 falling, 5 runs)
    case kSkShark: n[gSplat] = 3, n[gDrop] = 4; break;
    case kSkIce: n[gMist] = 22, n[gStar] = 12; break;
    case kSkGalaxy: n[gStar] = 12, n[gShoot] = 1; break;
    case kSkHolo: n[gStar] = 12; break;
    case kSkGold: case kSkChrome: case kSkDiamond: n[gStar] = 4; break;
    case kSkDragon: n[gSmoke] = 16, n[gEmber] = 12; break;
    default: break;
  }
}

struct Pt {
  V3 p{0.0f, 0.0f, 0.0f}, v{0.0f, 0.0f, 0.0f}, c{1.0f, 1.0f, 1.0f};
  float age = 0.0f, life = 1.0f, size = 0.01f, seed = 0.0f, R = 0.003f, rq = 0.02f;
  int mode = 0, mat = 0, iseed = 0;
};
struct Site {
  V3 at{0.0f, 0.0f, 0.0f}, bead{0.0f, 0.0f, 0.0f}, bv{0.0f, 0.0f, 0.0f};
  float age = 0.0f, grow = 1.0f, R0 = 0.002f, R1 = 0.004f, stretch = 0.5f, neckL = 0.03f;
};
struct Slide {  // (r163 fixes: a gooey run - its bead at x, y, its streak's top at tx, ty, at most `trail` behind)
  int face = 2;
  float x = 0.0f, y = 0.0f, tx = 0.0f, ty = 0.0f, age = 0.0f, R = 0.006f, spd = 0.01f, trail = 0.05f;
};

struct State {
  int skin = -1;
  bool up = false, modelFor = false;
  int first[gCount] = {}, count[gCount] = {}, cursor[gCount] = {};
  int quads = 0;
  std::vector<float> verts;  // (the quads: 4 vertices each, position + "normal")
  uint32_t rng = 0x9E3779B9u;
  // particles
  std::vector<Pt> ember, drop, splat, mist, star, shoot, smoke;
  std::vector<Site> sites;
  std::vector<Slide> slides;
  float nextSite = 0.5f, nextSlide = 1.0f, nextShoot = 1.5f, nextSmoke = 1.0f;
  float accEmber = 0.0f, accMist = 0.0f, accStar = 0.0f, accGlint = 0.0f;
  // the binder's frame this frame
  float M[16] = {};
  float R[9] = {};  // (its turn, row-major)
  V3 org{0.0f, 0.0f, 0.0f}, eye{0.0f, 0.0f, 0.0f};
  float s = 1.0f, op = 0.0f;
  // its motion (its own frame)
  bool havePrev = false;
  float prevR[9] = {};
  V3 prevVb{0.0f, 0.0f, 0.0f}, vb{0.0f, 0.0f, 0.0f}, w{0.0f, 0.0f, 0.0f}, wPrev{0.0f, 0.0f, 0.0f};
  V3 aS{0.0f, 0.0f, 0.0f}, alS{0.0f, 0.0f, 0.0f};
  // fur
  bool furTried = false, furOk = false;
  int furCount = -1;
  std::vector<int> shIdx, shRep, reps, repVert;
  std::vector<float> shH, repH;
  std::vector<float> o, ov;  // (each root's parting spring, by its source vertex: 3 floats)
  // what's under it (r163 fixes: the Tabletop field when the binder is over it)
  bool tableValid = false;
  V3 tablePos{0.0f, 0.0f, 0.0f};
  float tableYaw = 0.0f, tableScale = 0.03f, floorY = -1.6f;
  V3 S{0.0f, 0.0f, 0.0f}, Sv{0.0f, 0.0f, 0.0f}, Th{0.0f, 0.0f, 0.0f}, Thv{0.0f, 0.0f, 0.0f};
};
State g;

float rnd() {
  uint32_t x = g.rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  g.rng = x;
  return static_cast<float>(x >> 8) / 16777216.0f;
}

V3 to_l(V3 p) {  // its frame -> LOCAL
  const float* m = g.M;
  return mk(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
            m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
}
V3 dir_l(V3 d) {  // a direction, turned (unit length kept)
  const float* r = g.R;
  return mk(r[0] * d.x + r[1] * d.y + r[2] * d.z, r[3] * d.x + r[4] * d.y + r[5] * d.z, r[6] * d.x + r[7] * d.y + r[8] * d.z);
}
V3 dir_b(V3 d) {  // LOCAL direction -> its frame (the turn's transpose)
  const float* r = g.R;
  return mk(r[0] * d.x + r[3] * d.y + r[6] * d.z, r[1] * d.x + r[4] * d.y + r[7] * d.z, r[2] * d.x + r[5] * d.y + r[8] * d.z);
}
V3 to_eye(V3 c) { return nz(sub(g.eye, c)); }

void rot_xz(V3* p, float cx, float cz, float a) {
  const float dx = p->x - cx, dz = p->z - cz, c = std::cos(a), s = std::sin(a);
  p->x = cx + dx * c - dz * s;
  p->z = cz + dx * s + dz * c;
}
// xr_cards.cpp bd_open_pt: a closed-frame point of the spine (1) / front (2) as opened
void open_pt(int grp, float op, V3* p, V3* n) {
  const float ts = op * 1.5707963f, tf = op * kPi;
  if (grp == 1) {
    rot_xz(p, 0.0f, -0.5f * kT, ts);
    rot_xz(n, 0.0f, 0.0f, ts);
  } else if (grp == 2) {
    rot_xz(p, 0.0f, 0.5f * kT, tf);
    rot_xz(n, 0.0f, 0.0f, tf);
    p->x += -kT * std::sin(ts);
    p->z += -0.5f * kT + kT * std::cos(ts) - 0.5f * kT;
  }
}
// a point on its outside: 0 the back cover, 1 the spine, 2 the front (a, b 0..1 across / up it), as opened
void face_point(int face, float a, float b, V3* p, V3* n) {
  if (face == 0) *p = mk(a * kW, (b - 0.5f) * kH, -0.5f * kT), *n = mk(0.0f, 0.0f, -1.0f);
  else if (face == 1) *p = mk(-kST - 0.0012f, (b - 0.5f) * kH, (a - 0.5f) * kT), *n = mk(-1.0f, 0.0f, 0.0f);
  else *p = mk(a * kW, (b - 0.5f) * kH, 0.5f * kT), *n = mk(0.0f, 0.0f, 1.0f);
  open_pt(face, g.op, p, n);
}
V3 centre() {
  const V3 cb = add(mul(mk(kW * 0.5f, 0.0f, 0.0f), 1.0f - g.op), mul(mk(-0.5f * kT, 0.0f, -0.5f * kT), g.op));
  return to_l(cb);
}
float lowest_y() {
  float lo = 1e9f;
  for (int i = 0; i < 8; ++i) lo = std::min(lo, to_l(mk(i & 1 ? kW : 0.0f, i & 2 ? 0.5f * kH : -0.5f * kH, i & 4 ? 0.5f * kT : -0.5f * kT)).y);
  return lo;
}

// r163 fixes: what's under a point - the Tabletop field's top when over it (+5 cm) and below the point, else the floor (the
// field: 41.2 x 24.2 game units x tableScale, centred at tablePos, turned by tableYaw about +Y: xr_fx.cpp game_to_room)
float ground_y(V3 p) {
  if (g.tableValid) {
    const float dx = p.x - g.tablePos.x, dz = p.z - g.tablePos.z, cs = std::cos(g.tableYaw), sn = std::sin(g.tableYaw);
    const float ga = dx * cs - dz * sn, gc = dx * sn + dz * cs;
    if (std::fabs(ga) < 20.6f * g.tableScale + 0.05f && std::fabs(gc) < 12.1f * g.tableScale + 0.05f && g.tablePos.y < p.y)
      return g.tablePos.y;
  }
  return g.floorY;
}

// ------------------------------------------------------------------------------------------------------------- quads
void put(int grp, int kind, V3 c, V3 ax, V3 ay, float sx, float sy, V3 nrm) {
  if (g.cursor[grp] >= g.count[grp] || kind != kGroupKind[grp]) return;
  float* o = g.verts.data() + static_cast<size_t>(g.first[grp] + g.cursor[grp]) * 24;
  ++g.cursor[grp];
  for (int k = 0; k < 4; ++k) {
    const float fu = static_cast<float>((k % 2) * 2 - 1), fv = static_cast<float>((k / 2) * 2 - 1);
    const V3 p = add(c, add(mul(ax, fu * sx), mul(ay, fv * sy)));
    o[k * 6 + 0] = p.x, o[k * 6 + 1] = p.y, o[k * 6 + 2] = p.z;
    o[k * 6 + 3] = nrm.x, o[k * 6 + 4] = nrm.y, o[k * 6 + 5] = nrm.z;
  }
}
// a quad turned to the eye (its v down the view)
void bb(int grp, int kind, V3 c, float sx, float sy, V3 nrm) {
  const V3 e = to_eye(c);
  V3 ax = nz(cross(mk(0.0f, 1.0f, 0.0f), e));
  if (len(ax) < 0.5f) ax = mk(1.0f, 0.0f, 0.0f);
  put(grp, kind, c, ax, cross(ax, e), sx, sy, nrm);
}
// a quad hanging from `top` along `axis` (v 0 at its top), turned to the eye about it
void along(int grp, int kind, V3 top, V3 axis, float length, float halfW, V3 nrm) {
  const V3 c = add(top, mul(axis, length * 0.5f));
  V3 ax = nz(cross(axis, to_eye(c)));
  if (len(ax) < 0.5f) ax = nz(cross(axis, mk(0.0f, 0.0f, 1.0f)));
  put(grp, kind, c, ax, axis, halfW, length * 0.5f, nrm);
}
const V3 kX{1.0f, 0.0f, 0.0f}, kY{0.0f, 1.0f, 0.0f}, kZ{0.0f, 0.0f, 1.0f}, kDown{0.0f, -1.0f, 0.0f};

// --------------------------------------------------------------------------------------------------------- the motion
void motion(const BinderView& v, float dt) {
  if (dt <= 0.0f) return;
  const V3 vb = mul(dir_b(v.vel), 1.0f / g.s);
  V3 w = mk(0.0f, 0.0f, 0.0f), a = mk(0.0f, 0.0f, 0.0f);
  if (g.havePrev) {
    float d[9];  // prevR^T R
    for (int i = 0; i < 3; ++i)
      for (int j = 0; j < 3; ++j) d[i * 3 + j] = g.prevR[0 * 3 + i] * g.R[0 * 3 + j] + g.prevR[1 * 3 + i] * g.R[1 * 3 + j] + g.prevR[2 * 3 + i] * g.R[2 * 3 + j];
    w = mul(mk(d[7] - d[5], d[2] - d[6], d[3] - d[1]), 0.5f / dt);
    a = mul(sub(vb, g.prevVb), 1.0f / dt);
  }
  const V3 al = mul(sub(w, g.wPrev), 1.0f / dt);
  const float k = std::min(1.0f, dt / 0.05f);
  auto cl = [](V3 x, float m) { return mk(clampf(x.x, -m, m), clampf(x.y, -m, m), clampf(x.z, -m, m)); };
  g.aS = add(g.aS, mul(sub(cl(a, 30.0f), g.aS), k));
  g.alS = add(g.alS, mul(sub(cl(al, 200.0f), g.alS), k));
  g.vb = vb, g.w = w;
  std::memcpy(g.prevR, g.R, sizeof(g.R));
  g.prevVb = vb, g.wPrev = w;
  g.havePrev = true;
}

// --------------------------------------------------------------------------------------------------------------- fur
// its shell vertices: each one's height and its root's spring (r163 fixes: by BinderView shellSrc / shellH - the base vertex
// each was copied from, its height; the shells hold only their parts' outward vertices). A root is moved by its source's
// lowest shell vertex. Without shellSrc: as before (each part's vertices again per shell, in order: runs of one height).
void fur_init(const BinderView& v) {
  g.furTried = true;
  g.furOk = false;
  g.furCount = v.vertCount;
  const int nv = v.vertCount;
  g.shIdx.clear(), g.shRep.clear(), g.shH.clear(), g.reps.clear(), g.repVert.clear(), g.repH.clear();
  if (v.shellSrc != nullptr && v.shellH != nullptr) {
    std::vector<int> best(static_cast<size_t>(nv), -1);  // (by source: its lowest shell vertex)
    for (int i = 0; i < nv; ++i) {
      const int src = v.shellSrc[i];
      if (src < 0 || src >= nv || v.shellH[i] < 0.0f) continue;
      g.shIdx.push_back(i);
      g.shRep.push_back(src);
      g.shH.push_back(v.shellH[i]);
      int& b = best[static_cast<size_t>(src)];
      if (b < 0 || v.shellH[i] < v.shellH[b]) b = i;
    }
    for (int src = 0; src < nv; ++src)
      if (best[static_cast<size_t>(src)] >= 0) {
        g.reps.push_back(src);
        g.repVert.push_back(best[static_cast<size_t>(src)]);
        g.repH.push_back(v.shellH[best[static_cast<size_t>(src)]]);
      }
    g.o.assign(static_cast<size_t>(nv) * 3, 0.0f);
    g.ov.assign(static_cast<size_t>(nv) * 3, 0.0f);
    g.furOk = !g.shIdx.empty();
    return;
  }
  std::vector<int> hs(static_cast<size_t>(nv), -1);
  for (int i = 0; i < nv; ++i) {
    const float* n = v.verts + static_cast<size_t>(i) * 6 + 3;
    const float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (l > 3.9f && l < 5.1f) hs[static_cast<size_t>(i)] = static_cast<int>(std::lround((l - 4.0f) / 0.98f * 16.0f));
  }
  int cur = -1, curLen = 0;
  for (int i = 0; i < nv;) {
    int j = i;
    while (j < nv && hs[static_cast<size_t>(j)] == hs[static_cast<size_t>(i)]) ++j;
    if (hs[static_cast<size_t>(i)] >= 0) {
      if (hs[static_cast<size_t>(i)] == 1) cur = i, curLen = j - i;
      if (cur < 0 || j - i != curLen) return;
      for (int k = i; k < j; ++k) {
        g.shIdx.push_back(k);
        g.shRep.push_back(cur + (k - i));
        g.shH.push_back(static_cast<float>(hs[static_cast<size_t>(k)]) / 16.0f);
        if (hs[static_cast<size_t>(i)] == 1) g.reps.push_back(k), g.repVert.push_back(k), g.repH.push_back(1.0f / 16.0f);
      }
    }
    i = j;
  }
  g.o.assign(static_cast<size_t>(nv) * 3, 0.0f);
  g.ov.assign(static_cast<size_t>(nv) * 3, 0.0f);
  g.furOk = !g.shIdx.empty();
}
void fur(BinderView& v, V3 upB, float t, float dt) {
  if (!g.furTried || g.furCount != v.vertCount) fur_init(v);
  if (!g.furOk || dt <= 0.0f) return;
  const int si = v.skinIndex;
  const float fur = fur_mm(si) * 0.001f;
  // the sway: its tips lag the binder's motion (springs: its move, its turn)
  const float fhz = 2.6f * std::sqrt(10.0f / fur_mm(si)), kk = (2.0f * kPi * fhz) * (2.0f * kPi * fhz), cc = 2.0f * 0.25f * std::sqrt(kk);
  const int nsub = std::max(1, static_cast<int>(std::ceil(dt / (1.0f / 120.0f))));
  const float h = dt / nsub;
  for (int i = 0; i < nsub; ++i) {
    V3 acc = sub(sub(mul(sub(mul(g.vb, -0.03f), g.S), kk), mul(g.Sv, cc)), g.aS);
    g.Sv = add(g.Sv, mul(acc, h));
    g.S = add(g.S, mul(g.Sv, h));
    acc = sub(sub(mul(sub(mul(g.w, -0.03f), g.Th), kk), mul(g.Thv, cc)), g.alS);
    g.Thv = add(g.Thv, mul(acc, h));
    g.Th = add(g.Th, mul(g.Thv, h));
  }
  float l = len(g.S);
  if (l > 0.8f * fur) g.S = mul(g.S, 0.8f * fur / l);
  l = len(g.Th);
  if (l > 2.0f) g.Th = mul(g.Th, 2.0f / l);
  // the hands parting it: each root's own spring, pushed away from a hand within 5 cm (and pressed down), springing back
  V3 hb[2];
  bool hv[2] = {false, false};
  for (int k = 0; k < 2; ++k) {
    if (!v.handValid[k]) continue;
    hb[k] = mul(dir_b(sub(v.hand[k], g.org)), 1.0f / g.s);
    hv[k] = hb[k].x > -0.08f / g.s - kT && hb[k].x < kW + 0.08f / g.s && std::fabs(hb[k].y) < 0.5f * kH + 0.08f / g.s &&
            std::fabs(hb[k].z) < 0.5f * kT + 0.08f / g.s + kW * g.op;
  }
  const float Rr = 0.05f / g.s, kp = (2.0f * kPi * 2.4f) * (2.0f * kPi * 2.4f), cp = 2.0f * 0.3f * std::sqrt(kp);
  for (size_t j = 0; j < g.reps.size(); ++j) {
    const int ri = g.reps[j];
    const float* vv = v.verts + static_cast<size_t>(g.repVert[j]) * 6;
    float* o = &g.o[static_cast<size_t>(ri) * 3];
    float* ov = &g.ov[static_cast<size_t>(ri) * 3];
    V3 T = mk(0.0f, 0.0f, 0.0f);
    if (hv[0] || hv[1]) {
      const V3 n = nz(mk(vv[3], vv[4], vv[5]));
      const V3 root = sub(mk(vv[0], vv[1], vv[2]), mul(n, fur * g.repH[j]));
      for (int k = 0; k < 2; ++k) {
        if (!hv[k]) continue;
        const V3 d = sub(root, hb[k]);
        const float dist = len(d);
        if (dist >= Rr) continue;
        float f = 1.0f - dist / Rr;
        f = f * f * (3.0f - 2.0f * f);
        const V3 tg = nz(sub(d, mul(n, dot(d, n))));
        T = add(T, sub(mul(tg, fur * 1.25f * f), mul(n, fur * 0.55f * f)));
      }
    }
    if (T.x == 0.0f && T.y == 0.0f && T.z == 0.0f && std::fabs(o[0]) + std::fabs(o[1]) + std::fabs(o[2]) < 1e-6f &&
        std::fabs(ov[0]) + std::fabs(ov[1]) + std::fabs(ov[2]) < 1e-5f) {
      o[0] = o[1] = o[2] = ov[0] = ov[1] = ov[2] = 0.0f;
      continue;
    }
    const float tt[3] = {T.x, T.y, T.z};
    for (int i = 0; i < nsub; ++i)
      for (int c = 0; c < 3; ++c) {
        ov[c] += (kp * (tt[c] - o[c]) - cp * ov[c]) * h;
        o[c] += ov[c] * h;
      }
  }
  // every shell vertex: sway + a ripple + droop (along its surface) + its root's parting, by its height squared; where
  // it's pressed flat its shells read lower (their normals' length: more strands, darker - a matted trail)
  const V3 c = add(mul(mk(kW * 0.5f, 0.0f, 0.0f), 1.0f - v.open), mul(mk(0.0f, 0.0f, -0.5f * kT), v.open));
  const V3 dirr = mk(0.6f, -0.8f, 0.3f), droop = mul(upB, fur * fur_droop(si));
  const float lim = 0.9f * fur;
  for (size_t i = 0; i < g.shIdx.size(); ++i) {
    float* vv = v.verts + static_cast<size_t>(g.shIdx[i]) * 6;
    const V3 P = mk(vv[0], vv[1], vv[2]), n = nz(mk(vv[3], vv[4], vv[5]));
    const V3 r = sub(P, c);
    const float ph = r.x * 55.0f + r.y * 31.0f + r.z * 40.0f - t * 2.3f;
    V3 D = add(add(g.S, cross(g.Th, r)), mul(dirr, 0.10f * fur * std::sin(ph)));
    D = sub(D, droop);
    D = sub(D, mul(n, dot(D, n)));
    const float dl = len(D);
    if (dl > lim) D = mul(D, lim / dl);
    const float* o = &g.o[static_cast<size_t>(g.shRep[i]) * 3];
    D = add(D, mk(o[0], o[1], o[2]));
    const float hh = g.shH[i];
    vv[0] += D.x * hh * hh, vv[1] += D.y * hh * hh, vv[2] += D.z * hh * hh;
    const float flat = clampf(-(o[0] * n.x + o[1] * n.y + o[2] * n.z) / (0.55f * fur), 0.0f, 1.0f);
    if (flat > 0.0f) {
      const float k = 4.0f + 0.98f * hh * (1.0f - 0.45f * flat);
      vv[3] = n.x * k, vv[4] = n.y * k, vv[5] = n.z * k;
    }
  }
}
// (r163 fixes: slime's cover wobble / the puffer's breathing are in xr_fx_model.vert now - its poses are baked)

// -------------------------------------------------------------------------------------------------------------- halo
float flicker(int si, float t) {
  if (si == kSkLava) return (0.86f + 0.14f * std::sin(t * 1.25f)) * (0.92f + 0.08f * std::sin(t * 7.3f) * std::sin(t * 3.1f));
  if (si == kSkNeon) {
    const float fl = bh(std::floor(t * 14.0f), 3.7f);
    return fl > 0.96f ? 0.3f : (fl > 0.92f ? 0.7f : 1.0f);
  }
  if (si == kSkSlime) return 0.85f + 0.15f * std::sin(t * 1.4f);
  return 1.0f + 0.08f * std::sin(t * 0.9f);
}
void halo(int si, float t) {
  Halo H;
  if (!halo_of(si, &H)) return;
  const float fk = flicker(si, t), s = g.s;
  // its light on what's under it (r163 fixes: the Tabletop field when it's over it, else the floor)
  const V3 c = centre();
  const float floorY = ground_y(c);
  const float hgt = c.y - floorY;
  if (hgt > 0.02f) {
    const float rad = (0.25f + 0.3f * std::min(hgt, 1.5f)) * s;
    const float inten = H.k * H.pool * fk / (1.0f + (hgt / 0.6f) * (hgt / 0.6f));
    put(gPool, kPool, mk(c.x, floorY + 0.002f, c.z), kX, kZ, rad, rad, mul(H.rgb, inten));
  }
  // strips of light along its covers' edges (the glow hugging its outline), soft glows over its faces
  for (int face : {2, 0}) {
    const float z = face == 2 ? 0.5f * kT : -0.5f * kT;
    const V3 nf = mk(0.0f, 0.0f, face == 2 ? 1.0f : -1.0f);
    V3 cl[4];
    const V3 cs[4] = {mk(0.0f, -0.5f * kH, z), mk(kW, -0.5f * kH, z), mk(kW, 0.5f * kH, z), mk(0.0f, 0.5f * kH, z)};
    for (int i = 0; i < 4; ++i) {
      V3 p = cs[i], n = nf;
      open_pt(face, g.op, &p, &n);
      cl[i] = to_l(p);
    }
    V3 p0 = mk(0.0f, 0.0f, 0.0f), nl = nf;
    open_pt(face, g.op, &p0, &nl);
    nl = dir_l(nl);
    for (int e = 0; e < 4; ++e) {
      const V3 a = cl[e], b = cl[(e + 1) % 4], mid = mul(add(a, b), 0.5f), te = to_eye(mid), ed = nz(sub(b, a));
      const V3 side = nz(cross(ed, te));
      put(gRim, kRim, add(mid, mul(te, 0.01f * s)), ed, side, len(sub(b, a)) * 0.5f + 0.012f * s, 0.024f * s, mul(H.rgb, H.k * fk * 0.55f));
    }
    for (float yy : {0.2f, -0.2f}) {
      V3 p = mk(kW * 0.5f, yy * kH, z), n = nf;
      open_pt(face, g.op, &p, &n);
      const V3 cp = add(to_l(p), mul(nl, 0.012f * s)), te = to_eye(cp);
      const float fac = 0.3f + 0.7f * std::max(0.0f, dot(nl, te));
      bb(gBlob, kGlow, add(cp, mul(te, 0.01f * s)), 0.075f * s, 0.075f * s, mul(H.blob, H.k * fk * 0.12f * fac));
    }
  }
}

// --------------------------------------------------------------------------------------------------------- particles
void rand_face(int lowBias, V3* pl, V3* nl) {
  bool have = false;
  for (int i = 0; i < 1 + lowBias; ++i) {
    const float x = rnd();
    const int face = x < 0.45f ? 2 : (x < 0.9f ? 0 : 1);
    const float a = 0.04f + 0.92f * rnd(), b = 0.04f + 0.92f * rnd();
    V3 p, n;
    face_point(face, a, b, &p, &n);
    const V3 q = to_l(p);
    if (!have || q.y < pl->y) *pl = q, *nl = nz(dir_l(n)), have = true;
  }
}
void ember_burst(V3 at, int n) {
  for (int i = 0; i < n && static_cast<int>(g.ember.size()) < g.count[gEmber]; ++i) {
    Pt e;
    e.p = at;
    const float a = rnd(), b = rnd(), c = rnd();
    e.v = mul(mk((a - 0.5f) * 0.15f, 0.12f + 0.12f * b, (c - 0.5f) * 0.15f), g.s);
    e.life = 0.5f + 0.6f * rnd();
    e.size = (0.0015f + 0.0015f * rnd()) * g.s;
    e.seed = rnd();
    g.ember.push_back(e);
  }
}
void embers(float t, float dt, float rate) {
  const float s = g.s;
  g.accEmber += rate * dt;
  while (g.accEmber >= 1.0f) {
    g.accEmber -= 1.0f;
    if (static_cast<int>(g.ember.size()) >= g.count[gEmber]) continue;
    V3 pl, nl;
    rand_face(0, &pl, &nl);
    Pt e;
    e.p = add(pl, mul(nl, 0.003f * s));
    const float a = rnd(), b = rnd();
    const float c0 = rnd(), c1 = rnd(), c2 = rnd();
    e.v = mul(add(add(mul(nl, 0.02f + 0.03f * a), mk(0.0f, 0.03f + 0.04f * b, 0.0f)), mul(mk(c0 - 0.5f, c1 - 0.5f, c2 - 0.5f), 0.03f)), s);
    e.life = 1.0f + 1.6f * rnd();
    e.size = (0.003f + 0.003f * rnd()) * s;
    e.seed = rnd();
    g.ember.push_back(e);
  }
  for (size_t i = 0; i < g.ember.size();) {
    Pt& e = g.ember[i];
    e.age += dt;
    if (e.age >= e.life) {
      e = g.ember.back();
      g.ember.pop_back();
      continue;
    }
    e.v.y += 0.25f * s * dt;
    e.v = add(e.v, mul(mk(std::sin(e.p.y * 23.0f + t * 2.7f + e.seed * 6.28f), 0.0f, std::cos(e.p.y * 19.0f - t * 2.1f + e.seed * 9.0f)), 0.18f * s * dt));
    e.v = mul(e.v, std::max(0.0f, 1.0f - 1.2f * dt));
    e.p = add(e.p, mul(e.v, dt));
    ++i;
  }
  for (const Pt& e : g.ember) {
    const float k = 1.0f - e.age / e.life;
    const V3 col = mul(mk(1.0f, 0.30f + 0.6f * k * k, 0.05f + 0.35f * k * k * k), (1.4f * k + 0.2f) * std::min(1.0f, e.age / 0.15f));
    const float sp = len(e.v);
    const V3 dr = sp > 1e-6f ? mul(e.v, 1.0f / sp) : kY;
    const float ln = e.size * 2.5f + sp * 0.05f;
    along(gEmber, kStreak, sub(e.p, mul(dr, ln)), dr, ln, e.size, col);  // (its tail at v 0, its head ahead)
  }
}
void shimmer(float t) {
  const float s = g.s;
  float top = -1e9f;
  for (int i = 0; i < 8; ++i) top = std::max(top, to_l(mk(i & 1 ? kW : 0.0f, i & 2 ? 0.5f * kH : -0.5f * kH, i & 4 ? 0.5f * kT : -0.5f * kT)).y);
  const V3 cc = centre();
  for (int i = 0; i < 2; ++i) {
    V3 c = mk(cc.x, top + 0.085f * s, cc.z);
    const V3 ax = nz(cross(kY, to_eye(c)));
    c = add(c, mul(ax, (static_cast<float>(i) - 0.5f) * 0.06f * s));
    put(gShimmer, kShimmer, c, ax, kDown, 0.08f * s, 0.085f * s, mk(4.0f * static_cast<float>(7 + 23 * i), 0.25f * flicker(kSkLava, t), 0.0f));
  }
}
// drips: hanging from its lowest edge, growing, stretching, falling, splatting on what's under it (mat 0 slime, 1 water,
// 2 lava). r163 fixes (Lance's #1: the slime "constantly dripping"): slime's are big gooey bulbs (1.6-3 cm across) on
// strands stretching 5-10 cm before they snap, up to 6 at once (8 with the runs'), a new one every 0.2-0.6 s
struct DripCfg {
  int hang, fall;
  float gap0, gap1, grow0, grow1, st0, st1, R0, R10, R11, fade, rq0, rq1, nk0, nk1;
};
DripCfg drip_cfg(int mat) {
  if (mat == 0) return {6, 10, 0.2f, 0.6f, 1.0f, 2.0f, 0.9f, 1.6f, 0.004f, 0.008f, 0.0135f, 5.0f, 2.6f, 3.4f, 0.05f, 0.10f};
  if (mat == 1) return {1, 3, 2.5f, 4.0f, 1.2f, 2.0f, 0.35f, 0.35f, 0.0015f, 0.003f, 0.0038f, 2.0f, 4.0f, 5.5f, 0.015f, 0.015f};
  return {1, 2, 1.8f, 3.2f, 1.0f, 1.6f, 0.4f, 0.4f, 0.002f, 0.004f, 0.005f, 1.2f, 3.5f, 5.0f, 0.02f, 0.02f};
}
V3 edge_candidate() {
  const bool front = rnd() < 0.5f;
  const float zc = front ? 0.5f * kT - 0.5f * kCT : -0.5f * kT + 0.5f * kCT;
  const float e = rnd(), sv = rnd();
  V3 p = e < 0.4f ? mk(0.02f + sv * (kW - 0.04f), -0.5f * kH - 0.0005f, zc)
                  : (e < 0.7f ? mk(0.02f + sv * (kW - 0.04f), 0.5f * kH + 0.0005f, zc) : mk(kW + 0.0005f, (sv - 0.5f) * (kH - 0.04f), zc));
  if (front) {
    V3 n = mk(0.0f, 0.0f, 0.0f);
    open_pt(2, g.op, &p, &n);
  }
  return p;
}
void new_site(V3 at, const DripCfg& cfg, float R0) {
  Site st;
  st.at = at;
  st.grow = cfg.grow0 + (cfg.grow1 - cfg.grow0) * rnd();
  st.R1 = cfg.R10 + (cfg.R11 - cfg.R10) * rnd();
  st.stretch = cfg.st0 + (cfg.st1 - cfg.st0) * rnd();
  st.neckL = cfg.nk0 + (cfg.nk1 - cfg.nk0) * rnd();
  st.R0 = R0;
  st.bead = to_l(at);
  g.sites.push_back(st);
}
// r163 fixes: gooey runs - a bead of slime sliding down a face with its streak trailing behind it (up to ~7 cm); at the
// face's edge it hangs on there as a drip (or falls)
void sliders(float dt, const DripCfg& cfg) {
  const float s = g.s;
  const V3 gb = dir_b(kDown);
  g.nextSlide -= dt;
  if (g.nextSlide <= 0.0f && g.slides.size() < 5 && g.op < 0.05f) {
    g.nextSlide = 0.5f + 0.7f * rnd();
    Slide b;
    b.face = rnd() < 0.5f ? 2 : 0;
    b.x = b.tx = 0.03f + (kW - 0.06f) * rnd();
    b.y = b.ty = (0.0f + 0.45f * rnd()) * kH;
    b.R = 0.006f + 0.004f * rnd();
    b.spd = 0.008f + 0.01f * rnd();
    b.trail = 0.04f + 0.03f * rnd();
    g.slides.push_back(b);
  }
  for (size_t i = 0; i < g.slides.size();) {
    Slide& b = g.slides[i];
    b.age += dt;
    const float gl = std::sqrt(gb.x * gb.x + gb.y * gb.y);
    if (gl > 0.25f) {
      b.x += gb.x / gl * b.spd * dt * std::min(1.0f, gl);
      b.y += gb.y / gl * b.spd * dt * std::min(1.0f, gl);
    }
    const float dx = b.x - b.tx, dy = b.y - b.ty, dd = std::sqrt(dx * dx + dy * dy);
    if (dd > b.trail) b.tx += dx * (1.0f - b.trail / dd), b.ty += dy * (1.0f - b.trail / dd);
    const float z = b.face == 2 ? 0.5f * kT : -0.5f * kT;
    const V3 nf = dir_l(mk(0.0f, 0.0f, b.face == 2 ? 1.0f : -1.0f));
    const float Rb = b.R * s * std::min(1.0f, b.age / 0.6f);
    const V3 pl = add(to_l(mk(b.x, b.y, z)), mul(nf, Rb * 0.7f));
    if (b.x < 0.004f || b.x > kW - 0.004f || b.y < -0.5f * kH + 0.004f || b.y > 0.5f * kH - 0.004f) {
      const float zc = b.face == 2 ? 0.5f * kT - 0.5f * kCT : -0.5f * kT + 0.5f * kCT;
      const V3 at = mk(clampf(b.x, 0.0f, kW), clampf(b.y, -0.5f * kH - 0.0005f, 0.5f * kH + 0.0005f), zc);
      if (static_cast<int>(g.sites.size()) < cfg.hang + 2 && to_l(at).y < lowest_y() + 0.05f * s) {
        new_site(at, cfg, b.R);
      } else if (static_cast<int>(g.drop.size()) < cfg.fall) {
        Pt d;
        d.p = pl, d.v = mk(0.0f, 0.0f, 0.0f), d.R = Rb, d.mat = 0;
        g.drop.push_back(d);
      }
      b = g.slides.back();
      g.slides.pop_back();
      continue;
    }
    const V3 top = add(to_l(mk(b.tx, b.ty, z)), mul(nf, Rb * 0.45f));
    const V3 d = sub(pl, top);
    const float dl = len(d);
    const V3 axis = dl > 1e-5f ? mul(d, 1.0f / dl) : kDown;
    const float A2 = dl / std::max(Rb, 1e-5f) + kMDrop;
    along(gDrop, kDrop, top, axis, A2 * Rb, kMDrop * Rb, mk(0.75f, A2, 1.0f));
    ++i;
  }
}
void drips(float dt, int mat) {
  const float s = g.s;
  const DripCfg cfg = drip_cfg(mat);
  const float low = lowest_y();
  g.nextSite -= dt;
  if (g.nextSite <= 0.0f && static_cast<int>(g.sites.size()) < cfg.hang) {
    g.nextSite = cfg.gap0 + (cfg.gap1 - cfg.gap0) * rnd();
    bool have = false;
    V3 best = mk(0.0f, 0.0f, 0.0f);
    int seen = 0;
    for (int i = 0; i < 24; ++i) {  // (one of the candidates along its lowest edge, at random: they spread along it)
      const V3 pb = edge_candidate();
      if (to_l(pb).y < low + 0.025f * s) {
        ++seen;
        if (!have || rnd() * static_cast<float>(seen) < 1.0f) best = pb, have = true;
      }
    }
    bool clear = have;
    for (const Site& st : g.sites) clear = clear && len(sub(best, st.at)) > 0.025f;
    if (clear) new_site(best, cfg, cfg.R0);
  }
  if (mat == 0) sliders(dt, cfg);
  for (size_t i = 0; i < g.sites.size();) {
    Site& st = g.sites[i];
    st.age += dt;
    const V3 a = to_l(st.at);
    const float R0 = st.R0 * s, R1 = st.R1 * s;
    float Rb, Lh, neck;
    if (st.age < st.grow) {
      const float x = st.age / st.grow;
      Rb = R0 + (R1 - R0) * x * x * (3.0f - 2.0f * x);
      Lh = Rb * 1.2f;
      neck = 0.85f;
    } else {
      const float x = std::min(1.0f, (st.age - st.grow) / st.stretch);
      Rb = R1 * (1.0f + 0.15f * x);
      Lh = Rb * 1.2f + x * x * st.neckL * s;
      neck = 0.85f - 0.6f * x;
    }
    const V3 target = add(a, mul(kDown, Lh));
    const V3 acc = sub(mul(sub(target, st.bead), 180.0f), mul(st.bv, 8.0f));
    st.bv = add(st.bv, mul(acc, dt));
    st.bead = add(st.bead, mul(st.bv, dt));
    const bool lowNow = a.y <= low + 0.05f * s;
    if (st.age >= st.grow + st.stretch || !lowNow) {
      if (static_cast<int>(g.drop.size()) < cfg.fall) {
        Pt d;
        d.p = st.bead, d.v = mul(st.bv, 0.5f), d.R = Rb, d.mat = mat;
        g.drop.push_back(d);
      }
      if (rnd() < 0.6f && lowNow) {  // (the same spot again: dripping on)
        st.age = 0.0f;
        st.R0 = cfg.R0;
        st.R1 = cfg.R10 + (cfg.R11 - cfg.R10) * rnd();
        st.stretch = cfg.st0 + (cfg.st1 - cfg.st0) * rnd();
        st.neckL = cfg.nk0 + (cfg.nk1 - cfg.nk0) * rnd();
        st.bead = a;
        st.bv = mk(0.0f, 0.0f, 0.0f);
        ++i;
      } else {
        st = g.sites.back();
        g.sites.pop_back();
      }
      continue;
    }
    const V3 d = sub(st.bead, a);
    const float dl = len(d);
    const V3 axis = dl > 1e-6f ? mul(d, 1.0f / dl) : kDown;
    const float A2 = dl / Rb + kMDrop;
    along(gDrop, kDrop, a, axis, A2 * Rb, kMDrop * Rb, mk(static_cast<float>(mat) + neck, A2, 1.0f));
    ++i;
  }
  for (size_t i = 0; i < g.drop.size();) {  // falling: onto what's under it (the floor, the Tabletop field)
    Pt& d = g.drop[i];
    const float gy = ground_y(d.p);
    d.v.y -= 9.8f * dt;
    d.v = mul(d.v, std::max(0.0f, 1.0f - 0.3f * dt));
    d.p = add(d.p, mul(d.v, dt));
    if (d.p.y <= gy + d.R) {
      if (static_cast<int>(g.splat.size()) < g.count[gSplat]) {
        Pt sp;
        sp.p = mk(d.p.x, gy + 0.0015f, d.p.z);
        sp.rq = d.R * (cfg.rq0 + (cfg.rq1 - cfg.rq0) * rnd());
        sp.iseed = static_cast<int>(rnd() * 63.0f);
        sp.mat = d.mat;
        g.splat.push_back(sp);
      }
      if (d.mat == 2) ember_burst(mk(d.p.x, gy + 0.004f, d.p.z), 4);
      d = g.drop.back();
      g.drop.pop_back();
      continue;
    }
    const float sp = len(d.v);
    const V3 axis = sp > 1e-6f ? mul(d.v, 1.0f / sp) : kDown;
    const float A2 = kMDrop + 1.0f + std::min(sp * 0.012f / d.R, 4.0f);
    along(gDrop, kDrop, sub(d.p, mul(axis, (A2 - kMDrop) * d.R)), axis, A2 * d.R, kMDrop * d.R, mk(static_cast<float>(d.mat), A2, 1.0f));
    ++i;
  }
  for (size_t i = 0; i < g.splat.size();) {
    Pt& sp = g.splat[i];
    sp.age += dt;
    if (sp.age >= cfg.fade) {
      sp = g.splat.back();
      g.splat.pop_back();
      continue;
    }
    const float spread = std::sqrt(std::min(1.0f, sp.age / 0.18f));
    const float fade = 1.0f - sstep(cfg.fade * 0.6f, cfg.fade, sp.age);
    put(gSplat, kSplat, sp.p, kX, kZ, sp.rq, sp.rq, mk(static_cast<float>(sp.mat) + 4.0f * static_cast<float>(sp.iseed), spread, fade));
    ++i;
  }
}
void mist(float dt) {
  const float s = g.s;
  g.accMist += 6.0f * dt;
  while (g.accMist >= 1.0f) {
    g.accMist -= 1.0f;
    if (static_cast<int>(g.mist.size()) >= g.count[gMist]) continue;
    V3 pl, nl;
    rand_face(2, &pl, &nl);
    Pt m;
    m.p = add(pl, mul(nl, 0.01f * s));
    const float a = rnd(), b = rnd(), c = rnd();
    m.v = mul(add(add(mk(0.0f, -0.015f, 0.0f), mul(nl, 0.012f)), mul(mk(a - 0.5f, b - 0.5f, c - 0.5f), 0.008f)), s);
    m.life = 2.6f + 1.6f * rnd();
    m.iseed = static_cast<int>(rnd() * 63.0f);
    g.mist.push_back(m);
  }
  for (size_t i = 0; i < g.mist.size();) {
    Pt& m = g.mist[i];
    m.age += dt;
    if (m.age >= m.life) {
      m = g.mist.back();
      g.mist.pop_back();
      continue;
    }
    m.v.y -= 0.004f * s * dt;
    m.v = mul(m.v, std::max(0.0f, 1.0f - 0.4f * dt));
    m.p = add(m.p, mul(m.v, dt));
    ++i;
  }
  for (const Pt& m : g.mist) {
    const float a = m.age / m.life, size = (0.018f + 0.052f * std::sqrt(a)) * s;
    bb(gMist, kPuff, m.p, size, size, mk(4.0f * static_cast<float>(m.iseed), 0.16f * std::pow(std::sin(kPi * a), 1.2f), 0.0f));
  }
}
// stars: 0 ice glints on it, 1 the galaxy's round it (drifting), 2 holographic sparkles (rainbow)
void stars(float dt, float rate, int mode) {
  const float s = g.s;
  g.accStar += rate * dt;
  while (g.accStar >= 1.0f) {
    g.accStar -= 1.0f;
    if (static_cast<int>(g.star.size()) >= g.count[gStar]) continue;
    Pt st;
    st.mode = mode;
    if (mode == 1) {
      const float a = rnd(), b = rnd(), c = rnd(), d = rnd();
      st.p = to_l(mk(-0.04f + (kW + 0.08f) * a, (b - 0.5f) * (kH + 0.08f), (c < 0.5f ? 1.0f : -1.0f) * (0.5f * kT + 0.01f + 0.04f * d)));
      st.life = 1.0f + 1.5f * rnd();
      st.size = (0.006f + 0.008f * rnd()) * s;
      const int pick = static_cast<int>(rnd() * 2.999f);
      st.c = pick == 0 ? mk(1.0f, 1.0f, 1.0f) : (pick == 1 ? mk(0.6f, 0.75f, 1.0f) : mk(1.0f, 0.6f, 0.9f));
    } else {
      V3 pl, nl;
      rand_face(0, &pl, &nl);
      st.p = add(pl, mul(nl, 0.002f * s));
      st.life = mode == 0 ? 0.3f + 0.3f * rnd() : 0.4f + 0.3f * rnd();
      st.size = mode == 0 ? (0.008f + 0.008f * rnd()) * s : (0.008f + 0.006f * rnd()) * s;
      st.c = mode == 0 ? mk(0.85f, 0.95f, 1.0f) : rainbow6(rnd());
    }
    g.star.push_back(st);
  }
  const V3 cen = centre();
  for (size_t i = 0; i < g.star.size();) {
    Pt& st = g.star[i];
    st.age += dt;
    if (st.age >= st.life) {
      st = g.star.back();
      g.star.pop_back();
      continue;
    }
    if (st.mode == 1) st.p = add(st.p, mul(cross(kY, sub(st.p, cen)), 0.08f * dt));
    ++i;
  }
  for (const Pt& st : g.star) {
    float k = std::sin(kPi * st.age / st.life);
    if (st.mode == 0) k *= k;
    bb(gStar, kStar, st.p, st.size, st.size, mul(st.c, (st.mode == 0 ? 1.4f : 1.2f) * k));
  }
}
void shooting(float dt) {
  const float s = g.s;
  g.nextShoot -= dt;
  if (g.nextShoot <= 0.0f && g.shoot.empty()) {
    g.nextShoot = 5.0f + 3.0f * rnd();
    Pt st;
    st.p = to_l(mk(-0.08f, kH * (0.3f + 0.1f * rnd()), 0.5f * kT + 0.04f));
    st.v = mul(nz(dir_l(mk(1.0f, -0.35f, 0.0f))), 0.55f * s);
    st.life = 0.55f;
    g.shoot.push_back(st);
  }
  for (size_t i = 0; i < g.shoot.size();) {
    Pt& st = g.shoot[i];
    st.age += dt;
    if (st.age >= st.life) {
      st = g.shoot.back();
      g.shoot.pop_back();
      continue;
    }
    st.p = add(st.p, mul(st.v, dt));
    const V3 dr = nz(st.v);
    const float ln = 0.09f * s;
    along(gShoot, kStreak, sub(st.p, mul(dr, ln)), dr, ln, 0.004f * s, mul(mk(0.9f, 0.95f, 1.0f), 1.6f * std::sin(kPi * st.age / st.life)));
    ++i;
  }
}
// the metals: star glints popping where the shader's band of light is crossing it now (the same sweep)
void glints(float t, float dt, int si) {
  const float s = g.s;
  const float per = si == kSkGold ? 3.2f : 4.0f, tt = t + static_cast<float>(si) * 0.7f;
  const float band = fractf(tt / per) * 1.3f - 0.3f;
  g.accGlint += 3.0f * dt;
  while (g.accGlint >= 1.0f) {
    g.accGlint -= 1.0f;
    if (static_cast<int>(g.star.size()) >= g.count[gStar]) continue;
    const float y = 0.31f * rnd(), x = (band - 0.56f * y) / 0.83f, u = x / 0.598f, vv = y / 0.31f;
    float xb;
    int face;
    if (u > 0.54f && u < 1.0f) xb = (u - 0.54f) / 0.46f * kW, face = 2;
    else if (u > 0.0f && u < 0.46f) xb = (0.46f - u) / 0.46f * kW, face = 0;
    else continue;
    V3 p, n;
    face_point(face, xb / kW, 1.0f - vv, &p, &n);
    Pt st;
    st.p = add(to_l(p), mul(nz(dir_l(n)), 0.003f * s));
    st.life = 0.35f;
    st.size = (0.012f + 0.008f * rnd()) * s;
    st.c = si == kSkGold ? mk(1.0f, 0.85f, 0.5f) : mk(1.0f, 1.0f, 1.0f);
    g.star.push_back(st);
  }
  for (size_t i = 0; i < g.star.size();) {
    g.star[i].age += dt;
    if (g.star[i].age >= g.star[i].life) {
      g.star[i] = g.star.back();
      g.star.pop_back();
      continue;
    }
    ++i;
  }
  for (const Pt& st : g.star) {
    const float k = std::sin(kPi * st.age / st.life);
    bb(gStar, kStar, st.p, st.size, st.size, mul(st.c, 1.3f * k * k));
  }
}
void smoke(float dt) {
  const float s = g.s;
  g.nextSmoke -= dt;
  if (g.nextSmoke <= 0.0f) {
    g.nextSmoke = 1.5f + 1.5f * rnd();  // (r163 fixes: denser, lit smoke: xr_fx_model.frag bfx_main)
    V3 pl, nl;
    rand_face(0, &pl, &nl);
    for (int i = 0; i < 6 && static_cast<int>(g.smoke.size()) < g.count[gSmoke]; ++i) {
      Pt m;
      m.p = add(pl, mul(nl, 0.006f * s));
      const float a = rnd(), b = rnd(), c = rnd();
      m.v = mul(add(mk((a - 0.5f) * 0.04f, 0.05f + 0.04f * b, (c - 0.5f) * 0.04f), mul(nl, 0.03f)), s);
      m.age = -0.15f * static_cast<float>(i);
      m.life = 2.0f + 1.2f * rnd();
      m.iseed = static_cast<int>(rnd() * 63.0f);
      g.smoke.push_back(m);
    }
    ember_burst(add(pl, mul(nl, 0.004f * s)), 3);
  }
  for (size_t i = 0; i < g.smoke.size();) {
    Pt& m = g.smoke[i];
    m.age += dt;
    if (m.age >= m.life) {
      m = g.smoke.back();
      g.smoke.pop_back();
      continue;
    }
    if (m.age > 0.0f) {
      m.v = mul(m.v, std::max(0.0f, 1.0f - 0.5f * dt));
      m.p = add(m.p, mul(m.v, dt));
    }
    ++i;
  }
  for (const Pt& m : g.smoke) {
    if (m.age <= 0.0f) continue;
    const float a = m.age / m.life, size = (0.015f + 0.055f * std::sqrt(a)) * s;
    bb(gSmoke, kPuff, m.p, size, size, mk(4.0f * static_cast<float>(m.iseed) + 1.0f, 0.75f * std::pow(std::sin(kPi * a), 0.8f), 0.0f));
  }
}

// ------------------------------------------------------------------------------------------------------------ the model
void reset(int si) {
  g.skin = si;
  g.ember.clear(), g.drop.clear(), g.splat.clear(), g.mist.clear(), g.star.clear(), g.shoot.clear(), g.smoke.clear();
  g.sites.clear(), g.slides.clear();
  g.nextSite = 0.5f, g.nextSlide = 1.0f, g.nextShoot = 1.5f, g.nextSmoke = 1.0f;
  g.accEmber = g.accMist = g.accStar = g.accGlint = 0.0f;
  g.furTried = g.furOk = false;
  g.furCount = -1;
  g.S = g.Sv = g.Th = g.Thv = g.aS = g.alS = g.wPrev = mk(0.0f, 0.0f, 0.0f);
  g.havePrev = false;
  int n[gCount];
  group_sizes(si, n);
  g.quads = 0;
  for (int i = 0; i < gCount; ++i) {
    g.first[i] = g.quads;
    g.count[i] = n[i];
    g.quads += n[i];
  }
  g.verts.assign(static_cast<size_t>(g.quads) * 24, 0.0f);
}
bool model_up(bool fxReady) {
  if (g.quads == 0) {
    if (g.up && fxReady) xrfx::set_model(kBinderFxSlot, xrfx::Model{});
    g.up = false;
    return false;
  }
  if (g.up) return true;
  if (!fxReady) return false;
  xrfx::Model m;
  m.texRgba.push_back(std::vector<uint8_t>(4 * 4 * 4, 255));  // (its sprites are drawn, not pictured: bfx_main)
  m.texW.push_back(4);
  m.texH.push_back(4);
  for (int grp = 0; grp < gCount; ++grp)
    for (int q = 0; q < g.count[grp]; ++q) {
      const float kind = static_cast<float>(kGroupKind[grp]);
      for (int k = 0; k < 4; ++k) {
        m.uv.push_back(kind * 2.0f + static_cast<float>(k % 2));
        m.uv.push_back(static_cast<float>(k / 2));
      }
    }
  for (int q = 0; q < g.quads; ++q) {
    const uint32_t a = static_cast<uint32_t>(q * 4);
    m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
  }
  xrfx::ModelPart p;
  p.tex = 0;
  p.soft = true;  // (blended after everything solid, depth tested, not written)
  p.firstIndex = 0;
  p.indexCount = static_cast<uint32_t>(g.quads * 6);
  m.parts.push_back(p);
  m.vertexCount = static_cast<uint32_t>(g.quads * 4);
  xrfx::set_model(kBinderFxSlot, std::move(m));
  g.up = xrfx::model_up(kBinderFxSlot);
  return g.up;
}

}  // namespace

void frame(xrfx::Scene& scene, BinderView& v, bool fxReady) {
  const int si = v.skinIndex;
  if (si != g.skin) {
    if (g.up && fxReady) xrfx::set_model(kBinderFxSlot, xrfx::Model{});
    g.up = false;
    reset(si);
  }
  // the clock the shader's motion runs on (xr_fx.cpp: steady seconds, an hour round): the neon's halos flicker with it
  const float t = static_cast<float>(std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(), 3600.0));
  const float dt = clampf(v.dt, 0.0f, 0.1f);
  std::memcpy(g.M, v.toWorld, sizeof(g.M));
  g.s = std::max(0.05f, v.scale);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) g.R[i * 3 + j] = g.M[j * 4 + i] / g.s;
  g.org = mk(g.M[12], g.M[13], g.M[14]);
  g.op = clampf(v.open, 0.0f, 1.0f);
  g.eye = mul(add(scene.views[0].pose.position, scene.views[1].pose.position), 0.5f);
  g.tableValid = v.tableValid, g.tablePos = v.tablePos, g.tableYaw = v.tableYaw, g.tableScale = v.tableScale, g.floorY = v.floorY;
  motion(v, dt);
  // its fur moving (the vertices about to be drawn; r163 fixes: a furless skin hands none - its poses are baked)
  if (v.verts != nullptr && v.vertCount > 0) {
    if (fur_mm(si) > 0.0f) fur(v, dir_b(kY), t, dt);
  }
  if (!model_up(fxReady)) return;
  for (int i = 0; i < gCount; ++i) g.cursor[i] = 0;
  halo(si, t);
  switch (si) {
    case kSkLava: embers(t, dt, 16.0f), drips(dt, 2), shimmer(t); break;
    case kSkSlime: drips(dt, 0); break;
    case kSkShark: drips(dt, 1); break;
    case kSkIce: mist(dt), stars(dt, 7.0f, 0); break;
    case kSkGalaxy: stars(dt, 5.0f, 1), shooting(dt); break;
    case kSkHolo: stars(dt, 8.0f, 2); break;
    case kSkGold: case kSkChrome: case kSkDiamond: glints(t, dt, si); break;
    case kSkDragon: smoke(dt), embers(t, dt, 0.0f); break;
    default: break;
  }
  for (int grp = 0; grp < gCount; ++grp)  // (the rest of each group: nothing)
    for (int q = g.cursor[grp]; q < g.count[grp]; ++q)
      std::memset(g.verts.data() + static_cast<size_t>(g.first[grp] + q) * 24, 0, sizeof(float) * 24);
  xrfx::Scene::ModelDraw& md = scene.models[kBinderFxSlot];
  md.on = true;
  md.posNrm = g.verts.data();
  md.local = false;
  md.white = 0.0f;
  md.alpha = 1.0f;
  md.glow = 0.0f;
  md.under = -7.5f;  // (xr_fx_model.frag bfx_main)
  md.glowRgb = 0xFFFFFF;
  md.flameFrame = -1;
  md.shine = 0.0f;
  md.frame = -1;
  md.kaleido = 0u;
}

void stop(bool fxReady) {
  if (g.up && fxReady) xrfx::set_model(kBinderFxSlot, xrfx::Model{});
  g.up = false;
  g.skin = -1;  // (its pieces let go: a fresh start next time)
}

}  // namespace xrbfx

#endif  // __ANDROID__ && STRIKERS_XR
