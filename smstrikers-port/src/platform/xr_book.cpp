// strikers-quest r199: the TIZM SPORTSBOOK (xr_book.h; SPORTSBOOK.md). The model, the bets, the book file and the one
// drawing the hub page and the cabinet's screen share.
#include "xr_book.h"

#include "xr_casino.h"  // r202: the machine's other games (SLOTS, ROULETTE) and its game picker
#include "xr_menu.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

namespace xrbook {

// =========================================================================================================== the model
namespace {
constexpr int kMaxGoals = 16;
// franchise.cpp poisson(): k counted while the running product stays over e^-lambda, at most 19 - a poisson truncated at 19
void poisson_pmf(double lam, double* p) {
  double sum = 0.0, term = std::exp(-lam);
  for (int k = 0; k < kMaxGoals; ++k) {
    p[k] = term;
    sum += term;
    term *= lam / (k + 1);
  }
  for (int k = 0; k < kMaxGoals; ++k) p[k] /= sum;  // (the tail past 15: < 1e-9 at these rates)
}
}  // namespace

Probs game_probs(double lamH, double lamA, double sh, double sa) {
  double ph[kMaxGoals], pa[kMaxGoals];
  poisson_pmf(std::max(0.05, lamH), ph);
  poisson_pmf(std::max(0.05, lamA), pa);
  const double q = (sh + sa) > 0.0 ? sh / (sh + sa) : 0.5;  // golden goal: home's share (sim_game's)
  Probs r;
  double tie = 0.0, by[kMaxGoals * 2] = {};
  for (int i = 0; i < kMaxGoals; ++i)
    for (int j = 0; j < kMaxGoals; ++j) {
      const double p = ph[i] * pa[j];
      if (i == j) tie += p;
      else by[i - j + kMaxGoals] += p;
    }
  r.margin[1] = by[1 + kMaxGoals] + tie * q;
  r.margin[2] = by[2 + kMaxGoals];
  r.margin[3] = by[3 + kMaxGoals];
  r.margin[4] = 0.0;
  for (int d = 4; d < kMaxGoals; ++d) r.margin[4] += by[d + kMaxGoals];
  r.homeWin = r.margin[1] + r.margin[2] + r.margin[3] + r.margin[4];
  r.margin[0] = 1.0 - r.homeWin;
  r.homeBy2 = r.margin[2] + r.margin[3] + r.margin[4];
  r.awayBy2 = 0.0;
  for (int d = 2; d < kMaxGoals; ++d) r.awayBy2 += by[-d + kMaxGoals];
  return r;
}

int american(double p, double vig) {
  const double q = p * (1.0 + vig);
  if (!(q > 0.0)) return 2500;
  if (q >= 0.96) return -2500;  // (a near-certain side: the floor; such a line is taken off the board, price_day)
  const double dec = 1.0 / q;
  int a;
  if (dec >= 2.0) {
    const double plus = (dec - 1.0) * 100.0;
    const int step = plus >= 1000.0 ? 50 : 5;
    a = static_cast<int>(std::floor(plus / step)) * step;  // (down: never more than the vigged line)
    a = std::max(100, a);
  } else {
    const double minus = 100.0 / (dec - 1.0);
    const int step = minus >= 1000.0 ? 50 : 5;
    a = -static_cast<int>(std::ceil(minus / step)) * step;  // (up in size: never more than the vigged line)
    if (a > -100) a = -100;
  }
  return std::max(-2500, std::min(2500, a));
}

long long payout(long long stake, int a) {
  if (stake <= 0) return 0;
  stake = xrcoin::clamp(stake);  // (r208: <= kMax ~ 2^50, x 2,500 still far inside 64 bits)
  if (a >= 100) return xrcoin::clamp(stake + (stake * a) / 100);
  if (a <= -100) return xrcoin::clamp(stake + (stake * 100) / (-a));
  return stake;
}

std::string spread_text(float s) {  // "+1.5" / "-1.5" (by hand: no printf flags needed)
  const int t10 = static_cast<int>(std::lround(std::fabs(s) * 10.0f));
  return std::string(s < 0.0f ? "-" : "+") + std::to_string(t10 / 10) + "." + std::to_string(t10 % 10);
}

std::string price_text(int a) {
  return (a > 0 ? "+" : "") + std::to_string(a);
}

void price_day(const PortFranchiseBookDay& d, std::vector<Fixture>* out) {
  out->clear();
  for (int i = 0; i < d.count && i < 16; ++i) {
    const PortFranchiseBookGame& g = d.games[i];
    Fixture f;
    f.home = g.home;
    f.away = g.away;
    f.mine = g.mine;
    f.series = g.series;
    f.streakH = g.streakH;
    f.streakA = g.streakA;
    f.fair = game_probs(g.lamH, g.lamA, g.sh, g.sa);
    // the posted line leans on form, the house's way only: a hot club (a win streak) is priced shorter - up to 3 games x
    // kFormLean of extra edge on it - and nobody is ever priced longer than the fair line (franchise.cpp's sim doesn't play
    // form, so leaning a cold club longer would hand the bettor an edge)
    const double hotH = 1.0 + kFormLean * std::max(0, std::min(3, g.streakH));
    const double hotA = 1.0 + kFormLean * std::max(0, std::min(3, g.streakA));
    Probs post = f.fair;    auto make = [&](int market, int side, float spread, double fair, double posted, double vig) {
      Pick p;
      p.game = i;
      p.market = market;
      p.side = side;
      p.spread = spread;
      p.fair = fair;
      p.posted = posted;
      p.american = (posted >= 0.955 || posted <= 0.02) ? 0 : american(posted, vig);  // 0 = OFF the board
      return p;
    };
    f.ml[0] = make(kMoneyLine, 0, 0.0f, f.fair.homeWin, f.fair.homeWin * hotH, kVigLines);
    f.ml[1] = make(kMoneyLine, 1, 0.0f, 1.0 - f.fair.homeWin, (1.0 - f.fair.homeWin) * hotA, kVigLines);
    const bool homeFav = post.homeWin >= 0.5;  // (the spread's favourite: the fair line's)
    if (homeFav) {
      f.sp[0] = make(kSpread, 0, -1.5f, f.fair.homeBy2, f.fair.homeBy2 * hotH, kVigLines);
      f.sp[1] = make(kSpread, 1, +1.5f, 1.0 - f.fair.homeBy2, (1.0 - f.fair.homeBy2) * hotA, kVigLines);
    } else {
      f.sp[0] = make(kSpread, 0, +1.5f, 1.0 - f.fair.awayBy2, (1.0 - f.fair.awayBy2) * hotH, kVigLines);
      f.sp[1] = make(kSpread, 1, -1.5f, f.fair.awayBy2, f.fair.awayBy2 * hotA, kVigLines);
    }
    if (g.mine) {
      // WIN BY: your margin's chances come from the two clubs' goal rates (two poissons: their difference, golden goal on a
      // tie), your rate moved so the expected margin is the model's blended with how you've really been winning this season
      // (the model worth kWinByPrior games, each of yours one) - the shape stays a real goal-difference distribution, each
      // bigger margin rarer. Then, the house's way only, each margin is posted at least kWinByStep likelier than the next
      // bigger one (a shorter price, never a longer one), so every bigger margin clearly pays more.
      const bool home = g.home == d.myClub;
      const double lamMe = home ? g.lamH : g.lamA, lamThem = home ? g.lamA : g.lamH;
      const double sMe = home ? g.sh : g.sa, sThem = home ? g.sa : g.sh;
      const int n = std::max(0, std::min(24, d.myMarginCount));
      double emp = 0.0;
      for (int k = 0; k < n; ++k) emp += std::max(-6, std::min(6, d.myMargins[k]));
      const double mu0 = lamMe - lamThem;
      const double target = (kWinByPrior * mu0 + emp) / (kWinByPrior + n);
      const double lamAdj = std::max(0.4, std::min(lamMe * 2.2, lamMe + (target - mu0)));
      const Probs mine = game_probs(lamAdj, lamThem, sMe, sThem);
      // r225: WIN BY k = by k OR MORE (a real book's -0.5 / -1.5 / -2.5 / -3.5): its chance P(margin >= k) from the same
      // buckets; the same house rule on top (each line posted at least 1 / kWinByStep likelier than the next: 2+ always pays
      // more than 1+, 3+ more than 2+ ...)
      const double fair[5] = {0, at_least(mine, 1), at_least(mine, 2), at_least(mine, 3), at_least(mine, 4)};
      double post[5] = {0, fair[1], fair[2], fair[3], fair[4]};
      for (int k = 3; k >= 1; --k) post[k] = std::max(post[k], post[k + 1] / kWinByStep);
      for (int k = 1; k <= 4; ++k) {
        f.winBy[k] = make(kWinBy, k, 0.0f, fair[k], post[k], kVigWinBy);
        // (WIN BY 1+ as a big favourite: past -2500 american() holds the price at -2500, under the edge - off the board then)
        if (post[k] * (1.0 + kVigWinBy) >= 0.96) f.winBy[k].american = 0;
      }
    }    out->push_back(f);
  }
}

// r207: a same-game pair's joint chance over the margin's four buckets (+-1.5 spreads split exactly there)
double sgp_joint(const Probs& p, int mlSide, int spSide, float spread) {
  const double pr[4] = {p.homeBy2, p.margin[1], std::max(0.0, 1.0 - p.homeWin - p.awayBy2), p.awayBy2};
  const int m[4] = {2, 1, -1, -2};
  double j = 0.0;
  for (int k = 0; k < 4; ++k) {
    const bool ml = mlSide == 0 ? m[k] > 0 : m[k] < 0;
    const int by = spSide == 0 ? m[k] : -m[k];
    if (ml && static_cast<float>(by) + spread > 0.0f) j += pr[k];
  }
  return j;
}

Pick sgp_pick(const Fixture& f, int mlSide, int spSide, float spread, int* why) {
  Pick p;
  p.market = kSgp;
  p.side = mlSide;
  p.side2 = spSide;
  p.spread = spread;
  p.fair = sgp_joint(f.fair, mlSide, spSide, spread);
  const int streak = mlSide == 0 ? f.streakH : f.streakA;
  p.posted = p.fair * (1.0 + kFormLean * std::max(0, std::min(3, streak)));  // (the house's way only, as price_day's)
  p.american = (p.posted >= 0.955 || p.posted <= 0.02) ? 0 : american(p.posted, kVigLines);
  if (why != nullptr) *why = p.posted <= 0.02 ? 1 : (p.posted >= 0.955 ? 2 : 0);
  return p;
}

double decimal(int a) {
  if (a >= 100) return 1.0 + a / 100.0;
  if (a <= -100) return 1.0 + 100.0 / -a;
  return 1.0;
}

// ---- r199 round 3: FUTURES. A deterministic look at each race as it stands (no simulation, no random numbers): the
// clubs by strength + record (the record counting more as the season goes), the players by awards night's own score
// projected to the season's end. Early in the season the field is wide open (long prices all round); it narrows as the
// weeks go. Each option priced on its own with kVigFutures on top.
void price_futures(const PortFranchiseBookFutures& f, std::vector<Pick> out[4]) {
  for (int m = 0; m < 4; ++m) out[m].clear();
  if (!f.valid) return;
  int played = 0;
  for (int i = 0; i < PORT_FR_CLUBS; ++i) played = std::max(played, f.clubs[i].wins + f.clubs[i].losses);
  const double prog = std::max(0.0, std::min(1.0, played / static_cast<double>(PORT_FR_WEEKS)));
  const double left = std::max(0.0, PORT_FR_WEEKS - static_cast<double>(played));
  auto finish = [&](std::vector<std::pair<double, Pick>>& opts, std::vector<Pick>& dst) {
    double mx = -1e30, sum = 0.0;
    for (const auto& o : opts) mx = std::max(mx, o.first);
    for (auto& o : opts) sum += (o.second.fair = std::exp(o.first - mx));
    std::sort(opts.begin(), opts.end(), [](const std::pair<double, Pick>& a, const std::pair<double, Pick>& b) {
      return a.first != b.first ? a.first > b.first : a.second.who < b.second.who;
    });
    for (size_t i = 0; i < opts.size() && i < 8; ++i) {
      Pick p = opts[i].second;
      p.fair = p.posted = p.fair / std::max(1e-12, sum);
      p.american = p.fair >= 0.955 ? 0 : american(p.fair, kVigFutures);
      dst.push_back(p);
    }
  };
  {  // CHAMPION
    double mean = 0.0, var = 0.0;
    for (int i = 0; i < PORT_FR_CLUBS; ++i) mean += f.clubs[i].strength / PORT_FR_CLUBS;
    for (int i = 0; i < PORT_FR_CLUBS; ++i) var += (f.clubs[i].strength - mean) * (f.clubs[i].strength - mean) / PORT_FR_CLUBS;
    const double sd = std::max(0.5, std::sqrt(var));
    std::vector<std::pair<double, Pick>> o;
    for (int i = 0; i < PORT_FR_CLUBS; ++i) {
      const auto& c = f.clubs[i];
      const int g = c.wins + c.losses;
      const double pct = g > 0 ? static_cast<double>(c.wins) / g : 0.5;
      Pick p;
      p.market = kChampion;
      p.club = i;
      p.who = "club:" + std::to_string(i);
      const char* pl = PortFranchiseBookClubPlace(i);
      p.whoName = pl != nullptr ? pl : "";
      p.stat = std::to_string(c.wins) + "-" + std::to_string(c.losses);
      o.push_back({1.1 * (c.strength - mean) / sd + 4.0 * (pct - 0.5) * prog, p});
    }
    finish(o, out[0]);
  }
  auto players = [&](int market) {
    std::vector<std::pair<double, Pick>> o;
    const double sigma = market == kMvp ? 4.0 + 22.0 * (1.0 - prog) : 1.2 + 5.0 * (1.0 - prog);
    for (int k = 0; k < f.candCount; ++k) {
      const PortFranchiseBookCand& c = f.cands[k];
      if (market == kMvp && !c.captain) continue;  // (awards night's MVP: captains and backups)
      if (market == kRookie && !c.rookie) continue;
      const auto& cl = f.clubs[std::max(0, std::min(PORT_FR_CLUBS - 1, c.club))];
      const int g = std::max(1, cl.wins + cl.losses);
      // goals a game: his own so far, leaning on a prior (a captain ~0.6, a sidekick ~0.25) worth 4 games
      const double gpg = (c.goals + 4.0 * (c.captain ? 0.6 : 0.25)) / (cl.wins + cl.losses + 4.0);
      double score;
      if (market == kMvp) {
        const double winPct = static_cast<double>(cl.wins) / g;
        score = (c.goals * 3.0 + cl.wins * 2.0 + c.form + left * (3.0 * gpg + 2.0 * winPct)) / sigma;
      } else {
        score = (c.goals + left * gpg) / sigma;
      }
      Pick p;
      p.market = market;
      p.club = c.club;
      p.who = c.id;
      p.whoName = c.name;
      p.stat = std::to_string(c.goals) + (c.goals == 1 ? " GOAL" : " GOALS");
      o.push_back({score, p});
    }
    finish(o, out[market - kChampion]);
  };
  players(kMvp);
  players(kBoot);
  players(kRookie);
}

double parlay_boost(const std::vector<Leg>& legs, bool settledOnly) {
  double ret = 1.0;
  int n = 0;
  for (const Leg& l : legs) {
    if (settledOnly ? l.state != 1 : l.state == 3) continue;
    if (l.market == kWinBy) continue;  // (a bet on yourself keeps its own edge)
    if (!(l.fair > 0.0)) return 1.0;   // (an older book's leg: as it was priced then, no cap)
    ret *= l.fair * decimal(l.american);
    ++n;
  }
  if (n < 2 || ret >= kParlayFloor || !(ret > 1e-12)) return 1.0;
  return kParlayFloor / ret;
}

double multiplier(const Bet& b, bool settledOnly) {
  double m = 1.0;
  for (const Leg& l : b.legs)
    if (!settledOnly ? l.state != 3 : l.state == 1) m *= decimal(l.american);
  return m * parlay_boost(b.legs, settledOnly);
}

long long parlay_pays(long long stake, double mult) {
  if (stake <= 0 || !(mult > 0.0)) return 0;
  // r208 (Lance: "dont cap the ticket wins"): no win cap - floor(stake x mult) in 64-bit coins (a 25-leg parlay's multiplier can
  // pass 10^30: held at xrcoin::kMax, never wrapping). kMax < 2^53: every amount up to it is exact in a double.
  const double raw = std::floor(static_cast<double>(xrcoin::clamp(stake)) * mult + 1e-9);
  return xrcoin::from_double(raw);
}

int leg_state(const Leg& l, int hg, int ag) {
  const int homeBy = hg - ag;  // (a golden goal is in the score: never 0)
  if (l.market == kSgp) {      // r207: the same-game pair - its money line side wins AND its spread side covers
    const bool ml = (l.side == 0) == (homeBy > 0);
    const int by = l.side2 == 0 ? homeBy : -homeBy;
    return ml && static_cast<float>(by) + l.spread > 0.0f ? 1 : 2;
  }
  if (l.market == kMoneyLine) return (l.side == 0) == (homeBy > 0) ? 1 : 2;
  if (l.market == kSpread) {
    const int by = l.side == 0 ? homeBy : -homeBy;  // this side's margin
    return static_cast<float>(by) + l.spread > 0.0f ? 1 : 2;
  }
  const int my = l.spread < 0.0f ? -homeBy : homeBy;  // WIN BY: your margin (spread -1: you were away)
  if (my <= 0) return 2;
  if (l.orMore) return my >= std::max(1, l.side) ? 1 : 2;  // r225: by `side` or more
  return (l.side >= 4 ? my >= 4 : my == l.side) ? 1 : 2;   // (an older book's leg: as it was bought - by exactly)
}

int ticket_state(const Bet& b, long long* paid) {
  bool open = false, anyWon = false;
  for (const Leg& l : b.legs) {
    if (l.state == 2) {
      *paid = 0;
      return 2;  // (one leg down: the ticket is lost, whatever's still to play)
    }
    if (l.state == 0) open = true;
    if (l.state == 1) anyWon = true;
  }
  if (open) {
    *paid = 0;
    return 0;
  }
  if (!anyWon) {
    *paid = b.stake;
    return 3;  // (every leg void: the stake back)
  }
  *paid = parlay_pays(b.stake, multiplier(b, true));
  return 1;
}

std::string leg_words(const Leg& l) {
  const std::string pr = price_text(l.american);
  if (l.market == kWinBy) {
    if (!l.orMore) return std::string("WIN BY ") + (l.side >= 4 ? "4+" : std::to_string(l.side)) + " " + pr;  // (an older book's)
    const int k = std::max(1, std::min(4, l.side));  // r225: "WIN BY 2+ (-1.5) +150"
    return "WIN BY " + std::to_string(k) + "+ (-" + std::to_string(k - 1) + ".5) " + pr;
  }
  if (l.market == kSgp) {  // r207: "MUTE CITY ML + -1.5 +210" / "MUTE CITY ML + DEATH MOUNTAIN +1.5 +480"
    const char* a = PortFranchiseBookClubPlace(l.side == 0 ? l.home : l.away);
    const char* b = PortFranchiseBookClubPlace(l.side2 == 0 ? l.home : l.away);
    const std::string wa = a != nullptr && a[0] != '\0' ? a : "?", wb = b != nullptr && b[0] != '\0' ? b : "?";
    return wa + " ML + " + (l.side == l.side2 ? std::string() : wb + " ") + spread_text(l.spread) + " " + pr;
  }
  if (l.market == kChampion) {
    const char* pl = PortFranchiseBookClubPlace(l.club);
    return std::string("CHAMPION: ") + (pl != nullptr && pl[0] ? pl : "?") + " " + pr;
  }
  if (is_future_market(l.market) && l.market >= kMvp) {
    static const char* const kWhat[3] = {"MVP: ", "GOLDEN BOOT: ", "ROOKIE OF THE YEAR: "};
    return kWhat[std::min(2, l.market - kMvp)] + l.whoName + " " + pr;
  }
  const char* place = PortFranchiseBookClubPlace(l.side == 0 ? l.home : l.away);
  const std::string who = place != nullptr && place[0] != '\0' ? place : "?";
  if (l.market == kMoneyLine) return who + " ML " + pr;
  return who + " " + spread_text(l.spread) + " " + pr;
}

// ========================================================================================================== the book
namespace {
std::mutex g_m;
Wallet g_wallet;
const uint8_t* (*g_logo)(int, int*, int*) = nullptr;
int g_gen = 1;

struct Book {
  unsigned career = 0;
  bool loaded = false;
  uint32_t nextId = 1001;
  int lockSeason = -1, lockWeek = -1;
  std::vector<Bet> bets;  // newest last
  // what was trimmed off the history (older settled tickets): their totals
  int oldBets = 0, oldWon = 0, oldLost = 0;
  long long oldStaked = 0, oldReturned = 0;
  long long bigPays = 0;  // r208: 64-bit
  std::string bigWords;
} g_book;

PortFranchiseBookDay g_day{};
bool g_dayValid = false;
std::vector<Fixture> g_fx;
std::vector<int> g_lines;  // LEAGUE LINES: the fixtures shown (not yours)
int g_mine = -1;           // your fixture today (-1 none)
double g_lastRead = -1e9;
// r199 round 3: the season's futures (read with the day), their boards (CHAMPION, MVP, GOLDEN BOOT, ROOKIE)
PortFranchiseBookFutures g_fut{};
std::vector<Pick> g_futOpts[4];
int g_futMarket = 0;
constexpr int kTabLines = 0, kTabWinBy = 1, kTabFutures = 2, kTabBets = 3, kTabs = 4;

// the UI's state
int g_tab = 0;        // kTab*: LEAGUE LINES, WIN BY, FUTURES, MY BETS (the machine: SLIPS)
int g_cursor = 0;     // LINES: a line (2 a fixture: away, home); WIN BY: 0-3; MY BETS: a row (0 = COLLECT while there is some)
int g_col = 0;        // LINES: 0 money line, 1 spread
int g_focus = 0;      // 0 the board, 1 the slip
int g_slipRow = 0;    // the slip: its legs, then the stake row, then PLACE BET
int g_stake = 2;      // 0-3 kStakes, 4 MAX, r207: 5 CUSTOM (g_custom)
struct SlipLeg {
  Pick p;
  int home = -1, away = -1;  // (a future: -1, its club / player in p)
  int week = -1;             // r207: its game's matchday (a future: -1)
};
std::vector<SlipLeg> g_slip;
// r207 (Lance: CRAZY PARLAYS): LEAGUE LINES' week picker - 0 this matchday, k the k-th one ahead (the regular season's);
// the upcoming matchdays' boards (read on the game thread as they're wanted; the drawing only reads them)
int g_weekAhead = 0;
struct WeekBoard {
  int week = -1;
  PortFranchiseBookDay day{};
  std::vector<Fixture> fx;
  std::vector<int> lines;
};
std::vector<WeekBoard> g_weeks;
// r207: the CUSTOM stake (0 none typed yet) and its number pad: open, the key under the cursor (0-8 = 1-9, 9 CLEAR, 10 = 0,
// 11 BACKSPACE, 12 OK), what's typed
long long g_custom = 0;  // r208: 64-bit (up to the whole balance, 15 digits)
bool g_padOpen = false;
int g_padKey = 0;
std::string g_padText;
bool g_padFresh = false;  // (opened showing the stake already set: the first digit typed starts a new number)
constexpr int kPadKeys = 13, kPadOk = 12, kPadClear = 9, kPadBack = 11;
std::string g_msg;
uint32_t g_msgRgb = 0xFFD23F;
double g_msgAt = -1e9, g_now = 0.0, g_flashAt = -1e9, g_winAt = -1e9, g_collectAt = -1e9, g_multAt = -1e9;
double g_multFrom = 1.0;
long long g_collectCoins = 0;  // r208: 64-bit
bool g_collectMachine = false;
std::vector<uint32_t> g_printQ;
long long g_machineCollect = 0;
// the hit boxes of what the drawing last showed (base page coordinates): kind 0 a price (a, b = line, col), 1 a WIN BY
// tile (a), 2 a stake chip (a), 3 PLACE BET, 4 a tab (a), 5 a bet row (a), 6 a slip leg (a); r207: 10 a pad key (a), 11 the
// week picker's arrows (a -1 / +1), 12 the week picker itself; r224: 13 a list's up / down arrow (a -1 / +1, b the list)
struct Hit {
  float x0, y0, x1, y1;
  int kind, a, b;
};
std::vector<Hit> g_hits;
// r204 (Lance's r203 test: "the highlight doesn't move at all" + ticks from his drift): when the selection last really moved and
// when it was last picked (g_now's clock: the cursor's pop and press), the pointer's sticky hover box, and a step's ticks held
// back until it is known the selection moved (no sound for "nothing that way")
double g_moveAt = -1e9, g_pressAt = -1e9;
float g_hover[5] = {0, 0, 0, 0, 0};
bool g_deferTicks = false;
float g_tickWant = 0.0f;
// r224 (Lance 11-12: "4 games i just cant bet on bc anytime i select em the shit scrolls up or down"): each list's FIRST ROW
// SHOWN is state (r223 re-centred the list on the cursor every draw - the pointer hovering a row moved the cursor, the list
// re-centred under it, a new row was under the pointer, and it ran to an end). The window only moves when the cursor leaves it
// (the stick / buttons: one row at a time at its edges) or by its arrows (the pointer: a page); a pointer hovering a row never
// moves it. Kept up by keep_in_view() on the input / game side (g_m held) - the drawing only reads them.
constexpr int kLinesRows = 5, kLegsShown = 4;
int g_linesTop = 0;  // LEAGUE LINES: the first game shown
int g_betsTop = 0;   // MY BETS / SLIPS: the first ticket row shown (under COLLECT)
int g_legsTop = 0;   // the slip: the first leg shown
size_t g_legsSeen = 0;  // (the slip's size when last kept: a leg added shows the newest, as r207 did)

std::string home_dir() {
  const char* h = std::getenv("STRIKERS_HOME");
  return std::string(h != nullptr && *h != '\0' ? h : "/storage/emulated/0/Strikers");
}
std::string book_path(unsigned career) { return home_dir() + "/cards/book-" + std::to_string(career) + ".txt"; }
std::string coins_text_book(long long n) { return xrcoin::text(n); }  // "1,234,567" (r208: xr_coins.h's)

void say(const std::string& m, uint32_t rgb) {
  g_msg = m;
  g_msgRgb = rgb;
  g_msgAt = g_now;
}
void sound(const char* n, float gain) {
  if (g_deferTicks && std::strcmp(n, "book_tick") == 0) {  // (r204: an input's tick waits to see whether anything moved)
    g_tickWant = std::max(g_tickWant, gain);
    return;
  }
  if (g_wallet.sound != nullptr) g_wallet.sound(n, gain);
}

void save_book() {  // (g_m held) written whole: a temp file, then renamed
  if (g_book.career == 0) return;
  const std::string path = book_path(g_book.career), tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) return;
  std::fprintf(f, "tizm-sportsbook 3\nnext %u\nlock %d %d\nold %d %d %d %lld %lld\n", g_book.nextId, g_book.lockSeason,
               g_book.lockWeek, g_book.oldBets, g_book.oldWon, g_book.oldLost, g_book.oldStaked, g_book.oldReturned);
  // r208: the coin fields 64-bit text (an r207 book's numbers read back the same)
  std::fprintf(f, "big %lld %s\n", g_book.bigPays, g_book.bigWords.empty() ? "-" : g_book.bigWords.c_str());
  for (const Bet& b : g_book.bets) {
    std::fprintf(f, "ticket %u %d %d %lld %lld %d %lld %d %d %lld %d\n", b.id, b.season, b.week, b.stake, b.pays, b.state, b.paid,
                 b.collected ? 1 : 0, b.machine ? 1 : 0, b.placed, static_cast<int>(b.legs.size()));
    for (const Leg& l : b.legs) {
      std::string nm = l.whoName.empty() ? std::string("-") : l.whoName;  // (a future's player: spaces kept as _)
      for (char& ch : nm) ch = ch == ' ' ? '_' : ch;
      // r207: + the leg's matchday, its chance when placed (millionths), a same-game pair's spread side (after r199's 13 fields:
      // an r206 build reading this keeps its 13; this build reading an r206 book gets week -1 = the ticket's, fair 0 = no cap)
      // r225: + a 17th field, 1 = a WIN BY leg means "or more" (an r224 build reads its 16 and stops)
      std::fprintf(f, "leg %d %d %d %d %d %d %d %d %d %d %d %s %s %d %d %d %d\n", l.home, l.away, l.market, l.side,
                   static_cast<int>(std::lround(l.spread * 10.0f)), l.american, l.state, l.hg, l.ag, l.mine ? 1 : 0, l.club,
                   l.who.empty() ? "-" : l.who.c_str(), nm.c_str(), l.week, static_cast<int>(std::lround(l.fair * 1e6)), l.side2,
                   l.orMore ? 1 : 0);
    }
  }
  std::fclose(f);
  std::rename(tmp.c_str(), path.c_str());
}

void load_book(unsigned career) {  // (g_m held)
  g_book = Book();
  g_book.career = career;
  g_book.loaded = true;
  FILE* f = std::fopen(book_path(career).c_str(), "r");
  if (f == nullptr) return;
  char line[512];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    char words[400] = {};
    if (std::strncmp(line, "ticket ", 7) == 0) {
      // r208: the 11 fields read as 64-bit numbers (strtoll: an r199-r207 book's 32-bit ones the same; coins held to 0..kMax)
      Bet b;
      long long v[11] = {};
      int got = 0;
      const char* p = line + 7;
      while (got < 11 && xrcoin::parse(p, &v[got], &p)) ++got;
      if (got == 11) {
        b.id = static_cast<uint32_t>(v[0]);
        b.season = static_cast<int>(v[1]);
        b.week = static_cast<int>(v[2]);
        b.stake = xrcoin::clamp(v[3]);
        b.pays = xrcoin::clamp(v[4]);
        b.state = static_cast<int>(v[5]);
        b.paid = xrcoin::clamp(v[6]);
        b.collected = v[7] != 0;
        b.machine = v[8] != 0;
        b.placed = v[9];
        g_book.bets.push_back(b);
      }
    } else if (std::strncmp(line, "leg ", 4) == 0 && !g_book.bets.empty()) {
      Leg l;
      int sp10 = 0, mine = 0, wk = -1, fair6 = 0, side2 = 0, orMore = 0;
      char who[64] = {}, nm[64] = {};
      const int got = std::sscanf(line + 4, "%d %d %d %d %d %d %d %d %d %d %d %63s %63s %d %d %d %d", &l.home, &l.away, &l.market, &l.side,
                                  &sp10, &l.american, &l.state, &l.hg, &l.ag, &mine, &l.club, who, nm, &wk, &fair6, &side2, &orMore);
      l.orMore = got >= 17 && orMore != 0;  // r225 (an older book's WIN BY leg: by exactly, as it was bought)
      if (got >= 13) {
        l.who = std::strcmp(who, "-") == 0 ? std::string() : std::string(who);
        l.whoName = std::strcmp(nm, "-") == 0 ? std::string() : std::string(nm);
        for (char& ch : l.whoName) ch = ch == '_' ? ' ' : ch;
      }
      if (got >= 16) {  // r207's fields (an r199-r206 book: the ticket's week, no cap)
        l.week = wk;
        l.fair = std::max(0, fair6) * 1e-6;
        l.side2 = side2 != 0 ? 1 : 0;
      }
      if (got >= 10 &&
          g_book.bets.back().legs.size() < static_cast<size_t>(kMaxLegs)) {
        l.spread = sp10 / 10.0f;
        l.mine = mine != 0;
        g_book.bets.back().legs.push_back(l);
      }
    } else if (std::strncmp(line, "next ", 5) == 0) {
      std::sscanf(line + 5, "%u", &g_book.nextId);
    } else if (std::strncmp(line, "lock ", 5) == 0) {
      std::sscanf(line + 5, "%d %d", &g_book.lockSeason, &g_book.lockWeek);
    } else if (std::strncmp(line, "old ", 4) == 0) {
      std::sscanf(line + 4, "%d %d %d %lld %lld", &g_book.oldBets, &g_book.oldWon, &g_book.oldLost, &g_book.oldStaked,
                  &g_book.oldReturned);
    } else if (std::strncmp(line, "big ", 4) == 0) {  // r208: its coins 64-bit (strtoll)
      long long big = 0;
      const char* p = line + 4;
      if (xrcoin::parse(p, &big, &p)) {
        g_book.bigPays = xrcoin::clamp(big);
        // r229: a biggest win saved before the 32 new clubs names the old club ("MUTE CITY ML -150"): renamed as it loads
        if (std::sscanf(p, " %399[^\n]", words) >= 1)
          g_book.bigWords = std::strcmp(words, "-") == 0 ? std::string() : std::string(PortFranchiseRenameOldClubs(words));
      }
    }
  }
  std::fclose(f);
  // (a ticket without legs is no ticket)
  g_book.bets.erase(std::remove_if(g_book.bets.begin(), g_book.bets.end(), [](const Bet& b) { return b.legs.empty(); }), g_book.bets.end());
  for (const Bet& b : g_book.bets) g_book.nextId = std::max(g_book.nextId, b.id + 1);
}

