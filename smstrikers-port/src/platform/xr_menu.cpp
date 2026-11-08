// strikers-quest r157 (menu overhaul): the menus' kit (xr_menu.h). The numbers are tools/outside/menus_kit.py's.
#include "xr_menu.h"

#include <algorithm>
#include <cmath>

namespace xrmenu {

uint32_t mix(uint32_t a, uint32_t b, float t) {
  t = std::max(0.0f, std::min(1.0f, t));
  uint32_t out = 0;
  for (int s = 16; s >= 0; s -= 8) {
    const float x = static_cast<float>((a >> s) & 255), y = static_cast<float>((b >> s) & 255);
    out |= static_cast<uint32_t>(std::lround(x + (y - x) * t)) << s;
  }
  return out;
}

uint32_t glyph_rgb(const std::string& g) {
  if (g == "A") return kGreen;
  if (g == "B") return kRed;
  if (g == "X") return kBlue;
  if (g == "Y") return kYellow;
  return kSlate;
}

// ---- the builder
void Kit::box(float x0, float y0, float x1, float y1, float r, uint32_t rgb, float a, uint32_t rgb2) {
  if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) return;
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kBox;
  p.x0 = x0;
  p.y0 = y0;
  p.x1 = x1;
  p.y1 = y1;
  p.radius = std::min(r, 0.5f * std::min(x1 - x0, y1 - y0));
  p.rgb = rgb;
  p.rgb2 = rgb2;
  p.alpha = a;
  v.push_back(p);
}
void Kit::ring(float x0, float y0, float x1, float y1, float r, float width, uint32_t rgb, float a) {
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kRing;
  p.x0 = x0;
  p.y0 = y0;
  p.x1 = x1;
  p.y1 = y1;
  p.radius = std::min(r, 0.5f * std::min(x1 - x0, y1 - y0));
  p.outline = width;
  p.rgb = rgb;
  p.alpha = a;
  v.push_back(p);
}
void Kit::glow(float x0, float y0, float x1, float y1, uint32_t rgb, float a) {
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kGlow;
  p.x0 = x0;
  p.y0 = y0;
  p.x1 = x1;
  p.y1 = y1;
  p.rgb = rgb;
  p.alpha = a;
  v.push_back(p);
}
void Kit::poly(const std::vector<float>& xy, uint32_t rgb, float a) {
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kPoly;
  p.pts = xy;
  p.rgb = rgb;
  p.alpha = a;
  v.push_back(p);
}
void Kit::poly(std::initializer_list<float> xy, uint32_t rgb, float a) { poly(std::vector<float>(xy), rgb, a); }
static void words(Kit& K, xrtext::BoardPiece::Kind kind, const std::string& t, float x, float y, float px, float room,
                  uint32_t rgb, float outline) {
  if (t.empty()) return;
  xrtext::BoardPiece p;
  p.kind = kind;
  p.x0 = x;
  p.y0 = y;
  p.x1 = room;
  p.y1 = px;
  p.rgb = rgb;
  p.outline = outline;
  p.text = t;
  K.v.push_back(p);
}
void Kit::text(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline) {
  words(*this, xrtext::BoardPiece::kText, t, x, y, px, room, rgb, outline);
}
void Kit::left(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline) {
  words(*this, xrtext::BoardPiece::kTextLeft, t, x, y, px, room, rgb, outline);
}
void Kit::right(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline) {
  words(*this, xrtext::BoardPiece::kTextRight, t, x, y, px, room, rgb, outline);
}
float Kit::width(const std::string& t, float px) const { return xrtext::board_text_width(t, px); }

// ---- the pieces
void frame(Kit& K, float w, float h, uint32_t accent, float radius) {
  K.box(6, 6, w - 6, h - 6, radius, accent);
  K.box(16, 16, w - 16, h - 16, radius - 6, kNavyTop, 0.98f, kNavyBot);
}

void focus(Kit& K, float x0, float y0, float x1, float y1, float r) {
  K.glow(x0 - 40, y0 - 40, x1 + 40, y1 + 40, kGold, 0.35f);
  K.ring(x0 - 9, y0 - 9, x1 + 9, y1 + 9, r + 9, 10, kGold);
}

