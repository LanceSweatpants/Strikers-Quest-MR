// strikers-quest r201 (Lance 2026-11-08, C:\StrikersMR\COIN-SINKS.md 2): the CARD MUSEUM - display pieces bought with the loaded
// career's TIZM COINS and placed in your room (MR and VR) like the sportsbook machine:
//   WALL FRAME (a deep shadowbox, black lacquer + a gold lip, its brass picture light; on a WALL of your room scan),
//   GLASS PEDESTAL (a lacquer plinth, a glass cube, a downlight in its brass crown), SLAB RACK (walnut + brass, three lit tiers,
//   9 TIZM GRADING slabs), SHOWCASE CABINET (black + glass, a mirrored back, three lit shelves, 6 cards), 1/1 VAULT (a riveted
//   gunmetal vault, its dial, a thick glass case, a spotlight on a steel arm over it).
// Any card / slab of your binder goes in them (they stay in your binder: shown, not moved); each card's 3D figure stands in the
// case with it, moving (xr_quest.cpp: the franchise figures' loader). Grab: LEFT GRIP moves it, BOTH GRIPS turn + size it; floor
// pieces stand on your floor (the scan's or the Guardian's), the wall frame hangs on a wall of your room scan (no scan: it waits).
// Spots kept per room. Pure C++ (the page + the data); the 3D, the grab and the room are xr_quest.cpp's.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "xr_market.h"
#include "xr_text.h"

namespace xrmuseum {

enum Kind { kFrame = 0, kPedestal, kRack, kCabinet, kVault, kKinds };
struct KindInfo {
  const char* name;
  const char* line;   // the shop's line
  int price;
  int slots;
  bool wall;          // hangs on a wall (a room scan's)
  bool slabsOnly;     // TIZM GRADING slabs only
};
const KindInfo& kind_info(int kind);

struct Spot {  // where a piece is in one room (its frame: the scan's or the Guardian's)
  bool set = false;
  float x = 0.0f, z = 0.0f, yaw = 0.0f, scale = 1.0f;
  float y = 0.0f;   // a wall piece's height (its bottom over the floor)
  int wall = -1;    // a wall piece: the scan's wall it hangs on
};
struct Piece {
  uint32_t id = 0;
  int kind = 0;
  std::vector<xrmarket::Variant> items;  // its slots (card -1: empty)
  bool shown = true;                     // out in your room (X on the page)
};

// The wallet + sounds (xr_cards.cpp sets them: the binder's)
struct Wallet {
  unsigned (*career)() = nullptr;
  long long (*balance)() = nullptr;  // r208: 64-bit coins
  bool (*spend)(unsigned, long long, const char*) = nullptr;
  bool (*sound)(const char*, float) = nullptr;
};
void set_wallet(const Wallet& w);
// The shop's pictures of the pieces (STRIKERS_HOME/franchise/museum/thumb_<kind>.png, straight RGBA; null none)
void set_thumbs(std::shared_ptr<void> (*thumb)(int kind, const uint8_t** px, int* w, int* h));

// Every frame (game thread): switches with the career, drops copies you no longer have. True: something shown changed.
bool frame(double now);
int generation();       // anything on the page / in the room changed
int room_generation();  // what stands in the room changed (a piece shown / put away / filled)
bool redraw_due();      // r206: the page's last word just went stale (the page is drawn on changes only now)

// The pieces out in your room (copies; at most kMaxShown)
constexpr int kMaxShown = 6;
std::vector<Piece> shown();
// r226: every copy in a piece of yours (shown out in the room or put away) - kept out of TRADE-INS (xr_cards). Takes the museum's
// lock only: never call it holding xr_cards' g_mutex (frame() holds this lock while it asks xr_cards for your copies)
std::vector<xrmarket::Variant> items();
// Its spot in a room ("" key: none); set it (game thread; saved with the career's museum)
Spot spot(uint32_t id, const std::string& room);
void set_spot(uint32_t id, const std::string& room, const Spot& s);
// Your room as the page should say it (xr_quest.cpp): a room scan with walls (wall pieces can hang)
void set_room_scan(bool walls);

// The page (the hub's CARDS > MUSEUM): buttons, the hub's confirm, its hints, its drawing (kW x kH at ox, oy x k)
struct Input {
  int dir = 0;
  bool step = false, a = false, b = false, x = false, y = false;
};
bool input(const Input& in);
bool asking(std::string* question, std::string* line);
void hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord);
bool busy();  // choosing a card for a slot (B is the page's)
constexpr float kW = 1650.0f, kH = 958.0f;
void draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t);

// PC proof
void debug_add(int kind, const std::vector<xrmarket::Variant>& items);
void debug_cursor(int focus, int row, int pick, int slot);

}  // namespace xrmuseum
