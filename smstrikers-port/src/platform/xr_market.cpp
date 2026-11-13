// strikers-quest r201: the TIZM CARD MARKET (xr_market.h; COIN-SINKS.md 1).
#include "xr_market.h"
#include "xr_exq.h"  // r230: LANCINI EXQUISITE (its tiers, runs, prices)

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <atomic>
#include <unordered_map>

#include "xr_menu.h"

namespace xrmarket {

namespace {
// ============================================================================================== the numbers
// r210 (Lance 11-10: "think of these as the real pokemon market irl! its crazy!!! ... the characters some people will literally sell
// houses for"): a copy's price = kUnit x rarity^(1 - 0.7 s) x rating x DEMAND^(0.4 + 0.6 s) x finish x shiny x hit x grade (by the pop
// above it) x form x scarcity x the market's cycle^(0.4 + 0.8 s) x its own hype wave x a day's noise. s = how scarce the copy is (BASE 0,
// SILVER 0.25, GOLD /10 0.7, BLACK 1/1 1; + 0.15 shiny, + 0.3 a case hit; at most 1): the scarcer, the more the CHARACTER decides
// (a bulk common is bulk whoever is on it; a 1/1 of a grail is a house) and the less its pack rarity does. MARKET-PRICES.md.
// r231 (Lance 11-12: "the shit is tooo random ... stats/play should determine pricing. theres no pop culture vibe to the market like
// real life so literally only stats"): r210's DEMAND tiers (GRAIL / ICON / STAR / MID / OBSCURE, a fixed real-world popularity table) and
// the card's pack rarity / rating as price factors are GONE. A copy's price = kUnit x its family stage x DEMAND^(0.55 + 0.45 s) x finish x
// shiny x hit x grade x scarcity x the market's mood^(0.4 + 0.8 s) x a day's noise (+-3%) (x the JAM '96 / EXQUISITE premiums). DEMAND is
// its player's alone (MARKET-PRICES.md 2): a LEGACY that holds (awards, titles, career goals), this SEASON's production against the league's
// (his goals a game, his share of his club's goals - a passenger on a winning club gets little - the Golden Boot / MVP / Rookie races), his
// CURRENT FORM (fast, mean-reverting: breakouts, slumps), soft-capped (a dynasty plateaus) - 0.5 a benchwarmer .. 3 an average player .. ~60.
constexpr double kUnit = 10.0;                                    // an average player's BASE card (demand 3: x 3^0.55)
constexpr double kDemandMid = 3.0, kDemandK = 1.1;                // demand = kDemandMid x e^(kDemandK x level)
constexpr double kCapHi = 2.75, kCapLo = 1.6;                     // the level's soft cap: up to ~2.75 (demand ~61), down to ~-1.6 (~0.5)
constexpr double kExqSet = 1.6;                                    // r231: a LANCINI EXQUISITE copy's set premium (exq_mult)
constexpr double kDemandOut = -0.9;                               // a character not in this career's league at all (bulk: demand ~1.1)
constexpr double kParallelMult[4] = {1.0, 5.0, 600.0, 12000.0};  // BASE, SILVER TIZM, GOLD TIZM /10, BLACK TIZM 1/1
constexpr double kParS[4] = {0.0, 0.25, 0.7, 1.0};
constexpr double kShinyMult = 3.0;
constexpr double kHitMult[4] = {1.0, 60.0, 25.0, 40.0};          // -, TIZM BOOM, HOMETOWN, SIGNED (on a numbered copy ^(1 - parS / 2))
// a slab: raw 1, grades 1 - 10 (the hobby's gaps: a 9 ~2x an 8, a 10 several times a 9 - with the pop above it, below); a numbered
// copy's grade matters less (GOLD ^0.6, BLACK ^0.45: it's the only one either way)
constexpr double kGradeMult[11] = {1.0, 0.35, 0.4, 0.45, 0.5, 0.6, 0.7, 0.85, 1.1, 2.0, 7.0};
const char* const kGradeName[11] = {"", "POOR", "GOOD", "VG", "VG-EX", "EX", "EX-MT", "NM", "NM-MT", "MINT", "GEM MINT"};
const char* const kHitName[4] = {"", "TIZM BOOM", "HOMETOWN", "SIGNED"};
const char* const kParName[4] = {"BASE", "SILVER TIZM", "GOLD TIZM", "BLACK TIZM"};
constexpr double kHeatPull = 0.45;  // a matchday moves the SEASON level this far toward its target
constexpr double kFormKeep = 0.5;   // r231: the CURRENT FORM level keeps this much a matchday (mean-reverting: a hot week fades unless repeated)
constexpr double kNoise = 0.03;   // a day's noise: +-3% (r210: 5%), never carried over (no random walk)
// r210: the market's mood (a BOOM / a CRASH: one market-wide level, kept a matchday at a time, fading 28% a matchday) - r231: from the league's events
constexpr double kCycleKeep = 0.72;  // (r231: no random booms / crashes - the league's events move it: step())
// the world's packs (the league's collectors ripping): their copies of a card grow with the matchdays
constexpr double kWorldPacks0 = 3000.0, kWorldPacksDay = 150.0;
// a card's share of a pack slot by rarity (slots 1-4 74 / 22 / 4, slot 5 82 / 18), per pack card
constexpr double kRarityShare[3] = {4 * 0.74 / 5.0, (4 * 0.22 + 0.82) / 5.0, (4 * 0.04 + 0.18) / 5.0};
// r211 (Lance's new pull rates = xr_cards.cpp kSilverOdds / kGoldOdds / kBlackOdds, kHitIn, kShinyOdds; were 1/8, 1/50, 1/300,
// hits 1/300, 1/350, 1/450, shiny 1/128): the pop report's copies out there. Prices themselves don't read these (only a slab's pop)
constexpr double kParShare[4] = {1.0 - 1.0 / 50 - 1.0 / 250 - 1.0 / 2500, 1.0 / 50, 1.0 / 250, 1.0 / 2500};
constexpr double kHitShare[4] = {1.0, 1.0 / 1500, 1.0 / 1200, 1.0 / 2000};
constexpr double kShinyShare = 1.0 / 512;
constexpr int kGradeOdds[11] = {0, 4, 6, 10, 20, 40, 80, 200, 340, 240, 60};
const char* const kWho[24] = {"@MINTCONDITION", "@CHROMEKING",   "@TIZMVAULT",     "@SLABSNSTUFF", "@PULLMASTER",   "@BINDERBOSS",
                              "@HOBBYBOX",      "@REFRACTORRICK", "@GEMMINTGRACE",  "@PACKRIPPER",  "@THECARDSHARK", "@ONEOFONE",
                              "@WAXWIZARD",     "@SETBUILDER",    "@GRADEDGURU",    "@FOILFIEND",   "@CASEHITCARL",  "@ROOKIECARDRAY",
                              "@SHINYHUNTER",   "@PCPENNY",       "@LEGENDLOCKER",  "@TOPLOADER",   "@VINTAGEVINNY", "@PRIZMPRINCE"};

uint64_t mix64(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
uint64_t hstr(const std::string& s) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
  return h;
}
double u01(uint64_t h) { return static_cast<double>(mix64(h) >> 11) * (1.0 / 9007199254740992.0); }
double clampd(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
long long llr(double d) { return static_cast<long long>(d < 0 ? std::ceil(d - 0.5) : std::floor(d + 0.5)); }  // (r210: round to a whole coin)
std::string coins_text(long long n) { return xrcoin::text(n); }  // "1,234,567" (r208: xr_coins.h's)

std::string upper(std::string s) {
  for (char& c : s)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return s;
}
std::string pct_text(float p) {
  char b[24];
  std::snprintf(b, sizeof(b), "%s%.0f%%", p >= 0 ? "+" : "-", std::fabs(p));
  return b;
}

// ============================================================================================== the state
struct Line {
  std::string id;
  int activeCard = -1;   // the form it plays as (its card), -1 not in the league now
  int level = 0, regGoals = 0, seasonGoals = 0, careerGoals = 0, streak = 0, form = 0, club = -1, role = -1;
  // r231: heat = the SEASON level (this season's production vs the league's), legacy = the LEGACY level (events: awards, titles, a strong
  // season, retiring - it holds), shock = a one-matchday jolt fading; + `fast` the CURRENT FORM level (mean-reverting)
  double heat = 0.0, shock = 0.0, legacy = 0.0;
  bool retired = false;
  int retiredSeason = -1;
  bool seen = false;     // ever in the league
  std::string why;       // "4 GOALS THIS WEEK - W3 - BOOT RACE #2"
  std::vector<float> hist;  // r231: its DEMAND per matchday (oldest first, up to kHist; r210-r230: a perf factor - re-priced at load)
  // r231 (saved on its own "D" line)
  double fast = 0.0;          // the CURRENT FORM level
  double prior = 0.0;         // last season's level carried into the new one's first games
  double contrib = 0.0;       // its share of its club's goals (x2.5, 0..1): how much of the club's record is his
  int boots = 0, mvps = 0, roys = 0, titles = 0;
  int rank = 0, prevRank = 0; // the TOP CARDS race now / at the last matchday
  int lastSeason = -1;        // the season it last played in (a new one: the season's level carried into legacy + prior)
  int wkGoals = 0;            // goals in the last matchday
  std::string reason;         // the play behind its price ("14 GOALS - GOLDEN BOOT LEADER - UNBEATEN CLUB")
  // r232 (Lance: "you defff need to add assists into the equation"): its assists (regular season / season / career, the last
  // matchday's), PLAYMAKER OF THE YEAR awards; saved on its own "A" line (an r231 build ignores it). astSeen: its assists read
  // once (an r231 file's line: the franchise's assists so far aren't a matchday's)
  int regAst = 0, seasonAst = 0, careerAst = 0, wkAst = 0, pms = 0;
  bool astSeen = false;
};
struct Offer {
  uint32_t id = 0;
  Variant v;
  std::string who;
  long long amount = 0, reserve = 0;  // r210: 64-bit
  int bidders = 1, day = 0, expires = 0;
  bool final = false;
  uint32_t listing = 0;  // r238: a watcher's offer on one of YOUR LISTINGS (its id) - taking it takes the listing down and sells (0: an offer on a free copy)
};
// r210 (Lance 11-10: "make it so we can set sale prices ... a 'list for sale' with a price you choose! who knows who might buy at certain
// times!"): one of your copies up for sale at your price, until a collector pays it or you take it down
struct Mine {
  uint32_t id = 0;
  Variant v;
  long long ask = 0;
  int listed = 0;           // the step it went up
  int views = 0, watching = 0;
};
struct Sale {  // one of yours that sold (newest first, the last 12)
  Variant v;
  std::string buyer;
  long long amount = 0, market = 0;
  int day = 0;
  bool surprise = false;
};
struct Listing {
  Variant v;
  std::string seller;
  bool sold = false;
};
struct Headline {
  int day = 0;
  std::string text;
  uint32_t rgb = 0xFFD23F;
};
struct Market {
  unsigned career = 0;
  bool loaded = false;
  int steps = 0;                 // matchdays priced
  int season = -1, week = -1;
  std::vector<int> days;         // the day index of each history entry (oldest first; noise keys)
  std::unordered_map<std::string, Line> lines;
  std::vector<Offer> offers;
  std::vector<Listing> listings;
  std::map<std::string, int> firstSeen;  // a copy's sig -> the step it was first seen in your binder
  std::vector<long long> portfolio;      // its value per matchday
  std::vector<Headline> news;            // newest first
  long long spent = 0, received = 0;
  int bought = 0, sold = 0;
  uint32_t nextId = 101;
  // r210
  std::vector<double> cycleHist;  // the market's mood at each history entry (as days; 0 calm, + a boom, - a crash)
  double cycle = 0.0;
  std::vector<Mine> mine;         // your listings
  std::vector<Sale> sales;        // your listings that sold (newest first)
  int unseenSales = 0;            // sold since you last looked at the MARKET
  long long record = 0;           // the biggest sale on record (the collector world's)
  std::string recordText;
  bool scale210 = false;          // the file was r210's (prices at the r210 scale); an older file's offers are dropped, its chart rescaled
  // r231: the stats-driven market - a file from before (r201-r230: popularity tiers) re-prices at its first read of the franchise
  bool v231 = false, reprice231 = false;
  int phase = -1, awardsSeason = -1, champSeason = -1, recordGoals = 0;  // (the mood's league events, once each; recordGoals = past seasons' best)
  int seasonBest = 0, recordSeason = -1;  // (this season's most goals so far; the season its record boom fired)
  double starsWas = 0.0;          // the TOP 20's mean demand at the last matchday (the stars cooling: the market cools)
  // r238: real-time ticks - this matchday's so far (each its own looks at your listings: the hashes' key), all of this career's (saved "K")
  int tickN = 0, ticks = 0;
};

std::mutex g_m;
Source g_src;
Market g_mk;
std::vector<CardInfo> g_set;
std::unordered_map<std::string, int> g_byKey, g_byStat;
std::unordered_map<std::string, std::vector<int>> g_byLine;
int g_rarityCount[3] = {1, 1, 1};
std::vector<Holding> g_hold;  // your copies (read with the franchise)
int g_gen = 1;
double g_now = 0.0, g_lastRead = -1e9;
PortFranchiseMarketState g_fs{};
std::string g_msg;
uint32_t g_msgRgb = 0xFFD23F;
double g_msgAt = -1e9, g_flashAt = -1e9;

// the UI (r210: + MY LISTINGS, the 2nd tab)
constexpr int kTabMine = 0, kTabListed = 1, kTabMovers = 2, kTabBuy = 3, kTabOffers = 4, kTabSales = 5, kTabs = 6;  // (r238: + SALES)
int g_tab = 3;  // (r236: the page opens on BUY - kTabBuy, below)
int g_cur[kTabs] = {0, 0, 0, 0, 0, 0}, g_top[kTabs] = {0, 0, 0, 0, 0, 0};
int g_ask = 0;          // 0 none, 1 sell, 2 buy, 3 accept, 4 (r210) take a listing down
// r210: the number pad (LIST FOR SALE / EDIT PRICE: r207's CUSTOM stake pad's keys) - its 15 keys: 0-8 = 1-9, 9 CLR, 10 0, 11 DEL,
// 12-14 OK (one wide key under them)
std::atomic<bool> g_pad{false};  // (atomic: the render thread asks pad_open() every frame without the lock)
int g_padKey = 14;         // the key under the cursor
uint32_t g_padEdit = 0;    // 0 a new listing (g_padV), else the listing being re-priced
Variant g_padV;
std::string g_padDigits;   // what's typed ("" nothing)
bool g_padFresh = true;    // the first digit starts a new number (it opens showing a suggestion)
std::string g_padSay;      // its own last word (a refusal)
double g_padSayAt = -1e9, g_padHitAt = -1e9;
int g_padHit = -1;         // the key just pressed (its flash)
int g_padHover = -1;       // the key under a pointer (-1 none)
bool g_pointDirty = false; // a pointer changed the pad (redraw_due draws it)
std::atomic<bool> g_stashClick{false};  // a pointer's click that came while the page was being drawn (the lock busy): pressed next time
std::atomic<float> g_stashX{0.0f}, g_stashY{0.0f};
// r231: r210's DEMAND table (xr_market_hype.inc + STRIKERS_HOME/cards/market-hype.txt) is gone: hype_tier() is the TOP CARDS race now
Variant g_askV;
int g_askIdx = -1;
uint32_t g_askId = 0;
long long g_askCoins = 0;  // r208: 64-bit
bool g_countering = false;
int g_counterPick = 1;  // 0 +5% 1 +10% 2 +15%
constexpr int kCounterPct[3] = {5, 10, 15};

std::string home_dir() {
  const char* h = std::getenv("STRIKERS_HOME");
  return std::string(h != nullptr && *h != '\0' ? h : "/storage/emulated/0/Strikers");
}
std::string market_path(unsigned career) { return home_dir() + "/cards/market-" + std::to_string(career) + ".txt"; }
void say(const std::string& m, uint32_t rgb) {
  g_msg = m;
  g_msgRgb = rgb;
  g_msgAt = g_now;
}
void sound(const char* n, float gain) {
  if (g_src.sound != nullptr) g_src.sound(n, gain);
}
long long balance() { return g_src.balance != nullptr ? g_src.balance() : 0; }  // r208: 64-bit

void load_set() {  // (g_m held)
  if (!g_set.empty() || g_src.catalogue == nullptr) return;
  g_src.catalogue(&g_set);
  g_byKey.clear();
  g_byStat.clear();
  g_byLine.clear();
  g_rarityCount[0] = g_rarityCount[1] = g_rarityCount[2] = 0;
  for (size_t i = 0; i < g_set.size(); ++i) {
    g_byKey[g_set[i].key] = static_cast<int>(i);
    if (!g_set[i].stat.empty() && !g_byStat.count(g_set[i].stat)) g_byStat[g_set[i].stat] = static_cast<int>(i);
    if (!g_set[i].line.empty()) g_byLine[g_set[i].line].push_back(static_cast<int>(i));
    if (g_set[i].here && g_set[i].set == 0) ++g_rarityCount[std::max(0, std::min(2, g_set[i].rarity))];  // (r217: a set's - the Jam set the same)
  }
  for (int& n : g_rarityCount) n = std::max(1, n);
}
// a franchise player's key -> its card (as the card bonuses map it), -1 none
int card_of_key(const std::string& key) {
  auto it = g_byKey.find(key);
  if (it != g_byKey.end()) return it->second;
  if (key.compare(0, 4, "cap_") == 0) {
    it = g_byStat.find(key.substr(4));
    if (it != g_byStat.end()) return it->second;
    it = g_byKey.find("strikers_" + key.substr(4));
    if (it != g_byKey.end()) return it->second;
  }
  it = g_byStat.find(key);
  if (it != g_byStat.end()) return it->second;
  it = g_byKey.find("strikers_" + key);
  return it != g_byKey.end() ? it->second : -1;
}
const Line* line_of(int card) {
  if (card < 0 || card >= static_cast<int>(g_set.size())) return nullptr;
  auto it = g_mk.lines.find(g_set[card].line);
  return it != g_mk.lines.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------------------- the price
// r231: the level -> demand curve, soft-capped both ways (a dynasty's stars plateau: each more goal is worth less the higher he is)
double soft_cap(double raw) { return raw >= 0.0 ? kCapHi * std::tanh(raw / kCapHi) : -kCapLo * std::tanh(-raw / kCapLo); }
double demand_of_level(double level) { return kDemandMid * std::exp(kDemandK * level); }
// the TOP CARDS race band of a card's family (its line's rank): 5 the top 5 .. 1 not ranked (r210: a fixed popularity tier)
int tier_of(int card) {
  const Line* l = line_of(card);
  if (l == nullptr || l->rank <= 0) return 1;
  return l->rank <= 5 ? 5 : (l->rank <= 20 ? 4 : (l->rank <= 60 ? 3 : (l->rank <= 150 ? 2 : 1)));
}
// a card's main (LANCINI TIZM) card's info: its own for a main card, its twin's for a JAM '96 / EXQUISITE one (its family place)
const CardInfo& main_info(int card) {
  const CardInfo& c = g_set[static_cast<size_t>(card)];
  return c.set >= 1 && c.base >= 0 && c.base < static_cast<int>(g_set.size()) ? g_set[static_cast<size_t>(c.base)] : c;
}
double career_legacy(double prod);       // (below)
double prod_of(int goals, int assists);  // (below)
// r239: the form its family plays as now (its stage; -1 not in the league / no family)
int cur_stage(const Line* l) {
  if (l == nullptr || l->activeCard < 0 || l->activeCard >= static_cast<int>(g_set.size())) return -1;
  return main_info(l->activeCard).stage;
}
// r239 (Lance 11-13: a rookie Michael Jordan worth as much as - or more than - his prime): the ROOKIE premium on a card's play part. Its
// family's FIRST form is his rookie card: x (1 + proven x (kRookieBase + kRookieScarce x s)); proven = tanh(max(0, legacy - kRookieFloor) /
// kRookieLegacy) - his LEGACY level (awards, titles, a strong season carried in, career goals + assists: what he has proven); the form he
// plays as now x1, a form between the two in proportion, a form not reached yet x1. A family with no legacy: every form the same (r231-r238:
// the last form x1, a stage below x0.94 - gone). A card alone (no family) x1
double rookie_m(int card, double s) {
  const CardInfo& b = main_info(card);
  if (b.stages <= 1) return 1.0;
  const Line* l = line_of(card);
  if (l == nullptr) return 1.0;
  const double lv = l->legacy + career_legacy(prod_of(l->careerGoals, l->careerAst));
  const double proven = std::tanh(std::max(0.0, lv - kRookieFloor) / kRookieLegacy);
  if (proven <= 0.0) return 1.0;
  int cur = cur_stage(l);
  if (cur < 0) cur = b.stages - 1;  // (not playing now: his last form his "prime")
  double w = 0.0;
  if (b.stage == 0) w = 1.0;
  else if (b.stage < cur) w = 1.0 - static_cast<double>(b.stage) / static_cast<double>(cur);
  return 1.0 + w * proven * (kRookieBase + kRookieScarce * clampd(s, 0.0, 1.0));
}
// how scarce a copy is (0 bulk .. 1 the only one): how much its character's demand decides its price
double scarce_s(const Variant& v) {
  double s = kParS[std::max(0, std::min(3, v.parallel))];
  if (v.shiny) s += 0.15;
  if (v.hit != kHitNone) s += 0.3;
  return std::min(1.0, s);
}
double demand_at(int card, int i);
// r231: kUnit x its family stage x its DEMAND^(0.55 + 0.45 s) - the scarcer the copy, the more its player's play decides (a bench player's
// 1/1 is a curiosity; the Golden Boot leader's is a house). No pack rarity, no rating, no popularity: two players with the same play and
// the same stage price the same. (r210: kUnit x rarity^(1-0.7s) x rating x DEMAND tier^(0.4+0.6s) x a form factor)
double base_of(int card, double s, int i) { return kUnit * rookie_m(card, s) * std::pow(demand_at(card, i), 0.55 + 0.45 * s); }  // (r239: rookie_m - r231-r238 stage_mult)
// a slab's grade with the pop report: the share of slabs at this grade or higher (TIZM GRADING's odds) - fewer above it, more it's worth
const int kGradeOddsM[11] = {0, 4, 6, 10, 20, 40, 80, 200, 340, 240, 60};
double grade_above(int g) {
  int n = 0;
  for (int k = std::max(1, g); k <= 10; ++k) n += kGradeOddsM[k];
  return n / 1000.0;
}
long long pop_of(const Variant& v);
double grade_mult(const Variant& v) {
  if (v.grade <= 0) return 1.0;
  const int g = std::min(10, v.grade);
  double m = kGradeMult[g] * std::pow(1.0 / std::max(0.001, grade_above(g)), 0.12);
  // the absolute pop report: how many slabs of this copy at this grade or higher are out there (a pop-1 slab, a premium)
  Variant raw = v;
  long long higher = 0;
  for (int k = g; k <= 10; ++k) raw.grade = k, higher += pop_of(raw);
  m *= clampd(std::pow(50.0 / std::max<long long>(1, higher), 0.06), 0.85, 1.35);
  if (v.parallel == kGoldTizm) m = std::pow(m, 0.6);
  else if (v.parallel == kBlack) m = std::pow(m, 0.45);
  return m;
}
double variant_mult(const Variant& v) {
  const double ps = kParS[std::max(0, std::min(3, v.parallel))];
  double m = kParallelMult[std::max(0, std::min(3, v.parallel))];
  if (v.parallel == kGoldTizm && (v.serial == 1 || v.serial == 10)) m *= 1.25;  // (the first and the last of the run)
  if (v.shiny) m *= std::pow(kShinyMult, 1.0 - 0.5 * ps);
  m *= std::pow(kHitMult[std::max(0, std::min(3, v.hit))], 1.0 - 0.5 * ps);
  m *= grade_mult(v);
  return m;
}
// r231: r210's per-character "hype wave" (two random swells of its fans' moods) is gone - nothing but play moves a price
// its family's DEMAND at history entry i (i < 0: now). r231 (Lance: "if i evolved all the way up to charizard, charmander and charmeleon
// shouldnt be recognized as NOT charizard"): every form of the family - evolutions, a hero's suits, an ORIGINAL's uniforms - in full
// (r210-r230: another form than the one playing ^0.6). A character not in this career's league: bulk (kDemandOut)
double demand_at(int card, int i) {
  const Line* l = line_of(card);
  if (l == nullptr || l->hist.empty()) return demand_of_level(soft_cap(kDemandOut));
  const int n = static_cast<int>(l->hist.size());
  const int at = i < 0 ? n - 1 : std::max(0, n - static_cast<int>(g_mk.days.size()) + i);
  return std::max(0.05, static_cast<double>(l->hist[static_cast<size_t>(std::min(n - 1, std::max(0, at)))]));
}
double scarcity(const Variant& v, int step) {
  if (v.parallel >= kGoldTizm) return 1.0;  // (a numbered run: its copies never grow)
  const double grow = (kWorldPacks0 + kWorldPacksDay * std::max(0, step)) / kWorldPacks0;
  return std::pow(grow, -0.12);
}
double noise(const Variant& v, int day) {
  const uint64_t h = mix64(static_cast<uint64_t>(g_mk.career) * 0x9E3779B97F4A7C15ull ^ hstr(g_set[static_cast<size_t>(v.card)].key) ^
                           (static_cast<uint64_t>(v.parallel * 64 + v.hit * 8 + (v.shiny ? 4 : 0) + (v.serial >= 1000 ? (v.serial / 1000) * 512 : 0)) << 40) ^  // (r230: an EXQUISITE tier its own)
                           (static_cast<uint64_t>(v.grade) << 50) ^ static_cast<uint64_t>(static_cast<int64_t>(day) + 100000));
  return 1.0 + kNoise * (2.0 * u01(h) - 1.0);
}
double cycle_at(int i) {  // r210: the market's mood at history entry i (< 0 now)
  const int n = static_cast<int>(g_mk.cycleHist.size());
  if (n == 0) return 0.0;
  const int nd = static_cast<int>(g_mk.days.size());
  const int at = i < 0 ? n - 1 : n - nd + i;  // (the same last entry as days)
  return g_mk.cycleHist[static_cast<size_t>(std::max(0, std::min(n - 1, at)))];
}
// r217: a JAM '96 copy's premium over its LANCINI TIZM twin's (1 a main card): ~3x bulk, more the scarcer, an auto, a grail
double jam_mult(const Variant& v, double s) {
  if (v.card < 0 || v.card >= static_cast<int>(g_set.size()) || g_set[static_cast<size_t>(v.card)].set != 1) return 1.0;
  double m = kJamBase * std::pow(kJamScarce, s);
  if (v.hit == kHitSigned || v.parallel == kBlack) m *= kJamAuto;  // (every BLACK 1/1 is signed)
  return m;  // (r231: no x kJamGrail - a popularity premium; the player's demand already prices it)
}
// r230: a LANCINI EXQUISITE copy: its tier (0: not one; an un-numbered look-up of an Exquisite card prices as its GALLERY)
bool exq_v(const Variant& v) { return v.card >= 0 && v.card < static_cast<int>(g_set.size()) && g_set[static_cast<size_t>(v.card)].set == 2; }
int exq_t(const Variant& v) {
  if (!exq_v(v)) return 0;
  const int t = xrexq::tier_of(v.serial);
  return t > 0 ? t : xrexq::kGallery;
}
bool exq_rookie_v(const Variant& v) { return exq_v(v) && g_set[static_cast<size_t>(v.card)].series == 5; }
// its tier's premium x the number's (1 / the last / its jersey number) x a rookie x a grail x its slab
double exq_mult(const Variant& v) {
  const int t = exq_t(v);
  double m = xrexq::kMult[t] * xrexq::serial_premium(v.serial, g_set[static_cast<size_t>(v.card)].no);
  if (exq_rookie_v(v)) m *= xrexq::kRookie;
  // r231: x kExqSet - the set's own premium (r230 got it from the rarity / rating / GRAIL lifts, gone now: every tier stays above its JAM '96
  // equal by pull difficulty for any player - GALLERY over a JAM BASE even for a bench player); no x kGrail - the player's demand prices it
  return m * kExqSet * grade_mult(v);
}
// r234 THE RARITY FLOOR (Lance 11-12: "/25 and less would be at a premium no matter who the character is, no matter the set. 1/1's are
// always the chase, and shouldn't be viewed as less valuable just bc it's someone unknown"): every copy numbered /25 or lower carries a
// floor of its own that NO play reads - by its set (EXQUISITE > JAM '96 > LANCINI TIZM) and its run (1/1 >> /10 > /25), an auto / patch /
// case hit / shiny adding to it. The price = the floor + the play-driven price (r231's, unchanged): the floor lifts a nobody's 1/1 into a
// big-money chase card, while every goal still moves the price and a star's 1/1 stays far above (its play part dwarfs the floor). Not
// max(floor, play): that would flatten every player under the floor to one price (a 0-goal and a 9-goal 1/1 the same). The floor takes the
// copy's slab grade (as its finish does: a 1/1's grade matters little), the day's noise and the market's mood - never its character, its
// family stage, its demand. Pull odds unchanged. MARKET-PRICES.md section 1b. The /10 and /25 floors are kept under their copies' play part
// for anyone who plays (Lance 11-12: "its def a flippers market" - a /25 bought on a hunch has to be able to double when he pops: a big floor
// under it would dampen the swing; the 1/1s, the chase, carry the big floors).
constexpr double kFloorTizm[4] = {0.0, 0.0, 6000.0, 750000.0};      // LANCINI TIZM: -, -, GOLD /10, BLACK 1/1 (a JAM '96 copy: x jam_mult,
                                                                    // the same Jam premium its play part gets: GOLD /10 x4.9 = 29,000,
                                                                    // BLACK 1/1 x7.5 (its auto) = 5,600,000 - BLACK > GOLD > BASE kept)
constexpr double kFloorExq[xrexq::kTiers] = {0.0, 0.0, 0.0, 0.0,   // EXQUISITE: GALLERY /399, SILVER LEAF /299, GOLD LEAF /99 - none
                                             6000.0,               //   EMERALD /25
                                             60000.0,              //   SAPPHIRE /10 (over JAM GOLD /10's 29,000, as its play part: ~2x)
                                             12000000.0,           //   MASTERPIECE 1/1 (patch + on-card auto): the highest floor in the game (~2x JAM BLACK's)
                                             0.0,                  //   ON-CARD AUTO /49 - none
                                             1000.0,               //   GRAND THREADS PATCH /25 (1 in 3.6 packs: 6x EMERALD's odds - the pack's median kept under its 1,000)
                                             0.0,                  //   ROOKIE PATCH AUTO /99 - none
                                             100000.0};            //   PATCH AUTO /10 (a rookie's GOLD RPA /10 too)
constexpr double kFloorAuto = 1.6, kFloorCaseHit = 1.25, kFloorShiny = 1.2;  // LANCINI TIZM / JAM: a SIGNED copy, a BOOM / HOMETOWN, a shiny
double rarity_floor(const Variant& v) {
  if (v.card < 0 || v.card >= static_cast<int>(g_set.size())) return 0.0;
  if (exq_v(v)) {
    const int t = exq_t(v);
    double f = kFloorExq[t];
    if (f <= 0.0) return 0.0;
    f *= xrexq::serial_premium(v.serial, g_set[static_cast<size_t>(v.card)].no);  // (#1, the last, its jersey number)
    return f;  // (no rookie premium here: the floor reads no character - a rookie's play part carries his x1.5)
  }
  const int par = std::max(0, std::min(3, v.parallel));
  double f = kFloorTizm[par];
  if (f <= 0.0) return 0.0;
  if (par == kGoldTizm && (v.serial == 1 || v.serial == 10)) f *= 1.25;  // (the first and the last of the run, as its finish)
  if (v.hit == kHitSigned) f *= kFloorAuto;
  else if (v.hit != kHitNone) f *= kFloorCaseHit;
  if (v.shiny) f *= kFloorShiny;
  return f * jam_mult(v, scarce_s(v));  // (a JAM '96 copy: its Jam premium, as its play part - 1 for LANCINI TIZM)
}
double price_d(const Variant& v, int i) {  // (g_m held) i: history entry (< 0 now); unrounded (the charts, the % changes)
  if (v.card < 0 || v.card >= static_cast<int>(g_set.size())) return 1.0;
  const double rf = rarity_floor(v);
  const double fl = rf > 0.0 ? rf * grade_mult(v) : 0.0;  // r234 (0: not numbered /25 or lower)
  if (exq_v(v)) {  // r230: LANCINI EXQUISITE - every copy numbered (no world growth: its runs are fixed)
    const int n = static_cast<int>(g_mk.days.size());
    const int at = i < 0 ? n - 1 : i;
    const int day = n > 0 ? g_mk.days[static_cast<size_t>(std::max(0, std::min(n - 1, at)))] : 0;
    const double s = xrexq::kS[exq_t(v)];
    const double p = (base_of(v.card, s, at) * exq_mult(v) + fl) * noise(v, day) * std::exp(cycle_at(at) * (0.4 + 0.8 * s));
    return clampd(p, 1.0, 1e14);
  }
  const int n = static_cast<int>(g_mk.days.size());
  const int at = i < 0 ? n - 1 : i;
  const int day = n > 0 ? g_mk.days[static_cast<size_t>(std::max(0, std::min(n - 1, at)))] : 0;
  const int step = g_mk.steps - (n - 1 - std::max(0, at));
  const double s = scarce_s(v);
  const double p = (base_of(v.card, s, at) * variant_mult(v) * scarcity(v, step) * jam_mult(v, s) + fl) * noise(v, day) * std::exp(cycle_at(at) * (0.4 + 0.8 * s));
  return clampd(p, 1.0, 1e14);  // (held far under xrcoin::kMax: x 1.12 and any sum of a binder still fit)
}
long long price_i(const Variant& v, int i) { return std::max(1LL, static_cast<long long>(llr(price_d(v, i)))); }
}  // namespace
// r210: SELL NOW's share: a dealer pays bulk rates for a cheap card (20% at 200 coins or less), 82% for one worth 20,000 or more,
// log-scaled between (the hobby's buylist: nobody pays full price for bulk; a pack's commons are never worth more than the pack)
// r238 (Lance 11-12: "sell now needs to be like 90% value of the card"): 90% for every card - the bulk curve is gone
double bid_ratio(long long market) {
  (void)market;
  return kBid;
}
namespace {
long long bid_of(long long p) { return std::max(1LL, static_cast<long long>(std::floor(static_cast<double>(p) * bid_ratio(p)))); }
long long ask_of(long long p) { return std::max(2LL, static_cast<long long>(std::ceil(static_cast<double>(p) * kAsk))); }
long long pop_of(const Variant& v) {
  if (v.card < 0 || v.card >= static_cast<int>(g_set.size())) return 0;
  if (exq_v(v)) {  // r230: an EXQUISITE run (its number of copies), slabs a share of it
    const double run = xrexq::kRun[exq_t(v)];
    return std::max<long long>(1, static_cast<long long>(std::lround(v.grade > 0 ? run * 0.05 * kGradeOdds[v.grade] / 100.0 : run)));
  }
  if (v.parallel == kBlack) return 1;
  if (v.parallel == kGoldTizm) return 10;
  const int r = std::max(0, std::min(2, g_set[static_cast<size_t>(v.card)].rarity));
  const double packs = kWorldPacks0 + kWorldPacksDay * std::max(0, g_mk.steps);
  double n = packs * 5.0 * kRarityShare[r] / g_rarityCount[r] * kParShare[v.parallel] * kHitShare[std::max(0, std::min(3, v.hit))];
  if (v.shiny) n *= kShinyShare;
  if (g_set[static_cast<size_t>(v.card)].set == 1) n *= kJamPop;  // r217: fewer Jam packs out there
  if (v.grade > 0) n *= 0.03 * kGradeOdds[v.grade] / 1000.0;
  return std::max<long long>(1, static_cast<long long>(std::lround(n)));
}

// ---------------------------------------------------------------------------------------- holdings
std::string sig_of(const Variant& v) {
  if (v.card < 0 || v.card >= static_cast<int>(g_set.size())) return "?";
  char b[160];
  std::snprintf(b, sizeof(b), "%s:%d:%d:%d:%d:%d:%u", g_set[static_cast<size_t>(v.card)].key.c_str(), v.parallel, v.shiny ? 1 : 0, v.hit, v.serial,
                v.grade, v.cert);
  return b;
}
void read_holdings() {
  g_hold.clear();
  if (g_src.holdings != nullptr) g_src.holdings(&g_hold);
}
bool own(const Variant& v) {
  const std::string s = sig_of(v);
  for (const Holding& h : g_hold)
    if (!h.grading && sig_of(h) == s) return true;
  return false;
}
// r210: your copies of a kind (not at grading), how many of them you've listed, whether one is free (not listed: it can be sold now,
// offered on, traded in, graded)
int held_count(const Variant& v) {
  const std::string s = sig_of(v);
  int n = 0;
  for (const Holding& h : g_hold)
    if (!h.grading && sig_of(h) == s) n += h.count;
  return n;
}
int listed_count(const Variant& v) {
  const std::string s = sig_of(v);
  int n = 0;
  for (const Mine& m : g_mk.mine) n += sig_of(m.v) == s ? 1 : 0;
  return n;
}
// r213: copies held in an open COLLECTOR TRADE (xr_trade.cpp tells them: set_held_out) - not free here either
std::vector<Variant> g_heldOut;
int held_out_count(const Variant& v) {
  if (g_heldOut.empty()) return 0;
  const std::string s = sig_of(v);
  int n = 0;
  for (const Variant& h : g_heldOut) n += sig_of(h) == s ? 1 : 0;
  return n;
}
bool own_free(const Variant& v) { return held_count(v) > listed_count(v) + held_out_count(v); }
// ---- r235: THE SINGLES MARKET (the BUY tab's SEARCH): the collectors' + the dealer's case's copies for sale (xr_trade's index), the named
// buyers, what you paid lately (the flip rule), the query, its filters, its results, your watch list + recent searches
std::vector<Offering> g_offs;  // (xr_trade: set_offerings)
unsigned g_offCareer = 0;
std::vector<Buyer> g_buyers;
struct Paid {
  long long coins = 0;
  int step = 0;
};
std::map<std::string, Paid> g_paid;  // a copy's sig -> what you paid on the market, the matchday (kept 2 matchdays; saved "Y")
std::string g_q;                     // the query ("TOG")
int g_fSet = kFSetAll, g_fFin = kFFinAny, g_fPrice = kFPriceAny, g_fSort = kSortLow;
bool g_fFam = false;                 // r239: the FAMILY chip (on: every form of a family one of whose forms the query finds)
int g_salesForm = -1;                // r239: the SALES player page's FORM tab (-1 ALL FORMS, else that form's main card)
std::vector<SearchRow> g_rows;       // the results (made on change only: a key, a filter, the index, a matchday - never per frame)
bool g_rowsDirty = true;
bool g_kb = false;                   // the search panel (the letter grid) up
std::atomic<bool> g_kbOpen{false};   // (its flag for the render thread: search_open)
int g_kbKey = 0;                     // the key / chip under its cursor
std::vector<std::string> g_recent;   // the last searches (newest first, 4; saved "Q")
std::vector<std::string> g_watch;    // watched families (line ids; saved "W")
std::vector<uint32_t> g_alerted;     // listings already alerted on (a watched player under the market)
int g_unseenAlerts = 0;
uint32_t g_askOffer = 0;             // A > YES on an Offering: its id
std::string g_askWho;
struct PendingBuy {                  // the buy the market hands xr_trade once its lock is let go (input)
  bool on = false;
  uint32_t id = 0;
  long long ask = 0;
  Variant v;
  std::string who;
};
PendingBuy g_pend;
bool g_saveDue = false;  // (a recent search to keep: saved on the game thread's next frame - a pointer's click runs on the render thread)
void rows_build();  // (below)
void watch_check();
// ---- r238: THE LIVE MARKET (real-time ticks), SALES (every completed sale, per player), NOTICES (the hub's pop-up), HOLD BEFORE YOU BUY
std::atomic<bool> g_live{false};  // the hub open with a career (xr_cards, every frame: no lock)
double g_liveAcc = 0.0;           // hub seconds toward the next tick
struct Notice {
  std::string head, title, line;
};
std::vector<Notice> g_notices;  // (oldest first, 6 at most)
void notice(const std::string& head, const std::string& title, const std::string& line) {
  g_notices.push_back({head, title, line});
  if (g_notices.size() > 6) g_notices.erase(g_notices.begin());
}
std::map<int, std::vector<SaleRec>> g_hist;  // a player's (its LANCINI TIZM card's index) sales, newest first
int g_histN = 0;
uint32_t g_histSeq = 0;
bool g_histDirty = false;
const char* const kGroupName[kSaleGroups] = {"BASE", "SILVER", "GOLD /10", "BLACK 1/1", "CASE HITS", "GRADED", "JAM '96", "EXQUISITE"};
// the SALES tab: a player picked (its main card, -1: the list of players), its focused finish group, the players list (made on change only)
int g_salesCard = -1, g_salesGroup = 0;
struct SalesRow {
  int card = -1, n = 0;  // (r239: card = the FAMILY's face - the form it plays as now; n = its sales over every form)
  long long last = 0;
  Variant lastV;
  uint32_t seq = 0;
  int form = -1;         // r239: the form the query found when it isn't the face ("TOGEPI" -> the TOGETIC row, its TOGEPI tab), -1 none
  int forms = 1;         // r239: how many forms the family has a tab for
};
std::vector<SalesRow> g_salesRows;
bool g_salesDirty = true;
void record_locked(const Variant& v, long long price, const std::string& seller, const std::string& buyer, int kind);  // (below)
std::string copy_words(const Variant& v);  // (below)
void sales_rows_build();                   // (below)
// ---- r240: SELL, FILTERED (xr_market.h) - every kind of copy you have, made on change only (your binder, the prices, your listings, your open
// trades, your museum: sell_key), then filtered + sorted on a key / a filter (sell_rows_build: no price read - instant); never per frame
struct SellItem {
  Holding h;
  std::string sig, name;      // (name: search_name - what you type matches)
  uint64_t kind = 0;          // the CARD for "your last copy": the same card in the same finish (its number / grade / cert aside)
  long long market = 0, bid = 0;
  float chg = 0.0f;
  int listed = 0, heldOut = 0, museum = 0, free = 0, spare = 0;
  int set = 0, tier = 0, seen = 0, rank = 0, serialKey = 1 << 30;
};
std::vector<SellItem> g_sell;   // (bounded by your binder: one a kind of copy)
std::vector<int> g_sellRows;    // the shown ones (g_sell's places), filtered + sorted
uint64_t g_sellKey = 0;
bool g_sellRowsDirty = true;
std::string g_sq;               // SELL's own query (BUY / SALES keep theirs)
int g_sfSet = kFSetAll, g_sfFin = kSFinAny, g_sfType = kSTypeAny, g_sfStat = kSStatAll, g_sfPrice = kSPriceAny, g_sfSort = kSSortHigh;
bool g_sfFam = false;
bool g_sLast = false;           // LAST COPY: SELL TOO (off whenever the panel closes)
int g_sPct = 1;                 // LIST ALL SHOWN at kSListPct[g_sPct]
int g_bulkN = 0, g_bulkPct = 100;  // the bulk confirm's numbers (asked: re-checked at YES)
long long g_bulkTotal = 0;
bool g_bulkLast = false;
std::vector<Variant> g_museumV;  // the CARD MUSEUM's copies (xr_cards: set_museum)
// ---- r241: CARDS > GRADING's list (xr_market.h) - SELL's table of your copies (g_sell, made on change only) with GRADING's own query, chips,
// rows, cursor; its search panel is the market's (g_kb) opened from GRADING (g_gradeKb); a send asking = g_ask 8
std::string g_gq;
int g_gfSet = kFSetAll, g_gfFin = kSFinAny, g_gfType = kSTypeAny, g_gfStat = kGStatAll, g_gfSort = kGSortHigh;
bool g_gfFam = false;
std::vector<int> g_gradeRows;  // the shown ones (g_sell's places), filtered + sorted
bool g_gradeRowsDirty = true;
int g_gCur = 0, g_gTop = 0;
std::atomic<bool> g_gradeKb{false};    // the search panel up is GRADING's (search_mode: the render thread reads it)
std::atomic<bool> g_gradeAskA{false};  // a GRADING send asking (g_ask 8) - grade_leave reads it without the lock
Variant g_gradeAskV;
int g_gradeAskCost = 0;
struct PriceC {
  int epoch = 0;
  long long p = 0;
  float chg = 0.0f;
};
std::unordered_map<std::string, PriceC> g_pcache;  // a copy's sig -> its price + move at g_priceEpoch (cleared each new epoch: bounded)
int g_priceEpoch = 1;                               // moves whenever prices can (a matchday, a re-price, a career)
void sell_new_prices();                             // (below: the epoch moved - the cache warmed for your binder)
void sell_fresh();                                  // (below)
// r235: who buys your listing / bids on your card - a named collector who can afford it (65% of the time when one can: the copy goes into his
// binder, xr_trade world_sold), else one of the league's anonymous collectors (r201-r234: one of 24 handles, six of them the named ones').
// No buyers told (an old harness): r201's pick exactly
std::string pick_buyer(uint64_t h, long long amount) {
  if (g_buyers.empty()) return kWho[mix64(h) % 24];
  if (u01(h ^ 0xB0Bull) < 0.65) {
    std::vector<size_t> can;
    for (size_t i = 0; i < g_buyers.size(); ++i)
      if (g_buyers[i].wallet >= amount) can.push_back(i);
    if (!can.empty()) return g_buyers[can[static_cast<size_t>(mix64(h ^ 0xB0Cull) % can.size())]].handle;
  }
  std::vector<const char*> anon;
  for (const char* w : kWho) {
    bool named = false;
    for (const Buyer& b : g_buyers) named = named || b.handle == w;
    if (!named) anon.push_back(w);
  }
  return anon.empty() ? std::string(kWho[mix64(h) % 24]) : std::string(anon[static_cast<size_t>(mix64(h ^ 0xB0Dull) % anon.size())]);
}
void buyer_paid(const std::string& who, long long coins) {  // (his wallet down for the rest of the matchday's looks)
  for (Buyer& b : g_buyers)
    if (b.handle == who) b.wallet = std::max(0LL, b.wallet - coins);
}
void tell_listed() {  // (the binder keeps listed copies out of trade-ins + grading; r213: + the ones held in a collector trade)
  if (g_src.listed == nullptr) return;
  std::vector<Variant> v;
  for (const Mine& m : g_mk.mine) v.push_back(m.v);
  for (const Variant& h : g_heldOut) v.push_back(h);
  g_src.listed(v);
}
long long portfolio_value(int i) {
  long long t = 0;
  for (const Holding& h : g_hold)
    if (!h.grading) t = xrcoin::add(t, xrcoin::from_double(static_cast<double>(price_i(h, i)) * std::max(0, h.count)));  // (r210: held, never wrapping)
  return t;
}

// ---------------------------------------------------------------------------------------- r238: SALES (every completed sale, per player)
int family_card(int card) {  // its player's LANCINI TIZM card (a JAM '96 / EXQUISITE twin: its base)
  if (card < 0 || card >= static_cast<int>(g_set.size())) return -1;
  const CardInfo& c = g_set[static_cast<size_t>(card)];
  return c.set >= 1 && c.base >= 0 && c.base < static_cast<int>(g_set.size()) ? c.base : card;
}
int group_of(const Variant& v) {
  if (v.card < 0 || v.card >= static_cast<int>(g_set.size())) return 0;
  const int set = g_set[static_cast<size_t>(v.card)].set;
  if (set == 2) return 7;
  if (set == 1) return 6;
  if (v.grade > 0) return 5;
  if (v.hit != kHitNone) return 4;
  return std::max(0, std::min(3, v.parallel));
}
// bounded: kSalesPerGroup a player's group (its oldest out), kSalesMax in all (the oldest anywhere out)
void hist_trim(int fam) {
  auto it = g_hist.find(fam);
  if (it != g_hist.end()) {
    int per[kSaleGroups] = {};
    std::vector<SaleRec>& v = it->second;
    for (size_t i = 0; i < v.size();) {
      int& n = per[group_of(v[i].v)];
      if (++n > kSalesPerGroup) {
        v.erase(v.begin() + static_cast<long>(i));
        --g_histN;
      } else {
        ++i;
      }
    }
  }
  while (g_histN > kSalesMax) {
    auto oldest = g_hist.end();
    for (auto f = g_hist.begin(); f != g_hist.end(); ++f)
      if (!f->second.empty() && (oldest == g_hist.end() || f->second.back().seq < oldest->second.back().seq)) oldest = f;
    if (oldest == g_hist.end()) break;
    oldest->second.pop_back();
    --g_histN;
    if (oldest->second.empty()) g_hist.erase(oldest);
  }
}
void record_locked(const Variant& v, long long price, const std::string& seller, const std::string& buyer, int kind) {  // (g_m held)
  const int fam = family_card(v.card);
  if (fam < 0 || price < 1 || g_mk.career == 0) return;
  SaleRec r;
  r.v = v;
  r.price = price;
  r.step = g_mk.steps, r.season = std::max(1, g_mk.season), r.week = std::max(1, g_mk.week), r.kind = kind;
  r.seq = ++g_histSeq;
  r.seller = seller.empty() ? std::string("?") : seller;
  r.buyer = buyer.empty() ? std::string("?") : buyer;
  for (std::string* s : {&r.seller, &r.buyer})
    for (char& ch : *s)
      if (ch == '\t' || ch == '\n') ch = ' ';
  g_hist[fam].insert(g_hist[fam].begin(), r);
  ++g_histN;
  hist_trim(fam);
  g_histDirty = true;
  g_salesDirty = true;
}

// ---------------------------------------------------------------------------------------- re-pricing
std::string name_of_card(int c) { return c >= 0 && c < static_cast<int>(g_set.size()) ? upper(g_set[static_cast<size_t>(c)].name) : "?"; }
void headline(const std::string& t, uint32_t rgb, bool toLeague) {
  Headline h;
  h.day = g_mk.steps;
  h.text = t;
  h.rgb = rgb;
  g_mk.news.insert(g_mk.news.begin(), h);
  if (g_mk.news.size() > 14) g_mk.news.resize(14);
  if (toLeague) {
    if (g_src.news != nullptr) g_src.news(("STRIKERZ MARKET: " + t).c_str());  // (r217: the feature pages are STRIKERZ; the cards stay LANCINI TIZM)
    else PortFranchiseBookNews(("STRIKERZ MARKET: " + t).c_str());
  }
}

// One matchday (r231: THE STATS-DRIVEN MARKET - MARKET-PRICES.md 2). Every family's DEMAND from its player's play in this league alone:
//   LEGACY  (slow, it holds - an all-time great's cards never crash in an off week): awards (MVP +0.45, GOLDEN BOOT +0.35, ROOKIE OF THE
//           YEAR +0.2), a title (+0.35 x his share of the club's goals), a strong season carried in at its end, career goals (log), retiring
//           (by his career: goals + awards)
//   SEASON  (pulled 45% a matchday toward its target): his goals a game against the league's (a log ratio, trusted from ~6 games), the
//           club's record x HIS share of its goals (a passenger on an unbeaten club gets little), the Golden Boot / MVP / Rookie races,
//           his level; a brand-new career's first games lean on the players' ratings (and a new season's on last season's level)
//   FORM    (fast, mean-reverting: kFormKeep a matchday): his goals this matchday over what he usually scores - breakouts spike, cold
//           spells dip, both fade unless repeated (the franchise's form is left out: a club's results move it)
//   + a one-matchday SHOCK (an evolution, an award, a milestone). level = soft_cap(the sum) (a dynasty plateaus); DEMAND = 3 e^(1.1 level).
// The rows of one family (an evolution line / a hero's suits / an ORIGINAL's uniforms: their line id) are ONE player - whichever form
// plays, his numbers lift every form's cards. warm: the pre-history at a new career (no events, no headlines).
std::string reason_text(const Line& l, int bootRank, int mvpRank, int royRank, int w, int L, bool champ, int astRank = 0);
// r232 (Lance: "you defff need to add assists into the equation"): PRODUCTION = goals + kAstW x assists - an assist is most of a
// goal (the pass that made it); the season's numbers, his form and his legacy all read production, so a playmaker rises with
// his assists as a scorer does with his goals
constexpr double kAstW = 0.7;
double prod_of(int goals, int assists) { return std::max(0, goals) + kAstW * std::max(0, assists); }
double career_legacy(double prod) { return 0.12 * std::log2(1.0 + std::max(0.0, prod) / 30.0); }
double line_level(const Line& l) { return soft_cap(l.heat + l.fast + l.legacy + career_legacy(prod_of(l.careerGoals, l.careerAst)) + l.shock); }
void step(const PortFranchiseMarketState& fs, int dayIndex, bool warm, int pulls) {
  const int np = std::max(1, pulls);
  const bool newSeason = g_mk.season >= 0 && fs.season > g_mk.season;
  // ---- the league now: each club's production (its players' regular-season goals + assists), each player's production a game,
  // the league's mean (r231: goals alone)
  double teamGoals[PORT_FR_CLUBS] = {};
  auto rprod = [](const PortFranchiseMarketRow& r) { return prod_of(r.regGoals, r.regAssists); };
  for (int i = 0; i < fs.count; ++i) {
    const PortFranchiseMarketRow& r = fs.rows[i];
    if (r.club >= 0 && r.club < PORT_FR_CLUBS) teamGoals[r.club] += rprod(r);
  }
  auto games_of = [&](const PortFranchiseMarketRow& r) { return r.club >= 0 && r.club < PORT_FR_CLUBS ? fs.clubWins[r.club] + fs.clubLosses[r.club] : 0; };
  auto rate_of = [&](const PortFranchiseMarketRow& r) {
    const int gp = games_of(r);
    return gp > 0 ? rprod(r) / gp : 0.0;
  };
  auto goal_rate_of = [&](const PortFranchiseMarketRow& r) {  // (the Golden Boot's tie-break: goals alone)
    const int gp = games_of(r);
    return gp > 0 ? static_cast<double>(std::max(0, r.regGoals)) / gp : 0.0;
  };
  // (his share of his club's production x2.5, 0..1: 40% of it = all of the club's record is his; a club with under 3: 0.3 each)
  auto contrib_of = [&](const PortFranchiseMarketRow& r) {
    if (r.club < 0 || r.club >= PORT_FR_CLUBS) return 0.0;
    const double tg = teamGoals[r.club];
    return tg < 3.0 ? 0.3 : clampd(2.5 * rprod(r) / tg, 0.0, 1.0);
  };
  double mu = 0.0;
  int muN = 0;
  for (int i = 0; i < fs.count; ++i)
    if (fs.rows[i].role <= 2 && games_of(fs.rows[i]) >= 1) mu += rate_of(fs.rows[i]), ++muN;
  mu = muN > 0 ? mu / muN : 0.3;
  // ---- the races: the GOLDEN BOOT (regular-season goals; ties: the better rate), the MVP (his own: goals x3 + the club's wins x2 x his
  // share - r231: individual, no team passengers; the franchise's form is left out: a club's results move it), ROOKIE OF THE YEAR (a rookie's goals)
  // r232: + the ASSISTS race (regular-season assists; ties: more goals) - awards night's PLAYMAKER OF THE YEAR
  std::vector<std::pair<double, int>> boot, mvp, roy, ast;
  for (int i = 0; i < fs.count; ++i) {
    const PortFranchiseMarketRow& r = fs.rows[i];
    if (r.role > 2) continue;
    const int w = r.club >= 0 && r.club < PORT_FR_CLUBS ? fs.clubWins[r.club] : 0;
    if (r.regGoals > 0) boot.push_back({r.regGoals + 0.01 * goal_rate_of(r), i});
    mvp.push_back({3.0 * r.regGoals + 2.0 * w * contrib_of(r), i});
    if (r.rookie && r.regGoals > 0) roy.push_back({r.regGoals + 0.01 * goal_rate_of(r), i});
    if (r.regAssists > 0) ast.push_back({r.regAssists + 0.001 * std::max(0, r.regGoals), i});
  }
  auto ranks = [](std::vector<std::pair<double, int>>& v, std::unordered_map<int, int>* out) {
    std::stable_sort(v.begin(), v.end(), [](const std::pair<double, int>& a, const std::pair<double, int>& b) { return a.first > b.first; });
    // (ties share a place: two players with the same numbers are the same in the race)
    for (size_t k = 0; k < v.size(); ++k) {
      const int place = k > 0 && v[k].first == v[k - 1].first ? (*out)[v[k - 1].second] : static_cast<int>(k) + 1;
      if (place > 10) break;
      (*out)[v[k].second] = place;
    }
  };
  std::unordered_map<int, int> bootRank, mvpRank, royRank, astRank;
  ranks(boot, &bootRank);
  ranks(mvp, &mvpRank);
  ranks(roy, &royRank);
  ranks(ast, &astRank);  // r232
  auto rank_in = [](const std::unordered_map<int, int>& m, int i) {
    auto it = m.find(i);
    return it != m.end() ? it->second : 0;
  };
  // ---- a row's SEASON target
  auto target = [&](int i, const Line& l, int card) {
    const PortFranchiseMarketRow& r = fs.rows[i];
    const int gp = games_of(r);
    const double rate = rate_of(r), sample = std::min(1.0, gp / 6.0);
    double T = gp >= 1 ? 0.95 * clampd(std::log((rate + 0.08) / (mu + 0.08)), -1.2, 1.8) * sample : 0.0;
    const double contrib = contrib_of(r);
    const bool club = r.club >= 0 && r.club < PORT_FR_CLUBS;
    const int w = club ? fs.clubWins[r.club] : 0, streak = club ? fs.clubStreak[r.club] : 0;
    if (gp >= 2) T += 0.3 * (static_cast<double>(w) / gp - 0.5) * contrib;  // (the club's record: a small share, only as much as he carries it)
    T += 0.015 * clampd(streak, -4, 4) * contrib;
    const int br = rank_in(bootRank, i), mr = rank_in(mvpRank, i), rr = rank_in(royRank, i), ar = rank_in(astRank, i);
    static const double kBootRace[11] = {0, 0.45, 0.30, 0.22, 0.12, 0.12, 0.05, 0.05, 0.05, 0.05, 0.05};
    // r232: the ASSISTS race - a playmaker's own race (a little under the Golden Boot's: the goal is the bigger moment)
    static const double kAstRace[11] = {0, 0.32, 0.22, 0.16, 0.09, 0.09, 0.04, 0.04, 0.04, 0.04, 0.04};
    if (gp >= 3 && br > 0) T += kBootRace[br];
    if (gp >= 3 && ar > 0) T += kAstRace[ar];
    if (gp >= 4 && mr == 1) T += 0.30;
    else if (gp >= 4 && mr > 0 && mr <= 3) T += 0.15;
    if (gp >= 4 && rr == 1) T += 0.20;
    else if (gp >= 4 && rr > 0 && rr <= 3) T += 0.08;
    T += 0.35 * (r.level - 8) / 40.0;
    // (a brand-new career / season's first games: the players' ratings, then last season's level - fading over 8 games)
    const double rating = card >= 0 ? g_set[static_cast<size_t>(card)].rating : 75.0;
    const double rp = clampd(0.7 * (rating - 75.0) / 20.0, -0.8, 0.8);
    const double w0 = std::max(0.0, 1.0 - gp / 8.0);
    T += w0 * (l.lastSeason >= 0 && l.lastSeason < fs.season && l.prior != 0.0 ? 0.5 * rp + 0.5 * l.prior : rp);
    if (fs.champion >= 0 && r.club == fs.champion) T += 0.25 * contrib;
    return T;
  };
  // rows of one family together (a generic sidekick kind plays for several clubs: one market for its card, their average)
  std::vector<std::string> rowId(static_cast<size_t>(fs.count));
  std::unordered_map<std::string, std::vector<int>> groups;
  for (int i = 0; i < fs.count; ++i) {
    const int c = card_of_key(fs.rows[i].key);
    std::string id = c >= 0 ? g_set[static_cast<size_t>(c)].line : std::string(fs.rows[i].line);
    if (id.empty()) id = fs.rows[i].line;
    rowId[static_cast<size_t>(i)] = id;
    groups[id].push_back(i);
  }
  struct Move {
    std::string id;
    double was, now;
  };
  std::vector<Move> moves;
  std::unordered_map<std::string, bool> present;
  const std::string seasonTag = " S" + std::to_string(fs.season);
  for (int i = 0; i < fs.count; ++i) {
    const std::vector<int>& grp = groups[rowId[static_cast<size_t>(i)]];
    int rep = grp.front();  // (the group's top producer speaks for it - r232: goals + assists)
    for (int j : grp)
      if (rprod(fs.rows[j]) > rprod(fs.rows[rep])) rep = j;
    if (rep != i) continue;
    const PortFranchiseMarketRow& r = fs.rows[i];
    const int c = card_of_key(r.key);
    const std::string id = rowId[static_cast<size_t>(i)];
    Line& l = g_mk.lines[id];
    l.id = id;
    present[id] = true;
    const double wasLevel = l.seen ? line_level(l) : 0.0;
    // a new season: the last one's level into the legacy (a strong season builds it) and into this one's first games
    const bool seasonTurn = l.lastSeason >= 0 && fs.season > l.lastSeason;
    if (seasonTurn) {
      l.legacy += 0.15 * clampd(l.heat, 0.0, 2.0);
      l.prior = 0.6 * l.heat;
      l.heat *= 0.5;
      l.fast = 0.0;
    }
    double T = 0.0;
    for (int j : grp) T += target(j, l, c);
    T /= static_cast<double>(grp.size());
    // FORM: this matchday's goals over his usual (fast, fading)
    int wk = 0;
    if (l.seen) wk = seasonTurn || r.seasonGoals < l.seasonGoals ? std::max(0, r.seasonGoals) : r.seasonGoals - l.seasonGoals;
    // r232: + his assists this matchday (a line read before r232's assists: none counted at its first read - no fake week)
    int wkA = 0;
    if (l.seen && l.astSeen)
      wkA = seasonTurn || r.seasonAssists < l.seasonAst ? std::max(0, r.seasonAssists) : r.seasonAssists - l.seasonAst;
    const int gp = games_of(r);
    const double usual = gp >= 3 ? rate_of(r) : mu;
    // (his own production over his usual - the franchise's form is left out: a club's wins and losses move it, and a passenger's form isn't his)
    const double x = clampd(0.55 * (prod_of(wk, wkA) - usual * np) / np, -0.6, 1.4);
    for (int k = 0; k < np; ++k) l.fast = kFormKeep * l.fast + (1.0 - kFormKeep) * x;
    const int streak = r.club >= 0 && r.club < PORT_FR_CLUBS ? fs.clubStreak[r.club] : 0;
    const int w = r.club >= 0 && r.club < PORT_FR_CLUBS ? fs.clubWins[r.club] : 0, L = r.club >= 0 && r.club < PORT_FR_CLUBS ? fs.clubLosses[r.club] : 0;
    const int br = rank_in(bootRank, i), mr = rank_in(mvpRank, i), rr = rank_in(royRank, i), ar = rank_in(astRank, i);
    const double contrib = contrib_of(r);
    if (!warm && l.seen) {
      const std::string nm = name_of_card(c);
      if (c >= 0 && l.activeCard >= 0 && c != l.activeCard && l.club >= 0) {
        l.shock += 0.15;
        headline(name_of_card(l.activeCard) + " EVOLVES INTO " + nm + " - THE WHOLE FAMILY'S CARDS CLIMB", 0x39FF88, false);
      }
      if (wk >= 3) headline(nm + " CARDS HOT AFTER A " + std::to_string(wk) + "-GOAL MATCHDAY", 0xFFB020, false);
      if (r.careerGoals / 50 > l.careerGoals / 50 && r.careerGoals >= 50) {
        l.shock += 0.10;
        headline(nm + ": " + std::to_string(r.careerGoals / 50 * 50) + " CAREER GOALS - HIS CARDS CLIMB", 0xFFD23F, false);
      }
      // r232: a playmaker's matchday and milestones
      if (wkA >= 3 && wk < 3) headline(nm + " CARDS HOT AFTER A " + std::to_string(wkA) + "-ASSIST MATCHDAY", 0xFFB020, false);
      if (l.astSeen && r.careerAssists / 50 > l.careerAst / 50 && r.careerAssists >= 50) {
        l.shock += 0.08;
        headline(nm + ": " + std::to_string(r.careerAssists / 50 * 50) + " CAREER ASSISTS - HIS CARDS CLIMB", 0xFFD23F, false);
      }
    }
    // awards (once the regular season is over): legacy that holds
    const std::string akey = r.key;
    auto won = [&](const char* k) { return k != nullptr && k[0] != '\0' && akey == k; };
    if (won(fs.mvp) && l.why.find("|MVP" + seasonTag) == std::string::npos) {
      l.legacy += 0.45;
      l.shock += 0.2;
      ++l.mvps;
      l.why += "|MVP" + seasonTag;
      if (!warm) headline(name_of_card(c) + " WINS MVP - COLLECTORS PILE IN", 0xFFD23F, true);
    }
    if (won(fs.boot) && l.why.find("|BOOT" + seasonTag) == std::string::npos) {
      l.legacy += 0.35;
      l.shock += 0.15;
      ++l.boots;
      l.why += "|BOOT" + seasonTag;
      if (!warm) headline("GOLDEN BOOT: " + name_of_card(c) + " (" + std::to_string(r.regGoals) + " GOALS) - HIS CARDS CLIMB", 0xFFD23F, false);
    }
    if (won(fs.rookie) && l.why.find("|ROOK" + seasonTag) == std::string::npos) {
      l.legacy += 0.20;
      ++l.roys;
      l.why += "|ROOK" + seasonTag;
    }
    if (won(fs.playmaker) && l.why.find("|PLAY" + seasonTag) == std::string::npos) {  // r232: PLAYMAKER OF THE YEAR
      l.legacy += 0.25;
      l.shock += 0.12;
      ++l.pms;
      l.why += "|PLAY" + seasonTag;
      if (!warm) headline("PLAYMAKER OF THE YEAR: " + name_of_card(c) + " (" + std::to_string(r.regAssists) + " ASSISTS) - HIS CARDS CLIMB", 0xFFD23F, false);
    }
    if (fs.champion >= 0 && r.club == fs.champion && l.why.find("|CHAMP" + seasonTag) == std::string::npos) {
      l.legacy += 0.35 * contrib;  // (a title is his as much as he scored it)
      if (contrib >= 0.2) ++l.titles;
      l.why += "|CHAMP" + seasonTag;
    }
    for (int k = 0; k < np; ++k) l.heat += kHeatPull * (T - l.heat);
    l.shock *= 0.55;
    l.activeCard = c;
    l.level = r.level;
    l.regGoals = r.regGoals;
    l.seasonGoals = r.seasonGoals;
    l.careerGoals = r.careerGoals;
    l.form = r.form;
    l.streak = streak;
    l.club = r.club;
    l.role = r.role;
    l.seen = true;
    l.retired = false;
    l.lastSeason = fs.season;
    l.contrib = contrib;
    l.wkGoals = wk;
    l.regAst = r.regAssists;  // r232
    l.seasonAst = r.seasonAssists;
    l.careerAst = r.careerAssists;
    l.wkAst = wkA;
    l.astSeen = true;
    // why it moves (the detail panel's line): goals (+ r232: assists), the club's run, the races; the award tags ride along after a '|'
    std::string why;
    if (gp > 0)
      why += std::to_string(r.regGoals) + (r.regGoals == 1 ? " GOAL" : " GOALS") +
             (r.regAssists > 0 ? " " + std::to_string(r.regAssists) + (r.regAssists == 1 ? " ASSIST" : " ASSISTS") : std::string()) + " IN " +
             std::to_string(gp);
    if (streak >= 2) why += (why.empty() ? "" : "  -  ") + std::string("W") + std::to_string(streak);
    if (streak <= -2) why += (why.empty() ? "" : "  -  ") + std::string("L") + std::to_string(-streak);
    if (gp >= 3 && br > 0) why += (why.empty() ? "" : "  -  ") + std::string("BOOT RACE #") + std::to_string(br);
    if (gp >= 3 && ar > 0 && ar <= 5) why += (why.empty() ? "" : "  -  ") + std::string("ASSISTS RACE #") + std::to_string(ar);  // r232
    if (gp >= 4 && mr > 0 && mr <= 3) why += (why.empty() ? "" : "  -  ") + std::string("MVP RACE #") + std::to_string(mr);
    if (l.fast >= 0.25) why += (why.empty() ? "" : "  -  ") + std::string("ON FIRE");
    if (l.fast <= -0.2) why += (why.empty() ? "" : "  -  ") + std::string("COLD SPELL");
    why += (why.empty() ? "" : "  -  ") + std::string("LV ") + std::to_string(r.level);
    const size_t bar = l.why.find('|');
    l.why = why + (bar != std::string::npos ? l.why.substr(bar) : std::string());
    l.reason = reason_text(l, gp >= 3 ? br : 0, gp >= 4 ? mr : 0, gp >= 4 ? rr : 0, w, L, fs.champion >= 0 && r.club == fs.champion, gp >= 3 ? ar : 0);
    moves.push_back({id, wasLevel, line_level(l)});
  }
  // the lines not in the league now: retired = LEGEND (the legacy holds; the season fades), else a free agent fading
  std::unordered_map<std::string, int> retired;
  for (int i = 0; i < fs.retiredCount; ++i) retired[fs.retired[i]] = fs.retiredSeason[i];
  for (auto& kv : g_mk.lines) {
    Line& l = kv.second;
    if (present.count(kv.first)) continue;
    const double was = line_level(l);
    auto rt = retired.find(kv.first);
    if (rt != retired.end() && !l.retired) {
      l.retired = true;
      l.retiredSeason = rt->second;
      // (a career's worth: his goals + assists (r232) + awards - a bench player's retirement is no event)
      const double career = std::min(1.0, prod_of(l.careerGoals, l.careerAst) / 80.0);
      l.legacy += 0.35 * career + 0.10 * (l.mvps + l.boots + l.pms);
      l.shock += 0.15 * career;
      if (!warm && l.seen && (prod_of(l.careerGoals, l.careerAst) >= 40 || l.mvps + l.boots + l.pms > 0)) headline(name_of_card(l.activeCard) + " RETIRES - LEGEND CARDS JUMP", 0xFFD23F, true);
    }
    const double T = l.retired ? -0.3 : -0.4;  // (no more goals: the season level fades; the legacy is what holds)
    for (int k = 0; k < np; ++k) l.heat += kHeatPull * (T - l.heat), l.fast *= kFormKeep;
    l.shock *= 0.55;
    if (!l.retired) l.club = -1;
    const size_t bar = l.why.find('|');
    l.why = std::string(l.retired ? "LEGEND  -  RETIRED SEASON " + std::to_string(l.retiredSeason) : "NOT ON A CLUB") +
            (bar != std::string::npos ? l.why.substr(bar) : std::string());
    l.reason = reason_text(l, 0, 0, 0, 0, 0, false);
    moves.push_back({kv.first, was, line_level(l)});
  }
  // ---- the TOP CARDS race (every family in the league or retired, by demand)
  std::vector<std::pair<double, Line*>> race;
  for (auto& kv : g_mk.lines) {
    Line& l = kv.second;
    l.prevRank = l.rank;
    l.rank = 0;
    if (l.activeCard >= 0 && (l.club >= 0 || l.retired)) race.push_back({line_level(l), &l});
  }
  std::stable_sort(race.begin(), race.end(), [](const std::pair<double, Line*>& a, const std::pair<double, Line*>& b) {
    return a.first != b.first ? a.first > b.first : a.second->id < b.second->id;
  });
  for (size_t k = 0; k < race.size(); ++k) race[k].second->rank = static_cast<int>(k) + 1;
  double starsNow = 0.0;
  int starsN = 0;
  for (size_t k = 0; k < race.size() && k < 20; ++k) starsNow += demand_of_level(race[k].first), ++starsN;
  starsNow = starsN > 0 ? starsNow / starsN : 0.0;
  // ---- r231: the market's mood moves on the LEAGUE's events only (r210-r230: a random boom 5% / crash 3.5% a matchday): the playoffs
  // starting, awards night, the champions crowned, a single-season goals record (booms); the stars cooling (the top 20's demand -12%
  // in a matchday) and the off-season (cooling). Fading 28% a matchday as before.
  {
    double ev = 0.0;
    std::string up, down;
    int bestGoals = 0, bestRow = -1;
    for (int i = 0; i < fs.count; ++i)
      if (fs.rows[i].seasonGoals > bestGoals) bestGoals = fs.rows[i].seasonGoals, bestRow = i;
    if (!warm) {
      auto boom = [&](double v, const std::string& why) {  // (the biggest of the matchday's events, never stacked)
        if (v > ev) ev = v, up = why;
      };
      if (fs.phase != PORT_FR_PHASE_REGULAR && g_mk.phase == PORT_FR_PHASE_REGULAR) boom(0.15, "THE PLAYOFFS ARE HERE");
      if (fs.mvp[0] != '\0' && g_mk.awardsSeason != fs.season) {
        g_mk.awardsSeason = fs.season;
        const int mc = card_of_key(fs.mvp), bc = card_of_key(fs.boot);
        boom(0.25, "AWARDS NIGHT: " + name_of_card(mc) + " MVP" + (bc >= 0 ? ", " + name_of_card(bc) + " GOLDEN BOOT" : std::string()));
      }
      if (fs.champion >= 0 && g_mk.champSeason != fs.season) g_mk.champSeason = fs.season, boom(0.20, "THE CHAMPIONS ARE CROWNED - THEIR SCORERS LEAD");
      if (g_mk.recordGoals > 0 && bestGoals > g_mk.recordGoals && bestRow >= 0 && g_mk.recordSeason != fs.season) {
        g_mk.recordSeason = fs.season;
        boom(0.12, name_of_card(card_of_key(fs.rows[bestRow].key)) + " BREAKS THE SEASON GOALS RECORD (" + std::to_string(bestGoals) + ")");
      }
      if (newSeason) ev -= 0.12, down = "THE OFF-SEASON - COLLECTORS WAIT FOR NEW NUMBERS";
      else if (g_mk.starsWas > 0.0 && starsNow < 0.88 * g_mk.starsWas) ev -= 0.10, down = "THE LEAGUE'S STARS WENT COLD";
    }
    if (fs.mvp[0] != '\0' && warm) g_mk.awardsSeason = fs.season;  // (a career first seen after its awards: no boom for old news)
    if (fs.champion >= 0 && warm) g_mk.champSeason = fs.season;
    if (newSeason) g_mk.recordGoals = std::max(g_mk.recordGoals, g_mk.seasonBest), g_mk.seasonBest = 0;  // (a season's best becomes the record to beat)
    g_mk.seasonBest = std::max(g_mk.seasonBest, bestGoals);
    g_mk.starsWas = starsNow;
    g_mk.phase = fs.phase;
    g_mk.cycle = clampd(g_mk.cycle * std::pow(kCycleKeep, np) + ev, -0.8, 1.0);
    const std::string pct = std::to_string(static_cast<int>(std::lround((std::exp(std::fabs(ev)) - 1) * 100)));
    if (ev > 0) headline("THE STRIKERZ MARKET IS BOOMING - " + up + " (+" + pct + "%)", 0x39FF88, true);
    if (ev < 0) headline("THE MARKET COOLS - " + down + " (-" + pct + "%)", 0xFF4D5E, true);
  }
  // the history
  g_mk.days.push_back(dayIndex);
  g_mk.cycleHist.push_back(g_mk.cycle);
  while (g_mk.cycleHist.size() < g_mk.days.size()) g_mk.cycleHist.insert(g_mk.cycleHist.begin(), g_mk.cycleHist.front());
  if (static_cast<int>(g_mk.days.size()) > kHist) g_mk.days.erase(g_mk.days.begin());
  while (g_mk.cycleHist.size() > g_mk.days.size()) g_mk.cycleHist.erase(g_mk.cycleHist.begin());
  for (auto& kv : g_mk.lines) {
    Line& l = kv.second;
    l.hist.push_back(static_cast<float>(demand_of_level(line_level(l))));
    while (l.hist.size() > g_mk.days.size()) l.hist.erase(l.hist.begin());
    while (l.hist.size() < g_mk.days.size()) l.hist.insert(l.hist.begin(), l.hist.front());
  }
  ++g_mk.steps;
  if (warm) return;
  // the biggest movers make the headlines (the league news gets the top one) - with the play behind them
  std::stable_sort(moves.begin(), moves.end(), [](const Move& a, const Move& b) { return (a.now - a.was) > (b.now - b.was); });
  bool told = false;
  if (!moves.empty()) {
    const Move& up = moves.front();
    const Line& l = g_mk.lines[up.id];
    const double pct = (std::pow(demand_of_level(up.now) / demand_of_level(up.was), 0.55) - 1.0) * 100.0;
    if (pct >= 8.0 && l.activeCard >= 0) {
      headline(name_of_card(l.activeCard) + " CARDS UP " + std::to_string(static_cast<int>(pct)) + "%  -  " + l.reason, 0x39FF88, true);
      told = true;
    }
    const Move& dn = moves.back();
    const Line& d = g_mk.lines[dn.id];
    const double dp = (1.0 - std::pow(demand_of_level(dn.now) / demand_of_level(dn.was), 0.55)) * 100.0;
    if (dp >= 8.0 && d.activeCard >= 0)
      headline("MARKET COOLS ON " + name_of_card(d.activeCard) + ": DOWN " + std::to_string(static_cast<int>(dp)) + "%  -  " + d.reason, 0xFF4D5E, !told);
  }
}
// r231: the play behind a family's price, short ("14 GOALS - GOLDEN BOOT LEADER - UNBEATEN CLUB"): its races first, then the club's run
// (only when he carries it), his week, his legacy
std::string reason_text(const Line& l, int bootRank, int mvpRank, int royRank, int w, int L, bool champ, int astRank) {
  std::vector<std::string> p;
  if (l.retired) {
    p.push_back("LEGEND - RETIRED");
  } else if (l.club < 0) {
    p.push_back("NOT ON A CLUB");
  } else {
    // r232: "9 GOALS 12 ASSISTS" (assists once he has any); the ASSISTS race beside the Golden Boot's
    p.push_back(std::to_string(l.regGoals) + (l.regGoals == 1 ? " GOAL" : " GOALS") +
                (l.regAst > 0 ? " " + std::to_string(l.regAst) + (l.regAst == 1 ? " ASSIST" : " ASSISTS") : std::string()));
    if (bootRank == 1) p.push_back("GOLDEN BOOT LEADER");
    else if (bootRank > 0 && bootRank <= 5) p.push_back("BOOT RACE #" + std::to_string(bootRank));
    if (astRank == 1) p.push_back("ASSISTS LEADER");
    else if (astRank > 0 && astRank <= 3) p.push_back("ASSISTS RACE #" + std::to_string(astRank));
    if (mvpRank == 1) p.push_back("MVP FAVOURITE");
    else if (mvpRank > 0 && mvpRank <= 3) p.push_back("MVP RACE #" + std::to_string(mvpRank));
    if (royRank == 1) p.push_back("TOP ROOKIE");
    if (l.contrib >= 0.5 && L == 0 && w >= 3) p.push_back("UNBEATEN CLUB");
    else if (l.contrib >= 0.5 && w + L >= 4 && w >= 3 * L) p.push_back("CLUB " + std::to_string(w) + "-" + std::to_string(L));
    if (champ && l.contrib >= 0.2) p.push_back("CHAMPION");
    if (l.wkGoals >= 2) p.push_back(std::to_string(l.wkGoals) + " THIS MATCHDAY");
    else if (l.wkAst >= 2) p.push_back(std::to_string(l.wkAst) + " ASSISTS THIS MATCHDAY");  // r232
    else if (l.fast >= 0.25) p.push_back("ON FIRE");
    else if (l.fast <= -0.2) p.push_back("COLD SPELL");
  }
  if (l.mvps > 0) p.push_back(l.mvps > 1 ? std::to_string(l.mvps) + "X MVP" : "MVP");
  if (l.boots > 0) p.push_back(l.boots > 1 ? std::to_string(l.boots) + "X GOLDEN BOOT" : "GOLDEN BOOT WINNER");
  if (l.pms > 0) p.push_back(l.pms > 1 ? std::to_string(l.pms) + "X PLAYMAKER OF THE YEAR" : "PLAYMAKER OF THE YEAR");  // r232
  if (l.titles > 0) p.push_back(l.titles > 1 ? std::to_string(l.titles) + "X CHAMPION" : "TITLE WINNER");
  std::string s;
  for (size_t k = 0; k < p.size() && k < 4; ++k) s += (k ? " - " : "") + p[k];
  return s;
}

// r230: a CPU-made copy of an EXQUISITE card (a listing, a sale on the wire, a league collector's pull) made a real one: its tier from what
// the LANCINI TIZM roll gave (BLACK -> MASTERPIECE when its 1/1 is free, else SAPPHIRE; GOLD -> GOLD LEAF / EMERALD / SAPPHIRE; SILVER ->
// SILVER LEAF; a case hit -> ON-CARD AUTO / PATCH / an RPA for a rookie; else GALLERY), a number of its run no one has (yours, a pack waiting,
// a collector's - g_src.serial_taken - another listing). False: none free (leave it out)
bool exq_fill(Variant* v, uint64_t h, const std::vector<Variant>& also) {
  if (!exq_v(*v)) return true;
  int t = xrexq::kGallery;
  const double u = u01(h ^ 0xE1);
  if (v->parallel == kBlack) t = (g_src.black_taken != nullptr && g_src.black_taken(v->card)) ? xrexq::kSapphire : xrexq::kMasterpiece;
  else if (v->parallel == kGoldTizm) t = u < 0.62 ? xrexq::kGoldLeaf : (u < 0.9 ? xrexq::kEmerald : xrexq::kSapphire);
  else if (v->hit != kHitNone) t = exq_rookie_v(*v) && u < 0.3 ? xrexq::kRpa : (u < 0.62 ? xrexq::kAuto : xrexq::kPatch);
  else if (v->parallel == kSilver) t = xrexq::kSilverLeaf;
  for (; t > 0; t = xrexq::down_in_slot(t, xrexq::hit_tier(t))) {
    const int run = xrexq::kRun[t];
    const int start = static_cast<int>(mix64(h ^ 0xE2) % static_cast<uint64_t>(run));
    for (int k = 0; k < run; ++k) {
      const int s = xrexq::serial_of(t, 1 + (start + k) % run);
      bool taken = g_src.serial_taken != nullptr && g_src.serial_taken(v->card, s);
      for (const Holding& hd : g_hold) taken = taken || (hd.card == v->card && hd.serial == s);
      for (const Variant& a : also) taken = taken || (a.card == v->card && (a.serial == s || (t == xrexq::kMasterpiece && xrexq::tier_of(a.serial) == t)));
      if (taken) continue;
      v->serial = s;
      v->parallel = xrexq::kClass[t];
      v->hit = xrexq::kSigned[t] ? kHitSigned : kHitNone;
      v->shiny = false;
      return true;
    }
  }
  return false;
}
// the matchday's listings: 8 copies collectors put up (half from the hottest lines), deterministic from the career + day
void make_listings(int dayIndex) {
  g_mk.listings.clear();
  if (g_set.empty()) return;
  std::vector<int> here;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].here) here.push_back(static_cast<int>(i));
  if (here.empty()) return;
  std::vector<std::pair<double, int>> hot;
  for (int c : here) {
    const Line* l = line_of(c);
    const int self = g_set[static_cast<size_t>(c)].set >= 1 && g_set[static_cast<size_t>(c)].base >= 0 ? g_set[static_cast<size_t>(c)].base : c;  // (r217; r230)
    if (l != nullptr && l->activeCard == self && l->hist.size() >= 2) hot.push_back({l->hist.back() / std::max(0.05f, l->hist[l->hist.size() - 2]) + l->hist.back() * 0.1, c});
  }
  std::stable_sort(hot.begin(), hot.end(), [](const std::pair<double, int>& a, const std::pair<double, int>& b) { return a.first > b.first; });
  const uint64_t base = mix64(static_cast<uint64_t>(g_mk.career) * 0x51AFD7ED558CCD1Dull + static_cast<uint64_t>(dayIndex + 7777));
  for (int i = 0; i < kListings; ++i) {
    const uint64_t h = mix64(base + static_cast<uint64_t>(i) * 0x9E3779B97F4A7C15ull);
    Listing L;
    int c;
    if (i % 2 == 0 && !hot.empty()) {
      c = hot[static_cast<size_t>(std::min<double>(hot.size() - 1, std::floor(u01(h ^ 1) * std::min<size_t>(12, hot.size()))))].second;
      // r239 (Lance 11-13: "i just evolved togepi into togetic and now i cant find any togepi on the market from collectors, just togetic"):
      // the hot half picks the FAMILY (r238: only the form it plays as now got in - the earlier forms vanished from it); its copy is any
      // form of that family in the same set, evenly (his rookie TOGEPI as often as his TOGETIC / TOGEKISS)
      const CardInfo& hc = g_set[static_cast<size_t>(c)];
      auto fl = g_byLine.find(hc.line);
      if (!hc.line.empty() && fl != g_byLine.end()) {
        std::vector<int> forms;
        for (int f : fl->second)
          if (g_set[static_cast<size_t>(f)].here && g_set[static_cast<size_t>(f)].set == hc.set) forms.push_back(f);
        if (forms.size() > 1) c = forms[static_cast<size_t>(std::min<double>(forms.size() - 1, std::floor(u01(h ^ 0x239) * forms.size())))];
      }
    } else {
      c = here[static_cast<size_t>(std::min<double>(here.size() - 1, std::floor(u01(h ^ 2) * here.size())))];
    }
    L.v.card = c;
    const double u = u01(h ^ 3);
    L.v.parallel = u < 0.62 ? kBase : (u < 0.86 ? kSilver : (u < 0.95 ? kGoldTizm : kBase));
    if (u >= 0.95) L.v.hit = 1 + static_cast<int>(u01(h ^ 4) * 3.0) % 3;
    if (g_set[static_cast<size_t>(c)].key.compare(0, 3, "pk_") == 0 && u01(h ^ 5) < 0.06) L.v.shiny = true;
    if (u01(h ^ 6) < 0.15) {  // a slab
      int roll = static_cast<int>(u01(h ^ 7) * 1000.0), g = 10;
      for (int k = 1; k <= 10; ++k) {
        roll -= kGradeOdds[k];
        if (roll < 0) {
          g = k;
          break;
        }
      }
      L.v.grade = std::max(6, g);
      L.v.cert = 30000000u + static_cast<uint32_t>(mix64(h ^ 8) % 9000000u);
    }
    if (L.v.parallel == kGoldTizm) {  // a number you don't have
      int pick = 1 + static_cast<int>(u01(h ^ 9) * 10.0) % 10;
      bool found = false;
      for (int k = 0; k < 10 && !found; ++k) {
        Variant t = L.v;
        t.serial = 1 + (pick - 1 + k) % 10;
        t.grade = 0;
        t.cert = 0;
        bool mine = false;
        for (const Holding& hd : g_hold) mine = mine || (hd.card == c && hd.parallel == kGoldTizm && hd.serial == t.serial);
        if (!mine) {
          L.v.serial = t.serial;
          found = true;
        }
      }
      if (!found) {
        L.v.parallel = kBase;
        L.v.serial = 0;
      }
    }
    {  // r230: an EXQUISITE card's: a real number (never a MASTERPIECE: the 1/1s live in the collectors' world)
      std::vector<Variant> also;
      for (const Listing& o : g_mk.listings) also.push_back(o.v);
      if (exq_v(L.v) && L.v.parallel == kBlack) L.v.parallel = kGoldTizm;
      if (!exq_fill(&L.v, h ^ 0x4A11, also)) continue;
    }
    L.seller = "THE DEALER";  // r235: the market's own stock (the twenty collectors list their own copies: xr_trade, the search)
    g_mk.listings.push_back(L);
  }
}

double line_heat(int card) {  // r231: its family's level now (-1.6 .. 2.75: how wanted its player is)
  const Line* l = line_of(card);
  return l != nullptr ? line_level(*l) : 0.0;
}
// r238: tick > 0 - a real-time tick's (its own rolls, chance x chanceK, 1 new offer at most; the open ones not aged: a matchday does that)
void make_offers(int tick = 0, double chanceK = 1.0) {
  // the open ones: a day older (a bidding war: the top bid climbs toward what they'd really pay), capped to the market now,
  // gone when they expire or you no longer have the card
  for (size_t i = 0; i < g_mk.offers.size();) {
    Offer& o = g_mk.offers[i];
    const long long p = price_i(o.v, -1);
    bool gone = o.listing != 0 ? true : !own_free(o.v);  // (r238: a watcher's offer stands while its listing does)
    if (o.listing != 0)
      for (const Mine& m : g_mk.mine) gone = gone && m.id != o.listing;
    if (g_mk.steps > o.expires || gone) {  // (r210: a copy now listed by you draws no offers)
      g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
      continue;
    }
    o.reserve = std::min(o.reserve, static_cast<long long>(std::floor(static_cast<double>(p) * kOfferMax)));
    // r238: never under what SELL NOW pays today - an offer made before the card rose comes up to it (or goes, if he'd never pay that)
    if (o.reserve <= bid_of(p)) {
      g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
      continue;
    }
    o.amount = std::max(o.amount, bid_of(p) + 1);
    if (tick == 0 && o.bidders > 1 && !o.final) {  // (r238: a matchday's - a tick only clears the stale ones)
      const long long was = o.amount;
      o.amount = std::min(o.reserve, o.amount + std::max(1LL, (o.reserve - o.amount) / 2));
      if (o.amount > was) headline("BIDDING WAR: " + std::to_string(o.bidders) + " COLLECTORS ON YOUR " + name_of_card(o.v.card) + " - TOP BID " + coins_text(o.amount), 0xFFB020, false);
    }
    o.amount = std::min(o.amount, o.reserve);
    ++i;
  }
  int made = 0;
  std::vector<std::pair<double, size_t>> cands;
  for (size_t i = 0; i < g_hold.size(); ++i) {
    const Holding& h = g_hold[i];
    if (h.grading) continue;
    const std::string s = sig_of(h);
    auto fs = g_mk.firstSeen.find(s);
    if (fs == g_mk.firstSeen.end() || fs->second > g_mk.steps - 2) continue;  // (held a whole matchday first)
    // r238: the roll first, the binder-wide checks (own_free: every copy's sig) only for a copy it picks - the same picks, ~30x less work
    // (a real-time tick runs this every 90 s; r201-r237 checked every copy against the whole binder first: 33 ms in the harness)
    const long long p = price_i(h, -1);
    if (p < 500) continue;  // (nobody chases bulk: r210's scale - r201 was 20 coins)
    const double heat = line_heat(h.card);
    // r210: demand + the market's mood count too - r231: the league's top performers draw the big collectors
    const double chance = 0.06 + 0.10 * clampd(heat, 0.0, 2.0) + (h.parallel > 0 ? 0.08 : 0.0) + (h.grade >= 9 ? 0.08 : 0.0) + (h.hit ? 0.08 : 0.0) +
                          0.04 * (tier_of(h.card) - 2) + (p >= 250000 ? 0.15 : 0.0) + 0.1 * clampd(g_mk.cycle, -0.5, 0.5);
    const double u = u01(hstr(s) ^ (static_cast<uint64_t>(g_mk.career) << 7) ^ static_cast<uint64_t>(g_mk.steps) * 0x2545F4914F6CDD1Dull ^
                         static_cast<uint64_t>(tick) * 0x5851F42D4C957F2Dull);
    if (!(u < chance * chanceK)) continue;
    bool open = false;
    for (const Offer& o : g_mk.offers) open = open || sig_of(o.v) == s;
    if (open || !own_free(h)) continue;  // (r210: not on a copy you've listed)
    cands.push_back({u - chance * chanceK, i});
  }
  std::sort(cands.begin(), cands.end());
  for (const auto& cd : cands) {
    if (made >= (tick > 0 ? 1 : 3) || static_cast<int>(g_mk.offers.size()) >= kMaxOffers) break;
    const Holding& h = g_hold[cd.second];
    const uint64_t k = hstr(sig_of(h)) ^ static_cast<uint64_t>(g_mk.steps) * 0x9E3779B97F4A7C15ull ^ g_mk.career ^ static_cast<uint64_t>(tick) * 0xD1B54A32D192ED03ull;
    const long long p = price_i(h, -1);
    const double pd = static_cast<double>(p);
    Offer o;
    o.id = g_mk.nextId++;
    o.v = h;
    o.reserve = std::max(1LL, static_cast<long long>(std::floor(pd * std::min(kOfferMax, 0.97 + 0.11 * u01(k ^ 12)))));
    // r238: never under what SELL NOW pays (90%): an offer opens at 92 - 104% of the market (r201-r237: 86 - 104%, under r238's SELL NOW)
    o.amount = std::min(o.reserve, std::max(bid_of(p) + 1, static_cast<long long>(std::floor(pd * (0.92 + 0.12 * u01(k ^ 13))))));
    o.who = pick_buyer(k ^ 11, o.reserve);  // r235: (a named collector who can afford it, mostly)
    // a bidding war: a hot line, or (r210) a grail - r231: the TOP 5 of the race - the rich collectors all want it
    const bool grail = p >= 250000 || tier_of(h.card) >= 5;
    o.bidders = line_heat(h.card) > 0.8 || grail ? 2 + (u01(k ^ 14) < 0.4 ? 1 : 0) + (grail && u01(k ^ 15) < 0.5 ? 1 : 0) : 1;
    o.day = g_mk.steps;
    o.expires = g_mk.steps + 2;
    g_mk.offers.push_back(o);
    if (tick > 0) notice("AN OFFER", copy_words(h), o.who + " OFFERS " + coins_text(o.amount) + " - MARKET > OFFERS");  // r238: (in the hub: its pop-up)
    if (p >= 1000000)
      headline(o.who + " BIDS " + coins_text(o.amount) + " COINS FOR YOUR " + (exq_v(h) ? std::string(xrexq::short_name(exq_t(h))) : upper(kParName[h.parallel])) + " " + name_of_card(h.card) + (o.bidders > 1 ? " - A BIDDING WAR" : ""), 0xFFD23F, false);
    ++made;
  }
}

// r210: the collector world's own big sales this matchday - a grail changing hands somewhere (deterministic from the career + the day):
// the biggest ever is THE RECORD (headlined, its character's cards buzzing a while: "record-sale buzz")
void world_sales(int dayIndex) {
  if (g_set.empty()) return;
  const uint64_t base = mix64(static_cast<uint64_t>(g_mk.career) * 0x3C6EF372FE94F82Bull ^ static_cast<uint64_t>(static_cast<int64_t>(dayIndex) + 9191));
  // r231: a card of the league's TOP 60 (the TOP 5 most of the time: the Golden Boot / MVP race's cards make the records), its copy's
  // kind, a price round the market (an auction: 85% - 140%)
  std::vector<int> pool;
  for (size_t i = 0; i < g_set.size(); ++i) {
    const int t = tier_of(static_cast<int>(i));
    if (g_set[i].here && t >= 3)  // (r217: a JAM '96 card on the wire 1.5x as often - the hobby's most coveted)
      for (int k = 0; k < (t == 5 ? 12 : (t == 4 ? 4 : 2)) * (g_set[i].set == 1 ? 3 : 2) / 2; ++k) pool.push_back(static_cast<int>(i));
  }
  if (pool.empty()) return;
  const int sales = 1 + (u01(base ^ 1) < 0.35 ? 1 : 0);
  for (int n = 0; n < sales; ++n) {
    const uint64_t h = mix64(base + static_cast<uint64_t>(n) * 0x9E3779B97F4A7C15ull);
    Variant v;
    v.card = pool[static_cast<size_t>(mix64(h ^ 2) % pool.size())];
    const double u = u01(h ^ 3);
    if (u < 0.22) v.parallel = kBlack, v.serial = 1;
    else if (u < 0.62) v.parallel = kGoldTizm, v.serial = 1 + static_cast<int>(mix64(h ^ 4) % 10);
    else if (u < 0.80) v.parallel = kSilver;
    // r214 (bug since r210): one BLACK 1/1 per card ever - when this career's 1/1 of it is taken (yours, or a collector's), the sale
    // on the wire is a GOLD /10 instead (its price, its words and the record all from that)
    if (v.parallel == kBlack && g_src.black_taken != nullptr && g_src.black_taken(v.card))
      v.parallel = kGoldTizm, v.serial = 1 + static_cast<int>(mix64(h ^ 4) % 10);
    if (u01(h ^ 5) < 0.3) v.hit = 1 + static_cast<int>(mix64(h ^ 6) % 3);
    if (u01(h ^ 7) < 0.45) v.grade = 8 + static_cast<int>(mix64(h ^ 8) % 3);
    if (exq_v(v) && !exq_fill(&v, h ^ 0x5A1E, {})) continue;  // r230: an EXQUISITE sale: a number no one in this career has
    const long long amt = xrcoin::from_double(price_d(v, -1) * (0.85 + 0.55 * u01(h ^ 9)));
    if (amt < 100000) continue;  // (only the big ones make the wire)
    std::string what = (v.parallel == kBlack ? "BLACK 1/1 " : (v.parallel == kGoldTizm ? "GOLD /10 " : (v.parallel == kSilver ? "SILVER " : ""))) + name_of_card(v.card);
    if (v.hit == kHitSigned) what += " AUTO";
    else if (v.hit == kHitBoom) what += " TIZM BOOM";
    else if (v.hit == kHitHome) what += " HOMETOWN";
    if (exq_v(v)) what = name_of_card(v.card) + " " + xrexq::words(v.serial, exq_rookie_v(v));  // r230: "EXQUISITE CHARIZARD SAPPHIRE 03/10"
    if (v.grade > 0) what += ", GRADED " + std::to_string(v.grade);
    if (g_mk.record == 0) {  // (the standing record before this career's first sale: a BLACK 1/1 AUTO of the league's #1, graded 10)
      // r231: whoever leads THIS league's TOP CARDS race (r210-r230: always CHARIZARD - a popularity pick)
      std::string topKey = "pk_charizard";
      for (const auto& kv : g_mk.lines)
        if (kv.second.rank == 1 && kv.second.activeCard >= 0) {
          const CardInfo& tc = g_set[static_cast<size_t>(kv.second.activeCard)];
          topKey = tc.key;
        }
      Variant rv;
      rv.card = card_of_key(topKey) >= 0 ? card_of_key(topKey) : v.card;
      // r217: the hobby's top grail is a JAM '96 BLACK 1/1 AUTO (its A 1/1 if the Jam one's taken in this career, then a GOLD)
      {
        auto j = g_byKey.find(topKey + "_j96");
        if (j != g_byKey.end() && !(g_src.black_taken != nullptr && g_src.black_taken(j->second))) rv.card = j->second;
      }
      rv.parallel = kBlack, rv.serial = 1, rv.hit = kHitSigned, rv.grade = 10;
      {  // r230: now the LANCINI EXQUISITE MASTERPIECE 1/1 PATCH AUTO (its Jam / A 1/1 when it's taken in this career; not in the set: those)
        auto x = g_byKey.find(topKey + xrexq::kSuffix);
        if (x != g_byKey.end() && !(g_src.black_taken != nullptr && g_src.black_taken(x->second))) rv.card = x->second, rv.serial = xrexq::serial_of(xrexq::kMasterpiece, 1);
      }
      // r214: (its 1/1 taken in this career - yours or a collector's: the record a GOLD /10 of it)
      const bool gold = g_src.black_taken != nullptr && g_src.black_taken(rv.card);
      if (gold) rv.parallel = kGoldTizm;
      g_mk.record = xrcoin::from_double(price_d(rv, -1) * 1.1);
      g_mk.recordText = std::string("THE RECORD: A ") + (gold ? "GOLD /10 " : "BLACK 1/1 ") + name_of_card(rv.card) + " AUTO, GRADED 10 - " +
                        coins_text(g_mk.record) + " COINS";
      if (exq_v(rv) && !gold)  // r230
        g_mk.recordText = "THE RECORD: A " + name_of_card(rv.card) + " MASTERPIECE 1/1 PATCH AUTO, GRADED 10 - " + coins_text(g_mk.record) + " COINS";
    }
    const bool record = amt > g_mk.record;
    {  // r238: (its SALES history: two of the league's collectors)
      const uint64_t a = mix64(h ^ 21) % 24, b = (a + 1 + mix64(h ^ 22) % 23) % 24;
      record_locked(v, amt, kWho[a], kWho[b], 0);
    }
    std::string t = "A " + what + " JUST SOLD FOR " + coins_text(amt) + " COINS";
    // r231: the play behind it ("... COINS - 14 GOALS - GOLDEN BOOT LEADER"); no record-sale "buzz" on its price any more (play only)
    if (const Line* rl = line_of(v.card))
      if (!rl->reason.empty()) t += " - " + rl->reason.substr(0, rl->reason.find(" - ", rl->reason.find(" - ") + 3));
    if (record) {
      g_mk.record = amt;
      g_mk.recordText = t;
      t += " - A NEW RECORD!";
    }
    headline(t, record ? 0xFFD23F : 0xFFB020, record);  // (a new record: the league news too)
  }
}

// ---------------------------------------------------------------------------------------- the file
void save_market() {  // (g_m held)
  if (g_mk.career == 0) return;
  const std::string path = market_path(g_mk.career), tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) return;
  std::fprintf(f, "market201 1\n");
  std::fprintf(f, "V\t231\n");  // r210: prices at the r210 scale (an older file: its offers dropped, its portfolio chart rescaled at load); r231: stats-driven
  std::fprintf(f, "step\t%d\t%d\t%d\t%u\n", g_mk.steps, g_mk.season, g_mk.week, g_mk.nextId);
  std::fprintf(f, "days");
  for (int d : g_mk.days) std::fprintf(f, "\t%d", d);
  std::fprintf(f, "\n");
  for (const auto& kv : g_mk.lines) {
    const Line& l = kv.second;
    const std::string act = l.activeCard >= 0 ? g_set[static_cast<size_t>(l.activeCard)].key : "-";
    std::fprintf(f, "L\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%.5f\t%.5f\t%.5f\t%d\t%d\t%d\t%s\t", l.id.c_str(), act.c_str(), l.level, l.regGoals,
                 l.seasonGoals, l.careerGoals, l.streak, l.form, l.club, l.role, l.heat, l.shock, l.legacy, l.retired ? 1 : 0, l.retiredSeason,
                 l.seen ? 1 : 0, l.why.empty() ? "-" : l.why.c_str());
    for (size_t i = 0; i < l.hist.size(); ++i) std::fprintf(f, "%s%.4f", i ? "," : "", l.hist[i]);
    std::fprintf(f, "\n");
    // r231: its form / legacy counts / race place (an r230 build ignores the line)
    std::fprintf(f, "D\t%s\t%.5f\t%.5f\t%.4f\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\n", l.id.c_str(), l.fast, l.prior, l.contrib, l.boots, l.mvps, l.roys, l.titles,
                 l.rank, l.prevRank, l.lastSeason, l.wkGoals, l.reason.empty() ? "-" : l.reason.c_str());
    // r232: its assists (an r231 build ignores the line)
    if (l.astSeen) std::fprintf(f, "A\t%s\t%d\t%d\t%d\t%d\t%d\n", l.id.c_str(), l.regAst, l.seasonAst, l.careerAst, l.wkAst, l.pms);
  }
  std::fprintf(f, "E\t%d\t%d\t%d\t%d\t%.5f\t%d\t%d\n", g_mk.phase, g_mk.awardsSeason, g_mk.champSeason, g_mk.recordGoals, g_mk.starsWas, g_mk.seasonBest,
               g_mk.recordSeason);  // r231
  auto vtext = [&](const Variant& v) {
    char b[200];
    std::snprintf(b, sizeof(b), "%s\t%d\t%d\t%d\t%d\t%d\t%u", v.card >= 0 ? g_set[static_cast<size_t>(v.card)].key.c_str() : "-", v.parallel, v.shiny ? 1 : 0,
                  v.hit, v.serial, v.grade, v.cert);
    return std::string(b);
  };
  for (const Offer& o : g_mk.offers)  // (r238: + its listing's id - an older build reads the first 16 fields)
    std::fprintf(f, "O\t%u\t%s\t%s\t%lld\t%lld\t%d\t%d\t%d\t%d\t%u\n", o.id, vtext(o.v).c_str(), o.who.c_str(), o.amount, o.reserve, o.bidders, o.day, o.expires,
                 o.final ? 1 : 0, o.listing);
  std::fprintf(f, "K\t%d\t%d\n", g_mk.tickN, g_mk.ticks);  // r238: the real-time ticks (an older build ignores the line)
  // r210: your listings, your sales, the market's mood, the record
  for (const Mine& m : g_mk.mine) std::fprintf(f, "M\t%u\t%s\t%lld\t%d\t%d\t%d\n", m.id, vtext(m.v).c_str(), m.ask, m.listed, m.views, m.watching);
  for (auto it = g_mk.sales.rbegin(); it != g_mk.sales.rend(); ++it)
    std::fprintf(f, "H\t%s\t%s\t%lld\t%lld\t%d\t%d\n", vtext(it->v).c_str(), it->buyer.c_str(), it->amount, it->market, it->day, it->surprise ? 1 : 0);
  std::fprintf(f, "C\t%.5f\t%d", g_mk.cycle, g_mk.unseenSales);
  for (double c : g_mk.cycleHist) std::fprintf(f, "\t%.5f", c);
  std::fprintf(f, "\n");
  if (g_mk.record > 0) std::fprintf(f, "R\t%lld\t%s\n", g_mk.record, g_mk.recordText.c_str());
  for (const Listing& L : g_mk.listings) std::fprintf(f, "S\t%s\t%s\t%d\n", vtext(L.v).c_str(), L.seller.c_str(), L.sold ? 1 : 0);
  for (const auto& kv : g_mk.firstSeen) std::fprintf(f, "F\t%s\t%d\n", kv.first.c_str(), kv.second);
  std::fprintf(f, "P");
  for (long long v : g_mk.portfolio) std::fprintf(f, "\t%lld", v);
  std::fprintf(f, "\n");
  for (auto it = g_mk.news.rbegin(); it != g_mk.news.rend(); ++it) std::fprintf(f, "N\t%d\t%u\t%s\n", it->day, it->rgb, it->text.c_str());
  std::fprintf(f, "T\t%lld\t%lld\t%d\t%d\n", g_mk.spent, g_mk.received, g_mk.bought, g_mk.sold);
  // r235: your watch list, your recent searches, what you paid lately (the flip rule) - an older build ignores these lines
  for (const std::string& w : g_watch) std::fprintf(f, "W\t%s\n", w.c_str());
  for (auto it = g_recent.rbegin(); it != g_recent.rend(); ++it) std::fprintf(f, "Q\t%s\n", it->c_str());
  for (const auto& kv : g_paid)
    if (kv.second.step >= g_mk.steps - 1) std::fprintf(f, "Y\t%s\t%lld\t%d\n", kv.first.c_str(), kv.second.coins, kv.second.step);
  std::fclose(f);
  std::rename(tmp.c_str(), path.c_str());
  // r238: the SALES history beside it (its own file: written only when a sale came in - never the market file's size)
  if (g_histDirty) {
    g_histDirty = false;
    const std::string sp = home_dir() + "/cards/sales-" + std::to_string(g_mk.career) + ".txt", st = sp + ".tmp";
    if (FILE* s = std::fopen(st.c_str(), "w")) {
      std::fprintf(s, "sales238 1\n");
      for (const auto& kv : g_hist)
        for (auto it = kv.second.rbegin(); it != kv.second.rend(); ++it)
          std::fprintf(s, "Z\t%u\t%d\t%d\t%d\t%d\t%lld\t%s\t%s\t%s\n", it->seq, it->step, it->season, it->week, it->kind, it->price, it->seller.c_str(), it->buyer.c_str(),
                       vtext(it->v).c_str());
      std::fclose(s);
      std::rename(st.c_str(), sp.c_str());
    }
  }
}
std::vector<std::string> split_tabs(const std::string& s) {
  std::vector<std::string> out;
  size_t a = 0;
  for (;;) {
    const size_t b = s.find('\t', a);
    out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return out;
}
bool read_variant(const std::vector<std::string>& t, size_t at, Variant* v) {
  if (t.size() < at + 7) return false;
  auto it = g_byKey.find(t[at]);
  if (it == g_byKey.end()) return false;
  v->card = it->second;
  v->parallel = std::max(0, std::min(3, std::atoi(t[at + 1].c_str())));
  v->shiny = std::atoi(t[at + 2].c_str()) != 0;
  v->hit = std::max(0, std::min(3, std::atoi(t[at + 3].c_str())));
  v->serial = std::atoi(t[at + 4].c_str());
  v->grade = std::max(0, std::min(10, std::atoi(t[at + 5].c_str())));
  v->cert = static_cast<uint32_t>(std::strtoul(t[at + 6].c_str(), nullptr, 10));
  return true;
}
bool load_market(unsigned career) {  // (g_m held)
  g_mk = Market();
  g_mk.career = career;
  // r235: the search's own (this career's: read below)
  g_paid.clear(), g_recent.clear(), g_watch.clear(), g_alerted.clear(), g_rows.clear();
  g_unseenAlerts = 0, g_rowsDirty = true, g_q.clear(), g_kb = false, g_kbOpen = false;
  if (g_offCareer != career) g_offs.clear(), g_buyers.clear();
  // r238: this career's SALES history (its own file; none: a career from before r238 starts it now), its pop-ups, the SALES tab
  g_hist.clear(), g_histN = 0, g_histSeq = 0, g_histDirty = false, g_notices.clear();
  g_salesCard = -1, g_salesGroup = 0, g_salesRows.clear(), g_salesDirty = true, g_liveAcc = 0.0;
  g_salesForm = -1;  // r239
  if (FILE* s = std::fopen((home_dir() + "/cards/sales-" + std::to_string(career) + ".txt").c_str(), "r")) {
    std::string all;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), s)) > 0) all.append(buf, n);
    std::fclose(s);
    size_t a = 0;
    while (a < all.size()) {
      size_t b = all.find('\n', a);
      if (b == std::string::npos) b = all.size();
      const std::vector<std::string> t = split_tabs(all.substr(a, b - a));
      a = b + 1;
      if (t.size() < 16 || t[0] != "Z") continue;
      SaleRec r;
      if (!read_variant(t, 9, &r.v)) continue;
      r.seq = static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10));
      r.step = std::atoi(t[2].c_str()), r.season = std::atoi(t[3].c_str()), r.week = std::atoi(t[4].c_str()), r.kind = std::atoi(t[5].c_str());
      xrcoin::parse(t[6].c_str(), &r.price);
      r.seller = t[7], r.buyer = t[8];
      if (r.price < 1) continue;
      const int fam = r.v.card >= 0 && g_set[static_cast<size_t>(r.v.card)].set >= 1 && g_set[static_cast<size_t>(r.v.card)].base >= 0 ? g_set[static_cast<size_t>(r.v.card)].base : r.v.card;
      g_hist[fam].insert(g_hist[fam].begin(), r);  // (the file oldest first per player: newest first in memory)
      g_histSeq = std::max(g_histSeq, r.seq);
      ++g_histN;
    }
    std::vector<int> fams;
    for (const auto& kv : g_hist) fams.push_back(kv.first);
    for (int fm : fams) hist_trim(fm);  // (bounded, whatever the file holds)
  }
  FILE* f = std::fopen(market_path(career).c_str(), "r");
  if (f == nullptr) return false;
  std::string all;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
  std::fclose(f);
  size_t a = 0;
  while (a < all.size()) {
    size_t b = all.find('\n', a);
    if (b == std::string::npos) b = all.size();
    const std::string line = all.substr(a, b - a);
    a = b + 1;
    const std::vector<std::string> t = split_tabs(line);
    if (t.empty()) continue;
    if (t[0] == "step" && t.size() >= 5) {
      g_mk.steps = std::atoi(t[1].c_str());
      g_mk.season = std::atoi(t[2].c_str());
      g_mk.week = std::atoi(t[3].c_str());
      g_mk.nextId = static_cast<uint32_t>(std::strtoul(t[4].c_str(), nullptr, 10));
    } else if (t[0] == "days") {
      for (size_t i = 1; i < t.size(); ++i) g_mk.days.push_back(std::atoi(t[i].c_str()));
    } else if (t[0] == "L" && t.size() >= 19) {
      Line l;
      l.id = t[1];
      auto it = g_byKey.find(t[2]);
      l.activeCard = it != g_byKey.end() ? it->second : -1;
      l.level = std::atoi(t[3].c_str());
      l.regGoals = std::atoi(t[4].c_str());
      l.seasonGoals = std::atoi(t[5].c_str());
      l.careerGoals = std::atoi(t[6].c_str());
      l.streak = std::atoi(t[7].c_str());
      l.form = std::atoi(t[8].c_str());
      l.club = std::atoi(t[9].c_str());
      l.role = std::atoi(t[10].c_str());
      l.heat = std::atof(t[11].c_str());
      l.shock = std::atof(t[12].c_str());
      l.legacy = std::atof(t[13].c_str());
      l.retired = std::atoi(t[14].c_str()) != 0;
      l.retiredSeason = std::atoi(t[15].c_str());
      l.seen = std::atoi(t[16].c_str()) != 0;
      l.why = t[17] == "-" ? std::string() : t[17];
      const std::string& h = t[18];
      size_t p = 0;
      while (p < h.size()) {
        size_t q = h.find(',', p);
        if (q == std::string::npos) q = h.size();
        l.hist.push_back(static_cast<float>(std::atof(h.substr(p, q - p).c_str())));
        p = q + 1;
      }
      if (std::isfinite(l.heat) && std::isfinite(l.shock) && std::isfinite(l.legacy)) g_mk.lines[l.id] = l;
    } else if (t[0] == "O" && t.size() >= 16) {
      Offer o;
      o.id = static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10));
      if (!read_variant(t, 2, &o.v)) continue;
      o.who = t[9];
      xrcoin::parse(t[10].c_str(), &o.amount);  // (r210: 64-bit; an r201-r209 file's numbers read the same)
      xrcoin::parse(t[11].c_str(), &o.reserve);
      o.bidders = std::atoi(t[12].c_str());
      o.day = std::atoi(t[13].c_str());
      o.expires = std::atoi(t[14].c_str());
      o.final = std::atoi(t[15].c_str()) != 0;
      if (t.size() >= 17) o.listing = static_cast<uint32_t>(std::strtoul(t[16].c_str(), nullptr, 10));  // r238
      g_mk.offers.push_back(o);
    } else if (t[0] == "K" && t.size() >= 3) {  // r238: the real-time ticks
      g_mk.tickN = std::max(0, std::atoi(t[1].c_str()));
      g_mk.ticks = std::max(0, std::atoi(t[2].c_str()));
    } else if (t[0] == "S" && t.size() >= 10) {
      Listing L;
      if (!read_variant(t, 1, &L.v)) continue;
      L.seller = t[8];
      L.sold = std::atoi(t[9].c_str()) != 0;
      g_mk.listings.push_back(L);
    } else if (t[0] == "F" && t.size() >= 3) {
      g_mk.firstSeen[t[1]] = std::atoi(t[2].c_str());
    } else if (t[0] == "P") {
      for (size_t i = 1; i < t.size(); ++i) g_mk.portfolio.push_back(std::atoll(t[i].c_str()));
    } else if (t[0] == "N" && t.size() >= 4) {
      Headline h;
      h.day = std::atoi(t[1].c_str());
      h.rgb = static_cast<uint32_t>(std::strtoul(t[2].c_str(), nullptr, 10));
      h.text = t[3];
      g_mk.news.insert(g_mk.news.begin(), h);
    } else if (t[0] == "T" && t.size() >= 5) {
      g_mk.spent = std::atoll(t[1].c_str());
      g_mk.received = std::atoll(t[2].c_str());
      g_mk.bought = std::atoi(t[3].c_str());
      g_mk.sold = std::atoi(t[4].c_str());
    } else if (t[0] == "V" && t.size() >= 2) {  // r210
      g_mk.scale210 = std::atoi(t[1].c_str()) >= 210;
      g_mk.v231 = std::atoi(t[1].c_str()) >= 231;
    } else if (t[0] == "D" && t.size() >= 14) {  // r231 (after its L line)
      auto it = g_mk.lines.find(t[1]);
      if (it == g_mk.lines.end()) continue;
      Line& l = it->second;
      l.fast = std::atof(t[2].c_str());
      l.prior = std::atof(t[3].c_str());
      l.contrib = std::atof(t[4].c_str());
      l.boots = std::atoi(t[5].c_str());
      l.mvps = std::atoi(t[6].c_str());
      l.roys = std::atoi(t[7].c_str());
      l.titles = std::atoi(t[8].c_str());
      l.rank = std::atoi(t[9].c_str());
      l.prevRank = std::atoi(t[10].c_str());
      l.lastSeason = std::atoi(t[11].c_str());
      l.wkGoals = std::atoi(t[12].c_str());
      l.reason = t[13] == "-" ? std::string() : t[13];
      if (!std::isfinite(l.fast)) l.fast = 0.0;
      if (!std::isfinite(l.prior)) l.prior = 0.0;
    } else if (t[0] == "A" && t.size() >= 7) {  // r232 (after its L line)
      auto it = g_mk.lines.find(t[1]);
      if (it == g_mk.lines.end()) continue;
      Line& l = it->second;
      l.regAst = std::max(0, std::atoi(t[2].c_str()));
      l.seasonAst = std::max(0, std::atoi(t[3].c_str()));
      l.careerAst = std::max(0, std::atoi(t[4].c_str()));
      l.wkAst = std::max(0, std::atoi(t[5].c_str()));
      l.pms = std::max(0, std::atoi(t[6].c_str()));
      l.astSeen = true;
    } else if (t[0] == "E" && t.size() >= 6) {  // r231
      g_mk.phase = std::atoi(t[1].c_str());
      g_mk.awardsSeason = std::atoi(t[2].c_str());
      g_mk.champSeason = std::atoi(t[3].c_str());
      g_mk.recordGoals = std::atoi(t[4].c_str());
      g_mk.starsWas = std::atof(t[5].c_str());
      if (!std::isfinite(g_mk.starsWas)) g_mk.starsWas = 0.0;
      if (t.size() >= 8) g_mk.seasonBest = std::atoi(t[6].c_str()), g_mk.recordSeason = std::atoi(t[7].c_str());
    } else if (t[0] == "M" && t.size() >= 13) {
      Mine m;
      m.id = static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10));
      if (!read_variant(t, 2, &m.v)) continue;
      xrcoin::parse(t[9].c_str(), &m.ask);
      m.listed = std::atoi(t[10].c_str());
      m.views = std::atoi(t[11].c_str());
      m.watching = std::atoi(t[12].c_str());
      if (m.ask >= 1 && static_cast<int>(g_mk.mine.size()) < kMaxMine) g_mk.mine.push_back(m);
    } else if (t[0] == "H" && t.size() >= 13) {
      Sale s;
      if (!read_variant(t, 1, &s.v)) continue;
      s.buyer = t[8];
      xrcoin::parse(t[9].c_str(), &s.amount);
      xrcoin::parse(t[10].c_str(), &s.market);
      s.day = std::atoi(t[11].c_str());
      s.surprise = std::atoi(t[12].c_str()) != 0;
      g_mk.sales.insert(g_mk.sales.begin(), s);
    } else if (t[0] == "C" && t.size() >= 3) {
      g_mk.cycle = std::atof(t[1].c_str());
      g_mk.unseenSales = std::atoi(t[2].c_str());
      for (size_t i = 3; i < t.size(); ++i) g_mk.cycleHist.push_back(std::atof(t[i].c_str()));
      if (!std::isfinite(g_mk.cycle)) g_mk.cycle = 0.0;
    } else if (t[0] == "R" && t.size() >= 3) {
      xrcoin::parse(t[1].c_str(), &g_mk.record);
      g_mk.recordText = t[2];
    } else if (t[0] == "W" && t.size() >= 2) {  // r235
      if (g_watch.size() < 12 && std::find(g_watch.begin(), g_watch.end(), t[1]) == g_watch.end()) g_watch.push_back(t[1]);
    } else if (t[0] == "Q" && t.size() >= 2) {
      g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), t[1]), g_recent.end());
      g_recent.insert(g_recent.begin(), t[1]);
      if (g_recent.size() > 4) g_recent.resize(4);
    } else if (t[0] == "Y" && t.size() >= 4) {
      Paid p;
      xrcoin::parse(t[2].c_str(), &p.coins);
      p.step = std::atoi(t[3].c_str());
      g_paid[t[1]] = p;
    }
  }
  for (auto& kv : g_mk.lines)  // (a line's history as long as the days')
    while (!kv.second.hist.empty() && kv.second.hist.size() < g_mk.days.size()) kv.second.hist.insert(kv.second.hist.begin(), kv.second.hist.front());
  // r210: the market's mood as long as the days (an older file: calm all along)
  while (g_mk.cycleHist.size() < g_mk.days.size()) g_mk.cycleHist.insert(g_mk.cycleHist.begin(), g_mk.cycleHist.empty() ? 0.0 : g_mk.cycleHist.front());
  while (g_mk.cycleHist.size() > g_mk.days.size()) g_mk.cycleHist.erase(g_mk.cycleHist.begin());
  if (!g_mk.scale210) g_mk.offers.clear();  // (r201-r209 offers were at the old scale: new ones come at the next matchday)
  // r231: a file from before the stats-driven market (r201-r230): its prices re-priced from the play at the franchise's first read
  // (frame: reprice()); meanwhile its history read as demand (r210-r230 kept a form factor e^(0.7 x heat)); its offers (made at the
  // popularity prices) dropped - new ones come at the next matchday
  if (!g_mk.v231) {
    g_mk.reprice231 = true;
    g_mk.offers.clear();
    for (auto& kv : g_mk.lines)
      for (float& h : kv.second.hist) h = static_cast<float>(demand_of_level(soft_cap(std::log(std::max(0.05f, h)) / 0.7)));
  }
  g_mk.loaded = true;
  return true;
}

void check_mine(int pulls, int tick = 0);  // r210 (below: your listings, a collector buying; r238: + a real-time tick's)
void mag_build();            // r219 (below: STRIKERS ILLUSTRATED's market spread, made once a matchday)
// r231: an r201-r230 market file's first read of the franchise: every family re-priced from the play now - its legacy from the awards its
// lines kept (|MVP / |BOOT / |ROOK / |CHAMP tags), its history warmed up as a new career's (8 matchdays at the state now); your binder's
// chart rescaled to the new prices (its shape kept); listings, sales, the record, the wire as they were
void reprice231(const PortFranchiseMarketState& fs) {
  auto count = [](const std::string& s, const char* tag) {
    int n = 0;
    for (size_t p = s.find(tag); p != std::string::npos; p = s.find(tag, p + 1)) ++n;
    return n;
  };
  for (auto& kv : g_mk.lines) {
    Line& l = kv.second;
    l.mvps = count(l.why, "|MVP");
    l.boots = count(l.why, "|BOOT");
    l.roys = count(l.why, "|ROOK");
    l.pms = count(l.why, "|PLAY");  // r232 (none in an r201-r230 file)
    l.titles = 0;
    l.legacy = 0.45 * l.mvps + 0.35 * l.boots + 0.20 * l.roys + 0.25 * l.pms +
               (l.retired ? 0.35 * std::min(1.0, prod_of(l.careerGoals, l.careerAst) / 80.0) + 0.10 * (l.mvps + l.boots + l.pms) : 0.0);
    l.heat = l.shock = l.fast = l.prior = 0.0;
    l.hist.clear();
    l.rank = l.prevRank = 0;
    l.lastSeason = -1;
  }
  const long long before = g_mk.portfolio.empty() ? 0 : g_mk.portfolio.back();
  const int stepsWas = g_mk.steps;
  const double mood = g_mk.cycle;  // (the market's mood as it was)
  g_mk.days.clear();
  g_mk.cycleHist.clear();
  const bool newDay = fs.season != g_mk.season || fs.week != g_mk.week;
  const int dayIndex = fs.season * 40 + fs.week;
  for (int k = 8; k >= (newDay ? 1 : 0); --k) step(fs, dayIndex - k, true, 1);
  g_mk.steps = stepsWas;
  g_mk.cycle = mood;
  for (double& c : g_mk.cycleHist) c = mood;
  if (before > 0) {
    const double k = static_cast<double>(portfolio_value(-1)) / static_cast<double>(before);
    for (long long& p : g_mk.portfolio) p = xrcoin::from_double(static_cast<double>(p) * k);
  }
  g_mk.reprice231 = false;
  g_mk.v231 = true;
  sell_new_prices();  // r240
  mag_build();
}
// a new matchday (or the market's first sight of a career: its pre-history warmed up from the state now)
void advance(const PortFranchiseMarketState& fs) {
  const int dayIndex = fs.season * 40 + fs.week;
  if (g_mk.steps == 0) {
    g_mk.reprice231 = false;  // (r231: a first sight is priced from the play already)
    for (int k = 8; k >= 1; --k) step(fs, dayIndex - k, true, 1);
  }
  int pulls = 1;
  if (g_mk.season == fs.season && fs.week > g_mk.week) pulls = std::min(4, fs.week - g_mk.week);
  const bool fresh = g_mk.season < 0;
  step(fs, dayIndex, fresh, pulls);
  g_mk.season = fs.season;
  g_mk.week = fs.week;
  g_mk.tickN = 0;  // r238: (this matchday's real-time ticks start again)
  make_listings(dayIndex);
  make_offers();
  if (!fresh) {  // r210: the world's big sales, then collectors looking at yours (each matchday simmed: a look)
    world_sales(dayIndex);
    check_mine(pulls);
  }
  const long long pv = portfolio_value(-1);
  if (g_mk.portfolio.empty()) {  // (the first sight: the binder's value back through the warm-up)
    for (int i = 0; i + 1 < static_cast<int>(g_mk.days.size()); ++i) g_mk.portfolio.push_back(portfolio_value(i));
  }
  g_mk.portfolio.push_back(pv);
  if (static_cast<int>(g_mk.portfolio.size()) > kHist) g_mk.portfolio.erase(g_mk.portfolio.begin());
  for (auto it = g_paid.begin(); it != g_paid.end();)  // r235: (the flip rule's memory: 2 matchdays)
    if (it->second.step < g_mk.steps - 1) it = g_paid.erase(it);
    else ++it;
  g_rowsDirty = true;  // r235: the dealer's new listings, every price moved: the search's results again (once)
  rows_build();
  watch_check();
  sell_new_prices();  // r240: SELL's prices for this matchday (your binder's, once - part of the matchday's work, never a key press's)
  mag_build();  // r219: STRIKERS ILLUSTRATED's market spread for this matchday
  ++g_gen;
}

// ---------------------------------------------------------------------------------------- the actions
// r238: an offer with nothing left to buy - a watcher's whose listing came down, else one on a copy that's no longer free
bool offer_gone(const Offer& o) {
  if (o.listing == 0) return !own_free(o.v);
  for (const Mine& m : g_mk.mine)
    if (m.id == o.listing) return false;
  return true;
}
bool do_sell(const Variant& v, long long coins, const char* why, bool listing = false, const char* who = "A DEALER") {
  if (g_mk.career == 0 || g_src.take == nullptr || g_src.add == nullptr) return false;
  if (!(listing ? held_count(v) > 0 : own_free(v))) return false;  // (r210: a listed copy sells only through its listing)
  if (!g_src.take(v)) return false;
  if (g_src.sold != nullptr) g_src.sold(v, who);  // r213: (a sold 1/1 stays in the collector world)
  if (g_src.sold_for != nullptr) g_src.sold_for(v, who, coins);  // r235: (a named collector's buy: into his binder)
  g_src.add(g_mk.career, coins, why);
  g_mk.received = xrcoin::add(g_mk.received, coins);  // (r208: held, never wrapping)
  ++g_mk.sold;
  read_holdings();
  record_locked(v, coins, "YOU", who != nullptr ? who : "A DEALER", 1);  // r238: (its SALES history)
  for (size_t i = 0; i < g_mk.offers.size();)  // (its offers go with it when that was the last free copy)
    if (offer_gone(g_mk.offers[i])) g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
    else ++i;
  ++g_gen;
  return true;
}
bool do_buy(int li) {
  if (li < 0 || li >= static_cast<int>(g_mk.listings.size()) || g_mk.career == 0) return false;
  Listing& L = g_mk.listings[static_cast<size_t>(li)];
  if (L.sold) return false;
  const long long cost = ask_of(price_i(L.v, -1));
  if (g_src.spend == nullptr || g_src.give == nullptr) return false;
  if (!g_src.spend(g_mk.career, cost, "STRIKERZ MARKET")) {
    say("NOT ENOUGH COINS - " + coins_text(cost) + " NEEDED", 0xFF4D5E);
    return false;
  }
  if (!g_src.give(L.v)) {
    if (g_src.add != nullptr) g_src.add(g_mk.career, cost, "STRIKERZ MARKET REFUND");
    say("THAT COPY COULDN'T GO IN YOUR BINDER", 0xFF4D5E);
    return false;
  }
  L.sold = true;
  g_mk.spent = xrcoin::add(g_mk.spent, cost);
  ++g_mk.bought;
  read_holdings();
  record_locked(L.v, cost, "THE DEALER", "YOU", 2);  // r238: (its SALES history)
  g_mk.firstSeen[sig_of(L.v)] = g_mk.steps;  // (offers on it only after a whole matchday)
  g_paid[sig_of(L.v)] = {cost, g_mk.steps};  // r235: (the flip rule)
  g_rowsDirty = true;
  ++g_gen;
  return true;
}
Offer* offer_by(uint32_t id) {
  for (Offer& o : g_mk.offers)
    if (o.id == id) return &o;
  return nullptr;
}
bool do_accept(uint32_t id) {
  Offer* o = offer_by(id);
  if (o == nullptr) return false;
  const Offer copy = *o;
  if (copy.listing != 0) {  // r238: a watcher's offer on your listing - the listing comes down and that copy sells to him
    Mine keep;
    bool had = false;
    for (size_t i = 0; i < g_mk.mine.size(); ++i)
      if (g_mk.mine[i].id == copy.listing) {
        keep = g_mk.mine[i];
        had = true;
        g_mk.mine.erase(g_mk.mine.begin() + static_cast<long>(i));
        break;
      }
    if (!had) return false;
    if (!do_sell(copy.v, copy.amount, "STRIKERZ MARKET OFFER", false, copy.who.c_str())) {
      g_mk.mine.push_back(keep);  // (it couldn't sell: back up as it was)
      return false;
    }
    tell_listed();
  } else if (!do_sell(copy.v, copy.amount, "STRIKERZ MARKET OFFER", false, copy.who.c_str())) {
    return false;
  }
  for (size_t i = 0; i < g_mk.offers.size(); ++i)
    if (g_mk.offers[i].id == id) {
      g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
      break;
    }
  headline("YOUR " + (exq_v(copy.v) ? std::string(xrexq::short_name(exq_t(copy.v))) : upper(kParName[copy.v.parallel])) + " " + name_of_card(copy.v.card) + " SOLD TO " + copy.who + " FOR " + coins_text(copy.amount), 0xFFD23F, false);
  return true;
}
int do_counter(uint32_t id, int pct) {
  Offer* o = offer_by(id);
  if (o == nullptr || o->final) return 0;
  const long long want = static_cast<long long>(llr(static_cast<double>(o->amount) * (1.0 + pct / 100.0)));
  if (want <= o->reserve) {
    o->amount = want;
    return do_accept(id) ? 1 : 0;
  }
  if (want <= static_cast<long long>(static_cast<double>(o->reserve) * 1.06)) {
    o->amount = o->reserve;
    o->final = true;
    ++g_gen;
    return 2;
  }
  for (size_t i = 0; i < g_mk.offers.size(); ++i)
    if (g_mk.offers[i].id == id) {
      g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
      break;
    }
  ++g_gen;
  return 3;
}

// ---------------------------------------------------------------------------------------- r210: your listings
std::string copy_words(const Variant& v) { return upper(variant_words(v)) + " " + name_of_card(v.card); }
Mine* mine_by(uint32_t id) {
  for (Mine& m : g_mk.mine)
    if (m.id == id) return &m;
  return nullptr;
}
uint32_t do_list(const Variant& v, long long ask) {
  if (g_mk.career == 0 || v.card < 0 || v.card >= static_cast<int>(g_set.size())) return 0;
  ask = xrcoin::clamp(ask);
  if (ask < 1 || static_cast<int>(g_mk.mine.size()) >= kMaxMine || !own_free(v)) return 0;
  Mine m;
  m.id = g_mk.nextId++;
  m.v = v;
  m.ask = ask;
  m.listed = g_mk.steps;
  g_mk.mine.push_back(m);
  for (size_t i = 0; i < g_mk.offers.size();)  // (offers on it go: it's listed now - r238: a watcher's on another listing stays)
    if (offer_gone(g_mk.offers[i])) g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
    else ++i;
  tell_listed();
  ++g_gen;
  return m.id;
}
bool do_unlist(uint32_t id) {
  for (size_t i = 0; i < g_mk.mine.size(); ++i)
    if (g_mk.mine[i].id == id) {
      g_mk.mine.erase(g_mk.mine.begin() + static_cast<long>(i));
      for (size_t k = 0; k < g_mk.offers.size();)  // r238: (a watcher's offer on it goes with it)
        if (offer_gone(g_mk.offers[k])) g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(k));
        else ++k;
      tell_listed();
      ++g_gen;
      return true;
    }
  return false;
}
// a listing's chance this matchday: how many serious collectors look (its value, the character's demand, the market's mood, the buzz
// round it) and the most the one looking would pay (round the market: under it they jump, at it often, over it now and then - and a
// rich one, once in a while, pays anything near what you ask)
struct Odds {
  double look, mu, sigma, whale;
};
Odds listing_odds(const Variant& v, long long market) {
  const int t = tier_of(v.card);
  static const double kTierLook[6] = {1.0, 0.7, 0.9, 1.0, 1.15, 1.3};
  double look = market < 200 ? 0.05 : (market < 2000 ? 0.2 : (market < 50000 ? 0.4 : (market < 1000000 ? 0.5 : 0.6)));
  const Line* l = line_of(v.card);
  const double buzz = l != nullptr ? clampd(l->shock, 0.0, 1.0) : 0.0;
  look *= kTierLook[t] * (1.0 + 0.6 * clampd(g_mk.cycle, -0.8, 1.0)) * (1.0 + 0.5 * buzz);
  Odds o;
  o.look = clampd(look, 0.02, 0.92);
  o.mu = -0.06 + 0.25 * clampd(g_mk.cycle, -0.8, 1.0) + 0.03 * (t - 3) + 0.08 * buzz;
  o.sigma = 0.10;
  o.whale = 0.015 + (t >= 5 || market >= 1000000 ? 0.01 : 0.0);
  return o;
}
// r238 (Lance 11-12: "i've had a card sit for 1300 while the value has only climbed. i literally only set it so low so itd sell right away. now
// its been on the market for 3 days and price has gone up and nobody has made any action on it. lot of watchers but come on man this aint ebay
// this is fast flipping activity!"). WHY IT SAT (r210's model, above): a listing was LOOKED AT by one serious collector only by its value
// band - 20% a matchday for a card worth 200 - 2,000 (x 0.7 for a bench player's, x 0.9 the next band), 5% for bulk - only on matchdays, and
// the one looking paid ~94% of the market at the median: his 1,300 under a ~1,500 market had ~11% a matchday (~30% over his 3); the watchers
// were only a number on the page. NOW the buyers' side is the whole market at every look (a matchday, or a real-time TICK in the hub): a
// fair price SELLS - sell_p by your price's share of the market (at it ~60-80% a look, 5% under ~85-90%, 10% under ~95%; 5% over ~35%,
// 10% over ~12-25%, a long shot now and then - a rich collector), the watchers on an over-market listing coming round (and turning into
// OFFERS: watcher_offer). Deterministic per career / listing / matchday / tick - changing the price can't re-roll a look.
double sell_p(const Variant& v, double ratio, int watching) {
  const int t = tier_of(v.card);
  const double r0 = 1.035 + 0.01 * (t - 3) + 0.04 * clampd(g_mk.cycle, -0.8, 1.0);
  const double core = 0.97 / (1.0 + std::exp((ratio - r0) / 0.035));
  const double tail = ratio > 1.0 ? 0.08 * std::exp(-(ratio - 1.0) / 0.3) : 0.0;  // (a rich collector who wants it now)
  double p = std::max(core, tail);
  if (ratio > 1.0) p *= 1.0 + 0.03 * std::min(10, std::max(0, watching));  // (the watchers coming round)
  return clampd(p, 0.0, 0.985);
}
// r238: a watcher on your over-market listing makes an OFFER on it (under your price, over SELL NOW): A takes it (the listing comes down and
// that copy sells to him), Y counters, X declines - MARKET > OFFERS
void watcher_offer(const Mine& m, long long market, double ratio, uint64_t h, int tick) {
  if (ratio <= 1.0 || ratio > 1.6 || m.watching < 2 || static_cast<int>(g_mk.offers.size()) >= kMaxOffers) return;
  for (const Offer& o : g_mk.offers)
    if (o.listing == m.id || sig_of(o.v) == sig_of(m.v)) return;
  if (u01(h ^ 0xA77Eull) >= 0.25 * std::min(1.0, m.watching / 6.0)) return;
  const double pd = static_cast<double>(market);
  Offer o;
  o.v = m.v;
  o.listing = m.id;
  o.reserve = std::min(m.ask - 1, static_cast<long long>(std::floor(pd * std::min(kOfferMax, 1.0 + 0.08 * u01(h ^ 0xA780ull)))));
  o.amount = std::min(o.reserve, std::max(bid_of(market) + 1, static_cast<long long>(std::floor(pd * (0.95 + 0.07 * u01(h ^ 0xA781ull))))));
  if (o.amount < 1 || o.amount >= m.ask) return;
  o.reserve = std::max(o.reserve, o.amount);
  o.id = g_mk.nextId++;
  o.who = pick_buyer(h ^ 0xB1Dull, o.reserve);
  o.bidders = 1;
  o.day = g_mk.steps;
  o.expires = g_mk.steps + 2;
  g_mk.offers.push_back(o);
  headline(o.who + " WAS WATCHING YOUR " + copy_words(m.v) + " - OFFERS " + coins_text(o.amount), 0x2EF2FF, false);
  notice("AN OFFER ON YOUR LISTING", copy_words(m.v), o.who + " OFFERS " + coins_text(o.amount) + " (YOU ASK " + coins_text(m.ask) + ")");
  (void)tick;
}
// your listings at one look: `pulls` matchdays simmed (r210), or one real-time tick (tick > 0: its own rolls)
void check_mine(int pulls, int tick) {
  bool any = false;
  for (int k = 0; k < std::max(1, pulls); ++k) {
    const int key = tick > 0 ? 100 + tick : k;
    for (size_t i = 0; i < g_mk.mine.size();) {
      Mine m = g_mk.mine[i];
      const long long market = price_i(m.v, -1);
      const Odds o = listing_odds(m.v, market);
      // its views + watchers: eyes on it (more for a fair price, a wanted card, a hot market; a tick a third of a matchday's)
      const double ratio = static_cast<double>(m.ask) / static_cast<double>(std::max(1LL, market));
      const uint64_t hv = mix64(static_cast<uint64_t>(m.id) * 0xC2B2AE3D27D4EB4Full ^ static_cast<uint64_t>(g_mk.steps * 8 + k)) ^
                          (tick > 0 ? mix64(static_cast<uint64_t>(key) * 0x9E3779B97F4A7C15ull) : 0ull);
      g_mk.mine[i].views += static_cast<int>(std::lround(std::max(0.3, o.look) * 30.0 * std::min(3.0, 1.0 / (ratio * ratio)) * (0.6 + 0.8 * u01(hv)) * (tick > 0 ? 0.35 : 1.0)));
      g_mk.mine[i].watching = static_cast<int>(std::lround(std::max(0.3, o.look) * 14.0 * clampd(1.5 - ratio, 0.0, 1.5) * (0.7 + 0.6 * u01(hv ^ 9))));
      m.watching = g_mk.mine[i].watching;
      const uint64_t hs = mix64(static_cast<uint64_t>(g_mk.career) * 0xD6E8FEB86659FD93ull ^ static_cast<uint64_t>(m.id) * 0x9E3779B97F4A7C15ull ^
                                static_cast<uint64_t>(static_cast<int64_t>(g_mk.steps) * 131 + key + 77));
      if (u01(hs) >= sell_p(m.v, ratio, m.watching)) {
        watcher_offer(m, market, ratio, hs, tick);
        ++i;
        continue;
      }
      // sold: at your price, into this career's wallet, out of the binder (and the museum, if it was on show)
      const std::string buyer = pick_buyer(hv ^ 0x77, m.ask);  // r235: (a named collector who can afford it, mostly)
      if (!do_sell(m.v, m.ask, "STRIKERZ MARKET - YOUR LISTING SOLD", true, buyer.c_str())) {
        ++i;
        continue;
      }
      buyer_paid(buyer, m.ask);
      g_mk.mine.erase(g_mk.mine.begin() + static_cast<long>(i));
      for (size_t q = 0; q < g_mk.offers.size();)  // r238: (a watcher's offer on it goes)
        if (offer_gone(g_mk.offers[q])) g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(q));
        else ++q;
      Sale s;
      s.v = m.v;
      s.buyer = buyer;
      s.amount = m.ask;
      s.market = market;
      s.day = g_mk.steps;
      s.surprise = ratio > 1.05;
      g_mk.sales.insert(g_mk.sales.begin(), s);
      if (g_mk.sales.size() > 12) g_mk.sales.resize(12);
      ++g_mk.unseenSales;
      headline("SOLD! YOUR " + copy_words(m.v) + " WENT TO " + buyer + " FOR " + coins_text(m.ask) + " COINS" +
                   (s.surprise ? " - A COLLECTOR PAID YOUR PRICE!" : ""),
               0x39FF88, m.ask >= 1000);
      say((s.surprise ? "A COLLECTOR PAID YOUR PRICE! " : "SOLD! ") + copy_words(m.v) + " +" + coins_text(m.ask) + " COINS", 0x39FF88);
      notice("SOLD!", "YOUR " + copy_words(m.v), "TO " + buyer + " FOR " + coins_text(m.ask) + " COINS");  // r238: (the hub's pop-up)
      g_flashAt = g_now;
      any = true;
    }
  }
  if (any) {
    sound("book_collect", 0.9f);
    sound("book_coin", 0.7f);
    tell_listed();
  }
}// r238: one real-time tick (the hub open: every kTickSecs) - this matchday's next: your listings looked at again (fresh rolls), a watcher's
// offer, a collector's offer on a card of yours now and then (a quarter of a matchday's chance, one at most), the wire's big sales a third
// of the time; xr_trade's hobby follows Stats::tick (their listings, buys, a few packs). (g_m held, game thread)
#ifdef PROF238
extern "C" double host_now_ms();
extern "C" void host_log(const char* s, int len);
#define P238(x) const double x = host_now_ms()
#else
#define P238(x)
#endif
void tick_now() {
  if (g_mk.career == 0 || g_mk.steps <= 0) return;
  ++g_mk.tickN;
  ++g_mk.ticks;
  P238(t0);
  check_mine(1, g_mk.tickN);
  P238(t1);
  make_offers(g_mk.tickN, 0.25);
  P238(t2);
  const int dayIndex = std::max(0, g_mk.season) * 40 + std::max(0, g_mk.week);
  if (u01(static_cast<uint64_t>(g_mk.career) * 0x7F4A7C15ull ^ static_cast<uint64_t>(dayIndex * 64 + g_mk.tickN) * 0x9E3779B97F4A7C15ull) < 0.33)
    world_sales((1 << 20) + dayIndex * 64 + g_mk.tickN);
  P238(t3);
  g_rowsDirty = true;
  watch_check();
  P238(t4);
#ifdef PROF238
  { char b[200]; const int n = std::snprintf(b, sizeof(b), "PROF238 tick: mine %.2f offers %.2f wire %.2f watch %.2f", t1 - t0, t2 - t1, t3 - t2, t4 - t3); host_log(b, n); }
#endif
  ++g_gen;
}
// a listing whose copy left the binder some other way (it can't, through the market: but a save edited, a career switched...) comes down
bool trim_mine() {
  bool changed = false;
  for (size_t i = g_mk.mine.size(); i-- > 0;) {
    if (listed_count(g_mk.mine[i].v) > held_count(g_mk.mine[i].v)) {
      say("A LISTING CAME DOWN - " + copy_words(g_mk.mine[i].v) + " ISN'T IN YOUR BINDER", 0xFFB020);
      g_mk.mine.erase(g_mk.mine.begin() + static_cast<long>(i));
      changed = true;
    }
  }
  if (changed) tell_listed();
  return changed;
}

// the MY CARDS list: your copies (not at grading), the most valuable first
std::vector<Holding> mine_sorted() {
  std::vector<Holding> v;
  for (const Holding& h : g_hold)
    if (!h.grading && h.card >= 0 && h.card < static_cast<int>(g_set.size())) v.push_back(h);
  std::stable_sort(v.begin(), v.end(), [](const Holding& a, const Holding& b) { return price_i(a, -1) > price_i(b, -1); });
  return v;
}
struct Mover {
  int card;
  float pct;
  long long price;
};
void movers(std::vector<Mover>* hot, std::vector<Mover>* cold) {
  std::vector<Mover> all;
  for (const auto& kv : g_mk.lines) {
    const Line& l = kv.second;
    if (l.activeCard < 0 || l.hist.size() < 2 || !g_set[static_cast<size_t>(l.activeCard)].here) continue;
    Variant v;
    v.card = l.activeCard;
    if (l.club < 0 && !l.retired) continue;
    const double now = price_d(v, -1), was = price_d(v, static_cast<int>(g_mk.days.size()) - 2);
    all.push_back({l.activeCard, static_cast<float>(100.0 * (now - was) / std::max(0.01, was)), price_i(v, -1)});
  }
  std::stable_sort(all.begin(), all.end(), [](const Mover& a, const Mover& b) { return a.pct > b.pct; });
  for (size_t i = 0; i < all.size() && hot->size() < 5; ++i)
    if (all[i].pct > 0.5f) hot->push_back(all[i]);
  for (size_t i = all.size(); i-- > 0 && cold->size() < 5;)
    if (all[i].pct < -0.5f) cold->push_back(all[i]);
}
// r231: the TOP CARDS race - every family in the league (or retired) by its demand, its card as it plays now (g_m held)
std::vector<TopCard> top_list(int n) {
  std::vector<const Line*> v;
  for (const auto& kv : g_mk.lines)
    if (kv.second.rank > 0 && kv.second.activeCard >= 0) v.push_back(&kv.second);
  std::stable_sort(v.begin(), v.end(), [](const Line* a, const Line* b) { return a->rank < b->rank; });
  std::vector<TopCard> out;
  const int nd = static_cast<int>(g_mk.days.size());
  for (const Line* l : v) {
    if (static_cast<int>(out.size()) >= n) break;
    TopCard tc;
    tc.card = l->activeCard;
    tc.rank = l->rank;
    tc.prevRank = l->prevRank;
    tc.demand = l->hist.empty() ? 0.0 : l->hist.back();
    Variant b;
    b.card = l->activeCard;
    tc.price = price_i(b, -1);
    if (nd >= 2) {
      const double was = price_d(b, nd - 2);
      tc.pct = static_cast<float>(100.0 * (price_d(b, -1) - was) / std::max(0.01, was));
    }
    tc.why = l->reason;
    out.push_back(tc);
  }
  return out;
}
// ---------------------------------------------------------------------------------------- r219: STRIKERS ILLUSTRATED's market spread
// (BIG-PULLS.md 4) - made once a matchday (advance) or when a career's market is loaded, kept in g_mag (never saved: it's all computed
// from what the file already holds)
MagMarket g_mag;
std::vector<int> g_basketA, g_basketJ;  // THE STRIKERZ 100: the most-wanted characters' LANCINI TIZM cards + their JAM '96 twins
void mag_basket() {  // (g_m held) r231: the 100 most-wanted by PLAY now (their family's demand; r219-r230: fixed by the popularity tiers)
  g_basketA.clear();
  g_basketJ.clear();
  if (g_set.empty()) return;
  std::vector<int> jamOf(g_set.size(), -1);
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].set == 1 && g_set[i].base >= 0 && g_set[i].base < static_cast<int>(g_set.size())) jamOf[static_cast<size_t>(g_set[i].base)] = static_cast<int>(i);
  std::vector<int> a;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].set == 0 && g_set[i].here) a.push_back(static_cast<int>(i));
  std::vector<double> dem(g_set.size(), 0.0);
  for (int c : a) dem[static_cast<size_t>(c)] = demand_at(c, -1) * rookie_m(c, 0.0);  // (r239: rookie_m - r231-r238 stage_mult)
  std::stable_sort(a.begin(), a.end(), [&dem](int x, int y) { return dem[static_cast<size_t>(x)] > dem[static_cast<size_t>(y)]; });
  if (a.size() > 100) a.resize(100);
  for (int c : a) {
    g_basketA.push_back(c);
    g_basketJ.push_back(jamOf[static_cast<size_t>(c)]);
  }
}
// the index at history entry i: the basket's BASE + SILVER + GOLD /10 prices, averaged (a set's own cards; a missing twin: left out)
double mag_index(const std::vector<int>& basket, int i) {
  double sum = 0.0;
  int n = 0;
  for (int c : basket) {
    if (c < 0) continue;
    Variant v;
    v.card = c;
    for (int p = kBase; p <= kGoldTizm; ++p) {
      v.parallel = p;
      v.serial = p == kGoldTizm ? 5 : 0;
      sum += price_d(v, i);
      ++n;
    }
  }
  return n > 0 ? sum / n : 0.0;
}
// "A BLACK 1/1 GENGAR AUTO JUST SOLD FOR 42,995,459 COINS[ - A NEW RECORD!]" -> its words + amount
bool mag_parse_sale(const std::string& t, MagSale* s) {
  const size_t at = t.find(" JUST SOLD FOR ");
  if (t.compare(0, 2, "A ") != 0 || at == std::string::npos) return false;
  s->what = t.substr(2, at - 2);
  long long n = 0;
  bool any = false;
  for (size_t k = at + 15; k < t.size(); ++k) {
    const char ch = t[k];
    if (ch >= '0' && ch <= '9') {
      n = n * 10 + (ch - '0');
      any = true;
      if (n > xrcoin::kMax) n = xrcoin::kMax;
    } else if (ch != ',') {
      break;
    }
  }
  s->amount = n;
  s->record = t.find("A NEW RECORD!") != std::string::npos;
  return any;
}
// the league's collectors' pulls this matchday (deterministic from the career + the day: the same every time it's made): a SILVER, a
// GOLD /10 (a number), a case hit - of the TOP CARDS race's players mostly (r231; r219-r230: the popularity tiers). Never a BLACK 1/1 (one per card: the trades' world).
void mag_pulls(int dayIndex, std::vector<MagPull>* out) {
  out->clear();
  std::vector<int> pool;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].here)
      for (int k = 0, t = tier_of(static_cast<int>(i)); k < (t >= 5 ? 6 : (t == 4 ? 4 : (t == 3 ? 2 : 1))); ++k) pool.push_back(static_cast<int>(i));  // (r231: the race)
  if (pool.empty()) return;
  const uint64_t base = mix64(static_cast<uint64_t>(g_mk.career) * 0x2545F4914F6CDD1Dull ^ static_cast<uint64_t>(static_cast<int64_t>(dayIndex) + 21919));
  for (int n = 0; n < 6; ++n) {
    const uint64_t h = mix64(base + static_cast<uint64_t>(n) * 0x9E3779B97F4A7C15ull);
    MagPull p;
    p.v.card = pool[static_cast<size_t>(mix64(h ^ 1) % pool.size())];
    const double u = u01(h ^ 2);
    if (u < 0.45) p.v.parallel = kSilver;
    else if (u < 0.80) p.v.parallel = kGoldTizm, p.v.serial = 1 + static_cast<int>(mix64(h ^ 3) % 10);
    else p.v.hit = 1 + static_cast<int>(mix64(h ^ 4) % 3), p.v.parallel = u01(h ^ 5) < 0.3 ? kSilver : kBase;
    if (g_set[static_cast<size_t>(p.v.card)].key.find("pk_") == 0 && u01(h ^ 6) < 0.08) p.v.shiny = true;
    if (exq_v(p.v)) {  // r230: an EXQUISITE pull: a real number (never a 1/1)
      std::vector<Variant> also;
      for (const MagPull& q : *out) also.push_back(q.v);
      if (!exq_fill(&p.v, h ^ 0x6B, also)) continue;
    }
    p.who = kWho[mix64(h ^ 7) % 24];
    p.value = price_i(p.v, -1);
    bool dup = false;
    for (const MagPull& q : *out) dup = dup || q.v.card == p.v.card || q.who == p.who;
    if (!dup) out->push_back(p);
  }
  std::stable_sort(out->begin(), out->end(), [](const MagPull& a, const MagPull& b) { return a.value > b.value; });
  if (out->size() > 4) out->resize(4);
}
void mag_build() {  // (g_m held) after a matchday is priced / a career's market loaded
  MagMarket m;
  if (g_mk.career == 0 || g_mk.days.empty() || g_set.empty()) {
    g_mag = m;
    return;
  }
  mag_basket();
  m.valid = true;
  m.career = g_mk.career;
  m.steps = g_mk.steps;
  m.season = g_mk.season;
  m.week = g_mk.week;
  m.mood = g_mk.cycle;
  m.moodHist = g_mk.cycleHist;
  m.basket = static_cast<int>(g_basketA.size());
  for (int i = 0; i < static_cast<int>(g_mk.days.size()); ++i) {
    m.indexA.push_back(mag_index(g_basketA, i));
    m.indexJ.push_back(mag_index(g_basketJ, i));
  }
  std::vector<Mover> hot, cold;
  movers(&hot, &cold);
  for (int side = 0; side < 2; ++side)
    for (const Mover& mv : side == 0 ? hot : cold) {
      std::vector<MagMover>& dst = side == 0 ? m.hot : m.cold;
      if (dst.size() >= 3) break;
      MagMover x;
      x.v.card = mv.card;
      x.name = name_of_card(mv.card);
      x.pct = mv.pct;
      x.price = mv.price;
      if (const Line* l = line_of(mv.card)) x.why = l->reason, x.rank = l->rank;  // r231: the play behind it
      for (int i = 0; i < static_cast<int>(g_mk.days.size()); ++i) x.spark.push_back(price_d(x.v, i));
      dst.push_back(x);
    }
  for (const TopCard& tc : top_list(5)) {  // r231: the TOP CARDS race's first 5
    MagMover x;
    x.v.card = tc.card;
    x.name = name_of_card(tc.card);
    x.pct = tc.pct;
    x.price = tc.price;
    x.why = tc.why;
    x.rank = tc.rank;
    for (int i = 0; i < static_cast<int>(g_mk.days.size()); ++i) x.spark.push_back(price_d(x.v, i));
    m.top.push_back(x);
  }
  m.record = g_mk.record;
  m.recordText = g_mk.recordText;
  for (const Headline& h : g_mk.news) {
    if (h.day != g_mk.steps) continue;  // (this matchday's)
    MagSale s;
    if (!mag_parse_sale(h.text, &s)) continue;
    m.recordNew = m.recordNew || s.record;
    m.sales.push_back(s);
  }
  std::stable_sort(m.sales.begin(), m.sales.end(), [](const MagSale& a, const MagSale& b) { return a.amount > b.amount; });
  if (m.sales.size() > 3) m.sales.resize(3);
  mag_pulls(g_mk.days.back(), &m.pulls);
  m.portfolio = g_mk.portfolio;
  for (const Holding& h : g_hold) {
    if (h.grading || h.card < 0 || h.card >= static_cast<int>(g_set.size())) continue;
    m.copies += std::max(0, h.count);
    const long long p = price_i(h, -1);
    if (p > m.bestValue) m.bestValue = p, m.best = h;
  }
  if (m.bestValue == 0) m.best.card = -1;
  g_mag = std::move(m);
}
}  // namespace

// ============================================================================================== the public model
std::string variant_words(const Variant& v) {
  std::string s;
  {  // r230: a LANCINI EXQUISITE copy: "SAPPHIRE 03/10", "MASTERPIECE 1/1 PATCH AUTO" (+ JERSEY NUMBER, its slab)
    // (no lock: also called with the market lock held; the set is made once, before any copy exists)
    if (exq_v(v)) {
      s = xrexq::words(v.serial >= 1000 ? v.serial : xrexq::serial_of(xrexq::kGallery, 0), exq_rookie_v(v));
      if (v.serial < 1000) s = "GALLERY /399";
      if (xrexq::jersey(v.serial, g_set[static_cast<size_t>(v.card)].no)) s += " - JERSEY NUMBER";
      if (v.grade > 0) s += "  -  TIZM " + std::to_string(v.grade) + " " + kGradeName[v.grade];
      return s;
    }
  }
  if (v.hit != kHitNone) s = kHitName[std::max(0, std::min(3, v.hit))];
  else s = kParName[std::max(0, std::min(3, v.parallel))];
  if (v.parallel == kGoldTizm && v.serial > 0) {
    char b[16];
    std::snprintf(b, sizeof(b), " %02d/10", v.serial);
    s += b;
  }
  if (v.parallel == kBlack) s += " 1/1";
  if (v.hit != kHitNone && v.parallel != kBase) s += std::string(" - ") + kParName[v.parallel];
  if (v.shiny) s += " - SHINY";
  if (v.grade > 0) s += "  -  TIZM " + std::to_string(v.grade) + " " + kGradeName[v.grade];
  return s;
}
std::string grading_words(const Holding& h) {  // r242
  const int n = std::max(1, h.left);
  return "AT GRADING - " + std::to_string(n) + (n == 1 ? " MATCH" : " MATCHES");
}
std::string variant_sig(const std::vector<CardInfo>& set, const Variant& v) {
  if (v.card < 0 || v.card >= static_cast<int>(set.size())) return "?";
  char b[160];
  std::snprintf(b, sizeof(b), "%s:%d:%d:%d:%d:%d:%u", set[static_cast<size_t>(v.card)].key.c_str(), v.parallel, v.shiny ? 1 : 0, v.hit, v.serial, v.grade,
                v.cert);
  return b;
}
void set_source(const Source& s) {
  std::lock_guard<std::mutex> lk(g_m);
  g_src = s;
}
long long price(const Variant& v) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return price_i(v, -1);
}
long long bid(const Variant& v) { return bid_of(price(v)); }
long long ask(const Variant& v) { return ask_of(price(v)); }
int hype_tier(int card) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return tier_of(card);
}
int form_gap(int card) {  // r239
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  if (card < 0 || card >= static_cast<int>(g_set.size())) return 0;
  const int cur = cur_stage(line_of(card));
  return cur < 0 ? 0 : std::max(0, cur - main_info(card).stage);
}
double rookie_mult(int card, double s) {  // r239
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return card >= 0 && card < static_cast<int>(g_set.size()) ? rookie_m(card, s) : 1.0;
}
int demand_rank(int card) {  // r231
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  const Line* l = line_of(card);
  return l != nullptr ? l->rank : 0;
}
double demand(int card) {  // r231
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return card >= 0 && card < static_cast<int>(g_set.size()) ? demand_at(card, -1) : 0.0;
}
std::string demand_why(int card) {  // r231
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  const Line* l = line_of(card);
  return l != nullptr ? l->reason : std::string();
}
std::vector<TopCard> top_cards(int n) {  // r231
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return top_list(n);
}
int club_of(int card) {  // r230
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  const Line* l = line_of(card);
  return l != nullptr ? l->club : -1;
}
bool exq_card(int card) {  // r230
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return card >= 0 && card < static_cast<int>(g_set.size()) && g_set[static_cast<size_t>(card)].set == 2;
}
bool jam_card(int card) {  // r217
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return card >= 0 && card < static_cast<int>(g_set.size()) && g_set[static_cast<size_t>(card)].set == 1;
}
void history(const Variant& v, std::vector<long long>* out) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  out->clear();
  for (int i = 0; i < static_cast<int>(g_mk.days.size()); ++i) out->push_back(price_i(v, i));
}
long long population(const Variant& v) {
  std::lock_guard<std::mutex> lk(g_m);
  return pop_of(v);
}
float change(const Variant& v) {
  std::lock_guard<std::mutex> lk(g_m);
  const int n = static_cast<int>(g_mk.days.size());
  if (n < 2) return 0.0f;
  const double now = price_d(v, -1), was = price_d(v, n - 2);
  return static_cast<float>(100.0 * (now - was) / std::max(0.01, was));
}
long long card_value(int card) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_mk.career == 0 || card < 0 || card >= static_cast<int>(g_set.size()) || g_mk.days.empty()) return -1;
  long long best = -1;
  for (const Holding& h : g_hold)
    if (h.card == card && !h.grading) best = std::max(best, price_i(h, -1));
  if (best < 0) {
    Variant v;
    v.card = card;
    best = price_i(v, -1);
  }
  return best;
}

bool frame(double now) {
  std::lock_guard<std::mutex> lk(g_m);
  g_now = now;
  if (now - g_lastRead < 0.5) return false;
  const double since = now - g_lastRead;  // r238: (the hub's time toward a tick: at most 2 s a read - a pause never makes a burst)
  g_lastRead = now;
  load_set();
  const unsigned career = g_src.career != nullptr ? g_src.career() : 0;
  if (career == 0 || g_set.empty()) {
    if (g_mk.career != 0) {
      g_hist.clear(), g_histN = 0, g_histDirty = false, g_notices.clear(), g_salesRows.clear(), g_salesCard = -1;  // r238
      g_mk = Market();
      g_mag = MagMarket();  // r219
      g_heldOut.clear();  // (r213: the collector trades tell their own again)
      tell_listed();  // (r210: no career, nothing listed)
      ++g_gen;
      return true;
    }
    return false;
  }
  if (career != g_mk.career) {
    load_market(career);
    g_tab = 0;
    for (int& c : g_cur) c = 0;
    for (int& c : g_top) c = 0;
    g_ask = 0;
    g_countering = false;
    g_pad = false;
    g_heldOut.clear();  // (r213: the collector trades tell this career's own)
    read_holdings();
    tell_listed();  // (r210: the binder told this career's listings)
    sell_new_prices();  // r240: (this career's prices)
    mag_build();    // r219: its market spread from the file as it was (nothing new saved)
    ++g_gen;
  }
  const bool ok = g_src.franchise != nullptr ? g_src.franchise(&g_fs) : PortFranchiseMarketGet(&g_fs);
  read_holdings();
  bool changed = !g_hold.empty() && trim_mine();  // (r210: a listing whose copy is gone comes down - never on a binder not read yet)
  if (!g_mk.scale210 && g_mk.loaded) {  // r210: an r201-r209 market file - its portfolio chart rescaled to today's prices (its shape kept)
    g_mk.scale210 = true;
    if (!g_mk.portfolio.empty() && g_mk.portfolio.back() > 0) {
      const double k = static_cast<double>(portfolio_value(-1)) / static_cast<double>(g_mk.portfolio.back());
      for (long long& p : g_mk.portfolio) p = xrcoin::from_double(static_cast<double>(p) * k);
    }
    changed = true;
  }
  for (const Holding& h : g_hold) {  // (when each copy first showed up: offers wait a whole matchday)
    const std::string s = sig_of(h);
    if (!g_mk.firstSeen.count(s)) {
      g_mk.firstSeen[s] = g_mk.steps;
      changed = true;
    }
  }
  if (ok && g_fs.valid && g_fs.seed == career && g_mk.reprice231 && g_mk.steps > 0) {  // r231: an older file re-priced from the play
    reprice231(g_fs);
    changed = true;
  }
  if (ok && g_fs.valid && g_fs.seed == career && (g_fs.season != g_mk.season || g_fs.week != g_mk.week)) {
    advance(g_fs);
    changed = true;
  }
  bool ticked = false;
  // r238: THE LIVE MARKET - every kTickSecs of hub time (a career loaded, priced) a tick: your listings looked at, offers, the wire
  if (g_live.load() && g_mk.steps > 0 && g_mk.season >= 0 && since > 0.0) g_liveAcc += std::min(2.0, since);
  if (g_liveAcc >= kTickSecs) {
    g_liveAcc = 0.0;
    tick_now();
    save_market();  // (saved here: the tick's own - not through the cleanup below)
    ++g_gen;
    ticked = true;
  }
  if (g_saveDue) g_saveDue = false, changed = true;  // r235: (a recent search kept)
  if (g_rowsDirty && g_tab == kTabBuy) rows_build(), ++g_gen;  // r235: (the search's results, once after a change)
  if (g_salesDirty && g_tab == kTabSales) sales_rows_build(), ++g_gen;  // r238: (the SALES tab's players, once after a sale / a key)
  if (changed) {
    // (forget copies long gone: the file stays small)
    for (auto it = g_mk.firstSeen.begin(); it != g_mk.firstSeen.end();) {
      bool have = false;
      for (const Holding& h : g_hold) have = have || sig_of(h) == it->first;
      if (!have && it->second < g_mk.steps - 4) it = g_mk.firstSeen.erase(it);
      else ++it;
    }
    save_market();
    ++g_gen;
  }
  return changed || ticked;
}
int generation() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_gen;
}
// r206: the page is no longer drawn every frame (r201-r205 drew it 72 times a second - the crash): what changes with TIME alone
// asks for its few pictures here - the white flash after a deal fading (0.5 s: 8 a second), the foot's last word going back to
// the wire after 6 s (once). True (and the generation moved) when the page should be drawn again now.
bool redraw_due() {
  std::lock_guard<std::mutex> lk(g_m);
  static double s_last = -1e9;
  static bool s_fresh = false;
  bool due = false;
  const double fl = g_now - g_flashAt;
  if (fl >= 0.0 && fl < 0.62 && g_now - s_last >= 0.12) due = true;
  const bool fresh = g_now - g_msgAt < 6.0;
  if (fresh != s_fresh) {
    s_fresh = fresh;
    due = true;
  }
  if (g_pointDirty) {  // r210: a pointer moved the pad's cursor / pressed a key (point() runs on the render thread)
    g_pointDirty = false;
    due = true;
  }
  const double ph = g_now - g_padHitAt;  // r210: a pad key's press flash (0.25 s)
  if (g_pad && ph >= 0.0 && ph < 0.3 && g_now - s_last >= 0.08) due = true;
  if (due) {
    s_last = g_now;
    ++g_gen;
  }
  return due;
}
int offers_waiting() {
  std::lock_guard<std::mutex> lk(g_m);
  return static_cast<int>(g_mk.offers.size()) + g_mk.unseenSales + g_unseenAlerts;  // (r235: + your watch list's alerts)
}

// ============================================================================================== the page's buttons
namespace {
// r210: the number pad's keys (page units): 3 x 4 digit keys, OK under them; the panel round them
constexpr float kPadX0 = 430, kPadX1 = 1220, kPadY0 = 196, kPadY1 = 892;
void pad_key_box(int k, float* x0, float* y0, float* x1, float* y1) {
  if (k >= 12) {
    *x0 = 640, *x1 = 1178, *y0 = 824, *y1 = 878;
    return;
  }
  const int r = k / 3, c = k % 3;
  *x0 = 640 + c * 182.0f, *x1 = *x0 + 174, *y0 = 590 + r * 58.0f, *y1 = *y0 + 52;
}
const char* const kPadKey[13] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "CLR", "0", "DEL", "OK"};
long long pad_value() {
  xrcoin::Coins v = 0;
  if (!g_padDigits.empty()) xrcoin::parse(g_padDigits.c_str(), &v);
  return v;
}
void pad_say(const std::string& s) {
  g_padSay = s;
  g_padSayAt = g_now;
}
// open it: a new listing for `v` (its suggestion: the market, rounded), or `edit` a listing's price (its own)
void pad_open_for(const Variant& v, uint32_t edit, long long start) {
  g_pad = true;
  g_padV = v;
  g_padEdit = edit;
  g_padDigits = std::to_string(std::max(1LL, start));
  g_padFresh = true;
  g_padKey = 12;
  g_padSay.clear();
  g_padSayAt = -1e9;
  g_padHover = -1;
}
long long round_nice(long long p) {  // (a suggestion: 3 figures, e.g. 48,317 -> 48,300)
  long long m = 1;
  while (p / m >= 1000) m *= 10;
  return std::max(1LL, (p / m) * m);
}
// a key pressed (A on it, or a pointer's trigger): digits / CLR / DEL / OK
void pad_press(int k) {
  g_padHit = k;
  g_padHitAt = g_now;
  if (k >= 0 && k <= 10 && k != 9) {
    const char d = k == 10 ? '0' : static_cast<char>('1' + k);
    if (g_padFresh) g_padDigits.clear();
    g_padFresh = false;
    if (g_padDigits.size() >= 15) {
      pad_say("15 DIGITS AT MOST");
      sound("book_tick", 0.4f);
      return;
    }
    if (!(g_padDigits.empty() && d == '0')) g_padDigits += d;
    sound("book_tick", 0.6f);
  } else if (k == 9) {
    g_padDigits.clear();
    g_padFresh = false;
    sound("book_tick", 0.6f);
  } else if (k == 11) {
    if (g_padFresh) g_padDigits.clear();
    g_padFresh = false;
    if (!g_padDigits.empty()) g_padDigits.pop_back();
    sound("book_tick", 0.6f);
  } else if (k >= 12) {
    const long long v = pad_value();
    if (v < 1) {
      pad_say("TYPE YOUR PRICE - 1 COIN AT LEAST");
      return;
    }
    if (g_padEdit != 0) {
      Mine* m = mine_by(g_padEdit);
      if (m != nullptr) {
        m->ask = v;
        say("NEW PRICE: " + copy_words(m->v) + " - " + coins_text(v) + " COINS", 0x2EF2FF);
        sound("book_ding", 0.7f);
      }
    } else {
      if (do_list(g_padV, v) == 0) {
        pad_say(static_cast<int>(g_mk.mine.size()) >= kMaxMine ? "20 LISTINGS AT ONCE - TAKE ONE DOWN FIRST" : "THAT COPY CAN'T BE LISTED NOW");
        return;
      }
      say("LISTED: " + copy_words(g_padV) + " FOR " + coins_text(v) + " COINS - COLLECTORS LOOK EVERY MATCHDAY", 0x2EF2FF);
      sound("book_ding", 0.8f);
    }
    g_pad = false;
    save_market();
  }
}
void pad_move(int dir) {
  int k = g_padKey;
  if (k >= 12) {
    if (dir == 3) k = 10;
  } else {
    const int r = k / 3, c = k % 3;
    if (dir == 1 && c > 0) --k;
    else if (dir == 2 && c < 2) ++k;
    else if (dir == 3 && r > 0) k -= 3;
    else if (dir == 4) k = r < 3 ? k + 3 : 12;
  }
  if (k != g_padKey) {
    g_padKey = k;
    sound("book_tick", 0.4f);
  }
}
// ---------------------------------------------------------------------------------------- r235: THE SEARCH (the BUY tab)
float chg_of_v(const Variant& v) {  // (this matchday's move: the drawing's chg_of)
  const int n = static_cast<int>(g_mk.days.size());
  if (n < 2) return 0.0f;
  const double a = price_d(v, -1), b = price_d(v, n - 2);
  return static_cast<float>(100.0 * (a - b) / std::max(0.01, b));
}
// its name without the set's word ("JAM '96 TOGEPI" -> "TOGEPI"): what you type matches
std::string search_name(int card) {
  std::string nm = name_of_card(card);
  if (nm.compare(0, 8, "JAM '96 ") == 0) nm = nm.substr(8);
  if (nm.compare(0, 10, "EXQUISITE ") == 0) nm = nm.substr(10);
  return nm;
}
// a word of the name starts with the query ("TOG" -> TOGEPI, "MAR" -> MARIO, MARTH); from 2 letters anywhere in it too
bool name_match(const std::string& nm, const std::string& q) {
  if (q.empty()) return true;
  for (size_t at = 0; at < nm.size();) {
    if (nm.compare(at, q.size(), q) == 0) return true;
    const size_t sp = nm.find(' ', at);
    if (sp == std::string::npos) break;
    at = sp + 1;
  }
  return q.size() >= 2 && nm.find(q) != std::string::npos;
}
int set_of(const Variant& v) { return v.card >= 0 && v.card < static_cast<int>(g_set.size()) ? g_set[static_cast<size_t>(v.card)].set : 0; }
bool finish_ok(const Variant& v, int f) {
  const bool x = exq_v(v);
  const int t = x ? exq_t(v) : 0;
  switch (f) {
    case kFFinBase: return !x && v.parallel == kBase && v.hit == kHitNone;
    case kFFinSilver: return x ? t == xrexq::kSilverLeaf : v.parallel == kSilver;
    case kFFinGold: return x ? t == xrexq::kGoldLeaf : v.parallel == kGoldTizm;
    case kFFinNumbered: return x || v.parallel >= kGoldTizm;
    case kFFinOne: return v.parallel == kBlack || t == xrexq::kMasterpiece;
    case kFFinAuto: return v.hit == kHitSigned || v.parallel == kBlack;
    case kFFinPatch: return t == xrexq::kPatch || t == xrexq::kRpa || t == xrexq::kPatchAuto || t == xrexq::kMasterpiece;
    default: return true;
  }
}
bool price_ok(long long ask, long long market, int f) {
  static const long long kUnder[kFPrices] = {0, 1000, 10000, 100000, 1000000, 10000000, 0};
  if (f == kFPriceAny) return true;
  if (f == kFPriceUnder) return ask < market;
  return ask < kUnder[f];
}
const char* const kSetWord[kFSets] = {"ALL SETS", "LANCINI TIZM", "JAM '96", "EXQUISITE"};
const char* const kFinWord[kFFins] = {"ANY FINISH", "BASE", "SILVER", "GOLD", "NUMBERED", "1/1", "AUTO", "PATCH"};
const char* const kPriceWord[kFPrices] = {"ANY PRICE", "UNDER 1K", "UNDER 10K", "UNDER 100K", "UNDER 1M", "UNDER 10M", "UNDER MARKET"};
const char* const kSortWord[kSorts] = {"PRICE LOW-HIGH", "PRICE HIGH-LOW", "NEWEST", "TRENDING", "BEST DEAL"};  // (r236: PLAYER HOT -> TRENDING: TOP CARDS as a sort)
// every copy for sale that matches: the dealer's matchday listings (BUY at 112%) + the collectors' / the dealer's case's (their asks) -
// made on change only (a key, a filter, the index pushed, a matchday, a buy), at most 300 (g_m held)
void rows_build() {
  g_rowsDirty = false;
  g_rows.clear();
  if (g_mk.career == 0 || g_set.empty()) return;
  std::string q = g_q;
  while (!q.empty() && q.back() == ' ') q.pop_back();
  // r239: the FAMILY chip on - the families (line ids) one of whose forms the query finds (made once a build: a few hundred names)
  std::vector<std::string> famHit;
  if (g_fFam && !q.empty())
    for (const auto& kv : g_byLine)
      for (int f : kv.second)
        if (g_set[static_cast<size_t>(f)].set == 0 && name_match(search_name(f), q)) {
          famHit.push_back(kv.first);
          break;
        }
  auto name_ok = [&](int card) {
    if (name_match(search_name(card), q)) return true;
    if (famHit.empty()) return false;
    const std::string& ln = g_set[static_cast<size_t>(card)].line;
    return !ln.empty() && std::find(famHit.begin(), famHit.end(), ln) != famHit.end();
  };
  auto take = [&](SearchRow& r) {
    if (r.v.card < 0 || r.v.card >= static_cast<int>(g_set.size())) return;
    if (g_fSet != kFSetAll && set_of(r.v) != g_fSet - 1) return;
    if (!finish_ok(r.v, g_fFin) || !name_ok(r.v.card)) return;
    r.market = price_i(r.v, -1);
    if (r.src == 0) r.ask = ask_of(r.market);
    if (!price_ok(r.ask, r.market, g_fPrice)) return;
    const Line* l = line_of(r.v.card);
    r.rank = l != nullptr ? l->rank : 0;
    r.chg = chg_of_v(r.v);
    g_rows.push_back(r);
  };
  for (size_t i = 0; i < g_mk.listings.size(); ++i) {
    if (g_mk.listings[i].sold) continue;
    SearchRow r;
    r.src = 0, r.id = static_cast<uint32_t>(i), r.seller = 20, r.who = "THE DEALER", r.rgb = 0xFFD23F, r.v = g_mk.listings[i].v, r.day = g_mk.steps;
    take(r);
  }
  if (g_offCareer == g_mk.career)
    for (const Offering& o : g_offs) {
      SearchRow r;
      r.src = 1, r.id = o.id, r.seller = o.seller, r.who = o.who, r.rgb = o.rgb, r.v = o.v, r.ask = o.ask, r.day = o.day;
      take(r);
    }
  auto key = [](const SearchRow& r) { return static_cast<double>(r.ask) / static_cast<double>(std::max(1LL, r.market)); };
  std::stable_sort(g_rows.begin(), g_rows.end(), [&](const SearchRow& a, const SearchRow& b) {
    switch (g_fSort) {
      case kSortHigh: if (a.ask != b.ask) return a.ask > b.ask; break;
      case kSortNew: if (a.day != b.day) return a.day > b.day; break;
      case kSortHot: {
        const int ra = a.rank > 0 ? a.rank : 99999, rb = b.rank > 0 ? b.rank : 99999;
        if (ra != rb) return ra < rb;
        if (a.chg != b.chg) return a.chg > b.chg;
        break;
      }
      case kSortDeal: if (key(a) != key(b)) return key(a) < key(b); break;
      default: break;
    }
    if (a.ask != b.ask) return a.ask < b.ask;
    return a.id < b.id;
  });
  if (g_rows.size() > 300) g_rows.resize(300);
  g_cur[kTabBuy] = std::max(0, std::min(static_cast<int>(g_rows.size()) - 1, g_cur[kTabBuy]));
}
// a WATCHED player's copy listed under its market: once per listing - a headline, the foot's word, the MARKET rail's badge (g_m held)
void watch_check() {
  if (g_watch.empty() || g_mk.career == 0) return;
  auto watched_line = [](int card) {
    if (card < 0 || card >= static_cast<int>(g_set.size())) return false;
    return std::find(g_watch.begin(), g_watch.end(), g_set[static_cast<size_t>(card)].line) != g_watch.end();
  };
  auto alert = [&](uint32_t key, const Variant& v, const std::string& who, long long ask) {
    if (!watched_line(v.card)) return;
    for (uint32_t a : g_alerted)  // (a plain loop: libc++ turns std::find on 32-bit ints into wmemchr)
      if (a == key) return;
    const long long mk = price_i(v, -1);
    if (ask >= mk) return;
    g_alerted.push_back(key);
    if (g_alerted.size() > 200) g_alerted.erase(g_alerted.begin());
    const int pct = static_cast<int>(std::lround(100.0 * static_cast<double>(ask) / static_cast<double>(std::max(1LL, mk))));
    headline("WATCH: " + who + " LISTED " + copy_words(v) + " FOR " + coins_text(ask) + " - " + std::to_string(pct) + "% OF THE MARKET (BUY)", 0x2EF2FF, false);
    say("WATCH LIST: " + copy_words(v) + " AT " + std::to_string(pct) + "% OF THE MARKET - MARKET > BUY", 0x2EF2FF);
    ++g_unseenAlerts;
  };
  for (size_t i = 0; i < g_mk.listings.size(); ++i)
    if (!g_mk.listings[i].sold) alert(0x80000000u ^ static_cast<uint32_t>(g_mk.steps * 16 + static_cast<int>(i)), g_mk.listings[i].v, "THE DEALER", ask_of(price_i(g_mk.listings[i].v, -1)));
  if (g_offCareer == g_mk.career)
    for (const Offering& o : g_offs) alert(o.id, o.v, o.who, o.ask);
}
// ---- r240: SELL, FILTERED (g_m held, every one of these)
// what SELL's list is made from - your copies, the prices' epoch, your listings, your open trades, your museum: a few multiplies a copy (no
// strings, no prices), so asking "anything changed?" on a key press / a picture costs microseconds
uint64_t vhash(uint64_t h, const Variant& v) {
  h = mix64(h ^ (static_cast<uint64_t>(v.card + 1) * 0x9E3779B97F4A7C15ull));
  h = mix64(h ^ (static_cast<uint64_t>(v.parallel & 15) | (v.shiny ? 16ull : 0ull) | (static_cast<uint64_t>(v.hit & 15) << 5) |
                 (static_cast<uint64_t>(static_cast<uint32_t>(v.serial)) << 9)));
  return mix64(h ^ (static_cast<uint64_t>(v.grade & 31) << 41) ^ (static_cast<uint64_t>(v.cert) * 0xC2B2AE3D27D4EB4Full));
}
uint64_t sell_key() {
  uint64_t h = mix64(static_cast<uint64_t>(g_mk.career) ^ (static_cast<uint64_t>(g_priceEpoch) << 32) ^ 0x240ull);
  for (const Holding& x : g_hold)  // (r242: + a slab's matches to go - "AT GRADING - 1 MATCH" the moment one is played)
    h = mix64(vhash(h, x) ^ (static_cast<uint64_t>(x.count) << 1) ^ (x.grading ? 1ull : 0ull) ^ (static_cast<uint64_t>(x.left & 255) << 33));
  h = mix64(h ^ 0x51ull);
  for (const Mine& m : g_mk.mine) h = vhash(h, m.v);
  h = mix64(h ^ 0x52ull);
  for (const Variant& v : g_heldOut) h = vhash(h, v);
  h = mix64(h ^ 0x53ull);
  for (const Variant& v : g_museumV) h = vhash(h, v);
  return h;
}
// a CARD for "your last copy": the card + its finish (parallel, shiny, case hit / an EXQUISITE tier) - its number, grade, cert aside
uint64_t kind_of(const Variant& v) {
  const uint64_t t = exq_v(v) ? static_cast<uint64_t>(exq_t(v)) : 0ull;
  return (static_cast<uint64_t>(v.card + 1) << 24) | (static_cast<uint64_t>(v.parallel & 7) << 20) | (v.shiny ? (1ull << 19) : 0ull) |
         (static_cast<uint64_t>(v.hit & 7) << 16) | t;
}
// a copy's price + this matchday's move, kept for the epoch (a matchday's prices: one read a kind of copy, not one a key press)
void price_c(const Variant& v, const std::string& sig, long long* p, float* chg) {
  auto it = g_pcache.find(sig);
  if (it == g_pcache.end() || it->second.epoch != g_priceEpoch) {
    PriceC c;
    c.epoch = g_priceEpoch;
    c.p = price_i(v, -1);
    c.chg = chg_of_v(v);
    it = g_pcache.insert_or_assign(sig, c).first;
  }
  *p = it->second.p;
  *chg = it->second.chg;
}
void sell_new_prices() {
  ++g_priceEpoch;
  g_pcache.clear();  // (copies long gone go with it: the cache never outgrows your binder)
  if (g_mk.career == 0 || g_set.empty()) return;
  long long p = 0;
  float c = 0.0f;
  for (const Holding& h : g_hold)
    if (h.card >= 0 && h.card < static_cast<int>(g_set.size())) price_c(h, sig_of(h), &p, &c);
}
// every kind of copy you have, with its numbers - on a change only (a sale, a pull, a listing, a matchday's prices)
void sell_base_build() {
  g_sell.clear();
  g_sellKey = sell_key();
  g_sellRowsDirty = true;
  g_gradeRowsDirty = true;  // r241: (GRADING's rows come from this table too)
  if (g_mk.career == 0 || g_set.empty()) return;
  std::unordered_map<std::string, int> nList, nOut, nMus;  // (copies still to place: a sig can be on two holdings - a GOLD with no number)
  for (const Mine& m : g_mk.mine) ++nList[sig_of(m.v)];
  for (const Variant& v : g_heldOut) ++nOut[sig_of(v)];
  for (const Variant& v : g_museumV) ++nMus[sig_of(v)];
  auto take = [](std::unordered_map<std::string, int>& m, const std::string& s, int most) {
    auto f = m.find(s);
    if (f == m.end() || f->second <= 0 || most <= 0) return 0;
    const int n = std::min(most, f->second);
    f->second -= n;
    return n;
  };
  g_sell.reserve(g_hold.size());
  for (const Holding& h : g_hold) {
    if (h.card < 0 || h.card >= static_cast<int>(g_set.size()) || h.count <= 0) continue;
    SellItem s;
    s.h = h;
    s.sig = sig_of(h);
    s.name = search_name(h.card);
    s.kind = kind_of(h);
    price_c(h, s.sig, &s.market, &s.chg);
    s.bid = bid_of(s.market);
    if (!h.grading) {  // (a copy at TIZM GRADING: nothing free - it isn't back)
      s.listed = take(nList, s.sig, h.count);
      s.heldOut = take(nOut, s.sig, h.count - s.listed);
      s.museum = take(nMus, s.sig, h.count - s.listed - s.heldOut);
      s.free = h.count - s.listed - s.heldOut - s.museum;
    }
    s.set = set_of(h);
    s.tier = exq_v(h) ? exq_t(h) : 0;
    auto fs = g_mk.firstSeen.find(s.sig);
    s.seen = fs != g_mk.firstSeen.end() ? fs->second : g_mk.steps;
    if (const Line* l = line_of(h.card)) s.rank = l->rank;
    if (s.tier > 0) s.serialKey = xrexq::num_of(h.serial);
    else if (h.parallel == kBlack) s.serialKey = 1;
    else if (h.parallel == kGoldTizm && h.serial > 0) s.serialKey = h.serial;
    g_sell.push_back(std::move(s));
  }
  // YOUR LAST COPY of each card: one that stays anyway (in your museum, at grading) keeps it; else the most valuable free copy is held back
  struct KindN {
    int stays = 0, best = -1;
  };
  std::unordered_map<uint64_t, KindN> kinds;
  for (size_t i = 0; i < g_sell.size(); ++i) {
    const SellItem& s = g_sell[i];
    KindN& k = kinds[s.kind];
    k.stays += s.museum + (s.h.grading ? s.h.count : 0);
    if (s.free > 0 && (k.best < 0 || s.market > g_sell[static_cast<size_t>(k.best)].market)) k.best = static_cast<int>(i);
  }
  for (size_t i = 0; i < g_sell.size(); ++i) {
    SellItem& s = g_sell[i];
    const KindN& k = kinds[s.kind];
    s.spare = s.free - (k.stays == 0 && k.best == static_cast<int>(i) ? 1 : 0);
  }
}
bool sfin_ok(const SellItem& s, int f) {
  const bool x = s.set == 2;
  const int t = s.tier;
  switch (f) {
    case kSFinAny: return true;
    case kSFinBase: return !x && s.h.parallel == kBase && s.h.hit == kHitNone;
    case kSFinSilver: return !x && s.h.parallel == kSilver;
    case kSFinGold: return !x && s.h.parallel == kGoldTizm;
    case kSFin25: return x ? xrexq::kRun[t] <= 25 : s.h.parallel >= kGoldTizm;  // (/25 and under: GOLD /10, BLACK 1/1, EMERALD, SAPPHIRE, PATCH /25, PATCH AUTO /10, 1/1)
    case kSFinOne: return x ? t == xrexq::kMasterpiece : s.h.parallel == kBlack;
    default: return x && t == f - kSFinExq + 1;
  }
}
bool stype_ok(const SellItem& s, int f) {
  const Holding& v = s.h;
  const bool x = s.set == 2;
  const int t = s.tier;
  switch (f) {
    case kSTypeBase: return !x && v.parallel == kBase && v.hit == kHitNone && !v.shiny && v.grade == 0;
    case kSTypeParallel: return x ? (t >= xrexq::kSilverLeaf && t <= xrexq::kSapphire) : v.parallel != kBase;
    case kSTypeHit: return !x && v.hit != kHitNone;  // (TIZM BOOM / HOMETOWN / SIGNED)
    case kSTypeAuto: return x ? xrexq::kSigned[t] : (v.hit == kHitSigned || v.parallel == kBlack);  // (every BLACK 1/1 is signed)
    case kSTypePatch: return x && xrexq::kPatchOn[t];
    case kSTypeGraded: return v.grade > 0;
    case kSTypeShiny: return v.shiny;
    case kSTypeRookie: {  // a family's FIRST form (r239: his rookie card) / an EXQUISITE rookie
      const CardInfo& b = main_info(v.card);
      return (b.stages > 1 && b.stage == 0) || exq_rookie_v(v);
    }
    default: return true;
  }
}
bool sstat_ok(const SellItem& s, int f) {
  if (f == kSStatFree) return s.free > 0;
  if (f == kSStatListed) return s.listed > 0;
  if (f == kSStatSpare) return s.spare > 0;
  return true;
}
bool sprice_ok(long long m, int f) {
  static const long long kLo[kSPrices] = {0, 0, 100, 1000, 10000, 100000, 1000000};
  static const long long kHi[kSPrices] = {0, 100, 1000, 10000, 100000, 1000000, 0};
  if (f <= kSPriceAny || f >= kSPrices) return true;
  return m >= kLo[f] && (kHi[f] == 0 || m < kHi[f]);
}
// the shown rows: the query (+ FAMILY), the filters, the sort - no price read, no string made a copy (instant on a 1,000-copy binder)
void sell_rows_build() {
  g_sellRowsDirty = false;
  g_sellRows.clear();
  std::string q = g_sq;
  while (!q.empty() && q.back() == ' ') q.pop_back();
  std::vector<std::string> famHit;  // (r239's FAMILY chip, as BUY's)
  if (g_sfFam && !q.empty())
    for (const auto& kv : g_byLine)
      for (int f : kv.second)
        if (g_set[static_cast<size_t>(f)].set == 0 && name_match(search_name(f), q)) {
          famHit.push_back(kv.first);
          break;
        }
  for (size_t i = 0; i < g_sell.size(); ++i) {
    const SellItem& s = g_sell[i];
    if (g_sfSet != kFSetAll && s.set != g_sfSet - 1) continue;
    if (!sfin_ok(s, g_sfFin) || !stype_ok(s, g_sfType) || !sstat_ok(s, g_sfStat) || !sprice_ok(s.market, g_sfPrice)) continue;
    if (!q.empty() && !name_match(s.name, q)) {
      const std::string& ln = g_set[static_cast<size_t>(s.h.card)].line;
      bool fam = false;
      for (const std::string& f : famHit) fam = fam || (!ln.empty() && f == ln);
      if (!fam) continue;
    }
    g_sellRows.push_back(static_cast<int>(i));
  }
  std::stable_sort(g_sellRows.begin(), g_sellRows.end(), [](int ia, int ib) {
    const SellItem& a = g_sell[static_cast<size_t>(ia)];
    const SellItem& b = g_sell[static_cast<size_t>(ib)];
    switch (g_sfSort) {
      case kSSortLow: if (a.market != b.market) return a.market < b.market; break;
      case kSSortUp: if (a.chg != b.chg) return a.chg > b.chg; break;
      case kSSortDown: if (a.chg != b.chg) return a.chg < b.chg; break;
      case kSSortNew: if (a.seen != b.seen) return a.seen > b.seen; break;
      case kSSortHot: {
        const int ra = a.rank > 0 ? a.rank : 99999, rb = b.rank > 0 ? b.rank : 99999;
        if (ra != rb) return ra < rb;
        if (a.chg != b.chg) return a.chg > b.chg;
        break;
      }
      case kSSortSerial: if (a.serialKey != b.serialKey) return a.serialKey < b.serialKey; break;
      default: break;
    }
    if (a.market != b.market) return a.market > b.market;
    return ia < ib;
  });
  const int n = static_cast<int>(g_sellRows.size());
  g_cur[kTabMine] = std::max(0, std::min(std::max(0, n - 1), g_cur[kTabMine]));
}
void sell_fresh() {
  if (sell_key() != g_sellKey) sell_base_build();
  if (g_sellRowsDirty) sell_rows_build();
}
const SellItem* sell_at(int row) {  // (sell_fresh first)
  if (row < 0 || row >= static_cast<int>(g_sellRows.size())) return nullptr;
  return &g_sell[static_cast<size_t>(g_sellRows[static_cast<size_t>(row)])];
}
// ---- r241: CARDS > GRADING's rows (g_m held, every one of these) - SELL's table (every kind of copy you have: one row each, its own finish /
// number / grade), GRADING's own filters + sort. r152-r240's page showed ONE tile a card (grade_pick's "finest" copy - no choosing) and drew an
// EXQUISITE numbered copy's tile as a card you don't have (a dark silhouette + its checklist No.: Lance's EMERALD 19/25 IRON MAN III was "#069")
int grade_cost_of(const Variant& v) { return g_src.grade_cost != nullptr ? g_src.grade_cost(v) : 0; }
// why a row can't go to TIZM GRADING now ("" it can): the copy itself first (at grading / in a slab), then its copies' locks
std::string grade_lock(const SellItem& s) {
  if (s.h.grading) return "AT GRADING";
  if (s.h.grade > 0) return "GRADED";
  if (s.free > 0) return "";
  if (s.listed > 0) return "LISTED";
  if (s.museum > 0) return "IN MUSEUM";
  if (s.heldOut > 0) return "IN A TRADE";
  return "LOCKED";
}
std::string grade_lock_say(const SellItem& s) {  // (A on a locked row: why, and what to do)
  const std::string l = grade_lock(s);
  if (l == "AT GRADING") return "IT'S " + grading_words(s.h) + " TO GO - GRADED WHEN IT'S BACK";  // r242
  if (l == "GRADED") return "IT'S GRADED ALREADY - SEALED IN ITS SLAB";
  if (l == "LISTED") return "IT'S LISTED - TAKE THE LISTING DOWN FIRST (MARKET > LISTED)";
  if (l == "IN MUSEUM") return "IT'S IN YOUR MUSEUM - TAKE IT OUT FIRST (CARDS > MUSEUM)";
  if (l == "IN A TRADE") return "IT'S IN AN OPEN COLLECTOR TRADE - ANSWER IT FIRST (CARDS > COLLECTORS)";
  return "IT CAN'T GO RIGHT NOW";
}
int grade_finish_score(const SellItem& s) {  // (SORT: FINISH - the rarest first: an EXQUISITE tier by its pack rank, then 1/1, case hit, GOLD, SILVER)
  if (s.set == 2) return 1000 + xrexq::rank(s.tier) * 10;
  const int tier = s.h.parallel == kBlack ? 4 : (s.h.hit != kHitNone ? 3 : (s.h.parallel == kGoldTizm ? 2 : (s.h.parallel == kSilver ? 1 : 0)));
  return tier * 100 + (s.h.shiny ? 10 : 0) + (s.set == 1 ? 5 : 0);
}
int gfin_next(int f) {  // (GRADING's FINISH chip: the next value its SET chip can have - as SELL's)
  for (int k = 0; k < kSFins; ++k) {
    f = (f + 1) % kSFins;
    if (f == kSFinAny || f == kSFin25 || f == kSFinOne) return f;
    const bool exqTier = f >= kSFinExq;
    if (g_gfSet == kFSetExq ? exqTier : (g_gfSet == kFSetAll || !exqTier)) return f;
  }
  return kSFinAny;
}
void grade_rows_build() {
  g_gradeRowsDirty = false;
  g_gradeRows.clear();
  std::string q = g_gq;
  while (!q.empty() && q.back() == ' ') q.pop_back();
  std::vector<std::string> famHit;  // (the FAMILY chip, as SELL's: every form of a family a form of which the name finds)
  if (g_gfFam && !q.empty())
    for (const auto& kv : g_byLine)
      for (int f : kv.second)
        if (g_set[static_cast<size_t>(f)].set == 0 && name_match(search_name(f), q)) {
          famHit.push_back(kv.first);
          break;
        }
  for (size_t i = 0; i < g_sell.size(); ++i) {
    const SellItem& s = g_sell[i];
    if (g_gfSet != kFSetAll && s.set != g_gfSet - 1) continue;
    if (!sfin_ok(s, g_gfFin) || !stype_ok(s, g_gfType)) continue;
    const bool can = grade_lock(s).empty();
    if ((g_gfStat == kGStatCan && !can) || (g_gfStat == kGStatLocked && can)) continue;
    if (!q.empty() && !name_match(s.name, q)) {
      const std::string& ln = g_set[static_cast<size_t>(s.h.card)].line;
      bool fam = false;
      for (const std::string& f : famHit) fam = fam || (!ln.empty() && f == ln);
      if (!fam) continue;
    }
    g_gradeRows.push_back(static_cast<int>(i));
  }
  // (every sort: the copies that can go first - a locked one's row stays on the list, its reason on it, under them)
  std::vector<char> can(g_sell.size(), 0);
  for (int i : g_gradeRows) can[static_cast<size_t>(i)] = grade_lock(g_sell[static_cast<size_t>(i)]).empty() ? 1 : 0;
  std::stable_sort(g_gradeRows.begin(), g_gradeRows.end(), [&can](int ia, int ib) {
    const SellItem& a = g_sell[static_cast<size_t>(ia)];
    const SellItem& b = g_sell[static_cast<size_t>(ib)];
    if (can[static_cast<size_t>(ia)] != can[static_cast<size_t>(ib)]) return can[static_cast<size_t>(ia)] > can[static_cast<size_t>(ib)];
    switch (g_gfSort) {
      case kGSortLow: if (a.market != b.market) return a.market < b.market; break;
      case kGSortFinish: {
        const int fa = grade_finish_score(a), fb = grade_finish_score(b);
        if (fa != fb) return fa > fb;
        break;
      }
      case kGSortNew: if (a.seen != b.seen) return a.seen > b.seen; break;
      default: break;
    }
    if (a.market != b.market) return a.market > b.market;
    return ia < ib;
  });
  const int n = static_cast<int>(g_gradeRows.size());
  g_gCur = std::max(0, std::min(std::max(0, n - 1), g_gCur));
}
void grade_fresh() {
  if (sell_key() != g_sellKey) sell_base_build();
  if (g_gradeRowsDirty) grade_rows_build();
}
const SellItem* grade_at(int row) {  // (grade_fresh first)
  if (row < 0 || row >= static_cast<int>(g_gradeRows.size())) return nullptr;
  return &g_sell[static_cast<size_t>(g_gradeRows[static_cast<size_t>(row)])];
}
// A on a row: a locked one says why; a free one asks (the hub's confirm: g_ask 8 - the copy, its cost)
void grade_ask_row(int row) {
  grade_fresh();
  const SellItem* s = grade_at(row);
  if (s == nullptr) return;
  const std::string lk = grade_lock(*s);
  if (!lk.empty()) {
    say(grade_lock_say(*s), 0xFFB020);
    sound("book_tick", 0.4f);
    return;
  }
  const int cost = grade_cost_of(s->h);
  if (balance() < cost) {
    say("NOT ENOUGH BIT COINS - " + coins_text(cost) + " TO GRADE IT", 0xFF4D5E);
    sound("book_tick", 0.4f);
    return;
  }
  g_gradeAskV = s->h;
  g_gradeAskV.grade = 0, g_gradeAskV.cert = 0;
  g_gradeAskCost = cost;
  g_ask = 8;
  g_gradeAskA = true;
  sound("book_ding", 0.5f);
}
// YES: THAT copy sent (xr_cards seals it: the market's lock -> the binder's, as take) - still free (a tick / a matchday in between can list or
// sell it: then nothing happens and it says so); your copies read again, the cursor stays (the next row is under it: send another)
void grade_yes() {
  g_ask = 0;
  g_gradeAskA = false;
  grade_fresh();
  const Variant v = g_gradeAskV;
  const SellItem* s = nullptr;
  const std::string sig = sig_of(v);
  for (const SellItem& it : g_sell)
    if (it.sig == sig) s = &it;
  if (s == nullptr) {
    say("THAT COPY ISN'T IN YOUR BINDER ANY MORE", 0xFF4D5E);
    return;
  }
  if (!grade_lock(*s).empty()) {
    say(grade_lock_say(*s), 0xFFB020);
    return;
  }
  std::vector<std::string> next;  // (the rows from the cursor on, as shown: after a send the cursor goes to the first of them that can still go)
  for (size_t i = static_cast<size_t>(std::max(0, g_gCur)); i < g_gradeRows.size(); ++i) next.push_back(g_sell[static_cast<size_t>(g_gradeRows[i])].sig);
  uint32_t cert = 0;
  const int r = g_src.grade_send != nullptr ? g_src.grade_send(v, &cert) : 2;
  if (r == 0) {
    say("SENT: " + upper(variant_words(v)) + " " + name_of_card(v.card) + " - CERT #" + std::to_string(cert) + " - BACK AFTER " + std::to_string(kGradeBackMatches) + " MATCHES",
        0x39FF88);
    sound("book_collect", 0.8f);
    read_holdings();
    for (size_t i = 0; i < g_mk.offers.size();)  // (offers on a copy no longer free go, as a sale's)
      if (offer_gone(g_mk.offers[i])) g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
      else ++i;
    g_flashAt = g_now;
    g_rowsDirty = true;
    save_market();
  } else if (r == 1) {
    say("NOT ENOUGH BIT COINS - " + coins_text(g_gradeAskCost) + " TO GRADE IT", 0xFF4D5E);
  } else if (r == 3) {
    say("IT'S LOCKED - LISTED, IN A TRADE OR IN YOUR MUSEUM", 0xFFB020);
  } else {
    say("THAT COPY ISN'T IN YOUR BINDER ANY MORE", 0xFF4D5E);
  }
  g_gradeRowsDirty = true;
  grade_fresh();
  if (r == 0) {  // send another: the cursor on the same kind of copy if one more can go, else the next one shown that can
    for (const std::string& sg : next) {
      int at = -1;
      for (size_t i = 0; i < g_gradeRows.size() && at < 0; ++i) {
        const SellItem& s2 = g_sell[static_cast<size_t>(g_gradeRows[i])];
        if (s2.sig == sg && grade_lock(s2).empty()) at = static_cast<int>(i);
      }
      if (at >= 0) {
        g_gCur = at;
        break;
      }
    }
  }
}
// SELL ALL SHOWN / LIST ALL SHOWN: from the shown rows in their order - each row's free copies (LAST COPY: SELL TOO on) or its spares (off);
// a list: at most the listings that fit (kMaxMine at once), the first shown first
struct Plan {
  std::vector<int> item, n;
  BulkPlan b;
};
Plan bulk_plan(bool list, int pct, bool lastToo) {
  Plan p;
  int room = list ? std::max(0, kMaxMine - static_cast<int>(g_mk.mine.size())) : 1 << 30;
  for (int idx : g_sellRows) {
    const SellItem& s = g_sell[static_cast<size_t>(idx)];
    p.b.locked += s.h.count - s.free;
    int n = lastToo ? s.free : s.spare;
    p.b.kept += s.free - n;
    if (list) {
      const int fit = std::min(n, room);
      p.b.capped += n - fit;
      n = fit;
      room -= fit;
    }
    if (n <= 0) continue;
    p.item.push_back(idx);
    p.n.push_back(n);
    p.b.copies += n;
    ++p.b.rows;
    const long long each = list ? std::max(1LL, static_cast<long long>(llr(static_cast<double>(s.market) * pct / 100.0))) : s.bid;
    for (int k = 0; k < n; ++k) p.b.total = xrcoin::add(p.b.total, each), p.b.market = xrcoin::add(p.b.market, s.market);
  }
  return p;
}
// SELL ALL SHOWN > YES: every planned copy out of the binder at once (take_many: the binder saved once), SELL NOW's 90% each, one deposit
int do_sell_all(const Plan& p, long long* got) {
  if (got != nullptr) *got = 0;
  if (g_mk.career == 0 || g_src.add == nullptr || (g_src.take == nullptr && g_src.take_many == nullptr) || p.item.empty()) return 0;
  std::vector<Variant> copies;
  std::vector<long long> pay;
  for (size_t i = 0; i < p.item.size(); ++i) {
    const SellItem& s = g_sell[static_cast<size_t>(p.item[i])];
    for (int k = 0; k < p.n[i]; ++k) copies.push_back(s.h), pay.push_back(s.bid);
  }
  std::vector<uint8_t> took(copies.size(), 0);
  if (g_src.take_many != nullptr) {
    g_src.take_many(copies, &took);
    took.resize(copies.size(), 0);
  } else {
    for (size_t i = 0; i < copies.size(); ++i) took[i] = g_src.take(copies[i]) ? 1 : 0;
  }
  long long coins = 0;
  int n = 0;
  for (size_t i = 0; i < copies.size(); ++i) {
    if (!took[i]) continue;
    if (g_src.sold != nullptr) g_src.sold(copies[i], "A DEALER");  // (r213: a sold 1/1 stays in the collector world)
    if (g_src.sold_for != nullptr) g_src.sold_for(copies[i], "A DEALER", pay[i]);
    record_locked(copies[i], pay[i], "YOU", "A DEALER", 1);  // (r238: its SALES history)
    coins = xrcoin::add(coins, pay[i]);
    ++n;
  }
  if (coins > 0) g_src.add(g_mk.career, coins, "STRIKERZ MARKET - SELL ALL SHOWN");
  g_mk.received = xrcoin::add(g_mk.received, coins);
  g_mk.sold += n;
  read_holdings();
  for (size_t i = 0; i < g_mk.offers.size();)  // (offers on copies no longer free go)
    if (offer_gone(g_mk.offers[i])) g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
    else ++i;
  g_rowsDirty = true;
  ++g_gen;
  if (got != nullptr) *got = coins;
  return n;
}
// LIST ALL SHOWN > YES: a listing a planned copy at pct% of its market (do_list: r210's own, its rules)
int do_list_all(const Plan& p, int pct, long long* asked) {
  if (asked != nullptr) *asked = 0;
  int n = 0;
  long long sum = 0;
  for (size_t i = 0; i < p.item.size(); ++i) {
    const SellItem s = g_sell[static_cast<size_t>(p.item[i])];
    const long long each = std::max(1LL, static_cast<long long>(llr(static_cast<double>(s.market) * pct / 100.0)));
    for (int k = 0; k < p.n[i]; ++k)
      if (do_list(s.h, each) != 0) ++n, sum = xrcoin::add(sum, each);
  }
  if (asked != nullptr) *asked = sum;
  return n;
}
// ask: SELL ALL SHOWN (6) / LIST ALL SHOWN (7) - the hub's confirm with the plan's numbers; nothing to do: why, no confirm
void bulk_ask(bool list) {
  sell_fresh();
  const Plan p = bulk_plan(list, kSListPct[g_sPct], g_sLast);
  if (p.b.copies == 0) {
    if (list && static_cast<int>(g_mk.mine.size()) >= kMaxMine) say("20 LISTINGS AT ONCE - TAKE ONE DOWN FIRST (LISTED)", 0xFFB020);
    else if (g_sellRows.empty()) say("NOTHING SHOWN - CHANGE THE FILTERS", 0xFFB020);
    else say(p.b.kept > 0 ? "ONLY YOUR LAST COPIES HERE - LAST COPY: SELL TOO SELLS THEM" : "NOTHING FREE HERE - LISTED / LOCKED", 0xFFB020);
    sound("book_tick", 0.4f);
    return;
  }
  g_ask = list ? 7 : 6;
  g_bulkN = p.b.copies;
  g_bulkTotal = p.b.total;
  g_bulkPct = kSListPct[g_sPct];
  g_bulkLast = g_sLast;
  sound("book_ding", 0.5f);
}
// YES: the plan again (a tick / a matchday may have moved it since the question) - the same numbers: done; else asked again with the new ones
void bulk_yes(bool list) {
  sell_fresh();
  const Plan p = bulk_plan(list, g_bulkPct, g_bulkLast);
  if (p.b.copies != g_bulkN || p.b.total != g_bulkTotal) {
    g_bulkN = p.b.copies;
    g_bulkTotal = p.b.total;
    if (p.b.copies > 0) {
      g_ask = list ? 7 : 6;
      say("THE MARKET MOVED - CHECK THE NEW TOTAL", 0xFFB020);
    }
    return;
  }
  long long coins = 0;
  const int n = list ? do_list_all(p, g_bulkPct, &coins) : do_sell_all(p, &coins);
  if (n > 0) {
    say(list ? "LISTED " + std::to_string(n) + (n == 1 ? " CARD" : " CARDS") + " - ASKING " + coins_text(coins) + " IN ALL"
             : "SOLD " + std::to_string(n) + (n == 1 ? " CARD" : " CARDS") + " +" + coins_text(coins) + " COINS",
        list ? 0x2EF2FF : 0x39FF88);
    sound(list ? "book_ding" : "book_collect", 0.9f);
    if (!list) sound("book_coin", 0.8f);
    g_flashAt = g_now;
    save_market();
  }
  g_sellRowsDirty = true;
}
// SELL's FINISH chip: the next value that fits the SET chip (LANCINI TIZM / JAM '96: no EXQUISITE tiers; EXQUISITE: only its own)
int sfin_next(int f) {
  for (int k = 0; k < kSFins; ++k) {
    f = (f + 1) % kSFins;
    if (f == kSFinAny || f == kSFin25 || f == kSFinOne) return f;
    const bool exqTier = f >= kSFinExq;
    if (g_sfSet == kFSetExq ? exqTier : (g_sfSet == kFSetAll || !exqTier)) return f;
  }
  return kSFinAny;
}
// ---- r238: the SALES tab's own (g_m held)
bool has_int(const std::vector<int>& v, int x) {  // r239 (a plain loop: libc++ turns std::find on 32-bit ints into wmemchr)
  for (int y : v)
    if (y == x) return true;
  return false;
}
// r239 (Lance 11-13: every form of a family is ONE player): a family's forms (its main cards: on this headset or with a sale), the first form
// first - its SALES page's FORM tabs. A card alone: itself
std::vector<int> family_forms(int card) {
  std::vector<int> out;
  if (card < 0 || card >= static_cast<int>(g_set.size())) return out;
  const int m = family_card(card);
  const std::string& ln = g_set[static_cast<size_t>(m)].line;
  auto fl = ln.empty() ? g_byLine.end() : g_byLine.find(ln);
  if (fl != g_byLine.end())
    for (int f : fl->second)
      if (g_set[static_cast<size_t>(f)].set == 0 && (g_set[static_cast<size_t>(f)].here || g_hist.count(f) > 0)) out.push_back(f);
  if (!has_int(out, m)) out.push_back(m);
  std::stable_sort(out.begin(), out.end(), [](int a, int b) {
    const int sa = g_set[static_cast<size_t>(a)].stage, sb = g_set[static_cast<size_t>(b)].stage;
    return sa != sb ? sa < sb : a < b;
  });
  return out;
}
// the card a family shows as on SALES: the form it plays as now, else its newest-sold form, else its first
int family_face(int card) {
  const std::vector<int> fs = family_forms(card);
  if (fs.empty()) return -1;
  if (const Line* l = line_of(fs.front()))
    if (l->activeCard >= 0) {
      const int a = family_card(l->activeCard);
      if (has_int(fs, a)) return a;
    }
  int best = fs.front();
  uint32_t seq = 0;
  for (int f : fs) {
    auto it = g_hist.find(f);
    if (it != g_hist.end() && !it->second.empty() && it->second.front().seq > seq) seq = it->second.front().seq, best = f;
  }
  return best;
}
// a family's sales over every form (form >= 0: that form's only), newest first
std::vector<SaleRec> family_sales(int card, int form) {
  std::vector<SaleRec> out;
  for (int f : form >= 0 ? std::vector<int>{family_card(form)} : family_forms(card)) {
    auto it = g_hist.find(f);
    if (it != g_hist.end()) out.insert(out.end(), it->second.begin(), it->second.end());
  }
  std::sort(out.begin(), out.end(), [](const SaleRec& a, const SaleRec& b) { return a.seq > b.seq; });
  return out;
}
// the groups a player's page shows: BASE .. GRADED always, JAM '96 / EXQUISITE when a form in sight has that twin (or a sale of one)
// (r239: over the family's forms - every form, or the FORM tab's one)
std::vector<int> sales_groups(int card, int form = -1) {
  std::vector<int> g = {0, 1, 2, 3, 4, 5};
  bool jam = false, exq = false;
  const std::vector<int> fs = form >= 0 ? std::vector<int>{family_card(form)} : family_forms(card);
  for (const CardInfo& c : g_set)
    if (c.set >= 1 && has_int(fs, c.base)) jam = jam || c.set == 1, exq = exq || c.set == 2;
  for (const SaleRec& r : family_sales(card, form)) jam = jam || group_of(r.v) == 6, exq = exq || group_of(r.v) == 7;
  if (jam) g.push_back(6);
  if (exq) g.push_back(7);
  return g;
}
// a player's SALES page: his family's face; pickForm - that card's own FORM tab (from a card: SELL / LISTED / BUY / OFFERS' Y or LEFT, a
// search that found that form), else ALL FORMS (the list's A)
void open_sales(int card, bool pickForm = false) {
  const int m = family_card(card);
  g_salesCard = family_face(m);
  g_salesForm = pickForm && family_forms(m).size() > 1 ? m : -1;
  g_salesGroup = 0;
  const std::vector<SaleRec> recs = family_sales(g_salesCard, g_salesForm);
  if (!recs.empty()) g_salesGroup = group_of(recs.front().v);  // (the newest sale's finish first)
  g_salesDirty = true;
}
// the card the tab you came from was on (its player's SALES): BUY's copy, SELL's, a listing's, an offer's; -1 none
int context_card(int tab) {
  if (tab == kTabBuy && g_cur[kTabBuy] >= 0 && g_cur[kTabBuy] < static_cast<int>(g_rows.size())) return g_rows[static_cast<size_t>(g_cur[kTabBuy])].v.card;
  if (tab == kTabMine) {  // r240: SELL's row as shown
    sell_fresh();
    if (const SellItem* s = sell_at(g_cur[kTabMine])) return s->h.card;
  }
  if (tab == kTabListed && g_cur[kTabListed] >= 0 && g_cur[kTabListed] < static_cast<int>(g_mk.mine.size())) return g_mk.mine[static_cast<size_t>(g_cur[kTabListed])].v.card;
  if (tab == kTabOffers && g_cur[kTabOffers] >= 0 && g_cur[kTabOffers] < static_cast<int>(g_mk.offers.size())) return g_mk.offers[static_cast<size_t>(g_cur[kTabOffers])].v.card;
  return -1;
}
// the players' list: every player with sales (the newest first) the query matches; with a query also the players it matches with none yet
// (their market values) - made on change only (a sale, a key), 200 at most
void sales_rows_build() {
  g_salesDirty = false;
  g_salesRows.clear();
  std::string q = g_q;
  while (!q.empty() && q.back() == ' ') q.pop_back();
  // r239: ONE row a FAMILY (every form's sales together, the face = the form it plays as now); a query finds a family by any of its forms'
  // names - a form the query found that isn't the face opens on its own FORM tab (r238: a row a form - TOGEPI's sales apart from TOGETIC's)
  auto fam_key = [](int m) {
    const std::string& ln = g_set[static_cast<size_t>(m)].line;
    return ln.empty() ? "#" + std::to_string(m) : ln;
  };
  std::map<std::string, size_t> at;
  auto add_family = [&](int m) -> SalesRow* {  // (nullptr: the query doesn't find it)
    const std::string key = fam_key(m);
    auto f = at.find(key);
    if (f != at.end()) return &g_salesRows[f->second];
    const std::vector<int> fs = family_forms(m);
    SalesRow r;
    r.card = family_face(m);
    r.forms = static_cast<int>(fs.size());
    if (r.card < 0) return nullptr;
    if (!q.empty() && !name_match(search_name(r.card), q)) {
      for (int f2 : fs)
        if (r.form < 0 && name_match(search_name(f2), q)) r.form = f2;
      if (r.form < 0) return nullptr;
    }
    r.lastV.card = r.card;
    at[key] = g_salesRows.size();
    g_salesRows.push_back(r);
    return &g_salesRows.back();
  };
  for (const auto& kv : g_hist) {
    if (kv.second.empty() || kv.first < 0 || kv.first >= static_cast<int>(g_set.size())) continue;
    SalesRow* r = add_family(kv.first);
    if (r == nullptr) continue;
    r->n += static_cast<int>(kv.second.size());
    if (kv.second.front().seq > r->seq) r->last = kv.second.front().price, r->lastV = kv.second.front().v, r->seq = kv.second.front().seq;
  }
  std::sort(g_salesRows.begin(), g_salesRows.end(), [](const SalesRow& a, const SalesRow& b) { return a.seq > b.seq; });
  if (!q.empty()) {
    int extra = 0;
    for (size_t i = 0; i < g_set.size() && extra < 40; ++i) {
      if (g_set[i].set != 0 || !g_set[i].here || !name_match(search_name(static_cast<int>(i)), q)) continue;
      at.clear();  // (the sort moved the rows: the family's place looked up again)
      for (size_t k = 0; k < g_salesRows.size(); ++k) at[fam_key(g_salesRows[k].card)] = k;
      if (at.count(fam_key(static_cast<int>(i)))) continue;
      if (add_family(static_cast<int>(i)) != nullptr) ++extra;
    }
  }
  if (g_salesRows.size() > 200) g_salesRows.resize(200);
  g_cur[kTabSales] = std::max(0, std::min(static_cast<int>(g_salesRows.size()) - 1, g_cur[kTabSales]));
}
// the panel's keys (page units): 32 keys in 8 x 4 (A-Z ' - SPACE DEL CLR DONE), 4 recent searches, 4 filter chips
constexpr int kKbKeys = 32, kKbRecent = 32, kKbSet = 36, kKbFin = 37, kKbPrice = 38, kKbSort = 39, kKbFam = 40, kKbAll = 41;  // (r239: + FAMILY)
constexpr float kKbX1 = 760.0f, kRowsX0Open = 780.0f, kRowsY0 = 300.0f, kRowH = 95.0f;
constexpr int kRowsShown = 6;
const char* const kKbLabel[kKbKeys] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P",
                                       "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "'", "-", "SPACE", "DEL", "CLR", "DONE"};
// r240: SELL's panel - the same letter grid + recent searches (a row higher), its 8 chips in 2 x 4 (SET / FINISH, TYPE / STATUS, PRICE / SORT,
// FAMILY / LAST COPY) and over the results at the right SELL ALL SHOWN, LIST ALL SHOWN, AT n% (the list's price)
constexpr int kKbSLast = 43, kKbSellAll = 44, kKbListAll = 45, kKbPct = 46, kKbAllSell = 47;
constexpr float kSellRowsY0 = 330.0f;  // (the results under the bulk bar while the panel is up: 5 rows of kRowH)
constexpr int kSellRowsShown = 5;
// r241: GRADING's panel - the same letter grid + recent searches as SELL's (a row higher), its 6 chips in 2 x 3 (SET / FINISH, TYPE / STATUS,
// SORT / FAMILY); no bulk keys
constexpr int kKbAllGrade = kKbSet + 6;
int kb_mode() { return g_gradeKb.load() ? 2 : (g_tab == kTabMine ? 1 : 0); }  // r241: 0 BUY / SALES, 1 SELL, 2 GRADING
int kb_count() { return kb_mode() == 2 ? kKbAllGrade : (kb_mode() == 1 ? kKbAllSell : kKbAll); }
std::string& kbq() { return kb_mode() == 2 ? g_gq : (kb_mode() == 1 ? g_sq : g_q); }  // (the query the panel types: GRADING's / SELL's own, else BUY / SALES')
bool kb_box(int k, float* x0, float* y0, float* x1, float* y1) {
  if (k < kKbKeys) {
    const int r = k / 8, c = k % 8;
    *x0 = 20 + c * 92.0f, *x1 = *x0 + 84, *y0 = 340 + r * 74.0f, *y1 = *y0 + 66;
    return true;
  }
  if (kb_mode() == 2) {  // r241
    if (k < kKbSet) {
      const int i = k - kKbRecent;
      if (i >= static_cast<int>(g_recent.size())) return false;
      *x0 = 20 + i * 184.0f, *x1 = *x0 + 176, *y0 = 640, *y1 = 684;
      return true;
    }
    if (k < kKbAllGrade) {
      const int i = k - kKbSet;
      *x0 = 20 + (i % 2) * 375.0f, *x1 = *x0 + 365, *y0 = 694 + (i / 2) * 52.0f, *y1 = *y0 + 46;
      return true;
    }
    return false;
  }
  if (g_tab == kTabMine) {  // r240
    if (k < kKbSet) {
      const int i = k - kKbRecent;
      if (i >= static_cast<int>(g_recent.size())) return false;
      *x0 = 20 + i * 184.0f, *x1 = *x0 + 176, *y0 = 640, *y1 = 684;
      return true;
    }
    if (k <= kKbSLast) {
      const int i = k - kKbSet;
      *x0 = 20 + (i % 2) * 375.0f, *x1 = *x0 + 365, *y0 = 694 + (i / 2) * 52.0f, *y1 = *y0 + 46;
      return true;
    }
    if (k == kKbSellAll) return *x0 = 780, *x1 = 1205, *y0 = 200, *y1 = 262, true;
    if (k == kKbListAll) return *x0 = 1215, *x1 = 1490, *y0 = 200, *y1 = 262, true;
    if (k == kKbPct) return *x0 = 1500, *x1 = kW, *y0 = 200, *y1 = 262, true;
    return false;
  }
  if (k < kKbSet) {
    const int i = k - kKbRecent;
    if (i >= static_cast<int>(g_recent.size())) return false;
    *x0 = 20 + i * 184.0f, *x1 = *x0 + 176, *y0 = 662, *y1 = 706;
    return true;
  }
  if (k < kKbAll) {  // (r239: 5 chips - SET / FINISH, PRICE / SORT, FAMILY: rows 56 apart, 48 high; r235-r238: 4, 60 apart, 50 high)
    const int i = k - kKbSet;
    *x0 = 20 + (i % 2) * 375.0f, *x1 = *x0 + 365, *y0 = 716 + (i / 2) * 56.0f, *y1 = *y0 + 48;
    return true;
  }
  return false;
}
int kb_hit(float x, float y) {  // the key / chip within 10 units of the ray (-1 none)
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < kb_count(); ++k) {  // (r240: SELL's has more)
    float x0, y0, x1, y1;
    if (!kb_box(k, &x0, &y0, &x1, &y1)) continue;
    const float dx = std::max(0.0f, std::max(x0 - x, x - x1)), dy = std::max(0.0f, std::max(y0 - y, y - y1));
    const float d = dx * dx + dy * dy;
    if (d < 100.0f && d < bd) bd = d, best = k;
  }
  return best;
}
void kb_move(int dir) {  // the stick: the nearest key / chip that way (by their centres)
  float ax0, ay0, ax1, ay1;
  if (!kb_box(g_kbKey, &ax0, &ay0, &ax1, &ay1)) g_kbKey = 0, kb_box(0, &ax0, &ay0, &ax1, &ay1);
  const float cx = 0.5f * (ax0 + ax1), cy = 0.5f * (ay0 + ay1);
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < kb_count(); ++k) {  // (r240: SELL's has more)
    float x0, y0, x1, y1;
    if (k == g_kbKey || !kb_box(k, &x0, &y0, &x1, &y1)) continue;
    const float dx = 0.5f * (x0 + x1) - cx, dy = 0.5f * (y0 + y1) - cy;
    float along, across;
    if (dir == 1) along = -dx, across = std::fabs(dy);
    else if (dir == 2) along = dx, across = std::fabs(dy);
    else if (dir == 3) along = -dy, across = std::fabs(dx);
    else along = dy, across = std::fabs(dx);
    if (along < 8.0f) continue;
    const float d = along + 2.5f * across;
    if (d < bd) bd = d, best = k;
  }
  if (best >= 0 && best != g_kbKey) {
    g_kbKey = best;
    sound("book_tick", 0.4f);
  }
}
void remember_query() {
  std::string q = kbq();  // (r240: SELL's query on SELL)
  while (!q.empty() && q.back() == ' ') q.pop_back();
  if (q.empty()) return;
  g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), q), g_recent.end());
  g_recent.insert(g_recent.begin(), q);
  if (g_recent.size() > 4) g_recent.resize(4);
}
void kb_close() {
  g_kb = false;
  g_kbOpen = false;
  remember_query();
  g_saveDue = true;
  if (g_gradeKb.load()) {  // r241: GRADING's panel down (SELL's LAST COPY untouched)
    g_gradeKb = false;
    return;
  }
  if (g_tab == kTabMine) g_sLast = false;  // r240: LAST COPY: SELL TOO never stays on past its panel
}
// r241: a key / chip of GRADING's panel (the letters type GRADING's own query; its chips)
void kb_press_grade(int k) {
  bool changed = true;
  std::string& q = g_gq;
  if (k >= 0 && k < 28) {
    if (q.size() < 16) q += kKbLabel[k];
  } else if (k == 28) {
    if (!q.empty() && q.back() != ' ' && q.size() < 16) q += ' ';
    else changed = false;
  } else if (k == 29) {
    if (!q.empty()) q.pop_back();
    else changed = false;
  } else if (k == 30) {
    changed = !q.empty();
    q.clear();
  } else if (k == 31) {
    kb_close();
    sound("book_ding", 0.6f);
    return;
  } else if (k >= kKbRecent && k < kKbSet) {
    const int i = k - kKbRecent;
    if (i < static_cast<int>(g_recent.size())) q = g_recent[static_cast<size_t>(i)];
  } else if (k == kKbSet) {
    g_gfSet = (g_gfSet + 1) % kFSets;
    const int f = g_gfFin;  // (a finish the new set can't have: back to ANY)
    if (f >= kSFinExq ? (g_gfSet == kFSetA || g_gfSet == kFSetJam) : (g_gfSet == kFSetExq && f != kSFinAny && f != kSFin25 && f != kSFinOne)) g_gfFin = kSFinAny;
  } else if (k == kKbSet + 1) {
    g_gfFin = gfin_next(g_gfFin);
  } else if (k == kKbSet + 2) {
    g_gfType = (g_gfType + 1) % kSTypes;
  } else if (k == kKbSet + 3) {
    g_gfStat = (g_gfStat + 1) % kGStats;
  } else if (k == kKbSet + 4) {
    g_gfSort = (g_gfSort + 1) % kGSorts;
  } else if (k == kKbSet + 5) {
    g_gfFam = !g_gfFam;
  } else {
    changed = false;
  }
  sound("book_tick", 0.6f);
  if (changed) {
    g_gCur = 0;
    g_gTop = 0;
    g_gradeRowsDirty = true;
    grade_fresh();  // (the rows again from the kinds of copies - no price read)
  }
}
// r240: a key / chip of SELL's panel (the letters type SELL's own query; its chips; its bulk keys ask the hub's confirm)
void kb_press_sell(int k) {
  bool changed = true;
  std::string& q = g_sq;
  if (k >= 0 && k < 28) {
    if (q.size() < 16) q += kKbLabel[k];
  } else if (k == 28) {
    if (!q.empty() && q.back() != ' ' && q.size() < 16) q += ' ';
    else changed = false;
  } else if (k == 29) {
    if (!q.empty()) q.pop_back();
    else changed = false;
  } else if (k == 30) {
    changed = !q.empty();
    q.clear();
  } else if (k == 31) {
    kb_close();
    sound("book_ding", 0.6f);
    return;
  } else if (k >= kKbRecent && k < kKbSet) {
    const int i = k - kKbRecent;
    if (i < static_cast<int>(g_recent.size())) q = g_recent[static_cast<size_t>(i)];
  } else if (k == kKbSet) {
    g_sfSet = (g_sfSet + 1) % kFSets;
    const int f = g_sfFin;  // (a finish the new set can't have: back to ANY)
    if (f >= kSFinExq ? (g_sfSet == kFSetA || g_sfSet == kFSetJam) : (g_sfSet == kFSetExq && f != kSFinAny && f != kSFin25 && f != kSFinOne)) g_sfFin = kSFinAny;
  } else if (k == kKbSet + 1) {
    g_sfFin = sfin_next(g_sfFin);
  } else if (k == kKbSet + 2) {
    g_sfType = (g_sfType + 1) % kSTypes;
  } else if (k == kKbSet + 3) {
    g_sfStat = (g_sfStat + 1) % kSStats;
  } else if (k == kKbSet + 4) {
    g_sfPrice = (g_sfPrice + 1) % kSPrices;
  } else if (k == kKbSet + 5) {
    g_sfSort = (g_sfSort + 1) % kSSorts;
  } else if (k == kKbSet + 6) {
    g_sfFam = !g_sfFam;
  } else if (k == kKbSLast) {
    g_sLast = !g_sLast;
    changed = false;
  } else if (k == kKbPct) {
    g_sPct = (g_sPct + 1) % 4;
    changed = false;
  } else if (k == kKbSellAll || k == kKbListAll) {
    bulk_ask(k == kKbListAll);
    return;
  }
  sound("book_tick", 0.6f);
  if (changed) {
    g_cur[kTabMine] = 0;
    g_top[kTabMine] = 0;
    g_sellRowsDirty = true;
    sell_fresh();  // (the rows again from the kinds of copies - no price read)
  }
}
void kb_press(int k) {
  if (kb_mode() == 2) {  // r241
    kb_press_grade(k);
    return;
  }
  if (g_tab == kTabMine) {  // r240
    kb_press_sell(k);
    return;
  }
  bool changed = true;
  if (k >= 0 && k < 28) {  // a letter, ' or -
    if (g_q.size() < 16) g_q += kKbLabel[k];
  } else if (k == 28) {  // SPACE
    if (!g_q.empty() && g_q.back() != ' ' && g_q.size() < 16) g_q += ' ';
    else changed = false;
  } else if (k == 29) {  // DEL
    if (!g_q.empty()) g_q.pop_back();
    else changed = false;
  } else if (k == 30) {  // CLR
    changed = !g_q.empty();
    g_q.clear();
  } else if (k == 31) {  // DONE
    kb_close();
    sound("book_ding", 0.6f);
    return;
  } else if (k >= kKbRecent && k < kKbSet) {
    const int i = k - kKbRecent;
    if (i < static_cast<int>(g_recent.size())) g_q = g_recent[static_cast<size_t>(i)];
  } else if (k == kKbSet) {
    g_fSet = (g_fSet + 1) % kFSets;
  } else if (k == kKbFin) {
    g_fFin = (g_fFin + 1) % kFFins;
  } else if (k == kKbPrice) {
    g_fPrice = (g_fPrice + 1) % kFPrices;
  } else if (k == kKbSort) {
    g_fSort = (g_fSort + 1) % kSorts;
  } else if (k == kKbFam) {  // r239
    g_fFam = !g_fFam;
  }
  sound("book_tick", 0.6f);
  if (changed) {
    g_cur[kTabBuy] = 0;
    g_top[kTabBuy] = 0;
    rows_build();
    g_salesDirty = true;  // r238: (the SALES tab's list follows the letters too)
    g_cur[kTabSales] = g_top[kTabSales] = 0;
  }
}
bool watching(int card) {
  return card >= 0 && card < static_cast<int>(g_set.size()) && std::find(g_watch.begin(), g_watch.end(), g_set[static_cast<size_t>(card)].line) != g_watch.end();
}
bool watch_flip(int card) {  // (g_m held) true: now watched
  if (card < 0 || card >= static_cast<int>(g_set.size())) return false;
  const std::string& line = g_set[static_cast<size_t>(card)].line;
  auto f = std::find(g_watch.begin(), g_watch.end(), line);
  if (f != g_watch.end()) {
    g_watch.erase(f);
    return false;
  }
  if (g_watch.size() >= 12) g_watch.erase(g_watch.begin());
  g_watch.push_back(line);
  return true;
}
// A > YES on a result: the dealer's matchday listing (this module's do_buy), else an Offering (xr_trade, once the lock is let go: g_pend)
bool ask_row(int at) {
  if (at < 0 || at >= static_cast<int>(g_rows.size())) return false;
  const SearchRow& r = g_rows[static_cast<size_t>(at)];
  g_askV = r.v;
  g_askCoins = r.ask;
  g_askWho = r.who;
  if (r.src == 0) {
    g_ask = 2;
    g_askIdx = static_cast<int>(r.id);
  } else {
    g_ask = 5;
    g_askOffer = r.id;
  }
  return true;
}
}  // namespace

static bool input_locked(const Input& in) {  // (g_m held: r235's input() lets go of it before a collector's copy is bought)
  if (g_mk.career == 0) return false;
  bool used = false;
  auto done = [&]() {
    ++g_gen;
    used = true;
  };
  if (g_kb && g_gradeKb.load()) kb_close();  // r241: (GRADING's panel left open: down - this is the market's page)
  if (g_ask == 8) g_ask = 0, g_gradeAskA = false;  // r241: (a GRADING send left asking: dropped)
  if (g_kb && !g_pad && g_ask == 0) {  // r235: the search panel has every button while it's up: stick moves, A presses, X DEL, B closes it
    const int k0 = g_kbKey;
    bool hit = false;
    if (in.step && in.dir != 0) kb_move(in.dir);
    if (in.a) kb_press(g_kbKey), hit = true;
    else if (in.x) kb_press(29), hit = true;
    else if (in.b) {
      kb_close();
      sound("book_tick", 0.5f);
      hit = true;
    }
    if (hit || g_kbKey != k0) ++g_gen;  // (r206: a picture only when something changed - never a frame's worth of them)
    return true;
  }
  if (g_pad) {  // r210: the number pad has every button while it's up: stick moves, A presses, B closes it (nothing changed)
    if (in.step && in.dir != 0) pad_move(in.dir);
    if (in.a) pad_press(g_padKey >= 12 ? 12 : g_padKey);
    else if (in.x) pad_press(11);  // (X: DEL, a quick one)
    else if (in.b) {
      g_pad = false;
      say(g_padEdit != 0 ? "PRICE NOT CHANGED" : "NOT LISTED", 0x9A92B8);
    }
    ++g_gen;
    return true;
  }
  if (g_ask != 0) {  // asking (the hub's pop-up): A yes, B no
    if (in.a && (g_ask == 6 || g_ask == 7)) {  // r240: SELL ALL SHOWN / LIST ALL SHOWN - done when the numbers asked still hold (else asked again)
      const bool list = g_ask == 7;
      g_ask = 0;
      bulk_yes(list);
      done();
      return true;
    }
    if (in.a) {
      bool ok = false;
      if (g_ask == 1) {
        ok = do_sell(g_askV, g_askCoins, "STRIKERZ MARKET SALE");
        if (ok) {
          say("SOLD: " + upper(variant_words(g_askV)) + " " + name_of_card(g_askV.card) + " +" + coins_text(g_askCoins) + " COINS", 0x39FF88);
          sound("book_coin", 0.8f);
        }
      } else if (g_ask == 2) {
        ok = do_buy(g_askIdx);
        if (ok) {
          say("BOUGHT: " + name_of_card(g_askV.card) + " - IT'S IN YOUR BINDER", 0x39FF88);
          sound("book_ding", 0.8f);
          rows_build();  // r235
        }
      } else if (g_ask == 5) {  // r235: a collector's / the dealer's case's copy - bought by xr_trade once this lock is let go (input)
        g_pend.on = true;
        g_pend.id = g_askOffer;
        g_pend.ask = g_askCoins;
        g_pend.v = g_askV;
        g_pend.who = g_askWho;
      } else if (g_ask == 3) {
        ok = do_accept(g_askId);
        if (ok) {
          say("SOLD TO A COLLECTOR: +" + coins_text(g_askCoins) + " COINS", 0x39FF88);
          sound("book_collect", 0.8f);
        }
      } else if (g_ask == 4) {  // r210: a listing taken down
        if (do_unlist(g_askId)) {
          say("LISTING TAKEN DOWN - " + copy_words(g_askV) + " IS YOURS TO KEEP", 0x9A92B8);
          sound("book_tick", 0.6f);
          save_market();
        }
      }
      if (ok) {
        g_flashAt = g_now;
        save_market();
      }
      g_ask = 0;
      done();
    } else if (in.b) {
      g_ask = 0;
      done();
    }
    return used || in.dir != 0;
  }
  if (g_countering) {  // countering an offer: left / right +5 / +10 / +15%, A sends it, B drops it
    if (in.step && (in.dir == 1 || in.dir == 2)) {
      g_counterPick = std::max(0, std::min(2, g_counterPick + (in.dir == 2 ? 1 : -1)));
      sound("book_tick", 0.5f);
      done();
    } else if (in.a) {
      const int r = do_counter(g_askId, kCounterPct[g_counterPick]);
      if (r == 1) say("THEY TOOK IT - SOLD AT +" + std::to_string(kCounterPct[g_counterPick]) + "%", 0x39FF88), sound("book_collect", 0.8f), g_flashAt = g_now;
      else if (r == 2) say("THEY CAME BACK WITH A FINAL OFFER", 0xFFB020), sound("book_ding", 0.6f);
      else if (r == 3) say("TOO RICH FOR THEM - THEY WALKED AWAY", 0xFF4D5E);
      g_countering = false;
      save_market();
      done();
    } else if (in.b) {
      g_countering = false;
      done();
    }
    return true;
  }
  if (in.step && (in.dir == 1 || in.dir == 2)) {  // the tabs
    {  // r236: BUY > SELL > LISTED > OFFERS (TOP CARDS is the search's SORT: TRENDING now - its race, its reasons on each copy)
      // r238: > SALES (LEFT from BUY): it opens on the card you were on (BUY's copy, SELL's, a listing's, an offer's)
      static const int kOrder[5] = {kTabBuy, kTabMine, kTabListed, kTabOffers, kTabSales};
      const int from = g_tab;
      int at = 0;
      for (int i = 0; i < 5; ++i)
        if (kOrder[i] == g_tab) at = i;
      g_tab = kOrder[(at + (in.dir == 2 ? 1 : 4)) % 5];
      if (g_tab == kTabSales) {
        const int c = context_card(from);
        if (c >= 0) open_sales(c, true);  // (r239: on that copy's own FORM tab - X: his other forms / ALL FORMS)
      }
    }
    if (g_tab == kTabListed && g_mk.unseenSales > 0) {  // (r210: your sales seen)
      g_mk.unseenSales = 0;
      save_market();
    }
    if (g_tab == kTabBuy) {  // r235: the watch alerts seen; the results fresh
      g_unseenAlerts = 0;
      if (g_rowsDirty) rows_build();
    }
    sound("book_tick", 0.5f);
    done();
    return true;
  }
  if (g_tab == kTabSales) {  // r238: SALES - the players' list (A: one's sales, X: SEARCH), a player (up / down its finishes, A: BUY ONE, B: the list)
    if (g_salesDirty) sales_rows_build();
    if (in.x && g_salesCard >= 0) {  // r239: a player of several forms - X = his next FORM tab (ALL FORMS, the first form, ... the last, ALL)
      const std::vector<int> fs = family_forms(g_salesCard);
      if (fs.size() > 1) {
        size_t at = fs.size();
        for (size_t i = 0; i < fs.size(); ++i)
          if (fs[i] == g_salesForm) at = i;
        g_salesForm = at >= fs.size() ? fs.front() : (at + 1 < fs.size() ? fs[at + 1] : -1);
        const std::vector<int> gs = sales_groups(g_salesCard, g_salesForm);
        if (!has_int(gs, g_salesGroup)) g_salesGroup = 0;
        sound("book_tick", 0.5f);
        done();
        return true;
      }
    }
    if (in.x) {
      g_kb = true;
      g_kbOpen = true;
      g_kbKey = 0;
      g_salesCard = -1;  // (the list follows the letters)
      g_salesForm = -1;
      sound("book_ding", 0.5f);
      done();
      return true;
    }
    if (g_salesCard < 0) {
      const int n = static_cast<int>(g_salesRows.size());
      int& cur = g_cur[kTabSales];
      if (in.step && (in.dir == 3 || in.dir == 4) && n > 0) {
        cur = std::max(0, std::min(n - 1, cur + (in.dir == 4 ? 1 : -1)));
        sound("book_tick", 0.4f);
        done();
        return true;
      }
      cur = std::max(0, std::min(std::max(0, n - 1), cur));
      if (in.a && cur < n) {
        const SalesRow& sr = g_salesRows[static_cast<size_t>(cur)];
        open_sales(sr.form >= 0 ? sr.form : sr.card, sr.form >= 0);  // (r239: a form the search found: its tab)
        sound("book_tick", 0.6f);
        done();
        return true;
      }
      if (in.y && cur < n) {
        const bool now = watch_flip(g_salesRows[static_cast<size_t>(cur)].card);
        say(now ? "WATCHING " + search_name(g_salesRows[static_cast<size_t>(cur)].card) : "NOT WATCHING " + search_name(g_salesRows[static_cast<size_t>(cur)].card), now ? 0x2EF2FF : 0x9A92B8);
        save_market();
        done();
        return true;
      }
      return in.a || in.y;
    }
    const std::vector<int> gs = sales_groups(g_salesCard, g_salesForm);
    int gi = 0;
    for (size_t i = 0; i < gs.size(); ++i)
      if (gs[i] == g_salesGroup) gi = static_cast<int>(i);
    if (in.step && (in.dir == 3 || in.dir == 4) && !gs.empty()) {
      gi = std::max(0, std::min(static_cast<int>(gs.size()) - 1, gi + (in.dir == 4 ? 1 : -1)));
      if (gs[static_cast<size_t>(gi)] != g_salesGroup) sound("book_tick", 0.4f);
      g_salesGroup = gs[static_cast<size_t>(gi)];
      done();
      return true;
    }
    if (in.b) {  // (back to the list - the hub's B is its own again there)
      g_salesCard = -1;
      g_salesForm = -1;
      sound("book_tick", 0.5f);
      done();
      return true;
    }
    if (in.y) {
      const bool now = watch_flip(g_salesCard);
      say(now ? "WATCHING " + search_name(g_salesCard) : "NOT WATCHING " + search_name(g_salesCard), now ? 0x2EF2FF : 0x9A92B8);
      save_market();
      done();
      return true;
    }
    if (in.a) {  // BUY ONE: the BUY tab searching this player in this finish
      // (r239: on a FORM tab that form's copies; on ALL FORMS his whole family's - the FAMILY chip on)
      g_q = search_name(g_salesForm >= 0 ? g_salesForm : g_salesCard).substr(0, 16);
      g_fFam = g_salesForm < 0 && family_forms(g_salesCard).size() > 1;
      g_fSet = g_salesGroup == 6 ? kFSetJam : (g_salesGroup == 7 ? kFSetExq : kFSetA);
      static const int kFin[kSaleGroups] = {kFFinBase, kFFinSilver, kFFinGold, kFFinOne, kFFinAny, kFFinAny, kFFinAny, kFFinAny};
      g_fFin = kFin[std::max(0, std::min(kSaleGroups - 1, g_salesGroup))];
      g_fPrice = kFPriceAny;
      g_tab = kTabBuy;
      g_cur[kTabBuy] = g_top[kTabBuy] = 0;
      remember_query();
      rows_build();
      say(g_rows.empty() ? "NONE FOR SALE RIGHT NOW - Y WATCHES HIM" : std::to_string(g_rows.size()) + " FOR SALE", g_rows.empty() ? 0xFFB020 : 0x39FF88);
      sound("book_ding", 0.5f);
      done();
      return true;
    }
    return false;
  }
  // r206: MY CARDS' copies COUNTED here (r201-r205 sorted them by price - ~5,000 price reads - on every call, i.e. every game
  // frame on this page); the sorted list only when A picks one
  // r240: SELL's rows - made on a change only (sell_fresh: a few multiplies a copy to see nothing changed; the list itself on a key / a sale)
  int nMine = 0;
  if (g_tab == kTabMine) {
    sell_fresh();
    nMine = static_cast<int>(g_sellRows.size());
  }
  if (g_tab == kTabBuy && g_rowsDirty) rows_build();  // r235
  const int n = g_tab == kTabMine ? nMine
                                  : (g_tab == kTabBuy ? static_cast<int>(g_rows.size())
                                                      : (g_tab == kTabOffers ? static_cast<int>(g_mk.offers.size())
                                                                             : (g_tab == kTabListed ? static_cast<int>(g_mk.mine.size()) : 0)));
  if (in.step && (in.dir == 3 || in.dir == 4) && n > 0) {
    g_cur[g_tab] = std::max(0, std::min(n - 1, g_cur[g_tab] + (in.dir == 4 ? 1 : -1)));
    sound("book_tick", 0.4f);
    done();
    return true;
  }
  g_cur[g_tab] = std::max(0, std::min(std::max(0, n - 1), g_cur[g_tab]));
  const int at = g_cur[g_tab];
  if (in.x && g_tab == kTabOffers && at < n) {  // X: decline
    g_mk.offers.erase(g_mk.offers.begin() + at);
    say("OFFER DECLINED", 0x9A92B8);
    save_market();
    done();
    return true;
  }
  if (in.y && g_tab == kTabOffers && at < n && !g_mk.offers[static_cast<size_t>(at)].final) {  // Y: counter
    g_countering = true;
    g_counterPick = 1;
    g_askId = g_mk.offers[static_cast<size_t>(at)].id;
    done();
    return true;
  }
  if (in.x && g_tab == kTabMine) {  // r240: X: SELL's SEARCH panel (a player, the filters, SELL ALL / LIST ALL SHOWN) - as BUY's
    g_kb = true;
    g_kbOpen = true;
    g_kbKey = 0;
    sound("book_ding", 0.5f);
    done();
    return true;
  }
  if (in.y && g_tab == kTabMine && at < n) {  // r210: LIST FOR SALE - your price on the pad (the market's suggested). r240: on Y (X is SEARCH)
    const SellItem& s = *sell_at(at);
    const Holding h = s.h;
    if (static_cast<int>(g_mk.mine.size()) >= kMaxMine) say("20 LISTINGS AT ONCE - TAKE ONE DOWN FIRST (LISTED)", 0xFFB020);
    else if (h.grading) say("IT'S " + grading_words(h) + " TO GO - NOT BACK YET", 0xFFB020);  // r240
    else if (!own_free(h) && held_out_count(h) > 0) say("IT'S IN AN OPEN COLLECTOR TRADE - ANSWER IT FIRST (CARDS > COLLECTORS)", 0xFFB020);  // r213
    else if (!own_free(h)) say("EVERY COPY OF IT IS LISTED ALREADY - LISTED", 0xFFB020);
    else if (s.free <= 0 && s.museum > 0) say("IT'S IN YOUR MUSEUM - TAKE IT OUT FIRST (CARDS > MUSEUM)", 0xFFB020);  // r240
    else pad_open_for(h, 0, round_nice(s.market));
    done();
    return true;
  }
  if (in.x && g_tab == kTabListed && at < n) {  // r210: X: take a listing down (asks)
    g_ask = 4;
    g_askId = g_mk.mine[static_cast<size_t>(at)].id;
    g_askV = g_mk.mine[static_cast<size_t>(at)].v;
    g_askCoins = g_mk.mine[static_cast<size_t>(at)].ask;
    done();
    return true;
  }
  if (in.x && g_tab == kTabBuy) {  // r235: X: the SEARCH panel (type a player, the filters, recent searches)
    g_kb = true;
    g_kbOpen = true;
    g_kbKey = 0;
    sound("book_ding", 0.5f);
    done();
    return true;
  }
  if (in.y && g_tab == kTabBuy && at < n) {  // r235: Y: WATCH / UNWATCH the player of this copy (an alert when one lists under the market)
    const int card = g_rows[static_cast<size_t>(at)].v.card;
    const bool now = watch_flip(card);
    say(now ? "WATCHING " + search_name(card) + " - YOU'LL HEAR WHEN A COPY LISTS UNDER THE MARKET" : "NOT WATCHING " + search_name(card) + " ANY MORE", now ? 0x2EF2FF : 0x9A92B8);
    sound("book_ding", 0.5f);
    save_market();
    done();
    return true;
  }
  if (in.y && g_tab == kTabListed && at < n) {  // r238: Y: this card's SALES (every finish's recent sales). r240: LISTED's only (SELL's Y lists)
    open_sales(g_mk.mine[static_cast<size_t>(at)].v.card, true);  // (r239: its FORM tab)
    g_tab = kTabSales;
    sound("book_tick", 0.5f);
    done();
    return true;
  }
  if (in.x || in.y) return true;  // (nothing for them here; kept off the hub's trades)
  if (in.a) {
    if (g_tab == kTabMine && at < n) {
      const SellItem& s = *sell_at(at);  // r240: the row shown (filtered, sorted)
      const Holding h = s.h;
      if (h.grading) {  // r240: (a copy at TIZM GRADING: on the list, locked)
        say("IT'S " + grading_words(h) + " TO GO - NOT BACK YET", 0xFFB020);
        done();
        return true;
      }
      if (!own_free(h)) {  // r210: a listed copy sells through its listing (or take it down first)
        say(held_out_count(h) > 0 && listed_count(h) == 0 ? "IT'S IN AN OPEN COLLECTOR TRADE - ANSWER IT FIRST (CARDS > COLLECTORS)"  // r213
                                                           : "IT'S LISTED - TAKE THE LISTING DOWN FIRST (LISTED, X)",
            0xFFB020);
        done();
        return true;
      }
      if (s.free <= 0 && s.museum > 0) {  // r240: (a museum copy: never sold from the list - its piece first)
        say("IT'S IN YOUR MUSEUM - TAKE IT OUT FIRST (CARDS > MUSEUM)", 0xFFB020);
        done();
        return true;
      }
      g_ask = 1;
      g_askV = h;
      g_askCoins = s.bid;
    } else if (g_tab == kTabListed && at < n) {  // r210: A: EDIT PRICE
      const Mine& m = g_mk.mine[static_cast<size_t>(at)];
      pad_open_for(m.v, m.id, m.ask);
    } else if (g_tab == kTabBuy && at < n) {  // r235: any copy for sale - the dealer's or a collector's (ask_row)
      ask_row(at);
    } else if (g_tab == kTabOffers && at < n) {
      g_ask = 3;
      g_askId = g_mk.offers[static_cast<size_t>(at)].id;
      g_askV = g_mk.offers[static_cast<size_t>(at)].v;
      g_askCoins = g_mk.offers[static_cast<size_t>(at)].amount;
    } else {
      return true;
    }
    done();
    return true;
  }
  return used;
}
// r235: a collector's copy bought from the search - xr_trade's own lock takes it (its seller, his wallet, your coins, the card), so the
// market lets go of its lock first (lock order: trade -> market -> binder)
namespace {
void finish_buy(const PendingBuy& p) {
  std::string line;
  const bool ok = g_src.buy_offering != nullptr && g_src.buy_offering(p.id, p.ask, &line);
  std::lock_guard<std::mutex> lk(g_m);
  if (ok) {
    read_holdings();
    g_mk.spent = xrcoin::add(g_mk.spent, p.ask);
    ++g_mk.bought;
    g_mk.firstSeen[sig_of(p.v)] = g_mk.steps;  // (offers on it only after a whole matchday)
    g_paid[sig_of(p.v)] = {p.ask, g_mk.steps};   // (the flip rule)
    record_locked(p.v, p.ask, p.who, "YOU", 2);  // r238: (its SALES history)
    say(line.empty() ? "BOUGHT FROM " + p.who + " - IT'S IN YOUR BINDER" : line, 0x39FF88);
    sound("book_ding", 0.8f);
    g_flashAt = g_now;
    save_market();
  } else {
    say(line.empty() ? std::string("THAT COPY ISN'T FOR SALE ANY MORE") : line, 0xFF4D5E);
  }
  rows_build();
  ++g_gen;
}
}  // namespace
bool input(const Input& in) {
  bool used = false;
  PendingBuy p;
  {
    std::lock_guard<std::mutex> lk(g_m);
    used = input_locked(in);
    p = g_pend;
    g_pend = PendingBuy();
  }
  if (p.on) finish_buy(p);
  return used;
}
// r210: a pointer on the page: on the pad, its key under the ray is the cursor and the trigger presses it (as A); off the pad (a click
// on the page around it) nothing - the pad stays up. r235: the search panel's keys / chips the same; a click on a result picks it
bool point(float x, float y, bool click) {
  // (the render thread: never waits for the page's drawing - the lock busy, a click is kept for the next frame)
  std::unique_lock<std::mutex> lk(g_m, std::try_to_lock);
  const bool kb = g_kbOpen.load() && !g_pad.load();
  const bool inside = kb ? (x >= 0 && x <= kW && y >= 190 && y <= kH) : (x >= kPadX0 && x <= kPadX1 && y >= kPadY0 && y <= kPadY1);
  if (!lk.owns_lock()) {
    if (click && (g_pad.load() || kb) && inside) g_stashX = x, g_stashY = y, g_stashClick = true;
    return (g_pad.load() || kb) && inside;
  }
  if (!g_pad && g_kb && g_ask == 0) {  // r235: the search panel
    if (g_stashClick.exchange(false)) x = g_stashX.load(), y = g_stashY.load(), click = true;
    if (!inside) return false;
    const int k = kb_hit(x, y);
    if (k >= 0 && k != g_kbKey) g_kbKey = k, g_pointDirty = true;
    if (click && k >= 0) {
      kb_press(k);
      g_pointDirty = true;
      ++g_gen;
    } else if (click && x >= kRowsX0Open && y >= kSellRowsY0 && y < kSellRowsY0 + kSellRowsShown * kRowH && kb_mode() == 2) {  // r241: a copy on GRADING
      grade_fresh();
      const int at = g_gTop + static_cast<int>((y - kSellRowsY0) / kRowH);
      if (at >= 0 && at < static_cast<int>(g_gradeRows.size())) {
        g_gCur = at;
        kb_close();
        sound("book_tick", 0.6f);
        g_pointDirty = true;
        ++g_gen;
      }
    } else if (click && x >= kRowsX0Open && y >= kSellRowsY0 && y < kSellRowsY0 + kSellRowsShown * kRowH && g_tab == kTabMine) {  // r240: a copy on SELL
      sell_fresh();
      const int at = g_top[kTabMine] + static_cast<int>((y - kSellRowsY0) / kRowH);
      if (at >= 0 && at < static_cast<int>(g_sellRows.size())) {
        g_cur[kTabMine] = at;
        kb_close();
        sound("book_tick", 0.6f);
        g_pointDirty = true;
        ++g_gen;
      }
    } else if (click && x >= kRowsX0Open && y >= kRowsY0 && y < kRowsY0 + kRowsShown * kRowH && g_tab == kTabSales && kb_mode() != 2) {  // r238: a player on SALES
      if (g_salesDirty) sales_rows_build();
      const int at = g_top[kTabSales] + static_cast<int>((y - kRowsY0) / kRowH);
      if (at >= 0 && at < static_cast<int>(g_salesRows.size())) {
        const SalesRow& sr = g_salesRows[static_cast<size_t>(at)];
        open_sales(sr.form >= 0 ? sr.form : sr.card, sr.form >= 0);  // (r239)
        kb_close();
        sound("book_tick", 0.6f);
        g_pointDirty = true;
        ++g_gen;
      }
    } else if (click && x >= kRowsX0Open && y >= kRowsY0 && y < kRowsY0 + kRowsShown * kRowH && g_tab != kTabMine && kb_mode() != 2) {  // (a result: picked, the panel closed on it)
      const int at = g_top[kTabBuy] + static_cast<int>((y - kRowsY0) / kRowH);
      if (at >= 0 && at < static_cast<int>(g_rows.size())) {
        g_cur[kTabBuy] = at;
        kb_close();
        sound("book_tick", 0.6f);
        g_pointDirty = true;
        ++g_gen;
      }
    }
    return true;
  }
  if (!g_pad) {
    g_padHover = -1;
    g_stashClick = false;
    return false;
  }
  if (g_stashClick.exchange(false)) {  // (a click kept from a busy frame: where it was)
    x = g_stashX.load(), y = g_stashY.load();
    click = true;
  }
  if (x < kPadX0 || x > kPadX1 || y < kPadY0 || y > kPadY1) {
    if (g_padHover != -1) g_padHover = -1, g_pointDirty = true;
    return false;
  }
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < 13; ++k) {  // (the nearest key within 12 units of the ray: a trembling hand never falls into a gap)
    float x0, y0, x1, y1;
    pad_key_box(k, &x0, &y0, &x1, &y1);
    const float dx = std::max(0.0f, std::max(x0 - x, x - x1)), dy = std::max(0.0f, std::max(y0 - y, y - y1));
    const float d = dx * dx + dy * dy;
    if (d < 144.0f && d < bd) bd = d, best = k;
  }
  if (best != g_padHover) {
    g_padHover = best;
    if (best >= 0) g_padKey = best;
    g_pointDirty = true;
  }
  if (click && best >= 0) {
    pad_press(best);
    g_pointDirty = true;
    ++g_gen;
  }
  return true;
}
bool pad_open() { return g_pad.load(); }  // (no lock: the render thread asks every frame)
bool search_open() { return g_kbOpen.load(); }  // r235 (no lock: as pad_open)
void search_close() {  // r235: the page left - the letter grid down (the query kept)
  std::lock_guard<std::mutex> lk(g_m);
  if (!g_kb) return;
  kb_close();
  ++g_gen;
}

bool asking(std::string* q, std::string* line) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_ask == 0 || g_ask == 8) return false;  // (r241: 8 is GRADING's - grade_asking)
  if (g_ask == 6 || g_ask == 7) {  // r240: SELL ALL SHOWN / LIST ALL SHOWN - how many, the total, what's kept / skipped (the plan as it stands)
    sell_fresh();
    const Plan p = bulk_plan(g_ask == 7, g_bulkPct, g_bulkLast);
    const BulkPlan& b = p.b;
    // (the question: the numbers asked - YES does exactly them, or asks again if the market moved in between)
    const std::string cards = std::to_string(g_bulkN) + (g_bulkN == 1 ? " CARD" : " CARDS");
    std::string tail = b.locked > 0 ? "  -  " + std::to_string(b.locked) + " LOCKED SKIPPED" : std::string();
    if (g_ask == 6) {
      *q = "SELL " + cards + " FOR " + coins_text(g_bulkTotal) + " COINS?";
      *line = "SELL NOW 90%  -  MARKET " + coins_text(b.market) + (g_bulkLast ? "  -  YOUR LAST COPIES TOO" : "  -  YOUR LAST COPY OF EACH KEPT") + tail;
    } else {
      *q = "LIST " + cards + " AT " + std::to_string(g_bulkPct) + "% - " + coins_text(g_bulkTotal) + " COINS?";
      *line = "MARKET " + coins_text(b.market) + (b.capped > 0 ? "  -  THE FIRST " + std::to_string(b.copies) + " SHOWN (20 LISTINGS AT ONCE)" : std::string()) +
              (g_bulkLast ? "  -  YOUR LAST COPIES TOO" : "  -  YOUR LAST COPY OF EACH KEPT") + tail;
    }
    return true;
  }
  const std::string what = upper(variant_words(g_askV)) + " " + name_of_card(g_askV.card);
  const long long mkt = price_i(g_askV, -1);
  if (g_ask == 1) {
    *q = "SELL FOR " + coins_text(g_askCoins) + " COINS?";
    *line = what + "  -  MARKET " + coins_text(mkt) + ", A DEALER PAYS " + std::to_string(static_cast<int>(std::lround(bid_ratio(mkt) * 100))) + "% NOW";
  } else if (g_ask == 2 || g_ask == 5) {  // (r235: 5 a collector's / the dealer's case's copy, at its seller's price)
    *q = (g_ask == 5 ? "BUY FROM " + g_askWho + " FOR " : std::string("BUY FOR ")) + coins_text(g_askCoins) + " COINS?";
    *line = what + "  -  MARKET " + coins_text(mkt) + " (" + std::to_string(static_cast<int>(std::lround(100.0 * static_cast<double>(g_askCoins) / static_cast<double>(std::max(1LL, mkt))))) +
            "% OF IT)  -  YOU HAVE " + coins_text(balance());
  } else if (g_ask == 4) {
    *q = "TAKE THIS LISTING DOWN?";
    *line = what + "  -  LISTED AT " + coins_text(g_askCoins) + ", MARKET " + coins_text(mkt);
  } else {
    *q = "ACCEPT " + coins_text(g_askCoins) + " COINS?";
    *line = what + "  -  MARKET " + coins_text(mkt);
  }
  return true;
}
void hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord) {
  std::lock_guard<std::mutex> lk(g_m);
  *xWord = *yWord = "";
  *aDim = false;
  if (g_pad) {  // r210
    *aWord = "PRESS KEY";
    *xWord = "DEL";
    return;
  }
  if (g_countering) {
    *aWord = "SEND COUNTER";
    return;
  }
  if (g_kb) {  // r235: the search panel
    *aWord = g_kbKey == 31 ? "DONE" : "PRESS";
    if (g_tab == kTabMine && g_kbKey == kKbSellAll) *aWord = "SELL ALL SHOWN";  // r240
    if (g_tab == kTabMine && g_kbKey == kKbListAll) *aWord = "LIST ALL SHOWN";
    *xWord = "DEL";
    return;
  }
  if (g_tab == kTabBuy) {  // r235: the search's results
    const int at = g_cur[kTabBuy];
    *aWord = "BUY", *aDim = g_rows.empty();
    *xWord = "SEARCH";
    if (at >= 0 && at < static_cast<int>(g_rows.size())) *yWord = watching(g_rows[static_cast<size_t>(at)].v.card) ? "UNWATCH" : "WATCH";
    return;
  }
  if (g_tab == kTabSales) {  // r238
    *xWord = "SEARCH";
    if (g_salesCard < 0) {
      *aWord = "SEE SALES", *aDim = g_salesRows.empty();
      if (!g_salesRows.empty()) *yWord = watching(g_salesRows[static_cast<size_t>(std::max(0, std::min(static_cast<int>(g_salesRows.size()) - 1, g_cur[kTabSales])))].card) ? "UNWATCH" : "WATCH";
    } else {
      *aWord = "BUY ONE";
      *yWord = watching(g_salesCard) ? "UNWATCH" : "WATCH";
      if (family_forms(g_salesCard).size() > 1) *xWord = "FORM";  // r239: his FORM tabs
    }
    return;
  }
  if (g_tab == kTabMine) {  // r240: A SELL NOW, X SEARCH (+ the filters, SELL ALL / LIST ALL), Y LIST FOR SALE (r238-r239: X LIST, Y SALES)
    sell_fresh();
    const SellItem* s = sell_at(g_cur[kTabMine]);
    *aWord = "SELL NOW", *aDim = s == nullptr || s->free <= 0;
    *xWord = "SEARCH";
    if (s != nullptr) *yWord = "LIST FOR SALE";
  } else if (g_tab == kTabListed) {
    *aWord = "EDIT PRICE", *aDim = g_mk.mine.empty();
    if (!g_mk.mine.empty()) *xWord = "TAKE DOWN", *yWord = "SALES";
  } else if (g_tab == kTabBuy) *aWord = "BUY", *aDim = g_mk.listings.empty();
  else if (g_tab == kTabOffers) {
    *aWord = "ACCEPT";
    *aDim = g_mk.offers.empty();
    if (!g_mk.offers.empty()) {
      *xWord = "DECLINE";
      *yWord = "COUNTER";
    }
  } else {
    *aWord = "";
    *aDim = true;
  }
}

// ============================================================================================== the drawing
namespace {
constexpr uint32_t kBg0 = 0x0A1024, kBg1 = 0x05070F, kPanel = 0x121A33, kPanelHi = 0x1C2850, kLine = 0x2C3A66, kInk = 0xF4F6FF,
                   kDim = 0x8E99B8, kUp = 0x39FF88, kDown = 0xFF4D5E, kGold = 0xFFD23F, kCyan = 0x2EF2FF, kPink = 0xFF2E88;
struct Pen {
  std::vector<xrtext::BoardPiece> v;
  xrmenu::Kit K{v};
  std::vector<std::shared_ptr<void>>* keep = nullptr;
};
constexpr uint32_t kFinishFrame[4] = {0xF2EEE4, 0xC9D2E2, 0xF2C230, 0x15161C};
constexpr uint32_t kFinishEdge[4] = {0xB8B0A0, 0x8090B0, 0xA0700A, 0xFFD23F};

void chrome(Pen& P, const std::string& t, float x, float y, float px, float room, int align) {
  xrtext::BoardPiece p;
  p.kind = align == 0 ? xrtext::BoardPiece::kText : (align < 0 ? xrtext::BoardPiece::kTextLeft : xrtext::BoardPiece::kTextRight);
  p.x0 = x;
  p.y0 = y;
  p.x1 = room;
  p.y1 = px;
  p.rgb = 0xFFFFFF;
  p.rgb2 = 0x9FB2D6;
  p.outline = std::max(3.0f, px * 0.08f);
  p.outlineRgb = 0x0E1430;
  p.shadow = px * 0.06f;
  p.shadowRgb = kCyan;
  p.shadowA = 0.8f;
  p.text = t;
  P.v.push_back(p);
}
void coin(Pen& P, float cx, float cy, float r) {
  P.K.glow(cx - 2.0f * r, cy - 2.0f * r, cx + 2.0f * r, cy + 2.0f * r, kGold, 0.3f);
  P.K.box(cx - r, cy - r, cx + r, cy + r, r, 0xB8860B);
  P.K.box(cx - 0.86f * r, cy - 0.86f * r, cx + 0.86f * r, cy + 0.86f * r, 0.86f * r, kGold, 1.0f, 0xE0A800);
  // r216: a BIT COIN - the pixel ring + the 8-bit B (was a "T")
  xrcoin::bit_face([&](float x0, float y0, float x1, float y1, uint32_t rgb) { P.K.box(x0, y0, x1, y1, 0, rgb); }, cx, cy, r);
}
// a line chart of `vals` in the box: its fill under it (faint), the line, the last point lit
// (r210: the values as doubles - a grail's millions x 100 overflowed r201's ints)
void spark(Pen& P, float x0, float y0, float x1, float y1, const std::vector<double>& vals, uint32_t rgb, float width, bool fill) {
  if (vals.size() < 2) return;
  double lo = vals[0], hi = vals[0];
  for (double v : vals) lo = std::min(lo, v), hi = std::max(hi, v);
  lo = std::min(lo, hi - std::max(1e-9, hi / 4.0));  // (at least a quarter of its value tall: noise stays small)
  const double span = std::max(1e-9, hi - lo);
  const float pad = 0.12f * (y1 - y0);
  auto X = [&](size_t i) { return x0 + (x1 - x0) * i / static_cast<float>(vals.size() - 1); };
  auto Y = [&](double v) { return y1 - pad - (y1 - y0 - 2 * pad) * static_cast<float>((v - lo) / span); };
  for (size_t i = 0; i + 1 < vals.size(); ++i) {
    const float ax = X(i), ay = Y(vals[i]), bx = X(i + 1), by = Y(vals[i + 1]);
    if (fill) P.K.poly({ax, ay, bx, by, bx, y1, ax, y1}, rgb, 0.12f);
    const float dx = bx - ax, dy = by - ay, l = std::max(0.01f, std::sqrt(dx * dx + dy * dy));
    const float nx = -dy / l * width * 0.5f, ny = dx / l * width * 0.5f;
    P.K.poly({ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny}, rgb, 1.0f);
  }
  const float lx = X(vals.size() - 1), ly = Y(vals.back());
  P.K.glow(lx - 4 * width, ly - 4 * width, lx + 4 * width, ly + 4 * width, rgb, 0.6f);
  P.K.box(lx - width * 1.3f, ly - width * 1.3f, lx + width * 1.3f, ly + width * 1.3f, width * 1.3f, 0xFFFFFF);
}
// a card in miniature: its finish's frame, its picture, its name strip; a slab's clear case + grade on top
void mini(Pen& P, const Variant& v, float x0, float y0, float x1, float y1);
thread_local bool g_miniFlat = false;  // r220: (mini_cut drawing the mini it cuts)
// r220: a JAM '96 mini in its real die: the mini drawn as before into its own small picture, its card cut to the die (lk::jam_shape, from
// xr_cards through Source::cut_sil), laid in as one picture (its slab's case, round it, untouched)
bool mini_cut(Pen& P, const Variant& v, float x0, float y0, float x1, float y1) {
  if (g_src.cut_sil == nullptr || P.keep == nullptr || g_miniFlat) return false;
  const uint8_t* spx = nullptr;
  int sw = 0, sh = 0;
  std::shared_ptr<void> hold = g_src.cut_sil(v, &spx, &sw, &sh);
  if (spx == nullptr || sw <= 0 || sh <= 0) return false;
  const float w = x1 - x0, h = y1 - y0;
  const float ex0 = std::floor(x0 - 0.08f * w), ey0 = std::floor(y0 - 0.07f * h);
  const int iw = static_cast<int>(std::ceil(x1 + 0.08f * w - ex0)), ih = static_cast<int>(std::ceil(y1 + 0.06f * h - ey0));
  if (iw <= 2 || ih <= 2 || iw > 600 || ih > 800) return false;
  Pen Q;
  std::vector<std::shared_ptr<void>> keep;
  Q.keep = &keep;
  g_miniFlat = true;
  mini(Q, v, x0 - ex0, y0 - ey0, x1 - ex0, y1 - ey0);
  g_miniFlat = false;
  auto img = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(iw) * ih * 4, 0);
  xrtext::board(img->data(), iw, ih, Q.v);
  const float cy0 = v.grade > 0 ? y0 + 0.22f * h : y0;  // (the card under a slab's label)
  for (int y = 0; y < ih; ++y)
    for (int x = 0; x < iw; ++x) {
      uint8_t* q = &(*img)[(static_cast<size_t>(y) * iw + x) * 4];
      if (q[3] == 0) continue;
      for (int k = 0; k < 3; ++k) q[k] = static_cast<uint8_t>(std::min(255, q[k] * 255 / q[3]));  // (board: premultiplied)
      const float X = ex0 + x + 0.5f, Y = ey0 + y + 0.5f;
      if (X < x0 || X >= x1 || Y < cy0 || Y >= y1) continue;  // (outside the card: its slab's case)
      const int sx = std::min(sw - 1, static_cast<int>((X - x0) / w * sw)), sy = std::min(sh - 1, static_cast<int>((Y - cy0) / (y1 - cy0) * sh));
      q[3] = static_cast<uint8_t>(q[3] * spx[(static_cast<size_t>(sy) * sw + sx) * 4 + 3] / 255);
    }
  P.keep->push_back(img);
  P.keep->push_back(hold);
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kImage;
  p.x0 = ex0, p.y0 = ey0, p.x1 = ex0 + iw, p.y1 = ey0 + ih;
  p.img = img->data();
  p.iw = iw;
  p.ih = ih;
  P.v.push_back(p);
  return true;
}
void mini(Pen& P, const Variant& v, float x0, float y0, float x1, float y1) {
  struct ArtFrom {  // r236: a mini card is card art - the hub's LOOK B leaves its own lettering (and sizes) as they are
    std::vector<xrtext::BoardPiece>& v;
    size_t at;
    ~ArtFrom() {
      for (size_t i = at; i < v.size(); ++i) v[i].art = true;
    }
  } artMini{P.v, P.v.size()};
  if (v.card < 0 || v.card >= static_cast<int>(g_set.size())) return;
  const CardInfo& c = g_set[static_cast<size_t>(v.card)];
  if (c.set == 1 && mini_cut(P, v, x0, y0, x1, y1)) return;  // r220
  const float w = x1 - x0, h = y1 - y0, r = 0.07f * w;
  float cy0 = y0;
  if (v.grade > 0) {  // the slab: a clear case, its label on top
    P.K.box(x0 - 0.06f * w, y0 - 0.05f * h, x1 + 0.06f * w, y1 + 0.04f * h, r * 1.2f, 0xDDE6F5, 0.22f);
    P.K.ring(x0 - 0.06f * w, y0 - 0.05f * h, x1 + 0.06f * w, y1 + 0.04f * h, r * 1.2f, 2.0f, 0xE8F0FF, 0.7f);
    P.K.box(x0, y0, x1, y0 + 0.2f * h, 3, 0xF4F5FA);
    P.K.box(x1 - 0.36f * w, y0 + 0.02f * h, x1 - 0.02f * w, y0 + 0.18f * h, 3, 0x182040);
    P.K.text(std::to_string(v.grade), x1 - 0.19f * w, y0 + 0.105f * h, 0.15f * h, 0.32f * w, kGold, 0.0f);
    P.K.left("TIZM", x0 + 0.05f * w, y0 + 0.105f * h, 0.11f * h, 0.5f * w, 0x182040, 0.0f);
    cy0 = y0 + 0.22f * h;
  }
  const float ch = y1 - cy0;
  const uint32_t fr = v.hit == kHitBoom ? 0xFF6A1A : kFinishFrame[v.parallel];
  P.K.box(x0, cy0, x1, y1, r, fr, 1.0f, v.parallel == kGoldTizm ? 0xB07A10 : (v.parallel == kSilver ? 0x8C9AB8 : fr));
  P.K.ring(x0, cy0, x1, y1, r, 2.0f, kFinishEdge[v.parallel]);
  const float m = 0.07f * w;
  const bool jam = c.set == 1;  // r217: a JAM '96 card - its 90s marble colours, a '96 sticker
  const bool exq = c.set == 2;  // r230: a LANCINI EXQUISITE card - NOIR's black + gold, its tier's colour in its window
  if (exq) {
    const int t = exq_t(v);
    const uint32_t tc = t == xrexq::kEmerald ? 0x0E4A30 : (t == xrexq::kSapphire ? 0x14306A : (t == xrexq::kMasterpiece ? 0x3A2A08 : 0x1A1612));
    P.K.box(x0, cy0, x1, y1, r, 0x0C0C0E);
    P.K.ring(x0 + 1, cy0 + 1, x1 - 1, y1 - 1, r, 2.0f, t >= xrexq::kGoldLeaf ? 0xE6C774 : 0xB8A57A);
    P.K.box(x0 + m, cy0 + m, x1 - m, y1 - 0.2f * ch, r * 0.6f, xrmenu::mix(c.c1, tc, 0.55f), 1.0f, tc);
  } else if (jam) P.K.box(x0 + m, cy0 + m, x1 - m, y1 - 0.2f * ch, r * 0.6f, 0xE0218A, 1.0f, 0x18C6C0);
  else P.K.box(x0 + m, cy0 + m, x1 - m, y1 - 0.2f * ch, r * 0.6f, c.c1, 1.0f, xrmenu::mix(c.c1, 0x000000, 0.5f));
  if (!exq && (v.parallel == kSilver || v.parallel == kGoldTizm || v.parallel == kBlack)) {  // (the refractor's prism behind)
    static const uint32_t kBand[6] = {0xFF2E88, 0xFFB020, 0xFFE94A, 0x39FF88, 0x2EF2FF, 0x9B5CFF};
    for (int i = 0; i < 5; ++i) {
      const float a = x0 + m + (x1 - x0 - 2 * m) * (i * 0.21f), b = a + 0.08f * w;
      P.K.poly({a, y1 - 0.2f * ch, b, y1 - 0.2f * ch, b + 0.25f * w, cy0 + m, a + 0.25f * w, cy0 + m}, kBand[i % 6], 0.16f);
    }
  }
  if (g_src.portrait != nullptr) {
    const uint8_t* px = nullptr;
    int iw = 0, ih = 0;
    std::shared_ptr<void> k = g_src.portrait(v.card, &px, &iw, &ih);
    if (px != nullptr && iw > 0 && ih > 0) {
      if (P.keep != nullptr) P.keep->push_back(k);
      xrtext::BoardPiece p;
      p.kind = xrtext::BoardPiece::kImage;
      p.x0 = x0 + m;
      p.y0 = cy0 + (g_miniFlat ? std::max(m, 0.11f * ch) : m);  // (r220: in a die, its picture clear of a crown's valleys - smaller, never cut)
      p.x1 = x1 - m;
      p.y1 = y1 - 0.2f * ch;
      p.img = px;
      p.iw = iw;
      p.ih = ih;
      p.bottom = true;
      p.trim = true;
      p.focusX = 0.5f;
      p.focusY = 0.4f;
      P.v.push_back(p);
    }
  }
  P.K.box(x0 + m * 0.6f, y1 - 0.19f * ch, x1 - m * 0.6f, y1 - m * 0.6f, r * 0.5f, v.parallel == kBlack ? 0x08080C : 0x101830);
  std::string nm = upper(c.name);
  if (jam && nm.compare(0, 8, "JAM '96 ") == 0) nm = nm.substr(8);  // (its sticker says it)
  if (exq && nm.compare(0, 10, "EXQUISITE ") == 0) nm = nm.substr(10);  // (r230: its EXQ tag says it)
  P.K.text(nm, 0.5f * (x0 + x1), y1 - 0.105f * ch, std::min(0.1f * ch, 0.16f * w), w - 2 * m, exq ? 0xE6C774 : (v.parallel == kBlack ? kGold : (jam ? 0xFFD400 : 0xFFFFFF)), 0.0f);
  if (exq) {
    P.K.box(x0 + 0.04f * w, cy0 + 0.03f * ch, x0 + 0.52f * w, cy0 + 0.15f * ch, 0.06f * ch, 0x0C0C0E);
    P.K.text("EXQ", x0 + 0.28f * w, cy0 + 0.09f * ch, 0.1f * ch, 0.44f * w, 0xE6C774, 0.0f);
  }
  if (v.shiny) P.K.text("*", x1 - 0.14f * w, cy0 + 0.12f * ch, 0.14f * ch, 0.2f * w, kGold, 0.0f);
  if (jam) {
    const float sy = g_miniFlat ? 0.10f * ch : 0.0f;  // (r220: in a die, under a crown's valleys)
    P.K.box(x0 + 0.04f * w, cy0 + 0.03f * ch + sy, x0 + 0.46f * w, cy0 + 0.15f * ch + sy, 0.06f * ch, 0x0C0814);
    P.K.text("'96", x0 + 0.25f * w, cy0 + 0.09f * ch + sy, 0.1f * ch, 0.38f * w, 0xFFD400, 0.0f);
  }
}
void pct_tag(Pen& P, float pct, float x, float cy, float px, int align) {
  const bool up = pct >= 0.0f;
  const std::string t = (up ? "+" : "-") + std::to_string(static_cast<int>(std::lround(std::fabs(pct)))) + "%";
  const float w = xrtext::board_text_width(t, px) + px * 1.4f;
  const float bx0 = align < 0 ? x : (align > 0 ? x - w : x - 0.5f * w);
  P.K.box(bx0, cy - px * 0.62f, bx0 + w, cy + px * 0.62f, px * 0.62f, up ? 0x0E3A22 : 0x3A0E1A);
  const float ax = bx0 + px * 0.55f;
  if (up) P.K.poly({ax - px * 0.3f, cy + px * 0.22f, ax + px * 0.3f, cy + px * 0.22f, ax, cy - px * 0.28f}, kUp);
  else P.K.poly({ax - px * 0.3f, cy - px * 0.22f, ax + px * 0.3f, cy - px * 0.22f, ax, cy + px * 0.28f}, kDown);
  P.K.left(t, ax + px * 0.45f, cy + 1, px, w, up ? kUp : kDown, 0.0f);
}
std::vector<double> hist_of(const Variant& v) {  // (the chart's shape from the unrounded prices)
  std::vector<double> h;
  for (int i = 0; i < static_cast<int>(g_mk.days.size()); ++i) h.push_back(price_d(v, i));
  return h;
}
// r210: coins as they fit: the whole number with commas when it fits `room` at `px`, else short ("4.2M")
std::string money(long long n, float px, float room) {
  const std::string t = coins_text(n);
  return xrtext::board_text_width(t, px) <= room ? t : xrcoin::compact(n);
}
float chg_of(const Variant& v) {
  const int n = static_cast<int>(g_mk.days.size());
  if (n < 2) return 0.0f;
  const double a = price_d(v, -1), b = price_d(v, n - 2);
  return static_cast<float>(100.0 * (a - b) / std::max(0.01, b));
}
std::string pop_text(const Variant& v) {
  if (v.parallel == kBlack) return "1 OF 1 IN THE WORLD";
  if (v.parallel == kGoldTizm) return "10 IN THE WORLD";
  return "POP " + coins_text(pop_of(v));
}

// r210: a character's demand on a little pill - r231: its place in the TOP CARDS race by play ("#3 IN DEMAND" gold for the top 5, pink
// the top 20, cyan the top 60, dim the rest; "NOT IN YOUR LEAGUE")
void tier_pill(Pen& P, int card, float x, float cy, float px) {
  const int t = tier_of(card);
  static const uint32_t kCol[6] = {0, 0x6A7290, 0x8E99B8, 0x2EF2FF, 0xFF2E88, 0xFFD23F};
  const Line* l = line_of(card);
  const std::string w = l != nullptr && l->rank > 0 ? "#" + std::to_string(l->rank) + " IN DEMAND" : std::string("NOT IN YOUR LEAGUE");
  const float tw = xrtext::board_text_width(w, px) + px * 1.2f;
  P.K.box(x, cy - px * 0.62f, x + tw, cy + px * 0.62f, px * 0.62f, xrmenu::mix(kCol[t], 0x000000, 0.72f));
  P.K.ring(x, cy - px * 0.62f, x + tw, cy + px * 0.62f, px * 0.62f, 2.0f, kCol[t], 0.9f);
  P.K.text(w, x + 0.5f * tw, cy + 1, px, tw, kCol[t], 0.0f);
}
void draw_mine(Pen& P, float t) {
  std::vector<Holding> mine;  // r240: SELL's rows as shown (the cursor's copy the same in either look; r201-r239: mine_sorted)
  for (int i : g_sellRows) mine.push_back(g_sell[static_cast<size_t>(i)].h);
  if (mine.empty()) {
    P.K.text("NO CARDS YET", 520, 460, 72, 1000, kDim, 2.0f);
    P.K.text("OPEN PACKS FROM YOUR MATCHES - OR BUY FROM COLLECTORS", 520, 540, 36, 1000, kDim, 0.0f);
    return;
  }
  constexpr int kRows = 5;
  int& cur = g_cur[kTabMine];
  int& top = g_top[kTabMine];
  cur = std::max(0, std::min(static_cast<int>(mine.size()) - 1, cur));
  if (cur < top) top = cur;
  if (cur >= top + kRows) top = cur - kRows + 1;
  top = std::max(0, std::min(std::max(0, static_cast<int>(mine.size()) - kRows), top));
  P.K.left("CARD", 24, 214, 28, 200, kDim, 0.0f);
  P.K.right("PRICE", 760, 214, 28, 200, kDim, 0.0f);
  P.K.text("LAST 12 MATCHDAYS", 900, 214, 28, 260, kDim, 0.0f);
  for (int r = 0; r < kRows && top + r < static_cast<int>(mine.size()); ++r) {
    const Holding& h = mine[static_cast<size_t>(top + r)];
    const float y0 = 234 + r * 118.0f, y1 = y0 + 108;
    const bool on = top + r == cur;
    P.K.box(0, y0, 1010, y1, 20, on ? kPanelHi : kPanel);
    if (on) xrmenu::focus(P.K, 0, y0, 1010, y1, 20);
    mini(P, h, 14, y0 + 8, 74, y1 - 8);
    const CardInfo& c = g_set[static_cast<size_t>(h.card)];
    const int listed = listed_count(h);
    P.K.left(upper(c.name) + (h.count > 1 ? "  X" + std::to_string(h.count) : ""), 92, y0 + 34, 38, listed > 0 ? 330 : 440, kInk, 2.0f);
    if (listed > 0) {  // r210: listed by you (a pink tag)
      P.K.box(430, y0 + 16, 560, y0 + 52, 18, 0x4A1440);
      P.K.text(listed > 1 ? "LISTED X" + std::to_string(listed) : "LISTED", 495, y0 + 35, 22, 120, kPink, 0.0f);
    }
    P.K.left(variant_words(h), 92, y0 + 74, 26, 470, h.parallel == kGoldTizm ? kGold : (h.parallel == kBlack ? 0xFFE9A0 : 0xB8C2E0), 0.0f);
    const long long p = price_i(h, -1);
    const std::string pt = money(p, 46, 168);
    coin(P, 760 - xrtext::board_text_width(pt, 46) - 26, y0 + 34, 14);
    P.K.right(pt, 760, y0 + 36, 46, 170, kInk, 2.0f);
    pct_tag(P, chg_of(h), 760, y0 + 76, 24, 1);
    std::vector<double> hv = hist_of(h);
    if (hv.size() > 12) hv.erase(hv.begin(), hv.end() - 12);
    if (!hv.empty()) spark(P, 790, y0 + 16, 1000, y1 - 14, hv, hv.back() >= hv.front() ? kUp : kDown, 4.0f, true);
  }
  if (mine.size() > static_cast<size_t>(kRows)) xrmenu::scroll_note(P.K, 505, 868, top, kRows, static_cast<int>(mine.size()), "CARD");
  // the one under the cursor, big: its chart over 24 matchdays, why it moves, what selling pays - or LIST FOR SALE at your price
  const Holding& h = mine[static_cast<size_t>(cur)];
  const float X0 = 1030, X1 = kW;
  P.K.box(X0, 200, X1, 890, 26, 0x0E152C);
  P.K.ring(X0, 200, X1, 890, 26, 2, kLine);
  mini(P, h, X0 + 24, 222, X0 + 184, 446);
  const CardInfo& c = g_set[static_cast<size_t>(h.card)];
  P.K.left(upper(c.name), X0 + 204, 250, 44, X1 - X0 - 220, kInk, 2.0f);
  P.K.left(variant_words(h), X0 + 204, 292, 24, X1 - X0 - 220, kGold, 0.0f);
  tier_pill(P, h.card, X0 + 204, 330, 20);
  const long long p = price_i(h, -1);
  const std::string pt = money(p, 64, X1 - X0 - 250);
  coin(P, X0 + 222, 382, 20);
  P.K.left(pt, X0 + 252, 384, 64, X1 - X0 - 260, kInk, 3.0f);
  pct_tag(P, chg_of(h), X0 + 206, 432, 26, -1);
  P.K.left(pop_text(h), X0 + 320, 432, 24, X1 - X0 - 340, kDim, 0.0f);
  const std::vector<double> hv = hist_of(h);
  P.K.box(X0 + 20, 462, X1 - 20, 652, 16, 0x0A1022);
  for (int g = 1; g < 4; ++g) P.K.box(X0 + 30, 462 + g * 47.0f, X1 - 30, 463 + g * 47.0f, 0, kLine, 0.6f);
  if (!hv.empty()) spark(P, X0 + 34, 470, X1 - 34, 644, hv, hv.back() >= hv.front() ? kUp : kDown, 5.0f, true);
  if (!hv.empty()) {
    const long long lo = static_cast<long long>(llr(*std::min_element(hv.begin(), hv.end()))),
                    hi = static_cast<long long>(llr(*std::max_element(hv.begin(), hv.end())));
    P.K.left("HIGH " + money(hi, 24, 170) + "   LOW " + money(lo, 24, 170), X0 + 30, 674, 24, 420, kDim, 0.0f);
    P.K.right(std::to_string(hv.size()) + " MATCHDAYS", X1 - 30, 674, 24, 160, kDim, 0.0f);
  }
  const Line* l = line_of(h.card);
  std::string why = l != nullptr ? (l->reason.empty() ? l->why.substr(0, l->why.find('|')) : l->reason) : std::string("NOT IN YOUR LEAGUE");
  // r231: another form of the family: its player's numbers are this card's too
  if (l != nullptr && l->activeCard >= 0 && l->activeCard != h.card && g_set[static_cast<size_t>(h.card)].base != l->activeCard)
    why = "HIS FAMILY: " + name_of_card(l->activeCard) + "  -  " + why;
  P.K.left("WHY IT MOVES", X0 + 30, 710, 24, 300, kDim, 0.0f);
  P.K.left(why, X0 + 30, 744, 28, X1 - X0 - 60, kInk, 0.0f);
  // the buttons: SELL NOW (what a dealer pays this second) and r210's LIST FOR SALE (your price, collectors look every matchday)
  const bool free = own_free(h);
  const float by0 = 790, by1 = 868, mid = 0.5f * (X0 + X1);
  P.K.box(X0 + 24, by0, mid - 8, by1, 28, free ? 0x0F4A2A : 0x161C2C, 1.0f, free ? 0x0A2E1A : 0x10141F);
  P.K.ring(X0 + 24, by0, mid - 8, by1, 28, 3, free ? kUp : kLine);
  xrmenu::glyph(P.K, "A", X0 + 34, 0.5f * (by0 + by1), 40);
  P.K.text("SELL NOW", 0.5f * (X0 + 24 + mid - 8) + 22, by0 + 24, 26, mid - X0 - 100, free ? 0xFFFFFF : kDim, 2.0f);
  P.K.text(free ? money(bid_of(p), 30, mid - X0 - 100) + " COINS" : (listed_count(h) == 0 && held_out_count(h) > 0 ? "IN A TRADE" : "LISTED"),  // (r213)
           0.5f * (X0 + 24 + mid - 8) + 22, by0 + 56, 30, mid - X0 - 100,
           free ? kUp : kPink, 0.0f);
  P.K.box(mid + 8, by0, X1 - 24, by1, 28, 0x2A1450, 1.0f, 0x180C30);
  P.K.ring(mid + 8, by0, X1 - 24, by1, 28, 3, kPink);
  xrmenu::glyph(P.K, "X", mid + 18, 0.5f * (by0 + by1), 40);
  P.K.text("LIST FOR SALE", 0.5f * (mid + 8 + X1 - 24) + 22, by0 + 24, 26, X1 - mid - 100, 0xFFFFFF, 2.0f);
  P.K.text("YOUR PRICE", 0.5f * (mid + 8 + X1 - 24) + 22, by0 + 56, 24, X1 - mid - 100, 0xFFB6E0, 0.0f);
  (void)t;
}
// r231 (Lance 11-12: "the race for #2 is FIERCE ... buying low and selling high"): the MOVERS tab is TOP CARDS - the league's most-wanted
// players by their play (their place, its move since the last matchday, the play behind it, their BASE card's price + move), and beside
// it the matchday's biggest risers + fallers (the buy-low / sell-high windows)
void draw_movers(Pen& P, float t) {
  const std::vector<TopCard> top = top_list(5);
  {
    const float x0 = 0.0f, x1 = 1000.0f;
    P.K.box(x0, 196, x1, 676, 26, 0x0E152C);
    P.K.ring(x0, 196, x1, 676, 26, 3, kGold, 0.8f);
    P.K.glow(x0 + 40, 176, x0 + 420, 256, kGold, 0.25f + 0.08f * std::sin(t * 3.0f));
    chrome(P, "TOP CARDS", x0 + 30, 238, 64, 360, -1);
    P.K.left("THE LEAGUE'S MOST-WANTED, BY PLAY", x0 + 400, 240, 26, 560, kDim, 0.0f);
    if (top.empty()) P.K.text("THE RACE STARTS WITH THE FIRST MATCHDAY", 0.5f * (x0 + x1), 470, 40, 900, kDim, 0.0f);
    for (size_t i = 0; i < top.size(); ++i) {
      const TopCard& c = top[i];
      const float y0 = 284 + i * 78.0f, y1 = y0 + 70, cy = 0.5f * (y0 + y1);
      P.K.box(x0 + 16, y0, x1 - 16, y1, 16, i == 0 ? 0x2A2410 : kPanel);
      P.K.text("#" + std::to_string(c.rank), x0 + 52, cy, 34, 64, i == 0 ? kGold : kInk, 0.0f);
      // its place's move: up green, down red, new gold
      const int mv = c.prevRank > 0 ? c.prevRank - c.rank : 0;
      const std::string mt = c.prevRank <= 0 ? "NEW" : (mv > 0 ? "+" + std::to_string(mv) : (mv < 0 ? std::to_string(mv) : "="));
      P.K.text(mt, x0 + 112, cy, 24, 50, c.prevRank <= 0 ? kGold : (mv > 0 ? kUp : (mv < 0 ? kDown : kDim)), 0.0f);
      Variant v;
      v.card = c.card;
      mini(P, v, x0 + 144, y0 + 6, x0 + 186, y1 - 6);
      P.K.left(name_of_card(c.card), x0 + 200, y0 + 24, 32, 430, kInk, 2.0f);
      P.K.left(c.why, x0 + 200, y0 + 54, 20, 560, 0xC4CCE4, 0.0f);
      P.K.right(money(c.price, 30, 140), x0 + 860, y0 + 24, 30, 140, kInk, 0.0f);
      pct_tag(P, c.pct, x1 - 30, y0 + 24, 24, 1);
    }
  }
  std::vector<Mover> hot, cold;
  movers(&hot, &cold);
  {
    const float x0 = 1016.0f, x1 = kW;
    P.K.box(x0, 196, x1, 676, 26, 0x0E152C);
    P.K.ring(x0, 196, x1, 676, 26, 3, kLine, 0.8f);
    chrome(P, "MOVERS", x0 + 26, 238, 56, 260, -1);
    P.K.left("THIS MATCHDAY", x0 + 300, 240, 24, 300, kDim, 0.0f);
    std::vector<std::pair<Mover, bool>> rows;
    for (size_t i = 0; i < hot.size() && i < 3; ++i) rows.push_back({hot[i], true});
    for (size_t i = 0; i < cold.size() && rows.size() < 5; ++i) rows.push_back({cold[i], false});
    if (rows.empty()) P.K.text("NOTHING MOVING", 0.5f * (x0 + x1), 470, 40, 560, kDim, 0.0f);
    for (size_t i = 0; i < rows.size(); ++i) {
      const Mover& m = rows[i].first;
      const uint32_t col = rows[i].second ? kUp : kDown;
      const float y0 = 284 + i * 78.0f, y1 = y0 + 70;
      P.K.box(x0 + 14, y0, x1 - 14, y1, 16, kPanel);
      P.K.box(x0 + 14, y0 + 10, x0 + 20, y1 - 10, 3, col);
      Variant v;
      v.card = m.card;
      mini(P, v, x0 + 30, y0 + 6, x0 + 72, y1 - 6);
      P.K.left(name_of_card(m.card), x0 + 86, y0 + 24, 28, 330, kInk, 2.0f);
      const Line* l = line_of(m.card);
      P.K.left(l != nullptr ? l->reason : std::string(), x0 + 86, y0 + 54, 18, 400, 0xC4CCE4, 0.0f);
      pct_tag(P, m.pct, x1 - 28, y0 + 24, 24, 1);
    }
  }  // the market's headlines (a magazine's market column)
  P.K.box(0, 692, kW, 890, 26, 0x0E152C);
  P.K.ring(0, 692, kW, 890, 26, 2, kLine);
  P.K.left("MARKET WIRE", 26, 724, 30, 300, kGold, 2.0f);
  for (size_t i = 0; i < g_mk.news.size() && i < 4; ++i) {
    const Headline& h = g_mk.news[i];
    P.K.box(26, 758 + i * 33.0f, 36, 768 + i * 33.0f, 5, h.rgb);
    P.K.left(h.text, 50, 764 + i * 33.0f, 26, kW - 90, i == 0 ? kInk : 0xC4CCE4, 0.0f);
  }
  if (g_mk.news.empty()) P.K.left("THE WIRE FILLS UP AS THE MATCHDAYS GO BY", 50, 764, 26, kW - 90, kDim, 0.0f);
}
// r235: THE BUY TAB = THE SINGLES MARKET's SEARCH - every copy for sale (the 20 collectors at their own prices + the dealer), its seller,
// serial / grade, its price against the market, the player's stats line; X opens the search panel (the letter grid + recent + filters,
// drawn over the left while the results stay live on the right)
uint32_t l_rank_rgb(int rank) { return rank <= 0 ? kDim : (rank <= 5 ? kGold : (rank <= 20 ? kPink : (rank <= 60 ? kCyan : 0xB8C2E0))); }
void search_row(Pen& P, const SearchRow& r, float x0, float y0, float x1, float y1, bool on, bool narrow) {
  P.K.box(x0, y0, x1, y1, 20, on ? kPanelHi : kPanel);
  if (on) xrmenu::focus(P.K, x0, y0, x1, y1, 20);
  P.K.box(x0, y0 + 12, x0 + 6, y1 - 12, 3, r.rgb, 0.9f);
  mini(P, r.v, x0 + 16, y0 + 8 + (r.v.grade > 0 ? 6 : 0), x0 + 76, y1 - 8);
  const float w = x1 - x0, nameRoom = narrow ? 330.0f : 470.0f;
  P.K.left(name_of_card(r.v.card), x0 + 92, y0 + 26, 30, nameRoom, kInk, 2.0f);
  std::string fin = variant_words(r.v);
  P.K.left(fin, x0 + 92, y0 + 54, 21, nameRoom, r.v.parallel == kGoldTizm ? kGold : (r.v.parallel == kBlack ? 0xFFE9A0 : 0xB8C2E0), 0.0f);
  // the player's play (r231/r232): this season's goals + assists, his place in the TOP CARDS race
  std::string stats;
  if (const Line* l = line_of(r.v.card)) {
    if (l->rank > 0 || l->seasonGoals > 0 || l->seasonAst > 0)
      stats = std::to_string(l->seasonGoals) + " G  " + std::to_string(l->seasonAst) + " A THIS SEASON" + (l->rank > 0 ? "  -  #" + std::to_string(l->rank) + " IN DEMAND" : std::string());
    if (l->retired) stats = "RETIRED - A LEGEND";
  }
  if (stats.empty()) stats = "NOT IN YOUR LEAGUE";
  P.K.left(stats, x0 + 92, y0 + 78, 18, nameRoom, l_rank_rgb(r.rank), 0.0f);
  // the seller
  const float sx = x0 + (narrow ? 440.0f : 600.0f), sroom = narrow ? 170.0f : 330.0f;
  P.K.box(sx, y0 + 19, sx + 14, y0 + 33, 7, r.rgb);
  P.K.left(r.who, sx + 22, y0 + 27, narrow ? 20 : 24, sroom, r.rgb, 0.0f);
  const std::string when = r.src == 0 ? std::string("THE DEALER'S STOCK - 112%") : (r.seller == 20 ? std::string("THE DEALER'S CASE") : (r.day >= g_mk.steps - 0 ? std::string("LISTED THIS MATCHDAY") : (g_mk.steps - r.day <= 1 ? std::string("LISTED 1 MATCHDAY AGO") : "LISTED " + std::to_string(g_mk.steps - r.day) + " MATCHDAYS AGO")));
  P.K.left(when, sx, y0 + 56, 17, sroom + 20, kDim, 0.0f);
  if (watching(r.v.card)) P.K.left("WATCHED", sx + 34 + std::min(sroom - 100.0f, xrtext::board_text_width(r.who, narrow ? 20.0f : 24.0f)), y0 + 27, 16, 110, kPink, 0.0f);
  // the price: its ask, its share of the market (green under, gold about, red over), the market
  const std::string at = money(r.ask, 38, narrow ? 190.0f : 260.0f);
  coin(P, x1 - 20 - xrtext::board_text_width(at, 38) - 20, y0 + 28, 13);
  P.K.right(at, x1 - 20, y0 + 30, 38, narrow ? 200 : 270, kInk, 2.0f);
  const double ratio = static_cast<double>(r.ask) / static_cast<double>(std::max(1LL, r.market));
  const uint32_t rc = ratio < 0.97 ? kUp : (ratio <= 1.06 ? kGold : kDown);
  const std::string pt = std::to_string(static_cast<int>(std::lround(std::min(99999.0, ratio * 100.0)))) + "% OF MARKET";
  const float pw = xrtext::board_text_width(pt, 17) + 22;
  P.K.box(x1 - 20 - pw, y0 + 47, x1 - 20, y0 + 67, 10, xrmenu::mix(rc, 0x000000, 0.7f));
  P.K.text(pt, x1 - 20 - 0.5f * pw, y0 + 57, 17, pw, rc, 0.0f);
  P.K.right("MARKET " + money(r.market, 17, 160), x1 - 20, y0 + 79, 17, 200, kDim, 0.0f);
  (void)w;
}
// r236: a result row in the hub (LOOK B) - the card, its name, its finish, its price; the rest is the focused one's (panel shut)
void search_row_b(Pen& P, const SearchRow& r, float x0, float y0, float x1, float y1, bool on) {
  xrmenu::TileSpec ts;
  ts.colour = on ? xrmenu::kBCards : 0x2F8CFF;
  ts.on = on;
  xrmenu::tile(P.K, x0, y0, x1, y1, ts);
  mini(P, r.v, x0 + 14, y0 + 6, x0 + 14 + (y1 - y0 - 12) * 0.72f, y1 - 6);
  const float tx = x0 + 30 + (y1 - y0 - 12) * 0.72f, cy = 0.5f * (y0 + y1);
  xrmenu::say(P.K, name_of_card(r.v.card), tx, cy - 16, 36, 0xFFFFFF, 'l', x1 - tx - 220);
  xrmenu::label(P.K, variant_words(r.v), tx, cy + 22, 30, r.v.parallel == kGoldTizm ? kGold : xrmenu::kBSoft, 'l', x1 - tx - 220);
  xrmenu::say(P.K, money(r.ask, 40, 190), x1 - 24, cy, 40, xrmenu::kBGold, 'r', 200);
}
// the search panel (r235): the query, the letter grid, recent searches, the filters - over the left, the results live at the right (r238:
// BUY's copies, or SALES' players)
void draw_kb(Pen& P, bool bcast) {
  P.K.glow(-40, 160, kKbX1 + 40, 920, kCyan, 0.12f);
  P.K.box(0, 196, kKbX1, 904, 26, 0x0B1226, 0.98f);  // (r239: 904 - the FAMILY chip's row; r235-r238 892)
  P.K.ring(0, 196, kKbX1, 904, 26, 3, kCyan);
  const bool grade = kb_mode() == 2;    // r241: GRADING's own query + chips
  const bool sell = kb_mode() == 1;     // r240: SELL's own query, chips and bulk keys
  const std::string& qq = kbq();
  chrome(P, grade ? "SEARCH YOUR CARDS TO GRADE" : (sell ? "SEARCH YOUR CARDS" : "SEARCH A PLAYER"), 24, 230, 30, 500, -1);
  P.K.box(20, 254, kKbX1 - 20, 324, 18, 0x050812);
  P.K.ring(20, 254, kKbX1 - 20, 324, 18, 3, qq.empty() ? kLine : kCyan);
  if (qq.empty()) P.K.left("TYPE A NAME", 40, 290, 34, 500, kDim, 0.0f);
  else P.K.left(qq, 40, 291, 44, kKbX1 - 120, 0xFFFFFF, 2.0f);
  {  // (the caret: steady - the page is drawn on change only)
    const float cw = qq.empty() ? 0.0f : std::min(kKbX1 - 120.0f, xrtext::board_text_width(qq, 44));
    P.K.box(46 + cw, 268, 52 + cw, 312, 2, kCyan);
  }
  for (int k = 0; k < kb_count(); ++k) {
    float x0, y0, x1, y1;
    if (!kb_box(k, &x0, &y0, &x1, &y1)) continue;
    const bool on = k == g_kbKey;
    std::string label;
    uint32_t face = 0x1C2448;
    if (grade && k >= kKbSet) {  // r241: GRADING's chips
      static const char* const kSetS[kFSets] = {"ALL", "LANCINI TIZM", "JAM '96", "EXQUISITE"};
      static const char* const kTypeS[kSTypes] = {"ANY", "BASE", "PARALLEL", "CASE HIT", "AUTO", "PATCH", "GRADED", "SHINY", "ROOKIE"};
      static const char* const kStatS[kGStats] = {"ALL", "CAN SEND", "LOCKED"};
      static const char* const kSortS[kGSorts] = {"VALUE HIGH-LOW", "VALUE LOW-HIGH", "FINISH", "NEWEST"};
      static const char* const kFinS[kSFinExq] = {"ANY", "BASE", "SILVER", "GOLD /10", "/25 & UNDER", "1/1"};
      const std::string fin = g_gfFin < kSFinExq ? std::string(kFinS[g_gfFin]) : "EXQ " + std::string(xrexq::short_name(g_gfFin - kSFinExq + 1));
      bool lit = false;
      face = 0x153A60;
      switch (k) {
        case kKbSet: label = std::string("SET: ") + kSetS[g_gfSet], lit = g_gfSet != 0; break;
        case kKbSet + 1: label = "FINISH: " + fin, lit = g_gfFin != 0; break;
        case kKbSet + 2: label = std::string("TYPE: ") + kTypeS[g_gfType], lit = g_gfType != 0; break;
        case kKbSet + 3: label = std::string("STATUS: ") + kStatS[g_gfStat], lit = g_gfStat != 0; break;
        case kKbSet + 4: label = std::string("SORT: ") + kSortS[g_gfSort]; break;
        default: label = g_gfFam ? "FAMILY: EVERY FORM" : "FAMILY: THIS FORM", lit = g_gfFam; break;
      }
      if (lit) face = 0x1E5A8A;
      if (on) P.K.glow(x0 - 12, y0 - 12, x1 + 12, y1 + 12, kCyan, 0.25f);
      P.K.box(x0, y0, x1, y1, 14, face, 1.0f, xrmenu::mix(face, 0x000000, 0.35f));
      P.K.ring(x0, y0, x1, y1, 14, on ? 4.0f : 2.0f, on ? kCyan : (lit ? kCyan : kLine));
      P.K.text(label, 0.5f * (x0 + x1), 0.5f * (y0 + y1) + 1, 30.0f, x1 - x0 - 14, 0xFFFFFF, 0.0f);
      continue;
    }
    if (sell && k >= kKbSet) {  // r240: SELL's chips + bulk keys
      static const char* const kSetS[kFSets] = {"ALL", "LANCINI TIZM", "JAM '96", "EXQUISITE"};
      static const char* const kTypeS[kSTypes] = {"ANY", "BASE", "PARALLEL", "CASE HIT", "AUTO", "PATCH", "GRADED", "SHINY", "ROOKIE"};
      static const char* const kStatS[kSStats] = {"ALL", "NOT LISTED", "LISTED", "SPARES"};
      static const char* const kPriceS[kSPrices] = {"ANY", "UNDER 100", "100 - 1K", "1K - 10K", "10K - 100K", "100K - 1M", "1M +"};
      static const char* const kSortS[kSSorts] = {"VALUE HIGH-LOW", "VALUE LOW-HIGH", "GAINERS", "LOSERS", "NEWEST", "TRENDING", "SERIAL # LOW"};
      static const char* const kFinS[kSFinExq] = {"ANY", "BASE", "SILVER", "GOLD /10", "/25 & UNDER", "1/1"};
      const std::string fin = g_sfFin < kSFinExq ? std::string(kFinS[g_sfFin]) : "EXQ " + std::string(xrexq::short_name(g_sfFin - kSFinExq + 1));
      bool lit = false;
      face = 0x153A60;
      switch (k) {
        case kKbSet: label = std::string("SET: ") + kSetS[g_sfSet], lit = g_sfSet != 0; break;
        case kKbSet + 1: label = "FINISH: " + fin, lit = g_sfFin != 0; break;
        case kKbSet + 2: label = std::string("TYPE: ") + kTypeS[g_sfType], lit = g_sfType != 0; break;
        case kKbSet + 3: label = std::string("STATUS: ") + kStatS[g_sfStat], lit = g_sfStat != 0; break;
        case kKbSet + 4: label = std::string("PRICE: ") + kPriceS[g_sfPrice], lit = g_sfPrice != 0; break;
        case kKbSet + 5: label = std::string("SORT: ") + kSortS[g_sfSort]; break;
        case kKbSet + 6: label = g_sfFam ? "FAMILY: EVERY FORM" : "FAMILY: THIS FORM", lit = g_sfFam; break;
        case kKbSLast: label = g_sLast ? "LAST COPY: SELL TOO" : "LAST COPY: KEEP", face = g_sLast ? 0x8A2A1E : 0x153A60; break;
        case kKbSellAll: label = "SELL ALL SHOWN", face = 0x1E7A44; break;
        case kKbListAll: label = "LIST ALL SHOWN", face = 0x5A2A7A; break;
        default: label = "AT " + std::to_string(kSListPct[g_sPct]) + "%", face = 0x5A2A7A; break;
      }
      if (lit) face = 0x1E5A8A;
      if (on) P.K.glow(x0 - 12, y0 - 12, x1 + 12, y1 + 12, kCyan, 0.25f);
      P.K.box(x0, y0, x1, y1, 14, face, 1.0f, xrmenu::mix(face, 0x000000, 0.35f));
      P.K.ring(x0, y0, x1, y1, 14, on ? 4.0f : 2.0f, on ? kCyan : (lit ? kCyan : kLine));
      P.K.text(label, 0.5f * (x0 + x1), 0.5f * (y0 + y1) + 1, 30.0f, x1 - x0 - 14, 0xFFFFFF, 0.0f);
      continue;
    }
    if (k < kKbKeys) {
      label = kKbLabel[k];
      face = k == 31 ? 0x1E7A44 : (k >= 29 ? 0x2E1E4A : 0x1C2448);
    } else if (k < kKbSet) {
      label = g_recent[static_cast<size_t>(k - kKbRecent)];
      face = 0x14202E;
    } else if (k == kKbFam) {  // r239: the FAMILY chip (lit when on)
      label = g_fFam ? "FAMILY: EVERY FORM" : "FAMILY: THIS FORM ONLY";
      face = g_fFam ? 0x1E7A44 : 0x153A60;
    } else {
      static const char* const kHead[4] = {"SET: ", "FINISH: ", "PRICE: ", "SORT: "};
      const int i = k - kKbSet;
      label = std::string(kHead[i]) + (i == 0 ? kSetWord[g_fSet] : (i == 1 ? kFinWord[g_fFin] : (i == 2 ? kPriceWord[g_fPrice] : kSortWord[g_fSort])));
      face = 0x153A60;
    }
    if (on) P.K.glow(x0 - 12, y0 - 12, x1 + 12, y1 + 12, k == 31 ? kUp : kCyan, 0.25f);
    P.K.box(x0, y0, x1, y1, 14, face, 1.0f, xrmenu::mix(face, 0x000000, 0.35f));
    P.K.ring(x0, y0, x1, y1, 14, on ? 4.0f : 2.0f, on ? (k == 31 ? kUp : kCyan) : kLine);
    const float px = k < 26 ? 34.0f : (k < kKbKeys ? (k < 28 ? 34.0f : 20.0f) : 19.0f);
    P.K.text(label, 0.5f * (x0 + x1), 0.5f * (y0 + y1) + 1, px, x1 - x0 - 12, 0xFFFFFF, k < 28 ? 2.0f : 0.0f);
  }
  if (sell || grade) return;  // r240: (SELL's recent row sits where that label was; the hub's bar says the buttons) - r241: GRADING's too
  P.K.left(g_recent.empty() && !bcast ? "RECENT SEARCHES SHOW UP HERE" : "RECENT SEARCHES", 24, 648, 16, 400, kDim, 0.0f);  // (r236: short in the hub)
  if (!bcast) P.K.right("A PRESS  -  X DEL  -  B CLOSE  -  OR POINT + TRIGGER", kKbX1 - 24, 852, 16, 320, kDim, 0.0f);  // (r236: the hub's bar says them; r239: beside the FAMILY chip)
}
void draw_buy(Pen& P, float t) {
  const bool bcast = xrmenu::broadcast();  // r236
  const int n = static_cast<int>(g_rows.size());
  int& cur = g_cur[kTabBuy];
  int& top = g_top[kTabBuy];
  cur = std::max(0, std::min(std::max(0, n - 1), cur));
  if (cur < top) top = cur;
  if (cur >= top + kRowsShown) top = cur - kRowsShown + 1;
  top = std::max(0, std::min(std::max(0, n - kRowsShown), top));
  int sellers = 0;
  {
    std::vector<std::string> seen;
    for (const SearchRow& r : g_rows)
      if (std::find(seen.begin(), seen.end(), r.who) == seen.end()) seen.push_back(r.who);
    sellers = static_cast<int>(seen.size());
  }
  const std::string count = n == 0 ? std::string("NOTHING FOR SALE MATCHES") : std::to_string(n) + (n == 1 ? " COPY" : " COPIES") + " FOR SALE  -  " + std::to_string(sellers) + (sellers == 1 ? " SELLER" : " SELLERS");
  const float rx0 = g_kb ? kRowsX0Open : 0.0f;
  if (!g_kb) {
    // the search bar: X opens the panel; the query; how many copies, how many sellers
    P.K.box(0, 196, kW, 262, 22, 0x0E152C);
    P.K.ring(0, 196, kW, 262, 22, 2, g_q.empty() ? kLine : kCyan);
    xrmenu::glyph(P.K, "X", 34, 229, 40);
    P.K.left("SEARCH", 88, 230, 24, 100, kDim, 0.0f);
    if (g_q.empty()) P.K.left("PRESS X AND TYPE A PLAYER - \"TOG\" FINDS EVERY TOGEPI FOR SALE", 200, 230, 24, 880, kDim, 0.0f);
    else P.K.left("\"" + g_q + "\"", 200, 231, 36, 600, 0xFFFFFF, 2.0f);
    P.K.right(count, kW - 24, 230, 24, 640, n > 0 ? kInk : kDim, 0.0f);
    // the filters (A on them in the panel) + the watch list
    const std::string chips[5] = {kSetWord[g_fSet], kFinWord[g_fFin], kPriceWord[g_fPrice], std::string("SORT: ") + kSortWord[g_fSort],
                                  g_fFam ? "FAMILY: EVERY FORM" : "FAMILY: OFF"};  // (r239: + FAMILY)
    float cx = 0;
    for (int i = 0; i < 5; ++i) {
      const bool set = (i == 0 && g_fSet != 0) || (i == 1 && g_fFin != 0) || (i == 2 && g_fPrice != 0) || i == 3 || (i == 4 && g_fFam);
      const float cw = xrtext::board_text_width(chips[i], 18) + 26;
      P.K.box(cx, 268, cx + cw, 294, 13, set ? 0x153A60 : 0x0F1630);
      P.K.ring(cx, 268, cx + cw, 294, 13, 2, set ? kCyan : kLine, 0.9f);
      P.K.text(chips[i], cx + 0.5f * cw, 281, 18, cw, set ? 0xFFFFFF : kDim, 0.0f);
      cx += cw + 10;
    }
    std::string wl;
    for (size_t i = 0; i < g_watch.size() && i < 4; ++i) {
      std::string nm = g_watch[i];
      for (const auto& kv : g_mk.lines)
        if (kv.first == g_watch[i] && kv.second.activeCard >= 0) nm = search_name(kv.second.activeCard);
      if (nm.compare(0, 2, "N:") == 0 || nm.compare(0, 2, "n:") == 0) nm = upper(nm.substr(2));
      wl += (i ? ", " : "") + nm;
    }
    if (!wl.empty()) P.K.right("WATCHING: " + wl + (g_watch.size() > 4 ? " +" + std::to_string(g_watch.size() - 4) : ""), kW - 24, 281, 18, kW - cx - 40, kPink, 0.0f);
  } else {
    P.K.left(count, rx0 + 10, 230, 24, kW - rx0 - 20, n > 0 ? kInk : kDim, 0.0f);
    if (!bcast) P.K.left("LIVE - THE LIST FOLLOWS EVERY LETTER", rx0 + 10, 264, 18, kW - rx0 - 20, kDim, 0.0f);  // (r236: not in the hub)
  }
  if (n == 0) {
    P.K.text(g_q.empty() ? "NOTHING FOR SALE YET - PLAY A MATCHDAY" : "NO COPIES OF \"" + g_q + "\" FOR SALE RIGHT NOW", 0.5f * (rx0 + kW), 520, 34, kW - rx0 - 40, kDim, 2.0f);
    P.K.text("COLLECTORS LIST NEW CARDS EVERY MATCHDAY - Y ON A COPY WATCHES ITS PLAYER", 0.5f * (rx0 + kW), 580, 22, kW - rx0 - 40, kDim, 0.0f);
  }
  for (int r = 0; r < kRowsShown && top + r < n; ++r) {
    const float y0 = kRowsY0 + r * kRowH;
    if (bcast) search_row_b(P, g_rows[static_cast<size_t>(top + r)], rx0, y0, kW, y0 + kRowH - 7, !g_kb && top + r == cur);  // (r236: the same bands)
    else search_row(P, g_rows[static_cast<size_t>(top + r)], rx0, y0, kW, y0 + kRowH - 7, !g_kb && top + r == cur, g_kb);
  }
  if (n > kRowsShown) xrmenu::scroll_note(P.K, 0.5f * (rx0 + kW), 882, top, kRowsShown, n, "COPY");
  if (!g_kb) return;
  draw_kb(P, bcast);  // (r238: its own function - the SALES tab's search shows it too)

}

// ---- r236 (LOOK B, logs/r235-menus/optB_market.png): the BUY tab with the search panel shut - the search bar (the query, how many
// found), the four filter chips, 5 results (the card, its name, its finish, its price) and the focused copy's panel at the right (the
// card big, its finish + pop, the market + its move, its seller, its place in the race and why, ONE button: BUY). The panel open keeps
// r235's layout (its keys and rows are the pointer's).
void draw_buy_b(Pen& P, float t) {
  (void)t;
  const int n = static_cast<int>(g_rows.size());
  constexpr int kRowsB = 5;
  int& cur = g_cur[kTabBuy];
  int& top = g_top[kTabBuy];
  cur = std::max(0, std::min(std::max(0, n - 1), cur));
  if (cur < top) top = cur;
  if (cur >= top + kRowsB) top = cur - kRowsB + 1;
  top = std::max(0, std::min(std::max(0, n - kRowsB), top));
  xrmenu::Kit& K = P.K;
  const float lx1 = 970.0f;
  // the search bar (X opens the panel). r237 (Lance didn't see SEARCH in r236's dim bar): a button now - a gold edge, the X button's
  // glyph, the search glass and SEARCH in white; what's typed after it once there is a query
  K.box(0, 104, lx1, 190, 0, xrmenu::kBNight, 0.95f);
  K.v.back().cut = 20.0f;
  K.v.back().styled = true;
  K.ring(0, 104, lx1, 190, 0, 3.0f, g_q.empty() ? xrmenu::kBGold : 0xFFFFFF, 0.95f);
  K.v.back().cut = 20.0f;
  K.v.back().styled = true;
  const float gw = xrmenu::glyph(K, "X", 28, 147, 54);
  xrmenu::icon(K, "search", 28 + gw + 36, 147, 40, xrmenu::kBGold);
  const float sx = 28 + gw + 70;
  if (g_q.empty()) {
    xrmenu::say(K, "SEARCH", sx, 147, 46, 0xFFFFFF, 'l', 220);
    xrmenu::label(K, "A PLAYER", sx + std::min(220.0f, xrtext::face_text_width("SEARCH", 46, xrtext::kFaceBroadcast)) + 18, 149, 34, xrmenu::kBSoft, 'l', 220);
  } else {
    xrmenu::say(K, g_q, sx, 147, 46, 0xFFFFFF, 'l', lx1 - sx - 300);
  }
  xrmenu::label(K, n == 0 ? std::string("NONE FOUND") : std::to_string(n) + " FOUND", lx1 - 30, 147, 34, xrmenu::kBSoft, 'r', 260);
  // the filters (A on them in the panel: X)
  {
    // (r239: + FAMILY when it's on - "FAMILY" lit: every form of the player found)
    const std::string chips[5] = {std::string("SET: ") + (g_fSet == 0 ? "ALL" : kSetWord[g_fSet]), std::string("FINISH: ") + (g_fFin == 0 ? "ANY" : kFinWord[g_fFin]),
                                  std::string("PRICE: ") + (g_fPrice == 0 ? "ANY" : kPriceWord[g_fPrice]), std::string("SORT: ") + kSortWord[g_fSort], "FAMILY"};
    float cx = 0;
    const int nc = g_fFam ? 5 : 4;
    for (int n = 0; n < nc; ++n) {
      const int i = g_fFam ? (n == 0 ? 4 : n - 1) : n;  // (FAMILY first when on: always in sight)
      const bool set = (i == 0 && g_fSet != 0) || (i == 1 && g_fFin != 0) || (i == 2 && g_fPrice != 0) || i == 4;
      const float room = (lx1 - cx) - (nc - 1 - n) * 10.0f;
      if (room < 120) break;
      if (g_fFam && xrtext::face_text_width(chips[i], 30, xrtext::kFaceBroadcast) + 48.0f > lx1 - cx) break;  // (r239: 5 chips - one that won't fit stays off)
      cx += std::min(room, xrmenu::chip(K, chips[i], cx, 236, set ? 0x2B3D66 : 0x18233A, 'l', 30)) + 10;
    }
  }
  if (n == 0) {
    xrmenu::say(K, g_q.empty() ? "NOTHING FOR SALE YET" : "NO COPIES FOR SALE", 0.5f * lx1, 520, 56, xrmenu::kBDim, 'c', lx1 - 40);
    return;
  }
  const float ry0 = 286, rh = (898 - ry0 - (kRowsB - 1) * 12.0f) / kRowsB;
  for (int r = 0; r < kRowsB && top + r < n; ++r) {
    const SearchRow& row = g_rows[static_cast<size_t>(top + r)];
    const float y0 = ry0 + r * (rh + 12.0f), y1 = y0 + rh;
    xrmenu::TileSpec ts;
    ts.colour = top + r == cur ? 0xA855F7 : 0x2F8CFF;
    ts.on = top + r == cur;
    xrmenu::tile(K, 0, y0, lx1, y1, ts);
    mini(P, row.v, 20, y0 + 8, 20 + (rh - 16) * 0.72f, y1 - 8);
    const float tx = 40 + (rh - 16) * 0.72f;
    xrmenu::say(K, name_of_card(row.v.card), tx, y0 + rh / 2 - 16, 40, 0xFFFFFF, 'l', lx1 - tx - 260);
    xrmenu::label(K, variant_words(row.v), tx, y0 + rh / 2 + 24, 30, row.v.parallel == kGoldTizm ? kGold : xrmenu::kBSoft, 'l', lx1 - tx - 260);
    const std::string at = money(row.ask, 44, 210);
    xrmenu::say(K, at, lx1 - 30, y0 + rh / 2, 44, xrmenu::kBGold, 'r', 220);
    xrmenu::bcoin(K, lx1 - 50 - std::min(220.0f, xrtext::face_text_width(at, 44, xrtext::kFaceBroadcast)) - 20, y0 + rh / 2, 38);
  }
  // the focused copy
  const SearchRow& r = g_rows[static_cast<size_t>(cur)];
  const float fx0 = 1010, fx1 = kW, fcx = 0.5f * (fx0 + fx1), fy0 = 104, fy1 = 898;
  xrmenu::panel(K, fx0, fy0, fx1, fy1, kGold, 0.94f);
  const float by = fy1 - 120, vy = by - 50, ny = vy - 175;
  const float ch = ny - 70 - (fy0 + 30);
  K.glow(fcx - 220, fy0 + 30, fcx + 220, ny - 40, kGold, 0.22f);
  mini(P, r.v, fcx - ch * 0.36f, fy0 + 30, fcx + ch * 0.36f, ny - 60);
  xrmenu::say(K, name_of_card(r.v.card), fcx, ny - 10, 52, 0xFFFFFF, 'c', fx1 - fx0 - 40, true, 0.0f, 3.0f);
  // r237 (Lance's r236 freeze, "froze when i tried to go to the buy part"): pop_of - NOT population(), the public one that takes g_m again.
  // draw() holds g_m; std::mutex isn't recursive, so the hub's drawing thread waited on itself for ever, and the game thread behind it
  // (xrmarket::frame / generation take g_m every frame): the whole game stopped the moment BUY had a copy to show.
  xrmenu::label(K, variant_words(r.v) + "  -  POP " + std::to_string(pop_of(r.v)), fcx, ny + 40, 30, xrmenu::kBSoft, 'c', fx1 - fx0 - 40);
  std::string why;
  if (const Line* l = line_of(r.v.card)) why = l->rank > 0 ? "#" + std::to_string(l->rank) + " IN DEMAND" : std::string();
  xrmenu::label(K, r.who + (why.empty() ? std::string() : "  -  " + why), fcx, ny + 82, 30, r.rgb != 0 ? r.rgb : xrmenu::kBSoft, 'c', fx1 - fx0 - 40);
  xrmenu::label(K, "MARKET", fx0 + 40, vy - 40, 30);
  xrmenu::label(K, "GRIP IT: HOLD IT", fx1 - 40, vy - 40, 30, xrmenu::kBGold, 'r', 330);  // r238: (a grip on it brings this very copy into your hand)
  xrmenu::say(K, money(r.market, 46, 280), fx0 + 40, vy + 6, 46, 0xFFFFFF, 'l', 300);
  {
    const double ratio = static_cast<double>(r.ask) / static_cast<double>(std::max(1LL, r.market));
    const uint32_t rc = ratio < 0.97 ? kUp : (ratio <= 1.06 ? kGold : kDown);
    xrmenu::say(K, std::to_string(static_cast<int>(std::lround(std::min(99999.0, ratio * 100.0)))) + "% OF IT", fx1 - 40, vy + 6, 40, rc, 'r', 260);
  }
  xrmenu::button(K, fx0 + 30, by, fx1 - 30, by + 96, "BUY  " + money(r.ask, 50, 300), "A", true, true);
  if (n > kRowsB) {  // (more results: the arrows at the list's edge, lit where there are more)
    K.poly({lx1 + 20, ry0 + 6, lx1 + 32, ry0 - 18, lx1 + 44, ry0 + 6}, top > 0 ? xrmenu::kBGold : 0x3A4560);
    K.poly({lx1 + 20, 878, lx1 + 44, 878, lx1 + 32, 902}, top + kRowsB < n ? xrmenu::kBGold : 0x3A4560);
  }
}
// ---- r240 (Lance 11-13: "work on a way to sell better, like more filtered"): THE SELL TAB (LOOK B) - the search bar (X: SEARCH + FILTERS +
// SELL ALL), the filters that are on as chips, 4 rows (the card; its finish / number / grade; the player's season; its market value, this
// week's move, what SELL NOW pays - or why it's locked) and the focused copy's panel (the card big, its numbers, LIST FOR SALE on Y, SELL NOW on A)
std::string stat_short(int card) {  // "14 G  6 A  -  #3"
  const Line* l = line_of(card);
  if (l == nullptr) return "NOT IN YOUR LEAGUE";
  if (l->retired) return "RETIRED - A LEGEND";
  if (l->rank <= 0 && l->seasonGoals <= 0 && l->seasonAst <= 0) return "NOT IN YOUR LEAGUE";
  return std::to_string(l->seasonGoals) + " G  " + std::to_string(l->seasonAst) + " A" + (l->rank > 0 ? "  -  #" + std::to_string(l->rank) : std::string());
}
// why a row's copies can't all go now ("" all free): the biggest lock first
std::string lock_words(const SellItem& s) {
  if (s.h.grading) return grading_words(s.h);  // r242: "AT GRADING - 2 MATCHES" (never a grade: it has none yet)
  if (s.free > 0) return "";
  if (s.listed > 0) return "LISTED";
  if (s.museum > 0) return "IN MUSEUM";
  if (s.heldOut > 0) return "IN A TRADE";
  return "LOCKED";
}
std::string sell_chips_text(int i) {  // the shut page's chip i ("" not on): FAMILY, SET, FINISH, TYPE, STATUS, PRICE, then SORT (always)
  static const char* const kSetS[kFSets] = {"ALL", "LANCINI TIZM", "JAM '96", "EXQUISITE"};
  static const char* const kTypeS[kSTypes] = {"ANY", "BASE", "PARALLEL", "CASE HITS", "AUTOS", "PATCHES", "GRADED", "SHINY", "ROOKIES"};
  static const char* const kStatS[kSStats] = {"ALL", "NOT LISTED", "LISTED", "SPARES"};
  static const char* const kPriceS[kSPrices] = {"ANY", "UNDER 100", "100 - 1K", "1K - 10K", "10K - 100K", "100K - 1M", "1M +"};
  static const char* const kSortS[kSSorts] = {"VALUE HIGH-LOW", "VALUE LOW-HIGH", "GAINERS", "LOSERS", "NEWEST", "TRENDING", "SERIAL # LOW"};
  static const char* const kFinS[kSFinExq] = {"ANY", "BASE", "SILVER", "GOLD /10", "/25 & UNDER", "1/1"};
  switch (i) {
    case 0: return g_sfFam ? "FAMILY" : "";
    case 1: return g_sfSet != 0 ? kSetS[g_sfSet] : "";
    case 2: return g_sfFin == 0 ? "" : (g_sfFin < kSFinExq ? std::string(kFinS[g_sfFin]) : "EXQ " + std::string(xrexq::short_name(g_sfFin - kSFinExq + 1)));
    case 3: return g_sfType != 0 ? kTypeS[g_sfType] : "";
    case 4: return g_sfStat != 0 ? kStatS[g_sfStat] : "";
    case 5: return g_sfPrice != 0 ? kPriceS[g_sfPrice] : "";
    default: return std::string("SORT: ") + kSortS[g_sfSort];
  }
}
void draw_sell_b(Pen& P, float t) {
  (void)t;
  xrmenu::Kit& K = P.K;
  const int n = static_cast<int>(g_sellRows.size());
  constexpr int kRowsS = 4;
  int& cur = g_cur[kTabMine];
  int& top = g_top[kTabMine];
  cur = std::max(0, std::min(std::max(0, n - 1), cur));
  if (cur < top) top = cur;
  if (cur >= top + kRowsS) top = cur - kRowsS + 1;
  top = std::max(0, std::min(std::max(0, n - kRowsS), top));
  const float lx1 = 970.0f;
  int copies = 0;
  for (int i : g_sellRows) copies += g_sell[static_cast<size_t>(i)].h.count;
  // the search bar (as BUY's: a button - X opens SELL's panel: the name, the filters, SELL ALL / LIST ALL SHOWN)
  K.box(0, 104, lx1, 190, 0, xrmenu::kBNight, 0.95f);
  K.v.back().cut = 20.0f;
  K.v.back().styled = true;
  K.ring(0, 104, lx1, 190, 0, 3.0f, g_sq.empty() ? xrmenu::kBGold : 0xFFFFFF, 0.95f);
  K.v.back().cut = 20.0f;
  K.v.back().styled = true;
  const float gw = xrmenu::glyph(K, "X", 28, 147, 54);
  xrmenu::icon(K, "search", 28 + gw + 36, 147, 40, xrmenu::kBGold);
  const float sx = 28 + gw + 70;
  if (g_sq.empty()) {
    xrmenu::say(K, "SEARCH", sx, 147, 46, 0xFFFFFF, 'l', 220);
    xrmenu::label(K, "+ FILTERS", sx + std::min(220.0f, xrtext::face_text_width("SEARCH", 46, xrtext::kFaceBroadcast)) + 18, 149, 34, xrmenu::kBSoft, 'l', 260);
  } else {
    xrmenu::say(K, g_sq, sx, 147, 46, 0xFFFFFF, 'l', lx1 - sx - 300);
  }
  xrmenu::label(K, copies == 0 ? std::string("NONE") : coins_text(copies) + (copies == 1 ? " CARD" : " CARDS"), lx1 - 30, 147, 34, xrmenu::kBSoft, 'r', 260);
  {  // the filters that are on (+ the sort, always) - a chip that won't fit stays off
    float cx = 0;
    for (int i = 0; i < 7; ++i) {
      const std::string c = sell_chips_text(i);
      if (c.empty()) continue;
      if (xrtext::face_text_width(c, 30, xrtext::kFaceBroadcast) + 48.0f > lx1 - cx) break;
      cx += xrmenu::chip(K, c, cx, 236, i < 6 ? 0x2B3D66 : 0x18233A, 'l', 30) + 10;
    }
  }
  if (n == 0) {
    xrmenu::say(K, g_sell.empty() ? "NO CARDS YET" : "NONE MATCH", 0.5f * lx1, 520, 56, xrmenu::kBDim, 'c', lx1 - 40);
    return;
  }
  const float ry0 = 286, rh = (898 - ry0 - (kRowsS - 1) * 12.0f) / kRowsS;
  for (int r = 0; r < kRowsS && top + r < n; ++r) {
    const SellItem& s = g_sell[static_cast<size_t>(g_sellRows[static_cast<size_t>(top + r)])];
    const float y0 = ry0 + r * (rh + 12.0f), y1 = y0 + rh;
    xrmenu::TileSpec ts;
    ts.colour = top + r == cur ? 0xA855F7 : 0x2F8CFF;
    ts.on = top + r == cur;
    xrmenu::tile(K, 0, y0, lx1, y1, ts);
    mini(P, s.h, 18, y0 + 10, 18 + (rh - 20) * 0.72f, y1 - 10);
    const float tx = 36 + (rh - 20) * 0.72f, room = lx1 - tx - 270;
    xrmenu::say(K, name_of_card(s.h.card) + (s.h.count > 1 ? "  X" + std::to_string(s.h.count) : std::string()), tx, y0 + 34, 38, 0xFFFFFF, 'l', room);
    xrmenu::label(K, variant_words(s.h), tx, y0 + 76, 30, s.h.parallel == kGoldTizm || s.h.parallel == kBlack ? kGold : xrmenu::kBSoft, 'l', room);
    xrmenu::label(K, stat_short(s.h.card), tx, y0 + 114, 30, l_rank_rgb(s.rank), 'l', room);
    // the right: its market value, this week's move, SELL NOW's 90% (or its lock)
    const std::string at = money(s.market, 44, 210);
    xrmenu::say(K, at, lx1 - 26, y0 + 36, 44, xrmenu::kBGold, 'r', 220);
    xrmenu::bcoin(K, lx1 - 46 - std::min(220.0f, xrtext::face_text_width(at, 44, xrtext::kFaceBroadcast)) - 20, y0 + 36, 36);
    xrmenu::label(K, (s.chg >= 0.0f ? "+" : "-") + std::to_string(static_cast<int>(std::lround(std::fabs(s.chg)))) + "% THIS WEEK", lx1 - 26, y0 + 76, 30,
                  s.chg >= 0.5f ? kUp : (s.chg <= -0.5f ? kDown : xrmenu::kBSoft), 'r', 250);
    const std::string lk = lock_words(s);
    if (lk.empty()) xrmenu::label(K, "SELL " + money(s.bid, 30, 170), lx1 - 26, y0 + 114, 30, 0x3DDC84, 'r', 250);
    else xrmenu::label(K, lk, lx1 - 26, y0 + 114, 30, kPink, 'r', s.h.grading ? 410.0f : 250.0f);  // (r242: "AT GRADING - 2 MATCHES" at 30 px)
  }
  if (n > kRowsS) {  // (more: the arrows at the list's edge, lit where there are more)
    K.poly({lx1 + 20, ry0 + 6, lx1 + 32, ry0 - 18, lx1 + 44, ry0 + 6}, top > 0 ? xrmenu::kBGold : 0x3A4560);
    K.poly({lx1 + 20, 878, lx1 + 44, 878, lx1 + 32, 902}, top + kRowsS < n ? xrmenu::kBGold : 0x3A4560);
  }
  // the focused copy: big, its numbers, the two buttons
  const SellItem& s = g_sell[static_cast<size_t>(g_sellRows[static_cast<size_t>(cur)])];
  const float fx0 = 1010, fx1 = kW, fcx = 0.5f * (fx0 + fx1), fy0 = 104, fy1 = 898;
  xrmenu::panel(K, fx0, fy0, fx1, fy1, kGold, 0.94f);
  const float cy0 = fy0 + 24, cy1 = 424, ch = cy1 - cy0;
  K.glow(fcx - 220, cy0, fcx + 220, cy1, kGold, 0.22f);
  mini(P, s.h, fcx - ch * 0.36f, cy0, fcx + ch * 0.36f, cy1);
  xrmenu::say(K, name_of_card(s.h.card), fcx, 466, 48, 0xFFFFFF, 'c', fx1 - fx0 - 40, true, 0.0f, 3.0f);
  {  // its finish / number / grade (+ its pop when it fits at 30 px)
    const std::string fw = variant_words(s.h), fp = fw + "  -  POP " + std::to_string(pop_of(s.h));
    const bool fits = xrtext::face_text_width(fp, 30, xrtext::kFaceBroadcastUp, 0.14f) <= fx1 - fx0 - 40;
    xrmenu::label(K, fits ? fp : fw, fcx, 510, 30, s.h.parallel >= kGoldTizm ? kGold : xrmenu::kBSoft, 'c', fx1 - fx0 - 40);
  }
  xrmenu::label(K, stat_short(s.h.card), fcx, 548, 30, l_rank_rgb(s.rank), 'c', fx1 - fx0 - 40);
  {  // what you hold of it: how many, listed / museum / trade / grading, spare
    std::vector<std::string> parts = {"YOU HAVE " + std::to_string(s.h.count)};
    if (s.h.grading) parts.push_back(grading_words(s.h));  // r242
    if (s.listed > 0) parts.push_back(std::to_string(s.listed) + " LISTED");
    if (s.museum > 0) parts.push_back(std::to_string(s.museum) + " IN MUSEUM");
    if (s.heldOut > 0) parts.push_back(std::to_string(s.heldOut) + " IN A TRADE");
    if (s.spare > 0) parts.push_back(std::to_string(s.spare) + " SPARE");
    else if (s.free > 0) parts.push_back("YOUR LAST");
    std::string own;
    for (;;) {  // (as many as fit at 30 px - the first ones say the most)
      own.clear();
      for (size_t i = 0; i < parts.size(); ++i) own += (i ? "  -  " : "") + parts[i];
      if (parts.size() <= 1 || xrtext::face_text_width(own, 30, xrtext::kFaceBroadcastUp, 0.14f) <= fx1 - fx0 - 40) break;
      parts.pop_back();
    }
    xrmenu::label(K, own, fcx, 586, 30, s.free > 0 ? xrmenu::kBSoft : kPink, 'c', fx1 - fx0 - 40);
  }
  xrmenu::label(K, "MARKET", fx0 + 40, 630, 30);
  xrmenu::label(K, "GRIP IT: HOLD IT", fx1 - 40, 630, 30, xrmenu::kBGold, 'r', 330);  // (r238's grip: your copy in your hand)
  xrmenu::say(K, money(s.market, 46, 280), fx0 + 40, 674, 46, 0xFFFFFF, 'l', 300);
  xrmenu::say(K, (s.chg >= 0.0f ? "+" : "-") + std::to_string(static_cast<int>(std::lround(std::fabs(s.chg)))) + "% THIS WEEK", fx1 - 40, 674, 36,
              s.chg >= 0.5f ? kUp : (s.chg <= -0.5f ? kDown : xrmenu::kBSoft), 'r', 300);
  xrmenu::button(K, fx0 + 30, 712, fx1 - 30, 780, "LIST FOR SALE", "Y", false, false, s.free > 0, 34.0f);
  const std::string lk = lock_words(s);
  xrmenu::button(K, fx0 + 30, 794, fx1 - 30, 890, lk.empty() ? "SELL NOW  " + money(s.bid, 50, 300) : lk, "A", true, true, lk.empty());
}
// r240: the panel up on SELL - the letter grid + SELL's chips at the left (draw_kb), SELL ALL / LIST ALL SHOWN + AT n% over the results, what
// they'd do now (the copies, the coins), the results live under them
void draw_sell_kb(Pen& P, float t) {
  (void)t;
  xrmenu::Kit& K = P.K;
  const int n = static_cast<int>(g_sellRows.size());
  int& top = g_top[kTabMine];
  top = std::max(0, std::min(std::max(0, n - kSellRowsShown), top));
  const float rx0 = kRowsX0Open;
  {  // what SELL ALL SHOWN would do now (the confirm says it again before anything happens)
    const Plan p = bulk_plan(false, 100, g_sLast);
    int copies = 0;
    for (int i : g_sellRows) copies += g_sell[static_cast<size_t>(i)].h.count;
    const std::string w = n == 0 ? std::string("NONE MATCH") : coins_text(copies) + (copies == 1 ? " CARD" : " CARDS") + "  -  " +
                                                                    (p.b.copies == 0 ? std::string("NONE FREE TO SELL") : coins_text(p.b.copies) + " CAN SELL FOR " + coins_text(p.b.total));
    xrmenu::label(K, w, rx0 + 10, 298, 30, n > 0 ? xrmenu::kBSoft : xrmenu::kBDim, 'l', kW - rx0 - 20);
  }
  for (int r = 0; r < kSellRowsShown && top + r < n; ++r) {
    const SellItem& s = g_sell[static_cast<size_t>(g_sellRows[static_cast<size_t>(top + r)])];
    const float y0 = kSellRowsY0 + r * kRowH, y1 = y0 + kRowH - 7;
    xrmenu::TileSpec ts;
    ts.colour = 0x2F8CFF;
    xrmenu::tile(K, rx0, y0, kW, y1, ts);
    mini(P, s.h, rx0 + 14, y0 + 6, rx0 + 14 + (y1 - y0 - 12) * 0.72f, y1 - 6);
    const float tx = rx0 + 30 + (y1 - y0 - 12) * 0.72f, cy = 0.5f * (y0 + y1);
    xrmenu::say(K, name_of_card(s.h.card) + (s.h.count > 1 ? "  X" + std::to_string(s.h.count) : std::string()), tx, cy - 16, 36, 0xFFFFFF, 'l', kW - tx - 230);
    xrmenu::label(K, variant_words(s.h), tx, cy + 22, 30, s.h.parallel >= kGoldTizm ? kGold : xrmenu::kBSoft, 'l', kW - tx - 230);
    xrmenu::say(K, money(s.market, 40, 190), kW - 24, cy - 14, 40, xrmenu::kBGold, 'r', 200);
    const std::string lk = lock_words(s);
    xrmenu::label(K, lk.empty() ? (s.spare > 0 || g_sLast ? "SELL " + money(s.bid, 30, 150) : std::string("YOUR LAST")) : lk, kW - 24, cy + 24, 30,
                  lk.empty() ? (s.spare > 0 || g_sLast ? 0x3DDC84 : xrmenu::kBSoft) : kPink, 'r', s.h.grading ? 410.0f : 220.0f);  // (r242: at 30 px)
  }
  if (n > kSellRowsShown) xrmenu::scroll_note(K, 0.5f * (rx0 + kW), 846, top, kSellRowsShown, n, "ROW");
  draw_kb(P, true);
}
// ---- r241: CARDS > GRADING (LOOK B, as SELL): the sub-tabs SEND A CARD / YOUR SLABS (Y), the search bar (X), the chips on, 4 rows (the card +
// Xn; its finish / number; the player's season; its value, GRADE n or why it can't go), the focused copy's panel (the card big, what you hold of
// it, its cost, the grades' odds, back after 2 matches, A SEND TO GRADING). Drawn on change only (the hub asks when the page changed)
std::string grade_chips_text(int i) {  // the shut page's chip i ("" not on): FAMILY, SET, FINISH, TYPE, STATUS, then SORT (always)
  static const char* const kSetS[kFSets] = {"ALL", "LANCINI TIZM", "JAM '96", "EXQUISITE"};
  static const char* const kTypeS[kSTypes] = {"ANY", "BASE", "PARALLEL", "CASE HITS", "AUTOS", "PATCHES", "GRADED", "SHINY", "ROOKIES"};
  static const char* const kStatS[kGStats] = {"ALL", "CAN SEND", "LOCKED"};
  static const char* const kSortS[kGSorts] = {"VALUE HIGH-LOW", "VALUE LOW-HIGH", "FINISH", "NEWEST"};
  static const char* const kFinS[kSFinExq] = {"ANY", "BASE", "SILVER", "GOLD /10", "/25 & UNDER", "1/1"};
  switch (i) {
    case 0: return g_gfFam ? "FAMILY" : "";
    case 1: return g_gfSet != 0 ? kSetS[g_gfSet] : "";
    case 2: return g_gfFin == 0 ? "" : (g_gfFin < kSFinExq ? std::string(kFinS[g_gfFin]) : "EXQ " + std::string(xrexq::short_name(g_gfFin - kSFinExq + 1)));
    case 3: return g_gfType != 0 ? kTypeS[g_gfType] : "";
    case 4: return g_gfStat != 0 ? kStatS[g_gfStat] : "";
    default: return std::string("SORT: ") + kSortS[g_gfSort];
  }
}
Variant shown_v(const Holding& h) {  // (a copy at grading: its grade isn't known yet - never shown before it's back)
  Variant v = h;
  if (h.grading) v.grade = 0, v.cert = 0;
  return v;
}
void grade_subtabs(Pen& P) {  // SEND A CARD (this list) / YOUR SLABS (Y: xr_cards' slab page) + how many copies can go
  xrmenu::subtabs(P.K, {"SEND A CARD", "YOUR SLABS"}, 0, 0, 14, {"", ""}, xrmenu::kBCards);
  int can = 0;
  for (const SellItem& s : g_sell)
    if (grade_lock(s).empty()) can += s.free;
  xrmenu::label(P.K, coins_text(can) + (can == 1 ? " COPY CAN GO" : " COPIES CAN GO"), kW - 10, 44, 30, xrmenu::kBSoft, 'r', 600);
}
void draw_grade_b(Pen& P, float t) {
  (void)t;
  xrmenu::Kit& K = P.K;
  const int n = static_cast<int>(g_gradeRows.size());
  constexpr int kRowsS = 4;
  int& cur = g_gCur;
  int& top = g_gTop;
  cur = std::max(0, std::min(std::max(0, n - 1), cur));
  if (cur < top) top = cur;
  if (cur >= top + kRowsS) top = cur - kRowsS + 1;
  top = std::max(0, std::min(std::max(0, n - kRowsS), top));
  const float lx1 = 970.0f;
  int copies = 0;
  for (int i : g_gradeRows) copies += g_sell[static_cast<size_t>(i)].h.count;
  K.box(0, 104, lx1, 190, 0, xrmenu::kBNight, 0.95f);
  K.v.back().cut = 20.0f;
  K.v.back().styled = true;
  K.ring(0, 104, lx1, 190, 0, 3.0f, g_gq.empty() ? xrmenu::kBGold : 0xFFFFFF, 0.95f);
  K.v.back().cut = 20.0f;
  K.v.back().styled = true;
  const float gw = xrmenu::glyph(K, "X", 28, 147, 54);
  xrmenu::icon(K, "search", 28 + gw + 36, 147, 40, xrmenu::kBGold);
  const float sx = 28 + gw + 70;
  if (g_gq.empty()) {
    xrmenu::say(K, "SEARCH", sx, 147, 46, 0xFFFFFF, 'l', 220);
    xrmenu::label(K, "+ FILTERS", sx + std::min(220.0f, xrtext::face_text_width("SEARCH", 46, xrtext::kFaceBroadcast)) + 18, 149, 34, xrmenu::kBSoft, 'l', 260);
  } else {
    xrmenu::say(K, g_gq, sx, 147, 46, 0xFFFFFF, 'l', lx1 - sx - 300);
  }
  xrmenu::label(K, copies == 0 ? std::string("NONE") : coins_text(copies) + (copies == 1 ? " CARD" : " CARDS"), lx1 - 30, 147, 34, xrmenu::kBSoft, 'r', 260);
  {
    float cx = 0;
    for (int i = 0; i < 6; ++i) {
      const std::string c = grade_chips_text(i);
      if (c.empty()) continue;
      if (xrtext::face_text_width(c, 30, xrtext::kFaceBroadcast) + 48.0f > lx1 - cx) break;
      cx += xrmenu::chip(K, c, cx, 236, i < 5 ? 0x2B3D66 : 0x18233A, 'l', 30) + 10;
    }
  }
  if (n == 0) {
    xrmenu::say(K, g_sell.empty() ? "NO CARDS YET" : "NONE MATCH", 0.5f * lx1, 520, 56, xrmenu::kBDim, 'c', lx1 - 40);
    return;
  }
  const float ry0 = 286, rh = (898 - ry0 - (kRowsS - 1) * 12.0f) / kRowsS;
  for (int r = 0; r < kRowsS && top + r < n; ++r) {
    const SellItem& s = g_sell[static_cast<size_t>(g_gradeRows[static_cast<size_t>(top + r)])];
    const float y0 = ry0 + r * (rh + 12.0f), y1 = y0 + rh;
    xrmenu::TileSpec ts;
    ts.colour = top + r == cur ? 0xA855F7 : 0x2F8CFF;
    ts.on = top + r == cur;
    xrmenu::tile(K, 0, y0, lx1, y1, ts);
    mini(P, shown_v(s.h), 18, y0 + 10, 18 + (rh - 20) * 0.72f, y1 - 10);
    const float tx = 36 + (rh - 20) * 0.72f, room = lx1 - tx - 270;
    xrmenu::say(K, name_of_card(s.h.card) + (s.h.count > 1 ? "  X" + std::to_string(s.h.count) : std::string()), tx, y0 + 34, 38, 0xFFFFFF, 'l', room);
    xrmenu::label(K, variant_words(shown_v(s.h)), tx, y0 + 76, 30, s.h.parallel == kGoldTizm || s.h.parallel == kBlack || s.set == 2 ? kGold : xrmenu::kBSoft, 'l', room);
    xrmenu::label(K, stat_short(s.h.card), tx, y0 + 114, 30, l_rank_rgb(s.rank), 'l', room);
    const std::string at = money(s.market, 44, 210);
    xrmenu::say(K, at, lx1 - 26, y0 + 36, 44, xrmenu::kBGold, 'r', 220);
    xrmenu::bcoin(K, lx1 - 46 - std::min(220.0f, xrtext::face_text_width(at, 44, xrtext::kFaceBroadcast)) - 20, y0 + 36, 36);
    xrmenu::label(K, "MARKET", lx1 - 26, y0 + 76, 30, xrmenu::kBSoft, 'r', 250);
    const std::string lk = grade_lock(s);
    if (lk.empty()) xrmenu::label(K, "GRADE " + coins_text(grade_cost_of(s.h)), lx1 - 26, y0 + 114, 30, 0x3DDC84, 'r', 250);
    else xrmenu::label(K, s.h.grading ? grading_words(s.h) : lk, lx1 - 26, y0 + 114, 30, kPink, 'r', s.h.grading ? 410.0f : 250.0f);  // r242 (at 30 px)
  }
  if (n > kRowsS) {
    K.poly({lx1 + 20, ry0 + 6, lx1 + 32, ry0 - 18, lx1 + 44, ry0 + 6}, top > 0 ? xrmenu::kBGold : 0x3A4560);
    K.poly({lx1 + 20, 878, lx1 + 44, 878, lx1 + 32, 902}, top + kRowsS < n ? xrmenu::kBGold : 0x3A4560);
  }
  // the focused copy: big, what you hold of it, its cost, the send button (or why not)
  const SellItem& s = g_sell[static_cast<size_t>(g_gradeRows[static_cast<size_t>(cur)])];
  const float fx0 = 1010, fx1 = kW, fcx = 0.5f * (fx0 + fx1), fy0 = 104, fy1 = 898;
  xrmenu::panel(K, fx0, fy0, fx1, fy1, kGold, 0.94f);
  const float cy0 = fy0 + 24, cy1 = 424, ch = cy1 - cy0;
  K.glow(fcx - 220, cy0, fcx + 220, cy1, kGold, 0.22f);
  mini(P, shown_v(s.h), fcx - ch * 0.36f, cy0, fcx + ch * 0.36f, cy1);
  xrmenu::say(K, name_of_card(s.h.card), fcx, 466, 48, 0xFFFFFF, 'c', fx1 - fx0 - 40, true, 0.0f, 3.0f);
  xrmenu::label(K, variant_words(shown_v(s.h)), fcx, 510, 30, s.h.parallel >= kGoldTizm || s.set == 2 ? kGold : xrmenu::kBSoft, 'c', fx1 - fx0 - 40);
  {  // what you hold of it: how many, at grading / graded / listed / museum / trade
    std::vector<std::string> parts = {"YOU HAVE " + std::to_string(s.h.count)};
    if (s.h.grading) parts.push_back(grading_words(s.h));  // r242
    else if (s.h.grade > 0) parts.push_back("IN ITS SLAB");
    if (s.listed > 0) parts.push_back(std::to_string(s.listed) + " LISTED");
    if (s.museum > 0) parts.push_back(std::to_string(s.museum) + " IN MUSEUM");
    if (s.heldOut > 0) parts.push_back(std::to_string(s.heldOut) + " IN A TRADE");
    if (s.free > 0 && s.free < s.h.count && s.h.grade == 0) parts.push_back(std::to_string(s.free) + " CAN GO");
    std::string own;
    for (;;) {
      own.clear();
      for (size_t i = 0; i < parts.size(); ++i) own += (i ? "  -  " : "") + parts[i];
      if (parts.size() <= 1 || xrtext::face_text_width(own, 30, xrtext::kFaceBroadcastUp, 0.14f) <= fx1 - fx0 - 40) break;
      parts.pop_back();
    }
    xrmenu::label(K, own, fcx, 548, 30, grade_lock(s).empty() ? xrmenu::kBSoft : kPink, 'c', fx1 - fx0 - 40);
  }
  xrmenu::label(K, "MARKET", fx0 + 40, 600, 30);
  xrmenu::say(K, money(s.market, 46, 280), fx0 + 40, 644, 46, 0xFFFFFF, 'l', 300);
  xrmenu::label(K, "6% GEM MINT 10", fx1 - 40, 600, 30, xrmenu::kBSoft, 'r', 330);
  xrmenu::label(K, "BACK IN " + std::to_string(kGradeBackMatches) + " MATCHES", fx1 - 40, 644, 30, xrmenu::kBSoft, 'r', 330);
  const int cost = grade_cost_of(s.h);
  const std::string lk = grade_lock(s);
  const bool afford = balance() >= cost;
  if (lk.empty()) {
    xrmenu::label(K, afford ? "GRADED 1 TO 10 IN A SEALED SLAB" : "NEED " + coins_text(cost - balance()) + " MORE BIT COINS", fcx, 730, 30, afford ? xrmenu::kBGold : kPink, 'c',
                  fx1 - fx0 - 40);
  } else {  // (why not, and what to do - two short lines at 30 px)
    std::string l1 = lk, l2;
    if (lk == "AT GRADING") l1 = grading_words(s.h), l2 = "ITS GRADE: WHEN IT'S BACK";  // r242
    else if (lk == "GRADED") l1 = "GRADED ALREADY", l2 = "SEALED IN ITS SLAB";
    else if (lk == "LISTED") l1 = "IT'S LISTED", l2 = "TAKE IT DOWN: MARKET > LISTED";
    else if (lk == "IN MUSEUM") l1 = "IN YOUR MUSEUM", l2 = "TAKE IT OUT: CARDS > MUSEUM";
    else if (lk == "IN A TRADE") l1 = "IN A COLLECTOR TRADE", l2 = "ANSWER IT: CARDS > COLLECTORS";
    xrmenu::label(K, l1, fcx, 712, 30, kPink, 'c', fx1 - fx0 - 40);
    if (!l2.empty()) xrmenu::label(K, l2, fcx, 752, 30, xrmenu::kBSoft, 'c', fx1 - fx0 - 40);
  }
  xrmenu::button(K, fx0 + 30, 794, fx1 - 30, 890, lk.empty() ? "SEND TO GRADING  " + coins_text(cost) : (s.h.grading ? grading_words(s.h) : lk), "A", true, true, lk.empty() && afford);  // r242
}
// the panel up on GRADING - the letter grid + GRADING's chips at the left (draw_kb), the results live at the right
void draw_grade_kb(Pen& P, float t) {
  (void)t;
  xrmenu::Kit& K = P.K;
  const int n = static_cast<int>(g_gradeRows.size());
  int& top = g_gTop;
  top = std::max(0, std::min(std::max(0, n - kSellRowsShown), top));
  const float rx0 = kRowsX0Open;
  {
    int copies = 0, can = 0;
    for (int i : g_gradeRows) {
      const SellItem& s = g_sell[static_cast<size_t>(i)];
      copies += s.h.count;
      if (grade_lock(s).empty()) can += s.free;
    }
    const std::string w = n == 0 ? std::string("NONE MATCH") : coins_text(copies) + (copies == 1 ? " CARD" : " CARDS") + "  -  " +
                                                                    (can == 0 ? std::string("NONE CAN GO NOW") : coins_text(can) + " CAN GO TO GRADING");
    xrmenu::label(K, w, rx0 + 10, 298, 30, n > 0 ? xrmenu::kBSoft : xrmenu::kBDim, 'l', kW - rx0 - 20);
  }
  for (int r = 0; r < kSellRowsShown && top + r < n; ++r) {
    const SellItem& s = g_sell[static_cast<size_t>(g_gradeRows[static_cast<size_t>(top + r)])];
    const float y0 = kSellRowsY0 + r * kRowH, y1 = y0 + kRowH - 7;
    xrmenu::TileSpec ts;
    ts.colour = 0x2F8CFF;
    xrmenu::tile(K, rx0, y0, kW, y1, ts);
    mini(P, shown_v(s.h), rx0 + 14, y0 + 6, rx0 + 14 + (y1 - y0 - 12) * 0.72f, y1 - 6);
    const float tx = rx0 + 30 + (y1 - y0 - 12) * 0.72f, cy = 0.5f * (y0 + y1);
    xrmenu::say(K, name_of_card(s.h.card) + (s.h.count > 1 ? "  X" + std::to_string(s.h.count) : std::string()), tx, cy - 16, 36, 0xFFFFFF, 'l', kW - tx - 230);
    xrmenu::label(K, variant_words(shown_v(s.h)), tx, cy + 22, 30, s.h.parallel >= kGoldTizm || s.set == 2 ? kGold : xrmenu::kBSoft, 'l', kW - tx - 230);
    xrmenu::say(K, money(s.market, 40, 190), kW - 24, cy - 14, 40, xrmenu::kBGold, 'r', 200);
    const std::string lk = grade_lock(s);
    xrmenu::label(K, lk.empty() ? "GRADE " + coins_text(grade_cost_of(s.h)) : (s.h.grading ? grading_words(s.h) : lk), kW - 24, cy + 24, 30, lk.empty() ? 0x3DDC84 : kPink, 'r', s.h.grading ? 410.0f : 220.0f);  // r242 (at 30 px)
  }
  if (n > kSellRowsShown) xrmenu::scroll_note(K, 0.5f * (rx0 + kW), 846, top, kSellRowsShown, n, "ROW");
  draw_kb(P, true);
}
// ---- r238: THE SALES TAB (Lance 11-12: a card's recent sales - eBay / 130point sold listings): the players with sales (or the search's), a
// player's finishes (BASE .. GRADED, JAM '96, EXQUISITE) each with its last sale + how many sold, and the focused finish's chart, its market
// now and its last sales (price, the copy, the week, who bought from whom). LOOK B: few words, 30 px at least.
int twin_of(int card, int set) {
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].set == set && g_set[i].base == card) return static_cast<int>(i);
  return -1;
}
Variant rep_of(int card, int g) {  // a finish group's usual copy (its market: when it has no sale yet)
  Variant v;
  v.card = card;
  if (g == 1) v.parallel = kSilver;
  else if (g == 2) v.parallel = kGoldTizm, v.serial = 5;
  else if (g == 3) v.parallel = kBlack, v.serial = 1;
  else if (g == 4) v.hit = kHitSigned;
  else if (g == 5) v.grade = 9;
  else if (g == 6) v.card = twin_of(card, 1);
  else if (g == 7) {
    v.card = twin_of(card, 2);
    v.serial = xrexq::serial_of(xrexq::kGallery, 1);
    v.parallel = xrexq::kClass[xrexq::kGallery];
  }
  return v;
}
std::string week_text(const SaleRec& r) {
  return r.season == std::max(1, g_mk.season) ? "WK " + std::to_string(r.week) : "S" + std::to_string(r.season) + " WK " + std::to_string(r.week);
}
// a sale's line under its price: the copy's finish (its number, its grade, its tier)
std::string sale_words(const SaleRec& r) {
  std::string w = upper(variant_words(r.v));
  if (exq_v(r.v)) w = xrexq::words(r.v.serial, exq_rookie_v(r.v));
  return w;
}
void sales_chart(Pen& P, float x0, float y0, float x1, float y1, const std::vector<long long>& prices, long long market) {
  xrmenu::Kit& K = P.K;
  K.box(x0, y0, x1, y1, 16, 0x0A1022, 0.9f);
  for (int g = 1; g < 4; ++g) K.box(x0 + 10, y0 + g * (y1 - y0) / 4, x1 - 10, y0 + g * (y1 - y0) / 4 + 1, 0, kLine, 0.6f);
  double lo = static_cast<double>(market), hi = lo;
  for (long long p : prices) lo = std::min(lo, static_cast<double>(p)), hi = std::max(hi, static_cast<double>(p));
  if (hi - lo < hi * 0.1) lo = hi * 0.9 - 1.0, hi = hi * 1.05 + 1.0;
  const float pad = 0.14f * (y1 - y0);
  auto Y = [&](double v) { return y1 - pad - (y1 - y0 - 2 * pad) * static_cast<float>((v - lo) / std::max(1e-9, hi - lo)); };
  // the market now: a dashed gold line across
  const float my = Y(static_cast<double>(market));
  for (float x = x0 + 14; x < x1 - 14; x += 34) K.box(x, my - 2, std::min(x1 - 14, x + 20), my + 2, 1, kGold, 0.85f);
  xrmenu::label(K, "MARKET", x1 - 20, my - 24, 30, kGold, 'r', 200);
  if (prices.empty()) {
    xrmenu::label(K, "NO SALES YET", 0.5f * (x0 + x1), 0.5f * (y0 + y1) + 30, 34, xrmenu::kBDim, 'c', x1 - x0 - 40);
    return;
  }
  const size_t n = prices.size();
  auto X = [&](size_t i) { return n == 1 ? 0.5f * (x0 + x1) : x0 + 40 + (x1 - x0 - 80) * static_cast<float>(i) / static_cast<float>(n - 1); };
  for (size_t i = 0; i + 1 < n; ++i) {
    const float ax = X(i), ay = Y(static_cast<double>(prices[i])), bx = X(i + 1), by = Y(static_cast<double>(prices[i + 1]));
    const float dx = bx - ax, dy = by - ay, l = std::max(0.01f, std::sqrt(dx * dx + dy * dy)), nx = -dy / l * 2.5f, ny = dx / l * 2.5f;
    K.poly({ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny}, kCyan, 1.0f);
  }
  for (size_t i = 0; i < n; ++i) {
    const float cx = X(i), cy = Y(static_cast<double>(prices[i]));
    K.box(cx - 9, cy - 9, cx + 9, cy + 9, 9, i + 1 == n ? 0xFFFFFF : kCyan);
  }
}
void sales_row_b(Pen& P, const SalesRow& r, float x0, float y0, float x1, float y1, bool on) {
  xrmenu::Kit& K = P.K;
  xrmenu::TileSpec ts;
  ts.colour = on ? 0xA855F7 : 0x2F8CFF;
  ts.on = on;
  xrmenu::tile(K, x0, y0, x1, y1, ts);
  const float h = y1 - y0;
  Variant mv = r.n > 0 ? r.lastV : Variant();
  if (r.n <= 0) mv.card = r.card;
  mini(P, mv, x0 + 14, y0 + 6, x0 + 14 + (h - 12) * 0.72f, y1 - 6);
  const float tx = x0 + 30 + (h - 12) * 0.72f, cy = 0.5f * (y0 + y1);
  xrmenu::say(K, search_name(r.card), tx, cy - 16, 36, 0xFFFFFF, 'l', x1 - tx - 230);
  // (r239: a family's row - "12 SALES  -  3 FORMS", or the form the search found: "12 SALES  -  HIS TOGEPI TOO")
  std::string sub = r.n > 0 ? std::to_string(r.n) + (r.n == 1 ? " SALE" : " SALES") : std::string("NO SALES YET");
  if (r.form >= 0) sub += "  -  HIS " + search_name(r.form) + " TOO";
  else if (r.forms > 1) sub += "  -  " + std::to_string(r.forms) + " FORMS";
  xrmenu::label(K, sub, tx, cy + 22, 30, xrmenu::kBSoft, 'l', x1 - tx - 230);
  Variant base;
  base.card = r.card;
  const long long shown = r.n > 0 ? r.last : price_i(base, -1);
  xrmenu::say(K, money(shown, 40, 190), x1 - 24, cy - (r.n > 0 ? 12 : 0), 40, r.n > 0 ? xrmenu::kBGold : xrmenu::kBSoft, 'r', 200);
  if (r.n > 0) xrmenu::label(K, "LAST", x1 - 24, cy + 26, 30, xrmenu::kBDim, 'r', 200);
}
void draw_sales(Pen& P, float t) {
  (void)t;
  xrmenu::Kit& K = P.K;
  if (g_salesDirty) sales_rows_build();
  const int n = static_cast<int>(g_salesRows.size());
  int& cur = g_cur[kTabSales];
  int& top = g_top[kTabSales];
  cur = std::max(0, std::min(std::max(0, n - 1), cur));
  if (g_kb) {  // the search panel up: its live list of players at the right
    if (cur < top) top = cur;
    top = std::max(0, std::min(std::max(0, n - kRowsShown), top));
    xrmenu::label(K, n == 0 ? std::string("NOBODY FOUND") : std::to_string(n) + (n == 1 ? " PLAYER" : " PLAYERS"), kRowsX0Open + 10, 236, 30, xrmenu::kBSoft, 'l', kW - kRowsX0Open - 20);
    for (int r = 0; r < kRowsShown && top + r < n; ++r)
      sales_row_b(P, g_salesRows[static_cast<size_t>(top + r)], kRowsX0Open, kRowsY0 + r * kRowH, kW, kRowsY0 + (r + 1) * kRowH - 7, false);
    draw_kb(P, xrmenu::broadcast());
    return;
  }
  const float lx1 = 970.0f;
  if (g_salesCard < 0) {
    // ---- the players with sales (the newest first): the search bar, 5 rows, the focused one's finishes at the right
    constexpr int kRowsB = 5;
    if (cur < top) top = cur;
    if (cur >= top + kRowsB) top = cur - kRowsB + 1;
    top = std::max(0, std::min(std::max(0, n - kRowsB), top));
    K.box(0, 104, lx1, 190, 0, xrmenu::kBNight, 0.95f);
    K.v.back().cut = 20.0f;
    K.v.back().styled = true;
    K.ring(0, 104, lx1, 190, 0, 3.0f, g_q.empty() ? xrmenu::kBGold : 0xFFFFFF, 0.95f);
    K.v.back().cut = 20.0f;
    K.v.back().styled = true;
    const float gw = xrmenu::glyph(K, "X", 28, 147, 54);
    xrmenu::icon(K, "search", 28 + gw + 36, 147, 40, xrmenu::kBGold);
    const float sx = 28 + gw + 70;
    xrmenu::say(K, g_q.empty() ? std::string("SEARCH") : g_q, sx, 147, 46, 0xFFFFFF, 'l', lx1 - sx - 300);
    xrmenu::label(K, std::to_string(g_histN) + " SALES", lx1 - 30, 147, 34, xrmenu::kBSoft, 'r', 260);
    xrmenu::label(K, g_q.empty() ? "LATEST SALES" : "\"" + g_q + "\"", 0, 238, 34, xrmenu::kBGold, 'l', lx1);
    if (n == 0) {
      xrmenu::say(K, g_q.empty() ? "NO SALES YET" : "NOBODY FOUND", 0.5f * lx1, 520, 56, xrmenu::kBDim, 'c', lx1 - 40);
      return;
    }
    const float ry0 = 286, rh = (898 - ry0 - (kRowsB - 1) * 12.0f) / kRowsB;
    for (int r = 0; r < kRowsB && top + r < n; ++r)
      sales_row_b(P, g_salesRows[static_cast<size_t>(top + r)], 0, ry0 + r * (rh + 12.0f), lx1, ry0 + r * (rh + 12.0f) + rh, top + r == cur);
    if (n > kRowsB) {
      K.poly({lx1 + 20, ry0 + 6, lx1 + 32, ry0 - 18, lx1 + 44, ry0 + 6}, top > 0 ? xrmenu::kBGold : 0x3A4560);
      K.poly({lx1 + 20, 878, lx1 + 44, 878, lx1 + 32, 902}, top + kRowsB < n ? xrmenu::kBGold : 0x3A4560);
    }
    // the focused player: his card, his race place, each finish's last sale
    const SalesRow& f = g_salesRows[static_cast<size_t>(cur)];
    const float fx0 = 1010, fx1 = kW, fcx = 0.5f * (fx0 + fx1);
    xrmenu::panel(K, fx0, 104, fx1, 898, kGold, 0.94f);
    Variant fv;
    fv.card = f.card;
    mini(P, fv, fcx - 95, 130, fcx + 95, 390);
    xrmenu::say(K, search_name(f.card), fcx, 430, 48, 0xFFFFFF, 'c', fx1 - fx0 - 40, true, 0.0f, 3.0f);
    if (const Line* l = line_of(f.card)) xrmenu::label(K, l->rank > 0 ? "#" + std::to_string(l->rank) + " IN DEMAND" : std::string("NOT RANKED"), fcx, 476, 30, xrmenu::kBSoft, 'c', fx1 - fx0 - 40);
    const std::vector<SaleRec> fam = family_sales(f.card, -1);  // (r239: every form's)
    int shown = 0;
    for (int g : sales_groups(f.card)) {
      if (shown >= 5) break;
      long long last = -1;
      for (const SaleRec& s : fam)
        if (group_of(s.v) == g) {
          last = s.price;
          break;
        }
      if (last < 0) continue;
      const float y = 528 + shown * 46.0f;
      xrmenu::label(K, kGroupName[g], fx0 + 40, y, 32, xrmenu::kBSoft, 'l', 300);
      xrmenu::say(K, money(last, 34, 260), fx1 - 40, y, 34, xrmenu::kBGold, 'r', 280);
      ++shown;
    }
    if (shown == 0) xrmenu::label(K, "NO SALES YET - MARKET " + money(price_i(fv, -1), 32, 200), fcx, 560, 32, xrmenu::kBDim, 'c', fx1 - fx0 - 40);
    xrmenu::button(K, fx0 + 30, 898 - 120, fx1 - 30, 898 - 24, "SEE SALES", "A", true, true);
    return;
  }
  // ---- a player's SALES: his finishes at the left, the focused one's chart / market / last sales at the right
  // r239: a FAMILY - his FORM tabs (ALL FORMS + each form, first form first: X cycles) over the finishes; the sales are every form's (ALL)
  // or the tab's form's
  const int card = g_salesCard;
  const std::vector<int> forms = family_forms(card);
  if (g_salesForm >= 0 && !has_int(forms, g_salesForm)) g_salesForm = -1;
  const int shownCard = g_salesForm >= 0 ? g_salesForm : card;
  const std::vector<int> gs = sales_groups(card, g_salesForm);
  if (!has_int(gs, g_salesGroup)) g_salesGroup = gs.empty() ? 0 : gs.front();  // (r239: has_int)
  const std::vector<SaleRec> all = family_sales(card, g_salesForm);
  const bool multi = forms.size() > 1;
  {  // the player
    Variant v;
    v.card = shownCard;
    K.box(0, 104, kW, 190, 0, xrmenu::kBNight, 0.95f);
    K.v.back().cut = 20.0f;
    K.v.back().styled = true;
    mini(P, v, 16, 110, 16 + 58, 184);
    xrmenu::say(K, search_name(shownCard) + "  SALES", 96, 147, 50, 0xFFFFFF, 'l', 900);
    std::string side;
    if (const Line* l = line_of(card)) {
      side = l->rank > 0 ? "#" + std::to_string(l->rank) + " IN DEMAND" : std::string();
      if (l->seasonGoals > 0 || l->seasonAst > 0) side += (side.empty() ? "" : "  -  ") + std::to_string(l->seasonGoals) + " G " + std::to_string(l->seasonAst) + " A";
    }
    if (!side.empty()) xrmenu::label(K, side, kW - 30, 147, 32, xrmenu::kBSoft, 'r', 600);
  }
  if (multi) {  // r239: the FORM tabs - "ALL FORMS" + each form (its sales count), the one in sight lit; "X FORM" at the left
    const int nt = static_cast<int>(forms.size()) + 1;
    const float tx0 = 74, tw = std::min(330.0f, (kW - tx0 - 8.0f * (nt - 1)) / nt);
    const float gwx = xrmenu::glyph(K, "X", 30, 231, 40);
    (void)gwx;
    for (int i = 0; i < nt; ++i) {
      const int f = i == 0 ? -1 : forms[static_cast<size_t>(i - 1)];
      const bool on = f == g_salesForm;
      const float x0 = tx0 + i * (tw + 8.0f), x1 = x0 + tw;
      int cnt = 0;
      if (f < 0) {
        cnt = static_cast<int>(family_sales(card, -1).size());
      } else {
        auto it = g_hist.find(f);
        if (it != g_hist.end()) cnt = static_cast<int>(it->second.size());
      }
      xrmenu::TileSpec ts;
      ts.colour = on ? 0xFFD23F : 0x2F8CFF;
      ts.on = on;
      xrmenu::tile(K, x0, 204, x1, 258, ts, 16.0f);
      const std::string nm = f < 0 ? std::string("ALL FORMS") : search_name(f);
      xrmenu::say(K, nm, 0.5f * (x0 + x1), 222, 28, on ? 0xFFFFFF : xrmenu::kBSoft, 'c', tw - 16);
      xrmenu::label(K, std::to_string(cnt) + " SOLD", 0.5f * (x0 + x1), 246, 20, on ? 0xFFFFFF : xrmenu::kBDim, 'c', tw - 16);
    }
  }
  const int ng = static_cast<int>(gs.size());
  const float gy0 = multi ? 270.0f : 204.0f, gy1 = 898, gh = std::min(92.0f, (gy1 - gy0 - (ng - 1) * 8.0f) / std::max(1, ng)), gx1 = 590;
  for (int i = 0; i < ng; ++i) {
    const int g = gs[static_cast<size_t>(i)];
    const float y0 = gy0 + i * (gh + 8.0f), y1 = y0 + gh, cy = 0.5f * (y0 + y1);
    const bool on = g == g_salesGroup;
    xrmenu::TileSpec ts;
    ts.colour = on ? 0xA855F7 : 0x2F8CFF;
    ts.on = on;
    xrmenu::tile(K, 0, y0, gx1, y1, ts, 22.0f);
    int sold = 0;
    long long last = -1;
    for (const SaleRec& s : all)
      if (group_of(s.v) == g) {
        if (last < 0) last = s.price;
        ++sold;
      }
    xrmenu::say(K, kGroupName[g], 24, cy - 14, 34, 0xFFFFFF, 'l', 300);
    xrmenu::label(K, sold > 0 ? std::to_string(sold) + " SOLD" : std::string("NO SALES"), 24, cy + 22, 30, sold > 0 ? xrmenu::kBSoft : xrmenu::kBDim, 'l', 300);
    if (last >= 0) xrmenu::say(K, money(last, 36, 230), gx1 - 22, cy, 36, xrmenu::kBGold, 'r', 240);
  }
  // the focused finish
  const int g = g_salesGroup;
  std::vector<const SaleRec*> mine;
  for (const SaleRec& s : all)
    if (group_of(s.v) == g) mine.push_back(&s);
  const float fx0 = 610, fx1 = kW;
  xrmenu::panel(K, fx0, gy0, fx1, gy1, 0xA855F7, 0.94f);
  xrmenu::say(K, kGroupName[g], fx0 + 34, gy0 + 40, 46, 0xFFFFFF, 'l', 520);
  const Variant rv = !mine.empty() ? mine.front()->v : rep_of(shownCard, g);
  const float sq = multi ? 40.0f : 0.0f, rowStep = multi ? 94.0f : 100.0f, rowH = multi ? 86.0f : 92.0f;  // (r239: room for the FORM tabs)
  const long long mkt = rv.card >= 0 ? price_i(rv, -1) : 0;
  xrmenu::label(K, "MARKET NOW", fx1 - 34, gy0 + 22, 30, xrmenu::kBSoft, 'r', 300);
  xrmenu::say(K, rv.card >= 0 ? money(mkt, 46, 360) : std::string("-"), fx1 - 34, gy0 + 64, 46, 0xFFFFFF, 'r', 380);
  if (!mine.empty()) {
    const SaleRec& l = *mine.front();
    const int pct = static_cast<int>(std::lround(100.0 * static_cast<double>(l.price) / static_cast<double>(std::max(1LL, mkt))));
    xrmenu::label(K, "LAST " + money(l.price, 30, 260) + "  (" + std::to_string(pct) + "%)", fx0 + 34, gy0 + 86, 30, pct < 97 ? kUp : (pct <= 106 ? kGold : kDown), 'l', 560);
  }
  std::vector<long long> prices;
  for (auto it = mine.rbegin(); it != mine.rend(); ++it) prices.push_back((*it)->price);
  sales_chart(P, fx0 + 24, gy0 + 112, fx1 - 24, gy0 + 330 - sq, prices, mkt);
  if (!mine.empty()) {
    const SaleRec& o = *mine.back();
    xrmenu::label(K, std::to_string(mine.size()) + " SOLD  -  " + week_text(o) + " TO " + week_text(*mine.front()), fx0 + 34, gy0 + 362 - sq, 30, xrmenu::kBSoft, 'l', fx1 - fx0 - 60);
  }
  for (size_t i = 0; i < mine.size() && i < 3; ++i) {  // the last sales: the price, the copy, the week; who bought it from whom
    const SaleRec& s = *mine[i];
    const float y0 = gy0 + 392 - sq - (multi ? 4.0f : 0.0f) + i * rowStep;
    K.box(fx0 + 20, y0, fx1 - 20, y0 + rowH, 14, i == 0 ? 0x1C2850 : 0x121A33, 0.9f);
    xrmenu::say(K, money(s.price, 38, 230), fx0 + 36, y0 + 28, 38, xrmenu::kBGold, 'l', 240);
    // (r239: ALL FORMS - which form's copy it was: "TOGEPI  SILVER TIZM")
    const std::string what = multi && g_salesForm < 0 ? search_name(family_card(s.v.card)) + "  " + sale_words(s) : sale_words(s);
    xrmenu::label(K, what, fx0 + 290, y0 + 28, 30, s.v.parallel == kGoldTizm ? kGold : xrmenu::kBSoft, 'l', 520);
    xrmenu::label(K, week_text(s), fx1 - 36, y0 + 28, 30, xrmenu::kBSoft, 'r', 170);
    xrmenu::label(K, s.buyer + "  FROM  " + s.seller, fx0 + 36, y0 + rowH - 24, 30, s.kind == 1 || s.kind == 2 ? kCyan : xrmenu::kBDim, 'l', fx1 - fx0 - 72);
  }
  if (mine.empty()) xrmenu::label(K, "NONE SOLD YET - THE MARKET LINE IS ITS PRICE NOW", 0.5f * (fx0 + fx1), gy0 + 470 - sq, 30, xrmenu::kBDim, 'c', fx1 - fx0 - 60);
}
void draw_offers(Pen& P, float t) {
  // (r236: "COLLECTORS WANT YOUR CARDS" + "OFFERS END AFTER 2 MATCHDAYS - NEVER MORE THAN 108% ..." always on: gone - each offer says its days)
  const int n = static_cast<int>(g_mk.offers.size());
  if (n == 0) {
    P.K.text("NO OFFERS RIGHT NOW", 825, 470, 64, 1400, kDim, 2.0f);
    P.K.text("HOT CARDS, PARALLELS, CASE HITS AND HIGH GRADES DRAW THEM - EVERY MATCHDAY", 825, 550, 30, 1400, kDim, 0.0f);
    return;
  }
  int& cur = g_cur[kTabOffers];
  cur = std::max(0, std::min(n - 1, cur));
  for (int i = 0; i < n && i < kMaxOffers; ++i) {
    const Offer& o = g_mk.offers[static_cast<size_t>(i)];
    const float y0 = 234 + i * 132.0f, y1 = y0 + 122;
    const bool on = i == cur;
    P.K.box(0, y0, kW, y1, 22, on ? kPanelHi : kPanel);
    if (on) xrmenu::focus(P.K, 0, y0, kW, y1, 22);
    mini(P, o.v, 16, y0 + 10, 92, y1 - 10);
    const long long p = price_i(o.v, -1);
    P.K.left(o.who + (o.final ? "  FINAL OFFER" : "") + "  OFFERS", 112, y0 + 36, 32, 700, o.final ? 0xFFB020 : kCyan, 0.0f);
    P.K.left("YOUR " + upper(variant_words(o.v)) + " " + name_of_card(o.v.card), 112, y0 + 76, 30, 760, kInk, 0.0f);
    if (o.bidders > 1) {  // a bidding war: their paddles
      for (int b = 0; b < o.bidders; ++b) {
        const float bx = 900 + b * 46.0f;
        P.K.box(bx, y0 + 26, bx + 38, y0 + 64, 19, b == 0 ? kPink : 0x5A4A90, 1.0f);
        P.K.text(std::to_string(b + 1), bx + 19, y0 + 46, 24, 30, 0xFFFFFF, 0.0f);
      }
      P.K.left(std::to_string(o.bidders) + " BIDDING", 900, y0 + 92, 24, 200, kPink, 0.0f);
    }
    coin(P, 1130, y0 + 48, 18);
    const float pulse = on ? 0.5f + 0.5f * std::sin(t * 4.0f) : 0.0f;
    if (on) P.K.glow(1150, y0 + 10, 1400, y0 + 90, kGold, 0.15f + 0.15f * pulse);
    P.K.left(money(o.amount, 56, 270), 1160, y0 + 50, 56, 280, kGold, 2.0f);
    P.K.left("MARKET " + money(p, 24, 170) + "  (" + std::to_string(static_cast<int>(std::lround(100.0 * static_cast<double>(o.amount) / static_cast<double>(std::max(1LL, p))))) + "%)", 1130, y0 + 96, 24, 300, kDim, 0.0f);
    P.K.left(o.expires - g_mk.steps <= 0 ? "ENDS AT THE NEXT MATCHDAY" : "2 MATCHDAYS LEFT", 112, y0 + 108, 20, 400, kDim, 0.0f);  // (r210: under its words - the market's wider numbers sit on the right)
    if (on && g_countering) {  // the counter: +5 / +10 / +15%
      P.K.box(820, y0 - 8, kW - 8, y1 + 8, 26, 0x1A1036, 0.96f);
      P.K.ring(820, y0 - 8, kW - 8, y1 + 8, 26, 4, kPink);
      P.K.left("COUNTER", 846, y0 + 40, 34, 200, kPink, 2.0f);
      for (int k = 0; k < 3; ++k) {
        const float cx0 = 1010 + k * 205.0f, cx1 = cx0 + 190;
        const bool sel = k == g_counterPick;
        P.K.box(cx0, y0 + 14, cx1, y1 - 14, 30, sel ? 0x4A1440 : 0x150E2C);
        P.K.ring(cx0, y0 + 14, cx1, y1 - 14, 30, sel ? 4.0f : 2.0f, sel ? kPink : kLine);
        P.K.text("+" + std::to_string(kCounterPct[k]) + "%", 0.5f * (cx0 + cx1), y0 + 46, 34, 170, 0xFFFFFF, 0.0f);
        P.K.text(money(static_cast<long long>(llr(static_cast<double>(o.amount) * (1.0 + kCounterPct[k] / 100.0))), 28, 170), 0.5f * (cx0 + cx1), y0 + 84, 28, 170, kGold, 0.0f);
      }
      // (r236: its "LEFT / RIGHT - A SEND - B DROP IT" on the page: gone - the bar says them)
    }
  }
}
// r210: MY LISTINGS - your copies up for sale: your price against the market (under it: green, about it: gold, over it: red), how
// long it's been up, the eyes on it; then your recent sales
void draw_listed(Pen& P, float t) {
  P.K.right(std::to_string(g_mk.mine.size()) + " OF " + std::to_string(kMaxMine) + " LISTED", kW - 24, 214, 30, 900, kDim, 0.0f);  // (r236: one count - the rules sentence gone)
  const int n = static_cast<int>(g_mk.mine.size());
  int& cur = g_cur[kTabListed];
  int& top = g_top[kTabListed];
  constexpr int kRows = 4;
  if (n == 0) {
    P.K.text("NOTHING LISTED", 825, 330, 60, 1400, kDim, 2.0f);
    P.K.text((xrmenu::broadcast() ? std::string("SELL > X: LIST ONE FOR SALE") : std::string("MY CARDS > X: LIST FOR SALE - YOUR PRICE ON THE PAD; UNDER THE MARKET SELLS FAST, OVER IT NOW AND THEN")), 825, 400, 26, 1500, kDim, 0.0f);
  }
  cur = std::max(0, std::min(std::max(0, n - 1), cur));
  if (cur < top) top = cur;
  if (cur >= top + kRows) top = cur - kRows + 1;
  top = std::max(0, std::min(std::max(0, n - kRows), top));
  for (int r = 0; r < kRows && top + r < n; ++r) {
    const Mine& m = g_mk.mine[static_cast<size_t>(top + r)];
    const float y0 = 234 + r * 118.0f, y1 = y0 + 108;
    const bool on = top + r == cur;
    P.K.box(0, y0, kW, y1, 20, on ? kPanelHi : kPanel);
    if (on) xrmenu::focus(P.K, 0, y0, kW, y1, 20);
    mini(P, m.v, 14, y0 + 8, 74, y1 - 8);
    P.K.left(name_of_card(m.v.card), 92, y0 + 34, 36, 420, kInk, 2.0f);
    P.K.left(variant_words(m.v), 92, y0 + 74, 24, 430, m.v.parallel == kGoldTizm ? kGold : 0xB8C2E0, 0.0f);
    const long long mk = price_i(m.v, -1);
    const double ratio = static_cast<double>(m.ask) / static_cast<double>(std::max(1LL, mk));
    const uint32_t rc = ratio < 0.97 ? kUp : (ratio <= 1.06 ? kGold : kDown);
    P.K.left("ASKING", 540, y0 + 26, 22, 200, kDim, 0.0f);
    P.K.left(money(m.ask, 46, 300), 540, y0 + 66, 46, 310, kInk, 2.0f);
    P.K.left("MARKET", 870, y0 + 26, 22, 200, kDim, 0.0f);
    P.K.left(money(mk, 34, 250), 870, y0 + 62, 34, 260, kInk, 0.0f);
    const std::string rt = std::to_string(static_cast<int>(std::lround(std::min(99999.0, ratio * 100)))) + "% OF IT";
    P.K.box(870, y0 + 82, 870 + xrtext::board_text_width(rt, 20) + 24, y0 + 104, 11, xrmenu::mix(rc, 0x000000, 0.7f));
    P.K.left(rt, 882, y0 + 94, 20, 200, rc, 0.0f);
    const int days = std::max(0, g_mk.steps - m.listed);
    P.K.left(days == 0 ? "LISTED THIS MATCHDAY" : "LISTED " + std::to_string(days) + (days == 1 ? " MATCHDAY" : " MATCHDAYS"), 1150, y0 + 30, 22, 300, kDim, 0.0f);
    P.K.left(std::to_string(m.views) + " VIEWS  -  " + std::to_string(m.watching) + " WATCHING", 1150, y0 + 62, 24, 300, kInk, 0.0f);
    const double look = sell_p(m.v, ratio, m.watching);  // r238: its chance at the next look (a tick / a matchday) - what the bars say is what happens
    const char* buzz = look > 0.45 ? "HOT INTEREST" : (look > 0.2 ? "SOME INTEREST" : (look > 0.06 ? "A FEW LOOKERS" : "QUIET"));
    P.K.left(buzz, 1150, y0 + 92, 22, 300, look > 0.2 ? kGold : kDim, 0.0f);
    for (int b = 0; b < 5; ++b)  // (its interest as five bars)
      P.K.box(1480 + b * 28.0f, y0 + 80 - b * 9.0f, 1500 + b * 28.0f, y0 + 92, 4, b < static_cast<int>(std::ceil(look * 5.5)) ? kGold : 0x2A3150);
  }
  if (n > kRows) xrmenu::scroll_note(P.K, 825, 708, top, kRows, n, "LISTING");
  // your recent sales
  P.K.box(0, 718, kW, 890, 22, 0x0E152C);
  P.K.ring(0, 718, kW, 890, 22, 2, kLine);
  P.K.left("SOLD", 24, 748, 28, 200, kUp, 2.0f);
  P.K.left("YOUR LISTINGS COLLECTORS PAID FOR", 120, 748, 22, 600, kDim, 0.0f);
  if (g_mk.sales.empty()) P.K.left("NONE YET - WHO KNOWS WHO MIGHT BUY AT THE RIGHT MOMENT", 24, 800, 26, kW - 60, kDim, 0.0f);
  for (size_t i = 0; i < g_mk.sales.size() && i < 3; ++i) {
    const Sale& s = g_mk.sales[i];
    const float y = 790 + i * 34.0f;
    P.K.box(24, y - 9, 36, y + 3, 6, s.surprise ? kGold : kUp);
    P.K.left(copy_words(s.v) + "  TO " + s.buyer + "  -  " + coins_text(s.amount) + " COINS" + (s.surprise ? "  -  OVER THE MARKET!" : "") +
                 "  (MATCHDAY " + std::to_string(s.day) + ")",
             50, y - 3, 24, kW - 90, i == 0 ? kInk : 0xC4CCE4, 0.0f);
  }
  (void)t;
}
// r210: the number pad (r207's CUSTOM stake pad's keys): LIST FOR SALE / EDIT PRICE, the card, its market, your price big
void draw_pad(Pen& P, float t) {
  P.K.box(0, 190, kW, 898, 26, 0x02040A, 0.72f);  // (the page dimmed under it)
  P.K.glow(kPadX0 - 60, kPadY0 - 40, kPadX1 + 60, kPadY1 + 40, kPink, 0.16f);
  P.K.box(kPadX0, kPadY0, kPadX1, kPadY1, 30, 0x150E2C, 1.0f, 0x0B0818);
  P.K.ring(kPadX0, kPadY0, kPadX1, kPadY1, 30, 4, kPink);
  const Mine* m = g_padEdit != 0 ? mine_by(g_padEdit) : nullptr;
  const Variant& v = m != nullptr ? m->v : g_padV;
  chrome(P, g_padEdit != 0 ? "EDIT PRICE" : "LIST FOR SALE", kPadX0 + 30, kPadY0 + 46, 44, 520, -1);
  mini(P, v, kPadX0 + 30, kPadY0 + 84, kPadX0 + 140, kPadY0 + 240);
  P.K.left(name_of_card(v.card), kPadX0 + 160, kPadY0 + 106, 34, 600, kInk, 2.0f);
  P.K.left(variant_words(v), kPadX0 + 160, kPadY0 + 142, 22, 600, kGold, 0.0f);
  const long long mk = price_i(v, -1);
  P.K.left("MARKET VALUE  " + coins_text(mk) + " COINS", kPadX0 + 160, kPadY0 + 182, 26, 600, kInk, 0.0f);
  P.K.left(std::string("SELL NOW PAYS ") + coins_text(bid_of(mk)) + "  -  " + (line_of(v.card) != nullptr && line_of(v.card)->rank > 0 ? "#" + std::to_string(line_of(v.card)->rank) + " IN DEMAND" : std::string("NOT IN YOUR LEAGUE")), kPadX0 + 160, kPadY0 + 216, 22, 600, kDim,
           0.0f);
  // your price, big, with a caret; its share of the market under it
  const long long val = pad_value();
  const std::string shown = g_padDigits.empty() ? "0" : xrcoin::text(val);
  P.K.box(kPadX0 + 180, 456, kPadX1 - 30, 536 - 0, 18, 0x07050F);
  P.K.left("YOUR PRICE", kPadX0 + 30, 496, 26, 150, kDim, 0.0f);
  P.K.ring(kPadX0 + 180, 456, kPadX1 - 30, 536, 18, 3, g_padFresh ? kLine : kCyan);
  P.K.right(shown, kPadX1 - 64, 498, 54, kPadX1 - kPadX0 - 270, g_padFresh ? kDim : 0xFFFFFF, 2.0f);
  if (!g_padFresh && std::fmod(t, 1.0f) < 0.6f) P.K.box(kPadX1 - 56, 472, kPadX1 - 50, 522, 2, kCyan);
  (void)shown;
  if (val > 0) {
    const double r = static_cast<double>(val) / static_cast<double>(std::max(1LL, mk));
    const uint32_t rc = r < 0.97 ? kUp : (r <= 1.06 ? kGold : kDown);
    // r238: what the live market does with it (sell_p: a look every matchday + every 90 s in the hub)
    const char* hint = r <= 1.0 ? "SELLS FAST" : (r <= 1.06 ? "LIKELY TO SELL SOON" : (r <= 1.3 ? "OVER THE MARKET - IT CAN TAKE A WHILE" : "WAY OVER - A LONG SHOT"));
    P.K.left(std::to_string(static_cast<int>(std::lround(std::min(99999.0, r * 100)))) + "% OF THE MARKET  -  " + hint, kPadX0 + 30, 566, 22,
             kPadX1 - kPadX0 - 60, rc, 0.0f);
  }
  if (g_padFresh) P.K.right("SUGGESTED - TYPE TO CHANGE IT", kPadX1 - 30, 440, 20, 400, kDim, 0.0f);
  // the keys
  for (int k = 0; k < 13; ++k) {
    float x0, y0, x1, y1;
    pad_key_box(k, &x0, &y0, &x1, &y1);
    const bool on = k == g_padKey || (k == 12 && g_padKey >= 12);
    const bool hit = k == g_padHit && g_now - g_padHitAt < 0.25;
    const uint32_t face = k == 12 ? (hit ? 0x3AE07A : 0x1E7A44) : (k == 9 || k == 11 ? (hit ? 0x7A4A9A : 0x2E1E4A) : (hit ? 0x4A5A9A : 0x1C2448));
    P.K.box(x0, y0, x1, y1, 16, face, 1.0f, xrmenu::mix(face, 0x000000, 0.35f));
    P.K.ring(x0, y0, x1, y1, 16, on ? 5.0f : 2.0f, on ? (k == 12 ? kUp : kCyan) : kLine);
    if (on) P.K.glow(x0 - 14, y0 - 14, x1 + 14, y1 + 14, k == 12 ? kUp : kCyan, 0.25f);
    P.K.text(kPadKey[k], 0.5f * (x0 + x1), 0.5f * (y0 + y1) + 1, k == 12 ? 34 : (k == 9 || k == 11 ? 28 : 36), x1 - x0 - 16, 0xFFFFFF, 2.0f);
  }
  P.K.left("A  PRESS", kPadX0 + 30, 636, 22, 170, kDim, 0.0f);
  P.K.left("X  DEL", kPadX0 + 30, 668, 22, 170, kDim, 0.0f);
  P.K.left(g_padEdit != 0 ? "B  KEEP OLD" : "B  CANCEL", kPadX0 + 30, 700, 22, 170, kDim, 0.0f);
  P.K.left("OR POINT + TRIGGER", kPadX0 + 30, 740, 20, 170, kDim, 0.0f);
  P.K.left("NO FEES", kPadX0 + 30, 790, 22, 170, kUp, 0.0f);
  if (g_now - g_padSayAt < 4.0) P.K.text(g_padSay, 0.5f * (kPadX0 + kPadX1), kPadY1 - 2, 22, kPadX1 - kPadX0 - 40, kDown, 0.0f);
}
}  // namespace

void draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  Pen P;
  P.keep = &keep;
  const bool bcast = xrmenu::broadcast();  // r236: inside the hub (LOOK B) - the hub's own header / coins / bar say the rest
  if (bcast) {
    if (g_mk.career == 0) {
      xrmenu::say(P.K, "LOAD A FRANCHISE CAREER", 825, 480, 64, xrmenu::kBDim, 'c', 1400);
    } else {
      static const int kOrder[5] = {kTabBuy, kTabMine, kTabListed, kTabOffers, kTabSales};  // (r238: + SALES)
      static const char* const kName[5] = {"BUY", "SELL", "LISTED", "OFFERS", "SALES"};
      std::vector<std::string> names, badges;
      int at = 0;
      for (int i = 0; i < 5; ++i) {
        names.push_back(kName[i]);
        const int b = kOrder[i] == kTabOffers ? static_cast<int>(g_mk.offers.size()) : (kOrder[i] == kTabListed ? g_mk.unseenSales : (kOrder[i] == kTabBuy ? g_unseenAlerts : 0));
        badges.push_back(b > 0 ? std::to_string(std::min(99, b)) : std::string());
        if (kOrder[i] == g_tab) at = i;
      }
      xrmenu::subtabs(P.K, names, at, 0, 14, badges, xrmenu::kBCards);
      if (g_tab == kTabMine) {  // (MY PORTFOLIO lives on SELL now: its worth, its move, its little chart)
        const long long pv = portfolio_value(-1);
        std::vector<double> ph;
        for (long long v : g_mk.portfolio) ph.push_back(static_cast<double>(v));
        if (ph.empty() || ph.back() != static_cast<double>(pv)) ph.push_back(static_cast<double>(pv));
        const float pch = ph.size() >= 2 ? static_cast<float>(100.0 * (ph.back() - ph[ph.size() - 2]) / std::max(1.0, ph[ph.size() - 2])) : 0.0f;
        xrmenu::label(P.K, "PORTFOLIO", 1010, 44, 30);
        xrmenu::say(P.K, money(pv, 44, 240), 1250, 44, 44, 0xFFFFFF, 'l', 240);
        spark(P, 1500, 20, kW - 10, 66, ph, pch >= 0 ? kUp : kDown, 3.0f, true);
      }
      const size_t from = P.v.size();
      if (g_tab == kTabMine) sell_fresh();  // r240: (SELL's rows: made only if something changed)
      if (g_tab == kTabMine && g_kb) draw_sell_kb(P, t);  // r240: SELL as BUY - its search panel, filters, SELL ALL / LIST ALL SHOWN
      else if (g_tab == kTabMine) draw_sell_b(P, t);      // r240: (r236-r239: draw_mine, shifted)
      else if (g_tab == kTabListed) draw_listed(P, t);
      else if (g_tab == kTabBuy && !g_kb) draw_buy_b(P, t);
      else if (g_tab == kTabBuy) draw_buy(P, t);
      else if (g_tab == kTabSales) draw_sales(P, t);  // r238
      else draw_offers(P, t);
      if (g_tab != kTabBuy && g_tab != kTabSales && g_tab != kTabMine) {  // (r210-r235's tabs start at 214 under the old header: up under the sub-tabs - no pointer on them)
        for (size_t i = from; i < P.v.size(); ++i) {
          xrtext::BoardPiece& p = P.v[i];
          p.y0 -= 110.0f;
          const bool text = p.kind == xrtext::BoardPiece::kText || p.kind == xrtext::BoardPiece::kTextLeft || p.kind == xrtext::BoardPiece::kTextRight;
          if (!text) p.y1 -= 110.0f;
          for (size_t k = 1; k < p.pts.size(); k += 2) p.pts[k] -= 110.0f;
        }
      }
      if (g_pad) draw_pad(P, t);  // r210: the number pad over the page (its keys: the pointer's - not moved)
      if (g_now - g_msgAt < 6.0) xrmenu::say(P.K, g_msg, 0.5f * kW, 930, 34, g_msgRgb, 'c', kW - 60);  // (its last word only: no ticker)
    }
  } else {
  P.K.box(0, 0, kW, kH, 30, kBg0, 1.0f, kBg1);
  {  // a ticker-tape glow + the prism's shards behind the header (the card brand's)
    static const uint32_t kBand[6] = {0xFF2E88, 0xFFB020, 0xFFE94A, 0x39FF88, 0x2EF2FF, 0x9B5CFF};
    for (int i = 0; i < 9; ++i) {
      const float a = i * 190.0f - 60.0f, b = a + 90.0f;
      P.K.poly({a, 120, b, 120, b + 70, 0, a + 70, 0}, kBand[i % 6], 0.08f);
    }
    P.K.glow(-240, -200, 520, 380, kCyan, 0.18f);
    P.K.glow(kW - 520, -200, kW + 240, 300, kGold, 0.14f);
  }
  chrome(P, "STRIKERZ", 22, 60, 88, 290, -1);  // r217 (Lance: the card feature pages are STRIKERZ - the cards stay LANCINI TIZM)
  {
    xrtext::BoardPiece p;
    p.kind = xrtext::BoardPiece::kTextLeft;
    p.x0 = 330;
    p.y0 = 64;
    p.x1 = 460;
    p.y1 = 60;
    p.rgb = 0xFFF2B0;
    p.rgb2 = kGold;
    p.outline = 4.0f;
    p.outlineRgb = 0x2A1A00;
    p.text = "CARD MARKET";
    P.v.push_back(p);
  }
  if (g_mk.career == 0) {
    P.K.text("LOAD A FRANCHISE CAREER", 825, 520, 64, 1400, kDim, 2.0f);
  } else {
    const int n = static_cast<int>(g_mk.days.size());
    // r232 (the coordinator's look at r231's TOP CARDS page: the title's T ran under "WEEK 15 PRICES"): the week block starts after
    // the title's own measured width (+ its 4 px outline, a gap) and stops before MY PORTFOLIO's box (x 1060) - never touching
    const float titleR = 330.0f + std::min(460.0f, P.K.width("CARD MARKET", 60)) + 8.0f;
    const float wx0 = titleR + 30.0f, wx1 = 1040.0f;
    P.K.left(g_mk.week > 20 ? "PLAYOFF PRICES" : "WEEK " + std::to_string(std::max(1, g_mk.week)) + " PRICES", wx0, 44, 40, wx1 - wx0, kInk, 2.0f);
    if (!(g_mk.cycle > 0.08 || g_mk.cycle < -0.06)) P.K.left("SEASON " + std::to_string(std::max(1, g_mk.season)) + "  -  RE-PRICED EVERY MATCHDAY", wx0, 92, 24, wx1 - wx0, kDim, 0.0f);
    // r210: the market's mood (a pill under the week): BOOMING / CRASHING / HOT / COOL / CALM
    {
      const double cy = g_mk.cycle;
      const char* mood = cy > 0.25 ? "MARKET BOOMING" : (cy > 0.08 ? "MARKET HOT" : (cy < -0.2 ? "MARKET CRASHING" : (cy < -0.06 ? "MARKET COOL" : "")));
      if (mood[0] != '\0') {
        const uint32_t mc = cy > 0 ? kUp : kDown;
        P.K.glow(wx0 - 20, 82, wx1 + 20, 120, mc, 0.18f + 0.06f * std::sin(t * 3.0f));
        P.K.left(std::string(mood) + "  " + (cy > 0 ? "+" : "") + std::to_string(static_cast<int>(std::lround((std::exp(cy) - 1.0) * 100))) + "%", wx0, 92, 24, wx1 - wx0, mc, 0.0f);
      }
    }
    // your portfolio: what the binder is worth at market, its change, its own little chart (r210: 64-bit, doubles for the chart)
    const long long pv = portfolio_value(-1);
    std::vector<double> ph;
    for (long long v : g_mk.portfolio) ph.push_back(static_cast<double>(v));
    if (ph.empty() || ph.back() != static_cast<double>(pv)) ph.push_back(static_cast<double>(pv));
    const float pch = ph.size() >= 2 ? static_cast<float>(100.0 * (ph.back() - ph[ph.size() - 2]) / std::max(1.0, ph[ph.size() - 2])) : 0.0f;
    P.K.box(1060, 14, 1420, 112, 22, 0x0E152C);
    P.K.left("MY PORTFOLIO", 1076, 40, 24, 200, kDim, 0.0f);
    P.K.left(money(pv, 46, 196), 1076, 84, 46, 200, kInk, 2.0f);
    spark(P, 1290, 26, 1406, 70, ph, pch >= 0 ? kUp : kDown, 3.0f, true);
    pct_tag(P, pch, 1406, 92, 22, 1);
    coin(P, 1458, 52, 24);
    P.K.right(money(balance(), 46, 170), kW - 16, 54, 46, 170, kGold, 2.0f);  // (r208: past what fits short: "4.2B")
    P.K.right("BIT COINS", kW - 16, 98, 22, 170, kDim, 0.0f);
    (void)n;
    // the tabs (r210: + MY LISTINGS)
    const char* const kTabName[kTabs] = {"MY CARDS", "LISTINGS", "TOP CARDS", "BUY", "OFFERS", "SALES"};  // (r231: MOVERS -> TOP CARDS; r238: + SALES)
    for (int i = 0; i < kTabs; ++i) {
      const float tx0 = i * 150.0f, tx1 = tx0 + 142;
      const bool on = g_tab == i;
      if (on) P.K.glow(tx0 - 18, 122, tx1 + 18, 196, kCyan, 0.3f);
      P.K.box(tx0, 134, tx1, 182, 24, on ? 0x153A60 : 0x0F1630);
      P.K.ring(tx0, 134, tx1, 182, 24, on ? 4.0f : 2.0f, on ? kCyan : kLine);
      P.K.text(kTabName[i], 0.5f * (tx0 + tx1), 159, 28, 156, on ? 0xFFFFFF : kDim, on ? 2.0f : 0.0f);
      const int badge = i == kTabOffers ? static_cast<int>(g_mk.offers.size()) : (i == kTabListed ? g_mk.unseenSales : 0);
      if (badge > 0) {
        P.K.box(tx1 - 30, 120, tx1 + 12, 156, 18, i == kTabListed ? 0x1E9E50 : xrmenu::kBadge);
        P.K.text(std::to_string(std::min(99, badge)), tx1 - 9, 139, 26, 36, 0xFFFFFF, 0.0f);
      }
    }
    xrmenu::glyph(P.K, "L", 914, 158, 40);
    P.K.left("LEFT / RIGHT: TABS", 962, 159, 22, 120, kDim, 0.0f);
    if (g_tab == kTabMine) sell_fresh();  // r240
    if (g_tab == kTabMine && g_kb) draw_sell_kb(P, t);  // r240
    else if (g_tab == kTabMine) draw_mine(P, t);
    else if (g_tab == kTabListed) draw_listed(P, t);
    else if (g_tab == kTabMovers) draw_movers(P, t);
    else if (g_tab == kTabBuy) draw_buy(P, t);
    else if (g_tab == kTabSales) draw_sales(P, t);  // r238
    else draw_offers(P, t);
    if (g_pad) draw_pad(P, t);  // r210: the number pad over the page
    // the foot: the last word, else the wire scrolling
    const bool fresh = g_now - g_msgAt < 6.0;
    P.K.box(0, 904, kW, 958, 26, 0x070B18);
    P.K.ring(0, 904, kW, 958, 26, 2, fresh ? g_msgRgb : 0x2C3A66);
    if (fresh) {
      P.K.text(g_msg, 0.5f * kW, 932, 30, kW - 60, g_msgRgb, 0.0f);
    } else {
      std::string wire;
      for (size_t i = 0; i < g_mk.news.size() && i < 2; ++i) wire += (i ? "      -      " : "") + g_mk.news[i].text;
      if (wire.empty()) wire = "BUY 112%  -  SELL NOW 90%  -  LIST FOR SALE AT YOUR PRICE  -  PRICES MOVE WITH YOUR FRANCHISE, THE HYPE AND THE MARKET'S MOOD";  // (r238: 90%)
      P.K.text(wire, 0.5f * kW, 932, 26, kW - 60, 0xFFB020, 0.0f);
    }
  }
  }  // (r236: the old frame - outside the hub)
  const float fl = static_cast<float>(g_now - g_flashAt);
  if (fl >= 0.0f && fl < 0.5f) P.K.box(0, 0, kW, kH, 30, 0xFFFFFF, 0.35f * (1.0f - fl / 0.5f));
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
    p.cut *= k;  // r236
    p.lean *= k;
    if (bcast && text && !p.styled && !p.art && p.y1 >= 14.0f && p.y1 < 30.0f * k) p.y1 = 30.0f * k;  // r236: nothing under 30 px in the hub (the card art keeps its own: styled)
    for (size_t i = 0; i < p.pts.size(); ++i) p.pts[i] = (i % 2 == 0 ? ox : oy) + p.pts[i] * k;
    out.push_back(std::move(p));
  }
}

std::vector<Holding> my_copies() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_hold;
}
std::string card_name(int card) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return name_of_card(card);
}
std::string card_key(int card) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return card >= 0 && card < static_cast<int>(g_set.size()) ? g_set[static_cast<size_t>(card)].key : std::string();
}
int card_index(const std::string& key) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  auto it = g_byKey.find(key);
  return it != g_byKey.end() ? it->second : -1;
}
void draw_mini(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, const Variant& v, float x0, float y0, float x1, float y1) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  Pen P;
  P.keep = &keep;
  mini(P, v, x0, y0, x1, y1);
  for (xrtext::BoardPiece& p : P.v) out.push_back(std::move(p));
}

// ============================================================================================== the PC proof
Stats stats() {
  std::lock_guard<std::mutex> lk(g_m);
  Stats s;
  s.day = g_mk.steps;
  s.lines = static_cast<int>(g_mk.lines.size());
  s.listings = static_cast<int>(g_mk.listings.size());
  s.offers = static_cast<int>(g_mk.offers.size());
  s.spent = g_mk.spent;
  s.received = g_mk.received;
  s.bought = g_mk.bought;
  s.sold = g_mk.sold;
  s.career = g_mk.career;  // r213
  s.season = g_mk.season;
  s.week = g_mk.week;
  s.tick = g_mk.ticks;  // r238
  return s;
}
// r213: the COLLECTOR TRADES' held copies + your listed ones (xr_market.h)
void set_held_out(const std::vector<Variant>& copies) {
  std::lock_guard<std::mutex> lk(g_m);
  g_heldOut = copies;
  for (size_t i = 0; i < g_mk.offers.size();)  // (offers on a copy no longer free go, as for a listing)
    if (!g_hold.empty() && !own_free(g_mk.offers[i].v)) g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
    else ++i;
  tell_listed();
  ++g_gen;
}
std::vector<Variant> listed_copies() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<Variant> v;
  for (const Mine& m : g_mk.mine) v.push_back(m.v);
  return v;
}
std::vector<std::string> wire_lines() {  // r214
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<std::string> v;
  for (const Headline& h : g_mk.news) v.push_back(h.text);
  return v;
}
void debug_tab(int tab, int cursor) {
  std::lock_guard<std::mutex> lk(g_m);
  g_tab = std::max(0, std::min(kTabs - 1, tab));
  g_cur[g_tab] = cursor;
  g_ask = 0;
  g_countering = cursor < 0;
  if (cursor < 0) {
    g_cur[g_tab] = 0;
    if (!g_mk.offers.empty()) g_askId = g_mk.offers[0].id;
  }
}
std::vector<OfferView> offers() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<OfferView> v;
  for (const Offer& o : g_mk.offers) v.push_back({o.id, o.v, o.who, o.amount, o.bidders, price_i(o.v, -1), o.final});
  return v;
}
std::vector<ListingView> listings() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<ListingView> v;
  for (const Listing& L : g_mk.listings) {
    const long long p = price_i(L.v, -1);
    v.push_back({L.v, L.seller, ask_of(p), p, L.sold});
  }
  return v;
}
bool buy_listing(int i) {
  std::lock_guard<std::mutex> lk(g_m);
  return do_buy(i);
}
bool sell_now(const Variant& v) {
  std::lock_guard<std::mutex> lk(g_m);
  return do_sell(v, bid_of(price_i(v, -1)), "STRIKERZ MARKET SALE");
}
bool accept_offer(uint32_t id) {
  std::lock_guard<std::mutex> lk(g_m);
  return do_accept(id);
}
int counter_offer(uint32_t id, int percent) {
  std::lock_guard<std::mutex> lk(g_m);
  return do_counter(id, percent);
}
bool decline_offer(uint32_t id) {
  std::lock_guard<std::mutex> lk(g_m);
  for (size_t i = 0; i < g_mk.offers.size(); ++i)
    if (g_mk.offers[i].id == id) {
      g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
      return true;
    }
  return false;
}
void reset_for_test() {
  std::lock_guard<std::mutex> lk(g_m);
  g_mk = Market();
  g_mag = MagMarket();  // r219
  g_lastRead = -1e9;
  g_pad = false;
  g_hist.clear(), g_histN = 0, g_histSeq = 0, g_histDirty = false, g_notices.clear(), g_salesRows.clear(), g_salesCard = -1, g_liveAcc = 0.0;  // r238
  g_salesForm = -1, g_fFam = false;  // r239
  ++g_priceEpoch, g_pcache.clear(), g_sell.clear(), g_sellRows.clear(), g_sellKey = 0, g_sellRowsDirty = true;  // r240
  g_sq.clear(), g_sfSet = kFSetAll, g_sfFin = kSFinAny, g_sfType = kSTypeAny, g_sfStat = kSStatAll, g_sfPrice = kSPriceAny, g_sfSort = kSSortHigh;
  g_sfFam = g_sLast = false, g_sPct = 1, g_museumV.clear();
}
// ---- r210: your listings (the proof drives the same functions the page's buttons do)
std::vector<MineView> my_listings() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<MineView> v;
  for (const Mine& m : g_mk.mine) v.push_back({m.id, m.v, m.ask, price_i(m.v, -1), std::max(0, g_mk.steps - m.listed), m.views, m.watching});
  return v;
}
uint32_t list_for_sale(const Variant& v, long long ask) {
  std::lock_guard<std::mutex> lk(g_m);
  const uint32_t id = do_list(v, ask);
  if (id != 0) save_market();
  return id;
}
bool edit_listing(uint32_t id, long long ask) {
  std::lock_guard<std::mutex> lk(g_m);
  Mine* m = mine_by(id);
  if (m == nullptr || ask < 1) return false;
  m->ask = xrcoin::clamp(ask);
  save_market();
  ++g_gen;
  return true;
}
bool cancel_listing(uint32_t id) {
  std::lock_guard<std::mutex> lk(g_m);
  const bool ok = do_unlist(id);
  if (ok) save_market();
  return ok;
}
std::vector<SaleView> my_sales() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<SaleView> v;
  for (const Sale& s : g_mk.sales) v.push_back({s.v, s.buyer, s.amount, s.market, s.day, s.surprise});
  return v;
}
double cycle_now() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_mk.cycle;
}
// r219: STRIKERS ILLUSTRATED's market spread - a copy of this matchday's (made by advance / a career's load), microseconds under the lock
bool magazine(MagMarket* out) {
  std::lock_guard<std::mutex> lk(g_m);
  if (!g_mag.valid || g_mk.career == 0 || g_mag.career != g_mk.career) return false;
  *out = g_mag;
  return true;
}
long long record_sale(std::string* headline) {
  std::lock_guard<std::mutex> lk(g_m);
  if (headline != nullptr) *headline = g_mk.recordText;
  return g_mk.record;
}
// ---- r235: THE SINGLES MARKET (xr_market.h)
void set_offerings(unsigned career, const std::vector<Offering>& rows) {
  std::lock_guard<std::mutex> lk(g_m);
  g_offs = rows;
  g_offCareer = career;
  if (career != g_mk.career) return;
  rows_build();
  watch_check();
  ++g_gen;
}
void set_buyers(const std::vector<Buyer>& b) {
  std::lock_guard<std::mutex> lk(g_m);
  g_buyers = b;
}
bool paid_recent(const Variant& v, long long* paid) {
  std::lock_guard<std::mutex> lk(g_m);
  auto it = g_paid.find(sig_of(v));
  if (it == g_paid.end() || it->second.step < g_mk.steps - 1) return false;
  if (paid != nullptr) *paid = it->second.coins;
  return true;
}
void search_set(const std::string& query, int set, int finish, int price, int sort) {
  std::lock_guard<std::mutex> lk(g_m);
  g_q = upper(query).substr(0, 16);
  g_fSet = std::max(0, std::min(kFSets - 1, set));
  g_fFin = std::max(0, std::min(kFFins - 1, finish));
  g_fPrice = std::max(0, std::min(kFPrices - 1, price));
  g_fSort = std::max(0, std::min(kSorts - 1, sort));
  g_cur[kTabBuy] = g_top[kTabBuy] = 0;
  rows_build();
  g_salesDirty = true;  // r238: (SALES' list follows the query too)
  ++g_gen;
}
void search_family(bool on) {  // r239
  std::lock_guard<std::mutex> lk(g_m);
  g_fFam = on;
  g_cur[kTabBuy] = g_top[kTabBuy] = 0;
  rows_build();
  ++g_gen;
}
bool search_family_on() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_fFam;
}
std::vector<SearchRow> search_results() {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_rowsDirty) rows_build();
  return g_rows;
}
bool search_buy(int row, std::string* line) {
  PendingBuy p;
  bool ok = false;
  {
    std::lock_guard<std::mutex> lk(g_m);
    if (g_rowsDirty) rows_build();
    if (!ask_row(row)) return false;
    if (g_ask == 2) {
      ok = do_buy(g_askIdx);
      if (line != nullptr) *line = ok ? "BOUGHT FROM THE DEALER" : "NO";
      if (ok) rows_build(), save_market();
    } else {
      p.on = true, p.id = g_askOffer, p.ask = g_askCoins, p.v = g_askV, p.who = g_askWho;
    }
    g_ask = 0;
  }
  if (!p.on) return ok;
  std::string l;
  const bool r = g_src.buy_offering != nullptr && g_src.buy_offering(p.id, p.ask, &l);
  {
    std::lock_guard<std::mutex> lk(g_m);
    if (r) {
      read_holdings();
      g_mk.spent = xrcoin::add(g_mk.spent, p.ask);
      ++g_mk.bought;
      g_mk.firstSeen[sig_of(p.v)] = g_mk.steps;
      g_paid[sig_of(p.v)] = {p.ask, g_mk.steps};
      record_locked(p.v, p.ask, p.who, "YOU", 2);  // r238
      save_market();
    }
    rows_build();
  }
  if (line != nullptr) *line = l;
  return r;
}
bool watch_toggle(int card) {
  std::lock_guard<std::mutex> lk(g_m);
  const bool now = watch_flip(card);
  watch_check();
  save_market();
  return now;
}
std::vector<std::string> watched() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_watch;
}
std::vector<std::string> recent_searches() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_recent;
}
void debug_search(int stage, int key) {
  std::lock_guard<std::mutex> lk(g_m);
  g_tab = kTabBuy;
  g_pad = false;
  g_ask = 0;
  g_kb = g_kbOpen = stage == 1;
  if (stage == 1) g_kbKey = std::max(0, std::min(kKbAll - 1, key));
  if (stage == 2) ask_row(std::max(0, key));
  if (stage == 3) {  // (the pointer's way: a press on a key of the panel, as point() does)
    g_kb = g_kbOpen = true;
    kb_press(std::max(0, std::min(kKbAll - 1, key)));
  }
  if (g_rowsDirty) rows_build();
  ++g_gen;
}
// ---- r238: THE LIVE MARKET, SALES, NOTICES, HOLD BEFORE YOU BUY (xr_market.h)
void set_live(bool hubOpen) { g_live = hubOpen; }  // (no lock: xr_cards every frame)
void debug_tick() {
  std::lock_guard<std::mutex> lk(g_m);
  g_liveAcc = 0.0;
  tick_now();
  save_market();
}
double sell_chance(const Variant& v, long long ask) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  if (v.card < 0 || v.card >= static_cast<int>(g_set.size())) return 0.0;
  const long long market = price_i(v, -1);
  return sell_p(v, static_cast<double>(ask) / static_cast<double>(std::max(1LL, market)), 0);
}
void record_sale(const Variant& v, long long price, const char* seller, const char* buyer, int kind) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  if (v.card < 0 || v.card >= static_cast<int>(g_set.size())) return;
  record_locked(v, price, seller != nullptr ? seller : "?", buyer != nullptr ? buyer : "?", kind);
}
std::vector<SaleRec> sales_of(int card) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  auto it = g_hist.find(family_card(card));
  return it != g_hist.end() ? it->second : std::vector<SaleRec>();
}
int sales_count() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_histN;
}
int sale_group(const Variant& v) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return group_of(v);
}
void post_notice(const std::string& head, const std::string& title, const std::string& line) {  // r242
  std::lock_guard<std::mutex> lk(g_m);
  notice(head, title, line);
}
bool take_notice(std::string* head, std::string* title, std::string* line) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_notices.empty()) return false;
  *head = g_notices.front().head;
  *title = g_notices.front().title;
  *line = g_notices.front().line;
  g_notices.erase(g_notices.begin());
  return true;
}
// the BUY tab's copy under a pointer (LOOK B, draw_buy_b's layout: 5 rows at the left from y 286, the focused copy's panel at the right)
bool inspect_at(float x, float y, Variant* v, long long* ask, std::string* who) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_mk.career == 0 || g_tab != kTabBuy || g_kb || g_pad || g_ask != 0 || g_countering) return false;
  if (g_rowsDirty) rows_build();
  const int n = static_cast<int>(g_rows.size());
  if (n == 0) return false;
  int at = -1;
  constexpr float ry0 = 286.0f, rh = (898.0f - ry0 - 4 * 12.0f) / 5.0f;
  if (x >= 0.0f && x <= 970.0f && y >= ry0 && y <= 898.0f) {
    const int r = static_cast<int>((y - ry0) / (rh + 12.0f));
    if (r >= 0 && r < 5) at = g_top[kTabBuy] + r;
  } else if (x >= 1010.0f && x <= kW && y >= 104.0f && y <= 898.0f) {
    at = g_cur[kTabBuy];
  }
  if (at < 0 || at >= n) return false;
  if (g_cur[kTabBuy] != at) g_cur[kTabBuy] = at, ++g_gen;
  const SearchRow& r = g_rows[static_cast<size_t>(at)];
  *v = r.v;
  *ask = r.ask;
  *who = r.who;
  return true;
}
bool inspect_buy(const Variant& v, long long ask) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_mk.career == 0) return false;
  if (g_rowsDirty) rows_build();
  const std::string s = sig_of(v);
  for (size_t i = 0; i < g_rows.size(); ++i)
    if (sig_of(g_rows[i].v) == s && g_rows[i].ask == ask) {
      g_tab = kTabBuy;
      g_cur[kTabBuy] = static_cast<int>(i);
      g_kb = false, g_kbOpen = false, g_pad = false;
      ask_row(static_cast<int>(i));
      ++g_gen;
      return true;
    }
  say("THAT COPY ISN'T FOR SALE ANY MORE", 0xFF4D5E);
  ++g_gen;
  return false;
}
void debug_sales(int card, int group) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  g_tab = kTabSales;
  g_kb = g_kbOpen = false;
  g_pad = false;
  g_ask = 0;
  g_salesCard = card >= 0 ? family_face(family_card(card)) : -1;  // (r239: his family's face, ALL FORMS)
  g_salesForm = -1;
  g_salesGroup = 0;
  if (g_salesCard >= 0) {  // (the focused group: the one with the newest sale)
    const std::vector<SaleRec> recs = family_sales(g_salesCard, -1);
    if (!recs.empty()) g_salesGroup = group_of(recs.front().v);
  }
  if (group >= 0 && group < kSaleGroups) g_salesGroup = group;
  g_salesDirty = true;
  ++g_gen;
}
// ---- r239: SALES by FAMILY (xr_market.h)
void debug_sales_form(int form) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  g_salesForm = form >= 0 && form < static_cast<int>(g_set.size()) ? family_card(form) : -1;
  ++g_gen;
}
std::vector<int> sales_players() {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  sales_rows_build();
  std::vector<int> out;
  for (const SalesRow& r : g_salesRows) out.push_back(r.card);
  return out;
}
std::vector<int> sales_forms(int card) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  return family_forms(card);
}
std::vector<SaleRec> sales_of_family(int card, int form) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  if (card < 0 || card >= static_cast<int>(g_set.size())) return {};
  return family_sales(card, form);
}

// ---- r240: SELL, FILTERED (xr_market.h)
void sell_set(const SellFilter& f) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  g_sq = upper(f.query).substr(0, 16);
  g_sfSet = std::max(0, std::min(kFSets - 1, f.set));
  g_sfFin = std::max(0, std::min(kSFins - 1, f.finish));
  g_sfType = std::max(0, std::min(kSTypes - 1, f.type));
  g_sfStat = std::max(0, std::min(kSStats - 1, f.status));
  g_sfPrice = std::max(0, std::min(kSPrices - 1, f.price));
  g_sfSort = std::max(0, std::min(kSSorts - 1, f.sort));
  g_sfFam = f.family;
  g_cur[kTabMine] = g_top[kTabMine] = 0;
  g_sellRowsDirty = true;
  sell_fresh();
  ++g_gen;
}
SellFilter sell_filter() {
  std::lock_guard<std::mutex> lk(g_m);
  SellFilter f;
  f.query = g_sq, f.set = g_sfSet, f.finish = g_sfFin, f.type = g_sfType, f.status = g_sfStat, f.price = g_sfPrice, f.sort = g_sfSort, f.family = g_sfFam;
  return f;
}
std::vector<SellRow> sell_rows() {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  sell_fresh();
  std::vector<SellRow> out;
  out.reserve(g_sellRows.size());
  for (int i : g_sellRows) {
    const SellItem& s = g_sell[static_cast<size_t>(i)];
    SellRow r;
    r.h = s.h, r.market = s.market, r.bid = s.bid, r.chg = s.chg, r.listed = s.listed, r.heldOut = s.heldOut, r.museum = s.museum, r.free = s.free;
    r.spare = s.spare, r.seen = s.seen, r.rank = s.rank;
    out.push_back(r);
  }
  return out;
}
BulkPlan sell_plan(bool list, int pct, bool lastToo) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  sell_fresh();
  return bulk_plan(list, pct, lastToo).b;
}
int sell_all_shown(bool lastToo, long long* coins) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  sell_fresh();
  const int n = do_sell_all(bulk_plan(false, 100, lastToo), coins);
  if (n > 0) save_market();
  g_sellRowsDirty = true;
  return n;
}
int list_all_shown(int pct, bool lastToo, long long* asked) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  sell_fresh();
  const int n = do_list_all(bulk_plan(true, pct, lastToo), pct, asked);
  if (n > 0) save_market();
  g_sellRowsDirty = true;
  return n;
}
void set_museum(const std::vector<Variant>& copies) {
  std::lock_guard<std::mutex> lk(g_m);
  g_museumV = copies;  // (sell_key sees it: SELL's rows made again on their next look)
  ++g_gen;
}
// SELL's copy under a pointer (draw_sell_b's layout: 4 rows at the left from y 286, the focused copy's panel at the right)
bool inspect_mine_at(float x, float y, Variant* v, long long* market) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_mk.career == 0 || g_tab != kTabMine || g_kb || g_pad || g_ask != 0 || g_countering) return false;
  load_set();
  sell_fresh();
  const int n = static_cast<int>(g_sellRows.size());
  if (n == 0) return false;
  int at = -1;
  constexpr float ry0 = 286.0f, rh = (898.0f - ry0 - 3 * 12.0f) / 4.0f;
  if (x >= 0.0f && x <= 970.0f && y >= ry0 && y <= 898.0f) {
    const int r = static_cast<int>((y - ry0) / (rh + 12.0f));
    if (r >= 0 && r < 4) at = g_top[kTabMine] + r;
  } else if (x >= 1010.0f && x <= kW && y >= 104.0f && y <= 898.0f) {
    at = g_cur[kTabMine];
  }
  if (at < 0 || at >= n) return false;
  if (g_cur[kTabMine] != at) g_cur[kTabMine] = at, ++g_gen;
  const SellItem& s = g_sell[static_cast<size_t>(g_sellRows[static_cast<size_t>(at)])];
  *v = s.h;
  *market = s.market;
  return true;
}
bool inspect_sell(const Variant& v) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_mk.career == 0) return false;
  load_set();
  sell_fresh();
  const std::string sig = sig_of(v);
  for (size_t i = 0; i < g_sellRows.size(); ++i) {
    const SellItem& s = g_sell[static_cast<size_t>(g_sellRows[i])];
    if (s.sig != sig || s.h.grading) continue;
    g_tab = kTabMine;
    g_cur[kTabMine] = static_cast<int>(i);
    g_kb = false, g_kbOpen = false, g_pad = false;
    ++g_gen;
    if (s.free <= 0) {
      const std::string lk2 = lock_words(s);
      say("IT CAN'T SELL NOW - " + (lk2.empty() ? std::string("LOCKED") : lk2), 0xFFB020);
      return false;
    }
    g_ask = 1;
    g_askV = s.h;
    g_askCoins = s.bid;
    return true;
  }
  say("THAT COPY ISN'T IN YOUR BINDER ANY MORE", 0xFF4D5E);
  ++g_gen;
  return false;
}
void debug_sell(int stage, int key) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  g_tab = kTabMine;
  g_pad = false;
  if (stage == 0) g_ask = 0, g_kb = g_kbOpen = false;
  if (stage == 1) g_ask = 0, g_kb = g_kbOpen = true, g_kbKey = std::max(0, std::min(kKbAllSell - 1, key));
  if (stage == 2) {  // (a pointer's press on a key of the panel, as point() does)
    g_kb = g_kbOpen = true;
    g_kbKey = std::max(0, std::min(kKbAllSell - 1, key));
    kb_press(g_kbKey);
  }
  if (stage == 3) g_ask = 0;
  sell_fresh();
  ++g_gen;
}

void debug_pad(int stage) {
  std::lock_guard<std::mutex> lk(g_m);
  g_pad = false;
  if (stage == 1 || stage == 2) {
    g_tab = kTabMine;
    sell_fresh();  // r240: SELL's row as shown (r210-r239: mine_sorted)
    const SellItem* s = sell_at(g_cur[kTabMine]);
    if (s == nullptr) return;
    const Holding h = s->h;
    pad_open_for(h, 0, round_nice(price_i(h, -1)));
    if (stage == 2) {
      g_padDigits = std::to_string(std::max(1LL, price_i(h, -1) * 13 / 10));
      g_padFresh = false;
      g_padKey = 12;
    }
  } else if (stage == 3 && !g_mk.mine.empty()) {
    g_tab = kTabListed;
    const Mine& m = g_mk.mine[static_cast<size_t>(std::max(0, std::min(static_cast<int>(g_mk.mine.size()) - 1, g_cur[kTabListed])))];
    pad_open_for(m.v, m.id, m.ask);
    g_padKey = 7;
  }
  ++g_gen;
}

// ---- r241: CARDS > GRADING (xr_market.h) - xr_cards' page drawn + driven here
static bool grade_input_locked(const Input& in) {  // (g_m held)
  if (g_mk.career == 0) return false;
  auto done = [&]() { ++g_gen; };
  if (g_pad) g_pad = false;  // (the market's number pad belongs to its page: never up over GRADING)
  if (g_kb && !g_gradeKb.load()) kb_close();  // (the market's panel left open: down)
  if (g_ask != 0 && g_ask != 8) g_ask = 0;    // (a market question left asking: dropped - its page isn't up)
  if (g_kb && g_ask == 0) {  // its search panel: stick moves, A presses, X DEL, B closes it
    const int k0 = g_kbKey;
    bool hit = false;
    if (in.step && in.dir != 0) kb_move(in.dir);
    if (in.a) kb_press(g_kbKey), hit = true;
    else if (in.x) kb_press(29), hit = true;
    else if (in.b) {
      kb_close();
      sound("book_tick", 0.5f);
      hit = true;
    }
    if (hit || g_kbKey != k0) done();
    return true;
  }
  if (g_ask == 8) {  // the send asking (the hub's pop-up): A yes, B no
    if (in.a) {
      grade_yes();
      done();
    } else if (in.b) {
      g_ask = 0;
      g_gradeAskA = false;
      done();
    }
    return true;
  }
  grade_fresh();
  const int n = static_cast<int>(g_gradeRows.size());
  if (in.step && (in.dir == 3 || in.dir == 4) && n > 0) {
    g_gCur = std::max(0, std::min(n - 1, g_gCur + (in.dir == 4 ? 1 : -1)));
    sound("book_tick", 0.4f);
    done();
    return true;
  }
  g_gCur = std::max(0, std::min(std::max(0, n - 1), g_gCur));
  if (in.x) {  // X: the SEARCH panel (a player, GRADING's filters)
    g_kb = true;
    g_kbOpen = true;
    g_gradeKb = true;
    g_kbKey = 0;
    sound("book_ding", 0.5f);
    done();
    return true;
  }
  if (in.a && n > 0) {
    grade_ask_row(g_gCur);
    done();
    return true;
  }
  return in.a || in.x || in.step;
}
void grade_draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  Pen P;
  P.keep = &keep;
  if (g_mk.career == 0) {
    xrmenu::say(P.K, "LOAD A FRANCHISE CAREER", 825, 480, 64, xrmenu::kBDim, 'c', 1400);
  } else {
    grade_fresh();  // (GRADING's rows: made only if something changed)
    grade_subtabs(P);
    if (g_kb && g_gradeKb.load()) draw_grade_kb(P, t);
    else draw_grade_b(P, t);
    if (g_now - g_msgAt < 6.0) xrmenu::say(P.K, g_msg, 0.5f * kW, 930, 34, g_msgRgb, 'c', kW - 60);
  }
  const float fl = static_cast<float>(g_now - g_flashAt);
  if (fl >= 0.0f && fl < 0.5f) P.K.box(0, 0, kW, kH, 30, 0xFFFFFF, 0.35f * (1.0f - fl / 0.5f));
  for (xrtext::BoardPiece& p : P.v) {  // (as draw(): page units -> the hub's picture, nothing under 30 px)
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
    p.cut *= k;
    p.lean *= k;
    if (xrmenu::broadcast() && text && !p.styled && !p.art && p.y1 >= 14.0f && p.y1 < 30.0f * k) p.y1 = 30.0f * k;
    for (size_t i = 0; i < p.pts.size(); ++i) p.pts[i] = (i % 2 == 0 ? ox : oy) + p.pts[i] * k;
    out.push_back(std::move(p));
  }
}
bool grade_input(const Input& in) {
  std::lock_guard<std::mutex> lk(g_m);
  return grade_input_locked(in);
}
void grade_hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord) {
  std::lock_guard<std::mutex> lk(g_m);
  *xWord = *yWord = "";
  *aDim = false;
  if (g_kb && g_gradeKb.load()) {
    *aWord = g_kbKey == 31 ? "DONE" : "PRESS";
    *xWord = "DEL";
    return;
  }
  grade_fresh();
  const SellItem* s = grade_at(g_gCur);
  *aWord = "SEND TO GRADING";
  *aDim = s == nullptr || !grade_lock(*s).empty() || balance() < grade_cost_of(s->h);
  *xWord = "SEARCH";
  *yWord = "YOUR SLABS";
}
bool grade_asking(std::string* q, std::string* line) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_ask != 8) return false;
  const Variant& v = g_gradeAskV;
  *q = "SEND TO TIZM GRADING FOR " + coins_text(g_gradeAskCost) + " COINS?";
  *line = upper(variant_words(v)) + " " + name_of_card(v.card) + "  -  BACK AFTER " + std::to_string(kGradeBackMatches) + " MATCHES";
  return true;
}
bool grade_busy() {
  std::lock_guard<std::mutex> lk(g_m);
  return (g_kb && g_gradeKb.load()) || g_ask == 8;
}
int search_mode() { return g_kbOpen.load() ? (g_gradeKb.load() ? 2 : 1) : 0; }  // (no lock: as search_open)
void grade_leave() {
  if (!g_gradeAskA.load()) return;
  std::lock_guard<std::mutex> lk(g_m);
  if (g_ask == 8) g_ask = 0, ++g_gen;
  g_gradeAskA = false;
}
void grade_set(const GradeFilter& f) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  g_gq = upper(f.query).substr(0, 16);
  g_gfSet = std::max(0, std::min(kFSets - 1, f.set));
  g_gfFin = std::max(0, std::min(kSFins - 1, f.finish));
  g_gfType = std::max(0, std::min(kSTypes - 1, f.type));
  g_gfStat = std::max(0, std::min(kGStats - 1, f.status));
  g_gfSort = std::max(0, std::min(kGSorts - 1, f.sort));
  g_gfFam = f.family;
  g_gCur = g_gTop = 0;
  g_gradeRowsDirty = true;
  grade_fresh();
  ++g_gen;
}
GradeFilter grade_filter() {
  std::lock_guard<std::mutex> lk(g_m);
  GradeFilter f;
  f.query = g_gq, f.set = g_gfSet, f.finish = g_gfFin, f.type = g_gfType, f.status = g_gfStat, f.sort = g_gfSort, f.family = g_gfFam;
  return f;
}
std::vector<GradeRow> grade_rows() {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  grade_fresh();
  std::vector<GradeRow> out;
  for (int i : g_gradeRows) {
    const SellItem& s = g_sell[static_cast<size_t>(i)];
    GradeRow r;
    r.h = s.h;
    r.market = s.market;
    r.cost = grade_cost_of(s.h);
    r.lock = grade_lock(s);
    r.free = r.lock.empty() ? s.free : 0;
    out.push_back(r);
  }
  return out;
}
int grade_cursor() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_gCur;
}
void grade_debug(int stage, int key) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  g_pad = false;
  if (stage == 0) g_kb = g_kbOpen = false, g_gradeKb = false;
  if (stage == 1) g_ask = 0, g_kb = g_kbOpen = true, g_gradeKb = true, g_kbKey = std::max(0, std::min(kKbAllGrade - 1, key));
  if (stage == 2) {
    g_kb = g_kbOpen = true;
    g_gradeKb = true;
    g_kbKey = std::max(0, std::min(kKbAllGrade - 1, key));
    kb_press(g_kbKey);
  }
  grade_fresh();
  if (stage == 3) g_gCur = std::max(0, std::min(static_cast<int>(g_gradeRows.size()) - 1, key));
  ++g_gen;
}

// ---- r242 (Lance 11-13: "see the hottest 20, like a power rankings type vibe. see if you have any, what variations of the card you may have,
// and show chart/graph of price action of said card"): CARDS > HOT 20. The r231 TOP CARDS race (each family's demand from its play - one
// player, shown as the form he plays as now), 20 rows: rank + its move since the last matchday, the player + his club's logo, the play behind
// it, his price + this week's %, YOUR COPIES. The focused player: his family's price over the matchdays kept (kHist) in a finish you pick
// (stick LEFT / RIGHT: BASE, SILVER, GOLD /10, 1/1, JAM '96, each EXQUISITE tier), its HIGH / LOW, + this week's move; YOU HAVE: every copy
// of his family you own (set, finish / number, grade - or AT GRADING -, value); A: his actions - BUY (MARKET > BUY searching him), SELL
// (MARKET > SELL showing his copies), SALES (his SALES), WATCH. Drawn on change only (its own key: the matchday's prices, your copies, the
// cursor), the market's lock alone.
namespace {
const uint8_t* (*g_logoSrc)(int, int*, int*) = nullptr;
int g_hotCur = 0, g_hotTop = 0, g_hotFin = 0, g_hotAct = -1;  // the row, the first row shown, the chart's finish, the action focused (-1: the list)
int g_hotJump = 0;                                            // 1: the hub goes to MARKET now (BUY / SELL / SALES picked) - hot_take_jump
constexpr int kHotN = 20, kHotRows = 6;
const char* const kHotActs[4] = {"BUY", "SELL", "SALES", "WATCH"};
struct HotFin {  // a finish the chart can show
  std::string name;
  Variant v;  // (its copy: the family's face in that set / finish)
};
std::vector<HotFin> hot_fins(int face) {  // (g_m held) the face's finishes: LANCINI TIZM's four, its JAM '96, its EXQUISITE tiers
  std::vector<HotFin> out;
  if (face < 0 || face >= static_cast<int>(g_set.size())) return out;
  Variant v;
  v.card = face;
  out.push_back({"BASE", v});
  v.parallel = kSilver;
  out.push_back({"SILVER", v});
  v.parallel = kGoldTizm, v.serial = 5;
  out.push_back({"GOLD /10", v});
  v.parallel = kBlack, v.serial = 1;
  out.push_back({"1/1", v});
  const int jam = twin_of(face, 1), exq = twin_of(face, 2);
  if (jam >= 0) {
    Variant j;
    j.card = jam;
    out.push_back({"JAM '96", j});
  }
  if (exq >= 0) {
    for (int t = xrexq::kGallery; t < xrexq::kTiers; ++t) {
      Variant e;
      e.card = exq;
      if (t == xrexq::kRpa && !exq_rookie_v(e)) continue;  // (a ROOKIE PATCH AUTO: rookies only)
      e.serial = xrexq::serial_of(t, std::min(2, xrexq::kRun[t]));
      e.parallel = xrexq::kClass[t];
      e.hit = xrexq::kSigned[t] ? kHitSigned : kHitNone;
      out.push_back({std::string("EXQ ") + xrexq::short_name(t, exq_rookie_v(e)), e});
    }
  }
  return out;
}
std::string hot_family_note(int face) {  // "HIS TOGEPI TOO" - his rookie card (the first form) when he plays as a later one
  const std::vector<int> fs = family_forms(face);
  if (fs.size() < 2 || fs.front() == face) return "";
  return "HIS " + search_name(fs.front()) + " TOO";
}
std::vector<Holding> hot_yours(int face) {  // (g_m held) every copy of his family you own (any set, any form), the most valuable first
  std::vector<Holding> out;
  if (face < 0 || face >= static_cast<int>(g_set.size())) return out;
  const std::string& ln = g_set[static_cast<size_t>(family_card(face))].line;
  for (const Holding& h : g_hold) {
    if (h.card < 0 || h.card >= static_cast<int>(g_set.size()) || h.count <= 0) continue;
    const int m = family_card(h.card);
    if (m == family_card(face) || (!ln.empty() && g_set[static_cast<size_t>(m)].line == ln)) out.push_back(h);
  }
  std::stable_sort(out.begin(), out.end(), [](const Holding& a, const Holding& b) { return price_i(a, -1) > price_i(b, -1); });
  return out;
}
std::string hot_fit(std::string s, float room) {  // the words that fit at 30 px (parts dropped from the end at " - ", else cut with "..")
  auto w = [](const std::string& x) { return xrtext::face_text_width(x, 30, xrtext::kFaceBroadcastUp, 0.14f); };
  while (w(s) > room) {
    const size_t cut = s.rfind(" - ");
    if (cut == std::string::npos || cut == 0) break;
    s = s.substr(0, cut);
    while (!s.empty() && s.back() == ' ') s.pop_back();
  }
  if (w(s) <= room) return s;
  while (s.size() > 3 && w(s + "..") > room) s.pop_back();
  return s + "..";
}
int hot_count(const std::vector<Holding>& v) {
  int n = 0;
  for (const Holding& h : v) n += h.count;
  return n;
}
void hot_logo(Pen& P, int club, float x0, float y0, float x1, float y1) {
  int w = 0, h = 0;
  const uint8_t* img = g_logoSrc != nullptr && club >= 0 ? g_logoSrc(club, &w, &h) : nullptr;
  if (img == nullptr || w <= 0 || h <= 0) {
    const char* pl = club >= 0 ? PortFranchiseBookClubPlace(club) : nullptr;
    P.K.box(x0, y0, x1, y1, 0.5f * (x1 - x0), 0x2B3D66);
    P.K.text(std::string(pl != nullptr && pl[0] ? pl : "-").substr(0, 2), 0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.45f * (y1 - y0), x1 - x0, 0xFFFFFF, 0.0f);
    return;
  }
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kImage;
  p.x0 = x0, p.y0 = y0, p.x1 = x1, p.y1 = y1;
  p.img = img, p.iw = w, p.ih = h;
  p.alpha = 1.0f;
  P.v.push_back(p);
}
std::vector<double> hot_chart(const Variant& v) {  // (g_m held) its price at every matchday kept, oldest first
  std::vector<double> out;
  for (int i = 0; i < static_cast<int>(g_mk.days.size()); ++i) out.push_back(price_d(v, i));
  return out;
}
void draw_hot(Pen& P) {  // (g_m held) the page (LOOK B: few words, 30 px at least)
  xrmenu::Kit& K = P.K;
  const std::vector<TopCard> top = top_list(kHotN);
  const int n = static_cast<int>(top.size());
  xrmenu::say(K, "HOT 20", 0, 44, 52, 0xFFFFFF, 'l', 300);
  xrmenu::label(K, "MOST WANTED - BY HOW THEY PLAY", 260, 46, 30, xrmenu::kBSoft, 'l', 700);
  if (n == 0) {
    xrmenu::say(K, "NO RACE YET - PLAY A MATCHDAY", 0.5f * kW, 480, 52, xrmenu::kBDim, 'c', kW - 60);
    return;
  }
  g_hotCur = std::max(0, std::min(n - 1, g_hotCur));
  if (g_hotCur < g_hotTop) g_hotTop = g_hotCur;
  if (g_hotCur >= g_hotTop + kHotRows) g_hotTop = g_hotCur - kHotRows + 1;
  g_hotTop = std::max(0, std::min(std::max(0, n - kHotRows), g_hotTop));
  const float lx1 = 970.0f, ry0 = 104.0f, rh = (898.0f - ry0 - (kHotRows - 1) * 10.0f) / kHotRows;
  for (int r = 0; r < kHotRows && g_hotTop + r < n; ++r) {
    const TopCard& c = top[static_cast<size_t>(g_hotTop + r)];
    const float y0 = ry0 + r * (rh + 10.0f), y1 = y0 + rh, cy = 0.5f * (y0 + y1);
    xrmenu::TileSpec ts;
    ts.colour = g_hotTop + r == g_hotCur ? 0xA855F7 : 0x2F8CFF;
    ts.on = g_hotTop + r == g_hotCur && g_hotAct < 0;
    xrmenu::tile(K, 0, y0, lx1, y1, ts);
    // the rank, big; its move since the last matchday
    xrmenu::say(K, "#" + std::to_string(c.rank), 16, cy - 14, 50, c.rank <= 5 ? kGold : 0xFFFFFF, 'l', 110);
    const int mv = c.prevRank > 0 ? c.prevRank - c.rank : 0;
    const std::string mt = c.prevRank <= 0 ? "NEW" : (mv > 0 ? "+" + std::to_string(mv) : (mv < 0 ? std::to_string(mv) : "="));
    if (c.prevRank > 0 && mv != 0) {
      const float ax = 30.0f, ay = cy + 32.0f;
      if (mv > 0) K.poly({ax - 12, ay + 8, ax + 12, ay + 8, ax, ay - 10}, kUp);
      else K.poly({ax - 12, ay - 8, ax + 12, ay - 8, ax, ay + 10}, kDown);
    }
    xrmenu::label(K, mt, 50, cy + 32, 30, c.prevRank <= 0 ? kGold : (mv > 0 ? kUp : (mv < 0 ? kDown : xrmenu::kBSoft)), 'l', 80);
    const Line* l = line_of(c.card);
    hot_logo(P, l != nullptr ? l->club : -1, 140, cy - 36, 212, cy + 36);
    const float tx = 230;
    const std::string fam = hot_family_note(c.card);
    xrmenu::say(K, search_name(c.card), tx, cy - 24, 40, 0xFFFFFF, 'l', 380);
    xrmenu::label(K, hot_fit(c.why.empty() ? (fam.empty() ? std::string("-") : fam) : c.why, lx1 - 150 - tx), tx, cy + 22, 30, l_rank_rgb(c.rank), 'l', lx1 - 150 - tx);
    // the right: his price, this week's move; YOU HAVE n on the top line
    const std::string at = money(c.price, 40, 200);
    xrmenu::say(K, at, lx1 - 24, cy - 24, 40, xrmenu::kBGold, 'r', 210);
    xrmenu::label(K, (c.pct >= 0.0f ? "+" : "-") + std::to_string(static_cast<int>(std::lround(std::fabs(c.pct)))) + "%", lx1 - 24, cy + 22, 30,
                  c.pct >= 0.5f ? kUp : (c.pct <= -0.5f ? kDown : xrmenu::kBSoft), 'r', 110);
    const int yours = hot_count(hot_yours(c.card));
    if (yours > 0) xrmenu::chip(K, "YOU HAVE " + std::to_string(yours), lx1 - 190, cy - 24, 0x2EA043, 'r', 30);
  }
  if (n > kHotRows) {
    K.poly({lx1 + 20, ry0 + 6, lx1 + 32, ry0 - 18, lx1 + 44, ry0 + 6}, g_hotTop > 0 ? xrmenu::kBGold : 0x3A4560);
    K.poly({lx1 + 20, 878, lx1 + 44, 878, lx1 + 32, 902}, g_hotTop + kHotRows < n ? xrmenu::kBGold : 0x3A4560);
  }
  // the focused player: his chart in a finish, its high / low, this week; YOU HAVE; his actions
  const TopCard& c = top[static_cast<size_t>(g_hotCur)];
  const float fx0 = 1010, fx1 = kW, fcx = 0.5f * (fx0 + fx1);
  xrmenu::panel(K, fx0, 104, fx1, 898, kGold, 0.94f);
  xrmenu::say(K, "#" + std::to_string(c.rank) + "  " + search_name(c.card), fx0 + 30, 146, 44, 0xFFFFFF, 'l', fx1 - fx0 - 60);
  {
    const std::string fam = hot_family_note(c.card);
    xrmenu::label(K, fam.empty() ? std::string("ONE FORM") : fam, fx0 + 30, 190, 30, fam.empty() ? xrmenu::kBDim : kGold, 'l', fx1 - fx0 - 60);
  }
  const std::vector<HotFin> fins = hot_fins(c.card);
  g_hotFin = fins.empty() ? 0 : std::max(0, std::min(static_cast<int>(fins.size()) - 1, g_hotFin));
  if (!fins.empty()) {
    const HotFin& f = fins[static_cast<size_t>(g_hotFin)];
    const std::vector<double> ch = hot_chart(f.v);
    double hi = 0, lo = 0;
    for (size_t i = 0; i < ch.size(); ++i) hi = i == 0 ? ch[i] : std::max(hi, ch[i]), lo = i == 0 ? ch[i] : std::min(lo, ch[i]);
    const float pct = ch.size() >= 2 ? static_cast<float>(100.0 * (ch.back() - ch[ch.size() - 2]) / std::max(0.01, ch[ch.size() - 2])) : 0.0f;
    K.box(fx0 + 30, 222, fx1 - 30, 420, 16, 0x0B1022, 0.85f);
    // the finish picker inside the chart's box: < GOLD /10 >  (stick LEFT / RIGHT) - n of m
    xrmenu::label(K, "<  " + f.name + "  >", fx0 + 46, 250, 30, 0xFFFFFF, 'l', fx1 - fx0 - 200);
    xrmenu::label(K, std::to_string(ch.size()) + " MATCHDAYS", fx1 - 46, 250, 30, xrmenu::kBSoft, 'r', 240);
    spark(P, fx0 + 46, 276, fx1 - 46, 412, ch, pct >= 0 ? kUp : kDown, 4.0f, true);
    xrmenu::say(K, money(ch.empty() ? price_i(f.v, -1) : llr(ch.back()), 44, 260), fx0 + 30, 456, 44, 0xFFFFFF, 'l', 280);
    xrmenu::label(K, (pct >= 0.0f ? "+" : "-") + std::to_string(static_cast<int>(std::lround(std::fabs(pct)))) + "% THIS WEEK", fx1 - 30, 456, 30,
                  pct >= 0.5f ? kUp : (pct <= -0.5f ? kDown : xrmenu::kBSoft), 'r', 300);
    xrmenu::label(K, "HIGH " + coins_text(llr(hi)), fx0 + 30, 496, 30, xrmenu::kBSoft, 'l', 0.5f * (fx1 - fx0) - 40);
    xrmenu::label(K, "LOW " + coins_text(llr(lo)), fx1 - 30, 496, 30, xrmenu::kBSoft, 'r', 0.5f * (fx1 - fx0) - 40);
  }
  // YOU HAVE: his family's copies (the most valuable first)
  const std::vector<Holding> mine = hot_yours(c.card);
  xrmenu::label(K, mine.empty() ? std::string("YOU HAVE NONE") : "YOU HAVE " + std::to_string(hot_count(mine)), fx0 + 30, 540, 30, mine.empty() ? xrmenu::kBDim : 0x3DDC84, 'l', 300);
  for (int i = 0; i < 3 && i < static_cast<int>(mine.size()); ++i) {
    const Holding& h = mine[static_cast<size_t>(i)];
    const float y = 580 + i * 42.0f;
    const int st = g_set[static_cast<size_t>(h.card)].set;
    // (a copy at grading: its words, never a grade - it has none yet; its value the raw copy's). Its form named when it isn't the one he
    // plays as; the most that fits at 30 px
    const std::string form = family_card(h.card) != family_card(c.card) ? search_name(h.card) + " " : std::string();
    Variant raw = h;
    raw.grade = 0;  // (a slab back: "SLAB 8" first - the words that matter before the cut)
    const std::string fin = h.grading ? grading_words(h) : variant_words(raw);
    const std::string what = hot_fit((!h.grading && h.grade > 0 ? "SLAB " + std::to_string(h.grade) + "  " : std::string()) + form + (st == 2 ? "EXQ " : (st == 1 && fin.find("JAM") == std::string::npos ? "JAM '96 " : "")) + fin + (h.count > 1 ? "  X" + std::to_string(h.count) : ""),
                                     fx1 - fx0 - 230);
    xrmenu::label(K, what, fx0 + 30, y, 30, h.grading ? kPink : xrmenu::kBSoft, 'l', fx1 - fx0 - 230);
    xrmenu::label(K, coins_text(price_i(h, -1)), fx1 - 30, y, 30, xrmenu::kBGold, 'r', 180);
  }
  if (mine.size() > 3) xrmenu::label(K, "+" + std::to_string(mine.size() - 3) + " MORE - SELL SHOWS THEM ALL", fcx, 706, 30, xrmenu::kBDim, 'c', fx1 - fx0 - 60);
  // his actions: BUY / SELL / SALES / WATCH (A: the row's actions, LEFT / RIGHT, A)
  const bool watched = watching(c.card);
  for (int a = 0; a < 4; ++a) {
    const float bx0 = fx0 + 30 + (a % 2) * 0.5f * (fx1 - fx0 - 50), bx1 = bx0 + 0.5f * (fx1 - fx0 - 70), by0 = a < 2 ? 744.0f : 822.0f, by1 = by0 + 66.0f;
    const std::string w = a == 3 && watched ? "WATCHING" : kHotActs[a];
    const bool can = !(a == 1 && mine.empty());
    xrmenu::button(K, bx0, by0, bx1, by1, w, g_hotAct == a ? "A" : "", g_hotAct == a, g_hotAct == a, can, 30.0f);
  }
}
}  // namespace

void set_logo_source(const uint8_t* (*logo)(int club, int* w, int* h)) { g_logoSrc = logo; }
void hot_draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t) {
  (void)t;
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  Pen P;
  P.keep = &keep;
  if (g_mk.career == 0) xrmenu::say(P.K, "LOAD A FRANCHISE CAREER", 825, 480, 64, xrmenu::kBDim, 'c', 1400);
  else draw_hot(P);
  if (g_now - g_msgAt < 6.0) xrmenu::say(P.K, g_msg, 0.5f * kW, 930, 34, g_msgRgb, 'c', kW - 60);
  for (xrtext::BoardPiece& p : P.v) {  // (as grade_draw: page units -> the hub's picture, nothing under 30 px)
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
    p.cut *= k;
    p.lean *= k;
    if (xrmenu::broadcast() && text && !p.styled && !p.art && p.y1 >= 14.0f && p.y1 < 30.0f * k) p.y1 = 30.0f * k;
    for (size_t i = 0; i < p.pts.size(); ++i) p.pts[i] = (i % 2 == 0 ? ox : oy) + p.pts[i] * k;
    out.push_back(std::move(p));
  }
}
bool hot_input(const Input& in) {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  if (g_mk.career == 0) return false;
  if (!(in.step || in.a || in.b || in.x || in.y)) return false;  // (nothing pressed: no work - this runs every game frame on the page)
  const std::vector<TopCard> top = top_list(kHotN);
  const int n = static_cast<int>(top.size());
  if (n == 0) return false;
  g_hotCur = std::max(0, std::min(n - 1, g_hotCur));
  const int face = top[static_cast<size_t>(g_hotCur)].card;
  if (g_hotAct >= 0) {  // the actions: LEFT / RIGHT / UP / DOWN pick, A does it, B back to the list
    if (in.b) {
      g_hotAct = -1;
      sound("book_tick", 0.4f);
      ++g_gen;
      return true;
    }
    if (in.step && in.dir >= 1 && in.dir <= 4) {  // (the hub's stick: 1 left, 2 right, 3 up, 4 down - the 2 x 2 buttons)
      int a = g_hotAct;
      if (in.dir == 3 && a >= 2) a -= 2;
      if (in.dir == 4 && a < 2) a += 2;
      if (in.dir == 1 && a % 2 == 1) a -= 1;
      if (in.dir == 2 && a % 2 == 0) a += 1;
      if (a != g_hotAct) g_hotAct = a, sound("book_tick", 0.4f), ++g_gen;
      return true;
    }
    if (in.a) {
      const std::string nm = search_name(face).substr(0, 16);
      const bool fam = family_forms(face).size() > 1;
      if (g_hotAct == 0) {  // BUY: MARKET > BUY searching him (his whole family: the FAMILY chip on)
        g_q = nm;
        g_fFam = fam;
        g_fSet = kFSetAll;
        g_fFin = kFFinAny;
        g_fPrice = kFPriceAny;
        g_tab = kTabBuy;
        g_kb = g_kbOpen = false;
        g_cur[kTabBuy] = g_top[kTabBuy] = 0;
        remember_query();
        rows_build();
        say(g_rows.empty() ? "NONE OF " + nm + " FOR SALE RIGHT NOW - WATCH HIM" : std::to_string(g_rows.size()) + " OF " + nm + " FOR SALE", g_rows.empty() ? 0xFFB020 : 0x39FF88);
        g_hotJump = 1;
      } else if (g_hotAct == 1) {  // SELL: MARKET > SELL showing his copies (his family)
        if (hot_yours(face).empty()) {
          say("YOU HAVE NONE OF " + nm + " TO SELL", 0xFFB020);
          sound("book_tick", 0.4f);
          ++g_gen;
          return true;
        }
        g_sq = nm;
        g_sfFam = fam;
        g_sfSet = kFSetAll, g_sfFin = kSFinAny, g_sfType = kSTypeAny, g_sfStat = kSStatAll, g_sfPrice = kSPriceAny;
        g_tab = kTabMine;
        g_kb = g_kbOpen = false;
        g_cur[kTabMine] = g_top[kTabMine] = 0;
        g_sellRowsDirty = true;
        sell_fresh();
        g_hotJump = 1;
      } else if (g_hotAct == 2) {  // SALES: his SALES
        open_sales(face, false);
        g_tab = kTabSales;
        g_kb = g_kbOpen = false;
        g_hotJump = 1;
      } else {  // WATCH: a word when one lists under the market (BUY's watch list)
        const bool now = watch_flip(face);
        say(now ? "WATCHING " + nm + " - YOU'LL HEAR WHEN ONE LISTS UNDER THE MARKET" : "NOT WATCHING " + nm + " ANY MORE", now ? 0x2EF2FF : 0x9A92B8);
        save_market();
      }
      sound("book_ding", 0.5f);
      if (g_hotJump != 0) g_hotAct = -1;
      ++g_gen;
      return true;
    }
    return true;
  }
  if (in.step && (in.dir == 3 || in.dir == 4)) {  // up / down: the next player
    const int c = std::max(0, std::min(n - 1, g_hotCur + (in.dir == 4 ? 1 : -1)));
    if (c != g_hotCur) g_hotCur = c, g_hotFin = std::min(g_hotFin, 3), sound("book_tick", 0.4f), ++g_gen;
    return true;
  }
  if (in.step && (in.dir == 1 || in.dir == 2)) {  // left / right: the chart's finish
    const int nf = static_cast<int>(hot_fins(face).size());
    if (nf > 0) g_hotFin = ((g_hotFin + (in.dir == 2 ? 1 : -1)) % nf + nf) % nf, sound("book_tick", 0.4f), ++g_gen;
    return true;
  }
  if (in.a) {  // his actions (BUY first)
    g_hotAct = 0;
    sound("book_ding", 0.4f);
    ++g_gen;
    return true;
  }
  return false;
}
void hot_hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord) {
  std::lock_guard<std::mutex> lk(g_m);
  *xWord = *yWord = "";
  *aDim = g_mk.career == 0;
  *aWord = g_hotAct < 0 ? "HIS ACTIONS" : kHotActs[std::max(0, std::min(3, g_hotAct))];
}
int hot_take_jump() {
  std::lock_guard<std::mutex> lk(g_m);
  const int j = g_hotJump;
  g_hotJump = 0;
  return j;
}
bool hot_busy() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_hotAct >= 0;
}
std::vector<HotRow> hot_rows() {
  std::lock_guard<std::mutex> lk(g_m);
  load_set();
  std::vector<HotRow> out;
  for (const TopCard& c : top_list(kHotN)) {
    HotRow r;
    r.card = c.card, r.rank = c.rank, r.prevRank = c.prevRank, r.price = c.price, r.pct = c.pct, r.why = c.why;
    r.family = hot_family_note(c.card);
    const Line* l = line_of(c.card);
    r.club = l != nullptr ? l->club : -1;
    r.yours = hot_yours(c.card);
    for (const HotFin& f : hot_fins(c.card)) {
      r.finNames.push_back(f.name);
      std::vector<long long> ch;
      for (double d : hot_chart(f.v)) ch.push_back(llr(d));
      r.charts.push_back(ch);
    }
    out.push_back(r);
  }
  return out;
}
void hot_debug(int cursor, int fin, int act) {
  std::lock_guard<std::mutex> lk(g_m);
  g_hotCur = std::max(0, cursor);
  g_hotFin = std::max(0, fin);
  g_hotAct = std::max(-1, std::min(3, act));
  ++g_gen;
}
int hot_state(int* fin, int* act) {
  std::lock_guard<std::mutex> lk(g_m);
  if (fin != nullptr) *fin = g_hotFin;
  if (act != nullptr) *act = g_hotAct;
  return g_hotCur;
}
int market_tab() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_tab;
}

}  // namespace xrmarket