static float glyph_width(const Kit& K, const std::string& g, float size) {
  return g.size() > 1 ? std::max(size, K.width(g, 0.6f * size) + 0.75f * size) : size;
}
float glyph(Kit& K, const std::string& g, float x, float cy, float size) {
  const float r = 0.5f * size, wide = glyph_width(K, g, size);
  K.box(x - 3, cy - r - 3, x + wide + 3, cy + r + 3, r + 3, kWhite);
  K.box(x, cy - r, x + wide, cy + r, r, glyph_rgb(g));
  if (g == "L" || g == "R") {  // a stick: a ring inside the disc
    K.ring(x + 0.2f * size, cy - 0.3f * size, x + 0.8f * size, cy + 0.3f * size, 0.3f * size, 3, kWhite);
    K.text(g, x + 0.5f * wide, cy, 0.42f * size, wide, kWhite, 0.0f);
  } else {
    K.text(g, x + 0.5f * wide, cy, 0.6f * size, wide, kWhite, 2.0f);
  }
  return wide;
}

void icon(Kit& K, const std::string& kind, float cx, float cy, float s, uint32_t rgb, float a) {
  const float h = 0.5f * s, w = std::max(3.0f, s * 0.09f);
  if (kind == "play") {
    K.poly({cx - 0.35f * s, cy - 0.45f * s, cx + 0.45f * s, cy, cx - 0.35f * s, cy + 0.45f * s}, rgb, a);
  } else if (kind == "gear") {
    for (int k = 0; k < 8; ++k) {
      const float t = k * 0.785398f, c = std::cos(t), sn = std::sin(t);
      const float bx = cx + 0.4f * s * c, by = cy + 0.4f * s * sn;
      K.box(bx - 0.09f * s, by - 0.09f * s, bx + 0.09f * s, by + 0.09f * s, 0.03f * s, rgb, a);
    }
    K.box(cx - 0.36f * s, cy - 0.36f * s, cx + 0.36f * s, cy + 0.36f * s, 0.36f * s, rgb, a);
    K.box(cx - 0.14f * s, cy - 0.14f * s, cx + 0.14f * s, cy + 0.14f * s, 0.14f * s, kDark, a * 0.9f);
  } else if (kind == "trophy") {
    K.poly({cx - 0.36f * s, cy - h, cx + 0.36f * s, cy - h, cx + 0.26f * s, cy + 0.05f * s, cx - 0.26f * s, cy + 0.05f * s}, rgb, a);
    K.ring(cx - h, cy - 0.4f * s, cx - 0.2f * s, cy - 0.05f * s, 0.15f * s, w, rgb, a);
    K.ring(cx + 0.2f * s, cy - 0.4f * s, cx + h, cy - 0.05f * s, 0.15f * s, w, rgb, a);
    K.box(cx - 0.07f * s, cy, cx + 0.07f * s, cy + 0.3f * s, 2, rgb, a);
    K.box(cx - 0.28f * s, cy + 0.3f * s, cx + 0.28f * s, cy + h, 4, rgb, a);
  } else if (kind == "chart") {
    static const float kH[3] = {0.35f, 0.6f, 0.85f};
    for (int k = 0; k < 3; ++k) {
      const float x = cx - 0.42f * s + k * 0.3f * s;
      K.box(x, cy + h - kH[k] * s, x + 0.22f * s, cy + h, 3, rgb, a);
    }
  } else if (kind == "door") {
    K.box(cx - 0.32f * s, cy - h, cx + 0.32f * s, cy + h, 4, rgb, a);
    K.box(cx - 0.22f * s, cy - 0.4f * s, cx + 0.22f * s, cy + h, 3, kDark, a * 0.85f);
    K.box(cx + 0.08f * s, cy + 0.02f * s, cx + 0.16f * s, cy + 0.1f * s, 0.04f * s, rgb, a);
  } else if (kind == "eye") {  // VIEW: a screen with a table under it
    K.box(cx - h, cy - 0.42f * s, cx + h, cy + 0.12f * s, 6, rgb, a);
    K.box(cx - 0.42f * s, cy - 0.34f * s, cx + 0.42f * s, cy + 0.04f * s, 4, kDark, a * 0.8f);
    K.poly({cx - 0.3f * s, cy + 0.26f * s, cx + 0.3f * s, cy + 0.26f * s, cx + h, cy + h, cx - h, cy + h}, rgb, a);
  } else if (kind == "home") {
    K.poly({cx - h, cy - 0.02f * s, cx, cy - h, cx + h, cy - 0.02f * s}, rgb, a);
    K.box(cx - 0.36f * s, cy - 0.05f * s, cx + 0.36f * s, cy + h, 4, rgb, a);
    K.box(cx - 0.1f * s, cy + 0.15f * s, cx + 0.1f * s, cy + h, 3, kDark, a * 0.85f);
  } else if (kind == "table") {
    for (int k = 0; k < 4; ++k) {
      const float y = cy - 0.4f * s + k * 0.27f * s;
      K.box(cx - h, y - 0.07f * s, cx - 0.3f * s, y + 0.07f * s, 3, k == 0 ? kGold : rgb, a);
      K.box(cx - 0.22f * s, y - 0.07f * s, cx + h, y + 0.07f * s, 3, rgb, a);
    }
  } else if (kind == "shield") {
    K.poly({cx - 0.42f * s, cy - 0.4f * s, cx, cy - h, cx + 0.42f * s, cy - 0.4f * s, cx + 0.38f * s, cy + 0.1f * s, cx, cy + h,
            cx - 0.38f * s, cy + 0.1f * s},
           rgb, a);
    K.poly({cx, cy - 0.28f * s, cx + 0.2f * s, cy + 0.22f * s, cx - 0.2f * s, cy + 0.22f * s}, kDark, a * 0.8f);
  } else if (kind == "cards") {
    for (int k = 0; k < 3; ++k) {
      const float dx = (k - 1) * 0.16f * s;
      K.box(cx + dx - 0.25f * s, cy - 0.35f * s + (k == 1 ? -0.03f * s : 0.0f), cx + dx + 0.25f * s, cy + 0.35f * s, 0.06f * s,
            k == 1 ? rgb : mix(rgb, kDark, 0.25f), a);
      K.ring(cx + dx - 0.25f * s, cy - 0.35f * s + (k == 1 ? -0.03f * s : 0.0f), cx + dx + 0.25f * s, cy + 0.35f * s, 0.06f * s, 3,
             kDark, a);
    }
  } else if (kind == "news") {
    K.box(cx - h, cy - 0.4f * s, cx + h, cy + 0.4f * s, 6, rgb, a);
    K.box(cx - 0.4f * s, cy - 0.3f * s, cx + 0.4f * s, cy - 0.14f * s, 3, kDark, a * 0.85f);
    for (int k = 0; k < 3; ++k)
      K.box(cx - 0.4f * s, cy - 0.04f * s + k * 0.12f * s, cx - 0.04f * s, cy + 0.02f * s + k * 0.12f * s, 2, kDark, a * 0.6f);
    K.box(cx + 0.04f * s, cy - 0.04f * s, cx + 0.4f * s, cy + 0.3f * s, 3, kDark, a * 0.45f);
  } else if (kind == "swap") {
    K.box(cx - 0.4f * s, cy - 0.18f * s - w * 0.6f, cx + 0.3f * s, cy - 0.18f * s + w * 0.6f, 2, rgb, a);
    K.poly({cx + 0.18f * s, cy - 0.36f * s, cx + 0.48f * s, cy - 0.18f * s, cx + 0.18f * s, cy}, rgb, a);
    K.box(cx - 0.3f * s, cy + 0.2f * s - w * 0.6f, cx + 0.4f * s, cy + 0.2f * s + w * 0.6f, 2, rgb, a);
    K.poly({cx - 0.18f * s, cy + 0.02f * s, cx - 0.48f * s, cy + 0.2f * s, cx - 0.18f * s, cy + 0.38f * s}, rgb, a);
  } else if (kind == "evo") {  // up arrows
    for (int k = 0; k < 2; ++k) {
      const float y = cy - 0.1f * s + k * 0.36f * s;
      K.poly({cx - 0.4f * s, y + 0.12f * s, cx, y - 0.3f * s, cx + 0.4f * s, y + 0.12f * s}, rgb, a);
    }
  } else if (kind == "star") {
    std::vector<float> pts;
    for (int k = 0; k < 10; ++k) {
      const float r = k % 2 == 0 ? h : 0.2f * s, t = -1.5707963f + k * 0.6283185f;
      pts.push_back(cx + r * std::cos(t));
      pts.push_back(cy + r * std::sin(t));
    }
    // (a star isn't convex: a burst piece draws it)
    xrtext::BoardPiece p;
    p.kind = xrtext::BoardPiece::kBurst;
    p.x0 = cx - h;
    p.y0 = cy - h;
    p.x1 = cx + h;
    p.y1 = cy + h;
    p.points = 5;
    p.inner = 0.4f;
    p.rgb = rgb;
    p.alpha = a;
    K.v.push_back(p);
  } else {
    K.box(cx - h, cy - h, cx + h, cy + h, h, rgb, a);
  }
}

