// strikers-quest r202 (Lance 2026-11-08, C:\StrikersMR\COIN-SINKS.md section 3): CASINO GAMES on the TIZM SPORTSBOOK
// machine - SLOTS and ROULETTE as games on its screen (no separate tables), played with the career's own TIZM COINS
// (the binder's wallet: a fictional currency, never real money, nothing to buy). Franchise only (the machine only stands in
// your room with a Franchise career loaded).
//
//   A GAME PICKER on the machine (B from any board): SPORTSBOOK / SLOTS / ROULETTE.
//   TIZM SLOTS: 5 reels x 3 rows, 10 paylines, Tizm symbols (the TIZM logo = WILD, the BLACK 1/1 card = SCATTER, six
//     Strikers captains, the TIZM coin). Pull the machine's joystick back (or A). Reels speed up, run, ease into their stop
//     with a bounce; a NEAR-MISS TEASE holds the last reels when 2 scatters (or a run of WILDs) are already in. Bets 5 / 10 /
//     25 / 50 / 100 a spin. 5 TIZM WILDs on a line = the MEGA JACKPOT (500x the bet + a free TIZM pack). RTP 94.22%
//     (house edge 5.78%), exact: every one of the 32^5 reel stops counted (the PC proof).
//   TIZM ROULETTE: a European single-zero wheel (the real number order + colours), a betting board: straight numbers (35:1),
//     red / black, odd / even, 1-18 / 19-36 (1:1), dozens + columns (2:1); chips 25 / 50 / 100 / 250 / MAX; as many bets a
//     spin as the coins cover. Zero takes every outside bet. House edge 1/37 = 2.70% on every bet.
//   Both: the stake comes off when you spin, a win is paid the same moment (so a crash can't lose it) and shown when the
//     reels stop / the ball settles - a count-up, the machine's light show + coin sounds scaled to it (xr_quest.cpp reads
//     lights() / take_show()); session win / loss on screen. Sounds in-house (tools/outside/casino202/casino_sounds202.py).
//   RNG: splitmix64 of (the career's seed, a salt drawn once per career and kept in its casino file, the game, the spin's
//     number) - a career's spins are fixed by its count but nobody at the machine can see what's next.
//
// Pure C++ apart from xr_book.h / xr_text.h (the wallet, sounds and pictures come in as function pointers), so the same file
// runs in the PC proof (tools/outside/casino202: wasm32 + node). Threads: frame / input on the game thread (xr_book.cpp
// calls them), draw on any thread (under the module's mutex).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "xr_book.h"
#include "xr_text.h"

