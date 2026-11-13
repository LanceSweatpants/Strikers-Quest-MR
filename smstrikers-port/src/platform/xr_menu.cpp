// strikers-quest r157 (menu overhaul): the menus' kit (xr_menu.h). The numbers are tools/outside/menus_kit.py's.
#include "xr_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

// ---- r236: LOOK B "BROADCAST PREMIUM" (xr_menu.h)
namespace {
thread_local bool t_on = false;
thread_local uint32_t t_accent = 0xE8402A;
// the last piece pushed, marked as the kit's own (broadcast_pieces leaves it)
xrtext::BoardPiece& styled(Kit& K) {
  K.v.back().styled = true;
  return K.v.back();
}
void sbox(Kit& K, float x0, float y0, float x1, float y1, uint32_t rgb, float a = 1.0f, float cut = 0.0f, int cuts = 5, float lean = 0.0f,
          uint32_t rgb2 = 0, float a2 = -1.0f) {
  if (x1 - x0 < 1.0f || y1 - y0 < 1.0f) return;
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kBox;
  p.x0 = x0;
  p.y0 = y0;
  p.x1 = x1;
  p.y1 = y1;
  p.rgb = rgb;
  p.alpha = a;
  p.rgb2 = rgb2;
  p.alpha2 = a2;
  p.cut = cut;
  p.cuts = cuts;
  p.lean = lean;
  p.styled = true;
  K.v.push_back(p);
}
void sring(Kit& K, float x0, float y0, float x1, float y1, float width, uint32_t rgb, float a = 1.0f, float cut = 0.0f, int cuts = 5,
           float lean = 0.0f, float radius = 0.0f) {
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kRing;
  p.x0 = x0;
  p.y0 = y0;
  p.x1 = x1;
  p.y1 = y1;
  p.radius = radius;
  p.outline = width;
  p.rgb = rgb;
  p.alpha = a;
  p.cut = cut;
  p.cuts = cuts;
  p.lean = lean;
  p.styled = true;
  K.v.push_back(p);
}
void sglow(Kit& K, float x0, float y0, float x1, float y1, uint32_t rgb, float a) {
  K.glow(x0, y0, x1, y1, rgb, a);
  styled(K);
}
void spoly(Kit& K, std::initializer_list<float> xy, uint32_t rgb, float a = 1.0f) {
  K.poly(xy, rgb, a);
  styled(K);
}
float luma(uint32_t c) {
  return (0.2126f * ((c >> 16) & 255) + 0.7152f * ((c >> 8) & 255) + 0.0722f * (c & 255)) / 255.0f;
}
float sat(uint32_t c) {
  const int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
  const int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
  return mx > 0 ? static_cast<float>(mx - mn) / mx : 0.0f;
}
}  // namespace

bool broadcast() { return t_on; }
uint32_t accent() { return t_accent; }
BroadcastScope::BroadcastScope(uint32_t accentRgb) : wasOn(t_on), wasAccent(t_accent), face(xrtext::kFaceBroadcast) {
  t_on = true;
  t_accent = accentRgb;
}
PlainScope::PlainScope(bool active) : wasOn(t_on), face(active ? xrtext::kFaceLucky : xrtext::measure_face()) {
  if (active) t_on = false;
}
PlainScope::~PlainScope() { t_on = wasOn; }
BroadcastScope::~BroadcastScope() {
  t_on = wasOn;
  t_accent = wasAccent;
}

uint32_t club_accent(uint32_t c1, uint32_t c2) {
  // the more colourful of the two that isn't near black / white; pulled into a range white words read on
  auto score = [](uint32_t c) { return c == 0 ? -1.0f : sat(c) * (luma(c) < 0.08f ? 0.2f : 1.0f) * (luma(c) > 0.85f ? 0.2f : 1.0f); };
  uint32_t c = score(c1) >= score(c2) ? c1 : c2;
  if (c == 0 || score(c) < 0.25f) return 0xE8402A;
  for (int k = 0; k < 8 && luma(c) < 0.16f; ++k) c = mix(c, 0xFFFFFF, 0.18f);
  for (int k = 0; k < 8 && luma(c) > 0.55f; ++k) c = mix(c, 0x000000, 0.15f);
  return c;
}

