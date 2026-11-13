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
  // first form .. stages - 1 the last (a branch's forms: each its depth). A family's cards all move with the one player. r239: no more
  // "the last form dearer" - the FIRST form is his ROOKIE CARD (kRookie*: a premium that grows with his legacy). Defaults: a card alone
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
  // r242: at grading - the matches still to go (1 / 2); its grade is 0 then (not rolled until it's back: xr_cards slabs_tick), so nothing here
  // can show, price, filter or sort by it - its value is the raw copy's
  int left = 0;
};
// r242: "AT GRADING - 2 MATCHES" / "AT GRADING - 1 MATCH" (a copy at grading's words everywhere, in place of a grade)
std::string grading_words(const Holding& h);
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
  // r240: SELL ALL SHOWN - many copies out of the binder at once (the binder saved ONCE, not once a copy: a 1,000-card sale would write the
  // binder file 1,000 times through `take`). took[i] = 1: copies[i] came out. Called under the market's lock (market -> binder), as take.
  // Null: `take` once a copy
  int (*take_many)(const std::vector<Variant>& copies, std::vector<uint8_t>* took) = nullptr;
  // r241: CARDS > GRADING's list (below) - a copy's TIZM GRADING cost in BIT COINS (pure: no lock), and THAT exact copy sent (sealed in a slab,
  // its cost taken, the binder saved): 0 sent (*cert its slab's cert), 1 not enough coins, 2 not yours (any more), 3 locked (listed / in an
  // open collector trade / in your museum). Called under the market's lock (market -> binder), as take. Null: GRADING sends nothing
  int (*grade_cost)(const Variant& v) = nullptr;
  int (*grade_send)(const Variant& v, uint32_t* cert) = nullptr;
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
// (r238: 0.90 -> 0.93 - SELL NOW pays 90% now: a collector's copy bought at its floor and sold back at once still loses 3%+)
constexpr double kListFloor = 0.93;  // no CPU seller lists a copy under this much of its market (over SELL NOW's kBid: no instant loop)
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
// r239: the FAMILY chip - off (r235's): a name finds that form's copies ("TOGEPI" = TOGEPI's); on: every form of a family a form of which
// the name finds ("TOGEPI" = TOGEPI + TOGETIC + TOGEKISS copies). The query / filters kept
void search_family(bool on);
bool search_family_on();
std::vector<SearchRow> search_results();
bool search_buy(int row, std::string* line);  // as BUY > A > YES on that row
bool search_open();                           // the search panel (the letter grid) is up: its pointer + trigger (xr_quest)
void search_close();                          // the page left (xr_cards): the panel down, the query kept
bool watch_toggle(int card);                  // WATCH / UNWATCH its player (its family): true now watched
std::vector<std::string> watched();           // the watched families' names
std::vector<std::string> recent_searches();
void debug_search(int stage, int key);        // the proof's pictures: 0 shut, 1 the panel up (the key under the cursor), 2 a row asked