// keep the history to the newest 160 settled + collected tickets (+ every open / uncollected one); the rest in the totals
void trim_history() {  // (g_m held)
  int done = 0;
  for (const Bet& b : g_book.bets) done += b.state != 0 && b.collected ? 1 : 0;
  for (size_t i = 0; i < g_book.bets.size() && done > 160;) {
    const Bet& b = g_book.bets[i];
    if (b.state == 0 || !b.collected) {
      ++i;
      continue;
    }
    if (b.state != 3) {
      ++g_book.oldBets;
      g_book.oldStaked = xrcoin::add(g_book.oldStaked, b.stake);  // (r208: held, never wrapping)
      if (b.state == 1) {
        ++g_book.oldWon;
        g_book.oldReturned = xrcoin::add(g_book.oldReturned, b.paid);
      } else {
        ++g_book.oldLost;
      }
    }
    g_book.bets.erase(g_book.bets.begin() + static_cast<long>(i));
    --done;
  }
}

bool locked_now() {  // (g_m held) the day's bets are closed
  return !g_dayValid || g_day.locked || (g_book.lockSeason == g_day.season && g_book.lockWeek == g_day.week);
}

long long balance() { return g_wallet.balance != nullptr ? g_wallet.balance() : 0; }  // r208: 64-bit
long long stake_coins(int idx, long long bal) {
  if (idx == kStakeCustom) return std::max(0LL, g_custom);  // r207 (placing it checks it against the balance)
  if (idx >= kStakeMax) return std::max(0LL, bal);
  return kStakes[std::max(0, std::min(3, idx))];
}

struct Totals {
  int bets = 0, won = 0, lost = 0, open = 0, readyTickets = 0;
  long long openStake = 0, ready = 0;  // r208: 64-bit
  long long staked = 0, returned = 0;
};
Totals totals() {  // (g_m held)
  Totals t;
  t.bets = g_book.oldBets;
  t.won = g_book.oldWon;
  t.lost = g_book.oldLost;
  t.staked = g_book.oldStaked;
  t.returned = g_book.oldReturned;
  for (const Bet& b : g_book.bets) {
    if (b.state == 0) {
      ++t.open;
      t.openStake = xrcoin::add(t.openStake, b.stake);
      continue;
    }
    if ((b.state == 1 || b.state == 3) && !b.collected) {
      t.ready = xrcoin::add(t.ready, b.paid);  // (r208: held at kMax, never wrapping)
      ++t.readyTickets;
    }
    if (b.state == 3) continue;
    ++t.bets;
    t.staked = xrcoin::add(t.staked, b.stake);
    if (b.state == 1) {
      ++t.won;
      t.returned = xrcoin::add(t.returned, b.paid);
    } else {
      ++t.lost;
    }
  }
  return t;
}

bool is_future(int market) { return is_future_market(market); }

// ---- r207: the upcoming matchdays' boards (LEAGUE LINES' week picker; legs on them)
const WeekBoard* find_week(int week) {  // (g_m held; any thread: reads only)
  for (const WeekBoard& w : g_weeks)
    if (w.week == week) return &w;
  return nullptr;
}
bool g_weeksDirty = false;  // (a week picked / a leg added from one: frame() reads its board at once)
int max_ahead() {           // (g_m held) how many matchdays ahead the picker goes: the rest of the regular season
  if (!g_dayValid || g_day.phase != PORT_FR_PHASE_REGULAR) return 0;
  return std::max(0, std::min(PORT_FR_WEEKS - g_day.week, 19));
}
int shown_week() { return g_dayValid ? g_day.week + g_weekAhead : -1; }
const std::vector<Fixture>& shown_fx() {
  static const std::vector<Fixture> kNone;
  if (g_weekAhead <= 0) return g_fx;
  const WeekBoard* w = find_week(shown_week());
  return w != nullptr ? w->fx : kNone;
}
const std::vector<int>& shown_lines() {
  static const std::vector<int> kNone;
  if (g_weekAhead <= 0) return g_lines;
  const WeekBoard* w = find_week(shown_week());
  return w != nullptr ? w->lines : kNone;
}
// a game's fixture on its matchday's board (today's or an upcoming one's read already); null: not on one
const Fixture* fixture_for(int week, int home, int away) {
  const std::vector<Fixture>* fx = nullptr;
  if (g_dayValid && week == g_day.week) fx = &g_fx;
  else if (const WeekBoard* w = find_week(week)) fx = &w->fx;
  if (fx == nullptr) return nullptr;
  for (const Fixture& f : *fx)
    if (f.home == home && f.away == away) return &f;
  return nullptr;
}
const Pick* fixture_pick(const Fixture& f, int market, int side) {
  if (market == kWinBy) return &f.winBy[std::max(1, std::min(4, side))];
  return market == kMoneyLine ? &f.ml[side & 1] : &f.sp[side & 1];
}

// the pick under the LINES cursor (line = fixture * 2 + 0 away / 1 home on the board) - r207: on the matchday the picker shows
bool line_pick(int line, int col, Pick* out) {
  const int fi = line / 2;
  const std::vector<int>& lines = shown_lines();
  const std::vector<Fixture>& fx = shown_fx();
  if (fi < 0 || fi >= static_cast<int>(lines.size())) return false;
  const Fixture& f = fx[static_cast<size_t>(lines[static_cast<size_t>(fi)])];
  const int side = (line % 2) == 0 ? 1 : 0;  // (away first, like the boards in Vegas)
  *out = col == 0 ? f.ml[side] : f.sp[side];
  out->week = shown_week();
  return true;
}

// r207: the slip as a ticket's legs - a game's money line + spread merged into one SAME-GAME leg (priced from their joint
// chance), the rest one leg each. *err (may be null): why it can't go ("" fine)
std::vector<Leg> slip_to_legs(std::string* err) {  // (g_m held)
  std::vector<Leg> out;
  if (err != nullptr) err->clear();
  std::vector<bool> used(g_slip.size(), false);
  for (size_t i = 0; i < g_slip.size(); ++i) {
    if (used[i]) continue;
    const SlipLeg& s = g_slip[i];
    Leg l;
    if (is_future(s.p.market)) {
      l.market = s.p.market;
      l.club = s.p.club;
      l.who = s.p.who;
      l.whoName = s.p.whoName;
      l.side = s.p.club;
      l.american = s.p.american;
      l.fair = s.p.fair;
      out.push_back(l);
      continue;
    }
    l.home = s.home;
    l.away = s.away;
    l.week = s.week;
    l.mine = g_dayValid && (s.home == g_day.myClub || s.away == g_day.myClub);
    size_t j = i + 1;
    for (; j < g_slip.size(); ++j)
      if (!used[j] && !is_future(g_slip[j].p.market) && g_slip[j].home == s.home && g_slip[j].away == s.away && g_slip[j].week == s.week) break;
    if (j < g_slip.size()) {  // its partner on the same game: one same-game leg
      used[j] = true;
      const SlipLeg& t = g_slip[j];
      const SlipLeg& ml = s.p.market == kMoneyLine ? s : t;
      const SlipLeg& sp = s.p.market == kMoneyLine ? t : s;
      const Fixture* f = fixture_for(s.week, s.home, s.away);
      int why = 1;
      const Pick q = f != nullptr ? sgp_pick(*f, ml.p.side, sp.p.side, sp.p.spread, &why) : Pick();
      l.market = kSgp;
      l.side = q.side;
      l.side2 = q.side2;
      l.spread = q.spread;
      l.american = f != nullptr ? q.american : 0;
      l.fair = q.fair;
      if ((why != 0 || l.american == 0) && err != nullptr && err->empty()) *err = "A SAME-GAME PAIR IS OFF THE BOARD: TAKE A LEG OFF";
      out.push_back(l);
      continue;
    }
    l.market = s.p.market;
    l.side = s.p.side;
    l.spread = s.p.market == kWinBy ? (g_dayValid && s.home == g_day.myClub ? 1.0f : -1.0f) : s.p.spread;
    l.american = s.p.american;
    l.fair = s.p.fair;
    l.orMore = s.p.market == kWinBy;  // r225: WIN BY k = by k or more
    out.push_back(l);
  }
  return out;
}
double slip_mult() {  // (g_m held) the ticket's multiplier (r207: same-game pairs priced together, the parlay cap's boost in it)
  Bet b;
  b.legs = slip_to_legs(nullptr);
  return b.legs.empty() ? 1.0 : multiplier(b, false);
}

bool futures_open() {  // (g_m held) the regular season, the day not kicked off
  return g_fut.valid && g_fut.phase == PORT_FR_PHASE_REGULAR &&
         !(g_dayValid && (g_day.locked || (g_book.lockSeason == g_day.season && g_book.lockWeek == g_day.week)));
}
// r207: the slip can't be placed now - a leg on today's games with today kicked off, or a future with the futures shut (an
// upcoming matchday's legs stay open till theirs)
bool slip_locked() {  // (g_m held)
  for (const SlipLeg& s : g_slip) {
    if (is_future(s.p.market) ? !futures_open() : ((!g_dayValid || s.week <= g_day.week) && locked_now())) return true;
  }
  return false;
}

// a price into the slip (r207, Lance's CRAZY PARLAYS): ONE PICK A MARKET A GAME - the same price again takes it out, the other
// side of that market replaces it; a game's MONEY LINE + its SPREAD can both go on (a same-game pair, priced from their joint
// chance; a pair that can't both win is refused); a FUTURE is a leg like any other (one pick a race); up to kMaxLegs picks, from
// any matchday on the board
std::string pick_words(const Pick& p, int home, int away) {
  Leg l;
  l.home = home, l.away = away, l.market = p.market, l.side = p.side, l.spread = p.spread, l.american = p.american, l.club = p.club;
  l.whoName = p.whoName;
  l.orMore = p.market == kWinBy;  // (r225: the board's WIN BY = or more)
  std::string w = leg_words(l);
  const size_t sp = w.rfind(' ');
  return sp != std::string::npos ? w.substr(0, sp) : w;  // (without its price)
}
void toggle_leg(const Pick& p) {  // (g_m held)
  const double before = slip_mult();
  auto moved = [&](const std::string& m, uint32_t rgb, bool ding) {
    g_multFrom = before;
    g_multAt = g_now;
    say(m, rgb);
    sound(ding ? "book_ding" : "book_tick", ding ? 0.7f : 0.6f);
  };
  const std::string full = std::to_string(kMaxLegs) + " LEGS IS THE MOST A PARLAY TAKES";
  if (is_future(p.market)) {
    for (size_t i = 0; i < g_slip.size(); ++i) {
      if (g_slip[i].p.market != p.market) continue;
      if (g_slip[i].p.who == p.who) {
        g_slip.erase(g_slip.begin() + static_cast<long>(i));
        moved("TAKEN OFF THE SLIP", 0xC8D2E6, false);
      } else {
        g_slip[i].p = p;
        moved("FUTURE CHANGED (ONE PICK A RACE)", 0xC8D2E6, false);
      }
      return;
    }
    if (static_cast<int>(g_slip.size()) >= kMaxLegs) {
      say(full, 0xFF5A6E);
      return;
    }
    SlipLeg s;
    s.p = p;
    g_slip.push_back(s);
    moved(g_slip.size() == 1 ? std::string("ON THE SLIP: A FUTURE (IT SETTLES WHEN ITS RACE IS DECIDED)")
                             : "PARLAY: " + std::to_string(g_slip.size()) + " LEGS (A FUTURE IN IT: IT SETTLES WHEN ITS RACE IS DECIDED)",
          0x2EF2FF, true);
    return;
  }
  const std::vector<Fixture>* fx = nullptr;
  if (g_dayValid && p.week == g_day.week) fx = &g_fx;
  else if (const WeekBoard* w = find_week(p.week)) fx = &w->fx;
  if (fx == nullptr || p.game < 0 || p.game >= static_cast<int>(fx->size())) return;
  const Fixture f = (*fx)[static_cast<size_t>(p.game)];
  int same = -1, other = -1;
  for (size_t i = 0; i < g_slip.size(); ++i) {
    const SlipLeg& s = g_slip[i];
    if (is_future(s.p.market) || s.home != f.home || s.away != f.away || s.week != p.week) continue;
    if (s.p.market == p.market) same = static_cast<int>(i);
    else other = static_cast<int>(i);
  }
  // a pair with `other` (the game's other market) as it would stand: fine, or why not
  auto pair_ok = [&](const Pick& mine) -> bool {
    if (other < 0) return true;
    const Pick& o = g_slip[static_cast<size_t>(other)].p;
    if (mine.market == kWinBy || o.market == kWinBy) return true;
    const Pick& ml = mine.market == kMoneyLine ? mine : o;
    const Pick& sp = mine.market == kMoneyLine ? o : mine;
    int why = 0;
    const Pick q = sgp_pick(f, ml.side, sp.side, sp.spread, &why);
    if (why == 1 || q.american == 0) {
      say("CAN'T BOTH WIN: " + pick_words(ml, f.home, f.away) + " AND " + pick_words(sp, f.home, f.away), 0xFF5A6E);
      return false;
    }
    if (why == 2) {
      say("THAT PAIR IS A SURE THING: OFF THE BOARD", 0xC8D2E6);
      return false;
    }
    return true;
  };
  if (same >= 0) {
    if (g_slip[static_cast<size_t>(same)].p.side == p.side) {
      g_slip.erase(g_slip.begin() + same);
      moved("LEG TAKEN OFF THE SLIP", 0xC8D2E6, false);
      return;
    }
    if (!pair_ok(p)) return;
    g_slip[static_cast<size_t>(same)].p = p;
    moved("LEG CHANGED (ONE PICK A MARKET A GAME)", 0xC8D2E6, false);
    return;
  }
  if (static_cast<int>(g_slip.size()) >= kMaxLegs) {
    say(full, 0xFF5A6E);
    return;
  }
  if (!pair_ok(p)) return;
  SlipLeg s;
  s.p = p;
  s.home = f.home;
  s.away = f.away;
  s.week = p.week;
  if (other >= 0) {  // the same game's other market: next to it (the pair reads as one), priced together
    g_slip.insert(g_slip.begin() + other + 1, s);
    const Pick& o = g_slip[static_cast<size_t>(other)].p;
    const Pick& ml = p.market == kMoneyLine ? p : o;
    const Pick& sp = p.market == kMoneyLine ? o : p;
    const Pick q = sgp_pick(f, ml.side, sp.side, sp.spread, nullptr);
    moved("SAME GAME: " + pick_words(q, f.home, f.away) + " PRICED TOGETHER " + price_text(q.american), 0x2EF2FF, true);
    return;
  }
  g_slip.push_back(s);
  const bool later = g_dayValid && p.week > g_day.week;
  moved(g_slip.size() == 1 ? std::string(later ? "ON THE SLIP: A SINGLE (WEEK " + std::to_string(p.week) + ")" : "ON THE SLIP: A SINGLE")
                           : "PARLAY: " + std::to_string(g_slip.size()) + " LEGS" + (later ? "  (+ WEEK " + std::to_string(p.week) + ")" : ""),
        0x2EF2FF, true);
}

