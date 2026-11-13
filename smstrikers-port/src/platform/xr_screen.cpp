// strikers-quest r205: the TIZM machine's screen off the frame thread (xr_screen.h says what and why)
#include "xr_screen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace xrscreen {
namespace {

uint64_t fnv(uint64_t h, const void* p, size_t n) {
  const unsigned char* b = static_cast<const unsigned char*>(p);
  for (size_t i = 0; i < n; ++i) {
    h ^= b[i];
    h *= 1099511628211ull;
  }
  return h;
}
template <class T>
uint64_t mixv(uint64_t h, const T& v) {
  return fnv(h, &v, sizeof(v));
}
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
float rr_sd(float px, float py, float x0, float y0, float x1, float y1, float radius) {  // (xr_text.cpp's rounded_rect_sd)
  const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
  const float hx = 0.5f * (x1 - x0) - radius, hy = 0.5f * (y1 - y0) - radius;
  const float qx = std::fabs(px - cx) - hx, qy = std::fabs(py - cy) - hy;
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}
struct Px {
  float r = 0, g = 0, b = 0, a = 0;  // premultiplied
  void over(uint32_t rgb, float alpha) {
    const float a2 = clamp01(alpha);
    if (a2 <= 0.0f) return;
    r = ((rgb >> 16) & 255) / 255.0f * a2 + r * (1.0f - a2);
    g = ((rgb >> 8) & 255) / 255.0f * a2 + g * (1.0f - a2);
    b = (rgb & 255) / 255.0f * a2 + b * (1.0f - a2);
    a = a2 + a * (1.0f - a2);
  }
};
// the overlay's look (cursor_frame's, in base units x k): the frame 5 out from the item at rest, a 13-wide tube, its 4-wide
// white-hot core, a white bar down its left edge; a halo round it (outside only: over the screen it must not wash the item out)
// and a faint cyan light on the item (r204 filled it deep cyan under its text - an overlay can only tint it)
constexpr float kGrow = 5.0f, kTube = 13.0f, kCore = 4.0f, kHalo = 30.0f;
constexpr float kFlash[4] = {0.0f, 0.5f, 0.3f, 0.12f};

}  // namespace

void merge_bands(std::vector<Band>& b, int h, int gap, int maxBands) {
  std::vector<Band> v;
  for (Band x : b) {
    x.y0 = std::max(0, x.y0);
    x.y1 = std::min(h, x.y1);
    if (x.y1 > x.y0) v.push_back(x);
  }
  std::sort(v.begin(), v.end(), [](const Band& a, const Band& c) { return a.y0 < c.y0; });
  std::vector<Band> m;
  for (const Band& x : v) {
    if (!m.empty() && x.y0 <= m.back().y1 + gap) m.back().y1 = std::max(m.back().y1, x.y1);
    else m.push_back(x);
  }
  while (static_cast<int>(m.size()) > std::max(1, maxBands)) {
    size_t best = 0;
    int bestGap = 1 << 30;
    for (size_t i = 0; i + 1 < m.size(); ++i)
      if (m[i + 1].y0 - m[i].y1 < bestGap) bestGap = m[i + 1].y0 - m[i].y1, best = i;
    m[best].y1 = std::max(m[best].y1, m[best + 1].y1);
    m.erase(m.begin() + static_cast<long>(best) + 1);
  }
  b = m;
}
int band_rows(const std::vector<Band>& b) {
  int n = 0;
  for (const Band& x : b) n += x.y1 - x.y0;
  return n;
}

uint64_t piece_hash(const xrtext::BoardPiece& p) {
  uint64_t h = 1469598103934665603ull;
  h = mixv(h, static_cast<int>(p.kind));
  const float f[] = {p.x0,     p.y0,   p.x1,    p.y1,  p.radius, p.alpha,  p.outline, p.alpha2, p.focusX, p.focusY,
                     p.xscale, p.slant, p.angle, p.shadow, p.shadowA, p.inner, p.dot0,    p.dot1};
  h = fnv(h, f, sizeof(f));
  const uint32_t u[] = {p.rgb, p.rgb2, p.outlineRgb, p.shadowRgb, static_cast<uint32_t>(p.points), static_cast<uint32_t>(p.iw),
                        static_cast<uint32_t>(p.ih),
                        static_cast<uint32_t>((p.bottom ? 1 : 0) | (p.silhouette ? 2 : 0) | (p.trim ? 4 : 0) | (p.crop ? 8 : 0) |
                                              (p.flip ? 16 : 0) | (p.duo ? 32 : 0))};
  h = fnv(h, u, sizeof(u));
  h = mixv(h, reinterpret_cast<uintptr_t>(p.img));
  h = fnv(h, p.text.data(), p.text.size());
  h = mixv(h, p.text.size());
  if (!p.pts.empty()) h = fnv(h, p.pts.data(), p.pts.size() * sizeof(float));
  return h;
}
uint64_t pieces_hash(const std::vector<xrtext::BoardPiece>& v) {
  uint64_t h = 1469598103934665603ull;
  for (const xrtext::BoardPiece& p : v) h = mixv(h, piece_hash(p));
  return mixv(h, v.size());
}