float say(Kit& K, const std::string& t, float x, float y, float px, uint32_t rgb, char anchor, float room, bool italic, float track,
          float shadow) {
  if (t.empty()) return 0.0f;
  const int face = italic ? xrtext::kFaceBroadcast : xrtext::kFaceBroadcastUp;
  float wd = xrtext::face_text_width(t, px, face, track);
  if (room > 0.0f) wd = std::min(wd, room);
  xrtext::BoardPiece p;
  p.kind = anchor == 'c' ? xrtext::BoardPiece::kText : (anchor == 'r' ? xrtext::BoardPiece::kTextRight : xrtext::BoardPiece::kTextLeft);
  p.x0 = x;
  p.y0 = y;
  p.x1 = room > 0.0f ? room : 0.0f;
  p.y1 = px;
  p.rgb = rgb;
  p.text = t;
  p.face = face;
  p.track = track;
  p.shadow = shadow;
  p.shadowA = 0.5f;
  p.styled = true;
  K.v.push_back(p);
  return wd;
}
float label(Kit& K, const std::string& t, float x, float y, float px, uint32_t rgb, char anchor, float room) {
  return say(K, t, x, y, std::max(30.0f, px * 0.86f), rgb, anchor, room, false, 0.14f);  // (the 30 px floor)
}
void bcoin(Kit& K, float cx, float cy, float s) {
  sbox(K, cx - s / 2, cy - s / 2, cx + s / 2, cy + s / 2, kBGold);
  K.v.back().radius = s / 2;
  sring(K, cx - s / 2 + 4, cy - s / 2 + 4, cx + s / 2 - 4, cy + s / 2 - 4, 2.0f, 0xB07408, 1.0f, 0.0f, 5, 0.0f, s / 2 - 4);
  say(K, "B", cx, cy, s * 0.5f, 0x8A5A00, 'c', 0.0f, false);
  K.v.back().art = true;  // (the coin's mark)
}
void panel(Kit& K, float x0, float y0, float x1, float y1, uint32_t tint, float alpha, float cut) {
  sbox(K, x0, y0, x1, y1, kBGlass, alpha, cut);
  sbox(K, x0 + cut, y0, x1, y0 + 2, 0xFFFFFF, 0.18f);
  if (tint != 0) sglow(K, x0, y0 - 40, x0 + (x1 - x0) * 0.6f, y0 + (y1 - y0) * 0.6f, tint, 0.16f);
}
static float bglyph_width(const std::string& g, float size) {
  return g.size() > 1 ? std::max(size, xrtext::face_text_width(g, 0.55f * size, xrtext::kFaceBroadcastUp) + 0.7f * size) : size;
}
static float bglyph(Kit& K, const std::string& g, float x, float cy, float size, float a = 1.0f) {
  uint32_t col = 0x2A3550;
  if (g == "A") col = 0x2EA043;
  if (g == "B") col = 0xD63A2A;
  if (g == "X") col = 0x2F6FD0;
  if (g == "Y") col = 0xD8A920;
  const float wide = bglyph_width(g, size), r = 0.5f * size;
  const float rad = g.size() == 1 ? r : 10.0f;
  sbox(K, x, cy - r, x + wide, cy + r, col, a);
  K.v.back().radius = rad;
  sring(K, x, cy - r, x + wide, cy + r, 2.0f, 0xFFFFFF, a, 0.0f, 5, 0.0f, rad);
  say(K, g, x + 0.5f * wide, cy, 0.5f * size, 0xFFFFFF, 'c', 0.0f, false);
  K.v.back().alpha = a;
  K.v.back().art = true;  // (a button's letter: an icon)
  return wide;
}
void button(Kit& K, float x0, float y0, float x1, float y1, const std::string& lab, const std::string& glyphName, bool primary, bool on,
            bool can, float px) {
  const float lean = std::min(26.0f, 0.25f * (y1 - y0));
  const uint32_t acc = t_accent;
  if (on) sglow(K, x0 - 20, y0 - 20, x1 + 40, y1 + 20, primary ? acc : 0xFFFFFF, 0.4f);
  sbox(K, x0, y0, x1, y1, !can ? 0x232A3A : (primary ? acc : 0x18233A), primary || !can ? 1.0f : 0.92f, 0.0f, 5, lean);
  if (primary && can) sbox(K, x0, y1 - 8, x1, y1, kBGold, 1.0f, 0.0f, 5, lean * 8.0f / std::max(8.0f, y1 - y0));  // (the foot, same slope)
  if (on) sring(K, x0 - 6, y0 - 6, x1 + 6, y1 + 6, 5.0f, kBGold, 1.0f, 0.0f, 5, lean + 2.0f);
  if (px <= 0.0f) px = primary ? std::min(80.0f, (y1 - y0) * 0.6f) : std::min(50.0f, (y1 - y0) * 0.48f);
  const float cx = 0.5f * (x0 + x1 + lean), cy = 0.5f * (y0 + y1);
  const uint32_t ink = can ? 0xFFFFFF : kBDim;
  if (!glyphName.empty()) {
    const float gs = std::min(56.0f, (y1 - y0) * 0.5f);
    const float tw = std::min(xrtext::face_text_width(lab, px, xrtext::kFaceBroadcast), x1 - x0 - gs - 60.0f);
    const float sx = cx - 0.5f * (gs + 16.0f + tw);
    bglyph(K, glyphName, sx, cy, gs, can ? 1.0f : 0.5f);
    say(K, lab, sx + gs + 16.0f, cy, px, ink, 'l', x1 - x0 - gs - 60.0f);
  } else {
    say(K, lab, cx, cy, px, ink, 'c', x1 - x0 - 40.0f);
  }
}
float chip(Kit& K, const std::string& t, float x, float cy, uint32_t rgb, char anchor, float px, bool solid) {
  const float wd = xrtext::face_text_width(t, px, xrtext::kFaceBroadcast) + 50.0f;
  const float x0 = x - (anchor == 'c' ? 0.5f * wd : (anchor == 'r' ? wd : 0.0f));
  sbox(K, x0, cy - 0.7f * px, x0 + wd, cy + 0.7f * px, solid ? rgb : kBNight, solid ? 1.0f : 0.8f, 0.0f, 5, 12.0f);
  if (!solid) sring(K, x0, cy - 0.7f * px, x0 + wd, cy + 0.7f * px, 3.0f, rgb, 1.0f, 0.0f, 5, 12.0f);
  say(K, t, x0 + 0.5f * wd + 6.0f, cy, px, 0xFFFFFF, 'c');
  return wd;
}
void badge(Kit& K, const std::string& t, float x1, float y0, float px) {
  const float wd = xrtext::face_text_width(t, px, xrtext::kFaceBroadcast) + 36.0f, h = 1.4f * px;
  sbox(K, x1 - wd, y0, x1, y0 + h, kBGold, 1.0f, 0.0f, 5, 10.0f);
  say(K, t, x1 - 0.5f * wd + 5.0f, y0 + 0.5f * h, px, 0x0A0F1C, 'c');
}
float subtabs(Kit& K, const std::vector<std::string>& names, int at, float x0, float y0, const std::vector<std::string>& badges,
              uint32_t rgb, bool focused, float px, float h) {
  float x = x0;
  for (size_t i = 0; i < names.size(); ++i) {
    const bool on = static_cast<int>(i) == at;
    const float wd = xrtext::face_text_width(names[i], px, xrtext::kFaceBroadcast) + 70.0f;
    sbox(K, x, y0, x + wd, y0 + h, on ? rgb : kBRow, on ? 1.0f : 0.85f, 0.0f, 5, 14.0f);
    if (on && focused) sring(K, x - 6, y0 - 6, x + wd + 6, y0 + h + 6, 5.0f, kBGold, 1.0f, 0.0f, 5, 16.0f);
    say(K, names[i], x + 0.5f * wd + 7.0f, y0 + 0.5f * h, px, on ? 0xFFFFFF : kBSoft, 'c');
    if (i < badges.size() && !badges[i].empty()) {
      sbox(K, x + wd - 30, y0 - 14, x + wd + 12, y0 + 22, kBGold, 1.0f, 0.0f, 5, 8.0f);
      say(K, badges[i], x + wd - 5, y0 + 4, 30, 0x0A0F1C, 'c');
    }
    x += wd + 16.0f;
  }
  return x;
}
static void chevron(Kit& K, float x, float cy, float s, uint32_t rgb) {
  // a sharp ">" (two convex strokes)
  const float t = 0.22f * s;
  spoly(K, {x, cy - 0.5f * s, x + t, cy - 0.5f * s, x + 0.5f * s + t, cy, x + 0.5f * s, cy}, rgb);
  spoly(K, {x + 0.5f * s, cy, x + 0.5f * s + t, cy, x + t, cy + 0.5f * s, x, cy + 0.5f * s}, rgb);
}
float header_strip(Kit& K, float w, const std::string& where, uint32_t secRgb, bool logo, const std::vector<StripPart>& parts,
                   const std::string& coins, float* logoX0) {
  const float y0 = 132, y1 = 200, cy = 0.5f * (y0 + y1);
  sbox(K, 30, y0, w - 30, y1, kBNight, 0.7f);
  const float tw = std::min(470.0f, xrtext::face_text_width(where, 38, xrtext::kFaceBroadcast));
  const float px1 = std::max(330.0f, 56.0f + tw + 34.0f);
  sbox(K, 30, y0, px1, y1, secRgb);
  spoly(K, {px1 - 1, y0, px1 + 32, cy, px1 - 1, y1}, secRgb);
  say(K, where, 56, cy, 38, 0xFFFFFF, 'l', px1 - 70);
  float x = px1 + 70;
  if (logoX0 != nullptr) *logoX0 = logo ? x : 0.0f;
  if (logo) x += 80;
  const float coinX = w - 230;
  for (size_t k = 0; k < parts.size(); ++k) {
    const float room = coinX - 60 - x;
    if (room < 80) break;
    x += say(K, parts[k].text, x, cy, 36, k == 0 ? 0xFFFFFF : parts[k].rgb, 'l', room) + 22;
    chevron(K, x, cy, 20, t_accent);
    x += 40;
  }
  if (!coins.empty()) {
    bcoin(K, coinX, cy, 40);
    say(K, coins, coinX + 34, cy, 40, kBGold, 'l', w - 40 - coinX - 34);
  }
  return x;
}

