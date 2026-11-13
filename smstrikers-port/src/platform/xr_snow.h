// strikers-quest r214 (Lance 11-10: "make the snow wayyyyy more realistic on the field, from texture to behavior ... even
// when the snow accumulates in my house in MR, its not as realistic as id like. it looks like white pixels on the floor.
// that last part goes for both on the field in game and in MR"). The snow's own PURE part (no library calls but <math.h>,
// no allocation), shared by:
//   - the game's pitch (xr_weather.cpp: the broadcast picture + the MR Tabletop field): the snow grid (48 x 27 now), the
//     snow texture (layered coverage stages, slush, flake sprites, a glint), the stage alphas;
//   - the effects layer (xr_fx.cpp): the noise in the fire flipbook's free channels that xr_fx.frag shape 180 (the snow
//     layer) reads, and your room's snow field (floor cells, drifts at walls / furniture, your footprints, furniture tops);
//   - the PC proofs (tools/outside/snow214: WebAssembly + node, the previews in Python read what this writes).
// LOOKS ONLY (Lance's rule): nothing here reads or writes the game - positions come in, numbers to draw go out.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "xr_room.h"

namespace xrsnow {

// ------------------------------------------------------------------------------------------------ small helpers
inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) {
  uint32_t h = (a * 0x8da6b343u) ^ (b * 0xd8163841u) ^ (c * 0xcb1ab31fu);
  h ^= h >> 15;
  h *= 0x2c1b3c6du;
  h ^= h >> 12;
  h *= 0x297a2d39u;
  h ^= h >> 15;
  return h;
}
inline float h01(uint32_t a, uint32_t b, uint32_t c) { return static_cast<float>(hash3(a, b, c) >> 8) * (1.0f / 16777216.0f); }
inline float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
inline float sstep(float e0, float e1, float x) {
  const float t = clamp01((x - e0) / (e1 - e0));
  return t * t * (3.0f - 2.0f * t);
}
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline int wrapi(int i, int p) {
  const int m = i % p;
  return m < 0 ? m + p : m;
}
inline uint8_t byte01(float f) { return static_cast<uint8_t>(f <= 0.0f ? 0 : (f >= 1.0f ? 255 : static_cast<int>(f * 255.0f + 0.5f))); }

// Value noise that repeats every px x py cells.
inline float pnoise(float x, float y, int px, int py, uint32_t seed) {
  const float fx = floorf(x), fy = floorf(y);
  const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
  const float tx = x - fx, ty = y - fy;
  const uint32_t x0 = static_cast<uint32_t>(wrapi(ix, px)), x1 = static_cast<uint32_t>(wrapi(ix + 1, px));
  const uint32_t y0 = static_cast<uint32_t>(wrapi(iy, py)), y1 = static_cast<uint32_t>(wrapi(iy + 1, py));
  const float a = h01(x0, y0, seed), b = h01(x1, y0, seed), c = h01(x0, y1, seed), d = h01(x1, y1, seed);
  const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
  return (a + (b - a) * sx) * (1.0f - sy) + (c + (d - c) * sx) * sy;
}
// fBm over one tile (u, v in 0..1 of it; bx x by cells across it at the first octave), seamless at its edges
inline float pfbm(float u, float v, int bx, int by, int oct, uint32_t seed, float gain) {
  float sum = 0.0f, amp = 1.0f, nrm = 0.0f;
  int cx = bx, cy = by;
  for (int o = 0; o < oct; ++o) {
    sum += amp * pnoise(u * static_cast<float>(cx), v * static_cast<float>(cy), cx, cy, seed + 101u * static_cast<uint32_t>(o));
    nrm += amp;
    amp *= gain;
    cx *= 2;
    cy *= 2;
  }
  return sum / nrm;
}
// Rank-equalizes n values in place to 0..1 (a 2048-bin histogram): a threshold t then leaves 1 - t of them above it, so
// "how much of the ground the snow covers" is exactly what the depth asks for.
inline void equalize(float* v, int n) {
  if (n <= 0) return;
  float lo = v[0], hi = v[0];
  for (int i = 1; i < n; ++i) {
    lo = v[i] < lo ? v[i] : lo;
    hi = v[i] > hi ? v[i] : hi;
  }
  const float span = hi - lo > 1e-9f ? hi - lo : 1.0f;
  static const int kBins = 2048;
  uint32_t count[kBins];
  float before[kBins];
  memset(count, 0, sizeof(count));
  for (int i = 0; i < n; ++i) {
    int b = static_cast<int>((v[i] - lo) / span * (kBins - 0.001f));
    b = b < 0 ? 0 : (b >= kBins ? kBins - 1 : b);
    ++count[b];
  }
  float acc = 0.0f;
  for (int b = 0; b < kBins; ++b) {
    before[b] = acc;
    acc += static_cast<float>(count[b]);
  }
  for (int i = 0; i < n; ++i) {
    const float f = (v[i] - lo) / span * (kBins - 0.001f);
    int b = static_cast<int>(f);
    b = b < 0 ? 0 : (b >= kBins ? kBins - 1 : b);
    const float in = clamp01(f - static_cast<float>(b));
    v[i] = (before[b] + in * static_cast<float>(count[b])) / static_cast<float>(n);
  }
}

// A falling flake's look (the sprite here, xr_fx.frag shape 181 - keep the two line for line): a soft lumpy round clump,
// denser in its middle, a hint of six-fold feathering when crisp; blur 0 crisp .. 1 an out-of-focus bokeh disc.
// (x, y) in -1..1 of the quad. Returns alpha 0..1.
inline float flake_alpha(float x, float y, float seed, float blur) {
  const float r = sqrtf(x * x + y * y);
  const float ang = atan2f(y, x);
  const float lump = 1.0f + 0.12f * sinf(3.0f * ang + seed * 6.2832f) + 0.08f * sinf(5.0f * ang - seed * 10.7f) +
                     0.05f * sinf(8.0f * ang + seed * 14.4f) * (1.0f - blur);
  const float rr = r / (0.60f * lump);
  const float soft = 0.12f + 0.55f * blur;
  float a = 1.0f - sstep(1.0f - soft, 1.0f + 0.5f * soft, rr);
  a *= 0.72f + 0.28f * expf(-rr * rr * 2.2f);
  const float fe = (1.0f - blur) * 0.22f * sstep(0.35f, 0.95f, rr) * (0.5f + 0.5f * cosf(6.0f * ang + seed * 3.0f));
  a *= 1.0f - fe;
  a *= 1.0f - sstep(0.92f, 1.0f, r);
  return clamp01(a);
}

// ------------------------------------------------------------------------------------------------ the effects layer's noise
// xr_fx.cpp make_flame: the fire flipbook (RGBA8, its fire in R / G) keeps B and A for the snow layer (xr_fx.frag shape 180):
// B the snow's surface height (big soft drifts + wind ripples, equalized), A a fine grain. Both repeat across the whole
// texture. scratch: w * h floats. (~0.1 s for 256 x 512 on the PC; once, with the flipbook, at start-up.)
inline void fx_noise(int w, int h, uint8_t* rgba, float* scratch) {
  const int bx = 3, by = (3 * h + w / 2) / w > 0 ? (3 * h + w / 2) / w : 1;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
      const float f = pfbm(u, v, bx, by, 6, 0x5A0Au, 0.55f);
      const float r = pfbm(u, v, 2 * bx, 2 * by, 3, 0x5A0Bu, 0.5f);
      const float ridge = 1.0f - fabsf(2.0f * r - 1.0f);
      scratch[y * w + x] = 0.74f * f + 0.26f * ridge;
    }
  equalize(scratch, w * h);
  for (int i = 0; i < w * h; ++i) rgba[i * 4 + 2] = byte01(scratch[i]);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(w), v = (static_cast<float>(y) + 0.5f) / static_cast<float>(h);
      scratch[y * w + x] = pfbm(u, v, 24 * bx, 24 * by, 2, 0x5A0Cu, 0.5f);
    }
  equalize(scratch, w * h);
  for (int i = 0; i < w * h; ++i) rgba[i * 4 + 3] = byte01(scratch[i]);
}
// The layer's texels per metre (xr_fx.frag kSnowTexelsPerM: keep in step): your room / a furniture top 1 cm a texel, the
// life-size stadium 3 cm, the table round the Tabletop field 3 mm.
constexpr float kRoomTexelsPerM = 100.0f, kStadiumTexelsPerM = 33.0f, kTableTexelsPerM = 330.0f;

