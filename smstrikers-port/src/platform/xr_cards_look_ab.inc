// strikers-quest r217: the two sets' pictures - A "PRIZM CHROME" (LANCINI TIZM) and B "TIZM JAM '96" (LANCINI TIZM JAM '96): fronts
// (every finish, shiny, level, the specials' windows, TIZM BOOM, HOMETOWN, SIGNED / every BLACK 1/1's certified auto), backs, the
// packs, the boxes (the shop's pictures), the slab labels. Their mockups: logs\r216-card-mockups\A.png / B.png (tools\outside\
// cardlook216\dir_a.py / dir_b.py, line for line). Included after xr_cards_look.inc (lk::). Any thread.
namespace lk {

constexpr uint32_t kNavyA = 0x0B1022, kGoldA = 0xFFD23F;
constexpr uint32_t kInk = 0x0C0814, kYel = 0xFFD400, kMag = 0xE0218A, kTeal = 0x18C6C0, kPurp = 0x2B1055, kOrng = 0xFF7A1A;
constexpr uint32_t kRarRgb[3] = {0x3A4A70, 0xC8961E, 0x8A3CE0};
constexpr int kCrest = 120;  // B: how far its shoulders sit below the crest's top (front px)

inline int fk_of(int par) { return par == kSilver ? kSilverF : (par == kGoldTizm ? kGoldF : kBlackF); }
inline std::string stamp_of(const Ref& r) { return r.parallel == kBlack ? "1/1" : (r.serial < 10 ? "0" : "") + std::to_string(r.serial) + "/10"; }
// a gradient whose ends are the picture's corners along `deg` (kit.lin with no t0 / t1)
inline Lin lin_full(std::initializer_list<Stop> s, float deg, float W, float H) {
  const float a = deg * kPi / 180.0f;
  float lo = 1e9f, hi = -1e9f;
  for (float x : {0.0f, W})
    for (float y : {0.0f, H}) lo = std::min(lo, x * std::cos(a) + y * std::sin(a)), hi = std::max(hi, x * std::cos(a) + y * std::sin(a));
  return Lin(s, deg, lo, hi);
}

// ================================================================================================= B: the die-cut crest
// the card's outline: a rounded card whose top rises into a central crest (smooth shoulders) - its polygon, inset by `in` (front px of
// a w x h card), clockwise from the top left. Also the 3D card's edge (crest_outline, below)
inline std::vector<float> crest_pts(float in, float w, float h, float crest = static_cast<float>(kCrest), float r = 48.0f) {
  std::vector<float> p;
  for (int i = 0; i <= 120; ++i) {
    const float x = i / 120.0f * w, u = (x - w / 2) / (w * 0.30f);
    const float y = crest * (1.0f - std::exp(-std::pow(u, 6.0f))) + in;
    p.push_back(std::min(std::max(x, in), w - in));
    p.push_back(y);
  }
  for (float v : {w - in, h - r, w - in, h - in, in, h - in, in, h - r}) p.push_back(v);
  return p;
}
// its coverage (dir_b crest_shape): the polygon, cut to a rounded box, the shoulders' outer corners rounded
inline M crest(const Cv& cv, float in, float w = kCW, float h = kCH, float crestH = static_cast<float>(kCrest), float r = 48.0f) {
  const M m = Cv::inter(cv.poly(crest_pts(in, w, h, crestH, r)), cv.rrect(in, in, w - in, h - in, std::max(0.0f, r - in)));
  return cv.uni(Cv::inter(m, cv.rrect(in, crestH + in - 60, w - in, h - in, std::max(8.0f, r - in))), Cv::inter(m, cv.rrect(w * 0.08f, in, w * 0.92f, h - in, r)));
}
// the card's three crests (its edge, its frame, its field: inset 0 / 14 / 30), made once and kept as bytes (~1.6 MB each)
inline M crest_cached(const Cv& cv, int which) {
  static std::mutex mu;
  static std::vector<uint8_t> keep[3];
  static int kx0[3], ky0[3], kw[3], kh[3];
  static const float kIn[3] = {0.0f, 14.0f, 30.0f};
  M m;
  {
    std::lock_guard<std::mutex> lock(mu);
    if (keep[which].empty()) {
      const M c = crest(cv, kIn[which]);
      kx0[which] = c.x0, ky0[which] = c.y0, kw[which] = c.w, kh[which] = c.h;
      keep[which].resize(c.a.size());
      for (size_t i = 0; i < c.a.size(); ++i) keep[which][i] = static_cast<uint8_t>(std::lround(c.a[i] * 255.0f));
    }
    m.x0 = kx0[which], m.y0 = ky0[which], m.w = kw[which], m.h = kh[which];
    m.a.resize(keep[which].size());
    for (size_t i = 0; i < m.a.size(); ++i) m.a[i] = keep[which][i] / 255.0f;
  }
  return m;
}
// the same, landscape (a HOMETOWN, drawn W x H = kHomeW x kHomeH and turned onto the card: its right edge is the card's top)
inline M crest_land(const Cv& cv, float in) {
  const float w = kCW, h = kCH;  // (the portrait card's)
  auto land = [&](const std::vector<float>& pp) {
    std::vector<float> q;
    for (size_t i = 0; i + 1 < pp.size(); i += 2) q.push_back(static_cast<float>(kHomeW) - pp[i + 1]), q.push_back(pp[i]);
    return q;
  };
  auto rr = [&](float x0, float y0, float x1, float y1, float r) { return cv.rrect(kHomeW - y1, x0, kHomeW - y0, x1, r); };
  const M m = Cv::inter(cv.poly(land(crest_pts(in, w, h))), rr(in, in, w - in, h - in, std::max(0.0f, 48 - in)));
  return cv.uni(Cv::inter(m, rr(in, kCrest + in - 60, w - in, h - in, std::max(8.0f, 48 - in))), Cv::inter(m, rr(w * 0.08f, in, w * 0.92f, h - in, 48)));
}

// ================================================================================================= r220: the JAM '96 die-cuts
// Lance 11-11 picked DIRECTION 1 "ROYAL ESCALATION" (logs\r220-jam-diecuts): each rarity its own die - BASE the crest (today's), SILVER
// the CUT CREST, GOLD /10 the CROWN (gold ball finials, shield foot), BLACK 1/1 the STAR CROWN (jewels, side fins, shield foot), SIGNED
// the CHARTER (swallowtail foot), TIZM BOOM the BOOM BURST, HOMETOWN the PENNANT, a shiny its cut + the sparkle notch. The shapes, their
// 64-point 3D edges, THE PUNCH's crest match and the cut-away bits: xr_jamcut_shapes.inc (tools/outside/jamcut220/cuts220.py).
#include "xr_jamcut_shapes.inc"

// the die a JAM '96 card wears: its rarest feature (r211's odds: BLACK 1/1 > SIGNED > TIZM BOOM > HOMETOWN > GOLD > SILVER > BASE); a
// shiny the sparkle version of that cut. Its shape (0 .. jamcut::kShapes): cut x 2 + shiny
inline int jam_shape(const Ref& r) {
  int cut = jamcut::kCutCrest;
  if (r.parallel == kBlack) cut = jamcut::kCutStar;
  else if (r.hit == kHitSigned) cut = jamcut::kCutCharter;
  else if (r.hit == kHitBoom) cut = jamcut::kCutBoom;
  else if (r.hit == kHitHome) cut = jamcut::kCutPennant;
  else if (r.parallel == kGoldTizm) cut = jamcut::kCutCrown;
  else if (r.parallel == kSilver) cut = jamcut::kCutSharp;
  return cut * 2 + (r.shiny && jamcut::kShape[cut * 2 + 1].poly != nullptr ? 1 : 0);
}
inline const jamcut::Layout& jam_layout(int shape) { return jamcut::kLayout[std::max(0, std::min(jamcut::kCutN - 1, shape / 2))]; }
// the bottom of the back moves up this far over a die cut into the card's foot (a swallowtail / pennant)
inline float jam_back_foot(int shape) {
  const int cut = shape / 2;
  return cut == jamcut::kCutCharter || cut == jamcut::kCutPennant ? jam_layout(shape).foot : 0.0f;
}
// a polygon's anti-aliased cover over a w x h picture (Cv::poly's 4 sub-rows, without a canvas)
inline std::vector<float> poly_cover(const float* xy, int n, int w, int h) {
  std::vector<float> a(static_cast<size_t>(w) * h, 0.0f), xs;
  for (int y = 0; y < h; ++y)
    for (int s = 0; s < 4; ++s) {
      const float sy = y + (s + 0.5f) / 4.0f;
      xs.clear();
      for (int i = 0; i < n; ++i) {
        const float ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
        if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) xs.push_back(ax + (sy - ay) / (by - ay) * (bx - ax));
      }
      for (size_t i = 1; i < xs.size(); ++i)
        for (size_t j = i; j > 0 && xs[j - 1] > xs[j]; --j) std::swap(xs[j - 1], xs[j]);
      for (size_t k = 0; k + 1 < xs.size(); k += 2) {
        const float l = xs[k], r = xs[k + 1];
        const int il = std::max(0, static_cast<int>(std::floor(l))), ir = std::min(w - 1, static_cast<int>(std::floor(r)));
        for (int x = il; x <= ir; ++x) a[static_cast<size_t>(y) * w + x] += 0.25f * std::max(0.0f, std::min(r, x + 1.0f) - std::max(l, static_cast<float>(x)));
      }
    }
  for (float& v : a) v = sat(v);
  return a;
}
// the exact euclidean distance transform of one line (Felzenszwalb & Huttenlocher), squared, in place
inline void edt_line(float* f, int n, std::vector<float>& d, std::vector<int>& v, std::vector<float>& z) {
  d.resize(static_cast<size_t>(n)), v.resize(static_cast<size_t>(n)), z.resize(static_cast<size_t>(n) + 1);
  int k = 0;
  v[0] = 0;
  z[0] = -1e30f, z[1] = 1e30f;
  for (int q = 1; q < n; ++q) {
    double s;
    for (;;) {
      const int p = v[static_cast<size_t>(k)];
      s = ((static_cast<double>(f[q]) + static_cast<double>(q) * q) - (static_cast<double>(f[p]) + static_cast<double>(p) * p)) / (2.0 * (q - p));
      if (s <= z[static_cast<size_t>(k)]) --k;  // (z[0] is -inf: stops at 0)
      else break;
    }
    ++k;
    v[static_cast<size_t>(k)] = q;
    z[static_cast<size_t>(k)] = static_cast<float>(s);
    z[static_cast<size_t>(k) + 1] = 1e30f;
  }
  k = 0;
  for (int q = 0; q < n; ++q) {
    while (z[static_cast<size_t>(k) + 1] < q) ++k;
    const int p = v[static_cast<size_t>(k)];
    d[static_cast<size_t>(q)] = (q - p) * static_cast<float>(q - p) + f[p];
  }
  for (int q = 0; q < n; ++q) f[q] = d[static_cast<size_t>(q)];
}
// a die's signed distance map (one byte a front px: (s + 1) x 4, s its distance inside the cut's outline in px - an edge pixel its own
// cover - 0.5), made once a session per shape it's first needed for (~1.6 MB each; lazy, on a drawing thread - never the frame's), so
// any inset is a threshold: cover(inset) = sat(s - inset + 0.5)
inline const std::vector<uint8_t>& cut_map(int shape) {
  static std::mutex mu;
  static std::vector<uint8_t> keep[jamcut::kShapes];
  shape = std::max(0, std::min(jamcut::kShapes - 1, shape));
  if (jamcut::kShape[shape].poly == nullptr) shape -= 1;  // (no shiny version: its plain cut)
  std::lock_guard<std::mutex> lock(mu);
  std::vector<uint8_t>& k = keep[shape];
  if (!k.empty()) return k;
  const int W = kFrontW, H = kFrontH, PW = W + 2, PH = H + 2;
  const jamcut::ShapeDef& sd = jamcut::kShape[shape];
  const std::vector<float> cov = poly_cover(sd.poly, sd.polyN, W, H);
  std::vector<float> g(static_cast<size_t>(PW) * PH, 0.0f), line, d, z;
  std::vector<int> v;
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) g[static_cast<size_t>(y + 1) * PW + x + 1] = cov[static_cast<size_t>(y) * W + x] >= 0.5f ? 1e20f : 0.0f;
  line.resize(static_cast<size_t>(std::max(PW, PH)));
  for (int x = 0; x < PW; ++x) {
    for (int y = 0; y < PH; ++y) line[static_cast<size_t>(y)] = g[static_cast<size_t>(y) * PW + x];
    edt_line(line.data(), PH, d, v, z);
    for (int y = 0; y < PH; ++y) g[static_cast<size_t>(y) * PW + x] = line[static_cast<size_t>(y)];
  }
  for (int y = 0; y < PH; ++y) edt_line(&g[static_cast<size_t>(y) * PW], PW, d, v, z);
  k.resize(static_cast<size_t>(W) * H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      const float c = cov[i];
      const float s = c >= 0.5f && c >= 0.999f ? std::sqrt(g[static_cast<size_t>(y + 1) * PW + x + 1]) - 0.5f : c - 0.5f;
      k[i] = static_cast<uint8_t>(std::max(0L, std::min(255L, std::lround((s + 1.0f) * 4.0f))));
    }
  return k;
}
inline float cut_cov(uint8_t code, float inset) { return sat(code * 0.25f - 1.0f - inset + 0.5f); }
// a die's cover inset by `inset` front px, over the whole front
inline M cut_mask(int shape, float inset) {
  const std::vector<uint8_t>& s = cut_map(shape);
  M m;
  m.w = kFrontW, m.h = kFrontH;
  m.a.resize(s.size());
  for (size_t i = 0; i < s.size(); ++i) m.a[i] = cut_cov(s[i], inset);
  return m;
}
// a mask cut to the die inset by `inset` (only over the mask's own box)
inline M cut_clip(const M& a, int shape, float inset) {
  const std::vector<uint8_t>& s = cut_map(shape);
  M m = a;
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) {
      const int X = m.x0 + x, Y = m.y0 + y;
      m.a[static_cast<size_t>(y) * m.w + x] *= X < 0 || Y < 0 || X >= kFrontW || Y >= kFrontH ? 0.0f : cut_cov(s[static_cast<size_t>(Y) * kFrontW + X], inset);
    }
  return m;
}
// the band between two insets (a frame's rule)
inline M cut_ring(int shape, float a, float b) {
  const std::vector<uint8_t>& s = cut_map(shape);
  M m;
  m.w = kFrontW, m.h = kFrontH;
  m.a.resize(s.size());
  for (size_t i = 0; i < s.size(); ++i) m.a[i] = std::max(0.0f, cut_cov(s[i], a) - cut_cov(s[i], b));
  return m;
}
// the same die turned for a landscape picture (a HOMETOWN, kHomeW x kHomeH: its x the card's y from the foot up, its y the card's x)
inline M cut_mask_land(int shape, float inset) {
  const std::vector<uint8_t>& s = cut_map(shape);
  M m;
  m.w = kHomeW, m.h = kHomeH;
  m.a.resize(static_cast<size_t>(kHomeW) * kHomeH);
  for (int Y = 0; Y < kHomeH; ++Y)
    for (int X = 0; X < kHomeW; ++X) m.a[static_cast<size_t>(Y) * kHomeW + X] = cut_cov(s[static_cast<size_t>(kHomeW - 1 - X) * kFrontW + Y], inset);
  return m;
}
// a small picture of a die (straight RGBA, w x h, box-filtered): `rgb` where the card is (inset `inset`), or - outside - where it isn't
// (a binder pocket's page colour over the cut-away corners). Kept (a few KB each)
inline ImgP cut_small(int shape, float inset, int w, int h, uint32_t rgb, bool outside) {
  static std::mutex mu;
  static std::unordered_map<std::string, ImgP> keep;
  const std::string key = std::to_string(shape) + ":" + std::to_string(static_cast<int>(inset * 4)) + ":" + std::to_string(w) + "x" + std::to_string(h) + ":" +
                          std::to_string(rgb) + (outside ? "o" : "i");
  {
    std::lock_guard<std::mutex> lock(mu);
    auto it = keep.find(key);
    if (it != keep.end()) return it->second;
  }
  const std::vector<uint8_t>& s = cut_map(shape);
  auto img = std::make_shared<Img>();
  img->w = w, img->h = h;
  img->rgba.assign(static_cast<size_t>(w) * h * 4, 0);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int X0 = x * kFrontW / w, X1 = std::max(X0 + 1, (x + 1) * kFrontW / w), Y0 = y * kFrontH / h, Y1 = std::max(Y0 + 1, (y + 1) * kFrontH / h);
      float acc = 0.0f;
      for (int Y = Y0; Y < Y1; ++Y)
        for (int X = X0; X < X1; ++X) acc += cut_cov(s[static_cast<size_t>(Y) * kFrontW + X], inset);
      float a = acc / static_cast<float>((X1 - X0) * (Y1 - Y0));
      if (outside) a = 1.0f - a;
      uint8_t* q = &img->rgba[(static_cast<size_t>(y) * w + x) * 4];
      q[0] = static_cast<uint8_t>((rgb >> 16) & 255), q[1] = static_cast<uint8_t>((rgb >> 8) & 255), q[2] = static_cast<uint8_t>(rgb & 255);
      q[3] = static_cast<uint8_t>(std::lround(sat(a) * 255.0f));
    }
  std::lock_guard<std::mutex> lock(mu);
  keep[key] = img;
  return img;
}
// THE PUNCH's cut-away bits: the crest minus the cut, printed as the card's frame (its ink edge, its frame colour) - a quarter-size
// picture (kBitsW x kBitsH) each bit's quad cuts its piece from (its alpha the waste)
constexpr int kBitsW = 270, kBitsH = 378;
inline std::vector<uint8_t> bits_px(int shape, uint32_t frame, uint32_t frame2) {
  std::vector<uint8_t> out(static_cast<size_t>(kBitsW) * kBitsH * 4, 0);
  if (shape <= 1 && jamcut::kShape[shape].bitN == 0) return out;
  const std::vector<uint8_t>& cr = cut_map(0);
  const std::vector<uint8_t>& s = cut_map(shape);
  const F3 f1 = f3(frame), f2 = f3(frame2), ink = f3(kInk);
  for (int y = 0; y < kBitsH; ++y)
    for (int x = 0; x < kBitsW; ++x) {
      float acc[4] = {0, 0, 0, 0};
      for (int Y = 4 * y; Y < 4 * y + 4; ++Y)
        for (int X = 4 * x; X < 4 * x + 4; ++X) {
          const size_t i = static_cast<size_t>(Y) * kFrontW + X;
          const float a = cut_cov(cr[i], 0.0f) * (1.0f - cut_cov(s[i], 0.0f));
          if (a <= 0.0f) continue;
          const float fr = cut_cov(cr[i], 14.0f);
          const F3 c = mixf(ink, mixf(f1, f2, sat(static_cast<float>(Y) / kFrontH * 1.6f + static_cast<float>(X) / kFrontW * 0.4f) * 0.8f), fr);
          acc[0] += c.r * a, acc[1] += c.g * a, acc[2] += c.b * a, acc[3] += a;
        }
      uint8_t* q = &out[(static_cast<size_t>(y) * kBitsW + x) * 4];
      if (acc[3] <= 1e-4f) continue;
      for (int k = 0; k < 3; ++k) q[k] = static_cast<uint8_t>(std::lround(std::min(255.0f, acc[k] / acc[3])));
      q[3] = static_cast<uint8_t>(std::lround(sat(acc[3] / 16.0f) * 255.0f));
    }
  return out;
}
// a gold / magenta ball finial (the crowns' tips): an ink ring, a shaded sphere, its glint
inline void jam_sphere(Cv& cv, float cx, float cy, float r, bool gold) {
  cv.fill(cv.ellipse(cx - r - 5, cy - r - 5, cx + r + 5, cy + r + 5), kInk);
  const float hx = cx - 0.35f * r, hy = cy - 0.4f * r, R = 1.6f * r;
  cv.fillp(cv.ellipse(cx - r, cy - r, cx + r, cy + r), [&](float x, float y) {
    const float t = std::hypot(x - hx, y - hy) / R;
    return gold ? ramp(t, {{0, 0xFFF8DC}, {0.35f, 0xFFD24A}, {0.7f, 0xB07A10}, {1, 0x5A3A06}}) : ramp(t, {{0, 0xFFB8F0}, {0.25f, kMag}, {0.6f, 0x3A0A34}, {1, 0x08040C}});
  }, 1.0f, gold ? 0.85f : 0.4f);
  cv.fill(cv.ellipse(cx - 0.55f * r, cy - 0.62f * r, cx - 0.1f * r, cy - 0.2f * r), 0xFFFFFF, 0.7f);
}
// a four-point sparkle (a shiny's, round its notch)
inline void jam_sparkle(Cv& cv, float cx, float cy, float r, uint32_t rgb) {
  std::vector<float> p;
  for (int i = 0; i < 8; ++i) {
    const float a = -kPi / 2 + i * kPi / 4, d = i % 2 == 0 ? r : 0.22f * r;
    p.push_back(cx + d * std::cos(a)), p.push_back(cy + d * std::sin(a));
  }
  cv.fill(cv.poly(p), rgb);
}
#ifdef HARNESS_REAL_CARDS
// (the PC proof's safe-zone checks: what each part of the last B front covers - set by b_front, read by the harness)
struct JamProbe {
  int shape = 0;
  M plate, medal, pill, name, ticket, ticketBox, figure, sticker, serial;
};
inline JamProbe& jam_probe() {
  static JamProbe p;
  return p;
}
#endif
// its marble field by finish (dir_b marble_paint); `fo` out: its foil there
struct MarbleField {
  Marble m;
  int par;
  uint32_t c1;
  Refr rs, rg;
  MarbleField(uint32_t seed, int p, uint32_t col, int W, int H) : m(seed, 260.0f, W, H), par(p), c1(col) {
    rs = refr(kSilverF, static_cast<float>(W), 0.5f, 110, 170, 6);
    rg = refr(kGoldF, static_cast<float>(W), 0.0f, 150, 230, 6);
  }
  F3 operator()(float x, float y) const {
    float fo;
    return at(x, y, &fo);
  }
  F3 at(float x, float y, float* fo) const {
    const float v = m(x, y);
    *fo = par == kBase ? 0.0f : (par == kSilver ? 0.9f : 0.85f);
    if (par == kBase)
      return ramp(poster(v), {{0, kPurp}, {0.3f, mix_rgb(c1, 0x000000, 0.2f)}, {0.5f, kMag}, {0.7f, kTeal}, {0.85f, kYel}, {1, 0xFFFFFF}});
    if (par == kSilver) {
      const F3 rb = hsv(v * 1.4f + 0.1f, 0.55f, 1.0f);
      return rs(x, y) * 0.55f + rb * (0.45f * (0.7f + 0.5f * v));
    }
    if (par == kGoldTizm) {
      const float t = v * 3.0f - std::floor(v * 3.0f);
      return ramp(t, {{0, 0x6A4206}, {0.3f, 0xC08A18}, {0.6f, 0xFFE07A}, {0.8f, 0xFFF4C8}, {1, 0x8A5A0A}}) * 0.8f + rg(x, y) * 0.2f;
    }
    const float a = (v - 0.52f) / 0.018f, b = (v - 0.27f) / 0.012f;
    const float vein = sat(std::exp(-a * a) + 0.6f * std::exp(-b * b));
    const F3 base = ramp(v, {{0, 0x050407}, {0.5f, 0x15121E}, {1, 0x2A2436}});
    const F3 neon = hsv(0.83f + 0.25f * v, 0.85f, 1.0f);
    *fo = 0.25f + 0.75f * vein;
    return base + neon * (vein * 0.95f);
  }
  float foil(float x, float y) const {
    if (par == kBase) return 0.0f;
    if (par == kBlack) {
      const float v = m(x, y), a = (v - 0.52f) / 0.018f, b = (v - 0.27f) / 0.012f;
      return 0.25f + 0.75f * sat(std::exp(-a * a) + 0.6f * std::exp(-b * b));
    }
    return par == kSilver ? 0.9f : 0.85f;
  }
};
// 90s block letters: a soft shadow, a 10-step extrusion, an ink line, the face, a printed highlight across its top
template <class P>
TM block_text(Cv& cv, const std::string& t, float x, float y, float px, float room, P face, uint32_t ext0 = kPurp, uint32_t ext1 = 0x05030A,
              float dx = 10, float dy = 14, float slant = 0.2f, float angle = 0.0f, float outline = 8.0f) {
  TM m = text_masks(cv, t, x, y, px, 'c', slant, 1.0f, angle, room, outline);
  cv.fill(cv.blur(cv.shift(m.stroke, dx * 1.6f, dy * 1.6f), 6), 0x000000, 0.55f);
  for (int i = 10; i >= 1; --i) {
    const float k = i / 10.0f;
    cv.fill(cv.shift(m.stroke, dx * k, dy * k), mix_rgb(ext0, ext1, k));
  }
  cv.fill(m.stroke, kInk);
  cv.fillp(m.fill, face);
  M hi = m.fill;
  for (int yy = 0; yy < hi.h; ++yy) {
    const float Y = hi.y0 + yy + 0.5f, k = sat((y - 0.15f * px - Y) / (0.25f * px));
    for (int xx = 0; xx < hi.w; ++xx) hi.a[static_cast<size_t>(yy) * hi.w + xx] *= k;
  }
  cv.fill(hi, 0xFFFFFF, 0.35f);
  return m;
}