void broadcast_pieces(std::vector<xrtext::BoardPiece>& v, size_t from) {
  const uint32_t acc = t_accent;
  for (size_t i = from; i < v.size(); ++i) {
    xrtext::BoardPiece& p = v[i];
    if (p.styled || p.art) continue;
    switch (p.kind) {
      case xrtext::BoardPiece::kText:
      case xrtext::BoardPiece::kTextLeft:
      case xrtext::BoardPiece::kTextRight: {
        if (p.face != xrtext::kFaceLucky) break;
        p.face = xrtext::kFaceBroadcast;
        if (p.outline >= 3.0f && p.y1 >= 56.0f && p.shadow <= 0.0f) {  // (a big word: the package's soft shadow for its outline)
          p.shadow = 3.0f;
          p.shadowA = 0.45f;
          p.shadowRgb = 0x000000;
        }
        p.outline = 0.0f;
        if (p.rgb == kGold) p.rgb = kBGold;
        if (p.rgb == kCream) p.rgb = 0xFFFFFF;
        if (p.rgb2 == kGold) p.rgb2 = kBGold;
        break;
      }
      case xrtext::BoardPiece::kBox:
      case xrtext::BoardPiece::kRing: {
        const float w = p.x1 - p.x0, h = p.y1 - p.y0;
        // the tokens
        if (p.rgb == kTile) p.rgb = kBRow;
        else if (p.rgb == kTileHi) p.rgb = 0x1C2A48;
        else if (p.rgb == kTileOn) p.rgb = mix(acc, kBBg0, 0.55f);
        else if (p.rgb == kRowOn) p.rgb = mix(acc, kBBg0, 0.45f);
        else if (p.rgb == kPill) p.rgb = 0x18233A;
        else if (p.rgb == kNavyTop) p.rgb = kBGlass;
        else if (p.rgb == kGold) p.rgb = kBGold;
        if (p.rgb2 == kNavyBot) p.rgb2 = kBNight;
        if (p.rgb2 == kDark && p.rgb != kDark) p.rgb2 = kBBg0;
        // the shape: small marks / round things stay; pills lean; panels lose their round corners for cut ones
        if (w < 24.0f || h < 24.0f || p.radius < 6.0f) break;
        const bool round = std::fabs(w - h) < 0.2f * std::max(w, h) && p.radius >= 0.4f * std::min(w, h);
        if (round) break;
        if (p.radius >= 0.45f * h && w >= 1.4f * h && h <= 160.0f) {
          const float lean = std::min(22.0f, 0.3f * h);
          p.lean = lean;
          p.x0 -= 0.5f * lean;
          p.x1 -= 0.5f * lean;
          p.radius = 0.0f;
        } else {
          p.cut = std::max(8.0f, std::min(28.0f, 0.9f * p.radius));
          p.cuts = 5;
          p.radius = 0.0f;
        }
        break;
      }
      default:
        break;
    }
  }
}

