// strikers-quest r208 (Lance 11-10: "dont cap the ticket wins bc when i make enough, i'll wanna make 1,000,000 WAGERS"): BIT COINS
// (r216: were TIZM COINS - the name only; nothing saved changed) are 64-bit. Every balance, stake, payout, price paid and total is a Coins (long long); every change to a balance saturates at
// kMax (it never wraps: r199-r207's int balance would have gone negative past 2,147,483,647). kMax is under 2^53, so a double
// (the parlay's multiplier maths) holds every amount exactly. Pure header (the PC proofs include it too).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>

namespace xrcoin {

using Coins = long long;
// 999,999,999,999,999 (just under a quadrillion): the most a balance / a payout can be
constexpr Coins kMax = 999999999999999LL;

inline Coins clamp(Coins v) { return v < 0 ? 0 : (v > kMax ? kMax : v); }
// a + b, each first held to -kMax..kMax (never wraps: the result held to -kMax..kMax)
inline Coins add(Coins a, Coins b) {
  a = a > kMax ? kMax : (a < -kMax ? -kMax : a);
  b = b > kMax ? kMax : (b < -kMax ? -kMax : b);
  const Coins s = a + b;  // (|a|, |b| <= kMax ~ 2^50: no overflow here)
  return s > kMax ? kMax : (s < -kMax ? -kMax : s);
}
// a whole number of coins from a double: floored, NaN / negative 0, past kMax kMax
inline Coins from_double(double d) {
  if (!(d > 0.0)) return 0;
  if (d >= static_cast<double>(kMax)) return kMax;
  return static_cast<Coins>(std::floor(d));
}
// a coin amount from text (a save's field): strtoll (saturating, not UB as sscanf's %lld is past LLONG_MAX), held to -kMax..kMax;
// *end past it (null: not wanted). False: no number there.
inline bool parse(const char* s, Coins* out, const char** end = nullptr) {
  if (s == nullptr) return false;
  char* e = nullptr;
  const long long v = std::strtoll(s, &e, 10);
  if (e == s) return false;
  *out = v > kMax ? kMax : (v < -kMax ? -kMax : v);
  if (end != nullptr) *end = e;
  return true;
}
// "1,234,567,890"
inline std::string text(Coins n) {
  const unsigned long long u = n < 0 ? 0ULL - static_cast<unsigned long long>(n) : static_cast<unsigned long long>(n);
  const std::string d = std::to_string(u);
  std::string o;
  o.reserve(d.size() + d.size() / 3 + 1);
  for (size_t i = 0; i < d.size(); ++i) {
    if (i > 0 && (d.size() - i) % 3 == 0) o += ',';
    o += d[i];
  }
  return n < 0 ? "-" + o : o;
}
// "4.09T" / "12.3M" / "999K": three figures, rounded DOWN (a balance never reads as more than it is), trailing zeros dropped
// ("1M", "1.5B"); under 1,000 the number itself
inline std::string compact(Coins n) {
  const bool neg = n < 0;
  const unsigned long long u = neg ? 0ULL - static_cast<unsigned long long>(n) : static_cast<unsigned long long>(n);
  if (u < 1000ULL) return (neg ? "-" : "") + std::to_string(u);
  static const char* const kUnit[6] = {"K", "M", "B", "T", "Q", "QQ"};
  unsigned long long div = 1000ULL;
  int k = 0;
  while (k < 5 && u / div >= 1000ULL) div *= 1000ULL, ++k;
  const unsigned long long whole = u / div;                  // 1..999
  const int dec = whole >= 100 ? 0 : (whole >= 10 ? 1 : 2);  // (three figures)
  unsigned long long scale = 1;
  for (int i = 0; i < dec; ++i) scale *= 10ULL;
  const unsigned long long frac = (u % div) / (div / scale);  // floored
  std::string s = std::to_string(whole);
  if (dec > 0 && frac > 0) {
    std::string f = std::to_string(frac);
    while (static_cast<int>(f.size()) < dec) f = "0" + f;
    while (!f.empty() && f.back() == '0') f.pop_back();
    s += "." + f;
  }
  return (neg ? "-" : "") + s + kUnit[k];
}
// text(n) when it is at most maxChars long (commas counted), else compact(n): for the tight spots (a chip, a column, a badge)
inline std::string fit(Coins n, size_t maxChars) {
  std::string t = text(n);
  return t.size() <= maxChars ? t : compact(n);
}

// ---- r216 (Lance 11-11 REBRAND: "TIZM COINS" -> "BIT COINS" - 8-bit games + the crypto): the coin's face is an 8-bit B with the
// bitcoin's two strokes through its top + bottom (was a "T"). 7 x 9 pixels; tools/outside/rebrand216/art216.py BIT_B (the
// sportsbook cabinet's baked coins) is the same grid. Drawn as rectangles (whole runs of a row at once: few pieces).
constexpr const char* kBitB[9] = {".X.X...", "XXXXXX.", ".XX..XX", ".XX..XX", ".XXXXX.", ".XX..XX", ".XX..XX", "XXXXXX.", ".X.X..."};
inline bool bit_b_on(int i, int j) { return i >= 0 && i < 7 && j >= 0 && j < 9 && kBitB[j][i] == 'X'; }
// The face of a coin of radius r at (cx, cy): box(x0, y0, x1, y1, rgb) draws a plain rectangle. Over the coin's gold face:
// the stepped pixel ring (where r199's smooth ring was), the B's drop shadow, the B, its bevel on the letter's outside edges
// (lit top / left, dark bottom / right) and a pixel glint; under r 22 px only the ring + the B (the rest would be sub-pixel).
template <class Box>
void bit_face(Box box, float cx, float cy, float r) {
  const float c = 0.104f * r;  // one pixel of the B
  const bool fine = r >= 22.0f;
  // the ring: the cells of a grid (c) whose middle lies on a circle 0.70 r, a row's run each side
  {
    const float rr = 0.70f * r;
    const int n = static_cast<int>(rr / c) + 2;
    for (int j = -n; j < n; ++j) {
      const float y = (static_cast<float>(j) + 0.5f) * c;
      int run0 = 0;
      bool in = false;
      for (int i = -n; i <= n; ++i) {
        const float x = (static_cast<float>(i) + 0.5f) * c;
        const bool on = i < n && std::fabs(std::sqrt(x * x + y * y) - rr) <= 0.55f * c;
        if (on && !in) run0 = i, in = true;
        if (!on && in) {
          box(cx + run0 * c, cy + j * c, cx + i * c, cy + (j + 1) * c, 0xB8860Bu);
          in = false;
        }
      }
    }
  }
  const float gx = cx - 3.5f * c, gy = cy - 4.5f * c;  // the B's top-left
  auto runs = [&](float dx, float dy, uint32_t rgb) {  // the B's pixels, a row's runs at a time
    for (int j = 0; j < 9; ++j)
      for (int i = 0; i < 7;) {
        if (!bit_b_on(i, j)) { ++i; continue; }
        int e = i;
        while (bit_b_on(e, j)) ++e;
        box(gx + i * c + dx, gy + j * c + dy, gx + e * c + dx, gy + (j + 1) * c + dy, rgb);
        i = e;
      }
  };
  if (fine) runs(0.35f * c, 0.35f * c, 0x966200u);  // the drop shadow
  runs(0.0f, 0.0f, 0x704800u);
  if (!fine) return;
  const float b = 0.2f * c;
  for (int j = 0; j < 9; ++j)
    for (int i = 0; i < 7; ++i) {
      if (!bit_b_on(i, j)) continue;
      const float x0 = gx + i * c, y0 = gy + j * c;
      if (!bit_b_on(i, j - 1)) box(x0, y0, x0 + c, y0 + b, 0xB88414u);
      if (!bit_b_on(i - 1, j)) box(x0, y0, x0 + b, y0 + c, 0xB88414u);
      if (!bit_b_on(i, j + 1)) box(x0, y0 + c - b, x0 + c, y0 + c, 0x4A2C00u);
      if (!bit_b_on(i + 1, j)) box(x0 + c - b, y0, x0 + c, y0 + c, 0x4A2C00u);
    }
  // the glint: a plus of pixels on the face, top-left
  const float q = 0.7f * c, px = cx - 0.5f * r, py = cy - 0.5f * r;
  box(px - q, py, px + 2 * q, py + q, 0xFFF6C4u);
  box(px, py - q, px + q, py + 2 * q, 0xFFF6C4u);
  box(px, py, px + q, py + q, 0xFFFFFFu);
}

}  // namespace xrcoin
