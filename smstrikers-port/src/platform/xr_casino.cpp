// strikers-quest r202: CASINO GAMES on the TIZM SPORTSBOOK machine (xr_casino.h; C:\StrikersMR\COIN-SINKS.md section 3).
// TIZM SLOTS + TIZM ROULETTE: the models, the RNG, the career's casino file, the spins' timelines and the screen's drawing.
#include "xr_casino.h"

#include "xr_menu.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

namespace xrcasino {

// =================================================================================================== SLOTS: the model
// tools/outside/casino202/slots_tune.py + slots_strips.py: per reel W S MARIO PEACH DK YOSHI WARIO LUIGI COIN =
//   [2 2 2 2 2 3 4 5 10] [3 2 1 2 2 3 4 6 9] [3 2 1 2 3 4 4 5 8] [3 2 1 2 3 4 5 6 6] [2 2 1 2 3 4 4 6 8]
// spread round each reel, the scatters >= 3 stops apart (a window never shows two), nothing next to itself
const uint8_t kStrip[kReels][kStops] = {
    {8, 5, 7, 6, 8, 0, 8, 3, 1, 8, 7, 4, 2, 6, 8, 5, 8, 7, 8, 6, 0, 8, 7, 3, 1, 5, 8, 4, 2, 6, 8, 7},
    {5, 7, 6, 8, 0, 8, 7, 8, 3, 1, 4, 8, 7, 6, 2, 8, 0, 5, 7, 8, 6, 8, 7, 3, 8, 0, 5, 1, 4, 7, 6, 8},
    {4, 7, 5, 8, 6, 8, 0, 7, 8, 3, 1, 5, 6, 8, 4, 7, 0, 2, 8, 5, 6, 7, 8, 4, 8, 3, 0, 1, 5, 7, 6, 8},
    {6, 7, 8, 5, 4, 0, 1, 7, 8, 6, 3, 5, 7, 8, 6, 4, 0, 7, 8, 2, 5, 6, 1, 7, 8, 4, 3, 6, 0, 5, 7, 8},
    {8, 7, 6, 8, 5, 4, 3, 1, 7, 8, 6, 0, 8, 5, 7, 4, 8, 7, 6, 8, 5, 2, 3, 7, 1, 8, 4, 6, 0, 8, 5, 7},
};
const uint8_t kLine[kLines][kReels] = {
    {1, 1, 1, 1, 1}, {0, 0, 0, 0, 0}, {2, 2, 2, 2, 2}, {0, 1, 2, 1, 0}, {2, 1, 0, 1, 2},
    {0, 0, 1, 2, 2}, {2, 2, 1, 0, 0}, {1, 0, 0, 0, 1}, {1, 2, 2, 2, 1}, {1, 2, 1, 0, 1},
};
// x the line bet (the bet / 10) for 3 / 4 / 5 from the left; all even, so every pay at a bet of 5 coins comes out whole
const int kLinePay[kSyms][6] = {
    {0, 0, 0, 40, 200, 5000},  // TIZM WILD (5 = the MEGA JACKPOT, + a free pack)
    {0, 0, 0, 0, 0, 0},        // 1/1 (a scatter: kScatterPay)
    {0, 0, 0, 20, 80, 400},    // MARIO
    {0, 0, 0, 16, 60, 250},    // PEACH
    {0, 0, 0, 12, 40, 150},    // DONKEY KONG
    {0, 0, 0, 10, 30, 100},    // YOSHI
    {0, 0, 0, 6, 20, 60},      // WARIO
    {0, 0, 0, 4, 14, 40},      // LUIGI
    {0, 0, 0, 2, 8, 24},       // TIZM COIN
};
const int kScatterPay[6] = {0, 0, 0, 2, 10, 50};

void slot_grid(const int stops[kReels], int grid[kRows][kReels]) {
  for (int c = 0; c < kReels; ++c)
    for (int r = 0; r < kRows; ++r) grid[r][c] = kStrip[c][((stops[c] % kStops) + kStops + r) % kStops];
}

int line_mult(const int syms[kReels], int* count, int* sym) {
  int w = 0;
  while (w < kReels && syms[w] == kWild) ++w;
  int best = w >= 3 ? kLinePay[kWild][w] : 0, bestN = w >= 3 ? w : 0, bestS = kWild;
  int first = -1;
  for (int i = 0; i < kReels; ++i)
    if (syms[i] != kWild) {
      first = syms[i];
      break;
    }
  if (first >= 0 && first != kScatter) {
    int n = 0;
    while (n < kReels && (syms[n] == kWild || syms[n] == first)) ++n;
    if (n >= 3 && kLinePay[first][n] > best) best = kLinePay[first][n], bestN = n, bestS = first;
  }
  if (count != nullptr) *count = best > 0 ? bestN : 0;
  if (sym != nullptr) *sym = bestS;
  return best;
}

SlotResult slot_eval(const int stops[kReels], int bet) {
  SlotResult r;
  for (int c = 0; c < kReels; ++c) r.stops[c] = ((stops[c] % kStops) + kStops) % kStops;
  slot_grid(r.stops, r.grid);
  long long lineSum = 0;
  for (int l = 0; l < kLines; ++l) {
    int s[kReels];
    for (int c = 0; c < kReels; ++c) s[c] = r.grid[kLine[l][c]][c];
    r.lineMult[l] = line_mult(s, &r.lineCount[l], &r.lineSym[l]);
    lineSum += r.lineMult[l];
    if (r.lineMult[l] > 0) {
      for (int c = 0; c < r.lineCount[l]; ++c) r.cells |= 1u << (kLine[l][c] * kReels + c);
      if (r.lineSym[l] == kWild && r.lineCount[l] == 5) r.mega = true;
    }
  }
  uint32_t scat = 0;
  for (int rr = 0; rr < kRows; ++rr)
    for (int c = 0; c < kReels; ++c)
      if (r.grid[rr][c] == kScatter) ++r.scatters, scat |= 1u << (rr * kReels + c);
  r.scatterMult = kScatterPay[std::min(5, r.scatters)];
  if (r.scatterMult > 0) r.cells |= scat;
  r.win = static_cast<int>(lineSum * bet / 10 + static_cast<long long>(r.scatterMult) * bet);
  return r;
}

bool slot_tease(const int grid[kRows][kReels], int r) {
  if (r < 2 || r >= kReels) return false;
  int sc = 0;
  for (int c = 0; c < r; ++c)
    for (int rr = 0; rr < kRows; ++rr) sc += grid[rr][c] == kScatter ? 1 : 0;
  if (sc >= 2) return true;
  for (int l = 0; l < kLines; ++l) {
    bool all = true;
    for (int c = 0; c < r && all; ++c) all = grid[kLine[l][c]][c] == kWild;
    if (all) return true;
  }
  return false;
}

int win_tier(long long win, long long bet, bool mega) {
  if (mega) return 4;
  if (win <= 0) return 0;
  if (win < 5 * bet) return 1;
  if (win < 25 * bet) return 2;
  return 3;
}

// ================================================================================================ ROULETTE: the model
const int kWheel[37] = {0, 32, 15, 19, 4, 21, 2, 25, 17, 34, 6, 27, 13, 36, 11, 30, 8, 23, 10,
                        5, 24, 16, 33, 1, 20, 14, 31, 9, 22, 18, 29, 7, 28, 12, 35, 3, 26};
bool is_red(int n) {
  static const bool kRedN[37] = {false, true,  false, true,  false, true,  false, true,  false, true,  false, false, true,
                                 false, true,  false, true,  false, true,  true,  false, true,  false, true,  false, true,
                                 false, true,  false, false, true,  false, true,  false, true,  false, true};
  return n >= 1 && n <= 36 && kRedN[n];
}
bool bet_wins(int kind, int n, int x) {
  if (x < 0 || x > 36) return false;
  if (kind == kStraight) return x == n;
  if (x == 0) return false;  // (zero takes every outside bet)
  switch (kind) {
    case kRed: return is_red(x);
    case kBlack: return !is_red(x);
    case kOdd: return x % 2 == 1;
    case kEven: return x % 2 == 0;
    case kLow: return x <= 18;
    case kHigh: return x >= 19;
    case kDozen: return (x - 1) / 12 == n;
    case kColumn: return (x - 1) % 3 == n;
    default: return false;
  }
}
int bet_odds(int kind) { return kind == kStraight ? 35 : (kind == kDozen || kind == kColumn ? 2 : 1); }
long long bet_return(const RBet& b, int number) {  // r208: 64-bit (an amount <= kMax ~ 2^50, x 36 inside 64 bits), held at kMax
  return bet_wins(b.kind, b.n, number) ? xrcoin::clamp(xrcoin::clamp(b.amount) * (bet_odds(b.kind) + 1)) : 0;
}
std::string bet_name(int kind, int n) {
  switch (kind) {
    case kStraight: return std::to_string(n);
    case kRed: return "RED";
    case kBlack: return "BLACK";
    case kOdd: return "ODD";
    case kEven: return "EVEN";
    case kLow: return "1-18";
    case kHigh: return "19-36";
    case kDozen: return n == 0 ? "1ST 12" : (n == 1 ? "2ND 12" : "3RD 12");
    default: return "COLUMN " + std::to_string(n + 1);
  }
}
int wheel_index(int number) {
  for (int i = 0; i < 37; ++i)
    if (kWheel[i] == number) return i;
  return 0;
}

// ============================================================================================================ the RNG
uint64_t splitmix(uint64_t x) {
  x += 0x9E3779B97F4A7C15ull;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
  return x ^ (x >> 31);
}
uint64_t spin_bits(unsigned career, uint64_t salt, int game, uint64_t spin) {
  return splitmix(splitmix(splitmix(salt ^ (static_cast<uint64_t>(career) << 17)) + static_cast<uint64_t>(game) * 0xD1B54A32D192ED03ull) ^
                  (spin * 0x9E6C63D0676A9A99ull));
}
namespace {
// an unbiased pick in [0, n) from a stream (rejection: the stream re-mixed when a draw falls in the short top slice)
int pick(uint64_t* s, int n) {
  const uint64_t lim = ~0ull - (~0ull % static_cast<uint64_t>(n) + 1) % static_cast<uint64_t>(n);
  for (;;) {
    *s = splitmix(*s);
    if (*s <= lim) return static_cast<int>(*s % static_cast<uint64_t>(n));
  }
}
}  // namespace
void slot_stops(uint64_t bits, int stops[kReels]) {
  uint64_t s = bits;
  for (int c = 0; c < kReels; ++c) stops[c] = pick(&s, kStops);
}
int roulette_number(uint64_t bits) {
  uint64_t s = bits ^ 0x5EED0F3E7A11ull;
  return pick(&s, 37);
}

// ======================================================================================================= the machine
namespace {
xrbook::Wallet g_wallet;
}  // namespace
// r202: the machine's real audio - ONE table (Lance may veto clips: drop the row, and its file in casino_music202.py). Each kind
// steps through its rows in turn. Gains: the clips come levelled to the machine's own sounds.
struct ClipRow {
  int kind;
  const char* name;  // assets/franchise/cardsounds/<name>.wav
  float gain;
};
const ClipRow kClips[] = {
    {kMusicSmall, "casino_win_small_1_steeldrum", 0.85f},       {kMusicSmall, "casino_win_small_2_sax", 0.85f},
    {kMusicSmall, "casino_win_small_3_trumpet", 0.85f},         {kMusicSmall, "casino_win_small_4_fanfare_lick", 0.85f},
    {kMusicSmall, "casino_win_small_5_tuba", 0.85f},            {kMusicSmall, "casino_win_small_6_brass", 0.85f},
    {kMusicBig, "casino_win_big_1_victory", 0.9f},              {kMusicBig, "casino_win_big_2_orchestra", 0.9f},
    {kMusicJackpot, "casino_win_jackpot_1_brass_fanfare", 0.95f}, {kMusicJackpot, "casino_win_jackpot_2_victory_fanfare", 0.95f},
    {kMusicJackpot, "casino_win_jackpot_3_funky_disco", 0.95f},
    {kMusicLose, "casino_lose_trombone_1", 0.8f},               {kMusicLose, "casino_lose_womp", 0.8f},
    {kMusicLose, "casino_lose_slide_whistle", 0.8f},            {kMusicLose, "casino_lose_tuba", 0.8f},
    {kMusicLose, "casino_lose_pizzicato", 0.8f},                {kMusicLose, "casino_lose_trombone_2", 0.8f},
    {kMusicLose, "casino_lose_sax_slide", 0.8f},                {kMusicLose, "casino_lose_fail_sting", 0.8f},
    {kSfxChip, "casino_chips_stack_1", 0.8f},                   {kSfxChip, "casino_chips_collide", 0.8f},
    {kSfxChip, "casino_chips_stack_2", 0.8f},                   {kSfxChipsClear, "casino_chips_handle", 0.8f},
    {kSfxCoinIn, "casino_coin_drop_single", 0.8f},              {kSfxCoinsPour, "casino_coins_pour_tray", 0.9f},
    {kSfxCoinsJingle, "casino_coins_jingle", 0.8f},             {kSfxCoinSplash, "casino_coin_splash", 0.8f},
    {kSfxBallRoll, "casino_roulette_ball_roll", 0.75f},
};
void play_music(int kind) {
  static int s_turn[kClipKinds] = {};
  if (kind < 0 || kind >= kClipKinds) return;
  bool (*fn)(const char*, float) = g_wallet.music != nullptr ? g_wallet.music : g_wallet.sound;
  if (fn == nullptr) return;
  std::vector<const ClipRow*> rows;
  for (const ClipRow& r : kClips)
    if (r.kind == kind) rows.push_back(&r);
  for (size_t k = 0; k < rows.size(); ++k) {  // (its next one; a missing file: the one after)
    const ClipRow* r = rows[static_cast<size_t>(s_turn[kind]++) % rows.size()];
    if (fn(r->name, r->gain)) return;
  }
}namespace {
std::mutex g_cm;
const uint8_t* (*g_pic)(const char*, int*, int*) = nullptr;
double g_now = 0.0;
int g_screen = kBook, g_prevGame = kSlots, g_pickerAt = 1;
int g_pickerFrom = kBook;  // r203: where the picker was opened from (its BACK goes there)

struct Casino {
  unsigned career = 0;
  bool loaded = false;
  uint64_t salt = 0;
  long long slotSpins = 0, slotIn = 0, slotOut = 0, slotBest = 0, megas = 0;
  long long rSpins = 0, rIn = 0, rOut = 0, rBest = 0;
  std::vector<int> history;  // the last 12 numbers, newest first
  int slotBet = 1, chip = 0;
} g_c;
// this session (since the career was loaded on this run)
long long g_sesIn = 0, g_sesOut = 0, g_sesSpins = 0;
long long g_debugBalance = -1;  // r208: 64-bit

// ---- SLOTS: the spin on the reels
constexpr double kAccel = 0.28, kDecel = 0.5, kBounce = 0.26, kTease = 1.15;
struct SlotSpin {
  bool on = false, revealed = true;
  double t0 = -1e9, revealAt = -1e9;
  int prev[kReels] = {0, 7, 14, 21, 3};
  SlotResult r;
  double stopT[kReels] = {}, D[kReels] = {}, v[kReels] = {};
  int teaseFrom = kReels, fired = 0, bet = 25, tier = 0;
  bool teaseSaid = false;
} g_ss;
int g_rest[kReels] = {0, 7, 14, 21, 3};  // the reels at rest (top row)
bool g_haveResult = false;               // a result on the reels to show (lines, cells)
bool g_showLines = false;
long long g_pendingWin = 0;              // paid but not shown yet (hidden from the coins on screen)
int g_showTier = 0;                      // a win's show for the cabinet (take_show)
double g_showAt = -1e9;
int g_lastTier = 0;

// ---- ROULETTE
constexpr double kIdleSpin = 0.35, kSettle = 6.4, kReveal = 6.7, kDrop = 4.3;
constexpr double kLand[5] = {4.9, 5.26, 5.56, 5.84, 6.10};
std::vector<RBet> g_bets, g_lastBets;   // on the board now; the last spin's (X: REBET)
std::vector<int> g_undo;                // chips put down this round, newest last (index into g_bets, the amount)
std::vector<long long> g_undoAmt;  // r208: 64-bit
int g_row = 1, g_col = 5;               // the board's cursor (row 5: the chips, CLEAR, SPIN)
struct RSpin {
  bool on = false, revealed = true, any = false;
  double t0 = -1e9;
  int number = -1, tier = 0;
  long long ret = 0, staked = 0;  // r208: 64-bit
  std::vector<RBet> bets;
} g_rs;
bool g_showSpinBets = false;            // the last spin's chips still on the board (until a new chip goes down)
double g_wA0 = 0.0, g_wT0 = 0.0, g_wB = 0.0;  // the wheel's angle: a0 + idle x dt + B (1 - e^(-dt / 2.2))

std::string g_msg;
uint32_t g_msgRgb = 0xFFD23F;
double g_msgAt = -1e9;

struct Hit {
  float x0, y0, x1, y1;
  int kind, a, b;
};
std::vector<Hit> g_hits;
// r204: the cursor's pop / press (g_now's clock), the pointer's sticky hover, a step's ticks held back until it moved something
double g_moveAt = -1e9, g_pressAt = -1e9;
float g_hover[5] = {0, 0, 0, 0, 0};
bool g_deferTicks = false;
float g_tickWant = 0.0f;
uint64_t sel_key_locked() {  // (g_cm held) where the selection is
  return static_cast<uint64_t>(g_screen & 7) | static_cast<uint64_t>(g_pickerAt & 7) << 3 | static_cast<uint64_t>(g_row & 15) << 6 |
         static_cast<uint64_t>(g_col & 31) << 10 | static_cast<uint64_t>(g_c.slotBet & 7) << 15 | static_cast<uint64_t>(g_c.chip & 7) << 18 |
         static_cast<uint64_t>(g_showLines ? 1 : 0) << 21;
}
// the cursor on a casino screen (xrbook::cursor_frame: the same as the sportsbook's)
void cur_frame(std::vector<xrtext::BoardPiece>& v, int layer, float x0, float y0, float x1, float y1, float r, float t,
               uint32_t rgb = 0x2EF2FF) {
  xrbook::cursor_frame(v, layer, x0, y0, x1, y1, r, t, static_cast<float>(g_now - g_moveAt), static_cast<float>(g_now - g_pressAt), rgb);
}
// r205: an item lit as the cursor's - never on the machine's screen (xrbook::cursor_sink: its overlay lights it)
bool lit(bool cur) { return cur && xrbook::cursor_sink() == nullptr; }

std::string home_dir() {
  const char* h = std::getenv("STRIKERS_HOME");
  return std::string(h != nullptr && *h != '\0' ? h : "/storage/emulated/0/Strikers");
}
std::string casino_path(unsigned career) { return home_dir() + "/cards/casino-" + std::to_string(career) + ".txt"; }
void say(const std::string& m, uint32_t rgb) {
  g_msg = m;
  g_msgRgb = rgb;
  g_msgAt = g_now;
}
void sound(const char* n, float gain) {
  if (g_deferTicks && std::strcmp(n, "book_tick") == 0) {  // (r204: a step's tick waits to see whether anything moved)
    g_tickWant = std::max(g_tickWant, gain);
    return;
  }
  if (g_wallet.sound != nullptr) g_wallet.sound(n, gain);
}
long long balance() {  // r208: 64-bit
  if (g_debugBalance >= 0) return g_debugBalance;
  return g_wallet.balance != nullptr ? g_wallet.balance() : 0;
}
unsigned wallet_career() { return g_wallet.career != nullptr ? g_wallet.career() : 0u; }

// ---- the file: STRIKERS_HOME/cards/casino-<career>.txt (written whole: a temp file, then renamed)
std::string serialize(const Casino& c) {
  char b[512];
  std::string s = "casino 1\n";
  std::snprintf(b, sizeof(b), "salt %llu\n", static_cast<unsigned long long>(c.salt));
  s += b;
  std::snprintf(b, sizeof(b), "slots %lld %lld %lld %lld %lld\n", c.slotSpins, c.slotIn, c.slotOut, c.slotBest, c.megas);
  s += b;
  std::snprintf(b, sizeof(b), "roulette %lld %lld %lld %lld\n", c.rSpins, c.rIn, c.rOut, c.rBest);
  s += b;
  s += "history";
  for (int n : c.history) s += " " + std::to_string(n);
  s += "\n";
  std::snprintf(b, sizeof(b), "prefs %d %d\n", c.slotBet, c.chip);
  s += b;
  return s;
}
void parse(const std::string& text, Casino* c) {
  size_t at = 0;
  while (at < text.size()) {
    size_t e = text.find('\n', at);
    if (e == std::string::npos) e = text.size();
    const std::string line = text.substr(at, e - at);
    at = e + 1;
    std::vector<unsigned long long> v;
    const size_t sp = line.find(' ');
    const std::string key = line.substr(0, sp);
    if (sp != std::string::npos) {
      const char* p = line.c_str() + sp;
      for (;;) {
        while (*p == ' ') ++p;
        if (*p == '\0') break;
        char* end = nullptr;
        const unsigned long long x = std::strtoull(p, &end, 10);
        if (end == p) break;
        v.push_back(x);
        p = end;
      }
    }
    auto ll = [&](size_t i) { return i < v.size() ? static_cast<long long>(v[i]) : 0ll; };
    if (key == "salt" && !v.empty()) c->salt = v[0];
    else if (key == "slots") c->slotSpins = ll(0), c->slotIn = ll(1), c->slotOut = ll(2), c->slotBest = ll(3), c->megas = ll(4);
    else if (key == "roulette") c->rSpins = ll(0), c->rIn = ll(1), c->rOut = ll(2), c->rBest = ll(3);
    else if (key == "history") {
      c->history.clear();
      for (size_t i = 0; i < v.size() && i < 12; ++i)
        if (v[i] <= 36) c->history.push_back(static_cast<int>(v[i]));
    } else if (key == "prefs") {
      c->slotBet = static_cast<int>(std::min(4ll, ll(0)));
      c->chip = static_cast<int>(std::min(4ll, ll(1)));
    }
  }
}
uint64_t fresh_salt(unsigned career) {
  uint64_t s = static_cast<uint64_t>(std::time(nullptr)) * 0x2545F4914F6CDD1Dull ^ reinterpret_cast<uintptr_t>(&g_c) ^
               (static_cast<uint64_t>(career) << 32) ^ static_cast<uint64_t>(std::clock()) * 0x9E3779B97F4A7C15ull;
  if (FILE* f = std::fopen("/dev/urandom", "rb")) {  // (the headset: the kernel's own entropy on top)
    uint64_t u = 0;
    if (std::fread(&u, sizeof(u), 1, f) == 1) s ^= u;
    std::fclose(f);
  }
  return splitmix(s) | 1ull;
}
void save_casino() {  // (g_cm held)
  if (g_c.career == 0) return;
  const std::string path = casino_path(g_c.career), tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) return;
  const std::string s = serialize(g_c);
  const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
  std::fclose(f);
  if (ok) std::rename(tmp.c_str(), path.c_str());
}
void load_casino(unsigned career) {  // (g_cm held)
  g_c = Casino();
  g_c.career = career;
  g_c.loaded = true;
  if (FILE* f = std::fopen(casino_path(career).c_str(), "r")) {
    std::string text;
    char buf[1024];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, got);
    std::fclose(f);
    parse(text, &g_c);
  }
  if (g_c.salt == 0) {
    g_c.salt = fresh_salt(career);
    save_casino();
  }
  g_sesIn = g_sesOut = g_sesSpins = 0;
  g_bets.clear();
  g_lastBets.clear();
  g_undo.clear();
  g_undoAmt.clear();
  g_ss = SlotSpin();
  g_rs = RSpin();
  g_haveResult = false;
  g_pendingWin = 0;
  g_showSpinBets = false;
}