bool place_bet(bool machine) {  // (g_m held)
  if (g_slip.empty()) return false;
  // r207: legs from any matchday on the board + futures in it: futures need the futures open; a leg on today's games needs today
  // not kicked off; an upcoming matchday's legs are open until theirs
  bool anyFut = false, anyToday = false;
  for (const SlipLeg& s : g_slip) {
    if (is_future(s.p.market)) anyFut = true;
    else if (!g_dayValid || s.week <= g_day.week) anyToday = true;
  }
  const bool fut = anyFut && g_slip.size() == 1;
  if (anyFut && !futures_open()) {
    say("FUTURES ARE CLOSED (THE PLAYOFFS / A DAY IN PLAY)", 0xFF5A6E);
    return false;
  }
  if (anyToday && locked_now()) {
    say("LOCKED: THE DAY HAS KICKED OFF", 0xFF5A6E);
    return false;
  }
  const long long bal = balance();
  if (g_stake == kStakeCustom && g_custom <= 0) {
    say("TYPE YOUR STAKE: A ON CUSTOM OPENS THE NUMBER PAD", 0xFFB020);
    return false;
  }
  const long long st = stake_coins(g_stake, bal);
  if (st <= 0 || st > bal) {
    say(bal <= 0 ? "NO BIT COINS IN YOUR BINDER"
                 : (g_stake == kStakeCustom ? "YOUR CUSTOM STAKE IS MORE THAN YOUR " + coins_text_book(bal) + " COINS" : std::string("NOT ENOUGH BIT COINS")),
        0xFF5A6E);
    return false;
  }
  int open = 0;
  for (const Bet& b : g_book.bets) open += b.state == 0 ? 1 : 0;
  if (open >= 60) {
    say("60 TICKETS OPEN: LET SOME SETTLE", 0xFF5A6E);
    return false;
  }
  // every leg still on its board at its price (the boards move only between days / as the clubs change)
  for (const SlipLeg& s : g_slip) {
    bool there = false;
    if (is_future(s.p.market)) {
      for (const Pick& q : g_futOpts[std::max(0, std::min(3, s.p.market - kChampion))])
        if (q.who == s.p.who && q.american == s.p.american) there = true;
    } else if (const Fixture* f = fixture_for(s.week, s.home, s.away)) {
      there = g_dayValid && s.week >= g_day.week && fixture_pick(*f, s.p.market, s.p.side)->american == s.p.american;
    }
    if (!there || s.p.american == 0) {
      g_slip.clear();
      say(is_future(s.p.market) ? "THE FUTURES MOVED: PICK AGAIN" : "THE BOARD MOVED: PICK AGAIN", 0xFF5A6E);
      return false;
    }
  }
  std::string err;
  Bet b;
  b.legs = slip_to_legs(&err);
  if (!err.empty()) {
    say(err, 0xFF5A6E);
    return false;
  }
  b.season = g_dayValid ? g_day.season : g_fut.season;
  b.week = g_dayValid ? g_day.week : g_fut.week;
  for (Leg& l : b.legs)
    if (is_future(l.market)) l.week = b.week;  // (a future "kicks off" with the matchday it was bought on: cancel before it)
  b.stake = st;
  const double mult = multiplier(b, false);
  b.pays = parlay_pays(st, mult);  // (r208: uncapped, 64-bit)
  b.placed = static_cast<long long>(std::time(nullptr));
  b.machine = machine;
  if (g_wallet.spend == nullptr ||
      !g_wallet.spend(g_book.career, st, fut ? "a STRIKERZ SPORTSBOOK future" : (b.legs.size() > 1 ? "a STRIKERZ SPORTSBOOK parlay" : "a STRIKERZ SPORTSBOOK bet"))) {
    say("NOT ENOUGH BIT COINS", 0xFF5A6E);
    return false;
  }
  b.id = g_book.nextId++;
  g_book.bets.push_back(b);
  save_book();
  say((b.legs.size() > 1 ? "PARLAY PLACED: " + std::to_string(b.legs.size()) + " LEGS" : (fut ? "FUTURE PLACED: " : "BET PLACED: ") + leg_words(b.legs[0])) +
          "  -  PAYS " + coins_text_book(b.pays) + (machine ? "  -  YOUR SLIP IS PRINTING" : ""),
      0x39FF88);
  g_flashAt = g_now;
  xrcasino::play_music(xrcasino::kSfxCoinIn);  // r202: a real coin into the machine (was book_coin, in-house)
  if (machine) g_printQ.push_back(b.id);
  g_slip.clear();
  g_focus = 0;
  g_slipRow = 0;
  return true;
}
// r207: a leg has kicked off - its game's matchday under way or gone (a future: the matchday it was bought on); a ticket can be
// cancelled only while none of its legs has
bool leg_started(const Bet& b, const Leg& l) {  // (g_m held)
  const bool dayKnown = g_dayValid || g_fut.valid;
  if (!dayKnown) return true;
  const int season = g_dayValid ? g_day.season : g_fut.season, week = g_dayValid ? g_day.week : g_fut.week;
  if (b.season != season) return true;
  const int wk = is_future(l.market) ? b.week : (l.week >= 0 ? l.week : b.week);
  if (wk < week) return true;
  if (wk > week) return false;
  return is_future(l.market) ? !futures_open() : locked_now();
}
bool cancel_bet(size_t i) {  // (g_m held) an open ticket, none of its legs kicked off: the stake back
  if (i >= g_book.bets.size()) return false;
  Bet& b = g_book.bets[i];
  if (b.state != 0) return false;
  bool started = false;
  for (const Leg& l : b.legs) started = started || leg_started(b, l);
  if (started) {
    say("LOCKED: A LEG HAS KICKED OFF - IT SETTLES AS ITS GAMES COME IN", 0xFF5A6E);
    return false;
  }
  if (g_wallet.add == nullptr || !g_wallet.add(g_book.career, b.stake, "a STRIKERZ SPORTSBOOK bet cancelled")) return false;
  say("TICKET #" + std::to_string(b.id) + " CANCELLED: " + coins_text_book(b.stake) + " COINS BACK", 0xFFD23F);
  g_book.bets.erase(g_book.bets.begin() + static_cast<long>(i));
  save_book();
  sound("book_tick", 0.6f);
  return true;
}

// collect every settled winner / refund (g_m held): marked first, then paid (never twice)
long long collect(bool machine) {  // r208: 64-bit
  if (g_book.career == 0 || g_wallet.career == nullptr || g_wallet.career() != g_book.career) return 0;
  long long total = 0;
  int n = 0;
  for (Bet& b : g_book.bets)
    if ((b.state == 1 || b.state == 3) && !b.collected) {
      b.collected = true;
      total = xrcoin::add(total, b.paid);
      ++n;
    }
  if (total <= 0) return 0;
  save_book();
  if (g_wallet.add != nullptr) g_wallet.add(g_book.career, total, machine ? "STRIKERZ SPORTSBOOK winnings collected at the machine"
                                                                         : "STRIKERZ SPORTSBOOK winnings collected");
  trim_history();
  save_book();
  g_collectAt = g_now;
  g_collectCoins = total;
  g_collectMachine = machine;
  if (machine) {
    g_winAt = g_now;
    g_machineCollect = xrcoin::add(g_machineCollect, total);
    // r202 (Lance: victory music on every win): real CC0 music by the size of it, the coins pouring out of the tray
    xrcasino::play_music(total < 300 ? xrcasino::kMusicSmall : (total < 2500 ? xrcasino::kMusicBig : xrcasino::kMusicJackpot));
    xrcasino::play_music(xrcasino::kSfxCoinsPour);  // r202: real coins pouring into the tray
  } else {
    sound("book_collect", 0.85f);
  }
  say("COLLECTED " + coins_text_book(total) + " BIT COINS (" + std::to_string(n) + (n == 1 ? " TICKET)" : " TICKETS)"), 0xFFD23F);
  return total;
}

// the MY BETS rows: open tickets first (newest first), then the settled, newest first
std::vector<size_t> bet_rows() {  // (g_m held)
  std::vector<size_t> r;
  for (size_t i = g_book.bets.size(); i-- > 0;)
    if (g_book.bets[i].state == 0) r.push_back(i);
  for (size_t i = g_book.bets.size(); i-- > 0;)
    if (g_book.bets[i].state != 0) r.push_back(i);
  return r;
}

int max_cursor() {  // (g_m held)
  if (g_tab == kTabLines) return std::max(0, static_cast<int>(shown_lines().size()) * 2 - 1);
  if (g_tab == kTabWinBy) return 3;
  if (g_tab == kTabFutures) return std::max(0, static_cast<int>(g_futOpts[g_futMarket].size()) - 1);
  const int extra = totals().ready > 0 ? 1 : 0;  // (row 0: COLLECT)
  return std::max(0, static_cast<int>(bet_rows().size()) - 1 + extra);
}

// r224: the lists' windows (g_m held). Minimal scroll: the cursor above the window -> it's the first row, below -> the last;
// inside it (e.g. a row the pointer is on) -> the window stays put. Then each window is kept on its list (a list that shrank).
int bets_rows_shown(bool collect) { return collect ? 7 : 8; }
void keep_in_view() {
  {  // LEAGUE LINES (g_cursor: 2 a game)
    const int n = static_cast<int>(shown_lines().size());
    if (g_tab == kTabLines) {
      const int fi = g_cursor / 2;
      if (fi < g_linesTop) g_linesTop = fi;
      else if (fi >= g_linesTop + kLinesRows) g_linesTop = fi - kLinesRows + 1;
    }
    g_linesTop = std::max(0, std::min(std::max(0, n - kLinesRows), g_linesTop));
  }
  {  // MY BETS / SLIPS (g_cursor 0 = COLLECT while there's some: it sits above the list and moves nothing)
    const int extra = totals().ready > 0 ? 1 : 0, rows = bets_rows_shown(extra != 0);
    const int n = static_cast<int>(bet_rows().size());
    if (g_tab == kTabBets) {
      const int r = g_cursor - extra;
      if (r >= 0 && r < g_betsTop) g_betsTop = r;
      else if (r >= g_betsTop + rows) g_betsTop = r - rows + 1;
    }
    g_betsTop = std::max(0, std::min(std::max(0, n - rows), g_betsTop));
  }
  {  // the slip's legs: a leg added shows the newest (r207's way); the ring on a leg keeps it in view
    const int n = static_cast<int>(g_slip.size());
    if (g_slip.size() > g_legsSeen) g_legsTop = n - kLegsShown;
    g_legsSeen = g_slip.size();
    if (g_focus == 1 && g_tab != kTabBets && g_slipRow < n) {
      if (g_slipRow < g_legsTop) g_legsTop = g_slipRow;
      else if (g_slipRow >= g_legsTop + kLegsShown) g_legsTop = g_slipRow - kLegsShown + 1;
    }
    g_legsTop = std::max(0, std::min(std::max(0, n - kLegsShown), g_legsTop));
  }
}
// r224: a list's arrow (the pointer's trigger): a page up (dir -1) / down (+1); the cursor rides along on the same screen row so
// it stays in view (list 0 LEAGUE LINES, 1 MY BETS / SLIPS, 2 the slip's legs). True: it moved.
bool page_list(int list, int dir) {
  if (list == 0) {
    const int n = static_cast<int>(shown_lines().size());
    const int was = g_linesTop;
    g_linesTop = std::max(0, std::min(std::max(0, n - kLinesRows), g_linesTop + dir * kLinesRows));
    const int d = g_linesTop - was;
    if (d == 0) return false;
    const int fi = std::max(g_linesTop, std::min(std::min(n - 1, g_linesTop + kLinesRows - 1), g_cursor / 2 + d));
    g_tab = kTabLines, g_focus = 0;
    g_cursor = fi * 2 + (g_cursor & 1);
    return true;
  }
  if (list == 1) {
    const int extra = totals().ready > 0 ? 1 : 0, rows = bets_rows_shown(extra != 0);
    const int n = static_cast<int>(bet_rows().size());
    const int was = g_betsTop;
    g_betsTop = std::max(0, std::min(std::max(0, n - rows), g_betsTop + dir * rows));
    const int d = g_betsTop - was;
    if (d == 0) return false;
    g_tab = kTabBets, g_focus = 0;
    const int r = g_cursor - extra;
    if (r >= 0) g_cursor = extra + std::max(g_betsTop, std::min(std::min(n - 1, g_betsTop + rows - 1), r + d));
    return true;
  }
  const int n = static_cast<int>(g_slip.size());
  const int was = g_legsTop;
  g_legsTop = std::max(0, std::min(std::max(0, n - kLegsShown), g_legsTop + dir * kLegsShown));
  const int d = g_legsTop - was;
  if (d == 0) return false;
  if (g_focus == 1 && g_tab != kTabBets && g_slipRow < n)
    g_slipRow = std::max(g_legsTop, std::min(std::min(n - 1, g_legsTop + kLegsShown - 1), g_slipRow + d));
  return true;
}

// settles what's in (g_m held): true when anything changed. Nothing is paid here: winners wait to be collected.
bool settle() {
  if (g_book.career == 0) return false;
  bool changed = false;
  std::string bigLine;
  int newWins = 0, newLosses = 0;
  for (Bet& b : g_book.bets) {
    if (b.state != 0) continue;
    bool legChanged = false;
    for (Leg& l : b.legs) {
      if (l.state != 0) continue;
      if (is_future(l.market)) {
        // a future: settled the moment its result exists (franchise.cpp: the awards once the regular season is over, the
        // champion once the final is won); a season gone by without it: void
        if (!g_fut.valid) continue;
        if (g_fut.season > b.season) {
          l.state = 3;
          legChanged = true;
          continue;
        }
        if (g_fut.season != b.season) continue;
        if (l.market == kChampion) {
          if (g_fut.champion < 0) continue;
          l.state = g_fut.champion == l.club ? 1 : 2;
        } else {
          if (g_fut.phase == PORT_FR_PHASE_REGULAR) continue;
          const char* w = l.market == kMvp ? g_fut.mvp : (l.market == kBoot ? g_fut.boot : g_fut.rookie);
          l.state = w[0] == '\0' ? 3 : (l.who == w ? 1 : 2);  // (no rookie scored: no winner, the stake back)
        }
        legChanged = true;
        continue;
      }
      int hg = 0, ag = 0;
      // r207: each leg on its own matchday (an r199-r206 leg: the ticket's)
      const int r = PortFranchiseBookResult(b.season, l.week >= 0 ? l.week : b.week, l.home, l.away, &hg, &ag);
      if (r == 0) continue;
      legChanged = true;
      if (r < 0) {
        l.state = 3;  // (its game gone: void)
        continue;
      }
      l.hg = hg;
      l.ag = ag;
      l.state = leg_state(l, hg, ag);
    }
    if (!legChanged) continue;
    changed = true;
    long long paid = 0;
    b.state = ticket_state(b, &paid);
    b.paid = paid;
    b.collected = b.state == 2;  // (a loser has nothing to collect)
    if (b.state == 2) ++newLosses;
    if (b.state == 1) {
      ++newWins;
      if (b.paid > g_book.bigPays) {
        g_book.bigPays = b.paid;
        g_book.bigWords = b.legs.size() > 1 ? std::to_string(b.legs.size()) + "-LEG PARLAY" : leg_words(b.legs[0]);
      }
      if (b.paid - b.stake >= 500 || (b.legs.size() >= 3 && b.paid - b.stake >= 200)) {
        const char* me = PortFranchiseBookClubPlace(g_day.myClub);
        bigLine = std::string("STRIKERZ SPORTSBOOK: A ") + (me != nullptr && me[0] != '\0' ? me : "LOCAL") + " FAN HITS FOR " +
                  coins_text_book(b.paid) + " COINS ON " +
                  (b.legs.size() > 1 ? "A " + std::to_string(b.legs.size()) + "-LEG PARLAY" : leg_words(b.legs[0]));
      }
    }
  }
  if (changed) save_book();
  if (newWins > 0) {
    const Totals t = totals();
    say("WINNER! " + coins_text_book(t.ready) + " BIT COINS READY TO COLLECT", 0xFFD23F);
    sound("book_ding", 0.9f);
  } else if (changed) {
    say("TICKETS SETTLED: SEE MY BETS", 0xC8D2E6);
    // r202: a settle you SEE (a hand on the machine's joystick as it lands) with only losers in it: the funny sting
    if (newLosses > 0 && xrcasino::engaged()) xrcasino::play_music(xrcasino::kMusicLose);
  }
  if (!bigLine.empty()) PortFranchiseBookNews(bigLine.c_str());
  return changed;
}

void a_on_board(bool cabinet) {  // (g_m held)
  if (g_tab == kTabFutures) {
    const std::vector<Pick>& opts = g_futOpts[g_futMarket];
    const int at = std::max(0, std::min(static_cast<int>(opts.size()) - 1, g_cursor));
    if (opts.empty()) say("NO ONE ON THIS BOARD YET", 0xC8D2E6);
    else if (!futures_open()) say("FUTURES ARE CLOSED: THEY CLOSE AT THE PLAYOFFS", 0xFF5A6E);
    else if (opts[static_cast<size_t>(at)].american == 0) say("THAT ONE IS OFF THE BOARD", 0xC8D2E6);
    else toggle_leg(opts[static_cast<size_t>(at)]);
  } else if (g_tab == kTabLines) {
    Pick p;
    if (!line_pick(g_cursor, g_col, &p)) say(g_weekAhead > 0 ? "NO LINES THAT WEEK" : "NO LINES TODAY", 0xC8D2E6);
    else if (p.american == 0) say("THAT LINE IS OFF THE BOARD", 0xC8D2E6);
    else if (g_weekAhead == 0 && locked_now()) say("LOCKED: THE DAY HAS KICKED OFF", 0xFF5A6E);
    else toggle_leg(p);
  } else if (g_tab == kTabWinBy) {
    if (g_mine < 0) say("NO GAME FOR YOU TODAY", 0xC8D2E6);
    else if (locked_now()) say("LOCKED: THE DAY HAS KICKED OFF", 0xFF5A6E);
    else if (g_fx[static_cast<size_t>(g_mine)].winBy[g_cursor + 1].american != 0) {
      Pick p = g_fx[static_cast<size_t>(g_mine)].winBy[g_cursor + 1];
      p.week = g_day.week;  // r207
      toggle_leg(p);
    }
  } else {
    const Totals t = totals();
    int row = g_cursor;
    if (t.ready > 0) {
      if (row == 0) {
        collect(cabinet);
        return;
      }
      --row;
    }
    const std::vector<size_t> rows = bet_rows();
    if (row < 0 || row >= static_cast<int>(rows.size())) return;
    if (cabinet) {  // the machine's SLIPS: print it (again)
      g_printQ.push_back(g_book.bets[rows[static_cast<size_t>(row)]].id);
      sound("book_print", 0.9f);
      // r202: a LOSER slip printed (you looked it up): the funny sting - losses play only then, never on their own
      if (g_book.bets[rows[static_cast<size_t>(row)]].state == 2) xrcasino::play_music(xrcasino::kMusicLose);
      say("PRINTING TICKET #" + std::to_string(g_book.bets[rows[static_cast<size_t>(row)]].id), 0x2EF2FF);
    } else {
      cancel_bet(rows[static_cast<size_t>(row)]);
    }
  }
}

// r207: the upcoming matchdays' boards wanted now (the picker's, the slip's legs'), read fresh (game thread); true: one changed
bool refresh_weeks() {  // (g_m held)
  std::vector<int> want;
  if (g_dayValid) {
    g_weekAhead = std::max(0, std::min(max_ahead(), g_weekAhead));
    if (g_weekAhead > 0) want.push_back(g_day.week + g_weekAhead);
    for (const SlipLeg& s : g_slip)
      if (!is_future(s.p.market) && s.week > g_day.week) want.push_back(s.week);
  } else {
    g_weekAhead = 0;
  }
  std::sort(want.begin(), want.end());
  want.erase(std::unique(want.begin(), want.end()), want.end());
  std::vector<WeekBoard> fresh;
  bool changed = false;
  for (int w : want) {
    WeekBoard wb;
    wb.week = w;
    const WeekBoard* old = find_week(w);
    if (!PortFranchiseBookWeekGet(w, &wb.day)) {
      changed = changed || old != nullptr;
      continue;
    }
    if (old != nullptr && std::memcmp(&old->day, &wb.day, sizeof(wb.day)) == 0) {
      fresh.push_back(*old);
      continue;
    }
    price_day(wb.day, &wb.fx);
    for (size_t i = 0; i < wb.fx.size(); ++i)
      if (!wb.fx[i].mine) wb.lines.push_back(static_cast<int>(i));  // (your own game: WIN BY's, today only)
    fresh.push_back(wb);
    changed = true;
  }
  if (fresh.size() != g_weeks.size()) changed = true;
  g_weeks = std::move(fresh);
  return changed;
}

