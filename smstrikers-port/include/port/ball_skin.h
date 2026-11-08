// strikers-quest r168 (Lance picked FIREBALL and PLANET EARTH from the ball mockups, logs/balls-mockup): BALL SKINS. Looks
// only - the ball's physics, size and collision are the game's, untouched. The game's own draw of the ball
// (Game/Drawable/DrawableBall.cpp) swaps the ball model's geometry for a sphere of ours and its picture for one frame of a
// living, looping texture of ours (src/platform/ball_skin.cpp: baked on a thread from our own procedural shader - no game
// art), so the skin is in the game's own picture: the big screen, the Tabletop field, the life-size stadium, the replays.
// The trail and the hits (src/platform/xr_balls.cpp, the headset's effects layer) read where the ball is from here.
//
// Plain C: xr_balls.cpp (strikers_xr, no port prelude) reaches it by relative path.
#ifndef PORT_BALL_SKIN_H
#define PORT_BALL_SKIN_H

#ifdef __cplusplus
extern "C" {
#endif

// The skins (fixed: the shop's ids map onto these)
// r170: + 21 more (src/platform/ball_shade.h skin_defs(): its order; saved / sent by id only)
enum { PORT_BALL_CLASSIC = 0, PORT_BALL_FIREBALL = 1, PORT_BALL_EARTH = 2, PORT_BALL_COUNT = 24 };

// xr_balls.cpp, every display frame (any thread): your skin, the online friend's (0 classic / none), your side (0 home,
// 1 away, -1 not known) and whether a match is online. The ball wears the skin of the side that touched it last (your
// side: yours; online, the friend's side: theirs; the computer's: whichever it last wore), so both headsets show the
// same ball.
void PortBallSkinSetIds(int mine, int theirs, int mySide, int online);

// Where the ball is, for the effects (xr_balls.cpp, any thread): copied out under a lock. False before any match frame.
typedef struct PortBallFeed {
  unsigned frame;      // counts the game frames the ball was drawn in
  int visible;         // drawn this frame
  int skin;            // the skin drawn now (PORT_BALL_*; 0 when it is the game's own)
  float pos[3];        // where it is drawn (game units, Z up; the Tabletop's bigger ball lifted as drawn)
  float vel[3];        // its velocity (game units a second)
  float radius;        // its drawn radius (game units: x the Tabletop's players' scale)
  int ndcValid;        // where it is in the broadcast picture (-1..1, y up) and how big there (its radius in NDC y)
  float ndc[2], ndcRadius;
  unsigned kicks, bounces, goals;  // counters: a kick (its speed jumped), a bounce off the ground, a goal
  float kickPos[3], bouncePos[3], goalPos[3];  // where the last of each happened (game units)
  float kickSpeed, bounceSpeed;    // how hard (game units a second)
  int kickNdcValid, bounceNdcValid, goalNdcValid;
  float kickNdc[2], bounceNdc[2], goalNdc[2];
  double timeSec;      // the clock the skins animate on (s)
} PortBallFeed;
int PortBallSkinFeed(PortBallFeed* out);

// Game.cpp's goal (read only): where the ball is (game units)
void PortBallSkinGoal(float x, float y, float z);

// xr_balls.cpp's shop tiles / proofs: one texel of a skin's living surface at the unit direction (x, y, z) of the ball's own
// space (Z its pole) at time t (s), as sRGB 0..1 (the bake's own function). Any thread.
void PortBallSkinShade(int skin, float x, float y, float z, float t, float rgb[3]);

#ifdef __cplusplus
}
#endif

#endif  // PORT_BALL_SKIN_H