namespace xrcasino {

// ---------------------------------------------------------------------------------------------------- SLOTS (the model)
constexpr int kReels = 5, kRows = 3, kStops = 32, kLines = 10, kSyms = 9;
enum Sym { kWild = 0, kScatter = 1, kMario = 2, kPeach = 3, kDk = 4, kYoshi = 5, kWario = 6, kLuigi = 7, kCoin = 8 };
extern const uint8_t kStrip[kReels][kStops];  // the reel strips (each stop's symbol)
extern const uint8_t kLine[kLines][kReels];   // each payline's row on each reel (0 top .. 2 bottom)
extern const int kLinePay[kSyms][6];          // a line of n (3-5) from the left: x the LINE bet (the bet / 10)
extern const int kScatterPay[6];              // 3-5 scatters anywhere: x the whole bet
constexpr int kSlotBets[5] = {5, 10, 25, 50, 100};
constexpr int kMegaPacks = 1;                 // the MEGA JACKPOT's free pack(s)

struct SlotResult {
  int stops[kReels] = {0, 0, 0, 0, 0};  // the strip index on the top row of each reel
  int grid[kRows][kReels] = {};
  int lineMult[kLines] = {};            // x the line bet (0 no win)
  int lineCount[kLines] = {};           // how many from the left
  int lineSym[kLines] = {};             // what it paid as (kWild: a WILDs-only line)
  int scatters = 0, scatterMult = 0;    // x the whole bet
  int win = 0;                          // coins (stake not taken off: what comes back)
  bool mega = false;                    // 5 WILDs on a line
  uint32_t cells = 0;                   // the winning cells, bit r * 5 + c
};
// The grid a set of stops shows
void slot_grid(const int stops[kReels], int grid[kRows][kReels]);
// One line's pay (x the line bet), its count and symbol: the WILDs-only run vs the first real symbol's run (WILDs standing in)
int line_mult(const int syms[kReels], int* count, int* sym);
// The whole spin's result at `bet` coins (a multiple of 5: every pay comes out whole)
SlotResult slot_eval(const int stops[kReels], int bet);
// The near-miss tease: does reel `r` (2..4) hold its stop (2+ scatters already in on the reels before it, or every line's
// symbols on the reels before it WILDs - 2 or more)
bool slot_tease(const int grid[kRows][kReels], int r);
// A win's size for the show: 0 none, 1 small (< 5x), 2 big (< 25x), 3 huge, 4 the MEGA JACKPOT
int win_tier(long long win, long long bet, bool mega);  // (r208: 64-bit coins)

// ------------------------------------------------------------------------------------------------- ROULETTE (the model)
extern const int kWheel[37];  // the European wheel's numbers clockwise from 0
bool is_red(int n);
enum BetKind { kStraight = 0, kRed, kBlack, kOdd, kEven, kLow, kHigh, kDozen, kColumn };
struct RBet {
  int kind = kStraight, n = 0;  // n: the number (kStraight), the dozen / column 0-2 (column 0 = 1, 4, 7 ...)
  long long amount = 0;         // r208: 64-bit coins (the MAX chip = the whole balance)
};
bool bet_wins(int kind, int n, int number);
int bet_odds(int kind);                              // paid to 1: 35, 2 or 1
long long bet_return(const RBet& b, int number);     // what it gives back (stake included), 0 lost; r208: 64-bit, held at xrcoin::kMax
std::string bet_name(int kind, int n);               // "17", "RED", "2ND 12", "COLUMN 3"
int wheel_index(int number);                         // its place on the wheel

// ------------------------------------------------------------------------------------------------------------ the RNG
uint64_t splitmix(uint64_t x);
// A spin's random 64 bits: the career, its secret salt, the game (1 slots, 2 roulette), the spin's number
uint64_t spin_bits(unsigned career, uint64_t salt, int game, uint64_t spin);
// The slot stops / the roulette number from those bits (unbiased: rejection on the top bits)
void slot_stops(uint64_t bits, int stops[kReels]);
int roulette_number(uint64_t bits);

// ------------------------------------------------------------------------------------------------- the live machine
enum Screen { kBook = 0, kSlots = 1, kRoulette = 2, kPicker = 3 };
// the wallet (xr_book's: coins, sounds, the pack hook); a picture by key ("strikers_mario"; straight RGBA, null none)
void set_wallet(const xrbook::Wallet& w);
void set_picture_source(const uint8_t* (*pic)(const char* key, int* w, int* h));
int screen();
// The picker (r203: its BACK returns to the screen it was opened from; the picker's cursor starts on that game)
void open_picker();
// r203: a slots / roulette spin running (the machine's MENU waits for it)
bool spinning();
// r203: what the controls do on the screen up now (the CONTROL BAR's words while a hand has the joystick; "" the sportsbook)
std::string machine_hint();
// Every frame on the game thread (xr_book.cpp's frame): the career's casino file, the spins' timelines (reel stops, the
// ball, the sounds, the reveal)
void frame(double now);
// The machine's controls (at it, the screen not the sportsbook's): true used. r204: a step's tick only when it moved something
bool input(const xrbook::Input& in);
// r204: where the casino's selection is (screen, picker, board cursor, bet, chip, lines shown)
uint64_t selection_key();
// A pointer on the screen (0..1 across / down): hover moves the cursor, a click is A on it
bool point(float u, float v, bool click);
// A spin running or a win showing (the screen redraws fast)
bool animating();
// r205: animating() without the cursor's pop / press (the machine's screen: its cursor is an overlay); the cursor's pop / press
// now: seconds since each
bool animating_board();
void cursor_anim(float* moveAgo, float* pressAgo);
// The cabinet's light show now (0..1: the bulbs going wild while the reels / ball run, a win's show scaled to it)
float lights();
// A win just shown (once): its tier (1 small - a flash, 2+ the full show + the coin shower); 0 none
int take_show();
// r202 (Lance's r199 test: A at the machine went to the game): the machine is ENGAGED only while a hand holds its joystick
// (xr_quest.cpp book_engage); its screens show "MACHINE CONTROL" then. Any thread.
void set_engaged(bool on);
bool engaged();
// r205: how it's engaged: 1 a hand holds the 3D stick (arcade stick), 2 LATCHED (a quick grip: the thumbsticks + buttons drive it
// hands free - the control bar says CONTROL ON ... GRIP STICK: OFF)
void set_control_mode(int mode);
// The SLOTS handle on the cabinet's side (Lance 11-08): pulled down far enough by a hand = a pull (a spin, as A); true spun
bool lever_pull();
// When the last slots spin started (the module's clock; < 0 none) and the clock now: the handle swings down by itself on a
// spin started by A / the stick / the pointer
double slot_spun_at();
double clock_now();
// r202 (Lance: "crazy VICTORY music on every win and FUNNY LOSING music on every loss"; synthesized "sounds like Pong"): real
// CC0 / CC-BY clips (C:\StrikersMR\outside\casino-audio\final -> assets/franchise/cardsounds/casino_*.wav; credits in
// CASINO-AUDIO-CREDITS.txt), ONE table in xr_casino.cpp (kClips) says which play when; each kind steps through its rows in
// turn; at the SOUND > EFFECTS volume. The sportsbook's machine collect, its bets and its LOSER slips use it too.
enum Clip {
  kMusicSmall = 0, kMusicBig, kMusicJackpot, kMusicLose,              // the music
  kSfxChip, kSfxChipsClear, kSfxCoinIn, kSfxCoinsPour, kSfxCoinsJingle, kSfxCoinSplash, kSfxBallRoll,  // coins, chips, the ball
  kClipKinds
};
void play_music(int kind);
// A small "MACHINE CONTROL" pill for the screens' foot (the sportsbook's too)
void control_pill(std::vector<xrtext::BoardPiece>& out, float t);
// r203: the CONTROL BAR over the screen's foot while engaged: the MACHINE CONTROL pill + `hint` (or `msg`, when not empty)
void control_bar(std::vector<xrtext::BoardPiece>& out, float t, const std::string& hint, const std::string& msg, uint32_t msgRgb);
// The screen at (ox, oy) x k (the base page is xrbook::kW x kH); attract: nobody at the machine
void draw(std::vector<xrtext::BoardPiece>& out, float ox, float oy, float k, float t, bool attract);

// For the PC proof: a state to draw (now = the module's clock)
struct Debug {
  int screen = kSlots, slotBet = 1, chip = 0, pickerAt = 1, row = 1, col = 5;
  double now = 100.0;
  // slots: the result on the reels, spun at `spinAt` (< 0: idle, showing `result`)
  bool haveResult = false;
  SlotResult result;
  double spinAt = -1.0;
  bool showLines = false;
  // roulette: the bets on the board, a spin to `number` at `spinAt`
  std::vector<RBet> bets;
  int number = -1;
  std::vector<int> history;
  long long balance = -1;  // (< 0: the wallet's; r208: 64-bit)
  long long sessionIn = 0, sessionOut = 0;
};
void debug_set(const Debug& d);
// r203: the state as it stands (screen, cursor, bet, chip, the board's bets, the last number, the session)
Debug debug_get();
// The proof's control of the clock / career (frame(now) does the rest)
void debug_salt(unsigned career, uint64_t salt);

}  // namespace xrcasino
