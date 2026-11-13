// strikers-quest r225 (Lance 11-12: the blizzard's snow lay as one hard-edged square on his kitchen floor though he has
// his kitchen AND his living room scanned): WHICH SCANNED ROOM YOU'RE IN. xr_scene.cpp builds every room the headset
// lists (Quest Settings > Physical space > Space setup: one per room) and this picks, every frame, the one whose floor
// outline you stand over - walking from the kitchen into the living room hands every room effect (the snow lying, the
// rain's puddles, the confetti's floor, the sportsbook machine / museum spots) to that room. Rules:
//   - a room counts only if its floor is where your real floor is (within 30 cm of the Guardian's floor when known; 30-60
//     cm only when nothing matches better; an old / moved scan 60 cm+ off never) and you're 0.3..2.6 m over it (another
//     storey: no);
//   - IN (your head over its floor outline) beats NEAR (within 1 m of it) beats nothing; the room in use wins a tie, then
//     the bigger floor (two scans of the same kitchen: the bigger, fuller one);
//   - a change sticks after 0.5 s (a room) / 1.5 s (none: you're in a room that isn't scanned) - no flicker in a doorway;
//     the very first pick is at once.
// Pure C++ with no library calls (xr_room.h's rule): tools/outside/roomsnow225 compiles it for the PC proof.
#pragma once

#include "xr_room.h"

namespace xrroompick {

struct Info {
  const xrroom::Room* room;  // null / !valid: skipped
  float area;                // its floor's area (m2)
};
enum { kOut = 0, kNear = 1, kIn = 2 };

// its floor against your floor: 2 it's your floor (within 30 cm of the Guardian's, or that isn't known), 1 close (30-60
// cm: a Guardian floor set a little off - used only when no room matches better), 0 not yours (60 cm+ off, another
// storey, or you're not standing / sitting over it)
inline int floor_ok(const xrroom::Room& r, float hy, float hint, bool hasHint) {
  const float fy = xrroom::floor_y(r);
  const float up = hy - fy;
  if (!(up > 0.3f && up < 2.6f)) return 0;
  if (!hasHint) return 2;
  const float off = fy > hint ? fy - hint : hint - fy;
  return off <= 0.3f ? 2 : (off <= 0.6f ? 1 : 0);
}
// your head over its floor outline (kIn), within 1 m of it (kNear), or not
inline int where(const xrroom::Room& r, float hx, float hz) {
  if (xrroom::inside(r, hx, hz)) return kIn;
  static const float dir[8][2] = {{1.0f, 0.0f},  {0.7071f, 0.7071f},  {0.0f, 1.0f},  {-0.7071f, 0.7071f},
                                  {-1.0f, 0.0f}, {-0.7071f, -0.7071f}, {0.0f, -1.0f}, {0.7071f, -0.7071f}};
  for (int k = 0; k < 8; ++k)
    if (xrroom::inside(r, hx + dir[k][0], hz + dir[k][1]) || xrroom::inside(r, hx + 0.5f * dir[k][0], hz + 0.5f * dir[k][1]))
      return kNear;
  return kOut;
}
// the best room for your head now (-1: none); *how: kIn / kNear / kOut
inline int choose(const Info* rooms, int n, int cur, float hx, float hy, float hz, float hint, bool hasHint, int* how) {
  int best = -1, bestS = 0, bestW = kOut;
  float bestA = -1.0f;
  for (int i = 0; i < n; ++i) {
    if (rooms[i].room == nullptr || !rooms[i].room->valid) continue;
    const int fl = floor_ok(*rooms[i].room, hy, hint, hasHint);
    if (fl == 0) continue;
    const int w = where(*rooms[i].room, hx, hz);
    if (w == kOut) continue;
    const int s = 2 * w + (fl == 2 ? 1 : 0);  // over + your floor 5, over + close 4, beside + your floor 3, beside + close 2
    bool better;
    if (s != bestS) better = s > bestS;
    else if (i == cur) better = true;
    else if (best == cur) better = false;
    else better = rooms[i].area > bestA;
    if (better) {
      best = i;
      bestS = s;
      bestW = w;
      bestA = rooms[i].area;
    }
  }
  if (how) *how = best < 0 ? kOut : bestW;
  return best;
}

struct Picker {
  int cur = -1;    // the room in use (-1: none)
  int want = -1;   // the one asked for, waiting out its time
  float wantT = 0.0f;
  bool first = true;
  int how = kOut;  // (the log / status: how the room in use was picked)
};
// one frame: true when the room in use changed
inline bool step(Picker& p, const Info* rooms, int n, float hx, float hy, float hz, float hint, bool hasHint, float dt) {
  int how = kOut;
  const int best = choose(rooms, n, p.cur, hx, hy, hz, hint, hasHint, &how);
  if (best == p.cur) {
    p.want = p.cur;
    p.wantT = 0.0f;
    p.how = how;
    return false;
  }
  if (p.first && best >= 0) {
    p.first = false;
    p.cur = best;
    p.want = best;
    p.wantT = 0.0f;
    p.how = how;
    return true;
  }
  if (best != p.want) {
    p.want = best;
    p.wantT = 0.0f;
  }
  p.wantT += dt;
  if (p.wantT >= (best < 0 ? 1.5f : 0.5f)) {
    p.cur = best;
    p.wantT = 0.0f;
    p.how = how;
    p.first = false;
    return true;
  }
  return false;
}

// a floor outline's area (m2, the shoelace formula) and its box (w x d)
inline float outline_area(const xrroom::Room& r, float* w, float* d) {
  const int n = r.outlineN;
  const float(*o)[2] = r.outline;
  float a = 0.0f, x0 = 1e30f, x1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    a += o[i][0] * o[j][1] - o[j][0] * o[i][1];
    x0 = o[i][0] < x0 ? o[i][0] : x0;
    x1 = o[i][0] > x1 ? o[i][0] : x1;
    z0 = o[i][1] < z0 ? o[i][1] : z0;
    z1 = o[i][1] > z1 ? o[i][1] : z1;
  }
  if (w) *w = n > 0 ? x1 - x0 : 0.0f;
  if (d) *d = n > 0 ? z1 - z0 : 0.0f;
  return a < 0.0f ? -0.5f * a : 0.5f * a;
}

}  // namespace xrroompick
