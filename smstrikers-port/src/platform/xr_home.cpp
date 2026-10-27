// See xr_home.h.

#include "xr_home.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <mutex>
#include <random>
#include <sys/stat.h>

#include "../../include/port/xr_net.h"
#include "../../include/port/xr_quest.h"
#include "../../include/port/franchise.h"  // r76: a co-op franchise match is no rival match
#include "../../include/port/xr_guest.h"   // r87: the imported captains

namespace xrhome {
namespace {

#define HLOG(...)                          \
  do {                                     \
    std::fprintf(stderr, "[home] ");       \
    std::fprintf(stderr, __VA_ARGS__);     \
    std::fprintf(stderr, "\n");            \
  } while (0)

struct Online {
  std::string name;
  Record record;
};

// r87: an imported captain's games, by its pack (the class it played as kept for its colours' fallback).
struct GuestUses {
  int captain = -1;
  int uses = 0;
};
struct GuestRecord {
  int captain = -1;
  Record record;
};

struct State {
  bool loaded = false;
  std::string id, name;  // this headset (the panel's player id and team name)
  int pick = -1;         // the panel's home captain (-1 = the one you play most)
  std::string pickGuest;  // r90: ... an imported one (its pack; pick = the class it plays as)
  int64_t pickMtime = 0;
  int uses[12] = {};     // r87: the Strikers captains' own games (an imported one's go in guestUses)
  std::map<std::string, GuestUses> guestUses;
  Match last, ticket;
  Record cpu[12];        // r87: against the Strikers captains themselves (imported ones: guestCpu)
  std::map<std::string, GuestRecord> guestCpu;
  std::map<std::string, Online> online;  // by the friend's player id
  int comebacks = 0, bestComeback = 0, superStrikes = 0, matches = 0;
  int generation = 1;
  // The match being played.
  int human = -1;
  int score[2] = {0, 0};
  int worstDeficit = 0;
  int supers = 0;
  bool inMatch = false;
  std::string peerId, peerName;
  Cup cup;
};
State s;
std::mutex g_mutex;

std::string home_dir() {
  const char* home = std::getenv("STRIKERS_HOME");
  return home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers";
}
std::string path() { return home_dir() + "/xr_home.txt"; }
std::string pick_path() { return home_dir() + "/home-captain.txt"; }

bool is_captain(int c) {
  // The eight that captain a team: Daisy, Donkey Kong, Luigi, Mario, Peach, Waluigi, Wario, Yoshi.
  return c == 1 || c == 2 || c == 5 || c == 6 || c == 7 || c == 9 || c == 10 || c == 11;
}

// Names are saved last on a line, spaces kept; nothing else may hold a newline.
std::string clean(const std::string& n) {
  std::string out;
  for (char c : n) {
    if (c == '\n' || c == '\r' || c == '\t') continue;
    out.push_back(c);
    if (out.size() >= 24) break;
  }
  return out;
}

// r87: a guest pack as saved: one word ("-" none).
std::string pack_word(const std::string& p) {
  std::string out;
  for (char c : p) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') out.push_back(c);
    if (out.size() >= 40) break;
  }
  return out.empty() ? "-" : out;
}
std::string pack_from_word(const char* w) { return std::strcmp(w, "-") == 0 ? std::string() : std::string(w); }

void write_match(FILE* f, const char* key, const Match& m) {
  std::fprintf(f, "%s %d %d %d %d %d %d %d %d %lld %s\n", key, m.myCaptain, m.theirCaptain, m.mine, m.theirs,
               m.overtime ? 1 : 0, m.online ? 1 : 0, m.cup ? 1 : 0, m.forfeit ? 1 : 0, static_cast<long long>(m.when),
               clean(m.theirName).c_str());
  // r87: its imported captains on a line of their own (older files have none: the Strikers captains)
  if (!m.myGuest.empty() || !m.theirGuest.empty())
    std::fprintf(f, "%sguests %s %s\n", key, pack_word(m.myGuest).c_str(), pack_word(m.theirGuest).c_str());
}

void read_guests(const char* rest, Match* m) {
  char a[48] = {}, b[48] = {};
  if (std::sscanf(rest, "%47s %47s", a, b) != 2) return;
  m->myGuest = pack_from_word(a);
  m->theirGuest = pack_from_word(b);
}

bool read_match(const char* rest, Match* m) {
  int ot = 0, on = 0, cup = 0, ff = 0, used = 0;
  long long when = 0;
  if (std::sscanf(rest, "%d %d %d %d %d %d %d %d %lld %n", &m->myCaptain, &m->theirCaptain, &m->mine, &m->theirs, &ot,
                  &on, &cup, &ff, &when, &used) < 9) {
    return false;
  }
  m->overtime = ot != 0;
  m->online = on != 0;
  m->cup = cup != 0;
  m->forfeit = ff != 0;
  m->when = when;
  m->theirName = used > 0 ? std::string(rest + used) : std::string();
  while (!m->theirName.empty() && (m->theirName.back() == '\n' || m->theirName.back() == '\r')) m->theirName.pop_back();
  m->valid = true;
  return true;
}

void save() {
  const std::string tmp = path() + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) return;
  std::fprintf(f, "id %s\n", s.id.c_str());
  for (int c = 0; c < 12; ++c) {
    if (s.uses[c] > 0) std::fprintf(f, "use %d %d\n", c, s.uses[c]);
  }
  for (int c = 0; c < 12; ++c) {
    const Record& r = s.cpu[c];
    if (r.games() > 0) std::fprintf(f, "cpu %d %d %d %d %d\n", c, r.wins, r.losses, r.draws, r.lastLost ? 1 : 0);
  }
  // r87: the imported captains' games and records
  for (const auto& [pack, u] : s.guestUses) {
    if (u.uses > 0) std::fprintf(f, "guse %s %d %d\n", pack_word(pack).c_str(), u.captain, u.uses);
  }
  for (const auto& [pack, g] : s.guestCpu) {
    const Record& r = g.record;
    if (r.games() > 0)
      std::fprintf(f, "gcpu %s %d %d %d %d %d\n", pack_word(pack).c_str(), g.captain, r.wins, r.losses, r.draws,
                   r.lastLost ? 1 : 0);
  }
  for (const auto& [id, o] : s.online) {
    std::fprintf(f, "online %s %d %d %d %d %s\n", id.c_str(), o.record.wins, o.record.losses, o.record.draws,
                 o.record.lastLost ? 1 : 0, clean(o.name).c_str());
  }
  if (s.last.valid) write_match(f, "last", s.last);
  if (s.ticket.valid) write_match(f, "ticket", s.ticket);
  std::fprintf(f, "honors %d %d %d %d\n", s.comebacks, s.bestComeback, s.superStrikes, s.matches);
  std::fclose(f);
  std::rename(tmp.c_str(), path().c_str());
}

