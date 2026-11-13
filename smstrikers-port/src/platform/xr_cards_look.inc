// strikers-quest r217 (Lance 11-11: "go with A but also go with B!!! we will add B as a new pack!!!"): the cards' two looks,
// drawn pixel by pixel like the mockups he picked (logs\r216-card-mockups, tools\outside\cardlook216 - this is their kit and
// their dir_a / dir_b ported line for line to C++):
//   A "PRIZM CHROME" - LANCINI TIZM (the main set): a full-bleed team-colour field cut by Prizm facets and one bold light beam, a
//     colour slash behind the character, a chrome hexagon rating, a big leaning name bar, the stats on an ink panel
//   B "TIZM JAM '96" - LANCINI TIZM JAM '96 (the new premium pack): a die-cut crest top, liquid marble + halftone, the character a
//     die-cut sticker, a checker medallion rating, a huge extruded block name, neon equalizer stats on a ticket panel
// Included inside xr_cards.cpp's anonymous namespace (its Card / Ref / Img / xrtext). Any thread; every picture's canvas is
// its own (a front: ~33 MB of floats while it's drawn, as the board's ~26 MB). The foil goes in the alpha as render() does it.
namespace lk {

#ifdef HARNESS_REAL_CARDS  // (the PC proof's section timings)
inline double prof_now() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline std::string& prof_text() {
  static std::string s;
  return s;
}
inline double& prof_t() {
  static double t = 0;
  return t;
}
#define LKP(n)                                                                                \
  do {                                                                                        \
    const double n_ = prof_now();                                                             \
    prof_text() += std::string(" ") + (n) + " " + std::to_string(static_cast<int>(n_ - prof_t())); \
    prof_t() = n_;                                                                            \
  } while (0)
#else
#define LKP(n) \
  do {         \
  } while (0)
#endif

constexpr float kPi = 3.14159265f;
struct F3 {
  float r = 0, g = 0, b = 0;
};
inline F3 f3(uint32_t c) { return {static_cast<float>((c >> 16) & 255), static_cast<float>((c >> 8) & 255), static_cast<float>(c & 255)}; }
inline F3 operator+(F3 a, F3 b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
inline F3 operator*(F3 a, float k) { return {a.r * k, a.g * k, a.b * k}; }
inline F3 mixf(F3 a, F3 b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
inline uint32_t mixc(uint32_t a, uint32_t b, float t) { return mix_rgb(a, b, t); }
inline float sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float fract(double v) { return static_cast<float>(v - std::floor(v)); }

// a colour ramp: t 0..1 through stops (pos, rgb)
struct Stop {
  float t;
  uint32_t c;
};
inline F3 ramp(float t, std::initializer_list<Stop> s) {
  t = sat(t);
  const Stop* p = s.begin();
  if (t <= p->t) return f3(p->c);
  for (const Stop* q = p + 1; q != s.end(); ++q, ++p)
    if (t <= q->t) return mixf(f3(p->c), f3(q->c), (t - p->t) / std::max(1e-6f, q->t - p->t));
  return f3((s.end() - 1)->c);
}
inline F3 hsv(float h, float s, float v) {
  h = (h - std::floor(h)) * 6.0f;
  const int i = static_cast<int>(h) % 6;
  const float f = h - std::floor(h), p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
  F3 o;
  switch (i) {
    case 0: o = {v, t, p}; break;
    case 1: o = {q, v, p}; break;
    case 2: o = {p, v, t}; break;
    case 3: o = {p, q, v}; break;
    case 4: o = {t, p, v}; break;
    default: o = {v, p, q}; break;
  }
  return o * 255.0f;
}

// ------------------------------------------------------------------------------------------------ coverage masks
// a mask: coverage 0..1 over its box [x0, x0 + w) x [y0, y0 + h) of the canvas (nothing outside it)
struct M {
  int x0 = 0, y0 = 0, w = 0, h = 0;
  std::vector<float> a;
  bool empty() const { return w <= 0 || h <= 0; }
  float at(int x, int y) const {
    x -= x0;
    y -= y0;
    return x < 0 || y < 0 || x >= w || y >= h ? 0.0f : a[static_cast<size_t>(y) * w + x];
  }
  float& ref(int x, int y) { return a[static_cast<size_t>(y - y0) * w + (x - x0)]; }
};

class Cv {
 public:
  int W, H;
  std::vector<float> c;     // straight RGB 0..255 + alpha 0..1
  std::vector<float> foil;  // how much foil (0 printed .. 1 all foil: the alpha's code, render())
  std::vector<uint8_t> cut; // a special's window (see-through)
  Cv(int w, int h) : W(w), H(h), c(static_cast<size_t>(w) * h * 4, 0.0f), foil(static_cast<size_t>(w) * h, 0.0f) {}

  // ---- masks
  M box(int x0, int y0, int x1, int y1) const {
    M m;
    m.x0 = std::max(0, x0);
    m.y0 = std::max(0, y0);
    m.w = std::max(0, std::min(W, x1) - m.x0);
    m.h = std::max(0, std::min(H, y1) - m.y0);
    m.a.assign(static_cast<size_t>(m.w) * m.h, 0.0f);
    return m;
  }
  static float rbox_cov(float x, float y, float x0, float y0, float x1, float y1, float r) {
    r = std::max(0.0f, std::min(r, 0.5f * std::min(x1 - x0, y1 - y0)));
    const float qx = std::fabs(x - 0.5f * (x0 + x1)) - (0.5f * (x1 - x0) - r), qy = std::fabs(y - 0.5f * (y0 + y1)) - (0.5f * (y1 - y0) - r);
    const float d = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
    return sat(0.5f - d);
  }
  M rrect(float x0, float y0, float x1, float y1, float r) const {
    M m = box(static_cast<int>(std::floor(x0)) - 1, static_cast<int>(std::floor(y0)) - 1, static_cast<int>(std::ceil(x1)) + 1,
              static_cast<int>(std::ceil(y1)) + 1);
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) m.a[static_cast<size_t>(y) * m.w + x] = rbox_cov(m.x0 + x + 0.5f, m.y0 + y + 0.5f, x0, y0, x1, y1, r);
    return m;
  }
  M ring(float x0, float y0, float x1, float y1, float r, float t) const { return minus(rrect(x0, y0, x1, y1, r), rrect(x0 + t, y0 + t, x1 - t, y1 - t, std::max(0.0f, r - t))); }
  M ellipse(float x0, float y0, float x1, float y1) const {
    M m = box(static_cast<int>(std::floor(x0)) - 1, static_cast<int>(std::floor(y0)) - 1, static_cast<int>(std::ceil(x1)) + 1,
              static_cast<int>(std::ceil(y1)) + 1);
    const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1), rx = std::max(0.5f, 0.5f * (x1 - x0)), ry = std::max(0.5f, 0.5f * (y1 - y0));
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) {
        const float dx = (m.x0 + x + 0.5f - cx) / rx, dy = (m.y0 + y + 0.5f - cy) / ry;
        m.a[static_cast<size_t>(y) * m.w + x] = sat(0.5f - (std::sqrt(dx * dx + dy * dy) - 1.0f) * std::min(rx, ry));
      }
    return m;
  }
  // any simple polygon (x, y pairs), anti-aliased: 4 sub-rows a pixel, each span's ends their exact cover
  M poly(const std::vector<float>& xy) const {
    const size_t n = xy.size() / 2;
    if (n < 3) return box(0, 0, 0, 0);
    float a = 1e9f, b = 1e9f, c2 = -1e9f, d = -1e9f;
    for (size_t i = 0; i < n; ++i) a = std::min(a, xy[2 * i]), c2 = std::max(c2, xy[2 * i]), b = std::min(b, xy[2 * i + 1]), d = std::max(d, xy[2 * i + 1]);
    M m = box(static_cast<int>(std::floor(a)) - 1, static_cast<int>(std::floor(b)) - 1, static_cast<int>(std::ceil(c2)) + 1, static_cast<int>(std::ceil(d)) + 1);
    if (m.empty()) return m;
    std::vector<float> xs;
    for (int y = 0; y < m.h; ++y)
      for (int s = 0; s < 4; ++s) {
        const float sy = m.y0 + y + (s + 0.5f) / 4.0f;
        xs.clear();
        for (size_t i = 0; i < n; ++i) {
          const float ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
          if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) xs.push_back(ax + (sy - ay) / (by - ay) * (bx - ax));
        }
        for (size_t i = 1; i < xs.size(); ++i)  // (an insertion sort: a handful of crossings; libc++'s sort trips the wasm proof)
          for (size_t j = i; j > 0 && xs[j - 1] > xs[j]; --j) std::swap(xs[j - 1], xs[j]);
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
          const float l = xs[k] - m.x0, r = xs[k + 1] - m.x0;
          int il = std::max(0, static_cast<int>(std::floor(l))), ir = std::min(m.w - 1, static_cast<int>(std::floor(r)));
          for (int x = il; x <= ir; ++x) {
            const float cov = std::max(0.0f, std::min(r, x + 1.0f) - std::max(l, static_cast<float>(x)));
            m.a[static_cast<size_t>(y) * m.w + x] += 0.25f * cov;
          }
        }
      }
    for (float& v : m.a) v = sat(v);
    return m;
  }
  M hexagon(float cx, float cy, float r, float rot = -kPi / 2) const {
    std::vector<float> p;
    for (int i = 0; i < 6; ++i) p.push_back(cx + r * std::cos(rot + i * kPi / 3)), p.push_back(cy + r * std::sin(rot + i * kPi / 3));
    return poly(p);
  }
  // a mask's union / intersection / difference (the result over both boxes / the first's)
  static M inter(const M& a, const M& b) {
    M m = a;
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) m.a[static_cast<size_t>(y) * m.w + x] *= b.at(m.x0 + x, m.y0 + y);
    return m;
  }
  static M minus(const M& a, const M& b) {
    M m = a;
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) m.a[static_cast<size_t>(y) * m.w + x] *= 1.0f - b.at(m.x0 + x, m.y0 + y);
    return m;
  }
  M uni(const M& a, const M& b) const {
    if (a.empty()) return b;
    if (b.empty()) return a;
    M m = box(std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x0 + a.w, b.x0 + b.w), std::max(a.y0 + a.h, b.y0 + b.h));
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) m.a[static_cast<size_t>(y) * m.w + x] = std::max(a.at(m.x0 + x, m.y0 + y), b.at(m.x0 + x, m.y0 + y));
    return m;
  }
  static M scaled(M m, float k) {
    for (float& v : m.a) v *= k;
    return m;
  }
  M shift(const M& a, float dx, float dy) const {
    const int ix = static_cast<int>(std::lround(dx)), iy = static_cast<int>(std::lround(dy));
    M m = box(a.x0 + ix, a.y0 + iy, a.x0 + ix + a.w, a.y0 + iy + a.h);
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) m.a[static_cast<size_t>(y) * m.w + x] = a.at(m.x0 + x - ix, m.y0 + y - iy);
    return m;
  }
  // a gaussian blur of `s` px (three box passes each way), its box grown to hold it
  M blur(const M& a, float s) const {
    if (a.empty() || s < 0.3f) return a;
    const int g = static_cast<int>(std::ceil(3.0f * s)) + 1;
    M m = box(a.x0 - g, a.y0 - g, a.x0 + a.w + g, a.y0 + a.h + g);
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) m.a[static_cast<size_t>(y) * m.w + x] = a.at(m.x0 + x, m.y0 + y);
    const float wIdeal = std::sqrt(12.0f * s * s / 3.0f + 1.0f);
    int wl = static_cast<int>(std::floor(wIdeal));
    if (wl % 2 == 0) --wl;
    const int wu = wl + 2;
    const float mIdeal = (12.0f * s * s - 3.0f * wl * wl - 12.0f * wl - 9.0f) / (-4.0f * wl - 4.0f);
    const int mm = static_cast<int>(std::lround(mIdeal));
    std::vector<float> line, out;
    for (int pass = 0; pass < 3; ++pass) {
      const int r = ((pass < mm ? wl : wu) - 1) / 2;
      if (r <= 0) continue;
      const float inv = 1.0f / (2 * r + 1);
      for (int dir = 0; dir < 2; ++dir) {
        const int len = dir == 0 ? m.w : m.h, cnt = dir == 0 ? m.h : m.w;
        line.resize(static_cast<size_t>(len));
        out.resize(static_cast<size_t>(len));
        for (int k = 0; k < cnt; ++k) {
          for (int i = 0; i < len; ++i) line[static_cast<size_t>(i)] = dir == 0 ? m.a[static_cast<size_t>(k) * m.w + i] : m.a[static_cast<size_t>(i) * m.w + k];
          float acc = 0.0f;
          for (int i = -r; i <= r; ++i) acc += i >= 0 && i < len ? line[static_cast<size_t>(i)] : 0.0f;
          for (int i = 0; i < len; ++i) {
            out[static_cast<size_t>(i)] = acc * inv;
            const int add = i + r + 1, sub = i - r;
            acc += (add < len ? line[static_cast<size_t>(add)] : 0.0f) - (sub >= 0 ? line[static_cast<size_t>(sub)] : 0.0f);
          }
          for (int i = 0; i < len; ++i) (dir == 0 ? m.a[static_cast<size_t>(k) * m.w + i] : m.a[static_cast<size_t>(i) * m.w + k]) = out[static_cast<size_t>(i)];
        }
      }
    }
    return m;
  }
  // grown by `px` (round: a chamfer distance to what's covered)
  M grow(const M& a, float px) const {
    const int g = static_cast<int>(std::ceil(px)) + 2;
    M m = box(a.x0 - g, a.y0 - g, a.x0 + a.w + g, a.y0 + a.h + g);
    const int kInf = 1 << 28;
    std::vector<int> d(m.a.size());
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) d[static_cast<size_t>(y) * m.w + x] = a.at(m.x0 + x, m.y0 + y) >= 0.5f ? 0 : kInf;
    auto at = [&](int x, int y) { return x < 0 || y < 0 || x >= m.w || y >= m.h ? kInf : d[static_cast<size_t>(y) * m.w + x]; };
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) {
        int& v = d[static_cast<size_t>(y) * m.w + x];
        v = std::min({v, at(x - 1, y) + 3, at(x, y - 1) + 3, at(x - 1, y - 1) + 4, at(x + 1, y - 1) + 4});
      }
    for (int y = m.h - 1; y >= 0; --y)
      for (int x = m.w - 1; x >= 0; --x) {
        int& v = d[static_cast<size_t>(y) * m.w + x];
        v = std::min({v, at(x + 1, y) + 3, at(x, y + 1) + 3, at(x + 1, y + 1) + 4, at(x - 1, y + 1) + 4});
      }
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) {
        const size_t i = static_cast<size_t>(y) * m.w + x;
        m.a[i] = std::max(a.at(m.x0 + x, m.y0 + y), sat(px + 0.5f - d[i] / 3.0f));
      }
    return m;
  }

  // ---- painting: straight-alpha "over"; `fo` the foil it lays down (0 printed .. 1 foil); a solid cover (>0.5) closes a window
  void over(size_t i, F3 col, float a, float fo) {
    if (a <= 0.0f) return;
    a = std::min(a, 1.0f);
    float* p = &c[i * 4];
    const float oa = a + p[3] * (1.0f - a);
    const float k = p[3] * (1.0f - a);
    p[0] = (col.r * a + p[0] * k) / oa;
    p[1] = (col.g * a + p[1] * k) / oa;
    p[2] = (col.b * a + p[2] * k) / oa;
    foil[i] = (fo * a + foil[i] * k) / oa;
    p[3] = oa;
    if (!cut.empty() && a > 0.5f) cut[i] = 0;
  }
  template <class P>
  void fillp(const M& m, P paint, float alpha = 1.0f, float fo = 0.0f) {
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) {
        const float a = m.a[static_cast<size_t>(y) * m.w + x] * alpha;
        if (a <= 0.0f) continue;
        const int X = m.x0 + x, Y = m.y0 + y;
        over(static_cast<size_t>(Y) * W + X, paint(X + 0.5f, Y + 0.5f), a, fo);
      }
  }
  void fill(const M& m, uint32_t rgb, float alpha = 1.0f, float fo = 0.0f) {
    const F3 col = f3(rgb);
    fillp(m, [&](float, float) { return col; }, alpha, fo);
  }
  // light added (screen: c + (255 - c) x col x a) - colour only, no foil change
  template <class P>
  void addp(const M& m, P paint, float alpha) {
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) {
        const float a = m.a[static_cast<size_t>(y) * m.w + x] * alpha;
        if (a <= 0.0f) continue;
        const int X = m.x0 + x, Y = m.y0 + y;
        float* p = &c[(static_cast<size_t>(Y) * W + X) * 4];
        const F3 col = paint(X + 0.5f, Y + 0.5f);
        p[0] += (255.0f - p[0]) * col.r / 255.0f * a;
        p[1] += (255.0f - p[1]) * col.g / 255.0f * a;
        p[2] += (255.0f - p[2]) * col.b / 255.0f * a;
      }
  }
  void add(const M& m, uint32_t rgb, float alpha) {
    const F3 col = f3(rgb);
    addp(m, [&](float, float) { return col; }, alpha);
  }
  // a soft elliptical light
  void glow(float x0, float y0, float x1, float y1, uint32_t rgb, float a) {
    M m = box(static_cast<int>(x0), static_cast<int>(y0), static_cast<int>(x1) + 1, static_cast<int>(y1) + 1);
    const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1), rx = std::max(1.0f, 0.5f * (x1 - x0)), ry = std::max(1.0f, 0.5f * (y1 - y0));
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) {
        const float dx = (m.x0 + x + 0.5f - cx) / rx, dy = (m.y0 + y + 0.5f - cy) / ry;
        m.a[static_cast<size_t>(y) * m.w + x] = std::pow(sat(1.0f - std::sqrt(dx * dx + dy * dy)), 1.6f);
      }
    add(m, rgb, a);
  }
  M whole() const { return rrect(0, 0, static_cast<float>(W), static_cast<float>(H), 0); }
};

