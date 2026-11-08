// strikers-quest r160 round 2 (Lance: the 3D binder must flip EXACTLY like STRIKERS ILLUSTRATED): the page-turn rules
// both use - r131's magazine (xr_quest.cpp update_magazine / mag_flip / mag_tick) and the 3D binder (xr_cards.cpp
// room_binder) - in one place, so they can't drift apart:
//  - a page goes over the spine in kTurnS (0.45 s for a whole turn, smoothstep), its face to you;
//  - the triggers / A count only kOpenGuardNs (0.9 s) after it opens (a trigger squeezed through the match's end screens);
//  - a sweep of the FREE hand across the pages while the other hand holds it turns one: its pointer's tip near the pages
//    (5 cm behind .. 20 cm in front, 6 cm round them), 15 cm across within 0.3 s, right to left on / left to right back,
//    at most one every 0.45 s, and the hand coming back (the other way within 0.9 s) is no swipe - r131's guard against a
//    resting hand turning pages (r113's swipe took any 16 cm sweep near the paper and the cover turned by itself).
// Header only, no platform types.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xrturn {

constexpr float kTurnS = 0.45f;
constexpr uint64_t kOpenGuardNs = 900000000ull;
constexpr float kTrigOn = 0.55f, kTrigOff = 0.35f;  // a trigger's pull: pressed / let go again

// A turn's place now (from -> to over kTurnS x the way, smoothstep); *done when it's there.
inline float turn_ease(float from, float to, uint64_t startNs, uint64_t now, bool* done) {
  const float dur = std::max(0.08f, kTurnS * std::fabs(to - from));
  const float t = now > startNs ? static_cast<float>(static_cast<double>(now - startNs) * 1e-9) / dur : 0.0f;
  if (t >= 1.0f) {
    *done = true;
    return to;
  }
  *done = false;
  const float e = t * t * (3.0f - 2.0f * t);
  return from + (to - from) * e;
}

// The free hand's sweep across the pages.
struct Sweep {
  struct At {
    uint64_t ns = 0;
    float x = 0.0f, y = 0.0f, z = 0.0f;  // the free hand's pointer tip in the pages' own frame (m; x across, + right)
  } at[24];
  int next = 0, swipeDir = 0;
  uint64_t swipeNs = 0;
  void clear() {
    for (At& s : at) s.ns = 0;
  }
  void reset() {
    clear();
    next = 0;
    swipeDir = 0;
  }
  // One frame. `active`: a hand holds it, the other is free (tracked, its trigger let go); x, y, z its tip; near: on / just
  // over the pages (see above: the caller's test). +1 a page on, -1 back, 0 none.
  int step(bool active, bool near, float x, float y, float z, uint64_t now) {
    int swipe = 0;
    if (active) {
      at[next] = At{near ? now : 0, x, y, z};
      next = (next + 1) % 24;
      if (near && now - swipeNs > 450000000ull) {
        for (const At& s : at) {
          if (s.ns == 0 || now - s.ns > 300000000ull) continue;
          const float dx = x - s.x;
          if (std::fabs(dx) > 0.15f) {
            const int dir = dx < 0.0f ? 1 : -1;
            if (!(dir == -swipeDir && now - swipeNs < 900000000ull)) swipe = dir;
            break;
          }
        }
      }
    } else {
      clear();
    }
    if (swipe != 0) {
      swipeNs = now;
      swipeDir = swipe;
      clear();
    }
    return swipe;
  }
};

}  // namespace xrturn