// ---- SLOTS: the reels' motion. A reel's top-row position (in stops; it falls as the strip runs down the window) and speed
double reel_s(int c, double dt, double* speed) {  // the distance run by dt
  const double T = g_ss.stopT[c], D = g_ss.D[c], v = g_ss.v[c];
  double s = 0, sp = 0;
  if (dt <= 0) {
    s = 0, sp = 0;
  } else if (dt < kAccel) {
    s = 0.5 * v * dt * dt / kAccel, sp = v * dt / kAccel;
  } else if (dt < T - kDecel) {
    s = v * (dt - 0.5 * kAccel), sp = v;
  } else if (dt < T) {
    s = D - 0.5 * v * (T - dt) * (T - dt) / kDecel, sp = v * (T - dt) / kDecel;
  } else if (dt < T + kBounce) {  // past its stop a little and back (a reel's thunk)
    const double u = (dt - T) / kBounce;
    s = D + 0.16 * std::sin(3.14159265 * u) * (1.0 - u), sp = 0;
  } else {
    s = D, sp = 0;
  }
  if (speed != nullptr) *speed = sp;
  return s;
}
double reel_pos(int c, double* speed) {
  if (!g_ss.on) {
    if (speed != nullptr) *speed = 0;
    return g_rest[c];
  }
  return g_ss.prev[c] - reel_s(c, g_now - g_ss.t0, speed);
}

// the result's show: its MUSIC by size (r202: real CC0 clips, play_music) + the coins pouring into the tray; less back than
// the stake: only a coin's clink (no fanfare for a loss dressed up as a win). The light show reads lights() / take_show().
void win_sounds(int tier, bool beatStake) {
  if (!beatStake) {
    play_music(kSfxCoinSplash);
    return;
  }
  if (tier <= 1) play_music(kMusicSmall), play_music(kSfxCoinsJingle);
  else if (tier == 2) play_music(kMusicBig), play_music(kSfxCoinsPour);
  else play_music(kMusicJackpot), play_music(kSfxCoinsPour);
}

bool slot_spin() {  // (g_cm held) the lever pulled
  if (g_c.career == 0) {
    say("LOAD A FRANCHISE CAREER", 0xFF4D5E);
    return false;
  }
  if (g_ss.on && !g_ss.revealed) return false;
  const int bet = kSlotBets[std::max(0, std::min(4, g_c.slotBet))];
  if (balance() < bet) {
    say("NOT ENOUGH COINS FOR A " + std::to_string(bet) + " SPIN", 0xFF4D5E);
    sound("book_tick", 0.5f);
    return false;
  }
  // the spin's number first (kept), then the coins (a crash after can't replay it: the next pull is the next number)
  const long long n = ++g_c.slotSpins;
  save_casino();
  if (g_debugBalance < 0 && (g_wallet.spend == nullptr || !g_wallet.spend(g_c.career, bet, "STRIKERZ SLOTS spin"))) {
    say("THE MACHINE COULDN'T TAKE THE COINS", 0xFF4D5E);
    return false;
  }
  if (g_debugBalance >= 0) g_debugBalance -= bet;
  int stops[kReels];
  slot_stops(spin_bits(g_c.career, g_c.salt, 1, static_cast<uint64_t>(n)), stops);
  const SlotResult r = slot_eval(stops, bet);
  // paid now (so a crash can't lose it), shown when the reels stop
  if (r.win > 0) {
    if (g_debugBalance >= 0) g_debugBalance = xrcoin::clamp(g_debugBalance + r.win);
    else if (g_wallet.add != nullptr) g_wallet.add(g_c.career, r.win, r.mega ? "STRIKERZ SLOTS MEGA JACKPOT" : "STRIKERZ SLOTS win");
  }
  if (r.mega) {
    ++g_c.megas;
    if (g_wallet.pack == nullptr || !g_wallet.pack(g_c.career, kMegaPacks, "STRIKERZ SLOTS MEGA JACKPOT")) {
      if (g_debugBalance < 0 && g_wallet.add != nullptr) g_wallet.add(g_c.career, 50 * kMegaPacks, "STRIKERZ SLOTS MEGA JACKPOT (the pack's price)");
    }
  }
  g_c.slotIn = xrcoin::add(g_c.slotIn, bet);  // (r208: the totals held, never wrapping)
  g_c.slotOut = xrcoin::add(g_c.slotOut, r.win);
  g_c.slotBest = std::max<long long>(g_c.slotBest, r.win);
  save_casino();
  g_sesIn = xrcoin::add(g_sesIn, bet);
  g_sesOut = xrcoin::add(g_sesOut, r.win);
  ++g_sesSpins;
  // the reels' timeline
  SlotSpin s;
  s.on = true;
  s.revealed = false;
  s.t0 = g_now;
  s.r = r;
  s.bet = bet;
  for (int c = 0; c < kReels; ++c) s.prev[c] = g_ss.on ? g_ss.r.stops[c] : g_rest[c];
  double extra = 0;
  for (int c = 0; c < kReels; ++c) {
    if (c >= 2 && slot_tease(r.grid, c)) {
      extra += kTease;
      if (s.teaseFrom == kReels) s.teaseFrom = c;
    }
    s.stopT[c] = 0.95 + 0.30 * c + extra;
    const double run = s.stopT[c] - 0.5 * kAccel - 0.5 * kDecel;
    const int base = ((s.prev[c] - r.stops[c]) % kStops + kStops) % kStops;
    const double want = 17.0 * run;  // (about 17 stops a second at full speed)
    const int laps = std::max(1, static_cast<int>(std::lround((want - base) / kStops)));
    s.D[c] = base + static_cast<double>(laps) * kStops;
    s.v[c] = s.D[c] / run;
  }
  s.revealAt = s.t0 + s.stopT[kReels - 1] + kBounce;
  s.tier = win_tier(r.win, bet, r.mega);
  g_ss = s;
  g_haveResult = false;
  g_pendingWin = r.win;
  sound("casino_pull", 0.9f);
  sound("casino_reels", 0.75f);
  play_music(kSfxCoinIn);  // (the stake: a coin into the machine)
  g_msgAt = -1e9;
  return true;
}