// ================================================================================================= the fronts
// what a front needs besides the card: its picture (null: none - a special's 3D stands in for it), its emblem, the set's count
struct FrontIn {
  ImgP art;
  int total = 0, special = 0, level = 0;
  ImgP boomPage, boomBadge;  // a TIZM BOOM's printed page / its balloon (pp_page / pp_badge)
  ImgP home;                 // a HOMETOWN's home picture
  std::string place;         // its place's name
};
const char* const kParNameA[4] = {"", "SILVER TIZM", "GOLD TIZM", "BLACK TIZM"};

// the figure on a front, by the r217 auto-fit: a bust (cut off at its foot) anchored low with its cut hidden under the name, a
// whole figure standing on the name bar, fitted whole (contain) and centred; an opaque photo framed
inline void front_figure(Cv& cv, const Art& a, bool jam, const M* clip, uint32_t keyCol, float foot = 0.0f) {  // (r220: foot - a Jam die's room at its foot)
  if (!a.ok()) return;
  if (a.photo) {  // (an old portrait with no see-through background: a framed photo, never blown up to the box)
    const float cx = 540, cy = jam ? 700 : 660, s = 0.72f * 470.0f;
    const float r = s * std::min(1.0f, a.h() / a.w()), q = s * std::min(1.0f, a.w() / a.h());
    cv.fill(cv.blur(cv.shift(cv.rrect(cx - q - 22, cy - r - 22, cx + q + 22, cy + r + 22, 26), 8, 16), 12), 0x000000, 0.45f);
    cv.fill(cv.rrect(cx - q - 22, cy - r - 22, cx + q + 22, cy + r + 22, 26), 0xFFFFFF);
    paste(cv, *a.img, cx - q, cy - r, cx + q, cy + r);
    return;
  }
  Fig f;
  if (!jam) {
    f.shadow = 0.5f, f.rim = 0xFFFFFF, f.rimA = 0.55f;
    Placed p = a.bust ? fit(a, 70, 150, kCW - 50, 1250, 'b', 1.0f) : fit(a, 80, 190, kCW - 80, 1150, 'b', 0.98f);
    // (a wide, flat one - a bird's spread wings, a fish, a snake lying down: under 40% of the box's height standing on the bar - hangs in
    // the box's middle instead of lying along the bar)
    if (!a.bust && p.y1 - p.y0 < 0.40f * (1150 - 190)) p = fit(a, 80, 190, kCW - 80, 1150, 'c', 0.98f);
    f.clip = clip;
    figure(cv, a, p, f);
#ifdef HARNESS_REAL_CARDS
    prof_text() += " FIT " + std::to_string(static_cast<int>(p.x0)) + "," + std::to_string(static_cast<int>(p.y0)) + "," + std::to_string(static_cast<int>(p.x1)) + "," +
                   std::to_string(static_cast<int>(p.y1)) + (a.bust ? " BUST" : "") + (a.edge ? " EDGE" : "") + " art " + std::to_string(a.img->w);
#endif
  } else {
    f.shadow = 0.45f, f.keyline = 12, f.keyCol = keyCol, f.keyInk = kInk;
    Placed p = a.bust ? fit(a, 60, 250, kCW - 60, 1310 - foot, 'b', 1.0f) : fit(a, 90, 250, kCW - 90, 1130 - foot, 'b', 1.0f);
    if (!a.bust && p.y1 - p.y0 < 0.40f * (1130 - foot - 250)) p = fit(a, 90, 250, kCW - 90, 1130 - foot, 'c', 1.0f);  // (wide + flat: in the middle)
    if (a.bust) f.clip = clip;
#ifdef HARNESS_REAL_CARDS
    jam_probe().figure = figure(cv, a, p, f);
#else
    figure(cv, a, p, f);
#endif
#ifdef HARNESS_REAL_CARDS
    prof_text() += " FIT " + std::to_string(static_cast<int>(p.x0)) + "," + std::to_string(static_cast<int>(p.y0)) + "," + std::to_string(static_cast<int>(p.x1)) + "," +
                   std::to_string(static_cast<int>(p.y1)) + (a.bust ? " BUST" : "") + (a.edge ? " EDGE" : "") + " art " + std::to_string(a.img->w);
#endif
  }
}