// ------------------------------------------------------------------------------------------------ the game's snow texture
// One RGBA8 picture (straight alpha, its mips below it; xr_weather.cpp hands it to GX as RGBA8_PC, clamped): 4 x 2 slots of
// 256 (a 240 tile + an 8-texel border copied round from its far side, so a tile's edge never shows at any mip it's used at):
//   row 0: the coverage stages 1..4 (the same surface thresholded deeper and deeper: patches grow from its high points,
//          then join into the blanket; stage 4 the deep blanket with its drift ripples) - nested, so drawing them over
//          each other grows the snow; the powder between the patches is in every stage
//   row 1: slot 0 the powder (stage 0: a first dusting), slot 1 trampled slush, then 64 x 64 sprites: 4 crisp flakes,
//          4 out-of-focus ones, a glint (a four-point star), a soft dot (the blown flake's head)
constexpr int kAtlasW = 1024, kAtlasH = 512, kSlot = 256, kBorder = 8, kTile = 240, kAtlasLevels = 4;
constexpr int kStages = 5;  // 0 the powder .. 4 the deep blanket
enum { kSlotPowder = 0, kSlotStage1 = 1, kSlotStage4 = 4, kSlotSlush = 5 };
enum { kSprFlake = 0, kSprBokeh = 4, kSprGlint = 8, kSprDot = 9 };
// the cells of the pitch one tile covers (each way): 6 cells = ~5.2 m, 2.1 cm a texel
constexpr int kTileCells = 6;
inline size_t atlas_bytes() {
  size_t b = 0;
  for (int l = 0; l < kAtlasLevels; ++l) b += static_cast<size_t>(kAtlasW >> l) * static_cast<size_t>(kAtlasH >> l) * 4;
  return b;
}
// a slot's tile (0 the powder, 1..4 the stages, 5 the slush): its content's top-left texel
inline void slot_origin(int slot, int* x, int* y) {
  if (slot >= 1 && slot <= 4) {
    *x = (slot - 1) * kSlot + kBorder;
    *y = kBorder;
  } else {
    *x = (slot == kSlotSlush ? kSlot : 0) + kBorder;
    *y = kSlot + kBorder;
  }
}
// uv of texel position (tx, ty) in the atlas
inline void atlas_uv(float tx, float ty, float* u, float* v) {
  *u = tx / static_cast<float>(kAtlasW);
  *v = ty / static_cast<float>(kAtlasH);
}
// a sprite's uv rect (k: kSprFlake + 0..3, kSprBokeh + 0..3, kSprGlint, kSprDot), half a texel in
inline void sprite_rect(int k, float* u0, float* v0, float* u1, float* v1) {
  const int cx = 2 * kSlot + (k % 8) * 64, cy = kSlot + (k / 8) * 64;
  atlas_uv(static_cast<float>(cx) + 1.5f, static_cast<float>(cy) + 1.5f, u0, v0);
  atlas_uv(static_cast<float>(cx) + 62.5f, static_cast<float>(cy) + 62.5f, u1, v1);
}

namespace detail {
struct RGBA {
  float r, g, b, a;
};
inline void put(uint8_t* img, int x, int y, const RGBA& c) {
  uint8_t* p = img + (static_cast<size_t>(y) * kAtlasW + static_cast<size_t>(x)) * 4;
  p[0] = byte01(c.r);
  p[1] = byte01(c.g);
  p[2] = byte01(c.b);
  p[3] = byte01(c.a);
}
// The surface (equalized) of one tile, and its slope lighting: the light from the far top-left of the tile, high up (so
// the side of a lump away from it goes the blue of a snow shadow, its top white).
inline void tile_surface(float* H, float* lam, float* grain, uint32_t seed, float relief) {
  for (int y = 0; y < kTile; ++y)
    for (int x = 0; x < kTile; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / kTile, v = (static_cast<float>(y) + 0.5f) / kTile;
      const float f = pfbm(u, v, 3, 3, 6, seed, 0.55f);
      const float r = pfbm(u, v, 5, 5, 3, seed + 7u, 0.5f);
      H[y * kTile + x] = 0.72f * f + 0.28f * (1.0f - fabsf(2.0f * r - 1.0f));
      grain[y * kTile + x] = pfbm(u, v, 40, 40, 2, seed + 13u, 0.5f);
    }
  equalize(H, kTile * kTile);
  const float L[3] = {-0.42f, -0.50f, 0.757f};  // (tile x, tile y, up), unit
  for (int y = 0; y < kTile; ++y)
    for (int x = 0; x < kTile; ++x) {
      const float gx = H[y * kTile + wrapi(x + 1, kTile)] - H[y * kTile + wrapi(x - 1, kTile)];
      const float gy = H[wrapi(y + 1, kTile) * kTile + x] - H[wrapi(y - 1, kTile) * kTile + x];
      float n[3] = {-gx * relief, -gy * relief, 1.0f};
      const float l = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      lam[y * kTile + x] = clamp01((n[0] * L[0] + n[1] * L[1] + n[2] * L[2]) / l);
    }
}
// snow's colour lit `lam` (0 its shadow side .. 1 facing the light), `edge` how thin it is there (0 deep .. 1 the edge)
inline RGBA snow_col(float lam, float edge, float grain, float a) {
  const float k = sstep(0.30f, 0.97f, lam);
  RGBA c;
  c.r = mixf(0.70f, 1.0f, k);
  c.g = mixf(0.77f, 1.0f, k);
  c.b = mixf(0.91f, 1.0f, k);
  // thin edges: a little translucent blue (the light goes through)
  c.r = mixf(c.r, 0.80f, 0.45f * edge);
  c.g = mixf(c.g, 0.86f, 0.45f * edge);
  c.b = mixf(c.b, 0.97f, 0.45f * edge);
  const float gk = 0.965f + 0.06f * grain;
  c.r = clamp01(c.r * gk);
  c.g = clamp01(c.g * gk);
  c.b = clamp01(c.b * gk);
  c.a = a;
  return c;
}
inline void put_tile(uint8_t* img, int slot, const RGBA* px) {
  int ox, oy;
  slot_origin(slot, &ox, &oy);
  for (int y = -kBorder; y < kTile + kBorder; ++y)
    for (int x = -kBorder; x < kTile + kBorder; ++x) put(img, ox + x, oy + y, px[wrapi(y, kTile) * kTile + wrapi(x, kTile)]);
}
}  // namespace detail

// The powder's alpha at a texel (the first dusting, in every stage too, between the patches)
inline float powder_alpha(float g1, float g2) { return 0.10f + 0.30f * sstep(0.30f, 0.92f, g1) * (0.6f + 0.4f * g2); }
// The coverage stages: what share of the ground stage k covers
constexpr float kStageCover[kStages] = {0.0f, 0.20f, 0.45f, 0.72f, 1.0f};

