// strikers-quest r61: the home ground (Lance + GPT, 2026-10-20: "give people their own Strikers home
// ground that grows with them"). What it remembers between sessions (xr_home.txt beside the game's
// log): your home captain (the panel's pick, else the one you play most), your last match, your
// record against every CPU captain and every online friend (your CPU rival and online rival are the
// ones you have played most), and what you have earned: the first win's ticket, comebacks, Super
// Strike goals. Game.cpp and StatsTracker.cpp report the matches (port/xr_quest.h PortHome*);
// xr_quest.cpp shows it all (the home corner by the shelf, the Jumbotron banners, the Tabletop
// flags, the cup suite).
#pragma once

#include <cstdint>
#include <string>

namespace xrhome {

struct Record {
  int wins = 0, losses = 0, draws = 0;
  bool lastLost = false;  // the last game against them was a loss: they gloat until you win one
  int games() const { return wins + losses + draws; }
};

struct Match {
  bool valid = false;
  int myCaptain = -1, theirCaptain = -1;  // eCharacterClass
  // r87 (his r86 clip: the board read "WARIO 5 - 1 PEACH" for Geodude's win): the imported character
  // (guest pack, "pk_geodude") captaining in that class's place, "" = the Strikers captain itself
  std::string myGuest, theirGuest;
  int mine = 0, theirs = 0;
  bool overtime = false, online = false, cup = false, forfeit = false;
  std::string theirName;  // online: the friend's team name
  int64_t when = 0;       // seconds since 1970
};

struct Rival {
  bool valid = false;
  int captain = -1;   // CPU rival: their captain
  std::string guest;  // r87: ... an imported one ("" = the Strikers captain)
  std::string name;   // online rival: their name ("" = unnamed)
  Record record;
};

// What the displays need, with a number that changes whenever any of it does.
struct View {
  int generation = 0;
  int captain = 6;  // the home captain (Mario until you have played)
  std::string captainGuest;  // r87: the home captain is this imported one ("" none)
  Match last;
  Rival cpu, online;
  Match ticket;  // the first win (valid once you have one)
  int comebacks = 0, bestComeback = 0, superStrikes = 0, matches = 0;
};

// The cup being played (PortHomeCupState), for the cup suite.
struct Cup {
  bool inHub = false;  // in a cup or tournament, between its games
  int trophy = -1;     // eTrophyType being played for
  int round = 0, rounds = 0, teams = 0;
  int captains[8] = {}, wins[8] = {}, losses[8] = {}, points[8] = {};
  int user = -1;          // your line in the table
  int nextOpponent = -1;  // the captain you play next (-1 unknown)
  int generation = 0;     // changes when anything above does
};
Cup cup();

// Once, at startup (reads xr_home.txt and the panel's pick, home-captain.txt).
void init();
// Twice a second from the game thread: picks up a new pick from the panel.
void poll();
View view();

// This headset's identity for the online rival (the panel's player id and team name).
const std::string& my_id();
const std::string& my_name();
// xr_net.cpp: the friend of the online match now running ("" while none).
void set_peer(const std::string& id, const std::string& name);

const char* captain_name(int captain);

}  // namespace xrhome
