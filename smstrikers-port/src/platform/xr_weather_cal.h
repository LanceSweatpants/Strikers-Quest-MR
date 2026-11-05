// strikers-quest r137: the weather calendar (Lance, 11-03: "lets not make alllll late season games snowy, if theres 7
// games out of the season that are considered late season then like 3 of those should be snowy"; night games for big
// matchups). Pure arithmetic, no dependencies: xr_weather.cpp and franchise.cpp use it, and
// tools/outside/weather_calendar.py mirrors it line for line for the PC printout (keep the two the same).
//
// Looks only: nothing here reaches the game's physics, AI or ball.
#pragma once
#include <stdint.h>

namespace wxcal {

enum { kRain = 1, kSnow = 2, kNight = 4 };
// Where a game is played: an open stadium, a hot one (no snow: the Inferno clubs), a cold one (the Glacier clubs: a
// snowy playoff game can happen there), a covered one (the Forbidden Dome: no rain, no snow; night is fine).
enum { kClimateNormal = 0, kClimateHot = 1, kClimateCold = 2, kClimateCovered = 3 };

inline uint32_t mix(uint32_t h) {
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return h;
}
inline uint32_t hash3(uint32_t a, uint32_t b, uint32_t c) { return mix(mix(mix(a ^ 0x9E3779B9u) ^ b) ^ c); }

struct Rng {
  uint32_t s;
  uint32_t next() {
    s = mix(s + 0x9E3779B9u);
    return s;
  }
  int below(int n) { return n <= 0 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
};

inline bool can_rain(int climate) { return climate != kClimateCovered; }
inline bool can_snow(int climate) { return climate == kClimateNormal || climate == kClimateCold; }
inline int filter(int bits, int climate) {
  if (!can_rain(climate)) bits &= ~kRain;
  if (!can_snow(climate)) bits &= ~kSnow;
  return bits;
}

// One club's regular-season game: where it is played and whether it is a derby (the second meeting with a
// division rival).
struct Game {
  int climate;
  int derby;
};

// A club's regular season (n games in week order) -> out[i] = the weather bits of game i.
//  - late season = the last 7 games: exactly 3 snowy (where it can snow there), the 3 with the fewest back-to-back
//    pairs (none when the stadiums allow it), ties broken by the season's dice
//  - 2 or 3 rain games in the weeks before, never two in a row
//  - night: every derby, and 2 more picked by the dice (big matchups the schedule doesn't know: the nemesis games and
//    the playoffs are made night at match time, see playoff() / the nemesis check in franchise.cpp)
inline void season(uint32_t seed, int seasonNo, const Game* g, int n, uint8_t* out) {
  Rng r{hash3(seed, static_cast<uint32_t>(seasonNo), 0x57EA7E12u)};
  for (int i = 0; i < n; i++) out[i] = 0;
  const int late0 = n > 7 ? n - 7 : 0;
  // Snow.
  int elig[7];
  int ne = 0;
  for (int i = late0; i < n && ne < 7; i++)
    if (can_snow(g[i].climate)) elig[ne++] = i;
  if (ne <= 3) {
    for (int k = 0; k < ne; k++) out[elig[k]] |= kSnow;
  } else {
    int best[3] = {elig[0], elig[1], elig[2]};
    int bestAdj = 99;
    uint32_t bestKey = 0;
    for (int a = 0; a < ne; a++)
      for (int b = a + 1; b < ne; b++)
        for (int c = b + 1; c < ne; c++) {
          const int adj = (elig[b] - elig[a] == 1 ? 1 : 0) + (elig[c] - elig[b] == 1 ? 1 : 0);
          const uint32_t key = r.next();
          if (adj < bestAdj || (adj == bestAdj && key > bestKey)) {
            bestAdj = adj;
            bestKey = key;
            best[0] = elig[a];
            best[1] = elig[b];
            best[2] = elig[c];
          }
        }
    for (int k = 0; k < 3; k++) out[best[k]] |= kSnow;
  }
  // Rain: the weeks before the late season, shuffled, taken unless next to one already wet.
  int cand[64];
  int nc = 0;
  for (int i = 0; i < late0 && nc < 64; i++)
    if (can_rain(g[i].climate)) cand[nc++] = i;
  for (int i = nc - 1; i > 0; i--) {
    const int j = r.below(i + 1);
    const int t = cand[i];
    cand[i] = cand[j];
    cand[j] = t;
  }
  const int wantRain = 2 + r.below(2);
  int rained = 0;
  for (int k = 0; k < nc && rained < wantRain; k++) {
    const int i = cand[k];
    const bool nextToWet = (i > 0 && (out[i - 1] & kRain) != 0) || (i + 1 < n && (out[i + 1] & kRain) != 0);
    if (nextToWet) continue;
    out[i] |= kRain;
    rained++;
  }
  // Night: the derbies, then 2 more.
  for (int i = 0; i < n; i++)
    if (g[i].derby) out[i] |= kNight;
  nc = 0;
  for (int i = 0; i < n && nc < 64; i++)
    if ((out[i] & kNight) == 0) cand[nc++] = i;
  for (int i = nc - 1; i > 0; i--) {
    const int j = r.below(i + 1);
    const int t = cand[i];
    cand[i] = cand[j];
    cand[j] = t;
  }
  for (int k = 0; k < nc && k < 2; k++) out[cand[k]] |= kNight;
}

// A playoff game: always under the lights; at most one snowy playoff game a season - the first one a club plays at a
// cold home, when the season's coin says so.
inline int playoff(uint32_t seed, int seasonNo, int homeClimate, bool earlierColdHomeGame) {
  int bits = kNight;
  const bool coin = (hash3(seed, static_cast<uint32_t>(seasonNo), 0x5E50FF5u) & 1u) != 0;
  if (coin && homeClimate == kClimateCold && !earlierColdHomeGame) bits |= kSnow;
  return bits;
}

}  // namespace wxcal
