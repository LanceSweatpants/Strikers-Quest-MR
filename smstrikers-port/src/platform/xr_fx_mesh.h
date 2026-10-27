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

  return set;
}

}  // namespace xrfx::mesh
