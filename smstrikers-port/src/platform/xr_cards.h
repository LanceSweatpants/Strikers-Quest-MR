// strikers-quest r137 (Lance: "numbers 2 and 5 are sooooo fucking fire lets get on that"): trading cards, branded
// 2026-27 LANCINI TIZM STRIKERS (Lance: "the brand of the cards ... "Lancini Tizm" similar to the Panini Prizm!": his own
// parody brand, our own drawing - chrome stock, a refractor's prism, a chrome foil pack).
//
// Packs come from franchise matches only (franchise.cpp queues them: 1 a match played, 2 for a win, +1 a playoff series
// won, +1 a trophy); a pack is 5 cards rolled the moment it's earned (saved, so reloading never re-rolls it). The set is
// every pickable character, its evolved forms and its Marvel costumes (STRIKERS_HOME/franchise/cardset.txt, the same
// numbers on every headset): who it is common / rare / super rare; its finish BASE chrome / SILVER TIZM refractor / GOLD
// TIZM numbered /10 / BLACK TIZM 1/1; a Pokemon card shiny on top. The hub's CARDS page is the binder (9 pockets a page,
// LT / RT turn the pages); a pack opens in your hands in the room (one grip moves it, both grips turn and size it). r139
// (Lance: "i want the pack opening experience to be that of ripping a pack in real life"): the trigger of the hand not
// holding it rips it (A too), its 5 cards file out one by one into a stack in that hand, backs to you, and each pull of
// that trigger turns the front card over (its moment) / puts it round to the back, the next card's back (and its tell)
// to the front - one at a time, the biggest pull last. r153: each career has its own collection (binder, packs, coins, slabs, its own salt) in STRIKERS_HOME/cards/career-<seed>.txt (each card its key +
// finish + shiny + count, a GOLD's numbers: ready for online trading, next round).
//
// The pack is a bending foil pouch and (r138, Lance: "the cards, you made flimsy") each card stiff card stock - flat,
// ~1.2 mm thick, its edge all round in its finish's colour - in the effects layer (xr_fx model slots kPackSlot /
// kCardSlot; their pictures drawn here with xrtext::board, the foil in xr_fx_model.frag's card mode). r138: BASE is a
// plain printed card (no foil, no glint); only a parallel shines. Each finish's own 3D look plugs in at kFinishLook /
// finish_frame() in xr_cards.cpp. Everything runs on the game thread
// except the pictures (worker threads). Include after the Vulkan/OpenXR platform defines (as xr_fx.h).
#pragma once

#include "xr_fx.h"
#include "xr_market.h"  // r201
#include "xr_text.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace xrcards {

// The effects-layer model slots the cards use (xr_fx.h kModelSlots: the five before the magazine's, the last): the pack,
// the cards, r138: two special cards' 3D (a window's room, a pop-out, obsidian: the two biggest pulls on show) and the
// cards' light (the auras, the spotlight, the dimming).
constexpr int kPackSlot = xrfx::kModelSlots - 6;
constexpr int kCardSlot = xrfx::kModelSlots - 5;
constexpr int kSpecialSlot0 = xrfx::kModelSlots - 4;
constexpr int kSpecialSlot1 = xrfx::kModelSlots - 3;
constexpr int kLightSlot = xrfx::kModelSlots - 2;
// r141: a BLACK 1/1's world - its type's effects round you and on its card, its throne's aura (tinted light)
constexpr int kFxSlot = xrfx::kModelSlots - 7;
// r160 (Lance: binder skins + card sleeves): the 3D binder in your room (its skin cover, spine, rings, its pages) and the
// r163: its pages' own slot (the binder's model built once a skin)
constexpr int kBinderSlot = xrfx::kModelSlots - 9;
constexpr int kBinderPagesSlot = xrfx::kModelSlots - 8;  // r163: its pages (the sleeves' slot was; sleeves are gone): a page turn no longer rebuilds the binder

// r138: how the game picks an evolution's setting by kind (xr_quest.cpp evo_backdrop: "fire", "water" ...): a shiny
// Pokemon card's window shows its evolution room. Set once before frame().
void set_evo_backdrop(const char* (*pick)(const std::string& key));
// r141: the room's floor (LOCAL height), where a BLACK 1/1's life-size cameo stands. Set before frame().
void set_floor(float y);