// Makes the game's snow texture, every mip (out: atlas_bytes()). scratch: 3 * kTile * kTile floats.
// (~0.2 s on the PC, wasm: on a thread at the first snow, never the game's frame.)
inline void game_atlas(uint8_t* out, float* scratch) {
  using detail::RGBA;
  memset(out, 0, atlas_bytes());
  // transparent texels carry snow's colour (their mips never go dark round the edges)
  for (int i = 0; i < kAtlasW * kAtlasH; ++i) {
    out[i * 4] = 240;
    out[i * 4 + 1] = 244;
    out[i * 4 + 2] = 252;
    out[i * 4 + 3] = 0;
  }
  float* H = scratch;
  float* lam = scratch + kTile * kTile;
  float* grain = scratch + 2 * kTile * kTile;
  static RGBA tile[kTile * kTile];  // (one tile at a time; made on one thread, once)
  // the stages: one surface, thresholded deeper each time (nested: what stage k covers, k+1 covers too)
  detail::tile_surface(H, lam, grain, 0x5A0Du, 7.0f);
  for (int k = 1; k <= 4; ++k) {
    const float thr = 1.0f - kStageCover[k];
    for (int y = 0; y < kTile; ++y)
      for (int x = 0; x < kTile; ++x) {
        const int i = y * kTile + x;
        const float u = (static_cast<float>(x) + 0.5f) / kTile, v = (static_cast<float>(y) + 0.5f) / kTile;
        const float g2 = pfbm(u, v, 9, 9, 2, 0x5A0Eu, 0.5f);
        float cov, edge, l = lam[i];
        if (k < 4) {
          cov = sstep(thr - 0.075f, thr + 0.045f, H[i]);
          edge = 1.0f - sstep(thr, thr + 0.14f, H[i]);
        } else {
          // the deep blanket: no gaps; wind ripples across it (low ridges along the tile's x) + the lumps lit stronger
          cov = 1.0f;  // (nested: never under stage 3 anywhere)
          edge = 0.0f;
          const float rip = 0.5f + 0.5f * sinf((v * 9.0f + 0.9f * H[i]) * 6.2831853f);
          l = clamp01(l * 0.85f + 0.15f * (0.55f + 0.45f * rip) + 0.05f);
        }
        const float pw = powder_alpha(grain[i], g2);
        const float a = cov + pw * (1.0f - cov);
        RGBA c = detail::snow_col(cov > 0.02f ? l : 0.85f, cov > 0.02f ? edge : 1.0f, grain[i], a);
        tile[i] = c;
      }
    detail::put_tile(out, k, tile);
  }
  // the powder alone (stage 0)
  for (int y = 0; y < kTile; ++y)
    for (int x = 0; x < kTile; ++x) {
      const int i = y * kTile + x;
      const float u = (static_cast<float>(x) + 0.5f) / kTile, v = (static_cast<float>(y) + 0.5f) / kTile;
      const float g2 = pfbm(u, v, 9, 9, 2, 0x5A0Eu, 0.5f);
      tile[i] = detail::snow_col(0.85f, 1.0f, grain[i], powder_alpha(grain[i], g2));
    }
  detail::put_tile(out, kSlotPowder, tile);
  // trampled slush: grey-blue mush with darker boot marks, wet; the grass shows through it a little
  for (int y = 0; y < kTile; ++y)
    for (int x = 0; x < kTile; ++x) {
      const int i = y * kTile + x;
      const float u = (static_cast<float>(x) + 0.5f) / kTile, v = (static_cast<float>(y) + 0.5f) / kTile;
      const float n1 = pfbm(u, v, 4, 4, 4, 0x5A10u, 0.55f), n2 = pfbm(u, v, 14, 14, 3, 0x5A11u, 0.5f);
      float boot = 0.0f;
      for (int m = 0; m < 22; ++m) {
        const float cx = h01(static_cast<uint32_t>(m), 1u, 0x5A12u) * kTile, cy = h01(static_cast<uint32_t>(m), 2u, 0x5A12u) * kTile;
        const float an = h01(static_cast<uint32_t>(m), 3u, 0x5A12u) * 6.2831853f;
        float dx = static_cast<float>(x) - cx, dy = static_cast<float>(y) - cy;
        dx -= kTile * floorf(dx / kTile + 0.5f);
        dy -= kTile * floorf(dy / kTile + 0.5f);
        const float a1 = (dx * cosf(an) + dy * sinf(an)) / 11.0f, a2 = (-dx * sinf(an) + dy * cosf(an)) / 5.0f;
        const float q = a1 * a1 + a2 * a2;
        boot = q < 1.0f ? (boot > 1.0f - q ? boot : 1.0f - q) : boot;
      }
      RGBA c;
      const float t = n1 * 0.8f + 0.2f * n2;
      c.r = mixf(0.42f, 0.70f, t) * (1.0f - 0.28f * boot);
      c.g = mixf(0.46f, 0.74f, t) * (1.0f - 0.26f * boot);
      c.b = mixf(0.50f, 0.80f, t) * (1.0f - 0.22f * boot);
      c.a = clamp01(0.46f + 0.36f * n2 + 0.18f * boot);
      tile[i] = c;
    }
  detail::put_tile(out, kSlotSlush, tile);
  // the sprites: 64 x 64 cells
  for (int k = 0; k < 10; ++k) {
    const int cx = 2 * kSlot + (k % 8) * 64, cy = kSlot + (k / 8) * 64;
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        const float px_ = (static_cast<float>(x) + 0.5f) / 32.0f - 1.0f, py_ = (static_cast<float>(y) + 0.5f) / 32.0f - 1.0f;
        RGBA c;
        c.r = 0.97f;
        c.g = 0.98f;
        c.b = 1.0f;
        float a;
        if (k < 8) {
          const float seed = 0.17f + 0.23f * static_cast<float>(k % 4);
          a = flake_alpha(px_, py_, seed, k < 4 ? 0.1f : 0.85f);
          // lit from above: its underside a touch blue
          const float sh = clamp01(0.5f + 0.5f * py_);
          c.r -= 0.10f * sh;
          c.g -= 0.07f * sh;
          c.b -= 0.01f * sh;
        } else if (k == kSprGlint) {
          const float ax = fabsf(px_), ay = fabsf(py_);
          a = expf(-ax * 12.0f) * expf(-py_ * py_ * 2.0f) + expf(-ay * 12.0f) * expf(-px_ * px_ * 2.0f);
          a = clamp01(a * 0.8f + expf(-(px_ * px_ + py_ * py_) * 18.0f));
        } else {
          const float r2 = px_ * px_ + py_ * py_;
          a = expf(-r2 * 4.5f) * (r2 < 1.0f ? 1.0f : 0.0f);
        }
        c.a = a;
        detail::put(out, cx + x, cy + y, c);
      }
  }
  // mips (straight alpha; the transparent texels already carry snow's colour)
  uint8_t* src = out;
  for (int l = 1; l < kAtlasLevels; ++l) {
    const int sw = kAtlasW >> (l - 1), sh = kAtlasH >> (l - 1), dw = sw / 2, dh = sh / 2;
    uint8_t* dst = src + static_cast<size_t>(sw) * sh * 4;
    for (int y = 0; y < dh; ++y)
      for (int x = 0; x < dw; ++x)
        for (int c = 0; c < 4; ++c) {
          const size_t s0 = (static_cast<size_t>(2 * y) * sw + 2 * x) * 4 + c, s1 = s0 + 4, s2 = s0 + static_cast<size_t>(sw) * 4, s3 = s2 + 4;
          dst[(static_cast<size_t>(y) * dw + x) * 4 + c] = static_cast<uint8_t>((src[s0] + src[s1] + src[s2] + src[s3] + 2) / 4);
        }
    src = dst;
  }
}