// ---- ROULETTE
double wheel_angle(double now) { return g_wA0 + kIdleSpin * (now - g_wT0) + g_wB * (1.0 - std::exp(-(now - g_wT0) / 2.2)); }
// the ball: its angle round the wheel (screen, clockwise from up) and how far out (0..1 of R); false: none to show
bool ball_at(double now, double* ang, double* rad) {
  if (g_rs.number < 0) return false;
  const double dt = now - g_rs.t0, w = wheel_angle(now);
  const double end = wheel_index(g_rs.number) * (2.0 * 3.14159265358979 / 37.0);
  constexpr double kTrack = 0.86, kPocket = 0.595;
  if (!g_rs.on || dt >= kSettle) {
    *ang = w + end;
    *rad = kPocket;
    return true;
  }
  const double A = 2.0 * 3.14159265358979 * 8.0;  // (eight laps against the wheel, slowing to a stop on its pocket)
  const double u = 1.0 - dt / kSettle;
  double a = w + end + A * u * u;
  double r = kTrack;
  if (dt > kDrop) {  // off the track, down to the pockets, bouncing over the frets
    const double f = std::min(1.0, (dt - kDrop) / (kLand[0] - kDrop));
    r = kTrack + (kPocket - kTrack) * f * f;
    static const double kHop[4] = {0.07, 0.045, 0.025, 0.01}, kSkip[4] = {0.42, -0.30, 0.17, -0.07};
    for (int k = 0; k < 4; ++k)
      if (dt >= kLand[k] && dt < kLand[k + 1]) {
        const double h = (dt - kLand[k]) / (kLand[k + 1] - kLand[k]);
        r = kPocket + kHop[k] * std::sin(3.14159265 * h);
        a += kSkip[k] * std::sin(3.14159265 * h);
      }
  }
  *ang = a;
  *rad = r;
  return true;
}
long long staged() {  // r208: 64-bit
  long long s = 0;
  for (const RBet& b : g_bets) s = xrcoin::add(s, b.amount);
  return s;
}
constexpr int kChips[4] = {25, 50, 100, 250};
long long chip_coins(int idx, long long free) { return idx < 4 ? kChips[idx] : free; }

// the board's spots: (row, col) -> its bet; rows 0-2 the numbers (col 0 the zero, 1-12, 13 the columns), 3 the dozens, 4 the
// even-money six, 5 the chips / CLEAR / SPIN
bool spot_bet(int row, int col, RBet* b) {
  if (row <= 2) {
    if (col == 0) b->kind = kStraight, b->n = 0;
    else if (col <= 12) b->kind = kStraight, b->n = 3 * (col - 1) + (3 - row);
    else b->kind = kColumn, b->n = 2 - row;
    return true;
  }
  if (row == 3) {
    b->kind = kDozen;
    b->n = (std::max(1, std::min(12, col)) - 1) / 4;
    return true;
  }
  if (row == 4) {
    static const int kK[6] = {kLow, kEven, kRed, kBlack, kOdd, kHigh};
    b->kind = kK[(std::max(1, std::min(12, col)) - 1) / 2];
    b->n = 0;
    return true;
  }
  return false;
}
// the board's layout (base page px)
constexpr float kBx0 = 734, kZeroW = 66, kNumW = 62, kColW = 76, kBy0 = 150, kRowH = 76, kDozH = 58, kOutH = 58;
void spot_rect(int row, int col, float* x0, float* y0, float* x1, float* y1) {
  const float nx0 = kBx0 + kZeroW;
  if (row <= 2) {
    if (col == 0) {
      *x0 = kBx0, *x1 = nx0, *y0 = kBy0, *y1 = kBy0 + 3 * kRowH;
    } else if (col <= 12) {
      *x0 = nx0 + (col - 1) * kNumW, *x1 = *x0 + kNumW, *y0 = kBy0 + row * kRowH, *y1 = *y0 + kRowH;
    } else {
      *x0 = nx0 + 12 * kNumW, *x1 = *x0 + kColW, *y0 = kBy0 + row * kRowH, *y1 = *y0 + kRowH;
    }
  } else if (row == 3) {
    const int d = (std::max(1, std::min(12, col)) - 1) / 4;
    *x0 = nx0 + d * 4 * kNumW, *x1 = *x0 + 4 * kNumW, *y0 = kBy0 + 3 * kRowH + 6, *y1 = *y0 + kDozH;
  } else {
    const int o = (std::max(1, std::min(12, col)) - 1) / 2;
    *x0 = nx0 + o * 2 * kNumW, *x1 = *x0 + 2 * kNumW, *y0 = kBy0 + 3 * kRowH + 6 + kDozH + 6, *y1 = *y0 + kOutH;
  }
}
int bet_index(const RBet& b) {
  for (size_t i = 0; i < g_bets.size(); ++i)
    if (g_bets[i].kind == b.kind && g_bets[i].n == b.n) return static_cast<int>(i);
  return -1;
}
void place_chip(int row, int col) {  // (g_cm held)
  RBet b;
  if (!spot_bet(row, col, &b)) return;
  if (g_rs.on && !g_rs.revealed) return;
  const long long free = balance() - staged();
  const long long amt = std::min(free, chip_coins(g_c.chip, free));
  if (amt <= 0 || (g_c.chip < 4 && free < kChips[g_c.chip])) {
    say(free <= 0 ? "EVERY COIN YOU HAVE IS ON THE BOARD" : "NOT ENOUGH COINS FOR THAT CHIP", 0xFF4D5E);
    sound("book_tick", 0.5f);
    return;
  }
  if (g_showSpinBets) g_showSpinBets = false;
  int i = bet_index(b);
  if (i < 0) {
    g_bets.push_back(b);
    i = static_cast<int>(g_bets.size()) - 1;
  }
  g_bets[static_cast<size_t>(i)].amount += amt;
  g_undo.push_back(i);
  g_undoAmt.push_back(amt);
  play_music(kSfxChip);
  say(xrcoin::text(amt) + " ON " + bet_name(b.kind, b.n) + "  -  PAYS " + std::to_string(bet_odds(b.kind)) + " TO 1", 0x2EF2FF);
}
void undo_chip() {
  if (g_undo.empty()) return;
  const int i = g_undo.back();
  const long long amt = g_undoAmt.back();
  g_undo.pop_back();
  g_undoAmt.pop_back();
  if (i >= 0 && i < static_cast<int>(g_bets.size())) {
    g_bets[static_cast<size_t>(i)].amount -= amt;
    if (g_bets[static_cast<size_t>(i)].amount <= 0) {
      g_bets.erase(g_bets.begin() + i);
      for (int& u : g_undo)
        if (u > i) --u;
    }
  }
  sound("book_tick", 0.6f);
  say("CHIP TAKEN BACK", 0xC8D2E6);
}
void clear_bets() {
  if (g_bets.empty()) return;
  g_bets.clear();
  g_undo.clear();
  g_undoAmt.clear();
  play_music(kSfxChipsClear);
  say("BOARD CLEARED", 0xC8D2E6);
}
bool roulette_spin() {  // (g_cm held)
  if (g_c.career == 0) {
    say("LOAD A FRANCHISE CAREER", 0xFF4D5E);
    return false;
  }
  if (g_rs.on && !g_rs.revealed) return false;
  if (g_bets.empty()) {  // (nothing on the board: X puts the last spin's bets back)
    long long need = 0;
    for (const RBet& b : g_lastBets) need = xrcoin::add(need, b.amount);
    if (!g_lastBets.empty() && need <= balance()) {
      g_bets = g_lastBets;
      g_undo.clear();
      g_undoAmt.clear();
      g_showSpinBets = false;
      play_music(kSfxChip);
      say("SAME BETS AGAIN: " + xrcoin::text(need) + "  -  X TO SPIN", 0x2EF2FF);
      return true;
    }
    say(g_lastBets.empty() ? "PUT A CHIP ON THE BOARD FIRST" : "NOT ENOUGH COINS FOR THE SAME BETS", 0xFF4D5E);
    sound("book_tick", 0.5f);
    return false;
  }
  const long long total = staged();
  if (total > balance()) {
    say("NOT ENOUGH COINS FOR THOSE BETS", 0xFF4D5E);
    return false;
  }
  const long long n = ++g_c.rSpins;
  save_casino();
  if (g_debugBalance < 0 && (g_wallet.spend == nullptr || !g_wallet.spend(g_c.career, total, "STRIKERZ ROULETTE spin"))) {
    say("THE MACHINE COULDN'T TAKE THE COINS", 0xFF4D5E);
    return false;
  }
  if (g_debugBalance >= 0) g_debugBalance -= total;
  const int number = roulette_number(spin_bits(g_c.career, g_c.salt, 2, static_cast<uint64_t>(n)));
  long long ret = 0;
  for (const RBet& b : g_bets) ret = xrcoin::add(ret, bet_return(b, number));
  if (ret > 0) {
    if (g_debugBalance >= 0) g_debugBalance = xrcoin::clamp(g_debugBalance + ret);
    else if (g_wallet.add != nullptr) g_wallet.add(g_c.career, ret, "STRIKERZ ROULETTE win");
  }
  g_c.rIn = xrcoin::add(g_c.rIn, total);  // (r208: held, never wrapping)
  g_c.rOut = xrcoin::add(g_c.rOut, ret);
  g_c.rBest = std::max<long long>(g_c.rBest, ret - total);
  g_c.history.insert(g_c.history.begin(), number);
  if (g_c.history.size() > 12) g_c.history.resize(12);
  save_casino();
  g_sesIn = xrcoin::add(g_sesIn, total);
  g_sesOut = xrcoin::add(g_sesOut, ret);
  ++g_sesSpins;
  // the wheel gets a push, the ball goes in against it
  const double a0 = wheel_angle(g_now);
  g_wA0 = a0;
  g_wT0 = g_now;
  g_wB = 7.0;
  RSpin s;
  s.on = true;
  s.revealed = false;
  s.any = true;
  s.t0 = g_now;
  s.number = number;
  s.ret = ret;
  s.staked = total;
  s.bets = g_bets;
  s.tier = ret <= 0 ? 0 : (ret - total <= 0 ? 1 : win_tier(ret, std::max(1LL, total), false));
  g_rs = s;
  g_lastBets = g_bets;
  g_bets.clear();
  g_undo.clear();
  g_undoAmt.clear();
  g_showSpinBets = true;
  g_pendingWin = ret;
  sound("casino_nomore", 0.7f);
  sound("casino_wheel", 0.95f);    // (in-house: the wheel's whoosh, the drop, the bounces - timed to the ball)
  play_music(kSfxBallRoll);       // (real: the ball rolling round the track)
  say("NO MORE BETS", 0xFFB020);
  return true;
}

void go_screen(int s) {  // (g_cm held)
  if (s == g_screen) return;
  if (s == kSlots || s == kRoulette) g_prevGame = s;
  if (s == kPicker) g_pickerAt = g_screen == kPicker ? g_pickerAt : (g_screen == kBook ? 0 : g_screen);
  if (s == kPicker && g_screen != kPicker) g_pickerFrom = g_screen;
  g_screen = s;
  sound("book_tick", 0.6f);
}
bool busy_spinning() { return (g_ss.on && !g_ss.revealed) || (g_rs.on && !g_rs.revealed); }
}  // namespace

void set_wallet(const xrbook::Wallet& w) {
  std::lock_guard<std::mutex> lk(g_cm);
  g_wallet = w;
}
void set_picture_source(const uint8_t* (*pic)(const char*, int*, int*)) { g_pic = pic; }
int screen() {
  std::lock_guard<std::mutex> lk(g_cm);
  return g_screen;
}
void open_picker() {
  std::lock_guard<std::mutex> lk(g_cm);
  if (g_screen != kPicker) g_pickerFrom = g_screen;  // r203: its BACK returns there (r202: always the last casino game)
  g_pickerAt = g_screen == kPicker ? g_pickerAt : (g_screen == kBook ? 0 : g_screen);
  g_screen = kPicker;
  sound("book_tick", 0.6f);
}
bool spinning() {
  std::lock_guard<std::mutex> lk(g_cm);
  return busy_spinning();
}
namespace {
std::string hint_now() {  // (g_cm held)
  if (g_screen == kPicker) return "STICK: GAME   A / X: PLAY   B / Y: BACK";
  if (g_screen == kSlots) return "A / X or STICK BACK: SPIN   LEFT / RIGHT: BET   UP: LINES   B / Y: GAMES";
  if (g_screen == kRoulette) {
    const char* a = g_row < 5 ? "PLACE CHIP" : (g_col <= 4 ? "CHIP SIZE" : (g_col == 5 ? "CLEAR" : "SPIN"));
    return std::string("STICK: MOVE   A / X: ") + a + "   B / Y: " + (g_undo.empty() ? "GAMES" : "TAKE BACK") + "   MENU: GAMES";
  }
  return "";
}
}  // namespace
std::string machine_hint() {
  std::lock_guard<std::mutex> lk(g_cm);
  return hint_now();
}

void frame(double now) {
  std::lock_guard<std::mutex> lk(g_cm);
  g_now = now;
  const unsigned career = wallet_career();
  if (g_debugBalance < 0 && career != g_c.career) {
    if (career != 0) load_casino(career);
    else g_c = Casino();
  }
  // SLOTS: each reel's thunk as it lands, the tease, the reveal
  if (g_ss.on && !g_ss.revealed) {
    const double dt = now - g_ss.t0;
    for (int c = 0; c < kReels; ++c)
      if (!(g_ss.fired & (1 << c)) && dt >= g_ss.stopT[c]) {
        g_ss.fired |= 1 << c;
        bool scat = false;
        for (int r = 0; r < kRows; ++r) scat = scat || g_ss.r.grid[r][c] == kScatter;
        sound("casino_stop", 0.85f);
        if (scat) sound("book_ding", 0.6f);
        if (c + 1 == g_ss.teaseFrom && !g_ss.teaseSaid) {
          g_ss.teaseSaid = true;
          sound("casino_tease", 0.9f);
        }
      }
    if (now >= g_ss.revealAt) {
      g_ss.revealed = true;
      for (int c = 0; c < kReels; ++c) g_rest[c] = g_ss.r.stops[c];
      g_haveResult = true;
      g_pendingWin = 0;
      g_lastTier = g_ss.tier;
      if (g_ss.tier == 0) play_music(kMusicLose);  // (r202: a dead spin: a funny sting, rotating)
      if (g_ss.tier > 0) {
        g_showTier = g_ss.tier;
        g_showAt = now;
        win_sounds(g_ss.tier, g_ss.r.win >= g_ss.bet);
        const SlotResult& r = g_ss.r;
        say(r.mega ? "MEGA JACKPOT!  +" + std::to_string(r.win) + " COINS + A FREE TIZM PACK"
                   : (r.scatterMult > 0 && r.win == r.scatterMult * g_ss.bet ? std::to_string(r.scatters) + " 1/1 CARDS!  +" + std::to_string(r.win)
                                                                            : "WIN +" + std::to_string(r.win) + " COINS"),
            0xFFD23F);
      }
    }
  }
  // ROULETTE: the reveal
  if (g_rs.on && !g_rs.revealed && now - g_rs.t0 >= kReveal) {
    g_rs.revealed = true;
    g_pendingWin = 0;
    const int n = g_rs.number;
    const std::string col = n == 0 ? "GREEN" : (is_red(n) ? "RED" : "BLACK");
    if (g_rs.ret > 0) {
      g_showTier = std::max(1, g_rs.tier);
      g_showAt = now;
      win_sounds(g_showTier, g_rs.ret > g_rs.staked);
      say(std::to_string(n) + " " + col + "  -  YOU WIN " + xrcoin::text(g_rs.ret), 0xFFD23F);
    } else {
      play_music(kMusicLose);  // (r202: a funny sting, rotating)
      say(std::to_string(n) + " " + col + "  -  NO WIN THIS TIME", 0xC8D2E6);
    }
  }
}