float breadcrumb(Kit& K, float x, float cy, const std::vector<Crumb>& parts, float px) {
  for (size_t i = 0; i < parts.size(); ++i) {
    const Crumb& c = parts[i];
    if (i + 1 == parts.size()) {
      const float wd = K.width(c.text, px) + 36.0f;
      K.box(x, cy - 0.62f * px, x + wd, cy + 0.62f * px, 0.62f * px, c.rgb != 0 ? c.rgb : kTileOn);
      K.text(c.text, x + 0.5f * wd, cy, px, wd, kWhite, 3.0f);
      x += wd;
    } else {
      const float wd = std::min(800.0f, K.width(c.text, px));
      K.left(c.text, x, cy, px, 800.0f, kInkSoft, 2.0f);
      x += wd + 14.0f;
      K.poly({x, cy - 0.3f * px, x + 0.32f * px, cy, x, cy + 0.3f * px}, kGold);
      x += 0.32f * px + 16.0f;
    }
  }
  return x;
}

void hint_bar(Kit& K, float x0, float x1, float cy, const std::vector<Hint>& hints, float size, float px) {
  K.box(x0, cy - 46, x1, cy + 46, 46, 0x060912, 0.75f);
  const float gap = 54.0f;
  auto split = [](const std::string& g) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < g.size()) {
      const size_t sp = g.find(' ', at);
      out.push_back(g.substr(at, sp == std::string::npos ? std::string::npos : sp - at));
      if (sp == std::string::npos) break;
      at = sp + 1;
    }
    return out;
  };
  std::vector<float> widths;
  float total = 0.0f;
  for (const Hint& h : hints) {
    float gw = 0.0f;
    const std::vector<std::string> gs = split(h.glyph);
    for (size_t k = 0; k < gs.size(); ++k) gw += glyph_width(K, gs[k], size) + (k > 0 ? 8.0f : 0.0f);
    widths.push_back(gw + 14.0f + K.width(h.words, px));
    total += widths.back();
  }
  total += gap * std::max<size_t>(1, hints.size()) - gap;
  float scale = 1.0f;  // (too many for the bar: the whole bar a little smaller)
  if (total > x1 - x0 - 60.0f) scale = (x1 - x0 - 60.0f) / total;
  const float sz = size * scale, tp = px * scale, gp = gap * scale;
  float x = 0.5f * (x0 + x1 - total * scale);
  for (size_t i = 0; i < hints.size(); ++i) {
    const Hint& h = hints[i];
    const float start = x;
    const std::vector<std::string> gs = split(h.glyph);
    for (const std::string& g : gs) x += glyph(K, g, x, cy, sz) + 8.0f * scale;
    x += 6.0f * scale;
    K.left(h.words, x, cy + 2, tp, 900.0f, 0xE6ECF5, 2.0f);
    x += K.width(h.words, tp) + gp;
    if (h.dim) K.box(start - 6, cy - 34, x - gp + 6, cy + 34, 30, 0x060912, 0.72f);  // kept, faded
  }
}

