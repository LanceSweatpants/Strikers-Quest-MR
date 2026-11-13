// strikers-quest r201 (Lance 2026-11-08, C:\StrikersMR\COIN-SINKS.md 1): the TIZM CARD MARKET - card "stocks" for the loaded
// franchise career's own TIZM COINS (the binder's wallet; a fictional currency, never real money).
//
//   Every card (each character, each parallel: BASE / SILVER / GOLD /10 / BLACK 1/1, shiny, the case hits TIZM BOOM / HOMETOWN /
//   SIGNED, and TIZM GRADING slabs by grade) has a live price: its rarity and rating, how its character is really doing in
//   YOUR franchise (goals, form, the club's record, the GOLDEN BOOT / MVP races, levels, evolutions, awards; a retirement makes
//   its cards LEGEND cards), its scarcity (how many copies the league's collector world has: GOLD /10, BLACK 1/1) and a little
//   market noise. Re-priced every matchday (the career's week moving on), each price's history kept for its sparkline.
//   BUY from CPU collectors (their listings, a fresh few each matchday), SELL to them at once (they pay 82% of the market), and
//   their OFFERS on your cards (accept / decline / counter; two or three of them bidding on a hot card). Portfolio value,
//   HOT / COLD movers, market headlines (the league news too, not in co-op / online leagues).
//   No money machine: a buy costs 112% of the market, an instant sale pays 82%, no offer ever tops 108% (under the buy
//   price), offers only come for cards held a whole matchday; a pack's cards are worth less than the pack at the sell price.
//
// Deterministic: the prices are a function of the career (its seed) and the franchise states the market has seen at each
// matchday (no random numbers: hashes of the seed, the card and the day). This headset's own (STRIKERS_HOME/cards/
// market-<career>.txt beside the binder), never sent online.
//
// Pure C++ apart from franchise.h + xr_text.h + xr_menu.h (the cards, the wallet and the pictures come in as function
// pointers: xr_cards.cpp), so the same file runs in the PC proof (tools/outside/market201: wasm32 + node). Threads: frame /
// input on the game thread; draw on any thread (it copies what it needs under the module's mutex).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../../include/port/franchise.h"
#include "xr_coins.h"  // r208: 64-bit TIZM COINS
#include "xr_text.h"