// Every frame (game thread): takes the packs franchise.cpp earned, rolls and saves them.
void poll();

// For the hub (any thread): packs waiting to be opened; the newest big pull for STRIKERS ILLUSTRATED's cover line
// ("SHINY CHARIZARD", a super rare or a shiny from the last week of real time; "" none).
int packs_waiting();
std::string magazine_pull();
// r219 (BIG-PULLS.md 4): your big pulls of the last week of real time (a SILVER or better, a shiny, a case hit; newest first, up to 12)
// as the market's copies - STRIKERS ILLUSTRATED's market spread shows the best of them beside the league's. Any thread.
void magazine_pulls(std::vector<xrmarket::Variant>* out);
// r219 (BIG-PULLS.md 2): a BLACK 1/1's build-up in the stack in your hand - the room's darkness now (0 lit .. 1 dark) and the reveal's
// one white flash (0..1); xr_quest folds both into the passthrough's own style (any thread; 0 whenever no 1/1 is being built up)
float room_dark();
float room_flash();
// r230 (LANCINI EXQUISITE): the private gallery's mood now (0 the room as it is .. 1 a warm gallery: dimmer, warmer - xr_quest folds it into the
// passthrough's style and turns the crowd down by it; eased in as an EXQUISITE pack opens, back after its last card). Any thread
float room_gallery();
// r140: TIZM COINS (only to buy packs, the CARDS shop): your balance; the last played match's coins (0 none), why ("3 goals 18,
// 2 assists 8, 14 tackles 7, win 10") and when (unix time). Any thread.
// r208: 64-bit (xrcoin::Coins), held at xrcoin::kMax - every add saturates, never wrapping
long long coins();
int last_coins(std::string* why, long long* when);
// r199: TIZM SPORTSBOOK (xr_book.cpp) - the wallet is this binder's: the career whose binder is loaded (0 none); a stake
// taken (false: not that career's binder / not enough coins - nothing taken), a payout / refund added; both saved at once.
// A sound from STRIKERS_HOME/franchise/cardsounds/<name>.wav (false: not there). Any thread.
unsigned wallet_career();
bool spend_coins(unsigned career, long long coins, const char* why);
bool add_coins(unsigned career, long long coins, const char* why);
// r202: TIZM CASINO's MEGA JACKPOT: free packs into the loaded career's binder (rolled now, the shop's odds); false: not
// this career / no room
bool grant_packs(unsigned career, int packs, const char* why);
bool play_sound(const char* name, float gain);
// r202: the casino's win / lose music (cardsounds/<name>.wav) at the SOUND > EFFECTS volume; false: no such clip
bool play_music(const char* name, float gain);

