// strikers-quest r42: see xr_text.h.

#include "xr_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "imstb_truetype.h"

#include "xr_font_luckiest.h"

namespace xrtext {
namespace {

struct Color {
  float r, g, b, a;  // straight alpha; sRGB-encoded values, composited as they are (UI art)
};

Color rgb(uint32_t v, float a = 1.0f) {
  return {((v >> 16) & 0xFF) / 255.0f, ((v >> 8) & 0xFF) / 255.0f, (v & 0xFF) / 255.0f, a};
}
Color mix(Color a, Color b, float t) {
  return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// Premultiplied RGBA, float.
struct Canvas {
  int w, h;
  std::vector<float> px;
  Canvas(int width, int height) : w(width), h(height), px(static_cast<size_t>(width) * height * 4, 0.0f) {}
  void over(int x, int y, const Color& c, float cover) {
    if (x < 0 || y < 0 || x >= w || y >= h || cover <= 0.0f) return;
    const float a = c.a * clamp01(cover);
    float* p = &px[(static_cast<size_t>(y) * w + x) * 4];
    p[0] = c.r * a + p[0] * (1.0f - a);
    p[1] = c.g * a + p[1] * (1.0f - a);
    p[2] = c.b * a + p[2] * (1.0f - a);
    p[3] = a + p[3] * (1.0f - a);
  }
  void write(uint8_t* out) const {
    for (size_t i = 0; i < px.size(); ++i) out[i] = static_cast<uint8_t>(std::lround(clamp01(px[i]) * 255.0f));
  }
};

// Set up once, thread-safely (the banner is drawn off the game thread). Only read after that.
stbtt_fontinfo& font() {
  static stbtt_fontinfo f = [] {
    stbtt_fontinfo info;
    stbtt_InitFont(&info, kLuckiestGuy, stbtt_GetFontOffsetForIndex(kLuckiestGuy, 0));
    return info;
  }();
  return f;
}

float text_width(const char* text, float px) {
  stbtt_fontinfo& f = font();
  const float scale = stbtt_ScaleForPixelHeight(&f, px);
  float x = 0.0f;
  for (const char* c = text; *c != '\0'; ++c) {
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f, static_cast<unsigned char>(*c), &advance, &lsb);
    x += advance * scale;
    if (c[1] != '\0') {
      x += scale * stbtt_GetCodepointKernAdvance(&f, static_cast<unsigned char>(c[0]), static_cast<unsigned char>(c[1]));
    }
  }
  return x;
}

// The glyphs' coverage, `pad` pixels of room all round.
struct Mask {
  int w = 0, h = 0;
  std::vector<float> a;
};

Mask text_mask(const char* text, float px, int pad) {
  stbtt_fontinfo& f = font();
  const float scale = stbtt_ScaleForPixelHeight(&f, px);
  int ascent = 0, descent = 0, gap = 0;
  stbtt_GetFontVMetrics(&f, &ascent, &descent, &gap);
  Mask m;
  m.w = static_cast<int>(std::ceil(text_width(text, px))) + 2 * pad;
  m.h = static_cast<int>(std::ceil((ascent - descent) * scale)) + 2 * pad;
  m.a.assign(static_cast<size_t>(m.w) * m.h, 0.0f);
  const int baseline = pad + static_cast<int>(std::lround(ascent * scale));
  float x = static_cast<float>(pad);
  std::vector<unsigned char> glyph;
  for (const char* c = text; *c != '\0'; ++c) {
    const int cp = static_cast<unsigned char>(*c);
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f, cp, &advance, &lsb);
    const float shift = x - std::floor(x);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointBitmapBoxSubpixel(&f, cp, scale, scale, shift, 0.0f, &x0, &y0, &x1, &y1);
    const int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0) {
      glyph.assign(static_cast<size_t>(gw) * gh, 0);
      stbtt_MakeCodepointBitmapSubpixel(&f, glyph.data(), gw, gh, gw, scale, scale, shift, 0.0f, cp);
      for (int yy = 0; yy < gh; ++yy) {
        for (int xx = 0; xx < gw; ++xx) {
          const int mx = static_cast<int>(std::floor(x)) + x0 + xx, my = baseline + y0 + yy;
          if (mx < 0 || my < 0 || mx >= m.w || my >= m.h) continue;
          float& d = m.a[static_cast<size_t>(my) * m.w + mx];
          d = std::max(d, glyph[static_cast<size_t>(yy) * gw + xx] / 255.0f);
        }
      }
    }
    x += advance * scale;
    if (c[1] != '\0') x += scale * stbtt_GetCodepointKernAdvance(&f, cp, static_cast<unsigned char>(c[1]));
  }
  return m;
}

// Pixels from each pixel to the shape (0 inside): a two-pass chamfer distance, close enough to round
// for an outline.
std::vector<float> distance_outside(const Mask& m) {
  const float kInf = 1e9f, kA = 1.0f, kB = 1.41421f;
  std::vector<float> d(m.a.size());
  for (size_t i = 0; i < d.size(); ++i) d[i] = m.a[i] >= 0.5f ? 0.0f : kInf;
  auto at = [&](int x, int y) -> float& { return d[static_cast<size_t>(y) * m.w + x]; };
  for (int y = 0; y < m.h; ++y) {
    for (int x = 0; x < m.w; ++x) {
      float v = at(x, y);
      if (x > 0) v = std::min(v, at(x - 1, y) + kA);
      if (y > 0) {
        v = std::min(v, at(x, y - 1) + kA);
        if (x > 0) v = std::min(v, at(x - 1, y - 1) + kB);
        if (x + 1 < m.w) v = std::min(v, at(x + 1, y - 1) + kB);
      }
      at(x, y) = v;
    }
  }
  for (int y = m.h - 1; y >= 0; --y) {
    for (int x = m.w - 1; x >= 0; --x) {
      float v = at(x, y);
      if (x + 1 < m.w) v = std::min(v, at(x + 1, y) + kA);
      if (y + 1 < m.h) {
        v = std::min(v, at(x, y + 1) + kA);
        if (x + 1 < m.w) v = std::min(v, at(x + 1, y + 1) + kB);
        if (x > 0) v = std::min(v, at(x - 1, y + 1) + kB);
      }
      at(x, y) = v;
    }
  }
  return d;
}

struct TextStyle {
  float px;
  Color fillTop, fillBottom;
  Color outline;
  float outlinePx;
  Color shadow;
  float shadowX, shadowY;
};

// `text` centred on (cx, cy): drop shadow, outline, then a top-to-bottom gradient fill.
void draw_text(Canvas& cv, const char* text, float cx, float cy, const TextStyle& s) {
  const int pad = static_cast<int>(std::ceil(s.outlinePx)) + 2;
  const Mask m = text_mask(text, s.px, pad);
  const std::vector<float> dist = distance_outside(m);
  int top = m.h, bottom = 0;
  for (int y = 0; y < m.h; ++y) {
    for (int x = 0; x < m.w; ++x) {
      if (m.a[static_cast<size_t>(y) * m.w + x] > 0.5f) {
        top = std::min(top, y);
        bottom = std::max(bottom, y);
        break;
      }
    }
  }
  const float span = std::max(1.0f, static_cast<float>(bottom - top));
  const int ox = static_cast<int>(std::lround(cx - m.w * 0.5f));
  const int oy = static_cast<int>(std::lround(cy - (top + bottom) * 0.5f));
  auto ring = [&](size_t i) { return std::max(m.a[i], clamp01(s.outlinePx + 0.5f - dist[i])); };
  const int sx = static_cast<int>(std::lround(s.shadowX)), sy = static_cast<int>(std::lround(s.shadowY));
  if (s.shadow.a > 0.0f) {
    for (int y = 0; y < m.h; ++y)
      for (int x = 0; x < m.w; ++x) cv.over(ox + x + sx, oy + y + sy, s.shadow, ring(static_cast<size_t>(y) * m.w + x));
  }
  for (int y = 0; y < m.h; ++y)
    for (int x = 0; x < m.w; ++x) cv.over(ox + x, oy + y, s.outline, ring(static_cast<size_t>(y) * m.w + x));
  for (int y = 0; y < m.h; ++y) {
    const Color c = mix(s.fillTop, s.fillBottom, clamp01((y - top) / span));
    for (int x = 0; x < m.w; ++x) cv.over(ox + x, oy + y, c, m.a[static_cast<size_t>(y) * m.w + x]);
  }
}

// Signed distance to a rounded rectangle (negative inside).
float rounded_rect_sd(float px, float py, float x0, float y0, float x1, float y1, float radius) {
  const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
  const float hx = 0.5f * (x1 - x0) - radius, hy = 0.5f * (y1 - y0) - radius;
  const float qx = std::fabs(px - cx) - hx, qy = std::fabs(py - cy) - hy;
  const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
  return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - radius;
}

// A rounded panel: a dark rim, a `border` ring, and a top-to-bottom gradient inside with a soft
// gloss over its upper half.
void panel(Canvas& cv, float x0, float y0, float x1, float y1, float radius, float border, Color rim, Color ring,
           Color top, Color bottom) {
  const float rimPx = 4.0f;
  for (int y = 0; y < cv.h; ++y) {
    const float t = clamp01((y - y0) / std::max(1.0f, y1 - y0));
    const Color fill = mix(top, bottom, t);
    const Color gloss{1.0f, 1.0f, 1.0f, 0.16f * clamp01(1.0f - 2.2f * t)};
    for (int x = 0; x < cv.w; ++x) {
      const float sd = rounded_rect_sd(x + 0.5f, y + 0.5f, x0, y0, x1, y1, radius);
      if (sd > 1.0f) continue;
      cv.over(x, y, rim, 0.5f - sd);
      cv.over(x, y, ring, 0.5f - (sd + rimPx));
      const float inner = 0.5f - (sd + rimPx + border);
      cv.over(x, y, fill, inner);
      cv.over(x, y, gloss, inner);
    }
  }
}