void section_tabs(Kit& K, float x0, float x1, float y0, float y1, const std::vector<std::string>& names,
                  const std::vector<uint32_t>& colours, const std::vector<std::string>& icons, int at,
                  const std::vector<std::string>& badges, float px) {
  const float cy = 0.5f * (y0 + y1);
  glyph(K, "LT", x0, cy, 58);
  glyph(K, "RT", x1 - 82, cy, 58);
  const float ax0 = x0 + 110, ax1 = x1 - 110, tw = (ax1 - ax0) / std::max<size_t>(1, names.size());
  const float hh = 0.5f * (y1 - y0);
  for (size_t i = 0; i < names.size(); ++i) {
    const bool on = static_cast<int>(i) == at;
    const uint32_t c = i < colours.size() ? colours[i] : kTileOn;
    const float tx0 = ax0 + i * tw + 8, tx1 = ax0 + (i + 1) * tw - 8;
    if (on) {
      K.glow(tx0 - 30, y0 - 30, tx1 + 30, y1 + 30, c, 0.5f);
      K.box(tx0 - 5, y0 - 5, tx1 + 5, y1 + 5, hh + 5, kWhite);
    }
    K.box(tx0, y0, tx1, y1, hh, on ? c : kPill);
    if (!on) K.box(tx0 + hh - 4, y1 - 9, tx1 - hh + 4, y1 - 3, 3, c, 0.9f);
    const float s = hh, wd = K.width(names[i], px);
    const bool hasIcon = i < icons.size() && !icons[i].empty();
    if (hasIcon) {
      const float cx = 0.5f * (tx0 + tx1) - 0.5f * (wd + s + 18);
      icon(K, icons[i], cx + s / 2, cy, s, on ? kWhite : kInkSoft);
      K.left(names[i], cx + s + 18, cy + 2, px, tx1 - cx - s - 30, on ? kWhite : kInkSoft, on ? 3.0f : 2.0f);
    } else {
      K.text(names[i], 0.5f * (tx0 + tx1), cy + 2, px, tx1 - tx0 - 24, on ? kWhite : kInkSoft, on ? 3.0f : 2.0f);
    }
    if (i < badges.size() && !badges[i].empty()) {  // a red count on a section that wants you
      const float bx = tx1 - 26;
      K.box(bx - 29, y0 - 11, bx + 29, y0 + 47, 29, kWhite);
      K.box(bx - 26, y0 - 8, bx + 26, y0 + 44, 26, kBadge);
      K.text(badges[i], bx, y0 + 18, 32, 44, kWhite, 2.0f);
    }
  }
}

