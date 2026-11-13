// strikers-quest r230 (Lance 11-12, C:\StrikersMR\EXQUISITE.md): THE VAULT - LANCINI EXQUISITE's box as a real case in your room (his
// pick, packs_1 of the r229 mockups): a hinged black-leather collector's case, gold corners, two brass latches YOU flick open with your own
// hands (a heavy metal click + a thunk in the controller each), then the lid creaks open slowly and warm gold light spills out of it (soft
// glow planes: budget-safe), the packs lying in black velvet (5 in THE VAULT; a smaller one-pack case for a pack bought alone). One pure
// header (no OpenXR, no state): its mesh, its pose a frame, its timings, its sounds (made in code: no download) and a small software
// rasteriser (the shop's picture of it; the PC proofs) - xr_cards.cpp draws it in the effects layer (kSpecialSlot1, xr_fx_model.frag's
// vault mode, glowRgb.w -11), the PC proof renders exactly this mesh.
//   Its own space: metres, x right, y up, z toward you (its front at +D/2); its feet on y 0, its middle at x = z = 0.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace xrvault {

struct V3 {
  float x = 0, y = 0, z = 0;
};
inline V3 v3(float x, float y, float z) { return V3{x, y, z}; }
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 norm(V3 a) {
  const float l = std::sqrt(dot(a, a));
  return l > 1e-9f ? a * (1.0f / l) : V3{0, 1, 0};
}
// (x-axis turn about a hinge line at (y, z))
inline V3 rot_x(V3 p, float y0, float z0, float a) {
  const float c = std::cos(a), s = std::sin(a), y = p.y - y0, z = p.z - z0;
  return {p.x, y0 + y * c - z * s, z0 + y * s + z * c};
}
inline V3 rot_xn(V3 n, float a) {
  const float c = std::cos(a), s = std::sin(a);
  return {n.x, n.y * c - n.z * s, n.y * s + n.z * c};
}

// ---- its size
struct Dims {
  float W, D, Hb, Hl, wall, floorY, packW, packH;
  int packs;
};
inline Dims dims(bool box) {
  // THE VAULT: 40 x 25 cm, 7 cm deep + a 3.2 cm lid, five packs side by side; the one-pack case: 16 x 24 cm
  if (box) return Dims{0.40f, 0.25f, 0.070f, 0.032f, 0.014f, 0.024f, 0.068f, 0.068f * 1380.0f / 900.0f, 5};
  return Dims{0.16f, 0.24f, 0.060f, 0.028f, 0.013f, 0.020f, 0.094f, 0.094f * 1380.0f / 900.0f, 1};
}
// what a part is (its normals' length: xr_fx_model.frag vault_main)
enum Code { kLeather = 1, kMetal = 2, kVelvet = 3, kSatin = 4, kPackFace = 5, kGlow = 6 };
// its pictures (xr_cards vault_textures; the alpha < 0.985 of a leather / pack picture = polished metal / gold foil there)
enum Tex { kTexFront = 0, kTexSide, kTexRim, kTexVelvet, kTexLidTop, kTexLidIn, kTexBrass, kTexPack, kTexGlow, kTexBeam, kTexCount };
constexpr int kTexW[kTexCount] = {1024, 512, 256, 512, 1024, 1024, 128, 450, 256, 128};
constexpr int kTexH[kTexCount] = {256, 256, 256, 512, 640, 640, 128, 690, 256, 256};
// its moving groups
enum Group { kBase = 0, kLid = 1, kLatch0 = 2, kLatch1 = 3, kPack0 = 4, kGlowG = 12 };

struct Part {
  int tex = 0;
  bool soft = false;
  uint32_t first = 0, count = 0;
};
struct Mesh {
  Dims d{};
  bool box = true;
  std::vector<float> pos, nrm, uv;  // rest (3 / 3 / 2 a vertex)
  std::vector<float> code;          // a vertex's code (Code)
  std::vector<int> group;
  std::vector<uint32_t> idx;
  std::vector<Part> parts;
  uint32_t verts() const { return static_cast<uint32_t>(group.size()); }
};

// ---- where things are (its own space)
inline float latch_x(const Dims& d, int i) { return (i == 0 ? -0.30f : 0.30f) * d.W; }
constexpr float kLatchW = 0.034f, kLatchH = 0.046f, kLatchT = 0.006f, kLatchBelow = 0.026f;  // its plate; its hinge this far under the rim
inline V3 latch_point(const Dims& d, int i) { return {latch_x(d, i), d.Hb, 0.5f * d.D + kLatchT}; }  // (the spot a hand flicks)
inline V3 pack_center(const Dims& d, int k) {
  const float pitch = d.packs > 1 ? (d.W - 2 * d.wall - 0.012f) / d.packs : 0.0f;
  const float x = d.packs > 1 ? -0.5f * (d.W - 2 * d.wall - 0.012f) + pitch * (k + 0.5f) : 0.0f;
  return {x, d.floorY + 0.004f, 0.008f};
}

