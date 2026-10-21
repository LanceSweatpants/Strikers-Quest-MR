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

#ifdef __cplusplus
}
#endif