float rail(Kit& K, float x0, float x1, float y0, const std::vector<RailItem>& items, int at, bool focused, uint32_t colour,
           float row, float gap, float px) {
  float y = y0;
  for (size_t i = 0; i < items.size(); ++i) {
    const RailItem& it = items[i];
    const bool on = static_cast<int>(i) == at;
    if (on && focused) focus(K, x0, y, x1, y + row, 0.5f * row);
    K.box(x0, y, x1, y + row, 0.5f * row, on ? colour : kTile);
    if (on) K.poly({x1 - 4, y + 0.5f * row - 18, x1 + 22, y + 0.5f * row, x1 - 4, y + 0.5f * row + 18}, colour);
    const bool badge = !it.badge.empty();
    K.left(it.name, x0 + 34, y + 0.5f * row + 3, px, x1 - x0 - 70 - (badge ? 60 : 0),
           it.dim ? kInkDim : (on ? kWhite : kInkSoft), on ? 3.0f : 2.0f);
    if (badge) {
      K.box(x1 - 66, y + 0.5f * row - 22, x1 - 22, y + 0.5f * row + 22, 22, kBadge);
      K.text(it.badge, x1 - 44, y + 0.5f * row + 2, 30, 40, kWhite, 2.0f);
    }
    y += row + gap;
  }
  return y;
}