namespace {
bool input_locked(const xrbook::Input& in);
}
bool input(const xrbook::Input& in) {
  std::lock_guard<std::mutex> lk(g_cm);
  // r204: a step's tick only when the selection really moved (or a button acted); the cursor pops / presses
  const uint64_t k0 = sel_key_locked();
  g_deferTicks = true;
  g_tickWant = 0.0f;
  const bool used = input_locked(in);
  g_deferTicks = false;
  const bool moved = sel_key_locked() != k0;
  if (g_tickWant > 0.0f && (moved || in.a || in.b || in.x || in.y)) sound("book_tick", g_tickWant);
  if (moved) g_moveAt = g_now;
  if (in.a && used) g_pressAt = g_now;
  return used;
}
uint64_t selection_key() {
  std::lock_guard<std::mutex> lk(g_cm);
  return sel_key_locked();
}
namespace {
bool input_locked(const xrbook::Input& in) {  // (g_cm held)
  bool used = false;
  if (g_screen == kPicker) {
    if (in.step && in.dir != 0) {  // (r203: up / down too - any push moves it)
      g_pickerAt = std::max(0, std::min(2, g_pickerAt + (in.dir == 1 || in.dir == 3 ? -1 : 1)));
      sound("book_tick", 0.4f);
      used = true;
    }
    if (in.a) {
      go_screen(g_pickerAt == 0 ? kBook : g_pickerAt);
      used = true;
    } else if (in.b) {
      go_screen(g_pickerFrom == kPicker ? kBook : g_pickerFrom);  // r203: back where it was opened from (r202: the last game)
      used = true;
    }
    return used || in.x || in.y;
  }
  if (g_screen == kSlots) {
    const bool pull = in.a || (in.step && in.dir == 4);
    if (pull) {
      if (g_ss.on && !g_ss.revealed) {
        // (pulled again while they run: nothing - the reels are already deciding)
      } else {
        slot_spin();
      }
      used = true;
    }
    if (!busy_spinning()) {
      if (in.y || (in.step && in.dir == 2)) {
        g_c.slotBet = in.y ? (g_c.slotBet + 1) % 5 : std::min(4, g_c.slotBet + 1);
        sound("book_tick", 0.5f), used = true;
        say("BET " + std::to_string(kSlotBets[g_c.slotBet]) + " A SPIN", 0x2EF2FF);
        save_casino();
      }
      if (in.step && in.dir == 1) {
        g_c.slotBet = std::max(0, g_c.slotBet - 1);
        sound("book_tick", 0.5f), used = true;
        say("BET " + std::to_string(kSlotBets[g_c.slotBet]) + " A SPIN", 0x2EF2FF);
        save_casino();
      }
      if (in.b) go_screen(kPicker), used = true;
    }
    if (in.x || (in.step && in.dir == 3)) {
      g_showLines = !g_showLines;
      sound("book_tick", 0.4f);
      used = true;
    }
    return used || in.b;
  }
  if (g_screen == kRoulette) {
    if (busy_spinning()) return in.a || in.b || in.x || in.y || in.dir != 0;
    if (in.step && in.dir != 0) {
      if (in.dir == 3) {
        if (g_row == 5) g_row = 4, g_col = std::min(12, g_col * 2 + 1);
        else if (g_row > 0 && !(g_row <= 2 && g_col == 0)) --g_row;
      } else if (in.dir == 4) {
        if (g_row <= 2 && g_col == 0) g_row = 3, g_col = 1;
        else if (g_row == 4) g_row = 5, g_col = std::min(6, std::max(0, (g_col - 1) / 2 + (g_col > 6 ? 1 : 0)));
        else if (g_row < 5) {
          ++g_row;
          if (g_row >= 3 && (g_col == 0 || g_col == 13)) g_col = g_col == 0 ? 1 : 12;
        }
      } else if (in.dir == 1 || in.dir == 2) {
        const int d = in.dir == 1 ? -1 : 1;
        if (g_row <= 2) g_col = std::max(0, std::min(13, g_col + d));
        else if (g_row == 3) g_col = std::max(1, std::min(12, ((g_col - 1) / 4 + d) * 4 + 1));
        else if (g_row == 4) g_col = std::max(1, std::min(12, ((g_col - 1) / 2 + d) * 2 + 1));
        else g_col = std::max(0, std::min(6, g_col + d));
      }
      sound("book_tick", 0.35f);
      used = true;
    }
    if (in.a) {
      if (g_row == 5) {
        if (g_col <= 4) g_c.chip = g_col, save_casino(), sound("book_tick", 0.5f);
        else if (g_col == 5) clear_bets();
        else roulette_spin();
      } else {
        place_chip(g_row, g_col);
      }
      used = true;
    }
    if (in.y) {
      g_c.chip = (g_c.chip + 1) % 5;
      save_casino();
      sound("book_tick", 0.5f);
      used = true;
    }
    if (in.x) roulette_spin(), used = true;
    if (in.b) {
      if (!g_undo.empty()) undo_chip();
      else go_screen(kPicker);
      used = true;
    }
    return used;
  }
  return false;
}

bool point_hit(const Hit& h, bool click) {  // (g_cm held) the pointer on hit box h
    switch (h.kind) {
      case 1:  // a game pill
        if (click && !busy_spinning()) go_screen(h.a);
        return true;
      case 8:  // a picker tile
        if (g_pickerAt != h.a) g_pickerAt = h.a;  // (r234: no tick for the pointer just passing over a tile)
        if (click) go_screen(h.a == 0 ? kBook : h.a);
        return true;
      case 2:  // a slot bet
        if (click && !busy_spinning()) g_c.slotBet = h.a, save_casino(), sound("book_tick", 0.5f);
        return true;
      case 3:  // the slot SPIN
        if (click) slot_spin();
        return true;
      case 9:  // the paylines
        if (click) g_showLines = !g_showLines, sound("book_tick", 0.4f);
        return true;
      case 4:  // a board spot
        if (g_row != h.a || g_col != h.b) g_row = h.a, g_col = h.b;  // (r234: no tick for the pointer just passing over a spot)
        if (click && !busy_spinning()) place_chip(h.a, h.b);
        return true;
      case 5:
        g_row = 5, g_col = h.a;
        if (click && !busy_spinning()) g_c.chip = h.a, save_casino(), sound("book_tick", 0.5f);
        return true;
      case 6:
        g_row = 5, g_col = 5;
        if (click && !busy_spinning()) clear_bets();
        return true;
      case 7:
        g_row = 5, g_col = 6;
        if (click) roulette_spin();
        return true;
      default:
        return true;
    }
}
}  // namespace

bool point(float u, float v, bool click) {
  std::unique_lock<std::mutex> lk(g_cm);
  const float x = u * xrbook::kW, y = v * xrbook::kH;
  // r204: bigger hit areas + a sticky hover (xrbook::hit_at); the cursor pops onto what it's on, a click presses it
  const int hi = xrbook::hit_at(g_hits, x, y, g_hover);
  if (hi < 0) return false;
  const Hit h = g_hits[static_cast<size_t>(hi)];
  const uint64_t k0 = sel_key_locked();
  point_hit(h, click);
  if (sel_key_locked() != k0) g_moveAt = g_now;
  if (click) g_pressAt = g_now;
  return true;
}

bool animating() {
  std::lock_guard<std::mutex> lk(g_cm);
  if (g_screen == kBook) return false;
  return busy_spinning() || g_now - g_showAt < 4.8 || g_now - g_msgAt < 0.5 || g_now - g_moveAt < 0.3 || g_now - g_pressAt < 0.3;
}
bool animating_board() {  // r205: without the cursor's pop / press (the machine's overlay does those)
  std::lock_guard<std::mutex> lk(g_cm);
  if (g_screen == kBook) return false;
  return busy_spinning() || g_now - g_showAt < 4.8 || g_now - g_msgAt < 0.5;
}
void cursor_anim(float* moveAgo, float* pressAgo) {
  std::lock_guard<std::mutex> lk(g_cm);
  *moveAgo = static_cast<float>(g_now - g_moveAt);
  *pressAgo = static_cast<float>(g_now - g_pressAt);
}

float lights() {
  std::lock_guard<std::mutex> lk(g_cm);
  float l = 0.0f;
  if (g_ss.on && !g_ss.revealed) {  // the reels running: the bulbs go wild (wilder in a tease)
    const double dt = g_now - g_ss.t0;
    const bool tease = g_ss.teaseFrom < kReels && dt > g_ss.stopT[g_ss.teaseFrom - 1];
    l = static_cast<float>((tease ? 0.6 : 0.32) + 0.18 * (0.5 + 0.5 * std::sin(g_now * (tease ? 26.0 : 16.0))));
  }
  if (g_rs.on && !g_rs.revealed) l = std::max(l, static_cast<float>(0.22 + 0.1 * (0.5 + 0.5 * std::sin(g_now * 9.0))));
  const double s = g_now - g_showAt;
  if (s >= 0.0 && g_showTier > 0) {
    static const double kLen[5] = {0, 1.2, 2.6, 4.2, 6.5};
    static const float kTop[5] = {0, 0.45f, 0.85f, 1.0f, 1.0f};
    const int t = std::min(4, g_showTier);
    if (s < kLen[t]) l = std::max(l, kTop[t] * static_cast<float>(s < 0.15 ? s / 0.15 : 1.0 - std::max(0.0, s - 0.6 * kLen[t]) / (0.4 * kLen[t])));
  }
  return std::max(0.0f, std::min(1.0f, l));
}

int take_show() {
  std::lock_guard<std::mutex> lk(g_cm);
  static double s_taken = -1e9;
  if (g_showTier > 0 && g_showAt != s_taken && g_now - g_showAt < 1.0) {
    s_taken = g_showAt;
    return g_showTier;
  }
  return 0;
}

namespace {
std::atomic<bool> g_engaged{false};
}
void set_engaged(bool on) { g_engaged = on; }
std::atomic<int> g_ctlMode{1};  // r205: 1 a hand holds the stick (arcade stick), 2 LATCHED (the thumbsticks + buttons, hands free)
void set_control_mode(int mode) { g_ctlMode = mode; }
bool engaged() { return g_engaged.load(); }
bool lever_pull() {
  std::lock_guard<std::mutex> lk(g_cm);
  if (g_screen != kSlots) return false;
  return slot_spin();
}
double slot_spun_at() {
  std::lock_guard<std::mutex> lk(g_cm);
  return g_ss.on ? g_ss.t0 : -1.0;
}
double clock_now() {
  std::lock_guard<std::mutex> lk(g_cm);
  return g_now;
}

Debug debug_get() {
  std::lock_guard<std::mutex> lk(g_cm);
  Debug d;
  d.screen = g_screen, d.slotBet = g_c.slotBet, d.chip = g_c.chip, d.pickerAt = g_pickerAt, d.row = g_row, d.col = g_col;
  d.now = g_now;
  d.bets = g_bets;
  d.number = g_rs.any ? g_rs.number : -1;
  d.history = g_c.history;
  d.showLines = g_showLines;
  d.sessionIn = g_sesIn;
  d.sessionOut = g_sesOut;
  return d;
}

void debug_salt(unsigned career, uint64_t salt) {
  std::lock_guard<std::mutex> lk(g_cm);
  load_casino(career);
  g_c.salt = salt;
}