namespace xrmarket {

// ---- the cards (xr_cards.cpp's set and your copies) ----
struct CardInfo {
  std::string key, stat, name, line;  // its card key, its franchise ratings key, its name, its character line
  int rarity = 0, series = 0, rating = 75;  // 0 common 1 rare 2 super rare; xr_cards' series
  bool here = true;                   // packs on this headset can give it (only those are listed)
  uint32_t c1 = 0x31406A, c2 = 0xFFD23F;
  // r217: its set - 0 LANCINI TIZM, 1 LANCINI TIZM JAM '96 (the premium pack: its cards MORE COVETED - kJam*, PACKS-AB.md); `base` the
  // LANCINI TIZM card of the same character (a Jam card moves with it: the same demand, line), its own index for a main card
  int set = 0, base = -1;
  int no = 0;  // r230: a LANCINI EXQUISITE card's number on its checklist (its "jersey number": a copy numbered so, a premium)
  // r231: its place in its FAMILY (an evolution line / a hero's suits / an ORIGINAL's uniforms - one player for the market): stage 0 the
  // first form .. stages - 1 the last (a branch's forms: each its depth). A family's cards all move with the one player; the last form a
  // little dearer (kStageStep a stage below it), never more. Defaults: a card alone (no family)
  int stage = 0, stages = 1;
};
// r217: a JAM '96 card's price over its LANCINI TIZM twin's (the same copy): kJamBase x kJamScarce^s (s 0 bulk .. 1 a 1/1), x kJamAuto
// an autograph (SIGNED / a BLACK 1/1), x kJamGrail a GRAIL character - ~3x a BASE .. ~9x a grail's BLACK 1/1 auto (the 90s inserts)
constexpr double kJamBase = 3.0, kJamScarce = 2.0, kJamAuto = 1.25, kJamGrail = 1.2;
constexpr double kJamPop = 0.35;  // r217: fewer Jam packs in the collector world (their pop report)
bool jam_card(int card);           // r217: a JAM '96 card
// r230: a LANCINI EXQUISITE card (set 2): every copy numbered - its tier + number in Variant::serial (xr_exq.h: tier x 1000 + number); its
// price from its tier (xrexq::kS / kMult, the number's premiums, a rookie, a grail) over the same market as every card (MARKET-PRICES.md 11)
bool exq_card(int card);
enum { kBase = 0, kSilver, kGoldTizm, kBlack };
enum { kHitNone = 0, kHitBoom, kHitHome, kHitSigned };
struct Variant {  // one kind of copy
  int card = -1;
  int parallel = kBase;
  bool shiny = false;
  int hit = kHitNone;
  int serial = 0;     // a GOLD's number (1-10), a BLACK's 1
  int grade = 0;      // a TIZM GRADING slab's grade (1-10), 0 raw
  uint32_t cert = 0;  // a slab's cert
};
struct Holding : Variant {
  int count = 1;
  bool grading = false;  // at TIZM GRADING (not back yet: can't be sold)
};
// "GOLD TIZM 07/10", "SILVER TIZM - SHINY", "TIZM BOOM", "BASE - TIZM 9 MINT"
std::string variant_words(const Variant& v);
std::string variant_sig(const std::vector<CardInfo>& set, const Variant& v);

struct Source {
  unsigned (*career)() = nullptr;  // the binder loaded (its career seed), 0 none
  long long (*balance)() = nullptr;  // r208: the wallet's coins 64-bit
  bool (*spend)(unsigned career, long long coins, const char* why) = nullptr;
  bool (*add)(unsigned career, long long coins, const char* why) = nullptr;
  bool (*sound)(const char* name, float gain) = nullptr;
  void (*catalogue)(std::vector<CardInfo>* out) = nullptr;  // the set (its index = the card)
  void (*holdings)(std::vector<Holding>* out) = nullptr;    // your copies now
  bool (*give)(const Variant& v) = nullptr;                 // a bought copy into your binder
  bool (*take)(const Variant& v) = nullptr;                 // a sold copy out of it
  // a card's portrait (straight RGBA): kept alive by the returned holder; null none
  std::shared_ptr<void> (*portrait)(int card, const uint8_t** px, int* w, int* h) = nullptr;
  // the franchise as it stands (null: PortFranchiseMarketGet) - the PC proof feeds its simulated seasons here
  bool (*franchise)(PortFranchiseMarketState* out) = nullptr;
  void (*news)(const char* line) = nullptr;  // a market headline for the league news (null: PortFranchiseBookNews)
  // r210: your listed copies (LIST FOR SALE), told whenever they change: the binder keeps them out of TRADE-INS and GRADING (null: none)
  void (*listed)(const std::vector<Variant>& copies) = nullptr;
  // r213: a copy of yours just sold on the market (SELL NOW: who "A DEALER", an offer / your listing: the collector) - the COLLECTOR
  // TRADES module keeps a sold BLACK 1/1 in the collector world (one 1/1 per card ever). Null: nothing told.
  void (*sold)(const Variant& v, const char* who) = nullptr;
  // r214: this career's BLACK 1/1 of `card` is taken - yours (binder, slab, a pack waiting) or a CPU collector's (the COLLECTOR
  // TRADES world keys): the wire never sells a taken 1/1 (that sale goes out as a GOLD /10). Called under the market's lock (market ->
  // binder, never back): it must not call into the market or the trades module. Null: none taken.
  bool (*black_taken)(int card) = nullptr;
  // r220: a JAM '96 copy's die (its rarest feature's cut - xr_cards lk::jam_shape) as a small picture (straight RGBA, alpha 255 where the
  // card is): its minis wear their real cut. Kept alive by the returned holder. Null: the minis' rounded boxes, as before
  std::shared_ptr<void> (*cut_sil)(const Variant& v, const uint8_t** px, int* w, int* h) = nullptr;
  // r230: an EXQUISITE number taken in this career (yours, a pack waiting, a collector's): never listed / sold on the wire. Called under the
  // market's lock (market -> binder): it must not call back. Null: only your holdings are checked
  bool (*serial_taken)(int card, int serial) = nullptr;
  // r235: a copy of yours just sold (SELL NOW / an offer / your listing) - who bought it and for how much: a named collector's buy goes into
  // his binder (xr_trade world_sold: queued, any thread). Called under the market's lock. Null: none (r213's `sold` alone)
  void (*sold_for)(const Variant& v, const char* who, long long coins) = nullptr;
  // r235: you buy a collector's / the dealer's listed copy from the SEARCH (xr_trade buy_listing: its lock, the coins, the card) - called
  // with NO market lock held (the market's input lets go first). True: bought; `line` its words. Null: those listings can't be bought
  bool (*buy_offering)(uint32_t id, long long ask, std::string* line) = nullptr;
};
void set_source(const Source& s);
// r213 (COLLECTOR TRADES, xr_trade.cpp): your copies held in an open collector trade (a counter waiting for your answer): not free here
// (no SELL NOW / LIST / offers on them) and told to the binder with the listed ones (out of TRADE-INS and GRADING). Game thread.
void set_held_out(const std::vector<Variant>& copies);
// r213: your listed copies (LIST FOR SALE) - they can't go in a collector trade
std::vector<Variant> listed_copies();
// r214: the market wire's newest headlines, newest first (the PC proof reads them)
std::vector<std::string> wire_lines();

// ---- r235 (Lance 11-12: "make buying singles easy and searchable ... if i wanna buy togepi cards, i can just search togepi and see all
// collectors selling togepi at their respected prices"): THE SINGLES MARKET - every copy the 20 collectors + the dealer have up for sale,
// SEARCHED on the BUY tab (a name typed on an on-screen letter grid: pointer + trigger or stick + A; live results; recent searches; SET /
// FINISH / PRICE / SORT filters; WATCH a player). The collectors' listings come from xr_trade (set_offerings, on change / a matchday: an
// index - nothing scanned per frame); the dealer's are this module's own (BUY at 112%) + xr_trade's DEALER'S CASE.
constexpr double kListFloor = 0.90;  // no CPU seller lists a copy under this much of its market (over SELL NOW's kBid: no instant loop)
struct Offering {                    // one copy for sale
  uint32_t id = 0;                   // the seller's item id (xr_trade)
  int seller = -1;                   // its collector (0 .. 19), 20 THE DEALER'S CASE
  std::string who;                   // "@ONEOFONE", "THE DEALER"
  uint32_t rgb = 0xFFD23F;           // his colour
  Variant v;
  long long ask = 0;
  int day = 0;                       // the matchday it went up
};
void set_offerings(unsigned career, const std::vector<Offering>& rows);  // xr_trade (its lock -> this one); game thread
struct Buyer {                       // a named collector who might buy your listing / bid on your card (his wallet)
  std::string handle;
  long long wallet = 0;
};
void set_buyers(const std::vector<Buyer>& b);
// r235: a copy you bought on the market in the last 2 matchdays and what you paid - the collectors credit it at no more than 98% of that
// (no buy-cheap -> trade-to-a-collector loop the same week). False: none
bool paid_recent(const Variant& v, long long* paid);
// the search (the proof drives what the page's buttons do): the query + filters, the results, buy one (as A > YES), watch a player
enum { kFSetAll = 0, kFSetA, kFSetJam, kFSetExq, kFSets };
enum { kFFinAny = 0, kFFinBase, kFFinSilver, kFFinGold, kFFinNumbered, kFFinOne, kFFinAuto, kFFinPatch, kFFins };
enum { kFPriceAny = 0, kFPrice1K, kFPrice10K, kFPrice100K, kFPrice1M, kFPrice10M, kFPriceUnder, kFPrices };
enum { kSortLow = 0, kSortHigh, kSortNew, kSortHot, kSortDeal, kSorts };
struct SearchRow {
  int src = 0;          // 0 the dealer's listing of the matchday (BUY at 112%), 1 an Offering
  uint32_t id = 0;      // its listing index / its Offering id
  int seller = -1;
  std::string who;
  uint32_t rgb = 0;
  Variant v;
  long long ask = 0, market = 0;
  int day = 0, rank = 0;  // listed, its family's place in the TOP CARDS race
  float chg = 0.0f;
};
void search_set(const std::string& query, int set, int finish, int price, int sort);
std::vector<SearchRow> search_results();
bool search_buy(int row, std::string* line);  // as BUY > A > YES on that row
bool search_open();                           // the search panel (the letter grid) is up: its pointer + trigger (xr_quest)
void search_close();                          // the page left (xr_cards): the panel down, the query kept
bool watch_toggle(int card);                  // WATCH / UNWATCH its player (its family): true now watched
std::vector<std::string> watched();           // the watched families' names
std::vector<std::string> recent_searches();
void debug_search(int stage, int key);        // the proof's pictures: 0 shut, 1 the panel up (the key under the cursor), 2 a row asked

// ---- the model (pure: the PC proof calls these) ----
// r210 (Lance 11-10: "100 coins isnt enough for collectibles! ... think of these as the real pokemon market irl! its crazy!!!"):
// prices span a few coins (bulk commons) to tens of millions (a GRAIL character's BLACK 1/1 autograph) - MARKET-PRICES.md. Every
// price / bid / ask / offer is 64-bit (xrcoin::Coins), held under xrcoin::kMax.
constexpr double kBid = 0.88;       // collectors buy from you at this much of the market (r210: a card worth 20,000+; bulk less: bid_ratio) - r234: 0.82 -> 0.88 (Lance 11-12: "its def a flippers market" - a real pop pays after the spread; buy 1.12 / sell 0.88: an instant loop still gets back 79%)
constexpr double kBidBulk = 0.20;   // r210: what a dealer pays at once for bulk (a card worth 200 or less; log-scaled up to kBid at 20,000)
constexpr double kAsk = 1.12;       // and sell to you at this much
constexpr double kOfferMax = 1.08;  // an offer never goes over this much of the market (under kAsk: no flip)
constexpr int kHist = 24;           // matchdays of history kept
constexpr int kListings = 8;        // collector listings a matchday
constexpr int kMaxOffers = 5;
constexpr int kMaxMine = 20;        // r210: your own listings (LIST FOR SALE) at once
double bid_ratio(long long market);  // r210: SELL NOW's share of the market for a card worth this much

// The market price of a copy now (coins, >= 1) and its history (oldest first, the last = now); its world population
long long price(const Variant& v);
long long bid(const Variant& v);   // what selling it now pays
long long ask(const Variant& v);   // what buying it costs
void history(const Variant& v, std::vector<long long>* out);
long long population(const Variant& v);
// r231 (Lance 11-12: "stats/play should determine pricing ... literally only stats"): a character's DEMAND comes only from how its player
// does in THIS career's league (MARKET-PRICES.md 2) - no real-world popularity. hype_tier() is now its standing in the league's TOP CARDS
// race by that demand: 5 the top 5, 4 #6-20, 3 #21-60, 2 #61-150, 1 the rest / not in the league (the collectors, the pull shows' GRAIL!)
int hype_tier(int card);
constexpr double kStageStep = 0.94;  // r231: a family's form a stage below its last: x this (Charmander under Charizard: 0.94^2)
int demand_rank(int card);           // r231: its family's place in the TOP CARDS race (1 = the most wanted), 0 not ranked
double demand(int card);             // r231: its family's demand now (0.5 bench .. ~60 the league's best; 3 an average player)
std::string demand_why(int card);    // r231: why ("14 GOALS - GOLDEN BOOT LEADER - UNBEATEN CLUB"), "" none
struct TopCard {                     // r231: the TOP CARDS race (the market's TOP CARDS tab)
  int card = -1;                     // the family's card it plays as now
  int rank = 0, prevRank = 0;        // now, at the last matchday (0 new to the race)
  double demand = 0.0;
  long long price = 0;               // its BASE card's price
  float pct = 0.0f;                  // this matchday's change
  std::string why;
};
std::vector<TopCard> top_cards(int n);
// r230: the club its character plays for in the loaded career (franchise.h PortFranchiseClub*: r229's clubs), -1 none / not in the league
int club_of(int card);
// The card's best copy you own (its price), else its BASE price; -1 unknown (the binder's pockets)
long long card_value(int card);
// This matchday's change of a copy (percent, + up)
float change(const Variant& v);

// ---- live (the loaded career) ----
// Every frame on the game thread: reads the franchise (~every 0.5 s), switches markets with the career, re-prices on a new
// matchday (listings, offers, headlines), keeps the portfolio's history. True when what the page shows changed.
bool frame(double nowSeconds);
int generation();
// r206: the page wants drawing again for time alone (the deal flash fading, the last word expiring): at most ~8 a second
bool redraw_due();
// Offers waiting (the hub's badge) - r210: + your listings sold that you haven't seen yet
int offers_waiting();

// The page's buttons (game thread): dir 1 left 2 right 3 up 4 down (step: moved this frame), a / b / x / y pressed now. True:
// the market used it (B is the market's only while it asks / counters).
struct Input {
  int dir = 0;
  bool step = false, a = false, b = false, x = false, y = false;
};
bool input(const Input& in);
// r210: a pointer on the page (page units: 0..kW x 0..kH, as draw lays it out; click = its trigger pressed this frame). True: it is on
// something the market takes (the number pad's keys while it is up) - that trigger is the market's.
bool point(float x, float y, bool click);
bool pad_open();  // r210: the LIST FOR SALE / EDIT PRICE number pad is up
// Asking now (the hub's confirm pop-up: A YES / B NO): its question + line ("" none)
bool asking(std::string* question, std::string* line);
// The hub's hint bar: A's word (dim: nothing), X's, Y's ("" none)
void hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord);