void Change::add(const Change& o) {
  if (!o.any) return;
  any = true;
  full = full || o.full;
  bands.insert(bands.end(), o.bands.begin(), o.bands.end());
  rects.insert(rects.end(), o.rects.begin(), o.rects.end());
}
Rect piece_box(const xrtext::BoardPiece& p, int w) {
  Rect r;
  xrtext::piece_rows(p, &r.y0, &r.y1);
  switch (p.kind) {
    case xrtext::BoardPiece::kText:
    case xrtext::BoardPiece::kTextLeft:
    case xrtext::BoardPiece::kTextRight: {
      const float pad = std::ceil(std::max(0.0f, p.outline)) + 4.0f + std::fabs(p.shadow);
      const float xs = p.xscale > 0.05f ? p.xscale : 1.0f;
      float wd = xrtext::board_text_width(p.text, std::max(0.0f, p.y1)) * xs;
      if (p.x1 > 0.0f) wd = std::min(wd, p.x1);
      if (p.slant != 0.0f || p.angle != 0.0f) {  // (turned / leaning about its middle: its half-diagonal every way)
        const float hh = std::max(0.0f, p.y1) + 2.0f * pad;
        const float half = 0.5f * std::sqrt((wd + std::fabs(p.slant) * hh) * (wd + std::fabs(p.slant) * hh) + hh * hh) + pad;
        const float cx = p.kind == xrtext::BoardPiece::kText ? p.x0 : (p.kind == xrtext::BoardPiece::kTextLeft ? p.x0 + 0.5f * wd : p.x0 - 0.5f * wd);
        r.x0 = cx - half, r.x1 = cx + half;
        break;
      }
      if (p.kind == xrtext::BoardPiece::kText) r.x0 = p.x0 - 0.5f * wd - pad, r.x1 = p.x0 + 0.5f * wd + pad;
      else if (p.kind == xrtext::BoardPiece::kTextLeft) r.x0 = p.x0 - pad, r.x1 = p.x0 + wd + pad;
      else r.x0 = p.x0 - wd - pad, r.x1 = p.x0 + pad;
      break;
    }
    case xrtext::BoardPiece::kPoly: {
      float a = 1e9f, b = -1e9f;
      for (size_t i = 0; i < p.pts.size(); i += 2) a = std::min(a, p.pts[i]), b = std::max(b, p.pts[i]);
      r.x0 = a - 3.0f, r.x1 = b + 3.0f;
      break;
    }
    default:
      r.x0 = std::min(p.x0, p.x1) - 3.0f, r.x1 = std::max(p.x0, p.x1) + 3.0f;
      break;
  }
  r.x0 = std::max(r.x0, -1.0f);
  r.x1 = std::min(r.x1, static_cast<float>(w) + 1.0f);
  return r;
}
bool touches(const Change& c, float x0, float y0, float x1, float y1, float margin) {
  if (!c.any) return false;
  if (c.full) return true;
  for (const Rect& r : c.rects)
    if (r.x1 > x0 - margin && r.x0 < x1 + margin && r.y1 > y0 - margin && r.y0 < y1 + margin) return true;
  return false;
}