// The hub's CARDS page (drawing thread): pieces for xrtext::board over the hub's middle (top .. bottom, w x h the
// hub's picture), the button hints, and what the pieces' pictures live in (keep it until the board is drawn).
struct Hint {
  std::string glyph, words;
  uint32_t rgb = 0x3A4A70;
};
// r157 (menu overhaul): `hints` = the page's own A word (glyph "A") and its extras (X / Y) for the hub's one hint bar
// (MOVE, A, B BACK, LT RT SECTIONS, extras); `aDim`: A does nothing here (its slot faded). `popup`: a pack open / a card
// out / a trade-in's show / a slab back - its own hint bar, the hub's hidden. `confirm`: a send / buy asking (the
// hub's confirm pop-up: A YES, B NO), `confirmLine` under it.
struct HubPage {
  std::vector<xrtext::BoardPiece> pieces;
  std::vector<Hint> hints;
  std::shared_ptr<void> keep;
  bool popup = false, aDim = false;
  std::string confirm, confirmLine;
};
HubPage hub_page(float w, float h, float top, float bottom);
// r157: the CARDS section's rail: BINDER, SHOP, EXPLOSIONS, TRADE-INS, SETS, GRADING (the page showing; set: another,
// its cursor kept). Any thread. r160: + LOOKS after EXPLOSIONS (binder skins, card sleeves). r201: + MARKET after the BINDER (the
// TIZM CARD MARKET, xr_market.h), + MUSEUM after LOOKS (the CARD MUSEUM, xr_museum.h).
// r236 (LOOK B, the sitemap - Lance 11-12: "the card tab should ONLY have card shit"): one rail of 11 pages shown as two hub sections -
// CARDS 0-6 (PACKS = the old SHOP + opening, BINDER (its SETS inside: Y), MARKET, COLLECTORS, GRADING, TRADE-INS, MUSEUM) and LOCKER
// 7-10 (BALLS and BINDERS = the old LOOKS' two shelves, GOAL FX and TAKEOVERS = the old EXPLOSIONS grid split)
// r242: + HOT 20 after MARKET (Lance 11-13: "see the hottest 20, like a power rankings type vibe") - CARDS 0-7, LOCKER 8-11
constexpr int kRailPages = 12;  // (r213: + COLLECTORS, the 3rd; r236: 11; r242: 12)
constexpr int kCardsRail = 8;   // r236: CARDS' pages (then LOCKER's) - r242: 8
constexpr int kRailPacks = 0, kRailBinder = 1, kRailMarket = 2, kRailHot = 3, kRailCollectors = 4, kRailGrading = 5, kRailTrade = 6, kRailMuseum = 7,
              kRailBalls = 8, kRailGoalFx = 9, kRailTakeovers = 10, kRailCovers = 11;
int rail_page();
void set_rail_page(int i);
const char* rail_name(int i);
// r157: the hub draws the cards' pieces into its page area, x' = ox + x * k, y' = oy + y * k (and the pockets' grab test
// maps the same way). Set before hub_page (any thread).
void set_hub_fit(float ox, float oy, float k);

// The hub's buttons while the CARDS page is up (game thread). dir 1 left, 2 right, 3 up, 4 down (step: it moved
// this frame); a / b / x: pressed now; lt / rt: held. True: the cards used a button (the hub does nothing more with
// it); *stick: they used the stick; *redraw: the page changed. r157: the stick never leaves the page (the binder turns
// its pages at its edges); LT / RT are the hub's sections except in a pack / card / slab scene; B is the cards' only
// in a scene or while a send / buy asks (else the hub's: back to the rail); Y opens a pack (the binder).
struct Input {
  int dir = 0;
  bool step = false, a = false, b = false, x = false, lt = false, rt = false;
  bool y = false;  // r153: Y (the EXPLOSIONS page's equip: wants_y)
  int turn = 0;    // r171: the right stick's flick left (-1) / right (+1): the BINDER page turns a spread
};
bool hub_input(const Input& in, bool* redraw, bool* stick);
// r153: the CARDS page wants Y itself (its GOAL EXPLOSIONS page: Y equips) - the hub's Y (trades) leaves it be there
bool wants_y();
// A pack open or a card out of the binder: the hub stays on the CARDS page, its own buttons wait.
bool busy();
// Its redraw counter (the hub's picture is drawn again when it moves: a pack earned, cards added, a page turned).
int generation();
// r221: the frame thread's room-takeover coin payments since the last call (ns spent, how many) - xr_quest's goal probe
void take_coin_probe(long long* ns, int* pays);

// The hands (game thread, sync_input, before anything else takes the grips): grips as held, the hands' places (grip
// pose positions) and their pointers (aim poses, null when not tracked); r142: the hands' whole grip poses (null when
// their turn isn't tracked) - a pack / card / stack in a hand goes with that hand's whole pose. A grip that closes on the
// pack or a card is the cards' until let go (hand_busy).
void hands(const bool grip[2], const bool have[2], const XrVector3f hand[2], const XrPosef* aim[2], const XrPosef* gripPose[2],
           uint64_t now);
bool hand_busy(int h);
// r139: the controllers' index triggers as pulled (0..1: the left, the right), every frame after hands() (game thread): the
// hand not holding the pack rips it, then turns its cards over one by one.
void triggers(const float pull[2], uint64_t now);

