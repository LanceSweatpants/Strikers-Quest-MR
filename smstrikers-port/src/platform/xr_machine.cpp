// strikers-quest r203 / r204 / r205: the TIZM machine's controls (r205: the joystick tilted = the arcade stick; a quick grip latches it hands free) + the printed ticket held in a hand
// (xr_machine.h says what and why)
#include "xr_machine.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>

#include "xr_book.h"
#include "xr_casino.h"

namespace xrmachine {
namespace {

void (*g_log)(const char*) = nullptr;
void (*g_tick)(float, float) = nullptr;
Counts g_counts;

struct State {
  bool engaged = false;
  int dir = 0, src = -1;      // the thumbstick direction held (1 left 2 right 3 up 4 down) and its stick (0 / 1)
  double nextAt = 0.0;        // its next repeat
  bool pa = false, pb = false, px = false, py = false, pm = false;
  bool trigDown[2] = {false, false};
  double lastMenu = -1e9;     // r205: MENU twice = let go
  // r205: the arcade stick (a hand holding it): where the hand took hold (slowly following it while near the middle), the hand
  // low-passed, the tilt's push state
  bool held = false, tiltInit = false;
  float base[3] = {0, 0, 0}, filt[3] = {0, 0, 0};
  bool armed = true;
  int tiltDir = 0;
  double tiltNext = 0.0, lastNow = -1.0;
  float tx = 0.0f, ty = 0.0f;  // degrees (x right, y away)
  // the 3D stick's lean shown (a spring back to the middle when nothing holds it)
  float lean[2] = {0, 0}, leanV[2] = {0, 0};
};
State g_s;
Control g_ctl;
double g_heldSince = 0.0;
bool g_wasLatched = false;
bool g_tiltUsed = false;  // a tilt-move made during this hold (then it's no tap)
int g_event = kNoEvent;

// r204: a firm push - 55% over on its stronger axis, that axis 1.2 x the other; centred again under 30%
constexpr float kStickOn = 0.55f, kStickOff = 0.30f, kAxisLead = 1.2f;
// r204: one step a flick; held: the first repeat at 0.45 s, then 7 a second
constexpr double kFirst = 0.45, kRepeat = 1.0 / 7.0;
const char* const kDirName[5] = {"none", "left", "right", "up", "down"};
const char* const kSrcName[3] = {"left thumbstick", "right thumbstick", "the joystick tilted"};

void say(const char* fmt, ...) {
  if (g_log == nullptr) return;
  char b[512];
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  g_log(b);
}
void tick(float amp, float sec) {
  if (g_tick != nullptr) g_tick(amp, sec);
}
const char* screen_name() {
  switch (xrcasino::screen()) {
    case xrcasino::kSlots: return "SLOTS";
    case xrcasino::kRoulette: return "ROULETTE";
    case xrcasino::kPicker: return "the GAME PICKER";
    default: return "the SPORTSBOOK";
  }
}
float clamp1(float v) { return std::max(-1.0f, std::min(1.0f, v)); }
// how far a source pushes toward direction d
float along(const float v[2], int d) {
  switch (d) {
    case 1: return -v[0];
    case 2: return v[0];
    case 3: return v[1];
    case 4: return -v[1];
    default: return 0.0f;
  }
}
// the direction a source pushes past `on` (its stronger axis, clearly the stronger), 0 none
int dir_of(const float v[2], float on) {
  const float ax = std::fabs(v[0]), ay = std::fabs(v[1]);
  if (std::max(ax, ay) < on) return 0;
  if (ax >= ay) return ax >= kAxisLead * ay ? (v[0] < 0.0f ? 1 : 2) : 0;
  return ay >= kAxisLead * ax ? (v[1] > 0.0f ? 3 : 4) : 0;
}

}  // namespace

void set_hooks(void (*log)(const char*), void (*t)(float, float)) {
  g_log = log;
  g_tick = t;
}
void set_hand_nav(bool, float) {}
bool g_rightStick = true;  // r206
void set_right_stick(bool on) { g_rightStick = on; }
bool right_stick() { return g_rightStick; }
Counts counts() { return g_counts; }
void reset_counts() { g_counts = Counts(); }
void tilt_debug(float* tx, float* ty, bool* armed) {
  *tx = g_s.tx;
  *ty = g_s.ty;
  *armed = g_s.armed;
}

// ---------------------------------------------------------------------------------------------- r205: who has the machine
const char* event_text(int e) {
  switch (e) {
    case kOnHeld: return "MACHINE CONTROL ON (a hand on the joystick: tilt it to move)";
    case kLatched: return "MACHINE CONTROL ON - LATCHED (a quick grip: the thumbsticks + buttons have it, hands free; grip the stick again, MENU twice or walk away to let go)";
    case kOffGrip: return "MACHINE CONTROL OFF (the grip opened)";
    case kOffAway: return "MACHINE CONTROL OFF (stepped away)";
    case kOffMenu: return "MACHINE CONTROL OFF (MENU twice)";
    case kOffRegrip: return "MACHINE CONTROL OFF (a quick grip on the stick again)";
    case kHeldAgain: return "the joystick held again (latched: tilt it to move; let go and it stays latched)";
    case kBackToLatched: return "the joystick let go - still LATCHED (the thumbsticks + buttons)";
    case kOnPoke: return "MACHINE CONTROL ON - LATCHED (a button poked)";
    case kOffGone: return "MACHINE CONTROL OFF (the machine is away)";
    default: return "";
  }
}
Control control() { return g_ctl; }
void control_off(int why) {
  if (!g_ctl.on) return;
  g_ctl = Control();
  g_event = why;
}
int take_event() {
  const int e = g_event;
  g_event = kNoEvent;
  return e;
}
int control_update(const Grip& g) {
  Control& c = g_ctl;
  if (!c.on) {
    for (int h = 0; h < 2; ++h)
      if (g.down[h]) {
        c.on = c.held = true;
        c.latched = false;
        c.hand = h;
        g_heldSince = g.now;
        g_wasLatched = false;
        g_tiltUsed = false;
        return kOnHeld;
      }
    if (g.poke) {
      c.on = c.latched = true;
      c.held = false;
      return kOnPoke;
    }
    return kNoEvent;
  }
  if (g.away > kAwayM) {
    c = Control();
    return kOffAway;
  }
  if (c.held) {
    if (c.hand < 0 || !g.held[c.hand]) {
      const bool tap = g.now - g_heldSince < kQuickGrip && !g_tiltUsed;
      if (!g_wasLatched && tap) {
        c.held = false;
        c.latched = true;
        return kLatched;
      }
      if (g_wasLatched && tap) {
        c = Control();
        return kOffRegrip;
      }
      if (g_wasLatched) {
        c.held = false;
        c.latched = true;
        return kBackToLatched;
      }
      c = Control();
      return kOffGrip;
    }
    return kNoEvent;
  }
  // latched: a grip on the stick takes it in hand (the arcade stick; a tap of it lets go)
  for (int h = 0; h < 2; ++h)
    if (g.down[h]) {
      c.held = true;
      c.hand = h;
      g_heldSince = g.now;
      g_wasLatched = true;
      g_tiltUsed = false;
      return kHeldAgain;
    }
  return kNoEvent;
}

void update(const Pad& pad, float lean[2]) {
  Pad rs = pad;  // r206: machine_right_stick = 0 - the right thumbstick is nothing here
  if (!g_rightStick) rs.stick[1][0] = rs.stick[1][1] = 0.0f;
  const Pad& p = rs;
  State& s = g_s;
  const double dt = s.lastNow >= 0.0 ? std::max(0.0, std::min(0.1, p.now - s.lastNow)) : 1.0 / 72.0;
  s.lastNow = p.now;
  // the 3D stick's lean: with the hand while held, else a spring back to the middle (or the thumbstick's lean while engaged)
  auto spring = [&](const float target[2]) {
    for (int k = 0; k < 2; ++k) {
      s.leanV[k] += static_cast<float>((380.0 * (target[k] - s.lean[k]) - 20.0 * s.leanV[k]) * dt);
      s.lean[k] += static_cast<float>(s.leanV[k] * dt);
    }
  };
  if (!p.engaged) {
    if (s.engaged) say("sportsbook: input off (the machine let go)");
    const float lz[2] = {0.0f, 0.0f}, keep[2] = {s.lean[0], s.lean[1]}, keepV[2] = {s.leanV[0], s.leanV[1]};
    const double ln = s.lastNow;
    s = State();
    s.lastNow = ln;
    s.lean[0] = keep[0], s.lean[1] = keep[1], s.leanV[0] = keepV[0], s.leanV[1] = keepV[1];
    spring(lz);
    lean[0] = clamp1(s.lean[0]);
    lean[1] = clamp1(s.lean[1]);
    return;
  }
  if (!s.engaged) {  // just took it: anything already held must be let go before it counts
    s.engaged = true;
    s.pa = p.a, s.pb = p.b, s.px = p.x, s.py = p.y, s.pm = p.menu;
    for (int h = 0; h < 2; ++h) s.trigDown[h] = p.trig[h] > 0.3f;
    // (a stick already pushed as it's taken must come back to the middle first)
    for (int k = 0; k < 2; ++k)
      if (dir_of(p.stick[k], kStickOff) != 0) s.dir = dir_of(p.stick[k], kStickOff), s.src = k, s.nextAt = 1e300;
    say("sportsbook: input ready (%s, %s up) - %s, A / X / trigger: pick, B / Y: back, MENU: games, MENU twice: let go",
        p.held ? (p.hand == 1 ? "the right hand on the joystick" : "the left hand on the joystick") : "latched, hands free",
        screen_name(), p.held ? "TILT THE JOYSTICK or a thumbstick: move" : "a thumbstick: move");
  }
  // ---- r205: the arcade stick - the tilt from where the hand took hold
  int tiltStep = 0;
  if (p.held && p.handValid) {
    if (!s.held || !s.tiltInit) {
      for (int k = 0; k < 3; ++k) s.base[k] = s.filt[k] = p.handAt[k];
      s.tiltInit = true;
      s.armed = true;
      s.tiltDir = 0;
      s.tx = s.ty = 0.0f;
    }
    const float a = 1.0f - std::exp(-static_cast<float>(dt) / kTiltTau);
    for (int k = 0; k < 3; ++k) s.filt[k] += (p.handAt[k] - s.filt[k]) * a;
    const float dx = s.filt[0] - s.base[0], dz = s.filt[2] - s.base[2];
    s.tx = std::atan2(dx, kArm) * 57.29578f;
    s.ty = std::atan2(-dz, kArm) * 57.29578f;  // (away from you = up)
    const float mag = std::sqrt(s.tx * s.tx + s.ty * s.ty);
    if (s.armed) {
      const float ax = std::fabs(s.tx), ay = std::fabs(s.ty);
      if (std::max(ax, ay) >= kOnDeg) {  // a push: its stronger axis only (a diagonal is one step)
        s.tiltDir = ax >= ay ? (s.tx < 0.0f ? 1 : 2) : (s.ty > 0.0f ? 3 : 4);
        s.armed = false;
        s.tiltNext = p.now + kTiltFirst;
        tiltStep = s.tiltDir;
      } else if (mag < kOffDeg) {  // near the middle: the centre follows the hand slowly (a body's sway / drift never adds up)
        const float b = 1.0f - std::exp(-static_cast<float>(dt) / 1.5f);
        for (int k = 0; k < 3; ++k) s.base[k] += (s.filt[k] - s.base[k]) * b;
      }
    } else {
      const float v[2] = {s.tx, s.ty};
      if (mag < kOffDeg) {  // back in the middle: armed again
        s.armed = true;
        s.tiltDir = 0;
      } else if (along(v, s.tiltDir) >= kHoldDeg && p.now >= s.tiltNext) {  // held over: it repeats
        tiltStep = s.tiltDir;
        s.tiltNext += kTiltRepeat;
        if (s.tiltNext < p.now) s.tiltNext = p.now + kTiltRepeat;
      }
    }
  } else {
    s.tiltInit = false;
    s.armed = true;
    s.tiltDir = 0;
    s.tx = s.ty = 0.0f;
  }
  s.held = p.held;
  // the 3D stick: the hand's tilt while held (or a thumbstick pushed harder), else springing back / the thumbstick's lean
  {
    float target[2] = {0.0f, 0.0f};
    float best = 0.0f;
    for (int k = 0; k < 2; ++k) {
      const float m = std::sqrt(p.stick[k][0] * p.stick[k][0] + p.stick[k][1] * p.stick[k][1]);
      if (m > best) best = m, target[0] = clamp1(p.stick[k][0]), target[1] = clamp1(p.stick[k][1]);
    }
    if (p.held && p.handValid) {
      const float hx = clamp1(s.tx / kFullDeg), hy = clamp1(s.ty / kFullDeg);
      if (std::sqrt(hx * hx + hy * hy) >= best) {
        s.lean[0] = hx, s.lean[1] = hy;  // (in the hand: no spring)
        s.leanV[0] = s.leanV[1] = 0.0f;
      } else {
        spring(target);
      }
    } else {
      spring(target);
    }
    lean[0] = clamp1(s.lean[0]);
    lean[1] = clamp1(s.lean[1]);
  }
  // ---- the thumbsticks (r204): a firm flick, one step; held: 0.45 s then 7 a second
  int dir = 0, src = -1;
  if (s.dir != 0 && s.src >= 0 && s.src < 2 && along(p.stick[s.src], s.dir) > kStickOff) {
    dir = s.dir;
    src = s.src;
  } else {
    float best = 0.0f;
    for (int k = 0; k < 2; ++k) {
      const int d = dir_of(p.stick[k], kStickOn);
      if (d != 0 && along(p.stick[k], d) > best) {
        best = along(p.stick[k], d);
        dir = d;
        src = k;
      }
    }
  }
  bool step = false;
  if (dir != 0 && (dir != s.dir || src != s.src)) {
    step = true;
    s.nextAt = p.now + kFirst;
  } else if (dir != 0 && p.now >= s.nextAt) {
    step = true;
    s.nextAt += kRepeat;  // (on the beat: 7 a second however the frames fall)
    if (s.nextAt < p.now) s.nextAt = p.now + kRepeat;
  }
  s.dir = dir;
  s.src = dir != 0 ? src : -1;
  if (tiltStep != 0) {  // (the stick in the hand wins a frame both move in)
    step = true;
    dir = tiltStep;
    src = 2;
  }
  // the buttons, as they go down (both controllers': A / X pick, B / Y back, MENU the picker; a trigger picks too; r205: or poked)
  const bool aP = p.a && !s.pa, bP = p.b && !s.pb, xP = p.x && !s.px, yP = p.y && !s.py, mP = p.menu && !s.pm;
  s.pa = p.a, s.pb = p.b, s.px = p.x, s.py = p.y, s.pm = p.menu;
  const bool aE = aP || p.poke[0], bE = bP || p.poke[1], xE = xP || p.poke[2], yE = yP || p.poke[3], mE = mP || p.poke[4];
  for (int k = 0; k < 5; ++k)
    if (p.poke[k]) ++g_counts.pokes;
  bool trigPick[2] = {false, false};
  for (int h = 0; h < 2; ++h) {
    if (!s.trigDown[h] && p.trig[h] > 0.55f) {
      s.trigDown[h] = true;
      trigPick[h] = !p.trigOnScreen[h];  // (a pointer on the screen: that trigger is its click - never twice)
    } else if (s.trigDown[h] && p.trig[h] < 0.3f) {
      s.trigDown[h] = false;
    }
  }
  const bool pick = aE || xE || trigPick[0] || trigPick[1];
  const bool back = bE || yE;
  const char* before = screen_name();
  if (mE) {  // MENU: the game picker from anywhere (on the picker: back where it came from); r205: twice within 0.6 s = let go
    const bool twice = p.now - s.lastMenu < kMenuTwice;
    s.lastMenu = twice ? -1e9 : p.now;
    ++g_counts.pickers;
    if (xrcasino::screen() == xrcasino::kPicker) {
      xrbook::Input in;
      in.b = true;
      xrbook::input(in, true);
      say("sportsbook: input picker (MENU%s) on the GAME PICKER -> back to %s", p.poke[4] ? ", poked" : "", screen_name());
    } else if (xrcasino::spinning()) {
      say("sportsbook: input picker (MENU) on %s - waits: a spin is running", before);
    } else if (!twice) {
      xrcasino::open_picker();
      say("sportsbook: input picker (MENU%s) on %s -> %s", p.poke[4] ? ", poked" : "", before, screen_name());
    }
    ++g_counts.used;
    tick(0.4f, 0.03f);
    if (twice) control_off(kOffMenu);
    return;  // (one thing a frame)
  }
  if (!step && !pick && !back) return;
  xrbook::Input in;
  in.dir = dir;
  in.step = step;
  in.a = pick;
  in.b = back;
  const uint64_t key0 = xrbook::selection_key();
  const bool used = xrbook::input(in, true);
  const bool moved = xrbook::selection_key() != key0;  // r204: did the selection really go somewhere
  const char* after = screen_name();
  if (used) ++g_counts.used;
  if (step) {
    ++g_counts.moves;
    if (src == 2) {
      ++g_counts.tiltMoves;
      g_tiltUsed = true;
    }
    if (moved && !pick && !back) ++g_counts.changed;
    const bool pulled = !moved && used && xrcasino::screen() == xrcasino::kSlots && dir == 4;  // (slots: back = a pull)
    say("sportsbook: input move %s (%s) on %s%s", kDirName[dir], kSrcName[src], before,
        moved ? "" : (pulled ? " - a pull (spin)" : " - nothing that way"));
    if (moved) tick(0.22f, 0.014f);  // (r204: only a real move ticks)
  }
  if (pick) {
    ++g_counts.picks;
    say("sportsbook: input pick (%s) on %s%s",
        aE ? (p.poke[0] ? "A button poked" : "A") : (xE ? (p.poke[2] ? "X button poked" : "X") : (trigPick[0] ? "left trigger" : "right trigger")),
        before, used ? "" : " - nothing to pick");
    tick(0.45f, 0.03f);
  }
  if (back) {
    ++g_counts.backs;
    say("sportsbook: input back (%s) on %s", bE ? (p.poke[1] ? "B button poked" : "B") : (p.poke[3] ? "Y button poked" : "Y"), before);
    tick(0.3f, 0.02f);
  }
  if (after != before) {
    if (xrcasino::screen() == xrcasino::kPicker) ++g_counts.pickers;
    say("sportsbook: input -> now %s", after);
  }
}

// ---------------------------------------------------------------------------------------------- r204: the ticket in a hand
namespace {
void v3(float* o, float x, float y, float z) { o[0] = x, o[1] = y, o[2] = z; }
float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
void cross3(const float* a, const float* b, float* o) {
  float r[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
  v3(o, r[0], r[1], r[2]);
}
bool norm3(float* a) {
  const float l = std::sqrt(dot3(a, a));
  if (l < 1e-6f) return false;
  a[0] /= l, a[1] /= l, a[2] /= l;
  return true;
}
// a minus its part along unit n (then normalized; false when nothing is left)
bool square_to(float* a, const float* n) {
  const float k = dot3(a, n);
  a[0] -= k * n[0], a[1] -= k * n[1], a[2] -= k * n[2];
  return norm3(a);
}
}  // namespace

bool ticket_grab(Hold& h, const float (*pts)[3], int n, const float side[3], const float hand[3], float reach) {
  if (n < 1) return false;
  float best = 1e30f, bestU = 0.0f;
  for (int i = 0; i < n; ++i) {  // the nearest point on its middle line (segment by segment)
    const float* a = pts[i];
    const float* b = pts[i + 1];
    const float ab[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, ah[3] = {hand[0] - a[0], hand[1] - a[1], hand[2] - a[2]};
    const float l2 = dot3(ab, ab);
    const float t = l2 > 1e-10f ? std::max(0.0f, std::min(1.0f, dot3(ah, ab) / l2)) : 0.0f;
    const float q[3] = {a[0] + ab[0] * t - hand[0], a[1] + ab[1] * t - hand[1], a[2] + ab[2] * t - hand[2]};
    const float d2 = dot3(q, q);
    if (d2 < best) best = d2, bestU = (static_cast<float>(i) + t) / static_cast<float>(n);
  }
  if (best > reach * reach) return false;
  h = Hold();
  h.grabU = bestU;
  float d[3] = {pts[n][0] - pts[0][0], pts[n][1] - pts[0][1], pts[n][2] - pts[0][2]};
  if (!norm3(d)) v3(d, 0.0f, -1.0f, 0.0f);
  float sd[3] = {side[0], side[1], side[2]};
  if (!square_to(sd, d)) v3(sd, 1.0f, 0.0f, 0.0f);
  float nn[3];
  cross3(d, sd, nn);  // (book_paper_pose's: n = along x across - the printed side)
  if (!norm3(nn)) v3(nn, 0.0f, 0.0f, 1.0f);
  for (int k = 0; k < 3; ++k) h.n[k] = nn[k], h.d[k] = d[k];
  h.init = true;
  return true;
}

void ticket_hold(Hold& h, const float grip[3], const float gx[3], const float gy[3], const float gz[3], const float eye[3], float L,
                 float dt, float (*out)[3], int n, float side[3]) {
  // where the fingers have it: 4.5 cm out in front of the grip
  const float A[3] = {grip[0] + gz[0] * 0.045f, grip[1] + gz[1] * 0.045f, grip[2] + gz[2] * 0.045f};
  // its printed side to your eyes (the middle of what's held, not the fingers: a long slip faces you as a whole)
  const float mid = (0.5f - h.grabU) * L;
  float nT[3] = {eye[0] - (A[0] + h.d[0] * mid), eye[1] - (A[1] + h.d[1] * mid), eye[2] - (A[2] + h.d[2] * mid)};
  if (!norm3(nT)) v3(nT, -gz[0], -gz[1], -gz[2]);
  // down its length = down as you see it, a little of the wrist's roll in it (the controller's own down)
  float dT[3] = {-gy[0] * 0.3f, -1.0f - gy[1] * 0.3f, -gy[2] * 0.3f};
  if (!square_to(dT, nT)) {  // (looking straight down on it: the hand's own forward for "down" on the page)
    v3(dT, gz[0], gz[1], gz[2]);
    if (!square_to(dT, nT)) v3(dT, 0.0f, -1.0f, 0.0f);
  }
  if (!h.init) {
    for (int k = 0; k < 3; ++k) h.n[k] = nT[k], h.d[k] = dT[k];
    h.init = true;
  }
  // smoothly (a 0.12 s ease): a hand's jitter never shakes the page
  const float e = 1.0f - std::exp(-std::max(0.0f, dt) / 0.12f);
  for (int k = 0; k < 3; ++k) {
    h.n[k] += (nT[k] - h.n[k]) * e;
    h.d[k] += (dT[k] - h.d[k]) * e;
  }
  if (!norm3(h.n)) v3(h.n, nT[0], nT[1], nT[2]);
  if (!square_to(h.d, h.n)) v3(h.d, dT[0], dT[1], dT[2]);
  cross3(h.n, h.d, side);  // (text left -> right as you face it)
  norm3(side);
  // stiff card: straight along d, its ends curling gently away from you (1.2 cm at 20 cm from the fingers)
  constexpr float kCurl = 0.3f;
  for (int j = 0; j <= n; ++j) {
    const float s = (static_cast<float>(j) / static_cast<float>(n) - h.grabU) * L;
    const float back = kCurl * s * s;
    for (int k = 0; k < 3; ++k) out[j][k] = A[k] + h.d[k] * s - h.n[k] * back;
  }
}

float ticket_press(Hold& h, float dist, float facing, float dt) {
  if (dist <= kPostDist && facing >= kPostFacing) h.pressT += std::max(0.0f, dt);
  else h.pressT = std::max(0.0f, h.pressT - 3.0f * std::max(0.0f, dt));
  return std::min(1.0f, h.pressT / kPostHold);
}

}  // namespace xrmachine