// r207: the slip's legs take their boards' prices now; a leg whose game is gone / off / kicked off goes, and a same-game pair that
// can't stand any more loses its second pick
void reprice_slip() {  // (g_m held)
  std::vector<SlipLeg> keep;
  for (const SlipLeg& s : g_slip) {
    if (is_future(s.p.market)) {
      for (const Pick& q : g_futOpts[std::max(0, std::min(3, s.p.market - kChampion))])
        if (q.who == s.p.who && q.american != 0) {
          SlipLeg k = s;
          k.p = q;
          keep.push_back(k);
        }
      continue;
    }
    if (!g_dayValid || s.week < g_day.week) continue;
    const std::vector<Fixture>* fx = s.week == g_day.week ? &g_fx : nullptr;
    if (fx == nullptr)
      if (const WeekBoard* w = find_week(s.week)) fx = &w->fx;
    if (fx == nullptr) continue;
    for (size_t i = 0; i < fx->size(); ++i) {
      const Fixture& f = (*fx)[i];
      if (f.home != s.home || f.away != s.away) continue;
      SlipLeg k = s;
      k.p = *fixture_pick(f, s.p.market, s.p.side);
      k.p.game = static_cast<int>(i);
      k.p.week = s.week;
      if (k.p.american != 0) keep.push_back(k);
    }
  }
  g_slip = keep;
  std::string err;
  for (int guard = 0; guard < kMaxLegs; ++guard) {  // (a pair gone off the board: its second pick off)
    slip_to_legs(&err);
    if (err.empty()) break;
    bool cut = false;
    for (size_t i = 0; i < g_slip.size() && !cut; ++i)
      for (size_t j = i + 1; j < g_slip.size() && !cut; ++j)
        if (!is_future(g_slip[i].p.market) && g_slip[j].home == g_slip[i].home && g_slip[j].away == g_slip[i].away &&
            g_slip[j].week == g_slip[i].week) {
          const Fixture* f = fixture_for(g_slip[i].week, g_slip[i].home, g_slip[i].away);
          const SlipLeg& ml = g_slip[i].p.market == kMoneyLine ? g_slip[i] : g_slip[j];
          const SlipLeg& sp = g_slip[i].p.market == kMoneyLine ? g_slip[j] : g_slip[i];
          int why = 1;
          if (f != nullptr) sgp_pick(*f, ml.p.side, sp.p.side, sp.p.spread, &why);
          if (why != 0) {
            g_slip.erase(g_slip.begin() + static_cast<long>(j));
            say("THE PRICES MOVED: A SAME-GAME PICK CAME OFF THE SLIP", 0xFFB020);
            cut = true;
          }
        }
    if (!cut) break;
  }
}
}  // namespace

void set_wallet(const Wallet& w) {
  xrcasino::set_wallet(w);  // r202: the casino plays the same wallet
  std::lock_guard<std::mutex> lk(g_m);
  g_wallet = w;
}
void set_logo_source(const uint8_t* (*logo)(int, int*, int*)) { g_logo = logo; }
int generation() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_gen;
}
bool animating() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_now - g_flashAt < 0.7 || g_now - g_multAt < 0.8 || g_now - g_collectAt < 2.6 || g_now - g_winAt < 4.0 ||
         g_now - g_moveAt < 0.3 || g_now - g_pressAt < 0.3;  // (r204: the cursor's pop / press)
}
namespace {
uint64_t sel_key_locked() {  // (g_m held) where the selection is (a step that leaves it the same moved nothing)
  uint64_t k = static_cast<uint64_t>(g_tab & 7) | static_cast<uint64_t>(g_cursor & 0xFFF) << 3 | static_cast<uint64_t>(g_col & 3) << 15 |
               static_cast<uint64_t>(g_focus & 3) << 17 | static_cast<uint64_t>(g_slipRow & 63) << 19 | static_cast<uint64_t>(g_stake & 7) << 25 |
               static_cast<uint64_t>(g_futMarket & 7) << 28 | static_cast<uint64_t>(g_slip.size() & 63) << 31;
  // r207: the week picker, the number pad (its key, what's typed)
  k |= static_cast<uint64_t>(g_weekAhead & 31) << 37 | static_cast<uint64_t>(g_padOpen ? 1 : 0) << 42 | static_cast<uint64_t>(g_padKey & 15) << 43 |
       static_cast<uint64_t>(g_padText.size() & 15) << 47;
  return k;
}
}  // namespace
uint64_t selection_key() {
  uint64_t k;
  {
    std::lock_guard<std::mutex> lk(g_m);
    k = sel_key_locked();
  }
  return k ^ (xrcasino::selection_key() * 0x9E3779B97F4A7C15ull);
}
float cursor_move_ago() {
  std::lock_guard<std::mutex> lk(g_m);
  return static_cast<float>(g_now - g_moveAt);
}
long long ready_to_collect(int* tickets) {
  std::lock_guard<std::mutex> lk(g_m);
  const Totals t = totals();
  if (tickets != nullptr) *tickets = t.readyTickets;
  return t.ready;
}
uint32_t take_print() {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_printQ.empty()) return 0;
  const uint32_t id = g_printQ.front();
  g_printQ.erase(g_printQ.begin());
  return id;
}
long long take_machine_collect() {
  std::lock_guard<std::mutex> lk(g_m);
  const long long c = g_machineCollect;
  g_machineCollect = 0;
  return c;
}

bool frame(double now) {
  xrcasino::frame(now);  // r202: the casino's spins run on (reels landing, the ball, the reveal) wherever you are
  std::lock_guard<std::mutex> lk(g_m);
  g_now = now;
  if (g_weeksDirty && g_book.career != 0) {  // r207: a week just picked on LEAGUE LINES: its board read at once (this frame)
    g_weeksDirty = false;
    if (refresh_weeks()) reprice_slip();
    g_cursor = std::max(0, std::min(max_cursor(), g_cursor));
    keep_in_view();  // r224
    ++g_gen;
  }
  if (now - g_lastRead < 0.4) return false;
  g_lastRead = now;
  PortFranchiseBookDay d;
  PortFranchiseBookDayGet(&d);
  bool changed = false;
  const unsigned career = PortFranchiseCareerSeed();
  if (career != g_book.career || (career != 0 && !g_book.loaded)) {
    if (career != 0) load_book(career);
    else g_book = Book();
    g_slip.clear();
    g_tab = g_cursor = g_focus = g_slipRow = 0;
    g_printQ.clear();
    g_weekAhead = 0;  // r207
    g_weeks.clear();
    g_padOpen = false;
    changed = true;
  }
  if (career == 0) {
    if (g_dayValid) changed = true;
    g_dayValid = false;
  } else {
    const bool sameDay = g_dayValid == d.valid && d.season == g_day.season && d.week == g_day.week && d.count == g_day.count &&
                         d.locked == g_day.locked && std::memcmp(d.games, g_day.games, sizeof(d.games)) == 0;
    if (!sameDay) {
      g_day = d;
      g_dayValid = d.valid;
      price_day(g_day, &g_fx);
      g_lines.clear();
      g_mine = -1;
      for (size_t i = 0; i < g_fx.size(); ++i) {
        if (g_fx[i].mine) g_mine = static_cast<int>(i);
        else g_lines.push_back(static_cast<int>(i));
      }
      // the slip's legs still on a board take their new prices; the others go (r207: every matchday's board read again)
      g_weeks.clear();
      refresh_weeks();
      reprice_slip();
      changed = true;
    } else if (refresh_weeks()) {  // r207: an upcoming matchday's prices moved (the clubs changed)
      reprice_slip();
      changed = true;
    }
    {  // r199 round 3: the futures (re-priced when the races move: a week played, the playoffs, the final)
      static PortFranchiseBookFutures s_f;
      PortFranchiseBookFuturesGet(&s_f);
      if (std::memcmp(&s_f, &g_fut, sizeof(s_f)) != 0) {
        g_fut = s_f;
        price_futures(g_fut, g_futOpts);
        reprice_slip();  // (r207: a future on the slip takes its new price, or goes - the rest stay)
        changed = true;
      }
    }
    if (g_dayValid && g_day.locked && (g_book.lockSeason != g_day.season || g_book.lockWeek != g_day.week)) {
      g_book.lockSeason = g_day.season;  // (kicked off: this day's board stays shut, even if the match is quit)
      g_book.lockWeek = g_day.week;
      save_book();
      changed = true;
    }
    if (settle()) changed = true;
  }
  g_cursor = std::max(0, std::min(max_cursor(), g_cursor));
  {  // r224: the lists' windows kept on what's there now (a ticket settled, the day's board read)
    const int l = g_linesTop, b = g_betsTop, s = g_legsTop;
    keep_in_view();
    if (g_linesTop != l || g_betsTop != b || g_legsTop != s) changed = true;
  }
  if (changed) ++g_gen;
  return changed;
}

namespace {
bool input_locked(const Input& in, bool cabinet);
}
bool input(const Input& in, bool cabinet) {
  if (cabinet && xrcasino::screen() != xrcasino::kBook) {  // r202: SLOTS / ROULETTE / the picker
    const bool used = xrcasino::input(in);
    std::lock_guard<std::mutex> lk(g_m);
    ++g_gen;  // r204: the casino's moves redraw the machine's screen at once (r203: they waited for its next 0.1 s redraw)
    return used;
  }
  std::lock_guard<std::mutex> lk(g_m);
  if (g_book.career == 0) return false;
  // r204: the step's ticks wait - only a selection that really moved (or a button's action) makes a sound
  const uint64_t k0 = sel_key_locked();
  g_deferTicks = true;
  g_tickWant = 0.0f;
  const bool used = input_locked(in, cabinet);
  keep_in_view();  // r224: the stick / buttons moved the cursor: its list follows (one row at a time at the window's edges)
  g_deferTicks = false;
  const bool moved = sel_key_locked() != k0;
  if (g_tickWant > 0.0f && (moved || in.a || in.b || in.x || in.y)) sound("book_tick", g_tickWant);
  if (moved) g_moveAt = g_now;
  if (in.a && used) g_pressAt = g_now;
  if (moved || used) ++g_gen;
  return used;
}
namespace {
// r207: the CUSTOM stake's number pad (the slip's panel): 1-9 (keys 0-8), CLEAR (9), 0 (10), BACKSPACE (11), OK (12)
void pad_open() {  // (g_m held)
  g_padOpen = true;
  g_padKey = 0;
  g_padText = g_custom > 0 ? std::to_string(g_custom) : std::string();
  g_padFresh = !g_padText.empty();
  g_focus = 1;
  sound("book_ding", 0.5f);
  say("TYPE YOUR STAKE: 1 TO " + coins_text_book(std::max(0LL, balance())) + " BIT COINS  -  OK SETS IT, B CLOSES", 0x2EF2FF);
}
void pad_press(int key) {  // (g_m held)
  g_pressAt = g_now;
  const bool fresh = g_padFresh;
  g_padFresh = false;
  if (key <= 8 || key == 10) {
    const char d = key == 10 ? '0' : static_cast<char>('1' + key);
    if (fresh) g_padText.clear();  // (the stake shown was the old one: a new number)
    // r208: 15 digits (999,999,999,999,999 = xrcoin::kMax: any balance; r207 stopped at 9)
    if (g_padText.size() >= 15) say("THAT'S AS BIG AS IT GOES", 0xFFB020);
    else if (!(g_padText.empty() && d == '0')) g_padText += d;  // (no leading zero)
    sound("book_tick", 0.6f);
  } else if (key == kPadClear) {
    g_padText.clear();
    sound("book_tick", 0.6f);
  } else if (key == kPadBack) {
    if (!g_padText.empty()) g_padText.pop_back();
    sound("book_tick", 0.6f);
  } else {
    const long long n = g_padText.empty() ? 0 : std::atoll(g_padText.c_str());
    const long long bal = balance();
    if (n <= 0) {
      say("TYPE A STAKE: 1 TO " + coins_text_book(std::max(0LL, bal)), 0xFF5A6E);
    } else if (n > bal) {
      say("THAT'S MORE THAN YOUR " + coins_text_book(std::max(0LL, bal)) + " BIT COINS", 0xFF5A6E);
    } else {
      g_custom = n;
      g_stake = kStakeCustom;
      g_padOpen = false;
      say("STAKE SET: " + coins_text_book(n) + " BIT COINS", 0xFFD23F);
      sound("book_ding", 0.8f);
    }
  }
}
bool pad_input(const Input& in) {  // (g_m held) while it's up the pad has every input
  if (in.step && in.dir != 0) {
    const int k = g_padKey, row = k / 3, col = k % 3;
    int n = k;
    if (in.dir == 3) n = k == kPadOk ? 10 : (row > 0 ? k - 3 : k);
    if (in.dir == 4) n = k == kPadOk ? k : (row == 3 ? kPadOk : k + 3);
    if (in.dir == 1 && k != kPadOk && col > 0) n = k - 1;
    if (in.dir == 2 && k != kPadOk && col < 2) n = k + 1;
    if (n != k) g_padKey = n, sound("book_tick", 0.35f);
  }
  if (in.a) pad_press(g_padKey);
  if (in.b) {
    g_padOpen = false;
    say(g_custom > 0 ? "STAKE STAYS " + coins_text_book(g_custom) : std::string("NO CUSTOM STAKE TYPED"), 0xC8D2E6);
    sound("book_tick", 0.5f);
  }
  ++g_gen;
  return true;
}

bool input_locked(const Input& in, bool cabinet) {  // (g_m held)
  bool used = false;
  if (g_padOpen) return pad_input(in);  // r207
  if (!cabinet && g_focus == 2) g_focus = 0;  // (r203: the tabs row is the machine's; the hub page has X)
  if (g_focus == 3 && g_tab != kTabLines) g_focus = 0;
  if (g_focus == 3) {
    // r207: LEAGUE LINES' WEEK PICKER (up off its first line): left / right = this matchday / the ones ahead (the regular
    // season's), down or A = its games, up = the machine's TABS row
    if (in.step && (in.dir == 1 || in.dir == 2)) {
      const int mx = max_ahead(), was = g_weekAhead;
      g_weekAhead = std::max(0, std::min(mx, g_weekAhead + (in.dir == 1 ? -1 : 1)));
      if (g_weekAhead != was) {
        g_cursor = 0;
        g_weeksDirty = true;
        sound("book_tick", 0.45f);
      } else {
        say(mx == 0 ? std::string("UPCOMING WEEKS: THE REGULAR SEASON ONLY") : (in.dir == 1 ? std::string("THIS IS THIS WEEK'S BOARD") : std::string("THE LAST WEEK OF THE SEASON")),
            0xC8D2E6);
      }
      used = true;
    }
    if ((in.step && in.dir == 4) || in.a) {
      g_focus = 0;
      sound("book_tick", 0.4f);
      used = true;
    }
    if (in.step && in.dir == 3 && cabinet) {
      g_focus = 2;
      sound("book_tick", 0.35f);
      used = true;
    }
    if (in.b && cabinet) {
      xrcasino::open_picker();
      used = true;
    }
    if (in.x) {
      g_tab = (g_tab + 1) % kTabs;
      g_cursor = 0;
      g_focus = 0;
      used = true;
    }
    if (cabinet && in.y) {
      g_stake = (g_stake + 1) % kStakeChoices;
      used = true;
    }
    if (used) ++g_gen;
    return used;
  }
  if (cabinet && g_focus == 2) {
    // r203: the machine's TABS row (pushed up off the top of a board): left / right picks the board, down or A goes into it, B is
    // the game picker (the top level's)
    if (in.step && (in.dir == 1 || in.dir == 2)) {
      g_tab = (g_tab + (in.dir == 1 ? kTabs - 1 : 1)) % kTabs;
      g_cursor = 0;
      sound("book_tick", 0.45f);
      used = true;
    }
    if ((in.step && in.dir == 4) || in.a) {
      g_focus = g_tab == kTabLines ? 3 : 0;  // r207: LEAGUE LINES' week picker first
      sound("book_tick", 0.4f);
      used = true;
    }
    if (in.b) {
      xrcasino::open_picker();
      used = true;
    }
    if (used) ++g_gen;
    return used;
  }
  if (in.x) {  // the next board (LEAGUE LINES > WIN BY > MY BETS / SLIPS)
    g_tab = (g_tab + 1) % kTabs;
    g_cursor = 0;
    g_focus = 0;
    sound("book_tick", 0.5f);
    used = true;
  }
  if (cabinet && in.y) {  // at the machine: Y steps the stake
    g_stake = (g_stake + 1) % kStakeChoices;
    sound("book_tick", 0.5f);
    used = true;
  }
  const int nLegs = static_cast<int>(g_slip.size());
  if (g_focus == 1 && g_tab != kTabBets) {
    // the slip: its legs (A takes one off), the stake row (left / right), PLACE BET
    g_slipRow = std::max(0, std::min(nLegs + 1, g_slipRow));
    if (in.step && in.dir == 3) g_slipRow = std::max(0, g_slipRow - 1), used = true;
    if (in.step && in.dir == 4) g_slipRow = std::min(nLegs + 1, g_slipRow + 1), used = true;
    if (in.step && (in.dir == 1 || in.dir == 2)) {
      if (g_slipRow == nLegs) {
        g_stake = std::max(0, std::min(kStakeChoices - 1, g_stake + (in.dir == 1 ? -1 : 1)));
      } else if (in.dir == 1) {
        g_focus = 0;  // (left off the slip: back to the board)
      }
      used = true;
    }
    if (used && !in.x && !in.y) sound("book_tick", 0.4f);
    if (in.a) {
      if (g_slipRow < nLegs) {
        const double before = slip_mult();
        g_slip.erase(g_slip.begin() + g_slipRow);
        g_multFrom = before;
        g_multAt = g_now;
        say("LEG TAKEN OFF THE SLIP", 0xC8D2E6);
        sound("book_tick", 0.6f);
        if (g_slip.empty()) g_focus = 0;
      } else if (g_slipRow == nLegs && g_stake == kStakeCustom) {
        pad_open();  // r207: A on the CUSTOM chip: the number pad
      } else {
        place_bet(cabinet);
      }
      used = true;
    }
    if (in.b) {
      g_focus = 0;
      used = true;
    }
  } else {
    if (in.step && in.dir == 3 && g_tab == kTabLines && g_cursor == 0) {
      g_focus = 3;  // r207: up off LEAGUE LINES' first line = its WEEK PICKER (the hub page's too)
      sound("book_tick", 0.35f);
      ++g_gen;
      return true;
    }
    if (cabinet && in.step && in.dir == 3 && (g_cursor == 0 || g_tab == kTabWinBy)) {
      g_focus = 2;  // r203: up off the top of a board (WIN BY: up at all) = the TABS row (the machine has no X for NEXT BOARD now)
      sound("book_tick", 0.35f);
      ++g_gen;
      return true;
    }
    if (in.step && in.dir != 0) {
      const int mc = max_cursor();
      if (g_tab == kTabFutures) {
        if (in.dir == 3) g_cursor = std::max(0, g_cursor - 1);
        if (in.dir == 4) g_cursor = std::min(mc, g_cursor + 1);
        if (in.dir == 1 && g_futMarket > 0) --g_futMarket, g_cursor = 0;
        else if (in.dir == 2 && g_futMarket < 3) ++g_futMarket, g_cursor = 0;
        else if (in.dir == 2 && nLegs > 0) g_focus = 1, g_slipRow = nLegs;
      } else if (g_tab == kTabLines) {
        if (in.dir == 3) g_cursor = std::max(0, g_cursor - 1);
        if (in.dir == 4) g_cursor = std::min(mc, g_cursor + 1);
        if (in.dir == 1) g_col = 0;
        if (in.dir == 2) {
          if (g_col == 1 && nLegs > 0) g_focus = 1, g_slipRow = nLegs;  // (right off the spreads: into the slip)
          g_col = 1;
        }
      } else if (g_tab == kTabWinBy) {
        if (in.dir == 1) g_cursor = std::max(0, g_cursor - 1);
        if (in.dir == 2) {
          if (g_cursor == 3 && nLegs > 0) g_focus = 1, g_slipRow = nLegs;
          g_cursor = std::min(3, g_cursor + 1);
        }
      } else {
        if (in.dir == 3) g_cursor = std::max(0, g_cursor - 1);
        if (in.dir == 4) g_cursor = std::min(mc, g_cursor + 1);
      }
      sound("book_tick", 0.35f);
      used = true;
    }
    if (in.a) {
      a_on_board(cabinet);
      used = true;
    }
    if (cabinet && in.b) {  // r202: B on a board at the machine = its game picker (SPORTSBOOK / SLOTS / ROULETTE)
      xrcasino::open_picker();
      used = true;
    }
  }
  if (used) ++g_gen;
  return used;
}
}  // namespace

