// strikers-quest: the celebration pieces as small 3D meshes for xr_fx.cpp (Lance, 2026-10-10: "more
// depth"), built once when the effects renderer starts. Each piece is about one unit tall, centred
// on the origin; xr_fx.cpp scales it by the particle's size. Paper and streamers keep their width
// apart (wid) so the particle's aspect can make them narrow.
//
// Surfaces are parametric patches over u, v in [0, 1]. The outward normal is cross(dP/dv, dP/du):
// for a solid of revolution u runs around the axis and v along the profile, which goes out along
// the bottom, up the side and in across the top. The shader draws both faces (no culling), so only
// the normals have to point the right way.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

namespace xrfx::mesh {

struct Vertex {
  float pos[3];
  float wid[3];  // added times the particle's aspect (paper width); zero for solid pieces
  float nrm[3];
  float mat;     // base colour: 0 main, 1 detail, 2 cream, 3 dark
  float aux;     // a surface parameter the shader draws detail with (per shape)
};
static_assert(sizeof(Vertex) == 44, "mesh vertex layout");

enum Id : uint8_t {
  kPaperBent, kPaperCurl, kPaperTwist, kStreamer,
  kCoin, kHeart, kStar, kPetal, kMushroom, kEgg, kBanana, kShell, kHammer, kRose, kGarlic, kBow,
  kCap,  // r43c: the hat trick's hats
  // r57: the trophy shelf in your room: a cup (about a unit tall; the cup main colour, its base the
  // detail colour) and the shelf itself (in meters: wood main colour, trim detail, brackets dark).
  kTrophy, kShelf,
  // r61: the home ground: a hanging banner on a rod (a unit tall), a pennant on a pole (a unit tall),
  // a ball (a unit across).
  kBanner, kPennant, kBall,
  kPedestal,  // r61: the cup suite's pedestal, a unit tall (wood column, gold trim, dark base)
  // r82: the themed celebrations' pieces (C:\StrikersMR\CELEBRATION-THEMES.md), each about a unit tall.
  // aux tells xr_fx_mesh.frag the surface: 0 glossy, 1 metal, 2 rough stone, 3 gem / ice, 4 wood.
  kSword, kShield, kRod, kPokeBall, kDumbbell, kRock, kBone, kApple, kMissile, kPill,
  kRupee, kTriforce, kIceBlock, kIcicle, kGlove, kNote, kBat, kShard, kNeedle, kYoyo,
  kRug,  // r89: the home corner's round rug, a unit across, a little thick, face up (+y)
  // r124: the Marvel heroes' own pieces (web, spider, claws, star shield, skull, vampire bat, sai, snowflake,
  // chain link, mystic rune)
  kWeb, kSpider, kClaws, kStarShield, kSkull, kVampBat, kSai, kSnowflake, kChainLink, kRune,
  // r127: Thor's hammer, Hawkeye's arrow
  kMjolnir, kArrow,
  kSpoon,  // r127: Abra / Kadabra / Alakazam's spoon
  // r130: the new roster's pieces (20 Pokemon lines, 6 heroes), each LIGHT (under ~60 triangles: the celebrations
  // were too heavy on the GPU): a crescent moon, a "4" badge, a stretching arm, a surfboard, an acorn, a horn, a crown,
  // a spiral fossil, a talon, a spike, a toadstool, a lily pad, a wool puff, a leaf, sound waves, a boxing glove, an
  // impact burst, a spinning top, a palm, a twig, a cut gem, a lotus, lips, a heart, a star, a dog tag.
  kCrescent, kBadge4, kStretchArm, kSurfboard, kAcorn, kHorn, kCrown, kFossil, kTalon, kSpike, kToadstool, kLilyPad,
  kWool, kLeaf, kSoundWave, kGloveLite, kImpact, kTop, kPalm, kTwig, kGem, kLotus, kLips, kHeartLite, kStarLite, kDogTag,
  kCount
};

struct Range {
  uint32_t firstIndex = 0, indexCount = 0;
};

struct Set {
  std::vector<Vertex> verts;
  std::vector<uint32_t> indices;  // r82: 32-bit (the themed pieces took the set past 16 bits)
  Range ranges[kCount];
};

namespace detail {

constexpr float kPi = 3.14159265f;

struct V3 {
  float x, y, z;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline V3 normalize(V3 a) {
  const float l = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
  return l > 1e-12f ? a * (1.0f / l) : V3{0.0f, 1.0f, 0.0f};
}
inline float smooth(float a, float b, float x) {
  const float t = std::min(std::max((x - a) / (b - a), 0.0f), 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

// Row-major 3x3 rotations.
struct M3 {
  float m[9];
};
inline M3 rot_x(float a) { const float c = std::cos(a), s = std::sin(a); return {{1, 0, 0, 0, c, -s, 0, s, c}}; }
inline M3 rot_y(float a) { const float c = std::cos(a), s = std::sin(a); return {{c, 0, s, 0, 1, 0, -s, 0, c}}; }
inline M3 rot_z(float a) { const float c = std::cos(a), s = std::sin(a); return {{c, -s, 0, s, c, 0, 0, 0, 1}}; }
inline V3 apply(const M3& r, V3 v) {
  return {r.m[0] * v.x + r.m[1] * v.y + r.m[2] * v.z, r.m[3] * v.x + r.m[4] * v.y + r.m[5] * v.z,
          r.m[6] * v.x + r.m[7] * v.y + r.m[8] * v.z};
}

// (r, h) about an axis: 0 = x, 1 = y, 2 = z. Cyclic forms of (r cos t, h, r sin t), so each keeps the
// outward-normal rule above.
inline V3 revolve(int axis, float r, float h, float t) {
  const float c = std::cos(t), s = std::sin(t);
  if (axis == 0) return {h, r * s, r * c};
  if (axis == 2) return {r * s, r * c, h};
  return {r * c, h, r * s};
}

struct Builder {
  Set& out;

  static V3 no_wid(float, float) { return {0.0f, 0.0f, 0.0f}; }
  static float no_aux(float, float) { return 0.0f; }

  // (nu + 1) x (nv + 1) vertices; normals from the surface at aspect 1.
  template <class Pos, class Wid, class Aux>
  void patch(int nu, int nv, float mat, Pos pos, Wid wid, Aux aux) {
    const uint32_t base = static_cast<uint32_t>(out.verts.size());
    auto full = [&](float u, float v) { return pos(u, v) + wid(u, v); };
    for (int j = 0; j <= nv; ++j) {
      for (int k = 0; k <= nu; ++k) {
        const float u = static_cast<float>(k) / nu, v = static_cast<float>(j) / nv;
        // At a pole (radius 0) the u derivative vanishes: take the normal just off it.
        const float vc = std::min(std::max(v, 4e-3f), 1.0f - 4e-3f);
        const float e = 1e-3f;
        const V3 du = full(u + e, vc) - full(u - e, vc);
        const V3 dv = full(u, vc + e) - full(u, vc - e);
        const V3 n = normalize(cross(dv, du));
        const V3 p = pos(u, v), w = wid(u, v);
        out.verts.push_back(Vertex{{p.x, p.y, p.z}, {w.x, w.y, w.z}, {n.x, n.y, n.z}, mat, aux(u, v)});
      }
    }
    for (int j = 0; j < nv; ++j) {
      for (int k = 0; k < nu; ++k) {
        const uint32_t a = base + j * (nu + 1) + k, b = a + 1, c = a + (nu + 1), d = c + 1;
        for (uint32_t i : {a, b, c, b, d, c}) out.indices.push_back(i);
      }
    }
  }
  template <class Pos, class Aux>
  void patch(int nu, int nv, float mat, Pos pos, Aux aux) {
    patch(nu, nv, mat, pos, no_wid, aux);
  }

  // A solid of revolution; prof(u, v) gives (r, h) and may vary around the axis (lobes, waves).
  template <class Prof, class Aux>
  void lathe(int axis, int segs, int nv, float mat, Prof prof, Aux aux) {
    patch(segs, nv, mat,
          [=](float u, float v) {
            const auto rh = prof(u, v);
            return revolve(axis, rh.first, rh.second, 2.0f * kPi * u);
          },
          aux);
  }
  // A polyline profile with a hard edge at every point (coin rims, hammer head).
  void lathe_poly(int axis, int segs, float mat, std::initializer_list<std::pair<float, float>> pts) {
    const std::vector<std::pair<float, float>> p(pts);
    for (size_t i = 0; i + 1 < p.size(); ++i) {
      const auto a = p[i], b = p[i + 1];
      lathe(axis, segs, 1, mat,
            [=](float, float v) {
              return std::pair<float, float>{a.first + (b.first - a.first) * v, a.second + (b.second - a.second) * v};
            },
            no_aux);
    }
  }
  // A pillowy shape: an outline in x-y (clockwise from the top, as u grows) inflated along z.
  template <class Outline, class Bend>
  void puffy(int nu, int nv, float mat, float cx, float cy, float depth, float fullness, Outline outline, Bend bend) {
    patch(nu, nv, mat,
          [=](float u, float v) {
            const float phi = -0.5f * kPi + kPi * v;
            const float s = std::pow(std::max(std::cos(phi), 0.0f), fullness);
            const auto o = outline(2.0f * kPi * u);
            const float x = cx + (o.first - cx) * s, y = cy + (o.second - cy) * s;
            return V3{x, y, depth * std::sin(phi) + bend(x, y)};
          },
          no_aux);
  }
  // An ellipsoid, radii (a, b, c) on x, y, z.
  void ellipsoid(int nu, int nv, float mat, float a, float b, float c) {
    patch(nu, nv, mat,
          [=](float u, float v) {
            const float phi = -0.5f * kPi + kPi * v, t = 2.0f * kPi * u;
            return V3{a * std::cos(phi) * std::sin(t), b * std::cos(phi) * std::cos(t), c * std::sin(phi)};
          },
          no_aux);
  }
  void box(V3 c, V3 h, float mat) {
    const V3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    const float hs[3] = {h.x, h.y, h.z};
    for (int a = 0; a < 3; ++a) {
      const V3 n = axes[a], s = axes[(a + 1) % 3], t = axes[(a + 2) % 3];
      const float hn = hs[a], hss = hs[(a + 1) % 3], ht = hs[(a + 2) % 3];
      for (float side : {1.0f, -1.0f}) {
        const uint32_t base = static_cast<uint32_t>(out.verts.size());
        const V3 nn = n * side;
        for (int k = 0; k < 4; ++k) {
          const float su = (k & 1) ? 1.0f : -1.0f, tv = (k & 2) ? 1.0f : -1.0f;
          const V3 p = c + nn * hn + s * (su * hss) + t * (tv * ht);
          out.verts.push_back(Vertex{{p.x, p.y, p.z}, {0, 0, 0}, {nn.x, nn.y, nn.z}, mat, 0.0f});
        }
        for (uint32_t i : {0u, 1u, 2u, 1u, 3u, 2u}) out.indices.push_back(base + i);
      }
    }
  }
  // r82: the surface tag (aux) of the vertices added since `from`.
  void tag(size_t from, float aux) {
    for (size_t i = from; i < out.verts.size(); ++i) out.verts[i].aux = aux;
  }
  // Turns and moves the vertices added since `from`.
  void xform(size_t from, const M3& r, V3 t) {
    for (size_t i = from; i < out.verts.size(); ++i) {
      Vertex& vx = out.verts[i];
      const V3 p = apply(r, V3{vx.pos[0], vx.pos[1], vx.pos[2]}) + t;
      const V3 n = apply(r, V3{vx.nrm[0], vx.nrm[1], vx.nrm[2]});
      vx.pos[0] = p.x; vx.pos[1] = p.y; vx.pos[2] = p.z;
      vx.nrm[0] = n.x; vx.nrm[1] = n.y; vx.nrm[2] = n.z;
    }
  }
};

// Catmull-Rom through (r, h) points, t in [0, 1] over the whole list.
inline std::pair<float, float> spline(const std::vector<std::pair<float, float>>& p, float t) {
  const int n = static_cast<int>(p.size()) - 1;
  const float f = std::min(std::max(t, 0.0f), 1.0f) * n;
  const int i = std::min(static_cast<int>(f), n - 1);
  const float s = f - i;
  auto at = [&](int k) { return p[std::min(std::max(k, 0), n)]; };
  const auto p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
  auto cr = [&](float a, float b, float c, float d) {
    return 0.5f * (2.0f * b + (-a + c) * s + (2.0f * a - 5.0f * b + 4.0f * c - d) * s * s +
                   (-a + 3.0f * b - 3.0f * c + d) * s * s * s);
  };
  return {cr(p0.first, p1.first, p2.first, p3.first), cr(p0.second, p1.second, p2.second, p3.second)};
}

using RH = std::pair<float, float>;

}  // namespace detail

inline Set build(float detail) {
  using namespace detail;
  Set set;
  Builder b{set};
  // Segment counts scale with the detail (1 = close up, less for far and landed pieces).
  auto n = [detail](int x, int lo) { return std::max(lo, static_cast<int>(x * detail + 0.5f)); };
  auto start = [&](Id id) { set.ranges[id].firstIndex = static_cast<uint32_t>(set.indices.size()); };
  auto finish = [&](Id id) {
    set.ranges[id].indexCount = static_cast<uint32_t>(set.indices.size()) - set.ranges[id].firstIndex;
  };
  auto none = [](float, float) { return 0.0f; };
  auto flat = [](float, float) { return 0.0f; };

  // Paper: a card bent along its length, one curled across its width, one twisted like a propeller.
  start(kPaperBent);
  b.patch(1, 6, 0.0f, [](float, float v) { const float y = v - 0.5f; return V3{0.0f, y, 0.28f * (y * y - 1.0f / 12.0f)}; },
          [](float u, float) { return V3{u - 0.5f, 0.0f, 0.0f}; }, none);
  finish(kPaperBent);
  start(kPaperCurl);
  b.patch(4, 1, 0.0f, [](float, float v) { return V3{0.0f, v - 0.5f, 0.0f}; },
          [](float u, float) { const float x = u - 0.5f; return V3{x, 0.0f, 0.5f * (x * x - 1.0f / 12.0f)}; }, none);
  finish(kPaperCurl);
  start(kPaperTwist);
  b.patch(1, 5, 0.0f, [](float, float v) { return V3{0.0f, v - 0.5f, 0.0f}; },
          [](float u, float v) {
            const float x = u - 0.5f, t = 1.4f * (v - 0.5f);
            return V3{x * std::cos(t), 0.0f, x * std::sin(t)};
          },
          none);
  finish(kPaperTwist);
  // Streamers: a curly ribbon, its centre line a loose helix, the ribbon turning as it goes.
  start(kStreamer);
  b.patch(1, n(30, 10), 0.0f,
          [](float, float v) { return V3{0.05f * std::cos(3.0f * kPi * v), v - 0.5f, 0.05f * std::sin(3.0f * kPi * v)}; },
          [](float u, float v) {
            const float x = u - 0.5f, t = 5.0f * kPi * v;
            return V3{x * std::cos(t), 0.0f, x * std::sin(t)};
          },
          none);
  finish(kStreamer);

  // Mario: a gold coin with a raised rim and the slot bar on both faces (faces along z).
  start(kCoin);
  b.lathe_poly(2, n(28, 10), 0.0f,
               {{0.0f, -0.035f}, {0.38f, -0.035f}, {0.40f, -0.065f}, {0.47f, -0.065f}, {0.5f, -0.035f}, {0.5f, 0.035f},
                {0.47f, 0.065f}, {0.40f, 0.065f}, {0.38f, 0.035f}, {0.0f, 0.035f}});
  b.box({0.0f, 0.0f, 0.05f}, {0.065f, 0.22f, 0.017f}, 1.0f);
  b.box({0.0f, 0.0f, -0.05f}, {0.065f, 0.22f, 0.017f}, 1.0f);
  finish(kCoin);

  // Peach: a plump heart.
  start(kHeart);
  b.puffy(n(40, 16), n(10, 4), 0.0f, 0.0f, 0.02f, 0.17f, 0.6f,
          [](float t) {
            const float s = std::sin(t);
            return RH{16.0f * s * s * s / 34.0f,
                      (13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t)) /
                              34.0f + 0.07f};
          },
          flat);
  finish(kHeart);

  // Super Strikes: a puffy Super Star (the shader gives it eyes).
  start(kStar);
  b.puffy(n(40, 20), n(8, 4), 0.0f, 0.0f, 0.0f, 0.16f, 0.55f,
          [](float t) {
            const float k = std::pow(0.5f + 0.5f * std::cos(5.0f * t), 1.6f);
            const float r = 0.25f + 0.25f * k;
            return RH{r * std::sin(t), r * std::cos(t)};
          },
          flat);
  finish(kStar);

  // Daisy: a long petal, cupped and bent back.
  start(kPetal);
  b.puffy(n(24, 10), n(6, 3), 0.0f, 0.0f, 0.0f, 0.035f, 0.5f,
          [](float t) { return RH{0.21f * std::sin(t), 0.48f * std::cos(t)}; },
          [](float x, float y) { return 0.32f * x * x + 0.14f * y * y; });
  finish(kPetal);

  // Toad and Luigi: a Super / 1-Up Mushroom. Stem (cream, the shader adds its eyes), gills, cap.
  start(kMushroom);
  b.lathe_poly(1, n(24, 10), 2.0f, {{0.0f, -0.5f}, {0.19f, -0.5f}});
  b.lathe(1, n(24, 10), n(6, 2), 2.0f,
          [](float, float v) { return RH{0.19f + 0.045f * std::sin(kPi * v) - 0.03f * v, -0.5f + 0.49f * v}; }, none);
  b.lathe(1, n(24, 10), 1, 2.0f, [](float, float v) { return RH{0.5f * v, -0.06f * v}; }, [](float, float) { return 1.0f; });
  b.lathe(1, n(24, 10), n(8, 3), 0.0f,
          [](float, float v) {
            const float phi = 0.5f * kPi * v;
            return RH{0.5f * std::cos(phi), -0.06f + 0.48f * std::sin(phi)};
          },
          none);
  finish(kMushroom);

  // Yoshi: a spotted egg (the shader paints the spots).
  start(kEgg);
  b.lathe(1, n(24, 10), n(12, 5), 0.0f,
          [](float, float v) {
            const float phi = -0.5f * kPi + kPi * v, y = std::sin(phi);
            return RH{0.36f * std::cos(phi) * (1.0f - 0.12f * y), 0.5f * y};
          },
          none);
  finish(kEgg);

  // Donkey Kong: a banana, a tapering tube along an arc, brown tips (aux = along).
  start(kBanana);
  b.patch(n(12, 6), n(20, 8), 0.0f,
          [](float u, float v) {
            const float a = -0.95f + 1.9f * v, t = 2.0f * kPi * u;
            const V3 c{0.6f * std::sin(a), 0.47f - 0.6f * std::cos(a), 0.0f};
            const V3 n{std::sin(a), -std::cos(a), 0.0f}, bn{0.0f, 0.0f, 1.0f};
            const float r = 0.12f * std::pow(std::max(std::sin(kPi * v), 0.0f), 0.5f) * (1.0f + 0.07f * std::cos(5.0f * t));
            return c + n * (r * std::cos(t)) + bn * (r * std::sin(t));
          },
          [](float, float v) { return v; });
  finish(kBanana);

  // Koopa: a shell, cream underside and rim, the dome (the shader draws the plates).
  start(kShell);
  b.lathe_poly(1, n(24, 10), 1.0f, {{0.0f, -0.14f}, {0.44f, -0.14f}});
  b.lathe(1, n(24, 10), n(4, 2), 1.0f,
          [](float, float v) {
            const float a = -0.5f * kPi + kPi * v;
            return RH{0.44f + 0.07f * std::cos(a), -0.07f + 0.07f * std::sin(a)};
          },
          none);
  b.lathe(1, n(24, 10), n(7, 3), 0.0f,
          [](float, float v) {
            const float phi = 0.5f * kPi * v;
            return RH{0.44f * std::cos(phi), 0.34f * std::sin(phi)};
          },
          none);
  finish(kShell);

  // Hammer Bro: a wooden handle under a steel head.
  start(kHammer);
  b.lathe(1, n(12, 6), n(3, 1), 0.0f,
          [](float, float v) {
            const float phi = -0.5f * kPi + 0.5f * kPi * v;
            return RH{0.055f * std::cos(phi), -0.445f + 0.055f * std::sin(phi)};
          },
          none);
  b.lathe(1, n(12, 6), 1, 0.0f, [](float, float v) { return RH{0.055f, -0.445f + 0.6f * v}; }, none);
  {
    const size_t from = set.verts.size();
    b.lathe_poly(0, n(16, 8), 1.0f,
                 {{0.0f, -0.31f}, {0.13f, -0.31f}, {0.16f, -0.28f}, {0.16f, 0.28f}, {0.13f, 0.31f}, {0.0f, 0.31f}});
    b.xform(from, rot_y(0.0f), V3{0.0f, 0.3f, 0.0f});
  }
  finish(kHammer);

  // Waluigi: a rose, a ruffled bloom (aux = up the profile, for the swirl) on a stem with a leaf.
  start(kRose);
  {
    const std::vector<RH> prof = {{0.0f, 0.0f},   {0.1f, 0.01f},  {0.2f, 0.07f},  {0.26f, 0.17f}, {0.26f, 0.27f},
                                  {0.21f, 0.35f}, {0.12f, 0.33f}, {0.05f, 0.37f}, {0.0f, 0.38f}};
    b.lathe(1, n(30, 15), n(16, 7), 0.0f,
            [prof](float u, float v) {
              const auto rh = spline(prof, v);
              const float t = 2.0f * kPi * u;
              const float ruffle = smooth(0.3f, 0.7f, v);
              return RH{rh.first * (1.0f + 0.08f * std::sin(5.0f * t + 3.0f * v) * ruffle),
                        rh.second + 0.025f * std::sin(5.0f * t) * smooth(0.5f, 0.8f, v)};
            },
            [](float, float v) { return v; });
  }
  b.lathe_poly(1, n(8, 5), 1.0f, {{0.0f, -0.5f}, {0.028f, -0.5f}});
  b.lathe(1, n(8, 5), 1, 1.0f, [](float, float v) { return RH{0.028f, -0.5f + 0.52f * v}; }, none);
  {
    const size_t from = set.verts.size();
    b.puffy(n(14, 6), n(4, 2), 1.0f, 0.0f, 0.0f, 0.015f, 0.5f, [](float t) { return RH{0.07f * std::sin(t), 0.16f * std::cos(t)}; },
            [](float x, float) { return 0.6f * x * x; });
    b.xform(from, rot_z(-0.9f), V3{0.1f, -0.22f, 0.0f});
  }
  finish(kRose);

  // Wario: a garlic bulb, cloves bulging between creases (aux = how far from a crease), a pointed top.
  start(kGarlic);
  b.lathe(1, std::max(18, n(36, 18) / 6 * 6), n(14, 6), 0.0f,
          [](float u, float v) {
            const float phi = -0.5f * kPi + kPi * v, s = std::sin(phi), t = 2.0f * kPi * u;
            const float neck = smooth(0.35f, 1.0f, s);
            const float lobe = 1.0f + 0.1f * std::fabs(std::cos(3.0f * t)) * (1.0f - neck);
            return RH{0.42f * std::cos(phi) * (1.0f - 0.6f * neck) * lobe, -0.12f + 0.36f * s + 0.26f * neck * neck};
          },
          [](float u, float) { return std::fabs(std::cos(6.0f * kPi * u)); });
  finish(kGarlic);

  // Birdo: a bow, two loops and two tails around a knot.
  start(kBow);
  for (float side : {-1.0f, 1.0f}) {
    size_t from = set.verts.size();
    b.ellipsoid(n(16, 8), n(8, 4), 0.0f, 0.25f, 0.17f, 0.1f);
    b.xform(from, rot_z(side * 0.35f), V3{side * 0.23f, 0.03f, 0.0f});
    from = set.verts.size();
    b.ellipsoid(n(10, 6), n(6, 3), 0.0f, 0.06f, 0.15f, 0.05f);
    b.xform(from, rot_z(side * 0.4f), V3{side * 0.09f, -0.2f, -0.02f});
  }
  b.ellipsoid(n(12, 6), n(8, 4), 1.0f, 0.1f, 0.1f, 0.1f);
  finish(kBow);

  // r43c, the hat trick: a cap (Mario's kind). Crown and visor in the main colour, a dark inside, a
  // round badge (detail colour) on the front, a button on top. The visor points along +z.
  start(kCap);
  b.lathe(1, n(24, 10), n(8, 3), 0.0f,
          [](float, float v) {
            const float phi = 0.5f * kPi * v;
            return RH{0.42f * std::cos(phi), -0.12f + 0.36f * std::sin(phi)};
          },
          none);
  b.lathe_poly(1, n(24, 10), 3.0f, {{0.0f, -0.12f}, {0.42f, -0.12f}});
  b.patch(n(14, 6), 2, 0.0f,
          [](float u, float v) {
            const float a = -1.15f + 2.3f * u;
            const float r = 0.41f + 0.3f * v * std::cos(0.9f * a);
            return V3{r * std::sin(a), -0.12f - 0.035f * v, r * std::cos(a)};
          },
          none);
  {
    const size_t from = set.verts.size();
    b.ellipsoid(n(14, 6), n(6, 3), 1.0f, 0.12f, 0.12f, 0.015f);
    b.xform(from, rot_x(-0.49f), V3{0.0f, 0.05f, 0.375f});
  }
  {
    const size_t from = set.verts.size();
    b.ellipsoid(n(8, 4), n(4, 2), 0.0f, 0.035f, 0.025f, 0.035f);
    b.xform(from, rot_y(0.0f), V3{0.0f, 0.24f, 0.0f});
  }
  finish(kCap);

  // r57: a trophy cup, about a unit tall, standing on y = -0.5: an octagonal plinth (detail colour),
  // a stem with a knob, the bowl with a rolled rim, open at the top, and two loop handles (main
  // colour, polished by the shader).
  start(kTrophy);
  b.lathe_poly(1, 8, 1.0f, {{0.0f, -0.5f}, {0.3f, -0.5f}, {0.3f, -0.4f}, {0.24f, -0.36f}, {0.07f, -0.36f}});
  {
    const std::vector<RH> prof = {{0.07f, -0.36f}, {0.06f, -0.3f}, {0.11f, -0.22f},
                                  {0.05f, -0.12f}, {0.07f, -0.06f}, {0.14f, -0.02f}};
    b.lathe(1, n(24, 10), n(10, 4), 0.0f, [prof](float, float v) { return spline(prof, v); }, none);
  }
  {
    // Out and up the outside, over the rolled rim, down the inside to the bowl's floor.
    const std::vector<RH> prof = {{0.14f, -0.02f}, {0.27f, 0.08f}, {0.33f, 0.25f}, {0.36f, 0.4f}, {0.37f, 0.44f},
                                  {0.34f, 0.45f},  {0.31f, 0.4f},  {0.27f, 0.22f}, {0.12f, 0.08f}, {0.0f, 0.06f}};
    b.lathe(1, n(28, 12), n(24, 10), 0.0f, [prof](float, float v) { return spline(prof, v); }, none);
  }
  for (float side : {-1.0f, 1.0f}) {
    // A loop out from the bowl's side, from under the rim down to its waist: a tube round an arc.
    b.patch(n(10, 6), n(16, 8), 0.0f,
            [side](float u, float v) {
              const float a = kPi * v;
              const V3 c{side * (0.33f + 0.16f * std::sin(a)), 0.36f - 0.26f * v, 0.0f};
              const V3 tg = normalize(V3{side * 0.16f * kPi * std::cos(a), -0.26f, 0.0f});
              const V3 bn{0.0f, 0.0f, 1.0f};
              const V3 nm = cross(bn, tg);  // tg x nm = bn: the tube's normals then point out
              const float t = 2.0f * kPi * u, r = 0.028f;
              return c + nm * (r * std::cos(t)) - bn * (r * std::sin(t));
            },
            none);
  }
  finish(kTrophy);

  // r57: the shelf, in meters, y up, its front toward +z, the board's middle at the origin (its top at
  // y = 0.016): a rounded wooden board, a trim strip along the front, two dark brackets under it.
  // Rounded boxes: a superellipsoid built z-up like the ellipsoid, then turned so z is up.
  auto rbox = [&](float mat, float a, float bb, float c, float e, V3 at) {
    const size_t from = set.verts.size();
    auto sp = [e](float x) { return std::copysign(std::pow(std::fabs(x), e), x); };
    b.patch(n(28, 12), n(12, 6), mat,
            [=](float u, float v) {
              const float phi = -0.5f * kPi + kPi * v, t = 2.0f * kPi * u;
              return V3{a * sp(std::cos(phi)) * sp(std::sin(t)), bb * sp(std::cos(phi)) * sp(std::cos(t)), c * sp(std::sin(phi))};
            },
            none);
    b.xform(from, rot_x(-0.5f * kPi), at);
  };
  start(kShelf);
  rbox(0.0f, 0.46f, 0.11f, 0.016f, 0.3f, V3{0.0f, 0.0f, 0.0f});
  rbox(1.0f, 0.465f, 0.008f, 0.02f, 0.3f, V3{0.0f, 0.0f, 0.108f});
  for (float side : {-1.0f, 1.0f}) rbox(3.0f, 0.012f, 0.08f, 0.05f, 0.3f, V3{side * 0.33f, -0.06f, -0.02f});
  finish(kShelf);

  // r61: a banner a unit tall hanging from a gold rod: the cloth (main colour; the shader paints its
  // stripes and the captain's emblem from the position) with a swallowtail and a soft ripple.
  start(kBanner);
  {
    const size_t from = set.verts.size();
    b.lathe(0, n(10, 6), 1, 3.0f, [](float, float v) { return RH{0.018f, -0.27f + 0.54f * v}; }, none);
    b.xform(from, rot_y(0.0f), V3{0.0f, 0.5f, 0.0f});
  }
  for (float side : {-1.0f, 1.0f}) {
    const size_t from = set.verts.size();
    b.ellipsoid(n(10, 6), n(6, 3), 3.0f, 0.03f, 0.03f, 0.03f);
    b.xform(from, rot_y(0.0f), V3{side * 0.285f, 0.5f, 0.0f});
  }
  b.patch(n(10, 6), n(18, 8), 0.0f,
          [](float u, float v) {
            const float x = -0.21f + 0.42f * u;
            const float bottom = -0.49f + 0.17f * (1.0f - std::fabs(2.0f * u - 1.0f));
            const float y = 0.49f + (bottom - 0.49f) * v;
            return V3{x, y, 0.025f * std::sin(3.0f * kPi * u + 2.0f * v) * v};
          },
          none);
  finish(kBanner);

  // r61: a pennant a unit tall: a dark pole with a gold knob, the flag (main colour) off its top.
  start(kPennant);
  b.lathe(1, n(8, 5), 1, 3.0f, [](float, float v) { return RH{0.012f, -0.5f + v}; }, none);
  {
    const size_t from = set.verts.size();
    b.ellipsoid(n(10, 6), n(6, 3), 1.0f, 0.03f, 0.03f, 0.03f);
    b.xform(from, rot_y(0.0f), V3{0.0f, 0.51f, 0.0f});
  }
  b.patch(n(12, 6), n(6, 3), 0.0f,
          [](float u, float v) {
            const float x = 0.012f + 0.5f * u;
            const float y = (0.47f + (0.15f - 0.47f) * v) * (1.0f - u) + 0.31f * u;
            return V3{x, y, 0.02f * std::sin(2.5f * kPi * u) * u};
          },
          none);
  finish(kPennant);

  // r61: a ball a unit across (the Super Strike ball on its stand; the shader paints its panels).
  start(kBall);
  b.ellipsoid(n(28, 12), n(16, 8), 0.0f, 0.5f, 0.5f, 0.5f);
  finish(kBall);

  // r61: a pedestal a unit tall, standing on y = -0.5: a dark stepped base, an octagonal wooden
  // column, a gold cap the cup stands on (its top at y = 0.5).
  start(kPedestal);
  b.lathe_poly(1, 8, 3.0f, {{0.0f, -0.5f}, {0.34f, -0.5f}, {0.34f, -0.44f}, {0.29f, -0.44f}, {0.29f, -0.4f}});
  b.lathe_poly(1, 8, 0.0f, {{0.29f, -0.4f}, {0.22f, -0.38f}, {0.2f, 0.38f}, {0.24f, 0.4f}});
  b.lathe_poly(1, 8, 1.0f, {{0.24f, 0.4f}, {0.3f, 0.42f}, {0.3f, 0.5f}, {0.0f, 0.5f}});
  finish(kPedestal);

  // ---- r82: the themed celebrations' pieces (C:\StrikersMR\CELEBRATION-THEMES.md) ----
  auto tagged = [&](float aux, auto add) {
    const size_t from = set.verts.size();
    add();
    b.tag(from, aux);
  };
  auto placed = [&](float aux, const M3& r, V3 t, auto add) {
    const size_t from = set.verts.size();
    add();
    b.tag(from, aux);
    b.xform(from, r, t);
  };

  // A sword along +y: a diamond-section steel blade with a point (detail colour, metal), a crossguard
  // and pommel (main colour), a dark grip.
  start(kSword);
  tagged(1.0f, [&] {
    b.patch(4, n(8, 4), 1.0f,
            [](float u, float v) {
              const float w = 0.052f * (1.0f - 0.97f * smooth(0.8f, 1.0f, v));
              const float a = 2.0f * kPi * u;
              return V3{w * std::cos(a), -0.1f + 0.62f * v, 0.22f * w * std::sin(a)};
            },
            none);
  });
  tagged(0.0f, [&] { b.box({0.0f, -0.11f, 0.0f}, {0.15f, 0.022f, 0.032f}, 0.0f); });
  tagged(0.0f, [&] { b.lathe(1, n(10, 6), 1, 3.0f, [](float, float v) { return RH{0.028f, -0.4f + 0.28f * v}; }, none); });
  placed(0.0f, rot_y(0.0f), V3{0.0f, -0.43f, 0.0f}, [&] { b.ellipsoid(n(10, 6), n(6, 3), 0.0f, 0.045f, 0.045f, 0.045f); });
  finish(kSword);

  // A shield facing +z: a heater shape (main colour; the shader paints its emblem in the detail
  // colour), a steel rim behind its edge.
  start(kShield);
  {
    auto outline = [](float scale) {
      return [scale](float t) {
        const float s = std::sin(t), c = std::cos(t);
        const float x = 0.38f * s * (c < 0.0f ? 1.0f + 0.55f * c : 1.0f);
        const float y = c >= 0.0f ? 0.44f * std::pow(c, 0.25f) : 0.52f * c;
        return RH{x * scale, y * scale};
      };
    };
    auto bend = [](float x, float) { return -0.22f * x * x; };
    tagged(0.0f, [&] { b.puffy(n(32, 14), n(6, 3), 0.0f, 0.0f, 0.0f, 0.05f, 0.35f, outline(1.0f), bend); });
    placed(1.0f, rot_y(0.0f), V3{0.0f, 0.0f, -0.012f},
           [&] { b.puffy(n(32, 14), n(4, 2), 2.0f, 0.0f, 0.0f, 0.035f, 0.3f, outline(1.07f), bend); });
  }
  finish(kShield);

  // A lightning rod: a copper rod with a point (detail colour, metal), a glass ball on it (main
  // colour, glowing), a dark base.
  start(kRod);
  tagged(1.0f, [&] { b.lathe(1, n(8, 5), 1, 1.0f, [](float, float v) { return RH{0.022f, -0.44f + 0.8f * v}; }, none); });
  tagged(1.0f, [&] { b.lathe_poly(1, n(8, 5), 1.0f, {{0.022f, 0.36f}, {0.0f, 0.5f}}); });
  placed(3.0f, rot_y(0.0f), V3{0.0f, 0.06f, 0.0f}, [&] { b.ellipsoid(n(14, 8), n(8, 4), 0.0f, 0.075f, 0.075f, 0.075f); });
  tagged(1.0f, [&] { b.lathe_poly(1, n(12, 6), 3.0f, {{0.0f, -0.5f}, {0.09f, -0.5f}, {0.09f, -0.44f}, {0.0f, -0.44f}}); });
  finish(kRod);

  // A Poke Ball (the shader paints its halves, band and button).
  start(kPokeBall);
  b.ellipsoid(n(24, 12), n(14, 6), 0.0f, 0.5f, 0.5f, 0.5f);
  finish(kPokeBall);

  // A dumbbell lying along x: a steel bar (detail colour), two plates each end (main colour).
  start(kDumbbell);
  tagged(1.0f, [&] { b.lathe(0, n(10, 6), 1, 1.0f, [](float, float v) { return RH{0.035f, -0.46f + 0.92f * v}; }, none); });
  for (float side : {-1.0f, 1.0f}) {
    const float x0 = side < 0.0f ? -0.4f : 0.2f, x1 = side < 0.0f ? -0.2f : 0.4f;
    tagged(0.0f, [&] { b.lathe_poly(0, n(20, 10), 0.0f, {{0.0f, x0}, {0.2f, x0}, {0.2f, x1}, {0.0f, x1}}); });
  }
  finish(kDumbbell);

  // A rock: a lumpy ellipsoid (rough).
  start(kRock);
  tagged(2.0f, [&] {
    b.patch(n(18, 8), n(10, 5), 0.0f,
            [](float u, float v) {
              const float phi = -0.5f * kPi + kPi * v, t = 2.0f * kPi * u;
              const float r = 0.45f * (1.0f + 0.16f * std::sin(3.0f * t + 1.3f) * std::cos(2.0f * phi) +
                                       0.1f * std::sin(5.0f * t + 3.0f * phi) + 0.07f * std::cos(4.0f * phi + 0.5f));
              return V3{1.08f * r * std::cos(phi) * std::sin(t), 0.82f * r * std::cos(phi) * std::cos(t), r * std::sin(phi)};
            },
            none);
  });
  finish(kRock);

  // A bone along y: a shaft and two knobs at each end (main colour).
  start(kBone);
  tagged(0.0f, [&] { b.lathe(1, n(10, 6), 1, 0.0f, [](float, float v) { return RH{0.045f, -0.34f + 0.68f * v}; }, none); });
  for (float sy : {-1.0f, 1.0f}) {
    for (float sx : {-1.0f, 1.0f}) {
      placed(0.0f, rot_y(0.0f), V3{0.065f * sx, 0.37f * sy, 0.0f},
             [&] { b.ellipsoid(n(12, 6), n(8, 4), 0.0f, 0.085f, 0.085f, 0.085f); });
    }
  }
  finish(kBone);

  // An apple (main colour) with a dark stem and a leaf (detail colour).
  start(kApple);
  {
    const std::vector<RH> prof = {{0.0f, -0.4f}, {0.24f, -0.42f}, {0.41f, -0.26f}, {0.46f, 0.0f},
                                  {0.41f, 0.24f}, {0.27f, 0.38f}, {0.08f, 0.33f}, {0.0f, 0.3f}};
    tagged(0.0f, [&] { b.lathe(1, n(22, 10), n(12, 6), 0.0f, [prof](float, float v) { return spline(prof, v); }, none); });
  }
  placed(4.0f, rot_z(-0.25f), V3{0.0f, 0.3f, 0.0f},
         [&] { b.lathe(1, n(6, 4), 1, 3.0f, [](float, float v) { return RH{0.02f, 0.18f * v}; }, none); });
  placed(0.0f, rot_z(-0.9f), V3{0.11f, 0.42f, 0.0f},
         [&] { b.puffy(n(14, 6), n(4, 2), 1.0f, 0.0f, 0.0f, 0.015f, 0.5f, [](float t) { return RH{0.06f * std::sin(t), 0.13f * std::cos(t)}; }, flat); });
  finish(kApple);

  // A missile along y: body (main colour), nose and four fins (detail colour, metal).
  start(kMissile);
  tagged(0.0f, [&] { b.lathe_poly(1, n(14, 8), 0.0f, {{0.0f, -0.45f}, {0.12f, -0.45f}, {0.12f, 0.18f}}); });
  tagged(1.0f, [&] { b.lathe_poly(1, n(14, 8), 1.0f, {{0.12f, 0.18f}, {0.07f, 0.36f}, {0.0f, 0.48f}}); });
  for (int f = 0; f < 4; ++f) {
    placed(1.0f, rot_y(0.5f * kPi * f), V3{0.0f, 0.0f, 0.0f}, [&] { b.box({0.17f, -0.34f, 0.0f}, {0.06f, 0.1f, 0.008f}, 1.0f); });
  }
  finish(kMissile);

  // A capsule (Dr. Mario's pill) along y; the shader colours its halves.
  start(kPill);
  tagged(0.0f, [&] {
    b.lathe(1, n(16, 8), n(6, 3), 0.0f,
            [](float, float v) { const float phi = -0.5f * kPi + 0.5f * kPi * v; return RH{0.17f * std::cos(phi), -0.18f + 0.17f * std::sin(phi)}; },
            none);
    b.lathe(1, n(16, 8), 1, 0.0f, [](float, float v) { return RH{0.17f, -0.18f + 0.36f * v}; }, none);
    b.lathe(1, n(16, 8), n(6, 3), 0.0f,
            [](float, float v) { const float phi = 0.5f * kPi * v; return RH{0.17f * std::cos(phi), 0.18f + 0.17f * std::sin(phi)}; },
            none);
  });
  finish(kPill);

  // A rupee: a long hexagonal gem (main colour, a gem).
  start(kRupee);
  tagged(3.0f, [&] { b.lathe_poly(1, 6, 0.0f, {{0.0f, -0.5f}, {0.21f, -0.29f}, {0.21f, 0.29f}, {0.0f, 0.5f}}); });
  finish(kRupee);

  // The Triforce: three golden triangles facing +z (metal).
  start(kTriforce);
  {
    const float R = 0.2887f * 0.94f;
    const V3 at[3] = {{0.0f, 0.2887f, 0.0f}, {-0.25f, -0.1443f, 0.0f}, {0.25f, -0.1443f, 0.0f}};
    for (const V3& c : at) {
      placed(1.0f, rot_y(0.0f), c, [&] { b.lathe_poly(2, 3, 0.0f, {{0.0f, -0.05f}, {R, -0.05f}, {R, 0.05f}, {0.0f, 0.05f}}); });
    }
  }
  finish(kTriforce);

  // An ice block: a rounded cube (main colour, shining like a gem).
  start(kIceBlock);
  {
    const size_t from = set.verts.size();
    rbox(0.0f, 0.32f, 0.32f, 0.32f, 0.22f, V3{0.0f, 0.0f, 0.0f});
    b.tag(from, 3.0f);
  }
  finish(kIceBlock);

  // An icicle pointing down, rippled (main colour, ice).
  start(kIcicle);
  tagged(3.0f, [&] {
    b.lathe(1, n(10, 6), n(10, 5), 0.0f,
            [](float, float v) { return RH{0.13f * std::pow(v, 0.8f) * (1.0f + 0.1f * std::sin(18.0f * v)), -0.5f + v}; }, none);
    b.lathe_poly(1, n(10, 6), 0.0f, {{0.13f * 0.98f, 0.5f}, {0.0f, 0.5f}});
  });
  finish(kIcicle);

  // A boxing glove (main colour) with its thumb and a white cuff.
  start(kGlove);
  placed(0.0f, rot_y(0.0f), V3{0.0f, 0.08f, 0.0f}, [&] { b.ellipsoid(n(18, 8), n(10, 5), 0.0f, 0.3f, 0.36f, 0.27f); });
  placed(0.0f, rot_z(0.5f), V3{0.24f, -0.02f, 0.1f}, [&] { b.ellipsoid(n(12, 6), n(6, 3), 0.0f, 0.1f, 0.18f, 0.1f); });
  tagged(0.0f, [&] {
    b.lathe(1, n(16, 8), 1, 2.0f, [](float, float v) { return RH{0.21f, -0.48f + 0.26f * v}; }, none);
    b.lathe_poly(1, n(16, 8), 2.0f, {{0.0f, -0.48f}, {0.21f, -0.48f}});
  });
  finish(kGlove);

  // A music note (main colour): a tilted head, a stem and a flag.
  start(kNote);
  placed(0.0f, rot_z(0.4f), V3{-0.12f, -0.36f, 0.0f}, [&] { b.ellipsoid(n(16, 8), n(8, 4), 0.0f, 0.18f, 0.13f, 0.09f); });
  tagged(0.0f, [&] { b.box({0.045f, 0.06f, 0.0f}, {0.024f, 0.42f, 0.024f}, 0.0f); });
  placed(0.0f, rot_z(-0.9f), V3{0.15f, 0.34f, 0.0f}, [&] { b.ellipsoid(n(12, 6), n(6, 3), 0.0f, 0.15f, 0.05f, 0.035f); });
  finish(kNote);

  // A baseball bat along y (main colour, wood grain).
  start(kBat);
  {
    const std::vector<RH> prof = {{0.0f, -0.5f}, {0.055f, -0.49f}, {0.055f, -0.455f}, {0.028f, -0.43f}, {0.03f, -0.1f},
                                  {0.058f, 0.15f}, {0.074f, 0.38f}, {0.07f, 0.48f}, {0.0f, 0.5f}};
    tagged(4.0f, [&] { b.lathe(1, n(14, 8), n(16, 8), 0.0f, [prof](float, float v) { return spline(prof, v); }, none); });
  }
  finish(kBat);

  // A metal shard: a long jagged three-sided splinter (detail colour, metal).
  start(kShard);
  tagged(1.0f, [&] { b.lathe_poly(1, 3, 1.0f, {{0.0f, -0.5f}, {0.15f, -0.2f}, {0.09f, 0.32f}, {0.0f, 0.5f}}); });
  finish(kShard);

  // A pin / needle along y: steel (detail colour, metal) with a round head (main colour).
  start(kNeedle);
  tagged(1.0f, [&] { b.lathe_poly(1, n(8, 5), 1.0f, {{0.0f, -0.5f}, {0.018f, -0.3f}, {0.018f, 0.36f}, {0.0f, 0.36f}}); });
  placed(0.0f, rot_y(0.0f), V3{0.0f, 0.41f, 0.0f}, [&] { b.ellipsoid(n(10, 6), n(6, 3), 0.0f, 0.07f, 0.07f, 0.07f); });
  finish(kNeedle);

  // A yo-yo facing +z (main colour).
  start(kYoyo);
  tagged(0.0f, [&] {
    b.lathe_poly(2, n(20, 10), 0.0f,
                 {{0.0f, -0.2f}, {0.42f, -0.2f}, {0.46f, -0.16f}, {0.46f, -0.05f}, {0.1f, -0.03f},
                  {0.1f, 0.03f}, {0.46f, 0.05f}, {0.46f, 0.16f}, {0.42f, 0.2f}, {0.0f, 0.2f}});
  });
  finish(kYoyo);

  // r89: the home corner's rug: a flat disc a unit across, its edge rounded down to the floor.
  start(kRug);
  b.lathe_poly(1, n(48, 24), 0.0f, {{0.0f, 0.0f}, {0.5f, 0.0f}, {0.5f, 0.008f}, {0.485f, 0.016f}, {0.0f, 0.016f}});
  finish(kRug);

  // ---- r124 (Lance: "the celebration confetti and shit you have falling with those characters just needs a rehaul.
  // you have just random white balls falling with spider man"): the Marvel heroes' own pieces ----
  // A thin bar from a to b (w wide, t thick), its faces lit by their own normals.
  auto bar = [&](float mat, V3 a, V3 c, float w, float t) {
    const V3 d = c - a;
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (len < 1e-5f) return;
    const V3 x = d * (1.0f / len);
    const V3 up = std::fabs(x.z) < 0.9f ? V3{0.0f, 0.0f, 1.0f} : V3{0.0f, 1.0f, 0.0f};
    const V3 y = normalize(cross(up, x));
    const V3 z = cross(x, y);
    const M3 r{{x.x, y.x, z.x, x.y, y.y, z.y, x.z, y.z, z.z}};
    const size_t from = set.verts.size();
    b.box({0.0f, 0.0f, 0.0f}, {0.5f * len, 0.5f * w, 0.5f * t}, mat);
    b.xform(from, r, (a + c) * 0.5f);
  };
  // A flat outline (x-y, counter-clockwise, every point seen from (cx, cy)) given depth z0..z1.
  auto prism = [&](float mat, const std::vector<RH>& pts, float z0, float z1, float cx, float cy) {
    const uint32_t cnt = static_cast<uint32_t>(pts.size());
    for (float z : {z0, z1}) {
      const float nz = z == z1 ? 1.0f : -1.0f;
      const uint32_t base = static_cast<uint32_t>(set.verts.size());
      set.verts.push_back(Vertex{{cx, cy, z}, {0, 0, 0}, {0, 0, nz}, mat, 0.0f});
      for (const RH& p : pts) set.verts.push_back(Vertex{{p.first, p.second, z}, {0, 0, 0}, {0, 0, nz}, mat, 0.0f});
      for (uint32_t i = 0; i < cnt; ++i) {
        set.indices.push_back(base);
        set.indices.push_back(base + 1 + i);
        set.indices.push_back(base + 1 + (i + 1) % cnt);
      }
    }
    for (uint32_t i = 0; i < cnt; ++i) {
      const RH a = pts[i], c = pts[(i + 1) % cnt];
      const V3 nn = normalize(V3{c.second - a.second, -(c.first - a.first), 0.0f});
      const uint32_t base = static_cast<uint32_t>(set.verts.size());
      for (const RH& q : {a, c})
        for (float z : {z0, z1}) set.verts.push_back(Vertex{{q.first, q.second, z}, {0, 0, 0}, {nn.x, nn.y, nn.z}, mat, 0.0f});
      for (uint32_t k : {0u, 2u, 1u, 1u, 2u, 3u}) set.indices.push_back(base + k);
    }
  };

  // A spider web facing +z, a unit across: eight spokes, four rings sagging between them (main colour).
  start(kWeb);
  tagged(0.0f, [&] {
    float ang[8];
    for (int k = 0; k < 8; ++k) ang[k] = 2.0f * kPi * k / 8.0f + 0.12f * std::sin(5.0f * k);
    for (int k = 0; k < 8; ++k)
      bar(0.0f, V3{0.0f, 0.0f, 0.0f}, V3{0.5f * std::cos(ang[k]), 0.5f * std::sin(ang[k]), 0.0f}, 0.024f, 0.012f);
    for (float r : {0.11f, 0.21f, 0.31f, 0.41f}) {
      for (int k = 0; k < 8; ++k) {
        const float a0 = ang[k], a1 = ang[(k + 1) % 8] + (k == 7 ? 2.0f * kPi : 0.0f);
        const V3 p0{r * std::cos(a0), r * std::sin(a0), 0.0f}, p1{r * std::cos(a1), r * std::sin(a1), 0.0f};
        const float am = 0.5f * (a0 + a1), rm = 0.9f * r * std::cos(0.5f * (a1 - a0));
        const V3 pm{rm * std::cos(am), rm * std::sin(am), 0.0f};
        bar(0.0f, p0, pm, 0.017f, 0.01f);
        bar(0.0f, pm, p1, 0.017f, 0.01f);
      }
    }
  });
  finish(kWeb);

  // A spider, back up (+z), head along +y: body and head (main colour), a mark on its back (detail), eight legs.
  start(kSpider);
  placed(0.0f, rot_y(0.0f), V3{0.0f, -0.12f, 0.0f}, [&] { b.ellipsoid(n(16, 8), n(8, 4), 0.0f, 0.15f, 0.19f, 0.12f); });
  placed(0.0f, rot_y(0.0f), V3{0.0f, 0.1f, 0.0f}, [&] { b.ellipsoid(n(12, 6), n(6, 3), 0.0f, 0.09f, 0.09f, 0.07f); });
  placed(0.0f, rot_y(0.0f), V3{0.0f, -0.12f, 0.1f}, [&] { b.ellipsoid(n(10, 5), n(5, 3), 1.0f, 0.05f, 0.08f, 0.03f); });
  tagged(0.0f, [&] {
    for (float sx : {-1.0f, 1.0f}) {
      for (int k = 0; k < 4; ++k) {
        const float y = 0.13f - 0.08f * k, dy = 0.12f - 0.08f * k;
        const V3 hip{0.06f * sx, y, 0.0f}, knee{0.26f * sx, y + dy, 0.13f}, foot{0.42f * sx, y + 1.6f * dy, -0.12f};
        bar(0.0f, hip, knee, 0.025f, 0.025f);
        bar(0.0f, knee, foot, 0.02f, 0.02f);
      }
    }
  });
  finish(kSpider);

  // Wolverine's claws along +y: three blades (detail colour, metal) out of a knuckle bar (main colour).
  start(kClaws);
  for (float x : {-0.11f, 0.0f, 0.11f}) {
    placed(1.0f, rot_y(0.0f), V3{x, 0.0f, 0.0f}, [&] {
      prism(1.0f, {{-0.03f, -0.15f}, {0.03f, -0.15f}, {0.026f, 0.38f}, {0.0f, 0.52f}, {-0.016f, 0.42f}}, -0.012f, 0.012f, 0.0f,
            0.1f);
    });
  }
  tagged(0.0f, [&] { rbox(0.0f, 0.19f, 0.05f, 0.06f, 0.3f, V3{0.0f, -0.2f, 0.0f}); });
  finish(kClaws);

  // Captain America's shield facing +z: red (main), white (cream), red rings, a blue (detail) middle, a white star.
  start(kStarShield);
  tagged(1.0f, [&] { b.lathe_poly(2, n(48, 24), 0.0f, {{0.0f, -0.03f}, {0.5f, -0.03f}, {0.5f, 0.0f}}); });
  tagged(0.0f, [&] {
    b.lathe_poly(2, n(48, 24), 0.0f, {{0.5f, 0.0f}, {0.4f, 0.0f}, {0.4f, 0.012f}});
    b.lathe_poly(2, n(48, 24), 2.0f, {{0.4f, 0.012f}, {0.3f, 0.012f}, {0.3f, 0.024f}});
    b.lathe_poly(2, n(48, 24), 0.0f, {{0.3f, 0.024f}, {0.2f, 0.024f}, {0.2f, 0.036f}});
    b.lathe_poly(2, n(32, 16), 1.0f, {{0.2f, 0.036f}, {0.0f, 0.036f}});
  });
  tagged(0.0f, [&] {
    std::vector<RH> st;
    for (int k = 0; k < 10; ++k) {
      const float a = 0.5f * kPi + kPi * k / 5.0f, r = k % 2 == 0 ? 0.18f : 0.072f;
      st.push_back({r * std::cos(a), r * std::sin(a)});
    }
    prism(2.0f, st, 0.036f, 0.05f, 0.0f, 0.0f);
  });
  finish(kStarShield);

  // A skull facing +z (cream), dark eye sockets and nose, a row of teeth.
  start(kSkull);
  placed(0.0f, rot_y(0.0f), V3{0.0f, 0.08f, 0.0f}, [&] { b.ellipsoid(n(18, 8), n(10, 5), 2.0f, 0.26f, 0.29f, 0.27f); });
  placed(0.0f, rot_y(0.0f), V3{0.0f, -0.14f, 0.07f}, [&] { b.ellipsoid(n(14, 6), n(8, 4), 2.0f, 0.17f, 0.12f, 0.16f); });
  for (float sx : {-1.0f, 1.0f})
    placed(0.0f, rot_y(0.0f), V3{0.1f * sx, 0.03f, 0.215f}, [&] { b.ellipsoid(n(10, 5), n(6, 3), 3.0f, 0.075f, 0.07f, 0.04f); });
  placed(0.0f, rot_y(0.0f), V3{0.0f, -0.07f, 0.225f}, [&] { b.ellipsoid(n(8, 4), n(5, 3), 3.0f, 0.03f, 0.045f, 0.02f); });
  tagged(0.0f, [&] {
    for (int k = 0; k < 6; ++k) b.box({-0.075f + 0.03f * k, -0.2f, 0.2f}, {0.011f, 0.028f, 0.02f}, 2.0f);
  });
  finish(kSkull);

  // A bat, wings spread in x-y (main colour), red eyes (detail).
  start(kVampBat);
  placed(0.0f, rot_y(0.0f), V3{0.0f, 0.0f, 0.0f}, [&] { b.ellipsoid(n(12, 6), n(6, 3), 0.0f, 0.07f, 0.14f, 0.07f); });
  placed(0.0f, rot_y(0.0f), V3{0.0f, 0.16f, 0.0f}, [&] { b.ellipsoid(n(10, 5), n(5, 3), 0.0f, 0.06f, 0.06f, 0.06f); });
  for (float sx : {-1.0f, 1.0f}) {
    placed(0.0f, rot_y(0.0f), V3{0.0f, 0.0f, 0.0f}, [&] {
      std::vector<RH> w = {{0.05f, -0.06f}, {0.1f, -0.11f}, {0.19f, -0.01f}, {0.26f, -0.09f}, {0.32f, 0.02f},
                           {0.42f, -0.03f}, {0.48f, 0.1f},  {0.3f, 0.17f},  {0.05f, 0.09f}};
      if (sx < 0) {
        for (RH& p : w) p.first = -p.first;
        std::reverse(w.begin(), w.end());
      }
      prism(0.0f, w, -0.008f, 0.008f, 0.29f * sx, 0.16f);  // fanned from near the wrist: every edge point seen
    });
    placed(0.0f, rot_y(0.0f), V3{0.025f * sx, 0.18f, 0.05f}, [&] { b.ellipsoid(n(6, 4), n(4, 2), 1.0f, 0.014f, 0.014f, 0.01f); });
    placed(0.0f, rot_y(0.0f), V3{0.0f, 0.0f, 0.0f}, [&] {
      prism(0.0f, sx > 0 ? std::vector<RH>{{0.02f, 0.2f}, {0.06f, 0.2f}, {0.05f, 0.27f}}
                         : std::vector<RH>{{-0.06f, 0.2f}, {-0.02f, 0.2f}, {-0.05f, 0.27f}},
            -0.01f, 0.01f, 0.04f * sx, 0.22f);
    });
  }
  finish(kVampBat);

  // A sai along +y: the middle prong and the two side prongs (detail colour, metal), a wrapped grip (main colour).
  start(kSai);
  tagged(1.0f, [&] {
    b.lathe_poly(1, 4, 1.0f, {{0.0f, -0.06f}, {0.026f, 0.0f}, {0.02f, 0.46f}, {0.0f, 0.55f}});
    b.box({0.0f, -0.07f, 0.0f}, {0.14f, 0.02f, 0.022f}, 1.0f);
    for (float sx : {-1.0f, 1.0f}) {
      bar(1.0f, V3{0.13f * sx, -0.07f, 0.0f}, V3{0.17f * sx, 0.05f, 0.0f}, 0.03f, 0.03f);
      bar(1.0f, V3{0.17f * sx, 0.05f, 0.0f}, V3{0.15f * sx, 0.19f, 0.0f}, 0.026f, 0.026f);
    }
  });
  tagged(0.0f, [&] { b.lathe(1, n(10, 6), 1, 0.0f, [](float, float v) { return RH{0.034f, -0.45f + 0.37f * v}; }, none); });
  placed(1.0f, rot_y(0.0f), V3{0.0f, -0.48f, 0.0f}, [&] { b.ellipsoid(n(8, 4), n(5, 3), 1.0f, 0.045f, 0.035f, 0.045f); });
  finish(kSai);

  // A snowflake facing +z: six arms with two pairs of branches (main colour, ice).
  start(kSnowflake);
  tagged(3.0f, [&] {
    for (int k = 0; k < 6; ++k) {
      const float a = 2.0f * kPi * k / 6.0f;
      const V3 d{std::cos(a), std::sin(a), 0.0f};
      bar(0.0f, V3{0.0f, 0.0f, 0.0f}, d * 0.48f, 0.04f, 0.022f);
      for (float at : {0.2f, 0.33f}) {
        const float bl = at < 0.25f ? 0.14f : 0.1f;
        for (float sgn : {-1.0f, 1.0f}) {
          const float ba = a + sgn * kPi / 3.0f;
          bar(0.0f, d * at, d * at + V3{std::cos(ba), std::sin(ba), 0.0f} * bl, 0.032f, 0.02f);
        }
      }
    }
  });
  finish(kSnowflake);

  // Two chain links through each other along x (detail colour, metal): one flat, one stood on its edge.
  start(kChainLink);
  for (int l = 0; l < 2; ++l) {
    placed(1.0f, l == 0 ? rot_y(0.0f) : rot_x(0.5f * kPi), V3{l == 0 ? -0.18f : 0.18f, 0.0f, 0.0f}, [&] {
      b.patch(n(10, 6), n(28, 14), 1.0f,
              [](float u, float v) {
                const float t = 2.0f * kPi * v, a = 2.0f * kPi * u;
                const V3 c{0.22f * std::cos(t), 0.13f * std::sin(t), 0.0f};
                const V3 out = normalize(V3{std::cos(t) / 0.22f, std::sin(t) / 0.13f, 0.0f});
                return c + out * (0.045f * std::cos(a)) + V3{0.0f, 0.0f, 0.045f * std::sin(a)};
              },
              none);
    });
  }
  finish(kChainLink);

  // A mystic rune facing +z: a ring and two crossed squares inside it (main colour, glowing).
  start(kRune);
  tagged(3.0f, [&] {
    const int seg = 24;
    for (int k = 0; k < seg; ++k) {
      const float a0 = 2.0f * kPi * k / seg, a1 = 2.0f * kPi * (k + 1) / seg;
      bar(0.0f, V3{0.48f * std::cos(a0), 0.48f * std::sin(a0), 0.0f}, V3{0.48f * std::cos(a1), 0.48f * std::sin(a1), 0.0f}, 0.03f, 0.015f);
    }
    for (float off : {0.0f, 0.25f * kPi}) {
      for (int k = 0; k < 4; ++k) {
        const float a0 = off + 0.5f * kPi * k + 0.25f * kPi, a1 = a0 + 0.5f * kPi;
        bar(0.0f, V3{0.4f * std::cos(a0), 0.4f * std::sin(a0), 0.0f}, V3{0.4f * std::cos(a1), 0.4f * std::sin(a1), 0.0f}, 0.022f, 0.012f);
      }
    }
  });
  finish(kRune);

  // r127: Mjolnir along +y: its square head (main colour, metal), a leather-wrapped handle (detail colour), a pommel.
  start(kMjolnir);
  tagged(1.0f, [&] { rbox(0.0f, 0.25f, 0.15f, 0.14f, 0.3f, V3{0.0f, 0.3f, 0.0f}); });
  tagged(4.0f, [&] { b.lathe(1, n(10, 6), 1, 1.0f, [](float, float v) { return RH{0.036f, -0.44f + 0.6f * v}; }, none); });
  placed(1.0f, rot_y(0.0f), V3{0.0f, -0.47f, 0.0f}, [&] { b.ellipsoid(n(10, 6), n(6, 3), 0.0f, 0.05f, 0.04f, 0.05f); });
  finish(kMjolnir);

  // r127: an arrow along +y: a shaft (detail colour), its head and three fletching fins (main colour).
  start(kArrow);
  tagged(4.0f, [&] { b.lathe(1, n(8, 5), 1, 1.0f, [](float, float v) { return RH{0.013f, -0.47f + 0.82f * v}; }, none); });
  tagged(1.0f, [&] { b.lathe_poly(1, 4, 0.0f, {{0.0f, 0.33f}, {0.05f, 0.37f}, {0.0f, 0.52f}}); });
  for (int k = 0; k < 3; ++k) {
    placed(0.0f, rot_y(2.0944f * k), V3{0.0f, 0.0f, 0.0f}, [&] {
      prism(0.0f, {{0.012f, -0.45f}, {0.065f, -0.48f}, {0.065f, -0.33f}, {0.012f, -0.29f}}, -0.004f, 0.004f, 0.038f, -0.39f);
    });
  }
  finish(kArrow);

  // r127: a spoon along +y (metal, main colour): a shallow bowl and a flat handle.
  start(kSpoon);
  placed(1.0f, rot_y(0.0f), V3{0.0f, 0.3f, 0.0f}, [&] { b.ellipsoid(n(14, 7), n(7, 4), 0.0f, 0.11f, 0.16f, 0.035f); });
  tagged(1.0f, [&] { bar(0.0f, V3{0.0f, 0.16f, 0.0f}, V3{0.0f, -0.46f, 0.0f}, 0.045f, 0.018f); });
  placed(1.0f, rot_y(0.0f), V3{0.0f, -0.46f, 0.0f}, [&] { b.ellipsoid(n(8, 4), n(5, 3), 0.0f, 0.04f, 0.04f, 0.012f); });
  finish(kSpoon);

  // ---- r130: the new roster's pieces. LIGHT: each under ~60 triangles near (the counts in the comments are at detail
  // 1; the far set has fewer where a count scales), flat outlines as prisms / bands, details painted by
  // xr_fx_mesh.frag (93-118) instead of modelled. PC preview: tools\outside\drops130_preview.py. ----
  // A band between two polylines in x-y (outer / inner, as many points each) given depth z0..z1: its two faces, its two
  // long sides (unless `sides` is false: a thin one) and its ends (left out where it comes to a point). 8 (points - 1)
  // + 4 triangles (4 (points - 1) + 4 without the sides).
  auto band = [&](float mat, const std::vector<RH>& outer, const std::vector<RH>& inner, float z0, float z1,
                  bool sides = true) {
    const size_t cnt = std::min(outer.size(), inner.size());
    if (cnt < 2) return;
    auto quad = [&](V3 q0, V3 q1, V3 q2, V3 q3, V3 nn) {
      const uint32_t base = static_cast<uint32_t>(set.verts.size());
      for (const V3& p : {q0, q1, q2, q3}) set.verts.push_back(Vertex{{p.x, p.y, p.z}, {0, 0, 0}, {nn.x, nn.y, nn.z}, mat, 0.0f});
      for (uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) set.indices.push_back(base + k);
    };
    auto at = [](const RH& p, float z) { return V3{p.first, p.second, z}; };
    for (size_t i = 0; i + 1 < cnt; ++i) {
      const RH o0 = outer[i], o1 = outer[i + 1], i0 = inner[i], i1 = inner[i + 1];
      quad(at(o0, z1), at(o1, z1), at(i1, z1), at(i0, z1), V3{0.0f, 0.0f, 1.0f});
      quad(at(o0, z0), at(o1, z0), at(i1, z0), at(i0, z0), V3{0.0f, 0.0f, -1.0f});
      if (!sides) continue;
      // the long sides, each facing away from the other
      const V3 away{o0.first - i0.first + o1.first - i1.first, o0.second - i0.second + o1.second - i1.second, 0.0f};
      V3 no = normalize(V3{o1.second - o0.second, -(o1.first - o0.first), 0.0f});
      if (no.x * away.x + no.y * away.y < 0.0f) no = no * -1.0f;
      quad(at(o0, z0), at(o1, z0), at(o1, z1), at(o0, z1), no);
      V3 ni = normalize(V3{i1.second - i0.second, -(i1.first - i0.first), 0.0f});
      if (ni.x * away.x + ni.y * away.y > 0.0f) ni = ni * -1.0f;
      quad(at(i0, z0), at(i1, z0), at(i1, z1), at(i0, z1), ni);
    }
    for (int e = 0; e < 2; ++e) {
      const size_t i = e == 0 ? 0 : cnt - 1, j = e == 0 ? 1 : cnt - 2;
      const RH o = outer[i], in = inner[i];
      if (std::fabs(o.first - in.first) + std::fabs(o.second - in.second) < 1e-4f) continue;  // a point
      V3 ne = normalize(V3{o.second - in.second, -(o.first - in.first), 0.0f});
      const V3 next{outer[j].first - o.first, outer[j].second - o.second, 0.0f};
      if (ne.x * next.x + ne.y * next.y > 0.0f) ne = ne * -1.0f;
      quad(at(o, z0), at(in, z0), at(in, z1), at(o, z1), ne);
    }
  };
  // A tube round a centre line in x-y (centre(v), its radius r(v)), nu round it, nv along it; normals out.
  auto tube = [&](int nu, int nv, float mat, auto centre, auto radius) {
    b.patch(nu, nv, mat,
            [=](float u, float v) {
              const float e = 1e-3f;
              const V3 c = centre(v);
              const V3 tg = normalize(centre(std::min(v + e, 1.0f)) - centre(std::max(v - e, 0.0f)));
              const V3 nm{tg.y, -tg.x, 0.0f}, bn{0.0f, 0.0f, 1.0f};  // nm x bn = -tg: the normals then point out
              const float r = radius(v), t = 2.0f * kPi * u;
              return c + nm * (r * std::cos(t)) + bn * (r * std::sin(t));
            },
            none);
  };

  // A crescent moon facing +z, its horns to the right (main colour; metal, glowing when the piece glows). 48 tris.
  start(kCrescent);
  tagged(1.0f, [&] {
    const int cnt = n(7, 5);
    std::vector<RH> out, in;
    for (int k = 0; k < cnt; ++k) {
      const float t = static_cast<float>(k) / static_cast<float>(cnt - 1);
      const float a = 1.22f + 3.84f * t;  // 70 .. 290 degrees, round the left
      const float r = 0.48f - 0.24f * std::sin(kPi * t);
      out.push_back({0.16f + 0.48f * std::cos(a), 0.48f * std::sin(a)});
      in.push_back({0.16f + r * std::cos(a), r * std::sin(a)});
    }
    band(0.0f, out, in, -0.05f, 0.05f);
  });
  finish(kCrescent);

  // A round badge facing +z (main colour; the shader paints a ring and a "4" on both faces in the detail colour). 48 tris.
  start(kBadge4);
  tagged(0.0f, [&] {
    std::vector<RH> pts;
    const int cnt = n(12, 8);
    for (int k = 0; k < cnt; ++k) {
      const float a = 2.0f * kPi * k / cnt;
      pts.push_back({0.45f * std::cos(a), 0.45f * std::sin(a)});
    }
    prism(0.0f, pts, -0.04f, 0.04f, 0.0f, 0.0f);
  });
  finish(kBadge4);

  // Mr. Fantastic's arm stretching in an S up +y (main colour), his fist at its end (detail colour). 60 tris.
  start(kStretchArm);
  tagged(0.0f, [&] {
    tube(4, n(6, 4), 0.0f, [](float v) { return V3{0.16f * std::sin(1.7f * kPi * v), -0.5f + 0.78f * v, 0.0f}; },
         [](float v) { return 0.06f * std::sqrt(std::min(1.0f, v * 6.0f)); });
  });
  placed(0.0f, rot_z(-0.57f), V3{-0.086f, 0.35f, 0.0f}, [&] { b.box({0.0f, 0.0f, 0.0f}, {0.085f, 0.09f, 0.075f}, 1.0f); });
  finish(kStretchArm);

  // The Silver Surfer's board along +y, flat, facing +z (main colour, metal). 40 tris.
  start(kSurfboard);
  tagged(1.0f, [&] {
    prism(0.0f, {{0.0f, 0.5f}, {-0.11f, 0.34f}, {-0.155f, 0.1f}, {-0.145f, -0.18f}, {-0.09f, -0.4f}, {0.0f, -0.49f},
                 {0.09f, -0.4f}, {0.145f, -0.18f}, {0.155f, 0.1f}, {0.11f, 0.34f}},
          -0.025f, 0.025f, 0.0f, 0.0f);
  });
  finish(kSurfboard);

  // An acorn along +y: the nut (main colour, glossy) under its cap (detail colour, rough). 56 tris.
  start(kAcorn);
  tagged(0.0f, [&] {
    b.lathe(1, n(7, 5), 2, 0.0f,
            [](float, float v) { return RH{0.27f * std::pow(std::sin(0.5f * kPi * v), 0.6f), -0.5f + 0.54f * v}; }, none);
  });
  tagged(2.0f, [&] { b.lathe_poly(1, n(7, 5), 1.0f, {{0.3f, 0.01f}, {0.29f, 0.14f}, {0.0f, 0.27f}}); });
  finish(kAcorn);

  // A horn along +y curving to +x (the shader shades it from the main colour at its base to the detail colour at its
  // tip). 60 tris.
  start(kHorn);
  tagged(0.0f, [&] {
    tube(n(6, 5), n(4, 3), 0.0f, [](float v) { return V3{0.2f * v * v, -0.5f + 0.95f * v, 0.0f}; },
         [](float v) { return 0.16f * (1.0f - v); });
    b.lathe_poly(1, n(6, 5), 0.0f, {{0.0f, -0.5f}, {0.16f, -0.5f}});
  });
  finish(kHorn);

  // A crown, upright: a five-pointed band (main colour, metal) and a band of jewels round it (detail colour, gem). 40 tris.
  start(kCrown);
  tagged(1.0f, [&] {
    b.patch(10, 1, 0.0f,
            [](float u, float v) {
              const float top = 0.08f + 0.27f * (0.5f + 0.5f * std::cos(10.0f * kPi * u));  // points at every 5th
              return revolve(1, 0.42f, -0.25f + (top + 0.25f) * v, 2.0f * kPi * u);
            },
            none);
  });
  tagged(3.0f, [&] { b.lathe(1, 10, 1, 1.0f, [](float, float v) { return RH{0.435f, -0.17f + 0.1f * v}; }, none); });
  finish(kCrown);

  // A fossil / spiral shell: a lens facing +z (main colour, rough stone; the shader carves the spiral and its ribs on
  // both faces). 40 tris.
  start(kFossil);
  tagged(2.0f, [&] {
    b.lathe(2, n(10, 7), 2, 0.0f, [](float, float v) { return RH{0.48f * std::sin(kPi * v), -0.09f * std::cos(kPi * v)}; },
            none);
  });
  finish(kFossil);

  // A talon / scythe along +y, curving to the right, facing +z (main colour at its base to the detail colour at its
  // point: the shader). 50 tris.
  start(kTalon);
  tagged(0.0f, [&] {
    const int cnt = n(7, 5);
    std::vector<RH> out, in;
    for (int k = 0; k < cnt; ++k) {
      const float t = static_cast<float>(k) / static_cast<float>(cnt - 1);
      const float a = kPi - 0.80f * t;  // on a circle about (1.15, -0.45), radius 1.3
      const float w = 0.12f * std::pow(1.0f - t, 0.9f);
      const float c = std::cos(a), s = std::sin(a);
      out.push_back({1.15f + (1.3f + w) * c, -0.45f + (1.3f + w) * s});
      in.push_back({1.15f + (1.3f - w) * c, -0.45f + (1.3f - w) * s});
    }
    band(0.0f, out, in, -0.035f, 0.035f);
  });
  finish(kTalon);

  // A spike / needle along +y (main colour at its base to the detail colour at its point). 24 tris.
  start(kSpike);
  tagged(0.0f, [&] { b.lathe_poly(1, n(6, 5), 0.0f, {{0.0f, -0.5f}, {0.11f, -0.5f}, {0.0f, 0.5f}}); });
  finish(kSpike);

  // A toadstool, upright: its cap (main colour, the shader's spots in the detail colour), cream gills and stem. 56 tris.
  start(kToadstool);
  tagged(0.0f, [&] {
    b.lathe(1, n(7, 6), 2, 0.0f,
            [](float, float v) { const float phi = 0.5f * kPi * v; return RH{0.46f * std::cos(phi), -0.04f + 0.34f * std::sin(phi)}; },
            none);
    b.lathe_poly(1, n(7, 6), 2.0f, {{0.13f, -0.1f}, {0.46f, -0.04f}});
    b.lathe_poly(1, n(7, 6), 2.0f, {{0.16f, -0.5f}, {0.13f, -0.1f}});
  });
  finish(kToadstool);

  // A lily pad facing +z, its notch to +x (main colour; the shader's veins in the detail colour). 44 tris.
  start(kLilyPad);
  tagged(0.0f, [&] {
    std::vector<RH> pts;
    const int cnt = n(10, 7);
    for (int k = 0; k < cnt; ++k) {
      const float a = 0.44f + (2.0f * kPi - 0.88f) * k / (cnt - 1);
      pts.push_back({0.48f * std::cos(a), 0.48f * std::sin(a)});
    }
    pts.push_back({0.04f, 0.0f});
    prism(0.0f, pts, -0.015f, 0.015f, -0.12f, 0.0f);
  });
  finish(kLilyPad);

  // A wool puff: three soft lumps (main colour; matte, fuzzy: the shader). 62 tris.
  start(kWool);
  tagged(0.0f, [&] { b.ellipsoid(n(5, 4), 3, 0.0f, 0.27f, 0.25f, 0.24f); });
  for (float sx : {-1.0f, 1.0f}) {
    placed(0.0f, rot_y(0.0f), V3{0.25f * sx, -0.06f, 0.02f}, [&] { b.ellipsoid(4, 2, 0.0f, 0.2f, 0.19f, 0.19f); });
  }
  finish(kWool);

  // A leaf along +y, cupped and bent (main colour; the shader's midrib in the detail colour). 48 tris.
  start(kLeaf);
  tagged(0.0f, [&] {
    b.puffy(n(8, 6), 3, 0.0f, 0.0f, 0.0f, 0.03f, 0.5f,
            [](float t) {
              const float c = std::cos(t);
              return RH{0.21f * std::sin(t) * (1.0f - 0.35f * c * c), 0.48f * c};
            },
            [](float x, float y) { return 0.3f * x * x + 0.12f * y * y; });
  });
  finish(kLeaf);

  // Sound waves facing +z: three thin arcs ")))" (main colour, shining). 48 tris.
  start(kSoundWave);
  tagged(3.0f, [&] {
    for (float r : {0.16f, 0.32f, 0.48f}) {
      std::vector<RH> out, in;
      for (int k = 0; k < 4; ++k) {
        const float a = -0.85f + 0.5667f * k;
        out.push_back({-0.3f + (r + 0.035f) * std::cos(a), (r + 0.035f) * std::sin(a)});
        in.push_back({-0.3f + (r - 0.035f) * std::cos(a), (r - 0.035f) * std::sin(a)});
      }
      band(0.0f, out, in, -0.012f, 0.012f, false);
    }
  });
  finish(kSoundWave);

  // A boxing glove, light (main colour; cream cuff). 60 tris.
  start(kGloveLite);
  placed(0.0f, rot_y(0.0f), V3{0.0f, 0.08f, 0.0f}, [&] { b.ellipsoid(n(6, 5), 3, 0.0f, 0.3f, 0.36f, 0.27f); });
  placed(0.0f, rot_z(0.5f), V3{0.24f, -0.02f, 0.1f}, [&] { b.ellipsoid(3, 2, 0.0f, 0.1f, 0.18f, 0.1f); });
  tagged(0.0f, [&] { b.box({0.0f, -0.36f, 0.0f}, {0.2f, 0.11f, 0.17f}, 2.0f); });
  finish(kGloveLite);

  // An impact burst facing +z: seven uneven spikes (the main colour in the middle, the detail colour out at the spikes:
  // the shader). 56 tris.
  start(kImpact);
  tagged(0.0f, [&] {
    const float tip[7] = {0.5f, 0.42f, 0.48f, 0.4f, 0.5f, 0.44f, 0.46f};
    std::vector<RH> pts;
    for (int k = 0; k < 14; ++k) {
      const float a = 0.5f * kPi + 2.0f * kPi * k / 14.0f + (k % 2 == 0 ? 0.05f * std::sin(3.0f * k) : 0.0f);
      const float r = k % 2 == 0 ? tip[k / 2] : 0.22f;
      pts.push_back({r * std::cos(a), r * std::sin(a)});
    }
    prism(0.0f, pts, -0.03f, 0.03f, 0.0f, 0.0f);
  });
  finish(kImpact);

  // A spinning top along +y (its body striped in the main and detail colours by the shader; a dark handle). 58 tris.
  start(kTop);
  tagged(0.0f, [&] { b.lathe_poly(1, n(7, 5), 0.0f, {{0.0f, -0.5f}, {0.36f, -0.04f}, {0.32f, 0.1f}, {0.08f, 0.2f}}); });
  tagged(4.0f, [&] { b.lathe_poly(1, 4, 3.0f, {{0.08f, 0.2f}, {0.06f, 0.44f}, {0.0f, 0.46f}}); });
  finish(kTop);

  // An open palm facing +z, fingers up +y, thumb to the left (main colour). 60 tris.
  start(kPalm);
  tagged(0.0f, [&] {
    // round the palm's middle (0, -0.12): fingers (centre angle, length) with valleys between, the thumb, the wrist
    auto pol = [](float deg, float r) {
      const float a = deg * kPi / 180.0f;
      return RH{r * std::cos(a), -0.12f + r * std::sin(a)};
    };
    std::vector<RH> pts;
    const float fing[4][2] = {{50.0f, 0.44f}, {70.0f, 0.53f}, {90.0f, 0.57f}, {110.0f, 0.52f}};
    for (int f = 0; f < 4; ++f) {
      pts.push_back(pol(fing[f][0] - 6.0f, fing[f][1]));
      pts.push_back(pol(fing[f][0] + 6.0f, fing[f][1]));
      pts.push_back(pol(fing[f][0] + (f < 3 ? 10.0f : 15.0f), f < 3 ? 0.3f : 0.27f));
    }
    pts.push_back(pol(150.0f, 0.42f));  // the thumb
    pts.push_back(pol(235.0f, 0.42f));  // the wrist
    pts.push_back(pol(305.0f, 0.42f));
    prism(0.0f, pts, -0.03f, 0.03f, 0.0f, -0.12f);
  });
  finish(kPalm);

  // A twig along +y with a forked branch (main colour, wood) and a leaf on it (detail colour). 40 tris.
  start(kTwig);
  tagged(4.0f, [&] {
    bar(0.0f, V3{0.0f, -0.48f, 0.0f}, V3{0.03f, 0.46f, 0.0f}, 0.06f, 0.06f);
    bar(0.0f, V3{0.015f, -0.02f, 0.0f}, V3{0.2f, 0.25f, 0.0f}, 0.045f, 0.045f);
  });
  tagged(0.0f, [&] { prism(1.0f, {{0.2f, 0.25f}, {0.33f, 0.3f}, {0.38f, 0.44f}, {0.24f, 0.36f}}, -0.01f, 0.01f, 0.2875f, 0.3375f); });
  finish(kTwig);

  // A cut gem along +y: a point below, a crown of facets above (main colour, gem). 36 tris.
  start(kGem);
  tagged(3.0f, [&] { b.lathe_poly(1, 6, 0.0f, {{0.0f, -0.42f}, {0.34f, 0.12f}, {0.26f, 0.3f}, {0.0f, 0.34f}}); });
  finish(kGem);

  // A lotus, upright: a bowl of six pointed petals (the detail colour at its heart to the main colour at the tips: the
  // shader; lit on the side you see). 48 tris.
  start(kLotus);
  tagged(0.0f, [&] {
    b.lathe(1, 12, 2, 0.0f,
            [](float u, float v) {
              const float lobe = std::fabs(std::cos(6.0f * kPi * u));
              return RH{v * (0.3f + 0.18f * lobe), -0.16f + 0.08f * v + (0.08f + 0.26f * lobe) * v * v};
            },
            none);
  });
  finish(kLotus);

  // A pair of lips facing +z (main colour, glossy; the shader draws the line between them in the detail colour). 56 tris.
  start(kLips);
  tagged(0.0f, [&] {
    prism(0.0f, {{0.48f, 0.0f}, {0.36f, 0.1f}, {0.2f, 0.2f}, {0.08f, 0.19f}, {0.0f, 0.13f}, {-0.08f, 0.19f}, {-0.2f, 0.2f},
                 {-0.36f, 0.1f}, {-0.48f, 0.0f}, {-0.34f, -0.14f}, {-0.16f, -0.22f}, {0.0f, -0.24f}, {0.16f, -0.22f},
                 {0.34f, -0.14f}},
          -0.07f, 0.07f, 0.0f, 0.0f);
  });
  finish(kLips);

  // A plump heart, light (main colour, glossy). 60 tris.
  start(kHeartLite);
  tagged(0.0f, [&] {
    b.puffy(10, 3, 0.0f, 0.0f, 0.02f, 0.17f, 0.6f,
            [](float t) {
              const float s = std::sin(t);
              return RH{16.0f * s * s * s / 34.0f,
                        (13.0f * std::cos(t) - 5.0f * std::cos(2.0f * t) - 2.0f * std::cos(3.0f * t) - std::cos(4.0f * t)) /
                                34.0f + 0.07f};
            },
            flat);
  });
  finish(kHeartLite);

  // A plump five-pointed star, light (main colour, glossy). 60 tris.
  start(kStarLite);
  tagged(0.0f, [&] {
    b.puffy(10, 3, 0.0f, 0.0f, 0.0f, 0.15f, 0.55f,
            [](float t) {
              const float r = 0.21f + 0.29f * (0.5f + 0.5f * std::cos(5.0f * t));
              return RH{r * std::sin(t), r * std::cos(t)};
            },
            flat);
  });
  finish(kStarLite);

  // A dog tag facing +z (main colour, metal; the shader punches its hole and stamps its lines). 32 tris.
  start(kDogTag);
  tagged(1.0f, [&] {
    prism(0.0f, {{0.19f, -0.24f}, {0.19f, 0.24f}, {0.13f, 0.3f}, {-0.13f, 0.3f}, {-0.19f, 0.24f}, {-0.19f, -0.24f},
                 {-0.13f, -0.3f}, {0.13f, -0.3f}},
          -0.012f, 0.012f, 0.0f, 0.0f);
  });
  finish(kDogTag);

  return set;
}

}  // namespace xrfx::mesh
