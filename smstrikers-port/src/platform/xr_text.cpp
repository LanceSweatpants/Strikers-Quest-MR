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
#include "xr_font_karla.h"  // r230
#include "xr_font_roboto.h"  // r236 (LOOK B: the hub's face)

namespace xrtext {
namespace {

// r236: the face (and letter tracking, a share of px) the text routines below rasterise / measure in on this thread - board()
// sets it for each text piece (kFaceLucky outside one: every other picture as before)
thread_local int t_face = kFaceLucky;
thread_local float t_track = 0.0f;
thread_local int t_measure = kFaceLucky;  // board_text_width's (set_measure_face)
struct RasterFace {
  int face;
  float track;
  RasterFace(int f, float tr) : face(t_face), track(t_track) {
    t_face = f;
    t_track = tr;
  }
  ~RasterFace() {
    t_face = face;
    t_track = track;
  }
};

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
  int oy = 0;  // r205: a band of a bigger picture - its rows oy .. oy + h (board_band); 0 for a whole picture
  std::vector<float> px;
  Canvas(int width, int height) : w(width), h(height), px(static_cast<size_t>(width) * height * 4, 0.0f) {}
  Canvas(int width, int rows, int originY) : w(width), h(rows), oy(originY), px(static_cast<size_t>(width) * rows * 4, 0.0f) {}
  int top() const { return oy; }
  int bot() const { return oy + h; }  // (one past the last row)
  void over(int x, int y, const Color& c, float cover) {
    if (x < 0 || y < oy || x >= w || y >= oy + h || cover <= 0.0f) return;
    const float a = c.a * clamp01(cover);
    float* p = &px[(static_cast<size_t>(y - oy) * w + x) * 4];
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
stbtt_fontinfo& lucky_font() {
  static stbtt_fontinfo f = [] {
    stbtt_fontinfo info;
    stbtt_InitFont(&info, kLuckiestGuy, stbtt_GetFontOffsetForIndex(kLuckiestGuy, 0));
    return info;
  }();
  return f;
}
// r236: Roboto Bold Italic / Bold (xr_font_roboto.h, SIL OFL 1.1)
stbtt_fontinfo& roboto_font(bool italic) {
  static stbtt_fontinfo bi = [] {
    stbtt_fontinfo info;
    stbtt_InitFont(&info, kRobotoBoldItalic, stbtt_GetFontOffsetForIndex(kRobotoBoldItalic, 0));
    return info;
  }();
  static stbtt_fontinfo b = [] {
    stbtt_fontinfo info;
    stbtt_InitFont(&info, kRobotoBold, stbtt_GetFontOffsetForIndex(kRobotoBold, 0));
    return info;
  }();
  return italic ? bi : b;
}
stbtt_fontinfo& font() {
  if (t_face == kFaceBroadcast) return roboto_font(true);
  if (t_face == kFaceBroadcastUp) return roboto_font(false);
  return lucky_font();
}

// r230: Karla (LANCINI EXQUISITE's wide-tracked caps; xr_font_karla.h)
stbtt_fontinfo& karla_font() {
  static stbtt_fontinfo f = [] {
    stbtt_fontinfo info;
    stbtt_InitFont(&info, kKarlaRegular, stbtt_GetFontOffsetForIndex(kKarlaRegular, 0));
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
      x += t_track * px;  // r236: tracking (0 but for a tracked label)
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
  // r187 crash fix: a size <= 0 (or NaN / huge) made a negative / huge mask and the vector threw std::length_error,
  // killing the app (r185's select menu). Such text is drawn as nothing; the mask is capped at 16384 px a side.
  if (!(px > 0.0f) || !(px < 16384.0f) || pad < 0 || pad > 4096) return m;
  const float tw = text_width(text, px);
  if (!(tw >= 0.0f) || !(tw < 16384.0f)) return m;
  m.w = static_cast<int>(std::ceil(tw)) + 2 * pad;
  m.h = static_cast<int>(std::ceil((ascent - descent) * scale)) + 2 * pad;
  if (m.w <= 0 || m.h <= 0) {
    m.w = m.h = 0;
    return m;
  }
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
    if (c[1] != '\0') x += scale * stbtt_GetCodepointKernAdvance(&f, cp, static_cast<unsigned char>(c[1])) + t_track * px;  // (r236: + tracking)
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
                    int popups, bool home, int weather, int music, int mySongs) {
  Canvas cv(w, h);
  const float u = 0.3125f * w;  // r42's whole panel height: 200 at 640 wide
  panel(cv, 8.0f, 8.0f, w - 8.0f, h - 8.0f, 0.16f * u, 0.02f * u, rgb(0x000000, 0.9f), rgb(0xFFFFFF, 0.9f),
        rgb(0x1E2A44, 0.92f), rgb(0x0B1020, 0.92f));
  // The rows' bands, top to bottom: crowd volume (title and bar), voice chat, stadium height; r104: the
  // trophy shelf + badge board, the achievement pop-ups; the hint.
  const float band[8][2] = {{0.06f * u, 0.76f * u}, {0.80f * u, 1.18f * u}, {1.22f * u, 1.60f * u},
                            {1.64f * u, 2.02f * u}, {2.06f * u, 2.44f * u}, {2.48f * u, 2.86f * u},  // r133: + HOME GROUND
                            {2.90f * u, 3.28f * u},                                                   // r137: + WEATHER
                            {3.32f * u, 3.70f * u}};                                                  // r147: + MENU MUSIC
  const float bx0 = 0.04f * w, bx1 = 0.96f * w;
  if (row >= 0 && row < 8) {  // the picked row: a lighter band with a gold edge
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
  onoff(home ? "HOME GROUND: ON" : "HOME GROUND: OFF", 2.67f * u, 5, home ? 2 : 0);  // r133: board, banners, corner, shelf
  // r137: the weather of quick matches (OFF: the franchise calendar too): random green, a set one amber, off grey
  {
    static const char* const kWeather[6] = {"WEATHER: RANDOM", "WEATHER: CLEAR", "WEATHER: RAIN",
                                            "WEATHER: SNOW",   "WEATHER: NIGHT", "WEATHER: OFF"};
    const int wm = weather >= 0 && weather < 6 ? weather : 0;
    onoff(kWeather[wm], 3.09f * u, 6, wm == 0 ? 2 : (wm == 5 ? 0 : 1));
  }
  // r147: the menus' music (port/menu_music.h: 0 LOFI, 1 MY MUSIC, 2 BOTH, 3 OFF): BOTH green, one of the two amber,
  // off (the game's own menu music) grey; MY MUSIC says how many of his songs it found in Strikers/music
  {
    char ml[96];
    const int mm = music >= 0 && music < 4 ? music : 2;
    if (mm == 1 && mySongs == 0) {
      std::snprintf(ml, sizeof(ml), "MENU MUSIC: MY MUSIC (EMPTY: LOFI)");
    } else if (mm == 1 && mySongs > 0) {
      std::snprintf(ml, sizeof(ml), "MENU MUSIC: MY MUSIC (%d SONG%s)", mySongs, mySongs == 1 ? "" : "S");
    } else if (mm == 2 && mySongs > 0) {
      std::snprintf(ml, sizeof(ml), "MENU MUSIC: LOFI + MY %d SONG%s", mySongs, mySongs == 1 ? "" : "S");
    } else {
      static const char* const kMusic[4] = {"MENU MUSIC: LOFI", "MENU MUSIC: MY MUSIC", "MENU MUSIC: BOTH",
                                            "MENU MUSIC: OFF (THE GAME'S OWN)"};
      std::snprintf(ml, sizeof(ml), "%s", kMusic[mm]);
    }
    onoff(ml, 3.51f * u, 7, mm == 2 ? 2 : (mm == 3 ? 0 : 1));
  }
  const char* hint = "RIGHT STICK:  UP / DOWN PICKS,  LEFT / RIGHT CHANGES";
  TextStyle hs{fitted(hint, 0.12f * u), rgb(0xB8C2D6), rgb(0x9AA6BE), rgb(0x05070C), 0.012f * u, rgb(0x000000, 0.0f),
               0.0f, 0.0f};
  draw_text(cv, hint, 0.5f * w, 0.5f * (3.70f * u + h - 8.0f), hs);
  cv.write(rgba);
}

// r147: the menu music's "NOW PLAYING" card (port/menu_music.h): a gold disc with two beamed notes on the left;
// "NOW PLAYING" in gold over the title (white, as big as it fits, cut with "..." past that) and the artist (grey).
void now_playing_toast(uint8_t* rgba, int w, int h, const char* title, const char* artist) {
  Canvas cv(w, h);
  const float u = static_cast<float>(h);
  panel(cv, 6.0f, 6.0f, w - 6.0f, h - 6.0f, 0.24f * u, 0.022f * u, rgb(0x000000, 0.8f), rgb(0xFFD23F, 0.95f),
        rgb(0x1E2A44, 0.94f), rgb(0x0B1020, 0.94f));
  auto caps_of = [](const char* s) {
    std::string r = s != nullptr ? s : "";
    for (char& c : r) {
      if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
      else if (static_cast<unsigned char>(c) >= 0x80 || c < 0x20) c = ' ';  // outside the font
    }
    return r;
  };
  // The disc and its notes.
  const float R = 0.33f * u, dcx = 0.12f * u + R, dcy = 0.5f * u;
  auto ellipse_sd = [](float px, float py, float cx, float cy, float rx, float ry, float rot) {
    const float c = std::cos(rot), s = std::sin(rot);
    const float x = (px - cx) * c + (py - cy) * s, y = -(px - cx) * s + (py - cy) * c;
    return (std::sqrt((x / rx) * (x / rx) + (y / ry) * (y / ry)) - 1.0f) * std::min(rx, ry);
  };
  auto box_sd = [](float px, float py, float x0, float y0, float x1, float y1) {
    const float qx = std::max(x0 - px, px - x1), qy = std::max(y0 - py, py - y1);
    return std::max(qx, qy);
  };
  const float h1x = dcx - 0.30f * R, h1y = dcy + 0.36f * R, h2x = dcx + 0.36f * R, h2y = dcy + 0.24f * R;
  const float hrx = 0.25f * R, hry = 0.18f * R, stemW = 0.075f * R, top1 = dcy - 0.52f * R, top2 = dcy - 0.64f * R;
  const float s1x = h1x + hrx * 0.86f, s2x = h2x + hrx * 0.86f;
  for (int y = static_cast<int>(dcy - R) - 3; y <= static_cast<int>(dcy + R) + 3; ++y) {
    for (int x = static_cast<int>(dcx - R) - 3; x <= static_cast<int>(dcx + R) + 3; ++x) {
      const float px = x + 0.5f, py = y + 0.5f;
      const float d = std::sqrt((px - dcx) * (px - dcx) + (py - dcy) * (py - dcy)) - R;
      cv.over(x, y, rgb(0x241300), 0.5f - (d - 3.0f));
      const float t = clamp01((py - (dcy - R)) / (2.0f * R));
      cv.over(x, y, mix(rgb(0xFFF07A), rgb(0xFF9800), t), 0.5f - d);
      // the notes: two heads, two stems, the beam between the stems' tops (sloping up to the right)
      float n = std::min(ellipse_sd(px, py, h1x, h1y, hrx, hry, -0.35f), ellipse_sd(px, py, h2x, h2y, hrx, hry, -0.35f));
      n = std::min(n, box_sd(px, py, s1x - stemW, top1, s1x, h1y));
      n = std::min(n, box_sd(px, py, s2x - stemW, top2, s2x, h2y));
      const float bt = (s2x - s1x) > 0.0f ? clamp01((px - (s1x - stemW)) / (s2x - (s1x - stemW))) : 0.0f;
      const float beamTop = top1 + (top2 - top1) * bt;
      if (px >= s1x - stemW && px <= s2x) n = std::min(n, std::max(beamTop - py, py - (beamTop + 0.17f * R)));
      cv.over(x, y, rgb(0x0B1020), 0.5f - n);
    }
  }
  // The words, left-aligned in the room right of the disc.
  const float left = dcx + R + 0.14f * u, right = w - 0.1f * u, room = right - left;
  auto put_left = [&](const std::string& s, float y, const TextStyle& st) {
    draw_text(cv, s.c_str(), left + 0.5f * text_width(s.c_str(), st.px), y, st);
  };
  const TextStyle head{0.17f * u, rgb(0xFFF7B0), rgb(0xFFC21F), rgb(0x05070C), 0.012f * u, rgb(0x000000, 0.0f), 0.0f, 0.0f};
  put_left("NOW PLAYING", 0.22f * u, head);
  auto fit_line = [&](std::string s, float px, float minPx) {
    float p = px;
    const float wide = text_width(s.c_str(), p);
    if (wide > room) p = std::max(minPx, px * room / wide);
    while (s.size() > 4 && text_width(s.c_str(), p) > room) s = s.substr(0, s.size() - 4) + "...";
    return std::make_pair(s, p);
  };
  std::string t = caps_of(title);
  if (t.empty()) t = "MENU MUSIC";
  const auto tl = fit_line(t, 0.30f * u, 0.21f * u);
  put_left(tl.first, 0.52f * u,
           TextStyle{tl.second, rgb(0xFFFFFF), rgb(0xDDE4F0), rgb(0x05070C), 0.016f * u, rgb(0x000000, 0.45f), 0.006f * u,
                     0.008f * u});
  const std::string a = caps_of(artist);
  if (!a.empty()) {
    const auto al = fit_line(a, 0.18f * u, 0.14f * u);
    put_left(al.first, 0.80f * u,
             TextStyle{al.second, rgb(0xC8D2E6), rgb(0xA8B4CC), rgb(0x05070C), 0.01f * u, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  }
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
  if (!(room > 0.0f) || !(px > 0.0f)) return;  // r187: no room, nothing drawn (a negative size crashed r185)
  const float wide = text_width(text.c_str(), px);
  if (wide > room && wide > 0.0f) px *= room / wide;
  const float w = text_width(text.c_str(), px);
  draw_text(cv, text.c_str(), x + 0.5f * w, cy,
            TextStyle{px, top, bottom, rgb(0x05070C), outline, rgb(0x000000, 0.0f), 0.0f, 0.0f});
}

// A filled rounded rectangle.
void fill_rounded(Canvas& cv, float x0, float y0, float x1, float y1, float r, Color c) {
  for (int y = std::max(cv.top(), static_cast<int>(y0) - 1); y <= std::min(cv.bot() - 1, static_cast<int>(y1) + 1); ++y) {
    for (int x = static_cast<int>(x0) - 1; x <= static_cast<int>(x1) + 1; ++x) {
      const float sd = rounded_rect_sd(x + 0.5f, y + 0.5f, x0, y0, x1, y1, r);
      cv.over(x, y, c, 0.5f - sd);
    }
  }
}

// r112: a straight-alpha picture fitted into a box (bottom-centred when `bottom`, else centred): area-averaged
// where it shrinks, bilinear where it grows (the menu's big portraits); `tint` (alpha > 0) in place of its
// colours (a silhouette), `fade` its opacity.
void blit_fit(Canvas& cv, const uint8_t* img, int iw, int ih, float bx, float by, float bw, float bh, bool bottom,
              Color tint = Color{0.0f, 0.0f, 0.0f, 0.0f}, float fade = 1.0f) {
  if (img == nullptr || iw <= 0 || ih <= 0 || bw <= 1.0f || bh <= 1.0f) return;
  const float s = std::min(bw / iw, bh / ih);
  const int dw = std::max(1, static_cast<int>(iw * s)), dh = std::max(1, static_cast<int>(ih * s));
  const int ox = static_cast<int>(bx + 0.5f * (bw - dw));
  const int oy = static_cast<int>(bottom ? by + bh - dh : by + 0.5f * (bh - dh));
  const float step = 1.0f / s;
  auto texel = [&](int x, int y, float* r, float* g, float* b, float* a) {
    x = std::max(0, std::min(iw - 1, x));
    y = std::max(0, std::min(ih - 1, y));
    const uint8_t* p = img + (static_cast<size_t>(y) * iw + x) * 4;
    const float pa = p[3] / 255.0f;
    *r += p[0] / 255.0f * pa;
    *g += p[1] / 255.0f * pa;
    *b += p[2] / 255.0f * pa;
    *a += pa;
  };
  for (int y = 0; y < dh; ++y) {
    if (oy + y < cv.top() || oy + y >= cv.bot()) continue;  // (r205: a band - its rows only)
    for (int x = 0; x < dw; ++x) {
      float r = 0, g = 0, b = 0, a = 0;
      if (s < 1.0f) {
        const int n = std::max(1, std::min(4, static_cast<int>(std::ceil(step))));
        for (int sy = 0; sy < n; ++sy)
          for (int sx = 0; sx < n; ++sx)
            texel(static_cast<int>((x + (sx + 0.5f) / n) * step), static_cast<int>((y + (sy + 0.5f) / n) * step), &r, &g,
                  &b, &a);
        r /= n * n;
        g /= n * n;
        b /= n * n;
        a /= n * n;
      } else {
        const float fx = (x + 0.5f) * step - 0.5f, fy = (y + 0.5f) * step - 0.5f;
        const int x0 = static_cast<int>(std::floor(fx)), y0 = static_cast<int>(std::floor(fy));
        const float tx = fx - x0, ty = fy - y0;
        float c[4][4] = {};
        texel(x0, y0, &c[0][0], &c[0][1], &c[0][2], &c[0][3]);
        texel(x0 + 1, y0, &c[1][0], &c[1][1], &c[1][2], &c[1][3]);
        texel(x0, y0 + 1, &c[2][0], &c[2][1], &c[2][2], &c[2][3]);
        texel(x0 + 1, y0 + 1, &c[3][0], &c[3][1], &c[3][2], &c[3][3]);
        float v[4];
        for (int k = 0; k < 4; ++k)
          v[k] = (c[0][k] * (1 - tx) + c[1][k] * tx) * (1 - ty) + (c[2][k] * (1 - tx) + c[3][k] * tx) * ty;
        r = v[0];
        g = v[1];
        b = v[2];
        a = v[3];
      }
      if (a <= 0.002f) continue;
      const Color c = tint.a > 0.0f ? Color{tint.r, tint.g, tint.b, 1.0f} : Color{r / a, g / a, b / a, 1.0f};
      cv.over(ox + x, oy + y, c, a * fade);
    }
  }
}

// r131 (STRIKERS ILLUSTRATED's covers): a straight-alpha picture into a box with the board's picture options:
// trimmed of its see-through margins, fitted (bottom-centred when `bottom`) or cropped to fill the box (fx, fy: the
// part kept, 0..1), mirrored, duotone (dark -> light by brightness) or a silhouette (`tint`), `fade` its opacity.
// Area-averaged where it shrinks, bilinear where it grows (as blit_fit).
void blit_ex(Canvas& cv, const uint8_t* img, int iw, int ih, float bx, float by, float bw, float bh, bool bottom,
             bool crop, float fx, float fy, bool trim, bool flip, bool duo, Color dark, Color light, Color tint,
             float fade) {
  if (img == nullptr || iw <= 0 || ih <= 0 || bw <= 1.0f || bh <= 1.0f) return;
  int sx0 = 0, sy0 = 0, sx1 = iw, sy1 = ih;
  if (trim) {
    int minX = iw, minY = ih, maxX = -1, maxY = -1;
    for (int y = 0; y < ih; ++y)
      for (int x = 0; x < iw; ++x)
        if (img[(static_cast<size_t>(y) * iw + x) * 4 + 3] > 10) {
          minX = std::min(minX, x);
          maxX = std::max(maxX, x);
          minY = std::min(minY, y);
          maxY = std::max(maxY, y);
        }
    if (maxX < 0) return;
    sx0 = minX;
    sy0 = minY;
    sx1 = maxX + 1;
    sy1 = maxY + 1;
  }
  const float sw = static_cast<float>(sx1 - sx0), sh = static_cast<float>(sy1 - sy0);
  float s = 0.0f, dx0 = 0.0f, dy0 = 0.0f, cx0 = 0.0f, cy0 = 0.0f, cx1 = 0.0f, cy1 = 0.0f;
  if (crop) {
    s = std::max(bw / sw, bh / sh);
    dx0 = bx + (bw - sw * s) * fx;
    dy0 = by + (bh - sh * s) * fy;
    cx0 = bx;
    cy0 = by;
    cx1 = bx + bw;
    cy1 = by + bh;
  } else {
    s = std::min(bw / sw, bh / sh);
    dx0 = bx + 0.5f * (bw - sw * s);
    dy0 = bottom ? by + bh - sh * s : by + 0.5f * (bh - sh * s);
    cx0 = dx0;
    cy0 = dy0;
    cx1 = dx0 + sw * s;
    cy1 = dy0 + sh * s;
  }
  const float step = 1.0f / s;
  auto texel = [&](int x, int y, float* r, float* g, float* b, float* a) {
    x = std::max(sx0, std::min(sx1 - 1, x));
    y = std::max(sy0, std::min(sy1 - 1, y));
    const uint8_t* p = img + (static_cast<size_t>(y) * iw + x) * 4;
    const float pa = p[3] / 255.0f;
    *r += p[0] / 255.0f * pa;
    *g += p[1] / 255.0f * pa;
    *b += p[2] / 255.0f * pa;
    *a += pa;
  };
  const int px0 = std::max(0, static_cast<int>(std::floor(cx0))), px1 = std::min(cv.w, static_cast<int>(std::ceil(cx1)));
  const int py0 = std::max(cv.top(), static_cast<int>(std::floor(cy0))), py1 = std::min(cv.bot(), static_cast<int>(std::ceil(cy1)));
  const int n = std::max(1, std::min(4, static_cast<int>(std::ceil(step))));
  for (int y = py0; y < py1; ++y) {
    for (int x = px0; x < px1; ++x) {
      float u = (x + 0.5f - dx0) * step;
      const float v = (y + 0.5f - dy0) * step;
      if (u < 0.0f || v < 0.0f || u > sw || v > sh) continue;
      if (flip) u = sw - u;
      float r = 0, g = 0, b = 0, a = 0;
      if (s < 1.0f) {
        for (int ky = 0; ky < n; ++ky)
          for (int kx = 0; kx < n; ++kx)
            texel(sx0 + static_cast<int>(std::floor(u - 0.5f * step + (kx + 0.5f) / n * step)),
                  sy0 + static_cast<int>(std::floor(v - 0.5f * step + (ky + 0.5f) / n * step)), &r, &g, &b, &a);
        r /= n * n;
        g /= n * n;
        b /= n * n;
        a /= n * n;
      } else {
        const float fu = u - 0.5f, fv = v - 0.5f;
        const int x0 = static_cast<int>(std::floor(fu)), y0 = static_cast<int>(std::floor(fv));
        const float tx = fu - x0, ty = fv - y0;
        float c[4][4] = {};
        texel(sx0 + x0, sy0 + y0, &c[0][0], &c[0][1], &c[0][2], &c[0][3]);
        texel(sx0 + x0 + 1, sy0 + y0, &c[1][0], &c[1][1], &c[1][2], &c[1][3]);
        texel(sx0 + x0, sy0 + y0 + 1, &c[2][0], &c[2][1], &c[2][2], &c[2][3]);
        texel(sx0 + x0 + 1, sy0 + y0 + 1, &c[3][0], &c[3][1], &c[3][2], &c[3][3]);
        float vv[4];
        for (int k = 0; k < 4; ++k)
          vv[k] = (c[0][k] * (1 - tx) + c[1][k] * tx) * (1 - ty) + (c[2][k] * (1 - tx) + c[3][k] * tx) * ty;
        r = vv[0];
        g = vv[1];
        b = vv[2];
        a = vv[3];
      }
      if (a <= 0.002f) continue;
      Color c{r / a, g / a, b / a, 1.0f};
      if (tint.a > 0.0f) {
        c = Color{tint.r, tint.g, tint.b, 1.0f};
      } else if (duo) {
        const float lum = clamp01(0.3f * c.r + 0.59f * c.g + 0.11f * c.b);
        c = mix(dark, light, lum);
        c.a = 1.0f;
      }
      cv.over(x, y, c, a * fade);
    }
  }
}

// r131: `text` as draw_text, condensed (xscale), leaning (slant: x moved per px up) and turned (degrees, clockwise)
// about its middle (cx, cy); the shadow at (shadowX, shadowY), the outline, the fill. Coverage sampled bilinearly.
void draw_text_affine(Canvas& cv, const char* text, float cx, float cy, const TextStyle& s, float xscale, float slant,
                      float angle) {
  const int pad = static_cast<int>(std::ceil(s.outlinePx)) + 2;
  const Mask m = text_mask(text, s.px, pad);
  if (m.w <= 0 || m.h <= 0) return;
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
  if (top > bottom) {
    top = 0;
    bottom = m.h;
  }
  const float span = std::max(1.0f, static_cast<float>(bottom - top));
  std::vector<float> ring(m.a.size());
  for (size_t i = 0; i < ring.size(); ++i) ring[i] = std::max(m.a[i], clamp01(s.outlinePx + 0.5f - dist[i]));
  const float mcx = 0.5f * m.w, mcy = 0.5f * (top + bottom);
  const float rad = angle * 0.017453293f;
  const float ca = std::cos(rad), sa = std::sin(rad);
  const float xs = std::max(0.05f, xscale);
  float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
  const float cu[4] = {-mcx, m.w - mcx, -mcx, m.w - mcx}, cvv[4] = {-mcy, -mcy, m.h - mcy, m.h - mcy};
  for (int k = 0; k < 4; ++k) {
    const float u2 = cu[k] * xs - slant * cvv[k];
    const float X = cx + u2 * ca - cvv[k] * sa, Y = cy + u2 * sa + cvv[k] * ca;
    bx0 = std::min(bx0, X);
    by0 = std::min(by0, Y);
    bx1 = std::max(bx1, X);
    by1 = std::max(by1, Y);
  }
  auto sample = [&](const std::vector<float>& M, float u, float v) {
    const float fu = u - 0.5f, fv = v - 0.5f;
    const int x0 = static_cast<int>(std::floor(fu)), y0 = static_cast<int>(std::floor(fv));
    const float tx = fu - x0, ty = fv - y0;
    auto at = [&](int x, int y) {
      return x < 0 || y < 0 || x >= m.w || y >= m.h ? 0.0f : M[static_cast<size_t>(y) * m.w + x];
    };
    return (at(x0, y0) * (1 - tx) + at(x0 + 1, y0) * tx) * (1 - ty) + (at(x0, y0 + 1) * (1 - tx) + at(x0 + 1, y0 + 1) * tx) * ty;
  };
  for (int pass = 0; pass < 3; ++pass) {
    if (pass == 0 && (s.shadow.a <= 0.0f || (s.shadowX == 0.0f && s.shadowY == 0.0f))) continue;
    if (pass == 1 && s.outlinePx <= 0.0f) continue;
    const float ox = pass == 0 ? s.shadowX : 0.0f, oy = pass == 0 ? s.shadowY : 0.0f;
    const int y0 = static_cast<int>(std::floor(by0 + oy)) - 1, y1 = static_cast<int>(std::ceil(by1 + oy)) + 1;
    const int x0 = static_cast<int>(std::floor(bx0 + ox)) - 1, x1 = static_cast<int>(std::ceil(bx1 + ox)) + 1;
    for (int y = std::max(cv.top(), y0); y <= std::min(cv.bot() - 1, y1); ++y) {
      for (int x = std::max(0, x0); x <= std::min(cv.w - 1, x1); ++x) {
        const float dx = x + 0.5f - (cx + ox), dy = y + 0.5f - (cy + oy);
        const float u2 = dx * ca + dy * sa, v = -dx * sa + dy * ca;
        const float mu = (u2 + slant * v) / xs + mcx, mv = v + mcy;
        if (pass == 2) {
          const float cov = sample(m.a, mu, mv);
          if (cov > 0.0f) cv.over(x, y, mix(s.fillTop, s.fillBottom, clamp01((mv - top) / span)), cov);
        } else {
          const float cov = sample(ring, mu, mv);
          if (cov > 0.0f) cv.over(x, y, pass == 0 ? s.shadow : s.outline, cov);
        }
      }
    }
  }
}

// r112: a soft round light (strongest in the middle, gone at `radius`).
void glow_disc(Canvas& cv, float cx, float cy, float radius, Color c) {
  for (int y = static_cast<int>(cy - radius); y <= static_cast<int>(cy + radius); ++y) {
    for (int x = static_cast<int>(cx - radius); x <= static_cast<int>(cx + radius); ++x) {
      const float d = std::sqrt((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy)) / radius;
      if (d >= 1.0f) continue;
      cv.over(x, y, c, (1.0f - d) * (1.0f - d));
    }
  }
}

// r112: a controller button for the hints: a coloured circle with its letter (or a rounded pill for "LT RT"),
// left edge at x; returns its width.
float button_glyph(Canvas& cv, const char* glyph, float x, float cy, float size, Color fill) {
  const float r = 0.5f * size;
  const float tw = text_width(glyph, 0.62f * size);
  const bool pill = std::strlen(glyph) > 1;
  const float wide = pill ? std::max(size, tw + 0.7f * size) : size;
  fill_rounded(cv, x - 3.0f, cy - r - 3.0f, x + wide + 3.0f, cy + r + 3.0f, r + 3.0f, rgb(0xFFFFFF));
  fill_rounded(cv, x, cy - r, x + wide, cy + r, r, fill);
  draw_text(cv, glyph, x + 0.5f * wide, cy,
            TextStyle{0.62f * size, rgb(0xFFFFFF), rgb(0xE6ECF5), rgb(0x05070C), 2.0f, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  return wide;
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
        // r110: a Marvel hero's locked costume is earned with levels, not evolved
        const bool marvel = g.game != nullptr && std::strcmp(g.game, "Marvel") == 0;
        const std::string gm = g.locked ? std::string(marvel ? "UNLOCK IN FRANCHISE" : "EVOLVE IN FRANCHISE") : caps(g.game);
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

// r112: see xr_text.h.
void character_select(uint8_t* rgba, int w, int h, const char* title, const SelectTab* tabs, int tabCount, int tab,
                      bool onTabs, const SelectTile* tiles, int count, int cursor, int firstRow, const SelectSpot& spot,
                      const SelectHint* hints, int hintCount) {
  Canvas cv(w, h);
  panel(cv, 6.0f, 6.0f, w - 6.0f, h - 6.0f, 30.0f, 6.0f, rgb(0x000000, 0.92f), rgb(0xFFD23F), rgb(0x1E2A4E, 0.97f),
        rgb(0x070A14, 0.97f));
  auto fit = [&](const std::string& text, float px, float room) {
    const float wide = text_width(text.c_str(), px);
    return wide > room && wide > 0.0f ? px * room / wide : px;
  };
  auto words = [&](const std::string& s, float cx, float cy, float px, float room, Color a, Color b, float outline) {
    draw_text(cv, s.c_str(), cx, cy, TextStyle{fit(s, px, room), a, b, rgb(0x05070C), outline, rgb(0x000000, 0.0f), 0.0f, 0.0f});
  };
  const Color gold = rgb(0xFFD23F), cream = rgb(0xFFF7B0), white = rgb(0xFFFFFF), soft = rgb(0xD8DEEA);
  const uint32_t gameRgb = tab >= 0 && tab < tabCount ? tabs[tab].rgb : 0x31406A;
  const Color game = rgb(gameRgb);
  // the games, one tab each
  const float m = 0.014f * w, tabsTop = 0.02f * h, tabsBottom = 0.115f * h;
  if (tabCount > 0) {
    const float tw = (w - 2.0f * m) / tabCount;
    for (int i = 0; i < tabCount; ++i) {
      const bool on = i == tab;
      const float x0 = m + i * tw + 8.0f, x1 = m + (i + 1) * tw - 8.0f, y0 = on ? tabsTop : tabsTop + 0.014f * h;
      if (on && onTabs) {
        for (int k = 3; k >= 1; --k)
          fill_rounded(cv, x0 - 6.0f - 4.0f * k, y0 - 6.0f - 4.0f * k, x1 + 6.0f + 4.0f * k, tabsBottom + 6.0f + 4.0f * k,
                       26.0f + 4.0f * k, rgb(0xFFD23F, 0.22f));
      }
      if (on) fill_rounded(cv, x0 - 6.0f, y0 - 6.0f, x1 + 6.0f, tabsBottom + 6.0f, 26.0f, onTabs ? white : gold);
      fill_rounded(cv, x0, y0, x1, tabsBottom, 22.0f, rgb(tabs[i].rgb, on ? 1.0f : 0.55f));
      const std::string n = caps(tabs[i].name);
      words(n, 0.5f * (x0 + x1), 0.5f * (y0 + tabsBottom) - 0.008f * h, on ? 0.058f * h : 0.044f * h, x1 - x0 - 24.0f,
            on ? white : rgb(0xFFFFFF, 0.7f), on ? rgb(0xE6ECF5) : rgb(0xD8DEEA, 0.7f), on ? 3.5f : 2.5f);
      const std::string c = std::to_string(tabs[i].count);
      words(c, 0.5f * (x0 + x1), tabsBottom - 0.012f * h, 0.02f * h, x1 - x0, rgb(0xFFFFFF, on ? 0.85f : 0.5f),
            rgb(0xD8DEEA, on ? 0.85f : 0.5f), 1.5f);
    }
  }
  const float top = 0.135f * h, bottom = 0.915f * h;
  // the spotlight: the one under the cursor, big
  const float sx0 = m, sx1 = 0.398f * w, scx = 0.5f * (sx0 + sx1);
  fill_rounded(cv, sx0, top, sx1, bottom, 28.0f, mix(game, rgb(0x000000), 0.55f));
  glow_disc(cv, scx, 0.47f * h, 0.27f * w, mix(mix(game, rgb(0xFFE696), 0.35f), rgb(0x000000, 0.0f), 0.2f));
  const float formY0 = top + 0.012f * h, formS = 0.092f * h;
  const bool strip = spot.forms.size() > 1;
  const float picTop = strip ? formY0 + formS + 0.02f * h : top + 0.02f * h, picBottom = 0.665f * h;
  if (spot.rgba != nullptr) {
    blit_fit(cv, spot.rgba, spot.w, spot.h, sx0 + 0.02f * w, picTop, sx1 - sx0 - 0.04f * w, picBottom - picTop, true,
             spot.locked ? Color{0.05f, 0.07f, 0.13f, 1.0f} : Color{0.0f, 0.0f, 0.0f, 0.0f});
  }
  if (spot.taken) {
    fill_rounded(cv, sx0 + 0.03f * w, 0.4f * h, sx1 - 0.03f * w, 0.52f * h, 20.0f, rgb(0x05070C, 0.7f));
    words("TAKEN", scx, 0.46f * h, 0.09f * h, sx1 - sx0 - 0.08f * w, rgb(0xFF8A80), rgb(0xE04A40), 3.0f);
  }
  if (strip) {
    // its forms: the evolutions / costumes, the shown one framed; LT / RT either side
    const int n = static_cast<int>(spot.forms.size());
    const float gap = 0.008f * w;
    float fx = scx - 0.5f * (n * formS + (n - 1) * gap);
    for (int i = 0; i < n; ++i) {
      const SelectForm& f = spot.forms[i];
      const bool on = i == spot.formAt;
      fill_rounded(cv, fx - (on ? 5.0f : 2.0f), formY0 - (on ? 5.0f : 2.0f), fx + formS + (on ? 5.0f : 2.0f),
                   formY0 + formS + (on ? 5.0f : 2.0f), 16.0f, on ? gold : rgb(0x6A748A));
      fill_rounded(cv, fx, formY0, fx + formS, formY0 + formS, 14.0f, rgb(0x0B1020, 0.95f));
      blit_fit(cv, f.rgba, f.w, f.h, fx + 6.0f, formY0 + 6.0f, formS - 12.0f, formS - 12.0f, true,
               f.locked ? Color{0.09f, 0.12f, 0.2f, 1.0f} : Color{0.0f, 0.0f, 0.0f, 0.0f});
      if (f.locked) {
        const std::string lv = f.level > 0 ? "LV " + std::to_string(f.level) : "?";
        words(lv, fx + 0.5f * formS, formY0 + 0.5f * formS, 0.034f * h, formS - 10.0f, cream, gold, 2.5f);
      }
      fx += formS + gap;
    }
    const float lx0 = scx - 0.5f * (n * formS + (n - 1) * gap) - 0.012f * w;
    const float rx0 = scx + 0.5f * (n * formS + (n - 1) * gap) + 0.012f * w;
    const float gs = 0.05f * h;
    const float lw = text_width("LT", 0.62f * gs) + 0.7f * gs;
    button_glyph(cv, "LT", std::max(sx0 + 8.0f, lx0 - lw), formY0 + 0.5f * formS, gs, rgb(0x3A4A70));
    button_glyph(cv, "RT", std::min(sx1 - lw - 8.0f, rx0), formY0 + 0.5f * formS, gs, rgb(0x3A4A70));
  }
  const std::string name = spot.locked ? std::string("???") : caps(spot.name);
  words(name, scx, 0.71f * h, 0.082f * h, sx1 - sx0 - 0.04f * w, cream, rgb(0xFFC21F), 4.0f);
  float statsTop = 0.758f * h;
  if (spot.note != nullptr && spot.note[0] != '\0') {
    words(caps(spot.note), scx, 0.765f * h, 0.03f * h, sx1 - sx0 - 0.05f * w, rgb(0x9CC8FF), rgb(0x6A9AE0), 2.0f);
    statsTop += 0.03f * h;
  }
  if (spot.hasCard && !spot.locked) {
    // its card as bars, no numbers: 2 of 10 for the lowest, all 10 for the top
    static const char* const kStat[4] = {"SPEED", "SHOOT", "PASS", "TACKLE"};
    const float row = (bottom - 0.012f * h - statsTop) / 4.0f;
    const float labelX = sx0 + 0.025f * w, barX0 = sx0 + 0.135f * w, barX1 = sx1 - 0.025f * w;
    const float seg = (barX1 - barX0) / 10.0f;
    for (int i = 0; i < 4; ++i) {
      const float cy = statsTop + (i + 0.5f) * row;
      text_left(cv, kStat[i], labelX, cy, 0.72f * row, barX0 - labelX - 0.01f * w, soft, rgb(0xB8C2D6), 2.0f);
      const int lit = 2 + static_cast<int>(std::lround(8.0f * clamp01(spot.card[i])));
      for (int k = 0; k < 10; ++k) {
        fill_rounded(cv, barX0 + k * seg + 3.0f, cy - 0.3f * row, barX0 + (k + 1) * seg - 3.0f, cy + 0.3f * row, 6.0f,
                     k < lit ? gold : rgb(0x2A3550));
      }
    }
  }
  // the grid: this game's characters (a tile a family)
  const float gx0 = 0.412f * w, gx1 = w - m - 0.012f * w, gy0 = top, gy1 = bottom;
  const int cols = 5, rows = 3;
  const float cw = (gx1 - gx0) / cols, ch = (gy1 - gy0) / rows;
  for (int i = firstRow * cols; i < count && i < (firstRow + rows) * cols; ++i) {
    const SelectTile& t = tiles[i];
    const int c = i % cols, r = i / cols - firstRow;
    const float x0 = gx0 + c * cw + 8.0f, y0 = gy0 + r * ch + 8.0f, x1 = x0 + cw - 16.0f, y1 = y0 + ch - 16.0f;
    const bool on = i == cursor && !onTabs;
    const bool here = i == cursor;
    if (on) glow_disc(cv, 0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.75f * cw, rgb(0xFFD23F, 0.6f));
    fill_rounded(cv, x0, y0, x1, y1, 22.0f, on ? gold : (here ? rgb(0xB8C2D6) : mix(game, rgb(0x000000), 0.25f)));
    const float in = on ? 8.0f : 4.0f;
    fill_rounded(cv, x0 + in, y0 + in, x1 - in, y1 - in, 18.0f, on ? rgb(0x2B4690) : rgb(0x151C36));
    const float nameH = 0.2f * (y1 - y0);
    blit_fit(cv, t.rgba, t.w, t.h, x0 + 14.0f, y0 + 12.0f, x1 - x0 - 28.0f, y1 - y0 - nameH - 18.0f, true);
    words(caps(t.name), 0.5f * (x0 + x1), y1 - 0.55f * nameH, 0.62f * nameH, x1 - x0 - 24.0f, on ? cream : white,
          on ? rgb(0xFFC21F) : soft, 2.5f);
    if (t.forms > 1) {
      // how many forms it has: little dots at its top right
      for (int k = 0; k < t.forms && k < 6; ++k) {
        const float dx = x1 - 22.0f - k * 20.0f, dy = y0 + 22.0f;
        fill_rounded(cv, dx - 7.0f, dy - 7.0f, dx + 7.0f, dy + 7.0f, 7.0f, on ? rgb(0xFFF7B0) : gold);
      }
    }
    if (t.taken) {
      fill_rounded(cv, x0 + in, y0 + in, x1 - in, y1 - in, 18.0f, rgb(0x05070C, 0.62f));
      words("TAKEN", 0.5f * (x0 + x1), 0.5f * (y0 + y1) - 0.05f * ch, 0.16f * ch, x1 - x0 - 28.0f, rgb(0xFF8A80),
            rgb(0xE04A40), 2.0f);
    }
  }
  const int allRows = (count + cols - 1) / cols;
  if (allRows > rows) {
    // more than fits: a scroll bar at the right, the rows shown gold
    const float tx0 = w - m - 0.009f * w, tx1 = w - m - 0.001f * w, ty0 = gy0 + 8.0f, ty1 = gy1 - 8.0f;
    fill_rounded(cv, tx0, ty0, tx1, ty1, 0.5f * (tx1 - tx0), rgb(0x2A3550));
    const float span = ty1 - ty0;
    fill_rounded(cv, tx0, ty0 + span * firstRow / allRows, tx1, ty0 + span * (firstRow + rows) / allRows,
                 0.5f * (tx1 - tx0), gold);
  }
  // the foot: the title at the left, the buttons beside it
  float gsz = 0.048f * h, wordPx = 0.036f * h, gapIn = 0.012f * w, gapOut = 0.03f * w;
  const float fy = 0.955f * h;
  if (title != nullptr && title[0] != '\0') text_left(cv, caps(title), m + 0.01f * w, fy, 0.036f * h, 0.3f * w, cream, gold, 2.5f);
  auto row_width = [&]() {
    float total = 0.0f;
    for (int i = 0; i < hintCount; ++i) {
      const float gw = std::strlen(hints[i].glyph) > 1 ? std::max(gsz, text_width(hints[i].glyph, 0.62f * gsz) + 0.7f * gsz) : gsz;
      total += gw + gapIn + text_width(caps(hints[i].words).c_str(), wordPx) + gapOut;
    }
    return total;
  };
  // r187 crash fix (r185: an original's "UNIFORMS" with the franchise pick's "NO BACKUP" / "ALL THE SAME" ran the row
  // past the panel's edge: the last hint's room (w - hx) went negative, text_left scaled its size negative and
  // text_mask's vector got a negative size: std::length_error, the app gone). The row now shrinks to fit its room.
  float total = row_width();
  const float rowX0 = 0.34f * w, rowRoom = w - m - 0.01f * w - rowX0;
  if (total > rowRoom && total > 0.0f) {
    const float k = rowRoom / total;
    gsz *= k;
    wordPx *= k;
    gapIn *= k;
    gapOut *= k;
    total = row_width();
  }
  float hx = std::max(rowX0, 0.62f * w - 0.5f * total);
  for (int i = 0; i < hintCount; ++i) {
    hx += button_glyph(cv, hints[i].glyph, hx, fy, gsz, rgb(hints[i].rgb)) + gapIn;
    const std::string s = caps(hints[i].words);
    const float room = w - m - hx;
    if (room <= 1.0f) break;  // (never a negative room: nothing more fits)
    text_left(cv, s, hx, fy, wordPx, room, rgb(0xE6ECF5), rgb(0xB8C2D6), 2.0f);
    hx += std::min(room, text_width(s.c_str(), wordPx)) + gapOut;
  }
  cv.write(rgba);
}

// r113: see xr_text.h. r236: in the face set for this thread (set_measure_face)
float face_text_width(const std::string& text, float px, int face, float track) {
  if (face == kFaceBroadcast || face == kFaceBroadcastUp) {
    const RasterFace rf(face, track);
    return text_width(caps(text.c_str()).c_str(), px * kBroadcastPx) * kBroadcastXs;
  }
  const RasterFace rf(kFaceLucky, track);
  return text_width(caps(text.c_str()).c_str(), px);
}
float board_text_width(const std::string& text, float px) { return face_text_width(text, px, t_measure); }
void set_measure_face(int face) { t_measure = face; }
int measure_face() { return t_measure; }

// r236: a box's shape - rounded (radius), its corners cut (cut, cuts), leaning (lean: a parallelogram) - signed distance (< 0 inside)
static float piece_sd(const BoardPiece& p, float x, float y) {
  float px = x;
  if (p.lean != 0.0f) {  // (sheared back to the upright box: the top edge sits `lean` right of the bottom)
    const float h = std::max(1.0f, p.y1 - p.y0);
    px -= p.lean * (p.y1 - y) / h;
  }
  float sd = rounded_rect_sd(px, y, p.x0, p.y0, p.x1, p.y1, p.radius);
  if (p.cut > 0.0f) {
    constexpr float kR2 = 0.70710678f;
    if (p.cuts & 1) sd = std::max(sd, (p.cut - ((px - p.x0) + (y - p.y0))) * kR2);
    if (p.cuts & 2) sd = std::max(sd, (p.cut - ((p.x1 - px) + (y - p.y0))) * kR2);
    if (p.cuts & 4) sd = std::max(sd, (p.cut - ((p.x1 - px) + (p.y1 - y))) * kR2);
    if (p.cuts & 8) sd = std::max(sd, (p.cut - ((px - p.x0) + (p.y1 - y))) * kR2);
  }
  return sd;
}

// r205: the rows a piece can touch (conservative: a text's mask is its px tall + its outline's pad round it, wherever its ink
// sits, + its shadow; a turned / leaning text its half-diagonal) - xrscreen's dirty bands and board_band's skipping
void piece_rows(const BoardPiece& p, float* y0, float* y1) {
  switch (p.kind) {
    case BoardPiece::kText:
    case BoardPiece::kTextLeft:
    case BoardPiece::kTextRight: {
      const bool rob = p.face == kFaceBroadcast || p.face == kFaceBroadcastUp;  // r236
      const RasterFace rf(rob ? p.face : kFaceLucky, p.track);
      const float px = std::max(0.0f, p.y1) * (rob ? kBroadcastPx : 1.0f);
      const float pad = std::ceil(std::max(0.0f, p.outline)) + 3.0f;
      const float sh = std::fabs(p.shadow) + 2.0f;
      float half = px + 2.0f * pad + sh + 4.0f;
      if (p.slant != 0.0f || p.angle != 0.0f) {
        const float xs = p.xscale > 0.05f ? p.xscale : 1.0f;
        const float wd = (p.x1 > 0.0f ? std::min(p.x1, text_width(caps(p.text.c_str()).c_str(), px) * xs)
                                      : text_width(caps(p.text.c_str()).c_str(), px) * xs) +
                         std::fabs(p.slant) * (px + 2.0f * pad);
        half = 0.5f * std::sqrt(wd * wd + (px + 2.0f * pad) * (px + 2.0f * pad)) + sh + 6.0f;
      }
      *y0 = p.y0 - half;
      *y1 = p.y0 + half;
      return;
    }
    case BoardPiece::kPoly: {
      float a = 1e9f, b = -1e9f;
      for (size_t i = 1; i < p.pts.size(); i += 2) a = std::min(a, p.pts[i]), b = std::max(b, p.pts[i]);
      *y0 = a - 3.0f;
      *y1 = b + 3.0f;
      return;
    }
    default:
      *y0 = std::min(p.y0, p.y1) - 3.0f;
      *y1 = std::max(p.y0, p.y1) + 3.0f;
      return;
  }
}

static void board_into(Canvas& cv, const std::vector<BoardPiece>& pieces, bool band) {
  for (const BoardPiece& p : pieces) {
    if (band) {  // r205: a band - pieces nowhere near its rows are skipped (a text's mask costs)
      float a = 0.0f, b = 0.0f;
      piece_rows(p, &a, &b);
      if (b < static_cast<float>(cv.top()) - 1.0f || a > static_cast<float>(cv.bot()) + 1.0f) continue;
    }
    const Color c = rgb(p.rgb, p.alpha);
    switch (p.kind) {
      case BoardPiece::kBox: {
        const bool shaped = p.cut > 0.0f || p.lean != 0.0f;  // r236: cut corners / a parallelogram
        if (p.rgb2 == 0 && p.alpha2 < 0.0f && !shaped) {
          fill_rounded(cv, p.x0, p.y0, p.x1, p.y1, p.radius, c);
          break;
        }
        // r131: alpha2 >= 0 fades to it (rgb2 0 then: the same colour)
        const Color c2 = rgb(p.rgb2 != 0 ? p.rgb2 : p.rgb, p.alpha2 >= 0.0f ? p.alpha2 : p.alpha);
        const bool fade = p.rgb2 != 0 || p.alpha2 >= 0.0f;
        const float span = std::max(1.0f, p.y1 - p.y0);
        const int xa = std::max(0, static_cast<int>(std::floor(p.x0 + std::min(0.0f, p.lean))) - 1);
        const int xb = std::min(cv.w - 1, static_cast<int>(p.x1 + std::max(0.0f, p.lean)) + 1);
        for (int y = std::max(cv.top(), static_cast<int>(p.y0) - 1); y <= std::min(cv.bot() - 1, static_cast<int>(p.y1) + 1); ++y) {
          const Color f = fade ? mix(c, c2, clamp01((y - p.y0) / span)) : c;
          for (int x = xa; x <= xb; ++x)
            cv.over(x, y, f, 0.5f - (shaped ? piece_sd(p, x + 0.5f, y + 0.5f) : rounded_rect_sd(x + 0.5f, y + 0.5f, p.x0, p.y0, p.x1, p.y1, p.radius)));
        }
        break;
      }
      case BoardPiece::kRing: {
        const float wd = std::max(1.0f, p.outline);
        const int xa = std::max(0, static_cast<int>(std::floor(p.x0 + std::min(0.0f, p.lean))) - 2);
        const int xb = std::min(cv.w - 1, static_cast<int>(p.x1 + std::max(0.0f, p.lean)) + 2);
        for (int y = std::max(cv.top(), static_cast<int>(p.y0) - 2); y <= std::min(cv.bot() - 1, static_cast<int>(p.y1) + 2); ++y)
          for (int x = xa; x <= xb; ++x) {
            const float sd = piece_sd(p, x + 0.5f, y + 0.5f);
            cv.over(x, y, c, std::min(0.5f - sd, 0.5f + sd + wd));
          }
        break;
      }
      case BoardPiece::kStripes: {  // r236: the broadcast package's diagonal sweep (one pass over the box)
        const float period = std::max(4.0f, p.radius), half = 0.5f * std::max(0.5f, p.outline);
        const float nrm = 1.0f / std::sqrt(1.0f + p.slant * p.slant);
        for (int y = std::max(cv.top(), static_cast<int>(p.y0)); y <= std::min(cv.bot() - 1, static_cast<int>(p.y1)); ++y)
          for (int x = std::max(0, static_cast<int>(p.x0)); x <= std::min(cv.w - 1, static_cast<int>(p.x1)); ++x) {
            float u = (x + 0.5f + p.slant * (y + 0.5f)) / period;
            u -= std::floor(u);
            const float d = std::fabs(u - 0.5f) * period * nrm;  // (px to the line in the middle of each period)
            cv.over(x, y, c, half + 0.5f - d);
          }
        break;
      }
      case BoardPiece::kGlow: {
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        const float rx = std::max(1.0f, 0.5f * (p.x1 - p.x0)), ry = std::max(1.0f, 0.5f * (p.y1 - p.y0));
        for (int y = std::max(cv.top(), static_cast<int>(p.y0)); y <= std::min(cv.bot() - 1, static_cast<int>(p.y1)); ++y)
          for (int x = static_cast<int>(p.x0); x <= static_cast<int>(p.x1); ++x) {
            const float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (d < 1.0f) cv.over(x, y, c, (1.0f - d) * (1.0f - d));
          }
        break;
      }
      case BoardPiece::kImage:
        if (p.trim || p.crop || p.flip || p.duo) {  // r131
          blit_ex(cv, p.img, p.iw, p.ih, p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0, p.bottom, p.crop, p.focusX, p.focusY,
                  p.trim, p.flip, p.duo, rgb(p.rgb), rgb(p.rgb2), p.silhouette ? c : Color{0.0f, 0.0f, 0.0f, 0.0f},
                  p.alpha);
          break;
        }
        blit_fit(cv, p.img, p.iw, p.ih, p.x0, p.y0, p.x1 - p.x0, p.y1 - p.y0, p.bottom,
                 p.silhouette ? c : Color{0.0f, 0.0f, 0.0f, 0.0f}, p.alpha);
        break;
      case BoardPiece::kText:
      case BoardPiece::kTextLeft:
      case BoardPiece::kTextRight: {
        if (p.text.empty()) break;
        const std::string s = caps(p.text.c_str());
        const bool rob = p.face == kFaceBroadcast || p.face == kFaceBroadcastUp;  // r236: the hub's face
        const RasterFace rf(rob ? p.face : kFaceLucky, p.track);
        const float xs = (p.xscale > 0.05f ? p.xscale : 1.0f) * (rob ? kBroadcastXs : 1.0f);  // r131: condensed
        float px = p.y1 * (rob ? kBroadcastPx : 1.0f);
        const float wide = text_width(s.c_str(), px) * xs;
        if (p.x1 > 0.0f && wide > p.x1) px *= p.x1 / wide;
        const float wd = text_width(s.c_str(), px) * xs;
        const float cx = p.kind == BoardPiece::kText ? p.x0 : (p.kind == BoardPiece::kTextLeft ? p.x0 + 0.5f * wd : p.x0 - 0.5f * wd);
        const Color bottom = p.rgb2 != 0 ? rgb(p.rgb2, p.alpha) : c;
        const Color rim = p.outline > 0.0f ? rgb(p.outlineRgb, p.alpha) : c;
        const Color shade = p.shadow > 0.0f ? rgb(p.shadowRgb, p.shadowA * p.alpha) : rgb(0x000000, 0.0f);
        const TextStyle style{px, c, bottom, rim, p.outline, shade, p.shadow, p.shadow};
        if (p.slant != 0.0f || p.angle != 0.0f || xs != 1.0f) {
          draw_text_affine(cv, s.c_str(), cx, p.y0, style, xs, p.slant, p.angle);
        } else {
          draw_text(cv, s.c_str(), cx, p.y0, style);
        }
        break;
      }
      case BoardPiece::kPoly: {  // r131: a convex polygon, edges smoothed
        const size_t n = p.pts.size() / 2;
        if (n < 3) break;
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f, area = 0.0f;
        for (size_t i = 0; i < n; ++i) {
          const float ax = p.pts[2 * i], ay = p.pts[2 * i + 1];
          const float bx = p.pts[2 * ((i + 1) % n)], by = p.pts[2 * ((i + 1) % n) + 1];
          minX = std::min(minX, ax);
          maxX = std::max(maxX, ax);
          minY = std::min(minY, ay);
          maxY = std::max(maxY, ay);
          area += ax * by - bx * ay;
        }
        const float sgn = area > 0.0f ? 1.0f : -1.0f;
        std::vector<float> edge(n * 3);  // each edge's outward normal and offset
        for (size_t i = 0; i < n; ++i) {
          const float ax = p.pts[2 * i], ay = p.pts[2 * i + 1];
          const float ex = p.pts[2 * ((i + 1) % n)] - ax, ey = p.pts[2 * ((i + 1) % n) + 1] - ay;
          const float len = std::max(1e-6f, std::sqrt(ex * ex + ey * ey));
          edge[3 * i] = sgn * ey / len;
          edge[3 * i + 1] = -sgn * ex / len;
          edge[3 * i + 2] = ax * edge[3 * i] + ay * edge[3 * i + 1];
        }
        for (int y = std::max(cv.top(), static_cast<int>(std::floor(minY)) - 1); y <= std::min(cv.bot() - 1, static_cast<int>(maxY) + 1); ++y)
          for (int x = std::max(0, static_cast<int>(std::floor(minX)) - 1); x <= std::min(cv.w - 1, static_cast<int>(maxX) + 1); ++x) {
            float sd = -1e9f;
            for (size_t i = 0; i < n; ++i)
              sd = std::max(sd, (x + 0.5f) * edge[3 * i] + (y + 0.5f) * edge[3 * i + 1] - edge[3 * i + 2]);
            cv.over(x, y, c, 0.5f - sd);
          }
        break;
      }
      case BoardPiece::kBurst: {  // r131: spikes round the box's middle
        const float cx = 0.5f * (p.x0 + p.x1), cy = 0.5f * (p.y0 + p.y1);
        const float rx = std::max(1.0f, 0.5f * (p.x1 - p.x0)), ry = std::max(1.0f, 0.5f * (p.y1 - p.y0));
        const float rmin = std::min(rx, ry), seg = 6.2831853f / std::max(3, p.points), rot = p.angle * 0.017453293f;
        for (int y = std::max(cv.top(), static_cast<int>(std::floor(p.y0)) - 1); y <= std::min(cv.bot() - 1, static_cast<int>(p.y1) + 1); ++y)
          for (int x = std::max(0, static_cast<int>(std::floor(p.x0)) - 1); x <= std::min(cv.w - 1, static_cast<int>(p.x1) + 1); ++x) {
            const float u = (x + 0.5f - cx) / rx, v = (y + 0.5f - cy) / ry;
            const float r = std::sqrt(u * u + v * v);
            float f = (std::atan2(v, u) - rot) / seg;
            f -= std::floor(f);
            const float R = p.inner + (1.0f - p.inner) * std::fabs(1.0f - 2.0f * f);
            cv.over(x, y, c, (R - r) * rmin + 0.5f);
          }
        break;
      }
      case BoardPiece::kDots: {  // r131: halftone, the dots growing (or shrinking) top to bottom
        const float cell = std::max(2.0f, p.radius), span = std::max(1.0f, p.y1 - p.y0);
        for (int y = std::max(cv.top(), static_cast<int>(std::floor(p.y0))); y <= std::min(cv.bot() - 1, static_cast<int>(p.y1)); ++y) {
          const float py = y + 0.5f;
          if (py < p.y0 || py > p.y1) continue;
          const float ccy = p.y0 + (std::floor((py - p.y0) / cell) + 0.5f) * cell;
          const float rr = cell * (p.dot0 + (p.dot1 - p.dot0) * clamp01((py - p.y0) / span));
          for (int x = std::max(0, static_cast<int>(std::floor(p.x0))); x <= std::min(cv.w - 1, static_cast<int>(p.x1)); ++x) {
            const float px = x + 0.5f;
            if (px < p.x0 || px > p.x1) continue;
            const float ccx = p.x0 + (std::floor((px - p.x0) / cell) + 0.5f) * cell;
            const float d = std::sqrt((px - ccx) * (px - ccx) + (py - ccy) * (py - ccy));
            cv.over(x, y, c, rr - d + 0.5f);
          }
        }
        break;
      }
    }
  }
}

void board(uint8_t* rgba, int w, int h, const std::vector<BoardPiece>& pieces) {
  Canvas cv(w, h);
  board_into(cv, pieces, false);
  cv.write(rgba);
}

// r205: rows y0 .. y1 of the w x h picture only (rgba: the WHOLE picture's buffer; those rows written, the rest untouched) -
// exactly what board() puts there (the same pieces, in order, from clear), for the pieces that reach them
void board_band(uint8_t* rgba, int w, int h, const std::vector<BoardPiece>& pieces, int y0, int y1) {
  y0 = std::max(0, y0);
  y1 = std::min(h, y1);
  if (y1 <= y0 || w <= 0) return;
  Canvas cv(w, y1 - y0, y0);
  board_into(cv, pieces, true);
  cv.write(rgba + static_cast<size_t>(y0) * w * 4);
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
                     int options, const EvoImage* all) {
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
  } else if (stage == 0 && options > 2 && all != nullptr) {
    // r130: a split three to five ways (Tyrogue, Eevee): every form in a row, the chosen one framed in gold
    const int n = std::min(options, 5);
    const float gap = 0.02f * w, bw = (0.92f * w - gap * (n - 1)) / n, bh = picH * std::min(1.0f, 1.6f * bw / picW);
    const float by = picY + 0.5f * (picH - bh);
    for (int k = 0; k < n; ++k) {
      const float bx = 0.04f * w + k * (bw + gap);
      if (k == choice) fill_rounded(cv, bx - 6.0f, by - 6.0f, bx + bw + 6.0f, by + bh + 6.0f, 18.0f, rgb(0xFFD23F, 0.85f));
      fill_rounded(cv, bx, by, bx + bw, by + bh, 14.0f, rgb(k == choice ? 0x2A3F7A : 0x18224A, 0.95f));
      blit(all[k], bx + 10.0f, by + 10.0f, bw - 20.0f, bh - 20.0f, 0.0f, 1.0f);
    }
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

// r230 (LANCINI EXQUISITE): Karla, wide-tracked - the mockups' (tools/outside/exquisite229 xkit.text_mask) spacing: `track` in their units
// (the gap between letters = track x 0.45 % of the size); `px` the em's height (PIL's font size). Upper / lower case as given.
// r230: a UTF-8 string's code points (Karla has Latin-1: the middle dot)
std::vector<int> utf8_cps(const std::string& s) {
  std::vector<int> o;
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) o.push_back(c), ++i;
    else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) o.push_back(((c & 0x1F) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3F)), i += 2;
    else if ((c & 0xF0) == 0xE0 && i + 2 < s.size())
      o.push_back(((c & 0x0F) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3F)), i += 3;
    else o.push_back(' '), ++i;
  }
  return o;
}
float karla_width(const std::string& text, float px, float track) {  if (!(px > 0.0f) || !(px < 16384.0f)) return 0.0f;
  stbtt_fontinfo& f = karla_font();
  const float scale = stbtt_ScaleForMappingEmToPixels(&f, px), tr = track * 0.45f * px / 100.0f;
  float x = 0.0f;
  const std::vector<int> cps = utf8_cps(text);
  for (size_t i = 0; i < cps.size(); ++i) {
    const int cp = cps[i];
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f, cp, &advance, &lsb);
    x += advance * scale;
    if (i + 1 < cps.size()) x += scale * stbtt_GetCodepointKernAdvance(&f, cp, cps[i + 1]) + tr;
  }
  return x;
}
KarlaMask karla_mask(const std::string& text, float px, float track, int pad) {
  KarlaMask m;
  if (!(px > 0.0f) || !(px < 4096.0f) || pad < 0 || pad > 2048 || text.empty()) return m;
  stbtt_fontinfo& f = karla_font();
  const float scale = stbtt_ScaleForMappingEmToPixels(&f, px), tr = track * 0.45f * px / 100.0f;
  int ascent = 0, descent = 0, gap = 0;
  stbtt_GetFontVMetrics(&f, &ascent, &descent, &gap);
  const float tw = karla_width(text, px, track);
  if (!(tw >= 0.0f) || !(tw < 16384.0f)) return m;
  m.pad = pad;
  m.width = tw;
  m.w = static_cast<int>(std::ceil(tw)) + 2 * pad;
  m.h = static_cast<int>(std::ceil((ascent - descent) * scale)) + 2 * pad;
  m.base = pad + std::round(ascent * scale);
  int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;
  stbtt_GetCodepointBox(&f, 'H', &cx0, &cy0, &cx1, &cy1);
  m.capTop = m.base - cy1 * scale;
  m.capBot = m.base - cy0 * scale;
  m.a.assign(static_cast<size_t>(m.w) * m.h, 0.0f);
  float x = static_cast<float>(pad);
  std::vector<unsigned char> glyph;
  const std::vector<int> cps = utf8_cps(text);
  for (size_t i = 0; i < cps.size(); ++i) {
    const int cp = cps[i];
    int advance = 0, lsb = 0;
    stbtt_GetCodepointHMetrics(&f, cp, &advance, &lsb);
    const float shift = x - std::floor(x);
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    stbtt_GetCodepointBitmapBoxSubpixel(&f, cp, scale, scale, shift, 0.0f, &x0, &y0, &x1, &y1);
    const int gw = x1 - x0, gh = y1 - y0;
    if (gw > 0 && gh > 0) {
      glyph.assign(static_cast<size_t>(gw) * gh, 0);
      stbtt_MakeCodepointBitmapSubpixel(&f, glyph.data(), gw, gh, gw, scale, scale, shift, 0.0f, cp);
      for (int yy = 0; yy < gh; ++yy)
        for (int xx = 0; xx < gw; ++xx) {
          const int mx = static_cast<int>(std::floor(x)) + x0 + xx, my = static_cast<int>(m.base) + y0 + yy;
          if (mx < 0 || my < 0 || mx >= m.w || my >= m.h) continue;
          float& d = m.a[static_cast<size_t>(my) * m.w + mx];
          d = std::max(d, glyph[static_cast<size_t>(yy) * gw + xx] / 255.0f);
        }
    }
    x += advance * scale;
    if (i + 1 < cps.size()) x += scale * stbtt_GetCodepointKernAdvance(&f, cp, cps[i + 1]) + tr;
  }
  return m;
}

}  // namespace xrtext