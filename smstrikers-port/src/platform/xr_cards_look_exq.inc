// strikers-quest r230 (Lance 11-12, C:\StrikersMR\EXQUISITE.md): LANCINI EXQUISITE's look - Lance's picks from the r229 mockups
// (logs\r229-exquisite-mockups): the NOIR card (cards_1: matte black stock, an arched spotlit window, engraved rosette line-work, foil by
// tier) and THE VAULT (packs_1: a black-leather collector's case; the soft-touch matte-black pack with one embossed gold-foil gem). Its kit
// (tools\outside\exquisite229 xkit.py: real-leaf foils, guilloche, emboss / deboss, paper / leather / velvet stocks, the jersey swatch, the
// LANCINI EXQUISITE mark, foil serials, the RC shield) and card1 / pack_front(1) / pack_back(1) ported line for line to C++: drawn at
// 1080 x 1512 (the mockups' 720 x 1008 x 1.5 - kX). Karla (xr_text karla_mask) for every word but the RC (Luckiest Guy, the game's).
// Included inside xr_cards.cpp's anonymous namespace after xr_cards_look(_ab).inc (its Card / Ref / Img / lk kit). Any thread.
// EMERALD / SAPPHIRE: their jewel stock carries foil 0.5 in the alpha (the shader's jewel chrome band, xr_fx_model.frag kJewel: a card
// whose front's normals are kJewelCode long) - the window, the character, the words stay printed / metal foil (1.0).
namespace lk {
namespace xq {

constexpr float kX = 1.5f;  // the mockups' px -> the shipping front's
constexpr float kMW = 720.0f, kMH = 1008.0f;
constexpr uint32_t kIvory = 0xEDE6D3;
constexpr float kJewelFoil = 0.5f;  // (the jewel stock's foil: 0.25 .. 0.75 is the shader's jewel band; metal foil 1)

// ---------------------------------------------------------------------------------------------- noise / grain
inline float hash01(int x, int y, uint32_t seed) {
  return pp_h01(seed * 0x9E3779B9u ^ (static_cast<uint32_t>(x) * 73856093u) ^ (static_cast<uint32_t>(y) * 19349663u));
}
// xkit noise(): value noise, `oct` octaves (each 2.2x finer, half as strong), 0..1 (coords in mockup px)
inline float vnoise(float x, float y, float scale, uint32_t seed, int oct) {
  float acc = 0.0f, amp = 1.0f, tot = 0.0f;
  for (int o = 0; o < oct; ++o) {
    acc += amp * pp_noise(x / scale, y / scale, seed + 977u * static_cast<uint32_t>(o));
    tot += amp;
    amp *= 0.5f;
    scale /= 2.2f;
  }
  return acc / tot;
}
// xkit fine(): a grain ~N(0, 1) per pixel, a touch soft (two hashes averaged across a pixel's neighbour)
inline float fine(float X, float Y, uint32_t seed) {
  const int x = static_cast<int>(X), y = static_cast<int>(Y);
  return 1.6f * ((hash01(x, y, seed) + hash01(x + 1, y, seed) + hash01(x, y + 1, seed)) / 3.0f - 0.5f) * 2.0f;
}

// ---------------------------------------------------------------------------------------------- foils (xkit FOILS / foil())
enum { kFGold = 0, kFChampagne, kFSilver, kFPlatinum, kFBrass };
inline F3 foil_ramp(int kind, float t) {
  switch (kind) {
    case kFChampagne: return ramp(t, {{0, 0x52473A}, {0.3f, 0x9A8767}, {0.55f, 0xCDBB95}, {0.8f, 0xEEE3C9}, {1, 0xFFFDF6}});
    case kFSilver: return ramp(t, {{0, 0x40454E}, {0.3f, 0x80878F}, {0.55f, 0xBFC5CD}, {0.8f, 0xE9ECF0}, {1, 0xFFFFFF}});
    case kFPlatinum: return ramp(t, {{0, 0x4D5866}, {0.3f, 0x96A3B3}, {0.58f, 0xD5DDE6}, {0.82f, 0xF4F7FA}, {1, 0xFFFFFF}});
    default: return ramp(t, {{0, 0x4E330A}, {0.28f, 0x936A1E}, {0.52f, 0xD2A94F}, {0.72f, 0xF1D88E}, {0.88f, 0xFFF1C8}, {1, 0xFFFFFF}});
  }
}
// real foil as it reads mid-tilt (sheen bands, one beam, grain); leaf: gold-leaf squares with crackle on their seams; holo: a faint rainbow.
// Paint coords: the canvas's px (mockup px x kX)
struct Foil {
  int kind = kFGold;
  float seed = 1.0f, holo = 0.0f, flat = 0.0f, cell = 44.0f;
  bool leaf = false;
  float ca = std::cos(-32.0f * kPi / 180.0f), sa = std::sin(-32.0f * kPi / 180.0f);
  F3 operator()(float X, float Y) const {
    const float x = X / kX, y = Y / kX;
    const float u = x * ca + y * sa, v = -x * sa + y * ca;
    const float n = vnoise(x, y, 140.0f, static_cast<uint32_t>(seed * 101.0f) + 5u, 2);
    const float b = (u - kMW * 0.42f - 90.0f * std::sin(seed)) / 120.0f;
    float lum = 0.50f + 0.22f * fsin(u / 190.0f + seed) + 0.12f * fsin(u / 61.0f + v / 340.0f + 2.0f * seed) + 0.30f * std::exp(-b * b) + 0.18f * (n - 0.5f);
    if (leaf) {
      const float lx = (x + 7.0f * fsin(y / 31.0f)) / cell, ly = (y + 5.0f * fsin(x / 27.0f)) / cell;
      const float cu = std::floor(lx), cvv = std::floor(ly);
      lum += (pp_h01(static_cast<uint32_t>(static_cast<int>(cu) * 92837111) ^ static_cast<uint32_t>(static_cast<int>(cvv) * 689287499) ^
                     static_cast<uint32_t>(seed * 7919.0f)) - 0.5f) * 0.09f;
      const float fu = lx - cu, fv = ly - cvv;
      const float seam = std::min(std::min(fu, 1.0f - fu), std::min(fv, 1.0f - fv)) * cell;
      const float crack = sat(1.2f - seam) * (vnoise(x, y, 30.0f, static_cast<uint32_t>(seed * 13.0f) + 9u, 1) > 0.45f ? 1.0f : 0.5f);
      lum -= crack * 0.13f;
    }
    lum = lum * (1.0f - flat) + 0.62f * flat;
    lum += fine(X, Y, static_cast<uint32_t>(seed * 31.0f) + 4u) * 0.025f;
    F3 p = foil_ramp(kind, sat(lum));
    if (holo > 0.0f) {
      const F3 rb = hsv((u + v * 0.4f) / 700.0f + n * 0.4f, 0.55f, 1.0f);
      p = p * (1.0f - holo) + rb * (holo * (0.6f + 0.5f * sat(lum)));
    }
    return p;
  }
};
inline Foil foil(int kind, float seed, bool leaf = false, float holo = 0.0f, float flat = 0.0f) {
  Foil f;
  f.kind = kind, f.seed = seed, f.leaf = leaf, f.holo = holo, f.flat = flat;
  return f;
}
// a stock: matte paper (tooth + mottle) in `base`
struct Paper {
  F3 b;
  uint32_t seed;
  float tooth, mottle;
  F3 operator()(float X, float Y) const {
    const float x = X / kX, y = Y / kX;
    const float d = fine(X, Y, seed) * tooth * 0.6f + (vnoise(x, y, 90.0f, seed + 1u, 2) - 0.5f) * mottle * 2.0f;
    return {std::max(0.0f, b.r + d), std::max(0.0f, b.g + d), std::max(0.0f, b.b + d)};
  }
};
inline Paper paper(uint32_t base, uint32_t seed, float tooth = 3.0f, float mottle = 1.6f) { return Paper{f3(base), seed, tooth, mottle}; }
// a radial gradient (mockup px centre / radius)
struct Radial {
  float cx, cy, r;
  std::vector<Stop> s;
  F3 operator()(float X, float Y) const {
    const float t = sat(std::hypot(X / kX - cx, Y / kX - cy) / r);
    if (t <= s.front().t) return f3(s.front().c);
    for (size_t i = 1; i < s.size(); ++i)
      if (t <= s[i].t) return mixf(f3(s[i - 1].c), f3(s[i].c), (t - s[i - 1].t) / std::max(1e-6f, s[i].t - s[i - 1].t));
    return f3(s.back().c);
  }
};

// ---------------------------------------------------------------------------------------------- masks in mockup px
inline M rr(const Cv& cv, float x0, float y0, float x1, float y1, float r) { return cv.rrect(x0 * kX, y0 * kX, x1 * kX, y1 * kX, r * kX); }
inline M ring(const Cv& cv, float x0, float y0, float x1, float y1, float r, float t) { return cv.ring(x0 * kX, y0 * kX, x1 * kX, y1 * kX, r * kX, t * kX); }
inline M ell(const Cv& cv, float x0, float y0, float x1, float y1) { return cv.ellipse(x0 * kX, y0 * kX, x1 * kX, y1 * kX); }
inline M poly(const Cv& cv, std::vector<float> xy) {
  for (float& v : xy) v *= kX;
  return cv.poly(xy);
}
// engraved guilloche (xkit rosette): families of wavy rings round (cx, cy), `width` px lines `spacing` apart, out to rmax (mockup px)
inline M rosette(const Cv& cv, const M& within, float cx, float cy, float spacing, int lobes, float amp, float width, int fams, float rmax) {
  M m = within;
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) {
      float& a = m.a[static_cast<size_t>(y) * m.w + x];
      if (a <= 0.0f) continue;
      const float X = (m.x0 + x + 0.5f) / kX - cx, Y = (m.y0 + y + 0.5f) / kX - cy;
      const float r = std::sqrt(X * X + Y * Y), th = std::atan2(Y, X);
      float cov = 0.0f;
      for (int i = 0; i < fams; ++i) {
        const float s = i % 2 == 0 ? 1.0f : -1.0f;
        const float val = (r + s * amp * fsin(lobes * th + i * kPi / lobes)) / spacing;
        const float d = std::fabs(fract(val + 0.5f) - 0.5f) * spacing;
        cov = std::max(cov, sat((width / 2.0f - d) * kX + 0.5f));
      }
      a *= cov * sat((rmax - r) / 12.0f);
    }
  (void)cv;
  return m;
}
// fine horizontal hatching (an engraved shade)
inline M hatch(const M& within, float spacing, float width) {
  M m = within;
  for (int y = 0; y < m.h; ++y) {
    const float Y = (m.y0 + y + 0.5f) / kX;
    const float d = std::fabs(fract(Y / spacing + 0.5f) - 0.5f) * spacing;
    const float cov = sat((width / 2.0f - d) * kX + 0.5f);
    for (int x = 0; x < m.w; ++x) m.a[static_cast<size_t>(y) * m.w + x] *= cov;
  }
  return m;
}
// lit from the top left: a raised (or stamped-in) shape's highlight + shade (xkit emboss)
inline void emboss(Cv& cv, const M& mask, float soft, float strength, bool raised = true) {
  const M h = cv.blur(mask, soft * kX);
  M hi = h, lo = h;
  float mx = 1e-6f;
  std::vector<float> s(h.a.size(), 0.0f);
  for (int y = 1; y + 1 < h.h; ++y)
    for (int x = 1; x + 1 < h.w; ++x) {
      const size_t i = static_cast<size_t>(y) * h.w + x;
      const float gx = 0.5f * (h.a[i + 1] - h.a[i - 1]), gy = 0.5f * (h.a[i + h.w] - h.a[i - h.w]);
      s[i] = (gx + gy) * (raised ? 1.0f : -1.0f);
      mx = std::max(mx, std::fabs(s[i]));
    }
  for (size_t i = 0; i < s.size(); ++i) {
    const float v = s[i] / mx;
    hi.a[i] = std::pow(sat(v), 0.8f);
    lo.a[i] = std::pow(sat(-v), 0.8f);
  }
  cv.fill(lo, 0x000000, strength);
  cv.fill(hi, 0xFFFFFF, strength * 0.8f);
}
// a recessed window: its rim's shadow falling in
inline void inner_shadow(Cv& cv, const M& mask, float px, float a, float dx = 3.0f, float dy = 5.0f) {
  const int g = static_cast<int>(std::ceil((px * 3.0f + std::max(std::fabs(dx), std::fabs(dy))) * kX)) + 2;
  M out = cv.box(mask.x0 - g, mask.y0 - g, mask.x0 + mask.w + g, mask.y0 + mask.h + g);
  for (int y = 0; y < out.h; ++y)
    for (int x = 0; x < out.w; ++x) out.a[static_cast<size_t>(y) * out.w + x] = 1.0f - mask.at(out.x0 + x, out.y0 + y);
  cv.fill(Cv::inter(cv.blur(cv.shift(out, dx * kX, dy * kX), px * kX), mask), 0x000000, a);
}

// ---------------------------------------------------------------------------------------------- Karla words
struct KT {
  M fill;
  float width = 0.0f;
};
// a word's mask: (x, y) its cap height's middle (mockup px), align 'c' 'l' 'r', `track` (xkit units), `weight` px of faux bold, `room` max width
inline KT kmask(const Cv& cv, const std::string& t, float x, float y, float px, char align = 'c', float track = 0.0f, float weight = 0.0f, float room = 0.0f) {
  KT out;
  if (t.empty()) return out;
  if (room > 0.0f) {
    const float w = xrtext::karla_width(t, px, track);
    if (w > room) px *= room / w;
  }
  const float P = px * kX;
  const xrtext::KarlaMask k = xrtext::karla_mask(t, P, track, static_cast<int>(std::ceil(weight * kX)) + 4);
  if (k.w <= 0) return out;
  out.width = k.width / kX;
  const float mid = 0.5f * (k.capTop + k.capBot);
  const float ax = align == 'c' ? k.pad + 0.5f * k.width : (align == 'l' ? static_cast<float>(k.pad) : k.pad + k.width);
  const int ox = static_cast<int>(std::lround(x * kX - ax)), oy = static_cast<int>(std::lround(y * kX - mid));
  M m = cv.box(ox, oy, ox + k.w, oy + k.h);
  for (int yy = 0; yy < m.h; ++yy)
    for (int xx = 0; xx < m.w; ++xx) m.a[static_cast<size_t>(yy) * m.w + xx] = k.a[static_cast<size_t>(m.y0 + yy - oy) * k.w + (m.x0 + xx - ox)];
  out.fill = weight > 0.05f ? cv.grow(m, 0.5f * weight * kX) : m;
  return out;
}
inline float kwidth(const std::string& t, float px, float track) { return xrtext::karla_width(t, px * kX, track) / kX; }
// a word painted: `paint` (a colour or a foil), its foil code, alpha; deboss: stamped into the stock
template <class P>
KT ktext(Cv& cv, const std::string& t, float x, float y, float px, P paint, float fo, char align = 'c', float track = 0.0f, float weight = 0.0f,
         float room = 0.0f, float alpha = 1.0f, float deboss = 0.0f, float shadow = 0.0f) {
  KT k = kmask(cv, t, x, y, px, align, track, weight, room);
  if (k.fill.empty()) return k;
  if (shadow > 0.0f) cv.fill(cv.blur(cv.shift(k.fill, shadow * 0.3f * kX, shadow * kX), shadow * 0.5f * kX), 0x000000, 0.5f);
  if (deboss > 0.0f) {
    cv.fill(Cv::minus(cv.shift(k.fill, -deboss * 0.6f * kX, -deboss * 0.8f * kX), k.fill), 0x000000, 0.45f);
    cv.fill(Cv::minus(cv.shift(k.fill, deboss * 0.5f * kX, deboss * 0.7f * kX), k.fill), 0xFFFFFF, 0.28f);
  }
  cv.fillp(k.fill, paint, alpha, fo);
  return k;
}
inline KT ktextc(Cv& cv, const std::string& t, float x, float y, float px, uint32_t col, char align = 'c', float track = 0.0f, float weight = 0.0f,
                 float room = 0.0f, float alpha = 1.0f) {
  const F3 c = f3(col);
  return ktext(cv, t, x, y, px, [&](float, float) { return c; }, 0.0f, align, track, weight, room, alpha);
}

// ---------------------------------------------------------------------------------------------- the marks
// the six-facet gem in a foil (each facet the foil x its light: xkit FACETS)
template <class P>
void xgem(Cv& cv, float cx, float cy, float r, const P& paint, float fo = 1.0f) {
  static const float kFacets[6] = {1.18f, 1.0f, 0.72f, 0.56f, 0.82f, 1.08f};
  for (int i = 0; i < 6; ++i) {
    const float a0 = -kPi / 2 + i * kPi / 3, a1 = a0 + kPi / 3;
    const float k = kFacets[i];
    cv.fillp(poly(cv, {cx, cy, cx + r * std::cos(a0), cy + r * std::sin(a0), cx + r * std::cos(a1), cy + r * std::sin(a1)}),
             [&](float x, float y) {
               const F3 p = paint(x, y);
               return F3{std::min(255.0f, p.r * k), std::min(255.0f, p.g * k), std::min(255.0f, p.b * k)};
             },
             1.0f, fo);
  }
}
// the LANCINI EXQUISITE mark: the foil gem, LANCINI tracked wide, EXQUISITE under it (stacked) / beside it
template <class P>
void mark(Cv& cv, float cx, float y, float k, const P& paint, uint32_t ink, bool stacked = true) {
  if (stacked) {
    xgem(cv, cx, y + 20 * k, 19 * k, paint);
    ktextc(cv, "LANCINI", cx, y + 56 * k, 13 * k, ink, 'c', 95, 0.25f * k, 0, 0.8f);
    ktext(cv, "EXQUISITE", cx, y + 84 * k, 27 * k, paint, 1.0f, 'c', 48, 0.7f * k);
  } else {
    xgem(cv, cx + 18 * k, y, 17 * k, paint);
    ktextc(cv, "LANCINI", cx + 48 * k, y - 11 * k, 11 * k, ink, 'l', 95, 0.2f * k, 0, 0.8f);
    ktext(cv, "EXQUISITE", cx + 48 * k, y + 10 * k, 20 * k, paint, 1.0f, 'l', 40, 0.55f * k);
  }
}
// the foil serial: its number big, " / run" smaller on the same line ("1" / "/1" for a 1/1), stamped in
template <class P>
float serial_stamp(Cv& cv, float cx, float y, int num, int run, float px, const P& paint) {
  char a[16], b[16];
  if (run <= 1) std::snprintf(a, sizeof(a), "1"), std::snprintf(b, sizeof(b), " / 1");
  else std::snprintf(a, sizeof(a), run >= 100 ? "%03d" : "%02d", num), std::snprintf(b, sizeof(b), " / %d", run);
  const float wa = kwidth(a, px, 4), wb = kwidth(b, px * 0.62f, 6), tw = wa + wb, x0 = cx - tw / 2;
  ktext(cv, a, x0, y, px, paint, 1.0f, 'l', 4, px * 0.035f, 0, 1.0f, 1.6f);
  ktext(cv, b, x0 + wa, y + px * 0.17f, px * 0.62f, paint, 1.0f, 'l', 6, px * 0.025f, 0, 1.0f, 1.6f * 0.7f);
  return tw;
}
inline std::vector<float> shield_pts(float cx, float cy, float w, float h) {
  const float top = cy - h / 2;
  std::vector<float> p = {cx - w / 2, top, cx - w * 0.2f, top, cx, top + h * 0.03f, cx + w * 0.2f, top, cx + w / 2, top};
  for (int i = 0; i <= 30; ++i) {
    const float t = i / 30.0f;
    p.push_back(cx + w / 2 * (1 - std::pow(t, 1.7f))), p.push_back(top + h * 0.36f + h * 0.64f * t);
  }
  for (int i = 29; i >= 0; --i) {
    const float t = i / 30.0f;
    p.push_back(cx - w / 2 * (1 - std::pow(t, 1.7f))), p.push_back(top + h * 0.36f + h * 0.64f * t);
  }
  return p;
}
// the RC shield (ORIGINALS rookies): foil shield, an inner field, a hairline, RC (Luckiest Guy), ROOKIE
template <class P>
void rc_shield(Cv& cv, float cx, float cy, float w, const P& paint, uint32_t field) {
  const float h = w * 1.18f;
  const M outer = poly(cv, shield_pts(cx, cy, w, h));
  cv.fill(cv.blur(cv.shift(outer, 2 * kX, 4 * kX), 4 * kX), 0x000000, 0.45f);
  cv.fillp(outer, paint, 1.0f, 1.0f);
  cv.fill(poly(cv, shield_pts(cx, cy + h * 0.02f, w * 0.84f, h * 0.84f)), field);
  cv.fillp(Cv::minus(poly(cv, shield_pts(cx, cy + h * 0.02f, w * 0.78f, h * 0.78f)), poly(cv, shield_pts(cx, cy + h * 0.02f, w * 0.75f, h * 0.75f))), paint, 0.9f, 1.0f);
  TM rc = text_masks(cv, "RC", cx * kX, (cy - h * 0.06f) * kX, w * 0.40f * kX, 'c', 0, 1, 0, 0, 0);
  cv.fillp(rc.fill, paint, 1.0f, 1.0f);
  ktext(cv, "ROOKIE", cx, cy + h * 0.23f, w * 0.105f, paint, 1.0f, 'c', 40, w * 0.004f);
  emboss(cv, outer, 1.5f, 0.35f);
}

// ---------------------------------------------------------------------------------------------- the jersey swatch
// a game-worn style swatch in the character's club colours (r229's clubs): a twill weave, a jersey stripe with piping, lumps (mockup px)
struct Fabric {
  float x0, y0, x1, y1, stripeAt;
  F3 c1, c2, pipe;
  uint32_t seed;
  F3 operator()(float X, float Y) const {
    const float x = X / kX, y = Y / kX;
    const float rib = 0.5f + 0.5f * fsin((x + y * 0.5f) / 3.2f * 6.2831853f);
    const float cross = 0.5f + 0.5f * fsin((x - y * 1.7f) / (3.2f * 0.9f) * 6.2831853f);
    const float weave = 0.62f * rib + 0.38f * cross;
    const float lump = vnoise(x, y, 26.0f, seed, 2), thread = vnoise(x, y, 2.2f, seed + 3u, 1);
    const float d = (x - x0) * 0.42f + (y - y0) - (y1 - y0) * stripeAt, sw = (y1 - y0) * 0.16f;
    const F3 col = std::fabs(d) < sw ? c2 : (std::fabs(d) < sw + 7.0f ? pipe : c1);
    const float sh = 0.70f + 0.30f * weave + 0.22f * (lump - 0.5f) + 0.10f * (thread - 0.5f);
    return col * sh;
  }
};
// dashed stitching round a box, `inset` in (mockup px)
inline M stitches(const Cv& cv, float x0, float y0, float x1, float y1, float inset, float dash = 9.0f, float gap = 6.0f, float w = 2.2f) {
  M m = rr(cv, x0, y0, x1, y1, 0);
  for (float& v : m.a) v = 0.0f;
  const float ax[5] = {x0 + inset, x1 - inset, x1 - inset, x0 + inset, x0 + inset}, ay[5] = {y0 + inset, y0 + inset, y1 - inset, y1 - inset, y0 + inset};
  for (int s = 0; s < 4; ++s) {
    const float L = std::hypot(ax[s + 1] - ax[s], ay[s + 1] - ay[s]);
    for (float t = 0.0f; t < L; t += dash + gap) {
      const float t2 = std::min(L, t + dash);
      const float sx = ax[s] + (ax[s + 1] - ax[s]) * t / L, sy = ay[s] + (ay[s + 1] - ay[s]) * t / L;
      const float ex = ax[s] + (ax[s + 1] - ax[s]) * t2 / L, ey = ay[s] + (ay[s + 1] - ay[s]) * t2 / L;
      const M d = rr(cv, std::min(sx, ex) - w / 2, std::min(sy, ey) - w / 2, std::max(sx, ex) + w / 2, std::max(sy, ey) + w / 2, w / 2);
      for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
          const int X = d.x0 + x, Y = d.y0 + y;
          if (X < m.x0 || Y < m.y0 || X >= m.x0 + m.w || Y >= m.y0 + m.h) continue;
          float& v = m.ref(X, Y);
          v = std::max(v, d.a[static_cast<size_t>(y) * d.w + x]);
        }
    }
  }
  return m;
}