void band(Kit& K, float x0, float y0, float x1, float y1, float r, float h, uint32_t colour, float a) {
  // (the board has no clipping: the strip is a box of the tile's radius, cut to height h by its own bottom edge being
  // round - the caller draws the band first and the body over it from y0 + h)
  K.box(x0, y0, x1, std::min(y1, y0 + std::max(h, 2.0f * r)), r, colour, a);
}

void tile(Kit& K, float x0, float y0, float x1, float y1, const TileSpec& t, float r) {
  if (t.on) focus(K, x0, y0, x1, y1, r);
  band(K, x0, y0, x1, y1, r, 14.0f, t.colour, t.dim ? 0.4f : 0.9f);
  K.box(x0, y0 + 14.0f, x1, y1, r, mix(t.colour, kDark, t.on ? 0.15f : 0.55f), 1.0f, mix(t.colour, kDark, t.on ? 0.55f : 0.8f));
  const uint32_t ink = t.dim ? kInkDim : kWhite;
  if (t.layout == 0) {
    if (!t.icon.empty()) {
      const float s = t.iconS > 0.0f ? t.iconS : std::min(x1 - x0, y1 - y0) * 0.34f;
      icon(K, t.icon, 0.5f * (x0 + x1), y0 + 0.36f * (y1 - y0), s, ink);
      K.text(t.title, 0.5f * (x0 + x1), y0 + 0.7f * (y1 - y0), t.titlePx, x1 - x0 - 40, t.on ? kCream : ink, 4.0f);
      if (!t.sub.empty()) K.text(t.sub, 0.5f * (x0 + x1), y0 + 0.86f * (y1 - y0), t.subPx, x1 - x0 - 40, kInkSoft, 2.0f);
    } else {
      const float cy = 0.5f * (y0 + y1);
      K.text(t.title, 0.5f * (x0 + x1), cy - (t.sub.empty() ? 0.0f : 0.28f * t.subPx), t.titlePx, x1 - x0 - 40,
             t.on ? kCream : ink, 4.0f);
      if (!t.sub.empty()) K.text(t.sub, 0.5f * (x0 + x1), cy + 0.5f * t.titlePx + 4, t.subPx, x1 - x0 - 40, kInkSoft, 2.0f);
    }
  } else {
    const float cy = 0.5f * (y0 + y1) + 6;
    const float s = t.iconS > 0.0f ? t.iconS : (y1 - y0) * 0.5f;
    float tx = x0 + 40;
    if (!t.icon.empty()) {
      icon(K, t.icon, x0 + 40 + s / 2, cy, s, ink);
      tx = x0 + 70 + s;
    }
    if (!t.sub.empty()) {
      K.left(t.title, tx, cy - 0.42f * t.titlePx + 4, t.titlePx, x1 - tx - 30, t.on ? kCream : ink, 4.0f);
      K.left(t.sub, tx, cy + 0.5f * t.subPx + 8, t.subPx, x1 - tx - 30, kInkSoft, 2.0f);
    } else {
      K.left(t.title, tx, cy, t.titlePx, x1 - tx - 30, t.on ? kCream : ink, 4.0f);
    }
  }
  if (!t.badge.empty()) {
    const float bw = K.width(t.badge, 30) + 30;
    K.box(x1 - bw - 18, y0 + 26, x1 - 18, y0 + 70, 22, kBadge);
    K.text(t.badge, x1 - 18 - bw / 2, y0 + 49, 30, bw, kWhite, 2.0f);
  }
}