// ---- the pieces
void frame(Kit& K, float w, float h, uint32_t accent, float radius) {
  if (t_on) {  // r236: the package's background (dark, a diagonal sweep, the accent's light top left, a cool one bottom right)
    sbox(K, 0, 0, w, h, kBBg0, 1.0f, 0.0f, 5, 0.0f, kBBg1);
    sglow(K, -300, -400, 900, 500, t_accent, 0.22f);
    sglow(K, w - 660, h - 600, w + 440, h + 300, 0x2F8CFF, 0.12f);
    xrtext::BoardPiece p;
    p.kind = xrtext::BoardPiece::kStripes;
    p.x0 = 0;
    p.y0 = 0;
    p.x1 = w;
    p.y1 = h;
    p.radius = 120.0f;
    p.outline = 2.0f;
    p.slant = 700.0f / 1300.0f;
    p.rgb = 0xFFFFFF;
    p.alpha = 0.035f;
    p.styled = true;
    K.v.push_back(p);
    sbox(K, 0, 0, w, 6, t_accent);
    return;
  }
  K.box(6, 6, w - 6, h - 6, radius, accent);
  K.box(16, 16, w - 16, h - 16, radius - 6, kNavyTop, 0.98f, kNavyBot);
}

void focus(Kit& K, float x0, float y0, float x1, float y1, float r) {
  if (t_on) {  // r236: the accent's light + the gold cut-corner ring
    sglow(K, x0 - 30, y0 - 30, x1 + 30, y1 + 30, t_accent, 0.35f);
    sring(K, x0 - 7, y0 - 7, x1 + 7, y1 + 7, 5.0f, kBGold, 1.0f, std::min(26.0f, 0.4f * std::min(x1 - x0, y1 - y0) + 7.0f));
    return;
  }
  K.glow(x0 - 40, y0 - 40, x1 + 40, y1 + 40, kGold, 0.35f);
  K.ring(x0 - 9, y0 - 9, x1 + 9, y1 + 9, r + 9, 10, kGold);
}