// ---------------------------------------------------------------------------------------------- the autograph (foil, on its panel)
#if __has_include("xr_sign.h")
// its real autograph (xr_sign: the headset's own) as a foil-ink mask in a box (mockup px; 2 : 1 as xrsign draws it, fitted)
inline M sign_mask(Cv& cv, const Card& c, float x0, float y0, float x1, float y1) {
  const xrsign::Signature& s = xrsign::get(c.key, c.signAs);
  float bw = (x1 - x0) * kX, bh = (y1 - y0) * kX;
  if (bw > 2.0f * bh) bw = 2.0f * bh;
  else bh = 0.5f * bw;
  const int sw = std::max(8, static_cast<int>(bw)), sh = std::max(4, static_cast<int>(bh));
  std::vector<uint8_t> ink(static_cast<size_t>(sw) * sh * 4, 0);
  xrsign::draw(s, s.duration + 1.0f, ink.data(), sw, sh, sw * 4);
  const int ox = static_cast<int>(std::lround(0.5f * (x0 + x1) * kX - sw / 2.0f)), oy = static_cast<int>(std::lround(0.5f * (y0 + y1) * kX - sh / 2.0f));
  M m = cv.box(ox, oy, ox + sw, oy + sh);
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) m.a[static_cast<size_t>(y) * m.w + x] = ink[(static_cast<size_t>(m.y0 + y - oy) * sw + (m.x0 + x - ox)) * 4 + 3] / 255.0f;
  return cv.grow(m, 1.1f);  // (a touch bolder: foil ink reads at arm's length)
}
#endif

