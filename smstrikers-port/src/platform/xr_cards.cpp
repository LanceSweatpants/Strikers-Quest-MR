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
//  - its finish, any card (the parallels): BASE chrome; SILVER TIZM refractor 1 in 8 cards; GOLD TIZM numbered /10, 1 in
//    50 (ten of each card at most, each its own number); BLACK TIZM 1/1, 1 in 300 (one of each card, ever); and a Pokemon
//    card shiny on top, 1 in 128 (franchise.cpp kShinyOdds: an evolution's luck; its shiny model)
// tools\outside\cards_preview.py simulates 10,000 packs of these.
constexpr double kSlotSR = 0.04, kSlotR = 0.22, kLastSR = 0.18;
constexpr int kShinyOdds = 128;
constexpr int kSilverOdds = 8, kGoldOdds = 50, kBlackOdds = 300;
constexpr int kGoldRun = 10;  // GOLD TIZM: numbered /10
constexpr int kPackCards = 5;
constexpr int kSheets = 2 * kPackCards;  // r146: the cards in your room at once (a trade-in's ten)
constexpr int kArt = 1024;  // the big picture's size (xr_big)

enum { kCommon, kRare, kSuper };
enum { kBase, kSilver, kGoldTizm, kBlack };
const char* const kRarityName[3] = {"COMMON", "RARE", "SUPER RARE"};
const char* const kRarityShort[3] = {"C", "R", "SR"};
const char* const kParallelName[4] = {"BASE", "SILVER TIZM", "GOLD TIZM", "BLACK TIZM"};
const char* const kSeriesName[5] = {"STRIKERS", "CHARGED", "MELEE", "POKEMON", "MARVEL"};
constexpr uint32_t kSeriesRgb[5] = {0xE4202A, 0x23A04A, 0x4062C8, 0x2A5AB8, 0xB0101E};
constexpr uint32_t kSeriesInk[5] = {0xFFFFFF, 0xFFFFFF, 0xFFFFFF, 0xFFCB05, 0xFFFFFF};
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
};
// A card in a pack / out of the binder: which (its index in the set), its finish, shiny, its number in its run (GOLD
// 1-10, BLACK 1).
// r149 (Lance's CASE HITS, rare inserts on top of the finishes, their own odds): TIZM BOOM (a comic-book explosion popping
// out of the card), HOMETOWN (the card a window into their home turf), SIGNED (their autograph, writing itself on);
// never traded in. (Every BLACK 1/1 is signed too: its own, not a case hit.)
enum { kHitNone = 0, kHitBoom, kHitHome, kHitSigned };
constexpr double kHitOdds[4] = {0.0, 1.0 / 300.0, 1.0 / 350.0, 1.0 / 450.0};  // a card's chance of each
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
struct Owned {
  int n[4][2] = {};
  std::vector<int> golds;
  std::vector<Ref> hits;
  int total() const {
    int t = static_cast<int>(hits.size());
    for (const auto& p : n) t += p[0] + p[1];
    return t;
  }
  int best() const {  // the finest finish you have, -1 none
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
    return false;
  }
  bool blackAny() const { return best() == kBlack; }
  int hitBest() const {  // r149: its case hit to show (BOOM, then HOMETOWN, then SIGNED), 0 none
    int b = kHitNone;
    for (const Ref& h : hits)
      if (b == kHitNone || h.hit < b) b = h.hit;
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
int g_coins = 0, g_coinsEarned = 0, g_coinsSpent = 0, g_lastCoins = 0;
long long g_lastCoinsWhen = 0;
std::string g_lastCoinsWhy;
std::atomic<int> g_shopPick{0};
std::string g_shopSay;  // (the shop's last word: "NOT ENOUGH COINS" ...)
constexpr int kPackPrice = 50, kBoxPrice = 200, kBoxPacks = 5;
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
constexpr int kLevelCopies[5] = {1, 2, 3, 5, 10};
constexpr int kLevelXp = 10, kLineXp = 15, kTypeXp = 10, kXpCap = 60, kLineShinyOdds = 96, kPageCoins = 2, kPageCoinsCap = 20;
enum { kPageBinder = 0, kPageShop, kPageTrade, kPageSets };  // the CARDS tab's pages (LT: the next, RT: back)
std::atomic<int> g_page{kPageBinder};
std::unordered_set<std::string> g_keep;  // cards you keep out of trade-ins (X)
// r149 (Lance: it "took me farrrr too long to find that damn card i just got"): the cards packs gave you that you haven't
// looked at in the binder yet (the cursor resting on its pocket, or taking it out): NEW on their pockets
std::unordered_set<std::string> g_newCards;
// r149: GOD PACKs opened (every card SILVER or better / a case hit), the last one's time (STRIKERS ILLUSTRATED brags)
int g_godPacks = 0;
long long g_godWhen = 0;
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
  for (int i = 0; i < 5; ++i)
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
  std::lock_guard<std::mutex> lock(g_mutex);
  g_set = std::move(set);
  g_byKey.clear();
  for (size_t i = 0; i < g_set.size(); ++i) g_byKey[g_set[i].key] = static_cast<int>(i);
  g_owned.assign(g_set.size(), Owned{});
  int here = 0;
  for (const Card& c : g_set) here += c.here ? 1 : 0;
  CLOG("the set: %d cards, %d on this headset", static_cast<int>(g_set.size()), here);
  g_loaded = true;
}

// ------------------------------------------------------------------------------------------ the collection
// STRIKERS_HOME/cards/collection.txt, every career's (written whole: a temp file, then renamed over it). A card = its key
// + finish + shiny (+ its number for a GOLD / BLACK): what online trading will move.
std::string coll_path() { return home_dir() + "/cards/collection.txt"; }

// "<key>:<parallel>:<shiny>:<number>" (r149: ":<case hit>" after, when it's one)
bool parse_ref(const char* s, Ref* r) {
  char key[128];
  int par = 0, sh = 0, num = 0, hit = 0;
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
  return g_set[r.card].key + ":" + std::to_string(r.parallel) + ":" + (r.shiny ? "1" : "0") + ":" + std::to_string(r.serial) +
         (r.hit != kHitNone ? ":" + std::to_string(r.hit) : std::string());
}

void add_owned(const Ref& r, int delta) {  // (g_mutex held)
  if (r.card < 0) return;
  Owned& o = g_owned[r.card];
  if (r.hit != kHitNone) {  // r149: a case hit, kept whole
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
  if (r.parallel == kGoldTizm && r.serial > 0) {
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
      std::sscanf(line, "coins %d %d %d", &g_coins, &g_coinsEarned, &g_coinsSpent);
      g_coins = std::max(0, g_coins);
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
      if (parse_ref(b, &h) && h.hit != kHitNone) add_owned(h, +1);
    } else if (std::strncmp(line, "godpacks ", 9) == 0) {  // r149
      std::sscanf(line, "godpacks %d %lld", &g_godPacks, &g_godWhen);
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
  int owned = 0;
  for (const Owned& o : g_owned) owned += o.total() > 0 ? 1 : 0;
  CLOG("collection: %d different cards, %d packs waiting, %d opened", owned, static_cast<int>(g_packs.size()), g_opened);
}

void save_collection() {  // (g_mutex held)
  const std::string dir = home_dir() + "/cards";
  ::mkdir(dir.c_str(), 0775);
  std::string out =
      "# 2026-27 LANCINI TIZM STRIKERS (r137): your trading cards, kept across careers. Written by the game (xr_cards.cpp).\n"
      "#   card <key> <finish 0 base, 1 silver, 2 gold /10, 3 black 1/1> <shiny 0|1> <count> [gold numbers 3,7]\n"
      "#   pack <serial> <flags> <key>:<finish>:<shiny>:<number> x5   a pack waiting, rolled the moment it was earned\n"
      "#   pull <unix time> <key>:<finish>:<shiny>:<number>            a big pull (STRIKERS ILLUSTRATED's cover line)\n"
      "#   coins <balance> <ever earned> <ever spent>                  r140: TIZM COINS (franchise matches you play)\n"
      "#   lastcoins <coins> <unix time> <why>                          the last match's\n"
      "#   cameo <key>                                                  r141: a BLACK 1/1's life-size cameo, seen\n"
      "#   traded <cards> <packs>                                       r146: spare base cards traded in, packs got for them\n"
      "#   trade <unix time> <pack serial> <key>*<n>,...                r146: a trade-in (the newest 40)\n"
      "#   keep <key>                                                   r146: a card kept out of trade-ins\n"
      "#   new <key>                                                    r149: from a pack, not looked at in the binder yet\n"
      "#   hit <key>:<finish>:<shiny>:<number>:<hit 1 boom, 2 hometown, 3 signed>   r149: a case hit you have (its own copy)\n"
      "#   godpacks <opened> <unix time of the last>                   r149: GOD PACKs (all five SILVER+ / case hits)\n"
      "#   (a pack's / a pull's cards: <key>:<finish>:<shiny>:<number>[:<hit>])\n"
      "version 2\n";
  char buf[256];
  std::snprintf(buf, sizeof(buf), "salt %016llx\nserial %d\nopened %d\ncoins %d %d %d\n", static_cast<unsigned long long>(g_salt),
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
        out += "card " + g_set[i].key + " " + std::to_string(p) + " " + std::to_string(sh) + " " + std::to_string(n);
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
  const std::string path = coll_path(), tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) {
    CLOG("collection: can't write %s", tmp.c_str());
    return;
  }
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size() && std::fflush(f) == 0;
  ::fsync(fileno(f));
  std::fclose(f);
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) CLOG("collection: writing it failed");
}

// How big a pull is (the opening shows the biggest last): who it is, then its finish, shiny on top.
int pull_score(const Ref& r) {
  // r149 (Lance: "put the hits as the very last card you can see, IF ANY, and if you get 1 before the last card, that just
  // means you're about to have a crazy pack"): strict tiers - BASE < SILVER < GOLD < a case hit < BLACK 1/1 (the top) -, a
  // shiny above the rest of its tier, then who it is: the biggest is always the last card
  const int tier = r.parallel == kBlack ? 4 : (r.hit != kHitNone ? 3 : (r.parallel == kGoldTizm ? 2 : (r.parallel == kSilver ? 1 : 0)));
  return tier * 100 + (r.shiny ? 10 : 0) + (r.card >= 0 ? g_set[r.card].rarity : 0);
}
bool big_pull(const Ref& r) {
  return r.card >= 0 && (r.parallel >= kGoldTizm || r.shiny || r.hit != kHitNone || g_set[r.card].rarity == kSuper);
}

// A card's finish: BLACK 1/1 one of each card ever (one you have, or one waiting in a pack: a GOLD instead), GOLD /10 a
// number of the ten no copy of yours has (all ten taken: a SILVER).  (g_mutex held)
void roll_parallel(uint64_t& rng, Ref* r, const std::vector<Ref>& also) {
  const double x = rand01(rng);
  int par = x < 1.0 / kBlackOdds ? kBlack
          : x < 1.0 / kBlackOdds + 1.0 / kGoldOdds ? kGoldTizm
          : x < 1.0 / kBlackOdds + 1.0 / kGoldOdds + 1.0 / kSilverOdds ? kSilver : kBase;
  auto taken = [&](int parallel, int serial) {
    const Owned& o = g_owned[r->card];
    if (parallel == kBlack && (o.n[kBlack][0] + o.n[kBlack][1] > 0 || o.blackAny())) return true;  // (r149: a case hit's too)
    if (parallel == kGoldTizm && std::find(o.golds.begin(), o.golds.end(), serial) != o.golds.end()) return true;
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
// shiny 1 in 128; ordered for the opening (the biggest last).
// r149 (Lance: "a crazy, greek mythology ass "celebration" graphic that happens if you ever get a "god pack" in which ALL
// ... cards ... are silver or above!"): a GOD PACK - every card SILVER or better (or a case hit). By chance it would almost
// never happen (about 1 in 10,000 packs), so a pack is rolled as one 1 in kGodOdds (every card's finish at least SILVER);
// either way flagged kGodPack (its rip and its last card: the show)
constexpr int kGodOdds = 400;
constexpr unsigned kGodPack = 32;  // (a pack's flags)
bool god_cards(const Pack& p) {
  for (const Ref& r : p.cards)
    if (r.card >= 0 && r.parallel < kSilver && r.hit == kHitNone) return false;
  return true;
}
Pack roll_pack(uint64_t& rng, const std::vector<Ref>& earlier, bool god = false) {  // (g_mutex held)
  std::vector<int> pool[3];
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].here) pool[g_set[i].rarity].push_back(static_cast<int>(i));
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
    {  // r149: a case hit, on top of its finish (about 1 in 24 packs has one: tools/outside/cards149.py)
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

// "BLACK TIZM 1/1 CHARIZARD", "GOLD TIZM MEWTWO 07/10", "SHINY GENGAR", "SUPER RARE BLASTOISE", "MARIO"
std::string pull_text(const Ref& r) {
  const Card& c = g_set[r.card];
  std::string s = upper(c.name);
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
int spare_of(int card) {
  if (card < 0 || card >= static_cast<int>(g_set.size())) return 0;
  const Owned& o = g_owned[card];
  return std::max(0, std::min(o.n[kBase][0], o.total() - 1));
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
std::vector<std::pair<int, int>> trade_pick() {
  std::vector<std::pair<int, int>> left;  // (card, spares left)
  for (size_t i = 0; i < g_set.size(); ++i) {
    const int sp = spare_of(static_cast<int>(i));
    if (sp > 0 && g_keep.count(g_set[i].key) == 0) left.push_back({static_cast<int>(i), sp});
  }
  std::vector<std::pair<int, int>> out;
  int got = 0;
  for (; got < kTradeFor; ++got) {
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
  if (got < kTradeFor) out.clear();
  return out;
}
int spare_total(bool keptToo) {  // (g_mutex held)
  int n = 0;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (keptToo || g_keep.count(g_set[i].key) == 0) n += spare_of(static_cast<int>(i));
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
    bool here = true, pk = true, mua = true;
    for (int i : m) {
      here = here && g_set[i].here;
      pk = pk && pk_key(g_set[i].key);
      mua = mua && g_set[i].key.compare(0, 4, "mua_") == 0;
    }
    if (!here || (!pk && !mua)) continue;  // (a line you couldn't finish on this headset)
    std::sort(m.begin(), m.end(), [](int x, int y) { return g_set[x].num < g_set[y].num; });
    int first = m[0];
    for (int i : m)
      if (!evolved[i]) {
        first = i;
        break;
      }
    CardSet s;
    s.suits = mua;
    s.name = upper(g_set[first].name) + (mua ? " SUITS" : " LINE");
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
  const int lv = card_level(g_owned[card]);
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
ImgP card_art(const Card& c, bool shiny, float waitS) {
  const bool sh = shiny && pk_key(c.key);
  std::vector<uint8_t> px;
  xrbig::prefetch(c.key, sh, kArt);
  const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int>(waitS * 1000.0f));
  for (;;) {
    if (xrbig::get(c.key, sh, kArt, &px) && px.size() >= static_cast<size_t>(kArt) * kArt * 4) {
      auto img = std::make_shared<Img>();
      img->rgba = std::move(px);
      img->w = img->h = kArt;
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
  P.text(upper(c.name), 455, 1222, 92, 760, 0xFFFFFF, 6);
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
// mark big, the set, its number stamped. (No name: the backs show while a pack's cards wait to be turned over.)
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
  P.box(250, 1060, 830, 1260, 30, 0xE9EDF4, 1.0f, 0x9AA4B6);
  P.ring(250, 1060, 830, 1260, 30, 5, 0x111111);
  P.text("NO. " + num3(c.num), W / 2, 1130, 92, 540, 0x111111);
  P.text("OF " + std::to_string(total), W / 2, 1212, 48, 400, 0x333A48);
  P.text("A STRIKERS SUPER LEAGUE COLLECTION", W / 2, 1380, 40, W - 160, 0x8A94AA, 2);
}

// r149: a TIZM BOOM's words by who it is (its world's theme: fire FWOOSH!, electric KRAKA-TOOM! ...), and a HOMETOWN's
// home (its suite / evolution room's picture, STRIKERS_HOME/suites/<name>_outside.jpg) and its place's name (below)
void boom_words(const std::string& key, std::string out[3]);
std::string home_backdrop(const Card& c, std::string* place);
void card_stats(Pc& P, const Card& c, float x0, float y0, float x1, float y1, float px);

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
  const uint32_t yel = 0xFFD21F, red = 0xFF3B30, ink = 0x0B0B12;
  (void)flat;
  // r150 (Lance: "make it so those animations are not on the card, yet pop out from the card and disappear after the
  // animation happens"): its face clean - a pop-art frame (yellow, halftone, ink) round a clean panel, the character in
  // it, a small TIZM BOOM! badge; its bursts and words are 3D only now, popping out of it (special_moves)
  P.box(0, 0, W, H, 48, r.parallel == kBase ? ink : st.s1, 1.0f, r.parallel == kBase ? ink : st.s2);  // (a parallel's metal frame)
  P.box(22, 22, W - 22, H - 22, 40, ink);
  P.box(32, 32, W - 32, H - 32, 34, yel, 1.0f, 0xFFB000);
  P.dots(32, 32, W - 32, H - 32, 30, 0.06f, 0.34f, 0xFF6A00, 0.55f);
  P.box(kHoleX0 - 12, kHoleY0 - 12, kHoleX1 + 12, kHoleY1 + 12, 40, ink);
  P.box(kHoleX0, kHoleY0, kHoleX1, kHoleY1, 30, 0xDDF6FF, 1.0f, 0x6FCFF5);
  P.glow(kHoleX0 + 120, kHoleY0 + 120, kHoleX1 - 120, kHoleY1 - 60, 0xFFFFFF, 0.55f);
  P.dots(kHoleX0, kHoleY0, kHoleX1, kHoleY1, 26, 0.02f, 0.14f, 0x1A8FD8, 0.22f);
  card_art_piece(P, art, false);
  // TIZM BOOM!, small, in its bubble
  P.burst(30, 26, 400, 150, 16, 0.8f, 0.0f, ink);
  P.burst(40, 34, 390, 142, 16, 0.8f, 0.0f, 0xFFFFFF);
  P.style(0.16f, 0.92f, -4.0f, 4, ink, 0.9f);
  P.rim(ink);
  P.text("TIZM BOOM!", 215, 88, 52, 300, red, 5, 0xFFFFFF);
  P.plain();
  // its rating, series, finish, level
  P.box(bx - 92, by - 92, bx + 92, by + 92, 92, ink);
  P.box(bx - 82, by - 82, bx + 82, by + 82, 82, 0xFFFFFF);
  P.text(std::to_string(c.rating), bx, by - 14, 88, 140, red, 3);
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
  // its name, comic style; its emblem; its stats; the set
  P.poly({18, 1150, W - 18, 1112, W - 18, 1286, 18, 1318}, ink);
  P.poly({30, 1160, W - 30, 1124, W - 30, 1272, 30, 1304}, red);
  P.style(0.16f, 0.92f, -1.9f, 6, ink, 0.9f);
  P.rim(ink);
  P.text(upper(c.name), 455, 1218, 92, 760, 0xFFFFFF, 7);
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
  P.text("IN EVERY PACK", W / 2, 432, 50, W - 200, kGold, 3);
  const double srPerPack = (kPackCards - 1) * kSlotSR + kLastSR;
  const std::string rows[7][2] = {{"CARDS", "5"},
                                  {"RARE OR BETTER", "EVERY PACK"},
                                  {"SUPER RARE", "ABOUT 1 IN " + std::to_string(static_cast<int>(std::lround(1.0 / srPerPack))) + " PACKS"},
                                  {"SILVER TIZM", "1 IN " + std::to_string(kSilverOdds) + " CARDS"},
                                  {"GOLD TIZM /10", "1 IN " + std::to_string(kGoldOdds) + " CARDS"},
                                  {"BLACK TIZM 1/1", "1 IN " + std::to_string(kBlackOdds) + " CARDS"},
                                  {"SHINY POKEMON", "1 IN " + std::to_string(kShinyOdds)}};
  for (int i = 0; i < 7; ++i) {
    const float y = 510 + i * 78.0f;
    P.left(rows[i][0], 100, y, 40, 400, 0xE8ECF5, 2);
    P.right(rows[i][1], W - 100, y, 40, 400, i >= 3 ? 0xFFF2A0 : 0xFFFFFF, 2);
    if (i < 6) P.box(100, y + 36, W - 100, y + 40, 2, 0x2A3354);
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
void bake_signature(std::vector<uint8_t>& px, int w, int h, const Card& c);  // (below)
#endif

std::vector<uint8_t> draw_front(const Card& c, const Ref& r, int total, float waitS, int special = kSpNone, int level = 0) {
  // r147 (Lance, his GOLD Scyther: "they need a static image of the character on the card AS WELL AS the 3D body on top of the
  // card"): a GOLD's pop-out front keeps its picture too (its figure still stands on its top edge)
  const bool flat = special == kSpNone;
  // (r150: a TIZM BOOM's character printed on its clean face on show too - only its bursts and words pop out)
  const ImgP art = flat || special == kSpPop || special == kSpSign || special == kSpBoom ? card_art(c, r.shiny, waitS) : ImgP(),
             badge = emblem(c);
  Pc P, M;
  if (r.hit == kHitHome) {  // r149: HOMETOWN - drawn landscape, turned onto the card
    std::string place;
    const std::string file = home_backdrop(c, &place);
    const ImgP home = flat ? home_picture(file) : ImgP();
    home_front(P, c, r, art, badge, home, place, total, false, !flat, level);
    home_front(M, c, r, art, badge, home, place, total, true, !flat, level);
    return turn_ccw(render(kHomeW, kHomeH, P.v, &M.v), kHomeW, kHomeH);
  }
  std::vector<uint8_t> px;
  if (r.hit == kHitBoom) {  // r149: TIZM BOOM
    boom_front(P, c, r, art, badge, total, false, flat, level);
    boom_front(M, c, r, art, badge, total, true, flat, level);
    px = render(kFrontW, kFrontH, P.v, r.parallel != kBase ? &M.v : nullptr);
  } else {
    card_front(P, c, r, art, badge, total, false, special, level);
    card_front(M, c, r, art, badge, total, true, special, level);
    // r138: BASE no foil at all (a window still cut through a shiny's); r149: a SIGNED's sticker is foil
    const bool masked = r.parallel != kBase || special == kSpWindow || special == kSpGlass || r.hit == kHitSigned;
    px = render(kFrontW, kFrontH, P.v, masked ? &M.v : nullptr);
  }
#if __has_include("xr_sign.h")
  // r149: a SIGNED card / every BLACK 1/1, flat: its autograph on it (on show, its own writes itself on: special_up)
  if (flat && (r.hit == kHitSigned || r.parallel == kBlack)) bake_signature(px, kFrontW, kFrontH, c);
#endif
  return px;
}
std::vector<uint8_t> draw_back(const Card& c, int total) {  // (r147: the plain back, always)
  Pc P;
  P.k = static_cast<float>(kBackW) / kCW;
  card_back(P, c, total);
  return render(kBackW, kBackH, P.v, nullptr);
}
std::vector<uint8_t> draw_tag(int level, int copies, bool up) {  // r147
  Pc P;
  level_tag(P, level, copies, up);
  return render(kTagW, kTagH, P.v, nullptr);
}
std::vector<uint8_t> draw_pack(bool back, int total) {
  ImgP stars[3] = {portrait("strikers_mario"), portrait("pk_charizard"), portrait("mua_spiderman")};
  if (stars[1]->empty()) stars[1] = portrait("strikers_peach");
  if (stars[2]->empty()) stars[2] = portrait("strikers_wario");
  Pc P, M;
  if (back) {
    pack_back(P, total, false);
    pack_back(M, total, true);
  } else {
    pack_front(P, stars, false);
    pack_front(M, stars, true);
  }
  return render(kPackW, kPackH, P.v, &M.v);
}

// ------------------------------------------------------------------------------------- the binder (the hub)
// The hub's middle, 2160 x 1300 (y 190 .. 1190): an open binder (two pages of 3 x 3 pockets) at the left, its pages
// turned with LT / RT; your set progress, your packs and the card under the cursor at the right.
constexpr float kPocketW = 196, kPocketH = 274, kPocketGap = 20, kPageMargin = 28;
constexpr float kBinderY = 205, kLeftPageX = 50, kRightPageX = 760;
constexpr float kPageW = 2 * kPageMargin + 3 * kPocketW + 2 * kPocketGap;  // 684
constexpr float kPageH = 2 * kPageMargin + 3 * kPocketH + 2 * kPocketGap;  // 918
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
  const xrsign::Signature& s = xrsign::get(c.key, c.name);
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
void mini_card(Pc& P, const Card& c, const Owned& o, float x0, float y0, float x1, float y1, const ImgP& pic, bool baseOnly = false,
               bool isNew = false) {
  const float w = x1 - x0, h = y1 - y0, cx = 0.5f * (x0 + x1), cy = 0.5f * (y0 + y1);
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
      P.box(x0 + 5, y0 + 5, x1 - 5, y1 - 5, 10, 0xFFE14A, 1.0f, 0xFFB000);
      P.dots(x0 + 5, y0 + 5, x1 - 5, y1 - 5, 13, 0.08f, 0.42f, 0xFF6A00, 0.55f);
      P.box(x0 + 13, y0 + 27, x1 - 13, y1 - 61, 9, 0x111111);
      P.box(x0 + 17, y0 + 31, x1 - 17, y1 - 65, 7, 0xDDF6FF, 1.0f, 0x6FCFF5);
      P.glow(x0 + 30, y0 + 50, x1 - 30, y1 - 80, 0xFFFFFF, 0.5f);
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
    P.text(upper(c.name), cx, y1 - 33, 30, w - 24, 0xFFFFFF, 2);
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

// A small pack (the side panel's stack, the opening page): chrome, crimped, the gem and TIZM.
void mini_pack(Pc& P, float x0, float y0, float x1, float y1, bool plainBack = false) {
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

// r140: a TIZM COIN: chrome round the brand's gem, at (cx, cy), r its radius
void coin_icon(Pc& P, float cx, float cy, float r) {
  P.box(cx - r, cy - r, cx + r, cy + r, r, 0xF4F6FA, 1.0f, 0x7C8698);  // its chrome rim
  const float f = 0.84f * r;
  P.box(cx - f, cy - f, cx + f, cy + f, f, 0x2A3354, 1.0f, 0x0B1022);  // its face
  static const uint32_t kFacet[6] = {0xFFFFFF, 0xD9E0EE, 0x8E9BB4, 0x5A6682, 0xA6B3CC, 0xEEF2FA};
  const float g = 0.58f * r;
  for (int i = 0; i < 6; ++i) {
    const float a0 = -1.5707963f + 1.0471976f * i, a1 = a0 + 1.0471976f;
    P.poly({cx, cy, cx + g * std::cos(a0), cy + g * std::sin(a0), cx + g * std::cos(a1), cy + g * std::sin(a1)}, kFacet[i]);
  }
}

struct BinderView {
  std::vector<int> list;
  std::vector<Card> cards;  // the spread's
  std::vector<Owned> owned;
  std::vector<bool> isNew;  // r149: NEW (a pack's, not looked at yet)
  int spread = 0, spreads = 1, cursor = 0, total = 0, here = 0, have = 0, packs = 0, mode = 0;
  int haveTier[3] = {0, 0, 0}, hereTier[3] = {0, 0, 0}, shinies = 0, finishes[4] = {0, 0, 0, 0};
  int hits[4] = {0, 0, 0, 0}, godPacks = 0;  // r149: case hits by kind, GOD PACKs opened
  int coins = 0, lastCoins = 0, pick = 0;  // r140
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
  const float x0 = kLeftPageX - 18, x1 = kRightPageX + kPageW + 18, y0 = kBinderY - 14, cx = 0.5f * (x0 + x1);
  P.box(x0, y0, x1, kBinderY + kPageH + 14, 26, 0x1A2140, 1.0f, 0x0A0E1C);
  P.text("TRADE-INS", cx, y0 + 62, 70, 900, kGold, 5);
  P.text("10 SPARE BASE CARDS = 1 FREE LANCINI TIZM PACK  -  THE SAME PACKS, THE SAME ODDS", cx, y0 + 116, 30, 1300, 0xB8C2D6, 2);
  P.right(std::to_string(b.spareFree), cx - 30, y0 + 182, 76, 300, 0xFFFFFF, 5);
  P.left(b.spareFree == 1 ? "SPARE BASE CARD" : "SPARE BASE CARDS", cx - 10, y0 + 172, 40, 520, 0xE8ECF5, 3);
  const int packs = b.spareFree / kTradeFor;
  P.left(packs > 0 ? "= " + std::to_string(packs) + (packs == 1 ? " PACK" : " PACKS") : "10 MAKE A PACK", cx - 10, y0 + 210, 30, 520,
         packs > 0 ? kGold : 0x8A94AA, 2);
  if (b.spareAll > b.spareFree)
    P.right(std::to_string(b.spareAll - b.spareFree) + " KEPT", x1 - 50, y0 + 196, 30, 300, 0xFF9A8A, 2);
  if (b.tiles.empty()) {
    P.box(x0 + 60, y0 + 240, x1 - 60, y0 + 740, 26, 0x121830);
    P.text("NO SPARE BASE CARDS YET", cx, y0 + 440, 56, 1200, 0x8A94AA, 3);
    P.text("A CARD YOU HAVE MORE THAN ONE OF (PLAIN BASE COPIES, ANY RARITY) SHOWS UP HERE", cx, y0 + 520, 30, 1200, 0xB8C2D6, 2);
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
    if (i == b.tradeCursor) P.ring(tx0 - 10, ty0 - 10, tx1 + 10, ty1 + 10, 22, 8, kGold);
    // under it: its spares; the next trade's take and its level after
    // (r149: what's traded said plainly - its BASE copies to spare; a level it'd lose on the tile)
    P.text("BASE X" + std::to_string(t.spare) + " SPARE", tcx, ty1 + 22, 26, kTileW + 16, 0xE8ECF5, 2);
    if (t.next > 0 && !t.kept) {
      Owned after = t.o;
      after.n[kBase][0] -= t.next;
      const int lv0 = card_level(t.o), lv1 = card_level(after);
      if (lv1 < lv0) {
        const std::string lt = "LV" + std::to_string(lv0) + " TO LV" + std::to_string(lv1);
        const float lw = P.width(lt, 24) + 20;
        P.box(tcx - lw / 2, ty1 - 96, tcx + lw / 2, ty1 - 66, 15, 0xFF8A3A);
        P.text(lt, tcx, ty1 - 81, 24, lw - 8, 0x1A0A00);
      }
    }
  }
  const int rows = (static_cast<int>(b.tiles.size()) + kTileCols - 1) / kTileCols;
  if (rows > kTileRows)
    P.right("ROWS " + std::to_string(b.tradeTop + 1) + "-" + std::to_string(std::min(rows, b.tradeTop + kTileRows)) + " OF " +
                std::to_string(rows),
            x1 - 50, y0 + 822, 24, 300, 0x8A94AA, 2);
  // the slot: A trades the next 10 for a pack
  const float sy = y0 + 784;
  const bool can = b.spareFree >= kTradeFor;
  P.box(cx - 600, sy, cx + 260, sy + 76, 38, can ? 0x24305C : 0x151C36);
  P.ring(cx - 600, sy, cx + 260, sy + 76, 38, 5, can ? kGold : 0x3A4A70);
  P.box(cx - 540, sy + 34, cx - 360, sy + 42, 4, 0x05070C);  // (its slit)
  P.text(can ? "A: TRADE 10 FOR 1 PACK" : "NEED " + std::to_string(kTradeFor - b.spareFree) + " MORE SPARE BASE CARDS", cx - 100, sy + 38,
         40, 640, can ? 0xFFFFFF : 0x8A94AA, 3);
  mini_pack(P, cx + 300, sy - 16, cx + 376, sy + 92);
  P.text(!b.tradeSay.empty() ? b.tradeSay
                             : "TRADED SO FAR: " + std::to_string(b.tradedPacks) + (b.tradedPacks == 1 ? " PACK (" : " PACKS (") +
                                   std::to_string(b.tradedCards) + " CARDS)",
         cx, y0 + 890, 32, 1300, !b.tradeSay.empty() ? kGold : 0xE8ECF5, 2);
  P.text("X: KEEP A CARD OUT / PUT IT BACK  -  ONLY PLAIN BASE COPIES (COMMONS FIRST); NEVER A CARD'S LAST COPY  -  THE COPIES YOU KEEP LEVEL IT UP",
         cx, y0 + 926, 24, 1360, 0x8A94AA, 2);
}

// r146: SETS (LT from the trade-ins): the evolution lines (done first, then the nearest; 9 at a time, the stick for more),
// the 18 Pokemon types, the binder's pages; a done one glowing, its bonus
void sets_page(Pc& P, const BinderView& b) {
  const float x0 = kLeftPageX - 18, x1 = kRightPageX + kPageW + 18, y0 = kBinderY - 14, cx = 0.5f * (x0 + x1);
  P.box(x0, y0, x1, kBinderY + kPageH + 14, 26, 0x1A2140, 1.0f, 0x0A0E1C);
  P.text("SETS", cx, y0 + 62, 70, 900, kGold, 5);
  P.text("FINISH A SET FOR ITS BONUS  -  FRANCHISE ONLY (NEVER QUICK MATCHES OR ONLINE)  -  A CHARACTER'S XP BONUSES AT MOST +" +
             std::to_string(kXpCap) + "%",
         cx, y0 + 116, 26, 1340, 0xB8C2D6, 2);
  // the evolution lines
  const float lx0 = x0 + 30, lx1 = x0 + 720;
  P.left("EVOLUTION LINES", lx0, y0 + 170, 38, 420, 0xFFFFFF, 3);
  P.right(std::to_string(b.linesDone) + " / " + std::to_string(b.lines.size()) + " DONE", lx1, y0 + 170, 30, 260, kGold, 2);
  const std::string lineBonus = "+" + std::to_string(kLineXp) + "% XP", lineShiny = "SHINY 1 IN " + std::to_string(kLineShinyOdds);
  P.left(lineBonus + " FOR THE LINE, ITS EVOLUTIONS " + lineShiny + " (NOT " + std::to_string(kShinyOdds) + ")", lx0, y0 + 206, 22,
         lx1 - lx0, 0xB8C2D6, 2);
  for (int k = 0; k < 9; ++k) {
    const size_t i = static_cast<size_t>(b.setsTop + k);
    if (i >= b.lines.size()) break;
    const BinderView::SetRow& r = b.lines[i];
    const float ry = y0 + 230 + k * 74.0f;
    if (r.done) P.glow(lx0 - 16, ry - 12, lx1 + 16, ry + 78, kGold, 0.35f);
    P.box(lx0, ry, lx1, ry + 66, 18, r.done ? 0x2B3A70 : 0x121830);
    if (r.done) P.ring(lx0, ry, lx1, ry + 66, 18, 4, kGold);
    for (size_t m = 0; m < r.who.size(); ++m) {
      const float pcx = lx0 + 40 + m * 54.0f, pcy = ry + 33;
      P.box(pcx - 26, pcy - 26, pcx + 26, pcy + 26, 26, r.who[m].second ? light(r.rgb, 0.2f) : 0x0A0E1C);
      P.centred(portrait(r.who[m].first), pcx - 24, pcy - 24, pcx + 24, pcy + 24, 0.95f, r.who[m].second ? 0 : 0x232B44);
    }
    const float tx = lx0 + 26 + r.who.size() * 54.0f;
    P.left(r.name, tx, ry + 24, 28, lx1 - tx - 90, 0xFFFFFF, 2);
    P.left(r.done ? (r.suits ? lineBonus : lineBonus + "  -  " + lineShiny) : std::to_string(r.total - r.have) + " TO GO", tx, ry + 50,
           22, lx1 - tx - 90, r.done ? kGold : 0x8A94AA, 2);
    P.right(std::to_string(r.have) + "/" + std::to_string(r.total), lx1 - 16, ry + 34, 34, 90, r.done ? kGold : 0xE8ECF5, 2);
  }
  if (b.lines.size() > 9)
    P.text("STICK: MORE  (" + std::to_string(b.setsTop + 1) + "-" + std::to_string(std::min<size_t>(b.lines.size(), b.setsTop + 9)) +
               " OF " + std::to_string(b.lines.size()) + ")",
           0.5f * (lx0 + lx1), y0 + 914, 24, lx1 - lx0, 0x8A94AA, 2);
  // the types
  const float rx0 = x0 + 760, rx1 = x1 - 30;
  P.left("POKEMON TYPES", rx0, y0 + 170, 38, 420, 0xFFFFFF, 3);
  P.right(std::to_string(b.typesDone) + " / " + std::to_string(b.types.size()) + " DONE", rx1, y0 + 170, 30, 260, kGold, 2);
  P.left("+" + std::to_string(kTypeXp) + "% XP FOR EVERY POKEMON OF THE TYPE", rx0, y0 + 206, 22, rx1 - rx0, 0xB8C2D6, 2);
  const float cw = (rx1 - rx0 - 20) / 3.0f;
  for (size_t i = 0; i < b.types.size(); ++i) {
    const BinderView::SetRow& r = b.types[i];
    const float tx0 = rx0 + (i % 3) * (cw + 10), ty0 = y0 + 230 + (i / 3) * 64.0f, tx1 = tx0 + cw, ty1 = ty0 + 58;
    if (r.done) P.glow(tx0 - 12, ty0 - 10, tx1 + 12, ty1 + 10, r.rgb, 0.5f);
    P.box(tx0, ty0, tx1, ty1, 14, r.done ? dark(r.rgb, 0.15f) : 0x121830);
    P.box(tx0 + 6, ty0 + 6, tx0 + 18, ty1 - 6, 6, r.rgb);
    if (r.done) P.ring(tx0, ty0, tx1, ty1, 14, 3, 0xFFFFFF);
    P.left(r.name, tx0 + 28, ty0 + 22, 24, cw - 110, r.done ? 0xFFFFFF : 0xE8ECF5, 2);
    P.right(std::to_string(r.have) + "/" + std::to_string(r.total), tx1 - 10, ty0 + 22, 22, 90, r.done ? 0xFFFFFF : 0xB8C2D6, 2);
    P.box(tx0 + 28, ty0 + 40, tx1 - 12, ty0 + 48, 4, 0x05070C);
    if (r.total > 0 && r.have > 0)
      P.box(tx0 + 28, ty0 + 40, tx0 + 28 + (cw - 40) * std::min(1.0f, static_cast<float>(r.have) / r.total), ty0 + 48, 4,
            r.done ? 0xFFFFFF : r.rgb);
  }
  // the binder's pages
  const float py = y0 + 640;
  P.left("BINDER PAGES", rx0, py, 38, 420, 0xFFFFFF, 3);
  P.right(std::to_string(b.pagesFull) + " / " + std::to_string(b.pageFill.size()) + " FULL", rx1, py, 30, 260, kGold, 2);
  char mul[32];
  std::snprintf(mul, sizeof(mul), "%d.%02d", 1 + b.coinPct / 100, b.coinPct % 100);
  P.left("+" + std::to_string(kPageCoins) + "% TIZM COINS A FULL PAGE (AT MOST +" + std::to_string(kPageCoinsCap) + "%): NOW X" + mul,
         rx0, py + 34, 22, rx1 - rx0, b.coinPct > 0 ? kGold : 0xB8C2D6, 2);
  for (size_t p = 0; p < b.pageFill.size() && p < 65; ++p) {
    const float px0 = rx0 + (p % 13) * 49.0f, py0 = py + 56 + (p / 13) * 45.0f;
    const bool full = b.pageFill[p] >= 0.999f;
    if (full) P.glow(px0 - 6, py0 - 6, px0 + 50, py0 + 46, kGold, 0.4f);
    P.box(px0, py0, px0 + 44, py0 + 40, 8, full ? kGold : 0x121830);
    if (!full && b.pageFill[p] > 0.0f) P.box(px0 + 4, py0 + 32, px0 + 4 + 36 * b.pageFill[p], py0 + 36, 2, 0x6A7A9A);
    P.text(std::to_string(p + 1), px0 + 22, py0 + 18, 20, 40, full ? kNavy : 0x8A94AA, 0);
  }
}

}  // namespace

HubPage hub_page(float w, float h, float top, float bottom) {
  (void)w;
  (void)h;
  (void)top;
  (void)bottom;
  HubPage out;
  if (!g_loaded.load()) return out;
  BinderView b;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    b.list = binder_list();
    b.spreads = std::max(1, static_cast<int>((b.list.size() + kPerSpread - 1) / kPerSpread));
    g_spread = std::max(0, std::min(b.spreads - 1, g_spread));
    b.spread = g_spread;
    b.cursor = g_cursor;
    b.total = static_cast<int>(g_set.size());
    b.packs = static_cast<int>(g_packs.size());
    b.coins = g_coins;  // r140
    b.lastCoins = g_lastCoins;
    b.lastWhen = g_lastCoinsWhen;
    b.lastWhy = g_lastCoinsWhy;
    b.say = g_shopSay;
    b.page = g_page.load();  // r146
    b.shop = b.page == kPageShop;
    b.pick = g_shopPick.load();
    build_sets();
    if (b.page == kPageTrade) {  // r146: your spare base cards (r147: commons first), the most first
      std::vector<std::pair<int, int>> next = trade_pick();
      for (size_t i = 0; i < g_set.size(); ++i) {
        const int sp = spare_of(static_cast<int>(i));
        if (sp <= 0) continue;
        BinderView::Tile t;
        t.c = g_set[i];
        t.o = g_owned[i];
        t.spare = sp;
        t.kept = g_keep.count(g_set[i].key) > 0;
        for (const auto& nx : next)
          if (nx.first == static_cast<int>(i)) t.next = nx.second;
        b.tiles.push_back(t);
        b.spareAll += sp;
        b.spareFree += t.kept ? 0 : sp;
      }
      std::stable_sort(b.tiles.begin(), b.tiles.end(), [](const BinderView::Tile& x, const BinderView::Tile& y) {
        return trade_before(g_byKey[x.c.key], x.spare, g_byKey[y.c.key], y.spare);  // (r147)
      });
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
        b.curXp = card_bonus(g_byKey[t.c.key], &b.curWhy, nullptr);
      }
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
      for (size_t i = 0; i < g_set.size(); ++i) b.bonusChars += card_bonus(static_cast<int>(i), nullptr, nullptr) > 0 ? 1 : 0;
      g_setsTop = std::max(0, std::min(std::max(0, static_cast<int>(b.lines.size()) - 9), g_setsTop));
      b.setsTop = g_setsTop;
    }
    for (size_t i = 0; i < g_set.size(); ++i) {
      const Owned& o = g_owned[i];
      if (g_set[i].here) {
        ++b.here;
        ++b.hereTier[g_set[i].rarity];
      }
      if (o.total() > 0) {
        ++b.have;
        ++b.haveTier[g_set[i].rarity];
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
      b.isNew.push_back(g_newCards.count(g_set[b.list[at]].key) != 0);  // r149
    }
    if (b.page != kPageTrade && b.cursor < static_cast<int>(b.cards.size())) {  // r146: its level, its bonuses
      const int ci = b.list[static_cast<size_t>(b.spread) * kPerSpread + b.cursor];
      b.curLevel = card_level(g_owned[ci]);
      b.curXp = card_bonus(ci, &b.curWhy, nullptr);
    }
  }
  b.mode = g_sceneMode.load();
  Pc P;
  if (b.mode == 1) {
    // a pack open in your hands: no binder behind it (it would give the cards away)
    P.box(40, 200, 2120, 1150, 30, 0x151C36);
    mini_pack(P, 760, 280, 1080, 770);
    P.text("OPENING A PACK", 1480, 420, 84, 760, kGold, 5);
    P.text("HOLD IT IN ONE HAND", 1480, 540, 44, 900, 0xFFFFFF, 2);
    P.text("THE OTHER HAND'S TRIGGER RIPS IT (OR A)", 1480, 600, 40, 900, 0xB8C2D6, 2);
    P.text("A TRIGGER TURNS THE FRONT CARD OVER, THE NEXT PUTS IT TO THE BACK", 1080, 880, 48, 1960, 0xFFFFFF, 3);
    P.text("ONE CARD AT A TIME, THE BIGGEST PULL LAST  -  BOTH GRIPS TURN AND SIZE IT", 1080, 960, 40, 1900, 0xB8C2D6, 2);
    // r149
    P.text("CASE HITS: TIZM BOOM, HOMETOWN, SIGNED  -  ONE BEFORE THE LAST CARD? A CRAZY PACK  -  ALL FIVE SILVER+: GOD PACK", 1080,
           1012, 30, 1960, kGold, 2);
    P.text(kSetName, 1080, 1070, 36, 1900, 0x8A94B8, 2);
    out.hints = {{"TRIGGER", "RIP / NEXT CARD", 0x2EA043}, {"A", "SAME / NEXT PACK", 0x3A4A70}, {"B", "INTO THE BINDER", 0xC0392B}};
  } else if (b.mode == 3) {
    // r146: a trade-in going on in front of you (its cards into the slot, its pack out)
    P.box(40, 200, 2120, 1150, 30, 0x151C36);
    for (int i = 4; i >= 0; --i)
      mini_pack(P, 600 + i * 66, 330 - (i % 2) * 16, 750 + i * 66, 556 - (i % 2) * 16, i > 0);
    P.text("TRADE-IN", 1480, 420, 84, 760, kGold, 5);
    P.text("10 SPARE BASE CARDS INTO THE SLOT", 1480, 540, 44, 900, 0xFFFFFF, 2);
    P.text("A FREE PACK OUT  -  IT WAITS WITH YOUR PACKS", 1480, 600, 40, 900, 0xB8C2D6, 2);
    P.text(std::to_string(b.packs) + (b.packs == 1 ? " PACK WAITING" : " PACKS WAITING"), 1080, 880, 52, 1960, kGold, 3);
    P.text(kSetName, 1080, 1060, 40, 1900, 0x8A94B8, 2);
    out.hints = {{"A", "DONE", 0x2EA043}, {"B", "SKIP", 0xC0392B}};
  } else {
   if (b.page == kPageTrade) {
    trade_page(P, b);  // r146
   } else if (b.page == kPageSets) {
    sets_page(P, b);  // r146
   } else if (b.shop) {
    // r140: the CARD SHOP (instead of the binder's pages): your TIZM COINS, a pack or a box of five, the last match's coins
    const float x0 = kLeftPageX - 18, x1 = kRightPageX + kPageW + 18, y0 = kBinderY - 14, cx = 0.5f * (x0 + x1);
    P.box(x0, y0, x1, kBinderY + kPageH + 14, 26, 0x1A2140, 1.0f, 0x0A0E1C);
    P.text("THE CARD SHOP", cx, y0 + 74, 76, 900, kGold, 5);
    P.text("TIZM COINS BUY LANCINI TIZM PACKS  -  THE SAME PACKS, THE SAME ODDS", cx, y0 + 138, 32, 1260, 0xB8C2D6, 2);
    coin_icon(P, cx - 250, y0 + 222, 46);
    P.left(std::to_string(b.coins), cx - 186, y0 + 222, 84, 360, 0xFFFFFF, 5);
    P.left("TIZM COINS", cx + 30, y0 + 230, 44, 360, 0xE8ECF5, 3);
    for (int k = 0; k < 2; ++k) {
      const float ox0 = k == 0 ? x0 + 60 : cx + 30, ox1 = k == 0 ? cx - 30 : x1 - 60, oy0 = y0 + 300, oy1 = y0 + 790;
      const float ocx = 0.5f * (ox0 + ox1);
      const int price = k == 0 ? kPackPrice : kBoxPrice;
      const bool can = b.coins >= price;
      P.box(ox0, oy0, ox1, oy1, 28, k == b.pick ? 0x24305C : 0x151C36);
      if (k == b.pick) P.ring(ox0 - 8, oy0 - 8, ox1 + 8, oy1 + 8, 34, 8, kGold);
      P.text(k == 0 ? "1 PACK" : "BOX OF 5 PACKS", ocx, oy0 + 56, 52, ox1 - ox0 - 40, 0xFFFFFF, 4);
      if (k == 0) {
        mini_pack(P, ocx - 88, oy0 + 92, ocx + 88, oy0 + 356);
      } else {
        for (int i = 4; i >= 0; --i)
          mini_pack(P, ocx - 220 + i * 66, oy0 + 112 - (i % 2) * 16, ocx - 70 + i * 66, oy0 + 338 - (i % 2) * 16, i > 0);
      }
      coin_icon(P, ocx - 90, oy0 + 404, 30);
      P.left(std::to_string(price), ocx - 48, oy0 + 404, 60, 260, can ? kGold : 0xFF6A5A, 4);
      P.text(!can ? "NEED " + std::to_string(price - b.coins) + " MORE" : (k == 1 ? "40 A PACK  -  THE BEST BUY" : "5 CARDS"),
             ocx, oy1 - 40, 30, ox1 - ox0 - 40, !can ? 0xFF8A7A : 0xB8C2D6, 2);
    }
    if (!b.say.empty()) P.text(b.say, cx, y0 + 846, 38, 1300, kGold, 3);
    if (b.lastCoins > 0) {
      // r146: its multipliers (full binder pages, your players' card XP) on a gold line of their own
      std::string why = upper(b.lastWhy), bonus;
      size_t cut = why.find(", BINDER PAGES");
      if (cut == std::string::npos) cut = why.find("; CARD XP");
      if (cut != std::string::npos) {
        bonus = why.substr(cut + 2);
        why = why.substr(0, cut);
        for (size_t at = bonus.find("; "); at != std::string::npos; at = bonus.find("; ")) bonus.replace(at, 2, "  -  ");
      }
      P.text("LAST MATCH: +" + std::to_string(b.lastCoins) + " TIZM COINS  -  " + why, cx, y0 + (bonus.empty() ? 900 : 884), 30, 1320,
             0xE8ECF5, 2);
      if (!bonus.empty()) P.text(bonus, cx, y0 + 922, 30, 1320, kGold, 2);
    } else
      P.text("EARN THEM IN FRANCHISE MATCHES YOU PLAY: GOALS, SUPER STRIKES, ASSISTS, TACKLES, STEALS, ITEM HITS, WINS", cx,
             y0 + 900, 28, 1320, 0xB8C2D6, 2);
   } else {
    // the binder: two pages, the spine
    P.box(kLeftPageX - 18, kBinderY - 14, kRightPageX + kPageW + 18, kBinderY + kPageH + 14, 26, 0x2A1A12, 1.0f, 0x140C08);
    P.box(kLeftPageX, kBinderY, kLeftPageX + kPageW, kBinderY + kPageH, 18, 0x10131C);
    P.box(kRightPageX, kBinderY, kRightPageX + kPageW, kBinderY + kPageH, 18, 0x10131C);
    P.box(kLeftPageX + kPageW + 4, kBinderY + 30, kRightPageX - 4, kBinderY + kPageH - 30, 8, 0x05070C);
    for (int r = 0; r < 3; ++r)  // the rings
      P.box(kLeftPageX + kPageW - 4, kBinderY + 150 + r * 310.0f, kRightPageX + 4, kBinderY + 180 + r * 310.0f, 12, 0xB8BEC8, 1.0f, 0x6A707C);
    for (int s = 0; s < kPerSpread; ++s) {
      float x0, y0, x1, y1;
      pocket_rect(s, &x0, &y0, &x1, &y1);
      if (s < static_cast<int>(b.cards.size())) {
        const Card& c = b.cards[s];
        // r149: a shiny's pocket shows its shiny picture (its big one once made, else its shiny portrait)
        const ImgP pic = b.owned[s].shinyAny() ? card_art(c, true, 0.0f) : portrait(c.key);
        mini_card(P, c, b.owned[s], x0, y0, x1, y1, pic, false, b.isNew[s]);
      } else {
        P.box(x0, y0, x1, y1, 14, 0x0C0F18);
      }
      if (s == b.cursor) P.ring(x0 - 10, y0 - 10, x1 + 10, y1 + 10, 22, 8, kGold);
    }
    const int firstPage = b.spread * 2 + 1;
    P.text("PAGES " + std::to_string(firstPage) + "-" + std::to_string(firstPage + 1) + " OF " + std::to_string(b.spreads * 2),
           (kLeftPageX + kRightPageX + kPageW) / 2, kBinderY + kPageH + 40, 36, 800, 0xB8C2D6, 2);
   }
    // the side: the brand, progress, packs, the card under the cursor
    const float sx0 = 1480, sx1 = 2120, scx = 0.5f * (sx0 + sx1);
    P.box(sx0, kBinderY, sx1, kBinderY + kPageH, 28, 0x151C36);
    logo(P, sx0 + 140, kBinderY + 18, 0.92f, false);
    P.text(std::to_string(b.have) + " / " + std::to_string(b.here), scx, kBinderY + 192, 80, 600, 0xFFFFFF, 5);
    P.box(sx0 + 40, kBinderY + 248, sx1 - 40, kBinderY + 278, 15, 0x2A3354);
    if (b.here > 0 && b.have > 0)
      P.box(sx0 + 40, kBinderY + 248, sx0 + 40 + (sx1 - sx0 - 80) * std::min(1.0f, static_cast<float>(b.have) / b.here),
            kBinderY + 278, 15, kGold);
    P.text("SR " + std::to_string(b.haveTier[2]) + "/" + std::to_string(b.hereTier[2]) + "   R " + std::to_string(b.haveTier[1]) +
               "/" + std::to_string(b.hereTier[1]) + "   C " + std::to_string(b.haveTier[0]) + "/" + std::to_string(b.hereTier[0]),
           scx, kBinderY + 318, 34, 580, 0xE8ECF5, 2);
    P.text("SILVER " + std::to_string(b.finishes[kSilver]) + "   GOLD " + std::to_string(b.finishes[kGoldTizm]) + "   BLACK " +
               std::to_string(b.finishes[kBlack]) + "   SHINY " + std::to_string(b.shinies),
           scx, kBinderY + 360, 32, 590, 0xE6B4FF, 2);
    // packs (r146: a little smaller - the card under the cursor's level and bonuses below)
    const float py0 = kBinderY + 396;
    if (b.packs > 0) {
      for (int i = std::min(2, b.packs - 1); i >= 0; --i)
        mini_pack(P, sx0 + 50 + i * 16, py0 + 6 + i * 8, sx0 + 176 + i * 16, py0 + 196 + i * 8, i > 0);
      P.text(std::to_string(b.packs) + (b.packs == 1 ? " PACK" : " PACKS"), sx0 + 410, py0 + 76, 72, 360, kGold, 5);
      P.text("A: OPEN ONE", sx0 + 410, py0 + 150, 40, 360, 0xFFFFFF, 2);
    } else {
      P.text("NO PACKS", scx, py0 + 50, 56, 560, 0x8A94AA, 3);
      P.text("PLAY FRANCHISE MATCHES:", scx, py0 + 108, 30, 580, 0xE8ECF5, 2);
      P.text("1 PACK A MATCH, 2 FOR A WIN", scx, py0 + 146, 30, 580, 0xE8ECF5, 2);
      P.text("+1 SERIES WON, +1 TROPHY  -  10 SPARES = 1", scx, py0 + 184, 30, 580, 0xB8C2D6, 2);
    }
    // r140: your TIZM COINS; r146: the next page LT / the last one RT (binder, shop, trade-ins, sets)
    coin_icon(P, sx0 + 72, kBinderY + 626, 30);
    P.left(std::to_string(b.coins) + " TIZM COINS", sx0 + 116, kBinderY + 626, 40, 330, 0xFFFFFF, 3);
    static const char* const kNext[4] = {"LT: SHOP", "LT: TRADE-INS", "LT: SETS", "RT: TRADE-INS"};
    P.right(kNext[std::max(0, std::min(3, b.page))], sx1 - 40, kBinderY + 626, 28, 200, 0xB8C2D6, 2);
    // the card under the cursor (r146: its level, its franchise bonuses); on SETS your bonuses in all
    const float cy0 = kBinderY + 668;
    P.box(sx0 + 24, cy0, sx1 - 24, kBinderY + kPageH - 24, 20, 0x0E1428);
    const Card* cc = nullptr;
    const Owned* co = nullptr;
    if (b.page == kPageTrade && !b.tiles.empty()) {
      cc = &b.tiles[static_cast<size_t>(b.tradeCursor)].c;
      co = &b.tiles[static_cast<size_t>(b.tradeCursor)].o;
    } else if (b.page != kPageTrade && b.page != kPageSets && b.cursor < static_cast<int>(b.cards.size())) {
      cc = &b.cards[b.cursor];
      co = &b.owned[b.cursor];
    }
    if (b.page == kPageSets) {
      char mul[32];
      std::snprintf(mul, sizeof(mul), "X%d.%02d", 1 + b.coinPct / 100, b.coinPct % 100);
      P.text("YOUR SET BONUSES", scx, cy0 + 38, 40, 560, 0xFFFFFF, 3);
      P.text(std::to_string(b.bonusChars) + " CHARACTERS GET MORE FRANCHISE XP", scx, cy0 + 80, 28, 570, 0x6ED36E, 2);
      P.text("LINES " + std::to_string(b.linesDone) + "  -  TYPES " + std::to_string(b.typesDone) + "  -  FULL PAGES " +
                 std::to_string(b.pagesFull),
             scx, cy0 + 116, 28, 570, 0xE8ECF5, 2);
      P.text(std::string("TIZM COINS ") + mul + " IN FRANCHISE MATCHES", scx, cy0 + 152, 28, 570, b.coinPct > 0 ? kGold : 0xB8C2D6, 2);
      P.text("XP: LEVEL + LINE + TYPES, AT MOST +" + std::to_string(kXpCap) + "%", scx, cy0 + 184, 22, 570, 0x8A94AA, 2);
      // r149: your case hits, your GOD PACKs
      P.text("CASE HITS: BOOM " + std::to_string(b.hits[kHitBoom]) + "  HOMETOWN " + std::to_string(b.hits[kHitHome]) + "  SIGNED " +
                 std::to_string(b.hits[kHitSigned]) + "  -  GOD PACKS " + std::to_string(b.godPacks),
             scx, cy0 + 214, 24, 570, b.godPacks > 0 ? kGold : 0xE6B4FF, 2);
    } else if (cc != nullptr) {
      const Card& c = *cc;
      const Owned& o = *co;
      const bool own = o.total() > 0;
      P.text(own ? upper(c.name) : "???", scx, cy0 + 40, 46, 560, 0xFFFFFF, 3);
      P.text("#" + num3(c.num) + "  -  " + kRarityName[c.rarity], scx, cy0 + 84, 32, 560,
             c.rarity == kSuper ? 0xE6B4FF : (c.rarity == kRare ? kGold : 0xE8ECF5), 2);
      if (b.page == kPageTrade)  // r149: on TRADE-INS only its BASE copies (nothing of its finishes / shinies)
        P.text("BASE COPIES: " + std::to_string(o.n[kBase][0]) + "  -  " +
                   std::to_string(b.tiles[static_cast<size_t>(b.tradeCursor)].spare) + " SPARE",
               scx, cy0 + 124, 28, 570, 0x6ED36E, 2);
      else
        P.text(own ? have_text(o) : "NOT IN YOUR BINDER YET", scx, cy0 + 124, 28, 570, own ? 0x6ED36E : 0x8A94AA, 2);
      if (own) {
        // its level: stars, what it gives; the copies to its next
        const int lv = b.curLevel;
        for (int s = 0; s < lv; ++s) star_shape(P, sx0 + 64 + s * 30.0f, cy0 + 166, 12, kGold);
        const float tx = sx0 + 64 + lv * 30.0f;
        std::string t = "LV " + std::to_string(lv) + (lv > 1 ? ": +" + std::to_string(level_xp(lv)) + "% XP, EVOLVES FASTER" : "");
        if (lv < 5) t += "  -  " + std::to_string(kLevelCopies[lv] - o.total()) + " MORE: LV " + std::to_string(lv + 1);
        P.left(t, tx, cy0 + 166, 26, sx1 - 40 - tx, lv > 1 ? kGold : 0xE8ECF5, 2);
        P.text(b.curXp > 0 ? "FRANCHISE: +" + std::to_string(b.curXp) + "% XP (" + b.curWhy + ")" : "FRANCHISE: NO BONUS YET", scx,
               cy0 + 206, 24, 570, b.curXp > 0 ? 0x6ED36E : 0x8A94AA, 2);
      }
    }
    if (b.mode == 2) {  // a card out in front of you
      out.hints = {{"GRIP", "HOLD IT", 0x3A4A70}, {"LT RT", "TURN IT OVER", 0x3A4A70}, {"B", "BACK IN THE BINDER", 0xC0392B}};
    } else if (b.page == kPageTrade) {  // r146
      out.hints = {{"STICK", "PICK", 0x3A4A70}, {"A", "TRADE 10", 0x2EA043}, {"X", "KEEP / TRADE IT", 0x2E6FD0},
                   {"LT", "SETS", 0x3A4A70}, {"RT", "SHOP", 0x3A4A70}};
    } else if (b.page == kPageSets) {  // r146
      out.hints = {{"STICK", "MORE LINES", 0x3A4A70}, {"RT", "TRADE-INS", 0x3A4A70}};
    } else if (b.shop) {  // r140
      out.hints = {{"STICK", "PICK", 0x3A4A70}, {"A", "BUY", 0x2EA043}, {"LT", "TRADE-INS", 0x3A4A70}, {"RT", "THE BINDER", 0x3A4A70}};
    } else {
      out.hints.push_back({"LT RT", "PAGES / SHOP", 0x3A4A70});
      out.hints.push_back({"L", "CARDS", 0x3A4A70});
      if (b.packs > 0) {
        out.hints.push_back({"A", "OPEN A PACK", 0x2EA043});
        out.hints.push_back({"X", "LOOK", 0x2E6FD0});
      } else {
        out.hints.push_back({"A", "LOOK", 0x2EA043});
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

// r140: new packs rolled (packs earned, packs bought; `flags` what got them) and waiting; what's in them for the
// pictures to be made early. (g_mutex held)
static void add_packs(int n, unsigned flags, std::vector<std::pair<std::string, bool>>* prefetch) {
  for (int i = 0; i < n && g_packs.size() < 999; ++i) {
    uint64_t rng = g_salt ^ (static_cast<uint64_t>(g_serial + 1) * 0x9E3779B97F4A7C15ull);
    const bool god = mix64(rng) % kGodOdds == 0;  // r149: a GOD PACK
    Pack p = roll_pack(rng, {}, god);
    p.serial = ++g_serial;
    p.flags = flags | (god_cards(p) ? kGodPack : 0u);
    if (p.cards[0].card < 0) break;  // (no set on this headset)
    g_packs.push_back(p);
    for (const Ref& r : p.cards)
      if (r.card >= 0) prefetch->push_back({g_set[r.card].key, r.shiny});
  }
  g_packCount = static_cast<int>(g_packs.size());
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
    for (const char* n : {"base", "rare", "silver", "gold", "boom", "hometown", "signed", "black", "shiny", "godpack", "godtell"})
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

int coins() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_coins;
}
int last_coins(std::string* why, long long* when) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (why != nullptr) *why = g_lastCoinsWhy;
  if (when != nullptr) *when = g_lastCoinsWhen;
  return g_lastCoins;
}

void poll() {
  load_set();
  static bool s_read = false;
  if (!s_read) {
    s_read = true;
    std::lock_guard<std::mutex> lock(g_mutex);
    load_collection();
    if (g_salt == 0) {
      std::random_device rd;
      g_salt = (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ static_cast<uint64_t>(std::time(nullptr));
      if (g_salt == 0) g_salt = 0x5EED5EEDull;
    }
    g_packCount = static_cast<int>(g_packs.size());
    bonuses_update();  // r146: the franchise's card bonuses from the collection as it is
    ++g_gen;
  }
  // r140: TIZM COINS from a match just played (the moment it ends): kept, a coin's ring
  char why[512] = {};
  const int coins = PortFranchiseTakeCardCoins(why, static_cast<int>(sizeof(why)));
  if (coins > 0) {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_coins += coins;
      g_coinsEarned += coins;
      g_lastCoins = coins;
      g_lastCoinsWhen = static_cast<long long>(std::time(nullptr));
      g_lastCoinsWhy = why;
      save_collection();
      CLOG("+%d TIZM coins (%s): %d now", coins, why, g_coins);
    }
    ++g_gen;
    coin_sound(3);
  }
  unsigned flags = 0;
  const int n = PortFranchiseTakeCardPacks(&flags);
  if (n <= 0) return;
  std::vector<std::pair<std::string, bool>> prefetch;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    add_packs(n, flags, &prefetch);
    save_collection();
    CLOG("%d pack%s earned (%s%s%s), %d waiting", n, n == 1 ? "" : "s", (flags & 1) ? "a win" : "a match",
         (flags & 2) ? ", a playoff series won" : "", (flags & 4) ? ", a trophy" : "", static_cast<int>(g_packs.size()));
  }
  ++g_gen;
  // the pictures of what's in them made now, so opening them is instant
  for (const auto& kp : prefetch) xrbig::prefetch(kp.first, kp.second, kArt);
}

// r140: the CARDS shop: a pack (kPackPrice) or a box of five (kBoxPrice), paid in TIZM coins; rolled and saved the moment
// they're bought (the same odds as an earned pack, never re-rolled), waiting with the rest (opened the same way). 0
// bought, 1 not enough coins, 2 no room. (Game thread.)
static int buy_packs(bool box) {
  std::vector<std::pair<std::string, bool>> prefetch;
  const int price = box ? kBoxPrice : kPackPrice, n = box ? kBoxPacks : 1;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_coins < price) return 1;
    if (g_packs.size() + n > 999) return 2;
    g_coins -= price;
    g_coinsSpent += price;
    add_packs(n, kBought, &prefetch);
    save_collection();
    CLOG("bought %s for %d TIZM coins: %d left, %d packs waiting", box ? "a box of 5 packs" : "a pack", price, g_coins,
         static_cast<int>(g_packs.size()));
  }
  ++g_gen;
  coin_sound(2);
  for (const auto& kp : prefetch) xrbig::prefetch(kp.first, kp.second, kArt);
  return 0;
}

// r146: a TRADE-IN: the next kTradeFor spare base cards (trade_pick: commons first, the most duplicated first, none kept, never a last copy)
// out of your collection, a pack rolled for them that moment (the same odds as any, never re-rolled) waiting with the
// rest; its history; saved, the franchise bonuses worked out again (the levels it took). 0 traded (*out: the cards, for
// its show), 1 not enough spares, 2 no room. (Game thread.)
static int trade_in(std::vector<int>* out) {
  std::vector<std::pair<std::string, bool>> prefetch;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::vector<std::pair<int, int>> pick = trade_pick();
    if (pick.empty()) return 1;
    if (g_packs.size() + 1 > 999) return 2;
    const size_t before = g_packs.size();
    add_packs(1, kTraded, &prefetch);
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
      t.what += (t.what.empty() ? "" : ",") + g_set[p.first].key + "*" + std::to_string(p.second);
    }
    g_trades.push_back(t);
    if (g_trades.size() > 40) g_trades.erase(g_trades.begin(), g_trades.end() - 40);
    g_tradedCards += kTradeFor;
    ++g_tradedPacks;
    save_collection();
    bonuses_update();
    CLOG("traded in %d spare base cards (%s) for pack %d: %d packs waiting, %d traded in all", kTradeFor, t.what.c_str(), t.pack,
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
constexpr int kCornerSegs = 5, kEdgeSegs = 4 * (kCornerSegs + 1), kEdgeNV = (kEdgeSegs + 1) * 2;
constexpr int kCardVerts = 2 * kCardNV + kEdgeNV;  // a card's vertices: its front, its back, its edge

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
enum Phase { kWaiting, kFiling, kStack };

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
};

// The pictures a model is made of, drawn on threads; uploaded once all are in.
struct Pics {
  std::mutex m;
  std::vector<std::vector<uint8_t>> rgba;
  std::vector<int> w, h;
  int left = 0;
  int gen = 0;
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
  std::vector<std::string> packNew;  // r149: the cards the open pack made NEW (unmade if it goes back unopened)
  int newLook = -1;                  // r149: the NEW card the binder's cursor rests on, since when
  uint64_t newLookNs = 0;
  bool godAdded = false, godDone = false;  // r149: the open pack's a GOD PACK (counted), its show had (or skipped)
  bool boomShown[kSheets] = {};            // r150: a TIZM BOOM's pop-out show played this scene (again only on a shake)
};
Scene3 S;
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
void god_prepare();
void god_tell(uint64_t now);
void god_last(uint64_t now);
bool god_live();
void god_skip();
void god_end(bool fxReady);
int g_picGen = 0;
std::mutex g_packPxMutex;
std::vector<uint8_t> g_packPx[2];  // the pack's pictures, drawn once (every pack looks the same)
int g_packPxTotal = -1;

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
void ask_pictures(bool packToo) {
  const int gen = ++g_picGen;
  int total = 0;
  std::vector<Card> cards;
  std::vector<int> levels;  // r146: each card's level as it stands (its stars, its back's line)
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    total = static_cast<int>(g_set.size());
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
    {
      std::lock_guard<std::mutex> lock(g_packPxMutex);
      cached = g_packPxTotal == total && !g_packPx[0].empty() && !g_packPx[1].empty();
      if (cached) pics->rgba = {g_packPx[0], g_packPx[1]};
    }
    if (!cached) {
      pics->left = 2;
      std::thread([pics, total]() {
        for (int side = 0; side < 2; ++side) {
          std::vector<uint8_t> px = draw_pack(side == 1, total);
          {
            std::lock_guard<std::mutex> lock(g_packPxMutex);
            g_packPx[side] = px;
            g_packPxTotal = total;
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
  auto pics = std::make_shared<Pics>();
  pics->rgba.resize(4 * S.n);
  pics->w.assign(4 * S.n, kFrontW);
  pics->h.assign(4 * S.n, kFrontH);
  std::vector<int> tags(static_cast<size_t>(S.n), 0);
  for (int i = 0; i < S.n; ++i) {
    pics->w[S.n + i] = kBackW;
    pics->h[S.n + i] = kBackH;
    const FinishLook& fl = kFinishLook[S.refs[i].parallel];
    pics->w[2 * S.n + i] = pics->h[2 * S.n + i] = 4;
    std::vector<uint8_t>& e = pics->rgba[2 * S.n + i];
    e.resize(4 * 4 * 4);
    for (int p = 0; p < 16; ++p) {
      e[p * 4 + 0] = (fl.edge >> 16) & 255;
      e[p * 4 + 1] = (fl.edge >> 8) & 255;
      e[p * 4 + 2] = fl.edge & 255;
      e[p * 4 + 3] = static_cast<uint8_t>(255 - std::lround(fl.edgeFoil * 127.0f));
    }
    tags[static_cast<size_t>(i)] = S.tagKind[i];
    pics->w[3 * S.n + i] = S.tagKind[i] != 0 ? kTagW : 4;
    pics->h[3 * S.n + i] = S.tagKind[i] != 0 ? kTagH : 4;
    if (S.tagKind[i] == 0) pics->rgba[3 * S.n + i].assign(4 * 4 * 4, 0);
  }
  int tagCount = 0;
  for (int k : tags) tagCount += k != 0 ? 1 : 0;
  pics->left = 2 * S.n + tagCount;
  pics->gen = gen;
  S.cardPics = pics;
  // three threads (the fronts on two, the backs on one): each picture's canvas is ~26 MB of floats while it's drawn
  const std::vector<Ref> refs(S.refs, S.refs + S.n);
  for (int t = 0; t < 2; ++t)
    std::thread([pics, cards, refs, levels, total, t, waitS]() {
      for (size_t i = static_cast<size_t>(t); i < cards.size(); i += 2) {
        std::vector<uint8_t> px = draw_front(cards[i], refs[i], total, waitS, kSpNone, levels[i]);
        std::lock_guard<std::mutex> lock(pics->m);
        pics->rgba[i] = std::move(px);
        --pics->left;
      }
    }).detach();
  std::vector<int> tagLevel(S.tagLevel, S.tagLevel + S.n), tagCopies(S.tagCopies, S.tagCopies + S.n);
  std::thread([pics, cards, total, tags, tagLevel, tagCopies]() {
    for (size_t i = 0; i < cards.size(); ++i) {
      std::vector<uint8_t> px = draw_back(cards[i], total);  // (r147: the plain back - nothing of its level before it's turned)
      std::lock_guard<std::mutex> lock(pics->m);
      pics->rgba[cards.size() + i] = std::move(px);
      --pics->left;
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
  god_end(fxReady);       // r149 (before the 1/1 world's: the same slot)
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

// A pack opened: its cards into your collection at once (the opening is the show; a crash mid-way loses nothing), the
// pack up into your hands.
bool open_pack(uint64_t now) {
  Pack p;
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
      if (g_newCards.insert(g_set[r.card].key).second) S.packNew.push_back(g_set[r.card].key);  // r149: NEW in the binder
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
  }
  S.packData = p;
  set_anchor();
  S.mode = kPackUp;
  S.phase = kWaiting;
  S.t0 = S.phaseNs = now;
  S.cardsNs = 0;
  S.n = 0;
  for (const Ref& r : p.cards)
    if (r.card >= 0) S.refs[S.n++] = r;
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
void look_card(int card, uint64_t now, const XrVector3f* from) {
  set_anchor();
  S.mode = kOut;
  S.phase = kStack;
  S.t0 = S.phaseNs = now;
  S.n = 1;
  S.pack.on = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    // your finest copy of it: its finest finish (a shiny one of that if you have it), a GOLD's lowest number
    const Owned& o = g_owned[card];
    Ref r;
    r.card = card;
    r.parallel = std::max(0, o.best());
    r.shiny = o.n[r.parallel][1] > 0;
    r.serial = r.parallel == kBlack ? 1 : (r.parallel == kGoldTizm && !o.golds.empty() ? o.golds.front() : 0);
    S.refs[0] = r;
    CLOG("looking at card %d: %s", g_set[card].num, pull_text(r).c_str());
    if (g_newCards.erase(g_set[card].key) > 0) save_collection();  // r149: seen
  }
  sheet_reset(S.card[0], kCardWm, kCardHm);
  Sheet& s = S.card[0];
  s.tPos = v_add(v_add(S.anchor, v_mul(S.fwd, 0.40f)), XrVector3f{0.0f, -0.04f, 0.0f});
  s.pos = from != nullptr ? *from : v_add(s.tPos, XrVector3f{0.0f, -0.3f, 0.0f});
  s.lastPos = s.pos;
  s.scale = 0.3f;
  s.tScale = 1.45f;
  g_sceneMode = 2;
  drop_cards_model(true);
  ask_pictures(false);
  specials_plan(now, true);  // r138: a special card's 3D out of the binder too
  special_focus(0, now, true);
  ++g_gen;
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
    const float nx = -0.154f + (static_cast<float>(i % 5) - 2.0f) * 0.09f, ny = i < 5 ? 0.12f : -0.08f;
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
  if (S.mode == kPackUp && S.phase == kWaiting) unopen();
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
  if (s.flipDur > 0.0f) return;  // (still turning over)
  if (!S.faceUp[f]) {
    S.faceUp[f] = true;
    ++S.revealed;
    start_flip(s, 0.0f, 0.5f, now + 150000000ull, true);  // (out toward you first, then over)
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
void reveal_fx(int i) {
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
  const bool cameo = r.parallel == kBlack && S.mode == kPackUp && cameo_ask(i, c.key);
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
void card_box(float* out, const Sheet& s, const XrQuaternionf& q, const XrVector3f& pos, float scale, bool show,
              float thick = kCardT, float nrm = 1.0f) {  // r138: (a special's shell: obsidian thicker, normals 2 long)
  auto put = [&](int v, const XrVector3f& lp, const XrVector3f& ln) {
    const XrVector3f p = v_add(pos, q_rot(q, lp)), n = v_mul(q_rot(q, ln), nrm);
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
    for (int i = 0; i <= kCardNY; ++i)
      for (int j = 0; j <= kCardNX; ++j)
        put(side * kCardNV + i * (kCardNX + 1) + j,
            XrVector3f{(static_cast<float>(j) / kCardNX - 0.5f) * W, (0.5f - static_cast<float>(i) / kCardNY) * H, z},
            XrVector3f{0.0f, 0.0f, side == 0 ? 1.0f : -1.0f});
  }
  // its edge: round its four corners (top right, top left, bottom left, bottom right), back to the start
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
  // r150: above it in the room - its highest point (its corners; what stands on its top edge, up its own up), 2.5 cm more
  // (rising a little as it goes), straight over its middle
  const XrVector3f ax = q_rot(q, XrVector3f{0.5f * kCardWm * s.scale, 0.0f, 0.0f});
  const XrVector3f ay = q_rot(q, XrVector3f{0.0f, 0.5f * kCardHm * s.scale, 0.0f});
  float top = std::fabs(ax.y) + std::fabs(ay.y);
  const float above = special_above(i);
  if (above > 0.0f) {
    const XrVector3f up = q_rot(q, XrVector3f{0.0f, (0.5f * kCardHm + above) * s.scale, 0.0f});
    top = std::max(top, up.y + std::fabs(ax.y));
  }
  const float rise = t > kTagShow - 0.3f ? 0.02f * (t - (kTagShow - 0.3f)) / 0.3f : 0.0f;
  const XrVector3f at = v_add(s.pos, XrVector3f{0.0f, top + 0.025f * s.scale + 0.5f * TH + rise, 0.0f});
  const XrQuaternionf fq = facing(at);
  const XrVector3f R = q_rot(fq, XrVector3f{1.0f, 0.0f, 0.0f}), U = q_rot(fq, XrVector3f{0.0f, 1.0f, 0.0f});
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
  if (t >= 0.0f && t < 0.5f)  // (its burst)
    xrfx::card_sparkle(at, 0.05f * s.scale, 0.05f * s.scale, S.tagKind[i] == 2 ? 70.0f : 30.0f, dt, kGold, 0xFFFFFF);
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
        if (s == nullptr || (i == 0 && S.mode != kPackUp)) continue;
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

int special_kind(const Ref& r) {
  if (r.hit == kHitBoom) return kSpBoom;    // r149: its comic layers popping out
  if (r.hit == kHitHome) return kSpWindow;  // r149: a window into its home (landscape)
  if (r.parallel == kGoldTizm) return kSpPop;
  if (r.parallel == kBlack) return kSpGlass;
  if (r.parallel == kSilver || r.shiny) return kSpWindow;
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
// (r149: `wHalf`, `wBot`, `wTop` its window - a HOMETOWN's landscape one, in its own frame)
Fit fit_figure(const xrevo::Figure* f, int kind, float wHalf = kWinHalf, float wBot = kWinBottom, float wTop = kWinTop) {
  Fit fit;
  if (kind == kSpGlass) {
    for (uint32_t want : {5u, 2u}) {  // its cheer, else its big move
      for (const xrevo::Segment& seg : xrevo::segments(f))
        if (seg.kind == want && seg.frames > 4) {
          fit.loopStart = static_cast<int>(seg.start);
          fit.loopFrames = static_cast<int>(seg.frames);
          break;
        }
      if (fit.loopFrames > 0) {
        if (want == 5u)  // (its cameo's celebration: the cheer, then its first big move)
          for (const xrevo::Segment& seg : xrevo::segments(f))
            if (seg.kind == 2 && seg.frames > 4) {
              fit.bigStart = static_cast<int>(seg.start);
              fit.bigFrames = static_cast<int>(seg.frames);
              break;
            }
        break;
      }
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
  if (kind == kSpPop)  // a pop-out loops a move of its own when its pose file has one (a home figure's big move)
    for (const xrevo::Segment& seg : xrevo::segments(f))
      if (seg.kind == 2 && seg.frames > 4) {
        fit.loopStart = static_cast<int>(seg.start);
        fit.loopFrames = static_cast<int>(seg.frames);
        break;
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
  for (int y = 0; y < 5; ++y) {
    const float yaw = kYaw[y];
    const Bounds b = posed_bounds(f, yaw, 0, 0, slab ? 0.3f : 0.0f);
    const float s = std::min({fill * wAv / std::max(1e-4f, b.hi.x - b.lo.x), fill * hAv / std::max(1e-4f, b.hi.y - b.lo.y),
                              (z1 - z0) / std::max(1e-4f, b.hi.z - b.lo.z)});
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

float fx_r(uint64_t& s) { return static_cast<float>(rand01(s)); }
uint32_t fx_rgb_mix(uint32_t a, uint32_t b, float t) { return mix_rgb(a, b, clampf(t, 0.0f, 1.0f)); }

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

// A special's model, made on a thread: its pictures, its figure (or its picture standing up when it has none here).
struct SpecialBuild {
  ~SpecialBuild() {
    if (figure != nullptr) xrevo::release(figure);  // (a scene put away before its figure was taken)
  }
  std::mutex m;
  bool done = false;
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
};
struct Special {
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
      {"gamewatch", "suite_gamewatch", "SUPERFLAT WORLD"}};
  for (const Home& h : kHome)
    if (c.key == h.key) {
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
void home_room_pictures(const Card& c, std::vector<std::vector<uint8_t>>* tex, std::vector<int>* tw, std::vector<int>* th) {
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
void boom_layers(const Card& c, const Ref& r, std::vector<std::vector<uint8_t>>* tex, std::vector<int>* tw, std::vector<int>* th) {
  const uint32_t ink = 0x0B0B12;
  auto add = [&](const Pc& P, int w, int h) {
    tex->push_back(board_px(w, h, P));
    tw->push_back(w);
    th->push_back(h);
  };
  (void)r;  // (r150: the character stays printed on its face - only the burst and the words pop out)
  {  // burst shards round it (none over its middle)
    Pc B;
    for (int k = 0; k < 14; ++k) {
      const float a0 = 6.2831853f * k / 14.0f, a1 = a0 + 0.16f, am = a0 + 0.08f;
      const float r0 = 150.0f, r1 = 246.0f + 8.0f * ((k * 7) % 3);
      for (int pass = 0; pass < 2; ++pass) {
        const float g = pass == 0 ? 8.0f : 0.0f;
        polyv(B, {256 + (r0 - g) * std::cos(a0 - 0.02f), 256 + (r0 - g) * std::sin(a0 - 0.02f), 256 + (r1 + g) * std::cos(am),
                  256 + (r1 + g) * std::sin(am), 256 + (r0 - g) * std::cos(a1 + 0.02f), 256 + (r0 - g) * std::sin(a1 + 0.02f)},
              pass == 0 ? ink : (k % 2 == 0 ? 0xFFD21F : 0xFF3B30));
      }
    }
    add(B, 512, 512);
  }
  std::string words[3];
  boom_words(c.key, words);
  static const uint32_t kFill[3] = {0xFFFFFF, 0xFFD21F, 0x7AE0FF}, kInk[3] = {0xFF3B30, 0x1A5AD8, 0xFF3B30};
  for (int k = 0; k < 3; ++k) {  // its words in their bubbles
    Pc W;
    W.burst(6, 6, 506, 250, 16, 0.74f, 0.15f * k, ink);
    W.burst(18, 16, 494, 240, 16, 0.74f, 0.15f * k, kFill[k]);
    W.style(0.12f, 0.9f, k == 1 ? 6.0f : -6.0f);
    W.rim(ink);
    W.text(words[k], 256, 128, words[k].size() > 8 ? 62 : 84, 400, kInk[k], 7, 0xFFFFFF);
    add(W, 512, 256);
  }
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
  const xrsign::Signature& s = xrsign::get(c.key, c.name);
  std::unordered_map<uint32_t, size_t> used;
  uint32_t best = 0;
  size_t most = 0;
  for (const xrsign::Stroke& k : s.strokes)
    if ((used[k.rgba >> 8] += k.pts.size()) > most) most = used[k.rgba >> 8], best = k.rgba >> 8;
  *y = static_cast<float>((best >> 16) & 255) + static_cast<float>((best >> 8) & 255) / 256.0f;
  *z = 3.0f + static_cast<float>(best & 255) / 256.0f;
}
void bake_signature(std::vector<uint8_t>& px, int w, int h, const Card& c) {
  const xrsign::Signature& s = xrsign::get(c.key, c.name);
  std::vector<uint8_t> ink(static_cast<size_t>(kSignW) * kSignH * 4, 0);
  xrsign::draw(s, s.duration + 1.0f, ink.data(), kSignW, kSignH, kSignW * 4);
  // (a soft dark edge round its ink, 3 px - it reads on a busy refractor too)
  std::vector<uint8_t> edge(static_cast<size_t>(kSignW) * kSignH, 0), tmp(edge.size(), 0);
  for (int y = 0; y < kSignH; ++y)
    for (int x = 0; x < kSignW; ++x) {
      uint8_t m = 0;
      for (int d = -3; d <= 3; ++d) m = std::max(m, ink[(static_cast<size_t>(y) * kSignW + std::max(0, std::min(kSignW - 1, x + d))) * 4 + 3]);
      tmp[static_cast<size_t>(y) * kSignW + x] = m;
    }
  for (int y = 0; y < kSignH; ++y)
    for (int x = 0; x < kSignW; ++x) {
      uint8_t m = 0;
      for (int d = -3; d <= 3; ++d) m = std::max(m, tmp[static_cast<size_t>(std::max(0, std::min(kSignH - 1, y + d))) * kSignW + x]);
      edge[static_cast<size_t>(y) * kSignW + x] = m;
    }
  for (int y = 0; y < kSignH; ++y)
    for (int x = 0; x < kSignW; ++x) {
      const int X = static_cast<int>(kSignX0) + x, Y = static_cast<int>(kSignY0) + y;
      if (X < 0 || Y < 0 || X >= w || Y >= h) continue;
      const size_t i = static_cast<size_t>(y) * kSignW + x;
      const uint8_t* q = &ink[i * 4];
      if (q[3] == 0 && edge[i] == 0) continue;
      uint8_t* p = &px[(static_cast<size_t>(Y) * w + X) * 4];
      const float e = 0.45f * edge[i] / 255.0f, a = q[3] / 255.0f;
      for (int k = 0; k < 3; ++k) p[k] = static_cast<uint8_t>(std::lround((p[k] * (1.0f - e) + 10.0f * e) * (1.0f - a) + q[k] * a));
      if (p[3] > 0) p[3] = 255;  // (ink: no foil under it)
    }
}
// its autograph as it writes: R its glow, G its ink, B when it's written there (0..1 of its time), A how much shows
std::vector<uint8_t> sign_strip(const Card& c) {
  const xrsign::Signature& s = xrsign::get(c.key, c.name);
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
  // its glow: the ink spread 10 px (a box blur twice), its time carried out into it (the nearest written pixel's)
  std::vector<float> glow = core, tmp(core.size());
  std::vector<float> tw = when;
  for (int pass = 0; pass < 2; ++pass) {
    for (int y = 0; y < kSignH; ++y)
      for (int x = 0; x < kSignW; ++x) {
        float sum = 0.0f, tsum = 0.0f, tn = 0.0f;
        for (int d = -10; d <= 10; ++d) {
          const int xx = std::max(0, std::min(kSignW - 1, x + d));
          sum += glow[static_cast<size_t>(y) * kSignW + xx];
          const float tv = tw[static_cast<size_t>(y) * kSignW + xx];
          if (tv >= 0.0f) tsum += tv, tn += 1.0f;
        }
        tmp[static_cast<size_t>(y) * kSignW + x] = sum / 21.0f;
        if (tw[static_cast<size_t>(y) * kSignW + x] < 0.0f && tn > 0.0f) tw[static_cast<size_t>(y) * kSignW + x] = tsum / tn;
      }
    for (int y = 0; y < kSignH; ++y)
      for (int x = 0; x < kSignW; ++x) {
        float sum = 0.0f, tsum = 0.0f, tn = 0.0f;
        for (int d = -10; d <= 10; ++d) {
          const int yy = std::max(0, std::min(kSignH - 1, y + d));
          sum += tmp[static_cast<size_t>(yy) * kSignW + x];
          const float tv = tw[static_cast<size_t>(yy) * kSignW + x];
          if (tv >= 0.0f) tsum += tv, tn += 1.0f;
        }
        glow[static_cast<size_t>(y) * kSignW + x] = sum / 21.0f;
        if (tw[static_cast<size_t>(y) * kSignW + x] < 0.0f && tn > 0.0f) tw[static_cast<size_t>(y) * kSignW + x] = tsum / tn;
      }
  }
  for (size_t i = 0; i < when.size(); ++i) {
    const float g = clampf(glow[i] * 2.2f, 0.0f, 1.0f), a = std::max(g, core[i]);
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

// Its figure's model and standing loop (xr_big's sources: a Strikers character's captain / home figure; a guest's pack -
// a shiny one's OWN _shiny pack first - with its pose file), else none. Thread.
// r141 `home`: a home figure (its own moves: a celebration to loop on a BLACK 1/1's throne) first, when there is one;
// else a retarget guest's celebration (guests/cel/, made now the first time)
xrevo::Figure* load_figure(const Card& c, bool shiny, xrfx::Model* m, bool home = false) {
  const std::string g = home_dir() + "/guests/";
  std::vector<std::pair<std::string, std::string>> tries;
  if (c.key.compare(0, 9, "strikers_") == 0) {
    const std::string n = c.key.substr(9);
    if (home) tries.push_back({g + "home/strikers_" + n + ".gpk", g + "home/strikers_" + n + ".gev"});
    tries.push_back({g + "cap_" + n + ".gpk", g + "evo/cap_" + n + ".gev"});
    tries.push_back({g + "home/strikers_" + n + ".gpk", g + "home/strikers_" + n + ".gev"});
  } else {
    const bool homeFig = file_there(g + "home/" + c.key + ".gpk") && file_there(g + "home/" + c.key + ".gev");
    if (home && !shiny) tries.push_back({g + "home/" + c.key + ".gpk", g + "home/" + c.key + ".gev"});
    if (home && (shiny || !homeFig)) {
      const std::string gpk = g + c.key + (shiny ? "_shiny" : "") + ".gpk", gev = g + "cel/" + c.key + ".gev";
      if (file_there(gpk)) {
        if (!file_there(gev)) {
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
    total = static_cast<int>(g_set.size());
    level = card_level(g_owned[r.card]);  // r146: its stars
  }
  sp = Special{};
  sp.i = i;
  sp.kind = special_kind(r);
  sp.shiny = r.shiny;
  sp.spotUntil = sp.kind == kSpGlass && S.mode == kOut ? now + 4500000000ull : 0;  // (out of the binder: lit at once)
  sp.theme = sp.kind == kSpGlass ? fx_theme_of(c.key) : -1;  // r141: a BLACK 1/1's world
  sp.roster = c.roster;
  sp.klass = c.klass;
  sp.rgb = c.c1;  // r147
  // r149: a HOMETOWN's landscape diorama; an autograph (a SIGNED case hit, every BLACK 1/1) - writing itself on as it's
  // first turned over in a pack, out of the binder already written
  sp.home = r.hit == kHitHome;
  sp.signedCard = r.hit == kHitSigned || r.parallel == kBlack;
  sp.signDone = S.mode != kPackUp;
  auto b = std::make_shared<SpecialBuild>();
  sp.build = b;
  const int kind = sp.kind;
  std::thread([b, c, r, total, kind, level]() {
    std::vector<std::vector<uint8_t>> tex;
    std::vector<int> tw, th;
    tex.push_back(draw_front(c, r, total, kind == kSpPop ? 3.0f : 0.0f, kind, level));  // (r147: a GOLD's picture: its big one)
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
    if (r.hit == kHitHome) home_room_pictures(c, &tex, &tw, &th);  // r149: its home
    else if (kind == kSpWindow || kind == kSpGlass) room_pictures(c, kind, r.shiny, &tex, &tw, &th);
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
      signDur = std::max(1.0f, std::min(4.0f, xrsign::get(c.key, c.name).duration));
      sign_ink(c, &signY, &signZ);
      tex.push_back(sign_strip(c));
      tw.push_back(kSignW);
      th.push_back(kSignH);
    }
#endif
    xrfx::Model fig;
    // (r149: a TIZM BOOM / a SIGNED BASE: no figure - its picture on its front)
    xrevo::Figure* f = kind == kSpBoom || kind == kSpSign ? nullptr : load_figure(c, r.shiny, &fig, kind == kSpGlass);  // (r141: the 1/1's own celebration)
    Fit fit;
    if (f != nullptr) {
      // r140: measured posed, here on the thread; r149: a HOMETOWN in its landscape window
      fit = r.hit == kHitHome ? fit_figure(f, kind, kHomeWinHalfX, -kHomeWinHalfY, kHomeWinHalfY) : fit_figure(f, kind);
      CLOG("special: %s fitted at %.3f, yaw %.2f, feet (%.3f, %.3f, %.3f)%s", c.key.c_str(), fit.scale, fit.yaw, fit.feet.x,
           fit.feet.y, fit.feet.z, fit.plinthTop > kWinBottom ? ", on a plinth" : "");
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
    std::lock_guard<std::mutex> lock(b->m);
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
    b->done = true;
  }).detach();
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
  card_box(shell.data(), unit, XrQuaternionf{0.0f, 0.0f, 0.0f, 1.0f}, XrVector3f{0.0f, 0.0f, 0.0f}, 1.0f, true,
           sp.kind == kSpGlass ? 3.0f * kCardT : kCardT, 2.0f);
  grid_part(0, kCardNX, kCardNY, false);
  stat.insert(stat.end(), shell.begin(), shell.begin() + kCardNV * 6);
  grid_part(1, kCardNX, kCardNY, true);
  stat.insert(stat.end(), shell.begin() + kCardNV * 6, shell.begin() + 2 * kCardNV * 6);
  grid_part(2, kEdgeSegs, 1, false);
  stat.insert(stat.end(), shell.begin() + 2 * kCardNV * 6, shell.end());
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
  if (sp.kind == kSpWindow || sp.kind == kSpGlass) {
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
  const Fit& fit = b.fit;
  if (sp.kind == kSpWindow && fit.plinthTop > y0 + 0.001f) {  // r140: a plinth raising a short one to the middle
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
  if (b.layerTex >= 0 && b.layers >= 4) {
    sp.popBase = static_cast<int>(stat.size() / 6);
    sp.pops = 4;
    for (int k = 0; k < 4; ++k) {
      const XrVector3f q[4] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
      quad(b.layerTex + k, q, XrVector3f{0, 0, 0.75f}, false);
    }
  }
  // r149: its autograph writing itself on (a soft part over its front - a BLACK's over its glass; normals 3 long, their x
  // how far it's written: xr_fx_model.frag), across its picture, low
  if (b.signTex >= 0) {
    const float sx0 = (kSignX0 / kCW - 0.5f) * kCardWm, sx1 = ((kSignX0 + kSignW) / kCW - 0.5f) * kCardWm;
    const float sy1 = (0.5f - kSignY0 / kCH) * kCardHm, sy0 = (0.5f - (kSignY0 + kSignH) / kCH) * kCardHm;
    const float z = sp.kind == kSpGlass ? 1.5f * kCardT + 0.0012f : 0.5f * kCardT + 0.001f;
    const XrVector3f q[4] = {{sx0, sy0, z}, {sx1, sy0, z}, {sx1, sy1, z}, {sx0, sy1, z}};
    sp.signBase = static_cast<int>(stat.size() / 6);
    sp.signDur = b.signDur;
    sp.signY = b.signY;
    sp.signZ = b.signZ;
    quad(b.signTex, q, XrVector3f{sp.signDone ? 1.05f : 0.0f, sp.signY, sp.signZ}, true);
  }
  // its figure (posed every frame), or its picture standing up
  sp.fig = b.figure;
  b.figure = nullptr;
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
    sp.fit = fit;  // r140: its place and size (and a pop-out's own move, when its pose file has one), measured posed
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
    const float spike = std::fabs(std::fmod(a / 6.2831853f * 12.0f + 12.0f, 1.0f) - 0.5f) * 2.0f;  // 1 at a spike's tip
    const float edge = 0.5f + 0.45f * spike, f = d < edge ? clampf((edge - d) / 0.12f, 0.0f, 1.0f) * (0.45f + 0.55f * d / edge) : 0.0f;
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
    if (r.parallel == kGoldTizm) part(1, 2, i, 0, 1, 1);
    if (r.hit != kHitNone) part(8 + r.hit, 0, i, 10 + r.hit, 1, 1);  // r149: a case hit's own tell (light pictures 9 - 11)
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
constexpr float kBoomHit[5] = {0.0f, 1.2f, 2.4f, 3.6f, 4.4f}, kBoomLen = 4.75f;
float ease_out(float x) {
  x = clampf(x, 0.0f, 1.0f);
  return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}
bool boom_pose(int k, float u, float t, XrVector3f* c, float* w, float* h, float* rot) {
  // (the words' places: r150 - an arc ABOVE the card, clear of its top edge, out toward you (Lance: anything that pops up
  // "need to be moved to pop up above it"); their sizes; their tilt; their spin in)
  static const XrVector3f kTo[3] = {{-0.050f, 0.140f, 0.045f}, {0.055f, 0.150f, 0.055f}, {0.000f, 0.205f, 0.050f}};
  static const float kW[3] = {0.135f, 0.115f, 0.105f}, kTilt[3] = {-0.16f, 0.20f, -0.12f}, kSpin[3] = {-1.1f, 1.2f, -0.9f};
  const float acy = (0.5f - 0.5f * (kHoleY0 + kHoleY1) / kCH) * kCardHm;
  if (k == 0) {
    if (u < 0.65f) {  // the first hit: a shock ring off the card - round it, its middle clear of the card from the start
      const float e = ease_out(u / 0.65f);
      *c = {0.0f, acy, 0.012f + 0.02f * e};
      *w = *h = 0.36f + 0.3f * e;
      *rot = u * 1.6f;
      return true;
    }
    for (int j = 0; j < 3; ++j) {  // a word bursting away: a ring of shards where it was (no bigger than it: above the card)
      const float b = u - kBoomHit[j + 2];
      if (b < 0.0f || b >= 0.3f) continue;
      const float e = ease_out(b / 0.3f);
      *c = kTo[j];
      *w = *h = kW[j] * (0.4f + 0.6f * e);
      *rot = b * 3.0f;
      return true;
    }
    return false;
  }
  const int j = k - 1;
  const float a = u - kBoomHit[j], b = u - kBoomHit[j + 2];
  if (a < 0.0f || b >= 0.2f) return false;
  // (out from behind its top edge - never across its face: behind the card till it's clear of it, then out toward you)
  const XrVector3f from{0.4f * kTo[j].x, 0.07f, -0.004f};
  const float e = ease_out(a / 0.3f);
  *c = v_add(from, v_mul(v_sub(kTo[j], from), e));
  c->z = -0.004f + (kTo[j].z + 0.004f) * smooth(clampf((e - 0.45f) / 0.55f, 0.0f, 1.0f));
  if (a > 0.3f) c->y += 0.003f * std::sin(t * 2.1f + 1.7f * j);  // (hanging there, bobbing)
  float s = a < 0.3f ? 0.15f + 1.1f * e : 1.0f + 0.25f * std::exp(-(a - 0.3f) * 7.0f) * std::cos((a - 0.3f) * 17.0f);
  if (b >= 0.0f) s *= b < 0.1f ? 1.0f + 4.5f * b : 1.45f * (1.0f - (b - 0.1f) / 0.1f);  // (its burst: up, then gone)
  *w = kW[j] * s;
  *h = 0.5f * kW[j] * s;
  *rot = kSpin[j] * (1.0f - ease_out(a / 0.45f)) + kTilt[j] + 0.05f * std::sin(t * 2.4f + j);
  return true;
}
// r150: how far above card i's top edge (up its own up, m at size 1) what stands on it reaches - a GOLD's figure / its
// picture, a BLACK's throne and figure, a TIZM BOOM's words while they're out - so a message floats over it, not on it
float special_above(int i) {
  for (const Special& sp : g_sp) {
    if (sp.i != i || !sp.up) continue;
    if (sp.kind == kSpGlass) return kPedH + (sp.fig != nullptr ? sp.fit.scale * sp.fit.bh : 0.8f * kThroneFig) + 0.006f;
    if (sp.kind == kSpPop) return sp.fig != nullptr ? kPopFig + 0.01f : 0.62f * kCardWm;
    if (sp.kind == kSpBoom && sp.boomNs != 0) return 0.231f + 0.01f - 0.5f * kCardHm;  // (its words' arc's top)
  }
  return 0.0f;
}
// a TIZM BOOM turned over in a pack: its show from now (its music started with it: reveal_fx)
void special_boom_start(int i) {
  for (Special& sp : g_sp)
    if (sp.i == i && sp.kind == kSpBoom && i >= 0) {
      sp.boomNs = mono_ns();
      if (i < kSheets) S.boomShown[i] = true;
    }
}

void special_moves(Special& sp, bool faceUp, bool held, float t, uint64_t now) {
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
      if (!faceUp || sp.boomNs == 0 || u < 0.0f || !boom_pose(o, u, t, &c, &w, &h, &rot)) {
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
    float prog = sp.signDone ? 1.05f : 0.0f;
    if (!sp.signDone && faceUp) {
      if (sp.signNs == 0) sp.signNs = now + 450000000ull;  // (just after it's round)
      if (now > sp.signNs) {
        prog = std::min(1.05f, secs(now, sp.signNs) / sp.signDur * 1.05f);
        if (prog >= 1.05f) sp.signDone = true;
      }
    }
    for (int q = 0; q < 4; ++q) {
      float* v = &sp.verts[static_cast<size_t>(sp.signBase + q) * 6];
      v[3] = prog;
      v[4] = sp.signY;
      v[5] = sp.signZ;
    }
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
    if (g.stage == 3) S.godDone = true;
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
    const Sheet& s = S.card[sp.i];
    const bool show = s.on && s.shown && s.scale > 0.01f && special_live(sp.i);
    const XrQuaternionf q = sheet_q(s);
    const bool faceUp = std::cos(s.flip) > 0.2f;
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
      if (!c.started && sp.fig != nullptr && sp.figCount > 0 && show && faceUp) {
        c.started = true;
        {
          std::lock_guard<std::mutex> lock(g_mutex);
          g_cameoSeen.insert(c.key);
          save_collection();
        }
        XrVector3f fwd = q_rot(S.head.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
        const float fl = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
        fwd = fl > 1e-3f ? XrVector3f{fwd.x / fl, 0.0f, fwd.z / fl} : XrVector3f{0.0f, 0.0f, -1.0f};
        const float floor = g_floorY > -1.0e8f ? g_floorY : S.head.position.y - 1.6f;
        c.spot = {S.head.position.x + fwd.x * kCamDist, floor, S.head.position.z + fwd.z * kCamDist};
        const float fps = std::max(1.0f, xrevo::fps(sp.fig));
        c.celeb = sp.loopFrames > 0 ? clampf((sp.loopFrames + sp.fit.bigFrames) / fps, 3.0f, 7.0f) : 4.0f;
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
          dim = std::max(dim, cd);
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
      dim = std::max(dim, sp.spot);
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
        float pt = sp.loopFrames > 0 ? (sp.loopStart + std::fmod(t * fps, static_cast<float>(sp.loopFrames))) / fps : t;
        if (sp.kind == kSpGlass && sp.loopFrames == 0) lift = 0.0025f * std::sin(t * 1.6f);
        if (camE >= 0.0f) {
          const Cameo& c = g_cam;
          const float out1 = kCamDim + kCamOut;
          if (c.t >= out1 && c.t < c.backAt && sp.loopFrames > 0) {  // its cheer, then its big move (round again)
            const float f = std::fmod((c.t - out1) * fps, static_cast<float>(sp.loopFrames + sp.fit.bigFrames));
            pt = (f < sp.loopFrames ? sp.loopStart + f : sp.fit.bigStart + (f - sp.loopFrames)) / fps;
          }
          feet.y -= kThroneTop;
        }
        xrevo::pose(sp.fig, pt, feet, sp.fit.yaw, sp.fit.scale, lift, &sp.posed);
        if (sp.home)  // r149: a HOMETOWN's figure stands in its landscape diorama: turned onto the card with it
          for (size_t v = 0; v + 5 < sp.posed.size(); v += 6) {
            const float px = sp.posed[v], py = sp.posed[v + 1], nx = sp.posed[v + 3], ny = sp.posed[v + 4];
            sp.posed[v] = -py;
            sp.posed[v + 1] = px;
            sp.posed[v + 3] = -ny;
            sp.posed[v + 4] = nx;
          }
        if (sp.posed.size() >= static_cast<size_t>(sp.figCount) * 6) std::memcpy(fo, sp.posed.data(), sizeof(float) * 6 * sp.figCount);
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
    if (faceUp && sp.kind == kSpPop) {
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
    md.under = sp.kind == kSpGlass ? -3.5f : (sp.kind == kSpPop || sp.kind == kSpBoom || sp.kind == kSpSign ? -2.9f : -3.0f);
    if (camE >= 0.0f) md.under = -2.9f;  // (r141: out of its card, nothing to clip it)
    if (sp.home)  // r149: a HOMETOWN's window, landscape (on the card: narrow across, long up it)
      md.glowRgb = pack_rgb(std::ceil(kHomeWinHalfY / kCardWm * 255.0f) / 255.0f, std::floor((-kHomeWinHalfX / kCardHm + 0.5f) * 255.0f) / 255.0f,
                            std::ceil((kHomeWinHalfX / kCardHm + 0.5f) * 255.0f) / 255.0f);
    else
      md.glowRgb = pack_rgb(std::ceil(kWinHalf / kCardWm * 255.0f) / 255.0f, std::floor((kWinBottom / kCardHm + 0.5f) * 255.0f) / 255.0f,
                            std::ceil((kWinTop / kCardHm + 0.5f) * 255.0f) / 255.0f);
    special_moves(sp, faceUp, held, t, now);  // r149: a TIZM BOOM's layers popping, an autograph writing itself
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
    dim = std::max(dim, fx_dim());
  } else if (g_fx.up) {
    fx_end(fxReady);
  }
  const float dimR = g_cam.i >= 0 && g_cam.started ? 2.6f : 1.35f;  // (the cameo stands inside the dimming)
  // the light
  if (!g_lightUp || g_lightVerts.size() != static_cast<size_t>(g_lightN) * 6) return;
  const XrVector3f up{0.0f, 1.0f, 0.0f};
  for (const LightPart& lp : g_lightParts) {
    if (lp.kind == 3 || lp.kind == 4) continue;
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
      light_quad(lp.base, top, facing(top), 0.11f * s->scale * upk, 0.07f * s->scale * upk);
    }
  }
  // the spotlight's beam on the BLACK and your surroundings dimmed under it
  const Special* lit = nullptr;
  for (const Special& sp : g_sp)
    if (sp.up && sp.kind == kSpGlass && sp.spot > 0.01f && (lit == nullptr || sp.spot > lit->spot)) lit = &sp;
  for (const LightPart& lp : g_lightParts) {
    if (lp.kind == 3) {
      if (lit == nullptr) {
        light_hide(lp);
        continue;
      }
      // r138 proof: a cone's two walls over the card read as a solid lampshade; now one soft shaft from above, turned
      // to your eyes, just behind the slab (the card stays crisp in front of it, lit), its foot fading just under it
      const Sheet& s = S.card[lit->i];
      const float k = lit->spot, len = 0.25f + 0.6f * k, r0 = 0.012f, r1 = (0.03f + 0.07f * k) * s.scale + 0.02f;
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
  if (g_page.load() == kPageTrade) {
    if (rt || lt) {
      g_page = rt ? kPageShop : kPageSets;
      g_tradeSay.clear();
      *redraw = true;
      return true;
    }
    std::vector<int> traded;
    int r = -1;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      std::vector<int> tiles;  // (the page's order: the most spares first)
      for (size_t i = 0; i < g_set.size(); ++i)
        if (spare_of(static_cast<int>(i)) > 0) tiles.push_back(static_cast<int>(i));
      std::stable_sort(tiles.begin(), tiles.end(), [](int x, int y) {
        return trade_before(x, spare_of(x), y, spare_of(y));  // (r147: the page's order)
      });
      const int n = static_cast<int>(tiles.size());
      if (in.step && n > 0) {
        int c = std::max(0, std::min(n - 1, g_tradeCursor));
        const int col = c % kTileCols;
        bool moved = false;
        if (in.dir == 1 && col > 0) --c, moved = true;
        if (in.dir == 2 && col < kTileCols - 1 && c + 1 < n) ++c, moved = true;
        if (in.dir == 3 && c >= kTileCols) c -= kTileCols, moved = true;
        if (in.dir == 4 && c + kTileCols < n) c += kTileCols, moved = true;
        *stick = moved || in.dir == 3 || in.dir == 4;  // (left / right at the grid's edge: the hub's pages)
        g_tradeCursor = c;
        const int row = c / kTileCols;
        if (row < g_tradeTop) g_tradeTop = row;
        if (row >= g_tradeTop + kTileRows) g_tradeTop = row - kTileRows + 1;
        if (moved) *redraw = true;
      }
      if (in.x && g_tradeCursor < n) {
        const std::string& key = g_set[tiles[static_cast<size_t>(g_tradeCursor)]].key;
        if (g_keep.count(key) != 0) g_keep.erase(key);
        else g_keep.insert(key);
        g_tradeSay = g_keep.count(key) != 0 ? upper(g_set[tiles[static_cast<size_t>(g_tradeCursor)]].name) + " KEPT OUT OF TRADE-INS"
                                            : upper(g_set[tiles[static_cast<size_t>(g_tradeCursor)]].name) + " CAN BE TRADED AGAIN";
        save_collection();
        *redraw = true;
      }
    }
    if (in.a) {
      r = trade_in(&traded);
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_tradeSay = r == 0 ? "TRADED! A FREE PACK IS WAITING WITH YOUR PACKS"
                            : (r == 1 ? "NOT 10 SPARE BASE CARDS YET (THE ONES YOU KEEP DON'T COUNT)" : "NO ROOM FOR MORE PACKS");
      }
      if (r == 0) trade_show(traded, now);
      else buzz(0.15f, 0.04f);
      *redraw = true;
    }
    return in.a || in.x || *stick;
  }
  // r146: SETS (LT from the trade-ins): the stick scrolls the evolution lines; RT the trade-ins
  if (g_page.load() == kPageSets) {
    if (rt) {
      g_page = kPageTrade;
      *redraw = true;
      return true;
    }
    if (in.step && (in.dir == 3 || in.dir == 4)) {
      std::lock_guard<std::mutex> lock(g_mutex);
      build_sets();
      const int most = std::max(0, static_cast<int>(g_lines.size()) - 9);
      const int before = g_setsTop;
      g_setsTop = std::max(0, std::min(most, g_setsTop + (in.dir == 4 ? 3 : -3)));
      *stick = true;
      if (g_setsTop != before) *redraw = true;
    }
    return lt || *stick;
  }
  // r140: the shop (LT from the binder's first pages): its two offers side by side (the stick), A buys, RT the binder;
  // r146: LT the trade-ins
  if (g_page.load() == kPageShop) {
    if (rt || lt) {
      g_page = rt ? kPageBinder : kPageTrade;
      g_tradeSay.clear();
      *redraw = true;
      return true;
    }
    if (in.step && (in.dir == 1 || in.dir == 2)) {
      const int want = in.dir == 1 ? 0 : 1;  // (left of the first / right of the last: the hub's pages)
      if (want != g_shopPick.load()) {
        g_shopPick = want;
        *redraw = true;
        *stick = true;
      }
    }
    if (in.step && (in.dir == 3 || in.dir == 4)) *stick = true;
    if (in.a) {
      const bool box = g_shopPick.load() == 1;
      const int r = buy_packs(box);
      {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_shopSay = r == 0 ? (box ? "A BOX OF 5 PACKS IS YOURS  -  RT, THEN A TO OPEN THEM" : "A PACK IS YOURS  -  RT, THEN A TO OPEN IT")
                           : (r == 1 ? "NOT ENOUGH TIZM COINS YET  -  PLAY FRANCHISE MATCHES" : "NO ROOM FOR MORE PACKS");
      }
      buzz(r == 0 ? 0.35f : 0.15f, r == 0 ? 0.08f : 0.04f);
      *redraw = true;
    }
    return in.a || lt || rt || *stick;
  }
  // the binder
  bool used = false;
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::vector<int> list = binder_list();
  const int spreads = std::max(1, static_cast<int>((list.size() + kPerSpread - 1) / kPerSpread));
  if (lt && g_spread > 0) {
    --g_spread;
    *redraw = true;
  } else if (lt) {  // r140: LT from the first pages: the shop
    g_page = kPageShop;
    g_shopSay.clear();
    *redraw = true;
  }
  if (rt && g_spread < spreads - 1) {
    ++g_spread;
    *redraw = true;
  }
  if (in.step) {
    const int page = g_cursor / 9, s = g_cursor % 9;
    int col = page * 3 + s % 3, row = s / 3;
    bool moved = false;
    if (in.dir == 3 && row > 0) --row, moved = true;
    if (in.dir == 4 && row < 2) ++row, moved = true;
    if (in.dir == 1 && col > 0) --col, moved = true;
    if (in.dir == 2 && col < 5) ++col, moved = true;
    *stick = moved || in.dir == 3 || in.dir == 4;  // (left / right at the binder's edge: the hub's pages)
    g_cursor = (col / 3) * 9 + row * 3 + col % 3;
    if (moved) *redraw = true;
  }
  const size_t at = static_cast<size_t>(g_spread) * kPerSpread + g_cursor;
  const int cursorCard = at < list.size() ? list[at] : -1;
  const bool owned = owns(cursorCard);
  // r149: a NEW card looked at - the cursor resting on its pocket a moment - isn't new any more
  if (cursorCard >= 0 && g_newCards.count(g_set[cursorCard].key) != 0) {
    if (S.newLook != cursorCard) {
      S.newLook = cursorCard;
      S.newLookNs = now;
    } else if (secs(now, S.newLookNs) > 0.8f) {
      g_newCards.erase(g_set[cursorCard].key);
      save_collection();
      S.newLook = -1;
      *redraw = true;
    }
  } else {
    S.newLook = -1;
  }
  if (in.a && !g_packs.empty()) {
    S.askOpen = true;
    used = true;
  } else if ((in.a || in.x) && owned) {
    S.askLook = cursorCard;
    used = true;
  } else if (in.a || in.x) {
    used = true;  // (a card you don't have yet: nothing to take out)
  }
  return used;
}

void hands(const bool grip[2], const bool have[2], const XrVector3f hand[2], const XrPosef* aim[2], const XrPosef* gripPose[2],
           uint64_t now) {
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
  hold_sheets(grip, have, hand, aim, now);
}

bool hand_busy(int h) { return h >= 0 && h < 2 && (S.hold[h] >= 0 || S.spent[h]); }

void triggers(const float pull[2], uint64_t now) {
  bool pressed[2];
  for (int h = 0; h < 2; ++h) {
    pressed[h] = !S.trigDown[h] && pull[h] > 0.6f;
    if (pull[h] > 0.6f) S.trigDown[h] = true;
    else if (pull[h] < 0.3f) S.trigDown[h] = false;
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

void close(bool fxReady) {
  S.askOpen = S.askTear = S.askAway = S.nextPack = false;
  S.askLook = -1;
  if (S.mode == kNone && S.packModel < 0 && S.cardModel < 0) return;
  if (S.mode == kPackUp && S.phase == kWaiting) unopen();
  end_scene(fxReady);
}

bool frame(xrfx::Scene& scene, const XrPosef& head, bool live, bool fxReady, const XrCompositionLayerQuad* hub, uint64_t now) {
  poll();
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
    tear(now, -1);
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
      xrfx::set_model(kCardSlot, grid_model(S.cardPics, parts, false));
      S.cardModel = S.cardPics->gen;
      S.cardVerts.assign(static_cast<size_t>(S.n) * (kCardVerts + kTagVerts) * 6, 0.0f);
      S.cardsNs = now;
      CLOG("the cards' pictures up (%d)%s", S.n, xrfx::model_up(kCardSlot) ? "" : " - failed");
    }
  }
  const bool cardsUp = S.cardPics && S.cardModel == S.cardPics->gen;
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
      s.flip = s.flipFrom + (s.flipTo - s.flipFrom) * smooth(t);
      bendTo += 5.0f * std::sin(3.14159265f * clampf(t, 0.0f, 1.0f));
      if (t >= 1.0f) {
        s.flip = s.flipTo;
        s.flipDur = 0.0f;
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
            place(s, l, pop);
          }
        } else {
          // to its place (the rest a place nearer as one goes round); the front one turned over out toward you
          const float want = S.depth[i] == 0 && S.faceUp[i] ? 1.0f : 0.0f;
          S.popK[i] += (want - S.popK[i]) * std::min(1.0f, dt * (want > S.popK[i] ? 9.0f : 12.0f));
          S.zAt[i] += (stack_z(S.depth[i]) - S.zAt[i]) * std::min(1.0f, dt * 14.0f);
          const float p = smooth(S.popK[i]);
          place(s, XrVector3f{0.0f, kPopY * p, S.zAt[i] + kPopZ * p}, p);
        }
        if (s.flipFx && s.flipDur > 0.0f && now >= s.flipNs && secs(now, s.flipNs) / s.flipDur >= 0.5f) {
          s.flipFx = false;
          reveal_fx(i);
        }
      }
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
        const XrVector3f to = S.hubValid ? S.hubPose.position : v_add(S.anchor, XrVector3f{0.0f, -0.8f, 0.0f});
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
  if (!fxReady) return false;
  bool shown = false;
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
  specials_frame(scene, dt, now, fxReady, hide);
  if (S.cardModel >= 0 && xrfx::model_up(kCardSlot) &&
      S.cardVerts.size() == static_cast<size_t>(S.n) * (kCardVerts + kTagVerts) * 6) {
    float* o = S.cardVerts.data();
    for (int i = 0; i < S.n; ++i) {
      const Sheet& s = S.card[i];
      const bool show = s.on && s.shown && s.scale > 0.01f;
      const XrQuaternionf q = sheet_q(s);
      card_box(o + static_cast<size_t>(i) * kCardVerts * 6, s, q, s.pos, s.scale, show && !hide[i]);  // r138: stiff, thick
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
