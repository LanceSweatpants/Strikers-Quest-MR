// strikers-quest r168: the BALL SKINS' living surfaces - one procedural shader, written once here (plain C++, no
// dependencies) and used by the game-side bake (ball_skin.cpp: the looping texture frames the game's own ball wears)
// and by the shop (xr_balls.cpp: its tiles). xr_fx.frag shapes 160 / 161 (the shop's preview ball) and
// tools/outside/balls168/shade168.py (the PC proofs) are line-for-line twins of it: change one, change all four.
// Our own art: value noise from an integer hash, nothing of the game's.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ballshade {

constexpr float kPi = 3.14159265358979f;
// FIREBALL: its churning surface loops every kFireLoop seconds (kFireFrames baked frames)
constexpr float kFireLoop = 2.0f;
constexpr int kFireFrames = 32;
// PLANET EARTH: the clouds go once round the globe (relative to the land) every kEarthLoop seconds (kEarthFrames
// baked); the land's own spin is the drawn ball's turn (kEarthSpin seconds a turn: DrawableBall.cpp)
constexpr float kEarthLoop = 12.0f;
constexpr int kEarthFrames = 96;
constexpr float kEarthSpin = 16.0f;

struct V {
  float x, y, z;
};
inline V v(float x, float y, float z) { return V{x, y, z}; }
inline V operator+(V a, V b) { return V{a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V operator-(V a, V b) { return V{a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V operator*(V a, float s) { return V{a.x * s, a.y * s, a.z * s}; }
inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline V mixv(V a, V b, float t) { return V{mixf(a.x, b.x, t), mixf(a.y, b.y, t), mixf(a.z, b.z, t)}; }
inline float smooth(float e0, float e1, float x) {
  const float t = clamp01((x - e0) / (e1 - e0));
  return t * t * (3.0f - 2.0f * t);
}

// an integer lattice point's value 0..1
inline float hash3(int32_t x, int32_t y, int32_t z) {
  uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u ^ static_cast<uint32_t>(y) * 0xd8163841u ^ static_cast<uint32_t>(z) * 0xcb1ab31fu;
  h ^= h >> 15;
  h *= 0x2c1b3c6du;
  h ^= h >> 12;
  h *= 0x297a2d39u;
  h ^= h >> 15;
  return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}
// smooth value noise 0..1
inline float vnoise(V p) {
  const float fx = std::floor(p.x), fy = std::floor(p.y), fz = std::floor(p.z);
  const int32_t ix = static_cast<int32_t>(fx), iy = static_cast<int32_t>(fy), iz = static_cast<int32_t>(fz);
  const float tx = p.x - fx, ty = p.y - fy, tz = p.z - fz;
  const float ux = tx * tx * (3.0f - 2.0f * tx), uy = ty * ty * (3.0f - 2.0f * ty), uz = tz * tz * (3.0f - 2.0f * tz);
  const float a = mixf(hash3(ix, iy, iz), hash3(ix + 1, iy, iz), ux);
  const float b = mixf(hash3(ix, iy + 1, iz), hash3(ix + 1, iy + 1, iz), ux);
  const float c = mixf(hash3(ix, iy, iz + 1), hash3(ix + 1, iy, iz + 1), ux);
  const float d = mixf(hash3(ix, iy + 1, iz + 1), hash3(ix + 1, iy + 1, iz + 1), ux);
  return mixf(mixf(a, b, uy), mixf(c, d, uy), uz);
}
inline float fbm(V p, int oct) {
  float sum = 0.0f, amp = 0.5f, norm = 0.0f;
  for (int k = 0; k < oct; ++k) {
    sum += amp * vnoise(p);
    norm += amp;
    p = p * 2.03f + v(17.1f, 9.2f, 4.7f);
    amp *= 0.5f;
  }
  return sum / norm;
}

// FIREBALL at the unit direction n (the ball's own space), its loop's phase ph = 2 pi t / kFireLoop: churning flame -
// three warp fields each running round its own circle in noise space (so it loops), the heat through a fire ramp, the
// inner light flickering on whole multiples of the loop. sRGB 0..1.
inline V fire(V n, float ph) {
  const float c = std::cos(ph), s = std::sin(ph);
  const V w = v(c, s, c * 0.6f - s * 0.8f) * 0.85f;
  const V q = n * 2.1f;
  const float wx = fbm(q * 0.8f + w + v(11.3f, 0.0f, 0.0f), 3);
  const float wy = fbm(q * 0.8f - v(w.y, w.z, w.x) + v(0.0f, 3.1f, 0.0f), 3);
  const float wz = fbm(q * 0.8f + v(w.z, w.x, w.y) + v(0.0f, 0.0f, 7.7f), 3);
  const float h = fbm(q + v(wx, wy, wz) * 1.7f + w * 0.5f, 4);
  // r169 (Lance: "looks fake"): real flame turbulence - thin white-hot filaments (ridged noise through the same warp)
  // over a molten core, cooler dark-red crust drifting between them
  const float rn = fbm(q * 1.6f + v(wx, wy, wz) * 2.4f - w * 0.7f + v(2.3f, 5.1f, 1.9f), 4);
  const float rid = 1.0f - std::fabs(2.0f * rn - 1.0f);
  const float fil = rid * rid * rid * rid;
  const float heat = clamp01((h - 0.30f) * 2.3f * 0.8f + fil * 0.55f - 0.05f);
  V col;
  if (heat < 0.35f) col = mixv(v(0.22f, 0.01f, 0.0f), v(0.80f, 0.07f, 0.0f), heat / 0.35f);
  else if (heat < 0.62f) col = mixv(v(0.80f, 0.07f, 0.0f), v(1.0f, 0.34f, 0.02f), (heat - 0.35f) / 0.27f);
  else if (heat < 0.85f) col = mixv(v(1.0f, 0.34f, 0.02f), v(1.0f, 0.68f, 0.14f), (heat - 0.62f) / 0.23f);
  else col = mixv(v(1.0f, 0.68f, 0.14f), v(1.0f, 0.94f, 0.70f), (heat - 0.85f) / 0.15f);
  const float flick = 1.0f + 0.09f * std::sin(3.0f * ph) + 0.06f * std::sin(7.0f * ph + 1.3f);
  return v(clamp01(col.x * flick), clamp01(col.y * flick), clamp01(col.z * flick));
}

// PLANET EARTH at n (Z the poles): oceans deep to shallow, continents (forest, grass, desert, mountains), ice caps,
// city lights twinkling (their twinkle `blink`: a whole number, one per baked frame), clouds turned `cloudAng` about
// the pole. *cityOut (may be null): how much city light is in it (0..1), *cloudOut: cloud cover (0..1). sRGB 0..1.
inline V earth(V n, float cloudAng, int blink, float* cityOut, float* cloudOut) {
  const float e = fbm(n * 1.55f + v(5.2f, 1.3f, -2.7f), 5);
  const float land = smooth(0.515f, 0.535f, e);
  const float lat = std::fabs(n.z);
  const float veg = fbm(n * 5.0f + v(3.0f, 3.0f, 3.0f), 3);
  const float dry = smooth(0.52f, 0.70f, veg) * smooth(0.05f, 0.25f, lat) * (1.0f - smooth(0.45f, 0.60f, lat));
  V landC = mixv(mixv(v(0.09f, 0.30f, 0.08f), v(0.24f, 0.40f, 0.12f), veg), v(0.66f, 0.55f, 0.32f), dry);
  landC = mixv(landC, v(0.45f, 0.40f, 0.33f), 0.6f * smooth(0.60f, 0.70f, e));
  const V ocean = mixv(v(0.01f, 0.05f, 0.20f), v(0.04f, 0.26f, 0.55f), smooth(0.38f, 0.515f, e));
  V col = mixv(ocean, landC, land);
  const float ice = smooth(0.80f, 0.86f, lat + 0.12f * (fbm(n * 4.0f, 2) - 0.5f));
  col = mixv(col, v(0.92f, 0.95f, 1.0f), ice);
  // cities: one in some of a lattice's cells, on land off the ice
  const V g = n * 26.0f;
  const float cx = std::floor(g.x), cy = std::floor(g.y), cz = std::floor(g.z);
  const int32_t ix = static_cast<int32_t>(cx), iy = static_cast<int32_t>(cy), iz = static_cast<int32_t>(cz);
  float city = 0.0f;
  if (hash3(ix, iy, iz) > 0.80f && land > 0.5f && ice < 0.5f) {
    const V ctr = v(cx + 0.5f + (hash3(iy, iz, ix) - 0.5f) * 0.6f, cy + 0.5f + (hash3(iz, ix, iy) - 0.5f) * 0.6f,
                    cz + 0.5f + (hash3(ix + 7, iy, iz) - 0.5f) * 0.6f);
    const V d = g - ctr;
    city = std::exp(-(d.x * d.x + d.y * d.y + d.z * d.z) * 18.0f) * (0.55f + 0.45f * hash3(ix, iy + blink * 131, iz));
  }
  col = col + v(1.0f, 0.78f, 0.38f) * (0.9f * city);
  // clouds, turned about the pole
  const float ca = std::cos(cloudAng), sa = std::sin(cloudAng);
  const V nr = v(n.x * ca - n.y * sa, n.x * sa + n.y * ca, n.z);
  const float cw = fbm(nr * 1.3f + v(9.0f, 9.0f, 9.0f), 2);
  const float cl = fbm(nr * 2.6f + v(cw, cw, cw) * 1.2f, 5);
  const float cloud = smooth(0.54f, 0.70f, cl) * 0.92f;
  col = mixv(col, v(0.97f, 0.98f, 1.0f), cloud);
  if (cityOut != nullptr) *cityOut = city * (1.0f - cloud);
  if (cloudOut != nullptr) *cloudOut = cloud;
  return v(clamp01(col.x), clamp01(col.y), clamp01(col.z));
}

// the direction of texel (i, j) of a w x h equirectangular picture: u round the pole (Z), v from the north pole down
inline V texel_dir(int i, int j, int w, int h) {
  const float lon = 2.0f * kPi * (static_cast<float>(i) + 0.5f) / static_cast<float>(w);
  const float th = kPi * (static_cast<float>(j) + 0.5f) / static_cast<float>(h);
  return v(std::sin(th) * std::cos(lon), std::sin(th) * std::sin(lon), std::cos(th));
}

// ==================================================================================================================
// r170 (Lance: "ALL the remaining ball skins ready for the headset"): the other 21 from logs/balls-mockup, every one a
// living surface (its own motion over its loop) - one function each below, the table after them. Only C++ now: the
// shop's preview ball samples the CPU bake (xrfx::set_ball_atlas), the PC proofs run this very file compiled to
// WebAssembly (tools/outside/balls170) - one source of truth.
// ==================================================================================================================
inline V hsv(float h, float s, float val) {
  h = h - std::floor(h);
  const float r = clamp01(std::fabs(h * 6.0f - 3.0f) - 1.0f), g = clamp01(2.0f - std::fabs(h * 6.0f - 2.0f)),
              b = clamp01(2.0f - std::fabs(h * 6.0f - 4.0f));
  return v((1.0f + (r - 1.0f) * s) * val, (1.0f + (g - 1.0f) * s) * val, (1.0f + (b - 1.0f) * s) * val);
}
inline V clampv(V c) { return v(clamp01(c.x), clamp01(c.y), clamp01(c.z)); }
inline float dotv(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline float lenv(V a) { return std::sqrt(dotv(a, a)); }
inline V normv(V a) {
  const float l = lenv(a);
  return l > 1e-6f ? a * (1.0f / l) : v(0.0f, 0.0f, 1.0f);
}
// 3D cells: the nearest and second-nearest feature distances and the nearest's cell
struct Cell {
  float f1, f2;
  int32_t cx, cy, cz;
  V at;
};
inline Cell voronoi(V p) {
  const int32_t ix = static_cast<int32_t>(std::floor(p.x)), iy = static_cast<int32_t>(std::floor(p.y)), iz = static_cast<int32_t>(std::floor(p.z));
  Cell c{9.0f, 9.0f, 0, 0, 0, v(0, 0, 0)};
  for (int dz = -1; dz <= 1; ++dz)
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx) {
        const int32_t x = ix + dx, y = iy + dy, z = iz + dz;
        const V f = v(x + hash3(x, y, z), y + hash3(y, z, x + 3), z + hash3(z, x + 5, y));
        const float d = lenv(f - p);
        if (d < c.f1) {
          c.f2 = c.f1, c.f1 = d, c.cx = x, c.cy = y, c.cz = z, c.at = f;
        } else if (d < c.f2) {
          c.f2 = d;
        }
      }
  return c;
}
inline V rotz(V n, float a) { return v(n.x * std::cos(a) - n.y * std::sin(a), n.x * std::sin(a) + n.y * std::cos(a), n.z); }
inline V fire_ramp(float h) {
  h = clamp01(h);
  if (h < 0.3f) return mixv(v(0.05f, 0.0f, 0.0f), v(0.70f, 0.06f, 0.0f), h / 0.3f);
  if (h < 0.6f) return mixv(v(0.70f, 0.06f, 0.0f), v(1.0f, 0.42f, 0.04f), (h - 0.3f) / 0.3f);
  return mixv(v(1.0f, 0.42f, 0.04f), v(1.0f, 0.92f, 0.62f), (h - 0.6f) / 0.4f);
}

// SNOWBALL: packed snow lumps, blue crevices, icy patches, glints twinkling
inline V snowball(V n, float ph, int k) {
  const float lump = fbm(n * 6.0f, 4), crev = 1.0f - std::fabs(2.0f * fbm(n * 14.0f + v(3, 1, 2), 3) - 1.0f);
  V c = mixv(v(0.74f, 0.82f, 0.93f), v(0.97f, 0.98f, 1.0f), smooth(0.35f, 0.65f, lump));
  c = mixv(c, v(0.55f, 0.68f, 0.88f), 0.45f * std::pow(crev, 6.0f));
  c = mixv(c, v(0.72f, 0.88f, 1.0f), 0.5f * smooth(0.6f, 0.7f, fbm(n * 3.0f + v(7, 7, 7), 3)));
  const V g = n * 46.0f;
  const int32_t gx = static_cast<int32_t>(std::floor(g.x)), gy = static_cast<int32_t>(std::floor(g.y)), gz = static_cast<int32_t>(std::floor(g.z));
  if (hash3(gx, gy, gz) > 0.92f) {
    const float tw = hash3(gx + k * 17, gy, gz);
    const V d = g - v(gx + 0.5f, gy + 0.5f, gz + 0.5f);
    c = c + v(1.0f, 1.0f, 1.0f) * (std::exp(-dotv(d, d) * 9.0f) * (tw > 0.5f ? 1.2f * tw : 0.2f));
  }
  (void)ph;
  return clampv(c);
}
// LAVA ROCK: dark basalt, its cracks glowing and flowing, pores breathing
inline V lava(V n, float ph, int) {
  const V w = v(std::cos(ph), std::sin(ph), 0.0f) * 0.18f;
  const Cell c = voronoi(n * 3.6f + v(fbm(n * 2.0f + w, 2), fbm(n * 2.0f - w + v(4, 4, 4), 2), 0.0f) * 0.6f);
  const float edge = c.f2 - c.f1;
  const float crack = 1.0f - smooth(0.0f, 0.12f, edge);
  const float flow = fbm(n * 7.0f + v(std::cos(ph), std::sin(ph), std::cos(ph) * 0.5f) * 0.9f, 3);
  const float pulse = 0.65f + 0.35f * std::sin(ph + hash3(c.cx, c.cy, c.cz) * 6.28f);
  const float heat = clamp01(crack * (0.55f + 0.6f * flow) * pulse * 1.3f);
  const float rock = fbm(n * 11.0f, 3);
  V col = mixv(v(0.05f, 0.045f, 0.045f), v(0.16f, 0.13f, 0.12f), rock);
  col = col * (0.7f + 0.5f * smooth(0.02f, 0.3f, edge));
  return clampv(col + fire_ramp(heat) * heat * 1.1f);
}
// THUNDER BALL: a dark storm sphere, electric veins crackling across it, flaring frame to frame
inline V thunder(V n, float ph, int k) {
  const V w = v(std::cos(ph), std::sin(ph), std::sin(ph) * 0.7f) * 1.1f;
  const float cl = fbm(n * 2.5f + w * 0.3f, 4);
  V col = mixv(v(0.03f, 0.04f, 0.12f), v(0.20f, 0.22f, 0.40f), cl);
  const float r1 = 1.0f - std::fabs(2.0f * fbm(n * 3.2f + w, 4) - 1.0f);
  const float r2 = 1.0f - std::fabs(2.0f * fbm(n * 6.0f - w * 1.3f + v(5, 1, 3), 3) - 1.0f);
  const float flick = 0.55f + 0.65f * hash3(k, 7, 3);
  const float vein = std::pow(r1, 30.0f) * flick + std::pow(r2, 40.0f) * 0.7f * (0.6f + 0.6f * hash3(k, 9, 1));
  col = col + v(0.55f, 0.80f, 1.0f) * vein * 1.4f + v(1.0f, 1.0f, 1.0f) * std::pow(vein, 3.0f);
  return clampv(col);
}
// GALAXY: a spiral galaxy wrapped round each pole - two arms with dust lanes, a blazing core, nebulae, stars near and far
inline V galaxy(V n, float ph, int k) {
  const float d = std::acos(std::fabs(n.z)) / (0.5f * kPi);  // 0 the pole .. 1 the equator
  const float ang = std::atan2(n.y, n.x) * (n.z >= 0.0f ? 1.0f : -1.0f);
  const float rot = 0.5f * ph;  // (two arms: half a turn a loop is a whole one)
  const float arm = std::cos(2.0f * (ang - 2.6f * std::log(d + 0.06f) - rot));
  const float neb = fbm(n * 4.0f + v(1, 2, 3), 4);
  const float arms = smooth(0.15f, 1.0f, arm) * std::exp(-d * 1.4f) * (0.55f + 0.7f * neb);
  const float dust = smooth(0.55f, 0.95f, std::cos(2.0f * (ang - 2.6f * std::log(d + 0.06f) - rot) - 0.55f)) * std::exp(-d * 1.2f);
  V col = v(0.006f, 0.006f, 0.02f) + v(0.05f, 0.02f, 0.10f) * smooth(0.4f, 0.8f, fbm(n * 2.0f, 3));
  col = col + mixv(v(0.35f, 0.40f, 1.0f), v(1.0f, 0.45f, 0.85f), neb) * arms * 1.2f;
  col = col * (1.0f - 0.65f * dust);
  col = col + v(1.0f, 0.92f, 0.75f) * (std::exp(-d * d * 60.0f) * 1.3f + std::exp(-d * d * 8.0f) * 0.25f);
  for (int layer = 0; layer < 2; ++layer) {  // stars: fine far ones, a few bright near ones
    const float sc = layer == 0 ? 60.0f : 18.0f;
    const V g = n * sc;
    const int32_t gx = static_cast<int32_t>(std::floor(g.x)), gy = static_cast<int32_t>(std::floor(g.y)), gz = static_cast<int32_t>(std::floor(g.z));
    if (hash3(gx, gy, gz + layer * 11) > (layer == 0 ? 0.86f : 0.95f)) {
      const V dd = g - v(gx + 0.5f, gy + 0.5f, gz + 0.5f);
      const float tw = 0.6f + 0.4f * std::sin(ph * 2.0f + hash3(gx, gy, gz) * 40.0f);
      col = col + hsv(0.55f + 0.2f * hash3(gy, gz, gx), 0.25f, 1.0f) * (std::exp(-dotv(dd, dd) * (layer == 0 ? 30.0f : 12.0f)) * tw * (layer == 0 ? 0.8f : 1.4f));
    }
  }
  (void)k;
  return clampv(col);
}
// MOON: grey regolith, dark maria, craters of every size (cells in 3D: no seam), each with its bright rim
inline V moon(V n, float, int) {
  float g = 0.58f + 0.22f * (fbm(n * 9.0f, 4) - 0.5f);
  g -= 0.22f * smooth(0.52f, 0.60f, fbm(n * 1.3f + v(2, 5, 1), 4));  // maria
  for (int s = 0; s < 3; ++s) {
    const float sc = s == 0 ? 3.0f : (s == 1 ? 7.0f : 16.0f);
    const Cell c = voronoi(n * sc + v(s * 3.1f, s * 1.7f, s * 5.3f));
    const float rad = 0.18f + 0.22f * hash3(c.cx, c.cy + 9, c.cz);
    if (hash3(c.cx + 4, c.cy, c.cz) < 0.7f) {
      const float t = c.f1 / rad;
      if (t < 1.25f) {
        const float floor_ = smooth(0.0f, 0.85f, t);
        g += (t < 1.0f ? -0.10f * (1.0f - floor_) : 0.0f) + 0.16f * std::exp(-std::pow((t - 1.0f) / 0.12f, 2.0f));
      }
    }
  }
  g = clamp01(g);
  return v(g, g * 0.98f, g * 0.95f);
}
// 8-BIT PIXEL: a ball of big chunky pixels, outlined, its colours cycling like an old console's palette
inline V pixel(V n, float, int k) {
  const float lon = std::atan2(n.y, n.x) / (2.0f * kPi) + 0.5f, lat = std::acos(clamp01(n.z * 0.5f + 0.5f) * 2.0f - 1.0f) / kPi;
  const int i = static_cast<int>(std::floor(lon * 24.0f)), j = static_cast<int>(std::floor(lat * 12.0f));
  const float fu = lon * 24.0f - i, fv = lat * 12.0f - j;
  static const float pal[6][3] = {{0.90f, 0.15f, 0.13f}, {1.0f, 0.85f, 0.0f}, {0.18f, 0.80f, 0.25f},
                                  {0.0f, 0.70f, 1.0f}, {0.69f, 0.29f, 0.93f}, {1.0f, 0.48f, 0.0f}};
  const int band = (j + (i / 4 + j / 3) + k) % 6;
  const float* pc = pal[band];
  V c = v(pc[0], pc[1], pc[2]);
  if (hash3(i, j, 1) > 0.82f) c = v(1.0f, 1.0f, 1.0f);
  const float edge = std::min(std::min(fu, 1.0f - fu), std::min(fv, 1.0f - fv));
  if (edge < 0.08f) c = c * 0.25f;
  return c;
}
// NEON WIREFRAME: a black ball, a neon grid of glowing tubes, a pulse racing along them
inline V neon(V n, float ph, int) {
  const float lon = std::atan2(n.y, n.x), lat = std::asin(std::max(-1.0f, std::min(1.0f, n.z)));
  const float step = kPi / 12.0f;
  const float dl = std::fabs(lon / step - std::round(lon / step)) * step * std::cos(lat);
  const float dt = std::fabs(lat / step - std::round(lat / step)) * step;
  const int li = static_cast<int>(std::round(lon / step)), ti = static_cast<int>(std::round(lat / step));
  const float a = std::exp(-std::pow(dl / 0.018f, 2.0f)), b = std::exp(-std::pow(dt / 0.018f, 2.0f));
  const float ha = std::exp(-std::pow(dl / 0.07f, 2.0f)) * 0.3f, hb = std::exp(-std::pow(dt / 0.07f, 2.0f)) * 0.3f;
  const float pulse = 0.45f + 0.55f * std::pow(0.5f + 0.5f * std::sin(lat * 3.0f + lon * 2.0f - ph * 2.0f), 4.0f);
  const V cA = (li & 1) ? v(0.0f, 0.95f, 1.0f) : v(1.0f, 0.15f, 0.85f);
  const V cB = (ti & 1) ? v(1.0f, 0.15f, 0.85f) : v(0.0f, 0.95f, 1.0f);
  V col = v(0.015f, 0.01f, 0.03f) + cA * ((a + ha) * pulse) + cB * ((b + hb) * pulse);
  col = col + v(1.0f, 1.0f, 1.0f) * (a * b * 0.8f);
  return clampv(col);
}
// DISCO BALL: mirror tiles, each catching the room's light and flashing as it turns
inline V disco(V n, float ph, int k) {
  const float lat = std::asin(std::max(-1.0f, std::min(1.0f, n.z))), lon = std::atan2(n.y, n.x) + kPi;
  const int rows = 18, row = static_cast<int>(std::floor((lat / kPi + 0.5f) * rows));
  const float rl = ((row + 0.5f) / rows - 0.5f) * kPi;
  const int cols = std::max(4, static_cast<int>(std::round(2.0f * rows * std::cos(rl))));
  const int col = static_cast<int>(std::floor(lon / (2.0f * kPi) * cols));
  const float fu = lon / (2.0f * kPi) * cols - col, fv = (lat / kPi + 0.5f) * rows - row;
  const float h = hash3(row, col, 3);
  float g = 0.38f + 0.35f * h + 0.25f * smooth(-0.2f, 0.9f, std::sin(rl));  // (the ceiling's light on the upper tiles)
  V c = v(g, g, g * 1.04f);
  const float fl = hash3(row, col, k + 40);
  if (fl > 0.86f) c = c + hsv(hash3(row, col, k), 0.35f, 1.0f) * (fl - 0.86f) * 9.0f;
  const float e = std::min(std::min(fu, 1.0f - fu), std::min(fv, 1.0f - fv));
  if (e < 0.08f) c = c * 0.3f;
  (void)ph;
  return clampv(c);
}
// BASKETBALL: pebbled orange leather, black channels
inline V basketball(V n, float, int) {
  const float peb = fbm(n * 55.0f, 2);
  V c = v(0.82f, 0.36f, 0.09f) * (0.82f + 0.3f * peb);
  const float w = 0.022f;
  const bool seam = std::fabs(n.x) < w || std::fabs(n.z) < w || std::fabs(std::fabs(n.y) - 0.70f + 0.30f * n.x * n.x) < w;
  if (seam) c = v(0.05f, 0.03f, 0.02f);
  return c;
}
// 8-BALL: glossy black, the white circle and its 8
inline V eightball(V n, float, int) {
  const float d = std::acos(std::max(-1.0f, std::min(1.0f, n.x)));
  if (d > 0.44f) return v(0.025f, 0.025f, 0.03f);
  if (d > 0.42f) return v(0.5f, 0.5f, 0.5f);
  const float py = n.z / 0.40f, px = n.y / 0.40f;  // (on the circle: up / across)
  const float r1 = std::sqrt(px * px + (py - 0.33f) * (py - 0.33f)), r2 = std::sqrt(px * px + (py + 0.36f) * (py + 0.36f));
  const bool ink = std::fabs(r1 - 0.27f) < 0.07f || std::fabs(r2 - 0.31f) < 0.075f;
  return ink ? v(0.03f, 0.03f, 0.03f) : v(0.95f, 0.95f, 0.93f);
}
// BOWLING BALL: swirled resin, three finger holes
inline V bowling(V n, float, int) {
  const float s = fbm(n * 2.2f + v(fbm(n * 3.0f, 3), fbm(n * 3.0f + v(5, 5, 5), 3), 0.0f) * 2.2f, 4);
  V c = mixv(v(0.10f, 0.03f, 0.25f), v(0.25f, 0.40f, 0.95f), smooth(0.35f, 0.75f, s));
  c = mixv(c, v(0.02f, 0.02f, 0.05f), smooth(0.55f, 0.75f, fbm(n * 5.0f + v(1, 1, 1), 3)) * 0.6f);
  const V holes[3] = {normv(v(0.92f, 0.12f, 0.35f)), normv(v(0.92f, -0.14f, 0.35f)), normv(v(0.95f, 0.0f, 0.05f))};
  for (const V& hd : holes) {
    const float d = std::acos(std::max(-1.0f, std::min(1.0f, dotv(n, hd))));
    if (d < 0.11f) c = v(0.01f, 0.01f, 0.015f) * (1.0f + 3.0f * d);
    else if (d < 0.13f) c = c * 1.6f;
  }
  return clampv(c);
}
// OLD LEATHER BALL: worn brown panels, dark stitched seams, laces
inline V leather(V n, float, int) {
  const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
  const int face = ax > ay && ax > az ? 0 : (ay > az ? 1 : 2);
  const float m1 = std::max(ax, std::max(ay, az));
  const float m2 = face == 0 ? std::max(ay, az) : (face == 1 ? std::max(ax, az) : std::max(ax, ay));
  const float tone = 0.85f + 0.15f * hash3(face, n.x > 0, n.z > 0);
  V c = v(0.46f, 0.28f, 0.14f) * tone * (0.75f + 0.45f * fbm(n * 18.0f, 3));
  c = c * (1.0f - 0.35f * smooth(0.5f, 0.8f, fbm(n * 4.0f + v(9, 2, 4), 3)));  // (worn dark patches)
  const float seam = m1 - m2;
  if (seam < 0.03f) {
    c = c * 0.35f;
    const float st = std::sin((n.x + n.y + n.z) * 160.0f);
    if (st > 0.6f) c = v(0.80f, 0.72f, 0.55f);
  }
  if (n.z > 0.85f && std::fabs(n.x) < 0.06f) c = v(0.75f, 0.66f, 0.48f) * (0.7f + 0.3f * std::sin(n.y * 120.0f));  // laces
  return clampv(c);
}
// GOLD TROPHY BALL: polished gold, engraved panels, a sheen sweeping over it
inline V gold(V n, float ph, int) {
  const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
  const float m1 = std::max(ax, std::max(ay, az));
  const float m2 = ax >= m1 ? std::max(ay, az) : (ay >= m1 ? std::max(ax, az) : std::max(ax, ay));
  V c = v(0.86f, 0.64f, 0.18f) * (0.8f + 0.25f * fbm(n * 30.0f, 2));
  if (m1 - m2 < 0.02f) c = v(0.45f, 0.30f, 0.06f);
  const float band = std::exp(-std::pow((n.x * std::cos(ph) + n.y * std::sin(ph) - 0.2f * n.z) / 0.18f, 2.0f));
  c = c + v(1.0f, 0.95f, 0.75f) * band * 0.7f;
  return clampv(c);
}
// DIAMOND BALL: cut facets, each flashing its own rainbow fire as it turns
inline V diamond(V n, float ph, int k) {
  const Cell c = voronoi(n * 5.0f);
  const float h = hash3(c.cx, c.cy, c.cz);
  const float sh = 0.55f + 0.45f * std::sin(ph + h * 6.28f);
  V col = mixv(v(0.25f, 0.42f, 0.70f), v(0.92f, 0.97f, 1.0f), sh * sh);
  const float fl = hash3(c.cx + k * 13, c.cy, c.cz);
  if (fl > 0.78f) col = col * 0.5f + hsv(h + k / 16.0f, 0.65f, 1.0f) * 0.9f;
  const float edge = c.f2 - c.f1;
  if (edge < 0.05f) col = v(1.0f, 1.0f, 1.0f);
  return clampv(col);
}
// CHROME TIZM: a mirror - its room reflected (bright sky over a sharp horizon, dark ground), a moving glint, the diamond
inline V chrome(V n, float ph, int) {
  const float z = n.z + 0.08f * std::sin(n.x * 3.0f + ph);
  V c = z > 0.0f ? mixv(v(0.70f, 0.78f, 0.88f), v(0.98f, 0.99f, 1.0f), smooth(0.0f, 0.6f, z)) : mixv(v(0.20f, 0.18f, 0.16f), v(0.05f, 0.05f, 0.06f), smooth(0.0f, -0.6f, z));
  if (z > 0.0f) c = c * (0.85f + 0.15f * smooth(0.45f, 0.65f, fbm(v(n.x * 3.0f, n.y * 3.0f, z * 6.0f), 3)));  // (clouds in the sky it reflects)
  else c = c + v(0.10f, 0.09f, 0.08f) * smooth(0.4f, 0.7f, fbm(n * 7.0f, 3));                                  // (the floor it reflects)
  if (std::fabs(z) < 0.02f) c = v(1.0f, 1.0f, 1.0f);
  const float g = std::exp(-std::pow((n.x * std::cos(ph) + n.y * std::sin(ph) - 0.55f) / 0.08f, 2.0f) - std::pow((n.z - 0.4f) / 0.2f, 2.0f));
  c = c + v(1.0f, 1.0f, 1.0f) * g;
  const float ex = std::fabs(n.y) + std::fabs(n.z);  // (the diamond, engraved at the front)
  if (n.x > 0.0f && std::fabs(ex - 0.35f) < 0.025f) c = v(0.25f, 0.27f, 0.32f);
  return clampv(c);
}
// BLACK 1/1: matte black, a gold foil band with its 1/1, a holo kaleidoscope turning under the black
inline V black11(V n, float ph, int) {
  const float a = std::atan2(n.z, n.y);
  const float kal = std::cos(6.0f * a + ph) * std::cos(std::acos(std::max(-1.0f, std::min(1.0f, n.x))) * 9.0f - ph * 2.0f);
  V c = v(0.035f, 0.035f, 0.04f) + hsv(a / 6.28f + ph / 6.28f, 0.8f, 0.10f) * smooth(0.5f, 1.0f, kal);
  if (std::fabs(n.z) < 0.09f) {
    c = v(0.85f, 0.66f, 0.22f) * (0.8f + 0.3f * std::sin(std::atan2(n.y, n.x) * 40.0f + ph));
    const float lon = std::atan2(n.y, n.x);
    if (std::fabs(lon) < 0.18f && std::fabs(n.z) < 0.06f) {  // the 1/1 (three strokes)
      const float x = lon / 0.18f;
      if (std::fabs(x + 0.6f) < 0.1f || std::fabs(x - 0.6f) < 0.1f || std::fabs(x + n.z * 6.0f) < 0.1f) c = v(0.08f, 0.06f, 0.02f);
    }
  }
  return clampv(c);
}
// EYEBALL: veined white, a fibrous iris looking about, its pupil breathing
inline V eyeball(V n, float ph, int) {
  const V look = normv(v(1.0f, 0.22f * std::sin(ph), 0.14f * std::cos(ph * 2.0f)));
  const float d = std::acos(std::max(-1.0f, std::min(1.0f, dotv(n, look))));
  V c = v(0.95f, 0.92f, 0.88f) * (0.9f + 0.1f * fbm(n * 8.0f, 2));
  const float vein = std::pow(1.0f - std::fabs(2.0f * fbm(n * 4.0f + v(3, 3, 3), 4) - 1.0f), 40.0f) * smooth(0.6f, 1.9f, d);
  c = mixv(c, v(0.75f, 0.10f, 0.10f), clamp01(vein * 1.5f));
  if (d < 0.45f) {
    const V up = normv(v(-look.z, 0.0f, look.x) + v(0, 0, 0.0001f));
    const float ang = std::atan2(dotv(n, normv(v(-look.y, look.x, 0.0f))), dotv(n, v(0, 0, 1)));
    (void)up;
    const float fib = fbm(v(std::cos(ang * 9.0f) * 3.0f, std::sin(ang * 9.0f) * 3.0f, d * 12.0f), 3);
    c = mixv(v(0.10f, 0.35f, 0.55f), v(0.30f, 0.70f, 0.55f), fib) * (0.6f + 0.6f * smooth(0.45f, 0.2f, d));
    if (d > 0.40f) c = c * 0.35f;
    const float pupil = 0.14f + 0.05f * std::sin(ph);
    if (d < pupil) c = v(0.01f, 0.01f, 0.01f);
    if (std::acos(std::max(-1.0f, std::min(1.0f, dotv(n, normv(look + v(0.0f, 0.12f, 0.16f)))))) < 0.05f) c = v(1, 1, 1);
  }
  return clampv(c);
}
// BOB-OMB BALL: a glossy black bomb, its brass cap, a warning glow pulsing faster and faster
inline V bomb(V n, float ph, int) {
  V c = v(0.05f, 0.05f, 0.06f) * (0.9f + 0.2f * fbm(n * 20.0f, 2));
  if (n.z > 0.86f) c = n.z > 0.93f ? v(0.55f, 0.42f, 0.18f) : v(0.80f, 0.62f, 0.25f);
  const float pulse = std::pow(0.5f + 0.5f * std::sin(ph * 4.0f), 6.0f);
  c = c + v(1.0f, 0.12f, 0.05f) * (pulse * 0.55f * smooth(0.2f, 0.9f, 1.0f - std::fabs(n.z)));
  return clampv(c);
}
// CANNONBALL: pitted cast iron, rust blooms, dents
inline V cannon(V n, float, int) {
  float g = 0.13f + 0.06f * fbm(n * 25.0f, 3);
  const Cell c = voronoi(n * 6.0f);
  if (c.f1 < 0.18f && hash3(c.cx, c.cy, c.cz) > 0.55f) g *= 0.6f + 0.4f * (c.f1 / 0.18f);
  V col = v(g, g, g * 1.06f);
  col = mixv(col, v(0.36f, 0.17f, 0.07f) * (0.7f + 0.5f * fbm(n * 12.0f, 3)), smooth(0.58f, 0.72f, fbm(n * 3.0f + v(2, 2, 2), 4)) * 0.8f);
  return clampv(col);
}
// RUBBER DUCK: squeaky yellow rubber, an orange beak, two shining eyes
inline V duck(V n, float, int) {
  V c = v(1.0f, 0.84f, 0.12f);
  const float beak = std::pow(n.y / 0.30f, 2.0f) + std::pow((n.z + 0.05f) / 0.16f, 2.0f);
  if (n.x > 0.7f && beak < 1.0f) c = v(1.0f, 0.45f, 0.05f);
  for (int s = -1; s <= 1; s += 2) {
    const V e = normv(v(0.78f, 0.30f * s, 0.42f));
    const float d = std::acos(std::max(-1.0f, std::min(1.0f, dotv(n, e))));
    if (d < 0.11f) c = v(0.02f, 0.02f, 0.03f);
    if (std::acos(std::max(-1.0f, std::min(1.0f, dotv(n, normv(e + v(0.0f, 0.03f, 0.05f)))))) < 0.03f) c = v(1, 1, 1);
  }
  if (n.x < -0.85f && n.z > 0.2f) c = v(1.0f, 0.78f, 0.08f);  // (its tail's tuft, a touch deeper)
  return c;
}
// BLACK HOLE: the horizon's black, a hot accretion swirl round its middle, lensed arcs over its poles
inline V blackhole(V n, float ph, int) {
  const V r = rotz(n, -ph);
  const float band = std::exp(-std::pow(n.z / 0.20f, 2.0f));
  const float sw = fbm(v(r.x * 3.0f, r.y * 3.0f, n.z * 10.0f) + v(std::cos(ph), std::sin(ph), 0.0f) * 0.5f, 4);
  const float hot = band * (0.4f + 0.9f * sw);
  V c = v(0.0f, 0.0f, 0.0f) + mixv(v(0.55f, 0.15f, 0.95f), v(1.0f, 0.75f, 0.35f), clamp01(hot)) * hot * 1.3f;
  const float arc = std::exp(-std::pow((std::fabs(n.z) - 0.72f) / 0.04f, 2.0f)) * (0.5f + 0.5f * sw);
  c = c + v(1.0f, 0.85f, 0.6f) * arc * 0.8f;
  return clampv(c);
}

// The table: every skin's bake (frames over its loop, its picture's size), its turn about its pole as drawn (s a turn,
// 0 none), whether it is its own light (drawn unlit), its glow's colour (tiles, the preview's rim). Order = the shop's,
// = include/port/ball_skin.h's PORT_BALL_* (saved / sent by id, never by place).
struct SkinDef {
  const char* id;
  int frames, w, h;
  float loop, spin;
  bool emissive;
  uint32_t glow;  // 0xRRGGBB, 0 none
};
inline const SkinDef* skin_defs() {
  static const SkinDef k[] = {
      {"classic", 1, 64, 32, 1.0f, 0.0f, false, 0},
      {"fireball", kFireFrames, 256, 128, kFireLoop, 0.0f, true, 0xFF7A1A},
      {"earth", kEarthFrames, 192, 96, kEarthLoop, kEarthSpin, false, 0x5AB0FF},
      {"snowball", 16, 192, 96, 2.0f, 0.0f, false, 0xCFEFFF},
      {"lava", 24, 192, 96, 3.0f, 0.0f, true, 0xFF5A10},
      {"thunder", 16, 192, 96, 1.0f, 0.0f, true, 0x7FD8FF},
      {"galaxy", 32, 256, 128, 8.0f, 20.0f, true, 0x9A6BFF},
      {"moon", 1, 256, 128, 1.0f, 40.0f, false, 0xDDE4F0},
      {"pixel", 6, 192, 96, 1.2f, 0.0f, true, 0},
      {"neon", 16, 256, 128, 2.0f, 10.0f, true, 0xFF3AD8},
      {"disco", 16, 256, 128, 2.0f, 6.0f, false, 0xFFFFFF},
      {"basketball", 1, 256, 128, 1.0f, 0.0f, false, 0},
      {"eightball", 1, 256, 128, 1.0f, 0.0f, false, 0},
      {"bowling", 1, 256, 128, 1.0f, 0.0f, false, 0},
      {"leather", 1, 256, 128, 1.0f, 0.0f, false, 0},
      {"gold", 16, 192, 96, 2.0f, 0.0f, false, 0xFFD34A},
      {"diamond", 16, 192, 96, 2.0f, 0.0f, true, 0xBFE8FF},
      {"chrome", 16, 192, 96, 2.0f, 0.0f, true, 0xE8ECF5},
      {"black11", 16, 192, 96, 3.0f, 0.0f, false, 0xC9A227},
      {"eyeball", 16, 192, 96, 3.0f, 0.0f, false, 0},
      {"bomb", 16, 128, 64, 1.0f, 0.0f, false, 0xFF3A1A},
      {"cannon", 1, 256, 128, 1.0f, 0.0f, false, 0},
      {"duck", 1, 192, 96, 1.0f, 0.0f, false, 0},
      {"blackhole", 32, 192, 96, 4.0f, 8.0f, true, 0x9A4BFF},
  };
  return k;
}
constexpr int kSkinCount = 24;

// skin `skin`'s baked frame k (of its table's frames) at n
inline V shade_frame(int skin, V n, int k) {
  const SkinDef& d = skin_defs()[skin < 0 || skin >= kSkinCount ? 0 : skin];
  const float ph = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(std::max(1, d.frames));
  switch (skin) {
    case 1: return fire(n, ph);
    case 2: return earth(n, ph, k, nullptr, nullptr);
    case 3: return snowball(n, ph, k);
    case 4: return lava(n, ph, k);
    case 5: return thunder(n, ph, k);
    case 6: return galaxy(n, ph, k);
    case 7: return moon(n, ph, k);
    case 8: return pixel(n, ph, k);
    case 9: return neon(n, ph, k);
    case 10: return disco(n, ph, k);
    case 11: return basketball(n, ph, k);
    case 12: return eightball(n, ph, k);
    case 13: return bowling(n, ph, k);
    case 14: return leather(n, ph, k);
    case 15: return gold(n, ph, k);
    case 16: return diamond(n, ph, k);
    case 17: return chrome(n, ph, k);
    case 18: return black11(n, ph, k);
    case 19: return eyeball(n, ph, k);
    case 20: return bomb(n, ph, k);
    case 21: return cannon(n, ph, k);
    case 22: return duck(n, ph, k);
    case 23: return blackhole(n, ph, k);
    default: {  // CLASSIC: white with its dark patches (the shop's tile only: the game's own ball is drawn as it is)
      static const float ico[12][3] = {{0, 0.526f, 0.851f},  {0, -0.526f, 0.851f},  {0, 0.526f, -0.851f}, {0, -0.526f, -0.851f},
                                       {0.526f, 0.851f, 0},  {-0.526f, 0.851f, 0},  {0.526f, -0.851f, 0}, {-0.526f, -0.851f, 0},
                                       {0.851f, 0, 0.526f},  {-0.851f, 0, 0.526f},  {0.851f, 0, -0.526f}, {-0.851f, 0, -0.526f}};
      float best = 9.0f;
      for (const auto& c : ico) best = std::min(best, std::acos(std::max(-1.0f, std::min(1.0f, n.x * c[0] + n.y * c[1] + n.z * c[2]))));
      return best < 0.34f ? v(0.09f, 0.09f, 0.09f) : v(0.94f, 0.94f, 0.94f);
    }
  }
}
// at time t (s): the frame shown then
inline V shade(int skin, V n, float t) {
  const SkinDef& d = skin_defs()[skin < 0 || skin >= kSkinCount ? 0 : skin];
  const int k = static_cast<int>(std::floor(t / d.loop * d.frames)) % std::max(1, d.frames);
  return shade_frame(skin, n, k < 0 ? 0 : k);
}

}  // namespace ballshade