// ------------------------------------------------------------------------------------------------ the pitch's snow
// Game world: metres, Z up, the centre spot at the origin, goals along X (half-length 20.6), touchlines at Y = +-12.08.
// r214: 48 x 28 cells (0.86 m; r196's 32 x 18 were 1.3 m - tracks were 2.6 m smears). Drawn 2 x 2 cells a quad
// (24 x 14 quads of 1.7 m: the texture's own patches give the detail), the slush per cell.
constexpr int kPitchCols = 48, kPitchRows = 28, kPitchDraw = 2;
constexpr float kHalfX = 20.6f, kHalfY = 12.08f;
// One game frame of the snow lying on the pitch: piles up where nobody has played (faster at the boards, the corners and
// behind the goals; more on the side the wind blows toward), trampled where the players and a grounded ball go - the
// slush left there (s) fades and the snow fills back in. pos[k] = x, y, radius (m). true while anything lies.
inline bool pitch_step(float d[kPitchRows][kPitchCols], float s[kPitchRows][kPitchCols], const float (*pos)[3], int np, float dt,
                       bool flurry, float inten, float windX, float windY) {
  const float cw = 2.0f * kHalfX / kPitchCols, ch = 2.0f * kHalfY / kPitchRows;
  // a flurry only dusts and patches (stage ~2), a blizzard buries what nobody uses in ~75 s
  const float cap = flurry ? 0.45f : 1.0f;
  const float rate = dt * (flurry ? 0.35f : inten) / 75.0f;
  const float wl = sqrtf(windX * windX + windY * windY);
  const float wx = wl > 0.3f ? windX / wl : 0.0f, wy = wl > 0.3f ? windY / wl : 0.0f;
  const float wk = wl > 0.3f ? (wl / 6.0f > 1.0f ? 1.0f : wl / 6.0f) : 0.0f;
  bool any = false;
  for (int j = 0; j < kPitchRows; j++)
    for (int i = 0; i < kPitchCols; i++) {
      const float cx = -kHalfX + cw * (static_cast<float>(i) + 0.5f), cy = -kHalfY + ch * (static_cast<float>(j) + 0.5f);
      const float sx = cx / kHalfX, sy = cy / kHalfY;
      const float ex = fabsf(sx), ey = fabsf(sy);
      const float edge = ex > ey ? ex : ey;
      // behind / beside the goals (the goal mouths at the ends, |y| < 3.6 m): the net and the posts catch it
      const float goal = sstep(0.82f, 0.98f, ex) * (1.0f - sstep(2.6f, 4.2f, fabsf(cy)));
      // downwind boards
      float lee = (sx * wx + sy * wy) * edge;
      lee = lee > 0.0f ? lee * wk : 0.0f;
      const float bias = 0.45f + 0.9f * edge * edge + 0.5f * ex * ey + 0.45f * goal + 0.7f * lee;
      const float nz = 0.75f + 0.5f * h01(static_cast<uint32_t>(i), static_cast<uint32_t>(j), 0x5A0Fu);
      float& dd = d[j][i];
      float& ss = s[j][i];
      dd += rate * bias * nz;
      dd = dd > cap ? cap : dd;
      float trample = 0.0f;
      for (int k = 0; k < np; k++) {
        const float dx = pos[k][0] - cx, dy = pos[k][1] - cy;
        const float r = pos[k][2];
        const float q = (dx * dx + dy * dy) / (r * r);
        if (q < 1.0f) trample += 1.0f - q;
      }
      if (trample > 0.0f) {
        const float before = dd;
        dd -= dt * 2.6f * trample;
        dd = dd < 0.0f ? 0.0f : dd;
        ss += (before - dd) * 1.6f + dt * 0.4f * trample * (before > 0.05f ? 1.0f : 0.0f);
        ss = ss > 1.0f ? 1.0f : ss;
      }
      ss -= dt / 30.0f * (ss > 0.0f ? 1.0f : 0.0f);
      ss = ss < 0.0f ? 0.0f : (ss > dd + 0.6f ? dd + 0.6f : ss);
      any = any || dd > 0.02f || ss > 0.02f;
    }
  return any;
}
// The mean of the (up to 4) cells round grid vertex (vi, vj)
inline float pitch_vertex(const float g[kPitchRows][kPitchCols], int vi, int vj) {
  float s = 0.0f;
  int n = 0;
  for (int dj = -1; dj <= 0; dj++)
    for (int di = -1; di <= 0; di++) {
      const int i = vi + di, j = vj + dj;
      if (i < 0 || j < 0 || i >= kPitchCols || j >= kPitchRows) continue;
      s += g[j][i];
      n++;
    }
  return n > 0 ? s / static_cast<float>(n) : 0.0f;
}
// A depth's stage level (0 .. 5) and stage k's alpha at it: the powder first (full by depth 0.11), then stage k fades in
// between levels k and k + 1 (stage 1 from depth 0.18, the deep blanket full by 0.91) - drawn in order over the one below
// (full), they grow the snow and never flicker thin
inline float stage_level(float depth) { return clamp01(depth) * 5.5f; }
inline float stage_alpha(float level, int k) { return k == 0 ? clamp01(level / 0.6f) : clamp01(level - static_cast<float>(k)); }
// r196's 32 x 18 grid for the life-size stadium (xr_fx reads that size): the fine grid sampled at its cells' centres
inline void pitch_export32(const float d[kPitchRows][kPitchCols], float* out /* 32 * 18 */) {
  for (int j = 0; j < 18; ++j)
    for (int i = 0; i < 32; ++i) {
      const float fx = (static_cast<float>(i) + 0.5f) / 32.0f * kPitchCols - 0.5f, fy = (static_cast<float>(j) + 0.5f) / 18.0f * kPitchRows - 0.5f;
      int x0 = static_cast<int>(floorf(fx)), y0 = static_cast<int>(floorf(fy));
      const float ax = fx - static_cast<float>(x0), ay = fy - static_cast<float>(y0);
      int x1 = x0 + 1, y1 = y0 + 1;
      x0 = x0 < 0 ? 0 : x0;
      y0 = y0 < 0 ? 0 : y0;
      x1 = x1 >= kPitchCols ? kPitchCols - 1 : x1;
      y1 = y1 >= kPitchRows ? kPitchRows - 1 : y1;
      out[j * 32 + i] = (d[y0][x0] * (1.0f - ax) + d[y0][x1] * ax) * (1.0f - ay) + (d[y1][x0] * (1.0f - ax) + d[y1][x1] * ax) * ay;
    }
}