bool point(float u, float v, bool click, bool cabinet) {
  if (cabinet && xrcasino::screen() != xrcasino::kBook) {  // r202
    const bool on = xrcasino::point(u, v, click);
    std::lock_guard<std::mutex> lk(g_m);
    ++g_gen;  // (r204: whatever the pointer did shows at once)
    return on;
  }
  std::unique_lock<std::mutex> lk(g_m);
  const float x = u * kW, y = v * kH;
  // r204: bigger hit areas (the nearest box within 28 of the ray wins) + a sticky hover (no flicker between neighbours)
  int hi = -1;
  // r224: a list's arrows sit just under its last row - the ray inside one is that arrow (the last row's sticky hover would
  // otherwise keep it, and the trigger would pick the row)
  for (size_t i = 0; i < g_hits.size() && hi < 0; ++i) {
    const Hit& a = g_hits[i];
    if (a.kind == 13 && x >= a.x0 && x <= a.x1 && y >= a.y0 && y <= a.y1) {
      hi = static_cast<int>(i);
      g_hover[4] = 0.0f;  // (no sticky hover off an arrow: the row just above takes the ray as soon as it's on it)
    }
  }
  if (hi < 0) hi = hit_at(g_hits, x, y, g_hover);
  if (hi >= 0) {
    const Hit h = g_hits[static_cast<size_t>(hi)];
    bool moved = false;
    if (g_padOpen && h.kind != 10) return true;  // r207: the pad is up - only its keys
    switch (h.kind) {
      case 10:  // r207: a pad key
        moved = g_padKey != h.a;
        g_padKey = h.a;
        if (click) {
          pad_press(h.a);
          ++g_gen;
          return true;
        }
        break;
      case 11:  // r207: the week picker's arrows
        if (click) {
          const int was = g_weekAhead;
          g_weekAhead = std::max(0, std::min(max_ahead(), g_weekAhead + h.a));
          g_tab = kTabLines, g_focus = 3;
          if (g_weekAhead != was) g_cursor = 0, g_linesTop = 0, g_weeksDirty = true;  // (r224: a new week's board from its top)
          ++g_gen;
          sound("book_tick", 0.5f);
        }
        return true;
      case 12:
        moved = g_tab != kTabLines || g_focus != 3;
        g_tab = kTabLines, g_focus = 3;
        if (click) {
          ++g_gen;
          return true;
        }
        break;
      case 0:
        moved = g_tab != kTabLines || g_cursor != h.a || g_col != h.b || g_focus != 0;
        g_tab = kTabLines, g_cursor = h.a, g_col = h.b, g_focus = 0;
        break;
      case 1:
        moved = g_tab != kTabWinBy || g_cursor != h.a || g_focus != 0;
        g_tab = kTabWinBy, g_cursor = h.a, g_focus = 0;
        break;
      case 2:
        moved = g_focus != 1 || g_stake != h.a;
        g_focus = 1, g_slipRow = static_cast<int>(g_slip.size()), g_stake = h.a;
        if (click) {
          ++g_gen;
          if (h.a == kStakeCustom) pad_open();  // r207: the CUSTOM chip: the number pad
          else sound("book_tick", 0.5f);
          return true;
        }
        break;
      case 3:
        moved = g_focus != 1;
        g_focus = 1, g_slipRow = static_cast<int>(g_slip.size()) + 1;
        break;
      case 4:
        if (click) {
          g_tab = h.a;
          g_cursor = 0;
          g_focus = 0;
          keep_in_view();  // r224: (a board opens at its top)
          ++g_gen;
          sound("book_tick", 0.5f);
        }
        return true;
      case 6:
        moved = g_focus != 1 || g_slipRow != h.a;
        g_focus = 1, g_slipRow = h.a;
        break;
      case 7:
        moved = g_tab != kTabFutures || g_cursor != h.a || g_focus != 0;
        g_tab = kTabFutures, g_cursor = h.a, g_focus = 0;
        break;
      case 13:  // r224: a list's up / down arrow (a = -1 / +1, b = the list): the trigger pages it, hovering does nothing
        if (click) {
          if (page_list(h.b, h.a)) {
            keep_in_view();
            g_moveAt = g_now;
            sound("book_tick", 0.5f);
          }
          ++g_gen;
        }
        return true;
      case 9:  // r202: GAMES (the machine's picker)
        if (click) {
          lk.unlock();
          xrcasino::open_picker();
        }
        return true;
      case 8:
        if (click && g_futMarket != h.a) {
          g_futMarket = h.a;
          g_cursor = 0;
          ++g_gen;
          sound("book_tick", 0.5f);
        }
        return true;
      default:
        moved = g_tab != kTabBets || g_cursor != h.a;
        g_tab = kTabBets, g_cursor = h.a, g_focus = 0;
        break;
    }
    keep_in_view();  // r224: (a row the pointer is on is in its window: nothing scrolls)
    if (moved) {
      ++g_gen;
      g_moveAt = g_now;  // (r204: the cursor pops onto what the pointer is on)
      // r234 (Lance: the clicking in the background): no tick for a pointer just passing over a row - only a trigger / button makes one
    }
    if (click) {
      lk.unlock();
      Input in;
      in.a = true;
      return input(in, cabinet);
    }
    return true;
  }
  return false;
}

void open_tab(int tab) {
  std::lock_guard<std::mutex> lk(g_m);
  g_tab = std::max(0, std::min(kTabs - 1, tab));
  g_cursor = 0;
  g_focus = 0;
  keep_in_view();  // r224
  ++g_gen;
}

namespace {
void hints_now(std::string* aWord, bool* aDim, std::string* xWord);
}
void hub_hints(std::string* aWord, bool* aDim, std::string* xWord) {
  std::lock_guard<std::mutex> lk(g_m);
  hints_now(aWord, aDim, xWord);
}
namespace {
// r203: the machine's CONTROL BAR words for the sportsbook (g_m held)
std::string machine_hint_now() {
  if (g_padOpen) return "STICK: MOVE   A / X: PRESS THE KEY   B / Y: CLOSE THE PAD   (OK SETS YOUR STAKE)";  // r207
  if (g_focus == 3) return "LEFT / RIGHT: WEEK   A / X or DOWN: ITS GAMES   UP: BOARDS   B / Y: CASINO GAMES";
  if (g_focus == 2) return "LEFT / RIGHT: BOARD   A / X or DOWN: OPEN IT   B / Y: CASINO GAMES   MENU: GAMES";
  std::string a, x;
  bool dim = false;
  hints_now(&a, &dim, &x);
  const bool slip = g_focus == 1 && g_tab != kTabBets;
  if (!slip && g_tab == kTabBets && a == "CANCEL TICKET") a = "PRINT THE SLIP";  // (the machine's SLIPS print, a_on_board)
  return "STICK: MOVE   A / X: " + a + (slip ? "   B / Y: BACK TO THE BOARD" : "   B / Y: CASINO GAMES   UP TOP: BOARDS") + "   MENU: GAMES";
}
void hints_now(std::string* aWord, bool* aDim, std::string* xWord) {  // (g_m held)
  static const char* const kNext[kTabs] = {"WIN BY", "FUTURES", "MY BETS", "LEAGUE LINES"};
  *xWord = kNext[g_tab];
  const bool lkd = locked_now();
  const int nLegs = static_cast<int>(g_slip.size());
  if (g_padOpen) {  // r207
    *aWord = g_padKey == kPadOk ? "SET STAKE" : (g_padKey == kPadClear ? "CLEAR" : (g_padKey == kPadBack ? "BACKSPACE" : "TYPE"));
    *aDim = false;
    *xWord = "";
  } else if (g_focus == 3 && g_tab == kTabLines) {
    *aWord = "ITS GAMES";
    *aDim = false;
  } else if (g_focus == 1 && g_tab != kTabBets) {
    *aWord = g_slipRow < nLegs ? "TAKE LEG OFF" : (g_slipRow == nLegs && g_stake == kStakeCustom ? "TYPE STAKE" : (nLegs > 1 ? "PLACE PARLAY" : "PLACE BET"));
    *aDim = g_slipRow >= nLegs && !(g_slipRow == nLegs && g_stake == kStakeCustom) && (slip_locked() || nLegs == 0);
  } else if (g_tab == kTabFutures) {
    *aWord = "ADD TO SLIP";
    *aDim = !futures_open() || g_futOpts[g_futMarket].empty();
  } else if (g_tab == kTabBets) {
    const Totals t = totals();
    if (t.ready > 0 && g_cursor == 0) {
      *aWord = "COLLECT " + std::to_string(t.ready);
      *aDim = false;
    } else {
      *aWord = "CANCEL TICKET";
      const std::vector<size_t> rows = bet_rows();
      const int row = g_cursor - (t.ready > 0 ? 1 : 0);
      *aDim = row < 0 || row >= static_cast<int>(rows.size()) || g_book.bets[rows[static_cast<size_t>(row)]].state != 0 || lkd;
    }
  } else {
    *aWord = "ADD TO SLIP";
    *aDim = g_tab == kTabLines ? ((g_weekAhead == 0 && lkd) || shown_lines().empty()) : (lkd || g_mine < 0);
  }
}
}  // namespace

void debug_set(const Debug& d) {
  std::lock_guard<std::mutex> lk(g_m);
  g_tab = d.tab;
  g_futMarket = d.futMarket;
  g_cursor = d.cursor;
  g_col = d.col;
  g_stake = d.stake;
  g_focus = d.focus;
  g_slipRow = d.slipRow;
  const size_t slipWas = g_slip.size();  // r224
  g_slip.clear();
  for (const Pick& p : d.slip)
    if (is_future(p.market)) {
      SlipLeg s;
      s.p = p;
      g_slip.push_back(s);
    } else {
      // r207: a pick from an upcoming matchday's board (p.week) - read it now (the proof runs on the game thread)
      const int wk = p.week >= 0 ? p.week : (g_dayValid ? g_day.week : -1);
      const std::vector<Fixture>* fx = g_dayValid && wk == g_day.week ? &g_fx : nullptr;
      if (fx == nullptr && find_week(wk) == nullptr) {
        WeekBoard wb;
        wb.week = wk;
        if (PortFranchiseBookWeekGet(wk, &wb.day)) {
          price_day(wb.day, &wb.fx);
          for (size_t i = 0; i < wb.fx.size(); ++i)
            if (!wb.fx[i].mine) wb.lines.push_back(static_cast<int>(i));
          g_weeks.push_back(wb);
        }
      }
      if (fx == nullptr)
        if (const WeekBoard* w = find_week(wk)) fx = &w->fx;
      if (fx == nullptr || p.game < 0 || p.game >= static_cast<int>(fx->size())) continue;
      SlipLeg s;
      s.p = p;
      s.p.week = wk;
      s.home = (*fx)[static_cast<size_t>(p.game)].home;
      s.away = (*fx)[static_cast<size_t>(p.game)].away;
      s.week = wk;
      g_slip.push_back(s);
    }
  g_now = d.now;
  g_flashAt = d.flashAgo >= 0.0 ? d.now - d.flashAgo : -1e9;
  g_winAt = d.winAgo >= 0.0 ? d.now - d.winAgo : -1e9;
  g_collectAt = d.collectAgo >= 0.0 ? d.now - d.collectAgo : -1e9;
  g_collectCoins = d.collectCoins;
  g_collectMachine = d.collectMachine;
  g_multAt = d.multAgo >= 0.0 ? d.now - d.multAgo : -1e9;
  g_multFrom = d.multFrom;
  g_msg = d.msg;
  g_msgRgb = d.msgRgb;
  g_msgAt = d.msg.empty() ? -1e9 : d.now;
  // r207
  g_weekAhead = std::max(0, d.weekAhead);
  g_custom = std::max(0LL, d.custom);
  g_padOpen = d.padOpen;
  g_padKey = std::max(0, std::min(kPadKeys - 1, d.padKey));
  g_padText = d.padText;
  if (g_weekAhead > 0) refresh_weeks();
  // r224: the lists' windows (-1: from the cursor, the way the input keeps them; a slip set shows its newest legs)
  g_linesTop = std::max(0, d.linesTop);
  g_betsTop = std::max(0, d.betsTop);
  g_legsSeen = d.legsTop >= 0 && g_slip.size() == slipWas ? g_slip.size() : 0;
  g_legsTop = std::max(0, d.legsTop);
  keep_in_view();
  ++g_gen;
}
Debug debug_get() {
  std::lock_guard<std::mutex> lk(g_m);
  Debug d;
  d.tab = g_tab, d.futMarket = g_futMarket, d.cursor = g_cursor, d.col = g_col, d.stake = g_stake, d.focus = g_focus, d.slipRow = g_slipRow;
  for (const SlipLeg& s : g_slip) d.slip.push_back(s.p);
  d.now = g_now;
  d.msg = g_msg;
  d.msgRgb = g_msgRgb;
  d.weekAhead = g_weekAhead, d.custom = g_custom, d.padOpen = g_padOpen, d.padKey = g_padKey, d.padText = g_padText;  // r207
  d.linesTop = g_linesTop, d.betsTop = g_betsTop, d.legsTop = g_legsTop;  // r224
  return d;
}
int debug_bet_count() {
  std::lock_guard<std::mutex> lk(g_m);
  return static_cast<int>(g_book.bets.size());
}
std::vector<Bet> debug_bets() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_book.bets;
}
std::vector<Leg> debug_slip_legs(std::string* err) {
  std::lock_guard<std::mutex> lk(g_m);
  return slip_to_legs(err);
}
long long debug_slip_pays(long long stake) {
  std::lock_guard<std::mutex> lk(g_m);
  return g_slip.empty() ? 0 : parlay_pays(stake, slip_mult());
}
void debug_load(const PortFranchiseBookDay& day, const std::vector<Bet>& bets, const PortFranchiseBookFutures* fut) {
  std::lock_guard<std::mutex> lk(g_m);
  g_book = Book();
  g_book.career = day.seed;
  g_book.loaded = true;
  g_book.bets = bets;
  for (const Bet& b : bets) {
    g_book.nextId = std::max(g_book.nextId, b.id + 1);
    if (b.state == 1 && b.paid > g_book.bigPays) {
      g_book.bigPays = b.paid;
      g_book.bigWords = b.legs.size() > 1 ? std::to_string(b.legs.size()) + "-LEG PARLAY" : leg_words(b.legs[0]);
    }
  }
  g_day = day;
  g_dayValid = day.valid;
  price_day(g_day, &g_fx);
  g_lines.clear();
  g_mine = -1;
  for (size_t i = 0; i < g_fx.size(); ++i) {
    if (g_fx[i].mine) g_mine = static_cast<int>(i);
    else g_lines.push_back(static_cast<int>(i));
  }
  if (fut != nullptr) {
    g_fut = *fut;
    price_futures(g_fut, g_futOpts);
  }
  ++g_gen;
}

// ======================================================================================================== the drawing
namespace {
thread_local CursorBox* t_sink = nullptr;  // r205
}  // namespace
void set_cursor_sink(CursorBox* sink) { t_sink = sink; }
CursorBox* cursor_sink() { return t_sink; }
void cursor_anim(float* moveAgo, float* pressAgo) {
  if (xrcasino::screen() != xrcasino::kBook) {
    xrcasino::cursor_anim(moveAgo, pressAgo);
    return;
  }
  std::lock_guard<std::mutex> lk(g_m);
  *moveAgo = static_cast<float>(g_now - g_moveAt);
  *pressAgo = static_cast<float>(g_now - g_pressAt);
}
bool animating_board() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_now - g_flashAt < 0.7 || g_now - g_multAt < 0.8 || g_now - g_collectAt < 2.6 || g_now - g_winAt < 4.0;
}

void cursor_frame(std::vector<xrtext::BoardPiece>& v, int layer, float x0, float y0, float x1, float y1, float r, float t, float moveAgo,
                  float pressAgo, uint32_t rgb) {
  if (t_sink != nullptr) {  // r205: the machine's screen - the cursor is its overlay: noted, not drawn
    if (layer == 0) {
      t_sink->glow = true;
    } else {
      t_sink->on = true;
      t_sink->x0 = x0, t_sink->y0 = y0, t_sink->x1 = x1, t_sink->y1 = y1, t_sink->r = r;
      t_sink->rgb = rgb;
    }
    return;
  }
  xrmenu::Kit K{v};
  const float g = moveAgo >= 0.0f && moveAgo < 0.22f ? 1.0f - moveAgo / 0.22f : 0.0f;
  const float pop = g * g;  // (out fast, settling)
  const float press = pressAgo >= 0.0f && pressAgo < 0.28f ? 1.0f - pressAgo / 0.28f : 0.0f;
  const float grow = 5.0f + 16.0f * pop - 9.0f * press;
  const float a0 = x0 - grow, b0 = y0 - grow, a1 = x1 + grow, b1 = y1 + grow, rr = r + grow;
  if (layer == 0) {
    const float a = std::min(1.0f, 0.7f + 0.15f * std::sin(t * 6.0f) + 0.3f * pop + 0.3f * press);
    K.glow(a0 - 48, b0 - 48, a1 + 48, b1 + 48, rgb, a);         // (a wide halo ..
    K.glow(a0 - 18, b0 - 18, a1 + 18, b1 + 18, rgb, 0.8f * a);  // .. and a hot one close in)
    return;
  }
  if (press > 0.0f) K.box(x0, y0, x1, y1, r, 0xFFFFFF, 0.55f * press);  // (picked: a white flash)
  K.ring(a0, b0, a1, b1, rr, 13.0f, rgb);                                 // the tube
  K.ring(a0 + 1.5f, b0 + 1.5f, a1 - 1.5f, b1 - 1.5f, std::max(2.0f, rr - 1.5f), 4.0f, 0xFFFFFF, 0.95f);  // its white-hot core
  const float bh = std::max(10.0f, 0.62f * (b1 - b0));
  const float bc = 0.5f * (b0 + b1);
  K.box(a0 + 7, bc - 0.5f * bh, a0 + 17, bc + 0.5f * bh, 5.0f, 0xFFFFFF);  // the bar down its left edge
}

