// strikers-quest r213 (Lance 2026-11-10: "start the collector trading feature"; C:\StrikersMR\COLLECTOR-TRADES.md): COLLECTOR
// TRADES - card-for-card trades (coins on either side if wanted) with nine CPU collectors, each with a personality, a binder, a
// wallet, a mood (the market's booms / crashes) and a memory of how you treat them. They ACCEPT, DECLINE or COUNTER your offers,
// and come to you with offers of their own (OFFERS FOR YOU). Only CPU collectors (nothing online); the loaded career's own TIZM
// COINS (64-bit, never shared between careers). Franchise hub > CARDS > COLLECTORS.
//
//   Valuation (xr_trade.cpp, COLLECTOR-TRADES.md 3-4): this week's market price of every copy (xr_market) x the collector's TASTE for
//   your card (held at 1.10 at most) against his HOLD on his own (never under 1.12, the market's BUY) + coins 1:1, his NEED >= 1: no
//   loop of trades can make coins (each hop loses >= 1.8%).
//   ONE BLACK 1/1 PER CARD EVER: a collector's 1/1 is THE 1/1 of that card in this career (packs give a GOLD: world11-<career>.txt).
//
// Pure C++ apart from xr_market.h / xr_text.h / xr_menu.h / franchise.h (the binder, the wallet, the sounds come in as function
// pointers: xr_cards.cpp), so the same file runs in the PC proof (tools/outside/trades213). Threads: frame / input on the game
// thread; draw on any thread (it copies a snapshot under the module's lock, then draws without it); point on the render thread
// (try_lock). Lock order: this module's -> the market's -> the binder's (never back).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "xr_market.h"
#include "xr_text.h"

namespace xrtrade {

struct Source {
  unsigned (*career)() = nullptr;    // the binder loaded (its career seed), 0 none
  long long (*balance)() = nullptr;  // its coins (64-bit)
  bool (*spend)(unsigned career, long long coins, const char* why) = nullptr;
  bool (*add)(unsigned career, long long coins, const char* why) = nullptr;
  bool (*sound)(const char* name, float gain) = nullptr;
  void (*catalogue)(std::vector<xrmarket::CardInfo>* out) = nullptr;  // the set (its index = the card), as the market's
  void (*holdings)(std::vector<xrmarket::Holding>* out) = nullptr;  // your copies now (fresh from the binder)
  bool (*give)(const xrmarket::Variant& v) = nullptr;               // a copy into your binder
  bool (*take)(const xrmarket::Variant& v) = nullptr;               // a copy out of it
  bool (*black_mine)(int card) = nullptr;                           // a 1/1 of this card is yours (binder, slab, a pack waiting)
  // r230: an EXQUISITE number of this card is yours / in a pack waiting (xr_cards exq_taken): a collector's copy never repeats it
  bool (*serial_mine)(int card, int serial) = nullptr;
  // the cards whose 1/1 the collectors hold now (keys), told whenever it changes: packs give a GOLD instead (xr_cards roll_parallel)
  void (*world_blacks)(unsigned career, const std::vector<std::string>& keys) = nullptr;
  void (*news)(const char* line) = nullptr;  // a big trade for the league news (null: PortFranchiseBookNews)
  void (*log)(const char* line) = nullptr;   // the game log (null: none)
  // r235: a collector RIPS a pack - the SHIPPING pack roller (xr_cards roll_pack / roll_exq_pack: the same odds, the one-1/1-per-card rule,
  // the EXQUISITE numbers) from `seed`: set 0 LANCINI TIZM, 1 JAM '96, 2 EXQUISITE. Called under this module's lock (-> the binder's). Null:
  // the collectors rip nothing (their binders move by buys / sales only)
  bool (*rip)(int set, uint64_t seed, std::vector<xrmarket::Variant>* out) = nullptr;
};
void set_source(const Source& s);
// r235: a copy of yours sold on the market to `who` for `coins` (xr_market Source::sold_for - any thread, queued): a named collector's buy goes
// into his binder (its cost his basis), a BLACK 1/1 to its buyer or the GRAIL HUNTER as r213
void world_sold(const xrmarket::Variant& v, const char* who, long long coins);
// r235: you buy a collector's (or THE DEALER'S CASE's) listed copy - the market's SEARCH (no market lock held). `ask` the price you saw: a
// higher one now refuses ("THE PRICE CHANGED"). True: done (coins out, the card in, his wallet paid)
bool buy_listing(uint32_t id, long long ask, std::string* line);
// A copy of yours sold on the market (xr_market Source::sold - any thread, queued): a BLACK 1/1 goes to its buyer if he's one of the
// nine collectors, else to the GRAIL HUNTER (it stays the one 1/1 of that card)
void world_got(const xrmarket::Variant& v, const char* who);

// ---- live ----
bool frame(double nowSeconds);  // game thread, every frame (works ~2 a second): the career, the matchdays, the offers
int generation();
bool redraw_due();               // the page wants drawing again for time alone (the deal flash, the last word expiring)
int offers_waiting();            // collectors' offers open for you (the rail's badge, HOME's WAITING FOR YOU)
void open_tab(int tab);          // 0 COLLECTORS, 1 OFFERS FOR YOU, 2 MY TRADES (HOME's WAITING FOR YOU goes to 1)

// The page's buttons (game thread): dir 1 left 2 right 3 up 4 down (step: moved this frame), a / b / x / y pressed now. True: the page
// used it (B is the page's inside a collector's desk / a pop-up; on the collectors' list it's the hub's: back to the rail)
struct Input {
  int dir = 0;
  bool step = false, a = false, b = false, x = false, y = false;
};
bool input(const Input& in);
bool point(float x, float y, bool click);  // a pointer on the page (page units): the number pad's keys while it's up
bool pad_open();
bool asking(std::string* question, std::string* line);  // the hub's confirm (A YES / B NO)
void hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord);