// ------------------------------------------------------------------------------------------------ your room's snow field
// Your floor in 25 cm cells (up to 12 x 12 m), in a frame of its own turned like your room (its longest wall / your play
// area), so walls run along cell edges: how deep the snow is (0 none .. 0.77 a full blanket .. 1 a deep drift) and how
// trampled (0..1: your footprints / where you walk), the distance to the nearest wall or piece of furniture (drifts pile
// against them, most on the side the wind blows toward), your footprints (head over the floor, a step every 34 cm),
// and the snow on your furniture tops (with your room scan: tables, the couch's seat, storage, a bed).
constexpr int kFieldMax = 48;
constexpr float kFieldCell = 0.25f;
constexpr int kPrints = 48, kTopsMax = 12, kTopTiles = 6;
struct Footprint {
  float x, z, yaw, a;
  uint8_t side;  // 0 left, 1 right
};
struct Top {
  float cx, cy, cz, ux, uz, hu, hv;  // LOCAL: centre (y = the top's height), unit axis u (xz), half sizes along u / across
  int nu, nv;                        // tiles along u / v
  float depth;
  float mask[(kTopTiles + 1) * (kTopTiles + 1)];  // per tile corner: the top really is there (1) or not (0: the couch's back)
  uint16_t labels;
};
struct Field {
  bool built = false;
  int nx = 0, ny = 0;
  float yaw = 0.0f;            // A = (cos yaw, sin yaw), B = (sin yaw, -cos yaw): xr_fx orient_flat's x / y
  float p0x = 0.0f, p0z = 0.0f;  // LOCAL: cell (0, 0)'s corner
  float floorY = 0.0f;
  uint8_t valid[kFieldMax * kFieldMax];
  float dist[kFieldMax * kFieldMax];  // to the nearest wall / furniture / edge (m)
  float gna[kFieldMax * kFieldMax], gnb[kFieldMax * kFieldMax];  // away from it (unit, field axes)
  float noise[kFieldMax * kFieldMax];
  float depth[kFieldMax * kFieldMax], comp[kFieldMax * kFieldMax];
  // r225: how much of the snow a cell takes (0..1): 1 on your floor, fading out over ~0.9 m with a noisy, wind-drifted
  // edge where the snow's ground ends without a wall (no scan: your play area's / the circle's edge; with one: an open
  // side of the room, the field's own edge in a room over 12 m) - r214's ended in a hard straight line there
  float amt[kFieldMax * kFieldMax];
  int clampA = 0, clampB = 0;  // r225: the field's grid cut a room longer than 12 m that way (its own edges soft there)
  Footprint prints[kPrints];
  int printN = 0, printNext = 0;
  float lastX = 0.0f, lastZ = 0.0f;
  bool haveLast = false;
  int side = 0;
  Top tops[kTopsMax];
  int topN = 0;
  int validN = 0;
};
inline void field_axes(const Field& f, float* ax, float* az, float* bx, float* bz) {
  *ax = cosf(f.yaw);
  *az = sinf(f.yaw);
  *bx = *az;
  *bz = -*ax;
}
// LOCAL (x, z) -> field (a, b) metres from p0
inline void field_ab(const Field& f, float x, float z, float* a, float* b) {
  float ax, az, bx, bz;
  field_axes(f, &ax, &az, &bx, &bz);
  const float dx = x - f.p0x, dz = z - f.p0z;
  *a = dx * ax + dz * az;
  *b = dx * bx + dz * bz;
}
inline void field_xz(const Field& f, float a, float b, float* x, float* z) {
  float ax, az, bx, bz;
  field_axes(f, &ax, &az, &bx, &bz);
  *x = f.p0x + a * ax + b * bx;
  *z = f.p0z + a * az + b * bz;
}
// r225: smooth value noise in world metres (cells of `scale` m), 0..1
inline float vnoise(float x, float z, float scale, uint32_t seed) {
  const float u = x / scale, v = z / scale;
  const float fu = floorf(u), fv = floorf(v);
  const uint32_t a = static_cast<uint32_t>(static_cast<int>(fu)), b = static_cast<uint32_t>(static_cast<int>(fv));
  float tu = u - fu, tv = v - fv;
  tu = tu * tu * (3.0f - 2.0f * tu);
  tv = tv * tv * (3.0f - 2.0f * tv);
  return mixf(mixf(h01(a, b, seed), h01(a + 1u, b, seed), tu), mixf(h01(a, b + 1u, seed), h01(a + 1u, b + 1u, seed), tu), tv);
}
// r225 (Lance 11-12: the blizzard's snow lay as "one hard-edged square" on his kitchen floor - r214's play-area field, cut
// at the Guardian rectangle's straight sides): the snow's SOFT EDGE where its ground ends without a wall. `out`: how far
// (m) past that edge (negative inside). 1 from 0.45 m inside .. 0 at 0.45 m past; the line itself wobbles +-0.32 m (a
// drift's tongues and bays, 1.1 m across) with a finer 0.37 m grain - and the snow's depth fading with it, the coverage
// (xr_fx.frag 180) breaks into patches and powder there instead of stopping on a line.
constexpr float kSoftIn = 0.45f, kSoftOut = 0.45f, kSoftWobble = 0.32f, kSoftGrain = 0.12f;
constexpr float kSoftMargin = kSoftOut + kSoftWobble + kSoftGrain;  // how far past the edge it can still show (0.89 m)
inline float soft_edge(float out, float x, float z) {
  const float w = out + kSoftWobble * (2.0f * vnoise(x, z, 1.1f, 0x5E01u) - 1.0f) + kSoftGrain * (2.0f * vnoise(x, z, 0.37f, 0x5E02u) - 1.0f);
  return 1.0f - sstep(-kSoftIn, kSoftOut, w);
}

