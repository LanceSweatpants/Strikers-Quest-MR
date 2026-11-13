// strikers-quest r221 (Lance 11-12: "franchise games at a RANDOM stadium out of all 7"): where a franchise game is played.
// Pure (no STL, no allocation): shared by franchise.cpp and the PC proof (tools/outside/stadium221).
//
// Every game of a season gets one of the 7 stadiums (0-6, none locked, not in unlock order), from the career's seed, the
// season, the game's week, its index in the career's game list and its clubs only - so a replayed / rematched game, the
// co-op friend's copy and an online league's other headset all get the same one. Games are taken in week order (the
// list's order inside a week); a club never plays at the stadium of its own last game when another one is free (both
// clubs' last stadiums are left out; with both out 5 are left, so that is always possible). A game added later (a playoff
// game) never changes an earlier game's stadium.
#pragma once
#include <stdint.h>

namespace frstadium {

constexpr int kCount = 7;
constexpr int kMaxClubs = 64;

inline uint32_t mix(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

// One game's draw (before the no-repeat rule).
inline uint32_t draw(uint32_t seed, int season, int week, int index) {
  uint32_t h = mix(seed ^ 0x5A17D1A5u);
  h = mix(h ^ (static_cast<uint32_t>(season) * 0x9E3779B9u));
  h = mix(h ^ (static_cast<uint32_t>(week) * 0x85EBCA6Bu));
  h = mix(h ^ (static_cast<uint32_t>(index) * 0xC2B2AE35u));
  return h;
}

// out[i] = game i's stadium. order: n ints of scratch. Clubs outside 0..kMaxClubs-1 are ignored by the no-repeat rule.
inline void pick_all(uint32_t seed, int season, int n, const int* week, const int* home, const int* away, int8_t* out,
                     int* order) {
  for (int i = 0; i < n; ++i) order[i] = i;
  for (int i = 1; i < n; ++i) {  // stable: by week, then the list's order
    const int v = order[i];
    int j = i - 1;
    while (j >= 0 && week[order[j]] > week[v]) {
      order[j + 1] = order[j];
      --j;
    }
    order[j + 1] = v;
  }
  int8_t last[kMaxClubs];
  for (int k = 0; k < kMaxClubs; ++k) last[k] = -1;
  for (int o = 0; o < n; ++o) {
    const int i = order[o];
    const int a = home[i] >= 0 && home[i] < kMaxClubs ? last[home[i]] : -1;
    const int b = away[i] >= 0 && away[i] < kMaxClubs ? last[away[i]] : -1;
    int free[kCount];
    int nf = 0;
    for (int s = 0; s < kCount; ++s)
      if (s != a && s != b) free[nf++] = s;
    const uint32_t h = draw(seed, season, week[i], i);
    const int s = nf > 0 ? free[h % static_cast<uint32_t>(nf)] : static_cast<int>(h % kCount);
    out[i] = static_cast<int8_t>(s);
    if (home[i] >= 0 && home[i] < kMaxClubs) last[home[i]] = static_cast<int8_t>(s);
    if (away[i] >= 0 && away[i] < kMaxClubs) last[away[i]] = static_cast<int8_t>(s);
  }
}

}  // namespace frstadium