// ---- r238 (Lance 11-12: "this aint ebay this is fast flipping activity!"): THE LIVE MARKET + SALES + HOLD BEFORE YOU BUY
// REAL-TIME TICKS: while the franchise hub is open with a career (xr_cards tells it, every frame, no lock), every kTickSecs of that time is a
// market TICK on top of the matchdays - your listings looked at (a fair price sells at once: sell_chance), watchers turning into offers, the
// collectors buying / listing / ripping a little (xr_trade follows Stats::tick), the wire. Deterministic per career / matchday / tick.
constexpr double kTickSecs = 90.0;
void set_live(bool hubOpen);  // any thread (atomic)
void debug_tick();            // the proof: a tick now (as kTickSecs of hub time would)
double sell_chance(const Variant& v, long long ask);  // a listing of yours at `ask`: its chance to sell at one tick (a matchday is one)
// SALES: every completed sale (yours, the collectors', the dealer's, the wire's) kept per player (a LANCINI TIZM card + its JAM '96 and
// EXQUISITE twins: one history), by finish group, bounded (kSalesPerGroup a group, kSalesMax in all); saved beside the market
// (STRIKERS_HOME/cards/sales-<career>.txt). CARDS > MARKET > SALES shows them (eBay / 130point sold listings).
constexpr int kSaleGroups = 8;  // BASE, SILVER, GOLD /10, BLACK 1/1, CASE HITS, GRADED, JAM '96, EXQUISITE
constexpr int kSalesPerGroup = 12, kSalesMax = 2400;
struct SaleRec {
  Variant v;
  long long price = 0;
  int step = 0, season = 0, week = 0;  // the market's matchday count, the franchise week it sold in
  int kind = 0;                        // 0 the collectors' world, 1 yours sold, 2 you bought, 3 the dealer sold it
  uint32_t seq = 0;                    // its order (newest = highest)
  std::string seller, buyer;
};
// any thread; xr_trade calls it under its own lock (trade -> market, never back)
void record_sale(const Variant& v, long long price, const char* seller, const char* buyer, int kind);
std::vector<SaleRec> sales_of(int card);  // its player's sales (card or a twin), newest first
int sales_count();
int sale_group(const Variant& v);         // its finish group (0 .. kSaleGroups - 1)
// NOTICES: a sale of yours / an offer while you're in the hub - a pop-up (xr_quest draws it off the frame thread). False: none waiting
bool take_notice(std::string* head, std::string* title, std::string* line);
// r242: another module's pop-up in the same queue (xr_cards: TIZM GRADING's mail - "YOUR SLAB IS BACK", never its grade). Any thread, no lock held
void post_notice(const std::string& head, const std::string& title, const std::string& line);
// HOLD BEFORE YOU BUY (xr_cards: a grip with its pointer on BUY's list / the focused copy): the copy under (x, y) on the page (page units),
// focused; true: v / ask / who are it. inspect_buy: A on the copy in your hand - the BUY confirm for that very copy (false: it's gone)
bool inspect_at(float x, float y, Variant* v, long long* ask, std::string* who);
bool inspect_buy(const Variant& v, long long ask);
void debug_sales(int card, int group = -1);  // the proof's pictures: the SALES tab on `card`'s player (-1 the list), a finish group focused
// r239: SALES groups a FAMILY (one player: an evolution line, a hero's suits, an ORIGINAL's uniforms) - one row a player on the list, his
// page with a FORM tab each (ALL FORMS, TOGEPI, TOGETIC, TOGEKISS: X cycles them) over the finishes. form: -1 ALL FORMS, else that form's card
void debug_sales_form(int form);
std::vector<int> sales_players();            // the SALES list's players (each its family's card shown), as built now
std::vector<int> sales_forms(int card);      // the forms its player's page shows a tab for (its family's main cards, first form first)
std::vector<SaleRec> sales_of_family(int card, int form = -1);  // its player's sales over every form (form >= 0: that form's), newest first

