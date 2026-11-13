// strikers-quest r227 (Lance 11-12, r226: SETTINGS > ROOM said "YOUR ROOM SCAN COULDN'T BE READ (THE HEADSET SAID NO)"
// with the spatial data permission granted): HOW THE PIECES OF YOUR SCANNED ROOMS ARE ASKED FOR. r197-r226 asked for every
// piece of every room in ONE xrQuerySpacesFB uuid filter (his 3 rooms: 30 + 19 + 26 = 75 uuids) and the headset refused
// the request itself (XR_ERROR_VALIDATION_FAILURE, -1) - a uuid filter takes at most 50 (Meta's own SDK:
// OVRSpaceQuery.Options.MaxUuidCount = 50, its anchor fetch splits bigger lists the same way). Now:
//   - each uuid once (a piece two rooms both list - a shared wall, an old + a new scan of one kitchen - is asked for once,
//     under the first room listing it; xr_scene.cpp still gives it to every room that lists it), zero uuids dropped;
//   - one request per room, a room with more than 50 pieces split into requests of at most 50 - one bad request can't
//     sink the other rooms (xr_scene.cpp also halves a request the headset still refuses, down to single pieces).
// Pure C++ with no library calls (xr_room.h's rule): tools/outside/roomquery227 compiles it for the PC proof.
#pragma once

namespace xrroomquery {

constexpr int kMaxUuidsPerQuery = 50;

struct Batch {
  int room;   // the room it's for (its index in the rooms listed)
  int first;  // its uuids: out[first .. first + count)
  int count;
};
struct Stats {
  int listed = 0;     // uuids the rooms listed (all rooms)
  int zeros = 0;      // all-zero uuids dropped
  int dupInRoom = 0;  // listed twice by the same room
  int dupAcross = 0;  // already listed by an earlier room (asked for once, under that room)
  int kept = 0;       // uuids asked for
  int batches = 0;    // requests
  bool overflow = false;  // out / batches too small: some uuids not asked for
};

template <typename U>
inline bool same(const U& a, const U& b) {
  for (int i = 0; i < 16; ++i)
    if (a.data[i] != b.data[i]) return false;
  return true;
}
template <typename U>
inline bool zero(const U& a) {
  for (int i = 0; i < 16; ++i)
    if (a.data[i] != 0) return false;
  return true;
}

// lists[r] / counts[r]: room r's pieces as the headset gave them (xrGetSpaceContainerFB). out (outCap): the uuids to ask
// for, room by room; batches (batchCap): the requests, each one room's, at most maxPer uuids.
template <typename U>
inline Stats plan(const U* const* lists, const int* counts, int rooms, U* out, int outCap, Batch* batches, int batchCap,
                  int maxPer = kMaxUuidsPerQuery) {
  Stats s;
  if (maxPer < 1) maxPer = 1;
  int n = 0;
  for (int r = 0; r < rooms; ++r) {
    const int roomFirst = n;
    for (int k = 0; k < counts[r]; ++k) {
      const U& u = lists[r][k];
      ++s.listed;
      if (zero(u)) {
        ++s.zeros;
        continue;
      }
      int seen = -1;
      for (int j = 0; j < n && seen < 0; ++j)
        if (same(out[j], u)) seen = j;
      if (seen >= roomFirst) {
        ++s.dupInRoom;
        continue;
      }
      if (seen >= 0) {
        ++s.dupAcross;
        continue;
      }
      if (n >= outCap) {
        s.overflow = true;
        continue;
      }
      out[n++] = u;
    }
    for (int b = roomFirst; b < n; b += maxPer) {
      if (s.batches >= batchCap) {
        s.overflow = true;
        break;
      }
      Batch& bt = batches[s.batches++];
      bt.room = r;
      bt.first = b;
      bt.count = n - b < maxPer ? n - b : maxPer;
    }
  }
  s.kept = n;
  return s;
}

// the rooms listing a piece (bit r = room r; rooms 0..31) - a shared piece belongs to each of them
template <typename U>
inline unsigned rooms_of(const U& u, const U* const* lists, const int* counts, int rooms) {
  unsigned m = 0;
  for (int r = 0; r < rooms && r < 32; ++r)
    for (int k = 0; k < counts[r]; ++k)
      if (same(lists[r][k], u)) {
        m |= 1u << r;
        break;
      }
  return m;
}

}  // namespace xrroomquery