// Every display frame (game thread, PortXrEndFrame): takes the packs earned (poll); while `live` (the hub open on its
// pages, your head tracked) places and bends the pack / the cards for `scene` (its models in kPackSlot / kCardSlot),
// their sparkles; else puts them away. `hub`: the hub's picture as shown (null: not shown), for a card flying out of /
// back into its pocket. True while anything of theirs shows (the hub goes under the effects layer then).
bool frame(xrfx::Scene& scene, const XrPosef& head, bool live, bool fxReady, const XrCompositionLayerQuad* hub, uint64_t now);
// The hub closed (or a match started): everything put away at once.
void close(bool fxReady);
// r152: TIZM GRADING's slab shelf in your room - up to 6 of your slabs (the ones you put on it on the GRADING page, else
// your best) standing on a lit two-step stand, first beside the trophy shelf (`shelf` its place on its floor / table,
// `shelfYaw` its turn), then where you leave it (STRIKERS_HOME/cards/slab_shelf.txt); a grip on it moves it, both grips
// turn and size it (hand_busy while held). Every frame after frame() (game thread); `show` false (no home ground, a
// cards scene up, not your room): put away. Its model is the pack's slot's while no pack / card / slab is out.
void room_display(xrfx::Scene& scene, const XrVector3f& shelf, float shelfYaw, bool show, bool fxReady, uint64_t now);
// r160 (Lance: "a REAL 3D binder you pick up", its skin its whole cover): your binder in the room - taken out from CARDS >
// BINDER (X TAKE IT OUT / PUT IT BACK), first in front of you, then where you leave it (STRIKERS_HOME/cards/binder_room.txt,
// headset-wide); one grip moves it, both grips turn and size it (hand_busy while held); a trigger on it opens it / turns
// its pages (right page: on, left page: back, left on the first pages: shut) - the same spreads as the CARDS page. Also the
// LOOKS page's preview of a skin (in front of you). Every frame after frame() (game thread); `show` false (not your room):
// put away. Its model is kBinderSlot's.
void room_binder(xrfx::Scene& scene, const XrPosef& head, bool show, bool fxReady, uint64_t now);
// r160 round 2 (Lance: it flips EXACTLY like STRIKERS ILLUSTRATED - xr_pageturn.h's rules): while a hand holds the binder
// its right trigger turns a page on, its left back (0.9 s after the grab), a sweep of the free hand across it turns one,
// and the hub's buttons are its (binder_turn: +1 / -1 the stick, 2 A - the next, shut on its last pages -, -2 B shut it),
// its hint bar the magazine's. Game thread; binder_held any thread.
bool binder_held();
void binder_turn(int how);
// r163 (his r162 video: X never put it away once open / held): put it away now (leaving CARDS, the hub closing) / X: out or
// away from any state; what it's doing (0 away, 1 out shut, 2 out open, 3 held open, 4 a card of it out in your hand, 5
// held shut) - the hub's hint bar says only what its buttons do then.
void binder_put_away();
void binder_toggle();
int binder_state();
// r201: the CARD MUSEUM's card atlas for a piece (xr_quest.cpp, on a worker thread): its items' fronts in 3 x 3 cells of 341 x 476
// at the top of a 1024 x 2048 RGBA picture, a slab's label in 3 x 3 cells of 341 x 97 under them (y 1428), the card back at
// (0, 1719) 236 x 329; r210: clear (alpha 0) everywhere else, an empty slot clear but for a dashed card outline + EMPTY SPOT. Waits
// up to 2 s for a card's big picture. Any thread.
bool museum_atlas(const std::vector<xrmarket::Variant>& items, std::vector<uint8_t>* out);
// r210: a straight-RGBA sRGB picture's mip chain (levels 1..n down to 1 x 1): 2 x 2 averaged in linear light, weighted by alpha. Any
// thread (the museum's worker; the PC proof).
std::vector<std::vector<uint8_t>> mip_chain_srgb(const std::vector<uint8_t>& rgba, int w, int h);
// A buzz for the controllers asked for since the last call: its strength (0 none) and length.
float take_buzz(float* seconds);

}  // namespace xrcards