// ======================================================================================================== the drawing
namespace {
constexpr float kW = xrbook::kW, kH = xrbook::kH;
constexpr uint32_t kNight0 = 0x0B0718, kNight1 = 0x05030C, kPanel = 0x140E2A, kPanelHi = 0x241A48, kPink = 0xFF2E88,
                   kCyan = 0x2EF2FF, kLime = 0x39FF88, kGold = 0xFFD23F, kAmber = 0xFFB020, kLoseRed = 0xFF4D5E, kInk = 0xF4F1FF,
                   kDim = 0x9A92B8, kEdge = 0x3A2E66, kChrome0 = 0xFFFFFF, kChrome1 = 0x9FB2D6, kFelt = 0x0B5A3A,
                   kFelt2 = 0x063A26, kRouletteRed = 0xD8202E, kRouletteBlack = 0x15131C, kZero = 0x0E9A4E;
constexpr double kPi = 3.14159265358979;

struct Pen {
  std::vector<xrtext::BoardPiece> v;
  xrmenu::Kit K{v};
};
void neon_text(Pen& P, const std::string& t, float x, float y, float px, float room, uint32_t rgb, int align, float glowA = 0.5f) {
  const float w = std::min(room, xrtext::board_text_width(t, px));
  const float cx = align == 0 ? x : (align < 0 ? x + 0.5f * w : x - 0.5f * w);
  if (glowA > 0.0f) P.K.glow(cx - 0.5f * w - px * 0.6f, y - px * 0.95f, cx + 0.5f * w + px * 0.6f, y + px * 0.95f, rgb, glowA);
  xrtext::BoardPiece p;
  p.kind = align == 0 ? xrtext::BoardPiece::kText : (align < 0 ? xrtext::BoardPiece::kTextLeft : xrtext::BoardPiece::kTextRight);
  p.x0 = x, p.y0 = y, p.x1 = room, p.y1 = px;
  p.rgb = xrmenu::mix(rgb, 0xFFFFFF, 0.55f);
  p.rgb2 = rgb;
  p.outline = std::max(2.0f, px * 0.07f);
  p.outlineRgb = xrmenu::mix(rgb, 0x000000, 0.45f);
  p.text = t;
  P.v.push_back(p);
}
void chrome_text(Pen& P, const std::string& t, float x, float y, float px, float room, int align, float angle = 0.0f) {
  xrtext::BoardPiece p;
  p.kind = align == 0 ? xrtext::BoardPiece::kText : (align < 0 ? xrtext::BoardPiece::kTextLeft : xrtext::BoardPiece::kTextRight);
  p.x0 = x, p.y0 = y, p.x1 = room, p.y1 = px;
  p.rgb = kChrome0;
  p.rgb2 = kChrome1;
  p.outline = std::max(3.0f, px * 0.08f);
  p.outlineRgb = 0x1A1036;
  p.shadow = px * 0.06f;
  p.shadowRgb = kPink;
  p.shadowA = 0.9f;
  p.angle = angle;
  p.text = t;
  P.v.push_back(p);
}
void rot_text(Pen& P, const std::string& t, float x, float y, float px, uint32_t rgb, float angleDeg) {
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kText;
  p.x0 = x, p.y0 = y, p.x1 = px * 3.0f, p.y1 = px;
  p.rgb = rgb;
  p.angle = angleDeg;
  p.text = t;
  P.v.push_back(p);
}
void coin(Pen& P, float cx, float cy, float r, float a = 1.0f) {
  P.K.glow(cx - 2.2f * r, cy - 2.2f * r, cx + 2.2f * r, cy + 2.2f * r, kGold, 0.35f * a);
  P.K.box(cx - r, cy - r, cx + r, cy + r, r, 0xB8860B, a);
  P.K.box(cx - 0.86f * r, cy - 0.86f * r, cx + 0.86f * r, cy + 0.86f * r, 0.86f * r, kGold, a, 0xE0A800);
  // r216: a BIT COIN - the pixel ring + the 8-bit B (r202-r215: a ring + "T")
  xrcoin::bit_face([&](float x0, float y0, float x1, float y1, uint32_t rgb) { P.K.box(x0, y0, x1, y1, 0, rgb, a); }, cx, cy, r);
}
void prism(Pen& P, float x0, float y0, float x1, float y1, float t, float a = 0.10f) {
  static const uint32_t kBand[6] = {0xFF2E88, 0xFFB020, 0xFFE94A, 0x39FF88, 0x2EF2FF, 0x9B5CFF};
  const float w = x1 - x0, h = y1 - y0;
  for (int i = 0; i < 9; ++i) {
    const float s = std::fmod(i * 0.137f + t * 0.05f, 1.0f);
    const float aa = x0 + s * w * 1.3f - 0.15f * w, b = aa + w * 0.07f;
    P.K.poly({aa, y1, b, y1, b + 0.55f * h, y0, aa + 0.55f * h - w * 0.03f, y0}, kBand[i % 6], a);
  }
}
std::string coins_text(long long n) { return xrcoin::text(n); }  // "1,234,567" (r208: xr_coins.h's)
void burst(Pen& P, float cx, float cy, float rx, float ry, uint32_t rgb, float a, int points, float angle) {
  xrtext::BoardPiece b;
  b.kind = xrtext::BoardPiece::kBurst;
  b.x0 = cx - rx, b.y0 = cy - ry, b.x1 = cx + rx, b.y1 = cy + ry;
  b.rgb = rgb;
  b.alpha = a;
  b.points = points;
  b.inner = 0.45f;
  b.angle = angle;
  P.v.push_back(b);
}
void image(Pen& P, const char* key, float x0, float y0, float x1, float y1, float a, bool* ok) {
  int w = 0, h = 0;
  const uint8_t* img = g_pic != nullptr ? g_pic(key, &w, &h) : nullptr;
  *ok = img != nullptr && w > 0 && h > 0;
  if (!*ok) return;
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kImage;
  p.x0 = x0, p.y0 = y0, p.x1 = x1, p.y1 = y1;
  p.img = img;
  p.iw = w;
  p.ih = h;
  p.alpha = a;
  p.trim = true;
  P.v.push_back(p);
}
// a fat line from (ax, ay) to (bx, by): a quad
void seg(Pen& P, float ax, float ay, float bx, float by, float w, uint32_t rgb, float a) {
  const float dx = bx - ax, dy = by - ay, l = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
  const float nx = -dy / l * 0.5f * w, ny = dx / l * 0.5f * w;
  P.K.poly({ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny}, rgb, a);
}
void disc(Pen& P, float cx, float cy, float r, uint32_t rgb, float a = 1.0f, uint32_t rgb2 = 0) {
  P.K.box(cx - r, cy - r, cx + r, cy + r, r, rgb, a, rgb2);
}

// ---- the symbols
struct SymLook {
  const char* name;
  const char* key;
  uint32_t rgb;
  const char* letter;
};
const SymLook kLook[kSyms] = {
    {"STRIKERZ WILD", "", 0xFF2E88, "S"},       {"1/1 CARD", "", 0xFFD23F, "1"},
    {"MARIO", "strikers_mario", 0xE8302A, "M"}, {"PEACH", "strikers_peach", 0xFF7EC8, "P"},
    {"DONKEY KONG", "strikers_donkeykong", 0xB06A2C, "DK"}, {"YOSHI", "strikers_yoshi", 0x7CE04A, "Y"},
    {"WARIO", "strikers_wario", 0xF2C21A, "W"}, {"LUIGI", "strikers_luigi", 0x2FB84A, "L"},
    {"BIT COIN", "", 0xFFD23F, "C"},
};
// a symbol in a cell w x h centred at (cx, cy) (h < w: squashed on the drum); a: its light (the drum's ends dim)
void draw_sym(Pen& P, int s, float cx, float cy, float w, float h, float a, float t) {
  const float m = std::min(w, h);
  const SymLook& L = kLook[s];
  if (s == kWild) {
    const float x0 = cx - 0.46f * w, x1 = cx + 0.46f * w, y0 = cy - 0.44f * h, y1 = cy + 0.44f * h;
    P.K.glow(x0 - 20, y0 - 20, x1 + 20, y1 + 20, kPink, 0.35f * a);
    P.K.box(x0, y0, x1, y1, 0.16f * m, 0x3A0A44, a, 0x0A2A4A);
    prism(P, x0 + 6, y0 + 6, x1 - 6, y1 - 6, t, 0.22f * a);
    P.K.ring(x0, y0, x1, y1, 0.16f * m, std::max(2.0f, 0.035f * m), kChrome0, a);
    chrome_text(P, "STRIKERZ", cx, cy - 0.06f * h, 0.30f * m, 0.84f * w, 0);  // r216: the machine's brand (r202: TIZM, 0.36 m)
    neon_text(P, "WILD", cx, cy + 0.27f * h, 0.17f * m, 0.8f * w, kCyan, 0, 0.25f * a);
  } else if (s == kScatter) {
    const float cw = 0.6f * m, ch = 0.84f * h;
    const float x0 = cx - 0.5f * cw, x1 = cx + 0.5f * cw, y0 = cy - 0.5f * ch, y1 = cy + 0.5f * ch;
    P.K.glow(x0 - 30, y0 - 30, x1 + 30, y1 + 30, kGold, 0.3f * a);
    P.K.box(x0, y0, x1, y1, 0.07f * m, 0x0A0A10, a, 0x22202C);
    prism(P, x0 + 5, y0 + 5, x1 - 5, y1 - 5, t * 1.7f, 0.18f * a);
    P.K.ring(x0, y0, x1, y1, 0.07f * m, std::max(2.0f, 0.04f * m), kGold, a);
    P.K.ring(x0 + 0.06f * cw, y0 + 0.05f * ch, x1 - 0.06f * cw, y1 - 0.05f * ch, 0.05f * m, std::max(1.5f, 0.015f * m), 0xB8860B, a);
    neon_text(P, "1/1", cx, cy - 0.04f * h, 0.3f * m, 0.9f * cw, kGold, 0, 0.3f * a);
    P.K.text("BLACK", cx, cy + 0.28f * h, 0.11f * m, 0.9f * cw, 0xC9C2E6, 0.0f);
  } else if (s == kCoin) {
    coin(P, cx, cy, 0.34f * m, a);
  } else {
    const float x0 = cx - 0.45f * w, x1 = cx + 0.45f * w, y0 = cy - 0.44f * h, y1 = cy + 0.44f * h;
    P.K.box(x0, y0, x1, y1, 0.2f * m, xrmenu::mix(L.rgb, 0x000000, 0.62f), a, xrmenu::mix(L.rgb, 0x000000, 0.85f));
    P.K.glow(cx - 0.4f * w, cy - 0.4f * h, cx + 0.4f * w, cy + 0.4f * h, L.rgb, 0.35f * a);
    P.K.ring(x0, y0, x1, y1, 0.2f * m, std::max(2.0f, 0.03f * m), L.rgb, a);
    bool ok = false;
    image(P, L.key, x0 + 0.06f * w, y0 + 0.04f * h, x1 - 0.06f * w, y1 - 0.02f * h, a, &ok);
    if (!ok) {  // (no picture on this headset: the captain's letter in neon)
      neon_text(P, L.letter, cx, cy - 0.06f * h, 0.46f * m, 0.8f * w, L.rgb, 0, 0.4f * a);
      P.K.text(L.name, cx, cy + 0.3f * h, 0.12f * m, 0.84f * w, kInk, 1.0f);
    }
  }
}

// ---- the header (shared): TIZM + the game, the game pills, your coins (paid but not shown yet: held back)
void header(Pen& P, const char* game, float t, long long shownCoinsDelta) {
  P.K.box(0, 0, kW, kH, 30, kNight0, 1.0f, kNight1);
  prism(P, 0, 0, kW, 120, t);
  P.K.glow(-200, -200, 500, 400, kPink, 0.22f);
  P.K.glow(kW - 500, kH - 400, kW + 200, kH + 200, kCyan, 0.18f);
  // r216 (Lance 11-11 REBRAND): STRIKERZ, not TIZM (the cards' brand) - 70 px (r202: TIZM 92), the game's name moved right +
  // 58 px (r202: 262, 64 px), clear of the game pills at 690 (the sportsbook's header the same)
  chrome_text(P, "STRIKERZ", 24, 62, 70, 300, -1);
  const float flick = std::fmod(t, 9.0f) > 8.75f ? (std::sin(t * 80.0f) > 0.0f ? 0.25f : 0.6f) : 0.55f;
  neon_text(P, game, 338, 66, 58, 340, kPink, -1, flick);
  // the game pills (the pointer picks one; B: the picker)
  static const char* const kNames[3] = {"SPORTSBOOK", "SLOTS", "ROULETTE"};
  for (int i = 0; i < 3; ++i) {
    const float x0 = 690 + i * 168.0f, x1 = x0 + 160;
    const bool on = g_screen == i;
    if (on) P.K.glow(x0 - 14, 26, x1 + 14, 104, kPink, 0.3f);
    P.K.box(x0, 42, x1, 86, 22, on ? 0x4A1440 : 0x150E2C);
    P.K.ring(x0, 42, x1, 86, 22, on ? 3.5f : 2.0f, on ? kPink : kEdge);
    P.K.text(kNames[i], 0.5f * (x0 + x1), 65, 26, 146, on ? 0xFFFFFF : kDim, on ? 2.0f : 0.0f);
    g_hits.push_back({x0, 42, x1, 86, 1, i, 0});
  }
  xrmenu::glyph(P.K, "B", 690, 108, 30);
  P.K.left("GAMES", 726, 108, 24, 120, kDim, 0.0f);
  const long long bal = balance() - shownCoinsDelta;
  coin(P, 1290, 62, 30);
  neon_text(P, xrcoin::fit(bal, 15), 1630, 62, 60, 300, kGold, 1, 0.35f);  // (r208: in full to 999,999,999,999, then short: "4.09T")
  P.K.text("BIT COINS", 1500, 112, 26, 260, kDim, 0.0f);
}
// the foot: the last word or the ticker
void footer(Pen& P, const std::string& tickText, float t) {
  const bool fresh = g_now - g_msgAt < 5.0;
  const std::string tick = fresh ? g_msg : tickText;
  const uint32_t rgb = fresh ? g_msgRgb : kAmber;
  P.K.box(0, 904, kW, 958, 26, 0x0A0716);
  P.K.ring(0, 904, kW, 958, 26, 2, fresh ? rgb : 0x3A2E66);
  if (!fresh) {
    const float tw = xrtext::board_text_width(tick, 32) + 200.0f;
    const float off = std::fmod(t * 140.0f, tw);
    for (int r = 0; r < 3; ++r) {
      const float x = kW - off + r * tw - tw;
      if (x > kW - 20 || x + tw < 0) continue;
      P.K.left(tick, x, 932, 32, 0.0f, rgb, 0.0f);
    }
  } else {
    P.K.text(tick, 0.5f * kW, 932, 32, kW - 60.0f, rgb, 0.0f);
  }
}
void session_box(Pen& P, float x0, float y0, float x1, float y1) {
  P.K.box(x0, y0, x1, y1, 22, 0x120C26, 1.0f, 0x07040F);
  P.K.ring(x0, y0, x1, y1, 22, 2.5f, 0x4A2A80);
  const long long net = g_sesOut - g_sesIn - g_pendingWin;
  const float cy = 0.5f * (y0 + y1);
  P.K.left("THIS SESSION", x0 + 22, cy - 18, 26, 300, kDim, 0.0f);
  P.K.left(std::to_string(g_sesSpins) + (g_sesSpins == 1 ? " SPIN" : " SPINS") + "  -  IN " + xrcoin::fit(g_sesIn, 11), x0 + 22, cy + 18, 24,
           x1 - x0 - 200, kDim, 0.0f);
  neon_text(P, (net > 0 ? "+" : "") + xrcoin::fit(net, 9), x1 - 22, cy, 48, 220, net > 0 ? kLime : (net < 0 ? kLoseRed : kInk), 1, 0.3f);
}

// ---------------------------------------------------------------------------------------------------- SLOTS: drawing
constexpr float kReelX0 = 84, kReelW = 176, kReelGap = 8, kCell = 166, kDrumR = kCell / 0.5878f, kReelCy = 455;
constexpr float kWinHalf = 0.9781f * kDrumR;  // (the window shows the drum to +-78 degrees)
const uint32_t kLineRgb[kLines] = {0xFF2E88, 0x2EF2FF, 0x39FF88, 0xFFD23F, 0x9B5CFF, 0xFF8A3D, 0x3D9BFF, 0xFF5CE1, 0xB8FF3D, 0xFFFFFF};
float reel_cx(int c) { return kReelX0 + c * (kReelW + kReelGap) + 0.5f * kReelW; }
float row_cy(int r) { return kReelCy + (r - 1) * kDrumR * 0.5878f; }

void draw_slots(Pen& P, float t, bool attract) {
  const double now = g_now;
  const bool spinning = g_ss.on && !g_ss.revealed;
  const double dt = now - g_ss.t0;
  const double sa = g_now - g_showAt;
  const bool showing = g_haveResult && g_showTier > 0 && sa >= 0;
  // the count-up held back from the coins up top
  long long held = g_pendingWin;
  int shownWin = 0;
  if (showing && g_haveResult) {
    const SlotResult& r = g_ss.r;
    const double dur = std::min(4.0, 0.6 + 0.35 * std::log2(1.0 + static_cast<double>(r.win) / std::max(1, g_ss.bet)));
    const double f = std::min(1.0, sa / dur);
    shownWin = static_cast<int>(r.win * f);
    held += r.win - shownWin;
  }
  header(P, "SLOTS", t, held);
  // the cabinet's reel window: chrome bezel, the drums behind glass
  const float wx0 = kReelX0 - 26, wx1 = reel_cx(4) + 0.5f * kReelW + 26, wy0 = kReelCy - kWinHalf - 18, wy1 = kReelCy + kWinHalf + 18;
  P.K.glow(wx0 - 60, wy0 - 60, wx1 + 60, wy1 + 60, spinning ? kPink : kCyan, spinning ? 0.32f + 0.1f * std::sin(t * 14.0f) : 0.18f);
  P.K.box(wx0 - 10, wy0 - 10, wx1 + 10, wy1 + 10, 34, 0xC9D2E8, 1.0f, 0x5A6482);
  P.K.box(wx0, wy0, wx1, wy1, 26, 0x05030A);
  bool teaseOn = false;
  for (int c = 0; c < kReels; ++c) {
    double speed = 0;
    const double pos = reel_pos(c, &speed);
    const float cx = reel_cx(c), x0 = cx - 0.5f * kReelW, x1 = cx + 0.5f * kReelW;
    const bool tease = spinning && g_ss.teaseFrom <= c && dt > g_ss.stopT[g_ss.teaseFrom - 1] && dt < g_ss.stopT[c];
    teaseOn = teaseOn || tease;
    // the drum: lit in the middle, falling off to its ends
    P.K.box(x0, kReelCy - kWinHalf, x1, kReelCy + kWinHalf, 14, 0x1A1430, 1.0f, 0x100C22);
    const int top = static_cast<int>(std::floor(pos));
    const double frac = pos - top;
    const bool blur = speed > 6.0;
    for (int k = -2; k <= 3; ++k) {
      const double rowF = k - frac;  // (its row: 0 top, 1 middle, 2 bottom)
      const double ang = (rowF - 1.0) * 36.0 * kPi / 180.0;
      if (std::fabs(ang) > 80.0 * kPi / 180.0) continue;
      const float cy = kReelCy + static_cast<float>(std::sin(ang)) * kDrumR;
      const float sq = static_cast<float>(std::cos(ang));
      const int s = kStrip[c][((top + k) % kStops + kStops) % kStops];
      const float lit = 0.35f + 0.65f * sq * sq;
      if (blur) {  // (a streak of its colour: too fast to read)
        const uint32_t rgb = kLook[s].rgb;
        const float hh = kCell * sq * 0.5f + static_cast<float>(speed) * 3.2f;
        P.K.box(cx - 0.36f * kReelW, cy - hh, cx + 0.36f * kReelW, cy + hh, 0.2f * kReelW, rgb, 0.32f * lit, xrmenu::mix(rgb, 0x000000, 0.6f));
        P.K.v.back().alpha2 = 0.05f;
      } else {
        draw_sym(P, s, cx, cy, kCell, kCell * sq, lit, t);
      }
    }
    // the drum's shading (dark at the top and bottom) + the glass's sheen
    P.K.box(x0, kReelCy - kWinHalf, x1, kReelCy - 0.45f * kWinHalf, 14, 0x000000, 0.75f, 0);
    P.K.v.back().alpha2 = 0.0f;
    P.K.box(x0, kReelCy + 0.45f * kWinHalf, x1, kReelCy + kWinHalf, 14, 0x000000, 0.0f, 0);
    P.K.v.back().alpha2 = 0.75f;
    P.K.box(x0 + 8, kReelCy - kWinHalf + 6, x0 + 30, kReelCy + kWinHalf - 6, 10, 0xFFFFFF, 0.06f);
    if (tease) {  // (the near-miss: this reel held, its frame burning)
      const float pulse = 0.5f + 0.5f * std::sin(t * 20.0f);
      P.K.glow(x0 - 40, kReelCy - kWinHalf - 40, x1 + 40, kReelCy + kWinHalf + 40, kGold, 0.35f + 0.3f * pulse);
      P.K.ring(x0 - 4, kReelCy - kWinHalf - 4, x1 + 4, kReelCy + kWinHalf + 4, 16, 6.0f, pulse > 0.5f ? kGold : kPink);
    }
  }
  P.K.ring(wx0 - 10, wy0 - 10, wx1 + 10, wy1 + 10, 34, 5.0f, spinning ? kPink : kCyan);
  // the paylines' tabs both sides (lit when they pay)
  for (int l = 0; l < kLines; ++l) {
    const bool won = g_haveResult && !spinning && g_ss.r.lineMult[l] > 0;
    const float ly0 = row_cy(kLine[l][0]) + (l % 4 - 1.5f) * 26.0f, ly1 = row_cy(kLine[l][4]) + (l % 4 - 1.5f) * 26.0f;
    for (int side = 0; side < 2; ++side) {
      const float x = side == 0 ? wx0 - 24 : wx1 + 24, y = side == 0 ? ly0 : ly1;
      P.K.box(x - 17, y - 12, x + 17, y + 12, 8, won ? kLineRgb[l] : 0x1A1430);
      P.K.ring(x - 17, y - 12, x + 17, y + 12, 8, 2.0f, kLineRgb[l], won || g_showLines ? 1.0f : 0.55f);
      P.K.text(std::to_string(l + 1), x, y + 1, 20, 32, won ? 0x000000 : kLineRgb[l], 0.0f);
    }
  }
  // the lines: all of them (X), or the ones that paid
  const bool result = g_haveResult && !spinning;
  for (int l = 0; l < kLines; ++l) {
    const bool won = result && g_ss.r.lineMult[l] > 0;
    if (!won && !g_showLines) continue;
    const float off = (l % 4 - 1.5f) * 4.0f;
    float px = wx0 - 8, py = row_cy(kLine[l][0]) + (l % 4 - 1.5f) * 26.0f;
    const float a = won ? 0.85f + 0.15f * std::sin(t * 8.0f + l) : 0.55f;
    for (int c = 0; c <= kReels; ++c) {
      const float nx = c < kReels ? reel_cx(c) : wx1 + 8;
      const float ny = c < kReels ? row_cy(kLine[l][c]) + off : row_cy(kLine[l][4]) + (l % 4 - 1.5f) * 26.0f;
      if (won) seg(P, px, py, nx, ny, 22, kLineRgb[l], 0.25f * a);
      seg(P, px, py, nx, ny, won ? 9.0f : 5.0f, kLineRgb[l], a);
      px = nx, py = ny;
    }
  }
  if (result && g_ss.r.cells != 0) {  // the cells that paid: gold rings
    for (int r = 0; r < kRows; ++r)
      for (int c = 0; c < kReels; ++c)
        if (g_ss.r.cells & (1u << (r * kReels + c))) {
          const float cx = reel_cx(c), cy = row_cy(r), hw = 0.5f * kReelW - 6, hh = 0.5f * kCell * std::cos(static_cast<float>((r - 1) * 36.0 * kPi / 180.0)) - 4;
          P.K.glow(cx - hw - 20, cy - hh - 20, cx + hw + 20, cy + hh + 20, kGold, 0.3f + 0.15f * std::sin(t * 10.0f));
          P.K.ring(cx - hw, cy - hh, cx + hw, cy + hh, 16, 5.0f, kGold);
        }
  }
  // under the reels: the bet, the WIN window, the lever
  {
    const float y0 = 790, y1 = 890;
    P.K.box(30, y0, 1036, y1, 24, 0x120C26, 1.0f, 0x07040F);
    P.K.ring(30, y0, 1036, y1, 24, 2.5f, 0x4A2A80);
    P.K.left("BET", 52, 840, 30, 80, kDim, 0.0f);
    for (int i = 0; i < 5; ++i) {
      const float cx = 152 + i * 74.0f, cy = 840;
      const bool on = g_c.slotBet == i;
      if (on) P.K.glow(cx - 44, cy - 44, cx + 44, cy + 44, kCyan, 0.4f);
      disc(P, cx, cy, 31, on ? 0x0E4A5A : 0x1A1234);
      P.K.ring(cx - 31, cy - 31, cx + 31, cy + 31, 31, on ? 4.0f : 2.0f, on ? kCyan : kEdge);
      P.K.text(std::to_string(kSlotBets[i]), cx, cy + 1, 28, 56, on ? 0xFFFFFF : kDim, on ? 2.0f : 0.0f);
      g_hits.push_back({cx - 34, cy - 34, cx + 34, cy + 34, 2, i, 0});
    }
    // WIN: counting up
    const float wx = 520, wxx = 790;
    P.K.box(wx, y0 + 14, wxx, y1 - 14, 16, 0x05030A);
    P.K.ring(wx, y0 + 14, wxx, y1 - 14, 16, 3.0f, shownWin > 0 ? kGold : kEdge);
    P.K.left("WIN", wx + 14, 840, 26, 60, kDim, 0.0f);
    neon_text(P, coins_text(result ? shownWin : 0), wxx - 16, 840, 50, 190, shownWin > 0 ? kGold : 0x5A5478, 1, shownWin > 0 ? 0.45f : 0.0f);
    // the lever: SPIN (pull the stick back / A)
    const float bx0 = 812, bx1 = 1020;
    const bool can = !spinning && balance() >= kSlotBets[g_c.slotBet];
    const float pulse = can ? 0.5f + 0.5f * std::sin(t * 5.0f) : 0.0f;
    if (can) P.K.glow(bx0 - 24, y0 - 6, bx1 + 24, y1 + 6, kPink, 0.25f + 0.25f * pulse);
    P.K.box(bx0, y0 + 10, bx1, y1 - 10, 30, spinning ? 0x3A2440 : 0xC0186A, 1.0f, spinning ? 0x241830 : 0x6A0A3A);
    P.K.ring(bx0, y0 + 10, bx1, y1 - 10, 30, 3.0f, 0xFFB0D8, can ? 1.0f : 0.4f);
    P.K.text(spinning ? "GOOD LUCK" : "SPIN", 0.5f * (bx0 + bx1), 832, 40, bx1 - bx0 - 20, 0xFFFFFF, 2.0f);
    P.K.text("HANDLE  /  STICK BACK  /  A", 0.5f * (bx0 + bx1), 866, 18, bx1 - bx0 - 20, 0xFFD0E8, 0.0f);
    g_hits.push_back({bx0, y0, bx1, y1, 3, 0, 0});
  }
  // the PAYS table at the bet (coins)
  {
    const float x0 = 1074, x1 = 1636;
    const int bet = kSlotBets[g_c.slotBet];
    P.K.box(x0, 132, x1, 776, 26, 0x120C26, 1.0f, 0x07040F);
    P.K.ring(x0, 132, x1, 776, 26, 3.0f, 0x4A2A80);
    neon_text(P, "PAYS", x0 + 26, 170, 40, 200, kPink, -1, 0.4f);
    P.K.right("AT BET " + std::to_string(bet) + "  -  5 / 4 / 3 IN A ROW", x1 - 22, 172, 24, 380, kDim, 0.0f);
    static const int kOrder[kSyms] = {kWild, kScatter, kMario, kPeach, kDk, kYoshi, kWario, kLuigi, kCoin};
    for (int i = 0; i < kSyms; ++i) {
      const int s = kOrder[i];
      const float cy = 226 + i * 56.0f;
      if (i % 2 == 0) P.K.box(x0 + 10, cy - 27, x1 - 10, cy + 27, 12, 0x1A1234, 0.6f);
      draw_sym(P, s, x0 + 50, cy, 56, 52, 1.0f, t);
      P.K.left(kLook[s].name, x0 + 88, cy + 1, 24, 176, kInk, 0.0f);
      for (int n = 5; n >= 3; --n) {
        const long long pay = s == kScatter ? static_cast<long long>(kScatterPay[n]) * bet : static_cast<long long>(kLinePay[s][n]) * bet / 10;
        const float rx = x1 - 22 - (5 - n) * 104.0f;
        const bool lit = result && ((s == kScatter && g_ss.r.scatters == n && g_ss.r.scatterMult > 0));
        P.K.right(coins_text(pay), rx, cy + 1, n == 5 ? 30 : 26, 100, lit ? kGold : (n == 5 ? kGold : 0xC9C2E6), n == 5 ? 1.0f : 0.0f);
      }
    }
    P.K.text("WILD STANDS IN FOR ALL BUT 1/1  -  1/1 PAYS ANYWHERE (X BET)", 0.5f * (x0 + x1), 752, 20, x1 - x0 - 30, kDim, 0.0f);
    P.K.text("5 STRIKERZ WILDS = MEGA JACKPOT + A FREE PACK", 0.5f * (x0 + x1), 724, 22, x1 - x0 - 30, kPink, 0.0f);
    (void)x0;
  }
  session_box(P, 1074, 790, 1636, 890);
  // the near-miss's words
  if (teaseOn) neon_text(P, "ONE MORE...!", 0.5f * (wx0 + wx1), wy0 - 34, 44, 600, kGold, 0, 0.5f);
  // the win's show over the reels
  if (showing && g_ss.tier >= 2) {
    const SlotResult& r = g_ss.r;
    const float a = static_cast<float>(sa < 0.25 ? sa / 0.25 : std::max(0.0, 1.0 - std::max(0.0, sa - 3.4) / 1.2));
    if (a > 0.0f) {
      const float cx = 0.5f * (wx0 + wx1), cy = kReelCy;
      if (g_ss.tier >= 3) {
        P.K.box(wx0, wy0, wx1, wy1, 26, 0x1A0E00, 0.55f * a);
        burst(P, cx, cy, 560, 380, r.mega ? kPink : kGold, 0.4f * a, 20, static_cast<float>(sa) * 50.0f);
      }
      const bool strobe = std::fmod(sa * 8.0, 1.0) < 0.5;
      const char* big = r.mega ? "MEGA JACKPOT!" : (g_ss.tier >= 3 ? "HUGE WIN!" : "BIG WIN!");
      neon_text(P, big, cx, cy - 90, g_ss.tier >= 3 ? 120.0f : 96.0f, 900, strobe ? kGold : kPink, 0, 0.6f * a);
      coin(P, cx - 240, cy + 50, 52, a);
      neon_text(P, "+" + coins_text(shownWin), cx + 40, cy + 50, 104, 520, kGold, 0, 0.5f * a);
      if (r.mega) neon_text(P, "+ A FREE TIZM PACK  -  OPEN IT IN THE BINDER", cx, cy + 160, 38, 900, kCyan, 0, 0.4f * a);
    }
  }
  if (attract && !spinning) {  // nobody at it: the invitation
    const float cx = 0.5f * (wx0 + wx1);
    P.K.box(wx0 + 40, kReelCy - 120, wx1 - 40, kReelCy + 120, 40, 0x0A0716, 0.82f);
    P.K.ring(wx0 + 40, kReelCy - 120, wx1 - 40, kReelCy + 120, 40, 4, kPink, 0.6f + 0.4f * std::sin(t * 4.0f));
    neon_text(P, "STRIKERZ SLOTS", cx, kReelCy - 50, 92, 820, kPink, 0, 0.5f + 0.2f * std::sin(t * 3.0f));
    neon_text(P, "PULL THE HANDLE  -  OR GRAB THE STICK", cx, kReelCy + 50, 44, 820, kCyan, 0, 0.35f);
  }
  footer(P, "STRIKERZ SLOTS  -  10 LINES  -  RTP 94.2% (HOUSE EDGE 5.8%)  -  5 STRIKERZ WILDS ON A LINE = MEGA JACKPOT 500X + A FREE PACK  -  "
            "3+ 1/1 CARDS ANYWHERE PAY  -  STICK LEFT / RIGHT: BET  -  UP: LINES  -  BIT COINS ONLY",
         t);
}

// ------------------------------------------------------------------------------------------------- ROULETTE: drawing
constexpr float kWheelCx = 362, kWheelCy = 520, kWheelR = 330;
uint32_t pocket_rgb(int n) { return n == 0 ? kZero : (is_red(n) ? kRouletteRed : kRouletteBlack); }
void wheel_xy(float cx, float cy, double a, float r, float* x, float* y) {
  *x = cx + r * static_cast<float>(std::sin(a));
  *y = cy - r * static_cast<float>(std::cos(a));
}
void sector(Pen& P, float cx, float cy, double a0, double a1, float r0, float r1, uint32_t rgb, float alpha = 1.0f) {
  float x[4], y[4];
  wheel_xy(cx, cy, a0, r1, &x[0], &y[0]);
  wheel_xy(cx, cy, a1, r1, &x[1], &y[1]);
  wheel_xy(cx, cy, a1, r0, &x[2], &y[2]);
  wheel_xy(cx, cy, a0, r0, &x[3], &y[3]);
  P.K.poly({x[0], y[0], x[1], y[1], x[2], y[2], x[3], y[3]}, rgb, alpha);
}
// the wheel at angle `w` (radians, clockwise), R its radius; the ball (if any) at angle `ba`, `br` (0..1 of R); `hi` a pocket
// lit (its index, -1 none)
void draw_wheel(Pen& P, float cx, float cy, float R, double w, bool ball, double ba, double br, int hi, float t, bool small = false) {
  const double step = 2.0 * kPi / 37.0;
  P.K.glow(cx - 1.25f * R, cy - 1.25f * R, cx + 1.25f * R, cy + 1.25f * R, kGold, 0.18f);
  disc(P, cx + 0.02f * R, cy + 0.04f * R, 1.02f * R, 0x000000, 0.5f);
  disc(P, cx, cy, R, 0x6A3A1A, 1.0f, 0x2A1408);            // the wooden bowl
  P.K.ring(cx - R, cy - R, cx + R, cy + R, R, std::max(2.0f, 0.012f * R), kGold);
  disc(P, cx, cy, 0.92f * R, 0x1A0E08, 1.0f, 0x0A0604);    // the ball track
  disc(P, cx, cy, 0.80f * R, 0x3A2412);
  for (int d = 0; d < 8; ++d) {  // the diamond deflectors on the track (they don't turn)
    const double a = d * kPi / 4.0 + kPi / 8.0;
    float x0, y0, x1, y1, x2, y2, x3, y3;
    wheel_xy(cx, cy, a, 0.875f * R, &x0, &y0);
    wheel_xy(cx, cy, a + 0.035, 0.85f * R, &x1, &y1);
    wheel_xy(cx, cy, a, 0.825f * R, &x2, &y2);
    wheel_xy(cx, cy, a - 0.035, 0.85f * R, &x3, &y3);
    P.K.poly({x0, y0, x1, y1, x2, y2, x3, y3}, 0xD8DEEA);
  }
  for (int i = 0; i < 37; ++i) {  // the numbers' ring + the pockets
    const int n = kWheel[i];
    const double a0 = w + (i - 0.5) * step, a1 = a0 + step;
    const uint32_t c = pocket_rgb(n);
    sector(P, cx, cy, a0, a1, 0.66f * R, 0.80f * R, c);
    sector(P, cx, cy, a0, a1, 0.53f * R, 0.66f * R, xrmenu::mix(c, 0x000000, 0.35f));
    if (i == hi) {
      sector(P, cx, cy, a0, a1, 0.53f * R, 0.80f * R, kGold, 0.35f + 0.25f * std::sin(t * 9.0f));
    }
    // the frets between pockets (chrome)
    float fx0, fy0, fx1, fy1;
    wheel_xy(cx, cy, a0, 0.53f * R, &fx0, &fy0);
    wheel_xy(cx, cy, a0, 0.80f * R, &fx1, &fy1);
    seg(P, fx0, fy0, fx1, fy1, std::max(1.5f, 0.008f * R), 0xD0D6E4, 0.9f);
    if (!small) {
      float tx, ty;
      wheel_xy(cx, cy, a0 + 0.5 * step, 0.73f * R, &tx, &ty);
      rot_text(P, std::to_string(n), tx, ty, 0.058f * R, 0xFFFFFF, static_cast<float>((a0 + 0.5 * step) * 180.0 / kPi));
    }
  }
  P.K.ring(cx - 0.80f * R, cy - 0.80f * R, cx + 0.80f * R, cy + 0.80f * R, 0.80f * R, std::max(2.0f, 0.008f * R), kGold);
  P.K.ring(cx - 0.53f * R, cy - 0.53f * R, cx + 0.53f * R, cy + 0.53f * R, 0.53f * R, std::max(2.0f, 0.01f * R), 0xD0D6E4);
  // the cone + the turret
  disc(P, cx, cy, 0.53f * R, 0x8A5A2A, 1.0f, 0x4A2A10);
  disc(P, cx, cy, 0.40f * R, 0xC08A3A, 1.0f, 0x7A4A1A);
  P.K.glow(cx - 0.35f * R, cy - 0.45f * R, cx + 0.1f * R, cy - 0.05f * R, 0xFFFFFF, 0.18f);
  for (int k = 0; k < 4; ++k) {
    const double a = w + k * kPi / 2.0;
    float x0, y0, x1, y1;
    wheel_xy(cx, cy, a, 0.06f * R, &x0, &y0);
    wheel_xy(cx, cy, a, 0.36f * R, &x1, &y1);
    seg(P, x0, y0, x1, y1, 0.045f * R, 0xE8ECF4, 1.0f);
    disc(P, x1, y1, 0.035f * R, 0xFFFFFF);
  }
  disc(P, cx, cy, 0.09f * R, 0xF0F3FA, 1.0f, 0x8A94AA);
  disc(P, cx - 0.025f * R, cy - 0.03f * R, 0.03f * R, 0xFFFFFF, 0.8f);
  if (ball) {
    float bx, by;
    wheel_xy(cx, cy, ba, static_cast<float>(br) * R, &bx, &by);
    const float r = std::max(4.0f, 0.032f * R);
    disc(P, bx + 0.3f * r, by + 0.4f * r, 1.05f * r, 0x000000, 0.45f);
    disc(P, bx, by, r, 0xF4F6FA, 1.0f, 0xA8B0C4);
    disc(P, bx - 0.3f * r, by - 0.35f * r, 0.38f * r, 0xFFFFFF, 0.9f);
  }
}
void chip_disc(Pen& P, float cx, float cy, float r, long long amount, int denom) {
  static const uint32_t kChipRgb[5] = {0x1E9E5A, 0x2E6AE8, 0x2A2A34, 0xC0186A, 0xE0A800};
  const uint32_t c = kChipRgb[std::max(0, std::min(4, denom))];
  disc(P, cx + 2, cy + 3, r, 0x000000, 0.45f);
  disc(P, cx, cy, r, c, 1.0f, xrmenu::mix(c, 0x000000, 0.3f));
  for (int k = 0; k < 6; ++k) {  // the edge spots
    const double a = k * kPi / 3.0;
    float x0, y0, x1, y1;
    wheel_xy(cx, cy, a, 0.72f * r, &x0, &y0);
    wheel_xy(cx, cy, a, 0.98f * r, &x1, &y1);
    seg(P, x0, y0, x1, y1, 0.22f * r, 0xFFFFFF, 0.9f);
  }
  disc(P, cx, cy, 0.62f * r, xrmenu::mix(c, 0xFFFFFF, 0.15f));
  P.K.ring(cx - 0.62f * r, cy - 0.62f * r, cx + 0.62f * r, cy + 0.62f * r, 0.62f * r, std::max(1.5f, 0.06f * r), 0xFFFFFF, 0.8f);
  if (amount <= 0) return;
  const std::string t = amount >= 10000 ? xrcoin::compact(amount) : std::to_string(amount);  // (r208: "12K", "4.2B")
  P.K.text(t, cx, cy + 1, 0.5f * r * (t.size() > 3 ? 0.8f : 1.0f), 1.2f * r, 0xFFFFFF, 1.5f);
}
int denom_of(long long amount) {
  if (amount >= 250 && amount % 250 == 0) return 3;
  if (amount >= 100 && amount % 100 == 0) return 2;
  if (amount >= 50 && amount % 50 == 0) return 1;
  if (amount % 25 == 0) return 0;
  return 4;
}

void draw_roulette(Pen& P, float t, bool attract) {
  const bool spinning = g_rs.on && !g_rs.revealed;
  const double dt = g_now - g_rs.t0;
  const bool revealed = g_rs.any && g_rs.revealed;
  const double sa = g_now - g_showAt;
  long long held = g_pendingWin;  // r208: 64-bit
  long long shownRet = revealed ? g_rs.ret : 0;
  if (revealed && g_rs.ret > 0 && sa >= 0 && sa < 2.0) {
    shownRet = static_cast<long long>(static_cast<double>(g_rs.ret) * std::min(1.0, sa / 1.2));
    held += g_rs.ret - shownRet;
  }
  header(P, "ROULETTE", t, held);
  // the wheel
  const double w = wheel_angle(g_now);
  double ba = 0, br = 0;
  const bool ball = ball_at(g_now, &ba, &br);
  const int hi = revealed && g_rs.number >= 0 ? wheel_index(g_rs.number) : -1;
  draw_wheel(P, kWheelCx, kWheelCy, kWheelR, w, ball, ba, br, hi, t);
  if (spinning && dt < 2.0) neon_text(P, "NO MORE BETS", kWheelCx, 160, 44, 600, kAmber, 0, 0.4f);
  if (revealed && g_rs.number >= 0) {  // the number, big in the middle of the wheel
    const int n = g_rs.number;
    const float a = static_cast<float>(std::min(1.0, (g_now - g_rs.t0 - kReveal) / 0.3));
    disc(P, kWheelCx, kWheelCy, 92, pocket_rgb(n), 0.95f * a);
    P.K.ring(kWheelCx - 92, kWheelCy - 92, kWheelCx + 92, kWheelCy + 92, 92, 6, kGold, a);
    neon_text(P, std::to_string(n), kWheelCx, kWheelCy - 4, 96, 170, 0xFFFFFF, 0, 0.3f * a);
    P.K.text(n == 0 ? "ZERO" : (is_red(n) ? "RED" : "BLACK"), kWheelCx, kWheelCy + 58, 24, 160, 0xFFFFFF, 1.0f);
  }
  // the board: green felt, gold lines
  float bx1 = kBx0 + kZeroW + 12 * kNumW + kColW;
  float fy1 = kBy0 + 3 * kRowH + 6 + kDozH + 6 + kOutH;
  P.K.box(kBx0 - 18, kBy0 - 18, bx1 + 18, fy1 + 18, 26, kFelt, 1.0f, kFelt2);
  P.K.ring(kBx0 - 18, kBy0 - 18, bx1 + 18, fy1 + 18, 26, 4, 0xC9A040);
  const bool cursorOn = !spinning && !attract;
  int winNum = revealed ? g_rs.number : -1;
  for (int row = 0; row <= 4; ++row)
    for (int col = 0; col <= 13; ++col) {
      if (row >= 3 && (col == 0 || col == 13)) continue;
      if (row == 3 && (col - 1) % 4 != 0) continue;
      if (row == 4 && (col - 1) % 2 != 0) continue;
      if (row > 0 && col == 0) continue;
      float x0, y0, x1, y1;
      spot_rect(row, col, &x0, &y0, &x1, &y1);
      RBet b;
      spot_bet(row, col, &b);
      uint32_t fill = 0, ink = 0xFFFFFF;
      if (b.kind == kStraight) fill = pocket_rgb(b.n);
      else if (b.kind == kRed) fill = kRouletteRed;
      else if (b.kind == kBlack) fill = kRouletteBlack;
      const bool won = winNum >= 0 && bet_wins(b.kind, b.n, winNum);
      if (fill != 0) P.K.box(x0 + 4, y0 + 4, x1 - 4, y1 - 4, b.kind == kStraight && b.n == 0 ? 30 : 8, fill, b.kind == kStraight ? 1.0f : 0.95f);
      P.K.ring(x0, y0, x1, y1, b.kind == kStraight && b.n == 0 ? 30 : 4, 2.0f, 0xE8C860, 0.9f);
      if (won) {
        P.K.glow(x0 - 16, y0 - 16, x1 + 16, y1 + 16, kGold, 0.4f + 0.2f * std::sin(t * 9.0f));
        P.K.ring(x0 + 2, y0 + 2, x1 - 2, y1 - 2, 8, 5, kGold);
      }
      std::string label = b.kind == kColumn ? "2:1" : bet_name(b.kind, b.n);
      if (b.kind == kRed || b.kind == kBlack) label = b.kind == kRed ? "RED" : "BLACK";
      const float px = b.kind == kStraight ? 30.0f : 26.0f;
      P.K.text(label, 0.5f * (x0 + x1), 0.5f * (y0 + y1) + 1, px, x1 - x0 - 8, ink, 1.0f);
      g_hits.push_back({x0, y0, x1, y1, 4, row, col});
    }
  // the chips on the board: the new round's, or the last spin's (won ones shine, lost ones dim)
  const std::vector<RBet>& shown = g_bets.empty() && g_showSpinBets ? g_rs.bets : g_bets;
  const bool lastSpin = g_bets.empty() && g_showSpinBets;
  for (const RBet& b : shown) {
    int row = 0, col = 0;
    if (b.kind == kStraight) {
      if (b.n == 0) row = 1, col = 0;
      else col = (b.n - 1) / 3 + 1, row = 3 - ((b.n - 1) % 3 + 1);
    } else if (b.kind == kColumn) {
      row = 2 - b.n, col = 13;
    } else if (b.kind == kDozen) {
      row = 3, col = b.n * 4 + 1;
    } else {
      static const int kK[6] = {kLow, kEven, kRed, kBlack, kOdd, kHigh};
      int o = 0;
      for (int i = 0; i < 6; ++i)
        if (kK[i] == b.kind) o = i;
      row = 4, col = o * 2 + 1;
    }
    float x0, y0, x1, y1;
    spot_rect(row, col, &x0, &y0, &x1, &y1);
    const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
    if (lastSpin && revealed) {
      const bool won = bet_wins(b.kind, b.n, g_rs.number);
      if (!won) continue;  // (the croupier swept it)
      P.K.glow(cx - 50, cy - 50, cx + 50, cy + 50, kGold, 0.5f);
      chip_disc(P, cx, cy, 26, b.amount, denom_of(b.amount));
      neon_text(P, "+" + xrcoin::fit(bet_return(b, g_rs.number), 9), cx, cy - 40, 24, 160, kGold, 0, 0.4f);
    } else {
      chip_disc(P, cx, cy, 26, b.amount, denom_of(b.amount));
    }
  }
  if (cursorOn && g_row <= 4) {
    float x0, y0, x1, y1;
    spot_rect(g_row, g_col, &x0, &y0, &x1, &y1);
    cur_frame(P.v, 1, x0, y0, x1, y1, 10, t);  // (r204: the bold cursor - its frame only: a glow here would wash the number out)
  }
  // the chips, CLEAR, SPIN
  {
    const float cy = 596;
    const long long free = std::max(0LL, balance() - staged());
    for (int i = 0; i < 5; ++i) {
      const float cx = kBx0 + 26 + i * 86.0f;
      const bool on = g_c.chip == i;
      const bool cur = cursorOn && g_row == 5 && g_col == i;
      if (on) P.K.glow(cx - 56, cy - 56, cx + 56, cy + 56, kGold, 0.45f);
      chip_disc(P, cx, cy, on ? 36.0f : 31.0f, i < 4 ? kChips[i] : 0, i);
      if (i == 4) P.K.text("MAX", cx, cy + 1, 22, 60, 0x3A2A00, 0.0f);
      if (on) P.K.ring(cx - 42, cy - 42, cx + 42, cy + 42, 42, 4, kGold);
      if (cur) cur_frame(P.v, 1, cx - 44, cy - 44, cx + 44, cy + 44, 44, t);
      g_hits.push_back({cx - 40, cy - 40, cx + 40, cy + 40, 5, i, 0});
    }
    const float c0 = kBx0 + 456, c1 = c0 + 150;
    const bool curC = cursorOn && g_row == 5 && g_col == 5;
    if (curC) cur_frame(P.v, 0, c0, cy - 34, c1, cy + 34, 22, t);
    P.K.box(c0, cy - 34, c1, cy + 34, 22, lit(curC) ? 0x0D3F52 : 0x241A48);
    P.K.ring(c0, cy - 34, c1, cy + 34, 22, 2.0f, kEdge);
    P.K.text("CLEAR", 0.5f * (c0 + c1), cy + 1, 28, 130, kInk, 0.0f);
    if (curC) cur_frame(P.v, 1, c0, cy - 34, c1, cy + 34, 22, t);
    g_hits.push_back({c0, cy - 34, c1, cy + 34, 6, 0, 0});
    const float s0 = c1 + 18, s1 = bx1 + 18;
    const bool curS = cursorOn && g_row == 5 && g_col == 6;
    const bool can = !spinning && (!g_bets.empty() || !g_lastBets.empty());
    const float pulse = can ? 0.5f + 0.5f * std::sin(t * 5.0f) : 0.0f;
    if (can) P.K.glow(s0 - 20, cy - 54, s1 + 20, cy + 54, kPink, 0.25f + 0.25f * pulse);
    P.K.box(s0, cy - 40, s1, cy + 40, 30, spinning ? 0x3A2440 : 0xC0186A, 1.0f, spinning ? 0x241830 : 0x6A0A3A);
    P.K.ring(s0, cy - 40, s1, cy + 40, 30, 3.0f, 0xFFB0D8);
    if (curS) cur_frame(P.v, 1, s0, cy - 40, s1, cy + 40, 30, t);
    P.K.text(spinning ? "SPINNING" : (g_bets.empty() && !g_lastBets.empty() ? "REBET" : "SPIN"), 0.5f * (s0 + s1), cy - 8, 38, s1 - s0 - 20,
             0xFFFFFF, 2.0f);
    // (r203: no X glyph - X picks at the machine now; SPIN is the bottom row's, or the pointer's)
    g_hits.push_back({s0, cy - 40, s1, cy + 40, 7, 0, 0});
    (void)free;
  }
  // the board's sums, the last numbers, the odds
  {
    const float x0 = kBx0 - 18, x1 = bx1 + 18, y0 = 652, y1 = 776;
    P.K.box(x0, y0, x1, y1, 22, 0x120C26, 1.0f, 0x07040F);
    P.K.ring(x0, y0, x1, y1, 22, 2.5f, 0x4A2A80);
    const long long st = staged();
    P.K.left("ON THE BOARD", x0 + 22, y0 + 32, 24, 200, kDim, 0.0f);
    neon_text(P, xrcoin::fit(lastSpin ? g_rs.staked : st, 9), x0 + 22, y0 + 80, 46, 220, kCyan, -1, 0.3f);
    P.K.left("LAST NUMBERS", x0 + 270, y0 + 32, 24, 260, kDim, 0.0f);
    for (size_t i = 0; i < g_c.history.size() && i < 10; ++i) {
      const int n = g_c.history[i];
      if (i == 0 && spinning) continue;
      const float cx = x0 + 292 + static_cast<float>(i - (spinning ? 1 : 0)) * 58.0f, cy = y0 + 80;
      disc(P, cx, cy, 24, pocket_rgb(n));
      P.K.ring(cx - 24, cy - 24, cx + 24, cy + 24, 24, i == 0 ? 3.0f : 1.5f, i == 0 ? kGold : 0x6A6480);
      P.K.text(std::to_string(n), cx, cy + 1, 24, 44, 0xFFFFFF, 0.0f);
    }
  }
  session_box(P, kBx0 - 18, 790, bx1 + 18, 890);
  // the result's show
  if (revealed && g_rs.ret > 0 && sa >= 0 && sa < 4.0) {
    const float a = static_cast<float>(sa < 0.25 ? sa / 0.25 : std::max(0.0, 1.0 - std::max(0.0, sa - 2.8) / 1.2));
    if (g_rs.tier >= 2) burst(P, kWheelCx, kWheelCy, 420, 420, kGold, 0.3f * a, 18, static_cast<float>(sa) * 40.0f);
    const bool strobe = std::fmod(sa * 8.0, 1.0) < 0.5;
    neon_text(P, g_rs.tier >= 3 ? "BIG WINNER!" : "WINNER!", kWheelCx, 222, 70, 640, strobe ? kGold : kPink, 0, 0.6f * a);
    neon_text(P, "+" + xrcoin::fit(shownRet, 15), kWheelCx, 830, 64, 640, kGold, 0, 0.5f * a);
  }
  if (attract && !spinning) {
    P.K.box(kWheelCx - 300, kWheelCy + 200, kWheelCx + 300, kWheelCy + 320, 30, 0x0A0716, 0.85f);
    P.K.ring(kWheelCx - 300, kWheelCy + 200, kWheelCx + 300, kWheelCy + 320, 30, 3, kPink, 0.6f + 0.4f * std::sin(t * 4.0f));
    neon_text(P, "PLACE YOUR BETS", kWheelCx, kWheelCy + 244, 50, 560, kPink, 0, 0.5f);
    P.K.text("GRAB THE STICK TO PLAY  -  OR POINT + TRIGGER", kWheelCx, kWheelCy + 292, 26, 560, kCyan, 0.0f);
  }
  footer(P, "STRIKERZ ROULETTE  -  EUROPEAN SINGLE ZERO  -  HOUSE EDGE 2.7% ON EVERY BET  -  STRAIGHT 35:1  -  DOZENS + COLUMNS 2:1  -  "
            "RED / BLACK  ODD / EVEN  1-18 / 19-36  1:1  -  ZERO TAKES THE OUTSIDE BETS  -  A / X: CHIP  B / Y: TAKE BACK  -  THE BOTTOM ROW: CHIP SIZE, CLEAR, SPIN",
         t);
}

// ---------------------------------------------------------------------------------------------------- the PICKER
void draw_picker(Pen& P, float t) {
  header(P, "CASINO", t, 0);
  neon_text(P, "PICK A GAME", 0.5f * kW, 180, 64, 1200, kCyan, 0, 0.4f);
  static const char* const kTitle[3] = {"SPORTSBOOK", "SLOTS", "ROULETTE"};
  static const char* const kSub[3] = {"BET THE LEAGUE + FUTURES", "PULL FOR THE MEGA JACKPOT", "EUROPEAN SINGLE ZERO"};
  for (int i = 0; i < 3; ++i) {
    const float x0 = 64 + i * 518.0f, x1 = x0 + 486, y0 = 240, y1 = 800, cx = 0.5f * (x0 + x1);
    const bool on = g_pickerAt == i;
    if (on) cur_frame(P.v, 0, x0, y0, x1, y1, 34, t, kPink);
    P.K.box(x0, y0, x1, y1, 34, lit(on) ? 0x2A1650 : 0x140E2A, 1.0f, 0x07040F);
    P.K.ring(x0, y0, x1, y1, 34, 2.5f, kEdge);
    const float ay = 470;
    if (i == 0) {  // a little odds board
      P.K.box(x0 + 50, ay - 150, x1 - 50, ay + 150, 20, 0x05030A);
      static const char* const kRows[3][2] = {{"STARK TOWER", "+150"}, {"LAVA PIT", "-180"}, {"WIN BY 2+", "+210"}};  // (r225: or more; r229: the new clubs)
      for (int r = 0; r < 3; ++r) {
        const float ry = ay - 90 + r * 90.0f;
        P.K.left(kRows[r][0], x0 + 80, ry, 34, 220, kInk, 0.0f);
        neon_text(P, kRows[r][1], x1 - 80, ry, 44, 160, kRows[r][1][0] == '+' ? kLime : kCyan, 1, 0.3f);
      }
    } else if (i == 1) {  // three reels: TIZM TIZM TIZM
      for (int c = 0; c < 3; ++c) {
        const float rx = cx + (c - 1) * 128.0f;
        P.K.box(rx - 58, ay - 120, rx + 58, ay + 120, 14, 0xF3EEF9);
        P.K.box(rx - 58, ay - 120, rx + 58, ay + 120, 14, 0x1A1430);
        draw_sym(P, c == 1 ? kScatter : kWild, rx, ay, 108, 108, 1.0f, t);
      }
      seg(P, cx - 210, ay, cx + 210, ay, 6, kPink, 0.8f);
    } else {
      draw_wheel(P, cx, ay, 150, t * 0.4, true, -t * 1.3, 0.86, -1, t, true);
    }
    neon_text(P, kTitle[i], cx, 690, 58, 440, lit(on) ? kPink : 0xB070FF, 0, lit(on) ? 0.5f : 0.25f);
    P.K.text(kSub[i], cx, 748, 26, 440, kDim, 0.0f);
    if (on) cur_frame(P.v, 1, x0, y0, x1, y1, 34, t, kPink);
    g_hits.push_back({x0, y0, x1, y1, 8, i, 0});
  }
  {
    const long long net = g_sesOut - g_sesIn - g_pendingWin;
    P.K.text("THIS SESSION AT THE TABLES: " + std::string(net > 0 ? "+" : "") + xrcoin::fit(net, 15) + " COINS", 0.5f * kW, 856, 30, 1200,
             net > 0 ? kLime : (net < 0 ? kLoseRed : kInk), 0.0f);
  }
  footer(P, "STICK: PICK  -  A / X: PLAY  -  B / Y: BACK  -  BIT COINS ONLY, NEVER REAL MONEY  -  SLOTS RTP 94.2%  -  ROULETTE HOUSE EDGE 2.7%", t);
}
}  // namespace

