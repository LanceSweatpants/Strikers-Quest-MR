// strikers-quest r203 (Lance's r202 test, 2026-11-09: "the joystick only let me grip it, it did not let me move it, select
// anything, nor would it let me access the casino games"): THE TIZM MACHINE'S CONTROLS while a hand holds its joystick.
//
// r202 handed the machine only one controller's buttons (A / B are the RIGHT controller's, X / Y the LEFT's - with the left hand
// on the stick there was no A to pick and no B for the casino), and its whole input sat behind "the franchise hub is closed"
// (the hub is open behind you whenever a career is loaded - so nothing ever reached the machine, and the hub's own page moved
// under the left stick instead). Now, whichever hand grips:
//   MOVE    either thumbstick (r204: a firm flick - see below); the gripping hand only LEANS the 3D stick (it follows your hand)
//   PICK    A or X (either controller's lower button), or either trigger (not one whose pointer is on the screen: that one
//           is the pointer's click)
//   BACK    B or Y (the upper buttons) - from the top of the sportsbook / slots / an empty roulette board that's the GAME PICKER
//   GAMES   the MENU button: the game picker from anywhere (again: back where it was opened from)
// Each step that MOVES the selection / pick / back gives a short haptic tick, and every one is logged ("sportsbook: input ...").
//
// r204 (Lance's r203 test, 2026-11-09: "so sensitive that my stick drift takes it everywhere and you can hear it (ticks), but at
// the same time the highlight doesn't move at all"; his log: 105 of 109 moves were the HAND pushing the joystick - 3 cm from
// where it took hold was a push, so the ordinary wobble of a hand holding a ball walked the cursor down / up / down and back;
// his clip: the highlight jumping between two rows and back between 1 fps frames):
//   - the HAND no longer navigates (set_hand_nav(false), the default): the cabinet's stick still leans with your hand, but only
//     the thumbsticks move the cursor. set_hand_nav(true, metres) brings it back with a threshold of your choosing (a setting
//     for later; 6 cm at least).
//   - thumbsticks: a push is 55% of the way over on its stronger axis (and that axis at least 1.6 x the other: a drifting
//     stick wobbling round the middle never counts), centred again under 30% (hysteresis); one step per flick; held, it
//     repeats only after 0.45 s, then 7 a second.
//   - a move that changes nothing (the end of a list) is silent: no tick, no haptic ("nothing that way" in the log only).
//
// r205 (Lance's r204 test, 2026-11-09, angry: "I have to literally move my LEFT THUMBSTICK while HOLDING the joystick to move -
// dumb"): THE JOYSTICK IS THE CONTROLLER, like a real arcade stick - and you don't have to hold it to use the thumbsticks.
//   ARCADE STICK (a hand holds the 3D stick): TILTING it moves the cursor. The tilt is the hand's sideways / forward travel from
//     where it took hold, as an angle about a 15 cm lever (15 degrees = 4.0 cm), low-passed (60 ms): a push = 15 degrees on its
//     stronger axis (that axis only: a diagonal is one step), one step a push; it must come back under 6 degrees to count again
//     (hysteresis: 1.6 cm); held over (10 degrees+) it repeats after 0.45 s, then 6 a second. While it's near the middle (under 6) the
//     centre follows the hand slowly (1.5 s), so a body that sways or drifts never walks the cursor. Hand tremor (+-2 cm = 7.6
//     degrees) never reaches 15. The 3D stick tilts with the hand (20 degrees = all the way) and springs back to the middle
//     when let go.
//   LATCHED (hands free): a QUICK grip on the stick (let go within 0.5 s, no tilt-move) keeps MACHINE CONTROL ON - the
//     thumbsticks (55% push, as r204) and the buttons drive it with nothing held - until you walk away (1.2 m), press MENU
//     twice (within 0.6 s) or grip the stick quickly again. Holding it while latched = the arcade stick; let go: latched still.
//     A long hold that wasn't latched lets go as before (r204).
//   POKE: a fingertip (the controller's tip) pressing one of the cabinet's 3D buttons (within 3 cm of its top) = that button
//     (A / X pick, B / Y back, START = MENU); a poke with the machine off turns it on, latched.
// Pure C++ (xr_book.h / xr_casino.h + two hooks), so the PC proof drives this same file (tools/outside/machine205).
#pragma once