// ---------------------------------------------------------------------------------------- A: PRIZM CHROME
inline void a_field(Cv& cv, const Card& c, int par, const M& inner) {
  const Lin team = lin_full({{0, mix_rgb(c.c1, 0x000000, 0.55f)}, {0.55f, c.c1}, {1, mix_rgb(c.c1, 0xFFFFFF, 0.35f)}}, 118, kCW, kCH);
  const Refr r = refr(fk_of(par), kCW);
  const float fo = kStock[par].fFacet;
  const float ca = std::cos(-62.0f * kPi / 180.0f), sa = std::sin(-62.0f * kPi / 180.0f);
  cv.fillp(inner, [&](float x, float y) {
    const float sh = shards(x, y, ca, sa, 150, 230, 3);
    const F3 t = team(x, y);
    if (par == kBase) return t * (0.86f + 0.24f * sh);
    const F3 q = r(x, y);
    if (par == kSilver) return q * 0.62f + t * (0.38f * (0.8f + 0.4f * sh));
    if (par == kGoldTizm) return q * 0.78f + t * 0.22f;
    return q * 0.85f + t * (0.15f * (0.5f + sh));
  }, 1.0f, fo);
}
inline void a_frame(Cv& cv, int par, float W, float H) {
  const M card = cv.rrect(0, 0, W, H, 48);
  if (par == kBase) {
    cv.fill(card, 0xF7F7F7);
    cv.fill(cv.ring(0, 0, W, H, 48, 3), 0xD8DBE0);
  } else {
    cv.fillp(Cv::minus(card, cv.rrect(27, 27, W - 27, H - 27, 33)), refr(fk_of(par), W, 1.3f, 90, 140, 8, 1.0f, -62, H), 1.0f, kStock[par].fFrame);
  }
}
// the chrome hexagon rating + its series tag (+ its level's stars under it)
inline void a_rating(Cv& cv, const Card& c, int par, float cx, float cy, int level, float k = 1.0f, float maxX = kCW) {
  const float fo = par == kBase ? 0.0f : 1.0f;
  cv.fill(cv.blur(cv.shift(cv.hexagon(cx, cy, 104 * k), 4 * k, 10 * k), 8 * k), 0x000000, 0.45f);
  cv.fill(cv.hexagon(cx, cy, 106 * k), 0x0B0F1A);
  if (par == kBase) cv.fillp(cv.hexagon(cx, cy, 98 * k), Lin({{0, 0xFFFFFF}, {1, 0xC9CED8}}, 90, cy - 98 * k, cy + 98 * k));
  else cv.fillp(cv.hexagon(cx, cy, 98 * k), refr(par == kGoldTizm ? kGoldF : kSilverF, kCW, par == kBlack ? 0.5f : 2.0f), 1.0f, fo);
  cv.fill(cv.hexagon(cx, cy, 80 * k), kNavyA);
  cv.fill(Cv::inter(cv.hexagon(cx, cy, 80 * k), cv.poly({cx - 90 * k, cy - 90 * k, cx + 90 * k, cy - 90 * k, cx + 90 * k, cy - 30 * k, cx - 90 * k, cy + 10 * k})), 0xFFFFFF, 0.08f);
  TS s;
  s.outline = 4 * k;
  s.ocol = 0x05070C;
  text(cv, std::to_string(c.rating), cx, cy - 14 * k, 92 * k, par == kGoldTizm ? 0xFFE89A : kGoldA, s);
  text(cv, "OVR", cx, cy + 46 * k, 34 * k, 0xE8ECF5);
  const std::string sn = kSeriesName[c.series];
  const float sw = text_width(sn, 36 * k) + 44 * k, tx = std::min(cx, maxX - 34 * k - sw / 2);
  cv.fill(cv.rrect(tx - sw / 2 - 4 * k, cy + 116 * k, tx + sw / 2 + 4 * k, cy + 172 * k, 28 * k), 0x0B0F1A);
  cv.fill(cv.rrect(tx - sw / 2, cy + 120 * k, tx + sw / 2, cy + 168 * k, 24 * k), kSeriesRgb[c.series]);
  text(cv, sn, tx, cy + 144 * k, 36 * k, kSeriesInk[c.series]);
  if (level > 1) {  // r146: its level - that many gold stars on a navy pill
    const float lw = level * 46.0f * k + 26.0f * k, lx = std::min(cx, maxX - 34 * k - lw / 2);
    cv.fill(cv.rrect(lx - lw / 2 - 4 * k, cy + 180 * k, lx + lw / 2 + 4 * k, cy + 242 * k, 31 * k), 0x0B0F1A);
    cv.fill(cv.rrect(lx - lw / 2, cy + 184 * k, lx + lw / 2, cy + 238 * k, 27 * k), kNavyA);
    for (int i = 0; i < level; ++i) star(cv, lx - (level - 1) * 23.0f * k + i * 46.0f * k, cy + 211 * k, 17 * k, kGoldA);
  }
}
// the finish pill + shiny pill under the mark
inline void a_pills(Cv& cv, const Ref& r, float x, float py) {
  if (r.parallel != kBase) {
    const std::string name = kParNameA[r.parallel];
    const float pw = text_width(name, 38) + 60;
    cv.fill(cv.rrect(x - 4, py - 4, x + pw + 4, py + 62, 33), 0x0B0F1A);
    if (r.parallel == kBlack) cv.fill(cv.rrect(x, py, x + pw, py + 58, 29), 0x0B0C10);
    else cv.fillp(cv.rrect(x, py, x + pw, py + 58, 29), refr(fk_of(r.parallel), kCW, 3.0f), 1.0f, 1.0f);
    text(cv, name, x + pw / 2, py + 30, 38, r.parallel == kSilver ? 0x101830 : (r.parallel == kGoldTizm ? 0x3A2600 : 0xFFFFFF));
    py += 72;
  }
  if (r.shiny) {
    const float pw = text_width("SHINY", 38) + 60;
    cv.fill(cv.rrect(x - 4, py - 4, x + pw + 4, py + 62, 33), 0x0B0F1A);
    cv.fillp(cv.rrect(x, py, x + pw, py + 58, 29), refr(kSilverF, kCW, 4.2f, 150, 230, 3, 1.6f), 1.0f, 1.0f);
    text(cv, "SHINY", x + pw / 2, py + 30, 38, 0x2A0A5A);
  }
}
inline void a_stamp(Cv& cv, const Ref& r, float sx0, float sy0, float sx1, float sy1) {
  cv.fill(cv.blur(cv.shift(cv.rrect(sx0, sy0, sx1, sy1, 16), 4, 8), 6), 0x000000, 0.5f);
  cv.fill(cv.rrect(sx0 - 5, sy0 - 5, sx1 + 5, sy1 + 5, 20), 0x0B0F1A);
  const bool g = r.parallel == kGoldTizm;
  cv.fillp(cv.rrect(sx0, sy0, sx1, sy1, 16), g ? refr(kGoldF, kCW, 4.0f) : refr(kSilverF, kCW, 4.5f, 150, 230, 3, 1.6f), 1.0f, g ? 0.5f : 1.0f);
  text(cv, stamp_of(r), 0.5f * (sx0 + sx1), 0.5f * (sy0 + sy1) + 2, 66, g ? 0x2A1A00 : 0x0B0F1A);
}
inline void a_auto_pill(Cv& cv, float x0, float y0) {
  cv.fill(cv.rrect(x0 - 4, y0 - 4, x0 + 344, y0 + 70, 37), 0x0B0F1A);
  cv.fillp(cv.rrect(x0, y0, x0 + 340, y0 + 66, 33), refr(kSilverF, kCW, 5.0f, 150, 230, 3, 1.5f), 1.0f, 1.0f);
  text(cv, "CERTIFIED AUTO", x0 + 170, y0 + 34, 36, 0x0B1022);
}
// the leaning name bar (its chrome lip), the name, the rarity roundel; then the stats' ink panel, the set and its number
inline void a_foot(Cv& cv, const Card& c, int par, const M& inner, int total, bool boom) {
  const float W = kCW, H = kCH;
  const float tl = 1166, tr = 1112, bl = 1322, br = 1268;
  auto lipPaint = [&](float x, float y) -> F3 {
    if (par == kBase) return f3(c.c2);
    static const Refr s1 = refr(kSilverF, kCW, 1.0f), g1 = refr(kGoldF, kCW, 1.0f), b1 = refr(kSilverF, kCW, 2.4f, 150, 230, 3, 1.6f);
    return par == kSilver ? s1(x, y) : (par == kGoldTizm ? g1(x, y) : b1(x, y));
  };
  const float lfo = par == kBase ? 0.0f : 1.0f;
  cv.fill(cv.blur(cv.shift(cv.poly({28, tl, W - 28, tr, W - 28, br, 28, bl}), 0, 10), 8), 0x000000, 0.45f);
  cv.fillp(Cv::inter(cv.poly({28, tl - 20, W - 28, tr - 20, W - 28, tr + 2, 28, tl + 2}), inner), lipPaint, 1.0f, lfo);
  const M bar = Cv::inter(cv.poly({28, tl, W - 28, tr, W - 28, br + 60, 28, bl + 60}), inner);
  if (boom) cv.fill(bar, kPpRed);
  else if (par == kBlack) cv.fill(bar, 0x0A0B10);
  else cv.fillp(bar, Lin({{0, mix_rgb(c.c1, 0x000000, 0.45f)}, {1, mix_rgb(c.c1, 0x000000, 0.75f)}}, 90, 1110, 1330));
  cv.fill(Cv::inter(cv.poly({28, tl, W - 28, tr, W - 28, tr + 30, 28, tl + 30}), inner), 0xFFFFFF, 0.08f);
  const float ang = std::atan2(tl - tr, W - 56) * 180.0f / kPi;
  TS s;
  s.outline = 7;
  s.ocol = 0x05070C;
  s.shadow = 8;
  s.slant = 0.16f;
  s.room = 760;
  s.angle = ang;
  if (c.sub.empty()) {
    text(cv, upper(c.name), 470, 1222, 112, 0xFFFFFF, s);
  } else {  // r152: a costume's own name, its hero under it
    text(cv, upper(c.name), 470, 1200, 92, 0xFFFFFF, s);
    s.outline = 4, s.shadow = 4, s.room = 600;
    text(cv, upper(c.sub), 470, 1262, 42, kGoldA, s);
  }
  // the rarity roundel at its end
  const float rx = W - 128, ry = 1196;
  cv.fill(cv.hexagon(rx, ry, 74), 0x05070C);
  if (par == kBase) cv.fill(cv.hexagon(rx, ry, 66), 0xFFFFFF);
  else cv.fillp(cv.hexagon(rx, ry, 66), lipPaint, 1.0f, 1.0f);
  cv.fill(cv.hexagon(rx, ry, 54), kSeriesRgb[c.series]);
  TS o;
  o.outline = 3;
  text(cv, kRarityShort[c.rarity], rx, ry + 2, c.rarity == kSuper ? 50.0f : 56.0f, 0xFFFFFF, o);
  // the stats: big labels, chunky pips
  const M panel = Cv::inter(cv.poly({28, bl, W - 28, br, W - 28, H, 28, H}), inner);
  cv.fill(panel, kNavyA);
  cv.fillp(Cv::inter(cv.poly({28, bl, W - 28, br, W - 28, br + 5, 28, bl + 5}), inner), lipPaint, 1.0f, lfo);
  static const char* const kStatN[4] = {"SPEED", "SHOOTING", "PASSING", "SLIDE TACKLE"};
  for (int i = 0; i < 4; ++i) {
    const int col = i % 2, row = i / 2;
    const float lx = 66.0f + col * 500.0f, y = 1366.0f + row * 50.0f;
    TS l;
    l.align = 'l';
    l.room = col == 0 ? 236.0f : 270.0f;
    text(cv, kStatN[i], lx, y, 38, 0xC8D2E6, l);
    pips(cv, lx + (col == 0 ? 250.0f : 278.0f), y, lit_of(c.stats[i]), 30, 8, kGoldA, 0x2A3354);
  }
  TS l;
  l.align = 'l';
  l.room = 640;
  text(cv, kSetName, 66, 1462, 28, 0x8A94B8, l);
  TS rr;
  rr.align = 'r';
  rr.outline = 2;
  text(cv, num3(c.num) + "/" + std::to_string(total), W - 64, 1458, 42, kGoldA, rr);
}

// A's front (1080 x 1512)
inline std::vector<uint8_t> a_front(const Card& c, const Ref& r, const FrontIn& in) {
  const float W = kCW, H = kCH;
  Cv cv(kFrontW, kFrontH);
  const int par = r.parallel;
  const bool boom = r.hit == kHitBoom;
  LKP("start");
  const M inner = cv.rrect(28, 28, W - 28, H - 28, 32);
  a_frame(cv, par, W, H);
  LKP("frame");
  if (boom && in.boomPage) {  // r149 / r166: a TIZM BOOM - its printed comic page inside A's frame
    M pg = inner;
    paste(cv, *in.boomPage, 0, 0, W, H);  // (its cream stock, inked panel, Ben-Day sky, sun, city)
    if (par != kBase) cv.fillp(Cv::minus(cv.rrect(0, 0, W, H, 48), cv.rrect(16, 16, W - 16, H - 16, 36)), refr(fk_of(par), W, 1.3f, 90, 140, 8), 1.0f, kStock[par].fFrame);
    (void)pg;
  } else {
    a_field(cv, c, par, inner);
    LKP("field");
    if (par == kBase)
      cv.fill(Cv::inter(dots_mask(cv, inner, 16, [](float, float y) { return (y - 700.0f) / 700.0f; }, 30), inner), mix_rgb(c.c1, 0x000000, 0.6f), 0.35f);
    // the colour slash behind the character (its second colour), chrome-edged
    const float pfo = par == kBase ? 0.0f : 0.8f;
    cv.fill(Cv::inter(cv.poly({28, 966, W - 28, 366, W - 28, 654, 28, 1254}), inner), par != kBlack ? 0xFFFFFF : 0xC9CED8, par != kBase ? 0.9f : 0.75f, pfo);
    const M sl = Cv::inter(cv.poly({28, 980, W - 28, 380, W - 28, 640, 28, 1240}), inner);
    cv.fill(sl, mix_rgb(c.c2, 0x000000, par != kBlack ? 0.15f : 0.5f), 1.0f, par == kBase ? 0.0f : 0.5f);
    cv.fill(dots_mask(cv, sl, 12, [](float, float) { return 0.18f; }, 45), 0xFFFFFF, 0.25f);
    beam(cv, 560, 120, par == kBase ? 0.22f : 0.42f);
    beam(cv, 330, 34, par == kBase ? 0.15f : 0.3f);
    cv.glow(140, 260, W - 140, 1120, 0xFFFFFF, 0.18f);
    LKP("slash+beams");
  }
  const bool window = in.special == kSpWindow || in.special == kSpGlass;
  if (window) {  // r138: a special's window - see-through (its 3D behind it); what's drawn over it later stays
    cv.cut.assign(static_cast<size_t>(cv.W) * cv.H, 0);
    const M hole = cv.rrect(kHoleX0, kHoleY0, kHoleX1, kHoleY1, 24);
    for (int y = 0; y < hole.h; ++y)
      for (int x = 0; x < hole.w; ++x)
        if (hole.a[static_cast<size_t>(y) * hole.w + x] > 0.5f) cv.cut[static_cast<size_t>(hole.y0 + y) * cv.W + hole.x0 + x] = 1;
  } else if (in.art) {
    front_figure(cv, art_of(in.art), false, &inner, 0);
  }
  LKP("figure");
  // the mark, its finish / shiny; the rating; a TIZM BOOM's balloon
  logo(cv, 46, 40, 0.92f);
  a_pills(cv, r, 48, 186);
  a_rating(cv, c, par, W - 140, 150, in.level);
  if (boom && in.boomBadge) {
    paste(cv, *in.boomBadge, 372, 34, 708, 186);
    TS b;
    b.slant = 0.1f;
    b.xscale = 0.92f;
    b.angle = 4.0f;
    b.outline = 5;
    b.ocol = 0xFFFFFF;
    text(cv, "TIZM BOOM!", 536, 106, 44, kPpRed, b);
  }
  if (r.parallel == kGoldTizm || r.parallel == kBlack) a_stamp(cv, r, W - 300, 1000, W - 56, 1094);
  if (r.parallel == kBlack || r.hit == kHitSigned) a_auto_pill(cv, 56, 1050);
  LKP("mark");
  a_foot(cv, c, par, inner, in.total, boom);
  LKP("foot");
  std::vector<uint8_t> out = bytes(cv, true);
  LKP("bytes");
  return out;
}