// ------------------------------------------------------------------------------------------------ paints
// fast sine (|error| < 0.001: plenty for paint) - the marble's six a pixel
inline float fsin(float x) {
  x = x * 0.15915494f;
  x -= std::floor(x + 0.5f);  // (-0.5 .. 0.5 turns)
  x *= 6.2831853f;
  const float x2 = x * x;
  return x * (1.0f + x2 * (-0.16666667f + x2 * (0.0083333310f + x2 * (-0.00019840874f + x2 * (2.7525562e-6f - x2 * 2.3889859e-8f)))));
}
// Prizm facets: a lattice of skewed triangles along a diagonal, each a value 0..1 (its tilt to the light): a hash of its cell
inline float shards(float x, float y, float ca, float sa, float w, float h, float seed) {
  const float u = x * ca + y * sa, v = -x * sa + y * ca;
  const float cu = std::floor(u / w), cv = std::floor(v / h);
  const float fu = u / w - cu, fv = v / h - cv;
  const uint32_t key = static_cast<uint32_t>(static_cast<int>(cu) * 73856093) ^ static_cast<uint32_t>(static_cast<int>(cv) * 19349663) ^
                       (fu + fv > 1.0f ? 0x9E3779B9u : 0u) ^ static_cast<uint32_t>(seed * 1013.0f);
  return pp_h01(key);
}
inline float shards(float x, float y, float ang, float w, float h, float seed) {
  const float a = ang * kPi / 180.0f;
  return shards(x, y, std::cos(a), std::sin(a), w, h, seed);
}
enum { kSilverF = 0, kGoldF, kBlackF };
// the foil as it reads mid-tilt (kit.py refractor): silver a rainbow refractor over chrome; gold gold chrome; black black chrome
// with a faint rainbow; bold diagonal light bands on top. W the picture's width (its beam's place). Its light along the diagonal
// (bands + beams) from a table made once (2 per px of the diagonal)
struct Refr {
  int kind = kSilverF;
  float ang = -62, phase = 0, sw = 150, sh = 230, seed = 3, strength = 1, W = 1080;
  float ca = 0, sa = 0, u0 = 0;
  std::shared_ptr<std::vector<float>> lut;  // [2i] light, [2i+1] beam (black's tint)
  void make(float H) {
    const float a = ang * kPi / 180.0f;
    ca = std::cos(a), sa = std::sin(a);
    float lo = 1e9f, hi = -1e9f;
    for (float x : {0.0f, W})
      for (float y : {0.0f, H}) lo = std::min(lo, x * ca + y * sa), hi = std::max(hi, x * ca + y * sa);
    u0 = lo - 4.0f;
    const int n = static_cast<int>((hi - lo + 8.0f) * 2.0f) + 2;
    lut = std::make_shared<std::vector<float>>(static_cast<size_t>(n) * 2);
    for (int i = 0; i < n; ++i) {
      const float u = u0 + i * 0.5f;
      const float band = 0.5f + 0.5f * std::sin(u / 210.0f + phase);
      const float bu = (u - W * 0.55f - 200.0f * std::sin(phase)) / 110.0f, b2 = (u - W * 0.15f) / 50.0f;
      const float beam = std::exp(-bu * bu), beam2 = 0.7f * std::exp(-b2 * b2);
      (*lut)[2 * static_cast<size_t>(i)] = 0.25f * band + 0.55f * (beam + beam2);
      (*lut)[2 * static_cast<size_t>(i) + 1] = beam;
    }
  }
  F3 operator()(float x, float y) const {
    const float u = x * ca + y * sa, v = -x * sa + y * ca;
    const float s = shards(x, y, ca, sa, sw, sh, seed);
    const int n = static_cast<int>(lut->size() / 2);
    const int i = std::max(0, std::min(n - 1, static_cast<int>((u - u0) * 2.0f)));
    const float light = (*lut)[2 * static_cast<size_t>(i)], beam = (*lut)[2 * static_cast<size_t>(i) + 1];
    const float lum = sat(0.30f + 0.38f * s + light);
    if (kind == kSilverF) {
      const float hue = (u + v * 0.35f) / 900.0f + s * 0.18f + phase * 0.1f;
      const F3 rb = hsv(hue, sat(0.55f * strength), 1.0f);
      const F3 chrome = ramp(lum, {{0, 0x6E7788}, {0.45f, 0xC9D0DC}, {0.8f, 0xF4F7FC}, {1, 0xFFFFFF}});
      const float k = std::min(1.0f, (0.55f + 0.25f * s) * strength);
      const float kk = 0.55f + 0.6f * lum;
      return {chrome.r * (1 - k) + rb.r * k * kk, chrome.g * (1 - k) + rb.g * k * kk, chrome.b * (1 - k) + rb.b * k * kk};
    }
    if (kind == kGoldF) {
      const F3 g = ramp(lum, {{0, 0x5A3A06}, {0.35f, 0xA8760F}, {0.6f, 0xE6B640}, {0.82f, 0xFFE48A}, {1, 0xFFF8DC}});
      const float hue = (u + v * 0.35f) / 900.0f + s * 0.2f;
      const F3 rb = hsv(hue, 0.35f, 1.0f);
      return g * 0.86f + rb * (0.14f * lum);
    }
    const F3 g = ramp(lum * 0.8f, {{0, 0x050608}, {0.4f, 0x15181F}, {0.7f, 0x343A47}, {1, 0x7C8494}});
    const float hue = (u + v * 0.35f) / 700.0f + s * 0.3f;
    const F3 rb = hsv(hue, 0.8f, 1.0f);
    const float k = 0.10f + 0.22f * beam;
    return g * (1 - k) + rb * (k * 0.6f);
  }
};
inline Refr refr(int kind, float W, float phase = 0, float sw = 150, float sh = 230, float seed = 3, float strength = 1, float ang = -62,
                 float H = 0.0f) {
  Refr r;
  r.kind = kind;
  r.W = W;
  r.phase = phase;
  r.sw = sw;
  r.sh = sh;
  r.seed = seed;
  r.strength = strength;
  r.ang = ang;
  r.make(H > 0.0f ? H : std::max(W * 1.6f, kCH));
  return r;
}// a linear gradient along `deg` from t0 to t1
struct Lin {
  std::vector<Stop> s;
  float ca, sa, t0, t1;
  Lin(std::initializer_list<Stop> st, float deg, float a, float b) : s(st), ca(std::cos(deg * kPi / 180.0f)), sa(std::sin(deg * kPi / 180.0f)), t0(a), t1(b) {}
  F3 operator()(float x, float y) const {
    const float t = sat(((x * ca + y * sa) - t0) / (t1 - t0));
    if (t <= s.front().t) return f3(s.front().c);
    for (size_t i = 1; i < s.size(); ++i)
      if (t <= s[i].t) return mixf(f3(s[i - 1].c), f3(s[i].c), (t - s[i - 1].t) / std::max(1e-6f, s[i].t - s[i - 1].t));
    return f3(s.back().c);
  }
};
// a 90s liquid marble 0..1 (domain-warped sines), its phases from `seed`, normalised to this picture (its range measured)
struct Marble {
  float p[8], scale, lo = -2.4f, hi = 2.4f;
  Marble(uint32_t seed, float sc, int W, int H) : scale(sc) {
    for (int i = 0; i < 8; ++i) p[i] = 6.28f * pp_h01(seed * 977u + static_cast<uint32_t>(i) * 131u + 7u);
    float a = 1e9f, b = -1e9f;
    for (int y = 0; y < H; y += 6)
      for (int x = 0; x < W; x += 6) {
        const float v = raw(x + 0.5f, y + 0.5f);
        a = std::min(a, v), b = std::max(b, v);
      }
    lo = a, hi = std::max(a + 1e-3f, b);
  }
  float raw(float X, float Y) const {
    const float x = X / scale, y = Y / scale;
    const float wx = x + 0.9f * fsin(1.7f * y + p[0]) + 0.5f * fsin(3.1f * x + 2.3f * y + p[1]);
    const float wy = y + 0.9f * fsin(1.3f * x + p[2]) + 0.5f * fsin(2.7f * y - 1.9f * x + p[3]);
    return fsin(2.2f * wx + 1.4f * wy + p[4]) + 0.6f * fsin(4.1f * wy - 2.0f * wx + p[5]) + 0.3f * fsin(7.3f * wx + p[6]);
  }
  float operator()(float x, float y) const { return sat((raw(x, y) - lo) / (hi - lo)); }
};
// poster bands of a marble (dir_b ramp_poster)
inline float poster(float m, int bands = 7) {
  const float q = std::floor(m * bands) / bands;
  return q + sat((m * bands - std::floor(m * bands) - 0.85f) / 0.15f) / bands;
}
// halftone: a dot's coverage at (x, y), dots `cell` px apart on a grid turned `deg`, size f 0..1
inline float dot(float x, float y, float cell, float f, float ca, float sa) {
  const float u = (x * ca + y * sa) / cell, v = (-x * sa + y * ca) / cell;
  const float du = u - std::round(u), dv = v - std::round(v);
  const float r = 0.5f * std::sqrt(sat(f)) * 1.1f;
  return sat((r - std::sqrt(du * du + dv * dv)) * cell + 0.5f);
}
template <class F>
M dots_mask(const Cv& cv, const M& within, float cell, F size, float deg) {
  M m = within;
  const float ca = std::cos(deg * kPi / 180.0f), sa = std::sin(deg * kPi / 180.0f);
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) {
      float& v = m.a[static_cast<size_t>(y) * m.w + x];
      if (v <= 0.0f) continue;
      const float X = m.x0 + x + 0.5f, Y = m.y0 + y + 0.5f;
      v *= dot(X, Y, cell, size(X, Y), ca, sa);
    }
  (void)cv;
  return m;
}

