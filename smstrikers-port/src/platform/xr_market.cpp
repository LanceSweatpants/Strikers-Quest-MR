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
constexpr int kTabMine = 0, kTabListed = 1, kTabMovers = 2, kTabBuy = 3, kTabOffers = 4, kTabs = 5;
int g_tab = 3;  // (r236: the page opens on BUY - kTabBuy, below)
int g_cur[kTabs] = {0, 0, 0, 0, 0}, g_top[kTabs] = {0, 0, 0, 0, 0};
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
// its family stage premium: its last form 1, a stage below it kStageStep, two below kStageStep^2 (a JAM / EXQUISITE card: its twin's)
double stage_mult(int card) {
  const CardInfo& c = g_set[static_cast<size_t>(card)];
  const CardInfo& b = c.set >= 1 && c.base >= 0 && c.base < static_cast<int>(g_set.size()) ? g_set[static_cast<size_t>(c.base)] : c;
  const int below = std::max(0, b.stages - 1 - b.stage);
  return std::pow(kStageStep, below);
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
double base_of(int card, double s, int i) { return kUnit * stage_mult(card) * std::pow(demand_at(card, i), 0.55 + 0.45 * s); }
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
double bid_ratio(long long market) {
  if (market <= 200) return kBidBulk;
  if (market >= 20000) return kBid;
  return kBidBulk + (kBid - kBidBulk) * (std::log10(static_cast<double>(market)) - std::log10(200.0)) / 2.0;
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
    if (i % 2 == 0 && !hot.empty()) c = hot[static_cast<size_t>(std::min<double>(hot.size() - 1, std::floor(u01(h ^ 1) * std::min<size_t>(12, hot.size()))))].second;
    else c = here[static_cast<size_t>(std::min<double>(here.size() - 1, std::floor(u01(h ^ 2) * here.size())))];
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
void make_offers() {
  // the open ones: a day older (a bidding war: the top bid climbs toward what they'd really pay), capped to the market now,
  // gone when they expire or you no longer have the card
  for (size_t i = 0; i < g_mk.offers.size();) {
    Offer& o = g_mk.offers[i];
    const long long p = price_i(o.v, -1);
    if (g_mk.steps > o.expires || !own_free(o.v)) {  // (r210: a copy now listed by you draws no offers)
      g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
      continue;
    }
    o.reserve = std::min(o.reserve, static_cast<long long>(std::floor(static_cast<double>(p) * kOfferMax)));
    if (o.bidders > 1 && !o.final) {
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
    bool open = false;
    for (const Offer& o : g_mk.offers) open = open || sig_of(o.v) == s;
    if (open || !own_free(h)) continue;  // (r210: not on a copy you've listed)
    const long long p = price_i(h, -1);
    if (p < 500) continue;  // (nobody chases bulk: r210's scale - r201 was 20 coins)
    const double heat = line_heat(h.card);
    // r210: demand + the market's mood count too - r231: the league's top performers draw the big collectors
    const double chance = 0.06 + 0.10 * clampd(heat, 0.0, 2.0) + (h.parallel > 0 ? 0.08 : 0.0) + (h.grade >= 9 ? 0.08 : 0.0) + (h.hit ? 0.08 : 0.0) +
                          0.04 * (tier_of(h.card) - 2) + (p >= 250000 ? 0.15 : 0.0) + 0.1 * clampd(g_mk.cycle, -0.5, 0.5);
    const double u = u01(hstr(s) ^ (static_cast<uint64_t>(g_mk.career) << 7) ^ static_cast<uint64_t>(g_mk.steps) * 0x2545F4914F6CDD1Dull);
    if (u < chance) cands.push_back({u - chance, i});
  }
  std::sort(cands.begin(), cands.end());
  for (const auto& cd : cands) {
    if (made >= 3 || static_cast<int>(g_mk.offers.size()) >= kMaxOffers) break;
    const Holding& h = g_hold[cd.second];
    const uint64_t k = hstr(sig_of(h)) ^ static_cast<uint64_t>(g_mk.steps) * 0x9E3779B97F4A7C15ull ^ g_mk.career;
    const long long p = price_i(h, -1);
    const double pd = static_cast<double>(p);
    Offer o;
    o.id = g_mk.nextId++;
    o.v = h;
    o.reserve = std::max(1LL, static_cast<long long>(std::floor(pd * std::min(kOfferMax, 0.97 + 0.11 * u01(k ^ 12)))));
    o.amount = std::min(o.reserve, std::max(1LL, static_cast<long long>(std::floor(pd * (0.86 + 0.18 * u01(k ^ 13))))));
    o.who = pick_buyer(k ^ 11, o.reserve);  // r235: (a named collector who can afford it, mostly)
    // a bidding war: a hot line, or (r210) a grail - r231: the TOP 5 of the race - the rich collectors all want it
    const bool grail = p >= 250000 || tier_of(h.card) >= 5;
    o.bidders = line_heat(h.card) > 0.8 || grail ? 2 + (u01(k ^ 14) < 0.4 ? 1 : 0) + (grail && u01(k ^ 15) < 0.5 ? 1 : 0) : 1;
    o.day = g_mk.steps;
    o.expires = g_mk.steps + 2;
    g_mk.offers.push_back(o);
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
  for (const Offer& o : g_mk.offers)
    std::fprintf(f, "O\t%u\t%s\t%s\t%lld\t%lld\t%d\t%d\t%d\t%d\n", o.id, vtext(o.v).c_str(), o.who.c_str(), o.amount, o.reserve, o.bidders, o.day, o.expires,
                 o.final ? 1 : 0);
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
      g_mk.offers.push_back(o);
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

void check_mine(int pulls);  // r210 (below: your listings, a collector buying)
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
  mag_build();  // r219: STRIKERS ILLUSTRATED's market spread for this matchday
  ++g_gen;
}

// ---------------------------------------------------------------------------------------- the actions
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
  for (size_t i = 0; i < g_mk.offers.size();)  // (its offers go with it when that was the last free copy)
    if (!own_free(g_mk.offers[i].v)) g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
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
  if (!do_sell(copy.v, copy.amount, "STRIKERZ MARKET OFFER", false, copy.who.c_str())) return false;
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
  for (size_t i = 0; i < g_mk.offers.size();)  // (offers on it go: it's listed now)
    if (!own_free(g_mk.offers[i].v)) g_mk.offers.erase(g_mk.offers.begin() + static_cast<long>(i));
    else ++i;
  tell_listed();
  ++g_gen;
  return m.id;
}
bool do_unlist(uint32_t id) {
  for (size_t i = 0; i < g_mk.mine.size(); ++i)
    if (g_mk.mine[i].id == id) {
      g_mk.mine.erase(g_mk.mine.begin() + static_cast<long>(i));
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
// what a collector looking at listing `id` on `step` (its k-th look of a several-matchday sim) would pay at most, as a share of the
// market; 0 nobody looked. Deterministic: the career, the listing, the step - never your price (changing it can't re-roll a look)
double listing_offer(uint32_t id, int step, int k, const Odds& o, bool* whale) {
  const uint64_t h = mix64(static_cast<uint64_t>(g_mk.career) * 0xD6E8FEB86659FD93ull ^ static_cast<uint64_t>(id) * 0x9E3779B97F4A7C15ull ^
                           static_cast<uint64_t>(static_cast<int64_t>(step) * 31 + k + 77));
  *whale = false;
  if (u01(h ^ 1) >= o.look) return 0.0;
  const double u1 = std::max(1e-12, u01(h ^ 2)), u2 = u01(h ^ 3);
  const double z = std::sqrt(-2.0 * std::log(u1)) * std::cos(6.283185307 * u2);
  double w = std::exp(o.mu + o.sigma * z);
  if (u01(h ^ 4) < o.whale) {
    w = std::max(w, 1.15 + 0.45 * u01(h ^ 5));  // (a rich collector who wants it now: up to 1.6x the market)
    *whale = true;
  }
  return w;
}
void check_mine(int pulls) {
  bool any = false;
  for (int k = 0; k < std::max(1, pulls); ++k) {
    for (size_t i = 0; i < g_mk.mine.size();) {
      Mine m = g_mk.mine[i];
      const long long market = price_i(m.v, -1);
      const Odds o = listing_odds(m.v, market);
      // its views + watchers: eyes on it (more for a fair price, a wanted card, a hot market)
      const double ratio = static_cast<double>(m.ask) / static_cast<double>(std::max(1LL, market));
      const uint64_t hv = mix64(static_cast<uint64_t>(m.id) * 0xC2B2AE3D27D4EB4Full ^ static_cast<uint64_t>(g_mk.steps * 8 + k));
      g_mk.mine[i].views += static_cast<int>(std::lround(o.look * 30.0 * std::min(3.0, 1.0 / (ratio * ratio)) * (0.6 + 0.8 * u01(hv))));
      g_mk.mine[i].watching = static_cast<int>(std::lround(o.look * 14.0 * clampd(1.5 - ratio, 0.0, 1.5) * (0.7 + 0.6 * u01(hv ^ 9))));
      bool whale = false;
      const double w = listing_offer(m.id, g_mk.steps, k, o, &whale);
      if (w <= 0.0 || w * static_cast<double>(market) < static_cast<double>(m.ask)) {
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
      g_flashAt = g_now;
      any = true;
    }
  }
  if (any) {
    sound("book_collect", 0.9f);
    sound("book_coin", 0.7f);
    tell_listed();
  }
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
  for (int c : a) dem[static_cast<size_t>(c)] = demand_at(c, -1) * stage_mult(c);
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
  g_lastRead = now;
  load_set();
  const unsigned career = g_src.career != nullptr ? g_src.career() : 0;
  if (career == 0 || g_set.empty()) {
    if (g_mk.career != 0) {
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
  if (g_saveDue) g_saveDue = false, changed = true;  // r235: (a recent search kept)
  if (g_rowsDirty && g_tab == kTabBuy) rows_build(), ++g_gen;  // r235: (the search's results, once after a change)
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
  return changed;
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
  auto take = [&](SearchRow& r) {
    if (r.v.card < 0 || r.v.card >= static_cast<int>(g_set.size())) return;
    if (g_fSet != kFSetAll && set_of(r.v) != g_fSet - 1) return;
    if (!finish_ok(r.v, g_fFin) || !name_match(search_name(r.v.card), q)) return;
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
// the panel's keys (page units): 32 keys in 8 x 4 (A-Z ' - SPACE DEL CLR DONE), 4 recent searches, 4 filter chips
constexpr int kKbKeys = 32, kKbRecent = 32, kKbSet = 36, kKbFin = 37, kKbPrice = 38, kKbSort = 39, kKbAll = 40;
constexpr float kKbX1 = 760.0f, kRowsX0Open = 780.0f, kRowsY0 = 300.0f, kRowH = 95.0f;
constexpr int kRowsShown = 6;
const char* const kKbLabel[kKbKeys] = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P",
                                       "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "'", "-", "SPACE", "DEL", "CLR", "DONE"};
bool kb_box(int k, float* x0, float* y0, float* x1, float* y1) {
  if (k < kKbKeys) {
    const int r = k / 8, c = k % 8;
    *x0 = 20 + c * 92.0f, *x1 = *x0 + 84, *y0 = 340 + r * 74.0f, *y1 = *y0 + 66;
    return true;
  }
  if (k < kKbSet) {
    const int i = k - kKbRecent;
    if (i >= static_cast<int>(g_recent.size())) return false;
    *x0 = 20 + i * 184.0f, *x1 = *x0 + 176, *y0 = 662, *y1 = 706;
    return true;
  }
  if (k < kKbAll) {
    const int i = k - kKbSet;
    *x0 = 20 + (i % 2) * 375.0f, *x1 = *x0 + 365, *y0 = 722 + (i / 2) * 60.0f, *y1 = *y0 + 50;
    return true;
  }
  return false;
}
int kb_hit(float x, float y) {  // the key / chip within 10 units of the ray (-1 none)
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < kKbAll; ++k) {
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
  for (int k = 0; k < kKbAll; ++k) {
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
  std::string q = g_q;
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
}
void kb_press(int k) {
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
  }
  sound("book_tick", 0.6f);
  if (changed) {
    g_cur[kTabBuy] = 0;
    g_top[kTabBuy] = 0;
    rows_build();
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
      static const int kOrder[4] = {kTabBuy, kTabMine, kTabListed, kTabOffers};
      int at = 0;
      for (int i = 0; i < 4; ++i)
        if (kOrder[i] == g_tab) at = i;
      g_tab = kOrder[(at + (in.dir == 2 ? 1 : 3)) % 4];
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
  // r206: MY CARDS' copies COUNTED here (r201-r205 sorted them by price - ~5,000 price reads - on every call, i.e. every game
  // frame on this page); the sorted list only when A picks one
  int nMine = 0;
  for (const Holding& h : g_hold) nMine += !h.grading && h.card >= 0 && h.card < static_cast<int>(g_set.size()) ? 1 : 0;
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
  if (in.x && g_tab == kTabMine && at < n) {  // r210: X: LIST FOR SALE - your price on the pad (the market's suggested)
    const Holding h = mine_sorted()[static_cast<size_t>(at)];
    if (static_cast<int>(g_mk.mine.size()) >= kMaxMine) say("20 LISTINGS AT ONCE - TAKE ONE DOWN FIRST (MY LISTINGS)", 0xFFB020);
    else if (!own_free(h) && held_out_count(h) > 0) say("IT'S IN AN OPEN COLLECTOR TRADE - ANSWER IT FIRST (CARDS > COLLECTORS)", 0xFFB020);  // r213
    else if (!own_free(h)) say("EVERY COPY OF IT IS LISTED ALREADY - MY LISTINGS", 0xFFB020);
    else pad_open_for(h, 0, round_nice(price_i(h, -1)));
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
  if (in.x || in.y) return true;  // (nothing for them here; kept off the hub's trades)
  if (in.a) {
    if (g_tab == kTabMine && at < n) {
      const Holding h = mine_sorted()[static_cast<size_t>(at)];
      if (!own_free(h)) {  // r210: a listed copy sells through its listing (or take it down first)
        say(held_out_count(h) > 0 && listed_count(h) == 0 ? "IT'S IN AN OPEN COLLECTOR TRADE - ANSWER IT FIRST (CARDS > COLLECTORS)"  // r213
                                                           : "IT'S LISTED - TAKE THE LISTING DOWN FIRST (MY LISTINGS, X)",
            0xFFB020);
        done();
        return true;
      }
      g_ask = 1;
      g_askV = h;
      g_askCoins = bid_of(price_i(g_askV, -1));
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
    } else if (click && x >= kRowsX0Open && y >= kRowsY0 && y < kRowsY0 + kRowsShown * kRowH) {  // (a result: picked, the panel closed on it)
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
  if (g_ask == 0) return false;
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
  if (g_tab == kTabMine) {
    *aWord = "SELL NOW", *aDim = g_hold.empty();
    if (!g_hold.empty()) *xWord = "LIST FOR SALE";
  } else if (g_tab == kTabListed) {
    *aWord = "EDIT PRICE", *aDim = g_mk.mine.empty();
    if (!g_mk.mine.empty()) *xWord = "TAKE DOWN";
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
  const std::vector<Holding> mine = mine_sorted();
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
    const std::string chips[4] = {kSetWord[g_fSet], kFinWord[g_fFin], kPriceWord[g_fPrice], std::string("SORT: ") + kSortWord[g_fSort]};
    float cx = 0;
    for (int i = 0; i < 4; ++i) {
      const bool set = (i == 0 && g_fSet != 0) || (i == 1 && g_fFin != 0) || (i == 2 && g_fPrice != 0) || i == 3;
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
  // the panel: the query, the letter grid, recent searches, the filters
  P.K.glow(-40, 160, kKbX1 + 40, 920, kCyan, 0.12f);
  P.K.box(0, 196, kKbX1, 892, 26, 0x0B1226, 0.98f);
  P.K.ring(0, 196, kKbX1, 892, 26, 3, kCyan);
  chrome(P, "SEARCH A PLAYER", 24, 230, 30, 500, -1);
  P.K.box(20, 254, kKbX1 - 20, 324, 18, 0x050812);
  P.K.ring(20, 254, kKbX1 - 20, 324, 18, 3, g_q.empty() ? kLine : kCyan);
  if (g_q.empty()) P.K.left("TYPE A NAME", 40, 290, 34, 500, kDim, 0.0f);
  else P.K.left(g_q, 40, 291, 44, kKbX1 - 120, 0xFFFFFF, 2.0f);
  {  // (the caret: steady - the page is drawn on change only)
    const float cw = g_q.empty() ? 0.0f : std::min(kKbX1 - 120.0f, xrtext::board_text_width(g_q, 44));
    P.K.box(46 + cw, 268, 52 + cw, 312, 2, kCyan);
  }
  for (int k = 0; k < kKbAll; ++k) {
    float x0, y0, x1, y1;
    if (!kb_box(k, &x0, &y0, &x1, &y1)) continue;
    const bool on = k == g_kbKey;
    std::string label;
    uint32_t face = 0x1C2448;
    if (k < kKbKeys) {
      label = kKbLabel[k];
      face = k == 31 ? 0x1E7A44 : (k >= 29 ? 0x2E1E4A : 0x1C2448);
    } else if (k < kKbSet) {
      label = g_recent[static_cast<size_t>(k - kKbRecent)];
      face = 0x14202E;
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
  P.K.left(g_recent.empty() && !bcast ? "RECENT SEARCHES SHOW UP HERE" : "RECENT SEARCHES", 24, 648, 16, 400, kDim, 0.0f);  // (r236: short in the hub)
  if (!bcast) P.K.left("A PRESS  -  X DEL  -  B CLOSE  -  OR POINT + TRIGGER", 24, 866, 18, kKbX1 - 48, kDim, 0.0f);  // (r236: the hub's bar says them)
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
    const std::string chips[4] = {std::string("SET: ") + (g_fSet == 0 ? "ALL" : kSetWord[g_fSet]), std::string("FINISH: ") + (g_fFin == 0 ? "ANY" : kFinWord[g_fFin]),
                                  std::string("PRICE: ") + (g_fPrice == 0 ? "ANY" : kPriceWord[g_fPrice]), std::string("SORT: ") + kSortWord[g_fSort]};
    float cx = 0;
    for (int i = 0; i < 4; ++i) {
      const bool set = (i == 0 && g_fSet != 0) || (i == 1 && g_fFin != 0) || (i == 2 && g_fPrice != 0);
      const float room = (lx1 - cx) - (3 - i) * 10.0f;
      if (room < 120) break;
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
    const Odds o = listing_odds(m.v, mk);
    const double look = o.look * (ratio <= 1.0 ? 1.0 : std::exp(-std::pow((ratio - 1.0) / 0.18, 2.0)));
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
    const char* hint = r < 0.9 ? "UNDER THE MARKET - IT SHOULD GO FAST"
                               : (r <= 1.06 ? "ABOUT THE MARKET - LIKELY TO SELL" : (r <= 1.3 ? "OVER THE MARKET - RARE, BUT IT HAPPENS" : "WAY OVER - A LONG SHOT"));
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
      static const int kOrder[4] = {kTabBuy, kTabMine, kTabListed, kTabOffers};
      static const char* const kName[4] = {"BUY", "SELL", "LISTED", "OFFERS"};
      std::vector<std::string> names, badges;
      int at = 0;
      for (int i = 0; i < 4; ++i) {
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
      if (g_tab == kTabMine) draw_mine(P, t);
      else if (g_tab == kTabListed) draw_listed(P, t);
      else if (g_tab == kTabBuy && !g_kb) draw_buy_b(P, t);
      else if (g_tab == kTabBuy) draw_buy(P, t);
      else draw_offers(P, t);
      if (g_tab != kTabBuy) {  // (r210-r235's tabs start at 214 under the old header: up under the sub-tabs - no pointer on them)
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
    const char* const kTabName[kTabs] = {"MY CARDS", "LISTINGS", "TOP CARDS", "BUY", "OFFERS"};  // (r231: MOVERS -> TOP CARDS)
    for (int i = 0; i < kTabs; ++i) {
      const float tx0 = i * 178.0f, tx1 = tx0 + 170;
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
    if (g_tab == kTabMine) draw_mine(P, t);
    else if (g_tab == kTabListed) draw_listed(P, t);
    else if (g_tab == kTabMovers) draw_movers(P, t);
    else if (g_tab == kTabBuy) draw_buy(P, t);
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
      if (wire.empty()) wire = "BUY 112%  -  SELL NOW UP TO 82%  -  LIST FOR SALE AT YOUR PRICE  -  PRICES MOVE WITH YOUR FRANCHISE, THE HYPE AND THE MARKET'S MOOD";
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
  ++g_gen;
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
void debug_pad(int stage) {
  std::lock_guard<std::mutex> lk(g_m);
  g_pad = false;
  if (stage == 1 || stage == 2) {
    g_tab = kTabMine;
    const std::vector<Holding> mine = mine_sorted();
    if (mine.empty()) return;
    const Holding h = mine[static_cast<size_t>(std::max(0, std::min(static_cast<int>(mine.size()) - 1, g_cur[kTabMine])))];
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

}  // namespace xrmarket