// A's back (drawn 1080 x 1512)
inline Cv a_back_cv() {
  const float W = kCW, H = kCH;
  Cv cv(kFrontW, kFrontH);
  const M card = cv.rrect(0, 0, W, H, 48), inner = cv.rrect(28, 28, W - 28, H - 28, 32);
  cv.fillp(card, refr(kSilverF, W, 1.3f, 90, 140, 8), 1.0f, 0.95f);
  cv.fillp(inner, refr(kBlackF, W, 0.0f, 150, 230, 5), 1.0f, 0.6f);
  beam(cv, 520, 140, 0.25f);
  beam(cv, 300, 40, 0.18f);
  cv.glow(120, 160, W - 120, 900, 0x5AD8FF, 0.22f);
  gem(cv, W / 2, 470, 230, nullptr, 0x05070C, 16);
  cv.add(cv.blur(cv.hexagon(W / 2, 470, 240), 20), 0xFFFFFF, 0.08f);
  TS o;
  o.outline = 5;
  text(cv, "LANCINI", W / 2, 780, 70, 0xFFFFFF, o);
  TM a = text_masks(cv, "TIZM", W / 2 - 8, 892, 150, 'c', 0.24f, 0.92f, 0, 0, 0);
  cv.fill(a.fill, 0x22E0FF, 0.85f);
  a = text_masks(cv, "TIZM", W / 2 + 8, 904, 150, 'c', 0.24f, 0.92f, 0, 0, 0);
  cv.fill(a.fill, 0xFF2AD4, 0.85f);
  a = text_masks(cv, "TIZM", W / 2, 898, 150, 'c', 0.24f, 0.92f, 0, 0, 8);
  cv.fill(a.stroke, 0x05070C);
  cv.fillp(a.fill, Lin({{0, 0xFFFFFF}, {0.5f, 0xE4E9F2}, {0.56f, 0x9AA6BC}, {1, 0xF4F6FA}}, 90, 840, 960));
  o.outline = 4;
  text(cv, "2026-27 STRIKERS", W / 2, 1040, 64, kGoldA, o);
  text(cv, "SERIES 1", W / 2, 1110, 40, 0xB8C2D6);
  cv.fill(cv.rrect(170, 1200, W - 170, 1310, 55), 0x05070C, 0.8f);
  cv.fillp(cv.ring(170, 1200, W - 170, 1310, 55, 5), refr(kGoldF, W, 2.0f), 1.0f, 1.0f);
  text(cv, "CHASE THE 1/1", W / 2, 1256, 70, kGoldA, o);
  text(cv, "A STRIKERS SUPER LEAGUE COLLECTION", W / 2, 1400, 36, 0x8A94AA);
  return cv;
}

// ---------------------------------------------------------------------------------------- B: TIZM JAM '96
inline void b_medallion(Cv& cv, const Card& c, int par, float cx, float cy, float r) {
  cv.fill(cv.blur(cv.shift(cv.ellipse(cx - r, cy - r, cx + r, cy + r), 6, 12), 8), 0x000000, 0.5f);
  cv.fill(cv.ellipse(cx - r, cy - r, cx + r, cy + r), kInk);
  for (int i = 0; i < 24; ++i) {  // the checker ring
    const float a0 = i * kPi / 12, a1 = a0 + kPi / 12, o = r - 8, n = r - 34;
    cv.fill(cv.poly({cx + o * std::cos(a0), cy + o * std::sin(a0), cx + o * std::cos(a1), cy + o * std::sin(a1), cx + n * std::cos(a1), cy + n * std::sin(a1),
                     cx + n * std::cos(a0), cy + n * std::sin(a0)}),
            i % 2 == 0 ? 0xFFFFFF : kInk);
  }
  const float ri = r - 40;
  const M face = cv.ellipse(cx - ri, cy - ri, cx + ri, cy + ri);
  if (par == kBase) cv.fill(face, kYel);
  else if (par == kBlack) cv.fill(face, kMag, 1.0f, 0.6f);
  else cv.fillp(face, refr(fk_of(par), kCW, 2.0f), 1.0f, 1.0f);
  cv.fill(dots_mask(cv, face, 9, [](float, float) { return 0.25f; }, 20), par == kBase ? kOrng : 0xFFFFFF, 0.5f);
  TS s;
  s.outline = 6;
  s.ocol = kInk;
  s.slant = 0.12f;
  text(cv, std::to_string(c.rating), cx, cy - 10, 84, 0xFFFFFF, s);
  text(cv, "OVR", cx, cy + 46, 30, par != kBlack ? kInk : 0xFFFFFF);
}
inline void b_ribbon(Cv& cv, const M& shape, const std::string& t, int par) {
  const M band = Cv::inter(cv.poly({0, 330, 330, 0, 430, 0, 0, 430}), shape);
  cv.fill(cv.blur(cv.shift(band, 4, 10), 6), 0x000000, 0.5f);
  cv.fill(band, kInk);
  const M in = Cv::inter(cv.poly({0, 344, 344, 0, 416, 0, 0, 416}), shape);
  if (par == kBlack) cv.fill(in, kMag);
  else cv.fillp(in, refr(fk_of(par), kCW, 3.0f), 1.0f, 1.0f);
  TS s;
  s.angle = 45;
  s.room = 330;
  text(cv, t, 158, 222, 40, par == kSilver ? kInk : (par == kGoldTizm ? 0x3A2600 : 0xFFFFFF), s);
}
// the ticket panel: neon equalizer stats, the set, its number
// (r220: `shape` a die - the panel follows its cut (inset 44), moved up by its foot, its bottom row in from the clipped corners)
inline void b_ticket(Cv& cv, const Card& c, int par, int total, float W, float H, int shape = 0) {
  const float ft = shape != 0 ? jam_layout(shape).foot : 0.0f, ti = shape != 0 ? jam_layout(shape).textIn : 0.0f;
  const M tpr = cv.rrect(44, 1278 - ft, W - 44, H - 40 - ft, 26);
  const M tp = shape != 0 ? cut_clip(tpr, shape, 44) : tpr;
#ifdef HARNESS_REAL_CARDS
  jam_probe().ticket = tp, jam_probe().ticketBox = tpr;
#endif
  cv.fill(cv.grow(tp, 6), kInk);
  cv.fill(tp, 0x120A24);
  const uint32_t rim = par == kBase ? kYel : (par == kSilver ? 0xE4E9F2 : (par == kGoldTizm ? 0xFFD24A : kMag));
  if (shape != 0) cv.fill(Cv::minus(tp, cut_clip(cv.rrect(49, 1283 - ft, W - 49, H - 45 - ft, 21), shape, 49)), rim, 1.0f, par == kBase ? 0.0f : 0.8f);
  else cv.fill(cv.ring(44, 1278, W - 44, H - 40, 26, 5), rim, 1.0f, par == kBase ? 0.0f : 0.8f);
  cv.fill(cv.rrect(W / 2 - 2, 1300 - ft, W / 2 + 2, 1420 - ft, 2), 0x3A2A5A);
  static const uint32_t kNeon[4] = {kTeal, kMag, kYel, 0x7CF05A};
  static const char* const kStatN[4] = {"SPEED", "SHOOTING", "PASSING", "SLIDE TACKLE"};
  for (int i = 0; i < 4; ++i) {
    const int col = i % 2, row = i / 2;
    const float lx = 80.0f + col * 490.0f, y = 1322.0f + row * 60.0f - ft;
    TS l;
    l.align = 'l';
    l.room = col == 0 ? 220.0f : 250.0f;
    text(cv, kStatN[i], lx, y, 34, 0xFFFFFF, l);
    const int n = lit_of(c.stats[i]);
    const float bx = lx + (col == 0 ? 236.0f : 262.0f);
    for (int s = 0; s < 5; ++s) {
      const float h = 14.0f + s * 6.0f;
      cv.fill(cv.rrect(bx + s * 34, y + 18 - h, bx + s * 34 + 24, y + 18, 4), s < n ? kNeon[i] : 0x2E2448);
    }
  }
  TS l;
  l.align = 'l';
  l.room = 520 - ti;
  text(cv, "2026-27 LANCINI TIZM JAM '96", 80 + ti, 1446 - ft, 26, 0x9A8CC8, l);
  cv.fill(cv.rrect(W - 330 - ti, 1426 - ft, W - 268 - ti, 1466 - ft, 20), kRarRgb[c.rarity]);
  text(cv, kRarityShort[c.rarity], W - 299 - ti, 1447 - ft, 28, 0xFFFFFF);
  TS rr;
  rr.align = 'r';
  rr.outline = 2;
  rr.ocol = kInk;
  text(cv, num3(c.num) + "/" + std::to_string(total), W - 76 - ti, 1446 - ft, 40, kYel, rr);
}
// B's autograph sticker (its box: where bake_signature writes the real one - kSignBoxB)
constexpr float kStkX0 = 290, kStkY0 = 860, kStkX1 = 1010, kStkY1 = 1116;
inline void b_auto_sticker(Cv& cv, float foot = 0.0f) {  // (r220: up a die's foot)
  const float x0 = kStkX0, y0 = kStkY0 - foot, x1 = kStkX1, y1 = kStkY1 - foot;
#ifdef HARNESS_REAL_CARDS
  jam_probe().sticker = cv.rrect(x0 - 6, y0 - 6, x1 + 6, y1 + 6, 26);
#endif
  cv.fill(cv.blur(cv.shift(cv.rrect(x0, y0, x1, y1, 22), 5, 10), 6), 0x000000, 0.5f);
  cv.fill(cv.rrect(x0 - 6, y0 - 6, x1 + 6, y1 + 6, 26), kInk);
  cv.fillp(cv.rrect(x0, y0, x1, y1, 22), refr(kSilverF, kCW, 5.0f, 150, 230, 3, 1.6f), 1.0f, 1.0f);
  cv.fill(cv.rrect(x0 + 10, y0 + 10, x1 - 10, y1 - 10, 14), 0xFBFBF8);
  cv.fill(cv.rrect(x0 + 40, y1 - 46, x1 - 40, y1 - 44, 1), 0xC8CCD8);
  text(cv, "AUTHENTIC TIZM AUTOGRAPH", 0.5f * (x0 + x1), y1 - 26, 22, 0x6A7488);
}
inline void b_serial(Cv& cv, const Ref& r, float cx, float cy) {
  const bool one = r.parallel == kBlack;
  const float rr = one ? 96.0f : 92.0f;
  cv.fill(cv.blur(cv.shift(cv.ellipse(cx - rr, cy - rr, cx + rr, cy + rr), 5, 10), 6), 0x000000, 0.5f);
  cv.fill(cv.ellipse(cx - rr - 8, cy - rr - 8, cx + rr + 8, cy + rr + 8), kInk);
  cv.fillp(cv.ellipse(cx - rr, cy - rr, cx + rr, cy + rr), refr(r.parallel == kGoldTizm ? kGoldF : kSilverF, kCW, 4.0f, 150, 230, 3, 1.6f), 1.0f, 1.0f);
  TS s;
  s.outline = 6;
  s.ocol = kInk;
  s.slant = 0.15f;
  text(cv, stamp_of(r), cx, cy + 2, one ? 74.0f : 54.0f, 0xFFFFFF, s);
}