void load() {
  FILE* f = std::fopen(path().c_str(), "r");
  if (f == nullptr) return;
  char line[256];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    char key[16] = {};
    int used = 0;
    if (std::sscanf(line, "%15s %n", key, &used) < 1) continue;
    const char* rest = line + used;
    if (std::strcmp(key, "id") == 0) {
      char id[40] = {};
      if (std::sscanf(rest, "%39s", id) == 1) s.id = id;
    } else if (std::strcmp(key, "use") == 0) {
      int c = -1, n = 0;
      if (std::sscanf(rest, "%d %d", &c, &n) == 2 && c >= 0 && c < 12) s.uses[c] = n;
    } else if (std::strcmp(key, "cpu") == 0) {
      int c = -1, w = 0, l = 0, d = 0, ll = 0;
      if (std::sscanf(rest, "%d %d %d %d %d", &c, &w, &l, &d, &ll) == 5 && c >= 0 && c < 12) {
        s.cpu[c] = Record{w, l, d, ll != 0};
      }
    } else if (std::strcmp(key, "online") == 0) {
      char id[40] = {};
      int w = 0, l = 0, d = 0, ll = 0, n = 0;
      if (std::sscanf(rest, "%39s %d %d %d %d %n", id, &w, &l, &d, &ll, &n) >= 5) {
        Online& o = s.online[id];
        o.record = Record{w, l, d, ll != 0};
        o.name = n > 0 ? std::string(rest + n) : std::string();
        while (!o.name.empty() && (o.name.back() == '\n' || o.name.back() == '\r')) o.name.pop_back();
      }
    } else if (std::strcmp(key, "guse") == 0) {
      char pack[48] = {};
      int c = -1, n = 0;
      if (std::sscanf(rest, "%47s %d %d", pack, &c, &n) == 3 && std::strcmp(pack, "-") != 0) s.guestUses[pack] = {c, n};
    } else if (std::strcmp(key, "gcpu") == 0) {
      char pack[48] = {};
      int c = -1, w = 0, l = 0, d = 0, ll = 0;
      if (std::sscanf(rest, "%47s %d %d %d %d %d", pack, &c, &w, &l, &d, &ll) == 6 && std::strcmp(pack, "-") != 0) {
        s.guestCpu[pack] = {c, Record{w, l, d, ll != 0}};
      }
    } else if (std::strcmp(key, "last") == 0) {
      read_match(rest, &s.last);
    } else if (std::strcmp(key, "ticket") == 0) {
      read_match(rest, &s.ticket);
    } else if (std::strcmp(key, "lastguests") == 0) {
      read_guests(rest, &s.last);
    } else if (std::strcmp(key, "ticketguests") == 0) {
      read_guests(rest, &s.ticket);
    } else if (std::strcmp(key, "honors") == 0) {
      std::sscanf(rest, "%d %d %d %d", &s.comebacks, &s.bestComeback, &s.superStrikes, &s.matches);
    }
  }
  std::fclose(f);
}

