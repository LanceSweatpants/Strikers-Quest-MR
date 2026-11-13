// strikers-quest r199 (Lance 2026-11-08, C:\StrikersMR\SPORTSBOOK.md): the TIZM SPORTSBOOK - bet the loaded franchise
// career's own TIZM COINS (the binder's wallet; a fictional currency, never real money, nothing to buy) on the league.
//
//   LEAGUE LINES: every other game of the matchday - MONEY LINE (who wins: there are no draws, a tie goes to golden goal)
//     and the SPREAD (-1.5 / +1.5 goals) - priced from the clubs' strength exactly as franchise.cpp's sim_game plays them
//     out (ratings, levels, both captains, the nemesis edge, the difficulty push, home 1.06 / away 0.96) leaned a little by
//     form (the streaks), with a small house edge, in American odds (-150 / +130) and the coins it pays.
//   WIN BY: your own game - win by 1 / 2 / 3 / 4+ (a bet on yourself; no "lose" line), priced from the same model blended
//     with how you have really been winning this season, a bigger edge (a prop). r225 (Lance: "nobody even bets like that
//     where they say exactly 2, its always 2 or more"): WIN BY 1+ / 2+ / 3+ / 4+ = win by AT LEAST that many, a real book's
//     -0.5 / -1.5 / -2.5 / -3.5 lines (r199-r224: by exactly 1 / 2 / 3, 4+ the only "or more").
//   Stakes 25 / 50 / 100 / 250 / MAX (the balance). Bets lock at the matchday's kickoff (your match - or a CPU game you
//     watch - starting, or any game of the day in), settle when the games are in (your result + the simulated ones) and pay
//     into the binder; a game gone (a day sim'd past without it) is refunded. History + profit on MY BETS; a big win can
//     make the league news (not in co-op / online leagues: their news is shared).
//
// One model + one drawing for both ways in: the hub's CLUB > SPORTSBOOK page and the optional cabinet in your room
// (xr_quest.cpp: its screen is this same drawing). Deterministic: the prices are a function of the career's state only
// (no random numbers at all); the bets never touch the career (its rng, its results). Online league / co-op: the bets are
// this headset's own (STRIKERS_HOME/cards/book-<career>.txt beside the binder), never sent.
//
// Pure C++ apart from franchise.h + xr_text.h (no platform calls; the wallet and sounds come in as function pointers), so
// the same files run in the PC proof (tools/outside/sportsbook199: wasm32 + node). Threads: frame / input / place on the
// game thread; draw on any thread (it copies a snapshot under the module's mutex).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../../include/port/franchise.h"
#include "xr_coins.h"  // r208: TIZM COINS are 64-bit (xrcoin::Coins), saturating at xrcoin::kMax
#include "xr_text.h"