// ---- r240 (Lance 11-13: "work on a way to sell better, like more filtered. even add a filter actually!! so i can sell by rarity, by player, by
// type of card, by set of card, etc"): CARDS > MARKET > SELL as easy as BUY. X = the same SEARCH panel (the letter grid, recent searches, the
// FAMILY chip) with SELL's own filters - SET, FINISH (rarity: BASE / SILVER / GOLD /10 / /25 & UNDER / 1/1 / each EXQUISITE tier), TYPE
// (BASE, PARALLEL, CASE HIT, AUTO, PATCH, GRADED, SHINY, ROOKIE = a first form / an EXQUISITE rookie), STATUS (NOT LISTED / LISTED / SPARES),
// PRICE band - and SORTs (value, this week's gainers / losers, newest, trending, serial number). Each row: the card, its finish / number /
// grade, the player's season, its market value + this week's move, what SELL NOW pays (90%) or why it's locked (LISTED, IN MUSEUM, IN A TRADE,
// AT GRADING). BULK: SELL ALL SHOWN / LIST ALL SHOWN (at 95 / 100 / 105 / 110% of the market) with the hub's confirm showing how many + the
// total first. Never your LAST copy of a card (a card = the same card in the same finish: BASE TOGEPI, GOLD /10 TOGEPI ...; the most valuable
// copy is the one kept - a copy in your museum or at grading keeps it for you) unless LAST COPY: SELL TOO is on; never a listed / museum /
// in-a-trade / at-grading copy. A = SELL NOW (one), Y = LIST FOR SALE (one: the pad - r238's Y SALES moved: X is SEARCH now).
enum { kSFinAny = 0, kSFinBase, kSFinSilver, kSFinGold, kSFin25, kSFinOne, kSFinExq, kSFins = kSFinExq + 10 };  // (kSFinExq + tier - 1: GALLERY .. PATCH AUTO)
enum { kSTypeAny = 0, kSTypeBase, kSTypeParallel, kSTypeHit, kSTypeAuto, kSTypePatch, kSTypeGraded, kSTypeShiny, kSTypeRookie, kSTypes };
enum { kSStatAll = 0, kSStatFree, kSStatListed, kSStatSpare, kSStats };
enum { kSPriceAny = 0, kSPrice100, kSPrice1K, kSPrice10K, kSPrice100K, kSPrice1M, kSPriceTop, kSPrices };
enum { kSSortHigh = 0, kSSortLow, kSSortUp, kSSortDown, kSSortNew, kSSortHot, kSSortSerial, kSSorts };
constexpr int kSListPct[4] = {95, 100, 105, 110};  // LIST ALL SHOWN's choices (% of each copy's market)
struct SellFilter {
  std::string query;
  int set = kFSetAll, finish = kSFinAny, type = kSTypeAny, status = kSStatAll, price = kSPriceAny, sort = kSSortHigh;
  bool family = false;
};
void sell_set(const SellFilter& f);  // as the SELL panel's keys (the proof)
SellFilter sell_filter();
struct SellRow {                     // one row of SELL as shown
  Holding h;                         // the copies (count) of one kind of copy
  long long market = 0, bid = 0;     // each
  float chg = 0.0f;                  // this week's (this matchday's) move, %
  int listed = 0, heldOut = 0, museum = 0, free = 0, spare = 0;  // free: sellable now; spare: free beyond your last copy of the card
  int seen = 0, rank = 0;            // the matchday it first showed up in your binder; its family's TOP CARDS place
};
std::vector<SellRow> sell_rows();
struct BulkPlan {                    // what SELL ALL SHOWN / LIST ALL SHOWN does now (the confirm's numbers = what happens)
  int copies = 0, rows = 0;          // copies sold / listed, rows they come from
  int locked = 0, kept = 0, capped = 0;  // shown copies skipped: locked (listed, museum, trade, grading), your last copies kept, over 20 listings
  long long total = 0, market = 0;   // coins paid (SELL NOW, each 90% rounded down) / asked (LIST: each pct% of its market); their market
};
BulkPlan sell_plan(bool list, int pct, bool lastToo);
int sell_all_shown(bool lastToo, long long* coins);       // as SELL ALL SHOWN > YES: copies sold, coins in
int list_all_shown(int pct, bool lastToo, long long* asked);  // as LIST ALL SHOWN > YES: listings made, their asks summed
void set_museum(const std::vector<Variant>& copies);      // xr_cards (game thread, no lock held): the CARD MUSEUM's copies - never bulk-sold
bool inspect_mine_at(float x, float y, Variant* v, long long* market);  // HOLD THE CARD on SELL: your copy under (x, y) (page units), focused
bool inspect_sell(const Variant& v);                      // A on your copy in your hand: SELL NOW's confirm for it (false: it can't sell now)
void debug_sell(int stage, int key);  // the proof: 0 the panel shut, 1 up (the cursor on `key`), 2 `key` pressed (a pointer's click), 3 B