constexpr float kW = 1650.0f, kH = 958.0f;  // the page (as the market's)
void draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t);

// ---- the model (the PC proof drives the same functions the page's buttons do) ----
// r235 (Lance 11-12: "i also need more collectors, and for them to have less 1/1's up front"): 20 collectors (r213's nine + eleven),
// every one starting with a MODEST binder (base + a few low parallels: no 1/1, no GOLD, no case hit) and a rich-ish wallet; each matchday
// they RIP PACKS (the shipping roller), BUY singles (each other, THE DEALER'S CASE, your listings), LIST cards at their own prices
// (COLLECTOR-TRADES.md 11)
constexpr int kCollectors = 20;
constexpr int kDealer = kCollectors;  // r235: THE DEALER'S CASE (its listings' seller index)
constexpr int kMaxSide = 5;       // cards a side
constexpr int kMaxIncoming = 4;   // collectors' offers open at once
constexpr int kOfferDays = 3;     // matchdays an offer of theirs stands
constexpr double kTasteMax = 1.10, kHoldMin = 1.12;
// r235: where a copy came from (its collector's), what it cost him, its listing
enum { kFromSeed = 0, kFromPull, kFromBuy, kFromYou, kFromDealer };
struct Item {  // a copy in a collector's binder
  uint32_t id = 0;
  xrmarket::Variant v;
  int origin = kFromSeed;  // r235
  long long basis = 0;     // r235: what he paid (0: pulled / his from the start) - he never lists it under 105% of that
  int since = 0;           // r235: the matchday it came
  long long ask = 0;       // r235: > 0 listed for sale at this
  int listed = -1;         // r235: the matchday it went up
};
// r235: the hobby's counts (the PC proof)
struct HobbyStats {
  long long packs[3] = {0, 0, 0};  // ripped: LANCINI TIZM, JAM '96, EXQUISITE
  long long cards = 0, blacks = 0, golds = 0, hits = 0, exqBig = 0;  // pulled (exqBig: EMERALD+ / PATCH AUTO / MASTERPIECE)
  long long buys = 0, buyCoins = 0, fromYou = 0, bulked = 0;
  int listed = 0, under = 0, dealerCase = 0;
};
HobbyStats hobby_stats();
std::vector<Item> dealer_case();                     // THE DEALER'S CASE (r235: the old careers' pre-placed chase cards, for sale)
std::vector<xrmarket::Offering> for_sale();          // every copy listed now (the collectors' + the dealer's case)
std::vector<std::string> wire_lines();               // the trade wire, newest first
void debug_put(int collector, const xrmarket::Variant& v, long long ask);  // the proof: a copy into his binder (ask > 0: listed at it)
struct Offer {
  std::vector<xrmarket::Variant> mine;  // your copies
  std::vector<uint32_t> theirs;         // his copies (Item ids)
  long long myCoins = 0, theirCoins = 0;
};
enum { kRefused = 0, kAccepted, kCountered, kDeclined };
struct Reply {
  int kind = kRefused;  // kRefused: not sent (its line says why) / kAccepted: done / kCountered: his counter stands / kDeclined
  std::string line;     // his reaction
  double ratio = 0.0;   // R: what he gets / what he gives, in his eyes
  Offer counter;
};
Reply propose(int collector, const Offer& o);  // as his desk > Y SEND > YES
bool accept_counter(int collector);             // as the counter > A > YES
bool decline_counter(int collector);
bool has_counter(int collector, Offer* out);
struct CollectorView {
  std::string name, handle, kind;
  double goodwill = 0, mood = 0, need = 1;
  long long wallet = 0;
  int patience = 0, cards = 0, blacks = 0, trades = 0, lowballs = 0;
  std::vector<int> wants;  // the SET BUILDER's want list (cards)
};
std::vector<CollectorView> collectors();
std::vector<Item> binder(int collector);
double taste(int collector, const xrmarket::Variant& v);  // now (mood + rapport in)
double hold(int collector, const xrmarket::Variant& v);
double need(int collector);
struct IncomingView {
  uint32_t id;
  int collector;
  xrmarket::Variant want;  // your copy he wants
  Offer give;              // his side (give.mine empty)
  int daysLeft;
  bool final;
};
std::vector<IncomingView> incoming();
bool accept_incoming(uint32_t id);
int counter_incoming(uint32_t id, int percent);  // 1 accepted (done), 2 his FINAL OFFER, 3 he walked, 0 no
bool decline_incoming(uint32_t id);
struct TradeView {
  int day, collector;
  std::vector<xrmarket::Variant> gave, got;
  long long gaveCoins, gotCoins, gaveMarket, gotMarket;
  std::string headline;
};
std::vector<TradeView> history();  // newest first
std::vector<std::string> world_black_keys();  // the cards whose 1/1 a collector holds
// r219 (BIG-PULLS.md 4): STRIKERS ILLUSTRATED's COLLECTOR SPOTLIGHT - one of the nine (`which` mod 9: the matchday picks him in turn): who,
// his mood now, his word, what he's HUNTING (+ a card of it, card -1 none). Any thread (a copy under the lock); false: no career here.
struct Spotlight {
  int collector = -1;
  std::string name, handle, kind, quote, moodWord, hunting;
  double mood = 0.0;
  uint32_t c1 = 0, c2 = 0;
  int cards = 0, blacks = 0;
  xrmarket::Variant huntCard;
};
bool spotlight(int which, Spotlight* out);
// his portrait card (the COLLECTORS page's) as pieces in the box - any thread, no lock (his look is fixed)
void draw_portrait(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, int collector, float x0, float y0, float x1, float y1);
void reset_for_test();                         // forget the loaded career's trades (the proof's fresh career)
// r238 (HOLD BEFORE YOU BUY): a pointer on a collector's desk with HIS BINDER up (page units) - the copy under it (else the cursor's), focused
// (xr_cards: a grip there brings it into your hand). False: not on a desk's binder
bool inspect_at(float x, float y, xrmarket::Variant* v);
// the proof's pictures: tab, the collector + his desk open + its pane + the cursor, an offer in the desk (-1 keep), a counter shown
void debug_view(int tab, int collector, bool desk, int pane, int cursor, const Offer* draft, bool counterShown, int padFor);
void debug_say(int collector, const std::string& line, int kind);
void debug_mood(double cycle);                   // the market's mood as the collectors read it (the invariant sweep)
void debug_goodwill(int collector, double g);    // his rapport (the sweep, the pictures)
void debug_patience(int collector);              // his patience refilled (the personality runs: thousands of offers a matchday)

}  // namespace xrtrade
