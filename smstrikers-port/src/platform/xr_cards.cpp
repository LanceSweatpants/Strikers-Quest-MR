// See xr_cards.h. Built with xr_quest.cpp in strikers_xr (C++20, no port prelude).
// PC check of the pictures (this file's layouts, line for line): tools\outside\cards_preview.py -> C:\StrikersMR\logs\cards137

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#define VK_USE_PLATFORM_ANDROID_KHR 1
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include "xr_cards.h"

#include "xr_big.h"
#include "xr_evo.h"  // r138: the special cards' 3D figures (their packs and standing poses)
#include "../../include/port/franchise.h"
#include "../../include/port/xr_guest.h"

#include <jni.h>                 // r141: a guest's celebration made by the converter's Java (Cel)
#include <SDL3/SDL_system.h>
#if __has_include("xr_sign.h")
#include "xr_sign.h"  // r149: the characters' autographs (a SIGNED case hit, every BLACK 1/1)
#endif
#include "xr_goalfx.h"  // r153: GOAL EXPLOSIONS (bought here with TIZM COINS: the shop's EXPLOSIONS page, its wallet)
#include "xr_pageturn.h"  // r160 round 2: STRIKERS ILLUSTRATED's page-turn rules (the 3D binder turns by them)
#include "xr_binderfx.h"  // r163: the 3D binder's living skins (halos, light, particles, fur that moves)
#include "xr_balls.h"  // r168: BALL SKINS (the LOOKS page's top row: bought here, saved in looks.txt)
#include "xr_market.h"  // r201: the TIZM CARD MARKET (its MARKET page, the binder's values)
#include "xr_museum.h"  // r201: the CARD MUSEUM (its page; its pieces' card pictures)
#include "xr_trade.h"   // r213: COLLECTOR TRADES (its page; the collectors' 1/1s the packs respect)
#include "xr_menu.h"    // r236: LOOK B (the hub's broadcast kit: CARDS > PACKS, the LOCKER pages)
#include "xr_pulls.h"   // r219: BIG PULLS (a hit's slow-mo build-up, the dark room for a 1/1, its party's tier + word)
#include "xr_exq.h"     // r230: LANCINI EXQUISITE (its tiers, runs, rates, prices, its show)
#include "xr_vault.h"   // r230: THE VAULT (its case: mesh, moves, sounds)

// stb_image (public domain / MIT), PNG (the portraits) and JPEG (r138: the evolution rooms' pictures), its functions
// private to this file.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include "third_party/stb_image.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>  // r221: the collection's background saver
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <list>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <sys/stat.h>
#include <unistd.h>

namespace xrcards {
namespace {

#define CLOG(...)                          \
  do {                                     \
    std::fprintf(stderr, "[cards] ");      \
    std::fprintf(stderr, __VA_ARGS__);     \
    std::fprintf(stderr, "\n");            \
    std::fflush(stderr);                   \
  } while (0)

std::string home_dir() {
  const char* home = std::getenv("STRIKERS_HOME");
  return home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers";
}
// (xr_quest.cpp's now_ns: the same clock as the frame's `now`)
uint64_t mono_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}
std::string upper(std::string s) {
  for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return s;
}
std::string num3(int n) {
  char b[16];
  std::snprintf(b, sizeof(b), "%03d", n);
  return b;
}
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
uint32_t mix_rgb(uint32_t a, uint32_t b, float t) {
  auto ch = [&](int s) {
    const float x = static_cast<float>((a >> s) & 255), y = static_cast<float>((b >> s) & 255);
    return static_cast<uint32_t>(std::lround(x + (y - x) * clampf(t, 0.0f, 1.0f))) << s;
  };
  return ch(16) | ch(8) | ch(0);
}
uint32_t dark(uint32_t c, float t = 0.5f) { return mix_rgb(c, 0x000000, t); }
uint32_t light(uint32_t c, float t = 0.3f) { return mix_rgb(c, 0xFFFFFF, t); }
float luma(uint32_t c) { return (((c >> 16) & 255) * 0.3f + ((c >> 8) & 255) * 0.59f + (c & 255) * 0.11f) / 255.0f; }

// splitmix64: the packs' dice
uint64_t mix64(uint64_t& s) {
  uint64_t z = (s += 0x9E3779B97F4A7C15ull);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
double rand01(uint64_t& s) { return static_cast<double>(mix64(s) >> 11) * (1.0 / 9007199254740992.0); }

// ------------------------------------------------------------------------------------------------- the set
// LANCINI TIZM (Lance: "the brand of the cards basically, to be "Lancini Tizm" similar to the Panini Prizm!"; our own
// parody brand, our own drawing): 2026-27 LANCINI TIZM STRIKERS. Two things make a card rare:
//  - who it is: common / rare / super rare by kind and franchise rating (cardset.txt); slots 1-4 common 74% / rare 22% /
//    super rare 4%, slot 5 rare or better (rare 82% / super rare 18%): a super rare about every 3 packs
//  - its finish, any card (the parallels): BASE chrome; SILVER TIZM refractor 1 in 50 cards; GOLD TIZM numbered /10, 1 in
//    250 (ten of each card at most, each its own number); BLACK TIZM 1/1, 1 in 2,500 = 1 in 500 packs (one of each card,
//    ever); and a Pokemon card shiny on top, 1 in 512 (its shiny model; r211: the packs' own odds - franchise.cpp's
//    kShinyOdds, an evolution's luck, stays its own)
//  r211 (Lance 11-10, his new pull rates; were 1 in 8 / 50 / 300, shiny 1 in 128): every card still its own roll (pure odds -
//    a 1/1 can come in any pack)
// tools\outside\cards_preview.py simulates 10,000 packs of these; tools\outside\odds211 1,000,000 with this file's own code.
constexpr double kSlotSR = 0.04, kSlotR = 0.22, kLastSR = 0.18;
constexpr int kShinyOdds = 512;
constexpr int kSilverOdds = 50, kGoldOdds = 250, kBlackOdds = 2500;
constexpr int kGoldRun = 10;  // GOLD TIZM: numbered /10
constexpr int kPackCards = 5;
constexpr int kSheets = 2 * kPackCards;  // r146: the cards in your room at once (a trade-in's ten)
constexpr int kArt = 1024;  // the big picture's size (xr_big)

enum { kCommon, kRare, kSuper };
enum { kBase, kSilver, kGoldTizm, kBlack };
const char* const kRarityName[3] = {"COMMON", "RARE", "SUPER RARE"};
const char* const kRarityShort[3] = {"C", "R", "SR"};
const char* const kParallelName[4] = {"BASE", "SILVER TIZM", "GOLD TIZM", "BLACK TIZM"};
const char* const kSeriesName[6] = {"STRIKERS", "CHARGED", "MELEE", "POKEMON", "MARVEL", "ORIGINALS"};  // r183: ORIGINALS
constexpr uint32_t kSeriesRgb[6] = {0xE4202A, 0x23A04A, 0x4062C8, 0x2A5AB8, 0xB0101E, 0x0E7C86};
constexpr uint32_t kSeriesInk[6] = {0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFCB05, 0xFFFFFF, 0xFFFFFF};
constexpr uint32_t kGold = 0xFFD23F, kNavy = 0x0B1022;
constexpr const char* kSetName = "2026-27 LANCINI TIZM STRIKERS";

struct Card {
  int num = 0;
  std::string key, stat, name;
  int rarity = kCommon, series = 0, rating = 75;
  float stats[4] = {0.5f, 0.5f, 0.5f, 0.5f};
  uint32_t c1 = 0x31406A, c2 = kGold;
  int etch = -1;    // its outline's cell in guests/etch.r8 (a guest: its first form's roster place), -1 none
  int klass = -1;   // a Strikers character's class (its motif), -1 a guest
  int roster = -1;  // its roster place on this headset (its voice), -1 none
  bool here = false;  // on this headset: packs can give it
  // r152 (Lance, after Thor IV turned out to be Beta Ray Bill): a Marvel costume's own name from Marvel: Ultimate Alliance
  // (franchise/costume_names.txt) - `name` then its title ("Beta Ray Bill"; a look of the hero's, "Ultimate Thor"), `sub`
  // the hero under it ("Thor"), `signAs` who signs it (Beta Ray Bill himself; Thor in his Ultimate look); "" none
  std::string sub, signAs;
  // r217 (Lance 11-11: "go with A but also go with B!!! we will add B as a new pack!!!"): which set it's in - 0 LANCINI TIZM (the
  // main set, "PRIZM CHROME"), 1 LANCINI TIZM JAM '96 (the premium pack: every character its own Jam card, after the main set in
  // g_set); `id` its own name in saves / the market / trades / the museum (a main card: its key; a Jam card: key + kJamSuffix) -
  // `key` stays the character's (its pictures, model, autograph, home); `twin` the other set's card of the same character
  int set = 0, twin = -1;
  std::string id;
  // r230: set 2 = LANCINI EXQUISITE (key + "_exq", its checklist's characters only: `num` its number on the checklist, `twin` its LANCINI
  // TIZM card); a LANCINI TIZM card's `exq` its Exquisite card (-1 not on the checklist)
  int exq = -1;
};
constexpr const char* kJamSuffix = "_j96";
constexpr unsigned kJamPack = 64;  // r217: a pack's flags - a JAM '96 pack (its cards Jam cards; none: a LANCINI TIZM pack)
constexpr const char* kJamSetName = "2026-27 LANCINI TIZM JAM '96";
int g_setN = 0;  // r217: the cards in each set (the main set's count; the Jam set the same) - a card's "NNN/total"
// r230: a LANCINI EXQUISITE pack (its 3 cards Exquisite cards: shop only), one of a VAULT case of five (kExqBox; its place in the case in
// bits 9-11: 0 the case's first)
constexpr unsigned kExqPack = 128, kExqBox = 256;
constexpr int kExqBoxShift = 9;
int g_exqN = 0;  // r230: the LANCINI EXQUISITE checklist's count (150)
// A card in a pack / out of the binder: which (its index in the set), its finish, shiny, its number in its run (GOLD
// 1-10, BLACK 1).
// r149 (Lance's CASE HITS, rare inserts on top of the finishes, their own odds): TIZM BOOM (a comic-book explosion popping
// out of the card), HOMETOWN (the card a window into their home turf), SIGNED (their autograph, writing itself on);
// never traded in. (Every BLACK 1/1 is signed too: its own, not a case hit.)
enum { kHitNone = 0, kHitBoom, kHitHome, kHitSigned };
constexpr int kHitIn[4] = {0, 1500, 1200, 2000};  // r211: 1 in N cards (TIZM BOOM, HOMETOWN, SIGNED; were 300 / 350 / 450)
constexpr double kHitOdds[4] = {0.0, 1.0 / kHitIn[1], 1.0 / kHitIn[2], 1.0 / kHitIn[3]};  // a card's chance of each
const char* const kHitName[4] = {"", "TIZM BOOM", "HOMETOWN", "SIGNED"};
struct Ref {
  int card = -1;
  int parallel = kBase;
  bool shiny = false;
  int serial = 0;
  int hit = kHitNone;  // r149
};
struct Pack {
  int serial = 0;
  unsigned flags = 0;  // what earned it (franchise.h PortFranchiseTakeCardPacks)
  Ref cards[kPackCards];
};
struct Pull {
  long long when = 0;
  Ref ref;
};
// What you own of a card: copies by finish and shiny; the GOLD copies' numbers. r149: its case hits, each copy whole (its
// finish, shiny, number, kind - not in n: never a trade-in's spare)
// r152: its copies sealed in TIZM GRADING slabs (g_slabs: at grading or back) - still yours, still its level's, never a
// trade-in's spare; its best grade back (0 none) and how many are at grading
struct Owned {
  int n[4][2] = {};
  std::vector<int> golds;
  std::vector<Ref> hits;
  std::vector<Ref> sealed;  // r152
  int bestGrade = 0, grading = 0, slabsBack = 0;
  Ref bestSlab;  // (its best slab back: its copy, its cert)
  uint32_t bestCert = 0;
  int total() const {
    int t = static_cast<int>(hits.size() + sealed.size());
    for (const auto& p : n) t += p[0] + p[1];
    return t;
  }
  int free_total() const { return total() - static_cast<int>(sealed.size()); }  // r152: not sealed
  int best() const {  // the finest finish you have, -1 none
    for (int p = kBlack; p >= kBase; --p) {
      if (n[p][0] + n[p][1] > 0) return p;
      for (const Ref& h : hits)
        if (h.parallel == p) return p;
      for (const Ref& h : sealed)
        if (h.parallel == p) return p;
    }
    return -1;
  }
  int free_best() const {  // r152: the finest finish of the copies not sealed, -1 none
    for (int p = kBlack; p >= kBase; --p) {
      if (n[p][0] + n[p][1] > 0) return p;
      for (const Ref& h : hits)
        if (h.parallel == p) return p;
    }
    return -1;
  }
  bool shinyAny() const {
    for (const auto& p : n)
      if (p[1] > 0) return true;
    for (const Ref& h : hits)
      if (h.shiny) return true;
    for (const Ref& h : sealed)
      if (h.shiny) return true;
    return false;
  }
  bool blackAny() const { return best() == kBlack; }
  int hitBest() const {  // r149: its case hit to show (BOOM, then HOMETOWN, then SIGNED), 0 none
    int b = kHitNone;
    for (const Ref& h : hits)
      if (b == kHitNone || h.hit < b) b = h.hit;
    for (const Ref& h : sealed)
      if (h.hit != kHitNone && (b == kHitNone || h.hit < b)) b = h.hit;
    return b;
  }
};

std::mutex g_mutex;  // everything below (the drawing threads read it too)
std::vector<Card> g_set;
std::unordered_map<std::string, int> g_byKey;
std::atomic<bool> g_loaded{false};
std::vector<Owned> g_owned;  // per card
std::vector<Pack> g_packs;   // waiting
std::vector<Pull> g_pulls;   // big pulls, newest last
uint64_t g_salt = 0;
int g_serial = 0, g_opened = 0;
std::atomic<int> g_gen{1};
std::atomic<int> g_packCount{0};
// r140: TIZM COINS (franchise.cpp: from the matches you play; only to buy packs here): the balance, all ever earned / spent,
// the last match's (how many, when, why: "3 goals 18, 2 assists 8, ..."); the CARDS shop (shown instead of the binder's
// pages; 0 a pack, 1 a box of five); its prices
// r208 (Lance: 1,000,000 WAGERS): the balance + its totals 64-bit, every change through coins_add / coins_take (held at xrcoin::kMax)
long long g_coins = 0, g_coinsEarned = 0, g_coinsSpent = 0;
int g_lastCoins = 0;
long long g_lastCoinsWhen = 0;
std::string g_lastCoinsWhy;
std::atomic<int> g_shopPick{0};
std::atomic<bool> g_packsOnOpen{true};  // r236: CARDS > PACKS - the ring on the hero's OPEN (else on a product)
std::atomic<int> g_openSet{0};  // r217: the pack being opened's set (the hub's OPENING A ... PACK)
std::string g_shopSay;  // (the shop's last word: "NOT ENOUGH COINS" ...)
constexpr int kPackPrice = 50, kBoxPrice = 200, kBoxPacks = 5;
// r217: a JAM '96 pack / box of five (PACKS-AB.md: 5x - its cards price ~3-9x their LANCINI TIZM twins on the market; the median
// pack still under its price sold at once, as the main pack's)
constexpr int kJamPackPrice = 250, kJamBoxPrice = 1000;
constexpr unsigned kBought = 8;  // (a pack's flags: bought in the shop)
// r141: the BLACK 1/1s whose life-size cameo you've seen (it plays the first time each is turned over, never again)
std::unordered_set<std::string> g_cameoSeen;

// r146 (Lance: "lets look into a trade system for common duplicates, as well as bonuses for duplicates should you not trade
// em in as well as in game multipliers due to set bonuses orrrr just having duplicates"; his picks):
//  - TRADE-INS (the CARDS tab, past the shop): every kTradeFor spare BASE cards (r147: any rarity - Lance meant BASE, not
//    common - commons go first) = 1 free LANCINI TIZM pack (rolled and
//    saved the moment it's traded, the same odds, waiting with the rest). Only spare copies (a card's last copy stays),
//    never a SILVER / GOLD / BLACK or a shiny copy; the most duplicated go first, X keeps a card out of it
//  - CARD LEVELS: the copies you keep level a card (kLevelCopies: x2 LV2, x3 LV3, x5 LV4, x10 LV5; stars on its front, its
//    pocket); in FRANCHISE that character gets +kLevelXp% XP a level over 1 (so it evolves sooner)
//  - SETS (franchise): an EVOLUTION LINE all yours = +kLineXp% XP for its characters and their evolutions shiny 1 in
//    kLineShinyOdds; a Pokemon TYPE all yours = +kTypeXp% XP for its Pokemon; a full BINDER PAGE = +kPageCoins% TIZM COINS
//    (at most kPageCoinsCap). A character's XP bonuses together at most kXpCap. franchise.cpp applies them (a career of
//    this headset's own: never co-op, an online league or a quick match). Trading online: next round.
constexpr int kTradeFor = 10;
constexpr unsigned kTraded = 16;  // (a pack's flags: traded for)
// r226 (Lance 11-12: his 97 JAM '96 cards, nothing to trade in): JAM '96 spares trade in too - kJamTradeFor JAM '96 BASE spares = 1 JAM '96
// pack (its own trade: never mixed with LANCINI TIZM spares). The same count as the main set's: measured (logs\r226-cards\trade, the
// shipping market): a JAM '96 spare traded in sells at once for 3.3x a LANCINI TIZM one (15.4 / 4.6 coins) and a JAM '96 pack's median
// sold at once is 3.3x a LANCINI TIZM pack's (102 / 31) - pack median / its 10 spares' median: JAM '96 0.73 (100 cards) - 1.16 (300 cards),
// LANCINI TIZM 0.72 - 1.15: the same deal, so a Jam trade-in prints no more than the main set's always has
constexpr int kJamTradeFor = 10;
constexpr int trade_for(int set) { return set == 1 ? kJamTradeFor : kTradeFor; }
int g_tradeSet = 0;          // r226: the TRADE-INS page's set (0 LANCINI TIZM, 1 JAM '96)
bool g_tradeOnSets = false;  // r226: the cursor on the page's two set panels (stick up from the cards' top row)
constexpr int kLevelCopies[5] = {1, 2, 3, 5, 10};
constexpr int kLevelXp = 10, kLineXp = 15, kTypeXp = 10, kXpCap = 60, kLineShinyOdds = 96, kPageCoins = 2, kPageCoinsCap = 20;
// the CARDS tab's pages (LT: the next, RT: back): the binder, the shop, r153: GOAL EXPLOSIONS (the shop's other shelf),
// the trade-ins, the sets, the grading
enum { kPageBinder = 0, kPageShop, kPageTrade, kPageSets, kPageGrading, kPageExplosions, kPageLooks, kPageMarket, kPageMuseum, kPageCollectors,
       kPageTakeovers, kPageCovers };  // (r201: + MARKET, MUSEUM; r213: + COLLECTORS; r236: + TAKEOVERS, COVERS - LOCKER's; LOOKS = its BALLS)
// r157 (menu overhaul): the pages are a rail on the hub's CARDS section now (no LT / RT chain): its order and names
// r160: + LOOKS after EXPLOSIONS (binder skins + card sleeves)
// r213: + COLLECTORS under MARKET (COLLECTOR TRADES, xr_trade.cpp: not "TRADES" - the club's player trades - nor "TRADE-INS")
// r236 (LOOK B, the sitemap): CARDS = PACKS (the shop + opening), BINDER (SETS inside it: Y), MARKET, COLLECTORS, GRADING, TRADE-INS,
// MUSEUM; LOCKER = BALLS (LOOKS' ball shelf), GOAL FX (EXPLOSIONS without the takeovers), TAKEOVERS, BINDERS (LOOKS' binder covers)
constexpr int kRailOrder[11] = {kPageShop, kPageBinder, kPageMarket, kPageCollectors, kPageGrading, kPageTrade, kPageMuseum,
                                kPageLooks, kPageExplosions, kPageTakeovers, kPageCovers};
constexpr const char* kRailNames[11] = {"PACKS", "BINDER", "MARKET", "COLLECTORS", "GRADING", "TRADE-INS", "MUSEUM",
                                        "BALLS", "GOAL FX", "TAKEOVERS", "BINDERS"};
static_assert(sizeof(kRailOrder) / sizeof(kRailOrder[0]) == kRailPages, "the rail's pages (xr_cards.h kRailPages)");
// r157: where the hub draws the cards' pieces (x' = ox + x * k ...): its page area right of the rail
std::atomic<float> g_fitOx{0.0f}, g_fitOy{0.0f}, g_fitK{1.0f};
// r153: the EXPLOSIONS page: its cursor, a buy armed (X again on the same one buys it: which, when), a preview of ours
// going off (B / leaving stops it), its last word
int g_fxCursor = 0, g_fxArmed = -1;
int g_fxTop = 0;  // r161: EXPLOSIONS' first row in view
uint64_t g_fxArmedNs = 0;
bool g_fxPreviewing = false;
std::string g_fxSay;
constexpr int kFxCols = 4;
constexpr int kFxRowsShown = 4;  // r161: rows of EXPLOSIONS in view at once (they scroll)
// r236 (LOCKER, LOOK B): its four pages are 3 x 2 grids (scrolling a row at a time) with the focused item's panel at the right;
// GOAL FX / TAKEOVERS each keep the ring's place in their own list (catalogue indices; GOAL FX's last: NO EXPLOSION, -1)
constexpr int kLkGridCols = 3, kLkGridRows = 2;
int g_fxPos[2] = {0, 0};
std::vector<int> fx_list(bool takeovers) {
  std::vector<int> out;
  const std::vector<xrgoalfx::Item>& cat = xrgoalfx::catalogue();
  for (size_t i = 0; i < cat.size(); ++i)
    if ((cat[i].seconds > 5.0f) == takeovers) out.push_back(static_cast<int>(i));
  if (!takeovers) out.push_back(-1);
  return out;
}
std::atomic<int> g_page{kPageBinder};

// r237 THE HUB WATCHDOG (Lance's r236 freeze: the game stopped on CARDS > MARKET > BUY and the log just ended - nothing said where). The
// hub's work on the game thread (hub_input, poll: the market / museum / collectors' frames, a career's load) and its picture on the drawing
// thread (hub_page) run inside a WdScope naming the step and the page. A step that takes 250 ms or more says so when it ends ("took N ms in
// MARKET input, page MARKET"); one still running after 250 ms - a hang that never ends - is told by a small thread that looks every 0.1 s
// ("STILL BUSY after 250 ms in MARKET picture"), again every 5 s while it stays stuck. Only the innermost slow step speaks. Nothing waits on it.
constexpr uint64_t kWdMs = 250;
struct WdSlot {
  std::atomic<uint64_t> since{0};          // its outermost step's start (0: free)
  std::atomic<const char*> what{nullptr};  // the step it is in now (the innermost)
  std::atomic<const char*> thread{nullptr};
  std::atomic<int> page{-1};
  std::atomic<uint64_t> told{0};  // the start last told about
  std::atomic<int> tells{0};
};
WdSlot g_wd[6];
thread_local int t_wdSlot = -1, t_wdDepth = 0;
thread_local bool t_wdSaid = false;
const char* wd_page_name(int p) {
  static const char* const kName[12] = {"BINDER", "PACKS", "TRADE-INS", "SETS", "GRADING", "GOAL FX", "BALLS", "MARKET", "MUSEUM", "COLLECTORS", "TAKEOVERS", "BINDERS"};
  return p >= 0 && p < 12 ? kName[p] : "?";
}
void wd_thread_start() {
  static std::atomic<bool> s_on{false};
  if (s_on.exchange(true)) return;
  std::thread([]() {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      const uint64_t now = mono_ns();
      for (WdSlot& s : g_wd) {
        const uint64_t t0 = s.since.load();
        if (t0 == 0 || now <= t0) continue;
        const uint64_t ms = (now - t0) / 1000000ull;
        if (ms < kWdMs) continue;
        const int n = s.told.load() == t0 ? s.tells.load() : 0;
        if (n > 0 && ms < 5000ull * static_cast<uint64_t>(n)) continue;
        s.told = t0;
        s.tells = n + 1;
        const char* w = s.what.load();
        const char* th = s.thread.load();
        CLOG("WATCHDOG (r237): the hub's %s is STILL BUSY after %llu ms - in %s, page %s%s", th != nullptr ? th : "?", static_cast<unsigned long long>(ms),
             w != nullptr ? w : "?", wd_page_name(s.page.load()), n > 0 ? " (STUCK - a hang)" : "");
      }
    }
  }).detach();
}
class WdScope {
 public:
  WdScope(const char* thread, const char* what) : thread_(thread), what_(what), t0_(mono_ns()) {
    wd_thread_start();
    if (t_wdDepth++ == 0) {
      t_wdSaid = false;
      for (int i = 0; i < 6 && t_wdSlot < 0; ++i) {
        uint64_t free = 0;
        if (g_wd[i].since.compare_exchange_strong(free, t0_)) t_wdSlot = i;
      }
      if (t_wdSlot >= 0) g_wd[t_wdSlot].thread = thread;
    }
    if (t_wdSlot >= 0) {
      prev_ = g_wd[t_wdSlot].what.exchange(what);
      g_wd[t_wdSlot].page = g_page.load();
    }
  }
  ~WdScope() {
    const uint64_t ms = (mono_ns() - t0_) / 1000000ull;
    if (ms >= kWdMs && !t_wdSaid) {
      t_wdSaid = true;  // (the innermost slow step speaks; the steps round it stay quiet)
      CLOG("WATCHDOG (r237): the hub's %s took %llu ms in %s, page %s", thread_, static_cast<unsigned long long>(ms), what_, wd_page_name(g_page.load()));
    }
    if (t_wdSlot >= 0) g_wd[t_wdSlot].what = prev_;
    if (--t_wdDepth == 0 && t_wdSlot >= 0) {
      g_wd[t_wdSlot].since = 0;
      t_wdSlot = -1;
    }
  }
  WdScope(const WdScope&) = delete;
  WdScope& operator=(const WdScope&) = delete;

 private:
  const char* thread_;
  const char* what_;
  const char* prev_ = nullptr;
  uint64_t t0_;
};

std::unordered_set<std::string> g_keep;  // cards you keep out of trade-ins (X)
// r149 (Lance: it "took me farrrr too long to find that damn card i just got"): the cards packs gave you that you haven't
// looked at in the binder yet (the cursor resting on its pocket, or taking it out): NEW on their pockets
std::unordered_set<std::string> g_newCards;
// r149: GOD PACKs opened (every card SILVER or better / a case hit), the last one's time (STRIKERS ILLUSTRATED brags)
int g_godPacks = 0;
long long g_godWhen = 0;
// r152 (Lance: "now get on the graded slabs system"): TIZM GRADING - a copy of a card you own sent off for TIZM COINS
// (grade_cost), back after kGradeMatches franchise matches you play, sealed in a clear slab with its grade 1 - 10 (rolled
// as it's sent, kGradeOdds; seen when it's back) and its own cert number. A slab's copy stays yours (its card's level
// counts it; never a trade-in's spare; trade-ready: card + finish + grade + cert in collection.txt). Up to kDisplaySlabs
// stand on the display in your room (you pick them on the GRADING page; none picked: your best).
struct Slab {
  Ref ref;           // the copy sealed in it
  int grade = 0;     // 1 - 10
  uint32_t cert = 0;
  int left = 0;      // franchise matches to play before it's back (0: back)
  bool seen = false; // its BACK FROM GRADING moment had
  bool show = false; // picked for your room's display
  long long when = 0;
};
std::vector<Slab> g_slabs;
int g_gradeSent = 0;  // (slabs ever sent: each one's rolls)
// the GRADING page: its cursor (your slabs first, then the cards you can send), its first card row and first slab shown,
// a send armed (A again on the same card sends it: the card, when), its last word
int g_gradeCursor = 0, g_gradeTop = 0, g_gradeLeft = 0, g_gradeArmed = -1;
uint64_t g_gradeArmedNs = 0;
std::string g_gradeSay;
constexpr int kGSlabCols = 7, kGCols = 7, kGRows = 2;  // r157 round 2: 7 a row in the narrower page (r152: 8)
std::vector<int> grading_slab_order();  // (below: g_slabs' places, at grading first, then the best)
std::vector<int> grading_tiles();       // (below: the cards with a copy to send, the finest first)
bool grade_pick(int card, Ref* out);
constexpr int kGradeMatches = 2, kDisplaySlabs = 6;
// the grades' odds in thousandths (10 GEM MINT 6%, 9 MINT 24%, 8 NM-MT 34%, 7 NM 20%, 6 and lower 16%: tools/outside/cards152.py)
constexpr int kGradeOdds[11] = {0, 4, 6, 10, 20, 40, 80, 200, 340, 240, 60};
const char* const kGradeName[11] = {"", "POOR", "GOOD", "VG", "VG-EX", "EX", "EX-MT", "NM", "NM-MT", "MINT", "GEM MINT"};
int grade_cost(const Ref& r) {  // its TIZM COINS
  if (r.serial >= 1000 && r.serial < 11000) return xrexq::tier_of(r.serial) >= xrexq::kGoldLeaf ? 600 : 300;  // r230: an EXQUISITE copy (numbered)
  if (r.parallel == kBlack || r.hit != kHitNone) return 300;
  if (r.parallel == kGoldTizm) return 150;
  if (r.parallel == kSilver || r.shiny) return 100;
  return 75;
}
// its slabs (at grading and back) into its card's Owned: sealed copies, best grade, at grading (g_mutex held)
int pull_score(const Ref& r);
void slabs_sync() {
  for (Owned& o : g_owned) {
    o.sealed.clear();
    o.bestGrade = o.grading = o.slabsBack = 0;
    o.bestCert = 0;
  }
  for (const Slab& s : g_slabs) {
    if (s.ref.card < 0 || s.ref.card >= static_cast<int>(g_owned.size())) continue;
    Owned& o = g_owned[s.ref.card];
    o.sealed.push_back(s.ref);
    if (s.left > 0) {
      ++o.grading;
    } else {
      ++o.slabsBack;
      if (s.grade > o.bestGrade || (s.grade == o.bestGrade && pull_score(s.ref) > pull_score(o.bestSlab))) {
        o.bestGrade = s.grade;
        o.bestSlab = s.ref;
        o.bestCert = s.cert;
      }
    }
  }
}
struct Trade {
  long long when = 0;
  int pack = 0;      // the pack's serial
  std::string what;  // "pk_pidgey*3,pk_rattata*2,..."
};
std::vector<Trade> g_trades;  // the newest 40
int g_tradedCards = 0, g_tradedPacks = 0;
int g_tradeCursor = 0, g_tradeTop = 0, g_setsTop = 0;  // (the TRADE-INS page's cursor / first row; the SETS page's first line)
std::string g_tradeSay;

// The Strikers characters: key, class (eCharacterClass: their motif, xr_fx_mesh.frag)
struct StrikersInfo {
  const char* key;
  int klass;
};
const StrikersInfo kStrikers[12] = {{"strikers_birdo", 0},  {"strikers_daisy", 1},  {"strikers_donkeykong", 2},
                                    {"strikers_hammerbro", 3}, {"strikers_koopa", 4}, {"strikers_luigi", 5},
                                    {"strikers_mario", 6},  {"strikers_peach", 7},  {"strikers_toad", 8},
                                    {"strikers_waluigi", 9}, {"strikers_wario", 10}, {"strikers_yoshi", 11}};

int series_of(const std::string& s) {
  for (int i = 0; i < 6; ++i)
    if (s == kSeriesName[i]) return i;
  return 0;
}
bool pk_key(const std::string& key) { return key.compare(0, 3, "pk_") == 0; }
bool owns(int card) { return card >= 0 && card < static_cast<int>(g_owned.size()) && g_owned[card].total() > 0; }  // (g_mutex held)

// The set (STRIKERS_HOME/franchise/cardset.txt), with the franchise ratings and stat cards, the guests' colours and
// outlines, and what this headset has. Game thread, once.
void load_set() {
  if (g_loaded.load()) return;
  const std::string fr = home_dir() + "/franchise/";
  std::unordered_map<std::string, int> rating;
  if (FILE* f = std::fopen((fr + "ratings.txt").c_str(), "r")) {
    char key[128];
    int ovr = 0;
    while (std::fscanf(f, "%127s %d", key, &ovr) == 2) rating[key] = ovr;
    std::fclose(f);
  }
  std::unordered_map<std::string, std::array<float, 4>> stats;
  if (FILE* f = std::fopen((fr + "cards.txt").c_str(), "r")) {
    char line[256], key[128];
    float a, b, c, d;
    while (std::fgets(line, sizeof(line), f) != nullptr)
      if (line[0] != '#' && std::sscanf(line, "%127s %f %f %f %f", key, &a, &b, &c, &d) == 5) stats[key] = {a, b, c, d};
    std::fclose(f);
  }
  // r152: the Marvel costumes' own names (key, name, hero, kind: char / outfit; a base key keeps the hero's name)
  struct Costume {
    std::string name, hero;
    bool own = false;
  };
  std::unordered_map<std::string, Costume> costumes;
  if (FILE* f = std::fopen((fr + "costume_names.txt").c_str(), "r")) {
    char line[512];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      if (line[0] == '#') continue;
      std::vector<std::string> p(1);
      for (const char* s = line; *s != '\0' && *s != '\n' && *s != '\r'; ++s) {
        if (*s == '\t') p.emplace_back();
        else p.back() += *s;
      }
      if (p.size() < 3 || (p[0].find("_c") == std::string::npos && !PortOriginalUniform(p[0].c_str()))) continue;  // r183: + uniforms
      costumes[p[0]] = Costume{p[1], p[2], p.size() > 3 && p[3] == "char"};
    }
    std::fclose(f);
  }
  std::vector<Card> set;
  if (FILE* f = std::fopen((fr + "cardset.txt").c_str(), "r")) {
    char line[512];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      if (line[0] == '#' || line[0] == '\n') continue;
      std::vector<std::string> p;
      std::string cur;
      for (const char* s = line; *s != '\0' && *s != '\n' && *s != '\r'; ++s) {
        if (*s == '\t') {
          p.push_back(cur);
          cur.clear();
        } else {
          cur += *s;
        }
      }
      p.push_back(cur);
      if (p.size() < 6) continue;
      Card c;
      c.num = std::atoi(p[0].c_str());
      c.key = p[1];
      c.rarity = p[2] == "SR" ? kSuper : (p[2] == "R" ? kRare : kCommon);
      c.series = series_of(p[3]);
      c.stat = p[4];
      c.name = p[5];
      c.signAs = c.name;
      {
        auto cm = costumes.find(c.key);
        if (cm != costumes.end() && !cm->second.name.empty()) {  // r152: its costume's own name, its hero under it
          const Costume& k = cm->second;
          const bool own = k.own || k.name.find(k.hero) != std::string::npos;  // ("Ronin Unmasked": its hero in it already)
          c.name = own ? k.name : k.name + " " + k.hero;
          c.sub = k.hero;
          c.signAs = own ? k.name : k.hero;
        }
      }
      auto r = rating.find(c.stat);
      c.rating = r != rating.end() ? r->second : 75;
      auto st = stats.find(c.key);
      if (st != stats.end())
        for (int i = 0; i < 4; ++i) c.stats[i] = st->second[i];
      for (const StrikersInfo& s : kStrikers)
        if (c.key == s.key) c.klass = s.klass;
      if (c.klass >= 0) {
        c.here = true;
        uint32_t m = 0, a = 0;
        xrfx::team_colours(c.klass, &m, &a);
        c.c1 = m;
        c.c2 = a;
      } else {
        int idx = PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, c.key.c_str());
        if (idx < 0) idx = PortGuestRosterFindAny(PORT_GUEST_SIDEKICK, c.key.c_str());
        c.here = idx >= 0;
        c.roster = idx;
        if (idx >= 0) {
          unsigned m = 0, t = 0, pt = 0;
          if (PortGuestThemeColors(idx, &m, &t, &pt)) {
            c.c1 = m;
            c.c2 = luma(m) > 0.67f ? t : pt;
            if (luma(c.c2) > 0.92f && luma(t) < 0.92f) c.c2 = t;
          } else {
            c.c1 = kSeriesRgb[c.series];
          }
          // its outline: its first form's cell (an evolved form / a costume has none of its own)
          int e = idx;
          for (int guard = 0; guard < 4 && e >= 0 && PortGuestRosterEvolved(e); ++guard)
            e = PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, PortGuestRosterEvolvesFrom(e));
          c.etch = e >= 0 && e < xrfx::kEtchCells ? e : -1;
        } else {
          c.c1 = kSeriesRgb[c.series];
        }
      }
      set.push_back(c);
    }
    std::fclose(f);
  }
  // r217: the JAM '96 set - every card again, its own (after the main set: the main cards' indexes never move)
  const size_t mainN = set.size();
  for (size_t i = 0; i < mainN; ++i) {
    set[i].id = set[i].key;
    set[i].twin = static_cast<int>(mainN + i);
  }
  for (size_t i = 0; i < mainN; ++i) {
    Card j = set[i];
    j.set = 1;
    j.id = set[i].key + kJamSuffix;
    j.twin = static_cast<int>(i);
    set.push_back(j);
  }
  // r230: the LANCINI EXQUISITE set - its checklist's characters (STRIKERS_HOME/franchise/exquisite.txt: its number, the card key), after
  // the Jam set (no index of either set moves); a key not in the main set is left out (logged)
  int exqN = 0;
  if (FILE* f = std::fopen((fr + "exquisite.txt").c_str(), "r")) {
    char line[256], key[128];
    int no = 0;
    std::unordered_map<std::string, size_t> mainAt;
    for (size_t i = 0; i < mainN; ++i) mainAt[set[i].key] = i;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      if (line[0] == '#' || std::sscanf(line, "%d %127s", &no, key) != 2) continue;
      auto it = mainAt.find(key);
      if (it == mainAt.end()) {
        CLOG("exquisite.txt: %s isn't in the set (left out)", key);
        continue;
      }
      Card x = set[it->second];
      x.set = 2;
      x.num = no;
      x.id = std::string(key) + xrexq::kSuffix;
      x.twin = static_cast<int>(it->second);
      set[it->second].exq = static_cast<int>(set.size());
      set.push_back(x);
      ++exqN;
    }
    std::fclose(f);
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_set = std::move(set);
  g_setN = static_cast<int>(mainN);
  g_exqN = exqN;  // r230
  g_byKey.clear();
  for (size_t i = 0; i < g_set.size(); ++i) g_byKey[g_set[i].id] = static_cast<int>(i);  // (r217: by its id - a Jam card its own)
  g_owned.assign(g_set.size(), Owned{});
  int here = 0;
  for (const Card& c : g_set) here += c.here ? 1 : 0;
  CLOG("the set: %d cards, %d on this headset (r230: %d LANCINI EXQUISITE)", static_cast<int>(g_set.size()), here, g_exqN);
  g_loaded = true;
}

// ------------------------------------------------------------------------------------------ the collection
// STRIKERS_HOME/cards/collection.txt, every career's (written whole: a temp file, then renamed over it). A card = its key
// + finish + shiny (+ its number for a GOLD / BLACK): what online trading will move.
// r153 (Lance: "i started a new franchise and had the same amount of coins/cards/binder"; "i want EACH save/franchise to be
// a FRESH card binder with a fresh slate of RNG"): a binder per career - STRIKERS_HOME/cards/career-<its seed>.txt (its
// cards, packs, coins, slabs, its own salt: a new career's first pack never the same as another's), its slab shelf beside
// it. The old shared collection.txt (and slab_shelf.txt) go to the career played most (the most saves of the 3 slots) the
// first time it's loaded. A co-op / league career has its own binder on each headset.
unsigned g_careerSeed = 0;  // the career whose binder is loaded (0 none yet: nothing earned is taken)
bool g_dispStale = false;   // (its slab shelf to read again: game thread)
std::string coll_path() { return home_dir() + "/cards/career-" + std::to_string(g_careerSeed) + ".txt"; }
std::string legacy_coll_path() { return home_dir() + "/cards/collection.txt"; }

// "<key>:<parallel>:<shiny>:<number>" (r149: ":<case hit>" after, when it's one)
bool parse_ref(const char* s, Ref* r) {
  char key[128];
  int par = 0, sh = 0, num = 0, hit = 0;
  if (s[0] == '-' && (s[1] == '\0' || s[1] == ' ' || s[1] == '\n')) {  // r230: an empty place (an EXQUISITE pack's 3 cards of 5)
    *r = Ref{};
    return true;
  }
  if (std::sscanf(s, "%127[^:]:%d:%d:%d:%d", key, &par, &sh, &num, &hit) < 3) return false;
  auto it = g_byKey.find(key);
  if (it == g_byKey.end()) return false;
  r->card = it->second;
  r->parallel = std::max(0, std::min(3, par));
  r->shiny = sh != 0;
  r->serial = num;
  r->hit = std::max(0, std::min(3, hit));
  return true;
}
std::string ref_text(const Ref& r) {
  if (r.card < 0 || r.card >= static_cast<int>(g_set.size())) return "-";  // r230: an empty place
  return g_set[r.card].id + ":" + std::to_string(r.parallel) + ":" + (r.shiny ? "1" : "0") + ":" + std::to_string(r.serial) +
         (r.hit != kHitNone ? ":" + std::to_string(r.hit) : std::string());
}

void add_owned(const Ref& r, int delta) {  // (g_mutex held)
  if (r.card < 0) return;
  Owned& o = g_owned[r.card];
  // r230: every LANCINI EXQUISITE copy is numbered: kept whole with the case hits (its tier + number in its serial; never a trade-in's spare)
  const bool exq = g_set[static_cast<size_t>(r.card)].set == 2;
  if (r.hit != kHitNone || exq) {  // r149: a case hit, kept whole
    if (delta > 0) {
      o.hits.push_back(r);
    } else {
      for (auto it = o.hits.begin(); it != o.hits.end(); ++it)
        if (it->hit == r.hit && it->parallel == r.parallel && it->shiny == r.shiny && it->serial == r.serial) {
          o.hits.erase(it);
          break;
        }
    }
  } else {
    int& n = o.n[r.parallel][r.shiny ? 1 : 0];
    n = std::max(0, n + delta);
  }
  if (r.parallel == kGoldTizm && r.serial > 0 && !exq) {  // (r230: an Exquisite number isn't a GOLD /10's)
    auto it = std::find(o.golds.begin(), o.golds.end(), r.serial);
    if (delta > 0 && it == o.golds.end()) o.golds.push_back(r.serial);
    if (delta < 0 && it != o.golds.end()) o.golds.erase(it);
    std::sort(o.golds.begin(), o.golds.end());
  }
}

void load_collection() {  // (g_mutex held, the set loaded)
  FILE* f = std::fopen(coll_path().c_str(), "r");
  if (f == nullptr) return;
  char line[1024];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    char a[64], b[128], nums[256];
    if (line[0] == '#') continue;
    if (std::sscanf(line, "salt %63s", a) == 1) {
      g_salt = std::strtoull(a, nullptr, 16);
    } else if (std::sscanf(line, "serial %63s", a) == 1) {
      g_serial = std::atoi(a);
    } else if (std::sscanf(line, "opened %63s", a) == 1) {
      g_opened = std::atoi(a);
    } else if (std::strncmp(line, "coins ", 6) == 0) {  // r140
      // r208: 64-bit (strtoll: an r140-r207 file's 32-bit numbers read the same); the balance held to 0..kMax
      const char* p = line + 6;
      long long v = 0;
      if (xrcoin::parse(p, &v, &p)) g_coins = v;
      if (xrcoin::parse(p, &v, &p)) g_coinsEarned = v;
      if (xrcoin::parse(p, &v, &p)) g_coinsSpent = v;
      g_coins = xrcoin::clamp(g_coins);
    } else if (std::strncmp(line, "lastcoins ", 10) == 0) {
      int used = 0;
      if (std::sscanf(line, "lastcoins %d %lld %n", &g_lastCoins, &g_lastCoinsWhen, &used) >= 2 && used > 0) {
        g_lastCoinsWhy = line + used;
        while (!g_lastCoinsWhy.empty() && (g_lastCoinsWhy.back() == '\n' || g_lastCoinsWhy.back() == '\r')) g_lastCoinsWhy.pop_back();
      }
    } else if (std::sscanf(line, "cameo %127s", b) == 1) {  // r141
      g_cameoSeen.insert(b);
    } else if (std::sscanf(line, "keep %127s", b) == 1) {  // r146
      g_keep.insert(b);
    } else if (std::sscanf(line, "new %127s", b) == 1) {  // r149
      g_newCards.insert(b);
    } else if (std::sscanf(line, "hit %127s", b) == 1) {  // r149: a case hit you have
      Ref h;
      if (parse_ref(b, &h) && h.card >= 0 && (h.hit != kHitNone || g_set[static_cast<size_t>(h.card)].set == 2)) add_owned(h, +1);  // (r230: + Exquisite)
    } else if (std::strncmp(line, "godpacks ", 9) == 0) {  // r149
      std::sscanf(line, "godpacks %d %lld", &g_godPacks, &g_godWhen);
    } else if (std::strncmp(line, "graded ", 7) == 0) {  // r152
      std::sscanf(line, "graded %d", &g_gradeSent);
    } else if (std::strncmp(line, "slab ", 5) == 0) {  // r152: a TIZM GRADING slab
      Slab s;
      unsigned cert = 0;
      int seen = 0, show = 0;
      if (std::sscanf(line, "slab %127s %d %u %d %d %d %lld", b, &s.grade, &cert, &s.left, &seen, &show, &s.when) >= 4 &&
          parse_ref(b, &s.ref)) {
        s.cert = cert;
        s.grade = std::max(1, std::min(10, s.grade));
        s.left = std::max(0, s.left);
        s.seen = seen != 0;
        s.show = show != 0;
        g_slabs.push_back(s);
      }
    } else if (std::strncmp(line, "traded ", 7) == 0) {
      std::sscanf(line, "traded %d %d", &g_tradedCards, &g_tradedPacks);
    } else if (std::strncmp(line, "trade ", 6) == 0) {
      Trade t;
      char what[700] = {};
      if (std::sscanf(line, "trade %lld %d %699s", &t.when, &t.pack, what) == 3) {
        t.what = what;
        g_trades.push_back(t);
      }
    } else if (std::strncmp(line, "card ", 5) == 0) {
      int par = 0, shiny = 0, count = 0;
      nums[0] = '\0';
      if (std::sscanf(line, "card %127s %d %d %d %255s", b, &par, &shiny, &count, nums) >= 4) {
        auto it = g_byKey.find(b);
        if (it == g_byKey.end()) {
          CLOG("collection: %s isn't in this set (kept out)", b);
          continue;
        }
        Owned& o = g_owned[it->second];
        o.n[std::max(0, std::min(3, par))][shiny ? 1 : 0] += std::max(0, count);
        for (const char* s = nums; *s != '\0';) {  // a GOLD's numbers, "3,7"
          const int v = std::atoi(s);
          if (v > 0 && par == kGoldTizm) o.golds.push_back(v);
          const char* comma = std::strchr(s, ',');
          if (comma == nullptr) break;
          s = comma + 1;
        }
        std::sort(o.golds.begin(), o.golds.end());
      }
    } else if (std::strncmp(line, "pack ", 5) == 0) {
      Pack p;
      unsigned flags = 0;
      int used = 0;
      if (std::sscanf(line, "pack %d %u%n", &p.serial, &flags, &used) < 2) continue;
      p.flags = flags;
      const char* s = line + used;
      bool ok = true;
      for (int i = 0; i < kPackCards && ok; ++i) {
        char tok[200];
        if (std::sscanf(s, " %199s%n", tok, &used) < 1) {
          ok = false;
          break;
        }
        s += used;
        ok = parse_ref(tok, &p.cards[i]);
      }
      if (ok) g_packs.push_back(p);
    } else if (std::strncmp(line, "pull ", 5) == 0) {
      long long when = 0;
      Pull pl;
      if (std::sscanf(line, "pull %lld %127s", &when, b) == 2 && parse_ref(b, &pl.ref)) {
        pl.when = when;
        g_pulls.push_back(pl);
      }
    }
  }
  std::fclose(f);
  slabs_sync();  // r152
  int owned = 0;
  for (const Owned& o : g_owned) owned += o.total() > 0 ? 1 : 0;
  CLOG("collection: %d different cards, %d packs waiting, %d opened", owned, static_cast<int>(g_packs.size()), g_opened);
}

#ifdef HARNESS_REAL_CARDS
std::string g_h217Saved;
#endif
std::string collection_text();  // r221 (below)
// r221: the collection file's own lock (a leaf: taken under g_mutex by save_collection, alone by the background saver -
// never g_mutex under it) and the newest snapshot on disk (an older one is never written over a newer)
std::mutex g_collIoM;
unsigned long long g_collSeq = 0;      // (g_mutex) snapshots made
unsigned long long g_collWritten = 0;  // (g_collIoM)
void write_collection_file(const std::string& path, const std::string& out, unsigned long long seq) {
  std::lock_guard<std::mutex> io(g_collIoM);
  if (seq <= g_collWritten) return;
  const std::string tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) {
    CLOG("collection: can't write %s", tmp.c_str());
    return;
  }
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size() && std::fflush(f) == 0;
  ::fsync(fileno(f));
  std::fclose(f);
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) CLOG("collection: writing it failed");
  else g_collWritten = seq;
}
void save_collection() {  // (g_mutex held)
  const std::string dir = home_dir() + "/cards";
  ::mkdir(dir.c_str(), 0775);
  const std::string out = collection_text();
  write_collection_file(coll_path(), out, ++g_collSeq);
}
// r221 (Lance's r220 log, MR Jumbotron: ~20 takeover catches a goal, each a whole collection file written + fsync'd on the
// frame thread, under g_mutex): the room takeover's coins are paid at once (the balance) and the file is written by ONE
// background saver - the latest state each time it runs (asks while it writes: one more run after, never a pile).
std::mutex g_saverM;
std::condition_variable g_saverCv;
bool g_saverWanted = false, g_saverStarted = false;
std::atomic<int> g_saverRuns{0};
std::atomic<long long> g_saverNs{0}, g_saverMaxNs{0};
std::atomic<long long> g_coinPayNs{0};  // r221: the frame thread's time paying takeover coins (xr_quest's goal probe)
std::atomic<int> g_coinPays{0};
void saver_main() {
  for (;;) {
    {
      std::unique_lock<std::mutex> lk(g_saverM);
      g_saverCv.wait(lk, [] { return g_saverWanted; });
      g_saverWanted = false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::string out, path;
    unsigned long long seq = 0;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (g_careerSeed == 0) continue;
      const std::string dir = home_dir() + "/cards";
      ::mkdir(dir.c_str(), 0775);
      out = collection_text();
      path = coll_path();
      seq = ++g_collSeq;
    }
    write_collection_file(path, out, seq);
    const long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    g_saverNs.fetch_add(ns);
    if (ns > g_saverMaxNs.load()) g_saverMaxNs.store(ns);
    g_saverRuns.fetch_add(1);
  }
}
void save_collection_soon() {  // (any thread; g_mutex held or not)
#if defined(__wasm__)
  save_collection();  // (the PC proof's WebAssembly has no threads: written at once there)
#else
  {
    std::lock_guard<std::mutex> lk(g_saverM);
    g_saverWanted = true;
    if (!g_saverStarted) {
      g_saverStarted = true;
      std::thread(saver_main).detach();
    }
  }
  g_saverCv.notify_one();
#endif
}
std::string collection_text() {  // (g_mutex held)
  std::string out =
      "# 2026-27 LANCINI TIZM STRIKERS (r137): this career's trading cards (r153: a binder per career). Written by the game (xr_cards.cpp).\n"
      "#   card <key> <finish 0 base, 1 silver, 2 gold /10, 3 black 1/1> <shiny 0|1> <count> [gold numbers 3,7]\n"
      "#   pack <serial> <flags> <key>:<finish>:<shiny>:<number> x5   a pack waiting, rolled the moment it was earned\n"
      "#   pull <unix time> <key>:<finish>:<shiny>:<number>            a big pull (STRIKERS ILLUSTRATED's cover line)\n"
      "#   coins <balance> <ever earned> <ever spent>                  r140: BIT COINS (franchise matches you play)\n"
      "#   lastcoins <coins> <unix time> <why>                          the last match's\n"
      "#   cameo <key>                                                  r141: a BLACK 1/1's life-size cameo, seen\n"
      "#   traded <cards> <packs>                                       r146: spare base cards traded in, packs got for them\n"
      "#   trade <unix time> <pack serial> <key>*<n>,...                r146: a trade-in (the newest 40)\n"
      "#   keep <key>                                                   r146: a card kept out of trade-ins\n"
      "#   new <key>                                                    r149: from a pack, not looked at in the binder yet\n"
      "#   hit <key>:<finish>:<shiny>:<number>:<hit 1 boom, 2 hometown, 3 signed>   r149: a case hit you have (its own copy)\n"
      "#   godpacks <opened> <unix time of the last>                   r149: GOD PACKs (all five SILVER+ / case hits)\n"
      "#   slab <key>:<finish>:<shiny>:<number>[:<hit>] <grade 1-10> <cert> <matches left, 0 back> <seen> <on display> <unix time sent>\n"
      "#                                                                r152: a TIZM GRADING slab (its copy sealed: not in card / hit)\n"
      "#   graded <slabs ever sent>                                     r152\n"
      "#   (a pack's / a pull's cards: <key>:<finish>:<shiny>:<number>[:<hit>])\n"
      "#   r230: a LANCINI EXQUISITE copy <key>_exq:<finish>:0:<its tier x 1000 + its number>:<hit> - every one a hit line (each numbered);\n"
      "#         an EXQUISITE pack's 3 cards + '-' for its two empty places\n"
      "version 2\n";
  char buf[256];
  std::snprintf(buf, sizeof(buf), "salt %016llx\nserial %d\nopened %d\ncoins %lld %lld %lld\n", static_cast<unsigned long long>(g_salt),  // r208: 64-bit
                g_serial, g_opened, g_coins, g_coinsEarned, g_coinsSpent);
  out += buf;
  if (g_lastCoins > 0) out += "lastcoins " + std::to_string(g_lastCoins) + " " + std::to_string(g_lastCoinsWhen) + " " + g_lastCoinsWhy + "\n";
  {
    std::vector<std::string> seen(g_cameoSeen.begin(), g_cameoSeen.end());  // (r141; sorted: the file the same each time)
    std::sort(seen.begin(), seen.end());
    for (const std::string& k : seen) out += "cameo " + k + "\n";
  }
  {  // r146: trade-ins, kept cards (sorted: the file the same each time)
    out += "traded " + std::to_string(g_tradedCards) + " " + std::to_string(g_tradedPacks) + "\n";
    if (g_godPacks > 0) out += "godpacks " + std::to_string(g_godPacks) + " " + std::to_string(g_godWhen) + "\n";  // r149
    if (g_gradeSent > 0) out += "graded " + std::to_string(g_gradeSent) + "\n";  // r152
    for (const Slab& s : g_slabs)
      if (s.ref.card >= 0)
        out += "slab " + ref_text(s.ref) + " " + std::to_string(s.grade) + " " + std::to_string(s.cert) + " " + std::to_string(s.left) +
               " " + (s.seen ? "1" : "0") + " " + (s.show ? "1" : "0") + " " + std::to_string(s.when) + "\n";
    const size_t first = g_trades.size() > 40 ? g_trades.size() - 40 : 0;
    for (size_t i = first; i < g_trades.size(); ++i)
      out += "trade " + std::to_string(g_trades[i].when) + " " + std::to_string(g_trades[i].pack) + " " + g_trades[i].what + "\n";
    std::vector<std::string> keep(g_keep.begin(), g_keep.end());
    std::sort(keep.begin(), keep.end());
    for (const std::string& k : keep) out += "keep " + k + "\n";
    std::vector<std::string> fresh(g_newCards.begin(), g_newCards.end());  // r149
    std::sort(fresh.begin(), fresh.end());
    for (const std::string& k : fresh) out += "new " + k + "\n";
    for (size_t i = 0; i < g_owned.size(); ++i)  // r149: case hits, each copy whole (the set's order)
      for (const Ref& h : g_owned[i].hits) out += "hit " + ref_text(h) + "\n";
  }
  for (size_t i = 0; i < g_set.size(); ++i)
    for (int p = 0; p < 4; ++p)
      for (int sh = 0; sh < 2; ++sh) {
        const int n = g_owned[i].n[p][sh];
        if (n <= 0) continue;
        out += "card " + g_set[i].id + " " + std::to_string(p) + " " + std::to_string(sh) + " " + std::to_string(n);
        // (a card's GOLD numbers once, on its first GOLD line)
        if (p == kGoldTizm && (sh == 0 || g_owned[i].n[kGoldTizm][0] == 0) && !g_owned[i].golds.empty()) {
          std::string nums;
          for (int v : g_owned[i].golds) nums += (nums.empty() ? "" : ",") + std::to_string(v);
          out += " " + nums;
        }
        out += "\n";
      }
  for (const Pack& p : g_packs) {
    out += "pack " + std::to_string(p.serial) + " " + std::to_string(p.flags);
    for (const Ref& r : p.cards) out += " " + ref_text(r);
    out += "\n";
  }
  const size_t first = g_pulls.size() > 40 ? g_pulls.size() - 40 : 0;
  for (size_t i = first; i < g_pulls.size(); ++i) out += "pull " + std::to_string(g_pulls[i].when) + " " + ref_text(g_pulls[i].ref) + "\n";
#ifdef HARNESS_REAL_CARDS
  g_h217Saved = out;  // (r217's PC proof: the file as written, for its reload check)
#endif
  return out;
}

// How big a pull is (the opening shows the biggest last): who it is, then its finish, shiny on top.
int pull_score(const Ref& r) {
  // r149 (Lance: "put the hits as the very last card you can see, IF ANY, and if you get 1 before the last card, that just
  // means you're about to have a crazy pack"): strict tiers - BASE < SILVER < GOLD < a case hit < BLACK 1/1 (the top) -, a
  // shiny above the rest of its tier, then who it is: the biggest is always the last card
  const int tier = r.parallel == kBlack ? 4 : (r.hit != kHitNone ? 3 : (r.parallel == kGoldTizm ? 2 : (r.parallel == kSilver ? 1 : 0)));
  if (r.card >= 0 && r.card < static_cast<int>(g_set.size()) && g_set[static_cast<size_t>(r.card)].set == 2) {  // r230: an EXQUISITE copy
    return 1000 + xrexq::rank(xrexq::tier_of(r.serial)) * 10 + g_set[r.card].rarity;
  }
  return tier * 100 + (r.shiny ? 10 : 0) + (r.card >= 0 ? g_set[r.card].rarity : 0);
}
bool big_pull(const Ref& r) {
  if (r.card >= 0 && r.card < static_cast<int>(g_set.size()) && g_set[static_cast<size_t>(r.card)].set == 2)  // r230: GOLD LEAF+ / a hit
    return xrexq::tier_of(r.serial) >= xrexq::kGoldLeaf;
  return r.card >= 0 && (r.parallel >= kGoldTizm || r.shiny || r.hit != kHitNone || g_set[r.card].rarity == kSuper);
}

// r213 (Lance: one BLACK 1/1 per card ever - "whoever holds a grail 1/1 sets the price"): the cards whose 1/1 a CPU collector holds
// (COLLECTOR TRADES, xr_trade.cpp tells them; a career it hasn't told yet: its world11-<career>.txt) - a pack gives a GOLD instead.
// Its own lock (a leaf: taken under g_mutex, never the other way).
std::mutex g_worldM;
unsigned g_worldCareer = 0;
std::vector<std::string> g_worldKeys;  // (sorted)
void world_set(unsigned career, const std::vector<std::string>& keys) {
  std::lock_guard<std::mutex> lock(g_worldM);
  g_worldCareer = career;
  g_worldKeys = keys;
  std::sort(g_worldKeys.begin(), g_worldKeys.end());
}
bool world_black(int card) {  // (g_mutex held: g_set, g_careerSeed)
  if (card < 0 || card >= static_cast<int>(g_set.size()) || g_careerSeed == 0) return false;
  std::lock_guard<std::mutex> lock(g_worldM);
  if (g_worldCareer != g_careerSeed) {  // (not told for this career yet: its file)
    g_worldCareer = g_careerSeed;
    g_worldKeys.clear();
    if (FILE* f = std::fopen((home_dir() + "/cards/world11-" + std::to_string(g_careerSeed) + ".txt").c_str(), "r")) {
      char line[160];
      while (std::fgets(line, sizeof(line), f) != nullptr) {
        std::string k(line);
        while (!k.empty() && (k.back() == '\n' || k.back() == '\r' || k.back() == ' ')) k.pop_back();
        if (!k.empty() && k.compare(0, 7, "world11") != 0) g_worldKeys.push_back(k);
      }
      std::fclose(f);
      std::sort(g_worldKeys.begin(), g_worldKeys.end());
    }
  }
  return std::binary_search(g_worldKeys.begin(), g_worldKeys.end(), g_set[static_cast<size_t>(card)].id);
}

// A card's finish: BLACK 1/1 one of each card ever (one you have, or one waiting in a pack: a GOLD instead), GOLD /10 a
// number of the ten no copy of yours has (all ten taken: a SILVER).  (g_mutex held)
// r213: + a 1/1 a CPU collector holds (world_black): a GOLD instead too
void roll_parallel(uint64_t& rng, Ref* r, const std::vector<Ref>& also) {
  const double x = rand01(rng);
  int par = x < 1.0 / kBlackOdds ? kBlack
          : x < 1.0 / kBlackOdds + 1.0 / kGoldOdds ? kGoldTizm
          : x < 1.0 / kBlackOdds + 1.0 / kGoldOdds + 1.0 / kSilverOdds ? kSilver : kBase;
  auto taken = [&](int parallel, int serial) {
    const Owned& o = g_owned[r->card];
    if (parallel == kBlack && (o.n[kBlack][0] + o.n[kBlack][1] > 0 || o.blackAny())) return true;  // (r149: a case hit's too)
    if (parallel == kBlack && world_black(r->card)) return true;  // r213: a collector holds it
    if (parallel == kGoldTizm && std::find(o.golds.begin(), o.golds.end(), serial) != o.golds.end()) return true;
    for (const Ref& s : o.sealed)  // r152: one in a slab still yours
      if (s.parallel == parallel && (parallel == kBlack || s.serial == serial)) return true;
    for (const Pack& p : g_packs)
      for (const Ref& q : p.cards)
        if (q.card == r->card && q.parallel == parallel && (parallel == kBlack || q.serial == serial)) return true;
    for (const Ref& q : also)
      if (q.card == r->card && q.parallel == parallel && (parallel == kBlack || q.serial == serial)) return true;
    return false;
  };
  if (par == kBlack && taken(kBlack, 1)) par = kGoldTizm;
  r->serial = 0;
  if (par == kGoldTizm) {
    std::vector<int> free;
    for (int v = 1; v <= kGoldRun; ++v)
      if (!taken(kGoldTizm, v)) free.push_back(v);
    if (free.empty()) par = kSilver;
    else r->serial = free[mix64(rng) % free.size()];
  }
  if (par == kBlack) r->serial = 1;
  r->parallel = par;
}

// A pack: four cards of the common slots and one rare or better, never the same card twice; each its finish; a Pokemon
// shiny 1 in kShinyOdds; ordered for the opening (the biggest last).
// r149 (Lance: "a crazy, greek mythology ass "celebration" graphic that happens if you ever get a "god pack" in which ALL
// ... cards ... are silver or above!"): a GOD PACK - every card SILVER or better (or a case hit). By chance it would almost
// never happen (r211's odds: about 1 in 80 million packs), so a pack is rolled as one 1 in kGodOdds (every card's finish at
// least SILVER); either way flagged kGodPack (its rip and its last card: the show). r211 (Lance): 1 in 2,000 (was 400).
constexpr int kGodOdds = 2000;
constexpr unsigned kGodPack = 32;  // (a pack's flags)
bool god_cards(const Pack& p) {
  for (const Ref& r : p.cards)
    if (r.card >= 0 && r.parallel < kSilver && r.hit == kHitNone) return false;
  return true;
}
// r217: `set` 0 a LANCINI TIZM pack (its cards), 1 a JAM '96 pack (Jam cards) - the same odds, the same roll, each set its own pools
// and its own 1/1s (roll_parallel: per card)
Pack roll_pack(uint64_t& rng, const std::vector<Ref>& earlier, bool god = false, int set = 0) {  // (g_mutex held)
  std::vector<int> pool[3];
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].here && g_set[i].set == set) pool[g_set[i].rarity].push_back(static_cast<int>(i));
  Pack p;
  std::vector<Ref> also = earlier;
  for (int slot = 0; slot < kPackCards; ++slot) {
    const double x = rand01(rng);
    int tier = slot < kPackCards - 1 ? (x < kSlotSR ? kSuper : (x < kSlotSR + kSlotR ? kRare : kCommon))
                                     : (x < kLastSR ? kSuper : kRare);
    while (tier > kCommon && pool[tier].empty()) --tier;  // (a headset without that tier's characters)
    while (tier < kSuper && pool[tier].empty()) ++tier;
    if (pool[tier].empty()) break;
    int pick = -1;
    for (int tries = 0; tries < 12; ++tries) {
      pick = pool[tier][mix64(rng) % pool[tier].size()];
      bool twice = false;
      for (int k = 0; k < slot; ++k) twice = twice || p.cards[k].card == pick;
      if (!twice) break;
    }
    Ref& r = p.cards[slot];
    r.card = pick;
    roll_parallel(rng, &r, also);
    if (god && r.parallel == kBase) r.parallel = kSilver;  // r149: a god pack's: SILVER at least
    r.shiny = pk_key(g_set[pick].key) && mix64(rng) % kShinyOdds == 0;
    {  // r149: a case hit, on top of its finish (r211's odds: about 1 in 100 packs has one; r149-r210 1 in 24)
      const double x = r.parallel == kBlack ? 1.0 : rand01(rng);  // (a BLACK 1/1 is its own showpiece, signed already)
      r.hit = x < kHitOdds[kHitBoom] ? kHitBoom
            : x < kHitOdds[kHitBoom] + kHitOdds[kHitHome] ? kHitHome
            : x < kHitOdds[kHitBoom] + kHitOdds[kHitHome] + kHitOdds[kHitSigned] ? kHitSigned : kHitNone;
    }
    also.push_back(r);
  }
  std::stable_sort(std::begin(p.cards), std::end(p.cards), [](const Ref& a, const Ref& b) { return pull_score(a) < pull_score(b); });
  return p;
}

// r230: LANCINI EXQUISITE (its set, packs, numbers, words)
#include "xr_cards_exq.inc"

// "BLACK TIZM 1/1 CHARIZARD", "GOLD TIZM MEWTWO 07/10", "SHINY GENGAR", "SUPER RARE BLASTOISE", "MARIO"
std::string pull_text(const Ref& r) {
  if (exq_card(r.card)) return exq_pull_text(r);  // r230
  const Card& c = g_set[r.card];
  std::string s = upper(c.name);
  if (c.set == 1) s = "JAM '96 " + s;  // r217
  if (r.shiny) s = "SHINY " + s;
  if (r.hit != kHitNone) {  // r149: "TIZM BOOM CHARIZARD", "HOMETOWN MARIO", "SIGNED GOLD TIZM LINK 03/10"
    Ref plain = r;
    plain.hit = kHitNone;
    const std::string under = pull_text(plain);
    return std::string(kHitName[r.hit]) + " " + (r.parallel == kBase && !r.shiny && c.rarity == kSuper ? s : under);
  }
  if (r.parallel == kBlack) return "BLACK TIZM 1/1 " + s;
  if (r.parallel == kGoldTizm) return "GOLD TIZM " + s + " " + (r.serial < 10 ? "0" : "") + std::to_string(r.serial) + "/10";
  if (r.parallel == kSilver) return "SILVER TIZM " + s;
  if (!r.shiny && c.rarity == kSuper) return "SUPER RARE " + s;
  return s;
}

// ------------------------------------------------------------------- r146: trade-ins, card levels, sets
std::vector<int> binder_list();  // (below)
// A card's level by the copies you have (any finish): 0 none, 1 .. 5
int card_level(const Owned& o) {
  const int n = o.total();
  int lv = 0;
  for (int k = 0; k < 5; ++k)
    if (n >= kLevelCopies[k]) lv = k + 1;
  return lv;
}
int level_xp(int lv) { return lv > 1 ? (lv - 1) * kLevelXp : 0; }
// Its spare copies a trade-in can take: its plain BASE copies (no SILVER / GOLD / BLACK, no shiny), never its last copy of
// any kind. r147 (Lance: "when i said "common" i suuuuper meant "Base" cards"): any character's, common, rare or super rare.
// (g_mutex held)
// r210: your copies listed on the TIZM CARD MARKET (LIST FOR SALE: xrmarket tells mk_listed whenever they change) - kept out of
// TRADE-INS and GRADING while listed. Its own lock (a leaf: taken under g_mutex or the market's, never the other way).
std::mutex g_listedM;
std::vector<xrmarket::Variant> g_listedV;
int listed_n(int card, int parallel, bool shiny, int hit, int serial, int grade) {
  std::lock_guard<std::mutex> lock(g_listedM);
  int n = 0;
  for (const xrmarket::Variant& v : g_listedV)
    n += v.card == card && v.parallel == parallel && v.shiny == shiny && v.hit == hit && (parallel != kGoldTizm || v.serial == serial) && v.grade == grade ? 1 : 0;
  return n;
}
int listed_raw(int card) {  // (every raw copy of a card listed, any finish)
  std::lock_guard<std::mutex> lock(g_listedM);
  int n = 0;
  for (const xrmarket::Variant& v : g_listedV) n += v.card == card && v.grade == 0 ? 1 : 0;
  return n;
}
// r226: your copies in a CARD MUSEUM piece (shown or put away) - kept out of TRADE-INS like a listed one. A copy of the museum's list
// (xrmuseum::items: its own lock, taken before g_mutex - never under it), refreshed on the game thread when the museum changes. Leaf lock.
std::mutex g_museumM;
std::vector<xrmarket::Variant> g_museumV;
int museum_base_n(int card) {  // (its plain BASE copies in the museum: not shiny, no hit, raw)
  std::lock_guard<std::mutex> lock(g_museumM);
  int n = 0;
  for (const xrmarket::Variant& v : g_museumV) n += v.card == card && v.parallel == kBase && !v.shiny && v.hit == kHitNone && v.grade == 0 ? 1 : 0;
  return n;
}
// r226 (Lance 11-12: "how do i have 97 '96 jam cards but nothing to trade in yet ... are you not including base cards for the '96 jam
// cards???"): JAM '96 BASE spares trade in too - for a JAM '96 pack, never mixed with LANCINI TIZM spares (trade_pick(set)). The same
// rules as the main set's: plain BASE copies only (no SILVER / GOLD / BLACK, no shiny, never a case hit: hits aren't in n), never your
// last copy, never a listed / held one (r210 / r213), r226: never one in the museum; r152's slabs (graded / at grading) aren't in n.
// (r217-r225: a JAM '96 card was never a spare.)
int spare_of(int card) {
  if (card < 0 || card >= static_cast<int>(g_set.size())) return 0;
  const Owned& o = g_owned[card];
  // (r210: a listed BASE copy is no spare: never traded in under its listing)
  return std::max(0, std::min(o.n[kBase][0] - listed_n(card, kBase, false, kHitNone, 0, 0) - museum_base_n(card), o.total() - 1 - listed_raw(card)));
}
// The TRADE-INS page's order, and the next trade's: commons before rares before super rares (the best stay longest), the
// most duplicated first among them, then by number
bool trade_before(int x, int sx, int y, int sy) {
  if (g_set[x].rarity != g_set[y].rarity) return g_set[x].rarity < g_set[y].rarity;
  if (sx != sy) return sx > sy;
  return g_set[x].num < g_set[y].num;
}
// The next trade-in's cards (card, how many), kTradeFor of them, one at a time by trade_before (with the spares left), not
// the ones kept; empty when there aren't kTradeFor. (g_mutex held)
// r226: `set` 0 LANCINI TIZM spares (for a LANCINI TIZM pack), 1 JAM '96 spares (for a JAM '96 pack) - never both in one
std::vector<std::pair<int, int>> trade_pick(int set) {
  const int need = trade_for(set);
  std::vector<std::pair<int, int>> left;  // (card, spares left)
  for (size_t i = 0; i < g_set.size(); ++i) {
    if (g_set[i].set != set) continue;
    const int sp = spare_of(static_cast<int>(i));
    if (sp > 0 && g_keep.count(g_set[i].id) == 0) left.push_back({static_cast<int>(i), sp});
  }
  std::vector<std::pair<int, int>> out;
  int got = 0;
  for (; got < need; ++got) {
    int best = -1;
    for (size_t k = 0; k < left.size(); ++k)
      if (left[k].second > 0 &&
          (best < 0 || trade_before(left[k].first, left[k].second, left[best].first, left[best].second)))
        best = static_cast<int>(k);
    if (best < 0) break;
    --left[best].second;
    bool found = false;
    for (auto& o : out)
      if (o.first == left[best].first) {
        ++o.second;
        found = true;
      }
    if (!found) out.push_back({left[best].first, 1});
  }
  if (got < need) out.clear();
  return out;
}
int spare_total(bool keptToo, int set = 0) {  // (g_mutex held; r226: one set's)
  int n = 0;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].set == set && (keptToo || g_keep.count(g_set[i].id) == 0)) n += spare_of(static_cast<int>(i));
  return n;
}

// The sets (built once from the set: STRIKERS_HOME/franchise/evolutions.txt, types.txt): the EVOLUTION LINES (a Pokemon
// family, a Marvel hero's suits - every card of it on this headset), the 18 Pokemon TYPES (a Pokemon in its first and
// second type's), and the binder's pages (its order, 9 a page: what the binder shows).
struct CardSet {
  std::string name;
  uint32_t rgb = 0x3A4A70;
  std::vector<int> cards;
  bool suits = false;  // (a Marvel hero's: no shiny ones)
};
std::vector<CardSet> g_lines, g_types;
bool g_setsBuilt = false;
const char* const kTypeName[18] = {"normal", "fire",    "water", "grass", "electric", "ice",   "fighting", "poison", "ground",
                                   "flying", "psychic", "bug",   "rock",  "ghost",    "dragon", "dark",    "steel",  "fairy"};
constexpr uint32_t kTypeRgb[18] = {0xA8A878, 0xF08030, 0x6890F0, 0x78C850, 0xF8D030, 0x98D8D8, 0xC03028, 0xA040A0, 0xE0C068,
                                   0xA890F0, 0xF85888, 0xA8B820, 0xB8A038, 0x705898, 0x7038F8, 0x705848, 0xB8B8D0, 0xEE99AC};
void build_sets() {  // (g_mutex held, the set loaded)
  if (g_setsBuilt) return;
  g_setsBuilt = true;
  const int n = static_cast<int>(g_set.size());
  std::vector<int> up(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) up[i] = i;
  std::function<int(int)> root = [&](int i) { return up[i] == i ? i : (up[i] = root(up[i])); };
  std::vector<bool> evolved(static_cast<size_t>(n), false);
  if (FILE* f = std::fopen((home_dir() + "/franchise/evolutions.txt").c_str(), "r")) {
    char a[128], b[128];
    int lv = 0;
    while (std::fscanf(f, "%127s %127s %d", a, b, &lv) == 3) {
      auto ia = g_byKey.find(a), ib = g_byKey.find(b);
      if (ia == g_byKey.end() || ib == g_byKey.end()) continue;
      up[root(ia->second)] = root(ib->second);
      evolved[ib->second] = true;
    }
    std::fclose(f);
  }
  std::unordered_map<int, std::vector<int>> fam;
  for (int i = 0; i < n; ++i) fam[root(i)].push_back(i);
  for (auto& kv : fam) {
    std::vector<int>& m = kv.second;
    if (m.size() < 2) continue;
    bool here = true, pk = true, mua = true, orig = true;
    for (int i : m) {
      here = here && g_set[i].here;
      pk = pk && pk_key(g_set[i].key);
      mua = mua && g_set[i].key.compare(0, 4, "mua_") == 0;
      orig = orig && PortOriginalKey(g_set[i].key.c_str());  // r183: an original's line / uniforms
    }
    if (!here || (!pk && !mua && !orig)) continue;  // (a line you couldn't finish on this headset)
    const bool uniforms = orig && PortOriginalUniformLine(g_set[m[0]].key.c_str());
    std::sort(m.begin(), m.end(), [](int x, int y) { return g_set[x].num < g_set[y].num; });
    int first = m[0];
    for (int i : m)
      if (!evolved[i]) {
        first = i;
        break;
      }
    CardSet s;
    s.suits = mua || uniforms;
    s.name = upper(g_set[first].name) + (mua ? " SUITS" : (uniforms ? " UNIFORMS" : " LINE"));
    s.rgb = g_set[first].c1;
    s.cards = m;
    if (first != m[0]) {  // (its first form first)
      s.cards.erase(std::find(s.cards.begin(), s.cards.end(), first));
      s.cards.insert(s.cards.begin(), first);
    }
    g_lines.push_back(s);
  }
  std::sort(g_lines.begin(), g_lines.end(), [](const CardSet& x, const CardSet& y) { return g_set[x.cards[0]].num < g_set[y.cards[0]].num; });
  g_types.resize(18);
  for (int t = 0; t < 18; ++t) {
    g_types[t].name = upper(kTypeName[t]);
    g_types[t].rgb = kTypeRgb[t];
  }
  if (FILE* f = std::fopen((home_dir() + "/franchise/types.txt").c_str(), "r")) {
    char line[256], key[128], t1[32], t2[32];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      if (line[0] == '#') continue;
      t2[0] = '\0';
      const int got = std::sscanf(line, "%127s %31s %31s", key, t1, t2);
      if (got < 2) continue;
      auto it = g_byKey.find(key);
      if (it == g_byKey.end() || !g_set[it->second].here) continue;
      for (int t = 0; t < 18; ++t)
        if (std::strcmp(t1, kTypeName[t]) == 0 || (got > 2 && std::strcmp(t2, kTypeName[t]) == 0)) g_types[t].cards.push_back(it->second);
    }
    std::fclose(f);
  }
  // r183: the ORIGINALS set - every original on this headset (each form, each uniform), after the 18 types
  {
    CardSet o;
    o.name = "ORIGINALS";
    o.rgb = kSeriesRgb[5];
    for (int i = 0; i < n; ++i)
      if (g_set[i].here && g_set[i].set == 0 && PortOriginalKey(g_set[i].key.c_str())) o.cards.push_back(i);  // (r217: the main set's)
    if (!o.cards.empty()) g_types.push_back(o);
  }
  CLOG("sets: %d evolution lines, %d types", static_cast<int>(g_lines.size()), 18);
}
bool set_done(const CardSet& s) {  // (g_mutex held)
  if (s.cards.empty()) return false;
  for (int i : s.cards)
    if (g_owned[i].total() <= 0) return false;
  return true;
}
int set_have(const CardSet& s) {
  int n = 0;
  for (int i : s.cards) n += g_owned[i].total() > 0 ? 1 : 0;
  return n;
}
// The binder's pages full (every pocket of the page yours), of how many
int full_pages(int* pages) {  // (g_mutex held)
  const std::vector<int> list = binder_list();
  int full = 0;
  *pages = static_cast<int>((list.size() + 8) / 9);
  for (int p = 0; p < *pages; ++p) {
    bool all = true;
    for (int s = 0; s < 9 && all; ++s) {
      const size_t at = static_cast<size_t>(p) * 9 + s;
      all = at < list.size() && g_owned[list[at]].total() > 0;
    }
    full += all ? 1 : 0;
  }
  return full;
}
int coin_pct(int fullPages) { return std::min(kPageCoinsCap, fullPages * kPageCoins); }
// A card's franchise bonuses as they stand: its XP percent (capped) and why ("LV3 +20%, LINE +15%, FIRE +10%"), its
// evolutions' shiny odds (0 the usual). (g_mutex held, the sets built)
int card_bonus(int card, std::string* why, int* shinyOdds) {
  int xp = 0;
  std::string w;
  auto add = [&](int pct, const std::string& what) {
    xp += pct;
    w += (w.empty() ? "" : ", ") + what + " +" + std::to_string(pct) + "%";
  };
  // r217: the character's level - its LANCINI TIZM and JAM '96 copies together (one bonus a character, never two)
  // r230: + its LANCINI EXQUISITE copies
  int lv = card_level(g_owned[card]);
  {
    const Card& cc = g_set[static_cast<size_t>(card)];
    if (cc.twin >= 0 && cc.twin < static_cast<int>(g_owned.size())) {
      Owned both = g_owned[card];
      int extra = g_owned[static_cast<size_t>(cc.twin)].total();
      if (cc.exq >= 0 && cc.exq < static_cast<int>(g_owned.size())) extra += g_owned[static_cast<size_t>(cc.exq)].total();
      both.n[kBase][0] += extra;
      lv = card_level(both);
    }
  }
  if (lv > 1) add(level_xp(lv), "LV" + std::to_string(lv));
  if (shinyOdds != nullptr) *shinyOdds = 0;
  for (const CardSet& s : g_lines)
    if (std::find(s.cards.begin(), s.cards.end(), card) != s.cards.end() && set_done(s)) {
      add(kLineXp, s.suits ? "SUITS" : "LINE");
      if (!s.suits && shinyOdds != nullptr) *shinyOdds = kLineShinyOdds;
    }
  for (const CardSet& s : g_types)
    if (std::find(s.cards.begin(), s.cards.end(), card) != s.cards.end() && set_done(s)) add(kTypeXp, s.name);
  if (why != nullptr) *why = w;
  return std::min(kXpCap, xp);
}
// The bonuses to franchise.cpp (every change of the collection: loaded, a pack opened / put back, a trade-in). (g_mutex held)
void bonuses_update() {
  if (!g_loaded.load()) return;
  build_sets();
  std::vector<PortFranchiseCardBonus> list;
  int withXp = 0, best = 0;
  for (size_t i = 0; i < g_set.size(); ++i) {
    if (g_set[i].set != 0) continue;  // r217: a character once (its Jam card's copies count in its level, card_bonus)
    std::string why;
    int odds = 0;
    const int xp = card_bonus(static_cast<int>(i), &why, &odds);
    if (xp <= 0 && odds <= 0) continue;
    PortFranchiseCardBonus b{};
    std::snprintf(b.key, sizeof(b.key), "%s", g_set[i].key.c_str());
    b.xpPct = xp;
    b.shinyOdds = odds;
    std::snprintf(b.why, sizeof(b.why), "%s", why.c_str());
    list.push_back(b);
    withXp += xp > 0 ? 1 : 0;
    best = std::max(best, xp);
  }
  int pages = 0;
  const int full = full_pages(&pages);
  char cw[64];
  std::snprintf(cw, sizeof(cw), "%d full binder page%s", full, full == 1 ? "" : "s");
  PortFranchiseSetCardBonuses(list.data(), static_cast<int>(list.size()), coin_pct(full), cw);
  CLOG("card bonuses: %d characters with more XP (best +%d%%), %d of %d binder pages full: coins +%d%%", withXp, best, full, pages,
       coin_pct(full));
}

// ------------------------------------------------------------------------------------------------- pictures
struct Img {
  std::vector<uint8_t> rgba;  // straight alpha
  int w = 0, h = 0;
  bool empty() const { return rgba.empty() || w <= 0 || h <= 0; }
};
using ImgP = std::shared_ptr<const Img>;

// A portrait (STRIKERS_HOME/guests/portraits/<key>.png, 256 px), read once; any thread.
ImgP portrait(const std::string& key) {
  static std::mutex m;
  static std::unordered_map<std::string, ImgP> cache;
  {
    std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
  }
  auto img = std::make_shared<Img>();
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load((home_dir() + "/guests/portraits/" + key + ".png").c_str(), &w, &h, &n, 4);
  if (px != nullptr) {
    img->rgba.assign(px, px + static_cast<size_t>(w) * h * 4);
    img->w = w;
    img->h = h;
    stbi_image_free(px);
  }
  std::lock_guard<std::mutex> lock(m);
  if (cache.size() > 160) cache.clear();
  return cache.emplace(key, img).first->second;
}

// A card's big picture (xr_big: its own 3D model rendered big and sharp; a shiny Pokemon's shiny one), waited for up to
// `waitS` seconds; else its portrait (the shiny one's for a shiny Pokemon).
// r226: `urgent` false - the queue's order is the caller's (xrbig::first: a pack's cards in turn order; get() would move each one asked to
// the front, the last asked first); *big: true when it is the big picture (false: the portrait in its place - drawn again when it's in)
ImgP card_art(const Card& c, bool shiny, float waitS, bool urgent = true, bool* big = nullptr) {
  const bool sh = shiny && pk_key(c.key);
  std::vector<uint8_t> px;
  if (big != nullptr) *big = false;
  xrbig::prefetch(c.key, sh, kArt);
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(waitS * 1000.0f));
  for (;;) {
    if ((urgent || xrbig::ready(c.key, sh, kArt)) && xrbig::get(c.key, sh, kArt, &px) && px.size() >= static_cast<size_t>(kArt) * kArt * 4) {
      auto img = std::make_shared<Img>();
      img->rgba = std::move(px);
      img->w = img->h = kArt;
      if (big != nullptr) *big = true;
      return img;
    }
    if (xrbig::failed(c.key, sh, kArt) || std::chrono::steady_clock::now() >= until) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
  }
  ImgP p = portrait(sh ? c.key + "_shiny" : c.key);
  if (p->empty() && sh) p = portrait(c.key);
  return p;
}

// The guests' outlines (STRIKERS_HOME/guests/etch.r8, xr_fx.cpp make_etch's layouts): a cell as distances (cell units,
// inside < 0), side x side; empty when there is none.
std::vector<float> etch_cell(int cell, int* side) {
  *side = 0;
  std::vector<float> out;
  if (cell < 0) return out;
  FILE* f = std::fopen((home_dir() + "/guests/etch.r8").c_str(), "rb");
  if (f == nullptr) return out;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  int px = 0, cols = 16, rows = 0;
  for (const int c : {128, 64}) {
    if (size == long(16 * c) * long(32 * c)) px = c, rows = 32;
    else if (size == long(16 * c) * long(16 * c)) px = c, rows = 16;
  }
  if (size == 512L * 512L) px = 64, cols = 8, rows = 8;
  if (px == 0 || cell >= cols * rows) {
    std::fclose(f);
    return out;
  }
  const long stride = long(cols) * px;
  const int cx = cell % cols, cy = cell / cols;
  std::vector<uint8_t> row(static_cast<size_t>(px));
  out.resize(static_cast<size_t>(px) * px);
  bool any = false;
  for (int y = 0; y < px; ++y) {
    std::fseek(f, (long(cy) * px + y) * stride + long(cx) * px, SEEK_SET);
    if (std::fread(row.data(), 1, row.size(), f) != row.size()) {
      std::fclose(f);
      return {};
    }
    for (int x = 0; x < px; ++x) {
      out[static_cast<size_t>(y) * px + x] = (row[x] - 128.0f) / 127.0f * 0.25f;
      any = any || row[x] < 128;
    }
  }
  std::fclose(f);
  if (!any) return {};
  *side = px;
  return out;
}

// The Strikers characters' motifs (xr_fx_mesh.frag motif(), the same shapes): distance, cell units, inside < 0.
float sd_star5(float px, float py, float r, float rf) {
  const float k1x = 0.809016994375f, k1y = -0.587785252292f, k2x = -k1x, k2y = k1y;
  px = std::fabs(px);
  float d = std::max(k1x * px + k1y * py, 0.0f);
  px -= 2.0f * d * k1x;
  py -= 2.0f * d * k1y;
  d = std::max(k2x * px + k2y * py, 0.0f);
  px -= 2.0f * d * k2x;
  py -= 2.0f * d * k2y;
  px = std::fabs(px);
  py -= r;
  const float bax = rf * -k1y - 0.0f, bay = rf * k1x - 1.0f;
  const float h = clampf((px * bax + py * bay) / (bax * bax + bay * bay), 0.0f, r);
  const float qx = px - bax * h, qy = py - bay * h;
  const float s = py * bax - px * bay;
  return std::sqrt(qx * qx + qy * qy) * (s < 0.0f ? -1.0f : 1.0f);
}
float sd_heart(float px, float py) {
  px = std::fabs(px);
  if (py + px > 1.0f) return std::sqrt((px - 0.25f) * (px - 0.25f) + (py - 0.75f) * (py - 0.75f)) - std::sqrt(2.0f) / 4.0f;
  const float m = 0.5f * std::max(px + py, 0.0f);
  const float a = px * px + (py - 1.0f) * (py - 1.0f), b = (px - m) * (px - m) + (py - m) * (py - m);
  return std::sqrt(std::min(a, b)) * (px - py < 0.0f ? -1.0f : 1.0f);
}
float sd_hexagon(float px, float py, float r) {
  const float kx = -0.866025404f, ky = 0.5f, kz = 0.577350269f;
  px = std::fabs(px);
  py = std::fabs(py);
  const float d = std::min(kx * px + ky * py, 0.0f);
  px -= 2.0f * d * kx;
  py -= 2.0f * d * ky;
  px -= clampf(px, -kz * r, kz * r);
  py -= r;
  return std::sqrt(px * px + py * py) * (py < 0.0f ? -1.0f : 1.0f);
}
float sd_flower(float px, float py, float r) {
  float d = std::sqrt(px * px + py * py) - r * 0.55f;
  for (int i = 0; i < 5; ++i) {
    const float a = 6.2831853f * i / 5.0f;
    const float dx = px - r * std::sin(a), dy = py - r * std::cos(a);
    d = std::min(d, std::sqrt(dx * dx + dy * dy) - r * 0.62f);
  }
  return d;
}
// (p up = +y, as the shader's cells)
float motif(int klass, float x, float y) {
  const int c = klass;
  if (c == 0 || c == 7) return sd_heart(x * 1.6f, (y + 0.32f) * 1.6f) / 1.6f;
  if (c == 1) return sd_flower(x, y, 0.2f);
  if (c == 2) return (std::fabs((x + y) * 1.5f - std::floor((x + y) * 1.5f) - 0.5f) - 0.13f) / 1.5f;
  if (c == 4) return sd_hexagon(x, y, 0.3f);
  if (c == 8 || c == 11) return std::sqrt(x * x + y * y) - (c == 11 ? 0.27f : 0.22f);
  if (c == 9) return (std::fabs(x) + std::fabs(y) - 0.34f) * 0.7071f;
  if (c == 10) {
    const float f = x * 1.5f - std::floor(x * 1.5f);
    return std::fabs(y - 0.2f * (std::fabs(f - 0.5f) * 4.0f - 1.0f)) - 0.07f;
  }
  return sd_star5(x, y, 0.36f, 0.45f);
}

// The character's emblem, etched (as on your controllers / the home ground's banners): a badge in its colour, a rim in
// its second colour, its own outline (a guest) or its motif (a Strikers character) inlaid with a dark etched line.
ImgP emblem(const Card& c) {
  constexpr int S = 200;
  int side = 0;
  const std::vector<float> cell = c.klass < 0 ? etch_cell(c.etch, &side) : std::vector<float>();
  auto img = std::make_shared<Img>();
  img->w = img->h = S;
  img->rgba.assign(static_cast<size_t>(S) * S * 4, 0);
  const uint32_t body = c.c1, inlay = luma(c.c2) < 0.25f ? light(c.c2, 0.6f) : c.c2;
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
      const float u = (x + 0.5f) / S - 0.5f, v = (y + 0.5f) / S - 0.5f;  // v down
      const float r = std::sqrt(u * u + v * v);
      const float cover = clampf((0.485f - r) * S + 0.5f, 0.0f, 1.0f);
      if (cover <= 0.0f) continue;
      uint32_t col = mix_rgb(light(body, 0.25f), dark(body, 0.35f), (v + 0.5f));
      // the rim
      const float rim = clampf(1.0f - (std::fabs(r - 0.44f) - 0.03f) * S, 0.0f, 1.0f);
      col = mix_rgb(col, c.c2, rim);
      col = mix_rgb(col, 0x000000, 0.5f * clampf(1.0f - std::fabs(r - 0.405f) * S * 0.5f, 0.0f, 1.0f));
      // the motif, 0.66 of the badge across
      const float qx = u / 0.66f, qy = -v / 0.66f;
      float d = 1.0f;
      if (!cell.empty() && std::fabs(qx) <= 0.5f && std::fabs(qy) <= 0.5f) {
        const int cx = std::min(side - 1, static_cast<int>((qx + 0.5f) * side));
        const int cy = std::min(side - 1, static_cast<int>((0.5f - qy) * side));
        d = cell[static_cast<size_t>(cy) * side + cx];
      } else if (c.klass >= 0 && std::fabs(qx) <= 0.5f && std::fabs(qy) <= 0.5f) {
        d = motif(c.klass, qx, qy);
      }
      const float aa = 1.5f / (S * 0.66f);
      const float in = clampf(0.5f - d / aa, 0.0f, 1.0f);
      col = mix_rgb(col, inlay, 0.9f * in);
      const float line = clampf(1.0f - (std::fabs(d) - 0.012f) / aa, 0.0f, 1.0f);
      col = mix_rgb(col, dark(body, 0.75f), 0.85f * line);
      uint8_t* p = &img->rgba[(static_cast<size_t>(y) * S + x) * 4];
      p[0] = (col >> 16) & 255;
      p[1] = (col >> 8) & 255;
      p[2] = col & 255;
      p[3] = static_cast<uint8_t>(std::lround(cover * 255.0f));
    }
  return img;
}

// xr_quest.cpp's Pieces (the same calls, so the layouts read alike and port line for line to tools\outside\mag_board.py)
struct Pc {
  std::vector<xrtext::BoardPiece> v;
  std::vector<ImgP> keep;  // the pictures the pieces point into
  float ox = 0.0f, oy = 0.0f, k = 1.0f;
  float tSlant = 0.0f, tXscale = 1.0f, tAngle = 0.0f, tShadow = 0.0f, tShadowA = 0.55f;
  uint32_t tShadowRgb = 0x000000, tRim = 0x05070C;
  void plain() {
    tSlant = tAngle = tShadow = 0.0f;
    tXscale = 1.0f;
    tShadowA = 0.55f;
    tShadowRgb = 0x000000;
    tRim = 0x05070C;
  }
  void style(float slant, float xscale = 1.0f, float angle = 0.0f, float shadow = 0.0f, uint32_t shadowRgb = 0x000000,
             float shadowA = 0.55f) {
    tSlant = slant;
    tXscale = xscale;
    tAngle = angle;
    tShadow = shadow;
    tShadowRgb = shadowRgb;
    tShadowA = shadowA;
  }
  void rim(uint32_t rgb) { tRim = rgb; }
  xrtext::BoardPiece& add(xrtext::BoardPiece::Kind kind, float x0, float y0, float x1, float y1) {
    xrtext::BoardPiece p;
    p.kind = kind;
    p.x0 = ox + x0 * k;
    p.y0 = oy + y0 * k;
    p.x1 = ox + x1 * k;
    p.y1 = oy + y1 * k;
    v.push_back(p);
    return v.back();
  }
  void box(float x0, float y0, float x1, float y1, float r, uint32_t rgb, float a = 1.0f, uint32_t rgb2 = 0, float a2 = -1.0f) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kBox, x0, y0, x1, y1);
    p.radius = r * k;
    p.rgb = rgb;
    p.alpha = a;
    p.rgb2 = rgb2;
    p.alpha2 = a2;
  }
  void ring(float x0, float y0, float x1, float y1, float r, float width, uint32_t rgb, float a = 1.0f) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kRing, x0, y0, x1, y1);
    p.radius = r * k;
    p.outline = width * k;
    p.rgb = rgb;
    p.alpha = a;
  }
  void glow(float x0, float y0, float x1, float y1, uint32_t rgb, float a) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kGlow, x0, y0, x1, y1);
    p.rgb = rgb;
    p.alpha = a;
  }
  void image(const ImgP& g, float x0, float y0, float x1, float y1, bool bottom, float a = 1.0f, uint32_t silhouette = 0) {
    if (!g || g->empty()) return;
    keep.push_back(g);
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kImage, x0, y0, x1, y1);
    p.img = g->rgba.data();
    p.iw = g->w;
    p.ih = g->h;
    p.bottom = bottom;
    p.alpha = a;
    p.silhouette = silhouette != 0;
    p.rgb = silhouette;
  }
  // a cut-out: its see-through margins trimmed, fitted with its feet on the box's bottom (or cropped to fill it);
  // duotone dark -> light; silhouette != 0: in that colour
  void figure(const ImgP& g, float x0, float y0, float x1, float y1, bool crop, float fx, float fy, bool flip = false,
              uint32_t darkRgb = 0, uint32_t lightRgb = 0, float a = 1.0f, uint32_t silhouette = 0) {
    if (!g || g->empty()) return;
    keep.push_back(g);
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kImage, x0, y0, x1, y1);
    p.img = g->rgba.data();
    p.iw = g->w;
    p.ih = g->h;
    p.bottom = true;
    p.alpha = a;
    p.trim = true;
    p.crop = crop;
    p.focusX = fx;
    p.focusY = fy;
    p.flip = flip;
    p.duo = (darkRgb | lightRgb) != 0 && silhouette == 0;
    p.rgb = silhouette != 0 ? silhouette : darkRgb;
    p.rgb2 = lightRgb;
    p.silhouette = silhouette != 0;
  }
  // r140 (Lance: "make the pokemon/characters be in the middle of the card always, theres a couple where theyre at the
  // bottome or at the top just looking awkward"): a cut-out by what you can see of it (its see-through margins trimmed:
  // a big picture is framed like a portrait, its feet on a baseline), fitted whole and centred in the box both ways at
  // `fill` of it on its limiting side - every character in the middle at a like size
  void centred(const ImgP& g, float x0, float y0, float x1, float y1, float fill, uint32_t silhouette = 0) {
    if (!g || g->empty()) return;
    const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1), hw = 0.5f * (x1 - x0) * fill, hh = 0.5f * (y1 - y0) * fill;
    figure(g, cx - hw, cy - hh, cx + hw, cy + hh, false, 0.5f, 0.5f, false, 0, 0, 1.0f, silhouette);
    v.back().bottom = false;
  }
  void poly(std::initializer_list<float> xy, uint32_t rgb, float a = 1.0f) {
    xrtext::BoardPiece p;
    p.kind = xrtext::BoardPiece::kPoly;
    int i = 0;
    for (float f : xy) p.pts.push_back((i++ % 2) == 0 ? ox + f * k : oy + f * k);
    p.rgb = rgb;
    p.alpha = a;
    v.push_back(p);
  }
  void burst(float x0, float y0, float x1, float y1, int points, float inner, float angle, uint32_t rgb, float a = 1.0f) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kBurst, x0, y0, x1, y1);
    p.points = points;
    p.inner = inner;
    p.angle = angle;
    p.rgb = rgb;
    p.alpha = a;
  }
  void dots(float x0, float y0, float x1, float y1, float cell, float f0, float f1, uint32_t rgb, float a) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kDots, x0, y0, x1, y1);
    p.radius = cell * k;
    p.dot0 = f0;
    p.dot1 = f1;
    p.rgb = rgb;
    p.alpha = a;
  }
  void words(xrtext::BoardPiece::Kind kind, const std::string& t, float x, float y, float px, float room, uint32_t rgb,
             float outline = 0.0f, uint32_t rgb2 = 0) {
    xrtext::BoardPiece p;
    p.kind = kind;
    p.x0 = ox + x * k;
    p.y0 = oy + y * k;
    p.x1 = room * k;
    p.y1 = px * k;
    p.rgb = rgb;
    p.rgb2 = rgb2;
    p.outline = outline * k;
    p.outlineRgb = tRim;
    p.text = t;
    p.slant = tSlant;
    p.xscale = tXscale;
    p.angle = tAngle;
    p.shadow = tShadow * k;
    p.shadowRgb = tShadowRgb;
    p.shadowA = tShadowA;
    v.push_back(p);
  }
  void text(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline = 0.0f, uint32_t rgb2 = 0) {
    words(xrtext::BoardPiece::kText, t, x, y, px, room, rgb, outline, rgb2);
  }
  void left(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline = 0.0f, uint32_t rgb2 = 0) {
    words(xrtext::BoardPiece::kTextLeft, t, x, y, px, room, rgb, outline, rgb2);
  }
  void right(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline = 0.0f, uint32_t rgb2 = 0) {
    words(xrtext::BoardPiece::kTextRight, t, x, y, px, room, rgb, outline, rgb2);
  }
  float width(const std::string& t, float px) const { return xrtext::board_text_width(t, px * k) / k * tXscale; }
};

// ------------------------------------------------------------------------------------- the LANCINI TIZM look
// The parallels: chrome card stock with a refractor's prism (facets fanning out behind the character, the foil in their
// alpha: xr_fx_model.frag's card mode turns them into a moving rainbow as the card tilts): SILVER TIZM (a strong
// refractor), GOLD TIZM (gold chrome), BLACK TIZM (black chrome). r138: BASE is a plain printed card (no chrome, no foil;
// its row here only for the binder's frames).
constexpr float kCW = 1080.0f, kCH = 1512.0f;
constexpr int kFrontW = 1080, kFrontH = 1512, kBackW = 768, kBackH = 1075, kPackW = 900, kPackH = 1380;
struct Stock {
  uint32_t s1, s2, lite, mid, dark, ring;
  float fFacet, fFrame;  // its foil: the prism behind the character, the frame
};
const Stock kStock[4] = {{0xF6F6F3, 0xE4E6EA, 0xF6F8FC, 0xB4BCCA, 0x6E7788, 0xFFFFFF, 0.0f, 0.0f},     // BASE (printed)
                         {0xF7F9FF, 0xBFC8D8, 0xFFFFFF, 0xD6DDEA, 0x98A4B8, 0xFFFFFF, 0.85f, 0.95f},   // SILVER TIZM
                         {0xFFE27A, 0xA5760A, 0xFFF2BE, 0xE8B83A, 0x86600A, 0xFFEBA8, 0.85f, 1.00f},   // GOLD TIZM /10
                         {0x343842, 0x07080B, 0x535A68, 0x262A33, 0x0B0C10, 0xB4BAC4, 0.75f, 0.80f}};  // BLACK TIZM 1/1
// (r138: the BLACK's foil all under kHoloFoil - black chrome in its obsidian shell, xr_fx_model.frag - but its 1/1 stamp
// full: the one holographic thing on it)
constexpr uint32_t kFinishPill[4] = {0xFFFFFF, 0xE8EDF5, 0xFFD23F, 0x111318};
constexpr uint32_t kFinishInk[4] = {0x111111, 0x1A2233, 0x3A2600, 0xFFFFFF};

uint32_t gray(float f) { return static_cast<uint32_t>(std::lround(clampf(f, 0.0f, 1.0f) * 255.0f)) * 0x010101u; }

void polyv(Pc& P, const std::vector<float>& xy, uint32_t rgb, float a = 1.0f) {
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kPoly;
  for (size_t i = 0; i < xy.size(); ++i) p.pts.push_back(i % 2 == 0 ? P.ox + xy[i] * P.k : P.oy + xy[i] * P.k);
  p.rgb = rgb;
  p.alpha = a;
  P.v.push_back(p);
}
// r146: a five-pointed star at (cx, cy), r its points' radius (a dark rim round it first: it reads on anything)
void star_shape(Pc& P, float cx, float cy, float r, uint32_t rgb, bool rim = true) {
  for (int pass = rim ? 0 : 1; pass < 2; ++pass) {
    const float rr = pass == 0 ? r * 1.28f : r;
    std::vector<float> xy;
    for (int i = 0; i < 10; ++i) {
      const float a = -1.5707963f + 0.62831853f * i, d = i % 2 == 0 ? rr : rr * 0.45f;
      xy.push_back(cx + d * std::cos(a));
      xy.push_back(cy + d * std::sin(a));
    }
    // (a star isn't convex: its five points and its middle, each convex)
    for (int i = 0; i < 5; ++i) {
      const int a = 2 * i, b = (2 * i + 1) % 10, c = (2 * i + 9) % 10;
      polyv(P, {xy[2 * c], xy[2 * c + 1], xy[2 * a], xy[2 * a + 1], xy[2 * b], xy[2 * b + 1]}, pass == 0 ? 0x0B0F1A : rgb);
    }
    polyv(P, {xy[2], xy[3], xy[6], xy[7], xy[10], xy[11], xy[14], xy[15], xy[18], xy[19]}, pass == 0 ? 0x0B0F1A : rgb);
  }
}

// r149: a SIGNED card's certificate sticker - a holographic oval (all foil: render()'s mask), LANCINI TIZM CERTIFIED
// AUTOGRAPH round a little gem
void certified_sticker(Pc& P, float x0, float y0, float x1, float y1) {
  const float cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1), h = y1 - y0, w = x1 - x0;
  P.box(x0 - 4, y0 - 4, x1 + 4, y1 + 4, 0.5f * h + 4, 0x0B0F1A);
  P.box(x0, y0, x1, y1, 0.5f * h, 0xF2F4F8, 1.0f, 0xB8C0D0);
  P.ring(x0 + 6, y0 + 6, x1 - 6, y1 - 6, 0.5f * h - 6, 3, 0x6A7488);
  const float g = 0.22f * h, gx = x0 + 0.17f * w;
  P.poly({gx, cy - g, gx + 0.866f * g, cy - 0.5f * g, gx + 0.866f * g, cy + 0.5f * g, gx, cy + g, gx - 0.866f * g, cy + 0.5f * g,
          gx - 0.866f * g, cy - 0.5f * g}, 0x5A6682);
  P.left("LANCINI TIZM", x0 + 0.31f * w, cy - 0.27f * h, 0.17f * h, 0.66f * w, 0x1A2233, 1);
  P.left("CERTIFIED", x0 + 0.31f * w, cy + 0.02f * h, 0.24f * h, 0.66f * w, 0x0B1022, 2);
  P.left("AUTOGRAPH", x0 + 0.31f * w, cy + 0.29f * h, 0.19f * h, 0.66f * w, 0x7A1AA0, 1);
}

// A convex polygon (x, y pairs) cut to a box (Sutherland-Hodgman)
std::vector<float> clip_rect(std::vector<float> poly, float x0, float y0, float x1, float y1) {
  for (int e = 0; e < 4 && poly.size() >= 6; ++e) {
    std::vector<float> out;
    const size_t n = poly.size() / 2;
    auto inside = [&](float x, float y) { return e == 0 ? x >= x0 : (e == 1 ? x <= x1 : (e == 2 ? y >= y0 : y <= y1)); };
    for (size_t i = 0; i < n; ++i) {
      const float ax = poly[2 * i], ay = poly[2 * i + 1], bx = poly[2 * ((i + 1) % n)], by = poly[2 * ((i + 1) % n) + 1];
      const bool ain = inside(ax, ay), bin = inside(bx, by);
      if (ain) {
        out.push_back(ax);
        out.push_back(ay);
      }
      if (ain != bin) {
        const float t = e < 2 ? ((e == 0 ? x0 : x1) - ax) / (bx - ax) : ((e == 2 ? y0 : y1) - ay) / (by - ay);
        out.push_back(ax + (bx - ax) * t);
        out.push_back(ay + (by - ay) * t);
      }
    }
    poly = out;
  }
  return poly;
}
// The refractor's prism: facets fanning out from (cx, cy) to the box's edges, three shades in turn, cut to the box.
void prism(Pc& P, float x0, float y0, float x1, float y1, float cx, float cy, int n, float turn, uint32_t lite, uint32_t mid,
           uint32_t dk, float a) {
  for (int k = 0; k < n; ++k) {
    const float a0 = turn + 6.2831853f * k / n, a1 = turn + 6.2831853f * (k + 1) / n;
    const std::vector<float> c = clip_rect({cx, cy, cx + 4000.0f * std::cos(a0), cy + 4000.0f * std::sin(a0),
                                            cx + 4000.0f * std::cos(a1), cy + 4000.0f * std::sin(a1)},
                                           x0, y0, x1, y1);
    if (c.size() >= 6) polyv(P, c, k % 3 == 0 ? lite : (k % 3 == 1 ? mid : dk), a);
  }
}
// LANCINI TIZM's mark (Lance's own parody brand, our own drawing): a six-facet gem, LANCINI over TIZM, TIZM leaning in
// chrome with a cyan / magenta split at its edges (light through a prism). (x, y) its top left, k its size (1: the gem
// 116 px, TIZM 76 px, about 370 px wide). `mask`: its foil (the gem; never the words).
void logo(Pc& P, float x, float y, float k, bool mask) {
  const float r = 58.0f * k, cx = x + 58.0f * k, cy = y + 62.0f * k;
  static const uint32_t kFacet[6] = {0xFFFFFF, 0xD9E0EE, 0x8E9BB4, 0x5A6682, 0xA6B3CC, 0xEEF2FA};
  if (!mask) {  // its dark edge (it reads as a gem on any foil)
    const float e = r + 6.0f * k;
    P.poly({cx, cy - e, cx + 0.866f * e, cy - 0.5f * e, cx + 0.866f * e, cy + 0.5f * e, cx, cy + e, cx - 0.866f * e, cy + 0.5f * e,
            cx - 0.866f * e, cy - 0.5f * e}, 0x0B0F1A);
  }
  for (int i = 0; i < 6; ++i) {
    const float a0 = -1.5707963f + 1.0471976f * i, a1 = a0 + 1.0471976f;
    P.poly({cx, cy, cx + r * std::cos(a0), cy + r * std::sin(a0), cx + r * std::cos(a1), cy + r * std::sin(a1)},
           mask ? 0xFFFFFF : kFacet[i]);
  }
  const float tx = x + 132.0f * k;
  if (mask) return;  // (its words take the foil under them: white, outlined, they read on it)
  P.left("LANCINI", tx + 4.0f * k, y + 34.0f * k, 40.0f * k, 230.0f * k, 0xFFFFFF, 3.0f * k);
  P.style(0.24f, 0.92f);
  P.left("TIZM", tx - 5.0f * k, y + 92.0f * k, 76.0f * k, 230.0f * k, 0x22E0FF);
  P.v.back().alpha = 0.85f;
  P.left("TIZM", tx + 5.0f * k, y + 100.0f * k, 76.0f * k, 230.0f * k, 0xFF2AD4);
  P.v.back().alpha = 0.85f;
  P.rim(0x0B0F1A);
  P.left("TIZM", tx, y + 96.0f * k, 76.0f * k, 230.0f * k, 0xFFFFFF, 4.0f * k, 0x9AA6BC);
  P.plain();
}

// ------------------------------------------------------------------------------------------- the card's front
// 1080 x 1512 (2.5 : 3.5): its chrome stock and prism in its finish, its team colours' light, the character big (xr_big:
// its action pose); LANCINI TIZM's mark and its finish (a numbered one stamped 07/10, 1/1), its rating and series; its
// name on a leaning bar with its emblem; the four franchise stats; the set and its number. `mask`: the same shapes in
// grey = how much foil, for the card's alpha (render()).
// r138: `special` (the special card's own front, its 3D in its own model): no picture of the character (it's in 3D: in
// the window's room, standing on top of the card, inside the obsidian); a window / obsidian card its window cut through
// (the mask's green: render() makes it see-through; the mark, pills and badges over it stay)
enum { kSpNone = 0, kSpWindow = 1, kSpPop = 2, kSpGlass = 3, kSpBoom = 4, kSpSign = 5 };  // (r149: a TIZM BOOM, a SIGNED BASE)
constexpr float kHoleX0 = 70, kHoleY0 = 150, kHoleX1 = 1010, kHoleY1 = 1110;  // the window (px of the 1080 x 1512 front)
// r140: the character's picture centred in the same box (the window's: between the mark / rating and the name bar), on its
// limiting side kArtFill of it
constexpr float kArtFill = 0.88f;
// r141 (Lance's r140 clip: Monty Mole's card was his Charged menu icon - an opaque face close-up - blown up to the whole
// art box): a picture with no see-through background (the old portraits, when a big picture can't be made) is a
// photo, fitted whole in a white frame at kPhotoFill, never filling / cropping the art box
constexpr float kPhotoFill = 0.72f;
bool opaque_art(const ImgP& g) {
  if (!g || g->empty()) return false;
  const int w = g->w, h = g->h;
  const int at[4][2] = {{1, 1}, {w - 2, 1}, {1, h - 2}, {w - 2, h - 2}};
  int solid = 0;
  for (const auto& p : at) solid += g->rgba[(static_cast<size_t>(p[1]) * w + p[0]) * 4 + 3] > 240 ? 1 : 0;
  return solid >= 3;
}
// the character's picture on a card front (mask: its foil's: none under it)
void card_art_piece(Pc& P, const ImgP& art, bool mask) {
  if (!opaque_art(art)) {
    if (mask) P.centred(art, kHoleX0, kHoleY0, kHoleX1, kHoleY1, kArtFill, 0x010101);
    else P.centred(art, kHoleX0, kHoleY0, kHoleX1, kHoleY1, kArtFill);
    return;
  }
  const float cx = 0.5f * (kHoleX0 + kHoleX1), cy = 0.5f * (kHoleY0 + kHoleY1);
  const float s = kPhotoFill * std::min(kHoleX1 - kHoleX0, kHoleY1 - kHoleY0) * 0.5f;
  const float r = s * std::min(1.0f, static_cast<float>(art->h) / art->w), q = s * std::min(1.0f, static_cast<float>(art->w) / art->h);
  if (mask) {
    P.box(cx - q - 22, cy - r - 22, cx + q + 22, cy + r + 22, 26, gray(0));
    return;
  }
  P.box(cx - q - 14, cy - r - 4, cx + q + 30, cy + r + 34, 30, 0x000000, 0.35f);  // (its shadow)
  P.box(cx - q - 22, cy - r - 22, cx + q + 22, cy + r + 22, 26, 0xFFFFFF, 1.0f, 0xE4E6EA);
  P.image(art, cx - q, cy - r, cx + q, cy + r, false);
}
// r146: `level` its level (the copies you keep, card_level): from LV2 that many stars on a pill under its series
void card_front(Pc& P, const Card& c, const Ref& r, const ImgP& art, const ImgP& badge, int total, bool mask, int special = kSpNone,
                int level = 0) {
  const float W = kCW, H = kCH;
  const Stock& st = kStock[r.parallel];
  const float bx = W - 132, by = 142;  // the rating badge's middle
  float pillY = 196;                   // the finish / shiny pills, under the mark
  const bool stamped = r.parallel == kGoldTizm || r.parallel == kBlack;
  const std::string stamp = r.parallel == kBlack ? "1/1" : (r.serial < 10 ? "0" : "") + std::to_string(r.serial) + "/10";
  if (mask) {
    P.box(0, 0, W, H, 48, gray(st.fFrame));
    P.box(26, 26, W - 26, H - 26, 36, gray(st.fFacet));
    if (special == kSpWindow || special == kSpGlass) {
      P.box(kHoleX0, kHoleY0, kHoleX1, kHoleY1, 24, 0x00FF00);  // the window
      P.box(52, 44, 430, 180, 20, gray(st.fFacet));             // (the mark over it stays)
    }
    if (special == kSpNone || special == kSpPop) card_art_piece(P, art, true);
    logo(P, 52, 44, 1.0f, true);
    if (r.parallel == kBlack) {  // r138: the BLACK's gem black chrome too (only its 1/1 stamp holographic)
      const float gx = 52.0f + 58.0f, gy = 44.0f + 62.0f, gr = 58.0f;
      std::vector<float> hex;
      for (int i = 0; i < 6; ++i) {
        hex.push_back(gx + gr * std::cos(-1.5707963f + 1.0471976f * i));
        hex.push_back(gy + gr * std::sin(-1.5707963f + 1.0471976f * i));
      }
      polyv(P, hex, gray(st.fFrame));
    }
    P.box(bx - 92, by - 92, bx + 92, by + 92, 92, gray(0));
    P.box(bx - 104, by + 102, bx + 104, by + 158, 28, gray(0));
    if (level > 1) {  // r146: its level's pill solid (to read)
      const float lw = level * 46.0f + 26.0f, lx = std::min(bx, W - 40 - lw / 2);
      P.box(lx - lw / 2, by + 168, lx + lw / 2, by + 226, 29, gray(0));
    }
    if (r.parallel != kBase) {
      const float pw = P.width(kParallelName[r.parallel], 40) + 56;
      P.box(52, pillY, 52 + pw, pillY + 58, 29, gray(0));  // (the pills solid: to read)
      pillY += 70;
    }
    if (r.shiny) {
      P.box(52, pillY, 232, pillY + 58, 29, gray(0));
    }
    if (stamped) {  // (a foil stamp, its number solid; r138: the BLACK's 1/1 all holographic)
      P.box(W - 254, 1036, W - 54, 1114, 14, gray(r.parallel == kBlack ? 1.0f : 0.5f));
      P.text(stamp, W - 154, 1075, 58, 180, gray(0), 2);
    }
    P.poly({26, 1160, W - 26, 1124, W - 26, 1270, 26, 1306}, gray(0));
    P.image(badge, W - 214, 1116, W - 44, 1286, false, 1.0f, 0x010101);
    P.box(26, 1316, W - 26, H - 26, 36, gray(0));
    if (r.hit == kHitSigned) P.box(60, 958, 400, 1094, 68, gray(1.0f));  // r149: the certified sticker - all holographic
    return;
  }
  if (r.parallel == kBase) {
    // r138 (Lance: "i want a CLEAR distinction between whats base and whats a parralell"): BASE is a plain printed card -
    // white card stock round a panel in the team's colours, a soft light, halftone dots; no chrome, no prism, no foil
    // (render() gives it none): anything that shines is special
    P.box(0, 0, W, H, 48, 0xF6F6F3, 1.0f, 0xE4E6EA);
    P.box(34, 34, W - 34, 1312, 30, light(c.c1, 0.12f), 1.0f, dark(c.c1, 0.42f));
    P.glow(120, 220, W - 120, 1150, 0xFFFFFF, 0.38f);
    P.dots(34, 900, W - 34, 1312, 22, 0.05f, 0.34f, dark(c.c1, 0.6f), 0.45f);
    if (special == kSpNone || special == kSpPop) card_art_piece(P, art, false);
    P.ring(34, 34, W - 34, 1312, 30, 3, 0xFFFFFF, 0.85f);
  } else {
    // a parallel: its chrome stock, its prism (its foil: render()'s mask), the team's light, the character
    P.box(0, 0, W, H, 48, st.s1, 1.0f, st.s2);
    prism(P, 26, 26, W - 26, H - 26, W / 2, 600, 30, 0.11f, st.lite, st.mid, st.dark, 0.55f);
    P.glow(-60, 140, W + 60, 1180, c.c1, 0.6f);
    P.glow(160, 300, W - 160, 1080, 0xFFFFFF, 0.3f);
    if (special == kSpNone || special == kSpPop) card_art_piece(P, art, false);
    P.ring(12, 12, W - 12, H - 12, 46, 14, st.ring);
    P.ring(26, 26, W - 26, H - 26, 36, 3, st.dark, 0.7f);
  }
  // the mark; its finish, shiny; its rating, its series
  logo(P, 52, 44, 1.0f, false);
  if (r.parallel != kBase) {
    const float pw = P.width(kParallelName[r.parallel], 40) + 56;
    P.box(52, pillY, 52 + pw, pillY + 58, 29, kFinishPill[r.parallel]);
    P.text(kParallelName[r.parallel], 52 + pw / 2, pillY + 29, 40, pw - 24, kFinishInk[r.parallel], 2);
    pillY += 70;
  }
  if (r.shiny) {
    P.box(52, pillY, 232, pillY + 58, 29, 0xFFFFFF);
    P.text("SHINY", 142, pillY + 29, 40, 160, 0x2A0A5A, 2);
  }
  P.box(bx - 92, by - 92, bx + 92, by + 92, 92, kNavy, 0.92f);
  P.ring(bx - 92, by - 92, bx + 92, by + 92, 92, 8, r.parallel == kGoldTizm ? kGold : st.ring);
  P.text(std::to_string(c.rating), bx, by - 14, 88, 140, kGold, 4);
  P.text("OVR", bx, by + 50, 40, 120, 0xE8ECF5, 2);
  const float sw = std::min(208.0f, P.width(kSeriesName[c.series], 40) + 48);
  P.box(bx - sw / 2, by + 102, bx + sw / 2, by + 158, 28, kSeriesRgb[c.series]);
  P.text(kSeriesName[c.series], bx, by + 130, 40, sw - 24, kSeriesInk[c.series], 2);
  if (level > 1) {  // r146: its level: that many gold stars (inside the frame at LV5)
    const float lw = level * 46.0f + 26.0f, lx = std::min(bx, W - 40 - lw / 2);
    P.box(lx - lw / 2, by + 168, lx + lw / 2, by + 226, 29, kNavy, 0.94f);
    P.ring(lx - lw / 2, by + 168, lx + lw / 2, by + 226, 29, 3, kGold, 0.9f);
    for (int s = 0; s < level; ++s) star_shape(P, lx - (level - 1) * 23.0f + s * 46.0f, by + 197, 19, kGold);
  }
  if (stamped) {  // its number in its run, stamped in foil
    P.box(W - 254, 1036, W - 54, 1114, 14, r.parallel == kBlack ? 0x16181E : 0xFFE27A, 1.0f, r.parallel == kBlack ? 0x05060A : 0xC8961E);
    P.ring(W - 254, 1036, W - 54, 1114, 14, 4, r.parallel == kBlack ? 0xB4BAC4 : 0x3A2600);
    P.text(stamp, W - 154, 1075, 58, 180, r.parallel == kBlack ? 0xFFFFFF : 0x3A2600, 2);
  }
  // its name on a leaning bar in its colours, its emblem at the end
  P.poly({26, 1146, W - 26, 1110, W - 26, 1128, 26, 1164}, c.c2);
  P.poly({26, 1160, W - 26, 1124, W - 26, 1270, 26, 1306}, dark(c.c1, 0.45f));
  P.style(0.16f, 0.92f, -1.9f, 6, 0x000000, 0.5f);
  if (c.sub.empty()) {
    P.text(upper(c.name), 455, 1222, 92, 760, 0xFFFFFF, 6);
  } else {  // r152: a Marvel costume's own name, its hero under it
    P.text(upper(c.name), 455, 1196, 80, 760, 0xFFFFFF, 6);
    P.text(upper(c.sub), 455, 1262, 42, 600, kGold, 4);
  }
  P.plain();
  P.image(badge, W - 214, 1116, W - 44, 1286, false);
  // the franchise stats (assets/franchise/cards.txt): five pips each, two by two
  P.box(26, 1316, W - 26, H - 26, 36, kNavy, 0.94f);
  static const char* const kStat[4] = {"SPEED", "SHOOTING", "PASSING", "SLIDE TACKLE"};
  for (int i = 0; i < 4; ++i) {
    const float lx = i % 2 == 0 ? 64 : 552, px0 = i % 2 == 0 ? 340 : 846, y = 1352 + (i / 2) * 58.0f;
    P.left(kStat[i], lx, y, 40, i % 2 == 0 ? 260 : 280, 0xC8D2E6, 2);
    const int lit = std::max(1, std::min(5, static_cast<int>(std::lround(c.stats[i] * 5.0f))));
    for (int s = 0; s < 5; ++s) P.box(px0 + s * 34.0f, y - 14, px0 + s * 34.0f + 26, y + 14, 7, s < lit ? kGold : 0x2A3354);
  }
  // the set, its number, who it is (C / R / SR)
  P.box(44, 1424, W - 44, 1428, 2, 0x2A3354);
  P.left(kSetName, 64, 1458, 40, 640, 0x8A94B8);
  P.right(num3(c.num) + "/" + std::to_string(total), W - 64, 1458, 44, 220, kGold, 2);
  const float rw = P.width(num3(c.num) + "/" + std::to_string(total), 44);
  P.box(W - 64 - rw - 90, 1436, W - 64 - rw - 18, 1480, 22, c.rarity == kSuper ? 0x8A3CE0 : (c.rarity == kRare ? 0xC8961E : 0x3A4A70));
  P.text(kRarityShort[c.rarity], W - 64 - rw - 54, 1458, 34, 64, 0xFFFFFF);
  if (r.hit == kHitSigned) certified_sticker(P, 60, 958, 400, 1094);  // r149: SIGNED - its autograph's certificate
}

// The card's back, 768 x 1075 (drawn in the front's 1080 x 1512, scaled): black chrome and its prism, LANCINI TIZM's
// mark big, the set. (No name, no number (r212): the backs show while a pack's cards wait to be turned over.)
// (r147, Lance: "i dont like the fact that you added the "level 2" shit on the BACK of the card, meaning i see it FIRST":
// the back is the plain LANCINI TIZM back again - nothing of its level / copies before it's turned over; that's shown by
// its front's stars and level_tag's moment as it turns over)
void card_back(Pc& P, const Card& c, int total) {
  const float W = kCW, H = kCH;
  P.box(0, 0, W, H, 48, 0x23283A, 1.0f, 0x06070C);
  prism(P, 26, 26, W - 26, H - 26, W / 2, H * 0.42f, 26, 0.0f, 0x3A4258, 0x1C2132, 0x0A0C12, 0.7f);
  P.ring(12, 12, W - 12, H - 12, 46, 14, 0xC9CED8);
  P.ring(40, 40, W - 40, H - 40, 32, 4, 0x8A94AA, 0.8f);
  P.glow(140, 160, W - 140, 760, 0x5AD8FF, 0.25f);
  logo(P, 118, 290, 2.3f, false);
  P.text("2026-27", W / 2, 720, 70, 600, kGold, 4);
  P.text("LANCINI TIZM STRIKERS", W / 2, 810, 72, W - 140, 0xFFFFFF, 4);
  P.text("SERIES 1  -  " + std::to_string(total) + " CARDS", W / 2, 890, 44, W - 200, 0xB8C2D6, 2);
  // r212 (Lance: "i was starting to pick up on the order of the characters"): no card number on the back - a waiting
  // card gives nothing away; its number is on its front
  (void)c;
  P.glow(220, 1070, W - 220, 1250, kGold, 0.18f);
  P.text("CHASE THE 1/1", W / 2, 1160, 74, W - 220, kGold, 4);
  P.text("A STRIKERS SUPER LEAGUE COLLECTION", W / 2, 1380, 40, W - 160, 0x8A94AA, 2);
}

// r149: a TIZM BOOM's words by who it is (its world's theme: fire FWOOSH!, electric KRAKA-TOOM! ...), and a HOMETOWN's
// home (its suite / evolution room's picture, STRIKERS_HOME/suites/<name>_outside.jpg) and its place's name (below)
void boom_words(const std::string& key, std::string out[3]);
std::string home_backdrop(const Card& c, std::string* place);
void card_stats(Pc& P, const Card& c, float x0, float y0, float x1, float y1, float px);

// ============================================================================ r166: TIZM BOOM - "PRINTED PAGE"
// (Lance on r149/r150's BOOM: "the shapes are just too polygonal"; his pick of three mockups, logs\boom-redesign\A_*): a
// 1960s newsprint comic - cream paper grain, Ben-Day dot shading, hand-inked rounded outlines, smooth scalloped cloud
// balloons with drop shadows, curved speed lines, the character leaping up through a torn hole in the card's top with
// curling paper flaps; no pointed stars anywhere. A small pixel kit (straight-alpha float canvases, coverage masks, AA by
// distance) - the same shapes as tools/outside/boom_redesign165.py. Any thread.
constexpr uint32_t kPpInk = 0x16101C, kPpStock = 0xFAF3E1, kPpPaper = 0xF3E7C9, kPpRed = 0xD7262D, kPpYel = 0xFAD232;
constexpr uint32_t kPpSky = 0x78C8F0, kPpSkyDot = 0x2878C8, kPpSun = 0xFAD746, kPpHole = 0x302E4E;
uint32_t pp_hash(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}
float pp_h01(uint32_t x) { return static_cast<float>(pp_hash(x) & 0xFFFFFF) / 16777215.0f; }
float pp_noise(float x, float y, uint32_t seed) {  // (value noise, 0..1)
  const int ix = static_cast<int>(std::floor(x)), iy = static_cast<int>(std::floor(y));
  const float fx = x - ix, fy = y - iy, sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  auto at = [&](int a, int b) { return pp_h01(seed ^ (static_cast<uint32_t>(a) * 73856093u) ^ (static_cast<uint32_t>(b) * 19349663u)); };
  const float a = at(ix, iy), b = at(ix + 1, iy), c = at(ix, iy + 1), d = at(ix + 1, iy + 1);
  return (a + (b - a) * sx) + ((c + (d - c) * sx) - (a + (b - a) * sx)) * sy;
}
// a Ben-Day dot's coverage at (x, y): dots `sp` px apart on a grid turned `ang`, `size` 0..1 of a full dot
float pp_dot(float x, float y, float sp, float ang, float size) {
  if (size <= 0.0f) return 0.0f;
  const float c = std::cos(ang), s = std::sin(ang);
  const float u = (x * c + y * s) / sp, v = (-x * s + y * c) / sp;
  const float du = u - std::round(u), dv = v - std::round(v);
  const float d = std::sqrt(du * du + dv * dv) * sp, r = std::min(size, 1.25f) * sp * 0.59f;
  return clampf(r - d + 0.5f, 0.0f, 1.0f);
}
struct PpCanvas {  // straight RGBA, 0..1
  int w = 0, h = 0;
  std::vector<float> px;
  PpCanvas(int W, int H) : w(W), h(H), px(static_cast<size_t>(W) * H * 4, 0.0f) {}
  void over(int i, uint32_t rgb, float a) {
    if (a <= 0.0f) return;
    a = std::min(a, 1.0f);
    float* p = &px[static_cast<size_t>(i) * 4];
    const float oa = a + p[3] * (1.0f - a);
    const float k = p[3] * (1.0f - a);
    for (int ch = 0; ch < 3; ++ch) p[ch] = (((rgb >> (16 - 8 * ch)) & 255) / 255.0f * a + p[ch] * k) / oa;
    p[3] = oa;
  }
  void layer(const std::vector<float>& cov, uint32_t rgb, float a = 1.0f) {
    for (int i = 0; i < w * h; ++i) over(i, rgb, cov[static_cast<size_t>(i)] * a);
  }
  std::vector<uint8_t> bytes() const {
    std::vector<uint8_t> out(px.size());
    for (size_t i = 0; i < px.size(); ++i) out[i] = static_cast<uint8_t>(std::lround(clampf(px[i], 0.0f, 1.0f) * 255.0f));
    return out;
  }
};
// a comic cloud balloon's edge at angle `th`: round scallops (semicircle bumps, lobes of varied size, soft cusps); `boil`
// a hand-drawn line's wobble, different each of its frames
float pp_scallop(float th, int lobes, uint32_t seed, float bump, int boil) {
  const float u = th / 6.2831853f * lobes;
  const float ph = u - std::floor(u);
  const int idx = ((static_cast<int>(std::floor(u)) % lobes) + lobes) % lobes;
  const float amp = 1.0f + 0.3f * (pp_h01(seed * 31u + static_cast<uint32_t>(idx)) * 2.0f - 1.0f);
  const float prof = std::pow(std::sqrt(std::max(0.0f, 1.0f - (2.0f * ph - 1.0f) * (2.0f * ph - 1.0f))), 0.8f);
  const uint32_t bs = seed * 977u + static_cast<uint32_t>(boil) * 131u;
  const float lo = 0.014f * (std::sin(3.0f * th + 6.28f * pp_h01(bs)) + std::sin(5.0f * th + 6.28f * pp_h01(bs + 1)));
  return 1.0f + bump * prof * amp + lo;
}
// its coverage (w x h), grown by `grow` px (an ink line round it)
std::vector<float> pp_cloud(int w, int h, float cx, float cy, float rx, float ry, int lobes, uint32_t seed, int boil, float grow,
                            float bump = 0.22f) {
  std::vector<float> m(static_cast<size_t>(w) * h, 0.0f);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, d = std::sqrt(dx * dx + dy * dy);
      float th = std::atan2(dy, dx);
      if (th < 0.0f) th += 6.2831853f;
      const float R = pp_scallop(th, lobes, seed, bump, boil);
      const float s = d > 1e-4f ? std::sqrt(dx * rx * dx * rx + dy * ry * dy * ry) / d : 0.5f * (rx + ry);
      m[static_cast<size_t>(y) * w + x] = clampf((R - d) * s + grow + 0.5f, 0.0f, 1.0f);
    }
  return m;
}
// grown by r px (round: a chamfer distance to what's covered)
std::vector<float> pp_dilate(const std::vector<float>& cov, int w, int h, float r) {
  const int kInf = 1 << 28;
  std::vector<int> d(cov.size());
  for (size_t i = 0; i < cov.size(); ++i) d[i] = cov[i] >= 0.5f ? 0 : kInf;
  auto at = [&](int x, int y) { return x < 0 || y < 0 || x >= w || y >= h ? kInf : d[static_cast<size_t>(y) * w + x]; };
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      int& v = d[static_cast<size_t>(y) * w + x];
      v = std::min({v, at(x - 1, y) + 3, at(x, y - 1) + 3, at(x - 1, y - 1) + 4, at(x + 1, y - 1) + 4});
    }
  for (int y = h - 1; y >= 0; --y)
    for (int x = w - 1; x >= 0; --x) {
      int& v = d[static_cast<size_t>(y) * w + x];
      v = std::min({v, at(x + 1, y) + 3, at(x, y + 1) + 3, at(x + 1, y + 1) + 4, at(x - 1, y + 1) + 4});
    }
  std::vector<float> out(cov.size());
  for (size_t i = 0; i < cov.size(); ++i) out[i] = std::max(cov[i], clampf(r + 0.5f - d[i] / 3.0f, 0.0f, 1.0f));
  return out;
}
std::vector<float> pp_shift(const std::vector<float>& cov, int w, int h, int dx, int dy) {
  std::vector<float> out(cov.size(), 0.0f);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int sx = x - dx, sy = y - dy;
      if (sx >= 0 && sy >= 0 && sx < w && sy < h) out[static_cast<size_t>(y) * w + x] = cov[static_cast<size_t>(sy) * w + sx];
    }
  return out;
}
// a rounded box's coverage at (x, y)
float pp_rbox(float x, float y, float x0, float y0, float x1, float y1, float r) {
  const float qx = std::fabs(x - 0.5f * (x0 + x1)) - (0.5f * (x1 - x0) - r), qy = std::fabs(y - 0.5f * (y0 + y1)) - (0.5f * (y1 - y0) - r);
  const float d = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
  return clampf(0.5f - d, 0.0f, 1.0f);
}
// The face's printed page (1080 x 1512, the same for every TIZM BOOM; made once): cream card stock with its paper grain in
// an ink edge, the panel (the window's box) inked round, a sky in Ben-Day dots (bigger toward its foot), a sun of yellow
// dots behind the character, a city's rounded rooftops along its foot with lit window dots
ImgP pp_page() {
  static std::mutex m;
  static ImgP done;
  std::lock_guard<std::mutex> lock(m);
  if (done) return done;
  const int W = kFrontW, H = kFrontH;
  PpCanvas C(W, H);
  const float px0 = kHoleX0, py0 = kHoleY0, px1 = kHoleX1, py1 = kHoleY1;
  // the skyline (its buildings: x, width, height, colour)
  struct Bld {
    float x0, x1, top;
    uint32_t rgb;
  };
  std::vector<Bld> city;
  for (float bx = px0 - 10; bx < px1 - 30;) {
    const uint32_t k = static_cast<uint32_t>(city.size());
    const float bw = std::min(100.0f + 110.0f * pp_h01(k * 7 + 1), px1 - bx), bh = 160.0f + 230.0f * pp_h01(k * 7 + 2);
    city.push_back({bx, bx + bw, py1 - bh, pp_h01(k * 7 + 3) > 0.5f ? 0x2D4B96u : 0x463C8Cu});
    bx += bw + 9.0f;
  }
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      const int i = y * W + x;
      const float fx = x + 0.5f, fy = y + 0.5f;
      const float card = pp_rbox(fx, fy, 0, 0, W, H, 48);
      if (card <= 0.0f) continue;
      C.over(i, kPpInk, card);
      const float stock = pp_rbox(fx, fy, 16, 16, W - 16, H - 16, 36);
      const float grain = 0.93f + 0.09f * pp_noise(fx / 7.0f, fy / 7.0f, 11) + 0.03f * pp_noise(fx / 1.7f, fy / 1.7f, 12);
      const uint32_t sc = (std::min(255, static_cast<int>(250 * grain)) << 16) | (std::min(255, static_cast<int>(243 * grain)) << 8) |
                          std::min(255, static_cast<int>(225 * grain));
      C.over(i, sc, stock);
      C.over(i, kPpInk, pp_rbox(fx, fy, px0 - 12, py0 - 12, px1 + 12, py1 + 12, 34));
      const float panel = pp_rbox(fx, fy, px0, py0, px1, py1, 24);
      if (panel <= 0.0f) continue;
      C.over(i, kPpSky, panel);
      const float down = clampf((fy - py0) / (py1 - py0), 0.0f, 1.0f);
      C.over(i, kPpSkyDot, panel * pp_dot(fx, fy, 22.0f, 0.26f, 0.25f + 0.75f * down));
      const float sun = clampf(1.15f - std::hypot(fx - 540.0f, fy - (py0 + 400.0f)) / 430.0f, 0.0f, 1.0f);
      C.over(i, kPpSun, panel * pp_dot(fx, fy, 22.0f, 1.31f, sun));
      for (const Bld& b : city) {  // (inked rounded rooftops, window dots)
        const float ink = pp_rbox(fx, fy, b.x0 - 7, b.top - 7, b.x1 + 7, py1 + 40, 26);
        if (ink <= 0.0f) continue;
        C.over(i, kPpInk, ink * panel);
        const float body = pp_rbox(fx, fy, b.x0, b.top, b.x1, py1 + 40, 20) * panel;
        C.over(i, b.rgb, body);
        const float wx = std::fmod(fx - b.x0 - 24.0f + 400.0f, 38.0f) - 19.0f, wy = std::fmod(fy - b.top - 34.0f + 400.0f, 42.0f) - 21.0f;
        const uint32_t cell = static_cast<uint32_t>((fx - b.x0) / 38.0f) * 97u + static_cast<uint32_t>((fy - b.top) / 42.0f) * 13u +
                              static_cast<uint32_t>(b.x0);
        if (fx > b.x0 + 12 && fx < b.x1 - 12 && fy > b.top + 16 && pp_h01(cell) > 0.35f)
          C.over(i, 0xFADC78, body * clampf(8.0f - std::sqrt(wx * wx + wy * wy) + 0.5f, 0.0f, 1.0f));
      }
    }
  auto img = std::make_shared<Img>();
  img->rgba = C.bytes();
  img->w = W;
  img->h = H;
  done = img;
  return done;
}
// a cloud balloon (w x h; its ink line, a solid offset shadow, Ben-Day dots in it bigger toward its lower right), the
// `boil` of its line; bytes
std::vector<uint8_t> pp_balloon(int w, int h, float rx, float ry, int lobes, uint32_t seed, int boil, uint32_t bubble, uint32_t dots,
                                float dotSp, const std::vector<float>* letters = nullptr, const std::vector<float>* extrude = nullptr,
                                uint32_t fill = 0, int sh = 12) {
  const float cx = 0.5f * w - 0.4f * sh, cy = 0.5f * h - 0.4f * sh;
  const std::vector<float> body = pp_cloud(w, h, cx, cy, rx, ry, lobes, seed, boil, 0.0f);
  const std::vector<float> ink = pp_cloud(w, h, cx, cy, rx, ry, lobes, seed, boil, 7.0f);
  PpCanvas C(w, h);
  C.layer(pp_shift(ink, w, h, sh, sh + 2), kPpInk, 0.9f);
  C.layer(ink, kPpInk);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const int i = y * w + x;
      const float b = body[static_cast<size_t>(i)];
      if (b <= 0.0f) continue;
      C.over(i, bubble, b);
      const float g = clampf(((x - cx) / w + (y - cy) / h) * 1.2f + 0.35f, 0.0f, 1.0f);
      C.over(i, dots, b * pp_dot(x + 0.5f, y + 0.5f, dotSp, 0.26f, g));
    }
  if (letters != nullptr && extrude != nullptr) {
    std::vector<float> both(letters->size());
    for (size_t i = 0; i < both.size(); ++i) both[i] = std::max((*letters)[i], (*extrude)[i]);
    C.layer(pp_dilate(both, w, h, 5.0f), kPpInk);
    C.layer(*extrude, dark(fill, 0.55f));
    // the letters: their colour, a printed highlight band across their top third
    float top = static_cast<float>(h), bot = 0.0f;
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        if ((*letters)[static_cast<size_t>(y) * w + x] > 0.5f) top = std::min(top, static_cast<float>(y)), bot = std::max(bot, static_cast<float>(y));
    for (int y = 0; y < h; ++y) {
      const float hl = clampf(1.0f - (y - (top + 0.32f * (bot - top))) / (0.12f * (bot - top) + 1.0f), 0.0f, 1.0f) * 0.35f;
      const uint32_t c = mix_rgb(fill, 0xFFFFFF, hl);
      for (int x = 0; x < w; ++x) C.over(y * w + x, c, (*letters)[static_cast<size_t>(y) * w + x]);
    }
  }
  return C.bytes();
}
// the face's TIZM BOOM! badge: a small yellow cloud balloon, red dots (420 x 190; its words drawn over it: boom_front)
ImgP pp_badge() {
  static std::mutex m;
  static ImgP done;
  std::lock_guard<std::mutex> lock(m);
  if (done) return done;
  auto img = std::make_shared<Img>();
  img->rgba = pp_balloon(420, 190, 168, 62, 11, 5, 0, kPpYel, 0xEB462D, 11.0f, nullptr, nullptr, 0, 9);
  img->w = 420;
  img->h = 190;
  done = img;
  return done;
}

// r149: TIZM BOOM's front (Lance's case hit: "a comic-book explosion ... bold halftone burst panels ... jagged speech-bubble
// shapes, ink outlines, bright pop-art colours; the character's picture in the middle"): pop-art stock (yellow, a red
// burst, cyan halftone, a heavy ink outline), the character, TIZM BOOM! in a jagged bubble, its rating / series / finish /
// stars, a comic name bar, its stats. `flat` (no 3D of its own on show): its words as stickers on it too (its 3D pops
// them out of the card instead). Its foil: only its frame (a parallel's) - the comic is printed.
void boom_front(Pc& P, const Card& c, const Ref& r, const ImgP& art, const ImgP& badge, int total, bool mask, bool flat, int level) {
  const float W = kCW, H = kCH, bx = W - 132, by = 142;
  const Stock& st = kStock[r.parallel];
  if (mask) {
    P.box(0, 0, W, H, 48, gray(st.fFrame));
    P.box(22, 22, W - 22, H - 22, 40, gray(0));
    return;
  }
  const uint32_t yel = kPpYel, red = kPpRed, ink = kPpInk;
  (void)flat;
  // r150 (Lance: "make it so those animations are not on the card, yet pop out from the card and disappear after the
  // animation happens"): its face clean - its bursts and words are 3D only, popping out of it (special_moves)
  // r166 (Lance's pick, "PRINTED PAGE"): its face a printed 60s comic page (pp_page: cream stock and its grain, an inked
  // panel, a Ben-Day dot sky and sun, a city's rooftops), the character in the panel, TIZM BOOM! in a scalloped cloud
  // balloon (no points), a yellow inked rating, a red inked name plate. A parallel keeps its metal frame round the page.
  P.image(pp_page(), 0, 0, W, H, false);
  if (r.parallel != kBase) P.ring(0, 0, W, H, 48, 16, st.s1);  // (its metal round the page's edge, its panel where it always is)
  card_art_piece(P, art, false);
  // TIZM BOOM!, small, in its cloud balloon
  P.image(pp_badge(), 20, 24, 356, 176, false);
  P.style(0.1f, 0.92f, -4.0f, 0, ink, 0.0f);
  P.rim(ink);
  P.text("TIZM BOOM!", 184, 96, 44, 240, red, 5, 0xFFFFFF);
  P.plain();
  // its rating, series, finish, level
  P.box(bx - 92, by - 92, bx + 92, by + 92, 92, ink);
  P.box(bx - 80, by - 80, bx + 80, by + 80, 80, yel);
  P.dots(bx - 80, by - 80, bx + 80, by + 80, 14, 0.0f, 0.4f, 0xEB8C1E, 0.6f);
  P.text(std::to_string(c.rating), bx, by - 14, 88, 140, ink, 0);
  P.text("OVR", bx, by + 50, 40, 120, ink, 0);
  const float sw = std::min(208.0f, P.width(kSeriesName[c.series], 40) + 48);
  P.box(bx - sw / 2 - 5, by + 97, bx + sw / 2 + 5, by + 163, 33, ink);
  P.box(bx - sw / 2, by + 102, bx + sw / 2, by + 158, 28, kSeriesRgb[c.series]);
  P.text(kSeriesName[c.series], bx, by + 130, 40, sw - 24, kSeriesInk[c.series], 2);
  float pillY = 270;
  if (r.parallel != kBase) {
    const float pw = P.width(kParallelName[r.parallel], 40) + 56;
    P.box(48, pillY, 52 + pw + 4, pillY + 62, 31, ink);
    P.box(52, pillY + 4, 52 + pw, pillY + 58, 27, kFinishPill[r.parallel]);
    P.text(kParallelName[r.parallel], 52 + pw / 2, pillY + 31, 40, pw - 24, kFinishInk[r.parallel], 2);
    pillY += 74;
  }
  if (r.shiny) {
    P.box(48, pillY, 236, pillY + 62, 31, ink);
    P.box(52, pillY + 4, 232, pillY + 58, 27, 0xFFFFFF);
    P.text("SHINY", 142, pillY + 31, 40, 160, 0x2A0A5A, 2);
  }
  if (level > 1) {
    const float lw = level * 46.0f + 26.0f, lx = std::min(bx, W - 40 - lw / 2);
    P.box(lx - lw / 2, by + 170, lx + lw / 2, by + 228, 29, ink);
    for (int s = 0; s < level; ++s) star_shape(P, lx - (level - 1) * 23.0f + s * 46.0f, by + 199, 19, yel);
  }
  // its name on a red inked plate (r166: rounded, a printed highlight); its emblem; its stats; the set
  P.box(26, 1124, W - 26, 1306, 30, ink);
  P.box(40, 1138, W - 40, 1292, 20, red);
  P.box(52, 1146, W - 52, 1176, 14, 0xFFFFFF, 0.18f);
  P.style(0.1f, 0.92f, 0.0f, 6, ink, 0.9f);
  P.rim(ink);
  if (c.sub.empty()) {
    P.text(upper(c.name), 455, 1218, 92, 760, 0xFFFFFF, 7);
  } else {  // r152: a costume's own name, its hero under it
    P.text(upper(c.name), 455, 1192, 80, 760, 0xFFFFFF, 7);
    P.text(upper(c.sub), 455, 1258, 40, 600, yel, 5);
  }
  P.plain();
  P.image(badge, W - 214, 1116, W - 44, 1286, false);
  P.box(26, 1316, W - 26, H - 26, 36, ink);
  card_stats(P, c, 26, 1316, W - 26, H - 26, 40);
  if (r.parallel == kGoldTizm || r.parallel == kBlack) {
    const std::string stamp = r.parallel == kBlack ? "1/1" : (r.serial < 10 ? "0" : "") + std::to_string(r.serial) + "/10";
    P.box(W - 254, 1036, W - 54, 1114, 14, ink);
    P.box(W - 248, 1042, W - 60, 1108, 10, r.parallel == kBlack ? 0x16181E : 0xFFE27A);
    P.text(stamp, W - 154, 1075, 58, 180, r.parallel == kBlack ? 0xFFFFFF : 0x3A2600, 2);
  }
  (void)total;
}

// r149: HOMETOWN's front, drawn LANDSCAPE (Lance: "make sure the hometown ones are designed landscaped, so when we pull
// them, we have to physically hold it that way, as to see the full art of the card!"): kHomeW x kHomeH, its content's up
// the card's left edge (draw_front turns it: hold the card turned a quarter clockwise to read it) - HOMETOWN on a ribbon and
// its place's name, its window (a diorama of its home behind it on show - kHomeWin*; flat: its home's picture and the
// character in it), its name, rating, series, finish, level and stats along the bottom
constexpr int kHomeW = 1512, kHomeH = 1080;
constexpr float kHomeWinX0 = 70, kHomeWinX1 = 1442, kHomeWinY0 = 170, kHomeWinY1 = 910;
void home_front(Pc& P, const Card& c, const Ref& r, const ImgP& art, const ImgP& badge, const ImgP& home, const std::string& place,
                int total, bool mask, bool window, int level) {
  const float W = kHomeW, H = kHomeH;
  const Stock& st = kStock[r.parallel];
  if (mask) {
    P.box(0, 0, W, H, 48, gray(st.fFrame));
    P.box(22, 22, W - 22, H - 22, 40, gray(0));
    if (window) P.box(kHomeWinX0, kHomeWinY0, kHomeWinX1, kHomeWinY1, 24, 0x00FF00);
    return;
  }
  P.box(0, 0, W, H, 48, st.s1, 1.0f, st.s2);
  P.box(22, 22, W - 22, H - 22, 40, dark(c.c1, 0.55f), 1.0f, dark(c.c1, 0.8f));
  if (!window) {  // flat: its home's picture in the window, the character standing in it
    if (home && !home->empty()) P.image(home, kHomeWinX0, kHomeWinY0, kHomeWinX1, kHomeWinY1, false);
    else P.box(kHomeWinX0, kHomeWinY0, kHomeWinX1, kHomeWinY1, 24, 0x6AB0E8, 1.0f, 0x2A6A3A);
    P.glow(kHomeWinX0 + 300, kHomeWinY0 + 140, kHomeWinX1 - 300, kHomeWinY1, 0xFFFFFF, 0.25f);
    if (!opaque_art(art)) P.centred(art, 0.5f * (kHomeWinX0 + kHomeWinX1) - 330, kHomeWinY0 + 40, 0.5f * (kHomeWinX0 + kHomeWinX1) + 330,
                                    kHomeWinY1 - 10, 0.96f);
  }
  P.ring(kHomeWinX0 - 6, kHomeWinY0 - 6, kHomeWinX1 + 6, kHomeWinY1 + 6, 28, 8, kGold);
  // HOMETOWN on its ribbon; its place
  P.poly({40, 34, 760, 34, 730, 92, 760, 150, 40, 150, 70, 92}, 0x7A1414);
  P.poly({52, 44, 744, 44, 716, 92, 744, 140, 52, 140, 80, 92}, 0xD22A2A);
  P.style(0.12f, 0.95f, 0.0f, 4, 0x000000, 0.5f);
  P.text("HOMETOWN", 398, 94, 76, 600, 0xFFFFFF, 4);
  P.plain();
  const float pw = std::min(560.0f, P.width(place, 44) + 60);
  P.box(790, 52, 790 + pw, 136, 42, kNavy, 0.94f);
  P.ring(790, 52, 790 + pw, 136, 42, 4, kGold);
  P.text(place, 790 + pw / 2, 94, 44, pw - 30, 0xFFFFFF, 2);
  // its rating; its series
  P.box(W - 190, 26, W - 40, 176, 75, kNavy, 0.94f);
  P.ring(W - 190, 26, W - 40, 176, 75, 7, r.parallel == kGoldTizm ? kGold : st.ring);
  P.text(std::to_string(c.rating), W - 115, 88, 74, 120, kGold, 4);
  P.text("OVR", W - 115, 142, 32, 100, 0xE8ECF5, 2);
  // along the bottom: its name, its emblem, its finish / shiny / level, its stats, the set
  P.style(0.16f, 0.92f, -1.0f, 5, 0x000000, 0.5f);
  P.left(upper(c.name), 60, 968, 78, 700, 0xFFFFFF, 5);
  P.plain();
  if (!c.sub.empty()) P.left(upper(c.sub), 64, 930, 26, 400, kGold, 3);  // r152: a costume's hero, over its name
  P.image(badge, 1300, 916, 1450, 1066, false);
  float tx = 780;
  if (r.parallel != kBase) {
    const float w = P.width(kParallelName[r.parallel], 34) + 44;
    P.box(tx, 944, tx + w, 994, 25, kFinishPill[r.parallel]);
    P.text(kParallelName[r.parallel], tx + w / 2, 969, 34, w - 20, kFinishInk[r.parallel], 2);
    tx += w + 14;
  }
  if (r.shiny) {
    P.box(tx, 944, tx + 150, 994, 25, 0xFFFFFF);
    P.text("SHINY", tx + 75, 969, 34, 130, 0x2A0A5A, 2);
    tx += 164;
  }
  for (int s = 0; s < level && level > 1; ++s) star_shape(P, tx + 20 + s * 40.0f, 969, 16, kGold);
  card_stats(P, c, 40, 1004, 1150, 1062, 26);  // (clear of its number)
  P.right(num3(c.num) + "/" + std::to_string(total), 1280, 1040, 30, 200, kGold, 2);
}
// a landscape picture (w x h) turned a quarter counter-clockwise into a portrait one (h x w): its top along the card's left edge
std::vector<uint8_t> turn_ccw(const std::vector<uint8_t>& src, int w, int h) {
  std::vector<uint8_t> out(src.size());
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) std::memcpy(&out[(static_cast<size_t>(w - 1 - x) * h + y) * 4], &src[(static_cast<size_t>(y) * w + x) * 4], 4);
  return out;
}

// r147: a card's level moment as it turns over (Lance: a level-up "still feels good"), 640 x 200: a sticker that pops up in
// front of its face for a second and a half (frame(): level_tag) - LEVEL UP! with a green arrow (this copy took it up a
// level), else DUPLICATE with its copies; its stars and LV n either way
constexpr int kTagW = 640, kTagH = 200;
void level_tag(Pc& P, int level, int copies, bool up) {
  P.box(6, 6, kTagW - 6, kTagH - 6, 94, kNavy, 0.96f);
  P.ring(6, 6, kTagW - 6, kTagH - 6, 94, 6, kGold);
  if (up) {
    P.box(30, 30, 170, 170, 70, 0x2EA043);
    P.poly({100, 46, 146, 102, 54, 102}, 0xFFFFFF);
    P.box(86, 98, 114, 152, 6, 0xFFFFFF);
  } else {
    P.box(30, 30, 170, 170, 70, 0xD3202A);
    P.text("X" + std::to_string(copies), 100, 100, 62, 120, 0xFFFFFF, 3);
  }
  P.left(up ? "LEVEL UP!" : "DUPLICATE", 196, 70, 60, kTagW - 230, up ? kGold : 0xFFFFFF, 4);
  for (int s = 0; s < level; ++s) star_shape(P, 218 + s * 44.0f, 146, 18, kGold);
  P.left("LV " + std::to_string(level), 206 + level * 44.0f, 146, 46, kTagW - 230 - level * 44.0f, up ? 0xFFFFFF : kGold, 3);
}

// The pack, 900 x 1380: a chrome foil pouch (crimped top and bottom, a tear line under the top crimp) that catches the
// light as it turns. Front: the mark big, 2026-27 STRIKERS on a black band, three stars in chrome, "5 CARDS", the chase.
// Back: what's in it (the odds, as packs print them), how you earn them.
constexpr float kPW = 900.0f, kPH = 1380.0f;
constexpr float kTearV = 145.0f / 1380.0f;  // the tear line, down the pack (its strip above flies off)
void pack_crimps(Pc& P, uint32_t c1, uint32_t c2) {
  const float W = kPW, H = kPH;
  P.box(0, 14, W, 96, 6, c1, 1.0f, c2);
  P.box(0, H - 96, W, H - 14, 6, c2, 1.0f, c1);
  for (float x = 0; x < W - 1; x += 30) {
    P.poly({x, 15, x + 15, 1, x + 30, 15}, c1);
    P.poly({x, H - 15, x + 30, H - 15, x + 15, H - 1}, c1);
  }
  for (float x = 14; x < W; x += 22) {
    P.box(x, 24, x + 6, 86, 3, 0xFFFFFF, 0.22f);
    P.box(x, H - 86, x + 6, H - 24, 3, 0xFFFFFF, 0.22f);
  }
}
void hexagon(Pc& P, float cx, float cy, float r, uint32_t rgb, float a = 1.0f) {
  float p[12];
  for (int i = 0; i < 6; ++i) {
    const float ang = -1.5707963f + 1.0471976f * i;
    p[2 * i] = cx + r * std::cos(ang);
    p[2 * i + 1] = cy + r * std::sin(ang);
  }
  P.poly({p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11]}, rgb, a);
}
void pack_front(Pc& P, const ImgP stars[3], bool mask) {
  const float W = kPW, H = kPH;
  static const float kSx0[3] = {30, 210, 500}, kSx1[3] = {400, 690, 870};
  if (mask) {
    pack_crimps(P, gray(0.95f), gray(0.95f));
    P.box(0, 80, W, H - 80, 10, gray(0.9f));
    logo(P, 72, 168, 2.05f, true);
    P.poly({0, 600, W, 540, W, 650, 0, 710}, gray(0.35f));
    for (int i = 0; i < 3; ++i) P.figure(stars[i], kSx0[i], i == 1 ? 700 : 760, kSx1[i], 1180, false, 0.5f, 1.0f, i == 2, 0, 0, 1.0f, 0x010101);
    hexagon(P, W - 150, 1060, 112, gray(0.0f));
    P.box(50, 1190, W - 50, 1270, 24, gray(0));
    return;
  }
  pack_crimps(P, 0xEEF1F6, 0x8E97A8);
  P.box(0, 80, W, H - 80, 10, 0xDCE1EA, 1.0f, 0x7E889A);
  prism(P, 0, 80, W, H - 80, W / 2, 620, 34, 0.07f, 0xFFFFFF, 0xBFC7D6, 0x7E889C, 0.6f);
  P.glow(80, 140, W - 80, 560, 0x7FE8FF, 0.25f);
  for (float x = 40; x < W - 60; x += 36) P.box(x, 129, x + 20, 135, 3, 0x1A2030, 0.85f);
  P.poly({W - 46, 120, W - 26, 132, W - 46, 144}, 0x1A2030);
  for (int i = 0; i < 3; ++i)
    P.figure(stars[i], kSx0[i], i == 1 ? 700 : 760, kSx1[i], 1180, false, 0.5f, 1.0f, i == 2, 0x141A2A, 0xF4F7FB);
  logo(P, 72, 168, 2.05f, false);
  P.poly({0, 600, W, 540, W, 650, 0, 710}, 0x0E1220);
  P.style(0.2f, 0.94f, -3.8f);
  P.text("2026-27 STRIKERS", W / 2, 625, 66, W - 220, 0xFFFFFF, 3);
  P.plain();
  hexagon(P, W - 150, 1060, 112, 0x0E1220);
  hexagon(P, W - 150, 1060, 98, 0x2A3354);
  P.text("5", W - 150, 1040, 96, 140, 0xFFFFFF, 4);
  P.text("CARDS", W - 150, 1110, 40, 160, 0xFFFFFF, 2);
  P.box(50, 1190, W - 50, 1270, 24, 0x0E1220, 0.92f);
  P.text("CHASE: SILVER  -  GOLD /10  -  BLACK 1/1", W / 2, 1230, 40, W - 140, 0xFFFFFF, 2);
}
void pack_back(Pc& P, int total, bool mask) {
  const float W = kPW, H = kPH;
  if (mask) {
    pack_crimps(P, gray(0.95f), gray(0.95f));
    P.box(0, 80, W, H - 80, 10, gray(0.6f));
    P.box(50, 140, W - 50, 360, 30, gray(0));
    P.box(60, 380, W - 60, 1060, 30, gray(0));
    P.box(50, 1090, W - 50, 1280, 30, gray(0));
    return;
  }
  pack_crimps(P, 0xEEF1F6, 0x8E97A8);
  P.box(0, 80, W, H - 80, 10, 0x2A3040, 1.0f, 0x0A0C12);
  prism(P, 0, 80, W, H - 80, W / 2, H / 2, 30, 0.0f, 0x3A4258, 0x1C2132, 0x0A0C12, 0.7f);
  logo(P, 220, 150, 1.25f, false);
  P.text(std::string(kSetName) + "  -  " + std::to_string(total) + " CARDS", W / 2, 330, 40, W - 120, 0xFFFFFF, 2);
  P.box(60, 380, W - 60, 1060, 30, kNavy, 0.9f);
  P.text("IN EVERY PACK", W / 2, 426, 50, W - 200, kGold, 3);
  const double srPerPack = (kPackCards - 1) * kSlotSR + kLastSR;
  // r211 (Lance's new pull rates): + the case hits and the GOD PACK, plain "1 IN N CARDS / PACKS"; eleven rows in r210's panel
  auto in = [](int n, const char* what) { return "1 IN " + xrcoin::text(n) + " " + what; };
  constexpr int kRows = 11;
  const std::string rows[kRows][2] = {{"CARDS", "5"},
                                      {"RARE OR BETTER", "EVERY PACK"},
                                      {"SUPER RARE", "ABOUT 1 IN " + std::to_string(static_cast<int>(std::lround(1.0 / srPerPack))) + " PACKS"},
                                      {"SILVER TIZM", in(kSilverOdds, "CARDS")},
                                      {"GOLD TIZM /10", in(kGoldOdds, "CARDS")},
                                      {"BLACK TIZM 1/1", in(kBlackOdds / kPackCards, "PACKS")},
                                      {"TIZM BOOM", in(kHitIn[kHitBoom], "CARDS")},
                                      {"HOMETOWN", in(kHitIn[kHitHome], "CARDS")},
                                      {"SIGNED", in(kHitIn[kHitSigned], "CARDS")},
                                      {"SHINY POKEMON", in(kShinyOdds, "POKEMON")},
                                      {"GOD PACK", in(kGodOdds, "PACKS")}};
  for (int i = 0; i < kRows; ++i) {
    const float y = 492 + i * 52.0f;
    P.left(rows[i][0], 100, y, 32, 380, 0xE8ECF5, 2);
    P.right(rows[i][1], W - 100, y, 32, 400, i >= 3 ? 0xFFF2A0 : 0xFFFFFF, 2);
    if (i < kRows - 1) P.box(100, y + 24, W - 100, y + 27, 2, 0x2A3354);
  }
  P.text("EARN THEM IN FRANCHISE MATCHES", W / 2, 1130, 46, W - 140, kGold, 3);
  P.text("1 A MATCH  -  2 FOR A WIN", W / 2, 1192, 42, W - 160, 0xFFFFFF, 2);
  P.text("+1 A PLAYOFF SERIES WON  -  +1 A TROPHY", W / 2, 1250, 40, W - 140, 0xC8D8FF, 2);
}

// Drawn: the board, then its foil (the mask board) into the alpha (1 - alpha x 2 = foil: xr_fx_model.frag's card mode);
// straight colours where it's solid.
std::vector<uint8_t> render(int w, int h, const std::vector<xrtext::BoardPiece>& col, const std::vector<xrtext::BoardPiece>* mask) {
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  xrtext::board(px.data(), w, h, col);
  std::vector<uint8_t> m;
  if (mask != nullptr && !mask->empty()) {
    m.resize(px.size());
    xrtext::board(m.data(), w, h, *mask);
  }
  for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
    uint8_t* p = &px[i * 4];
    const int a = p[3];
    if (!m.empty() && m[i * 4 + 1] > m[i * 4] + 100) {  // r138: a special card's window, see-through
      p[0] = p[1] = p[2] = p[3] = 0;
      continue;
    }
    if (a == 0) continue;
    if (a < 255) {  // its edge: straight colour, as it is
      for (int k = 0; k < 3; ++k) p[k] = static_cast<uint8_t>(std::min(255, p[k] * 255 / a));
      continue;
    }
    if (!m.empty()) {
      const int foil = m[i * 4];  // (grey, premultiplied: its coverage times its strength)
      p[3] = static_cast<uint8_t>(255 - (foil * 127 + 127) / 255);
    }
  }
  return px;
}

// r149: the four stats, five pips each - two by two on a card's foot (the front's own layout), in a row on a landscape one
void card_stats(Pc& P, const Card& c, float x0, float y0, float x1, float y1, float px) {
  static const char* const kStat[4] = {"SPEED", "SHOOTING", "PASSING", "SLIDE TACKLE"};
  const bool row = (x1 - x0) > 10.0f * (y1 - y0);  // (a landscape foot 1240 x 58 a row; a card's foot 1028 x 170 two by two)
  for (int i = 0; i < 4; ++i) {
    float lx, pipX, y;
    if (row) {
      lx = x0 + 20 + i * (x1 - x0 - 40) / 4.0f;
      pipX = lx + P.width(kStat[i], px) + 12;
      y = 0.5f * (y0 + y1);
    } else {
      const float cw = 0.5f * (x1 - x0);
      lx = x0 + 38 + (i % 2) * cw;
      pipX = x0 + (i % 2 == 0 ? 314 : 820) * (x1 - x0) / 1028.0f;
      y = y0 + 36 + (i / 2) * 58.0f;
    }
    P.left(kStat[i], lx, y, px, row ? 260 : 270, 0xC8D2E6, 2);
    const int lit = std::max(1, std::min(5, static_cast<int>(std::lround(c.stats[i] * 5.0f))));
    const float pw = row ? 0.55f * px : 26, pg = row ? 0.75f * px : 34;
    for (int s = 0; s < 5; ++s) P.box(pipX + s * pg, y - 0.35f * px, pipX + s * pg + pw, y + 0.35f * px, 0.18f * px, s < lit ? kGold : 0x2A3354);
  }
}

ImgP home_picture(const std::string& name);  // (below)
#if __has_include("xr_sign.h")
void bake_signature(std::vector<uint8_t>& px, int w, int h, const Card& c, float foot = 0.0f);  // (below)
#endif

// r217: the two sets' looks (A PRIZM CHROME, B TIZM JAM '96) - their pixel kit and their pictures
#include "xr_cards_look.inc"
#include "xr_cards_look_ab.inc"
#include "xr_cards_look_exq.inc"  // r230: LANCINI EXQUISITE (NOIR + THE VAULT)

// r230: an EXQUISITE EMERALD / SAPPHIRE's front normals' length (xr_fx_model.frag kJewelCode: its jewel chrome refractor + sparkle), 0 any other
constexpr float kJewelCode = 7.0f;
float jewel_code(const Ref& r) {
  if (r.card < 0 || r.card >= static_cast<int>(g_set.size()) || g_set[static_cast<size_t>(r.card)].set != 2) return 0.0f;
  const int t = xrexq::tier_of(r.serial);
  return t == xrexq::kEmerald ? kJewelCode : (t == xrexq::kSapphire ? kJewelCode + 1.0f : 0.0f);
}
// r220: a JAM '96 card's die (lk::jam_shape: its rarest feature; 0 the crest) and how far it moves the name / sticker up (its foot);
// an A card: -1 / 0
int jam_shape_of(const Card& c, const Ref& r) { return c.set == 1 ? lk::jam_shape(r) : -1; }
float jam_foot(const Ref& r) {
  const int s = lk::jam_shape(r);
  return s != 0 ? lk::jam_layout(s).foot : 0.0f;
}

// r226: `artUrgent` / *bigArt as card_art's (*bigArt true too for a front with no picture of the character: nothing to wait for)
std::vector<uint8_t> draw_front(const Card& c, const Ref& r, int total, float waitS, int special = kSpNone, int level = 0,
                                bool artUrgent = true, bool* bigArt = nullptr) {
  if (c.set == 2) {  // r230: LANCINI EXQUISITE - the NOIR front (lk::xq), every tier; always flat (its autograph / patch printed on it)
    lk::xq::XIn xin;
    xin.art = card_art(c, false, waitS, artUrgent, bigArt);
    xin.no = c.num;
    xin.total = g_exqN;
    xin.rookie = exq_rookie(c);
    exq_club(c, &xin.club1, &xin.club2);
    (void)total, (void)special, (void)level;
    return lk::xq::x_front(c, r, xin);
  }
  // r147 (Lance, his GOLD Scyther: "they need a static image of the character on the card AS WELL AS the 3D body on top of the
  // card"): a GOLD's pop-out front keeps its picture too (its figure still stands on its top edge)
  const bool flat = special == kSpNone;
  // (r150: a TIZM BOOM's character printed on its clean face on show too - only its bursts and words pop out)
  // r217 (Lance 11-11: A for the main set, B the new JAM '96 pack): every front in its set's look (lk::a_front / b_front, the
  // mockups' pixel kit; a HOMETOWN landscape, turned onto the card) - the same finishes, specials' windows, case hits, stars
  lk::FrontIn in;
  const bool wantArt = flat || special == kSpPop || special == kSpSign || special == kSpBoom;
  if (bigArt != nullptr) *bigArt = true;
  in.art = wantArt ? card_art(c, r.shiny, waitS, artUrgent, bigArt) : ImgP();
  in.total = total;
  in.special = special;
  in.level = level;
  std::vector<uint8_t> px;
  if (r.hit == kHitHome) {  // r149: HOMETOWN - drawn landscape, turned onto the card
    const std::string file = home_backdrop(c, &in.place);
    if (flat) in.home = home_picture(file);
    return turn_ccw(lk::home_front_lk(c, r, in, !flat), kHomeW, kHomeH);
  }
  if (r.hit == kHitBoom) in.boomPage = pp_page(), in.boomBadge = pp_badge();  // r149 / r166: TIZM BOOM's printed page
  px = c.set == 1 ? lk::b_front(c, r, in) : lk::a_front(c, r, in);
#if __has_include("xr_sign.h")
  // r149: a SIGNED card / every BLACK 1/1, flat: its autograph on it (on show, its own writes itself on: special_up)
  // (r220: a Jam die's sticker sits up its foot)
  if (flat && (r.hit == kHitSigned || r.parallel == kBlack)) bake_signature(px, kFrontW, kFrontH, c, c.set == 1 ? jam_foot(r) : 0.0f);
#endif
  return px;
}
// r217: a set's back (every card of a set the same: drawn once a set, kept - 3.3 MB each)
// r220: `shape` a JAM '96 card's own die (lk::jam_shape: its back once it's turned over / out of the binder / in a slab; 0 the crest a
// face-down card shows) - the last few drawn kept (3.3 MB each, at most kBackKeep)
std::vector<uint8_t> draw_back(const Card& c, int total, int shape = 0) {  // (r147: the plain back, always)
  (void)total;
  static std::mutex m;
  static std::vector<uint8_t> done[3];  // (r230: + LANCINI EXQUISITE's)
  static std::vector<std::pair<int, std::vector<uint8_t>>> cut;  // (newest last)
  constexpr size_t kBackKeep = 3;
  const int set = c.set == 1 ? 1 : (c.set == 2 ? 2 : 0);
  if (set == 1 && shape > 0) {
    {
      std::lock_guard<std::mutex> lock(m);
      for (size_t i = 0; i < cut.size(); ++i)
        if (cut[i].first == shape) {
          auto e = std::move(cut[i]);
          cut.erase(cut.begin() + static_cast<long>(i));
          cut.push_back(std::move(e));
          return cut.back().second;
        }
    }
    std::vector<uint8_t> px = lk::shrink(lk::b_back_cv(shape), kBackW, kBackH, true);
    std::lock_guard<std::mutex> lock(m);
    cut.push_back({shape, px});
    while (cut.size() > kBackKeep) cut.erase(cut.begin());
    return px;
  }
  std::lock_guard<std::mutex> lock(m);
  if (done[set].empty()) done[set] = lk::shrink(set == 2 ? lk::xq::x_back_cv() : (set == 1 ? lk::b_back_cv() : lk::a_back_cv()), kBackW, kBackH, true);
  return done[set];
}
std::vector<uint8_t> draw_tag(int level, int copies, bool up) {  // r147
  Pc P;
  level_tag(P, level, copies, up);
  return render(kTagW, kTagH, P.v, nullptr);
}
// r217: `set` 0 the LANCINI TIZM wrapper (mockup A), 1 the JAM '96 one (mockup B); their stars placed so no face is covered
// (lk::trio_layout - logged)
std::vector<uint8_t> draw_pack(bool back, int total, int set = 0) {
  if (set == 2) return lk::bytes(back ? lk::xq::x_pack_back_cv() : lk::xq::x_pack_cv(), true);  // r230: LANCINI EXQUISITE
  if (back) return lk::bytes(lk::pack_back_cv(set, total), true);
  ImgP mario = portrait("strikers_mario"), chari = portrait("pk_charizard"), spidey = portrait("mua_spiderman");
  if (chari->empty()) chari = portrait("strikers_peach");
  if (spidey->empty()) spidey = portrait("strikers_wario");
  lk::Star st[3];
  lk::pack_stars(mario, chari, spidey, st);
  std::vector<uint8_t> px = lk::bytes(set == 1 ? lk::b_pack_cv(st) : lk::a_pack_cv(st), true);
  CLOG("pack wrapper %s: stars placed, faces covered at most %.1f%%", set == 1 ? "JAM '96" : "LANCINI TIZM", 100.0f * lk::g_trioWorst[set]);
  return px;
}

// ------------------------------------------------------------------------------------- the binder (the hub)
// The hub's middle, 2160 x 1300 (y 190 .. 1190): an open binder (two pages of 3 x 3 pockets) at the left, its pages
// turned with LT / RT; your set progress, your packs and the card under the cursor at the right.
// r157 round 2: in the hub's page area right of its rail, at full size (round 1 drew r156's layout x0.79): smaller
// pockets (172 x 240), the binder 470 .. 1726, the column at its right for the one under the ring and your packs
// r160 round 2 (the BINDER page framed in its skin, wide enough to read): pockets 160 x 223 (r157: 172 x 240), a 46 px
// cover round the pages, a 60 px spine between them; the pages' area (the other CARDS pages) stays 470 .. 1726 from y 222
// r163 (Lance: the cards looked "like big ass cards" in r162): back to r159's 172 x 240 pockets - the skin's frame kept,
// a little narrower (28 px round the pages, a 56 px spine)
constexpr float kPocketW = 172, kPocketH = 240, kPocketGap = 12, kPageMargin = 16;
constexpr float kAreaX0 = 470, kAreaX1 = 1726, kAreaY0 = 222, kFrameM = 28, kFrameV = 28, kSpineW = 56;
constexpr float kBinderY = kAreaY0 + kFrameV, kLeftPageX = kAreaX0 + kFrameM;
constexpr float kPageW = 2 * kPageMargin + 3 * kPocketW + 2 * kPocketGap;  // 548 (r157: 592)
constexpr float kPageH = 2 * kPageMargin + 3 * kPocketH + 2 * kPocketGap;  // 737 (r157: 796)
constexpr float kRightPageX = kLeftPageX + kPageW + kSpineW;
constexpr int kPerSpread = 18;
void pocket_rect(int slot, float* x0, float* y0, float* x1, float* y1) {
  const int page = slot / 9, s = slot % 9;
  *x0 = (page == 0 ? kLeftPageX : kRightPageX) + kPageMargin + (s % 3) * (kPocketW + kPocketGap);
  *y0 = kBinderY + kPageMargin + (s / 3) * (kPocketH + kPocketGap);
  *x1 = *x0 + kPocketW;
  *y1 = *y0 + kPocketH;
}

// The binder's state (game thread writes, the drawing thread reads; under g_mutex)
int g_spread = 0, g_cursor = 0;
std::atomic<int> g_sceneMode{0};  // 0 the binder, 1 a pack open, 2 a card out (what the hub's page shows)

// The cards the binder shows: on this headset or owned, by number. (g_mutex held)
std::vector<int> binder_list() {
  std::vector<int> out;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].here || g_owned[i].total() > 0) out.push_back(static_cast<int>(i));
  return out;
}

// r149: a colour round the rainbow (0..1 round it, 6 stops, smooth between)
uint32_t rainbow_at(float t) {
  static const uint32_t kRb[6] = {0xFF4A4A, 0xFFA03A, 0xFFE24A, 0x4AD86A, 0x4AA8FF, 0xB06AFF};
  const float x = (t - std::floor(t)) * 6.0f;
  const int i = static_cast<int>(x) % 6;
  return mix_rgb(kRb[i], kRb[(i + 1) % 6], x - std::floor(x));
}
// r149: a sparkle's glint (a four-pointed star of light)
void glint(Pc& P, float cx, float cy, float r, uint32_t rgb, float a) {
  P.glow(cx - 0.7f * r, cy - 0.7f * r, cx + 0.7f * r, cy + 0.7f * r, rgb, 0.6f * a);
  P.poly({cx, cy - r, cx + 0.14f * r, cy, cx, cy + r, cx - 0.14f * r, cy}, rgb, a);
  P.poly({cx - r, cy, cx, cy - 0.14f * r, cx + r, cy, cx, cy + 0.14f * r}, rgb, a);
}
// r149: holo bands across a box (diagonal, round the rainbow, `a` strong)
void holo_bands(Pc& P, float x0, float y0, float x1, float y1, float band, float a, float phase) {
  const float h = y1 - y0;
  for (int k = -static_cast<int>(h / band) - 2; k * band < (x1 - x0) + band; ++k) {
    const float sx = x0 + k * band;
    const std::vector<float> c = clip_rect({sx, y0, sx + band, y0, sx + band + h * 0.8f, y1, sx + h * 0.8f, y1}, x0, y0, x1, y1);
    if (c.size() >= 6) polyv(P, c, rainbow_at(phase + k / 6.0f), a);
  }
}

// r149: a picture turned a quarter round to the left (a HOMETOWN's pocket: its landscape card sideways), kept by `key`
ImgP img_turned(const std::string& key, const ImgP& g) {
  if (!g || g->empty()) return ImgP();
  static std::mutex m;
  static std::unordered_map<std::string, ImgP> cache;
  std::lock_guard<std::mutex> lock(m);
  auto it = cache.find(key);
  if (it != cache.end() && it->second) return it->second;
  auto img = std::make_shared<Img>();
  img->w = g->h;
  img->h = g->w;
  img->rgba = turn_ccw(g->rgba, g->w, g->h);
  cache[key] = img;
  return img;
}
// its home's picture, turned (read once a home)
ImgP home_pocket_pic(const Card& c) {
  std::string place;
  const std::string name = home_backdrop(c, &place);
  static std::mutex m;
  static std::unordered_map<std::string, ImgP> cache;
  {
    std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
  }
  ImgP p = img_turned("home:" + name, home_picture(name));
  std::lock_guard<std::mutex> lock(m);
  cache[name] = p;
  return p;
}
#if __has_include("xr_sign.h")
// r149: its autograph whole, for its pocket (2 : 1, kept)
ImgP sign_pic(const Card& c) {
  static std::mutex m;
  static std::unordered_map<std::string, ImgP> cache;
  std::lock_guard<std::mutex> lock(m);
  auto it = cache.find(c.key);
  if (it != cache.end()) return it->second;
  const xrsign::Signature& s = xrsign::get(c.key, c.signAs);
  auto img = std::make_shared<Img>();
  img->w = 400;
  img->h = 200;
  std::vector<uint8_t> ink(static_cast<size_t>(img->w) * img->h * 4, 0);
  xrsign::draw(s, s.duration + 1.0f, ink.data(), img->w, img->h, img->w * 4);
  // (a pocket's small: its ink a pixel bolder, a dark edge round it so it reads on any picture)
  img->rgba.assign(ink.size(), 0);
  for (int y = 0; y < img->h; ++y)
    for (int x = 0; x < img->w; ++x) {
      int a1 = 0, a2 = 0;
      size_t at = 0;
      for (int dy = -3; dy <= 3; ++dy)
        for (int dx = -3; dx <= 3; ++dx) {
          const int xx = x + dx, yy = y + dy;
          if (xx < 0 || yy < 0 || xx >= img->w || yy >= img->h) continue;
          const size_t i = (static_cast<size_t>(yy) * img->w + xx) * 4;
          a2 = std::max<int>(a2, ink[i + 3]);
          if (std::abs(dx) <= 1 && std::abs(dy) <= 1 && ink[i + 3] > a1) a1 = ink[i + 3], at = i;
        }
      const float ia = a1 / 255.0f, sa = 0.6f * a2 / 255.0f * (1.0f - ia), a = ia + sa;
      uint8_t* o = &img->rgba[(static_cast<size_t>(y) * img->w + x) * 4];
      if (a <= 0.0f) continue;
      for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(std::lround((ink[at + k] * ia + 12.0f * sa) / a));
      o[3] = static_cast<uint8_t>(std::lround(a * 255.0f));
    }
  ImgP p = s.strokes.empty() ? ImgP() : ImgP(img);
  cache[c.key] = p;
  return p;
}
#endif

// A pocket: a card you have in its finest finish (its stock, a little prism, its colours' light, its portrait, its name,
// how many, GOLD /10, BLACK 1/1), or a dark silhouette with its number.
// r149: a case hit's pocket its own look (above its finish, under a BLACK 1/1): TIZM BOOM pop-art (yellow, halftone, a
// red burst, an ink outline, BOOM!); HOMETOWN sideways (its home's picture and its character turned a quarter, as its
// landscape card is held upright; HOMETOWN and its name up its sides); SIGNED - and every BLACK 1/1 - its autograph
// across its picture and AUTO.
// r149 (Lance: "you neeeeed to show the card as shiny so it stands out from the rest in the binder, it took me farrrr too
// long to find that damn card i just got bc the only thing you did was add a rain bow line"): in the binder a hit pops out
// of its page - a SHINY its shiny picture (`pic`: the caller's), a holo rainbow over the whole pocket, a glowing rainbow
// border, sparkles and a SHINY badge; a SILVER a refractor's rainbow sheen and a bright silver halo; a GOLD a gold halo and
// border and its number; a BLACK obsidian with a violet glow; `isNew` a NEW tag (a card from a pack you haven't looked at
// in the binder yet). r149 (Lance: "never display if you have a shiny or not or silver or whatever tf at the trade in"):
// `baseOnly` (the TRADE-INS page) the plain BASE card whatever else you have of it - BASE stock, no finish, no shiny, its
// corner BASE
void mini_card0(Pc& P, const Card& c, const Owned& o, float x0, float y0, float x1, float y1, const ImgP& pic, bool baseOnly,
                bool isNew);
// r217: a pocket - its card as before; a JAM '96 card die-cut (its crest's shoulders cut from the pocket's top corners) and its '96
// sticker, so the binder's Jam pages read as the other set from across the page
std::vector<uint8_t> board_px(int w, int h, const Pc& P);  // (below)
void mini_exq(Pc& P, const Card& c, const Owned& o, float x0, float y0, float x1, float y1, const ImgP& pic, bool isNew);  // r230 (below)
void mini_card(Pc& P, const Card& c, const Owned& o, float x0, float y0, float x1, float y1, const ImgP& pic, bool baseOnly = false,
               bool isNew = false) {
  struct ArtFrom {  // r236: card art keeps its own look in the hub (xr_text.h BoardPiece::art)
    std::vector<xrtext::BoardPiece>& v;
    size_t at;
    ~ArtFrom() {
      for (size_t i = at; i < v.size(); ++i) v[i].art = true;
    }
  } artFrom{P.v, P.v.size()};
  if (c.set == 2 && (baseOnly || o.bestGrade <= 0)) {  // r230: a LANCINI EXQUISITE pocket (NOIR's black + gold)
    mini_exq(P, c, o, x0, y0, x1, y1, pic, isNew);
    return;
  }
  if (c.set != 1 || (!baseOnly && o.bestGrade > 0)) {  // (a slab's pocket: its case)
    mini_card0(P, c, o, x0, y0, x1, y1, pic, baseOnly, isNew);
    return;
  }
  const float w = x1 - x0, h = y1 - y0;
  // r220: its best copy's own die (lk::jam_shape: the crest, a crown, the star crown ...)
  Ref best;
  best.parallel = baseOnly ? kBase : std::max(0, o.best());
  best.hit = baseOnly || best.parallel == kBlack ? kHitNone : o.hitBest();
  best.shiny = !baseOnly && o.shinyAny();
  // r223 (Lance 11-12 on the JAM '96 cards: "the tips up top are like invisible with a black outline"): r220 painted the HUB's page navy
  // (0x10131C) over what the die cuts away - right on the hub's page, but the 3D binder's pages are white pockets (and TIZM GRADING's
  // page another blue): dark wedges at every Jam pocket's top inside the pocket's outline. Now the pocket is drawn into its own picture
  // (the pocket + its halo round it, on the page's own pixel grid) and cut to its die there: what the die cuts away is see-through, so
  // whatever page it's on shows - the hub's exactly as before
  constexpr float kHalo = 26.0f;  // (mini_card0's halos reach 24 past the pocket)
  const float fx0 = std::floor(P.ox + (x0 - kHalo) * P.k), fy0 = std::floor(P.oy + (y0 - kHalo) * P.k);
  const float fx1 = std::ceil(P.ox + (x1 + kHalo) * P.k), fy1 = std::ceil(P.oy + (y1 + kHalo) * P.k);
  const int iw = static_cast<int>(fx1 - fx0), ih = static_cast<int>(fy1 - fy0);
  const int shape = lk::jam_shape(best);
  if (iw > 0 && ih > 0 && iw <= 2048 && ih <= 2048 && P.k > 1e-4f) {
    Pc Q;
    Q.k = P.k;
    Q.ox = P.ox - fx0, Q.oy = P.oy - fy0;
    Q.tSlant = P.tSlant, Q.tXscale = P.tXscale, Q.tAngle = P.tAngle, Q.tShadow = P.tShadow, Q.tShadowA = P.tShadowA;
    Q.tShadowRgb = P.tShadowRgb, Q.tRim = P.tRim;
    mini_card0(Q, c, o, x0, y0, x1, y1, pic, baseOnly, isNew);
    auto img = std::make_shared<Img>();
    img->w = iw, img->h = ih;
    img->rgba = board_px(iw, ih, Q);
    // its die over the pocket (box-filtered, the pocket's size in the page's pixels); its halo outside the pocket as it was
    const float px0 = P.ox + x0 * P.k - fx0, py0 = P.oy + y0 * P.k - fy0, pw = w * P.k, ph = h * P.k;
    const int dw = std::max(16, std::min(kFrontW, static_cast<int>(std::lround(pw)))), dh = std::max(16, std::min(kFrontH, static_cast<int>(std::lround(ph))));
    const ImgP die = lk::cut_small(shape, 0.0f, dw, dh, 0xFFFFFF, false);
    for (int y = 0; y < ih; ++y)
      for (int x = 0; x < iw; ++x) {
        const float u = (x + 0.5f - px0) / pw, v = (y + 0.5f - py0) / ph;
        if (u < 0.0f || v < 0.0f || u >= 1.0f || v >= 1.0f) continue;
        const int sx = std::min(dw - 1, static_cast<int>(u * dw)), sy = std::min(dh - 1, static_cast<int>(v * dh));
        uint8_t& a = img->rgba[(static_cast<size_t>(y) * iw + x) * 4 + 3];
        a = static_cast<uint8_t>((a * die->rgba[(static_cast<size_t>(sy) * dw + sx) * 4 + 3] + 127) / 255);
      }
    P.image(img, (fx0 - P.ox) / P.k, (fy0 - P.oy) / P.k, (fx1 - P.ox) / P.k, (fy1 - P.oy) / P.k, false);
  } else {  // (never on a page; as r220)
    mini_card0(P, c, o, x0, y0, x1, y1, pic, baseOnly, isNew);
    const int iw2 = std::max(16, std::min(400, static_cast<int>(std::lround(w)))), ih2 = std::max(16, std::min(560, static_cast<int>(std::lround(h))));
    P.image(lk::cut_small(shape, 0.0f, iw2, ih2, 0x10131C, true), x0 - 0.5f, y0 - 0.5f, x1 + 0.5f, y1 + 0.5f, false);
  }
  P.box(x0 + 0.33f * w, y0 + 2, x1 - 0.33f * w, y0 + 0.07f * h, 0.035f * h, 0x0C0814);
  P.text("'96", 0.5f * (x0 + x1), y0 + 0.037f * h, 0.055f * h, 0.3f * w, 0xFFD400, 0);
}
void mini_card0(Pc& P, const Card& c, const Owned& o, float x0, float y0, float x1, float y1, const ImgP& pic, bool baseOnly,
                bool isNew) {
  const float w = x1 - x0, h = y1 - y0, cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
  if (!baseOnly && o.bestGrade > 0) {  // r152: a card with a TIZM GRADING slab back: its best slab
    void slab_pocket(Pc & P, const Card& c, const Ref& r, int grade, uint32_t cert, int left, float x0, float y0, float x1, float y1,
                     const ImgP& pic, bool shown, bool isNew, int count);
    const ImgP spic = o.bestSlab.shiny ? card_art(c, true, 0.0f) : portrait(c.key);
    slab_pocket(P, c, o.bestSlab, o.bestGrade, o.bestCert, 0, x0, y0, x1, y1, spic, false, isNew, o.total());
    return;
  }
  const int best = baseOnly ? (o.total() > 0 ? kBase : -1) : o.best();
  const bool shiny = !baseOnly && o.shinyAny();
  const int hit = baseOnly || best == kBlack ? kHitNone : o.hitBest();  // r149
  bool autograph = !baseOnly && best == kBlack;
  for (const Ref& r : o.hits) autograph = autograph || (!baseOnly && r.hit == kHitSigned);
  if (best >= 0 && hit == kHitHome) {  // r149: HOMETOWN, sideways
    P.glow(x0 - 22, y0 - 22, x1 + 22, y1 + 22, 0x6AC8FF, 0.7f);
    const Stock& st = kStock[best];
    P.box(x0, y0, x1, y1, 14, st.s1, 1.0f, st.s2);
    P.box(x0 + 6, y0 + 6, x1 - 6, y1 - 6, 10, 0x0E1A30);
    P.figure(home_pocket_pic(c), x0 + 8, y0 + 8, x1 - 62, y1 - 8, true, 0.5f, 0.5f);
    P.glow(x0 + 8, y0 + 8, x1 - 62, y1 - 8, 0xFFE6A0, 0.18f);
    P.centred(img_turned("pic:" + c.key + (shiny ? ":s" : ""), pic), x0 + 30, y0 + 40, x1 - 66, y1 - 40, 0.92f);
    P.ring(x0 + 8, y0 + 8, x1 - 62, y1 - 8, 8, 4, best == kGoldTizm ? kGold : 0xFFFFFF);
    P.box(x1 - 60, y0 + 8, x1 - 8, y1 - 8, 8, kNavy, 0.94f);  // its name up its right side (its landscape's foot)
    P.style(0.0f, 1.0f, -90.0f);
    P.text(upper(c.name), x1 - 34, cy, 28, h - 40, 0xFFFFFF, 2);
    P.box(x0 + 10, y0 + 62, x0 + 42, y1 - 104, 8, 0xC0392B);  // HOMETOWN up its left (its landscape's top)
    P.text("HOMETOWN", x0 + 26, 0.5f * (y0 + 62 + y1 - 104), 19, h - 176, 0xFFFFFF, 2);
    P.plain();
    P.left("#" + num3(c.num), x0 + 14, y0 + 26, 28, 100, 0xFFFFFF, 3);
    if (best >= kSilver) {
      const std::string tag = best == kGoldTizm ? (o.golds.empty() ? std::string("/10") : (o.golds.front() < 10 ? "0" : "") +
                                                                                              std::to_string(o.golds.front()) + "/10")
                                                : std::string("SILVER");
      const float tw = P.width(tag, 22) + 18;
      P.box(x0 + 8, y1 - 98, x0 + 8 + tw, y1 - 66, 16, kFinishPill[best]);
      P.text(tag, x0 + 8 + tw / 2, y1 - 82, 22, tw - 10, kFinishInk[best]);
    }
    if (shiny) {
      P.box(x0 + 8, y1 - 60, x0 + 82, y1 - 30, 15, 0xFFFFFF);
      P.ring(x0 + 8, y1 - 60, x0 + 82, y1 - 30, 15, 3, 0xB06AFF);
      P.text("SHINY", x0 + 45, y1 - 45, 20, 66, 0x2A0A5A);
    }
    if (o.total() > 1) {
      P.box(x1 - 72, y0 + 8, x1 - 8, y0 + 50, 20, 0xD3202A);
      P.text("X" + std::to_string(o.total()), x1 - 40, y0 + 29, 30, 58, 0xFFFFFF, 2);
    }
    const int lv = card_level(o);
    for (int s = 0; s < lv && lv > 1; ++s) star_shape(P, x0 + 22 + s * 24.0f, y0 + 60, 10.5f, kGold);
    if (isNew) {
      P.box(x0 + 4, y0 + 4, x0 + 86, y0 + 46, 21, 0xFF3B30);
      P.ring(x0 + 4, y0 + 4, x0 + 86, y0 + 46, 21, 3, 0xFFFFFF);
      P.text("NEW", x0 + 45, y0 + 25, 28, 74, 0xFFFFFF, 2);
    }
    P.poly({x0 + 4, y0 + h * 0.36f, x1 - 4, y0 + h * 0.06f, x1 - 4, y0 + h * 0.2f, x0 + 4, y0 + h * 0.5f}, 0xFFFFFF, 0.06f);
    return;
  }
  if (best < 0) {
    P.box(x0, y0, x1, y1, 14, 0x161C2E);
    P.ring(x0, y0, x1, y1, 14, 3, 0x2A3354);
    P.centred(pic, x0 + 14, y0 + 30, x1 - 14, y1 - 64, 0.9f, 0x232B44);  // (r140: centred)
    P.text("#" + num3(c.num), cx, y1 - 36, 40, w - 20, 0x5A6684, 2);
  } else {
    // its halo past the pocket (in the gap round it): what it is, from across the page
    if (hit == kHitBoom) {  // r149
      P.glow(x0 - 24, y0 - 24, x1 + 24, y1 + 24, 0xFF5A1E, 0.8f);
    } else if (shiny) {
      P.glow(x0 - 22, y0 - 22, cx + 12, y1 + 22, 0xFF6AD8, 0.75f);
      P.glow(cx - 12, y0 - 22, x1 + 22, y1 + 22, 0x5AD8FF, 0.75f);
    } else if (best == kGoldTizm) {
      P.glow(x0 - 22, y0 - 22, x1 + 22, y1 + 22, 0xFFC93A, 0.8f);
    } else if (best == kBlack) {
      P.glow(x0 - 22, y0 - 22, x1 + 22, y1 + 22, 0x8A50F0, 0.75f);
    } else if (best == kSilver) {
      P.glow(x0 - 18, y0 - 18, x1 + 18, y1 + 18, 0xDDE6FF, 0.55f);
    }
    const Stock& st = kStock[best];
    P.box(x0, y0, x1, y1, 14, st.s1, 1.0f, st.s2);
    if (best == kBase) {  // r138: a printed card, plainly (its team's colours in a white border)
      P.box(x0 + 7, y0 + 7, x1 - 7, y1 - 7, 10, light(c.c1, 0.12f), 1.0f, dark(c.c1, 0.42f));
      P.glow(x0 + 10, y0 + 20, x1 - 10, y1 - 30, 0xFFFFFF, 0.35f);
    } else {
      prism(P, x0 + 4, y0 + 4, x1 - 4, y1 - 4, cx, y0 + 0.42f * h, 14, 0.2f, st.lite, st.mid, st.dark, 0.5f);
      P.glow(x0 - 10, y0 + 20, x1 + 10, y1 - 30, best == kBlack ? 0x6A30D0 : c.c1, 0.55f);
      if (best == kSilver) holo_bands(P, x0 + 4, y0 + 4, x1 - 4, y1 - 4, 18, 0.13f, 0.1f);  // (its refractor)
    }
    if (hit == kHitBoom) {  // r149: TIZM BOOM pop-art (r150: its face clean, as its card's - a pop-art frame, a clean panel)
      // r166: its printed page (as its card's face, pp_page): cream stock, an inked panel, a Ben-Day dot sky and sun
      P.box(x0 + 5, y0 + 5, x1 - 5, y1 - 5, 10, kPpStock, 1.0f, 0xEFE3C4);
      P.box(x0 + 13, y0 + 27, x1 - 13, y1 - 61, 9, kPpInk);
      P.box(x0 + 17, y0 + 31, x1 - 17, y1 - 65, 7, kPpSky);
      P.dots(x0 + 17, y0 + 31, x1 - 17, y1 - 65, 9, 0.12f, 0.5f, kPpSkyDot, 0.75f);
      P.glow(x0 + 30, y0 + 50, x1 - 30, y1 - 100, kPpSun, 0.55f);
    }
    if (shiny) holo_bands(P, x0 + 4, y0 + 4, x1 - 4, y1 - 4, 22, 0.2f, 0.0f);  // (r149: holo under its picture)
    P.centred(pic, x0 + 10, y0 + 34, x1 - 10, y1 - 64, 0.9f);  // (r140: centred)
#if __has_include("xr_sign.h")
    if (autograph)  // r149: its autograph across its picture
      if (const ImgP sg = sign_pic(c)) P.image(sg, x0 + 6, y0 + 0.2f * h, x1 - 6, y0 + 0.2f * h + 0.5f * (w - 12), false);
#endif
    if (shiny) {  // r149: the holo over it too, faint; sparkles
      holo_bands(P, x0 + 4, y0 + 4, x1 - 4, y1 - 64, 22, 0.1f, 0.5f);
      static const float kSp[6][3] = {{0.16f, 0.30f, 13}, {0.84f, 0.24f, 10}, {0.78f, 0.62f, 15}, {0.22f, 0.70f, 9}, {0.52f, 0.14f, 8},
                                      {0.60f, 0.80f, 7}};
      for (const auto& s : kSp) glint(P, x0 + s[0] * w, y0 + s[1] * h, s[2], 0xFFFFFF, 0.9f);
    }
    // its border: a SHINY's glowing rainbow all round; a parallel's its metal
    if (shiny) {
      const int n = 32;
      for (int k = 0; k < n; ++k) {
        const float u0 = static_cast<float>(k) / n, u1 = static_cast<float>(k + 1) / n;
        auto at = [&](float u, float* px, float* py) {  // (round the edge: top, right, bottom, left)
          const float per = 2.0f * (w + h), d = u * per;
          if (d < w) *px = x0 + d, *py = y0;
          else if (d < w + h) *px = x1, *py = y0 + (d - w);
          else if (d < 2 * w + h) *px = x1 - (d - w - h), *py = y1;
          else *px = x0, *py = y1 - (d - 2 * w - h);
        };
        float ax, ay, bx, by;
        at(u0, &ax, &ay);
        at(u1, &bx, &by);
        P.box(std::min(ax, bx) - 4, std::min(ay, by) - 4, std::max(ax, bx) + 4, std::max(ay, by) + 4, 3, rainbow_at(u0 * 2.0f));
      }
      P.ring(x0 + 5, y0 + 5, x1 - 5, y1 - 5, 11, 2, 0xFFFFFF, 0.8f);
    } else if (hit == kHitBoom) {  // r149: its ink outline
      P.ring(x0, y0, x1, y1, 14, 8, 0x111111);
      P.ring(x0 + 8, y0 + 8, x1 - 8, y1 - 8, 8, 3, best == kGoldTizm ? kGold : 0xFFFFFF);
    } else if (best == kGoldTizm) {
      P.ring(x0, y0, x1, y1, 14, 7, kGold);
    } else if (best == kBlack) {
      P.ring(x0, y0, x1, y1, 14, 6, 0x8A50F0);
    } else if (best == kSilver) {
      P.ring(x0, y0, x1, y1, 14, 6, 0xF4F8FF);
    }
    P.box(x0 + 6, y1 - 60, x1 - 6, y1 - 6, 8, kNavy, 0.92f);
    if (c.sub.empty()) {
      P.text(upper(c.name), cx, y1 - 33, 30, w - 24, 0xFFFFFF, 2);
    } else {  // r152: a costume's own name, its hero under it
      P.text(upper(c.name), cx, y1 - 42, 25, w - 22, 0xFFFFFF, 2);
      P.text(upper(c.sub), cx, y1 - 18, 18, w - 30, kGold, 1);
    }
    P.left("#" + num3(c.num), x0 + 14, y0 + 26, 28, 100, 0xFFFFFF, 3);
    if (best >= kSilver) {  // its finish (a GOLD's own number)
      const std::string tag = best == kBlack ? "1/1" : (best == kGoldTizm ? (o.golds.empty() ? std::string("/10")
                                                                                             : (o.golds.front() < 10 ? "0" : "") +
                                                                                                   std::to_string(o.golds.front()) + "/10")
                                                                          : std::string("SILVER"));
      const float tw = P.width(tag, shiny ? 22 : 26) + 20;
      P.box(x0 + 8, y1 - 98, x0 + 8 + tw, y1 - 66, 16, kFinishPill[best]);
      P.text(tag, x0 + 8 + tw / 2, y1 - 82, shiny ? 22 : 26, tw - 10, kFinishInk[best]);
    }
    if (shiny) {  // r149: its SHINY badge
      const float tw = P.width("SHINY", 24) + 24;
      P.box(x1 - 8 - tw, y1 - 98, x1 - 8, y1 - 66, 16, 0xFFFFFF);
      P.ring(x1 - 8 - tw, y1 - 98, x1 - 8, y1 - 66, 16, 3, 0xB06AFF);
      P.text("SHINY", x1 - 8 - tw / 2, y1 - 82, 24, tw - 10, 0x2A0A5A);
    }
    if (hit == kHitBoom || autograph) {  // r149: its case hit's badge (a row up), BOOM! / AUTO
      const std::string tag = hit == kHitBoom ? "BOOM!" : "AUTO";
      const float tw = P.width(tag, 24) + 26, bx = hit == kHitBoom && autograph ? x0 + 8 : cx - tw / 2;
      P.box(bx, y1 - 134, bx + tw, y1 - 102, 16, hit == kHitBoom ? 0xFFE14A : 0x1A1206);
      P.ring(bx, y1 - 134, bx + tw, y1 - 102, 16, 3, hit == kHitBoom ? 0x111111 : kGold);
      P.text(tag, bx + tw / 2, y1 - 118, 24, tw - 10, hit == kHitBoom ? 0xE0201A : kGold);
      if (hit == kHitBoom && autograph) {
        const float aw = P.width("AUTO", 24) + 26;
        P.box(x1 - 8 - aw, y1 - 134, x1 - 8, y1 - 102, 16, 0x1A1206);
        P.ring(x1 - 8 - aw, y1 - 134, x1 - 8, y1 - 102, 16, 3, kGold);
        P.text("AUTO", x1 - 8 - aw / 2, y1 - 118, 24, aw - 10, kGold);
      }
    }
    if (baseOnly) {  // r149: what's traded: its BASE copies
      P.box(x1 - 78, y0 + 8, x1 - 8, y0 + 50, 20, kNavy);
      P.ring(x1 - 78, y0 + 8, x1 - 8, y0 + 50, 20, 3, 0xFFFFFF);
      P.text("BASE", x1 - 43, y0 + 29, 26, 62, 0xFFFFFF, 1);
    } else if (o.total() > 1) {
      P.box(x1 - 72, y0 + 8, x1 - 8, y0 + 50, 20, 0xD3202A);
      P.text("X" + std::to_string(o.total()), x1 - 40, y0 + 29, 30, 58, 0xFFFFFF, 2);
    }
    const int lv = card_level(o);  // r146: its level's stars, under its number
    for (int s = 0; s < lv && lv > 1; ++s) star_shape(P, x0 + 22 + s * 24.0f, y0 + 60, 10.5f, kGold);
    if (isNew && !baseOnly) {  // r149: NEW (from a pack, not looked at in the binder yet)
      P.box(x0 + 4, y0 + 4, x0 + 86, y0 + 46, 21, 0xFF3B30);
      P.ring(x0 + 4, y0 + 4, x0 + 86, y0 + 46, 21, 3, 0xFFFFFF);
      P.text("NEW", x0 + 45, y0 + 25, 28, 74, 0xFFFFFF, 2);
    }
  }
  P.poly({x0 + 4, y0 + h * 0.36f, x1 - 4, y0 + h * 0.06f, x1 - 4, y0 + h * 0.2f, x0 + 4, y0 + h * 0.5f}, 0xFFFFFF, 0.06f);  // the sleeve
}

// r230: a LANCINI EXQUISITE pocket - NOIR in small: matte black, a gold hairline, an arched window spotlit in its colour with its portrait,
// its name in gold, its best copy's tier + number on a pill in the tier's colour (EMERALD green, SAPPHIRE blue ...), RC for a rookie; a
// card you don't have: a dark silhouette and its checklist number
uint32_t exq_tier_rgb(int t) {
  switch (t) {
    case xrexq::kSilverLeaf: case xrexq::kPatch: return 0xC9CED6;
    case xrexq::kGoldLeaf: case xrexq::kAuto: case xrexq::kRpa: case xrexq::kPatchAuto: return 0xE6C774;
    case xrexq::kEmerald: return 0x2FC07E;
    case xrexq::kSapphire: return 0x4A7CF0;
    case xrexq::kMasterpiece: return 0xFFD86A;
    default: return 0xCDBB95;
  }
}
void mini_exq(Pc& P, const Card& c, const Owned& o, float x0, float y0, float x1, float y1, const ImgP& pic, bool isNew) {
  const float w = x1 - x0, h = y1 - y0, cx = 0.5f * (x0 + x1);
  const Ref best = exq_best(o, true);
  const int t = best.card >= 0 ? xrexq::tier_of(best.serial) : 0;
  if (t == 0) {
    P.box(x0, y0, x1, y1, 14, 0x0C0C0E);
    P.ring(x0 + 5, y0 + 5, x1 - 5, y1 - 5, 10, 2, 0x3A3426);
    P.centred(pic, x0 + 14, y0 + 30, x1 - 14, y1 - 64, 0.9f, 0x1E1C18);
    P.text("#" + num3(c.num), cx, y1 - 36, 40, w - 20, 0x5A5240, 2);
    return;
  }
  const uint32_t tc = exq_tier_rgb(t);
  P.glow(x0 - 22, y0 - 22, x1 + 22, y1 + 22, tc, t >= xrexq::kGoldLeaf ? 0.75f : 0.45f);
  P.box(x0, y0, x1, y1, 14, t == xrexq::kEmerald ? 0x07281B : (t == xrexq::kSapphire ? 0x0A1A3C : 0x0E0E10));
  P.ring(x0 + 4, y0 + 4, x1 - 4, y1 - 4, 11, t == xrexq::kMasterpiece || t == xrexq::kGoldLeaf ? 6 : 3, tc);
  P.box(x0 + 14, y0 + 30, x1 - 14, y1 - 92, 0.42f * (w - 28), dark(c.c1, 0.55f), 1.0f, dark(c.c1, 0.85f));  // (its arched window)
  P.glow(x0 + 20, y0 + 40, x1 - 20, y1 - 96, light(c.c1, 0.3f), 0.35f);
  P.centred(pic, x0 + 16, y0 + 36, x1 - 16, y1 - 94, 0.92f);
  P.text(upper(c.name), cx, y1 - 74, 26, w - 24, 0xE6C774, 2);
  const std::string tag = std::string(xrexq::short_name(t, exq_rookie(c))) + " " + xrexq::num_text(best.serial);
  const float tw = std::min(w - 16, P.width(tag, 22) + 20);
  P.box(cx - tw / 2, y1 - 52, cx + tw / 2, y1 - 20, 16, tc);
  P.text(tag, cx, y1 - 36, 22, tw - 10, t == xrexq::kSapphire || t == xrexq::kEmerald ? 0xFFFFFF : 0x1A1206);
  P.left("EXQ", x0 + 12, y0 + 22, 22, 80, 0xE6C774, 2);
  if (exq_rookie(c)) {
    P.box(x1 - 56, y0 + 8, x1 - 10, y0 + 44, 10, 0xE6C774);
    P.text("RC", x1 - 33, y0 + 26, 24, 40, 0x1A1206);
  }
  if (o.total() > 1) {
    P.box(x0 + 8, y1 - 132, x0 + 70, y1 - 98, 17, 0xD3202A);
    P.text("X" + std::to_string(o.total()), x0 + 39, y1 - 115, 24, 54, 0xFFFFFF, 2);
  }
  if (isNew) {
    P.box(x0 + 4, y0 + 40, x0 + 86, y0 + 82, 21, 0xFF3B30);
    P.ring(x0 + 4, y0 + 40, x0 + 86, y0 + 82, 21, 3, 0xFFFFFF);
    P.text("NEW", x0 + 45, y0 + 61, 28, 74, 0xFFFFFF, 2);
  }
  P.poly({x0 + 4, y0 + h * 0.36f, x1 - 4, y0 + h * 0.06f, x1 - 4, y0 + h * 0.2f, x0 + 4, y0 + h * 0.5f}, 0xFFFFFF, 0.05f);  // the sleeve
}
// r152: a TIZM GRADING slab's label (w x h, ~3.5 : 1): LANCINI TIZM GRADING, its card's name and number, its finish /
// shiny / case hit, its set, its cert; its grade huge on the right with its words (GEM MINT, MINT, NM-MT ...). A GEM
// MINT 10's label gold and holographic. grade 0: at grading (no grade yet)
std::string ref_words(const Ref& r);  // (below)
void slab_label(Pc& P, const Card& c, const Ref& r, int grade, uint32_t cert, float w, float h) {
  const bool gem = grade == 10;
  const float k = h / 274.0f;  // (drawn for 960 x 274)
  if (c.set == 2) {  // r230: a LANCINI EXQUISITE slab's label: matte black, gold hairlines, the gold name, its tier + number, a gold grade disc
    P.box(0, 0, w, h, 18 * k, 0x0C0C0E);
    P.ring(8 * k, 8 * k, w - 8 * k, h - 8 * k, 12 * k, 3 * k, 0xC9A04A);
    P.left("LANCINI TIZM GRADING  -  EXQUISITE", 34 * k, 52 * k, 30 * k, w * 0.6f, 0xEDE6D3, 2 * k);
    P.left(upper(c.name) + "  #" + num3(c.num), 30 * k, 122 * k, 48 * k, w * 0.64f, 0xE6C774, 3 * k);
    P.left(xrexq::words(r.serial, exq_rookie(c)), 30 * k, 176 * k, 30 * k, w * 0.64f, 0xFFFFFF, 2 * k);
    P.left(std::string(xrexq::kSetName) + "   CERT #" + std::to_string(cert), 30 * k, 226 * k, 22 * k, w * 0.64f, 0xB8A57A, 0);
    const float gcx = w - 150 * k, gcy = 0.5f * h, gr = 112 * k;
    P.box(gcx - gr, gcy - gr, gcx + gr, gcy + gr, gr, 0xC9A04A);
    P.box(gcx - gr + 8 * k, gcy - gr + 8 * k, gcx + gr - 8 * k, gcy + gr - 8 * k, gr, 0x0C0C0E);
    if (grade > 0) {
      P.text(std::to_string(grade), gcx, gcy - 18 * k, 120 * k, 2 * gr, 0xE6C774, 3 * k);
      P.text(kGradeName[grade], gcx, gcy + 70 * k, 28 * k, 2 * gr - 30 * k, 0xEDE6D3, 2 * k);
    } else {
      P.text("AT GRADING", gcx, gcy, 30 * k, 2 * gr - 30 * k, 0xEDE6D3, 2 * k);
    }
    return;
  }
  if (c.set == 1) {  // r217: a JAM '96 slab's label (mockup B): ink, a purple-teal marble field, the name in yellow, a yellow grade disc
    P.box(0, 0, w, h, 18 * k, 0x0C0814);
    P.box(8 * k, 8 * k, w - 8 * k, h - 8 * k, 12 * k, 0x4A1A8A, 1.0f, 0x1A6AAA);
    P.glow(w * 0.1f, -h * 0.4f, w * 0.7f, h * 0.9f, 0xE0218A, 0.35f);
    P.dots(8 * k, 8 * k, w - 8 * k, h - 8 * k, 16 * k, 0.05f, 0.3f, 0x0C0814, 0.25f);
    hexagon(P, 52 * k, 58 * k, 30 * k, 0x0C0814);
    hexagon(P, 52 * k, 58 * k, 22 * k, 0xE8ECF5);
    P.left("LANCINI TIZM GRADING  -  JAM '96", 96 * k, 58 * k, 32 * k, w * 0.6f, 0xFFFFFF, 2 * k);
    P.left(upper(c.name) + "  #" + num3(c.num), 30 * k, 128 * k, 48 * k, w * 0.64f, 0xFFD400, 3 * k);
    P.left(ref_words(r), 30 * k, 182 * k, 30 * k, w * 0.64f, 0xFFFFFF, 2 * k);
    P.left("2026-27 STRIKERS   CERT #" + std::to_string(cert), 30 * k, 230 * k, 24 * k, w * 0.64f, 0xB8A8E8, 0);
    const float gcx = w - 150 * k, gcy = 0.5f * h, gr = 112 * k;
    P.box(gcx - gr, gcy - gr, gcx + gr, gcy + gr, gr, 0x0C0814);
    P.box(gcx - gr + 12 * k, gcy - gr + 12 * k, gcx + gr - 12 * k, gcy + gr - 12 * k, gr - 12 * k, gem ? 0xFFD400 : 0xFFE680);
    if (grade > 0) {
      P.text(kGradeName[grade], gcx, gcy - 62 * k, 26 * k, 1.5f * gr, 0x0C0814, 0);
      P.text(std::to_string(grade), gcx, gcy + 18 * k, 120 * k, 1.5f * gr, 0xFFFFFF, 6 * k);
    } else {
      P.text("AT GRADING", gcx, gcy - 62 * k, 24 * k, 1.5f * gr, 0x0C0814, 0);
      P.text("?", gcx, gcy + 18 * k, 110 * k, 1.5f * gr, 0xFFFFFF, 6 * k);
    }
    return;
  }
  P.box(0, 0, w, h, 18 * k, gem ? 0xFFF1B0 : 0xF7F8FA, 1.0f, gem ? 0xE2AE3A : 0xE3E7EE);
  if (gem) holo_bands(P, 4 * k, 4 * k, w - 4 * k, h - 4 * k, 34 * k, 0.16f, 0.15f);
  P.ring(0, 0, w, h, 18 * k, 5 * k, gem ? 0x9A6A10 : 0x1A2233);
  P.ring(10 * k, 10 * k, w - 10 * k, h - 10 * k, 12 * k, 2 * k, gem ? 0xC8961E : 0xB8C0D0);
  hexagon(P, 52 * k, 58 * k, 30 * k, gem ? 0x6A4A08 : 0x1A2233);
  hexagon(P, 52 * k, 58 * k, 20 * k, gem ? 0xFFE27A : 0x8E9BB4);
  P.left("LANCINI TIZM GRADING", 96 * k, 58 * k, 34 * k, w * 0.62f, gem ? 0x5A3A00 : 0x0B1022, 0);
  P.left(upper(c.name) + "  #" + num3(c.num), 30 * k, 128 * k, 46 * k, w * 0.66f, 0x0B1022, 0);
  P.left(ref_words(r), 30 * k, 180 * k, 30 * k, w * 0.66f, gem ? 0x6A4A08 : 0x3A4A70, 0);
  P.left(std::string(kSetName) + "   CERT #" + std::to_string(cert), 30 * k, 230 * k, 24 * k, w * 0.66f, 0x5A6684, 0);
  const float gx0 = w - 290 * k, gx1 = w - 24 * k, gcx = 0.5f * (gx0 + gx1);
  P.box(gx0, 22 * k, gx1, h - 22 * k, 14 * k, gem ? 0x1A1206 : 0x0B1022);
  if (grade > 0) {
    P.text(kGradeName[grade], gcx, 64 * k, 34 * k, gx1 - gx0 - 20 * k, gem ? kGold : 0xFFFFFF, 0);
    P.text(std::to_string(grade), gcx, 168 * k, 150 * k, gx1 - gx0 - 30 * k, gem ? kGold : 0xFFFFFF, 0);
  } else {
    P.text("AT GRADING", gcx, 64 * k, 30 * k, gx1 - gx0 - 20 * k, 0xB8C2D6, 0);
    P.text("?", gcx, 168 * k, 140 * k, gx1 - gx0 - 30 * k, 0x8A94AA, 0);
  }
}
// the words for a copy: its finish, shiny, case hit ("GOLD TIZM 07/10  -  SHINY  -  TIZM BOOM")
std::string ref_words(const Ref& r) {
  std::string s = kParallelName[r.parallel];
  if (r.parallel == kGoldTizm && r.serial > 0) s += std::string(" ") + (r.serial < 10 ? "0" : "") + std::to_string(r.serial) + "/10";
  if (r.parallel == kBlack) s += " 1/1";
  if (r.shiny) s += "  -  SHINY";
  if (r.hit != kHitNone) s += std::string("  -  ") + kHitName[r.hit];
  return s;
}
// r152: a slab as a pocket (the binder, the GRADING page): the clear case, its label strip on top, its card in it as it
// is (its finish, shiny, case hit); at grading (left > 0) greyed with the matches to go. `shown`: on your room's display
void mini_card(Pc& P, const Card& c, const Owned& o, float x0, float y0, float x1, float y1, const ImgP& pic, bool baseOnly,
               bool isNew);
void slab_pocket(Pc& P, const Card& c, const Ref& r, int grade, uint32_t cert, int left, float x0, float y0, float x1, float y1,
                 const ImgP& pic, bool shown, bool isNew, int count) {
  struct ArtFrom {  // r236: card art keeps its own look in the hub (xr_text.h BoardPiece::art)
    std::vector<xrtext::BoardPiece>& v;
    size_t at;
    ~ArtFrom() {
      for (size_t i = at; i < v.size(); ++i) v[i].art = true;
    }
  } artFrom{P.v, P.v.size()};
  const float w = x1 - x0, h = y1 - y0, cx = 0.5f * (x0 + x1);
  if (grade == 10 && left == 0) P.glow(x0 - 22, y0 - 22, x1 + 22, y1 + 22, 0xFFD24A, 0.8f);
  P.box(x0, y0, x1, y1, 12, 0xC9D6E6, 0.42f);  // (the clear acrylic)
  P.ring(x0, y0, x1, y1, 12, 3, 0xF4F8FF, 0.9f);
  P.ring(x0 + 5, y0 + 5, x1 - 5, y1 - 5, 9, 1.5f, 0xFFFFFF, 0.5f);
  const float lw = w - 12, lh = std::min(lw * 274.0f / 960.0f, 0.2f * h);  // (its label, its own shape)
  {
    const float sox = P.ox, soy = P.oy;
    P.ox = sox + (x0 + 6) * P.k;
    P.oy = soy + (y0 + 6) * P.k;
    slab_label(P, c, r, left > 0 ? 0 : grade, cert, lw, lh);
    P.ox = sox;
    P.oy = soy;
  }
  const float ly1 = y0 + 6 + lh;
  // its card, sealed: a card's shape in what's left - simply (its stock and finish, its picture, its name: small, the
  // character to be seen)
  const float ch = y1 - 10 - (ly1 + 8), cw = std::min(w - 20, ch / 1.4f);  // (a card 1 : 1.4)
  {
    const float a0 = cx - cw / 2, a1 = cx + cw / 2, b0 = ly1 + 8, b1 = y1 - 10, acx = cx;
    const Stock& st = kStock[r.parallel];
    P.box(a0, b0, a1, b1, 6, st.s1, 1.0f, st.s2);
    if (r.hit == kHitBoom) {
      P.box(a0 + 3, b0 + 3, a1 - 3, b1 - 3, 4, kPpStock, 1.0f, 0xEFE3C4);  // r166: its printed page
      P.box(a0 + 7, b0 + 7, a1 - 7, b1 - 21, 4, kPpInk);
      P.box(a0 + 9, b0 + 9, a1 - 9, b1 - 23, 3, kPpSky);
      P.dots(a0 + 9, b0 + 9, a1 - 9, b1 - 23, 6, 0.12f, 0.5f, kPpSkyDot, 0.75f);
    } else if (r.parallel == kBase) {
      P.box(a0 + 3, b0 + 3, a1 - 3, b1 - 3, 4, light(c.c1, 0.12f), 1.0f, dark(c.c1, 0.42f));
    } else {
      prism(P, a0 + 2, b0 + 2, a1 - 2, b1 - 2, acx, b0 + 0.42f * (b1 - b0), 12, 0.2f, st.lite, st.mid, st.dark, 0.5f);
    }
    if (r.shiny) holo_bands(P, a0 + 2, b0 + 2, a1 - 2, b1 - 2, 14, 0.18f, 0.0f);
    P.centred(pic, a0 + 4, b0 + 5, a1 - 4, b1 - 22, 0.94f);
    P.box(a0 + 3, b1 - 21, a1 - 3, b1 - 3, 4, kNavy, 0.92f);
    P.text(upper(c.name), acx, b1 - 12, 14, cw - 10, 0xFFFFFF, 1);
    if (r.parallel == kGoldTizm) P.ring(a0, b0, a1, b1, 6, 3, kGold);
    else if (r.parallel == kBlack) P.ring(a0, b0, a1, b1, 6, 3, 0x8A50F0);
    else if (r.parallel == kSilver) P.ring(a0, b0, a1, b1, 6, 3, 0xF4F8FF);
  }
  P.poly({x0 + 4, y0 + h * 0.62f, x1 - 4, y0 + h * 0.30f, x1 - 4, y0 + h * 0.40f, x0 + 4, y0 + h * 0.72f}, 0xFFFFFF, 0.1f);  // (glare)
  if (left > 0) {
    P.box(x0 + 4, ly1 + 4, x1 - 4, y1 - 4, 8, 0x0B1022, 0.62f);
    P.text("AT TIZM", cx, ly1 + 0.36f * (y1 - ly1), 26, w - 20, 0xFFFFFF, 2);
    P.text("GRADING", cx, ly1 + 0.36f * (y1 - ly1) + 30, 26, w - 20, 0xFFFFFF, 2);
    P.text(std::to_string(left) + (left == 1 ? " MATCH TO GO" : " MATCHES TO GO"), cx, ly1 + 0.36f * (y1 - ly1) + 66, 20, w - 16, kGold, 2);
  }
  if (shown) {  // (on its bottom edge, clear of its card)
    P.box(cx - 34, y1 - 12, cx + 34, y1 + 14, 13, kGold);
    P.text("SHELF", cx, y1 + 1, 17, 60, kNavy, 0);
  }
  if (count > 1) {
    P.box(x0 + 6, y1 - 40, x0 + 64, y1 - 8, 14, 0xD3202A);
    P.text("X" + std::to_string(count), x0 + 35, y1 - 24, 20, 52, 0xFFFFFF, 1);
  }
  if (isNew) {
    P.box(x0 + 4, y0 + 4, x0 + 74, y0 + 38, 17, 0xFF3B30);
    P.text("NEW", x0 + 39, y0 + 21, 22, 60, 0xFFFFFF, 2);
  }
}

// A small pack (the side panel's stack, the opening page): chrome, crimped, the gem and TIZM.
// r217: the CARD SHOP's pictures (below): each pack's own wrapper (small) / its box of five (3/4 view); null while being drawn
ImgP shop_pic(int set, bool box);
void mini_pack(Pc& P, float x0, float y0, float x1, float y1, bool plainBack = false) {
  struct ArtFrom {  // r236: card art keeps its own look in the hub (xr_text.h BoardPiece::art)
    std::vector<xrtext::BoardPiece>& v;
    size_t at;
    ~ArtFrom() {
      for (size_t i = at; i < v.size(); ++i) v[i].art = true;
    }
  } artFrom{P.v, P.v.size()};
  const float w = x1 - x0, cx = 0.5f * (x0 + x1);
  P.box(x0, y0, x1, y1, 10, 0xDCE1EA, 1.0f, 0x7E889A);
  P.box(x0, y0, x1, y0 + 18, 4, 0xEEF1F6, 1.0f, 0x8E97A8);
  P.box(x0, y1 - 18, x1, y1, 4, 0x8E97A8, 1.0f, 0xEEF1F6);
  if (plainBack) return;  // (one behind another: its edge shows)
  prism(P, x0, y0 + 18, x1, y1 - 18, cx, y0 + 0.45f * (y1 - y0), 18, 0.1f, 0xFFFFFF, 0xBFC7D6, 0x7E889C, 0.6f);
  hexagon(P, cx, y0 + 0.3f * (y1 - y0), 0.16f * w, 0xFFFFFF);
  hexagon(P, cx, y0 + 0.3f * (y1 - y0), 0.11f * w, 0x8E9BB4);
  P.style(0.24f, 0.92f);
  P.rim(0x0B0F1A);
  P.text("TIZM", cx, y0 + 0.56f * (y1 - y0), 0.26f * w, w - 20, 0xFFFFFF, 3, 0x9AA6BC);
  P.plain();
  P.box(x0, y0 + 0.68f * (y1 - y0), x1, y0 + 0.78f * (y1 - y0), 0, 0x0E1220);
}

// r140: the coin, at (cx, cy), r its radius. r216 (Lance 11-11: TIZM COINS -> BIT COINS): the BIT COIN everywhere coins show -
// the sportsbook / casino / market's gold coin with the 8-bit B (xrcoin::bit_face); r140-r215 here: chrome round the TIZM gem
void coin_icon(Pc& P, float cx, float cy, float r) {
  P.box(cx - r, cy - r, cx + r, cy + r, r, 0xB8860B);  // its rim
  const float f = 0.86f * r;
  P.box(cx - f, cy - f, cx + f, cy + f, f, 0xFFD23F, 1.0f, 0xE0A800);  // its gold face
  xrcoin::bit_face([&](float x0, float y0, float x1, float y1, uint32_t rgb) { P.box(x0, y0, x1, y1, 0, rgb); }, cx, cy, r);
}

struct BinderView {
  std::vector<int> list;
  std::vector<long long> values;  // r201: the spread's cards' market values (TIZM CARD MARKET; -1 none; r210: 64-bit)
  std::vector<Card> cards;  // the spread's
  std::vector<Owned> owned;
  std::vector<bool> isNew;  // r149: NEW (a pack's, not looked at yet)
  int spread = 0, spreads = 1, cursor = 0, total = 0, here = 0, have = 0, packs = 0, mode = 0;
  int jamHere = 0, jamHave = 0;  // r217: the JAM '96 set's progress (here / have: the LANCINI TIZM set's)
  int exqHere = 0, exqHave = 0;  // r230: LANCINI EXQUISITE's
  int haveTier[3] = {0, 0, 0}, hereTier[3] = {0, 0, 0}, shinies = 0, finishes[4] = {0, 0, 0, 0};
  int hits[4] = {0, 0, 0, 0}, godPacks = 0;  // r149: case hits by kind, GOD PACKs opened
  // r152: TIZM GRADING - your slabs (at grading first, then the best), the cards you can send (their copy, its cost), the
  // cursor (slabs first, then the cards), the first card row shown, the first slab shown, its last word, a send armed
  struct GSlab {
    Card c;
    Slab s;
  };
  std::vector<GSlab> gSlabs;
  struct GTile {
    Card c;
    Owned o;
    Ref r;
    int cost = 0;
  };
  std::vector<GTile> gTiles;
  int gCursor = 0, gTop = 0, gLeft = 0, gArmed = -1;
  std::string gSay;
  // r153: GOAL EXPLOSIONS - the catalogue as it stands for you (owned, equipped), the cursor, a buy armed, its last word
  struct FxTile {
    std::string id, name, desc;
    int price = 0, tier = 0;
    uint32_t rgb1 = 0, rgb2 = 0;
    float secs = 0.0f;
    bool owned = false, equipped = false;
  };
  std::vector<FxTile> fx;
  int fxCursor = 0, fxArmed = -1;
  int fxTop = 0;  // r161: the first row in view (20 explosions + NO EXPLOSION: 6 rows, 4 at a time)
  std::vector<int> fxList;  // r236: GOAL FX / TAKEOVERS: the page's catalogue indices (-1 NO EXPLOSION), the ring's place in it
  int fxPos = 0;
  bool packsOnOpen = true;  // r236: CARDS > PACKS - the ring on OPEN
  bool fxPreviewing = false, fxNone = false;
  std::string fxSay;
  // r160: binder skins + card sleeves - the one you have on of each (the binder page's frame, its pockets' sleeves), the
  // LOOKS page (its row, each row's first tile shown and cursor, a buy asking: kind / which, its last word, a preview up),
  // what you own of each
  int skin = 0, lkRow = 0, lkFirst[2] = {0, 0}, lkCol[2] = {0, 0}, lkArmKind = -1, lkArm = -1;
  bool lkPreview = false, binderOut = false;
  std::string lkSay;
  std::vector<bool> lkOwned[2];
  // r168: BALL SKINS (the LOOKS page's top row): the ring on it (lkBallFocus) and which, what you own, the one on, a
  // preview up (a buy asking: lkArmKind 1, lkArm the ball)
  bool lkBallFocus = true, ballPreview = false;
  int lkBall = 0, ballOn = 0, lkBallFirst = 0;  // (r170: the first ball in view)
  std::vector<bool> ballOwned;
  long long coins = 0;  // r140; r208: 64-bit
  int lastCoins = 0, pick = 0;
  long long lastWhen = 0;
  std::string lastWhy, say;
  bool shop = false;
  // r146: the page (kPage*); the card under the cursor's level and franchise bonus
  int page = kPageBinder;
  int curLevel = 0, curXp = 0;
  std::string curWhy;
  // r146: TRADE-INS: your spare base cards (r147: commons first, the most first), the next trade's take of each, how many in all / not kept
  struct Tile {
    Card c;
    Owned o;
    int spare = 0, next = 0;
    bool kept = false;
  };
  std::vector<Tile> tiles;
  int spareAll = 0, spareFree = 0, tradedCards = 0, tradedPacks = 0, tradeCursor = 0, tradeTop = 0;
  std::string tradeSay;
  // r226: the page's set (its tiles / spareAll / spareFree are that set's), each set's spares (all / not kept), the cursor on the set panels
  int tradeSet = 0;
  int setSpareAll[2] = {0, 0}, setSpareFree[2] = {0, 0};
  bool tradeOnSets = false;
  // r146: SETS: the evolution lines (done first, then nearest), the types, the binder's pages
  struct SetRow {
    std::string name;
    uint32_t rgb = 0;
    int have = 0, total = 0;
    bool done = false, suits = false;
    std::vector<std::pair<std::string, bool>> who;  // (portrait key, yours)
  };
  std::vector<SetRow> lines, types;
  std::vector<float> pageFill;  // each binder page: how full (1 full)
  int linesDone = 0, typesDone = 0, pagesFull = 0, coinPct = 0, setsTop = 0, bonusChars = 0;
};

// "YOU HAVE 3: 2 BASE, GOLD 07/10" (the shiny ones counted after)
std::string have_text(const Owned& o) {
  std::string s = "YOU HAVE " + std::to_string(o.total()) + ":";
  const char* kShort[4] = {"BASE", "SILVER", "GOLD", "BLACK 1/1"};
  bool first = true;
  for (int p = 0; p < 4; ++p) {
    const int n = o.n[p][0] + o.n[p][1];
    if (n == 0) continue;
    s += first ? " " : ", ";
    first = false;
    if (p == kGoldTizm && !o.golds.empty()) {
      s += "GOLD";
      for (int v : o.golds) s += std::string(" ") + (v < 10 ? "0" : "") + std::to_string(v) + "/10";
    } else {
      s += (n > 1 ? std::to_string(n) + " " : std::string()) + kShort[p];
    }
  }
  for (const Ref& h : o.hits) {  // r149: its case hits, each its own copy
    s += first ? " " : ", ";
    first = false;
    s += kHitName[h.hit];
    if (h.parallel != kBase) s += std::string(" ") + kShort[h.parallel];
  }
  if (!o.sealed.empty()) {  // r152: its slabs
    s += first ? " " : ", ";
    first = false;
    s += std::to_string(o.sealed.size()) + " GRADED" + (o.grading > 0 ? " (" + std::to_string(o.grading) + " AT GRADING)" : std::string());
  }
  int sh = 0;
  for (const auto& p : o.n) sh += p[1];
  for (const Ref& h : o.hits) sh += h.shiny ? 1 : 0;
  if (sh > 0) s += " (" + std::to_string(sh) + " SHINY)";
  return s;
}

// r146: TRADE-INS (instead of the binder's pages, LT from the shop): your spare base cards (r147: commons first, the most first, 6 a row, 2 rows
// at a time), the next trade's take of each (a gold "-2"; its level after it), the kept ones dimmed; the slot (A trades the
// next 10 for a pack), what you've traded.
constexpr float kTileW = 176, kTileH = 220, kTileGapX = 24, kTilePitchY = 270;
constexpr int kTileCols = 6, kTileRows = 2;
void trade_tile_rect(int k, float cx, float y0, float* tx0, float* ty0) {  // k: its place among the visible 12
  *tx0 = cx - 0.5f * (kTileCols * kTileW + (kTileCols - 1) * kTileGapX) + (k % kTileCols) * (kTileW + kTileGapX);
  *ty0 = y0 + 236 + (k / kTileCols) * kTilePitchY;
}
void trade_page(Pc& P, const BinderView& b) {
  const float x0 = kAreaX0, x1 = kAreaX1, y0 = kAreaY0, cx = 0.5f * (x0 + x1);
  P.box(x0, y0, x1, 1180, 26, 0x1A2140, 1.0f, 0x0A0E1C);  // (r157 round 2: down to the page area's foot)
  P.text("TRADE-INS", cx, y0 + 50, 70, 900, kGold, 5);
  // r230: LANCINI EXQUISITE has none - every card of it is numbered (no spares: each copy its own number)
  // (r236: "EXQUISITE: NO TRADE-INS - EVERY CARD IS NUMBERED" gone - 24-26 px and always on)
  
  // r226 (Lance 11-12: JAM '96 spares trade in too): the two sets side by side - each its own spares and its own pack (its real wrapper,
  // the shop's picture); the one picked (stick up from the cards, left / right) is the grid below and what A trades. (r146-r225: one
  // count, LANCINI TIZM spares only.)
  for (int set = 0; set < 2; ++set) {
    const float px0 = set == 0 ? x0 + 30 : cx + 10, px1 = set == 0 ? cx - 10 : x1 - 30, py0 = y0 + 96, py1 = y0 + 206;
    const bool on = b.tradeSet == set;
    const uint32_t head = set == 0 ? kGold : 0xFFD400;
    P.box(px0, py0, px1, py1, 24, on ? (set == 0 ? 0x24305C : 0x34185A) : 0x121830);
    if (on) P.ring(px0 - 6, py0 - 6, px1 + 6, py1 + 6, 30, b.tradeOnSets ? 7 : 4, b.tradeOnSets ? head : (set == 0 ? 0x8A94B8 : 0xE0218A));
    const ImgP pic = shop_pic(set, false);
    const float ph = py1 - py0 - 16;
    float tx = px0 + 100;
    if (pic) {
      const float pw = ph * static_cast<float>(pic->w) / pic->h;
      P.image(pic, px0 + 14, py0 + 8, px0 + 14 + pw, py0 + 8 + ph, false);
      tx = px0 + 30 + pw;
    } else {
      mini_pack(P, px0 + 16, py0 + 8, px0 + 80, py0 + 8 + ph);
    }
    const int free = b.setSpareFree[set], packs = free / trade_for(set);
    P.left(set == 0 ? "LANCINI TIZM" : "JAM '96", tx, py0 + 30, 40, px1 - tx - 16, on ? head : 0xB8C2D6, 3);
    P.left(std::to_string(free) + (free == 1 ? " SPARE" : " SPARES") + (packs > 0 ? "  =  " + std::to_string(packs) + (packs == 1 ? " PACK" : " PACKS") : ""),
           tx, py0 + 76, 40, px1 - tx - 16, packs > 0 ? 0xFFFFFF : 0x8A94AA, 2);
    if (b.setSpareAll[set] > free)
      P.right(std::to_string(b.setSpareAll[set] - free) + " KEPT", px1 - 18, py0 + 30, 32, 200, 0xFF9A8A, 2);
  }
  if (b.tiles.empty()) {
    P.box(x0 + 60, y0 + 240, x1 - 60, y0 + 740, 26, 0x121830);
    P.text(b.tradeSet == 1 ? "NO SPARE JAM '96 BASE CARDS YET" : "NO SPARE LANCINI TIZM BASE CARDS YET", cx, y0 + 440, 56, 1300, 0x8A94AA, 3);
    P.text("A CARD YOU HAVE TWO OR MORE OF SHOWS UP HERE", cx, y0 + 520, 40, x1 - x0 - 140, 0xB8C2D6, 2);
  }
  for (int k = 0; k < kTileCols * kTileRows; ++k) {
    const int i = b.tradeTop * kTileCols + k;
    if (i >= static_cast<int>(b.tiles.size())) break;
    const BinderView::Tile& t = b.tiles[static_cast<size_t>(i)];
    float tx0, ty0;
    trade_tile_rect(k, cx, y0, &tx0, &ty0);
    const float tx1 = tx0 + kTileW, ty1 = ty0 + kTileH, tcx = 0.5f * (tx0 + tx1);
    mini_card(P, t.c, t.o, tx0, ty0, tx1, ty1, portrait(t.c.key), true);  // (r149: the plain BASE card, whatever else you have)
    if (t.kept) {  // (kept out of it)
      P.box(tx0, ty0, tx1, ty1, 14, 0x05070C, 0.62f);
      P.box(tcx - 62, ty0 + 0.42f * kTileH - 24, tcx + 62, ty0 + 0.42f * kTileH + 24, 12, 0xC0392B);
      P.text("KEEP", tcx, ty0 + 0.42f * kTileH, 34, 110, 0xFFFFFF, 2);
    } else if (t.next > 0) {  // (the next trade takes this many)
      P.box(tx0 - 8, ty0 - 18, tx0 + 58, ty0 + 26, 14, kGold);
      P.text("-" + std::to_string(t.next), tx0 + 25, ty0 + 4, 32, 58, kNavy, 0);
    }
    if (i == b.tradeCursor && !b.tradeOnSets) P.ring(tx0 - 10, ty0 - 10, tx1 + 10, ty1 + 10, 22, 8, kGold);  // (r226: not while on the sets)
    // under it: its spares; the next trade's take and its level after
    // (r149: what's traded said plainly - its BASE copies to spare; a level it'd lose on the tile)
    P.text("X" + std::to_string(t.spare) + " SPARE", tcx, ty1 + 24, 40, kTileW + 16, 0xE8ECF5, 2);
    if (t.next > 0 && !t.kept) {
      Owned after = t.o;
      after.n[kBase][0] -= t.next;
      const int lv0 = card_level(t.o), lv1 = card_level(after);
      if (lv1 < lv0) {
        const std::string lt = "LV " + std::to_string(lv0) + ">" + std::to_string(lv1);  // (r236: 30 px, short: was "LV3 TO LV2" at 24)
        const float lw = P.width(lt, 30) + 20;
        P.box(tcx - lw / 2, ty1 - 100, tcx + lw / 2, ty1 - 62, 19, 0xFF8A3A);
        P.text(lt, tcx, ty1 - 81, 30, lw - 8, 0x1A0A00);
      }
    }
  }
  const int rows = (static_cast<int>(b.tiles.size()) + kTileCols - 1) / kTileCols;
  if (rows > kTileRows)
    P.right("ROWS " + std::to_string(b.tradeTop + 1) + "-" + std::to_string(std::min(rows, b.tradeTop + kTileRows)) + " OF " +
                std::to_string(rows),
            x1 - 50, y0 + 822, 30, 300, 0x8A94AA, 2);  // (r236: 30 px floor)
  // the slot: A trades the next 10 for a pack
  const float sy = y0 + 784;
  const int need = trade_for(b.tradeSet);
  const bool can = b.spareFree >= need;
  const std::string sn = b.tradeSet == 1 ? "JAM '96" : "LANCINI TIZM";
  P.box(cx - 640, sy, cx + 260, sy + 76, 38, can ? (b.tradeSet == 1 ? 0x34185A : 0x24305C) : 0x151C36);
  P.ring(cx - 640, sy, cx + 260, sy + 76, 38, 5, can ? (b.tradeSet == 1 ? 0xFFD400 : kGold) : 0x3A4A70);
  P.box(cx - 612, sy + 34, cx - 512, sy + 42, 4, 0x05070C);  // (its slit)
  P.text(can ? std::to_string(need) + " SPARES = 1 " + sn + " PACK"  // (r236: no "A:" on the page - the bar says it)
             : "NEED " + std::to_string(need - b.spareFree) + " MORE SPARE " + sn + " BASE CARDS",
         cx - 150, sy + 38, 40, 680, can ? 0xFFFFFF : 0x8A94AA, 3);
  {  // (the pack it gives: its set's real wrapper)
    const ImgP pic = shop_pic(b.tradeSet, false);
    if (pic) {
      const float ph = 124, pw = ph * static_cast<float>(pic->w) / pic->h;
      P.image(pic, cx + 338 - pw / 2, sy - 24, cx + 338 + pw / 2, sy - 24 + ph, false);
    } else {
      mini_pack(P, cx + 300, sy - 16, cx + 376, sy + 92);
    }
  }
  // r236: only its last word (a trade done / not yet) - the always-on TRADED SO FAR + "STICK UP: PICK THE SET - X: KEEP A CARD OUT -
  // NEVER A CARD'S LAST COPY" lines are gone (the bar has X KEEP / TRADE IT; a last copy never shows here)
  if (!b.tradeSay.empty()) P.text(b.tradeSay, cx, y0 + 900, 40, x1 - x0 - 60, kGold, 2);
}

// r146: SETS (LT from the trade-ins): the evolution lines (done first, then the nearest; 9 at a time, the stick for more),
// the 18 Pokemon types, the binder's pages; a done one glowing, its bonus
// r157 round 2 (menu overhaul: every word 40 px): SETS as one list the stick scrolls (ROW n OF m) - the evolution lines
// (done first, then the nearest: its first forms, its name, have / total), the 18 Pokemon types two a row, the binder's
// pages as numbered chips (gold: full); each part under its own heading with its rule. (r146-r156: three dense columns,
// 20-28 px labels.)
constexpr int kSetsShown = 10;
int sets_rows(const BinderView& b) {
  return 2 + static_cast<int>(b.lines.size()) + 2 + static_cast<int>((b.types.size() + 1) / 2) + 2 +
         static_cast<int>((b.pageFill.size() + 11) / 12);
}
void sets_page(Pc& P, const BinderView& b) {
  const float x0 = kAreaX0, x1 = kAreaX1, y0 = kAreaY0, cx = 0.5f * (x0 + x1);
  P.box(x0, y0, x1, 1180, 26, 0x1A2140, 1.0f, 0x0A0E1C);
  P.text("SETS", cx, y0 + 56, 64, 900, kGold, 5);
  const float lx0 = x0 + 30, lx1 = x1 - 30;
  const std::string lineBonus = "+" + std::to_string(kLineXp) + "% XP", lineShiny = "SHINY 1 IN " + std::to_string(kLineShinyOdds);
  char mul[32];
  std::snprintf(mul, sizeof(mul), "%d.%02d", 1 + b.coinPct / 100, b.coinPct % 100);
  const int total = sets_rows(b), first = std::max(0, std::min(std::max(0, total - kSetsShown), b.setsTop));
  int row = 0;
  auto slot = [&](float* y) {  // (this row's y, or false: not shown)
    const int k = row++ - first;
    if (k < 0 || k >= kSetsShown) return false;
    *y = y0 + 112 + k * 80.0f;
    return true;
  };
  auto head = [&](const std::string& t, const std::string& right) {
    float y;
    if (!slot(&y)) return;
    P.left(t, lx0, y + 40, 48, lx1 - lx0 - 320, 0xFFFFFF, 3);
    P.right(right, lx1, y + 40, 44, 300, kGold, 2);
  };
  auto rule = [&](const std::string& t) {
    float y;
    if (!slot(&y)) return;
    P.left(t, lx0, y + 38, 40, lx1 - lx0, 0xB8C2D6, 2);
  };
  auto tile = [&](const BinderView::SetRow& r, float tx0, float tx1, float y, bool faces) {
    if (r.done) P.glow(tx0 - 12, y - 8, tx1 + 12, y + 80, faces ? kGold : r.rgb, 0.35f);
    P.box(tx0, y, tx1, y + 70, 18, r.done ? (faces ? 0x2B3A70 : dark(r.rgb, 0.15f)) : 0x121830);
    if (r.done) P.ring(tx0, y, tx1, y + 70, 18, 4, faces ? kGold : 0xFFFFFF);
    float tx = tx0 + 20;
    if (faces) {
      for (size_t m = 0; m < r.who.size() && m < 4; ++m) {
        const float pcx = tx0 + 38 + m * 56.0f, pcy = y + 35;
        P.box(pcx - 26, pcy - 26, pcx + 26, pcy + 26, 26, r.who[m].second ? light(r.rgb, 0.2f) : 0x0A0E1C);
        P.centred(portrait(r.who[m].first), pcx - 24, pcy - 24, pcx + 24, pcy + 24, 0.95f, r.who[m].second ? 0 : 0x232B44);
      }
      tx = tx0 + 26 + std::min<size_t>(4, r.who.size()) * 56.0f;
    } else {
      P.box(tx0 + 10, y + 10, tx0 + 24, y + 60, 7, r.rgb);
      tx = tx0 + 40;
    }
    P.left(r.name, tx, y + 36, 40, tx1 - tx - 140, r.done ? kGold : 0xFFFFFF, 2);
    P.right(std::to_string(r.have) + "/" + std::to_string(r.total), tx1 - 18, y + 36, 40, 120, r.done ? kGold : 0xE8ECF5, 2);
  };
  head("EVOLUTION LINES", std::to_string(b.linesDone) + " / " + std::to_string(b.lines.size()) + " DONE");
  rule("LINE DONE: " + lineBonus);  // (r236: short)
  (void)lineShiny;
  for (const BinderView::SetRow& r : b.lines) {
    float y;
    if (slot(&y)) tile(r, lx0, lx1, y, true);
  }
  head("POKEMON TYPES", std::to_string(b.typesDone) + " / " + std::to_string(b.types.size()) + " DONE");
  rule("TYPE DONE: +" + std::to_string(kTypeXp) + "% XP");  // (r236: short)
  for (size_t i = 0; i < b.types.size(); i += 2) {
    float y;
    if (!slot(&y)) continue;
    const float mid = 0.5f * (lx0 + lx1);
    tile(b.types[i], lx0, mid - 10, y, false);
    if (i + 1 < b.types.size()) tile(b.types[i + 1], mid + 10, lx1, y, false);
  }
  head("BINDER PAGES", std::to_string(b.pagesFull) + " / " + std::to_string(b.pageFill.size()) + " FULL");
  rule("FULL PAGE: +" + std::to_string(kPageCoins) + "% COINS  -  NOW X" + mul);  // (r236: short)
  for (size_t p0 = 0; p0 < b.pageFill.size(); p0 += 12) {
    float y;
    if (!slot(&y)) continue;
    const float cw = (lx1 - lx0 - 11 * 10.0f) / 12;
    for (size_t p = p0; p < b.pageFill.size() && p < p0 + 12; ++p) {
      const float px0 = lx0 + (p - p0) * (cw + 10);
      const bool full = b.pageFill[p] >= 0.999f;
      if (full) P.glow(px0 - 6, y - 6, px0 + cw + 6, y + 76, kGold, 0.4f);
      P.box(px0, y, px0 + cw, y + 70, 12, full ? kGold : 0x121830);
      if (!full && b.pageFill[p] > 0.0f) P.box(px0 + 6, y + 58, px0 + 6 + (cw - 12) * b.pageFill[p], y + 64, 3, 0x6A7A9A);
      P.text(std::to_string(p + 1), px0 + 0.5f * cw, y + 34, 40, cw - 8, full ? kNavy : 0xB8C2D6, 0);
    }
  }
  if (total > kSetsShown) {  // ROW n OF m between up / down arrows
    const float sy = 1150;
    const uint32_t up = first > 0 ? kGold : 0x3A4560, dn = first + kSetsShown < total ? kGold : 0x3A4560;
    P.box(cx - 260, sy - 30, cx + 260, sy + 30, 30, 0x0B1020, 0.8f);
    P.text("ROW " + std::to_string(first + 1) + " OF " + std::to_string(total - kSetsShown + 1), cx, sy + 1, 40, 380, 0xD2DAEB, 2);
    P.poly({cx - 240, sy + 10, cx - 218, sy - 14, cx - 196, sy + 10}, up);
    P.poly({cx + 196, sy - 10, cx + 218, sy + 14, cx + 240, sy - 10}, dn);
  }
}
// r152: TIZM GRADING (LT from SETS): your slabs along the top (at grading - its matches to go -, then back, the best
// first; X puts one on / off your room's display), the cards you can send below (each its finest copy not sealed, its
// cost in TIZM COINS; A, then A again, sends it). The cursor: the slabs, then the cards.
void grading_slab_rect(int k, float* x0, float* y0) {  // (k: its place among the shown)
  *x0 = kAreaX0 + 40 + k * 172.0f;
  *y0 = kAreaY0 + 176;
}
void grading_tile_rect(int k, float* x0, float* y0) {  // (k: its place in the grid shown)
  *x0 = kAreaX0 + 47 + (k % kGCols) * 172.0f;
  *y0 = kAreaY0 + 452 + (k / kGCols) * 252.0f;
}
void grading_page(Pc& P, const BinderView& b) {
  const float x0 = kAreaX0, x1 = kAreaX1, y0 = kAreaY0, cx = 0.5f * (x0 + x1);
  P.box(x0, y0, x1, 1180, 26, 0x1A2140, 1.0f, 0x0A0E1C);
  hexagon(P, cx - 330, y0 + 60, 34, 0xFFFFFF);
  hexagon(P, cx - 330, y0 + 60, 24, 0x8E9BB4);
  P.text("TIZM GRADING", cx + 30, y0 + 62, 70, 900, kGold, 5);
  // (r236: "BACK IN A SLAB AFTER 2 MATCHES, GRADED 1 TO 10" - always on - is the send confirm's line now)
  const int nS = static_cast<int>(b.gSlabs.size()), nT = static_cast<int>(b.gTiles.size());
  int back = 0, at = 0;
  for (const auto& g : b.gSlabs) (g.s.left > 0 ? at : back) += 1;
  P.left("YOUR SLABS  " + std::to_string(back) + (at > 0 ? "  -  " + std::to_string(at) + " AT GRADING" : std::string()), x0 + 40, y0 + 162, 40,
         700, 0xFFFFFF, 2);
  if (nS == 0) {
    P.box(x0 + 40, y0 + 176, x1 - 40, y0 + 388, 20, 0x121830);
    P.text("NO SLABS YET  -  SEND A CARD BELOW", cx, y0 + 282, 40, x1 - x0 - 120, 0x8A94AA, 2);
  }
  for (int k = 0; k < kGSlabCols; ++k) {
    const int i = b.gLeft + k;
    if (i >= nS) break;
    const auto& g = b.gSlabs[static_cast<size_t>(i)];
    float sx, sy;
    grading_slab_rect(k, &sx, &sy);
    const ImgP pic = g.s.ref.shiny ? card_art(g.c, true, 0.0f) : portrait(g.c.key);
    slab_pocket(P, g.c, g.s.ref, g.s.grade, g.s.cert, g.s.left, sx, sy, sx + 150, sy + 212, pic, g.s.show, g.s.left == 0 && !g.s.seen, 0);
    if (i == b.gCursor) P.ring(sx - 9, sy - 9, sx + 159, sy + 221, 18, 7, kGold);
  }
  if (b.gLeft > 0) P.text("<", x0 + 20, y0 + 282, 50, 40, kGold, 2);
  if (b.gLeft + kGSlabCols < nS) P.text(">", x1 - 20, y0 + 282, 50, 40, kGold, 2);
  P.left("SEND ONE", x0 + 40, y0 + 424, 40, 400, 0xFFFFFF, 2);
  P.right(b.gSay.empty() ? xrcoin::fit(b.coins, 13) + " BIT COINS" : b.gSay,  // (r208: commas; past 13 characters short)
          x1 - 40, y0 + 424, 40, x1 - x0 - 360, b.gSay.empty() ? kGold : 0xFFE7A0, 2);
  if (nT == 0) P.text("NO CARDS TO SEND YET", cx, y0 + 640, 40, x1 - x0 - 120, 0x8A94AA, 2);
  for (int k = 0; k < kGCols * kGRows; ++k) {
    const int i = b.gTop * kGCols + k;
    if (i >= nT) break;
    const auto& t = b.gTiles[static_cast<size_t>(i)];
    float tx, ty;
    grading_tile_rect(k, &tx, &ty);
    Owned one;  // (the copy it sends, as it is)
    if (t.r.hit != kHitNone) one.hits.push_back(t.r);
    else one.n[t.r.parallel][t.r.shiny ? 1 : 0] = 1;
    if (t.r.parallel == kGoldTizm && t.r.serial > 0) one.golds.push_back(t.r.serial);
    mini_card(P, t.c, one, tx, ty, tx + 136, ty + 190, t.r.shiny ? card_art(t.c, true, 0.0f) : portrait(t.c.key), false, false);
    const bool armed = b.gArmed == t.r.card;
    const bool cur = nS + i == b.gCursor;
    if (cur) P.ring(tx - 9, ty - 9, tx + 145, ty + 199, 18, 7, armed ? 0x2EA043 : kGold);
    const bool can = b.coins >= t.cost;
    coin_icon(P, tx + 22, ty + 216, 18);  // r157 round 2: its cost at 40 px
    P.left(std::to_string(t.cost), tx + 46, ty + 218, 40, 92, can ? kGold : 0xFF6A5A, 2);
    if (armed && cur) {
      // (r157: the hub's confirm asks; the card says so)
      P.box(tx - 4, ty + 66, tx + 140, ty + 126, 14, 0x2EA043, 0.95f);
      P.text("ASKING", tx + 68, ty + 96, 40, 130, 0xFFFFFF, 2);
    }
  }
  const int rows = (nT + kGCols - 1) / kGCols;
  if (rows > kGRows)
    P.left("ROWS " + std::to_string(b.gTop + 1) + "-" + std::to_string(std::min(rows, b.gTop + kGRows)) + " OF " + std::to_string(rows),
           x0 + 300, kAreaY0 + 424, 40, 400, 0x8A94AA, 2);  // (r157 round 2: beside SEND ONE, 40 px)
}

// ===================================================================================== r160: binder skins + card sleeves
// (Lance: "Binder skins and card sleeves first. for binder skins, i want literal different skins from aligator, to
// leather, to furry, and everything in between imagination wise!!!"; his calls: a REAL 3D binder you pick up - the skin
// is its whole cover - and hits auto top-loaded; BINDERS-SLEEVES-PLAN.md). The catalogue (ids fixed: saved), what you
// own and have on (STRIKERS_HOME/cards/looks.txt, headset-wide like goalfx.txt; bought with the loaded career's TIZM
// COINS), the skins' pictures (STRIKERS_HOME/franchise/binders: tools/outside/binders160, Blender) and the sleeves'
// (franchise/sleeves), and their 2D look on the hub: the BINDER page framed in the skin, each pocket's card in its sleeve
// (a top-loader over a GOLD /10, a BLACK 1/1, a case hit or a shiny), the LOOKS page.
struct LookItem {
  const char* id;
  const char* name;
  int tier;   // 0 FREE .. 5 MYTHIC (xrgoalfx::tier_name / tier_rgb: the same tiers as the explosions)
  int price;  // TIZM coins
  uint32_t rgb1, rgb2;  // its colours (a tile before its picture is in)
};
// (the plan's table: 35 skins - its "first set, 30" counted short)
const LookItem kSkins[] = {
    {"classic_blue", "Classic Blue", 0, 0, 0x2A4A9A, 0x14234E},
    {"brown_leather", "Brown Leather", 1, 150, 0x7A4A2A, 0x3E2414},
    {"black_leather_gold", "Black Leather", 1, 150, 0x1C1C20, 0xC9A227},
    {"denim", "Denim", 1, 150, 0x3A5A8A, 0x1E3050},
    {"suede", "Suede", 1, 150, 0xA0764E, 0x6A4A2E},
    {"cardboard", "Cardboard Box", 1, 150, 0xC09A64, 0x8A6A3E},
    {"old_oak", "Old Oak", 1, 150, 0x8A5E34, 0x4A3018},
    {"alligator", "Alligator", 2, 300, 0x4A5A2A, 0x232A12},
    {"crocodile", "Crocodile", 2, 300, 0x6A6A3A, 0x2E2E18},
    {"snakeskin", "Snakeskin", 2, 300, 0xB8A070, 0x4A3A22},
    {"zebra", "Zebra", 2, 300, 0xF0F0EA, 0x18181A},
    {"leopard", "Leopard", 2, 300, 0xD8A04A, 0x3A2412},
    {"varsity", "Varsity Jacket", 2, 300, 0x9A1E24, 0xF0E8D8},
    {"baseball_glove", "Baseball Glove", 2, 300, 0xB0682E, 0x5A3016},
    {"velvet", "Velvet", 2, 300, 0x7A1438, 0x3A0618},
    {"puffer", "Puffer Jacket", 2, 300, 0xE85A1E, 0x7A2A0A},
    {"pink_fur", "Pink Fur", 3, 600, 0xF27AB8, 0xA03A70},
    {"yeti_fur", "Yeti Fur", 3, 600, 0xEEF2F6, 0x9AB0C4},
    {"plush_teddy", "Plush Teddy", 3, 600, 0xB07A4A, 0x6A4424},
    {"shag_rug", "Shag Rug", 3, 600, 0xD8C49A, 0x8A7450},
    {"carbon_fiber", "Carbon Fiber", 3, 600, 0x2A2C30, 0x0C0D10},
    {"brushed_chrome", "Brushed Chrome", 3, 600, 0xD8DCE2, 0x7A808A},
    {"diamond_plate", "Diamond Plate", 3, 600, 0xA8AEB6, 0x4A5058},
    {"marble", "Marble", 3, 600, 0xF2F0EC, 0x8A8680},
    {"dragon_scales", "Dragon Scales", 4, 1200, 0x2A8A4A, 0x0E3A1C},
    {"shark_skin", "Shark Skin", 4, 1200, 0x6A7A8A, 0x2A3440},
    {"galaxy", "Galaxy", 4, 1200, 0x3A1A7A, 0x080420},
    {"holographic", "Holographic", 4, 1200, 0xC8D8F0, 0x8A6AD8},
    {"neon_grid", "Neon Grid", 4, 1200, 0x18E0FF, 0x10061E},
    {"pixel_8bit", "Pixel 8-Bit", 4, 1200, 0x4AD86A, 0x2A4AD8},
    {"molten_lava", "Molten Lava", 5, 2500, 0xFF6A18, 0x1A0A06},
    {"frozen_ice", "Frozen Ice", 5, 2500, 0xBDEBFF, 0x4A8AC8},
    {"slime", "Slime", 5, 2500, 0x7AE83A, 0x2A7A14},
    {"gold_plated", "Gold-Plated", 5, 2500, 0xFFD54A, 0x8A6A14},
    {"stone_tablet", "Stone Tablet", 5, 2500, 0x9A968C, 0x4A4842},
};
constexpr int kSkinCount = static_cast<int>(sizeof(kSkins) / sizeof(kSkins[0]));
// r163 (Lance: "take the whole sleeves aspect and trash it"): sleeves and top-loaders are gone - skins only
enum { kLookSkin = 0 };

int look_count(int kind) {
  (void)kind;
  return kSkinCount;
}
const LookItem& look_item(int kind, int i) {
  (void)kind;
  return kSkins[std::max(0, std::min(kSkinCount - 1, i))];
}
// owned / on (own lock: the drawing threads read it; never taken with g_mutex held)
std::mutex g_lookM;
bool g_lookRead = false;
std::unordered_set<std::string> g_lookOwned;
int g_lookOn[2] = {0, 0};
std::atomic<int> g_lookGen{1};
// r163: the coins r160 / r162's sleeves cost (looks.txt never said which career paid): given back once to the career
// loaded next (poll), the sleeves dropped from looks.txt then
std::atomic<int> g_sleeveRefund{0};
int old_sleeve_price(const char* id) {
  if (std::strncmp(id, "matte_", 6) == 0) return 100;
  if (std::strcmp(id, "holo") == 0 || std::strcmp(id, "frosted") == 0) return 250;
  if (std::strncmp(id, "art_", 4) == 0) return 500;
  if (std::strcmp(id, "onetouch") == 0) return 1000;
  return 0;
}
std::string looks_path() { return home_dir() + "/cards/looks.txt"; }
int look_find(int kind, const char* id) {
  for (int i = 0; i < look_count(kind); ++i)
    if (std::strcmp(look_item(kind, i).id, id) == 0) return i;
  return -1;
}
bool look_owned_l(int kind, int i) {
  const LookItem& it = look_item(kind, i);
  return it.price == 0 || g_lookOwned.count(it.id) != 0;
}
void looks_read_l() {
  if (g_lookRead) return;
  g_lookRead = true;
  FILE* f = std::fopen(looks_path().c_str(), "r");
  if (f == nullptr) return;
  char line[160];
  int on = -1, refund = 0;
  bool refunded = false;
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    char key[24] = {}, val[48] = {};
    if (std::sscanf(line, "%23s %47s", key, val) != 2) continue;
    if (std::strcmp(key, "owned") == 0) {
      if (look_find(kLookSkin, val) >= 0) g_lookOwned.insert(val);
      else refund += old_sleeve_price(val);  // (r163: an old sleeve's)
    } else if (std::strcmp(key, "skin") == 0) {
      on = look_find(kLookSkin, val);
    } else if (std::strcmp(key, "refund_pending") == 0) {  // (r163: written before a career took it)
      refund += std::max(0, std::atoi(val));
    } else if (std::strcmp(key, "sleeves_refunded") == 0) {
      refunded = true;
    } else if (std::strcmp(key, "ball") == 0 || std::strcmp(key, "ball_owned") == 0) {
      xrballs::load_line(key, val);  // r168: the ball skins' lines (xr_balls.cpp keeps them; written back below)
    }
  }
  std::fclose(f);
  xrballs::loaded();  // r168
  if (on >= 0 && look_owned_l(kLookSkin, on)) g_lookOn[0] = on;
  if (!refunded && refund > 0) g_sleeveRefund = refund;
  CLOG("looks: %d skins owned, %s on%s", static_cast<int>(g_lookOwned.size()), kSkins[g_lookOn[0]].id,
       refund > 0 && !refunded ? " (old sleeves to refund)" : "");
}
void looks_save_l() {  // (written whole: a temp file, then renamed; r163: skins only)
  std::string out = "# r160 Quest 3 Strikers binder skins (xr_cards.cpp; r163: skins only)\nskin " + std::string(kSkins[g_lookOn[0]].id) + "\n";
  out += g_sleeveRefund.load() > 0 ? "refund_pending " + std::to_string(g_sleeveRefund.load()) + "\n" : std::string("sleeves_refunded 1\n");
  for (const std::string& o : g_lookOwned) out += "owned " + o + "\n";
  out += xrballs::save_lines();  // r168: the ball skins (ball <id> / ball_owned <id>)
  ::mkdir((home_dir() + "/cards").c_str(), 0775);
  const std::string path = looks_path(), tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) return;
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size() && std::fflush(f) == 0;
  ::fsync(fileno(f));
  std::fclose(f);
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) CLOG("looks: writing %s failed", path.c_str());
}
bool look_owned(int kind, int i) {
  std::lock_guard<std::mutex> lock(g_lookM);
  looks_read_l();
  return look_owned_l(kind, i);
}
int look_on(int kind) {
  std::lock_guard<std::mutex> lock(g_lookM);
  looks_read_l();
  (void)kind;
  return g_lookOn[0];
}
bool look_equip(int kind, int i) {
  std::lock_guard<std::mutex> lock(g_lookM);
  looks_read_l();
  if (i < 0 || i >= look_count(kind) || !look_owned_l(kind, i)) return false;
  if (g_lookOn[0] != i) {
    g_lookOn[0] = i;
    looks_save_l();
    ++g_lookGen;
    CLOG("looks: %s on", look_item(kind, i).id);
  }
  return true;
}
void look_add_owned(int kind, int i) {
  std::lock_guard<std::mutex> lock(g_lookM);
  looks_read_l();
  g_lookOwned.insert(look_item(kind, i).id);
  looks_save_l();
  ++g_lookGen;
}
// r168: BALL SKINS (the LOOKS page's top row): xr_balls.cpp keeps what you own / have on, looks.txt is written here
constexpr int kBallTiles = 24;  // CLASSIC, FIREBALL, PLANET EARTH + r170's 21 (xrballs::catalogue order)
constexpr int kBallCols = 5;    // r170: in view at once (the row scrolls)
const xrballs::Item& ball_item(int i) {
  const auto& c = xrballs::catalogue();
  return c[static_cast<size_t>(std::max(0, std::min(static_cast<int>(c.size()) - 1, i)))];
}
bool ball_owned(int i) {
  std::lock_guard<std::mutex> lock(g_lookM);
  looks_read_l();
  return xrballs::owned(ball_item(i).id);
}
int ball_on() {
  std::lock_guard<std::mutex> lock(g_lookM);
  looks_read_l();
  return std::max(0, xrballs::find(xrballs::equipped()));
}
bool ball_equip(int i) {
  std::lock_guard<std::mutex> lock(g_lookM);
  looks_read_l();
  if (!xrballs::equip(ball_item(i).id)) return false;
  looks_save_l();
  ++g_lookGen;
  CLOG("looks: ball %s on", ball_item(i).id);
  return true;
}
void ball_add_owned(int i) {
  std::lock_guard<std::mutex> lock(g_lookM);
  looks_read_l();
  xrballs::add_owned(ball_item(i).id);
  looks_save_l();
  ++g_lookGen;
}

bool load_rgba(const std::string& path, std::vector<uint8_t>* px, int* w, int* h);  // (below)
std::vector<uint8_t> crop(const std::vector<uint8_t>& src, int sw, int sh, float u0, float u1, float v0, float v1, int ow, int oh);
ImgP img_of(std::vector<uint8_t> px, int w, int h) {
  auto img = std::make_shared<Img>();
  img->rgba = std::move(px);
  img->w = w;
  img->h = h;
  return img;
}
// a skin's colour picture's part (u0..u1 of its outside strip: back 0-.46, spine .46-.54, front .54-1), ow x oh; its own
// two colours' gradient when its picture isn't on this headset
ImgP skin_part(int i, float u0, float u1, int ow, int oh) {
  std::vector<uint8_t> src;
  int w = 0, h = 0;
  const LookItem& it = look_item(kLookSkin, i);
  if (load_rgba(home_dir() + "/franchise/binders/" + it.id + "_albedo.jpg", &src, &w, &h) && w > 8 && h > 8) {
    std::vector<uint8_t> px = crop(src, w, h, u0, u1, 0.0f, 1.0f, ow, oh);
    // r160 round 2: lit by its own normal map (a light from the top left, its gloss by its roughness) - its scales,
    // stitches and grain raised on the hub as on the 3D binder
    std::vector<uint8_t> nm;
    int nw = 0, nh = 0;
    if (load_rgba(home_dir() + "/franchise/binders/" + it.id + "_normal.jpg", &nm, &nw, &nh) && nw > 8 && nh > 8) {
      const std::vector<uint8_t> n = crop(nm, nw, nh, u0, u1, 0.0f, 1.0f, ow, oh);
      const float L[3] = {-0.42f, 0.55f, 0.72f}, Hh[3] = {-0.22f, 0.29f, 0.93f};
      for (size_t i = 0; i < px.size(); i += 4) {
        const float nx = n[i] / 127.5f - 1.0f, ny = n[i + 1] / 127.5f - 1.0f, nz = std::sqrt(std::max(0.0f, 1.0f - nx * nx - ny * ny));
        const float rough = n[i + 2] / 255.0f;
        const float diff = std::max(0.0f, nx * L[0] + ny * L[1] + nz * L[2]);
        const float sp = std::pow(std::max(0.0f, nx * Hh[0] + ny * Hh[1] + nz * Hh[2]), 6.0f + 120.0f * (1.0f - rough)) * (1.0f - rough) * 150.0f;
        for (int c = 0; c < 3; ++c) px[i + c] = static_cast<uint8_t>(clampf(px[i + c] * (0.42f + 0.78f * diff) + sp, 0.0f, 255.0f));
      }
    }
    return img_of(std::move(px), ow, oh);
  }
  std::vector<uint8_t> px(static_cast<size_t>(ow) * oh * 4);
  for (int y = 0; y < oh; ++y)
    for (int x = 0; x < ow; ++x) {
      const uint32_t c = mix_rgb(it.rgb1, it.rgb2, (static_cast<float>(y) / oh) * 0.8f + 0.2f * std::sin(x * 0.07f + y * 0.05f));
      uint8_t* o = &px[(static_cast<size_t>(y) * ow + x) * 4];
      o[0] = static_cast<uint8_t>(c >> 16);
      o[1] = static_cast<uint8_t>(c >> 8);
      o[2] = static_cast<uint8_t>(c);
      o[3] = 255;
    }
  return img_of(std::move(px), ow, oh);
}
// the LOOKS page's tile picture of a skin (its front cover), read once; any thread
ImgP skin_tile(int i) {
  static std::mutex m;
  static std::unordered_map<int, ImgP> cache;
  {
    std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(i);
    if (it != cache.end()) return it->second;
  }
  ImgP g = skin_part(i, 0.56f, 0.98f, 172, 200);
  std::lock_guard<std::mutex> lock(m);
  return cache.emplace(i, g).first->second;
}
// r168: a ball skin's tile picture (the ball lit, its glow round it: xr_balls.cpp renders it from its own surface), once
ImgP ball_tile(int i) {
  static std::mutex m;
  static std::unordered_map<int, ImgP> cache;
  {
    std::lock_guard<std::mutex> lock(m);
    auto it = cache.find(i);
    if (it != cache.end()) return it->second;
  }
  ImgP g = img_of(xrballs::tile_rgba(i, 200), 200, 200);
  std::lock_guard<std::mutex> lock(m);
  return cache.emplace(i, g).first->second;
}
// the BINDER page's frame in the skin on: the covers' insides (front / back) and the spine (the last one asked kept)
struct SkinFrame {
  int skin = -1;
  ImgP front, back, spine;
};
std::shared_ptr<const SkinFrame> skin_frame(int i) {
  static std::mutex m;
  static std::shared_ptr<const SkinFrame> last;
  {
    std::lock_guard<std::mutex> lock(m);
    if (last && last->skin == i) return last;
  }
  auto f = std::make_shared<SkinFrame>();
  f->skin = i;
  f->front = skin_part(i, 0.54f, 1.0f, 320, 420);
  f->back = skin_part(i, 0.0f, 0.46f, 320, 420);
  f->spine = skin_part(i, 0.46f, 0.54f, 48, 420);
  std::lock_guard<std::mutex> lock(m);
  last = f;
  return last;
}
// up to two lines filling `room` at px (the LOOKS tiles' names)
void two_lines(Pc& P, const std::string& t, float cx, float y, float px, float room, uint32_t rgb) {
  std::vector<std::string> out(1);
  size_t at = 0;
  while (at <= t.size()) {
    size_t sp = t.find(' ', at);
    if (sp == std::string::npos) sp = t.size();
    const std::string word = t.substr(at, sp - at), tryLine = out.back().empty() ? word : out.back() + " " + word;
    if (!out.back().empty() && P.width(tryLine, px) > room && out.size() < 2) out.push_back(word);
    else out.back() = tryLine;
    at = sp + 1;
  }
  const float lh = 1.08f * px, top = y - 0.5f * lh * (out.size() - 1);
  for (size_t k = 0; k < out.size(); ++k) P.text(out[k], cx, top + k * lh, px, room, rgb, 2);
}
// r163: skins only - a grid of 5 x 2 in view, scrolling a row at a time; r168: 5 x 1 in view (the BALL SKINS row above)
constexpr int kLkCols = 5, kLkRows = 1;
// the LOOKS page's state (game thread writes, the drawing thread reads; under g_mutex): r163: the cursor (g_lkCol[0]) and
// the first row in view (g_lkFirst[0]); a buy asking (g_lkArm, -1 none; g_lkArmKind 0), its last word; a skin preview up
int g_lkRow = 0, g_lkFirst[2] = {0, 0}, g_lkCol[2] = {0, 0}, g_lkArmKind = -1, g_lkArm = -1;
std::string g_lkSay;
// r168: the BALL SKINS row: the ring on it (it starts there: the top row) and which ball; a buy asking for a ball is
// g_lkArmKind 1 (g_lkArm the ball)
bool g_lkBallFocus = true;
int g_lkBall = 0;
int g_lkBallFirst = 0;  // r170: the first ball in view
constexpr int kLookBall = 1;
std::atomic<int> g_skinPreview{-1};
std::atomic<bool> g_binderOut{false};  // (the 3D binder out in your room: room_binder's, below)
std::atomic<bool> g_binderToggle{false};  // (the BINDER page's X: out / away, done by room_binder)
std::atomic<bool> g_binderHeld{false};    // (r160 round 2: a hand holds it - the hub's buttons and hint bar are its)
std::atomic<bool> g_binderAway{false};    // (r163: put it away - leaving CARDS, the hub closing; room_binder does it)
std::atomic<int> g_bdOutCard{-1};       // (r163: the card (its number) out of the 3D binder in your hand, -1 none)

}  // namespace

// r160: the LOOKS page (CARDS rail, after EXPLOSIONS): two rows - BINDER SKINS, CARD SLEEVES - 4 tiles in view a row
// (scrolling), each its picture, name, tier and price / OWNED / ON (a green check on the one on); the stick picks, A puts on
// one you own or asks to buy it (the hub's confirm), X previews it (the 3D binder in that skin in front of you / a card of
// yours out in that sleeve). Every word 40 px or more.
void looks_page(Pc& P, const BinderView& b) {
  // r163 (Lance: "take the whole sleeves aspect and trash it"): BINDER SKINS only - 5 a row, 2 rows in view, scrolling
  const float x0 = kAreaX0, x1 = kAreaX1, y0 = kAreaY0, cx = 0.5f * (x0 + x1);
  P.box(x0, y0, x1, 1180, 26, 0x1A2140, 1.0f, 0x0A0E1C);
  const float gx0 = x0 + 30, gx1 = x1 - 30, gap = 18, tw = (gx1 - gx0 - (kLkCols - 1) * gap) / kLkCols, th = 390;
  // r168: BALL SKINS on top - CLASSIC / FIREBALL / PLANET EARTH, each its living ball's picture, name, tier and price /
  // OWNED / ON; beside them what a ball skin is
  {
    P.left("BALL SKINS", x0 + 36, y0 + 50, 64, 520, kGold, 5);
    P.left("YOUR MATCH BALL", x0 + 420, y0 + 54, 40, 400, 0xB8C2D6, 2);
    {  // r170: where you are along the row: BALLS 1-5 OF 24, its arrows
      const std::string where = "BALLS " + std::to_string(b.lkBallFirst + 1) + "-" + std::to_string(std::min(kBallTiles, b.lkBallFirst + kBallCols)) +
                                " OF " + std::to_string(kBallTiles);
      const float wx = x1 - 40, ay = y0 + 54;
      P.right(where, wx - 44, ay, 40, 380, 0xB8C2D6, 2);
      const float aw = P.width(where, 40);
      P.poly({wx - 44 - aw - 20, ay - 14, wx - 44 - aw - 48, ay, wx - 44 - aw - 20, ay + 14}, b.lkBallFirst > 0 ? kGold : 0x3A4560);  // (left)
      P.poly({wx - 30, ay - 14, wx - 2, ay, wx - 30, ay + 14}, b.lkBallFirst + kBallCols < kBallTiles ? kGold : 0x3A4560);     // (right)
    }
    const float bth = 350, by0 = y0 + 90;
    for (int i = b.lkBallFirst; i < std::min(kBallTiles, b.lkBallFirst + kBallCols); ++i) {
      const xrballs::Item& it = ball_item(i);
      const float tx0 = gx0 + (i - b.lkBallFirst) * (tw + gap), tx1 = tx0 + tw, tcx = 0.5f * (tx0 + tx1);
      const bool on = b.lkBallFocus && b.lkBall == i;
      const bool mine = i < static_cast<int>(b.ballOwned.size()) && b.ballOwned[static_cast<size_t>(i)];
      const bool equipped = b.ballOn == i;
      P.box(tx0, by0, tx1, by0 + bth, 22, on ? 0x24305C : 0x121830);
      P.box(tx0, by0, tx1, by0 + 10, 5, xrgoalfx::tier_rgb(it.tier));  // (its tier's colour along its top)
      P.box(tcx - 78, by0 + 20, tcx + 78, by0 + 176, 78, 0x05070C, 0.85f);  // (a dark well behind the ball)
      P.figure(ball_tile(i), tcx - 80, by0 + 18, tcx + 80, by0 + 178, true, 0.5f, 0.5f);
      two_lines(P, it.name, tcx, by0 + 218, 40, tw - 24, 0xFFFFFF);
      P.text(xrgoalfx::tier_name(it.tier), tcx, by0 + 280, 40, tw - 24, xrgoalfx::tier_rgb(it.tier), 2);
      const std::string state = equipped ? "ON" : (mine ? "OWNED" : std::to_string(it.price) + " COINS");
      P.text(state, tcx, by0 + 322, 40, tw - 24, equipped ? 0x6ED36E : (mine ? 0xE8ECF5 : (b.coins >= it.price ? kGold : 0xFF8A7A)), 2);
      if (equipped) {  // a green check: the one on
        const float ccx = tx1 - 34, ccy = by0 + 38;
        P.box(ccx - 23, ccy - 23, ccx + 23, ccy + 23, 23, 0x2EA043);
        P.poly({ccx - 13, ccy, ccx - 6, ccy - 6, ccx + 1, ccy + 2, ccx + 1, ccy + 12}, 0xFFFFFF);
        P.poly({ccx + 1, ccy + 2, ccx + 12, ccy - 14, ccx + 18, ccy - 8, ccx + 1, ccy + 12}, 0xFFFFFF);
      }
      if (on) P.ring(tx0 - 9, by0 - 9, tx1 + 9, by0 + bth + 9, 30, 8, kGold);
    }
  }
  const int n = kSkinCount, rows = (n + kLkCols - 1) / kLkCols, top = b.lkFirst[0];
  const float hy = y0 + 470;  // (r168: the binder skins under the balls, one row in view)
  int owned = 0;
  for (int i = 0; i < n; ++i) owned += i < static_cast<int>(b.lkOwned[0].size()) && b.lkOwned[0][static_cast<size_t>(i)] ? 1 : 0;
  P.left("BINDER SKINS", x0 + 36, hy, 64, 620, kGold, 5);
  P.left(std::to_string(owned) + " OF " + std::to_string(n) + " YOURS", x0 + 600, hy + 4, 40, 330, 0xB8C2D6, 2);
  {  // where you are: ROW 2 OF 7, its arrows
    const std::string where = "ROW " + std::to_string(top + 1) + " OF " + std::to_string(rows);
    const float wx = x1 - 40;
    P.right(where, wx - 40, hy + 4, 40, 330, 0xB8C2D6, 2);
    const float aw = P.width(where, 40);
    P.poly({wx - 40 - aw - 56, hy + 16, wx - 40 - aw - 36, hy - 12, wx - 40 - aw - 16, hy + 16}, top > 0 ? kGold : 0x3A4560);  // (up)
    P.poly({wx - 30, hy - 8, wx - 10, hy + 20, wx + 10, hy - 8}, top + kLkRows < rows ? kGold : 0x3A4560);             // (down)
  }
  const float gy0 = hy + 44;
  for (int r = 0; r < kLkRows; ++r)
    for (int k = 0; k < kLkCols; ++k) {
      const int i = (top + r) * kLkCols + k;
      if (i >= n) break;
      const LookItem& it = look_item(kLookSkin, i);
      const float tx0 = gx0 + k * (tw + gap), tx1 = tx0 + tw, tcx = 0.5f * (tx0 + tx1), ty0 = gy0 + r * (th + 16);
      const bool on = !b.lkBallFocus && b.lkCol[0] == i;
      const bool mine = i < static_cast<int>(b.lkOwned[0].size()) && b.lkOwned[0][static_cast<size_t>(i)];
      const bool equipped = b.skin == i;
      P.box(tx0, ty0, tx1, ty0 + th, 22, on ? 0x24305C : 0x121830);
      P.box(tx0, ty0, tx1, ty0 + 10, 5, xrgoalfx::tier_rgb(it.tier));  // (its tier's colour along its top)
      const float px0 = tx0 + 16, px1 = tx1 - 16, py0 = ty0 + 24, py1 = ty0 + 206;
      P.box(px0 - 3, py0 - 3, px1 + 3, py1 + 3, 12, 0x05070C);
      P.figure(skin_tile(i), px0, py0, px1, py1, true, 0.5f, 0.5f);
      P.ring(px0, py0, px1, py1, 10, 3, 0x05070C, 0.9f);
      P.poly({px0 + 4, py0 + 50, px1 - 4, py0 + 4, px1 - 4, py0 + 36, px0 + 4, py0 + 82}, 0xFFFFFF, 0.08f);  // (a cover's sheen)
      two_lines(P, upper(it.name), tcx, ty0 + 254, 40, tw - 24, 0xFFFFFF);
      P.text(xrgoalfx::tier_name(it.tier), tcx, ty0 + 320, 40, tw - 24, xrgoalfx::tier_rgb(it.tier), 2);
      const std::string state = equipped ? "ON" : (mine ? "OWNED" : std::to_string(it.price) + " COINS");
      P.text(state, tcx, ty0 + 362, 40, tw - 24, equipped ? 0x6ED36E : (mine ? 0xE8ECF5 : (b.coins >= it.price ? kGold : 0xFF8A7A)), 2);
      if (equipped) {  // a green check: the one on
        const float ccx = tx1 - 34, ccy = ty0 + 38;
        P.box(ccx - 23, ccy - 23, ccx + 23, ccy + 23, 23, 0x2EA043);
        P.poly({ccx - 13, ccy, ccx - 6, ccy - 6, ccx + 1, ccy + 2, ccx + 1, ccy + 12}, 0xFFFFFF);
        P.poly({ccx + 1, ccy + 2, ccx + 12, ccy - 14, ccx + 18, ccy - 8, ccx + 1, ccy + 12}, 0xFFFFFF);
      }
      if (on) P.ring(tx0 - 9, ty0 - 9, tx1 + 9, ty0 + th + 9, 30, 8, kGold);
    }
  const std::string say = !b.lkSay.empty() ? b.lkSay : ((b.lkPreview || b.ballPreview) ? "PREVIEWING IN FRONT OF YOU  -  B STOPS IT" : "");
  if (!say.empty()) P.text(say, cx, 1150, 40, x1 - x0 - 60, kGold, 3);
}


namespace {
// r160: the binder page's frame in the skin on: its covers' insides round the pages, the spine between them
void binder_frame(Pc& P, int skin) {
  // (round 2: a 46 px cover round the pages, a 60 px spine between them - the skin lit by its own normal map)
  const float fx0 = kAreaX0, fx1 = kRightPageX + kPageW + kFrameM, fy0 = kAreaY0, fy1 = kBinderY + kPageH + kFrameV;
  const float sx0 = kLeftPageX + kPageW, sx1 = kRightPageX;
  const std::shared_ptr<const SkinFrame> f = skin_frame(skin);
  P.box(fx0 - 6, fy0 - 4, fx1 + 8, fy1 + 10, 32, 0x000000, 0.6f);  // (its shadow)
  P.figure(f->front, fx0, fy0, sx0, fy1, true, 0.5f, 0.5f);
  P.figure(f->back, sx1, fy0, fx1, fy1, true, 0.5f, 0.5f);
  P.figure(f->spine, sx0, fy0, sx1, fy1, true, 0.5f, 0.5f);
  P.box(sx0, fy0, sx0 + 8, fy1, 0, 0x000000, 0.4f);  // (the spine's folds, a catch of light beside each)
  P.box(sx0 + 8, fy0, sx0 + 11, fy1, 0, 0xFFFFFF, 0.12f);
  P.box(sx1 - 8, fy0, sx1, fy1, 0, 0x000000, 0.4f);
  P.box(sx1 - 11, fy0, sx1 - 8, fy1, 0, 0xFFFFFF, 0.12f);
  for (int side = 0; side < 2; ++side) {  // (the pages sit in the covers: a soft shadow round each)
    const float px0 = side == 0 ? kLeftPageX : kRightPageX;
    P.box(px0 - 8, kBinderY - 5, px0 + kPageW + 8, kBinderY + kPageH + 12, 24, 0x000000, 0.45f);
  }
  P.ring(fx0, fy0, fx1, fy1, 28, 6, 0x05070C, 0.9f);  // (its rounded corners, its edge)
  P.ring(fx0 + 5, fy0 + 5, fx1 - 5, fy1 - 5, 24, 2, 0xFFFFFF, 0.14f);
  P.poly({fx0 + 8, fy0 + 140, fx1 - 8, fy0 + 10, fx1 - 8, fy0 + 70, fx0 + 8, fy0 + 200}, 0xFFFFFF, 0.05f);
}
}  // namespace

// r157 (menu overhaul; r153 left the shop's EXPLOSIONS page undrawn): GOAL EXPLOSIONS on the CARDS rail - the catalogue
// 4 a row (its two colours, its tier, its price / OWNED / ON), then NO EXPLOSION; the stick picks, A equips one you own
// or asks to buy it (the hub's confirm), X previews it in your room.
// r161 (pack 2: 20 explosions + NO EXPLOSION = 6 rows): 4 rows in view, scrolling with the cursor ("ROWS 1-4 OF 6" and
// its arrows, as the LOOKS page's rows); a ROOM TAKEOVER's tile has a gold corner; the say line 40 px under the grid
void explosions_page(Pc& P, const BinderView& b) {
  const float x0 = kAreaX0, x1 = kAreaX1, y0 = kAreaY0, cx = 0.5f * (x0 + x1);
  P.box(x0, y0, x1, 1180, 26, 0x1A2140, 1.0f, 0x0A0E1C);  // (r157 round 2: down to the page area's foot)
  P.text("GOAL EXPLOSIONS", cx, y0 + 56, 66, 900, kGold, 5);
  const int n = static_cast<int>(b.fx.size()) + 1;  // (+ NO EXPLOSION)
  const int rows = (n + kFxCols - 1) / kFxCols;
  const float gx0 = x0 + 30, gx1 = x1 - 30, gap = 14, tw = (gx1 - gx0 - (kFxCols - 1) * gap) / kFxCols, th = 168, gy0 = y0 + 158;
  {  // what it is, and where you are in the rows: ROWS 1-4 OF 6, an arrow lit each way there are more
    const float ly = y0 + 116;
    P.left("GOES OFF WHEN YOUR SIDE SCORES", gx0, ly, 40, 760, 0xB8C2D6, 2);
    const std::string where = "ROWS " + std::to_string(b.fxTop + 1) + "-" + std::to_string(std::min(rows, b.fxTop + kFxRowsShown)) +
                              " OF " + std::to_string(rows);
    const float ax = gx1 - 16;
    P.right(where, ax - 64, ly + 2, 40, 340, 0xB8C2D6, 2);
    P.poly({ax - 44, ly - 2, ax - 26, ly - 24, ax - 8, ly - 2}, b.fxTop > 0 ? kGold : 0x3A4560);                    // up
    P.poly({ax - 44, ly + 6, ax - 8, ly + 6, ax - 26, ly + 28}, b.fxTop + kFxRowsShown < rows ? kGold : 0x3A4560);  // down
  }
  for (int i = b.fxTop * kFxCols; i < n && i < (b.fxTop + kFxRowsShown) * kFxCols; ++i) {
    const int row = i / kFxCols - b.fxTop;
    const float tx0 = gx0 + (i % kFxCols) * (tw + gap), ty0 = gy0 + row * (th + gap), tx1 = tx0 + tw, ty1 = ty0 + th;
    const bool on = i == b.fxCursor;
    if (i == n - 1) {  // NO EXPLOSION
      P.box(tx0, ty0, tx1, ty1, 22, on ? 0x24305C : 0x121830);
      P.text("NO EXPLOSION", 0.5f * (tx0 + tx1), ty0 + 58, 40, tw - 24, 0xE8ECF5, 3);
      P.text(b.fxNone ? "ON" : "OFF", 0.5f * (tx0 + tx1), ty0 + 118, 40, tw - 24, b.fxNone ? 0x6ED36E : 0x8A94AA, 2);
    } else {
      const BinderView::FxTile& t = b.fx[static_cast<size_t>(i)];
      P.box(tx0, ty0, tx1, ty1, 22, t.rgb1, 1.0f, t.rgb2);
      P.box(tx0, ty0 + 0.34f * th, tx1, ty1, 22, 0x05070C, 0.62f);  // (its words on a dark foot)
      if (t.secs > 5.0f) P.poly({tx1 - 70, ty0, tx1 - 16, ty0, tx1, ty0 + 16, tx1, ty0 + 70}, kGold);  // (a ROOM TAKEOVER: its gold corner)
      P.text(upper(t.name), 0.5f * (tx0 + tx1), ty0 + 31, 40, tw - 24, 0xFFFFFF, 3);
      const std::string state = t.equipped ? "ON" : (t.owned ? "OWNED" : std::to_string(t.price) + " COINS");
      P.text(xrgoalfx::tier_name(t.tier), 0.5f * (tx0 + tx1), ty0 + 90, 40, tw - 24, xrgoalfx::tier_rgb(t.tier), 2);
      P.text(state, 0.5f * (tx0 + tx1), ty0 + 134, 40, tw - 24, t.equipped ? 0x6ED36E : (t.owned ? 0xE8ECF5 : (b.coins >= t.price ? kGold : 0xFF8A7A)), 2);
      if (t.equipped) {  // a green check: the one that goes off
        const float cy = ty1 - 32;  // (in its foot's corner, clear of the name)
        P.box(tx1 - 58, cy - 23, tx1 - 12, cy + 23, 23, 0x2EA043);
        P.poly({tx1 - 47, cy, tx1 - 40, cy - 6, tx1 - 33, cy + 2, tx1 - 33, cy + 12}, 0xFFFFFF);
        P.poly({tx1 - 33, cy + 2, tx1 - 22, cy - 14, tx1 - 16, cy - 8, tx1 - 33, cy + 12}, 0xFFFFFF);
      }
    }
    if (on) P.ring(tx0 - 9, ty0 - 9, tx1 + 9, ty1 + 9, 30, 8, kGold);
  }
  const std::string say = !b.fxSay.empty() ? b.fxSay : (b.fxPreviewing ? "PREVIEWING IN YOUR ROOM  -  B STOPS IT" : "");
  if (!say.empty()) P.text(say, cx, 1140, 40, x1 - x0 - 60, kGold, 3);
}

// ---- r236 (LOOK B "BROADCAST PREMIUM", Lance 11-12; logs/r235-menus/optB_cards.png, optB_locker.png): CARDS > PACKS and the
// four LOCKER pages, drawn with the hub's broadcast kit (xr_menu.h: the hub's BroadcastScope is on while hub_page runs)
// CARDS > PACKS: the hero (your packs + ONE action: OPEN) over BUY PACKS - three products, name + price only (the box / THE VAULT on
// the focused one's X)
void packs_page_b(Pc& P, const BinderView& b) {
  xrmenu::Kit K(P.v);
  const float px0 = 470, px1 = 2120, y0 = 232;
  const bool have = b.packs > 0, onOpen = have && b.packsOnOpen;
  xrmenu::panel(K, px0, y0, px1, y0 + 470, xrmenu::kBCards, 0.94f);
  P.glow(px0 + 60, y0 + 30, px0 + 640, y0 + 450, xrmenu::kBCards, 0.35f);
  const ImgP pack = shop_pic(0, false);
  for (int k = 0; k < 3; ++k) {
    if (!have && k < 2) continue;
    const float a = have && k < 3 - std::min(3, b.packs) ? 0.0f : (have ? 1.0f : 0.35f);
    if (a <= 0.0f) continue;
    if (pack) P.image(pack, px0 + 90 + k * 110, y0 + 60 + (2 - k) * 4, px0 + 330 + k * 110, y0 + 420 - (2 - k) * 4, false, a);
    else mini_pack(P, px0 + 90 + k * 110, y0 + 60, px0 + 330 + k * 110, y0 + 420);
  }
  if (have) {
    xrmenu::say(K, std::to_string(b.packs) + (b.packs == 1 ? " PACK" : " PACKS"), px0 + 720, y0 + 150, 110, 0xFFFFFF, 'l', px1 - px0 - 760, true, 0.0f, 3.0f);
    xrmenu::label(K, "READY TO OPEN", px0 + 724, y0 + 240, 38);
    xrmenu::button(K, px0 + 720, y0 + 300, px0 + 1180, y0 + 410, "OPEN", "A", true, onOpen);
  } else {
    xrmenu::say(K, "NO PACKS", px0 + 720, y0 + 150, 110, xrmenu::kBDim, 'l', px1 - px0 - 760);
    xrmenu::label(K, "ONE EVERY MATCH  -  + A JAM '96 FOR A WIN", px0 + 724, y0 + 240, 38, xrmenu::kBSoft, 'l', px1 - px0 - 780);
  }
  xrmenu::label(K, "BUY PACKS", px0 + 10, y0 + 540, 40);
  if (!b.say.empty()) xrmenu::label(K, b.say, px1 - 10, y0 + 540, 34, xrmenu::kBGold, 'r', 1150);
  else if (b.lastCoins > 0) xrmenu::label(K, "LAST MATCH +" + std::to_string(b.lastCoins) + " BIT COINS", px1 - 10, y0 + 540, 34, xrmenu::kBGold, 'r', 900);
  const float tw = (px1 - px0 - 60) / 3.0f, ty0 = y0 + 590, ty1 = 1170;
  static const uint32_t kCol[3] = {0x3B6FD0, 0xE0408A, 0xC9A227};
  for (int set = 0; set < 3; ++set) {
    const float x0 = px0 + set * (tw + 30);
    const bool focus = !onOpen && b.pick / 2 == set;
    const bool box = focus && b.pick % 2 == 1;
    const long long price = set == 2 ? (box ? xrexq::kBoxPrice : xrexq::kPackPrice) : set == 0 ? (box ? kBoxPrice : kPackPrice) : (box ? kJamBoxPrice : kJamPackPrice);
    const bool can = b.coins >= price;
    xrmenu::TileSpec ts;
    ts.colour = kCol[set];
    ts.on = focus;
    xrmenu::tile(K, x0, ty0, x0 + tw, ty1, ts);
    const ImgP pic = shop_pic(set, box);
    if (pic) P.image(pic, x0 + 30, ty0 + 30, x0 + 230, ty1 - 30, false);
    else mini_pack(P, x0 + 40, ty0 + 40, x0 + 220, ty1 - 40);
    const char* name = set == 0 ? (box ? "BOX OF 5" : "TIZM PACK") : set == 1 ? (box ? "JAM BOX OF 5" : "JAM '96 PACK") : (box ? "THE VAULT" : "EXQUISITE");
    xrmenu::say(K, name, x0 + 250, ty0 + 150, 48, 0xFFFFFF, 'l', tw - 270);
    xrmenu::bcoin(K, x0 + 272, ty0 + 240, 44);
    xrmenu::say(K, xrcoin::text(price), x0 + 306, ty0 + 240, 52, can ? xrmenu::kBGold : 0xFF6A5A, 'l', tw - 330);
    if (focus)  // (the focused one's details: what's in it, or what it still needs)
      xrmenu::label(K, !can ? "NEED " + xrcoin::text(price - b.coins) + " MORE"
                            : set == 2 ? (box ? "5 PACKS IN A CASE" : "3 CARDS  -  1 HIT") : (box ? "5 PACKS" : "5 CARDS"),
                    x0 + 252, ty0 + 316, 30, can ? xrmenu::kBSoft : 0xFF8A7A, 'l', tw - 270);
  }
}

// LOCKER (BALLS, GOAL FX, TAKEOVERS, BINDERS): six big tiles (3 x 2, scrolling a row at a time) - name + price / EQUIPPED - and the
// focused one's panel (its big picture, tier, price, ONE button)
void locker_page_b(Pc& P, const BinderView& b) {
  xrmenu::Kit K(P.v);
  struct It {
    std::string name, kind;
    int tier = 0;
    long long price = 0;
    bool owned = false, on = false, none = false, takeover = false;
    ImgP pic;
    bool ball = false;
    uint32_t c1 = 0, c2 = 0;
  };
  std::vector<It> items;
  int cursor = 0;
  const bool balls = b.page == kPageLooks, covers = b.page == kPageCovers;
  if (balls) {
    for (int i = 0; i < kBallTiles; ++i) {
      const xrballs::Item& it = ball_item(i);
      It x;
      x.name = it.name;
      x.kind = "YOUR MATCH BALL";
      x.tier = it.tier;
      x.price = it.price;
      x.owned = i < static_cast<int>(b.ballOwned.size()) && b.ballOwned[static_cast<size_t>(i)];
      x.on = b.ballOn == i;
      x.pic = ball_tile(i);
      x.ball = true;
      items.push_back(x);
    }
    cursor = b.lkBall;
  } else if (covers) {
    for (int i = 0; i < kSkinCount; ++i) {
      const LookItem& it = look_item(kLookSkin, i);
      It x;
      x.name = upper(it.name);
      x.kind = "YOUR BINDER'S COVER";
      x.tier = it.tier;
      x.price = it.price;
      x.owned = i < static_cast<int>(b.lkOwned[0].size()) && b.lkOwned[0][static_cast<size_t>(i)];
      x.on = b.skin == i;
      x.pic = skin_tile(i);
      items.push_back(x);
    }
    cursor = b.lkCol[0];
  } else {
    for (int idx : b.fxList) {
      It x;
      if (idx < 0 || idx >= static_cast<int>(b.fx.size())) {
        x.name = "NO EXPLOSION";
        x.kind = "THE GOAL'S OWN SHOW ONLY";
        x.owned = true;
        x.on = b.fxNone;
        x.none = true;
        x.c1 = 0x2A3550;
        x.c2 = 0x111A2E;
      } else {
        const BinderView::FxTile& t = b.fx[static_cast<size_t>(idx)];
        x.name = upper(t.name);
        x.takeover = t.secs > 5.0f;
        x.kind = x.takeover ? "TAKES OVER YOUR ROOM" : "GOES OFF WHEN YOU SCORE";
        x.tier = t.tier;
        x.price = t.price;
        x.owned = t.owned;
        x.on = t.equipped;
        x.c1 = t.rgb1;
        x.c2 = t.rgb2;
      }
      items.push_back(x);
    }
    cursor = b.fxPos;
  }
  const int n = static_cast<int>(items.size());
  cursor = std::max(0, std::min(std::max(0, n - 1), cursor));
  const float px0 = 470, gx1 = 1440, y0 = 232, y1 = 1170, gap = 26;
  const int rows = (n + kLkGridCols - 1) / kLkGridCols, crow = cursor / kLkGridCols;
  const int top = std::max(0, std::min(std::max(0, rows - kLkGridRows), crow - (crow > 0 ? 1 : 0)));
  const float tw = (gx1 - px0 - gap * (kLkGridCols - 1)) / kLkGridCols, th = (y1 - y0 - gap * (kLkGridRows - 1)) / kLkGridRows;
  // a swatch for an explosion / takeover / NO EXPLOSION: its two colours as a lit disc, a burst for the explosions
  auto swatch = [&](const It& it, float cx, float cy, float r) {
    P.glow(cx - 1.4f * r, cy - 1.4f * r, cx + 1.4f * r, cy + 1.4f * r, it.c1, 0.35f);
    P.box(cx - r, cy - r, cx + r, cy + r, r, it.c1, 1.0f, it.c2);
    if (!it.none) {
      xrtext::BoardPiece bp;
      bp.kind = xrtext::BoardPiece::kBurst;
      bp.x0 = cx - 0.8f * r;
      bp.y0 = cy - 0.8f * r;
      bp.x1 = cx + 0.8f * r;
      bp.y1 = cy + 0.8f * r;
      bp.points = it.takeover ? 8 : 12;
      bp.inner = it.takeover ? 0.55f : 0.45f;
      bp.rgb = 0xFFFFFF;
      bp.alpha = 0.55f;
      P.v.push_back(bp);
    } else {
      P.box(cx - 0.55f * r, cy - 0.07f * r, cx + 0.55f * r, cy + 0.07f * r, 0.07f * r, 0xFFFFFF, 0.5f);
    }
  };
  for (int k = 0; k < kLkGridCols * kLkGridRows; ++k) {
    const int i = top * kLkGridCols + k;
    if (i >= n) break;
    const It& it = items[static_cast<size_t>(i)];
    const int c = k % kLkGridCols, r = k / kLkGridCols;
    const float x0 = px0 + c * (tw + gap), yy = y0 + r * (th + gap), cx = x0 + 0.5f * tw;
    xrmenu::TileSpec ts;
    ts.colour = it.c1 != 0 && i == cursor ? it.c1 : 0x2F8CFF;  // (the package's blue; an explosion focused: its own colour)
    ts.on = i == cursor;
    xrmenu::tile(K, x0, yy, x0 + tw, yy + th, ts);
    if (it.takeover) P.poly({x0 + tw - 70, yy, x0 + tw, yy, x0 + tw, yy + 70}, xrmenu::kBGold);  // (a ROOM TAKEOVER: its gold corner)
    if (it.pic) {
      if (it.ball) P.box(cx - 110, yy + 50, cx + 110, yy + 270, 110, 0x05070C, 0.7f);
      P.figure(it.pic, cx - 120, yy + 40, cx + 120, yy + 280, true, 0.5f, 0.5f);
    } else {
      swatch(it, cx, yy + 160, 110);
    }
    xrmenu::say(K, it.name, cx, yy + th - 110, 42, 0xFFFFFF, 'c', tw - 30);
    if (it.on) {
      xrmenu::label(K, "EQUIPPED", cx, yy + th - 50, 32, 0x3DDC84, 'c', tw - 30);
    } else if (it.owned) {
      xrmenu::label(K, "OWNED", cx, yy + th - 50, 32, xrmenu::kBSoft, 'c', tw - 30);
    } else {
      const std::string pr = xrcoin::text(it.price);
      const float ww = xrtext::face_text_width(pr, 40, xrtext::kFaceBroadcast) + 50;
      xrmenu::bcoin(K, cx - ww / 2 + 18, yy + th - 50, 36);
      xrmenu::say(K, pr, cx - ww / 2 + 46, yy + th - 50, 40, b.coins >= it.price ? xrmenu::kBGold : 0xFF6A5A, 'l');
    }
  }
  if (rows > kLkGridRows) {  // (more rows: the arrows down the grid's right edge, lit where there are more)
    const float ax = gx1 + 20;
    P.poly({ax - 12, y0 + 40, ax, y0 + 16, ax + 12, y0 + 40}, top > 0 ? xrmenu::kBGold : 0x3A4560);
    P.poly({ax - 12, y1 - 40, ax + 12, y1 - 40, ax, y1 - 16}, top + kLkGridRows < rows ? xrmenu::kBGold : 0x3A4560);
  }
  // the focused one
  if (n == 0) return;
  const It& f = items[static_cast<size_t>(cursor)];
  const float fx0 = 1480, fx1 = 2120, cxp = 0.5f * (fx0 + fx1), py = y1 - 235;
  const uint32_t tint = f.c1 != 0 ? f.c1 : xrgoalfx::tier_rgb(f.tier);
  xrmenu::panel(K, fx0, y0, fx1, y1, tint, 0.94f);
  const float bs = std::min(400.0f, py - 300 - (y0 + 60));
  P.glow(cxp - 260, y0 + 30, cxp + 260, y0 + 60 + bs + 50, tint, 0.4f);
  if (f.pic) P.figure(f.pic, cxp - bs / 2, y0 + 60, cxp + bs / 2, y0 + 60 + bs, true, 0.5f, 0.5f);
  else swatch(f, cxp, y0 + 60 + bs / 2, bs * 0.4f);
  xrmenu::say(K, f.name, cxp, py - 175, 64, 0xFFFFFF, 'c', fx1 - fx0 - 60, true, 0.0f, 3.0f);
  xrmenu::label(K, f.none ? f.kind : std::string(xrgoalfx::tier_name(f.tier)) + "  -  " + f.kind, cxp, py - 105, 32, xrmenu::kBSoft, 'c', fx1 - fx0 - 60);
  if (f.on || f.owned || f.price <= 0) {
    xrmenu::say(K, f.on ? "EQUIPPED" : "OWNED", cxp, py, 56, f.on ? 0x3DDC84 : xrmenu::kBSoft, 'c');
  } else {
    const std::string pr = xrcoin::text(f.price);
    const float ww = xrtext::face_text_width(pr, 64, xrtext::kFaceBroadcast) + 70;
    xrmenu::bcoin(K, cxp - ww / 2 + 26, py, 52);
    xrmenu::say(K, pr, cxp - ww / 2 + 66, py, 64, b.coins >= f.price ? xrmenu::kBGold : 0xFF6A5A, 'l');
  }
  const std::string say = !(balls || covers) ? (!b.fxSay.empty() ? b.fxSay : (b.fxPreviewing ? "PREVIEWING  -  B STOPS IT" : ""))
                                             : (!b.lkSay.empty() ? b.lkSay : ((b.lkPreview || b.ballPreview) ? "PREVIEWING  -  B STOPS IT" : ""));
  if (!say.empty()) xrmenu::label(K, say, cxp, y1 - 172, 30, xrmenu::kBGold, 'c', fx1 - fx0 - 40);
  xrmenu::button(K, fx0 + 40, y1 - 140, fx1 - 40, y1 - 40, f.on ? "EQUIPPED" : (f.owned ? "PUT IT ON" : "BUY"), "A", true, true, !f.on);
}

// r201: the MARKET and MUSEUM pages - their modules draw them into the page area (470, 222 .. 2120, 1180 = their 1650 x 958),
// fitted as the rest; their hints and their confirm. Never with g_mutex held (they call into the binder).
HubPage market_museum_page(int page) {
  WdScope wd("drawing thread", page == kPageMarket ? "MARKET picture" : (page == kPageCollectors ? "COLLECTORS picture" : "MUSEUM picture"));  // r237
  HubPage out;
  auto keep = std::make_shared<std::vector<std::shared_ptr<void>>>();
  const float ox = g_fitOx.load(), oy = g_fitOy.load(), k = g_fitK.load();
  const float t = static_cast<float>(std::fmod(std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(), 3600.0));
  std::string aWord, xWord, yWord, q, line;
  bool aDim = false;
  if (page == kPageMarket) {
    xrmarket::draw(out.pieces, *keep, ox + 470.0f * k, oy + 222.0f * k, k, t);
    xrmarket::hints(&aWord, &aDim, &xWord, &yWord);
    if (xrmarket::asking(&q, &line)) out.confirm = q, out.confirmLine = line;
  } else if (page == kPageCollectors) {  // r213: COLLECTOR TRADES (the same page area)
    xrtrade::draw(out.pieces, *keep, ox + 470.0f * k, oy + 222.0f * k, k, t);
    xrtrade::hints(&aWord, &aDim, &xWord, &yWord);
    if (xrtrade::asking(&q, &line)) out.confirm = q, out.confirmLine = line;
  } else {
    xrmuseum::draw(out.pieces, *keep, ox + 470.0f * k, oy + 222.0f * k, k, t);
    xrmuseum::hints(&aWord, &aDim, &xWord, &yWord);
    if (xrmuseum::asking(&q, &line)) out.confirm = q, out.confirmLine = line;
  }
  if (!aWord.empty()) out.hints.push_back({"A", aWord, 0x2EA043});
  if (!xWord.empty()) out.hints.push_back({"X", xWord, 0x2E6FD0});
  if (!yWord.empty()) out.hints.push_back({"Y", yWord, 0xC9A227});
  if (aWord.empty()) out.hints.push_back({"A", "SELECT", 0x2EA043});
  out.aDim = aDim || aWord.empty();
  out.keep = keep;
  return out;
}
HubPage hub_page(float w, float h, float top, float bottom) {
  (void)w;
  (void)h;
  (void)top;
  (void)bottom;
  WdScope wd("drawing thread", "hub picture (CARDS / LOCKER page)");  // r237
  HubPage out;
  if (!g_loaded.load()) return out;
  if ((g_page.load() == kPageMarket || g_page.load() == kPageMuseum || g_page.load() == kPageCollectors) && g_sceneMode.load() == 0)
    return market_museum_page(g_page.load());  // (r213: + COLLECTORS)
  BinderView b;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    b.list = binder_list();
    b.spreads = std::max(1, static_cast<int>((b.list.size() + kPerSpread - 1) / kPerSpread));
    g_spread = std::max(0, std::min(b.spreads - 1, g_spread));
    b.spread = g_spread;
    b.cursor = g_cursor;
    b.total = g_setN;  // (r217: a set's count)
    b.packs = static_cast<int>(g_packs.size());
    b.coins = g_coins;  // r140
    b.lastCoins = g_lastCoins;
    b.lastWhen = g_lastCoinsWhen;
    b.lastWhy = g_lastCoinsWhy;
    b.say = g_shopSay;
    b.page = g_page.load();  // r146
    b.shop = b.page == kPageShop;
    b.pick = g_shopPick.load();
    b.packsOnOpen = g_packsOnOpen.load();  // r236
    build_sets();
    b.tradeSet = g_tradeSet;  // r226 (the trade-in show's words too)
    if (b.page == kPageTrade) {  // r146: your spare base cards (r147: commons first), the most first
      // r226: the page's set's (g_tradeSet); both sets' counts for their panels
      for (size_t i = 0; i < g_set.size(); ++i) {
        const int sp = spare_of(static_cast<int>(i));
        const int s = g_set[i].set == 1 ? 1 : 0;
        b.setSpareAll[s] += sp;
        b.setSpareFree[s] += g_keep.count(g_set[i].id) > 0 ? 0 : sp;
      }
      std::vector<std::pair<int, int>> next = trade_pick(g_tradeSet);
      for (size_t i = 0; i < g_set.size(); ++i) {
        if (g_set[i].set != g_tradeSet) continue;
        const int sp = spare_of(static_cast<int>(i));
        if (sp <= 0) continue;
        BinderView::Tile t;
        t.c = g_set[i];
        t.o = g_owned[i];
        t.spare = sp;
        t.kept = g_keep.count(g_set[i].id) > 0;
        for (const auto& nx : next)
          if (nx.first == static_cast<int>(i)) t.next = nx.second;
        b.tiles.push_back(t);
        b.spareAll += sp;
        b.spareFree += t.kept ? 0 : sp;
      }
      std::stable_sort(b.tiles.begin(), b.tiles.end(), [](const BinderView::Tile& x, const BinderView::Tile& y) {
        return trade_before(g_byKey[x.c.id], x.spare, g_byKey[y.c.id], y.spare);  // (r147)
      });
      if (b.tiles.empty()) g_tradeOnSets = true;  // r226: no cards of this set - the cursor on the set panels (left / right: the other set)
      b.tradeOnSets = g_tradeOnSets;
      b.tradedCards = g_tradedCards;
      b.tradedPacks = g_tradedPacks;
      g_tradeCursor = std::max(0, std::min(static_cast<int>(b.tiles.size()) - 1, g_tradeCursor));
      const int rows = (static_cast<int>(b.tiles.size()) + kTileCols - 1) / kTileCols;  // (fewer after a trade: still in view)
      g_tradeTop = std::max(0, std::min(std::max(0, rows - kTileRows), g_tradeTop));
      if (g_tradeCursor / kTileCols < g_tradeTop) g_tradeTop = g_tradeCursor / kTileCols;
      if (g_tradeCursor / kTileCols >= g_tradeTop + kTileRows) g_tradeTop = g_tradeCursor / kTileCols - kTileRows + 1;
      b.tradeCursor = g_tradeCursor;
      b.tradeTop = g_tradeTop;
      b.tradeSay = g_tradeSay;
      if (!b.tiles.empty()) {
        const BinderView::Tile& t = b.tiles[static_cast<size_t>(b.tradeCursor)];
        b.curLevel = card_level(t.o);
        b.curXp = card_bonus(g_byKey[t.c.id], &b.curWhy, nullptr);
      }
    }
    if (b.page == kPageExplosions || b.page == kPageTakeovers) {  // r153: GOAL EXPLOSIONS (r236: GOAL FX / TAKEOVERS)
      b.fxList = fx_list(b.page == kPageTakeovers);
      g_fxPos[b.page == kPageTakeovers ? 1 : 0] = std::max(0, std::min(static_cast<int>(b.fxList.size()) - 1, g_fxPos[b.page == kPageTakeovers ? 1 : 0]));
      b.fxPos = g_fxPos[b.page == kPageTakeovers ? 1 : 0];
      const char* eq = xrgoalfx::equipped();
      for (const xrgoalfx::Item& it : xrgoalfx::catalogue()) {
        BinderView::FxTile t;
        t.id = it.id;
        t.name = it.name;
        t.desc = it.desc;
        t.price = it.price;
        t.tier = it.tier;
        t.rgb1 = it.rgb1;
        t.rgb2 = it.rgb2;
        t.secs = it.seconds;
        t.owned = xrgoalfx::owned(it.id);
        t.equipped = eq != nullptr && t.id == eq;
        b.fx.push_back(t);
      }
      b.fxNone = eq == nullptr || std::string(eq) == "none" || eq[0] == '\0';
      // (r161: up to NO EXPLOSION, the last tile - r157 clamped it one short, so the cursor never stayed on it; and the
      // rows scroll: 4 in view, the cursor's always among them)
      g_fxCursor = std::max(0, std::min(static_cast<int>(b.fx.size()), g_fxCursor));
      b.fxCursor = g_fxCursor;
      const int fxRows = (static_cast<int>(b.fx.size()) + 1 + kFxCols - 1) / kFxCols;
      g_fxTop = std::max(0, std::min(std::max(0, fxRows - kFxRowsShown), g_fxTop));
      if (g_fxCursor / kFxCols < g_fxTop) g_fxTop = g_fxCursor / kFxCols;
      if (g_fxCursor / kFxCols >= g_fxTop + kFxRowsShown) g_fxTop = g_fxCursor / kFxCols - kFxRowsShown + 1;
      b.fxTop = g_fxTop;
      b.fxArmed = g_fxArmed;  // r157: the hub's confirm asks until A / B
      b.coins = g_coins;
      b.fxPreviewing = g_fxPreviewing && xrgoalfx::playing();
      b.fxSay = g_fxSay;
    }
    if (b.page == kPageGrading) {  // r152: TIZM GRADING - your slabs, the cards you can send
      for (int k : grading_slab_order()) b.gSlabs.push_back({g_set[g_slabs[static_cast<size_t>(k)].ref.card], g_slabs[static_cast<size_t>(k)]});
      for (int ci : grading_tiles()) {
        BinderView::GTile t;
        t.c = g_set[ci];
        t.o = g_owned[ci];
        grade_pick(ci, &t.r);
        t.cost = grade_cost(t.r);
        b.gTiles.push_back(t);
      }
      const int n = static_cast<int>(b.gSlabs.size() + b.gTiles.size());
      g_gradeCursor = std::max(0, std::min(std::max(0, n - 1), g_gradeCursor));
      b.gCursor = g_gradeCursor;
      b.gTop = g_gradeTop;
      b.gLeft = g_gradeLeft;
      b.gArmed = g_gradeArmed;  // r157: asks (the hub's confirm) until A sends it / B says no - no 4 s timer
      b.gSay = g_gradeSay;
    }
    if (b.page == kPageSets) {  // r146: every set, its progress
      auto row = [&](const CardSet& s) {
        BinderView::SetRow r;
        r.name = s.name;
        r.rgb = s.rgb;
        r.total = static_cast<int>(s.cards.size());
        r.have = set_have(s);
        r.done = r.have == r.total && r.total > 0;
        r.suits = s.suits;
        for (size_t k = 0; k < s.cards.size() && k < 6; ++k) r.who.push_back({g_set[s.cards[k]].key, g_owned[s.cards[k]].total() > 0});
        return r;
      };
      for (const CardSet& s : g_lines) b.lines.push_back(row(s));
      std::stable_sort(b.lines.begin(), b.lines.end(), [](const BinderView::SetRow& x, const BinderView::SetRow& y) {
        if (x.done != y.done) return x.done;
        return x.have * y.total > y.have * x.total;  // (the nearest to done first)
      });
      for (const CardSet& s : g_types) b.types.push_back(row(s));
      for (const auto& r : b.lines) b.linesDone += r.done ? 1 : 0;
      for (const auto& r : b.types) b.typesDone += r.done ? 1 : 0;
      const std::vector<int> list = binder_list();
      for (size_t p = 0; p < (list.size() + 8) / 9; ++p) {
        int have = 0;
        for (size_t s = 0; s < 9; ++s) have += p * 9 + s < list.size() && g_owned[list[p * 9 + s]].total() > 0 ? 1 : 0;
        b.pageFill.push_back(have / 9.0f);
        b.pagesFull += have == 9 ? 1 : 0;
      }
      b.coinPct = coin_pct(b.pagesFull);
      for (size_t i = 0; i < g_set.size(); ++i) b.bonusChars += g_set[i].set == 0 && card_bonus(static_cast<int>(i), nullptr, nullptr) > 0 ? 1 : 0;
      g_setsTop = std::max(0, std::min(std::max(0, sets_rows(b) - kSetsShown), g_setsTop));  // r157 round 2: rows of the one list
      b.setsTop = g_setsTop;
    }
    for (size_t i = 0; i < g_set.size(); ++i) {
      const Owned& o = g_owned[i];
      if (g_set[i].set == 1) {  // r217: the JAM '96 set's own progress
        b.jamHere += g_set[i].here ? 1 : 0;
        b.jamHave += o.total() > 0 ? 1 : 0;
      } else if (g_set[i].set == 2) {  // r230: LANCINI EXQUISITE's
        b.exqHere += g_set[i].here ? 1 : 0;
        b.exqHave += o.total() > 0 ? 1 : 0;
      } else {
        if (g_set[i].here) {
          ++b.here;
          ++b.hereTier[g_set[i].rarity];
        }
        if (o.total() > 0) {
          ++b.have;
          ++b.haveTier[g_set[i].rarity];
        }
      }
      if (o.shinyAny()) ++b.shinies;
      for (int p = 1; p < 4; ++p) b.finishes[p] += o.n[p][0] + o.n[p][1];
      for (const Ref& h : o.hits) ++b.hits[h.hit];  // r149
    }
    b.godPacks = g_godPacks;
    for (int s = 0; s < kPerSpread; ++s) {
      const size_t at = static_cast<size_t>(b.spread) * kPerSpread + s;
      if (at >= b.list.size()) break;
      b.cards.push_back(g_set[b.list[at]]);
      b.owned.push_back(g_owned[b.list[at]]);
      b.isNew.push_back(g_newCards.count(g_set[b.list[at]].id) != 0);  // r149
    }
    if (b.page != kPageTrade && b.cursor < static_cast<int>(b.cards.size())) {  // r146: its level, its bonuses
      const int ci = b.list[static_cast<size_t>(b.spread) * kPerSpread + b.cursor];
      b.curLevel = card_level(g_owned[ci]);
      b.curXp = card_bonus(ci, &b.curWhy, nullptr);
    }
    if (b.page == kPageLooks || b.page == kPageCovers) {  // r160: the LOOKS page's cursor, a buy asking, its last word (r236: BALLS / BINDERS)
      g_lkBallFocus = b.page == kPageLooks;
      g_lkBall = std::max(0, std::min(kBallTiles - 1, g_lkBall));
      g_lkCol[0] = std::max(0, std::min(kSkinCount - 1, g_lkCol[0]));
      const int rows = (kSkinCount + kLkCols - 1) / kLkCols;
      g_lkCol[0] = std::max(0, std::min(kSkinCount - 1, g_lkCol[0]));
      const int row = g_lkCol[0] / kLkCols;
      g_lkFirst[0] = std::max(0, std::min(std::max(0, rows - kLkRows), g_lkFirst[0]));
      if (row < g_lkFirst[0]) g_lkFirst[0] = row;
      if (row >= g_lkFirst[0] + kLkRows) g_lkFirst[0] = row - kLkRows + 1;
      b.lkCol[0] = g_lkCol[0];
      b.lkFirst[0] = g_lkFirst[0];
      b.lkArmKind = g_lkArmKind;
      b.lkArm = g_lkArm;
      b.lkSay = g_lkSay;
      b.lkBallFocus = g_lkBallFocus;  // r168
      b.lkBall = std::max(0, std::min(kBallTiles - 1, g_lkBall));
      g_lkBall = b.lkBall;  // r170: the row scrolls with the ring
      if (g_lkBall < g_lkBallFirst) g_lkBallFirst = g_lkBall;
      if (g_lkBall >= g_lkBallFirst + kBallCols) g_lkBallFirst = g_lkBall - kBallCols + 1;
      g_lkBallFirst = std::max(0, std::min(kBallTiles - kBallCols, g_lkBallFirst));
      b.lkBallFirst = g_lkBallFirst;
    }
  }
  // r201: the spread's market values (the market's lock, after g_mutex: never inside it)
  for (int s = 0; s < static_cast<int>(b.cards.size()); ++s) {
    const size_t at = static_cast<size_t>(b.spread) * kPerSpread + s;
    b.values.push_back(at < b.list.size() && b.owned[static_cast<size_t>(s)].total() > 0 ? xrmarket::card_value(b.list[at]) : -1);
  }
  // r160: the skin on (its own lock: after g_mutex), what you own, a preview up, the binder out
  b.skin = look_on(kLookSkin);
  b.binderOut = g_binderOut.load();
  if (b.page == kPageLooks || b.page == kPageCovers) {  // (r236: + BINDERS)
    for (int i = 0; i < kSkinCount; ++i) b.lkOwned[0].push_back(look_owned(kLookSkin, i));
    b.lkPreview = g_skinPreview.load() >= 0;
    for (int i = 0; i < kBallTiles; ++i) b.ballOwned.push_back(ball_owned(i));  // r168
    b.ballOn = ball_on();
    b.ballPreview = xrballs::previewing();
  }
  b.mode = g_sceneMode.load();
  Pc P;
  // r157 round 2 (menu overhaul): the cards' pop-up pages (the 3D is in your hands / the room; this is what the panel says
  // meanwhile), in the hub's page area, 40 px or more, a few words each; their own hint bar (the hub's hidden)
  const float qx0 = 470, qx1 = 2120, qcx = 0.5f * (qx0 + qx1), qroom = qx1 - qx0 - 80;
  if (b.mode == 1) {
    // a pack open in your hands: no binder behind it (it would give the cards away)
    P.box(qx0, 222, qx1, 1180, 30, 0x151C36);
    mini_pack(P, qcx - 560, 300, qcx - 260, 760);
    P.text(g_openSet.load() == 2 ? "OPENING AN EXQUISITE PACK" : (g_openSet.load() == 1 ? "OPENING A JAM '96 PACK" : "OPENING A PACK"), qcx + 220, 380, 80, 880,
           g_openSet.load() == 2 ? 0xE6C774u : (g_openSet.load() == 1 ? 0xFFD400u : kGold), 5);
    P.text("HOLD IT IN ONE HAND", qcx + 220, 500, 48, 880, 0xFFFFFF, 3);
    P.text("THE OTHER TRIGGER RIPS IT", qcx + 220, 580, 44, 880, 0xB8C2D6, 2);
    P.text("A TRIGGER TURNS THE FRONT CARD OVER", qcx, 860, 48, qroom, 0xFFFFFF, 3);
    P.text("THE BIGGEST PULL COMES LAST", qcx, 940, 44, qroom, 0xB8C2D6, 2);
    P.text("BOTH GRIPS TURN AND SIZE IT", qcx, 1010, 44, qroom, 0xB8C2D6, 2);
    P.text(g_openSet.load() == 2 ? xrexq::kSetName : (g_openSet.load() == 1 ? kJamSetName : kSetName), qcx, 1110, 40, qroom, 0x8A94B8, 2);  // (r217; r230)
    out.hints = {{"TRIGGER", "RIP / NEXT CARD", 0x2EA043}, {"A", "SAME / NEXT PACK", 0x2EA043}, {"B", "INTO THE BINDER", 0xC0392B}};
  } else if (b.mode == 4) {
    // r152: a slab BACK FROM TIZM GRADING, its envelope opening in front of you
    P.box(qx0, 222, qx1, 1180, 30, 0x151C36);
    hexagon(P, qcx - 420, 540, 150, 0xFFFFFF);
    hexagon(P, qcx - 420, 540, 108, 0x8E9BB4);
    P.text("BACK FROM", qcx + 220, 440, 64, 880, 0xFFFFFF, 4);
    P.text("TIZM GRADING", qcx + 220, 530, 84, 880, kGold, 5);
    P.text("WATCH ITS GRADE FLIP UP", qcx, 860, 48, qroom, 0xFFFFFF, 3);
    P.text("GRIP IT TO HOLD IT  -  LT RT TURN IT OVER", qcx, 950, 44, qroom, 0xB8C2D6, 2);
    P.text(kSetName, qcx, 1110, 40, qroom, 0x8A94B8, 2);
    out.hints = {{"GRIP", "HOLD IT", 0x3A4A70}, {"LT RT", "TURN IT OVER", 0x3A4A70}, {"B", "INTO THE BINDER", 0xC0392B}};
  } else if (b.mode == 3) {
    // r146: a trade-in going on in front of you (its cards into the slot, its pack out)
    P.box(qx0, 222, qx1, 1180, 30, 0x151C36);
    for (int i = 4; i >= 0; --i)
      mini_pack(P, qcx - 640 + i * 66, 330 - (i % 2) * 16, qcx - 490 + i * 66, 556 - (i % 2) * 16, i > 0);
    P.text("TRADE-IN", qcx + 260, 400, 84, 800, kGold, 5);
    // (r226: the set traded - the page's, it can't change during the show)
    P.text(std::to_string(trade_for(b.tradeSet)) + (b.tradeSet == 1 ? " SPARE JAM '96 CARDS IN" : " SPARE CARDS IN"), qcx + 260, 510, 48, 800, 0xFFFFFF, 3);
    P.text(b.tradeSet == 1 ? "1 FREE JAM '96 PACK OUT" : "1 FREE PACK OUT", qcx + 260, 580, 48, 800, 0xB8C2D6, 2);
    P.text(std::to_string(b.packs) + (b.packs == 1 ? " PACK WAITING" : " PACKS WAITING"), qcx, 880, 56, qroom, kGold, 3);
    P.text(kSetName, qcx, 1110, 40, qroom, 0x8A94B8, 2);
    out.hints = {{"A", "DONE", 0x2EA043}, {"B", "SKIP", 0xC0392B}};
  } else {
   if (b.page == kPageTrade) {
    trade_page(P, b);  // r146
   } else if (b.page == kPageSets) {
    sets_page(P, b);  // r146
   } else if (b.page == kPageGrading) {
    grading_page(P, b);  // r152
   } else if (b.page == kPageExplosions || b.page == kPageTakeovers || b.page == kPageLooks || b.page == kPageCovers) {
    locker_page_b(P, b);  // r236: LOCKER (r157's EXPLOSIONS / r160's LOOKS split in four)
   } else if (b.shop) {
    packs_page_b(P, b);  // r236: CARDS > PACKS (r140's shop + opening)
   } else if (false) {  // (r140-r230's shop layout, kept for the record: never drawn)
    // r140: the CARD SHOP (instead of the binder's pages): your TIZM COINS, a pack or a box of five, the last match's coins
    const float x0 = kAreaX0, x1 = kAreaX1, y0 = kAreaY0, cx = 0.5f * (x0 + x1);
    P.box(x0, y0, x1, 1180, 26, 0x1A2140, 1.0f, 0x0A0E1C);  // (r157 round 2: down to the page area's foot)
    // r217 (Lance 11-11): two packs side by side, each with its real wrapper and its box - LANCINI TIZM (50 / 200) and the premium
    // JAM '96 (250 / 1,000); stick left / right the pack, up / down a pack or its box (b.pick = set x 2 + box)
    P.text("THE CARD SHOP", cx, y0 + 58, 70, 900, kGold, 5);
    coin_icon(P, cx - 250, y0 + 140, 40);
    P.left(xrcoin::fit(b.coins, 9), cx - 196, y0 + 140, 70, 360, 0xFFFFFF, 5);  // (r208: commas; past 9 characters short: "4.2B")
    P.left("BIT COINS", cx + 40, y0 + 146, 40, 360, 0xE8ECF5, 3);
    // r230: three columns - LANCINI TIZM, JAM '96, LANCINI EXQUISITE (1,000 a pack, THE VAULT case of five 5,000: shop only)
    const float colW = (x1 - x0 - 60 - 2 * 18) / 3.0f;
    for (int set = 0; set < 3; ++set) {
      const float cx0 = x0 + 30 + set * (colW + 18), cx1 = cx0 + colW, ccx = 0.5f * (cx0 + cx1);
      const uint32_t head = set == 0 ? kGold : (set == 1 ? 0xFFD400 : 0xE6C774);
      const uint32_t panel = set == 0 ? 0x151C36 : (set == 1 ? 0x1A0E30 : 0x0D0C0B), sel = set == 0 ? 0x24305C : (set == 1 ? 0x34185A : 0x2A2418);
      P.box(cx0, y0 + 196, cx1, y0 + 250, 27, set == 0 ? 0x0B1022 : (set == 1 ? 0x0C0814 : 0x050505));
      P.ring(cx0, y0 + 196, cx1, y0 + 250, 27, 3, set == 0 ? 0x8A94B8 : (set == 1 ? 0xE0218A : 0xC9A04A));
      P.text(set == 0 ? "LANCINI TIZM" : (set == 1 ? "JAM '96  -  PREMIUM" : "LANCINI EXQUISITE"), ccx, y0 + 223, 36, cx1 - cx0 - 40, head, 3);
      for (int box = 0; box < 2; ++box) {
        const int k = set * 2 + box;
        const float oy0 = y0 + 266 + box * 318.0f, oy1 = oy0 + 300;
        const int price = set == 2 ? (box ? xrexq::kBoxPrice : xrexq::kPackPrice)
                                   : set == 0 ? (box ? kBoxPrice : kPackPrice) : (box ? kJamBoxPrice : kJamPackPrice);
        const bool can = b.coins >= price;
        P.box(cx0, oy0, cx1, oy1, 26, k == b.pick ? sel : panel);
        if (k == b.pick) P.ring(cx0 - 7, oy0 - 7, cx1 + 7, oy1 + 7, 32, 7, head);
        const ImgP pic = shop_pic(set, box != 0);
        const float px0 = cx0 + 14, py0 = oy0 + 16, phMax = oy1 - oy0 - 32, pwMax = 0.48f * (cx1 - cx0);
        float tx = cx0 + 0.5f * (cx1 - cx0);
        if (pic) {
          float pw = phMax * static_cast<float>(pic->w) / pic->h, ph = phMax;
          if (pw > pwMax) ph *= pwMax / pw, pw = pwMax;
          const float pyc = py0 + 0.5f * (phMax - ph);
          P.image(pic, px0, pyc, px0 + pw, pyc + ph, false);
          tx = px0 + pw + 14;
        } else if (box == 0) {
          mini_pack(P, px0 + 10, py0 + 10, px0 + 140, py0 + phMax - 10);
          tx = px0 + 156;
        } else {
          for (int i = 4; i >= 0; --i) mini_pack(P, px0 + i * 22, py0 + 40 - (i % 2) * 12, px0 + 90 + i * 22, py0 + phMax - 30 - (i % 2) * 12, i > 0);
          tx = px0 + 196;
        }
        const float tw = cx1 - 14 - tx;
        P.left(set == 2 ? (box ? "THE VAULT" : "1 PACK") : (box ? "BOX OF 5" : "1 PACK"), tx, oy0 + 56, 38, tw, 0xFFFFFF, 4);
        if (set == 2 && box) P.left("A CASE OF 5 PACKS", tx, oy0 + 96, 24, tw, 0xC9B98A, 2);
        coin_icon(P, tx + 20, oy0 + 150, 22);
        P.left(xrcoin::text(price), tx + 50, oy0 + 150, 48, tw - 50, can ? head : 0xFF6A5A, 4);
        const std::string sub = !can ? "NEED " + xrcoin::text(price - b.coins) + " MORE"
                                     : set == 2 ? (box ? "1,000 A PACK" : "3 CARDS  -  1 HIT")
                                                : (box ? (set == 0 ? "40 A PACK - BEST BUY" : "200 A PACK - BEST BUY") : (set == 0 ? "5 CARDS" : "5 JAM '96 CARDS"));
        P.left(sub, tx, oy0 + 214, 28, tw, !can ? 0xFF8A7A : 0xB8C2D6, 2);
        if (set == 1 && box == 0) P.left("WIN A MATCH: 1 FREE", tx, oy0 + 256, 24, tw, 0xE0218A, 2);
        if (set == 0 && box == 0) P.left("EVERY MATCH: 1 FREE", tx, oy0 + 256, 24, tw, 0x8A94B8, 2);
        if (set == 2) P.left(box ? "EVERY CARD NUMBERED" : "SHOP ONLY", tx, oy0 + 256, 24, tw, 0xE6C774, 2);
      }
    }    if (!b.say.empty()) P.text(b.say, cx, y0 + 912, 36, x1 - x0 - 60, kGold, 3);
    if (b.lastCoins > 0 && b.say.empty()) {  // (r217: the shop's last word in its place while it's up)
      // r146: its multipliers (full binder pages, your players' card XP) on a gold line of their own
      std::string why = upper(b.lastWhy), bonus;
      size_t cut = why.find(", BINDER PAGES");
      if (cut == std::string::npos) cut = why.find("; CARD XP");
      if (cut != std::string::npos) {
        bonus = why.substr(cut + 2);
        why = why.substr(0, cut);
        for (size_t at = bonus.find("; "); at != std::string::npos; at = bonus.find("; ")) bonus.replace(at, 2, "  -  ");
      }
      P.text("LAST MATCH: +" + std::to_string(b.lastCoins) + " BIT COINS  -  " + why, cx, y0 + 912, 32, x1 - x0 - 60, 0xE8ECF5, 2);
      if (!bonus.empty()) P.text(bonus, cx, y0 + 950, 30, x1 - x0 - 60, kGold, 2);
    } else if (b.say.empty())
      P.text("GOALS, ASSISTS, TACKLES, WINS: ALL EARN COINS", cx, y0 + 912, 36, x1 - x0 - 60, 0xB8C2D6, 2);
   } else {
    // the binder: two pages, the spine - r160: framed in your binder's skin (its covers' insides, its spine)
    binder_frame(P, b.skin);
    P.box(kLeftPageX, kBinderY, kLeftPageX + kPageW, kBinderY + kPageH, 18, 0x10131C);
    P.box(kRightPageX, kBinderY, kRightPageX + kPageW, kBinderY + kPageH, 18, 0x10131C);
    for (int r = 0; r < 3; ++r)  // the rings
      P.box(kLeftPageX + kPageW - 4, kBinderY + (0.17f + 0.33f * r) * kPageH - 15, kRightPageX + 4, kBinderY + (0.17f + 0.33f * r) * kPageH + 15, 12, 0xB8BEC8, 1.0f, 0x6A707C);
    for (int s = 0; s < kPerSpread; ++s) {
      float x0, y0, x1, y1;
      pocket_rect(s, &x0, &y0, &x1, &y1);
      if (s < static_cast<int>(b.cards.size())) {
        const Card& c = b.cards[s];
        // r149: a shiny's pocket shows its shiny picture (its big one once made, else its shiny portrait)
        const ImgP pic = b.owned[s].shinyAny() ? card_art(c, true, 0.0f) : portrait(c.key);
        mini_card(P, c, b.owned[s], x0, y0, x1, y1, pic, false, b.isNew[s]);  // (r163: no sleeves - the card fills its pocket again)
        if (s < static_cast<int>(b.values.size()) && b.values[static_cast<size_t>(s)] > 0) {  // r201: its market value, a gold tag
          const std::string vt = xrcoin::fit(b.values[static_cast<size_t>(s)], 6);  // (r210: a grail's millions short: "48.3M")
          const float tw = P.width(vt, 30) + 40;
          P.box(x1 - tw - 4, y0 + 4, x1 - 4, y0 + 42, 19, 0x0B1020, 0.88f);
          P.box(x1 - tw + 4, y0 + 13, x1 - tw + 24, y0 + 33, 10, kGold);
          P.left(vt, x1 - tw + 30, y0 + 24, 30, tw - 32, kGold, 2);
        }
      } else {
        P.box(x0, y0, x1, y1, 14, 0x0C0F18);
      }
      if (s == b.cursor) P.ring(x0 - 10, y0 - 10, x1 + 10, y1 + 10, 22, 8, kGold);
    }
    const int firstPage = b.spread * 2 + 1;
    // r157: the stick past the binder's edge turns its pages (arrows lit where there are more)
    const float pcx = (kLeftPageX + kRightPageX + kPageW) / 2, pcy = kBinderY + kPageH + kFrameV + 26;
    // r217: the binder holds LANCINI TIZM's pages, then JAM '96's - the spread's set named
    const bool jamSpread = !b.cards.empty() && b.cards[0].set == 1, exqSpread = !b.cards.empty() && b.cards[0].set == 2;  // (r230: + EXQUISITE)
    P.text(std::string(exqSpread ? "EXQUISITE  -  " : (jamSpread ? "JAM '96  -  " : "")) + "PAGES " + std::to_string(firstPage) + "-" + std::to_string(firstPage + 1) + " OF " + std::to_string(b.spreads * 2),  // r171 (r236: the turn's words live in the bar: R TURN PAGE)
           pcx, pcy, 40, 1100, 0xB8C2D6, 2);
    const uint32_t back = b.spread > 0 ? kGold : 0x3A4560, on = b.spread + 1 < b.spreads ? kGold : 0x3A4560;
    P.poly({kLeftPageX + 10, pcy, kLeftPageX + 46, pcy - 24, kLeftPageX + 46, pcy + 24}, back);
    P.poly({kRightPageX + kPageW - 10, pcy, kRightPageX + kPageW - 46, pcy - 24, kRightPageX + kPageW - 46, pcy + 24}, on);
    if (false) {  // r160: X takes your binder out (r236: printed in the bar only - no on-page copy)
      const std::string w = b.binderOut ? "PUT THE BINDER AWAY" : "TAKE THE BINDER OUT";
      const float tw = P.width(w, 40), bx0 = pcx - 0.5f * (tw + 100), by = 1140;
      P.box(bx0 - 14, by - 34, bx0 + tw + 100, by + 34, 34, 0x0B1020, 0.85f);
      P.box(bx0 - 3, by - 31, bx0 + 61, by + 33, 32, 0xFFFFFF);
      P.box(bx0, by - 28, bx0 + 58, by + 30, 29, 0x2E6FD0);
      P.text("X", bx0 + 29, by + 1, 36, 50, 0xFFFFFF, 2);
      P.left(w, bx0 + 76, by, 40, tw + 10, 0xFFFFFF, 2);
    }
   }
    // r157 round 2 (menu overhaul, no scaling): your collection under the hub's rail (the brand, have / total, coins);
    // the one under the ring and your packs in a column at the right; every word 40 px or more (r140-r156's side panel
    // was 640 px wide with 22-34 px lines)
    // r236 (LOOK B, the sitemap): PACKS and the LOCKER pages draw their own focus panels; the other pages keep the column at the
    // right (the one under the ring, full height now - no packs box: packs live on PACKS, HOME and the tab badge); the collection
    // box left the rail's foot for the BINDER column's foot (the numbered rail runs down to it)
    const bool ownPanels = b.page == kPageShop || b.page == kPageLooks || b.page == kPageCovers || b.page == kPageExplosions || b.page == kPageTakeovers;
    if (!ownPanels && b.page == kPageBinder) {
      xrmenu::Kit K(P.v);
      const float sx0 = kAreaX1 + 20, sx1 = 2120, cy0 = 860;
      xrmenu::label(K, "YOUR COLLECTION", sx0 + 30, cy0 + 20, 32);
      xrmenu::say(K, "TIZM  " + std::to_string(b.have) + " / " + std::to_string(b.here), sx0 + 30, cy0 + 82, 44, 0xFFFFFF, 'l', sx1 - sx0 - 60);
      xrmenu::say(K, "JAM '96  " + std::to_string(b.jamHave) + " / " + std::to_string(b.jamHere), sx0 + 30, cy0 + 150, 40, 0xFF5AC8, 'l', sx1 - sx0 - 60);
      xrmenu::say(K, "EXQUISITE  " + std::to_string(b.exqHave) + " / " + std::to_string(b.exqHere), sx0 + 30, cy0 + 214, 40, 0xE6C774, 'l', sx1 - sx0 - 60);
    }
    if (false) {  // (r217-r235's collection box under the rail: never drawn - see above)
      const float cx0 = 40, cx1 = 420, ccx = 0.5f * (cx0 + cx1), cy0 = 960, cy1 = 1180;
      P.box(cx0, cy0, cx1, cy1, 30, 0x151C36);
      logo(P, cx0 + 90, cy0 + 6, 0.5f, false);  // (r217: smaller - the JAM '96 line under it)
      P.text(std::to_string(b.have) + " / " + std::to_string(b.here), ccx, cy0 + 108, 40, cx1 - cx0 - 40, 0xFFFFFF, 4);
      P.box(cx0 + 30, cy0 + 134, cx1 - 30, cy0 + 145, 6, 0x2A3354);
      if (b.here > 0 && b.have > 0)
        P.box(cx0 + 30, cy0 + 134, cx0 + 30 + (cx1 - cx0 - 60) * std::min(1.0f, static_cast<float>(b.have) / b.here), cy0 + 145, 6, kGold);
      // r217: the JAM '96 set's progress under it
      // r230: + LANCINI EXQUISITE's beside it
      P.text("JAM '96 " + std::to_string(b.jamHave) + "/" + std::to_string(b.jamHere), ccx - 92, cy0 + 166, 30, 172, 0xFF5AC8, 3);
      P.text("EXQ " + std::to_string(b.exqHave) + "/" + std::to_string(b.exqHere), ccx + 98, cy0 + 166, 30, 160, 0xE6C774, 3);
      P.text(xrcoin::fit(b.coins, 11) + " COINS", ccx, cy0 + 202, 34, cx1 - cx0 - 40, kGold, 2);  // (r208)
    }
    const float sx0 = kAreaX1 + 20, sx1 = 2120, scx = 0.5f * (sx0 + sx1), room = sx1 - sx0 - 30;
    const float ty0 = kAreaY0, ty1 = b.page == kPageBinder ? 840.0f : 1180.0f;  // r236: full height (no packs box under it)
    if (!ownPanels) P.box(sx0, ty0, sx1, ty1, 28, 0x151C36);
    // (a line too long for the column split in two at the space nearest its middle)
    auto two = [&](const std::string& t, float y, float px, uint32_t rgb) {  // (lines filled word by word, up to 4)
      std::vector<std::string> out(1);
      size_t at = 0;
      while (at <= t.size()) {
        size_t sp = t.find(' ', at);
        if (sp == std::string::npos) sp = t.size();
        const std::string word = t.substr(at, sp - at), tryLine = out.back().empty() ? word : out.back() + " " + word;
        if (!out.back().empty() && P.width(tryLine, px) > room && out.size() < 4) out.push_back(word);
        else out.back() = tryLine;
        at = sp + 1;
      }
      const float lh = 1.12f * px, top = y - 0.5f * lh * (out.size() - 1);
      for (size_t i = 0; i < out.size(); ++i) P.text(out[i], scx, top + i * lh, px, room, rgb, 2);
    };
    const Card* cc = nullptr;
    const Owned* co = nullptr;
    if (b.page == kPageTrade && !b.tiles.empty()) {
      cc = &b.tiles[static_cast<size_t>(b.tradeCursor)].c;
      co = &b.tiles[static_cast<size_t>(b.tradeCursor)].o;
    } else if (b.page == kPageBinder && b.cursor < static_cast<int>(b.cards.size())) {
      cc = &b.cards[b.cursor];
      co = &b.owned[b.cursor];
    }
    const float y = ty0 + 70;
    if (ownPanels) {  // r236: PACKS / LOCKER draw their own focused one
    } else if (b.page == kPageExplosions) {  // r157: the explosion under the cursor
      if (b.fxCursor < static_cast<int>(b.fx.size())) {
        const BinderView::FxTile& t = b.fx[static_cast<size_t>(b.fxCursor)];
        two(upper(t.name), y + 20, 48, 0xFFFFFF);
        P.text(xrgoalfx::tier_name(t.tier), scx, y + 140, 40, room, xrgoalfx::tier_rgb(t.tier), 2);
        P.text(t.equipped ? "ON" : (t.owned ? "OWNED" : (t.price > 0 ? std::to_string(t.price) + " COINS" : "FREE")), scx, y + 200, 44, room,
               t.equipped ? 0x6ED36E : kGold, 3);
        two(upper(t.desc), y + 320, 40, 0xE8ECF5);
        if (!t.owned && b.coins < t.price) P.text("NEED " + std::to_string(t.price - b.coins) + " MORE", scx, y + 440, 40, room, 0xFF8A7A, 2);
      } else {
        two("NO EXPLOSION", y + 20, 48, 0xFFFFFF);
        two("ONLY THE GOAL'S OWN SHOW", y + 180, 40, 0xB8C2D6);
      }
    } else if (b.page == kPageLooks && b.lkBallFocus) {  // r168: the ball under the ring
      const int i = b.lkBall;
      const xrballs::Item& it = ball_item(i);
      const bool mine = i < static_cast<int>(b.ballOwned.size()) && b.ballOwned[static_cast<size_t>(i)];
      const bool equipped = b.ballOn == i;
      two(it.name, y + 20, 48, 0xFFFFFF);
      P.text(xrgoalfx::tier_name(it.tier), scx, y + 140, 40, room, xrgoalfx::tier_rgb(it.tier), 2);
      P.text(equipped ? "ON" : (mine ? "OWNED" : (it.price > 0 ? std::to_string(it.price) + " COINS" : "FREE")), scx, y + 200, 44, room,
             equipped ? 0x6ED36E : kGold, 3);
      {  // r170: its line, up to its first " - "
        std::string d = it.desc;
        const size_t cut = d.find(" - ");
        if (cut != std::string::npos) d = d.substr(0, cut);
        two(i == 1 ? "FLAMES, EMBERS, A SCORCH" : (i == 2 ? "SPINS, CLOUDS, CITY LIGHTS" : d), y + 280, 40, 0xE8ECF5);
      }
      if (!mine && b.coins < it.price) P.text("NEED " + std::to_string(it.price - b.coins) + " MORE", scx, y + 370, 40, room, 0xFF8A7A, 2);
      else two("X: SEE IT IN FRONT OF YOU", y + 380, 40, 0xB8C2D6);
    } else if (b.page == kPageLooks) {  // r160: the skin under the ring
      const int i = b.lkCol[0];
      const LookItem& it = look_item(kLookSkin, i);
      const bool mine = i < static_cast<int>(b.lkOwned[0].size()) && b.lkOwned[0][static_cast<size_t>(i)];
      const bool equipped = b.skin == i;
      two(upper(it.name), y + 20, 48, 0xFFFFFF);
      P.text(xrgoalfx::tier_name(it.tier), scx, y + 140, 40, room, xrgoalfx::tier_rgb(it.tier), 2);
      P.text(equipped ? "ON" : (mine ? "OWNED" : (it.price > 0 ? std::to_string(it.price) + " COINS" : "FREE")), scx, y + 200, 44, room,
             equipped ? 0x6ED36E : kGold, 3);
      two("YOUR BINDER'S WHOLE COVER", y + 300, 40, 0xE8ECF5);
      two("X: SEE IT IN FRONT OF YOU", y + 400, 40, 0xB8C2D6);
      if (!mine && b.coins < it.price) P.text("NEED " + std::to_string(it.price - b.coins) + " MORE", scx, y + 490, 40, room, 0xFF8A7A, 2);
    } else if (b.page == kPageGrading) {  // r152: the slab under the cursor (its grade, cert, when) or the card (its copy, its cost)
      const int nS = static_cast<int>(b.gSlabs.size());
      if (b.gCursor < nS) {
        const auto& g = b.gSlabs[static_cast<size_t>(b.gCursor)];
        two(upper(g.c.name), y + 20, 48, 0xFFFFFF);
        two(ref_words(g.s.ref), y + 160, 40, kGold);
        if (g.s.left > 0) {
          P.text("AT TIZM GRADING", scx, y + 280, 40, room, 0xB8C2D6, 2);
          P.text("BACK IN " + std::to_string(g.s.left) + (g.s.left == 1 ? " MATCH" : " MATCHES"), scx, y + 340, 40, room, 0xB8C2D6, 2);
        } else {
          P.text(std::string(kGradeName[g.s.grade]) + " " + std::to_string(g.s.grade), scx, y + 280, 48, room, g.s.grade == 10 ? kGold : 0xFFFFFF, 3);
          P.text("CERT #" + std::to_string(g.s.cert), scx, y + 350, 40, room, 0xB8C2D6, 2);
          P.text(g.s.show ? "ON YOUR SHELF" : "X: ON YOUR SHELF", scx, y + 420, 40, room, g.s.show ? kGold : 0x8A94AA, 2);
        }
      } else if (b.gCursor - nS < static_cast<int>(b.gTiles.size())) {
        const auto& t = b.gTiles[static_cast<size_t>(b.gCursor - nS)];
        two(upper(t.c.name), y + 20, 48, 0xFFFFFF);
        two("SENDS: " + ref_words(t.r), y + 160, 40, kGold);
        P.text(std::to_string(t.cost) + " COINS", scx, y + 280, 48, room, b.coins >= t.cost ? kGold : 0xFF8A7A, 3);
        if (b.coins < t.cost) P.text("NEED " + std::to_string(t.cost - b.coins) + " MORE", scx, y + 350, 40, room, 0xFF8A7A, 2);
        two("10 GEM MINT: 6 IN 100", y + 450, 40, 0x8A94AA);
      }
    } else if (b.page == kPageSets) {
      char mul[32];
      std::snprintf(mul, sizeof(mul), "X%d.%02d", 1 + b.coinPct / 100, b.coinPct % 100);
      two("YOUR SET BONUSES", y + 20, 48, 0xFFFFFF);
      two(std::to_string(b.bonusChars) + " CHARACTERS: MORE XP", y + 160, 40, 0x6ED36E);
      P.text("LINES " + std::to_string(b.linesDone) + "  TYPES " + std::to_string(b.typesDone), scx, y + 270, 40, room, 0xE8ECF5, 2);
      P.text("FULL PAGES " + std::to_string(b.pagesFull), scx, y + 330, 40, room, 0xE8ECF5, 2);
      P.text(std::string("COINS ") + mul, scx, y + 400, 44, room, b.coinPct > 0 ? kGold : 0xB8C2D6, 3);
      P.text("GOD PACKS " + std::to_string(b.godPacks), scx, y + 470, 40, room, b.godPacks > 0 ? kGold : 0xE6B4FF, 2);
    } else if (b.shop) {
      two("YOUR BIT COINS", y + 20, 48, 0xFFFFFF);
      P.text(xrcoin::fit(b.coins, 9), scx, y + 200, 96, room, kGold, 5);  // (r208: commas; past 9 characters short)
      two("EARN THEM IN FRANCHISE MATCHES", y + 360, 40, 0xB8C2D6);
    } else if (cc != nullptr) {
      const Card& c = *cc;
      const Owned& o = *co;
      const bool own = o.total() > 0;
      two(own ? upper(c.name) : "???", y + 20, 48, 0xFFFFFF);
      P.text(std::string(c.set == 1 ? "JAM '96  -  " : "") + "#" + num3(c.num) + " - " + kRarityName[c.rarity], scx, y + 150, 40, room,
             c.rarity == kSuper ? 0xE6B4FF : (c.rarity == kRare ? kGold : 0xE8ECF5), 2);
      if (b.page == kPageTrade) {  // r149: on TRADE-INS only its BASE copies (nothing of its finishes / shinies)
        P.text("BASE X" + std::to_string(o.n[kBase][0]), scx, y + 220, 40, room, 0x6ED36E, 2);
        P.text(std::to_string(b.tiles[static_cast<size_t>(b.tradeCursor)].spare) + " SPARE", scx, y + 280, 40, room, 0x6ED36E, 2);
      } else {
        two(own ? have_text(o) : "NOT IN YOUR BINDER YET", y + 250, 40, own ? 0x6ED36E : 0x8A94AA);
      }
      if (own) {
        // (r160 round 2: on the binder its A chip has the column's foot - the level a little higher, its "N MORE" line left
        // out there, so nothing runs under the chip or past the box)
        const bool chip = b.page == kPageBinder;
        const int lv = b.curLevel;
        for (int s = 0; s < lv; ++s) star_shape(P, scx - (lv - 1) * 18.0f + s * 36.0f, y + (chip ? 316 : 350), 14, kGold);
        P.text("LV " + std::to_string(lv) + (lv > 1 ? ": +" + std::to_string(level_xp(lv)) + "% XP" : ""), scx, y + (chip ? 362 : 410), 40, room,
               lv > 1 ? kGold : 0xE8ECF5, 2);
        if (lv < 5 && !chip) P.text(std::to_string(kLevelCopies[lv] - o.total()) + " MORE: LV " + std::to_string(lv + 1), scx, y + 470, 40, room, 0xE8ECF5, 2);
      }
      if (b.page == kPageBinder && own && b.cursor < static_cast<int>(b.values.size()) && b.values[static_cast<size_t>(b.cursor)] > 0)  // r201
        P.text("MARKET " + xrcoin::text(b.values[static_cast<size_t>(b.cursor)]) + " COINS", scx, y + 200, 36, room, kGold, 2);
      if (false && b.page == kPageBinder && own) {  // A takes it out (r236: the bar's A only - no on-page copy)
        P.box(sx0 + 20, ty1 - 80, sx1 - 20, ty1 - 10, 35, 0x0B1020, 0.8f);
        const float gx = sx0 + 40;
        P.box(gx - 3, ty1 - 77, gx + 61, ty1 - 13, 32, 0xFFFFFF);
        P.box(gx, ty1 - 74, gx + 58, ty1 - 16, 29, 0x2EA043);
        P.text("A", gx + 29, ty1 - 45, 36, 50, 0xFFFFFF, 2);
        P.left("TAKE IT OUT", gx + 76, ty1 - 46, 40, sx1 - gx - 100, 0xFFFFFF, 2);
      }
    }
    // the packs (Y opens one: printed on them, as in the hint bar) - r236: on CARDS > PACKS only (its hero)
    if (false) {
      const float py0 = ty1 + 20, py1 = 1180;
      P.box(sx0, py0, sx1, py1, 28, b.packs > 0 ? 0x3A1F5A : 0x151C36, 1.0f, b.packs > 0 ? 0x1A0E2A : 0);
      if (b.packs > 0) {
        for (int i = std::min(2, b.packs - 1); i >= 0; --i)
          mini_pack(P, scx - 70 + i * 16 - 16, py0 + 26 + i * 8, scx + 70 + i * 16 - 16, py0 + 216 + i * 8, i > 0);
        P.text(std::to_string(b.packs) + (b.packs == 1 ? " PACK" : " PACKS"), scx, py0 + 280, 64, room, kGold, 4);
        P.box(sx0 + 20, py1 - 96, sx1 - 20, py1 - 20, 38, 0x2EA043);
        const float gx = sx0 + 40;
        P.box(gx - 3, py1 - 87, gx + 61, py1 - 23, 32, 0xFFFFFF);
        P.box(gx, py1 - 84, gx + 58, py1 - 26, 29, 0xC9A227);
        P.text("Y", gx + 29, py1 - 55, 36, 50, 0xFFFFFF, 2);
        P.left("OPEN ONE", gx + 76, py1 - 56, 44, sx1 - gx - 100, 0xFFFFFF, 3);
      } else {
        P.text("NO PACKS", scx, py0 + 80, 56, room, 0x8A94AA, 3);
        P.text("1 PACK A MATCH", scx, py0 + 190, 40, room, 0xE8ECF5, 2);
        P.text("+ A JAM '96 PACK A WIN", scx, py0 + 240, 40, room, 0xFF5AC8, 2);  // (r217)
        two("10 SPARES = 1 PACK", py0 + 330, 40, 0xB8C2D6);
      }
    }
    // r157 (menu overhaul): the page's A word and its extras only - the hub makes the one hint bar (MOVE, A, B BACK,
    // LT RT SECTIONS, these); a card out is a pop-up with its own bar
    if (b.mode == 2) {  // a card out in front of you
      out.popup = true;
      out.hints = {{"GRIP", "HOLD IT", 0x3A4A70}, {"LT RT", "TURN IT OVER", 0x3A4A70}, {"B", "BACK IN THE BINDER", 0xC0392B}};
      if (g_bdOutCard.load() >= 0)  // r163: out of your 3D binder: back into its pocket (B, or let go on it)
        out.hints = {{"GRIP", "HOLD IT", 0x3A4A70}, {"LT RT", "TURN IT OVER", 0x3A4A70}, {"B", "BACK IN ITS POCKET", 0xC0392B},
                     {"GRIP", "LET GO ON ITS POCKET", 0x3A4A70}};
    } else if (b.page == kPageTrade) {  // r146
      out.hints = {{"A", "TRADE " + std::to_string(trade_for(b.tradeSet)) + (b.tradeSet == 1 ? " JAM '96" : ""), 0x2EA043},
                   {"X", "KEEP / TRADE IT", 0x2E6FD0}};  // (r226)
      out.aDim = b.spareFree < trade_for(b.tradeSet);
    } else if (b.page == kPageSets) {  // r146 (r236: the BINDER's other view)
      out.hints = {{"A", "SELECT", 0x2EA043}, {"Y", "PAGES", 0xC9A227}};
      out.aDim = true;
    } else if (b.page == kPageGrading) {  // r152
      out.hints = {{"A", "SEND / LOOK", 0x2EA043}, {"X", "SLAB SHELF", 0x2E6FD0}};
      if (b.gArmed >= 0 && b.gArmed < static_cast<int>(g_set.size())) {  // r157: A, then A again within 4 s -> one confirm
        const int nS = static_cast<int>(b.gSlabs.size());
        const int ti = b.gCursor - nS;
        const BinderView::GTile* t = ti >= 0 && ti < static_cast<int>(b.gTiles.size()) ? &b.gTiles[static_cast<size_t>(ti)] : nullptr;
        out.confirm = "SEND " + upper(g_set[b.gArmed].name) + "?";
        out.confirmLine = t != nullptr ? ref_words(t->r) + "  -  " + std::to_string(t->cost) + " BIT COINS  -  BACK AFTER " +
                                             std::to_string(kGradeMatches) + " FRANCHISE MATCHES"
                                       : std::string("TO TIZM GRADING");
      }
    } else if (b.page == kPageExplosions || b.page == kPageTakeovers) {  // r157 (r236: GOAL FX / TAKEOVERS)
      const bool none = b.fxCursor >= static_cast<int>(b.fx.size());
      const BinderView::FxTile* t = none ? nullptr : &b.fx[static_cast<size_t>(b.fxCursor)];
      out.hints = {{"A", t == nullptr ? "PUT IT ON" : (t->owned ? "PUT IT ON" : "BUY IT"), 0x2EA043}};
      out.aDim = t != nullptr ? t->equipped : b.fxNone;
      if (t != nullptr) out.hints.push_back({"X", "PREVIEW", 0x2E6FD0});
      if (b.fxArmed >= 0 && b.fxArmed < static_cast<int>(b.fx.size())) {
        const BinderView::FxTile& a = b.fx[static_cast<size_t>(b.fxArmed)];
        out.confirm = "BUY " + upper(a.name) + "?";
        out.confirmLine = std::to_string(a.price) + " BIT COINS  -  YOU HAVE " + xrcoin::text(b.coins);
      }
    } else if (b.page == kPageLooks) {  // r168: a ball skin (r236: LOCKER > BALLS)
      const int i = b.lkBall;
      const bool mine = i < static_cast<int>(b.ballOwned.size()) && b.ballOwned[static_cast<size_t>(i)];
      out.hints = {{"A", mine ? "PUT IT ON" : "BUY", 0x2EA043}, {"X", "PREVIEW", 0x2E6FD0}};
      out.aDim = b.ballOn == i;
      if (b.lkArmKind == kLookBall && b.lkArm >= 0 && b.lkArm < kBallTiles) {
        const xrballs::Item& a = ball_item(b.lkArm);
        out.confirm = "BUY " + std::string(a.name) + "?";
        out.confirmLine = std::string("BALL SKIN") + "  -  " + std::to_string(a.price) + " BIT COINS  -  YOU HAVE " + xrcoin::text(b.coins);
      }
    } else if (b.page == kPageCovers) {  // r160 (r236: LOCKER > BINDERS)
      const int i = b.lkCol[0];
      const bool mine = i < static_cast<int>(b.lkOwned[0].size()) && b.lkOwned[0][static_cast<size_t>(i)];
      out.hints = {{"A", mine ? "PUT IT ON" : "BUY", 0x2EA043}, {"X", "PREVIEW", 0x2E6FD0}};
      out.aDim = b.skin == i;
      if (b.lkArmKind == kLookSkin && b.lkArm >= 0 && b.lkArm < look_count(b.lkArmKind)) {
        const LookItem& a = look_item(b.lkArmKind, b.lkArm);
        out.confirm = "BUY " + upper(a.name) + "?";
        out.confirmLine = std::string("BINDER SKIN") + "  -  " + std::to_string(a.price) +
                          " BIT COINS  -  YOU HAVE " + xrcoin::text(b.coins);
      }
    } else if (b.shop) {  // r140 (r236: CARDS > PACKS - A opens one on the hero, buys a product; X its box / one pack)
      const bool onOpen = b.packs > 0 && b.packsOnOpen;
      out.hints = {{"A", onOpen ? "OPEN" : "BUY", 0x2EA043}, {"X", b.pick % 2 == 1 ? "ONE PACK" : (b.pick / 2 == 2 ? "THE VAULT" : "BOX OF 5"), 0x2E6FD0}};
      if (!onOpen) {
        const int set = b.pick / 2;
        const bool box = b.pick % 2 == 1;
        const long long price = set == 2 ? (box ? xrexq::kBoxPrice : xrexq::kPackPrice) : set == 0 ? (box ? kBoxPrice : kPackPrice) : (box ? kJamBoxPrice : kJamPackPrice);
        out.aDim = b.coins < price;
      }
    } else {
      out.hints = {{"A", "LOOK", 0x2EA043}, {"Y", "SETS", 0xC9A227}};  // (r236: Y = SETS; packs open on PACKS)
      out.aDim = !(b.cursor < static_cast<int>(b.owned.size()) && b.owned[static_cast<size_t>(b.cursor)].total() > 0);
      // (r236: Y is SETS now - always there)
      out.hints.insert(out.hints.begin() + 1, {"X", b.binderOut ? "BINDER AWAY" : "BINDER OUT", 0x2E6FD0});  // r160: your 3D binder
      if (b.spreads > 1 && !b.binderOut) out.hints.insert(out.hints.begin() + 1, {"R", "TURN PAGE", 0x3A4A70});  // r171: the right stick
      if (b.binderOut) out.hints.push_back({"TRIGGER", "ON A CARD IN IT: TAKE IT", 0x3A4A70});  // r163
    }
  }
  if (b.mode == 1 || b.mode == 3 || b.mode == 4) out.popup = true;  // r157: the scenes keep their own bar
  {  // r157: into the hub's page area (right of its rail)
    const float ox = g_fitOx.load(), oy = g_fitOy.load(), k = g_fitK.load();
    if (k != 1.0f || ox != 0.0f || oy != 0.0f) {
      for (xrtext::BoardPiece& p : P.v) {
        const bool words = p.kind == xrtext::BoardPiece::kText || p.kind == xrtext::BoardPiece::kTextLeft ||
                           p.kind == xrtext::BoardPiece::kTextRight;
        p.x0 = ox + p.x0 * k;
        p.y0 = oy + p.y0 * k;
        if (words) {
          p.x1 *= k;  // (its room, its height)
          p.y1 *= k;
        } else {
          p.x1 = ox + p.x1 * k;
          p.y1 = oy + p.y1 * k;
        }
        p.radius *= k;
        p.outline *= k;
        p.shadow *= k;
        for (size_t i = 0; i + 1 < p.pts.size(); i += 2) {
          p.pts[i] = ox + p.pts[i] * k;
          p.pts[i + 1] = oy + p.pts[i + 1] * k;
        }
      }
    }
  }
  auto keep = std::make_shared<std::vector<ImgP>>(std::move(P.keep));
  out.pieces = std::move(P.v);
  out.keep = keep;
  return out;
}

int packs_waiting() { return g_packCount.load(); }
int generation() { return g_gen.load(); }
// r221: the frame thread's takeover-coin payments since the last call (the goal probe in xr_quest)
void take_coin_probe(long long* ns, int* pays) {
  *ns = g_coinPayNs.exchange(0);
  *pays = g_coinPays.exchange(0);
}

std::string magazine_pull() {
  if (!g_loaded.load()) return "";
  std::lock_guard<std::mutex> lock(g_mutex);
  const long long now = static_cast<long long>(std::time(nullptr));
  if (g_godPacks > 0 && now - g_godWhen <= 7LL * 24 * 3600) return "GOD PACK!";  // r149: the biggest brag there is
  for (auto it = g_pulls.rbegin(); it != g_pulls.rend(); ++it) {
    if (now - it->when > 7LL * 24 * 3600) break;
    if (big_pull(it->ref)) return pull_text(it->ref);
  }
  return "";
}
// r219: your big pulls of the last 7 days as the market's copies (STRIKERS ILLUSTRATED's market spread)
void magazine_pulls(std::vector<xrmarket::Variant>* out) {
  out->clear();
  if (!g_loaded.load()) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  const long long now = static_cast<long long>(std::time(nullptr));
  for (auto it = g_pulls.rbegin(); it != g_pulls.rend() && out->size() < 12; ++it) {
    if (now - it->when > 7LL * 24 * 3600) break;
    const Ref& r = it->ref;
    if (r.card < 0 || r.card >= static_cast<int>(g_set.size()) || (r.parallel == kBase && !r.shiny && r.hit == kHitNone)) continue;
    xrmarket::Variant v;
    v.card = r.card;
    v.parallel = r.parallel;
    v.shiny = r.shiny;
    v.hit = r.hit;
    v.serial = r.parallel == kBlack && !exq_card(r.card) ? 1 : r.serial;  // (r230: an Exquisite copy's tier + number)
    out->push_back(v);
  }
}

// r140: new packs rolled (packs earned, packs bought; `flags` what got them) and waiting; what's in them for the
// pictures to be made early. (g_mutex held)
static void add_packs(int n, unsigned flags, std::vector<std::pair<std::string, bool>>* prefetch) {
  for (int i = 0; i < n && g_packs.size() < 999; ++i) {
    uint64_t rng = g_salt ^ (static_cast<uint64_t>(g_serial + 1) * 0x9E3779B97F4A7C15ull);
    const bool god = mix64(rng) % kGodOdds == 0;  // r149: a GOD PACK
    const bool exq = (flags & kExqPack) != 0;     // r230: a LANCINI EXQUISITE pack (its own rates; no GOD PACK roll)
    Pack p = exq ? roll_exq_pack(rng, {}) : roll_pack(rng, {}, god, (flags & kJamPack) != 0 ? 1 : 0);  // r217: a JAM '96 pack's: its Jam cards
    p.serial = ++g_serial;
    // (r230: a VAULT case's packs: each its place in the case)
    p.flags = (flags & ~(7u << kExqBoxShift)) | (exq && (flags & kExqBox) != 0 ? static_cast<unsigned>(i % xrexq::kBoxPacks) << kExqBoxShift : 0u) |
              (!exq && god_cards(p) ? kGodPack : 0u);
    if (p.cards[0].card < 0) break;  // (no set on this headset)
    g_packs.push_back(p);
    for (const Ref& r : p.cards)
      if (r.card >= 0) prefetch->push_back({g_set[r.card].key, r.shiny});
  }
  g_packCount = static_cast<int>(g_packs.size());
}
// r226: the next `packs` waiting packs' cards, in the order they'll be opened and turned over - their big pictures asked for ahead (a
// session's start: the queue an earlier session made is gone with it; after a pack: the next one) (g_mutex held)
static void next_packs_art(int packs, std::vector<std::pair<std::string, bool>>* out) {
  for (size_t p = 0; p < g_packs.size() && p < static_cast<size_t>(packs); ++p)
    for (const Ref& r : g_packs[p].cards)
      if (r.card >= 0) out->push_back({g_set[r.card].key, r.shiny && pk_key(g_set[r.card].key)});
}

// r140: coins landing: a few bright pings of a coin dropping on coins, `k` of them (made here, like the chimes)
static void coin_sound(int k) {
  constexpr int kRate = 32000;
  std::vector<short> pcm(static_cast<size_t>((0.09f * k + 0.5f) * kRate));
  for (int c = 0; c < k; ++c) {
    const float at = 0.09f * c, f0 = 3200.0f + 380.0f * ((c * 7) % 3), f1 = f0 * 1.51f;
    for (size_t s = static_cast<size_t>(at * kRate); s < pcm.size(); ++s) {
      const float t = static_cast<float>(s) / kRate - at;
      const float env = std::min(1.0f, t / 0.002f) * std::exp(-t / 0.11f);
      const float v = std::sin(6.2831853f * f0 * t) * 0.6f + std::sin(6.2831853f * f1 * t) * 0.3f * std::exp(-t / 0.05f);
      pcm[s] = static_cast<short>(clampf(pcm[s] + v * env * 6500.0f, -32000.0f, 32000.0f));
    }
  }
  PortCardSound(pcm.data(), static_cast<int>(pcm.size()), 0.8f);
}

// r149 (Lance: "change the noise that plays anytime you get a nice card, it sounds so basic while i know it was meant to
// sound mighty. give each tier its own sound"): the reveals' own sounds, made on the PC (tools/outside/card_sounds149.py:
// synthesised, layered, mastered) into STRIKERS_HOME/franchise/cardsounds/<name>.wav - 16-bit PCM, 32 kHz mono - read once
static const std::vector<short>& card_wav(const std::string& name) {
  static std::mutex m;
  static std::unordered_map<std::string, std::vector<short>> cache;
  std::lock_guard<std::mutex> lock(m);
  auto it = cache.find(name);
  if (it != cache.end()) return it->second;
  std::vector<short>& out = cache[name];
  FILE* f = std::fopen((home_dir() + "/franchise/cardsounds/" + name + ".wav").c_str(), "rb");
  if (f == nullptr) return out;
  std::vector<uint8_t> d;
  uint8_t buf[65536];
  size_t got;
  while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + got);
  std::fclose(f);
  auto u32 = [&](size_t at) { return at + 4 <= d.size() ? static_cast<uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 | d[at + 3] << 24) : 0u; };
  auto u16 = [&](size_t at) { return at + 2 <= d.size() ? static_cast<uint32_t>(d[at] | d[at + 1] << 8) : 0u; };
  if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return out;
  int channels = 1, bits = 16;
  for (size_t at = 12; at + 8 <= d.size();) {
    const uint32_t len = u32(at + 4);
    if (std::memcmp(d.data() + at, "fmt ", 4) == 0) {
      channels = static_cast<int>(u16(at + 10));
      bits = static_cast<int>(u16(at + 22));
    } else if (std::memcmp(d.data() + at, "data", 4) == 0 && bits == 16 && channels >= 1) {
      const size_t frames = std::min<size_t>(len, d.size() - at - 8) / (2 * static_cast<size_t>(channels));
      out.resize(frames);
      for (size_t i = 0; i < frames; ++i) out[i] = static_cast<short>(u16(at + 8 + i * 2 * channels));
      break;
    }
    at += 8 + len + (len & 1);
  }
  CLOG("card sound %s: %d frames", name.c_str(), static_cast<int>(out.size()));
  return out;
}
static bool play_wav(const std::string& name, float gain) {
  const std::vector<short>& pcm = card_wav(name);
  if (pcm.empty()) return false;
  PortCardSound(pcm.data(), static_cast<int>(pcm.size()), gain);
  return true;
}
// read them all on a thread the first time a pack comes up (none read in the middle of a reveal)
static void card_sounds_preload() {
  static std::atomic<bool> once{false};
  if (once.exchange(true)) return;
  std::thread([]() {
    for (const char* n : {"base", "rare", "silver", "gold", "boom", "hometown", "signed", "black", "shiny", "godpack", "godtell",
                          "casino_coins_pour_tray", "casino_coins_jingle"})  // (r219: the big pulls' coin showers)
      card_wav(n);
  }).detach();
}

// r147: a card's level up as it turns over: three quick rising bell notes (C6 E6 G6)
static void level_sound() {
  constexpr int kRate = 32000;
  static const float kNote[3] = {1046.5f, 1318.5f, 1568.0f};
  std::vector<short> pcm(static_cast<size_t>(0.8f * kRate));
  for (int k = 0; k < 3; ++k) {
    const float at = 0.085f * k, f = kNote[k];
    for (size_t s = static_cast<size_t>(at * kRate); s < pcm.size(); ++s) {
      const float t = static_cast<float>(s) / kRate - at;
      const float env = std::min(1.0f, t / 0.003f) * std::exp(-t / 0.18f);
      const float v = std::sin(6.2831853f * f * t) * 0.55f + std::sin(6.2831853f * 2.0f * f * t) * 0.18f * std::exp(-t / 0.06f);
      pcm[s] = static_cast<short>(clampf(pcm[s] + v * env * 5200.0f, -32000.0f, 32000.0f));
    }
  }
  PortCardSound(pcm.data(), static_cast<int>(pcm.size()), 0.7f);
}

long long coins() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_coins;
}
// r208: every change to the balance (g_mutex held): held at xrcoin::kMax (never wrapping), the ever-earned / -spent totals too
static void coins_add(long long c) {
  g_coins = xrcoin::clamp(xrcoin::add(g_coins, c));
  g_coinsEarned = xrcoin::add(g_coinsEarned, c);
}
static void coins_take(long long c) {
  g_coins = xrcoin::clamp(g_coins - c);
  g_coinsSpent = xrcoin::add(g_coinsSpent, c);
}
// r153: GOAL EXPLOSIONS' wallet (xr_goalfx.cpp buys with it): your TIZM COINS; a spend taken and saved (collection.txt,
// written whole: a temp file, then renamed), false when there aren't enough (nothing taken)
static long long goalfx_balance() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_coins;
}
static bool goalfx_spend(long long coins, const char* what) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (coins < 0 || g_coins < coins) return false;
    coins_take(coins);
    save_collection();
    CLOG("GOAL EXPLOSION %s bought for %lld BIT coins: %lld left", what != nullptr ? what : "?", coins, g_coins);
  }
  ++g_gen;
  coin_sound(2);
  return true;
}
// r160: a binder skin / card sleeve bought with the loaded career's TIZM COINS (the same wallet as the explosions: taken
// and saved in its collection file), then yours headset-wide (looks.txt). 0 bought, 1 not enough coins, 2 yours already
static int looks_buy(int kind, int i) {
  if (look_owned(kind, i)) return 2;
  const LookItem& it = look_item(kind, i);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_careerSeed == 0 || g_coins < it.price) return 1;
    coins_take(it.price);
    save_collection();
    CLOG("LOOKS: %s bought for %d BIT coins: %lld left", it.id, it.price, g_coins);
  }
  look_add_owned(kind, i);
  ++g_gen;
  coin_sound(2);
  return 0;
}
// r168: a ball skin, the same way (the loaded career's TIZM COINS, then yours headset-wide in looks.txt): 0 bought, 1 not
// enough coins, 2 yours already
static int balls_buy(int i) {
  if (ball_owned(i)) return 2;
  const xrballs::Item& it = ball_item(i);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_careerSeed == 0 || g_coins < it.price) return 1;
    coins_take(it.price);
    save_collection();
    CLOG("LOOKS: ball %s bought for %d BIT coins: %lld left", it.id, it.price, g_coins);
  }
  ball_add_owned(i);
  ++g_gen;
  coin_sound(2);
  return 0;
}
// r199: TIZM SPORTSBOOK's wallet (xr_book.cpp): the loaded binder's coins, only for the career it belongs to
unsigned wallet_career() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_careerSeed;
}
bool spend_coins(unsigned career, long long coins, const char* why) {  // r208: 64-bit
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (career == 0 || career != g_careerSeed || coins <= 0 || g_coins < coins) return false;
    coins_take(coins);
    save_collection();
    CLOG("SPORTSBOOK: %lld BIT coins staked (%s): %lld left", coins, why != nullptr ? why : "?", g_coins);
  }
  ++g_gen;
  return true;
}
bool add_coins(unsigned career, long long coins, const char* why) {  // r208: 64-bit, held at kMax (never wrapping)
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (career == 0 || career != g_careerSeed || coins <= 0) return false;
    const long long was = g_coins;
    coins_add(coins);
    save_collection();
    if (was + coins > xrcoin::kMax)
      CLOG("SPORTSBOOK: +%lld BIT coins (%s): %lld now - THE MOST A BINDER HOLDS (%lld over it not kept)", coins, why != nullptr ? why : "?",
           g_coins, was + coins - xrcoin::kMax);
    else
      CLOG("SPORTSBOOK: +%lld BIT coins (%s): %lld now", coins, why != nullptr ? why : "?", g_coins);
  }
  ++g_gen;
  return true;
}
bool play_sound(const char* name, float gain) { return name != nullptr && play_wav(name, gain); }
// r202: the casino's win / lose music (real CC0 clips) at the SOUND > EFFECTS volume (the game's SFX group)
bool play_music(const char* name, float gain) {
  if (name == nullptr) return false;
  const std::vector<short>& pcm = card_wav(name);
  if (pcm.empty()) return false;
  PortCardSoundSfx(pcm.data(), static_cast<int>(pcm.size()), gain);
  return true;
}

// ---- r201: the TIZM CARD MARKET + the CARD MUSEUM (xr_market.cpp / xr_museum.cpp): your copies as they see them, a bought copy
// into the binder, a sold one out of it, the set, the pictures. Never called with g_mutex held from their side's view: they call
// in here (their lock -> g_mutex), xr_cards never calls them while holding g_mutex.
std::vector<xrmarket::CardInfo> g_mkSet;  // (built once on the game thread: the character lines come from franchise.cpp)
void mk_catalogue(std::vector<xrmarket::CardInfo>* out) { *out = g_mkSet; }
void mk_holdings(std::vector<xrmarket::Holding>* out) {
  out->clear();
  std::lock_guard<std::mutex> lock(g_mutex);
  auto add = [&](int card, int par, bool shiny, int hit, int serial, int grade, uint32_t cert, int count, bool grading) {
    if (count <= 0) return;
    xrmarket::Holding h;
    h.card = card;
    h.parallel = par;
    h.shiny = shiny;
    h.hit = hit;
    h.serial = serial;
    h.grade = grade;
    h.cert = cert;
    h.count = count;
    h.grading = grading;
    out->push_back(h);
  };
  for (size_t i = 0; i < g_owned.size(); ++i) {
    const Owned& o = g_owned[i];
    const int c = static_cast<int>(i);
    for (int s = 0; s < 2; ++s) {
      add(c, kBase, s != 0, kHitNone, 0, 0, 0, o.n[kBase][s], false);
      add(c, kSilver, s != 0, kHitNone, 0, 0, 0, o.n[kSilver][s], false);
      add(c, kBlack, s != 0, kHitNone, 1, 0, 0, o.n[kBlack][s], false);
    }
    // a GOLD: each number its own copy (the shiny ones the last numbers: the collection keeps no tie between them)
    const int gn = o.n[kGoldTizm][0], gs = o.n[kGoldTizm][1];
    for (int k = 0; k < gn + gs; ++k)
      add(c, kGoldTizm, k >= gn, kHitNone, k < static_cast<int>(o.golds.size()) ? o.golds[static_cast<size_t>(k)] : 0, 0, 0, 1, false);
    for (const Ref& h : o.hits) add(c, h.parallel, h.shiny, h.hit, h.serial, 0, 0, 1, false);
  }
  for (const Slab& s : g_slabs)
    add(s.ref.card, s.ref.parallel, s.ref.shiny, s.ref.hit, s.ref.serial, s.grade, s.cert, 1, s.left > 0);
}
Ref mk_ref(const xrmarket::Variant& v) {
  Ref r;
  r.card = v.card;
  r.parallel = std::max(0, std::min(3, v.parallel));
  r.shiny = v.shiny;
  r.serial = v.serial;
  r.hit = std::max(0, std::min(3, v.hit));
  return r;
}
void mk_changed() {  // (g_mutex held) the binder changed: saved, the bonuses, the hub's picture
  save_collection();
  bonuses_update();
  ++g_gen;
}
bool mk_give(const xrmarket::Variant& v) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_careerSeed == 0 || v.card < 0 || v.card >= static_cast<int>(g_set.size())) return false;
  if (v.grade > 0) {
    Slab s;
    s.ref = mk_ref(v);
    s.grade = std::max(1, std::min(10, v.grade));
    s.cert = v.cert;
    s.left = 0;
    s.seen = true;
    s.when = static_cast<long long>(std::time(nullptr));
    g_slabs.push_back(s);
    slabs_sync();
  } else {
    if (v.parallel == kGoldTizm) {
      for (int g : g_owned[static_cast<size_t>(v.card)].golds)
        if (g == v.serial) return false;  // (that number is yours already)
    }
    if (v.parallel == kBlack && g_owned[static_cast<size_t>(v.card)].n[kBlack][0] + g_owned[static_cast<size_t>(v.card)].n[kBlack][1] > 0) return false;
    if (g_set[static_cast<size_t>(v.card)].set == 2 && (!xrexq::valid(v.serial) || exq_taken(v.card, v.serial, {})))  // r230: an EXQUISITE number is one copy
      return false;
    add_owned(mk_ref(v), +1);
  }
  g_newCards.insert(g_set[static_cast<size_t>(v.card)].id);
  mk_changed();
  CLOG("TIZM MARKET: bought %s (finish %d, hit %d, grade %d)", g_set[static_cast<size_t>(v.card)].key.c_str(), v.parallel, v.hit, v.grade);
  return true;
}
bool mk_take(const xrmarket::Variant& v) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_careerSeed == 0 || v.card < 0 || v.card >= static_cast<int>(g_owned.size())) return false;
  Owned& o = g_owned[static_cast<size_t>(v.card)];
  if (v.grade > 0) {
    for (auto it = g_slabs.begin(); it != g_slabs.end(); ++it)
      if (it->ref.card == v.card && it->cert == v.cert && it->left == 0) {
        g_slabs.erase(it);
        slabs_sync();
        mk_changed();
        return true;
      }
    return false;
  }
  if (v.hit != kHitNone || g_set[static_cast<size_t>(v.card)].set == 2) {  // (r230: every EXQUISITE copy kept whole)
    bool have = false;
    for (const Ref& h : o.hits) have = have || (h.hit == v.hit && h.parallel == v.parallel && h.shiny == v.shiny && h.serial == v.serial);
    if (!have) return false;
  } else if (o.n[std::max(0, std::min(3, v.parallel))][v.shiny ? 1 : 0] <= 0) {
    return false;
  }
  add_owned(mk_ref(v), -1);
  mk_changed();
  CLOG("TIZM MARKET: sold %s (finish %d, hit %d)", g_set[static_cast<size_t>(v.card)].key.c_str(), v.parallel, v.hit);
  return true;
}
long long mk_balance() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_coins;
}
std::shared_ptr<void> mk_portrait(int card, const uint8_t** px, int* w, int* h) {
  std::string key;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (card < 0 || card >= static_cast<int>(g_set.size())) return nullptr;
    key = g_set[static_cast<size_t>(card)].key;
  }
  ImgP p = portrait(key);
  if (!p || p->empty()) return nullptr;
  *px = p->rgba.data();
  *w = p->w;
  *h = p->h;
  return std::const_pointer_cast<void>(std::static_pointer_cast<const void>(p));
}
std::shared_ptr<void> mk_thumb(int kind, const uint8_t** px, int* w, int* h) {
  static std::mutex m;
  static ImgP cache[xrmuseum::kKinds];
  std::lock_guard<std::mutex> lock(m);
  if (kind < 0 || kind >= xrmuseum::kKinds) return nullptr;
  if (!cache[kind]) {
    auto img = std::make_shared<Img>();
    int iw = 0, ih = 0, n = 0;
    unsigned char* d = stbi_load((home_dir() + "/franchise/museum/thumb_" + std::to_string(kind) + ".png").c_str(), &iw, &ih, &n, 4);
    if (d != nullptr) {
      img->rgba.assign(d, d + static_cast<size_t>(iw) * ih * 4);
      img->w = iw;
      img->h = ih;
      stbi_image_free(d);
    }
    cache[kind] = img;
  }
  if (cache[kind]->empty()) return nullptr;
  *px = cache[kind]->rgba.data();
  *w = cache[kind]->w;
  *h = cache[kind]->h;
  return std::const_pointer_cast<void>(std::static_pointer_cast<const void>(cache[kind]));
}
// r235 (COLLECTOR-TRADES.md 11): a CPU collector rips a pack - THIS roller (roll_pack / roll_exq_pack, add_packs' own GOD PACK roll): the same
// odds as yours, one 1/1 per card per set ever (yours, a pack waiting, the collector world's list - told after every rip), every EXQUISITE
// number once. `set` 0 LANCINI TIZM, 1 JAM '96, 2 EXQUISITE. Nothing of yours changes (the pack is never in g_packs). (the trades module's
// lock -> g_mutex -> g_worldM, as black_mine)
bool trade_rip(int set, uint64_t seed, std::vector<xrmarket::Variant>* out) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_careerSeed == 0 || g_set.empty() || out == nullptr) return false;
  uint64_t rng = seed;
  Pack p;
  if (set == 2) {
    p = roll_exq_pack(rng, {});
  } else {
    const bool god = mix64(rng) % kGodOdds == 0;
    p = roll_pack(rng, {}, god, set == 1 ? 1 : 0);
  }
  for (const Ref& r : p.cards) {
    if (r.card < 0) continue;
    xrmarket::Variant v;
    v.card = r.card;
    v.parallel = r.parallel;
    v.shiny = r.shiny;
    v.hit = r.hit;
    v.serial = r.serial;
    out->push_back(v);
  }
  return true;
}
void mk_install() {  // (game thread, the set loaded: once)
  static bool s_done = false;
  if (s_done || !g_loaded.load()) return;
  s_done = true;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    // r231: each card's place in its FAMILY (evolutions.txt: an evolution line, a hero's suits, an ORIGINAL's uniforms - one player for the
    // market): its depth from the first form, the family's deepest + 1 (a branch's forms each their own depth) - the market's stage premium
    std::unordered_map<std::string, std::string> evoPrev;
    if (FILE* f = std::fopen((home_dir() + "/franchise/evolutions.txt").c_str(), "r")) {
      char a[128], b[128];
      int lv = 0;
      while (std::fscanf(f, "%127s %127s %d", a, b, &lv) == 3) evoPrev[b] = a;
      std::fclose(f);
    }
    auto depth_of = [&evoPrev](const std::string& key, std::string* root) {
      int d = 0;
      std::string r = key;
      for (auto it = evoPrev.find(r); it != evoPrev.end() && d < 8; it = evoPrev.find(r)) r = it->second, ++d;
      *root = r;
      return d;
    };
    std::unordered_map<std::string, int> famDeep;
    for (const Card& c : g_set) {
      std::string r;
      const int d = depth_of(c.key, &r);
      famDeep[r] = std::max(famDeep[r], d);
    }
    for (const Card& c : g_set) {
      xrmarket::CardInfo k;
      {
        std::string r;
        k.stage = depth_of(c.key, &r);
        k.stages = famDeep[r] + 1;
      }
      k.key = c.id;  // (r217: a Jam card its own)
      k.stat = c.stat;
      k.name = c.name;
      char line[64];
      PortFranchiseMarketLine(c.key.c_str(), line, sizeof(line));
      k.line = line;
      k.rarity = c.rarity;
      k.series = c.series;
      k.rating = c.rating;
      k.here = c.here;
      k.c1 = c.c1;
      k.c2 = c.c2;
      // r217: its set (a JAM '96 card: "JAM '96 <name>" on the market / the wire / the collectors; its LANCINI TIZM twin its base)
      k.set = c.set;
      k.base = c.set >= 1 ? c.twin : static_cast<int>(g_mkSet.size());
      if (c.set == 1) {
        k.name = "JAM '96 " + c.name;
        k.stat.clear();  // (franchise rows map to the main card: card_of_key's stat lookup keeps the first)
      }
      if (c.set == 2) {  // r230: a LANCINI EXQUISITE card (its checklist number: the "jersey number" premium)
        k.name = "EXQUISITE " + c.name;
        k.stat.clear();
        k.no = c.num;
      }
      g_mkSet.push_back(k);
    }
  }
  xrmarket::Source s;
  s.career = &wallet_career;
  s.balance = &mk_balance;
  s.spend = &spend_coins;
  s.add = &add_coins;
  s.sound = &play_sound;
  s.catalogue = &mk_catalogue;
  s.holdings = &mk_holdings;
  s.give = &mk_give;
  s.take = &mk_take;
  s.portrait = &mk_portrait;
  s.listed = [](const std::vector<xrmarket::Variant>& v) {  // r210: listed copies stay out of trade-ins + grading
    {
      std::lock_guard<std::mutex> lock(g_listedM);
      g_listedV = v;
    }
    ++g_gen;
  };
  s.sold = [](const xrmarket::Variant& v, const char* who) {  // r213: a 1/1 sold on the market stays in the collector world
    if (v.parallel != kBlack || v.card < 0) return;
    {
      std::lock_guard<std::mutex> lock(g_mutex);  // (the market's lock -> g_mutex, as give / take)
      if (v.card >= static_cast<int>(g_set.size())) return;
      world_black(v.card);  // (this career's list read first if it wasn't yet)
      std::vector<std::string> keys;
      {
        std::lock_guard<std::mutex> wl(g_worldM);
        keys = g_worldKeys;
      }
      keys.push_back(g_set[static_cast<size_t>(v.card)].id);  // (the pack roller knows at once; the trades module's own list follows)
      world_set(g_careerSeed, keys);
    }
    (void)who;  // (r235: the copy itself goes to the collector world through sold_for - its buyer, his price)
  };
  // r235: any copy of yours sold (SELL NOW / an offer / your listing): a named collector's buy into his binder (queued: the market's lock held)
  s.sold_for = [](const xrmarket::Variant& v, const char* who, long long coins) { xrtrade::world_sold(v, who, coins); };
  // r235: the SEARCH buys a collector's / the dealer's case's copy through the trades module (no market lock held: xr_market input)
  s.buy_offering = [](uint32_t id, long long ask, std::string* line) { return xrtrade::buy_listing(id, ask, line); };
  // r214: the wire never sells a 1/1 that's taken in this career - yours (binder, slab, a pack waiting) or a collector's (the world
  // keys COLLECTOR TRADES keeps / its world11 file). The market's lock -> g_mutex -> g_worldM (a leaf), as `sold`; nothing called back.
  s.black_taken = [](int card) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (card < 0 || card >= static_cast<int>(g_owned.size()) || card >= static_cast<int>(g_set.size())) return false;
    const Owned& o = g_owned[static_cast<size_t>(card)];
    if (o.n[kBlack][0] + o.n[kBlack][1] > 0 || o.blackAny()) return true;
    for (const Ref& r : o.sealed)
      if (r.parallel == kBlack) return true;
    for (const Slab& sl : g_slabs)
      if (sl.ref.card == card && sl.ref.parallel == kBlack) return true;
    for (const Pack& p : g_packs)
      for (const Ref& q : p.cards)
        if (q.card == card && q.parallel == kBlack) return true;
    return world_black(card);
  };
  // r230: an EXQUISITE number taken in this career (yours, a pack waiting, a collector's): the market lists / sells another number. The market's
  // lock -> g_mutex -> g_worldM, as black_taken
  s.serial_taken = [](int card, int serial) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return exq_taken(card, serial, {});
  };
  // r220: a JAM '96 copy's die for its minis (the market, the museum's labels, the magazine, the collectors' trades)
  s.cut_sil = [](const xrmarket::Variant& v, const uint8_t** px, int* w, int* h) -> std::shared_ptr<void> {
    Ref r;
    r.parallel = v.parallel, r.shiny = v.shiny, r.hit = v.hit;
    ImgP p = lk::cut_small(lk::jam_shape(r), 0.0f, 108, 151, 0xFFFFFF, false);
    if (!p || p->empty()) return nullptr;
    *px = p->rgba.data();
    *w = p->w;
    *h = p->h;
    return std::const_pointer_cast<void>(std::static_pointer_cast<const void>(p));
  };
  xrmarket::set_source(s);
  {  // r213: COLLECTOR TRADES - the same binder, wallet and set as the market's; the collectors' 1/1s to the pack roller
    xrtrade::Source t;
    t.career = &wallet_career;
    t.balance = &mk_balance;
    t.spend = &spend_coins;
    t.add = &add_coins;
    t.sound = &play_sound;
    t.catalogue = &mk_catalogue;
    t.holdings = &mk_holdings;
    t.give = &mk_give;
    t.take = &mk_take;
    t.black_mine = [](int card) {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (card < 0 || card >= static_cast<int>(g_owned.size())) return false;
      const Owned& o = g_owned[static_cast<size_t>(card)];
      if (o.n[kBlack][0] + o.n[kBlack][1] > 0 || o.blackAny()) return true;
      for (const Ref& s : o.sealed)
        if (s.parallel == kBlack) return true;
      for (const Slab& sl : g_slabs)
        if (sl.ref.card == card && sl.ref.parallel == kBlack) return true;
      for (const Pack& p : g_packs)
        for (const Ref& q : p.cards)
          if (q.card == card && q.parallel == kBlack) return true;
      return false;
    };
    t.serial_mine = [](int card, int serial) {  // r230: an EXQUISITE number of yours (binder, slab, a pack waiting) - a collector's never repeats it
      std::lock_guard<std::mutex> lock(g_mutex);
      return exq_taken(card, serial, {});
    };
    t.world_blacks = [](unsigned career, const std::vector<std::string>& keys) {
      world_set(career, keys);
      CLOG("COLLECTOR TRADES: %d BLACK 1/1s in the collectors' binders (packs give a GOLD for those)", static_cast<int>(keys.size()));
    };
    t.log = [](const char* line) { CLOG("%s", line); };
    t.rip = &trade_rip;  // r235: the collectors rip packs with this roller
    xrtrade::set_source(t);
  }
  xrmuseum::Wallet w;
  w.career = &wallet_career;
  w.balance = &mk_balance;
  w.spend = &spend_coins;
  w.sound = &play_sound;
  xrmuseum::set_wallet(w);
  xrmuseum::set_thumbs(&mk_thumb);
  CLOG("TIZM MARKET + CARD MUSEUM: %d cards priced", static_cast<int>(g_mkSet.size()));
}
// r201: a box-filtered copy of an RGBA picture at w x h into `dst` (its stride dw) at (x, y)
void mk_blit(const std::vector<uint8_t>& src, int sw, int sh, uint8_t* dst, int dw, int dh, int x, int y, int w, int h) {
  if (src.size() < static_cast<size_t>(sw) * sh * 4 || w <= 0 || h <= 0) return;
  for (int j = 0; j < h; ++j) {
    const int ty = y + j;
    if (ty < 0 || ty >= dh) continue;
    const int sy0 = j * sh / h, sy1 = std::max(sy0 + 1, (j + 1) * sh / h);
    for (int i = 0; i < w; ++i) {
      const int tx = x + i;
      if (tx < 0 || tx >= dw) continue;
      const int sx0 = i * sw / w, sx1 = std::max(sx0 + 1, (i + 1) * sw / w);
      unsigned acc[4] = {0, 0, 0, 0}, n = 0;
      for (int yy = sy0; yy < sy1; yy += std::max(1, (sy1 - sy0) / 3))
        for (int xx = sx0; xx < sx1; xx += std::max(1, (sx1 - sx0) / 3)) {
          const uint8_t* p = &src[(static_cast<size_t>(yy) * sw + xx) * 4];
          for (int k = 0; k < 4; ++k) acc[k] += p[k];
          ++n;
        }
      uint8_t* d = &dst[(static_cast<size_t>(ty) * dw + tx) * 4];
      for (int k = 0; k < 4; ++k) d[k] = static_cast<uint8_t>(acc[k] / std::max(1u, n));
      // r217: its cut-outs stay cut (a JAM '96 card's die-cut crest, every card's round corners: the museum's shader drops alpha
      // under 0.3); its foil's alpha (128-255) solid as before
      d[3] = d[3] < 96 ? 0 : 255;
    }
  }
}
// r210: a straight-RGBA sRGB picture's whole mip chain (levels 1..n, each half the last, down to 1 x 1), made on a worker: each texel
// the 2 x 2 above it averaged in LINEAR light (sRGB decoded, averaged, encoded - the GPU's blit did this too) weighted by alpha (a clear
// texel's colour never bleeds into a card's edge), alpha averaged plainly. Copied up as it is (xrfx::Model::texMips: no GPU work).
std::vector<std::vector<uint8_t>> mip_chain_srgb(const std::vector<uint8_t>& rgba, int w, int h) {
  struct Tables {  // (a function-local static: built once, thread-safe)
    float lin[256];
    uint8_t enc[4097];
    Tables() {
      for (int i = 0; i < 256; ++i) {
        const float c = i / 255.0f;
        lin[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
      }
      for (int i = 0; i <= 4096; ++i) {
        const float l = i / 4096.0f;
        const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
        enc[i] = static_cast<uint8_t>(std::lround(std::min(1.0f, std::max(0.0f, c)) * 255.0f));
      }
    }
  };
  static const Tables s_t;
  const float* s_lin = s_t.lin;
  const uint8_t* s_enc = s_t.enc;
  std::vector<std::vector<uint8_t>> out;
  const std::vector<uint8_t>* src = &rgba;
  int sw = w, sh = h;
  while (sw > 1 || sh > 1) {
    const int dw = std::max(1, sw / 2), dh = std::max(1, sh / 2);
    std::vector<uint8_t> d(static_cast<size_t>(dw) * dh * 4);
    for (int y = 0; y < dh; ++y)
      for (int x = 0; x < dw; ++x) {
        float acc[3] = {0, 0, 0}, wsum = 0, asum = 0, csum[3] = {0, 0, 0};
        int n = 0;
        for (int j = 0; j < 2; ++j)
          for (int i = 0; i < 2; ++i) {
            const int sx = std::min(sw - 1, 2 * x + i), sy = std::min(sh - 1, 2 * y + j);
            const uint8_t* p = &(*src)[(static_cast<size_t>(sy) * sw + sx) * 4];
            const float a = p[3] / 255.0f;
            for (int c = 0; c < 3; ++c) acc[c] += s_lin[p[c]] * a, csum[c] += s_lin[p[c]];
            wsum += a;
            asum += p[3];
            ++n;
          }
        uint8_t* q = &d[(static_cast<size_t>(y) * dw + x) * 4];
        for (int c = 0; c < 3; ++c) {
          const float l = wsum > 1e-4f ? acc[c] / wsum : csum[c] / n;  // (all clear: the plain average keeps the velvet under it)
          q[c] = s_enc[static_cast<int>(std::lround(std::min(1.0f, std::max(0.0f, l)) * 4096.0f))];
        }
        q[3] = static_cast<uint8_t>(std::lround(asum / n));
      }
    out.push_back(std::move(d));
    src = &out.back();
    sw = dw;
    sh = dh;
  }
  return out;
}
// r226 (Lance's JAM '96 OMASTAR at arm's length: the LANCINI TIZM plate / small words "garbled"): a card front's mips for the cards in your
// hands - mip_chain_srgb's levels (each from the one above, as it makes them: nothing compounds), then each level's colour given a light
// 3x3 unsharp (kFrontMipSharp, in linear light, premultiplied: never past its own alpha) - the GPU's 2x2 box chain is soft at the sizes a
// card is seen at in the hand (measured, logs\r226-cards\legibility: the plate / stats at a card 180-400 px wide on screen, error vs a clean
// shrink of the same front 10-14% lower, edge contrast closer to it, no overshoot). Level 0 (the card up close) and every level's alpha
// (the foil, the die-cut) exactly as before. Worker threads only (a front ~1.4 ms a megapixel more).
constexpr float kFrontMipSharp = 0.5f;
std::vector<std::vector<uint8_t>> mip_chain_front(const std::vector<uint8_t>& rgba, int w, int h) {
  struct Tables {
    float lin[256];
    uint8_t enc[4097];
    Tables() {
      for (int i = 0; i < 256; ++i) {
        const float c = i / 255.0f;
        lin[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
      }
      for (int i = 0; i <= 4096; ++i) {
        const float l = i / 4096.0f;
        const float c = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
        enc[i] = static_cast<uint8_t>(std::lround(std::min(1.0f, std::max(0.0f, c)) * 255.0f));
      }
    }
  };
  static const Tables s_t;
  std::vector<std::vector<uint8_t>> lv = mip_chain_srgb(rgba, w, h);
  int lw = w, lh = h;
  for (std::vector<uint8_t>& d : lv) {
    lw = std::max(1, lw / 2), lh = std::max(1, lh / 2);
    if (lw < 3 || lh < 3 || d.size() < static_cast<size_t>(lw) * lh * 4) continue;
    const size_t n = static_cast<size_t>(lw) * lh;
    std::vector<float> P(n * 3);
    for (size_t i = 0; i < n; ++i) {
      const float a = d[i * 4 + 3] / 255.0f;
      for (int c = 0; c < 3; ++c) P[i * 3 + c] = s_t.lin[d[i * 4 + c]] * a;
    }
    for (int y = 0; y < lh; ++y)
      for (int x = 0; x < lw; ++x) {
        const size_t i = static_cast<size_t>(y) * lw + x;
        const uint8_t ab = d[i * 4 + 3];
        if (ab == 0) continue;  // (clear: its colour as mip_chain_srgb left it)
        const float a = ab / 255.0f;
        float b[3] = {0, 0, 0};
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            const size_t k = static_cast<size_t>(std::min(lh - 1, std::max(0, y + dy))) * lw + std::min(lw - 1, std::max(0, x + dx));
            for (int c = 0; c < 3; ++c) b[c] += P[k * 3 + c];
          }
        for (int c = 0; c < 3; ++c) {
          const float p = P[i * 3 + c], v = std::min(a, std::max(0.0f, p + kFrontMipSharp * (p - b[c] / 9.0f)));
          d[i * 4 + c] = s_t.enc[static_cast<int>(std::lround(std::min(1.0f, v / a) * 4096.0f))];
        }
      }
  }
  return lv;
}
// r210: an empty spot in a piece - its card cell clear (alpha 0: the shader drops it, the case / stand behind shows) but for a thin
// dashed outline of a card and a small EMPTY SPOT plate: up close it reads as a place for a card, far off (the outline's mips under the
// shader's 0.3 cut) nothing - never a grey card. Drawn at the cell's own size (w x h), straight RGBA.
std::vector<uint8_t> empty_spot(int w, int h) {
  Pc P;
  const float m = 0.07f * w, t = std::max(3.0f, 0.016f * w), arm = 0.16f * w;
  const uint32_t ink = 0xE6D8B4;
  const float x0 = m, y0 = m, x1 = w - m, y1 = h - m;
  // the corners: an L each (a mount's corner brackets)
  P.box(x0, y0, x0 + arm, y0 + t, t * 0.5f, ink, 0.95f), P.box(x0, y0, x0 + t, y0 + arm, t * 0.5f, ink, 0.95f);
  P.box(x1 - arm, y0, x1, y0 + t, t * 0.5f, ink, 0.95f), P.box(x1 - t, y0, x1, y0 + arm, t * 0.5f, ink, 0.95f);
  P.box(x0, y1 - t, x0 + arm, y1, t * 0.5f, ink, 0.95f), P.box(x0, y1 - arm, x0 + t, y1, t * 0.5f, ink, 0.95f);
  P.box(x1 - arm, y1 - t, x1, y1, t * 0.5f, ink, 0.95f), P.box(x1 - t, y1 - arm, x1, y1, t * 0.5f, ink, 0.95f);
  // the dashes between them, along each side
  const float dash = 0.06f * w, gap = 0.045f * w;
  for (float x = x0 + arm + gap; x + dash <= x1 - arm - gap * 0.5f; x += dash + gap) {
    P.box(x, y0, x + dash, y0 + t, t * 0.5f, ink, 0.8f);
    P.box(x, y1 - t, x + dash, y1, t * 0.5f, ink, 0.8f);
  }
  for (float y = y0 + arm + gap; y + dash <= y1 - arm - gap * 0.5f; y += dash + gap) {
    P.box(x0, y, x0 + t, y + dash, t * 0.5f, ink, 0.8f);
    P.box(x1 - t, y, x1, y + dash, t * 0.5f, ink, 0.8f);
  }
  const float cy = 0.5f * h;
  P.box(0.2f * w, cy - 0.055f * h, 0.8f * w, cy + 0.055f * h, 0.05f * h, 0x101422, 0.82f);
  P.ring(0.2f * w, cy - 0.055f * h, 0.8f * w, cy + 0.055f * h, 0.05f * h, t * 0.7f, ink, 0.9f);
  P.text("EMPTY SPOT", 0.5f * w, cy + 0.004f * h, 0.05f * h, 0.54f * w, ink, 0);
  return render(w, h, P.v, nullptr);
}
bool museum_atlas(const std::vector<xrmarket::Variant>& items, std::vector<uint8_t>* out) {
  constexpr int W = 1024, H = 2048, CW = 341, CH = 476, LH = 97;
  // r210: clear everywhere (alpha 0) but the cards drawn (r201-r209 filled it with the case's dark velvet, alpha 1: an empty slot's
  // face + label strip drew as a flat dark card - his "olive / grey slabs"); the colour under the clear is that velvet, so a filtered
  // edge never brightens
  out->assign(static_cast<size_t>(W) * H * 4, 0);
  for (size_t i = 0; i < out->size(); i += 4) {
    (*out)[i] = 14;
    (*out)[i + 1] = 16;
    (*out)[i + 2] = 34;
  }
  static const std::vector<uint8_t> s_empty = empty_spot(CW - 2, CH - 2);
  for (size_t k = 0; k < 9; ++k) {  // (the empty spots first: a card drawn below covers its cell)
    if (k < items.size() && items[k].card >= 0) continue;
    const int x = static_cast<int>(k % 3) * CW, y = static_cast<int>(k / 3) * CH;
    for (int j = 0; j < CH - 2; ++j)
      std::memcpy(&(*out)[(static_cast<size_t>(y + j) * W + x) * 4], &s_empty[static_cast<size_t>(j) * (CW - 2) * 4], static_cast<size_t>(CW - 2) * 4);
  }
  std::vector<Card> cards;
  int total = 0, all = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_set.empty()) return false;
    total = g_setN;  // (r217: a set's count)
    // r220: a JAM '96 card's index is past the first set's count (r217-r219 left every Jam card out of the case here - an empty spot's
    // velvet: the bound was the set's count, not the cards')
    all = static_cast<int>(g_set.size());
    for (const xrmarket::Variant& v : items) cards.push_back(v.card >= 0 && v.card < all ? g_set[static_cast<size_t>(v.card)] : Card());
  }
  for (size_t k = 0; k < items.size() && k < 9; ++k) {
    const xrmarket::Variant& v = items[k];
    if (v.card < 0 || v.card >= all) continue;
    const Ref r = mk_ref(v);
    const std::vector<uint8_t> front = draw_front(cards[k], r, total, 2.0f);
    mk_blit(front, kFrontW, kFrontH, out->data(), W, H, static_cast<int>(k % 3) * CW, static_cast<int>(k / 3) * CH, CW - 2, CH - 2);
    if (v.grade > 0) {
      Pc P;
      slab_label(P, cards[k], r, v.grade, v.cert, 960.0f, 274.0f);  // (the slab label's own 960 x 274)
      const std::vector<uint8_t> lab = render(960, 274, P.v, nullptr);
      mk_blit(lab, 960, 274, out->data(), W, H, static_cast<int>(k % 3) * CW, 1428 + static_cast<int>(k / 3) * LH, CW - 2, LH - 2);
    }
  }
  {  // the card back (every card's back the same plain one)
    Card c0;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      c0 = g_set[0];
    }
    const std::vector<uint8_t> back = draw_back(c0, total);
    mk_blit(back, kBackW, kBackH, out->data(), W, H, 0, 1719, 236, 329);
  }
  return true;
}

int last_coins(std::string* why, long long* when) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (why != nullptr) *why = g_lastCoinsWhy;
  if (when != nullptr) *when = g_lastCoinsWhen;
  return g_lastCoins;
}

// r153: a binder per career - everything the collection file holds back to nothing (g_mutex held)
static void reset_collection() {
  g_owned.assign(g_set.size(), Owned{});
  g_packs.clear();
  g_pulls.clear();
  g_salt = 0;
  g_serial = g_opened = 0;
  g_coins = g_coinsEarned = g_coinsSpent = g_lastCoins = 0;
  g_lastCoinsWhen = 0;
  g_lastCoinsWhy.clear();
  g_cameoSeen.clear();
  g_keep.clear();
  g_newCards.clear();
  g_godPacks = 0;
  g_godWhen = 0;
  g_slabs.clear();
  g_gradeSent = 0;
  g_trades.clear();
  g_tradedCards = g_tradedPacks = 0;
  g_page = kPageBinder;
  g_shopSay.clear();
  g_tradeSay.clear();
  g_gradeSay.clear();
  g_tradeCursor = g_tradeTop = g_setsTop = 0;
  g_gradeCursor = g_gradeTop = g_gradeLeft = 0;
  g_gradeArmed = -1;
}

// r153: the old shared collection.txt / slab_shelf.txt become `seed`'s if it's the career played most of the 3 slots
// (the most saves): Lance's binder stays with his old career, a career started after it starts empty. (g_mutex held)
static void claim_legacy(unsigned seed) {
  const std::string legacy = legacy_coll_path();
  FILE* f = std::fopen(legacy.c_str(), "r");
  if (f == nullptr) return;
  std::fclose(f);
  PortFranchiseSlot slots[PORT_FR_SLOTS] = {};
  PortFranchiseSlots(slots);
  int best = -1;
  for (int i = 0; i < PORT_FR_SLOTS; ++i)
    if (slots[i].used && (best < 0 || slots[i].version > slots[best].version)) best = i;
  if (best < 0 || slots[best].careerId != seed) return;
  const std::string mine = coll_path();
  if (std::rename(legacy.c_str(), mine.c_str()) == 0) {
    std::rename((home_dir() + "/cards/slab_shelf.txt").c_str(),
                (home_dir() + "/cards/slab_shelf-" + std::to_string(seed) + ".txt").c_str());  // (disp_path's, below)
    CLOG("collection: the old shared binder is career %d's now (%s, %u saves)", best + 1, slots[best].club, slots[best].version);
  }
}

void poll() {
  WdScope wd("game thread", "cards poll");  // r237
  load_set();
  static bool s_wallet = false;
  if (!s_wallet) {
    s_wallet = true;
    xrgoalfx::Wallet w;  // r153: GOAL EXPLOSIONS are bought with your TIZM COINS
    w.balance = &goalfx_balance;
    w.spend = &goalfx_spend;
    xrgoalfx::set_wallet(w);
  }
  mk_install();  // r201: the TIZM CARD MARKET + the CARD MUSEUM (their wallet, your copies, the set)
  {
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    {
      WdScope wdm("game thread", "MARKET frame (prices, a matchday, the search's results)");  // r237
      if (xrmarket::frame(now)) ++g_gen;
    }
    {
      WdScope wdm("game thread", "MUSEUM frame");  // r237
      if (xrmuseum::frame(now)) ++g_gen;
    }
    {  // r226: the museum's copies (kept out of TRADE-INS) - re-read when the museum changed (its lock, never under g_mutex)
      static int s_museumGen = -1;
      const int mg = xrmuseum::generation();
      if (mg != s_museumGen) {
        s_museumGen = mg;
        std::vector<xrmarket::Variant> v = xrmuseum::items();
        std::lock_guard<std::mutex> lock(g_museumM);
        if (v.size() != g_museumV.size()) ++g_gen;
        g_museumV.swap(v);
      }
    }
    {
      WdScope wdt("game thread", "COLLECTORS frame (the twenty's matchday, the index)");  // r237
      if (xrtrade::frame(now)) ++g_gen;  // r213: COLLECTOR TRADES (after the market: this career's prices first)
    }
    if (g_page.load() == kPageMarket && xrmarket::redraw_due()) ++g_gen;  // r206: the flash / the last word, a few pictures only
    if ((g_page.load() != kPageMarket || g_sceneMode.load() != 0) && xrmarket::search_open()) xrmarket::search_close();  // r235: (off the page: its letter grid down)
    if (g_page.load() == kPageMuseum && xrmuseum::redraw_due()) ++g_gen;
    if (g_page.load() == kPageCollectors && xrtrade::redraw_due()) ++g_gen;
  }
  // r153: the loaded career's own binder (a new career: an empty one, its own salt); none loaded yet: nothing taken (a
  // match's packs / coins wait in franchise.cpp for their career). Not while a pack / card is out in your hands.
  const unsigned seed = PortFranchiseCareerSeed();
  if (seed != 0 && seed != g_careerSeed && g_loaded.load() && !busy()) {
    WdScope wdc("game thread", "a career's binder loading");  // r237
    std::vector<std::pair<std::string, bool>> nextArt;  // r226
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      reset_collection();
      g_careerSeed = seed;
      ::mkdir((home_dir() + "/cards").c_str(), 0775);
      claim_legacy(seed);
      load_collection();
      const bool fresh = g_salt == 0;
      if (fresh) {
        std::random_device rd;
        g_salt = (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ static_cast<uint64_t>(std::time(nullptr)) ^
                 (static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ull);
        if (g_salt == 0) g_salt = 0x5EED5EEDull;
        save_collection();
      }
      g_packCount = static_cast<int>(g_packs.size());
      bonuses_update();  // r146: the franchise's card bonuses from this career's binder
      CLOG("collection: career %u's binder%s", seed, fresh ? " (new: empty, its own salt)" : "");
      next_packs_art(2, &nextArt);  // r226: the next two packs' big pictures made now (in the background, after anything asked first)
    }
    for (const auto& kp : nextArt) xrbig::prefetch(kp.first, kp.second, kArt);
    {  // r163: the old sleeves' coins back (once, to the first career loaded after r163: looks.txt never said which paid)
      int refund = 0;
      {
        std::lock_guard<std::mutex> lk(g_lookM);
        looks_read_l();
        refund = g_sleeveRefund.exchange(0);
      }
      if (refund > 0) {
        {
          std::lock_guard<std::mutex> lock(g_mutex);
          g_coins = xrcoin::clamp(xrcoin::add(g_coins, refund));
          g_coinsSpent = std::max(0LL, g_coinsSpent - refund);
          save_collection();
          CLOG("looks: sleeves are gone - %d BIT coins back to career %u (%lld now)", refund, seed, g_coins);
        }
        std::lock_guard<std::mutex> lk(g_lookM);
        looks_save_l();  // (its sleeves dropped, refunded noted)
      }
    }
    g_dispStale = true;
    ++g_gen;
  }
  {  // r153: GOAL EXPLOSIONS owned / equipped changed: the CARDS page redrawn
    static int seenFx = -1;
    const int gfx = xrgoalfx::generation();
    if (gfx != seenFx) {
      seenFx = gfx;
      ++g_gen;
    }
    static int seenLooks = -1;  // r160: and a skin / sleeve bought or put on
    const int lg = g_lookGen.load();
    if (lg != seenLooks) {
      seenLooks = lg;
      ++g_gen;
    }
  }
  {  // r164: 8-BIT TAKEOVER's caught coins: real TIZM coins in a franchise match of the loaded career, else dropped
    // r221 (Lance's r220 log: ~20 of these a goal, each one a whole binder file written + fsync'd here on the frame
    // thread under g_mutex, and a log line): paid at once as before (the balance), the file written by the background
    // saver (save_collection_soon), and ONE log line for the takeover once its catches stop for 2 s
    static int s_runCoins = 0, s_runCatches = 0;
    static std::chrono::steady_clock::time_point s_runLast;
    const int caught = xrgoalfx::take_earned();
    if (caught > 0 && g_careerSeed != 0 && seed == g_careerSeed && PortFranchiseMatchActive()) {
      const auto t0 = std::chrono::steady_clock::now();
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        coins_add(caught);
      }
      save_collection_soon();
      s_runCoins += caught;
      ++s_runCatches;
      s_runLast = std::chrono::steady_clock::now();
      g_coinPayNs.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(s_runLast - t0).count());
      g_coinPays.fetch_add(1);
      ++g_gen;
    } else if (caught > 0) {
      CLOG("%d 8-BIT TAKEOVER coins not paid (not a franchise match of this career)", caught);
    }
    if (s_runCatches > 0 && std::chrono::steady_clock::now() - s_runLast > std::chrono::seconds(2)) {
      long long now;
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        now = g_coins;
      }
      CLOG("+%d BIT coins from a room takeover in %d catches (8-BIT TAKEOVER coins / r182 PIXEL INVADERS kills): %lld now "
           "(r221: paid at each catch, the binder saved off the frame thread - %d saves so far, avg %.1f ms, max %.1f ms)",
           s_runCoins, s_runCatches, now, g_saverRuns.load(),
           g_saverRuns.load() > 0 ? g_saverNs.load() / 1e6 / g_saverRuns.load() : 0.0, g_saverMaxNs.load() / 1e6);
      s_runCoins = s_runCatches = 0;
    }
  }
  if (g_careerSeed == 0 || seed != g_careerSeed) return;  // r153: (a match's coins / packs: its own career's)
  // r140: TIZM COINS from a match just played (the moment it ends): kept, a coin's ring
  char why[512] = {};
  const int coins = PortFranchiseTakeCardCoins(why, static_cast<int>(sizeof(why)));
  if (coins > 0) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      coins_add(coins);
      g_lastCoins = coins;
      g_lastCoinsWhen = static_cast<long long>(std::time(nullptr));
      g_lastCoinsWhy = why;
      save_collection();
      CLOG("+%d BIT coins (%s): %lld now", coins, why, g_coins);
    }
    ++g_gen;
    coin_sound(3);
  }
  unsigned flags = 0;
  const int n = PortFranchiseTakeCardPacks(&flags);
  // r217 (Lance 11-11): a WIN = 1 LANCINI TIZM pack + 1 JAM '96 pack, a loss 1 LANCINI TIZM pack (franchise.cpp: a series / a trophy
  // won still a LANCINI TIZM pack more each)
  const int nJam = PortFranchiseTakeJamPacks();
  if (n <= 0 && nJam <= 0) return;
  std::vector<std::pair<std::string, bool>> prefetch;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    add_packs(n, flags, &prefetch);
    add_packs(nJam, flags | kJamPack, &prefetch);
    // r152: a franchise match played (its packs: one a match): TIZM GRADING's slabs a match nearer coming back
    for (Slab& s : g_slabs)
      if (s.left > 0 && --s.left == 0) CLOG("TIZM GRADING: %s back - grade %d", pull_text(s.ref).c_str(), s.grade);
    slabs_sync();
    save_collection();
    CLOG("%d pack%s + %d JAM '96 earned (%s%s%s), %d waiting", n, n == 1 ? "" : "s", nJam, (flags & 1) ? "a win" : "a match",
         (flags & 2) ? ", a playoff series won" : "", (flags & 4) ? ", a trophy" : "", static_cast<int>(g_packs.size()));
  }
  ++g_gen;
  // the pictures of what's in them made now, so opening them is instant
  for (const auto& kp : prefetch) xrbig::prefetch(kp.first, kp.second, kArt);
}

// r140: the CARDS shop: a pack (kPackPrice) or a box of five (kBoxPrice), paid in TIZM coins; rolled and saved the moment
// they're bought (the same odds as an earned pack, never re-rolled), waiting with the rest (opened the same way). 0
// bought, 1 not enough coins, 2 no room. (Game thread.)
// r217: `jam` a JAM '96 pack / box (kJamPackPrice / kJamBoxPrice)
// r230: `set` 0 LANCINI TIZM, 1 JAM '96, 2 LANCINI EXQUISITE (a pack 1,000 / THE VAULT case of five 5,000 - its packs kExqPack, a case's
// kExqBox with each one's place in it)
static int buy_packs(bool box, int set = 0) {
  std::vector<std::pair<std::string, bool>> prefetch;
  const bool jam = set == 1, exq = set == 2;
  const int price = exq ? (box ? xrexq::kBoxPrice : xrexq::kPackPrice) : jam ? (box ? kJamBoxPrice : kJamPackPrice) : (box ? kBoxPrice : kPackPrice),
            n = box ? kBoxPacks : 1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_coins < price) return 1;
    if (g_packs.size() + n > 999) return 2;
    coins_take(price);
    add_packs(n, kBought | (jam ? kJamPack : 0u) | (exq ? kExqPack | (box ? kExqBox : 0u) : 0u), &prefetch);
    save_collection();
    CLOG("bought %s%s for %d BIT coins: %lld left, %d packs waiting", exq ? "LANCINI EXQUISITE: " : (jam ? "JAM '96: " : ""),
         box ? (exq ? "THE VAULT (a case of 5 packs)" : "a box of 5 packs") : "a pack", price, g_coins, static_cast<int>(g_packs.size()));
  }
  ++g_gen;
  coin_sound(2);
  for (const auto& kp : prefetch) xrbig::prefetch(kp.first, kp.second, kArt);
  return 0;
}
// r202: TIZM CASINO's MEGA JACKPOT pays a free pack (the shop's odds, rolled + saved now, waiting with the rest)
bool grant_packs(unsigned career, int n, const char* why) {
  std::vector<std::pair<std::string, bool>> prefetch;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (career == 0 || career != g_careerSeed || n <= 0 || g_packs.size() + n > 999) return false;
    add_packs(n, kBought, &prefetch);
    save_collection();
    CLOG("STRIKERZ CASINO: %d free pack%s (%s), %d packs waiting", n, n == 1 ? "" : "s", why != nullptr ? why : "?",
         static_cast<int>(g_packs.size()));
  }
  ++g_gen;
  for (const auto& kp : prefetch) xrbig::prefetch(kp.first, kp.second, kArt);
  return true;
}

namespace {
// r152: TIZM GRADING. The copy of a card it sends: its finest not sealed (a BLACK 1/1, a case hit, a GOLD - its lowest
// number -, a SILVER, a shiny, a BASE), false none. (g_mutex held)
bool grade_pick(int card, Ref* out) {
  if (card < 0 || card >= static_cast<int>(g_owned.size())) return false;
  const Owned& o = g_owned[card];
  // r210: the finest copy in the order below that is NOT listed on the market (a listed copy stays out of grading); r152-r209 took the
  // first kind there was
  std::vector<std::pair<Ref, int>> cands;  // (a kind of copy, how many of it you have)
  Ref r;
  r.card = card;
  for (int s = 1; s >= 0; --s)
    if (o.n[kBlack][s] > 0) r.parallel = kBlack, r.shiny = s != 0, r.serial = 1, r.hit = kHitNone, cands.push_back({r, o.n[kBlack][s]});
  {
    std::vector<Ref> hits = o.hits;
    std::stable_sort(hits.begin(), hits.end(), [](const Ref& a, const Ref& b) { return pull_score(a) > pull_score(b); });
    for (const Ref& h : hits) cands.push_back({h, 1});
  }
  if (o.n[kGoldTizm][0] + o.n[kGoldTizm][1] > 0) {
    r = Ref();
    r.card = card;
    r.parallel = kGoldTizm, r.shiny = o.n[kGoldTizm][1] > 0;
    for (int v : o.golds) {  // (a number not sealed already)
      bool sealed = false;
      for (const Ref& s : o.sealed) sealed = sealed || (s.parallel == kGoldTizm && s.serial == v);
      if (!sealed) r.serial = v, cands.push_back({r, 1});
    }
  }
  for (int par : {kSilver, kBase})
    for (int s = 1; s >= 0; --s)
      if (o.n[par][s] > 0) {
        r = Ref();
        r.card = card;
        r.parallel = par, r.shiny = s != 0;
        cands.push_back({r, o.n[par][s]});
      }
  for (const auto& c : cands) {
    const int listed = listed_n(card, c.first.parallel, c.first.shiny, c.first.hit, c.first.serial, 0);
    if (c.second > listed) {
      *out = c.first;
      return true;
    }
  }
  return false;
}
std::vector<int> grading_slab_order() {  // (g_mutex held)
  std::vector<int> v;
  for (size_t i = 0; i < g_slabs.size(); ++i) v.push_back(static_cast<int>(i));
  std::stable_sort(v.begin(), v.end(), [](int a, int b) {
    const Slab &x = g_slabs[static_cast<size_t>(a)], &y = g_slabs[static_cast<size_t>(b)];
    if ((x.left > 0) != (y.left > 0)) return x.left > 0;
    if (x.left > 0) return x.left < y.left;
    if (x.grade != y.grade) return x.grade > y.grade;
    return pull_score(x.ref) > pull_score(y.ref);
  });
  return v;
}
std::vector<int> grading_tiles() {  // (g_mutex held)
  std::vector<std::pair<int, int>> v;
  for (size_t i = 0; i < g_set.size(); ++i) {
    Ref r;
    if (grade_pick(static_cast<int>(i), &r)) v.push_back({pull_score(r), static_cast<int>(i)});
  }
  std::stable_sort(v.begin(), v.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
    return a.first != b.first ? a.first > b.first : g_set[a.second].num < g_set[b.second].num;
  });
  std::vector<int> out;
  for (const auto& p : v) out.push_back(p.second);
  return out;
}
int roll_grade(uint64_t& rng) {
  int x = static_cast<int>(mix64(rng) % 1000ull);
  for (int g = 10; g >= 1; --g) {
    if (x < kGradeOdds[g]) return g;
    x -= kGradeOdds[g];
  }
  return 8;
}
// a card sent off (its finest copy not sealed): 0 sent (*sent its slab), 1 not enough coins, 2 nothing to send. (Game thread.)
static int grade_send(int card, Slab* sent) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    Ref r;
    if (!grade_pick(card, &r)) return 2;
    const int cost = grade_cost(r);
    if (g_coins < cost) return 1;
    coins_take(cost);
    Slab s;
    s.ref = r;
    uint64_t rng = g_salt ^ (static_cast<uint64_t>(++g_gradeSent) * 0xC2B2AE3D27D4EB4Full);
    s.grade = roll_grade(rng);
    for (int guard = 0; guard < 64; ++guard) {  // its cert: 8 digits, no other slab's
      s.cert = 10000000u + static_cast<uint32_t>(mix64(rng) % 90000000ull);
      bool used = false;
      for (const Slab& o : g_slabs) used = used || o.cert == s.cert;
      if (!used) break;
    }
    s.left = kGradeMatches;
    s.when = static_cast<long long>(std::time(nullptr));
    add_owned(r, -1);  // (out of its copies: sealed)
    g_slabs.push_back(s);
    slabs_sync();
    save_collection();
    bonuses_update();
    *sent = s;
    CLOG("TIZM GRADING: %s sent for %d coins (cert %u), back after %d matches", pull_text(r).c_str(), cost, s.cert, kGradeMatches);
  }
  ++g_gen;
  coin_sound(2);
  return 0;
}

}  // namespace

// r146: a TRADE-IN: the next kTradeFor spare base cards (trade_pick: commons first, the most duplicated first, none kept, never a last copy)
// out of your collection, a pack rolled for them that moment (the same odds as any, never re-rolled) waiting with the
// rest; its history; saved, the franchise bonuses worked out again (the levels it took). 0 traded (*out: the cards, for
// its show), 1 not enough spares, 2 no room. (Game thread.)
// r226: `set` 0 LANCINI TIZM spares -> a LANCINI TIZM pack, 1 JAM '96 spares -> a JAM '96 pack (kJamPack in its flags: rolled from the Jam set)
static int trade_in(std::vector<int>* out, int set = 0) {
  std::vector<std::pair<std::string, bool>> prefetch;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::vector<std::pair<int, int>> pick = trade_pick(set);
    if (pick.empty()) return 1;
    if (g_packs.size() + 1 > 999) return 2;
    const size_t before = g_packs.size();
    add_packs(1, kTraded | (set == 1 ? kJamPack : 0u), &prefetch);
    if (g_packs.size() == before) return 2;  // (no set on this headset)
    Trade t;
    t.when = static_cast<long long>(std::time(nullptr));
    t.pack = g_packs.back().serial;
    for (const auto& p : pick) {
      Ref r;
      r.card = p.first;
      r.parallel = kBase;
      r.shiny = false;
      add_owned(r, -p.second);
      for (int k = 0; k < p.second; ++k) out->push_back(p.first);
      t.what += (t.what.empty() ? "" : ",") + g_set[p.first].id + "*" + std::to_string(p.second);
    }
    g_trades.push_back(t);
    if (g_trades.size() > 40) g_trades.erase(g_trades.begin(), g_trades.end() - 40);
    g_tradedCards += trade_for(set);
    ++g_tradedPacks;
    save_collection();
    bonuses_update();
    CLOG("traded in %d spare %sbase cards (%s) for %spack %d: %d packs waiting, %d traded in all", trade_for(set), set == 1 ? "JAM '96 " : "", t.what.c_str(),
         set == 1 ? "JAM '96 " : "", t.pack,
         static_cast<int>(g_packs.size()), g_tradedCards);
  }
  ++g_gen;
  for (const auto& kp : prefetch) xrbig::prefetch(kp.first, kp.second, kArt);
  return 0;
}

// ======================================================================================= the pack in your hands
namespace {

constexpr float kCardWm = 0.12f, kCardHm = kCardWm * 1.4f;  // a card at size 1 (m): bigger than a real one, to read
constexpr float kPackWm = 0.135f, kPackHm = kPackWm * kPH / kPW;
// r138 (Lance: "the cards, you made flimsy, they have the same feel as the magazine does, and that shouldnt be a thing"):
// a card is stiff card stock: flat (never bends), kCardT thick at size 1, its edge all round with the same rounded
// corners as its pictures (48 px of 1080 across: kCornerR of its width)
constexpr float kCardT = 0.0012f, kCornerR = 48.0f / 1080.0f;
constexpr int kCardNX = 2, kCardNY = 2, kCardNV = (kCardNX + 1) * (kCardNY + 1);
// r220: 15 (64 edge segments - every JAM '96 die's edge at exactly lk::jamcut::kLoopN points, so THE PUNCH lerps point to point; an A
// card's round corners a little smoother)
constexpr int kCornerSegs = 15, kEdgeSegs = 4 * (kCornerSegs + 1), kEdgeNV = (kEdgeSegs + 1) * 2;  // r217: 9 (40 edge segments: a Jam card's crest)
static_assert(kEdgeSegs == lk::jamcut::kLoopN, "every die's edge loop has kEdgeSegs points");
constexpr int kCardVerts = 2 * kCardNV + kEdgeNV;  // a card's vertices: its front, its back, its edge
// r220 (THE PUNCH): after each card's own (kCardVerts) and its level moment (kTagVerts): its front inside the crest only (face down: none
// of its real cut showing round the back), its back with its own die (after the punch), its cut-away bits (quads)
constexpr int kBitMax = lk::jamcut::kBitMax, kPunchVerts = lk::jamcut::kLoopN + kCardNV + 4 * kBitMax;
constexpr uint64_t kPunchedLongAgo = 1;  // (punchNs: already its cut - out of the binder, a slab, a trade-in)

// r138: each finish's own look in your hands beyond its picture - where the special (non-BASE) treatments plug in (being
// chosen with Lance: things popping out of the card, depth inside it, an aura on its back before it turns over ...):
// its edge (the stock's core: BASE white, a parallel's in its metal, and how much foil that takes), its reveal (a
// sparkle round it, the big show with the fanfare, the buzz), and finish_frame() (its extra draws / particles each frame).
struct FinishLook {
  uint32_t edge;
  float edgeFoil;            // 0 printed .. 1 foil (xr_fx_model.frag's card mode)
  float sparkleS, sparkleR;  // its reveal's sparkle round the card (0: none)
  bool show;                 // its reveal's big show: a flash, rings of light, sparks, the fanfare
  float buzz, buzzS;
};
const FinishLook kFinishLook[4] = {{0xF2F1EC, 0.0f, 0.0f, 0.0f, false, 0.22f, 0.05f},  // BASE: printed, a white core, a tap
                                   {0xDCE2EC, 0.8f, 1.8f, 0.13f, false, 0.7f, 0.2f},   // SILVER TIZM
                                   {0xE8B83A, 0.9f, 2.6f, 0.15f, false, 0.9f, 0.3f},   // GOLD TIZM /10
                                   {0x1A1C22, 0.7f, 3.0f, 0.16f, true, 1.0f, 0.45f}};  // BLACK TIZM 1/1
constexpr int kPackNX = 10, kBodyNY = 14, kStripNY = 2;
constexpr int kBodyNV = (kPackNX + 1) * (kBodyNY + 1), kStripNV = (kPackNX + 1) * (kStripNY + 1);
constexpr int kPackNV = 2 * kBodyNV + 2 * kStripNV;

enum Mode { kNone = 0, kPackUp = 1, kOut = 2, kAway = 3, kTrade = 4 };  // (r146: a trade-in's show)
// r139 (Lance: "i want the pack opening experience to be that of ripping a pack in real life"): the pack in your hand
// waiting to be ripped; ripped, its cards filing out one by one into a stack in that hand, backs to you; the stack (each
// trigger turns its front card over / puts it to the back)
enum Phase { kWaiting, kFiling, kStack, kVault };  // (r230: kVault - an EXQUISITE pack in its case, the latches / the lid first)

XrVector3f v_add(XrVector3f a, XrVector3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XrVector3f v_sub(XrVector3f a, XrVector3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f v_mul(XrVector3f a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float v_len(XrVector3f a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
XrVector3f v_lerp(XrVector3f a, XrVector3f b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }
XrVector3f v_cross(XrVector3f a, XrVector3f b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
XrQuaternionf q_mul(const XrQuaternionf& a, const XrQuaternionf& b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
XrQuaternionf q_conj(const XrQuaternionf& q) { return {-q.x, -q.y, -q.z, q.w}; }
XrVector3f q_rot(const XrQuaternionf& q, const XrVector3f& v) {
  const XrQuaternionf p{v.x, v.y, v.z, 0.0f};
  const XrQuaternionf r = q_mul(q_mul(q, p), q_conj(q));
  return {r.x, r.y, r.z};
}
XrQuaternionf q_axis(float x, float y, float z, float a) {
  const float s = std::sin(0.5f * a);
  return {x * s, y * s, z * s, std::cos(0.5f * a)};
}
float wrap_pi(float a) {
  while (a > 3.14159265f) a -= 6.2831853f;
  while (a < -3.14159265f) a += 6.2831853f;
  return a;
}
float smooth(float t) {
  t = clampf(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
float secs(uint64_t a, uint64_t b) { return a > b ? static_cast<float>(static_cast<double>(a - b) * 1e-9) : 0.0f; }

// A sheet in your room: the pack or a card. It faces you (turned relYaw more), leans back to your eyes, rolled (a fan of
// cards) - r142: until a hand first has it, then turned as held (`pose`) -, turned over by `flip` (0 its front to you,
// pi its back: about its own up), `scale` times its size, bent across (curvature 1/m: the pack's pouch only; r138: a card
// is rigid, never bent).
struct Sheet {
  bool on = false, shown = false;
  bool rigid = false;  // r138: a card (stiff, thick: card_box), not the pack's pouch
  XrVector3f pos{0.0f, 0.0f, 0.0f}, tPos{0.0f, 0.0f, 0.0f};
  float relYaw = 0.0f, roll = 0.0f, tRoll = 0.0f, flip = 0.0f, scale = 1.0f, tScale = 1.0f;
  float bend = 0.0f, bendV = 0.0f;
  float flipFrom = 0.0f, flipTo = 0.0f, flipDur = 0.0f;
  uint64_t flipNs = 0;
  bool flipFx = false;     // its reveal's moment (the face coming round) still to come
  bool free = false;       // let go by a hand somewhere: it stays there
  int hands = 0;           // bit 0 the left hand, bit 1 the right
  float offFade = 0.0f;    // (a card out of the binder glides from its pocket into the hand: relP -> relPTo, r142)
  float twoDist = 1.0f, twoScale = 1.0f;
  XrVector3f twoPos{0.0f, 0.0f, 0.0f}, twoMid{0.0f, 0.0f, 0.0f};
  XrVector3f lastPos{0.0f, 0.0f, 0.0f}, vel{0.0f, 0.0f, 0.0f};
  float w = kCardWm, h = kCardHm;
  // r139: a card in the stack faces as the stack in your hand does (not each to your eyes on its own: they'd splay)
  bool fixedBase = false;
  XrQuaternionf base{0.0f, 0.0f, 0.0f, 1.0f};
  // r142 (Lance: "make the cards more manipulable, like i felt like i was forced to hold the card at an angle and i couldnt
  // wave it the way i wanted to"): once a hand has had it, it's turned as it's held (`pose`, in the room) - not to your
  // eyes any more - and stays just so when let go; its place and turn in each holding hand's own frame since that hand
  // closed on it (relP, relQ; relPTo: a card out of the binder gliding from its pocket into the hand); `lead` the hand it
  // turns with while both hold it
  bool posed = false;
  XrQuaternionf pose{0.0f, 0.0f, 0.0f, 1.0f};
  XrQuaternionf relQ[2] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
  XrVector3f relP[2] = {};
  XrVector3f relPTo{0.0f, 0.0f, 0.0f};
  int lead = 1;
  // r219 (BIG PULLS): a hit's reveal in a pack - its build-up's plan (xr_pulls.h; tier 0: a BASE card's plain 0.5 s flip), and how far
  // its turn has come (0 face down .. 1 face up: the moment at 0.5)
  xrpulls::Plan pull;
  float flipP = 0.0f;
};

// The pictures a model is made of, drawn on threads; uploaded once all are in.
struct Pics {
  std::mutex m;
  std::vector<std::vector<uint8_t>> rgba;
  std::vector<int> w, h;
  int left = 0;
  int gen = 0;
  // r226 (the cards' pictures): a front drawn with its portrait (its big picture not in yet: late[i] 1), drawn again once it is
  // (2 drawing, 3 done) - redrawn[i] the new front until the frame thread swaps it into the card model; what it was drawn from
  std::vector<uint8_t> late;
  std::vector<std::vector<uint8_t>> redrawn;
  // r226: a picture's mips made on its drawing thread (mip_chain_front: the fronts'; empty = the GPU makes them, as before)
  std::vector<std::vector<std::vector<uint8_t>>> mips, redrawnMips;
  std::vector<Card> cards;
  std::vector<Ref> refs;
  std::vector<int> levels;
  int total = 0;
  unsigned bigGen = ~0u;  // (xrbig::generation() when the late ones were last looked at)
};

struct Scene3 {
  int mode = kNone, phase = kWaiting;
  uint64_t t0 = 0, phaseNs = 0, cardsNs = 0, lastNs = 0;
  XrVector3f anchor{0.0f, 0.0f, 0.0f}, fwd{0.0f, 0.0f, -1.0f}, right{1.0f, 0.0f, 0.0f};
  Sheet pack;
  Sheet card[kSheets];
  Ref refs[kSheets];
  int n = 0, revealed = 0;
  Pack packData;       // the pack open now (put back if it goes unopened)
  int pullsAdded = 0;  // (its big pulls, taken back with it)
  // the pack's strip after the tear
  bool torn = false;
  XrVector3f stripPos{0.0f, 0.0f, 0.0f}, stripVel{0.0f, 0.0f, 0.0f};
  XrQuaternionf stripQ{0.0f, 0.0f, 0.0f, 1.0f};
  float stripSpin = 0.0f, stripScale = 1.0f, stripBase = 1.0f;
  float shredT = 0.0f;  // r139: foil shreds off the rip
  XrVector3f shredAt{0.0f, 0.0f, 0.0f};
  // r139: the stack in the hand that held the pack (S.pack is its holder from the rip on): each card's place (0 the front,
  // your side), its place's depth as it eases there, out toward you for its moment (0..1), turned face up; its trip out of
  // the pack (filing, the order: the back of the stack first), its trip round to the back (0: not moving)
  int depth[kSheets] = {};
  float zAt[kSheets] = {}, popK[kSheets] = {}, backU[kSheets] = {};
  bool faceUp[kSheets] = {};
  uint64_t backNs[kSheets] = {};
  int filed = 0;
  bool trigDown[2] = {false, false};
  // the wrapper, empty, falling away
  bool wrapFall = false;
  XrVector3f wrapPos{0.0f, 0.0f, 0.0f}, wrapVel{0.0f, 0.0f, 0.0f};
  XrQuaternionf wrapQ{0.0f, 0.0f, 0.0f, 1.0f};
  float wrapScale = 0.0f;
  // models
  std::shared_ptr<Pics> packPics, cardPics;
  int packModel = -1, cardModel = -1;  // the pictures' generation up in its slot (-1 none)
  std::vector<float> packVerts, cardVerts;
  // the hands
  int hold[2] = {-1, -1};  // the sheet each hand holds (0 the pack, 1.. the cards), -1 none
  bool spent[2] = {false, false}, prevGrip[2] = {false, false};
  bool aimOk[2] = {false, false};  // r163: the pointers this frame (hands(): a card out of the 3D binder)
  XrPosef aimAt[2] = {};
  XrVector3f handAt[2] = {};
  bool handHave[2] = {false, false};
  XrQuaternionf handQ[2] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};  // r142: each grip's turn (its last tracked)
  float apart = 0.0f, apartV = 0.0f;
  // the hub's buttons' asks (done in frame(), with its clock)
  bool askOpen = false, askTear = false, askAway = false, nextPack = false;
  int askLook = -1;
  // effects
  float sparkleT = 0.0f, sparkleR = 0.1f;
  XrVector3f sparkleAt{0.0f, 0.0f, 0.0f};
  bool burst = false;
  uint32_t burstRgb = 0xFFFFFF;
  XrVector3f burstAt{0.0f, 0.0f, 0.0f};
  // the hub's picture (where the binder is)
  bool hubValid = false;
  XrPosef hubPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  float hubW = 0.0f, hubH = 0.0f;
  XrPosef head{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  uint64_t pageUpNs = 0;
  bool prevLt = false, prevRt = false;
  float buzz = 0.0f, buzzS = 0.0f;
  // r146: a trade-in's show: the slot in front of you, its cards gone in so far, the pack out (when), skipped
  XrVector3f slot{0.0f, 0.0f, 0.0f};
  XrVector3f tradeFrom[kSheets] = {};
  int tradeIn = 0;
  bool tradeSlid[kSheets] = {};
  uint64_t popNs = 0;
  bool tradeSkip = false;
  // r147: each card's level moment as it's turned over in a pack (0 none: its first copy; 1 a duplicate; 2 this copy
  // took it up a level), its level and copies, when it was turned over (0 not yet)
  int tagKind[kSheets] = {}, tagLevel[kSheets] = {}, tagCopies[kSheets] = {};
  uint64_t tagNs[kSheets] = {};
  // r220 (THE PUNCH): each card's die (-1 an A card, 0 a JAM '96 crest, > 0 its real cut: lk::jam_shape) and when it was punched from
  // the crest it shows face down to its real cut (0: still to come - a pack card not turned over yet; kPunchedLongAgo: already its
  // cut, e.g. out of the binder); the card's pose at that moment (its bits fall from there)
  int cutShape[kSheets] = {};
  uint64_t punchNs[kSheets] = {};
  XrQuaternionf punchQ[kSheets] = {};
  XrVector3f punchPos[kSheets] = {};
  float punchScale[kSheets] = {};
  std::vector<std::string> packNew;  // r149: the cards the open pack made NEW (unmade if it goes back unopened)
  int newLook = -1;                  // r149: the NEW card the binder's cursor rests on, since when
  uint64_t newLookNs = 0;
  bool godAdded = false, godDone = false;  // r149: the open pack's a GOD PACK (counted), its show had (or skipped)
  bool boomShown[kSheets] = {};            // r150: a TIZM BOOM's pop-out show played this scene (again only on a shake)
  // r152: a TIZM GRADING slab out in front of you (g_slabs' place, -1 none; as it was when taken out), its BACK FROM
  // GRADING reveal (when it started, 0 none; where it ends up), a slab asked for by the GRADING page
  int outSlab = -1, askSlab = -1;
  Slab slab;
  uint64_t gradeNs = 0;
  XrVector3f revealAt{0.0f, 0.0f, 0.0f};
  bool gradeHit = false;  // (its grade's moment had)
};
Scene3 S;
// r230: LANCINI EXQUISITE's opening (xr_cards_vault.inc, below): THE VAULT case, the private gallery's mood
std::atomic<float> g_gallery{0.0f};
uint64_t g_lastRevealNs = 0;  // (the last card's reveal: the gallery eases back a while after the last)
void vault_begin(uint64_t now, unsigned flags, int serial);
void vault_end(bool fxReady);
void vault_flick_next(uint64_t now);
bool vault_trigger(int h, uint64_t now);
// r219 (BIG PULLS, C:\StrikersMR\BIG-PULLS.md): the hit being built up in the stack now (its card, -1 none) and its heartbeat's last
// moment played; the room's darkness + white flash now (a 1/1's: room_dark / room_flash - xr_quest's passthrough reads them, the
// effects' shade round you darkens with it); a 1/1's own show (its cameo, its spotlight's + autograph's dimming) held back after
// its reveal till g_pullGateNs, then eased in (pull_gate)
int g_pullCard = -1;
float g_pullBeatT = -1.0f;
std::atomic<float> g_pullDark{0.0f}, g_pullFlash{0.0f};
uint64_t g_pullGateNs = 0;
float pull_floor();  // (the room's floor for the party: below, with g_floorY)
// A card's build-up (xr_pulls.h) as it's turned over in a pack: its finish, shiny, case hit, set; a GRAIL character (the market's
// demand tier 5); a GOD PACK's last card
xrpulls::Plan pull_plan_of(int i) {
  const Ref& r = S.refs[i];
  xrpulls::Pull p;
  {  // r230: an EXQUISITE card - its own show (xr_exq.h: GOLD LEAF / hits BIG, EMERALD / SAPPHIRE HUGE, a MASTERPIECE 1/1 / a GOLD RPA the JACKPOT + the lights out)
    std::lock_guard<std::mutex> lock(g_mutex);
    if (exq_card(r.card)) {
      p.exq = xrexq::tier_of(r.serial);
      p.rookie = exq_rookie(g_set[static_cast<size_t>(r.card)]);
    }
  }
  p.parallel = r.parallel;
  p.shiny = r.shiny;
  p.hit = r.hit;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    p.jam = r.card >= 0 && r.card < static_cast<int>(g_set.size()) && g_set[r.card].set == 1;
  }
  // (only a hit asks the market - its lock, never with the binder's held)
  if (r.parallel != kBase || r.shiny || r.hit != kHitNone || p.exq > 0) p.grail = xrmarket::hype_tier(r.card) >= 5;
  p.godLast = S.godAdded && S.revealed >= S.n;
  return xrpulls::plan(p);
}
// 1 while nothing holds a 1/1's own dimming back; after a dark build-up's reveal 0 till g_pullGateNs, then up over kEaseBack
float pull_gate(uint64_t now) {
  if (g_pullGateNs == 0 || now >= g_pullGateNs + static_cast<uint64_t>(xrpulls::kEaseBack * 1e9f)) return 1.0f;
  if (now <= g_pullGateNs) return 0.0f;
  return static_cast<float>(static_cast<double>(now - g_pullGateNs) * 1e-9) / xrpulls::kEaseBack;
}
// r138: the special cards' 3D (below)
void specials_plan(uint64_t now, bool fxReady);
void specials_end(bool fxReady);
void special_chime(const Ref& r);
void special_spot(int i, uint64_t until);
void specials_frame(xrfx::Scene& scene, float dt, uint64_t now, bool fxReady, bool hide[kSheets]);
void special_focus(int i, uint64_t now, bool fxReady);
void card_sound(int kind);  // r139: 0 the rip, 1 a card sliding out, 2 a card turning over, 3 a card put to the back
bool cameo_ask(int i, const std::string& key);  // r141: a BLACK 1/1 turned over for the first time: its cameo
bool cameo_live();
void cameo_skip();
// r149: the GOD PACK's show (below): its pictures drawn as the pack comes up, its tell at the rip, its show after the last
// card's moment (A / B / a trigger skips it), put away with the scene
void special_boom_start(int i);  // r150: a TIZM BOOM turned over: its pop-out show, with its music
void slab_end(bool fxReady);     // r152: a TIZM GRADING slab's model put away (below)
void god_prepare();
void god_tell(uint64_t now);
void god_last(uint64_t now);
bool god_live();
void god_skip();
void god_end(bool fxReady);
int g_picGen = 0;
std::mutex g_packPxMutex;
std::vector<uint8_t> g_packPx[3][2];  // the pack's pictures, drawn once (every pack of a set looks the same) - r217: [set][side] (r230: + EXQUISITE)
int g_packPxTotal[3] = {-1, -1, -1};
// r217: the CARD SHOP's pictures - each pack's real wrapper (300 x 460) and its box of five in a 3/4 view (650 x 550), drawn once a
// set on a thread of their own the first time the shop asks (the hub draws a plain pack till then, then again: g_gen), kept (~0.6
// + 1.4 MB a set)
std::mutex g_shopPicM;
ImgP g_shopPic[3][2];
bool g_shopPicAsked[3] = {false, false, false};
std::vector<uint8_t> vault_shop_px(const Img& pack, int* w, int* h);  // r230 (below)
ImgP shop_pic(int set, bool box) {
  set = set == 1 ? 1 : (set == 2 ? 2 : 0);
  {
    std::lock_guard<std::mutex> lock(g_shopPicM);
    if (g_shopPic[set][box ? 1 : 0]) return g_shopPic[set][box ? 1 : 0];
    if (g_shopPicAsked[set]) return ImgP();
    g_shopPicAsked[set] = true;
  }
  auto work = [set]() {
    const std::vector<uint8_t> big = draw_pack(false, g_setN, set);
    auto pack = std::make_shared<Img>();
    {  // (its foil's alpha solid: a picture on the hub)
      Img full;
      full.w = kPackW, full.h = kPackH, full.rgba = big;
      for (size_t i = 3; i < full.rgba.size(); i += 4)
        if (full.rgba[i] >= 120) full.rgba[i] = 255;
      lk::Cv cv(300, 460);
      lk::paste(cv, full, 0, 0, 300, 460);
      pack->w = 300, pack->h = 460, pack->rgba = lk::bytes(cv, false);
    }
    ImgP mario = portrait("strikers_mario"), chari = portrait("pk_charizard"), spidey = portrait("mua_spiderman");
    lk::Star st[3];
    lk::pack_stars(mario, chari, spidey, st);
    auto box = std::make_shared<Img>();
    if (set == 2) box->rgba = vault_shop_px(*pack, &box->w, &box->h);  // r230: THE VAULT, open, its 5 packs in velvet
    else box->rgba = lk::box_picture(set, *pack, st, &box->w, &box->h);
    {
      std::lock_guard<std::mutex> lock(g_shopPicM);
      g_shopPic[set][0] = pack;
      g_shopPic[set][1] = box;
    }
    CLOG("the CARD SHOP's %s pictures drawn", set == 2 ? "LANCINI EXQUISITE" : (set == 1 ? "JAM '96" : "LANCINI TIZM"));
    ++g_gen;
  };
#ifdef HARNESS_REAL_CARDS
  work();  // (the PC proof: no threads)
  std::lock_guard<std::mutex> lock(g_shopPicM);
  return g_shopPic[set][box ? 1 : 0];
#else
  std::thread(work).detach();
  return ImgP();
#endif
}
std::vector<uint8_t> shop_box_px(int set, int* w, int* h) {  // (the PC proof: the box, drawn now)
  const std::vector<uint8_t> big = draw_pack(false, g_setN, set);
  Img full;
  full.w = kPackW, full.h = kPackH, full.rgba = big;
  for (size_t i = 3; i < full.rgba.size(); i += 4)
    if (full.rgba[i] >= 120) full.rgba[i] = 255;
  lk::Cv cv(300, 460);
  lk::paste(cv, full, 0, 0, 300, 460);
  Img pack;
  pack.w = 300, pack.h = 460, pack.rgba = lk::bytes(cv, false);
  lk::Star st[3];
  lk::pack_stars(portrait("strikers_mario"), portrait("pk_charizard"), portrait("mua_spiderman"), st);
  return lk::box_picture(set, pack, st, w, h);
}

Sheet* sheet(int i) { return i == 0 ? &S.pack : (i >= 1 && i <= S.n ? &S.card[i - 1] : nullptr); }

void buzz(float amp, float s) {
  if (amp > S.buzz) {
    S.buzz = amp;
    S.buzzS = s;
  }
}

// Where the sheet faces: you (turned relYaw more), leaning back to your eyes, rolled; then flipped.
XrQuaternionf sheet_base(const Sheet& s) {
  if (s.fixedBase) return s.base;  // r139: in the stack
  if (s.posed) return s.pose;      // r142: as a hand holds it / let it go
  const XrVector3f& h = S.head.position;
  const float dx = h.x - s.pos.x, dz = h.z - s.pos.z;
  const float yaw = std::atan2(dx, dz) + s.relYaw;
  const float up = std::atan2(h.y - s.pos.y, std::max(0.05f, std::sqrt(dx * dx + dz * dz)));
  const float tilt = clampf(-up, -1.2f, 1.0f);
  return q_mul(q_mul(q_axis(0, 1, 0, yaw), q_axis(1, 0, 0, tilt)), q_axis(0, 0, 1, s.roll));
}
XrQuaternionf sheet_q(const Sheet& s) { return q_mul(sheet_base(s), q_axis(0, 1, 0, s.flip)); }

// Is the hand at it / its pointer on it: how near (m), < 0 not.
float sheet_hit(const Sheet& s, const XrVector3f& hand, const XrPosef* aim) {
  if (!s.on || !s.shown || s.scale < 0.2f) return -1.0f;
  const XrQuaternionf qi = q_conj(sheet_base(s));
  // (the pack comes up between your hands: a hand at its side takes it)
  const float margin = s.w > kCardWm + 0.001f ? 0.11f : 0.05f;
  const float hw = 0.5f * s.w * s.scale + margin, hh = 0.5f * s.h * s.scale + 0.05f;
  const XrVector3f l = q_rot(qi, v_sub(hand, s.pos));
  if (std::fabs(l.z) < 0.12f && std::fabs(l.x) < hw && std::fabs(l.y) < hh) return std::fabs(l.z);
  if (aim != nullptr) {
    const XrVector3f o = q_rot(qi, v_sub(aim->position, s.pos));
    const XrVector3f d = q_rot(qi, q_rot(aim->orientation, XrVector3f{0.0f, 0.0f, -1.0f}));
    if (std::fabs(d.z) > 1e-3f) {
      const float t = -o.z / d.z;
      if (t > 0.0f && t < 2.0f) {
        const float x = o.x + d.x * t, y = o.y + d.y * t;
        if (std::fabs(x) < 0.5f * s.w * s.scale + 0.02f && std::fabs(y) < hh - 0.03f) return 0.2f + t;
      }
    }
  }
  return -1.0f;
}

void start_flip(Sheet& s, float to, float dur, uint64_t now, bool fx) {
  s.flipFrom = s.flip;
  s.flipTo = to;
  s.flipDur = dur;
  s.flipNs = now;
  s.flipFx = fx;
  s.pull = xrpulls::Plan{};  // r219: a plain turn (stack_step gives a hit's its build-up after this)
  s.flipP = 0.0f;
}

// r139: the stack (Lance: "the cards file 1 by 1 with the backs of the cards showing, but in a single line not spread
// apart the way it was. that way each turn of the card is truly an experience"): in the frame of the hand's pack (its
// holder from the rip on; metres at the pack's size 1, x right, y up, z toward you), the cards one behind another, backs
// to you, in front of the pack's face; out of the pack one by one (the back of the stack first: the front card is the
// first turned over, the biggest pull last); the front card out toward you for its moment once turned over; round to
// the back over the top of the stack.
constexpr float kStackCard = 0.92f;                       // a card's size against the pack's
constexpr float kStackZ0 = 0.007f, kStackGap = 0.0022f;   // the back card in front of the pack's face; card to card
constexpr float kFileStart = 0.40f, kFileGap = 0.32f, kFileDur = 0.60f;  // s: after the rip; card to card; its trip
constexpr float kOutY = 0.168f;                           // a card right out of the pack, over the torn top
constexpr float kPopZ = 0.075f, kPopY = 0.012f, kPopScale = 0.12f;      // its moment: out toward you, a touch bigger
constexpr float kBackDur = 0.62f, kLiftY = 0.172f;        // round to the back: its time; lifted clear over the stack
float stack_z(int d) { return kStackZ0 + static_cast<float>(S.n - 1 - d) * kStackGap; }
int front_card() {
  for (int i = 0; i < S.n; ++i)
    if (S.depth[i] == 0) return i;
  return -1;
}
// out of the pack (u 0..1): up out of the torn top, toward you clear of the stack, down onto its place at the front
XrVector3f file_path(int i, float u) {
  const float zs = stack_z(S.depth[i]);
  if (u < 0.45f) return {0.0f, -0.01f + smooth(u / 0.45f) * (kOutY + 0.01f), 0.0f};
  if (u < 0.65f) return {0.0f, kOutY, smooth((u - 0.45f) / 0.2f) * (zs + 0.012f)};
  const float v = smooth((u - 0.65f) / 0.35f);
  return {0.0f, kOutY * (1.0f - v), zs + 0.012f * (1.0f - v)};
}
// round to the back (u 0..1; its place already the back's): back into the stack's front and up clear of it, over to
// behind it, down into its place; *pop its moment going
XrVector3f back_path(int i, float u, float* pop) {
  const float zf = stack_z(0) + 0.004f, zb = stack_z(S.depth[i]) - 0.004f;
  if (u < 0.35f) {
    const float v = smooth(u / 0.35f);
    *pop = std::min(smooth(S.popK[i]), 1.0f - v);
    return {0.0f, kPopY * *pop + kLiftY * v, stack_z(0) + 0.004f * v + kPopZ * *pop};
  }
  *pop = 0.0f;
  if (u < 0.6f) return {0.0f, kLiftY, zf + (zb - zf) * smooth((u - 0.35f) / 0.25f)};
  const float v = smooth((u - 0.6f) / 0.4f);
  return {0.0f, kLiftY * (1.0f - v), zb + 0.004f * v};
}

// What to draw: the pictures (threads) for the pack (once: kept) and the cards.
// r220: a punched card's bits printed as its frame (BASE white, a TIZM BOOM yellow, SILVER / GOLD / BLACK their metal) - its two colours
void punch_bit_rgb(const Ref& r, uint32_t* f1, uint32_t* f2) {
  *f1 = r.parallel == kSilver ? 0xF4F6FAu : r.parallel == kGoldTizm ? 0xFFE27Au : r.parallel == kBlack ? 0x2A2436u : r.hit == kHitBoom ? 0xFFD400u : 0xFFFFFFu;
  *f2 = r.parallel == kSilver ? 0x9AA6BCu : r.parallel == kGoldTizm ? 0xB07A10u : r.parallel == kBlack ? 0xE0218Au : r.hit == kHitBoom ? 0xFF7A1Au : 0xDDE3EEu;
}
void ask_pictures(bool packToo) {
  const int gen = ++g_picGen;
  int total = 0;
  std::vector<Card> cards;
  std::vector<int> levels;  // r146: each card's level as it stands (its stars, its back's line)
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    total = g_setN;  // (r217: a set's count)
    for (int i = 0; i < S.n; ++i) {
      cards.push_back(g_set[S.refs[i].card]);
      levels.push_back(card_level(g_owned[S.refs[i].card]));
      // r147: a pack's card (in your collection already): its level moment as it turns over - a duplicate, or the copy that
      // took it up a level (a pack never has the same card twice: the copies before it, one fewer)
      const int copies = g_owned[S.refs[i].card].total();
      Owned before;
      before.n[kBase][0] = std::max(0, copies - 1);
      S.tagKind[i] = S.mode == kPackUp && copies >= 2 ? (levels.back() > card_level(before) ? 2 : 1) : 0;
      S.tagLevel[i] = levels.back();
      S.tagCopies[i] = copies;
      S.tagNs[i] = 0;
      // r220 (THE PUNCH): its die; a pack's Jam card with a real cut is punched as it's turned over, anything else is its cut already
      S.cutShape[i] = jam_shape_of(cards.back(), S.refs[i]);
      S.punchNs[i] = S.mode == kPackUp && S.cutShape[i] > 0 ? 0 : kPunchedLongAgo;
    }
  }
  const float waitS = S.mode == kTrade ? 0.8f : 4.0f;  // (r146: a trade-in's ten: its show shouldn't wait on big pictures)
  if (packToo) {
    auto pics = std::make_shared<Pics>();
    pics->rgba.resize(2);
    pics->w = {kPackW, kPackW};
    pics->h = {kPackH, kPackH};
    pics->gen = gen;
    S.packPics = pics;
    bool cached = false;
    const int pset = (S.packData.flags & kExqPack) != 0 ? 2 : ((S.packData.flags & kJamPack) != 0 ? 1 : 0);  // r217: a JAM '96 pack its own wrapper (r230: + EXQUISITE)
    {
      std::lock_guard<std::mutex> lock(g_packPxMutex);
      cached = g_packPxTotal[pset] == total && !g_packPx[pset][0].empty() && !g_packPx[pset][1].empty();
      if (cached) pics->rgba = {g_packPx[pset][0], g_packPx[pset][1]};
    }
    if (!cached) {
      pics->left = 2;
      std::thread([pics, total, pset]() {
        for (int side = 0; side < 2; ++side) {
          std::vector<uint8_t> px = draw_pack(side == 1, total, pset);
          {
            std::lock_guard<std::mutex> lock(g_packPxMutex);
            g_packPx[pset][side] = px;
            g_packPxTotal[pset] = total;
          }
          std::lock_guard<std::mutex> lock(pics->m);
          pics->rgba[side] = std::move(px);
          --pics->left;
        }
      }).detach();
    }
  }
  // the cards' pictures: their fronts, their backs, then (r138) their edges: a few pixels of the finish's edge colour, its
  // foil in their alpha (ready at once)
  // r147: then their level moments (kTagW x kTagH; a card with none: 4 x 4 see-through)
  // r220: then each card's back with its own die (a punched pack card's, drawn to swap in at its punch; 4 x 4 see-through for the rest)
  // and its cut-away bits' picture (lk::bits_px, quarter size; 4 x 4 none)
  auto pics = std::make_shared<Pics>();
  pics->rgba.resize(6 * S.n);
  pics->w.assign(6 * S.n, kFrontW);
  pics->h.assign(6 * S.n, kFrontH);
  std::vector<int> tags(static_cast<size_t>(S.n), 0), backShape(static_cast<size_t>(S.n), 0), punchShape(static_cast<size_t>(S.n), 0);
  std::vector<uint32_t> bitRgb(static_cast<size_t>(S.n) * 2, 0);
  int extra = 0;
  for (int i = 0; i < S.n; ++i) {
    const int sh = std::max(0, S.cutShape[i]);
    const bool punch = S.cutShape[i] > 0 && S.punchNs[i] == 0;
    backShape[static_cast<size_t>(i)] = punch ? 0 : sh;  // (the back it shows first: a punched card's crest, else its own die)
    punchShape[static_cast<size_t>(i)] = punch ? sh : 0;
    for (int k : {4 * S.n + i, 5 * S.n + i}) {
      pics->w[static_cast<size_t>(k)] = pics->h[static_cast<size_t>(k)] = 4;
      pics->rgba[static_cast<size_t>(k)].assign(4 * 4 * 4, 0);
    }
    if (punch) {
      pics->w[static_cast<size_t>(4 * S.n + i)] = kBackW, pics->h[static_cast<size_t>(4 * S.n + i)] = kBackH;
      pics->w[static_cast<size_t>(5 * S.n + i)] = lk::kBitsW, pics->h[static_cast<size_t>(5 * S.n + i)] = lk::kBitsH;
      extra += 2;
      punch_bit_rgb(S.refs[i], &bitRgb[static_cast<size_t>(2 * i)], &bitRgb[static_cast<size_t>(2 * i + 1)]);
    }
  }
  for (int i = 0; i < S.n; ++i) {
    pics->w[S.n + i] = kBackW;
    pics->h[S.n + i] = kBackH;
    const FinishLook& fl = kFinishLook[S.refs[i].parallel];
    pics->w[2 * S.n + i] = pics->h[2 * S.n + i] = 4;
    std::vector<uint8_t>& e = pics->rgba[2 * S.n + i];
    e.resize(4 * 4 * 4);
    // r230: an EXQUISITE card's edge - black stock, gilt where its front has a leaf band (lk::xq::look_of)
    uint32_t edge = fl.edge;
    float edgeFoil = fl.edgeFoil;
    if (i < static_cast<int>(cards.size()) && cards[static_cast<size_t>(i)].set == 2) {
      const lk::xq::Look xl = lk::xq::look_of(std::max(1, xrexq::tier_of(S.refs[i].serial)));
      edge = xl.band ? (xl.foil == lk::xq::kFSilver ? 0xC9CED6u : 0xC9A04Au) : 0x141416u;
      edgeFoil = xl.band ? 1.0f : 0.15f;
    }
    for (int p = 0; p < 16; ++p) {
      e[p * 4 + 0] = (edge >> 16) & 255;
      e[p * 4 + 1] = (edge >> 8) & 255;
      e[p * 4 + 2] = edge & 255;
      e[p * 4 + 3] = static_cast<uint8_t>(255 - std::lround(edgeFoil * 127.0f));
    }
    tags[static_cast<size_t>(i)] = S.tagKind[i];
    pics->w[3 * S.n + i] = S.tagKind[i] != 0 ? kTagW : 4;
    pics->h[3 * S.n + i] = S.tagKind[i] != 0 ? kTagH : 4;
    if (S.tagKind[i] == 0) pics->rgba[3 * S.n + i].assign(4 * 4 * 4, 0);
  }
  int tagCount = 0;
  for (int k : tags) tagCount += k != 0 ? 1 : 0;
  pics->left = 2 * S.n + tagCount + extra;
  pics->gen = gen;
  S.cardPics = pics;
  // three threads (the fronts on two, the backs on one): each picture's canvas is ~26 MB of floats while it's drawn
  const std::vector<Ref> refs(S.refs, S.refs + S.n);
  // r226 (Lance's JAM '96 BOWSER, a framed menu icon): the cards' big pictures FIRST in the queue, in the order they're turned over
  // (r225: each front's get() pushed its own to the front - the last asked first - behind nothing it could see); a front drawn without
  // its big picture (still being drawn after waitS) is noted and drawn again the moment it's in (late_fronts: swapped into the model)
  {
    std::vector<std::pair<std::string, bool>> order;
    for (size_t i = 0; i < cards.size(); ++i) order.push_back({cards[i].key, refs[i].shiny && pk_key(cards[i].key)});
    xrbig::first(order, kArt);
  }
  pics->late.assign(static_cast<size_t>(S.n), 0);
  pics->redrawn.assign(static_cast<size_t>(S.n), {});
  pics->redrawnMips.assign(static_cast<size_t>(S.n), {});
  pics->mips.assign(pics->rgba.size(), {});
  pics->cards = cards;
  pics->refs = refs;
  pics->levels = levels;
  pics->total = total;
  for (int t = 0; t < 2; ++t)
    std::thread([pics, cards, refs, levels, total, t, waitS]() {
      for (size_t i = static_cast<size_t>(t); i < cards.size(); i += 2) {
        bool big = true;
        std::vector<uint8_t> px = draw_front(cards[i], refs[i], total, waitS, kSpNone, levels[i], false, &big);
        std::vector<std::vector<uint8_t>> mp = mip_chain_front(px, kFrontW, kFrontH);  // r226: its small text crisper in the hand
        std::lock_guard<std::mutex> lock(pics->m);
        pics->rgba[i] = std::move(px);
        pics->mips[i] = std::move(mp);
        pics->late[i] = big ? 0 : 1;
        --pics->left;
      }
    }).detach();
  std::vector<int> tagLevel(S.tagLevel, S.tagLevel + S.n), tagCopies(S.tagCopies, S.tagCopies + S.n);
  std::thread([pics, cards, total, tags, tagLevel, tagCopies, backShape, punchShape, bitRgb]() {
    for (size_t i = 0; i < cards.size(); ++i) {
      // (r147: the plain back - nothing of its level before it's turned; r220: with its own die, or the crest it's punched from)
      std::vector<uint8_t> px = draw_back(cards[i], total, backShape[i]);
      std::lock_guard<std::mutex> lock(pics->m);
      pics->rgba[cards.size() + i] = std::move(px);
      --pics->left;
    }
    for (size_t i = 0; i < cards.size(); ++i) {  // r220: a punched card's own back and its bits
      if (punchShape[i] <= 0) continue;
      std::vector<uint8_t> b = draw_back(cards[i], total, punchShape[i]);
      std::vector<uint8_t> bits = lk::bits_px(punchShape[i], bitRgb[2 * i], bitRgb[2 * i + 1]);
      std::lock_guard<std::mutex> lock(pics->m);
      pics->rgba[4 * cards.size() + i] = std::move(b);
      pics->rgba[5 * cards.size() + i] = std::move(bits);
      pics->left -= 2;
    }
    for (size_t i = 0; i < cards.size(); ++i) {  // r147: its level moment's sticker
      if (tags[i] == 0) continue;
      std::vector<uint8_t> px = draw_tag(tagLevel[i], tagCopies[i], tags[i] == 2);
      std::lock_guard<std::mutex> lock(pics->m);
      pics->rgba[3 * cards.size() + i] = std::move(px);
      --pics->left;
    }
  }).detach();
}

// A grid model for xrfx::set_model: grids of nx x ny cells, each with its picture (tex) and its rows v0..v1 (a back
// mirrored: seen from behind).
struct GridPart {
  int tex, nx, ny;
  float v0, v1;
  bool mirror;
};
xrfx::Model grid_model(const std::shared_ptr<Pics>& pics, const std::vector<GridPart>& parts, bool keepPics) {
  xrfx::Model m;
  {
    std::lock_guard<std::mutex> lock(pics->m);
    for (size_t i = 0; i < pics->rgba.size(); ++i) {
      if (keepPics) m.texRgba.push_back(pics->rgba[i]);
      else m.texRgba.push_back(std::move(pics->rgba[i]));
      m.texW.push_back(static_cast<uint32_t>(pics->w[i]));
      m.texH.push_back(static_cast<uint32_t>(pics->h[i]));
    }
    if (!pics->mips.empty()) {  // r226: the fronts' own mips (mip_chain_front); the rest empty: the GPU's, as before
      m.texMips.resize(pics->rgba.size());
      for (size_t i = 0; i < pics->mips.size() && i < m.texMips.size(); ++i) {
        if (keepPics) m.texMips[i] = pics->mips[i];
        else m.texMips[i] = std::move(pics->mips[i]);
      }
    }
  }
  uint32_t base = 0;
  for (const GridPart& g : parts) {
    for (int i = 0; i <= g.ny; ++i)
      for (int j = 0; j <= g.nx; ++j) {
        const float u = static_cast<float>(j) / g.nx, v = static_cast<float>(i) / g.ny;
        m.uv.push_back(g.mirror ? 1.0f - u : u);
        m.uv.push_back(g.v0 + (g.v1 - g.v0) * v);
      }
    xrfx::ModelPart part;
    part.tex = g.tex;
    part.firstIndex = static_cast<uint32_t>(m.indices.size());
    for (int i = 0; i < g.ny; ++i)
      for (int j = 0; j < g.nx; ++j) {
        const uint32_t a = base + static_cast<uint32_t>(i * (g.nx + 1) + j), b = a + 1, c = a + g.nx + 1, d = c + 1;
        m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
      }
    part.indexCount = static_cast<uint32_t>(m.indices.size()) - part.firstIndex;
    m.parts.push_back(part);
    base += static_cast<uint32_t>((g.nx + 1) * (g.ny + 1));
  }
  m.vertexCount = base;
  return m;
}

// r226 (Lance 11-12, his JAM '96 BOWSER "the art just looks wonky": a square picture window, a zoomed face over orange - Bowser's
// Charged menu icon, 128 px, opaque = r141's framed photo; his log: the card's pictures were up before "bowser_1024 drawn in 8413 ms",
// so the front got the icon and kept it): the cards out (frame thread, the card model up). A front drawn with its portrait whose big
// picture is in now is drawn again on a thread (the same card, finish, level - its big picture this time) and swapped into the card
// model (xrfx::set_model_tex: that one picture; the card's place, flip, foil, tags untouched). Cheap when nothing is late: one
// counter compared a frame.
void late_fronts() {
  const std::shared_ptr<Pics> pics = S.cardPics;
  if (!pics) return;
  struct Up {
    int i;
    std::vector<uint8_t> px;
    std::vector<std::vector<uint8_t>> mips;
  };
  std::vector<Up> up;
  std::vector<int> start;
  {
    std::lock_guard<std::mutex> lock(pics->m);
    for (size_t i = 0; i < pics->late.size(); ++i)
      if (pics->late[i] == 3 && !pics->redrawn[i].empty()) {
        up.push_back({static_cast<int>(i), std::move(pics->redrawn[i]), std::move(pics->redrawnMips[i])});
        pics->redrawn[i].clear();
        pics->redrawnMips[i].clear();
        pics->late[i] = 0;
      }
    const unsigned g = xrbig::generation();
    bool any = false;
    for (uint8_t l : pics->late) any = any || l == 1;
    if (any && g != pics->bigGen) {
      pics->bigGen = g;
      for (size_t i = 0; i < pics->late.size() && i < pics->cards.size(); ++i) {
        if (pics->late[i] != 1) continue;
        const bool sh = pics->refs[i].shiny && pk_key(pics->cards[i].key);
        if (xrbig::ready(pics->cards[i].key, sh, kArt)) {
          pics->late[i] = 2;
          start.push_back(static_cast<int>(i));
        } else {
          xrbig::prefetch(pics->cards[i].key, sh, kArt);  // (still queued: nothing; let go from memory: asked again)
        }
      }
    }
  }
  for (const Up& u : up) {
    const bool ok = u.i < S.n && xrfx::set_model_tex(kCardSlot, u.i, u.px, kFrontW, kFrontH, &u.mips);
    const Card& c = pics->cards[static_cast<size_t>(u.i)];
    CLOG("card %d (%s%s): its big picture came in after the cards were up - its front drawn again%s", u.i, c.set == 1 ? "JAM '96 " : "",
         c.key.c_str(), ok ? " and swapped in" : " - the swap failed (its first front stays)");
  }
  for (int i : start)
    std::thread([pics, i]() {
      bool big = false;
      std::vector<uint8_t> px = draw_front(pics->cards[static_cast<size_t>(i)], pics->refs[static_cast<size_t>(i)], pics->total, 2.0f, kSpNone,
                                           pics->levels[static_cast<size_t>(i)], true, &big);
      std::vector<std::vector<uint8_t>> mp = big ? mip_chain_front(px, kFrontW, kFrontH) : std::vector<std::vector<uint8_t>>();
      std::lock_guard<std::mutex> lock(pics->m);
      if (big) {
        pics->redrawn[static_cast<size_t>(i)] = std::move(px);
        pics->redrawnMips[static_cast<size_t>(i)] = std::move(mp);
        pics->late[static_cast<size_t>(i)] = 3;
      } else {
        pics->late[static_cast<size_t>(i)] = 1;  // (let go from memory meanwhile: looked at again next frame)
        pics->bigGen = ~0u;
      }
    }).detach();
}

void drop_cards_model(bool fxReady) {
  if (fxReady && S.cardModel >= 0) xrfx::set_model(kCardSlot, xrfx::Model{});
  S.cardModel = -1;
}
void drop_models(bool fxReady) {
  if (fxReady && S.packModel >= 0) xrfx::set_model(kPackSlot, xrfx::Model{});
  S.packModel = -1;
  drop_cards_model(fxReady);
}

void end_scene(bool fxReady) {
  drop_models(fxReady);
  vault_end(fxReady);  // r230
  god_end(fxReady);       // r149 (before the 1/1 world's: the same slot)
  slab_end(fxReady);      // r152
  S.outSlab = -1;
  S.gradeNs = 0;
  specials_end(fxReady);  // r138
  for (bool& b : S.boomShown) b = false;  // r150
  for (int h = 0; h < 2; ++h) S.hold[h] = -1;
  S.mode = kNone;
  S.n = 0;
  S.pack.on = false;
  S.packPics.reset();
  S.cardPics.reset();
  S.sparkleT = 0.0f;
  g_sceneMode = 0;
  ++g_gen;
}

// The scene stands where you are: in front of you, a little below your eyes.
void set_anchor() {
  S.anchor = S.head.position;
  const XrVector3f f = q_rot(S.head.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
  const float l = std::sqrt(f.x * f.x + f.z * f.z);
  S.fwd = l > 1e-3f ? XrVector3f{f.x / l, 0.0f, f.z / l} : XrVector3f{0.0f, 0.0f, -1.0f};
  S.right = {-S.fwd.z, 0.0f, S.fwd.x};
}

void sheet_reset(Sheet& s, float w, float h) {
  s = Sheet{};
  s.on = true;
  s.w = w;
  s.h = h;
  s.rigid = w == kCardWm;  // r138: the cards are stiff; the pack's pouch still gives a little
}

int special_kind(const Ref& r);                                    // (below)
void cel_prefetch(const std::vector<std::pair<Card, bool>>& cards);  // r193 (below)
// A pack opened: its cards into your collection at once (the opening is the show; a crash mid-way loses nothing), the
// pack up into your hands.
bool open_pack(uint64_t now) {
  Pack p;
  std::vector<std::pair<std::string, bool>> thisArt, nextArt;  // r226
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_packs.empty()) return false;
    p = g_packs.front();
    g_packs.erase(g_packs.begin());
    const long long t = static_cast<long long>(std::time(nullptr));
    S.pullsAdded = 0;
    S.packNew.clear();
    for (const Ref& r : p.cards) {
      if (r.card < 0) continue;
      add_owned(r, +1);
      if (g_newCards.insert(g_set[r.card].id).second) S.packNew.push_back(g_set[r.card].id);  // r149: NEW in the binder
      if (big_pull(r)) {
        g_pulls.push_back(Pull{t, r});
        ++S.pullsAdded;
      }
    }
    ++g_opened;
    S.godAdded = (p.flags & kGodPack) != 0;  // r149
    S.godDone = false;
    if (S.godAdded) {
      ++g_godPacks;
      g_godWhen = t;
    }
    save_collection();
    bonuses_update();  // r146: its cards' levels / sets
    g_packCount = static_cast<int>(g_packs.size());
    std::string what;
    for (const Ref& r : p.cards)
      if (r.card >= 0) what += std::string(what.empty() ? "" : ", ") + pull_text(r) + " (" + kRarityShort[g_set[r.card].rarity] + ")";
    CLOG("pack %d opened: %s; %d waiting", p.serial, what.c_str(), static_cast<int>(g_packs.size()));
    // r226: its cards' big pictures first in the queue now (in turn-over order), while it's gripped and ripped; the next pack's after
    for (const Ref& r : p.cards)
      if (r.card >= 0) thisArt.push_back({g_set[r.card].key, r.shiny && pk_key(g_set[r.card].key)});
    next_packs_art(1, &nextArt);
  }
  xrbig::first(thisArt, kArt);
  for (const auto& kp : nextArt) xrbig::prefetch(kp.first, kp.second, kArt);
  S.packData = p;
  g_openSet = (p.flags & kExqPack) != 0 ? 2 : ((p.flags & kJamPack) != 0 ? 1 : 0);  // r217; r230: 2 an EXQUISITE pack
  set_anchor();
  S.mode = kPackUp;
  S.phase = kWaiting;
  S.t0 = S.phaseNs = now;
  S.cardsNs = 0;
  S.n = 0;
  for (const Ref& r : p.cards)
    if (r.card >= 0) S.refs[S.n++] = r;
  {  // r193: its card figures' celebrations made now, before any card can be turned (cel_prefetch)
    std::vector<std::pair<Card, bool>> figs;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (int i = 0; i < S.n; ++i) {
      const int k = special_kind(S.refs[i]);
      if (k == kSpWindow || k == kSpGlass || k == kSpPop) figs.push_back({g_set[S.refs[i].card], S.refs[i].shiny});
    }
    cel_prefetch(figs);
  }
  S.revealed = 0;
  S.torn = false;
  S.wrapFall = false;
  S.shredT = 0.0f;
  S.filed = 0;
  for (int i = 0; i < kSheets; ++i) {
    S.depth[i] = i;
    S.zAt[i] = S.popK[i] = S.backU[i] = 0.0f;
    S.faceUp[i] = false;
    S.backNs[i] = 0;
  }
  sheet_reset(S.pack, kPackWm, kPackHm);
  // it comes up into your hands (between them when both are in front of you), else in front of you
  XrVector3f at = v_add(v_add(S.anchor, v_mul(S.fwd, 0.40f)), XrVector3f{0.0f, -0.16f, 0.0f});
  if (S.handHave[0] && S.handHave[1]) {
    const XrVector3f mid = v_mul(v_add(S.handAt[0], S.handAt[1]), 0.5f);
    const XrVector3f d = v_sub(mid, S.anchor);
    const float ahead = d.x * S.fwd.x + d.z * S.fwd.z;
    if (ahead > 0.15f && ahead < 0.7f && v_len(v_sub(S.handAt[0], S.handAt[1])) < 0.6f) at = v_add(mid, XrVector3f{0.0f, 0.05f, 0.0f});
  }
  S.pack.tPos = at;
  S.pack.pos = v_add(at, XrVector3f{0.0f, -0.35f, 0.0f});
  S.pack.lastPos = S.pack.pos;
  S.pack.scale = 0.2f;
  S.pack.tScale = 1.0f;
  S.pack.shown = true;
  for (int i = 0; i < S.n; ++i) {
    sheet_reset(S.card[i], kCardWm, kCardHm);
    S.card[i].flip = 3.14159265f;
  }
  g_sceneMode = 1;
  ask_pictures(S.packModel < 0);
  specials_plan(now, true);  // r138: its auras (r139: each special's 3D made as its card comes to the stack's front)
  if (S.godAdded) god_prepare();  // r149
  card_sounds_preload();          // r149: the tiers' sounds
  if ((p.flags & kExqPack) != 0) vault_begin(now, p.flags, p.serial);  // r230: THE VAULT first (S.phase kVault: the latches, the lid)
  buzz(0.35f, 0.08f);
  ++g_gen;
  return true;
}

// The pack put back unopened (B before it's torn, the hub closed): its cards out of your collection again.
void unopen() {
  std::lock_guard<std::mutex> lock(g_mutex);
  for (const Ref& r : S.packData.cards) add_owned(r, -1);
  for (const std::string& k : S.packNew) g_newCards.erase(k);  // r149: never seen
  S.packNew.clear();
  for (int i = 0; i < S.pullsAdded && !g_pulls.empty(); ++i) g_pulls.pop_back();
  S.pullsAdded = 0;
  g_packs.insert(g_packs.begin(), S.packData);
  g_opened = std::max(0, g_opened - 1);
  if (S.godAdded) g_godPacks = std::max(0, g_godPacks - 1);  // r149
  S.godAdded = false;
  save_collection();
  bonuses_update();  // r146
  g_packCount = static_cast<int>(g_packs.size());
  CLOG("pack %d put back unopened", S.packData.serial);
}

// A card out of the binder, in front of you (from its pocket on the hub's picture).
// r152: `slab` a TIZM GRADING slab of it to take out (g_slabs' place), -1 never one, -2 (the binder) its best slab back
// when it has one (its pocket shows it) - the slab with its card sealed in it
void look_card(int card, uint64_t now, const XrVector3f* from, int slab = -2) {
  set_anchor();
  S.mode = kOut;
  S.phase = kStack;
  S.t0 = S.phaseNs = now;
  S.n = 1;
  S.pack.on = false;
  S.outSlab = -1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    // your finest copy of it: its finest finish (a shiny one of that if you have it), a GOLD's lowest number
    // (r152: of the copies not sealed; its finest a case hit: that copy)
    const Owned& o = g_owned[card];
    Ref r;
    r.card = card;
    r.parallel = std::max(0, o.free_best());
    r.shiny = o.n[r.parallel][1] > 0;
    r.serial = r.parallel == kBlack ? 1 : (r.parallel == kGoldTizm && !o.golds.empty() ? o.golds.front() : 0);
    if (r.parallel == o.free_best() && o.n[r.parallel][0] + o.n[r.parallel][1] == 0)
      for (const Ref& h : o.hits)
        if (h.parallel == r.parallel) r = h;
    if (g_set[card].set == 2) {  // r230: an EXQUISITE card - its best numbered copy (its highest tier, the lowest number)
      const Ref b = exq_best(o, false);
      if (b.card >= 0) r = b;
    }
    if (slab == -2 && o.bestGrade > 0)  // (the binder's pocket shows its best slab: that one)
      for (size_t k = 0; k < g_slabs.size(); ++k)
        if (g_slabs[k].ref.card == card && g_slabs[k].left == 0 && g_slabs[k].grade == o.bestGrade && g_slabs[k].cert == o.bestCert)
          slab = static_cast<int>(k);
    if (slab >= 0 && slab < static_cast<int>(g_slabs.size()) && g_slabs[static_cast<size_t>(slab)].left == 0) {
      Slab& s = g_slabs[static_cast<size_t>(slab)];
      if (!s.seen) {
        s.seen = true;
        save_collection();
      }
      S.outSlab = slab;
      S.slab = s;
      r = s.ref;
      CLOG("TIZM GRADING slab out: %s, %s %d, cert %u", pull_text(r).c_str(), kGradeName[s.grade], s.grade, s.cert);
    }
    S.refs[0] = r;
    CLOG("looking at card %d: %s", g_set[card].num, pull_text(r).c_str());
    if (g_newCards.erase(g_set[card].id) > 0) save_collection();  // r149: seen
  }
  sheet_reset(S.card[0], kCardWm, kCardHm);
  Sheet& s = S.card[0];
  s.tPos = v_add(v_add(S.anchor, v_mul(S.fwd, 0.40f)), XrVector3f{0.0f, -0.04f, 0.0f});
  s.pos = from != nullptr ? *from : v_add(s.tPos, XrVector3f{0.0f, -0.3f, 0.0f});
  s.lastPos = s.pos;
  s.scale = 0.3f;
  s.tScale = S.outSlab >= 0 ? 1.15f : 1.45f;  // (r152: a slab a little smaller: its case and label round it)
  g_sceneMode = 2;
  drop_cards_model(true);
  ask_pictures(false);
  specials_plan(now, true);  // r138: a special card's 3D out of the binder too (r152: in a slab only a window's room)
  special_focus(0, now, true);
  ++g_gen;
}
// r152: a slab's BACK FROM GRADING moment (the first slab back you haven't seen): out of its padded envelope in front of
// you, its grade flipping up (slab_frame)
void grade_reveal(int k, uint64_t now) {
  int card = -1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (k < 0 || k >= static_cast<int>(g_slabs.size())) return;
    card = g_slabs[static_cast<size_t>(k)].ref.card;
  }
  look_card(card, now, nullptr, k);
  if (S.outSlab != k) return;
  Sheet& s = S.card[0];
  S.gradeNs = now;
  S.gradeHit = false;
  S.revealAt = s.tPos;
  s.free = true;
  s.pos = s.lastPos = v_add(s.tPos, XrVector3f{0.0f, -0.6f, 0.0f});
  s.scale = s.tScale;
  g_sceneMode = 4;
  card_sound(1);
  CLOG("TIZM GRADING: back from grading - %s", pull_text(S.slab.ref).c_str());
}

// r146: a trade-in's show (Lance's pick: "cards slide into a slot, a pack pops out"): its ten cards come off the hub's
// picture one after another, line up over a glittering slot in front of you and slide down into it; the pack it got
// pops up out of the slot (a burst, coins ringing, a buzz), spins, and flies into the binder with your other packs. The
// trade is saved before it starts (trade_in); B / A skips it.
void trade_show(const std::vector<int>& cards, uint64_t now) {
  set_anchor();
  S.mode = kTrade;
  S.phase = kWaiting;
  S.t0 = S.phaseNs = now;
  S.cardsNs = 0;
  S.n = std::min(kSheets, static_cast<int>(cards.size()));
  S.torn = S.wrapFall = false;
  S.shredT = 0.0f;
  S.slot = v_add(v_add(S.anchor, v_mul(S.fwd, 0.42f)), XrVector3f{0.0f, -0.16f, 0.0f});
  S.tradeIn = 0;
  S.popNs = 0;
  S.tradeSkip = false;
  for (int i = 0; i < S.n; ++i) {
    Ref r;
    r.card = cards[static_cast<size_t>(i)];
    S.refs[i] = r;
    sheet_reset(S.card[i], kCardWm, kCardHm);
    Sheet& s = S.card[i];
    s.flip = 0.0f;
    s.scale = 0.3f;
    s.free = true;  // (placed by the show, not eased)
    S.tradeSlid[i] = false;
    // off the hub's picture (its trade-ins' grid: two rows of five, as parts of its width / height from its middle), else
    // from low in front of you
    float nx = -0.154f + (static_cast<float>(i % 5) - 2.0f) * 0.09f, ny = i < 5 ? 0.12f : -0.08f;
    {  // r157: the trade-ins' grid where the hub draws it now (its page area, right of the rail)
      const float k = g_fitK.load();
      nx = (g_fitOx.load() + (nx + 0.5f) * 2160.0f * k) / 2160.0f - 0.5f;
      ny = 0.5f - (g_fitOy.load() + (0.5f - ny) * 1300.0f * k) / 1300.0f;
    }
    S.tradeFrom[i] = S.hubValid && S.hubW > 0.0f
                         ? v_add(S.hubPose.position, q_rot(S.hubPose.orientation, XrVector3f{nx * S.hubW, ny * S.hubH, 0.04f}))
                         : v_add(S.slot, XrVector3f{(static_cast<float>(i % 5) - 2.0f) * 0.1f, -0.3f, 0.0f});
    s.pos = s.lastPos = S.tradeFrom[i];
  }
  sheet_reset(S.pack, kPackWm, kPackHm);
  S.pack.shown = false;
  S.pack.scale = 0.0f;
  S.pack.free = true;
  S.pack.pos = S.pack.lastPos = S.slot;
  g_sceneMode = 3;
  drop_cards_model(true);
  ask_pictures(S.packModel < 0);
  ++g_gen;
}

void put_away(uint64_t now) {
  if (S.mode == kNone || S.mode == kAway) return;
  if (S.mode == kTrade) {  // r146: a trade-in's show: skipped (its pack is already yours)
    S.tradeSkip = true;
    return;
  }
  if (S.mode == kPackUp && (S.phase == kWaiting || S.phase == kVault)) unopen();  // (r230: still in its case: unopened)
  S.mode = kAway;
  S.t0 = now;
  for (int h = 0; h < 2; ++h) S.hold[h] = -1;
  for (int i = 0; i < S.n; ++i) S.card[i].hands = 0;
  S.pack.hands = 0;
  g_sceneMode = 0;
  ++g_gen;
}

// r139: ripped (the trigger of the hand not holding it, `hand`; A: -1): its top strip torn off along the crimp, flying off
// to that hand's side, foil shreds off the tear, the rip's sound, a buzz in both hands; it stays in the hand that holds it
// (the stack's holder now) and its cards start out of it
void tear(uint64_t now, int hand) {
  if (S.mode != kPackUp || S.phase != kWaiting) return;
  S.phase = kFiling;
  S.phaseNs = now;
  S.torn = true;
  S.filed = 0;
  for (int i = 0; i < S.n; ++i) {
    S.depth[i] = i;
    S.zAt[i] = stack_z(i);
    S.popK[i] = S.backU[i] = 0.0f;
    S.faceUp[i] = false;
    S.backNs[i] = 0;
    S.card[i].shown = false;
  }
  const XrQuaternionf q = sheet_q(S.pack);
  const float s = S.pack.scale, side = hand == 0 ? -1.0f : 1.0f;
  S.stripBase = s;
  S.stripPos = v_add(S.pack.pos, q_rot(q, XrVector3f{0.0f, (0.5f - 0.5f * kTearV) * kPackHm * s, 0.0f}));
  S.stripVel = q_rot(q, XrVector3f{0.55f * side, 0.9f, 0.25f});
  S.stripQ = q;
  S.stripSpin = 9.0f * side;
  S.stripScale = 1.0f;
  S.shredAt = v_add(S.pack.pos, q_rot(q, XrVector3f{0.0f, (0.5f - kTearV) * kPackHm * s, 0.006f}));
  S.shredT = 0.45f;
  card_sound(0);
  buzz(0.8f, 0.18f);
  if (S.godAdded) god_tell(now);  // r149: a GOD PACK's tell
  CLOG("the pack ripped open (%s)", hand == 0 ? "left trigger" : (hand == 1 ? "right trigger" : "A"));
}

// r219: every frame of the stack: the hit being built up - its heartbeat (made in code, xr_pulls.h) with a buzz in both hands each
// beat, harder as it turns; a 1/1's room going dark, slammed back at its reveal with one white flash (g_pullDark / g_pullFlash)
void pull_frame(uint64_t now) {
  if (g_pullCard < 0 || g_pullCard >= S.n || S.card[g_pullCard].pull.tier == 0) {
    g_pullCard = -1;
    g_pullDark = 0.0f;
    g_pullFlash = 0.0f;
    return;
  }
  const Sheet& s = S.card[g_pullCard];
  const float t = now >= s.flipNs ? secs(now, s.flipNs) : 0.0f;
  static std::vector<short> s_lub, s_dub;
  if (s_lub.empty()) {
    xrpulls::heart_pcm(false, &s_lub);
    xrpulls::heart_pcm(true, &s_dub);
  }
  xrpulls::beats(s.pull, g_pullBeatT, t, [&](float, float amp, float gain, bool dub) {
    const std::vector<short>& pcm = dub ? s_dub : s_lub;
    PortCardSound(pcm.data(), static_cast<int>(pcm.size()), gain);
    buzz(amp, dub ? 0.05f : 0.07f);
  });
  g_pullBeatT = std::max(g_pullBeatT, t);
  g_pullDark = xrpulls::dark(s.pull, t);
  g_pullFlash = xrpulls::flash(s.pull, t - xrpulls::reveal_at(s.pull));
  if (t > xrpulls::total(s.pull) + 0.6f) {  // (its build-up + its flash over: the lift eases back by itself)
    g_pullCard = -1;
    g_pullDark = 0.0f;
    g_pullFlash = 0.0f;
  }
}

// r139: a pull of the trigger (or A) with the stack in your hand: its front card turned over - out toward you, its
// moment (reveal_fx as its face comes round) - or, turned over already, put round to the back and the next card's back
// (its tell) to the front
void stack_step(uint64_t now) {
  if (S.mode != kPackUp || S.phase != kStack) return;
  const int f = front_card();
  if (f < 0) return;
  for (int i = 0; i < S.n; ++i)
    if (S.backNs[i] != 0 && secs(now, S.backNs[i]) < 0.4f * kBackDur) return;  // (one still lifting off the stack)
  Sheet& s = S.card[f];
  if (s.flipDur > 0.0f) {  // (still turning over)
    // r219: a pull of the trigger during a hit's slow turn = SNAP NOW (on to its snap; the heartbeats it skipped stay unplayed)
    if (s.pull.tier > 0 && S.faceUp[f] && now >= s.flipNs) {
      const float t = secs(now, s.flipNs), edge = s.pull.lift + s.pull.slow;
      if (t > s.pull.lift && t < edge) {
        s.flipNs -= static_cast<uint64_t>(static_cast<double>(edge - t) * 1e9);
        g_pullBeatT = edge + 1.0f;
        CLOG("big pull: snapped now (%.2f s into its %.2f s build-up)", t, edge);
      }
    }
    return;
  }
  if (!S.faceUp[f]) {
    S.faceUp[f] = true;
    ++S.revealed;
    // r219 (BIG PULLS): a hit's slow-mo build-up (xr_pulls.h: it lifts toward you, turns slowly to edge-on with a heartbeat and the
    // controllers pulsing, then snaps round - its moment where the snap passes half way); a BASE card turns as it always did
    const xrpulls::Plan pl = pull_plan_of(f);
    if (pl.tier > 0) {
      start_flip(s, 0.0f, xrpulls::total(pl), now, true);
      s.pull = pl;
      g_pullCard = f;
      g_pullBeatT = -1.0f;
      CLOG("big pull: %s - build-up tier %d%s: lift %.2f + slow %.2f + snap %.2f s (the reveal at %.2f s), heartbeat %.0f-%.0f bpm, buzz %.2f-%.2f%s, party %d%s%s",
           pull_text(S.refs[f]).c_str(), pl.tier, pl.notches == 2 ? " (two notches)" : (pl.notches == 1 ? " (a notch)" : ""), pl.lift, pl.slow, pl.snap,
           xrpulls::reveal_at(pl), pl.bpm0, pl.bpm1, pl.amp0, pl.amp1, pl.dark ? ", the room goes dark" : "", pl.party, *pl.word ? " - " : "", pl.word);
    } else {
      start_flip(s, 0.0f, 0.5f, now + 150000000ull, true);  // (out toward you first, then over)
    }
    card_sound(2);
    return;
  }
  for (int i = 0; i < S.n; ++i)
    if (i != f) --S.depth[i];
  S.depth[f] = S.n - 1;
  S.backNs[f] = now;
  card_sound(3);
  buzz(0.15f, 0.03f);
  special_focus(front_card(), now, true);
}

// The effects as a card's face comes round, by its finish (kFinishLook): a BLACK 1/1 or a shiny the big show (a flash,
// rings of light, sparks, the fanfare), a GOLD /10 a long sparkle, a SILVER a sparkle; r138: a BASE card none (only a
// parallel shines or sparkles: Lance "i want a CLEAR distinction between whats base and whats a parralell"), a tap of
// the controllers, a little more for a rare / super rare character. A guest's own voice (its character select line) for
// a parallel, a shiny, or a rare or better character; the controllers buzz to match.
void punch_start(int i, uint64_t now);  // r220 (below)
void reveal_fx(int i) {
  if (S.mode == kPackUp) punch_start(i, mono_ns());  // r220: THE PUNCH - a JAM '96 card's crest snaps to its real cut, here with its reveal
  const Ref r = S.refs[i];
  Card c;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    c = g_set[r.card];
  }
  const Sheet& s = S.card[i];
  const FinishLook& fl = kFinishLook[r.parallel];
  S.sparkleAt = v_sub(s.pos, XrVector3f{0.0f, 0.5f * s.h * s.scale, 0.0f});
  const bool show = fl.show || r.shiny;
  // r149: each tier its own reveal sound (BLACK > a case hit > GOLD > SILVER > a rare character > BASE; a shiny's sparkle
  // cascade on top); without the files (an old install) the r138 chimes and the evolution fanfare as before
  const int xt = c.set == 2 ? xrexq::tier_of(r.serial) : 0;  // r230: an EXQUISITE card's tier
  g_lastRevealNs = mono_ns();
  if (xt > 0 && S.mode == kPackUp) {  // r230: its foil serial stamped at the reveal (r220's press, made in code)
    static std::vector<short> s_press;
    if (s_press.empty()) xrpulls::die_press_pcm(&s_press);
    PortCardSound(s_press.data(), static_cast<int>(s_press.size()), xt >= xrexq::kGoldLeaf ? 0.75f : 0.5f);
  }
  const char* tier = r.parallel == kBlack ? "black"
                     : r.hit == kHitBoom ? "boom"
                     : r.hit == kHitHome ? "hometown"
                     : r.hit == kHitSigned ? "signed"
                     : r.parallel == kGoldTizm ? "gold"
                     : r.parallel == kSilver ? "silver"
                     : c.rarity >= kRare ? "rare" : "base";
  const bool tierWav = play_wav(tier, 1.0f);
  if (tierWav && r.shiny) play_wav("shiny", 0.9f);
  if (r.hit == kHitBoom) special_boom_start(i);  // r150: its words pop out on its music's brass hits
  if (show) {
    S.sparkleT = 3.0f;
    S.sparkleR = 0.16f;
    S.burst = true;
    S.burstRgb = r.parallel == kBlack ? 0xB4BAC4 : c.c1;
    S.burstAt = S.sparkleAt;
    if (!tierWav) PortEvoMusic(c.key.compare(0, 4, "mua_") == 0 ? 3 : 1);  // the fanfare (the evolution's; a hero's from Ultimate Alliance)
    buzz(1.0f, 0.45f);
  } else if (fl.sparkleS > 0.0f) {
    S.sparkleT = fl.sparkleS;
    S.sparkleR = fl.sparkleR;
    buzz(fl.buzz, fl.buzzS);
  } else {
    buzz(c.rarity == kSuper ? 0.4f : (c.rarity == kRare ? 0.3f : fl.buzz), c.rarity >= kRare ? 0.08f : fl.buzzS);
  }
  const bool special = show || r.parallel != kBase;
  // r141: a BLACK 1/1 turned over in a pack for the first time: its life-size cameo (its voice comes with its celebration)
  const bool cameo = r.parallel == kBlack && c.set != 2 && S.mode == kPackUp && cameo_ask(i, c.id);  // (r230: an EXQUISITE 1/1 has no cameo - flat)
  if ((special || c.rarity >= kRare) && c.roster >= 0 && !cameo)
    PortGuestVoiceSay(c.roster, "select,cheer,taunt,quote", special || c.rarity == kSuper ? 1.0f : 0.8f);
  else if ((special || c.rarity >= kRare) && c.klass >= 0 && !cameo)
    PortStrikersVoiceSay(c.klass);  // r141: a Strikers character's own line too (the game's character voice)
  if (!tierWav) special_chime(r);  // r138: a special's own chime (r149: the tier sounds' fallback)
  special_spot(i, mono_ns() + 4500000000ull);  // r138: a BLACK's spotlight
  if (S.tagKind[i] != 0 && S.mode == kPackUp) {  // r147: its level moment (level_moment), as its face comes round
    S.tagNs[i] = mono_ns();
    if (S.tagKind[i] == 2) level_sound();
    buzz(S.tagKind[i] == 2 ? 0.45f : 0.2f, 0.06f);
    CLOG("%s: %s, LV %d (%d copies)", pull_text(r).c_str(), S.tagKind[i] == 2 ? "level up" : "duplicate", S.tagLevel[i], S.tagCopies[i]);
  }
  CLOG("turned over: %s (%s)", pull_text(r).c_str(), kRarityName[c.rarity]);
  // r219 (BIG PULLS): its party in your room (r207's casino party pieces, xr_goalfx pull_party: its coins + confetti out of your hand,
  // the giant word 2 m out, the siren, the fireworks) + the casino's coin clips; a 1/1's own show held back till the party's peak
  const xrpulls::Plan& pl = s.pull;
  if (S.mode == kPackUp && pl.tier > 0) {
    if (pl.party > 0) {
      xrgoalfx::pull_party(pl.party, pl.word, s.pos, S.head, pull_floor());
      play_wav(pl.party >= xrpulls::kPartyBig ? "casino_coins_pour_tray" : "casino_coins_jingle", pl.party >= xrpulls::kPartyBig ? 0.7f : 0.6f);
      CLOG("big pull: its party - %s%s%s", pl.party == 4 ? "JACKPOT" : (pl.party == 3 ? "HUGE" : (pl.party == 2 ? "BIG" : "a pop")), *pl.word ? " - " : "", pl.word);
    }
    if (pl.dark) g_pullGateNs = mono_ns() + static_cast<uint64_t>(xrpulls::kAfterParty * 1e9f);
  }
  if (S.mode == kPackUp && S.godAdded && !S.godDone && S.revealed >= S.n) god_last(mono_ns());  // r149: its show next
}

// r138: a finish's extra draws with its card each frame - an aura round it, things popping out of it, depth inside it,
// particles; `back`: its back is to you (still to be turned over). None yet: the special treatments being chosen with
// Lance plug in here (with kFinishLook). Today's parallels show by their foil and their reveal.
void finish_frame(const Sheet& s, const Ref& r, const XrQuaternionf& q, bool back, float dt) {
  (void)s;
  (void)r;
  (void)q;
  (void)back;
  (void)dt;
}

// r138: a card as stiff card stock (Lance: "the cards, you made flimsy"): its front and its back flat, kCardT apart, and
// its edge all round (the rounded rectangle its pictures' see-through corners cut: kCornerR of its width) facing out,
// into `out` (kCardVerts x 6 floats: its front grid, its back grid, its edge's two rows), at `q`, `pos`, `scale`; all at
// one point when not shown.
// r217: a JAM '96 card's die-cut edge (lk::crest_pts' outline, the shape its pictures' alpha cut): kEdgeSegs + 1 points round it
// (the last the first again), counter-clockwise from its right side's top as the rounded card's, and their outward normals - in
// card metres (W x H, x right, y up)
// r220: a JAM '96 die's edge (lk::jamcut: kEdgeSegs points, the same order as the crest's below - its right side at y 800, up, over the
// top right to left, down, along the foot), `morph` 0 the crest it punches from (point for point: kCrestFor) .. 1 its cut; card metres
void cut_edge(int shape, float morph, float W, float H, XrVector3f* p, XrVector3f* n) {
  const lk::jamcut::ShapeDef& sd = lk::jamcut::kShape[std::max(0, std::min(lk::jamcut::kShapes - 1, shape))];
  const float* a = sd.crestFor ? sd.crestFor : lk::jamcut::kShape[0].loop;
  const float* b = sd.loop ? sd.loop : lk::jamcut::kShape[0].loop;
  const float m = clampf(morph, 0.0f, 1.0f);
  for (int k = 0; k <= kEdgeSegs; ++k) {
    const int i = k % kEdgeSegs;
    const float x = a[2 * i] + (b[2 * i] - a[2 * i]) * m, y = a[2 * i + 1] + (b[2 * i + 1] - a[2 * i + 1]) * m;
    p[k] = XrVector3f{(x / kCW - 0.5f) * W, (0.5f - y / kCH) * H, 0.0f};
  }
  for (int k = 0; k <= kEdgeSegs; ++k) {
    const XrVector3f q0 = p[(k + kEdgeSegs - 1) % kEdgeSegs], q1 = p[(k + 1) % kEdgeSegs];
    const float tx = q1.x - q0.x, ty = q1.y - q0.y, l = std::max(1e-6f, std::sqrt(tx * tx + ty * ty));
    n[k] = XrVector3f{ty / l, -tx / l, 0.0f};
  }
}
// (r217's crest, analytic: kept for the r217 PC proof's edge timing - the cards use cut_edge's crest, shape 0)
void crest_edge(float W, float H, XrVector3f* p, XrVector3f* n) {
  std::vector<float> px;  // (front px: x right, y down)
  auto add = [&](float x, float y) { px.push_back(x), px.push_back(y); };
  const float cw = kCW, ch = kCH, r = 48.0f, rs = 12.0f, crestH = static_cast<float>(lk::kCrest);
  auto top = [&](float x) { const float u = (x - cw / 2) / (cw * 0.30f); return crestH * (1.0f - std::exp(-std::pow(u, 6.0f))); };
  add(cw, crestH + rs);                                                       // its right side, just under the shoulder
  for (int i = 1; i <= 2; ++i) {                                              // the right shoulder's corner
    const float a = 1.5707963f * i / 2.0f;
    add(cw - rs + rs * std::cos(a), crestH + rs - rs * std::sin(a));
  }
  for (int i = 1; i <= 24; ++i) {                                             // the crest, right to left
    const float x = (cw - rs) - (cw - 2 * rs) * i / 25.0f;
    add(x, top(x));
  }
  for (int i = 0; i <= 2; ++i) {                                              // the left shoulder's corner
    const float a = 1.5707963f + 1.5707963f * i / 2.0f;
    add(rs + rs * std::cos(a), crestH + rs - rs * std::sin(a));
  }
  for (int i = 0; i <= 4; ++i) {                                              // down its left side; the bottom left corner
    const float a = 3.1415927f + 1.5707963f * i / 4.0f;
    add(r + r * std::cos(a), ch - r - r * std::sin(a));
  }
  for (int i = 0; i <= 4; ++i) {                                              // its foot; the bottom right corner
    const float a = 4.712389f + 1.5707963f * i / 4.0f;
    add(cw - r + r * std::cos(a), ch - r - r * std::sin(a));
  }
  const int m = static_cast<int>(px.size() / 2);  // (= kEdgeSegs points; the loop closes on the first)
  for (int k = 0; k <= kEdgeSegs; ++k) {
    const int i = std::min(k % kEdgeSegs, m - 1);
    p[k] = XrVector3f{(px[2 * i] / cw - 0.5f) * W, (0.5f - px[2 * i + 1] / ch) * H, 0.0f};
  }
  for (int k = 0; k <= kEdgeSegs; ++k) {
    const XrVector3f a = p[(k + kEdgeSegs - 1) % kEdgeSegs], b = p[(k + 1) % kEdgeSegs];
    const float tx = b.x - a.x, ty = b.y - a.y, l = std::max(1e-6f, std::sqrt(tx * tx + ty * ty));
    n[k] = XrVector3f{ty / l, -tx / l, 0.0f};
  }
}
// (r220: `shape` -1 an A card's rounded edge, else its JAM '96 die - lk::jam_shape; `morph` THE PUNCH: 0 the crest .. 1 its cut)
void card_box(float* out, const Sheet& s, const XrQuaternionf& q, const XrVector3f& pos, float scale, bool show,
              float thick = kCardT, float nrm = 1.0f, int shape = -1, float morph = 1.0f, float frontNrm = 0.0f) {  // r138: (a special's shell: obsidian thicker, normals 2 long)
  // r230: rontNrm > 0 its front's normals this long (an EXQUISITE EMERALD / SAPPHIRE: xr_fx_model.frag's jewel chrome, kJewelCode)
  float nk = nrm;
  auto put = [&](int v, const XrVector3f& lp, const XrVector3f& ln) {
    const XrVector3f p = v_add(pos, q_rot(q, lp)), n = v_mul(q_rot(q, ln), nk);
    float* o = out + v * 6;
    o[0] = p.x;
    o[1] = p.y;
    o[2] = p.z;
    o[3] = n.x;
    o[4] = n.y;
    o[5] = n.z;
  };
  if (!show || scale <= 1e-4f) {
    for (int v = 0; v < kCardVerts; ++v) put(v, XrVector3f{0.0f, 0.0f, 0.0f}, XrVector3f{0.0f, 0.0f, 0.0f});
    return;
  }
  const float W = s.w * scale, H = s.h * scale, T = thick * scale;
  for (int side = 0; side < 2; ++side) {  // its front (+z), its back (-z; its picture mirrored: seen from behind)
    const float z = side == 0 ? 0.5f * T : -0.5f * T;
    nk = side == 0 && frontNrm > 0.0f ? frontNrm : nrm;
    for (int i = 0; i <= kCardNY; ++i)
      for (int j = 0; j <= kCardNX; ++j)
        put(side * kCardNV + i * (kCardNX + 1) + j,
            XrVector3f{(static_cast<float>(j) / kCardNX - 0.5f) * W, (0.5f - static_cast<float>(i) / kCardNY) * H, z},
            XrVector3f{0.0f, 0.0f, side == 0 ? 1.0f : -1.0f});
  }
  nk = nrm;
  // its edge: round its four corners (top right, top left, bottom left, bottom right), back to the start
  if (shape >= 0) {  // r217: a JAM '96 card's die-cut crest (r220: its own die, punched from the crest)
    XrVector3f ep[kEdgeSegs + 1], en[kEdgeSegs + 1];
    cut_edge(shape, morph, W, H, ep, en);
    for (int k = 0; k <= kEdgeSegs; ++k) {
      put(2 * kCardNV + k, XrVector3f{ep[k].x, ep[k].y, 0.5f * T}, en[k]);
      put(2 * kCardNV + (kEdgeSegs + 1) + k, XrVector3f{ep[k].x, ep[k].y, -0.5f * T}, en[k]);
    }
    return;
  }
  const float rc = kCornerR * W, cx = 0.5f * W - rc, cy = 0.5f * H - rc;
  for (int k = 0; k <= kEdgeSegs; ++k) {
    const int kk = k % kEdgeSegs, corner = kk / (kCornerSegs + 1), step = kk % (kCornerSegs + 1);
    const float a = (static_cast<float>(corner) + static_cast<float>(step) / kCornerSegs) * 1.5707963f;
    const float ox = corner == 0 || corner == 3 ? cx : -cx, oy = corner < 2 ? cy : -cy;
    const XrVector3f n{std::cos(a), std::sin(a), 0.0f};
    put(2 * kCardNV + k, XrVector3f{ox + rc * n.x, oy + rc * n.y, 0.5f * T}, n);
    put(2 * kCardNV + (kEdgeSegs + 1) + k, XrVector3f{ox + rc * n.x, oy + rc * n.y, -0.5f * T}, n);
  }
}

// ============================================================================================ r220: THE PUNCH
// Lance 11-11: a JAM '96 card face down / in the pack shows the plain crest (no give-away: its tells + its build-up as r219 left them);
// at its reveal (reveal_fx: where r219's snap passes half way) its edge snaps from the crest to its real cut (xr_pulls kPunchMorph), a
// die-press ka-chunk (made in code), and the cut-away pieces fall off as small bits that shrink away (kBitLife; at most kBitMax a card,
// worked out from the time alone - nothing kept per frame). Before it: its front only inside the crest (its real cut - a crown's balls -
// never shows round its back), its crest back; after: its whole front (its picture's alpha is its die), its back with its own die.
// The card model's parts after the level moments, a card at a time: its crest front (the crest loop in triangles: the front picture),
// its die's back (a grid: picture 4n + i), its bits (a quad each: picture 5n + i, each cutting its own piece)
void punch_parts(xrfx::Model& m, int n, const int* shapes) {
  uint32_t base = m.vertexCount;
  auto grid = [&](int tex, int nx, int ny, float u0, float u1, float v0, float v1, bool mirror) {
    for (int i = 0; i <= ny; ++i)
      for (int j = 0; j <= nx; ++j) {
        const float u = u0 + (u1 - u0) * j / nx, v = v0 + (v1 - v0) * i / ny;
        m.uv.push_back(mirror ? 1.0f - u : u);
        m.uv.push_back(v);
      }
    xrfx::ModelPart part;
    part.tex = tex;
    part.firstIndex = static_cast<uint32_t>(m.indices.size());
    for (int i = 0; i < ny; ++i)
      for (int j = 0; j < nx; ++j) {
        const uint32_t a = base + static_cast<uint32_t>(i * (nx + 1) + j), b = a + 1, c = a + nx + 1, d = c + 1;
        m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
      }
    part.indexCount = static_cast<uint32_t>(m.indices.size()) - part.firstIndex;
    m.parts.push_back(part);
    base += static_cast<uint32_t>((nx + 1) * (ny + 1));
  };
  for (int i = 0; i < n; ++i) {
    {  // its crest front
      const float* L = lk::jamcut::kShape[0].loop;
      for (int k = 0; k < lk::jamcut::kLoopN; ++k) m.uv.push_back(L[2 * k] / kCW), m.uv.push_back(L[2 * k + 1] / kCH);
      xrfx::ModelPart part;
      part.tex = i;
      part.firstIndex = static_cast<uint32_t>(m.indices.size());
      for (int t = 0; t < 3 * lk::jamcut::kCrestTris; ++t) m.indices.push_back(base + lk::jamcut::kCrestTri[t]);
      part.indexCount = static_cast<uint32_t>(m.indices.size()) - part.firstIndex;
      m.parts.push_back(part);
      base += static_cast<uint32_t>(lk::jamcut::kLoopN);
    }
    grid(4 * n + i, kCardNX, kCardNY, 0.0f, 1.0f, 0.0f, 1.0f, true);  // its die's back (seen from behind: mirrored)
    // its bits: each quad's uv its piece's box of the card (its quads past its die's count stay folded away)
    const lk::jamcut::ShapeDef& sd = lk::jamcut::kShape[std::max(0, std::min(lk::jamcut::kShapes - 1, shapes[i]))];
    for (int b = 0; b < kBitMax; ++b) {
      if (shapes[i] > 0 && b < sd.bitN) {
        const float* B = sd.bits + 6 * b;
        grid(5 * n + i, 1, 1, B[0] / kCW, B[2] / kCW, B[1] / kCH, B[3] / kCH, false);
      } else {
        grid(5 * n + i, 1, 1, 0.0f, 1.0f, 0.0f, 1.0f, false);
      }
    }
  }
  m.vertexCount = base;
}
// how far card i's edge has come from the crest to its cut (1: its cut; an A card / the crest: no matter)
float punch_morph_of(int i, uint64_t now) {
  if (S.cutShape[i] <= 0) return 1.0f;
  if (S.punchNs[i] == 0) return 0.0f;
  if (S.punchNs[i] == kPunchedLongAgo) return 1.0f;
  return xrpulls::punch_morph(secs(now, S.punchNs[i]));
}
// card i turned over in a pack: punched (reveal_fx, its moment) - its die-press, a tap in the hands; its bits fall from here
void punch_start(int i, uint64_t now) {
  if (i < 0 || i >= S.n || S.cutShape[i] <= 0 || S.punchNs[i] != 0) return;
  const Sheet& s = S.card[i];
  S.punchNs[i] = now;
  S.punchQ[i] = sheet_q(s);
  S.punchPos[i] = s.pos;
  S.punchScale[i] = s.scale;
  static std::vector<short> s_press;
  if (s_press.empty()) xrpulls::die_press_pcm(&s_press);
  PortCardSound(s_press.data(), static_cast<int>(s_press.size()), 0.85f);
  buzz(0.55f, 0.06f);
  CLOG("THE PUNCH: card %d - the crest punched to its %s (%d bits)", i, lk::jamcut::kShape[S.cutShape[i]].name, lk::jamcut::kShape[S.cutShape[i]].bitN);
}
// card i's punch vertices this frame (`out`: kPunchVerts x 6 floats) and its own card_box's (`box`: its front folded away before the
// punch - the crest front stands in - its crest back folded away after it - its die's back stands in)
void punch_frame(float* out, float* box, int i, const Sheet& s, const XrQuaternionf& q, bool show, uint64_t now) {
  std::fill(out, out + static_cast<size_t>(kPunchVerts) * 6, 0.0f);
  const int sh = S.cutShape[i];
  if (sh <= 0 || S.punchNs[i] == kPunchedLongAgo) return;  // (an A card, the crest, or its cut already: card_box as it is)
  const bool pending = S.punchNs[i] == 0;
  auto put = [&](float* o, const XrVector3f& p, const XrVector3f& n) { o[0] = p.x, o[1] = p.y, o[2] = p.z, o[3] = n.x, o[4] = n.y, o[5] = n.z; };
  if (show) {
    if (pending) {
      const float W = s.w * s.scale, H = s.h * s.scale, z = 0.5f * kCardT * s.scale;
      const float* L = lk::jamcut::kShape[0].loop;
      const XrVector3f nz = q_rot(q, XrVector3f{0.0f, 0.0f, 1.0f});
      for (int k = 0; k < lk::jamcut::kLoopN; ++k)
        put(out + k * 6, v_add(s.pos, q_rot(q, XrVector3f{(L[2 * k] / kCW - 0.5f) * W, (0.5f - L[2 * k + 1] / kCH) * H, z})), nz);
      std::fill(box, box + static_cast<size_t>(kCardNV) * 6, 0.0f);  // (its whole front: after the punch)
    } else {
      float* back = box + static_cast<size_t>(kCardNV) * 6;
      std::copy(back, back + static_cast<size_t>(kCardNV) * 6, out + static_cast<size_t>(lk::jamcut::kLoopN) * 6);
      std::fill(back, back + static_cast<size_t>(kCardNV) * 6, 0.0f);
    }
  }
  if (pending) return;
  // its bits: from where each piece was on the card at the punch, thrown out across it, out of its face and up, tumbling, falling
  const float t = secs(now, S.punchNs[i]), k = xrpulls::bit_scale(t);
  if (k <= 0.0f) return;
  const lk::jamcut::ShapeDef& sd = lk::jamcut::kShape[sh];
  const float sc = S.punchScale[i], W = kCardWm * sc, H = kCardHm * sc;
  const XrQuaternionf pq = S.punchQ[i];
  for (int b = 0; b < sd.bitN && b < kBitMax; ++b) {
    const float* B = sd.bits + 6 * b;
    const XrVector3f rest{(B[4] / kCW - 0.5f) * W, (0.5f - B[5] / kCH) * H, 0.0f};
    const float rl = std::max(1e-4f, std::sqrt(rest.x * rest.x + rest.y * rest.y));
    const xrpulls::BitThrow th = xrpulls::bit_throw(b, static_cast<uint32_t>(S.refs[i].card * 31 + i));
    const XrVector3f vl{rest.x / rl * th.out, rest.y / rl * th.out, th.face};
    XrVector3f c = v_add(v_add(S.punchPos[i], q_rot(pq, rest)), v_mul(q_rot(pq, vl), t));
    c.y += th.up * t - 0.5f * xrpulls::kBitGravity * t * t;
    const XrQuaternionf bq = q_mul(pq, q_axis(th.ax, th.ay, th.az, th.spin * t));
    const XrVector3f nz = q_rot(bq, XrVector3f{0.0f, 0.0f, 1.0f});
    float* o = out + (static_cast<size_t>(lk::jamcut::kLoopN + kCardNV) + static_cast<size_t>(b) * 4) * 6;
    for (int v = 0; v < 4; ++v) {  // (the grid's order: top left, top right, bottom left, bottom right - its uv the piece's box)
      const float px = v % 2 == 0 ? B[0] : B[2], py = v < 2 ? B[1] : B[3];
      const XrVector3f l{(px - B[4]) / kCW * W * k, -(py - B[5]) / kCH * H * k, 0.0f};
      put(o + v * 6, v_add(c, q_rot(bq, l)), nz);
    }
  }
}

// r147 (Lance: no level on the back - "i see it FIRST"; coordinator: a level-up "still feels good" after the flip): a pack
// card's level moment, level_tag's sticker in the cards' model (kTagVerts a card, after all the cards' boxes) - just after
// it's turned over (reveal_fx), popping up (overshooting a touch), held, then gone (kTagShow s in all), a little gold burst
// round it as it comes; only while its face is round to you.
// r150 (Lance: "ANY message that pops up ON CARD, needs to happen above the card. "level up" and "duplicate" and anything
// you put on the card, need to be moved to pop up above it"): it floats ABOVE the card - a few cm over its highest point in
// the room (over its figure / throne / TIZM BOOM's words when one stands there: special_above), turned to your eyes,
// following the card as you move it; nothing over its face
XrQuaternionf facing(const XrVector3f& at);
float special_above(int i);
constexpr int kTagVerts = 4;
constexpr float kTagDelay = 0.22f, kTagShow = 1.7f;
void level_moment(float* out, int i, bool show, const XrQuaternionf& q, uint64_t now, float dt) {
  const Sheet& s = S.card[i];
  float k = 0.0f, t = -1.0f;
  if (show && S.tagKind[i] != 0 && S.tagNs[i] != 0 && std::cos(s.flip) > 0.5f) {
    t = secs(now, S.tagNs[i]) - kTagDelay;
    if (t >= 0.0f && t < 0.24f) {
      const float u = t / 0.24f - 1.0f;
      k = 1.0f + 2.70158f * u * u * u + 1.70158f * u * u;  // (ease out, a touch past 1)
    } else if (t >= 0.24f && t < kTagShow - 0.3f) {
      k = 1.0f;
    } else if (t >= kTagShow - 0.3f && t < kTagShow) {
      k = 1.0f - smooth((t - (kTagShow - 0.3f)) / 0.3f);
    }
  }
  if (k <= 0.01f) {
    std::memset(out, 0, sizeof(float) * 6 * kTagVerts);
    return;
  }
  const float TW = 0.78f * kCardWm * k * s.scale, TH = TW * static_cast<float>(kTagH) / kTagW;
  // r165 (Lance's r164 video: the LEVEL UP / DUPLICATE pill sat ON the card's top edge - "it should not obstruct the view of
  // the card at all"): r150 put it over the card in the ROOM (world up), but a card held low and tipped back is looked DOWN
  // on, so the room's "above" lands on its face from your eyes. Now it's placed in YOUR VIEW: every point of the card (its
  // corners front and back, a few mm round them for a slab) and of what stands on its top edge (a GOLD's figure / BLACK's
  // throne / TIZM BOOM's words: special_above, a box out in front of and behind the card and wide as BOOM's words) taken to
  // your eyes; the tag's bottom edge a clear gap (2.5 cm at the card's distance) over the highest of them on your view's up,
  // across from the card's middle, at the card's distance, turned to your eyes - then its own corners checked the same way
  // and lifted if any dips into that gap. Same size as before; follows the card as it moves.
  const XrVector3f eye = S.head.position;
  XrVector3f F = v_sub(s.pos, eye);
  float D = v_len(F);
  if (D < 0.05f) D = 0.05f, F = XrVector3f{0.0f, 0.0f, -1.0f};
  else F = v_mul(F, 1.0f / D);
  const XrQuaternionf vq = facing(s.pos);  // (your view's right / up at the card: no roll, as the tag itself is turned)
  const XrVector3f VR = q_rot(vq, XrVector3f{1.0f, 0.0f, 0.0f}), VU = q_rot(vq, XrVector3f{0.0f, 1.0f, 0.0f});
  auto dot = [](const XrVector3f& a, const XrVector3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
  auto up_of = [&](const XrVector3f& p) {  // (how high p looks from your eyes: tan of its angle up from the card's middle)
    const XrVector3f d = v_sub(p, eye);
    return dot(d, VU) / std::max(0.02f, dot(d, F));
  };
  float top = -1e9f;
  const float hw = 0.5f * kCardWm + 0.008f, hh = 0.5f * kCardHm + 0.008f, ht = 0.5f * kCardT + 0.004f;
  for (int c = 0; c < 8; ++c) {
    const XrVector3f l{(c & 1 ? hw : -hw) * s.scale, (c & 2 ? hh : -hh) * s.scale, (c & 4 ? ht : -ht) * s.scale};
    top = std::max(top, up_of(v_add(s.pos, q_rot(q, l))));
  }
  const float above = special_above(i);
  if (above > 0.0f) {  // (what stands on its top edge: a box from its top edge up, out front and behind)
    const float sw = std::max(hw, 0.145f), sd = 0.07f;  // (r166: BOOM's side balloons reach 14.5 cm out)
    for (int c = 0; c < 8; ++c) {
      const XrVector3f l{(c & 1 ? sw : -sw) * s.scale, (c & 2 ? 0.5f * kCardHm + above : 0.5f * kCardHm) * s.scale,
                         (c & 4 ? sd : -sd) * s.scale};
      top = std::max(top, up_of(v_add(s.pos, q_rot(q, l))));
    }
  }
  const float gap = (0.025f * s.scale) / D;  // (the clear gap, as seen)
  const float rise = t > kTagShow - 0.3f ? 0.02f * (t - (kTagShow - 0.3f)) / 0.3f : 0.0f;
  XrVector3f at = v_add(v_add(eye, v_mul(F, D)), v_mul(VU, D * (top + gap) + 0.5f * TH + rise));
  XrQuaternionf fq = facing(at);
  XrVector3f R = q_rot(fq, XrVector3f{1.0f, 0.0f, 0.0f}), U = q_rot(fq, XrVector3f{0.0f, 1.0f, 0.0f});
  for (int pass = 0; pass < 3; ++pass) {  // (its own lowest corner as seen, lifted clear if it dips into the gap)
    float low = 1e9f;
    for (int c = 0; c < 4; ++c)
      low = std::min(low, up_of(v_add(at, v_add(v_mul(R, (c & 1 ? 0.5f : -0.5f) * TW), v_mul(U, (c & 2 ? 0.5f : -0.5f) * TH)))));
    if (low >= top + gap) break;
    at = v_add(at, v_mul(VU, D * (top + gap - low) + 0.001f));
    fq = facing(at);
    R = q_rot(fq, XrVector3f{1.0f, 0.0f, 0.0f}), U = q_rot(fq, XrVector3f{0.0f, 1.0f, 0.0f});
  }
  const XrVector3f n = q_rot(fq, XrVector3f{0.0f, 0.0f, 1.0f});
  for (int r = 0; r <= 1; ++r)
    for (int c = 0; c <= 1; ++c) {
      const XrVector3f p = v_add(at, v_add(v_mul(R, (static_cast<float>(c) - 0.5f) * TW), v_mul(U, (0.5f - static_cast<float>(r)) * TH)));
      float* o = out + (r * 2 + c) * 6;
      o[0] = p.x;
      o[1] = p.y;
      o[2] = p.z;
      o[3] = n.x;
      o[4] = n.y;
      o[5] = n.z;
    }
  if (t >= 0.0f && t < 0.5f)  // (its burst; r165: round the tag's upper half, not down over the card)
    xrfx::card_sparkle(v_add(at, v_mul(U, 0.1f * TH)), 0.3f * TW, 0.5f * TH, S.tagKind[i] == 2 ? 70.0f : 30.0f, dt, kGold, 0xFFFFFF);
}

// One sheet's grid into `out` (6 floats a vertex): nx x ny over its picture's rows v0..v1 (the pack's body or strip),
// its front (else its back, a millimetre behind: seen from behind), bent across by its curvature, `puff` the pack's
// pillow (m at size 1), at `q`, `pos`, `scale`; normals from the bent surface; all at one point when not shown.
void bend_grid(float* out, int nx, int ny, float v0, float v1, float bend, float sw, float sh, const XrQuaternionf& q,
               const XrVector3f& pos, float scale, bool front, float puff, bool show) {
  const int nv = (nx + 1) * (ny + 1);
  if (!show || scale <= 1e-4f) {
    for (int i = 0; i < nv; ++i) {
      float* o = out + i * 6;
      o[0] = pos.x;
      o[1] = pos.y;
      o[2] = pos.z;
      o[3] = o[4] = o[5] = 0.0f;
    }
    return;
  }
  static std::vector<XrVector3f> P;
  P.resize(static_cast<size_t>(nv));
  const float W = sw * scale, H = sh * scale, k = bend;
  for (int i = 0; i <= ny; ++i) {
    const float v = v0 + (v1 - v0) * static_cast<float>(i) / ny;
    for (int j = 0; j <= nx; ++j) {
      const float u = static_cast<float>(j) / nx;
      const float sx = (u - 0.5f) * W;
      float x = sx, z = 0.0f;
      if (std::fabs(k) > 1e-3f) {
        x = std::sin(k * sx) / k;
        z = (1.0f - std::cos(k * sx)) / k;
      }
      if (puff > 0.0f) {  // a pillow: up off its middle, flat at its crimps and its sides
        const float vb = clampf((v - 0.07f) / 0.86f, 0.0f, 1.0f);
        const float p = puff * scale * std::sqrt(std::max(0.0f, std::sin(3.14159265f * u))) *
                        std::sqrt(std::max(0.0f, std::sin(3.14159265f * vb)));
        z += front ? p : -p;
      }
      if (!front) z -= 0.001f * std::max(0.8f, scale);
      P[static_cast<size_t>(i * (nx + 1) + j)] = {x, (0.5f - v) * H, z};
    }
  }
  for (int i = 0; i <= ny; ++i)
    for (int j = 0; j <= nx; ++j) {
      const XrVector3f& a = P[static_cast<size_t>(i * (nx + 1) + std::min(nx, j + 1))];
      const XrVector3f& b = P[static_cast<size_t>(i * (nx + 1) + std::max(0, j - 1))];
      const XrVector3f& c = P[static_cast<size_t>(std::min(ny, i + 1) * (nx + 1) + j)];
      const XrVector3f& d = P[static_cast<size_t>(std::max(0, i - 1) * (nx + 1) + j)];
      XrVector3f n = v_cross(v_sub(c, d), v_sub(a, b));  // (down x across = +z on the front)
      const float l = v_len(n);
      n = l > 1e-9f ? v_mul(n, (front ? 1.0f : -1.0f) / l) : XrVector3f{0.0f, 0.0f, front ? 1.0f : -1.0f};
      const XrVector3f p = v_add(pos, q_rot(q, P[static_cast<size_t>(i * (nx + 1) + j)]));
      const XrVector3f wn = q_rot(q, n);
      float* o = out + (i * (nx + 1) + j) * 6;
      o[0] = p.x;
      o[1] = p.y;
      o[2] = p.z;
      o[3] = wn.x;
      o[4] = wn.y;
      o[5] = wn.z;
    }
}

// r142: a unit turn (a product of many drifts off unit length)
XrQuaternionf q_unit(const XrQuaternionf& q) {
  const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
  return l > 1e-6f ? XrQuaternionf{q.x / l, q.y / l, q.z / l, q.w / l} : XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f};
}
// r142: a hand closes on a sheet (or is the one left holding it): where it is and how it's turned, in that hand's own
// frame from now on (turned to your eyes until a hand first has it; then as held)
void sheet_take(Sheet& s, int h, const XrVector3f& hand) {
  if (!s.posed) {
    s.pose = q_unit(sheet_base(s));
    s.posed = true;
  }
  const XrQuaternionf hi = q_conj(S.handQ[h]);
  s.relQ[h] = q_unit(q_mul(hi, s.pose));
  s.relP[h] = q_rot(hi, v_sub(s.pos, hand));
}

// The hands holding a sheet. r142 (Lance: "make the cards more manipulable, like i felt like i was forced to hold the
// card at an angle and i couldnt wave it the way i wanted to"): held as a real card is - one grip: it goes with that
// hand's whole pose, 1:1 and at once (no easing: waved, tilted, flipped over with the wrist, turned round to its back),
// held where the grip closed on it; both grips: its size from how far apart they are (as before), turned with the hand
// that had it first, moved with the hands' middle; let go: it stays there just as it was (S.pack: the stack's holder,
// its cards in its frame). (Was: one hand only moved it, always turned to your eyes and leaning back to them; both turned
// it about the vertical only.)
void hold_sheets(const bool grip[2], const bool have[2], const XrVector3f hand[2], const XrPosef* aim[2], uint64_t now) {
  const bool live = S.mode == kPackUp || S.mode == kOut;  // r139: the pack, then its stack (the same holder), is held throughout
  for (int h = 0; h < 2; ++h) {
    if (live && grip[h] && !S.prevGrip[h] && have[h] && S.hold[h] < 0) {
      int best = -1;
      float bestD = 1e9f;
      for (int i = 0; i <= S.n; ++i) {
        const Sheet* s = sheet(i);
        if (s == nullptr || (i == 0 && S.mode != kPackUp) || (i == 0 && S.phase == kVault)) continue;  // (r230: the pack still in its case)
        if (i > 0 && S.mode == kPackUp) continue;  // (r139: the cards are the stack's, in the pack's hand)
        const float d = sheet_hit(*s, hand[h], aim[h]);
        if (d >= 0.0f && d < bestD) {
          bestD = d;
          best = i;
        }
      }
      if (best >= 0) {
        S.hold[h] = best;
        S.spent[h] = true;
        Sheet* s = sheet(best);
        s->free = true;
        buzz(0.25f, 0.04f);
        CLOG("the %s taken by the %s grip", best == 0 ? "pack" : "card", h == 0 ? "left" : "right");
      }
    }
    if ((!grip[h] || !have[h] || !live) && S.hold[h] >= 0) S.hold[h] = -1;
    if (!grip[h]) S.spent[h] = false;
    S.prevGrip[h] = grip[h];
  }
  const float dt = S.lastNs != 0 ? std::min(0.05f, secs(now, S.lastNs)) : 0.0f;
  const XrVector3f mid = v_mul(v_add(hand[0], hand[1]), 0.5f);
  const float apart = v_len(v_sub(hand[1], hand[0]));
  for (int i = 0; i <= S.n; ++i) {
    Sheet* s = sheet(i);
    if (s == nullptr || !s->on) continue;
    const int hands = (S.hold[0] == i ? 1 : 0) | (S.hold[1] == i ? 2 : 0);
    if (hands != s->hands) {
      if (hands == 3) {
        s->lead = s->hands == 1 ? 0 : 1;  // (the hand that had it; both at once: the right)
        sheet_take(*s, s->lead, hand[s->lead]);
        s->twoPos = s->pos;
        s->twoMid = mid;
        s->twoDist = std::max(0.05f, apart);
        s->twoScale = s->scale;
        S.apart = apart;
        S.apartV = 0.0f;
      } else if (hands != 0) {
        const int h = hands == 1 ? 0 : 1;
        sheet_take(*s, h, hand[h]);
        s->offFade = 0.0f;  // (a glide out of the binder: only in the hand that took it out)
      }
      s->hands = hands;
    }
    if (hands == 3) {
      s->pose = q_unit(q_mul(S.handQ[s->lead], s->relQ[s->lead]));
      s->pos = v_add(s->twoPos, v_sub(mid, s->twoMid));
      s->scale = clampf(s->twoScale * apart / s->twoDist, 0.5f, i == 0 ? 2.5f : 3.0f);  // (r139: no tear by pulling apart)
    } else if (hands != 0) {
      const int h = hands == 1 ? 0 : 1;
      if (s->offFade > 0.0f) {
        s->offFade = std::max(0.0f, s->offFade - dt);
        s->relP[h] = v_lerp(s->relP[h], s->relPTo, std::min(1.0f, dt * 10.0f));
      }
      s->pose = q_unit(q_mul(S.handQ[h], s->relQ[h]));
      s->pos = v_add(hand[h], q_rot(S.handQ[h], s->relP[h]));
    }
  }
}

// ================================================================================== r138: the special cards in 3D
// (Lance picked them: "all the starred ones EXCEPT for the 1/1 black tizm, i DO want the Obsidian Glass option BUT i
// would like the spotlight to still be a thing for those ones!! make sure you include OUR version of the shiny pokemon")
//  - TELL BEFORE THE FLIP: face down, a hit glows from behind (SILVER cool white, GOLD warm gold + slow sparkles, BLACK
//    dark purple smoke curling off its edges, a SHINY a rainbow glitter shimmer on top); BASE nothing
//  - SILVER TIZM: a WINDOW card - its character stands in a little room behind the card (a portal: what's behind its
//    front shows only through its window, xr_fx_model.frag), a soft chime
//  - GOLD TIZM /10: POP-OUT - its character has stepped out of the card and stands on its top edge; gold sparkles
//    r153 (Lance: "i want those designed like the silver jawns in terms of depth, but id rather create a loop of the
//    character ... transferring from in the card, to on top of the card the way it is now, and then going back to a solid
//    card the way it is now, but obvioulsy once they go back to solid gold, dont let the character be up top of the
//    card"): a STEP-OUT loop (gold_cycle, kGoldLoop s) - IN its own gold room behind a window like a SILVER's; it steps
//    forward out of the window and jumps up onto the top edge while the solid gold face (its picture, today's front)
//    slides down over the window behind it; ON TOP as before; it hops off and dives back into its picture (a gold burst
//    as it goes in: the solid face hides it); SOLID gold, nobody on top; the face slides back up: IN again
//  - BLACK TIZM 1/1: OBSIDIAN GLASS - a thick slab of black glass, the character floating inside it; a SPOTLIGHT from
//    above as it's turned over and whenever it's held up, your surroundings dimmed under it
//  - a SHINY Pokemon: its evolution room (the game's own setting by its kind) in its window, sparkles; always OUR shiny
//    (its _shiny pack, r127's glint), on any finish
// Its 3D (a model of its own in kSpecialSlot0 / 1: its front with the window cut through, its back, its edge, its room
// / slab, its figure: guests/<pack>.gpk posed by its standing loop, xr_evo) for the two biggest pulls on show (performance:
// the rest show their flat card); the auras, the spotlight's beam and the dimming in kLightSlot.
constexpr int kMaxSpecials = 2;
enum { kAuraNone = -1, kAuraSilver = 0, kAuraGold, kAuraSmoke, kAuraRainbow };
// r138 proof: the face-down halos (light pictures 6 silver / 7 gold / 8 black's shadow), the card + this much each side;
// a BLACK's smoke puffs
enum { kHaloSilver = 6, kHaloGold = 7, kHaloBlack = 8 };
constexpr float kAuraMargin = 0.045f;
constexpr int kSmokePuffs = 10;
const char* (*g_backdrop)(const std::string&) = nullptr;

int special_kind_open(const Ref& r);
int special_kind(const Ref& r) {
  // r152: sealed in a TIZM GRADING slab, only a window's room behind its face (nothing popping out through its case)
  if (S.mode == kOut && S.outSlab >= 0) return special_kind_open(r) == kSpWindow ? kSpWindow : kSpNone;
  return special_kind_open(r);
}
int special_kind_open(const Ref& r) {
  if (r.card >= 0 && r.card < static_cast<int>(g_set.size()) && g_set[static_cast<size_t>(r.card)].set == 2) return kSpNone;  // r230: an EXQUISITE card - flat, every tier (its foil / jewel chrome in the shader)
  if (r.hit == kHitBoom) return kSpBoom;    // r149: its comic layers popping out
  if (r.hit == kHitHome) return kSpWindow;  // r149: a window into its home (landscape)
  // r218 (Lance 11-11): SILVER and GOLD keep only their glow tell before the flip; turned over they're solid cards (their
  // A / B front with its moving foil) - no SILVER window room, no GOLD /10 step-out loop (r153; its code below is no
  // longer reached: no Special, no figure, no per-frame work for them). A GOLD shiny too; a shiny BASE / SILVER keeps its
  // evolution room as before.
  if (r.parallel == kGoldTizm) return r.hit == kHitSigned ? kSpSign : kSpNone;  // (a SIGNED GOLD: its autograph still writes on)
  if (r.parallel == kBlack) return kSpGlass;
  if (r.shiny) return kSpWindow;
  if (r.hit == kHitSigned) return kSpSign;  // r149: a SIGNED BASE: its autograph writing itself on
  return kSpNone;
}
// r149: a HOMETOWN's diorama - landscape: its window in its own frame (x across the card's height, up its left edge)
constexpr float kHomeWinHalfX = 0.5f * (kHomeWinX1 - kHomeWinX0) / kHomeW * kCardHm + 0.001f;  // (across: the card's y)
constexpr float kHomeWinHalfY = 0.5f * (kHomeWinY1 - kHomeWinY0) / kHomeH * kCardWm + 0.001f;  // (up: the card's -x)
// a point / direction of the landscape frame in the card's (its up the card's -x, its right the card's +y)
inline XrVector3f home_to_card(const XrVector3f& p) { return {-p.y, p.x, p.z}; }
uint32_t pack_rgb(float r, float g, float b) {
  auto c = [](float v) { return static_cast<uint32_t>(std::lround(clampf(v, 0.0f, 1.0f) * 255.0f)); };
  return (c(r) << 16) | (c(g) << 8) | c(b);
}
// the window in the card's own frame (metres at size 1): its half width, its bottom, its top (a hair over the hole: the
// front round it hides the rest)
constexpr float kWinHalf = (kHoleX1 - kCW * 0.5f) / kCW * kCardWm + 0.001f;
constexpr float kWinBottom = (0.5f - kHoleY1 / kCH) * kCardHm - 0.001f;
constexpr float kWinTop = (0.5f - kHoleY0 / kCH) * kCardHm + 0.001f;

// a window's room deep, the obsidian slab deep (r140: 6 cm, was 4 / 3: a round creature fitted by its depth came out half
// size); how far a window's figure may come out of the window toward you (the slab's stays inside its glass)
constexpr float kRoomD = 0.06f, kSlabD = 0.06f, kWinOut = 0.02f;

// r140 (Lance: "make the pokemon/characters be in the middle of the card always"): a special's figure placed by what it
// takes up posed (its bounds over its loop, at size 1, its feet at the origin), not its standing height - a short / wide
// creature sank to the window's floor, a tall one crowded its top. Window (SILVER, a shiny's evolution room): its bounds
// fitted centred in the window (kFigFill of its width / height, its depth within the room); turned 3/4 when that lets
// a long one (a snake, a whale) come out bigger; on the floor when that's near the middle anyway, else raised on a plinth
// (a flier / a floater just floats in the middle). Pop-out (GOLD): kPopFig high over the card's top edge (no wider than
// kPopWide), standing on it. Obsidian (BLACK): floating in the middle of the slab, fitted, its sway and turn included.
constexpr float kFigFill = 0.82f, kSlabFill = 0.78f, kPopFig = 0.62f * kCardWm, kPopWide = 1.15f * kCardWm;
// r153: a GOLD's step-out loop (s into it; it starts IN, at kGoldIn, as it's turned over): SOLID until kGoldOpen0, its face
// sliding up off the window to kGoldOpen1, IN its room, stepping out and up kGoldOut0..kGoldOut1 (its face sliding down
// behind it kGoldShut0..kGoldShut1), ON TOP, back into its picture kGoldBack0..kGoldBack1 (its burst at kGoldInto), SOLID to
// the end; out forward up to kGoldOutZ in front of the card, hopping kGoldHop over its path; the face in kGoldRows strips
// (its sliding edge: one strip squeezed)
constexpr float kGoldLoop = 12.0f, kGoldIn = 3.2f, kGoldOpen0 = 2.5f, kGoldOpen1 = 3.2f, kGoldOut0 = 6.0f, kGoldOut1 = 7.2f;
constexpr float kGoldShut0 = 6.65f, kGoldShut1 = 7.35f, kGoldBack0 = 10.0f, kGoldBack1 = 11.0f, kGoldInto = 0.82f;
constexpr float kGoldOutZ = 0.034f, kGoldHop = 0.022f;
constexpr int kGoldRows = 32;
struct Fit {
  float scale = 0.0f, yaw = -1.5707963f;
  XrVector3f feet{0.0f, 0.0f, 0.0f};
  int loopStart = 0, loopFrames = 0;
  float plinthTop = 0.0f, plinthHalfW = 0.0f, plinthZ0 = 0.0f, plinthZ1 = 0.0f;  // (plinthTop > kWinBottom: a plinth)
  // r141 (the BLACK 1/1's throne): its posed bounds at size 1 (width, height, depth)
  float bw = 0.0f, bh = 0.0f, bd = 0.0f;
  int bigStart = 0, bigFrames = 0;  // (its cameo: its cheer, then this big move of its own)
};
struct Bounds {
  XrVector3f lo{1e9f, 1e9f, 1e9f}, hi{-1e9f, -1e9f, -1e9f};
};
// its bounds posed at size 1, feet at the origin, facing `yaw`, at `moments` moments of its loop (turned +-sway too)
Bounds posed_bounds(const xrevo::Figure* f, float yaw, int loopStart, int loopFrames, float sway, int moments = 8) {
  Bounds b;
  std::vector<float> out;
  const float fps = std::max(1.0f, xrevo::fps(f));
  for (int k = 0; k < moments; ++k) {
    const float t = loopFrames > 0 ? (loopStart + loopFrames * k / static_cast<float>(moments)) / fps : 0.4f * k;
    for (int w = -1; w <= 1; ++w) {
      if (sway == 0.0f && w != 0) continue;
      xrevo::pose(f, t, XrVector3f{0.0f, 0.0f, 0.0f}, yaw + sway * w, 1.0f, 0.0f, &out);
      for (size_t i = 0; i + 5 < out.size(); i += 6) {
        b.lo = {std::min(b.lo.x, out[i]), std::min(b.lo.y, out[i + 1]), std::min(b.lo.z, out[i + 2])};
        b.hi = {std::max(b.hi.x, out[i]), std::max(b.hi.y, out[i + 1]), std::max(b.hi.z, out[i + 2])};
      }
    }
  }
  return b;
}
// r141 (coordinator, Lance's BLACK 1/1 features: "LIVING THRONE: the 1/1's character stands ON TOP of the obsidian card
// (like the GOLD pop-out but bigger), on a little black-glass throne/pedestal, doing their OWN celebration on a loop under
// the spotlight"): kThroneFig high over the card's top edge (no wider than kThroneWide), on a two-step pedestal kPedH
// high; it loops its own celebration (a home figure's cheer, else its big move), its idle with a gentle bob when its pose
// file has none (the Pokemon, the Melee fighters: their evolution idles)
constexpr float kThroneFig = 0.85f * kCardWm, kThroneWide = 1.3f * kCardWm, kPedH = 0.014f, kPedStep = 0.006f;
constexpr float kThroneTop = 0.5f * kCardHm + kPedH;  // (the pedestal's top, in the card's frame)
// r193 (Lance: a SILVER Croconaw "stuttered like 5 times before running in place on the card"): a card figure's move -
// the first of `kinds` (in that order) with a clean stretch (xrevo::clean_stretch: none of the game's cut-scene jumps; not
// a run - a card holds its figure in place, so a run reads as running on the spot), that stretch; false none
bool card_move(const xrevo::Figure* f, std::initializer_list<uint32_t> kinds, uint32_t* start, uint32_t* frames) {
  for (uint32_t want : kinds)
    for (const xrevo::Segment& seg : xrevo::segments(f))
      if (seg.kind == want && seg.frames > 4) {
        float tr = 0.0f;
        if (xrevo::clean_stretch(f, seg.start, seg.frames, std::min<uint32_t>(seg.frames, 30u), 0.5f, start, frames, &tr)) {
          if (*frames != seg.frames)
            CLOG("special: its move (kind %u, %u frames at %u) cut to its clean stretch: %u frames at %u (travel %.2f)", want,
                 seg.frames, seg.start, *frames, *start, tr);
          return true;
        }
        CLOG("special: its move (kind %u, %u frames at %u) left out: no clean stretch (jumps / a run)", want, seg.frames, seg.start);
      }
  return false;
}
// (r149: `wHalf`, `wBot`, `wTop` its window - a HOMETOWN's landscape one, in its own frame)
Fit fit_figure(const xrevo::Figure* f, int kind, float wHalf = kWinHalf, float wBot = kWinBottom, float wTop = kWinTop) {
  Fit fit;
  if (kind == kSpGlass) {
    // its cheer, else its big move (r193: their clean stretches - card_move)
    uint32_t s = 0, n = 0;
    if (card_move(f, {5u}, &s, &n)) {
      fit.loopStart = static_cast<int>(s);
      fit.loopFrames = static_cast<int>(n);
      if (card_move(f, {2u}, &s, &n)) {  // (its cameo's celebration: the cheer, then its first big move)
        fit.bigStart = static_cast<int>(s);
        fit.bigFrames = static_cast<int>(n);
      }
    } else if (card_move(f, {2u}, &s, &n)) {
      fit.loopStart = static_cast<int>(s);
      fit.loopFrames = static_cast<int>(n);
    }
    const Bounds b = posed_bounds(f, fit.yaw, fit.loopStart, fit.loopFrames, 0.0f, 16);
    fit.bw = std::max(1e-4f, b.hi.x - b.lo.x);
    fit.bh = std::max(1e-4f, b.hi.y - b.lo.y);
    fit.bd = std::max(1e-4f, b.hi.z - b.lo.z);
    fit.scale = std::min(kThroneFig / fit.bh, kThroneWide / fit.bw);
    const float hover = b.lo.y > 0.08f * fit.bh ? 0.006f : 0.0006f;  // (a flier a little over its throne)
    fit.feet = {-fit.scale * 0.5f * (b.lo.x + b.hi.x), kThroneTop + hover - fit.scale * b.lo.y,
                -fit.scale * 0.5f * (b.lo.z + b.hi.z)};
    return fit;
  }
  if (kind == kSpPop) {  // a pop-out loops a move of its own when its pose file has one (a home figure's big move)
    uint32_t s = 0, n = 0;  // (r181: + a Pokemon's / Melee's cheer, cel_bake's kind 5; r193: its clean stretch)
    if (card_move(f, {2u, 5u}, &s, &n)) {
      fit.loopStart = static_cast<int>(s);
      fit.loopFrames = static_cast<int>(n);
    }
  }
  if (kind == kSpPop) {
    const Bounds b = posed_bounds(f, fit.yaw, fit.loopStart, fit.loopFrames, 0.0f);
    const float bw = std::max(1e-4f, b.hi.x - b.lo.x), bh = std::max(1e-4f, b.hi.y - b.lo.y);
    fit.scale = std::min(kPopFig / bh, kPopWide / bw);
    const float hover = b.lo.y > 0.08f * bh ? 0.008f : 0.0008f;  // (a flier a little over the edge)
    fit.feet = {-fit.scale * 0.5f * (b.lo.x + b.hi.x), 0.5f * kCardHm + hover - fit.scale * b.lo.y,
                -fit.scale * 0.5f * (b.lo.z + b.hi.z)};
    return fit;
  }
  const bool slab = kind == kSpGlass;
  const float fill = slab ? kSlabFill : kFigFill, D = slab ? kSlabD : kRoomD;
  const float wAv = 2.0f * wHalf, hAv = wTop - wBot, z0 = -D + 0.002f, z1 = slab ? 0.005f : kWinOut;
  // facing you, 3/4 either way, or side on (a snake, a whale, a long four-legged one: its length across the window, not
  // into it) - whichever fits it biggest, facing you first (it reads best), then 3/4
  float best = -1.0f;
  Bounds bb;
  static const float kYaw[5] = {-1.5707963f, -0.7853982f, -2.3561945f, 0.0f, -3.1415927f};
  static const float kPrefer[5] = {1.35f, 1.15f, 1.15f, 1.0f, 1.0f};  // (side on only when it's a third bigger)
  // r173: a window's figure lives in it (its idle, then its big move: live_moves).
  // r181 (Lance: two SILVERs "didnt act like normal silver behavior ... hardly moved"): sized and placed by its STANDING
  // (idle) bounds, as before r173 - the big move only shrinks it at most 15% (a jump may leave the window a moment;
  // never a tiny figure); a big move is its celebration (kind 2) or a Pokemon's / Melee's cheer (kind 5: cel_bake's)
  const xrevo::Segment* idleSeg = nullptr;
  const xrevo::Segment* bigSeg = nullptr;
  for (const xrevo::Segment& seg : xrevo::segments(f)) {
    if (seg.kind == 0 && idleSeg == nullptr && seg.frames > 4) idleSeg = &seg;
    if ((seg.kind == 2 || seg.kind == 5) && seg.frames > 4 && (bigSeg == nullptr || (bigSeg->kind == 5 && seg.kind == 2))) bigSeg = &seg;
  }
  for (int y = 0; y < 5; ++y) {
    const float yaw = kYaw[y];
    const Bounds b = idleSeg != nullptr && !slab ? posed_bounds(f, yaw, static_cast<int>(idleSeg->start), static_cast<int>(idleSeg->frames), 0.0f)
                                                 : posed_bounds(f, yaw, 0, 0, slab ? 0.3f : 0.0f);
    auto fitS = [&](const Bounds& x) {
      return std::min({fill * wAv / std::max(1e-4f, x.hi.x - x.lo.x), fill * hAv / std::max(1e-4f, x.hi.y - x.lo.y),
                       (z1 - z0) / std::max(1e-4f, x.hi.z - x.lo.z)});
    };
    float s = fitS(b);
    if (bigSeg != nullptr && !slab) {
      Bounds b2 = posed_bounds(f, yaw, static_cast<int>(bigSeg->start), static_cast<int>(bigSeg->frames), 0.0f);
      b2.lo = {std::min(b.lo.x, b2.lo.x), std::min(b.lo.y, b2.lo.y), std::min(b.lo.z, b2.lo.z)};
      b2.hi = {std::max(b.hi.x, b2.hi.x), std::max(b.hi.y, b2.hi.y), std::max(b.hi.z, b2.hi.z)};
      s = std::max(fitS(b2), 0.85f * s);
    }
    const float score = s * kPrefer[y];
    if (score > best) {
      best = score;
      fit.scale = s;
      fit.yaw = yaw;
      bb = b;
    }
  }
  const float s = fit.scale, bh = bb.hi.y - bb.lo.y;
  const float zMid = 0.5f * (z0 + z1), yMid = 0.5f * (wBot + wTop) - (slab ? 0.0035f : 0.0f);  // (its bob)
  fit.feet = {-s * 0.5f * (bb.lo.x + bb.hi.x), yMid - s * 0.5f * (bb.lo.y + bb.hi.y), zMid - s * 0.5f * (bb.lo.z + bb.hi.z)};
  const float bottom = fit.feet.y + s * bb.lo.y, gap = bottom - wBot;
  const bool floats = bb.lo.y > 0.08f * bh;  // (a flier / a floater: in the middle, in the air)
  if (!slab && !floats) {
    if (gap < 0.12f * hAv) {  // near the middle on the floor anyway: stand it there
      fit.feet.y = wBot + 0.0015f - s * bb.lo.y;
    } else {  // raised on a plinth to the middle
      fit.plinthTop = bottom;
      fit.plinthHalfW = std::min(0.45f * s * (bb.hi.x - bb.lo.x), 0.8f * wHalf);
      fit.plinthZ0 = std::max(z0 + 0.002f, fit.feet.z + s * bb.lo.z);
      fit.plinthZ1 = std::min(-0.002f, fit.feet.z + s * bb.hi.z);
      if (fit.plinthZ1 - fit.plinthZ0 < 0.01f) fit.plinthZ0 = fit.plinthZ1 - 0.01f;
    }
  }
  return fit;
}

// ===================================================================================== r141: a BLACK 1/1's world
// (Lance's picks, r141: "i want the bubbles and droplets around me, but i also want like water FX on the card itself")
// While a BLACK 1/1 is at the front of the stack (or out of the binder) face up: its type's effect on the card itself
// (on / inside its glass: flames running its edges, water rippling and beading down it, vines over its corners ...) and
// the throne's slow purple aura; while you hold it, its world round you too - its type's things drifting within ~2 m
// (never over the card or your view: at your sides, above, below), a soft light tint, an ambient sound - fading when
// put down. One 1/1 at a time, in its own model (kFxSlot): additive sprites from one atlas drawn here, each tinted by its
// vertex "normal" (xr_fx_model.frag's light mode -4.5), ~80 quads + the tint's sphere.
// Pokemon by their primary type (STRIKERS_HOME/franchise/types.txt: the Gen 1-3 Pokemon's real types as the games have
// them today, Fairy included; a Normal / Flying bird is Flying), Marvel heroes by their power, the Strikers / Charged /
// Melee characters by their own signature things.
XrQuaternionf facing(const XrVector3f& at);
enum FxSpr : uint8_t {
  kFsSoft, kFsEmber, kFsBubble, kFsDrop, kFsLeaf, kFsPollen, kFsSpark, kFsRing, kFsOrb, kFsWisp, kFsFlake, kFsPebble,
  kFsDust, kFsFeather, kFsStreak, kFsPuff, kFsFirefly, kFsScale, kFsShock, kFsConfetti, kFsStar, kFsGlint, kFsTendril,
  kFsHeart, kFsFlame, kFsBolt, kFsCrack, kFsFrost, kFsIcicle, kFsVine, kFsRune, kFsCaustic, kFsWeb, kFsShield, kFsArrow,
  kFsClaw, kFsChain, kFsCoin, kFsBanana, kFsRupee, kFsLaser, kFsPetal, kFsFireball, kFsBeam, kFsRipple, kFsPsi,
  kFsMushroom, kFsBone, kFsEgg, kFsShell, kFsHammer, kFsPill, kFsBlade, kFsFlat, kFsCount
};
// round you: rising, falling, drifting, swirling round you, jumping (sparks / flashes), along the floor, rings pulsing,
// circling close (fireflies)
enum FxAround : uint8_t { kFaNone, kFaRise, kFaFall, kFaDrift, kFaSwirl, kFaJump, kFaFloor, kFaPulse, kFaOrbit };
// on the card: along its edges, wandering its face, drops (beading, running down with gravity, dripping off), rising
// inside the glass, growing at its corners, a sheen sweeping, hanging under its bottom edge, one landing and resting,
// streaks across, bursts, seeping in from the edges, rings rippling, a glow inside, light drifting inside, falling off
enum FxCard : uint8_t {
  kFcNone, kFcEdge, kFcFace, kFcDrops, kFcInside, kFcCorner, kFcSweep, kFcHang, kFcLand, kFcStreak, kFcBurst, kFcSeep,
  kFcRipple, kFcGlow, kFcDrift, kFcFallOff
};
enum FxSnd : uint8_t {
  kSnNone, kSnCrackle, kSnWater, kSnRustle, kSnStatic, kSnHum, kSnWhisper, kSnChimes, kSnRumble, kSnGust, kSnBubbling,
  kSnBuzz, kSnRoar, kSnThump, kSnCrowd, kSnClink, kSnDrone, kSnTwinkle
};
struct FxEm {
  uint8_t spr = 0, mo = 0, n = 0;
  uint32_t c1 = 0xFFFFFF, c2 = 0xFFFFFF;
  float size = 1.0f;
};
constexpr FxEm E(uint8_t spr, uint8_t mo, uint8_t n, uint32_t c1, uint32_t c2 = 0, float size = 1.0f) {
  return FxEm{spr, mo, n, c1, c2 == 0 ? c1 : c2, size};
}
constexpr FxEm E0 = FxEm{0, 0, 0, 0, 0, 0.0f};
struct FxTheme {
  const char* name;
  FxEm a[3];      // round you
  uint32_t tint;  // the light round you
  uint8_t snd;
  FxEm c[3];      // on the card
};
enum FxThemeId : int {
  tNormal, tFire, tWater, tGrass, tElectric, tPsychic, tGhost, tIce, tRock, tFlying, tPoison, tBug, tDragon, tFighting,
  tSteel, tDark, tFairy,
  mFire, mHellfire, mLightning, mDoom, mIce, mRunes, mPhoenix, mPsi, mInvisible, mMysterio, mCosmic, mShadow, mTeleport,
  mStrength, mWeb, mShield, mArrows, mClaws, mDeadpool, mStreet,
  sMario, sLuigi, sPeach, sDaisy, sYoshi, sDK, sWario, sWaluigi, sToad, sKoopa, sHammer, sBirdo, sBowser, sBowserJr,
  sDiddy, sPetey, sBoo, sDryBones, sMonty, sShyGuy, sFox, sFalco, sFalcon, sLink, sZelda, sSheik, sSamus, sKirby, sNess,
  sIceClimbers, sMarth, sRoy, sGanon, sGameWatch, sDrMario, kFxThemes
};
const FxTheme kFxTheme[kFxThemes] = {
    // --- Pokemon, by type
    {"normal", {E(kFsConfetti, kFaFall, 16, 0xFF7AC0, 0xFFE060), E(kFsStar, kFaSwirl, 8, 0xFFFFFF, 0xFFD050), E0}, 0xFFD8EC,
     kSnCrowd, {E(kFsStar, kFcBurst, 6, 0xFFFFFF, 0xFFE070), E(kFsGlint, kFcFace, 3, 0xFFFFFF), E0}},
    {"fire", {E(kFsEmber, kFaRise, 22, 0xFF8A2A, 0xFFD060), E(kFsSoft, kFaFloor, 4, 0xFF6A20, 0xFF9030, 2.0f), E(kFsPuff, kFaRise, 4, 0, 0, 1.6f)}, 0xFF7A30,
     kSnCrackle, {E(kFsFlame, kFcEdge, 14, 0xFF7A20, 0xFFD050), E(kFsSoft, kFcGlow, 1, 0xFF5A10), E(kFsEmber, kFcInside, 6, 0xFFB040)}},
    {"water", {E(kFsBubble, kFaRise, 14, 0xBFE8FF), E(kFsDrop, kFaDrift, 12, 0x8FD0FF, 0xCFEFFF), E(kFsCaustic, kFaFloor, 5, 0x3A90E0)},
     0x3A8CFF, kSnWater,
     {E(kFsDrop, kFcDrops, 20, 0xD8F2FF), E(kFsRipple, kFcRipple, 3, 0x8FD0FF), E(kFsCaustic, kFcDrift, 3, 0x4AA8FF)}},
    {"grass", {E(kFsLeaf, kFaFall, 12, 0x6ED04A, 0xB8E05A), E(kFsPollen, kFaDrift, 14, 0xFFF0A0), E(kFsSoft, kFaFloor, 4, 0x2A6020, 0x3A7A28, 2.0f)},
     0x5AD05A, kSnRustle, {E(kFsVine, kFcCorner, 4, 0x5AC040), E(kFsLeaf, kFcLand, 1, 0x7AD84A), E(kFsPollen, kFcFace, 4, 0xFFF0A0)}},
    {"electric", {E(kFsSpark, kFaJump, 16, 0xFFF6A0, 0xFFFFFF), E(kFsGlint, kFaJump, 4, 0xFFE860), E0}, 0xFFE860, kSnStatic,
     {E(kFsBolt, kFcEdge, 8, 0xFFF090, 0xFFFFFF), E(kFsSpark, kFcInside, 8, 0xFFFFA0), E0}},
    {"psychic", {E(kFsRing, kFaPulse, 6, 0xFF7AD8), E(kFsOrb, kFaDrift, 10, 0xC870FF, 0xFF90E0), E0}, 0xB060FF, kSnHum,
     {E(kFsRing, kFcRipple, 3, 0xFF8AE0), E(kFsOrb, kFcFace, 4, 0xC870FF), E0}},
    {"ghost", {E(kFsWisp, kFaFloor, 10, 0xB070FF, 0x8A50E0, 2.2f), E(kFsWisp, kFaDrift, 8, 0xC090FF, 0x9A60F0, 2.0f), E(kFsPuff, kFaFloor, 6, 0, 0, 2.2f)},
     0x6A3AA0, kSnWhisper, {E(kFsPuff, kFcDrift, 5, 0xB070FF, 0x8040E0, 1.5f), E(kFsWisp, kFcInside, 6, 0xC890FF, 0, 1.3f), E(kFsPuff, kFcSeep, 4, 0, 0, 1.2f)}},
    {"ice", {E(kFsFlake, kFaFall, 18, 0xEAF6FF), E(kFsPuff, kFaFloor, 6, 0x6A9AC8, 0x8AB8E0, 2.0f), E0}, 0x8FD0FF, kSnChimes,
     {E(kFsFrost, kFcCorner, 4, 0xDDF4FF), E(kFsIcicle, kFcHang, 6, 0xCFEFFF), E0}},
    {"rock", {E(kFsPebble, kFaRise, 10, 0xC8A878, 0xA88858), E(kFsDust, kFaRise, 8, 0xD8C8A0), E0}, 0xE0B070, kSnRumble,
     {E(kFsCrack, kFcCorner, 2, 0xFFD890), E(kFsDust, kFcFallOff, 5, 0xD8C8A0), E0}},
    {"flying", {E(kFsFeather, kFaFall, 10, 0xFFFFFF, 0xE8F0FF), E(kFsStreak, kFaSwirl, 10, 0xDDEEFF), E0}, 0xCFE8FF, kSnGust,
     {E(kFsStreak, kFcStreak, 4, 0xFFFFFF), E(kFsFeather, kFcLand, 1, 0xFFFFFF), E0}},
    {"poison", {E(kFsPuff, kFaFloor, 8, 0xB070F0, 0x8AD040, 1.4f), E(kFsBubble, kFaRise, 10, 0xC8F070, 0xD090FF, 1.2f), E0}, 0x8AD040,
     kSnBubbling, {E(kFsBubble, kFcInside, 12, 0xD090FF, 0xC8F070, 1.3f), E(kFsPuff, kFcDrift, 2, 0x9A50D0, 0, 1.2f), E0}},
    {"bug", {E(kFsFirefly, kFaOrbit, 16, 0xFFF080, 0xC8FF60), E0, E0}, 0xFFB060, kSnBuzz,
     {E(kFsFirefly, kFcFace, 10, 0xFFF080, 0xC8FF60, 1.3f), E0, E0}},
    {"dragon", {E(kFsScale, kFaSwirl, 16, 0xFFC840, 0x9A60FF), E0, E0}, 0xFFC040, kSnRoar,
     {E(kFsScale, kFcFace, 10, 0xFFD050, 0xB080FF), E(kFsGlint, kFcBurst, 3, 0xFFE080), E0}},
    {"fighting", {E(kFsShock, kFaPulse, 5, 0xFFB070), E(kFsDust, kFaFloor, 8, 0xD8B890), E(kFsGlint, kFaJump, 4, 0xFFFFFF)},
     0xFF9050, kSnThump, {E(kFsCrack, kFcCorner, 2, 0xFFD0A0), E(kFsGlint, kFcBurst, 2, 0xFFFFFF), E0}},
    {"steel", {E(kFsGlint, kFaJump, 10, 0xE8F0FF), E(kFsSpark, kFaDrift, 8, 0xFFE8B0), E0}, 0xC0D0E8, kSnClink,
     {E(kFsStreak, kFcSweep, 1, 0xFFFFFF), E(kFsSpark, kFcEdge, 6, 0xFFE0A0), E0}},
    {"dark", {E(kFsTendril, kFaFloor, 10, 0, 0, 1.6f), E(kFsPuff, kFaDrift, 6, 0, 0, 2.0f), E(kFsGlint, kFaJump, 6, 0xFF3030)}, 0x400818,
     kSnDrone, {E(kFsPuff, kFcSeep, 8, 0, 0, 2.0f), E(kFsTendril, kFcDrift, 3, 0xC02050, 0x901040, 2.0f), E(kFsGlint, kFcBurst, 3, 0xFF3030)}},
    {"fairy", {E(kFsSpark, kFaDrift, 14, 0xFFB0E0), E(kFsHeart, kFaRise, 8, 0xFF80C8), E0}, 0xFFA0D8, kSnTwinkle,
     {E(kFsSpark, kFcBurst, 8, 0xFFD0F0), E0, E0}},
    // --- Marvel, by power
    {"fire (hero)", {E(kFsEmber, kFaRise, 20, 0xFF9030, 0xFFD060), E(kFsFireball, kFaDrift, 4, 0xFFFFFF), E0}, 0xFF7A30,
     kSnCrackle, {E(kFsFlame, kFcEdge, 14, 0xFF7A20, 0xFFD050), E(kFsSoft, kFcGlow, 1, 0xFF5A10), E0}},
    {"hellfire", {E(kFsEmber, kFaRise, 18, 0xFF6A10, 0xFFB030), E(kFsChain, kFaSwirl, 6, 0xC8C8D0), E0}, 0xFF5010, kSnCrackle,
     {E(kFsFlame, kFcEdge, 14, 0xFF6010, 0xFFC040), E(kFsChain, kFcCorner, 2, 0xD0D0D8), E(kFsSoft, kFcGlow, 1, 0xFF4010)}},
    {"lightning", {E(kFsSpark, kFaJump, 14, 0xD8ECFF), E(kFsBolt, kFaJump, 3, 0xCFE4FF, 0xFFFFFF, 2.5f), E0}, 0x9FC8FF,
     kSnStatic, {E(kFsBolt, kFcEdge, 8, 0xD0E8FF, 0xFFFFFF), E(kFsSpark, kFcInside, 6, 0xD8ECFF), E0}},
    {"doom", {E(kFsSpark, kFaJump, 14, 0x9CFF9A), E(kFsBolt, kFaJump, 3, 0x7CFF7A, 0xC8FFC0, 2.5f), E0}, 0x6AE060, kSnStatic,
     {E(kFsBolt, kFcEdge, 8, 0x7CFF7A, 0xC8FFC0), E(kFsSpark, kFcInside, 6, 0x9CFF9A), E0}},
    {"ice (hero)", {E(kFsFlake, kFaFall, 18, 0xEAF6FF), E(kFsPuff, kFaFloor, 6, 0x6A9AC8, 0x8AB8E0, 2.0f), E0}, 0x8FD0FF,
     kSnChimes, {E(kFsFrost, kFcCorner, 4, 0xDDF4FF), E(kFsIcicle, kFcHang, 6, 0xCFEFFF), E0}},
    {"runes", {E(kFsRune, kFaDrift, 6, 0xFFA030), E(kFsSpark, kFaSwirl, 10, 0xFFC060), E0}, 0xFF9A30, kSnHum,
     {E(kFsRune, kFcGlow, 1, 0xFFA030), E(kFsSpark, kFcFace, 4, 0xFFC060), E0}},
    {"phoenix", {E(kFsFeather, kFaRise, 12, 0xFF8A20, 0xFFD040), E(kFsEmber, kFaRise, 10, 0xFFB040), E0}, 0xFF7A30, kSnCrackle,
     {E(kFsFlame, kFcEdge, 12, 0xFF8A20, 0xFFD040), E(kFsFeather, kFcLand, 1, 0xFFA030), E0}},
    {"psychic (hero)", {E(kFsRing, kFaPulse, 6, 0xB070FF), E(kFsOrb, kFaDrift, 8, 0xC090FF), E0}, 0x9A60FF, kSnHum,
     {E(kFsRing, kFcRipple, 3, 0xC090FF), E(kFsOrb, kFcFace, 3, 0xC090FF), E0}},
    {"invisible", {E(kFsRing, kFaDrift, 8, 0x80C8FF), E(kFsSpark, kFaDrift, 10, 0xBFE8FF), E0}, 0x80C0FF, kSnHum,
     {E(kFsRipple, kFcRipple, 3, 0x9FD8FF), E(kFsSpark, kFcBurst, 3, 0xBFE8FF), E0}},
    {"mysterio", {E(kFsPuff, kFaFloor, 10, 0x40E070, 0x20A050), E(kFsOrb, kFaDrift, 6, 0x60FF90), E0}, 0x50D070, kSnWhisper,
     {E(kFsPuff, kFcDrift, 4, 0x40E070), E0, E0}},
    {"cosmic", {E(kFsStar, kFaDrift, 14, 0xFFFFFF, 0xFFE8A0), E(kFsPollen, kFaSwirl, 14, 0xC8D8FF), E(kFsStreak, kFaSwirl, 6, 0xFFE8C0)},
     0xC0C8FF, kSnHum, {E(kFsStar, kFcBurst, 6, 0xFFFFFF, 0xFFE8A0), E(kFsStreak, kFcSweep, 1, 0xFFF0D0), E0}},
    {"shadow", {E(kFsPuff, kFaFloor, 10, 0, 0, 2.0f), E(kFsGlint, kFaJump, 5, 0xD8E4FF), E(kFsPuff, kFaDrift, 4, 0, 0, 1.8f)}, 0x8090B0, kSnDrone,
     {E(kFsPuff, kFcSeep, 6, 0, 0, 1.3f), E(kFsGlint, kFcBurst, 2, 0xC8D8FF), E0}},
    {"teleport", {E(kFsPuff, kFaJump, 8, 0xA070F0, 0xD0A0FF, 1.8f), E(kFsPuff, kFaFloor, 6, 0, 0, 1.8f), E0},
     0x7A50C0, kSnWhisper, {E(kFsPuff, kFcBurst, 4, 0x9A60E0), E0, E0}},
    {"strength", {E(kFsPebble, kFaRise, 10, 0xC89870, 0xA88050, 1.4f), E(kFsDust, kFaFloor, 8, 0xC8B090, 0, 1.5f), E0}, 0xE0A060, kSnRumble,
     {E(kFsCrack, kFcCorner, 2, 0xFFD090), E(kFsDust, kFcFallOff, 5, 0xD0B890), E0}},
    {"webs", {E(kFsWeb, kFaDrift, 6, 0xFFFFFF, 0, 2.2f), E(kFsSpark, kFaDrift, 6, 0xFF5050), E0}, 0xE05050, kSnGust,
     {E(kFsWeb, kFcCorner, 2, 0xFFFFFF, 0, 1.4f), E(kFsSpark, kFcBurst, 2, 0xFF5050), E0}},
    {"shield", {E(kFsShield, kFaDrift, 4, 0xFFFFFF), E(kFsStar, kFaDrift, 10, 0xFFFFFF, 0xFF4040), E0}, 0x6080FF, kSnCrowd,
     {E(kFsShield, kFcLand, 1, 0xFFFFFF), E(kFsStar, kFcBurst, 4, 0xFFFFFF, 0xFF4040), E0}},
    {"arrows", {E(kFsArrow, kFaSwirl, 8, 0xC070FF, 0xE0C0FF, 1.8f), E(kFsStreak, kFaSwirl, 6, 0xE0C0FF), E0}, 0x9A60D0, kSnGust,
     {E(kFsArrow, kFcLand, 1, 0xC070FF), E(kFsStreak, kFcStreak, 3, 0xE0C0FF), E0}},
    {"claws", {E(kFsClaw, kFaDrift, 5, 0xE8F0FF), E(kFsGlint, kFaJump, 6, 0xFFFFFF), E0}, 0xFFD040, kSnClink,
     {E(kFsClaw, kFcCorner, 1, 0xFFFFFF), E(kFsStreak, kFcSweep, 1, 0xFFFFFF), E0}},
    {"deadpool", {E(kFsBlade, kFaSwirl, 6, 0xE8F0FF, 0, 1.8f), E(kFsHeart, kFaRise, 8, 0xFF3040, 0, 1.3f), E0}, 0xFF3040, kSnCrowd,
     {E(kFsBlade, kFcLand, 1, 0xE8F0FF), E(kFsHeart, kFcBurst, 3, 0xFF4050), E0}},
    {"street", {E(kFsGlint, kFaJump, 8, 0xFFFFFF), E(kFsStreak, kFaSwirl, 6, 0xDDE8FF), E0}, 0xB0B8D0, kSnGust,
     {E(kFsStreak, kFcSweep, 1, 0xFFFFFF), E(kFsGlint, kFcBurst, 3, 0xFFFFFF), E0}},
    // --- Strikers / Charged / Melee: their own things
    {"mario", {E(kFsCoin, kFaSwirl, 10, 0xFFFFFF), E(kFsFireball, kFaDrift, 5, 0xFFFFFF), E0}, 0xFF4040, kSnCrowd,
     {E(kFsCoin, kFcBurst, 4, 0xFFFFFF), E(kFsFireball, kFcEdge, 4, 0xFFFFFF), E0}},
    {"luigi", {E(kFsCoin, kFaSwirl, 10, 0xFFFFFF), E(kFsFireball, kFaDrift, 5, 0x9CFF9A), E0}, 0x40D040, kSnCrowd,
     {E(kFsCoin, kFcBurst, 4, 0xFFFFFF), E(kFsFireball, kFcEdge, 4, 0x9CFF9A), E0}},
    {"peach", {E(kFsHeart, kFaRise, 10, 0xFF80C0), E(kFsPetal, kFaFall, 12, 0xFFB0D0), E0}, 0xFF9AD0, kSnTwinkle,
     {E(kFsHeart, kFcBurst, 4, 0xFF80C0), E(kFsPetal, kFcLand, 1, 0xFFB0D0), E0}},
    {"daisy", {E(kFsPetal, kFaFall, 12, 0xFFD040, 0xFFA040), E(kFsSpark, kFaDrift, 8, 0xFFE080), E0}, 0xFFB040, kSnTwinkle,
     {E(kFsPetal, kFcLand, 1, 0xFFD040), E(kFsSpark, kFcBurst, 4, 0xFFE080), E0}},
    {"yoshi", {E(kFsEgg, kFaDrift, 6, 0xFFFFFF), E(kFsHeart, kFaRise, 6, 0x70E070), E0}, 0x60E060, kSnCrowd,
     {E(kFsEgg, kFcLand, 1, 0xFFFFFF), E(kFsSpark, kFcBurst, 4, 0xB0FFB0), E0}},
    {"dk", {E(kFsBanana, kFaSwirl, 12, 0xFFFFFF, 0, 1.5f), E(kFsStar, kFaDrift, 6, 0xFFE070), E0}, 0xFFD040, kSnThump,
     {E(kFsBanana, kFcLand, 1, 0xFFFFFF), E(kFsStar, kFcBurst, 3, 0xFFE070), E0}},
    {"wario", {E(kFsCoin, kFaSwirl, 10, 0xFFFFFF), E(kFsPuff, kFaFloor, 5, 0x8A8A20, 0xA8A030), E0}, 0xFFD020, kSnCrowd,
     {E(kFsCoin, kFcBurst, 4, 0xFFFFFF), E0, E0}},
    {"waluigi", {E(kFsStar, kFaSwirl, 10, 0xA040E0, 0xD090FF), E(kFsPetal, kFaFall, 10, 0x9A40E0), E0}, 0x8A40D0, kSnCrowd,
     {E(kFsStar, kFcBurst, 5, 0xC070FF), E0, E0}},
    {"toad", {E(kFsMushroom, kFaDrift, 6, 0xFFFFFF), E(kFsStar, kFaDrift, 8, 0xFFE070), E0}, 0xFF6060, kSnTwinkle,
     {E(kFsMushroom, kFcLand, 1, 0xFFFFFF), E(kFsSpark, kFcBurst, 3, 0xFFFFFF), E0}},
    {"koopa", {E(kFsShell, kFaSwirl, 8, 0xFFFFFF, 0, 1.6f), E(kFsStar, kFaDrift, 6, 0xA0FFA0), E0}, 0x60D060, kSnCrowd,
     {E(kFsShell, kFcLand, 1, 0xFFFFFF), E(kFsSpark, kFcBurst, 3, 0xC0FFC0), E0}},
    {"hammer bro", {E(kFsHammer, kFaSwirl, 8, 0xFFFFFF, 0, 1.6f), E(kFsGlint, kFaJump, 5, 0xFFFFFF), E0}, 0x60A0FF, kSnClink,
     {E(kFsHammer, kFcLand, 1, 0xFFFFFF), E(kFsGlint, kFcBurst, 2, 0xFFFFFF), E0}},
    {"birdo", {E(kFsEgg, kFaDrift, 6, 0xFFC0E0), E(kFsHeart, kFaRise, 8, 0xFF80C0), E0}, 0xFF80C0, kSnTwinkle,
     {E(kFsEgg, kFcLand, 1, 0xFFC0E0), E(kFsHeart, kFcBurst, 3, 0xFF80C0), E0}},
    {"bowser", {E(kFsFireball, kFaDrift, 6, 0xFFFFFF), E(kFsEmber, kFaRise, 14, 0xFF6010, 0xFFA030), E0}, 0xFF6A20, kSnRoar,
     {E(kFsFlame, kFcEdge, 12, 0xFF6010, 0xFFB030), E(kFsSoft, kFcGlow, 1, 0xFF5010), E0}},
    {"bowser jr", {E(kFsFireball, kFaDrift, 5, 0xFFFFFF), E(kFsStar, kFaDrift, 8, 0xFFC060), E0}, 0xFF9040, kSnRoar,
     {E(kFsFlame, kFcEdge, 8, 0xFF8030, 0xFFC050), E(kFsStar, kFcBurst, 3, 0xFFC060), E0}},
    {"diddy", {E(kFsBanana, kFaSwirl, 12, 0xFFFFFF, 0, 1.5f), E(kFsConfetti, kFaFall, 8, 0xFF4040, 0xFFE040), E0}, 0xFFD040, kSnCrowd,
     {E(kFsBanana, kFcLand, 1, 0xFFFFFF), E(kFsStar, kFcBurst, 3, 0xFFE070), E0}},
    {"petey", {E(kFsPetal, kFaFall, 10, 0xFF5050, 0xFFFFFF), E(kFsBubble, kFaRise, 8, 0xB070E0), E0}, 0x60C040, kSnBubbling,
     {E(kFsBubble, kFcInside, 8, 0xC080FF), E(kFsPetal, kFcLand, 1, 0xFF5050), E0}},
    {"boo", {E(kFsWisp, kFaDrift, 10, 0xE8E8FF, 0xC8C8FF), E(kFsPuff, kFaFloor, 5, 0, 0, 1.8f), E0}, 0xC8C8FF,
     kSnWhisper, {E(kFsWisp, kFcInside, 5, 0xE8E8FF), E(kFsPuff, kFcDrift, 2, 0x8A8AC0), E0}},
    {"dry bones", {E(kFsBone, kFaSwirl, 6, 0xE8E8E0), E(kFsFlame, kFaRise, 10, 0x60A0FF, 0xA0D0FF), E0}, 0x7090FF, kSnWhisper,
     {E(kFsFlame, kFcEdge, 12, 0x5090FF, 0xA0D0FF), E(kFsBone, kFcLand, 1, 0xE8E8E0), E0}},
    {"monty mole", {E(kFsPebble, kFaRise, 10, 0xA07040, 0xC09060), E(kFsDust, kFaFloor, 8, 0xB08860), E0}, 0xC08050, kSnRumble,
     {E(kFsDust, kFcFallOff, 6, 0xB08860), E(kFsCrack, kFcCorner, 2, 0xFFD090), E0}},
    {"shy guy", {E(kFsConfetti, kFaFall, 16, 0xFF4040, 0xFFFFFF), E(kFsHeart, kFaRise, 5, 0xFF6060), E0}, 0xFF5050, kSnCrowd,
     {E(kFsConfetti, kFcBurst, 6, 0xFF4040, 0xFFFFFF), E0, E0}},
    {"fox", {E(kFsLaser, kFaSwirl, 10, 0x40C0FF, 0xA0E8FF), E0, E0}, 0x4080FF, kSnStatic,
     {E(kFsLaser, kFcStreak, 4, 0x60D0FF), E0, E0}},
    {"falco", {E(kFsLaser, kFaSwirl, 10, 0xFF5050, 0xFFA0A0), E(kFsFeather, kFaFall, 6, 0x6080FF), E0}, 0x4060FF, kSnStatic,
     {E(kFsLaser, kFcStreak, 4, 0xFF6060), E(kFsFeather, kFcLand, 1, 0x80A0FF), E0}},
    {"falcon", {E(kFsEmber, kFaRise, 18, 0xFF8030, 0xFFD060), E(kFsFlame, kFaRise, 4, 0xFF6020, 0xFFB040, 1.6f), E0}, 0xFF8030,
     kSnCrackle, {E(kFsFlame, kFcEdge, 14, 0xFF7020, 0xFFD050), E(kFsSoft, kFcGlow, 1, 0xFF6010), E0}},
    {"link", {E(kFsRupee, kFaSwirl, 10, 0xFFFFFF, 0x80B0FF), E(kFsSpark, kFaDrift, 8, 0xFFFFC0), E0}, 0x50D070, kSnTwinkle,
     {E(kFsRupee, kFcLand, 1, 0xFFFFFF), E(kFsSpark, kFcBurst, 4, 0xFFFFC0), E0}},
    {"zelda", {E(kFsSpark, kFaDrift, 14, 0xFF90FF, 0x90C0FF), E(kFsRune, kFaDrift, 4, 0xFFB0FF), E0}, 0xD080FF, kSnTwinkle,
     {E(kFsRune, kFcGlow, 1, 0xFFA0FF), E(kFsSpark, kFcBurst, 4, 0xFFB0FF), E0}},
    {"sheik", {E(kFsStreak, kFaSwirl, 10, 0xE0E8FF), E(kFsPuff, kFaJump, 4, 0x8090C0, 0, 1.4f), E0}, 0x8090C0, kSnGust,
     {E(kFsStreak, kFcStreak, 4, 0xE0E8FF), E0, E0}},
    {"samus", {E(kFsOrb, kFaDrift, 10, 0xFF9030, 0x60FF90), E0, E0}, 0xFF8030, kSnHum,
     {E(kFsOrb, kFcInside, 6, 0xFFA040, 0x80FFA0), E(kFsSoft, kFcGlow, 1, 0xFF8030), E0}},
    {"kirby", {E(kFsStar, kFaSwirl, 12, 0xFFE040, 0xFF90C0), E0, E0}, 0xFF9AD0, kSnTwinkle,
     {E(kFsStar, kFcBurst, 6, 0xFFE040, 0xFF90C0), E0, E0}},
    {"ness", {E(kFsPsi, kFaPulse, 5, 0xFF60C0), E(kFsStar, kFaDrift, 8, 0xFFE070), E0}, 0xFF70C0, kSnHum,
     {E(kFsPsi, kFcRipple, 3, 0xFF70C0), E0, E0}},
    {"ice climbers", {E(kFsFlake, kFaFall, 18, 0xEAF6FF), E(kFsPuff, kFaFloor, 6, 0x6A9AC8, 0x8AB8E0, 2.0f), E0}, 0x8FD0FF,
     kSnChimes, {E(kFsFrost, kFcCorner, 4, 0xDDF4FF), E(kFsIcicle, kFcHang, 6, 0xCFEFFF), E0}},
    {"marth", {E(kFsGlint, kFaJump, 8, 0xFFFFFF), E(kFsPetal, kFaFall, 8, 0x8090FF), E0}, 0x6080FF, kSnClink,
     {E(kFsStreak, kFcSweep, 1, 0xFFFFFF), E(kFsGlint, kFcBurst, 3, 0xFFFFFF), E0}},
    {"roy", {E(kFsEmber, kFaRise, 18, 0xFF6020, 0xFFB040), E0, E0}, 0xFF6020, kSnCrackle,
     {E(kFsFlame, kFcEdge, 12, 0xFF6020, 0xFFC050), E(kFsSoft, kFcGlow, 1, 0xFF5010), E0}},
    {"ganondorf", {E(kFsFlame, kFaRise, 10, 0x8A30E0, 0xC070FF), E(kFsTendril, kFaFloor, 6, 0, 0, 1.6f), E0}, 0x6020A0, kSnDrone,
     {E(kFsFlame, kFcEdge, 12, 0x8A30E0, 0xC070FF), E(kFsPuff, kFcSeep, 4, 0, 0, 1.3f), E0}},
    {"game & watch", {E(kFsConfetti, kFaFall, 12, 0xD0D0D0), E(kFsSpark, kFaJump, 6, 0xFFFFFF), E0}, 0xE0E0E0, kSnTwinkle,
     {E(kFsConfetti, kFcBurst, 4, 0xE0E0E0), E0, E0}},
    {"dr. mario", {E(kFsPill, kFaSwirl, 10, 0xFFFFFF), E0, E0}, 0xFF6060, kSnTwinkle,
     {E(kFsPill, kFcLand, 1, 0xFFFFFF), E(kFsSpark, kFcBurst, 3, 0xFFFFFF), E0}},
};

// A card's theme: a Pokemon by its type (types.txt), a hero by power, the rest by who they are
std::unordered_map<std::string, int> g_fxTypes;
bool g_fxTypesRead = false;
int fx_type_theme(const std::string& t1, const std::string& t2) {
  static const std::pair<const char*, int> kT[] = {
      {"normal", tNormal},     {"fire", tFire},   {"water", tWater},     {"grass", tGrass},       {"electric", tElectric},
      {"psychic", tPsychic},   {"ghost", tGhost}, {"ice", tIce},         {"rock", tRock},         {"ground", tRock},
      {"flying", tFlying},     {"poison", tPoison}, {"bug", tBug},       {"dragon", tDragon},     {"fighting", tFighting},
      {"steel", tSteel},       {"dark", tDark},   {"fairy", tFairy}};
  if (t1 == "normal" && t2 == "flying") return tFlying;  // (a bird reads as a bird)
  for (const auto& k : kT)
    if (t1 == k.first) return k.second;
  return tNormal;
}
int fx_theme_of(const std::string& key) {
  if (key.compare(0, 3, "pk_") == 0) {
    if (!g_fxTypesRead) {
      g_fxTypesRead = true;
      if (FILE* f = std::fopen((home_dir() + "/franchise/types.txt").c_str(), "r")) {
        char line[256], k[96], a[32], b[32];
        while (std::fgets(line, sizeof(line), f) != nullptr) {
          if (line[0] == '#') continue;
          b[0] = '\0';
          if (std::sscanf(line, "%95s %31s %31s", k, a, b) >= 2) g_fxTypes[k] = fx_type_theme(a, b);
        }
        std::fclose(f);
      }
    }
    auto it = g_fxTypes.find(key);
    return it != g_fxTypes.end() ? it->second : tNormal;
  }
  if (key.compare(0, 4, "mua_") == 0) {
    std::string h = key.substr(4);
    const size_t c = h.find("_c");
    if (c != std::string::npos) h.resize(c);
    static const std::pair<const char*, int> kH[] = {
        {"ghostrider", mHellfire}, {"humantorch", mFire},       {"thor", mLightning},   {"storm", mLightning},
        {"drdoom", mDoom},         {"iceman", mIce},            {"drstrange", mRunes},  {"jeangrey", mPhoenix},
        {"professorx", mPsi},      {"invisiblewoman", mInvisible}, {"mysterio", mMysterio}, {"ironman", mCosmic},
        {"silversurfer", mCosmic}, {"msmarvel", mCosmic},       {"captainmarvel", mCosmic}, {"vision", mCosmic},
        {"blade", mShadow},        {"moonknight", mShadow},     {"nightcrawler", mTeleport}, {"thing", mStrength},
        {"colossus", mStrength},   {"lukecage", mStrength},     {"rhino", mStrength},   {"spiderman", mWeb},
        {"spiderwoman", mWeb},     {"captainamerica", mShield}, {"hawkeye", mArrows},   {"ronin", mArrows},
        {"wolverine", mClaws},     {"deadpool", mDeadpool}};
    for (const auto& e : kH)
      if (h == e.first) return e.second;
    return mStreet;
  }
  static const std::pair<const char*, int> kS[] = {
      {"strikers_mario", sMario},   {"strikers_luigi", sLuigi},     {"strikers_peach", sPeach},   {"strikers_daisy", sDaisy},
      {"strikers_yoshi", sYoshi},   {"strikers_donkeykong", sDK},   {"strikers_wario", sWario},   {"strikers_waluigi", sWaluigi},
      {"strikers_toad", sToad},     {"strikers_koopa", sKoopa},     {"strikers_hammerbro", sHammer}, {"strikers_birdo", sBirdo},
      {"bowser", sBowser},          {"bowserjr", sBowserJr},        {"diddykong", sDiddy},        {"petey", sPetey},
      {"boo", sBoo},                {"drybones", sDryBones},        {"montymole", sMonty},        {"shyguy", sShyGuy},
      {"fox", sFox},                {"falco", sFalco},              {"falcon", sFalcon},          {"link", sLink},
      {"younglink", sLink},         {"zelda", sZelda},              {"sheik", sSheik},            {"samus", sSamus},
      {"kirby", sKirby},            {"ness", sNess},                {"popo", sIceClimbers},       {"nana", sIceClimbers},
      {"marth", sMarth},            {"roy", sRoy},                  {"ganondorf", sGanon},        {"gamewatch", sGameWatch},
      {"drmario", sDrMario}};
  for (const auto& e : kS)
    if (key == e.first) return e.second;
  // r183: the ORIGINALS by what they are (a uniform as its single)
  if (key.compare(0, 5, "orig_") == 0) {
    std::string h = key.substr(5);
    if (h.size() > 2 && h[h.size() - 2] == '_') h.resize(h.size() - 2);
    static const std::pair<const char*, int> kO[] = {
        {"quarterbyte", sGameWatch}, {"highscore", tElectric}, {"gameover", mDoom},
        {"rookie", tSteel},          {"gambit", sMarth},       {"checkmate", sRoy},
        {"tatami", tFighting},       {"nightstep", sSheik},    {"overclock", tElectric},
        {"tincan", tSteel},          {"ironclad", mShield},    {"wyrmguard", tDragon},
        {"graves", mShadow},         {"halfmoon", mClaws},     {"fullmoon", tDark},
        {"luchador", mStrength},     {"racedriver", sFalcon},  {"goalie", tIce},
        {"mascot", mStreet},         {"redline", mLightning}};
    for (const auto& e : kO)
      if (h == e.first) return e.second;
  }
  return tNormal;
}

// The atlas: 8 x 8 cells of 64 px, each sprite drawn in light (premultiplied colour, alpha 0: added): most in white, to
// be tinted; the things that have their own colours (coins, bananas, rupees ...) in them
constexpr int kFxAtlas = 512, kFxCell = 64;
std::vector<uint8_t> fx_atlas() {
  Pc P;
  auto at = [](int i, float* cx, float* cy) {
    *cx = (i % 8) * kFxCell + 0.5f * kFxCell;
    *cy = (i / 8) * kFxCell + 0.5f * kFxCell;
  };
  auto disc = [&](float x, float y, float r, uint32_t rgb, float a = 1.0f) { P.box(x - r, y - r, x + r, y + r, r, rgb, a); };
  auto glow = [&](float x, float y, float r, uint32_t rgb, float a = 1.0f) { P.glow(x - r, y - r, x + r, y + r, rgb, a); };
  auto ring = [&](float x, float y, float r, float w, uint32_t rgb, float a = 1.0f) { P.ring(x - r, y - r, x + r, y + r, r, w, rgb, a); };
  auto line = [&](float x0, float y0, float x1, float y1, float w, uint32_t rgb, float a = 1.0f) {
    const float dx = x1 - x0, dy = y1 - y0, l = std::max(0.01f, std::sqrt(dx * dx + dy * dy)), nx = -dy / l * 0.5f * w, ny = dx / l * 0.5f * w;
    polyv(P, {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny}, rgb, a);
  };
  auto ellipse = [&](float x, float y, float rx, float ry, float rot, uint32_t rgb, float a = 1.0f) {
    std::vector<float> pts;
    for (int k = 0; k < 20; ++k) {
      const float t = 6.2831853f * k / 20.0f, u = rx * std::cos(t), w = ry * std::sin(t);
      pts.push_back(x + u * std::cos(rot) - w * std::sin(rot));
      pts.push_back(y + u * std::sin(rot) + w * std::cos(rot));
    }
    polyv(P, pts, rgb, a);
  };
  auto star = [&](float x, float y, float r, float ri, int n, float rot, uint32_t rgb, float a = 1.0f) {  // (a point up)
    P.burst(x - r, y - r, x + r, y + r, n, ri / r, -90.0f + rot * 57.29578f, rgb, a);
  };
  float x = 0, y = 0;
  const uint32_t W = 0xFFFFFF;
  at(kFsSoft, &x, &y), glow(x, y, 31, W);
  at(kFsEmber, &x, &y), glow(x, y, 18, W, 0.85f), ellipse(x, y + 4, 4, 9, 0.0f, W), disc(x, y, 4, W);
  at(kFsBubble, &x, &y), glow(x, y, 26, W, 0.12f), ring(x, y, 24, 2.5f, W, 0.85f), disc(x - 9, y - 9, 4, W), disc(x + 10, y + 8, 2, W, 0.6f);
  at(kFsDrop, &x, &y), ellipse(x, y + 3, 13, 16, 0.0f, W, 0.35f), ring(x, y + 3, 14, 2.0f, W, 0.6f), disc(x - 5, y - 4, 4, W),
      disc(x + 5, y + 11, 2, W, 0.6f);  // (a bead of water: clear, its rim and a highlight)
  at(kFsLeaf, &x, &y), ellipse(x, y, 25, 11, -0.6f, W, 0.85f), line(x - 20, y + 15, x + 20, y - 15, 2.0f, W, 0.5f);
  at(kFsPollen, &x, &y), glow(x, y, 20, W, 0.5f), disc(x - 6, y - 4, 3, W), disc(x + 5, y + 2, 3.5f, W), disc(x, y + 8, 2.5f, W);
  at(kFsSpark, &x, &y), glow(x, y, 16, W, 0.7f), star(x, y, 26, 4, 4, 0.0f, W), disc(x, y, 3, W);
  at(kFsRing, &x, &y), glow(x, y, 30, W, 0.15f), ring(x, y, 25, 3.0f, W, 0.9f);
  at(kFsOrb, &x, &y), glow(x, y, 28, W, 0.7f), disc(x, y, 10, W, 0.95f);
  at(kFsWisp, &x, &y);
  for (int k = 0; k < 9; ++k) {
    const float t = k / 8.0f;
    glow(x - 18 + 36 * t, y + 12 * std::sin(t * 6.0f) - 8 * t, 12 - 8 * t, W, 0.55f - 0.3f * t);
  }
  at(kFsFlake, &x, &y);
  for (int k = 0; k < 6; ++k) {
    const float t = 1.0471976f * k, c = std::cos(t), s = std::sin(t);
    line(x, y, x + 24 * c, y + 24 * s, 2.5f, W);
    line(x + 14 * c, y + 14 * s, x + 14 * c + 7 * std::cos(t + 0.8f), y + 14 * s + 7 * std::sin(t + 0.8f), 1.8f, W);
    line(x + 14 * c, y + 14 * s, x + 14 * c + 7 * std::cos(t - 0.8f), y + 14 * s + 7 * std::sin(t - 0.8f), 1.8f, W);
  }
  at(kFsPebble, &x, &y), polyv(P, {x - 14, y - 6, x - 4, y - 14, x + 10, y - 12, x + 16, y, x + 8, y + 12, x - 8, y + 13, x - 16, y + 4}, 0xD0D0D0, 0.9f),
      disc(x - 4, y - 6, 4, W, 0.5f);
  at(kFsDust, &x, &y), glow(x - 6, y - 3, 18, W, 0.45f), glow(x + 7, y + 4, 16, W, 0.4f), glow(x, y + 8, 12, W, 0.35f);
  at(kFsFeather, &x, &y), line(x - 22, y + 20, x + 20, y - 20, 1.6f, W), ellipse(x + 2, y - 2, 24, 8, -0.78f, W, 0.75f);
  at(kFsStreak, &x, &y), P.box(x - 28, y - 1.5f, x + 28, y + 1.5f, 1.5f, W, 0.9f), P.box(x - 28, y - 4, x + 28, y + 4, 4, W, 0.25f);
  at(kFsPuff, &x, &y), glow(x - 9, y + 3, 17, W, 0.6f), glow(x + 9, y + 2, 16, W, 0.6f), glow(x, y - 8, 17, W, 0.6f), glow(x, y + 6, 22, W, 0.3f);
  at(kFsFirefly, &x, &y), glow(x, y, 22, W, 0.65f), disc(x, y, 4, W);
  at(kFsScale, &x, &y), polyv(P, {x, y - 22, x + 16, y - 4, x + 10, y + 16, x - 10, y + 16, x - 16, y - 4}, W, 0.8f),
      line(x - 12, y - 4, x, y - 18, 2.0f, W);
  at(kFsShock, &x, &y), ring(x, y, 26, 6.0f, W, 0.9f), glow(x, y, 31, W, 0.2f);
  at(kFsConfetti, &x, &y), polyv(P, {x - 12, y - 6, x + 10, y - 12, x + 13, y - 2, x - 9, y + 4}, W);
  at(kFsStar, &x, &y), glow(x, y, 26, W, 0.3f), star(x, y, 22, 9, 5, 0.0f, W);
  at(kFsGlint, &x, &y), glow(x, y, 12, W, 0.8f), P.box(x - 28, y - 1.2f, x + 28, y + 1.2f, 1.2f, W), P.box(x - 1.2f, y - 28, x + 1.2f, y + 28, 1.2f, W);
  at(kFsTendril, &x, &y);
  for (int k = 0; k < 14; ++k) {
    const float t = k / 13.0f;
    glow(x - 26 + 52 * t, y + 9 * std::sin(t * 9.0f), 7 - 4 * t, W, 0.7f);
  }
  at(kFsHeart, &x, &y), disc(x - 8, y - 5, 10, W), disc(x + 8, y - 5, 10, W), polyv(P, {x - 17, y - 1, x + 17, y - 1, x, y + 20}, W);
  at(kFsFlame, &x, &y), polyv(P, {x, y - 28, x + 7, y - 12, x + 14, y + 6, x + 9, y + 20, x, y + 24, x - 9, y + 20, x - 14, y + 6, x - 7, y - 12}, W, 0.75f),
      polyv(P, {x, y - 10, x + 6, y + 6, x, y + 18, x - 6, y + 6}, W);
  at(kFsBolt, &x, &y), glow(x, y, 26, W, 0.3f), line(x + 6, y - 27, x - 6, y - 3, 6.0f, W), line(x - 7, y - 4, x + 7, y + 1, 4.5f, W),
      line(x + 6, y, x - 5, y + 27, 3.5f, W);
  at(kFsCrack, &x, &y), line(x - 26, y - 20, x - 6, y - 4, 2.5f, W), line(x - 6, y - 4, x + 4, y + 18, 2.5f, W),
      line(x - 6, y - 4, x + 24, y - 10, 2.0f, W), line(x + 4, y + 18, x + 22, y + 26, 1.5f, W), line(x + 4, y + 18, x - 12, y + 28, 1.5f, W),
      glow(x - 6, y - 4, 10, W, 0.6f);
  at(kFsFrost, &x, &y);
  for (int k = 0; k < 3; ++k) {
    const float a0 = -1.2f + 0.9f * k;
    const float ex = x - 26 + 50 * std::cos(a0 * 0.4f), ey = y + 26 - 50 * std::sin(0.6f + 0.25f * k);
    line(x - 26, y + 26, ex, ey, 2.0f, W, 0.9f);
    for (int j = 1; j < 4; ++j) {
      const float px = x - 26 + (ex - x + 26) * j / 4.0f, py = y + 26 + (ey - y - 26) * j / 4.0f;
      line(px, py, px + 8, py - 3, 1.4f, W, 0.8f);
      line(px, py, px - 3, py - 8, 1.4f, W, 0.8f);
    }
  }
  at(kFsIcicle, &x, &y), polyv(P, {x - 7, y - 28, x + 7, y - 28, x, y + 28}, W, 0.85f), line(x - 2, y - 26, x, y + 18, 1.5f, W);
  at(kFsVine, &x, &y);
  for (int k = 0; k < 10; ++k) {
    const float t = k / 9.0f;
    glow(x - 26 + 50 * t, y + 26 - 50 * t + 10 * std::sin(t * 7.0f), 4, W, 0.9f);
  }
  ellipse(x - 10, y + 6, 8, 4, 0.5f, W), ellipse(x + 6, y - 8, 8, 4, -0.7f, W), ellipse(x + 18, y - 20, 6, 3, 0.3f, W);
  at(kFsRune, &x, &y), ring(x, y, 26, 2.5f, W), ring(x, y, 17, 1.8f, W, 0.8f), line(x, y - 15, x + 13, y + 8, 1.8f, W),
      line(x + 13, y + 8, x - 13, y + 8, 1.8f, W), line(x - 13, y + 8, x, y - 15, 1.8f, W);
  at(kFsCaustic, &x, &y);  // (pool light: a net of bright wavering lines over a faint glow)
  glow(x, y, 30, W, 0.25f);
  {
    float gx[16], gy[16];
    for (int k = 0; k < 16; ++k) {
      gx[k] = x - 24 + 16 * (k % 4) + 5.0f * std::sin(k * 2.3f);
      gy[k] = y - 24 + 16 * (k / 4) + 5.0f * std::cos(k * 1.7f);
    }
    auto wave = [&](int a, int b) {  // (each strand bowed: wavering, not a wire net)
      const float mx = 0.5f * (gx[a] + gx[b]) + 3.5f * std::sin(a * 1.9f + b), my = 0.5f * (gy[a] + gy[b]) + 3.5f * std::cos(a * 1.3f + b);
      line(gx[a], gy[a], mx, my, 1.6f, W, 0.85f);
      line(mx, my, gx[b], gy[b], 1.6f, W, 0.85f);
    };
    for (int k = 0; k < 16; ++k) {
      if (k % 4 < 3) wave(k, k + 1);
      if (k / 4 < 3) wave(k, k + 4);
    }
  }
  at(kFsWeb, &x, &y);
  for (int k = 0; k < 5; ++k) line(x - 30, y - 30, x - 30 + 60 * std::cos(0.39f * k), y - 30 + 60 * std::sin(0.39f * k), 1.4f, W);
  for (int r = 1; r <= 3; ++r) {
    std::vector<float> arc;
    for (int k = 0; k < 5; ++k) {
      const float rr = 17.0f * r;
      arc.push_back(x - 30 + rr * std::cos(0.39f * k));
      arc.push_back(y - 30 + rr * std::sin(0.39f * k));
    }
    for (size_t k = 0; k + 3 < arc.size(); k += 2) line(arc[k], arc[k + 1], arc[k + 2], arc[k + 3], 1.2f, W, 0.9f);
  }
  at(kFsShield, &x, &y), disc(x, y, 27, 0xE02030), disc(x, y, 21, 0xFFFFFF), disc(x, y, 15, 0xE02030), disc(x, y, 10, 0x2050D0),
      star(x, y, 9, 3.6f, 5, 0.0f, 0xFFFFFF);
  at(kFsArrow, &x, &y), line(x - 24, y + 24, x + 18, y - 18, 2.5f, W), polyv(P, {x + 26, y - 26, x + 12, y - 20, x + 20, y - 12}, W),
      line(x - 24, y + 24, x - 28, y + 14, 2.0f, W), line(x - 24, y + 24, x - 14, y + 28, 2.0f, W);
  at(kFsClaw, &x, &y);
  for (int k = -1; k <= 1; ++k) line(x - 18 + 12 * k, y + 24, x + 6 + 12 * k, y - 24, 3.0f - std::abs(k) * 0.5f, W);
  at(kFsChain, &x, &y), ring(x - 16, y + 8, 10, 3.0f, W), ring(x, y, 10, 3.0f, W), ring(x + 16, y - 8, 10, 3.0f, W);
  at(kFsCoin, &x, &y), glow(x, y, 30, 0xFFD040, 0.3f), ellipse(x, y, 17, 23, 0.0f, 0xFFC820), ellipse(x, y, 12, 18, 0.0f, 0xFFE878),
      P.box(x - 2.5f, y - 11, x + 2.5f, y + 11, 2.5f, 0xE8A010);
  at(kFsBanana, &x, &y);
  for (int k = 0; k < 9; ++k) {
    const float t = -1.1f + 2.2f * k / 8.0f;
    disc(x + 22 * std::sin(t), y - 2 + 14 * std::cos(t) - 6, 6.5f - std::abs(t) * 2.0f, 0xFFE040);
  }
  at(kFsRupee, &x, &y), polyv(P, {x, y - 26, x + 12, y - 14, x + 12, y + 14, x, y + 26, x - 12, y + 14, x - 12, y - 14}, 0x30D060),
      polyv(P, {x, y - 18, x + 6, y - 10, x + 6, y + 10, x, y + 18, x - 6, y + 10, x - 6, y - 10}, 0x90FFB0);
  at(kFsLaser, &x, &y), glow(x, y, 20, W, 0.3f), P.box(x - 28, y - 2.5f, x + 28, y + 2.5f, 2.5f, W), P.box(x - 28, y - 6, x + 28, y + 6, 6, W, 0.35f);
  at(kFsPetal, &x, &y), ellipse(x, y, 10, 20, 0.5f, W, 0.85f);
  at(kFsFireball, &x, &y), glow(x, y, 30, 0xFF6010, 0.8f), disc(x, y, 13, 0xFFA020), disc(x - 3, y - 3, 7, 0xFFF080);
  at(kFsBeam, &x, &y), P.box(x - 10, y - 28, x + 10, y + 28, 10, W, 0.35f), P.box(x - 4, y - 28, x + 4, y + 28, 4, W, 0.4f);
  at(kFsRipple, &x, &y), ring(x, y, 27, 2.0f, W, 0.7f), ring(x, y, 20, 1.5f, W, 0.4f);
  at(kFsPsi, &x, &y), ring(x, y, 27, 3.0f, W), ring(x, y, 19, 2.5f, W, 0.8f), ring(x, y, 11, 2.0f, W, 0.6f);
  at(kFsMushroom, &x, &y), P.box(x - 8, y, x + 8, y + 22, 6, 0xF0E8D8), ellipse(x, y - 4, 26, 16, 0.0f, 0xE82020), disc(x - 10, y - 8, 5, W),
      disc(x + 9, y - 10, 5, W), disc(x, y - 16, 4, W);
  at(kFsBone, &x, &y), line(x - 16, y + 12, x + 16, y - 12, 7.0f, 0xF0F0E8), disc(x - 20, y + 10, 6, 0xF0F0E8), disc(x - 14, y + 18, 6, 0xF0F0E8),
      disc(x + 14, y - 18, 6, 0xF0F0E8), disc(x + 20, y - 10, 6, 0xF0F0E8);
  at(kFsEgg, &x, &y), ellipse(x, y + 2, 18, 24, 0.0f, 0xF8F8F0), disc(x - 6, y - 6, 6, 0x40C040), disc(x + 8, y + 6, 6, 0x40C040), disc(x - 4, y + 14, 4, 0x40C040);
  at(kFsShell, &x, &y), disc(x, y, 24, W), disc(x, y, 20, 0x30B040), polyv(P, {x, y - 12, x + 11, y - 4, x + 7, y + 10, x - 7, y + 10, x - 11, y - 4}, 0xA0E890);
  at(kFsHammer, &x, &y), line(x - 18, y + 22, x + 8, y - 6, 5.0f, 0xB07840), P.box(x - 4, y - 26, x + 26, y - 6, 4, 0xC8CCD4);
  at(kFsPill, &x, &y), line(x - 14, y + 14, x, y, 14.0f, 0xFF4040), line(x, y, x + 14, y - 14, 14.0f, 0xFFE040), disc(x - 14, y + 14, 7, 0xFF4040),
      disc(x + 14, y - 14, 7, 0xFFE040);
  at(kFsBlade, &x, &y), polyv(P, {x - 26, y + 26, x + 22, y - 24, x + 26, y - 26, x + 24, y - 22, x - 22, y + 28}, W), glow(x + 6, y - 6, 8, W, 0.8f);
  at(kFsFlat, &x, &y), P.box(x - 20, y - 20, x + 20, y + 20, 0, W);  // (sampled at its middle: the tint)
  std::vector<uint8_t> px(static_cast<size_t>(kFxAtlas) * kFxAtlas * 4);
  xrtext::board(px.data(), kFxAtlas, kFxAtlas, P.v);  // (premultiplied: added as light, or its alpha a shade - the shader)
  return px;
}

// The ambient sound of a theme's world: a loop (~2.6 s, faded in and out, laid end over end while you hold it)
std::vector<short> fx_ambient(int snd) {
  constexpr int kRate = 32000;
  const float len = 2.6f;
  std::vector<short> pcm(static_cast<size_t>(len * kRate));
  uint32_t rng = 0x51ED270Bu + static_cast<uint32_t>(snd) * 2654435761u;
  auto rnd = [&rng]() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return static_cast<float>(rng & 0xFFFFFFu) / 8388608.0f - 1.0f;
  };
  float lp = 0.0f, lp2 = 0.0f, ev = 0.0f, evF = 0.0f, evT = 0.0f;
  for (size_t s = 0; s < pcm.size(); ++s) {
    const float t = static_cast<float>(s) / kRate, x = t / len;
    const float env = std::min(1.0f, x / 0.12f) * std::min(1.0f, (1.0f - x) / 0.12f);
    const float n = rnd();
    float v = 0.0f;
    lp += 0.06f * (n - lp);
    lp2 += 0.3f * (n - lp2);
    // an event now and then (a crackle, a bubble, a chirp, a clink ...): its own little sound
    if (ev <= 0.0f && rnd() > 0.9996f) {
      ev = 1.0f;
      evF = 400.0f + 2400.0f * (0.5f + 0.5f * rnd());
      evT = t;
    }
    const float et = t - evT;
    const float evOut = ev > 0.0f ? std::sin(6.2831853f * evF * et) * std::exp(-et * 30.0f) : 0.0f;
    if (ev > 0.0f && et > 0.25f) ev = 0.0f;
    switch (snd) {
      case kSnCrackle: v = 0.25f * lp2 + (rnd() > 0.995f ? 0.9f * rnd() : 0.0f); break;
      case kSnWater: v = 0.6f * lp * (0.7f + 0.3f * std::sin(t * 2.4f)) + 0.35f * std::sin(6.2831853f * (300.0f + 900.0f * et) * et) * std::exp(-et * 18.0f) * (ev > 0.0f ? 1.0f : 0.0f); break;
      case kSnRustle: v = 0.3f * lp2 * (0.5f + 0.5f * std::sin(t * 3.1f)) + 0.25f * std::sin(6.2831853f * (2600.0f + 600.0f * std::sin(et * 60.0f)) * et) * std::exp(-et * 14.0f) * (ev > 0.0f ? 1.0f : 0.0f); break;
      case kSnStatic: v = 0.18f * std::sin(6.2831853f * 120.0f * t) * (0.6f + 0.4f * std::sin(6.2831853f * 240.0f * t)) + (rnd() > 0.997f ? 0.8f * rnd() : 0.0f); break;
      case kSnHum: v = 0.3f * std::sin(6.2831853f * 110.0f * t) * (0.6f + 0.4f * std::sin(t * 5.0f)) + 0.12f * std::sin(6.2831853f * 165.0f * t); break;
      case kSnWhisper: v = 0.5f * lp * (0.4f + 0.6f * std::pow(0.5f + 0.5f * std::sin(t * 1.7f), 2.0f)); break;
      case kSnChimes: v = 0.35f * lp + 0.3f * evOut; break;
      case kSnRumble: v = 1.2f * lp * 0.6f + 0.15f * std::sin(6.2831853f * 45.0f * t); break;
      case kSnGust: v = 0.7f * lp * std::pow(0.5f + 0.5f * std::sin(t * 2.3f), 3.0f); break;
      case kSnBubbling: v = 0.15f * lp + 0.4f * std::sin(6.2831853f * (180.0f + 500.0f * et) * et) * std::exp(-et * 25.0f) * (ev > 0.0f ? 1.0f : 0.0f) + (rnd() > 0.9985f ? 0.4f * rnd() : 0.0f); break;
      case kSnBuzz: v = 0.12f * std::sin(6.2831853f * (220.0f + 10.0f * std::sin(t * 7.0f)) * t) * (0.5f + 0.5f * std::sin(t * 1.3f)) + 0.05f * lp2; break;
      case kSnRoar: v = (x > 0.25f && x < 0.75f ? std::sin(3.14159265f * (x - 0.25f) / 0.5f) : 0.0f) * (0.5f * lp + 0.2f * std::sin(6.2831853f * 70.0f * t)); break;
      case kSnThump: v = 0.6f * std::sin(6.2831853f * 60.0f * std::fmod(t, 0.65f)) * std::exp(-std::fmod(t, 0.65f) * 14.0f) + 0.05f * lp; break;
      case kSnCrowd: v = 0.5f * lp * (0.7f + 0.3f * std::sin(t * 0.9f)) + 0.12f * lp2; break;
      case kSnClink: v = 0.4f * evOut + 0.05f * lp; break;
      case kSnDrone: v = 0.25f * std::sin(6.2831853f * 55.0f * t) + 0.15f * std::sin(6.2831853f * 82.5f * t) * (0.5f + 0.5f * std::sin(t * 0.8f)); break;
      case kSnTwinkle: v = 0.35f * evOut * (0.5f + 0.5f * std::sin(6.2831853f * evF * 1.5f * et)) + 0.03f * lp2; break;
      default: break;
    }
    pcm[s] = static_cast<short>(clampf(v * env * 9000.0f, -32000.0f, 32000.0f));
  }
  return pcm;
}

struct FxPt {
  bool on = false, world = false;  // world: a card's particle let go (a drop dripping off)
  bool shade = false;              // a shade (its stream's colour 0: smoke, shadow), not light
  uint8_t spr = 0, mo = 0;
  uint32_t rgb = 0xFFFFFF;
  XrVector3f p{0.0f, 0.0f, 0.0f}, v{0.0f, 0.0f, 0.0f};
  float age = 0.0f, life = 1.0f, size = 0.03f, rot = 0.0f, spin = 0.0f, a = 0.0f, r = 0.0f, y = 0.0f, grow = 1.0f, base = 1.0f;
  int base0 = 0;  // its quad's first vertex
};
// (r141 proof: the first cut's things round you read as pin-pricks - 2.5-5 cm at 0.7-2 m - now half as many again, 6-12
// cm, 0.6-1.6 m away; the card's 1.2-2 cm)
constexpr int kFxMaxAround = 72, kFxMaxCard = 40;
struct FxState {
  bool up = false;
  int card = -1, theme = 0;
  bool shiny = false;
  float on = 0.0f, cardOn = 0.0f, auraH = 0.08f;
  FxPt around[kFxMaxAround], cardPt[kFxMaxCard];
  int nAround = 0, nCard = 0;
  int aEm[kFxMaxAround] = {}, cEm[kFxMaxCard] = {};  // which emitter (3 = the shiny's rainbow)
  int auraBase = 0, sphereBase = 0, nVerts = 0;
  std::vector<float> verts;
  uint64_t rng = 1;
  std::vector<short> sound;
  float soundT = 0.0f;
  bool landed = false;
  // r142: the room's up in its card's frame this frame (the card held any way: what rises / falls / hangs on it goes by
  // the room's up and down, as its water does; what's part of it - corners, edges, sheen, glow - stays the card's)
  XrVector3f upC{0.0f, 1.0f, 0.0f};
  float realAcc[6] = {};  // r179: its real effects' spawn clocks (fx_real)
};
FxState g_fx;
// r142: the room's up across the card's face (unit; lying flat, no slope across it: its own up, as before)
void fx_up_across(float* dx, float* dy) {
  const XrVector3f& u = g_fx.upC;
  const float l = std::sqrt(u.x * u.x + u.y * u.y);
  *dx = l > 0.3f ? u.x / l : 0.0f;
  *dy = l > 0.3f ? u.y / l : 1.0f;
}
float g_floorY = -1.0e9f;  // (the room's floor, LOCAL; xr_quest.cpp set_floor)
float pull_floor() { return g_floorY > -1.0e8f ? g_floorY : S.head.position.y - 1.6f; }  // r219

float fx_r(uint64_t& s) { return static_cast<float>(rand01(s)); }
uint32_t fx_rgb_mix(uint32_t a, uint32_t b, float t) { return mix_rgb(a, b, clampf(t, 0.0f, 1.0f)); }

// r179 (Lance on his Combusken 1/1: "the flames on the card looked childish"): the worlds whose drawn sprites read as
// cartoon shapes use REAL effects instead (xr_balls.cpp world_fx: the r169 fire flipbook, its embers and smoke, crackling
// bolts, bubbles and splashes, glints and mist) - their atlas streams of those kinds left out:
//   fire / fire hero / hellfire / phoenix: real flames licking up off the card's edges, embers, smoke; flame plumes,
//     embers and smoke round you (no more cartoon flame / ember / fireball sprites)
//   electric / lightning / doom: crackling bolts along the card's edges and striking round you, flying sparks
//   ghost / dark / shadow / teleport / mysterio: wispy real smoke rising off the card, rolling along the floor round you
//   water: real bubbles rising and splashes jumping round you (the card's beading drops / ripples / caustics stay)
//   ice / ice hero: crystal glints and cold mist (the falling flakes, frost and icicles stay)
// (the rest - psychic rings, grass leaves, the heroes' runes / webs / stars ... - read as their things, unchanged)
enum FxReal { kRlNone, kRlFire, kRlBolt, kRlSmoke, kRlWater, kRlIce };
int fx_real_kind(int theme) {
  switch (theme) {
    case tFire: case mFire: case mHellfire: case mPhoenix: return kRlFire;
    case tElectric: case mLightning: case mDoom: return kRlBolt;
    case tGhost: case tDark: case mShadow: case mTeleport: case mMysterio: return kRlSmoke;
    case tWater: return kRlWater;
    case tIce: case mIce: return kRlIce;
    default: return kRlNone;
  }
}
bool fx_real_replaces(int theme, uint8_t spr) {
  switch (fx_real_kind(theme)) {
    case kRlFire: return spr == kFsFlame || spr == kFsEmber || spr == kFsPuff || spr == kFsFireball;
    case kRlBolt: return spr == kFsBolt || spr == kFsSpark;
    case kRlSmoke: return spr == kFsWisp || spr == kFsPuff || spr == kFsTendril;
    case kRlWater: return spr == kFsBubble;
    case kRlIce: return spr == kFsPuff;
    default: return false;
  }
}

// its model: the atlas, a quad a particle (its sprite's cell), the aura's quad, the tint's sphere
void fx_end(bool fxReady) {
  if (g_fx.up && fxReady) xrfx::set_model(kFxSlot, xrfx::Model{});
  g_fx = FxState{};
}
void fx_begin(int card, int theme, bool shiny, bool fxReady) {
  fx_end(fxReady);
  if (!fxReady || theme < 0 || theme >= kFxThemes) return;
  static std::vector<uint8_t> atlas;
  if (atlas.empty()) atlas = fx_atlas();
  FxState& f = g_fx;
  f.card = card;
  f.theme = theme;
  f.shiny = shiny;
  f.rng = 0x1F1ull + static_cast<uint64_t>(theme) * 0x9E3779B97F4A7C15ull;
  const FxTheme& th = kFxTheme[theme];
  xrfx::Model m;
  m.texRgba.push_back(atlas);
  m.texW.push_back(kFxAtlas);
  m.texH.push_back(kFxAtlas);
  int verts = 0;
  auto quad = [&](uint8_t spr) {
    const float u0 = (spr % 8) / 8.0f, v0 = (spr / 8) / 8.0f, u1 = u0 + 1.0f / 8.0f, v1 = v0 + 1.0f / 8.0f, e = 0.5f / kFxAtlas;
    const float us[4] = {u0 + e, u1 - e, u0 + e, u1 - e}, vs[4] = {v0 + e, v0 + e, v1 - e, v1 - e};
    for (int k = 0; k < 4; ++k) {
      m.uv.push_back(us[k]);
      m.uv.push_back(vs[k]);
    }
    xrfx::ModelPart p;
    p.tex = 0;
    p.soft = true;
    p.firstIndex = static_cast<uint32_t>(m.indices.size());
    const uint32_t a = static_cast<uint32_t>(verts);
    m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    p.indexCount = 6;
    m.parts.push_back(p);
    verts += 4;
    return a;
  };
  // round you (its 3 streams, then the shiny's rainbow glitter), on the card (its 3, then the shiny's)
  auto fill = [&](const FxEm* em, int nem, bool card) {
    const int cap = card ? kFxMaxCard : kFxMaxAround;
    int& n = card ? f.nCard : f.nAround;
    for (int e = 0; e <= nem; ++e) {
      const bool rainbow = e == nem;
      if (rainbow && !shiny) break;
      const FxEm r = rainbow ? (card ? E(kFsSpark, kFcBurst, 6, 0xFFFFFF) : E(kFsSpark, kFaDrift, 12, 0xFFFFFF)) : em[e];
      if (!rainbow && fx_real_replaces(theme, r.spr)) continue;  // r179: real effects instead (fx_real)
      const int count = card ? r.n : (3 * r.n + 1) / 2;  // (round you: half as many again)
      for (int k = 0; k < count && n < cap; ++k) {
        FxPt& p = card ? f.cardPt[n] : f.around[n];
        p.spr = r.spr;
        p.mo = r.mo;
        p.base = r.size;
        p.shade = !rainbow && r.c1 == 0;
        p.rgb = rainbow ? 0xFFFFFF : (k % 2 == 0 ? r.c1 : r.c2);
        p.base0 = static_cast<int>(quad(r.spr));
        if (card) f.cEm[n] = rainbow ? 3 : e;
        else f.aEm[n] = rainbow ? 3 : e;
        p.age = -fx_r(f.rng) * 3.0f;  // (they start at different times)
        ++n;
      }
    }
  };
  fill(th.a, 3, false);
  fill(th.c, 3, true);
  f.auraBase = static_cast<int>(quad(kFsSoft));
  // the tint: a sphere 1.35 m round you, one flat colour (its vertices' tint), as the dimming's
  f.sphereBase = verts;
  {
    const float uc = (kFsFlat % 8 + 0.5f) / 8.0f, vc = (kFsFlat / 8 + 0.5f) / 8.0f;
    xrfx::ModelPart p;
    p.tex = 0;
    p.soft = true;
    p.firstIndex = static_cast<uint32_t>(m.indices.size());
    for (int i = 0; i <= 6; ++i)
      for (int j = 0; j <= 10; ++j) {
        m.uv.push_back(uc);
        m.uv.push_back(vc);
      }
    for (int i = 0; i < 6; ++i)
      for (int j = 0; j < 10; ++j) {
        const uint32_t a = static_cast<uint32_t>(verts + i * 11 + j), b = a + 1, c = a + 11, d = c + 1;
        m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
      }
    p.indexCount = static_cast<uint32_t>(m.indices.size()) - p.firstIndex;
    m.parts.push_back(p);
    verts += 77;
  }
  m.vertexCount = static_cast<uint32_t>(verts);
  f.nVerts = verts;
  f.verts.assign(static_cast<size_t>(verts) * 6, 0.0f);
  xrfx::set_model(kFxSlot, std::move(m));
  f.up = xrfx::model_up(kFxSlot);
  f.sound = fx_ambient(th.snd);
  CLOG("1/1 world: %s (%d round you, %d on the card)%s%s", th.name, f.nAround, f.nCard, shiny ? ", shiny" : "",
       f.up ? "" : " - failed");
}

// (a shade: its "normal" -k, xr_fx_model.frag darkens what's behind it by its picture's coverage x k x 0.8)
void fx_put(int base, const XrVector3f& c, const XrQuaternionf& q, float w, float h, uint32_t rgb, float k, bool shade = false) {
  float* o = g_fx.verts.data() + static_cast<size_t>(base) * 6;
  static const float kX[4] = {-0.5f, 0.5f, -0.5f, 0.5f}, kY[4] = {0.5f, 0.5f, -0.5f, -0.5f};
  float r = ((rgb >> 16) & 255) / 255.0f * k, g = ((rgb >> 8) & 255) / 255.0f * k, b = (rgb & 255) / 255.0f * k;
  if (shade) r = g = b = -0.8f * k - 1e-4f;
  for (int i = 0; i < 4; ++i) {
    const XrVector3f p = v_add(c, q_rot(q, XrVector3f{kX[i] * w, kY[i] * h, 0.0f}));
    float* v = o + i * 6;
    v[0] = p.x;
    v[1] = p.y;
    v[2] = p.z;
    v[3] = r;
    v[4] = g;
    v[5] = b;
  }
}
void fx_hide(int base) {
  float* o = g_fx.verts.data() + static_cast<size_t>(base) * 6;
  for (int k = 0; k < 24; ++k) o[k] = 0.0f;
}

// round you: where a stream's things come from (at your sides, behind, above and below - never in front of your eyes,
// never at the card: 0.6 - 1.6 m away, out of the 40 degrees ahead)
void fx_spawn_around(FxPt& p, const XrVector3f& head, const XrVector3f& fwd, float floor) {
  uint64_t& s = g_fx.rng;
  const float side = fx_r(s) < 0.5f ? -1.0f : 1.0f;
  const float az = side * (0.75f + 1.6f * fx_r(s));  // 43 .. 135 degrees off ahead (and some behind)
  const float d = 0.6f + 1.0f * fx_r(s);
  const XrVector3f right{-fwd.z, 0.0f, fwd.x};
  const XrVector3f dir = v_add(v_mul(fwd, std::cos(az)), v_mul(right, std::sin(az)));
  p.p = v_add(head, v_mul(dir, d));
  p.a = std::atan2(dir.z, dir.x);
  p.r = d;
  p.age = 0.0f;
  p.rot = fx_r(s) * 6.2831853f;
  p.spin = (fx_r(s) - 0.5f) * 1.5f;
  p.size = (0.06f + 0.06f * fx_r(s)) * p.base;
  const float low = std::max(floor + 0.05f, head.y - 1.4f);
  switch (p.mo) {
    case kFaRise:
      p.p.y = low + 0.6f * fx_r(s);
      p.v = {(fx_r(s) - 0.5f) * 0.05f, 0.10f + 0.14f * fx_r(s), (fx_r(s) - 0.5f) * 0.05f};
      p.life = 5.0f + 3.0f * fx_r(s);
      break;
    case kFaFall:
      p.p.y = head.y + 0.25f + 0.6f * fx_r(s);
      p.v = {(fx_r(s) - 0.5f) * 0.06f, -(0.12f + 0.12f * fx_r(s)), (fx_r(s) - 0.5f) * 0.06f};
      p.life = 5.0f + 3.0f * fx_r(s);
      break;
    case kFaSwirl:
    case kFaOrbit:
      p.y = head.y - 0.8f + 1.4f * fx_r(s);
      p.r = p.mo == kFaOrbit ? 0.55f + 0.5f * fx_r(s) : 0.7f + 0.8f * fx_r(s);
      p.spin = (p.mo == kFaOrbit ? 0.5f : 0.22f) * (fx_r(s) < 0.5f ? -1.0f : 1.0f) * (0.7f + 0.6f * fx_r(s));
      p.life = 6.0f + 4.0f * fx_r(s);
      if (p.mo == kFaOrbit) p.size *= 0.6f;
      break;
    case kFaJump:
      p.p.y = head.y - 0.7f + 1.3f * fx_r(s);
      p.v = {0.0f, 0.0f, 0.0f};
      p.life = 0.12f + 0.25f * fx_r(s);
      break;
    case kFaFloor:
      p.p.y = (floor > -1.0e8f ? floor : head.y - 1.55f) + 0.03f + 0.18f * fx_r(s);
      p.v = {(fx_r(s) - 0.5f) * 0.08f, 0.0f, (fx_r(s) - 0.5f) * 0.08f};
      p.life = 4.0f + 3.0f * fx_r(s);
      p.size *= 1.6f;
      break;
    case kFaPulse:
      p.p.y = head.y - 0.6f + 0.9f * fx_r(s);
      p.v = {0.0f, 0.0f, 0.0f};
      p.life = 2.4f + 0.8f * fx_r(s);
      break;
    default:
      p.p.y = head.y - 0.7f + 1.2f * fx_r(s);
      p.v = {(fx_r(s) - 0.5f) * 0.1f, (fx_r(s) - 0.5f) * 0.05f, (fx_r(s) - 0.5f) * 0.1f};
      p.life = 5.0f + 3.0f * fx_r(s);
      break;
  }
  p.on = true;
}

// on the card, in its own frame (metres at size 1: its front the z = 0 plane, its window kWin*): where each kind starts
void fx_spawn_card(FxPt& p) {
  uint64_t& s = g_fx.rng;
  const float W = 0.5f * kCardWm, H = 0.5f * kCardHm;
  p.world = false;
  p.age = 0.0f;
  p.rot = fx_r(s) * 6.2831853f;
  p.spin = (fx_r(s) - 0.5f) * 1.0f;
  p.size = (0.012f + 0.008f * fx_r(s)) * p.base;
  p.v = {0.0f, 0.0f, 0.0f};
  p.grow = 1.0f;
  const float z = 2.4f * kCardT;  // (over the obsidian's glass)
  float dx, dy;  // r142: the room's up across its face
  fx_up_across(&dx, &dy);
  switch (p.mo) {
    case kFcEdge: {  // round its edge, licking up / out
      const float u = fx_r(s) * 4.0f;
      const int sideI = std::min(3, static_cast<int>(u));
      const float f = u - sideI;
      p.p = sideI == 0 ? XrVector3f{-W + 2 * W * f, H, z} : sideI == 1 ? XrVector3f{-W + 2 * W * f, -H, z}
          : sideI == 2 ? XrVector3f{W, -H + 2 * H * f, z} : XrVector3f{-W, -H + 2 * H * f, z};
      const float nx = sideI == 2 ? 1.0f : (sideI == 3 ? -1.0f : 0.0f), ny = sideI == 0 ? 1.0f : (sideI == 1 ? -1.0f : 0.0f);
      p.v = {nx * 0.006f, ny * 0.006f, 0.0f};  // (r142: a flame's licking up added as it goes - the room's up)
      // (a bolt lies along its edge; a flame stands up (r142: in the room); the rest turned any way)
      p.rot = p.spr == kFsBolt ? (sideI < 2 ? 1.5707963f : 0.0f) : (p.spr == kFsFlame ? std::atan2(-dx, dy) : fx_r(s) * 6.2831853f);
      p.spin = 0.0f;
      p.life = p.spr == kFsBolt ? 0.08f + 0.12f * fx_r(s) : 0.5f + 0.4f * fx_r(s);
      p.size = (p.spr == kFsBolt ? 0.03f : 0.02f + 0.012f * fx_r(s)) * p.base;
      break;
    }
    case kFcDrops:  // beading on its face, then running down
      p.p = {(fx_r(s) * 2.0f - 1.0f) * (W - 0.006f), (fx_r(s) * 2.0f - 1.0f) * (H - 0.006f), z};
      p.size = 0.0f;
      p.grow = 0.005f + 0.005f * fx_r(s);
      p.life = 6.0f;
      p.rot = 0.0f;
      p.spin = 0.0f;
      break;
    case kFcInside: {  // up through the glass, behind its face (r142: up in the room - from its window's low side)
      const float hx = kWinHalf * 0.85f, hy = 0.5f * (kWinTop - kWinBottom) - 0.004f, cy = 0.5f * (kWinTop + kWinBottom);
      const float e = std::fabs(dx) * hx + std::fabs(dy) * hy, across = std::fabs(dy) * hx + std::fabs(dx) * hy;
      const float sx = (fx_r(s) * 2.0f - 1.0f) * across;  // (perpendicular: -dy, dx)
      p.p = {-dx * e - dy * sx, cy - dy * e + dx * sx, -0.006f - 0.02f * fx_r(s)};
      p.r = (fx_r(s) - 0.5f) * 0.004f;   // its drift across
      p.a = 0.015f + 0.015f * fx_r(s);   // its rise
      p.v = {-dy * p.r + dx * p.a, dx * p.r + dy * p.a, 0.0f};
      p.life = 2.0f * e / p.a;
      break;
    }
    case kFcCorner: case kFcLand: {  // at a corner (vines, frost, webs, cracks: growing) / one resting on its face
      static const float kCx[4] = {-1, 1, -1, 1}, kCy[4] = {1, 1, -1, -1};
      const int c = static_cast<int>(fx_r(s) * 4.0f) & 3;
      p.p = p.mo == kFcLand ? XrVector3f{0.35f * W * (fx_r(s) - 0.5f), 0.25f * H, z}
                            : XrVector3f{kCx[c] * (W - 0.012f), kCy[c] * (H - 0.012f), z};
      p.rot = p.mo == kFcLand ? fx_r(s) * 1.5f - 0.75f : std::atan2(-kCy[c], -kCx[c]) + 2.356f;
      p.spin = 0.0f;
      p.size = (p.mo == kFcLand ? 0.03f : 0.04f) * p.base;
      p.life = 1.0e9f;
      break;
    }
    case kFcHang:  // under its bottom edge (r142: its lowest in the room - placed each frame, fx_frame; v.x along it)
      p.v.x = fx_r(s) * 2.0f - 1.0f;
      p.p = {p.v.x * (W - 0.008f), -H - 0.006f, z};
      p.rot = 0.0f;
      p.spin = 0.0f;
      p.size = (0.012f + 0.01f * fx_r(s)) * p.base;
      p.life = 1.0e9f;
      break;
    case kFcSweep:  // a sheen across it, now and then
      p.p = {-W - 0.03f, 0.0f, z};
      p.v = {0.08f, 0.0f, 0.0f};
      p.rot = -0.6f;
      p.spin = 0.0f;
      p.size = 0.05f * p.base;
      p.life = 2.6f;
      break;
    case kFcStreak:  // streaks across
      p.p = {-W - 0.02f, (fx_r(s) * 2.0f - 1.0f) * H * 0.8f, z};
      p.v = {0.12f + 0.06f * fx_r(s), 0.0f, 0.0f};
      p.rot = 0.0f;
      p.spin = 0.0f;
      p.size = 0.03f * p.base;
      p.life = (2.0f * W + 0.04f) / p.v.x;
      break;
    case kFcBurst:  // popping on its face
      p.p = {(fx_r(s) * 2.0f - 1.0f) * W * 0.85f, (fx_r(s) * 2.0f - 1.0f) * H * 0.85f, z};
      p.life = 0.5f + 0.4f * fx_r(s);
      p.size = (0.014f + 0.008f * fx_r(s)) * p.base;
      break;
    case kFcSeep: {  // creeping in from its edges
      const float u = fx_r(s) * 4.0f;
      const int sideI = std::min(3, static_cast<int>(u));
      const float f = u - sideI;
      p.p = sideI == 0 ? XrVector3f{-W + 2 * W * f, H, z} : sideI == 1 ? XrVector3f{-W + 2 * W * f, -H, z}
          : sideI == 2 ? XrVector3f{W, -H + 2 * H * f, z} : XrVector3f{-W, -H + 2 * H * f, z};
      p.v = v_mul(XrVector3f{-p.p.x, -p.p.y, 0.0f}, 0.12f);
      p.life = 2.5f + 1.0f * fx_r(s);
      p.size = (0.03f + 0.015f * fx_r(s)) * p.base;
      break;
    }
    case kFcRipple:  // rings opening out on its glass
      p.p = {(fx_r(s) * 2.0f - 1.0f) * kWinHalf * 0.6f, kWinBottom + (kWinTop - kWinBottom) * (0.2f + 0.6f * fx_r(s)), z};
      p.life = 2.2f + 0.6f * fx_r(s);
      p.size = 0.0f;
      break;
    case kFcGlow:  // a glow in its glass (its middle), breathing
      p.p = {0.0f, 0.5f * (kWinBottom + kWinTop), -0.01f};
      p.life = 1.0e9f;
      p.size = (p.spr == kFsSoft ? 0.11f : 0.07f) * p.base;
      p.rot = 0.0f;
      break;
    case kFcDrift:  // light / smoke drifting inside its glass
      p.p = {(fx_r(s) * 2.0f - 1.0f) * kWinHalf * 0.7f, kWinBottom + (kWinTop - kWinBottom) * fx_r(s), -0.012f};
      p.v = {(fx_r(s) - 0.5f) * 0.01f, (fx_r(s) - 0.5f) * 0.01f, 0.0f};
      p.life = 4.0f + 2.0f * fx_r(s);
      p.size = (p.spr == kFsCaustic ? 0.05f : 0.035f) * p.base;
      break;
    case kFcFallOff: {  // off its bottom edge, falling (world) (r142: its low edge in the room)
      const float e = std::fabs(dx) * W + std::fabs(dy) * H, across = std::fabs(dy) * W + std::fabs(dx) * H;
      const float sx = (fx_r(s) * 2.0f - 1.0f) * across;
      p.p = {-dx * e - dy * sx, -dy * e + dx * sx, z};
      p.life = 1.2f;
      p.size = (0.008f + 0.006f * fx_r(s)) * p.base;
      break;
    }
    default:  // its face, wandering
      p.p = {(fx_r(s) * 2.0f - 1.0f) * kWinHalf * 0.9f, kWinBottom + (kWinTop - kWinBottom) * fx_r(s), z};
      p.v = {(fx_r(s) - 0.5f) * 0.01f, (fx_r(s) - 0.5f) * 0.01f, 0.0f};
      p.life = 1.6f + 1.4f * fx_r(s);
      break;
  }
  p.on = true;
}

// r179: its REAL effects this frame (fx_real_kind; xr_balls.cpp world_fx): on the card while its face is to you (cardOn),
// round you while you hold it (on). Positions in the room (LOCAL): the card's edge / face points through its frame.
void fx_real(float dt, const XrQuaternionf& q, const XrVector3f& pos, float scale, const XrVector3f& head, const XrVector3f& fwd,
             float floor) {
  FxState& f = g_fx;
  const int kind = fx_real_kind(f.theme);
  if (kind == kRlNone) return;
  uint64_t& s = f.rng;
  const XrVector3f up{0.0f, 1.0f, 0.0f}, zero{0.0f, 0.0f, 0.0f};
  const float W = 0.5f * kCardWm, H = 0.5f * kCardHm, z = 2.4f * kCardT;
  auto every = [&](int slot, float rate) {  // (how many to make this frame at `rate` a second)
    f.realAcc[slot] += rate * dt;
    const int n = static_cast<int>(f.realAcc[slot]);
    f.realAcc[slot] -= static_cast<float>(n);
    return std::min(n, 8);
  };
  auto edge = [&](XrVector3f* along) {  // a point on its edge (the room), the edge's way (the room)
    const float u = fx_r(s) * 4.0f;
    const int side = std::min(3, static_cast<int>(u));
    const float g = u - static_cast<float>(side);
    const XrVector3f l = side == 0 ? XrVector3f{-W + 2 * W * g, H, z} : side == 1 ? XrVector3f{-W + 2 * W * g, -H, z}
                       : side == 2 ? XrVector3f{W, -H + 2 * H * g, z} : XrVector3f{-W, -H + 2 * H * g, z};
    if (along != nullptr) *along = q_rot(q, side < 2 ? XrVector3f{1.0f, 0.0f, 0.0f} : XrVector3f{0.0f, 1.0f, 0.0f});
    return v_add(pos, q_rot(q, v_mul(l, scale)));
  };
  auto face = [&](float in) {  // a point on its face (in: how far in from its edges, 0 .. 1)
    const XrVector3f l{(fx_r(s) * 2.0f - 1.0f) * W * in, (fx_r(s) * 2.0f - 1.0f) * H * in, z};
    return v_add(pos, q_rot(q, v_mul(l, scale)));
  };
  auto around = [&](float yLo, float yHi, float d0, float d1) {  // round you: at your sides / behind, never ahead
    const float sd = fx_r(s) < 0.5f ? -1.0f : 1.0f, az = sd * (0.8f + 1.5f * fx_r(s)), d = d0 + (d1 - d0) * fx_r(s);
    const XrVector3f right{-fwd.z, 0.0f, fwd.x};
    const XrVector3f dir = v_add(v_mul(fwd, std::cos(az)), v_mul(right, std::sin(az)));
    XrVector3f p = v_add(head, v_mul(dir, d));
    p.y = yLo + (yHi - yLo) * fx_r(s);
    return p;
  };
  const float low = floor > -1.0e8f ? floor : head.y - 1.55f;
  const float card = f.cardOn, room = f.on;
  auto rv = [&](float a) { return XrVector3f{(fx_r(s) - 0.5f) * a, (fx_r(s) - 0.5f) * a, (fx_r(s) - 0.5f) * a}; };
  switch (kind) {
    case kRlFire: {
      // on the card: flames licking up off its edges (the room's up), embers flying off, smoke curling away
      for (int i = every(0, 34.0f * card); i > 0; --i) {
        const float sz = (0.018f + 0.016f * fx_r(s)) * scale;
        xrballs::world_fx(xrballs::kWxFlame, v_add(edge(nullptr), v_mul(up, 0.35f * sz)), v_add(v_mul(up, 0.05f), rv(0.02f)), up, sz,
                          0.35f + 0.25f * fx_r(s), 0, 0, up);
      }
      for (int i = every(1, 9.0f * card); i > 0; --i)
        xrballs::world_fx(xrballs::kWxEmber, edge(nullptr), v_add(v_mul(up, 0.08f + 0.1f * fx_r(s)), rv(0.08f)), up, 0.0025f,
                          1.0f + 0.8f * fx_r(s), 0xFFE6A0, fx_r(s) < 0.5f ? 0xFF7A1A : 0xFF4A10, up);
      for (int i = every(2, 1.6f * card); i > 0; --i)
        xrballs::world_fx(xrballs::kWxSmoke, v_add(edge(nullptr), v_mul(up, 0.03f * scale)), v_add(v_mul(up, 0.04f), rv(0.02f)), up,
                          0.02f * scale, 1.6f + fx_r(s), 0x2A2420, 0, up);
      // round you: flames standing up out of the floor at your sides and behind, embers rising, smoke
      for (int i = every(3, 4.5f * room); i > 0; --i) {
        const float sz = 0.16f + 0.14f * fx_r(s);
        XrVector3f p = around(low, low, 0.8f, 1.6f);
        p.y = low + 0.36f * sz;
        xrballs::world_fx(xrballs::kWxFlame, p, v_mul(up, 0.15f), up, sz, 0.7f + 0.5f * fx_r(s), 0, 0, up);
      }
      for (int i = every(4, 16.0f * room); i > 0; --i)
        xrballs::world_fx(xrballs::kWxEmber, around(low, low + 0.6f, 0.6f, 1.6f), v_add(v_mul(up, 0.15f + 0.2f * fx_r(s)), rv(0.12f)), up,
                          0.004f, 2.0f + 1.5f * fx_r(s), 0xFFE6A0, fx_r(s) < 0.5f ? 0xFF7A1A : 0xFF4A10, up);
      for (int i = every(5, 1.2f * room); i > 0; --i)
        xrballs::world_fx(xrballs::kWxSmoke, around(low + 0.3f, low + 0.9f, 1.0f, 1.8f), v_add(v_mul(up, 0.12f), rv(0.05f)), up, 0.22f,
                          3.0f + fx_r(s), 0x2A2420, 0, up);
      break;
    }
    case kRlBolt: {
      const uint32_t glow = f.theme == mDoom ? 0x7CFF7A : (f.theme == mLightning ? 0x9FC8FF : 0xFFE860);
      for (int i = every(0, 14.0f * card); i > 0; --i) {  // crackling along its edges
        XrVector3f along;
        const XrVector3f p = edge(&along);
        if (fx_r(s) < 0.5f) along = v_mul(along, -1.0f);
        xrballs::world_fx(xrballs::kWxBolt, p, zero, up, (0.03f + 0.03f * fx_r(s)) * scale, 0.08f + 0.08f * fx_r(s), 0xFFFFFF, glow, along);
      }
      for (int i = every(1, 10.0f * card); i > 0; --i)
        xrballs::world_fx(xrballs::kWxEmber, edge(nullptr), rv(0.6f), up, 0.0018f, 0.25f + 0.2f * fx_r(s), 0xFFFFFF, glow, up);
      for (int i = every(3, 3.0f * room); i > 0; --i) {  // striking round you
        const XrVector3f p = around(head.y - 0.2f, head.y + 0.6f, 0.8f, 1.5f);
        const XrVector3f ax = XrVector3f{(fx_r(s) - 0.5f) * 0.8f, -1.0f, (fx_r(s) - 0.5f) * 0.8f};
        const float l = v_len(ax);
        xrballs::world_fx(xrballs::kWxBolt, p, zero, up, 0.3f + 0.4f * fx_r(s), 0.1f + 0.1f * fx_r(s), 0xFFFFFF, glow, v_mul(ax, 1.0f / l));
      }
      for (int i = every(4, 18.0f * room); i > 0; --i)
        xrballs::world_fx(xrballs::kWxEmber, around(low + 0.2f, head.y + 0.4f, 0.6f, 1.5f), rv(1.2f), up, 0.003f, 0.3f + 0.3f * fx_r(s),
                          0xFFFFFF, glow, up);
      break;
    }
    case kRlSmoke: {
      const uint32_t c = f.theme == tGhost ? 0x6A48A8 : (f.theme == mTeleport ? 0x7A50C0 : (f.theme == mMysterio ? 0x3A9A60 : 0x16121C));
      for (int i = every(0, 6.0f * card); i > 0; --i)  // wisps rising off its face
        xrballs::world_fx(xrballs::kWxSmoke, face(0.8f), v_add(v_mul(up, 0.035f), rv(0.015f)), up, (0.02f + 0.015f * fx_r(s)) * scale,
                          1.6f + fx_r(s), c, 0, up);
      for (int i = every(3, 2.5f * room); i > 0; --i)  // rolling along the floor round you
        xrballs::world_fx(xrballs::kWxSmoke, around(low + 0.05f, low + 0.25f, 0.6f, 1.6f), XrVector3f{(fx_r(s) - 0.5f) * 0.12f, 0.02f, (fx_r(s) - 0.5f) * 0.12f},
                          up, 0.28f + 0.15f * fx_r(s), 3.5f + fx_r(s), c, 0, up);
      break;
    }
    case kRlWater: {
      for (int i = every(0, 3.0f * card); i > 0; --i)  // bubbles off its face
        xrballs::world_fx(xrballs::kWxBubble, face(0.7f), v_mul(up, 0.04f), up, 0.004f * scale / 1.0f + 0.002f, 1.2f, 0xFFFFFF, 0x9ADFFF, up);
      for (int i = every(3, 9.0f * room); i > 0; --i)  // bubbles rising round you
        xrballs::world_fx(xrballs::kWxBubble, around(low + 0.1f, head.y, 0.6f, 1.5f), v_add(v_mul(up, 0.12f), rv(0.05f)), up,
                          0.012f + 0.014f * fx_r(s), 3.0f + fx_r(s), 0xFFFFFF, 0x9ADFFF, up);
      for (int i = every(4, 10.0f * room); i > 0; --i)  // splashes jumping off the floor
        xrballs::world_fx(xrballs::kWxDrop, around(low + 0.01f, low + 0.02f, 0.7f, 1.5f), v_add(v_mul(up, 1.4f + 0.8f * fx_r(s)), rv(0.6f)), up,
                          0.008f, 0.6f, 0xD8F4FF, 0x2A7BFF, up);
      break;
    }
    case kRlIce: {
      for (int i = every(0, 5.0f * card); i > 0; --i)
        xrballs::world_fx(xrballs::kWxGlint, face(0.95f), zero, up, 0.008f * scale + 0.002f, 0.5f + 0.4f * fx_r(s), 0xFFFFFF, 0xBFE8FF, up);
      for (int i = every(1, 0.8f * card); i > 0; --i)
        xrballs::world_fx(xrballs::kWxMist, edge(nullptr), v_mul(up, -0.02f), up, 0.03f * scale, 2.0f, 0xEAF6FF, 0x8AB8E0, up);
      for (int i = every(3, 12.0f * room); i > 0; --i)
        xrballs::world_fx(xrballs::kWxGlint, around(low + 0.3f, head.y + 0.5f, 0.6f, 1.6f), rv(0.05f), up, 0.02f + 0.02f * fx_r(s),
                          0.6f + 0.5f * fx_r(s), 0xFFFFFF, 0xBFE8FF, up);
      for (int i = every(4, 2.0f * room); i > 0; --i)
        xrballs::world_fx(xrballs::kWxMist, around(low + 0.05f, low + 0.3f, 0.6f, 1.6f), rv(0.08f), up, 0.3f + 0.15f * fx_r(s), 3.5f,
                          0xEAF6FF, 0x8AB8E0, up);
      break;
    }
    default:
      break;
  }
}

// Every frame (the 1/1's special, game thread): `faceUp` its face to you at the front (its card's effect, the aura),
// `held` in your hand (its world round you), its card frame (q / pos / scale), the throne's figure's top; its sound
float fx_dim() {  // (a Dark / Shadow world dims your room a little)
  if (!g_fx.up) return 0.0f;
  return (g_fx.theme == tDark || g_fx.theme == mShadow || g_fx.theme == sGanon) ? 0.3f * g_fx.on : 0.0f;
}
void fx_frame(xrfx::Scene& scene, bool faceUp, bool held, const XrQuaternionf& q, const XrVector3f& pos, float scale,
              float auraTop, float dt, float t, bool cameo) {
  FxState& f = g_fx;
  if (!f.up) return;
  f.on += ((held && faceUp ? 1.0f : 0.0f) - f.on) * std::min(1.0f, dt * (held ? 1.2f : 0.8f));
  f.cardOn += ((faceUp && !cameo ? 1.0f : 0.0f) - f.cardOn) * std::min(1.0f, dt * 2.0f);
  const XrPosef& head = S.head;
  XrVector3f fwd = q_rot(head.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
  const float fl = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
  fwd = fl > 1e-3f ? XrVector3f{fwd.x / fl, 0.0f, fwd.z / fl} : XrVector3f{0.0f, 0.0f, -1.0f};
  const float floor = g_floorY;
  // round you
  for (int i = 0; i < f.nAround; ++i) {
    FxPt& p = f.around[i];
    p.age += dt;
    if (f.on < 0.02f) {
      fx_hide(p.base0);
      if (p.on) p.on = false;
      continue;
    }
    if (!p.on || p.age >= p.life) {
      if (p.age < 0.0f) {
        fx_hide(p.base0);
        continue;
      }
      fx_spawn_around(p, head.position, fwd, floor);
    }
    float k = std::sin(3.14159265f * clampf(p.age / p.life, 0.0f, 1.0f));
    float size = p.size;
    switch (p.mo) {
      case kFaSwirl:
      case kFaOrbit: {
        p.a += p.spin * dt;
        p.p = {head.position.x + p.r * std::cos(p.a), p.y + 0.08f * std::sin(t * 0.7f + p.r * 5.0f), head.position.z + p.r * std::sin(p.a)};
        // (never right ahead: fade there)
        const XrVector3f d = v_sub(p.p, head.position);
        const float ahead = (d.x * fwd.x + d.z * fwd.z) / std::max(0.01f, std::sqrt(d.x * d.x + d.z * d.z));
        k *= clampf((0.82f - ahead) / 0.25f, 0.0f, 1.0f);
        break;
      }
      case kFaJump:
        k = 1.0f;
        p.p = v_add(p.p, XrVector3f{(fx_r(f.rng) - 0.5f) * 0.02f, (fx_r(f.rng) - 0.5f) * 0.02f, (fx_r(f.rng) - 0.5f) * 0.02f});
        break;
      case kFaPulse:
        size = p.size * (1.0f + 9.0f * p.age / p.life);
        k = 1.0f - p.age / p.life;
        break;
      case kFaFall:
        p.v.x += std::sin(t * 1.3f + i) * 0.02f * dt;
        p.p = v_add(p.p, v_mul(p.v, dt));
        break;
      default:
        p.p = v_add(p.p, v_mul(p.v, dt));
        break;
    }
    p.rot += p.spin * dt;
    const uint32_t rgb = f.aEm[i] == 3 ? fx_rgb_mix(0xFF60A0, 0x60C0FF, 0.5f + 0.5f * std::sin(t * 2.0f + i)) : p.rgb;
    fx_put(p.base0, p.p, q_mul(facing(p.p), q_axis(0, 0, 1, p.rot)), size, size, rgb, k * f.on * (p.mo == kFaPulse ? 0.7f : 0.9f),
           p.shade);
  }
  // on the card (its frame: pos + q (size `scale`)); a drop let go runs in the room
  const XrVector3f gl = q_rot(q_conj(q), XrVector3f{0.0f, -1.0f, 0.0f});  // (down, in the card's frame)
  const float W = 0.5f * kCardWm, H = 0.5f * kCardHm;
  f.upC = v_mul(gl, -1.0f);  // r142: the room's up in it (held any way)
  float upX, upY;            // (across its face)
  fx_up_across(&upX, &upY);
  const XrVector3f toEye = v_sub(head.position, pos), cardN = q_rot(q, XrVector3f{0.0f, 0.0f, 1.0f});
  const float inFront = (toEye.x * cardN.x + toEye.y * cardN.y + toEye.z * cardN.z) / std::max(1e-3f, v_len(toEye));
  for (int i = 0; i < f.nCard; ++i) {
    FxPt& p = f.cardPt[i];
    p.age += dt;
    if (f.cardOn < 0.02f) {
      fx_hide(p.base0);
      continue;
    }
    if (!p.on || p.age >= p.life) {
      if (p.age < 0.0f) {
        fx_hide(p.base0);
        continue;
      }
      fx_spawn_card(p);
    }
    float k = std::sin(3.14159265f * clampf(p.age / p.life, 0.0f, 1.0f));
    float size = p.size;
    XrVector3f at{0.0f, 0.0f, 0.0f};
    XrQuaternionf qq = q_mul(q, q_axis(0, 0, 1, p.rot));
    if (p.world) {  // let go of the card: a drip falling, dust drifting down
      p.v.y -= (p.spr == kFsDrop ? 2.5f : 0.25f) * dt;
      p.p = v_add(p.p, v_mul(p.v, dt));
      k = 1.0f - p.age / p.life;
    } else {
      switch (p.mo) {
        case kFcDrops: {
          // beads, then (big enough) runs down the glass, the way the card tips (gravity in its plane; each drop its own
          // stickiness, a little wander), to its low edge and drips off
          if (p.size < p.grow) {
            p.size = std::min(p.grow, p.size + dt * 0.004f);
          } else {
            const float stick = 0.5f + std::fabs(std::sin(i * 12.9898f));
            p.v = v_add(v_mul(p.v, 1.0f - std::min(1.0f, dt * 2.0f)), v_mul(XrVector3f{gl.x, gl.y, 0.0f}, 0.16f * stick * dt));
            p.p = v_add(p.p, v_mul(p.v, dt));
            p.p.x += std::sin(t * 3.0f + i) * 0.0008f * dt;
          }
          k = 1.0f;
          size = p.size * 1.5f;
          if (std::fabs(p.p.x) > W || std::fabs(p.p.y) > H) {  // off the edge: a drip, falling in the room
            p.world = true;
            p.p = v_add(pos, q_rot(q, v_mul(p.p, scale)));
            p.v = v_mul(q_rot(q, p.v), scale);
            p.age = 0.0f;
            p.life = 0.7f;
          }
          break;
        }
        case kFcCorner:
          size = p.size * smooth(p.age / 2.5f) * (1.0f + 0.05f * std::sin(t * 1.4f + i));
          k = 0.9f;
          break;
        case kFcLand: {  // floats down onto its face, then rests (r142: down from above in the room - never up through it)
          const float u = smooth(p.age / 1.8f);
          k = 0.95f;
          at = v_add(p.p, v_mul(XrVector3f{f.upC.x, f.upC.y, std::max(0.0f, f.upC.z)}, (1.0f - u) * 0.12f));
          at.x += (1.0f - u) * 0.03f * std::sin(p.age * 3.0f);
          at.z += (1.0f - u) * 0.02f;
          p.v = at;  // (kept below)
          break;
        }
        case kFcHang:
          k = 0.85f + 0.15f * std::sin(t * 2.0f + i * 1.7f);
          // r142: under its lowest edge in the room, pointing down (an icicle hangs; held upside down it hangs off its top)
          if (std::fabs(upY) >= std::fabs(upX)) p.p = {p.v.x * (W - 0.008f), upY > 0.0f ? -H - 0.006f : H + 0.006f, p.p.z};
          else p.p = {upX > 0.0f ? -W - 0.006f : W + 0.006f, p.v.x * (H - 0.008f), p.p.z};
          p.rot = std::atan2(-upX, upY);
          break;
        case kFcInside: {  // r142: rising by the room's up (turned as the card turns), drifting a little across
          p.v = {-upY * p.r + upX * p.a, upX * p.r + upY * p.a, 0.0f};
          p.p = v_add(p.p, v_mul(p.v, dt));
          break;
        }
        case kFcRipple:
          size = 0.002f + 0.06f * p.age / p.life;
          k = 1.0f - p.age / p.life;
          break;
        case kFcGlow:
          k = 0.55f + 0.25f * std::sin(t * 1.6f);
          p.rot += 0.2f * dt;
          break;
        case kFcBurst:
          size = p.size * (0.4f + 1.2f * (p.age / p.life));
          break;
        case kFcFallOff:  // (off its bottom edge, falling in the room from here)
          p.world = true;
          p.p = v_add(pos, q_rot(q, v_mul(p.p, scale)));
          p.v = {0.0f, -0.02f, 0.0f};
          break;
        case kFcEdge: {
          // r142: a flame licks up in the room (off its face too when it lies flat) and stands up in it
          XrVector3f m{p.v.x, p.v.y, 0.0f};
          if (p.spr == kFsFlame) {
            m = v_add(m, v_mul(f.upC, 0.02f));
            p.rot = std::atan2(-upX, upY);
          }
          p.p = v_add(p.p, v_mul(m, dt));
          size = p.size * (1.0f - 0.4f * p.age / p.life);
          break;
        }
        default:
          p.p = v_add(p.p, v_mul(p.v, dt));
          break;
      }
      p.rot += p.spin * dt;
    }
    if (p.world) {
      at = p.p;
      qq = q_mul(facing(at), q_axis(0, 0, 1, p.rot));
    } else {
      at = v_add(pos, q_rot(q, v_mul(p.mo == kFcLand ? p.v : p.p, scale)));
      // (what's inside its glass seen only through its window: gone as the card turns side on)
      if (p.p.z < 0.0f) k *= clampf((inFront - 0.25f) / 0.25f, 0.0f, 1.0f);
    }
    size *= scale;
    const uint32_t rgb = f.cEm[i] == 3 ? fx_rgb_mix(0xFF60A0, 0x60FFC0, 0.5f + 0.5f * std::sin(t * 3.0f + i)) : p.rgb;
    const float wide = p.mo == kFcSweep || p.mo == kFcStreak ? 2.4f : 1.0f;
    fx_put(p.base0, at, qq, size * wide, size, rgb, k * f.cardOn * (p.mo == kFcSweep ? 0.6f : 1.0f), p.shade);
  }
  // the throne's aura: a slow purple glow behind its figure
  if (f.cardOn > 0.02f) {
    const XrVector3f top = v_add(pos, q_rot(q, XrVector3f{0.0f, (0.5f * kCardHm + auraTop * 0.5f) * scale, -0.01f * scale}));
    const float sz = (auraTop * 1.7f + 0.03f) * scale * (1.0f + 0.06f * std::sin(t * 0.9f));
    fx_put(f.auraBase, top, q_mul(facing(top), q_axis(0, 0, 1, t * 0.15f)), sz, sz, 0x7A3CFF, (0.45f + 0.15f * std::sin(t * 0.8f)) * f.cardOn);
  } else {
    fx_hide(f.auraBase);
  }
  // the tint round you
  {
    float* o = f.verts.data() + static_cast<size_t>(f.sphereBase) * 6;
    const uint32_t c = kFxTheme[f.theme].tint;
    const float k = 0.07f * f.on;
    for (int i = 0; i <= 6; ++i)
      for (int j = 0; j <= 10; ++j) {
        const float th = 3.14159265f * i / 6.0f, ph = 6.2831853f * j / 10.0f;
        float* v = o + (i * 11 + j) * 6;
        const XrVector3f d{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
        v[0] = k > 0.001f ? head.position.x + d.x * 1.35f : 0.0f;
        v[1] = k > 0.001f ? head.position.y + d.y * 1.35f : 0.0f;
        v[2] = k > 0.001f ? head.position.z + d.z * 1.35f : 0.0f;
        v[3] = ((c >> 16) & 255) / 255.0f * k;
        v[4] = ((c >> 8) & 255) / 255.0f * k;
        v[5] = (c & 255) / 255.0f * k;
      }
  }
  fx_real(dt, q, pos, scale, head.position, fwd, floor);  // r179: its real effects (fire / bolts / smoke / water / ice)
  // its sound, laid end over end while you hold it
  f.soundT -= dt;
  if (f.on > 0.3f && f.soundT <= 0.0f && !f.sound.empty()) {
    PortCardSound(f.sound.data(), static_cast<int>(f.sound.size()), 0.35f * f.on);
    f.soundT = static_cast<float>(f.sound.size()) / 32000.0f - 0.3f;
  }
  xrfx::Scene::ModelDraw& md = scene.models[kFxSlot];
  md.on = true;
  md.posNrm = f.verts.data();
  md.local = false;
  md.white = 0.0f;
  md.alpha = 1.0f;
  md.glow = 0.0f;
  md.under = -4.5f;  // (light, each sprite tinted: xr_fx_model.frag)
  md.glowRgb = 0xFFFFFF;
  md.flameFrame = -1;
  md.shine = 0.0f;
  md.frame = -1;
}

// ===================================================================================== r171: HOMETOWN's 3D diorama
// (Lance, his r168 HOMETOWN ODDISH "THE DEEP FOREST": "that whole experience was underwhelming af, and i want that to be an
// actual like diorama within the card, not a 2D background amidst the 3Difiication"): its landscape window now holds a
// real little world built here (no pictures of it: geometry) - a sky behind, a far ridge and a nearer one (hills, peaks,
// a skyline, dunes, a volcano ...), the ground rising away in a height field, its home's own things in 3D (trees with
// trunks and leaf clusters, rocks, water, houses, towers, a castle, pipes, crystals ...), grass, the figure standing on
// the ground with a soft shadow - and ALIVE (Lance's rule): leaves and grass swaying, water rippling, clouds and birds
// drifting across, fog rolling, light shafts breathing, its home's particles (fireflies, petals, snow, embers ...).
// Turned landscape the first time it pops up like a pop-up book, back to front (home_moves). Flat-shaded low poly,
// coloured from a palette picture; in its own landscape frame (home_to_card turns it onto the card). ~5-9k vertices.
constexpr int kGoldTwinkles = 14;  // r171: a GOLD's vault's twinkles (gold_fx_moves)
enum HomeBiome { kHbMeadow, kHbForest, kHbCastle, kHbHyrule, kHbTropic, kHbJungle, kHbDesert, kHbVolcano, kHbHaunt,
                 kHbCity, kHbNight, kHbSnow, kHbSky, kHbAlien, kHbPastel, kHbTown, kHbFlat };
enum HomeAnim { kHaStatic = 0, kHaSway, kHaWater, kHaParticle, kHaDrift, kHaBird, kHaBob, kHaFog, kHaShaft };
enum HomePart { kHpSky = 0, kHpSolid, kHpShadow, kHpShaft, kHpFog, kHpCount };
enum HomeFx { kHfLeaves, kHfPetals, kHfSnow, kHfEmbers, kHfFireflies, kHfSparkles, kHfBubbles, kHfSand };
struct HomeParticle {
  XrVector3f p;
  float vx, vy, size, phase;
  int kind;
};
struct HomeMesh {
  std::vector<float> v;         // 6 a vertex: where (its home frame), its normal (0.75 long: lit by itself)
  std::vector<float> uv;        // 2
  std::vector<float> anim;      // 6: kind, pivot y (its unfold's hinge), delay (s), a, b, c
  std::vector<uint32_t> idx[kHpCount];
  std::vector<uint32_t> pal;    // the palette's colours (16 x 16 swatches)
  std::vector<HomeParticle> fx;
  XrVector3f lo{-0.12f, -0.06f, -0.11f}, hi{0.12f, 0.06f, 0.0f};  // (its particles' box)
  bool ok = false;
};
struct HomeKit {
  uint32_t skyTop, skyBot, sun, ground0, ground1, far, mid, fog, grass, fxCol;
  bool night = false, shafts = false, sea = false, pond = false, lava = false, stars = false;
  int ridge = 0;  // 0 hills, 1 peaks, 2 skyline, 3 dunes, 4 volcano, 5 cloud bank
  int trees = 0, treeKind = 0;  // 0 round, 1 pine, 2 palm, 3 dead, 4 mixed round + pine
  int fx = kHfLeaves, fxN = 30, birds = 1, clouds = 2, grassN = 160, flowers = 0;
  float rise = 0.016f, bumps = 0.003f, fogA = 0.0f;
  int mushrooms = 0, pipes = 0, blocks = 0, houses = 0, towers = 0, castle = 0, crystals = 0, graves = 0, pyramids = 0,
      columns = 0, pylons = 0, lamps = 0, rocks = 4, islands = 0, buildings = 0;
  uint32_t leaf = 0x4FA048, leaf2 = 0x3C8A3C, trunk = 0x7A5230, rock = 0x8C8C94, roof = 0xC8382E, wall = 0xF2E6D2;
  bool snowy = false, neon = false, mono = false;
};
int home_biome(const std::string& n) {
  if (n == "evo_grass") return kHbForest;
  if (n == "suite_zelda") return kHbHyrule;
  if (n == "suite_peach" || n == "suite_fireemblem") return kHbCastle;
  if (n == "suite_yoshi" || n == "evo_water") return kHbTropic;
  if (n == "suite_dk") return kHbJungle;
  if (n == "suite_daisy") return kHbDesert;
  if (n == "evo_fire" || n == "suite_bowser") return kHbVolcano;
  if (n == "suite_luigi" || n == "evo_ghost") return kHbHaunt;
  if (n == "evo_city" || n == "suite_wario" || n == "suite_starfox") return kHbCity;
  if (n == "suite_fzero" || n == "suite_waluigi" || n == "evo_electric") return kHbNight;
  if (n == "suite_iceclimber" || n == "evo_rock") return kHbSnow;
  if (n == "evo_sky") return kHbSky;
  if (n == "suite_metroid" || n == "evo_psychic") return kHbAlien;
  if (n == "suite_kirby") return kHbPastel;
  if (n == "suite_earthbound") return kHbTown;
  if (n == "suite_gamewatch") return kHbFlat;
  return kHbMeadow;  // (suite_mario, KANTO ...)
}
HomeKit home_kit(int b, const std::string& n) {
  HomeKit k{0x5AA8F0, 0xCFE9FA, 0xFFF4C8, 0x6CC04A, 0x4E9A3E, 0x9CC8D8, 0x5E9E5A, 0xFFFFFF, 0x5CB848, 0xFFFFFF};
  switch (b) {
    case kHbForest:
      k.skyTop = 0x7EC0E6, k.skyBot = 0xE2F0DC, k.ground0 = 0x5E9A40, k.ground1 = 0x3E7A38, k.far = 0x8CB4A8, k.mid = 0x3E6E4A;
      k.trees = 22, k.treeKind = 4, k.pond = true, k.fx = kHfFireflies, k.fxN = 34, k.shafts = true, k.fog = 0xDDEED8, k.fogA = 0.5f;
      k.grassN = 240, k.flowers = 12, k.birds = 2, k.clouds = 1, k.rocks = 6, k.mushrooms = 0;
      break;
    case kHbHyrule:
      k.trees = 12, k.treeKind = 4, k.castle = 1, k.fx = kHfLeaves, k.shafts = true, k.birds = 3, k.flowers = 20, k.fog = 0xEAF4E8, k.fogA = 0.35f;
      break;
    case kHbCastle:
      k.skyTop = 0x6CB4F2, k.skyBot = 0xFCE4EE, k.trees = 8, k.castle = 1, k.fx = kHfPetals, k.fxCol = 0xFFB4D2, k.flowers = 40,
      k.clouds = 4, k.roof = n == "suite_fireemblem" ? 0x3A5AB0 : 0xE04A6A, k.wall = 0xFBF3EE;
      break;
    case kHbTropic:
      k.skyTop = 0x3CA6F0, k.skyBot = 0xD8F4FA, k.ground0 = 0xF0DCA0, k.ground1 = 0x7CC85A, k.sea = true, k.trees = 7, k.treeKind = 2,
      k.fx = n == "evo_water" ? kHfBubbles : kHfPetals, k.fxCol = n == "evo_water" ? 0xC8F0FF : 0xFFE070, k.birds = 3, k.clouds = 4,
      k.rise = 0.004f, k.grassN = 70, k.flowers = 10, k.rocks = 5, k.far = 0x7CB8A0;
      break;
    case kHbJungle:
      k.skyTop = 0x4CA8E0, k.skyBot = 0xE8F4D8, k.ground0 = 0x5A9A34, k.ground1 = 0x3A7A2C, k.trees = 20, k.treeKind = 2, k.pond = true,
      k.fx = kHfLeaves, k.shafts = true, k.fog = 0xE0F0D0, k.fogA = 0.4f, k.grassN = 260, k.leaf = 0x3E9A3A, k.leaf2 = 0x2E7A2E;
      break;
    case kHbDesert:
      k.skyTop = 0x58A6E8, k.skyBot = 0xFCE8C0, k.ground0 = 0xF0CC84, k.ground1 = 0xE0B068, k.far = 0xE8C890, k.mid = 0xD8A860,
      k.ridge = 3, k.pyramids = 2, k.trees = 4, k.treeKind = 2, k.fx = kHfSand, k.fxCol = 0xF6DCA0, k.grassN = 20, k.bumps = 0.005f,
      k.rocks = 3, k.rock = 0xC89A64;
      break;
    case kHbVolcano:
      k.skyTop = 0x3A1E28, k.skyBot = 0xD8603A, k.sun = 0xFF8A40, k.ground0 = 0x4A3A3A, k.ground1 = 0x2E2428, k.far = 0x5A3438,
      k.mid = 0x3A2A2E, k.ridge = 4, k.lava = true, k.trees = 6, k.treeKind = 3, k.fx = kHfEmbers, k.fxCol = 0xFFA040, k.fxN = 40,
      k.fog = 0x5A3030, k.fogA = 0.45f, k.grassN = 0, k.birds = 0, k.clouds = 0, k.rocks = 9, k.rock = 0x3A3036, k.trunk = 0x2A2226,
      k.castle = n == "suite_bowser" ? 2 : 0, k.wall = 0x5A5660, k.roof = 0x3A3440;
      break;
    case kHbHaunt:
      k.skyTop = 0x14102E, k.skyBot = 0x5A3A7A, k.sun = 0xF0F0FF, k.night = true, k.stars = true, k.ground0 = 0x3A4A3A,
      k.ground1 = 0x26302A, k.far = 0x2E2A4A, k.mid = 0x1E1C30, k.trees = 8, k.treeKind = 3, k.graves = 6, k.houses = 1,
      k.fx = kHfFireflies, k.fxCol = 0xB48CFF, k.fog = 0x8A7AB0, k.fogA = 0.55f, k.birds = 2, k.clouds = 0, k.grassN = 90,
      k.grass = 0x3E5A3E, k.trunk = 0x2A2430, k.wall = 0x4A4258, k.roof = 0x2A2438, k.rock = 0x5A5A66;
      break;
    case kHbCity:
      k.skyTop = 0x5C9EE0, k.skyBot = 0xF4D8C0, k.ground0 = 0x8A8E96, k.ground1 = 0x6E727C, k.far = 0x98A8C0, k.mid = 0x6A7894,
      k.ridge = 2, k.buildings = 9, k.lamps = 4, k.trees = 4, k.fx = kHfLeaves, k.fxN = 14, k.birds = 4, k.clouds = 3, k.rise = 0.002f,
      k.bumps = 0.0f, k.grassN = 0, k.rocks = 0;
      break;
    case kHbNight:
      k.skyTop = 0x0C0A24, k.skyBot = 0x46207A, k.sun = 0xFFD0FF, k.night = true, k.stars = true, k.neon = true, k.ground0 = 0x2C2A3A,
      k.ground1 = 0x1E1C2A, k.far = 0x2A2450, k.mid = 0x1A1836, k.ridge = 2, k.buildings = 10, k.lamps = 5,
      k.pylons = n == "evo_electric" ? 3 : 0, k.fx = n == "evo_electric" ? kHfEmbers : kHfSparkles, k.fxCol = n == "evo_electric" ? 0xFFF060 : 0xFF70E0,
      k.birds = 0, k.clouds = 0, k.rise = 0.002f, k.bumps = 0.0f, k.grassN = 0, k.rocks = 0;
      break;
    case kHbSnow:
      k.skyTop = 0x7AB4E8, k.skyBot = 0xEEF6FC, k.ground0 = 0xF4F8FC, k.ground1 = 0xDCE6F0, k.far = 0xB8CCE0, k.mid = 0x8CA0B8,
      k.ridge = 1, k.trees = 14, k.treeKind = 1, k.snowy = n == "suite_iceclimber", k.fx = kHfSnow, k.fxN = 50, k.grassN = 0,
      k.rocks = 7, k.birds = 2, k.fog = 0xF0F6FF, k.fogA = 0.4f, k.leaf = 0x2E6A4E, k.leaf2 = 0x24583E;
      if (n == "evo_rock") k.ground0 = 0x9A9488, k.ground1 = 0x7A7468, k.fx = kHfLeaves, k.fxN = 12, k.grassN = 60, k.grass = 0x7A9A50;
      break;
    case kHbSky:
      k.skyTop = 0x3C8EEA, k.skyBot = 0xEAF6FF, k.ground0 = 0xFFFFFF, k.ground1 = 0xE6F0FA, k.far = 0xF2F8FF, k.mid = 0xE0ECF8,
      k.ridge = 5, k.islands = 3, k.trees = 0, k.fx = kHfSparkles, k.birds = 5, k.clouds = 5, k.grassN = 0, k.rocks = 0,
      k.rise = 0.002f, k.bumps = 0.004f;
      break;
    case kHbAlien:
      k.skyTop = 0x1A0E3A, k.skyBot = n == "evo_psychic" ? 0xC070D0 : 0x2A8A8A, k.sun = 0xFFE0FF, k.night = true, k.stars = true,
      k.ground0 = n == "evo_psychic" ? 0x8A6AA8 : 0x4A3A5A, k.ground1 = 0x342A48, k.far = 0x4A3A6A, k.mid = 0x2E2448, k.ridge = 1,
      k.crystals = 6, k.columns = n == "evo_psychic" ? 4 : 0, k.fx = n == "evo_psychic" ? kHfSparkles : kHfBubbles,
      k.fxCol = n == "evo_psychic" ? 0xFFD8FF : 0x8AF0D0, k.fog = 0x6AC0B0, k.fogA = 0.35f, k.grassN = 0, k.rocks = 8,
      k.rock = 0x6A4A7A, k.birds = 0, k.clouds = 0;
      break;
    case kHbPastel:
      k.skyTop = 0x8AC8FF, k.skyBot = 0xFFE0F0, k.ground0 = 0x9AE08A, k.ground1 = 0x7ACC8A, k.far = 0xC8E0F0, k.mid = 0x8AD0A0,
      k.trees = 7, k.leaf = 0xFFA8C8, k.leaf2 = 0xF08AB4, k.fx = kHfSparkles, k.fxCol = 0xFFF080, k.clouds = 4, k.flowers = 30;
      break;
    case kHbTown:
      k.houses = 5, k.trees = 8, k.fx = kHfLeaves, k.birds = 3, k.clouds = 3, k.flowers = 12, k.lamps = 2;
      break;
    case kHbFlat:
      k.skyTop = 0xB8C4A8, k.skyBot = 0xC8D2B8, k.sun = 0xC8D2B8, k.ground0 = 0xA8B498, k.ground1 = 0x98A488, k.far = 0x8A967A,
      k.mid = 0x6A7660, k.mono = true, k.trees = 5, k.leaf = 0x2A2E26, k.leaf2 = 0x2A2E26, k.trunk = 0x2A2E26, k.houses = 2,
      k.wall = 0x3A3E36, k.roof = 0x2A2E26, k.fx = kHfSparkles, k.fxCol = 0x2A2E26, k.grassN = 0, k.rocks = 2, k.rock = 0x3A3E36,
      k.birds = 2, k.clouds = 2, k.bumps = 0.0f;
      break;
    default:  // kHbMeadow: the Mushroom Kingdom's rolling green hills
      k.trees = 6, k.mushrooms = 3, k.pipes = 2, k.blocks = 3, k.fx = kHfPetals, k.fxCol = 0xFFFFFF, k.clouds = 4, k.flowers = 26;
      break;
  }
  return k;
}
// the builder: flat-shaded triangles in its frame, coloured from the palette
struct HomeB {
  HomeMesh& M;
  float delay = 0.0f, pivot = 0.0f;
  float an[4] = {kHaStatic, 0, 0, 0};
  int part = kHpSolid;
  bool glow = false;
  uint32_t seed = 1;
  explicit HomeB(HomeMesh& m) : M(m) {}
  float rnd() { return pp_h01(seed++ * 2654435761u); }
  float rr(float a, float b) { return a + (b - a) * rnd(); }
  int swatch(uint32_t c) {
    for (size_t i = 0; i < M.pal.size(); ++i)
      if (M.pal[i] == c) return static_cast<int>(i);
    if (M.pal.size() >= 256) return 0;
    M.pal.push_back(c);
    return static_cast<int>(M.pal.size()) - 1;
  }
  void vert(const XrVector3f& p, const XrVector3f& n, float u, float v) {
    M.v.insert(M.v.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
    M.uv.push_back(u);
    M.uv.push_back(v);
    M.anim.insert(M.anim.end(), {an[0], pivot, delay, an[1], an[2], an[3]});
  }
  // r173 (Lance on the BOOM's "too polygonal"; the r171 dioramas were faceted): smooth shading - a triangle's corners
  // may carry their own normals (sn, while `smoothN`: a ball's round, a trunk's round, the ground's slope)
  bool smoothN = false;
  XrVector3f sn[3];
  void tri(XrVector3f a, XrVector3f b, XrVector3f c, uint32_t col) {
    const int s = swatch(col);
    const float u = ((s % 16) + 0.5f) / 16.0f, v = ((s / 16) + 0.5f) / 16.0f;
    const float nl = glow ? 0.75f : 1.0f;
    XrVector3f n = v_cross(v_sub(b, a), v_sub(c, a));
    const float l = v_len(n);
    n = l > 1e-9f ? v_mul(n, nl / l) : XrVector3f{0.0f, 0.0f, nl};
    XrVector3f nn[3] = {n, n, n};
    if (smoothN)
      for (int k = 0; k < 3; ++k) {
        const float sl = v_len(sn[k]);
        if (sl > 1e-9f) nn[k] = v_mul(sn[k], nl / sl);
      }
    const uint32_t base = static_cast<uint32_t>(M.v.size() / 6);
    vert(a, nn[0], u, v);
    vert(b, nn[1], u, v);
    vert(c, nn[2], u, v);
    M.idx[part].insert(M.idx[part].end(), {base, base + 1, base + 2});
  }
  void triS(XrVector3f a, XrVector3f b, XrVector3f c, XrVector3f na, XrVector3f nb, XrVector3f nc, uint32_t col) {
    smoothN = true;
    sn[0] = na, sn[1] = nb, sn[2] = nc;
    tri(a, b, c, col);
    smoothN = false;
  }
  void quad(XrVector3f a, XrVector3f b, XrVector3f c, XrVector3f d, uint32_t col) {
    tri(a, b, c, col);
    tri(a, c, d, col);
  }
  // a picture quad (its own uvs: the sky, the soft parts), facing +z
  void pic(XrVector3f a, XrVector3f b, XrVector3f c, XrVector3f d, int p, float nl) {
    const uint32_t base = static_cast<uint32_t>(M.v.size() / 6);
    const XrVector3f n{0.0f, 0.0f, nl};
    vert(a, n, 0, 1);
    vert(b, n, 1, 1);
    vert(c, n, 1, 0);
    vert(d, n, 0, 0);
    M.idx[p].insert(M.idx[p].end(), {base, base + 1, base + 2, base, base + 2, base + 3});
  }
  void box(XrVector3f c, XrVector3f h, uint32_t col, uint32_t top = 0) {
    const XrVector3f p[8] = {{c.x - h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y - h.y, c.z + h.z}, {c.x + h.x, c.y + h.y, c.z + h.z},
                             {c.x - h.x, c.y + h.y, c.z + h.z}, {c.x - h.x, c.y - h.y, c.z - h.z}, {c.x + h.x, c.y - h.y, c.z - h.z},
                             {c.x + h.x, c.y + h.y, c.z - h.z}, {c.x - h.x, c.y + h.y, c.z - h.z}};
    quad(p[0], p[1], p[2], p[3], col);
    quad(p[5], p[4], p[7], p[6], dark(col, 0.2f));
    quad(p[4], p[0], p[3], p[7], dark(col, 0.1f));
    quad(p[1], p[5], p[6], p[2], dark(col, 0.1f));
    quad(p[3], p[2], p[6], p[7], top != 0 ? top : light(col, 0.1f));
  }
  // a cylinder / cone / frustum up from `b`, r0 at the bottom, r1 at the top, `n` sides (`cap`: its top closed)
  void cyl(XrVector3f b, float r0, float r1, float h, int n, uint32_t col, bool cap = true, float lean = 0.0f) {
    for (int i = 0; i < n; ++i) {
      const float a0 = 6.2831853f * i / n, a1 = 6.2831853f * (i + 1) / n;
      const XrVector3f p0{b.x + r0 * std::cos(a0), b.y, b.z + r0 * std::sin(a0)}, p1{b.x + r0 * std::cos(a1), b.y, b.z + r0 * std::sin(a1)};
      const XrVector3f q0{b.x + lean + r1 * std::cos(a0), b.y + h, b.z + r1 * std::sin(a0)},
          q1{b.x + lean + r1 * std::cos(a1), b.y + h, b.z + r1 * std::sin(a1)};
      // (r173: round - each side's corners lit by their own way out, one colour all round)
      const float slope = (r0 - r1) / std::max(1e-5f, h);
      const XrVector3f n0{std::cos(a0), slope, std::sin(a0)}, n1{std::cos(a1), slope, std::sin(a1)};
      if (r1 > 1e-5f) {
        triS(p0, p1, q1, n0, n1, n1, col);
        triS(p0, q1, q0, n0, n1, n0, col);
      } else {
        const float am = 0.5f * (a0 + a1);
        triS(p0, p1, q0, n0, n1, XrVector3f{std::cos(am), slope, std::sin(am)}, col);
      }
      if (cap && r1 > 1e-5f) tri(XrVector3f{b.x + lean, b.y + h, b.z}, q0, q1, light(col, 0.08f));
    }
  }
  // a lumpy ball (an icosahedron, each corner pushed in or out a little): a leaf cluster, a rock, a cloud puff
  void ball(XrVector3f c, XrVector3f r, uint32_t col, float lump = 0.15f, uint32_t col2 = 0) {
    static const float t = 1.618034f;
    static const float P[12][3] = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t},
                                   {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    static const int F[20][3] = {{0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11}, {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6},
                                 {7, 1, 8}, {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9}, {4, 9, 5}, {2, 4, 11}, {6, 2, 10},
                                 {8, 6, 7}, {9, 8, 1}};
    // r173: rounder - half the lumps, smooth normals (the ellipsoid's), and a big one (a canopy, a bush, a cloud)
    // split once (80 faces, its new corners out on its surface); a second colour as a soft patch, not facets
    lump *= 0.5f;
    XrVector3f q[12], u[12];
    for (int i = 0; i < 12; ++i) {
      const float k = (1.0f + lump * (rnd() * 2.0f - 1.0f)) / 1.902113f;
      u[i] = v_mul(XrVector3f{P[i][0], P[i][1], P[i][2]}, 1.0f / 1.902113f);
      q[i] = {c.x + P[i][0] * k * r.x, c.y + P[i][1] * k * r.y, c.z + P[i][2] * k * r.z};
    }
    auto nrm = [&](const XrVector3f& un) { return XrVector3f{un.x / std::max(1e-5f, r.x), un.y / std::max(1e-5f, r.y), un.z / std::max(1e-5f, r.z)}; };
    auto on = [&](const XrVector3f& un) {  // (a direction from its middle: out to its surface)
      const XrVector3f d = v_mul(un, 1.0f / std::max(1e-6f, v_len(un)));
      return XrVector3f{c.x + d.x * r.x, c.y + d.y * r.y, c.z + d.z * r.z};
    };
    const bool split = std::max({r.x, r.y, r.z}) >= 0.0075f;
    for (int f = 0; f < 20; ++f) {
      const int A = F[f][0], Bi = F[f][1], C = F[f][2];
      const uint32_t fc = col2 != 0 && u[A].y > 0.3f ? col2 : col;
      if (!split) {
        triS(q[A], q[Bi], q[C], nrm(u[A]), nrm(u[Bi]), nrm(u[C]), fc);
        continue;
      }
      const XrVector3f uab = v_add(u[A], u[Bi]), ubc = v_add(u[Bi], u[C]), uca = v_add(u[C], u[A]);
      const XrVector3f ab = on(uab), bc = on(ubc), ca = on(uca);
      triS(q[A], ab, ca, nrm(u[A]), nrm(uab), nrm(uca), fc);
      triS(ab, q[Bi], bc, nrm(uab), nrm(u[Bi]), nrm(ubc), fc);
      triS(ca, bc, q[C], nrm(uca), nrm(ubc), nrm(u[C]), fc);
      triS(ab, bc, ca, nrm(uab), nrm(ubc), nrm(uca), fc);
    }
  }
  void anim(float kind, float a = 0, float b = 0, float c = 0) {
    an[0] = kind;
    an[1] = a;
    an[2] = b;
    an[3] = c;
  }
};
// The diorama for `name` (home_backdrop's), its figure where `fit` stands it. Thread.
void home_scene(HomeMesh& M, const std::string& name, uint32_t seed, const Fit& fit, bool hasFig) {
  const int biome = home_biome(name);
  const HomeKit K = home_kit(biome, name);
  HomeB B(M);
  B.seed = seed * 7919u + 13u;
  const float X = 0.0825f, ZB = -0.108f, Y0 = -kHomeWinHalfY + 0.0015f;
  const float fx = hasFig ? fit.feet.x : 0.0f, fz = hasFig ? fit.feet.z : -0.02f;
  // the ground's height: flat round the figure, rising away (a hill behind), bumps; a sea / a pond dipping under water
  auto ground = [&](float x, float z) {
    const float back = smooth(clampf((-z - 0.035f) / 0.065f, 0.0f, 1.0f));
    const float near = clampf((std::fabs(x - fx) - 0.022f) / 0.02f, 0.0f, 1.0f) + clampf((-z + fz - 0.02f) / 0.02f, 0.0f, 1.0f);
    // (r173: softer - rolling, not lumpy: the noise wider and gentler)
    float y = Y0 + K.rise * back + 0.8f * K.bumps * clampf(near, 0.0f, 1.0f) * (pp_noise(x * 24.0f + 3.0f, z * 24.0f, seed) - 0.4f);
    if (biome == kHbDesert) y += 0.006f * back * std::sin(x * 45.0f + z * 20.0f);
    if (K.sea) y -= 0.016f * smooth(clampf((-z - 0.04f) / 0.03f, 0.0f, 1.0f));
    if (K.pond) {
      const float d = std::hypot((x - 0.045f) / 0.028f, (z + 0.065f) / 0.016f);
      y -= 0.005f * smooth(clampf(1.2f - d, 0.0f, 1.0f));
    }
    return y;
  };
  auto clear = [&](float x, float z, float r) {  // (away from the figure)
    return std::fabs(x - fx) > 0.026f + r || z < fz - 0.03f - r;
  };
  auto depthDelay = [](float z) { return 0.05f + 0.5f * (1.0f - clampf(-z / 0.105f, 0.0f, 1.0f)); };
  // --- the sky: behind it all and at its sides (lit by itself: its picture); never folds
  B.anim(kHaStatic);
  B.delay = -10.0f;
  B.pivot = Y0;
  B.pic({-X, -0.06f, ZB}, {X, -0.06f, ZB}, {X, 0.15f, ZB}, {-X, 0.15f, ZB}, kHpSky, 0.75f);
  B.pic({-X, -0.06f, -0.002f}, {-X, -0.06f, ZB}, {-X, 0.15f, ZB}, {-X, 0.15f, -0.002f}, kHpSky, 0.75f);
  B.pic({X, -0.06f, ZB}, {X, -0.06f, -0.002f}, {X, 0.15f, -0.002f}, {X, 0.15f, ZB}, kHpSky, 0.75f);
  // --- the far ridge and the nearer one (hills / peaks / skyline / dunes / volcano / cloud bank)
  for (int layer = 0; layer < 2; ++layer) {
    const float z = layer == 0 ? -0.102f : -0.086f;
    const uint32_t col = layer == 0 ? K.far : K.mid;
    B.delay = depthDelay(z);
    const float base = ground(0.0f, z) - 0.004f;
    B.pivot = base;
    if (K.ridge == 2) {  // a skyline: towers of all heights, their windows lit
      for (float x = -X; x < X;) {
        const float w = B.rr(0.008f, 0.016f), h = B.rr(0.02f, layer == 0 ? 0.07f : 0.045f);
        const uint32_t c = mix_rgb(col, layer == 0 ? 0xFFFFFF : 0x000000, B.rr(0.0f, 0.15f));
        B.box({x + 0.5f * w, base + 0.5f * h, z}, {0.5f * w, 0.5f * h, 0.004f}, c);
        B.glow = true;
        for (float wy = base + 0.006f; wy < base + h - 0.004f; wy += 0.005f)
          for (float wx = x + 0.0025f; wx < x + w - 0.002f; wx += 0.0035f)
            if (B.rnd() > (K.night ? 0.45f : 0.75f))
              B.quad({wx, wy, z + 0.0042f}, {wx + 0.0015f, wy, z + 0.0042f}, {wx + 0.0015f, wy + 0.002f, z + 0.0042f}, {wx, wy + 0.002f, z + 0.0042f},
                     K.neon ? (B.rnd() > 0.5f ? 0xFF5AD8u : 0x5AE8FFu) : (K.night ? 0xFFE08Au : 0xCFE6F5u));
        B.glow = false;
        x += w + B.rr(0.0f, 0.003f);
      }
      continue;
    }
    const int N = 32;
    for (int i = 0; i < N; ++i) {
      const float x0 = -X + 2 * X * i / N, x1 = -X + 2 * X * (i + 1) / N;
      auto hgt = [&](float x) {
        const float u = x / X;
        float h = 0.0f;
        if (K.ridge == 0 || K.ridge == 5) h = 0.012f + 0.01f * std::sin(u * 3.1f + layer * 1.7f) + 0.006f * std::sin(u * 7.3f + layer);
        else if (K.ridge == 1) h = 0.02f + 0.022f * std::pow(std::fabs(std::sin(u * 4.2f + layer * 1.1f)), 1.6f) + 0.004f * std::sin(u * 17.0f);
        else if (K.ridge == 3) h = 0.008f + 0.008f * std::sin(u * 2.6f + layer * 2.0f) + 0.003f * std::sin(u * 6.0f);
        else if (K.ridge == 4) h = layer == 0 ? 0.01f + 0.045f * std::max(0.0f, 1.0f - std::fabs(u - 0.25f) * 2.2f) : 0.012f + 0.006f * std::sin(u * 5.0f);
        return std::max(0.002f, h + (layer == 1 ? -0.004f : 0.0f));
      };
      const float h0 = hgt(x0), h1 = hgt(x1);
      B.quad({x0, base, z}, {x1, base, z}, {x1, base + h1, z - 0.003f}, {x0, base + h0, z - 0.003f}, col);
      if (K.ridge == 1 && layer == 0) {  // (snow on the peaks)
        const float s0 = std::max(0.0f, h0 - 0.026f), s1 = std::max(0.0f, h1 - 0.026f);
        if (s0 + s1 > 0.0f)
          B.quad({x0, base + h0 - s0 * 0.9f, z - 0.0026f}, {x1, base + h1 - s1 * 0.9f, z - 0.0026f}, {x1, base + h1, z - 0.0029f},
                 {x0, base + h0, z - 0.0029f}, 0xF4F8FF);
      }
      if (K.ridge == 5)  // (a cloud bank: round tops)
        B.ball({0.5f * (x0 + x1), base + 0.5f * (h0 + h1), z}, {0.006f, 0.005f, 0.003f}, col, 0.1f);
    }
    if (K.ridge == 4 && layer == 0) {  // the volcano's glowing crater and lava running down it
      B.glow = true;
      const float cx = 0.25f * X, top = base + 0.055f;
      B.ball({cx, top - 0.002f, z + 0.001f}, {0.006f, 0.0025f, 0.002f}, 0xFF8A2A, 0.1f);
      for (int r = 0; r < 3; ++r) {
        const float ox = (r - 1) * 0.004f;
        B.quad({cx + ox - 0.0008f, top - 0.003f, z + 0.0012f}, {cx + ox + 0.0008f, top - 0.003f, z + 0.0012f},
               {cx + ox * 3.5f + 0.0012f, base + 0.006f, z + 0.0012f}, {cx + ox * 3.5f - 0.0012f, base + 0.006f, z + 0.0012f}, 0xFF6A1E);
      }
      B.glow = false;
    }
  }
  // --- the ground: a height field running away, its colour from near to far, a little noise in it
  {
    const int NX = 22, NZ = 12;
    B.delay = 0.02f;
    B.pivot = Y0;
    for (int j = 0; j < NZ; ++j)
      for (int i = 0; i < NX; ++i) {
        const float x0 = -X + 2 * X * i / NX, x1 = -X + 2 * X * (i + 1) / NX;
        const float z0 = -0.001f + (ZB + 0.006f) * j / NZ, z1 = -0.001f + (ZB + 0.006f) * (j + 1) / NZ;
        const float k = static_cast<float>(j) / NZ;
        uint32_t c = mix_rgb(K.ground0, K.ground1, k);
        c = mix_rgb(c, 0x000000, 0.08f * pp_h01(static_cast<uint32_t>(i * 31 + j * 7) + seed));
        if (K.sea && k > 0.38f) c = mix_rgb(K.ground0, 0xC8B080, 0.4f);
        if (biome == kHbCity || biome == kHbNight) {  // (a road across, its line)
          if (k > 0.15f && k < 0.35f) c = biome == kHbNight ? 0x23212E : 0x55585F;
        }
        const XrVector3f a{x0, ground(x0, z0), z0}, b{x1, ground(x1, z0), z0}, cc{x1, ground(x1, z1), z1}, d{x0, ground(x0, z1), z1};
        // r173: a soft slope - each corner's normal from the ground's own rise there (no facets)
        auto gn = [&](float x, float z) {
          const float e = 0.002f;
          return XrVector3f{-(ground(x + e, z) - ground(x - e, z)) / (2 * e), 1.0f, -(ground(x, z + e) - ground(x, z - e)) / (2 * e)};
        };
        B.triS(a, b, cc, gn(x0, z0), gn(x1, z0), gn(x1, z1), c);
        B.triS(a, cc, d, gn(x0, z0), gn(x1, z1), gn(x0, z1), c);
      }
  }
  // --- water: the sea far off, a pond, a lava river - rippling (and the lava lit by itself)
  if (K.sea || K.pond || K.lava) {
    B.anim(kHaWater);
    B.delay = 0.1f;
    const int NX = K.sea ? 22 : 10, NZ = K.sea ? 6 : 5;
    for (int j = 0; j < NZ; ++j)
      for (int i = 0; i < NX; ++i) {
        float x0, x1, z0, z1, y;
        if (K.sea) {
          x0 = -X + 2 * X * i / NX, x1 = -X + 2 * X * (i + 1) / NX;
          z0 = -0.045f + (ZB + 0.045f) * j / NZ, z1 = -0.045f + (ZB + 0.045f) * (j + 1) / NZ;
          y = Y0 - 0.004f;
        } else if (K.pond) {
          x0 = 0.045f - 0.03f + 0.06f * i / NX, x1 = 0.045f - 0.03f + 0.06f * (i + 1) / NX;
          z0 = -0.065f + 0.018f - 0.036f * j / NZ, z1 = -0.065f + 0.018f - 0.036f * (j + 1) / NZ;
          y = ground(0.045f, -0.065f) + 0.0035f;
          if (std::hypot((0.5f * (x0 + x1) - 0.045f) / 0.028f, (0.5f * (z0 + z1) + 0.065f) / 0.016f) > 1.05f) continue;
        } else {  // lava: a river winding across the middle
          x0 = -X + 2 * X * i / NX, x1 = -X + 2 * X * (i + 1) / NX;
          const float zc = -0.07f + 0.01f * std::sin(x0 * 40.0f);
          z0 = zc + 0.006f - 0.012f * j / NZ, z1 = zc + 0.006f - 0.012f * (j + 1) / NZ;
          y = ground(0.5f * (x0 + x1), zc) + 0.0012f;
          B.glow = true;
        }
        const uint32_t c = K.lava ? (j % 2 ? 0xFF7A20u : 0xFFB040u) : (K.sea ? mix_rgb(0x2A8AD0, 0x1E5A9A, static_cast<float>(j) / NZ) : 0x3A8AB8u);
        B.quad({x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, c);
        B.glow = false;
      }
    B.anim(kHaStatic);
  }
  // --- its things: placed round the figure (never on it), back ones first
  auto spot = [&](float r, float zMin, float zMax, XrVector3f* p) {
    for (int tries = 0; tries < 40; ++tries) {
      const float x = B.rr(-X + r, X - r), z = B.rr(zMax, zMin);
      if (!clear(x, z, r)) continue;
      if (K.sea && z < -0.045f) continue;
      if (K.pond && std::hypot((x - 0.045f) / 0.032f, (z + 0.065f) / 0.02f) < 1.0f) continue;
      if (K.lava && std::fabs(z - (-0.07f + 0.01f * std::sin(x * 40.0f))) < 0.01f) continue;
      *p = {x, ground(x, z), z};
      return true;
    }
    return false;
  };
  auto tree = [&](XrVector3f p, int kind, float s) {
    B.delay = depthDelay(p.z);
    B.pivot = p.y;
    const float th = (kind == 2 ? 0.03f : 0.016f) * s;
    B.anim(kHaStatic);
    if (kind == 0) {  // round: a trunk, three leaf clusters swaying
      B.cyl(p, 0.0016f * s, 0.0011f * s, th, 6, K.trunk);
      B.anim(kHaSway, 0.0009f * s, th + 0.02f * s, B.rr(0.0f, 6.28f));
      for (int k = 0; k < 3; ++k)
        B.ball({p.x + B.rr(-0.004f, 0.004f) * s, p.y + th + B.rr(0.0f, 0.008f) * s, p.z + B.rr(-0.003f, 0.003f) * s},
               v_mul(XrVector3f{1.0f, 0.85f, 1.0f}, B.rr(0.006f, 0.009f) * s), k % 2 ? K.leaf : K.leaf2, 0.2f, light(K.leaf, 0.15f));
    } else if (kind == 1) {  // pine: three cones stacked (snow on them in the snow)
      B.cyl(p, 0.0013f * s, 0.001f * s, 0.006f * s, 5, K.trunk);
      B.anim(kHaSway, 0.0006f * s, 0.03f * s, B.rr(0.0f, 6.28f));
      for (int k = 0; k < 3; ++k) {
        const float y = p.y + (0.005f + k * 0.0065f) * s, r = (0.009f - k * 0.0024f) * s;
        B.cyl({p.x, y, p.z}, r, 0.0f, 0.011f * s, 7, k % 2 ? K.leaf : K.leaf2);
        if (K.snowy) B.cyl({p.x, y + 0.0062f * s, p.z}, r * 0.45f, 0.0f, 0.0048f * s, 7, 0xF4F8FF);
      }
    } else if (kind == 2) {  // palm: a curving trunk, its fronds drooping and swaying
      XrVector3f q = p;
      const float lean = B.rr(-1.0f, 1.0f);
      for (int k = 0; k < 4; ++k) {
        B.cyl(q, 0.0013f * s, 0.0011f * s, 0.25f * th, 5, k % 2 ? K.trunk : dark(K.trunk, 0.15f), false, lean * 0.0012f * k * s);
        q = {q.x + lean * 0.0012f * k * s, q.y + 0.25f * th, q.z};
      }
      B.anim(kHaSway, 0.0012f * s, th + 0.01f, B.rr(0.0f, 6.28f));
      for (int k = 0; k < 6; ++k) {
        const float a = 6.2831853f * k / 6.0f + B.rr(-0.2f, 0.2f), l = 0.014f * s;
        const XrVector3f tip{q.x + l * std::cos(a), q.y - 0.006f * s, q.z + 0.6f * l * std::sin(a)};
        const XrVector3f mid{q.x + 0.55f * l * std::cos(a), q.y + 0.002f * s, q.z + 0.33f * l * std::sin(a)};
        const XrVector3f side{-std::sin(a) * 0.0028f * s, 0.0f, std::cos(a) * 0.0017f * s};
        B.tri(q, v_add(mid, side), mid, K.leaf);
        B.tri(q, mid, v_sub(mid, side), K.leaf2);
        B.tri(v_add(mid, side), tip, mid, K.leaf2);
        B.tri(mid, tip, v_sub(mid, side), K.leaf);
      }
    } else {  // dead: a bare trunk and its crooked branches
      B.cyl(p, 0.0016f * s, 0.0009f * s, th * 1.3f, 5, K.trunk);
      B.anim(kHaSway, 0.0005f * s, th * 1.6f, B.rr(0.0f, 6.28f));
      for (int k = 0; k < 3; ++k) {
        const float y = p.y + th * (0.6f + 0.25f * k), a = B.rr(0.0f, 6.28f);
        B.cyl({p.x, y, p.z}, 0.0007f * s, 0.0003f * s, 0.006f * s, 4, K.trunk, false, (k % 2 ? 1.0f : -1.0f) * 0.004f * s * std::cos(a));
      }
    }
    B.anim(kHaStatic);
  };
  XrVector3f p;
  // the far-back and middle things first
  for (int k = 0; k < K.castle; ++k) {  // a castle on the hill: towers, roofs, a wall, flags
    const float cx = k == 0 ? -0.035f : 0.045f, cz = -0.092f, by = ground(cx, cz);
    B.delay = depthDelay(cz);
    B.pivot = by;
    B.box({cx, by + 0.01f, cz}, {0.02f, 0.01f, 0.005f}, K.wall);
    for (int t2 = 0; t2 < 3; ++t2) {
      const float tx = cx + (t2 - 1) * 0.02f, h = t2 == 1 ? 0.034f : 0.024f;
      B.cyl({tx, by, cz}, 0.0048f, 0.0048f, h, 8, K.wall);
      B.cyl({tx, by + h, cz}, 0.0062f, 0.0f, 0.012f, 8, K.roof);
      B.anim(kHaSway, 0.0008f, 0.004f, B.rr(0, 6));
      B.tri({tx, by + h + 0.012f, cz}, {tx + 0.006f, by + h + 0.0145f, cz}, {tx, by + h + 0.017f, cz}, K.roof);
      B.anim(kHaStatic);
    }
    B.glow = true;
    for (int w = 0; w < 4; ++w)
      B.quad({cx - 0.012f + w * 0.007f, by + 0.008f, cz + 0.0052f}, {cx - 0.010f + w * 0.007f, by + 0.008f, cz + 0.0052f},
             {cx - 0.010f + w * 0.007f, by + 0.012f, cz + 0.0052f}, {cx - 0.012f + w * 0.007f, by + 0.012f, cz + 0.0052f},
             K.night || biome == kHbVolcano ? 0xFFC060u : 0x30406Au);
    B.glow = false;
  }
  for (int k = 0; k < K.pyramids; ++k) {
    const float cx = k == 0 ? -0.04f : 0.03f, cz = k == 0 ? -0.09f : -0.096f, by = ground(cx, cz) - 0.002f, s = k == 0 ? 0.022f : 0.016f;
    B.delay = depthDelay(cz);
    B.pivot = by;
    const XrVector3f top{cx, by + s * 1.1f, cz};
    const XrVector3f c4[4] = {{cx - s, by, cz + s * 0.6f}, {cx + s, by, cz + s * 0.6f}, {cx + s, by, cz - s * 0.6f}, {cx - s, by, cz - s * 0.6f}};
    for (int f = 0; f < 4; ++f) B.tri(c4[f], c4[(f + 1) % 4], top, f % 2 ? 0xE8C080u : 0xD0A460u);
  }
  for (int k = 0; k < K.buildings; ++k) {  // the city's nearer blocks: their lit windows, a roof
    const float w = B.rr(0.012f, 0.02f), h = B.rr(0.03f, 0.06f), x = -X + 0.01f + (2 * X - 0.02f) * (k + 0.5f) / K.buildings + B.rr(-0.004f, 0.004f);
    const float z = B.rr(-0.075f, -0.06f);
    if (!clear(x, z, 0.5f * w)) continue;
    const float by = ground(x, z);
    B.delay = depthDelay(z);
    B.pivot = by;
    const uint32_t c = K.neon ? mix_rgb(0x2A2848, 0x48306A, B.rnd()) : mix_rgb(0xC8B8A8, 0x8A9AB0, B.rnd());
    B.box({x, by + 0.5f * h, z}, {0.5f * w, 0.5f * h, 0.006f}, c, dark(c, 0.3f));
    B.glow = true;
    for (float wy = by + 0.005f; wy < by + h - 0.004f; wy += 0.0055f)
      for (float wx = x - 0.5f * w + 0.0025f; wx < x + 0.5f * w - 0.002f; wx += 0.0038f)
        if (B.rnd() > (K.night ? 0.35f : 0.6f))
          B.quad({wx, wy, z + 0.0062f}, {wx + 0.0018f, wy, z + 0.0062f}, {wx + 0.0018f, wy + 0.0026f, z + 0.0062f}, {wx, wy + 0.0026f, z + 0.0062f},
                 K.neon ? (B.rnd() > 0.5f ? 0xFF5AD8u : 0x5AE8FFu) : (K.night ? 0xFFE08Au : 0xBFDDF2u));
    B.glow = false;
  }
  for (int k = 0; k < K.pylons; ++k) {  // power pylons, their lights
    const float x = -0.06f + 0.06f * k, z = -0.088f, by = ground(x, z);
    B.delay = depthDelay(z);
    B.pivot = by;
    B.cyl({x - 0.003f, by, z}, 0.0007f, 0.0005f, 0.04f, 4, 0x8A8EA0, false, 0.003f);
    B.cyl({x + 0.003f, by, z}, 0.0007f, 0.0005f, 0.04f, 4, 0x8A8EA0, false, -0.003f);
    B.box({x, by + 0.03f, z}, {0.008f, 0.0006f, 0.0006f}, 0x9A9EB0);
    B.glow = true;
    B.ball({x, by + 0.041f, z}, {0.0014f, 0.0014f, 0.0014f}, 0xFF4A4A, 0.0f);
    B.glow = false;
  }
  for (int k = 0; k < K.houses; ++k)
    if (spot(0.012f, -0.05f, -0.09f, &p)) {  // houses: walls, a pitched roof, lit windows, a door
      B.delay = depthDelay(p.z);
      B.pivot = p.y;
      const float w = B.rr(0.009f, 0.013f), h = B.rr(0.008f, 0.012f), d = 0.007f;
      const uint32_t wc = biome == kHbHaunt ? K.wall : mix_rgb(K.wall, 0xF0C890, B.rnd() * 0.5f);
      B.box({p.x, p.y + 0.5f * h, p.z}, {w, 0.5f * h, d}, wc);
      const uint32_t rc = biome == kHbTown ? (k % 2 ? 0xC84A3Au : 0x3A6AC0u) : K.roof;
      B.quad({p.x - w - 0.001f, p.y + h, p.z + d + 0.001f}, {p.x + w + 0.001f, p.y + h, p.z + d + 0.001f}, {p.x + w + 0.001f, p.y + h + 0.007f, p.z},
             {p.x - w - 0.001f, p.y + h + 0.007f, p.z}, rc);
      B.quad({p.x + w + 0.001f, p.y + h, p.z - d - 0.001f}, {p.x - w - 0.001f, p.y + h, p.z - d - 0.001f}, {p.x - w - 0.001f, p.y + h + 0.007f, p.z},
             {p.x + w + 0.001f, p.y + h + 0.007f, p.z}, dark(rc, 0.15f));
      B.tri({p.x - w, p.y + h, p.z + d}, {p.x - w, p.y + h, p.z - d}, {p.x - w, p.y + h + 0.007f, p.z}, wc);
      B.tri({p.x + w, p.y + h, p.z - d}, {p.x + w, p.y + h, p.z + d}, {p.x + w, p.y + h + 0.007f, p.z}, wc);
      B.glow = true;
      for (int w2 = -1; w2 <= 1; w2 += 2)
        B.quad({p.x + w2 * 0.5f * w - 0.0014f, p.y + 0.4f * h, p.z + d + 0.0003f}, {p.x + w2 * 0.5f * w + 0.0014f, p.y + 0.4f * h, p.z + d + 0.0003f},
               {p.x + w2 * 0.5f * w + 0.0014f, p.y + 0.75f * h, p.z + d + 0.0003f}, {p.x + w2 * 0.5f * w - 0.0014f, p.y + 0.75f * h, p.z + d + 0.0003f},
               K.night ? 0xFFD070u : 0xFFF0B0u);
      B.glow = false;
      B.quad({p.x - 0.0015f, p.y, p.z + d + 0.0003f}, {p.x + 0.0015f, p.y, p.z + d + 0.0003f}, {p.x + 0.0015f, p.y + 0.5f * h, p.z + d + 0.0003f},
             {p.x - 0.0015f, p.y + 0.5f * h, p.z + d + 0.0003f}, 0x5A3A26);
    }
  for (int k = 0; k < K.trees; ++k)
    if (spot(0.008f, -0.03f, -0.098f, &p)) {
      int kind = K.treeKind;
      if (kind == 4) kind = B.rnd() > 0.5f ? 0 : 1;
      if (kind == 0 && biome == kHbJungle) kind = B.rnd() > 0.5f ? 0 : 2;
      tree(p, kind == 3 ? 3 : kind, B.rr(0.8f, 1.35f) * (p.z < -0.07f ? 0.9f : 1.0f));
    }
  for (int k = 0; k < K.mushrooms; ++k)
    if (spot(0.01f, -0.035f, -0.08f, &p)) {  // big spotted mushrooms
      B.delay = depthDelay(p.z);
      B.pivot = p.y;
      const float s = B.rr(0.8f, 1.3f);
      B.cyl(p, 0.0035f * s, 0.003f * s, 0.009f * s, 8, 0xF6EEDC);
      B.ball({p.x, p.y + 0.011f * s, p.z}, {0.009f * s, 0.006f * s, 0.009f * s}, k % 2 ? 0xE83A30u : 0x3A9A4Au, 0.05f);
      for (int d = 0; d < 4; ++d)
        B.ball({p.x + 0.0055f * s * std::cos(d * 1.6f), p.y + 0.0135f * s, p.z + 0.0055f * s * std::sin(d * 1.6f) + 0.002f},
               {0.0018f * s, 0.0012f * s, 0.0018f * s}, 0xFFFFFF, 0.05f);
    }
  for (int k = 0; k < K.pipes; ++k)
    if (spot(0.008f, -0.035f, -0.075f, &p)) {
      B.delay = depthDelay(p.z);
      B.pivot = p.y;
      B.cyl(p, 0.005f, 0.005f, 0.012f, 10, 0x2EA83A);
      B.cyl({p.x, p.y + 0.012f, p.z}, 0.0065f, 0.0065f, 0.004f, 10, 0x3AC84A);
    }
  for (int k = 0; k < K.blocks; ++k) {  // ? blocks floating, bobbing
    const float x = -0.05f + 0.05f * k + B.rr(-0.005f, 0.005f), z = B.rr(-0.05f, -0.07f);
    B.delay = depthDelay(z);
    B.pivot = ground(x, z);
    B.anim(kHaBob, 0.0018f, 0.0f, B.rr(0, 6));
    B.box({x, Y0 + 0.032f + B.rr(0.0f, 0.01f), z}, {0.0035f, 0.0035f, 0.0035f}, 0xF6C030, 0xFFDA50);
    B.anim(kHaStatic);
  }
  for (int k = 0; k < K.columns; ++k) {  // a temple's columns
    const float x = -0.06f + 0.04f * k, z = -0.08f, by = ground(x, z);
    B.delay = depthDelay(z);
    B.pivot = by;
    B.cyl({x, by, z}, 0.003f, 0.0027f, 0.034f, 8, 0xEDE6F6);
    B.box({x, by + 0.035f, z}, {0.0045f, 0.0012f, 0.0045f}, 0xF6F0FF);
  }
  for (int k = 0; k < K.crystals; ++k)
    if (spot(0.006f, -0.035f, -0.09f, &p)) {  // glowing crystals, bobbing
      B.delay = depthDelay(p.z);
      B.pivot = p.y;
      B.anim(kHaBob, 0.0015f, 0.0f, B.rr(0, 6));
      B.glow = true;
      const float s = B.rr(0.8f, 1.4f);
      B.ball({p.x, p.y + 0.012f * s, p.z}, {0.0025f * s, 0.008f * s, 0.0025f * s}, k % 2 ? 0x9AF0FFu : 0xE89AFFu, 0.0f, 0xFFFFFF);
      B.glow = false;
      B.anim(kHaStatic);
    }
  for (int k = 0; k < K.graves; ++k)
    if (spot(0.005f, -0.03f, -0.07f, &p)) {
      B.delay = depthDelay(p.z);
      B.pivot = p.y;
      B.box({p.x, p.y + 0.004f, p.z}, {0.0025f, 0.004f, 0.0008f}, 0x8A8A96);
      B.ball({p.x, p.y + 0.008f, p.z}, {0.0025f, 0.0015f, 0.0008f}, 0x8A8A96, 0.0f);
    }
  for (int k = 0; k < K.islands; ++k) {  // floating islands, bobbing, a tree on each
    const float x = -0.055f + 0.055f * k, z = -0.07f - 0.012f * (k % 2), y = Y0 + 0.02f + 0.012f * (k % 2);
    B.delay = depthDelay(z);
    B.pivot = Y0;
    B.anim(kHaBob, 0.002f, 0.0f, k * 2.1f);
    B.cyl({x, y - 0.012f, z}, 0.0f, 0.011f, 0.012f, 8, 0x8A6A4A, false);
    B.cyl({x, y, z}, 0.011f, 0.0105f, 0.0025f, 8, 0x6CC04A);
    B.cyl({x + 0.003f, y + 0.0025f, z}, 0.0012f, 0.0009f, 0.008f, 5, K.trunk);
    B.ball({x + 0.003f, y + 0.013f, z}, {0.006f, 0.005f, 0.006f}, K.leaf, 0.2f);
    B.anim(kHaStatic);
  }
  for (int k = 0; k < K.lamps; ++k)
    if (spot(0.003f, -0.025f, -0.05f, &p)) {
      B.delay = depthDelay(p.z);
      B.pivot = p.y;
      B.cyl(p, 0.0006f, 0.0005f, 0.02f, 5, 0x3A3A44);
      B.glow = true;
      B.ball({p.x, p.y + 0.021f, p.z}, {0.0018f, 0.0018f, 0.0018f}, K.neon ? 0xFF8AF0u : 0xFFE6A0u, 0.0f);
      B.glow = false;
    }
  for (int k = 0; k < K.rocks; ++k)
    if (spot(0.005f, -0.01f, -0.09f, &p)) {
      B.delay = depthDelay(p.z);
      B.pivot = p.y;
      const float s = B.rr(0.003f, 0.007f);
      B.ball({p.x, p.y + 0.4f * s, p.z}, {s * 1.3f, s * 0.8f, s}, mix_rgb(K.rock, 0x000000, B.rr(0.0f, 0.2f)), 0.3f, light(K.rock, 0.12f));
    }
  // a plinth for a figure the fit raised: a rock it stands on
  if (hasFig && fit.plinthTop > Y0 + 0.002f) {
    B.delay = 0.3f;
    B.pivot = Y0;
    const float hh = fit.plinthTop - Y0;
    B.ball({fit.feet.x, Y0 + 0.5f * hh, fit.feet.z}, {fit.plinthHalfW * 1.2f, 0.5f * hh + 0.001f, 0.012f}, K.rock, 0.12f);
  }
  // foreground: framing bushes / rocks at the window's sides, near
  for (int side = -1; side <= 1; side += 2)
    for (int k = 0; k < 2; ++k) {
      const float x = side * B.rr(0.058f, 0.078f), z = B.rr(-0.006f, -0.02f), y = ground(x, z);
      B.delay = 0.55f;
      B.pivot = y;
      if (K.grassN > 0 || biome == kHbPastel) {
        B.anim(kHaSway, 0.0006f, 0.012f, B.rr(0, 6));
        B.ball({x, y + 0.004f, z}, {0.008f, 0.006f, 0.005f}, k ? K.leaf : K.leaf2, 0.25f, light(K.leaf, 0.12f));
        B.anim(kHaStatic);
      } else {
        B.ball({x, y + 0.003f, z}, {0.007f, 0.005f, 0.005f}, K.rock, 0.3f);
      }
    }
  // grass blades and flowers, swaying
  B.anim(kHaSway, 0.0009f, 0.006f, 0.0f);
  for (int k = 0; k < K.grassN + K.flowers; ++k) {
    const float x = B.rr(-X + 0.002f, X - 0.002f), z = B.rr(-0.004f, -0.09f);
    if (K.sea && z < -0.04f) continue;
    if (K.pond && std::hypot((x - 0.045f) / 0.03f, (z + 0.065f) / 0.018f) < 1.0f) continue;
    if (std::fabs(x - fx) < 0.012f && std::fabs(z - fz) < 0.012f) continue;
    const float y = ground(x, z), h = B.rr(0.003f, 0.0065f), w = 0.0007f, lean = B.rr(-0.001f, 0.001f);
    B.delay = depthDelay(z) + 0.1f;
    B.pivot = y;
    B.an[3] = B.rr(0.0f, 6.28f);
    B.an[2] = h;
    const uint32_t gc = mix_rgb(K.grass, 0x000000, B.rr(0.0f, 0.25f));
    B.tri({x - w, y, z}, {x + w, y, z}, {x + lean, y + h, z + 0.0004f}, gc);
    if (k >= K.grassN) {  // a flower on it
      static const uint32_t kFl[5] = {0xFFFFFF, 0xFFE040, 0xFF7AA8, 0xC8A0FF, 0xFF5A4A};
      B.ball({x + lean, y + h + 0.0008f, z + 0.0004f}, {0.0012f, 0.0009f, 0.0012f}, kFl[k % 5], 0.0f, 0xFFF0A0);
    }
  }
  B.anim(kHaStatic);
  // clouds drifting across (puff clusters), birds flapping across
  for (int k = 0; k < K.clouds; ++k) {
    const float x = B.rr(-X, X), y = B.rr(0.018f, 0.045f), z = B.rr(-0.098f, -0.085f);
    B.delay = 0.0f;
    B.pivot = y;
    B.anim(kHaDrift, x, B.rr(0.0015f, 0.003f), B.rr(0, 6));
    for (int b2 = 0; b2 < 4; ++b2)
      B.ball({x + (b2 - 1.5f) * 0.0045f, y + (b2 % 2) * 0.002f, z}, {0.005f, 0.0035f, 0.002f}, b2 % 2 ? 0xFFFFFFu : 0xEEF4FFu, 0.1f);
    B.anim(kHaStatic);
  }
  for (int k = 0; k < K.birds; ++k) {
    const float x = B.rr(-X, X), y = B.rr(0.012f, 0.035f), z = B.rr(-0.07f, -0.05f);
    const uint32_t c = K.night ? 0x14121Cu : (biome == kHbTropic ? 0xFFFFFFu : 0x3A3A44u);
    B.delay = 0.0f;
    B.pivot = y;
    const float sp2 = B.rr(0.006f, 0.012f), ph = B.rr(0, 6);
    B.anim(kHaBird, x, sp2, 0.0f);
    B.tri({x - 0.0006f, y, z}, {x + 0.0006f, y, z}, {x, y - 0.0004f, z}, c);
    B.anim(kHaBird, x, sp2, ph + 1.0f);  // (its wing tips flap)
    B.tri({x - 0.0005f, y, z}, {x, y - 0.0002f, z}, {x - 0.004f, y + 0.0015f, z}, c);
    B.tri({x + 0.0005f, y, z}, {x + 0.004f, y + 0.0015f, z}, {x, y - 0.0002f, z}, c);
    B.anim(kHaStatic);
  }
  // soft parts: the figure's shadow on the ground; light shafts; fog rolling
  if (hasFig) {
    const float w = clampf(0.6f * fit.scale * fit.bw, 0.012f, 0.05f), d = clampf(0.6f * fit.scale * fit.bd, 0.008f, 0.03f);
    const float y = std::max(Y0, fit.plinthTop) + 0.0004f;
    B.delay = -10.0f;
    B.pivot = y;
    B.pic({fit.feet.x - w, y, fit.feet.z + d}, {fit.feet.x + w, y, fit.feet.z + d}, {fit.feet.x + w, y, fit.feet.z - d}, {fit.feet.x - w, y, fit.feet.z - d},
          kHpShadow, 1.0f);
  }
  if (K.shafts)
    for (int k = 0; k < 4; ++k) {
      const float x = B.rr(-0.06f, 0.06f), z = B.rr(-0.09f, -0.06f), w = B.rr(0.004f, 0.008f), lean = 0.025f;
      B.delay = 0.6f;
      B.pivot = Y0;
      B.anim(kHaShaft, x, 0.0f, B.rr(0, 6));
      B.pic({x - w, Y0, z}, {x + w, Y0, z}, {x + w + lean, 0.07f, z}, {x - w + lean, 0.07f, z}, kHpShaft, 1.0f);
      B.anim(kHaStatic);
    }
  if (K.fogA > 0.0f)
    for (int k = 0; k < 3; ++k) {
      const float z = -0.06f - 0.014f * k, y = ground(0.0f, z) + 0.002f;
      B.delay = 0.5f;
      B.pivot = y;
      B.anim(kHaFog, 0.012f, 0.0f, k * 2.0f);
      B.pic({-0.1f, y - 0.002f, z}, {0.1f, y - 0.002f, z}, {0.1f, y + 0.014f, z}, {-0.1f, y + 0.014f, z}, kHpFog, 1.0f);
      B.anim(kHaStatic);
    }
  // its particles (quads placed every frame: home_moves)
  for (int k = 0; k < K.fxN; ++k) {
    HomeParticle q;
    q.kind = K.fx;
    q.p = {B.rr(-X, X), B.rr(Y0, 0.05f), B.rr(-0.006f, -0.09f)};
    q.phase = B.rr(0.0f, 6.28f);
    q.size = K.fx == kHfSnow ? 0.0011f : (K.fx == kHfFireflies || K.fx == kHfSparkles ? 0.0012f : 0.0016f);
    q.vx = K.fx == kHfSand ? 0.012f : B.rr(-0.002f, 0.002f);
    q.vy = K.fx == kHfEmbers || K.fx == kHfBubbles ? B.rr(0.005f, 0.01f) : (K.fx == kHfFireflies || K.fx == kHfSparkles ? 0.0f : -B.rr(0.004f, 0.008f));
    M.fx.push_back(q);
    const bool glow = K.fx == kHfEmbers || K.fx == kHfFireflies || K.fx == kHfSparkles || K.fx == kHfBubbles;
    B.glow = glow;
    B.delay = -10.0f;
    B.anim(kHaParticle, static_cast<float>(k), 0.0f, 0.0f);
    uint32_t c = K.fxCol;
    if (K.fx == kHfLeaves) c = B.rnd() > 0.5f ? 0xE89A3Au : (biome == kHbCity ? 0xD0B040u : K.leaf);
    if (K.fx == kHfFireflies && K.fxCol == 0xFFFFFF) c = 0xE0FF7A;
    B.tri({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, c);
    B.tri({0, 0, 0}, {0, 0, 0}, {0, 0, 0}, c);
    for (int j = 0; j < 6; ++j) M.anim[M.anim.size() - 6 * (6 - j) + 4] = static_cast<float>(j);  // (its corner, in its 2 triangles)
    B.glow = false;
    B.anim(kHaStatic);
  }
  M.lo = {-X, Y0, -0.095f};
  M.hi = {X, 0.06f, -0.004f};
  M.ok = true;
  (void)K.stars;
}
// its pictures (kHp order: the sky, the palette, the shadow, a light shaft, fog) - the sky its gradient, its sun / moon,
// stars at night; the palette's swatches (16 x 16)
void home_pictures(const HomeMesh& M, const std::string& name, std::vector<std::vector<uint8_t>>* tex, std::vector<int>* tw,
                   std::vector<int>* th) {
  const int biome = home_biome(name);
  const HomeKit K = home_kit(biome, name);
  auto add = [&](std::vector<uint8_t> px, int w, int h) {
    tex->push_back(std::move(px));
    tw->push_back(w);
    th->push_back(h);
  };
  {  // the sky (256 x 512: up the sky quad, 0.21 m tall from 6 cm under the window's bottom)
    const int W = 256, H = 512;
    std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const float v = static_cast<float>(y) / H;  // (0 top)
        uint32_t c = mix_rgb(K.skyTop, K.skyBot, smooth(clampf((v - 0.15f) / 0.6f, 0.0f, 1.0f)));
        const float sd = std::hypot((x - 170.0f) / 256.0f, (y - 250.0f) / 512.0f * 2.0f);
        c = mix_rgb(c, K.sun, clampf(0.55f - sd * 2.2f, 0.0f, 1.0f));                      // (its glow)
        c = mix_rgb(c, K.night ? 0xF4F2FF : 0xFFFCEE, clampf((0.045f - sd) * 200.0f, 0.0f, 1.0f));  // (the sun / the moon)
        if (K.stars && v < 0.6f && pp_h01(static_cast<uint32_t>(y * W + x) * 7u + 3u) > 0.9965f) c = 0xFFFFFF;
        if (biome == kHbAlien) {  // (a big ringed planet)
          const float pd = std::hypot((x - 70.0f) / 256.0f, (y - 150.0f) / 512.0f * 2.0f);
          if (pd < 0.12f) c = mix_rgb(0xE8A0D0, 0x6A4AA0, clampf(pd / 0.12f, 0.0f, 1.0f));
        }
        uint8_t* p = &px[(static_cast<size_t>(y) * W + x) * 4];
        p[0] = (c >> 16) & 255, p[1] = (c >> 8) & 255, p[2] = c & 255, p[3] = 255;
      }
    add(std::move(px), W, H);
  }
  {  // the palette
    std::vector<uint8_t> px(256 * 256 * 4, 255);
    for (int y = 0; y < 256; ++y)
      for (int x = 0; x < 256; ++x) {
        const size_t s = static_cast<size_t>((y / 16) * 16 + x / 16);
        const uint32_t c = s < M.pal.size() ? M.pal[s] : 0xFF00FF;
        uint8_t* p = &px[(static_cast<size_t>(y) * 256 + x) * 4];
        p[0] = (c >> 16) & 255, p[1] = (c >> 8) & 255, p[2] = c & 255;
      }
    add(std::move(px), 256, 256);
  }
  auto soft = [&](int W, int H, uint32_t col, auto alpha) {
    std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4);
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        uint8_t* p = &px[(static_cast<size_t>(y) * W + x) * 4];
        p[0] = (col >> 16) & 255, p[1] = (col >> 8) & 255, p[2] = col & 255;
        p[3] = static_cast<uint8_t>(std::lround(clampf(alpha((x + 0.5f) / W, (y + 0.5f) / H), 0.0f, 1.0f) * 255.0f));
      }
    add(std::move(px), W, H);
  };
  soft(64, 64, 0x0A0806, [](float u, float v) { return 0.6f * std::pow(clampf(1.0f - std::hypot(u - 0.5f, v - 0.5f) * 2.0f, 0.0f, 1.0f), 1.5f); });
  soft(32, 128, K.night ? 0xC8B8FF : 0xFFF2C8, [](float u, float v) {
    return 0.35f * std::exp(-std::pow((u - 0.5f) / 0.22f, 2.0f)) * smooth(clampf(v / 0.3f, 0.0f, 1.0f)) * smooth(clampf((1.0f - v) / 0.4f, 0.0f, 1.0f));
  });
  const float fa = K.fogA;
  soft(256, 64, K.fog, [fa](float u, float v) {
    const float n = 0.6f + 0.4f * pp_noise(u * 9.0f, v * 3.0f, 77);
    return fa * n * std::sin(3.1415927f * u) * smooth(clampf(v / 0.4f, 0.0f, 1.0f)) * smooth(clampf((1.0f - v) / 0.5f, 0.0f, 1.0f));
  });
}

// A special's model, made on a thread: its pictures, its figure (or its picture standing up when it has none here).
struct SpecialBuild {
  ~SpecialBuild() {
    if (figure != nullptr) xrevo::release(figure);  // (a scene put away before its figure was taken)
    if (celFig != nullptr) xrevo::release(celFig);  // (r181)
  }
  std::mutex m;
  bool done = false;
  // r181: its celebration made after the card is up (first reveal): the same pack with its living pose file, to swap in for
  // the standing one (celVerts: its vertices - the same pack's, checked)
  xrevo::Figure* celFig = nullptr;
  uint32_t celVerts = 0;
  // r193: its celebration still to be made after the card is up (its figure holds a still meanwhile); that tried (done
  // or not)
  bool celLater = false, celTried = false;
  std::vector<std::vector<uint8_t>> tex;
  std::vector<int> tw, th;
  xrfx::Model fig;
  xrevo::Figure* figure = nullptr;
  int standee = -1;  // the picture standing in for the figure (its texture), -1 none
  Fit fit;           // r140: where and how big its figure goes
  // r149: a TIZM BOOM's layers (their textures, from layerTex: a burst, three words in bubbles - r150); an
  // autograph writing itself (its texture, -1 none)
  int layerTex = -1, layers = 0, signTex = -1;
  float signDur = 2.5f;  // (its autograph's time to write)
  float signY = 0.0f, signZ = 3.0f;  // (its ink's colour in its normals: sign_ink)
  // r153: a GOLD's solid face (its picture: today's front, over its window; its texture, -1 none) and its figure's place
  // on the top edge (fit: in its room)
  int solidTex = -1;
  Fit fitTop;
  HomeMesh home;  // r171: a HOMETOWN's diorama (its pictures at the room's: roomTex)
  int cutBackTex = -1;  // r220: a JAM '96 die's own back (its texture, last), -1 none
};
struct Special {
  int set = 0;  // r217: its card's set (its autograph's place: lk::sign_box)
  // r220 (THE PUNCH): its card's die (-1 an A card / the crest) and its foot (its autograph's place moves up it); its shell's vertices
  // before the punch (the crest: its front inside the crest, the crest back, the crest edge) and after (its whole front, its die's
  // back, its die's edge) - the first shellN of verts; which is in (-1 not yet, 0 before, 1 punching, 2 after)
  int shape = -1;
  float foot = 0.0f;
  int shellN = 0;
  std::vector<float> shellPre, shellPost;
  int shellState = -1;
  int i = -1;  // the scene's card (S.card[i]), -1 none
  int kind = kSpNone;
  bool shiny = false;
  std::shared_ptr<SpecialBuild> build;
  bool up = false;  // its model in its slot
  xrevo::Figure* fig = nullptr;
  int figBase = 0, figCount = 0;  // its figure's vertices in verts
  int standBase = -1;             // (its standee quad's)
  int loopStart = 0, loopFrames = 0;  // a move it loops (a home figure's), 0: its standing loop
  float figScale = 1.0f, figHeight = 0.1f;
  Fit fit;  // r140
  std::vector<float> verts, posed;
  float spot = 0.0f;
  uint64_t spotUntil = 0;
  uint64_t usedNs = 0;  // r139: when its card last came to the stack's front (the older one makes way)
  // r141 (a BLACK 1/1): its world's theme (-1 none), its voice (roster place, -1 none), its throne's things kept while
  // its cameo hides them (its shell, slab, glass, pedestal: verts before figBase)
  int theme = -1, roster = -1;
  int klass = -1;  // (a Strikers character's class: its voice through the game's own, PortStrikersVoiceSay)
  std::vector<float> statCopy;
  uint32_t rgb = 0;  // r147: its card's colour (a BLACK 1/1's kaleidoscope)
  // r149: a HOMETOWN (landscape: its room and figure turned onto the card), a TIZM BOOM's layers (their vertices, from
  // popBase: 4 each), an autograph's (its 4 vertices; when it started writing, 0 not yet; written in full already)
  bool home = false, signedCard = false, signDone = false;
  int popBase = -1, pops = 0, signBase = -1;
  uint64_t signNs = 0;
  float signDur = 2.5f, signY = 0.0f, signZ = 3.0f;
  XrVector3f popAt[8] = {};
  float popW[8] = {}, popH[8] = {};
  // r150: a TIZM BOOM's pop-out show (when it started, 0 none; played once already), and a shake of it (its last place
  // and time; its last fast move's direction and time; the turns back and forth in it)
  uint64_t boomNs = 0;
  bool boomDone = false;
  XrVector3f lastPos{0.0f, 0.0f, 0.0f}, fastDir{0.0f, 0.0f, 0.0f};
  uint64_t lastNs = 0, fastNs = 0, flipNs = 0;
  int flips = 0;
  // r153: a GOLD's step-out loop - its solid face's strips (from solidBase: (kGoldRows + 1) x 2 vertices, -1 none), its
  // figure's place on top (fit: in its room), how far into the loop (only while its face is to you), how much of the
  // window its face covers (0 open .. 1 solid), how much it stands on top (0 .. 1), its dive's burst done this time round
  int solidBase = -1;
  Fit fitTop;
  float goldT = kGoldIn, goldShut = 0.0f, goldTop = 0.0f;
  bool goldBurst = false;
  // r171: a HOMETOWN's diorama (its vertices from homeBase, as built in its own frame, how each moves; its particles; its
  // pop-up: when it started, 0 not yet; how long it's been face up waiting to be turned landscape); a GOLD's shine (its
  // quads from goldFxBase: light sweeps, a halo behind its figure, a light pool, twinkles)
  int homeBase = -1, homeN = 0;
  std::vector<float> homeRest, homeAnim;
  std::vector<HomeParticle> homeFx;
  XrVector3f homeLo{0, 0, 0}, homeHi{0, 0, 0};
  uint64_t homeNs = 0;
  float homeWait = 0.0f;
  int goldFxBase = -1;
  int airBase = -1;        // r179: its autograph's neon copy in the air (its quad)
  bool airBuzzed = false;  // (its neon buzz played)
  // r193: the autograph's MOMENT (sign_moment): when it started gathering (0 not yet), when it landed on the card (0 not
  // yet); how long its face has been away mid-way (over 0.6 s: it starts again next time); where it hangs (world, eased
  // after the card); its sounds played (1 gather, 2 + write); when its face first came to you; its strokes (pen tip)
  uint64_t airNs = 0, landNs = 0, frontNs = 0, airLastNs = 0;
  float awayS = 0.0f;
  XrVector3f airPos{0.0f, 0.0f, 0.0f};
  bool airPosOk = false;
  int airSnd = 0;
  const void* sig = nullptr;  // (xrsign::Signature)
  // r193 (Lance: the 1/1's platform "was visible before I flipped the card over"): its 3D on top rising out of the card's
  // top edge once its face is to you (0 hidden .. 1 all), xr_fx_model.frag
  float reveal = 0.0f;
  // r193 (Lance: the SILVER Croconaw "stuttered like 5 times before running in place"): its celebration still being made
  // (its figure holds a still pose - never the raw standing file - until it's here); a blend from the pose it held (its
  // vertices) into the new one (blendT s in, < 0 none); and between its idle and its big move (both ways)
  bool celPending = false;
  std::vector<float> blendFrom;
  float blendT = -1.0f;
  // r173: its figure's living loop (frames in its pose file: its idle, its big move; 0 none)
  uint32_t liveIdleStart = 0, liveIdleFrames = 0, liveBigStart = 0, liveBigFrames = 0;
};
Special g_sp[kMaxSpecials];

// ============================================================================ r141: the BLACK 1/1's life-size cameo
// (coordinator, Lance's pick: "LIFE-SIZE CAMEO, FIRST REVEAL ONLY ... the room dims; the character steps OUT life-size
// ~1.5-2 m away on the floor facing him ..., does its full celebration with its voice line, then shrinks back onto the
// card's throne. Skippable with B."): the first time each BLACK 1/1 is turned over in a pack (collection.txt "cameo
// <key>": never again). Its throne's figure (its special's own model, the card shown flat by the cards' model meanwhile)
// leaps out of the card growing to kCamH tall, kCamDist ahead of you on the floor, turned to you; its celebration once
// (its idle as it comes and goes); its voice (a guest's own; none for a Strikers character here); back onto its throne.
// B (or a trigger / A) sends it back at once.
constexpr float kCamDim = 0.7f, kCamOut = 1.3f, kCamBack = 1.1f, kCamUndim = 0.6f;
constexpr float kCamH = 1.45f, kCamWide = 1.6f, kCamDist = 1.7f;
struct Cameo {
  int i = -1;  // the scene's card, -1 none
  std::string key;
  bool started = false, said = false, landed = false;
  float t = 0.0f, wait = 0.0f;  // seconds into it; waiting for its figure
  float celeb = 4.0f, backAt = 1.0e9f;
  XrVector3f spot{0.0f, 0.0f, 0.0f};
};
Cameo g_cam;
bool cameo_ask(int i, const std::string& key) {
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_cameoSeen.count(key) != 0) return false;
  }
  g_cam = Cameo{};
  g_cam.i = i;
  g_cam.key = key;
  return true;
}
bool cameo_live() { return g_cam.i >= 0; }
void cameo_skip() {
  Cameo& c = g_cam;
  if (c.i < 0) return;
  if (!c.started) {
    c = Cameo{};
    return;
  }
  const float out0 = kCamDim, out1 = kCamDim + kCamOut;
  if (c.t >= c.backAt) return;  // (on its way back already)
  if (c.t < out0) c.backAt = c.t - kCamBack;
  else if (c.t < out1) c.backAt = c.t - kCamBack * (1.0f - (c.t - out0) / kCamOut);  // (back from as far out as it got)
  else c.backAt = c.t;
  CLOG("cameo: skipped");
}
// how far out it is now (0 on its throne .. 1 life size), the room's dimming
float cameo_e(const Cameo& c, float* dim) {
  const float out0 = kCamDim, out1 = kCamDim + kCamOut, back1 = c.backAt + kCamBack;
  float e = 1.0f;
  if (c.t >= c.backAt) e = 1.0f - smooth((c.t - c.backAt) / kCamBack);
  else if (c.t < out0) e = 0.0f;
  else if (c.t < out1) e = smooth((c.t - out0) / kCamOut);
  const float in = smooth(c.t / kCamDim), out = c.t > back1 ? 1.0f - smooth((c.t - back1) / kCamUndim) : 1.0f;
  *dim = std::min(in, out);
  return e;
}
bool cameo_done(const Cameo& c) { return c.started && c.t >= c.backAt + kCamBack + kCamUndim; }
XrQuaternionf q_nlerp(const XrQuaternionf& a, XrQuaternionf b, float t) {
  if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) b = {-b.x, -b.y, -b.z, -b.w};
  XrQuaternionf r{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
  const float l = std::sqrt(r.x * r.x + r.y * r.y + r.z * r.z + r.w * r.w);
  return l > 1e-6f ? XrQuaternionf{r.x / l, r.y / l, r.z / l, r.w / l} : a;
}
struct LightPart {  // what each of the light model's quads is for
  int kind = 0;     // 0 an aura behind a card, 1 a smoke puff, 2 the pop-out's glow, 3 the beam, 4 the dimming
  int card = -1, n = 0, base = 0, count = 0;  // its card; puff number / dim level; its vertices
};
std::vector<LightPart> g_lightParts;
std::vector<float> g_lightVerts;
bool g_lightUp = false;
int g_lightN = 0;  // the vertices it has

// A JPEG / PNG off the headset, RGBA
bool load_rgba(const std::string& path, std::vector<uint8_t>* px, int* w, int* h) {
  int n = 0;
  unsigned char* d = stbi_load(path.c_str(), w, h, &n, 4);
  if (d == nullptr) return false;
  px->assign(d, d + static_cast<size_t>(*w) * *h * 4);
  stbi_image_free(d);
  return true;
}
// part of a picture (u0..u1, v0..v1 of it), bilinear, ow x oh
std::vector<uint8_t> crop(const std::vector<uint8_t>& src, int sw, int sh, float u0, float u1, float v0, float v1, int ow, int oh) {
  std::vector<uint8_t> out(static_cast<size_t>(ow) * oh * 4);
  for (int y = 0; y < oh; ++y)
    for (int x = 0; x < ow; ++x) {
      const float fx = (u0 + (u1 - u0) * (x + 0.5f) / ow) * sw - 0.5f, fy = (v0 + (v1 - v0) * (y + 0.5f) / oh) * sh - 0.5f;
      const int x0 = std::max(0, std::min(sw - 1, static_cast<int>(std::floor(fx)))), y0 = std::max(0, std::min(sh - 1, static_cast<int>(std::floor(fy))));
      const int x1 = std::min(sw - 1, x0 + 1), y1 = std::min(sh - 1, y0 + 1);
      const float tx = clampf(fx - x0, 0.0f, 1.0f), ty = clampf(fy - y0, 0.0f, 1.0f);
      for (int c = 0; c < 4; ++c) {
        const float a = src[(static_cast<size_t>(y0) * sw + x0) * 4 + c], b = src[(static_cast<size_t>(y0) * sw + x1) * 4 + c];
        const float d = src[(static_cast<size_t>(y1) * sw + x0) * 4 + c], e = src[(static_cast<size_t>(y1) * sw + x1) * 4 + c];
        out[(static_cast<size_t>(y) * ow + x) * 4 + c] =
            static_cast<uint8_t>(std::lround((a * (1 - tx) + b * tx) * (1 - ty) + (d * (1 - tx) + e * tx) * ty));
      }
      out[(static_cast<size_t>(y) * ow + x) * 4 + 3] = 255;
    }
  return out;
}
std::vector<uint8_t> board_px(int w, int h, const Pc& P) {
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  xrtext::board(px.data(), w, h, P.v);
  for (size_t i = 0; i < px.size(); i += 4)  // (solid: straight colours)
    if (px[i + 3] > 0 && px[i + 3] < 255)
      for (int k = 0; k < 3; ++k) px[i + k] = static_cast<uint8_t>(std::min(255, px[i + k] * 255 / px[i + 3]));
  return px;
}

// The room behind a window (back wall, floor, left, right, ceiling: 512 x 512 / 512 x 256 each): a SILVER's own (its
// team's colours, a refractor's prism, a silver floor); a shiny's evolution room (the middle of evo_<theme>_outside.jpg:
// the horizon ahead, the ground, either side, the sky); obsidian's dark violet glass. Thread.
void room_pictures(const Card& c, int kind, bool shiny, std::vector<std::vector<uint8_t>>* tex, std::vector<int>* tw, std::vector<int>* th) {
  auto add = [&](std::vector<uint8_t> px, int w, int h) {
    tex->push_back(std::move(px));
    tw->push_back(w);
    th->push_back(h);
  };
  if (kind == kSpWindow && shiny && g_backdrop != nullptr) {
    static const char* const kAny[] = {"sky", "fire", "water", "grass", "electric", "rock", "psychic", "ghost", "city"};
    std::vector<uint8_t> eq;
    int ew = 0, eh = 0;
    const std::string want = g_backdrop(c.key);
    for (int k = -1; k < 9 && eq.empty(); ++k)
      if (!load_rgba(home_dir() + "/suites/evo_" + (k < 0 ? want : std::string(kAny[k])) + "_outside.jpg", &eq, &ew, &eh)) eq.clear();
    if (!eq.empty()) {
      add(crop(eq, ew, eh, 0.36f, 0.64f, 0.30f, 0.62f, 512, 512), 512, 512);  // ahead
      add(crop(eq, ew, eh, 0.36f, 0.64f, 0.62f, 0.86f, 512, 256), 512, 256);  // the ground
      add(crop(eq, ew, eh, 0.22f, 0.36f, 0.30f, 0.62f, 256, 512), 256, 512);  // to the left
      add(crop(eq, ew, eh, 0.64f, 0.78f, 0.30f, 0.62f, 256, 512), 256, 512);  // to the right
      add(crop(eq, ew, eh, 0.36f, 0.64f, 0.10f, 0.30f, 512, 256), 512, 256);  // the sky
      return;
    }
  }
  if (kind == kSpGlass) {
    // r138 (coordinator: "deep near-black glass ... maybe a faint purple inner glow behind it"): the slab's depths
    // near-black, a faint violet glow just behind the figure (it stands out against it; never a lit violet room)
    Pc B;
    B.box(0, 0, 512, 512, 0, 0x040307, 1.0f, 0x010103);
    B.glow(96, 120, 416, 500, 0x4A1C96, 0.55f);
    B.glow(186, 230, 326, 420, 0x8A50F0, 0.16f);
    add(board_px(512, 512, B), 512, 512);
    Pc S2;  // its sides: black glass, the faintest violet line where they meet the window
    S2.box(0, 0, 256, 256, 0, 0x040308, 1.0f, 0x010102);
    S2.box(0, 0, 256, 8, 0, 0x3A2278, 0.35f);
    for (int k = 0; k < 4; ++k) add(board_px(256, 256, S2), 256, 256);
    return;
  }
  if (kind == kSpPop) {
    // r153: a GOLD's room (its step-out loop starts in it): the SILVER's depth in gold - its team's colour deep in a gold
    // vault, a gold prism, a polished gold floor running away, gold walls
    // r171 (Lance: "way more shiny on the inside"): POLISHED gold - each surface a gold mirror: the room's light
    // reflected in it (bright bands where the lamps' reflections stretch, deep amber where it reflects the dark), beveled
    // panels catching a bright edge, a lamp in the ceiling; light sweeps / a halo / a pool / twinkles move over it
    // (gold_fx_moves)
    auto gold = [&](int W, int H, bool floor, bool ceilg, float seed) {
      PpCanvas G(W, H);
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
          const float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
          // its reflection: a few soft bright bands (the lights) over deep amber, streaked like polished metal
          float r = 0.18f;
          r += 0.75f * std::exp(-std::pow((u - 0.28f - 0.05f * std::sin(seed)) / 0.06f, 2.0f));
          r += 0.5f * std::exp(-std::pow((u - 0.66f) / 0.035f, 2.0f));
          r += 0.25f * std::exp(-std::pow((u - 0.86f) / 0.08f, 2.0f));
          if (floor) r = 0.25f + 0.85f * std::exp(-std::pow((u - 0.5f) / 0.1f, 2.0f)) * (0.4f + 0.6f * v) + 0.3f * std::exp(-std::pow((u - 0.2f) / 0.04f, 2.0f));
          if (ceilg) r = 0.2f + 0.9f * std::exp(-(std::pow((u - 0.5f) / 0.18f, 2.0f) + std::pow((v - 0.55f) / 0.25f, 2.0f)));
          r += 0.06f * (pp_noise(u * 3.0f, v * 90.0f, 31) - 0.5f);  // (fine streaks)
          // beveled panels: a dark seam, a bright edge beside it
          const float cell = floor ? 0.125f : 0.25f;
          const float du = std::fmod(u, cell) / cell, dv = std::fmod(v, floor ? 0.25f : 0.5f) / (floor ? 0.25f : 0.5f);
          const float seam = std::min(std::min(du, 1.0f - du), std::min(dv, 1.0f - dv));
          if (seam < 0.025f) r *= 0.35f;
          else if (seam < 0.06f) r += 0.45f * (du < 0.5f || dv < 0.5f ? 1.0f : 0.3f);
          r = clampf(r, 0.0f, 1.6f);
          // gold: deep amber -> rich gold -> pale bright gold -> near white at the hottest
          uint32_t col = r < 0.5f ? mix_rgb(0x3A2404, 0xB07A12, r / 0.5f)
                                  : (r < 1.0f ? mix_rgb(0xB07A12, 0xFFD45A, (r - 0.5f) / 0.5f) : mix_rgb(0xFFD45A, 0xFFF8E0, clampf(r - 1.0f, 0.0f, 1.0f)));
          col = mix_rgb(col, c.c1, 0.06f);
          G.over(y * W + x, col, 1.0f);
        }
      add(G.bytes(), W, H);
    };
    gold(512, 512, false, false, 0.3f);  // ahead
    gold(512, 256, true, false, 0.0f);   // the floor
    gold(256, 512, false, false, 1.7f);  // left
    gold(256, 512, false, false, 4.1f);  // right
    gold(512, 256, false, true, 0.0f);   // the ceiling and its lamp
    // its shine's pictures: a light sweep (a soft white-gold band), a glow (soft round), a twinkle (four curved points)
    auto softp = [&](int W, int H, uint32_t col, auto alpha) {
      std::vector<uint8_t> px(static_cast<size_t>(W) * H * 4);
      for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
          uint8_t* p = &px[(static_cast<size_t>(y) * W + x) * 4];
          p[0] = (col >> 16) & 255, p[1] = (col >> 8) & 255, p[2] = col & 255;
          p[3] = static_cast<uint8_t>(std::lround(clampf(alpha((x + 0.5f) / W, (y + 0.5f) / H), 0.0f, 1.0f) * 255.0f));
        }
      add(std::move(px), W, H);
    };
    softp(64, 64, 0xFFF6D8, [](float u, float v) { return 0.7f * std::exp(-std::pow((u - 0.5f) / 0.18f, 2.0f)) * std::sin(3.1415927f * v); });
    softp(128, 128, 0xFFD870, [](float u, float v) { return 0.75f * std::pow(clampf(1.0f - std::hypot(u - 0.5f, v - 0.5f) * 2.0f, 0.0f, 1.0f), 1.6f); });
    softp(64, 64, 0xFFFBEA, [](float u, float v) {
      const float x = std::fabs(u - 0.5f) * 2.0f, y = std::fabs(v - 0.5f) * 2.0f;
      return std::pow(x, 0.55f) + std::pow(y, 0.55f) < 1.0f ? 1.0f : 0.0f;
    });
    return;
  }
  // a SILVER's room: its team's colours behind it, a prism, a silver floor
  Pc B;
  B.box(0, 0, 512, 512, 0, light(c.c1, 0.2f), 1.0f, dark(c.c1, 0.45f));
  prism(B, 0, 0, 512, 512, 256, 300, 22, 0.15f, 0xFFFFFF, 0xD6DDEA, 0x98A4B8, 0.3f);
  B.glow(90, 120, 422, 520, 0xFFFFFF, 0.45f);
  add(board_px(512, 512, B), 512, 512);
  Pc F;  // the floor: silver tiles running away
  F.box(0, 0, 512, 256, 0, 0xC9D0DC, 1.0f, 0x8C95A4);
  for (int k = 0; k <= 8; ++k) F.box(k * 64.0f - 2, 0, k * 64.0f + 2, 256, 0, 0x6E7788, 0.6f);
  for (int k = 0; k <= 4; ++k) F.box(0, k * 64.0f - 2, 512, k * 64.0f + 2, 0, 0x6E7788, 0.6f);
  add(board_px(512, 256, F), 512, 256);
  Pc S2;
  S2.box(0, 0, 256, 512, 0, dark(c.c1, 0.35f), 1.0f, dark(c.c1, 0.65f));
  prism(S2, 0, 0, 256, 512, 128, 256, 10, 0.3f, 0xD6DDEA, 0x98A4B8, 0x6E7788, 0.18f);
  add(board_px(256, 512, S2), 256, 512);
  add(board_px(256, 512, S2), 256, 512);
  Pc C2;
  C2.box(0, 0, 512, 256, 0, dark(c.c1, 0.7f));
  add(board_px(512, 256, C2), 512, 256);
}

// ===================================================================================== r149: case hits' things
// TIZM BOOM's words by its world's theme (fx_theme_of): a big one, two more
void boom_words(const std::string& key, std::string out[3]) {
  const std::string t = kFxTheme[std::max(0, std::min(kFxThemes - 1, fx_theme_of(key)))].name;
  auto has = [&](const char* w) { return t.find(w) != std::string::npos; };
  if (key.compare(0, 9, "strikers_") == 0) out[0] = "KAPOW!", out[1] = "GOAL!", out[2] = "BOOM!";
  else if (has("fire") || has("hell") || has("phoenix")) out[0] = "FWOOSH!", out[1] = "BOOM!", out[2] = "SIZZLE!";
  else if (has("water")) out[0] = "SPLOOSH!", out[1] = "SPLASH!", out[2] = "BLOOP!";
  else if (has("electric") || has("lightning") || has("bolt")) out[0] = "KRAKA-TOOM!", out[1] = "ZZZAP!", out[2] = "BZZT!";
  else if (has("grass") || has("bug")) out[0] = "THWACK!", out[1] = "SNAP!", out[2] = "FWIP!";
  else if (has("psychic") || has("psi") || has("cosmic") || has("rune") || has("teleport")) out[0] = "WOOOM!", out[1] = "VWORP!", out[2] = "ZING!";
  else if (has("ghost") || has("shadow") || has("dark") || has("invisible") || has("boo")) out[0] = "BOO!", out[1] = "WHOOO!", out[2] = "POOF!";
  else if (has("ice") || has("frost")) out[0] = "KRSSSH!", out[1] = "CRACK!", out[2] = "SHINK!";
  else if (has("flying")) out[0] = "WHOOSH!", out[1] = "SWOOP!", out[2] = "FWAP!";
  else if (has("dragon") || has("doom")) out[0] = "ROARRR!", out[1] = "KRAKA-TOOM!", out[2] = "BOOM!";
  else if (has("web")) out[0] = "THWIP!", out[1] = "THWIP!", out[2] = "SPLAT!";
  else out[0] = "BOOM!", out[1] = "POW!", out[2] = "KAPOW!";
}
// HOMETOWN's home: its picture (STRIKERS_HOME/suites/<name>_outside.jpg - the game's own suites by series, a Pokemon's
// evolution room by its kind, the city for a hero) and its place's name
std::string home_backdrop(const Card& c, std::string* place) {
  struct Home {
    const char* key;
    const char* suite;
    const char* place;
  };
  static const Home kHome[] = {
      {"strikers_mario", "suite_mario", "MUSHROOM KINGDOM"}, {"strikers_luigi", "suite_luigi", "LUIGI'S MANSION"},
      {"strikers_peach", "suite_peach", "PEACH'S CASTLE"}, {"strikers_daisy", "suite_daisy", "SARASALAND"},
      {"strikers_yoshi", "suite_yoshi", "YOSHI'S ISLAND"}, {"strikers_donkeykong", "suite_dk", "DK ISLAND"},
      {"strikers_wario", "suite_wario", "DIAMOND CITY"}, {"strikers_waluigi", "suite_waluigi", "WALUIGI PINBALL"},
      {"strikers_toad", "suite_peach", "TOAD TOWN"}, {"strikers_koopa", "suite_bowser", "BOWSER'S CASTLE"},
      {"strikers_hammerbro", "suite_bowser", "BOWSER'S CASTLE"}, {"strikers_birdo", "suite_yoshi", "YOSHI'S ISLAND"},
      {"bowser", "suite_bowser", "BOWSER'S CASTLE"}, {"bowserjr", "suite_bowser", "BOWSER'S CASTLE"},
      {"diddykong", "suite_dk", "DK ISLAND"}, {"petey", "suite_mario", "ISLE DELFINO"}, {"boo", "suite_luigi", "BOO MANSION"},
      {"drybones", "suite_bowser", "BOWSER'S CASTLE"}, {"montymole", "suite_mario", "MUSHROOM KINGDOM"},
      {"shyguy", "suite_yoshi", "YOSHI'S ISLAND"}, {"drmario", "suite_mario", "MUSHROOM KINGDOM"},
      {"falcon", "suite_fzero", "MUTE CITY"}, {"fox", "suite_starfox", "CORNERIA"}, {"falco", "suite_starfox", "CORNERIA"},
      {"link", "suite_zelda", "HYRULE"}, {"younglink", "suite_zelda", "HYRULE"}, {"zelda", "suite_zelda", "HYRULE"},
      {"sheik", "suite_zelda", "HYRULE"}, {"ganondorf", "suite_zelda", "HYRULE"}, {"samus", "suite_metroid", "PLANET ZEBES"},
      {"kirby", "suite_kirby", "DREAM LAND"}, {"ness", "suite_earthbound", "ONETT"}, {"popo", "suite_iceclimber", "ICICLE MOUNTAIN"},
      {"nana", "suite_iceclimber", "ICICLE MOUNTAIN"}, {"marth", "suite_fireemblem", "ALTEA"}, {"roy", "suite_fireemblem", "PHERAE"},
      {"gamewatch", "suite_gamewatch", "SUPERFLAT WORLD"},
      // r183: the ORIGINALS' home turf, the nearest of the 17 settings (home_biome)
      {"orig_quarterbyte", "suite_fzero", "THE NEON ARCADE"}, {"orig_highscore", "suite_fzero", "THE NEON ARCADE"},
      {"orig_gameover", "evo_electric", "THE POWER GRID"},     {"orig_rookie", "suite_peach", "THE CHESS CASTLE"},
      {"orig_gambit", "suite_peach", "THE CHESS CASTLE"},     {"orig_checkmate", "suite_peach", "THE KING'S HALL"},
      {"orig_tatami", "evo_grass", "THE BAMBOO DOJO"},        {"orig_nightstep", "suite_waluigi", "THE ROOFTOPS AT NIGHT"},
      {"orig_overclock", "evo_electric", "THE CYBER DOJO"},   {"orig_tincan", "suite_mario", "THE VILLAGE GREEN"},
      {"orig_ironclad", "suite_peach", "THE KNIGHTS' KEEP"},  {"orig_wyrmguard", "evo_fire", "THE DRAGON'S PEAK"},
      {"orig_graves", "suite_luigi", "THE HOLLOW"},           {"orig_halfmoon", "evo_ghost", "THE HAUNTED WOODS"},
      {"orig_fullmoon", "evo_ghost", "UNDER THE FULL MOON"},  {"orig_luchador", "evo_city", "THE ARENA"},
      {"orig_racedriver", "suite_fzero", "THE SPEEDWAY"},     {"orig_goalie", "suite_iceclimber", "THE ICE RINK"},
      {"orig_mascot", "suite_mario", "THE BALLPARK"},         {"orig_redline", "evo_city", "THE CITY AT NIGHT"}};
  std::string own = c.key;  // r183: an original's uniform at its single's home
  if (own.compare(0, 5, "orig_") == 0 && own.size() > 2 && own[own.size() - 2] == '_') own.resize(own.size() - 2);
  for (const Home& h : kHome)
    if (c.key == h.key || own == h.key) {
      *place = h.place;
      return h.suite;
    }
  if (c.key.compare(0, 4, "mua_") == 0) {
    *place = "NEW YORK CITY";
    return "evo_city";
  }
  static const struct {
    const char* theme;
    const char* place;
  } kEvo[] = {{"sky", "THE HIGH SKIES"}, {"fire", "THE VOLCANO"}, {"water", "THE OPEN SEA"}, {"grass", "THE DEEP FOREST"},
              {"electric", "THE POWER PLANT"}, {"rock", "THE MOUNTAINS"}, {"psychic", "THE STAR TEMPLE"},
              {"ghost", "THE HAUNTED TOWER"}, {"city", "THE BIG CITY"}};
  const std::string th = g_backdrop != nullptr ? g_backdrop(c.key) : std::string("sky");
  *place = "KANTO";
  for (const auto& e : kEvo)
    if (th == e.theme) *place = e.place;
  return "evo_" + th;
}
// its picture for a flat HOMETOWN card's window (the middle of its _outside.jpg, as wide as the window)
ImgP home_picture(const std::string& name) {
  std::vector<uint8_t> eq;
  int ew = 0, eh = 0;
  if (!load_rgba(home_dir() + "/suites/" + name + "_outside.jpg", &eq, &ew, &eh) &&
      !load_rgba(home_dir() + "/suites/evo_sky_outside.jpg", &eq, &ew, &eh))
    return ImgP();
  auto img = std::make_shared<Img>();
  img->w = 686;
  img->h = 370;
  img->rgba = crop(eq, ew, eh, 0.30f, 0.70f, 0.30f, 0.62f, img->w, img->h);
  return img;
}

// r149: a HOMETOWN's diorama (landscape: back wall wide, floor, left, right, sky) from its home's _outside.jpg, as the
// shiny window's evolution room is made (room_pictures); none: an open sky over grass. Thread.
// (r171: its diorama replaces it - home_scene; kept for a look back)
[[maybe_unused]] void home_room_pictures(const Card& c, std::vector<std::vector<uint8_t>>* tex, std::vector<int>* tw, std::vector<int>* th) {
  std::string place;
  const std::string name = home_backdrop(c, &place);
  std::vector<uint8_t> eq;
  int ew = 0, eh = 0;
  auto add = [&](std::vector<uint8_t> px, int w, int h) {
    tex->push_back(std::move(px));
    tw->push_back(w);
    th->push_back(h);
  };
  if (load_rgba(home_dir() + "/suites/" + name + "_outside.jpg", &eq, &ew, &eh) ||
      load_rgba(home_dir() + "/suites/evo_sky_outside.jpg", &eq, &ew, &eh)) {
    add(crop(eq, ew, eh, 0.30f, 0.70f, 0.30f, 0.62f, 1024, 512), 1024, 512);  // ahead
    add(crop(eq, ew, eh, 0.30f, 0.70f, 0.62f, 0.86f, 1024, 256), 1024, 256);  // the ground
    add(crop(eq, ew, eh, 0.18f, 0.30f, 0.30f, 0.62f, 256, 512), 256, 512);    // to the left
    add(crop(eq, ew, eh, 0.70f, 0.82f, 0.30f, 0.62f, 256, 512), 256, 512);    // to the right
    add(crop(eq, ew, eh, 0.30f, 0.70f, 0.08f, 0.30f, 1024, 256), 1024, 256);  // the sky
    return;
  }
  Pc S2, G;
  S2.box(0, 0, 512, 512, 0, 0x8AC8F4, 1.0f, 0x3A7AC8);
  G.box(0, 0, 512, 256, 0, 0x4AA04A, 1.0f, 0x2A6A2A);
  for (int k = 0; k < 5; ++k) add(board_px(512, k == 1 ? 256 : 512, k == 1 ? G : S2), 512, k == 1 ? 256 : 512);
}
// r149: a TIZM BOOM's layers popping out of the card (thread): a ring of comic burst shards, three words in jagged
// bubbles (its power's: boom_words). (r150: no cut-out of the character - it stays printed on the card's clean face)
// r166: a word hand-lettered (each letter its own small tilt / size / lift, a hair different each `boil` frame) as a
// coverage mask (w x h), fitted to `room` px across at most `px` tall, centred at (cx, cy); and its dark extrude (down right)
void pp_letters(int w, int h, const std::string& word, float cx, float cy, float px, float room, uint32_t seed, int boil,
                std::vector<float>* letters, std::vector<float>* extrude) {
  Pc L;
  std::vector<float> wd;
  float tot = 0.0f;
  for (char ch : word) wd.push_back(L.width(std::string(1, ch), px) + 4.0f), tot += wd.back();
  const float k = tot > room ? room / tot : 1.0f;
  float x = cx - 0.5f * tot * k;
  for (size_t i = 0; i < word.size(); ++i) {
    const uint32_t s = seed * 7919u + static_cast<uint32_t>(i) * 104729u, b = static_cast<uint32_t>(boil) * 31337u;
    const float rot = (pp_h01(s) - 0.5f) * 12.0f + (pp_h01(s + b + 1) - 0.5f) * 2.0f;
    const float dy = (pp_h01(s + 2) - 0.5f) * 0.1f * px * k + (pp_h01(s + b + 3) - 0.5f) * 1.5f;
    const float sc = 1.0f + (pp_h01(s + 4) - 0.5f) * 0.14f;
    L.style(0.0f, 1.0f, rot);
    L.text(std::string(1, word[i]), x + 0.5f * wd[i] * k, cy + dy, px * k * sc, wd[i] * k * 1.6f, 0xFFFFFF);
    x += wd[i] * k;
  }
  const std::vector<uint8_t> b = board_px(w, h, L);
  letters->assign(static_cast<size_t>(w) * h, 0.0f);
  for (size_t i = 0; i < letters->size(); ++i) (*letters)[i] = b[i * 4 + 3] / 255.0f;
  extrude->assign(letters->size(), 0.0f);
  const int depth = std::max(4, static_cast<int>(0.09f * px * k));
  for (int d = 1; d <= depth; ++d) {
    const std::vector<float> s = pp_shift(*letters, w, h, d, d);
    for (size_t i = 0; i < s.size(); ++i) (*extrude)[i] = std::max((*extrude)[i], s[i]);
  }
}
// r166: curved speed lines swept round a ring (512 x 512; none in its middle - the card in front of it), hand-drawn: each
// line tapered both ends, all bending one way; `seed` a frame of them (two, flickering on the hits like drawn animation)
std::vector<uint8_t> pp_speed_ring(uint32_t seed) {
  const int S = 512;
  PpCanvas C(S, S);
  struct Line {
    float a, ra, rb, wid, bend;
  };
  std::vector<Line> lines;
  for (int i = 0; i < 26; ++i) {
    const uint32_t s = seed * 1000u + static_cast<uint32_t>(i) * 17u;
    lines.push_back({6.2831853f * i / 26.0f + 0.25f * pp_h01(s), 120.0f * (1.0f + 0.25f * pp_h01(s + 1)), 250.0f * (0.78f + 0.2f * pp_h01(s + 2)),
                     6.0f * (0.5f + pp_h01(s + 3)), 0.22f + 0.1f * (pp_h01(s + 4) - 0.5f)});
  }
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
      const float dx = x + 0.5f - 256.0f, dy = y + 0.5f - 256.0f, rho = std::sqrt(dx * dx + dy * dy);
      if (rho < 110.0f || rho > 252.0f) continue;
      const float phi = std::atan2(dy, dx);
      float cov = 0.0f;
      for (const Line& l : lines) {
        if (rho < l.ra || rho > l.rb) continue;
        const float u = (rho - l.ra) / (l.rb - l.ra);
        float da = phi - (l.a + l.bend * u);
        da -= 6.2831853f * std::round(da / 6.2831853f);
        const float ww = l.wid * std::pow(std::sin(3.1415927f * std::min(1.0f, u * 1.15f)), 0.7f) * (1.0f - 0.6f * u);
        cov = std::max(cov, clampf(ww - std::fabs(da) * rho + 0.5f, 0.0f, 1.0f));
      }
      if (cov > 0.0f) C.over(y * S + x, kPpInk, cov);
    }
  return C.bytes();
}
// r166: a ring of round puffs where a word was (256 x 256: cream, inked) - big, or small (it shrinking away)
std::vector<uint8_t> pp_puffs(float r) {
  const int S = 256;
  PpCanvas C(S, S);
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x)
      for (int k = 0; k < 11; ++k) {
        const float a = 6.2831853f * k / 11.0f + 0.3f * pp_h01(static_cast<uint32_t>(k) + 40u);
        const float rr = 92.0f * (0.85f + 0.3f * pp_h01(static_cast<uint32_t>(k) + 60u)), pr = r * (0.75f + 0.5f * pp_h01(static_cast<uint32_t>(k) + 80u));
        const float d = std::hypot(x + 0.5f - (128.0f + rr * std::cos(a)), y + 0.5f - (128.0f + 0.8f * rr * std::sin(a)));
        C.over(y * S + x, kPpInk, clampf(pr + 3.5f - d + 0.5f, 0.0f, 1.0f));
        C.over(y * S + x, kPpStock, clampf(pr - d + 0.5f, 0.0f, 1.0f));
      }
  return C.bytes();
}
// r166: the torn hole in the card's top (256 x 192; the card's top edge at y = kPpEdge: nothing above it) - a wobbly rounded
// rip, dark inside, inked; and its curling paper flaps round it (the same frame: cream, a shaded underside, inked)
constexpr float kPpEdge = 58.0f, kPpHoleW = 0.05f, kPpHoleH = 0.0375f;
float pp_rip(float th) { return 1.0f + 0.07f * std::sin(7.0f * th + 1.3f) + 0.05f * std::sin(13.0f * th + 4.1f) + 0.03f * std::sin(29.0f * th + 2.2f); }
std::vector<uint8_t> pp_hole() {
  const int W = 256, H = 192;
  PpCanvas C(W, H);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      if (y + 0.5f < kPpEdge) continue;
      const float dx = (x + 0.5f - 128.0f) / 100.0f, dy = (y + 0.5f - kPpEdge) / 100.0f, d = std::sqrt(dx * dx + dy * dy);
      const float R = pp_rip(std::atan2(dy, dx));
      C.over(y * W + x, kPpInk, clampf((R - d) * 100.0f + 5.0f, 0.0f, 1.0f));
      const float in = clampf((R - d) * 100.0f + 0.5f, 0.0f, 1.0f);
      C.over(y * W + x, mix_rgb(kPpHole, 0x14121E, clampf((y - kPpEdge) / 90.0f, 0.0f, 1.0f)), in);
    }
  return C.bytes();
}
std::vector<uint8_t> pp_flaps() {
  const int W = 256, H = 192;
  PpCanvas C(W, H);
  static const float kAng[5] = {0.12f, 0.75f, 1.55f, 2.35f, 3.0f}, kLen[5] = {44, 36, 32, 38, 46};
  for (int f = 0; f < 5; ++f) {
    const float a = kAng[f], R = 100.0f * pp_rip(a) * 0.93f;
    const float bx = 128.0f + R * std::cos(a), by = kPpEdge + R * std::sin(a);
    // its axis: out from the rim and up (curling toward you), bending as it goes
    const float ax = std::cos(a - 0.5f), ay = std::sin(a - 0.5f) - 0.55f, al = std::sqrt(ax * ax + ay * ay);
    const float ux = ax / al, uy = ay / al, nx = -uy, ny = ux;
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        const float px = x + 0.5f - bx, py = y + 0.5f - by;
        const float s = px * ux + py * uy, t = px * nx + py * ny - 0.006f * s * s;
        const float L = kLen[f];
        if (s < -16.0f || s > L + 8.0f) continue;
        const float wv = 17.0f * std::sqrt(std::max(0.0f, 1.0f - std::pow(clampf(s / L, 0.0f, 1.0f), 2.0f))) * (s < 0.0f ? 1.0f : 1.0f);
        const float in = clampf(wv - std::fabs(t) + 0.5f, 0.0f, 1.0f) * clampf(s + 16.0f, 0.0f, 1.0f);
        const float rim = clampf(wv + 3.5f - std::fabs(t) + 0.5f, 0.0f, 1.0f) * clampf(s + 18.0f, 0.0f, 1.0f) * (s <= L + 3.5f ? 1.0f : 0.0f);
        C.over(y * W + x, kPpInk, rim);
        C.over(y * W + x, t > 0.0f ? 0xD6C8AA : 0xEEE1C6, in);  // (its underside a shade darker)
      }
  }
  return C.bytes();
}
// r166: the character leaping out (512 x 512): its own picture (card_art), its feet on the bottom, an ink line round it,
// magenta Ben-Day dots on its shadow side; an old photo framed white and inked
std::vector<uint8_t> pp_hero(const ImgP& art) {
  const int S = 512;
  PpCanvas C(S, S);
  if (!art || art->empty()) return C.bytes();
  int bx0 = art->w, by0 = art->h, bx1 = -1, by1 = -1;
  const bool photo = opaque_art(art);
  for (int y = 0; y < art->h; ++y)
    for (int x = 0; x < art->w; ++x)
      if (photo || art->rgba[(static_cast<size_t>(y) * art->w + x) * 4 + 3] > 40)
        bx0 = std::min(bx0, x), by0 = std::min(by0, y), bx1 = std::max(bx1, x), by1 = std::max(by1, y);
  if (bx1 < bx0) return C.bytes();
  const float bw = static_cast<float>(bx1 - bx0 + 1), bh = static_cast<float>(by1 - by0 + 1);
  const float k = std::min((photo ? 400.0f : 470.0f) / bw, (photo ? 400.0f : 480.0f) / bh);
  const float ox = 256.0f - 0.5f * bw * k, oy = (photo ? 470.0f : 500.0f) - bh * k;
  std::vector<float> a(static_cast<size_t>(S) * S, 0.0f);
  std::vector<uint32_t> col(a.size(), 0);
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {  // (bilinear)
      const float sx = (x + 0.5f - ox) / k + bx0 - 0.5f, sy = (y + 0.5f - oy) / k + by0 - 0.5f;
      if (sx < bx0 - 1 || sy < by0 - 1 || sx > bx1 + 1 || sy > by1 + 1) continue;
      const int x0 = std::max(0, std::min(art->w - 1, static_cast<int>(std::floor(sx)))), y0 = std::max(0, std::min(art->h - 1, static_cast<int>(std::floor(sy))));
      const int x1 = std::min(art->w - 1, x0 + 1), y1 = std::min(art->h - 1, y0 + 1);
      const float fx = clampf(sx - x0, 0.0f, 1.0f), fy = clampf(sy - y0, 0.0f, 1.0f);
      float c4[4] = {0, 0, 0, 0};
      const int xs[4] = {x0, x1, x0, x1}, ys[4] = {y0, y0, y1, y1};
      const float ws[4] = {(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
      for (int q = 0; q < 4; ++q) {
        const uint8_t* p = &art->rgba[(static_cast<size_t>(ys[q]) * art->w + xs[q]) * 4];
        const float pa = photo ? 1.0f : p[3] / 255.0f;
        for (int ch = 0; ch < 3; ++ch) c4[ch] += p[ch] * pa * ws[q];
        c4[3] += pa * ws[q];
      }
      const size_t i = static_cast<size_t>(y) * S + x;
      a[i] = c4[3];
      if (c4[3] > 1e-3f)
        col[i] = (static_cast<uint32_t>(c4[0] / c4[3]) << 16) | (static_cast<uint32_t>(c4[1] / c4[3]) << 8) | static_cast<uint32_t>(c4[2] / c4[3]);
    }
  if (photo) {  // (a photo: a white frame, inked)
    std::vector<float> fr(a.size());
    for (int y = 0; y < S; ++y)
      for (int x = 0; x < S; ++x) fr[static_cast<size_t>(y) * S + x] = pp_rbox(x + 0.5f, y + 0.5f, ox - 18, oy - 18, ox + bw * k + 18, oy + bh * k + 18, 16);
    C.layer(pp_dilate(fr, S, S, 6.0f), kPpInk);
    C.layer(fr, 0xFFFFFF);
  } else {
    C.layer(pp_dilate(a, S, S, 7.0f), kPpInk);
  }
  const float midX = 256.0f + 0.08f * bw * k;
  for (int y = 0; y < S; ++y)
    for (int x = 0; x < S; ++x) {
      const size_t i = static_cast<size_t>(y) * S + x;
      if (a[i] <= 0.0f) continue;
      C.over(static_cast<int>(i), col[i], a[i]);
      const float shade = clampf((x - midX) / (0.3f * bw * k), 0.0f, 1.0f) * 0.85f;
      C.over(static_cast<int>(i), 0x3C1446, a[i] * 0.7f * pp_dot(x + 0.5f, y + 0.5f, 9.0f, 0.79f, shade));
    }
  return C.bytes();
}
// r166: a TIZM BOOM's pop-out show's pictures, in kBoomTex order: two frames of its speed lines; its three words (its
// power's: boom_words) each in its cloud balloon, three frames each (their lines boiling); its puffs (big, small); the torn
// hole in its top, the character leaping out of it, the hole's paper flaps
// (r150's burst shards and jagged word bubbles gone: "too polygonal")
constexpr int kBoomTex = 17, kBoomQuads = 21;
void boom_layers(const Card& c, const Ref& r, std::vector<std::vector<uint8_t>>* tex, std::vector<int>* tw, std::vector<int>* th) {
  auto add = [&](std::vector<uint8_t> px, int w, int h) {
    tex->push_back(std::move(px));
    tw->push_back(w);
    th->push_back(h);
  };
  add(pp_speed_ring(1), 512, 512);
  add(pp_speed_ring(2), 512, 512);
  std::string words[3];
  boom_words(c.key, words);
  // KAPOW!-yellow with red letters and red dots; white with blue letters and cyan dots; pink with yellow letters and magenta
  static const uint32_t kBubble[3] = {kPpYel, 0xFFFCF0, 0xF05A8C}, kDots[3] = {0xEB462D, 0x3CAAE1, 0xE13C78},
                        kFill[3] = {kPpRed, 0x285AC8, 0xFAD728};
  static const int kLobes[3] = {12, 9, 9};
  for (int j = 0; j < 3; ++j)
    for (int v = 0; v < 3; ++v) {
      const int W = 512, H = 256;
      std::vector<float> letters, extrude;
      pp_letters(W, H, words[j], 0.5f * W - 5.0f, 0.5f * H - 6.0f, 104.0f, 300.0f, 3u + 4u * j, v, &letters, &extrude);
      add(pp_balloon(W, H, 180.0f, 80.0f, kLobes[j], 3u + 4u * j, v, kBubble[j], kDots[j], 10.0f, &letters, &extrude, kFill[j]), W, H);
    }
  add(pp_puffs(17.0f), 256, 256);
  add(pp_puffs(8.0f), 256, 256);
  add(pp_hole(), 256, 192);
  add(pp_hero(card_art(c, r.shiny, 3.0f)), 512, 512);
  add(pp_flaps(), 256, 192);
  // its panel left empty (the page's panel, no one in it: while the character is out of the card)
  const ImgP pg = pp_page();
  std::vector<uint8_t> e(512 * 512 * 4);
  for (int y = 0; y < 512; ++y)
    for (int x = 0; x < 512; ++x) {
      const int sx = static_cast<int>(kHoleX0 + (x + 0.5f) * (kHoleX1 - kHoleX0) / 512.0f);
      const int sy = static_cast<int>(kHoleY0 + (y + 0.5f) * (kHoleY1 - kHoleY0) / 512.0f);
      std::memcpy(&e[(static_cast<size_t>(y) * 512 + x) * 4], &pg->rgba[(static_cast<size_t>(sy) * pg->w + sx) * 4], 4);
    }
  add(std::move(e), 512, 512);
}
// r166: its quads (boom_place) on its pictures: 0-1 the speed lines' two frames; 2-10 word j's frame v (2 + 3j + v); 11-16
// word j's puffs big / small (11 + 2j + s); 17 the hole, 18 the character, 19 the flaps, 20 its panel left empty
int boom_tex_of(int quad) {
  if (quad <= 10) return quad;
  if (quad <= 16) return 11 + (quad - 11) % 2;
  return quad - 4;
}

// r149 (Lance's SIGNED case hit, and every BLACK 1/1): its own autograph (xr_sign.cpp: its strokes over time), across the
// card - flat: drawn whole into its front; on show: a soft part writing itself (sign_strip: its ink, and when each pixel
// of it is written). Its box 2 : 1 (xrsign::kBoxAspect), on the 1080 x 1512 front across its picture, low.
constexpr int kSignW = 840, kSignH = 420;
constexpr float kSignX0 = 120, kSignY0 = 560;
#if __has_include("xr_sign.h")
// its ink's colour (the one most of its strokes are in) for the shader, in its normals: y = red + green / 256, z = 3 +
// blue / 256 (xr_fx_model.frag; 0 = its gold)
void sign_ink(const Card& c, float* y, float* z) {
  const xrsign::Signature& s = xrsign::get(c.key, c.signAs);
  std::unordered_map<uint32_t, size_t> used;
  uint32_t best = 0;
  size_t most = 0;
  for (const xrsign::Stroke& k : s.strokes)
    if ((used[k.rgba >> 8] += k.pts.size()) > most) most = used[k.rgba >> 8], best = k.rgba >> 8;
  *y = static_cast<float>((best >> 16) & 255) + static_cast<float>((best >> 8) & 255) / 256.0f;
  *z = 3.0f + static_cast<float>(best & 255) / 256.0f;
}
void bake_signature(std::vector<uint8_t>& px, int w, int h, const Card& c, float foot) {  // r217: on its set's own place (lk::bake_sign)
  lk::bake_sign(px, w, h, c, c.set, foot);  // (r220: up a Jam die's foot)
}
// its autograph as it writes: R its glow, G its ink, B when it's written there (0..1 of its time), A how much shows
std::vector<uint8_t> sign_strip(const Card& c) {
  const xrsign::Signature& s = xrsign::get(c.key, c.signAs);
  const int n = 48;
  std::vector<uint8_t> out(static_cast<size_t>(kSignW) * kSignH * 4, 0), step(out.size());
  std::vector<float> when(static_cast<size_t>(kSignW) * kSignH, -1.0f);
  for (int k = 1; k <= n; ++k) {
    std::fill(step.begin(), step.end(), 0);
    xrsign::draw(s, s.duration * static_cast<float>(k) / n, step.data(), kSignW, kSignH, kSignW * 4);
    for (size_t i = 0; i < when.size(); ++i)
      if (when[i] < 0.0f && step[i * 4 + 3] > 40) when[i] = static_cast<float>(k) / n;
  }
  std::vector<float> core(when.size(), 0.0f);
  for (size_t i = 0; i < when.size(); ++i) core[i] = step[i * 4 + 3] / 255.0f;  // (the whole of it: the last step)
  // its glow: the ink spread 10 px (a box blur twice), its time carried out into it (the nearest written pixel's);
  // r193 (Lance: "the ink needs to SHINE"): and a wide halo round it, 24 px more (the shader's luminous glow)
  std::vector<float> tmp(core.size());
  std::vector<float> tw = when;
  auto blur = [&](std::vector<float>& glow, int R) {
    const float n = 2.0f * R + 1.0f;
    for (int pass = 0; pass < 2; ++pass) {
      for (int y = 0; y < kSignH; ++y)
        for (int x = 0; x < kSignW; ++x) {
          float sum = 0.0f, tsum = 0.0f, tn = 0.0f;
          for (int d = -R; d <= R; ++d) {
            const int xx = std::max(0, std::min(kSignW - 1, x + d));
            sum += glow[static_cast<size_t>(y) * kSignW + xx];
            const float tv = tw[static_cast<size_t>(y) * kSignW + xx];
            if (tv >= 0.0f) tsum += tv, tn += 1.0f;
          }
          tmp[static_cast<size_t>(y) * kSignW + x] = sum / n;
          if (tw[static_cast<size_t>(y) * kSignW + x] < 0.0f && tn > 0.0f) tw[static_cast<size_t>(y) * kSignW + x] = tsum / tn;
        }
      for (int y = 0; y < kSignH; ++y)
        for (int x = 0; x < kSignW; ++x) {
          float sum = 0.0f, tsum = 0.0f, tn = 0.0f;
          for (int d = -R; d <= R; ++d) {
            const int yy = std::max(0, std::min(kSignH - 1, y + d));
            sum += tmp[static_cast<size_t>(yy) * kSignW + x];
            const float tv = tw[static_cast<size_t>(yy) * kSignW + x];
            if (tv >= 0.0f) tsum += tv, tn += 1.0f;
          }
          glow[static_cast<size_t>(y) * kSignW + x] = sum / n;
          if (tw[static_cast<size_t>(y) * kSignW + x] < 0.0f && tn > 0.0f) tw[static_cast<size_t>(y) * kSignW + x] = tsum / tn;
        }
    }
  };
  std::vector<float> glow = core;
  blur(glow, 10);
  std::vector<float> wide = glow;
  blur(wide, 24);
  for (size_t i = 0; i < when.size(); ++i) {
    const float g = clampf(std::max(glow[i] * 2.2f, wide[i] * 3.2f), 0.0f, 1.0f), a = std::max(g, core[i]);
    out[i * 4 + 0] = static_cast<uint8_t>(std::lround(g * 255.0f));
    out[i * 4 + 1] = static_cast<uint8_t>(std::lround(core[i] * 255.0f));
    out[i * 4 + 2] = static_cast<uint8_t>(std::lround(clampf(tw[i] < 0.0f ? 1.0f : tw[i], 0.0f, 1.0f) * 255.0f));
    out[i * 4 + 3] = static_cast<uint8_t>(std::lround(a * 255.0f));
  }
  return out;
}
#endif

// r141 (coordinator: every guest's "OWN celebration on a loop ... the way the game itself animates them in matches"): a
// retarget guest's (a Pokemon's, a Melee fighter's) celebration - its Strikers class's goal cut-scene through the game's
// retarget, the match's flier / floater / slither / spin rules, on the spot and on the floor - made by the converter's
// Java (guests/render/Cel.java) through JNI the first time a BLACK 1/1 of it needs it, into guests/cel/<pack>.gev (GEV1 +
// SEG1, as a home figure's): the same code and the same bytes on every headset, Lance's and the public build's
// (tools/guestconv CelTest on the PC). One at a time; a few hundredths of a second each on the PC.
struct CelJni {
  bool tried = false;
  jclass cls = nullptr;
  jmethodID bake = nullptr;
};
CelJni g_celJni;
std::mutex g_celMutex;
bool cel_jni_clear(JNIEnv* env) {
  if (!env->ExceptionCheck()) return false;
  env->ExceptionDescribe();
  env->ExceptionClear();
  return true;
}
JNIEnv* cel_env() {  // (g_celMutex held) xr_big.cpp's jni_env: this thread's, the class through the activity's loader
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr || g_celJni.tried) return g_celJni.cls != nullptr ? env : nullptr;
  g_celJni.tried = true;
  auto* act = static_cast<jobject>(SDL_GetAndroidActivity());
  if (act == nullptr) {
    cel_jni_clear(env);
    CLOG("no activity: no celebrations made");
    return nullptr;
  }
  jclass actCls = env->GetObjectClass(act);
  jmethodID getLoader = env->GetMethodID(actCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject loader = getLoader != nullptr && !cel_jni_clear(env) ? env->CallObjectMethod(act, getLoader) : nullptr;
  cel_jni_clear(env);
  jclass loaderCls = env->FindClass("java/lang/ClassLoader");
  jmethodID loadClass = loaderCls != nullptr ? env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
  cel_jni_clear(env);
  jstring name = env->NewStringUTF("io.github.newcoke.strikers.guests.render.Cel");
  jobject cls = loader != nullptr && loadClass != nullptr ? env->CallObjectMethod(loader, loadClass, name) : nullptr;
  cel_jni_clear(env);
  if (cls != nullptr) {
    g_celJni.cls = static_cast<jclass>(env->NewGlobalRef(cls));
    // Cel.bake(gpk, out, home, data): 1 made, 0 none to make, -1 failed
    g_celJni.bake = env->GetStaticMethodID(g_celJni.cls, "bake",
                                           "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)I");
    if (cel_jni_clear(env) || g_celJni.bake == nullptr) {
      env->DeleteGlobalRef(g_celJni.cls);
      g_celJni.cls = nullptr;
    }
  }
  for (jobject o : {static_cast<jobject>(act), static_cast<jobject>(actCls), loader, static_cast<jobject>(loaderCls),
                    static_cast<jobject>(name), cls})
    if (o != nullptr) env->DeleteLocalRef(o);
  if (g_celJni.cls == nullptr) CLOG("the celebrations' maker (guests.render.Cel) not found: guests idle on their thrones");
  return g_celJni.cls != nullptr ? env : nullptr;
}
int cel_bake(const std::string& gpk, const std::string& out) {
  std::lock_guard<std::mutex> lock(g_celMutex);
  JNIEnv* env = cel_env();
  if (env == nullptr) return -1;
  if (env->PushLocalFrame(8) != 0) {
    cel_jni_clear(env);
    return -1;
  }
  const char* data = std::getenv("STRIKERS_DATA");
  jstring a = env->NewStringUTF(gpk.c_str()), b = env->NewStringUTF(out.c_str()), c = env->NewStringUTF(home_dir().c_str());
  jstring d = env->NewStringUTF(data != nullptr ? data : "");
  jint r = env->CallStaticIntMethod(g_celJni.cls, g_celJni.bake, a, b, c, d);
  if (cel_jni_clear(env)) r = -1;
  env->PopLocalFrame(nullptr);
  return static_cast<int>(r);
}
bool file_there(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}
// r193: a celebration file there and whole (none being made this moment: cel_bake holds g_celMutex while it writes);
// `wait`: wait for one being made, else a file being made counts as not there
bool cel_ready(const std::string& gev, bool wait) {
  if (wait) {
    std::lock_guard<std::mutex> lock(g_celMutex);
    return file_there(gev);
  }
  std::unique_lock<std::mutex> lock(g_celMutex, std::try_to_lock);
  return lock.owns_lock() && file_there(gev);
}
// r193 (the SILVER Croconaw's celebration was made 1.4 s AFTER its card was up: it stood in its raw standing file, then
// swapped): every card figure's celebration in a pack made as the pack comes up, one after another on a thread of its
// own (as its pictures are fetched ahead), so it's there by the time its card's turned. The same files load_figure
// would make (a guest's guests/cel/<key>.gev from its pack - its shiny pack for a shiny); none for a Strikers character /
// a guest with its own home figure (a plain one).
void cel_prefetch(const std::vector<std::pair<Card, bool>>& cards) {
  if (cards.empty()) return;
  std::thread([cards]() {
    const std::string g = home_dir() + "/guests/";
    for (const auto& cs : cards) {
      const Card& c = cs.first;
      const bool shiny = cs.second;
      if (c.key.compare(0, 9, "strikers_") == 0) continue;
      if (!shiny && file_there(g + "home/" + c.key + ".gpk") && file_there(g + "home/" + c.key + ".gev")) continue;
      const std::string gpk = g + c.key + (shiny ? "_shiny" : "") + ".gpk", gev = g + "cel/" + c.key + ".gev";
      if (!file_there(gpk) || cel_ready(gev, true)) continue;
      const uint64_t t0 = mono_ns();
      const int r = cel_bake(gpk, gev);
      CLOG("pack: %s's celebration made ahead: %s (%.0f ms)", c.key.c_str(), r == 1 ? "made" : (r == 0 ? "none to make" : "FAILED"),
           secs(mono_ns(), t0) * 1000.0f);
    }
  }).detach();
}

// Its figure's model and standing loop (xr_big's sources: a Strikers character's captain / home figure; a guest's pack -
// a shiny one's OWN _shiny pack first - with its pose file), else none. Thread.
// r141 `home`: a home figure (its own moves: a celebration to loop on a BLACK 1/1's throne) first, when there is one;
// else a retarget guest's celebration (guests/cel/, made now the first time)
// r181: `later` (non-null): a celebration not baked yet is NOT baked here (that held a card's first reveal 1.1-1.7 s) -
// its pack / pose file to make are handed back (the special's thread bakes it after the card is up and swaps it in)
xrevo::Figure* load_figure(const Card& c, bool shiny, xrfx::Model* m, bool home = false,
                           std::pair<std::string, std::string>* later = nullptr) {
  const std::string g = home_dir() + "/guests/";
  std::vector<std::pair<std::string, std::string>> tries;
  if (c.key.compare(0, 9, "strikers_") == 0) {
    const std::string n = c.key.substr(9);
    if (home) tries.push_back({g + "home/strikers_" + n + ".gpk", g + "home/strikers_" + n + ".gev"});
    tries.push_back({g + "cap_" + n + ".gpk", g + "evo/cap_" + n + ".gev"});
    tries.push_back({g + "home/strikers_" + n + ".gpk", g + "home/strikers_" + n + ".gev"});
  } else {
    // r183: an ORIGINAL's standing loop (guests/evo/<key>.gev: its class's idle through the retarget, made on this
    // headset from the game's own files - nothing of the game ships; StrikersHome makes them at start, this if not yet)
    if (PortOriginalKey(c.key.c_str()) && file_there(g + c.key + ".gpk") && !file_there(g + "evo/" + c.key + ".gev")) {
      const int r = cel_bake(g + c.key + ".gpk", g + "evo/" + c.key + ".gev");
      CLOG("special: %s's standing loop %s", c.key.c_str(), r == 1 ? "made" : (r == 0 ? "none to make" : "FAILED"));
    }
    const bool homeFig = file_there(g + "home/" + c.key + ".gpk") && file_there(g + "home/" + c.key + ".gev");
    if (home && !shiny) tries.push_back({g + "home/" + c.key + ".gpk", g + "home/" + c.key + ".gev"});
    if (home && (shiny || !homeFig)) {
      const std::string gpk = g + c.key + (shiny ? "_shiny" : "") + ".gpk", gev = g + "cel/" + c.key + ".gev";
      // (r193: a celebration being made ahead right now - cel_prefetch - counts as not there yet: never read half made)
      if (file_there(gpk) && !cel_ready(gev, later == nullptr) && later != nullptr) {
        *later = {gpk, gev};  // r181: made after the card is up (its standing figure meanwhile: the evo try below)
      } else if (file_there(gpk)) {
        if (!cel_ready(gev, true)) {
          const uint64_t t0 = mono_ns();
          const int r = cel_bake(gpk, gev);
          CLOG("special: %s's celebration %s (%.0f ms)", c.key.c_str(), r == 1 ? "made" : (r == 0 ? "none to make" : "FAILED"),
               secs(mono_ns(), t0) * 1000.0f);
        }
        tries.push_back({gpk, gev});
      }
    }
    if (shiny) tries.push_back({g + c.key + "_shiny.gpk", g + "evo/" + c.key + ".gev"});
    tries.push_back({g + c.key + ".gpk", g + "evo/" + c.key + ".gev"});
    tries.push_back({g + "home/" + c.key + ".gpk", g + "home/" + c.key + ".gev"});
    tries.push_back({g + c.key + ".gpk", g + "home/" + c.key + ".gev"});
  }
  for (const auto& t : tries) {
    FILE* a = std::fopen(t.first.c_str(), "rb");
    FILE* b = a != nullptr ? std::fopen(t.second.c_str(), "rb") : nullptr;
    if (a != nullptr) std::fclose(a);
    if (b == nullptr) continue;
    std::fclose(b);
    *m = xrfx::Model{};
    if (xrevo::Figure* f = xrevo::load_files(t.first, t.second, m)) {
      CLOG("special: %s's figure %s", c.key.c_str(), t.first.c_str());
      return f;
    }
  }
  return nullptr;
}

void special_start(Special& sp, int i, uint64_t now) {
  const Ref r = S.refs[i];
  Card c;
  int total = 0, level = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    c = g_set[r.card];
    total = g_setN;  // (r217: a set's count)
    level = card_level(g_owned[r.card]);  // r146: its stars
  }
  sp = Special{};
  sp.i = i;
  sp.kind = special_kind(r);
  sp.shiny = r.shiny;
  sp.spotUntil = sp.kind == kSpGlass && S.mode == kOut ? now + 4500000000ull : 0;  // (out of the binder: lit at once)
  sp.theme = sp.kind == kSpGlass ? fx_theme_of(c.key) : -1;  // r141: a BLACK 1/1's world
  sp.set = c.set;  // r217
  sp.shape = c.set == 1 ? lk::jam_shape(r) : -1;  // r220
  sp.foot = c.set == 1 ? jam_foot(r) : 0.0f;
  sp.roster = c.roster;
  sp.klass = c.klass;
  sp.rgb = c.c1;  // r147
  // r149: a HOMETOWN's landscape diorama; an autograph (a SIGNED case hit, every BLACK 1/1) - writing itself on as it's
  // first turned over in a pack, out of the binder already written
  sp.home = r.hit == kHitHome;
  sp.signedCard = r.hit == kHitSigned || r.parallel == kBlack;
  sp.signDone = S.mode != kPackUp;
#if __has_include("xr_sign.h")
  if (sp.signedCard) sp.sig = &xrsign::get(c.key, c.signAs);  // r193 (its pen tip, sign_moment)
#endif
  auto b = std::make_shared<SpecialBuild>();
  sp.build = b;
  const int kind = sp.kind;
  std::thread([b, c, r, total, kind, level]() {
    std::vector<std::vector<uint8_t>> tex;
    std::vector<int> tw, th;
    // (r153: a GOLD's front its window cut through, as a SILVER's - its solid face with its picture over it, below)
    tex.push_back(draw_front(c, r, total, 0.0f, kind == kSpPop ? kSpWindow : kind, level));
    tw.push_back(kFrontW);
    th.push_back(kFrontH);
    tex.push_back(draw_back(c, total));  // (r147: plain - its level only after it's turned over)
    tw.push_back(kBackW);
    th.push_back(kBackH);
    const FinishLook& fl = kFinishLook[r.parallel];
    std::vector<uint8_t> e(4 * 4 * 4);
    for (int p = 0; p < 16; ++p) {
      e[p * 4 + 0] = (fl.edge >> 16) & 255;
      e[p * 4 + 1] = (fl.edge >> 8) & 255;
      e[p * 4 + 2] = fl.edge & 255;
      e[p * 4 + 3] = static_cast<uint8_t>(255 - std::lround(fl.edgeFoil * 127.0f));
    }
    tex.push_back(std::move(e));
    tw.push_back(4);
    th.push_back(4);
    if (r.hit == kHitHome) {  // r171: its diorama's pictures here (made with it once its figure's fitted, below)
      for (int k = 0; k < kHpCount; ++k) {
        tex.emplace_back(4 * 4 * 4, 255);
        tw.push_back(4);
        th.push_back(4);
      }
    }
    else if (kind == kSpWindow || kind == kSpGlass || kind == kSpPop) room_pictures(c, kind, r.shiny, &tex, &tw, &th);
    int solidTex = -1;
    if (kind == kSpPop) {  // r153: its solid face - today's GOLD front (r147: its picture, its big one)
      solidTex = static_cast<int>(tex.size());
      tex.push_back(draw_front(c, r, total, 3.0f, kSpPop, level));
      tw.push_back(kFrontW);
      th.push_back(kFrontH);
    }
    if (kind == kSpGlass) {  // the slab's face: dark violet glass, two streaks of light across it
      Pc G;
      // r138 (coordinator: polished obsidian, the figure crisp through it): a light black tint, two thin sharp streaks
      // and a broad faint sheen (the shader adds the real reflections: a crisp highlight, its polished edges)
      G.box(0, 0, 256, 256, 0, 0x050309, 0.16f);
      G.poly({0, 58, 256, -6, 256, 24, 0, 108}, 0xFFFFFF, 0.05f);
      G.poly({0, 74, 256, 10, 256, 15, 0, 79}, 0xFFFFFF, 0.42f);
      G.poly({0, 86, 256, 22, 256, 24, 0, 88}, 0xFFFFFF, 0.30f);
      G.poly({0, 196, 256, 132, 256, 134, 0, 198}, 0xFFFFFF, 0.18f);
      std::vector<uint8_t> px(256 * 256 * 4);
      xrtext::board(px.data(), 256, 256, G.v);  // (premultiplied: the shader's soft parts take it as it is)
      for (size_t k = 0; k < px.size(); k += 4)
        if (px[k + 3] > 0)
          for (int ch = 0; ch < 3; ++ch) px[k + ch] = static_cast<uint8_t>(std::min(255, px[k + ch] * 255 / px[k + 3]));
      tex.push_back(std::move(px));
      tw.push_back(256);
      th.push_back(256);
      // r141: its throne's pedestal - black glass, a violet line of light round its top, a faint sheen across
      Pc T;
      T.box(0, 0, 128, 128, 0, 0x0E0B16, 1.0f, 0x030206);
      T.box(0, 0, 128, 7, 0, 0x8A50F0, 0.9f);
      T.box(0, 7, 128, 12, 0, 0x4A2296, 0.5f);
      T.poly({0, 70, 128, 34, 128, 42, 0, 78}, 0xFFFFFF, 0.07f);
      T.box(0, 122, 128, 128, 0, 0x2A1450, 0.8f);
      tex.push_back(board_px(128, 128, T));
      tw.push_back(128);
      th.push_back(128);
      // r147 (Lance: "they need a static image of the character on the card AS WELL AS the 3D body on top of the card";
      // "a damn kaleidoscope dream inside showcasing and spotlighting the character within the stone glass"): its picture
      // for inside the stone (its own see-through: cut out, fitted; an old photo: framed)
      const ImgP art = card_art(c, r.shiny, 3.0f);
      Pc A;
      if (art && !art->empty()) {
        if (!opaque_art(art)) {
          A.centred(art, 6, 6, 506, 506, 1.0f);
        } else {
          A.box(40, 40, 472, 472, 26, 0xFFFFFF, 1.0f, 0xE4E6EA);
          A.image(art, 60, 60, 452, 452, false);
        }
      }
      tex.push_back(board_px(512, 512, A));
      tw.push_back(512);
      th.push_back(512);
    }
    int layerTex = -1, layers = 0, signTex = -1;
    float signDur = 2.5f, signY = 0.0f, signZ = 3.0f;
    if (kind == kSpBoom) {  // r149: TIZM BOOM's layers popping out of the card
      layerTex = static_cast<int>(tex.size());
      boom_layers(c, r, &tex, &tw, &th);
      layers = static_cast<int>(tex.size()) - layerTex;
    }
#if __has_include("xr_sign.h")
    if (r.hit == kHitSigned || r.parallel == kBlack) {  // r149: its autograph, to write itself on
      signTex = static_cast<int>(tex.size());
      signDur = std::max(1.0f, std::min(4.0f, xrsign::get(c.key, c.signAs).duration));
      sign_ink(c, &signY, &signZ);
      tex.push_back(sign_strip(c));
      tw.push_back(kSignW);
      th.push_back(kSignH);
    }
#endif
    xrfx::Model fig;
    // (r149: a TIZM BOOM / a SIGNED BASE: no figure - its picture on its front)
    // (r141: the 1/1's own celebration; r173 (Lance: "SOME characters do a little movement/celebration move when on 3D
    // display in their card and others dont"): EVERY card's figure from its living files - its home figure / its
    // celebration (cel bake) with their moves - not only the 1/1's; a SILVER / GOLD / HOMETOWN's was its select-screen
    // standing idle (a breath, no more))
    // (r181: but not waited for - a celebration not made yet is made after the card is up, its standing figure meanwhile;
    // a BLACK 1/1's still at once: its throne / cameo are fitted by its cheer)
    std::pair<std::string, std::string> later;
    xrevo::Figure* f = kind == kSpBoom || kind == kSpSign ? nullptr : load_figure(c, r.shiny, &fig, true, kind == kSpGlass ? nullptr : &later);
    Fit fit, fitTop;
    if (f != nullptr) {
      // r140: measured posed, here on the thread; r149: a HOMETOWN in its landscape window; r153: a GOLD's in its room
      // (fit, as a SILVER's) and on its top edge (fitTop, as before)
      fit = r.hit == kHitHome ? fit_figure(f, kind, kHomeWinHalfX, -kHomeWinHalfY, kHomeWinHalfY)
                              : fit_figure(f, kind == kSpPop ? kSpWindow : kind);
      if (kind == kSpPop) fitTop = fit_figure(f, kSpPop);
      CLOG("special: %s fitted at %.3f, yaw %.2f, feet (%.3f, %.3f, %.3f)%s", c.key.c_str(), fit.scale, fit.yaw, fit.feet.x,
           fit.feet.y, fit.feet.z, fit.plinthHalfW > 0.0f ? ", on a plinth" : "");  // (r181: a real plinth only - it said so for all)
      if (kind == kSpPop)
        CLOG("special: %s on its top edge at %.3f, feet (%.3f, %.3f, %.3f)", c.key.c_str(), fitTop.scale, fitTop.feet.x,
             fitTop.feet.y, fitTop.feet.z);
    }
    int standee = -1;
    if (f == nullptr && kind != kSpBoom && kind != kSpSign) {  // none here: its picture standing in for it
      const ImgP art = card_art(c, r.shiny, 3.0f);
      if (art && !art->empty()) {
        standee = static_cast<int>(tex.size());
        tex.push_back(art->rgba);
        tw.push_back(art->w);
        th.push_back(art->h);
      }
    }
    HomeMesh home;
    if (r.hit == kHitHome) {  // r171: its home as a 3D diorama, its figure standing in it
      std::string place;
      const std::string name = home_backdrop(c, &place);
      home_scene(home, name, static_cast<uint32_t>(std::hash<std::string>()(c.key)), fit, f != nullptr);
      std::vector<std::vector<uint8_t>> ht;
      std::vector<int> hw, hh;
      home_pictures(home, name, &ht, &hw, &hh);
      for (int k = 0; k < kHpCount && k < static_cast<int>(ht.size()); ++k) {
        tex[static_cast<size_t>(3 + k)] = std::move(ht[static_cast<size_t>(k)]);
        tw[static_cast<size_t>(3 + k)] = hw[static_cast<size_t>(k)];
        th[static_cast<size_t>(3 + k)] = hh[static_cast<size_t>(k)];
      }
      CLOG("special: %s's HOMETOWN diorama (%s, biome %d): %zu vertices, %zu colours, %zu particles", c.key.c_str(), name.c_str(),
           home_biome(name), home.v.size() / 6, home.pal.size(), home.fx.size());
    }
    int cutBackTex = -1;  // r220: a JAM '96 die's own back (after its punch / out of the binder), last
    if (c.set == 1 && lk::jam_shape(r) > 0) {
      cutBackTex = static_cast<int>(tex.size());
      tex.push_back(draw_back(c, total, lk::jam_shape(r)));
      tw.push_back(kBackW);
      th.push_back(kBackH);
    }
    std::unique_lock<std::mutex> lock(b->m);  // (r181: let go before its celebration is made, below)
    b->cutBackTex = cutBackTex;
    b->home = std::move(home);
    b->tex = std::move(tex);
    b->tw = std::move(tw);
    b->th = std::move(th);
    b->fig = std::move(fig);
    b->figure = f;
    b->standee = standee;
    b->fit = fit;
    b->layerTex = layerTex;
    b->layers = layers;
    b->signTex = signTex;
    b->signDur = signDur;
    b->signY = signY;
    b->signZ = signZ;
    b->solidTex = solidTex;
    b->fitTop = fitTop;
    b->celLater = f != nullptr && !later.first.empty();  // r193
    b->done = true;
    lock.unlock();
    // r181: its celebration, made now the card is up (its standing figure shows meanwhile), then read with the same pack
    // for the frame loop to swap in (specials_frame: the same vertices, the same fit - both by its standing pose)
    // r193: made ahead as the pack came up (cel_prefetch) - here only if that hasn't reached it yet (made already: read)
    if (f != nullptr && !later.first.empty()) {
      const uint64_t t0 = mono_ns();
      const int made = cel_ready(later.second, true) ? 1 : cel_bake(later.first, later.second);
      xrfx::Model tmp;
      xrevo::Figure* cf = made == 1 || file_there(later.second) ? xrevo::load_files(later.first, later.second, &tmp) : nullptr;
      CLOG("special: %s's celebration %s after the card was up (%.0f ms)%s", c.key.c_str(),
           made == 1 ? "made" : (made == 0 ? "none to make" : "FAILED"), secs(mono_ns(), t0) * 1000.0f, cf != nullptr ? ", to swap in" : "");
      std::lock_guard<std::mutex> lk(b->m);
      if (cf != nullptr) {
        b->celFig = cf;
        b->celVerts = tmp.vertexCount;
      }
      b->celTried = true;
    } else {
      std::lock_guard<std::mutex> lk(b->m);
      b->celTried = true;
    }
  }).detach();
}

// r173 / r181 / r193: its figure's living loop - its idle (kind 0), then its big move (kind 2, else its cheer 5, else its
// greeting / reaction 1 / 3: the first with a clean stretch, card_move), round again; none: its idle alone / its whole
// pose file
constexpr float kBlendS = 0.25f, kBlendIn = 0.4f;  // r193: a change of move blended over (s); a celebration swapped in
void live_moves(Special& sp) {
  sp.liveIdleStart = sp.liveIdleFrames = sp.liveBigStart = sp.liveBigFrames = 0;
  for (const xrevo::Segment& seg : xrevo::segments(sp.fig))
    if (seg.kind == 0 && seg.frames > 4) {
      sp.liveIdleStart = seg.start, sp.liveIdleFrames = seg.frames;
      break;
    }
  uint32_t s = 0, n = 0;
  if (card_move(sp.fig, {2u, 5u, 1u, 3u}, &s, &n)) sp.liveBigStart = s, sp.liveBigFrames = n;
}

// Its model up (game thread, its pictures and figure ready): the shell (front with its window, back, edge: normals 2
// long), its room / slab (inside, normals in, behind its front) or nothing (a pop-out), the obsidian's glass, its figure.
void special_up(Special& sp, int slot) {
  SpecialBuild& b = *sp.build;
  xrfx::Model m;
  std::vector<float> stat;  // the vertices that never move (6 floats each)
  {
    std::lock_guard<std::mutex> lock(b.m);
    for (size_t k = 0; k < b.tex.size(); ++k) {
      m.texRgba.push_back(std::move(b.tex[k]));
      m.texW.push_back(static_cast<uint32_t>(b.tw[k]));
      m.texH.push_back(static_cast<uint32_t>(b.th[k]));
    }
  }
  const int roomTex = 3, glassTex = sp.kind == kSpGlass ? 3 + 5 : -1;
  auto grid_part = [&](int tex, int nx, int ny, bool mirror) {
    const uint32_t base = static_cast<uint32_t>(stat.size() / 6);
    xrfx::ModelPart part;
    part.tex = tex;
    part.firstIndex = static_cast<uint32_t>(m.indices.size());
    for (int i = 0; i <= ny; ++i)
      for (int j = 0; j <= nx; ++j) {
        const float u = static_cast<float>(j) / nx;
        m.uv.push_back(mirror ? 1.0f - u : u);
        m.uv.push_back(static_cast<float>(i) / ny);
      }
    for (int i = 0; i < ny; ++i)
      for (int j = 0; j < nx; ++j) {
        const uint32_t a = base + static_cast<uint32_t>(i * (nx + 1) + j), bb = a + 1, c = a + nx + 1, d = c + 1;
        m.indices.insert(m.indices.end(), {a, c, bb, bb, c, d});
      }
    part.indexCount = static_cast<uint32_t>(m.indices.size()) - part.firstIndex;
    m.parts.push_back(part);
  };
  // the shell, in its own frame (normals 2 long: the shader's "shell"), part by part
  Sheet unit;
  unit.w = kCardWm;
  unit.h = kCardHm;
  std::vector<float> shell(static_cast<size_t>(kCardVerts) * 6);
  const float shellT = sp.kind == kSpGlass ? 3.0f * kCardT : kCardT;
  // (r217: a Jam card's crest; r220: its own die - punched from the crest as it's turned over, shell_frame)
  card_box(shell.data(), unit, XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f}, XrVector3f{0.0f, 0.0f, 0.0f}, 1.0f, true, shellT, 2.0f, sp.shape, 1.0f);
  grid_part(0, kCardNX, kCardNY, false);
  stat.insert(stat.end(), shell.begin(), shell.begin() + kCardNV * 6);
  grid_part(1, kCardNX, kCardNY, true);
  stat.insert(stat.end(), shell.begin() + kCardNV * 6, shell.begin() + 2 * kCardNV * 6);
  grid_part(2, kEdgeSegs, 1, false);
  stat.insert(stat.end(), shell.begin() + 2 * kCardNV * 6, shell.end());
  sp.shellN = 0;
  if (sp.shape > 0 && b.cutBackTex >= 0) {  // r220: THE PUNCH - its crest front + its die's back, after its edge (the shell's states: shell_frame)
    const uint32_t base = static_cast<uint32_t>(stat.size() / 6);
    const float* L = lk::jamcut::kShape[0].loop;
    xrfx::ModelPart part;
    part.tex = 0;
    part.firstIndex = static_cast<uint32_t>(m.indices.size());
    for (int k = 0; k < lk::jamcut::kLoopN; ++k) {
      m.uv.push_back(L[2 * k] / kCW);
      m.uv.push_back(L[2 * k + 1] / kCH);
      stat.insert(stat.end(), {(L[2 * k] / kCW - 0.5f) * kCardWm, (0.5f - L[2 * k + 1] / kCH) * kCardHm, 0.5f * shellT, 0.0f, 0.0f, 2.0f});
    }
    for (int t = 0; t < 3 * lk::jamcut::kCrestTris; ++t) m.indices.push_back(base + lk::jamcut::kCrestTri[t]);
    part.indexCount = static_cast<uint32_t>(m.indices.size()) - part.firstIndex;
    m.parts.push_back(part);
    grid_part(b.cutBackTex, kCardNX, kCardNY, true);
    stat.insert(stat.end(), shell.begin() + kCardNV * 6, shell.begin() + 2 * kCardNV * 6);
    sp.shellN = static_cast<int>(stat.size() / 6);
    // after: its whole front, its die's back, its die's edge (the crest back + crest front folded away)
    sp.shellPost.assign(stat.begin(), stat.end());
    std::fill(sp.shellPost.begin() + kCardNV * 6, sp.shellPost.begin() + 2 * kCardNV * 6, 0.0f);
    std::fill(sp.shellPost.begin() + static_cast<size_t>(kCardVerts) * 6, sp.shellPost.begin() + static_cast<size_t>(kCardVerts + lk::jamcut::kLoopN) * 6, 0.0f);
    // before: its front inside the crest, the crest back, the crest's edge (its whole front + its die's back folded away)
    sp.shellPre.assign(stat.begin(), stat.end());
    std::vector<float> crest(static_cast<size_t>(kCardVerts) * 6);
    card_box(crest.data(), unit, XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f}, XrVector3f{0.0f, 0.0f, 0.0f}, 1.0f, true, shellT, 2.0f, sp.shape, 0.0f);
    std::copy(crest.begin() + 2 * kCardNV * 6, crest.end(), sp.shellPre.begin() + 2 * kCardNV * 6);
    std::fill(sp.shellPre.begin(), sp.shellPre.begin() + kCardNV * 6, 0.0f);
    std::fill(sp.shellPre.begin() + static_cast<size_t>(kCardVerts + lk::jamcut::kLoopN) * 6, sp.shellPre.end(), 0.0f);
    std::copy(sp.shellPost.begin(), sp.shellPost.end(), stat.begin());  // (shell_frame puts the right one in)
    sp.shellState = -1;
  }
  // r153: a GOLD's solid face (today's front, its picture) a hair over its windowed front, in kGoldRows strips across it -
  // placed every frame (gold_face): slid up off the window / down over it, the strips past its edge squeezed to nothing
  if (sp.kind == kSpPop && b.solidTex >= 0) {
    sp.solidBase = static_cast<int>(stat.size() / 6);
    grid_part(b.solidTex, 1, kGoldRows, false);
    stat.resize(stat.size() + static_cast<size_t>(kGoldRows + 1) * 2 * 6, 0.0f);
  }
  // a quad (4 corners, its normal; uv corners 0,1 / 1,1 / 1,0 / 0,0)
  // (r149: `turn`: given in a HOMETOWN's landscape frame, turned onto the card)
  auto quad = [&](int tex, const XrVector3f p[4], const XrVector3f& n0, bool soft, bool turn = false) {
    const uint32_t base = static_cast<uint32_t>(stat.size() / 6);
    static const float kU[4] = {0, 1, 1, 0}, kV[4] = {1, 1, 0, 0};
    const XrVector3f n = turn ? home_to_card(n0) : n0;
    for (int k = 0; k < 4; ++k) {
      const XrVector3f q = turn ? home_to_card(p[k]) : p[k];
      stat.insert(stat.end(), {q.x, q.y, q.z, n.x, n.y, n.z});
      m.uv.push_back(kU[k]);
      m.uv.push_back(kV[k]);
    }
    xrfx::ModelPart part;
    part.tex = tex;
    part.soft = soft;
    part.firstIndex = static_cast<uint32_t>(m.indices.size());
    m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    part.indexCount = 6;
    m.parts.push_back(part);
  };
  // (r149: a HOMETOWN's window landscape - its room built in its own frame, turned onto the card)
  const bool hm = sp.home;
  const float D = sp.kind == kSpGlass ? kSlabD : kRoomD, x0 = hm ? -kHomeWinHalfX : -kWinHalf, x1 = hm ? kHomeWinHalfX : kWinHalf,
              y0 = hm ? -kHomeWinHalfY : kWinBottom, y1 = hm ? kHomeWinHalfY : kWinTop;
  const bool diorama = hm && b.home.ok;  // r171: a HOMETOWN's 3D diorama in place of its room's walls
  if (diorama) {
    const HomeMesh& H = b.home;
    const uint32_t base = static_cast<uint32_t>(stat.size() / 6);
    sp.homeBase = static_cast<int>(base);
    sp.homeN = static_cast<int>(H.v.size() / 6);
    for (size_t i = 0; i + 5 < H.v.size(); i += 6) {
      const XrVector3f p = home_to_card(XrVector3f{H.v[i], H.v[i + 1], H.v[i + 2]}), n = home_to_card(XrVector3f{H.v[i + 3], H.v[i + 4], H.v[i + 5]});
      stat.insert(stat.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
    }
    m.uv.insert(m.uv.end(), H.uv.begin(), H.uv.end());
    for (int p = 0; p < kHpCount; ++p) {
      if (H.idx[p].empty()) continue;
      xrfx::ModelPart part;
      part.tex = roomTex + p;
      part.soft = p >= kHpShadow;
      part.firstIndex = static_cast<uint32_t>(m.indices.size());
      for (uint32_t ix : H.idx[p]) m.indices.push_back(ix + base);
      part.indexCount = static_cast<uint32_t>(H.idx[p].size());
      m.parts.push_back(part);
    }
    sp.homeRest = H.v;
    sp.homeAnim = H.anim;
    sp.homeFx = H.fx;
    sp.homeLo = H.lo;
    sp.homeHi = H.hi;
  }
  if (!diorama && (sp.kind == kSpWindow || sp.kind == kSpGlass || sp.kind == kSpPop)) {  // (r153: a GOLD's room too)
    const XrVector3f back[4] = {{x0, y0, -D}, {x1, y0, -D}, {x1, y1, -D}, {x0, y1, -D}};
    const XrVector3f floor[4] = {{x0, y0, 0.0f}, {x1, y0, 0.0f}, {x1, y0, -D}, {x0, y0, -D}};
    const XrVector3f left[4] = {{x0, y0, 0.0f}, {x0, y0, -D}, {x0, y1, -D}, {x0, y1, 0.0f}};
    const XrVector3f right[4] = {{x1, y0, -D}, {x1, y0, 0.0f}, {x1, y1, 0.0f}, {x1, y1, -D}};
    const XrVector3f ceil[4] = {{x0, y1, -D}, {x1, y1, -D}, {x1, y1, 0.0f}, {x0, y1, 0.0f}};
    // (r147: the obsidian's walls, normals a quarter long: xr_fx_model.frag's kaleidoscope - its back wall, the four
    // others its mirrors)
    const float wn = sp.kind == kSpGlass ? 0.25f : 1.0f;
    quad(roomTex + 0, back, XrVector3f{0, 0, wn}, false, hm);
    quad(roomTex + 1, floor, XrVector3f{0, wn, 0}, false, hm);
    quad(roomTex + 2, left, XrVector3f{wn, 0, 0}, false, hm);
    quad(roomTex + 3, right, XrVector3f{-wn, 0, 0}, false, hm);
    quad(roomTex + 4, ceil, XrVector3f{0, -wn, 0}, false, hm);
  }
  // r171: a GOLD's shine (placed every frame: gold_fx_moves) - 4 light sweeps, a halo, a pool (soft), its twinkles (lit by
  // themselves: normals 0.75 long) - on room_pictures' three shine pictures after its room's five
  if (sp.kind == kSpPop && !hm && static_cast<int>(m.texRgba.size()) >= roomTex + 8) {
    sp.goldFxBase = static_cast<int>(stat.size() / 6);
    const XrVector3f z4[4] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    for (int k = 0; k < 4; ++k) quad(roomTex + 5, z4, XrVector3f{0, 0, 1}, true);
    quad(roomTex + 6, z4, XrVector3f{0, 0, 1}, true);
    quad(roomTex + 6, z4, XrVector3f{0, 1, 0}, true);
    for (int k = 0; k < kGoldTwinkles; ++k) quad(roomTex + 7, z4, XrVector3f{0, 0, 0.75f}, false);
  }
  const Fit& fit = b.fit;
  if (!diorama && (sp.kind == kSpWindow || sp.kind == kSpPop) && fit.plinthTop > y0 + 0.001f) {  // r140: a plinth raising a short one to the middle (r171: a diorama's own rock)
    const float px0 = -fit.plinthHalfW, px1 = fit.plinthHalfW, py0 = y0, py1 = fit.plinthTop, pz0 = fit.plinthZ0,
                pz1 = fit.plinthZ1;
    const XrVector3f top[4] = {{px0, py1, pz1}, {px1, py1, pz1}, {px1, py1, pz0}, {px0, py1, pz0}};
    const XrVector3f front[4] = {{px0, py0, pz1}, {px1, py0, pz1}, {px1, py1, pz1}, {px0, py1, pz1}};
    const XrVector3f left[4] = {{px0, py0, pz0}, {px0, py0, pz1}, {px0, py1, pz1}, {px0, py1, pz0}};
    const XrVector3f right[4] = {{px1, py0, pz1}, {px1, py0, pz0}, {px1, py1, pz0}, {px1, py1, pz1}};
    quad(roomTex + 1, top, XrVector3f{0, 1, 0}, false, hm);  // (in the floor's own look)
    quad(roomTex + 1, front, XrVector3f{0, 0, 1}, false, hm);
    quad(roomTex + 1, left, XrVector3f{-1, 0, 0}, false, hm);
    quad(roomTex + 1, right, XrVector3f{1, 0, 0}, false, hm);
  }
  if (glassTex >= 0) {  // the obsidian's face over its window: soft, a shell part (its front's facing)
    const float z = 1.5f * kCardT - 0.0002f;
    const XrVector3f g[4] = {{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}};
    quad(glassTex, g, XrVector3f{0, 0, 2}, true);
    // r147: the character's picture in the stone - in the middle of its window, 2.6 cm in (it moves against the window as
    // the card tilts: you look INTO it), in front of the kaleidoscope; normals half long (xr_fx_model.frag: crisp, a bright
    // rim, spotlit)
    if (static_cast<int>(m.texRgba.size()) > glassTex + 2) {
      const float ah = 0.43f * std::min(x1 - x0, y1 - y0), acy = 0.5f * (y0 + y1), az = -0.026f;
      const XrVector3f a[4] = {{-ah, acy - ah, az}, {ah, acy - ah, az}, {ah, acy + ah, az}, {-ah, acy + ah, az}};
      quad(glassTex + 2, a, XrVector3f{0, 0, 0.5f}, false);
    }
    // r141: its throne - a two-step pedestal of black glass on its top edge, as wide as its figure stands (normals 1
    // long, above the card's top: lit as its figure, xr_fx_model.frag)
    const float figW = fit.scale * fit.bw, figD = fit.scale * fit.bd;
    const float pw = clampf(0.42f * figW, 0.28f * kCardWm, 0.5f * kCardWm), pd = clampf(0.4f * figD, 0.012f, 0.022f);
    const float yb = 0.5f * kCardHm + 0.0005f;
    for (int step = 0; step < 2; ++step) {
      const float hw = step == 0 ? pw : 0.8f * pw, hd = step == 0 ? pd : 0.8f * pd;
      const float y0 = step == 0 ? yb : yb + kPedStep, y1 = step == 0 ? yb + kPedStep : kThroneTop;
      const XrVector3f top[4] = {{-hw, y1, hd}, {hw, y1, hd}, {hw, y1, -hd}, {-hw, y1, -hd}};
      const XrVector3f fr[4] = {{-hw, y0, hd}, {hw, y0, hd}, {hw, y1, hd}, {-hw, y1, hd}};
      const XrVector3f bk[4] = {{hw, y0, -hd}, {-hw, y0, -hd}, {-hw, y1, -hd}, {hw, y1, -hd}};
      const XrVector3f lf[4] = {{-hw, y0, -hd}, {-hw, y0, hd}, {-hw, y1, hd}, {-hw, y1, -hd}};
      const XrVector3f rt[4] = {{hw, y0, hd}, {hw, y0, -hd}, {hw, y1, -hd}, {hw, y1, hd}};
      quad(glassTex + 1, top, XrVector3f{0, 1, 0}, false);
      quad(glassTex + 1, fr, XrVector3f{0, 0, 1}, false);
      quad(glassTex + 1, bk, XrVector3f{0, 0, -1}, false);
      quad(glassTex + 1, lf, XrVector3f{-1, 0, 0}, false);
      quad(glassTex + 1, rt, XrVector3f{1, 0, 0}, false);
    }
  }
  // r149: a TIZM BOOM's layers popping out of the front (normals 0.75 long: xr_fx_model.frag's flat bright stickers; placed
  // every frame - they pop): the character over the middle of its art box, its burst behind it, its three words further out
  // (r150: the burst and the three words, hidden until its pop-out show: special_moves places them every frame)
  // r166 ("PRINTED PAGE"): kBoomQuads of them (boom_place: its speed lines, its words' boiling frames, their puffs, the torn
  // hole, the character leaping out, the hole's flaps), on kBoomTex pictures
  if (b.layerTex >= 0 && b.layers >= kBoomTex) {
    sp.popBase = static_cast<int>(stat.size() / 6);
    sp.pops = kBoomQuads;
    for (int k = 0; k < kBoomQuads; ++k) {
      const XrVector3f q[4] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
      quad(b.layerTex + boom_tex_of(k), q, XrVector3f{0, 0, 0.75f}, false);
    }
  }
  // r149: its autograph writing itself on (a soft part over its front - a BLACK's over its glass; normals 3 long, their x
  // how far it's written: xr_fx_model.frag), across its picture, low
  if (b.signTex >= 0) {
    const lk::SignBox sb = lk::sign_box(sp.set, sp.foot);  // r217: each set's autograph place
    const float sx0 = (sb.x0 / kCW - 0.5f) * kCardWm, sx1 = ((sb.x0 + sb.w) / kCW - 0.5f) * kCardWm;
    const float sy1 = (0.5f - sb.y0 / kCH) * kCardHm, sy0 = (0.5f - (sb.y0 + sb.h) / kCH) * kCardHm;
    const float z = sp.kind == kSpGlass ? 1.5f * kCardT + 0.0012f : 0.5f * kCardT + 0.001f;
    const XrVector3f q[4] = {{sx0, sy0, z}, {sx1, sy0, z}, {sx1, sy1, z}, {sx0, sy1, z}};
    sp.signBase = static_cast<int>(stat.size() / 6);
    sp.signDur = b.signDur;
    sp.signY = b.signY;
    sp.signZ = b.signZ;
    quad(b.signTex, q, XrVector3f{sp.signDone ? 1.05f : 0.0f, sp.signY, sp.signZ}, true);
    // r179: its copy in the AIR - the same picture as a neon tube (normals x 10 + its progress: xr_fx_model.frag), placed
    // every frame (sign_air): written in the air in step with the card's, hanging, then flying down onto the card's
    sp.airBase = static_cast<int>(stat.size() / 6);
    const XrVector3f z4[4] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    quad(b.signTex, z4, XrVector3f{10.0f, sp.signY, sp.signZ}, true);
  }
  // its figure (posed every frame), or its picture standing up
  sp.fig = b.figure;
  b.figure = nullptr;
  {
    std::lock_guard<std::mutex> lk(b.m);
    sp.celPending = sp.fig != nullptr && b.celLater && !b.celTried && b.celFig == nullptr;  // r193: a still until its celebration's here
    if (sp.fig != nullptr && b.celLater && (b.celTried || b.celFig != nullptr)) sp.celPending = b.celFig != nullptr;
  }
  if (sp.fig != nullptr) {
    xrfx::Model& f = b.fig;
    const uint32_t texBase = static_cast<uint32_t>(m.texRgba.size()), vBase = static_cast<uint32_t>(stat.size() / 6);
    const uint32_t iBase = static_cast<uint32_t>(m.indices.size());
    for (size_t k = 0; k < f.texRgba.size(); ++k) {
      m.texRgba.push_back(std::move(f.texRgba[k]));
      m.texW.push_back(f.texW[k]);
      m.texH.push_back(f.texH[k]);
    }
    m.uv.insert(m.uv.end(), f.uv.begin(), f.uv.end());
    for (uint32_t ix : f.indices) m.indices.push_back(ix + vBase);
    for (xrfx::ModelPart p : f.parts) {
      p.tex += static_cast<int>(texBase);
      p.firstIndex += iBase;
      m.parts.push_back(p);
    }
    sp.figBase = static_cast<int>(vBase);
    sp.figCount = static_cast<int>(f.vertexCount);
    stat.resize(stat.size() + static_cast<size_t>(f.vertexCount) * 6, 0.0f);
    sp.figHeight = std::max(0.05f, xrevo::height(sp.fig));
    // r173: its living loop (a window's / a HOMETOWN's: its idle, then its big move - its celebration / power / combo -,
    // round again); none: its whole pose file (its idle breathing)
    // (r181: a Pokemon's / Melee fighter's celebration is cel_bake's cheer, kind 5 - r173 looked for kind 2 only, so on a
    // SILVER they only breathed; then its greeting / reaction, else its idle alone; r193: their clean stretches)
    live_moves(sp);
    CLOG("special: card %d's figure lives: idle %u frames, big move %u frames (%s)", sp.i, sp.liveIdleFrames, sp.liveBigFrames,
         sp.liveBigFrames > 0 ? "its own" : (sp.liveIdleFrames > 0 ? "idle only" : "its whole pose file"));
    sp.fit = fit;  // r140: its place and size (and a pop-out's own move, when its pose file has one), measured posed
    sp.fitTop = b.fitTop;  // r153: a GOLD's on its top edge (fit: in its room)
    sp.loopStart = fit.loopStart;
    sp.loopFrames = fit.loopFrames;
  } else if (b.standee >= 0) {
    sp.standBase = static_cast<int>(stat.size() / 6);
    const XrVector3f s4[4] = {{-0.03f, 0.0f, 0.0f}, {0.03f, 0.0f, 0.0f}, {0.03f, 0.06f, 0.0f}, {-0.03f, 0.06f, 0.0f}};
    quad(b.standee, s4, XrVector3f{0, 0, 1}, false);  // (placed every frame)
  }
  m.vertexCount = static_cast<uint32_t>(stat.size() / 6);
  sp.verts = std::move(stat);
  xrfx::set_model(slot, std::move(m));
  sp.up = xrfx::model_up(slot);
  static const char* const kKindName[6] = {"none", "window", "pop-out", "obsidian", "TIZM BOOM", "signed"};
  CLOG("special: card %d (%s%s%s) up in its slot%s", sp.i, kKindName[std::max(0, std::min(5, sp.kind))], sp.home ? ", HOMETOWN" : "",
       sp.signedCard ? ", its autograph" : "", sp.up ? "" : " - failed");
}

// The cards' light (kLightSlot): its pictures (premultiplied: alpha 0 light added, alpha a shade), made once
struct LightPics {
  std::vector<std::vector<uint8_t>> tex;
  std::vector<int> w;
};
const LightPics& light_pics() {
  static LightPics L;
  if (!L.tex.empty()) return L;
  auto make = [&](int w, auto fn) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * w * 4);
    for (int y = 0; y < w; ++y)
      for (int x = 0; x < w; ++x) {
        float c[4];
        fn((x + 0.5f) / w * 2.0f - 1.0f, (y + 0.5f) / w * 2.0f - 1.0f, x, y, c);
        for (int k = 0; k < 4; ++k) px[(static_cast<size_t>(y) * w + x) * 4 + k] = static_cast<uint8_t>(std::lround(clampf(c[k], 0.0f, 1.0f) * 255.0f));
      }
    L.tex.push_back(std::move(px));
    L.w.push_back(w);
  };
  auto glow = [](float u, float v, float r, float g, float b, float* c) {
    const float d = std::sqrt(u * u + v * v), f = d < 1.0f ? (1.0f - d) * (1.0f - d) : 0.0f;
    c[0] = r * f;
    c[1] = g * f;
    c[2] = b * f;
    c[3] = 0.0f;
  };
  make(128, [&](float u, float v, int, int, float* c) { glow(u, v, 0.75f, 0.85f, 1.0f, c); });  // 0 SILVER: cool white
  make(128, [&](float u, float v, int, int, float* c) { glow(u, v, 1.0f, 0.72f, 0.22f, c); });  // 1 GOLD: warm gold
  make(64, [&](float u, float v, int x, int y, float* c) {                                      // 2 BLACK: a smoke puff
    const float d = std::sqrt(u * u + v * v);
    const float n = 0.75f + 0.25f * std::sin(x * 0.7f + y * 0.45f) * std::sin(x * 0.31f - y * 0.53f);
    const float a = (d < 1.0f ? std::pow(1.0f - d, 1.2f) : 0.0f) * n * 0.92f;  // (r138 proof: denser, it read faint)
    c[0] = 0.30f * a;
    c[1] = 0.07f * a;
    c[2] = 0.48f * a;
    c[3] = a;
  });
  make(128, [&](float u, float v, int x, int y, float* c) {  // 3 SHINY: rainbow glitter in a soft glow
    const float d = std::sqrt(u * u + v * v), f = d < 1.0f ? (1.0f - d) : 0.0f;
    const float hue = std::fmod(std::atan2(v, u) / 6.2831853f + 1.0f + d * 0.5f, 1.0f) * 6.0f;
    const float rr = clampf(std::fabs(hue - 3.0f) - 1.0f, 0.0f, 1.0f), gg = clampf(2.0f - std::fabs(hue - 2.0f), 0.0f, 1.0f),
                bb = clampf(2.0f - std::fabs(hue - 4.0f), 0.0f, 1.0f);
    const uint32_t hsh = static_cast<uint32_t>(x * 73856093) ^ static_cast<uint32_t>(y * 19349663);
    const float glit = (hsh % 97u) < 3u ? 1.0f : 0.0f;
    const float ring = f * (0.55f + 0.45f * clampf((d - 0.3f) * 3.0f, 0.0f, 1.0f));  // (brightest round the card)
    c[0] = (rr * 0.8f + glit) * ring;
    c[1] = (gg * 0.8f + glit) * ring;
    c[2] = (bb * 0.8f + glit) * ring;
    c[3] = 0.0f;
  });
  make(64, [&](float u, float v, int, int, float* c) {  // 4 the beam: a soft shaft of warm white, brightest low, its
    const float t = (v + 1.0f) * 0.5f;                  // sides fading out, its foot fading under the card
    const float side = std::pow(1.0f - u * u, 1.5f), foot = clampf((1.0f - t) / 0.06f, 0.0f, 1.0f);
    const float f = (0.08f + 0.5f * std::pow(t, 1.3f)) * side * foot;
    c[0] = 1.0f * f;
    c[1] = 0.95f * f;
    c[2] = 0.82f * f;
    c[3] = 0.0f;
  });
  make(64, [&](float, float, int, int y, float* c) {  // 5 the dimming: rows of 8 px, row r = a shade of (r + 1) x 7%
    c[0] = c[1] = c[2] = 0.0f;
    c[3] = 0.07f * static_cast<float>(y / 8 + 1);
  });
  // r138 proof: a round glow sat mostly behind the card (it read as nothing); the face-down halos follow the card's shape:
  // full under the card, fading out over kAuraMargin all round (their quad: the card + kAuraMargin each side)
  auto halo = [](float u, float v) {
    const float iu = 0.5f * kCardWm / (0.5f * kCardWm + kAuraMargin), iv = 0.5f * kCardHm / (0.5f * kCardHm + kAuraMargin);
    const float dx = std::max(0.0f, std::fabs(u) - iu) / (1.0f - iu), dy = std::max(0.0f, std::fabs(v) - iv) / (1.0f - iv);
    const float d = std::sqrt(dx * dx + dy * dy);
    return d < 1.0f ? std::pow(1.0f - d, 1.7f) : 0.0f;
  };
  make(128, [&](float u, float v, int, int, float* c) {  // 6 SILVER: a cool white halo
    const float f = halo(u, v);
    c[0] = 0.75f * f;
    c[1] = 0.86f * f;
    c[2] = 1.0f * f;
    c[3] = 0.0f;
  });
  make(128, [&](float u, float v, int, int, float* c) {  // 7 GOLD: a warm gold halo
    const float f = halo(u, v);
    c[0] = 1.0f * f;
    c[1] = 0.74f * f;
    c[2] = 0.24f * f;
    c[3] = 0.0f;
  });
  make(128, [&](float u, float v, int x, int y, float* c) {  // 8 BLACK: a dark violet shadow-halo the smoke comes off, a
    const float n = 0.8f + 0.2f * std::sin(x * 0.37f + y * 0.21f) * std::sin(x * 0.13f - y * 0.29f);  // violet glow in it
    const float h = halo(u, v), a = std::pow(h, 0.7f) * 0.9f * n;
    c[0] = 0.30f * a;
    c[1] = 0.06f * a;
    c[2] = 0.50f * a;
    c[3] = a;
  });
  // r149: the case hits' tells behind a face-down card (light, added): 9 TIZM BOOM - a comic starburst, red and yellow
  // spikes, halftone dots in it; 10 HOMETOWN - a halo blue sky above, warm gold below; 11 SIGNED - a gold halo with swirls
  // of ink light round it
  make(128, [&](float u, float v, int x, int y, float* c) {
    const float d = std::sqrt(u * u + v * v), a = std::atan2(v, u);
    // r166 (no pointed stars: "PRINTED PAGE"): a cloud balloon's round scallops, red and yellow by turns
    const float ph = std::fmod(a / 6.2831853f * 12.0f + 12.0f, 1.0f);
    const float lobe = std::sqrt(std::max(0.0f, 1.0f - (2.0f * ph - 1.0f) * (2.0f * ph - 1.0f)));
    const float edge = 0.6f + 0.2f * lobe, f = d < edge ? clampf((edge - d) / 0.12f, 0.0f, 1.0f) * (0.45f + 0.55f * d / edge) : 0.0f;
    const bool red = (static_cast<int>(std::floor(a / 6.2831853f * 12.0f + 12.0f)) % 2) == 0;
    const float dots = (((x / 6) + (y / 6)) % 2 == 0) ? 1.0f : 0.7f;
    c[0] = (red ? 1.0f : 1.0f) * f * dots;
    c[1] = (red ? 0.25f : 0.85f) * f * dots;
    c[2] = (red ? 0.18f : 0.15f) * f * dots;
    c[3] = 0.0f;
  });
  make(128, [&](float u, float v, int, int, float* c) {
    const float f = halo(u, v), sky = clampf(0.5f - 0.6f * v, 0.0f, 1.0f);
    c[0] = (0.45f * sky + 1.0f * (1.0f - sky)) * f;
    c[1] = (0.78f * sky + 0.78f * (1.0f - sky)) * f;
    c[2] = (1.0f * sky + 0.35f * (1.0f - sky)) * f;
    c[3] = 0.0f;
  });
  make(128, [&](float u, float v, int, int, float* c) {
    const float d = std::sqrt(u * u + v * v), a = std::atan2(v, u);
    const float f = halo(u, v), swirl = 0.55f + 0.45f * std::pow(std::max(0.0f, std::sin(a * 3.0f + d * 9.0f)), 6.0f);
    c[0] = 1.0f * f * swirl;
    c[1] = 0.82f * f * swirl;
    c[2] = 0.45f * f * swirl;
    c[3] = 0.0f;
  });
  // r230: 12 the EXQUISITE gallery's warm veil: rows of 8 px, row r = (r + 1) x 4.5% darker with a little warm amber in it (premultiplied)
  make(64, [&](float, float, int, int y, float* c) {
    const float a = 0.045f * static_cast<float>(y / 8 + 1);
    c[0] = 0.13f * a, c[1] = 0.075f * a, c[2] = 0.02f * a;
    c[3] = a;
  });
  return L;
}

// The light model for this scene's cards (an aura per card that has one - two for a shiny parallel -, smoke for a
// BLACK, a glow under a GOLD's figure, the beam and the dimming when a BLACK is here). Game thread.
void light_up(bool fxReady) {
  g_lightParts.clear();
  g_lightUp = false;
  if (!fxReady) return;
  const LightPics& L = light_pics();
  xrfx::Model m;
  for (size_t k = 0; k < L.tex.size(); ++k) {
    m.texRgba.push_back(L.tex[k]);
    m.texW.push_back(static_cast<uint32_t>(L.w[k]));
    m.texH.push_back(static_cast<uint32_t>(L.w[k]));
  }
  int verts = 0;
  auto part = [&](int tex, int kind, int card, int n, int rows, int cols, float v0 = 0.0f, float v1 = 1.0f) {
    LightPart lp;
    lp.kind = kind;
    lp.card = card;
    lp.n = n;
    lp.base = verts;
    lp.count = (rows + 1) * (cols + 1);
    xrfx::ModelPart p;
    p.tex = tex;
    p.soft = true;
    p.firstIndex = static_cast<uint32_t>(m.indices.size());
    for (int i = 0; i <= rows; ++i)
      for (int j = 0; j <= cols; ++j) {
        m.uv.push_back(static_cast<float>(j) / cols);
        m.uv.push_back(v0 + (v1 - v0) * static_cast<float>(i) / rows);
      }
    for (int i = 0; i < rows; ++i)
      for (int j = 0; j < cols; ++j) {
        const uint32_t a = static_cast<uint32_t>(verts + i * (cols + 1) + j), b = a + 1, c = a + cols + 1, d = c + 1;
        m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
      }
    p.indexCount = static_cast<uint32_t>(m.indices.size()) - p.firstIndex;
    m.parts.push_back(p);
    verts += lp.count;
    g_lightParts.push_back(lp);
  };
  bool black = false;
  for (int i = 0; i < S.n; ++i) {
    const Ref& r = S.refs[i];
    const int aura = r.parallel == kSilver ? kAuraSilver : (r.parallel == kGoldTizm ? kAuraGold : (r.parallel == kBlack ? kAuraSmoke : kAuraNone));
    if (aura == kAuraSilver || aura == kAuraGold) part(aura == kAuraSilver ? kHaloSilver : kHaloGold, 0, i, 0, 1, 1);
    if (aura == kAuraSmoke) {
      black = true;
      part(kHaloBlack, 0, i, 0, 1, 1);  // its shadow-halo, then the smoke coming off it
      for (int j = 0; j < kSmokePuffs; ++j) part(2, 1, i, j, 1, 1);
    }
    if (r.shiny) part(3, 0, i, 1, 1, 1);
    // (r218: no GOLD figure on its top edge any more - its glow under it (kind 2) left out)
    if (r.hit != kHitNone) part(8 + r.hit, 0, i, 10 + r.hit, 1, 1);  // r149: a case hit's own tell (light pictures 9 - 11)
  }
  // r230: an EXQUISITE pack - the private gallery: its warm veil round you (8 levels: g_gallery) + the soft spotlight on the card in hand
  const bool gallery = (S.packData.flags & kExqPack) != 0 && S.mode == kPackUp;
  if (gallery) {
    for (int k = 1; k <= 8; ++k) part(12, 5, -1, k, 6, 10, (k - 0.5f) / 8.0f, (k - 0.5f) / 8.0f);
    if (!black) part(4, 3, -1, 0, 1, 16, 0.01f, 0.99f);
  }
  if (black) {
    // the beam: a soft shaft turned to your eyes, 16 across (its ends kept off the repeating edge)
    part(4, 3, -1, 0, 1, 16, 0.01f, 0.99f);
    // the dimming, 8 shades: shade k (7% .. 56%) its picture's row k - 1 (the middle of the row: its sampler repeats)
    for (int k = 1; k <= 8; ++k) part(5, 4, -1, k, 6, 10, (k - 0.5f) / 8.0f, (k - 0.5f) / 8.0f);
  }
  if (g_lightParts.empty()) {
    xrfx::set_model(kLightSlot, xrfx::Model{});
    return;
  }
  m.vertexCount = static_cast<uint32_t>(verts);
  g_lightN = verts;
  g_lightVerts.assign(static_cast<size_t>(verts) * 6, 0.0f);
  xrfx::set_model(kLightSlot, std::move(m));
  g_lightUp = xrfx::model_up(kLightSlot);
}

// This scene's specials: none made yet (r139: each card's 3D is made as it comes to the front of the stack, below); its
// light. (open_pack, look_card)
void specials_plan(uint64_t now, bool fxReady) {
  (void)now;
  for (int k = 0; k < kMaxSpecials; ++k) {
    if (g_sp[k].up && fxReady) xrfx::set_model(kSpecialSlot0 + k, xrfx::Model{});
    if (g_sp[k].fig != nullptr) xrevo::release(g_sp[k].fig);
    g_sp[k] = Special{};
  }
  fx_end(fxReady);  // r141
  g_cam = Cameo{};
  light_up(fxReady);
}
// r139: card i at the front of the stack (or out of the binder): its 3D made now if it has a special (its pictures and
// figure on a thread, ready by the time it's turned over), in the slot of the special that was at the front longest ago
// (two: this one and the one before; the rest of the stack shows flat)
Special* special_of(int i);
void special_focus(int i, uint64_t now, bool fxReady) {
  if (i < 0 || i >= S.n || special_kind(S.refs[i]) == kSpNone) return;
  if (Special* sp = special_of(i)) {
    sp->usedNs = now;
    return;
  }
  int k = 0;
  for (int j = 1; j < kMaxSpecials; ++j)
    if (g_sp[j].i < 0 || (g_sp[k].i >= 0 && g_sp[j].usedNs < g_sp[k].usedNs)) k = j;
  if (g_sp[k].up && fxReady) xrfx::set_model(kSpecialSlot0 + k, xrfx::Model{});
  if (g_sp[k].fig != nullptr) xrevo::release(g_sp[k].fig);
  g_sp[k] = Special{};
  special_start(g_sp[k], i, now);
  g_sp[k].usedNs = now;
}
// r139: what of a card shows now: its 3D (at the stack's front, or lifting off it on its way to the back; out of the
// binder always); its tell before it's turned over (only the front card's: the tension builds card by card)
bool special_live(int i) {
  if (S.mode != kPackUp) return true;
  return S.phase == kStack && (S.depth[i] == 0 || (S.backNs[i] != 0 && S.backU[i] < 0.6f));
}
bool tell_live(int i) {
  if (S.mode != kPackUp) return true;
  return S.phase == kStack && S.depth[i] == 0 && S.backNs[i] == 0;
}
void specials_end(bool fxReady) {
  for (int k = 0; k < kMaxSpecials; ++k) {
    if (g_sp[k].up && fxReady) xrfx::set_model(kSpecialSlot0 + k, xrfx::Model{});
    if (g_sp[k].fig != nullptr) xrevo::release(g_sp[k].fig);
    g_sp[k] = Special{};
  }
  fx_end(fxReady);  // r141
  g_cam = Cameo{};
  if (g_lightUp && fxReady) xrfx::set_model(kLightSlot, xrfx::Model{});
  g_lightUp = false;
  g_lightParts.clear();
  g_pullGateNs = 0;  // r219
}
Special* special_of(int i) {
  for (Special& sp : g_sp)
    if (sp.i == i && sp.i >= 0) return &sp;
  return nullptr;
}
// a BLACK turned over: its spotlight on for a while
void special_spot(int i, uint64_t until) {
  if (Special* sp = special_of(i))
    if (sp->kind == kSpGlass) sp->spotUntil = until;
}

// A special's chime as it's turned over (made here: a bell's partials dying away; SILVER soft, GOLD two bright notes,
// BLACK a low swell and a glass ping, a shiny a sparkling run up)
void special_chime(const Ref& r) {
  constexpr int kRate = 32000;
  struct Note {
    float at, hz, gain, decay;
  };
  std::vector<Note> notes;
  if (r.parallel == kBlack) notes = {{0.0f, 98.0f, 0.5f, 1.4f}, {0.35f, 2637.0f, 0.22f, 1.1f}, {0.36f, 3951.0f, 0.1f, 0.8f}};
  else if (r.parallel == kGoldTizm) notes = {{0.0f, 1319.0f, 0.3f, 1.2f}, {0.16f, 1976.0f, 0.3f, 1.5f}};
  else if (r.parallel == kSilver) notes = {{0.0f, 1568.0f, 0.22f, 1.4f}};
  if (r.shiny)
    for (int k = 0; k < 5; ++k) notes.push_back({0.45f + 0.07f * k, 1047.0f * std::pow(1.26f, static_cast<float>(k)), 0.12f, 0.5f});
  if (notes.empty()) return;
  float len = 0.0f;
  for (const Note& n : notes) len = std::max(len, n.at + n.decay * 3.0f);
  std::vector<short> pcm(static_cast<size_t>(len * kRate));
  for (const Note& n : notes) {
    const int start = static_cast<int>(n.at * kRate);
    const bool swell = n.hz < 200.0f;
    for (size_t s = static_cast<size_t>(start); s < pcm.size(); ++s) {
      const float t = static_cast<float>(s - start) / kRate;
      const float env = (swell ? std::min(1.0f, t / 0.35f) : std::min(1.0f, t / 0.004f)) * std::exp(-t / n.decay);
      if (env < 1e-4f && t > 0.5f) break;
      const float w = 6.2831853f * n.hz * t;
      const float v = std::sin(w) + 0.35f * std::sin(2.0f * w) + 0.15f * std::sin(3.01f * w) + 0.06f * std::sin(4.2f * w);
      pcm[s] = static_cast<short>(clampf(pcm[s] + v * env * n.gain * 9000.0f, -32000.0f, 32000.0f));
    }
  }
  PortCardSound(pcm.data(), static_cast<int>(pcm.size()), 1.0f);
}

// r139: the pack's own sounds (made here, like the chimes): 0 the rip - foil tearing, a crackle of little tears that
// thickens over a papery hiss, a snap as it comes away; 1 a card sliding out - a soft papery shh; 2 a card turning over -
// a quick flick of air; 3 a card put to the back - card on card, a tap as it lands
void card_sound(int kind) {
  constexpr int kRate = 32000;
  static uint32_t calls = 0;
  const float len = kind == 0 ? 0.42f : (kind == 1 ? 0.16f : (kind == 2 ? 0.12f : 0.22f));
  std::vector<short> pcm(static_cast<size_t>(len * kRate));
  uint32_t rng = 0x9E3779B9u ^ (static_cast<uint32_t>(kind) * 7919u + (++calls) * 104729u);
  auto rnd = [&rng]() {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return static_cast<float>(rng & 0xFFFFFFu) / 8388608.0f - 1.0f;
  };
  float lp = 0.0f, lp2 = 0.0f, crack = 0.0f;
  for (size_t s = 0; s < pcm.size(); ++s) {
    const float x = static_cast<float>(s) / static_cast<float>(pcm.size());
    const float n = rnd();
    float v = 0.0f;
    if (kind == 0) {
      const float env = std::min(1.0f, x / 0.08f) * (x < 0.8f ? 1.0f : std::max(0.0f, (1.0f - x) / 0.2f));
      if (rnd() > 0.985f - 0.03f * x) crack = 1.0f;
      crack *= 0.86f;
      const float hp = n - lp;
      lp += 0.35f * (n - lp);
      v = (0.35f * hp + 0.9f * crack * rnd()) * env + (x > 0.9f && x < 0.92f ? 0.8f * n : 0.0f);
    } else if (kind == 1) {
      lp += 0.25f * (n - lp);
      v = 0.35f * lp * std::sin(3.14159265f * x);
    } else if (kind == 2) {
      lp += 0.12f * (n - lp);
      lp2 += 0.5f * (lp - lp2);
      v = 0.9f * lp2 * std::exp(-x * 4.0f) * std::min(1.0f, x / 0.05f);
    } else {
      lp += 0.18f * (n - lp);
      v = 0.3f * lp * std::sin(3.14159265f * std::min(1.0f, x * 1.2f)) + (x > 0.85f ? 0.25f * n * std::exp(-(x - 0.85f) * 60.0f) : 0.0f);
    }
    pcm[s] = static_cast<short>(clampf(v * 14000.0f, -32000.0f, 32000.0f));
  }
  PortCardSound(pcm.data(), static_cast<int>(pcm.size()), kind == 0 ? 1.0f : 0.7f);
}

// a quad facing `toward` (a billboard) or along a turn (`q`), w x h, at `c`, into the light model's vertices
void light_quad(int base, const XrVector3f& c, const XrQuaternionf& q, float w, float h) {
  float* o = g_lightVerts.data() + static_cast<size_t>(base) * 6;
  static const float kX[4] = {-0.5f, 0.5f, -0.5f, 0.5f}, kY[4] = {0.5f, 0.5f, -0.5f, -0.5f};
  for (int k = 0; k < 4; ++k) {
    const XrVector3f p = v_add(c, q_rot(q, XrVector3f{kX[k] * w, kY[k] * h, 0.0f}));
    const XrVector3f n = q_rot(q, XrVector3f{0.0f, 0.0f, 1.0f});
    o[k * 6 + 0] = p.x;
    o[k * 6 + 1] = p.y;
    o[k * 6 + 2] = p.z;
    o[k * 6 + 3] = n.x;
    o[k * 6 + 4] = n.y;
    o[k * 6 + 5] = n.z;
  }
}
void light_hide(const LightPart& lp) {
  float* o = g_lightVerts.data() + static_cast<size_t>(lp.base) * 6;
  for (int k = 0; k < lp.count * 6; ++k) o[k] = 0.0f;
}
XrQuaternionf facing(const XrVector3f& at) {  // turned to your eyes
  const XrVector3f& h = S.head.position;
  const float dx = h.x - at.x, dz = h.z - at.z;
  const float up = std::atan2(h.y - at.y, std::max(0.05f, std::sqrt(dx * dx + dz * dz)));
  return q_mul(q_axis(0, 1, 0, std::atan2(dx, dz)), q_axis(1, 0, 0, -up));
}

// r149: a TIZM BOOM's layers popping out of its front (each its own beat, the words wobbling; held, each pops now and then -
// "animated pops when held"; face down, gone), an autograph writing itself on (from just after it's first turned face up,
// over its time; then written - normals' x: how far, xr_fx_model.frag)
// r150 (Lance: "make it so those animations are not on the card, yet pop out from the card and disappear after the
// animation happens"; "old school batman type music when those are pulled"): a TIZM BOOM's pop-out show, on its music's
// brass hits (boom.wav, tools/outside/card_sounds150.py - an original 60s-TV-superhero sting: the hits at kBoomHit):
// the burst a shock ring off the card on the first; each word flying out of the card's middle on its hit (0, 1.2, 2.4 s)
// into the space round it, overshooting with a bounce, spinning in, hanging there bobbing, then bursting away (a ring of
// shards where it was) on the hit two later. Then gone - its face clean. `k` 0 the burst, 1 - 3 the words; false: hidden.
// r153 (Lance: the synthesised sting "sounds like a PONG soundtrack"; his pick: "the first 3 seconds of the retro twangy
// surf rock"): boom.wav = the first 3 s of "Retro Twangy Surf Rock" by BudgetPixel AI (CC BY 4.0, cut, mono 32 kHz, -16 dB
// RMS, faded out; the old sting kept as boom.wav.bak-r153m), 120 BPM - the words on its hits (0, 0.5, 1.15, 2.0, 2.5 s)
// r166 ("PRINTED PAGE"): the show on the same hits, a little longer (its last puffs and the character back in its hole)
constexpr float kBoomHit[5] = {0.0f, 0.5f, 1.15f, 2.0f, 2.5f}, kBoomLen = 3.15f;
float ease_out(float x) {
  x = clampf(x, 0.0f, 1.0f);
  return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}
float pp_back(float x, float s) {  // (ease out, overshooting by s)
  x = clampf(x, 0.0f, 1.0f) - 1.0f;
  return 1.0f + (s + 1.0f) * x * x * x + s * x * x;
}
float pp_spring(float a, float k, float w) { return a <= 0.0f ? 0.0f : 1.0f - std::exp(-k * a) * std::cos(w * a); }
float pp_wobble(float a, float k, float w) { return a <= 0.0f ? 0.0f : std::exp(-k * a) * std::sin(w * a); }
// r166: quad `o` of a TIZM BOOM's show (boom_tex_of) at `u` s into it (`t` the clock: its idle bob), in its card's frame
// (m at size 1, its front +z, its top edge y = kCardHm / 2): where, how big, its turn; false hidden. On boom.wav's hits:
//  0    the character LEAPS up out of a torn hole in the card's top (paper flaps curling), curved speed lines sweep out
//       behind the card, and the first word pops out of the top edge (behind the card, then out toward you) in its cloud
//       balloon - overshooting, then wobbling (squash and stretch) and hanging there bobbing, its line boiling on fours
//  0.5  the second word, 1.15 the third (left and right of the character, under the first)
//  2.0  the first two leave (a swell, then gone in a ring of round puffs), 2.5 the third; the character dives back down
//       its hole, the lines fade, the hole closes - the card's face clean again (Lance r150: nothing left on the card)
// Nothing goes across the card's face but the hole / its flaps / the character at its top edge.
constexpr float kBoomTopY = 0.5f * kCardHm;
constexpr XrVector3f kBoomTo[3] = {{0.0f, 0.205f, 0.050f}, {-0.083f, 0.150f, 0.045f}, {0.083f, 0.144f, 0.055f}};
constexpr float kBoomW[3] = {0.135f, 0.100f, 0.100f}, kBoomTilt[3] = {-0.12f, 0.16f, -0.17f};
constexpr float kBoomOut[3] = {2.0f, 2.12f, 2.5f};
bool boom_place(int o, float u, float t, XrVector3f* c, float* w, float* h, float* rot) {
  const int frame = static_cast<int>(u * 30.0f);
  if (o <= 1) {  // its speed lines (flickering between two drawings on threes), behind the card round its top
    if (u < 0.0f || u > 2.8f || (frame / 3) % 2 != o) return false;
    const float k = clampf(u / 0.25f, 0.0f, 1.0f) * (1.0f - smooth(clampf((u - 2.4f) / 0.4f, 0.0f, 1.0f)));
    if (k < 0.02f) return false;
    *c = {0.0f, kBoomTopY + 0.01f, -0.006f};
    *w = *h = 0.42f * (0.45f + 0.55f * k);
    *rot = 0.1f * u;
    return true;
  }
  if (o <= 10) {  // a word in its balloon (the boiling frame of it showing now)
    const int j = (o - 2) / 3, v = (o - 2) % 3;
    if ((frame / 4) % 3 != v) return false;
    const float a = u - kBoomHit[j], g = u - kBoomOut[j];
    if (a < 0.0f || g >= 0.18f) return false;
    float s = pp_spring(a, 6.0f, 17.0f);
    if (g >= 0.0f) s *= g < 0.07f ? 1.0f + 1.6f * g / 0.18f : 1.11f * (1.0f - (g - 0.07f) / 0.11f);  // (a swell, then gone)
    // out from behind the card's top edge, then out toward you (never across its face)
    const XrVector3f from{0.3f * kBoomTo[j].x, kBoomTopY + 0.01f, -0.004f};
    const float e = ease_out(a / 0.3f);
    *c = v_add(from, v_mul(v_sub(kBoomTo[j], from), e));
    c->z = -0.004f + (kBoomTo[j].z + 0.004f) * smooth(clampf((e - 0.45f) / 0.55f, 0.0f, 1.0f));
    c->y += 0.003f * std::sin(t * 2.4f + 2.0f * j);
    const float sq = 1.0f + 0.12f * pp_wobble(a, 6.0f, 20.0f);
    *w = kBoomW[j] * s * sq;
    *h = 0.5f * kBoomW[j] * s / sq;
    *rot = kBoomTilt[j] + 0.05f * std::sin(t * 1.7f + j) - 0.5f * (1.0f - smooth(clampf(a / 0.3f, 0.0f, 1.0f))) * (j % 2 ? 1.0f : -1.0f);
    return true;
  }
  if (o <= 16) {  // a word's ring of round puffs as it goes (big, then small)
    const int j = (o - 11) / 2, sm = (o - 11) % 2;
    const float g = u - kBoomOut[j] - 0.05f;
    if (g < 0.0f || g >= 0.45f || (g < 0.22f) == (sm == 1)) return false;
    const float e = pp_back(g / 0.45f, 0.6f);
    *c = kBoomTo[j];
    *w = *h = kBoomW[j] * 0.9f * (0.5f + 0.6f * e);
    *rot = 0.6f * g;
    return true;
  }
  // the torn hole, the character, its flaps: open on the first hit, closed again after it's dived back in
  const float open = smooth(clampf(u / 0.12f, 0.0f, 1.0f)) * (1.0f - smooth(clampf((u - 2.8f) / 0.3f, 0.0f, 1.0f)));
  const float edgeDown = (0.5f - kPpEdge / 192.0f) * kPpHoleH;  // (the quad's middle below the card's top edge)
  if (o == 17 || o == 19) {
    if (open < 0.02f) return false;
    const float fl = o == 19 ? 1.0f + 0.06f * std::sin(t * 9.0f) : 1.0f;
    *w = kPpHoleW * open * fl;
    *h = kPpHoleH * open * fl;
    *c = {0.0f, kBoomTopY - edgeDown * open * fl, 0.5f * kCardT + (o == 19 ? 0.0032f : 0.0006f)};
    *rot = 0.0f;
    return true;
  }
  // (18) the character: up out of the hole with a spring (squash and stretch), hanging there bobbing, back down at 2.5
  float rise = 0.0f;
  if (u >= 0.0f && u < 0.3f) rise = pp_back(u / 0.3f, 1.3f);
  else if (u >= 0.3f && u < 2.5f) rise = 1.0f;
  else if (u >= 2.5f && u < 2.8f) rise = 1.0f - smooth((u - 2.5f) / 0.3f);
  if (rise < 0.02f) return false;
  if (o == 20) {  // (20) its panel left empty while it's out (it's left the picture: back in it as it dives home)
    if (rise < 0.5f) return false;
    const float x0 = (kHoleX0 / kCW - 0.5f) * kCardWm, x1 = (kHoleX1 / kCW - 0.5f) * kCardWm;
    const float y0 = (0.5f - kHoleY0 / kCH) * kCardHm, y1 = (0.5f - kHoleY1 / kCH) * kCardHm;
    *c = {0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * kCardT + 0.0003f};
    *w = x1 - x0;
    *h = y0 - y1;
    *rot = 0.0f;
    return true;
  }
  const float sq = 1.0f + 0.1f * pp_wobble(u - 0.3f, 5.0f, 16.0f), side = 0.088f;
  *h = side * rise * sq;
  *w = side * (0.3f + 0.7f * std::min(rise, 1.0f)) / sq;
  const float bottom = kBoomTopY - 0.016f;
  *c = {0.0f, bottom + 0.5f * *h + (u > 0.3f && u < 2.5f ? 0.002f * std::sin(t * 2.6f) : 0.0f), 0.5f * kCardT + 0.0018f};
  *rot = 0.05f * std::sin(t * 1.9f);
  return true;
}
// r150: how far above card i's top edge (up its own up, m at size 1) what stands on it reaches - a GOLD's figure / its
// picture, a BLACK's throne and figure, a TIZM BOOM's words while they're out - so a message floats over it, not on it
float special_above(int i) {
  for (const Special& sp : g_sp) {
    if (sp.i != i || !sp.up) continue;
    if (sp.kind == kSpGlass) return kPedH + (sp.fig != nullptr ? sp.fit.scale * sp.fit.bh : 0.8f * kThroneFig) + 0.006f;
    // (r153: a GOLD's figure only while it's up there - none in its room, none once it's back in its picture)
    if (sp.kind == kSpPop) return sp.goldTop < 0.01f ? 0.0f : (sp.fig != nullptr ? kPopFig + 0.01f : 0.62f * kCardWm);
    // (its words' top: r166 the first word's balloon, overshooting and bobbing - r165's box round it 13 cm each side)
    if (sp.kind == kSpBoom && sp.boomNs != 0) return 0.205f + 0.5f * 0.5f * 0.135f * 1.15f + 0.003f + 0.01f - 0.5f * kCardHm;
  }
  return 0.0f;
}

// r153: a GOLD's step-out loop at `t` s into it (kGoldLoop: SOLID, its face sliding up, IN its room, stepping out and up
// onto its top edge - its face sliding down behind it -, ON TOP, back into its picture, SOLID): how much of its window its
// face covers (0 open .. 1 solid), how much it stands on top (0 .. 1), its figure's feet / size / turn in the card's frame
// (none: hidden - SOLID, nobody in its room or on top), and whether it's far enough up to loop its top-edge move
struct GoldPose {
  float shut = 1.0f, top = 0.0f;
  bool fig = false, topLoop = false;
  float scale = 0.0f, yaw = -1.5707963f;
  XrVector3f feet{0.0f, 0.0f, 0.0f};
  float dive = -1.0f;  // (how far back into its picture: 0 .. 1; -1 not diving)
};
GoldPose gold_pose(const Special& sp, float t) {
  GoldPose g;
  if (sp.fig == nullptr) {  // (none here: today's GOLD - solid, its picture standing on top)
    g.top = 1.0f;
    return g;
  }
  if (t < kGoldOpen0) g.shut = 1.0f;
  else if (t < kGoldOpen1) g.shut = 1.0f - smooth((t - kGoldOpen0) / (kGoldOpen1 - kGoldOpen0));
  else if (t < kGoldShut0) g.shut = 0.0f;
  else if (t < kGoldShut1) g.shut = smooth((t - kGoldShut0) / (kGoldShut1 - kGoldShut0));
  else g.shut = 1.0f;
  const Fit &in = sp.fit, &up = sp.fitTop;
  auto between = [&](const Fit& a, const Fit& b, float u) {  // from a to b, out toward you and hopping on the way
    const float e = smooth(u), arc = std::sin(3.14159265f * clampf(u, 0.0f, 1.0f));
    g.fig = true;
    g.feet = v_lerp(a.feet, b.feet, e);
    g.feet.y += kGoldHop * arc;
    g.feet.z += kGoldOutZ * arc;
    g.scale = a.scale + (b.scale - a.scale) * e;
    g.yaw = a.yaw + (b.yaw - a.yaw) * e;
    return e;
  };
  if (t < kGoldOpen0) {
    // SOLID: nobody (back in its picture)
  } else if (t < kGoldOut0) {  // IN its room (its face sliding up off it first)
    g.fig = true;
    g.feet = in.feet;
    g.scale = in.scale;
    g.yaw = in.yaw;
  } else if (t < kGoldOut1) {  // stepping out of the window and up onto its top edge
    g.top = between(in, up, (t - kGoldOut0) / (kGoldOut1 - kGoldOut0));
  } else if (t < kGoldBack0) {  // ON TOP (today's GOLD)
    g.fig = true;
    g.top = 1.0f;
    g.feet = up.feet;
    g.scale = up.scale;
    g.yaw = up.yaw;
  } else if (t < kGoldBack1) {  // off its top edge and back into its picture (its solid face hiding it as it goes in)
    g.dive = (t - kGoldBack0) / (kGoldBack1 - kGoldBack0);
    g.top = 1.0f - between(up, in, g.dive);
  }
  g.topLoop = g.top > 0.5f;
  return g;
}
// its solid face's strips for `shut` (0: all at its top edge, nothing; 1: the whole face), in sp.verts
void gold_face(Special& sp, float shut) {
  if (sp.solidBase < 0) return;
  float* o = sp.verts.data() + static_cast<size_t>(sp.solidBase) * 6;
  const float yb = (0.5f - clampf(shut, 0.0f, 1.0f)) * kCardHm, z = 0.5f * kCardT + 0.00035f;
  for (int i = 0; i <= kGoldRows; ++i) {
    const float y = std::max((0.5f - static_cast<float>(i) / kGoldRows) * kCardHm, yb);
    for (int j = 0; j <= 1; ++j) {
      float* v = o + (static_cast<size_t>(i) * 2 + j) * 6;
      v[0] = (j == 0 ? -0.5f : 0.5f) * kCardWm;
      v[1] = y;
      v[2] = z;
      v[3] = 0.0f;
      v[4] = 0.0f;
      v[5] = 2.0f;  // (normals 2 long: a shell part, xr_fx_model.frag)
    }
  }
}
// a TIZM BOOM turned over in a pack: its show from now (its music started with it: reveal_fx)
void special_boom_start(int i) {
  for (Special& sp : g_sp)
    if (sp.i == i && sp.kind == kSpBoom && i >= 0) {
      sp.boomNs = mono_ns();
      if (i < kSheets) S.boomShown[i] = true;
    }
}

// r179 (Lance, his BLACK 1/1 Combusken: "can we make it so we see the autograph be signed in the air in like a neon or just
// LED fashion??? ... dont change the ink just make the in air autograph be a neon burnt orange like the auto on card was!";
// "no pen involved just a floating signature"): as its autograph writes itself on the card, the SAME signature floats in
// the air above the card - ~50 cm wide, upright, turned to your eyes - growing stroke by stroke in step with it (the same
// picture and progress), a neon tube in the card's own ink (xr_fx_model.frag's air mode: a hot core, its colour glowing
// round it, a faint flicker / hum; no writing tip), a neon buzz as it starts; written, it hangs a moment, then flies down
// onto the card's autograph (shrinking into it) and is gone. Every BLACK 1/1 and SIGNED case hit.
// r193 (Lance on r192's 1/1: "the autograph needs to take full center stage. You'll see the signature float down to the card
// but it wasn't in my eyesight to even know it happened"; "the ink needs to SHINE - that was the most dull inked signature
// ever"; "it needs to be a hell of a feeling getting those 1/1s"; coordinator: in the room where r179's writes, just LOWER -
// near / just over the card in his hand, chest to eye height - never following his head): the autograph's MOMENT, every
// BLACK 1/1 and SIGNED case hit turned over in a pack. GATHER (kAirBuild: light and sparks drawing in where the pen will
// start, a rising shimmer of chimes, the room dimming, the 1/1's figure dimmed and stepping down into the card), WRITE (big: kAirW wide 0.5-0.95 m from
// your eyes, in polished foil ink in its own colour - xr_fx_model.frag: a white-hot glint riding the pen tip, sparks
// dropping off it, tinkles), HANG (a light sweep over the finished foil, twinkles), FLY down onto the card (sparks trailing)
// and LAND (only now is it on the card: a flash and a ring there, a bell, a buzz in your hand), then a slow shimmer on the
// card for good. A BLACK 1/1's life-size cameo has the stage first: the moment gathers as its figure heads back into the
// card (while it's out the card shows flat - its autograph printed on it - so the moment waits for it; its celebration kept
// to <= 4.5 s). Turned away mid-way (0.6 s): it starts again when its face is back. No shake anywhere.
constexpr float kAirBuild = 1.0f, kAirHang = 1.2f, kAirFly = 0.75f, kAirW = 0.46f, kAirBack = 0.9f;
float air_write(const Special& sp) { return clampf(sp.signDur * 1.1f, 1.6f, 4.2f); }
float air_land(const Special& sp) { return kAirBuild + air_write(sp) + kAirHang + kAirFly; }
// how much the moment holds the stage (0 .. 1): up as it gathers, down after it lands (the room's dimming, its figure's)
float sign_focus(const Special& sp, uint64_t now) {
  if (sp.landNs != 0) return 1.0f - smooth(clampf(secs(now, sp.landNs) / kAirBack, 0.0f, 1.0f));
  if (sp.airNs == 0 || now <= sp.airNs) return 0.0f;
  return smooth(clampf(secs(now, sp.airNs) / 0.6f, 0.0f, 1.0f));
}
uint32_t sign_rgb(const Special& sp) {  // its ink (sign_ink's), none = its gold
  const int r = static_cast<int>(sp.signY), g = static_cast<int>(std::lround((sp.signY - static_cast<float>(r)) * 256.0f)),
            b = static_cast<int>(std::lround((sp.signZ - 3.0f) * 256.0f));
  if (r + g + b == 0) return 0xFFBD4Du;
  return static_cast<uint32_t>(r & 255) << 16 | static_cast<uint32_t>(g & 255) << 8 | static_cast<uint32_t>(b & 255);
}
// its sounds (made here, 32 kHz mono): 0 the gather (four chimes sweeping up over a swelling air, ~1 s), 1 the writing's
// tinkles over a faint hiss (len s), 2 the landing (a bell struck bright over a soft low thump)
const std::vector<short>& sign_sound(int kind, float len) {
  static std::vector<short> made[3];
  static float madeLen = -1.0f;
  constexpr int kRate = 32000;
  std::vector<short>& pcm = made[kind];
  if (kind == 1 && std::fabs(madeLen - len) > 0.01f) pcm.clear(), madeLen = len;
  if (!pcm.empty()) return pcm;
  uint32_t rng = 0x5EED1u + static_cast<uint32_t>(kind);
  auto rnd = [&]() {
    rng = rng * 1664525u + 1013904223u;
    return static_cast<float>(rng >> 8) / 16777216.0f;
  };
  if (kind == 0) {
    static const float kF[4] = {523.25f, 659.25f, 783.99f, 1046.5f};
    pcm.assign(static_cast<size_t>((kAirBuild + 0.2f) * kRate), 0);
    float lp = 0.0f;
    for (size_t s = 0; s < pcm.size(); ++s) {
      const float t = static_cast<float>(s) / kRate, x = std::min(1.0f, t / kAirBuild);
      const float env = x * x * (t < kAirBuild ? 1.0f : std::exp(-(t - kAirBuild) / 0.05f));
      float v = 0.0f;
      for (int k = 0; k < 4; ++k) {  // (each up 1.2 octaves: its phase the sweep's integral)
        const float ph = kF[k] * kAirBuild / (1.2f * 0.693147f) * (std::pow(2.0f, 1.2f * x) - 1.0f);
        v += std::sin(6.2831853f * ph) * (0.22f - 0.03f * k) * (0.6f + 0.4f * std::sin(6.2831853f * (7.0f + 3.0f * k) * t));
      }
      lp += (rnd() * 2.0f - 1.0f - lp) * (0.05f + 0.25f * x);  // (the air swelling, brighter as it builds)
      v += lp * 0.5f * x;
      pcm[s] = static_cast<short>(clampf(v * env * 9000.0f, -32000.0f, 32000.0f));
    }
  } else if (kind == 1) {
    pcm.assign(static_cast<size_t>((len + 0.3f) * kRate), 0);
    for (float at = 0.0f; at < len; at += 0.05f + 0.06f * rnd()) {
      const float f0 = 2600.0f + 2600.0f * rnd(), amp = 0.25f + 0.35f * rnd();
      const size_t s1 = std::min(pcm.size(), static_cast<size_t>((at + 0.25f) * kRate));
      for (size_t s = static_cast<size_t>(at * kRate); s < s1; ++s) {
        const float t = static_cast<float>(s) / kRate - at;
        const float e = std::min(1.0f, t / 0.002f) * std::exp(-t / 0.06f);
        const float v = std::sin(6.2831853f * f0 * t) + 0.4f * std::sin(6.2831853f * f0 * 2.01f * t);
        pcm[s] = static_cast<short>(clampf(pcm[s] + v * e * amp * 5000.0f, -32000.0f, 32000.0f));
      }
    }
    float lp = 0.0f;
    for (size_t s = 0; s < pcm.size(); ++s) {
      const float t = static_cast<float>(s) / kRate, n = rnd() * 2.0f - 1.0f;
      lp += (n - lp) * 0.3f;
      const float e = std::min(1.0f, t / 0.2f) * clampf((len + 0.2f - t) / 0.3f, 0.0f, 1.0f);
      pcm[s] = static_cast<short>(clampf(pcm[s] + (n - lp) * e * 700.0f, -32000.0f, 32000.0f));
    }
  } else {
    static const float kP[5][3] = {{1046.5f, 1.0f, 1.6f}, {1568.0f, 0.55f, 1.1f}, {2093.0f, 0.45f, 0.9f}, {2637.0f, 0.3f, 0.6f},
                                   {3520.0f, 0.2f, 0.35f}};
    pcm.assign(static_cast<size_t>(2.2f * kRate), 0);
    for (size_t s = 0; s < pcm.size(); ++s) {
      const float t = static_cast<float>(s) / kRate;
      float v = 0.0f;
      for (const auto& p : kP) v += std::sin(6.2831853f * p[0] * t) * p[1] * std::exp(-t / p[2]);
      v *= std::min(1.0f, t / 0.003f);
      v += std::sin(6.2831853f * (70.0f - 20.0f * t) * t) * 0.9f * std::exp(-t / 0.12f);  // (the thump)
      pcm[s] = static_cast<short>(clampf(v * 7000.0f, -32000.0f, 32000.0f));
    }
  }
  return pcm;
}
// the moment's clock (every frame its card is here; `front`: its face to your eyes): starts, plays its sounds, lands
void sign_moment(Special& sp, bool front, uint64_t now, float dt) {
  if (sp.signDone || sp.i < 0 || sp.i >= kSheets) return;
  if (!front) {
    sp.frontNs = 0;
    if (sp.airNs != 0) {
      sp.awayS += dt;
      if (sp.awayS > 0.6f) {  // (turned away mid-way: from the start again when its face is back)
        sp.airNs = 0;
        sp.airSnd = 0;
        sp.airPosOk = false;
      }
    }
    return;
  }
  sp.awayS = 0.0f;
  if (sp.frontNs == 0) sp.frontNs = now;
  if (sp.airNs == 0) {
    if (sp.kind == kSpGlass && g_cam.i == sp.i) {  // (its cameo first: the moment gathers as its figure heads back)
      if (!g_cam.started || g_cam.t < g_cam.backAt + 0.3f) return;
    } else if (secs(now, sp.frontNs) < 0.5f) {
      return;
    }
    sp.airNs = now;
    CLOG("autograph: its moment starts (%.1f s to land)", air_land(sp));
  }
  const float u = secs(now, sp.airNs);
  if (sp.airSnd == 0) {
    sp.airSnd = 1;
    const std::vector<short>& p = sign_sound(0, 0.0f);
    PortCardSound(p.data(), static_cast<int>(p.size()), 0.7f);
  }
  if (sp.airSnd == 1 && u >= kAirBuild) {
    sp.airSnd = 2;
    const std::vector<short>& p = sign_sound(1, air_write(sp));
    PortCardSound(p.data(), static_cast<int>(p.size()), 0.45f);
    buzz(0.25f, 0.05f);
  }
  if (u >= air_land(sp)) {
    sp.signDone = true;
    sp.landNs = now;
    const std::vector<short>& p = sign_sound(2, 0.0f);
    PortCardSound(p.data(), static_cast<int>(p.size()), 0.9f);
    buzz(0.6f, 0.18f);
    const Sheet& s = S.card[sp.i];
    const lk::SignBox sb = lk::sign_box(sp.set, sp.foot);  // r217
    const float sx = ((sb.x0 + 0.5f * sb.w) / kCW - 0.5f) * kCardWm, sy = (0.5f - (sb.y0 + 0.5f * sb.h) / kCH) * kCardHm;
    xrfx::ink_land(v_add(s.pos, q_rot(sheet_q(s), v_mul(XrVector3f{sx, sy, 0.004f}, s.scale))), S.head.position,
                   0.55f * kCardWm * s.scale, sign_rgb(sp));
    CLOG("autograph: landed on its card");
  }
}
// its copy in the air (its quad, in the model's frame this frame: fq / fat / fsc - the card's, or its cameo's)
void sign_air(Special& sp, bool front, uint64_t now, float dt, const XrQuaternionf& fq, const XrVector3f& fat, float fsc) {
  if (sp.airBase < 0 || static_cast<size_t>(sp.airBase + 4) * 6 > sp.verts.size()) return;
  float* o = &sp.verts[static_cast<size_t>(sp.airBase) * 6];
  if (!front || sp.signDone || sp.airNs == 0 || now <= sp.airNs || sp.i < 0 || sp.i >= kSheets) {
    std::memset(o, 0, sizeof(float) * 24);
    return;
  }
  const float u = secs(now, sp.airNs), W = air_write(sp), wEnd = kAirBuild + W;
  const Sheet& s = S.card[sp.i];
  const XrQuaternionf q = sheet_q(s);
  const float sc = std::max(1e-3f, s.scale);
  const XrVector3f& head = S.head.position;
  const float airH = kAirW * static_cast<float>(kSignH) / kSignW;
  // where it hangs: just over the card in your hand as you see it (a BLACK's figure stepped down into the card meanwhile),
  // between your chest and your eyes, 0.5-0.95 m out - in the room, eased after the card (never after your head)
  const XrVector3f cu = q_rot(q, XrVector3f{0.0f, 0.5f * kCardHm * sc, 0.0f}), cr = q_rot(q, XrVector3f{0.5f * kCardWm * sc, 0.0f, 0.0f});
  const float top = s.pos.y + std::fabs(cu.y) + std::fabs(cr.y);
  XrVector3f d{s.pos.x - head.x, 0.0f, s.pos.z - head.z};
  float dl = v_len(d);
  if (dl < 0.05f) {
    const XrVector3f f = q_rot(S.head.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
    d = XrVector3f{f.x, 0.0f, f.z};
    dl = std::max(1e-3f, v_len(d));
  }
  d = v_mul(d, 1.0f / dl);
  const float dist = clampf(dl + 0.06f, 0.5f, 0.95f);
  // (just over the card AS YOU SEE IT: its bottom 2 degrees over the card's top edge along your eye's line - it's further
  // out than the card, so a plain height would put it up high in your view)
  const float eTop = std::atan2(top - head.y, std::max(0.05f, dl)), half = std::atan2(0.5f * airH, dist);
  const float cy = clampf(head.y + dist * std::tan(clampf(eTop + 0.035f + half, -1.2f, 0.3f)), head.y - 0.45f, head.y - 0.05f);
  const XrVector3f want{head.x + d.x * dist, cy, head.z + d.z * dist};
  if (!sp.airPosOk) {
    sp.airPos = want;
    sp.airPosOk = true;
  } else {
    sp.airPos = v_lerp(sp.airPos, want, std::min(1.0f, dt * 2.0f));
  }
  const XrVector3f c = v_add(sp.airPos, XrVector3f{0.0f, 0.005f * std::sin(u * 1.7f), 0.0f});
  const XrQuaternionf aq = facing(c);
  const XrVector3f R = q_rot(aq, XrVector3f{0.5f * kAirW, 0.0f, 0.0f}), U = q_rot(aq, XrVector3f{0.0f, 0.5f * airH, 0.0f});
  const XrVector3f air[4] = {v_sub(v_sub(c, R), U), v_sub(v_add(c, R), U), v_add(v_add(c, R), U), v_add(v_sub(c, R), U)};
  // the card's autograph (as special_up put it), in the room
  const lk::SignBox sb = lk::sign_box(sp.set, sp.foot);  // r217
  const float sx0 = (sb.x0 / kCW - 0.5f) * kCardWm, sx1 = ((sb.x0 + sb.w) / kCW - 0.5f) * kCardWm;
  const float sy1 = (0.5f - sb.y0 / kCH) * kCardHm, sy0 = (0.5f - (sb.y0 + sb.h) / kCH) * kCardHm;
  const float z = (sp.kind == kSpGlass ? 1.5f * kCardT + 0.0012f : 0.5f * kCardT + 0.001f) + 0.0006f;
  const XrVector3f onCard[4] = {{sx0, sy0, z}, {sx1, sy0, z}, {sx1, sy1, z}, {sx0, sy1, z}};
  const float e = smooth(clampf((u - wEnd - kAirHang) / kAirFly, 0.0f, 1.0f));
  // (its normals' x, xr_fx_model.frag: 9.45 .. 9.95 gathering, 10 + its progress writing, 11.05 + seconds since written)
  const float x = u < kAirBuild ? 9.45f + 0.5f * (u / kAirBuild)
                                : (u < wEnd ? 10.0f + 1.05f * (u - kAirBuild) / W : 11.05f + std::min(5.0f, u - wEnd));
  XrVector3f wc[4];
  for (int k = 0; k < 4; ++k) {
    wc[k] = v_lerp(air[k], v_add(s.pos, q_rot(q, v_mul(onCard[k], sc))), e);
    const XrVector3f l = v_mul(q_rot(q_conj(fq), v_sub(wc[k], fat)), 1.0f / std::max(1e-4f, fsc));  // (into the model's frame)
    float* v = o + k * 6;
    v[0] = l.x, v[1] = l.y, v[2] = l.z;
    v[3] = x, v[4] = sp.signY, v[5] = sp.signZ;
  }
  // a point of it (its box: bx 0..1 across, by 0..1 down) in the room
  auto box = [&](float bx, float by) { return v_add(wc[3], v_add(v_mul(v_sub(wc[2], wc[3]), bx), v_mul(v_sub(wc[0], wc[3]), by))); };
  const uint32_t ink = sign_rgb(sp);
#if __has_include("xr_sign.h")
  const xrsign::Signature* sg = static_cast<const xrsign::Signature*>(sp.sig);
#else
  const void* sg = nullptr;
#endif
  if (u < kAirBuild) {  // light drawing in where its pen will start
    XrVector3f p0 = box(0.15f, 0.5f);
#if __has_include("xr_sign.h")
    if (sg != nullptr && !sg->strokes.empty() && !sg->strokes[0].pts.empty()) p0 = box(sg->strokes[0].pts[0].x, sg->strokes[0].pts[0].y);
#endif
    const float k = u / kAirBuild;
    xrfx::ink_gather(p0, 0.18f * (1.0f - 0.4f * k), 30.0f + 110.0f * k, dt, 0xFFFFFF, ink);
  } else if (u < wEnd) {  // its pen tip: sparks dropping off it
#if __has_include("xr_sign.h")
    if (sg != nullptr) {
      const float st = (u - kAirBuild) / W * 1.05f * sg->duration;
      const xrsign::Point* tip = nullptr;
      for (const xrsign::Stroke& k : sg->strokes)
        for (const xrsign::Point& p : k.pts)
          if (p.t <= st) tip = &p;
      if (tip != nullptr && st - tip->t < 0.05f) xrfx::ink_sparks(box(tip->x, tip->y), 70.0f, dt, 0xFFF8E0, ink);
    }
#endif
  } else if (u < wEnd + kAirHang) {  // written: twinkles over its foil
    xrfx::card_sparkle(v_sub(c, U), 0.42f * kAirW, 2.0f * airH, 16.0f, dt, 0xFFFFFF, ink);
  } else {  // flying down: sparks trailing it
    xrfx::ink_sparks(box(0.5f, 0.5f), 90.0f * (1.0f - e) + 10.0f, dt, 0xFFF8E0, ink);
  }
}

void special_moves(Special& sp, bool faceUp, bool held, float t, uint64_t now, float dt, const XrQuaternionf& fq,
                   const XrVector3f& fat, float fsc) {
  if (sp.popBase >= 0 && static_cast<size_t>(sp.popBase + 4 * sp.pops) * 6 <= sp.verts.size()) {
    // r150: its show - played once as it's first face up (out of the binder: with its music; turned over in a pack, the
    // reveal started both); again whenever it's shaken in your hand (back and forth twice, fast)
    const Sheet& s = S.card[sp.i];
    if (sp.lastNs != 0 && now > sp.lastNs && held && faceUp) {
      const float dt = secs(now, sp.lastNs);
      const XrVector3f v = v_mul(v_sub(s.pos, sp.lastPos), 1.0f / std::max(1e-3f, dt));
      const float speed = v_len(v);
      if (speed > 0.8f) {
        const XrVector3f dir = v_mul(v, 1.0f / speed);
        if (sp.fastNs != 0 && secs(now, sp.fastNs) < 0.45f &&
            dir.x * sp.fastDir.x + dir.y * sp.fastDir.y + dir.z * sp.fastDir.z < -0.3f) {
          if (sp.flipNs == 0 || secs(now, sp.flipNs) > 1.0f) sp.flips = 0;
          ++sp.flips;
          sp.flipNs = now;
        }
        sp.fastDir = dir;
        sp.fastNs = now;
      }
      if (sp.flips >= 2 && sp.boomNs == 0) {
        sp.flips = 0;
        sp.boomNs = now;
        play_wav("boom", 1.0f);
        buzz(0.5f, 0.12f);
        CLOG("TIZM BOOM shaken: its show again");
      }
    }
    sp.lastPos = s.pos;
    sp.lastNs = now;
    if (faceUp && sp.boomNs == 0 && !sp.boomDone && sp.i >= 0 && sp.i < kSheets && !S.boomShown[sp.i]) {
      sp.boomNs = now;
      S.boomShown[sp.i] = true;
      if (S.mode != kPackUp) play_wav("boom", 1.0f);  // (out of the binder: its music now)
    }
    const float u = sp.boomNs != 0 && now >= sp.boomNs ? secs(now, sp.boomNs) : -1.0f;
    // (over: played out, or turned face down again - not the moment it's still coming round as its reveal starts it)
    if (u > kBoomLen || (!faceUp && u > 0.4f)) {
      sp.boomNs = 0;
      sp.boomDone = true;
    }
    for (int o = 0; o < sp.pops; ++o) {
      float* v = &sp.verts[static_cast<size_t>(sp.popBase + 4 * o) * 6];
      XrVector3f c;
      float w = 0.0f, h = 0.0f, rot = 0.0f;
      if (!faceUp || sp.boomNs == 0 || u < 0.0f || !boom_place(o, u, t, &c, &w, &h, &rot)) {  // r166
        std::memset(v, 0, sizeof(float) * 24);
        continue;
      }
      const float cs = std::cos(rot), sn = std::sin(rot);
      static const float kX[4] = {-1, 1, 1, -1}, kY[4] = {-1, -1, 1, 1};
      for (int q = 0; q < 4; ++q) {
        const float x = 0.5f * kX[q] * w, y = 0.5f * kY[q] * h;
        v[q * 6 + 0] = c.x + x * cs - y * sn;
        v[q * 6 + 1] = c.y + x * sn + y * cs;
        v[q * 6 + 2] = c.z;
        v[q * 6 + 3] = 0.0f;
        v[q * 6 + 4] = 0.0f;
        v[q * 6 + 5] = 0.75f;
      }
    }
  }
  if (sp.signBase >= 0 && static_cast<size_t>(sp.signBase + 4) * 6 <= sp.verts.size()) {
    // r193: its moment (sign_moment); on the card nothing until it lands (-0.5), then 1.05 + seconds since it landed (its
    // landing's light sweep, then its shimmer for good: xr_fx_model.frag); out of the binder written already (8.05)
    sign_moment(sp, faceUp, now, dt);
    const float prog = !sp.signDone ? -0.5f : 1.05f + (sp.landNs != 0 ? std::min(7.0f, secs(now, sp.landNs)) : 7.0f);
    for (int q = 0; q < 4; ++q) {
      float* v = &sp.verts[static_cast<size_t>(sp.signBase + q) * 6];
      v[3] = prog;
      v[4] = sp.signY;
      v[5] = sp.signZ;
    }
    sign_air(sp, faceUp, now, dt, fq, fat, fsc);  // r179 / r193
  }
}

// ===================================================================================== r149: GOD PACK
// (Lance: "lets do a crazy, greek mythology ass "celebration" graphic ... a "god pack"") A pack whose five cards are all
// SILVER or better / case hits (add_packs: its own 1 in kGodOdds roll makes one). At the rip its tell (godtell.wav, a
// gold flash, the room dimmed a moment, a thunderbolt); after its last card's moment the show (kGodLen s; A / B / a
// trigger skips it): the room goes dark, the clouds ahead part, Olympus' gold light pours down, eight marble columns
// rise round you, thunderbolts, a laurel wreath round GOD PACK in gold, gold glitter; godpack.wav (choir, orchestra,
// thunder) its loudest moment on the words. One model in the 1/1 world's slot (kFxSlot; that world waits for it): light
// and shade quads (xr_fx_model.frag -4.5: a quad's "normal" its colour, negative a shade); its pictures made here (no
// image files) on a thread as the pack comes up, kept for the next one.
enum { kGtFlat, kGtCloud, kGtGlow, kGtShaft, kGtColumn, kGtBolt, kGtTitle, kGtCount };
struct GodPics {
  std::mutex m;
  bool done = false;
  std::vector<std::vector<uint8_t>> tex;
  std::vector<int> tw, th;
};
std::shared_ptr<GodPics> g_godPics;
constexpr int kGodClouds = 14, kGodCols = 8, kGodSegs = 8, kGodBolts = 4;
constexpr float kGodLen = 7.6f, kGodTellLen = 1.4f, kGodWait = 1.3f, kGodWords = 3.0f, kGodSkip = 0.35f;
constexpr float kGodColAz[kGodCols] = {-24, 24, -62, 62, -104, 104, -148, 148};  // (degrees off ahead, + your right)
constexpr float kGodBoltAt[kGodBolts] = {2.2f, 2.75f, 3.0f, 4.4f}, kGodBoltAz[kGodBolts] = {-42, 44, 8, -78};
struct GodShow {
  int stage = 0;    // 0 none, 1 its tell, 2 its show waiting (t0: when), 3 its show
  bool up = false;  // its model in kFxSlot
  bool placed = false, flash = false;
  uint64_t t0 = 0;
  bool skip = false;
  float fade = 0.0f;  // (skipped: its fade's time left)
  XrVector3f c{0.0f, 0.0f, 0.0f}, fwd{0.0f, 0.0f, -1.0f}, right{1.0f, 0.0f, 0.0f};
  float floor = 0.0f;
  int dome = 0, sun = 0, shaft = 0, clouds = 0, cols = 0, bolts = 0, halo = 0, title = 0;
  std::vector<float> verts;
  int bolted = 0;
  bool words = false, sounded = false;
  // its music's start, the words' moment (its music's loudest: 3 - 4.5 s), its length (the words held, then gone)
  float soundAt = kGodWords, wordsAt = kGodWords, len = kGodLen;
  float cloudA[kGodClouds] = {}, cloudR[kGodClouds] = {}, cloudS[kGodClouds] = {};
};
GodShow g_god;

float god_ss(float a, float b, float x) {
  const float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
float god_noise(float x, float y, uint32_t seed) {
  auto h = [seed](int i, int j) {
    uint32_t n = static_cast<uint32_t>(i) * 374761393u + static_cast<uint32_t>(j) * 668265263u + seed * 2246822519u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return static_cast<float>((n ^ (n >> 16)) & 0xFFFFu) / 65535.0f;
  };
  const int i = static_cast<int>(std::floor(x)), j = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(i), fy = y - static_cast<float>(j);
  const float sx = fx * fx * (3.0f - 2.0f * fx), sy = fy * fy * (3.0f - 2.0f * fy);
  const float a = h(i, j), b = h(i + 1, j), c = h(i, j + 1), d = h(i + 1, j + 1);
  return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}
float god_fbm(float x, float y, uint32_t seed) {
  float s = 0.0f, a = 0.5f, n = 0.0f;
  for (int k = 0; k < 4; ++k) {
    s += a * god_noise(x, y, seed + static_cast<uint32_t>(k));
    n += a;
    x *= 2.03f;
    y *= 2.03f;
    a *= 0.5f;
  }
  return s / n;
}
// (premultiplied: its light is its colour, its shade its coverage)
void god_px(std::vector<uint8_t>& px, size_t i, float r, float g, float b, float a) {
  a = clampf(a, 0.0f, 1.0f);
  px[i * 4 + 0] = static_cast<uint8_t>(std::lround(clampf(r, 0.0f, 1.0f) * a * 255.0f));
  px[i * 4 + 1] = static_cast<uint8_t>(std::lround(clampf(g, 0.0f, 1.0f) * a * 255.0f));
  px[i * 4 + 2] = static_cast<uint8_t>(std::lround(clampf(b, 0.0f, 1.0f) * a * 255.0f));
  px[i * 4 + 3] = static_cast<uint8_t>(std::lround(a * 255.0f));
}
// a cumulus: lumps, ragged at the edges, lit from above (blue-grey under)
std::vector<uint8_t> god_cloud() {
  const int n = 256;
  std::vector<uint8_t> px(static_cast<size_t>(n) * n * 4, 0);
  struct B {
    float x, y, rx, ry;
  };
  const B bl[] = {{0.0f, 0.12f, 0.55f, 0.36f},   {-0.42f, 0.22f, 0.38f, 0.27f}, {0.43f, 0.2f, 0.4f, 0.28f}, {-0.14f, -0.16f, 0.36f, 0.33f},
                  {0.22f, -0.2f, 0.34f, 0.3f}, {-0.66f, 0.33f, 0.26f, 0.18f}, {0.67f, 0.31f, 0.26f, 0.18f}};
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const float u = (x + 0.5f) / n * 2.0f - 1.0f, v = (y + 0.5f) / n * 2.0f - 1.0f;
      float d = 0.0f;
      for (const B& b : bl) {
        const float dx = (u - b.x) / b.rx, dy = (v - b.y) / b.ry;
        d += std::exp(-(dx * dx + dy * dy) * 1.6f);
      }
      const float nz = god_fbm(u * 2.6f + 3.1f, v * 2.6f, 11);
      d += 0.55f * (nz - 0.5f);
      const float a = god_ss(0.32f, 0.7f, d) * (1.0f - god_ss(0.86f, 1.0f, std::max(std::fabs(u), std::fabs(v))));
      if (a <= 0.0f) continue;
      const float lit = clampf(0.5f - 0.5f * v + 0.35f * (nz - 0.5f) + 0.25f * god_ss(0.7f, 1.4f, d), 0.0f, 1.0f);
      god_px(px, static_cast<size_t>(y) * n + x, 0.42f + 0.58f * lit, 0.45f + 0.52f * lit, 0.56f + 0.36f * lit, a);
    }
  return px;
}
std::vector<uint8_t> god_glow() {
  const int n = 256;  // (stretched over 2.6 m: 128 px stepped in rings)
  std::vector<uint8_t> px(static_cast<size_t>(n) * n * 4, 0);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const float u = (x + 0.5f) / n * 2.0f - 1.0f, v = (y + 0.5f) / n * 2.0f - 1.0f, r = std::sqrt(u * u + v * v);
      god_px(px, static_cast<size_t>(y) * n + x, 1.0f, 1.0f, 1.0f, std::exp(-r * r * 4.5f) * (1.0f - god_ss(0.8f, 1.0f, r)));
    }
  return px;
}
// the light pouring down: brightest at its top, streaked, fading to nothing at its foot
std::vector<uint8_t> god_shaft() {
  const int w = 128, h = 512;
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float u = (x + 0.5f) / w * 2.0f - 1.0f, v = (y + 0.5f) / h;
      const float streak = 0.7f + 0.3f * god_noise(u * 9.0f, v * 1.2f, 5);
      const float a = std::exp(-u * u * 5.0f) * (1.0f - god_ss(0.75f, 1.0f, std::fabs(u))) * god_ss(0.0f, 0.06f, v) * (1.0f - 0.6f * v) *
                      (1.0f - god_ss(0.85f, 1.0f, v)) * streak;
      god_px(px, static_cast<size_t>(y) * w + x, 1.0f, 1.0f, 1.0f, a);
    }
  return px;
}
// a marble column (1 : 4): its entablature, abacus, echinus, gold necking, fluted shaft in drums with entasis, gold
// torus, plinth; white marble with grey and gold veins, lit from the front, Olympus' gold catching its right side
std::vector<uint8_t> god_column() {
  const int w = 256, h = 1024;
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
  const float L[2] = {0.45f, 0.82f};  // (the light across, toward you)
  for (int y = 0; y < h; ++y) {
    const float Y = (y + 0.5f) / h;
    float hw = 0.0f;
    int part = 1;  // 0 a slab, 1 round, 2 its fluted shaft
    bool gold = false;
    if (Y < 0.035f) {
      hw = 0.47f, part = 0;
    } else if (Y < 0.065f) {
      hw = 0.43f, part = 0, gold = Y > 0.058f;
    } else if (Y < 0.11f) {
      const float s = (Y - 0.065f) / 0.045f;
      hw = 0.31f + 0.11f * (1.0f - s) * (1.0f - s);
    } else if (Y < 0.125f) {
      hw = 0.315f, gold = true;
    } else if (Y < 0.89f) {
      const float s = (Y - 0.125f) / 0.765f;
      hw = 0.285f + 0.03f * s + 0.008f * std::sin(3.14159265f * s), part = 2;
    } else if (Y < 0.915f) {
      const float s = (Y - 0.89f) / 0.025f;
      hw = 0.33f + 0.04f * std::sqrt(std::max(0.0f, 1.0f - (2.0f * s - 1.0f) * (2.0f * s - 1.0f))), gold = true;
    } else if (Y < 0.94f) {
      hw = 0.33f;
    } else {
      hw = 0.46f, part = 0;
    }
    for (int x = 0; x < w; ++x) {
      const float X = (x + 0.5f) / w - 0.5f;
      const float cov = clampf((hw - std::fabs(X)) * w + 0.5f, 0.0f, 1.0f);
      if (cov <= 0.0f) continue;
      const float nx = clampf(X / hw, -0.999f, 0.999f), th = std::asin(nx);
      float shade, rim = god_ss(0.55f, 1.0f, nx);
      if (part == 0) {
        shade = 0.84f + 0.1f * nx - 0.28f * god_ss(0.86f, 1.0f, std::fabs(nx));
        if (Y < 0.035f && Y > 0.03f) shade *= 0.72f;  // (its entablature's lower edge)
        rim *= 0.5f;
      } else {
        const float tf = part == 2 ? th + 0.32f * std::sin(th * 20.0f) : th;  // (flutes: 20 round it)
        shade = 0.3f + 0.78f * std::max(0.0f, L[0] * std::sin(tf) + L[1] * std::cos(tf));
        if (part == 2) {
          shade *= 0.9f + 0.1f * std::cos(th * 20.0f);
          if (std::fmod(Y - 0.125f, 0.153f) < 0.0028f) shade *= 0.72f;  // (its drums' joints)
        }
      }
      if (Y > 0.985f) shade *= 0.75f;
      float r, g, b;
      if (gold) {
        const float spec = std::pow(std::max(0.0f, std::cos(th - 0.5f)), 10.0f);
        r = 0.98f * shade + 0.5f * spec, g = 0.76f * shade + 0.42f * spec, b = 0.34f * shade + 0.25f * spec;
      } else {
        const float v1 = god_fbm(th * 0.9f + 13.0f, Y * 16.0f, 7), v2 = god_fbm(th * 0.7f + 50.0f, Y * 9.0f, 9);
        const float vein = 1.0f - god_ss(0.0f, 0.035f, std::fabs(v1 - 0.5f)), gv = 1.0f - god_ss(0.0f, 0.02f, std::fabs(v2 - 0.5f));
        r = 0.95f - 0.28f * vein, g = 0.93f - 0.27f * vein, b = 0.89f - 0.25f * vein;
        r += (0.95f - r) * 0.45f * gv, g += (0.76f - g) * 0.45f * gv, b += (0.38f - b) * 0.45f * gv;
        r *= shade, g *= shade, b *= shade;
      }
      r += 0.38f * rim, g += 0.3f * rim, b += 0.14f * rim;
      god_px(px, static_cast<size_t>(y) * w + x, r, g, b, cov);
    }
  }
  return px;
}
// a thunderbolt (1 : 8): a jagged white-hot bolt (midpoint displacement) with three forks, a gold-white glow round it
std::vector<uint8_t> god_bolt() {
  const int w = 128, h = 1024;
  struct Seg {
    float x0, y0, x1, y1, k;
  };
  std::vector<Seg> segs;
  uint64_t rng = 0xB0175ull;
  auto zig = [&](float x0, float y0, float x1, float y1, float disp, int levels, float k) {
    std::vector<float> xs{x0, x1}, ys{y0, y1};
    for (int l = 0; l < levels; ++l) {
      std::vector<float> nx, ny;
      for (size_t i = 0; i + 1 < xs.size(); ++i) {
        nx.push_back(xs[i]);
        ny.push_back(ys[i]);
        nx.push_back(clampf(0.5f * (xs[i] + xs[i + 1]) + (fx_r(rng) - 0.5f) * disp, 12.0f, w - 12.0f));
        ny.push_back(0.5f * (ys[i] + ys[i + 1]) + (fx_r(rng) - 0.5f) * disp * 0.2f);
      }
      nx.push_back(xs.back());
      ny.push_back(ys.back());
      xs.swap(nx);
      ys.swap(ny);
      disp *= 0.56f;
    }
    for (size_t i = 0; i + 1 < xs.size(); ++i) segs.push_back({xs[i], ys[i], xs[i + 1], ys[i + 1], k});
    return std::make_pair(xs, ys);
  };
  const auto trunk = zig(64.0f, 6.0f, 60.0f, h - 6.0f, 90.0f, 7, 1.0f);
  for (int b = 0; b < 3; ++b) {
    const size_t at = trunk.first.size() * static_cast<size_t>(2 + 2 * b) / 10;
    const float x0 = trunk.first[at], y0 = trunk.second[at], dir = b % 2 == 0 ? 1.0f : -1.0f;
    zig(x0, y0, clampf(x0 + dir * (30.0f + 20.0f * fx_r(rng)), 10.0f, w - 10.0f), y0 + 150.0f + 110.0f * fx_r(rng), 36.0f, 5, 0.55f);
  }
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4, 0);
  for (int y = 0; y < h; ++y) {
    const float py = y + 0.5f, V = py / h;
    for (int x = 0; x < w; ++x) {
      const float pxx = x + 0.5f;
      float d = 1e9f;
      for (const Seg& s : segs) {
        if (py < std::min(s.y0, s.y1) - 60.0f || py > std::max(s.y0, s.y1) + 60.0f) continue;
        const float vx = s.x1 - s.x0, vy = s.y1 - s.y0, l2 = vx * vx + vy * vy;
        const float t = l2 > 0.0f ? clampf(((pxx - s.x0) * vx + (py - s.y0) * vy) / l2, 0.0f, 1.0f) : 0.0f;
        const float dx = pxx - (s.x0 + vx * t), dy = py - (s.y0 + vy * t);
        d = std::min(d, std::sqrt(dx * dx + dy * dy) / s.k);
      }
      const float core = 1.0f - god_ss(0.9f, 2.4f, d), glow = 0.7f * std::exp(-d / 6.0f) + 0.32f * std::exp(-d / 20.0f);
      const float ends = god_ss(0.0f, 0.03f, V) * (1.0f - god_ss(0.97f, 1.0f, V));
      const size_t i = static_cast<size_t>(y) * w + x;
      px[i * 4 + 0] = static_cast<uint8_t>(std::lround(clampf((core + glow) * ends, 0.0f, 1.0f) * 255.0f));
      px[i * 4 + 1] = static_cast<uint8_t>(std::lround(clampf((core + 0.88f * glow) * ends, 0.0f, 1.0f) * 255.0f));
      px[i * 4 + 2] = static_cast<uint8_t>(std::lround(clampf((core + 0.62f * glow) * ends, 0.0f, 1.0f) * 255.0f));
      px[i * 4 + 3] = static_cast<uint8_t>(std::lround(clampf((core + glow) * ends, 0.0f, 1.0f) * 255.0f));
    }
  }
  return px;
}
// GOD PACK (2 : 1): gold letters, BLESSED BY OLYMPUS, Greek key bands, a gold laurel wreath round it all
std::vector<uint8_t> god_title() {
  const int w = 1024, h = 512;
  Pc P;
  auto poly = [&](const std::vector<float>& xy, uint32_t rgb) {
    xrtext::BoardPiece p;
    p.kind = xrtext::BoardPiece::kPoly;
    p.pts = xy;
    p.rgb = rgb;
    p.alpha = 1.0f;
    P.v.push_back(p);
  };
  auto leaf = [&](float bx, float by, float ang, float len, float wid, uint32_t rgb) {
    std::vector<float> xy;
    const float ca = std::cos(ang), sa = std::sin(ang);
    for (int s = 0; s <= 16; ++s) {
      const int k = s <= 8 ? s : 16 - s;
      const float u = k / 8.0f, side = s <= 8 ? 1.0f : -1.0f;
      const float along = u * len, across = side * wid * 0.5f * std::pow(std::sin(3.14159265f * u), 0.75f);
      xy.push_back(bx + along * ca - across * sa);
      xy.push_back(by + along * sa + across * ca);
    }
    poly(xy, rgb);
  };
  // the wreath: two branches from its foot up either side, open at its top
  constexpr float kCx = 512.0f, kCy = 250.0f, kRx = 470.0f, kRy = 225.0f;
  for (int side = 0; side < 2; ++side) {
    auto at = [&](float deg, float* x, float* y, float* ang) {
      const float th = deg * 3.14159265f / 180.0f;
      *x = kCx + kRx * std::cos(th);
      *y = kCy + kRy * std::sin(th);
      float tx = -kRx * std::sin(th), ty = kRy * std::cos(th);
      if (side == 1) {
        *x = 1024.0f - *x;
        tx = -tx;
      }
      *ang = std::atan2(ty, tx);
    };
    for (float deg = 100.0f; deg <= 226.0f; deg += 3.0f) {  // its stem
      float x, y, a;
      at(deg, &x, &y, &a);
      P.box(x - 5.0f, y - 5.0f, x + 5.0f, y + 5.0f, 5.0f, 0x8A5E14);
    }
    for (int k = 0; k <= 13; ++k) {
      float x, y, a;
      at(100.0f + k * 9.5f, &x, &y, &a);
      const float len = 64.0f * (1.0f - 0.38f * k / 13.0f), wid = 24.0f * (1.0f - 0.3f * k / 13.0f);
      for (int o = 0; o < 2; ++o) {
        const float la = a + (o == 0 ? 0.62f : -0.62f);
        leaf(x - 3.0f * std::cos(la), y - 3.0f * std::sin(la), la, len + 8.0f, wid + 7.0f, 0x5C3A0A);
        leaf(x, y, la, len, wid, (k + o) % 2 != 0 ? 0xF2C94C : 0xD9A632);
      }
    }
    float x, y, a;
    at(229.0f, &x, &y, &a);
    leaf(x, y, a, 50.0f, 20.0f, 0xF2C94C);  // (its tip)
  }
  P.box(486, 458, 538, 494, 12, 0x5C3A0A);  // its tie
  P.box(490, 462, 534, 490, 10, 0xE0A82E);
  // the Greek key bands
  auto seg = [&](float x0, float y0, float x1, float y1) {
    const float t = 3.2f;
    P.box(std::min(x0, x1) - t, std::min(y0, y1) - t, std::max(x0, x1) + t, std::max(y0, y1) + t, 0.0f, 0xE6B23A);
  };
  for (int band = 0; band < 2; ++band) {
    const int n = band == 0 ? 17 : 13;
    const float u = 34.0f, y0 = band == 0 ? 30.0f : 352.0f, q = u / 4.0f, x0 = 512.0f - n * u * 0.5f;
    seg(x0, y0 + u, x0 + n * u, y0 + u);
    static const float kKey[] = {0, 4, 0, 0, 3, 0, 3, 2.6f, 1.4f, 2.6f, 1.4f, 1.4f};
    for (int i = 0; i < n; ++i)
      for (int k = 0; k + 3 < 12; k += 2) seg(x0 + i * u + kKey[k] * q, y0 + kKey[k + 1] * q, x0 + i * u + kKey[k + 2] * q, y0 + kKey[k + 3] * q);
  }
  P.style(0.0f, 1.06f, 0.0f, 6.0f, 0x2A1600, 0.8f);
  P.rim(0x3D2606);
  P.text("GOD PACK", 512, 215, 132, 720, 0xFFF3BE, 9, 0xD4891A);
  P.plain();
  P.rim(0x3D2606);
  P.text("BLESSED BY OLYMPUS", 512, 318, 40, 560, 0xFFE7A0, 4);
  std::vector<uint8_t> px = board_px(w, h, P);
  for (size_t i = 0; i < px.size(); i += 4)  // (premultiplied)
    for (int k = 0; k < 3; ++k) px[i + k] = static_cast<uint8_t>(px[i + k] * px[i + 3] / 255);
  return px;
}

void god_prepare() {
  if (g_godPics) return;  // (drawn, or being drawn: kept for the next one)
  auto p = std::make_shared<GodPics>();
  g_godPics = p;
  std::thread([p]() {
    std::vector<std::vector<uint8_t>> tex(kGtCount);
    std::vector<int> tw(kGtCount), th(kGtCount);
    tex[kGtFlat].assign(8 * 8 * 4, 255);
    tw[kGtFlat] = th[kGtFlat] = 8;
    tex[kGtCloud] = god_cloud();
    tw[kGtCloud] = th[kGtCloud] = 256;
    tex[kGtGlow] = god_glow();
    tw[kGtGlow] = th[kGtGlow] = 256;
    tex[kGtShaft] = god_shaft();
    tw[kGtShaft] = 128;
    th[kGtShaft] = 512;
    tex[kGtColumn] = god_column();
    tw[kGtColumn] = 256;
    th[kGtColumn] = 1024;
    tex[kGtBolt] = god_bolt();
    tw[kGtBolt] = 128;
    th[kGtBolt] = 1024;
    tex[kGtTitle] = god_title();
    tw[kGtTitle] = 1024;
    th[kGtTitle] = 512;
    std::lock_guard<std::mutex> lock(p->m);
    p->tex = std::move(tex);
    p->tw = std::move(tw);
    p->th = std::move(th);
    p->done = true;
    CLOG("god pack: its pictures drawn");
  }).detach();
}

// its model (render thread): the dark's sphere, the sun, its light, the clouds, the columns (each in kGodSegs slices,
// so it can rise out of the floor), the bolts, the words' halo, the words
bool god_model_up(bool fxReady) {
  GodShow& g = g_god;
  if (!fxReady || !g_godPics) return false;
  {
    std::lock_guard<std::mutex> lock(g_godPics->m);
    if (!g_godPics->done) return false;
  }
  const GodPics& P = *g_godPics;  // (done: read only from here)
  xrfx::Model m;
  for (int k = 0; k < kGtCount; ++k) {
    m.texRgba.push_back(P.tex[k]);
    m.texW.push_back(static_cast<uint32_t>(P.tw[k]));
    m.texH.push_back(static_cast<uint32_t>(P.th[k]));
  }
  int verts = 0;
  auto quad = [&](float u0, float v0, float u1, float v1) {
    const float us[4] = {u0, u1, u0, u1}, vs[4] = {v0, v0, v1, v1};
    for (int k = 0; k < 4; ++k) {
      m.uv.push_back(us[k]);
      m.uv.push_back(vs[k]);
    }
    const uint32_t a = static_cast<uint32_t>(verts);
    m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    verts += 4;
    return static_cast<int>(a);
  };
  auto part = [&](int tex, size_t first) {
    xrfx::ModelPart p;
    p.tex = tex;
    p.soft = true;
    p.firstIndex = static_cast<uint32_t>(first);
    p.indexCount = static_cast<uint32_t>(m.indices.size() - first);
    m.parts.push_back(p);
  };
  size_t first = m.indices.size();
  g.dome = verts;
  for (int i = 0; i <= 6; ++i)
    for (int j = 0; j <= 10; ++j) {
      m.uv.push_back(0.5f);
      m.uv.push_back(0.5f);
    }
  for (int i = 0; i < 6; ++i)
    for (int j = 0; j < 10; ++j) {
      const uint32_t a = static_cast<uint32_t>(verts + i * 11 + j), b = a + 1, c = a + 11, d = c + 1;
      m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
    }
  verts += 77;
  part(kGtFlat, first);
  first = m.indices.size();
  g.sun = quad(0, 0, 1, 1);
  quad(0, 0, 1, 1);
  part(kGtGlow, first);
  first = m.indices.size();
  g.shaft = quad(0, 0, 1, 1);
  part(kGtShaft, first);
  first = m.indices.size();
  g.clouds = verts;
  for (int j = 0; j < kGodClouds; ++j)
    for (int k = 0; k < 2; ++k) quad(j % 2 != 0 ? 1.0f : 0.0f, 0.0f, j % 2 != 0 ? 0.0f : 1.0f, 1.0f);
  part(kGtCloud, first);
  first = m.indices.size();
  g.cols = verts;
  for (int c = 0; c < kGodCols; ++c)
    for (int k = 0; k < 2; ++k)
      for (int s = 0; s < kGodSegs; ++s) quad(0.0f, static_cast<float>(s) / kGodSegs, 1.0f, static_cast<float>(s + 1) / kGodSegs);
  part(kGtColumn, first);
  first = m.indices.size();
  g.bolts = verts;
  for (int b = 0; b < kGodBolts; ++b) quad(b % 2 != 0 ? 1.0f : 0.0f, 0.0f, b % 2 != 0 ? 0.0f : 1.0f, 1.0f);
  part(kGtBolt, first);
  first = m.indices.size();
  g.halo = quad(0, 0, 1, 1);
  part(kGtGlow, first);
  first = m.indices.size();
  g.title = quad(0, 0, 1, 1);
  quad(0, 0, 1, 1);
  part(kGtTitle, first);
  m.vertexCount = static_cast<uint32_t>(verts);
  g.verts.assign(static_cast<size_t>(verts) * 6, 0.0f);
  xrfx::set_model(kFxSlot, std::move(m));
  g.up = xrfx::model_up(kFxSlot);
  CLOG("god pack: its model up%s (%d vertices)", g.up ? "" : " - failed", verts);
  return g.up;
}

// a quad's corners (top left, top right, bottom left, bottom right), lit `rgb` x k - or a shade, k its darkness
void god_put(int base, const XrVector3f& tl, const XrVector3f& tr, const XrVector3f& bl, const XrVector3f& br, uint32_t rgb, float k,
             bool shade) {
  float* o = g_god.verts.data() + static_cast<size_t>(base) * 6;
  if (k <= 0.002f) {
    std::fill(o, o + 24, 0.0f);
    return;
  }
  float r = ((rgb >> 16) & 255) / 255.0f * k, g = ((rgb >> 8) & 255) / 255.0f * k, b = (rgb & 255) / 255.0f * k;
  if (shade) r = g = b = -std::min(1.0f, k) - 1e-4f;
  const XrVector3f p[4] = {tl, tr, bl, br};
  for (int i = 0; i < 4; ++i) {
    float* v = o + i * 6;
    v[0] = p[i].x;
    v[1] = p[i].y;
    v[2] = p[i].z;
    v[3] = r;
    v[4] = g;
    v[5] = b;
  }
}
void god_bill(int base, const XrVector3f& c, const XrVector3f& right, const XrVector3f& up, float w, float h, uint32_t rgb, float k,
              bool shade = false) {
  const XrVector3f hr = v_mul(right, 0.5f * w), hu = v_mul(up, 0.5f * h);
  god_put(base, v_add(v_sub(c, hr), hu), v_add(v_add(c, hr), hu), v_sub(v_sub(c, hr), hu), v_sub(v_add(c, hr), hu), rgb, k, shade);
}
// turned to your eyes: its right and up
void god_axes(const XrVector3f& at, XrVector3f* right, XrVector3f* up) {
  XrVector3f d = v_sub(at, S.head.position);
  const float l = v_len(d);
  d = l > 1e-4f ? v_mul(d, 1.0f / l) : g_god.fwd;
  XrVector3f r = v_cross(d, XrVector3f{0.0f, 1.0f, 0.0f});
  const float rl = v_len(r);
  r = rl > 1e-3f ? v_mul(r, 1.0f / rl) : g_god.right;
  *right = r;
  *up = v_cross(r, d);
}
// a thing standing on the floor turned to you (a column, a bolt): its right
XrVector3f god_upright(const XrVector3f& at) {
  XrVector3f d = v_sub(at, S.head.position);
  d.y = 0.0f;
  const float l = v_len(d);
  return l > 1e-3f ? v_cross(v_mul(d, 1.0f / l), XrVector3f{0.0f, 1.0f, 0.0f}) : g_god.right;
}
void god_place(uint64_t now, int stage) {
  GodShow& g = g_god;
  g.stage = stage;
  g.t0 = now;
  g.placed = true;
  g.skip = false;
  g.fade = 0.0f;
  g.bolted = 0;
  g.words = g.sounded = false;
  g.c = S.head.position;
  XrVector3f f = q_rot(S.head.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
  const float fl = std::sqrt(f.x * f.x + f.z * f.z);
  g.fwd = fl > 1e-3f ? XrVector3f{f.x / fl, 0.0f, f.z / fl} : XrVector3f{0.0f, 0.0f, -1.0f};
  g.right = {-g.fwd.z, 0.0f, g.fwd.x};
  g.floor = g_floorY > -1.0e8f ? g_floorY : g.c.y - 1.6f;
  uint64_t rng = 0x60D5ull + static_cast<uint64_t>(S.packData.serial) * 0x9E3779B97F4A7C15ull;
  for (int j = 0; j < kGodClouds; ++j) {
    g.cloudA[j] = 6.2831853f * (j + 0.35f * fx_r(rng)) / kGodClouds;
    g.cloudR[j] = 0.2f + 0.5f * fx_r(rng);
    g.cloudS[j] = 1.0f + 0.5f * fx_r(rng);
  }
  // the words on its music's loudest moment (godpack.wav: its 50 ms of most energy - 4.0 s in today's), 3 - 4.5 s in;
  // its length to hold them (a fallback fanfare: the words at kGodWords)
  g.soundAt = kGodWords - 0.2f;
  g.wordsAt = kGodWords;
  const std::vector<short>& pcm = card_wav("godpack");
  if (!pcm.empty() && stage == 3) {
    constexpr int kWin = 1600;  // (50 ms)
    double best = -1.0;
    size_t bestAt = 0;
    for (size_t s = 0; s + kWin <= pcm.size(); s += kWin / 2) {
      double e = 0.0;
      for (size_t i = s; i < s + kWin; ++i) e += static_cast<double>(pcm[i]) * pcm[i];
      if (e > best) best = e, bestAt = s;
    }
    const float peak = static_cast<float>(bestAt) / 32000.0f;
    g.wordsAt = clampf(peak, kGodWords, 4.5f);
    g.soundAt = std::max(0.0f, g.wordsAt - peak);
  }
  g.len = std::max(kGodLen, g.wordsAt + 3.8f);
}
void god_tell(uint64_t now) {
  if (!play_wav("godtell", 1.0f)) PortEvoMusic(1);
  buzz(1.0f, 0.3f);
  god_place(now, 1);
  g_god.flash = true;
  CLOG("god pack: its tell at the rip");
}
void god_last(uint64_t now) {
  if (g_god.stage == 2 || g_god.stage == 3) return;
  g_god.stage = 2;
  g_god.t0 = now + static_cast<uint64_t>(kGodWait * 1e9f);
  CLOG("god pack: every card seen, its show next");
}
bool god_live() { return g_god.stage == 3 && !g_god.skip; }
void god_skip() {
  if (g_god.stage == 3 && !g_god.skip) {
    g_god.skip = true;
    g_god.fade = kGodSkip;
    CLOG("god pack: its show skipped");
  }
}
void god_end(bool fxReady) {
  if (g_god.up && fxReady) xrfx::set_model(kFxSlot, xrfx::Model{});
  g_god = GodShow{};
}

// Every frame (specials_frame): the tell / the show; true while it has kFxSlot (the 1/1 world kept out)
bool god_frame(xrfx::Scene& scene, float dt, uint64_t now, bool fxReady) {
  GodShow& g = g_god;
  if (g.stage == 0) return false;
  if (g.stage == 2) {
    if (cameo_live()) g.t0 = std::max<uint64_t>(g.t0, now + 600000000ull);  // (a 1/1's cameo first)
    if (now < g.t0 || S.mode != kPackUp) return false;
    god_place(now, 3);
    CLOG("god pack: its show");
  }
  if (!g.up) {
    if (g_fx.up) fx_end(fxReady);
    god_model_up(fxReady);
  }
  const float t = secs(now, g.t0), len = g.stage == 1 ? kGodTellLen : g.len;
  if (g.skip) g.fade -= dt;
  if (t >= len || (g.skip && g.fade <= 0.0f)) {
    if (g.stage == 3) {
      S.godDone = true;
      // r219 (BIG PULLS): the gods hand over to the casino - a GOD PACK's JACKPOT party as its Olympus show clears (never stacked on its
      // sky dome: both whole-room layers)
      if (S.mode == kPackUp) {
        xrgoalfx::pull_party(xrpulls::kPartyJackpot, "GOD PACK", S.pack.pos, S.head, pull_floor());
        play_wav("casino_coins_pour_tray", 0.75f);
        CLOG("god pack: its JACKPOT party");
      }
    }
    god_end(fxReady);  // (the 1/1 world back next frame)
    return false;
  }
  if (g.flash) {
    g.flash = false;
    xrfx::screen_flash(1.0f, 0.86f, 0.5f, 0.3f);
  }
  if (!g.up || g.verts.empty()) return true;  // (its pictures still being drawn: the slot kept)
  const XrVector3f Y{0.0f, 1.0f, 0.0f};
  const float M = g.stage == 1 ? 1.0f : (1.0f - god_ss(g.len - 1.0f, g.len, t)) * (g.skip ? clampf(g.fade / kGodSkip, 0.0f, 1.0f) : 1.0f);
  const float sunK = g.stage == 3 ? god_ss(1.0f, 2.6f, t) * M : 0.0f;
  // the dark round you
  {
    const float dark = g.stage == 1 ? 0.5f * (t < 0.1f ? t / 0.1f : 1.0f - god_ss(0.1f, kGodTellLen, t)) : 0.72f * god_ss(0.0f, 0.9f, t) * M;
    float* o = g.verts.data() + static_cast<size_t>(g.dome) * 6;
    for (int i = 0; i <= 6; ++i)
      for (int j = 0; j <= 10; ++j) {
        const float th = 3.14159265f * i / 6.0f, ph = 6.2831853f * j / 10.0f;
        float* v = o + (i * 11 + j) * 6;
        const XrVector3f d{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
        const bool on = dark > 0.003f;
        v[0] = on ? g.c.x + d.x * 3.4f : 0.0f;
        v[1] = on ? g.c.y + d.y * 3.4f : 0.0f;
        v[2] = on ? g.c.z + d.z * 3.4f : 0.0f;
        v[3] = v[4] = v[5] = on ? -dark - 1e-4f : 0.0f;
      }
  }
  // the sun, the clouds parting before it, its light pouring down
  const XrVector3f sun = v_add(v_add(g.c, v_mul(g.fwd, 2.5f)), v_mul(Y, 1.25f));
  {
    XrVector3f r, u;
    god_axes(sun, &r, &u);
    const float pulse = 1.0f + 0.05f * std::sin(t * 2.1f);
    god_bill(g.sun, sun, r, u, 2.6f * pulse, 2.6f * pulse, 0xFFC25A, 0.8f * sunK);
    god_bill(g.sun + 4, sun, r, u, 0.95f, 0.95f, 0xFFF4D8, 1.15f * sunK);
    XrVector3f d = v_sub(sun, g.c);
    d = v_mul(d, 1.0f / std::max(1e-3f, v_len(d)));
    const XrVector3f cc = v_add(g.c, v_mul(d, 2.3f));
    const float open = god_ss(0.9f, 2.9f, t), fin = god_ss(0.15f, 0.9f, t);
    for (int j = 0; j < kGodClouds; ++j) {
      const int base = g.clouds + j * 8;
      if (g.stage != 3) {
        god_put(base, {}, {}, {}, {}, 0, 0.0f, false);
        god_put(base + 4, {}, {}, {}, {}, 0, 0.0f, false);
        continue;
      }
      const float rr = g.cloudR[j] + 2.6f * open;
      const XrVector3f at = v_add(v_add(cc, v_mul(r, std::cos(g.cloudA[j]) * rr)), v_mul(u, std::sin(g.cloudA[j]) * rr * 0.62f));
      const float a = fin * (1.0f - 0.7f * god_ss(0.5f, 1.0f, open)) * M, s = g.cloudS[j] * (1.0f + 0.25f * open);
      const float lit = sunK * (0.55f + 0.45f * (1.0f - clampf(rr / 3.0f, 0.0f, 1.0f)));
      god_bill(base, at, r, u, s, 0.68f * s, 0, 0.55f * a, true);
      god_bill(base + 4, at, r, u, s, 0.68f * s, mix_rgb(0x5A6178, 0xFFD58A, lit), 0.85f * a);
    }
    const XrVector3f foot{g.c.x + g.fwd.x * 0.9f, g.floor, g.c.z + g.fwd.z * 0.9f};
    const XrVector3f axis = v_sub(foot, sun), mid = v_lerp(sun, foot, 0.5f);
    XrVector3f side = v_cross(axis, v_sub(S.head.position, mid));
    side = v_mul(side, 1.0f / std::max(1e-4f, v_len(side)));
    const float shaftK = g.stage == 3 ? god_ss(1.5f, 2.9f, t) * M * 0.5f * (0.9f + 0.1f * std::sin(t * 3.1f)) : 0.0f;
    god_put(g.shaft, v_sub(sun, v_mul(side, 0.22f)), v_add(sun, v_mul(side, 0.22f)), v_sub(foot, v_mul(side, 0.75f)),
            v_add(foot, v_mul(side, 0.75f)), 0xFFE2A0, shaftK, false);
    if (fxReady && g.stage == 3 && t > 1.8f && t < g.len - 0.8f && M > 0.3f)
      xrfx::card_sparkle(v_lerp(sun, foot, 0.55f), 0.3f, 1.0f, 36.0f, dt, 0xFFD24A, 0xFFF0C0);
  }
  // the columns rising out of the floor round you
  {
    constexpr float kH = 2.75f, kW = kH * 0.25f, kR = 1.85f;
    bool rising = false;
    for (int c = 0; c < kGodCols; ++c) {
      const int base = g.cols + c * 2 * kGodSegs * 4;
      const float az = kGodColAz[c] * 3.14159265f / 180.0f;
      const XrVector3f dir = v_add(v_mul(g.fwd, std::cos(az)), v_mul(g.right, std::sin(az)));
      const XrVector3f at{g.c.x + dir.x * kR, g.floor, g.c.z + dir.z * kR};
      const float u = g.stage == 3 ? clampf((t - (1.0f + 0.22f * (c / 2))) / 1.35f, 0.0f, 1.0f) : 0.0f;
      const float p = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u), top = g.floor + kH * p;
      const float fin = god_ss(0.0f, 0.15f, u) * M;
      const XrVector3f rt = v_mul(god_upright(at), 0.5f * kW);
      const uint32_t tint = mix_rgb(0xE8E4DC, 0xFFE8C0, sunK);
      if (u > 0.0f && u < 1.0f) {
        rising = true;
        if (fxReady) xrfx::card_sparkle(at, 0.25f, 0.12f, 24.0f, dt, 0xFFD24A, 0xFFF4D0);
      }
      for (int s = 0; s < kGodSegs; ++s) {
        float y0 = top - s * kH / kGodSegs;
        const float y1 = std::max(g.floor, y0 - kH / kGodSegs);
        const bool show = u > 0.0f && y0 > g.floor + 0.005f;
        for (int k = 0; k < 2; ++k) {
          const int q = base + (k * kGodSegs + s) * 4;
          if (!show) {
            god_put(q, {}, {}, {}, {}, 0, 0.0f, false);
            continue;
          }
          const XrVector3f a{at.x, y0, at.z}, b{at.x, y1, at.z};
          god_put(q, v_sub(a, rt), v_add(a, rt), v_sub(b, rt), v_add(b, rt), tint, k == 0 ? 0.93f * fin : fin, k == 0);
        }
      }
    }
    if (rising) buzz(0.22f, 0.08f);
  }
  // the thunderbolts (the tell: one)
  for (int b = 0; b < kGodBolts; ++b) {
    const int q = g.bolts + b * 4;
    // (the third on the words, the last after them)
    const float when = g.stage == 1 ? 0.03f : (b == 2 ? g.wordsAt : (b == 3 ? g.wordsAt + 1.4f : kGodBoltAt[b]));
    const float age = t - when;
    if ((g.stage == 1 && b > 0) || age < 0.0f || age > 0.38f || M < 0.05f) {
      god_put(q, {}, {}, {}, {}, 0, 0.0f, false);
      continue;
    }
    if (b >= g.bolted) {
      g.bolted = b + 1;
      if (g.stage == 3) xrfx::screen_flash(1.0f, 0.93f, 0.75f, 0.24f);
      buzz(0.85f, 0.22f);
    }
    const float az = (g.stage == 1 ? 32.0f : kGodBoltAz[b]) * 3.14159265f / 180.0f, rr = g.stage == 1 ? 1.7f : 2.35f;
    const XrVector3f dir = v_add(v_mul(g.fwd, std::cos(az)), v_mul(g.right, std::sin(az)));
    const XrVector3f top{g.c.x + dir.x * rr, g.c.y + 2.3f, g.c.z + dir.z * rr}, bot{top.x, g.floor, top.z};
    const XrVector3f rt = v_mul(god_upright(top), 0.275f);
    const float k = (age < 0.06f ? 1.0f : (age < 0.1f ? 0.2f : 1.0f - god_ss(0.1f, 0.38f, age))) * 1.3f * M;
    god_put(q, v_sub(top, rt), v_add(top, rt), v_sub(bot, rt), v_add(bot, rt), 0xFFFFFF, k, false);
  }
  // the words: GOD PACK in its wreath, punching in on the music's biggest moment
  {
    const float tt = t - g.wordsAt;
    const XrVector3f at = v_add(v_add(g.c, v_mul(g.fwd, 1.6f)), v_mul(Y, 0.22f));
    if (g.stage != 3 || tt < 0.0f) {
      god_put(g.halo, {}, {}, {}, {}, 0, 0.0f, false);
      god_put(g.title, {}, {}, {}, {}, 0, 0.0f, false);
      god_put(g.title + 4, {}, {}, {}, {}, 0, 0.0f, false);
    } else {
      if (!g.words) {
        g.words = true;
        S.burst = true;
        S.burstRgb = kGold;
        S.burstAt = v_sub(at, v_mul(Y, 0.25f));
        buzz(1.0f, 0.5f);
      }
      const float sc = tt < 0.22f ? 0.35f + 0.77f * smooth(tt / 0.22f)
                                  : (tt < 0.5f ? 1.12f - 0.12f * smooth((tt - 0.22f) / 0.28f) : 1.0f + 0.012f * std::sin(tt * 2.2f));
      const float fin = god_ss(0.0f, 0.12f, tt) * M;
      XrVector3f r, u;
      god_axes(at, &r, &u);
      god_bill(g.halo, v_add(at, v_mul(g.fwd, 0.03f)), r, u, 2.4f * sc, 1.5f * sc, 0xFFB845, 0.55f * fin * (1.0f + 0.08f * std::sin(t * 2.6f)));
      god_bill(g.title, at, r, u, 1.3f * sc, 0.65f * sc, 0, 0.85f * fin, true);
      god_bill(g.title + 4, at, r, u, 1.3f * sc, 0.65f * sc, 0xFFFFFF, 1.1f * fin);
      if (fxReady && tt < 3.6f && M > 0.3f) xrfx::card_sparkle(at, 0.62f, 0.32f, 60.0f, dt, kGold, 0xFFFFFF);
    }
  }
  // its music
  if (g.stage == 3 && !g.sounded && t >= g.soundAt && M > 0.5f) {
    g.sounded = true;
    if (!play_wav("godpack", 1.0f)) PortEvoMusic(1);
  }
  xrfx::Scene::ModelDraw& md = scene.models[kFxSlot];
  md.on = true;
  md.posNrm = g.verts.data();
  md.local = false;
  md.white = 0.0f;
  md.alpha = 1.0f;
  md.glow = 0.0f;
  md.under = -4.5f;  // (light and shade, each quad tinted: xr_fx_model.frag)
  md.glowRgb = 0xFFFFFF;
  md.flameFrame = -1;
  md.shine = 0.0f;
  md.frame = -1;
  md.kaleido = 0u;
  return true;
}

// Every frame: the specials' models up when ready, placed in their cards' frames (their figures posed, the spotlight's
// strength), the light (auras before the flip, smoke, the beam, the dimming), the sparkles. `hide[i]`: card i drawn by
// its special (the cards' own model leaves it out).
// r171: a HOMETOWN's diorama this frame - its pop-up (the first time it's turned landscape, face up - or after 2.5 s face up
// anyway: its things rising out of the ground back to front like a pop-up book's, overshooting a touch, with its sound),
// then alive: grass and leaves swaying, water rippling (its light moving on it), clouds and birds drifting across (wings
// flapping), floating things bobbing, fog rolling, light shafts breathing, its particles drifting / falling / rising /
// wandering and twinkling. Into its vertices, turned onto the card. (`q` the card's turn in the room)
void home_moves(Special& sp, bool faceUp, const XrQuaternionf& q, float t, uint64_t now, float dt) {
  if (sp.homeBase < 0 || sp.homeN <= 0 || sp.homeRest.size() < static_cast<size_t>(sp.homeN) * 6) return;
  if (sp.homeNs == 0) {
    const XrVector3f homeUp = q_rot(q, XrVector3f{-1.0f, 0.0f, 0.0f});  // (its landscape's up: the card's -x)
    sp.homeWait = faceUp ? sp.homeWait + dt : 0.0f;
    if (faceUp && (homeUp.y > 0.55f || sp.homeWait > 2.5f)) {
      sp.homeNs = now;
      play_wav("hometown", 0.8f);
      CLOG("HOMETOWN: its diorama pops up%s", homeUp.y > 0.55f ? " (turned landscape)" : "");
    }
  }
  const float T = sp.homeNs != 0 && now >= sp.homeNs ? secs(now, sp.homeNs) : -1.0f;
  float* out = sp.verts.data() + static_cast<size_t>(sp.homeBase) * 6;
  const XrVector3f lo = sp.homeLo, hi = sp.homeHi;
  auto wrap = [](float v, float a, float b) { return a + std::fmod(std::fmod(v - a, b - a) + (b - a), b - a); };
  for (int i = 0; i < sp.homeN; ++i) {
    const float* r = &sp.homeRest[static_cast<size_t>(i) * 6];
    const float* a = &sp.homeAnim[static_cast<size_t>(i) * 6];
    XrVector3f p{r[0], r[1], r[2]}, n{r[3], r[4], r[5]};
    const int kind = static_cast<int>(a[0]);
    const float pivot = a[1], delay = a[2];
    switch (kind) {
      case kHaSway: {
        const float w = clampf((p.y - pivot) / std::max(1e-4f, a[4]), 0.0f, 1.0f);
        p.x += a[3] * std::sin(t * 1.7f + a[5] + p.x * 30.0f) * w * w;
        p.z += 0.4f * a[3] * std::sin(t * 1.3f + a[5]) * w * w;
        break;
      }
      case kHaWater: {
        const float k1 = 90.0f, k2 = 120.0f;
        p.y += 0.0007f * std::sin(p.x * k1 + t * 2.2f) + 0.0005f * std::sin(p.z * k2 - t * 1.7f + p.x * 30.0f);
        const float dx = 0.0007f * k1 * std::cos(p.x * k1 + t * 2.2f) + 0.0005f * 30.0f * std::cos(p.z * k2 - t * 1.7f + p.x * 30.0f);
        const float dz = 0.0005f * k2 * std::cos(p.z * k2 - t * 1.7f + p.x * 30.0f);
        const float nl = v_len(n);
        XrVector3f wn{-dx, 1.0f, -dz};
        n = v_mul(wn, nl / v_len(wn));
        break;
      }
      case kHaDrift:
      case kHaBird: {
        const float cx = a[3], off = wrap(cx + a[4] * t, lo.x - 0.01f, hi.x + 0.01f) - cx;
        p.x += off;
        if (kind == kHaDrift) p.y += 0.0008f * std::sin(t * 0.7f + a[5]);
        else {
          p.y += 0.002f * std::sin(t * 0.9f + cx * 50.0f);
          if (a[5] > 0.0f) p.y += 0.0018f * std::sin(t * 12.0f + a[5]);  // (its wings)
        }
        break;
      }
      case kHaBob:
        p.y += a[3] * std::sin(t * 1.4f + a[5]);
        break;
      case kHaFog:
        p.x += a[3] * std::sin(t * 0.22f + a[5]);
        break;
      case kHaShaft:
        p.x = a[3] + (p.x - a[3]) * (1.0f + 0.3f * std::sin(t * 0.8f + a[5]));
        break;
      case kHaParticle: {
        const int k = static_cast<int>(a[3]);
        if (k < 0 || k >= static_cast<int>(sp.homeFx.size())) break;
        const HomeParticle& f = sp.homeFx[static_cast<size_t>(k)];
        XrVector3f c{f.p.x + f.vx * t + 0.003f * std::sin(t * 0.9f + f.phase), f.p.y + f.vy * t, f.p.z + 0.002f * std::sin(t * 0.7f + f.phase * 2.0f)};
        float s = f.size;
        if (f.kind == kHfFireflies) {  // (wandering, blinking)
          c.x = f.p.x + 0.008f * std::sin(t * 0.5f + f.phase);
          c.y = f.p.y + 0.006f * std::sin(t * 0.7f + f.phase * 1.3f);
          s *= 0.3f + 0.7f * std::pow(0.5f + 0.5f * std::sin(t * 2.3f + f.phase * 3.0f), 3.0f);
        } else if (f.kind == kHfSparkles) {
          s *= std::pow(std::max(0.0f, std::sin(t * 2.0f + f.phase * 4.0f)), 4.0f);
        }
        c.x = wrap(c.x, lo.x, hi.x);
        c.y = wrap(c.y, lo.y, hi.y);
        const float rot = t * (f.kind == kHfLeaves || f.kind == kHfPetals ? 2.0f : 0.6f) + f.phase;
        static const float kCx[4] = {-1, 1, 1, -1}, kCy[4] = {-1, -1, 1, 1};
        static const int kOf[6] = {0, 1, 2, 0, 2, 3};
        const int corner = kOf[std::max(0, std::min(5, static_cast<int>(a[4])))];
        const float ux = kCx[corner] * s, uy = kCy[corner] * s * (f.kind == kHfLeaves || f.kind == kHfPetals ? 0.6f : 1.0f);
        p = {c.x + ux * std::cos(rot) - uy * std::sin(rot), c.y + ux * std::sin(rot) + uy * std::cos(rot), c.z};
        if (T < 0.6f) p = {0.0f, -1.0f, -0.05f};  // (none till it's up)
        break;
      }
      default:
        break;
    }
    // its pop-up: rising out of its hinge (the ground under it), back ones first, overshooting a touch
    if (delay > -5.0f) {
      const float u = T < 0.0f ? 0.0f : (T - delay) / 0.38f;
      const float f = u <= 0.0f ? 0.0f : (u >= 1.0f ? 1.0f : pp_back(u, 1.4f));
      p.y = pivot + (p.y - pivot) * std::max(0.0f, f);
      if (f < 0.02f && kind != kHaWater) p.z = std::min(p.z, -0.002f);
    }
    const XrVector3f pc = home_to_card(p), nc = home_to_card(n);
    float* o = out + static_cast<size_t>(i) * 6;
    o[0] = pc.x, o[1] = pc.y, o[2] = pc.z, o[3] = nc.x, o[4] = nc.y, o[5] = nc.z;
  }
}
// r171 (Lance: "when it comes to the gold cards, those need to be way more shiny on the inside of the card when its 3D aspect
// shows"): a GOLD's vault this frame - light sweeping across its polished walls and floor, a halo of gold light behind its
// figure while it's in there, a pool of light under it, twinkles winking on the gold. Into its quads (gold_fx_quads).
void gold_fx_moves(Special& sp, bool faceUp, float t, const GoldPose& gp) {
  if (sp.goldFxBase < 0) return;
  float* o = sp.verts.data() + static_cast<size_t>(sp.goldFxBase) * 6;
  const float D = kRoomD, x0 = -kWinHalf, x1 = kWinHalf, y0 = kWinBottom, y1 = kWinTop;
  const float open = faceUp ? 1.0f - sp.goldShut : 0.0f;
  int q = 0;
  auto put = [&](const XrVector3f c[4], const XrVector3f& n) {
    for (int k = 0; k < 4; ++k) {
      float* v = o + static_cast<size_t>(q * 4 + k) * 6;
      const XrVector3f p = open > 0.01f ? c[k] : XrVector3f{0.0f, 0.0f, -0.01f};
      v[0] = p.x, v[1] = p.y, v[2] = p.z, v[3] = n.x, v[4] = n.y, v[5] = n.z;
    }
    ++q;
  };
  // the sweeps: across the back wall, along the floor, down each side wall (each its own pace)
  {
    const float u = std::fmod(t / 2.6f, 1.0f), x = x0 - 0.03f + (x1 - x0 + 0.06f) * u, w = 0.014f;
    const XrVector3f c[4] = {{x - w, y0, -D + 0.0008f}, {x + w, y0, -D + 0.0008f}, {x + w + 0.012f, y1, -D + 0.0008f}, {x - w + 0.012f, y1, -D + 0.0008f}};
    put(c, XrVector3f{0, 0, 1});
  }
  {
    const float u = std::fmod(t / 3.1f + 0.4f, 1.0f), z = 0.01f - (D + 0.03f) * u, w = 0.008f;
    const XrVector3f c[4] = {{x0, y0 + 0.0006f, z + w}, {x1, y0 + 0.0006f, z + w}, {x1, y0 + 0.0006f, z - w}, {x0, y0 + 0.0006f, z - w}};
    put(c, XrVector3f{0, 1, 0});
  }
  for (int s = -1; s <= 1; s += 2) {
    const float u = std::fmod(t / 3.7f + (s > 0 ? 0.5f : 0.0f), 1.0f), y = y1 + 0.02f - (y1 - y0 + 0.04f) * u, w = 0.01f;
    const float x = s < 0 ? x0 + 0.0008f : x1 - 0.0008f;
    const XrVector3f c[4] = {{x, y - w, 0.0f}, {x, y - w, -D}, {x, y + w - 0.01f, -D}, {x, y + w, 0.0f}};
    put(c, XrVector3f{-static_cast<float>(s), 0, 0});
  }
  // the halo behind its figure (while it's in its room) and the pool of light under it
  {
    const float in = gp.fig && gp.top < 0.5f ? 1.0f : 0.0f;
    const float h = std::max(0.02f, sp.fit.scale * sp.fit.bh) * 1.5f * in * (1.0f + 0.05f * std::sin(t * 2.0f));
    const XrVector3f f = gp.feet;
    const float cy = f.y + 0.4f * h, z = std::max(-D + 0.002f, f.z - 0.012f);
    const XrVector3f c[4] = {{f.x - 0.5f * h, cy - 0.5f * h, z}, {f.x + 0.5f * h, cy - 0.5f * h, z}, {f.x + 0.5f * h, cy + 0.5f * h, z}, {f.x - 0.5f * h, cy + 0.5f * h, z}};
    put(c, XrVector3f{0, 0, 1});
    const float r = 0.032f * in * (1.0f + 0.06f * std::sin(t * 1.6f)), py = y0 + 0.0009f;
    const XrVector3f pl[4] = {{f.x - r, py, f.z + r}, {f.x + r, py, f.z + r}, {f.x + r, py, f.z - r}, {f.x - r, py, f.z - r}};
    put(pl, XrVector3f{0, 1, 0});
  }
  // twinkles winking on the gold (each its own place and beat)
  for (int k = 0; k < kGoldTwinkles; ++k) {
    const float ph = pp_h01(static_cast<uint32_t>(k) * 97u + 5u), sp2 = 0.7f + 0.8f * pp_h01(static_cast<uint32_t>(k) * 31u + 9u);
    const float w = std::pow(std::max(0.0f, std::sin(t * sp2 * 2.2f + ph * 6.28f)), 6.0f), s = 0.006f * w;
    XrVector3f c, n;
    const int wall = k % 3;
    const float u = pp_h01(static_cast<uint32_t>(k) * 13u + 1u), v = pp_h01(static_cast<uint32_t>(k) * 17u + 2u);
    if (wall == 0) c = {x0 + (x1 - x0) * u, y0 + (y1 - y0) * v, -D + 0.0012f}, n = {0, 0, 0.75f};
    else if (wall == 1) c = {x0 + (x1 - x0) * u, y0 + 0.0012f, -D * v}, n = {0, 0.75f, 0};
    else c = {k % 2 ? x0 + 0.0012f : x1 - 0.0012f, y0 + (y1 - y0) * v, -D * u}, n = {k % 2 ? 0.75f : -0.75f, 0, 0};
    const float rot = t * 0.8f + ph * 6.0f;
    XrVector3f ax, ay;
    if (wall == 0) ax = {std::cos(rot) * s, std::sin(rot) * s, 0}, ay = {-std::sin(rot) * s, std::cos(rot) * s, 0};
    else if (wall == 1) ax = {std::cos(rot) * s, 0, std::sin(rot) * s}, ay = {-std::sin(rot) * s, 0, std::cos(rot) * s};
    else ax = {0, std::cos(rot) * s, std::sin(rot) * s}, ay = {0, -std::sin(rot) * s, std::cos(rot) * s};
    const XrVector3f cc[4] = {v_sub(v_sub(c, ax), ay), v_sub(v_add(c, ax), ay), v_add(v_add(c, ax), ay), v_add(v_sub(c, ax), ay)};
    put(cc, n);
  }
}

void specials_frame(xrfx::Scene& scene, float dt, uint64_t now, bool fxReady, bool hide[kSheets]) {
  for (int i = 0; i < kSheets; ++i) hide[i] = false;
  if (!fxReady) return;
  const float t = static_cast<float>(static_cast<double>(now % 1000000000000ull) * 1e-9);
  float dim = 0.0f;
  // r141: the BLACK 1/1 here (its world: its card's frame, face up, held)
  int fxK = -1;
  bool fxFaceUp = false, fxHeld = false;
  XrQuaternionf fxQ{0.0f, 0.0f, 0.0f, 1.0f};
  XrVector3f fxPos{0.0f, 0.0f, 0.0f};
  float fxScale = 1.0f, fxTop = kThroneFig;
  bool camSeen = false;  // (its cameo's special still here)
  for (int k = 0; k < kMaxSpecials; ++k) {
    Special& sp = g_sp[k];
    if (sp.i < 0 || sp.i >= S.n) continue;
    if (!sp.up && sp.build) {
      bool done;
      {
        std::lock_guard<std::mutex> lock(sp.build->m);
        done = sp.build->done;
      }
      if (done) special_up(sp, kSpecialSlot0 + k);
      if (!sp.up) {
        if (done) sp.build.reset();
        continue;
      }
    }
    if (!sp.up) continue;
    // r181: its celebration ready (made after the card came up): swapped in for its standing figure - the same pack and
    // vertices, the same fit (by its standing pose) - so it carries on where it stands, now with its own big move
    if (sp.build && sp.fig != nullptr) {
      xrevo::Figure* cf = nullptr;
      uint32_t cv = 0;
      {
        std::lock_guard<std::mutex> lock(sp.build->m);
        cf = sp.build->celFig;
        cv = sp.build->celVerts;
        sp.build->celFig = nullptr;
      }
      if (cf != nullptr) {
        if (static_cast<int>(cv) == sp.figCount) {
          xrevo::release(sp.fig);
          sp.fig = cf;
          live_moves(sp);  // (r193: their clean stretches)
          uint32_t ps = 0, pn = 0;
          if (sp.kind == kSpPop && card_move(sp.fig, {2u, 5u}, &ps, &pn)) {  // (a GOLD's pop-out loop: its big move)
            sp.loopStart = static_cast<int>(ps);
            sp.loopFrames = static_cast<int>(pn);
          }
          // r193: from the still it held while this was made, blended in over kBlendIn (no pop)
          sp.blendFrom = sp.posed;
          sp.blendT = 0.0f;
          sp.celPending = false;
          CLOG("special: card %d's celebration swapped in: idle %u frames, big move %u frames", sp.i, sp.liveIdleFrames, sp.liveBigFrames);
        } else {
          xrevo::release(cf);
        }
      }
      if (sp.celPending) {  // r193: none came (failed / none to make): its standing loop after all, blended in
        std::lock_guard<std::mutex> lock(sp.build->m);
        if (sp.build->celTried && sp.build->celFig == nullptr) {
          sp.celPending = false;
          sp.blendFrom = sp.posed;
          sp.blendT = 0.0f;
        }
      }
    }
    if (sp.shellN > 0) {  // r220: THE PUNCH on its shell - the crest before, snapping to its die (kPunchMorph), its die after
      const int pi = sp.i;
      const int st = S.punchNs[pi] == 0 ? 0 : (S.punchNs[pi] == kPunchedLongAgo || secs(now, S.punchNs[pi]) >= xrpulls::kPunchMorph ? 2 : 1);
      std::vector<float>& dst = sp.statCopy.empty() ? sp.verts : sp.statCopy;
      if (dst.size() >= sp.shellPost.size() && (st != sp.shellState || st == 1)) {
        const std::vector<float>& src = st == 0 ? sp.shellPre : sp.shellPost;
        std::copy(src.begin(), src.end(), dst.begin());
        if (st == 1) {
          Sheet unit;
          unit.w = kCardWm;
          unit.h = kCardHm;
          std::vector<float> tmp(static_cast<size_t>(kCardVerts) * 6);
          card_box(tmp.data(), unit, XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f}, XrVector3f{0.0f, 0.0f, 0.0f}, 1.0f, true,
                   sp.kind == kSpGlass ? 3.0f * kCardT : kCardT, 2.0f, sp.shape, punch_morph_of(pi, now));
          std::copy(tmp.begin() + 2 * kCardNV * 6, tmp.end(), dst.begin() + 2 * kCardNV * 6);
        }
        sp.shellState = st;
      }
    }
    const Sheet& s = S.card[sp.i];
    const bool show = s.on && s.shown && s.scale > 0.01f && special_live(sp.i);
    const XrQuaternionf q = sheet_q(s);
    // r193 (Lance: the 1/1's platform "was visible before I flipped the card over - looked dumb"): face up = turned over
    // AND its face toward your eyes (held turned round by the wrist counts as face down too) - its 3D, its world on the
    // card, its effects only then (xr_fx_model.frag also cuts its 3D whenever your eye is behind its face)
    const XrVector3f faceN = q_rot(q, XrVector3f{0.0f, 0.0f, 1.0f}), faceToEye = v_sub(S.head.position, s.pos);
    const bool faceUp = std::cos(s.flip) > 0.2f && faceN.x * faceToEye.x + faceN.y * faceToEye.y + faceN.z * faceToEye.z > 0.0f;
    sp.reveal = faceUp && show ? std::min(1.0f, sp.reveal + dt / 0.45f) : 0.0f;  // (its 3D on top rising out of its edge)
    const bool held = s.hands != 0 || (S.mode == kPackUp && S.pack.hands != 0);  // (r139: the stack in your hand)
    if (sp.kind == kSpGlass) {  // r141: its world (after these), fading as it goes
      fxK = k;
      fxFaceUp = show && faceUp;
      fxHeld = show && held;
      fxQ = q;
      fxPos = s.pos;
      fxScale = s.scale;
      fxTop = kPedH + (sp.fig != nullptr ? sp.fit.scale * sp.fit.bh : 0.8f * kThroneFig);
    }
    // r141: its cameo (a BLACK 1/1 turned over for the first time): started once its figure's here and it's face up;
    // over when it's back on its throne and the room's light is back, or at once if its card goes
    float camE = -1.0f;  // (>= 0: out of its card, this far)
    if (sp.kind == kSpGlass && g_cam.i == sp.i) {
      Cameo& c = g_cam;
      camSeen = true;
      // (r219: after a dark build-up's reveal, not before the party's peak: pull_gate)
      if (!c.started && sp.fig != nullptr && sp.figCount > 0 && show && faceUp && now >= g_pullGateNs) {
        c.started = true;
        {
          std::lock_guard<std::mutex> lock(g_mutex);
          g_cameoSeen.insert(c.key);  // (r217: the card's id - cameo_ask was given it)
          save_collection();
        }
        XrVector3f fwd = q_rot(S.head.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
        const float fl = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
        fwd = fl > 1e-3f ? XrVector3f{fwd.x / fl, 0.0f, fwd.z / fl} : XrVector3f{0.0f, 0.0f, -1.0f};
        const float floor = g_floorY > -1.0e8f ? g_floorY : S.head.position.y - 1.6f;
        c.spot = {S.head.position.x + fwd.x * kCamDist, floor, S.head.position.z + fwd.z * kCamDist};
        const float fps = std::max(1.0f, xrevo::fps(sp.fig));
        // (r193: with its autograph's moment after it, kept to 4.5 s)
        c.celeb = sp.loopFrames > 0 ? clampf((sp.loopFrames + sp.fit.bigFrames) / fps, 3.0f, sp.signedCard ? 4.5f : 7.0f) : 4.0f;
        c.backAt = kCamDim + kCamOut + c.celeb;
        buzz(0.5f, 0.25f);
        CLOG("cameo: %s, %.1f s of celebration, %.2f m ahead", c.key.c_str(), c.celeb, kCamDist);
      } else if (!c.started && sp.up && sp.fig == nullptr) {
        c = Cameo{};  // (no figure on this headset: no cameo)
      }
      if (c.started) {
        c.t += dt;
        // while it's out of its card, its card shows flat (the cards' model): the throne's shell, slab, glass and
        // pedestal put by, back as it lands on its throne again
        const bool out = c.t >= kCamDim && c.t < c.backAt + kCamBack && show && !cameo_done(c);
        const size_t nStat = static_cast<size_t>(sp.figBase) * 6;
        if (out && sp.statCopy.empty()) {
          sp.statCopy.assign(sp.verts.begin(), sp.verts.begin() + nStat);
          std::fill(sp.verts.begin(), sp.verts.begin() + nStat, 0.0f);
        } else if (!out && !sp.statCopy.empty()) {
          if (sp.statCopy.size() == nStat) std::copy(sp.statCopy.begin(), sp.statCopy.end(), sp.verts.begin());
          sp.statCopy.clear();
        }
        if (!show || cameo_done(c)) {
          c = Cameo{};
        } else {
          float cd = 0.0f;
          const float e = cameo_e(c, &cd);
          camE = out ? e : -1.0f;
          dim = std::max(dim, cd * pull_gate(now));  // (r219: eased in after a dark build-up's party)
          const float out1 = kCamDim + kCamOut;
          if (!c.landed && c.t >= out1 && c.t < c.backAt) {  // there, life size: its celebration, its voice
            c.landed = true;
            xrfx::evo_burst(scene, v_add(c.spot, XrVector3f{0.0f, 0.7f, 0.0f}), 0.6f, 0x9A60FF);
            if (sp.roster >= 0) PortGuestVoiceSay(sp.roster, "cheer,taunt,select,quote", 1.0f);
            else if (sp.klass >= 0) PortStrikersVoiceSay(sp.klass);  // (a Strikers captain: the game's own line)
            buzz(0.4f, 0.2f);
          }
        }
      }
    }
    if (!show) continue;
    hide[sp.i] = camE < 0.0f;  // (r141: in its cameo its card shows flat)
    // the spotlight (BLACK): as it's turned over and while it's held up near your eyes
    if (sp.kind == kSpGlass) {
      const float dy = s.pos.y - S.head.position.y;
      // (r142: held any way now - its face toward your eyes, not turned away from them)
      const XrVector3f nrm = q_rot(q, XrVector3f{0.0f, 0.0f, 1.0f}), toEye = v_sub(S.head.position, s.pos);
      const bool toward = nrm.x * toEye.x + nrm.y * toEye.y + nrm.z * toEye.z > 0.0f;
      const bool heldUp = held && dy > -0.16f && v_len(toEye) < 0.75f && faceUp && toward;
      const float want = (now < sp.spotUntil || heldUp) && faceUp ? 1.0f : 0.0f;
      sp.spot += (want - sp.spot) * std::min(1.0f, dt * (want > sp.spot ? 3.0f : 1.8f));
      dim = std::max(dim, sp.spot * pull_gate(now));  // (r219: the lights stay up through a big pull's party, then ease down)
    }
    const float signFocus = sp.signedCard ? sign_focus(sp, now) : 0.0f;  // r193: its autograph's moment has the stage
    dim = std::max(dim, 0.6f * signFocus * pull_gate(now));
    // r153: a GOLD's step-out loop - on only while its face is to you (it starts IN its room, turned over); its solid face
    // over its window as far as the loop says; a gold glint along the face's edge as it slides; a burst as it dives back
    // into its picture
    GoldPose gp;
    if (sp.kind == kSpPop) {
      if (faceUp && sp.fig != nullptr) {
        sp.goldT += dt;
        if (sp.goldT >= kGoldLoop) sp.goldT -= kGoldLoop;
        if (sp.goldT < kGoldBack0) sp.goldBurst = false;
      }
      gp = gold_pose(sp, sp.goldT);
      sp.goldShut = gp.shut;
      sp.goldTop = gp.top;
      gold_face(sp, gp.shut);
      gold_fx_moves(sp, faceUp, t, gp);  // r171: its vault's shine
      if (faceUp && gp.shut > 0.01f && gp.shut < 0.99f) {
        const XrVector3f edge = v_add(s.pos, q_rot(q, XrVector3f{0.0f, (0.5f - gp.shut) * kCardHm * s.scale, 0.006f * s.scale}));
        xrfx::card_sparkle(edge, 0.45f * kCardWm * s.scale, 0.012f * s.scale, 40.0f, dt, 0xFFF2BE, 0xFFC830);
      }
      if (faceUp && gp.dive >= kGoldInto && gp.dive < kGoldInto + 0.18f) {
        const XrVector3f mid =
            v_add(s.pos, q_rot(q, XrVector3f{0.0f, 0.5f * (kWinBottom + kWinTop) * s.scale, 0.008f * s.scale}));
        xrfx::card_sparkle(mid, 0.38f * kCardWm * s.scale, 0.3f * kCardHm * s.scale, 130.0f, dt, 0xFFE27A, 0xFFB000);
        if (!sp.goldBurst) {
          sp.goldBurst = true;
          if (held) buzz(0.25f, 0.06f);
        }
      }
    }
    // its figure: posed in the card's frame (a pop-out on the card's top edge; inside a window / the slab on the floor
    // / floating), only while its face is to you
    if (sp.fig != nullptr && sp.figCount > 0) {
      float* fo = sp.verts.data() + static_cast<size_t>(sp.figBase) * 6;
      if (faceUp || camE >= 0.0f) {
        // r140: where fit_figure put it (centred by its posed bounds); r141: a BLACK's on its throne looping its own
        // celebration (no motion of its own: a gentle bob); out in its cameo, in the cameo's frame (its throne's top
        // there), its throne's loop as it comes and goes (r141 proof: a home idle can be anything - Spider-Man's hangs
        // upside down from a web), its cheer then its big move at life size
        float lift = 0.0f;
        XrVector3f feet = sp.fit.feet;
        const float fps = std::max(1.0f, xrevo::fps(sp.fig));
        // r193 (Lance: the SILVER Croconaw "stuttered like 5 times before running in place on the card"): what it plays -
        // frame A, and while it changes over, frame B blended in by w (kBlendS: idle into its big move and back, a looped
        // move's end into its start), each a big move's (`big`: held in place, below) or not; a still (its first frame)
        // while its celebration is still being made - never the raw standing file stepping in and then swapped
        float fA = -1.0f, fB = -1.0f, w = 0.0f;
        bool bigA = false, bigB = false;
        float pt = t;  // (seconds: its whole pose file, when none of the below)
        auto loop_wrap = [&](float start, float L) {  // (a move looped: its last Bf frames blended into its first)
          const float Bf = std::min(kBlendS * fps, 0.25f * L);
          if (Bf < 1.0f) {
            fA = start + std::fmod(t * fps, L);
            return;
          }
          const float fa = Bf + std::fmod(t * fps, L - Bf);
          fA = start + fa;
          if (fa >= L - Bf) {
            fB = start + fa - (L - Bf);
            w = (fa - (L - Bf)) / Bf;
          }
        };
        if (sp.celPending && sp.kind != kSpGlass && camE < 0.0f) {
          pt = 0.0f;
        } else if (sp.loopFrames > 0) {
          loop_wrap(static_cast<float>(sp.loopStart), static_cast<float>(sp.loopFrames));
          bigA = bigB = true;
        } else if (sp.liveBigFrames > 0 && sp.liveIdleFrames > 0) {  // r173: its idle (at least 2.5 s), then its big move
          const float iL = static_cast<float>(sp.liveIdleFrames), bL = static_cast<float>(sp.liveBigFrames);
          const float I = iL * std::max(1.0f, std::ceil(2.5f * fps / iL));
          const float Bf = std::min(kBlendS * fps, 0.3f * std::min(iL, bL));
          const float cyc = I + bL - 2.0f * Bf;
          const float f = std::fmod(t * fps, cyc);
          auto idle = [&](float x) { return sp.liveIdleStart + std::fmod(x, iL); };
          if (f < Bf) {  // (its big move's end blending into its idle)
            fA = sp.liveBigStart + bL - Bf + f, bigA = true;
            fB = idle(f), w = Bf > 0.0f ? f / Bf : 1.0f;
          } else if (f < I - Bf) {
            fA = idle(f);
          } else if (f < I) {  // (its idle blending into its big move)
            fA = idle(f);
            fB = sp.liveBigStart + f - (I - Bf), bigB = true, w = (f - (I - Bf)) / std::max(1e-3f, Bf);
          } else {
            fA = sp.liveBigStart + f - (I - Bf), bigA = true;
          }
        } else if (sp.liveBigFrames > 0) {
          loop_wrap(static_cast<float>(sp.liveBigStart), static_cast<float>(sp.liveBigFrames));
          bigA = bigB = true;
        } else if (sp.liveIdleFrames > 0) {
          fA = sp.liveIdleStart + std::fmod(t * fps, static_cast<float>(sp.liveIdleFrames));
        }
        if (sp.kind == kSpGlass && sp.loopFrames == 0) lift = 0.0025f * std::sin(t * 1.6f);
        if (camE >= 0.0f) {
          const Cameo& c = g_cam;
          const float out1 = kCamDim + kCamOut;
          if (c.t >= out1 && c.t < c.backAt && sp.loopFrames > 0) {  // its cheer, then its big move (round again)
            const float f = std::fmod((c.t - out1) * fps, static_cast<float>(sp.loopFrames + sp.fit.bigFrames));
            fA = f < sp.loopFrames ? sp.loopStart + f : sp.fit.bigStart + (f - sp.loopFrames);
            fB = -1.0f;
          }
          feet.y -= kThroneTop;
        }
        float yaw = sp.fit.yaw, scl = sp.fit.scale;
        // (what its big-move frames are held over: its loop's start / its idle's)
        float refF = static_cast<float>(sp.loopFrames > 0 ? sp.loopStart : (sp.liveIdleFrames > 0 ? sp.liveIdleStart : sp.liveBigStart));
        if (sp.kind == kSpPop) {  // r153: where its step-out loop has it (on top: its top-edge move, when it has one)
          feet = gp.feet;
          yaw = gp.yaw;
          scl = gp.scale;
          if (gp.topLoop && sp.fitTop.loopFrames > 0 && !sp.celPending) {
            fB = -1.0f, w = 0.0f;
            loop_wrap(static_cast<float>(sp.fitTop.loopStart), static_cast<float>(sp.fitTop.loopFrames));
            bigA = bigB = true;
          }
        }
        // r181: a big move played IN PLACE on a card (a Melee cheer, a hero's combo walk off a long way): its drift across
        // the floor taken out - its middle kept over where its standing pose has it (hops / jumps up and down kept), so no
        // figure walks out of its window / off its card
        static std::vector<float> ref, other;
        bool refMade = false;
        auto pose_at = [&](float frame, bool big, std::vector<float>* out) {
          xrevo::pose(sp.fig, frame / fps, feet, yaw, scl, lift, out);
          if (!big || camE >= 0.0f || out->empty()) return;
          if (!refMade) {
            xrevo::pose(sp.fig, refF / fps, feet, yaw, scl, lift, &ref);
            refMade = true;
          }
          if (ref.size() != out->size()) return;
          double cx = 0.0, cz = 0.0, rx = 0.0, rz = 0.0;
          const size_t n = out->size() / 6;
          for (size_t v = 0; v < n; ++v) {
            cx += (*out)[v * 6], cz += (*out)[v * 6 + 2];
            rx += ref[v * 6], rz += ref[v * 6 + 2];
          }
          const float dx = static_cast<float>((rx - cx) / std::max<size_t>(1, n)), dz = static_cast<float>((rz - cz) / std::max<size_t>(1, n));
          for (size_t v = 0; v < n; ++v) (*out)[v * 6] += dx, (*out)[v * 6 + 2] += dz;
        };
        if (sp.kind == kSpPop && !gp.fig) {
          sp.posed.clear();  // (SOLID: nobody in its room or on top)
        } else if (fA < 0.0f) {
          xrevo::pose(sp.fig, pt, feet, yaw, scl, lift, &sp.posed);
        } else {
          pose_at(fA, bigA, &sp.posed);
          if (fB >= 0.0f && w > 0.0f) {
            pose_at(fB, bigB, &other);
            const float k = smooth(clampf(w, 0.0f, 1.0f));
            if (other.size() == sp.posed.size())
              for (size_t v = 0; v < sp.posed.size(); ++v) sp.posed[v] += (other[v] - sp.posed[v]) * k;
          }
        }
        if (sp.home)  // r149: a HOMETOWN's figure stands in its landscape diorama: turned onto the card with it
          for (size_t v = 0; v + 5 < sp.posed.size(); v += 6) {
            const float px = sp.posed[v], py = sp.posed[v + 1], nx = sp.posed[v + 3], ny = sp.posed[v + 4];
            sp.posed[v] = -py;
            sp.posed[v + 1] = px;
            sp.posed[v + 3] = -ny;
            sp.posed[v + 4] = nx;
          }
        if (sp.blendT >= 0.0f) {  // r193: from the still it held into its celebration (no pop)
          sp.blendT += dt;
          const float k = smooth(clampf(sp.blendT / kBlendIn, 0.0f, 1.0f));
          if (sp.blendFrom.size() == sp.posed.size())
            for (size_t v = 0; v < sp.posed.size(); ++v) sp.posed[v] = sp.blendFrom[v] + (sp.posed[v] - sp.blendFrom[v]) * k;
          if (sp.blendT >= kBlendIn || sp.blendFrom.size() != sp.posed.size()) {
            sp.blendT = -1.0f;
            sp.blendFrom.clear();
          }
        }
        if (sp.posed.size() >= static_cast<size_t>(sp.figCount) * 6) std::memcpy(fo, sp.posed.data(), sizeof(float) * 6 * sp.figCount);
        else std::memset(fo, 0, sizeof(float) * 6 * sp.figCount);  // (r153: a GOLD's, SOLID)
      } else {
        std::memset(fo, 0, sizeof(float) * 6 * sp.figCount);
      }
    } else if (sp.standBase >= 0) {  // its picture standing in for it
      float* so = sp.verts.data() + static_cast<size_t>(sp.standBase) * 6;
      const bool throne = sp.kind == kSpGlass;  // (r141: on a BLACK's throne)
      const float hgt = sp.kind == kSpPop ? 0.62f * kCardWm : (throne ? 0.8f * kThroneFig : 0.8f * (kWinTop - kWinBottom));
      const float by = sp.kind == kSpPop ? 0.5f * kCardHm : (throne ? kThroneTop : kWinBottom + 0.002f);
      const float bz = sp.kind == kSpPop ? 0.002f : (throne ? 0.0f : -0.018f);
      const float xs[4] = {-0.5f, 0.5f, 0.5f, -0.5f}, ys[4] = {0.0f, 0.0f, 1.0f, 1.0f};
      for (int c = 0; c < 4; ++c) {
        const float v[6] = {faceUp ? xs[c] * hgt : 0.0f, faceUp ? by + ys[c] * hgt : 0.0f, faceUp ? bz : 0.0f, 0.0f, 0.0f, 1.0f};
        std::memcpy(so + c * 6, v, sizeof(v));
      }
    }
    // the gold's sparkles drifting off its figure; a shiny's in its window
    if (faceUp && sp.kind == kSpPop && sp.goldTop > 0.5f) {  // (r153: only while it's up there)
      const XrVector3f top = v_add(s.pos, q_rot(q, XrVector3f{0.0f, 0.5f * kCardHm * s.scale, 0.0f}));
      xrfx::card_sparkle(top, 0.05f * s.scale, 0.08f * s.scale, 5.0f, dt, 0xFFE27A, 0xFFB000);
    }
    if (faceUp && sp.shiny) xrfx::card_sparkle(v_sub(s.pos, XrVector3f{0.0f, 0.4f * kCardHm * s.scale, 0.0f}), 0.06f * s.scale,
                                               0.12f * s.scale, 3.0f, dt, 0xFFFFFF, 0xC8F0FF);
    // r141 (coordinator: a shiny 1/1 - its own colours, OUR glint sweeping over it, "the rainbow sparkle layer AROUND"):
    // rainbow glitter round its figure on its throne / out in its cameo
    if (sp.shiny && sp.kind == kSpGlass && (faceUp || camE >= 0.0f) && sp.fig != nullptr) {
      static const uint32_t kRb[6] = {0xFF6A6A, 0xFFB04A, 0xFFF06A, 0x6AF08A, 0x6AB8FF, 0xC88AFF};
      const int c = static_cast<int>(t * 5.0f) % 6;
      const float figH = sp.fit.scale * sp.fit.bh;
      if (camE < 0.0f) {
        const XrVector3f base = v_add(s.pos, q_rot(q, XrVector3f{0.0f, kThroneTop * s.scale, 0.0f}));
        xrfx::card_sparkle(base, 0.6f * figH * s.scale, figH * s.scale, 6.0f, dt, kRb[c], kRb[(c + 2) % 6]);
      } else if (g_cam.started) {  // (out life size: round it on the floor, a little behind and to the sides)
        const float h = std::min(kCamH, kCamWide * figH / std::max(1e-3f, sp.fit.scale * sp.fit.bw)) * camE;
        xrfx::card_sparkle(g_cam.spot, 0.5f * h + 0.05f, h, 14.0f * camE, dt, kRb[c], kRb[(c + 2) % 6]);
      }
    }
    // its draw, in the card's frame
    xrfx::Scene::ModelDraw& md = scene.models[kSpecialSlot0 + k];
    md.on = true;
    md.posNrm = sp.verts.data();
    md.local = true;
    float sc = s.scale;
    XrQuaternionf fq = q;
    XrVector3f at = s.pos;
    if (camE >= 0.0f) {
      // r141: its cameo's frame - from its throne's top on the card (the card's turn and size) to the floor kCamDist
      // ahead of you (upright, turned to you, kCamH tall), leaping on the way (its size eased in proportion)
      const Cameo& c = g_cam;
      const XrVector3f p0 = v_add(s.pos, q_rot(q, XrVector3f{0.0f, kThroneTop * s.scale, 0.0f}));
      const float k1 = std::min(kCamH / std::max(1e-3f, sp.fit.scale * sp.fit.bh), kCamWide / std::max(1e-3f, sp.fit.scale * sp.fit.bw));
      const XrQuaternionf q1 = q_axis(0, 1, 0, std::atan2(S.head.position.x - c.spot.x, S.head.position.z - c.spot.z));
      fq = q_nlerp(q, q1, camE);
      at = v_add(v_lerp(p0, c.spot, camE), XrVector3f{0.0f, 0.35f * std::sin(3.14159265f * camE), 0.0f});
      sc = s.scale * std::pow(k1 / std::max(1e-3f, s.scale), camE);
    }
    const XrVector3f ax = q_rot(fq, XrVector3f{sc, 0.0f, 0.0f}), ay = q_rot(fq, XrVector3f{0.0f, sc, 0.0f}),
                     az = q_rot(fq, XrVector3f{0.0f, 0.0f, sc});
    const float mtx[16] = {ax.x, ax.y, ax.z, 0.0f, ay.x, ay.y, ay.z, 0.0f, az.x, az.y, az.z, 0.0f, at.x, at.y, at.z, 1.0f};
    std::memcpy(md.toWorld, mtx, sizeof(mtx));
    md.white = kCardWm;  // (the window in the card's width, xr_fx_model.frag)
    md.alpha = 1.0f;
    md.glow = camE >= 0.0f ? 0.5f : sp.spot;
    // (a pop-out, r149: a TIZM BOOM, a SIGNED BASE: no window to clip it)
    // (r153: a GOLD has its window now - what stands on its top edge is above it, never clipped: xr_fx_model.frag)
    md.under = sp.kind == kSpGlass ? -3.5f : (sp.kind == kSpBoom || sp.kind == kSpSign ? -2.9f : -3.0f);
    if (camE >= 0.0f) md.under = -2.9f;  // (r141: out of its card, nothing to clip it)
    if (sp.home)  // r149: a HOMETOWN's window, landscape (on the card: narrow across, long up it)
      md.glowRgb = pack_rgb(std::ceil(kHomeWinHalfY / kCardWm * 255.0f) / 255.0f, std::floor((-kHomeWinHalfX / kCardHm + 0.5f) * 255.0f) / 255.0f,
                            std::ceil((kHomeWinHalfX / kCardHm + 0.5f) * 255.0f) / 255.0f);
    else
      md.glowRgb = pack_rgb(std::ceil(kWinHalf / kCardWm * 255.0f) / 255.0f, std::floor((kWinBottom / kCardHm + 0.5f) * 255.0f) / 255.0f,
                            std::ceil((kWinTop / kCardHm + 0.5f) * 255.0f) / 255.0f);
    // r193: its 3D cut to its face's side, rising out of its top edge (not out in its cameo: nothing to cut it); while its
    // autograph's moment has the stage its figure steps back down into the card (dimmed as it goes), up again after it lands
    md.reveal = camE >= 0.0f ? -1.0f : sp.reveal * (1.0f - signFocus);
    md.dim = 0.65f * signFocus;
    special_moves(sp, faceUp, held, t, now, dt, fq, at, sc);  // r149: a TIZM BOOM's layers popping, an autograph writing itself
    if (sp.home) home_moves(sp, faceUp, q, t, now, dt);  // r171: a HOMETOWN's diorama, alive
    if (sp.kind == kSpPop) md.glow = std::max(md.glow, 0.6f);  // r171: a GOLD's vault (and its figure) lit up bright
    md.flameFrame = -1;
    md.shine = sp.shiny ? 1.0f : 0.0f;  // OUR shiny's glint (r127)
    md.frame = -1;
    md.kaleido = sp.kind == kSpGlass ? (sp.rgb != 0 ? sp.rgb : 0x6A2AD8u) : 0u;  // r147: its stone's kaleidoscope colour
  }
  // r141: a cameo waiting for its figure gives up after a while; the BLACK 1/1's world (its effects round you and on its
  // card, its throne's aura, its light, its sound), fading in and out with it
  if (g_cam.i >= 0 && !g_cam.started) {
    g_cam.wait += dt;
    if (g_cam.wait > 6.0f) g_cam = Cameo{};
  }
  if (g_cam.i >= 0 && g_cam.started && !camSeen) g_cam = Cameo{};  // (its special made way: over)
  if (god_frame(scene, dt, now, fxReady)) {
    // r149: a GOD PACK's tell / show has the 1/1 world's slot (the world back after it)
  } else if (fxK >= 0 && g_sp[fxK].theme >= 0) {
    const Special& sp = g_sp[fxK];
    if (g_fx.card != sp.i) fx_begin(sp.i, sp.theme, sp.shiny, fxReady);  // (once: a failed upload isn't retried every frame)
    fx_frame(scene, fxFaceUp, fxHeld, fxQ, fxPos, fxScale, fxTop, dt, t, g_cam.i == sp.i && g_cam.started);
    dim = std::max(dim, fx_dim() * pull_gate(now));
  } else if (g_fx.up) {
    fx_end(fxReady);
  }
  // r219: a 1/1's build-up - the room round you dark (the cards' own shade at its darkest), slammed back at its reveal
  dim = std::max(dim, g_pullDark.load());
  const float dimR = g_cam.i >= 0 && g_cam.started ? 2.6f : 1.35f;  // (the cameo stands inside the dimming)
  // the light
  if (!g_lightUp || g_lightVerts.size() != static_cast<size_t>(g_lightN) * 6) return;
  const XrVector3f up{0.0f, 1.0f, 0.0f};
  for (const LightPart& lp : g_lightParts) {
    if (lp.kind == 3 || lp.kind == 4 || lp.kind == 5) continue;  // (r230: 5 the gallery's veil)
    const Sheet* s = lp.card >= 0 && lp.card < S.n ? &S.card[lp.card] : nullptr;
    if (s == nullptr || !s->on || !s->shown || s->scale < 0.01f || ((lp.kind == 0 || lp.kind == 1) && !tell_live(lp.card)) ||
        (lp.kind == 2 && !special_live(lp.card))) {
      light_hide(lp);
      continue;
    }
    const float down = clampf(-std::cos(s->flip) * 1.6f, 0.0f, 1.0f);  // its back to you
    const XrQuaternionf q = sheet_q(*s);
    const XrVector3f away = v_mul(v_sub(s->pos, S.head.position), 1.0f / std::max(0.01f, v_len(v_sub(s->pos, S.head.position))));
    const float W = kCardWm * s->scale, H = kCardHm * s->scale;
    if (lp.kind == 0) {  // an aura behind it while it's face down (a slow beat, heart-drop)
      const float k = down * (1.0f + 0.05f * std::sin(t * 2.6f + lp.card));
      if (k < 0.01f) {
        light_hide(lp);
        continue;
      }
      const XrQuaternionf spin = q_mul(q, q_axis(0, 0, 1, lp.n == 1 ? t * 0.35f : 0.0f));  // (the shiny's glitter turns)
      const XrVector3f at = v_add(s->pos, v_mul(away, 0.008f + 0.003f * lp.n));
      if (lp.n == 1) {  // a shiny's rainbow ring, round and turning, past the card's corners
        light_quad(lp.base, at, spin, 2.7f * W * k, 2.7f * W * k);
      } else if (lp.n == 10 + kHitBoom) {  // r149: TIZM BOOM's starburst, big, turning, punching out on a beat
        const float beat = 1.0f + 0.12f * std::pow(std::max(0.0f, std::sin(t * 4.2f)), 8.0f);
        light_quad(lp.base, at, q_mul(q, q_axis(0, 0, 1, t * 0.25f)), 2.9f * W * k * beat, 2.9f * W * k * beat);
      } else if (lp.n >= 10 + kHitHome) {  // r149: HOMETOWN's sky / SIGNED's ink halo, a little past the card, swelling
        const float m = 3.0f * kAuraMargin * s->scale, sw = 1.0f + 0.06f * std::sin(t * 1.8f);
        light_quad(lp.base, at, lp.n == 10 + kHitSigned ? q_mul(q, q_axis(0, 0, 1, std::sin(t * 0.6f) * 0.3f)) : q, (W + m) * k * sw,
                   (H + m) * k * sw);
      } else {  // a halo the card's shape (light pictures 6 - 8)
        const float m = 2.0f * kAuraMargin * s->scale;
        light_quad(lp.base, at, spin, (W + m) * k, (H + m) * k);
      }
    } else if (lp.kind == 1) {  // a smoke puff curling off its edge, rising, growing, gone
      const float cyc = t / 1.7f + lp.n / static_cast<float>(kSmokePuffs), ph = cyc - std::floor(cyc);
      const uint32_t hsh = static_cast<uint32_t>(std::floor(cyc)) * 2654435761u + static_cast<uint32_t>(lp.n) * 40503u;
      const float along = static_cast<float>(hsh % 1000u) / 1000.0f;  // where round its edge: top, bottom, right, left
      const int side = std::min(3, static_cast<int>(along * 4.0f));
      const float f = along * 4.0f - side;
      const float nx = side == 2 ? 1.0f : (side == 3 ? -1.0f : 0.0f), ny = side == 0 ? 1.0f : (side == 1 ? -1.0f : 0.0f);
      const float ex = side < 2 ? (f - 0.5f) * W : 0.5f * nx * W, ey = side < 2 ? 0.5f * ny * H : (f - 0.5f) * H;
      const float off = (0.004f + 0.022f * ph) * s->scale;  // (r138 proof: hugging the edge, bigger - it read faint)
      const XrVector3f at = v_add(v_add(s->pos, q_rot(q, XrVector3f{ex + nx * off, ey + ny * off, 0.0f})),
                                  v_add(v_mul(up, 0.03f * ph * s->scale), v_mul(away, 0.004f)));
      const float size = (0.035f + 0.05f * ph) * s->scale * std::sin(3.14159265f * ph) * down;
      if (size < 0.001f) {
        light_hide(lp);
        continue;
      }
      light_quad(lp.base, at, q_mul(facing(at), q_axis(0, 0, 1, ph * 2.0f + lp.n)), size, size);
    } else if (lp.kind == 2) {  // the glow under a GOLD's figure on its top edge
      const float upk = clampf(std::cos(s->flip) * 1.6f, 0.0f, 1.0f);
      Special* sp = special_of(lp.card);
      if (upk < 0.01f || sp == nullptr || !sp->up) {
        light_hide(lp);
        continue;
      }
      const XrVector3f top = v_add(s->pos, q_rot(q, XrVector3f{0.0f, 0.5f * H, 0.0f}));
      const float on = upk * sp->goldTop;  // (r153: only while it stands up there)
      if (on < 0.01f) {
        light_hide(lp);
        continue;
      }
      light_quad(lp.base, top, facing(top), 0.11f * s->scale * on, 0.07f * s->scale * on);
    }
  }
  // the spotlight's beam on the BLACK and your surroundings dimmed under it
  const Special* lit = nullptr;
  for (const Special& sp : g_sp)
    if (sp.up && sp.kind == kSpGlass && sp.spot > 0.01f && (lit == nullptr || sp.spot > lit->spot)) lit = &sp;
  // r219: a 1/1's build-up - the one spotlight on the card in your hand while the room is dark (it shone only after the turn)
  int litI = lit != nullptr ? lit->i : -1;
  float litK = lit != nullptr ? lit->spot : 0.0f;
  if (g_pullCard >= 0 && g_pullCard < S.n && g_pullDark.load() > litK) litI = g_pullCard, litK = g_pullDark.load();
  // r230: the EXQUISITE gallery's soft spotlight on the card turned over at the front of the stack (no 1/1's beam on)
  if (litI < 0 && g_gallery.load() > 0.05f && S.mode == kPackUp && S.phase == kStack) {
    const int f = front_card();
    if (f >= 0 && S.faceUp[f]) litI = f, litK = 0.55f * g_gallery.load();
  }
  for (const LightPart& lp : g_lightParts) {
    if (lp.kind == 3) {
      if (litI < 0 || litK <= 0.01f) {
        light_hide(lp);
        continue;
      }
      // r138 proof: a cone's two walls over the card read as a solid lampshade; now one soft shaft from above, turned
      // to your eyes, just behind the slab (the card stays crisp in front of it, lit), its foot fading just under it
      const Sheet& s = S.card[litI];
      const float k = litK, len = 0.25f + 0.6f * k, r0 = 0.012f, r1 = (0.03f + 0.07f * k) * s.scale + 0.02f;
      float hx = S.head.position.x - s.pos.x, hz = S.head.position.z - s.pos.z;
      const float hl = std::max(0.001f, std::sqrt(hx * hx + hz * hz));
      hx /= hl;
      hz /= hl;
      const float foot = 0.5f * kCardHm * s.scale + 0.01f;
      const float back = 0.015f + kSlabD * s.scale;  // (behind the slab's depth: its glass stays black)
      float* o = g_lightVerts.data() + static_cast<size_t>(lp.base) * 6;
      for (int row = 0; row <= 1; ++row)
        for (int j = 0; j <= 16; ++j) {
          const float across = (j / 16.0f * 2.0f - 1.0f) * (row == 0 ? r0 : r1), y = row == 0 ? len : -foot;
          const XrVector3f p{s.pos.x - hx * back - hz * across, s.pos.y + y, s.pos.z - hz * back + hx * across};
          float* v = o + (row * 17 + j) * 6;
          v[0] = p.x;
          v[1] = p.y;
          v[2] = p.z;
          v[3] = hx;
          v[4] = 0.0f;
          v[5] = hz;
        }
    } else if (lp.kind == 5) {  // r230: the gallery's warm veil, 1.6 m round you (outside the 1/1's dimming)
      const int level = static_cast<int>(std::lround(g_gallery.load() * 8.0f));
      if (lp.n != level) {
        light_hide(lp);
        continue;
      }
      float* o = g_lightVerts.data() + static_cast<size_t>(lp.base) * 6;
      const XrVector3f& h = S.head.position;
      for (int i = 0; i <= 6; ++i)
        for (int j = 0; j <= 10; ++j) {
          const float th = 3.14159265f * i / 6.0f, ph = 6.2831853f * j / 10.0f;
          const XrVector3f d{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
          float* v = o + (i * 11 + j) * 6;
          v[0] = h.x + d.x * 1.6f;
          v[1] = h.y + d.y * 1.6f;
          v[2] = h.z + d.z * 1.6f;
          v[3] = -d.x;
          v[4] = -d.y;
          v[5] = -d.z;
        }
    } else if (lp.kind == 4) {
      const int level = static_cast<int>(std::lround(dim * 8.0f));
      if (lp.n != level) {
        light_hide(lp);
        continue;
      }
      // a shade all round you, past arm's length (the cards stay bright): a sphere 1.35 m out
      float* o = g_lightVerts.data() + static_cast<size_t>(lp.base) * 6;
      const XrVector3f& h = S.head.position;
      for (int i = 0; i <= 6; ++i)
        for (int j = 0; j <= 10; ++j) {
          const float th = 3.14159265f * i / 6.0f, ph = 6.2831853f * j / 10.0f;
          const XrVector3f d{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
          float* v = o + (i * 11 + j) * 6;
          v[0] = h.x + d.x * dimR;
          v[1] = h.y + d.y * dimR;
          v[2] = h.z + d.z * dimR;
          v[3] = -d.x;
          v[4] = -d.y;
          v[5] = -d.z;
        }
    }
  }
  // the face-down hits' sparkles: GOLD slow gold ones, a shiny rainbow glitter
  for (int i = 0; i < S.n; ++i) {
    const Sheet& s = S.card[i];
    if (!s.on || !s.shown || std::cos(s.flip) > -0.3f || !tell_live(i)) continue;
    if (S.refs[i].parallel == kGoldTizm) xrfx::card_sparkle(v_sub(s.pos, XrVector3f{0, 0.5f * kCardHm * s.scale, 0}), 0.09f * s.scale,
                                                            kCardHm * s.scale, 2.5f, dt, 0xFFE27A, 0xFFB000);
    if (S.refs[i].shiny) {
      static const uint32_t kRb[6] = {0xFF6A6A, 0xFFB04A, 0xFFF06A, 0x6AF08A, 0x6AB8FF, 0xC88AFF};
      const int c = static_cast<int>(t * 5.0f) % 6;
      xrfx::card_sparkle(v_sub(s.pos, XrVector3f{0, 0.5f * kCardHm * s.scale, 0}), 0.09f * s.scale, kCardHm * s.scale, 4.0f, dt,
                         kRb[c], kRb[(c + 2) % 6]);
    }
  }
  xrfx::Scene::ModelDraw& md = scene.models[kLightSlot];
  md.on = true;
  md.posNrm = g_lightVerts.data();
  md.local = false;
  md.white = 0.0f;
  md.alpha = 1.0f;
  md.glow = 0.0f;
  md.under = -4.0f;  // the cards' light (xr_fx_model.frag)
  md.glowRgb = 0xFFFFFF;
  md.flameFrame = -1;
  md.shine = 0.0f;
  md.frame = -1;
}

}  // namespace

void set_evo_backdrop(const char* (*pick)(const std::string& key)) { g_backdrop = pick; }
void set_floor(float y) { g_floorY = y; }  // r141

bool busy() { return S.mode == kPackUp || S.mode == kOut || S.mode == kTrade; }  // (r146: a trade-in's show)

bool hub_input(const Input& in, bool* redraw, bool* stick) {
  WdScope wd("game thread", "hub input");  // r237
  if (g_page.load() == kPageCollectors && g_sceneMode.load() == 0) {  // r213: COLLECTOR TRADES - as the market (r206's rule: redraw on change only)
    WdScope wdp("game thread", "COLLECTORS input");  // r237
    *stick = in.dir != 0;
    const int genBefore = xrtrade::generation();
    xrtrade::Input ti;
    ti.dir = in.dir;
    ti.step = in.step;
    ti.a = in.a;
    ti.b = in.b;
    ti.x = in.x;
    ti.y = in.y;
    const bool used = xrtrade::input(ti);
    if (xrtrade::generation() != genBefore) {
      *redraw = true;
      ++g_gen;
    }
    return used || *stick;
  }
  if ((g_page.load() == kPageMarket || g_page.load() == kPageMuseum) && g_sceneMode.load() == 0) {  // r201
    WdScope wdp("game thread", g_page.load() == kPageMarket ? "MARKET input" : "MUSEUM input");  // r237
    *stick = in.dir != 0;
    bool used;
    const bool mk = g_page.load() == kPageMarket;
    const int genBefore = mk ? xrmarket::generation() : xrmuseum::generation();  // r206: what changed = its generation moved
    if (g_page.load() == kPageMarket) {
      xrmarket::Input mi;
      mi.dir = in.dir;
      mi.step = in.step;
      mi.a = in.a;
      mi.b = in.b;
      mi.x = in.x;
      mi.y = in.y;
      used = xrmarket::input(mi);
    } else {
      xrmuseum::Input mi;
      mi.dir = in.dir;
      mi.step = in.step;
      mi.a = in.a;
      mi.b = in.b;
      mi.x = in.x;
      mi.y = in.y;
      used = xrmuseum::input(mi);
    }
    // r206 (Lance's r205 test: MARKET = fps drops, then the game killed using 6.3 GiB): r201-r205 bumped g_gen HERE ON EVERY
    // CALL - hub_input runs every game frame on this page - so xr_quest saw the cards change 72 times a second: fr_refresh
    // each frame -> the hub's 2160 x 1300 picture asked for again each frame -> a new drawing thread each frame (each ~56 MB:
    // its float canvas + pixels), far more started than finished, and an 11 MB upload on the frame thread for each that did.
    // Now only what the market / museum CHANGED redraws (a move, a pick, a tab: its own generation moved - its input() says
    // "used" every frame while countering / asking, so that word can't be the test); a stick held / drifting asks for nothing.
    if ((mk ? xrmarket::generation() : xrmuseum::generation()) != genBefore) {
      *redraw = true;
      ++g_gen;
    }
    return used || *stick;
  }
  *stick = false;
  poll();
  if (!g_loaded.load()) return false;
  const uint64_t now = mono_ns();
  S.pageUpNs = now;
  const bool lt = in.lt && !S.prevLt, rt = in.rt && !S.prevRt;
  S.prevLt = in.lt;
  S.prevRt = in.rt;
  if (S.mode == kPackUp) {
    *stick = true;  // (the stick and the buttons are the pack's while it's open)
    // r139: the triggers are the hands' (triggers(): the hand not holding the pack rips it, then turns its cards over one
    // by one); A does what a trigger does (every card seen: the next pack, or put away when none); B into the binder
    // (before the rip: the pack put back unopened)
    if (cameo_live() && (in.a || in.b)) {  // r141: a BLACK 1/1's cameo: B (or A) sends it back onto its throne
      cameo_skip();
      return true;
    }
    if (god_live() && (in.a || in.b)) {  // r149: a GOD PACK's show: A / B skips it
      god_skip();
      return true;
    }
    if (in.a) {
      if (S.phase == kWaiting) S.askTear = true;
      else if (S.phase == kStack && S.revealed >= S.n) S.nextPack = S.askAway = true;
      else if (S.phase == kStack) stack_step(now);
    }
    if (in.b && S.phase != kFiling) S.askAway = true;
    return in.a || in.b || in.x || lt || rt;
  }
  if (S.mode == kOut) {
    *stick = true;
    if (lt || rt) {
      Sheet& s = S.card[0];
      start_flip(s, s.flipTo > 1.0f ? 0.0f : 3.14159265f, 0.5f, now, false);
    }
    if (in.a || in.b || in.x) S.askAway = true;
    return in.a || in.b || in.x || lt || rt;
  }
  if (S.mode == kAway) {
    *stick = true;
    return in.a || in.b || in.x;
  }
  if (S.mode == kTrade) {  // r146: a trade-in's show: A / B skip it
    *stick = true;
    if (in.a || in.b) S.tradeSkip = true;
    return in.a || in.b || in.x || lt || rt;
  }
  // r146: TRADE-INS (LT from the shop): the stick over your spare base cards, A trades the next 10 for a pack (its show), X
  // keeps the card under the cursor out of it (or puts it back), LT the sets, RT the shop
  // r157: (no LT / RT to the next page: the pages are the hub's CARDS rail)
  if (g_page.load() == kPageTrade) {
    std::vector<int> traded;
    int r = -1;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      std::vector<int> tiles;  // (the page's order: the most spares first; r226: the page's set's)
      for (size_t i = 0; i < g_set.size(); ++i)
        if (g_set[i].set == g_tradeSet && spare_of(static_cast<int>(i)) > 0) tiles.push_back(static_cast<int>(i));
      std::stable_sort(tiles.begin(), tiles.end(), [](int x, int y) {
        return trade_before(x, spare_of(x), y, spare_of(y));  // (r147: the page's order)
      });
      const int n = static_cast<int>(tiles.size());
      // r226: the two set panels over the cards - stick up from the cards' top row (or with no cards) onto them, left / right picks the
      // set (its cards below, what A trades), down back onto its cards
      if (in.step && g_tradeOnSets) {
        *stick = true;
        if (in.dir == 1 || in.dir == 2) {
          const int s = in.dir == 1 ? 0 : 1;
          if (s != g_tradeSet) {
            g_tradeSet = s;
            g_tradeCursor = 0;
            g_tradeTop = 0;
            g_tradeSay.clear();
          }
          *redraw = true;
        } else if (in.dir == 4 && n > 0) {
          g_tradeOnSets = false;
          *redraw = true;
        }
      } else if (in.step && in.dir == 3 && (n == 0 || std::max(0, std::min(n - 1, g_tradeCursor)) < kTileCols)) {
        g_tradeOnSets = true;
        *stick = true;
        *redraw = true;
      } else if (in.step && n == 0) {
        *stick = true;  // (no cards of this set: only up reaches the panels)
      } else if (in.step && n > 0) {
        int c = std::max(0, std::min(n - 1, g_tradeCursor));
        const int col = c % kTileCols;
        bool moved = false;
        if (in.dir == 1 && col > 0) --c, moved = true;
        if (in.dir == 2 && col < kTileCols - 1 && c + 1 < n) ++c, moved = true;
        if (in.dir == 3 && c >= kTileCols) c -= kTileCols, moved = true;
        if (in.dir == 4 && c + kTileCols < n) c += kTileCols, moved = true;
        *stick = true;  // r157: the stick stays on the page (stops at the grid's edge)
        g_tradeCursor = c;
        const int row = c / kTileCols;
        if (row < g_tradeTop) g_tradeTop = row;
        if (row >= g_tradeTop + kTileRows) g_tradeTop = row - kTileRows + 1;
        if (moved) *redraw = true;
      }
      if (in.x && !g_tradeOnSets && g_tradeCursor < n) {
        const std::string& key = g_set[tiles[static_cast<size_t>(g_tradeCursor)]].id;
        if (g_keep.count(key) != 0) g_keep.erase(key);
        else g_keep.insert(key);
        g_tradeSay = g_keep.count(key) != 0 ? upper(g_set[tiles[static_cast<size_t>(g_tradeCursor)]].name) + " KEPT OUT OF TRADE-INS"
                                            : upper(g_set[tiles[static_cast<size_t>(g_tradeCursor)]].name) + " CAN BE TRADED AGAIN";
        save_collection();
        *redraw = true;
      }
    }
    if (in.a) {
      const int set = g_tradeSet;  // r226: the picked set's spares for its own pack
      r = trade_in(&traded, set);
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        const std::string sn = set == 1 ? "JAM '96 " : "";
        g_tradeSay = r == 0 ? "TRADED! A FREE " + sn + "PACK IS WAITING WITH YOUR PACKS"
                            : (r == 1 ? "NOT " + std::to_string(trade_for(set)) + " SPARE " + sn + "BASE CARDS YET (THE ONES YOU KEEP DON'T COUNT)"
                                      : "NO ROOM FOR MORE PACKS");
      }
      if (r == 0) trade_show(traded, now);
      else buzz(0.15f, 0.04f);
      *redraw = true;
    }
    return in.a || in.x || *stick;
  }
  // r146: SETS (LT from the trade-ins): the stick scrolls the evolution lines; RT the trade-ins
  if (g_page.load() == kPageSets) {
    if (in.y || in.b) {  // r236: SETS is the BINDER's other view now - Y (or B) back to the pages
      g_page = kPageBinder;
      ++g_gen;
      *redraw = true;
      return true;
    }
    if (in.step) *stick = true;  // r157
    if (in.step && (in.dir == 3 || in.dir == 4)) {
      std::lock_guard<std::mutex> lock(g_mutex);
      build_sets();
      const int most = std::max(0, static_cast<int>(g_lines.size() + (g_types.size() + 1) / 2) + 40 - kSetsShown);  // (r157 round 2: a row at a time; hub_page clamps it)
      const int before = g_setsTop;
      g_setsTop = std::max(0, std::min(most, g_setsTop + (in.dir == 4 ? 1 : -1)));
      *stick = true;
      if (g_setsTop != before) *redraw = true;
    }
    return *stick;
  }
  // r157: GOAL EXPLOSIONS (r153's shop shelf, now on the rail): the stick over the 12 + NO EXPLOSION, A puts on one you own
  // (or asks to buy it: the hub's confirm, A buys, B no), X previews it in your room (B / leaving stops it)
  // r236 (LOCKER): GOAL FX and TAKEOVERS - the same catalogue split in two pages (a room takeover = seconds > 5; NO EXPLOSION on GOAL
  // FX), 3 a row; the ring's place on each page (g_fxPos) picks the catalogue's item (g_fxCursor: its index, cat.size() = none)
  if (g_page.load() == kPageExplosions || g_page.load() == kPageTakeovers) {
    const std::vector<xrgoalfx::Item>& cat = xrgoalfx::catalogue();
    const int n = static_cast<int>(cat.size()) + 1;
    const bool tk = g_page.load() == kPageTakeovers;
    const std::vector<int> list = fx_list(tk);
    auto fx_say = [](const std::string& t) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_fxSay = t;
    };
    if (g_fxArmed >= 0) {  // asking to buy it: A yes, B no, nothing else
      if (in.a) {
        long long left = 0;  // r208
        const xrgoalfx::Buy r = g_fxArmed < static_cast<int>(cat.size()) ? xrgoalfx::try_buy(cat[static_cast<size_t>(g_fxArmed)].id, &left)
                                                                         : xrgoalfx::Buy::kUnknown;
        if (r == xrgoalfx::Buy::kBought || r == xrgoalfx::Buy::kAlreadyOwned) xrgoalfx::equip(cat[static_cast<size_t>(g_fxArmed)].id);
        {
          std::lock_guard<std::mutex> lock(g_mutex);
          g_fxSay = r == xrgoalfx::Buy::kBought ? upper(cat[static_cast<size_t>(g_fxArmed)].name) + " IS YOURS - IT'S ON"
                    : r == xrgoalfx::Buy::kNotEnough ? std::string("NOT ENOUGH BIT COINS YET  -  PLAY FRANCHISE MATCHES")
                                                     : std::string("COULDN'T BUY IT");
        }
        buzz(r == xrgoalfx::Buy::kBought ? 0.35f : 0.15f, r == xrgoalfx::Buy::kBought ? 0.08f : 0.04f);
        g_fxArmed = -1;
        ++g_gen;
        *redraw = true;
      } else if (in.b) {
        g_fxArmed = -1;
        *redraw = true;
      }
      *stick = true;
      return in.a || in.b || in.x || in.y || in.step;
    }
    const int ln = static_cast<int>(list.size());
    int pos = std::max(0, std::min(std::max(0, ln - 1), g_fxPos[tk ? 1 : 0]));
    if (in.step && ln > 0) {
      const int before = pos, col = pos % kLkGridCols;
      if (in.dir == 1 && col > 0) --pos;
      if (in.dir == 2 && col < kLkGridCols - 1 && pos + 1 < ln) ++pos;
      if (in.dir == 3 && pos >= kLkGridCols) pos -= kLkGridCols;
      if (in.dir == 4 && pos + kLkGridCols < ln) pos += kLkGridCols;
      else if (in.dir == 4 && pos / kLkGridCols < (ln - 1) / kLkGridCols) pos = ln - 1;
      if (pos != before) {
        fx_say("");
        *redraw = true;
      }
      *stick = true;
    }
    g_fxPos[tk ? 1 : 0] = pos;
    int c = ln > 0 && list[static_cast<size_t>(pos)] >= 0 ? list[static_cast<size_t>(pos)] : n - 1;
    (void)n;
    g_fxCursor = c;
    if (in.b && g_fxPreviewing) {  // (B stops a preview before it takes you back to the rail)
      xrgoalfx::stop();
      g_fxPreviewing = false;
      *redraw = true;
      return true;
    }
    if (in.a) {
      if (c >= static_cast<int>(cat.size())) {
        xrgoalfx::equip("none");
        fx_say("NO GOAL EXPLOSION: THE GOAL'S OWN SHOW ONLY");
      } else if (xrgoalfx::owned(cat[static_cast<size_t>(c)].id)) {
        xrgoalfx::equip(cat[static_cast<size_t>(c)].id);
        fx_say(upper(cat[static_cast<size_t>(c)].name) + " IS ON");
      } else {
        g_fxArmed = c;  // the hub asks (its confirm)
        g_fxArmedNs = mono_ns();
      }
      ++g_gen;
      *redraw = true;
    }
    if (in.x && c < static_cast<int>(cat.size())) {
      // ~2 m in front of you, level, its +Z toward you
      const XrQuaternionf q = S.head.orientation;
      XrVector3f f = q_rot(q, XrVector3f{0.0f, 0.0f, -1.0f});
      f.y = 0.0f;
      const float len = std::sqrt(f.x * f.x + f.z * f.z);
      if (len > 1e-3f) f = XrVector3f{f.x / len, 0.0f, f.z / len};
      else f = XrVector3f{0.0f, 0.0f, -1.0f};
      const float yaw = std::atan2(-f.x, -f.z);
      XrPosef where;
      where.orientation = XrQuaternionf{0.0f, std::sin(0.5f * yaw), 0.0f, std::cos(0.5f * yaw)};
      where.position = v_add(S.head.position, XrVector3f{2.0f * f.x, -0.6f, 2.0f * f.z});
      xrgoalfx::preview(cat[static_cast<size_t>(c)].id, where);
      g_fxPreviewing = true;
      *redraw = true;
    }
    return in.a || in.x || *stick;
  }
  // r160: LOOKS - the stick over the two rows (BINDER SKINS, CARD SLEEVES; left / right along a row, up / down between
  // them), A puts on one you own or asks to buy it (the hub's confirm: A buys and puts it on, B no), X previews it (a skin:
  // the 3D binder in that skin in front of you; a sleeve: a card of yours out in it), B stops a preview first
  // r236 (LOCKER): BALLS (kPageLooks) and BINDERS (kPageCovers) - the two shelves of r160-r170's LOOKS, each its own page, 3 a row
  if (g_page.load() == kPageLooks || g_page.load() == kPageCovers) {
    auto say = [](const std::string& t) {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_lkSay = t;
    };
    int armKind = -1, arm = -1, row = 0, col = 0;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      armKind = g_lkArmKind;
      arm = g_lkArm;
      g_lkBallFocus = g_page.load() == kPageLooks;  // (the page says which shelf)
    }
    if (armKind == kLookBall && arm >= 0) {  // r168: a ball's buy asking: A yes, B no, nothing else
      if (in.a) {
        const int r = balls_buy(arm);
        if (r == 0 || r == 2) ball_equip(arm);
        say(r == 0 ? std::string(ball_item(arm).name) + " IS YOURS - IT'S ON"
                   : (r == 1 ? std::string("NOT ENOUGH BIT COINS YET  -  PLAY FRANCHISE MATCHES") : std::string(ball_item(arm).name) + " IS ON"));
        buzz(r == 0 ? 0.35f : 0.15f, r == 0 ? 0.08f : 0.04f);
      }
      if (in.a || in.b) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_lkArmKind = g_lkArm = -1;
        *redraw = true;
      }
      *stick = true;
      return in.a || in.b || in.x || in.y || in.step;
    }
    if (armKind >= 0 && arm >= 0) {  // a buy asking: A yes, B no, nothing else
      if (in.a) {
        const int r = looks_buy(armKind, arm);
        if (r == 0 || r == 2) look_equip(armKind, arm);
        say(r == 0 ? upper(look_item(armKind, arm).name) + " IS YOURS - IT'S ON"
                   : (r == 1 ? std::string("NOT ENOUGH BIT COINS YET  -  PLAY FRANCHISE MATCHES") : upper(look_item(armKind, arm).name) + " IS ON"));
        buzz(r == 0 ? 0.35f : 0.15f, r == 0 ? 0.08f : 0.04f);
      }
      if (in.a || in.b) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_lkArmKind = g_lkArm = -1;
        *redraw = true;
      }
      *stick = true;
      return in.a || in.b || in.x || in.y || in.step;
    }
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      if (in.step) {  // r236: the shelf's grid, 3 a row - the stick moves the ring, it stops at the edges
        int& cur = g_lkBallFocus ? g_lkBall : g_lkCol[0];
        const int cnt = g_lkBallFocus ? kBallTiles : kSkinCount;
        const int before = cur, col0 = cur % kLkGridCols;
        if (in.dir == 1 && col0 > 0) --cur;
        if (in.dir == 2 && col0 < kLkGridCols - 1 && cur + 1 < cnt) ++cur;
        if (in.dir == 3 && cur >= kLkGridCols) cur -= kLkGridCols;
        if (in.dir == 4 && cur + kLkGridCols < cnt) cur += kLkGridCols;
        else if (in.dir == 4 && cur / kLkGridCols < (cnt - 1) / kLkGridCols) cur = cnt - 1;
        cur = std::max(0, std::min(cnt - 1, cur));
        if (cur != before) {
          g_lkSay.clear();
          *redraw = true;
        }
        *stick = true;
      }
      row = kLookSkin;
      col = g_lkCol[0];
    }
    bool ballFocus = false;
    int ball = 0;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      ballFocus = g_lkBallFocus;
      ball = std::max(0, std::min(kBallTiles - 1, g_lkBall));
    }
    const bool previewing = g_skinPreview.load() >= 0 || xrballs::previewing();
    if (in.b && previewing) {  // (B stops a preview before it takes you back to the rail)
      g_skinPreview = -1;
      xrballs::stop_preview();  // r168
      say("");
      *redraw = true;
      return true;
    }
    if (ballFocus) {  // r168: a ball skin under the ring - A puts it on / asks to buy it, X: it in front of you
      if (in.a) {
        if (ball_owned(ball)) {
          ball_equip(ball);
          say(std::string(ball_item(ball).name) + " IS ON");
        } else {
          std::lock_guard<std::mutex> lock(g_mutex);
          g_lkArmKind = kLookBall;  // the hub asks (its confirm)
          g_lkArm = ball;
        }
        ++g_gen;
        *redraw = true;
      }
      if (in.x) {
        g_skinPreview = -1;  // (one preview at a time)
        const XrQuaternionf q = S.head.orientation;
        XrVector3f f = q_rot(q, XrVector3f{0.0f, 0.0f, -1.0f});
        f.y = 0.0f;
        const float len = std::sqrt(f.x * f.x + f.z * f.z);
        f = len > 1e-3f ? XrVector3f{f.x / len, 0.0f, f.z / len} : XrVector3f{0.0f, 0.0f, -1.0f};
        XrPosef where;
        where.orientation = XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f};
        where.position = v_add(S.head.position, XrVector3f{0.75f * f.x, -0.12f, 0.75f * f.z});  // (~75 cm out, just under your eyes)
        xrballs::preview(ball_item(ball).id, where);
        say("");
        *redraw = true;
      }
      return in.a || in.x || *stick;
    }
    if (in.a) {
      if (look_owned(row, col)) {
        look_equip(row, col);
        say(upper(look_item(row, col).name) + " IS ON");
      } else {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_lkArmKind = row;  // the hub asks (its confirm)
        g_lkArm = col;
      }
      ++g_gen;
      *redraw = true;
    }
    if (in.x) {
      xrballs::stop_preview();  // r168: (one preview at a time)
      g_skinPreview = col;  // (room_binder: the binder in that skin in front of you)
      say("");
      *redraw = true;
    }
    return in.a || in.x || *stick;
  }
  // r152: TIZM GRADING (LT from SETS): the stick over your slabs (the top row) and the cards you can send (the grid); A on a
  // card arms its send, A again sends it (its coins); A on a slab back takes it out in front of you; X on a slab back puts
  // it on / off your room's slab shelf (kDisplaySlabs at most); RT back to SETS
  if (g_page.load() == kPageGrading) {
    int look = -1, send = -1;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      // r157: a send asking (the hub's confirm pop-up): A sends it, B says no, nothing else moves
      if (g_gradeArmed >= 0) {
        if (in.a) send = g_gradeArmed;
        if (in.b) {
          g_gradeArmed = -1;
          g_gradeSay.clear();
          *redraw = true;
        }
        *stick = true;
        if (send < 0) return in.a || in.b || in.x || in.y || in.step;
      }
    }
    if (send < 0) {
      std::lock_guard<std::mutex> lock(g_mutex);
      const std::vector<int> slabs = grading_slab_order(), tiles = grading_tiles();
      const int nS = static_cast<int>(slabs.size()), nT = static_cast<int>(tiles.size()), n = nS + nT;
      int c = std::max(0, std::min(std::max(0, n - 1), g_gradeCursor));
      if (in.step && n > 0) {
        bool moved = false;
        if (c < nS) {  // the slabs' row
          const int col = c - g_gradeLeft;
          if (in.dir == 1 && c > 0) --c, moved = true;
          if (in.dir == 2 && c + 1 < nS) ++c, moved = true;
          if (in.dir == 4 && nT > 0) c = nS + g_gradeTop * kGCols + std::min(std::max(0, col), kGCols - 1), moved = true;
        } else {  // the cards' grid
          const int i = c - nS, col = i % kGCols, row = i / kGCols;
          if (in.dir == 1 && col > 0) --c, moved = true;
          if (in.dir == 2 && col < kGCols - 1 && i + 1 < nT) ++c, moved = true;
          if (in.dir == 3) {
            if (row > 0) c -= kGCols, moved = true;
            else if (nS > 0) c = std::min(nS - 1, g_gradeLeft + col), moved = true;
          }
          if (in.dir == 4 && i + kGCols < nT) c += kGCols, moved = true;
          else if (in.dir == 4 && row < (nT - 1) / kGCols) c = nS + nT - 1, moved = true;
        }
        c = std::max(0, std::min(std::max(0, n - 1), c));
        if (c < nS) {  // (the slabs shown: the cursor's in them)
          if (c < g_gradeLeft) g_gradeLeft = c;
          if (c >= g_gradeLeft + kGSlabCols) g_gradeLeft = c - kGSlabCols + 1;
        } else {
          const int row = (c - nS) / kGCols;
          if (row < g_gradeTop) g_gradeTop = row;
          if (row >= g_gradeTop + kGRows) g_gradeTop = row - kGRows + 1;
        }
        *stick = true;  // r157
        if (moved) {
          g_gradeArmed = -1;
          *redraw = true;
        }
      }
      g_gradeCursor = c;
      if ((in.a || in.x) && c < nS) {
        Slab& s = g_slabs[static_cast<size_t>(slabs[static_cast<size_t>(c)])];
        if (s.left > 0) {
          g_gradeSay = "AT TIZM GRADING  -  BACK AFTER " + std::to_string(s.left) + (s.left == 1 ? " MORE FRANCHISE MATCH" : " MORE FRANCHISE MATCHES");
        } else if (in.x) {
          int on = 0;
          for (const Slab& o : g_slabs) on += o.show ? 1 : 0;
          if (!s.show && on >= kDisplaySlabs) {
            g_gradeSay = "YOUR SLAB SHELF HOLDS " + std::to_string(kDisplaySlabs) + "  -  TAKE ONE OFF FIRST (X)";
          } else {
            s.show = !s.show;
            g_gradeSay = s.show ? upper(g_set[s.ref.card].name) + " ON YOUR SLAB SHELF" : upper(g_set[s.ref.card].name) + " OFF YOUR SLAB SHELF";
            save_collection();
            ++g_gen;
          }
        } else {
          look = slabs[static_cast<size_t>(c)];
        }
        *redraw = true;
      }
      if ((in.a || in.x) && c >= nS && c < n) {
        const int card = tiles[static_cast<size_t>(c - nS)];
        if (in.x) {
          g_gradeSay = "A SENDS IT (IT ASKS FIRST)  -  X IS FOR YOUR SLABS (THE TOP ROW)";
        } else {
          // r157: A asks (the hub's one confirm: A YES / B NO) instead of r152's A, then A again within 4 s
          g_gradeArmed = card;
          g_gradeArmedNs = mono_ns();
          g_gradeSay.clear();
        }
        *redraw = true;
      }
    }
    if (send >= 0) {
      Slab sent;
      const int r = grade_send(send, &sent);
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_gradeArmed = -1;
        g_gradeSay = r == 0 ? "SENT TO TIZM GRADING  -  BACK AFTER " + std::to_string(kGradeMatches) + " FRANCHISE MATCHES (CERT #" + std::to_string(sent.cert) + ")"
                            : (r == 1 ? "NOT ENOUGH BIT COINS YET  -  PLAY FRANCHISE MATCHES" : "NOTHING OF IT LEFT TO SEND");
      }
      buzz(r == 0 ? 0.35f : 0.15f, r == 0 ? 0.08f : 0.04f);
    }
    if (look >= 0) S.askSlab = look;
    return in.a || in.x || *stick;
  }
  // r140: the shop (LT from the binder's first pages): its two offers side by side (the stick), A buys, RT the binder;
  // r146: LT the trade-ins
  if (g_page.load() == kPageShop) {
    // r236 (LOOK B: CARDS > PACKS, the old SHOP + opening): the hero (your packs + OPEN) over three products (TIZM / JAM '96 /
    // EXQUISITE: name + price); up / down between them, left / right the product, X its box (BOX OF 5 / THE VAULT) or back, A
    // opens one (the hero) / buys (a product), Y opens one from anywhere on it (as the binder's Y did)
    const bool havePacks = !g_packs.empty();
    if (in.y || (in.a && havePacks && g_packsOnOpen.load())) {
      if (havePacks) S.askOpen = true;
      *redraw = true;
      return true;
    }
    if (in.step && in.dir >= 1 && in.dir <= 4) {
      int set = g_shopPick.load() / 2, box = g_shopPick.load() % 2;
      bool onOpen = g_packsOnOpen.load() && havePacks;
      if (in.dir == 3 && havePacks) onOpen = true;
      if (in.dir == 4) onOpen = false;
      if (!onOpen && in.dir == 1 && set > 0) set -= 1, box = 0;
      if (!onOpen && in.dir == 2 && set < 2) set += 1, box = 0;
      const int want = set * 2 + box;
      if (want != g_shopPick.load() || onOpen != g_packsOnOpen.load()) {
        g_shopPick = want;
        g_packsOnOpen = onOpen;
        *redraw = true;
      }
    }
    if (in.x && !(g_packsOnOpen.load() && havePacks)) {  // the product's box / case (or back to one pack)
      g_shopPick = g_shopPick.load() ^ 1;
      *redraw = true;
      return true;
    }
    if (in.step) *stick = true;  // r157: the stick stays on the page
    if (in.a && !(g_packsOnOpen.load() && havePacks)) {
      const bool box = g_shopPick.load() % 2 == 1, jam = g_shopPick.load() / 2 == 1, exq = g_shopPick.load() / 2 == 2;
      const int r = buy_packs(box, g_shopPick.load() / 2);
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        const std::string what = exq ? (box ? "THE VAULT IS YOURS - 5 LANCINI EXQUISITE PACKS" : "A LANCINI EXQUISITE PACK IS YOURS")
                                 : jam ? (box ? "A JAM '96 BOX OF 5 PACKS IS YOURS" : "A JAM '96 PACK IS YOURS") : (box ? "A BOX OF 5 PACKS IS YOURS" : "A PACK IS YOURS");
        g_shopSay = r == 0 ? what + (box ? "  -  OPEN THEM HERE" : "  -  OPEN IT HERE")
                           : (r == 1 ? "NOT ENOUGH BIT COINS YET  -  PLAY FRANCHISE MATCHES" : "NO ROOM FOR MORE PACKS");
      }
      buzz(r == 0 ? 0.35f : 0.15f, r == 0 ? 0.08f : 0.04f);
      *redraw = true;
    }
    return in.a || *stick;
  }
  // the binder
  bool used = false;
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::vector<int> list = binder_list();
  const int spreads = std::max(1, static_cast<int>((list.size() + kPerSpread - 1) / kPerSpread));
  // r171 (Lance: "a one button flip of pages for the binder without it being pulled out 3D ... manually flipping through
  // every slot to change a page is crazy"): the RIGHT stick left / right turns a whole spread (each flick, the hub's repeat
  // when held), the cursor staying on the same pocket of the new spread; the left stick still moves it. (Not while your 3D
  // binder's out: its own pages.)
  if (in.turn != 0 && !binder_held()) {
    const int to = std::max(0, std::min(spreads - 1, g_spread + in.turn));
    if (to != g_spread) {
      g_spread = to;
      *redraw = true;
      buzz(0.12f, 0.02f);
    }
    *stick = true;
  }
  if (in.step) {
    const int page = g_cursor / 9, s = g_cursor % 9;
    int col = page * 3 + s % 3, row = s / 3;
    bool moved = false;
    if (in.dir == 3 && row > 0) --row, moved = true;
    if (in.dir == 4 && row < 2) ++row, moved = true;
    if (in.dir == 1 && col > 0) --col, moved = true;
    if (in.dir == 2 && col < 5) ++col, moved = true;
    // r157 (menu overhaul): the stick past the binder's edge turns its pages (the hub's 350 ms first repeat, so a page
    // doesn't turn by accident); LT / RT are the hub's sections now
    if (in.dir == 1 && col == 0 && !moved && g_spread > 0) {
      --g_spread;
      col = 5;
      moved = true;
    } else if (in.dir == 2 && col == 5 && !moved && g_spread < spreads - 1) {
      ++g_spread;
      col = 0;
      moved = true;
    }
    *stick = true;
    g_cursor = (col / 3) * 9 + row * 3 + col % 3;
    if (moved) *redraw = true;
  }
  const size_t at = static_cast<size_t>(g_spread) * kPerSpread + g_cursor;
  const int cursorCard = at < list.size() ? list[at] : -1;
  const bool owned = owns(cursorCard);
  // r149: a NEW card looked at - the cursor resting on its pocket a moment - isn't new any more
  if (cursorCard >= 0 && g_newCards.count(g_set[cursorCard].id) != 0) {
    if (S.newLook != cursorCard) {
      S.newLook = cursorCard;
      S.newLookNs = now;
    } else if (secs(now, S.newLookNs) > 0.8f) {
      g_newCards.erase(g_set[cursorCard].id);
      save_collection();
      S.newLook = -1;
      *redraw = true;
    }
  } else {
    S.newLook = -1;
  }
  // r157: Y opens a pack (printed on the packs, the hint bar's extra); A takes the card under the ring out (LOOK)
  // r160: X takes your 3D binder out into the room / puts it away (room_binder does it, this frame)
  if (in.x) {
    g_binderToggle = true;
    used = true;
  }
  if (in.y) {  // r236: Y = the binder's SETS (its bonuses); packs open on CARDS > PACKS (A, or Y there)
    g_page = kPageSets;
    ++g_gen;
    *redraw = true;
    used = true;
  } else if (in.a && owned) {
    S.askLook = cursorCard;
    used = true;
  } else if (in.a) {
    used = true;  // (a card you don't have yet: nothing to take out)
  }
  return used || *stick;
}

int rail_page() {
  const int p = g_page.load();
  if (p == kPageSets) return kRailBinder;  // r236: SETS is the BINDER's other view (Y)
  for (int i = 0; i < kRailPages; ++i)
    if (kRailOrder[i] == p) return i;
  return 0;
}
void set_rail_page(int i) {
  i = std::max(0, std::min(kRailPages - 1, i));
  if (kRailOrder[i] == g_page.load()) return;
  if ((g_page.load() == kPageExplosions || g_page.load() == kPageTakeovers) && g_fxPreviewing) {  // (leaving it stops a preview)
    xrgoalfx::stop();
    g_fxPreviewing = false;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_shopSay.clear();
    g_tradeSay.clear();
    g_gradeSay.clear();
    g_fxSay.clear();
    g_gradeArmed = -1;
    g_fxArmed = -1;
    g_lkArmKind = g_lkArm = -1;  // r160: (leaving LOOKS: a buy asking dropped, its preview stopped)
    g_lkSay.clear();
  }
  g_skinPreview = -1;
  if (xrballs::previewing()) xrballs::stop_preview();  // r168
  g_page = kRailOrder[i];
  ++g_gen;
}
const char* rail_name(int i) { return kRailNames[std::max(0, std::min(kRailPages - 1, i))]; }
void set_hub_fit(float ox, float oy, float k) {
  g_fitOx = ox;
  g_fitOy = oy;
  g_fitK = k;
}
bool wants_y() {  // (r201: the market's COUNTER; r213: the collectors' SEND / COUNTER)
  return g_page.load() == kPageBinder || g_page.load() == kPageMarket || g_page.load() == kPageMuseum || g_page.load() == kPageCollectors;
}
#ifdef HARNESS_REAL_CARDS  // (tools/outside/menus_render157: the pop-up pages drawn without their 3D)
void harness_scene(int mode) { g_sceneMode = mode; }
void harness_page(int railIndex, bool sets) {  // r236: a page by its rail index (sets: the BINDER's SETS view)
  set_rail_page(railIndex);
  if (sets) g_page = kPageSets;
}
void harness_wear(int skin, int sleeve) {  // r160 round 2: a skin on (the binder page's renders; r163: no sleeves)
  (void)sleeve;
  std::lock_guard<std::mutex> lock(g_lookM);
  looks_read_l();
  g_lookOn[0] = skin;
}
// r166: a TIZM BOOM's real pictures for the PC proof (tools/outside/boom166_proof.py): its front, its show's pictures (and a
// BOOM pocket's look through its binder page), and its show's quads' places frame by frame (logged: POSE lines)
}  // namespace xrcards
extern "C" void host_save_png(const char* name, int len, const uint8_t* px, int w, int h);
extern "C" void host_log(const char* s, int len);
namespace xrcards {
void harness_boom(const char* who) {
  load_set();
  int at = 0;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (upper(g_set[i].name) == upper(who)) at = static_cast<int>(i);
  if (g_set.empty()) return;
  const Card& c = g_set[static_cast<size_t>(at)];
  auto save = [](const std::string& name, std::vector<uint8_t> px, int w, int h) {
    for (size_t i = 0; i < px.size(); i += 4)  // (the host wants premultiplied)
      for (int k = 0; k < 3; ++k) px[i + k] = static_cast<uint8_t>(px[i + k] * px[i + 3] / 255);
    host_save_png(name.c_str(), static_cast<int>(name.size()), px.data(), w, h);
  };
  Ref r;
  r.card = at;
  r.hit = kHitBoom;
  const std::string tag = "boom166_" + c.key;
  save(tag + "_front_flat.png", draw_front(c, r, static_cast<int>(g_set.size()), 0.0f, kSpNone, 2), kFrontW, kFrontH);
  save(tag + "_front_show.png", draw_front(c, r, static_cast<int>(g_set.size()), 0.0f, kSpBoom, 0), kFrontW, kFrontH);
  Ref g = r;
  g.parallel = kGoldTizm;
  g.serial = 7;
  save(tag + "_front_gold.png", draw_front(c, g, static_cast<int>(g_set.size()), 0.0f, kSpBoom, 0), kFrontW, kFrontH);
  std::vector<std::vector<uint8_t>> tex;
  std::vector<int> tw, th;
  boom_layers(c, r, &tex, &tw, &th);
  for (size_t k = 0; k < tex.size(); ++k) save(tag + "_tex" + std::to_string(k) + ".png", tex[k], tw[k], th[k]);
  char line[160];
  for (int f = 0; f <= static_cast<int>(kBoomLen * 30.0f) + 3; ++f) {
    const float u = f / 30.0f;
    for (int o = 0; o < kBoomQuads; ++o) {
      XrVector3f p;
      float w = 0, h = 0, rot = 0;
      if (!boom_place(o, u, u, &p, &w, &h, &rot)) continue;
      const int n = std::snprintf(line, sizeof(line), "POSE %d %d %d %.5f %.5f %.5f %.5f %.5f %.5f", f, o, boom_tex_of(o), p.x, p.y, p.z, w, h, rot);
      host_log(line, n);
    }
  }
}
// r171: a HOMETOWN diorama / a GOLD vault's real geometry and motion for the PC proof (tools/outside/r171_proof.py): its
// pictures, and its vertices every frame of 5 s (home_moves / gold_fx_moves themselves) packed 24-bit into PNG rows
namespace {
void harness_pack(const std::string& name, const std::vector<std::vector<float>>& rows, float lo, float hi) {
  if (rows.empty()) return;
  const int w = static_cast<int>(rows[0].size()), h = static_cast<int>(rows.size());
  std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const float v = clampf((rows[static_cast<size_t>(y)][static_cast<size_t>(x)] - lo) / (hi - lo), 0.0f, 1.0f);
      const uint32_t q = static_cast<uint32_t>(std::lround(v * 16777215.0f));
      uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
      p[0] = (q >> 16) & 255, p[1] = (q >> 8) & 255, p[2] = q & 255, p[3] = 255;
    }
  host_save_png(name.c_str(), static_cast<int>(name.size()), px.data(), w, h);
}
void harness_savetex(const std::string& name, std::vector<uint8_t> px, int w, int h) {
  for (size_t i = 0; i < px.size(); i += 4)
    for (int k = 0; k < 3; ++k) px[i + k] = static_cast<uint8_t>(px[i + k] * px[i + 3] / 255);
  host_save_png(name.c_str(), static_cast<int>(name.size()), px.data(), w, h);
}
}  // namespace
void harness_home(const char* name) {
  HomeMesh H;
  Fit fit;
  fit.feet = {0.0f, -kHomeWinHalfY + 0.0015f, -0.02f};
  fit.scale = 0.05f, fit.bw = 0.9f, fit.bh = 1.2f, fit.bd = 0.6f;
  home_scene(H, name, 12345u, fit, true);
  std::vector<std::vector<uint8_t>> tex;
  std::vector<int> tw, th;
  home_pictures(H, name, &tex, &tw, &th);
  const std::string tag = std::string("home171_") + name;
  for (size_t k = 0; k < tex.size(); ++k) harness_savetex(tag + "_tex" + std::to_string(k) + ".png", tex[k], tw[k], th[k]);
  Special sp;
  sp.homeBase = 0;
  sp.homeN = static_cast<int>(H.v.size() / 6);
  sp.homeRest = H.v;
  sp.homeAnim = H.anim;
  sp.homeFx = H.fx;
  sp.homeLo = H.lo;
  sp.homeHi = H.hi;
  sp.verts.assign(H.v.size(), 0.0f);
  const XrQuaternionf q = q_axis(0, 0, 1, -1.5707963f);  // (held landscape)
  const uint64_t t0 = 5000000000000ull;
  std::vector<std::vector<float>> rows;
  for (int f = 0; f < 150; ++f) {
    const uint64_t now = t0 + static_cast<uint64_t>(f) * 33333333ull;
    home_moves(sp, true, q, 20.0f + f / 30.0f, now, 1.0f / 30.0f);
    rows.push_back(sp.verts);
  }
  harness_pack(tag + "_verts.png", rows, -0.5f, 0.5f);
  std::vector<std::vector<float>> uv{H.uv};
  harness_pack(tag + "_uv.png", uv, 0.0f, 1.0f);
  std::vector<float> ix;
  std::string parts = "HOMEPARTS " + tag;
  for (int p = 0; p < kHpCount; ++p) {
    parts += " " + std::to_string(H.idx[p].size());
    for (uint32_t i : H.idx[p]) ix.push_back(static_cast<float>(i));
  }
  std::vector<std::vector<float>> ixr{ix};
  harness_pack(tag + "_idx.png", ixr, 0.0f, 16777215.0f);
  host_log(parts.c_str(), static_cast<int>(parts.size()));
}
void harness_gold(const char* who) {
  load_set();
  int at = 0;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (upper(g_set[i].name) == upper(who)) at = static_cast<int>(i);
  if (g_set.empty()) return;
  const Card& c = g_set[static_cast<size_t>(at)];
  Ref r;
  r.card = at;
  r.parallel = kGoldTizm;
  r.serial = 3;
  std::vector<std::vector<uint8_t>> tex;
  std::vector<int> tw, th;
  room_pictures(c, kSpPop, false, &tex, &tw, &th);
  const std::string tag = "gold171_" + c.key;
  harness_savetex(tag + "_front.png", draw_front(c, r, static_cast<int>(g_set.size()), 0.0f, kSpWindow, 0), kFrontW, kFrontH);
  for (size_t k = 0; k < tex.size(); ++k) harness_savetex(tag + "_tex" + std::to_string(k) + ".png", tex[k], tw[k], th[k]);
  Special sp;
  sp.goldFxBase = 0;
  sp.goldShut = 0.0f;
  sp.fit.scale = 0.05f, sp.fit.bh = 1.0f;
  sp.verts.assign(static_cast<size_t>(6 + kGoldTwinkles) * 4 * 6, 0.0f);
  GoldPose gp;
  gp.fig = true;
  gp.top = 0.0f;
  gp.feet = {0.0f, kWinBottom + 0.0015f, -0.022f};
  std::vector<std::vector<float>> rows;
  for (int f = 0; f < 150; ++f) {
    gold_fx_moves(sp, true, 20.0f + f / 30.0f, gp);
    rows.push_back(sp.verts);
  }
  harness_pack(tag + "_fx.png", rows, -0.5f, 0.5f);
  char line[200];
  const int n = std::snprintf(line, sizeof(line), "GOLDROOM %s %.5f %.5f %.5f %.5f %d", tag.c_str(), kWinHalf, kWinBottom, kWinTop, kRoomD, kGoldTwinkles);
  host_log(line, n);
  const int n2 = std::snprintf(line, sizeof(line), "HOMEWIN %.5f %.5f", kHomeWinHalfX, kHomeWinHalfY);
  host_log(line, n2);
}
// r210: the CARD MUSEUM's card atlas for the PC proof (tools/outside/cards210): `spec` = up to 9 items "key:parallel:grade" or "-" (an
// empty spot), ';' between. Saved straight (each picture's colour with alpha 255 + its alpha as grey): museum210_atlas (r210's,
// museum_atlas itself) and its mips (museum210_mip<i>, mip_chain_srgb itself)
void harness_museum210(const char* spec) {
  load_set();
  std::vector<xrmarket::Variant> items;
  std::string s(spec), tok;
  for (size_t i = 0; i <= s.size(); ++i) {
    if (i < s.size() && s[i] != ';') {
      tok += s[i];
      continue;
    }
    xrmarket::Variant v;
    char key[96] = {};
    int par = 0, grade = 0;
    if (tok != "-" && std::sscanf(tok.c_str(), "%95[^:]:%d:%d", key, &par, &grade) >= 1) {
      auto it = g_byKey.find(key);
      if (it != g_byKey.end()) {
        v.card = it->second;
        v.parallel = par;
        v.serial = par == kGoldTizm ? 7 : (par == kBlack ? 1 : 0);
        v.grade = grade;
        v.cert = grade > 0 ? 20261110u + static_cast<uint32_t>(items.size()) : 0u;
      }
    }
    items.push_back(v);
    tok.clear();
  }
  std::vector<uint8_t> atlas;
  if (!museum_atlas(items, &atlas)) return;
  auto save2 = [](const std::string& name, const std::vector<uint8_t>& px, int w, int h) {
    std::vector<uint8_t> rgb = px, a = px;
    for (size_t i = 0; i < px.size(); i += 4) {
      rgb[i + 3] = 255;
      a[i] = a[i + 1] = a[i + 2] = px[i + 3];
      a[i + 3] = 255;
    }
    host_save_png((name + "_rgb.png").c_str(), static_cast<int>(name.size() + 8), rgb.data(), w, h);
    host_save_png((name + "_a.png").c_str(), static_cast<int>(name.size() + 6), a.data(), w, h);
  };
  save2("museum210_atlas", atlas, 1024, 2048);
  const std::vector<std::vector<uint8_t>> mips = mip_chain_srgb(atlas, 1024, 2048);
  for (size_t i = 0; i < mips.size() && i < 8; ++i) save2("museum210_mip" + std::to_string(i + 1), mips[i], std::max(1, 1024 >> (i + 1)), std::max(1, 2048 >> (i + 1)));
  char line[96];
  const int n = std::snprintf(line, sizeof(line), "MUSEUM210 %zu items, %zu mips", items.size(), mips.size());
  host_log(line, n);
}
// r211: Lance's new pull rates for the PC proof (tools/outside/odds211). mode 1: the pack's back (its ODDS) as the headset draws it
// (odds211_pack_back.png; _flat: its foil alpha made solid to look at), then `packs` packs rolled as add_packs rolls them (its own GOD
// PACK roll, roll_pack, god_cards) - every card of the set in the pools, each pack against an empty binder (the pure odds) - counted:
// ODDS lines (regular packs: not god-rolled; all: every pack). mode 2: the one-of-each-ever rule - every card's 1/1 already yours,
// `packs` packs: no BLACK comes (each becomes a GOLD /10)
void harness_odds211(int mode, int packs) {
  load_set();
  if (g_set.empty()) return;
  char line[256];
  auto say = [&](int k) { host_log(line, std::min(k, static_cast<int>(sizeof(line)) - 1)); };
  if (mode == 1) {
    std::vector<uint8_t> px = draw_pack(true, static_cast<int>(g_set.size()));
    harness_savetex("odds211_pack_back.png", px, kPackW, kPackH);
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    harness_savetex("odds211_pack_back_flat.png", px, kPackW, kPackH);
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  for (Card& c : g_set) c.here = true;
  g_owned.assign(g_set.size(), Owned{});
  g_packs.clear();
  if (mode == 2)
    for (Owned& o : g_owned) o.n[kBlack][0] = 1;
  // [0] regular packs, [1] every pack
  long long packsN[2] = {}, cards[2] = {}, fin[2][4] = {}, hit[2][4] = {}, pkCards[2] = {}, shiny[2] = {}, rar[2][3] = {};
  long long withBlack[2] = {}, withGold[2] = {}, withSilver[2] = {}, withHit[2] = {}, godRolled = 0, godFlagged = 0, godNatural = 0;
  const uint64_t salt = mode == 1 ? 0x2110DD5A17C0FFEEull : 0x2112B1AC0F0E1E55ull;
  for (int i = 0; i < packs; ++i) {
    uint64_t rng = salt ^ (static_cast<uint64_t>(i + 1) * 0x9E3779B97F4A7C15ull);  // (add_packs' own seeding)
    const bool god = mix64(rng) % kGodOdds == 0;
    const Pack p = roll_pack(rng, {}, god);
    const bool flagged = god_cards(p);
    godRolled += god ? 1 : 0;
    godFlagged += flagged ? 1 : 0;
    godNatural += flagged && !god ? 1 : 0;
    for (int g = god ? 1 : 0; g < 2; ++g) {
      ++packsN[g];
      bool b = false, gd = false, sv = false, h = false;
      for (const Ref& r : p.cards) {
        if (r.card < 0) continue;
        ++cards[g];
        ++fin[g][r.parallel];
        ++hit[g][r.hit];
        ++rar[g][g_set[static_cast<size_t>(r.card)].rarity];
        if (pk_key(g_set[static_cast<size_t>(r.card)].key)) ++pkCards[g], shiny[g] += r.shiny ? 1 : 0;
        b = b || r.parallel == kBlack, gd = gd || r.parallel == kGoldTizm, sv = sv || r.parallel == kSilver, h = h || r.hit != kHitNone;
      }
      withBlack[g] += b, withGold[g] += gd, withSilver[g] += sv, withHit[g] += h;
    }
  }
  say(std::snprintf(line, sizeof(line), "ODDS211 mode %d packs %d set %d | rates %d %d %d | hits %d %d %d | shiny %d | god %d", mode, packs,
                    static_cast<int>(g_set.size()), kSilverOdds, kGoldOdds, kBlackOdds, kHitIn[1], kHitIn[2], kHitIn[3], kShinyOdds, kGodOdds));
  for (int g = 0; g < 2; ++g) {
    const char* w = g == 0 ? "regular" : "all";
    say(std::snprintf(line, sizeof(line), "ODDS %s packs %lld cards %lld base %lld silver %lld gold %lld black %lld", w, packsN[g], cards[g], fin[g][0], fin[g][1],
                      fin[g][2], fin[g][3]));
    say(std::snprintf(line, sizeof(line), "ODDS %s hits boom %lld home %lld signed %lld none %lld | pokemon %lld shiny %lld | C %lld R %lld SR %lld", w, hit[g][1],
                      hit[g][2], hit[g][3], hit[g][0], pkCards[g], shiny[g], rar[g][0], rar[g][1], rar[g][2]));
    say(std::snprintf(line, sizeof(line), "ODDS %s packswith blackp %lld goldp %lld silverp %lld hitp %lld", w, withBlack[g], withGold[g], withSilver[g], withHit[g]));
  }
  say(std::snprintf(line, sizeof(line), "ODDS god rolled %lld flagged %lld natural %lld", godRolled, godFlagged, godNatural));
  g_owned.assign(g_set.size(), Owned{});
}
// r213: the collectors' 1/1s and the SHIPPING pack roller (tools/outside/trades213: cards213.cpp). A career (seed 0x213213), an empty binder,
// every card in the pools; `packs` packs rolled as add_packs rolls them, each against what the world holds: mode 1 every card's 1/1 a
// collector's (told: world_set) - no BLACK may come; mode 2 every other card's - BLACKs only of the rest; mode 3 none - BLACKs at r211's
// rate; mode 4 none told, the career's world11 file (every card) read the first time a 1/1 is rolled - no BLACK may come
void harness_world213(int mode, int packs) {
  load_set();
  if (g_set.empty()) return;
  char line[256];
  auto say = [&](int k) { host_log(line, std::min(k, static_cast<int>(sizeof(line)) - 1)); };
  std::lock_guard<std::mutex> lock(g_mutex);
  const unsigned was = g_careerSeed;
  for (Card& c : g_set) c.here = true;
  g_owned.assign(g_set.size(), Owned{});
  g_packs.clear();
  g_careerSeed = 0x213213u;
  std::vector<std::string> keys;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (mode == 1 || (mode == 2 && i % 2 == 0)) keys.push_back(g_set[i].key);
  if (mode == 4) {
    std::lock_guard<std::mutex> wl(g_worldM);
    g_worldCareer = 0;  // (not told: the file)
    g_worldKeys.clear();
  } else {
    world_set(g_careerSeed, keys);
  }
  long long cards = 0, blackIn = 0, blackOut = 0, gold = 0;
  for (int i = 0; i < packs; ++i) {
    uint64_t rng = 0x2131B1AC00000000ull ^ (static_cast<uint64_t>(i + 1) * 0x9E3779B97F4A7C15ull);
    const bool god = mix64(rng) % kGodOdds == 0;
    const Pack p = roll_pack(rng, {}, god);
    for (const Ref& r : p.cards) {
      if (r.card < 0) continue;
      ++cards;
      gold += r.parallel == kGoldTizm ? 1 : 0;
      if (r.parallel == kBlack) ((mode == 1 || mode == 4 || (mode == 2 && r.card % 2 == 0)) ? blackIn : blackOut)++;
    }
  }
  say(std::snprintf(line, sizeof(line), "WORLD213 mode %d packs %d cards %lld told %d black_world %lld black_other %lld gold %lld", mode, packs, cards,
                    static_cast<int>(keys.size()), blackIn, blackOut, gold));
  {
    std::lock_guard<std::mutex> wl(g_worldM);
    g_worldCareer = 0;
    g_worldKeys.clear();
  }
  g_owned.assign(g_set.size(), Owned{});
  g_careerSeed = was;
}
// r235: the collectors' rips through the SHIPPING trade_rip (tools/outside/collect235). A career (seed 0x235235), every card in the pools;
// `packs` packs of each set (0 LANCINI TIZM, 1 JAM '96, 2 EXQUISITE): mode 1 every card's 1/1 + every EXQUISITE number a collector's (told) -
// no BLACK, no MASTERPIECE, no told number may come; mode 2 nothing told, an empty binder - the rates; mode 3 every 1/1 YOURS (the binder) -
// no BLACK. Logged: RIP235 mode set packs cards black gold silver hits shiny | exq tiers 1..10 | dup numbers
void harness_rip235(int mode, int packs) {
  load_set();
  if (g_set.empty()) return;
  char line[400];
  auto say = [&](int k) { host_log(line, std::min(k, static_cast<int>(sizeof(line)) - 1)); };
  const unsigned was = g_careerSeed;
  std::vector<std::string> keys;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (Card& c : g_set) c.here = true;
    g_owned.assign(g_set.size(), Owned{});
    g_packs.clear();
    g_careerSeed = 0x235235u;
    if (mode == 3)  // (an EXQUISITE 1/1 is its MASTERPIECE, kept whole in Owned::hits)
      for (size_t i = 0; i < g_set.size(); ++i) {
        if (g_set[i].set == 2) g_owned[i].hits.push_back(exq_ref(static_cast<int>(i), xrexq::kMasterpiece, 1));
        else g_owned[i].n[kBlack][0] = 1;
      }
    if (mode == 1)
      for (size_t i = 0; i < g_set.size(); ++i) {
        keys.push_back(g_set[i].id);
        if (g_set[i].set == 2)  // (every EXQUISITE number but the GALLERY /399s: those may still come)
          for (int t = xrexq::kSilverLeaf; t < xrexq::kTiers; ++t)
            for (int n = 1; n <= xrexq::kRun[t]; ++n) keys.push_back(g_set[i].id + "#" + std::to_string(xrexq::serial_of(t, n)));
      }
    std::sort(keys.begin(), keys.end());
    world_set(g_careerSeed, keys);
  }
  for (int set = 0; set < 3; ++set) {
    long long cards = 0, fin[4] = {}, hits = 0, shiny = 0, tiers[xrexq::kTiers] = {}, dups = 0, toldNum = 0, wrongSet = 0;
    for (int i = 0; i < packs; ++i) {
      std::vector<xrmarket::Variant> got;
      trade_rip(set, 0x2350000000ull ^ (static_cast<uint64_t>(set) << 48) ^ (static_cast<uint64_t>(i + 1) * 0x9E3779B97F4A7C15ull), &got);
      for (const xrmarket::Variant& v : got) {
        ++cards;
        wrongSet += g_set[static_cast<size_t>(v.card)].set != set ? 1 : 0;
        if (set == 2) {
          const int t = xrexq::tier_of(v.serial);
          ++tiers[t];
          const std::string k = g_set[static_cast<size_t>(v.card)].id + "#" + std::to_string(v.serial);
          if (std::binary_search(keys.begin(), keys.end(), k) || (mode == 1 && t == xrexq::kMasterpiece)) ++toldNum;
          // (the rips here are not added to any world: a number may repeat across packs - the trades module tells each after its rip)
        } else {
          ++fin[std::max(0, std::min(3, v.parallel))];
          hits += v.hit != kHitNone ? 1 : 0;
          shiny += v.shiny ? 1 : 0;
        }
      }
    }
    say(std::snprintf(line, sizeof(line), "RIP235 mode %d set %d packs %d cards %lld black %lld gold %lld silver %lld hits %lld shiny %lld wrongset %lld | exq %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld | told %lld dups %lld",
                      mode, set, packs, cards, fin[3], fin[2], fin[1], hits, shiny, wrongSet, tiers[1], tiers[2], tiers[3], tiers[4], tiers[5], tiers[6], tiers[7], tiers[8],
                      tiers[9], tiers[10], toldNum, dups));
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::lock_guard<std::mutex> wl(g_worldM);
    g_worldCareer = 0;
    g_worldKeys.clear();
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_owned.assign(g_set.size(), Owned{});
  g_careerSeed = was;
}
// r183: a card's fronts by key (the ORIGINALS' proof): BASE, SILVER, GOLD /10, BLACK 1/1, HOMETOWN, SIGNED, BOOM, and its
// HOMETOWN's home (logged: HOMEOF <key> <suite> <place>); its sets logged (SETOF <set> <cards>)
void harness_fronts(const char* key) {
  load_set();
  auto it = g_byKey.find(key);
  if (it == g_byKey.end()) return;
  const Card& c = g_set[static_cast<size_t>(it->second)];
  const int n = static_cast<int>(g_set.size());
  const std::string tag = std::string("card183_") + key;
  struct V {
    const char* name;
    int parallel, serial, hit;
  };
  static const V kV[] = {{"base", kBase, 0, kHitNone},     {"silver", kSilver, 0, kHitNone}, {"gold", kGoldTizm, 7, kHitNone},
                         {"black", kBlack, 1, kHitNone},   {"hometown", kBase, 0, kHitHome}, {"signed", kBase, 0, kHitSigned},
                         {"boom", kBase, 0, kHitBoom}};
  for (const V& v : kV) {
    Ref r;
    r.card = it->second;
    r.parallel = v.parallel;
    r.serial = v.serial;
    r.hit = v.hit;
    harness_savetex(tag + "_" + v.name + ".png", draw_front(c, r, n, 0.0f, kSpNone, 2), kFrontW, kFrontH);
  }
  std::string place;
  const std::string suite = home_backdrop(c, &place);
  char line[256];
  int k = std::snprintf(line, sizeof(line), "HOMEOF %s %s %s | %s #%d %s rating %d", key, suite.c_str(), place.c_str(), c.name.c_str(),
                        c.num, kSeriesName[c.series], c.rating);
  host_log(line, k);
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    build_sets();
    k = std::snprintf(line, sizeof(line), "SETS lines %d types %d (last %s %d cards, first %d; this card %d)", static_cast<int>(g_lines.size()),
                      static_cast<int>(g_types.size()), g_types.back().name.c_str(), static_cast<int>(g_types.back().cards.size()),
                      g_types.back().cards.empty() ? -1 : g_types.back().cards[0], it->second);
    host_log(line, k);
    for (const std::vector<CardSet>* list : {&g_lines, &g_types})
      for (const CardSet& s : *list) {
        bool in = false;
        for (int i : s.cards) in = in || i == it->second;
        if (!in) continue;
        {
          std::string who;
          for (int i : s.cards) who += " " + g_set[static_cast<size_t>(i)].key;
          k = std::snprintf(line, sizeof(line), "SETOF %s (%d):%s", s.name.c_str(), static_cast<int>(s.cards.size()), who.c_str());
          host_log(line, std::min(k, static_cast<int>(sizeof(line)) - 1));
        }
      }
  }
}
void harness_looks(int row, int col, int armKind, int arm) {  // r160: the LOOKS page's cursor / a buy asking
  std::lock_guard<std::mutex> lock(g_mutex);
  // r168: row 1 = the BALL SKINS row (col its ball; armKind 1 a ball's buy asking); row 0 the binder skins
  g_lkBallFocus = row == kLookBall;
  if (row == kLookBall) {
    g_lkBall = col;
    row = 0;
  } else {
    g_lkCol[row] = col;
  }
  g_lkRow = row;
  g_lkArmKind = armKind;
  g_lkArm = arm;
}
#endif  // r153's declaration (r157: Y = OPEN A PACK in the binder)

namespace {
// ===================================================================================== r152: TIZM GRADING's slab
// A slab out in front of you (kOut with S.outSlab): its card as any card out of the binder (the cards' model, a window's
// room), round it the slab - one model in the pack's slot (the pack's never out with it), light and shade quads
// (xr_fx_model.frag -4.5, its card's own frame each frame): its label (LANCINI TIZM GRADING, its name, its grade huge) and
// its back (TIZM GRADING, its cert) solid - a shade, then its picture just in front -, its clear case soft over the card
// (its faces a breath darker with a glare across them, its edges and bevels bright as thick acrylic catches the light,
// the frosted rails round the card). Its BACK FROM GRADING reveal: a padded envelope rises in front of you, the slab slides
// up out of it, the envelope drops away, the slab comes to you and its label flips up - its grade's sound; a GEM MINT 10
// a burst of gold.
enum { kStFlat, kStGlare, kStLabel, kStBack, kStEnvelope, kStCount };
struct SlabPics {
  std::mutex m;
  bool done = false;
  std::vector<std::vector<uint8_t>> tex;
  std::vector<int> tw, th;
};
constexpr float kSlX = 0.069f, kSlB = -0.093f, kSlT = 0.132f, kSlZ = 0.0045f;  // (its case round the card: m at size 1)
constexpr float kLabX = 0.063f, kLabY0 = 0.090f, kLabY1 = 0.126f;            // (its label, over the card's top edge)
constexpr int kLabW = 960, kLabH = 274;
struct SlabView {
  int k = -1;  // (the slab its model's for)
  uint32_t cert = 0;
  std::shared_ptr<SlabPics> pics;
  bool up = false;
  std::vector<float> verts;
  int nQuads = 0;
};
SlabView g_sv;
// its pictures (a thread): a glare, its label, its label's back, the envelope
std::vector<uint8_t> slab_glare() {
  const int n = 256;
  std::vector<uint8_t> px(static_cast<size_t>(n) * n * 4, 0);
  for (int y = 0; y < n; ++y)
    for (int x = 0; x < n; ++x) {
      const float u = (x + 0.5f) / n, v = (y + 0.5f) / n, d = u - 0.85f * v;
      const float a = 0.85f * std::exp(-(d - 0.05f) * (d - 0.05f) / 0.004f) + 0.45f * std::exp(-(d + 0.28f) * (d + 0.28f) / 0.0012f) +
                      0.12f * std::exp(-((u - 0.2f) * (u - 0.2f) + (v - 0.15f) * (v - 0.15f)) / 0.03f);
      const size_t i = (static_cast<size_t>(y) * n + x) * 4;
      px[i] = px[i + 1] = px[i + 2] = static_cast<uint8_t>(std::lround(clampf(a, 0.0f, 1.0f) * 255.0f));
      px[i + 3] = 255;
    }
  return px;
}
std::vector<uint8_t> slab_back_px(uint32_t cert, int grade) {
  Pc P;
  const float w = kLabW, h = kLabH;
  P.box(0, 0, w, h, 18, 0xDCE1EA, 1.0f, 0x7E889A);
  prism(P, 0, 0, w, h, w * 0.3f, h * 0.5f, 22, 0.1f, 0xFFFFFF, 0xBFC7D6, 0x8E97A8, 0.55f);
  holo_bands(P, 4, 4, w - 4, h - 4, 30, grade == 10 ? 0.22f : 0.12f, 0.4f);
  P.ring(0, 0, w, h, 18, 5, 0x1A2233);
  hexagon(P, 90, 137, 64, 0x1A2233);
  hexagon(P, 90, 137, 46, 0xFFFFFF);
  P.left("TIZM GRADING", 180, 92, 54, 520, 0x0B1022, 2);
  P.left("AUTHENTIC  -  SEALED  -  " + std::string(kSetName), 182, 150, 24, 560, 0x1A2233, 0);
  for (int k = 0; k < 36; ++k) {  // its barcode, from its cert
    const uint32_t bit = (cert * 2654435761u >> (k % 29)) ^ static_cast<uint32_t>(k * 7);
    const float bx = 182 + k * 13.0f, bw = (bit & 3) == 0 ? 7.0f : ((bit & 3) == 1 ? 3.0f : 5.0f);
    P.box(bx, 180, bx + bw, 236, 0, 0x0B1022);
  }
  P.right("CERT #" + std::to_string(cert), w - 30, 214, 34, 300, 0x0B1022, 0);
  return board_px(kLabW, kLabH, P);
}
std::vector<uint8_t> envelope_px() {
  constexpr int W = 512, H = 640;
  Pc P;
  P.box(0, 0, W, H, 22, 0xE9C97E, 1.0f, 0xCFA65A);
  P.dots(10, 10, W - 10, H - 10, 26, 0.18f, 0.24f, 0xF6DFA2, 0.55f);  // (its bubbles through the paper)
  P.box(0, 0, W, 96, 18, 0xD8B262, 1.0f, 0xC59A4C);                    // (its flap)
  P.poly({0, 96, W, 96, W - 18, 120, 18, 120}, 0xB8893E, 0.6f);
  P.box(66, 170, W - 66, 400, 14, 0xFBFBF8);  // (its label)
  P.ring(66, 170, W - 66, 400, 14, 3, 0x1A2233);
  hexagon(P, 128, 236, 38, 0x1A2233);
  hexagon(P, 128, 236, 26, 0x8E9BB4);
  P.left("TIZM GRADING", 180, 222, 36, 250, 0x0B1022, 2);
  P.left("RETURN SHIPMENT", 182, 262, 24, 260, 0x3A4A70, 0);
  P.box(96, 300, W - 96, 304, 2, 0xB8C0D0);
  P.text("GRADED CARD INSIDE", W / 2, 332, 30, W - 160, 0x0B1022, 0);
  P.text("HANDLE WITH CARE", W / 2, 370, 24, W - 160, 0x5A6684, 0);
  P.style(0.0f, 1.0f, -8.0f);
  P.box(120, 470, W - 120, 560, 10, 0xD3202A, 0.92f);
  P.text("FRAGILE", W / 2, 515, 56, W - 260, 0xFFFFFF, 0);
  P.plain();
  return board_px(W, H, P);
}
void slab_prepare(int k) {
  Card c;
  Slab s;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (k < 0 || k >= static_cast<int>(g_slabs.size())) return;
    s = g_slabs[static_cast<size_t>(k)];
    c = g_set[s.ref.card];
  }
  auto p = std::make_shared<SlabPics>();
  g_sv = SlabView{};
  g_sv.k = k;
  g_sv.cert = s.cert;
  g_sv.pics = p;
  std::thread([p, c, s]() {
    std::vector<std::vector<uint8_t>> tex(kStCount);
    std::vector<int> tw(kStCount), th(kStCount);
    tex[kStFlat].assign(8 * 8 * 4, 255);
    tw[kStFlat] = th[kStFlat] = 8;
    tex[kStGlare] = slab_glare();
    tw[kStGlare] = th[kStGlare] = 256;
    {
      Pc P;
      slab_label(P, c, s.ref, s.grade, s.cert, kLabW, kLabH);
      tex[kStLabel] = board_px(kLabW, kLabH, P);
    }
    tw[kStLabel] = kLabW;
    th[kStLabel] = kLabH;
    tex[kStBack] = slab_back_px(s.cert, s.grade);
    tw[kStBack] = kLabW;
    th[kStBack] = kLabH;
    tex[kStEnvelope] = envelope_px();
    tw[kStEnvelope] = 512;
    th[kStEnvelope] = 640;
    for (int t : {kStLabel, kStBack, kStEnvelope})  // (light: premultiplied)
      for (size_t i = 0; i < tex[t].size(); i += 4)
        for (int ch = 0; ch < 3; ++ch) tex[t][i + ch] = static_cast<uint8_t>(tex[t][i + ch] * tex[t][i + 3] / 255);
    std::lock_guard<std::mutex> lock(p->m);
    p->tex = std::move(tex);
    p->tw = std::move(tw);
    p->th = std::move(th);
    p->done = true;
  }).detach();
}
// its quads, in order: solid - the shades (label, its back, the envelope's two sides), the label, its back, the envelope's
// two sides; soft - the case's flat light / shade (rails 4, sides 4 + 4 shades, bevels 4 + 4, its faces' shades 2), its
// glare (front, back)
constexpr int kSqShade = 0, kSqLabel = 4, kSqBack = 5, kSqEnv = 6, kSqFlat = 8, kSqGlare = 30, kSqCount = 32;
bool slab_model_up(bool fxReady) {
  if (!fxReady || !g_sv.pics) return false;
  {
    std::lock_guard<std::mutex> lock(g_sv.pics->m);
    if (!g_sv.pics->done) return false;
  }
  const SlabPics& P = *g_sv.pics;
  xrfx::Model m;
  for (int t = 0; t < kStCount; ++t) {
    m.texRgba.push_back(P.tex[t]);
    m.texW.push_back(static_cast<uint32_t>(P.tw[t]));
    m.texH.push_back(static_cast<uint32_t>(P.th[t]));
  }
  int verts = 0;
  auto quad = [&](bool mirror) {
    const float us[4] = {mirror ? 1.0f : 0.0f, mirror ? 0.0f : 1.0f, mirror ? 1.0f : 0.0f, mirror ? 0.0f : 1.0f}, vs[4] = {0, 0, 1, 1};
    for (int k = 0; k < 4; ++k) {
      m.uv.push_back(us[k]);
      m.uv.push_back(vs[k]);
    }
    const uint32_t a = static_cast<uint32_t>(verts);
    m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
    verts += 4;
  };
  auto part = [&](int tex, bool soft, int from, int to) {
    xrfx::ModelPart p;
    p.tex = tex;
    p.soft = soft;
    p.firstIndex = static_cast<uint32_t>(from * 6);
    p.indexCount = static_cast<uint32_t>((to - from) * 6);
    m.parts.push_back(p);
  };
  for (int q = 0; q < kSqCount; ++q) quad(q == kSqBack || q == kSqEnv + 1);  // (the back's and the envelope's back read from behind)
  part(kStFlat, false, kSqShade, kSqLabel);
  part(kStLabel, false, kSqLabel, kSqBack);
  part(kStBack, false, kSqBack, kSqEnv);
  part(kStEnvelope, false, kSqEnv, kSqFlat);
  part(kStFlat, true, kSqFlat, kSqGlare);
  part(kStGlare, true, kSqGlare, kSqCount);
  m.vertexCount = static_cast<uint32_t>(verts);
  g_sv.verts.assign(static_cast<size_t>(verts) * 6, 0.0f);
  g_sv.nQuads = kSqCount;
  xrfx::set_model(kPackSlot, std::move(m));
  g_sv.up = xrfx::model_up(kPackSlot);
  CLOG("TIZM GRADING slab: its model up%s", g_sv.up ? "" : " - failed");
  return g_sv.up;
}
void slab_end(bool fxReady) {
  if (g_sv.up && fxReady) xrfx::set_model(kPackSlot, xrfx::Model{});
  g_sv = SlabView{};
}
// a quad from its corners (top left, top right, bottom left, bottom right; world), lit rgb x k - or a shade, k its darkness
void slab_put(int q, const XrVector3f p[4], uint32_t rgb, float k, bool shade) {
  float* o = g_sv.verts.data() + static_cast<size_t>(q) * 24;
  if (k <= 0.002f) {
    std::fill(o, o + 24, 0.0f);
    return;
  }
  float r = ((rgb >> 16) & 255) / 255.0f * k, g = ((rgb >> 8) & 255) / 255.0f * k, b = (rgb & 255) / 255.0f * k;
  if (shade) r = g = b = -std::min(1.0f, k) - 1e-4f;
  for (int i = 0; i < 4; ++i) {
    o[i * 6 + 0] = p[i].x;
    o[i * 6 + 1] = p[i].y;
    o[i * 6 + 2] = p[i].z;
    o[i * 6 + 3] = r;
    o[i * 6 + 4] = g;
    o[i * 6 + 5] = b;
  }
}
// every frame (frame(), kOut): its model up when its pictures are; its reveal; its quads in its card's frame
void slab_frame(xrfx::Scene& scene, float dt, uint64_t now, bool fxReady) {
  if (S.mode != kOut || S.outSlab < 0) {
    if (g_sv.k >= 0) slab_end(fxReady);
    return;
  }
  if (g_sv.k != S.outSlab || g_sv.cert != S.slab.cert) slab_prepare(S.outSlab);
  if (!g_sv.up && !slab_model_up(fxReady)) return;
  Sheet& s = S.card[0];
  // its reveal: the envelope, the slab sliding out, the label flipping up (t s in)
  const float t = S.gradeNs != 0 ? secs(now, S.gradeNs) : 99.0f;
  float labelA = 0.0f;  // (its label turned back off upright, radians)
  const XrVector3f A = S.revealAt;
  XrVector3f away = v_sub(A, S.head.position);
  away.y = 0.0f;
  away = v_mul(away, 1.0f / std::max(1e-3f, v_len(away)));
  XrVector3f E = A;
  float envDrop = -1.0f;  // (the envelope gone: < 0)
  if (S.gradeNs != 0) {
    const XrVector3f E1 = v_add(A, XrVector3f{0.0f, -0.06f, 0.0f});
    if (t < 0.6f) {
      E = v_lerp(v_add(A, XrVector3f{0.0f, -0.5f, 0.0f}), E1, 1.0f - (1.0f - t / 0.6f) * (1.0f - t / 0.6f));
      envDrop = 0.0f;
    } else if (t < 2.2f) {
      const float u = clampf((t - 1.45f) / 0.65f, 0.0f, 1.0f);
      E = v_add(E1, XrVector3f{0.0f, -1.1f * u * u, 0.0f});
      envDrop = u;
    }
    // the slab: inside it, sliding up out of it (behind its front), then to you
    const XrVector3f in = v_add(v_add(E1, v_mul(away, 0.02f)), XrVector3f{0.0f, -0.02f, 0.0f});
    const XrVector3f out = v_add(v_add(E1, v_mul(away, 0.02f)), XrVector3f{0.0f, 0.24f, 0.0f});
    if (t < 0.65f) s.pos = in;
    else if (t < 1.45f) s.pos = v_lerp(in, out, smooth((t - 0.65f) / 0.8f));
    else s.pos = v_lerp(out, A, smooth(clampf((t - 1.45f) / 0.5f, 0.0f, 1.0f)));
    s.lastPos = s.pos;
    s.free = true;
    s.shown = s.shown && t >= 0.6f;
    labelA = t < 2.0f ? 1.5707963f : 1.5707963f * (1.0f - clampf((t - 2.0f) / 0.3f, 0.0f, 1.0f));
    if (t >= 2.0f && t < 2.3f) labelA -= 0.25f * std::sin(3.14159265f * (t - 2.0f) / 0.3f);  // (a little past, back)
    if (t >= 2.0f && !S.gradeHit && labelA < 0.6f) {  // its grade's moment
      S.gradeHit = true;
      const int g = S.slab.grade;
      card_sound(3);
      if (g == 10) {
        play_wav("gold", 1.0f);
        play_wav("shiny", 0.9f);
        S.burst = true;
        S.burstRgb = kGold;
        S.burstAt = v_add(A, XrVector3f{0.0f, 0.16f, 0.0f});
        S.sparkleAt = A;
        S.sparkleR = 0.2f;
        S.sparkleT = 3.0f;
        buzz(1.0f, 0.45f);
      } else if (g == 9) {
        play_wav("silver", 1.0f);
        S.sparkleAt = A;
        S.sparkleR = 0.15f;
        S.sparkleT = 1.5f;
        buzz(0.6f, 0.2f);
      } else {
        play_wav(g >= 7 ? "rare" : "base", 1.0f);
        buzz(0.35f, 0.1f);
      }
      CLOG("TIZM GRADING: %s %d (%s)", kGradeName[g], g, pull_text(S.slab.ref).c_str());
    }
    if (t > 2.6f) {  // (done: held as any card out)
      S.gradeNs = 0;
      s.free = false;
      s.tPos = A;
      g_sceneMode = 2;
      ++g_gen;
    }
  }
  const bool show = s.on && s.shown && s.scale > 0.01f;
  const XrQuaternionf q = sheet_q(s);
  const float sc = s.scale;
  auto W = [&](float x, float y, float z) { return v_add(s.pos, q_rot(q, XrVector3f{x * sc, y * sc, z * sc})); };
  auto rect = [&](int qi, float x0, float y0, float x1, float y1, float z, uint32_t rgb, float k, bool shade) {
    const XrVector3f p[4] = {W(x0, y1, z), W(x1, y1, z), W(x0, y0, z), W(x1, y0, z)};
    slab_put(qi, p, rgb, show ? k : 0.0f, shade);
  };
  // its label (turned back by labelA about its foot) and its back
  auto label = [&](int qi, float z, bool shade, uint32_t rgb) {
    const float cs = std::cos(labelA), sn = std::sin(labelA), hgt = kLabY1 - kLabY0;
    auto L = [&](float x, float up) { return W(x, kLabY0 + up * cs, z - up * sn); };
    const XrVector3f p[4] = {L(-kLabX, hgt), L(kLabX, hgt), L(-kLabX, 0.0f), L(kLabX, 0.0f)};
    slab_put(qi, p, rgb, show ? 1.0f : 0.0f, shade);
  };
  label(kSqShade + 0, 0.0007f, true, 0);
  label(kSqShade + 1, -0.0007f, true, 0);
  label(kSqLabel, 0.0009f, false, 0xFFFFFF);
  label(kSqBack, -0.0009f, false, 0xFFFFFF);
  // the envelope (its two sides, turned to you)
  if (envDrop >= 0.0f && S.gradeNs != 0) {
    const XrQuaternionf fq = q_mul(facing(E), q_axis(0, 0, 1, envDrop * 0.6f));
    const float ew = 0.105f * s.tScale * 1.6f, eh = 0.155f * s.tScale * 1.6f;
    auto Ev = [&](float x, float y, float z) { return v_add(E, q_rot(fq, XrVector3f{x, y, z})); };
    const XrVector3f f[4] = {Ev(-ew, eh, 0.004f), Ev(ew, eh, 0.004f), Ev(-ew, -eh, 0.004f), Ev(ew, -eh, 0.004f)};
    const XrVector3f fs[4] = {Ev(-ew, eh, 0.0035f), Ev(ew, eh, 0.0035f), Ev(-ew, -eh, 0.0035f), Ev(ew, -eh, 0.0035f)};
    const XrVector3f b[4] = {Ev(ew, eh, -0.004f), Ev(-ew, eh, -0.004f), Ev(ew, -eh, -0.004f), Ev(-ew, -eh, -0.004f)};
    const XrVector3f bs[4] = {Ev(ew, eh, -0.0035f), Ev(-ew, eh, -0.0035f), Ev(ew, -eh, -0.0035f), Ev(-ew, -eh, -0.0035f)};
    slab_put(kSqShade + 2, fs, 0, 1.0f, true);
    slab_put(kSqShade + 3, bs, 0, 1.0f, true);
    slab_put(kSqEnv, f, 0xFFFFFF, 1.0f, false);
    slab_put(kSqEnv + 1, b, 0xFFFFFF, 1.0f, false);
  } else {
    for (int qi : {kSqShade + 2, kSqShade + 3, kSqEnv, kSqEnv + 1}) slab_put(qi, nullptr, 0, 0.0f, false);
  }
  // the clear case: the frosted rails round the card, its four sides (bright edges, a shade), its bevels, its faces
  int f = kSqFlat;
  rect(f++, -kSlX + 0.002f, kSlB + 0.002f, -0.0605f, kSlT - 0.002f, 0.0f, 0xE8F0FF, 0.1f, false);
  rect(f++, 0.0605f, kSlB + 0.002f, kSlX - 0.002f, kSlT - 0.002f, 0.0f, 0xE8F0FF, 0.1f, false);
  rect(f++, -0.0605f, kSlB + 0.002f, 0.0605f, -0.0845f, 0.0f, 0xE8F0FF, 0.1f, false);
  rect(f++, -0.0605f, 0.0845f, 0.0605f, kLabY0, 0.0f, 0xE8F0FF, 0.1f, false);
  for (int side = 0; side < 2; ++side) {  // (its sides: light, then their shade)
    const bool sh = side == 1;
    const uint32_t c = 0xDDEBFF;
    const float k = sh ? 0.15f : 0.45f;
    const XrVector3f l[4] = {W(-kSlX, kSlT, -kSlZ), W(-kSlX, kSlT, kSlZ), W(-kSlX, kSlB, -kSlZ), W(-kSlX, kSlB, kSlZ)};
    const XrVector3f r[4] = {W(kSlX, kSlT, kSlZ), W(kSlX, kSlT, -kSlZ), W(kSlX, kSlB, kSlZ), W(kSlX, kSlB, -kSlZ)};
    const XrVector3f tp[4] = {W(-kSlX, kSlT, -kSlZ), W(kSlX, kSlT, -kSlZ), W(-kSlX, kSlT, kSlZ), W(kSlX, kSlT, kSlZ)};
    const XrVector3f bt[4] = {W(-kSlX, kSlB, kSlZ), W(kSlX, kSlB, kSlZ), W(-kSlX, kSlB, -kSlZ), W(kSlX, kSlB, -kSlZ)};
    slab_put(f++, l, c, show ? k : 0.0f, sh);
    slab_put(f++, r, c, show ? k : 0.0f, sh);
    slab_put(f++, tp, c, show ? k : 0.0f, sh);
    slab_put(f++, bt, c, show ? k : 0.0f, sh);
  }
  for (int face = 0; face < 2; ++face) {  // its bevels: thin bright strips round its front and back
    const float z = face == 0 ? kSlZ + 0.0001f : -kSlZ - 0.0001f, k = face == 0 ? 0.55f : 0.3f, e = 0.0018f;
    rect(f++, -kSlX, kSlT - e, kSlX, kSlT, z, 0xFFFFFF, k, false);
    rect(f++, -kSlX, kSlB, kSlX, kSlB + e, z, 0xFFFFFF, k, false);
    rect(f++, -kSlX, kSlB, -kSlX + e, kSlT, z, 0xFFFFFF, k, false);
    rect(f++, kSlX - e, kSlB, kSlX, kSlT, z, 0xFFFFFF, k, false);
  }
  rect(f++, -kSlX, kSlB, kSlX, kSlT, kSlZ, 0, 0.06f, true);
  rect(f++, -kSlX, kSlB, kSlX, kSlT, -kSlZ, 0, 0.06f, true);
  const float sweep = 0.04f * std::sin(static_cast<float>(static_cast<double>(now % 100000000000ull) * 1e-9) * 0.7f);
  rect(kSqGlare, -kSlX, kSlB + sweep, kSlX, kSlT + sweep, kSlZ + 0.0002f, 0xFFFFFF, 0.3f, false);
  rect(kSqGlare + 1, kSlX, kSlB, -kSlX, kSlT, -kSlZ - 0.0002f, 0xFFFFFF, 0.15f, false);
  (void)dt;
  xrfx::Scene::ModelDraw& md = scene.models[kPackSlot];
  md.on = true;
  md.posNrm = g_sv.verts.data();
  md.local = false;
  md.white = 0.0f;
  md.alpha = 1.0f;
  md.glow = 0.0f;
  md.under = -4.5f;  // (light and shade, each quad tinted: xr_fx_model.frag)
  md.glowRgb = 0xFFFFFF;
  md.flameFrame = -1;
  md.shine = 0.0f;
  md.frame = -1;
  md.kaleido = 0u;
}

}  // namespace

// ===================================================================================== r152: the slab shelf in your room
// Up to kDisplaySlabs slabs on a black two-step stand (three on its front step, three on its back), each leaning back a
// touch, a warm light under each and a light strip along its front; one model (the pack's slot while no cards scene is
// up), light and shade quads in the room (xr_fx_model.frag -4.5): the stand and the slabs' cards and labels solid, their
// glass soft. Its pictures (each slab's card's front, its label) drawn on a thread when what's on it changes.
namespace {
struct DispPics {
  std::mutex m;
  bool done = false;
  std::vector<std::vector<uint8_t>> tex;
  std::vector<int> tw, th;
};
struct DispQuad {  // (a quad as laid out this frame)
  int tex;
  bool soft, mirror;
  XrVector3f p[4];
  uint32_t rgb;
  float k;
  bool shade;
};
struct Display {
  bool shown = false, placed = false, read = false;
  XrVector3f pos{0.0f, 0.0f, 0.0f};
  float yaw = 0.0f, scale = 1.0f;
  // its grab: by which hands; one hand: its offset from the hand; both: where they started (their middle, their line's
  // turn and length), its turn, size and place from their middle then
  bool hold[2] = {false, false};
  XrVector3f off{0.0f, 0.0f, 0.0f}, mid0{0.0f, 0.0f, 0.0f}, rel0{0.0f, 0.0f, 0.0f};
  float ang0 = 0.0f, dist0 = 1.0f, yaw0 = 0.0f, scale0 = 1.0f;
  bool prevGrip[2] = {false, false};
  // what's on it, its pictures, its model
  std::string sig;
  std::vector<Slab> slabs;
  std::vector<Card> cards;
  std::shared_ptr<DispPics> pics;
  bool up = false;
  std::vector<float> verts;
  std::vector<DispQuad> quads;
};
Display g_disp;
std::string disp_path() { return home_dir() + "/cards/slab_shelf-" + std::to_string(g_careerSeed) + ".txt"; }  // r153: a career's
void disp_save() {
  ::mkdir((home_dir() + "/cards").c_str(), 0775);
  if (FILE* f = std::fopen(disp_path().c_str(), "w")) {
    std::fprintf(f, "# r152: TIZM GRADING's slab shelf in your room: x y z (LOCAL m) yaw (rad) size\nplaced %.4f %.4f %.4f %.4f %.3f\n",
                 g_disp.pos.x, g_disp.pos.y, g_disp.pos.z, g_disp.yaw, g_disp.scale);
    std::fclose(f);
  }
}
void disp_read() {
  g_disp.read = true;
  if (FILE* f = std::fopen(disp_path().c_str(), "r")) {
    char line[256];
    while (std::fgets(line, sizeof(line), f) != nullptr)
      if (std::sscanf(line, "placed %f %f %f %f %f", &g_disp.pos.x, &g_disp.pos.y, &g_disp.pos.z, &g_disp.yaw, &g_disp.scale) == 5) {
        g_disp.placed = true;
        g_disp.scale = clampf(g_disp.scale, 0.4f, 3.0f);
      }
    std::fclose(f);
  }
}
// the slabs on it: the ones you put on it, else your best back (g_mutex held)
std::vector<Slab> disp_pick() {
  std::vector<Slab> out;
  for (const Slab& s : g_slabs)
    if (s.left == 0 && s.show && out.size() < static_cast<size_t>(kDisplaySlabs)) out.push_back(s);
  if (out.empty())
    for (int k : grading_slab_order()) {
      const Slab& s = g_slabs[static_cast<size_t>(k)];
      if (s.left == 0 && out.size() < static_cast<size_t>(kDisplaySlabs)) out.push_back(s);
    }
  return out;
}
XrVector3f disp_w(const XrVector3f& l) {  // (its own frame - x along it, y up, z out of its front - into the room)
  const float c = std::cos(g_disp.yaw), s = std::sin(g_disp.yaw), k = g_disp.scale;
  return {g_disp.pos.x + (l.x * c + l.z * s) * k, g_disp.pos.y + l.y * k, g_disp.pos.z + (-l.x * s + l.z * c) * k};
}
// this frame's quads (the same ones in the same order every frame: solid shades, solid pictures, soft)
void disp_layout(float t) {
  std::vector<DispQuad>& Q = g_disp.quads;
  Q.clear();
  const int n = static_cast<int>(g_disp.slabs.size());
  auto box = [&](float x0, float x1, float y0, float y1, float z0, float z1, std::vector<DispQuad>* shades, std::vector<DispQuad>* lights) {
    // (its front, top and two ends: a shade each, then its dark lacquer lit from above)
    const XrVector3f fr[4] = {disp_w({x0, y1, z1}), disp_w({x1, y1, z1}), disp_w({x0, y0, z1}), disp_w({x1, y0, z1})};
    const XrVector3f tp[4] = {disp_w({x0, y1, z0}), disp_w({x1, y1, z0}), disp_w({x0, y1, z1}), disp_w({x1, y1, z1})};
    const XrVector3f lf[4] = {disp_w({x0, y1, z0}), disp_w({x0, y1, z1}), disp_w({x0, y0, z0}), disp_w({x0, y0, z1})};
    const XrVector3f rt[4] = {disp_w({x1, y1, z1}), disp_w({x1, y1, z0}), disp_w({x1, y0, z1}), disp_w({x1, y0, z0})};
    const XrVector3f* faces[4] = {fr, tp, lf, rt};
    const uint32_t lit[4] = {0x1C1E26, 0x2E313C, 0x15171D, 0x15171D};
    for (int f = 0; f < 4; ++f) {
      DispQuad s{kStFlat, false, false, {faces[f][0], faces[f][1], faces[f][2], faces[f][3]}, 0, 1.0f, true};
      shades->push_back(s);
      DispQuad l = s;
      const float lift = 0.0006f;  // (just in front of its shade)
      const XrVector3f nrm = f == 0 ? XrVector3f{0, 0, lift} : (f == 1 ? XrVector3f{0, lift, 0} : (f == 2 ? XrVector3f{-lift, 0, 0} : XrVector3f{lift, 0, 0}));
      for (int i = 0; i < 4; ++i) l.p[i] = v_add(l.p[i], v_mul(v_sub(disp_w(nrm), disp_w({0, 0, 0})), 1.0f));
      l.rgb = lit[f];
      l.shade = false;
      lights->push_back(l);
    }
  };
  std::vector<DispQuad> shades, lights, soft;
  box(-0.31f, 0.31f, 0.0f, 0.035f, -0.10f, 0.10f, &shades, &lights);   // its base
  box(-0.31f, 0.31f, 0.035f, 0.135f, -0.10f, -0.01f, &shades, &lights);  // its back step
  // the slabs: its card and label (solid: a shade, its picture just in front), its glass (soft)
  constexpr float kS = 0.6f;
  for (int i = 0; i < n; ++i) {
    const bool front = i < 3;
    const float x = (static_cast<float>(i % 3) - 1.0f) * 0.19f, yb = front ? 0.039f : 0.139f, zc = front ? 0.045f : -0.055f;
    const float lean = -0.14f, cl = std::cos(lean), sl = std::sin(lean);
    auto S2 = [&](float lx, float ly, float lz) {  // (a point of the slab, card-local m at size 1)
      const float y = (ly - kSlB) * kS, z = lz * kS;
      return disp_w({x + lx * kS, yb + y * cl - z * sl, zc + y * sl + z * cl});
    };
    auto rect = [&](float x0, float y0, float x1, float y1, float z) {
      return std::array<XrVector3f, 4>{S2(x0, y1, z), S2(x1, y1, z), S2(x0, y0, z), S2(x1, y0, z)};
    };
    const int texCard = kStCount + 2 * i, texLab = kStCount + 2 * i + 1;
    auto add = [&](std::vector<DispQuad>* to, int tex, bool sft, const std::array<XrVector3f, 4>& p, uint32_t rgb, float k, bool shade) {
      to->push_back(DispQuad{tex, sft, false, {p[0], p[1], p[2], p[3]}, rgb, k, shade});
    };
    add(&shades, kStFlat, false, rect(-0.06f, -0.084f, 0.06f, 0.084f, 0.0f), 0, 1.0f, true);
    add(&shades, kStFlat, false, rect(-kLabX, kLabY0, kLabX, kLabY1, 0.0f), 0, 1.0f, true);
    add(&lights, texCard, false, rect(-0.06f, -0.084f, 0.06f, 0.084f, 0.0008f), 0xFFFFFF, 1.0f, false);
    add(&lights, texLab, false, rect(-kLabX, kLabY0, kLabX, kLabY1, 0.0008f), 0xFFFFFF, 1.0f, false);
    add(&soft, kStFlat, true, rect(-kSlX, kSlB, kSlX, kSlT, kSlZ), 0, 0.06f, true);
    const float sweep = 0.05f * std::sin(t * 0.6f + i);
    add(&soft, kStGlare, true, rect(-kSlX, kSlB + sweep, kSlX, kSlT + sweep, kSlZ + 0.0002f), 0xFFFFFF, 0.28f, false);
    const float e = 0.0022f;
    add(&soft, kStFlat, true, rect(-kSlX, kSlT - e, kSlX, kSlT, kSlZ + 0.0003f), 0xFFFFFF, 0.6f, false);
    add(&soft, kStFlat, true, rect(-kSlX, kSlB, kSlX, kSlB + e, kSlZ + 0.0003f), 0xFFFFFF, 0.6f, false);
    add(&soft, kStFlat, true, rect(-kSlX, kSlB, -kSlX + e, kSlT, kSlZ + 0.0003f), 0xFFFFFF, 0.6f, false);
    add(&soft, kStFlat, true, rect(kSlX - e, kSlB, kSlX, kSlT, kSlZ + 0.0003f), 0xFFFFFF, 0.6f, false);
    add(&soft, kStFlat, true, rect(-kSlX + e, kSlB + e, kSlX - e, kSlT - e, 0.0f), 0xE8F0FF, 0.08f, false);  // (its rails, frosted)
    // a warm light behind it, a gem mint's gold
    const XrVector3f gc = disp_w({x, yb + 0.07f, zc - 0.02f});
    const XrVector3f gr = v_sub(disp_w({x + 0.11f, 0, 0}), disp_w({x, 0, 0})), gu = v_sub(disp_w({0, 0.13f, 0}), disp_w({0, 0, 0}));
    soft.push_back(DispQuad{kStCount - 1 + 0, true, false, {v_add(v_sub(gc, gr), gu), v_add(v_add(gc, gr), gu), v_sub(v_sub(gc, gr), gu),
                                                             v_sub(v_add(gc, gr), gu)},
                            g_disp.slabs[static_cast<size_t>(i)].grade == 10 ? 0xFFC24Au : 0xFFE6B0u, 0.35f, false});
  }
  // its light strip along its front
  const XrVector3f ls[4] = {disp_w({-0.29f, 0.03f, 0.1006f}), disp_w({0.29f, 0.03f, 0.1006f}), disp_w({-0.29f, 0.024f, 0.1006f}),
                            disp_w({0.29f, 0.024f, 0.1006f})};
  soft.push_back(DispQuad{kStFlat, true, false, {ls[0], ls[1], ls[2], ls[3]}, 0xFFE8C0, 1.0f, false});
  Q.insert(Q.end(), shades.begin(), shades.end());
  Q.insert(Q.end(), lights.begin(), lights.end());
  Q.insert(Q.end(), soft.begin(), soft.end());
}
void disp_prepare() {
  auto p = std::make_shared<DispPics>();
  g_disp.pics = p;
  g_disp.up = false;
  const std::vector<Slab> slabs = g_disp.slabs;
  const std::vector<Card> cards = g_disp.cards;
  int total = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    total = g_setN;  // (r217: a set's count)
  }
  std::thread([p, slabs, cards, total]() {
    std::vector<std::vector<uint8_t>> tex;
    std::vector<int> tw, th;
    auto add = [&](std::vector<uint8_t> px, int w, int h) {
      tex.push_back(std::move(px));
      tw.push_back(w);
      th.push_back(h);
    };
    add(std::vector<uint8_t>(8 * 8 * 4, 255), 8, 8);
    add(slab_glare(), 256, 256);
    add(std::vector<uint8_t>(8 * 8 * 4, 255), 8, 8);  // (kStLabel .. kStEnvelope: unused here)
    add(std::vector<uint8_t>(8 * 8 * 4, 255), 8, 8);
    {  // (kStCount - 1: the warm light's glow)
      constexpr int g = 64;
      std::vector<uint8_t> px(static_cast<size_t>(g) * g * 4, 0);
      for (int y = 0; y < g; ++y)
        for (int x = 0; x < g; ++x) {
          const float u = (x + 0.5f) / g * 2 - 1, v = (y + 0.5f) / g * 2 - 1, d = std::sqrt(u * u + v * v);
          const uint8_t a = static_cast<uint8_t>(std::lround(clampf(d < 1 ? (1 - d) * (1 - d) : 0.0f, 0.0f, 1.0f) * 255.0f));
          px[(static_cast<size_t>(y) * g + x) * 4 + 0] = px[(static_cast<size_t>(y) * g + x) * 4 + 1] = px[(static_cast<size_t>(y) * g + x) * 4 + 2] = a;
          px[(static_cast<size_t>(y) * g + x) * 4 + 3] = 255;
        }
      add(std::move(px), g, g);
    }
    for (size_t i = 0; i < slabs.size(); ++i) {
      const std::vector<uint8_t> front = draw_front(cards[i], slabs[i].ref, total, 4.0f);
      // a third its size (3 x 3 to one); solid: its corners cut, straight colours (the light adds them)
      constexpr int sw = kFrontW / 3, sh = kFrontH / 3;
      std::vector<uint8_t> small(static_cast<size_t>(sw) * sh * 4, 0);
      for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
          int acc[4] = {0, 0, 0, 0};
          for (int dy = 0; dy < 3; ++dy)
            for (int dx = 0; dx < 3; ++dx)
              for (int ch = 0; ch < 4; ++ch) acc[ch] += front[(static_cast<size_t>(y * 3 + dy) * kFrontW + x * 3 + dx) * 4 + ch];
          uint8_t* o = &small[(static_cast<size_t>(y) * sw + x) * 4];
          const bool in = acc[3] / 9 >= 77;
          for (int ch = 0; ch < 3; ++ch) o[ch] = in ? static_cast<uint8_t>(acc[ch] / 9) : 0;
          o[3] = in ? 255 : 0;
        }
      add(std::move(small), sw, sh);
      Pc P;
      slab_label(P, cards[i], slabs[i].ref, slabs[i].grade, slabs[i].cert, 768, 219);
      std::vector<uint8_t> lab = board_px(768, 219, P);
      for (size_t q = 0; q < lab.size(); q += 4)
        for (int ch = 0; ch < 3; ++ch) lab[q + ch] = static_cast<uint8_t>(lab[q + ch] * lab[q + 3] / 255);
      add(std::move(lab), 768, 219);
    }
    std::lock_guard<std::mutex> lock(p->m);
    p->tex = std::move(tex);
    p->tw = std::move(tw);
    p->th = std::move(th);
    p->done = true;
  }).detach();
}
bool disp_model_up(bool fxReady, float t) {
  if (!fxReady || !g_disp.pics) return false;
  {
    std::lock_guard<std::mutex> lock(g_disp.pics->m);
    if (!g_disp.pics->done) return false;
  }
  disp_layout(t);
  xrfx::Model m;
  {
    std::lock_guard<std::mutex> lock(g_disp.pics->m);
    for (size_t k = 0; k < g_disp.pics->tex.size(); ++k) {
      m.texRgba.push_back(g_disp.pics->tex[k]);
      m.texW.push_back(static_cast<uint32_t>(g_disp.pics->tw[k]));
      m.texH.push_back(static_cast<uint32_t>(g_disp.pics->th[k]));
    }
  }
  const std::vector<DispQuad>& Q = g_disp.quads;
  for (size_t q = 0; q < Q.size(); ++q) {
    for (int k = 0; k < 4; ++k) {
      m.uv.push_back(k % 2 == 0 ? 0.0f : 1.0f);
      m.uv.push_back(k < 2 ? 0.0f : 1.0f);
    }
    const uint32_t a = static_cast<uint32_t>(q * 4);
    m.indices.insert(m.indices.end(), {a, a + 2, a + 1, a + 1, a + 2, a + 3});
  }
  for (size_t q = 0; q < Q.size();) {  // (a part a run of the same picture and kind)
    size_t e = q;
    while (e < Q.size() && Q[e].tex == Q[q].tex && Q[e].soft == Q[q].soft) ++e;
    xrfx::ModelPart p;
    p.tex = Q[q].tex;
    p.soft = Q[q].soft;
    p.firstIndex = static_cast<uint32_t>(q * 6);
    p.indexCount = static_cast<uint32_t>((e - q) * 6);
    m.parts.push_back(p);
    q = e;
  }
  m.vertexCount = static_cast<uint32_t>(Q.size() * 4);
  g_disp.verts.assign(Q.size() * 24, 0.0f);
  xrfx::set_model(kPackSlot, std::move(m));
  g_disp.up = xrfx::model_up(kPackSlot);
  CLOG("slab shelf: its model up%s (%d slabs)", g_disp.up ? "" : " - failed", static_cast<int>(g_disp.slabs.size()));
  return g_disp.up;
}
void disp_drop(bool fxReady) {
  if (g_disp.up && fxReady && S.mode == kNone) xrfx::set_model(kPackSlot, xrfx::Model{});
  g_disp.up = false;
}
// a grip on it (hands(), no cards scene up): one hand moves it, both turn and size it; saved when let go
void disp_grab(const bool grip[2], const bool have[2], const XrVector3f hand[2]) {
  const XrVector3f centre = disp_w({0.0f, 0.1f, 0.0f});
  bool was = g_disp.hold[0] || g_disp.hold[1];
  for (int h = 0; h < 2; ++h) {
    const bool press = grip[h] && !g_disp.prevGrip[h];
    if (press && have[h] && g_disp.shown && S.mode == kNone && v_len(v_sub(hand[h], centre)) < 0.26f * g_disp.scale + 0.08f) {
      g_disp.hold[h] = true;
      buzz(0.25f, 0.04f);
    }
    if (!grip[h] || !have[h]) g_disp.hold[h] = false;
    g_disp.prevGrip[h] = grip[h];
  }
  const bool both = g_disp.hold[0] && g_disp.hold[1];
  static bool wasBoth = false;
  static int one = -1;
  if (both) {
    const XrVector3f mid = v_mul(v_add(hand[0], hand[1]), 0.5f), d = v_sub(hand[1], hand[0]);
    const float ang = std::atan2(d.z, d.x), dist = std::max(0.05f, std::sqrt(d.x * d.x + d.z * d.z));
    if (!wasBoth) {
      g_disp.mid0 = mid;
      g_disp.ang0 = ang;
      g_disp.dist0 = dist;
      g_disp.yaw0 = g_disp.yaw;
      g_disp.scale0 = g_disp.scale;
      g_disp.rel0 = v_sub(g_disp.pos, mid);
    }
    const float da = ang - g_disp.ang0;
    g_disp.scale = clampf(g_disp.scale0 * dist / g_disp.dist0, 0.4f, 3.0f);
    g_disp.yaw = g_disp.yaw0 - da;
    const float c = std::cos(-da), s = std::sin(-da), k = g_disp.scale / g_disp.scale0;
    const XrVector3f r = g_disp.rel0;
    g_disp.pos = v_add(mid, XrVector3f{(r.x * c + r.z * s) * k, r.y * k, (-r.x * s + r.z * c) * k});
    one = -1;
  } else {
    for (int h = 0; h < 2; ++h)
      if (g_disp.hold[h]) {
        if (one != h) g_disp.off = v_sub(g_disp.pos, hand[h]);
        one = h;
        g_disp.pos = v_add(hand[h], g_disp.off);
      }
    if (!g_disp.hold[0] && !g_disp.hold[1]) one = -1;
  }
  wasBoth = both;
  if (was && !g_disp.hold[0] && !g_disp.hold[1]) {
    g_disp.placed = true;
    disp_save();
  }
}
}  // namespace

void room_display(xrfx::Scene& scene, const XrVector3f& shelf, float shelfYaw, bool show, bool fxReady, uint64_t now) {
  if (g_dispStale) {  // r153: another career's binder loaded - its own shelf read, its slabs laid out again
    g_dispStale = false;
    g_disp.read = g_disp.placed = false;
    g_disp.sig.clear();
  }
  if (!g_disp.read) disp_read();
  if (!show || !fxReady || S.mode != kNone || !g_loaded.load()) {
    if (S.mode != kNone) g_disp.up = false;  // (the pack's slot the scene's now)
    else disp_drop(fxReady);
    g_disp.shown = false;
    return;
  }
  // what's on it (rebuilt when that changes)
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::vector<Slab> pick = disp_pick();
    std::string sig;
    for (const Slab& s : pick) sig += std::to_string(s.cert) + ",";
    if (sig != g_disp.sig || !g_disp.pics) {
      g_disp.sig = sig;
      g_disp.slabs = pick;
      g_disp.cards.clear();
      for (const Slab& s : pick) g_disp.cards.push_back(g_set[s.ref.card]);
      if (!pick.empty()) disp_prepare();
      else g_disp.pics.reset();
      if (g_disp.up && fxReady) xrfx::set_model(kPackSlot, xrfx::Model{});
      g_disp.up = false;
    }
  }
  if (g_disp.slabs.empty()) {
    g_disp.shown = false;
    return;
  }
  if (!g_disp.placed) {  // (first: beside the trophy shelf, past its board, turned as it is)
    g_disp.yaw = shelfYaw;
    const float c = std::cos(shelfYaw), s = std::sin(shelfYaw);
    g_disp.pos = {shelf.x + 1.3f * c, shelf.y, shelf.z - 1.3f * s};
    g_disp.scale = 1.0f;
  }
  const float t = static_cast<float>(static_cast<double>(now % 1000000000000ull) * 1e-9);
  if (!g_disp.up && !disp_model_up(fxReady, t)) {
    g_disp.shown = false;
    return;
  }
  disp_layout(t);
  const std::vector<DispQuad>& Q = g_disp.quads;
  if (g_disp.verts.size() != Q.size() * 24) return;
  for (size_t q = 0; q < Q.size(); ++q) {
    const DispQuad& d = Q[q];
    float r = ((d.rgb >> 16) & 255) / 255.0f * d.k, gg = ((d.rgb >> 8) & 255) / 255.0f * d.k, b = (d.rgb & 255) / 255.0f * d.k;
    if (d.shade) r = gg = b = -std::min(1.0f, d.k) - 1e-4f;
    for (int i = 0; i < 4; ++i) {
      float* o = g_disp.verts.data() + q * 24 + i * 6;
      o[0] = d.p[i].x;
      o[1] = d.p[i].y;
      o[2] = d.p[i].z;
      o[3] = r;
      o[4] = gg;
      o[5] = b;
    }
  }
  g_disp.shown = true;
  xrfx::Scene::ModelDraw& md = scene.models[kPackSlot];
  md.on = true;
  md.posNrm = g_disp.verts.data();
  md.local = false;
  md.white = 0.0f;
  md.alpha = 1.0f;
  md.glow = 0.0f;
  md.under = -4.5f;
  md.glowRgb = 0xFFFFFF;
  md.flameFrame = -1;
  md.shine = 0.0f;
  md.frame = -1;
  md.kaleido = 0u;
}

namespace {
// ===================================================================================== r160: the 3D binder in your room
// (Lance's call: "a REAL 3D binder you pick up", the skin its whole cover.) Its model: tools/outside/binders160 (Blender) ->
// franchise/binders/binder.mesh (cover panels, spine, rings, the pages' block, closed), each skin's three pictures made
// into one atlas (xr_fx_model.frag's binder mode); its two open pages drawn here like the hub's (the same pockets, sleeves,
// top-loaders), a page turning between them. Opening: the spine swings about its hinge with the back cover (90 deg), the
// front about its hinge with the spine (180 deg), flat: front | spine | back.
struct BMesh {
  bool tried = false;
  float W = 0.275f, H = 0.31f, T = 0.055f, ct = 0.0045f, px0 = 0.012f, px1 = 0.267f, py0 = -0.145f, py1 = 0.145f;
  float ringX = 0.028f, ringZ = 0.0f, ringR = 0.019f;
  struct Part {
    int id = 0;
    std::vector<float> v;  // 8 floats a vertex: position, normal, uv
    std::vector<uint32_t> idx;
  };
  std::vector<Part> parts;
};
BMesh g_bm;
enum { kBpBack = 0, kBpSpine, kBpFront, kBpRings, kBpBlock };
// (no binder.mesh on this headset: the same binder from boxes and rings, so it still works)
void bmesh_fallback() {
  BMesh& m = g_bm;
  m.parts.clear();
  auto box = [&](int id, float x0, float x1, float y0, float y1, float z0, float z1, const std::function<void(int, float, float, float, float*, float*)>& uv) {
    BMesh::Part p;
    p.id = id;
    static const int kF[6][4][3] = {{{0, 1, 0}, {1, 1, 0}, {1, 0, 0}, {0, 0, 0}},   // -Z (its back face)
                                    {{1, 1, 1}, {0, 1, 1}, {0, 0, 1}, {1, 0, 1}},   // +Z
                                    {{0, 1, 1}, {0, 1, 0}, {0, 0, 0}, {0, 0, 1}},   // -X
                                    {{1, 1, 0}, {1, 1, 1}, {1, 0, 1}, {1, 0, 0}},   // +X
                                    {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}},   // +Y
                                    {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}};  // -Y
    static const float kN[6][3] = {{0, 0, -1}, {0, 0, 1}, {-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int f = 0; f < 6; ++f) {
      const uint32_t base = static_cast<uint32_t>(p.v.size() / 8);
      for (int k = 0; k < 4; ++k) {
        const float x = kF[f][k][0] ? x1 : x0, y = kF[f][k][1] ? y1 : y0, z = kF[f][k][2] ? z1 : z0;
        float u = 0.0f, v = 0.0f;
        uv(f, x, y, z, &u, &v);
        p.v.insert(p.v.end(), {x, y, z, kN[f][0], kN[f][1], kN[f][2], u, v});
      }
      p.idx.insert(p.idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    m.parts.push_back(std::move(p));
  };
  const float W = m.W, H = m.H, T = m.T, ct = m.ct;
  auto vv = [&](float y) { return (0.5f * H - y) / H; };
  box(kBpBack, 0.0f, W, -0.5f * H, 0.5f * H, -0.5f * T, -0.5f * T + ct, [&](int f, float x, float y, float, float* u, float* v) {
    *u = f == 1 ? 0.54f + 0.46f * x / W : 0.46f * (1.0f - x / W);
    *v = vv(y);
  });
  box(kBpSpine, -ct, 0.0f, -0.5f * H, 0.5f * H, -0.5f * T, 0.5f * T, [&](int, float, float y, float z, float* u, float* v) {
    *u = 0.46f + 0.08f * (z + 0.5f * T) / T;
    *v = vv(y);
  });
  box(kBpFront, 0.0f, W, -0.5f * H, 0.5f * H, 0.5f * T - ct, 0.5f * T, [&](int f, float x, float y, float, float* u, float* v) {
    *u = f == 0 ? 0.46f * (1.0f - x / W) : 0.54f + 0.46f * x / W;
    *v = vv(y);
  });
  box(kBpBlock, 0.012f, W - 0.006f, -0.5f * H + 0.006f, 0.5f * H - 0.006f, -0.5f * T + ct, -0.5f * T + ct + 0.025f,
      [&](int, float x, float y, float, float* u, float* v) {
        *u = x / W;
        *v = vv(y);
      });
  BMesh::Part rings;  // three rings (tori) and their plate
  rings.id = kBpRings;
  for (int r = 0; r < 3; ++r) {
    const float yc = (static_cast<float>(r) - 1.0f) * 0.33f * H, tube = 0.0019f;
    constexpr int kA = 20, kB = 6;
    const uint32_t base = static_cast<uint32_t>(rings.v.size() / 8);
    for (int a = 0; a <= kA; ++a)
      for (int b = 0; b <= kB; ++b) {
        const float A = 6.2831853f * a / kA, B = 6.2831853f * b / kB;
        const float cx = std::cos(A), cz = std::sin(A), rr = m.ringR + tube * std::cos(B);
        const float nx = std::cos(B) * cx, ny = std::sin(B), nz = std::cos(B) * cz;
        rings.v.insert(rings.v.end(), {m.ringX + rr * cx, yc + tube * std::sin(B), m.ringZ + rr * cz, nx, ny, nz, 0.5f, 0.5f});
      }
    for (int a = 0; a < kA; ++a)
      for (int b = 0; b < kB; ++b) {
        const uint32_t i0 = base + static_cast<uint32_t>(a * (kB + 1) + b), i1 = i0 + kB + 1;
        rings.idx.insert(rings.idx.end(), {i0, i1, i0 + 1, i0 + 1, i1, i1 + 1});
      }
  }
  m.parts.push_back(std::move(rings));
}
void bmesh_load() {
  if (g_bm.tried) return;
  g_bm.tried = true;
  std::vector<uint8_t> d;
  if (FILE* f = std::fopen((home_dir() + "/franchise/binders/binder.mesh").c_str(), "rb")) {
    uint8_t buf[65536];
    size_t got;
    while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + got);
    std::fclose(f);
  }
  size_t at = 0;
  auto u32 = [&](uint32_t* o) {
    if (at + 4 > d.size()) return false;
    std::memcpy(o, d.data() + at, 4);
    at += 4;
    return true;
  };
  auto f32 = [&](float* o) {
    if (at + 4 > d.size()) return false;
    std::memcpy(o, d.data() + at, 4);
    at += 4;
    return true;
  };
  bool ok = d.size() > 64 && std::memcmp(d.data(), "BND1", 4) == 0;
  at = 4;
  uint32_t version = 0, nparts = 0;
  ok = ok && u32(&version) && version == 1;
  float hdr[11];
  for (float& h : hdr) ok = ok && f32(&h);
  ok = ok && u32(&nparts) && nparts > 0 && nparts < 64;
  std::vector<BMesh::Part> parts;
  for (uint32_t p = 0; ok && p < nparts; ++p) {
    uint32_t id = 0, nv = 0, ni = 0;
    ok = u32(&id) && u32(&nv) && u32(&ni) && nv < 200000 && ni < 600000 && at + (static_cast<size_t>(nv) * 8 + ni) * 4 <= d.size();
    if (!ok) break;
    BMesh::Part part;
    part.id = static_cast<int>(id);
    part.v.resize(static_cast<size_t>(nv) * 8);
    std::memcpy(part.v.data(), d.data() + at, part.v.size() * 4);
    at += part.v.size() * 4;
    part.idx.resize(ni);
    std::memcpy(part.idx.data(), d.data() + at, static_cast<size_t>(ni) * 4);
    at += static_cast<size_t>(ni) * 4;
    for (uint32_t& i : part.idx) ok = ok && i < nv;
    parts.push_back(std::move(part));
  }
  if (ok) {
    BMesh& m = g_bm;
    m.W = hdr[0], m.H = hdr[1], m.T = hdr[2], m.ct = hdr[3], m.px0 = hdr[4], m.px1 = hdr[5], m.py0 = hdr[6], m.py1 = hdr[7];
    m.ringX = hdr[8], m.ringZ = hdr[9], m.ringR = hdr[10];
    m.parts = std::move(parts);
    CLOG("binder: its model (%d parts, %.3f x %.3f x %.3f m)", static_cast<int>(m.parts.size()), m.W, m.H, m.T);
  } else {
    bmesh_fallback();
    CLOG("binder: no binder.mesh - its boxes and rings");
  }
}
// a skin's look beyond its pictures (franchise/binders/skins.txt): fur, its glow's colour, holo / sparkle / pulse
struct SkinLook {
  float furMm = 0.0f, emit[3] = {0.0f, 0.0f, 0.0f};
  bool holo = false, sparkle = false, pulse = false;
};
SkinLook skin_look(int i) {
  static std::mutex m;
  static bool read = false;
  static std::unordered_map<std::string, SkinLook> all;
  std::lock_guard<std::mutex> lock(m);
  if (!read) {
    read = true;
    if (FILE* f = std::fopen((home_dir() + "/franchise/binders/skins.txt").c_str(), "r")) {
      char line[256];
      while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (line[0] == '#') continue;
        char id[48] = {};
        SkinLook s;
        int h = 0, sp = 0, pu = 0;
        if (std::sscanf(line, "%47s %f %f %f %f %d %d %d", id, &s.furMm, &s.emit[0], &s.emit[1], &s.emit[2], &h, &sp, &pu) >= 5) {
          s.holo = h != 0;
          s.sparkle = sp != 0;
          s.pulse = pu != 0;
          all[id] = s;
        }
      }
      std::fclose(f);
    }
  }
  auto it = all.find(look_item(kLookSkin, i).id);
  return it != all.end() ? it->second : SkinLook{};
}
// a skin's atlas (1024 x 1024: rows 8..503 its colour + metal, rows 520..1015 its normal + roughness + glow / fur; their
// edge rows repeated into the gutters), made on a thread
struct Atlas {
  std::mutex m;
  bool done = false;
  int skin = -1;
  std::vector<uint8_t> px;
};
std::shared_ptr<Atlas> atlas_make(int skin) {
  auto a = std::make_shared<Atlas>();
  a->skin = skin;
  std::thread([a, skin]() {
    constexpr int S = 1024;
    std::vector<uint8_t> px(static_cast<size_t>(S) * S * 4, 0);
    std::vector<uint8_t> al, nm, mk;
    int aw = 0, ah = 0, nw = 0, nh = 0, mw = 0, mh = 0;
    const std::string base = home_dir() + "/franchise/binders/" + look_item(kLookSkin, skin).id;
    const bool haveA = load_rgba(base + "_albedo.jpg", &al, &aw, &ah) && aw > 4 && ah > 4;
    const bool haveN = load_rgba(base + "_normal.jpg", &nm, &nw, &nh) && nw > 4 && nh > 4;
    const bool haveM = load_rgba(base + "_mask.jpg", &mk, &mw, &mh) && mw > 4 && mh > 4;
    const LookItem& it = look_item(kLookSkin, skin);
    auto sample = [](const std::vector<uint8_t>& src, int w, int h, float u, float v, int ch) {
      const float fx = clampf(u * w - 0.5f, 0.0f, w - 1.0f), fy = clampf(v * h - 0.5f, 0.0f, h - 1.0f);
      const int x0 = static_cast<int>(fx), y0 = static_cast<int>(fy), x1 = std::min(w - 1, x0 + 1), y1 = std::min(h - 1, y0 + 1);
      const float tx = fx - x0, ty = fy - y0;
      auto at = [&](int x, int y) { return static_cast<float>(src[(static_cast<size_t>(y) * w + x) * 4 + ch]); };
      return (at(x0, y0) * (1 - tx) + at(x1, y0) * tx) * (1 - ty) + (at(x0, y1) * (1 - tx) + at(x1, y1) * tx) * ty;
    };
    for (int y = 0; y < S; ++y) {
      const bool top = y < 512;
      const int r0 = top ? 8 : 520;
      const float v = clampf((static_cast<float>(y - r0) + 0.5f) / 496.0f, 0.0f, 1.0f);  // (the gutters: its edge row again)
      for (int x = 0; x < S; ++x) {
        const float u = (x + 0.5f) / S;
        uint8_t* o = &px[(static_cast<size_t>(y) * S + x) * 4];
        if (top) {
          if (haveA) {
            for (int c = 0; c < 3; ++c) o[c] = static_cast<uint8_t>(sample(al, aw, ah, u, v, c) + 0.5f);
          } else {
            const uint32_t c = mix_rgb(it.rgb1, it.rgb2, 0.25f + 0.5f * v);
            o[0] = static_cast<uint8_t>(c >> 16), o[1] = static_cast<uint8_t>(c >> 8), o[2] = static_cast<uint8_t>(c);
          }
          o[3] = haveM ? static_cast<uint8_t>(sample(mk, mw, mh, u, v, 0) + 0.5f) : 0;
        } else {
          if (haveN) {
            for (int c = 0; c < 3; ++c) o[c] = static_cast<uint8_t>(sample(nm, nw, nh, u, v, c) + 0.5f);
          } else {
            o[0] = o[1] = 128;
            o[2] = 140;
          }
          o[3] = haveM ? static_cast<uint8_t>(sample(mk, mw, mh, u, v, 1) + 0.5f) : 0;
        }
      }
    }
    // (the sampler is sRGB: the lower half's RGB bytes go up as they are and the shader takes them back to these)
    std::lock_guard<std::mutex> lock(a->m);
    a->px = std::move(px);
    a->done = true;
  }).detach();
  return a;
}
// the binder's two open pages for a spread (as the hub's: the same pockets, the cards in your sleeve, hits top-loaded),
// 512 x 688 each, drawn on a thread
constexpr int kBPageW = 512, kBPageH = 688;
constexpr int kBakedOpen = 24;  // r163 quick fixes: a non-furry binder's open / shut poses, baked
struct BPages {
  std::mutex m;
  bool done = false;
  int spread = -1;
  std::vector<uint8_t> px[2];
};
std::shared_ptr<BPages> bpages_make(int spread) {
  auto p = std::make_shared<BPages>();
  p->spread = spread;
  std::vector<Card> cards;
  std::vector<Owned> owned;
  std::vector<bool> isNew;
  int total = 0, spreads = 1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::vector<int> list = binder_list();
    spreads = std::max(1, static_cast<int>((list.size() + kPerSpread - 1) / kPerSpread));
    total = static_cast<int>(list.size());
    for (int s = 0; s < kPerSpread; ++s) {
      const size_t at = static_cast<size_t>(spread) * kPerSpread + s;
      if (at >= list.size()) break;
      cards.push_back(g_set[list[at]]);
      owned.push_back(g_owned[list[at]]);
      isNew.push_back(g_newCards.count(g_set[list[at]].id) != 0);
    }
  }
  (void)total;
  const int outCard = g_bdOutCard.load();  // (r163: a card taken out of the 3D binder: its pocket empty meanwhile)
  std::thread([p, cards, owned, isNew, spread, spreads, outCard]() {
    for (int side = 0; side < 2; ++side) {
      Pc P;
      P.k = static_cast<float>(kBPageW) / kPageW;
      const float ox = side == 0 ? kLeftPageX : kRightPageX;
      P.ox = -ox * P.k;
      P.oy = -kBinderY * P.k;
      // a 9-pocket page: clear plastic over a white sheet, its pockets' welded seams, its three punched holes
      P.box(ox, kBinderY, ox + kPageW, kBinderY + kPageH, 6, 0xEEF1F5, 1.0f, 0xDDE3EA);
      for (int s = 0; s < 9; ++s) {
        float x0, y0, x1, y1;
        pocket_rect(side * 9 + s, &x0, &y0, &x1, &y1);
        P.box(x0 - 7, y0 - 7, x1 + 7, y1 + 7, 6, 0xC4CCD6);
        P.box(x0 - 3, y0 - 3, x1 + 3, y1 + 3, 5, 0xE4E9EF);
        const size_t at = static_cast<size_t>(side * 9 + s);
        if (at < cards.size()) {
          const ImgP pic = owned[at].shinyAny() ? card_art(cards[at], true, 0.6f) : portrait(cards[at].key);
          if (cards[at].num + 100000 * cards[at].set == outCard) {  // (r217: its number + its set: a Jam card's the same number)
            P.box(x0, y0, x1, y1, 10, 0xD4DAE2);  // (r163: out in your hand)
          } else {
            mini_card(P, cards[at], owned[at], x0, y0, x1, y1, pic, false, isNew[at]);  // (r163: no sleeves)
          }
        } else {
          P.box(x0, y0, x1, y1, 10, 0xD4DAE2);
        }
      }
      const float hx = side == 0 ? ox + kPageW - 12 : ox + 12;  // (its holes on the rings' side)
      for (int r = 0; r < 3; ++r) {
        const float hy = kBinderY + (0.17f + 0.33f * r) * kPageH;
        P.box(hx - 7, hy - 7, hx + 7, hy + 7, 7, 0x8A94A4);
      }
      P.poly({ox + 4, kBinderY + 0.30f * kPageH, ox + kPageW - 4, kBinderY + 0.10f * kPageH, ox + kPageW - 4, kBinderY + 0.18f * kPageH,
              ox + 4, kBinderY + 0.38f * kPageH},
             0xFFFFFF, 0.10f);  // (the plastic's gloss)
      P.text("PAGE " + std::to_string(spread * 2 + side + 1) + " OF " + std::to_string(spreads * 2), ox + 0.5f * kPageW,
             kBinderY + kPageH - 9, 13, 200, 0x7A8496, 0);
      std::vector<uint8_t> px = board_px(kBPageW, kBPageH, P);
      for (size_t q = 3; q < px.size(); q += 4) px[q] = 255;
      std::lock_guard<std::mutex> lock(p->m);
      p->px[side] = std::move(px);
    }
    std::lock_guard<std::mutex> lock(p->m);
    p->done = true;
  }).detach();
  return p;
}

struct Binder3 {
  bool read = false, out = false;
  XrVector3f pos{0.0f, 0.0f, 0.0f};
  XrQuaternionf q{0.0f, 0.0f, 0.0f, 1.0f};
  float scale = 1.0f, open = 0.0f, openTo = 0.0f, openFrom = 0.0f;
  uint64_t openNs = 0, flipNs = 0, grabNs = 0;  // (r160 round 2: its cover / page turning since (xr_pageturn.h), held since)
  bool prevTrig[2] = {false, false};
  int outSlot = -1;  // (r163: the pocket a card of it is out of)
  xrturn::Sweep sweep;  // (the free hand's sweep across it, as the magazine's)
  // its grab (as the slab shelf's; both hands: turned by how the line between them turned, sized by its length)
  bool hold[2] = {false, false}, prevGrip[2] = {false, false}, wasBoth = false;
  int one = -1;
  XrVector3f off{0.0f, 0.0f, 0.0f}, mid0{0.0f, 0.0f, 0.0f}, rel0{0.0f, 0.0f, 0.0f}, d0{1.0f, 0.0f, 0.0f};
  XrQuaternionf q0{0.0f, 0.0f, 0.0f, 1.0f};
  float scale0 = 1.0f;
  // its model: the skin in it, its atlas; the four page pictures (q: which spread / side each holds), the spread shown
  // (its left / right in which quads), a page turning (to which spread, its way, how far), the pictures asked for
  int skin = -1, built = -1;
  std::shared_ptr<Atlas> atlas;
  std::shared_ptr<BPages> pages, want;
  int shown = -1, qL = 0, qR = 1, flipTo = -1, flipDir = 0;
  float flipT = 0.0f;
  int pagesGen = -1;
  std::vector<uint8_t> pic[4];
  bool up = false, needUp = false, pagesUp = false, pagesNeedUp = false;  // (r163: the pages' own model)
  std::vector<float> rest;  // (r163: this frame's vertices before xr_binderfx moved them)
  bool fur = false;         // (r163 quick fixes: its vertices go up every frame only when furry)
  std::vector<int> shellSrc;
  std::vector<float> shellH;
  XrVector3f lastPos{0.0f, 0.0f, 0.0f};
  std::vector<float> base;      // its vertices closed (6 floats: position, normal), at size 1
  std::vector<int> group;       // each vertex's part (kBp*; 5 + q: page quad q)
  size_t pageBase = 0;          // (the first page quad's first vertex)
  std::vector<float> verts;     // as drawn (its own frame)
  SkinLook look;
  bool previewing = false;
  uint64_t lastNs = 0;
};
Binder3 g_bd;
std::string bd_path() { return home_dir() + "/cards/binder_room.txt"; }
void bd_save() {
  ::mkdir((home_dir() + "/cards").c_str(), 0775);
  if (FILE* f = std::fopen(bd_path().c_str(), "w")) {
    std::fprintf(f, "# r160: your 3D binder in the room: out, x y z (LOCAL m) qx qy qz qw, size, open\nout %d\nplaced %.4f %.4f %.4f %.5f %.5f %.5f %.5f %.3f %d\n",
                 g_bd.out ? 1 : 0, g_bd.pos.x, g_bd.pos.y, g_bd.pos.z, g_bd.q.x, g_bd.q.y, g_bd.q.z, g_bd.q.w, g_bd.scale,
                 g_bd.openTo > 0.5f ? 1 : 0);
    std::fclose(f);
  }
}
void bd_read() {
  g_bd.read = true;
  if (FILE* f = std::fopen(bd_path().c_str(), "r")) {
    char line[256];
    int out = 0, open = 0;
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      if (std::sscanf(line, "out %d", &out) == 1) g_bd.out = out != 0;
      XrQuaternionf q{};
      XrVector3f p{};
      float s = 1.0f;
      if (std::sscanf(line, "placed %f %f %f %f %f %f %f %f %d", &p.x, &p.y, &p.z, &q.x, &q.y, &q.z, &q.w, &s, &open) >= 8) {
        g_bd.pos = p;
        g_bd.q = q_unit(q);
        g_bd.scale = clampf(s, 0.4f, 3.0f);
        g_bd.open = g_bd.openTo = g_bd.openFrom = open != 0 ? 1.0f : 0.0f;
      }
    }
    std::fclose(f);
  }
  g_binderOut = g_bd.out;
}
// in front of your eyes: a little below and to the right (clear of the hub), turned to you, upright
void bd_place(const XrPosef& head, XrVector3f* pos, XrQuaternionf* q, float ahead) {
  XrVector3f f = q_rot(head.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
  f.y = 0.0f;
  const float len = std::sqrt(f.x * f.x + f.z * f.z);
  f = len > 1e-3f ? XrVector3f{f.x / len, 0.0f, f.z / len} : XrVector3f{0.0f, 0.0f, -1.0f};
  const XrVector3f r{-f.z, 0.0f, f.x};
  const float yaw = std::atan2(-f.x, -f.z);  // (its +Z, the front cover, toward you)
  *q = q_axis(0.0f, 1.0f, 0.0f, yaw);
  *pos = v_add(v_add(head.position, v_mul(f, ahead)), v_add(v_mul(r, 0.18f), XrVector3f{0.0f, -0.22f, 0.0f}));
}
XrVector3f bd_world(const XrVector3f& l) { return v_add(g_bd.pos, q_rot(g_bd.q, v_mul(l, g_bd.scale))); }
XrVector3f bd_local(const XrVector3f& w) { return v_mul(q_rot(q_conj(g_bd.q), v_sub(w, g_bd.pos)), 1.0f / std::max(0.05f, g_bd.scale)); }
// a world point in reach of it (`margin` m round its box, as it's open now)
bool bd_near(const XrVector3f& w, float margin) {
  const BMesh& m = g_bm;
  const XrVector3f l = bd_local(w);
  const float k = margin / std::max(0.05f, g_bd.scale);
  const bool open = g_bd.open > 0.5f;
  const float x0 = open ? -m.T - m.W : -0.02f, x1 = m.W, z0 = open ? -0.5f * m.T - 0.02f : -0.5f * m.T, z1 = open ? -0.5f * m.T + 0.06f : 0.5f * m.T;
  return l.x > x0 - k && l.x < x1 + k && std::fabs(l.y) < 0.5f * m.H + k && l.z > z0 - k && l.z < z1 + k;
}
// the page quads' place in the binder's frame: its right page lying on the back cover, its left on the front's inside
// (as the front is turned now), the turning sheet (`phi` 0 lying right .. pi lying left); corners top-left, top-right,
// bottom-right, bottom-left as you see the picture
void bd_page_rect(float* x0, float* x1, float* y0, float* y1) {
  const BMesh& m = g_bm;
  float w = m.px1 - m.px0, h = m.py1 - m.py0;
  const float want = static_cast<float>(kBPageW) / kBPageH;  // (the pictures' shape)
  if (w / h > want) w = h * want;
  else h = w / want;
  *x0 = std::max(m.px0 + 0.004f, m.ringX - 0.0055f);  // (its punched holes on the rings)
  if (*x0 + w > m.px1) *x0 = std::max(m.px0, m.px1 - w);
  *x1 = *x0 + w;
  *y0 = 0.5f * (m.py0 + m.py1) - 0.5f * h;
  *y1 = *y0 + h;
}
void bd_rotXZ(XrVector3f* p, float cx, float cz, float a) {
  const float dx = p->x - cx, dz = p->z - cz, c = std::cos(a), s = std::sin(a);
  p->x = cx + dx * c - dz * s;
  p->z = cz + dx * s + dz * c;
}
void bd_nrmXZ(XrVector3f* n, float a) { bd_rotXZ(n, 0.0f, 0.0f, a); }
// a closed-frame point of the spine / front, as opened `open` (0..1)
void bd_open_pt(int grp, float open, XrVector3f* p, XrVector3f* n) {
  const BMesh& m = g_bm;
  const float ts = open * 1.5707963f, tf = open * 3.14159265f;
  if (grp == kBpSpine) {
    bd_rotXZ(p, 0.0f, -0.5f * m.T, ts);
    bd_nrmXZ(n, ts);
  } else if (grp == kBpFront) {
    bd_rotXZ(p, 0.0f, 0.5f * m.T, tf);
    bd_nrmXZ(n, tf);
    // its hinge rides on the spine's top edge
    p->x += -m.T * std::sin(ts);
    p->z += -0.5f * m.T + m.T * std::cos(ts) - 0.5f * m.T;
  }
}
void bd_layout() {
  Binder3& B = g_bd;
  const BMesh& m = g_bm;
  const size_t nv = B.group.size();
  if (B.verts.size() != nv * 6) B.verts.assign(nv * 6, 0.0f);
  float px0, px1, py0, py1;
  bd_page_rect(&px0, &px1, &py0, &py1);
  const float zr = -0.5f * m.T + m.ct + 0.0012f, zs = zr + 0.0022f, xm = -0.5f * m.T;
  // each page quad's role: 0 hidden, 1 the left page, 2 the right, 3 the sheet's front, 4 its back
  int role[4] = {0, 0, 0, 0};
  float phi = 0.0f;
  if (B.flipTo >= 0 && B.flipDir != 0) {
    const int nl = (B.qL + 2) % 4, nr = (B.qR + 2) % 4;  // (the new spread's pictures: the two free quads)
    if (B.flipDir > 0) {
      role[B.qL] = 1, role[nr] = 2, role[B.qR] = 3, role[nl] = 4;
      phi = smooth(B.flipT) * 3.14159265f;
    } else {
      role[nl] = 1, role[B.qR] = 2, role[nr] = 3, role[B.qL] = 4;
      phi = (1.0f - smooth(B.flipT)) * 3.14159265f;
    }
  } else {
    role[B.qL] = 1;
    role[B.qR] = 2;
  }
  const bool showPages = B.open > 0.01f;
  for (size_t i = 0; i < nv; ++i) {
    const int g = B.group[i];
    XrVector3f p{B.base[i * 6], B.base[i * 6 + 1], B.base[i * 6 + 2]}, n{B.base[i * 6 + 3], B.base[i * 6 + 4], B.base[i * 6 + 5]};
    if (g == kBpSpine || g == kBpFront) bd_open_pt(g, B.open, &p, &n);
    if (g == kBpBlock && B.open > 0.03f) p = n = XrVector3f{0.0f, 0.0f, 0.0f};
    if (g >= 5) {
      const int qd = g - 5, c = static_cast<int>((i - B.pageBase) % 4);  // (its corner: TL, TR, BR, BL)
      const int r = showPages ? role[qd] : 0;
      const bool topC = c < 2, leftC = c == 0 || c == 3;
      if (r == 1) {  // the left page, on the front's inside (closed frame: its outer edge at px1)
        p = XrVector3f{leftC ? px1 : px0, topC ? py1 : py0, 0.5f * m.T - m.ct - 0.0012f};
        n = XrVector3f{0.0f, 0.0f, -2.0f};
        bd_open_pt(kBpFront, B.open, &p, &n);
      } else if (r == 2) {
        p = XrVector3f{leftC ? px0 : px1, topC ? py1 : py0, zr};
        n = XrVector3f{0.0f, 0.0f, 2.0f};
      } else if (r == 3 || r == 4) {
        const bool front = r == 3;
        p = XrVector3f{front ? (leftC ? px0 : px1) : (leftC ? px1 : px0), topC ? py1 : py0, front ? zs : zs - 0.0004f};
        n = XrVector3f{0.0f, 0.0f, front ? 2.0f : -2.0f};
        // (a page bows a little as it turns)
        p.z += 0.02f * std::sin(phi) * ((p.x - px0) / std::max(0.01f, px1 - px0)) * (1.0f - (p.x - px0) / std::max(0.01f, px1 - px0)) * 4.0f;
        bd_rotXZ(&p, xm, zs, phi);
        bd_nrmXZ(&n, phi);
      } else {
        p = n = XrVector3f{0.0f, 0.0f, 0.0f};
      }
    }
    float* o = B.verts.data() + i * 6;
    o[0] = p.x, o[1] = p.y, o[2] = p.z, o[3] = n.x, o[4] = n.y, o[5] = n.z;
  }
}
// its model up: the atlas, the four page pictures, chrome, paper; its parts (fur shells on a furry skin)
bool bd_model_up(bool fxReady) {
  Binder3& B = g_bd;
  if (!fxReady || !B.atlas) return false;
  {
    std::lock_guard<std::mutex> lock(B.atlas->m);
    if (!B.atlas->done) return false;
  }
  bmesh_load();
  const BMesh& bm = g_bm;
  B.look = skin_look(B.atlas->skin);
  xrfx::Model m;
  {
    std::lock_guard<std::mutex> lock(B.atlas->m);
    m.texRgba.push_back(B.atlas->px);
  }
  m.texW.push_back(1024);
  m.texH.push_back(1024);
  // (r163: its pages are their own model in kBinderPagesSlot - bd_pages_up: a page turn no longer builds this one again)
  m.texRgba.push_back(std::vector<uint8_t>(8 * 8 * 4, 255));  // 1 chrome (r163: was 5)
  m.texW.push_back(8);
  m.texH.push_back(8);
  {  // 2 the pages' block: page edges, cream with fine lines (r163: was 6)
    std::vector<uint8_t> px(64 * 64 * 4);
    for (int y = 0; y < 64; ++y)
      for (int x = 0; x < 64; ++x) {
        const uint8_t l = static_cast<uint8_t>(232 - ((x % 4) == 0 ? 26 : 0));
        uint8_t* o = &px[(static_cast<size_t>(y) * 64 + x) * 4];
        o[0] = l, o[1] = static_cast<uint8_t>(l - 4), o[2] = static_cast<uint8_t>(l - 14), o[3] = 255;
      }
    m.texRgba.push_back(std::move(px));
    m.texW.push_back(64);
    m.texH.push_back(64);
  }
  B.base.clear();
  B.group.clear();
  auto part_of = [&](int tex, const std::vector<uint32_t>& idx, uint32_t first) {
    xrfx::ModelPart p;
    p.tex = tex;
    p.firstIndex = static_cast<uint32_t>(m.indices.size());
    for (uint32_t i : idx) m.indices.push_back(first + i);
    p.indexCount = static_cast<uint32_t>(idx.size());
    m.parts.push_back(p);
  };
  auto add_part = [&](const BMesh::Part& src, int tex, float nlen, int grp) {
    const uint32_t first = static_cast<uint32_t>(B.group.size());
    for (size_t v = 0; v + 7 < src.v.size(); v += 8) {
      B.base.insert(B.base.end(), {src.v[v], src.v[v + 1], src.v[v + 2], src.v[v + 3] * nlen, src.v[v + 4] * nlen, src.v[v + 5] * nlen});
      m.uv.push_back(src.v[v + 6]);
      m.uv.push_back(src.v[v + 7]);
      B.group.push_back(grp);
    }
    part_of(tex, src.idx, first);
  };
  int partFirst[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
  for (const BMesh::Part& p : bm.parts) {
    if (p.id >= 0 && p.id < 8 && partFirst[p.id] < 0) partFirst[p.id] = static_cast<int>(B.group.size());
    if (p.id <= kBpFront) add_part(p, 0, 1.0f, p.id);
    else if (p.id == kBpRings) add_part(p, 1, 3.0f, kBpBack);
    else if (p.id == kBpBlock) add_part(p, 2, 2.0f, kBpBlock);
  }
  B.shellSrc.assign(B.group.size(), -1);
  B.shellH.assign(B.group.size(), -1.0f);
  // fur: shells over its outside (back -Z, spine -X, front +Z), each a little further out, kept where a strand reaches.
  // r163 quick fixes: a shell holds only its part's OUTWARD vertices (those its outward triangles use), in their order -
  // shell s of part p a run of the same length for every s; shellSrc / shellH say where each came from (xr_binderfx.cpp)
  const float fur = clampf(B.look.furMm, 0.0f, 25.0f) * 0.001f;
  B.fur = fur > 0.0f;
  if (fur > 0.0f) {
    // (r160 round 2: 16 shells; r163 quick fixes: by its length, each layer under ~1 mm apart - shag 16, yeti / pink 12,
    // teddy / zebra / leopard 8)
    const int kShells = B.look.furMm >= 13.0f ? 16 : (B.look.furMm >= 9.0f ? 12 : 8);
    for (const BMesh::Part& p : bm.parts) {
      if (p.id > kBpFront || partFirst[p.id] < 0) continue;
      std::vector<uint32_t> keep;
      for (size_t t = 0; t + 2 < p.idx.size(); t += 3) {
        bool out = true;
        for (int k = 0; k < 3; ++k) {
          const float* v = &p.v[static_cast<size_t>(p.idx[t + k]) * 8];
          out = out && (p.id == kBpBack ? v[5] < -0.7f : (p.id == kBpFront ? v[5] > 0.7f : v[3] < -0.7f));
        }
        if (out) keep.insert(keep.end(), {p.idx[t], p.idx[t + 1], p.idx[t + 2]});
      }
      if (keep.empty()) continue;
      std::vector<int> local(p.v.size() / 8, -1);  // (a part vertex -> its place in the shell's run)
      std::vector<uint32_t> used;
      for (uint32_t i : keep)
        if (local[i] < 0) {
          local[i] = static_cast<int>(used.size());
          used.push_back(i);
        }
      std::sort(used.begin(), used.end());  // (in their original order)
      for (size_t k = 0; k < used.size(); ++k) local[used[k]] = static_cast<int>(k);
      std::vector<uint32_t> keepL;
      keepL.reserve(keep.size());
      for (uint32_t i : keep) keepL.push_back(static_cast<uint32_t>(local[i]));
      for (int s = 1; s <= kShells; ++s) {
        const float h = static_cast<float>(s) / kShells;
        const uint32_t first = static_cast<uint32_t>(B.group.size());
        for (uint32_t vi : used) {
          const size_t v = static_cast<size_t>(vi) * 8;
          const float nx = p.v[v + 3], ny = p.v[v + 4], nz = p.v[v + 5], k = 4.0f + 0.98f * h;
          B.base.insert(B.base.end(), {p.v[v] + nx * fur * h, p.v[v + 1] + ny * fur * h, p.v[v + 2] + nz * fur * h, nx * k, ny * k, nz * k});
          m.uv.push_back(p.v[v + 6]);
          m.uv.push_back(p.v[v + 7]);
          B.group.push_back(p.id);
          B.shellSrc.push_back(partFirst[p.id] + static_cast<int>(vi));
          B.shellH.push_back(h);
        }
        part_of(0, keepL, first);
      }
    }
  }
  B.pageBase = B.group.size();
  for (int q = 0; q < 4; ++q) {  // the page quads (each its own picture) - laid out with the rest, drawn by bd_pages_up's model
    for (int c = 0; c < 4; ++c) {
      B.base.insert(B.base.end(), {0, 0, 0, 0, 0, 2});
      B.group.push_back(5 + q);
      B.shellSrc.push_back(-1);
      B.shellH.push_back(-1.0f);
    }
  }
  m.vertexCount = static_cast<uint32_t>(B.pageBase);
  // r163 quick fixes (per-frame upload): a skin with no fur has nothing moving on the CPU - its open / shut poses baked
  // (kBakedOpen frames, open 0..1; room_binder picks the nearest) and nothing uploaded a frame; a furry one's vertices
  // (its shells sway / part) still go up every frame
  if (!B.fur) {
    const float keepOpen = B.open;
    m.frames.reserve(static_cast<size_t>(kBakedOpen) * B.pageBase * 6);
    for (int k = 0; k < kBakedOpen; ++k) {
      B.open = static_cast<float>(k) / (kBakedOpen - 1);
      bd_layout();
      m.frames.insert(m.frames.end(), B.verts.begin(), B.verts.begin() + static_cast<std::ptrdiff_t>(B.pageBase * 6));
    }
    m.frameCount = kBakedOpen;
    B.open = keepOpen;
  }
  xrfx::set_model(kBinderSlot, std::move(m));
  B.up = xrfx::model_up(kBinderSlot);
  B.pagesNeedUp = true;
  B.built = B.atlas->skin;
  B.needUp = false;
  bd_layout();
  CLOG("binder: its model up%s (%s, %d vertices, %s)", B.up ? "" : " - failed", look_item(kLookSkin, B.built).id,
       static_cast<int>(B.pageBase), B.fur ? "fur: its vertices go up every frame" : "its poses baked: nothing goes up a frame");
  return B.up;
}
// r163: its four page pictures as their own model (kBinderPagesSlot, the same frame): only these go up again when a page
// turns or a card leaves / comes back to its pocket (r162 built the whole binder again each time: "its model up" x 27)
bool bd_pages_up(bool fxReady) {
  Binder3& B = g_bd;
  if (!fxReady) return false;
  xrfx::Model m;
  static const float kUv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
  for (int q = 0; q < 4; ++q) {
    if (B.pic[q].size() == static_cast<size_t>(kBPageW) * kBPageH * 4) {
      m.texRgba.push_back(B.pic[q]);
      m.texW.push_back(kBPageW);
      m.texH.push_back(kBPageH);
    } else {
      m.texRgba.push_back(std::vector<uint8_t>(8 * 8 * 4, 236));
      m.texW.push_back(8);
      m.texH.push_back(8);
    }
    for (int c = 0; c < 4; ++c) {
      m.uv.push_back(kUv[c][0]);
      m.uv.push_back(kUv[c][1]);
    }
    xrfx::ModelPart p;
    p.tex = q;
    p.firstIndex = static_cast<uint32_t>(m.indices.size());
    const uint32_t a = static_cast<uint32_t>(q * 4);
    m.indices.insert(m.indices.end(), {a, a + 1, a + 2, a, a + 2, a + 3});
    p.indexCount = 6;
    m.parts.push_back(p);
  }
  m.vertexCount = 16;
  xrfx::set_model(kBinderPagesSlot, std::move(m));
  B.pagesUp = xrfx::model_up(kBinderPagesSlot);
  B.pagesNeedUp = false;
  return B.pagesUp;
}
void bd_drop(bool fxReady) {
  if (g_bd.up && fxReady) xrfx::set_model(kBinderSlot, xrfx::Model{});
  if (g_bd.pagesUp && fxReady) xrfx::set_model(kBinderPagesSlot, xrfx::Model{});
  g_bd.up = g_bd.pagesUp = false;
  xrbfx::stop(fxReady);  // r163: its living pieces
}
// a grip on it: one hand moves it (only moves it), both turn it (as the line between them turns) and size it; saved
// when let go
void bd_grab(const bool grip[2], const bool have[2], const XrVector3f hand[2], bool shown) {
  Binder3& B = g_bd;
  const bool was = B.hold[0] || B.hold[1];
  for (int h = 0; h < 2; ++h) {
    const bool press = grip[h] && !B.prevGrip[h];
    if (press && have[h] && shown && !B.previewing && !g_disp.hold[h] && S.hold[h] < 0 && bd_near(hand[h], 0.06f)) {
      B.hold[h] = true;
      buzz(0.25f, 0.04f);
    }
    if (!grip[h] || !have[h]) B.hold[h] = false;
    B.prevGrip[h] = grip[h];
  }
  const bool both = B.hold[0] && B.hold[1];
  if (both) {
    const XrVector3f mid = v_mul(v_add(hand[0], hand[1]), 0.5f), d = v_sub(hand[1], hand[0]);
    const float dl = std::max(0.05f, v_len(d));
    if (!B.wasBoth) {
      B.mid0 = mid;
      B.d0 = v_mul(d, 1.0f / dl);
      B.q0 = B.q;
      B.scale0 = B.scale;
      B.rel0 = v_sub(B.pos, mid);
      B.off = XrVector3f{dl, 0.0f, 0.0f};  // (the hands' distance then: off.x)
    }
    const XrVector3f d1 = v_mul(d, 1.0f / dl);
    // the turn taking the hands' first line to this one (the shortest way)
    XrVector3f ax = v_cross(B.d0, d1);
    const float c = clampf(B.d0.x * d1.x + B.d0.y * d1.y + B.d0.z * d1.z, -1.0f, 1.0f), sl = v_len(ax);
    XrQuaternionf turn{0.0f, 0.0f, 0.0f, 1.0f};
    if (sl > 1e-5f) turn = q_axis(ax.x / sl, ax.y / sl, ax.z / sl, std::atan2(sl, c));
    B.scale = clampf(B.scale0 * dl / std::max(0.05f, B.off.x), 0.4f, 3.0f);
    B.q = q_unit(q_mul(turn, B.q0));
    B.pos = v_add(mid, v_mul(q_rot(turn, B.rel0), B.scale / std::max(0.05f, B.scale0)));
    B.one = -1;
  } else {
    for (int h = 0; h < 2; ++h)
      if (B.hold[h]) {
        if (B.one != h) B.off = v_sub(B.pos, hand[h]);
        B.one = h;
        B.pos = v_add(hand[h], B.off);
      }
    if (!B.hold[0] && !B.hold[1]) B.one = -1;
  }
  B.wasBoth = both;
  const bool heldNow = B.hold[0] || B.hold[1];
  if (heldNow && !was) {  // (r160 round 2: held - its triggers count 0.9 s on, as the magazine's after it opens; its hint bar)
    B.grabNs = mono_ns();
    B.sweep.reset();
    ++g_gen;
  }
  g_binderHeld = heldNow;
  if (was && !heldNow) {
    bd_save();
    ++g_gen;
  }
}
// r160 round 2 (Lance: it flips EXACTLY like STRIKERS ILLUSTRATED): a page / its cover turned as the magazine's
// (xr_pageturn.h, mag_flip): +1 on (shut: it opens, its cover over the spine), -1 back (on its first pages: it shuts), 2
// A (the next; on its last pages it shuts, as A on the magazine's back cover puts it down), -2 B (shut). A page still going
// over is finished first, as mag_flip settles one. False: nothing that way.
bool bd_turn(int how) {
  Binder3& B = g_bd;
  if (!B.out || B.previewing) return false;
  const uint64_t now = mono_ns();
  auto cover = [&](float to) {
    B.openFrom = B.open;
    B.openTo = to;
    B.openNs = now;
    buzz(0.3f, 0.05f);
    bd_save();
  };
  if (how == -2) {
    if (B.openTo > 0.5f) cover(0.0f);
    return true;
  }
  if (B.openTo < 0.5f) {
    if (how > 0) cover(1.0f);
    return how > 0;
  }
  if (B.open < 0.98f) return true;  // (its cover still going over)
  if (B.flipTo >= 0) B.flipNs = 0;  // (the page going over lands now: room_binder, this frame)
  std::lock_guard<std::mutex> lock(g_mutex);
  const int spreads = std::max(1, static_cast<int>((binder_list().size() + kPerSpread - 1) / kPerSpread));
  if (how > 0) {
    if (g_spread + 1 < spreads) {
      ++g_spread;
      ++g_gen;
      buzz(0.2f, 0.04f);
    } else if (how == 2) {
      cover(0.0f);
    } else {
      return false;
    }
  } else if (g_spread > 0) {
    --g_spread;
    ++g_gen;
    buzz(0.2f, 0.04f);
  } else {
    cover(0.0f);
  }
  return true;
}
// r163: pocket `slot` (0-17 of the spread shown) on the open binder: its middle (LOCAL), a breath over its page
bool bd_pocket_world(int slot, XrVector3f* out) {
  if (slot < 0 || slot >= kPerSpread) return false;
  const BMesh& m = g_bm;
  float px0, px1, py0, py1, x0, y0, x1, y1;
  bd_page_rect(&px0, &px1, &py0, &py1);
  pocket_rect(slot, &x0, &y0, &x1, &y1);
  const float ox = slot < 9 ? kLeftPageX : kRightPageX;
  const float u = (0.5f * (x0 + x1) - ox) / kPageW, v = (0.5f * (y0 + y1) - kBinderY) / kPageH;  // (its picture: across, down)
  const float y = py1 - v * (py1 - py0);
  XrVector3f p, n{0.0f, 0.0f, 1.0f};
  if (slot >= 9) {
    p = XrVector3f{px0 + u * (px1 - px0), y, -0.5f * m.T + m.ct + 0.005f};
  } else {  // (the left page rides the front cover: its picture's left edge at px1, closed)
    p = XrVector3f{px1 - u * (px1 - px0), y, 0.5f * m.T - m.ct - 0.005f};
    bd_open_pt(kBpFront, g_bd.open, &p, &n);
  }
  *out = bd_world(p);
  return true;
}
// r163: the pocket hand h's pointer is on (the open binder's spread shown; -1 none): its ray met with the pages' plane
int bd_pocket_at(int h) {
  Binder3& B = g_bd;
  if (!B.out || B.previewing || B.open < 0.98f || B.flipTo >= 0 || !S.aimOk[h]) return -1;
  const BMesh& m = g_bm;
  const XrVector3f o = bd_local(S.aimAt[h].position);
  const XrVector3f d = q_rot(q_conj(B.q), q_rot(S.aimAt[h].orientation, XrVector3f{0.0f, 0.0f, -1.0f}));
  const float zp = -0.5f * m.T + m.ct + 0.0012f;
  if (std::fabs(d.z) < 1e-3f) return -1;
  const float t = (zp - o.z) / d.z;
  if (t < -0.06f / std::max(0.05f, B.scale) || t > 1.6f / std::max(0.05f, B.scale)) return -1;
  const float lx = o.x + d.x * t, ly = o.y + d.y * t;
  float px0, px1, py0, py1;
  bd_page_rect(&px0, &px1, &py0, &py1);
  const float v = (py1 - ly) / (py1 - py0);
  if (v < 0.0f || v > 1.0f) return -1;
  int side = -1;
  float u = 0.0f;
  if (lx >= px0 && lx <= px1) {
    side = 1;
    u = (lx - px0) / (px1 - px0);
  } else {
    const float xc = -m.T - lx;  // (the front's closed frame)
    if (xc >= px0 && xc <= px1) {
      side = 0;
      u = (px1 - xc) / (px1 - px0);
    }
  }
  if (side < 0) return -1;
  const float X = (side == 0 ? kLeftPageX : kRightPageX) + u * kPageW, Y = kBinderY + v * kPageH;
  for (int s = 0; s < 9; ++s) {
    float x0, y0, x1, y1;
    pocket_rect(side * 9 + s, &x0, &y0, &x1, &y1);
    if (X >= x0 - 6 && X <= x1 + 6 && Y >= y0 - 6 && Y <= y1 + 6) return side * 9 + s;
  }
  return -1;
}
// r163 (Lance: cards out of the 3D binder): a card pulled out of its pocket - a grip: into that hand (gliding out of its
// pocket, as a card out of the hub's), a trigger: up out of its pocket to in front of you; then the usual card in your
// hands (turn it over, tilt it, its 3D). Its pocket empty meanwhile; B / let go on its pocket puts it back in. True: taken.
bool bd_take(int h, int slot, bool grip, const XrVector3f& hand, uint64_t now) {
  int card = -1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::vector<int> list = binder_list();
    const size_t at = static_cast<size_t>(g_spread) * kPerSpread + slot;
    if (at < list.size() && owns(list[at])) card = list[at];
    if (card >= 0) g_cursor = slot;
  }
  if (card < 0) return false;
  XrVector3f pocket;
  if (!bd_pocket_world(slot, &pocket)) return false;
  look_card(card, now, &pocket);
  g_bdOutCard = g_set[card].num + 100000 * g_set[card].set;  // (r217)
  g_bd.outSlot = slot;
  g_bd.pagesGen = -1;  // (its pocket drawn empty)
  Sheet& s = S.card[0];
  if (grip && S.aimOk[h]) {
    const XrVector3f tip = v_add(S.aimAt[h].position, q_rot(S.aimAt[h].orientation, XrVector3f{0.0f, 0.03f, -0.11f}));
    s.free = true;
    s.hands = 1 << h;
    S.hold[h] = 1;
    S.spent[h] = true;
    sheet_take(s, h, hand);
    s.relPTo = q_rot(q_conj(S.handQ[h]), v_sub(tip, hand));
    s.offFade = 0.4f;
    S.prevGrip[h] = true;
  }
  buzz(0.3f, 0.05f);
  CLOG("binder: card %d out of its pocket (%s)", g_set[card].num, grip ? "grip" : "trigger");
  return true;
}
// the free hand's sweep across it while the other holds it (xr_pageturn.h's Sweep: the magazine's rules)
void bd_sweep(const XrPosef* aim[2], uint64_t now) {
  Binder3& B = g_bd;
  const bool one = B.hold[0] != B.hold[1];
  const int fh = B.hold[0] ? 1 : 0;
  const bool active = B.out && !B.previewing && one && aim[fh] != nullptr && !S.trigDown[fh] && S.mode == kNone;
  XrVector3f l{0.0f, 0.0f, 0.0f};
  bool near = false;
  if (active) {
    const BMesh& m = g_bm;
    const float s = B.scale;
    l = q_rot(q_conj(B.q), v_sub(aim[fh]->position, B.pos));  // (its own frame, in metres: x across, + right)
    const bool open = B.open > 0.5f;
    const float zp = (open ? -0.5f * m.T + m.ct : 0.5f * m.T) * s;
    const float xa = (open ? -m.T - m.W : 0.0f) * s - 0.06f, xb = m.W * s + 0.06f;
    near = l.z > zp - 0.05f && l.z < zp + 0.2f && l.x > xa && l.x < xb && std::fabs(l.y) < 0.5f * m.H * s + 0.06f;
  }
  const int dir = B.sweep.step(active, near, l.x, l.y, l.z, now);
  if (dir != 0 && bd_turn(dir)) CLOG("binder: a page swiped %s", dir > 0 ? "on" : "back");
}
}  // namespace

void room_binder(xrfx::Scene& scene, const XrPosef& head, bool show, bool fxReady, uint64_t now) {
  Binder3& B = g_bd;
  if (!B.read) bd_read();
  const float dt = B.lastNs != 0 && now > B.lastNs ? std::min(0.1f, static_cast<float>(static_cast<double>(now - B.lastNs) * 1e-9)) : 0.0f;
  B.lastNs = now;
  // r163 (his r162 video: X never put it away once it was open / held): X toggles it from every state (shut, open, held,
  // a page turning: all dropped at once); leaving CARDS / the hub closing puts it away (g_binderAway)
  const bool toggle = g_binderToggle.exchange(false), away = g_binderAway.exchange(false);
  if ((toggle && B.out) || (away && B.out)) {
    B.out = false;
    B.hold[0] = B.hold[1] = false;
    g_binderHeld = false;
    B.flipTo = -1;
    B.flipDir = 0;
    B.want.reset();
    if (S.mode == kOut && g_bdOutCard.load() >= 0) S.askAway = true;  // (a card of it out in your hand goes back too)
    g_binderOut = false;
    bd_save();
    buzz(0.3f, 0.05f);
    ++g_gen;
    CLOG("binder: put away (%s)", toggle ? "X" : "left CARDS / the hub closed");
  } else if (toggle) {
    B.out = true;
    // where you last left it, if that's still near you (3 m); else in front of you
    if (!(B.pos.x != 0.0f || B.pos.y != 0.0f || B.pos.z != 0.0f) || v_len(v_sub(B.pos, head.position)) > 3.0f) {
      bd_place(head, &B.pos, &B.q, 0.5f);
      B.scale = 1.0f;
    }
    B.open = B.openTo = B.openFrom = 0.0f;
    B.hold[0] = B.hold[1] = false;
    g_binderHeld = false;
    g_binderOut = true;
    bd_save();
    buzz(0.3f, 0.05f);
    ++g_gen;
    CLOG("binder: out in your room");
  }
  const int prev = g_skinPreview.load();
  B.previewing = prev >= 0;
  if (!g_loaded.load() || !show || !fxReady || (!B.out && !B.previewing)) {
    if (!B.out && !B.previewing && (B.up || B.pagesUp)) bd_drop(fxReady);
    if (!show || !B.out) {
      B.hold[0] = B.hold[1] = false;
      g_binderHeld = false;
    }
    return;
  }
  // the skin: the one on, a preview's
  const int skin = B.previewing ? prev : look_on(kLookSkin);
  if (!B.atlas || B.atlas->skin != skin) B.atlas = atlas_make(skin);
  // the pages: the spread the hub shows (a turn to it when it moves while open); redrawn when the collection changes
  int spread = 0;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    spread = g_spread;
  }
  const int gen = g_gen.load();
  // r163: a card of it back from your hand (put back / put away): its pocket drawn full again
  if (g_bdOutCard.load() >= 0 && S.mode != kOut && S.mode != kAway) {
    g_bdOutCard = -1;
    B.pagesGen = -1;
  }
  if (B.flipTo < 0 && !B.want && (B.shown != spread || (gen != B.pagesGen && B.open > 0.01f))) {
    B.want = bpages_make(spread);
    B.pagesGen = gen;
  }
  if (B.want) {
    bool done = false;
    {
      std::lock_guard<std::mutex> lock(B.want->m);
      done = B.want->done;
    }
    if (done) {
      const bool turn = B.shown >= 0 && B.want->spread != B.shown && B.open > 0.98f && !B.previewing;
      {
        std::lock_guard<std::mutex> lock(B.want->m);
        if (turn) {  // its pictures into the two free quads; the sheet turns from the old to them
          B.pic[(B.qL + 2) % 4] = B.want->px[0];
          B.pic[(B.qR + 2) % 4] = B.want->px[1];
          B.flipTo = B.want->spread;
          B.flipDir = B.want->spread > B.shown ? 1 : -1;
          B.flipT = 0.0f;
          B.flipNs = now;
        } else {
          B.pic[B.qL] = B.want->px[0];
          B.pic[B.qR] = B.want->px[1];
          B.shown = B.want->spread;
        }
      }
      B.want.reset();
      B.pagesNeedUp = true;  // (r163: only the pages' model)
    }
  }
  if ((!B.up || B.built != skin) && B.atlas) {  // (r163: once a skin)
    bool ready = false;
    {
      std::lock_guard<std::mutex> lock(B.atlas->m);
      ready = B.atlas->done;
    }
    if (ready) bd_model_up(fxReady);
  }
  if (!B.up) return;
  if (B.pagesNeedUp) bd_pages_up(fxReady);
  // a page turning; opening / shutting
  if (B.flipTo >= 0) {
    bool done = false;  // (r160 round 2: over the spine in xr_pageturn.h's 0.45 s, as the magazine's)
    B.flipT = xrturn::turn_ease(0.0f, 1.0f, B.flipNs, now, &done);
    if (done) {
      B.qL = (B.qL + 2) % 4;
      B.qR = (B.qR + 2) % 4;
      B.shown = B.flipTo;
      B.flipTo = -1;
      B.flipDir = 0;
      B.flipT = 0.0f;
    }
  }
  {  // its cover over the spine as a magazine's cover turns (0.45 s)
    bool done = false;
    B.open = xrturn::turn_ease(B.openFrom, B.openTo, B.openNs, now, &done);
  }
  (void)dt;
  // where it is: yours where you left it; a preview in front of you, turning a little to show its cover and spine
  XrVector3f pos = B.pos;
  XrQuaternionf q = B.q;
  float scale = B.scale, open = B.open;
  if (B.previewing) {
    bd_place(head, &pos, &q, 0.55f);
    const float t = static_cast<float>(static_cast<double>(now % 100000000000ull) * 1e-9);
    q = q_mul(q, q_axis(0.0f, 1.0f, 0.0f, 0.45f * std::sin(t * 0.8f) - 0.25f));
    scale = 1.0f;
    open = 0.0f;
  }
  const float keepOpen = B.open;
  B.open = open;
  bd_layout();
  B.open = keepOpen;
  // (column-major: its turn x its size, then its place)
  const XrVector3f ax = q_rot(q, XrVector3f{scale, 0, 0}), ay = q_rot(q, XrVector3f{0, scale, 0}), az = q_rot(q, XrVector3f{0, 0, scale});
  const float M[16] = {ax.x, ax.y, ax.z, 0, ay.x, ay.y, ay.z, 0, az.x, az.y, az.z, 0, pos.x, pos.y, pos.z, 1};
  // r163: a card of it out of its pocket, let go on that pocket: it slides back in
  if (S.mode == kOut && g_bdOutCard.load() >= 0 && S.card[0].hands == 0 && B.open > 0.98f && !B.previewing) {
    XrVector3f pocket;
    if (bd_pocket_world(B.outSlot, &pocket) && v_len(v_sub(S.card[0].pos, pocket)) < 0.09f * std::max(0.5f, scale)) {
      S.askAway = true;
      CLOG("binder: the card let go on its pocket - back in");
    }
  }
  // r163 (Lance: "heavyyyyy on every special skin is moving and alive"): its living skin (xr_binderfx.cpp) - halos, light,
  // particles, fur that sways and parts at your hand
  {
    xrbfx::BinderView v;
    v.skin = look_item(kLookSkin, skin).id;
    v.skinIndex = skin;
    std::memcpy(v.toWorld, M, sizeof(M));
    v.scale = scale;
    v.open = open;
    v.W = g_bm.W, v.H = g_bm.H, v.T = g_bm.T, v.ct = g_bm.ct;
    for (int h = 0; h < 2; ++h) {
      v.handValid[h] = S.handHave[h];
      v.hand[h] = S.handAt[h];
    }
    v.vel = dt > 1e-4f ? v_mul(v_sub(pos, B.lastPos), 1.0f / dt) : XrVector3f{0.0f, 0.0f, 0.0f};
    B.lastPos = pos;
    if (B.fur) {  // (r163 quick fixes: only a furry binder's vertices move on the CPU)
      B.rest.assign(B.verts.begin(), B.verts.begin() + static_cast<std::ptrdiff_t>(std::min(B.verts.size(), B.pageBase * 6)));
      v.verts = B.verts.data();
      v.vertCount = static_cast<int>(B.pageBase);
      v.rest = B.rest.data();
      v.shellSrc = B.shellSrc.data();
      v.shellH = B.shellH.data();
    }
    v.tableValid = scene.tabletop && scene.mr;  // (the light pool: on the Tabletop field when it's under the binder)
    v.tablePos = scene.tablePos;
    v.tableYaw = scene.tableYaw;
    v.tableScale = scene.tableScale;
    v.floorY = g_floorY;
    v.dt = dt;
    v.now = now;
    xrbfx::frame(scene, v, fxReady);
  }
  for (int slot : {kBinderSlot, kBinderPagesSlot}) {
    if (slot == kBinderPagesSlot && !B.pagesUp) continue;
    xrfx::Scene::ModelDraw& md = scene.models[slot];
    md.on = true;
    md.posNrm = slot == kBinderSlot ? B.verts.data() : B.verts.data() + B.pageBase * 6;  // (r163: its pages their own model)
    md.local = true;
    std::memcpy(md.toWorld, M, sizeof(M));
    md.white = static_cast<float>((B.look.holo ? 1 : 0) | (B.look.sparkle ? 2 : 0) | (B.look.pulse ? 4 : 0));
    const float* e = B.look.emit;
    md.glow = e[0] + e[1] + e[2] > 0.01f ? 2.2f : 0.0f;
    md.glowRgb = (static_cast<uint32_t>(clampf(e[0], 0, 1) * 255.0f) << 16) | (static_cast<uint32_t>(clampf(e[1], 0, 1) * 255.0f) << 8) |
                 static_cast<uint32_t>(clampf(e[2], 0, 1) * 255.0f);
    md.alpha = 1.0f;
    md.under = -5.5f;  // (xr_fx_model.frag: the binder)
    md.flameFrame = -1;
    md.shine = 0.0f;
    md.frame = -1;
    md.kaleido = static_cast<uint32_t>(skin + 1);  // r163: its skin for the shader's living touches
    if (slot == kBinderSlot && !B.fur)  // (r163 quick fixes: its baked pose nearest how open it is - nothing uploaded)
      md.frame = static_cast<int>(std::lround(clampf(open, 0.0f, 1.0f) * (kBakedOpen - 1)));
  }
}

bool binder_held() { return g_binderHeld.load(); }
void binder_turn(int how) {
  // r163: B (-2) only ever shuts it - "a page turned (the hub's B)" in r162's log was this line's wording, its B shut it
  if (!g_binderHeld.load() || S.mode != kNone) return;
  if (bd_turn(how)) CLOG("binder: %s (the hub's %s)", how == -2 ? "shut" : "a page turned", how == 2 ? "A" : (how == -2 ? "B" : "stick"));
}
void binder_put_away() { g_binderAway = true; }  // r163: (room_binder, this frame)
void binder_toggle() { g_binderToggle = true; }
int binder_state() {
  if (!g_bd.out) return 0;
  if (S.mode == kOut && g_bdOutCard.load() >= 0) return 4;
  if (g_bd.openTo < 0.5f) return g_binderHeld.load() ? 5 : 1;
  return g_binderHeld.load() ? 3 : 2;
}

// r230: LANCINI EXQUISITE's opening - THE VAULT (its case, latches, lid, light) and the private gallery (its mood, its music)
namespace {  // (the scene's own: the anonymous namespace again)
#include "xr_cards_vault.inc"
}  // namespace

void hands(const bool grip[2], const bool have[2], const XrVector3f hand[2], const XrPosef* aim[2], const XrPosef* gripPose[2],
           uint64_t now) {
  disp_grab(grip, have, hand);  // r152: the slab shelf in your room
  for (int h = 0; h < 2; ++h) {  // r163: the pointers this frame
    S.aimOk[h] = aim[h] != nullptr;
    if (aim[h] != nullptr) S.aimAt[h] = *aim[h];
  }
  // r163: a grip closing with its pointer on a card in the open 3D binder: that card out of its pocket into the hand
  // (before the binder's own grab: a grip on a card takes the card, elsewhere the binder)
  for (int h = 0; h < 2; ++h) {
    if (!(grip[h] && !g_bd.prevGrip[h] && have[h]) || S.mode != kNone || g_bd.hold[0] || g_bd.hold[1]) continue;
    const int slot = bd_pocket_at(h);
    if (slot >= 0 && bd_take(h, slot, true, hand[h], now)) g_bd.prevGrip[h] = true;
  }
  bd_grab(grip, have, hand, g_bd.out && g_bd.up);  // r160: your 3D binder
  bd_sweep(aim, now);  // r160 round 2: the free hand's sweep across it (the magazine's)
  for (int h = 0; h < 2; ++h) {
    S.handHave[h] = have[h];
    if (have[h]) S.handAt[h] = hand[h];
    if (gripPose[h] != nullptr) S.handQ[h] = q_unit(gripPose[h]->orientation);  // r142 (untracked: its last turn held)
  }
  // a grip with its pointer on a pocket of the binder: that card out, into the hand
  const bool pageUp = S.pageUpNs != 0 && mono_ns() - S.pageUpNs < 200000000ull;
  if (S.mode == kNone && pageUp && S.hubValid && S.hubW > 0.0f && S.hubH > 0.0f && g_loaded.load()) {
    for (int h = 0; h < 2 && S.mode == kNone; ++h) {
      if (!(grip[h] && !S.prevGrip[h] && have[h] && aim[h] != nullptr)) continue;
      const XrQuaternionf qi = q_conj(S.hubPose.orientation);
      const XrVector3f o = q_rot(qi, v_sub(aim[h]->position, S.hubPose.position));
      const XrVector3f d = q_rot(qi, q_rot(aim[h]->orientation, XrVector3f{0.0f, 0.0f, -1.0f}));
      if (std::fabs(d.z) < 1e-3f) continue;
      const float t = -o.z / d.z;
      if (t <= 0.0f || t > 8.0f) continue;
      const float X = ((o.x + d.x * t) / S.hubW + 0.5f) * 2160.0f, Y = (0.5f - (o.y + d.y * t) / S.hubH) * 1300.0f;
      for (int slot = 0; slot < kPerSpread; ++slot) {
        float x0, y0, x1, y1;
        pocket_rect(slot, &x0, &y0, &x1, &y1);
        {  // r157: where the hub draws it (its page area, right of the rail)
          const float ox = g_fitOx.load(), oy = g_fitOy.load(), k = g_fitK.load();
          x0 = ox + x0 * k, x1 = ox + x1 * k, y0 = oy + y0 * k, y1 = oy + y1 * k;
        }
        if (X < x0 || X > x1 || Y < y0 || Y > y1) continue;
        int card = -1;
        {
          std::lock_guard<std::mutex> lock(g_mutex);
          const std::vector<int> list = binder_list();
          const size_t at = static_cast<size_t>(g_spread) * kPerSpread + slot;
          if (at < list.size() && owns(list[at])) card = list[at];
          if (card >= 0) g_cursor = slot;
        }
        if (card < 0) break;
        const XrVector3f pocket =
            v_add(S.hubPose.position, q_rot(S.hubPose.orientation, XrVector3f{(0.5f * (x0 + x1) / 2160.0f - 0.5f) * S.hubW,
                                                                                (0.5f - 0.5f * (y0 + y1) / 1300.0f) * S.hubH, 0.03f}));
        look_card(card, now, &pocket);
        Sheet& s = S.card[0];
        const XrVector3f tip = v_add(aim[h]->position, q_rot(aim[h]->orientation, XrVector3f{0.0f, 0.03f, -0.11f}));
        s.free = true;
        s.hands = 1 << h;
        S.hold[h] = 1;
        S.spent[h] = true;
        // r142: in this hand from its pocket (turned to your eyes as it comes out; then as the hand turns), gliding to
        // just ahead of the hand
        sheet_take(s, h, hand[h]);
        s.relPTo = q_rot(q_conj(S.handQ[h]), v_sub(tip, hand[h]));
        s.offFade = 0.4f;
        S.prevGrip[h] = true;
        buzz(0.3f, 0.05f);
        break;
      }
    }
  }
  vault_hands(grip, have, hand, now);  // r230: THE VAULT's latches (before the pack can be taken)
  hold_sheets(grip, have, hand, aim, now);
}

bool hand_busy(int h) { return h >= 0 && h < 2 && (S.hold[h] >= 0 || S.spent[h] || g_disp.hold[h] || g_bd.hold[h]); }  // (r152: the slab shelf; r160: the binder)

void triggers(const float pull[2], uint64_t now) {
  bool pressed[2];
  for (int h = 0; h < 2; ++h) {
    pressed[h] = !S.trigDown[h] && pull[h] > 0.6f;
    if (pull[h] > 0.6f) S.trigDown[h] = true;
    else if (pull[h] < 0.3f) S.trigDown[h] = false;
  }
  // r163: a trigger with its pointer on a card in the open 3D binder takes that card out (the trigger elsewhere: a page)
  for (int h = 0; h < 2; ++h) {
    if (!pressed[h] || S.mode != kNone) continue;
    const int slot = bd_pocket_at(h);
    if (slot >= 0 && bd_take(h, slot, false, S.handAt[h], now)) {
      pressed[h] = false;
      g_bd.prevTrig[h] = true;
    }
  }
  // magazine's (xr_pageturn.h: pulled past kTrigOn, let go under kTrigOff, not in the first 0.9 s of the grab)
  for (int h = 0; h < 2; ++h) {
    const bool down = pull[h] > xrturn::kTrigOn;
    if (g_binderHeld.load() && S.mode == kNone && down && !g_bd.prevTrig[h] && mono_ns() - g_bd.grabNs > xrturn::kOpenGuardNs) {
      if (bd_turn(h == 1 ? 1 : -1)) CLOG("binder: a page turned %s (%s trigger)", h == 1 ? "on" : "back", h == 1 ? "right" : "left");
      pressed[h] = false;
    }
    if (down) g_bd.prevTrig[h] = true;
    if (pull[h] < xrturn::kTrigOff) g_bd.prevTrig[h] = false;
  }
  if (S.mode != kPackUp) return;
  if (cameo_live() && (pressed[0] || pressed[1])) {  // r141: a trigger during a 1/1's cameo sends it back first
    cameo_skip();
    return;
  }
  if (god_live() && (pressed[0] || pressed[1])) {  // r149: a GOD PACK's show: a trigger skips it too
    god_skip();
    return;
  }
  for (int h = 0; h < 2; ++h)  // r230: THE VAULT waiting - a trigger flicks a latch (the nearest the hand / under its pointer)
    if (pressed[h] && S.phase == kVault && vault_trigger(h, now)) return;
  for (int h = 0; h < 2; ++h) {
    if (!pressed[h] || S.pack.hands == (1 << h)) continue;  // (the holding hand's own trigger does nothing)
    if (S.phase == kWaiting) tear(now, h);
    else if (S.phase == kStack) stack_step(now);
    return;
  }
}

float take_buzz(float* seconds) {
  const float a = S.buzz;
  *seconds = S.buzzS;
  S.buzz = 0.0f;
  S.buzzS = 0.0f;
  return a;
}
float room_dark() { return g_pullDark.load(); }    // r219
float room_flash() { return g_pullFlash.load(); }  // r219
float room_gallery() { return g_gallery.load(); }  // r230

void close(bool fxReady) {
  g_skinPreview = -1;  // r160: (a LOOKS preview ends with the hub)
  if (xrballs::previewing()) xrballs::stop_preview();  // r168: (and a ball's)
  if (g_bd.out) g_binderAway = true;  // r163: the hub closing puts your 3D binder away
  S.askOpen = S.askTear = S.askAway = S.nextPack = false;
  S.askLook = -1;
  if (S.mode == kNone && S.packModel < 0 && S.cardModel < 0) return;
  if (S.mode == kPackUp && (S.phase == kWaiting || S.phase == kVault)) unopen();  // (r230)
  end_scene(fxReady);
}

bool frame(xrfx::Scene& scene, const XrPosef& head, bool live, bool fxReady, const XrCompositionLayerQuad* hub, uint64_t now) {
  poll();
  exq_clubs_refresh(now);  // r230: the GRAND THREADS patches' clubs
  gallery_frame(now, S.lastNs != 0 ? std::min(0.05f, secs(now, S.lastNs)) : 0.0f);  // r230: the EXQUISITE gallery's mood + music
  // r219: the room's dark / flash only while a 1/1's build-up runs in the stack in your hand (pull_frame, below) - else never (set
  // once a frame: xr_quest's passthrough never sees a stray 0 between)
  if (S.mode != kPackUp || S.phase != kStack || !live) g_pullCard = -1;
  if (g_pullCard < 0) {
    g_pullDark = 0.0f;
    g_pullFlash = 0.0f;
  }
  if (!live) {
    close(fxReady);
    S.hubValid = false;
    S.lastNs = now;
    return false;
  }
  S.head = head;
  S.hubValid = hub != nullptr;
  if (hub != nullptr) {
    S.hubPose = hub->pose;
    S.hubW = hub->size.width;
    S.hubH = hub->size.height;
  }
  const float dt = S.lastNs != 0 ? std::min(0.05f, secs(now, S.lastNs)) : 0.0f;
  // the hub's buttons' asks
  if (S.askTear) {
    S.askTear = false;
    if (S.mode == kPackUp && S.phase == kVault) vault_flick_next(now);  // r230: A flicks THE VAULT's next latch
    else tear(now, -1);
  }
  if (S.askAway) {
    S.askAway = false;
    put_away(now);
  }
  if (S.askOpen) {
    S.askOpen = false;
    if (S.mode == kNone) open_pack(now);
  }
  if (S.askLook >= 0) {
    if (S.mode == kNone) look_card(S.askLook, now, nullptr);
    S.askLook = -1;
  }
  if (S.askSlab >= 0) {  // r152: a slab from the GRADING page
    if (S.mode == kNone) look_card(g_slabs.size() > static_cast<size_t>(S.askSlab) ? g_slabs[static_cast<size_t>(S.askSlab)].ref.card : 0, now,
                                   nullptr, S.askSlab);
    S.askSlab = -1;
  }
  // r152: a slab back from TIZM GRADING you haven't seen: its BACK FROM GRADING moment, as the CARDS page comes up
  if (S.mode == kNone && S.pageUpNs != 0 && now - S.pageUpNs < 300000000ull) {
    int back = -1;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      for (size_t k = 0; k < g_slabs.size() && back < 0; ++k)
        if (g_slabs[k].left == 0 && !g_slabs[k].seen) back = static_cast<int>(k);
    }
    if (back >= 0) grade_reveal(back, now);
  }
  if (S.mode == kNone) {
    S.lastNs = now;
    return false;
  }
  // the pictures up once drawn
  if (fxReady && S.packPics && S.packModel != S.packPics->gen) {
    bool done;
    {
      std::lock_guard<std::mutex> lock(S.packPics->m);
      done = S.packPics->left == 0;
    }
    if (done) {
      const std::vector<GridPart> parts = {{0, kPackNX, kBodyNY, kTearV, 1.0f, false},
                                           {1, kPackNX, kBodyNY, kTearV, 1.0f, true},
                                           {0, kPackNX, kStripNY, 0.0f, kTearV, false},
                                           {1, kPackNX, kStripNY, 0.0f, kTearV, true}};
      xrfx::set_model(kPackSlot, grid_model(S.packPics, parts, false));
      S.packModel = S.packPics->gen;
      S.packVerts.assign(static_cast<size_t>(kPackNV) * 6, 0.0f);
      CLOG("the pack's pictures up%s", xrfx::model_up(kPackSlot) ? "" : " - failed");
    }
  }
  if (fxReady && S.cardPics && S.cardModel != S.cardPics->gen) {
    bool done;
    {
      std::lock_guard<std::mutex> lock(S.cardPics->m);
      done = S.cardPics->left == 0;
    }
    if (done) {
      // r138: each card its front, its back and its edge (card_box's order: kCardVerts a card)
      std::vector<GridPart> parts;
      for (int i = 0; i < S.n; ++i) {
        parts.push_back({i, kCardNX, kCardNY, 0.0f, 1.0f, false});
        parts.push_back({S.n + i, kCardNX, kCardNY, 0.0f, 1.0f, true});
        parts.push_back({2 * S.n + i, kEdgeSegs, 1, 0.0f, 1.0f, false});
      }
      for (int i = 0; i < S.n; ++i) parts.push_back({3 * S.n + i, 1, 1, 0.0f, 1.0f, false});  // r147: the level moments
      xrfx::Model cm = grid_model(S.cardPics, parts, false);
      punch_parts(cm, S.n, S.cutShape);  // r220: THE PUNCH's (kPunchVerts a card)
      xrfx::set_model(kCardSlot, std::move(cm));
      S.cardModel = S.cardPics->gen;
      S.cardVerts.assign(static_cast<size_t>(S.n) * (kCardVerts + kTagVerts + kPunchVerts) * 6, 0.0f);
      S.cardsNs = now;
      CLOG("the cards' pictures up (%d)%s", S.n, xrfx::model_up(kCardSlot) ? "" : " - failed");
    }
  }
  const bool cardsUp = S.cardPics && S.cardModel == S.cardPics->gen;
  if (fxReady && cardsUp) late_fronts();  // r226: a front drawn before its big picture was in: drawn again + swapped in
  const float k = 1.0f - std::exp(-dt * 9.0f);
  auto settle = [&](Sheet& s) {
    if (s.hands == 0 && !s.free) {
      s.pos = v_lerp(s.pos, s.tPos, k);
      s.roll += (s.tRoll - s.roll) * k;
      s.scale += (s.tScale - s.scale) * k;
      s.relYaw += (0.0f - s.relYaw) * k;
    }
    // a flip under way (the pack's pouch bends as it goes; r138: a card turns over stiff)
    float bendTo = s.hands != 0 && s.hands != 3 ? 1.2f : 0.0f;
    if (s.flipDur > 0.0f && now >= s.flipNs) {
      const float t = secs(now, s.flipNs) / s.flipDur;
      // r219: a hit's build-up turns by its own curve (xr_pulls.h: slow to edge-on, then the snap); anything else r139's smooth turn
      s.flipP = s.pull.tier > 0 ? xrpulls::progress(s.pull, secs(now, s.flipNs)) : smooth(t);
      s.flip = s.flipFrom + (s.flipTo - s.flipFrom) * s.flipP;
      bendTo += 5.0f * std::sin(3.14159265f * clampf(t, 0.0f, 1.0f));
      if (t >= 1.0f) {
        s.flip = s.flipTo;
        s.flipDur = 0.0f;
        s.flipP = 1.0f;
      }
    }
    // its own motion throws the bend (a flick), a spring - not a card's (r138: no bend, droop or flutter at all)
    const XrVector3f v = dt > 1e-4f ? v_mul(v_sub(s.pos, s.lastPos), 1.0f / dt) : XrVector3f{0.0f, 0.0f, 0.0f};
    const float push = dt > 1e-4f ? clampf(v_len(v_sub(v, s.vel)) / dt * 0.02f, 0.0f, 30.0f) : 0.0f;
    s.vel = v;
    s.lastPos = s.pos;
    if (s.rigid) {
      s.bend = s.bendV = 0.0f;
      return;
    }
    s.bendV += (90.0f * (bendTo - s.bend) - 7.0f * s.bendV + push) * dt;
    s.bend = clampf(s.bend + s.bendV * dt, -6.0f, 8.0f);
  };
  // the pack in your hand, its rip, its cards filing out into a stack in that hand, the stack (r139)
  if (S.mode == kPackUp) {
    settle(S.pack);
    const XrQuaternionf hq = sheet_base(S.pack);  // the holder: the pack, then the stack in its hand
    const float hs = S.pack.scale;
    auto place = [&](Sheet& s, const XrVector3f& local, float pop) {
      s.fixedBase = true;
      s.base = hq;
      s.free = true;
      s.scale = hs * kStackCard * (1.0f + kPopScale * pop);
      s.pos = v_add(S.pack.pos, q_rot(hq, v_mul(local, hs)));
    };
    if (S.phase == kFiling) {
      // one by one (once their pictures are up): up out of the pack, onto the stack, backs to you (the back card first)
      const uint64_t base = std::max(S.cardsNs, S.phaseNs + static_cast<uint64_t>(kFileStart * 1e9f));
      int done = 0;
      for (int k = 0; k < S.n; ++k) {
        const int i = S.n - 1 - k;
        Sheet& s = S.card[i];
        s.flip = 3.14159265f;
        const uint64_t st = base + static_cast<uint64_t>(static_cast<float>(k) * kFileGap * 1e9f);
        const float u = cardsUp && now >= st ? secs(now, st) / kFileDur : -1.0f;
        s.shown = u >= 0.0f;
        if (u < 0.0f) {
          place(s, XrVector3f{0.0f, -0.01f, 0.0f}, 0.0f);  // (inside, unseen)
        } else if (u >= 1.0f) {
          ++done;
          S.zAt[i] = stack_z(S.depth[i]);
          place(s, XrVector3f{0.0f, 0.0f, S.zAt[i]}, 0.0f);
        } else {
          if (k >= S.filed) {  // just starting out: its slide
            S.filed = k + 1;
            card_sound(1);
            buzz(0.12f, 0.03f);
          }
          place(s, file_path(i, u), 0.0f);
        }
        s.lastPos = s.pos;
      }
      if (done == S.n && S.n > 0) {
        S.phase = kStack;
        S.phaseNs = now;
        // the wrapper, empty, falls away; the front card's 3D (if it has any) made ready before it's turned over
        S.wrapFall = true;
        S.wrapPos = S.pack.pos;
        S.wrapQ = hq;
        S.wrapScale = hs;
        // (r142: off the stack's back whichever way it's held, down by gravity)
        S.wrapVel = v_add(q_rot(hq, XrVector3f{0.05f, 0.0f, -0.35f}), XrVector3f{0.0f, -0.25f, 0.0f});
        buzz(0.25f, 0.05f);
        special_focus(front_card(), now, fxReady);
        CLOG("the cards out of the pack, a stack in your hand");
      }
    } else if (S.phase == kStack) {
      for (int i = 0; i < S.n; ++i) {
        Sheet& s = S.card[i];
        s.shown = true;
        settle(s);  // (its turn over, stiff)
        // r219: a hit's build-up lifts it up out of the stack toward your eyes (4 cm up, 5 cm nearer, 6% bigger), eased back after
        const float lift = s.pull.tier > 0 && now >= s.flipNs ? xrpulls::lift(s.pull, secs(now, s.flipNs)) : 0.0f;
        const XrVector3f liftAt{0.0f, 0.04f * lift, 0.05f * lift};
        const float liftPop = 0.06f / kPopScale * lift;
        if (S.backNs[i] != 0) {  // round to the back
          const float u = secs(now, S.backNs[i]) / kBackDur;
          S.backU[i] = u;
          if (u >= 1.0f) {
            S.backNs[i] = 0;
            S.backU[i] = 0.0f;
            S.popK[i] = 0.0f;
            S.zAt[i] = stack_z(S.depth[i]);
            place(s, XrVector3f{0.0f, 0.0f, S.zAt[i]}, 0.0f);
          } else {
            float pop = 0.0f;
            const XrVector3f l = back_path(i, u, &pop);
            S.zAt[i] = l.z;
            place(s, v_add(l, liftAt), pop + liftPop);
          }
        } else {
          // to its place (the rest a place nearer as one goes round); the front one turned over out toward you
          const float want = S.depth[i] == 0 && S.faceUp[i] ? 1.0f : 0.0f;
          S.popK[i] += (want - S.popK[i]) * std::min(1.0f, dt * (want > S.popK[i] ? 9.0f : 12.0f));
          S.zAt[i] += (stack_z(S.depth[i]) - S.zAt[i]) * std::min(1.0f, dt * 14.0f);
          const float p = smooth(S.popK[i]);
          place(s, v_add(XrVector3f{0.0f, kPopY * p, S.zAt[i] + kPopZ * p}, liftAt), p + liftPop);
        }
        // (r219: its moment where its turn passes half way - r139's half its time for a plain turn, the same frame)
        if (s.flipFx && s.flipDur > 0.0f && now >= s.flipNs && s.flipP >= 0.5f) {
          s.flipFx = false;
          reveal_fx(i);
        }
      }
      pull_frame(now);  // r219: the build-up's heartbeat, haptics, the dark room
    }
    if (S.wrapFall) {  // the empty wrapper, falling, turning, gone
      S.wrapVel.y -= 2.4f * dt;
      S.wrapPos = v_add(S.wrapPos, v_mul(S.wrapVel, dt));
      S.wrapQ = q_mul(q_axis(0.2f, 0.1f, 0.9747f, 1.6f * dt), S.wrapQ);
      S.wrapScale = std::max(0.0f, S.wrapScale - dt * 1.1f);
      if (S.wrapScale <= 0.0f) S.wrapFall = false;
    }
    if (S.torn) {
      S.stripVel.y -= 2.2f * dt;
      S.stripPos = v_add(S.stripPos, v_mul(S.stripVel, dt));
      S.stripQ = q_mul(q_axis(0.3f, 0.2f, 0.93f, S.stripSpin * dt), S.stripQ);
      S.stripScale = std::max(0.0f, S.stripScale - dt / 0.9f);
    }  } else if (S.mode == kOut) {
    Sheet& s = S.card[0];
    settle(s);
    s.shown = cardsUp;
  } else if (S.mode == kTrade) {
    // r146: a trade-in's show (trade_show): once the cards' pictures are up, one every kTradeGap s: off the hub's picture,
    // over the slot, down into it (smaller and smaller); the pack up out of it, spinning; then into the binder
    constexpr float kTradeGap = 0.16f, kTradeDur = 0.8f, kPopHold = 1.5f, kPopAway = 0.5f;
    const XrVector3f over = v_add(S.slot, XrVector3f{0.0f, 0.2f, 0.0f});
    if (cardsUp && S.cardsNs == 0) S.cardsNs = now;
    const uint64_t base = S.cardsNs != 0 ? S.cardsNs + 150000000ull : 0;
    bool allIn = base != 0;
    for (int i = 0; i < S.n; ++i) {
      Sheet& s = S.card[i];
      const uint64_t st = base + static_cast<uint64_t>(i * kTradeGap * 1e9f);
      const float u = base != 0 && now >= st ? secs(now, st) / kTradeDur : -1.0f;
      if (u < 1.0f) allIn = false;
      s.shown = u >= 0.0f && u < 1.0f && !S.tradeSkip;
      if (u < 0.0f) {
        s.pos = S.tradeFrom[i];
        continue;
      }
      if (u < 0.55f) {  // off the hub, up over the slot, growing
        const float v = smooth(u / 0.55f);
        s.pos = v_add(v_lerp(S.tradeFrom[i], over, v), XrVector3f{0.0f, 0.08f * std::sin(3.14159265f * v), 0.0f});
        s.scale = 0.3f + 0.6f * v;
      } else {  // down into the slot, drawn in
        const float v = clampf((u - 0.55f) / 0.45f, 0.0f, 1.0f);
        if (!S.tradeSlid[i]) {
          S.tradeSlid[i] = true;
          card_sound(1);
        }
        s.pos = v_lerp(over, v_add(S.slot, XrVector3f{0.0f, -0.03f, 0.0f}), v * v);
        s.scale = 0.9f * (1.0f - smooth(clampf((v - 0.35f) / 0.65f, 0.0f, 1.0f)));
        if (u >= 1.0f && S.tradeIn <= i) {
          S.tradeIn = i + 1;
          buzz(0.12f, 0.03f);
        }
      }
      s.lastPos = s.pos;
    }
    // the slot: glittering while cards go in
    if (fxReady && base != 0 && S.popNs == 0) xrfx::card_sparkle(S.slot, 0.07f, 0.025f, 26.0f, dt, 0xFFE27A, 0xFFFFFF);
    if ((allIn || S.tradeSkip) && S.popNs == 0) {  // the pack out of it
      S.popNs = now;
      S.pack.shown = true;
      S.burst = true;
      S.burstRgb = kGold;
      S.burstAt = S.slot;
      S.sparkleAt = S.slot;
      S.sparkleR = 0.1f;
      S.sparkleT = 1.6f;
      coin_sound(4);
      buzz(0.7f, 0.2f);
      CLOG("trade-in: the pack out");
    }
    if (S.popNs != 0 && S.tradeSkip && now > S.popNs) {  // (skipped: its coins rang, done)
      end_scene(fxReady);
      S.lastNs = now;
      return false;
    }
    if (S.popNs != 0) {
      const float pt = secs(now, S.popNs);
      if (pt < kPopHold) {  // up out of the slot with a bounce, spinning to a stop
        const float v = clampf(pt / 0.45f, 0.0f, 1.0f);
        const float rise = 0.24f * (1.0f - (1.0f - v) * (1.0f - v)) + 0.03f * std::sin(9.0f * pt) * std::exp(-3.0f * pt);
        S.pack.pos = v_add(S.slot, XrVector3f{0.0f, rise, 0.0f});
        S.pack.scale = 0.2f + 0.85f * smooth(v) + 0.08f * std::sin(7.0f * pt) * std::exp(-2.5f * pt);
        S.pack.relYaw = 4.0f * 3.14159265f * (1.0f - smooth(clampf(pt / 1.1f, 0.0f, 1.0f)));
      } else {  // into the binder with your packs
        const float v = smooth(clampf((pt - kPopHold) / kPopAway, 0.0f, 1.0f));
        XrVector3f to = S.hubValid ? S.hubPose.position : v_add(S.anchor, XrVector3f{0.0f, -0.8f, 0.0f});
    XrVector3f pocket;  // r163: a card out of the 3D binder slides back into its own pocket
    const bool home3d = g_bdOutCard.load() >= 0 && g_bd.out && bd_pocket_world(g_bd.outSlot, &pocket);
    if (home3d) to = pocket;
        S.pack.pos = v_lerp(v_add(S.slot, XrVector3f{0.0f, 0.24f, 0.0f}), to, v);
        S.pack.scale = 1.05f * (1.0f - v);
        S.pack.relYaw = 0.0f;
        if (pt >= kPopHold + kPopAway) {
          end_scene(fxReady);
          S.lastNs = now;
          return false;
        }
      }
      S.pack.lastPos = S.pack.pos;
    }
  } else if (S.mode == kAway) {
    // into the binder: toward the hub's picture (or down), smaller and smaller
    const float t = secs(now, S.t0) / 0.45f;
    const XrVector3f to = S.hubValid ? S.hubPose.position : v_add(S.anchor, XrVector3f{0.0f, -0.8f, 0.0f});
    for (int i = 0; i < S.n; ++i) {
      Sheet& s = S.card[i];
      s.free = true;
      s.pos = v_lerp(s.pos, to, std::min(1.0f, k * 1.4f));
      s.scale = std::max(0.0f, s.scale - dt * 3.0f);
      settle(s);
    }
    S.pack.scale = std::max(0.0f, S.pack.scale - dt * 3.0f);
    S.stripScale = 0.0f;
    if (t >= 1.0f) {
      const bool next = S.nextPack;
      S.nextPack = false;
      end_scene(fxReady);
      if (next && g_packCount.load() > 0) open_pack(now);  // the next one up at once
      S.lastNs = now;
      return false;
    }
  }
  // the sparkles, the shiny's show
  if (fxReady) {
    if (S.sparkleT > 0.0f) {
      xrfx::evo_sparkle(S.sparkleAt, S.sparkleR, 2.0f * S.sparkleR + 0.12f, dt);
      S.sparkleT -= dt;
    }
    if (S.burst) {
      S.burst = false;
      xrfx::evo_burst(scene, S.burstAt, 0.3f, S.burstRgb);
    }
    if (S.shredT > 0.0f) {  // r139: foil shreds off the rip
      xrfx::card_sparkle(S.shredAt, 0.05f * S.pack.scale, 0.05f, 45.0f, dt, 0xE8ECF5, 0xA8B4C8);
      S.shredT -= dt;
    }
  }
  S.lastNs = now;
  vault_frame(scene, now, fxReady);  // r230: THE VAULT (its pack rising into your hands: before the pack is drawn)
  if (!fxReady) return false;
  bool shown = g_vault.on && g_vault.model >= 0;
  // the pack: its body (front and back, a pillow) and its strip (torn off: flying)
  if (S.packModel >= 0 && xrfx::model_up(kPackSlot) && S.packVerts.size() == static_cast<size_t>(kPackNV) * 6) {
    // (r139: in your hand until its cards are out; then the empty wrapper falling away, its own way)
    const bool inHand = S.phase != kStack;
    const bool packShown = S.pack.on && S.pack.shown && (S.mode == kPackUp || S.mode == kTrade) &&  // (r146: a trade-in's pack)
                           (inHand ? S.pack.scale > 0.01f : S.wrapFall && S.wrapScale > 0.01f);
    const XrQuaternionf q = inHand ? sheet_q(S.pack) : S.wrapQ;
    const XrVector3f bodyAt = inHand ? S.pack.pos : S.wrapPos;
    const float bodyScale = inHand ? S.pack.scale : S.wrapScale;
    float* o = S.packVerts.data();
    const float b = inHand ? S.pack.bend * 0.25f : 0.0f;  // (stiffer than a card)
    bend_grid(o, kPackNX, kBodyNY, kTearV, 1.0f, b, kPackWm, kPackHm, q, bodyAt, bodyScale, true, 0.008f, packShown);
    bend_grid(o + kBodyNV * 6, kPackNX, kBodyNY, kTearV, 1.0f, b, kPackWm, kPackHm, q, bodyAt, bodyScale, false, 0.008f,
              packShown);
    float* st = o + 2 * kBodyNV * 6;
    if (!S.torn) {
      bend_grid(st, kPackNX, kStripNY, 0.0f, kTearV, b, kPackWm, kPackHm, q, S.pack.pos, S.pack.scale, true, 0.0f, packShown);
      bend_grid(st + kStripNV * 6, kPackNX, kStripNY, 0.0f, kTearV, b, kPackWm, kPackHm, q, S.pack.pos, S.pack.scale, false, 0.0f,
                packShown);
    } else {
      // torn off: drawn about its own middle (its rows laid as if the pack's middle were where it puts them)
      const float sc = S.stripBase * std::max(0.01f, S.stripScale);
      const float stripY = (0.5f - 0.5f * kTearV) * kPackHm * sc;
      const XrVector3f at = v_sub(S.stripPos, q_rot(S.stripQ, XrVector3f{0.0f, stripY, 0.0f}));
      const bool stripShown = S.stripScale > 0.01f && S.mode == kPackUp;
      bend_grid(st, kPackNX, kStripNY, 0.0f, kTearV, 0.0f, kPackWm, kPackHm, S.stripQ, at, sc, true, 0.0f, stripShown);
      bend_grid(st + kStripNV * 6, kPackNX, kStripNY, 0.0f, kTearV, 0.0f, kPackWm, kPackHm, S.stripQ, at, sc, false, 0.0f, stripShown);
      shown = shown || stripShown;
    }
    xrfx::Scene::ModelDraw& md = scene.models[kPackSlot];
    md.on = true;
    md.posNrm = S.packVerts.data();
    md.white = 0.0f;
    md.alpha = 1.0f;
    md.glow = 0.0f;
    md.under = -2.0f;  // a card: paper, glossy, its foil (xr_fx_model.frag)
    md.glowRgb = 0xFFFFFF;
    md.flameFrame = -1;
    md.shine = 0.0f;
    md.frame = -1;
    shown = shown || packShown;
  }
  // r138: the special cards' 3D (a card drawn by its special is left out of the cards' model) and the cards' light
  bool hide[kSheets];
  slab_frame(scene, dt, now, fxReady);  // r152: a TIZM GRADING slab round its card (its reveal)
  specials_frame(scene, dt, now, fxReady, hide);
  if (S.cardModel >= 0 && xrfx::model_up(kCardSlot) &&
      S.cardVerts.size() == static_cast<size_t>(S.n) * (kCardVerts + kTagVerts + kPunchVerts) * 6) {
    float* o = S.cardVerts.data();
    for (int i = 0; i < S.n; ++i) {
      const Sheet& s = S.card[i];
      const bool show = s.on && s.shown && s.scale > 0.01f;
      const XrQuaternionf q = sheet_q(s);
      // (r217: a Jam card's crest; r220: its own die - punched from the crest as it's turned over: punch_frame)
      float* box = o + static_cast<size_t>(i) * kCardVerts * 6;
      card_box(box, s, q, s.pos, s.scale, show && !hide[i], kCardT, 1.0f, S.cutShape[i], punch_morph_of(i, now), jewel_code(S.refs[i]));  // r138: stiff, thick (r230: a jewel's front)
      punch_frame(o + (static_cast<size_t>(S.n) * (kCardVerts + kTagVerts) + static_cast<size_t>(i) * kPunchVerts) * 6, box, i, s, q, show && !hide[i], now);
      if (show) finish_frame(s, S.refs[i], q, std::cos(s.flip) < 0.0f, dt);
      level_moment(o + (static_cast<size_t>(S.n) * kCardVerts + static_cast<size_t>(i) * kTagVerts) * 6, i, show, q, now, dt);  // r147
      shown = shown || show;
    }
    xrfx::Scene::ModelDraw& md = scene.models[kCardSlot];
    md.on = true;
    md.posNrm = S.cardVerts.data();
    md.white = 0.0f;
    md.alpha = 1.0f;
    md.glow = 0.0f;
    md.under = -2.0f;
    md.glowRgb = 0xFFFFFF;
    md.flameFrame = -1;
    md.shine = 0.0f;
    md.frame = -1;
  }
  return shown;
}

}  // namespace xrcards

#endif  // __ANDROID__ && STRIKERS_XR