// ---- r241 (Lance 11-13: "i cant grade an emerald /25 iron man and other cards in my collection but i have them in my collection"): CARDS >
// GRADING picks THE EXACT COPY, as SELL does - every copy you own (LANCINI TIZM, JAM '96, EXQUISITE: every tier / auto / patch, case hits,
// shinies, any form or suit whoever plays now) one row a kind of copy (its finish, its number), its value, its cost; X = the SEARCH panel
// (the letter grid, recent searches, FAMILY) with GRADING's chips - SET, FINISH, TYPE, STATUS (ALL / CAN SEND / LOCKED), SORT (VALUE, FINISH,
// NEWEST); A = SEND TO GRADING (the hub's confirm: the copy, its cost, back after kGradeBackMatches), again and again. A copy that can't go
// says why: AT GRADING, GRADED (in a slab already), LISTED, IN A TRADE, IN MUSEUM. xr_cards draws the page through grade_draw (its page area,
// as the market's) and hands it the page's buttons (grade_input); its Y is the page's own (YOUR SLABS). The list is made on a change only
// (SELL's table: your binder, the prices, your listings / trades / museum), a key press = filter + sort.
constexpr int kGradeBackMatches = 2;  // (xr_cards kGradeMatches - the confirm's words)
struct Input;                         // (below: the page's buttons)
enum { kGStatAll = 0, kGStatCan, kGStatLocked, kGStats };
enum { kGSortHigh = 0, kGSortLow, kGSortFinish, kGSortNew, kGSorts };
struct GradeFilter {
  std::string query;
  int set = kFSetAll, finish = kSFinAny, type = kSTypeAny, status = kGStatAll, sort = kGSortHigh;
  bool family = false;
};
struct GradeRow {                    // one row of GRADING as shown
  Holding h;                         // the copies (count) of one kind of copy
  long long market = 0;
  int cost = 0;                      // TIZM GRADING's cost (each)
  int free = 0;                      // copies that can go now
  std::string lock;                  // "" free, else why not: AT GRADING / GRADED / LISTED / IN MUSEUM / IN A TRADE
};
void grade_draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t);
bool grade_input(const Input& in);   // (game thread) the page's buttons but Y
void grade_hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord);
bool grade_asking(std::string* q, std::string* line);
bool grade_busy();                   // its search panel / its send confirm up (xr_cards: Y stays with them)
int search_mode();                   // the search panel up: 0 no, 1 the MARKET's (BUY / SELL / SALES), 2 GRADING's (no lock: any thread)
void grade_leave();                  // (game thread) the GRADING page left: a send asking is dropped (cheap: an atomic unless one asks)
void grade_set(const GradeFilter& f);  // as GRADING's panel keys (the proof)
GradeFilter grade_filter();
std::vector<GradeRow> grade_rows();
int grade_cursor();
void grade_debug(int stage, int key);  // the proof: 0 panel shut, 1 up (cursor on `key`), 2 `key` pressed, 3 the cursor on row `key`