namespace xrbook {

// ---- the model (pure functions: the PC proof calls these directly) ----
// r199 round 3: + FUTURES (season-long: the champion, MVP, golden boot, rookie of the year - awards night's own picks); a futures
// ticket is a single (never in a parlay), open in the regular season, settled at awards night, collected like any win
// r207 (Lance: CRAZY PARLAYS): + kSgp - a SAME-GAME pair on one ticket leg (that game's MONEY LINE + its SPREAD, priced from
// their JOINT chance - e.g. home ML + home -1.5 = P(home by 2+), never the product); futures may be parlay legs now
enum Market { kMoneyLine = 0, kSpread = 1, kWinBy = 2, kChampion = 3, kMvp = 4, kBoot = 5, kRookie = 6, kSgp = 7 };
inline bool is_future_market(int m) { return m >= 3 && m <= 6; }
constexpr double kVigFutures = 0.25;  // a futures board's overround per option (a long book)
constexpr double kVigLines = 0.045;  // the overround on a two-way line (each side's price = its chance x 1.045)
constexpr double kVigWinBy = 0.12;   // WIN BY (a prop, four outcomes and "lose" not offered)
constexpr double kWinByPrior = 6.0;  // WIN BY: the model counts as this many of your games
constexpr double kWinByStep = 0.72;  // WIN BY: a margin posted at least 1 / 0.72 likelier than the next bigger one
constexpr double kFormLean = 0.012;  // a win-streak game shortens that club's price by this much more edge (3 games at most)
constexpr int kStakes[4] = {25, 50, 100, 250};  // + MAX (the balance) + r207 CUSTOM (typed on the number pad: 1 .. the balance)
constexpr int kStakeMax = 4, kStakeCustom = 5, kStakeChoices = 6;
// r208 (Lance 11-10: "dont cap the ticket wins"): NO win cap (r207's kMaxWin 1,000,000 is gone) - a ticket pays its full stake x
// multiplier in 64-bit coins, held only at xrcoin::kMax (999,999,999,999,999: never wrapping)
// r207: a long parlay's compounded edge is capped: the legs' return to a fair-priced player (the product of chance x decimal over
// every leg but WIN BY) never falls under this - past it the payout is boosted back up to it (a 30% house edge at most; a WIN BY
// leg - a bet on yourself - keeps its own edge outside the cap)
constexpr double kParlayFloor = 0.70;

struct Probs {
  double homeWin = 0.5;                 // money line (golden goal included)
  double homeBy2 = 0.25, awayBy2 = 0.25;  // the margin 2+ either way (what -1.5 needs)
  double margin[5] = {0, 0, 0, 0, 0};   // the home side's win by 1, 2, 3, 4+ (index 1-4); [0] its loss
};
// r225: the chance of winning by AT LEAST k (1..4) from a side's margin buckets (margin[k] + ... + margin[4])
inline double at_least(const Probs& p, int k) {
  double s = 0.0;
  for (int j = k < 1 ? 1 : k; j <= 4; ++j) s += p.margin[j];
  return s;
}
// A game's chances from its goal rates (poisson, truncated as franchise.cpp's draws are) and its golden-goal split sh / sa
Probs game_probs(double lamH, double lamA, double sh, double sa);
// The American price for a chance p with overround `vig`, rounded toward the house (never pays more than the fair line
// x 1 / (1 + vig)); clamped to -2500 .. +2500
int american(double p, double vig);
// The coins a winning stake returns (stake included) at that price: floor(stake x decimal) (r208: 64-bit, held at xrcoin::kMax)
long long payout(long long stake, int american);
// "+150" / "-180"
std::string price_text(int american);

struct Pick {  // a priced selection on the board
  int game = -1;           // index into the day's games
  int market = kMoneyLine;
  int side = 0;            // money line / spread: 0 home 1 away; WIN BY: 1..4 (r225: k = by k or more)
  float spread = 0.0f;     // the side's spread (-1.5 / +1.5); 0 money line / WIN BY
  int american = 100;
  double fair = 0.5;       // the model's chance (unvigged, unleaned): the proof's house edge
  double posted = 0.5;     // the leaned chance the price was made from
  // a future's: its club (CHAMPION) or player (id + name, his club)
  int club = -1;
  std::string who, whoName, stat;
  int week = -1;           // r207: the matchday its game is on (-1: a future / today's board as r199)
  int side2 = 0;           // r207: kSgp - the spread's side (0 home 1 away; `spread` its spread)
};
// The season's futures from franchise.cpp's races (deterministic): each market's top 8, likeliest first
void price_futures(const PortFranchiseBookFutures& f, std::vector<Pick> out[4]);
struct Fixture {
  int home = -1, away = -1, series = -1;
  bool mine = false;
  int streakH = 0, streakA = 0;
  Pick ml[2], sp[2];       // [0] home [1] away
  Pick winBy[5];           // mine only: [1..4]
  Probs fair;
};
// The day's board from franchise.cpp's numbers (deterministic: the same day, the same prices)
void price_day(const PortFranchiseBookDay& d, std::vector<Fixture>* out);
// r207: a SAME-GAME pair's JOINT chance - `mlSide` wins (0 home 1 away) AND `spSide` covers `spread` (+-1.5) - straight from the
// margin's distribution (home by 2+, by 1, away by 1, away by 2+: no draws, a tie goes to golden goal). 0 = they can't both win.
double sgp_joint(const Probs& p, int mlSide, int spSide, float spread);
// r207: the pair as one priced pick (market kSgp, side = the money line's, side2 + spread = the spread's), priced from the joint
// chance with a single line's edge (leaned the house's way by the money line side's form). *why: 0 fine, 1 can't both win (or
// a long shot off the board), 2 a sure thing (off the board)
Pick sgp_pick(const Fixture& f, int mlSide, int spSide, float spread, int* why);

// r199 round 2 (Lance: COLLECT TO GET PAID, PARLAYS, PRINTED SLIPS): a ticket is one slip of 1-10 legs (one a game) -
// a single (1 leg) or a PARLAY (all its legs must win; the multiplier = the product of the legs' decimal prices, each with
// its own house edge; a void leg - its game gone - drops out of it). Settled winners (and refunds) WAIT until collected (the
// hub page: a small count-up; the machine: the full show).
// r207 (Lance: CRAZY PARLAYS): 25 legs (r199-r206: 10), from any upcoming matchday (each leg locks at its own game's kickoff),
// futures among them, a game's MONEY LINE + SPREAD together as one same-game leg (kSgp)
constexpr int kMaxLegs = 25;
struct Leg {
  int home = -1, away = -1, market = kMoneyLine, side = 0;
  float spread = 0.0f;  // the side's spread; WIN BY: +1 you were home, -1 away
  int american = 100;
  int state = 0;        // 0 open, 1 won, 2 lost, 3 void
  int hg = -1, ag = -1;
  bool mine = false;
  int club = -1;               // a future: its club
  std::string who, whoName;    // a future: its player's id / name
  // r207 (saved after r199's fields: an older book's legs load with these as here): the matchday its game is on (-1: the
  // ticket's own week, r199-r206), the model's chance when it was placed (0 unknown: no parlay cap), kSgp's spread side
  int week = -1;
  double fair = 0.0;
  int side2 = 0;
  // r225: a WIN BY leg means "by `side` OR MORE" (saved as a 17th field: 1). A leg of an older book (r199-r224, no 17th
  // field) keeps the meaning it was bought with - by exactly 1 / 2 / 3, 4+ - its words and its settling both (an r224 build
  // reading an r225 book ignores the field: its open WIN BY legs would settle exact there)
  bool orMore = false;
};
struct Bet {
  uint32_t id = 0;      // the ticket number
  int season = 0, week = 0;
  std::vector<Leg> legs;
  long long stake = 0, pays = 0;  // pays: what it returns if every leg wins (stake included); r208: 64-bit coins
  int state = 0;            // 0 open, 1 won, 2 lost, 3 void (the stake back)
  long long paid = 0;       // what it returns now it's settled (won: the stake x the won legs' multiplier; void: the stake)
  bool collected = false;   // paid into the binder
  bool machine = false;     // placed at the machine (its slip printed there)
  long long placed = 0;     // unix time
};
double decimal(int american);  // 1 + a / 100 or 1 + 100 / -a
// the legs' product (settledOnly: the won legs only; else every leg not void) x r207's parlay cap boost (parlay_boost)
double multiplier(const Bet& b, bool settledOnly);
// r207: the boost a parlay's legs get so its house edge never passes 1 - kParlayFloor (1 = none): from each leg's chance x decimal
// (WIN BY legs and legs of an older book - fair 0 - left out; one leg alone never boosted)
double parlay_boost(const std::vector<Leg>& legs, bool settledOnly);
// floor(stake x mult) in 64-bit maths (r208: no win cap - only xrcoin::kMax, never wrapping)
long long parlay_pays(long long stake, double mult);
// A leg on a final score (golden goal in it): 1 won, 2 lost
int leg_state(const Leg& l, int hg, int ag);
// A ticket from its legs' states: 0 still open, 1 won, 2 lost, 3 void; *paid what it returns
int ticket_state(const Bet& b, long long* paid);
// "DEATH MOUNTAIN ML -150", "MUTE CITY +1.5 -180", "WIN BY 2+ (-1.5) +150" (r225; an older book's exact leg "WIN BY 2 +320")
std::string leg_words(const Leg& l);

// ---- the live sportsbook (the loaded career) ----
struct Wallet {
  unsigned (*career)() = nullptr;                     // the binder loaded (its career seed), 0 none
  // r208: 64-bit coins (add saturates at xrcoin::kMax)
  long long (*balance)() = nullptr;
  bool (*spend)(unsigned career, long long coins, const char* why) = nullptr;
  bool (*add)(unsigned career, long long coins, const char* why) = nullptr;
  // "book_coin" (a bet), "book_tick" (the cursor: a slot machine's tick), "book_ding" (a leg added), "book_print" (the
  // machine's printer), "book_collect" (the hub's count-up jingle), "book_jackpot" + "book_shower" (the machine's payout)
  bool (*sound)(const char* name, float gain) = nullptr;
  // r202: the casino's MEGA JACKPOT pays a free TIZM pack (xr_cards: rolled + waiting with the rest); null: its price in coins
  bool (*pack)(unsigned career, int packs, const char* why) = nullptr;
  // r202: the machine's win / lose MUSIC (real CC0 clips, assets/franchise/cardsounds/casino_music_*.wav) at the SOUND >
  // EFFECTS volume; null: through `sound`
  bool (*music)(const char* name, float gain) = nullptr;
};
void set_wallet(const Wallet& w);
// A club's logo for the drawing (any thread; straight-alpha RGBA, null none)
void set_logo_source(const uint8_t* (*logo)(int club, int* w, int* h));

// Every frame on the game thread: reads the day (every ~0.4 s), switches books with the career, locks at kickoff, settles
// what's in (never pays: collecting does). True when what the drawing shows changed.
bool frame(double nowSeconds);
// The drawing's counter (moves whenever anything shown changes); animating: a count-up / flash / multiplier climbing (the
// hub redraws its page ~15 a second while it is)
int generation();
bool animating();
// Winnings settled and waiting to be collected (coins), how many tickets
long long ready_to_collect(int* tickets);

// The page's buttons (game thread): dir 1 left 2 right 3 up 4 down (step: moved this frame), a / b / x / y pressed now.
// `cabinet`: at the machine (its SLIPS board prints; Y steps the stake). True: the sportsbook used it (B on the board isn't
// used on the hub page: the hub goes back to its rail).
struct Input {
  int dir = 0;
  bool step = false, a = false, b = false, x = false, y = false;
};
bool input(const Input& in, bool cabinet);
// r204: where the selection is now (the sportsbook's + the casino's; equal before and after a step = it moved nothing: no tick)
uint64_t selection_key();
// r204: seconds since the selection last really moved (the cursor's pop)
float cursor_move_ago();
// r204 (Lance's r203 test: "the highlight doesn't move at all"): THE CURSOR on every board, the hub page's and the machine's (the
// casino's too) - a thick glowing frame (`rgb`, a white-hot core inside it, a bright bar down its left edge) that POPS out as it
// lands (0.22 s after a move) and sinks + flashes white as it is picked (0.28 s). layer 0: its glow (under the item), 1: the
// frame (over it). moveAgo / pressAgo: seconds (anything big: none).
void cursor_frame(std::vector<xrtext::BoardPiece>& v, int layer, float x0, float y0, float x1, float y1, float r, float t, float moveAgo,
                  float pressAgo, uint32_t rgb = 0x2EF2FF);
// r205 (Lance's r204 test: "any time I do move, it gets CHOPPY" - every move redrew the machine's whole screen, then 20 a second
// while the cursor popped): THE MACHINE'S CURSOR IS AN OVERLAY. While a sink is set on the drawing thread, cursor_frame draws
// nothing and notes the cursor's box (draw() turns it into the picture's pixels: x0..y1, r, k = the scale), and the boards draw
// their items UNLIT (no cyan fill under the cursor): the picture doesn't change when the cursor moves, so a move redraws nothing -
// xr_quest.cpp lays the cursor (xr_screen.cpp's picture of it) over the screen as a quad of its own. The hub page sets no sink
// (its cursor stays in its picture).
struct CursorBox {
  bool on = false;
  float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f, r = 0.0f;
  uint32_t rgb = 0x2EF2FF;
  bool glow = false;  // its glow (layer 0) asked for too
  float k = 1.0f;     // the picture's pixels per base unit
};
void set_cursor_sink(CursorBox* sink);  // (this thread; null: off)
CursorBox* cursor_sink();
// r205: the cursor's pop / press now (the casino's while a casino screen is up): seconds since each (big: none)
void cursor_anim(float* moveAgo, float* pressAgo);
// r205: animating() without the cursor's own pop / press (the machine's screen: its overlay does those, not a redraw)
bool animating_board();
// r204: a pointer's target among hit boxes (x0 y0 x1 y1 first): every box grown by 28 (the nearest wins; inside = 0), and the
// box it is on keeps it while the ray stays within 36 of it (`hover`: x0 y0 x1 y1 + 1 when set) - a hand's tremor never
// flickers between neighbours. -1: nothing near.
template <class H>
int hit_at(const std::vector<H>& hits, float x, float y, float hover[5]) {
  constexpr float kGrow = 28.0f, kStick = 36.0f;
  if (hover[4] > 0.5f && x > hover[0] - kStick && x < hover[2] + kStick && y > hover[1] - kStick && y < hover[3] + kStick)
    for (size_t i = 0; i < hits.size(); ++i) {
      const H& h = hits[i];
      if (h.x0 > hover[0] - 0.5f && h.x0 < hover[0] + 0.5f && h.y0 > hover[1] - 0.5f && h.y0 < hover[1] + 0.5f && h.x1 > hover[2] - 0.5f &&
          h.x1 < hover[2] + 0.5f && h.y1 > hover[3] - 0.5f && h.y1 < hover[3] + 0.5f)
        return static_cast<int>(i);
    }
  int best = -1;
  float bd = kGrow * kGrow + 1e-3f;
  for (size_t i = 0; i < hits.size(); ++i) {
    const H& h = hits[i];
    const float dx = x < h.x0 ? h.x0 - x : (x > h.x1 ? x - h.x1 : 0.0f), dy = y < h.y0 ? h.y0 - y : (y > h.y1 ? y - h.y1 : 0.0f);
    const float d2 = dx * dx + dy * dy;
    if (d2 < bd) bd = d2, best = static_cast<int>(i);
  }
  if (best >= 0) {
    const H& h = hits[static_cast<size_t>(best)];
    hover[0] = h.x0, hover[1] = h.y0, hover[2] = h.x1, hover[3] = h.y1, hover[4] = 1.0f;
  } else {
    hover[4] = 0.0f;
  }
  return best;
}
// A pointer on the drawing (its own 0..1 across / down the base page): hovering moves the cursor there, a click (trigger)
// is A on it. True: it is on something.
bool point(float u, float v, bool click, bool cabinet);
// A board shown (0 LEAGUE LINES, 1 WIN BY, 2 FUTURES, 3 MY BETS): HOME's WAITING FOR YOU (winnings) opens MY BETS
void open_tab(int tab);
// The hub's hint bar (the A word, dim), the extras (X its word)
void hub_hints(std::string* aWord, bool* aDim, std::string* xWord);
// The machine's queues (game thread): a ticket to print out of its slot (0 none); a collect made there (its coins; 0 none:
// the cabinet's light show + coin shower)
uint32_t take_print();
long long take_machine_collect();
// A ticket's paper (any thread): its pieces for xrtext::board at w x h (the paper's size in pixels; r204: 768 wide - twice
// r199's, a bold layout with big text: Lance "impossible to read" - its height by its legs), with its stamp (WINNER green /
// LOSER red once settled). False: no such ticket.
constexpr int kTicketPx = 768;
bool ticket(uint32_t id, std::vector<xrtext::BoardPiece>* out, int* w, int* h);
// The ticket's height for its legs (layout units: 384 wide; the picture is kTicketPx / 384 x this - r207: a long one's picture
// narrower so it stays under 4096 px tall; past 10 legs each leg a little shorter)
int ticket_height(int legs);
constexpr int kTicketMaxPx = 4096;

// The base page is kW x kH; draw(...) puts it at (ox, oy) scaled by k. `cabinet`: the machine's screen (animated by `t`
// seconds: the ticker runs, the attract screen when nobody is at it); `machineOn`: the setting (the hub page's corner).
constexpr float kW = 1650.0f, kH = 958.0f;
void draw(std::vector<xrtext::BoardPiece>& out, float ox, float oy, float k, bool cabinet, float t, bool machineOn, bool attract = false);

// For the PC proof (any thread): state to draw a given UI case
struct Debug {
  int futMarket = 0;
  int tab = 0, cursor = 0, col = 0, stake = 2, focus = 0, slipRow = 0;
  std::vector<Pick> slip;
  double now = 0.0, flashAgo = -1.0, winAgo = -1.0, collectAgo = -1.0, multAgo = -1.0;  // (< 0: none)
  long long collectCoins = 0;
  bool collectMachine = false;
  double multFrom = 1.0;
  std::string msg;
  uint32_t msgRgb = 0xFFD23F;
  // r207: LEAGUE LINES' week picker (0 = this matchday, k = k matchdays ahead), the CUSTOM stake, the number pad
  int weekAhead = 0;
  long long custom = 0;  // r208: 64-bit (up to the whole balance)
  bool padOpen = false;
  int padKey = 0;
  std::string padText;
  // r224: the lists' first rows shown (LEAGUE LINES' games, MY BETS' tickets, the slip's legs); -1: kept from the cursor
  int linesTop = -1, betsTop = -1, legsTop = -1;
};
void debug_set(const Debug& d);
// r203: the state as it stands (the machine-input proof reads where the cursor went; slip = the legs on it, msg the last word)
Debug debug_get();
int debug_bet_count();
// r207: the book's tickets as they stand (the proof's settle / lock checks); the slip's legs as a ticket would get them (*err: why
// not, "" fine) and what it pays at a stake
std::vector<Bet> debug_bets();
std::vector<Leg> debug_slip_legs(std::string* err);
long long debug_slip_pays(long long stake);
void debug_load(const PortFranchiseBookDay& day, const std::vector<Bet>& bets, const PortFranchiseBookFutures* fut = nullptr);

}  // namespace xrbook