namespace {
// the TIZM SPORTSBOOK palette: a dark Vegas night, neon tubes, the card brand's chrome + prism, coin gold
constexpr uint32_t kNight0 = 0x0B0718, kNight1 = 0x05030C, kPanel = 0x140E2A, kPanelHi = 0x241A48, kPink = 0xFF2E88,
                   kCyan = 0x2EF2FF, kLime = 0x39FF88, kGold = 0xFFD23F, kAmber = 0xFFB020, kRed = 0xFF4D5E, kInk = 0xF4F1FF,
                   kDim = 0x9A92B8, kLine = 0x3A2E66, kChrome0 = 0xFFFFFF, kChrome1 = 0x9FB2D6;

struct Pen {  // the base page's pieces, put at (ox, oy) x k when done
  std::vector<xrtext::BoardPiece> v;
  xrmenu::Kit K{v};
};
// r204: the cursor (cursor_frame) on the sportsbook's boards (g_m held): its glow under the item, its frame over it; the fill a
// selected item takes (a deep cyan: the whole thing lit, not only its edge)
constexpr uint32_t kCurFill = 0x0D3F52;
void cur_under(Pen& P, float x0, float y0, float x1, float y1, float r, float t) {
  cursor_frame(P.v, 0, x0, y0, x1, y1, r, t, static_cast<float>(g_now - g_moveAt), static_cast<float>(g_now - g_pressAt));
}
void cur_over(Pen& P, float x0, float y0, float x1, float y1, float r, float t, uint32_t rgb = 0x2EF2FF) {
  cursor_frame(P.v, 1, x0, y0, x1, y1, r, t, static_cast<float>(g_now - g_moveAt), static_cast<float>(g_now - g_pressAt), rgb);
}
// r205: an item drawn lit as the cursor's (its cyan fill, its brighter glow) - never on the machine's screen (the overlay lights it):
// there the picture must not change when the cursor moves
bool lit(bool cur) { return cur && t_sink == nullptr; }

void neon_text(Pen& P, const std::string& t, float x, float y, float px, float room, uint32_t rgb, int align, float glowA = 0.5f) {
  const float w = std::min(room, xrtext::board_text_width(t, px));
  const float cx = align == 0 ? x : (align < 0 ? x + 0.5f * w : x - 0.5f * w);
  if (glowA > 0.0f) P.K.glow(cx - 0.5f * w - px * 0.6f, y - px * 0.95f, cx + 0.5f * w + px * 0.6f, y + px * 0.95f, rgb, glowA);
  xrtext::BoardPiece p;
  p.kind = align == 0 ? xrtext::BoardPiece::kText : (align < 0 ? xrtext::BoardPiece::kTextLeft : xrtext::BoardPiece::kTextRight);
  p.x0 = x;
  p.y0 = y;
  p.x1 = room;
  p.y1 = px;
  p.rgb = xrmenu::mix(rgb, 0xFFFFFF, 0.55f);  // (a white-hot core, the tube's colour round it)
  p.rgb2 = rgb;
  p.outline = std::max(2.0f, px * 0.07f);
  p.outlineRgb = xrmenu::mix(rgb, 0x000000, 0.45f);
  p.text = t;
  P.v.push_back(p);
}

void chrome_text(Pen& P, const std::string& t, float x, float y, float px, float room, int align) {
  xrtext::BoardPiece p;
  p.kind = align == 0 ? xrtext::BoardPiece::kText : (align < 0 ? xrtext::BoardPiece::kTextLeft : xrtext::BoardPiece::kTextRight);
  p.x0 = x;
  p.y0 = y;
  p.x1 = room;
  p.y1 = px;
  p.rgb = kChrome0;
  p.rgb2 = kChrome1;
  p.outline = std::max(3.0f, px * 0.08f);
  p.outlineRgb = 0x1A1036;
  p.shadow = px * 0.06f;
  p.shadowRgb = kPink;
  p.shadowA = 0.9f;
  p.text = t;
  P.v.push_back(p);
}

void coin(Pen& P, float cx, float cy, float r) {
  P.K.glow(cx - 2.2f * r, cy - 2.2f * r, cx + 2.2f * r, cy + 2.2f * r, kGold, 0.35f);
  P.K.box(cx - r, cy - r, cx + r, cy + r, r, 0xB8860B);
  P.K.box(cx - 0.86f * r, cy - 0.86f * r, cx + 0.86f * r, cy + 0.86f * r, 0.86f * r, kGold, 1.0f, 0xE0A800);
  // r216: a BIT COIN - the pixel ring + the 8-bit B (r199-r215: a ring + "T")
  xrcoin::bit_face([&](float x0, float y0, float x1, float y1, uint32_t rgb) { P.K.box(x0, y0, x1, y1, 0, rgb); }, cx, cy, r);
}

// a prism refractor's shards behind the brand (the cards' LANCINI TIZM look)
void prism(Pen& P, float x0, float y0, float x1, float y1, float t) {
  static const uint32_t kBand[6] = {0xFF2E88, 0xFFB020, 0xFFE94A, 0x39FF88, 0x2EF2FF, 0x9B5CFF};
  const float w = x1 - x0, h = y1 - y0;
  for (int i = 0; i < 9; ++i) {
    const float s = std::fmod(i * 0.137f + t * 0.05f, 1.0f);
    const float a = x0 + s * w * 1.3f - 0.15f * w, b = a + w * 0.07f;
    P.K.poly({a, y1, b, y1, b + 0.55f * h, y0, a + 0.55f * h - w * 0.03f, y0}, kBand[i % 6], 0.10f);
  }
}

std::string coins_text(long long n) { return coins_text_book(n); }
std::string mult_text(double m) {
  // r207: a crazy parlay's multiplier - X12,345 with commas; r208 (no cap): past a million short ("X1.24M", "X1.13T"), past
  // xrcoin's range "X1.2E30"
  if (!(m < 1e15)) {
    if (!(m < 1e300)) return "X1E300+";
    char b[32];
    std::snprintf(b, sizeof(b), "X%.1fE%d", m / std::pow(10.0, std::floor(std::log10(m))), static_cast<int>(std::floor(std::log10(m))));
    return b;
  }
  if (m >= 1e6) return "X" + xrcoin::compact(static_cast<long long>(std::floor(m)));  // ("X1.24M": as short as r207's "X1M+" there)
  if (m >= 1000.0) return "X" + coins_text(static_cast<long long>(std::floor(m)));
  char b[32];
  std::snprintf(b, sizeof(b), m >= 100.0 ? "X%.0f" : (m >= 10.0 ? "X%.1f" : "X%.2f"), m);
  return b;
}

void logo_img(Pen& P, int club, float x0, float y0, float x1, float y1, float alpha = 1.0f) {
  int w = 0, h = 0;
  const uint8_t* img = g_logo != nullptr ? g_logo(club, &w, &h) : nullptr;
  if (img == nullptr || w <= 0 || h <= 0) {
    const char* pl = PortFranchiseBookClubPlace(club);
    P.K.box(x0, y0, x1, y1, 0.5f * (x1 - x0), kPanelHi, alpha);
    P.K.text(std::string(pl != nullptr && pl[0] ? pl : "?").substr(0, 2), 0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.45f * (y1 - y0), x1 - x0,
             kInk, 0.0f);
    return;
  }
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kImage;
  p.x0 = x0;
  p.y0 = y0;
  p.x1 = x1;
  p.y1 = y1;
  p.img = img;
  p.iw = w;
  p.ih = h;
  p.alpha = alpha;
  P.v.push_back(p);
}

std::string place(int club) {
  const char* p = PortFranchiseBookClubPlace(club);
  return p != nullptr && p[0] != '\0' ? std::string(p) : std::string("?");
}

void streak_tag(Pen& P, int streak, float x, float cy) {
  if (streak == 0) return;
  const bool hot = streak > 0;
  const std::string t = (hot ? "W" : "L") + std::to_string(std::abs(streak));
  const float w = xrtext::board_text_width(t, 30) + 24;
  P.K.box(x, cy - 18, x + w, cy + 18, 18, hot ? 0x145A34 : 0x5A1424);
  P.K.text(t, x + 0.5f * w, cy + 1, 30, w, hot ? kLime : kRed, 0.0f);
}

bool on_slip(int home, int away, int week, int market, int side) {
  for (const SlipLeg& s : g_slip)
    if (s.home == home && s.away == away && s.week == week && s.p.market == market && s.p.side == side) return true;
  return false;
}

// a price cell: its spread (dim) + the price (bright: plus = lime, minus = ice white), lit when it's the cursor / on the slip
void price_cell(Pen& P, float x0, float y0, float x1, float y1, const Pick& p, bool cur, bool picked, bool locked, float t,
                int line, int col) {
  const bool off = p.american == 0;
  const uint32_t base = lit(cur) ? kCurFill : (picked ? 0x3B2470 : kPanel);
  if (cur) cur_under(P, x0, y0, x1, y1, 14, t);
  P.K.box(x0, y0, x1, y1, 14, base);
  P.K.ring(x0, y0, x1, y1, 14, picked ? 4.0f : 2.0f, picked ? kPink : kLine);
  const float cy = 0.5f * (y0 + y1) + 1;
  if (off || locked) {
    P.K.text(off ? "OFF" : "LOCKED", 0.5f * (x0 + x1), cy, 34, x1 - x0 - 16, kDim, 0.0f);
  } else if (p.market == kSpread) {
    P.K.left(spread_text(p.spread), x0 + 16, cy, 40, 0.48f * (x1 - x0), 0xC9C2E6, 2.0f);
    P.K.right(price_text(p.american), x1 - 16, cy, 42, 0.5f * (x1 - x0), p.american > 0 ? kLime : kInk, 2.0f);
  } else {
    P.K.text(price_text(p.american), 0.5f * (x0 + x1), cy, 44, x1 - x0 - 16, p.american > 0 ? kLime : kInk, 2.0f);
  }
  if (picked) {  // (on the slip: a pink check in its corner)
    P.K.box(x1 - 26, y0 - 8, x1 + 8, y0 + 26, 17, kPink);
    P.K.text("+", x1 - 9, y0 + 9, 30, 30, 0xFFFFFF, 0.0f);
  }
  if (cur) cur_over(P, x0, y0, x1, y1, 14, t);
  g_hits.push_back({x0, y0, x1, y1, 0, line, col});
}

// r224: a list's UP / DOWN arrows at its foot - two buttons the pointer's trigger pages the list with (hit kind 13: a -1 / +1,
// b the list); each hit box runs on below the button (hitY1, nothing to hit there) so it's easy to land on
void list_arrows(Pen& P, float ux0, float ux1, float dx0, float dx1, float y0, float y1, float hitY1, bool canUp, bool canDown, int list) {
  const float r = 0.5f * (y1 - y0), cy = 0.5f * (y0 + y1), hh = 0.32f * (y1 - y0);
  for (int k = 0; k < 2; ++k) {
    const float x0 = k == 0 ? ux0 : dx0, x1 = k == 0 ? ux1 : dx1, cx = 0.5f * (x0 + x1);
    const bool can = k == 0 ? canUp : canDown;
    P.K.box(x0, y0, x1, y1, r, can ? kPanelHi : 0x150E2C);
    P.K.ring(x0, y0, x1, y1, r, 2.0f, can ? kCyan : kLine);
    if (k == 0) P.K.poly({cx - 22, cy + hh, cx, cy - hh, cx + 22, cy + hh}, can ? kCyan : kLine);
    else P.K.poly({cx - 22, cy - hh, cx, cy + hh, cx + 22, cy - hh}, can ? kCyan : kLine);
    g_hits.push_back({x0, y0, x1, hitY1, 13, k == 0 ? -1 : 1, list});
  }
}

void draw_lines(Pen& P, float t, bool locked) {
  const float x0 = 0, x1 = 1080;
  // r207 (CRAZY PARLAYS): the WEEK PICKER - this matchday or one ahead (up off the first line; the pointer: its arrows)
  {
    const float px0 = 0, px1 = 500, py0 = 206, py1 = 250;
    const bool cur = g_focus == 3;
    const int mx = max_ahead();
    if (cur) cur_under(P, px0, py0, px1, py1, 22, t);
    P.K.box(px0, py0, px1, py1, 22, lit(cur) ? kCurFill : (g_weekAhead > 0 ? 0x3A1450 : 0x150E2C));
    P.K.ring(px0, py0, px1, py1, 22, g_weekAhead > 0 ? 3.0f : 2.0f, g_weekAhead > 0 ? kPink : kLine);
    const std::string wk = !g_dayValid ? std::string("NO GAMES TODAY")
                                        : (g_weekAhead == 0 ? (g_day.phase == PORT_FR_PHASE_PLAYOFFS ? std::string("TODAY") : "THIS WEEK: " + std::to_string(g_day.week))
                                                            : "WEEK " + std::to_string(shown_week()) + "  (+" + std::to_string(g_weekAhead) + ")");
    P.K.text(wk, 250, 229, 30, 360, g_weekAhead > 0 ? 0xFFFFFF : kInk, 0.0f);
    const bool canL = g_weekAhead > 0, canR = g_weekAhead < mx;
    P.K.poly({20, 228, 40, 214, 40, 242}, canL ? kCyan : kLine);
    P.K.poly({480, 228, 460, 214, 460, 242}, canR ? kCyan : kLine);
    g_hits.push_back({px0, py0, 70, py1, 11, -1, 0});
    g_hits.push_back({430, py0, px1, py1, 11, 1, 0});
    g_hits.push_back({70, py0, 430, py1, 12, 0, 0});
    if (cur) cur_over(P, px0, py0, px1, py1, 22, t);
  }
  P.K.text("MONEY LINE", 688, 228, 32, 220, kDim, 0.0f);
  P.K.text("SPREAD", 948, 228, 32, 260, kDim, 0.0f);
  const std::vector<int>& lines = shown_lines();
  const std::vector<Fixture>& fx = shown_fx();
  const int wkShown = shown_week();
  if (g_weekAhead > 0) locked = false;  // (an upcoming matchday: open till its own kickoff)
  const int n = static_cast<int>(lines.size());
  if (n == 0) {
    P.K.text(g_weekAhead > 0 ? "NO GAMES ON THE BOARD THAT WEEK" : (g_dayValid ? "NO OTHER GAMES TODAY" : "NO GAMES TODAY"), 540, 520, 60, 980, kDim, 2.0f);
    return;
  }
  constexpr int kRows = kLinesRows;
  const int curFix = g_cursor / 2;
  // r224: the window is state (g_linesTop, moved by the stick at its edges or its arrows) - r223 re-centred it on the cursor here,
  // so a pointer hovering a row scrolled the list away under it
  const int first = std::max(0, std::min(std::max(0, n - kRows), g_linesTop));
  for (int r = 0; r < kRows && first + r < n; ++r) {
    const int fi = first + r;
    const Fixture& f = fx[static_cast<size_t>(lines[static_cast<size_t>(fi)])];
    const float y0 = 252 + r * 126.0f, y1 = y0 + 116;
    const bool rowCur = g_focus == 0 && curFix == fi;
    P.K.box(x0, y0, x1, y1, 22, lit(rowCur) ? 0x1C1438 : 0x110B22);
    P.K.ring(x0, y0, x1, y1, 22, 2, lit(rowCur) ? 0x5A3FA0 : 0x2A2050);
    for (int k = 0; k < 2; ++k) {  // away (top) then home
      const int club = k == 0 ? f.away : f.home, streak = k == 0 ? f.streakA : f.streakH;
      const float ly0 = y0 + 6 + k * 54.0f, ly1 = ly0 + 50, cy = 0.5f * (ly0 + ly1);
      logo_img(P, club, x0 + 14, ly0 + 2, x0 + 60, ly1 - 2);
      const std::string nm = (k == 1 ? "@ " : "") + place(club);
      P.K.left(nm, x0 + 72, cy + 1, 40, 380, kInk, 2.0f);
      streak_tag(P, streak, x0 + 72 + std::min(380.0f, xrtext::board_text_width(nm, 40)) + 14, cy);
      const int line = fi * 2 + k, side = k == 0 ? 1 : 0;
      price_cell(P, 576, ly0, 800, ly1, f.ml[side], g_focus == 0 && g_cursor == line && g_col == 0,
                 on_slip(f.home, f.away, wkShown, kMoneyLine, side), locked, t, line, 0);
      price_cell(P, 816, ly0, 1076, ly1, f.sp[side], g_focus == 0 && g_cursor == line && g_col == 1,
                 on_slip(f.home, f.away, wkShown, kSpread, side), locked, t, line, 1);
    }
    if (f.series >= 0) P.K.text("PLAYOFFS", 500, y0 + 58, 30, 120, kAmber, 0.0f);
  }
  if (n > kRows) {
    const float cy = 892;
    P.K.text(std::to_string(first + 1) + "-" + std::to_string(std::min(n, first + kRows)) + " OF " + std::to_string(n) + " GAMES", 540,
             cy, 32, 380, kDim, 0.0f);
    list_arrows(P, 150, 330, 750, 930, 876, 903, 940, first > 0, first + kRows < n, 0);  // r224: the pointer pages with these
  }
}

void draw_winby(Pen& P, float t, bool locked) {
  if (g_mine < 0) {
    P.K.text("NO GAME FOR YOU TODAY", 540, 470, 64, 980, kDim, 2.0f);
    P.K.text("WIN BY OPENS ON YOUR NEXT MATCHDAY", 540, 560, 40, 980, kDim, 0.0f);
    return;
  }
  const Fixture& f = g_fx[static_cast<size_t>(g_mine)];
  const bool home = f.home == g_day.myClub;
  const int me = home ? f.home : f.away, them = home ? f.away : f.home;
  P.K.box(0, 210, 1080, 420, 24, 0x110B22);
  P.K.glow(40, 200, 400, 430, kPink, 0.25f);
  P.K.glow(680, 200, 1040, 430, kCyan, 0.25f);
  logo_img(P, me, 30, 226, 190, 386);
  logo_img(P, them, 890, 226, 1050, 386);
  neon_text(P, "YOU", 320, 262, 40, 240, kGold, 0, 0.3f);
  P.K.text(place(me), 320, 322, 44, 250, kInk, 2.0f);
  P.K.text(home ? "AWAY" : "HOME", 760, 262, 34, 240, kDim, 0.0f);
  P.K.text(place(them), 760, 322, 44, 250, kInk, 2.0f);
  neon_text(P, "VS", 540, 290, 76, 140, kPink, 0, 0.45f);
  P.K.text(home ? "AT HOME" : "ON THE ROAD", 540, 382, 32, 260, kDim, 0.0f);
  // r225: WIN BY k OR MORE, its half-goal line under it (a real book's -0.5 / -1.5 / -2.5 / -3.5)
  const char* const kBig[4] = {"1+", "2+", "3+", "4+"};
  const char* const kHalfLine[4] = {"(-0.5)", "(-1.5)", "(-2.5)", "(-3.5)"};
  const long long bal = balance(), st = stake_coins(g_stake, bal);
  for (int k = 0; k < 4; ++k) {
    const Pick& p = f.winBy[k + 1];
    const float tx0 = k * 272.0f, tx1 = tx0 + 256, ty0 = 448, ty1 = 760;
    const bool cur = g_focus == 0 && g_cursor == k;
    const bool picked = on_slip(f.home, f.away, g_day.week, kWinBy, k + 1);
    if (cur) cur_under(P, tx0, ty0, tx1, ty1, 26, t);
    P.K.box(tx0, ty0, tx1, ty1, 26, lit(cur) ? kCurFill : (picked ? 0x3B2470 : kPanel), 1.0f, 0x0A0716);
    P.K.ring(tx0, ty0, tx1, ty1, 26, picked ? 5.0f : 3.0f, picked ? kPink : kLine);
    P.K.text("WIN BY", 0.5f * (tx0 + tx1), ty0 + 40, 34, 230, kDim, 0.0f);
    neon_text(P, kBig[k], 0.5f * (tx0 + tx1), ty0 + 120, 104, 230, k == 3 ? kPink : kGold, 0, 0.35f);
    P.K.text(kHalfLine[k], 0.5f * (tx0 + tx1), ty0 + 186, 30, 230, kInk, 0.0f);
    if (locked) {
      P.K.text("LOCKED", 0.5f * (tx0 + tx1), ty0 + 240, 44, 230, kDim, 0.0f);
    } else {
      P.K.text(price_text(p.american), 0.5f * (tx0 + tx1), ty0 + 240, 50, 230, kLime, 2.0f);
      P.K.text("ALONE PAYS " + xrcoin::fit(payout(st, p.american), 9), 0.5f * (tx0 + tx1), ty0 + 288, 30, 236, kGold, 0.0f);
    }
    if (cur) cur_over(P, tx0, ty0, tx1, ty1, 26, t);
    g_hits.push_back({tx0, ty0, tx1, ty1, 1, k, 0});
  }
  int cnt[5] = {0, 0, 0, 0, 0};  // how you've really been winning this season
  for (int k = 0; k < g_day.myMarginCount && k < 24; ++k) {
    const int m = g_day.myMargins[k];
    ++cnt[m <= 0 ? 0 : std::min(4, m)];
  }
  P.K.left("YOUR WINS THIS SEASON", 0, 802, 28, 240, kDim, 0.0f);
  P.K.left("(YOUR RECORD, NOT A BET)", 0, 834, 22, 240, kDim, 0.0f);
  const char* const kLab[5] = {"LOST", "BY 1", "BY 2", "BY 3", "BY 4+"};
  for (int k = 0; k < 5; ++k) {
    const int idx = k < 4 ? k + 1 : 0;  // (by 1 .. 4+, then lost)
    const float bx = 250 + k * 166.0f;
    P.K.box(bx, 790, bx + 150, 842, 26, idx == 0 ? 0x3A1424 : 0x1A2A4A);
    P.K.text(std::string(kLab[idx]) + " X" + std::to_string(cnt[idx]), bx + 75, 817, 30, 140, idx == 0 ? kRed : kInk, 0.0f);
  }
  P.K.text("THIS GAME ONLY  -  A BET ON YOURSELF  -  ADD IT TO A PARLAY TOO", 540, 892, 30, 1000, kDim, 0.0f);
}

void draw_futures(Pen& P, float t) {
  static const char* const kMk[4] = {"CHAMPION", "MVP", "GOLDEN BOOT", "ROOKIE OF THE YEAR"};
  const bool open = futures_open();
  for (int m = 0; m < 4; ++m) {  // the four races (left / right)
    const float cx0 = m * 270.0f, cx1 = cx0 + 262;
    const bool on = g_futMarket == m;
    if (on) P.K.glow(cx0 - 16, 194, cx1 + 16, 268, kGold, 0.3f);
    P.K.box(cx0, 206, cx1, 256, 25, on ? 0x4A3A08 : 0x150E2C);
    P.K.ring(cx0, 206, cx1, 256, 25, on ? 4.0f : 2.0f, on ? kGold : kLine);
    P.K.text(kMk[m], 0.5f * (cx0 + cx1), 232, 30, 244, on ? kGold : kDim, on ? 2.0f : 0.0f);
    g_hits.push_back({cx0, 206, cx1, 256, 8, m, 0});
  }
  P.K.text(open ? "THE WHOLE SEASON  -  CLOSES AT THE PLAYOFFS  -  THE PRICES MOVE EVERY MATCHDAY"
                : (g_fut.valid && g_fut.phase != PORT_FR_PHASE_REGULAR ? "CLOSED FOR THIS SEASON: OPEN AGAIN NEXT SEASON"
                                                                       : "CLOSED WHILE THE DAY IS IN PLAY"),
           540, 284, 26, 1060, open ? kDim : kRed, 0.0f);
  const std::vector<Pick>& o = g_futOpts[g_futMarket];
  if (o.empty()) {
    P.K.text(g_futMarket == 3 ? "NO ROOKIES ON THE BOARD YET" : "NO ONE ON THIS BOARD YET", 540, 560, 56, 980, kDim, 2.0f);
    return;
  }
  for (size_t i = 0; i < o.size() && i < 8; ++i) {
    const Pick& p = o[i];
    const float y0 = 306 + i * 72.0f, y1 = y0 + 64, cy = 0.5f * (y0 + y1);
    const bool cur = g_focus == 0 && g_cursor == static_cast<int>(i);
    bool picked = false;
    for (const SlipLeg& s : g_slip) picked = picked || (s.p.market == p.market && s.p.who == p.who);
    if (cur) cur_under(P, 0, y0, 1080, y1, 20, t);
    P.K.box(0, y0, 1080, y1, 20, lit(cur) ? kCurFill : 0x110B22);
    P.K.ring(0, y0, 1080, y1, 20, 2.0f, 0x2A2050);
    P.K.text(std::to_string(i + 1), 34, cy + 1, 32, 50, i == 0 ? kGold : kDim, 0.0f);
    logo_img(P, p.club, 64, y0 + 6, 116, y1 - 6);
    P.K.left(p.whoName, 132, cy + 1, 40, 470, kInk, 2.0f);
    const std::string sub = p.market == kChampion ? p.stat : place(p.club) + "  -  " + p.stat;
    P.K.right(sub, 790, cy + 1, 28, 260, kDim, 0.0f);
    // its chance as a little bar (the book's own, before its edge)
    P.K.box(600, y1 - 12, 790, y1 - 6, 3, 0x2A2050);
    P.K.box(600, y1 - 12, 600 + 190 * static_cast<float>(std::min(1.0, p.fair)), y1 - 6, 3, kCyan);
    price_cell(P, 816, y0 + 6, 1076, y1 - 6, p, false, picked, !open, t, static_cast<int>(i), 0);
    g_hits.back().kind = 7;  // (the price cell's hit box: a future's row)
    g_hits.back().x0 = 0;    // (r204: the whole row is its hit box)
    if (cur) cur_over(P, 0, y0, 1080, y1, 20, t);
  }
}

void draw_bets(Pen& P, float t, bool cabinet) {
  const Totals tt = totals();
  const long long profit = tt.returned - tt.staked;
  struct Stat {
    const char* label;
    std::string value;
    uint32_t rgb;
  } stats[4] = {{"TICKETS", std::to_string(tt.bets) + (tt.open > 0 ? " + " + std::to_string(tt.open) + " OPEN" : ""), kInk},
                {"WON - LOST", std::to_string(tt.won) + " - " + std::to_string(tt.lost), kInk},
                // r208: big ones short past 11 characters ("+4.09T")
                {"PROFIT", (profit > 0 ? "+" : "") + xrcoin::fit(profit, 11), profit > 0 ? kLime : (profit < 0 ? kRed : kInk)},
                {"BIGGEST HIT", g_book.bigPays > 0 ? xrcoin::fit(g_book.bigPays, 11) : std::string("-"), kGold}};
  for (int k = 0; k < 4; ++k) {
    const float bx0 = k * 272.0f, bx1 = bx0 + 256;
    P.K.box(bx0, 210, bx1, 318, 22, kPanel, 1.0f, 0x0A0716);
    P.K.ring(bx0, 210, bx1, 318, 22, 2, kLine);
    P.K.text(stats[k].label, 0.5f * (bx0 + bx1), 240, 30, 236, kDim, 0.0f);
    P.K.text(stats[k].value, 0.5f * (bx0 + bx1), 288, 44, 236, stats[k].rgb, 2.0f);
  }
  const std::vector<size_t> rows = bet_rows();
  float y = 334;
  const int extra = tt.ready > 0 ? 1 : 0;
  if (extra) {  // COLLECT: the winnings waiting
    const bool cur = g_focus == 0 && g_cursor == 0;
    const float y1 = y + 78;
    P.K.glow(-30, y - 30, 1110, y1 + 30, kGold, (lit(cur) ? 0.45f : 0.3f) + 0.15f * std::sin(t * 5.0f));
    if (cur) cur_under(P, 0, y, 1080, y1, 32, t);
    P.K.box(0, y, 1080, y1, 32, 0x5A4200, 1.0f, 0x2A1E00);
    P.K.ring(0, y, 1080, y1, 32, 3.0f, kGold);
    coin(P, 48, y + 39, 24);
    neon_text(P, "COLLECT " + coins_text(tt.ready) + " BIT COINS", 540, y + 41, 46, 860, kGold, 0, 0.0f);
    P.K.right(std::to_string(tt.readyTickets) + (tt.readyTickets == 1 ? " TICKET" : " TICKETS"), 1060, y + 41, 30, 170, 0xFFE9A0, 0.0f);
    if (cur) cur_over(P, 0, y, 1080, y1, 32, t);
    g_hits.push_back({0, y, 1080, y1, 5, 0, 0});
    y = y1 + 10;
  }
  if (rows.empty()) {
    P.K.text("NO TICKETS YET", 540, 560, 64, 980, kDim, 2.0f);
    P.K.text("PICK A PRICE ON LEAGUE LINES OR WIN BY", 540, 640, 36, 980, kDim, 0.0f);
    return;
  }
  const int kRows = bets_rows_shown(extra != 0);
  const int n = static_cast<int>(rows.size());
  const int curRow = g_cursor - extra;
  const int first = std::max(0, std::min(std::max(0, n - kRows), g_betsTop));  // r224: state (g_betsTop), not re-centred
  for (int r = 0; r < kRows && first + r < n; ++r) {
    const Bet& b = g_book.bets[rows[static_cast<size_t>(first + r)]];
    const float y0 = y + r * 66.0f, y1 = y0 + 58;
    const bool cur = g_focus == 0 && curRow == first + r;
    if (cur) cur_under(P, 0, y0, 1080, y1, 18, t);
    P.K.box(0, y0, 1080, y1, 18, lit(cur) ? kCurFill : 0x110B22);
    const bool waiting = (b.state == 1 || b.state == 3) && !b.collected;
    static const char* const kState[4] = {"OPEN", "WON", "LOST", "VOID"};
    static const uint32_t kStateRgb[4] = {kAmber, kLime, kRed, kDim};
    const int s = std::max(0, std::min(3, b.state));
    P.K.box(12, y0 + 9, 142, y1 - 9, 20, xrmenu::mix(kStateRgb[s], 0x000000, 0.65f));
    P.K.text(kState[s], 77, 0.5f * (y0 + y1) + 1, 30, 120, kStateRgb[s], 0.0f);
    std::string what = "#" + std::to_string(b.id) + "  ";
    if (b.legs.size() > 1) what += std::to_string(b.legs.size()) + "-LEG PARLAY " + mult_text(multiplier(b, false));
    else if (b.legs[0].market == kWinBy) what += "YOUR GAME: " + leg_words(b.legs[0]);
    else if (is_future(b.legs[0].market)) what += "FUTURE  " + leg_words(b.legs[0]);
    else what += leg_words(b.legs[0]);
    P.K.left(what, 160, 0.5f * (y0 + y1) + 1, 32, 600, kInk, 0.0f);
    // r208: the columns' big numbers short ("1M", "+4.09T READY") so they stay readable
    P.K.right(xrcoin::fit(b.stake, 6), 870, 0.5f * (y0 + y1) + 1, 32, 100, kDim, 0.0f);
    const std::string res = b.state == 1 ? (waiting ? "+" + xrcoin::fit(b.paid, 7) + " READY" : "+" + xrcoin::fit(b.paid, 9))
                                         : (b.state == 2 ? "-" + xrcoin::fit(b.stake, 9)
                                                         : (b.state == 3 ? (waiting ? "BACK: READY" : "BACK") : "PAYS " + xrcoin::fit(b.pays, 7)));
    P.K.right(res, 1064, 0.5f * (y0 + y1) + 1, 32, 180, b.state == 1 ? kLime : (b.state == 2 ? kRed : kGold), 0.0f);
    if (cur) cur_over(P, 0, y0, 1080, y1, 18, t);
    g_hits.push_back({0, y0, 1080, y1, 5, first + r + extra, 0});
  }
  const bool more = n > kRows;  // r224: more tickets than fit: the arrows either side of the foot line (the pointer pages)
  P.K.text(cabinet ? "A / X: PRINT THE SLIP (ANY TICKET, ANY TIME)" : "A ON AN OPEN TICKET: CANCEL IT (BEFORE KICKOFF)", 540, 892, 30,
           more ? 780.0f : 1000.0f, kDim, 0.0f);
  if (more) list_arrows(P, 0, 130, 950, 1080, 878, 903, 940, first > 0, first + kRows < n, 1);
}

// r207: the CUSTOM stake's NUMBER PAD over the slip's panel - what's typed big, 1-9 / CLR / 0 / DEL, OK; the cursor on its keys
// (the machine's overlay too), the pointer on any key
void draw_pad(Pen& P, float t, float x0, float x1, long long bal) {
  const float cx = 0.5f * (x0 + x1);
  const long long typed = g_padText.empty() ? 0 : std::atoll(g_padText.c_str());
  const bool over = typed > bal;
  P.K.box(x0 + 24, 214, x1 - 24, 300, 22, 0x07040F);
  P.K.ring(x0 + 24, 214, x1 - 24, 300, 22, 3.0f, over ? kRed : kGold);
  coin(P, x0 + 70, 257, 26);
  if (g_padText.empty()) P.K.right("0", x1 - 50, 259, 64, 380, 0x5A5070, 0.0f);
  else neon_text(P, coins_text(typed), x1 - 50, 259, 66, 390, over ? kRed : kGold, 1, 0.35f);
  const bool blink = std::fmod(t, 1.0f) < 0.55f;
  if (blink) P.K.box(x1 - 44, 232, x1 - 38, 286, 2, over ? kRed : kGold);  // (the caret)
  P.K.text(over ? "MORE THAN YOUR " + coins_text(bal) + " COINS" : "ANY AMOUNT: 1 TO " + coins_text(std::max(0LL, bal)), cx, 322, 26, 500,
           over ? kRed : kDim, 0.0f);
  static const char* const kKey[12] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "CLR", "0", "DEL"};
  const float kw = (x1 - x0 - 80 - 2 * 14) / 3.0f, kh = 84, gap = 12;
  for (int k = 0; k < kPadKeys; ++k) {
    float bx0, by0, bx1, by1;
    if (k == kPadOk) {
      bx0 = x0 + 40, bx1 = x1 - 40, by0 = 344 + 4 * (kh + gap), by1 = by0 + 84;
    } else {
      const int row = k / 3, col = k % 3;
      bx0 = x0 + 40 + col * (kw + 14), bx1 = bx0 + kw, by0 = 344 + row * (kh + gap), by1 = by0 + kh;
    }
    const bool cur = g_padKey == k;
    const bool ok = k == kPadOk, fn = k == kPadClear || k == kPadBack;
    const bool canOk = typed > 0 && !over;
    if (cur) cur_under(P, bx0, by0, bx1, by1, 22, t);
    const uint32_t fill = ok ? (canOk ? 0x0E7A3A : 0x262040) : (fn ? 0x3A1430 : kPanelHi);
    P.K.box(bx0, by0, bx1, by1, 22, lit(cur) ? kCurFill : fill, 1.0f, ok ? (canOk ? 0x07522A : 0x1A1430) : 0x120C26);
    P.K.ring(bx0, by0, bx1, by1, 22, 2.5f, ok ? (canOk ? kLime : 0x3A3060) : (fn ? kPink : 0x5A4A80));
    if (ok) P.K.text("OK  -  SET STAKE", cx, 0.5f * (by0 + by1) + 2, 44, bx1 - bx0 - 30, canOk ? 0xFFFFFF : 0x6A6090, 3.0f);
    else P.K.text(kKey[k], 0.5f * (bx0 + bx1), 0.5f * (by0 + by1) + 2, fn ? 36 : 54, kw - 16, fn ? 0xFFB8D8 : kInk, 2.0f);
    if (cur) cur_over(P, bx0, by0, bx1, by1, 22, t);
    g_hits.push_back({bx0, by0, bx1, by1, 10, k, 0});
  }
  P.K.text("B: CLOSE THE PAD (THE STAKE STAYS)", cx, 868, 24, 500, kDim, 0.0f);
}