// The page: kW x kH at (ox, oy) x k; `keep` holds the pictures the pieces point into
constexpr float kW = 1650.0f, kH = 958.0f;
void draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t);

// For the CARD MUSEUM page (xr_museum.cpp): your copies (as the market reads them), a card's name, a card drawn in miniature
// (unscaled pieces; `keep` holds its picture)
std::vector<Holding> my_copies();
std::string card_name(int card);
std::string card_key(int card);       // "" none
int card_index(const std::string& key);  // -1 none
void draw_mini(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, const Variant& v, float x0, float y0, float x1, float y1);

// ---- the PC proof ----
struct Stats {
  int day = 0, lines = 0, listings = 0, offers = 0;
  long long spent = 0, received = 0;
  int bought = 0, sold = 0;
  unsigned career = 0;        // r213: the career the market has loaded (0 none)
  int season = -1, week = -1;  // r213: the franchise week it last priced
};
Stats stats();
void debug_tab(int tab, int cursor);
// Offers / listings now (for the proof's exploit checks)
struct OfferView {
  uint32_t id;
  Variant v;
  std::string who;
  long long amount;
  int bidders;
  long long market;
  bool final;
};
std::vector<OfferView> offers();
struct ListingView {
  Variant v;
  std::string seller;
  long long ask, market;
  bool sold;
};
std::vector<ListingView> listings();
bool buy_listing(int i);                       // as BUY > A > YES
bool sell_now(const Variant& v);               // as MY CARDS > A > YES
bool accept_offer(uint32_t id);                // as OFFERS > A > YES
int counter_offer(uint32_t id, int percent);   // 1 sold at it, 2 they came back with a final offer, 3 they walked, 0 no
bool decline_offer(uint32_t id);
void reset_for_test();                         // forget the loaded market (the proof's fresh career)
// r210: your own listings (LIST FOR SALE): id, the copy, your asking price, the market now, matchdays listed, views, watching
struct MineView {
  uint32_t id;
  Variant v;
  long long ask, market;
  int days, views, watching;
};
std::vector<MineView> my_listings();
uint32_t list_for_sale(const Variant& v, long long ask);  // as MY CARDS > X > the pad > OK: its id, 0 refused
bool edit_listing(uint32_t id, long long ask);            // as MY LISTINGS > A > the pad > OK
bool cancel_listing(uint32_t id);                         // as MY LISTINGS > X > YES
struct SaleView {
  Variant v;
  std::string buyer;
  long long amount, market;
  int day;
  bool surprise;  // over the market: "A COLLECTOR PAID YOUR PRICE!"
};
std::vector<SaleView> my_sales();  // your listings that sold (newest first)
// the market's world: its boom / crash level now (0 calm), the biggest sale on record and its headline
double cycle_now();
long long record_sale(std::string* headline);
// ---- r219 (Lance 11-11, C:\StrikersMR\BIG-PULLS.md 4): STRIKERS ILLUSTRATED's THE STRIKERZ MARKET spread - what it shows, made once a
// matchday (when the market prices one, or loads a career) under the module's lock on the game thread; magazine() hands a copy to any
// thread in microseconds (the magazine's own drawing thread draws it - never the frame thread, never per frame)
struct MagMover {
  Variant v;            // the character's BASE card (as the page's HOT / COLD)
  std::string name;
  float pct = 0.0f;     // this matchday's change
  long long price = 0;
  std::vector<double> spark;  // its price over the matchdays kept (oldest first)
  std::string why;            // r231: the play behind the move ("14 GOALS - GOLDEN BOOT LEADER")
  int rank = 0;               // r231: its place in the TOP CARDS race
};
struct MagSale {        // a sale on this matchday's wire
  std::string what;     // "BLACK 1/1 GENGAR AUTO, GRADED 9"
  long long amount = 0;
  bool record = false;  // a new record
};
struct MagPull {        // a league collector's pull this matchday (never a BLACK 1/1: those are tracked one per card)
  std::string who;
  Variant v;
  long long value = 0;
};
struct MagMarket {
  bool valid = false;
  unsigned career = 0;
  int steps = 0, season = -1, week = -1;
  double mood = 0.0;                    // the boom / crash level now (0 calm, + a boom, - a crash: -0.8 .. 1)
  std::vector<double> moodHist;         // per matchday kept
  std::vector<double> indexA, indexJ;   // THE STRIKERZ 100 per matchday (oldest first): LANCINI TIZM, JAM '96 (the same characters)
  int basket = 0;                       // the characters in it
  std::vector<MagMover> hot, cold;      // 3 each, biggest first
  std::vector<MagMover> top;            // r231: the TOP CARDS race's first 5 (rank order)
  long long record = 0;
  std::string recordText;               // its headline ("A BLACK 1/1 ... JUST SOLD FOR ... COINS")
  bool recordNew = false;               // it fell this matchday
  std::vector<MagSale> sales;           // this matchday's wire (biggest first, up to 3)
  std::vector<MagPull> pulls;           // the league's best pulls this matchday (biggest first, up to 4)
  std::vector<long long> portfolio;     // your binder's market value per matchday (oldest first; the last = this matchday)
  int copies = 0;                       // your copies
  Variant best;                         // your most valuable copy (card -1 none)
  long long bestValue = 0;
};
bool magazine(MagMarket* out);  // false: no career loaded / nothing priced yet
void debug_pad(int stage);  // the proof's pictures: 0 shut, 1 LIST FOR SALE open on MY CARDS' cursor, 2 typed, 3 EDIT on MY LISTINGS

}  // namespace xrmarket