// ------------------------------------------------------------------------------------------------ text (xr_text's board)
// a text's masks: its fill and its fill + stroke (`stroke` px round it). (x, y): its anchor at its letters' middle; align 'c' 'l'
// 'r'; `room` shrinks it to fit; slant / xscale / angle (degrees, + rising to the right) as the board's text
struct TM {
  M fill, stroke;
};
inline TM text_masks(const Cv& cv, const std::string& t, float x, float y, float px, char align, float slant, float xscale, float angle,
                     float room, float stroke) {
  TM out;
  if (t.empty()) return out;
  float wd = xrtext::board_text_width(t, px) * xscale;
  if (room > 0.0f && wd > room) px *= room / wd, wd = room;
  const float cx = align == 'c' ? x : (align == 'l' ? x + 0.5f * wd : x - 0.5f * wd);
  float hw = 0.5f * wd + std::fabs(slant) * px + stroke + 10.0f, hh = px + stroke + 10.0f;
  if (angle != 0.0f) hw = hh = std::sqrt(hw * hw + hh * hh);
  const int bx0 = static_cast<int>(std::floor(cx - hw)), by0 = static_cast<int>(std::floor(y - hh));
  const int bw = static_cast<int>(std::ceil(2 * hw)) + 2, bh = static_cast<int>(std::ceil(2 * hh)) + 2;
  std::vector<uint8_t> px8(static_cast<size_t>(bw) * bh * 4);
  for (int pass = 0; pass < (stroke > 0.0f ? 2 : 1); ++pass) {
    xrtext::BoardPiece p;
    p.kind = xrtext::BoardPiece::kText;
    p.x0 = cx - bx0;
    p.y0 = y - by0;
    p.x1 = 0.0f;
    p.y1 = px;
    p.rgb = 0xFFFFFF;
    p.outline = pass == 1 ? stroke : 0.0f;
    p.outlineRgb = 0xFFFFFF;
    p.text = t;
    p.slant = slant;
    p.xscale = xscale;
    p.angle = -angle;
    std::fill(px8.begin(), px8.end(), 0);
    xrtext::board(px8.data(), bw, bh, {p});
    M m = cv.box(bx0, by0, bx0 + bw, by0 + bh);
    for (int yy = 0; yy < m.h; ++yy)
      for (int xx = 0; xx < m.w; ++xx) {
        const int lx = m.x0 + xx - bx0, ly = m.y0 + yy - by0;
        m.a[static_cast<size_t>(yy) * m.w + xx] = px8[(static_cast<size_t>(ly) * bw + lx) * 4 + 3] / 255.0f;
      }
    (pass == 0 ? out.fill : out.stroke) = std::move(m);
  }
  if (stroke <= 0.0f) out.stroke = out.fill;
  return out;
}
inline float text_width(const std::string& t, float px, float xscale = 1.0f) { return xrtext::board_text_width(t, px) * xscale; }
struct TS {  // a text's style
  char align = 'c';
  float slant = 0, xscale = 1, angle = 0, room = 0, outline = 0, shadow = 0, shadowA = 0.5f, fo = 0;
  uint32_t ocol = 0x0B0F1A, scol = 0x000000;
};
template <class P>
TM textp(Cv& cv, const std::string& t, float x, float y, float px, P paint, const TS& s = TS()) {
  TM m = text_masks(cv, t, x, y, px, s.align, s.slant, s.xscale, s.angle, s.room, s.outline);
  if (s.shadow > 0.0f) cv.fill(cv.blur(cv.shift(m.stroke, s.shadow * 0.4f, s.shadow), s.shadow * 0.5f), s.scol, s.shadowA);
  if (s.outline > 0.0f) cv.fill(m.stroke, s.ocol, 1.0f, 0.0f);
  cv.fillp(m.fill, paint, 1.0f, s.fo);
  return m;
}
inline TM text(Cv& cv, const std::string& t, float x, float y, float px, uint32_t col, const TS& s = TS()) {
  const F3 c = f3(col);
  return textp(cv, t, x, y, px, [&](float, float) { return c; }, s);
}

