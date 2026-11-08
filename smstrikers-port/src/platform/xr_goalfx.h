// strikers-quest r153 (Lance: "what if we did rocket league-esque goal explosions that ppl could buy with their
// coins???"; "i want that shit to be something that takes over the whole room in typical lance sweatpants fashion"):
// GOAL EXPLOSIONS. When YOUR side scores, your equipped explosion goes off - in every mode (quick matches, cups,
// franchise, online: cosmetic only, the game is the same game). It starts at the goal where the ball went in (on the
// big screen's picture, on the Tabletop field's goal, at the real goal of the life-size stadium), a wave rolls out of it
// through you, and then it takes over the whole room round you for 3-5 s (its signature moment: lightning all round
// you, the room a star field, a plant bursting up through your floor, columns rising round you ...), with its own
// sound and a rumble in both hands; then it clears and the goal's usual show follows. Our own designs (no other game's).
// Online the friend's headset plays YOUR explosion when you score (the hello / match start carry the equipped id; an
// older build just plays none for you).
//
// Bought with TIZM COINS (xr_cards.cpp keeps them: it lends its wallet here with set_wallet - nothing here touches the
// cards' files). Owned + equipped are kept in STRIKERS_HOME/cards/goalfx.txt (written whole: a temp file, then renamed).
//
// Include after the Vulkan/OpenXR platform defines (as xr_fx.h).
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <vector>

namespace xrfx {
struct Scene;
}

namespace xrgoalfx {

// ------------------------------------------------------------------------------------------------------------------
// The shop's side (xr_cards.cpp's EXPLOSIONS page). Any thread unless it says otherwise.
// ------------------------------------------------------------------------------------------------------------------

// The catalogue: fixed, in shop order, the same on every headset. Ids never change (saved, and sent online).
struct Item {
  const char* id;     // "classic", "confetti", ... (lower case ASCII, at most 15 characters)
  const char* name;   // "CLASSIC BURST"
  const char* desc;   // one line for the shop
  int price;          // TIZM coins (0 = free: everyone owns it)
  int tier;           // 0 FREE, 1 COMMON (150), 2 RARE (300), 3 EPIC (600), 4 LEGENDARY (1200), 5 MYTHIC (2500)
  uint32_t rgb1, rgb2;  // its two colours (0xRRGGBB: a swatch / the tile's colours)
  float seconds;      // how long it plays (r161: the room takeovers ~8 s - over 5 s = a takeover, xr_cards.cpp's shop)
};
const std::vector<Item>& catalogue();
const Item* find(const char* id);  // null: no such id
const char* tier_name(int tier);   // "FREE" .. "MYTHIC"
uint32_t tier_rgb(int tier);       // the tier's colour (0xRRGGBB)

// TIZM COINS live in xr_cards.cpp; set once (at start) by the cards. spend() takes the coins and saves them, false when
// there aren't enough (then nothing is taken). Both are called on the thread that calls buy().
struct Wallet {
  int (*balance)() = nullptr;
  bool (*spend)(int coins, const char* what) = nullptr;
};
void set_wallet(const Wallet& wallet);
// r164: TIZM coins caught in 8-BIT TAKEOVER since the last call (your own goals only; at most 150 a goal) - the cards
// pay them into the loaded career during a franchise match, else drop them (any thread)
int take_earned();

bool owned(const char* id);  // the free one always; unknown ids never
enum class Buy { kBought, kAlreadyOwned, kNotEnough, kNoWallet, kUnknown };
// Buys it with the wallet's coins (kBought: spent, owned and saved). *coinsLeft (may be null): the balance after.
Buy try_buy(const char* id, int* coinsLeft);
bool buy(const char* id, int* coinsLeft);  // try_buy() is kBought or kAlreadyOwned

// The one that goes off when you score: an owned id, or "none" / "" for none at all. False (nothing changes): not
// owned / unknown. Saved at once.
bool equip(const char* id);
const char* equipped();  // "classic" until another is equipped; "none" when off

// The shop's preview (game thread): plays it in your room, owned or not, its goal at `where` (its +Z toward you; e.g.
// the shop's tile, or ~2 m in front of you) - the whole room takeover, as it plays after a goal. Replaces one playing.
void preview(const char* id, const XrPosef& where);
bool playing();  // a goal explosion or a preview is going off now
void stop();     // ends it now (what is in the air fades out quickly)

// Changes whenever anything above does (owned / equipped / the friend's): the shop redraws.
int generation();

// ------------------------------------------------------------------------------------------------------------------
// The game's side (Game.cpp, xr_fx.cpp, xr_quest.cpp, xr_net.cpp). Game thread unless it says otherwise.
// ------------------------------------------------------------------------------------------------------------------

// Online: the friend's equipped id from their hello / the match start ("" none: an older build). Any thread.
void set_friend_equipped(const char* id);
const char* friend_equipped();

// xr_fx.cpp, inside xrfx::update (every display frame while the effects layer is up): resolves a goal that came in
// (where its goal is in this view), runs its beats, steps its pieces. True while anything of it shows.
bool update(const xrfx::Scene& scene);
bool pieces_up();  // any of its pieces up now (xrfx::render draws then)

// xr_fx.cpp, inside xrfx::render: its pieces as the effects shaders' instances (the same 56 bytes as xr_fx.cpp's
// Instance). The 3D ones first, grouped by mesh (runs: xr_fx mesh ids, first + count in `out`), then the quads from
// *quadFirst on. Returns how many were written (at most maxPieces).
struct Piece {
  float posSize[4];
  float quat[4];
  uint32_t color1, color2;  // R, G, B, A bytes (low byte red)
  float params[4];          // shape, aspect, alpha, glow
};
static_assert(sizeof(Piece) == 56, "the effects' instance");
struct MeshRun {
  int mesh = 0;  // an xr_fx mesh id; + 1000: its reduced detail (far from you, or lying still)
  uint32_t first = 0, count = 0;
};
uint32_t write(const xrfx::Scene& scene, Piece* out, uint32_t maxPieces, MeshRun* runs, int maxRuns, int* runCount,
               uint32_t* quadFirst);

// xr_quest.cpp: a buzz for both controllers asked for since the last call (0 none) and how long.
float take_buzz(float* seconds);

// r161 (GOAL EXPLOSIONS pack 2, Lance: "make sure these rooms are interactive ... living and breathing"): the ROOM
// TAKEOVERS live off your hands - punch, grab, push, splash, the trigger fires. xr_quest.cpp sync_input, every frame
// after xrcards::triggers (game thread): the hands' grip poses (null: not tracked), their aim poses (null), the grips as
// held (raw), the index triggers (0..1) and whether the match is being played now. True while a takeover has your hands:
// then the triggers and the grips are its own (the caller keeps them from the game, the table, the screen). Never while
// the match is live: the moment play is back (or never stopped) the takeover clears.
bool hands(const XrPosef* grip[2], const XrPosef* aim[2], const bool gripHeld[2], const float trigger[2], bool playLive);

}  // namespace xrgoalfx

// Game.cpp (CheckForGoal, read only - the match plays the same): a goal by `side` (0 home, 1 away), the scorer's pad
// (-1 the computer), an own goal, where the ball is (game units) and where that is in the broadcast picture (-1..1, y
// up; ndcValid 0 = not in it). Decides whose explosion goes off (yours when your side scores, the online friend's when
// theirs does) and holds the goal's usual show until it has played.
extern "C" void PortGoalFxGoal(int side, int humanPad, int ownGoal, float x, float y, float z, float ndcX, float ndcY,
                               int ndcValid);
