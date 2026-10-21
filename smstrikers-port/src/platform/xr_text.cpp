// strikers-quest r42: see xr_text.h.

#include "xr_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
                 bool hatTrick) {
  Canvas cv(w, h);
  const Color team = rgb(teamRgb);
  // r43d: sized off u (r42's banner height, 420 at 1024 wide); the stats line takes the room below.
  const float u = 0.41f * w;
  panel(cv, 10.0f, 10.0f, w - 10.0f, h - 10.0f, 0.13f * u, 0.03f * u, rgb(0x241300), rgb(0xFFD23F),
        mix(team, rgb(0xFFFFFF), 0.18f), mix(team, rgb(0x000000), 0.45f));

  TextStyle header{0.19f * u, rgb(0xFFFFFF), rgb(0xE6E6E6), rgb(0x161616), 0.018f * u, rgb(0x000000, 0.45f),
                   0.010f * u, 0.012f * u};
  draw_text(cv, "GOAL SCORED BY:", 0.5f * w, 0.29f * u, header);

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
void settings_panel(uint8_t* rgba, int w, int h, int level, int steps, int voice, int heightCm, int row) {
  Canvas cv(w, h);
  const float u = 0.3125f * w;  // r42's whole panel height: 200 at 640 wide
  panel(cv, 8.0f, 8.0f, w - 8.0f, h - 8.0f, 0.16f * u, 0.02f * u, rgb(0x000000, 0.9f), rgb(0xFFFFFF, 0.9f),
        rgb(0x1E2A44, 0.92f), rgb(0x0B1020, 0.92f));
  // The rows' bands, top to bottom: crowd volume (title and bar), voice chat, stadium height; the hint.
  const float band[3][2] = {{0.06f * u, 0.76f * u}, {0.80f * u, 1.18f * u}, {1.22f * u, 1.60f * u}};
  const float bx0 = 0.04f * w, bx1 = 0.96f * w;
  if (row >= 0 && row < 3) {  // the picked row: a lighter band with a gold edge
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
  const char* hint = "RIGHT STICK:  UP / DOWN PICKS,  LEFT / RIGHT CHANGES";
  TextStyle hs{fitted(hint, 0.12f * u), rgb(0xB8C2D6), rgb(0x9AA6BE), rgb(0x05070C), 0.012f * u, rgb(0x000000, 0.0f),
               0.0f, 0.0f};
  draw_text(cv, hint, 0.5f * w, 0.5f * (1.60f * u + h - 8.0f), hs);
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

}  // namespace xrtext