static float glyph_width(const Kit& K, const std::string& g, float size) {
  if (t_on) return bglyph_width(g, size);  // r236
  return g.size() > 1 ? std::max(size, K.width(g, 0.6f * size) + 0.75f * size) : size;
}
float glyph(Kit& K, const std::string& g, float x, float cy, float size) {
  if (t_on) return bglyph(K, g, x, cy, size);  // r236
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

static void icon_shapes(Kit& K, const std::string& kind, float cx, float cy, float s, uint32_t rgb, float a);
void icon(Kit& K, const std::string& kind, float cx, float cy, float s, uint32_t rgb, float a) {
  const size_t from = K.v.size();
  icon_shapes(K, kind, cx, cy, s, rgb, a);
  if (t_on)  // r236: an icon keeps its own shapes in the broadcast look
    for (size_t i = from; i < K.v.size(); ++i) K.v[i].styled = true;
}
static void icon_shapes(Kit& K, const std::string& kind, float cx, float cy, float s, uint32_t rgb, float a) {
  const float h = 0.5f * s, w = std::max(3.0f, s * 0.09f);
  if (kind == "shirt") {  // r236: LOCKER - a jersey (body, two sleeves, the neck cut out)
    K.box(cx - 0.28f * s, cy - 0.32f * s, cx + 0.28f * s, cy + h, 4, rgb, a);
    K.poly({cx - 0.28f * s, cy - 0.36f * s, cx - 0.5f * s, cy - 0.16f * s, cx - 0.38f * s, cy + 0.02f * s, cx - 0.2f * s, cy - 0.12f * s}, rgb, a);
    K.poly({cx + 0.28f * s, cy - 0.36f * s, cx + 0.2f * s, cy - 0.12f * s, cx + 0.38f * s, cy + 0.02f * s, cx + 0.5f * s, cy - 0.16f * s}, rgb, a);
    K.box(cx - 0.3f * s, cy - 0.4f * s, cx + 0.3f * s, cy - 0.28f * s, 3, rgb, a);
    K.poly({cx - 0.12f * s, cy - 0.41f * s, cx + 0.12f * s, cy - 0.41f * s, cx, cy - 0.24f * s}, kDark, a * 0.9f);
    return;
  }
  if (kind == "ball") {  // r236: a ball (the disc, three dark patches)
    K.box(cx - h, cy - h, cx + h, cy + h, h, rgb, a);
    K.box(cx - 0.14f * s, cy - 0.14f * s, cx + 0.14f * s, cy + 0.14f * s, 0.14f * s, kDark, a * 0.85f);
    K.box(cx - 0.42f * s, cy - 0.3f * s, cx - 0.22f * s, cy - 0.1f * s, 0.1f * s, kDark, a * 0.85f);
    K.box(cx + 0.22f * s, cy + 0.08f * s, cx + 0.42f * s, cy + 0.28f * s, 0.1f * s, kDark, a * 0.85f);
    return;
  }
  if (kind == "search") {  // r236: a magnifier (ring + handle)
    K.ring(cx - 0.42f * s, cy - 0.42f * s, cx + 0.18f * s, cy + 0.18f * s, 0.3f * s, std::max(4.0f, 0.1f * s), rgb, a);
    K.poly({cx + 0.1f * s, cy + 0.18f * s, cx + 0.18f * s, cy + 0.1f * s, cx + 0.48f * s, cy + 0.4f * s, cx + 0.4f * s, cy + 0.48f * s}, rgb, a);
    return;
  }
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
  if (t_on) {  // r236: the header strip's words (the hub draws header_strip; this is any other caller's)
    for (size_t i = 0; i < parts.size(); ++i) {
      x += say(K, parts[i].text, x, cy, px * 0.86f, i + 1 == parts.size() ? 0xFFFFFF : kBSoft) + 18.0f;
      if (i + 1 < parts.size()) {
        chevron(K, x, cy, 20, t_accent);
        x += 38.0f;
      }
    }
    return x;
  }
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
  if (t_on) {  // r236: the package's bottom bar - the whole width, the accent's line on top, a gold stub, italic words
    const float W = x1 + x0;  // (the bar's own edges: the panel's, x0 / x1 the hub's 30-in margins)
    const float top = cy - 44.0f;
    sbox(K, 0, top, W, std::max(cy + 50.0f, top + 94.0f), 0x04060B, 0.95f);
    sbox(K, 0, top, W, top + 3, t_accent);
    sbox(K, 0, top, 230, top + 10, kBGold);
    const float bsz = 48.0f, bpx = 38.0f, gap = 70.0f;
    std::vector<float> widths;
    float total = 0.0f;
    for (const Hint& h : hints) {
      float gw = 0.0f;
      for (const std::string& g : split(h.glyph)) gw += bglyph_width(g, bsz) + 8.0f;
      widths.push_back(gw + 10.0f + xrtext::face_text_width(h.words, bpx, xrtext::kFaceBroadcast));
      total += widths.back();
    }
    total += gap * (hints.empty() ? 0.0f : static_cast<float>(hints.size() - 1));
    const float scale = total > W - 120.0f ? (W - 120.0f) / total : 1.0f;
    float x = 0.5f * (W - total * scale);
    for (size_t i = 0; i < hints.size(); ++i) {
      const Hint& h = hints[i];
      const float a = h.dim ? 0.35f : 1.0f;
      for (const std::string& g : split(h.glyph)) x += bglyph(K, g, x, cy + 4, bsz * scale, a) + 8.0f * scale;
      x += 10.0f * scale;
      x += say(K, h.words, x, cy + 4, bpx * scale, 0xE6ECF5) + gap * scale;
      K.v.back().alpha = a;
    }
    return;
  }
  K.box(x0, cy - 46, x1, cy + 46, 46, 0x060912, 0.75f);
  const float gap = 54.0f;
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
  if (t_on) {  // r236: the package's tab bar (optB_*.png): a dark bar with cut corners, the section on a parallelogram in its colour
               // with a gold underline and a glow, slashes between the others, gold counts
    const float W = x0 + x1, by0 = 26, by1 = 112, cy = 0.5f * (by0 + by1);
    sbox(K, 30, by0, W - 30, by1, kBNight, 0.92f, 22.0f);
    sbox(K, 30, by1 - 1, W - 30, by1 + 1, 0xFFFFFF, 0.08f);
    bglyph(K, "LT", 48, cy, 48);
    bglyph(K, "RT", W - 48 - 72, cy, 48);
    const float ax0 = 150, ax1 = W - 150, tw = (ax1 - ax0) / std::max<size_t>(1, names.size());
    for (size_t i = 0; i < names.size(); ++i) {
      const bool on = static_cast<int>(i) == at;
      const uint32_t c = i < colours.size() ? colours[i] : kBLeague;
      const float tx0 = ax0 + i * tw, tx1 = tx0 + tw;
      if (on) {
        sglow(K, tx0, by0 - 20, tx1, by1 + 30, c, 0.45f);
        sbox(K, tx0 + 4, by0 + 8, tx1 - 22, by1 - 8, c, 1.0f, 0.0f, 5, 18.0f);
        sbox(K, tx0 + 4, by1 - 14, tx1 - 22, by1 - 8, kBGold, 1.0f, 0.0f, 5, 2.0f);
      } else if (i + 1 < names.size() && static_cast<int>(i + 1) != at) {
        spoly(K, {tx1 - 2, by0 + 18, tx1 + 2, by0 + 18, tx1 - 10, by1 - 18, tx1 - 14, by1 - 18}, 0xFFFFFF, 0.12f);
      }
      const float tpx = 44.0f;
      const float wd = std::min(tw - 90.0f, xrtext::face_text_width(names[i], tpx, xrtext::kFaceBroadcast));
      const bool hasIcon = i < icons.size() && !icons[i].empty();
      const float sx = 0.5f * (tx0 + tx1) - 0.5f * (wd + (hasIcon ? 50.0f : 0.0f));
      if (hasIcon) icon(K, icons[i], sx + 18, cy, 34, on ? 0xFFFFFF : kBSoft);
      say(K, names[i], sx + (hasIcon ? 50.0f : 0.0f), cy, tpx, on ? 0xFFFFFF : kBSoft, 'l', tw - 90.0f);
      if (i < badges.size() && !badges[i].empty()) {
        const float bx = tx1 - 44;
        spoly(K, {bx - 18, by0 + 2, bx + 22, by0 + 2, bx + 14, by0 + 36, bx - 26, by0 + 36}, kBGold);
        say(K, badges[i], bx - 2, by0 + 19, 30, 0x0A0F1C, 'c');
      }
    }
    return;
  }
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
  if (t_on) {  // r236: the numbered rail ("01 PACKS"): cut-corner rows with the section's edge, the page showing filled with an arrow
    (void)row;
    (void)gap;
    (void)px;
    const float bx0 = 30, bx1 = x1, brow = 92, bgap = 14;
    float y = 232;
    for (size_t i = 0; i < items.size(); ++i) {
      const RailItem& it = items[i];
      const bool on = static_cast<int>(i) == at;
      if (on) {
        sglow(K, bx0, y - 10, bx1 + 40, y + brow + 10, colour, 0.35f);
        sbox(K, bx0, y, bx1, y + brow, colour, 1.0f, 20.0f, 2);
        spoly(K, {bx1 - 1, y + 20, bx1 + 30, y + 0.5f * brow, bx1 - 1, y + brow}, colour);
        if (focused) sring(K, bx0 - 6, y - 6, bx1 + 6, y + brow + 6, 4.0f, kBGold, 1.0f, 24.0f, 2);
      } else {
        sbox(K, bx0, y, bx1, y + brow, kBRow, 0.78f, 20.0f, 2);
        sbox(K, bx0, y, bx0 + 4, y + brow, colour, 0.8f);
      }
      char num[8];
      std::snprintf(num, sizeof(num), "%02d", static_cast<int>(i + 1));
      say(K, num, bx0 + 24, y + 0.5f * brow, 30, on ? 0xFFFFFF : colour);
      const bool hasBadge = !it.badge.empty();
      say(K, it.name, bx0 + 84, y + 0.5f * brow, 44, it.dim ? kBDim : (on ? 0xFFFFFF : kBInk), 'l', bx1 - bx0 - (hasBadge ? 170.0f : 110.0f));
      if (hasBadge) {
        spoly(K, {bx1 - 74, y + 0.5f * brow - 22, bx1 - 26, y + 0.5f * brow - 22, bx1 - 34, y + 0.5f * brow + 22, bx1 - 82, y + 0.5f * brow + 22}, kBGold);
        say(K, it.badge, bx1 - 54, y + 0.5f * brow, 30, 0x0A0F1C, 'c');
      }
      y += brow + bgap;
    }
    (void)x0;
    (void)y0;
    return y;
  }
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
  if (t_on) {  // r236: a glass tile with cut corners, washed in its colour (stronger focused), a light top line, the gold focus
    const uint32_t c = t.colour;
    sbox(K, x0, y0, x1, y1, mix(c, kBBg0, t.on ? 0.35f : 0.82f), 0.95f, 22.0f);
    sbox(K, x0 + 22, y0, x1, y0 + 2, 0xFFFFFF, t.on ? 0.25f : 0.1f);
    if (t.on) focus(K, x0, y0, x1, y1, r);
    const uint32_t ink = t.dim ? kBDim : 0xFFFFFF;
    const float cx = 0.5f * (x0 + x1);
    if (t.layout == 0) {
      if (!t.icon.empty()) {
        const float s = t.iconS > 0.0f ? t.iconS : std::min(x1 - x0, y1 - y0) * 0.34f;
        icon(K, t.icon, cx, y0 + 0.36f * (y1 - y0), s, ink);
        say(K, t.title, cx, y0 + 0.7f * (y1 - y0), t.titlePx, ink, 'c', x1 - x0 - 40);
        if (!t.sub.empty()) label(K, t.sub, cx, y0 + 0.86f * (y1 - y0), t.subPx, kBSoft, 'c', x1 - x0 - 40);
      } else {
        const float cy = 0.5f * (y0 + y1);
        say(K, t.title, cx, cy - (t.sub.empty() ? 0.0f : 0.28f * t.subPx), t.titlePx, ink, 'c', x1 - x0 - 40);
        if (!t.sub.empty()) label(K, t.sub, cx, cy + 0.5f * t.titlePx + 4, t.subPx, kBSoft, 'c', x1 - x0 - 40);
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
        say(K, t.title, tx, cy - 0.42f * t.titlePx + 4, t.titlePx, ink, 'l', x1 - tx - 30);
        label(K, t.sub, tx, cy + 0.5f * t.subPx + 8, t.subPx, kBSoft, 'l', x1 - tx - 30);
      } else {
        say(K, t.title, tx, cy, t.titlePx, ink, 'l', x1 - tx - 30);
      }
    }
    if (!t.badge.empty()) badge(K, t.badge, x1 - 18, y0 + 22);
    return;
  }
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
  if (t_on) {  // r236: a cut-corner row, its value on a dark parallelogram (gold chevrons while focused)
    if (on) focus(K, x0, y0, x1, y1, 0);
    sbox(K, x0, y0, x1, y1, on ? mix(t_accent, kBBg0, 0.45f) : kBRow, 0.9f, 18.0f);
    const float cy = 0.5f * (y0 + y1), tx = x0 + 34, vx1 = x1 - 26;
    float vx0 = vx1;
    if (dim) {
      const float pw = std::max(250.0f, xrtext::face_text_width(why, px - 2, xrtext::kFaceBroadcast) + 60.0f);
      vx0 = vx1 - pw;
      say(K, why, vx1, cy, px - 2, kBDim, 'r', pw);
    } else if (segs > 0) {
      const float sw = 40.0f;
      const float bx0 = vx1 - segs * (sw + 8) + 8;
      for (int k = 0; k < segs; ++k) sbox(K, bx0 + k * (sw + 8), cy - 18, bx0 + k * (sw + 8) + sw, cy + 16, k < lit ? kBGold : 0x2A3550, 1.0f, 0.0f, 5, 8.0f);
      vx0 = bx0;
    } else {
      const float pw = std::max(250.0f, xrtext::face_text_width(value, px - 4, xrtext::kFaceBroadcast) + 130.0f);
      vx0 = vx1 - pw;
      sbox(K, vx0, cy - 0.5f * (y1 - y0) + 12, vx1, cy + 0.5f * (y1 - y0) - 12, kBNight, 0.8f, 0.0f, 5, 14.0f);
      if (on) {
        chevron(K, vx1 - 40, cy, 22, kBGold);
        spoly(K, {vx0 + 40, cy - 11, vx0 + 30, cy - 11, vx0 + 19, cy, vx0 + 30, cy}, kBGold);
        spoly(K, {vx0 + 19, cy, vx0 + 30, cy, vx0 + 40, cy + 11, vx0 + 30, cy + 11}, kBGold);
      }
      say(K, value, 0.5f * (vx0 + vx1) + 6, cy, px - 4, on ? kBGold : kBSoft, 'c', pw - 100);
    }
    say(K, label, tx, cy, px, dim ? kBDim : 0xFFFFFF, 'l', vx0 - tx - 24);
    return;
  }
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
  if (t_on) {  // r236: the page dimmed, a glass panel with the accent's bar on top, the question big, A YES / B NO parallelograms
    sbox(K, 0, 0, w, h, 0x03050A, 0.74f);
    const float bw = std::min(w - 160.0f, std::max(1100.0f, xrtext::face_text_width(question, 72, xrtext::kFaceBroadcast) + 200.0f));
    const float bh = line.empty() ? 380.0f : 460.0f;
    const float x0 = 0.5f * (w - bw), y0 = 0.5f * (h - bh), x1 = x0 + bw, y1 = y0 + bh, cx = 0.5f * w;
    sglow(K, x0 - 60, y0 - 60, x1 + 60, y1 + 60, t_accent, 0.25f);
    sbox(K, x0, y0, x1, y1, kBGlass, 0.98f, 34.0f);
    sbox(K, x0 + 34, y0, x1, y0 + 10, t_accent);
    sbox(K, x0 + 34, y0 + 10, x0 + 290, y0 + 16, kBGold);
    say(K, question, cx, y0 + 110, 72, 0xFFFFFF, 'c', bw - 80, true, 0.0f, 3.0f);
    if (!line.empty()) say(K, line, cx, y0 + 196, 40, kBSoft, 'c', bw - 80);
    const float py = y1 - 110, pw = 400.0f;
    button(K, cx - pw - 30, py - 52, cx - 30, py + 52, yes, "A", true, false);
    button(K, cx + 30, py - 52, cx + 30 + pw, py + 52, no, "B", false, false);
    return;
  }
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
  if (t_on) {  // r236: a dark parallelogram, the words tracked, gold arrows where there's more
    const float wd = xrtext::face_text_width(t, 34, xrtext::kFaceBroadcastUp, 0.14f) + 200;
    sbox(K, cx - wd / 2, cy - 28, cx + wd / 2, cy + 28, kBNight, 0.8f, 0.0f, 5, 12.0f);
    label(K, t, cx, cy, 40, kBSoft, 'c', wd - 160);
    const uint32_t up = first > 0 ? kBGold : 0x3A4560, down = first + shown < total ? kBGold : 0x3A4560;
    spoly(K, {cx - wd / 2 + 30, cy + 10, cx - wd / 2 + 52, cy - 14, cx - wd / 2 + 74, cy + 10}, up);
    spoly(K, {cx + wd / 2 - 74, cy - 10, cx + wd / 2 - 52, cy + 14, cx + wd / 2 - 30, cy - 10}, down);
    return;
  }
  const float wd = K.width(t, 40) + 240;
  K.box(cx - wd / 2, cy - 30, cx + wd / 2, cy + 30, 30, kDark, 0.75f);
  K.text(t, cx, cy + 1, 40, wd - 200, kInkSoft, 2.0f);
  const uint32_t up = first > 0 ? kGold : 0x3A4560, down = first + shown < total ? kGold : 0x3A4560;
  K.poly({cx - wd / 2 + 30, cy + 10, cx - wd / 2 + 52, cy - 14, cx - wd / 2 + 74, cy + 10}, up);
  K.poly({cx + wd / 2 - 74, cy - 10, cx + wd / 2 - 52, cy + 14, cx + wd / 2 - 30, cy - 10}, down);
}

}  // namespace xrmenu