// The panel's home captain pick ("-1" or nothing = automatic).
void read_pick() {
  struct stat st {};
  if (stat(pick_path().c_str(), &st) != 0) {
    if (s.pick != -1) {
      s.pick = -1;
      s.pickGuest.clear();
      ++s.generation;
    }
    return;
  }
  if (static_cast<int64_t>(st.st_mtime) == s.pickMtime) return;
  s.pickMtime = st.st_mtime;
  // a captain's class number, or (r90, Lance: "its still only available for the base players") an imported
  // captain's pack, standing in for the Strikers captain it plays as
  char word[64] = {};
  if (FILE* f = std::fopen(pick_path().c_str(), "r")) {
    if (std::fscanf(f, "%63s", word) != 1) word[0] = '\0';
    std::fclose(f);
  }
  int c = -1;
  std::string guest;
  char* end = nullptr;
  const long n = std::strtol(word, &end, 10);
  if (word[0] != '\0' && end != nullptr && *end == '\0') {
    c = static_cast<int>(n);
  } else if (word[0] != '\0') {
    static const int kTeamCaptain[8] = {1, 2, 5, 6, 7, 9, 10, 11};  // eTeamID -> eCharacterClass
    const int idx = PortGuestRosterFind(PORT_GUEST_CAPTAIN, word);
    const int team = idx >= 0 ? PortGuestRosterBase(idx) : -1;
    if (team >= 0 && team < 8) {
      c = kTeamCaptain[team];
      guest = word;
    }
  }
  if (!is_captain(c)) {
    c = -1;
    guest.clear();
  }
  if (c != s.pick || guest != s.pickGuest) {
    s.pick = c;
    s.pickGuest = guest;
    ++s.generation;
    HLOG("home captain: %s", c < 0 ? "the one you play most" : (guest.empty() ? captain_name(c) : guest.c_str()));
  }
}

// r87: an imported captain you play most is your home captain itself (`guest`: its pack, else "").
int home_captain_locked(std::string* guest = nullptr) {
  if (guest != nullptr) guest->clear();
  if (is_captain(s.pick)) {
    if (guest != nullptr) *guest = s.pickGuest;  // r90: the panel's pick may be an imported captain
    return s.pick;
  }
  int best = -1, bestUses = 0;
  for (int c = 0; c < 12; ++c) {
    if (is_captain(c) && s.uses[c] > bestUses) {
      best = c;
      bestUses = s.uses[c];
    }
  }
  for (const auto& [pack, u] : s.guestUses) {
    if (u.uses > bestUses && is_captain(u.captain)) {
      best = u.captain;
      bestUses = u.uses;
      if (guest != nullptr) *guest = pack;
    }
  }
  return best >= 0 ? best : 6;  // Mario until you have played
}

void ensure_loaded() {
  if (s.loaded) return;
  s.loaded = true;
  load();
  // The panel's ids win (StrikersHome.prepare), so a reinstall keeps the friend's record of you.
  if (const char* id = std::getenv("STRIKERS_PLAYER_ID"); id != nullptr && std::strlen(id) >= 8) s.id = id;
  if (s.id.size() < 8) {
    std::random_device rd;
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%08x%08x", rd(), rd());
    s.id = buf;
  }
  if (const char* n = std::getenv("STRIKERS_TEAM_NAME"); n != nullptr) s.name = clean(n);
  read_pick();
  HLOG("loaded: home %s, %d matches, last %s, %zu online friends", captain_name(home_captain_locked()), s.matches,
       s.last.valid ? "yes" : "none", s.online.size());
}

}  // namespace

const char* captain_name(int c) {
  static const char* const kNames[12] = {"Birdo", "Daisy", "Donkey Kong", "Hammer Bro", "Koopa", "Luigi",
                                         "Mario", "Peach", "Toad", "Waluigi", "Wario", "Yoshi"};
  return c >= 0 && c < 12 ? kNames[c] : "?";
}

void init() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensure_loaded();
}

