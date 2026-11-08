// strikers-quest r172: STADIUM PULSE (src/platform/stadium_pulse.cpp). Lance: "a 'stadium pulse' feature like the
// old NCAA football games ... reactive crowds and more amplified crowd noise pending goals and late game antics, almost
// like a college basketball student section type vibe". ATMOSPHERE ONLY: nothing here is read by the match.
//
// The crowd is the HOME club's: side 0 (a quick match's left side; a franchise week's home club, franchise.cpp puts the
// home club on side 0), so your away games are hostile. The pulse (0-100) is computed from the match alone (the game's
// own audio events, the goals, the scores and the game clock, in whole tenths of game time, integer maths), so both
// headsets of an online match get the same curve.
#ifndef PORT_STADIUM_PULSE_H
#define PORT_STADIUM_PULSE_H

#ifdef __cplusplus
extern "C" {
#endif

// --- inputs (from the match; living_crowd.cpp forwards them)
void PortPulseEvent(int event, int team);  // AudioScriptEventMgr::FireEvent (AUDIO_EVENT, AUDIO_EVENT_TEAM)
void PortPulseGoal(int side);              // Game.cpp at a goal, after the score went up (0 home, 1 away scored)
void PortPulseUpdate(float dt);            // Audio::Update, every game frame: the clock's ticks + the crowd layers

// --- outputs
int PortPulseValue(void);          // 0..100 (-1 = no match / Classic)
float PortPulseCrowdGain(void);    // the crowd's loudness for the mixer (PortAudioSetCrowdLiving), 0.6..1.7
// CrowdMood.cpp: the chant to warm next (the game's own stomp-clap chants while the section is rocking), or NULL for
// the game's own pick.
const char* PortPulseChantPick(void);
// CrowdMood.cpp: 1 = start the warmed chant now, at *volume x the game's chant volume (your crowd volume included).
int PortPulseChantNow(float* volume);
void PortPulseChantStarted(void);
// GameRenderTask.cpp: the pulse gauge under the score bar (the big screen and the Tabletop scoreboard).
void PortStadiumPulseRender(float dt);

#ifdef __cplusplus
}
#endif

#endif // PORT_STADIUM_PULSE_H