void value_row(Kit& K, float x0, float y0, float x1, float y1, const std::string& label, const std::string& value, bool on,
               int lit, int segs, bool dim, const std::string& why, float px) {
  const float r = y1 - y0 < 90.0f ? 0.5f * (y1 - y0) : 28.0f;
  if (on) focus(K, x0, y0, x1, y1, r);
  K.box(x0, y0, x1, y1, r, on ? kRowOn : kTile);
  const float cy = 0.5f * (y0 + y1) + 3, tx = x0 + 34, vx1 = x1 - 26;
  float vx0 = vx1;
  if (dim) {
    const float pw = std::max(250.0f, K.width(why, px - 2) + 60.0f);
    vx0 = vx1 - pw;
    K.right(why, vx1, cy, px - 2, pw, kInkDim, 2.0f);
  } else if (segs > 0) {
    const float sw = 40.0f;
    const float bx0 = vx1 - segs * (sw + 8) + 8;
    for (int k = 0; k < segs; ++k)
      K.box(bx0 + k * (sw + 8), cy - 18, bx0 + k * (sw + 8) + sw, cy + 16, 7, k < lit ? kGold : 0x2A3550);
    vx0 = bx0;
  } else {
    const float pw = std::max(250.0f, K.width(value, px - 4) + 130.0f);
    vx0 = vx1 - pw;
    K.box(vx0, cy - 0.5f * (y1 - y0) + 12, vx1, cy + 0.5f * (y1 - y0) - 18, 26, kDark, 0.7f);
    if (on) {
      K.poly({vx0 + 18, cy - 3, vx0 + 40, cy - 19, vx0 + 40, cy + 13}, kGold);
      K.poly({vx1 - 18, cy - 3, vx1 - 40, cy - 19, vx1 - 40, cy + 13}, kGold);
    }
    K.text(value, 0.5f * (vx0 + vx1), cy, px - 4, pw - 100, on ? kGold : kInkSoft, 2.0f);
  }
  K.left(label, tx, cy, px, vx0 - tx - 24, dim ? kInkDim : (on ? kCream : kWhite), 3.0f);
}

void confirm(Kit& K, float w, float h, const std::string& question, const std::string& line, const std::string& yes,
             const std::string& no) {
  K.box(16, 16, w - 16, h - 16, 24, 0x03050A, 0.72f);  // the page behind it, dimmed
  const float bw = std::min(w - 160.0f, std::max(1100.0f, K.width(question, 72) + 200.0f)), bh = line.empty() ? 380.0f : 460.0f;
  const float x0 = 0.5f * (w - bw), y0 = 0.5f * (h - bh), x1 = x0 + bw, y1 = y0 + bh, cx = 0.5f * w;
  K.box(x0 - 8, y0 - 8, x1 + 8, y1 + 8, 44, kGold);
  K.box(x0, y0, x1, y1, 38, kNavyTop, 1.0f, kNavyBot);
  K.text(question, cx, y0 + 110, 72, bw - 80, kCream, 4.0f);
  if (!line.empty()) K.text(line, cx, y0 + 196, 40, bw - 80, kInkSoft, 2.0f);
  // the answers as two pills, A YES / B NO
  const float py = y1 - 110, pw = 400.0f;
  for (int k = 0; k < 2; ++k) {
    const float px0 = k == 0 ? cx - pw - 30 : cx + 30, px1 = px0 + pw;
    K.box(px0, py - 52, px1, py + 52, 52, k == 0 ? 0x1F6E31 : 0x7A2219);
    const float gw = glyph(K, k == 0 ? "A" : "B", px0 + 26, py, 64);
    K.left(k == 0 ? yes : no, px0 + 46 + gw, py + 2, 52, px1 - px0 - gw - 70, kWhite, 3.0f);
  }
}

void scroll_note(Kit& K, float cx, float cy, int first, int shown, int total, const std::string& what) {
  if (total <= shown) return;
  const int pages = total - shown + 1;
  const std::string t = what + " " + std::to_string(first + 1) + " OF " + std::to_string(pages);
  const float wd = K.width(t, 40) + 240;
  K.box(cx - wd / 2, cy - 30, cx + wd / 2, cy + 30, 30, kDark, 0.75f);
  K.text(t, cx, cy + 1, 40, wd - 200, kInkSoft, 2.0f);
  const uint32_t up = first > 0 ? kGold : 0x3A4560, down = first + shown < total ? kGold : 0x3A4560;
  K.poly({cx - wd / 2 + 30, cy + 10, cx - wd / 2 + 52, cy - 14, cx - wd / 2 + 74, cy + 10}, up);
  K.poly({cx + wd / 2 - 74, cy - 10, cx + wd / 2 - 52, cy + 14, cx + wd / 2 - 30, cy - 10}, down);
}

}  // namespace xrmenu