Change Board::diff(const std::vector<xrtext::BoardPiece>& pieces, int w, int h) {
  std::vector<Sig> now;
  now.reserve(pieces.size());
  for (const xrtext::BoardPiece& p : pieces) {
    Sig s;
    s.h = piece_hash(p);
    xrtext::piece_rows(p, &s.y0, &s.y1);
    s.box.y0 = s.y0, s.box.y1 = s.y1, s.box.x0 = -1e6f, s.box.x1 = 1e6f;  // (its x only when it changes: piece_box)
    now.push_back(s);
  }
  Change c;
  if (!have_ || w != w_ || h != h_) {
    for (size_t k = 0; k < now.size(); ++k) now[k].box = piece_box(pieces[k], w);  // (each one's box, for when it changes)
    prev_ = std::move(now);
    have_ = true;
    w_ = w, h_ = h;
    c.any = c.full = true;
    c.bands.push_back(Band{0, h});
    return c;
  }
  // what's gone + what's new (a multiset: a piece drawn twice counts twice) mark their rows
  std::vector<std::pair<uint64_t, uint32_t>> a(prev_.size()), b(now.size());
  for (size_t i = 0; i < prev_.size(); ++i) a[i] = {prev_[i].h, static_cast<uint32_t>(i)};
  for (size_t i = 0; i < now.size(); ++i) b[i] = {now[i].h, static_cast<uint32_t>(i)};
  std::sort(a.begin(), a.end());
  std::sort(b.begin(), b.end());
  auto mark = [&](const Sig& s, const xrtext::BoardPiece* p) {
    c.bands.push_back(Band{static_cast<int>(std::floor(s.y0)) - 1, static_cast<int>(std::ceil(s.y1)) + 2});
    c.rects.push_back(p != nullptr ? piece_box(*p, w) : s.box);
  };
  size_t i = 0, j = 0;
  while (i < a.size() || j < b.size()) {
    if (j >= b.size() || (i < a.size() && a[i].first < b[j].first)) {
      mark(prev_[a[i].second], nullptr);
      ++i;
    } else if (i >= a.size() || b[j].first < a[i].first) {
      Sig& s = now[b[j].second];
      s.box = piece_box(pieces[b[j].second], w);  // (kept: when it goes, its box is known)
      mark(s, &pieces[b[j].second]);
      ++j;
    } else {
      now[b[j].second].box = prev_[a[i].second].box;
      ++i, ++j;
    }
  }
  prev_ = std::move(now);
  merge_bands(c.bands, h, 12, 6);
  c.any = !c.bands.empty();
  if (c.any && band_rows(c.bands) > h * 8 / 10) {
    c.full = true;
    c.bands.assign(1, Band{0, h});
  }
  return c;
}
void Board::draw(const std::vector<xrtext::BoardPiece>& pieces, int w, int h, uint8_t* rgba, const Change& c) {
  if (!c.any) return;
  if (c.full) {
    xrtext::board(rgba, w, h, pieces);
    return;
  }
  std::vector<Band> bands = c.bands;
  merge_bands(bands, h, 12, 6);
  if (band_rows(bands) > h * 8 / 10) {
    xrtext::board(rgba, w, h, pieces);
    return;
  }
  for (const Band& d : bands) xrtext::board_band(rgba, w, h, pieces, d.y0, d.y1);
}
std::vector<Band> Board::render(const std::vector<xrtext::BoardPiece>& pieces, int w, int h, uint8_t* rgba, bool* full) {
  const Change c = diff(pieces, w, h);
  if (full != nullptr) *full = c.full;
  draw(pieces, w, h, rgba, c);
  return c.any ? c.bands : std::vector<Band>();
}
bool same_look(const xrbook::CursorBox& a, const xrbook::CursorBox& b) {
  if (a.on != b.on) return false;
  if (!a.on) return true;
  return std::fabs((a.x1 - a.x0) - (b.x1 - b.x0)) < 0.5f && std::fabs((a.y1 - a.y0) - (b.y1 - b.y0)) < 0.5f && std::fabs(a.r - b.r) < 0.25f &&
         a.rgb == b.rgb && a.glow == b.glow && std::fabs(a.k - b.k) < 1e-4f;
}