// ---- r242 (Lance 11-13: "see the hottest 20, like a power rankings type vibe. see if you have any, what variations of the card you may have,
// and show chart/graph of price action of said card"): CARDS > HOT 20 (after MARKET) - the r231 TOP CARDS race's first 20 (a family = one
// player, shown as the form he plays as now): rank + its move since the last matchday, his club's logo, the play behind it, price + this
// week's %, YOU HAVE n. The focused player: his price over the matchdays kept in a finish (stick LEFT / RIGHT: BASE, SILVER, GOLD /10, 1/1,
// JAM '96, each EXQUISITE tier) + HIGH / LOW + this week; YOU HAVE (your copies of his family: set, finish / number, grade or AT GRADING,
// value); A = his actions (BUY / SELL / SALES / WATCH: the stick picks, A does it, B back). xr_cards draws it through hot_draw (the market's
// page area) and hands it the page's buttons (hot_input); BUY / SELL / SALES leave for MARKET (hot_take_jump). On change only.
struct HotRow {                      // one HOT 20 row (the proof)
  int card = -1, rank = 0, prevRank = 0, club = -1;
  long long price = 0;
  float pct = 0.0f;
  std::string why, family;           // the play behind it; "HIS TOGEPI TOO" (his first form when he plays as a later one)
  std::vector<Holding> yours;        // your copies of his family, the most valuable first
  std::vector<std::string> finNames; // the chart's finishes
  std::vector<std::vector<long long>> charts;  // each finish's price per matchday kept, oldest first
};
void set_logo_source(const uint8_t* (*logo)(int club, int* w, int* h));  // a club's logo (xr_quest: the sportsbook's own)
void hot_draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t);
bool hot_input(const Input& in);     // (game thread) the page's buttons
void hot_hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord);
int hot_take_jump();                 // 1: an action left for MARKET (its tab set) - xr_cards goes there (once)
bool hot_busy();                     // its actions focused (B is the page's: back to the list)
std::vector<HotRow> hot_rows();
void hot_debug(int cursor, int fin, int act);  // the proof: the row, the chart's finish, the action focused (-1 the list)
int hot_state(int* fin, int* act);   // the row (+ the finish, the action)
int market_tab();                    // the market's tab now (the proof: where an action left it)

// ---- the model (pure: the PC proof calls these) ----
// r210 (Lance 11-10: "100 coins isnt enough for collectibles! ... think of these as the real pokemon market irl! its crazy!!!"):
// prices span a few coins (bulk commons) to tens of millions (a GRAIL character's BLACK 1/1 autograph) - MARKET-PRICES.md. Every
// price / bid / ask / offer is 64-bit (xrcoin::Coins), held under xrcoin::kMax.
constexpr double kBid = 0.90;       // collectors buy from you at this much of the market (r210: a card worth 20,000+; bulk less: bid_ratio) - r234: 0.82 -> 0.88 (Lance 11-12: "its def a flippers market" - a real pop pays after the spread; buy 1.12 / sell 0.88: an instant loop still gets back 79%)
                                    // r238 (Lance 11-12: "i literally saw one where the market value was 108 and the sell now was like 18... sell now needs to be like
                                    // 90% value of the card"): 0.90 for EVERY card - r210's bulk curve (20% under 200 coins) is gone (bid_ratio = kBid)
constexpr double kBidBulk = 0.20;   // (r210-r237's bulk rate - no longer used: kept for the record)
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
constexpr double kStageStep = 0.94;  // (r231-r238: a form a stage below its last x this - no longer used since r239: kept for the record)
// r239 (Lance 11-13: "it wouldn't be outlandish for a rookie Michael Jordan to be worth more or the same as a Michael Jordan in his prime
// year or his last year"): a family's FIRST form is the player's ROOKIE CARD. Its play part x (1 + R), R = proven x (kRookieBase +
// kRookieScarce x s) - proven = tanh(max(0, his LEGACY level - kRookieFloor) / kRookieLegacy) (awards, titles, a strong season carried in,
// career goals + assists: 0 a nobody .. ~1 a legend), s the copy's scarcity (0 BASE .. 1 a 1/1). The form he plays as NOW x1 (the hot
// "now" demand: every form carries his whole demand), a form between the two in proportion, a form he hasn't reached x1. No legacy: every
// form the same price; a legend's rookie BASE ~+14%, his rookie 1/1 ~+43% over his prime form. The r234 floors read no form (unchanged)
constexpr double kRookieBase = 0.15, kRookieScarce = 0.30, kRookieLegacy = 1.0, kRookieFloor = 0.10;
int form_gap(int card);              // r239: how many forms below the one its family plays as now (0: that form, a later one, no family)
double rookie_mult(int card, double s);  // r239: the rookie premium on its play part (1 none; s 0 BASE .. 1 a 1/1) - the proof reads it
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
  int tick = 0;                // r238: real-time ticks so far (this session's career: xr_trade's hobby follows it)
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
