// Online 1v1 for the Quest build (2026-10-10): two headsets in the same room (a room code picked in
// the launcher panel) play one Friendly together. src/platform/xr_net.cpp.
//
// Delay-based lockstep: both run the same game and swap only their pad states, one per game frame,
// through the game's room server (r39: one outbound WebSocket each, no port forwarding; r33-r38 used
// a public MQTT broker). Starting a Friendly while the other player is in the room waits at the
// loading screen for them to start one too; then both load the same match (host = the home side
// with its captain, sidekick and stadium; guest = the away side with theirs) and every frame runs
// only once both players' input for it has arrived. Without a friend in the room a Friendly is
// played offline as always.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* A match setup: this headset's picks for PortNetHoldMatchStart, the agreed one for
 * PortNetFrameBegin. */
typedef struct PortNetMatch {
    int teams[2];       /* eTeamID, home and away */
    int sidekicks[2];   /* eSidekickID */
    int stadium;        /* eStadiumID */
    unsigned char options[64]; /* the host's gameplay / power-up / cheat settings, raw */
    int optionsSize;
    /* strikers-quest r44c: 0 both sides human; 1 home / 2 away played by this headset's computer AI
     * (only the test friend, tools/fakefriend, asks for it; a real headset never does). */
    int cpuPlays;
    /* strikers-quest r63, online co-op Cup (Lance: "an online co-op mode for Cup mode"): the game
     * mode (GameInfoManager::eGameModes; 0 Friendly), and for a cup the side both players play on
     * (the cup team's) and the host's cup as it stands (Game.cpp port_net_pack_cup), which the
     * guest takes over at the match start: the two headsets play the same cup. */
    int mode;
    int coopSide;
    unsigned char cup[3584];
    int cupSize;
    /* How far the cup is (games played), so the headset further along hosts it. */
    int cupProgress;
    /* strikers-quest r67 (protocol 8): each side's guest characters, captain and sidekick, by pack
     * name ("" none; include/port/xr_guest.h). Online they are drawn and named as themselves on both
     * headsets that have the pack, and play on their slot's stats (both simulate the same match). */
    char guests[2][2][24];
    /* strikers-quest r67b (protocol 9): those guests' own stats as their own headset read them
     * (PORT_GUEST_STATS = 28 values, mask = the keys set; 0 = none, the slot's). Both headsets play
     * each side's guests on these, so the match is the same on both. */
    float guestStats[2][2][28];
    unsigned guestStatMask[2][2];
    /* strikers-quest r78 (protocol 11, the franchise's 'F' tail): each side's co-captain
     * (include/port/lineup.h): its captain character class (-1 none), its guest (pack, "" none) and that
     * guest's own stats. */
    int coCaptains[2];
    char coGuests[2][24];
    float coStats[2][28];
    unsigned coStatMask[2];
} PortNetMatch;

/* SuperLoadingScene, every frame its intro slide is done, with this headset's picks and the side
 * its player chose: 1 = hold the match start (waiting for the friend), 0 = go ahead (offline, or
 * the online match starts this frame). */
int PortNetHoldMatchStart(const PortNetMatch* localChoice, int localSide);

/* main.cpp, before the game's tasks each game frame. While online: 1 and the RNG seed to use for
 * this frame (reseeded every frame, so a difference in what one headset draws cannot carry into
 * the next frame). On the first online frame also fills the match setup to apply. */
int PortNetFrameBegin(unsigned int* seed, PortNetMatch* startMatch, int* isStartFrame);

/* strikers-quest r39b: main.cpp, once a game frame really runs (aurora_begin_frame took it), before
 * PortNetGameFrameBegin: uses up the online frame whose pads the headset layer handed to PAD. A
 * frame the renderer refuses is handed out again with the same inputs and seed, so both headsets
 * run the same frames. Nothing offline. */
void PortNetCommitFrame(void);

/* Game.cpp, after the tasks of an online frame: a hash of the match state, compared between the
 * headsets every 30 frames (desync check), and whether the game is back in the front end. */