// ---- the mesh
inline Mesh build(bool box) {
  Mesh m;
  m.box = box;
  m.d = dims(box);
  const Dims& d = m.d;
  const float W = d.W, D = d.D, Hb = d.Hb, Hl = d.Hl, t = d.wall;
  auto quad = [&](int tex, bool soft, int code, int group, V3 a, V3 b, V3 c, V3 e, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    // a b c e counter-clockwise seen from its front (a top left, b top right, c bottom right, e bottom left)
    const V3 n = norm(cross(b - a, e - a));
    const uint32_t base = m.verts();
    const V3 P[4] = {a, b, c, e};
    const float U[4] = {u0, u1, u1, u0}, Vv[4] = {v0, v0, v1, v1};
    for (int k = 0; k < 4; ++k) {
      m.pos.insert(m.pos.end(), {P[k].x, P[k].y, P[k].z});
      m.nrm.insert(m.nrm.end(), {-n.x, -n.y, -n.z});
      m.uv.insert(m.uv.end(), {U[k], Vv[k]});
      m.code.push_back(static_cast<float>(code));
      m.group.push_back(group);
    }
    Part p;
    p.tex = tex, p.soft = soft;
    p.first = static_cast<uint32_t>(m.idx.size());
    m.idx.insert(m.idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    p.count = 6;
    if (!m.parts.empty() && m.parts.back().tex == tex && m.parts.back().soft == soft && m.parts.back().first + m.parts.back().count == p.first)
      m.parts.back().count += 6;
    else
      m.parts.push_back(p);
  };
  // (the normal: cross(b - a, e - a) points away from the viewer for a b c e clockwise on screen: flipped above so the face points at you)
  const float x0 = -W / 2, x1 = W / 2, z0 = -D / 2, z1 = D / 2;
  // the base: front (its stitching, gold corners, the latches' back plates), sides, back, bottom
  quad(kTexFront, false, kLeather, kBase, v3(x0, Hb, z1), v3(x1, Hb, z1), v3(x1, 0, z1), v3(x0, 0, z1));
  quad(kTexSide, false, kLeather, kBase, v3(x1, Hb, z1), v3(x1, Hb, z0), v3(x1, 0, z0), v3(x1, 0, z1));
  quad(kTexSide, false, kLeather, kBase, v3(x0, Hb, z0), v3(x0, Hb, z1), v3(x0, 0, z1), v3(x0, 0, z0));
  quad(kTexSide, false, kLeather, kBase, v3(x1, Hb, z0), v3(x0, Hb, z0), v3(x0, 0, z0), v3(x1, 0, z0));
  quad(kTexSide, false, kLeather, kBase, v3(x0, 0, z1), v3(x1, 0, z1), v3(x1, 0, z0), v3(x0, 0, z0), 0, 0, 1, 0.2f);
  // the rim on top of the walls (leather), the inner walls + the floor (velvet)
  const float ix0 = x0 + t, ix1 = x1 - t, iz0 = z0 + t, iz1 = z1 - t, fy = d.floorY;
  quad(kTexRim, false, kLeather, kBase, v3(x0, Hb, z0), v3(x1, Hb, z0), v3(x1, Hb, iz0), v3(x0, Hb, iz0));
  quad(kTexRim, false, kLeather, kBase, v3(x0, Hb, iz1), v3(x1, Hb, iz1), v3(x1, Hb, z1), v3(x0, Hb, z1));
  quad(kTexRim, false, kLeather, kBase, v3(x0, Hb, iz0), v3(ix0, Hb, iz0), v3(ix0, Hb, iz1), v3(x0, Hb, iz1));
  quad(kTexRim, false, kLeather, kBase, v3(ix1, Hb, iz0), v3(x1, Hb, iz0), v3(x1, Hb, iz1), v3(ix1, Hb, iz1));
  quad(kTexVelvet, false, kVelvet, kBase, v3(ix0, Hb, iz0), v3(ix1, Hb, iz0), v3(ix1, fy, iz0), v3(ix0, fy, iz0), 0, 0, 1, 0.12f);   // back wall
  quad(kTexVelvet, false, kVelvet, kBase, v3(ix1, Hb, iz1), v3(ix0, Hb, iz1), v3(ix0, fy, iz1), v3(ix1, fy, iz1), 0, 0.3f, 1, 0.42f);  // front wall
  quad(kTexVelvet, false, kVelvet, kBase, v3(ix0, Hb, iz1), v3(ix0, Hb, iz0), v3(ix0, fy, iz0), v3(ix0, fy, iz1), 0, 0.5f, 0.6f, 0.62f);
  quad(kTexVelvet, false, kVelvet, kBase, v3(ix1, Hb, iz0), v3(ix1, Hb, iz1), v3(ix1, fy, iz1), v3(ix1, fy, iz0), 0.4f, 0.7f, 1, 0.82f);
  quad(kTexVelvet, false, kVelvet, kBase, v3(ix0, fy, iz0), v3(ix1, fy, iz0), v3(ix1, fy, iz1), v3(ix0, fy, iz1));
  // the packs in their velvet beds (each its own group: one rises out, the rest stay)
  for (int k = 0; k < d.packs; ++k) {
    const V3 c = pack_center(d, k);
    const float hw = 0.5f * d.packW, hh = 0.5f * d.packH;
    quad(kTexPack, false, kPackFace, kPack0 + k, v3(c.x - hw, c.y, c.z - hh), v3(c.x + hw, c.y, c.z - hh), v3(c.x + hw, c.y, c.z + hh), v3(c.x - hw, c.y, c.z + hh));
  }
  // the lid (closed: on top; its hinge along the back top edge): its top (the embossed mark), its sides, its inside (satin + the plate)
  const float ly0 = Hb, ly1 = Hb + Hl;
  quad(kTexLidTop, false, kLeather, kLid, v3(x0, ly1, z0), v3(x1, ly1, z0), v3(x1, ly1, z1), v3(x0, ly1, z1));
  quad(kTexSide, false, kLeather, kLid, v3(x0, ly1, z1), v3(x1, ly1, z1), v3(x1, ly0, z1), v3(x0, ly0, z1), 0, 0.6f, 1, 1);
  quad(kTexSide, false, kLeather, kLid, v3(x1, ly1, z1), v3(x1, ly1, z0), v3(x1, ly0, z0), v3(x1, ly0, z1), 0, 0.6f, 1, 1);
  quad(kTexSide, false, kLeather, kLid, v3(x0, ly1, z0), v3(x0, ly1, z1), v3(x0, ly0, z1), v3(x0, ly0, z0), 0, 0.6f, 1, 1);
  quad(kTexSide, false, kLeather, kLid, v3(x1, ly1, z0), v3(x0, ly1, z0), v3(x0, ly0, z0), v3(x1, ly0, z0), 0, 0.6f, 1, 1);
  // its underside: a leather rim round the satin (facing down while it's shut)
  quad(kTexRim, false, kLeather, kLid, v3(x0, ly0, z1), v3(x1, ly0, z1), v3(x1, ly0, iz1), v3(x0, ly0, iz1));
  quad(kTexRim, false, kLeather, kLid, v3(x0, ly0, iz0), v3(x1, ly0, iz0), v3(x1, ly0, z0), v3(x0, ly0, z0));
  quad(kTexRim, false, kLeather, kLid, v3(x0, ly0, iz1), v3(ix0, ly0, iz1), v3(ix0, ly0, iz0), v3(x0, ly0, iz0));
  quad(kTexRim, false, kLeather, kLid, v3(ix1, ly0, iz1), v3(x1, ly0, iz1), v3(x1, ly0, iz0), v3(ix1, ly0, iz0));
  quad(kTexLidIn, false, kSatin, kLid, v3(ix0, ly0 + 0.006f, iz1), v3(ix1, ly0 + 0.006f, iz1), v3(ix1, ly0 + 0.006f, iz0), v3(ix0, ly0 + 0.006f, iz0));
  // (its satin's recess walls)
  quad(kTexVelvet, false, kVelvet, kLid, v3(ix0, ly0, iz1), v3(ix1, ly0, iz1), v3(ix1, ly0 + 0.006f, iz1), v3(ix0, ly0 + 0.006f, iz1), 0, 0, 1, 0.02f);
  quad(kTexVelvet, false, kVelvet, kLid, v3(ix1, ly0, iz0), v3(ix0, ly0, iz0), v3(ix0, ly0 + 0.006f, iz0), v3(ix1, ly0 + 0.006f, iz0), 0, 0, 1, 0.02f);
  // the latches' keepers on the lid's front (brass), the latches (hinged at their foot, flicked up + out)
  for (int i = 0; i < 2; ++i) {
    const float lx = latch_x(d, i), kw = 0.5f * kLatchW + 0.004f;
    quad(kTexBrass, false, kMetal, kLid, v3(lx - kw, ly0 + 0.022f, z1 + 0.002f), v3(lx + kw, ly0 + 0.022f, z1 + 0.002f), v3(lx + kw, ly0 + 0.004f, z1 + 0.002f),
         v3(lx - kw, ly0 + 0.004f, z1 + 0.002f));
    const int g = i == 0 ? kLatch0 : kLatch1;
    const float a0 = lx - 0.5f * kLatchW, a1 = lx + 0.5f * kLatchW, yb = Hb - kLatchBelow, yt = yb + kLatchH, zb = z1 + 0.0005f, zf = z1 + kLatchT;
    quad(kTexBrass, false, kMetal, g, v3(a0, yt, zf), v3(a1, yt, zf), v3(a1, yb, zf), v3(a0, yb, zf));                 // its face
    quad(kTexBrass, false, kMetal, g, v3(a0, yt, zb), v3(a1, yt, zb), v3(a1, yt, zf), v3(a0, yt, zf), 0, 0, 1, 0.15f);  // its top edge
    quad(kTexBrass, false, kMetal, g, v3(a1, yt, zf), v3(a1, yt, zb), v3(a1, yb, zb), v3(a1, yb, zf), 0, 0, 0.15f, 1);
    quad(kTexBrass, false, kMetal, g, v3(a0, yt, zb), v3(a0, yt, zf), v3(a0, yb, zf), v3(a0, yb, zb), 0, 0, 0.15f, 1);
    quad(kTexBrass, false, kMetal, g, v3(a1, yt, zb), v3(a0, yt, zb), v3(a0, yb, zb), v3(a1, yb, zb), 0, 0, 1, 1);  // its back (seen flicked open)
  }
  // the light spilling out (soft, added: xr_fx_model.frag's vault glow - how strong by ModelDraw::glow): a pool over the velvet, two crossed
  // shafts rising out of the open case
  // (r230 review: the light must read as coming from INSIDE the case - the pool lies inside its opening only, low over the velvet; the two
  // short soft shafts rise from the middle of the velvet, inset from the walls, fading out well under the open lid's top)
  quad(kTexGlow, true, kGlow, kGlowG, v3(ix0, fy + 0.012f, iz0), v3(ix1, fy + 0.012f, iz0), v3(ix1, fy + 0.012f, iz1), v3(ix0, fy + 0.012f, iz1));
  const float sh = box ? 0.15f : 0.13f, bx = 0.30f * (ix1 - ix0), bz = 0.30f * (iz1 - iz0);
  quad(kTexBeam, true, kGlow, kGlowG, v3(-bx, Hb + sh, 0), v3(bx, Hb + sh, 0), v3(bx, fy + 0.01f, 0), v3(-bx, fy + 0.01f, 0));
  quad(kTexBeam, true, kGlow, kGlowG, v3(0, Hb + sh, -bz), v3(0, Hb + sh, bz), v3(0, fy + 0.01f, bz), v3(0, fy + 0.01f, -bz));
  return m;
}

// ---- its moves
constexpr float kLatchFlick = 0.16f;      // s: a latch flicked open (to kLatchOpen, a small overshoot)
constexpr float kLatchOpen = 1.92f;       // rad: ~110 degrees (up + out, hanging open)
constexpr float kLidWait = 0.35f;         // s: the second latch -> the lid starts
constexpr float kLidOpenS = 2.6f;         // s: the lid creaking open
constexpr float kLidOpenA = 1.88f;        // rad: ~108 degrees (past upright: it rests back)
constexpr float kPackWait = 0.35f, kPackLift = 1.0f;  // s: the open lid -> the pack lifts out of its velvet into your hands
constexpr float kAppear = 0.6f;           // s: the case fading in in front of you
inline float smooth01(float t) {
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  return t * t * (3 - 2 * t);
}
// a latch's angle `t` s after its flick (< 0: shut)
inline float latch_angle(float t) {
  if (t < 0) return 0;
  if (t >= kLatchFlick + 0.25f) return kLatchOpen;
  if (t < kLatchFlick) {
    const float u = t / kLatchFlick;
    return kLatchOpen * 1.08f * (1 - (1 - u) * (1 - u));
  }
  const float u = (t - kLatchFlick) / 0.25f;
  return kLatchOpen * (1.08f - 0.08f * smooth01(u));
}
// the lid's angle `t` s after it starts to open (< 0: shut): a slow creak - a hesitant start, steady, easing to rest
inline float lid_angle(float t) {
  if (t <= 0) return 0;
  const float u = std::min(1.0f, t / kLidOpenS);
  const float e = 0.25f * u * u + 0.75f * u * u * (3.0f - 2.0f * u);  // (slow off the latches, easing to rest)
  const float wob = 0.010f * std::sin(u * 31.0f) * u * (1 - u) * 4.0f;  // (the creak's little catches)
  return kLidOpenA * std::min(1.0f, std::max(0.0f, e + wob));
}
// the light out of it (0..1) as the lid opens, breathing a little once open
inline float glow_of(float lidA, float sinceOpen) {
  const float o = smooth01((lidA / kLidOpenA - 0.12f) / 0.55f);
  return o * (0.92f + 0.08f * std::sin(sinceOpen * 1.7f));
}

// ---- a frame's pose -> the vertices xr_fx draws (6 floats each: place, normal x its code)
struct Pose {
  float lid = 0.0f;         // rad
  float latch[2] = {0, 0};  // rad
  unsigned packs = 0x1F;    // the packs still lying in it (bit k)
  float glow = 0.0f;        // 0: its glow planes collapsed
};
inline void pose(const Mesh& m, const Pose& p, std::vector<float>* out) {
  const Dims& d = m.d;
  out->resize(static_cast<size_t>(m.verts()) * 6);
  for (uint32_t i = 0; i < m.verts(); ++i) {
    V3 P{m.pos[i * 3], m.pos[i * 3 + 1], m.pos[i * 3 + 2]}, N{m.nrm[i * 3], m.nrm[i * 3 + 1], m.nrm[i * 3 + 2]};
    const int g = m.group[i];
    bool hide = false;
    if (g == kLid) {
      P = rot_x(P, d.Hb, -0.5f * d.D, -p.lid);
      N = rot_xn(N, -p.lid);
    } else if (g == kLatch0 || g == kLatch1) {
      const float a = p.latch[g - kLatch0];
      P = rot_x(P, d.Hb - kLatchBelow, 0.5f * d.D, a);
      N = rot_xn(N, a);
    } else if (g >= kPack0 && g < kPack0 + 8) {
      hide = ((p.packs >> (g - kPack0)) & 1u) == 0;
    } else if (g == kGlowG) {
      hide = p.glow <= 0.004f;
    }
    float* o = out->data() + static_cast<size_t>(i) * 6;
    if (hide) {
      for (int k = 0; k < 6; ++k) o[k] = 0;
      continue;
    }
    const float c = m.code[i];
    o[0] = P.x, o[1] = P.y, o[2] = P.z;
    o[3] = N.x * c, o[4] = N.y * c, o[5] = N.z * c;
  }
}

// ---- its sounds (32 kHz mono; no file)
// r231 (Lance on the r230 preview: "what the fuck was all that static noise ... that shit better not play in VR!!"): every sound here is
// made of decaying sine partials only - NO random numbers, no noise layer anywhere (r230's latch had a 4 ms burst of white noise, the
// lounge's brushes were noise; the static in the preview itself was its PC tool's PNG transport, logs\r231\audio\evidence). Each sound
// starts on a zero-phase sine with a short fade-in and ends faded to 0 (no clicks at the edges).
namespace snd {
constexpr int kRate = 32000;
constexpr float kTau = 6.2831853f;
inline float fade_in(float t, float s) { return t >= s ? 1.0f : t / s; }
// a damped sine partial (zero phase at its start, a `rise` s fade-in)
inline float mode(float t, float hz, float amp, float dec, float rise = 0.0006f) {
  return t < 0.0f ? 0.0f : amp * fade_in(t, rise) * std::exp(-t / dec) * std::sin(kTau * hz * t);
}
// float -> 16-bit at `peak` of full scale (the peak measured), the last `tail` s faded to 0
inline void to_pcm(const std::vector<float>& v, float peak, float tail, std::vector<short>* out) {
  float pk = 1e-6f;
  for (float x : v) pk = std::max(pk, std::fabs(x));
  const size_t n = v.size(), fade = static_cast<size_t>(tail * kRate);
  out->assign(n, 0);
  for (size_t s = 0; s < n; ++s) {
    const float f = s + fade > n ? static_cast<float>(n - s) / static_cast<float>(fade) : 1.0f;
    (*out)[s] = static_cast<short>(std::max(-32000.0f, std::min(32000.0f, v[s] / pk * peak * f * 32000.0f)));
  }
}
}  // namespace snd
// a heavy brass latch flicked open: a clean metal click-clack - the catch's bright partials (inharmonic, like a small brass part ringing)
// struck, then 16 ms on the latch seating (a lower, softer set), over a low thunk of the case
inline void latch_pcm(std::vector<short>* out) {
  using namespace snd;
  std::vector<float> v(static_cast<size_t>(0.32f * kRate), 0.0f);
  double ph = 0;
  for (size_t s = 0; s < v.size(); ++s) {
    const float t = static_cast<float>(s) / kRate, u = t - 0.016f;
    const float click = mode(t, 2140.0f, 0.30f, 0.040f) + mode(t, 3390.0f, 0.22f, 0.022f) + mode(t, 4710.0f, 0.13f, 0.012f) + mode(t, 6180.0f, 0.07f, 0.006f);
    const float seat = mode(u, 1480.0f, 0.16f, 0.030f) + mode(u, 2630.0f, 0.10f, 0.018f) + mode(u, 3860.0f, 0.05f, 0.010f);
    const float f = 85.0f + 70.0f * std::exp(-t / 0.03f);
    ph += kTau * f / kRate;
    const float thunk = 0.8f * std::sin(static_cast<float>(ph)) * fade_in(t, 0.002f) * std::exp(-t / 0.06f);
    v[s] = click + seat + thunk;
  }
  to_pcm(v, 0.75f, 0.02f, out);
}
// the lid's creak (kLidOpenS long), soft: a slow stick-slip - smooth pulses (a raised-cosine 2.5 ms wide each, never a one-sample spike, so
// nothing above ~2 kHz) at a rate wandering 22 - 48 a second, through two warm wood / leather resonances; swelling and fading with the
// move; a soft settle at its end. Deterministic (no random numbers).
inline void creak_pcm(std::vector<short>* out) {
  using namespace snd;
  const float len = kLidOpenS + 0.4f;
  std::vector<float> v(static_cast<size_t>(len * kRate), 0.0f);
  float y1a = 0, y2a = 0, y1b = 0, y2b = 0, next = 0.0f, pulseAt = -1.0f;
  int k = 0;
  auto bp = [&](float f, float q, float& y1, float& y2, float x) {  // (a 2-pole resonator)
    const float r = std::exp(-3.14159f * f / (q * kRate)), c = 2 * r * std::cos(kTau * f / kRate);
    const float y = x + c * y1 - r * r * y2;
    y2 = y1, y1 = y;
    return y * (1 - r);
  };
  constexpr float kPulse = 0.0025f;
  for (size_t s = 0; s < v.size(); ++s) {
    const float t = static_cast<float>(s) / kRate, u = t / kLidOpenS;
    if (t >= next && t < kLidOpenS) {
      pulseAt = t;
      const float wob = 0.5f + 0.5f * std::sin(static_cast<float>(k) * 2.399f);  // (a golden-angle walk: uneven, not random)
      const float rate = 22.0f + 26.0f * (0.5f + 0.5f * std::sin(u * 7.0f + 1.3f)) * (0.7f + 0.3f * wob);
      next = t + 1.0f / rate;
      ++k;
    }
    float x = 0.0f;
    if (pulseAt >= 0.0f && t - pulseAt < kPulse) x = 0.5f - 0.5f * std::cos(kTau * (t - pulseAt) / kPulse);
    const float env = u < 1 ? std::sin(3.14159f * u) : 0.0f;
    const float a = bp(300.0f + 80.0f * std::sin(u * 5.0f), 14.0f, y1a, y2a, x), b = bp(760.0f + 160.0f * u, 10.0f, y1b, y2b, x);
    float settle = 0;
    if (t > kLidOpenS) settle = 0.45f * mode(t - kLidOpenS, 70.0f, 1.0f, 0.07f, 0.004f);
    v[s] = (a * 1.0f + b * 0.45f) * env + settle;
  }
  to_pcm(v, 0.55f, 0.05f, out);
}
// THE PRIVATE GALLERY's lounge (r231: no noise anywhere) - 4 phrases of 2 bars at 76 bpm (Dm9 G13 | Cmaj9 Am9 | Fmaj7#11 E7b9 |
// Dm9 G13sus), laid end to end while the gallery's up: a warm electric piano (a soft FM tine, a bell's touch), an upright bass walking
// (fundamental + a plucked 2nd / 3rd), a soft kit made of tones - a felt kick on 1 and 3, the brush-snare's drum-head modes on 2 and 4, a
// quiet ride "ting" (three metal partials) on the swung eighths. One fixed level for all four (no phrase louder than another), peak ~-6 dBFS (r230: each -6.2).
constexpr float kLoungeBpm = 76.0f;
inline float lounge_len() { return 8.0f * 60.0f / kLoungeBpm; }
inline void lounge_pcm(int phrase, std::vector<short>* out) {
  using namespace snd;
  const float beat = 60.0f / kLoungeBpm, L = lounge_len();
  auto hz = [](int midi) { return 440.0f * std::pow(2.0f, (midi - 69) / 12.0f); };
  static const int kCh[8][5] = {{50, 53, 57, 60, 64}, {43, 47, 52, 53, 57}, {48, 52, 55, 59, 62}, {45, 48, 52, 55, 59},
                                {41, 45, 48, 52, 59}, {40, 44, 47, 50, 53}, {50, 53, 57, 60, 64}, {43, 48, 50, 53, 57}};
  static const int kBass[8][4] = {{38, 45, 41, 43}, {31, 38, 35, 33}, {36, 43, 40, 38}, {33, 40, 36, 35},
                                  {29, 36, 33, 35}, {28, 35, 32, 26}, {38, 45, 41, 40}, {31, 38, 36, 35}};
  const int ph = ((phrase % 4) + 4) % 4;
  std::vector<float> v(static_cast<size_t>(L * kRate), 0.0f);
  auto add = [&](float at, float dur, auto fn) {  // fn(t) for t in [0, dur)
    const size_t s0 = static_cast<size_t>(at * kRate), s1 = std::min(v.size(), static_cast<size_t>((at + dur) * kRate));
    for (size_t s = s0; s < s1; ++s) v[s] += fn(static_cast<float>(s - s0) / kRate);
  };
  auto keys = [&](float at, float f, float amp, float dec) {
    add(at, dec * 6.0f, [=](float t) {
      const float env = fade_in(t, 0.008f) * std::exp(-t / dec);
      const float idx = 0.7f * std::exp(-t / 0.22f);  // (the tine's brightness fading)
      return amp * env * (std::sin(kTau * f * t + idx * std::sin(kTau * f * t)) + 0.10f * std::sin(kTau * 3.0f * f * t) * std::exp(-t / 0.06f));
    });
  };
  auto bass = [&](float at, float f, float amp) {
    add(at, 1.4f, [=](float t) {
      const float env = fade_in(t, 0.010f) * std::exp(-t / 0.50f);
      return amp * env * (std::sin(kTau * f * t) + 0.30f * std::sin(kTau * 2 * f * t) * std::exp(-t / 0.25f) + 0.10f * std::sin(kTau * 3 * f * t) * std::exp(-t / 0.06f));
    });
  };
  for (int bar = 0; bar < 2; ++bar) {
    const int c = ph * 2 + bar;
    const float b0 = bar * 4.0f * beat;
    for (int k = 0; k < 5; ++k) keys(b0 + 0.012f * k, hz(kCh[c][k] + 12), 0.075f, 1.5f);                 // the chord on 1
    for (int k = 1; k < 5; ++k) keys(b0 + 1.5f * beat + 0.01f * k, hz(kCh[c][k] + 12), 0.042f, 0.9f);  // a soft push on the and of 2
    for (int q = 0; q < 4; ++q) bass(b0 + q * beat, hz(kBass[c][q]), 0.30f);                            // the bass walking
    for (int q = 0; q < 4; ++q) {
      const float at = b0 + q * beat;
      if (q % 2 == 0)  // felt kick: a 62 -> 46 Hz thump
        add(at, 0.35f, [](float t) {
          const float f = 46.0f + 16.0f * std::exp(-t / 0.03f);
          return 0.16f * fade_in(t, 0.004f) * std::exp(-t / 0.09f) * std::sin(kTau * f * t);
        });
      else  // the brush-snare's drum head: its first modes, soft and short
        add(at, 0.30f, [](float t) {
          return mode(t, 182.0f, 0.040f, 0.060f, 0.003f) + mode(t, 291.0f, 0.028f, 0.045f, 0.003f) + mode(t, 402.0f, 0.016f, 0.030f, 0.003f) +
                 mode(t, 510.0f, 0.010f, 0.020f, 0.003f);
        });
      for (int e = 0; e < 2; ++e)  // the ride: a quiet "ting" on the swung eighths (the 2nd late, softer)
        add(at + (e == 1 ? 0.62f * beat : 0.0f), 0.5f, [e](float t) {
          const float g = e == 1 ? 0.55f : 1.0f;
          return g * (mode(t, 2093.0f, 0.010f, 0.12f, 0.002f) + mode(t, 3310.0f, 0.006f, 0.07f, 0.002f) + mode(t, 4470.0f, 0.003f, 0.04f, 0.002f));
        });
    }
  }
  // one fixed level for every phrase (r230 normalised each phrase alone), the last 80 ms faded (phrases are laid end to end)
  constexpr float kLevel = 0.88f;
  const size_t fade = static_cast<size_t>(0.08f * kRate);
  out->assign(v.size(), 0);
  for (size_t s = 0; s < v.size(); ++s) {
    const float f = s + fade > v.size() ? static_cast<float>(v.size() - s) / static_cast<float>(fade) : 1.0f;
    (*out)[s] = static_cast<short>(std::max(-32000.0f, std::min(32000.0f, v[s] * kLevel * f * 32000.0f)));
  }
}

// ---- a small software rasteriser (the shop's picture of it, the PC proofs): textured, z-buffered, the vault shader's light in short
struct Cam {
  V3 eye, at, up{0, 1, 0};
  float fovY = 0.6f;  // rad
};
struct TexImg {
  const uint8_t* rgba = nullptr;  // straight RGBA, rows top first
  int w = 0, h = 0;
};
inline void sample(const TexImg& t, float u, float v, float* o) {
  o[0] = o[1] = o[2] = 0, o[3] = 1;
  if (t.rgba == nullptr || t.w <= 0) return;
  const float x = std::min(std::max(u, 0.0f), 1.0f) * (t.w - 1), y = std::min(std::max(v, 0.0f), 1.0f) * (t.h - 1);
  const int x0 = static_cast<int>(x), y0 = static_cast<int>(y), x1 = std::min(t.w - 1, x0 + 1), y1 = std::min(t.h - 1, y0 + 1);
  const float fx = x - x0, fy = y - y0;
  for (int c = 0; c < 4; ++c) {
    auto at = [&](int xx, int yy) { return t.rgba[(static_cast<size_t>(yy) * t.w + xx) * 4 + c] / 255.0f; };
    o[c] = (at(x0, y0) * (1 - fx) + at(x1, y0) * fx) * (1 - fy) + (at(x0, y1) * (1 - fx) + at(x1, y1) * fx) * fy;
  }
}
// draws the posed case into `out` (straight RGBA, W x H; alpha 0 where nothing): `glowK` its glow's strength, `lit` the gallery's warm key
inline void render_ss(const Mesh& m, const Pose& p, const TexImg tex[kTexCount], const Cam& cam, int W, int H, std::vector<uint8_t>* out);
// (drawn at 2x and box-filtered down: smooth edges, a smooth glow)
inline void render(const Mesh& m, const Pose& p, const TexImg tex[kTexCount], const Cam& cam, int W, int H, std::vector<uint8_t>* out) {
  std::vector<uint8_t> big;
  render_ss(m, p, tex, cam, 2 * W, 2 * H, &big);
  out->assign(static_cast<size_t>(W) * H * 4, 0);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (int k = 0; k < 4; ++k) {
        const uint8_t* s = &big[(static_cast<size_t>(2 * y + (k >> 1)) * (2 * W) + 2 * x + (k & 1)) * 4];
        const float a = s[3] / 255.0f;
        acc[0] += s[0] * a, acc[1] += s[1] * a, acc[2] += s[2] * a, acc[3] += a;
      }
      uint8_t* o = &(*out)[(static_cast<size_t>(y) * W + x) * 4];
      for (int c = 0; c < 3; ++c) o[c] = static_cast<uint8_t>(acc[3] > 0 ? std::min(255.0f, acc[c] / acc[3]) : 0.0f);
      o[3] = static_cast<uint8_t>(std::min(255.0f, acc[3] / 4.0f * 255.0f + 0.5f));
    }
}
inline void render_ss(const Mesh& m, const Pose& p, const TexImg tex[kTexCount], const Cam& cam, int W, int H, std::vector<uint8_t>* out) {
  std::vector<float> pn;
  pose(m, p, &pn);
  out->assign(static_cast<size_t>(W) * H * 4, 0);
  std::vector<float> zb(static_cast<size_t>(W) * H, 1e9f), acc(static_cast<size_t>(W) * H * 3, 0.0f);
  const V3 f = norm(cam.at - cam.eye), r = norm(cross(f, cam.up)), u = cross(r, f);
  const float k = 0.5f * H / std::tan(0.5f * cam.fovY);
  auto proj = [&](V3 P, float* sx, float* sy, float* sz) {
    const V3 q = P - cam.eye;
    const float z = dot(q, f);
    *sz = z;
    *sx = 0.5f * W + k * dot(q, r) / std::max(1e-4f, z);
    *sy = 0.5f * H - k * dot(q, u) / std::max(1e-4f, z);
  };
  const V3 key = norm(v3(0.25f, 0.9f, 0.45f));
  for (int pass = 0; pass < 2; ++pass)
    for (const Part& part : m.parts) {
      if (part.soft != (pass == 1)) continue;
      for (uint32_t t = part.first; t < part.first + part.count; t += 3) {
        const uint32_t ia = m.idx[t], ib = m.idx[t + 1], ic = m.idx[t + 2];
        const float* A = &pn[ia * 6];
        const float* B = &pn[ib * 6];
        const float* C = &pn[ic * 6];
        if (A[3] == 0 && A[4] == 0 && A[5] == 0) continue;
        float ax, ay, az, bx, by, bz, cx, cy, cz;
        proj(v3(A[0], A[1], A[2]), &ax, &ay, &az);
        proj(v3(B[0], B[1], B[2]), &bx, &by, &bz);
        proj(v3(C[0], C[1], C[2]), &cx, &cy, &cz);
        if (az < 0.01f || bz < 0.01f || cz < 0.01f) continue;
        const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (std::fabs(area) < 1e-6f) continue;
        const int X0 = std::max(0, static_cast<int>(std::floor(std::min({ax, bx, cx})))), X1 = std::min(W - 1, static_cast<int>(std::ceil(std::max({ax, bx, cx}))));
        const int Y0 = std::max(0, static_cast<int>(std::floor(std::min({ay, by, cy})))), Y1 = std::min(H - 1, static_cast<int>(std::ceil(std::max({ay, by, cy}))));
        const V3 n = norm(v3(A[3], A[4], A[5]));
        const float code = std::sqrt(A[3] * A[3] + A[4] * A[4] + A[5] * A[5]);
        const V3 mid = v3((A[0] + B[0] + C[0]) / 3, (A[1] + B[1] + C[1]) / 3, (A[2] + B[2] + C[2]) / 3);
        V3 vv = norm(cam.eye - mid);
        V3 nn = dot(n, vv) < 0 ? n * -1.0f : n;
        if (code < 5.5f && dot(n, vv) < 0) continue;  // (solid faces from their own side)
        for (int y = Y0; y <= Y1; ++y)
          for (int x = X0; x <= X1; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area, w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area, w2 = 1 - w0 - w1;
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            const float iz = w0 / az + w1 / bz + w2 / cz, z = 1.0f / iz;
            const size_t pi = static_cast<size_t>(y) * W + x;
            const float uu = (w0 * m.uv[ia * 2] / az + w1 * m.uv[ib * 2] / bz + w2 * m.uv[ic * 2] / cz) * z;
            const float vq = (w0 * m.uv[ia * 2 + 1] / az + w1 * m.uv[ib * 2 + 1] / bz + w2 * m.uv[ic * 2 + 1] / cz) * z;
            float s[4];
            sample(tex[part.tex], uu, vq, s);
            if (pass == 1) {  // (glow: added where it isn't behind the case - warm gold, as the shader's)
              if (z >= zb[pi]) continue;
              const float g = s[3] * p.glow;
              acc[pi * 3] += 1.0f * g * 0.5f, acc[pi * 3 + 1] += 0.86f * g * 0.5f, acc[pi * 3 + 2] += 0.58f * g * 0.5f;
              continue;
            }
            if (z >= zb[pi]) continue;
            zb[pi] = z;
            const float diff = std::max(0.0f, dot(nn, key));
            const V3 h = norm(key + vv);
            const float spec = std::pow(std::max(0.0f, dot(nn, h)), 60.0f);
            const bool metal = code > 1.5f && code < 2.5f;
            const float foil = s[3] < 0.985f ? std::min(1.0f, (0.985f - s[3]) * 2.0f) : 0.0f;
            float L = 0.42f + 0.62f * diff, sp = 0.04f * spec;
            if (metal || foil > 0.3f) L = 0.55f + 0.5f * diff, sp = 0.45f * spec + 0.08f;
            if (code > 2.5f && code < 3.5f) L = 0.5f + 0.35f * diff + 0.35f * std::pow(1.0f - std::max(0.0f, dot(nn, vv)), 2.5f);
            const bool inside = (code > 2.5f && code < 3.5f) || (code > 4.5f && code < 5.5f);
            const float warm = inside ? 0.45f * p.glow : 0.0f;
            uint8_t* o = &(*out)[pi * 4];
            const float spm = metal || foil > 0.3f ? 1.0f : 0.0f;  // (metal: its glint in its own colour, never blown to white)
            const float rr = s[0] * L + sp * (1.0f - 0.5f * spm + 0.5f * spm * s[0]) + warm * 1.0f * s[0] + warm * 0.10f,
                        gg = s[1] * L + sp * (0.92f - 0.5f * spm + 0.5f * spm * s[1]) + warm * 0.84f * s[1] + warm * 0.07f,
                        bb = s[2] * L + sp * (0.8f - 0.5f * spm + 0.5f * spm * s[2]) + warm * 0.55f * s[2] + warm * 0.03f;
            o[0] = static_cast<uint8_t>(std::min(255.0f, rr * 255.0f));
            o[1] = static_cast<uint8_t>(std::min(255.0f, gg * 255.0f));
            o[2] = static_cast<uint8_t>(std::min(255.0f, bb * 255.0f));
            o[3] = 255;
          }
      }
    }
  for (size_t i = 0; i < static_cast<size_t>(W) * H; ++i) {
    uint8_t* o = &(*out)[i * 4];
    const float a = std::min(1.0f, std::max(acc[i * 3], std::max(acc[i * 3 + 1], acc[i * 3 + 2])));
    if (o[3] == 0 && a > 0.004f) {  // (glow over nothing: its own colour at its strength)
      o[0] = static_cast<uint8_t>(std::min(255.0f, acc[i * 3] / std::max(a, 1e-4f) * 255.0f));
      o[1] = static_cast<uint8_t>(std::min(255.0f, acc[i * 3 + 1] / std::max(a, 1e-4f) * 255.0f));
      o[2] = static_cast<uint8_t>(std::min(255.0f, acc[i * 3 + 2] / std::max(a, 1e-4f) * 255.0f));
      o[3] = static_cast<uint8_t>(a * 255.0f);
    } else {
      for (int c = 0; c < 3; ++c) o[c] = static_cast<uint8_t>(std::min(255.0f, o[c] + acc[i * 3 + c] * 255.0f));
    }
  }
}

}  // namespace xrvault