// r203 (Lance's r202 test: "it did not let me move it, select anything"): while a hand holds the joystick the screen's whole
// foot is the CONTROL BAR - the lime MACHINE CONTROL pill + what every button does here (or the last word, for a moment)
void control_bar(std::vector<xrtext::BoardPiece>& out, float t, const std::string& hint, const std::string& msg, uint32_t msgRgb) {
  Pen P;
  const float y0 = 904, y1 = 958, cy = 0.5f * (y0 + y1);
  P.K.box(0, y0, kW, y1, 26, 0x04140C);
  P.K.ring(0, y0, kW, y1, 26, 3.0f, msg.empty() ? kLime : msgRgb);
  const float x0 = 8, x1 = 330;
  P.K.glow(x0 - 20, y0 - 16, x1 + 20, y1 + 16, kLime, 0.3f + 0.12f * std::sin(t * 5.0f));
  P.K.box(x0, y0 + 5, x1, y1 - 5, 24, 0x06301A);
  P.K.ring(x0, y0 + 5, x1, y1 - 5, 24, 3.0f, kLime);
  disc(P, x0 + 28, cy, 9, kLime, 0.6f + 0.4f * std::sin(t * 7.0f));
  const bool latched = g_ctlMode.load() == 2;  // r205: hands free - the thumbsticks + buttons have it until you let it go
  P.K.left(latched ? "CONTROL ON" : "MACHINE CONTROL", x0 + 48, cy + 1, 28, x1 - x0 - 58, 0xEFFFF4, 1.5f);
  if (!msg.empty()) P.K.text(msg, 0.5f * (x1 + kW), cy + 1, 32, kW - x1 - 40, msgRgb, 1.5f);
  else P.K.text(latched ? hint + "   GRIP STICK: OFF" : hint, 0.5f * (x1 + kW), cy + 1, 30, kW - x1 - 40, 0xFFFFFF, 1.5f);
  for (xrtext::BoardPiece& p : P.v) out.push_back(std::move(p));
}
void control_pill(std::vector<xrtext::BoardPiece>& out, float t) { control_bar(out, t, "STICK: MOVE   A / X: PICK   B / Y: BACK   MENU: GAMES", "", 0); }