void poll() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensure_loaded();
  read_pick();
}

View view() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensure_loaded();
  View v;
  v.generation = s.generation;
  v.captain = home_captain_locked(&v.captainGuest);
  v.last = s.last;
  v.ticket = s.ticket;
  v.comebacks = s.comebacks;
  v.bestComeback = s.bestComeback;
  v.superStrikes = s.superStrikes;
  v.matches = s.matches;
  // The CPU rival: the CPU captain you have played most (twice at least), more losses breaking a tie.
  int best = -1;
  for (int c = 0; c < 12; ++c) {
    const Record& r = s.cpu[c];
    if (r.games() < 2) continue;
    if (best < 0 || r.games() > s.cpu[best].games() ||
        (r.games() == s.cpu[best].games() && r.losses > s.cpu[best].losses)) {
      best = c;
    }
  }
  if (best >= 0) {
    v.cpu.valid = true;
    v.cpu.captain = best;
    v.cpu.record = s.cpu[best];
  }
  // r87: an imported CPU captain is a rival of its own
  for (const auto& [pack, gr] : s.guestCpu) {
    const Record& r = gr.record;
    if (r.games() < 2) continue;
    if (!v.cpu.valid || r.games() > v.cpu.record.games() ||
        (r.games() == v.cpu.record.games() && r.losses > v.cpu.record.losses)) {
      v.cpu.valid = true;
      v.cpu.captain = gr.captain;
      v.cpu.guest = pack;
      v.cpu.record = r;
    }
  }
  // The online rival: the friend you have played most.
  const Online* top = nullptr;
  for (const auto& [id, o] : s.online) {
    if (top == nullptr || o.record.games() > top->record.games()) top = &o;
  }
  if (top != nullptr && top->record.games() > 0) {
    v.online.valid = true;
    v.online.name = top->name;
    v.online.record = top->record;
  }
  return v;
}

Cup cup() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return s.cup;
}

const std::string& my_id() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensure_loaded();
  return s.id;
}

const std::string& my_name() {
  std::lock_guard<std::mutex> lock(g_mutex);
  ensure_loaded();
  return s.name;
}

void set_peer(const std::string& id, const std::string& name) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (id != s.peerId || name != s.peerName) {
    s.peerId = id;
    s.peerName = clean(name);
    if (!id.empty()) HLOG("online friend: %s (%s)", s.peerName.empty() ? "no name" : s.peerName.c_str(), id.c_str());
  }
}

}  // namespace xrhome

using xrhome::s;
using xrhome::g_mutex;

extern "C" void PortHomeMatchBegin(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  xrhome::ensure_loaded();
  s.inMatch = true;
  s.human = -1;
  s.score[0] = s.score[1] = 0;
  s.worstDeficit = 0;
  s.supers = 0;
}

extern "C" void PortHomeHumanSide(int side) {
  if (side != 0 && side != 1) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  s.human = side;
}

extern "C" void PortHomeGoal(int side, int superStrike) {
  if (side != 0 && side != 1) return;
  std::lock_guard<std::mutex> lock(g_mutex);
  // A Super Strike counts two (the game's own scoring).
  s.score[side] += superStrike ? 2 : 1;
  if (s.human >= 0) {
    s.worstDeficit = std::max(s.worstDeficit, s.score[1 - s.human] - s.score[s.human]);
    if (superStrike && side == s.human) ++s.supers;
  }
}

