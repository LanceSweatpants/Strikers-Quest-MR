// strikers-quest r42: the life-size goal scorer (src/platform/xr_scorer.cpp).
#ifndef PORT_XR_SCORER_H
#define PORT_XR_SCORER_H

class cCharacter;

// Game.cpp CheckForGoal (port_celebration_goal): who scored (the credited player; a keeper or an
// unknown stands in as the team's captain), which side, and whether it was a Super Strike (no show).
void XrScorerGoal(const cCharacter* scorer, int side, bool superStrike);
// r73: franchise mode's nemesis face-off (franchise.cpp, at the match intro): the nemesis comes up to
// you life-size, shows off, and runs back before kick-off.
void XrScorerFaceOff(const cCharacter* who, int side);
// GameRenderTask, every game frame after the snapshot is rendered: pose the scorer's copy and send
// it to its own headset layer, and tell the headset where the show is.
void XrScorerRender();

#endif // PORT_XR_SCORER_H