Atlas cursor_atlas(const xrbook::CursorBox& b, uint8_t* rgba, int stride) {
  Atlas at;
  if (!b.on) return at;
  const float k = b.k > 0.0f ? b.k : 1.0f;
  const float w = std::max(1.0f, b.x1 - b.x0), h = std::max(1.0f, b.y1 - b.y0);
  const float g = kGrow * k, tube = kTube * k, core = kCore * k, halo = kHalo * k;
  const float pad = g + (b.glow ? halo : 0.0f) + 3.0f;
  const float cwPic = w + 2.0f * pad, chPic = h + 2.0f * pad;
  const float s = std::min(1.0f, std::min(static_cast<float>(kCellMaxW) / cwPic, static_cast<float>(kCellMaxH) / chPic));
  at.on = true;
  at.scale = s;
  at.cellW = std::min(kCellMaxW, static_cast<int>(std::ceil(cwPic * s)));
  at.cellH = std::min(kCellMaxH, static_cast<int>(std::ceil(chPic * s)));
  at.hx = 0.5f * w + g;
  at.hy = 0.5f * h + g;
  const float r = std::max(0.0f, std::min(b.r, 0.5f * std::min(w, h)));
  const float rF = r + g, rC = std::max(2.0f * k, rF - 1.5f * k);
  const float bh = std::max(10.0f * k, 0.62f * (h + 2.0f * g)), bc = 0.5f * h;
  for (int cell = 0; cell < 4; ++cell) {
    const int ox = (cell % 2) * at.cellW, oy = (cell / 2) * at.cellH;
    for (int v = 0; v < at.cellH; ++v) {
      uint8_t* row = rgba + (static_cast<size_t>(oy + v) * stride + ox) * 4;
      const float Y = (v + 0.5f) / s - pad;
      for (int u = 0; u < at.cellW; ++u) {
        const float X = (u + 0.5f) / s - pad;
        Px c;
        const float sdF = rr_sd(X, Y, -g, -g, w + g, h + g, rF);
        if (b.glow && sdF > 0.0f && sdF < halo) {
          const float f = 1.0f - sdF / halo;
          c.over(b.rgb, 0.6f * f * f);
        }
        const float sdR = rr_sd(X, Y, 0.0f, 0.0f, w, h, r);
        const float covR = clamp01(0.5f - sdR * s);
        if (covR > 0.0f) {
          c.over(b.rgb, 0.12f * covR);
          if (kFlash[cell] > 0.0f) c.over(0xFFFFFF, kFlash[cell] * covR);
        }
        c.over(b.rgb, std::min(0.5f - sdF * s, 0.5f + (sdF + tube) * s));
        const float sdC = rr_sd(X, Y, -g + 1.5f * k, -g + 1.5f * k, w + g - 1.5f * k, h + g - 1.5f * k, rC);
        c.over(0xFFFFFF, 0.95f * clamp01(std::min(0.5f - sdC * s, 0.5f + (sdC + core) * s)));
        const float sdB = rr_sd(X, Y, -g + 7.0f * k, bc - 0.5f * bh, -g + 17.0f * k, bc + 0.5f * bh, 5.0f * k);
        c.over(0xFFFFFF, 0.5f - sdB * s);
        uint8_t* o = row + static_cast<size_t>(u) * 4;
        o[0] = static_cast<uint8_t>(std::lround(clamp01(c.r) * 255.0f));
        o[1] = static_cast<uint8_t>(std::lround(clamp01(c.g) * 255.0f));
        o[2] = static_cast<uint8_t>(std::lround(clamp01(c.b) * 255.0f));
        o[3] = static_cast<uint8_t>(std::lround(clamp01(c.a) * 255.0f));
      }
    }
  }
  return at;
}

Pose cursor_pose(const xrbook::CursorBox& b, const Atlas& a, float moveAgo, float pressAgo) {
  Pose p;
  if (!b.on || !a.on) return p;
  const float k = b.k > 0.0f ? b.k : 1.0f;
  const float gm = moveAgo >= 0.0f && moveAgo < 0.22f ? 1.0f - moveAgo / 0.22f : 0.0f;
  const float pop = gm * gm;  // (out fast, settling)
  const float press = pressAgo >= 0.0f && pressAgo < 0.28f ? 1.0f - pressAgo / 0.28f : 0.0f;
  const float grow = (kGrow + 16.0f * pop - 9.0f * press) * k;  // (cursor_frame's)
  const float w = std::max(1.0f, b.x1 - b.x0), h = std::max(1.0f, b.y1 - b.y0);
  const float sx = (0.5f * w + grow) / std::max(1.0f, a.hx), sy = (0.5f * h + grow) / std::max(1.0f, a.hy);
  p.on = true;
  p.cell = press > 0.66f ? 1 : (press > 0.33f ? 2 : (press > 0.04f ? 3 : 0));
  p.cx = 0.5f * (b.x0 + b.x1);
  p.cy = 0.5f * (b.y0 + b.y1);
  p.w = a.cellW / a.scale * sx;
  p.h = a.cellH / a.scale * sy;
  return p;
}

void History::add(uint64_t version, const std::vector<Band>& bands, int h) {
  (void)h;
  q_.emplace_back(version, bands);
  while (q_.size() > 24) q_.pop_front();
}
bool History::since(uint64_t have, uint64_t want, int h, std::vector<Band>* out) const {
  out->clear();
  if (have == 0) return false;
  if (have >= want) return true;
  uint64_t n = 0;
  for (const auto& e : q_)
    if (e.first > have && e.first <= want) {
      ++n;
      out->insert(out->end(), e.second.begin(), e.second.end());
    }
  if (n != want - have) return false;  // (older than the history: all of it)
  merge_bands(*out, h, 4, 8);
  return band_rows(*out) <= h * 3 / 4;
}

}  // namespace xrscreen