// ------------------------------------------------------------------------------------------------ the character's picture
// its see-through margins trimmed (alpha > 12), a bust (cut off at its foot: its bottom row solid across a fifth of it), an opaque
// photo (no see-through background: an old portrait)
struct Art {
  ImgP img;
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  bool bust = false, photo = false, edge = false;  // edge: the render itself cut at its picture's side / top (a limb out of frame)
  bool ok() const { return img && !img->empty() && x1 > x0 && y1 > y0; }
  float w() const { return static_cast<float>(x1 - x0); }
  float h() const { return static_cast<float>(y1 - y0); }
};
inline Art art_of(const ImgP& g) {
  Art a;
  a.img = g;
  if (!g || g->empty()) return a;
  const int w = g->w, h = g->h;
  a.x0 = w, a.y0 = h;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if (g->rgba[(static_cast<size_t>(y) * w + x) * 4 + 3] > 12) a.x0 = std::min(a.x0, x), a.x1 = std::max(a.x1, x + 1), a.y0 = std::min(a.y0, y), a.y1 = std::max(a.y1, y + 1);
  if (a.x1 <= a.x0) {
    a.x0 = a.y0 = 0, a.x1 = w, a.y1 = h;
    return a;
  }
  int solid = 0;
  for (int x = a.x0; x < a.x1; ++x) solid += g->rgba[(static_cast<size_t>(a.y1 - 1) * w + x) * 4 + 3] > 200 ? 1 : 0;
  a.bust = solid > 0.18f * (a.x1 - a.x0) && a.y1 >= h - 2;
  a.photo = opaque_art(g);
  a.edge = !a.photo && (a.x0 <= 0 || a.x1 >= w || a.y0 <= 0);
  return a;
}
// a picture's colour + alpha at (u, v) (its pixels), bilinear, premultiplied
inline void sample(const Img& g, float u, float v, float* out) {
  u -= 0.5f, v -= 0.5f;
  const int x0 = static_cast<int>(std::floor(u)), y0 = static_cast<int>(std::floor(v));
  const float fx = u - x0, fy = v - y0;
  out[0] = out[1] = out[2] = out[3] = 0.0f;
  for (int k = 0; k < 4; ++k) {
    const int x = x0 + (k & 1), y = y0 + (k >> 1);
    if (x < 0 || y < 0 || x >= g.w || y >= g.h) continue;
    const float wgt = ((k & 1) ? fx : 1 - fx) * ((k >> 1) ? fy : 1 - fy);
    const uint8_t* p = &g.rgba[(static_cast<size_t>(y) * g.w + x) * 4];
    const float a = p[3] / 255.0f * wgt;
    out[0] += p[0] * a, out[1] += p[1] * a, out[2] += p[2] * a, out[3] += a;
  }
}
// a half-size copy (2 x 2 averaged, premultiplied) - for drawing a picture down more than 2x
inline ImgP half(const Img& g) {
  auto o = std::make_shared<Img>();
  o->w = std::max(1, g.w / 2), o->h = std::max(1, g.h / 2);
  o->rgba.assign(static_cast<size_t>(o->w) * o->h * 4, 0);
  for (int y = 0; y < o->h; ++y)
    for (int x = 0; x < o->w; ++x) {
      float s[4] = {0, 0, 0, 0};
      for (int k = 0; k < 4; ++k) {
        const uint8_t* p = &g.rgba[(static_cast<size_t>(std::min(g.h - 1, 2 * y + (k >> 1))) * g.w + std::min(g.w - 1, 2 * x + (k & 1))) * 4];
        const float a = p[3] / 255.0f;
        s[0] += p[0] * a, s[1] += p[1] * a, s[2] += p[2] * a, s[3] += a;
      }
      uint8_t* q = &o->rgba[(static_cast<size_t>(y) * o->w + x) * 4];
      for (int c = 0; c < 3; ++c) q[c] = static_cast<uint8_t>(std::lround(s[3] > 0 ? std::min(255.0f, s[c] / s[3]) : 0.0f));
      q[3] = static_cast<uint8_t>(std::lround(s[3] / 4.0f * 255.0f));
    }
  return o;
}
// the auto-fit (r217, Lance: "centered perfectly"): the trimmed figure fitted whole in the box (contain) at `fill`, centred left to
// right; anchor 'b' its feet on the box's foot (every card the same feet line), 'c' the box's middle, 't' its top
struct Placed {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0, s = 1;  // where its trimmed picture lands (card px), its scale
};
inline Placed fit(const Art& a, float x0, float y0, float x1, float y1, char anchor, float fill) {
  Placed p;
  if (!a.ok()) return p;
  const float bw = (x1 - x0) * fill, bh = (y1 - y0) * fill;
  p.s = std::min(bw / a.w(), bh / a.h());
  const float w = a.w() * p.s, h = a.h() * p.s, cx = 0.5f * (x0 + x1);
  p.x0 = cx - 0.5f * w;
  p.y0 = anchor == 'b' ? y1 - h : (anchor == 't' ? y0 : 0.5f * (y0 + y1) - 0.5f * h);
  p.x1 = p.x0 + w;
  p.y1 = p.y0 + h;
  return p;
}
// the figure drawn where `p` puts it (clip: only inside it): its key line (a die-cut sticker's white edge + an ink line), its
// shadow, its rim light, then it. Returns its alpha as drawn
struct Fig {
  float shadow = 0.45f, rimA = 0.9f;
  uint32_t rim = 0;  // 0 none
  float keyline = 0;  // px of white round it (0 none)
  uint32_t keyCol = 0xFFFFFF, keyInk = 0x0C0814;
  const M* clip = nullptr;
  bool sharpen = true;
};
inline M figure(Cv& cv, const Art& a, const Placed& p, const Fig& f) {
  M m = cv.box(static_cast<int>(std::floor(p.x0)), static_cast<int>(std::floor(p.y0)), static_cast<int>(std::ceil(p.x1)) + 1,
               static_cast<int>(std::ceil(p.y1)) + 1);
  if (!a.ok() || m.empty()) return m;
  ImgP src = a.img;
  float sx0 = static_cast<float>(a.x0), sy0 = static_cast<float>(a.y0), k = 1.0f / p.s;
  while (k > 2.0f && src->w > 8) {  // (drawn down more than 2x: from a half-size copy)
    src = half(*src);
    sx0 *= 0.5f, sy0 *= 0.5f, k *= 0.5f;
  }
  std::vector<F3> col(m.a.size());
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) {
      float s[4];
      sample(*src, sx0 + (m.x0 + x + 0.5f - p.x0) * k, sy0 + (m.y0 + y + 0.5f - p.y0) * k, s);
      const size_t i = static_cast<size_t>(y) * m.w + x;
      float av = s[3];
      if (f.clip != nullptr) av *= f.clip->at(m.x0 + x, m.y0 + y);
      m.a[i] = av;
      col[i] = s[3] > 1e-4f ? F3{s[0] / s[3], s[1] / s[3], s[2] / s[3]} : F3{};
    }
  if (f.sharpen && k < 0.9f) {  // (drawn up: a soft unsharp mask, as the mockups' 1.2 px / 60%)
    std::vector<F3> b = col;
    for (int y = 1; y + 1 < m.h; ++y)
      for (int x = 1; x + 1 < m.w; ++x) {
        const size_t i = static_cast<size_t>(y) * m.w + x;
        if (m.a[i] < 0.9f) continue;
        F3 s{};
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) s = s + col[static_cast<size_t>(y + dy) * m.w + x + dx];
        s = s * (1.0f / 9.0f);
        b[i] = {std::min(255.0f, std::max(0.0f, col[i].r + 0.6f * (col[i].r - s.r))), std::min(255.0f, std::max(0.0f, col[i].g + 0.6f * (col[i].g - s.g))),
                std::min(255.0f, std::max(0.0f, col[i].b + 0.6f * (col[i].b - s.b)))};
      }
    col.swap(b);
  }
  if (f.keyline > 0.0f) {
    const M k1 = cv.grow(m, f.keyline);
    cv.fill(cv.grow(k1, 5), f.keyInk);
    cv.fill(k1, f.keyCol);
  }
  if (f.shadow > 0.0f) cv.fill(cv.blur(cv.shift(m, 10, 18), 16), 0x000000, f.shadow);
  if (f.rim != 0) cv.fill(cv.blur(Cv::minus(cv.grow(m, 6), m), 3), f.rim, f.rimA);
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) {
      const size_t i = static_cast<size_t>(y) * m.w + x;
      cv.over(static_cast<size_t>(m.y0 + y) * cv.W + m.x0 + x, col[i], m.a[i], 0.0f);
    }
  return m;
}
// an already drawn picture (straight RGBA) laid into the canvas in a box (bilinear; down more than 2x from half copies)
inline void paste(Cv& cv, const Img& g, float x0, float y0, float x1, float y1, float alpha = 1.0f, float fo = 0.0f) {
  Art a;
  a.img = std::make_shared<Img>(g);
  a.x0 = a.y0 = 0;
  a.x1 = g.w;
  a.y1 = g.h;
  Placed p;
  p.x0 = x0, p.y0 = y0, p.x1 = x1, p.y1 = y1, p.s = (x1 - x0) / g.w;
  M m = cv.box(static_cast<int>(std::floor(x0)), static_cast<int>(std::floor(y0)), static_cast<int>(std::ceil(x1)) + 1, static_cast<int>(std::ceil(y1)) + 1);
  ImgP src = a.img;
  float k = 1.0f / p.s, ky = static_cast<float>(g.h) / (y1 - y0);
  float sc = 1.0f;
  while (k > 2.0f && src->w > 8) src = half(*src), k *= 0.5f, ky *= 0.5f, sc *= 0.5f;
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) {
      float s[4];
      sample(*src, (m.x0 + x + 0.5f - x0) * k, (m.y0 + y + 0.5f - y0) * ky, s);
      if (s[3] <= 1e-4f) continue;
      cv.over(static_cast<size_t>(m.y0 + y) * cv.W + m.x0 + x, {s[0] / s[3], s[1] / s[3], s[2] / s[3]}, s[3] * alpha, fo);
    }
  (void)sc;
}