// r207: the slip's same-game partner of leg i (-1 none)
int pair_of(size_t i) {
  const SlipLeg& s = g_slip[i];
  if (is_future(s.p.market)) return -1;
  for (size_t j = 0; j < g_slip.size(); ++j)
    if (j != i && !is_future(g_slip[j].p.market) && g_slip[j].home == s.home && g_slip[j].away == s.away && g_slip[j].week == s.week)
      return static_cast<int>(j);
  return -1;
}

void draw_slip(Pen& P, float t, bool locked) {
  (void)locked;
  const float x0 = 1104, x1 = 1650, cx = 0.5f * (x0 + x1);
  const bool focus = g_focus == 1 && g_tab != kTabBets;
  const int n = static_cast<int>(g_slip.size());
  const bool slipLocked = slip_locked();
  P.K.box(x0, 132, x1, 888, 28, 0x120C26, 1.0f, 0x07040F);
  P.K.ring(x0, 132, x1, 888, 28, focus || g_padOpen ? 5.0f : 3.0f, focus || g_padOpen ? kPink : 0x4A2A80);
  bool anyFut = false;
  for (const SlipLeg& s : g_slip) anyFut = anyFut || is_future(s.p.market);
  neon_text(P, g_padOpen ? std::string("CUSTOM STAKE")
                         : (n > 1 ? "PARLAY  " + std::to_string(n) + " LEGS" : (n == 1 && anyFut ? std::string("FUTURE") : std::string("BET SLIP"))),
            cx, 176, 44, 480, kPink, 0, 0.45f);
  const long long bal = balance(), st = stake_coins(g_stake, bal);
  if (g_padOpen) {
    draw_pad(P, t, x0, x1, bal);
    return;
  }
  Bet sb;
  sb.legs = slip_to_legs(nullptr);
  const double boost = parlay_boost(sb.legs, false);
  if (n == 0) {
    P.K.text("PICK A PRICE", cx, 290, 48, 480, kInk, 2.0f);
    P.K.text("A: ADD IT TO THE SLIP", cx, 360, 32, 480, kDim, 0.0f);
    P.K.text("ADD MORE: A PARLAY", cx, 404, 32, 480, kDim, 0.0f);
    P.K.text("(UP TO 25 LEGS, ANY WEEK, FUTURES TOO)", cx, 448, 26, 500, kDim, 0.0f);
  } else {
    // the legs (4 shown, the list follows the ring; r207: up to 25 - it scrolls, a bar shows where)
    constexpr int kShown = kLegsShown;
    const int ring = focus && g_slipRow < n ? g_slipRow : -1;
    // r224: the window is state (g_legsTop: a leg added shows the newest, the ring keeps itself in view, the arrows page) -
    // r223 re-centred it on the ring, so the pointer on a leg scrolled the legs away under it
    const int first = std::max(0, std::min(std::max(0, n - kShown), g_legsTop));
    for (int i = 0; i < kShown && first + i < n; ++i) {
      const size_t li = static_cast<size_t>(first + i);
      const SlipLeg& s = g_slip[li];
      const float ly0 = 212 + i * 62.0f, ly1 = ly0 + 54, lcy = 0.5f * (ly0 + ly1) + 1;
      const bool on = ring == first + i;
      const int partner = pair_of(li);
      if (on) cur_under(P, x0 + 14, ly0, x1 - 30, ly1, 18, t);
      P.K.box(x0 + 14, ly0, x1 - 30, ly1, 18, lit(on) ? kCurFill : (partner >= 0 ? 0x2A1240 : 0x1A1234));
      P.K.ring(x0 + 14, ly0, x1 - 30, ly1, 18, 2.0f, partner >= 0 ? kPink : 0x3A2A66);
      float tx = x0 + 30;
      if (partner >= 0) {  // a same-game pick: its pink SG tag, the pair's joint price on its first row
        P.K.box(tx - 6, ly0 + 12, tx + 40, ly1 - 12, 10, kPink);
        P.K.text("SG", tx + 17, lcy, 22, 44, 0xFFFFFF, 0.0f);
        tx += 50;
      }
      if (!is_future(s.p.market) && g_dayValid && s.week > g_day.week) {  // a leg from an upcoming matchday: its week
        P.K.box(tx - 6, ly0 + 12, tx + 62, ly1 - 12, 10, 0x4A3A08);
        P.K.text("WK" + std::to_string(s.week), tx + 28, lcy, 22, 66, kGold, 0.0f);
        tx += 72;
      }
      std::string w = s.p.market == kWinBy ? "YOUR GAME: " + pick_words(s.p, s.home, s.away) : pick_words(s.p, s.home, s.away);
      std::string pr = price_text(s.p.american);
      if (partner >= 0) {
        int why = 0;
        const Fixture* f = fixture_for(s.week, s.home, s.away);
        const SlipLeg& o = g_slip[static_cast<size_t>(partner)];
        const SlipLeg& ml = s.p.market == kMoneyLine ? s : o;
        const SlipLeg& sp = s.p.market == kMoneyLine ? o : s;
        const Pick q = f != nullptr ? sgp_pick(*f, ml.p.side, sp.p.side, sp.p.spread, &why) : Pick();
        pr = static_cast<int>(li) < partner ? price_text(q.american) : "PAIR";
      }
      P.K.left(w, tx, lcy, 28, x1 - 30 - tx - 110, kInk, 0.0f);
      P.K.right(pr, x1 - 44, lcy, 30, 104, pr == "PAIR" ? kPink : (s.p.american > 0 ? kLime : kCyan), 0.0f);
      if (on) cur_over(P, x0 + 14, ly0, x1 - 30, ly1, 18, t);
      g_hits.push_back({x0 + 14, ly0, x1 - 30, ly1, 6, first + i, 0});
    }
    if (n > kShown) {
      P.K.text("LEGS " + std::to_string(first + 1) + "-" + std::to_string(std::min(n, first + kShown)) + " OF " + std::to_string(n) +
                   "  (UP / DOWN)",
               cx - 10, 466, 24, 300, kDim, 0.0f);
      list_arrows(P, x0 + 14, x0 + 104, x1 - 120, x1 - 30, 456, 480, 494, first > 0, first + kShown < n, 2);  // r224
      // the scroll bar
      const float by0 = 212, by1 = 204 + kShown * 62.0f;
      P.K.box(x1 - 22, by0, x1 - 14, by1, 4, 0x2A2050);
      const float h = (by1 - by0) * kShown / n, at = by0 + (by1 - by0 - h) * first / std::max(1, n - kShown);
      P.K.box(x1 - 22, at, x1 - 14, at + h, 4, kCyan);
    }
    // the multiplier climbing as legs go on
    const double target = slip_mult();
    const float ma = static_cast<float>(g_now - g_multAt);
    const double e = ma >= 0.0f && ma < 0.6f ? 1.0 - std::pow(1.0 - ma / 0.6, 3.0) : 1.0;
    const double shown = g_multFrom + (target - g_multFrom) * e;
    const bool single = sb.legs.size() == 1;
    P.K.left(single ? "PRICE" : (boost > 1.005 ? "MULTIPLIER (BOOSTED)" : "MULTIPLIER"), x0 + 30, 500, 28, 270, boost > 1.005 ? kGold : kDim, 0.0f);
    neon_text(P, single ? price_text(sb.legs[0].american) : mult_text(shown), x1 - 30, 500, 52, 260,
              single ? (sb.legs[0].american > 0 ? kLime : kCyan) : (ma >= 0.0f && ma < 0.6f ? 0xFFFFFF : kCyan), 1, 0.35f);
  }
  // the stake chips (25 50 100 250 MAX, r207: CUSTOM - the number pad)
  const bool stakeRow = focus && g_slipRow == n;
  P.K.left("STAKE", x0 + 30, 548, 30, 200, kDim, 0.0f);
  for (int k = 0; k < kStakeChoices; ++k) {
    const float w = 80, gap = 6.8f, bx0 = x0 + 16 + k * (w + gap), bx1 = bx0 + w, by0 = 568, by1 = 636;
    const bool on = g_stake == k;
    const bool can = k == kStakeMax ? bal > 0 : (k == kStakeCustom ? true : kStakes[k] <= bal);
    if (on && stakeRow) cur_under(P, bx0, by0, bx1, by1, 30, t);
    P.K.box(bx0, by0, bx1, by1, 30, on ? 0x5A4200 : (k == kStakeCustom ? 0x2A1240 : 0x1C1430));
    P.K.ring(bx0, by0, bx1, by1, 30, on ? 4.0f : 2.0f, on ? kGold : (k == kStakeCustom ? kPink : (can ? 0x5A4A80 : 0x2A2040)));
    std::string lab = k == kStakeMax ? std::string("MAX") : (k == kStakeCustom ? std::string("CUSTOM") : std::to_string(kStakes[k]));
    float px = k == kStakeCustom ? 20.0f : 34.0f;
    if (k == kStakeCustom && g_custom > 0) {  // its amount once typed (shrunk to fit)
      lab = xrcoin::fit(g_custom, 7);  // (r208: past 9,999,999 short on the chip: "1.5B"; the line under TO WIN has it in full)
      px = lab.size() <= 3 ? 34.0f : (lab.size() <= 5 ? 26.0f : 20.0f);
    }
    P.K.text(lab, 0.5f * (bx0 + bx1), 0.5f * (by0 + by1) + 1, px, w - 8, can ? (on ? kGold : kInk) : 0x5A5070, 0.0f);
    if (on && stakeRow) cur_over(P, bx0, by0, bx1, by1, 30, t);
    g_hits.push_back({bx0, by0, bx1, by1, 2, k, 0});
  }
  const double mult = n > 0 ? slip_mult() : 1.0;
  // r208 (Lance: "dont cap the ticket wins"): no house limit - TO WIN is the whole amount (64-bit). Big numbers: TO WIN in full
  // up to 999,999,999 (11 characters), past that short ("4.09T"); the line under it always the full PAYS figure (the stake's
  // words dropped when they wouldn't fit)
  const long long pays = n > 0 ? parlay_pays(st, mult) : 0;
  P.K.left("TO WIN", x0 + 30, 680, 28, 200, kDim, 0.0f);
  coin(P, x0 + 236.0f, 682, 22);
  neon_text(P, xrcoin::fit(n > 0 ? std::max(0LL, pays - st) : 0, 11), x1 - 30, 682, 52, 270, kGold, 1, 0.4f);
  std::string under;
  if (n > 0) {
    under = "PAYS " + coins_text(pays);
    const std::string back = "  (STAKE " + coins_text(st) + " BACK)";
    if (under.size() + back.size() <= 40) under += back;
  } else {
    under = g_stake == kStakeCustom && g_custom <= 0 ? std::string("CUSTOM: A ON IT TO TYPE YOUR STAKE") : std::string("STAKE ") + coins_text(st);
  }
  P.K.text(under, cx, 730, 26, 500, kDim, 0.0f);
  {  // PLACE BET
    const float bx0 = x0 + 40, bx1 = x1 - 40, by0 = 768, by1 = 864;
    const bool fut = n == 1 && is_future(g_slip[0].p.market);
    const bool can = n > 0 && !slipLocked && st > 0 && st <= bal;
    const bool on = focus && g_slipRow == n + 1;
    if (can) P.K.glow(bx0 - 30, by0 - 30, bx1 + 30, by1 + 30, kLime, (lit(on) ? 0.45f : 0.2f) + 0.2f * std::sin(t * 7.0f));
    if (on) cur_under(P, bx0, by0, bx1, by1, 46, t);
    P.K.box(bx0, by0, bx1, by1, 46, can ? 0x0E7A3A : 0x262040, 1.0f, can ? 0x07522A : 0x1A1430);
    P.K.ring(bx0, by0, bx1, by1, 46, 3.0f, can ? kLime : 0x3A3060);
    P.K.text(n > 0 && slipLocked ? "LOCKED" : (n > 1 ? "PLACE PARLAY" : (fut ? "PLACE FUTURE" : "PLACE BET")), cx, 0.5f * (by0 + by1) + 2, 52,
             bx1 - bx0 - 40, can ? 0xFFFFFF : 0x6A6090, 3.0f);
    if (on) cur_over(P, bx0, by0, bx1, by1, 46, t);
    g_hits.push_back({bx0, by0, bx1, by1, 3, 0, 0});
  }
}

// the machine when nobody is at it: its attract screen (WINNINGS READY / the day's best price / walk up)
void draw_attract(Pen& P, float t) {
  const Totals tt = totals();
  P.K.glow(kW * 0.5f - 700, 140, kW * 0.5f + 700, 860, tt.ready > 0 ? kGold : kPink, 0.25f + 0.1f * std::sin(t * 2.0f));
  if (tt.ready > 0) {
    neon_text(P, "WINNINGS READY", kW * 0.5f, 330, 120, 1500, kGold, 0, 0.5f + 0.2f * std::sin(t * 6.0f));
    coin(P, kW * 0.5f - 330, 520, 70);
    neon_text(P, coins_text(tt.ready), kW * 0.5f + 60, 520, 150, 800, kGold, 0, 0.5f);
    neon_text(P, "GRAB THE STICK + PRESS A / X TO COLLECT", kW * 0.5f, 720, 56, 1500, kCyan, 0, 0.35f);
  } else {
    neon_text(P, "BETS ARE OPEN", kW * 0.5f, 330, 120, 1500, kPink, 0, 0.5f + 0.2f * std::sin(t * 3.0f));
    std::string best;
    int bestA = 0;
    for (int fi : g_lines) {
      const Fixture& f = g_fx[static_cast<size_t>(fi)];
      for (int s = 0; s < 2; ++s)
        if (f.ml[s].american > bestA) {
          bestA = f.ml[s].american;
          best = place(s == 0 ? f.home : f.away);
        }
    }
    if (!best.empty()) neon_text(P, "TODAY'S LONG SHOT: " + best + " " + price_text(bestA), kW * 0.5f, 520, 64, 1500, kLime, 0, 0.35f);
    neon_text(P, "GRAB THE STICK TO PLAY", kW * 0.5f, 720, 56, 1500, kCyan, 0, 0.35f);
  }
  // r202: the machine's other games
  neon_text(P, "ALSO ON THIS MACHINE: STRIKERZ SLOTS + ROULETTE", kW * 0.5f, 820, 40, 1500, 0xB070FF, 0, 0.3f);
}
}  // namespace

