// The Quest OpenXR layer: the game on a big screen floating in the room (passthrough) or in the
// dark (VR), Touch controllers as the GameCube pad.
//
// Only Android builds with STRIKERS_XR do anything; everywhere else these are stubs that report
// "not active", so the flat main loop runs unchanged.
//
// Frame contract (src/Game/main.cpp):
//   PortXrInit() once, after aurora_initialize().
//   Each loop iteration while PortXrActive():
//     tick = PortXrBeginFrame();          // XR events, xrWaitFrame/xrBeginFrame, Touch -> PAD
//     if (tick) { aurora_begin_frame() ... game ... aurora_end_frame(); }
//     PortXrEndFrame(ranGameFrame);       // copy the new frame to the screen, xrEndFrame
// The game still steps at 60 Hz: PortXrBeginFrame says when a game frame is due, so a 72/90 Hz
// headset does not speed the game up.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when this build will try OpenXR (Quest build, STRIKERS_XR not "0"). Asked BEFORE
 * aurora_initialize, because XR mode makes Aurora headless: no window surface is ever created. */
int  PortXrWanted(void);
int  PortXrInit(void);
int  PortXrActive(void);
int  PortXrShouldQuit(void);
int  PortXrBeginFrame(void);
void PortXrEndFrame(int ranGameFrame);
void PortXrShutdown(void);

/* main.cpp: how long each step of one headset loop iteration took (ns), for the stats line:
 * PortXrBeginFrame (incl. xrWaitFrame), aurora_begin_frame, the game's tasks (incl. every send),
 * aurora_end_frame, PortXrEndFrame (incl. synchronize). */
void PortXrNoteLoopTimes(int ranGameFrame, unsigned long long xrBegin, unsigned long long auroraBegin,
                         unsigned long long tasks, unsigned long long auroraEnd, unsigned long long xrEnd);

/* Goal celebrations. Game.cpp calls PortXrGoal from CheckForGoal (team 0 home / 1 away; character
 * = eCharacterClass of whoever gets the credit, -1 unknown; superStrike = a captain's 2-goal Super
 * Strike; hyper = perfect timing; humanPad = the scorer's pad or -1), and PortCelebrationPoll
 * (main.cpp, once a game frame after the tasks) reports the phases:
 * 1 replay started, 2 replay played out, 3 skipped, 4 play live again. */
void PortXrGoal(int team, int character, int superStrike, int hyper, int ownGoal, int humanPad);
/* PortCelebrationPoll, every game frame: 1 while the match itself is being played (task state 2),
 * 0 in cutscenes, replays and menus. glPlat.cpp asks PortXrHudOnlyNormalView whether the normal
 * (broadcast) view may skip the 3D world: in Tabletop during live play it only feeds the small
 * scoreboard, so it draws the HUD alone. STRIKERS_XR_HUD_ONLY=0 keeps the full view. */
void PortXrSetGameplayLive(int live);
/* PortCelebrationPoll, every game frame: where the player you control (pad 0) is, in game units,
 * and his team's captain (eCharacterClass, for the colour); shown = 0 hides the Tabletop marker. */
void PortXrSetPlayerMarker(int shown, float x, float y, float z, int teamCaptain);
int PortXrHudOnlyNormalView(void);
void PortXrCelebrationPhase(int phase);
/* The game's full-screen flashes (ColourBlendScreenTransition, PhotoFlash), colour 0..1 and
 * opacity, every frame one is drawn: 1 = the headset shows it over your whole view, so the game
 * leaves it off its own picture; 0 = draw it as usual (not in the headset, or a dark fade). */
int PortXrScreenFlash(float r, float g, float b, float a);
/* Self-test (xr_fx "wipe"): 1 once when PortCelebrationPoll should play the white-flash wipe. */
int PortXrTakeTestWipe(void);
/* Self-test (xr_fx "item"): 1 once when PortCelebrationPoll should give your team a green shell. */
int PortXrTakeTestItem(void);
/* PortCelebrationPoll, every game frame: the Super Strike meter (ShootToScoreMeter). shown = 1
 * while it is up in live play; (x, y, z) = where the game hangs it (game units, by the captain);
 * halfU/halfV = half the square it takes in the middle of the picture, as fractions of the
 * picture's width/height (ShootToScoreMeter.cpp centres it there while PortXrHudOnlyNormalView).
 * In Tabletop the scoreboard shows that square over the captain instead of the score bar. */
void PortXrSetStsMeter(int shown, float x, float y, float z, float halfU, float halfV);
/* strikers-quest r44e: a Super Strike is under way (a captain charging or shooting it), once per
 * game frame. In Tabletop the screen then shows the game's whole picture (the shot, the meter),
 * beside the field, instead of the HUD-only score bar. */
void PortXrSetSuperStrike(int on);
/* Self-test (xr_fx "sts"): 1 once when PortCelebrationPoll should show the meter for 3 s. */
int PortXrTakeTestSts(void);

/* r42: the post-goal show, the life-size scorer (Lance: every goal but a Super Strike; the replay
 * keeps going until it is over). Presentation.cpp holds the goal replay until this many lockstep
 * game frames after the goal script starts; the headset times the scorer's run-up, celebration and
 * run-back to fit inside it. A compile-time constant: both headsets of an online match must agree. */