// Builds the field: centre (cx, cz), turned `yaw`, half sizes (m); q.floor(x, z): open floor there (inside the walls, no
// furniture standing on it); r225 q.soft(x, z): how much of the snow it takes there (soft_edge: 1 on your floor, fading
// out where the ground ends without a wall). Q is anything with `bool floor(float, float) const` + `float soft(float,
// float) const`. A grid asked bigger than 12 m is cut there - its cut sides fade out too (no straight line, no drift).
template <class Q>
inline void field_build(Field& f, float cx, float cz, float yaw, float halfA, float halfB, float floorY, const Q& q) {
  f.built = true;
  f.yaw = yaw;
  f.floorY = floorY;
  int nx = static_cast<int>(ceilf(2.0f * halfA / kFieldCell)), ny = static_cast<int>(ceilf(2.0f * halfB / kFieldCell));
  f.clampA = nx > kFieldMax ? 1 : 0;
  f.clampB = ny > kFieldMax ? 1 : 0;
  nx = nx < 1 ? 1 : (nx > kFieldMax ? kFieldMax : nx);
  ny = ny < 1 ? 1 : (ny > kFieldMax ? kFieldMax : ny);
  f.nx = nx;
  f.ny = ny;
  float ax, az, bx, bz;
  field_axes(f, &ax, &az, &bx, &bz);
  const float ha = 0.5f * nx * kFieldCell, hb = 0.5f * ny * kFieldCell;
  f.p0x = cx - ha * ax - hb * bx;
  f.p0z = cz - ha * az - hb * bz;
  f.validN = 0;
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      float x, z;
      field_xz(f, (static_cast<float>(i) + 0.5f) * kFieldCell, (static_cast<float>(j) + 0.5f) * kFieldCell, &x, &z);
      const int k = j * kFieldMax + i;
      float am = 0.0f;
      if (q.floor(x, z)) {
        am = clamp01(q.soft(x, z));
        const int ei = i < nx - 1 - i ? i : nx - 1 - i, ej = j < ny - 1 - j ? j : ny - 1 - j;
        if (f.clampA) am *= soft_edge(kSoftMargin - (static_cast<float>(ei) + 0.5f) * kFieldCell, x, z);
        if (f.clampB) am *= soft_edge(kSoftMargin - (static_cast<float>(ej) + 0.5f) * kFieldCell, x, z);
      }
      f.amt[k] = am;
      f.valid[k] = am > 0.01f ? 1 : 0;
      f.validN += f.valid[k];
      f.depth[k] = 0.0f;
      f.comp[k] = 0.0f;
      f.noise[k] = 0.8f + 0.4f * h01(static_cast<uint32_t>(i), static_cast<uint32_t>(j), 0x5A20u);
    }
  // distance to the nearest closed cell / the grid's edge (two-pass chamfer, metres; r225: a cut side isn't an edge)
  const float big = 99.0f, c1 = kFieldCell, c2 = kFieldCell * 1.41421356f;
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      const int k = j * kFieldMax + i;
      float v = f.valid[k] ? big : 0.0f;
      float e = big;
      if (!f.clampA) {
        const float ea = (static_cast<float>(i < nx - 1 - i ? i : nx - 1 - i) + 0.5f) * kFieldCell;
        e = ea < e ? ea : e;
      }
      if (!f.clampB) {
        const float eb = (static_cast<float>(j < ny - 1 - j ? j : ny - 1 - j) + 0.5f) * kFieldCell;
        e = eb < e ? eb : e;
      }
      f.dist[k] = f.valid[k] ? (v < e ? v : e) : 0.0f;
    }
  for (int pass = 0; pass < 2; ++pass)
    for (int jj = 0; jj < ny; ++jj)
      for (int ii = 0; ii < nx; ++ii) {
        const int j = pass == 0 ? jj : ny - 1 - jj, i = pass == 0 ? ii : nx - 1 - ii;
        const int s = pass == 0 ? -1 : 1;
        const int k = j * kFieldMax + i;
        if (!f.valid[k]) continue;
        float v = f.dist[k];
        const int nb[4][2] = {{s, 0}, {0, s}, {s, s}, {-s, s}};
        for (int n = 0; n < 4; ++n) {
          const int i2 = i + nb[n][0], j2 = j + nb[n][1];
          if (i2 < 0 || j2 < 0 || i2 >= nx || j2 >= ny) continue;
          const float w = (nb[n][0] != 0 && nb[n][1] != 0) ? c2 : c1;
          const float o = f.dist[j2 * kFieldMax + i2] + w;
          v = o < v ? o : v;
        }
        f.dist[k] = v;
      }
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      const int k = j * kFieldMax + i;
      auto at = [&](int i2, int j2) {
        i2 = i2 < 0 ? 0 : (i2 >= nx ? nx - 1 : i2);
        j2 = j2 < 0 ? 0 : (j2 >= ny ? ny - 1 : j2);
        const float v = f.dist[j2 * kFieldMax + i2];
        return v > 1.5f ? 1.5f : v;
      };
      float ga = at(i + 1, j) - at(i - 1, j), gb = at(i, j + 1) - at(i, j - 1);
      const float l = sqrtf(ga * ga + gb * gb);
      f.gna[k] = l > 1e-4f ? ga / l : 0.0f;
      f.gnb[k] = l > 1e-4f ? gb / l : 0.0f;
    }
  f.printN = 0;
  f.printNext = 0;
  f.haveLast = false;
  f.topN = 0;
}
// A furniture top (LOCAL now): its tiles' corners kept where q.top(x, z) is within 5 cm of its height (the couch box's
// back and arms drop out: only its seat). Q has `float top(float x, float z) const`.
template <class Q>
inline void field_add_top(Field& f, float cx, float cz, float ux, float uz, float hu, float hv, float y, uint16_t labels, const Q& q) {
  if (f.topN >= kTopsMax) return;
  Top& t = f.tops[f.topN];
  t.cx = cx;
  t.cy = y;
  t.cz = cz;
  t.ux = ux;
  t.uz = uz;
  t.hu = hu;
  t.hv = hv;
  t.labels = labels;
  t.depth = 0.0f;
  int nu = static_cast<int>(2.0f * hu / 0.22f + 0.5f), nv = static_cast<int>(2.0f * hv / 0.22f + 0.5f);
  t.nu = nu < 1 ? 1 : (nu > kTopTiles ? kTopTiles : nu);
  t.nv = nv < 1 ? 1 : (nv > kTopTiles ? kTopTiles : nv);
  int kept = 0;
  for (int jv = 0; jv <= t.nv; ++jv)
    for (int iu = 0; iu <= t.nu; ++iu) {
      // (the corner pulled 3 cm in, so an edge corner looks at the top, not past it)
      const float su = (-1.0f + 2.0f * static_cast<float>(iu) / t.nu) * (hu - 0.03f), sv = (-1.0f + 2.0f * static_cast<float>(jv) / t.nv) * (hv - 0.03f);
      const float x = cx + ux * su - uz * sv, z = cz + uz * su + ux * sv;
      const float h = q.top(x, z);
      const float m = fabsf(h - y) < 0.05f ? 1.0f : 0.0f;
      t.mask[jv * (kTopTiles + 1) + iu] = m;
      kept += m > 0.5f ? 1 : 0;
    }
  if (kept >= 2) ++f.topN;
}
inline float field_depth_at(const Field& f, float x, float z) {
  float a, b;
  field_ab(f, x, z, &a, &b);
  const int i = static_cast<int>(floorf(a / kFieldCell)), j = static_cast<int>(floorf(b / kFieldCell));
  if (i < 0 || j < 0 || i >= f.nx || j >= f.ny) return 0.0f;
  const int k = j * kFieldMax + i;
  return f.valid[k] ? f.depth[k] : 0.0f;
}
// One frame. rate: how fast it snows (blizzard its strength x the match's fade; flurry 0.15; plain snow 0.5), cap the most
// it piles (flurry 0.5), windA / windB the wind in the field's axes (m/s); melt: the weather's gone (or your room's weather
// switched off): it all goes in 1.5 s. head: your head (LOCAL); a step is a footprint when you stand (head > 1 m over the
// floor) and there's snow to print in.
inline void field_step(Field& f, float dt, float rate, float cap, float windA, float windB, bool melt, float headX, float headY, float headZ) {
  if (!f.built) return;
  const int nx = f.nx, ny = f.ny;
  if (melt) {
    const float m = dt / 1.5f;
    for (int j = 0; j < ny; ++j)
      for (int i = 0; i < nx; ++i) {
        const int k = j * kFieldMax + i;
        f.depth[k] = f.depth[k] > m ? f.depth[k] - m : 0.0f;
        f.comp[k] = f.comp[k] > m ? f.comp[k] - m : 0.0f;
      }
    for (int t = 0; t < f.topN; ++t) f.tops[t].depth = f.tops[t].depth > m ? f.tops[t].depth - m : 0.0f;
    for (int p = 0; p < f.printN; ++p) f.prints[p].a = f.prints[p].a > m ? f.prints[p].a - m : 0.0f;
    f.haveLast = false;
    return;
  }
  const float wl = sqrtf(windA * windA + windB * windB);
  const float wa = wl > 0.2f ? windA / wl : 0.0f, wb = wl > 0.2f ? windB / wl : 0.0f;
  const float wk = wl > 0.2f ? (wl / 5.0f > 1.0f ? 1.0f : wl / 5.0f) : 0.0f;
  const float grow = dt * rate / 70.0f;  // the open floor: a full blanket (0.77) in ~54 s of full blizzard
  for (int j = 0; j < ny; ++j)
    for (int i = 0; i < nx; ++i) {
      const int k = j * kFieldMax + i;
      if (!f.valid[k]) continue;
      const float wall = expf(-f.dist[k] / 0.32f);
      // the wind piles it against what's downwind: blowing toward the wall / the furniture (against the "away" normal)
      float lee = -(f.gna[k] * wa + f.gnb[k] * wb);
      lee = lee > 0.0f ? lee * wk : 0.0f;
      const float bias = (1.0f + 1.6f * wall + 1.4f * wall * lee) * f.noise[k];
      const float am = f.amt[k];  // (r225: 1 on your floor; its soft edge takes less, and less deep)
      float d = f.depth[k] + grow * bias * am;
      const float c = cap * (0.82f + 0.18f * wall) * am;  // (the open floor a blanket, the drifts deeper)
      f.depth[k] = d > c ? (f.depth[k] > c ? f.depth[k] : c) : d;
      // footprints / your path fill back in with the snow
      const float c2 = f.comp[k] - grow * 1.8f;
      f.comp[k] = c2 > 0.0f ? c2 : 0.0f;
    }
  for (int t = 0; t < f.topN; ++t) {
    const float d = f.tops[t].depth + grow * 0.95f;
    f.tops[t].depth = d > cap ? cap : d;
  }
  for (int p = 0; p < f.printN; ++p) {
    const float a = f.prints[p].a - grow * 2.0f;  // (a print snowed over in ~35 s of blizzard)
    f.prints[p].a = a > 0.0f ? a : 0.0f;
  }
  // your steps
  if (headY - f.floorY < 1.0f) {
    f.haveLast = false;
    return;
  }
  if (!f.haveLast) {
    f.lastX = headX;
    f.lastZ = headZ;
    f.haveLast = true;
    return;
  }
  const float dx = headX - f.lastX, dz = headZ - f.lastZ;
  const float L = sqrtf(dx * dx + dz * dz);
  if (L > 1.5f) {  // (a recenter / a jump: start again from here)
    f.lastX = headX;
    f.lastZ = headZ;
    return;
  }
  if (L < 0.34f) return;
  const float ux = dx / L, uz = dz / L;
  const float sideK = f.side == 0 ? -1.0f : 1.0f;
  // (your right as you walk: forward u, right = (-uz, ux) in LOCAL x/z with Y up and -Z ahead)
  const float px = headX - ux * 0.05f + (-uz) * 0.095f * sideK, pz = headZ - uz * 0.05f + ux * 0.095f * sideK;
  f.lastX = headX;
  f.lastZ = headZ;
  const float here = field_depth_at(f, px, pz);
  if (here > 0.05f) {
    Footprint& fp = f.prints[f.printNext];
    fp.x = px;
    fp.z = pz;
    fp.yaw = atan2f(ux, -uz);
    fp.a = 1.0f;
    fp.side = static_cast<uint8_t>(f.side);
    f.printNext = (f.printNext + 1) % kPrints;
    f.printN = f.printN < kPrints ? f.printN + 1 : kPrints;
    // the snow under it pressed down (and your path's cells a little)
    float a, b;
    field_ab(f, px, pz, &a, &b);
    const int ci = static_cast<int>(floorf(a / kFieldCell)), cj = static_cast<int>(floorf(b / kFieldCell));
    for (int j = cj - 1; j <= cj + 1; ++j)
      for (int i = ci - 1; i <= ci + 1; ++i) {
        if (i < 0 || j < 0 || i >= nx || j >= ny) continue;
        const int k = j * kFieldMax + i;
        if (!f.valid[k]) continue;
        const float ca = (static_cast<float>(i) + 0.5f) * kFieldCell - a, cb = (static_cast<float>(j) + 0.5f) * kFieldCell - b;
        const float w = clamp01(1.0f - sqrtf(ca * ca + cb * cb) / 0.30f);
        const float c = 0.75f * w;
        f.comp[k] = f.comp[k] > c ? f.comp[k] : c;
        f.depth[k] *= 1.0f - 0.18f * w;
      }
  }
  f.side ^= 1;
}
// A vertex's depth / trample (vi 0..nx, vj 0..ny): the mean of the open cells round it, a little less at a wall's or a
// piece of furniture's foot (so the edge of the snow there is soft, not the cells' steps). -1: no open cell round it.
inline float field_vertex(const Field& f, const float* g, int vi, int vj) {
  float s = 0.0f;
  int n = 0, m = 0;
  for (int dj = -1; dj <= 0; ++dj)
    for (int di = -1; di <= 0; ++di) {
      const int i = vi + di, j = vj + dj;
      ++m;
      if (i < 0 || j < 0 || i >= f.nx || j >= f.ny) continue;
      const int k = j * kFieldMax + i;
      if (!f.valid[k]) continue;
      s += g[k];
      ++n;
    }
  if (n == 0) return -1.0f;
  return s / static_cast<float>(n) * (0.55f + 0.45f * static_cast<float>(n) / static_cast<float>(m));
}

