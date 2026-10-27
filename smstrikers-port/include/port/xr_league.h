// strikers-quest r100 (Lance: "make it so 8 people can play on an online franchise ... UP TO 8 player online ...
// leagues"): the online franchise league's link to the room server's League object (server/src/index.js,
// wss://.../league/<CODE>, JSON text), on a thread of its own (src/platform/xr_league.cpp). The franchise
// (src/platform/franchise.cpp) runs the league; this only carries what the league's headsets share: who is in
// it (each headset's id and name - the lobby's STRIKERS_PLAYER_ID / STRIKERS_TEAM_NAME - and club), the shared
// career text with its version, and who is ready to play whom (two members ready get a match room).
//
// Game thread only. Nothing blocks: calls queue a message, answers are taken later.
#pragma once

#include <string>
#include <vector>

struct PortLeagueMember
{
    std::string name;
    int club;
    bool online;
    bool owner;
    int ready;  // the game it's ready to play, -1 none
};

struct PortLeagueState
{
    int link;            // 0 off, 1 connecting, 2 connected (a state heard)
    bool exists;         // a league with this code
    std::string code;
    int max;
    int version;         // the shared career's
    int myClub;          // -1 not a member
    bool owner;
    std::vector<PortLeagueMember> members;
    std::string error;   // the server's last refusal ("" none)
    int errors = 0;      // how many refusals so far (a new one, even with the same words)
};

// Connect to league `code` (4-8 letters/digits; "" leaves). Reconnects by itself.
void PortLeagueConnect(const std::string& code);
// A new league code (6 characters, no 0/O/1/I).
std::string PortLeagueNewCode();
// This headset's league id (16 hex) and name.
std::string PortLeagueMyId();
std::string PortLeagueMyName();
// The link's state now; `serial` grows with every change (a state, an error).
PortLeagueState PortLeagueGetState(unsigned* serial);

void PortLeagueCreate(int club, const std::string& career, int maxMembers);
void PortLeagueClaim(int club);
void PortLeagueLeave();
void PortLeagueGet();
// A career the server sent (once each): true with its text and version.
bool PortLeagueTakeCareer(std::string* text, int* version);
void PortLeaguePut(int base, const std::string& career, const char* op);
// The answer to the last put: 0 none yet, 1 landed (its new version), 2 conflict (the newest version).
int PortLeagueTakePutAnswer(int* version);
void PortLeagueReady(int game, int home, int away);
void PortLeagueUnready();
// A match to play (once): the league's game, the relay room, whether this headset hosts it.
bool PortLeagueTakeGo(int* game, std::string* room, bool* host);