namespace xrmachine {

struct Pad {
  bool engaged = false;          // MACHINE CONTROL ON (held or latched: control().on)
  bool held = false;             // r205: a hand holds the 3D stick now (the arcade stick)
  int hand = -1;                 // which (0 left, 1 right; the last to hold it)
  float stick[2][2] = {{0, 0}, {0, 0}};  // the thumbsticks [left, right][x, y] (y up = +)
  bool handValid = false;
  float handAt[3] = {0, 0, 0};   // the gripping hand in the cabinet's own frame, in metres (x right, z toward you)
  bool a = false, b = false, x = false, y = false, menu = false;
  bool poke[5] = {false, false, false, false, false};  // r205: a fingertip pressed the cabinet's A, B, X, Y, START this frame
  float trig[2] = {0, 0};
  bool trigOnScreen[2] = {false, false};  // that hand's pointer is on the machine's screen (its trigger clicks there)
  double now = 0.0;              // seconds
};

// The hooks: a log line ("sportsbook: input ...") and a haptic tick (amplitude 0..1, seconds)
void set_hooks(void (*log)(const char* line), void (*tick)(float amp, float sec));
// (r204's hand navigation switch: r205's tilt replaces it - kept so old callers build; it does nothing)
void set_hand_nav(bool on, float metres = 0.07f);
// r206 (strikers.ini machine_right_stick = 0, Lance's drifting right stick): false = the RIGHT thumbstick (Pad::stick[1]) is
// ignored - no move on any screen, latched or held, and the 3D stick doesn't lean with it; the left stick, buttons, triggers as ever
// (default true)
void set_right_stick(bool on);
bool right_stick();
// One frame: reads the pad, drives xrbook / xrcasino; `lean` gets where the cabinet's stick leans (-1..1, x right, y away)
void update(const Pad& p, float lean[2]);

// ---- r205: who has the machine (xr_quest.cpp book_engage feeds it every frame) ----
constexpr double kQuickGrip = 0.5;  // a grip let go within this (no tilt-move in it) = a tap: latch / unlatch
constexpr double kMenuTwice = 0.6;  // MENU, MENU within this = let go
constexpr float kAwayM = 1.2f;      // walked this far from the stick = let go
constexpr float kArm = 0.15f, kOnDeg = 15.0f, kOffDeg = 6.0f, kHoldDeg = 10.0f, kFullDeg = 20.0f, kTiltTau = 0.06f;
constexpr double kTiltFirst = 0.45, kTiltRepeat = 1.0 / 6.0;
struct Grip {
  bool down[2] = {false, false};  // that grip closed this frame within reach of the stick's ball
  bool held[2] = {false, false};  // the grips now (and that hand tracked)
  bool poke = false;              // a fingertip pressed one of its buttons this frame
  float away = 0.0f;              // your head's distance from the stick (metres, across the floor)
  double now = 0.0;
};
enum Event { kNoEvent = 0, kOnHeld, kLatched, kOffGrip, kOffAway, kOffMenu, kOffRegrip, kHeldAgain, kBackToLatched, kOnPoke, kOffGone };
struct Control {
  bool on = false, held = false, latched = false;
  int hand = -1;
};
int control_update(const Grip& g);  // the event this frame (kNoEvent none)
Control control();
void control_off(int why);          // (kOffGone: the machine went away)
int take_event();                   // an event update() made (MENU twice: kOffMenu), once
const char* event_text(int e);

// What happened so far (the PC proof)
struct Counts {
  int moves = 0, picks = 0, backs = 0, pickers = 0, used = 0;
  int changed = 0;  // r204: moves that really moved the selection (the ones that tick)
  int tiltMoves = 0;  // r205: moves made by tilting the 3D stick
  int pokes = 0;      // r205: buttons poked
};
Counts counts();
void reset_counts();
// r205: the stick's tilt now (degrees, filtered: x right, y away) and whether it is armed (back under 6 degrees since its last push)
void tilt_debug(float* tx, float* ty, bool* armed);

// ---- r204: the printed TICKET in a hand (Lance's r203 test: "TOO paper-like - semi see-through at points, impossible to read,
// hard to pick up in ways that feel natural"; posted on the machine twice when he only let go near it). Pure math, so the PC
// proof poses the same paper the headset does (xr_quest.cpp book_cabinet_frame calls it every frame the ticket is held).
// Held, the ticket is stiff card stock: it stays where your fingers took it (anywhere along it), its printed side turns to
// your eyes smoothly (~0.15 s), its text upright (its length follows "down" as you see it, a little of your wrist's roll), a
// gentle curl away from you at its ends - no droop, no flapping.
struct Hold {
  float grabU = 0.0f;         // where along the paper the hand has it (0 its head .. 1 its foot)
  float n[3] = {0, 0, 1};     // the printed side's normal (smoothed)
  float d[3] = {0, -1, 0};    // down its length (smoothed, square to n)
  bool init = false;
  float pressT = 0.0f;        // how long it has been held flat against the cabinet (posting it)
};
// A grip closing on the paper (its middle line `pts`, n + 1 points head first; `side` across it): where along it the hand is
// (the nearest point: 0..1), the hold's frame from the paper as it hangs. False: the hand is farther than `reach` from all of it.
bool ticket_grab(Hold& h, const float (*pts)[3], int n, const float side[3], const float hand[3], float reach);
// One frame held: the grip's position + its axes (right, up, forward: world, unit), your eyes, the paper's length, dt ->
// its middle line (n + 1 points) and its across direction (side: the printed text's left -> right)
void ticket_hold(Hold& h, const float grip[3], const float gx[3], const float gy[3], const float gz[3], const float eye[3], float L,
                 float dt, float (*out)[3], int n, float side[3]);
// r204: POSTING is deliberate - the paper pressed flat against a face of the cabinet (`dist` metres off it, `facing`: the cosine
// between its printed side and the face's outward normal) held still ~0.5 s. Returns 0..1 (1: post it now); anything else
// lets it go back to 0 (and letting go anywhere just lets it go: it fades, as before).
constexpr float kPostDist = 0.04f, kPostFacing = 0.5f, kPostHold = 0.5f;
float ticket_press(Hold& h, float dist, float facing, float dt);

}  // namespace xrmachine