// B's front (1080 x 1512, its crest die-cut: see-through above its shoulders)
inline std::vector<uint8_t> b_front(const Card& c, const Ref& r, const FrontIn& in) {
  const float W = kCW, H = kCH;
  Cv cv(kFrontW, kFrontH);
  const int par = r.parallel;
  const bool boom = r.hit == kHitBoom;
  LKP("start");
  // r220: its die (its rarest feature; BASE today's crest, drawn exactly as before) - the card's edge, its frame (inset 14), its field
  // (30; a crown's 36), its layout's foot (the name / stats / figure / sticker up that far: room for a shield / swallowtail foot)
  const int shape = jam_shape(r);
  const bool die = shape != 0;
  const jamcut::Layout& lay = jam_layout(shape);
  const float foot = die ? lay.foot : 0.0f;
  const M outer = die ? cut_mask(shape, 0.0f) : crest_cached(cv, 0), inner = die ? cut_mask(shape, 14.0f) : crest_cached(cv, 1),
          fld = die ? cut_mask(shape, lay.fieldIn) : crest_cached(cv, 2);
  LKP("crests");
#ifdef HARNESS_REAL_CARDS
  jam_probe() = JamProbe{};
  jam_probe().shape = shape;
#endif
  cv.fill(outer, kInk);
  if (par == kBase) cv.fill(inner, die && boom ? kYel : 0xFFFFFFu);  // (a TIZM BOOM die: its yellow comic frame)
  else cv.fillp(inner, refr(fk_of(par), W, par == kBlack ? 0.2f : 1.0f, 150, 230, 3, par == kBlack ? 1.8f : 1.0f), 1.0f, 1.0f);
  if (die) {  // its frame's rules (the mockups': SILVER an ink line, GOLD dark-gold + cream, BLACK neon magenta + teal, SIGNED teal, BOOM ink + magenta)
    if (par == kSilver) cv.fill(cut_ring(shape, 21, 24), kInk, 0.75f);
    else if (par == kGoldTizm) cv.fill(cut_ring(shape, 27, 31), 0x5A3A06), cv.fill(cut_ring(shape, 31, 33), 0xFFF4C8, 0.8f);
    else if (par == kBlack) cv.fill(cut_ring(shape, 17, 22), kMag), cv.fill(cut_ring(shape, 30, 33), kTeal, 0.9f);
    else if (r.hit == kHitSigned) cv.fill(cut_ring(shape, 20, 24), kTeal);
    else if (boom) cv.fill(cut_ring(shape, 20, 24), kInk), cv.fill(cut_ring(shape, 30, 33), kMag);
    LKP("rules");
  }
  if (boom && in.boomPage) {  // a TIZM BOOM: its printed comic page as the field
    const Img& g = *in.boomPage;
    cv.fillp(fld, [&](float x, float y) {
      float s[4];
      sample(g, x, y, s);
      return s[3] > 1e-4f ? F3{s[0] / s[3], s[1] / s[3], s[2] / s[3]} : F3{250, 243, 225};
    });
  } else {
    const MarbleField mf(0x96u + static_cast<uint32_t>(c.num) * 7919u, par, c.c1, kFrontW, kFrontH);
    for (int y = 0; y < fld.h; ++y)
      for (int x = 0; x < fld.w; ++x) {
        const float a = fld.a[static_cast<size_t>(y) * fld.w + x];
        if (a <= 0.0f) continue;
        const float X = fld.x0 + x + 0.5f, Y = fld.y0 + y + 0.5f;
        float fo = 0.0f;
        const F3 col = mf.at(X, Y, &fo);
        cv.over(static_cast<size_t>(fld.y0 + y) * cv.W + fld.x0 + x, col, a, fo);
      }
    LKP("marble");
    // big halftone dots, bigger toward the bottom right
    cv.fill(dots_mask(cv, fld, 26, [&](float x, float y) { return sat((x / W) * 0.5f + (y / H) * 0.8f - 0.35f) * 0.8f; }, 15), par != kBlack ? kInk : 0x3A2A55,
            par != kSilver ? 0.28f : 0.18f);
    // a giant ghost TIZM down the right side
    const TM g = text_masks(cv, "TIZM", W - 40, 760, 330, 'c', 0.2f, 1.0f, 90, 0, 0);
    cv.fill(Cv::inter(g.fill, fld), 0xFFFFFF, 0.12f);
    // speed swooshes behind the character
    const uint32_t sc[3] = {kYel, kMag, kTeal};
    for (int i = 0; i < 3; ++i) {
      const float yy = 700.0f + 60.0f * i;
      cv.fill(Cv::inter(cv.poly({30, yy + 140, W - 30, yy - 260, W - 30, yy - 230, 30, yy + 170}), fld), par == kBase ? sc[i] : 0xFFFFFF,
              par == kBase ? 0.85f : 0.35f);
    }
  }
  const bool window = in.special == kSpWindow || in.special == kSpGlass;
  if (window) {
    cv.cut.assign(static_cast<size_t>(cv.W) * cv.H, 0);
    const M hole = cv.rrect(kHoleX0, kHoleY0, kHoleX1, kHoleY1, 24);
    for (int y = 0; y < hole.h; ++y)
      for (int x = 0; x < hole.w; ++x)
        if (hole.a[static_cast<size_t>(y) * hole.w + x] > 0.5f) cv.cut[static_cast<size_t>(hole.y0 + y) * cv.W + hole.x0 + x] = 1;
  } else if (in.art) {
    if (die && shape / 2 == jamcut::kCutStar && !boom) {  // r220: the 1/1's light - rays from behind its figure (the mockup's)
      M all;
      for (int i = 0; i < 14; ++i) {
        const float a = i * kPi / 7 + 0.1f;
        const M t = cv.poly({540, 700, 540 + 1500 * std::cos(a - 0.05f), 700 + 1500 * std::sin(a - 0.05f), 540 + 1500 * std::cos(a + 0.05f),
                             700 + 1500 * std::sin(a + 0.05f)});
        all = all.empty() ? t : cv.uni(all, t);
      }
      cv.add(Cv::inter(cv.blur(all, 8), fld), 0xFF7AE0, 0.18f);
    }
    front_figure(cv, art_of(in.art), true, &fld, par != kBlack ? 0xFFFFFFu : 0x18E8FFu, foot);
  }
  LKP("figure");
  // the mark in the crest (r220: where its die puts it - a crown's plate a little lower, the star crown's lower and narrower)
  {
    const float pk = lay.plateW / 500.0f, pcx = lay.plateCx, py0 = lay.plateY0;
    const M plate = cv.rrect(pcx - lay.plateW / 2, py0, pcx + lay.plateW / 2, py0 + 146 * pk, 40 * pk);
    cv.fill(plate, kInk);
    logo(cv, pcx - 190 * pk, py0 + 10 * pk, 0.98f * pk);
#ifdef HARNESS_REAL_CARDS
    jam_probe().plate = plate;
#endif
  }
  if (die) {  // its ornaments: a crown's ball finials (gold / the 1/1's magenta), the star crown's TIZM gem; a shiny's sparkles by its notch
    for (int i = 0; i < lay.ballN; ++i) jam_sphere(cv, lay.balls[i][0], lay.balls[i][1], lay.balls[i][2] - 6, par == kGoldTizm);
    if (lay.gem[2] > 0.0f) gem(cv, lay.gem[0], lay.gem[1], lay.gem[2], nullptr, kInk, 5);
    if (r.shiny && lay.sparkY > 0.0f) {
      jam_sparkle(cv, 78, lay.sparkY - 40, 20, 0xFFF6B0);
      jam_sparkle(cv, 104, lay.sparkY + 30, 13, 0xFFF6B0);
      jam_sparkle(cv, 48, lay.sparkY + 70, 10, 0xFFF6B0);
    }
  }
  if (par != kBase) b_ribbon(cv, outer, kParNameA[par], par);
  b_medallion(cv, c, par, W - 160, 300, 118);
#ifdef HARNESS_REAL_CARDS
  jam_probe().medal = cv.ellipse(W - 160 - 118, 300 - 118, W - 160 + 118, 300 + 118);
#endif
  {  // its series sticker under the medallion; shiny / level stickers under that
    const std::string sn = kSeriesName[c.series];
    const float sw = text_width(sn, 36) + 50;
    const M pill = cv.rrect(W - 160 - sw / 2, 446, W - 160 + sw / 2, 494, 24);
#ifdef HARNESS_REAL_CARDS
    jam_probe().pill = pill;
#endif
    cv.fill(cv.grow(pill, 5), kInk);
    cv.fill(pill, kSeriesRgb[c.series]);
    text(cv, sn, W - 160, 470, 36, kSeriesInk[c.series]);
    float y = 512;
    if (r.shiny) {
      const float pw = text_width("SHINY", 34) + 46;
      const M sp = cv.rrect(W - 160 - pw / 2, y, W - 160 + pw / 2, y + 48, 24);
      cv.fill(cv.grow(sp, 5), kInk);
      cv.fillp(sp, refr(kSilverF, W, 4.2f, 150, 230, 3, 1.6f), 1.0f, 1.0f);
      text(cv, "SHINY", W - 160, y + 24, 34, 0x2A0A5A);
      y += 62;
    }
    if (in.level > 1) {
      const float lw = in.level * 40.0f + 24.0f;
      const M lp = cv.rrect(W - 160 - lw / 2, y, W - 160 + lw / 2, y + 50, 25);
      cv.fill(cv.grow(lp, 5), kInk);
      cv.fill(lp, 0x120A24);
      for (int i = 0; i < in.level; ++i) star(cv, W - 160 - (in.level - 1) * 20.0f + i * 40.0f, y + 25, 15, kYel, kInk);
    }
  }
  if (boom && in.boomBadge) {
    paste(cv, *in.boomBadge, 40, 190, 376, 342);
    TS b;
    b.slant = 0.1f;
    b.xscale = 0.92f;
    b.angle = 4.0f;
    b.outline = 5;
    b.ocol = 0xFFFFFF;
    text(cv, "TIZM BOOM!", 204, 262, 44, kPpRed, b);
  }
  if (r.parallel == kGoldTizm || r.parallel == kBlack) {
    const float sy = (r.parallel == kBlack || r.hit == kHitSigned ? 988.0f : 1010.0f) - foot;
    b_serial(cv, r, 165, sy);
#ifdef HARNESS_REAL_CARDS
    jam_probe().serial = cv.ellipse(165 - 104, sy - 104, 165 + 104, sy + 104);
#endif
  }
  if (r.parallel == kBlack || r.hit == kHitSigned) b_auto_sticker(cv, foot);
  // the name: huge extruded block letters (r220: up its die's foot)
  const float gy0 = 1130 - foot, gy1 = 1260 - foot;
  const Lin fb({{0, 0xFFF36A}, {0.5f, kYel}, {1, kOrng}}, 90, gy0, gy1), fs({{0, 0xFFFFFF}, {0.55f, 0xDDE3EE}, {0.6f, 0x9AA6BC}, {1, 0xF4F6FA}}, 90, gy0, gy1),
      fg({{0, 0xFFF4C8}, {0.5f, 0xFFD24A}, {0.58f, 0xB07A10}, {1, 0xFFE27A}}, 90, gy0, gy1), fk({{0, 0xFF8AE0}, {1, kMag}}, 90, gy0, gy1);
  auto face = [&](float x, float y) -> F3 { return par == kBase ? fb(x, y) : (par == kSilver ? fs(x, y) : (par == kGoldTizm ? fg(x, y) : fk(x, y))); };
  TM nm;
  if (c.sub.empty()) {
    nm = block_text(cv, upper(c.name), 520, 1190 - foot, 128, 900, face, par != kBlack ? kPurp : 0x5A0A4Au, 0x05030A, 10, 14, 0.2f, 4.0f);
  } else {
    nm = block_text(cv, upper(c.name), 520, 1168 - foot, 104, 900, face, par != kBlack ? kPurp : 0x5A0A4Au, 0x05030A, 8, 11, 0.2f, 4.0f);
    TS s;
    s.outline = 5;
    s.ocol = kInk;
    s.slant = 0.15f;
    s.angle = 4.0f;
    s.room = 700;
    text(cv, upper(c.sub), 540, 1246 - foot, 40, kYel, s);
  }
#ifdef HARNESS_REAL_CARDS
  jam_probe().name = nm.stroke;
#endif
  (void)nm;
  LKP("mark+name");
  b_ticket(cv, c, par, in.total, W, H, shape);
  LKP("ticket");
  if (die) {  // the die: nothing outside its cut (the ribbon, the ornaments drawn past it - and the canvas's own cover)
    for (size_t i = 0; i < outer.a.size(); ++i) cv.c[i * 4 + 3] *= outer.a[i];
  }
  return bytes(cv, true);
}

// r223: a mask turned left-right on the front's canvas (kFrontW wide)
inline M mirror_x(const M& m) {
  M o = m;
  o.x0 = kFrontW - (m.x0 + m.w);
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) o.a[static_cast<size_t>(y) * m.w + x] = m.a[static_cast<size_t>(y) * m.w + (m.w - 1 - x)];
  return o;
}
// r223: a die as a card's BACK picture needs it - the back is drawn as you read it and shows mirrored on the card (xr_cards card_box /
// grid_model: its back's u = 1 - x), so its cut must be the die turned left-right to sit on the card's own edge (r220 used the front's:
// TIZM BOOM's burst points and a shiny's sparkle notch landed on the wrong side - seen from behind, the burst's tips see-through inside
// their outline)
inline M back_cut_mask(int shape, float inset) { return mirror_x(cut_mask(shape, inset)); }
// B's back (drawn 1080 x 1512, crest-cut; r220: `shape` a card's own die - its back once it's turned over / out of the binder)
inline Cv b_back_cv(int shape = 0) {
  const float W = kCW, H = kCH;
  Cv cv(kFrontW, kFrontH);
  cv.fill(shape != 0 ? back_cut_mask(shape, 0.0f) : crest_cached(cv, 0), kInk);
  cv.fillp(shape != 0 ? back_cut_mask(shape, 14.0f) : crest_cached(cv, 1), refr(kSilverF, W, 0.4f, 150, 230, 3, 1.4f), 1.0f, 1.0f);
  const M fld = shape != 0 ? back_cut_mask(shape, 30.0f) : crest_cached(cv, 2);
  const Marble m(11, 260.0f, kFrontW, kFrontH);
  cv.fillp(fld, [&](float x, float y) { return ramp(poster(m(x, y)), {{0, 0x12062A}, {0.35f, kPurp}, {0.55f, 0x4A1A8A}, {0.75f, 0x1A6AAA}, {1, kTeal}}); });
  cv.fill(dots_mask(cv, fld, 26, [&](float x, float y) { return sat(std::hypot(x - W / 2, y - 560) / 900.0f); }, 15), 0x000000, 0.3f);
  for (int i = 0; i < 16; ++i) {  // rays from the gem
    const float a = i * kPi / 8;
    cv.fill(Cv::inter(cv.blur(cv.poly({W / 2, 560, W / 2 + 1400 * std::cos(a - 0.08f), 560 + 1400 * std::sin(a - 0.08f), W / 2 + 1400 * std::cos(a + 0.08f),
                                       560 + 1400 * std::sin(a + 0.08f)}),
                              6),
                      fld),
            0xFFFFFF, 0.06f);
  }
  cv.fill(cv.hexagon(W / 2, 520, 270), kInk);
  cv.fill(cv.hexagon(W / 2, 520, 252), 0xFFFFFF);
  gem(cv, W / 2, 520, 228, nullptr, kInk, 10);
  block_text(cv, "LANCINI TIZM", W / 2, 900, 112, 900, Lin({{0, 0xFFFFFF}, {0.55f, 0xDDE3EE}, {0.6f, 0x9AA6BC}, {1, 0xF4F6FA}}, 90, 850, 960), kPurp, 0x05030A,
             10, 14, 0.2f, 3.0f);
  TS o;
  o.outline = 4;
  o.ocol = kInk;
  text(cv, "2026-27 STRIKERS  -  JAM '96", W / 2, 1030, 46, 0xFFFFFF, o);
  const float bf = jam_back_foot(shape), cf = 0.4f * bf;  // (r220: up a swallowtail / pennant foot)
  block_text(cv, "CHASE THE 1/1", W / 2, 1220 - cf, 104, 860, Lin({{0, 0xFFF36A}, {0.5f, kYel}, {1, kOrng}}, 90, 1170 - cf, 1280 - cf), kPurp, 0x05030A, 10, 14, 0.2f, -3.0f);
  text(cv, "A STRIKERS SUPER LEAGUE COLLECTION", W / 2, 1410 - bf, 34, 0xB8A8E8);
  if (shape != 0) {  // (its die: nothing past its cut - r223: the die as the back sits on it)
    const M outer = back_cut_mask(shape, 0.0f);
    for (size_t i = 0; i < outer.a.size(); ++i) cv.c[i * 4 + 3] *= outer.a[i];
  }
  (void)H;
  return cv;
}

// a canvas shrunk to w x h (area average, premultiplied; foil carried) - the backs are drawn at the front's size
inline std::vector<uint8_t> shrink(const Cv& cv, int w, int h, bool foil) {
  std::vector<uint8_t> big = bytes(cv, false), out(static_cast<size_t>(w) * h * 4, 0);
  const float kx = static_cast<float>(cv.W) / w, ky = static_cast<float>(cv.H) / h;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int x0 = static_cast<int>(x * kx), x1 = std::max(x0 + 1, static_cast<int>((x + 1) * kx)), y0 = static_cast<int>(y * ky),
                y1 = std::max(y0 + 1, static_cast<int>((y + 1) * ky));
      float s[4] = {0, 0, 0, 0}, fo = 0;
      int n = 0;
      for (int yy = y0; yy < y1 && yy < cv.H; ++yy)
        for (int xx = x0; xx < x1 && xx < cv.W; ++xx, ++n) {
          const size_t i = static_cast<size_t>(yy) * cv.W + xx;
          const float a = big[i * 4 + 3] / 255.0f;
          s[0] += big[i * 4] * a, s[1] += big[i * 4 + 1] * a, s[2] += big[i * 4 + 2] * a, s[3] += a;
          fo += cv.foil[i] * a;
        }
      uint8_t* q = &out[(static_cast<size_t>(y) * w + x) * 4];
      if (n == 0 || s[3] <= 0.0f) continue;
      for (int k = 0; k < 3; ++k) q[k] = static_cast<uint8_t>(std::lround(std::min(255.0f, s[k] / s[3])));
      const int a = static_cast<int>(std::lround(s[3] / n * 255.0f));
      q[3] = static_cast<uint8_t>(a);
      if (a >= 255 && foil) q[3] = static_cast<uint8_t>(255 - (static_cast<int>(std::lround(fo / s[3] * 255.0f)) * 127 + 127) / 255);
    }
  return out;
}