#define PORT_GOAL_SHOW_FRAMES 690
/* The show's timeline, seconds after the goal script starts (both XrScorer.cpp, which poses the
 * scorer, and xr_quest.cpp, which places it, follow it): appears and runs over, arrives beside you,
 * celebrates, runs back, gone. It runs at PORT_SCORER_SPEED (m/s) and stands PORT_SCORER_HEIGHT m. */
#define PORT_SCORER_APPEAR 1.2f
#define PORT_SCORER_ARRIVE 3.8f
#define PORT_SCORER_LEAVE 9.0f
#define PORT_SCORER_RUNOUT 2.3f
#define PORT_SCORER_SPEED 2.6f
#define PORT_SCORER_HEIGHT 1.52f
/* xr_scorer.cpp, every game frame: phase 0 hidden, 1 running over, 2 celebrating, 3 running back;
 * seconds into that phase; the character's own height in game units (scaled to PORT_SCORER_HEIGHT);
 * who (eCharacterClass), the side that scored (0 home / 1 away) and its captain (team colours). */
void PortXrScorerFrame(int phase, float phaseTime, float height, int characterClass, int side, int captainClass);
/* r43d, xr_scorer.cpp, just before each PortXrScorerFrame: the scorer's goals and assists this game
 * (goals < 0: unknown) and whether this goal is its third regular one (the hat trick). */
void PortXrScorerStats(int goals, int assists, int hatTrick);
/* r68h: and which of its team it is (cPlayer m_ID: 0 the captain, 1-3 the sidekicks; -1 the keeper):
   a sidekick's banner shows its own name (PortGuestSidekickName). */
void PortXrScorerPlayer(int playerId);
/* r73: the show that follows is a franchise nemesis's face-off (1), not a goal (0): its banner says so. */
void PortXrScorerFaceOff(int faceoff);
/* PortCelebrationPoll, every game frame: the game is paused (the crowd volume panel shows). */
void PortXrSetPaused(int paused);
/* Fielder.cpp UseTeamPowerup, for the log: the team's items (ePowerUpType) and whether the
 * player's state refused it. */
void PortXrNoteItemUse(int current, int next, int blocked);
/* DrawableCharacter.cpp: how much bigger to draw the players (about their feet), 1 = as the game
 * has them. Above 1 only in the Tabletop modes; gameplay is untouched. */
float PortXrCharacterScale(void);
/* One line to the headset log (logcat tag strikers_xr). */
void PortXrLog(const char* msg);
void PortCelebrationPoll(void);

/* The game's pad rumble, mirrored to the Touch controllers (pad 0 only). The preset is
 * eRumbleActionPreset (RumbleActions.h) and sets the strength; start/stop follow cPlatPad. */
void PortXrRumblePreset(int pad, int preset);
void PortXrRumbleStart(int pad, float seconds);
void PortXrRumbleStop(int pad);

/* r55, "explosive MR" (Lance: "Item effects that hit your room"): big item moments, reported by the
 * game where they happen (read only: nothing here touches the match, so online is unaffected).
 * Positions in game units (x along the pitch, z up). ndcX/ndcY: where the point is in the broadcast
 * picture (-1..1, y up), valid when ndcValid, for the big screen in your room.
 * A Bob-omb (or any exploding item) going off: size 0 small .. 2 giant, radius in game units,
 * type ePowerUpType, the thrower's pad (-1 none). */
void PortXrItemBlast(float x, float y, float z, int size, float radius, int type, int throwerPad, float ndcX,
                     float ndcY, int ndcValid);
/* The Chain Chomp landing on its target (ChainChomp.cpp), and biting a player, lunging along (vx, vy). */
void PortXrChompSlam(float x, float y, float z, float ndcX, float ndcY, int ndcValid);
void PortXrChompBite(float x, float y, float z, float vx, float vy);
/* glPlat.cpp: a game point in the broadcast picture (-1..1, y up); 0 = behind or off it. */
int PortBroadcastNdc(float x, float y, float z, float* ndcX, float* ndcY);

/* r57, the trophy shelf in your room: the cups you have won (the save's trophy bits, eTrophyType 0-7:
 * Mushroom, Flower, Star, Bowser, then the Super ones) and how many times each, from Game.cpp
 * whenever they change; and a cup just won (StatsTracker::AwardCup), first time or not. */
void PortXrSetTrophies(unsigned bits, const unsigned short* wins8);
void PortXrCupWon(int trophyType, int firstTime);

/* r61, the home ground (src/platform/xr_home.cpp; read only, nothing here changes the match):
 * a match is being set up; which side you play (Game.cpp's poll, the side of the player you
 * control; -1 none seen); a goal (side, a Super Strike); the match over (StatsTracker::TrackWinner:
 * the final scores as the game counts them, overtime, the side that forfeited or -1, a cup or
 * tournament game, both teams' captains as eCharacterClass). */
void PortHomeMatchBegin(void);
void PortHomeHumanSide(int side);
void PortHomeGoal(int side, int superStrike);
void PortHomeMatchEnd(int scoreLeft, int scoreRight, int overtime, int forfeitSide, int cup, int captainLeft,
                      int captainRight);
/* r61, the cup suite (VR when you enter a cup): Game.cpp's poll, twice a second: in a cup or
 * tournament between its games (its hub and screens), the cup being played for (eTrophyType, -1
 * none), the round, and the table's lines (team captain, wins, losses, points), yours marked. */
void PortHomeCupState(int inCupHub, int trophy, int round, int rounds, int teams, const int* captains,
                      const int* wins, const int* losses, const int* points, int userTeamIndex, int nextOpponent);

#ifdef __cplusplus
}
#endif