// ---- where the field lies (xr_fx.cpp picks one: your room scan, else your play area, else a circle round you)
namespace detail {
struct ScanFloor {
  const xrroom::Room* r;
  float fy;
  bool floor(float x, float z) const { return xrroom::inside(*r, x, z) && xrroom::top_at(*r, x, z) < fy + 0.06f; }
  float top(float x, float z) const { return xrroom::top_at(*r, x, z); }
  // r225: 1 on your floor - but fading out toward an OPEN side of the room (Space setup's "invisible wall": the kitchen
  // open to the living room), where nothing stops the snow: no straight line across the floor there
  float soft(float x, float z) const {
    const float dx = x - r->ft[0], dz = z - r->ft[2];
    const float bx = r->fc * dx - r->fs * dz, bz = r->fs * dx + r->fc * dz;  // (LOCAL now -> the build frame)
    float best = 1e9f;
    for (int i = 0; i < r->wallN; ++i) {
      const xrroom::Wall& w = r->walls[i];
      if ((w.labels & xrroom::kLabInvisible) == 0) continue;
      float t = (bx - w.x0) * w.dx + (bz - w.z0) * w.dz;
      t = t < 0.0f ? 0.0f : (t > w.len ? w.len : t);
      const float px = w.x0 + w.dx * t - bx, pz = w.z0 + w.dz * t - bz;
      const float d = px * px + pz * pz;
      best = d < best ? d : best;
    }
    if (best > 1e8f) return 1.0f;
    return soft_edge(0.5f - sqrtf(best), x, z);
  }
};
// r225: the play area / the circle: the snow runs ~0.9 m past their edge, fading out with a noisy, drifted edge
struct RectFloor {
  float cx, cz, c, s, ha, hb;
  float out(float x, float z) const {  // signed distance past the rectangle (m)
    const float dx = x - cx, dz = z - cz;
    const float a = fabsf(dx * c + dz * s) - ha, b = fabsf(dx * s - dz * c) - hb;
    if (a > 0.0f || b > 0.0f) {
      const float pa = a > 0.0f ? a : 0.0f, pb = b > 0.0f ? b : 0.0f;
      return sqrtf(pa * pa + pb * pb);
    }
    return a > b ? a : b;
  }
  bool floor(float x, float z) const { return out(x, z) < kSoftMargin; }
  float soft(float x, float z) const { return soft_edge(out(x, z), x, z); }
};
struct DiscFloor {
  float cx, cz, r;
  float out(float x, float z) const { return sqrtf((x - cx) * (x - cx) + (z - cz) * (z - cz)) - r; }
  bool floor(float x, float z) const { return out(x, z) < kSoftMargin; }
  float soft(float x, float z) const { return soft_edge(out(x, z), x, z); }
};
}  // namespace detail
// With your room scan: the field turned like your longest wall, over your floor's outline; open floor = inside your walls
// with no furniture on it; the tops of your tables / couch seats / storage / bed (with the scan's mesh: the seat, not the
// couch box's top) collect their own snow. r225: a room longer than the field's 12 m: the field centred on you that way
// (headX / headZ, LOCAL), its cut sides soft.
inline void field_from_scan(Field& f, const xrroom::Room& rm, float headX, float headZ, bool haveHead) {
  const float c = rm.fc, s = rm.fs;
  float best = -1.0f, yaw = 0.0f;
  for (int i = 0; i < rm.wallN; ++i) {
    const xrroom::Wall& w = rm.walls[i];
    if (w.len > best) {
      best = w.len;
      const float dx = c * w.dx + s * w.dz, dz = -s * w.dx + c * w.dz;
      yaw = atan2f(dz, dx);
    }
  }
  const float ax = cosf(yaw), az = sinf(yaw), bx = az, bz = -ax;
  float a0 = 1e9f, a1 = -1e9f, b0 = 1e9f, b1 = -1e9f;
  for (int i = 0; i < rm.outlineN; ++i) {
    const float x = c * rm.outline[i][0] + s * rm.outline[i][1] + rm.ft[0];
    const float z = -s * rm.outline[i][0] + c * rm.outline[i][1] + rm.ft[2];
    const float a = x * ax + z * az, b = x * bx + z * bz;
    a0 = a < a0 ? a : a0;
    a1 = a > a1 ? a : a1;
    b0 = b < b0 ? b : b0;
    b1 = b > b1 ? b : b1;
  }
  if (rm.outlineN < 3) {
    a0 = b0 = -3.0f;
    a1 = b1 = 3.0f;
  }
  float ca = 0.5f * (a0 + a1), cb = 0.5f * (b0 + b1);
  const float span = 0.5f * static_cast<float>(kFieldMax) * kFieldCell;  // (6 m: half the field)
  if (haveHead) {
    const float ha = headX * ax + headZ * az, hb = headX * bx + headZ * bz;
    if (0.5f * (a1 - a0) + 0.05f > span) ca = ha < a0 + span ? a0 + span : (ha > a1 - span ? a1 - span : ha);
    if (0.5f * (b1 - b0) + 0.05f > span) cb = hb < b0 + span ? b0 + span : (hb > b1 - span ? b1 - span : hb);
  }
  const float cx = ca * ax + cb * bx, cz = ca * az + cb * bz;
  const float fy = xrroom::floor_y(rm);
  const detail::ScanFloor q{&rm, fy};
  field_build(f, cx, cz, yaw, 0.5f * (a1 - a0) + 0.05f, 0.5f * (b1 - b0) + 0.05f, fy, q);
  const uint16_t want = xrroom::kLabTable | xrroom::kLabCouch | xrroom::kLabStorage | xrroom::kLabBed | xrroom::kLabOther;
  const uint16_t never = xrroom::kLabScreen | xrroom::kLabLamp | xrroom::kLabPlant | xrroom::kLabDoor | xrroom::kLabWindow |
                         xrroom::kLabArt | xrroom::kLabWall | xrroom::kLabCeiling | xrroom::kLabFloor;
  for (int i = 0; i < rm.boxN && f.topN < kTopsMax; ++i) {
    const xrroom::Box& b = rm.boxes[i];
    if ((b.labels & want) == 0 || (b.labels & never) != 0) continue;
    if (b.hu < 0.12f || b.hv < 0.12f) continue;
    const float x = c * b.cx + s * b.cz + rm.ft[0], z = -s * b.cx + c * b.cz + rm.ft[2];
    const float ux = c * b.ux + s * b.uz, uz = -s * b.ux + c * b.uz;
    float y = xrroom::top_at(rm, x, z);
    if (y < fy - 1.0f) y = b.y1 + rm.ft[1];
    if (y - fy < 0.2f || y - fy > 1.4f) continue;
    field_add_top(f, x, z, ux, uz, b.hu, b.hv, y, b.labels, q);
  }
}
inline void field_from_scan(Field& f, const xrroom::Room& rm) { field_from_scan(f, rm, 0.0f, 0.0f, false); }
// No scan: your play area (the Guardian's rectangle on your floor). r225: no straight sides - the snow runs past the
// rectangle and fades out over ~0.9 m with a noisy, drifted edge (r214: drifts piled against its straight sides)
inline void field_from_play(Field& f, float cx, float cz, float yaw, float ha, float hb, float floorY) {
  const detail::RectFloor q{cx, cz, cosf(yaw), sinf(yaw), ha, hb};
  field_build(f, cx, cz, yaw, ha + kSoftMargin + 0.05f, hb + kSoftMargin + 0.05f, floorY, q);
}
// Neither: a circle round where you stood when it began (r225: its edge soft the same way)
inline void field_from_disc(Field& f, float cx, float cz, float r, float floorY) {
  const detail::DiscFloor q{cx, cz, r};
  field_build(f, cx, cz, 0.0f, r + kSoftMargin + 0.05f, r + kSoftMargin + 0.05f, floorY, q);
}
// r225: the snow's ground for the log - its area (m2, each cell by how much it takes), the open cells' box (LOCAL)
inline float field_area(const Field& f, float* x0, float* x1, float* z0, float* z1) {
  float a = 0.0f;
  *x0 = *z0 = 1e9f;
  *x1 = *z1 = -1e9f;
  for (int j = 0; j < f.ny; ++j)
    for (int i = 0; i < f.nx; ++i) {
      const int k = j * kFieldMax + i;
      if (!f.valid[k]) continue;
      a += f.amt[k] * kFieldCell * kFieldCell;
      float x, z;
      field_xz(f, (static_cast<float>(i) + 0.5f) * kFieldCell, (static_cast<float>(j) + 0.5f) * kFieldCell, &x, &z);
      *x0 = x < *x0 ? x : *x0;
      *x1 = x > *x1 ? x : *x1;
      *z0 = z < *z0 ? z : *z0;
      *z1 = z > *z1 ? z : *z1;
    }
  if (a <= 0.0f) *x0 = *x1 = *z0 = *z1 = 0.0f;
  return a;
}
// r225: the mean depth lying on the open floor (a new ground starts from it: walking into another scanned room mid-
// blizzard doesn't make the snow start again from bare floor)
inline float field_mean(const Field& f) {
  float s = 0.0f;
  int n = 0;
  for (int j = 0; j < f.ny; ++j)
    for (int i = 0; i < f.nx; ++i) {
      const int k = j * kFieldMax + i;
      if (!f.valid[k] || f.amt[k] < 0.99f) continue;
      s += f.depth[k];
      ++n;
    }
  return n > 0 ? s / static_cast<float>(n) : 0.0f;
}
inline void field_seed(Field& f, float mean, float cap) {
  if (mean <= 0.0f) return;
  for (int j = 0; j < f.ny; ++j)
    for (int i = 0; i < f.nx; ++i) {
      const int k = j * kFieldMax + i;
      if (!f.valid[k]) continue;
      const float wall = expf(-f.dist[k] / 0.32f);
      const float c = cap * (0.82f + 0.18f * wall) * f.amt[k];
      const float d = mean * f.noise[k] * (1.0f + 0.5f * wall) * f.amt[k];
      f.depth[k] = d < c ? d : c;
    }
  for (int t = 0; t < f.topN; ++t) f.tops[t].depth = mean * 0.95f < cap ? mean * 0.95f : cap;
}
// The tier's snowfall in your room (xr_weather.h tiers: 4 flurry, 5 blizzard, else plain snow): how fast it piles, the most
// it piles. strength = the blizzard's own (0.1..1), amount = the match's fade in / out.
inline void room_rate(int tier, float strength, float amount, float* rate, float* cap) {
  if (tier == 5) {
    *rate = strength * amount;
    *cap = 1.0f;
  } else if (tier == 4) {
    *rate = 0.15f * amount;
    *cap = 0.5f;
  } else {
    *rate = 0.5f * amount;
    *cap = 0.85f;
  }
}

}  // namespace xrsnow