// ================================================================================================= HOMETOWN (landscape)
// r149's HOMETOWN, drawn LANDSCAPE (kHomeW x kHomeH; draw_front turns it onto the card): its home in the window (flat: its home's
// picture + the character; on show: see-through, its diorama behind), HOMETOWN on a ribbon + its place, the rating, the name along
// the foot, the finish / shiny / level, the stats. A: chrome; B: the Jam crest (the card's top = this picture's right edge), marble
inline std::vector<uint8_t> home_front_lk(const Card& c, const Ref& r, const FrontIn& in, bool window) {
  const float W = static_cast<float>(kHomeW), H = static_cast<float>(kHomeH);
  const bool jam = c.set == 1;
  Cv cv(kHomeW, kHomeH);
  const int par = r.parallel;
  // r220: a Jam HOMETOWN's die is the PENNANT (its swallowtail at this picture's left end = the card's foot): the window in from it
  const int shape = jam ? jam_shape(r) : 0;
  const float wx0 = jam ? kHomeWinX0 + jam_layout(shape).foot + 2.0f : kHomeWinX0, wy0 = kHomeWinY0, wx1 = jam ? 1366.0f : kHomeWinX1, wy1 = kHomeWinY1;
  M inner, outerJ;
  if (!jam) {
    const M card = cv.rrect(0, 0, W, H, 48);
    inner = cv.rrect(22, 22, W - 22, H - 22, 40);
    if (par == kBase) cv.fill(card, 0xF7F7F7);
    else cv.fillp(Cv::minus(card, inner), refr(fk_of(par), W, 1.3f, 90, 140, 8, 1, -62, H), 1.0f, kStock[par].fFrame);
    const float ca = std::cos(-62.0f * kPi / 180.0f), sa = std::sin(-62.0f * kPi / 180.0f);
    const Lin team({{0, mix_rgb(c.c1, 0x000000, 0.45f)}, {1, mix_rgb(c.c1, 0x000000, 0.8f)}}, 90, 0, H);
    cv.fillp(inner, [&](float x, float y) { return team(x, y) * (0.86f + 0.24f * shards(x, y, ca, sa, 150, 230, 3)); }, 1.0f, par == kBase ? 0.0f : 0.5f);
  } else {
    outerJ = cut_mask_land(shape, 0.0f);
    cv.fill(outerJ, kInk);
    const M fr = cut_mask_land(shape, 14.0f);
    if (par == kBase) cv.fill(fr, 0xFFFFFF);
    else cv.fillp(fr, refr(fk_of(par), W, 1.0f, 150, 230, 3, 1, -62, H), 1.0f, 1.0f);
    inner = cut_mask_land(shape, 30.0f);
    const MarbleField mf(0x96u + static_cast<uint32_t>(c.num) * 7919u, par, c.c1, kHomeW, kHomeH);
    for (int y = 0; y < inner.h; ++y)
      for (int x = 0; x < inner.w; ++x) {
        const float a = inner.a[static_cast<size_t>(y) * inner.w + x];
        if (a <= 0.0f) continue;
        float fo = 0.0f;
        const F3 col = mf.at(inner.x0 + x + 0.5f, inner.y0 + y + 0.5f, &fo);
        cv.over(static_cast<size_t>(inner.y0 + y) * cv.W + inner.x0 + x, col, a, fo);
      }
    cv.fill(dots_mask(cv, inner, 26, [&](float x, float y) { return sat((x / W) * 0.5f + (y / H) * 0.8f - 0.35f) * 0.8f; }, 15), kInk, 0.22f);
  }
  // the window: its gold ring, its home + the character (flat), or see-through (its diorama on show)
  cv.fill(cv.rrect(wx0 - 12, wy0 - 12, wx1 + 12, wy1 + 12, 32), jam ? kInk : 0x0B0F1Au);
  cv.fillp(cv.rrect(wx0 - 7, wy0 - 7, wx1 + 7, wy1 + 7, 28), refr(kGoldF, W, 2.0f, 150, 230, 3, 1, -62, H), 1.0f, 1.0f);
  const M win = cv.rrect(wx0, wy0, wx1, wy1, 24);
  if (window) {
    cv.cut.assign(static_cast<size_t>(cv.W) * cv.H, 0);
    for (int y = 0; y < win.h; ++y)
      for (int x = 0; x < win.w; ++x)
        if (win.a[static_cast<size_t>(y) * win.w + x] > 0.5f) cv.cut[static_cast<size_t>(win.y0 + y) * cv.W + win.x0 + x] = 1;
  } else {
    if (in.home && !in.home->empty()) {
      const Img& g = *in.home;
      const float sx = static_cast<float>(g.w) / (wx1 - wx0), sy = static_cast<float>(g.h) / (wy1 - wy0);
      const float s = std::min(sx, sy);  // (cover the window, centred)
      const float ox = 0.5f * (g.w - (wx1 - wx0) * s), oy = 0.5f * (g.h - (wy1 - wy0) * s);
      cv.fillp(win, [&](float x, float y) {
        float smp[4];
        sample(g, ox + (x - wx0) * s, oy + (y - wy0) * s, smp);
        return smp[3] > 1e-4f ? F3{smp[0] / smp[3], smp[1] / smp[3], smp[2] / smp[3]} : F3{106, 176, 232};
      });
    } else {
      cv.fillp(win, Lin({{0, 0x6AB0E8}, {0.7f, 0xBFE0F4}, {0.71f, 0x3A8A4A}, {1, 0x2A6A3A}}, 90, wy0, wy1));
    }
    cv.glow(wx0 + 300, wy0 + 140, wx1 - 300, wy1, 0xFFFFFF, 0.2f);
    if (in.art) {
      const Art a = art_of(in.art);
      if (a.ok() && !a.photo) {
        Fig f;
        f.shadow = 0.45f;
        if (jam) f.keyline = 10, f.keyCol = 0xFFFFFF, f.keyInk = kInk;
        else f.rim = 0xFFFFFF, f.rimA = 0.5f;
        f.clip = &win;
        figure(cv, a, fit(a, 0.5f * (wx0 + wx1) - 360, wy0 + 30, 0.5f * (wx0 + wx1) + 360, wy1 - 6, 'b', 1.0f), f);
      }
    }
  }
  // HOMETOWN on its ribbon; its place (r220: a Jam pennant's ribbon a little in from its swallowtail)
  const float rx = jam ? 14.0f : 0.0f;
  cv.fill(cv.poly({40 + rx, 34, 760, 34, 730, 92, 760, 150, 40 + rx, 150, 70 + rx, 92}), jam ? kInk : 0x7A1414u);
  cv.fill(cv.poly({52 + rx, 44, 744, 44, 716, 92, 744, 140, 52 + rx, 140, 80 + rx, 92}), jam ? kMag : 0xD22A2Au);
  TS h;
  h.slant = 0.12f;
  h.xscale = 0.95f;
  h.outline = 4;
  h.ocol = jam ? kInk : 0x3A0808u;
  text(cv, "HOMETOWN", 398, 94, 76, 0xFFFFFF, h);
  const float pw = std::min(520.0f, text_width(in.place, 44) + 60);
  cv.fill(cv.rrect(790, 52, 790 + pw, 136, 42), jam ? kInk : kNavyA, 0.94f);
  cv.fill(cv.ring(790, 52, 790 + pw, 136, 42, 4), jam ? kYel : kGoldA);
  TS pl;
  pl.room = pw - 30;
  text(cv, in.place, 790 + pw / 2, 94, 44, 0xFFFFFF, pl);
  // its rating
  if (!jam) {
    a_rating(cv, c, par, W - 120, 104, 0, 0.72f, W);
  } else {
    b_medallion(cv, c, par, 1250, 104, 84);
  }
  // along the foot: its name, its finish / shiny / level, its stats, its number
  TS n;
  n.align = 'l';
  n.slant = 0.16f;
  n.xscale = 0.92f;
  n.outline = 6;
  n.ocol = jam ? kInk : 0x05070Cu;
  n.shadow = 5;
  n.room = 690;
  if (jam) {
    TM m = text_masks(cv, upper(c.name), 60, 968, 84, 'l', 0.2f, 1.0f, 0, 690, 7);
    for (int i = 6; i >= 1; --i) cv.fill(cv.shift(m.stroke, i * 1.2f, i * 1.6f), mix_rgb(kPurp, 0x05030A, i / 6.0f));
    cv.fill(m.stroke, kInk);
    cv.fillp(m.fill, Lin({{0, 0xFFF36A}, {0.5f, kYel}, {1, kOrng}}, 90, 930, 1006));
  } else {
    text(cv, upper(c.name), 60, 968, 80, 0xFFFFFF, n);
  }
  if (!c.sub.empty()) {
    TS sb;
    sb.align = 'l';
    sb.outline = 3;
    sb.room = 400;
    text(cv, upper(c.sub), 64, 922, 28, jam ? kYel : kGoldA, sb);
  }
  float tx = 790;
  if (par != kBase) {
    const std::string pn = kParNameA[par];
    const float w = text_width(pn, 34) + 44;
    cv.fill(cv.rrect(tx - 3, 941, tx + w + 3, 997, 28), 0x0B0F1A);
    if (par == kBlack) cv.fill(cv.rrect(tx, 944, tx + w, 994, 25), 0x0B0C10);
    else cv.fillp(cv.rrect(tx, 944, tx + w, 994, 25), refr(fk_of(par), W, 3.0f, 150, 230, 3, 1, -62, H), 1.0f, 1.0f);
    text(cv, pn, tx + w / 2, 969, 34, par == kBlack ? 0xFFFFFFu : (par == kGoldTizm ? 0x3A2600u : 0x101830u));
    tx += w + 14;
  }
  if (r.shiny) {
    cv.fill(cv.rrect(tx - 3, 941, tx + 153, 997, 28), 0x0B0F1A);
    cv.fillp(cv.rrect(tx, 944, tx + 150, 994, 25), refr(kSilverF, W, 4.2f, 150, 230, 3, 1.6f, -62, H), 1.0f, 1.0f);
    text(cv, "SHINY", tx + 75, 969, 34, 0x2A0A5A);
    tx += 164;
  }
  for (int s = 0; s < in.level && in.level > 1; ++s) star(cv, tx + 20 + s * 40.0f, 969, 16, jam ? kYel : kGoldA);
  if (r.parallel == kGoldTizm || r.parallel == kBlack) {  // its number in its run
    TS st;
    st.outline = 4;
    st.ocol = 0x0B0F1A;
    cv.fill(cv.rrect(1220, 930, 1430, 1000, 16), 0x0B0F1A);
    cv.fillp(cv.rrect(1226, 936, 1424, 994, 12), refr(r.parallel == kGoldTizm ? kGoldF : kSilverF, W, 4.0f, 150, 230, 3, 1.6f, -62, H), 1.0f, 1.0f);
    text(cv, stamp_of(r), 1325, 966, 46, r.parallel == kGoldTizm ? 0x2A1A00u : 0x0B0F1Au);
  }
  static const char* const kStatN[4] = {"SPEED", "SHOOTING", "PASSING", "SLIDE TACKLE"};
  cv.fill(cv.rrect(40, 1006, 1190, 1060, 22), jam ? 0x120A24u : kNavyA, 0.94f);
  static const uint32_t kNeon[4] = {kTeal, kMag, kYel, 0x7CF05A};
  for (int i = 0; i < 4; ++i) {
    const float lx = 60 + i * 282.0f;
    TS l;
    l.align = 'l';
    l.room = 150;
    text(cv, kStatN[i], lx, 1033, 26, 0xC8D2E6, l);
    const float px0 = lx + std::min(150.0f, text_width(kStatN[i], 26)) + 10;
    const int lit = lit_of(c.stats[i]);
    for (int s = 0; s < 5; ++s) cv.fill(cv.rrect(px0 + s * 20, 1024, px0 + s * 20 + 15, 1042, 6), s < lit ? (jam ? kNeon[i] : kGoldA) : 0x2A3354u);
  }
  TS rr;
  rr.align = 'r';
  rr.outline = 2;
  rr.ocol = jam ? kInk : 0x0B0F1Au;
  text(cv, num3(c.num) + "/" + std::to_string(in.total), jam ? 1360.0f : 1440.0f, 1034, 32, jam ? kYel : kGoldA, rr);
  if (jam)  // r220: its die - nothing past its cut
    for (size_t i = 0; i < outerJ.a.size(); ++i) cv.c[i * 4 + 3] *= outerJ.a[i];
  return bytes(cv, true);
}

// ================================================================================================= the packs
// the pack's trio (common.py trio_layout - the mockups' "nobody's face covered" rule): the widest figure (Charizard, wings) at the
// back, centred and highest; the two others in front, lower, pushed out to the sides (drawn back -> left -> right). No figure's
// HEAD BOX may be covered more than 1% by any figure drawn after it (on their alpha, a quarter size): the front figures step outwards
// (and shrink 3% every 4th step) until it holds. Heads: measured on the 256 portraits (Mario, Charizard, Spider-Man).
struct Star {
  Art a;
  float hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;  // its head box (its trimmed picture's px)
};
struct TrioOut {
  float x[3], y[3], h[3];
  float worst = 0;  // the most of any face covered (0..1)
  int steps = 0;
};
inline Star star_of(const ImgP& img, float hx0, float hy0, float hx1, float hy1) {
  Star s;
  s.a = art_of(img);
  s.hx0 = hx0 - s.a.x0, s.hy0 = hy0 - s.a.y0, s.hx1 = hx1 - s.a.x0, s.hy1 = hy1 - s.a.y0;
  return s;
}
inline TrioOut trio_layout(const Star st[3], float rx0, float ry0, float rx1, float ry1, int W, int H) {
  const float RW = rx1 - rx0, RH = ry1 - ry0;
  TrioOut o;
  const float hb = 0.74f * RH, wb = st[0].a.w() * hb / std::max(1.0f, st[0].a.h());
  o.x[0] = rx0 + (RW - wb) / 2, o.y[0] = ry0, o.h[0] = hb;
  o.x[1] = rx0 - 0.02f * RW, o.h[1] = 0.60f * RH;
  o.h[2] = 0.86f * RH;
  o.x[2] = rx1 - st[2].a.w() * o.h[2] / std::max(1.0f, st[2].a.h()) + 0.02f * RW;
  const int q = 4, gw = W / q, gh = H / q;
  for (int it = 0; it < 120; ++it) {
    o.y[1] = ry1 - o.h[1];
    o.y[2] = ry1 - o.h[2];
    std::vector<uint8_t> mk[3];
    for (int k = 0; k < 3; ++k) {
      mk[k].assign(static_cast<size_t>(gw) * gh, 0);
      const Star& s = st[k];
      if (!s.a.ok()) continue;
      const float sc = o.h[k] / s.a.h(), w = s.a.w() * sc;
      for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x) {
          const float X = (x + 0.5f) * q, Y = (y + 0.5f) * q;
          if (X < o.x[k] || X >= o.x[k] + w || Y < o.y[k] || Y >= o.y[k] + o.h[k]) continue;
          float smp[4];
          sample(*s.a.img, s.a.x0 + (X - o.x[k]) / sc, s.a.y0 + (Y - o.y[k]) / sc, smp);
          mk[k][static_cast<size_t>(y) * gw + x] = smp[3] > 60.0f / 255.0f ? 1 : 0;
        }
    }
    float worst = 0;
    int mover = -1;
    for (int k = 0; k < 3; ++k) {
      const Star& s = st[k];
      const float sc = o.h[k] / std::max(1.0f, s.a.h());
      const int hx0 = static_cast<int>((o.x[k] + s.hx0 * sc) / q), hy0 = static_cast<int>((o.y[k] + s.hy0 * sc) / q);
      const int hx1 = static_cast<int>((o.x[k] + s.hx1 * sc) / q), hy1 = static_cast<int>((o.y[k] + s.hy1 * sc) / q);
      int face = 0;
      int cov[3] = {0, 0, 0};
      for (int y = std::max(0, hy0); y < std::min(gh, hy1); ++y)
        for (int x = std::max(0, hx0); x < std::min(gw, hx1); ++x) {
          const size_t i = static_cast<size_t>(y) * gw + x;
          if (!mk[k][i]) continue;
          ++face;
          for (int l = k + 1; l < 3; ++l) cov[l] += mk[l][i];
        }
      for (int l = k + 1; l < 3; ++l) {
        const float c = static_cast<float>(cov[l]) / std::max(1, face);
        if (c > 0.01f && c > worst) worst = c, mover = l;
        o.worst = std::max(o.worst, 0.0f);
      }
    }
    o.steps = it + 1;
    // (the report: the most of a face covered after this step's placement)
    if (mover < 0) {
      o.worst = 0.0f;
      // measure exactly (no threshold) for the log
      float most = 0.0f;
      for (int k = 0; k < 3; ++k) {
        const float sc = o.h[k] / std::max(1.0f, st[k].a.h());
        const int hx0 = static_cast<int>((o.x[k] + st[k].hx0 * sc) / q), hy0 = static_cast<int>((o.y[k] + st[k].hy0 * sc) / q);
        const int hx1 = static_cast<int>((o.x[k] + st[k].hx1 * sc) / q), hy1 = static_cast<int>((o.y[k] + st[k].hy1 * sc) / q);
        int face = 0, cov = 0;
        for (int y = std::max(0, hy0); y < std::min(gh, hy1); ++y)
          for (int x = std::max(0, hx0); x < std::min(gw, hx1); ++x) {
            const size_t i = static_cast<size_t>(y) * gw + x;
            if (!mk[k][i]) continue;
            ++face;
            bool c = false;
            for (int l = k + 1; l < 3; ++l) c = c || mk[l][i];
            cov += c ? 1 : 0;
          }
        most = std::max(most, static_cast<float>(cov) / std::max(1, face));
      }
      o.worst = most;
      break;
    }
    o.worst = worst;
    if (mover == 1) {
      o.x[1] -= 10;
      if (it % 4 == 3) o.h[1] *= 0.97f;
    } else {
      o.x[2] += 10;
      if (it % 4 == 3) o.h[2] *= 0.97f;
    }
  }
  return o;
}
inline void draw_trio(Cv& cv, const Star st[3], const TrioOut& o, const Fig& f) {
  for (int k = 0; k < 3; ++k) {
    if (!st[k].a.ok()) continue;
    Placed p;
    p.s = o.h[k] / st[k].a.h();
    p.x0 = o.x[k], p.y0 = o.y[k], p.x1 = o.x[k] + st[k].a.w() * p.s, p.y1 = o.y[k] + o.h[k];
    figure(cv, st[k].a, p, f);
  }
}
// the crimped ends (serrated, ribbed)
template <class P>
void crimps(Cv& cv, P paint, float fo) {
  const float PW = static_cast<float>(cv.W), PH = static_cast<float>(cv.H);
  for (int end = 0; end < 2; ++end) {
    const float y0 = end == 0 ? 0.0f : PH - 96, y1 = end == 0 ? 96.0f : PH;
    M m = cv.rrect(0, y0 + 8, PW, y1 - 8, 6);
    for (float x = 0; x < PW; x += 30)
      m = cv.uni(m, end == 0 ? cv.poly({x, 16, x + 15, 1, x + 30, 16}) : cv.poly({x, PH - 16, x + 30, PH - 16, x + 15, PH - 1}));
    cv.fillp(m, paint, 1.0f, fo);
    for (float x = 14; x < PW; x += 22) {
      cv.fill(cv.rrect(x, y0 + 20, x + 6, y1 - 20, 3), 0x000000, 0.18f);
      cv.fill(cv.rrect(x + 7, y0 + 20, x + 10, y1 - 20, 2), 0xFFFFFF, 0.3f);
    }
  }
}
inline void tear_line(Cv& cv, uint32_t col) {
  const float PW = static_cast<float>(cv.W);
  for (float x = 40; x < PW - 60; x += 36) cv.fill(cv.rrect(x, 129, x + 20, 135, 3), col, 0.85f);
  cv.fill(cv.poly({PW - 46, 120, PW - 26, 132, PW - 46, 144}), col);
}
// the three stars (draw_pack's portraits: Mario, Charizard, Spider-Man) as Stars, back -> left -> right
inline void pack_stars(const ImgP mario, const ImgP charizard, const ImgP spidey, Star out[3]) {
  out[0] = star_of(charizard, 130, 55, 185, 115);
  out[1] = star_of(mario, 95, 5, 245, 140);
  out[2] = star_of(spidey, 112, 5, 150, 40);
}
inline float g_trioWorst[2] = {-1.0f, -1.0f};  // (the last packs' faces covered, for the log / the proof)