// A five-point star (4x4 samples a pixel), with a dark edge.
void star(Canvas& cv, float cx, float cy, float radius, Color fill, Color edge) {
  auto inside = [&](float px, float py, float r) {
    float vx[10], vy[10];
    for (int i = 0; i < 10; ++i) {
      const float ang = -1.5707963f + i * 0.6283185f;
      const float rr = (i % 2 == 0) ? r : r * 0.45f;
      vx[i] = cx + rr * std::cos(ang);
      vy[i] = cy + rr * std::sin(ang);
    }
    bool in = false;
    for (int i = 0, j = 9; i < 10; j = i++) {
      if (((vy[i] > py) != (vy[j] > py)) && (px < (vx[j] - vx[i]) * (py - vy[i]) / (vy[j] - vy[i]) + vx[i])) in = !in;
    }
    return in;
  };
  const float outer = radius + 6.0f;
  for (int y = static_cast<int>(cy - outer); y <= static_cast<int>(cy + outer); ++y) {
    for (int x = static_cast<int>(cx - outer); x <= static_cast<int>(cx + outer); ++x) {
      int edgeHits = 0, fillHits = 0;
      for (int sy = 0; sy < 4; ++sy) {
        for (int sx = 0; sx < 4; ++sx) {
          const float px = x + (sx + 0.5f) / 4.0f, py = y + (sy + 0.5f) / 4.0f;
          if (inside(px, py, outer)) ++edgeHits;
          if (inside(px, py, radius)) ++fillHits;
        }
      }
      cv.over(x, y, edge, edgeHits / 16.0f);
      cv.over(x, y, mix(fill, rgb(0xFFFFFF), 0.25f * clamp01((cy - y) / radius)), fillHits / 16.0f);
    }
  }
}

}  // namespace

void goal_banner(uint8_t* rgba, int w, int h, const char* name, uint32_t teamRgb, int goals, int assists,
                 bool hatTrick, const char* header, bool ownGoal) {
  Canvas cv(w, h);
  const Color team = rgb(teamRgb);
  // r43d: sized off u (r42's banner height, 420 at 1024 wide); the stats line takes the room below.
  const float u = 0.41f * w;
  panel(cv, 10.0f, 10.0f, w - 10.0f, h - 10.0f, 0.13f * u, 0.03f * u, rgb(0x241300), rgb(0xFFD23F),
        mix(team, rgb(0xFFFFFF), 0.18f), mix(team, rgb(0x000000), 0.45f));

  // r72: a nemesis's goal against you says so, in red. r85 (Lance didn't know a goal was an own goal):
  // an own goal says so, in orange, over the name it counts for.
  const bool nemesis = header != nullptr && !ownGoal;
  const char* top = ownGoal ? "OWN GOAL - OFF A DEFENDER" : (header != nullptr ? header : "GOAL SCORED BY:");
  float hpx = 0.19f * u;
  const float hwide = text_width(top, hpx), hroom = 0.88f * w;
  if (hwide > hroom) hpx *= hroom / hwide;
  TextStyle headerStyle{hpx, ownGoal ? rgb(0xFFD080) : (nemesis ? rgb(0xFF8A80) : rgb(0xFFFFFF)),
                        ownGoal ? rgb(0xFF9A20) : (nemesis ? rgb(0xE04A40) : rgb(0xE6E6E6)), rgb(0x161616), 0.018f * u,
                        rgb(0x000000, 0.45f), 0.010f * u, 0.012f * u};
  draw_text(cv, top, 0.5f * w, 0.29f * u, headerStyle);

  // The name as big as it fits between the stars.
  const float starR = 0.12f * u, room = w - 2.0f * (0.06f * w + 2.6f * starR);
  float px = 0.40f * u;
  const float wide = text_width(name, px);
  if (wide > room) px *= room / wide;
  TextStyle big{px, rgb(0xFFF7B0), rgb(0xFF9800), rgb(0x3A1800), 0.026f * u, rgb(0x000000, 0.55f), 0.016f * u,
                0.02f * u};
  draw_text(cv, name, 0.5f * w, 0.65f * u, big);
  star(cv, 0.06f * w + 1.3f * starR, 0.65f * u, starR, rgb(0xFFD23F), rgb(0x3A1800));
  star(cv, w - (0.06f * w + 1.3f * starR), 0.65f * u, starR, rgb(0xFFD23F), rgb(0x3A1800));

  // r43d (Lance: "show their game stats for that game"): goals and assists so far this game.
  if (goals >= 0) {
    char line[96];
    std::snprintf(line, sizeof(line), "%s%d GOAL%s,  %d ASSIST%s", hatTrick ? "HAT TRICK!   " : "THIS GAME:  ", goals,
                  goals == 1 ? "" : "S", assists, assists == 1 ? "" : "S");
    float spx = 0.15f * u;
    const float swide = text_width(line, spx), sroom = w - 0.12f * w;
    if (swide > sroom) spx *= sroom / swide;
    const TextStyle stats = hatTrick ? TextStyle{spx, rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x3A1800), 0.02f * u,
                                                 rgb(0x000000, 0.5f), 0.01f * u, 0.012f * u}
                                     : TextStyle{spx, rgb(0xFFFFFF), rgb(0xDADADA), rgb(0x161616), 0.016f * u,
                                                 rgb(0x000000, 0.45f), 0.008f * u, 0.01f * u};
    draw_text(cv, line, 0.5f * w, h - 0.24f * u, stats);
  }
  cv.write(rgba);
}

// r44g (r42's crowd volume panel, r44's voice line): the pause settings, one row picked at a time.
void settings_panel(uint8_t* rgba, int w, int h, int level, int steps, int voice, int heightCm, int row, bool shelf,
                    int popups) {
  Canvas cv(w, h);
  const float u = 0.3125f * w;  // r42's whole panel height: 200 at 640 wide
  panel(cv, 8.0f, 8.0f, w - 8.0f, h - 8.0f, 0.16f * u, 0.02f * u, rgb(0x000000, 0.9f), rgb(0xFFFFFF, 0.9f),
        rgb(0x1E2A44, 0.92f), rgb(0x0B1020, 0.92f));
  // The rows' bands, top to bottom: crowd volume (title and bar), voice chat, stadium height; r104: the
  // trophy shelf + badge board, the achievement pop-ups; the hint.
  const float band[5][2] = {{0.06f * u, 0.76f * u}, {0.80f * u, 1.18f * u}, {1.22f * u, 1.60f * u},
                            {1.64f * u, 2.02f * u}, {2.06f * u, 2.44f * u}};
  const float bx0 = 0.04f * w, bx1 = 0.96f * w;
  if (row >= 0 && row < 5) {  // the picked row: a lighter band with a gold edge
    const float y0 = band[row][0], y1 = band[row][1];
    for (int y = static_cast<int>(y0) - 3; y <= static_cast<int>(y1) + 3; ++y) {
      for (int x = static_cast<int>(bx0) - 3; x <= static_cast<int>(bx1) + 3; ++x) {
        const float sd = rounded_rect_sd(x + 0.5f, y + 0.5f, bx0, y0, bx1, y1, 0.08f * u);
        cv.over(x, y, rgb(0xFFD23F, 0.95f), 0.5f - sd);
        cv.over(x, y, rgb(0x34507A, 0.95f), 0.5f - (sd + 3.0f));
      }
    }
  }
  auto line_style = [&](float px, bool picked, uint32_t top, uint32_t bottom) {
    return TextStyle{px, rgb(picked ? 0xFFF7B0 : top), rgb(picked ? 0xFFC21F : bottom), rgb(0x05070C), 0.016f * u,
                     rgb(0x000000, 0.0f), 0.0f, 0.0f};
  };
  auto fitted = [&](const char* text, float px) {
    const float wide = text_width(text, px), room = 0.86f * w;
    return wide > room ? px * room / wide : px;
  };
  // Crowd volume.
  draw_text(cv, "CROWD VOLUME", 0.5f * w, 0.22f * u, line_style(0.22f * u, row == 0, 0xFFFFFF, 0xDDE4F0));
  const float x0 = 0.08f * w, x1 = 0.92f * w, gap = 0.012f * w;
  const float segW = ((x1 - x0) - gap * (steps - 1)) / steps;
  const float y0 = 0.42f * u, y1 = 0.64f * u;
  for (int i = 0; i < steps; ++i) {
    const float sx0 = x0 + i * (segW + gap), sx1 = sx0 + segW;
    const bool lit = i < level;
    for (int y = static_cast<int>(y0) - 2; y <= static_cast<int>(y1) + 2; ++y) {
      const float t = clamp01((y - y0) / (y1 - y0));
      const Color c = lit ? mix(rgb(0xFFF07A), rgb(0xFF9800), t) : mix(rgb(0x48526A), rgb(0x2A3142), t);
      for (int x = static_cast<int>(sx0) - 2; x <= static_cast<int>(sx1) + 2; ++x) {
        cv.over(x, y, c, 0.5f - rounded_rect_sd(x + 0.5f, y + 0.5f, sx0, y0, sx1, y1, 0.2f * segW));
      }
    }
  }
  // Voice chat: on (green), off (grey), or on without the microphone allowed (amber).
  const char* v = voice == 1 ? "VOICE CHAT: ON" : (voice == 2 ? "VOICE CHAT: ON, NO MIC ACCESS" : "VOICE CHAT: OFF");
  const uint32_t vTop = voice == 1 ? 0x9CFF8A : (voice == 2 ? 0xFFD27A : 0xB8C2D6);
  const uint32_t vBottom = voice == 1 ? 0x3CC23C : (voice == 2 ? 0xFF9800 : 0x9AA6BE);
  draw_text(cv, v, 0.5f * w, 0.99f * u, line_style(fitted(v, 0.18f * u), row == 1, vTop, vBottom));
  // Your eye height in the life-size stadium (r44g).
  char hl[64];
  if (heightCm == 0) {
    std::snprintf(hl, sizeof(hl), "STADIUM HEIGHT: NORMAL");
  } else {
    std::snprintf(hl, sizeof(hl), "STADIUM HEIGHT: YOU %d CM %s", heightCm > 0 ? heightCm : -heightCm,
                  heightCm > 0 ? "HIGHER" : "LOWER");
  }
  draw_text(cv, hl, 0.5f * w, 1.41f * u, line_style(fitted(hl, 0.18f * u), row == 2, 0xFFFFFF, 0xDDE4F0));
  // r104: on = green, off = grey (the voice chat line's colours); pop-ups "unlocks only" in amber.
  auto onoff = [&](const char* text, float y, int r, int state) {
    const uint32_t top = state == 2 ? 0x9CFF8A : (state == 1 ? 0xFFD27A : 0xB8C2D6);
    const uint32_t bottom = state == 2 ? 0x3CC23C : (state == 1 ? 0xFF9800 : 0x9AA6BE);
    draw_text(cv, text, 0.5f * w, y, line_style(fitted(text, 0.18f * u), row == r, top, bottom));
  };
  onoff(shelf ? "TROPHY SHELF + BADGES: ON" : "TROPHY SHELF + BADGES: OFF", 1.83f * u, 3, shelf ? 2 : 0);
  onoff(popups == 2 ? "ACHIEVEMENT POP-UPS: ALL" : (popups == 1 ? "ACHIEVEMENT POP-UPS: UNLOCKS ONLY" : "ACHIEVEMENT POP-UPS: OFF"),
        2.25f * u, 4, popups);
  const char* hint = "RIGHT STICK:  UP / DOWN PICKS,  LEFT / RIGHT CHANGES";
  TextStyle hs{fitted(hint, 0.12f * u), rgb(0xB8C2D6), rgb(0x9AA6BE), rgb(0x05070C), 0.012f * u, rgb(0x000000, 0.0f),
               0.0f, 0.0f};
  draw_text(cv, hint, 0.5f * w, 0.5f * (2.44f * u + h - 8.0f), hs);
  cv.write(rgba);
}