// the canvas out as render() makes a picture: straight colour; where it's solid its foil in the alpha (255 - foil x 127); a
// window see-through
inline std::vector<uint8_t> bytes(const Cv& cv, bool foil) {
  std::vector<uint8_t> out(static_cast<size_t>(cv.W) * cv.H * 4);
  for (size_t i = 0; i < static_cast<size_t>(cv.W) * cv.H; ++i) {
    uint8_t* q = &out[i * 4];
    const float* p = &cv.c[i * 4];
    if (!cv.cut.empty() && cv.cut[i] != 0) {
      q[0] = q[1] = q[2] = q[3] = 0;
      continue;
    }
    for (int k = 0; k < 3; ++k) q[k] = static_cast<uint8_t>(std::lround(std::min(255.0f, std::max(0.0f, p[k]))));
    const int a = static_cast<int>(std::lround(sat(p[3]) * 255.0f));
    q[3] = static_cast<uint8_t>(a);
    if (a >= 255 && foil) {
      const int f = static_cast<int>(std::lround(sat(cv.foil[i]) * 255.0f));
      q[3] = static_cast<uint8_t>(255 - (f * 127 + 127) / 255);
    }
  }
  return out;
}

// ------------------------------------------------------------------------------------------------ shared marks
// LANCINI TIZM's six-facet gem
inline void gem(Cv& cv, float cx, float cy, float r, const uint32_t* pal = nullptr, uint32_t edge = 0x0B0F1A, float edgeW = -1.0f, float fo = 0.0f,
                bool edgeOn = true) {
  static const uint32_t kPal[6] = {0xFFFFFF, 0xD9E0EE, 0x8E9BB4, 0x5A6682, 0xA6B3CC, 0xEEF2FA};
  if (pal == nullptr) pal = kPal;
  if (edgeOn) cv.fill(cv.hexagon(cx, cy, r + (edgeW >= 0.0f ? edgeW : r * 0.11f)), edge);
  for (int i = 0; i < 6; ++i) {
    const float a0 = -kPi / 2 + i * kPi / 3, a1 = a0 + kPi / 3;
    cv.fill(cv.poly({cx, cy, cx + r * std::cos(a0), cy + r * std::sin(a0), cx + r * std::cos(a1), cy + r * std::sin(a1)}), pal[i], 1.0f, fo);
  }
}
// the full mark: the gem, LANCINI over a leaning chrome TIZM with its cyan / magenta split (xr_cards logo())
inline void logo(Cv& cv, float x, float y, float k, uint32_t words = 0xFFFFFF, uint32_t edge = 0x0B0F1A, float gemFoil = 0.0f) {
  gem(cv, x + 58 * k, y + 62 * k, 58 * k, nullptr, edge, -1.0f, gemFoil);
  const float tx = x + 132 * k;
  TS s;
  s.align = 'l';
  s.outline = 3 * k;
  s.ocol = edge;
  text(cv, "LANCINI", tx + 4 * k, y + 34 * k, 40 * k, words, s);
  TS t;
  t.align = 'l';
  t.slant = 0.24f;
  t.xscale = 0.92f;
  TM a = text_masks(cv, "TIZM", tx - 5 * k, y + 92 * k, 76 * k, 'l', t.slant, t.xscale, 0, 0, 0);
  cv.fill(a.fill, 0x22E0FF, 0.85f);
  a = text_masks(cv, "TIZM", tx + 5 * k, y + 100 * k, 76 * k, 'l', t.slant, t.xscale, 0, 0, 0);
  cv.fill(a.fill, 0xFF2AD4, 0.85f);
  a = text_masks(cv, "TIZM", tx, y + 96 * k, 76 * k, 'l', t.slant, t.xscale, 0, 0, 4 * k);
  cv.fill(a.stroke, edge);
  const Lin chrome({{0, 0xFFFFFF}, {0.5f, 0xE4E9F2}, {0.56f, 0x9AA6BC}, {1, 0xF4F6FA}}, 90, y + 60 * k, y + 130 * k);
  cv.fillp(a.fill, chrome);
}
// five pips (pill shapes) at (x, y) middle
inline void pips(Cv& cv, float x, float y, int lit, float size, float gap, uint32_t on, uint32_t off) {
  const float h = size * 0.8f;
  for (int i = 0; i < 5; ++i) {
    const float x0 = x + i * (size + gap);
    cv.fill(cv.rrect(x0, y - h / 2, x0 + size, y + h / 2, h / 2), i < lit ? on : off);
  }
}
inline int lit_of(float v) { return std::max(1, std::min(5, static_cast<int>(std::floor(v * 5.0f + 0.5f)))); }
// a five-pointed star (a dark rim first)
inline void star(Cv& cv, float cx, float cy, float r, uint32_t rgb, uint32_t rim = 0x0B0F1A) {
  for (int pass = 0; pass < 2; ++pass) {
    const float rr = pass == 0 ? r * 1.28f : r;
    std::vector<float> xy;
    for (int i = 0; i < 10; ++i) {
      const float a = -kPi / 2 + 0.62831853f * i, d = i % 2 == 0 ? rr : rr * 0.45f;
      xy.push_back(cx + d * std::cos(a)), xy.push_back(cy + d * std::sin(a));
    }
    cv.fill(cv.poly(xy), pass == 0 ? rim : rgb);
  }
}
// one bold diagonal light beam across the picture (blurred band, added)
// (the band's soft edges straight from a table: a band blurred 10 px across, as the mockups' blurred polygon, without the blur)
inline void beam(Cv& cv, float x, float w, float a, float ang = -62) {
  const float t = std::tan((90 + ang) * kPi / 180.0f);
  const float sig = 10.0f * std::sqrt(1.0f + t * t);  // (10 px across the band = this along a row)
  const int pad = static_cast<int>(4.0f * sig) + 2, n = static_cast<int>(w) + 2 * pad + 2;
  std::vector<float> prof(static_cast<size_t>(n));
  auto phi = [](float z) { return 0.5f * (1.0f + std::tanh(0.7978845f * (z + 0.044715f * z * z * z))); };
  for (int i = 0; i < n; ++i) {
    const float d = i - pad + 0.5f;  // (from the band's left edge)
    prof[static_cast<size_t>(i)] = phi(d / sig) - phi((d - w) / sig);
  }
  M m = cv.box(0, 0, cv.W, cv.H);
  for (int y = 0; y < m.h; ++y) {
    const float xl = x + t * (y + 0.5f);
    const int lo = std::max(0, static_cast<int>(std::floor(xl)) - pad), hi = std::min(m.w, static_cast<int>(std::ceil(xl + w)) + pad);
    for (int X = lo; X < hi; ++X) {
      const int i = static_cast<int>(std::floor(X + 0.5f - xl)) + pad;
      if (i >= 0 && i < n) m.a[static_cast<size_t>(y) * m.w + X] = prof[static_cast<size_t>(i)];
    }
  }
  cv.add(m, 0xFFFFFF, a);
}

}  // namespace lk