void draw(std::vector<xrtext::BoardPiece>& out, float ox, float oy, float k, float t, bool attract) {
  std::lock_guard<std::mutex> lk(g_cm);
  g_hits.clear();
  Pen P;
  if (g_screen == kSlots) draw_slots(P, t, attract);
  else if (g_screen == kRoulette) draw_roulette(P, t, attract);
  else draw_picker(P, t);
  if (!attract && g_engaged.load()) control_bar(P.v, t, hint_now(), g_now - g_msgAt < 3.0 ? g_msg : std::string(), g_msgRgb);
  for (xrtext::BoardPiece& p : P.v) {
    p.x0 = ox + p.x0 * k;
    p.y0 = oy + p.y0 * k;
    const bool text = p.kind == xrtext::BoardPiece::kText || p.kind == xrtext::BoardPiece::kTextLeft || p.kind == xrtext::BoardPiece::kTextRight;
    if (text) {
      p.x1 *= k;
      p.y1 *= k;
    } else {
      p.x1 = ox + p.x1 * k;
      p.y1 = oy + p.y1 * k;
    }
    p.radius *= k;
    p.outline *= k;
    p.shadow *= k;
    for (size_t i = 0; i < p.pts.size(); ++i) p.pts[i] = (i % 2 == 0 ? ox : oy) + p.pts[i] * k;
    out.push_back(std::move(p));
  }
  if (xrbook::CursorBox* s = xrbook::cursor_sink()) {  // r205: the overlay's box, where the page went
    if (s->on) {
      s->x0 = ox + s->x0 * k, s->y0 = oy + s->y0 * k;
      s->x1 = ox + s->x1 * k, s->y1 = oy + s->y1 * k;
      s->r *= k;
      s->k = k;
    }
  }
}