// r46: a RetroAchievements unlock. The font has capitals only, so everything is upper-cased; the
// description wraps onto a second line, at its word gap nearest the middle, when it would not fit.
void achievement_toast(uint8_t* rgba, int w, int h, const char* header, const char* title, const char* description,
                       int points) {
  Canvas cv(w, h);
  const float u = static_cast<float>(h);
  panel(cv, 8.0f, 8.0f, w - 8.0f, h - 8.0f, 0.14f * u, 0.025f * u, rgb(0x241300), rgb(0xFFD23F), rgb(0x22305A),
        rgb(0x0B1020));
  auto upper = [](const char* s) {
    std::string r = s != nullptr ? s : "";
    for (char& c : r) {
      if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
      else if (static_cast<unsigned char>(c) >= 0x80) c = ' ';  // outside the font
    }
    return r;
  };
  // The star on the left, the text centred in the room to its right.
  const float starR = 0.22f * u, starX = 0.04f * w + 1.3f * starR;
  star(cv, starX, 0.5f * u, starR, rgb(0xFFD23F), rgb(0x3A1800));
  const float left = starX + 1.6f * starR, right = w - 0.04f * w, room = right - left, cx = 0.5f * (left + right);
  auto fit = [&](const std::string& text, float px) {
    const float wide = text_width(text.c_str(), px);
    return wide > room ? px * room / wide : px;
  };

  std::string head = upper(header);
  if (points > 0) {
    char p[32];
    std::snprintf(p, sizeof(p), "   +%d POINT%s", points, points == 1 ? "" : "S");
    head += p;
  }
  draw_text(cv, head.c_str(), cx, 0.2f * u,
            TextStyle{fit(head, 0.13f * u), rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x05070C), 0.012f * u,
                      rgb(0x000000, 0.0f), 0.0f, 0.0f});
  const std::string t = upper(title);
  draw_text(cv, t.c_str(), cx, 0.43f * u,
            TextStyle{fit(t, 0.2f * u), rgb(0xFFFFFF), rgb(0xDDE4F0), rgb(0x05070C), 0.018f * u,
                      rgb(0x000000, 0.45f), 0.008f * u, 0.01f * u});

  const std::string d = upper(description);
  const float dpx = 0.12f * u;
  const TextStyle ds{dpx, rgb(0xC8D2E6), rgb(0xA8B4CC), rgb(0x05070C), 0.01f * u, rgb(0x000000, 0.0f), 0.0f, 0.0f};
  if (text_width(d.c_str(), dpx) <= room) {
    draw_text(cv, d.c_str(), cx, 0.7f * u, ds);
  } else {
    size_t cut = std::string::npos;
    for (size_t i = 0; i < d.size(); ++i) {
      if (d[i] == ' ' && (cut == std::string::npos ||
                          std::abs(static_cast<long>(i) - static_cast<long>(d.size() / 2)) <
                              std::abs(static_cast<long>(cut) - static_cast<long>(d.size() / 2)))) {
        cut = i;
      }
    }
    const std::string a = cut != std::string::npos ? d.substr(0, cut) : d;
    const std::string b = cut != std::string::npos ? d.substr(cut + 1) : std::string();
    const float px = std::min(fit(a, dpx), b.empty() ? dpx : fit(b, dpx));
    TextStyle s2 = ds;
    s2.px = px;
    draw_text(cv, a.c_str(), cx, 0.66f * u, s2);
    if (!b.empty()) draw_text(cv, b.c_str(), cx, 0.66f * u + 1.15f * px, s2);
  }
  cv.write(rgba);
}

namespace {

std::string caps(const char* s) {
  std::string r = s != nullptr ? s : "";
  for (char& c : r) {
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    else if (static_cast<unsigned char>(c) >= 0x80) c = ' ';  // outside the font
  }
  return r;
}

// `text` split at the word gap nearest its middle ("" second half when it has none).
void split_middle(const std::string& text, std::string* a, std::string* b) {
  size_t cut = std::string::npos;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == ' ' && (cut == std::string::npos ||
                           std::abs(static_cast<long>(i) - static_cast<long>(text.size() / 2)) <
                               std::abs(static_cast<long>(cut) - static_cast<long>(text.size() / 2)))) {
      cut = i;
    }
  }
  *a = cut != std::string::npos ? text.substr(0, cut) : text;
  *b = cut != std::string::npos ? text.substr(cut + 1) : std::string();
}

// `text` with its left edge at x (draw_text centres): centred at x + half its width.
void draw_left(Canvas& cv, const std::string& text, float x, float cy, const TextStyle& s) {
  draw_text(cv, text.c_str(), x + 0.5f * text_width(text.c_str(), s.px), cy, s);
}

}  // namespace

// r51b (Lance: the list "kills fps ... sound was skipping when i would scroll down": r51 redrew a
// whole page on the game thread at every scroll step): the list is drawn once, as one tall picture,
// on a thread of its own when it opens, and the headset scrolls a window over it.
void achievements_list(uint8_t* rgba, int w, int h, const AchievementLine* rows, int count, int rowH,
                       const char* message) {
  Canvas cv(w, h);
  panel(cv, 8.0f, 8.0f, w - 8.0f, h - 8.0f, 36.0f, 6.0f, rgb(0x000000, 0.92f), rgb(0xFFD23F),
        rgb(0x1E2A44, 0.95f), rgb(0x0B1020, 0.95f));
  const float margin = 0.05f * w;
  auto fitted = [&](const std::string& text, float px, float room) {
    const float wide = text_width(text.c_str(), px);
    return wide > room ? px * room / wide : px;
  };
  auto plain = [&](float px, uint32_t top, uint32_t bottom, float outline) {
    return TextStyle{px, rgb(top), rgb(bottom), rgb(0x05070C), outline, rgb(0x000000, 0.0f), 0.0f, 0.0f};
  };
  if (count <= 0) {
    std::string a, b;
    const std::string m = caps(message);
    const float px = 34.0f, room = w - 2.0f * margin, cy = 0.5f * h;
    if (text_width(m.c_str(), px) <= room) {
      draw_text(cv, m.c_str(), 0.5f * w, cy, plain(px, 0xFFFFFF, 0xDDE4F0, 2.5f));
    } else {
      split_middle(m, &a, &b);
      const float p2 = std::min(fitted(a, px, room), fitted(b, px, room));
      draw_text(cv, a.c_str(), 0.5f * w, cy - 0.65f * p2, plain(p2, 0xFFFFFF, 0xDDE4F0, 2.5f));
      draw_text(cv, b.c_str(), 0.5f * w, cy + 0.65f * p2, plain(p2, 0xFFFFFF, 0xDDE4F0, 2.5f));
    }
    cv.write(rgba);
    return;
  }
  const float top = 0.5f * (h - count * rowH);  // the rows centred, the panel's edge round them
  const float starR = 0.22f * rowH;
  const float textX = margin + 2.6f * starR;
  const float pointsRoom = 0.16f * w;
  for (int i = 0; i < count; ++i) {
    const AchievementLine& r = rows[i];
    const float y0 = top + i * rowH;
    // A faint band behind every other row, so a row's lines read as one.
    if (i % 2 == 0) {
      for (int y = static_cast<int>(y0); y < static_cast<int>(y0 + rowH); ++y) {
        for (int x = static_cast<int>(margin * 0.6f); x < static_cast<int>(w - margin * 0.6f); ++x) {
          cv.over(x, y, rgb(0xFFFFFF, 0.05f), 1.0f);
        }
      }
    }
    const float cy = y0 + 0.5f * rowH;
    if (r.unlocked) {
      star(cv, margin + starR, cy, starR, rgb(0xFFD23F), rgb(0x3A1800));
    } else {
      star(cv, margin + starR, cy, starR, rgb(0x4A5468), rgb(0x10141E));
    }
    const std::string t = caps(r.title);
    const float titleRoom = w - textX - pointsRoom - margin;
    const TextStyle ts = r.unlocked ? plain(fitted(t, 30.0f, titleRoom), 0xFFF7B0, 0xFFC21F, 2.5f)
                                    : plain(fitted(t, 30.0f, titleRoom), 0xE8ECF4, 0xB8C2D6, 2.5f);
    draw_left(cv, t, textX, y0 + 0.27f * rowH, ts);
    // Points (and a locked one's progress) at the right.
    char pts[32];
    std::snprintf(pts, sizeof(pts), "%d PT%s", r.points, r.points == 1 ? "" : "S");
    const std::string p = pts;
    draw_text(cv, p.c_str(), w - margin - 0.5f * pointsRoom, y0 + 0.27f * rowH,
              plain(fitted(p, 24.0f, pointsRoom), r.unlocked ? 0xFFF7B0 : 0xB8C2D6, r.unlocked ? 0xFFC21F : 0x9AA6BE,
                    2.0f));
    if (!r.unlocked && r.progress != nullptr && r.progress[0] != '\0') {
      const std::string g = caps(r.progress);
      draw_text(cv, g.c_str(), w - margin - 0.5f * pointsRoom, y0 + 0.66f * rowH,
                plain(fitted(g, 22.0f, pointsRoom), 0x9CFF8A, 0x3CC23C, 2.0f));
    }
    // The description, on one line or two.
    const std::string d = caps(r.description);
    const float descRoom = w - textX - pointsRoom - margin;
    const float dpx = 19.0f;
    const uint32_t dTop = r.unlocked ? 0xD8DEEA : 0x9AA6BE, dBottom = r.unlocked ? 0xC0C8D8 : 0x8490A8;
    if (text_width(d.c_str(), dpx) <= descRoom) {
      draw_left(cv, d, textX, y0 + 0.66f * rowH, plain(dpx, dTop, dBottom, 1.5f));
    } else {
      std::string a, b;
      split_middle(d, &a, &b);
      const float p2 = std::min(fitted(a, dpx, descRoom), b.empty() ? dpx : fitted(b, dpx, descRoom));
      draw_left(cv, a, textX, y0 + 0.58f * rowH, plain(p2, dTop, dBottom, 1.5f));
      if (!b.empty()) draw_left(cv, b, textX, y0 + 0.58f * rowH + 1.05f * p2, plain(p2, dTop, dBottom, 1.5f));
    }
  }
  cv.write(rgba);
}