// ---------------------------------------------------------------------------------------------- the NOIR front
struct Look {
  uint32_t stock;
  int foil;
  bool band;
  float holo;
};
inline Look look_of(int t) {
  switch (t) {
    case xrexq::kSilverLeaf: return {0x111317, kFSilver, true, 0.0f};
    case xrexq::kGoldLeaf: return {0x120F0A, kFGold, true, 0.0f};
    case xrexq::kEmerald: return {0x07281B, kFGold, false, 0.0f};
    case xrexq::kSapphire: return {0x0A1A3C, kFPlatinum, false, 0.0f};
    case xrexq::kMasterpiece: return {0x050505, kFGold, true, 0.22f};
    case xrexq::kAuto: return {0x0E0E10, kFGold, false, 0.0f};
    case xrexq::kPatch: return {0x0E0E10, kFSilver, false, 0.0f};
    case xrexq::kRpa: return {0x0B0B0C, kFGold, true, 0.0f};
    case xrexq::kPatchAuto: return {0x0B0B0C, kFGold, true, 0.10f};
    default: return {0x0E0E10, kFChampagne, false, 0.0f};
  }
}
inline bool jewel(int t) { return t == xrexq::kEmerald || t == xrexq::kSapphire; }
// what the front needs besides the card (xr_cards x_front): its picture, its checklist number + count, its club colours, rookie
struct XIn {
  ImgP art;
  int no = 0, total = 150;
  uint32_t club1 = 0, club2 = 0;
  bool rookie = false;
};
#ifdef HARNESS_REAL_CARDS
struct XProbe {  // (the PC proof's never-covered checks: what each part of the last front covers)
  M figure, window, rc, name, serial, panel;
};
inline XProbe& x_probe() {
  static XProbe p;
  return p;
}
#endif
inline std::vector<uint8_t> x_front(const Card& c, const Ref& r, const XIn& in) {
  const int t = std::max(1, xrexq::tier_of(r.serial));
  const int run = xrexq::kRun[t], num = xrexq::num_of(r.serial);
  const Look L = look_of(t);
  const bool hit = xrexq::hit_tier(t);
  const float W = kMW, H = kMH;
  const uint32_t s = 7;
  Cv cv(kFrontW, kFrontH);
  LKP("x start");
  const M body = rr(cv, 0, 0, W, H, 22);
  cv.fillp(body, paper(L.stock, s, 3.0f, 1.6f), 1.0f, jewel(t) ? kJewelFoil : 0.0f);
  const Foil fo = foil(L.foil, static_cast<float>(s), false, L.holo), leaf = foil(L.foil, static_cast<float>(s + 3), true, L.holo);
  if (L.band) {
    const M band = Cv::minus(body, rr(cv, 17, 17, W - 17, H - 17, 9));
    cv.fillp(band, leaf, 1.0f, 1.0f);
    emboss(cv, band, 2.0f, 0.35f);
  }
  const M inner = rr(cv, 30, 30, W - 30, H - 30, 7);
  cv.fillp(rosette(cv, inner, W / 2, 440, 7.5f, 14, 9, 0.9f, 2, 560), fo, 0.075f, 1.0f);
  cv.fillp(ring(cv, 23, 23, W - 23, H - 23, 10, 2.4f), fo, 1.0f, 1.0f);
  cv.fillp(ring(cv, 31, 31, W - 31, H - 31, 6, 1.0f), fo, 0.75f, 1.0f);
  {
    const float cxy[4][2] = {{31, 31}, {W - 31, 31}, {W - 31, H - 31}, {31, H - 31}};
    for (const auto& p : cxy) {
      const float x = p[0], y = p[1];
      cv.fillp(poly(cv, {x, y - 9, x + 9, y, x, y + 9, x - 9, y}), fo, 1.0f, 1.0f);
    }
  }
  LKP("x stock");
  mark(cv, W / 2, 30, 1.0f, fo, kIvory);
  M rcm;
  if (in.rookie) {
    rc_shield(cv, W - 86, 92, 64, fo, L.stock);
    rcm = poly(cv, shield_pts(W - 86, 92, 64, 64 * 1.18f));
  }
  // the arched window, spotlit in the character's colour, engraved hatching, its rim's shadow; the character in it
  const float wy1 = hit ? 540.0f : 748.0f;
  const M win = cv.uni(rr(cv, 66, 250, W - 66, wy1, 0), ell(cv, 66, 160, W - 66, 340));
  Radial spot{W / 2, 330, 560, {{0, mix_rgb(c.c1, 0x000000, 0.42f)}, {0.5f, mix_rgb(c.c1, 0x000000, 0.78f)}, {1, mix_rgb(L.stock, 0x000000, 0.4f)}}};
  cv.fillp(win, spot, 1.0f, 0.0f);
  cv.fill(hatch(win, 4.6f, 0.8f), 0x000000, 0.16f);
  cv.glow((W / 2 - 230) * kX, (wy1 - 60) * kX, (W / 2 + 230) * kX, (wy1 + 50) * kX, mix_rgb(c.c1, 0xFFFFFF, 0.3f), 0.25f);
  inner_shadow(cv, win, 12, 0.55f);
  LKP("x window");
  M figM;
  if (in.art) {
    const Art a = art_of(in.art);
    if (a.ok() && a.photo) {  // (an old portrait, opaque: framed in the window's middle - drawn again once its big picture is in)
      const float cx = W / 2 * kX, cy = 0.5f * (250 + wy1) * kX, q = 0.32f * (W - 132) * kX, rH = q * std::min(1.0f, a.h() / a.w()), qW = q * std::min(1.0f, a.w() / a.h());
      cv.fill(cv.rrect(cx - qW - 10, cy - rH - 10, cx + qW + 10, cy + rH + 10, 8), 0xEDE6D3);
      paste(cv, *a.img, cx - qW, cy - rH, cx + qW, cy + rH);
    } else if (a.ok()) {
      const float bx0 = 100 * kX, by0 = 212 * kX, bx1 = (W - 100) * kX, by1 = (a.bust ? wy1 : wy1 - 14) * kX;
      Placed p = fit(a, bx0, by0, bx1, by1, 'b', 1.0f);
      if (!a.bust && p.y1 - p.y0 < 0.40f * (by1 - by0)) p = fit(a, bx0, by0, bx1, by1, 'c', 1.0f);  // (wide + flat: in the middle)
      Fig f;
      f.shadow = 0.42f, f.rim = mix_rgb(c.c1, 0xFFFFFF, 0.5f), f.rimA = 0.35f, f.clip = &win;
      figM = figure(cv, a, p, f);
    }
  }
  cv.fillp(Cv::minus(cv.grow(win, 2.6f * kX), win), fo, 1.0f, 1.0f);
  LKP("x figure");
  // the name block: a foil hairline with its diamond, the name stamped in foil, the series + number
  const float ry = wy1 + 22;
  {
    const float ab[2][2] = {{150, W / 2 - 22}, {W / 2 + 22, W - 150}};
    for (const auto& q : ab) cv.fillp(rr(cv, q[0], ry - 0.8f, q[1], ry + 0.8f, 0), fo, 0.85f, 1.0f);
  }
  cv.fillp(poly(cv, {W / 2, ry - 8, W / 2 + 8, ry, W / 2, ry + 8, W / 2 - 8, ry}), fo, 1.0f, 1.0f);
  const float ny = wy1 + (hit ? 60.0f : 64.0f);
  KT nm;
  if (c.sub.empty()) {
    nm = ktext(cv, upper(c.name), W / 2, ny, hit ? 56.0f : 64.0f, fo, 1.0f, 'c', 12, 1.7f, 590, 1.0f, 1.2f);
  } else {  // (a costume's own name, its hero under it)
    nm = ktext(cv, upper(c.name), W / 2, ny - 6, hit ? 46.0f : 52.0f, fo, 1.0f, 'c', 12, 1.5f, 590, 1.0f, 1.2f);
  }
  char line[96];
  std::snprintf(line, sizeof(line), "%s   \xC2\xB7   No. %03d", c.sub.empty() ? kSeriesName[c.series] : upper(c.sub).c_str(), in.no);
  ktextc(cv, line, W / 2, ny + 47, 17, kIvory, 'c', 55, 0.3f, 600, 0.7f);
  LKP("x name");
  M panel;
  if (hit) {
    const float py0 = 668, py1 = 862;
    const uint32_t c1 = in.club1 != 0 ? in.club1 : c.c1, c2 = in.club2 != 0 ? in.club2 : c.c2;
    auto sign_into = [&](float x0, float y0, float x1, float y1) {
#if __has_include("xr_sign.h")
      cv.fillp(sign_mask(cv, c, x0, y0, x1, y1), fo, 1.0f, 1.0f);
#else
      (void)x0, (void)y0, (void)x1, (void)y1;
#endif
    };
    if (t == xrexq::kAuto) {
      panel = rr(cv, 66, py0, W - 66, py1, 8);
      cv.fill(panel, mix_rgb(L.stock, 0xFFFFFF, 0.05f));
      emboss(cv, panel, 2.5f, 0.5f, false);
      sign_into(100, py0 + 14, W - 100, py1 - 30);
      ktextc(cv, "AUTHENTIC ON-CARD SIGNATURE", W / 2, py1 - 14, 10.5f, kIvory, 'c', 110, 0, 0, 0.5f);
    } else if (t == xrexq::kPatch) {
      panel = rr(cv, 205, py0, W - 205, py1, 6);
      cv.fillp(panel, Fabric{205, py0, W - 205, py1, 0.55f, f3(c1), f3(c2), f3(0xF4F1EA), 4u});
      cv.fill(Cv::inter(stitches(cv, 205, py0, W - 205, py1, 11), panel), mix_rgb(c1, 0xFFFFFF, 0.55f), 0.85f);
      inner_shadow(cv, panel, 9, 0.75f);
      cv.fillp(Cv::minus(cv.grow(panel, 2.6f * kX), panel), fo, 1.0f, 1.0f);
    } else {  // RPA, PATCH AUTO /10, MASTERPIECE: the patch + the autograph
      const M pw = rr(cv, 66, py0, 300, py1, 6);
      cv.fillp(pw, Fabric{66, py0, 300, py1, 0.45f, f3(c1), f3(c2), f3(0xF4F1EA), 5u});
      cv.fill(Cv::inter(stitches(cv, 66, py0, 300, py1, 10), pw), 0xF0F0F0, 0.8f);
      inner_shadow(cv, pw, 9, 0.75f);
      cv.fillp(Cv::minus(cv.grow(pw, 2.6f * kX), pw), fo, 1.0f, 1.0f);
      const M pan = rr(cv, 318, py0, W - 66, py1, 8);
      cv.fill(pan, mix_rgb(L.stock, 0xFFFFFF, 0.05f));
      emboss(cv, pan, 2.5f, 0.5f, false);
      sign_into(326, py0 + 16, W - 74, py1 - 34);
      const char* small = t == xrexq::kMasterpiece ? "ONE OF ONE  \xC2\xB7  SIGNED  \xC2\xB7  WORN"
                                                   : (in.rookie ? "ROOKIE  \xC2\xB7  SIGNED  \xC2\xB7  WORN" : "SIGNED  \xC2\xB7  WORN");
      ktextc(cv, small, (318 + W - 66) / 2, py1 - 14, 10, kIvory, 'c', 90, 0, 300, 0.5f);
      panel = cv.uni(pw, pan);
    }
  }
  LKP("x panel");
  // the serial + the tier
  serial_stamp(cv, W / 2, 918, num, run, 50, fo);
  std::string lab = xrexq::name(t, in.rookie);
  if (t == xrexq::kAuto) lab = "ON-CARD AUTOGRAPH";
  if (t == xrexq::kPatch) lab = "GRAND THREADS";
  if (in.rookie && !hit) lab += "   \xC2\xB7   ROOKIE";
  ktext(cv, lab, W / 2, 964, 15, fo, 1.0f, 'c', 95, 0.4f, 600);
  // the grain over everything
  for (size_t i = 0; i < static_cast<size_t>(cv.W) * cv.H; ++i) {
    if (cv.c[i * 4 + 3] <= 0.0f) continue;
    const float g = (hash01(static_cast<int>(i % cv.W), static_cast<int>(i / cv.W), 77u) - 0.5f) * 4.0f;
    for (int k = 0; k < 3; ++k) cv.c[i * 4 + k] = std::max(0.0f, std::min(255.0f, cv.c[i * 4 + k] + g));
  }
#ifdef HARNESS_REAL_CARDS
  x_probe().figure = figM, x_probe().window = win, x_probe().rc = rcm, x_probe().name = nm.fill, x_probe().panel = panel;
#endif
  (void)rcm, (void)nm;
  LKP("x grain");
  std::vector<uint8_t> out = bytes(cv, true);
  LKP("x bytes");
  return out;
}