void PortNetFrameEnd(unsigned int stateHash, int inFrontEnd);

/* Game.cpp, the game's half: PortNetHoldMatchStart with this headset's picks from GameInfoManager
 * (a Friendly; r63: or a cup match, for co-op), and PortNetFrameBegin applied (the seed, and the
 * agreed match on frame 0). */
int PortNetGameHoldMatchStart(void);
void PortNetGameFrameBegin(void);

/* The pad this headset's player uses: 0 offline or as host, 1 as guest while online. */
int PortNetLocalPad(void);

/* 1 while an online match runs (fixed game time step, both pads from the network). */
int PortNetOnline(void);

/* strikers-quest r45: Game.cpp, every online frame while both teams exist: the score (home, away).
 * A tournament match (the panel's match id, nativeSetTourney) reports it when the match ends. */
void PortNetNoteScore(int home, int away);

/* strikers-quest r76, franchise co-op (Lance: online on their own headsets, the SAME club, both keep the
 * career). The co-op career is run by one headset (the one that made the room, Host an online match;
 * else the lower id): its hub's actions and its matches' start. Its career file goes to the friend after
 * every change (in pieces: the room server takes 4 KB a message) and both save the same one; a match
 * starts on both from the same career and plays in lockstep with both players on that club (like the
 * co-op Cup: the host the captain and a sidekick, the friend the other two).
 *
 * PortNetMatch.mode for a co-op franchise match (no eGameModes value): both sides' teams, sidekicks,
 * stadium and guests (with their stats) come from the host; coopSide = the club's side (-1: watching a
 * CPU v CPU game together). */
enum { PORT_NET_MODE_FRANCHISE = 200 };
/* r100: coopSide for an online league's game between two people (include/port/xr_league.h): each on their own
 * club, pad 0 the host (the home club), pad 1 the other (the away club). */
enum { PORT_NET_COOP_VERSUS = 2 };
/* r100: the league's match room (its go) and whether this headset hosts it (the home club's); null: back to
 * the launcher's room as it was. */
void PortNetLeagueRoom(const char* room, int host);
/* 1 once the connection has moved to that room (the match start can wait for the other person there). */
int PortNetLeagueRoomReady(void);
int PortNetCoopFriend(void);         /* 1: in a room with the friend there (same build and Version) */
int PortNetCoopHost(void);           /* 1: this headset runs the co-op career */
const char* PortNetCoopFriendName(void);  /* their team name ("" unknown) */
/* What this headset has (franchise.cpp, every frame): a co-op career saved (its id = its seed, and its
 * version, counted up on every save), and whether it is open in the hub. Told to the friend each second. */
void PortNetCoopSetMine(int has, unsigned careerId, unsigned version, int inHub);
/* The friend's, from their last status: 0 nothing heard yet, else 1 (has / id / version / in the hub). */
int PortNetCoopFriendCareer(int* has, unsigned* careerId, unsigned* version, int* inHub);
/* Send this headset's co-op career (the save file's text) to the friend; resent until they have it all. */
void PortNetCoopSendCareer(const char* text, int size, unsigned careerId, unsigned version);
/* A whole career from the friend, once: the text (valid until the next poll) and its size, or null. */
const char* PortNetCoopTakeCareer(int* size, unsigned* careerId, unsigned* version);
/* Ask the friend for their co-op career / whether they asked for this headset's (once). */
void PortNetCoopRequestCareer(void);
int PortNetCoopTakeRequest(void);
/* The host starts a match of the co-op career (its version as saved; watchGame -1 = the club's own match,
 * else the CPU v CPU game being watched); the friend's franchise takes it once and starts the same. */
void PortNetCoopSendPlay(unsigned careerId, unsigned version, int watchGame);
int PortNetCoopTakePlay(unsigned* careerId, unsigned* version, int* watchGame);
/* Game.cpp: the host's match settings as the online start carries them (gameplay, power-ups, cheats,
 * difficulty), for the franchise's own match start. */
int PortNetPackOptions(unsigned char* out);

#ifdef __cplusplus
}
#endif