void draw(std::vector<xrtext::BoardPiece>& out, float ox, float oy, float k, bool cabinet, float t, bool machineOn, bool attract) {
  const xrmenu::PlainScope plain(cabinet);  // r236: the MACHINE's screen keeps its own look whatever thread draws it
  if (cabinet && xrcasino::screen() != xrcasino::kBook) {  // r202: the machine's SLOTS / ROULETTE / its game picker
    xrcasino::draw(out, ox, oy, k, t, attract);
    return;
  }
  std::lock_guard<std::mutex> lk(g_m);
  g_hits.clear();
  Pen P;
  const bool locked = locked_now();
  const bool bcast = !cabinet && xrmenu::broadcast();  // r236: the hub page in LOOK B (the machine's screen: as it was)
  if (bcast) {
    const char* const kTabName[kTabs] = {"LEAGUE LINES", "WIN BY", "FUTURES", "MY BETS"};
    const Totals tt = totals();
    std::vector<std::string> names, badges;
    for (int i = 0; i < kTabs; ++i) {
      names.push_back(kTabName[i]);
      badges.push_back(i == kTabBets && tt.ready > 0 ? std::string("$") : std::string());
      g_hits.push_back({i * 226.0f, 136, i * 226.0f + 216, 188, 4, i, 0});  // (the machine's pointer boxes stay where its own screen has them)
    }
    xrmenu::subtabs(P.K, names, g_tab, 0, 14, badges, xrmenu::kBLeague);
    const bool live = g_dayValid && !locked;
    const std::string day = !g_dayValid ? std::string("NO GAMES TODAY")
                                        : (g_day.phase == PORT_FR_PHASE_PLAYOFFS ? std::string(g_day.roundName) : "WEEK " + std::to_string(g_day.week));
    xrmenu::label(P.K, day + "  -  " + (live ? "BETTING OPEN" : (g_dayValid ? "LOCKED: IN PLAY" : "CLOSED")), 20, 868, 32, live ? kLime : kRed, 'l', 900);  // (under the boards: the slip's panel is at the top right)
    const size_t from = P.v.size();
    if (g_book.career == 0) xrmenu::say(P.K, "LOAD A FRANCHISE CAREER", 825, 480, 64, xrmenu::kBDim, 'c', 1400);
    else if (g_tab == kTabLines) draw_lines(P, t, locked);
    else if (g_tab == kTabWinBy) draw_winby(P, t, locked);
    else if (g_tab == kTabFutures) draw_futures(P, t);
    else draw_bets(P, t, false);
    draw_slip(P, t, locked);
    for (size_t i = from; i < P.v.size(); ++i) {  // (the boards started at ~200 under the old header: up under the sub-tabs)
      xrtext::BoardPiece& p = P.v[i];
      p.y0 -= 110.0f;
      const bool text = p.kind == xrtext::BoardPiece::kText || p.kind == xrtext::BoardPiece::kTextLeft || p.kind == xrtext::BoardPiece::kTextRight;
      if (!text) p.y1 -= 110.0f;
      for (size_t q = 1; q < p.pts.size(); q += 2) p.pts[q] -= 110.0f;
    }
    const bool fresh = g_now - g_msgAt < 6.0;
    if (fresh) xrmenu::say(P.K, g_msg, 0.5f * kW, 930, 34, g_msgRgb, 'c', kW - 60);
    else if (tt.ready > 0) xrmenu::say(P.K, "WINNINGS READY: " + coins_text(tt.ready) + " BIT COINS", 0.5f * kW, 930, 34, kGold, 'c', kW - 60);
  } else {
  // the night behind it all + the prism's shards (the cards' refractor), a soft pink / cyan bloom at the corners
  P.K.box(0, 0, kW, kH, 30, kNight0, 1.0f, kNight1);
  prism(P, 0, 0, kW, 120, cabinet ? t : 0.0f);
  P.K.glow(-200, -200, 500, 400, kPink, 0.22f);
  P.K.glow(kW - 500, kH - 400, kW + 200, kH + 200, kCyan, 0.18f);
  // the header: STRIKERZ (chrome) SPORTSBOOK (pink neon), the day, your coins. r216 (Lance 11-11 REBRAND): STRIKERZ, not
  // TIZM (the cards' brand) - twice as long, so 70 px (r199: TIZM 92) and SPORTSBOOK moved right + 58 px (r199: 262, 64 px):
  // STRIKERZ 24 .. ~316, SPORTSBOOK 338 .. ~682, the day's box from 730
  chrome_text(P, "STRIKERZ", 24, 62, 70, 300, -1);
  const float flick = cabinet && std::fmod(t, 9.0f) > 8.75f ? (std::sin(t * 80.0f) > 0.0f ? 0.25f : 0.6f) : 0.55f;
  neon_text(P, "SPORTSBOOK", 338, 66, 58, 380, kPink, -1, flick);
  {
    std::string day = !g_dayValid ? std::string("NO GAMES TODAY")
                                  : (g_day.phase == PORT_FR_PHASE_PLAYOFFS ? std::string(g_day.roundName) : "WEEK " + std::to_string(g_day.week) + " BOARD");
    P.K.text(day, 960, 44, 40, 460, kInk, 2.0f);
    const bool live = g_dayValid && !locked;
    const float dot = live ? (0.6f + 0.4f * std::sin(t * 5.0f)) : 1.0f;
    P.K.box(800, 74, 1120, 112, 19, live ? 0x0E3A22 : 0x3A0E1A);
    P.K.box(818, 85, 834, 101, 8, live ? kLime : kRed, dot);
    P.K.text(live ? "BETTING OPEN" : (g_dayValid ? "LOCKED: IN PLAY" : "CLOSED"), 970, 94, 28, 280, live ? kLime : kRed, 0.0f);
  }
  // your coins: counting up through a collect
  {
    long long shownBal = balance();
    const float ca = static_cast<float>(g_now - g_collectAt);
    if (ca >= 0.0f && ca < 1.6f) shownBal -= static_cast<long long>(static_cast<double>(g_collectCoins) * (1.0 - std::min(1.0, ca / 1.4)));
    coin(P, 1290, 62, 30);
    neon_text(P, xrcoin::fit(shownBal, 15), 1630, 62, 60, 300, kGold, 1, ca >= 0.0f && ca < 1.6f ? 0.7f : 0.35f);  // (r208: in full to 999,999,999,999, then short: "1.24T")
    P.K.text("BIT COINS", 1500, 112, 26, 260, kDim, 0.0f);
  }
  if (cabinet && attract) {
    draw_attract(P, t);
  } else {
    // the boards' tabs
    const char* const kTabName[kTabs] = {"LEAGUE LINES", "WIN BY", "FUTURES", cabinet ? "SLIPS" : "MY BETS"};
    const Totals tt = totals();
    for (int i = 0; i < kTabs; ++i) {
      const float tx0 = i * 226.0f, tx1 = tx0 + 216;
      const bool on = g_tab == i;
      const bool cur = on && cabinet && g_focus == 2;  // r203: the machine's cursor on the TABS row
      if (on && !lit(cur)) P.K.glow(tx0 - 20, 120, tx1 + 20, 204, kPink, 0.35f);
      if (cur) cur_under(P, tx0, 136, tx1, 188, 26, t);
      P.K.box(tx0, 136, tx1, 188, 26, lit(cur) ? kCurFill : (on ? 0x4A1440 : 0x150E2C));
      P.K.ring(tx0, 136, tx1, 188, 26, on ? 4.0f : 2.0f, on ? kPink : kLine);
      P.K.text(kTabName[i], 0.5f * (tx0 + tx1), 163, 32, 196, on ? 0xFFFFFF : kDim, on ? 2.0f : 0.0f);
      if (cur) cur_over(P, tx0, 136, tx1, 188, 26, t);
      if (i == kTabBets && tt.ready > 0) {  // (winnings waiting: a gold count on the tab)
        P.K.box(tx1 - 36, 120, tx1 + 14, 158, 19, kGold);
        P.K.text("$", tx1 - 11, 140, 28, 40, 0x3A2A00, 0.0f);
      }
      g_hits.push_back({tx0, 136, tx1, 188, 4, i, 0});
    }
    if (cabinet) {  // r202: at the machine, B is its game picker (SPORTSBOOK / SLOTS / ROULETTE); r203: up off the top = the tabs
      P.K.left("PUSH UP: BOARDS", 908, 149, 24, 190, g_focus == 2 ? kCyan : kDim, 0.0f);
      xrmenu::glyph(P.K, "B", 916, 180, 30);
      P.K.left("CASINO GAMES", 954, 181, 24, 140, kPink, 0.0f);
      g_hits.push_back({908, 166, 1100, 198, 9, 0, 0});
    } else {
      xrmenu::glyph(P.K, "X", 920, 162, 44);
      P.K.left("NEXT BOARD", 976, 163, 30, 110, kDim, 0.0f);
    }
    if (g_book.career == 0) P.K.text("LOAD A FRANCHISE CAREER", 540, 520, 64, 1000, kDim, 2.0f);
    else if (g_tab == kTabLines) draw_lines(P, t, locked);
    else if (g_tab == kTabWinBy) draw_winby(P, t, locked);
    else if (g_tab == kTabFutures) draw_futures(P, t);
    else draw_bets(P, t, cabinet);
    draw_slip(P, t, locked);
  }
  // the foot: the last word (a bet placed, a win) or the ticker (the machine scrolls it), the machine's switch (hub)
  {
    const bool fresh = g_now - g_msgAt < 6.0;
    const Totals tt = totals();
    std::string tick = fresh ? g_msg
                             : (tt.ready > 0 ? "WINNINGS READY TO COLLECT: " + coins_text(tt.ready) + " BIT COINS  -  MY BETS > COLLECT"
                                             : (locked ? std::string("TICKETS SETTLE AFTER THE FINAL WHISTLE  -  COLLECT YOUR WINNINGS HERE")
                                                       : std::string("BETS LOCK AT KICKOFF  -  MONEY LINE + SPREAD ON EVERY GAME  -  WIN BY ON YOURS  -  CRAZY PARLAYS: 25 LEGS, ANY WEEK, FUTURES TOO  -  BIT COINS ONLY")));
    const uint32_t tickRgb = fresh ? g_msgRgb : (tt.ready > 0 ? kGold : kAmber);
    P.K.box(0, 904, kW, 958, 26, 0x0A0716);
    P.K.ring(0, 904, kW, 958, 26, 2, fresh || tt.ready > 0 ? tickRgb : 0x3A2E66);
    if (cabinet && !fresh) {
      const float tw = xrtext::board_text_width(tick, 32) + 200.0f;
      const float off = std::fmod(t * 140.0f, tw);
      for (int r = 0; r < 3; ++r) {
        const float x = kW - off + r * tw - tw;
        if (x > kW - 20 || x + tw < 0) continue;
        P.K.left(tick, x, 932, 32, 0.0f, tickRgb, 0.0f);  // (the screen's edge cuts it, an LED ticker's way)
      }
    } else {
      P.K.text(tick, 0.5f * kW - (cabinet ? 0.0f : 170.0f), 932, 32, kW - (cabinet ? 60.0f : 400.0f), tickRgb, 0.0f);
    }
    if (!cabinet) P.K.right(std::string("MACHINE IN MY ROOM: ") + (machineOn ? "ON" : "OFF") + "  (Y)", kW - 24, 932, 28, 330,
                            machineOn ? kLime : kDim, 0.0f);
  }
  }  // (r236: the old frame - the machine, and outside the hub)
  // a bet placed: the screen flashes
  const float fl = static_cast<float>(g_now - g_flashAt);
  if (fl >= 0.0f && fl < 0.6f) P.K.box(0, 0, kW, kH, 30, 0xFFFFFF, 0.55f * (1.0f - fl / 0.6f));
  // a collect: the hub's small one (a gold count-up over the page, a few sparkles), the machine's full screen JACKPOT
  const float ca = static_cast<float>(g_now - g_collectAt);
  if (ca >= 0.0f && ca < 2.6f && g_collectCoins > 0) {
    const float a = ca < 0.25f ? ca / 0.25f : std::max(0.0f, 1.0f - std::max(0.0f, ca - 1.8f) / 0.8f);
    const long long shown = static_cast<long long>(static_cast<double>(g_collectCoins) * std::min(1.0, ca / 1.4));
    if (g_collectMachine && cabinet) {
      P.K.box(0, 0, kW, kH, 30, 0x1A0E00, 0.75f * a);
      xrtext::BoardPiece b;
      b.kind = xrtext::BoardPiece::kBurst;
      b.x0 = 0.5f * kW - 820;
      b.y0 = 0.5f * kH - 520;
      b.x1 = 0.5f * kW + 820;
      b.y1 = 0.5f * kH + 520;
      b.rgb = kGold;
      b.alpha = 0.45f * a;
      b.points = 20;
      b.inner = 0.42f;
      b.angle = ca * 50.0f;
      P.v.push_back(b);
      const bool strobe = std::fmod(ca * 8.0f, 1.0f) < 0.5f;
      neon_text(P, "JACKPOT!", 0.5f * kW, 0.5f * kH - 170, 170, 1400, strobe ? kGold : kPink, 0, 0.7f * a);
      coin(P, 0.5f * kW - 380, 0.5f * kH + 60, 80);
      neon_text(P, "+" + coins_text(shown), 0.5f * kW + 70, 0.5f * kH + 60, 160, 900, kGold, 0, 0.6f * a);
      neon_text(P, "COINS IN THE TRAY  -  PAID INTO YOUR BINDER", 0.5f * kW, 0.5f * kH + 230, 46, 1400, kCyan, 0, 0.3f * a);
    } else {
      const float cy = 0.5f * kH + 40;
      P.K.box(240, cy - 130, kW - 240, cy + 130, 60, 0x241600, 0.92f * a);
      P.K.ring(240, cy - 130, kW - 240, cy + 130, 60, 6, kGold, a);
      for (int s = 0; s < 7; ++s) {  // (a few sparkles round it)
        const float ang = s * 0.9f + ca * 2.0f, rr = 150 + 30 * std::sin(ca * 6.0f + s);
        const float sx = 0.5f * kW + std::cos(ang) * (rr + 380), sy = cy + std::sin(ang) * rr * 0.6f;
        P.K.glow(sx - 40, sy - 40, sx + 40, sy + 40, s % 2 ? kGold : 0xFFFFFF, 0.6f * a);
      }
      coin(P, 0.5f * kW - 330, cy, 56);
      neon_text(P, "+" + coins_text(shown) + " COLLECTED", 0.5f * kW + 70, cy, 96, 1000, kGold, 0, 0.45f * a);
    }
  }
  // a winner settled while you watch the machine: a gold burst
  const float wn = static_cast<float>(g_now - g_winAt);
  if (cabinet && !(g_collectMachine && ca >= 0.0f && ca < 2.6f) && wn >= 0.0f && wn < 4.0f) {
    const float a = wn < 0.3f ? wn / 0.3f : std::max(0.0f, 1.0f - (wn - 0.3f) / 3.7f);
    xrtext::BoardPiece b;
    b.kind = xrtext::BoardPiece::kBurst;
    b.x0 = 0.5f * kW - 700;
    b.y0 = 0.5f * kH - 420;
    b.x1 = 0.5f * kW + 700;
    b.y1 = 0.5f * kH + 420;
    b.rgb = kGold;
    b.alpha = 0.35f * a;
    b.points = 18;
    b.inner = 0.45f;
    b.angle = wn * 40.0f;
    P.v.push_back(b);
    neon_text(P, "WINNER!", 0.5f * kW, 0.5f * kH - 40, 150, 1200, kGold, 0, 0.6f * a);
  }
  // r202: a hand on the machine's joystick: it has the controller (the cue)
  // (r203: the CONTROL BAR - what each control does here; the last word for 3 s after it's said)
  if (cabinet && !attract && xrcasino::engaged())
    xrcasino::control_bar(P.v, t, machine_hint_now(), g_now - g_msgAt < 3.0 ? g_msg : std::string(), g_msgRgb);
  // put the page where it goes
  for (xrtext::BoardPiece& p : P.v) {
    p.x0 = ox + p.x0 * k;
    p.y0 = oy + p.y0 * k;
    const bool text = p.kind == xrtext::BoardPiece::kText || p.kind == xrtext::BoardPiece::kTextLeft || p.kind == xrtext::BoardPiece::kTextRight;
    if (text) {
      p.x1 *= k;  // (room)
      p.y1 *= k;  // (px)
    } else {
      p.x1 = ox + p.x1 * k;
      p.y1 = oy + p.y1 * k;
    }
    p.radius *= k;
    p.outline *= k;
    p.shadow *= k;
    p.cut *= k;  // r236
    p.lean *= k;
    if (bcast && text && !p.styled && !p.art && p.y1 >= 14.0f && p.y1 < 30.0f * k) p.y1 = 30.0f * k;  // r236: nothing under 30 px in the hub (the machine: as it was)
    for (size_t i = 0; i < p.pts.size(); ++i) p.pts[i] = (i % 2 == 0 ? ox : oy) + p.pts[i] * k;
    out.push_back(std::move(p));
  }
  if (t_sink != nullptr && t_sink->on) {  // r205: the overlay's box, where the page went
    t_sink->x0 = ox + t_sink->x0 * k, t_sink->y0 = oy + t_sink->y0 * k;
    t_sink->x1 = ox + t_sink->x1 * k, t_sink->y1 = oy + t_sink->y1 * k;
    t_sink->r *= k;
    t_sink->k = k;
  }
}

// ---- the paper ticket (the machine's printer) ----
// r204 (Lance's r203 test: "impossible to read"): a bold layout - the pick + its price big and black on every leg, the payout
// biggest, the small print darker - drawn in 384 layout units and pictured at kTicketPx (2x r199's 384: crisp at 40 cm)
namespace {
int ticket_pitch(int legs) { return legs <= 10 ? 92 : 68; }  // r207: past 10 legs a little tighter (still 28 px bold)
}  // namespace
int ticket_height(int legs) {
  const int n = std::max(1, std::min(kMaxLegs, legs));
  return 510 + ticket_pitch(n) * n;
}

bool ticket(uint32_t id, std::vector<xrtext::BoardPiece>* out, int* w, int* h) {
  std::lock_guard<std::mutex> lk(g_m);
  const Bet* bp = nullptr;
  for (const Bet& b : g_book.bets)
    if (b.id == id) bp = &b;
  if (bp == nullptr) return false;
  const Bet& b = *bp;
  const int W = 384, H = ticket_height(static_cast<int>(b.legs.size()));
  // r207: a long parlay's picture stays under kTicketMaxPx tall (a little narrower: 25 legs ~710 px wide, r199's was 384)
  const float s = std::min(static_cast<float>(kTicketPx) / static_cast<float>(W), static_cast<float>(kTicketMaxPx) / static_cast<float>(H));
  *w = static_cast<int>(std::lround(W * s));
  *h = std::min(kTicketMaxPx, static_cast<int>(std::lround(H * s)));
  Pen P;
  const uint32_t ink = 0x101018, mid = 0x3C3C4C, pink = 0xC4125C;
  // thick card stock: warm white, a pink TIZM band at the head, a torn (zig-zag) foot
  P.K.box(0, 0, W, H - 14, 0, 0xFAF7EF, 1.0f, 0xF1ECDF);
  for (int i = 0; i < W / 16; ++i) P.K.poly({i * 16.0f, H - 15.0f, i * 16.0f + 16, H - 15.0f, i * 16.0f + 8, H - 2.0f}, 0xF1ECDF);
  P.K.box(0, 0, W, 104, 0, 0xFFD6E8, 1.0f, 0xFFEAF2);
  P.K.text("STRIKERZ", W * 0.5f, 40, 62, W - 40, pink, 1.6f);  // r216 (r199-r215: TIZM)
  P.K.text("SPORTSBOOK", W * 0.5f, 84, 30, W - 40, ink, 1.0f);
  P.K.text("TICKET #" + std::to_string(b.id), W * 0.5f, 132, 32, W - 40, ink, 1.2f);
  int wLo = b.week, wHi = b.week;  // r207: a parlay over several matchdays: "WEEKS 14-17"
  bool anyFut = false;
  for (const Leg& l : b.legs) {
    anyFut = anyFut || is_future(l.market);
    if (!is_future(l.market) && l.week >= 0) wLo = std::min(wLo, l.week), wHi = std::max(wHi, l.week);
  }
  const std::string day = (b.week > PORT_FR_WEEKS ? std::string("PLAYOFFS")
                                                  : (wHi > wLo ? "WEEKS " + std::to_string(wLo) + "-" + std::to_string(wHi) : "WEEK " + std::to_string(b.week))) +
                          "  -  SEASON " + std::to_string(b.season) + (anyFut && b.legs.size() == 1 ? "  -  BOUGHT" : "");
  P.K.text(day, W * 0.5f, 166, 24, W - 40, mid, 0.6f);
  for (int x = 16; x < W - 16; x += 14) P.K.box(x, 186, x + 8, 190, 0, mid);
  P.K.left(b.legs.size() > 1 ? std::to_string(b.legs.size()) + "-LEG PARLAY" : std::string(is_future(b.legs[0].market) ? "FUTURE" : "SINGLE"), 18, 222, 38,
           W - 36, pink, 1.4f);
  float y = 262;
  const int pitch = ticket_pitch(static_cast<int>(b.legs.size()));
  const bool tight = pitch < 92;  // r207: a long parlay's legs - 18 / 28 px (r204: 22 / 34)
  for (size_t i = 0; i < b.legs.size(); ++i) {
    const Leg& l = b.legs[i];
    std::string top = l.market == kWinBy ? std::string("YOUR GAME")
                                         : (is_future(l.market) ? "SEASON " + std::to_string(b.season) + " FUTURE" : place(l.away) + " @ " + place(l.home));
    if (l.market == kSgp) top += "  -  SAME GAME";
    if (!is_future(l.market) && l.week >= 0 && l.week != b.week) top += "  -  WEEK " + std::to_string(l.week);
    const uint32_t lc = l.state == 1 ? 0x0B6A30 : (l.state == 2 ? 0xA8101E : ink);
    P.K.left(top, 18, y + (tight ? 2.0f : 4.0f), tight ? 18.0f : 22.0f, W - 36, mid, 0.5f);
    P.K.left(leg_words(l), 18, y + (tight ? 30.0f : 40.0f), tight ? 28.0f : 34.0f, W - (l.state != 0 ? 100 : 36), lc, 1.3f);
    if (l.state != 0)
      P.K.right(l.state == 1 ? "WON" : (l.state == 2 ? "LOST" : "VOID"), W - 18, y + (tight ? 30.0f : 40.0f), tight ? 24.0f : 28.0f, 76, lc, 1.2f);
    if (i + 1 < b.legs.size()) P.K.box(18, y + pitch - 22, W - 18, y + pitch - 20, 0, 0xD8D2C4);
    y += pitch;
  }
  for (int x = 16; x < W - 16; x += 14) P.K.box(x, y - 14, x + 8, y - 10, 0, mid);
  P.K.left("STAKE", 18, y + 22, 26, 160, mid, 0.6f);
  // r208: a long amount in full without the unit (it stays big enough to read). r216: the unit is the BIT COINS' (r199-r215
  // " TIZM"): " BIT COINS" when it fits the room at its size, else " COINS", else the amount alone
  auto tizm = [](long long c, float px, float room) {
    const std::string s = coins_text(c);
    if (xrtext::board_text_width(s + " BIT COINS", px) <= room) return s + " BIT COINS";
    if (xrtext::board_text_width(s + " COINS", px) <= room) return s + " COINS";
    return s;
  };
  P.K.right(tizm(b.stake, 32, 240), W - 18, y + 22, 32, 240, ink, 1.2f);
  if (b.legs.size() > 1) {
    P.K.left("MULTIPLIER", 18, y + 62, 26, 180, mid, 0.6f);
    P.K.right(mult_text(multiplier(b, false)), W - 18, y + 62, 32, 200, ink, 1.2f);
  }
  const bool settled = b.state != 0;
  P.K.left(settled ? (b.state == 2 ? "PAID" : "PAYS OUT") : "TO PAY", 18, y + 110, 30, 170, ink, 1.2f);
  P.K.right(tizm(settled ? b.paid : b.pays, 44, 200), W - 18, y + 110, 44, 250, ink, 1.8f);  // (r216: the unit only if
                                                                                              // clear of PAYS OUT on its left)
  // a barcode
  uint32_t r = b.id * 2654435761u;
  for (float x = 40; x < W - 40;) {
    r = r * 1664525u + 1013904223u;
    const float bw = 2.0f + static_cast<float>((r >> 28) & 3);
    P.K.box(x, y + 146, x + bw, y + 196, 0, ink);
    x += bw + 2.0f + static_cast<float>((r >> 24) & 3);
  }
  P.K.text("BIT COINS ONLY - NO CASH VALUE", W * 0.5f, y + 222, 20, W - 40, mid, 0.4f);
  // the stamp: WINNER (green) / LOSER (red) across it once settled; VOID grey
  if (settled) {
    const uint32_t sc = b.state == 1 ? 0x12A04A : (b.state == 2 ? 0xD8182A : 0x7A7A86);
    const char* word = b.state == 1 ? "WINNER" : (b.state == 2 ? "LOSER" : "VOID");
    const float cy = 0.5f * H + 20;
    xrtext::BoardPiece ring;
    ring.kind = xrtext::BoardPiece::kText;
    ring.x0 = W * 0.5f;
    ring.y0 = cy;
    ring.x1 = W - 30;
    ring.y1 = 92;
    ring.rgb = sc;
    ring.alpha = 0.82f;
    ring.outline = 5.0f;
    ring.outlineRgb = xrmenu::mix(sc, 0xFFFFFF, 0.25f);
    ring.angle = -18.0f;
    ring.text = word;
    P.K.box(30, cy - 70, W - 30, cy + 70, 16, sc, 0.10f);
    P.v.push_back(ring);
  }
  // pictured at kTicketPx: every piece x s
  for (xrtext::BoardPiece& p : P.v) {
    p.x0 *= s;
    p.y0 *= s;
    p.x1 *= s;
    p.y1 *= s;
    p.radius *= s;
    p.outline *= s;
    p.shadow *= s;
    for (float& q : p.pts) q *= s;
  }
  *out = std::move(P.v);
  return true;
}

}  // namespace xrbook