// ---------------------------------------------------------------------------------------------- the back
inline Cv x_back_cv() {
  const float W = kMW, H = kMH;
  Cv cv(kFrontW, kFrontH);
  const M body = rr(cv, 0, 0, W, H, 22);
  cv.fillp(body, paper(0x0C0C0E, 11, 3.0f, 1.6f));
  const Foil fo = foil(kFGold, 5.0f);
  cv.fillp(rosette(cv, rr(cv, 30, 30, W - 30, H - 30, 7), W / 2, H / 2, 7.5f, 16, 9, 0.9f, 2, 470), fo, 0.09f, 1.0f);
  cv.fillp(ring(cv, 23, 23, W - 23, H - 23, 10, 2.4f), fo, 1.0f, 1.0f);
  cv.fillp(ring(cv, 31, 31, W - 31, H - 31, 6, 1.0f), fo, 0.75f, 1.0f);
  const M rg = Cv::minus(ell(cv, W / 2 - 150, 220, W / 2 + 150, 520), ell(cv, W / 2 - 141, 229, W / 2 + 141, 511));
  cv.fillp(rg, fo, 1.0f, 1.0f);
  xgem(cv, W / 2, 370, 104, fo);
  emboss(cv, poly(cv, [&] {
           std::vector<float> p;
           for (int i = 0; i < 6; ++i) p.push_back(W / 2 + 104 * std::cos(-kPi / 2 + i * kPi / 3)), p.push_back(370 + 104 * std::sin(-kPi / 2 + i * kPi / 3));
           return p;
         }()),
         3.0f, 0.5f);
  ktextc(cv, "LANCINI", W / 2, 600, 26, kIvory, 'c', 80, 0.45f, 0, 0.85f);
  ktext(cv, "EXQUISITE", W / 2, 662, 66, fo, 1.0f, 'c', 26, 1.7f, 0, 1.0f, 1.3f);
  cv.fillp(rr(cv, W / 2 - 130, 708, W / 2 + 130, 709.5f, 0), fo, 1.0f, 1.0f);
  ktextc(cv, "2026-27  STRIKERS", W / 2, 744, 20, kIvory, 'c', 55, 0.3f, 0, 0.8f);
  ktext(cv, "EVERY CARD SERIAL NUMBERED", W / 2, 880, 17, fo, 1.0f, 'c', 50, 0.4f);
  ktextc(cv, "ONE HIT IN EVERY PACK", W / 2, 914, 15, kIvory, 'c', 50, 0.3f, 0, 0.7f);
  return cv;
}