extern "C" void PortHomeMatchEnd(int scoreLeft, int scoreRight, int overtime, int forfeitSide, int cup, int captainLeft,
                                 int captainRight) {
  std::lock_guard<std::mutex> lock(g_mutex);
  xrhome::ensure_loaded();
  if (!s.inMatch) return;
  s.inMatch = false;
  const int h = s.human;
  if (h != 0 && h != 1) {
    HLOG("match over %d-%d, no side of yours seen: not counted", scoreLeft, scoreRight);
    return;
  }
  const int score[2] = {scoreLeft, scoreRight};
  const int caps[2] = {captainLeft, captainRight};
  xrhome::Match m;
  m.valid = true;
  m.myCaptain = caps[h];
  m.theirCaptain = caps[1 - h];
  // r87: who really captained each side (an imported character in that class's place)
  for (int side = 0; side < 2; ++side) {
    const int gi = PortGuestCaptainFor(side);
    const char* pack = gi >= 0 ? PortGuestRosterPack(gi) : nullptr;
    (side == h ? m.myGuest : m.theirGuest) = pack != nullptr ? std::string(pack) : std::string();
  }
  m.mine = std::max(0, score[h]);
  m.theirs = std::max(0, score[1 - h]);
  m.overtime = overtime != 0;
  m.cup = cup != 0;
  m.forfeit = forfeitSide == 0 || forfeitSide == 1;
  // r63: an online cup match is co-op (the friend on your team, the computer against you); r76: so is
  // an online franchise match (the co-op career)
  m.online = !s.peerId.empty() && PortNetOnline() != 0 && cup == 0 && !PortFranchiseCoopActive();
  m.theirName = m.online ? s.peerName : std::string();
  m.when = static_cast<int64_t>(std::time(nullptr));
  const bool won = forfeitSide == 1 - h || (forfeitSide != h && score[h] > score[1 - h]);
  const bool lost = forfeitSide == h || (forfeitSide != 1 - h && score[h] < score[1 - h]);
  if (!m.myGuest.empty()) {
    xrhome::GuestUses& u = s.guestUses[m.myGuest];
    u.captain = m.myCaptain;
    ++u.uses;
  } else if (m.myCaptain >= 0 && m.myCaptain < 12) {
    ++s.uses[m.myCaptain];
  }
  ++s.matches;
  xrhome::Record* rec = nullptr;
  if (m.online) {
    xrhome::Online& o = s.online[s.peerId];
    if (!s.peerName.empty()) o.name = s.peerName;
    rec = &o.record;
  } else if (!m.theirGuest.empty()) {
    xrhome::GuestRecord& gr = s.guestCpu[m.theirGuest];
    gr.captain = m.theirCaptain;
    rec = &gr.record;
  } else if (m.theirCaptain >= 0 && m.theirCaptain < 12) {
    rec = &s.cpu[m.theirCaptain];
  }
  if (rec != nullptr) {
    if (won) ++rec->wins;
    else if (lost) ++rec->losses;
    else ++rec->draws;
    rec->lastLost = lost;
  }
  if (won && !s.ticket.valid) s.ticket = m;  // the inaugural win's ticket
  if (won && s.worstDeficit >= 2) {
    ++s.comebacks;
    s.bestComeback = std::max(s.bestComeback, s.worstDeficit);
  }
  s.superStrikes += s.supers;
  s.last = m;
  ++s.generation;
  xrhome::save();
  HLOG("match over: %s %d-%d %s%s%s (%s, came back from %d down, %d Super Strike goals)",
       m.myGuest.empty() ? xrhome::captain_name(m.myCaptain) : m.myGuest.c_str(), m.mine, m.theirs,
       m.online ? (m.theirName.empty() ? "online friend" : m.theirName.c_str())
                : (m.theirGuest.empty() ? xrhome::captain_name(m.theirCaptain) : m.theirGuest.c_str()),
       m.overtime ? " in overtime" : "", m.forfeit ? " (forfeit)" : "", won ? "win" : (lost ? "loss" : "draw"),
       s.worstDeficit, s.supers);
}

extern "C" void PortHomeCupState(int inCupHub, int trophy, int round, int rounds, int teams, const int* captains,
                                 const int* wins, const int* losses, const int* points, int userTeamIndex,
                                 int nextOpponent) {
  std::lock_guard<std::mutex> lock(g_mutex);
  xrhome::Cup c;
  c.inHub = inCupHub != 0;
  c.trophy = trophy;
  c.round = round;
  c.rounds = rounds;
  c.teams = std::clamp(teams, 0, 8);
  for (int i = 0; i < c.teams; ++i) {
    c.captains[i] = captains != nullptr ? captains[i] : -1;
    c.wins[i] = wins != nullptr ? wins[i] : 0;
    c.losses[i] = losses != nullptr ? losses[i] : 0;
    c.points[i] = points != nullptr ? points[i] : 0;
  }
  c.user = userTeamIndex;
  c.nextOpponent = nextOpponent;
  const xrhome::Cup& o = s.cup;
  bool same = o.inHub == c.inHub && o.trophy == c.trophy && o.round == c.round && o.rounds == c.rounds &&
              o.teams == c.teams && o.user == c.user && o.nextOpponent == c.nextOpponent;
  for (int i = 0; same && i < c.teams; ++i) {
    same = o.captains[i] == c.captains[i] && o.wins[i] == c.wins[i] && o.losses[i] == c.losses[i] &&
           o.points[i] == c.points[i];
  }
  c.generation = same ? o.generation : o.generation + 1;
  if (!same && c.inHub != o.inHub) {
    HLOG("cup suite: %s (cup %d, round %d of %d)", c.inHub ? "in a cup" : "out of the cup", c.trophy, c.round, c.rounds);
  }
  s.cup = c;
}
