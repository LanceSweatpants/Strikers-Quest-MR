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

// r198 (Lance, 11-08: "not every rain game should be a thunderstorm and not every snow game a blizzard"): the tier odds,
// quick matches and franchise alike (xr_weather.cpp pick_tier; online the roll comes from the match's shared seed).
// rain: 0 DRIZZLE 35% / 1 RAIN 45% / 2 THUNDERSTORM 20%; snow: 0 FLURRY 65% / 1 BLIZZARD 35%.
inline int rain_tier(uint32_t roll) {
  const uint32_t r = roll % 100u;
  return r < 35u ? 0 : (r < 80u ? 1 : 2);
}
inline int snow_tier(uint32_t roll) { return roll % 100u < 65u ? 0 : 1; }

// r198: where a game sits in the season: 0 early (the first 30%), 1 mid, 2 late (the last 7 games, as r137)
inline int phase(int i, int n) {
  const int late0 = n > 7 ? n - 7 : 0;
  return i >= late0 ? 2 : (i >= n * 3 / 10 ? 1 : 0);
}

// r198: up to k games drawn by weight without replacement (w[i] 0 = not allowed), each gets `bit`; a drawn game's
// neighbours lose their weight for the same bit (no two rain games / snow games in a row when the draw can help it)
inline int draw(Rng& r, int* w, int n, int k, uint8_t* out, int bit, bool spread) {
  int got = 0;
  for (; got < k; got++) {
    int tot = 0;
    for (int i = 0; i < n; i++) tot += w[i];
    if (tot <= 0) break;
    int x = r.below(tot);
    int i = 0;
    for (; i < n - 1; i++) {
      if (x < w[i]) break;
      x -= w[i];
    }
    out[i] |= static_cast<uint8_t>(bit);
    w[i] = 0;
    if (spread) {
      if (i > 0) w[i - 1] = 0;
      if (i + 1 < n) w[i + 1] = 0;
    }
  }
  return got;
}

// r198: how streaky a season is (0 = fine): the same rain / snow back to back, three wet games running, more than 4
// clear games running, two plain night games running
inline int streaks(const uint8_t* o, int n) {
  int s = 0, clearRun = 0, nightRun = 0;
  for (int i = 0; i < n; i++) {
    const int wet = o[i] & (kRain | kSnow);
    if (i > 0 && (o[i] & kRain) && (o[i - 1] & kRain)) s += 10;
    if (i > 0 && (o[i] & kSnow) && (o[i - 1] & kSnow)) s += 10;
    if (i > 1 && wet && (o[i - 1] & (kRain | kSnow)) && (o[i - 2] & (kRain | kSnow))) s += 6;
    clearRun = o[i] == 0 ? clearRun + 1 : 0;
    if (clearRun > 4) s += 3;
    nightRun = o[i] == kNight ? nightRun + 1 : 0;
    if (nightRun > 1) s += 2;
  }
  return s;
}

// A club's regular season (n games in week order) -> out[i] = the weather bits of game i.
// r198 (Lance, 11-08: "I def want more variety to each 20-game season in terms of weather"): every season its own mix,
// from the career's seed + the season + the club only (the same on reload and on a league / co-op partner's headset):
//  - mostly clear games (about 11 of 20)
//  - 2-4 rain games (where it can rain: weighted to the early and middle weeks), 2-3 snow games (where it can snow: the
//    winter end of the season - mid-season sometimes, the last 7 games most), so some seasons are wetter, some snowier
//  - night: every derby (the second meeting with a division rival) + 1-2 more, and 0-2 of the rain / snow games under
//    the lights; the nemesis games and the playoffs are made night at match time (playoff() / franchise.cpp)
//  - no streaks: the least streaky of up to 48 tries by the season's dice (streaks() above: no rain / snow twice in a
//    row, no three wet games running, no more than 4 clear games running, no two plain night games running)
inline void season(uint32_t seed, int seasonNo, const Game* g, int n, uint8_t* out) {
  Rng r{hash3(seed, static_cast<uint32_t>(seasonNo), 0x57EA7E12u)};
  if (n > 64) {
    for (int i = 64; i < n; i++) out[i] = 0;
    n = 64;
  }
  // the season's mix
  const int wantRain = 2 + r.below(3);
  const int wantSnow = 2 + r.below(2);
  const int wantNight = 1 + r.below(2);
  const int wantWetNight = r.below(3);
  uint8_t best[64];
  uint8_t tryOut[64];
  int w[64];
  int bestScore = 1 << 30;
  for (int t = 0; t < 48 && bestScore > 0; t++) {
    for (int i = 0; i < n; i++) tryOut[i] = g[i].derby ? kNight : 0;
    // snow: the winter end of the season
    for (int i = 0; i < n; i++) {
      const int ph = phase(i, n);
      w[i] = can_snow(g[i].climate) ? (ph == 2 ? 4 : ph) : 0;
    }
    draw(r, w, n, wantSnow, tryOut, kSnow, true);
    // rain: the autumn weeks most, never on a snow game
    for (int i = 0; i < n; i++)
      w[i] = can_rain(g[i].climate) && (tryOut[i] & kSnow) == 0 ? (phase(i, n) == 2 ? 1 : 3) : 0;
    draw(r, w, n, wantRain, tryOut, kRain, true);
    // rain / snow under the lights (a derby on a wet game counts)
    int wetNights = 0;
    for (int i = 0; i < n; i++) {
      const bool wet = (tryOut[i] & (kRain | kSnow)) != 0;
      if (wet && (tryOut[i] & kNight)) wetNights++;
      w[i] = wet && (tryOut[i] & kNight) == 0 ? 1 : 0;
    }
    if (wantWetNight > wetNights) draw(r, w, n, wantWetNight - wetNights, tryOut, kNight, false);
    // a plain night game or two more
    for (int i = 0; i < n; i++) w[i] = tryOut[i] == 0 ? 1 : 0;
    draw(r, w, n, wantNight, tryOut, kNight, false);
    const int sc = streaks(tryOut, n);
    if (sc < bestScore) {
      bestScore = sc;
      for (int i = 0; i < n; i++) best[i] = tryOut[i];
    }
  }
  for (int i = 0; i < n; i++) out[i] = best[i];
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