// ---------------------------------------------------------------------------------------------- the pack (900 x 1380 = 600 x 920 x 1.5)
constexpr float kPW6 = 600.0f, kPH6 = 920.0f;
inline M wrapper_mask(const Cv& cv, float teeth = 15.0f, float depth = 9.0f) {
  std::vector<float> p;
  const int n = static_cast<int>(kPW6 / teeth);
  for (int i = 0; i <= n; ++i) p.push_back(i * teeth), p.push_back(i % 2 == 0 ? 0.0f : depth);
  for (int i = n; i >= 0; --i) p.push_back(i * teeth), p.push_back(kPH6 - (i % 2 == 0 ? 0.0f : depth));
  return poly(cv, p);
}
// the crimped seals (ridged), the pillow's dark edges, a soft sheen, faint crinkles
inline void wrapper_finish(Cv& cv, const M& body, float sheen, float crinkle, uint32_t seed, float band = 38.0f) {
  const M crimp = Cv::inter(cv.uni(rr(cv, 0, 0, kPW6, band, 0), rr(cv, 0, kPH6 - band, kPW6, kPH6, 0)), body);
  cv.fill(hatch(crimp, 3.2f, 1.3f), 0x000000, 0.35f);
  cv.fill(hatch(cv.shift(crimp, 0, 1 * kX), 3.2f, 0.7f), 0xFFFFFF, 0.12f);
  cv.fill(Cv::inter(rr(cv, 0, band - 2, kPW6, band, 0), body), 0x000000, 0.35f);
  cv.fill(Cv::inter(rr(cv, 0, kPH6 - band, kPW6, kPH6 - band + 2, 0), body), 0x000000, 0.35f);
  M edge = body;
  for (int y = 0; y < edge.h; ++y)
    for (int x = 0; x < edge.w; ++x) {
      const float X = (edge.x0 + x + 0.5f) / kX;
      edge.a[static_cast<size_t>(y) * edge.w + x] *= std::pow(sat(1.0f - std::min(X, kPW6 - X) / 70.0f), 1.5f);
    }
  cv.fill(edge, 0x000000, 0.45f);
  cv.glow(kPW6 * 0.15f * kX, kPH6 * 0.1f * kX, kPW6 * 0.85f * kX, kPH6 * 0.9f * kX, 0xFFFFFF, sheen);
  M hi = body, lo = body;
  const float ca = std::cos(-70.0f * kPi / 180.0f), sa = std::sin(-70.0f * kPi / 180.0f);
  for (int y = 0; y < body.h; ++y)
    for (int x = 0; x < body.w; ++x) {
      const float X = (body.x0 + x + 0.5f) / kX, Y = (body.y0 + y + 0.5f) / kX;
      const float u = X * ca + Y * sa, v = -X * sa + Y * ca, n = vnoise(X, Y, 50.0f, seed, 2);
      const float cr = fsin(u / 23.0f + n * 3.5f) * fsin(v / 140.0f + n * 2.0f);
      const size_t i = static_cast<size_t>(y) * body.w + x;
      hi.a[i] *= sat(cr - 0.55f) / 0.45f;
      lo.a[i] *= sat(-cr - 0.6f) / 0.4f;
    }
  cv.add(cv.blur(hi, 2 * kX), 0xFFFFFF, crinkle);
  cv.fill(cv.blur(lo, 2 * kX), 0x000000, crinkle * 1.4f);
}
inline Cv x_pack_cv() {
  Cv cv(kPackW, kPackH);
  const M body = wrapper_mask(cv);
  cv.fillp(body, paper(0x0D0D0F, 3, 2.5f, 1.5f), 1.0f, 0.0f);
  const Foil fo = foil(kFGold, 5.0f);
  cv.fillp(rosette(cv, body, kPW6 / 2, 330, 7, 16, 8, 0.9f, 2, 300), fo, 0.09f, 1.0f);
  cv.fillp(ring(cv, 34, 62, kPW6 - 34, kPH6 - 62, 6, 2.0f), fo, 1.0f, 1.0f);
  cv.fillp(ring(cv, 42, 70, kPW6 - 42, kPH6 - 70, 4, 0.9f), fo, 0.7f, 1.0f);
  cv.fillp(Cv::minus(ell(cv, kPW6 / 2 - 132, 198, kPW6 / 2 + 132, 462), ell(cv, kPW6 / 2 - 124, 206, kPW6 / 2 + 124, 454)), fo, 1.0f, 1.0f);
  xgem(cv, kPW6 / 2, 330, 92, fo);
  {
    std::vector<float> p;
    for (int i = 0; i < 6; ++i) p.push_back(kPW6 / 2 + 92 * std::cos(-kPi / 2 + i * kPi / 3)), p.push_back(330 + 92 * std::sin(-kPi / 2 + i * kPi / 3));
    emboss(cv, poly(cv, p), 3.0f, 0.5f);
  }
  ktextc(cv, "LANCINI", kPW6 / 2, 560, 22, kIvory, 'c', 70, 0.4f, 0, 0.85f);
  ktext(cv, "EXQUISITE", kPW6 / 2, 612, 58, fo, 1.0f, 'c', 26, 1.6f, 0, 1.0f, 1.2f);
  cv.fillp(rr(cv, kPW6 / 2 - 120, 652, kPW6 / 2 + 120, 653.5f, 0), fo, 1.0f, 1.0f);
  ktextc(cv, "2026-27  STRIKERS", kPW6 / 2, 688, 19, kIvory, 'c', 55, 0.3f, 0, 0.8f);
  ktext(cv, "EVERY CARD SERIAL NUMBERED", kPW6 / 2, 790, 15, fo, 1.0f, 'c', 50, 0.35f);
  ktextc(cv, "3 CARDS  |  1 HIT", kPW6 / 2, 820, 15, kIvory, 'c', 50, 0.3f, 0, 0.7f);
  wrapper_finish(cv, body, 0.05f, 0.02f, 2u);
  return cv;
}
// the back: the ladder, the hits, the exact odds (r211's ODDS page, xrexq::kOddsRows - what the proof's 1,000,000 packs measured)
inline Cv x_pack_back_cv() {
  Cv cv(kPackW, kPackH);
  const M body = wrapper_mask(cv);
  cv.fillp(body, paper(0x0D0D0F, 13, 2.5f, 1.5f), 1.0f, 0.0f);
  const Foil fo = foil(kFGold, 5.0f);
  xgem(cv, kPW6 / 2, 96, 22, fo);
  ktext(cv, "EVERY CARD SERIAL NUMBERED", kPW6 / 2, 146, 17, fo, 1.0f, 'c', 40, 0.5f);
  ktextc(cv, "3 CARDS A PACK  -  2 NUMBERED  +  1 HIT", kPW6 / 2, 174, 12, kIvory, 'c', 40, 0.25f, 500, 0.75f);
  ktext(cv, "ODDS", kPW6 / 2, 214, 20, fo, 1.0f, 'c', 120, 0.5f);
  float y = 252;
  for (int i = 0; i < xrexq::kOddsRowsN; ++i) {
    const bool hitRow = i >= 5;
    if (i == 5) {
      cv.fillp(rr(cv, kPW6 / 2 - 120, y - 12, kPW6 / 2 + 120, y - 10.6f, 0), fo, 1.0f, 1.0f);
      ktext(cv, "ONE HIT IN EVERY PACK", kPW6 / 2, y + 12, 15, fo, 1.0f, 'c', 40, 0.5f);
      y += 42;
    }
    ktextc(cv, xrexq::kOddsRows[i].what, 92, y, 15, i == xrexq::kOddsRowsN - 1 ? 0xF1D88E : kIvory, 'l', 30, 0.35f, 260);
    ktext(cv, xrexq::kOddsRows[i].odds, kPW6 - 92, y, 15, fo, 1.0f, 'r', 30, 0.35f, 200);
    cv.fill(rr(cv, 92, y + 15, kPW6 - 92, y + 15.8f, 0), 0x3A3426, 0.6f);
    y += hitRow ? 40.0f : 38.0f;
  }
  ktextc(cv, "ORIGINALS ROOKIES CARRY THE RC SHIELD", kPW6 / 2, y + 10, 12, kIvory, 'c', 35, 0, 0, 0.7f);
  ktextc(cv, "A SOLD-OUT RUN GOES TO THE NEXT TIER DOWN", kPW6 / 2, y + 34, 11, kIvory, 'c', 30, 0, 0, 0.55f);
  ktextc(cv, "1 PACK  |  3 CARDS  |  1,000 BIT COINS", kPW6 / 2, 800, 15, kIvory, 'c', 40, 0.3f, 0, 0.8f);
  ktext(cv, "LANCINI EXQUISITE  |  2026-27 STRIKERS", kPW6 / 2, 830, 13, fo, 1.0f, 'c', 50, 0.3f);
  wrapper_finish(cv, body, 0.06f, 0.025f, 9u);
  return cv;
}

}  // namespace xq
}  // namespace lk