// r57: the badge board over the trophy shelf (Lance: "alongside your RetroAchievements badges"): a
// framed board, its title, and every badge of the set in rows of `columns`, the ones unlocked in
// colour, the rest grey and dim. A badge without a picture shows as a plain star.
void badge_board(uint8_t* rgba, int w, int h, const BadgeTile* tiles, int count, int columns, const char* title) {
  Canvas cv(w, h);
  panel(cv, 6.0f, 6.0f, w - 6.0f, h - 6.0f, 28.0f, 6.0f, rgb(0x000000, 0.9f), rgb(0xFFD23F), rgb(0x22305A, 0.95f),
        rgb(0x0B1020, 0.95f));
  const float header = 0.13f * h;
  const std::string t = caps(title);
  const float room = 0.9f * w;
  const float tpx = std::min(0.62f * header, text_width(t.c_str(), 0.62f * header) > room
                                                 ? 0.62f * header * room / text_width(t.c_str(), 0.62f * header)
                                                 : 0.62f * header);
  draw_text(cv, t.c_str(), 0.5f * w, 0.55f * header + 6.0f,
            TextStyle{tpx, rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x05070C), 3.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  if (count <= 0 || columns <= 0) {
    cv.write(rgba);
    return;
  }
  const int rows = (count + columns - 1) / columns;
  const float margin = 0.04f * w;
  const float cell = std::min((w - 2.0f * margin) / columns, (h - header - 2.0f * margin) / rows);
  const float x0 = 0.5f * (w - cell * columns), y0 = header + margin;
  const float tile = 0.86f * cell;
  for (int i = 0; i < count; ++i) {
    const BadgeTile& b = tiles[i];
    const float cx = x0 + (i % columns + 0.5f) * cell, cy = y0 + (i / columns + 0.5f) * cell;
    if (b.rgba64 == nullptr) {
      star(cv, cx, cy, 0.4f * tile, b.unlocked ? rgb(0xFFD23F) : rgb(0x4A5468), rgb(0x10141E));
      continue;
    }
    // The 64 x 64 picture scaled into the tile (nearest at this size reads fine), with a thin frame.
    const int tx0 = static_cast<int>(cx - 0.5f * tile), ty0 = static_cast<int>(cy - 0.5f * tile);
    const int ts = static_cast<int>(tile);
    for (int y = -2; y < ts + 2; ++y) {
      for (int x = -2; x < ts + 2; ++x) {
        const bool frame = x < 0 || y < 0 || x >= ts || y >= ts;
        if (frame) {
          cv.over(tx0 + x, ty0 + y, b.unlocked ? rgb(0xFFD23F) : rgb(0x3A4252), 1.0f);
          continue;
        }
        const int sx = std::min(63, x * 64 / ts), sy = std::min(63, y * 64 / ts);
        const uint8_t* s = b.rgba64 + (static_cast<size_t>(sy) * 64 + sx) * 4;
        float r = s[0] / 255.0f, gch = s[1] / 255.0f, bch = s[2] / 255.0f;
        if (!b.unlocked) {
          const float l = 0.3f * r + 0.59f * gch + 0.11f * bch;
          r = gch = bch = 0.15f + 0.35f * l;
        }
        cv.over(tx0 + x, ty0 + y, Color{r, gch, bch, 1.0f}, s[3] / 255.0f);
      }
    }
  }
  cv.write(rgba);
}

namespace {

// r61: `text` with its left edge at x, centred on cy, shrunk to fit `room` pixels.
void text_left(Canvas& cv, const std::string& text, float x, float cy, float px, float room, Color top, Color bottom,
               float outline) {
  const float wide = text_width(text.c_str(), px);
  if (wide > room && wide > 0.0f) px *= room / wide;
  const float w = text_width(text.c_str(), px);
  draw_text(cv, text.c_str(), x + 0.5f * w, cy,
            TextStyle{px, top, bottom, rgb(0x05070C), outline, rgb(0x000000, 0.0f), 0.0f, 0.0f});
}

// A filled rounded rectangle.
void fill_rounded(Canvas& cv, float x0, float y0, float x1, float y1, float r, Color c) {
  for (int y = static_cast<int>(y0) - 1; y <= static_cast<int>(y1) + 1; ++y) {
    for (int x = static_cast<int>(x0) - 1; x <= static_cast<int>(x1) + 1; ++x) {
      const float sd = rounded_rect_sd(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r);
      cv.over(x, y, c, 0.5f - sd);
    }
  }
}

}  // namespace

void home_board(uint8_t* rgba, int w, int h, const char* title, uint32_t teamRgb, uint32_t teamRgb2,
                const HomeLine* lines, int count) {
  Canvas cv(w, h);
  panel(cv, 6.0f, 6.0f, w - 6.0f, h - 6.0f, 28.0f, 6.0f, rgb(0x000000, 0.9f), rgb(0xFFD23F), rgb(0x1B2440, 0.95f),
        rgb(0x0B1020, 0.95f));
  // The title band in the team's colours.
  const float band = 0.16f * h;
  const Color c1 = rgb(teamRgb), c2 = rgb(teamRgb2);
  for (int y = 16; y < static_cast<int>(16 + band); ++y) {
    const float t = (y - 16.0f) / band;
    for (int x = 16; x < w - 16; ++x) {
      const float sd = rounded_rect_sd(x + 0.5f, y + 0.5f, 16.0f, 16.0f, w - 16.0f, 16.0f + band, 18.0f);
      cv.over(x, y, mix(c1, mix(c1, rgb(0x000000), 0.35f), t), 0.5f - sd);
    }
  }
  // A stripe of the second colour under it.
  fill_rounded(cv, 16.0f, 16.0f + band - 6.0f, w - 16.0f, 16.0f + band, 3.0f, c2);
  const std::string t = caps(title);
  text_left(cv, t, 0.5f * w - 0.5f * std::min(text_width(t.c_str(), 0.55f * band), 0.9f * w), 16.0f + 0.5f * band,
            0.55f * band, 0.9f * w, rgb(0xFFFFFF), rgb(0xE6ECF5), 3.0f);
  if (count <= 0) {
    cv.write(rgba);
    return;
  }
  const float top = 16.0f + band + 0.03f * h;
  const float row = (h - 22.0f - top) / static_cast<float>(count);
  const float left = 0.06f * w, room = 0.88f * w;
  for (int i = 0; i < count; ++i) {
    const HomeLine& l = lines[i];
    const float y = top + i * row;
    if (i > 0) fill_rounded(cv, left, y - 1.5f, left + room, y + 0.5f, 1.0f, rgb(0x3A4664, 0.8f));
    text_left(cv, caps(l.label), left, y + 0.17f * row, 0.17f * row, room, rgb(0x9AA6BE), rgb(0x7E8AA2), 1.5f);
    float valueRoom = room;
    if (l.tag != nullptr && l.tag[0] != '\0') {
      // The tag: a pill at the right.
      const std::string tag = caps(l.tag);
      const float px = 0.22f * row;
      const float tw = text_width(tag.c_str(), px) + 0.5f * px;
      const float x1 = left + room, x0 = x1 - tw, cy = y + 0.45f * row;
      fill_rounded(cv, x0, cy - 0.62f * px, x1, cy + 0.62f * px, 0.6f * px, rgb(l.tagRgb));
      draw_text(cv, tag.c_str(), 0.5f * (x0 + x1), cy,
                TextStyle{px, rgb(0xFFFFFF), rgb(0xF0F0F0), rgb(0x05070C), 2.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
      valueRoom = room - tw - 0.03f * w;
    }
    text_left(cv, caps(l.value), left, y + 0.45f * row, 0.3f * row, valueRoom, rgb(0xFFF7B0), rgb(0xFFC21F), 2.5f);
    if (l.note != nullptr && l.note[0] != '\0') {
      const Color nc = rgb(l.noteRgb);
      text_left(cv, caps(l.note), left, y + 0.76f * row, 0.17f * row, room, nc, mix(nc, rgb(0x000000), 0.15f), 1.5f);
    }
  }
  cv.write(rgba);
}

void match_ticket(uint8_t* rgba, int w, int h, const char* header, const char* score, const char* date,
                  uint32_t teamRgb, uint32_t teamRgb2) {
  Canvas cv(w, h);
  // A gold frame round a paper ticket.
  panel(cv, 4.0f, 4.0f, w - 4.0f, h - 4.0f, 14.0f, 8.0f, rgb(0x000000, 0.85f), rgb(0xFFD23F), rgb(0xF6EFD9),
        rgb(0xE9DDBA));
  // The stub at the left in the team colour, cut off by a dotted line.
  const float stub = 0.24f * w;
  fill_rounded(cv, 22.0f, 22.0f, stub, h - 22.0f, 8.0f, rgb(teamRgb));
  fill_rounded(cv, stub - 14.0f, 22.0f, stub, h - 22.0f, 2.0f, rgb(teamRgb2));
  for (float y = 26.0f; y < h - 26.0f; y += 14.0f) {
    fill_rounded(cv, stub + 8.0f, y, stub + 12.0f, y + 6.0f, 2.0f, rgb(0x6A5A3A));
  }
  star(cv, 0.5f * (22.0f + stub - 14.0f), 0.5f * h, 0.12f * h, rgb(0xFFD23F), rgb(0x10141E));
  const float cx = 0.5f * (stub + 16.0f + w - 22.0f), room = w - 22.0f - stub - 30.0f;
  auto centred = [&](const std::string& text, float cy, float px, Color a, Color b, float outline) {
    const float wide = text_width(text.c_str(), px);
    if (wide > room) px *= room / wide;
    draw_text(cv, text.c_str(), cx, cy, TextStyle{px, a, b, rgb(0x2A1E0E), outline, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  };
  centred(caps(header), 0.24f * h, 0.15f * h, rgb(teamRgb), mix(rgb(teamRgb), rgb(0x000000), 0.3f), 0.0f);
  centred(caps(score), 0.53f * h, 0.24f * h, rgb(0x2A1E0E), rgb(0x4A3A1E), 0.0f);
  centred(caps(date), 0.8f * h, 0.11f * h, rgb(0x6A5A3A), rgb(0x6A5A3A), 0.0f);
  cv.write(rgba);
}

void cup_board(uint8_t* rgba, int w, int h, const char* title, const CupLine* lines, int count, const char* footer) {
  Canvas cv(w, h);
  panel(cv, 6.0f, 6.0f, w - 6.0f, h - 6.0f, 30.0f, 6.0f, rgb(0x000000, 0.9f), rgb(0xFFD23F), rgb(0x22305A, 0.95f),
        rgb(0x0B1020, 0.95f));
  const float head = 0.15f * h;
  const std::string t = caps(title);
  {
    float px = 0.5f * head;
    const float wide = text_width(t.c_str(), px);
    if (wide > 0.9f * w) px *= 0.9f * w / wide;
    draw_text(cv, t.c_str(), 0.5f * w, 0.55f * head + 6.0f,
              TextStyle{px, rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x05070C), 3.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  }
  const float foot = footer != nullptr && footer[0] != '\0' ? 0.14f * h : 0.0f;
  const float top = head + 0.04f * h;
  const float rows = static_cast<float>(std::max(count, 1) + 1);
  const float row = (h - top - foot - 18.0f) / rows;
  const float x0 = 0.06f * w, x1 = 0.94f * w;
  const float colName = x0 + 0.1f * w, colW = 0.62f * w, colL = 0.72f * w, colP = 0.85f * w;
  auto cell = [&](const std::string& s, float cx, float cy, float px, Color a, Color b) {
    draw_text(cv, s.c_str(), cx, cy, TextStyle{px, a, b, rgb(0x05070C), 2.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  };
  // Column heads.
  const Color muted = rgb(0x9AA6BE);
  const float hy = top + 0.5f * row;
  cell("W", colW, hy, 0.45f * row, muted, muted);
  cell("L", colL, hy, 0.45f * row, muted, muted);
  cell("PTS", colP, hy, 0.45f * row, muted, muted);
  for (int i = 0; i < count; ++i) {
    const CupLine& l = lines[i];
    const float y0 = top + (i + 1) * row, cy = y0 + 0.5f * row;
    if (l.mine) fill_rounded(cv, x0, y0 + 3.0f, x1, y0 + row - 3.0f, 10.0f, rgb(0xFFD23F, 0.22f));
    const Color a = l.mine ? rgb(0xFFF7B0) : rgb(0xF0F3FA), b = l.mine ? rgb(0xFFC21F) : rgb(0xC8D0E0);
    cell(std::to_string(i + 1), x0 + 0.04f * w, cy, 0.5f * row, a, b);
    text_left(cv, caps(l.name), colName, cy, 0.52f * row, colW - colName - 0.05f * w, a, b, 2.0f);
    cell(std::to_string(l.wins), colW, cy, 0.5f * row, a, b);
    cell(std::to_string(l.losses), colL, cy, 0.5f * row, a, b);
    cell(std::to_string(l.points), colP, cy, 0.5f * row, a, b);
  }
  if (foot > 0.0f) {
    const std::string f = caps(footer);
    float px = 0.45f * foot;
    const float wide = text_width(f.c_str(), px);
    if (wide > 0.9f * w) px *= 0.9f * w / wide;
    draw_text(cv, f.c_str(), 0.5f * w, h - 12.0f - 0.5f * foot,
              TextStyle{px, rgb(0xFFFFFF), rgb(0xE0E6F0), rgb(0x05070C), 3.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  }
  cv.write(rgba);
}

void achievements_header(uint8_t* rgba, int w, int h, const char* title, const char* hint) {
  Canvas cv(w, h);
  panel(cv, 8.0f, 8.0f, w - 8.0f, h - 8.0f, 30.0f, 6.0f, rgb(0x000000, 0.92f), rgb(0xFFD23F), rgb(0x22305A, 0.95f),
        rgb(0x0B1020, 0.95f));
  const float room = 0.9f * w;
  auto fit = [&](const std::string& text, float px) {
    const float wide = text_width(text.c_str(), px);
    return wide > room ? px * room / wide : px;
  };
  const std::string t = caps(title), k = caps(hint);
  draw_text(cv, t.c_str(), 0.5f * w, 0.4f * h,
            TextStyle{fit(t, 0.3f * h), rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x05070C), 3.0f, rgb(0x000000, 0.0f), 0.0f,
                      0.0f});
  draw_text(cv, k.c_str(), 0.5f * w, 0.76f * h,
            TextStyle{fit(k, 0.16f * h), rgb(0xB8C2D6), rgb(0x9AA6BE), rgb(0x05070C), 2.0f, rgb(0x000000, 0.0f), 0.0f,
                      0.0f});
  cv.write(rgba);
}

// r67: see xr_text.h.
void guest_picker(uint8_t* rgba, int w, int h, const char* title, const GuestTile* tiles, int count, int columns,
                  int cursor, const char* hint) {
  Canvas cv(w, h);
  panel(cv, 6.0f, 6.0f, w - 6.0f, h - 6.0f, 30.0f, 6.0f, rgb(0x000000, 0.92f), rgb(0xFFD23F), rgb(0x22305A, 0.96f),
        rgb(0x0B1020, 0.96f));
  const float header = 0.11f * h, footer = 0.08f * h;
  auto fit = [&](const std::string& text, float px, float room) {
    const float wide = text_width(text.c_str(), px);
    return wide > room && wide > 0.0f ? px * room / wide : px;
  };
  const std::string t = caps(title), k = caps(hint);
  draw_text(cv, t.c_str(), 0.5f * w, 0.55f * header + 6.0f,
            TextStyle{fit(t, 0.55f * header, 0.9f * w), rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x05070C), 3.0f,
                      rgb(0x000000, 0.0f), 0.0f, 0.0f});
  draw_text(cv, k.c_str(), 0.5f * w, h - 0.55f * footer - 6.0f,
            TextStyle{fit(k, 0.5f * footer, 0.9f * w), rgb(0xD8DEEA), rgb(0xB8C2D6), rgb(0x05070C), 2.0f,
                      rgb(0x000000, 0.0f), 0.0f, 0.0f});
  if (count > 0 && columns > 0) {
    // r68e: tiles may say where they go (the character select menu's sections start new rows)
    int rows = (count + columns - 1) / columns;
    for (int i = 0; i < count; ++i)
      if (tiles[i].row >= 0) rows = std::max(rows, tiles[i].row + 1);
    const float margin = 0.02f * w;
    const float cellW = (w - 2.0f * margin) / columns;
    const float cellH = (h - header - footer - 2.0f * margin) / std::max(rows, 2);
    const float x0 = margin, y0 = header + margin;
    for (int i = 0; i < count; ++i) {
      const GuestTile& g = tiles[i];
      const int col = g.row >= 0 ? g.col : i % columns, row = g.row >= 0 ? g.row : i / columns;
      const float cx0 = x0 + col * cellW, cy0 = y0 + row * cellH;
      const bool on = i == cursor;
      // The cell: a soft card, gold-framed when the cursor is on it (r68e: else in its section's colour).
      fill_rounded(cv, cx0 + 6.0f, cy0 + 6.0f, cx0 + cellW - 6.0f, cy0 + cellH - 6.0f, 14.0f,
                   on ? rgb(0xFFD23F) : (g.frame != 0 ? rgb(g.frame, 0.95f) : rgb(0x31406A, 0.9f)));
      fill_rounded(cv, cx0 + (on ? 12.0f : 9.0f), cy0 + (on ? 12.0f : 9.0f), cx0 + cellW - (on ? 12.0f : 9.0f),
                   cy0 + cellH - (on ? 12.0f : 9.0f), 10.0f, on ? rgb(0x3B5AA8) : rgb(0x1A2442));
      const float nameH = 0.24f * cellH;
      const float picX0 = cx0 + 16.0f, picY0 = cy0 + 14.0f;
      const float picW = cellW - 32.0f, picH = cellH - nameH - 22.0f;
      if (g.rgba != nullptr && g.w > 0 && g.h > 0) {
        // The game's own portrait, whole, fitted into the picture area (nearest; bottom-centred).
        const float s = std::min(picW / g.w, picH / g.h);
        const int dw = std::max(1, static_cast<int>(g.w * s)), dh = std::max(1, static_cast<int>(g.h * s));
        const int ox = static_cast<int>(picX0 + 0.5f * (picW - dw)), oy = static_cast<int>(picY0 + (picH - dh));
        for (int y = 0; y < dh; ++y) {
          const int sy = std::min(g.h - 1, static_cast<int>(y / s));
          for (int x = 0; x < dw; ++x) {
            const int sx = std::min(g.w - 1, static_cast<int>(x / s));
            const uint8_t* p = g.rgba + (static_cast<size_t>(sy) * g.w + sx) * 4;
            // r70c: a locked evolved Pokemon: its silhouette only, dark with a faint blue cast
            const Color pc = g.locked ? Color{0.05f, 0.07f, 0.13f, 1.0f}
                                      : Color{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f};
            cv.over(ox + x, oy + y, pc, p[3] / 255.0f);
          }
        }
      } else {
        // No portrait: the initials, large.
        std::string initials;
        const std::string n = g.locked ? std::string("?") : caps(g.name);
        for (size_t c = 0; c < n.size() && initials.size() < 2; ++c) {
          if (n[c] != ' ' && (c == 0 || n[c - 1] == ' ')) initials += n[c];
        }
        draw_text(cv, initials.c_str(), picX0 + 0.5f * picW, picY0 + 0.5f * picH,
                  TextStyle{0.5f * picH, rgb(0xFFFFFF), rgb(0xC8D0E0), rgb(0x05070C), 3.0f, rgb(0x000000, 0.0f), 0.0f,
                            0.0f});
      }
      const std::string n = g.locked ? std::string("???") : caps(g.name);
      const float npx = fit(n, 0.55f * nameH, cellW - 28.0f);
      draw_text(cv, n.c_str(), cx0 + 0.5f * cellW, cy0 + cellH - 0.62f * nameH,
                TextStyle{npx, on ? rgb(0xFFF7B0) : rgb(0xFFFFFF), on ? rgb(0xFFC21F) : rgb(0xD8DEEA), rgb(0x05070C),
                          2.5f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
      if (g.locked || (g.game != nullptr && g.game[0] != '\0')) {
        const std::string gm = g.locked ? std::string("EVOLVE IN FRANCHISE") : caps(g.game);
        draw_text(cv, gm.c_str(), cx0 + 0.5f * cellW, cy0 + cellH - 0.2f * nameH,
                  TextStyle{fit(gm, 0.3f * nameH, cellW - 28.0f), rgb(0x9CC8FF), rgb(0x6A9AE0), rgb(0x05070C), 1.5f,
                            rgb(0x000000, 0.0f), 0.0f, 0.0f});
      }
      if (g.taken) {
        // r68e: the other side has this one: dimmed, "TAKEN" across it.
        fill_rounded(cv, cx0 + 9.0f, cy0 + 9.0f, cx0 + cellW - 9.0f, cy0 + cellH - 9.0f, 10.0f, rgb(0x05070C, 0.62f));
        const std::string tk = "TAKEN";
        draw_text(cv, tk.c_str(), cx0 + 0.5f * cellW, cy0 + 0.45f * cellH,
                  TextStyle{fit(tk, 0.2f * cellH, cellW - 28.0f), rgb(0xFF8A80), rgb(0xE04A40), rgb(0x05070C), 2.0f,
                            rgb(0x000000, 0.0f), 0.0f, 0.0f});
      }
    }
  }
  cv.write(rgba);
}

// r70: see xr_text.h.
void franchise_board(uint8_t* rgba, int w, int h, const std::string& title, const std::string& subtitle,
                     uint32_t rgb1, uint32_t rgb2, const uint8_t* logo, int logoW, int logoH,
                     const std::vector<FrCard>& cards, const std::string& hint) {
  Canvas cv(w, h);
  panel(cv, 6.0f, 6.0f, w - 6.0f, h - 6.0f, 30.0f, 6.0f, rgb(0x000000, 0.92f), rgb(0xFFD23F), rgb(0x1E2A4E, 0.96f),
        rgb(0x0B1020, 0.96f));
  // A picture fitted into a box (area-averaged: the logos are 512 px), centred.
  auto blit = [&](const uint8_t* img, int iw, int ih, float bx, float by, float bw, float bh) {
    if (img == nullptr || iw <= 0 || ih <= 0) return;
    const float s = std::min(bw / iw, bh / ih);
    const int dw = std::max(1, static_cast<int>(iw * s)), dh = std::max(1, static_cast<int>(ih * s));
    const int ox = static_cast<int>(bx + 0.5f * (bw - dw)), oy = static_cast<int>(by + 0.5f * (bh - dh));
    const float step = 1.0f / s;
    const int n = std::max(1, std::min(4, static_cast<int>(step)));
    for (int y = 0; y < dh; ++y) {
      for (int x = 0; x < dw; ++x) {
        float r = 0, g = 0, b = 0, a = 0;
        for (int sy = 0; sy < n; ++sy) {
          for (int sx = 0; sx < n; ++sx) {
            const int px = std::min(iw - 1, static_cast<int>((x + (sx + 0.5f) / n) * step));
            const int py = std::min(ih - 1, static_cast<int>((y + (sy + 0.5f) / n) * step));
            const uint8_t* p = img + (static_cast<size_t>(py) * iw + px) * 4;
            const float pa = p[3] / 255.0f;
            r += p[0] / 255.0f * pa;
            g += p[1] / 255.0f * pa;
            b += p[2] / 255.0f * pa;
            a += pa;
          }
        }
        if (a <= 0.0f) continue;
        cv.over(ox + x, oy + y, Color{r / a, g / a, b / a, 1.0f}, a / (n * n));
      }
    }
  };
  auto fit = [&](const std::string& text, float px, float room) {
    const float wide = text_width(text.c_str(), px);
    return wide > room && wide > 0.0f ? px * room / wide : px;
  };
  // The title band in the club's colours, its logo at the left.
  const float band = 0.14f * h, bx0 = 18.0f, bx1 = w - 18.0f, by0 = 18.0f;
  const Color c1 = rgb(rgb1), c2 = rgb(rgb2);
  for (int y = static_cast<int>(by0); y < static_cast<int>(by0 + band); ++y) {
    const float t = (y - by0) / band;
    for (int x = static_cast<int>(bx0); x < static_cast<int>(bx1); ++x) {
      const float sd = rounded_rect_sd(x + 0.5f, y + 0.5f, bx0, by0, bx1, by0 + band, 20.0f);
      cv.over(x, y, mix(c1, mix(c1, rgb(0x000000), 0.4f), t), 0.5f - sd);
    }
  }
  fill_rounded(cv, bx0, by0 + band - 7.0f, bx1, by0 + band, 3.0f, c2);
  float tx = bx0 + 0.03f * w;
  if (logo != nullptr) {
    blit(logo, logoW, logoH, bx0 + 0.01f * w, by0 + 0.06f * band, 0.88f * band, 0.88f * band);
    tx = bx0 + 0.01f * w + band;
  }
  const std::string tt = caps(title.c_str());
  const float subRoom = subtitle.empty() ? 0.0f : 0.3f * w;
  text_left(cv, tt, tx, by0 + 0.5f * band, 0.5f * band, bx1 - tx - subRoom - 0.03f * w, rgb(0xFFFFFF), rgb(0xE6ECF5), 3.0f);
  if (!subtitle.empty()) {
    const std::string st = caps(subtitle.c_str());
    const float spx = fit(st, 0.3f * band, subRoom);
    draw_text(cv, st.c_str(), bx1 - 0.02f * w - 0.5f * text_width(st.c_str(), spx), by0 + 0.5f * band,
              TextStyle{spx, rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x05070C), 2.5f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  }
  // The hint at the foot.
  const float footer = 0.075f * h;
  if (!hint.empty()) {
    const std::string k = caps(hint.c_str());
    draw_text(cv, k.c_str(), 0.5f * w, h - 0.55f * footer - 8.0f,
              TextStyle{fit(k, 0.48f * footer, 0.92f * w), rgb(0xD8DEEA), rgb(0xB8C2D6), rgb(0x05070C), 2.0f,
                        rgb(0x000000, 0.0f), 0.0f, 0.0f});
  }
  // The cards.
  if (!cards.empty()) {
    float total = 0.0f;
    for (const FrCard& c : cards) total += std::max(0.1f, c.weight);
    const float gap = 0.015f * w, top = by0 + band + 0.025f * h, bottom = h - footer - 0.02f * h;
    const float room = bx1 - bx0 - gap * (cards.size() - 1);
    float x = bx0;
    for (const FrCard& c : cards) {
      const float cw = room * std::max(0.1f, c.weight) / total;
      fill_rounded(cv, x, top, x + cw, bottom, 18.0f, rgb(0x0B1226, 0.85f));
      fill_rounded(cv, x + 3.0f, top + 3.0f, x + cw - 3.0f, bottom - 3.0f, 16.0f, rgb(0x16204A, 0.9f));
      const float head = 0.075f * h;
      if (!c.title.empty()) {
        const std::string ct = caps(c.title.c_str());
        draw_text(cv, ct.c_str(), x + 0.5f * cw, top + 0.55f * head,
                  TextStyle{fit(ct, 0.5f * head, cw - 30.0f), rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x05070C), 2.5f,
                            rgb(0x000000, 0.0f), 0.0f, 0.0f});
      }
      const int n = static_cast<int>(c.lines.size());
      if (n > 0) {
        const float ly0 = top + head + 6.0f;
        const float lh = std::min((bottom - ly0 - 10.0f) / n, 0.075f * h);
        for (int i = 0; i < n; ++i) {
          const FrLine& l = c.lines[i];
          const float y0 = ly0 + i * lh, cy = y0 + 0.5f * lh;
          if (l.cursor) fill_rounded(cv, x + 10.0f, y0 + 2.0f, x + cw - 10.0f, y0 + lh - 2.0f, 10.0f, rgb(0xFFD23F, 0.85f));
          if (l.gold || l.cursor)
            fill_rounded(cv, x + 13.0f, y0 + 5.0f, x + cw - 13.0f, y0 + lh - 5.0f, 8.0f,
                         l.cursor ? rgb(0x3B5AA8) : rgb(0xFFD23F, 0.2f));
          float lx = x + 22.0f;
          if (l.logo != nullptr) {
            blit(l.logo, l.logoW, l.logoH, lx, y0 + 0.08f * lh, 0.84f * lh, 0.84f * lh);
            lx += 0.95f * lh;
          }
          const Color a = l.dim ? rgb(0x8A94AA) : (l.gold ? rgb(0xFFF7B0) : rgb(0xF0F3FA));
          const Color b = l.dim ? rgb(0x6A748A) : (l.gold ? rgb(0xFFC21F) : rgb(0xC8D0E0));
          float rightW = 0.0f;
          const float px = 0.52f * lh;
          if (!l.right.empty()) {
            const std::string rv = caps(l.right.c_str());
            rightW = text_width(rv.c_str(), px) + 24.0f;
            draw_text(cv, rv.c_str(), x + cw - 22.0f - 0.5f * (rightW - 24.0f), cy,
                      TextStyle{px, a, b, rgb(0x05070C), 2.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
          }
          text_left(cv, caps(l.text.c_str()), lx, cy, px, x + cw - 22.0f - rightW - lx, a, b, 2.0f);
        }
      }
      x += cw + gap;
    }
  }
  cv.write(rgba);
}

void guest_tip(uint8_t* rgba, int w, int h, const char* text) {
  Canvas cv(w, h);
  panel(cv, 4.0f, 4.0f, w - 4.0f, h - 4.0f, 0.4f * h, 4.0f, rgb(0x000000, 0.85f), rgb(0xFFD23F), rgb(0x22305A, 0.95f),
        rgb(0x0B1020, 0.95f));
  const std::string t = caps(text);
  const float px0 = 0.46f * h;
  const float wide = text_width(t.c_str(), px0);
  const float px = wide > 0.9f * w ? px0 * 0.9f * w / wide : px0;
  draw_text(cv, t.c_str(), 0.5f * w, 0.52f * h,
            TextStyle{px, rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x05070C), 2.5f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  cv.write(rgba);
}

// r73: see xr_text.h.
void plaque_name(uint8_t* rgba, int w, int h, const char* name, uint32_t top, uint32_t bottom, uint32_t outline) {
  Canvas cv(w, h);
  const std::string n = caps(name);
  float px = 0.62f * h;
  const float wide = text_width(n.c_str(), px);
  if (wide > 0.9f * w) px *= 0.9f * w / wide;
  // a dark rim, then the letters with their blue outline over it
  draw_text(cv, n.c_str(), 0.5f * w, 0.52f * h,
            TextStyle{px, rgb(0x05070C), rgb(0x05070C), rgb(0x05070C), 0.11f * px, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  draw_text(cv, n.c_str(), 0.5f * w, 0.52f * h,
            TextStyle{px, rgb(top), rgb(bottom), rgb(outline), 0.07f * px, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  cv.write(rgba);
}

// r73: see xr_text.h.
void award_standee(uint8_t* rgba, int w, int h, const uint8_t* pic, int pw, int ph, const char* award, const char* name) {
  Canvas cv(w, h);
  const float plateH = 0.2f * h;
  const float plateY0 = h - plateH - 4.0f;
  // the winner, standing on the plate (area-averaged: portraits are 256-512 px)
  if (pic != nullptr && pw > 0 && ph > 0) {
    const float bw = w - 8.0f, bh = plateY0 - 4.0f;
    const float s = std::min(bw / pw, bh / ph);
    const int dw = std::max(1, static_cast<int>(pw * s)), dh = std::max(1, static_cast<int>(ph * s));
    const int ox = static_cast<int>(0.5f * (w - dw)), oy = static_cast<int>(plateY0 - dh);
    const float step = 1.0f / s;
    const int n = std::max(1, std::min(3, static_cast<int>(step)));
    for (int y = 0; y < dh; ++y) {
      for (int x = 0; x < dw; ++x) {
        float r = 0, g = 0, b = 0, a = 0;
        for (int sy = 0; sy < n; ++sy) {
          for (int sx = 0; sx < n; ++sx) {
            const int px = std::min(pw - 1, static_cast<int>((x + (sx + 0.5f) / n) * step));
            const int py = std::min(ph - 1, static_cast<int>((y + (sy + 0.5f) / n) * step));
            const uint8_t* p = pic + (static_cast<size_t>(py) * pw + px) * 4;
            const float pa = p[3] / 255.0f;
            r += p[0] / 255.0f * pa;
            g += p[1] / 255.0f * pa;
            b += p[2] / 255.0f * pa;
            a += pa;
          }
        }
        if (a <= 0.0f) continue;
        cv.over(ox + x, oy + y, Color{r / a, g / a, b / a, 1.0f}, a / (n * n));
      }
    }
  }
  // the plate
  panel(cv, 4.0f, plateY0, w - 4.0f, h - 4.0f, 0.25f * plateH, 4.0f, rgb(0x000000, 0.85f), rgb(0xFFD23F),
        rgb(0x2A3F7A, 0.97f), rgb(0x0E1530, 0.97f));
  const std::string a = caps(award), nm = caps(name);
  const float apx = 0.3f * plateH;
  draw_text(cv, a.c_str(), 0.5f * w, plateY0 + 0.32f * plateH,
            TextStyle{std::min(apx, apx * (w - 30.0f) / std::max(1.0f, text_width(a.c_str(), apx))), rgb(0xFFF7B0),
                      rgb(0xFFC21F), rgb(0x2A1800), 2.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  const float npx = 0.26f * plateH;
  draw_text(cv, nm.c_str(), 0.5f * w, plateY0 + 0.7f * plateH,
            TextStyle{std::min(npx, npx * (w - 30.0f) / std::max(1.0f, text_width(nm.c_str(), npx))), rgb(0xFFFFFF),
                      rgb(0xD8DEEA), rgb(0x05070C), 2.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  cv.write(rgba);
}

// r73: see xr_text.h.
void news_ticker(uint8_t* rgba, int w, int h, const char* tag, const char* text) {
  Canvas cv(w, h);
  panel(cv, 3.0f, 3.0f, w - 3.0f, h - 3.0f, 0.3f * h, 3.0f, rgb(0x000000, 0.85f), rgb(0xFFD23F), rgb(0x1A2448, 0.95f),
        rgb(0x080C1A, 0.95f));
  const std::string t = caps(tag);
  const float tagPx = 0.46f * h;
  const float tagW = text_width(t.c_str(), tagPx) + 0.6f * h;
  fill_rounded(cv, 0.18f * h, 0.16f * h, 0.18f * h + tagW, h - 0.16f * h, 0.18f * h, rgb(0xFFD23F));
  draw_text(cv, t.c_str(), 0.18f * h + 0.5f * tagW, 0.52f * h,
            TextStyle{tagPx, rgb(0x2A1800), rgb(0x2A1800), rgb(0xFFF2A0), 0.5f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  const float x0 = 0.18f * h + tagW + 0.35f * h;
  text_left(cv, caps(text), x0, 0.52f * h, 0.5f * h, w - x0 - 0.3f * h, rgb(0xFFFFFF), rgb(0xD8DEEA), 2.0f);
  cv.write(rgba);
}

// r77: see xr_text.h.
void xp_meter(uint8_t* rgba, int w, int h, const char* name, int level, int xp, int newLevel, int newXp, int gain,
              const char* line, bool gold, const char* side, float glow, int glowFrom, float sideGlow) {
  Canvas cv(w, h);
  // r87: a level up's light: up quickly over its first 0.3 s (the old level showing), then the new level
  // and the light easing off.
  constexpr float kFlip = 0.3f;
  auto light = [](float t) {
    if (t < 0.0f || t >= kLevelGlowS) return 0.0f;
    if (t < kFlip) return t / kFlip;
    const float f = 1.0f - (t - kFlip) / (kLevelGlowS - kFlip);
    return f * f;
  };
  const float lvLight = light(glow), sideLight = light(sideGlow);
  panel(cv, 3.0f, 3.0f, w - 3.0f, h - 3.0f, 0.18f * h, 3.0f, rgb(0x000000, 0.85f),
        mix(rgb(gold ? 0xFFD23F : 0x6FD3FF), rgb(0xFFF2A0), lvLight), rgb(0x1A2448, 0.95f), rgb(0x080C1A, 0.95f));
  // soft gold light behind a piece of text centred at cx, cy
  auto halo = [&](float cx, float cy, float halfW, float halfH, float a) {
    if (a <= 0.0f) return;
    for (int k = 3; k >= 0; --k) {
      const float grow = 1.0f + 0.35f * k;
      const float hw = halfW + 0.18f * h * k, hh = halfH * grow;
      fill_rounded(cv, cx - hw, cy - hh, cx + hw, cy + hh, hh, rgb(0xFFC21F, a * (0.22f - 0.04f * k)));
    }
  };
  const float pad = 0.05f * w;
  // the name, its level as the match stands (r87: no "LV 9 > 11"), what it gains
  const int shown = glow >= 0.0f && glow < kFlip && glowFrom >= 0 ? glowFrom : newLevel;
  const std::string lv = "LV " + std::to_string(shown);
  const std::string plus = "+" + std::to_string(gain) + " XP";
  const float row1 = 0.26f * h, px1 = 0.24f * h;
  const float plusW = text_width(plus.c_str(), px1);
  const float lvW = text_width(lv.c_str(), px1);
  text_left(cv, caps(name), pad, row1, px1, w - 2.0f * pad - plusW - lvW - 0.12f * w, rgb(0xFFFFFF), rgb(0xD8DEEA), 2.0f);
  const float lvX = w - pad - plusW - 0.06f * w - 0.5f * lvW;
  halo(lvX, row1, 0.5f * lvW + 0.03f * w, 0.17f * h, lvLight);
  draw_text(cv, lv.c_str(), lvX, row1,
            TextStyle{px1 * (1.0f + 0.18f * lvLight), mix(rgb(0xFFE070), rgb(0xFFFFFF), lvLight),
                      mix(rgb(0xFFB020), rgb(0xFFF2A0), lvLight), rgb(0x2A1800), 2.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  draw_text(cv, plus.c_str(), w - pad - 0.5f * plusW, row1,
            TextStyle{px1, rgb(0x9CFF8A), rgb(0x3FD24A), rgb(0x062A08), 2.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  // the bar: what it had (gold), what this match adds (green); a level up shows the new level's part
  const float bx0 = pad, bx1 = w - pad, by0 = 0.45f * h, by1 = 0.62f * h;
  fill_rounded(cv, bx0, by0, bx1, by1, 0.5f * (by1 - by0), rgb(0x05070F, 0.9f));
  const float had = newLevel > level ? 0.0f : clamp01(xp / 100.0f);
  const float now = clamp01(newXp / 100.0f);
  if (now > 0.0f)
    fill_rounded(cv, bx0 + 2.0f, by0 + 2.0f, bx0 + 2.0f + (bx1 - bx0 - 4.0f) * now, by1 - 2.0f, 0.5f * (by1 - by0) - 2.0f,
                 rgb(0x3FD24A));
  if (had > 0.0f)
    fill_rounded(cv, bx0 + 2.0f, by0 + 2.0f, bx0 + 2.0f + (bx1 - bx0 - 4.0f) * had, by1 - 2.0f, 0.5f * (by1 - by0) - 2.0f,
                 rgb(0xFFC83A));
  if (lvLight > 0.0f)  // r87: the whole bar lights up with the level
    fill_rounded(cv, bx0 + 2.0f, by0 + 2.0f, bx1 - 2.0f, by1 - 2.0f, 0.5f * (by1 - by0) - 2.0f, rgb(0xFFF2A0, 0.55f * lvLight));
  // the line (an evolution earned, a level up, the next form) and the sidekicks
  const float row3 = 0.81f * h, px3 = 0.17f * h;
  const float sideW = side != nullptr && side[0] != '\0' ? text_width(caps(side).c_str(), px3) : 0.0f;
  if (line != nullptr && line[0] != '\0')
    text_left(cv, caps(line), pad, row3, px3, w - 2.0f * pad - sideW - 0.04f * w, gold ? rgb(0xFFE070) : rgb(0xD8DEEA),
              gold ? rgb(0xFFB020) : rgb(0xA8B0C0), 2.0f);
  if (sideW > 0.0f) {
    halo(w - pad - 0.5f * sideW, row3, 0.5f * sideW + 0.02f * w, 0.12f * h, sideLight);
    draw_text(cv, caps(side).c_str(), w - pad - 0.5f * sideW, row3,
              TextStyle{px3, mix(rgb(0xBFD8FF), rgb(0xFFF2A0), sideLight), mix(rgb(0x8FA8D8), rgb(0xFFC21F), sideLight),
                        rgb(0x05070C), 2.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  }
  cv.write(rgba);
}

// r70c: see xr_text.h.
void evolution_scene(uint8_t* rgba, int w, int h, const EvoImage& from, const EvoImage& to, const EvoImage& alt,
                     int stage, float t, int choice, const char* title, const char* line, const char* hint, bool shiny,
                     int options) {
  Canvas cv(w, h);
  panel(cv, 6.0f, 6.0f, w - 6.0f, h - 6.0f, 30.0f, 6.0f, rgb(0x000000, 0.94f), rgb(0xFFD23F), rgb(0x141C3A, 0.97f),
        rgb(0x05070F, 0.97f));
  const float cx = 0.5f * w, cy = 0.5f * h;
  const float kPi = 3.14159265f;
  // The light behind it: slow turning rays, brighter as the evolution builds and when it's done.
  float rays = 0.0f;
  if (stage == 1) rays = clamp01((t - 1.2f) / 3.0f) * 0.8f;
  if (stage == 2) rays = 1.0f;
  if (rays > 0.0f) {
    // r95: each pixel's cos / sin of 12 x its angle and its fall-off worked out once (the scene is redrawn
    // ~15 times a second; atan2 + exp + cos a pixel made each picture slower than that)
    static std::mutex s_rayMutex;
    static std::vector<float> s_c12, s_s12, s_fall;
    static int s_rw = 0, s_rh = 0;
    std::lock_guard<std::mutex> lock(s_rayMutex);
    if (s_rw != w || s_rh != h) {
      s_rw = w;
      s_rh = h;
      s_c12.assign(static_cast<size_t>(w) * h, 0.0f);
      s_s12.assign(static_cast<size_t>(w) * h, 0.0f);
      s_fall.assign(static_cast<size_t>(w) * h, 0.0f);
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          const float dx = x - cx, dy = y - cy;
          const float r = std::sqrt(dx * dx + dy * dy) / (0.5f * h);
          const float a = std::atan2(dy, dx);
          const size_t i = static_cast<size_t>(y) * w + x;
          s_c12[i] = std::cos(a * 12.0f);
          s_s12[i] = std::sin(a * 12.0f);
          s_fall[i] = std::exp(-1.6f * r) * 0.55f;
        }
    }
    const float turn = 0.25f * t;
    const float ct = std::cos(turn * 12.0f), st = std::sin(turn * 12.0f);
    const Color rc = shiny && stage == 2 ? rgb(0xFFE070) : rgb(0xBFD8FF);
    for (int y = 12; y < h - 12; y += 1) {
      for (int x = 12; x < w - 12; x += 1) {
        const size_t i = static_cast<size_t>(y) * w + x;
        const float band = 0.5f + 0.5f * (s_c12[i] * ct - s_s12[i] * st);  // cos(12 (a + turn))
        const float v = rays * s_fall[i] * (0.25f + 0.75f * band * band);
        if (v > 0.01f) cv.over(x, y, rc, v);
      }
    }
  }
  // A portrait, fitted into the middle; white = the evolution's silhouette.
  auto blit = [&](const EvoImage& im, float bx, float by, float bw, float bh, float white, float alpha) {
    if (im.rgba == nullptr || im.w <= 0 || im.h <= 0 || alpha <= 0.0f) return;
    const float s = std::min(bw / im.w, bh / im.h);
    const int dw = std::max(1, static_cast<int>(im.w * s)), dh = std::max(1, static_cast<int>(im.h * s));
    const int ox = static_cast<int>(bx + 0.5f * (bw - dw)), oy = static_cast<int>(by + 0.5f * (bh - dh));
    for (int y = 0; y < dh; ++y) {
      const int sy = std::min(im.h - 1, static_cast<int>(y / s));
      for (int x = 0; x < dw; ++x) {
        const int sx = std::min(im.w - 1, static_cast<int>(x / s));
        const uint8_t* p = im.rgba + (static_cast<size_t>(sy) * im.w + sx) * 4;
        if (p[3] == 0) continue;
        const Color c = mix(Color{p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, 1.0f}, Color{1.0f, 1.0f, 1.0f, 1.0f}, white);
        cv.over(ox + x, oy + y, c, alpha * p[3] / 255.0f);
      }
    }
  };
  const float picH = 0.58f * h, picW = 0.5f * w;
  const float picX = cx - 0.5f * picW, picY = 0.16f * h;
  if (stage == 0 && options < 2) {
    // r95: the EVO page for one form: the old form, an arrow, the new form's white silhouette (framed)
    const float bw = 0.36f * w;
    const float bxL = 0.07f * w, bxR = w - 0.07f * w - bw;
    fill_rounded(cv, bxL, picY, bxL + bw, picY + picH, 18.0f, rgb(0x2A3F7A, 0.95f));
    blit(from, bxL + 20.0f, picY + 20.0f, bw - 40.0f, picH - 40.0f, 0.0f, 1.0f);
    fill_rounded(cv, bxR - 8.0f, picY - 8.0f, bxR + bw + 8.0f, picY + picH + 8.0f, 22.0f, rgb(0xFFD23F, 0.85f));
    fill_rounded(cv, bxR, picY, bxR + bw, picY + picH, 18.0f, rgb(0x18224A, 0.95f));
    blit(to, bxR + 20.0f, picY + 20.0f, bw - 40.0f, picH - 40.0f, 1.0f, 1.0f);
    const float ap = 0.16f * h;
    const float aw = text_width(">", ap);
    text_left(cv, ">", cx - 0.5f * aw, picY + 0.5f * picH, ap, aw + 4.0f, rgb(0xFFF7B0), rgb(0xFFC21F), 3.0f);
  } else if (stage == 0) {
    // A split: the two forms side by side, the chosen one framed in gold.
    for (int k = 0; k < 2; ++k) {
      const EvoImage& im = k == 0 ? to : alt;
      const float bx = (k == 0 ? 0.08f : 0.54f) * w, bw = 0.38f * w;
      if (k == choice) fill_rounded(cv, bx - 8.0f, picY - 8.0f, bx + bw + 8.0f, picY + picH + 8.0f, 22.0f, rgb(0xFFD23F, 0.85f));
      fill_rounded(cv, bx, picY, bx + bw, picY + picH, 18.0f, rgb(k == choice ? 0x2A3F7A : 0x18224A, 0.95f));
      blit(im, bx + 20.0f, picY + 20.0f, bw - 40.0f, picH - 40.0f, 0.0f, 1.0f);
    }
  } else if (stage == 1) {
    if (t < 1.2f) {
      blit(from, picX, picY, picW, picH, 0.0f, 1.0f);
    } else if (t < 2.2f) {
      blit(from, picX, picY, picW, picH, clamp01(t - 1.2f), 1.0f);
    } else {
      // Back and forth between the two shapes, faster and faster.
      const float u = std::min(t, 7.0f) - 2.2f;
      const float phase = 1.1f * u + 9.0f * u * u * u / (3.0f * 4.8f * 4.8f);
      const bool showTo = (static_cast<int>(std::floor(2.0f * phase)) & 1) != 0;
      const float pulse = 1.0f + 0.04f * std::sin(phase * 2.0f * kPi);
      const float pw = picW * pulse, ph = picH * pulse;
      blit(showTo ? to : from, cx - 0.5f * pw, picY + 0.5f * (picH - ph), pw, ph, 1.0f, 1.0f);
    }
    if (t > 6.8f) {  // the flash
      const float f = t < 7.3f ? clamp01((t - 6.8f) / 0.5f) : clamp01(1.0f - (t - 7.3f) / 0.6f);
      for (int y = 8; y < h - 8; ++y)
        for (int x = 8; x < w - 8; ++x) cv.over(x, y, Color{1.0f, 1.0f, 1.0f, 1.0f}, f);
    }
  } else if (stage == 2) {
    blit(to, picX, picY, picW, picH, clamp01(1.0f - t / 0.8f), 1.0f);
    if (shiny) {
      // sparkles round it
      for (int k = 0; k < 14; ++k) {
        const float a = k * 2.399963f + 0.6f * t;
        const float rr = (0.22f + 0.07f * std::sin(t * 3.0f + k)) * h;
        const float sx = cx + std::cos(a) * rr * 1.4f, sy = picY + 0.5f * picH + std::sin(a) * rr;
        const float tw = 0.5f + 0.5f * std::sin(t * 6.0f + k * 1.7f);
        const float size = (6.0f + 10.0f * tw) * h / 768.0f;
        for (int d = -static_cast<int>(size); d <= static_cast<int>(size); ++d) {
          const float fade = 1.0f - std::fabs(static_cast<float>(d)) / size;
          cv.over(static_cast<int>(sx) + d, static_cast<int>(sy), rgb(0xFFF2A0), fade);
          cv.over(static_cast<int>(sx), static_cast<int>(sy) + d, rgb(0xFFF2A0), fade);
        }
      }
    }
  } else {
    blit(from, picX, picY, picW, picH, 0.0f, 1.0f);
  }
  const std::string tt = caps(title), ll = caps(line), hh = caps(hint);
  const float titlePx = 0.075f * h;
  text_left(cv, tt, 0.5f * w - 0.5f * std::min(text_width(tt.c_str(), titlePx), 0.9f * w), 0.08f * h, titlePx, 0.9f * w,
            rgb(0xFFF7B0), rgb(0xFFC21F), 3.0f);
  if (!ll.empty()) {
    const float lp = 0.06f * h;
    const float lw = std::min(text_width(ll.c_str(), lp), 0.92f * w);
    text_left(cv, ll, 0.5f * w - 0.5f * lw, 0.83f * h, lp, 0.92f * w, shiny && stage == 2 ? rgb(0xFFF2A0) : rgb(0xFFFFFF),
              shiny && stage == 2 ? rgb(0xFFC21F) : rgb(0xD8DEEA), 3.0f);
  }
  if (!hh.empty()) {
    const float hp = 0.04f * h;
    const float hw = std::min(text_width(hh.c_str(), hp), 0.92f * w);
    text_left(cv, hh, 0.5f * w - 0.5f * hw, 0.93f * h, hp, 0.92f * w, rgb(0xB8C2D6), rgb(0x9AA6BE), 2.0f);
  }
  cv.write(rgba);
}

}  // namespace xrtext