// A's pack (900 x 1380)
inline Cv a_pack_cv(const Star st[3]) {
  const float PW = kPW, PH = kPH;
  Cv cv(kPackW, kPackH);
  const M body = cv.rrect(0, 80, PW, PH - 80, 10);
  cv.fillp(body, refr(kSilverF, PW, 0.6f, 130, 200, 3, 1.0f, -62, PH), 1.0f, 0.9f);
  crimps(cv, refr(kSilverF, PW, 2.0f, 60, 60, 3, 1.0f, 0, PH), 0.95f);
  const M fld = Cv::inter(cv.poly({0, 640, PW, 520, PW, 1210, 0, 1210}), body);
  cv.fillp(fld, Lin({{0, 0x1A2550}, {1, 0x070A16}}, 90, 520, 1210), 1.0f, 0.2f);
  cv.fill(dots_mask(cv, fld, 14, [](float, float) { return 0.12f; }, 30), 0x5AD8FF, 0.25f);
  beam(cv, 300, 120, 0.25f);
  const TrioOut o = trio_layout(st, 20, 560, PW - 20, 1210, kPackW, kPackH);
  g_trioWorst[0] = o.worst;
  Fig f;
  f.shadow = 0.5f, f.rim = 0xFFFFFF, f.rimA = 0.45f;
  draw_trio(cv, st, o, f);
  tear_line(cv, 0x1A2030);
  logo(cv, 70, 168, 2.0f);
  const M band = cv.poly({0, 470, PW, 400, PW, 500, 0, 570});
  cv.fill(cv.blur(cv.shift(band, 0, 10), 8), 0x000000, 0.4f);
  cv.fill(band, 0x0B0F1A);
  cv.fillp(cv.poly({0, 562, PW, 492, PW, 500, 0, 570}), refr(kGoldF, PW, 1.0f, 150, 230, 3, 1, -62, PH), 1.0f, 1.0f);
  TS s;
  s.slant = 0.18f;
  s.angle = std::atan2(70.0f, PW) * 180.0f / kPi;
  s.outline = 3;
  text(cv, "2026-27 STRIKERS", PW / 2, 486, 70, 0xFFFFFF, s);
  cv.fill(cv.rrect(40, 1196, PW - 40, 1282, 24), 0x0B0F1A, 0.94f);
  cv.fillp(cv.ring(40, 1196, PW - 40, 1282, 24, 4), refr(kGoldF, PW, 2.0f, 150, 230, 3, 1, -62, PH), 1.0f, 1.0f);
  cv.fill(cv.hexagon(118, 1238, 74), 0x05070C);
  cv.fillp(cv.hexagon(118, 1238, 64), refr(kGoldF, PW, 0.3f, 150, 230, 3, 1, -62, PH), 1.0f, 1.0f);
  text(cv, "5", 118, 1222, 64, 0x2A1A00);
  text(cv, "CARDS", 118, 1268, 22, 0x2A1A00);
  TS c;
  c.room = 640;
  text(cv, "CHASE: SILVER - GOLD /10 - BLACK 1/1", 520, 1240, 38, 0xFFFFFF, c);
  return cv;
}
// B's pack (900 x 1380)
inline Cv b_pack_cv(const Star st[3]) {
  const float PW = kPW, PH = kPH;
  Cv cv(kPackW, kPackH);
  const M body = cv.rrect(0, 80, PW, PH - 80, 10);
  const Marble m(21, 300.0f, kPackW, kPackH);
  cv.fillp(body, [&](float x, float y) { return ramp(poster(m(x, y)), {{0, kPurp}, {0.3f, kMag}, {0.55f, 0xFF5AA8}, {0.7f, kTeal}, {0.88f, kYel}, {1, 0xFFFFFF}}); },
           1.0f, 0.45f);
  cv.fillp(body, refr(kSilverF, PW, 1.0f, 120, 180, 3, 1, -62, PH), 0.35f, 0.8f);
  cv.fill(dots_mask(cv, body, 22, [](float, float y) { return sat((y - 500.0f) / 800.0f) * 0.8f; }, 15), kInk, 0.3f);
  crimps(cv, Lin({{0, kYel}, {0.5f, 0xFFFFFF}, {1, kYel}}, 0, 0, PW), 0.7f);
  const M stripe = cv.poly({0, 560, PW, 430, PW, 560, 0, 690});
  cv.fill(cv.grow(stripe, 6), kYel);
  cv.fill(stripe, kInk);
  block_text(cv, "2026-27 STRIKERS", PW / 2, 556, 76, 780, Lin({{0, 0xFFF36A}, {0.5f, kYel}, {1, kOrng}}, 90, 520, 600), kPurp, 0x05030A, 6, 9, 0.2f,
             std::atan2(130.0f, PW) * 180.0f / kPi);
  const TrioOut o = trio_layout(st, 16, 690, PW - 16, 1196, kPackW, kPackH);
  g_trioWorst[1] = o.worst;
  Fig f;
  f.shadow = 0.45f, f.keyline = 10, f.keyCol = 0xFFFFFF, f.keyInk = kInk;
  draw_trio(cv, st, o, f);
  tear_line(cv, kInk);
  cv.fill(cv.rrect(60, 160, PW - 60, 420, 50), kInk, 0.9f);
  cv.fill(cv.ring(60, 160, PW - 60, 420, 50, 6), kYel);
  logo(cv, 92, 196, 1.3f);
  block_text(cv, "JAM '96", 706, 292, 84, 250, Lin({{0, 0xFF8AE0}, {1, kMag}}, 90, 250, 334), kPurp, 0x05030A, 6, 8, 0.2f, 6.0f);
  cv.fill(cv.rrect(36, 1196, PW - 36, 1284, 26), kInk);
  cv.fill(cv.ring(36, 1196, PW - 36, 1284, 26, 5), kTeal);
  cv.fill(cv.ellipse(40, 1150, 200, 1310), kInk);
  cv.fill(cv.ellipse(52, 1162, 188, 1298), kYel);
  TS s;
  s.outline = 5;
  s.ocol = kInk;
  text(cv, "5", 120, 1214, 70, 0xFFFFFF, s);
  text(cv, "CARDS", 120, 1268, 24, kInk);
  TS c;
  c.room = 620;
  text(cv, "CHASE: SILVER - GOLD /10 - BLACK 1/1", 535, 1240, 36, 0xFFFFFF, c);
  return cv;
}
// a pack's back (its ODDS - the same for both packs - and how you earn them)
inline Cv pack_back_cv(int set, int total) {
  const float PW = kPW, PH = kPH;
  Cv cv(kPackW, kPackH);
  const M body = cv.rrect(0, 80, PW, PH - 80, 10);
  if (set == 0) {
    cv.fillp(body, refr(kBlackF, PW, 0.0f, 150, 230, 5, 1, -62, PH), 1.0f, 0.6f);
    crimps(cv, refr(kSilverF, PW, 2.0f, 60, 60, 3, 1.0f, 0, PH), 0.95f);
    beam(cv, 420, 120, 0.18f);
  } else {
    const Marble m(33, 300.0f, kPackW, kPackH);
    cv.fillp(body, [&](float x, float y) { return ramp(poster(m(x, y)), {{0, 0x12062A}, {0.35f, kPurp}, {0.55f, 0x4A1A8A}, {0.75f, 0x1A6AAA}, {1, kTeal}}); },
             1.0f, 0.3f);
    crimps(cv, Lin({{0, kYel}, {0.5f, 0xFFFFFF}, {1, kYel}}, 0, 0, PW), 0.7f);
  }
  tear_line(cv, set == 0 ? 0x1A2030 : kInk);
  logo(cv, 220, 150, 1.25f);
  TS t;
  t.room = PW - 120;
  t.outline = 3;
  t.ocol = set == 0 ? 0x0B0F1A : kInk;
  text(cv, std::string(set == 0 ? kSetName : kJamSetName) + "  -  " + std::to_string(total) + " CARDS", PW / 2, 330, 40, 0xFFFFFF, t);
  const uint32_t panel = set == 0 ? kNavyA : 0x120A24, edge = set == 0 ? 0x2A3354 : kYel, head = set == 0 ? kGoldA : kYel;
  cv.fill(cv.rrect(60, 380, PW - 60, 1060, 30), panel, 0.92f);
  cv.fill(cv.ring(60, 380, PW - 60, 1060, 30, 4), edge);
  TS hh;
  hh.outline = 3;
  hh.ocol = t.ocol;
  text(cv, "IN EVERY PACK", PW / 2, 426, 50, head, hh);
  const double srPerPack = (kPackCards - 1) * kSlotSR + kLastSR;
  auto in = [](int n, const char* what) { return "1 IN " + xrcoin::text(n) + " " + what; };
  constexpr int kRows = 11;
  const std::string rows[kRows][2] = {{"CARDS", "5"},
                                      {"RARE OR BETTER", "EVERY PACK"},
                                      {"SUPER RARE", "ABOUT 1 IN " + std::to_string(static_cast<int>(std::lround(1.0 / srPerPack))) + " PACKS"},
                                      {"SILVER TIZM", in(kSilverOdds, "CARDS")},
                                      {"GOLD TIZM /10", in(kGoldOdds, "CARDS")},
                                      {"BLACK TIZM 1/1", in(kBlackOdds / kPackCards, "PACKS")},
                                      {"TIZM BOOM", in(kHitIn[kHitBoom], "CARDS")},
                                      {"HOMETOWN", in(kHitIn[kHitHome], "CARDS")},
                                      {"SIGNED", in(kHitIn[kHitSigned], "CARDS")},
                                      {"SHINY POKEMON", in(kShinyOdds, "POKEMON")},
                                      {"GOD PACK", in(kGodOdds, "PACKS")}};
  for (int i = 0; i < kRows; ++i) {
    const float y = 492 + i * 52.0f;
    TS l;
    l.align = 'l';
    l.room = 380;
    text(cv, rows[i][0], 100, y, 32, 0xE8ECF5, l);
    TS r;
    r.align = 'r';
    r.room = 400;
    text(cv, rows[i][1], PW - 100, y, 32, i >= 3 ? (set == 0 ? 0xFFF2A0u : kYel) : 0xFFFFFFu, r);
    if (i < kRows - 1) cv.fill(cv.rrect(100, y + 24, PW - 100, y + 27, 2), set == 0 ? 0x2A3354u : 0x3A2A5Au);
  }
  TS e;
  e.room = PW - 140;
  e.outline = 3;
  e.ocol = t.ocol;
  if (set == 0) {
    text(cv, "EARN THEM IN FRANCHISE MATCHES", PW / 2, 1130, 46, kGoldA, e);
    text(cv, "1 FOR EVERY MATCH YOU PLAY", PW / 2, 1192, 42, 0xFFFFFF, e);
    text(cv, "+1 A PLAYOFF SERIES WON  -  +1 A TROPHY", PW / 2, 1250, 40, 0xC8D8FF, e);
  } else {
    text(cv, "THE PREMIUM PACK", PW / 2, 1130, 46, kYel, e);
    text(cv, "1 FOR EVERY FRANCHISE WIN", PW / 2, 1192, 42, 0xFFFFFF, e);
    text(cv, "OR THE CARD SHOP  -  SAME ODDS, ITS OWN 1/1s", PW / 2, 1250, 36, 0xB8A8E8, e);
  }
  return cv;
}

