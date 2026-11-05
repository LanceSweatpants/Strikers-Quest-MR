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
// to the front - one at a time, the biggest pull last. Your collection is kept across careers in STRIKERS_HOME/cards/collection.txt (each card its key +
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
// r140: TIZM COINS (only to buy packs, the CARDS shop): your balance; the last played match's coins (0 none), why ("3 goals 18,
// 2 assists 8, 14 tackles 7, win 10") and when (unix time). Any thread.
int coins();
int last_coins(std::string* why, long long* when);

// The hub's CARDS page (drawing thread): pieces for xrtext::board over the hub's middle (top .. bottom, w x h the
// hub's picture), the button hints, and what the pieces' pictures live in (keep it until the board is drawn).
struct Hint {
  std::string glyph, words;
  uint32_t rgb = 0x3A4A70;
};
struct HubPage {
  std::vector<xrtext::BoardPiece> pieces;
  std::vector<Hint> hints;
  std::shared_ptr<void> keep;
};
HubPage hub_page(float w, float h, float top, float bottom);

// The hub's buttons while the CARDS page is up (game thread). dir 1 left, 2 right, 3 up, 4 down (step: it moved
// this frame); a / b / x: pressed now; lt / rt: held. True: the cards used a button (the hub does nothing more with
// it); *stick: they used the stick (a stick left / right at the binder's edge is the hub's: its pages); *redraw: the
// page changed.
struct Input {
  int dir = 0;
  bool step = false, a = false, b = false, x = false, lt = false, rt = false;
};
bool hub_input(const Input& in, bool* redraw, bool* stick);
// A pack open or a card out of the binder: the hub stays on the CARDS page, its own buttons wait.
bool busy();
// Its redraw counter (the hub's picture is drawn again when it moves: a pack earned, cards added, a page turned).
int generation();

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
// A buzz for the controllers asked for since the last call: its strength (0 none) and length.
float take_buzz(float* seconds);

}  // namespace xrcards