void debug_set(const Debug& d) {
  std::lock_guard<std::mutex> lk(g_cm);
  g_screen = d.screen;
  g_c.slotBet = d.slotBet;
  g_c.chip = d.chip;
  g_pickerAt = d.pickerAt;
  g_row = d.row;
  g_col = d.col;
  g_now = d.now;
  g_debugBalance = d.balance;
  g_sesIn = d.sessionIn;
  g_sesOut = d.sessionOut;
  g_sesSpins = d.sessionIn > 0 ? std::max<long long>(1, d.sessionIn / 25) : 0;
  g_showLines = d.showLines;
  g_msgAt = -1e9;
  // slots
  g_ss = SlotSpin();
  g_haveResult = false;
  g_pendingWin = 0;
  g_showTier = 0;
  g_showAt = -1e9;
  if (d.haveResult) {
    const int bet = kSlotBets[d.slotBet];
    g_ss.r = d.result;
    g_ss.bet = bet;
    g_ss.tier = win_tier(d.result.win, bet, d.result.mega);
    if (d.spinAt >= 0.0) {  // spinning toward it
      for (int c = 0; c < kReels; ++c) g_ss.prev[c] = (d.result.stops[c] + 11 + 5 * c) % kStops;
      g_ss.on = true;
      g_ss.revealed = false;
      g_ss.t0 = d.spinAt;
      double extra = 0;
      for (int c = 0; c < kReels; ++c) {
        if (c >= 2 && slot_tease(d.result.grid, c)) {
          extra += kTease;
          if (g_ss.teaseFrom == kReels) g_ss.teaseFrom = c;
        }
        g_ss.stopT[c] = 0.95 + 0.30 * c + extra;
        const double run = g_ss.stopT[c] - 0.5 * kAccel - 0.5 * kDecel;
        const int base = ((g_ss.prev[c] - d.result.stops[c]) % kStops + kStops) % kStops;
        const int laps = std::max(1, static_cast<int>(std::lround((17.0 * run - base) / kStops)));
        g_ss.D[c] = base + static_cast<double>(laps) * kStops;
        g_ss.v[c] = g_ss.D[c] / run;
      }
      g_ss.revealAt = g_ss.t0 + g_ss.stopT[kReels - 1] + kBounce;
      g_pendingWin = d.result.win;
      g_sesOut += g_pendingWin;  // (as the live path: paid at the pull, hidden till it lands)
    } else {  // landed: the show (spinAt < 0: the result shown at -spinAt - 1 seconds ago... kept simple: now - 1.2 s)
      for (int c = 0; c < kReels; ++c) g_rest[c] = d.result.stops[c];
      g_haveResult = true;
      g_showTier = g_ss.tier;
      g_showAt = d.now + d.spinAt + 1.0;  // (spinAt -1 = just landed; -2.5 = 1.5 s into the show)
    }
  }
  // roulette
  g_bets = d.bets;
  g_undo.clear();
  g_undoAmt.clear();
  g_c.history = d.history;
  g_rs = RSpin();
  g_showSpinBets = false;
  if (d.number >= 0) {
    g_rs.any = true;
    g_rs.number = d.number;
    g_rs.bets = d.bets;
    g_rs.staked = 0;
    g_rs.ret = 0;
    for (const RBet& b : d.bets) g_rs.staked += b.amount, g_rs.ret += bet_return(b, d.number);
    g_rs.tier = g_rs.ret <= 0 ? 0 : (g_rs.ret - g_rs.staked <= 0 ? 1 : win_tier(g_rs.ret, std::max(1LL, g_rs.staked), false));
    g_bets.clear();
    g_showSpinBets = true;
    g_wA0 = 0.7;
    g_wB = 7.0;
    g_rs.t0 = d.spinAt;
    g_wT0 = d.spinAt;
    if (d.now - d.spinAt < kReveal) {
      g_rs.on = true;
      g_rs.revealed = false;
      g_pendingWin = g_rs.ret;
      g_sesOut += g_pendingWin;
    } else {
      g_rs.on = true;
      g_rs.revealed = true;
      if (g_rs.ret > 0) g_showTier = std::max(1, g_rs.tier), g_showAt = d.spinAt + kReveal;
    }
  }
}

}  // namespace xrcasino