// ================================================================================================= the boxes (the shop's pictures)
// a quad's homography (unit square -> quad: tl, tr, br, bl) and its inverse
struct Hom {
  float m[9];
};
inline Hom hom_sq(const float q[8]) {
  const float x0 = q[0], y0 = q[1], x1 = q[2], y1 = q[3], x2 = q[4], y2 = q[5], x3 = q[6], y3 = q[7];
  const float dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3, dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
  const float den = dx1 * dy2 - dx2 * dy1;
  const float g = (dx3 * dy2 - dx2 * dy3) / den, h = (dx1 * dy3 - dx3 * dy1) / den;
  Hom H;
  const float a = x1 - x0 + g * x1, b = x3 - x0 + h * x3, c = x0, d = y1 - y0 + g * y1, e = y3 - y0 + h * y3, f = y0;
  const float m[9] = {a, b, c, d, e, f, g, h, 1.0f};
  std::memcpy(H.m, m, sizeof(m));
  return H;
}
inline Hom hom_inv(const Hom& A) {
  const float* m = A.m;
  const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
  Hom R;
  float* r = R.m;
  r[0] = (m[4] * m[8] - m[5] * m[7]) / det, r[1] = (m[2] * m[7] - m[1] * m[8]) / det, r[2] = (m[1] * m[5] - m[2] * m[4]) / det;
  r[3] = (m[5] * m[6] - m[3] * m[8]) / det, r[4] = (m[0] * m[8] - m[2] * m[6]) / det, r[5] = (m[2] * m[3] - m[0] * m[5]) / det;
  r[6] = (m[3] * m[7] - m[4] * m[6]) / det, r[7] = (m[1] * m[6] - m[0] * m[7]) / det, r[8] = (m[0] * m[4] - m[1] * m[3]) / det;
  return R;
}
// a face picture laid on a quad of the canvas (shade: its light)
inline void warp(Cv& cv, const Img& face, const float q[8], float shade) {
  const Hom inv = hom_inv(hom_sq(q));
  ImgP src = std::make_shared<Img>(face);
  float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
  for (int i = 0; i < 4; ++i) lo[0] = std::min(lo[0], q[2 * i]), hi[0] = std::max(hi[0], q[2 * i]), lo[1] = std::min(lo[1], q[2 * i + 1]), hi[1] = std::max(hi[1], q[2 * i + 1]);
  while (src->w > 2 * (hi[0] - lo[0]) && src->w > 8) src = half(*src);
  const M qm = cv.poly({q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]});
  for (int y = 0; y < qm.h; ++y)
    for (int x = 0; x < qm.w; ++x) {
      const float a = qm.a[static_cast<size_t>(y) * qm.w + x];
      if (a <= 0.0f) continue;
      const float X = qm.x0 + x + 0.5f, Y = qm.y0 + y + 0.5f;
      const float* m = inv.m;
      const float w = m[6] * X + m[7] * Y + m[8];
      const float u = (m[0] * X + m[1] * Y + m[2]) / w, v = (m[3] * X + m[4] * Y + m[5]) / w;
      float s[4];
      sample(*src, u * src->w, v * src->h, s);
      if (s[3] <= 1e-4f) continue;
      cv.over(static_cast<size_t>(qm.y0 + y) * cv.W + qm.x0 + x, {std::min(255.0f, s[0] / s[3] * shade), std::min(255.0f, s[1] / s[3] * shade), std::min(255.0f, s[2] / s[3] * shade)},
              s[3] * a, 0.0f);
    }
}
inline Img img_of(const Cv& cv) {
  Img g;
  g.w = cv.W, g.h = cv.H;
  g.rgba = bytes(cv, false);
  return g;
}
// the box of 5 in a 3/4 view (common.py box3d): its front, top and right side; `packPic` its pack's front (the five on its front)
inline std::vector<uint8_t> box_picture(int set, const Img& packPic, const Star st[3], int* outW, int* outH) {
  // the faces, at 0.5 the mockup's size (they're shown ~0.4 of it)
  const float k = 0.5f;
  Cv f(static_cast<int>(1200 * k), static_cast<int>(780 * k)), t(static_cast<int>(1200 * k), static_cast<int>(820 * k)), s(static_cast<int>(820 * k), static_cast<int>(780 * k));
  auto K = [k](float v) { return v * k; };
  if (set == 0) {
    f.fillp(f.rrect(0, 0, f.W, f.H, 3), refr(kBlackF, K(1200), 0.0f, K(150), K(230), 2, 1, -62, K(780)));
    f.fillp(f.poly({0, K(520), K(1200), K(380), K(1200), K(780), 0, K(780)}), refr(kSilverF, K(1200), 1.0f, K(150), K(230), 3, 1, -62, K(780)));
    logo(f, K(50), K(50), 1.6f * k);
    TS r;
    r.align = 'r';
    r.outline = K(5);
    text(f, "HOBBY BOX", K(1150), K(120), K(84), 0xFFFFFF, r);
    TS r2;
    r2.align = 'r';
    text(f, "5 PACKS  -  5 CARDS A PACK", K(1150), K(205), K(40), 0xB8C2D6, r2);
  } else {
    const Marble m(31, K(320), f.W, f.H);
    f.fillp(f.rrect(0, 0, f.W, f.H, 3), [&](float x, float y) { return ramp(poster(m(x, y)), {{0, kPurp}, {0.35f, kMag}, {0.6f, kTeal}, {0.85f, kYel}, {1, 0xFFFFFF}}); });
    f.fill(f.poly({0, K(300), K(1200), K(170), K(1200), K(300), 0, K(430)}), kInk);
    f.fill(f.rrect(K(40), K(30), K(600), K(230), K(40)), kInk);
    logo(f, K(70), K(52), 1.35f * k);
    block_text(f, "BOX OF 5 PACKS", K(860), K(240), K(74), K(620), Lin({{0, 0xFFF36A}, {1, kOrng}}, 90, K(200), K(280)), kPurp, 0x05030A, K(6), K(8), 0.2f, 6.0f, K(8));
  }
  for (int i = 0; i < 5; ++i) {
    const float x = K(set == 0 ? 640.0f + i * 105.0f : 650.0f + i * 100.0f), y0 = K((set == 0 ? 300.0f : 380.0f) - (i % 2) * 18.0f);
    const float x1 = x + K(150), y1 = y0 + K(230);
    f.fill(f.blur(f.shift(f.rrect(x, y0 + K(30), x1, y1 + K(30), 4), 3, 5), 4), 0x000000, 0.5f);
    paste(f, packPic, x, y0, x1, y1);
  }
  const TrioOut o = trio_layout(st, K(30), K(set == 0 ? 260.0f : 330.0f), K(set == 0 ? 640.0f : 620.0f), K(set == 0 ? 760.0f : 770.0f), f.W, f.H);
  Fig fg;
  fg.shadow = 0.45f;
  if (set == 0) fg.rim = 0xFFFFFF, fg.rimA = 0.6f;
  else fg.keyline = K(8), fg.keyCol = 0xFFFFFF, fg.keyInk = kInk;
  draw_trio(f, st, o, fg);
  f.fill(f.rrect(K(640), K(650), K(1170), K(740), K(set == 0 ? 20.0f : 30.0f)), set == 0 ? 0x0B0F1Au : kInk);
  TS y;
  y.outline = set == 0 ? K(2) : 0.0f;
  text(f, "2026-27 STRIKERS", K(905), K(696), K(52), set == 0 ? kGoldA : kYel, y);
  // its top
  if (set == 0) {
    t.fillp(t.rrect(0, 0, t.W, t.H, 2), refr(kSilverF, K(1200), 2.2f, K(180), K(280), 3, 1, -62, K(820)));
    gem(t, K(600), K(330), K(210), nullptr, 0x0B0F1A, K(16));
    t.fill(t.rrect(K(150), K(600), K(1050), K(740), K(30)), 0x0B0F1A);
    text(t, "LOOK FOR BLACK TIZM 1/1", K(600), K(670), K(60), kGoldA);
  } else {
    const Marble m(5, K(340), t.W, t.H);
    t.fillp(t.rrect(0, 0, t.W, t.H, 2), [&](float x, float yy) { return ramp(poster(m(x, yy)), {{0, kPurp}, {0.4f, 0x4A1A8A}, {0.7f, kTeal}, {1, kYel}}); });
    t.fill(t.hexagon(K(600), K(330), K(240)), kInk);
    gem(t, K(600), K(330), K(210), nullptr, kInk, 0, 0, false);
    block_text(t, "FIND THE 1/1", K(600), K(690), K(110), K(1000), Lin({{0, 0xFFF36A}, {1, kOrng}}, 90, K(640), K(740)), kPurp, 0x05030A, K(6), K(8), 0.2f, 0, K(8));
  }
  // its side
  if (set == 0) s.fillp(s.rrect(0, 0, s.W, s.H, 2), refr(kBlackF, K(820), 0.0f, K(150), K(230), 9, 1, -62, K(780)));
  else s.fill(s.rrect(0, 0, s.W, s.H, 2), kInk);
  TS so;
  so.outline = set == 0 ? K(4) : 0.0f;
  text(s, "LANCINI TIZM", K(410), K(110), K(set == 0 ? 70.0f : 66.0f), set == 0 ? 0xFFFFFFu : kYel, so);
  const char* rows[6] = {"SILVER TIZM", "GOLD TIZM /10", "BLACK TIZM 1/1", "TIZM BOOM", "HOMETOWN", "SIGNED"};
  const uint32_t ca[6] = {0xE4E9F2, 0xFFD23F, 0xB98CFF, 0xFF6A5A, 0x7FE0A0, 0x5AD8FF}, cb[6] = {0xE4E9F2, 0xFFD23F, kMag, kOrng, 0x7CF05A, kTeal};
  for (int i = 0; i < 6; ++i) text(s, rows[i], K(410), K(230 + i * 84.0f), K(46), set == 0 ? ca[i] : cb[i], so);
  // the 3/4 view (1300 x 1100 at 0.5)
  Cv o3(static_cast<int>(1300 * k), static_cast<int>(1100 * k));
  const float fx0 = K(60), fy0 = K(420), fw = K(860), fh = K(560), dx = K(300), dy = K(-250);
  const float F[8] = {fx0, fy0, fx0 + fw, fy0, fx0 + fw, fy0 + fh, fx0, fy0 + fh};
  const float T[8] = {fx0 + dx, fy0 + dy, fx0 + fw + dx, fy0 + dy, fx0 + fw, fy0, fx0, fy0};
  const float Sd[8] = {fx0 + fw, fy0, fx0 + fw + dx, fy0 + dy, fx0 + fw + dx, fy0 + fh + dy, fx0 + fw, fy0 + fh};
  o3.fill(o3.blur(o3.poly({fx0 + K(30), fy0 + fh, fx0 + fw + K(40), fy0 + fh, fx0 + fw + dx + K(120), fy0 + fh + dy + K(40), fx0 + dx + K(60), fy0 + fh + dy + K(60)}), K(28)),
          0x000000, 0.66f);
  warp(o3, img_of(s), Sd, 0.72f);
  warp(o3, img_of(t), T, 1.08f);
  warp(o3, img_of(f), F, 1.0f);
  for (const float* e : {F, Sd}) {  // (its edges catching the light)
    (void)e;
  }
  o3.fill(o3.poly({F[0], F[1] - 1.5f, F[2], F[3] - 1.5f, F[2], F[3] + 1.5f, F[0], F[1] + 1.5f}), 0xFFFFFF, 0.47f);
  o3.fill(o3.poly({F[2] - 1.5f, F[3], F[2] + 1.5f, F[3], F[4] + 1.5f, F[5], F[4] - 1.5f, F[5]}), 0xFFFFFF, 0.47f);
  o3.fill(o3.poly({F[2], F[3] - 1.5f, Sd[2], Sd[3] - 1.5f, Sd[2], Sd[3] + 1.5f, F[2], F[3] + 1.5f}), 0xFFFFFF, 0.47f);
  *outW = o3.W, *outH = o3.H;
  return bytes(o3, false);
}

// ------------------------------------------------------------------------------------------------ the autograph's place
// r217: where a SIGNED / BLACK 1/1's autograph sits on each set's front (2 : 1, xrsign::kBoxAspect): A across the picture's lower
// half above its CERTIFIED AUTO pill; B on its white AUTHENTIC TIZM AUTOGRAPH sticker (in a deep blue ink, as 90s stickers were
// signed). The 3D signing moment lands it there too (xr_cards kSign*)
struct SignBox {
  float x0, y0, w, h;
};
inline SignBox sign_box(int set, float foot = 0.0f) { return set == 1 ? SignBox{430, 868 - foot, 440, 220} : SignBox{90, 690, 700, 350}; }  // (r220: a Jam die's foot)
#if __has_include("xr_sign.h")
inline void bake_sign(std::vector<uint8_t>& px, int w, int h, const Card& c, int set, float foot = 0.0f) {
  const xrsign::Signature& s = xrsign::get(c.key, c.signAs);
  const SignBox b = sign_box(set, foot);
  const int sw = static_cast<int>(b.w), sh = static_cast<int>(b.h);
  std::vector<uint8_t> ink(static_cast<size_t>(sw) * sh * 4, 0);
  xrsign::draw(s, s.duration + 1.0f, ink.data(), sw, sh, sw * 4);
  std::vector<uint8_t> edge(static_cast<size_t>(sw) * sh, 0);
  if (set == 0) {  // (a soft dark edge round its ink, 3 px - it reads on a busy refractor too)
    std::vector<uint8_t> tmp(edge.size(), 0);
    for (int y = 0; y < sh; ++y)
      for (int x = 0; x < sw; ++x) {
        uint8_t m = 0;
        for (int d = -3; d <= 3; ++d) m = std::max(m, ink[(static_cast<size_t>(y) * sw + std::max(0, std::min(sw - 1, x + d))) * 4 + 3]);
        tmp[static_cast<size_t>(y) * sw + x] = m;
      }
    for (int y = 0; y < sh; ++y)
      for (int x = 0; x < sw; ++x) {
        uint8_t m = 0;
        for (int d = -3; d <= 3; ++d) m = std::max(m, tmp[static_cast<size_t>(std::max(0, std::min(sh - 1, y + d))) * sw + x]);
        edge[static_cast<size_t>(y) * sw + x] = m;
      }
  }
  for (int y = 0; y < sh; ++y)
    for (int x = 0; x < sw; ++x) {
      const int X = static_cast<int>(b.x0) + x, Y = static_cast<int>(b.y0) + y;
      if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
      const size_t i = static_cast<size_t>(y) * sw + x;
      uint8_t q[4] = {ink[i * 4], ink[i * 4 + 1], ink[i * 4 + 2], ink[i * 4 + 3]};
      if (set == 1) {  // (B: a deep blue ballpoint, its own ink's hue kept a little)
        const uint32_t own = (static_cast<uint32_t>(q[0]) << 16) | (static_cast<uint32_t>(q[1]) << 8) | q[2];
        const uint32_t bl = mix_rgb(0x1E36B8, own, 0.18f);
        q[0] = (bl >> 16) & 255, q[1] = (bl >> 8) & 255, q[2] = bl & 255;
      }
      if (q[3] == 0 && edge[i] == 0) continue;
      uint8_t* p = &px[(static_cast<size_t>(Y) * w + X) * 4];
      if (p[3] == 0) continue;
      const float e = 0.45f * edge[i] / 255.0f, a = q[3] / 255.0f;
      for (int k = 0; k < 3; ++k) p[k] = static_cast<uint8_t>(std::lround((p[k] * (1.0f - e) + 10.0f * e) * (1.0f - a) + q[k] * a));
      p[3] = 255;  // (ink: no foil under it)
    }
}
#endif

}  // namespace lk
