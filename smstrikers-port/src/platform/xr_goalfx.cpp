// See xr_goalfx.h. Built with xr_quest.cpp in strikers_xr (C++20, no port prelude).
//
// How one goes off (Show): it starts at the goal spot O (where the ball went in: on the big screen's picture, on the
// Tabletop field's goal, at the life-size stadium's goal; a preview: the shop's spot), sized there by the goal unit g
// (a ninth of the big screen's width, the Tabletop's goal, the real goal); a wave rolls from O through you at the
// catalogue's speed (its arrival time `arrive` = when it reaches your head), and the room's beats count from then -
// the room sized by its unit u (your room 1, the cup suite 1.35, the life-size stadium 3) round your head H, between
// the floor and the top. Every explosion is a table of beats (Beat: when, which emitter, where, how many ...): the same
// emitters for all twelve, each look a table. tools/outside/goalfx153.py reads these tables straight out of this file
// for the PC proofs (one source of truth): keep the B(...) rows one a line.
//
// Its pieces are its own (not the celebration's: xr_fx's budget and pieces stay as they were), drawn by xr_fx.cpp with
// the celebration's shaders (write()): at most kMaxParts x the effects' load scale (xrfx::load_scale(), lowered while
// the headset misses frames) - every count in the tables is scaled by it too.

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#define VK_USE_PLATFORM_ANDROID_KHR 1
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include "xr_goalfx.h"

#include "xr_fx.h"
#include "xr_home.h"
#include "xr_net.hpp"
#include "xr_room.h"  // r197: your room scan's collision model (xrfx::Scene::room)

#include <android/log.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

// xr_guest_voice.cpp: a mono 32 kHz clip at the game's voice volume x volume
void PortCardSound(const short* pcm, int frames, float volume);

#define GLOG(...) __android_log_print(ANDROID_LOG_INFO, "strikers_xr", __VA_ARGS__)

namespace xrgoalfx {
namespace {

// ------------------------------------------------------------------------------------------------------------------
// Looks: xr_fx.frag's quad shapes (xr_fx.cpp's Shape ids), the goal explosions' own (r153, xr_fx.frag 140-149), and
// the celebration's 3D pieces (200+: xr_fx meshes by their celebration shape, mesh_look)
// ------------------------------------------------------------------------------------------------------------------
constexpr int kShPaper = 0, kShCoin = 1, kShStar = 3, kShPetal = 4, kShSpark = 13, kShMetal = 15, kShFlash = 16;
constexpr int kShPuff = 29, kShStreak = 30, kShBolt = 31, kShRing = 32, kShHex = 37, kShFlake = 39, kShDrop = 70;
constexpr int kShWisp = 73, kShBoltIcon = 75, kShFlame = 76, kShTwinkle = 77;
constexpr int kShBomb = 140, kShSmoke = 141, kShPortal = 142, kShChomper = 143, kShColumn = 144, kShCard = 145;
constexpr int kShShard = 146, kShPixel = 147, kShWave = 148, kShStem = 149;
constexpr int kShFirePuff = 190;  // a fog puff that glows (fire, nebulae): shape 29, lit (not a 3D piece: under 200)
constexpr int kMxPaper = 200, kMxFoil = 201, kMxStreamer = 202, kMxCube = 203, kMxIce = 204, kMxIcicle = 205;
constexpr int kMxShard = 206, kMxRock = 207, kMxStar = 208, kMxCoin = 209, kMxGem = 210, kMxLeaf = 211;
// r161: the room takeovers' own quads (xr_fx.frag 150-158): a pixel "?" block, a pixel alien (its sprite and frame in
// colour 2's alpha), a shark's fin, a dance-floor tile, a mirror ball, water, a meteor's crater, a black hole, a light
// cone; the celebration's spiral, bubble; and its fish (3D, xr_fx mesh kFishPiece)
constexpr int kShQBlock = 150, kShInvader = 151, kShFin = 152, kShTile = 153, kShBall = 154, kShWater = 155;
constexpr int kShCrater = 156, kShHole = 157, kShCone = 158, kShSpiral = 33, kShBubble = 34;
constexpr int kMxFish = 212;

// A pixel quad's look (colour 2's alpha in xr_fx.frag 147): a block, an 8-bit burst, a stepped ring
constexpr uint8_t kPixBlock = 40, kPixBurst = 128, kPixRing = 220;

// Colours: 0xRRGGBB, or PAL(n): a colour from palette n for each piece
constexpr uint32_t kPal = 0x1000000u;
#define PAL(n) (kPal | static_cast<uint32_t>(n))
enum PalId { kPalConfetti = 1, kPalRainbow, kPalPixel, kPalCards, kPalIce, kPalStars, kPalFire, kPalGold, kPalCardEdge };
const std::vector<uint32_t>& palette(int n) {
  static const std::vector<uint32_t> kP[] = {
      {0xFFFFFF},
      {0xE83A3A, 0xFFD21F, 0x2FCB5A, 0x2F8BFF, 0xFF5FA8, 0x9B5BFF, 0xFF8A1F, 0x1FD8D0},  // confetti
      {0xFF3B3B, 0xFF9A1F, 0xFFE53B, 0x4BE36A, 0x3BC8FF, 0x6B6BFF, 0xC85BFF, 0xFF5FC8},  // rainbow
      {0xE52521, 0xFFD800, 0x2ECC40, 0x00B4FF, 0xB14AED, 0xFF7A00, 0xFFFFFF},            // pixel
      {0x3A7BFF, 0xE83A3A, 0x2FCB5A, 0xFFD21F, 0x9B5BFF, 0xFF5FA8, 0x1FD8D0, 0xF2F2F2},  // cards' faces
      {0xE8FBFF, 0xBFF0FF, 0x8FDCFF, 0xFFFFFF, 0x6FC8FF},                                // ice
      {0xFFFFFF, 0xDDE8FF, 0xFFF2C8, 0xC8D8FF, 0xFFE0F0},                                // stars
      {0xFFE070, 0xFFB030, 0xFF7A1A, 0xFF4A10},                                          // fire
      {0xFFE38A, 0xFFD34A, 0xFFF4C8, 0xE8B030},                                          // gold
      {0xD8DCE6, 0xC0C8FF, 0xFFD34A, 0x26262C},  // the cards' edges: BASE chrome, SILVER TIZM, GOLD TIZM, BLACK TIZM
  };
  return kP[(n >= 1 && n <= 9) ? n : 0];
}

// ------------------------------------------------------------------------------------------------------------------
// The beats. Base: from the goal (kT0) or from when the wave reaches you (kTA). At: where (the goal spot O; your head;
// the floor under you; the floor spot in front of you between you and the goal; the floor spot front-right of you; the
// top of the room over you). Sizes and speeds in the anchor's unit: g at the goal, u everywhere else.
//
//   emitter     n            dur          look   size              speed             life        a                 b
//   kFlash      cores        -            yes    core size         -                 life        glow              growth (x size)
//   kBurst      pieces       spread over  yes    piece size        speed             life        drag (1/s)        gravity share
//   kRings      rings        spaced over  yes    final size        scatter radius    life        0 flat, 1 facing  rise speed
//   kRays       rays         -            yes    length            -                 life        width (x length)  -
//   kBolts      bolts        -            -      length            -                 life        width             -
//   kSweep      -            -            yes    size round you    -                 -           floor wave (1)    -
//   kStreaks    streaks      over         yes    length            speed             -           miss radius       -
//   kVeil       -            -            -      glow (0 dims)     -                 life        peak              fade in (s)
//   kRain       pieces       over         yes    piece size        fall speed        life        flutter           gravity share (0 floats down)
//   kRise       pieces       over         yes    piece size        rise speed        life        wobble            -
//   kFountain   pieces       over         yes    piece size        flight time (s)   life        lands within      gravity share
//   kStrikes    strikes      over         -      bolt width        -                 -           flash             0 anywhere, 1 the columns, 2 in front
//   kShell      pieces       -            yes    piece size        outward speed     life        radius            orbit (rad/s)
//   kWhirl      pieces       over         yes    piece size        rise speed        life        radius            turn (rad/s)
//   kFireRing   flames       -            yes    flame height      sweep speed       life        start radius      -
//   kJets       sparks       over         yes    spark size        up speed          spark life  radius            jets
//   kColumns    columns      -            -      height            rise time (s)     life        radius            width
//   kPlant      -            -            -      height            -                 life        head size         -
//   kBomb       -            flight (s)   -      bomb size         -                 until boom  arc height        -
//   kShatter    shards       -            -      shard size        fall-away speed   life        radius            hold (s)
//   kGlitter    twinkles     over         yes    size              -                 life        -                 -
//   kPops       pixel bursts over         -      size              -                 life        -                 -
//   kAirBursts  bursts       over         yes    piece size        speed             piece life  pieces each       -
//   kComets     streaks      over         yes    length            speed             life        -                 -
//   kPortal     -            -            -      size              spin (turns/s)    life        1 overhead        -
//   kPuffs      puffs        -            yes    size              rise speed        life        peak              spread
//   kBuzz       -            -            -      -                 -                 -           strength          seconds
//   kSound      clip         -            -      -                 -                 -           gain              -
//   kTakeover   which        room (s)     -      -                 -                 -           -                 -
// ------------------------------------------------------------------------------------------------------------------
enum Base : uint8_t { kT0, kTA };
// r207: + kAtCab (the TIZM machine's cabinet top), kAtAbove (the air ~0.85 m over it) - the CASINO WIN PARTY's anchors (its
// kAtGoal is the machine's payout tray)
enum At : uint8_t { kAtGoal, kAtHead, kAtFloor, kAtFront, kAtSide, kAtTop, kAtCab, kAtAbove };
// r207: + the CASINO WIN PARTY's emitters (casino_party): kCasCoins (coins fountaining out of the tray onto your floor: n over dur,
// size, speed out, life), kCasConfetti (a cannon-load up out of the cabinet's top that flutters down: n, size, speed up, life),
// kCasFirework (a shell bursting over the machine: n shells over dur, a = sparks each, size), kCasSiren (the siren light out on
// its top + its two turning beams: life), kCasLetters (the giant 3D letters rising over it, facing you: size = cap height, life),
// kCasPulse (the room's gold flash: n pulses over dur, a = their peak)
enum Em : uint8_t {
  kFlash, kBurst, kRings, kRays, kBolts, kSweep, kStreaks, kVeil, kRain, kRise, kFountain, kStrikes, kShell, kWhirl,
  kFireRing, kJets, kColumns, kPlant, kBomb, kShatter, kGlitter, kPops, kAirBursts, kComets, kPortal, kPuffs, kBuzz,
  kSound, kTakeover, kCasCoins, kCasConfetti, kCasFirework, kCasSiren, kCasLetters, kCasPulse
};
// r161: kTakeover (pack 2): n = which room takeover (Takeover below), dur = how long it has your room (s); the room's
// own code (room_step) runs it from then on - it reads your hands every frame
enum Takeover : uint8_t { kToNone, kToBits, kToInvaders, kToMeteor, kToHole, kToFlood, kToFlip, kToShark, kToDisco };
struct Beat {
  float t;
  Base base;
  Em em;
  At at;
  int n;
  float dur;
  int shape;
  float size, speed, life;
  uint32_t c1, c2;
  float a, b;
};
#define B(t, base, em, at, n, dur, shape, size, speed, life, c1, c2, a, b) \
  Beat { t, base, em, at, n, dur, shape, size, speed, life, c1, c2, a, b }

// r155 (Lance: "i REALLY want the rocket league shit"): every explosion rebuilt as a Rocket League goal explosion - the
// GOAL does the work, all at once: a blinding flash, a fat fireball rolling out (glowing puffs flung out and braked hard),
// a hard shockwave dome, rays, a shower of spark trails, heavy debris in its theme thrown with real weight, then smoke
// rolling up; the wave blasts through you (a short hit, a floor ring, a few of its pieces) and it's over in ~2.5 s.
// The core rows are the same in all twelve (its colours and debris its own), its set piece after them.

// ---- CLASSIC BURST (free): a white-gold detonation, gold foil and spark trails, a gold shockwave through you
const Beat kFxClassic[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.60f, 0.00f, 0.20f, 0xFFFFFF, 0xFFD040, 1.00f, 1.20f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 5.20f, 0.00f, 0.14f, 0xFFFFFF, 0xFFE9A0, 1.00f, 0.40f),
    B(0.00f, kT0, kBurst, kAtGoal, 35, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.83f, 0xFFF4C8, 0xFFB020, 6.00f, -0.10f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShFirePuff, 3.52f, 4.38f, 1.27f, 0xFFD060, 0xE08A10, 4.00f, -0.15f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.30f, 0xFFFFFF, 0xFFD040, 1.00f, 0.00f),
    B(0.05f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 7.00f, 0.00f, 0.40f, 0xFFF4D0, 0x5AC8FF, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 14, 0.00f, kShStreak, 4.50f, 0.00f, 0.22f, 0xFFFFFF, 0xFFC830, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 70, 0.00f, kShStreak, 0.35f, 10.0f, 1.10f, 0xFFFFFF, 0xFFC830, 1.20f, 0.50f),
    B(0.00f, kT0, kBurst, kAtGoal, 30, 0.00f, kMxFoil, 0.14f, 6.00f, 1.80f, PAL(kPalGold), 0xC8901E, 0.50f, 1.00f),
    B(0.12f, kT0, kPuffs, kAtGoal, 10, 0.00f, kShPuff, 2.80f, 0.55f, 2.16f, 0x6A6A70, 0x3A3A40, 0.50f, 1.10f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.00f, 0.00f, 0.00f, 0xFFF4D0, 0xFFC830, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.22f, 0xFFE9B0, 0, 0.25f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.35f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.60f, 0xFFF4D0, 0xFFC830, 0.00f, 0.00f),
    B(0.05f, kTA, kRain, kAtTop, 90, 1.00f, kMxFoil, 0.035f, 0.50f, 1.80f, PAL(kPalGold), 0xC8901E, 0.35f, 0.00f),
};

// ---- CONFETTI CANNON: a white blast cloud and a cannon-load of confetti and streamers fired out of the goal
const Beat kFxConfetti[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.20f, 0.00f, 0.18f, 0xFFFFFF, 0xFFE0F0, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 32, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.83f, 0xFFFFFF, 0xFF9AD0, 6.00f, -0.10f),
    B(0.02f, kT0, kBurst, kAtGoal, 12, 0.00f, kShPuff, 3.40f, 4.20f, 1.44f, 0xFFFFFF, 0xE0E0E8, 4.00f, -0.10f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.30f, 0xFFFFFF, 0xFF5FA8, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 12, 0.00f, kShStreak, 4.00f, 0.00f, 0.20f, 0xFFFFFF, 0xFFD21F, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 220, 0.00f, kMxPaper, 0.11f, 8.00f, 2.20f, PAL(kPalConfetti), PAL(kPalConfetti), 1.40f, 0.30f),
    B(0.00f, kT0, kBurst, kAtGoal, 18, 0.00f, kMxStreamer, 0.55f, 7.00f, 2.20f, PAL(kPalConfetti), PAL(kPalConfetti), 1.40f, 0.30f),
    B(0.00f, kT0, kBurst, kAtGoal, 40, 0.00f, kShStreak, 0.30f, 9.00f, 0.90f, 0xFFFFFF, PAL(kPalConfetti), 1.40f, 0.40f),
    B(0.10f, kT0, kPuffs, kAtGoal, 8, 0.00f, kShPuff, 2.60f, 0.50f, 1.80f, 0xF4F4F8, 0xC8C8D0, 0.45f, 1.00f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.00f, 0.00f, 0.00f, 0xFFFFFF, 0xFF5FA8, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.80f, 0.00f, 0.20f, 0xFFD0E8, 0, 0.25f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.80f, 0.25f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 5.50f, 0.00f, 0.60f, 0xFFFFFF, 0xFF5FA8, 0.00f, 0.00f),
    B(0.05f, kTA, kRain, kAtTop, 260, 1.20f, kMxPaper, 0.045f, 0.55f, 2.00f, PAL(kPalConfetti), PAL(kPalConfetti), 0.38f, 0.00f),
};

// ---- PIXEL POP: an 8-bit detonation - a blocky fireball of pixels, a stepped shockwave, cubes and coins flung out
const Beat kFxPixel[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShPixel, 3.60f, 0.00f, 0.40f, 0xFFFFFF, 0xFFD800, 0.40f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 70, 0.00f, kShPixel, 0.40f, 7.00f, 0.60f, PAL(kPalPixel), 0xFFFFFF, 6.00f, -0.10f),
    B(0.03f, kT0, kBurst, kAtGoal, 40, 0.00f, kShPixel, 0.55f, 3.50f, 0.85f, 0xFF7A00, 0xE52521, 4.00f, -0.10f),
    B(0.00f, kT0, kRings, kAtGoal, 2, 0.08f, kShPixel, 9.00f, 0.00f, 0.35f, PAL(kPalPixel), 0xFFFFFF, 1.00f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 40, 0.00f, kMxCube, 0.15f, 7.00f, 1.80f, PAL(kPalPixel), 0x202020, 0.40f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 16, 0.00f, kMxCoin, 0.13f, 6.00f, 1.80f, 0xFFD21F, 0xC8901E, 0.40f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 50, 0.00f, kShPixel, 0.09f, 10.0f, 1.00f, PAL(kPalPixel), 0xFFFFFF, 1.40f, 0.50f),
    B(0.12f, kT0, kPuffs, kAtGoal, 8, 0.00f, kShPuff, 2.60f, 0.50f, 1.80f, 0x5A5A66, 0x30303A, 0.45f, 1.00f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShPixel, 3.00f, 0.00f, 0.00f, 0x00B4FF, 0xFFFFFF, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.18f, 0x00B4FF, 0, 0.25f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.80f, 0.20f),
    B(0.00f, kTA, kRings, kAtFloor, 2, 0.15f, kShPixel, 5.50f, 0.00f, 0.60f, PAL(kPalPixel), 0xFFFFFF, 0.00f, 0.00f),
    B(0.05f, kTA, kPops, kAtHead, 8, 0.70f, kShPixel, 0.80f, 0.00f, 0.45f, PAL(kPalPixel), 0xFFD800, 0.00f, 0.00f),
    B(0.10f, kTA, kRain, kAtTop, 40, 0.80f, kMxCoin, 0.065f, 0.80f, 1.80f, 0xFFD21F, 0xC8901E, 0.00f, 0.50f),
};

// ---- BOB-OMB BLAST: the goal pops, a fizzing bomb hops out onto your floor in front of you ... and goes off there: a
// full fireball, shock dome and debris right in front of you
const Beat kFxBomb[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 2.20f, 0.00f, 0.20f, 0xFFF0C0, 0xFFB040, 1.00f, 0.60f),
    B(0.00f, kT0, kBurst, kAtGoal, 16, 0.00f, kShFirePuff, 1.39f, 9.38f, 0.68f, 0xFFE070, 0xFF6A10, 6.00f, -0.10f),
    B(0.00f, kT0, kPuffs, kAtGoal, 5, 0.00f, kShPuff, 2.00f, 0.40f, 1.08f, 0xE0E0E4, 0x9A9AA2, 0.50f, 0.50f),
    B(0.05f, kT0, kBomb, kAtFront, 1, 0.70f, 0, 0.42f, 0.00f, 1.30f, 0x1A1C26, 0xFF3020, 0.70f, 0.00f),
    B(1.35f, kT0, kSound, kAtFront, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(1.35f, kT0, kFlash, kAtFront, 1, 0.00f, kShSpark, 3.00f, 0.00f, 0.20f, 0xFFFFFF, 0xFF9A2A, 1.00f, 1.20f),
    B(1.35f, kT0, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.25f, 0xFFB060, 0, 0.25f, 0.01f),
    B(1.35f, kT0, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.50f),
    B(1.35f, kT0, kBurst, kAtFront, 26, 0.00f, kShFirePuff, 0.88f, 4.80f, 0.77f, 0xFFF0B0, 0xFF6A10, 6.00f, -0.10f),
    B(1.37f, kT0, kBurst, kAtFront, 14, 0.00f, kShFirePuff, 1.28f, 2.64f, 1.19f, 0xFFB040, 0xC03A08, 4.00f, -0.15f),
    B(1.35f, kT0, kRings, kAtFront, 1, 0.00f, kShRing, 4.50f, 0.00f, 0.30f, 0xFFFFFF, 0xFF9A2A, 1.00f, 0.00f),
    B(1.35f, kT0, kRings, kAtFront, 1, 0.00f, kShSmoke, 4.60f, 0.00f, 1.20f, 0xD8D8DC, 0x8A8A92, 0.00f, 0.00f),
    B(1.35f, kT0, kRays, kAtFront, 14, 0.00f, kShStreak, 1.60f, 0.00f, 0.20f, 0xFFFFFF, 0xFFD080, 0.035f, 0.00f),
    B(1.35f, kT0, kBurst, kAtFront, 70, 0.00f, kShStreak, 0.16f, 5.00f, 1.10f, 0xFFF0B0, 0xFF7A1A, 1.20f, 0.60f),
    B(1.35f, kT0, kBurst, kAtFront, 26, 0.00f, kMxRock, 0.070f, 3.20f, 1.80f, 0x3A3530, 0x1E1A18, 0.30f, 1.00f),
    B(1.45f, kT0, kPuffs, kAtFront, 12, 0.00f, kShPuff, 1.20f, 0.40f, 2.04f, 0x5A5A60, 0x2E2E34, 0.60f, 0.60f),
};

// ---- ICE SHATTER: the goal detonates in a frozen blast - an icy fireball, a frost shockwave, ice chunks and shards
const Beat kFxIce[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 5.00f, 0.00f, 0.18f, 0xFFFFFF, 0x8FDCFF, 1.00f, 0.40f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.20f, 0.00f, 0.20f, 0xFFFFFF, 0xBFF0FF, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 35, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.90f, 0xF4FDFF, 0x6FC8FF, 6.00f, -0.05f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShPuff, 3.40f, 4.20f, 1.44f, 0xFFFFFF, 0xBFE0FF, 4.00f, -0.05f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShHex, 10.0f, 0.00f, 0.35f, 0xE8FBFF, 0x6FC8FF, 1.00f, 0.00f),
    B(0.04f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 7.00f, 0.00f, 0.40f, 0xFFFFFF, 0x8FDCFF, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 12, 0.00f, kShStreak, 4.20f, 0.00f, 0.22f, 0xFFFFFF, 0x8FDCFF, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 34, 0.00f, kMxIce, 0.18f, 7.00f, 1.80f, PAL(kPalIce), 0xFFFFFF, 0.40f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 30, 0.00f, kMxShard, 0.14f, 8.00f, 1.60f, PAL(kPalIce), 0xFFFFFF, 0.50f, 0.90f),
    B(0.00f, kT0, kBurst, kAtGoal, 60, 0.00f, kShStreak, 0.30f, 10.0f, 0.90f, 0xFFFFFF, 0x8FDCFF, 1.30f, 0.40f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.20f, 0.00f, 0.00f, 0xE8FBFF, 0x6FC8FF, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.70f, 0.00f, 0.30f, 0xBFE8FF, 0, 0.25f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.30f),
    B(0.00f, kTA, kRings, kAtFloor, 8, 0.25f, kShFlake, 0.90f, 1.60f, 1.80f, 0xF4FDFF, 0x8FDCFF, 0.00f, 0.00f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.60f, 0xE8FBFF, 0x6FC8FF, 0.00f, 0.00f),
    B(0.05f, kTA, kRain, kAtTop, 120, 1.20f, kShFlake, 0.035f, 0.40f, 1.80f, 0xFFFFFF, 0xBFE8FF, 0.25f, 0.00f),
};

// ---- THUNDERSTRUCK: a plasma detonation - an electric-blue fireball, bolts ripping out of it, a crackling shock dome
const Beat kFxThunder[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.60f, 0.00f, 0.18f, 0xFFFFFF, 0x6FB8FF, 1.00f, 1.20f),
    B(0.00f, kT0, kBolts, kAtGoal, 9, 0.00f, 0, 4.20f, 0.00f, 0.30f, 0xFFFFFF, 0x6FB8FF, 0.14f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 35, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.75f, 0xE8F4FF, 0x3A8CFF, 6.00f, -0.05f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShFirePuff, 3.52f, 4.38f, 1.20f, 0x9FD0FF, 0x2A50C0, 4.00f, -0.10f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.28f, 0xFFFFFF, 0x3A8CFF, 1.00f, 0.00f),
    B(0.06f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 7.00f, 0.00f, 0.35f, 0xE8F4FF, 0x6FB8FF, 1.00f, 0.00f),
    B(0.10f, kT0, kBolts, kAtGoal, 5, 0.00f, 0, 3.40f, 0.00f, 0.25f, 0xFFFFFF, 0x9FD0FF, 0.11f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 80, 0.00f, kShStreak, 0.35f, 11.0f, 0.90f, 0xFFFFFF, 0x6FB8FF, 1.40f, 0.40f),
    B(0.00f, kT0, kBurst, kAtGoal, 18, 0.00f, kMxRock, 0.12f, 6.00f, 1.60f, 0x2A2E3A, 0x14161E, 0.40f, 1.00f),
    B(0.12f, kT0, kPuffs, kAtGoal, 10, 0.00f, kShPuff, 2.80f, 0.55f, 2.04f, 0x3A4050, 0x1A1E28, 0.55f, 1.10f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.40f, 0.00f, 0.00f, 0xE8F4FF, 0x3A8CFF, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.20f, 0xC8E0FF, 0, 0.25f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.30f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.55f, 0xE8F4FF, 0x3A8CFF, 0.00f, 0.00f),
    B(0.10f, kTA, kStrikes, kAtHead, 3, 0.60f, 0, 0.12f, 0.00f, 0.00f, 0xFFFFFF, 0x7FC0FF, 0.35f, 0.00f),
};

// ---- INFERNO RING: the big one in fire - a huge orange fireball rolling out of the goal, black smoke, burning debris,
// and a ring of flame bursting out across your floor as the wave hits you
const Beat kFxInferno[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.80f, 0.00f, 0.20f, 0xFFFFFF, 0xFFB040, 1.00f, 1.20f),
    B(0.00f, kT0, kBurst, kAtGoal, 40, 0.00f, kShFirePuff, 1.85f, 14.07f, 0.90f, 0xFFF0B0, 0xFF7A1A, 6.00f, -0.15f),
    B(0.02f, kT0, kBurst, kAtGoal, 18, 0.00f, kShFirePuff, 3.96f, 4.75f, 1.42f, 0xFFB040, 0xD03A08, 4.00f, -0.25f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.30f, 0xFFF0B0, 0xFF4A10, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 14, 0.00f, kShStreak, 4.50f, 0.00f, 0.22f, 0xFFFFFF, 0xFFA030, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 30, 0.00f, kShFlame, 0.40f, 4.00f, 0.80f, PAL(kPalFire), 0xFF3A10, 2.00f, -0.30f),
    B(0.00f, kT0, kBurst, kAtGoal, 80, 0.00f, kShStreak, 0.35f, 10.0f, 1.20f, 0xFFF0B0, 0xFF5A10, 1.20f, 0.50f),
    B(0.00f, kT0, kBurst, kAtGoal, 26, 0.00f, kMxRock, 0.13f, 6.50f, 1.80f, 0x3A2A20, 0xFF5A10, 0.40f, 1.00f),
    B(0.15f, kT0, kPuffs, kAtGoal, 12, 0.00f, kShPuff, 3.20f, 0.70f, 2.40f, 0x3A3436, 0x1A1618, 0.65f, 1.20f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.20f, 0.00f, 0.00f, 0xFFD070, 0xFF4A10, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.30f, 0xFF8A30, 0, 0.25f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.45f),
    B(0.00f, kTA, kFireRing, kAtFloor, 40, 0.00f, kShFlame, 0.60f, 2.20f, 1.30f, PAL(kPalFire), 0xFF3A10, 0.40f, 0.00f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.60f, 0xFFB040, 0xFF3A10, 0.00f, 0.00f),
    B(0.05f, kTA, kRise, kAtFloor, 120, 1.00f, kShSpark, 0.030f, 0.80f, 1.30f, 0xFFE070, 0xFF5A10, 0.20f, 0.00f),
};

// ---- GALAXY WARP: a cosmic detonation - a violet star-fireball, a portal ripping open in the goal, star streaks
// rushing past you, your room flickering to deep space for a beat
const Beat kFxGalaxy[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 5.20f, 0.00f, 0.18f, 0xFFFFFF, 0xC08CFF, 1.00f, 0.40f),
    B(0.00f, kT0, kPortal, kAtGoal, 1, 0.00f, kShPortal, 3.60f, 0.90f, 1.10f, 0xE8D8FF, 0x6A3AD0, 0.00f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 35, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.90f, 0xF0E0FF, 0x7A3AE0, 6.00f, 0.00f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShFirePuff, 3.74f, 4.38f, 1.35f, 0xC050FF, 0x2030A0, 4.00f, 0.00f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.30f, 0xFFFFFF, 0x9B5BFF, 1.00f, 0.00f),
    B(0.05f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 7.00f, 0.00f, 0.40f, 0xE8D8FF, 0x3BC8FF, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 14, 0.00f, kShStreak, 4.50f, 0.00f, 0.22f, 0xFFFFFF, 0xC08CFF, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 80, 0.00f, kShTwinkle, 0.10f, 9.00f, 1.20f, PAL(kPalStars), 0xC08CFF, 1.40f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 16, 0.00f, kMxGem, 0.14f, 6.00f, 1.60f, 0xC08CFF, 0x3BC8FF, 0.50f, 0.80f),
    B(0.02f, kT0, kStreaks, kAtGoal, 50, 0.30f, kShStreak, 0.90f, 14.0f, 0.00f, 0xFFFFFF, 0x8CC8FF, 1.60f, 0.00f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.20f, 0.00f, 0.00f, 0xD8B8FF, 0x5030C0, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.80f, 0x06040F, 0, 0.60f, 0.04f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.90f, 0.35f),
    B(0.00f, kTA, kShell, kAtHead, 260, 0.00f, kShSpark, 0.040f, 0.00f, 0.80f, PAL(kPalStars), 0x9FB8FF, 2.30f, 0.30f),
    B(0.70f, kTA, kShell, kAtHead, 80, 0.00f, kShTwinkle, 0.030f, 4.00f, 0.40f, 0xFFFFFF, 0xC08CFF, 1.20f, 0.00f),
};

// ---- PIRANHA CHOMP: a jungle detonation - a green blast of leaves and dirt out of the goal, then the giant chomper
// bursts up through your floor beside you and snaps
const Beat kFxChomp[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.40f, 0.00f, 0.18f, 0xF0FFE0, 0x7CFF6A, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 32, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.83f, 0xE8FFD0, 0x3ACB3A, 6.00f, -0.05f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShPuff, 3.20f, 4.20f, 1.44f, 0x9A7A5A, 0x5A4030, 4.00f, -0.05f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.30f, 0xE8FFD0, 0x3ACB3A, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 12, 0.00f, kShStreak, 4.00f, 0.00f, 0.20f, 0xFFFFFF, 0x7CFF6A, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 50, 0.00f, kMxLeaf, 0.14f, 8.00f, 2.00f, 0x6CD848, 0x2E8A2A, 1.20f, 0.40f),
    B(0.00f, kT0, kBurst, kAtGoal, 24, 0.00f, kMxRock, 0.12f, 6.50f, 1.80f, 0x6A4A30, 0x3A2A1A, 0.40f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 50, 0.00f, kShStreak, 0.30f, 9.00f, 0.90f, 0xFFFFFF, 0x7CFF6A, 1.30f, 0.40f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.00f, 0.00f, 0.00f, 0xB8FF90, 0x2E8A2A, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.90f, 0.40f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.25f, 0x90FF70, 0, 0.25f, 0.01f),
    B(0.05f, kTA, kRings, kAtSide, 1, 0.00f, kShSmoke, 2.20f, 0.00f, 0.80f, 0x5A3A20, 0x2A1A10, 0.00f, 0.00f),
    B(0.05f, kTA, kBurst, kAtSide, 22, 0.00f, kMxRock, 0.070f, 2.60f, 1.60f, 0x6A4A30, 0x3A2A1A, 0.30f, 1.00f),
    B(0.05f, kTA, kPuffs, kAtSide, 8, 0.00f, kShPuff, 0.60f, 0.40f, 1.20f, 0x8A6A4A, 0x5A4030, 0.55f, 0.30f),
    B(0.08f, kTA, kPlant, kAtSide, 1, 0.00f, 0, 1.85f, 0.00f, 2.75f, 0xE0262E, 0xFFFFFF, 0.95f, 0.00f),
};

// ---- KALEIDOSCOPE: a prism detonation - a fireball in every colour, a hex shockwave, mirror shards flung out of the
// goal, your room cracking into turning mirrors for a moment
const Beat kFxKaleido[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 5.40f, 0.00f, 0.18f, 0xFFFFFF, 0xFFFFFF, 1.00f, 0.40f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.40f, 0.00f, 0.20f, 0xFFFFFF, 0xC8A0FF, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 40, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.90f, PAL(kPalRainbow), 0xFFFFFF, 6.00f, 0.00f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShFirePuff, 3.52f, 4.38f, 1.27f, PAL(kPalRainbow), PAL(kPalRainbow), 4.00f, 0.00f),
    B(0.00f, kT0, kRings, kAtGoal, 3, 0.10f, kShHex, 10.0f, 0.00f, 0.35f, PAL(kPalRainbow), PAL(kPalRainbow), 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 16, 0.00f, kShStreak, 4.50f, 0.00f, 0.22f, 0xFFFFFF, PAL(kPalRainbow), 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 36, 0.00f, kMxShard, 0.15f, 8.00f, 1.80f, 0xE8F0FF, PAL(kPalRainbow), 0.50f, 0.90f),
    B(0.00f, kT0, kBurst, kAtGoal, 70, 0.00f, kShStreak, 0.32f, 10.0f, 1.00f, PAL(kPalRainbow), 0xFFFFFF, 1.30f, 0.40f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.20f, 0.00f, 0.00f, 0xFFFFFF, 0xC8A0FF, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.16f, 0xFFFFFF, 0, 0.25f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.25f),
    B(0.00f, kTA, kShatter, kAtHead, 40, 0.00f, kShShard, 0.60f, 1.30f, 1.30f, 0, 0, 1.35f, 0.35f),
    B(0.40f, kTA, kRain, kAtTop, 60, 0.60f, kMxShard, 0.060f, 2.20f, 1.30f, PAL(kPalRainbow), 0xFFFFFF, 0.00f, 0.80f),
};

// ---- CARDSTORM: a gold detonation that blows a whole case of LANCINI TIZM cards out of the goal, a whirl of them
// round you as the wave hits
const Beat kFxCardstorm[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.60f, 0.00f, 0.20f, 0xFFFFFF, 0xFFD34A, 1.00f, 1.20f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 5.00f, 0.00f, 0.14f, 0xFFF4C8, 0xFFD34A, 1.00f, 0.40f),
    B(0.00f, kT0, kBurst, kAtGoal, 35, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.83f, 0xFFF4C8, 0xFFB020, 6.00f, -0.10f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShFirePuff, 3.52f, 4.38f, 1.27f, 0xFFD34A, 0xB07CFF, 4.00f, -0.10f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.30f, 0xFFFFFF, 0xFFD34A, 1.00f, 0.00f),
    B(0.05f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 7.00f, 0.00f, 0.40f, 0xFFE38A, 0xB07CFF, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 14, 0.00f, kShStreak, 4.50f, 0.00f, 0.22f, 0xFFFFFF, 0xFFD34A, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 90, 0.00f, kShCard, 0.34f, 8.00f, 2.00f, PAL(kPalCards), PAL(kPalCardEdge), 1.00f, 0.30f),
    B(0.00f, kT0, kBurst, kAtGoal, 60, 0.00f, kShStreak, 0.32f, 10.0f, 1.00f, 0xFFFFFF, 0xFFD34A, 1.30f, 0.40f),
    B(0.12f, kT0, kPuffs, kAtGoal, 8, 0.00f, kShPuff, 2.60f, 0.50f, 1.80f, 0x6A6460, 0x3A3632, 0.45f, 1.00f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.00f, 0.00f, 0.00f, 0xFFE38A, 0xB07CFF, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.22f, 0xFFE38A, 0, 0.25f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.90f, 0.35f),
    B(0.00f, kTA, kWhirl, kAtHead, 140, 0.20f, kShCard, 0.15f, 0.30f, 1.50f, PAL(kPalCards), PAL(kPalCardEdge), 1.40f, 3.20f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.60f, 0xFFE38A, 0xB07CFF, 0.00f, 0.00f),
};

// ---- OLYMPUS: the gods' detonation - the biggest white-gold fireball, bolts tearing out of it, marble columns slamming
// up round you as the wave hits, bolts striking their tops
const Beat kFxOlympus[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 4.20f, 0.00f, 0.22f, 0xFFFFFF, 0xFFD34A, 1.00f, 1.30f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 6.00f, 0.00f, 0.16f, 0xFFFFFF, 0xFFF4C8, 1.00f, 0.40f),
    B(0.00f, kT0, kBolts, kAtGoal, 8, 0.00f, 0, 4.40f, 0.00f, 0.32f, 0xFFFFFF, 0xFFD34A, 0.14f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 43, 0.00f, kShFirePuff, 1.93f, 14.07f, 0.98f, 0xFFFFFF, 0xFFC830, 6.00f, -0.10f),
    B(0.02f, kT0, kBurst, kAtGoal, 18, 0.00f, kShFirePuff, 4.18f, 4.75f, 1.50f, 0xFFE38A, 0xE8B030, 4.00f, -0.15f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 11.0f, 0.00f, 0.32f, 0xFFFFFF, 0xFFB020, 1.00f, 0.00f),
    B(0.06f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 8.00f, 0.00f, 0.40f, 0xFFF4C8, 0xFFD34A, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 18, 0.00f, kShStreak, 5.00f, 0.00f, 0.24f, 0xFFFFFF, 0xFFD34A, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 90, 0.00f, kShStreak, 0.38f, 11.0f, 1.20f, 0xFFFFFF, 0xFFD34A, 1.20f, 0.50f),
    B(0.00f, kT0, kBurst, kAtGoal, 24, 0.00f, kMxRock, 0.15f, 6.50f, 1.90f, 0xF4EEE2, 0xC8C0B0, 0.40f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 24, 0.00f, kMxFoil, 0.13f, 6.00f, 1.90f, PAL(kPalGold), 0xC8901E, 0.50f, 1.00f),
    B(0.14f, kT0, kPuffs, kAtGoal, 10, 0.00f, kShPuff, 3.00f, 0.60f, 2.28f, 0x8A8478, 0x4A463E, 0.50f, 1.20f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.40f, 0.00f, 0.00f, 0xFFF4C8, 0xFFB020, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.26f, 0xFFE9B0, 0, 0.25f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.50f),
    B(0.00f, kTA, kColumns, kAtHead, 7, 0.00f, 0, 2.30f, 0.30f, 2.40f, 0xF4EEE2, 0xFFD34A, 1.90f, 0.42f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.60f, 0xFFF4C8, 0xFFB020, 0.00f, 0.00f),
    B(0.35f, kTA, kStrikes, kAtHead, 7, 0.70f, 0, 0.11f, 0.00f, 0.00f, 0xFFFFFF, 0xFFD34A, 0.30f, 1.00f),
};

// ------------------------------------------------------------------------------------------------------------------
// r161: GOAL EXPLOSIONS pack 2 - the ROOM TAKEOVERS. Each still goes off as r155's Rocket League detonation at the goal
// (flash, fireball, dome, rays, sparks, debris, smoke, the wave through you), then the wave reaches you and kTakeover
// hands your room to its own live code (room_step): things that move on their own and react to your hands and head,
// for ~6.6 s, then it clears (sooner the moment the match is live again).
// ------------------------------------------------------------------------------------------------------------------

// ---- 8-BIT TAKEOVER: an 8-bit detonation, then your room fills with floating ? blocks and pixel cubes - punch a block
// and it pops coins that fly to your hand, cubes bounce off your hands
const Beat kFxBits[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShPixel, 3.80f, 0.00f, 0.40f, 0xFFFFFF, 0xFFD800, 0.40f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.20f, 0.00f, 0.18f, 0xFFFFFF, 0x00B4FF, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 80, 0.00f, kShPixel, 0.42f, 7.50f, 0.65f, PAL(kPalPixel), 0xFFFFFF, 6.00f, -0.10f),
    B(0.03f, kT0, kBurst, kAtGoal, 40, 0.00f, kShPixel, 0.60f, 3.60f, 0.90f, 0xFFD800, 0xE52521, 4.00f, -0.10f),
    B(0.00f, kT0, kRings, kAtGoal, 3, 0.10f, kShPixel, 10.0f, 0.00f, 0.38f, PAL(kPalPixel), 0xFFFFFF, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 12, 0.00f, kShStreak, 4.20f, 0.00f, 0.20f, 0xFFFFFF, 0x00B4FF, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 44, 0.00f, kMxCube, 0.15f, 7.00f, 1.80f, PAL(kPalPixel), 0x202020, 0.40f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 20, 0.00f, kMxCoin, 0.13f, 6.50f, 1.80f, 0xFFD21F, 0xC8901E, 0.40f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 50, 0.00f, kShPixel, 0.09f, 10.0f, 1.00f, PAL(kPalPixel), 0xFFFFFF, 1.40f, 0.50f),
    B(0.12f, kT0, kPuffs, kAtGoal, 8, 0.00f, kShPuff, 2.60f, 0.50f, 1.80f, 0x5A5A66, 0x30303A, 0.45f, 1.00f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShPixel, 3.00f, 0.00f, 0.00f, 0x00B4FF, 0xFFFFFF, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 1.00f, 0.00f, 0.18f, 0x00B4FF, 0, 0.22f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.80f, 0.20f),
    B(0.00f, kTA, kRings, kAtFloor, 2, 0.15f, kShPixel, 5.50f, 0.00f, 0.60f, PAL(kPalPixel), 0xFFFFFF, 0.00f, 0.00f),
    B(0.05f, kTA, kTakeover, kAtHead, kToBits, 6.60f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.00f, 0.00f),
    B(0.30f, kTA, kGlitter, kAtHead, 50, 6.00f, kShPixel, 0.022f, 0.00f, 0.90f, PAL(kPalPixel), 0xFFFFFF, 0.00f, 0.00f),
};

// ---- PIXEL INVADERS: a neon 8-bit blast, the room goes dark, rows of pixel aliens march down round you - your trigger
// fires a laser from the controller, punch the ones that dive at you, a mothership last
const Beat kFxInvaders[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShPixel, 3.60f, 0.00f, 0.40f, 0xFFFFFF, 0x4FFFB0, 0.40f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.00f, 0.00f, 0.18f, 0xFFFFFF, 0xFF4FD8, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 70, 0.00f, kShPixel, 0.40f, 7.00f, 0.60f, 0x4FFFB0, 0xFFFFFF, 6.00f, -0.10f),
    B(0.03f, kT0, kBurst, kAtGoal, 40, 0.00f, kShPixel, 0.58f, 3.50f, 0.85f, 0xFF4FD8, 0x5AC8FF, 4.00f, -0.10f),
    B(0.00f, kT0, kRings, kAtGoal, 2, 0.08f, kShPixel, 9.00f, 0.00f, 0.35f, 0x4FFFB0, 0xFF4FD8, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 12, 0.00f, kShStreak, 4.00f, 0.00f, 0.20f, 0xFFFFFF, 0x4FFFB0, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 60, 0.00f, kShStreak, 0.35f, 11.0f, 0.90f, 0xFFFFFF, 0x4FFFB0, 1.40f, 0.40f),
    B(0.00f, kT0, kBurst, kAtGoal, 50, 0.00f, kShPixel, 0.08f, 10.0f, 1.00f, PAL(kPalPixel), 0xFFFFFF, 1.40f, 0.50f),
    B(0.12f, kT0, kPuffs, kAtGoal, 6, 0.00f, kShPuff, 2.40f, 0.50f, 1.60f, 0x3A2A50, 0x1A1028, 0.45f, 1.00f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShPixel, 3.00f, 0.00f, 0.00f, 0x4FFFB0, 0xFF4FD8, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 7.20f, 0x05020F, 0, 0.30f, 0.30f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.80f, 0.20f),
    B(0.00f, kTA, kRings, kAtFloor, 2, 0.15f, kShPixel, 5.50f, 0.00f, 0.60f, 0x4FFFB0, 0xFF4FD8, 0.00f, 0.00f),
    B(0.05f, kTA, kTakeover, kAtHead, kToInvaders, 6.60f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.00f, 0.00f),
    B(0.20f, kTA, kGlitter, kAtHead, 40, 6.20f, kShTwinkle, 0.020f, 0.00f, 1.20f, 0xFFFFFF, 0x8CC8FF, 0.00f, 0.00f),
};

// ---- METEOR STRIKE: a fire detonation, your room's sky goes dark orange, meteors streak down round you - punch them to
// embers or catch one (grip) and throw it; the misses crater your floor
const Beat kFxMeteor[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.80f, 0.00f, 0.20f, 0xFFFFFF, 0xFFB040, 1.00f, 1.20f),
    B(0.00f, kT0, kBurst, kAtGoal, 40, 0.00f, kShFirePuff, 1.85f, 14.07f, 0.90f, 0xFFF0B0, 0xFF7A1A, 6.00f, -0.15f),
    B(0.02f, kT0, kBurst, kAtGoal, 18, 0.00f, kShFirePuff, 3.96f, 4.75f, 1.42f, 0xFFB040, 0xD03A08, 4.00f, -0.25f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.30f, 0xFFF0B0, 0xFF4A10, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 14, 0.00f, kShStreak, 4.50f, 0.00f, 0.22f, 0xFFFFFF, 0xFFA030, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 80, 0.00f, kShStreak, 0.35f, 10.0f, 1.20f, 0xFFF0B0, 0xFF5A10, 1.20f, 0.50f),
    B(0.00f, kT0, kBurst, kAtGoal, 30, 0.00f, kMxRock, 0.14f, 6.50f, 1.80f, 0x4A3628, 0xFF5A10, 0.40f, 1.00f),
    B(0.15f, kT0, kPuffs, kAtGoal, 12, 0.00f, kShPuff, 3.20f, 0.70f, 2.40f, 0x3A3436, 0x1A1618, 0.65f, 1.20f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.20f, 0.00f, 0.00f, 0xFFD070, 0xFF4A10, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 7.20f, 0x1A0800, 0, 0.26f, 0.40f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.35f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.60f, 0xFFB040, 0xFF3A10, 0.00f, 0.00f),
    B(0.05f, kTA, kTakeover, kAtHead, kToMeteor, 6.60f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.00f, 0.00f),
    B(0.20f, kTA, kRain, kAtTop, 70, 6.20f, kShSpark, 0.012f, 0.35f, 2.40f, 0xFFE070, 0xFF5A10, 0.25f, 0.00f),
};

// ---- BLACK HOLE: a violet detonation, then a black hole tears open IN THE GOAL and your room spirals into it - push
// back with your hands, grab pieces out of the pull; it collapses into a supernova
const Beat kFxHole[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 5.20f, 0.00f, 0.18f, 0xFFFFFF, 0xC08CFF, 1.00f, 0.40f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.40f, 0.00f, 0.20f, 0xFFFFFF, 0x9B5BFF, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 35, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.90f, 0xF0E0FF, 0x7A3AE0, 6.00f, 0.00f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShFirePuff, 3.74f, 4.38f, 1.35f, 0x9B5BFF, 0x1A0A40, 4.00f, 0.00f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.30f, 0xFFFFFF, 0x9B5BFF, 1.00f, 0.00f),
    B(0.05f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 7.00f, 0.00f, 0.40f, 0xE8D8FF, 0x3BC8FF, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 14, 0.00f, kShStreak, 4.50f, 0.00f, 0.22f, 0xFFFFFF, 0xC08CFF, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 70, 0.00f, kShTwinkle, 0.10f, 9.00f, 1.20f, PAL(kPalStars), 0xC08CFF, 1.40f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 18, 0.00f, kMxRock, 0.12f, 6.00f, 1.60f, 0x2A2438, 0x14101E, 0.40f, 1.00f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.20f, 0.00f, 0.00f, 0xD8B8FF, 0x5030C0, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 7.20f, 0x05020C, 0, 0.34f, 0.50f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.90f, 0.35f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.60f, 0xD8B8FF, 0x5030C0, 0.00f, 0.00f),
    B(0.05f, kTA, kTakeover, kAtHead, kToHole, 6.60f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.00f, 0.00f),
};

// ---- ROOM FLOOD: a water blast out of the goal, then the water rises round you to your knees - fish swim past and dart
// from your hands, slap it and it splashes, a fish leaps now and then; it drains away down a whirlpool
const Beat kFxFlood[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.40f, 0.00f, 0.18f, 0xFFFFFF, 0x8FDCFF, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 35, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.80f, 0xF4FDFF, 0x3AA8E8, 6.00f, 0.10f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShPuff, 3.40f, 4.20f, 1.44f, 0xFFFFFF, 0xBFE8FF, 4.00f, 0.00f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.32f, 0xFFFFFF, 0x3AA8E8, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 12, 0.00f, kShStreak, 4.20f, 0.00f, 0.22f, 0xFFFFFF, 0x8FDCFF, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 110, 0.00f, kShDrop, 0.10f, 8.00f, 1.40f, 0xE8F8FF, 0x3AA8E8, 0.60f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 36, 0.00f, kShBubble, 0.12f, 5.00f, 1.40f, 0xBFF0FF, 0xFFFFFF, 1.20f, -0.10f),
    B(0.00f, kT0, kBurst, kAtGoal, 50, 0.00f, kShStreak, 0.30f, 9.00f, 0.90f, 0xFFFFFF, 0x8FDCFF, 1.30f, 0.40f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.20f, 0.00f, 0.00f, 0xE8FBFF, 0x3AA8E8, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.70f, 0.00f, 0.30f, 0x8FDCFF, 0, 0.20f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.90f, 0.30f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.60f, 0xE8FBFF, 0x3AA8E8, 0.00f, 0.00f),
    B(0.05f, kTA, kTakeover, kAtHead, kToFlood, 6.80f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.00f, 0.00f),
};

// ---- GRAVITY FLIP: a confetti detonation, then everything on your floor falls UP to your ceiling and sticks - your
// hands blow it round like wind, swipe it loose - then gravity flips back and it all rains down
const Beat kFxFlip[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.40f, 0.00f, 0.18f, 0xFFFFFF, 0xE0C0FF, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 34, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.83f, 0xFFF4FF, 0xC85BFF, 6.00f, -0.10f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShFirePuff, 3.52f, 4.38f, 1.27f, 0xFFD34A, 0x7A3AE0, 4.00f, -0.10f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.30f, 0xFFFFFF, 0xC85BFF, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 12, 0.00f, kShStreak, 4.00f, 0.00f, 0.20f, 0xFFFFFF, 0xFFD34A, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 160, 0.00f, kMxPaper, 0.11f, 8.00f, 2.00f, PAL(kPalConfetti), PAL(kPalConfetti), 1.40f, -0.20f),
    B(0.00f, kT0, kBurst, kAtGoal, 14, 0.00f, kMxStreamer, 0.55f, 7.00f, 2.00f, PAL(kPalConfetti), PAL(kPalConfetti), 1.40f, -0.20f),
    B(0.00f, kT0, kBurst, kAtGoal, 40, 0.00f, kShStreak, 0.30f, 9.00f, 0.90f, 0xFFFFFF, 0xC85BFF, 1.40f, -0.30f),
    B(0.10f, kT0, kPuffs, kAtGoal, 8, 0.00f, kShPuff, 2.60f, 0.50f, 1.80f, 0x5A4A66, 0x30283A, 0.45f, 1.00f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.00f, 0.00f, 0.00f, 0xFFFFFF, 0xC85BFF, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.80f, 0.00f, 0.22f, 0xE0C0FF, 0, 0.22f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.80f, 0.25f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 5.50f, 0.00f, 0.60f, 0xFFFFFF, 0xC85BFF, 0.00f, 0.00f),
    B(0.05f, kTA, kTakeover, kAtHead, kToFlip, 6.60f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.00f, 0.00f),
};

// ---- SHARK BITE: a sea spray blast, then the floor round your feet turns to water, a fin circles you - bop it and it
// dives; let it close in and the shark snaps up out of the water; last it jumps clean through the water beside you
// r162 (Lance: "the only one i dont want is the shark fin one"): out of the catalogue; its table kept for the PC twin
[[maybe_unused]] const Beat kFxShark[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.40f, 0.00f, 0.18f, 0xFFFFFF, 0x6FC8FF, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 35, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.80f, 0xF4FDFF, 0x2A8AD0, 6.00f, 0.10f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShPuff, 3.40f, 4.20f, 1.44f, 0xFFFFFF, 0x9AC8E8, 4.00f, 0.00f),
    B(0.00f, kT0, kRings, kAtGoal, 1, 0.00f, kShRing, 10.0f, 0.00f, 0.32f, 0xFFFFFF, 0x2A8AD0, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 12, 0.00f, kShStreak, 4.20f, 0.00f, 0.22f, 0xFFFFFF, 0x6FC8FF, 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 110, 0.00f, kShDrop, 0.10f, 8.00f, 1.40f, 0xE8F8FF, 0x2A8AD0, 0.60f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 50, 0.00f, kShStreak, 0.30f, 9.00f, 0.90f, 0xFFFFFF, 0x6FC8FF, 1.30f, 0.40f),
    B(0.10f, kT0, kPuffs, kAtGoal, 8, 0.00f, kShPuff, 2.60f, 0.50f, 1.80f, 0xE8F4FF, 0x9AB8D0, 0.40f, 1.00f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.20f, 0.00f, 0.00f, 0xE8FBFF, 0x2A8AD0, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.70f, 0.00f, 0.30f, 0x6FC8FF, 0, 0.18f, 0.01f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.90f, 0.30f),
    B(0.00f, kTA, kRings, kAtFloor, 1, 0.00f, kShWave, 6.00f, 0.00f, 0.60f, 0xE8FBFF, 0x2A8AD0, 0.00f, 0.00f),
    B(0.05f, kTA, kTakeover, kAtHead, kToShark, 7.20f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.00f, 0.00f),
};

// ---- DISCO INFERNO: a rainbow detonation, the room goes dark and your floor becomes a lit dance floor (it lights under
// your feet), a mirror ball drops, its beams follow your hands, a clap fires sparkles; it ends in a ring of fire
const Beat kFxDisco[] = {
    B(0.00f, kT0, kSound, kAtGoal, 0, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 5.40f, 0.00f, 0.18f, 0xFFFFFF, 0xFF5FC8, 1.00f, 0.40f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 3.40f, 0.00f, 0.20f, 0xFFFFFF, 0xFFD21F, 1.00f, 1.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 40, 0.00f, kShFirePuff, 1.69f, 13.13f, 0.90f, PAL(kPalRainbow), 0xFFFFFF, 6.00f, 0.00f),
    B(0.02f, kT0, kBurst, kAtGoal, 14, 0.00f, kShFirePuff, 3.52f, 4.38f, 1.27f, PAL(kPalRainbow), PAL(kPalRainbow), 4.00f, 0.00f),
    B(0.00f, kT0, kRings, kAtGoal, 3, 0.10f, kShHex, 10.0f, 0.00f, 0.35f, PAL(kPalRainbow), PAL(kPalRainbow), 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtGoal, 16, 0.00f, kShStreak, 4.50f, 0.00f, 0.22f, 0xFFFFFF, PAL(kPalRainbow), 0.035f, 0.00f),
    B(0.00f, kT0, kBurst, kAtGoal, 80, 0.00f, kShTwinkle, 0.10f, 9.00f, 1.20f, PAL(kPalRainbow), 0xFFFFFF, 1.40f, 0.10f),
    B(0.00f, kT0, kBurst, kAtGoal, 120, 0.00f, kMxPaper, 0.11f, 8.00f, 2.00f, PAL(kPalConfetti), PAL(kPalConfetti), 1.40f, 0.30f),
    B(0.00f, kT0, kBurst, kAtGoal, 50, 0.00f, kShStreak, 0.32f, 10.0f, 1.00f, PAL(kPalRainbow), 0xFFFFFF, 1.30f, 0.40f),
    B(0.02f, kT0, kSweep, kAtGoal, 1, 0.00f, kShWave, 3.20f, 0.00f, 0.00f, 0xFFFFFF, 0xFF5FC8, 1.00f, 0.00f),
    B(0.00f, kTA, kSound, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.00f),
    B(0.00f, kTA, kVeil, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 7.40f, 0x08040F, 0, 0.30f, 0.30f),
    B(0.00f, kTA, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.90f, 0.30f),
    B(0.05f, kTA, kTakeover, kAtHead, kToDisco, 6.80f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.00f, 0.00f),
};

// ------------------------------------------------------------------------------------------------------------------
// The catalogue (shop order: cheapest first). Prices: TIZM coins (a played franchise match ~56, a pack 50).
// ------------------------------------------------------------------------------------------------------------------
struct Def {
  Item item;
  const Beat* beats;
  int beatCount;
  float waveSpeed;  // the wave from the goal through you (room units a second): when the room's beats start
};
#define BEATS(x) x, static_cast<int>(sizeof(x) / sizeof(x[0]))
const Def kDefs[] = {
    {{"classic", "CLASSIC BURST", "White-gold blast, spark trails, gold foil.", 0, 0, 0xFFD040, 0x5AC8FF, 2.6f}, BEATS(kFxClassic), 14.0f},
    {{"confetti", "CONFETTI CANNON", "A cannon-load of confetti and streamers.", 150, 1, 0xFF5FA8, 0xFFD21F, 2.8f}, BEATS(kFxConfetti), 14.0f},
    {{"pixel", "PIXEL POP", "8-bit blast: a blocky fireball, cubes, coins.", 150, 1, 0x00B4FF, 0xFFD800, 2.5f}, BEATS(kFxPixel), 14.0f},
    {{"bomb", "BOB-OMB BLAST", "A bomb hops to your feet and goes off there.", 300, 2, 0xFF7A1A, 0x2A2C36, 3.3f}, BEATS(kFxBomb), 14.0f},
    {{"ice", "ICE SHATTER", "A frozen blast of ice chunks and shards.", 300, 2, 0x8FDCFF, 0xFFFFFF, 2.6f}, BEATS(kFxIce), 14.0f},
    {{"thunder", "THUNDERSTRUCK", "Plasma blast, bolts, lightning round you.", 300, 2, 0x7FC0FF, 0x0A1430, 2.6f}, BEATS(kFxThunder), 16.0f},
    {{"inferno", "INFERNO RING", "A huge fireball and a ring of flame round you.", 600, 3, 0xFF7A1A, 0xFFE070, 2.8f}, BEATS(kFxInferno), 14.0f},
    {{"galaxy", "GALAXY WARP", "Star fireball, a portal, deep space for a beat.", 600, 3, 0xC08CFF, 0x06040F, 2.5f}, BEATS(kFxGalaxy), 14.0f},
    {{"chomp", "PIRANHA CHOMP", "Leaves fly, then a giant chomper bursts up.", 600, 3, 0xE0262E, 0x3ACB3A, 3.0f}, BEATS(kFxChomp), 14.0f},
    // r161: pack 2, the room takeovers (~8 s, live off your hands), each among its price's; every line short enough for the
    // shop's side panel (4 lines at 40 px, nothing shrunk)
    {{"bitroom", "8-BIT TAKEOVER", "? blocks: punch them for REAL coins (150 max)!", 600, 3, 0xFFD800, 0x00B4FF, 8.0f}, BEATS(kFxBits), 14.0f},
    {{"gravity", "GRAVITY FLIP", "Confetti falls UP, blow it round, then it rains.", 600, 3, 0xC85BFF, 0xFFD34A, 8.0f}, BEATS(kFxFlip), 14.0f},
    {{"flood", "ROOM FLOOD", "Water to your knees, fish, splash it!", 600, 3, 0x3AA8E8, 0x0A3A6A, 8.2f}, BEATS(kFxFlood), 14.0f},
    {{"kaleido", "KALEIDOSCOPE", "A prism blast, mirror shards, your room cracks.", 1200, 4, 0xC8A0FF, 0x3BC8FF, 2.5f}, BEATS(kFxKaleido), 14.0f},
    {{"cardstorm", "CARDSTORM", "A whole case of Tizm cards blown out at you.", 1200, 4, 0xFFD34A, 0xB07CFF, 2.6f}, BEATS(kFxCardstorm), 14.0f},
    {{"invaders", "PIXEL INVADERS", "Blast the aliens: 15 REAL coins each!", 1200, 4, 0x4FFFB0, 0xFF4FD8, 8.0f}, BEATS(kFxInvaders), 14.0f},
    {{"meteor", "METEOR STRIKE", "Meteors rain: punch them, catch and throw.", 1200, 4, 0xFF6A1A, 0x3A2A20, 8.0f}, BEATS(kFxMeteor), 14.0f},
    {{"olympus", "OLYMPUS", "The gods: a huge fireball, bolts, columns.", 2500, 5, 0xFFD34A, 0xF4EEE2, 2.9f}, BEATS(kFxOlympus), 16.0f},
    {{"blackhole", "BLACK HOLE", "A black hole pulls your room in. Grab back!", 2500, 5, 0x9B5BFF, 0x05020C, 8.0f}, BEATS(kFxHole), 14.0f},
    {{"disco", "DISCO INFERNO", "A lit dance floor, a mirror ball, clap it up!", 2500, 5, 0xFF5FC8, 0xFFD21F, 8.2f}, BEATS(kFxDisco), 14.0f},
};
constexpr int kDefCount = static_cast<int>(sizeof(kDefs) / sizeof(kDefs[0]));

// ---- r207 (Lance 11-10, "go head"): the CASINO WIN PARTY - a SLOTS / ROULETTE win at the TIZM machine goes off in your room, by
// its size (xr_casino's tier): 1 a pop, 2 BIG WIN the full show, 3 HUGE WIN more of it, 4 JACKPOT everything. Its "goal" is the
// machine's payout tray (kAtGoal), kAtCab its cabinet's top, kAtAbove the air over it; sizes in metres (x the machine's size).
// Never in the shop (not in kDefs: no id finds it).
const Beat kCasPop[] = {
    B(0.00f, kT0, kCasPulse, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.30f, 0xFFD040, 0, 0.35f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 0.30f, 0.00f, 0.25f, 0xFFFFFF, 0xFFD040, 1.00f, 0.80f),
    B(0.00f, kT0, kCasCoins, kAtGoal, 10, 0.00f, kMxCoin, 0.042f, 1.50f, 4.20f, 0xFFD21F, 0xC8901E, 0.00f, 0.00f),
    B(0.00f, kT0, kBurst, kAtCab, 22, 0.00f, kShSpark, 0.022f, 1.60f, 0.70f, 0xFFF4C0, 0xFFC830, 1.50f, 0.30f),
    B(0.00f, kT0, kBurst, kAtCab, 10, 0.00f, kShTwinkle, 0.05f, 0.90f, 0.60f, 0xFFFFFF, 0xFFE9A0, 1.20f, 0.10f),
};
const Beat kCasBig[] = {
    B(0.00f, kT0, kCasPulse, kAtHead, 2, 0.45f, 0, 0.00f, 0.00f, 0.30f, 0xFFD040, 0, 0.60f, 0.00f),
    B(0.00f, kT0, kFlash, kAtCab, 1, 0.00f, kShSpark, 0.90f, 0.00f, 0.25f, 0xFFFFFF, 0xFFD040, 1.00f, 1.00f),
    B(0.00f, kT0, kFlash, kAtCab, 1, 0.00f, kShTwinkle, 1.20f, 0.00f, 0.18f, 0xFFFFFF, 0xFFE9A0, 1.00f, 0.40f),
    B(0.00f, kT0, kCasSiren, kAtCab, 1, 0.00f, 0, 0.16f, 1.40f, 4.30f, 0xFF2A1A, 0xD8DCE6, 0.00f, 0.00f),
    B(0.05f, kT0, kCasLetters, kAtCab, 2, 0.00f, 0, 0.26f, 0.00f, 4.00f, 0xFFC830, 0xFF2E88, 0.00f, 0.00f),
    B(0.10f, kT0, kCasCoins, kAtGoal, 60, 1.60f, kMxCoin, 0.042f, 1.90f, 6.00f, 0xFFD21F, 0xC8901E, 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtCab, 110, 0.00f, kMxPaper, 0.040f, 3.40f, 3.20f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtCab, 8, 0.00f, kMxStreamer, 0.22f, 3.00f, 3.20f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.40f, kT0, kCasFirework, kAtAbove, 4, 2.40f, kShStreak, 0.10f, 2.20f, 1.20f, PAL(kPalRainbow), 0xFFFFFF, 26.0f, 0.00f),
    B(0.00f, kT0, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.70f, 0.30f),
};
const Beat kCasHuge[] = {
    B(0.00f, kT0, kCasPulse, kAtHead, 3, 0.75f, 0, 0.00f, 0.00f, 0.30f, 0xFFD040, 0, 0.75f, 0.00f),
    B(0.00f, kT0, kFlash, kAtCab, 1, 0.00f, kShSpark, 1.10f, 0.00f, 0.25f, 0xFFFFFF, 0xFFD040, 1.00f, 1.00f),
    B(0.00f, kT0, kFlash, kAtCab, 1, 0.00f, kShTwinkle, 1.50f, 0.00f, 0.18f, 0xFFFFFF, 0xFFE9A0, 1.00f, 0.40f),
    B(0.00f, kT0, kRings, kAtCab, 1, 0.00f, kShRing, 2.00f, 0.00f, 0.40f, 0xFFF4D0, 0xFFC830, 1.00f, 0.00f),
    B(0.00f, kT0, kCasSiren, kAtCab, 1, 0.00f, 0, 0.17f, 1.60f, 5.30f, 0xFF2A1A, 0xD8DCE6, 0.00f, 0.00f),
    B(0.05f, kT0, kCasLetters, kAtCab, 3, 0.00f, 0, 0.28f, 0.00f, 5.00f, 0xFFC830, 0xFF2E88, 0.00f, 0.00f),
    B(0.10f, kT0, kCasCoins, kAtGoal, 100, 2.40f, kMxCoin, 0.042f, 2.10f, 6.50f, 0xFFD21F, 0xC8901E, 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtCab, 180, 0.00f, kMxPaper, 0.040f, 3.80f, 3.60f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtCab, 12, 0.00f, kMxStreamer, 0.24f, 3.40f, 3.60f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.30f, kT0, kCasFirework, kAtAbove, 8, 3.60f, kShStreak, 0.10f, 2.40f, 1.30f, PAL(kPalRainbow), 0xFFFFFF, 28.0f, 0.00f),
    B(0.00f, kT0, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.85f, 0.40f),
};
const Beat kCasJackpot[] = {
    B(0.00f, kT0, kCasPulse, kAtHead, 7, 2.40f, 0, 0.00f, 0.00f, 0.30f, 0xFFD040, 0, 1.00f, 0.00f),
    B(0.00f, kT0, kFlash, kAtCab, 1, 0.00f, kShSpark, 1.40f, 0.00f, 0.30f, 0xFFFFFF, 0xFFD040, 1.00f, 1.20f),
    B(0.00f, kT0, kFlash, kAtCab, 1, 0.00f, kShTwinkle, 1.90f, 0.00f, 0.20f, 0xFFFFFF, 0xFFE9A0, 1.00f, 0.40f),
    B(0.00f, kT0, kRings, kAtCab, 2, 0.30f, kShRing, 2.60f, 0.00f, 0.45f, 0xFFF4D0, 0xFFC830, 1.00f, 0.00f),
    B(0.00f, kT0, kRays, kAtAbove, 14, 0.00f, kShStreak, 1.40f, 0.00f, 0.30f, 0xFFFFFF, 0xFFC830, 0.035f, 0.00f),
    B(0.00f, kT0, kCasSiren, kAtCab, 1, 0.00f, 0, 0.18f, 1.80f, 7.20f, 0xFF2A1A, 0xD8DCE6, 0.00f, 0.00f),
    B(0.05f, kT0, kCasLetters, kAtCab, 4, 0.00f, 0, 0.34f, 0.00f, 6.80f, 0xFFC830, 0xFF2E88, 0.00f, 0.00f),
    B(0.10f, kT0, kCasCoins, kAtGoal, 160, 3.60f, kMxCoin, 0.044f, 2.30f, 7.00f, 0xFFD21F, 0xC8901E, 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtCab, 220, 0.00f, kMxPaper, 0.040f, 4.20f, 3.80f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtCab, 14, 0.00f, kMxStreamer, 0.26f, 3.60f, 3.80f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(2.20f, kT0, kCasConfetti, kAtCab, 120, 0.00f, kMxFoil, 0.040f, 3.60f, 3.40f, PAL(kPalGold), 0xC8901E, 0.00f, 0.00f),
    B(0.25f, kT0, kCasFirework, kAtAbove, 16, 5.60f, kShStreak, 0.11f, 2.60f, 1.40f, PAL(kPalRainbow), 0xFFFFFF, 26.0f, 0.00f),
    B(0.60f, kT0, kRain, kAtTop, 90, 2.60f, kMxFoil, 0.032f, 0.55f, 2.40f, PAL(kPalGold), 0xC8901E, 0.30f, 0.00f),
    B(0.00f, kT0, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 1.00f, 0.50f),
    B(1.60f, kT0, kBuzz, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.00f, 0, 0, 0.60f, 0.30f),
};
const Def kCasinoDefs[4] = {
    {{"casino1", "CASINO POP", "", 0, 0, 0xFFD040, 0xFF2E88, 1.6f}, BEATS(kCasPop), 14.0f},
    {{"casino2", "BIG WIN", "", 0, 0, 0xFFD040, 0xFF2E88, 4.6f}, BEATS(kCasBig), 14.0f},
    {{"casino3", "HUGE WIN", "", 0, 0, 0xFFD040, 0xFF2E88, 5.6f}, BEATS(kCasHuge), 14.0f},
    {{"casino4", "JACKPOT", "", 0, 0, 0xFFD040, 0xFF2E88, 7.6f}, BEATS(kCasJackpot), 14.0f},
};
// the giant letters' words by tier (kCasLetters' n): 2 BIG WIN, 3 HUGE WIN, 4 JACKPOT
const char* cas_word(int tier) { return tier >= 4 ? "JACKPOT" : (tier == 3 ? "HUGE WIN" : "BIG WIN"); }
#include "xr_goalfx_pulls.inc"  // r219: kPullDefs (the party on big pulls)

const Def* def_of(const char* id) {
  if (id == nullptr || *id == '\0') return nullptr;
  for (const Def& d : kDefs)
    if (std::strcmp(d.item.id, id) == 0) return &d;
  return nullptr;
}

// ------------------------------------------------------------------------------------------------------------------
// Owned + equipped (STRIKERS_HOME/cards/goalfx.txt), the wallet, the friend's
// ------------------------------------------------------------------------------------------------------------------
std::mutex g_mutex;
bool g_loaded = false;
std::vector<std::string> g_owned;
std::string g_equipped = "classic";
std::string g_friend;
Wallet g_wallet;
std::atomic<int> g_gen{1};

std::string home_dir() {
  const char* home = std::getenv("STRIKERS_HOME");
  return home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers";
}
std::string save_path() { return home_dir() + "/cards/goalfx.txt"; }

bool owned_locked(const char* id) {
  const Def* d = def_of(id);
  if (d == nullptr) return false;
  if (d->item.price == 0) return true;
  for (const std::string& o : g_owned)
    if (o == id) return true;
  return false;
}

void load_locked() {
  if (g_loaded) return;
  g_loaded = true;
  FILE* f = std::fopen(save_path().c_str(), "r");
  if (f == nullptr) return;
  char line[160];
  std::string eq;
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    char key[24] = {}, val[40] = {};
    if (std::sscanf(line, "%23s %39s", key, val) != 2) continue;
    if (std::strcmp(key, "owned") == 0 && def_of(val) != nullptr) {
      if (std::find(g_owned.begin(), g_owned.end(), std::string(val)) == g_owned.end()) g_owned.emplace_back(val);
    } else if (std::strcmp(key, "equipped") == 0) {
      eq = val;
    }
  }
  std::fclose(f);
  if (eq == "none" || owned_locked(eq.c_str())) g_equipped = eq;
  GLOG("goalfx: %d owned, %s equipped", static_cast<int>(g_owned.size()), g_equipped.c_str());
}

void save_locked() {
  std::string out = "# Quest 3 Strikers goal explosions (xr_goalfx.cpp)\nequipped " + g_equipped + "\n";
  for (const std::string& o : g_owned) out += "owned " + o + "\n";
  const std::string path = save_path(), tmp = path + ".tmp";
  ::mkdir((home_dir() + "/cards").c_str(), 0775);
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) {
    GLOG("goalfx: can't write %s", tmp.c_str());
    return;
  }
  const bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size() && std::fflush(f) == 0;
  ::fsync(fileno(f));
  std::fclose(f);
  if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) GLOG("goalfx: writing %s failed", path.c_str());
}

// ------------------------------------------------------------------------------------------------------------------
// Little maths
// ------------------------------------------------------------------------------------------------------------------
struct V3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
};
V3 v3(float x, float y, float z) { return V3{x, y, z}; }
V3 v3(const XrVector3f& p) { return V3{p.x, p.y, p.z}; }
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float len(V3 a) { return std::sqrt(dot(a, a)); }
V3 norm(V3 a, V3 fallback = V3{0.0f, 0.0f, -1.0f}) {
  const float l = len(a);
  return l > 1e-5f ? a * (1.0f / l) : fallback;
}
V3 rot(const XrQuaternionf& q, V3 v) {
  const V3 u{q.x, q.y, q.z};
  const V3 t = cross(u, v) * 2.0f;
  return v + t * q.w + cross(u, t);
}
float clampf(float x, float a, float b) { return std::max(a, std::min(b, x)); }
uint64_t mono_ns() {  // r161: the hands' clock
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}
float ease_out(float x) {
  x = clampf(x, 0.0f, 1.0f);
  return 1.0f - (1.0f - x) * (1.0f - x) * (1.0f - x);
}

// The quaternion of the basis (x, y, z) (columns), as xr_fx.vert reads it (x, y, z, w)
void quat_basis(V3 x, V3 y, V3 z, float q[4]) {
  const float r00 = x.x, r01 = y.x, r02 = z.x, r10 = x.y, r11 = y.y, r12 = z.y, r20 = x.z, r21 = y.z, r22 = z.z;
  const float tr = r00 + r11 + r22;
  if (tr > 0.0f) {
    const float t = std::sqrt(tr + 1.0f) * 2.0f;
    q[3] = 0.25f * t; q[0] = (r21 - r12) / t; q[1] = (r02 - r20) / t; q[2] = (r10 - r01) / t;
  } else if (r00 > r11 && r00 > r22) {
    const float t = std::sqrt(1.0f + r00 - r11 - r22) * 2.0f;
    q[3] = (r21 - r12) / t; q[0] = 0.25f * t; q[1] = (r01 + r10) / t; q[2] = (r02 + r20) / t;
  } else if (r11 > r22) {
    const float t = std::sqrt(1.0f + r11 - r00 - r22) * 2.0f;
    q[3] = (r02 - r20) / t; q[0] = (r01 + r10) / t; q[1] = 0.25f * t; q[2] = (r12 + r21) / t;
  } else {
    const float t = std::sqrt(1.0f + r22 - r00 - r11) * 2.0f;
    q[3] = (r10 - r01) / t; q[0] = (r02 + r20) / t; q[1] = (r12 + r21) / t; q[2] = 0.25f * t;
  }
}
// A quad facing `eye` from `at`, turned `roll` in its own plane
void q_facing(V3 at, V3 eye, float roll, float q[4]) {
  const V3 z = norm(eye - at, V3{0.0f, 0.0f, 1.0f});
  V3 x = cross(V3{0.0f, 1.0f, 0.0f}, z);
  x = len(x) < 1e-4f ? V3{1.0f, 0.0f, 0.0f} : norm(x);
  const V3 y = cross(z, x);
  const float c = std::cos(roll), s = std::sin(roll);
  quat_basis(x * c + y * s, y * c - x * s, z, q);
}
// Upright (its up stays up), turned about the vertical to face `eye`
void q_upright(V3 at, V3 eye, float q[4]) {
  V3 z = eye - at;
  z.y = 0.0f;
  z = norm(z, V3{0.0f, 0.0f, 1.0f});
  const V3 y{0.0f, 1.0f, 0.0f};
  quat_basis(cross(y, z), y, z, q);
}
// A quad whose long side (local y) runs along `dir`, its face turned to `eye`
void q_along(V3 at, V3 dir, V3 eye, float q[4]) {
  const V3 y = norm(dir, V3{0.0f, 1.0f, 0.0f});
  V3 t = eye - at;
  V3 z = t - y * dot(t, y);
  z = len(z) < 1e-4f ? V3{0.0f, 0.0f, 1.0f} : norm(z);
  quat_basis(cross(y, z), y, z, q);
}
// Lying flat (face up), turned `yaw`
void q_flat(float yaw, float q[4]) {
  const V3 x{std::cos(yaw), 0.0f, std::sin(yaw)}, z{0.0f, 1.0f, 0.0f};
  quat_basis(x, cross(z, x), z, q);
}
// The plane whose normal is n (its face toward n)
void q_plane(V3 n, float q[4]) {
  const V3 z = norm(n, V3{0.0f, 0.0f, 1.0f});
  V3 x = cross(V3{0.0f, 1.0f, 0.0f}, z);
  x = len(x) < 1e-4f ? V3{1.0f, 0.0f, 0.0f} : norm(x);
  quat_basis(x, cross(z, x), z, q);
}
void q_axis_angle(V3 axis, float angle, float q[4]) {
  const float h = 0.5f * angle, s = std::sin(h);
  q[0] = axis.x * s; q[1] = axis.y * s; q[2] = axis.z * s; q[3] = std::cos(h);
}
void q_mul(const float a[4], const float b[4], float out[4]) {
  const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
  out[0] = x; out[1] = y; out[2] = z; out[3] = w;
}
uint32_t hsv(float h, float s, float v) {
  h = h - std::floor(h);
  const float r = clampf(std::fabs(h * 6.0f - 3.0f) - 1.0f, 0.0f, 1.0f);
  const float g = clampf(2.0f - std::fabs(h * 6.0f - 2.0f), 0.0f, 1.0f);
  const float b = clampf(2.0f - std::fabs(h * 6.0f - 4.0f), 0.0f, 1.0f);
  auto ch = [&](float c) { return static_cast<uint32_t>(clampf((1.0f + (c - 1.0f) * s) * v, 0.0f, 1.0f) * 255.0f + 0.5f); };
  return (ch(r) << 16) | (ch(g) << 8) | ch(b);
}
uint32_t pack(uint32_t rgb, uint8_t a) {
  return ((rgb >> 16) & 0xFF) | (((rgb >> 8) & 0xFF) << 8) | ((rgb & 0xFF) << 16) | (static_cast<uint32_t>(a) << 24);
}

// The proofs' twin (tools/outside/goalfx153.py) draws with the same little generator
struct Rng {
  uint32_t x = 0x9E3779B9u;
  uint32_t next() {
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
  }
  float f(float a, float b) { return a + (b - a) * static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
  int i(int n) { return n <= 1 ? 0 : static_cast<int>(next() % static_cast<uint32_t>(n)); }
  V3 unit() {
    const float z = f(-1.0f, 1.0f), a = f(0.0f, 6.2831853f), r = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return V3{r * std::cos(a), r * std::sin(a), z};
  }
};

// ------------------------------------------------------------------------------------------------------------------
// Pieces
// ------------------------------------------------------------------------------------------------------------------
enum Orient : uint8_t {
  kOrFace,       // faces your eyes (xr_fx.vert's own billboard)
  kOrFaceRoll,   // faces your head, turned `roll`
  kOrUpright,    // stands up, turned to face you
  kOrAlong,      // its length along its flight
  kOrAlongFixed, // its length along `axis`
  kOrFlat,       // lying flat, turned `roll`
  kOrFixed,      // its own turn (q), turning about `axis` at `spin`
  kOrVeil        // a sheet just before your eyes (the room tinted)
};
enum Floor : uint8_t { kFlNone, kFlBounce, kFlDie, kFlSettle, kFlShatter };
enum Anim : uint8_t { kAnNone, kAnColumn, kAnStem, kAnLeaf, kAnHead, kAnBomb, kAnPortal, kAnPop, kAnShard, kAnFling, kAnFlame };

struct P {
  V3 p, v;
  float size = 0.1f, grow = 0.0f, aspect = 1.0f;
  float age = 0.0f, life = 1.0f, fadeIn = 0.03f, fadeOut = 0.3f, peak = 1.0f, glow = 1.0f;
  float drag = 0.0f, grav = 0.0f, fall = 0.0f, flutter = 0.0f, phase = 0.0f;
  float orbit = 0.0f;
  float roll = 0.0f, rollSpeed = 0.0f;
  float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  V3 axis{0.0f, 1.0f, 0.0f};
  float spin = 0.0f;
  uint32_t c1 = 0xFFFFFF, c2 = 0xFFFFFF;
  uint8_t c2a = 255;
  int shape = kShSpark;  // the shader's shape id
  int mesh = -1;         // an xr_fx mesh, -1 a quad
  Orient orient = kOrFace;
  Floor floor = kFlNone;
  Anim anim = kAnNone;
  bool ringFade = false;  // fades over its whole life (rings, waves)
  bool flicker = false;   // lightning
  bool inPicture = false; // stays in front of the big screen's picture
  bool released = false;
  float a0 = 0.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
  // r161: a room takeover's own piece: its role (Role), the hand holding it (-1 none), its own numbers and two spots
  uint8_t role = 0;
  int8_t held = -1;
  float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f;
  V3 h0, h1;
  // r207: a CASINO WIN PARTY piece (its own ways, casino_step): 1 a coin (bounces, rolls, spins down flat on your floor), 2 a
  // giant letter, 3 the siren light, 4 one of its beams, 5 its glow
  uint8_t cas = 0;
};

// r161: the takeovers' pieces by what they are (P::role), and their sounds (STRIKERS_HOME/franchise/goalfx/fx_<name>.wav,
// tools/outside/goalfx_sounds161.py)
enum Role : uint8_t {
  kRoNone, kRoBlock, kRoCube, kRoCoin, kRoAlien, kRoShot, kRoLaser, kRoMother, kRoMeteor, kRoCrater, kRoHole, kRoDebris,
  kRoWater, kRoFish, kRoDrop, kRoFlip, kRoFin, kRoSnap, kRoShark, kRoTile, kRoBall, kRoBeam, kRoSpot
};
enum Sfx : uint8_t {
  kSfxPunch, kSfxCoin, kSfxBump, kSfxLaser, kSfxZap, kSfxShard, kSfxThud, kSfxGrab, kSfxWhoosh, kSfxSplash, kSfxBop,
  kSfxSnap, kSfxChime, kSfxClap, kSfxBlip, kSfxGulp, kSfxCount
};
const char* const kSfxNames[kSfxCount] = {"punch", "coin", "bump", "laser", "zap", "shard", "thud", "grab",
                                          "whoosh", "splash", "bop", "snap", "chime", "clap", "blip", "gulp"};

// r161: a hand as the takeover sees it (where its grip is, how fast it moves, where it points, grip / trigger now and
// the frame before; wet: under the water's surface)
struct HandSt {
  bool on = false;
  V3 p, v, aimP, aimD{0.0f, 0.0f, -1.0f};
  bool grip = false, gripWas = false, wet = false;
  float trig = 0.0f, trigWas = 0.0f, fireAt = -9.0f, buzzAt = -9.0f;
};
// r161: the takeover running (one at a time): which, since when, for how long; its director's timers and counters, its
// centre, level, phase, angle, gravity's sign
struct Room {
  int kind = 0;
  bool on = false, clearing = false, sawDead = false, finale = false;
  float t0 = 0.0f, dur = 6.6f;
  float next[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  int count[4] = {0, 0, 0, 0};
  V3 c, cv, ball;
  V3 spot[6];
  float level = 0.0f, phase = 0.0f, ang = 0.0f, flip = 1.0f;
};

constexpr uint32_t kMaxParts = 1500;  // xr_fx.cpp reserves this many instances (kGoalFxParts)

// One explosion going off
constexpr int kTakeoverCoinCap = 150;  // r164: real TIZM coins a goal's 8-BIT TAKEOVER can pay at most
std::atomic<int> g_earned{0};          // r164: caught, not yet taken by the cards

struct Show {
  bool on = false;
  const Def* def = nullptr;
  bool preview = false, theirs = false;
  float age = 0.0f, arrive = 0.3f, end = 4.0f;
  int earned = 0;  // r164: 8-BIT TAKEOVER's coins caught this goal (real TIZM coins, at most kTakeoverCoinCap)
  std::vector<float> acc;
  std::vector<uint8_t> done;
  // the goal spot: where, which way out, the big screen's picture plane (picture: on the big screen), its unit
  V3 O, D{0.0f, 1.0f, 0.0f}, S1{1.0f, 0.0f, 0.0f}, S2{0.0f, 1.0f, 0.0f};
  bool picture = false;
  float g = 0.4f;
  // the room: your head, your forward / right on the floor, the floor, the top, its unit, its reach
  V3 H, fwd{0.0f, 0.0f, -1.0f}, right{1.0f, 0.0f, 0.0f};
  float floor = 0.0f, top = 2.4f, u = 1.0f, R = 2.2f;
  V3 front, side;
  V3 colTop[12];
  int colCount = 0;
  Rng rng;
  // r197: your room scan (MR, USE MY ROOM SCAN on; null: the floor plane as before) - the debris, the takeover's pieces
  // land on your real furniture and bounce off your walls
  const xrroom::Room* scan = nullptr;
  // r161: the room takeover, your hands, when each takeover sound last played
  Room room;
  HandSt hand[2];
  bool holds[2] = {false, false};
  float sfxAt[kSfxCount] = {-9.0f, -9.0f, -9.0f, -9.0f, -9.0f, -9.0f, -9.0f, -9.0f,
                            -9.0f, -9.0f, -9.0f, -9.0f, -9.0f, -9.0f, -9.0f, -9.0f};
  float sfxAny = -9.0f;
  // r207: the CASINO WIN PARTY (0 a goal explosion): its tier, the machine's cabinet top + its front (out of its screen) + size,
  // the room's gold pulses so far (when, how strong)
  int casino = 0;
  V3 cabTop, mFwd{0.0f, 0.0f, 1.0f};
  float mScale = 1.0f;
  float pulseAt[16] = {};
  float pulsePeak[16] = {};
  int pulses = 0;
  // r219: a party on a big pull (pull_party): its giant word's place (2 m out, over your eyes) and the word ("" none); cabTop is then
  // the floor spot the siren rises out of, its "tray" (O) the card in your hand
  bool pull = false;
  V3 stage;
  char word[16] = {};
};

// A goal (or a preview) waiting for the next update (it needs the scene: where the goal is in this view)
struct Pending {
  bool on = false;
  const Def* def = nullptr;
  bool preview = false, theirs = false;
  float gx = 0.0f, gy = 0.0f, gz = 0.0f, ndcX = 0.0f, ndcY = 0.0f;
  bool ndcValid = false;
  XrPosef where{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  // r207: a casino party (its tier; the tray, the cabinet's top, the machine's front + size)
  int casino = 0;
  V3 tray, cabTop, mFwd{0.0f, 0.0f, 1.0f};
  float mScale = 1.0f, floorY = 0.0f;
  bool pull = false;  // r219: a big pull's party (its word's stage, its word)
  V3 stage;
  char word[16] = {};
};

std::mutex g_showMutex;  // g_pending, g_buzz
Pending g_pending;
float g_buzzAmp = 0.0f, g_buzzSec = 0.0f;
bool g_stopAsked = false;
Show g_show;
std::vector<P> g_parts;
uint32_t g_seed = 1;
std::atomic<bool> g_playing{false};
int g_peakParts = 0;
std::atomic<float> g_casFlash{0.0f};  // r207: the casino party's gold room flash now (casino_flash)

// r161: the hands as hands() last saw them (g_showMutex), whether a takeover has them, whether the match is live
struct HandIn {
  bool on = false;
  V3 p, v, aimP, aimD{0.0f, 0.0f, -1.0f};
  bool grip = false;
  float trig = 0.0f;
  uint64_t ns = 0;
};
HandIn g_handIn[2];
std::atomic<bool> g_ownHands{false}, g_playLive{false}, g_liveKnown{false};

// Gravity where things are this size (a unit of 1: the room's own 9.8; the big screen's and the table's slower, the
// stadium's faster - their pieces fall as their size reads)
float G(const Show&, float unit) { return 9.8f * std::sqrt(std::max(0.05f, unit)); }

// ------------------------------------------------------------------------------------------------------------------
// Sounds: STRIKERS_HOME/franchise/goalfx/<id>_<clip>.wav (tools/outside/goalfx_sounds153.py), read once, on a thread
// ------------------------------------------------------------------------------------------------------------------
std::mutex g_wavMutex;
std::unordered_map<std::string, std::vector<short>> g_wavs;
std::atomic<bool> g_wavsLoading{false};

std::vector<short> read_wav(const std::string& path) {
  std::vector<short> out;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return out;
  std::vector<uint8_t> d;
  uint8_t buf[65536];
  size_t got;
  while ((got = std::fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + got);
  std::fclose(f);
  auto u32 = [&](size_t at) { return at + 4 <= d.size() ? static_cast<uint32_t>(d[at] | d[at + 1] << 8 | d[at + 2] << 16 | d[at + 3] << 24) : 0u; };
  auto u16 = [&](size_t at) { return at + 2 <= d.size() ? static_cast<uint32_t>(d[at] | d[at + 1] << 8) : 0u; };
  if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) return out;
  int channels = 1, bits = 16;
  for (size_t at = 12; at + 8 <= d.size();) {
    const uint32_t n = u32(at + 4);
    if (std::memcmp(d.data() + at, "fmt ", 4) == 0) {
      channels = static_cast<int>(u16(at + 10));
      bits = static_cast<int>(u16(at + 22));
    } else if (std::memcmp(d.data() + at, "data", 4) == 0 && bits == 16 && channels >= 1) {
      const size_t frames = std::min<size_t>(n, d.size() - at - 8) / (2 * static_cast<size_t>(channels));
      out.resize(frames);
      for (size_t i = 0; i < frames; ++i) out[i] = static_cast<short>(u16(at + 8 + i * 2 * channels));
      break;
    }
    at += 8 + n + (n & 1);
  }
  return out;
}

void preload_sounds() {
  if (g_wavsLoading.exchange(true)) return;
  std::thread([]() {
    // (r161: a takeover's own _2 (its finale) and _3 (its room starting), and the takeovers' shared fx_<name> clips;
    // a missing one plays nothing)
    std::vector<std::string> names;
    for (const Def& d : kDefs)
      for (int clip = 0; clip < 4; ++clip) names.push_back(std::string(d.item.id) + "_" + std::to_string(clip));
    for (const char* n : kSfxNames) names.push_back(std::string("fx_") + n);
    for (const std::string& name : names) {
      std::vector<short> pcm = read_wav(home_dir() + "/franchise/goalfx/" + name + ".wav");
      std::lock_guard<std::mutex> lock(g_wavMutex);
      g_wavs[name] = std::move(pcm);
    }
    GLOG("goalfx: sounds read");
  }).detach();
}

void play_named(const std::string& name, float gain) {
  std::lock_guard<std::mutex> lock(g_wavMutex);
  auto it = g_wavs.find(name);
  if (it == g_wavs.end() || it->second.empty()) return;
  PortCardSound(it->second.data(), static_cast<int>(it->second.size()), gain);
}
void play_sound(const Def* d, int clip, float gain) { play_named(std::string(d->item.id) + "_" + std::to_string(clip), gain); }

void buzz(float amp, float seconds) {
  std::lock_guard<std::mutex> lock(g_showMutex);
  g_buzzAmp = std::max(g_buzzAmp, amp);
  g_buzzSec = std::max(g_buzzSec, seconds);
}

// ------------------------------------------------------------------------------------------------------------------
// Spawning
// ------------------------------------------------------------------------------------------------------------------
uint32_t part_cap() { return static_cast<uint32_t>(std::max(400.0f, static_cast<float>(kMaxParts) * xrfx::load_scale())); }

uint32_t pick(Show& s, uint32_t c) {
  if (c < kPal) return c;
  const std::vector<uint32_t>& p = palette(static_cast<int>(c & 0xFF));
  return p[static_cast<size_t>(s.rng.i(static_cast<int>(p.size())))];
}

// A piece of `look` with its look's ways (glow, how it turns, what the floor does to it)
P* spawn(Show& s, int look, V3 p, V3 v, float size, float life, uint32_t c1, uint32_t c2) {
  if (g_parts.size() >= part_cap()) return nullptr;
  g_parts.emplace_back();
  P& q = g_parts.back();
  q.p = p;
  q.v = v;
  q.size = size;
  q.life = std::max(0.05f, life);
  q.fadeOut = std::max(0.05f, 0.3f * q.life);
  q.c1 = pick(s, c1);
  q.c2 = pick(s, c2);
  q.phase = s.rng.f(0.0f, 6.2831853f);
  q.shape = look;
  if (look >= 200) {
    // the celebration's 3D pieces (xr_fx meshes): their celebration shape and mesh
    int shape = kShPaper, variant = s.rng.i(3);
    switch (look) {
      case kMxPaper: shape = kShPaper; break;
      case kMxFoil: shape = kShMetal; break;
      case kMxStreamer: shape = kShPaper; variant = 3; break;
      case kMxCube: shape = 128; break;
      case kMxIce: shape = 52; break;
      case kMxIcicle: shape = 53; break;
      case kMxShard: shape = 57; break;
      case kMxRock: shape = 45; break;
      case kMxStar: shape = kShStar; break;
      case kMxCoin: shape = kShCoin; break;
      case kMxGem: shape = 113; break;
      case kMxLeaf: shape = 106; break;
      case kMxFish: shape = 122; variant = 0; break;  // r161: xr_fx's kFishPiece (its head along +x, its side +z)
      default: break;
    }
    q.shape = shape;
    q.mesh = xrfx::piece_mesh(shape, variant);
    if (q.mesh < 0) q.shape = kShSpark;  // (no mesh: a spark)
    q.orient = kOrFixed;
    const V3 ax = s.rng.unit();
    q.axis = norm(ax, V3{0.0f, 1.0f, 0.0f});
    q.spin = s.rng.f(2.0f, 6.0f);
    q_axis_angle(norm(s.rng.unit()), s.rng.f(0.0f, 6.2831853f), q.q);
    q.glow = (look == kMxIce || look == kMxIcicle || look == kMxShard || look == kMxGem) ? 0.35f : 0.0f;
    q.floor = (look == kMxPaper || look == kMxFoil || look == kMxStreamer || look == kMxLeaf) ? kFlSettle
              : (look == kMxIcicle ? kFlShatter : kFlBounce);
    if (look == kMxStreamer) q.aspect = s.rng.f(0.05f, 0.08f);
    if (look == kMxPaper || look == kMxFoil) q.aspect = s.rng.f(0.45f, 0.75f);
    if (look == kMxIcicle) {  // icicles fall point down
      q.orient = kOrAlong;
      q.spin = 0.0f;
    }
    if (look == kMxFish) {  // r161: a fish swims (its room turns it), the floor isn't its
      q.floor = kFlNone;
      q.spin = 0.0f;
    }
    return &q;
  }
  switch (look) {
    case kShFirePuff: q.shape = kShPuff; q.glow = 0.85f; q.peak = 0.5f; q.c2a = static_cast<uint8_t>(s.rng.i(256)); break;
    case kShPuff: q.glow = 0.0f; q.peak = 0.45f; q.c2a = static_cast<uint8_t>(s.rng.i(256)); break;
    case kShStreak: q.orient = kOrAlong; q.aspect = 0.06f; q.glow = 1.0f; q.floor = kFlDie; break;
    case kShDrop: q.orient = kOrAlong; q.aspect = 0.6f; q.glow = 0.3f; q.floor = kFlDie; break;
    case kShBolt: q.orient = kOrAlongFixed; q.flicker = true; break;
    case kShRing: case kShWave: q.ringFade = true; q.fadeIn = 0.0f; break;
    case kShHex: q.ringFade = true; q.fadeIn = 0.0f; break;
    case kShFlake: q.orient = kOrFaceRoll; q.rollSpeed = s.rng.f(-1.5f, 1.5f); q.roll = s.rng.f(0.0f, 6.28f); q.glow = 0.6f; q.floor = kFlDie; break;
    case kShFlame:
      q.glow = 0.9f;
      q.anim = kAnFlame;
      q.aspect = 0.72f;
      q.c2a = static_cast<uint8_t>(s.rng.i(256));
      q.floor = kFlNone;
      break;
    case kShWisp: q.glow = 1.0f; q.aspect = 0.7f; break;
    case kShTwinkle: q.glow = 1.0f; q.floor = kFlDie; break;
    case kShSpark: q.glow = 1.0f; q.floor = kFlDie; break;
    case kShStar: q.orient = kOrFaceRoll; q.rollSpeed = s.rng.f(-3.0f, 3.0f); q.glow = 0.5f; break;
    case kShPetal:
    case kShCoin:
    case kShPaper:
    case kShMetal:
    case kShCard:
      q.orient = kOrFixed;
      q.axis = norm(s.rng.unit(), V3{0.0f, 1.0f, 0.0f});
      q.spin = s.rng.f(3.0f, 8.0f);
      q_axis_angle(norm(s.rng.unit()), s.rng.f(0.0f, 6.2831853f), q.q);
      q.glow = 0.0f;
      if (look == kShCard) {
        q.aspect = 0.714f;  // a card: 0.12 x 0.168
        q.glow = 0.08f;
        q.c2a = static_cast<uint8_t>(s.rng.i(256));
      }
      if (look == kShPetal) q.aspect = 0.55f;
      break;
    case kShBomb: q.glow = 0.0f; q.orient = kOrFaceRoll; q.c2a = 0; break;
    case kShSmoke: q.glow = 0.0f; q.ringFade = true; q.fadeIn = 0.0f; q.c2a = static_cast<uint8_t>(s.rng.i(256)); q.peak = 0.85f; break;
    case kShPortal: q.glow = 0.9f; break;
    case kShChomper: q.glow = 0.0f; q.orient = kOrUpright; break;
    case kShColumn: q.glow = 0.0f; q.orient = kOrUpright; break;
    case kShStem: q.glow = 0.0f; q.orient = kOrUpright; break;
    case kShShard: q.glow = 0.3f; q.orient = kOrFixed; q.c2a = static_cast<uint8_t>(s.rng.i(4) * 64 + 16); q.peak = 0.82f; break;
    case kShPixel: q.glow = 0.25f; q.orient = kOrFaceRoll; q.c2a = kPixBlock; break;
    // r161: the takeovers' own looks
    case kShQBlock: q.glow = 0.15f; q.orient = kOrFaceRoll; q.c2a = 0; break;
    case kShInvader: q.glow = 0.35f; q.orient = kOrFaceRoll; break;
    case kShFin: q.glow = 0.0f; q.orient = kOrUpright; q.c2a = 0; break;
    case kShBall: q.glow = 0.2f; q.orient = kOrFaceRoll; break;
    case kShTile: case kShWater: case kShCrater:
      q.glow = look == kShTile ? 0.6f : (look == kShWater ? 0.12f : 0.0f);
      q.orient = kOrFixed;
      q.spin = 0.0f;
      q_flat(0.0f, q.q);
      q.a3 = 1.0f;  // (lying round you: never shrunk near your eyes)
      if (look == kShCrater) q.c2a = 255;
      break;
    case kShHole: q.glow = 0.0f; q.a3 = 1.0f; break;
    case kShCone: q.glow = 1.0f; q.orient = kOrAlongFixed; q.a3 = 1.0f; break;
    case kShSpiral: q.glow = 0.9f; break;
    case kShBubble: q.glow = 0.5f; break;
    default: break;
  }
  return &q;
}

V3 anchor(const Show& s, At at) {
  switch (at) {
    case kAtGoal: return s.picture ? s.O + s.D * 0.03f : s.O;
    case kAtHead: return s.H;
    case kAtFloor: return V3{s.H.x, s.floor, s.H.z};
    case kAtFront: return s.front;
    case kAtSide: return s.side;
    case kAtTop: return V3{s.H.x, s.top - 0.1f * s.u, s.H.z};
    case kAtCab: return s.cabTop;                                          // r207
    case kAtAbove:                                                        // r207 (r219: a pull's - over its word, under your ceiling)
      return s.pull ? V3{s.stage.x, std::min(s.stage.y + 0.55f, s.top - 0.25f), s.stage.z} : s.cabTop + V3{0.0f, 0.85f * s.mScale, 0.0f};
  }
  return s.H;
}
float unit_of(const Show& s, At at) { return at == kAtGoal || at == kAtCab || at == kAtAbove ? s.g : s.u; }

// A direction out of an anchor: in the picture's plane on the big screen (a little out of it), else round the goal's
// outward side; round you from your own anchors (above the floor from the floor's)
V3 burst_dir(Show& s, At at) {
  V3 d = s.rng.unit();
  if (at == kAtGoal && s.picture) {
    const V3 w = s.S1 * d.x + s.S2 * d.y + s.D * (0.25f * std::fabs(d.z));
    return norm(w, s.D);
  }
  if (at == kAtGoal) return norm(d + s.D * 0.9f, s.D);
  if (at == kAtFront || at == kAtSide || at == kAtFloor) d.y = std::fabs(d.y) * 0.9f + 0.1f;
  return norm(d, V3{0.0f, 1.0f, 0.0f});
}

// The room's spot `r` metres out round you at height y, never in the cone toward the goal (`gap` radians either side)
V3 around_you(Show& s, float rmin, float rmax, float y0, float y1, float gap) {
  const float toward = std::atan2(s.fwd.z, s.fwd.x);
  float a = 0.0f;
  for (int tries = 0; tries < 6; ++tries) {
    a = s.rng.f(0.0f, 6.2831853f);
    float da = std::fabs(std::remainder(a - toward, 6.2831853f));
    if (da > gap) break;
  }
  const float r = std::sqrt(s.rng.f(rmin * rmin, rmax * rmax));
  return V3{s.H.x + std::cos(a) * r, s.rng.f(y0, y1), s.H.z + std::sin(a) * r};
}

// One jagged bolt from a to b (`width` wide, `life`), with a branch or two
void bolt(Show& s, V3 a, V3 b, float width, float life, uint32_t c1, uint32_t c2, int branches) {
  constexpr int kPts = 9;
  V3 pts[kPts];
  const float l = len(b - a);
  const V3 dir = norm(b - a);
  V3 side = norm(cross(dir, V3{0.0f, 1.0f, 0.0f}), V3{1.0f, 0.0f, 0.0f});
  const V3 side2 = cross(dir, side);
  for (int i = 0; i < kPts; ++i) {
    const float t = static_cast<float>(i) / (kPts - 1);
    const float amp = (i == 0 || i == kPts - 1) ? 0.0f : 0.09f * l * std::sin(3.14159f * t);
    pts[i] = a + (b - a) * t + side * s.rng.f(-amp, amp) + side2 * s.rng.f(-amp, amp);
  }
  auto seg = [&](V3 p0, V3 p1, float w) {
    const V3 d = p1 - p0;
    const float sl = len(d);
    if (sl < 1e-3f) return;
    P* q = spawn(s, kShBolt, (p0 + p1) * 0.5f, V3{}, sl * 1.1f, life, c1, c2);
    if (q == nullptr) return;
    q->aspect = w / (sl * 1.1f);
    q->axis = d * (1.0f / sl);
    q->fadeIn = 0.0f;
    q->fadeOut = life;
  };
  for (int i = 0; i + 1 < kPts; ++i) seg(pts[i], pts[i + 1], width);
  for (int k = 0; k < branches; ++k) {
    V3 p = pts[2 + s.rng.i(kPts - 4)];
    const V3 bd = norm(dir + side * s.rng.f(-1.2f, 1.2f) + side2 * s.rng.f(-0.6f, 0.6f));
    for (int j = 0; j < 3; ++j) {
      const V3 n = p + bd * (0.11f * l) + side * s.rng.f(-0.03f, 0.03f) * l;
      seg(p, n, width * 0.55f);
      p = n;
    }
  }
}

// A lightning strike from the top of the room (or over a column) down to `ground`
void strike(Show& s, V3 ground, float topY, float width, float flash, uint32_t c1, uint32_t c2) {
  const V3 from{ground.x + s.rng.f(-0.25f, 0.25f) * s.u, topY, ground.z + s.rng.f(-0.25f, 0.25f) * s.u};
  const float life = s.rng.f(0.22f, 0.34f);
  bolt(s, from, ground, width, life, c1, c2, 2);
  if (flash > 0.0f) {
    if (P* v = spawn(s, kShFlash, s.H, V3{}, 3.0f, 0.2f, c2, c2)) {
      v->orient = kOrVeil;
      v->peak = flash;
      v->glow = 1.0f;
      v->fadeIn = 0.0f;
      v->fadeOut = 0.2f;
    }
  }
  for (int i = 0; i < 16; ++i) {
    V3 d = s.rng.unit();
    d.y = std::fabs(d.y);
    const float sp = s.rng.f(0.6f, 1.6f) * s.u;
    if (P* q = spawn(s, kShSpark, ground, d * sp, 0.014f * s.u * s.rng.f(0.7f, 1.3f), s.rng.f(0.3f, 0.6f), c1, c2)) {
      q->drag = 2.0f;
      q->grav = 0.5f * G(s, s.u);
    }
  }
  if (P* r = spawn(s, kShRing, V3{ground.x, s.floor + 0.01f, ground.z}, V3{}, 0.12f * s.u, 0.45f, c1, c2)) {
    r->orient = kOrFlat;
    r->grow = 2.4f * s.u / 0.45f;
  }
  buzz(0.55f, 0.08f);
}

void room_start(Show& s, int kind, float dur);  // r161 (below: the room takeovers)

// ------------------------------------------------------------------------------------------------------------------
// The emitters (see the table at the top). `n` already scaled by the load.
// ------------------------------------------------------------------------------------------------------------------
void emit(Show& s, const Beat& b, int n, int index) {
  const float U = unit_of(s, b.at);
  V3 at = anchor(s, b.at);
  const float G0 = G(s, U);
  // what bursts from a spot on the floor bursts from a little over it
  const bool floorSpot = b.at == kAtFront || b.at == kAtSide || b.at == kAtFloor;
  if (floorSpot && (b.em == kFlash || b.em == kBurst || b.em == kRays || b.em == kPuffs)) at.y += 0.12f * s.u;
  switch (b.em) {
    case kFlash: {
      for (int i = 0; i < n; ++i) {
        P* q = spawn(s, b.shape, at, V3{}, b.size * U * s.rng.f(0.9f, 1.1f), b.life, b.c1, b.c2);
        if (q == nullptr) return;
        q->glow = b.a;
        q->fadeIn = 0.02f;
        q->fadeOut = b.life * 0.7f;
        q->grow = b.b * q->size / b.life;
        q->inPicture = b.at == kAtGoal && s.picture;
        if (b.shape == kShPixel) {
          q->anim = kAnPop;
          q->a0 = q->size;
          q->c2a = kPixBurst;
          q->orient = kOrFaceRoll;
          q->grow = 0.0f;
        }
        if (b.shape == kShTwinkle) {
          q->orient = kOrFaceRoll;
          q->roll = 0.785f * static_cast<float>(s.rng.i(2));
        }
      }
      break;
    }
    case kBurst: {
      for (int i = 0; i < n; ++i) {
        const V3 d = burst_dir(s, b.at);
        const float sp = b.speed * U * s.rng.f(0.45f, 1.0f);
        P* q = spawn(s, b.shape, at, d * sp, b.size * U * s.rng.f(0.7f, 1.3f), b.life * s.rng.f(0.75f, 1.15f), b.c1, b.c2);
        if (q == nullptr) return;
        q->drag = b.a;
        q->grav = b.b * G0;
        q->inPicture = b.at == kAtGoal && s.picture;
        if (b.shape == kShPixel) q->c2a = kPixBlock;
        if (b.shape == kShFlame) q->grav = b.b * G0;  // (a negative share rises)
        if (b.b >= 0.9f && q->floor == kFlDie && b.shape == kShSpark) q->floor = kFlBounce;  // embers
      }
      break;
    }
    case kRings: {
      for (int i = 0; i < n; ++i) {
        V3 p = at;
        if (b.speed > 0.0f) {
          const float a = s.rng.f(0.0f, 6.2831853f), r = b.speed * U * std::sqrt(s.rng.f(0.0f, 1.0f));
          p = p + V3{std::cos(a) * r, 0.0f, std::sin(a) * r};
        }
        if (b.a < 0.5f && b.at != kAtGoal) p.y = (b.at == kAtHead ? s.floor : p.y) + 0.012f + 0.004f * static_cast<float>(i);
        if (b.a >= 0.5f && b.at == kAtHead) p = s.H + s.fwd * (1.1f * s.u);  // facing you: a step in front of you
        if (b.a >= 0.5f && floorSpot) p.y = s.floor + 0.45f * s.u;            // (standing on the floor, not in it)
        const float life = b.life;
        P* q = spawn(s, b.shape, p, V3{0.0f, b.b * U, 0.0f}, b.size * U * 0.12f, life, b.c1, b.c2);
        if (q == nullptr) return;
        q->grow = b.size * U * 0.88f / life;
        q->ringFade = true;
        q->fadeIn = 0.0f;
        q->fadeOut = life;
        q->glow = (b.shape == kShSmoke || b.shape == kShFlake) ? (b.shape == kShFlake ? 0.6f : 0.0f) : 1.0f;
        if (b.shape == kShPixel) q->c2a = kPixRing;
        if (b.at == kAtGoal && s.picture) {
          q->orient = kOrFixed;
          q->spin = 0.0f;
          quat_basis(s.S1, s.S2, s.D, q->q);
          q->inPicture = true;
        } else if (b.a < 0.5f) {
          q->orient = kOrFixed;
          q->spin = 0.0f;
          q_flat(s.rng.f(0.0f, 6.28f), q->q);
        } else {
          q->orient = kOrFaceRoll;
          q->rollSpeed = b.shape == kShHex ? 0.8f : 0.0f;
        }
        if (b.shape == kShFlake) {
          q->ringFade = false;  // frost: grows, holds, fades at the end
          q->fadeIn = 0.15f;
          q->fadeOut = 0.6f;
          q->peak = 0.7f;
        }
        if (b.shape == kShSmoke) q->peak = 0.85f;
      }
      break;
    }
    case kRays: {
      for (int i = 0; i < n; ++i) {
        const V3 d = burst_dir(s, b.at);
        const float l = b.size * U * s.rng.f(0.6f, 1.1f);
        P* q = spawn(s, b.shape, at + d * (0.5f * l), V3{}, l, b.life * s.rng.f(0.8f, 1.1f), b.c1, b.c2);
        if (q == nullptr) return;
        q->orient = kOrAlongFixed;
        q->axis = d;
        q->aspect = b.a > 0.0f ? b.a : 0.05f;
        q->fadeIn = 0.0f;
        q->fadeOut = q->life;
        q->inPicture = b.at == kAtGoal && s.picture;
      }
      break;
    }
    case kBolts: {
      for (int i = 0; i < n; ++i) {
        const V3 d = burst_dir(s, b.at);
        bolt(s, at, at + d * (b.size * U * s.rng.f(0.6f, 1.1f)), b.a * U, b.life, b.c1, b.c2, 1);
      }
      break;
    }
    case kSweep: {
      // from its anchor through your head and on: a wave (its centre flying, growing to `size` round you), and on the
      // floor a ring sweeping out at the same speed
      const V3 from = at;
      const V3 to = s.H;
      const float sp = s.def->waveSpeed * s.u;
      const float dist = std::max(0.2f, len(to - from));
      const V3 dir = norm(to - from);
      const float life = (dist + 1.4f * s.u) / sp;
      const float size0 = 0.6f * (b.at == kAtGoal ? s.g : s.u * 0.4f), size1 = b.size * s.u;
      if (P* q = spawn(s, b.shape, from, dir * sp, size0, life, b.c1, b.c2)) {
        q->grow = (size1 - size0) / std::max(0.08f, dist / sp);
        q->orient = kOrFixed;
        q->spin = 0.0f;
        q_plane(dir * -1.0f, q->q);
        q->ringFade = false;
        q->fadeIn = 0.04f;
        q->fadeOut = 0.35f;
        q->peak = 0.9f;
        q->a3 = 1.0f;  // (never shrunk near your eyes: it passes round you)
        if (b.shape == kShPixel) q->c2a = kPixRing;
      }
      if (b.a > 0.5f) {
        const V3 c{from.x, s.floor + 0.01f, from.z};
        const float l2 = (len(V3{to.x - c.x, 0.0f, to.z - c.z}) + 1.6f * s.u) / sp;
        if (P* q = spawn(s, b.shape, c, V3{}, 0.1f, l2, b.c1, b.c2)) {
          q->grow = sp / 0.465f;  // its edge (0.93 of its half size) at the wave's speed
          q->orient = kOrFixed;
          q->spin = 0.0f;
          q_flat(0.0f, q->q);
          q->ringFade = false;
          q->fadeIn = 0.03f;
          q->fadeOut = 0.4f;
          q->peak = 0.8f;
          if (b.shape == kShPixel) q->c2a = kPixRing;
        }
      }
      break;
    }
    case kStreaks: {
      for (int i = 0; i < n; ++i) {
        const V3 p = at + s.rng.unit() * (0.3f * U);
        const V3 dir0 = norm(s.H - p);
        V3 side = norm(cross(dir0, V3{0.0f, 1.0f, 0.0f}), V3{1.0f, 0.0f, 0.0f});
        const V3 side2 = cross(dir0, side);
        const float a = s.rng.f(0.0f, 6.2831853f), r = s.rng.f(0.35f * s.u, std::max(0.4f, b.a) * s.u);
        const V3 target = s.H + side * (std::cos(a) * r) + side2 * (std::sin(a) * r);
        const V3 v = norm(target - p) * (b.speed * s.u * s.rng.f(0.8f, 1.2f));
        const float life = (len(target - p) + 2.0f * s.u) / std::max(0.1f, len(v));
        P* q = spawn(s, b.shape, p, v, b.size * s.u * s.rng.f(0.7f, 1.2f), life, b.c1, b.c2);
        if (q == nullptr) return;
        q->fadeIn = 0.05f;
        q->fadeOut = 0.25f;
        q->floor = kFlNone;
      }
      break;
    }
    case kVeil: {
      if (P* q = spawn(s, kShFlash, s.H, V3{}, 3.0f, b.life, b.c1, b.c1)) {
        q->orient = kOrVeil;
        q->glow = b.size;
        q->peak = b.a;
        q->fadeIn = b.b;
        q->fadeOut = std::min(0.6f, 0.5f * b.life);
        q->a3 = 1.0f;
      }
      break;
    }
    case kRain: {
      for (int i = 0; i < n; ++i) {
        const float a = s.rng.f(0.0f, 6.2831853f), r = 0.95f * s.R * std::sqrt(s.rng.f(0.0f, 1.0f));
        const V3 p{s.H.x + std::cos(a) * r, s.top - s.rng.f(0.0f, 0.3f) * s.u, s.H.z + std::sin(a) * r};
        P* q = spawn(s, b.shape, p, V3{0.0f, -b.speed * s.u, 0.0f}, b.size * s.u * s.rng.f(0.8f, 1.25f),
                     b.life * s.rng.f(0.85f, 1.15f), b.c1, b.c2);
        if (q == nullptr) return;
        if (b.b > 0.0f) {
          q->grav = b.b * G(s, s.u);
        } else {
          q->fall = b.speed * s.u * s.rng.f(0.8f, 1.2f);
          q->flutter = b.a * s.u;
        }
        q->fadeIn = 0.15f;
        if (b.shape == kShPixel) q->c2a = kPixBlock;
      }
      break;
    }
    case kRise: {
      for (int i = 0; i < n; ++i) {
        const float a = s.rng.f(0.0f, 6.2831853f), r = s.R * std::sqrt(s.rng.f(0.02f, 1.0f));
        const V3 p{s.H.x + std::cos(a) * r, s.floor + 0.02f, s.H.z + std::sin(a) * r};
        P* q = spawn(s, b.shape, p, V3{0.0f, b.speed * s.u * s.rng.f(0.6f, 1.4f), 0.0f}, b.size * s.u * s.rng.f(0.7f, 1.3f),
                     b.life * s.rng.f(0.7f, 1.2f), b.c1, b.c2);
        if (q == nullptr) return;
        q->flutter = b.a * s.u;
        q->floor = kFlNone;
        q->fadeIn = 0.2f;
        q->fadeOut = 0.5f * q->life;
      }
      break;
    }
    case kFountain: {
      for (int i = 0; i < n; ++i) {
        const float a = s.rng.f(0.0f, 6.2831853f), r = std::max(0.3f, b.a * s.u * std::sqrt(s.rng.f(0.05f, 1.0f)));
        const V3 land{s.H.x + std::cos(a) * r, s.floor + 0.02f, s.H.z + std::sin(a) * r};
        const V3 p = at + s.rng.unit() * (0.2f * s.g);
        const float tf = b.speed * s.rng.f(0.8f, 1.25f);
        const float gr = std::max(0.2f, b.b) * G(s, s.u);
        const V3 v = (land - p) * (1.0f / tf) + V3{0.0f, 0.5f * gr * tf, 0.0f};
        P* q = spawn(s, b.shape, p, v, b.size * s.u * s.rng.f(0.8f, 1.25f), b.life * s.rng.f(0.85f, 1.15f), b.c1, b.c2);
        if (q == nullptr) return;
        q->grav = gr;
        q->fadeIn = 0.05f;
      }
      break;
    }
    case kStrikes: {
      for (int i = 0; i < n; ++i) {
        V3 ground;
        float topY = s.top;
        if (b.b > 1.5f) {
          ground = V3{s.front.x, s.floor + 0.01f, s.front.z};
        } else if (b.b > 0.5f && s.colCount > 0) {
          const int c = index % s.colCount;
          ground = s.colTop[c];
          topY = std::max(s.top, ground.y + 1.2f * s.u);
        } else {
          // where you see them: in front of you and to your sides (24-110 degrees off the way to the goal, which stays
          // clear), the odd one further round
          const float toward = std::atan2(s.fwd.z, s.fwd.x);
          const float a = toward + (s.rng.i(2) == 0 ? 1.0f : -1.0f) * s.rng.f(0.42f, s.rng.i(4) == 0 ? 2.8f : 1.9f);
          const float r = s.R * s.rng.f(0.35f, 0.95f);
          ground = V3{s.H.x + std::cos(a) * r, s.floor + 0.01f, s.H.z + std::sin(a) * r};
        }
        strike(s, ground, topY, b.size * s.u, b.a, b.c1, b.c2);
      }
      break;
    }
    case kShell: {
      const bool big = b.shape == kShPuff || b.shape == kShFirePuff;
      for (int i = 0; i < n; ++i) {
        V3 d = s.rng.unit();
        d.y = clampf(d.y, -0.35f, 1.0f);
        d = norm(d);
        const float r = b.a * s.u * s.rng.f(0.7f, 1.3f);
        V3 p = s.H + d * r;
        p.y = std::max(p.y, s.floor + 0.05f);
        P* q = spawn(s, b.shape, p, d * (b.speed * s.u), b.size * s.u * s.rng.f(0.6f, 1.4f), b.life * s.rng.f(0.8f, 1.2f),
                     b.c1, b.c2);
        if (q == nullptr) return;
        q->orbit = b.b;
        q->fadeIn = big ? 0.4f : 0.2f;
        q->fadeOut = 0.45f * q->life;
        if (q->mesh >= 0) {
          q->grav = 0.7f * G(s, s.u);
          q->fadeIn = 0.05f;
        }
        if (big) q->peak = b.shape == kShFirePuff ? 0.32f : 0.35f;
      }
      break;
    }
    case kWhirl: {
      for (int i = 0; i < n; ++i) {
        const float a = s.rng.f(0.0f, 6.2831853f), r = b.a * s.u * s.rng.f(0.55f, 1.0f);
        const V3 p{s.H.x + std::cos(a) * r, s.floor + s.rng.f(0.05f, 0.92f) * (s.top - s.floor), s.H.z + std::sin(a) * r};
        P* q = spawn(s, b.shape, p, V3{0.0f, b.speed * s.u * s.rng.f(0.5f, 1.5f), 0.0f}, b.size * s.u * s.rng.f(0.8f, 1.2f),
                     b.life * s.rng.f(0.85f, 1.1f), b.c1, b.c2);
        if (q == nullptr) return;
        q->orbit = b.b * s.rng.f(0.8f, 1.2f);
        q->anim = kAnFling;
        q->a0 = q->life - 0.5f;
        q->fadeIn = 0.15f;
        q->fadeOut = 0.35f;
      }
      break;
    }
    case kFireRing: {
      const V3 c{at.x, s.floor, at.z};
      for (int i = 0; i < n; ++i) {
        const float a = 6.2831853f * (static_cast<float>(i) + s.rng.f(-0.3f, 0.3f)) / static_cast<float>(n);
        const V3 d{std::cos(a), 0.0f, std::sin(a)};
        const float h = b.size * s.u * s.rng.f(0.8f, 1.25f);
        P* q = spawn(s, b.shape, c + d * (b.a * s.u) + V3{0.0f, 0.45f * h, 0.0f}, d * (b.speed * s.u * s.rng.f(0.9f, 1.1f)), h,
                     b.life * s.rng.f(0.85f, 1.1f), b.c1, b.c2);
        if (q == nullptr) return;
        q->fadeIn = 0.12f;
        q->fadeOut = 0.5f;
        q->drag = 0.25f;
      }
      break;
    }
    case kJets: {
      const int jets = std::max(1, static_cast<int>(b.b));
      for (int i = 0; i < n; ++i) {
        const int j = s.rng.i(jets);
        const float a = 6.2831853f * (static_cast<float>(j) + 0.5f) / static_cast<float>(jets) + std::atan2(s.fwd.z, s.fwd.x);
        const V3 base{s.H.x + std::cos(a) * b.a * s.u, s.floor + 0.02f, s.H.z + std::sin(a) * b.a * s.u};
        const float sp = b.speed * s.u;
        const V3 v{s.rng.f(-0.08f, 0.08f) * sp, sp * s.rng.f(0.8f, 1.1f), s.rng.f(-0.08f, 0.08f) * sp};
        P* q = spawn(s, b.shape, base, v, b.size * s.u * s.rng.f(0.8f, 1.3f), b.life * s.rng.f(0.75f, 1.15f), b.c1, b.c2);
        if (q == nullptr) return;
        q->drag = 1.0f;
        q->grav = 0.6f * G(s, s.u);
      }
      break;
    }
    case kColumns: {
      // round you on a ring, leaving the way to the goal clear (a 56 degree gap)
      const float toward = std::atan2(s.fwd.z, s.fwd.x), gap = 0.49f;
      s.colCount = 0;
      for (int i = 0; i < n && i < 12; ++i) {
        const float a = toward + gap + (6.2831853f - 2.0f * gap) * (static_cast<float>(i) + 0.5f) / static_cast<float>(n);
        const V3 base{s.H.x + std::cos(a) * b.a * s.u, s.floor, s.H.z + std::sin(a) * b.a * s.u};
        const float h = b.size * s.u;
        P* q = spawn(s, kShColumn, base + V3{0.0f, 0.1f * h, 0.0f}, V3{}, 0.2f * h, b.life, b.c1, b.c2);
        if (q == nullptr) break;
        q->anim = kAnColumn;
        q->a0 = h;
        q->a1 = std::max(0.1f, b.speed) + 0.06f * static_cast<float>(i);  // they come up one after another
        q->a2 = b.b * s.u;
        q->fadeIn = 0.15f;
        q->fadeOut = 0.4f;
        s.colTop[s.colCount++] = base + V3{0.0f, h, 0.0f};
        // dust where it breaks through
        for (int k = 0; k < 4; ++k) {
          if (P* d = spawn(s, kShPuff, base + V3{s.rng.f(-0.2f, 0.2f) * s.u, 0.1f * s.u, s.rng.f(-0.2f, 0.2f) * s.u},
                           V3{s.rng.f(-0.3f, 0.3f) * s.u, 0.25f * s.u, s.rng.f(-0.3f, 0.3f) * s.u}, 0.45f * s.u, 1.2f, 0xD8CFC0,
                           0x8A8070)) {
            d->grow = 0.4f * s.u;
            d->peak = 0.5f;
            d->fadeIn = 0.1f;
          }
        }
        if (P* r = spawn(s, kShWave, base + V3{0.0f, 0.012f, 0.0f}, V3{}, 0.2f * s.u, 0.7f, b.c2, 0xFFF4C8)) {
          r->grow = 2.2f * s.u / 0.7f;
          r->orient = kOrFixed;
          r->spin = 0.0f;
          q_flat(0.0f, r->q);
          r->fadeOut = 0.7f;
        }
      }
      break;
    }
    case kPlant: {
      // its stem in eight, two leaves, its head (all one plant: their anims share its age)
      const float h = b.size * s.u, head = b.a * s.u;
      for (int k = 0; k < 8; ++k) {
        if (P* q = spawn(s, kShStem, at, V3{}, 0.01f, b.life, 0x3FA83A, 0x1E6A24)) {
          q->anim = kAnStem;
          q->a0 = static_cast<float>(k);
          q->a1 = h;
          q->a2 = head;
          q->fadeIn = 0.0f;
          q->fadeOut = 0.2f;
        }
      }
      for (int k = 0; k < 2; ++k) {
        if (P* q = spawn(s, kShPetal, at, V3{}, 0.01f, b.life, 0x5CC840, 0x2E8A2A)) {
          q->anim = kAnLeaf;
          q->orient = kOrFaceRoll;
          q->spin = 0.0f;
          q->a0 = k == 0 ? -1.0f : 1.0f;
          q->a1 = h;
          q->a2 = head;
          q->aspect = 0.5f;
          q->fadeIn = 0.0f;
          q->fadeOut = 0.2f;
          q->glow = 0.0f;
        }
      }
      if (P* q = spawn(s, kShChomper, at, V3{}, 0.01f, b.life, b.c1, b.c2)) {
        q->anim = kAnHead;
        q->a1 = h;
        q->a2 = head;
        q->fadeIn = 0.0f;
        q->fadeOut = 0.2f;
        q->c2a = 0;
      }
      break;
    }
    case kBomb: {
      if (P* q = spawn(s, kShBomb, anchor(s, kAtGoal), V3{}, b.size * s.u, b.life, b.c1, b.c2)) {
        q->anim = kAnBomb;
        q->a0 = std::max(0.2f, b.dur);  // flight
        q->a1 = b.a * s.u;              // arc
        q->fadeIn = 0.05f;
        q->fadeOut = 0.02f;
        q->rollSpeed = 7.0f;
      }
      break;
    }
    case kShatter: {
      for (int i = 0; i < n; ++i) {
        V3 d;
        for (int tries = 0; tries < 8; ++tries) {
          d = s.rng.unit();
          d.y = clampf(d.y, -0.45f, 0.85f);
          d = norm(d);
          const V3 flat = norm(V3{d.x, 0.0f, d.z});
          if (dot(flat, s.fwd) < std::cos(0.44f) || std::fabs(d.y) > 0.6f) break;  // the way to the game stays clear
        }
        const float r = b.a * s.u * s.rng.f(0.85f, 1.2f);
        const V3 p = s.H + d * r;
        P* q = spawn(s, kShShard, p, V3{}, b.size * s.u * s.rng.f(0.75f, 1.35f), b.life * s.rng.f(0.9f, 1.1f), 0xFFFFFF, 0xFFFFFF);
        if (q == nullptr) return;
        q_facing(p, s.H, s.rng.f(0.0f, 6.2831853f), q->q);
        q->spin = 0.0f;
        q->anim = kAnShard;
        q->a0 = b.b * s.rng.f(0.85f, 1.15f);
        q->a1 = b.speed * s.u;
        q->a2 = s.rng.f(0.0f, 1.0f);
        q->orbit = 0.55f;
        q->fadeIn = 0.04f;
        q->fadeOut = 0.8f;
        q->axis = d;
      }
      break;
    }
    case kGlitter: {
      for (int i = 0; i < n; ++i) {
        V3 p = around_you(s, 0.5f * s.u, s.R, s.floor + 0.1f * s.u, s.top - 0.05f, 0.0f);
        P* q = spawn(s, b.shape, p, V3{0.0f, 0.05f * s.u, 0.0f}, b.size * s.u * s.rng.f(0.6f, 1.3f), b.life * s.rng.f(0.6f, 1.2f),
                     b.c1, b.c2);
        if (q == nullptr) return;
        q->fadeIn = 0.08f;
        q->fadeOut = 0.6f * q->life;
        q->floor = kFlNone;
        if (b.shape == kShPixel) q->c2a = kPixBlock, q->orient = kOrFaceRoll;
      }
      break;
    }
    case kPops: {
      for (int i = 0; i < n; ++i) {
        const V3 p = around_you(s, 0.6f * s.R, 1.0f * s.R, s.floor + 0.3f * s.u, s.top - 0.3f * s.u, 0.42f);
        P* q = spawn(s, kShPixel, p, V3{}, b.size * s.u * s.rng.f(0.7f, 1.3f), b.life, b.c1, b.c2);
        if (q == nullptr) return;
        q->anim = kAnPop;
        q->a0 = q->size;
        q->c2a = kPixBurst;
        q->fadeIn = 0.0f;
        q->fadeOut = 0.3f * b.life;
        q->glow = 0.3f;
        for (int k = 0; k < 6; ++k) {
          if (P* c = spawn(s, kMxCube, p, s.rng.unit() * (1.6f * s.u), 0.05f * s.u, 1.6f, b.c1, 0x202020)) {
            c->grav = 0.8f * G(s, s.u);
          }
        }
        buzz(0.3f, 0.05f);
      }
      break;
    }
    case kAirBursts: {
      const int each = std::max(4, static_cast<int>(b.a * xrfx::load_scale()));
      for (int i = 0; i < n; ++i) {
        const V3 c = around_you(s, 0.0f, 0.7f * s.R, s.top - 0.55f * s.u, s.top - 0.15f * s.u, 0.0f);
        if (P* f = spawn(s, kShSpark, c, V3{}, 0.25f * s.u, 0.18f, 0xFFFFFF, pick(s, b.c1))) f->fadeIn = 0.0f;
        const uint32_t c1 = pick(s, b.c1);
        for (int k = 0; k < each; ++k) {
          P* q = spawn(s, b.shape, c, s.rng.unit() * (b.speed * s.u * s.rng.f(0.6f, 1.0f)), b.size * s.u * s.rng.f(0.8f, 1.25f),
                       b.life * s.rng.f(0.7f, 1.2f), b.shape >= 200 ? b.c1 : c1, b.c2);
          if (q == nullptr) return;
          if (q->mesh >= 0) {
            q->drag = 2.2f;
            q->fall = 0.5f * s.u;
            q->flutter = 0.3f * s.u;
          } else {
            q->drag = 1.6f;
            q->grav = 0.25f * G(s, s.u);
          }
        }
        buzz(0.35f, 0.06f);
      }
      break;
    }
    case kComets: {
      for (int i = 0; i < n; ++i) {
        V3 d = s.rng.unit();
        d.y = s.rng.f(0.15f, 0.7f);
        d = norm(d);
        const V3 p = s.H + d * (0.9f * s.R);
        V3 t = norm(cross(d, V3{0.0f, 1.0f, 0.0f}), V3{1.0f, 0.0f, 0.0f});
        if (s.rng.i(2) == 0) t = t * -1.0f;
        P* q = spawn(s, b.shape, p, (t + V3{0.0f, -0.25f, 0.0f}) * (b.speed * s.u), b.size * s.u, b.life, b.c1, b.c2);
        if (q == nullptr) return;
        q->fadeIn = 0.1f;
        q->fadeOut = 0.3f;
        q->floor = kFlNone;
      }
      break;
    }
    case kPortal: {
      V3 p = at;
      if (b.a > 0.5f) p = V3{s.H.x, s.top - 0.05f * s.u, s.H.z};
      P* q = spawn(s, kShPortal, p, V3{}, b.size * U, b.life, b.c1, b.c2);
      if (q == nullptr) return;
      q->anim = kAnPortal;
      q->a0 = b.size * U;
      q->a1 = b.speed;
      q->fadeIn = 0.2f;
      q->fadeOut = 0.35f;
      q->inPicture = b.at == kAtGoal && s.picture;
      if (b.a > 0.5f) {
        q->orient = kOrFixed;
        q->spin = 0.0f;
        q_plane(V3{0.0f, -1.0f, 0.0f}, q->q);  // facing down at you
      } else if (b.at == kAtGoal && s.picture) {
        q->orient = kOrFixed;
        q->spin = 0.0f;
        quat_basis(s.S1, s.S2, s.D, q->q);
      }
      break;
    }
    case kPuffs: {
      for (int i = 0; i < n; ++i) {
        V3 off = s.rng.unit() * (b.b * U * s.rng.f(0.2f, 1.0f));
        if (b.at != kAtGoal) off.y = std::fabs(off.y) * 0.6f;
        const V3 v = V3{off.x, 0.0f, off.z} * 0.6f + V3{0.0f, b.speed * U * s.rng.f(0.6f, 1.3f), 0.0f};
        P* q = spawn(s, b.shape, at + off, v, b.size * U * s.rng.f(0.7f, 1.3f), b.life * s.rng.f(0.8f, 1.2f), b.c1, b.c2);
        if (q == nullptr) return;
        q->grow = 0.8f * q->size / q->life;
        q->peak = b.a;
        q->drag = 1.2f;
        q->fadeIn = 0.06f;
        q->fadeOut = 0.6f * q->life;
        q->inPicture = b.at == kAtGoal && s.picture;
      }
      break;
    }
    // ---- r207: the CASINO WIN PARTY
    case kCasCoins: {  // out of the payout tray's mouth, along the machine's front and up: they land on your floor, bounce, roll
      const V3 side{s.mFwd.z, 0.0f, -s.mFwd.x};
      for (int i = 0; i < n; ++i) {
        const V3 p = at + side * (s.rng.f(-0.07f, 0.07f) * s.mScale) + V3{0.0f, s.rng.f(0.0f, 0.03f), 0.0f};
        const float out = b.speed * s.rng.f(0.55f, 1.15f) * std::sqrt(s.mScale);
        const V3 v = s.mFwd * out + side * s.rng.f(-0.55f, 0.55f) + V3{0.0f, s.rng.f(1.0f, 2.6f), 0.0f};
        P* q = spawn(s, kMxCoin, p, v, b.size * s.mScale * s.rng.f(0.9f, 1.1f), b.life * s.rng.f(0.85f, 1.15f), b.c1, b.c2);
        if (q == nullptr) return;
        q->cas = 1;
        q->mesh = xrfx::casino_coin_mesh();  // (the light coin)
        q->grav = G(s, 1.0f);
        q->spin = s.rng.f(8.0f, 18.0f);  // (tumbling end over end)
        q->axis = norm(side * s.rng.f(0.7f, 1.0f) + s.rng.unit() * 0.3f, side);
        q->fadeIn = 0.0f;
        q->fadeOut = 0.8f;
        q->glow = 0.0f;
        q->b2 = s.rng.f(0.0f, 6.2831853f);  // (the way it lies when it settles)
      }
      break;
    }
    case kCasConfetti: {  // a cannon-load up out of the cabinet's top, then it flutters down round the machine
      for (int i = 0; i < n; ++i) {
        V3 d = s.rng.unit();
        d.y = std::fabs(d.y) * 1.6f + 0.8f;
        d = norm(d + s.mFwd * (s.pull ? 0.8f : 0.35f));  // (r219: out of your hand - away from your face)
        P* q = spawn(s, b.shape, at + V3{0.0f, 0.04f, 0.0f}, d * (b.speed * s.rng.f(0.6f, 1.15f) * std::sqrt(s.mScale)),
                     b.size * s.mScale * s.rng.f(0.75f, 1.3f), b.life * s.rng.f(0.8f, 1.2f), b.c1, b.c2);
        if (q == nullptr) return;
        q->fall = s.rng.f(0.35f, 0.6f);
        q->flutter = s.rng.f(0.25f, 0.5f);
        q->fadeIn = 0.0f;
      }
      break;
    }
    case kCasFirework: {  // a shell bursting over the machine: a flash, a ring, spark trails out + falling
      const V3 side{s.mFwd.z, 0.0f, -s.mFwd.x};
      const V3 c = at + side * (s.rng.f(-0.9f, 0.9f) * s.mScale) + s.mFwd * (s.rng.f(-0.3f, 0.5f) * s.mScale) +
                   V3{0.0f, s.rng.f(-0.1f, 0.55f) * s.mScale, 0.0f};
      const uint32_t c1 = pick(s, b.c1);
      if (P* f = spawn(s, kShSpark, c, V3{}, 0.36f * s.mScale, 0.20f, 0xFFFFFF, c1)) f->fadeIn = 0.0f, f->glow = 1.0f;
      if (P* r = spawn(s, kShRing, c, V3{}, 0.08f * s.mScale, 0.40f, c1, 0xFFFFFF)) {
        r->grow = 1.6f * s.mScale / 0.4f;
        r->orient = kOrFaceRoll;
        r->glow = 1.0f;
      }
      const int each = std::max(8, static_cast<int>(b.a * xrfx::load_scale()));
      for (int k = 0; k < each; ++k) {
        const V3 d = s.rng.unit();
        P* q = spawn(s, b.shape, c, d * (b.speed * s.mScale * s.rng.f(0.75f, 1.05f)), b.size * s.mScale * s.rng.f(0.8f, 1.2f),
                     b.life * s.rng.f(0.75f, 1.15f), c1, b.c2);
        if (q == nullptr) return;
        q->drag = 1.7f;
        q->grav = 0.32f * G(s, 1.0f);
        q->floor = kFlDie;
        q->glow = 1.0f;
        q->fadeOut = 0.5f * q->life;
      }
      buzz(0.25f, 0.05f);
      break;
    }
    case kCasSiren: {  // the siren light on the cabinet's top: the light (3D), its glow, its two beams turning
      if (P* q = spawn(s, kShSpark, at, V3{}, b.size * s.mScale, b.life, b.c1, b.c2)) {
        q->cas = 3;
        q->shape = 50;  // (xr_fx_mesh.frag's themed piece: its surface by aux - the chrome base metal, the lens a shining gem)
        q->mesh = xrfx::siren_mesh();
        q->orient = kOrFixed;
        q->spin = 0.0f;
        q->floor = kFlNone;
        q->fadeIn = 0.0f;
        q->fadeOut = 0.3f;
        q->a0 = b.size * s.mScale;
        q->a1 = b.speed;  // (the beams' turns a second)
        q->a3 = 1.0f;
      }
      if (P* q = spawn(s, kShSpark, at, V3{}, 0.5f * s.mScale, b.life, b.c1, 0xFFFFFF)) {
        q->cas = 5;
        q->floor = kFlNone;
        q->fadeIn = 0.15f;
        q->fadeOut = 0.3f;
        q->peak = 0.7f;
        q->a0 = b.size * s.mScale;
        q->a1 = b.speed;
      }
      for (int k = 0; k < 2; ++k)
        if (P* q = spawn(s, kShCone, at, V3{}, 2.6f * s.mScale, b.life, b.c1, b.c1)) {
          q->cas = 4;
          q->floor = kFlNone;
          q->fadeIn = 0.35f;
          q->fadeOut = 0.3f;
          q->peak = 0.34f;
          q->aspect = 0.26f;
          q->a0 = b.size * s.mScale;
          q->a1 = b.speed;
          q->b0 = static_cast<float>(k);
        }
      break;
    }
    case kCasLetters: {  // the word, letter by letter, rising over the machine (each its own piece: laid out round its middle)
      const char* word = s.pull ? s.word : cas_word(b.n);  // (r219: a pull's own word)
      float widths[16] = {}, total = 0.0f;
      const int len = static_cast<int>(std::strlen(word));
      const float gap = 0.10f, space = 0.42f;
      for (int i = 0; i < len && i < 16; ++i) {
        float wdt = 0.0f;
        if (word[i] != ' ') xrfx::glyph_mesh(word[i], &wdt);
        widths[i] = word[i] == ' ' ? space : wdt;
        total += widths[i] + (i + 1 < len ? gap : 0.0f);
      }
      const float cap = b.size * s.mScale;
      float x = -0.5f * total;
      int shown = 0;
      for (int i = 0; i < len && i < 16; ++i) {
        const float mid = x + 0.5f * widths[i];
        x += widths[i] + gap;
        if (word[i] == ' ') continue;
        float wdt = 0.0f;
        const int mesh = xrfx::glyph_mesh(word[i], &wdt);
        if (mesh < 0) continue;
        P* q = spawn(s, kShSpark, at, V3{}, cap, b.life, b.c1, b.c2);
        if (q == nullptr) return;
        q->cas = 2;
        q->shape = 1;  // (xr_fx_mesh.frag: all metal - the gold face, its sides in colour 2)
        q->mesh = mesh;
        q->orient = kOrFixed;
        q->spin = 0.0f;
        q->floor = kFlNone;
        q->fadeIn = 0.0f;
        q->fadeOut = 0.45f;
        q->glow = 0.0f;
        q->a0 = cap;
        q->a1 = mid * cap;                      // (its place along the word, metres)
        q->a2 = static_cast<float>(shown++);   // (its turn: they rise one after another)
        q->b1 = static_cast<float>(b.n);
      }
      break;
    }
    case kCasPulse: {  // the room's gold flash: the passthrough's brightness (casino_flash) + a faint gold veil in the effects
      for (P& v : g_parts)  // (one veil at a time: the last pulse's goes as this one comes - a veil is a whole screen of fill)
        if (v.orient == kOrVeil && v.b3 == 7.0f) v.life = std::min(v.life, v.age);
      if (s.pulses < 16) {
        s.pulseAt[s.pulses] = s.age;
        s.pulsePeak[s.pulses] = b.a;
        ++s.pulses;
      }
      if (P* q = spawn(s, kShFlash, s.H, V3{}, 3.0f, b.life, b.c1, b.c1)) {
        q->orient = kOrVeil;
        q->glow = 1.0f;
        q->peak = 0.2f * b.a;
        q->fadeIn = 0.03f;
        q->fadeOut = std::max(0.05f, b.life - 0.03f);
        q->a3 = 1.0f;
        q->b3 = 7.0f;  // (the party's veil)
      }
      break;
    }
    case kBuzz:
      buzz(b.a * (s.theirs ? 0.6f : 1.0f), b.b);
      break;
    case kSound:
      play_sound(s.def, b.n, b.a > 0.0f ? b.a : 1.0f);
      break;
    case kTakeover:  // r161
      room_start(s, b.n, b.dur);
      break;
  }
}

// How many of a beat at once (pieces scale with the load; the set pieces do not)
int scaled(const Beat& b, int n) {
  switch (b.em) {
    case kSweep: case kVeil: case kColumns: case kPlant: case kBomb: case kPortal: case kBuzz: case kSound: case kTakeover:
    case kCasSiren: case kCasLetters: case kCasPulse:  // r207
      return n;
    case kFireRing: case kShatter: case kStrikes:
      return std::max(1, static_cast<int>(std::lround(n * std::sqrt(xrfx::load_scale()))));
    default:
      return n <= 0 ? 0 : std::max(1, static_cast<int>(std::lround(n * xrfx::load_scale())));
  }
}

// ------------------------------------------------------------------------------------------------------------------
// The anims (set pieces), every frame
// ------------------------------------------------------------------------------------------------------------------
// The plant's timeline: up out of the floor (0-0.45 s), its mouth wide (0.7 s), three snaps (1.0, 1.5, 2.0 s), down
// into the floor over its last 0.55 s
constexpr float kChomps[3] = {1.0f, 1.5f, 2.0f};
float plant_up(float t, float life) { return ease_out(t / 0.45f) * ease_out((life - t) / 0.55f); }
float plant_open(float t) {
  float o = clampf((t - 0.3f) / 0.4f, 0.0f, 1.0f);
  for (float tc : kChomps) {
    if (t > tc - 0.1f && t <= tc) o = std::min(o, (tc - t) / 0.1f);
    else if (t > tc && t <= tc + 0.06f) o = 0.0f;
    else if (t > tc + 0.06f && t <= tc + 0.3f) o = std::min(o, (t - tc - 0.06f) / 0.24f);
  }
  if (t > 2.3f) o = std::min(o, std::max(0.12f, 1.0f - (t - 2.3f) / 0.2f));
  return clampf(o, 0.0f, 1.0f);
}
float plant_lunge(float t) {
  float l = 0.0f;
  for (float tc : kChomps) {
    const float d = t - tc;
    if (d > -0.12f && d < 0.25f) l = std::max(l, d < 0.0f ? (d + 0.12f) / 0.12f : 1.0f - d / 0.25f);
  }
  return l;
}

void animate(Show& s, P& q, float dt, V3 head) {
  const float t = q.age;
  switch (q.anim) {
    case kAnColumn: {
      float e = ease_out(t / q.a1);
      if (t > q.life - 0.5f) e *= ease_out((q.life - t) / 0.5f);
      const float h = q.a0 * (0.12f + 0.88f * e);
      q.p.y = s.floor + 0.5f * h;
      q.size = h;
      q.aspect = q.a2 / h;
      break;
    }
    case kAnStem: case kAnLeaf: case kAnHead: {
      const float up = plant_up(t, q.life);
      const float h = q.a1, headSize = q.a2;
      const float stemH = 0.8f * h * up;
      const V3 base = s.side;
      const V3 toYou = norm(V3{head.x - base.x, 0.0f, head.z - base.z});
      const V3 lat = cross(V3{0.0f, 1.0f, 0.0f}, toYou);
      const float sway = std::sin(t * 2.2f) * 0.06f * s.u;
      if (q.anim == kAnStem) {
        const float k = q.a0, f = (k + 0.5f) / 8.0f;
        const float y = s.floor + stemH * f;
        q.p = V3{base.x, y, base.z} + lat * (sway * f * f) + toYou * (0.08f * s.u * plant_lunge(t) * f * f);
        q.size = std::max(0.01f, stemH / 8.0f * 1.45f);
        q.aspect = (0.17f * s.u * (1.0f - 0.25f * f) * std::max(0.3f, up)) / q.size;
      } else if (q.anim == kAnLeaf) {
        q.p = V3{base.x, s.floor + 0.16f * h * up, base.z} + lat * (q.a0 * 0.2f * s.u * up);
        q.size = std::max(0.01f, 0.55f * s.u * up);
        q.roll = q.a0 * 1.05f;
      } else {
        const float open = plant_open(t);
        const V3 top = V3{base.x, s.floor + stemH + 0.36f * headSize * up, base.z} + lat * sway +
                       toYou * (0.24f * s.u * plant_lunge(t));
        q.p = top;
        q.size = std::max(0.01f, headSize * std::max(0.05f, up) * (1.0f + 0.12f * open));
        q.c2a = static_cast<uint8_t>(open * 255.0f + 0.5f);
        // a snap: leaves and sparks flying, a ring, a buzz
        for (float tc : kChomps) {
          if (t - dt < tc && t >= tc) {
            for (int i = 0; i < 12; ++i) {
              if (P* l = spawn(s, kShPetal, top, (s.rng.unit() + toYou * 0.8f) * (1.6f * s.u), 0.09f * s.u, 1.2f, 0x6CD848, 0x2E8A2A)) {
                l->grav = 0.6f * G(s, s.u);
                l->drag = 1.2f;
              }
            }
            for (int i = 0; i < 10; ++i) {
              if (P* sp = spawn(s, kShSpark, top, s.rng.unit() * (1.4f * s.u), 0.025f * s.u, 0.4f, 0xFFFFFF, 0xFF6060)) sp->drag = 2.5f;
            }
            if (P* r = spawn(s, kShRing, top + toYou * (0.3f * s.u), V3{}, 0.2f * s.u, 0.35f, 0xFFFFFF, 0xFF4040)) {
              r->grow = 2.0f * s.u / 0.35f;
              r->orient = kOrFaceRoll;
              r->ringFade = true;
              r->fadeIn = 0.0f;
              r->fadeOut = 0.35f;
            }
            buzz(0.75f, 0.12f);
          }
        }
      }
      break;
    }
    case kAnBomb: {
      const V3 from = s.picture ? s.O + s.D * 0.05f : s.O;
      const float r = 0.5f * q.size;
      const V3 to = V3{s.front.x, s.floor + r, s.front.z};
      const float tf = q.a0;
      if (t < tf) {
        const float u = t / tf;
        const float arc = std::max(q.a1, 0.3f * (from.y - to.y) + 0.4f * s.u);
        q.p = from + (to - from) * u + V3{0.0f, arc * 4.0f * u * (1.0f - u), 0.0f};
      } else {
        const float h1 = 0.16f * s.u, h2 = 0.06f * s.u;
        const float d = t - tf;
        float y = 0.0f;
        if (d < 0.24f) y = h1 * 4.0f * (d / 0.24f) * (1.0f - d / 0.24f);
        else if (d < 0.38f) y = h2 * 4.0f * ((d - 0.24f) / 0.14f) * (1.0f - (d - 0.24f) / 0.14f);
        q.p = to + V3{0.0f, y, 0.0f};
        q.rollSpeed *= std::exp(-4.0f * dt);
        q.roll *= std::exp(-3.0f * dt);
        if (t - dt < tf) {  // it lands: dust, a thump
          for (int i = 0; i < 4; ++i) {
            if (P* p = spawn(s, kShPuff, to, V3{s.rng.f(-0.4f, 0.4f) * s.u, 0.15f * s.u, s.rng.f(-0.4f, 0.4f) * s.u}, 0.3f * s.u,
                             0.7f, 0xD8D0C8, 0x9A9088)) {
              p->grow = 0.4f * s.u;
              p->peak = 0.4f;
            }
          }
          buzz(0.45f, 0.08f);
        }
      }
      // flashing faster as it goes: lit windows before its end
      const float left = q.life - t;
      bool lit = left < 0.04f;
      for (float w : {0.62f, 0.42f, 0.27f, 0.15f, 0.08f})
        if (left < w && left > w - 0.05f) lit = true;
      q.c2a = lit ? 255 : 0;
      // its fuse fizzes
      const int n = static_cast<int>(55.0f * dt + s.rng.f(0.0f, 1.0f));
      const V3 toYou = norm(head - q.p);
      const V3 rx = norm(cross(V3{0.0f, 1.0f, 0.0f}, toYou), V3{1.0f, 0.0f, 0.0f});
      const float c = std::cos(q.roll), sn = std::sin(q.roll);
      const V3 tip = q.p + (rx * (0.31f * c - 0.48f * sn) + V3{0.0f, 1.0f, 0.0f} * (0.31f * sn + 0.48f * c)) * q.size;
      for (int i = 0; i < n; ++i) {
        if (P* sp = spawn(s, kShSpark, tip, (s.rng.unit() * 0.5f + V3{0.0f, 0.5f, 0.0f}) * s.u, 0.018f * s.u, 0.3f, 0xFFF4C0, 0xFF8A1A)) {
          sp->grav = 0.5f * G(s, s.u);
          sp->drag = 1.5f;
        }
      }
      break;
    }
    case kAnPortal: {
      const float grow = ease_out(t / 0.35f) * clampf((q.life - t) / 0.3f, 0.0f, 1.0f);
      q.size = q.a0 * std::max(0.02f, grow);
      const float turn = t * q.a1;
      q.c2a = static_cast<uint8_t>(static_cast<int>((turn - std::floor(turn)) * 255.0f) & 255);
      break;
    }
    case kAnPop: {
      const float f = t / q.life;
      const int step = std::min(3, static_cast<int>(f * 5.0f));
      static const float kSteps[4] = {0.35f, 0.65f, 0.9f, 1.0f};
      q.size = q.a0 * kSteps[step];
      break;
    }
    case kAnShard: {
      if (!q.released && t >= q.a0) {
        q.released = true;
        q.orbit = 0.0f;
        q.v = q.axis * q.a1 + s.rng.unit() * (0.3f * s.u);
        q.grav = 0.7f * G(s, s.u);
        q.axis = norm(s.rng.unit(), V3{0.0f, 1.0f, 0.0f});
        q.spin = s.rng.f(2.0f, 5.0f);
        q.life = std::min(q.life, t + 1.0f);
      }
      const float hue = q.a2 + t * 0.45f;
      q.c1 = hsv(hue, 0.28f, 1.0f);
      q.c2 = hsv(hue + 0.3f, 0.75f, 1.0f);
      break;
    }
    case kAnFling: {
      if (t > q.a0) {
        const V3 out = norm(V3{q.p.x - s.H.x, 0.0f, q.p.z - s.H.z});
        q.v = q.v + (out * 7.0f + V3{0.0f, 1.0f, 0.0f}) * (s.u * dt);
      }
      break;
    }
    case kAnFlame: {
      q.c2a = static_cast<uint8_t>(static_cast<int>(q.phase * 40.0f + t * 90.0f) & 255);  // its tongues sway
      break;
    }
    default:
      break;
  }
}

// ------------------------------------------------------------------------------------------------------------------
// r161: the ROOM TAKEOVERS (GOAL EXPLOSIONS pack 2). One at a time (Show::room): room_start (kTakeover's beat) puts its
// first pieces up; room_step, every frame, runs its director (what comes next) and then every piece with a role (what it
// does on its own; what your hands and head do to it); then room_clear clears it. What you can touch is sized in metres
// round your head (your reach doesn't grow with the stadium); the room's own things by its unit. Ported line for line to
// tools/outside/goalfx153.py (its proofs drive scripted hands through the same code).
// ------------------------------------------------------------------------------------------------------------------
constexpr float kG = 9.8f;  // (a metre's gravity: the takeovers' pieces are in metres)

// A takeover's sound: the same one at most every 0.1 s, any at most every 0.09 s (the card sounds' four voices)
void sfx(Show& s, int k, float gain) {
  if (s.age - s.sfxAt[k] < 0.1f || s.age - s.sfxAny < 0.09f) return;
  s.sfxAt[k] = s.age;
  s.sfxAny = s.age;
  play_named(std::string("fx_") + kSfxNames[k], clampf(gain, 0.05f, 1.0f) * (s.theirs ? 0.7f : 1.0f));
}

// The hand nearest p within r moving at least minSpeed (m/s); -1 none
int hand_near(const Show& s, V3 p, float r, float minSpeed) {
  int best = -1;
  float bd = r;
  for (int h = 0; h < 2; ++h) {
    const HandSt& hd = s.hand[h];
    if (!hd.on || len(hd.v) < minSpeed) continue;
    const float d = len(hd.p - p);
    if (d < bd) {
      bd = d;
      best = h;
    }
  }
  return best;
}

float seg_dist(V3 p, V3 a, V3 b) {
  const V3 ab = b - a;
  const float t = clampf(dot(p - a, ab) / std::max(1e-6f, dot(ab, ab)), 0.0f, 1.0f);
  return len(a + ab * t - p);
}
float flat_dist(V3 a, V3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z)); }
float turn_to(float from, float to, float most) {
  const float d = std::remainder(to - from, 6.2831853f);
  return from + clampf(d, -most, most);
}

// A piece picked at random among those `ok` says yes to (null: none)
template <typename F>
P* pick_piece(Show& s, F ok) {
  int n = 0;
  for (P& q : g_parts)
    if (ok(q)) ++n;
  if (n == 0) return nullptr;
  int k = s.rng.i(n);
  for (P& q : g_parts)
    if (ok(q) && k-- == 0) return &q;
  return nullptr;
}

// A little burst of n pieces of `look` out of p (gravity in g, drag 1/s, `add` on every piece's speed)
void bits_out(Show& s, int look, V3 p, int n, float speed, float size, float life, uint32_t c1, uint32_t c2, float grav,
              float drag, V3 add) {
  for (int i = 0; i < n; ++i) {
    P* q = spawn(s, look, p, s.rng.unit() * (speed * s.rng.f(0.4f, 1.0f)) + add, size * s.rng.f(0.7f, 1.3f),
                 life * s.rng.f(0.7f, 1.2f), c1, c2);
    if (q == nullptr) return;
    q->grav = grav * kG;
    q->drag = drag;
  }
}
// An 8-bit pop: a pixel burst and pixels flying
void pix_pop(Show& s, V3 p, float size, uint32_t c1, int pixels) {
  if (P* f = spawn(s, kShPixel, p, V3{}, size, 0.3f, c1, 0xFFFFFF)) {
    f->anim = kAnPop;
    f->a0 = size;
    f->c2a = kPixBurst;
    f->fadeIn = 0.0f;
    f->fadeOut = 0.12f;
    f->glow = 0.4f;
  }
  bits_out(s, kShPixel, p, pixels, 1.8f, 0.035f, 0.7f, c1, 0xFFFFFF, 0.45f, 1.2f, V3{});
}
// A ring lying flat at p, growing to `size` over its life
P* flat_ring(Show& s, int look, V3 p, float size, float life, uint32_t c1, uint32_t c2) {
  P* r = spawn(s, look, p, V3{}, 0.08f * size, life, c1, c2);
  if (r == nullptr) return nullptr;
  r->grow = 0.92f * size / life;
  r->orient = kOrFixed;
  r->spin = 0.0f;
  q_flat(s.rng.f(0.0f, 6.28f), r->q);
  r->ringFade = true;
  r->fadeIn = 0.0f;
  r->fadeOut = life;
  r->a3 = 1.0f;
  return r;
}
// A ring facing you at p
void face_ring(Show& s, V3 p, float size, float life, uint32_t c1, uint32_t c2) {
  if (P* r = spawn(s, kShRing, p, V3{}, 0.1f * size, life, c1, c2)) {
    r->grow = 0.9f * size / life;
    r->orient = kOrFaceRoll;
    r->fadeOut = life;
  }
}
// A splash where water (its surface at p.y) is hit: drops thrown up (k 0..1: how hard), two rings, foam
void splash(Show& s, V3 p, float k, V3 push) {
  const int n = static_cast<int>(8.0f + 12.0f * k);
  for (int i = 0; i < n; ++i) {
    V3 d = s.rng.unit();
    d.y = std::fabs(d.y) * 0.6f + 0.6f;
    P* q = spawn(s, kShDrop, p + V3{0.0f, 0.02f, 0.0f}, d * ((1.0f + 1.6f * k) * s.rng.f(0.5f, 1.0f)) + push * 0.35f,
                 0.035f * s.rng.f(0.7f, 1.4f), 1.2f, 0xE8F8FF, 0x3AA8E8);
    if (q == nullptr) break;
    q->grav = kG;
    q->role = kRoDrop;
    q->h0 = p;
  }
  flat_ring(s, kShRing, p + V3{0.0f, 0.01f, 0.0f}, 0.5f + 0.5f * k, 0.7f, 0xFFFFFF, 0x8FDCFF);
  flat_ring(s, kShRing, p + V3{0.0f, 0.012f, 0.0f}, 0.9f + 0.6f * k, 1.0f, 0xE8F8FF, 0x3AA8E8);
  bits_out(s, kShSpark, p, 6, 0.9f * (0.5f + k), 0.03f, 0.35f, 0xFFFFFF, 0xBFE8FF, 0.6f, 1.5f, V3{0.0f, 0.6f, 0.0f});
}
// A swimmer's turn: its nose along `fwd`, its side out (+z), its tail wiggled about the vertical; `roll` about its nose
void q_swim(V3 fwd, float wiggle, float roll, float q[4]) {
  V3 x = norm(fwd, V3{1.0f, 0.0f, 0.0f});
  const float c = std::cos(wiggle), sn = std::sin(wiggle);
  x = norm(V3{x.x * c - x.z * sn, x.y, x.x * sn + x.z * c}, x);
  V3 z = cross(x, V3{0.0f, 1.0f, 0.0f});
  z = len(z) < 1e-4f ? V3{0.0f, 0.0f, 1.0f} : norm(z);
  V3 y = cross(z, x);
  if (roll != 0.0f) {
    const float rc = std::cos(roll), rs = std::sin(roll);
    const V3 y2 = y * rc + z * rs, z2 = z * rc - y * rs;
    y = y2;
    z = z2;
  }
  quat_basis(x, y, z, q);
}
// Twinkling stars round a bopped head
void bonk_stars(Show& s, V3 p) {
  for (int i = 0; i < 6; ++i) {
    const float a = 6.2831853f * static_cast<float>(i) / 6.0f;
    if (P* t = spawn(s, kShStar, p + V3{std::cos(a) * 0.12f, 0.1f, std::sin(a) * 0.12f}, V3{std::cos(a) * 0.3f, 0.5f, std::sin(a) * 0.3f},
                     0.06f, 0.7f, 0xFFE070, 0xFFFFFF)) {
      t->glow = 0.8f;
      t->drag = 1.5f;
    }
  }
}
float room_left(const Show& s) { return s.room.dur - (s.age - s.room.t0); }
uint8_t inv_look(int sprite) { return static_cast<uint8_t>(sprite * 32 + 16); }  // (xr_fx.frag 151: 0-5 aliens A/B, 6 saucer, 7 hit)

// ---- 8-BIT TAKEOVER's pieces
void bits_block(Show& s, V3 p, float delay, float size) {
  P* q = spawn(s, kShQBlock, p, V3{s.rng.f(-0.03f, 0.03f), 0.0f, s.rng.f(-0.03f, 0.03f)}, 0.001f, s.room.dur + 2.0f, 0xFFC020,
               0x3A2008);
  if (q == nullptr) return;
  q->role = kRoBlock;
  q->a0 = size;
  q->a1 = p.y;
  q->b0 = delay;
  q->fadeIn = 0.0f;
  q->fadeOut = 0.25f;
}
void bits_cube(Show& s, V3 p, float delay) {
  P* q = spawn(s, kMxCube, p, V3{s.rng.f(-0.12f, 0.12f), s.rng.f(-0.05f, 0.05f), s.rng.f(-0.12f, 0.12f)}, s.rng.f(0.07f, 0.15f),
               s.room.dur + 1.5f, PAL(kPalPixel), 0x202020);
  if (q == nullptr) return;
  q->role = kRoCube;
  q->floor = kFlNone;
  q->spin = s.rng.f(0.8f, 2.5f);
  q->fadeIn = 0.2f + delay;
  q->b2 = -9.0f;
}
void bits_coin(Show& s, V3 p, V3 v) {
  if (P* c = spawn(s, kMxCoin, p, v, 0.07f, 3.0f, 0xFFD21F, 0xC8901E)) {
    c->role = kRoCoin;
    c->grav = 0.6f * kG;
    c->floor = kFlNone;
    c->axis = V3{0.0f, 1.0f, 0.0f};
    c->spin = 9.0f;
    c->b0 = 0.35f;
  }
}
// A punched ? block: it hops, goes brown, pops coins that fly to your hand
void bits_hit(Show& s, P& q, int h) {
  q.c2a = 255;
  q.a2 = q.age;
  q.a3 = q.age;
  const HandSt& hd = s.hand[h];
  q.v = q.v + V3{hd.v.x, 0.0f, hd.v.z} * 0.08f;
  const int n = 4 + s.rng.i(4);
  for (int k = 0; k < n; ++k) {
    V3 d = s.rng.unit();
    d.y = std::fabs(d.y) + 0.8f;
    bits_coin(s, q.p + V3{0.0f, 0.5f * q.size, 0.0f}, norm(d) * s.rng.f(1.4f, 2.2f));
  }
  pix_pop(s, q.p + V3{0.0f, 0.6f * q.size, 0.0f}, 0.28f, 0xFFD800, 6);
  sfx(s, kSfxBump, 0.9f);
  buzz(0.45f, 0.06f);
  s.room.count[0] += 1;
}

// ---- PIXEL INVADERS' pieces
V3 inv_spot(const Show& s, const P& q) {
  // the formation round you: columns across your front and sides (az), rows down from the top; in front of you (the way
  // to the game) a row only flies over the screen's top, never across it
  const float toward = std::atan2(s.fwd.z, s.fwd.x);
  const float az = toward + s.room.ang + q.a0;
  const float rr = 1.5f;
  float y = std::max(s.floor + 0.35f, s.room.level - q.a1);
  if (std::fabs(std::remainder(az - toward, 6.2831853f)) < 0.5f) y = std::max(y, s.H.y + 0.32f * rr);
  y = std::min(y, s.top - 0.06f);
  return V3{s.H.x + std::cos(az) * rr, y, s.H.z + std::sin(az) * rr};
}
// r182 (Lance: "with the space invaders room explosion, i want the things you obliterate to be worth coins, lets say 15
// coins per invader you destroy per goal"): every invader HE destroys (trigger laser or a punch; the mothership counts as
// one) = 15 real TIZM coins - his own goal only, never a preview or the friend's; xr_cards pays them in a franchise match
// (take_earned), like 8-BIT TAKEOVER's coins. Not capped: 24 aliens + the mothership at most a goal.
constexpr int kInvaderCoins = 15;
void inv_pay(Show& s, V3 p) {
  if (s.preview || s.theirs) return;
  g_earned.fetch_add(kInvaderCoins);
  sfx(s, kSfxCoin, 0.7f);
  bits_out(s, kShTwinkle, p, 4, 0.7f, 0.05f, 0.4f, 0xFFF4C8, 0xFFD21F, 0.0f, 2.0f, V3{});
}
void inv_kill(Show& s, P& q, bool loud) {
  pix_pop(s, q.p, 2.0f * q.size, q.c1, 12);
  if (loud) {
    sfx(s, kSfxZap, 0.8f);
    buzz(0.3f, 0.05f);
    inv_pay(s, q.p);  // r182: his kill (a laser or a punch), not the takeover clearing itself
  } else {
    sfx(s, kSfxBlip, 0.35f);
  }
  s.room.count[0] += 1;
  q.role = kRoNone;
  q.life = q.age;
}
void inv_shot(Show& s, V3 p, float size) {
  const V3 target = around_you(s, 0.4f, 1.1f, s.floor, s.floor, 0.3f);
  if (P* q = spawn(s, kShPixel, p, norm(target - p) * 1.3f, size, 4.0f, 0xFF5050, 0xFFFFFF)) {
    q->role = kRoShot;
    q->aspect = 0.45f;
    q->floor = kFlNone;
    q->glow = 0.5f;
  }
}
void inv_fire(Show& s, int h) {
  const HandSt& hd = s.hand[h];
  if (P* q = spawn(s, kShStreak, hd.aimP + hd.aimD * 0.15f, hd.aimD * 10.0f, 0.34f, 0.55f, 0xFFFFFF, 0x40FF80)) {
    q->role = kRoLaser;
    q->aspect = 0.12f;
    q->fadeIn = 0.0f;
  }
  if (P* f = spawn(s, kShSpark, hd.aimP + hd.aimD * 0.1f, V3{}, 0.09f, 0.08f, 0xFFFFFF, 0x40FF80)) f->fadeIn = 0.0f;
  sfx(s, kSfxLaser, 0.6f);
  buzz(0.15f, 0.03f);
}
void mother_boom(Show& s, P& q) {
  pix_pop(s, q.p, 1.0f, 0xFF3A5A, 30);
  bits_out(s, kMxCoin, q.p, 14, 2.0f, 0.07f, 2.0f, 0xFFD21F, 0xC8901E, 0.8f, 0.5f, V3{});
  face_ring(s, q.p, 1.6f, 0.45f, 0xFFFFFF, 0xFF3A5A);
  play_sound(s.def, 2, 1.0f);
  buzz(0.8f, 0.3f);
  inv_pay(s, q.p);  // r182: the mothership counts as one invader
  s.room.count[0] += 10;
  q.role = kRoNone;
  q.life = q.age;
}

// ---- METEOR STRIKE's pieces
void meteor_spawn(Show& s, bool big) {
  const float toward = std::atan2(s.fwd.z, s.fwd.x);
  V3 target;
  if (big) {
    const float a = toward + (s.rng.i(2) == 0 ? 1.0f : -1.0f) * 0.75f;
    target = V3{s.H.x + std::cos(a) * 1.3f, s.floor, s.H.z + std::sin(a) * 1.3f};
  } else if (s.rng.f(0.0f, 1.0f) < 0.45f) {  // into your reach (beside you, chest high): punch it or catch it
    const float side = s.rng.i(2) == 0 ? 1.0f : -1.0f;
    target = s.H + s.right * (side * s.rng.f(0.32f, 0.55f)) + s.fwd * s.rng.f(-0.1f, 0.35f) + V3{0.0f, -s.rng.f(0.05f, 0.45f), 0.0f};
  } else {
    target = around_you(s, 0.6f, std::min(2.2f, s.R), s.floor, s.floor, 0.4f);
  }
  const V3 from = norm(V3{s.rng.f(-0.6f, 0.6f), 1.0f, s.rng.f(-0.6f, 0.6f)});
  const V3 p = target + from * 3.2f;
  const float sp = big ? 2.6f : s.rng.f(3.2f, 4.2f);
  P* q = spawn(s, kMxRock, p, norm(target - p) * sp, big ? 0.42f : s.rng.f(0.11f, 0.19f), 4.5f, 0x5A4030, 0xFF6A1A);
  if (q == nullptr) return;
  q->role = kRoMeteor;
  q->floor = kFlNone;
  q->glow = 0.35f;
  q->spin = 3.0f;
  q->b3 = big ? 2.0f : 0.0f;
  q->fadeIn = 0.1f;
  sfx(s, kSfxWhoosh, big ? 0.9f : 0.3f);
}
void meteor_shatter(Show& s, P& q, int h, bool big) {
  const V3 hv = h >= 0 ? s.hand[h].v : V3{};
  const int ne = big ? 40 : 14;
  for (int i = 0; i < ne; ++i) {
    P* e = spawn(s, kShSpark, q.p, s.rng.unit() * s.rng.f(0.8f, 2.4f) + hv * 0.35f, 0.03f, s.rng.f(0.5f, 0.9f), 0xFFE070, 0xFF5A10);
    if (e == nullptr) break;
    e->grav = 0.6f * kG;
    e->drag = 1.4f;
    e->floor = kFlBounce;
  }
  bits_out(s, kMxRock, q.p, big ? 14 : 6, 1.8f, 0.035f, 1.4f, 0x4A3628, 0x2A1E16, 1.0f, 0.3f, hv * 0.3f);
  bits_out(s, kShFirePuff, q.p, big ? 10 : 4, 0.8f, big ? 0.35f : 0.16f, 0.5f, 0xFFE070, 0xFF5A10, -0.1f, 3.0f, V3{});
  if (P* f = spawn(s, kShSpark, q.p, V3{}, big ? 0.9f : 0.35f, 0.15f, 0xFFFFFF, 0xFF9A2A)) f->fadeIn = 0.0f;
  sfx(s, kSfxShard, big ? 1.0f : 0.8f);
  buzz(big ? 0.8f : 0.5f, big ? 0.15f : 0.07f);
  s.room.count[0] += big ? 5 : 1;
  q.role = kRoNone;
  q.life = q.age;
  q.held = -1;
}
void meteor_land(Show& s, P& q, bool big, V3 head) {
  const V3 g{q.p.x, s.floor + 0.012f, q.p.z};
  if (P* c = spawn(s, kShCrater, g, V3{}, big ? 1.4f : s.rng.f(0.55f, 0.8f), room_left(s) + 1.2f, 0xFF7A1A, 0x1A1410)) {
    c->role = kRoCrater;
    q_flat(s.rng.f(0.0f, 6.28f), c->q);
    c->fadeIn = 0.05f;
    c->fadeOut = 0.8f;
    c->peak = 0.95f;
  }
  flat_ring(s, kShRing, g + V3{0.0f, 0.01f, 0.0f}, big ? 2.4f : 1.1f, 0.5f, 0xFFF0B0, 0xFF6A1A);
  for (int i = 0; i < (big ? 30 : 10); ++i) {
    V3 d = s.rng.unit();
    d.y = std::fabs(d.y) + 0.3f;
    P* e = spawn(s, kShSpark, g + V3{0.0f, 0.03f, 0.0f}, norm(d) * s.rng.f(0.8f, 2.2f), 0.028f, s.rng.f(0.5f, 1.0f), 0xFFE070, 0xFF5A10);
    if (e == nullptr) break;
    e->grav = 0.7f * kG;
    e->drag = 1.2f;
    e->floor = kFlBounce;
  }
  bits_out(s, kShPuff, g + V3{0.0f, 0.05f, 0.0f}, 6, 0.6f, 0.35f, 1.2f, 0x6A5A4A, 0x3A3028, -0.02f, 2.0f, V3{0.0f, 0.3f, 0.0f});
  for (int k = 0; k < 3; ++k) {
    if (P* m = spawn(s, kShPuff, g + V3{0.0f, 0.1f, 0.0f}, V3{0.0f, 0.35f + 0.1f * static_cast<float>(k), 0.0f}, 0.3f, 2.2f, 0x3A3436, 0x1A1618)) {
      m->grow = 0.25f;
      m->peak = 0.4f;
      m->fadeIn = 0.2f;
      m->drag = 0.3f;
    }
  }
  const float dist = flat_dist(g, head);
  sfx(s, kSfxThud, clampf(1.2f - 0.35f * dist, 0.3f, 1.0f));
  buzz(clampf(0.45f - 0.15f * dist, 0.1f, 0.4f) + (big ? 0.4f : 0.0f), 0.08f);
  if (big) {
    play_sound(s.def, 2, 1.0f);
    if (P* v = spawn(s, kShFlash, s.H, V3{}, 3.0f, 0.25f, 0xFFB060, 0xFFB060)) {
      v->orient = kOrVeil;
      v->peak = 0.25f;
      v->fadeIn = 0.0f;
      v->a3 = 1.0f;
    }
  }
  q.role = kRoNone;
  q.life = q.age;
}

// ---- BLACK HOLE's pieces
void hole_piece(Show& s, V3 p, float delay) {
  const int k = s.rng.i(10);
  int look = kShTwinkle;
  float sz = 0.05f;
  uint32_t c1 = 0xFFFFFF, c2 = 0xC08CFF;
  if (k < 3) {
    look = kMxRock;
    sz = s.rng.f(0.04f, 0.09f);
    c1 = 0x3A3448;
    c2 = 0x1A1624;
  } else if (k < 6) {
    look = kShSpark;
    sz = s.rng.f(0.03f, 0.05f);
    c1 = 0xE8D8FF;
    c2 = 0x9B5BFF;
  } else if (k < 8) {
    look = kShCard;
    sz = 0.12f;
    c1 = PAL(kPalCards);
    c2 = PAL(kPalCardEdge);
  }
  const V3 out = norm(V3{p.x - s.H.x, 0.0f, p.z - s.H.z}, s.right);
  P* q = spawn(s, look, p, cross(V3{0.0f, 1.0f, 0.0f}, out) * 0.35f, sz, room_left(s) + 1.5f, c1, c2);
  if (q == nullptr) return;
  q->role = kRoDebris;
  q->b0 = delay;
  q->b2 = -9.0f;
  q->floor = kFlNone;
  q->fadeIn = 0.3f;
  q->drag = 0.4f;
  if (look == kShCard) q->glow = 0.6f;
}
void supernova(Show& s) {
  emit(s, B(0.0f, kT0, kFlash, kAtGoal, 1, 0.0f, kShTwinkle, 7.0f, 0.0f, 0.30f, 0xFFFFFF, 0xC08CFF, 1.0f, 0.6f), 1, 0);
  emit(s, B(0.0f, kT0, kFlash, kAtGoal, 1, 0.0f, kShSpark, 4.5f, 0.0f, 0.30f, 0xFFFFFF, 0x9B5BFF, 1.0f, 1.5f), 1, 0);
  emit(s, B(0.0f, kT0, kRings, kAtGoal, 2, 0.0f, kShRing, 12.0f, 0.0f, 0.45f, 0xFFFFFF, 0xC08CFF, 1.0f, 0.0f), 2, 0);
  const Beat rays = B(0.0f, kT0, kRays, kAtGoal, 20, 0.0f, kShStreak, 5.5f, 0.0f, 0.30f, 0xFFFFFF, 0xC08CFF, 0.035f, 0.0f);
  emit(s, rays, scaled(rays, rays.n), 0);
  const Beat stars = B(0.0f, kT0, kBurst, kAtGoal, 120, 0.0f, kShTwinkle, 0.12f, 10.0f, 1.40f, PAL(kPalStars), 0xC08CFF, 1.0f, 0.0f);
  emit(s, stars, scaled(stars, stars.n), 0);
  const Beat rush = B(0.0f, kT0, kStreaks, kAtGoal, 60, 0.0f, kShStreak, 0.90f, 14.0f, 0.0f, 0xFFFFFF, 0xC8B0FF, 1.6f, 0.0f);
  emit(s, rush, scaled(rush, rush.n), 0);
  emit(s, B(0.0f, kTA, kVeil, kAtHead, 1, 0.0f, 0, 1.0f, 0.0f, 0.35f, 0xE8D8FF, 0, 0.35f, 0.01f), 1, 0);
  play_sound(s.def, 2, 1.0f);
  buzz(0.9f, 0.4f);
}

// ---- ROOM FLOOD's / SHARK BITE's pieces
void fish_spawn(Show& s, int i) {
  static const uint32_t kFish[5][2] = {{0xFF8A1A, 0xFFFFFF}, {0x2F8BFF, 0xFFE070}, {0xFFD21F, 0x2A2A2A}, {0xFF5FA8, 0xFFE0F0},
                                       {0x4BE36A, 0xFFF4C8}};
  const float a = s.rng.f(0.0f, 6.2831853f), rr = s.rng.f(0.6f, std::min(s.R, 1.9f));
  const int k = i % 5;
  P* q = spawn(s, kMxFish, V3{s.H.x + std::cos(a) * rr, s.floor + 0.05f, s.H.z + std::sin(a) * rr}, V3{}, s.rng.f(0.26f, 0.4f),
               s.room.dur + 1.0f, kFish[k][0], kFish[k][1]);
  if (q == nullptr) return;
  q->role = kRoFish;
  q->b0 = a + (s.rng.i(2) == 0 ? 1.5708f : -1.5708f);
  q->b1 = s.rng.f(0.35f, 0.6f);
  q->b3 = q->b1;
  q->a1 = s.rng.f(0.06f, 0.16f);
  q->a0 = std::min(s.R * 0.95f, rr * 1.3f + 0.3f);
  q->fadeIn = 0.6f;
  q->fadeOut = 0.5f;
}
// Your hands in water whose surface is at y `surf` (within `reach` of your head across): a splash going in or out, bubbles
// and a wake moving under it
void hands_in_water(Show& s, float surf, float reach, float dt) {
  for (int h = 0; h < 2; ++h) {
    HandSt& hd = s.hand[h];
    if (!hd.on || flat_dist(hd.p, s.H) > reach) {
      hd.wet = false;
      continue;
    }
    const bool wet = hd.p.y < surf;
    const float sp = len(hd.v);
    if (wet != hd.wet) {
      if (sp > 0.35f) {
        splash(s, V3{hd.p.x, surf, hd.p.z}, clampf(sp / 2.5f, 0.2f, 1.0f), V3{hd.v.x, 0.0f, hd.v.z});
        sfx(s, kSfxSplash, 0.45f + 0.5f * std::min(1.0f, sp / 2.5f));
        buzz(0.3f, 0.05f);
      }
      hd.wet = wet;
    }
    if (wet && sp > 0.5f) {
      if (s.rng.f(0.0f, 1.0f) < dt * 25.0f) {
        if (P* b = spawn(s, kShBubble, hd.p, V3{s.rng.f(-0.1f, 0.1f), 0.5f, s.rng.f(-0.1f, 0.1f)}, 0.03f,
                         std::max(0.1f, (surf - hd.p.y) / 0.5f), 0xBFF0FF, 0xFFFFFF))
          b->fadeIn = 0.05f;
      }
      if (s.rng.f(0.0f, 1.0f) < dt * 7.0f) flat_ring(s, kShRing, V3{hd.p.x, surf + 0.004f, hd.p.z}, 0.35f, 0.6f, 0xFFFFFF, 0x8FDCFF);
    }
  }
}

// ---- DISCO INFERNO's
void clap_burst(Show& s, V3 at, float rt) {
  s.room.next[2] = rt;
  s.room.next[3] = rt;
  for (int i = 0; i < 24; ++i) {
    P* t = spawn(s, kShTwinkle, at, s.rng.unit() * s.rng.f(0.6f, 1.6f), 0.06f, s.rng.f(0.5f, 0.9f), hsv(s.rng.f(0.0f, 1.0f), 0.6f, 1.0f), 0xFFFFFF);
    if (t == nullptr) break;
    t->drag = 1.2f;
    t->grav = 0.1f * kG;
  }
  bits_out(s, kMxPaper, at, 12, 1.4f, 0.04f, 1.6f, PAL(kPalConfetti), PAL(kPalConfetti), 0.3f, 1.0f, V3{0.0f, 0.4f, 0.0f});
  if (P* f = spawn(s, kShSpark, at, V3{}, 0.35f, 0.15f, 0xFFFFFF, 0xFF5FC8)) f->fadeIn = 0.0f;
  face_ring(s, at, 0.9f, 0.35f, 0xFFFFFF, 0xFF5FC8);
  sfx(s, kSfxClap, 1.0f);
  buzz(0.45f, 0.06f);
  s.room.count[0] += 1;
}

// ------------------------------------------------------------------------------------------------------------------
// room_start: its first pieces (kTakeover's beat)
// ------------------------------------------------------------------------------------------------------------------
void room_start(Show& s, int kind, float dur) {
  Room& r = s.room;
  const bool dead = r.sawDead;
  r = Room{};
  r.sawDead = dead;
  r.kind = kind;
  r.on = true;
  r.t0 = s.age;
  r.dur = dur;
  const float L = xrfx::load_scale(), ls = std::sqrt(L);
  const float toward = std::atan2(s.fwd.z, s.fwd.x);
  play_sound(s.def, 3, 0.85f);
  switch (kind) {
    case kToBits: {
      const int nb = std::max(8, static_cast<int>(std::lround(22.0f * ls)));
      for (int i = 0; i < nb; ++i) {
        const bool near = i < (nb * 9) / 20;
        const V3 p = near ? around_you(s, 0.42f, 0.85f, s.H.y - 0.55f, s.H.y + 0.2f, 0.75f)
                          : around_you(s, 1.0f * s.u, 0.9f * s.R, s.floor + 0.6f * s.u, s.top - 0.3f * s.u, 0.4f);
        bits_block(s, p, 0.05f * static_cast<float>(i), near ? 0.2f : 0.3f * std::max(1.0f, 0.8f * s.u));
      }
      const int nc = std::max(15, static_cast<int>(std::lround(60.0f * L)));
      for (int i = 0; i < nc; ++i) bits_cube(s, around_you(s, 0.35f, s.R, s.floor + 0.25f * s.u, s.top - 0.15f * s.u, 0.0f), 0.6f * s.rng.f(0.0f, 1.0f));
      r.next[0] = 1.2f;
      r.next[1] = 0.8f;
      r.count[1] = nb;
      break;
    }
    case kToInvaders: {
      const int cols = std::max(4, static_cast<int>(std::lround(8.0f * ls)));
      static const uint32_t kRow[3] = {0xFF4FD8, 0x4FFFB0, 0x5AC8FF};
      static const int kType[3] = {4, 2, 0};
      r.level = std::min(s.top - 0.1f, s.H.y + 1.0f);
      for (int row = 0; row < 3; ++row) {
        for (int c = 0; c < cols; ++c) {
          P* q = spawn(s, kShInvader, s.H + V3{0.0f, 1.0f, 0.0f}, V3{}, 0.22f, r.dur + 1.5f, kRow[row], 0xFFFFFF);
          if (q == nullptr) break;
          q->role = kRoAlien;
          q->a0 = (static_cast<float>(c) - 0.5f * static_cast<float>(cols - 1)) * (2.9f / static_cast<float>(std::max(1, cols - 1)));
          q->a1 = 0.22f * static_cast<float>(row);
          q->b3 = static_cast<float>(kType[row]);
          q->c2a = inv_look(kType[row]);
          q->fadeIn = 0.15f + 0.03f * static_cast<float>(row * cols + c);
          q->fadeOut = 0.2f;
          q->p = inv_spot(s, *q);
        }
      }
      r.count[1] = 1;
      r.count[3] = cols * 3;
      r.next[0] = 0.5f;
      r.next[1] = 1.0f;
      r.next[2] = 0.8f;
      break;
    }
    case kToMeteor:
      r.next[0] = 0.15f;
      break;
    case kToHole: {
      r.c = anchor(s, kAtGoal) + (s.picture ? s.D * 0.04f : V3{});
      for (int k = 0; k < 2; ++k) {
        P* h = spawn(s, k == 0 ? kShSpiral : kShHole, r.c, V3{}, 0.01f, r.dur + 0.6f, k == 0 ? 0xC08CFF : 0xE8D8FF, k == 0 ? 0x2A0A5A : 0x6A3AD0);
        if (h == nullptr) break;
        h->role = kRoHole;
        h->b0 = k == 0 ? 2.0f : 1.0f;
        h->a0 = (k == 0 ? 4.5f : 2.4f) * s.g;
        h->peak = k == 0 ? 0.7f : 1.0f;
        h->fadeIn = 0.1f;
        h->a3 = 1.0f;
        if (s.picture) {
          h->orient = kOrFixed;
          h->spin = 0.0f;
          quat_basis(s.S1, s.S2, s.D, h->q);
          h->inPicture = true;
        }
      }
      const int n = std::max(30, static_cast<int>(std::lround(120.0f * L)));
      for (int i = 0; i < n; ++i) hole_piece(s, around_you(s, 0.3f, 0.95f * s.R, s.floor + 0.2f, s.top - 0.2f, 0.0f), s.rng.f(0.2f, 1.8f));
      break;
    }
    case kToFlood: {
      const float rw = std::min(s.R, 2.3f * std::max(1.0f, 0.75f * s.u));
      if (P* w = spawn(s, kShWater, V3{s.H.x, s.floor + 0.005f, s.H.z}, V3{}, 2.0f * rw, r.dur + 1.0f, 0x3AA8E8, 0x0A3A6A)) {
        w->role = kRoWater;
        w->peak = 0.62f;
        w->fadeIn = 0.4f;
        w->fadeOut = 0.6f;
      }
      const int nf = std::max(4, static_cast<int>(std::lround(9.0f * ls)));
      for (int i = 0; i < nf; ++i) fish_spawn(s, i);
      r.next[0] = 1.6f;
      break;
    }
    case kToFlip: {
      const int n = std::max(60, static_cast<int>(std::lround(240.0f * L)));
      for (int i = 0; i < n; ++i) {
        const float a = s.rng.f(0.0f, 6.2831853f), rr = s.R * std::sqrt(s.rng.f(0.03f, 1.0f));
        const V3 p{s.H.x + std::cos(a) * rr, s.floor + 0.01f, s.H.z + std::sin(a) * rr};
        const int k = s.rng.i(20);
        int look = kShCard;
        float sz = 0.13f;
        uint32_t c1 = PAL(kPalCards), c2 = PAL(kPalCardEdge);
        if (k < 11) { look = kMxPaper; sz = 0.045f; c1 = c2 = PAL(kPalConfetti); }
        else if (k < 14) { look = kMxFoil; sz = 0.045f; c1 = PAL(kPalGold); c2 = 0xC8901E; }
        else if (k < 16) { look = kMxStreamer; sz = 0.28f; c1 = c2 = PAL(kPalConfetti); }
        else if (k < 18) { look = kMxStar; sz = 0.07f; c1 = 0xFFD34A; c2 = 0xFFF4C8; }
        else if (k < 19) { look = kMxCube; sz = 0.07f; c1 = PAL(kPalConfetti); c2 = 0x202020; }
        P* q = spawn(s, look, p, V3{}, sz * s.rng.f(0.8f, 1.25f), r.dur + 2.5f, c1, c2);
        if (q == nullptr) break;
        q->role = kRoFlip;
        q->floor = kFlNone;
        q->b0 = 0.08f + 0.35f * rr / s.R + s.rng.f(0.0f, 0.15f);
        q->b3 = q->spin;
        q->spin = 0.0f;
        q->fadeIn = 0.15f;
        q->fadeOut = 0.6f;
        q->h0.x = (look == kMxPaper || look == kMxFoil || look == kMxStreamer) ? 1.0f : 0.0f;
      }
      for (int i = 0; i < 6; ++i) {  // a few floating in zero-g in your reach
        const V3 p = around_you(s, 0.4f, 0.75f, s.H.y - 0.5f, s.H.y + 0.15f, 0.55f);
        P* q = spawn(s, i % 2 == 0 ? kMxStar : kMxCube, p, V3{s.rng.f(-0.05f, 0.05f), 0.0f, s.rng.f(-0.05f, 0.05f)}, i % 2 == 0 ? 0.11f : 0.1f,
                     r.dur + 2.5f, i % 2 == 0 ? 0xFFD34A : PAL(kPalConfetti), 0xFFF4C8);
        if (q == nullptr) break;
        q->role = kRoFlip;
        q->floor = kFlNone;
        q->b1 = 4.0f;
        q->b0 = 99.0f;
        q->spin = 1.0f;
        q->fadeIn = 0.3f;
        q->fadeOut = 0.6f;
      }
      r.flip = 1.0f;
      break;
    }
    case kToShark: {
      const float pr = 1.7f * std::min(1.6f, std::max(1.0f, s.u));
      if (P* w = spawn(s, kShWater, V3{s.H.x, s.floor + 0.012f, s.H.z}, V3{}, 0.01f, r.dur + 1.0f, 0x2A8AD0, 0x06243F)) {
        w->role = kRoWater;
        w->b0 = 1.0f;
        w->a0 = 2.0f * pr;
        w->peak = 0.7f;
        w->fadeIn = 0.0f;
        w->fadeOut = 0.5f;
      }
      if (P* f = spawn(s, kShFin, V3{s.H.x, s.floor, s.H.z}, V3{}, 0.01f, r.dur + 1.0f, 0x56626E, 0xC8D2DC)) {
        f->role = kRoFin;
        f->b1 = 1.3f;
        f->fadeIn = 0.0f;
        f->fadeOut = 0.3f;
      }
      // (r161 polish: its twin lying on the water, light grey, a white edge - looked at from right above, the upright fin alone is a sliver)
      if (P* f2 = spawn(s, kShFin, V3{s.H.x, s.floor, s.H.z}, V3{}, 0.01f, r.dur + 1.0f, 0x8A96A2, 0xFFFFFF)) {
        f2->role = kRoFin;
        f2->b0 = 1.0f;
        f2->orient = kOrFixed;
        f2->spin = 0.0f;
        f2->a3 = 1.0f;
        f2->fadeIn = 0.0f;
        f2->fadeOut = 0.3f;
      }
      r.level = 0.012f;
      const float side = s.rng.i(2) == 0 ? 1.0f : -1.0f;  // (it surfaces to your side and swims round in front of you)
      r.ang = toward + side * 1.2f;
      r.count[1] = side > 0.0f ? -1 : 1;
      r.next[0] = 0.6f;
      r.next[1] = 1.3f;
      break;
    }
    case kToDisco: {
      const float ts = 0.5f, Rt = std::min(s.R, 2.2f);
      const int nmax = static_cast<int>(Rt / ts) + 1;
      for (int i = -nmax; i <= nmax; ++i) {
        for (int j = -nmax; j <= nmax; ++j) {
          const float cx = static_cast<float>(i) * ts, cz = static_cast<float>(j) * ts;
          if (cx * cx + cz * cz > Rt * Rt) continue;
          P* q = spawn(s, kShTile, V3{s.H.x, s.floor + 0.006f, s.H.z} + s.fwd * cx + s.right * cz, V3{}, ts * 0.94f, r.dur + 1.0f, 0xFF5FC8, 0xFFFFFF);
          if (q == nullptr) break;
          q->role = kRoTile;
          q->a0 = static_cast<float>(i);
          q->a1 = static_cast<float>(j);
          q_flat(toward, q->q);
          q->b0 = 0.05f * std::sqrt(cx * cx + cz * cz) / ts;
          q->fadeIn = 0.0f;
          q->peak = 0.0f;
          q->fadeOut = 0.4f;
        }
      }
      r.c = s.H + s.fwd * 0.8f + s.right * 0.6f;
      r.c.y = s.top - 0.48f;
      r.ball = V3{r.c.x, s.top + 0.3f, r.c.z};
      if (P* b = spawn(s, kShBall, r.ball, V3{}, 0.34f, r.dur + 1.0f, 0xD8DCE6, 0x8A90A0)) {
        b->role = kRoBall;
        b->fadeIn = 0.2f;
        b->b2 = -9.0f;
      }
      for (int k = -1; k < 6; ++k) {  // (-1: the ball's string; 0, 1: the beams that follow your hands)
        P* bm = spawn(s, k < 0 ? kShStreak : kShCone, r.ball, V3{}, 1.5f, r.dur + 0.6f, k < 0 ? 0x9A9AA0 : hsv(0.16f * k, 0.7f, 1.0f), 0xFFFFFF);
        if (bm == nullptr) break;
        bm->role = kRoBeam;
        bm->orient = kOrAlongFixed;
        bm->floor = kFlNone;
        bm->b0 = static_cast<float>(k);
        bm->peak = k < 0 ? 0.8f : 0.32f;
        bm->glow = k < 0 ? 0.1f : 1.0f;
        bm->fadeIn = 0.6f;
        bm->fadeOut = 0.3f;
        bm->a0 = 1.05f * static_cast<float>(k);
        bm->a1 = 1.0f + 0.15f * static_cast<float>(k);
        bm->a3 = 1.0f;
        bm->h1 = V3{s.H.x + std::cos(bm->a0) * bm->a1, s.floor, s.H.z + std::sin(bm->a0) * bm->a1};
        if (k >= 0) {
          r.spot[k] = bm->h1;
          if (P* sp = spawn(s, kShSpark, bm->h1, V3{}, 0.45f, r.dur + 0.6f, hsv(0.16f * k, 0.5f, 1.0f), hsv(0.16f * k, 0.8f, 1.0f))) {
            sp->role = kRoSpot;
            sp->b0 = static_cast<float>(k);
            sp->floor = kFlNone;
            sp->peak = 0.5f;
            sp->fadeIn = 0.6f;
            sp->fadeOut = 0.3f;
            sp->a3 = 1.0f;
          }
        }
      }
      r.next[2] = -9.0f;
      r.next[3] = -9.0f;
      break;
    }
    default:
      break;
  }
  GLOG("goalfx: room takeover %d for %.1f s (%d pieces)", kind, dur, static_cast<int>(g_parts.size()));
}

// It clears: what it put up goes (each its own way)
void room_clear(Show& s) {
  for (P& q : g_parts) {
    switch (q.role) {
      case kRoBlock: case kRoAlien: q.b2 = q.age + s.rng.f(0.0f, 0.6f); break;
      case kRoCube:
        q.grav = 0.6f * kG;
        q.floor = kFlBounce;
        q.life = std::min(q.life, q.age + s.rng.f(0.6f, 1.1f));
        q.fadeOut = 0.3f;
        break;
      case kRoWater: case kRoFish: case kRoFin: case kRoShark: q.life = std::min(q.life, q.age + 0.7f); break;
      case kRoCrater: case kRoFlip: q.life = std::min(q.life, q.age + 1.0f); break;
      case kRoDebris: case kRoHole: case kRoBeam: case kRoSpot: q.life = std::min(q.life, q.age + 0.35f); break;
      case kRoTile: q.life = std::min(q.life, q.age + 0.3f + 0.04f * (std::fabs(q.a0) + std::fabs(q.a1))); break;
      case kRoBall: q.life = std::min(q.life, q.age + 0.6f); break;
      case kRoMeteor: q.life = std::min(q.life, q.age + 0.8f); break;
      default: break;
    }
    q.held = -1;
  }
}

// ------------------------------------------------------------------------------------------------------------------
// room_step: every frame - its director, then its pieces
// ------------------------------------------------------------------------------------------------------------------
void room_director(Show& s, float rt, float left, float dt, V3 head) {
  Room& r = s.room;
  const float L = xrfx::load_scale();
  switch (r.kind) {
    case kToBits:
      if (r.clearing) break;
      if (rt >= r.next[0]) {  // a block bumps by itself: a coin
        r.next[0] = rt + s.rng.f(1.0f, 1.7f);
        if (P* q = pick_piece(s, [](const P& p) { return p.role == kRoBlock && p.c2a == 0 && p.age > p.b0 + 0.3f; })) {
          q->a2 = q->age;
          bits_coin(s, q->p + V3{0.0f, 0.5f * q->size, 0.0f}, V3{s.rng.f(-0.4f, 0.4f), 2.0f, s.rng.f(-0.4f, 0.4f)});
          sfx(s, kSfxBlip, 0.5f);
        }
      }
      if (rt >= r.next[1] && left > 1.5f) {  // another block pops in
        r.next[1] = rt + 0.8f;
        int live = 0;
        for (const P& q : g_parts) live += (q.role == kRoBlock && q.c2a == 0) ? 1 : 0;
        if (live < r.count[1]) {
          bits_block(s, around_you(s, 0.42f, 0.9f, s.H.y - 0.5f, s.H.y + 0.25f, 0.75f), 0.0f, 0.2f);
          sfx(s, kSfxBlip, 0.3f);
        }
      }
      break;
    case kToInvaders: {
      if (rt >= r.next[0] && !r.clearing) {  // the march: a step (faster as fewer are left), down a row every fifth
        int alive = 0;
        for (const P& q : g_parts) alive += q.role == kRoAlien ? 1 : 0;
        r.next[0] = rt + 0.5f * (0.35f + 0.65f * static_cast<float>(alive) / static_cast<float>(std::max(1, r.count[3])));
        r.ang += 0.07f * static_cast<float>(r.count[1]);
        r.count[2] += 1;
        if (r.count[2] % 5 == 0) {
          r.count[1] = -r.count[1];
          r.level = std::max(s.H.y - 0.25f, r.level - 0.08f);
        }
        sfx(s, kSfxBlip, 0.22f);
      }
      if (rt >= r.next[1] && !r.clearing) {  // a shot drops
        r.next[1] = rt + s.rng.f(0.3f, 0.55f);
        if (P* q = pick_piece(s, [](const P& p) { return p.role == kRoAlien && p.b0 == 0.0f; })) inv_shot(s, q->p, 0.05f);
      }
      if (rt >= r.next[2] && !r.clearing && left > 2.0f) {  // one dives at you
        r.next[2] = rt + s.rng.f(1.1f, 1.6f);
        if (P* q = pick_piece(s, [](const P& p) { return p.role == kRoAlien && p.b0 == 0.0f && p.age > 0.8f; })) {
          const float toward = std::atan2(s.fwd.z, s.fwd.x);
          const float a = toward + (s.rng.i(2) == 0 ? 1.0f : -1.0f) * s.rng.f(0.75f, 1.2f);
          q->b0 = 1.0f;
          q->b1 = q->age;
          q->h0 = q->p;
          q->h1 = V3{s.H.x + std::cos(a) * 0.55f, s.H.y - s.rng.f(0.05f, 0.3f), s.H.z + std::sin(a) * 0.55f};
          sfx(s, kSfxBlip, 0.45f);
        }
      }
      if (!r.finale && rt >= r.dur - 2.6f) {  // the mothership
        r.finale = true;
        if (P* q = spawn(s, kShInvader, s.H + V3{0.0f, 1.0f, 0.0f}, V3{}, 0.5f, 2.6f, 0xFF3A5A, 0xFFE070)) {
          q->role = kRoMother;
          q->aspect = 2.0f;
          q->b0 = 3.0f;
          q->b1 = -9.0f;
          q->b2 = s.rng.i(2) == 0 ? 1.0f : -1.0f;
          q->c2a = inv_look(6);
          q->fadeIn = 0.2f;
          q->glow = 0.5f;
        }
        sfx(s, kSfxWhoosh, 0.6f);
      }
      // your triggers fire (held: again every 0.22 s)
      for (int h = 0; h < 2; ++h) {
        HandSt& hd = s.hand[h];
        if (!hd.on || r.clearing) continue;
        const bool pull = hd.trig > 0.6f && hd.trigWas <= 0.6f, held = hd.trig > 0.6f && s.age - hd.fireAt > 0.22f;
        if (pull || held) {
          inv_fire(s, h);
          hd.fireAt = s.age;
        }
      }
      break;
    }
    case kToMeteor:
      if (!r.clearing && rt >= r.next[0] && left > 1.8f) {
        r.next[0] = rt + s.rng.f(0.25f, 0.42f);
        meteor_spawn(s, false);
      }
      if (!r.finale && rt >= r.dur - 1.9f) {
        r.finale = true;
        meteor_spawn(s, true);
      }
      break;
    case kToHole:
      if (!r.clearing && rt < r.dur - 1.6f) {  // the room keeps pouring in from round you
        r.next[0] += dt * 22.0f * L;
        while (r.next[0] >= 1.0f) {
          r.next[0] -= 1.0f;
          hole_piece(s, around_you(s, 0.8f * s.R, s.R, s.floor + 0.2f, s.top - 0.2f, 0.0f), 0.0f);
        }
      }
      if (!r.finale && rt >= r.dur - 1.15f) {  // it collapses: SUPERNOVA
        r.finale = true;
        supernova(s);
        for (P& q : g_parts) {
          if (q.role != kRoDebris) continue;
          q.held = -1;
          q.v = norm(q.p - r.c) * s.rng.f(2.5f, 4.5f) + s.rng.unit() * 0.5f;
          q.life = std::min(q.life, q.age + s.rng.f(0.5f, 0.9f));
          q.fadeOut = 0.4f;
        }
      }
      break;
    case kToFlood: {
      const float Lmax = 0.48f;
      float lv = Lmax * ease_out(rt / 1.3f);
      if (left < 1.4f) lv = Lmax * ease_out(std::max(0.0f, left) / 1.4f);
      lv += 0.012f * std::sin(rt * 1.3f) * (lv / Lmax);
      r.level = std::max(0.0f, lv);
      const float surf = s.floor + r.level;
      if (r.level > 0.05f) hands_in_water(s, surf, s.R, dt);
      if (s.rng.f(0.0f, 1.0f) < dt * 8.0f && r.level > 0.1f) {  // bubbles off the floor
        const float a = s.rng.f(0.0f, 6.2831853f), rr = 0.8f * s.R * std::sqrt(s.rng.f(0.0f, 1.0f));
        if (P* b = spawn(s, kShBubble, V3{s.H.x + std::cos(a) * rr, s.floor + 0.03f, s.H.z + std::sin(a) * rr}, V3{0.0f, 0.45f, 0.0f},
                         s.rng.f(0.02f, 0.04f), r.level / 0.45f, 0xBFF0FF, 0xFFFFFF))
          b->flutter = 0.1f;
      }
      if (rt >= r.next[0] && left > 1.8f && r.level > 0.3f && !r.clearing) {  // a fish leaps
        r.next[0] = rt + s.rng.f(1.3f, 2.1f);
        if (P* q = pick_piece(s, [](const P& p) { return p.role == kRoFish && p.a2 == 0.0f; })) {
          q->a2 = q->age;
          splash(s, V3{q->p.x, surf, q->p.z}, 0.4f, V3{});
          sfx(s, kSfxSplash, 0.5f);
        }
      }
      if (!r.finale && left < 1.4f) {  // it drains: a whirlpool
        r.finale = true;
        r.c = V3{s.H.x, s.floor, s.H.z} + s.right * 0.8f;
        if (P* w = spawn(s, kShSpiral, V3{r.c.x, surf + 0.008f, r.c.z}, V3{}, 0.1f, std::max(0.3f, left) + 0.8f, 0xE8F8FF, 0x3AA8E8)) {
          w->role = kRoWater;
          w->b0 = 2.0f;
          w->orient = kOrFixed;
          w->spin = 0.0f;
          q_flat(0.0f, w->q);
          w->peak = 0.7f;
          w->a3 = 1.0f;
        }
        play_sound(s.def, 2, 0.9f);
      }
      break;
    }
    case kToFlip:
      r.count[1] = 0;
      if (!r.finale && rt >= r.dur - 1.8f) {  // gravity flips back: it all rains down
        r.finale = true;
        r.flip = -1.0f;
        play_sound(s.def, 2, 1.0f);
        buzz(0.5f, 0.15f);
        flat_ring(s, kShWave, V3{s.H.x, s.top - 0.02f, s.H.z}, 4.0f * s.u, 0.6f, 0xFFFFFF, 0xC85BFF);
        if (P* v = spawn(s, kShFlash, s.H, V3{}, 3.0f, 0.25f, 0xE0C0FF, 0xE0C0FF)) {
          v->orient = kOrVeil;
          v->peak = 0.18f;
          v->fadeIn = 0.0f;
          v->a3 = 1.0f;
        }
        for (P& q : g_parts) {
          if (q.role != kRoFlip) continue;
          if (q.b1 < 0.5f || q.b1 > 4.5f) {
            q.b1 = 5.0f;
            continue;
          }
          q.b1 = 3.0f;
          if (q.h0.x > 0.5f) {
            q.fall = s.rng.f(0.6f, 1.0f);
            q.flutter = 0.35f;
            q.grav = 0.0f;
            q.v.y = 0.0f;
          } else {
            q.grav = kG;
            q.drag = 0.2f;
          }
          q.spin = q.b3 > 0.0f ? q.b3 : 3.0f;
        }
      }
      break;
    case kToShark:
      if (!r.clearing) hands_in_water(s, s.floor + 0.10f, 0.85f * s.R, dt);
      if (!r.finale && rt >= r.dur - 2.2f) {  // it breaches: a clean jump through the water beside you
        r.finale = true;
        r.count[2] = 4;
        const float toward = std::atan2(s.fwd.z, s.fwd.x);
        const float side = s.rng.i(2) == 0 ? 1.0f : -1.0f;
        if (P* q = spawn(s, kMxFish, s.H, V3{}, 1.5f, 3.0f, 0x6A7684, 0xEEF2F6)) {
          q->role = kRoShark;
          q->a0 = toward + side * 1.25f;
          q->a1 = side * 1.5f;
          q->b1 = 0.0f;
          q->b2 = -9.0f;
          q->fadeIn = 0.05f;
          q->fadeOut = 0.2f;
          const V3 p0{s.H.x + std::cos(q->a0) * 1.3f, s.floor + 0.012f, s.H.z + std::sin(q->a0) * 1.3f};
          q->p = p0 - V3{0.0f, 0.25f, 0.0f};
          splash(s, p0, 0.9f, V3{});
        }
        sfx(s, kSfxSplash, 0.9f);
        buzz(0.4f, 0.1f);
      }
      break;
    case kToDisco: {
      if (r.clearing) break;
      for (int h = 0; h < 2; ++h) {  // sparkle trails off your moving hands
        const HandSt& hd = s.hand[h];
        const float sp = len(hd.v);
        if (hd.on && sp > 1.0f && s.rng.f(0.0f, 1.0f) < dt * 30.0f * std::min(1.0f, sp / 2.5f)) {
          if (P* t = spawn(s, kShTwinkle, hd.p, hd.v * -0.1f + V3{0.0f, 0.15f, 0.0f}, 0.045f, 0.55f, hsv(rt * 0.5f + 0.5f * h, 0.6f, 1.0f), 0xFFFFFF))
            t->drag = 1.5f;
        }
      }
      const HandSt &h0 = s.hand[0], &h1 = s.hand[1];
      if (h0.on && h1.on) {  // a clap: sparkles where your hands meet
        const V3 dv = h0.p - h1.p;
        const float d = len(dv);
        const float closing = d > 1e-3f ? dot(h0.v - h1.v, dv * (1.0f / d)) : 0.0f;
        if (d < 0.14f && closing < -0.8f && rt - r.next[2] > 0.3f) clap_burst(s, (h0.p + h1.p) * 0.5f, rt);
      }
      if (!r.finale && rt >= r.dur - 1.6f) {  // the finale: a ring of fire round you
        r.finale = true;
        const Beat ring = B(0.0f, kTA, kFireRing, kAtFloor, 36, 0.0f, kShFlame, 0.55f, 1.6f, 1.2f, PAL(kPalFire), 0xFF3A10, 0.5f, 0.0f);
        emit(s, ring, scaled(ring, ring.n), 0);
        const Beat rise = B(0.0f, kTA, kRise, kAtFloor, 80, 0.0f, kShSpark, 0.03f, 0.8f, 1.2f, 0xFFE070, 0xFF5A10, 0.2f, 0.0f);
        emit(s, rise, scaled(rise, rise.n), 0);
        play_sound(s.def, 2, 1.0f);
        buzz(0.6f, 0.3f);
      }
      break;
    }
    default:
      break;
  }
}

void room_piece(Show& s, P& q, float rt, float left, float dt, V3 head) {
  Room& r = s.room;
  switch (q.role) {
    // ---- 8-BIT TAKEOVER
    case kRoBlock: {
      const float k = q.age - q.b0;
      if (k < 0.0f) {
        q.size = 0.001f;
        break;
      }
      static const float kIn[4] = {0.35f, 0.7f, 1.0f, 1.0f};
      const float grow = kIn[std::min(3, static_cast<int>(k / 0.06f))];
      float hop = 0.0f;
      if (q.a2 > 0.0f && (q.age - q.a2) < 0.2f) hop = 0.07f * std::sin(3.14159f * (q.age - q.a2) / 0.2f);
      q.p.y = q.a1 + 0.035f * std::sin(1.7f * q.age + q.phase) + hop;
      q.v.y = 0.0f;
      q.size = q.a0 * grow;
      const V3 dh = q.p - head;
      const float dl = len(dh);
      if (dl < 0.3f) {  // never in your face
        q.p = q.p + norm(dh, s.fwd * -1.0f) * (0.3f - dl);
        q.a1 = q.p.y;
      }
      if (r.clearing) {
        if (q.c2a == 0 && q.age >= q.b2) {
          pix_pop(s, q.p, 0.36f, 0xFFC020, 8);
          sfx(s, kSfxBlip, 0.4f);
          q.role = kRoNone;
          q.life = q.age;
        }
      } else if (q.c2a == 0) {
        const int h = hand_near(s, q.p, 0.5f * q.size + 0.07f, 0.5f);
        if (h >= 0 && grow >= 1.0f) bits_hit(s, q, h);
      }
      if (q.c2a != 0 && q.role == kRoBlock && q.age - q.a3 > 0.7f) {
        pix_pop(s, q.p, 0.32f * q.a0 / 0.2f, 0xC87A2A, 8);
        q.role = kRoNone;
        q.life = q.age;
      }
      break;
    }
    case kRoCoin: {
      if (q.age < q.b0) break;  // (flung up first)
      q.grav = 0.0f;
      int h = -1;
      float best = 9.0f;
      for (int k = 0; k < 2; ++k) {
        if (!s.hand[k].on) continue;
        const float d = len(s.hand[k].p - q.p);
        if (d < best) {
          best = d;
          h = k;
        }
      }
      const V3 target = h >= 0 ? s.hand[h].p : head + V3{0.0f, -0.3f, 0.0f};
      const V3 to = target - q.p;
      const float sp = std::min(5.0f, 2.0f + 3.0f * (q.age - q.b0));
      q.v = q.v + (norm(to) * sp - q.v) * std::min(1.0f, 7.0f * dt);
      if (len(to) < (h >= 0 ? 0.09f : 0.22f)) {
        sfx(s, kSfxCoin, 0.85f);
        buzz(0.22f, 0.04f);
        bits_out(s, kShTwinkle, q.p, 3, 0.6f, 0.05f, 0.35f, 0xFFF4C8, 0xFFD21F, 0.0f, 2.0f, V3{});
        r.count[2] += 1;
        // r164 (Lance: "any coins we get from hitting the boxes, are actual tizm coins that go to my account ... cap it at
        // 150 ... make it like a good investment prop"): each caught coin is a TIZM coin (your own goal, never a preview
        // or the friend's); xr_cards takes them (take_earned) and pays them only in a franchise match
        if (!s.preview && !s.theirs && s.earned < kTakeoverCoinCap) {
          ++s.earned;
          g_earned.fetch_add(1);
        }
        q.role = kRoNone;
        q.life = q.age;
      }
      break;
    }
    case kRoCube: {
      if (q.b0 == 0.0f) {  // afloat: it stays in the room
        if (flat_dist(q.p, s.H) > s.R) q.v = q.v + norm(V3{s.H.x - q.p.x, 0.0f, s.H.z - q.p.z}) * (0.6f * dt);
        if (q.p.y < s.floor + 0.2f) q.v.y += 0.4f * dt;
        if (q.p.y > s.top - 0.1f) q.v.y -= 0.4f * dt;
      }
      const V3 dh = q.p - head;
      if (len(dh) < 0.28f) q.v = q.v + norm(dh) * (2.0f * dt);
      if (q.age - q.b2 > 0.15f && !r.clearing) {  // it bounces off your hands
        const int h = hand_near(s, q.p, 0.5f * q.size + 0.06f, 0.0f);
        if (h >= 0) {
          const HandSt& hd = s.hand[h];
          const float sp = len(hd.v);
          q.v = hd.v * 1.15f + norm(q.p - hd.p, V3{0.0f, 1.0f, 0.0f}) * (0.35f + 0.3f * sp);
          q.spin = 6.0f + 4.0f * std::min(1.0f, sp / 2.0f);
          q.axis = norm(s.rng.unit(), V3{0.0f, 1.0f, 0.0f});
          q.b2 = q.age;
          if (sp > 0.9f) {
            q.b0 = 1.0f;
            q.grav = 0.55f * kG;
            q.floor = kFlBounce;
            q.drag = 0.3f;
          }
          if (sp > 0.6f) sfx(s, kSfxPunch, 0.35f + 0.3f * std::min(1.0f, sp / 3.0f));
          buzz(0.12f + 0.15f * std::min(1.0f, sp / 3.0f), 0.03f);
        }
      }
      break;
    }
    // ---- PIXEL INVADERS
    case kRoAlien: {
      q.c2a = inv_look(static_cast<int>(q.b3) + (r.count[2] & 1));
      const V3 spot = inv_spot(s, q);
      q.v = V3{};
      if (q.b0 == 0.0f) {
        q.p = spot;
      } else if (q.b0 == 1.0f) {  // diving at you: swoop, hover in your reach, then back
        const float k = (q.age - q.b1) / 0.9f;
        if (k < 1.0f) {
          q.p = q.h0 + (q.h1 - q.h0) * ease_out(k) + V3{0.0f, 0.25f * std::sin(3.14159f * k), 0.0f};
        } else if (k < 1.0f + 1.3f / 0.9f) {
          const float t = q.age;
          q.p = q.h1 + V3{0.06f * std::sin(5.0f * t), 0.04f * std::sin(7.0f * t), 0.06f * std::cos(5.0f * t)};
        } else {
          q.b0 = 2.0f;
          q.b1 = q.age;
          q.h0 = q.p;
        }
      } else {
        const float k = (q.age - q.b1) / 0.7f;
        if (k >= 1.0f) q.b0 = 0.0f;
        q.p = q.h0 + (spot - q.h0) * ease_out(k);
      }
      if (r.clearing) {
        if (q.age >= q.b2) inv_kill(s, q, false);
      } else if (q.age > q.fadeIn) {
        const int h = hand_near(s, q.p, 0.5f * q.size + 0.07f, 0.5f);
        if (h >= 0) inv_kill(s, q, true);
      }
      break;
    }
    case kRoMother: {
      const float toward = std::atan2(s.fwd.z, s.fwd.x);
      const float k = q.age / 2.4f;
      const float az = toward + q.b2 * (-1.65f + 3.3f * k);
      const float y = std::min(s.top - 0.15f, s.H.y + 0.62f) + 0.05f * std::sin(q.age * 3.0f);
      q.p = V3{s.H.x + std::cos(az) * 1.7f, y, s.H.z + std::sin(az) * 1.7f};
      q.v = V3{};
      q.c2a = inv_look(q.b1 > q.age ? 7 : 6);
      if (k >= 1.0f) q.life = q.age;
      if (std::fmod(q.age, 0.5f) < dt) inv_shot(s, q.p, 0.08f);
      break;
    }
    case kRoLaser: {
      const V3 a = q.p, b = q.p + q.v * dt;
      for (P& t : g_parts) {
        if ((t.role != kRoAlien && t.role != kRoMother) || t.age < t.fadeIn) continue;
        if (seg_dist(t.p, a, b) >= 0.5f * t.size * (t.role == kRoMother ? 1.6f : 1.0f) + 0.05f) continue;
        if (t.role == kRoMother) {
          t.b0 -= 1.0f;
          t.b1 = t.age + 0.08f;
          sfx(s, kSfxZap, 0.8f);
          buzz(0.3f, 0.05f);
          if (t.b0 <= 0.0f) mother_boom(s, t);
        } else {
          inv_kill(s, t, true);
        }
        q.role = kRoNone;
        q.life = q.age;
        break;
      }
      break;
    }
    case kRoShot: {
      if (hand_near(s, q.p, 0.09f, 0.0f) >= 0) {  // your hand blocks it
        bits_out(s, kShSpark, q.p, 6, 1.0f, 0.03f, 0.3f, 0xFFFFFF, 0xFF5050, 0.3f, 2.0f, V3{});
        sfx(s, kSfxBlip, 0.3f);
        buzz(0.1f, 0.03f);
        q.role = kRoNone;
        q.life = q.age;
      } else if (len(q.p - head) < 0.3f) {
        q.life = q.age;
      } else if (q.p.y < s.floor + 0.03f) {  // it splats on your floor
        if (P* rr = flat_ring(s, kShPixel, V3{q.p.x, s.floor + 0.01f, q.p.z}, 0.25f, 0.4f, 0xFF5050, 0xFFFFFF)) rr->c2a = kPixRing;
        q.life = q.age;
      }
      break;
    }
    // ---- METEOR STRIKE
    case kRoMeteor: {
      const bool big = q.b3 > 1.5f;
      if (q.held >= 0) {
        HandSt& hd = s.hand[q.held];
        if (!hd.on || !hd.grip) {  // let go: thrown (or dropped)
          q.held = -1;
          q.v = hd.v * 1.4f;
          q.grav = 0.8f * kG;
          q.b3 = 1.0f;
          q.b2 = q.age;
          sfx(s, kSfxWhoosh, 0.5f + 0.3f * std::min(1.0f, len(hd.v) / 3.0f));
          buzz(0.2f, 0.04f);
        } else {
          q.p = hd.p + V3{0.0f, 0.04f, 0.0f};
          q.v = hd.v;
          if (s.rng.f(0.0f, 1.0f) < dt * 25.0f)
            bits_out(s, kShSpark, q.p, 1, 0.6f, 0.025f, 0.35f, 0xFFE070, 0xFF5A10, -0.1f, 2.0f, V3{0.0f, 0.3f, 0.0f});
          if (s.age - hd.buzzAt > 0.25f) {
            buzz(0.15f, 0.06f);
            hd.buzzAt = s.age;
          }
          if (q.age - q.b0 > 1.6f) meteor_shatter(s, q, -1, false);  // it crumbles in your hand
          break;
        }
      }
      q.b1 += dt * 45.0f;  // its trail
      while (q.b1 >= 1.0f) {
        q.b1 -= 1.0f;
        if (P* f = spawn(s, kShFirePuff, q.p - q.v * 0.02f, q.v * -0.05f + s.rng.unit() * 0.15f, q.size * s.rng.f(0.7f, 1.1f), 0.35f, 0xFFE070, 0xFF5A10)) {
          f->grow = 0.4f;
          f->drag = 2.0f;
        }
      }
      const V3 dh = q.p - head;
      if (len(dh) < 0.32f && q.b3 < 0.5f) q.v = q.v + norm(V3{dh.x, 0.0f, dh.z}, s.right) * (6.0f * dt);
      if (!r.clearing && q.b3 != 1.0f) {  // your hands: a grip catches it, a punch shatters it
        for (int h = 0; h < 2; ++h) {
          const HandSt& hd = s.hand[h];
          if (!hd.on) continue;
          const float d = len(hd.p - q.p);
          if (hd.grip && !s.holds[h] && !big && d < 0.5f * q.size + 0.12f) {
            q.held = static_cast<int8_t>(h);
            s.holds[h] = true;
            q.b0 = q.age;
            sfx(s, kSfxGrab, 0.8f);
            buzz(0.35f, 0.05f);
            break;
          }
          if (len(hd.v) > 0.7f && d < 0.5f * q.size + 0.08f) {
            meteor_shatter(s, q, h, big);
            break;
          }
        }
      }
      if (q.role == kRoMeteor && q.held < 0 && (q.p.y <= s.floor + 0.3f * q.size || (q.b3 == 1.0f && q.age - q.b2 > 1.6f)))
        meteor_land(s, q, big, head);
      break;
    }
    case kRoCrater: {
      const float heat = clampf(1.0f - q.age / 2.5f, 0.0f, 1.0f);
      q.c2a = static_cast<uint8_t>(heat * 255.0f + 0.5f);
      if (s.rng.f(0.0f, 1.0f) < dt * 3.0f * heat) {
        if (P* m = spawn(s, kShPuff, q.p + V3{s.rng.f(-0.1f, 0.1f), 0.05f, s.rng.f(-0.1f, 0.1f)}, V3{0.0f, 0.3f, 0.0f}, 0.15f, 1.4f, 0x4A4446, 0x2A2628)) {
          m->grow = 0.2f;
          m->peak = 0.3f;
        }
      }
      break;
    }
    // ---- BLACK HOLE
    case kRoHole: {
      float g = ease_out(rt / 0.5f);
      const float tc = r.dur - 1.5f;
      if (rt > tc) g *= clampf(1.0f - (rt - tc) / 0.35f, 0.0f, 1.0f);
      q.size = std::max(0.002f, q.a0 * g);
      q.c2a = static_cast<uint8_t>(static_cast<int>(rt * (q.b0 > 1.5f ? 50.0f : 90.0f)) & 255);
      if (rt > tc + 0.4f) q.life = std::min(q.life, q.age + 0.05f);
      break;
    }
    case kRoDebris: {
      if (q.held >= 0) {
        HandSt& hd = s.hand[q.held];
        if (!hd.on || !hd.grip) {
          q.held = -1;
          q.v = hd.v * 1.3f;
          q.b0 = q.age + 0.8f;
          sfx(s, kSfxWhoosh, 0.4f);
        } else {
          q.p = hd.p + V3{0.0f, 0.03f, 0.0f};
          q.v = hd.v;
          if (s.age - hd.buzzAt > 0.3f) {  // it tugs at your hand
            buzz(0.12f, 0.08f);
            hd.buzzAt = s.age;
          }
          break;
        }
      }
      if (r.finale) break;
      const V3 to = r.c - q.p;
      const float d = len(to);
      const V3 dir = to * (1.0f / std::max(d, 1e-3f));
      if (q.age > q.b0) {  // the pull, swirling in
        const float k = 1.2f + 2.6f * clampf(rt / r.dur, 0.0f, 1.0f);
        const float acc = k * (0.6f + 1.5f / (d + 0.3f));
        const V3 axis = s.picture ? s.D : V3{0.0f, 1.0f, 0.0f};
        V3 tang = cross(dir, axis);
        tang = len(tang) < 1e-3f ? s.right : norm(tang);
        q.v = q.v + (dir * (0.85f * acc) + tang * (0.3f * clampf(d / 1.5f, 0.2f, 1.0f) * acc)) * dt;
        const float sp = len(q.v);
        if (sp > 6.0f) q.v = q.v * (6.0f / sp);
      }
      for (int h = 0; h < 2 && !r.clearing; ++h) {  // your hands: grab it back, shove it, hold the flow back
        HandSt& hd = s.hand[h];
        if (!hd.on) continue;
        const V3 dv = q.p - hd.p;
        const float dd = len(dv);
        if (hd.grip && !s.holds[h] && dd < 0.16f) {
          q.held = static_cast<int8_t>(h);
          s.holds[h] = true;
          sfx(s, kSfxGrab, 0.6f);
          buzz(0.3f, 0.05f);
          break;
        }
        if (dd < 0.32f) {
          const float f = 1.0f - dd / 0.32f;
          if (len(hd.v) > 0.6f) {
            if (q.age - q.b2 > 0.25f) {
              q.v = q.v + hd.v * (1.4f * f);
              q.b2 = q.age;
              sfx(s, kSfxWhoosh, 0.3f);
              buzz(0.1f, 0.03f);
            }
          } else {
            q.v = q.v * std::exp(-3.0f * f * dt) + norm(dv, V3{0.0f, 1.0f, 0.0f}) * (9.0f * f * dt);
          }
        }
      }
      const V3 dh = q.p - head;
      if (len(dh) < 0.35f) q.v = q.v + norm(dh) * (6.0f * dt);
      const float hs = 2.4f * s.g;
      if (d < 0.7f * hs) q.size *= std::exp(-5.0f * dt);
      if (d < 0.25f * hs + 0.03f || q.size < 0.004f) {  // swallowed
        if (P* f = spawn(s, kShSpark, q.p, V3{}, 0.08f, 0.15f, 0xE8D8FF, 0x9B5BFF)) f->fadeIn = 0.0f;
        r.count[0] += 1;
        if (r.count[0] % 6 == 0) sfx(s, kSfxGulp, 0.3f);
        q.role = kRoNone;
        q.life = q.age;
      }
      break;
    }
    // ---- ROOM FLOOD / SHARK BITE
    case kRoWater: {
      q.c2a = static_cast<uint8_t>(static_cast<int>(rt * 40.0f) & 255);
      if (q.b0 < 0.5f) {
        q.p.y = s.floor + 0.005f + r.level;
      } else if (q.b0 < 1.5f) {  // the shark's patch: opens round your feet, closes at the end
        q.size = std::max(0.01f, q.a0 * ease_out(rt / 0.5f) * clampf(left / 0.7f, 0.0f, 1.0f));
      } else {  // the drain's whirlpool
        q.p.y = s.floor + r.level + 0.008f;
        q.size = std::min(1.2f, q.size + 1.2f * dt);
        q.c2a = static_cast<uint8_t>(static_cast<int>(rt * 120.0f) & 255);
      }
      break;
    }
    case kRoFish: {
      const float surf = s.floor + r.level;
      if (q.a2 > 0.0f) {  // leaping
        const float k = (q.age - q.a2) / 0.75f;
        if (k >= 1.0f) {
          q.a2 = 0.0f;
          splash(s, V3{q.p.x, surf, q.p.z}, 0.4f, V3{});
          sfx(s, kSfxSplash, 0.5f);
        } else {
          const V3 f{std::cos(q.b0), 0.0f, std::sin(q.b0)};
          q.v = f * 1.3f;
          q.p.y = surf + 0.5f * std::sin(3.14159f * k);
          const float pitch = (0.5f - k) * 1.8f;
          q_swim(V3{f.x * std::cos(pitch), std::sin(pitch), f.z * std::cos(pitch)}, 0.0f, 0.0f, q.q);
          break;
        }
      }
      const float rx = q.p.x - s.H.x, rz = q.p.z - s.H.z, rh = std::sqrt(rx * rx + rz * rz);
      q.b2 = clampf(q.b2 + s.rng.f(-1.0f, 1.0f) * 2.5f * dt, -1.2f, 1.2f);
      q.b0 += q.b2 * dt;
      if (rh < 0.45f) q.b0 = turn_to(q.b0, std::atan2(rz, rx), 2.5f * dt);     // out of your legs
      if (rh > q.a0) q.b0 = turn_to(q.b0, std::atan2(-rz, -rx), 2.5f * dt);    // back into the water
      for (int h = 0; h < 2; ++h) {  // a hand in the water: it darts away
        const HandSt& hd = s.hand[h];
        if (!hd.on || hd.p.y > surf + 0.15f) continue;
        const float dx = q.p.x - hd.p.x, dz = q.p.z - hd.p.z;
        if (dx * dx + dz * dz < 0.45f * 0.45f) {
          q.b0 = std::atan2(dz, dx);
          q.b3 = 1.9f;
        }
      }
      q.b3 += (q.b1 - q.b3) * std::min(1.0f, 1.6f * dt);
      if (r.finale) {  // down the whirlpool
        q.b0 = turn_to(q.b0, std::atan2(r.c.z - q.p.z, r.c.x - q.p.x), 4.0f * dt);
        q.b3 = 1.2f;
        if (flat_dist(q.p, r.c) < 0.25f) q.size *= std::exp(-4.0f * dt);
      }
      const V3 f{std::cos(q.b0), 0.0f, std::sin(q.b0)};
      q.v = f * q.b3;
      q.v.y = (std::max(s.floor + 0.06f, surf - q.a1) - q.p.y) * 3.0f;
      q_swim(f, 0.28f * std::sin(q.age * (6.0f + 6.0f * q.b3) + q.phase), 0.0f, q.q);
      break;
    }
    case kRoDrop:
      if (q.v.y < 0.0f && q.p.y < q.h0.y) {
        if (s.rng.i(3) == 0) flat_ring(s, kShRing, V3{q.p.x, q.h0.y + 0.006f, q.p.z}, 0.15f, 0.35f, 0xFFFFFF, 0x8FDCFF);
        q.life = q.age;
      }
      break;
    case kRoFin: {
      if (q.b0 > 0.5f) {  // (the twin lying on the water: the fin's place, size and heading this frame; seen the more, the more you look down)
        q.p = r.c + V3{0.0f, 0.006f, 0.0f};
        q.size = std::max(0.001f, 1.15f * r.cv.y);
        q_flat(std::atan2(r.cv.z, r.cv.x), q.q);
        const float dd = len(head - q.p);
        const float steep = dd > 1e-3f ? (head.y - q.p.y) / dd : 1.0f;
        q.peak = clampf(r.phase * 3.0f, 0.0f, 1.0f) * clampf(steep * 1.6f - 0.5f, 0.0f, 1.0f) * 0.9f;
        break;
      }
      // r.count[2]: 0 under, 1 circling, 2 closing in, 3 diving, 4 gone (the breach / the end); r.phase: how far out
      const float surf = s.floor + r.level;
      float& rise = r.phase;
      int& st = r.count[2];
      const float dirS = static_cast<float>(r.count[1]);
      if (left < 0.9f && st != 4) st = 4;
      if (st == 0) {
        rise = std::max(0.0f, rise - 3.0f * dt);
        if (rt >= r.next[0]) {
          st = 1;
          r.next[1] = rt + s.rng.f(1.4f, 2.0f);
          splash(s, V3{q.p.x, surf, q.p.z}, 0.3f, V3{});
          sfx(s, kSfxSplash, 0.35f);
        }
      } else if (st == 1) {
        rise = std::min(1.0f, rise + 2.0f * dt);
        q.b1 += (1.25f - q.b1) * std::min(1.0f, 2.0f * dt);
        r.ang += 1.15f * dirS * dt;
        if (rt >= r.next[1] && left > 2.8f) {
          st = 2;
          r.next[1] = rt + 0.9f;
        }
      } else if (st == 2) {  // it closes in, more of it out of the water
        rise = std::min(1.5f, rise + 2.0f * dt);
        q.b1 += (0.62f - q.b1) * std::min(1.0f, 3.0f * dt);
        r.ang += 0.9f * dirS * dt;
        if (rt >= r.next[1]) {  // SNAP: the shark's head bursts up out of the water
          st = 3;
          r.next[0] = rt + 1.6f;
          if (P* sn = spawn(s, kShChomper, V3{q.p.x, surf, q.p.z}, V3{}, 0.45f, 1.1f, 0x6A7684, 0xF0F4F8)) {
            sn->role = kRoSnap;
            sn->c2a = 0;
            sn->fadeIn = 0.0f;
            sn->fadeOut = 0.15f;
            sn->h0 = V3{q.p.x, surf, q.p.z};
            sn->b0 = -1.0f;
          }
        }
      } else if (st == 3) {
        rise = std::max(0.0f, rise - 3.0f * dt);
        r.ang += 0.6f * dirS * dt;
        if (rise <= 0.0f && rt >= r.next[0] && left > 2.6f && !r.finale) {
          st = 1;
          r.ang += s.rng.f(1.5f, 3.0f);
          q.b1 = 1.3f;
          r.next[1] = rt + s.rng.f(0.8f, 1.2f);
        }
      } else {
        rise = std::max(0.0f, rise - 3.0f * dt);
      }
      const V3 dir{std::cos(r.ang), 0.0f, std::sin(r.ang)};
      const V3 base{s.H.x + dir.x * q.b1, surf, s.H.z + dir.z * q.b1};
      q.size = std::max(0.001f, 0.55f * rise);
      q.p = base + V3{0.0f, 0.5f * q.size, 0.0f};
      q.peak = clampf(rise * 3.0f, 0.0f, 1.0f);
      // its sprite leads with its nose: mirrored when it swims to its left as you see it
      const V3 t = V3{-dir.z, 0.0f, dir.x} * dirS;
      const V3 z = norm(V3{head.x - q.p.x, 0.0f, head.z - q.p.z}, V3{0.0f, 0.0f, 1.0f});
      q.c2a = dot(t, V3{z.z, 0.0f, -z.x}) >= 0.0f ? 0 : 255;
      r.c = base;  // (for its twin on the water)
      r.cv = V3{t.x, q.size, t.z};
      if (rise > 0.3f) {  // its wake
        if (s.rng.f(0.0f, 1.0f) < dt * 30.0f)
          bits_out(s, kShSpark, base - t * 0.15f, 1, 0.3f, 0.03f, 0.4f, 0xFFFFFF, 0xBFE8FF, -0.05f, 2.0f, V3{0.0f, 0.15f, 0.0f});
        q.b2 += dt;
        if (q.b2 > 0.22f) {
          q.b2 = 0.0f;
          flat_ring(s, kShRing, base + V3{0.0f, 0.004f, 0.0f}, 0.45f, 0.8f, 0xFFFFFF, 0x8FDCFF);
        }
      }
      if ((st == 1 || st == 2) && rise > 0.5f && !r.clearing) {  // bop it: it dives
        for (int h = 0; h < 2; ++h) {
          const HandSt& hd = s.hand[h];
          if (!hd.on || len(hd.v) < 0.4f) continue;
          if (flat_dist(hd.p, q.p) < 0.3f && hd.p.y < surf + q.size + 0.35f) {
            st = 3;
            r.next[0] = rt + 1.0f;
            splash(s, base, 0.6f, V3{hd.v.x, 0.0f, hd.v.z});
            bonk_stars(s, q.p);
            sfx(s, kSfxBop, 0.9f);
            buzz(0.5f, 0.07f);
            r.count[0] += 1;
            break;
          }
        }
      }
      break;
    }
    case kRoSnap: {
      const float k = q.age;
      float hgt = k < 0.25f ? ease_out(k / 0.25f) : (k < 0.75f ? 1.0f : std::max(0.0f, 1.0f - (k - 0.75f) / 0.3f));
      if (q.b0 >= 0.0f) hgt = std::max(0.0f, q.b1 - (q.age - q.b0) * 3.0f);  // bopped: down fast
      q.p = q.h0 + V3{0.0f, std::max(0.05f, 0.75f * hgt), 0.0f};
      q.peak = clampf(hgt * 4.0f, 0.0f, 1.0f);
      const float open = q.b0 >= 0.0f ? 0.1f : (k < 0.45f ? clampf(k / 0.3f, 0.0f, 1.0f) : (k < 0.55f ? 1.0f - (k - 0.45f) / 0.1f : 0.0f));
      q.c2a = static_cast<uint8_t>(open * 255.0f + 0.5f);
      if (q.b0 < 0.0f && k - dt < 0.55f && k >= 0.55f) {  // the snap
        sfx(s, kSfxSnap, 1.0f);
        buzz(0.55f, 0.08f);
        splash(s, q.h0, 0.5f, V3{});
      }
      if (q.b0 < 0.0f && k < 0.6f && !r.clearing) {  // bop it first
        const int h = hand_near(s, q.p, 0.32f, 0.5f);
        if (h >= 0) {
          q.b0 = q.age;
          q.b1 = hgt;
          bonk_stars(s, q.p + V3{0.0f, 0.15f, 0.0f});
          sfx(s, kSfxBop, 1.0f);
          buzz(0.5f, 0.07f);
          r.count[0] += 1;
          q.life = std::min(q.life, q.age + 0.35f);
        }
      }
      break;
    }
    case kRoShark: {
      const float surf = s.floor + r.level;
      q.b1 += dt / 1.1f * (q.b2 > 0.0f ? 2.2f : 1.0f);
      const float k = q.b1;
      if (k >= 1.0f) {  // back in: the big splash
        const V3 p1{s.H.x + std::cos(q.a0 + q.a1) * 1.3f, surf, s.H.z + std::sin(q.a0 + q.a1) * 1.3f};
        splash(s, p1, 1.0f, V3{});
        bits_out(s, kShPuff, p1 + V3{0.0f, 0.1f, 0.0f}, 6, 0.8f, 0.4f, 1.0f, 0xF4FAFF, 0xBFD8E8, -0.05f, 2.0f, V3{0.0f, 0.5f, 0.0f});
        play_sound(s.def, 2, 1.0f);
        buzz(0.7f, 0.2f);
        q.role = kRoNone;
        q.life = q.age;
        break;
      }
      const float ang = q.a0 + q.a1 * k, peak = s.H.y - surf + 0.2f;
      q.p = V3{s.H.x + std::cos(ang) * 1.3f, surf - 0.25f + peak * std::sin(3.14159f * k), s.H.z + std::sin(ang) * 1.3f};
      q.v = V3{};
      const float side = q.a1 * 1.3f;
      const V3 fwd = norm(V3{-std::sin(ang) * side, peak * 3.14159f * std::cos(3.14159f * k), std::cos(ang) * side});
      q_swim(fwd, 0.0f, q.b2 > 0.0f ? (q.age - q.b2) * 9.0f : 0.0f, q.q);
      if (q.b2 < 0.0f && !r.clearing) {  // bop it in the air
        const int h = hand_near(s, q.p, 0.5f, 0.6f);
        if (h >= 0) {
          q.b2 = q.age;
          bonk_stars(s, q.p);
          sfx(s, kSfxBop, 1.0f);
          buzz(0.5f, 0.07f);
          r.count[0] += 1;
        }
      }
      break;
    }
    // ---- GRAVITY FLIP
    case kRoFlip: {
      const bool light = q.h0.x > 0.5f;
      const float rest = q.mesh >= 0 ? 0.3f * q.size : 0.0f;
      if (q.b1 < 0.5f) {  // on the floor, its turn to go
        if (q.age >= q.b0 && r.flip > 0.0f) {
          q.b1 = 1.0f;
          q.v = V3{s.rng.f(-0.2f, 0.2f), s.rng.f(0.6f, 1.4f), s.rng.f(-0.2f, 0.2f)};
          q.grav = -(light ? 0.35f : 0.6f) * kG;
          q.drag = light ? 1.6f : 0.6f;
          q.flutter = light ? 0.5f : 0.0f;
          q.spin = q.b3 > 0.0f ? q.b3 : 3.0f;
        }
      } else if (q.b1 < 1.5f) {  // falling UP
        if (q.p.y >= s.top - rest) {
          q.p.y = s.top - rest;
          q.v = V3{};
          q.grav = 0.0f;
          q.flutter = 0.0f;
          q.spin *= 0.15f;
          q.b1 = 2.0f;
        }
      } else if (q.b1 < 2.5f) {  // stuck to your ceiling
        q.p.y = s.top - rest;
        q.v.y = 0.0f;
        q.v.x *= std::exp(-2.0f * dt);
        q.v.z *= std::exp(-2.0f * dt);
      } else if (q.b1 < 3.5f) {  // raining back down
        // r197: onto what's really under it with your room scan (a table, the couch), else the floor
        const float fl = s.scan != nullptr ? xrroom::surface_below(*s.scan, q.p.x, q.p.y + 0.1f, q.p.z, nullptr) : s.floor;
        if (q.p.y <= fl + rest) {
          q.p.y = fl + rest;
          q.v = V3{};
          q.fall = 0.0f;
          q.grav = 0.0f;
          q.spin = 0.0f;
          q.flutter = 0.0f;
          q.b1 = 5.0f;
          q.life = std::min(q.life, q.age + 1.2f);
          q.fadeOut = 0.8f;
        }
      } else if (q.b1 < 4.5f) {  // afloat in zero-g
        q.v = q.v * std::exp(-0.6f * dt);
        const V3 dh = q.p - head;
        if (len(dh) < 0.3f) q.v = q.v + norm(dh) * (2.0f * dt);
      }
      if (r.clearing || q.b1 > 4.5f) break;
      for (int h = 0; h < 2; ++h) {  // your hands: wind (and a punch)
        const HandSt& hd = s.hand[h];
        const float sp = len(hd.v);
        if (!hd.on || sp < 0.35f) continue;
        const float dhz = flat_dist(q.p, hd.p), above = q.p.y - hd.p.y;
        if (dhz >= 0.55f || above < -0.35f) continue;
        const float f = (1.0f - dhz / 0.55f) / (1.0f + 0.8f * std::max(0.0f, above));
        const bool stuck = q.b1 > 1.5f && q.b1 < 2.5f;
        const V3 wind{hd.v.x, stuck ? 0.0f : hd.v.y * 0.5f, hd.v.z};
        q.v = q.v + (wind * 1.1f - q.v) * std::min(1.0f, 5.0f * f * dt);
        r.count[1] += 1;
        if (stuck && hd.v.y < -1.2f && f > 0.25f && r.flip > 0.0f) {  // swiped loose (it falls back up)
          q.b1 = 1.0f;
          q.v.y = hd.v.y * 0.5f;
          q.grav = -(light ? 0.35f : 0.6f) * kG;
          q.flutter = light ? 0.5f : 0.0f;
          q.spin = 3.0f;
        }
        if (len(q.p - hd.p) < 0.12f + 0.5f * q.size && sp > 0.8f && q.age - q.b2 > 0.2f) {
          q.v = hd.v * 1.2f;
          q.spin = 8.0f;
          q.b2 = q.age;
          if (q.b1 > 3.5f && r.flip > 0.0f) {
            q.b1 = 1.0f;
            q.grav = -0.6f * kG;
          }
          sfx(s, kSfxPunch, 0.5f);
          buzz(0.25f, 0.04f);
        }
      }
      break;
    }
    // ---- DISCO INFERNO
    case kRoTile: {
      const float k = rt - q.b0;
      if (k < 0.0f) {
        q.peak = 0.0f;
        break;
      }
      const float beat = rt * 2.0f;  // 120 a minute
      const int bi = static_cast<int>(std::floor(beat));
      const float bf = beat - static_cast<float>(bi);
      const int i = static_cast<int>(q.a0), j = static_cast<int>(q.a1);
      const float hue = 0.13f * static_cast<float>(i) + 0.29f * static_cast<float>(j) + 0.37f * static_cast<float>(bi);
      float lit = ((i + j + bi) & 1) == 0 ? 0.35f + 0.65f * std::exp(-3.0f * bf) : 0.22f;
      const float dist = flat_dist(q.p, s.H);
      lit = std::max(lit, 0.9f * std::exp(-std::pow((dist - bf * 2.4f) / 0.3f, 2.0f)));  // the beat's ring out from you
      if (r.next[3] > -1.0f) lit = std::max(lit, std::exp(-std::pow((dist - (rt - r.next[3]) * 4.0f) / 0.35f, 2.0f)));  // a clap's
      float white = clampf(1.0f - flat_dist(q.p, head) / 0.6f, 0.0f, 1.0f);  // under your feet as you move
      for (int h = 0; h < 2; ++h)
        if (s.hand[h].on) white = std::max(white, 0.6f * clampf(1.0f - flat_dist(q.p, s.hand[h].p) / 0.45f, 0.0f, 1.0f));
      q.c1 = r.finale ? hsv(0.03f + 0.05f * std::sin(rt * 9.0f + static_cast<float>(i + j)), 0.9f, 1.0f) : hsv(hue, 0.85f * (1.0f - white), 1.0f);
      q.peak = clampf(std::max(lit, white), 0.0f, 1.0f) * std::min(1.0f, k / 0.15f) * 0.9f;
      break;
    }
    case kRoBall: {
      r.cv = r.cv + (q.h0 * -14.0f - r.cv * 1.6f) * dt;  // its swing: a spring back to rest
      q.h0 = q.h0 + r.cv * dt;
      const float drop = ease_out(rt / 0.7f);
      q.p = V3{r.c.x, s.top + 0.3f + (r.c.y - s.top - 0.3f) * drop, r.c.z} + q.h0;
      q.v = V3{};
      q.c2a = static_cast<uint8_t>(static_cast<int>(rt * 60.0f) & 255);
      r.ball = q.p;
      if (!r.clearing && q.age - q.b2 > 0.3f) {  // punch it: it swings, sparkles fly
        const int h = hand_near(s, q.p, 0.5f * q.size + 0.08f, 0.5f);
        if (h >= 0) {
          r.cv = r.cv + s.hand[h].v * 0.8f;
          q.b2 = q.age;
          for (int k = 0; k < 18; ++k) {
            if (P* t = spawn(s, kShTwinkle, q.p, s.rng.unit() * s.rng.f(0.8f, 1.8f), 0.05f, 0.6f, hsv(s.rng.f(0.0f, 1.0f), 0.5f, 1.0f), 0xFFFFFF))
              t->drag = 1.2f;
          }
          sfx(s, kSfxChime, 0.9f);
          buzz(0.35f, 0.05f);
          r.count[0] += 1;
        }
      }
      if (s.rng.f(0.0f, 1.0f) < dt * 22.0f) {  // glints flying off it
        const V3 d = s.rng.unit();
        if (P* t = spawn(s, kShTwinkle, q.p + d * (0.5f * q.size), d * 1.2f, 0.04f, 0.5f, hsv(s.rng.f(0.0f, 1.0f), 0.4f, 1.0f), 0xFFFFFF))
          t->drag = 0.8f;
      }
      break;
    }
    case kRoBeam: {
      const int k = static_cast<int>(q.b0);
      V3 target;
      if (k < 0) {  // the ball's string, up to your ceiling
        target = V3{r.ball.x, s.top + 0.3f, r.ball.z};
        q.h1 = target;
      } else {
        if (k < 2 && s.hand[k].on) {
          target = s.hand[k].p;  // these two follow your hands
        } else {
          const float a = q.a0 + rt * (k % 2 == 0 ? 0.9f : -0.75f);
          target = V3{s.H.x + std::cos(a) * q.a1, s.floor, s.H.z + std::sin(a) * q.a1};
        }
        q.h1 = q.h1 + (target - q.h1) * std::min(1.0f, 8.0f * dt);
        r.spot[k] = q.h1;
        q.c1 = hsv(0.16f * static_cast<float>(k) + rt * 0.15f, 0.7f, 1.0f);
        q.c2 = q.c1;
      }
      const V3 d = q.h1 - r.ball;
      const float l = std::max(0.1f, len(d));
      q.axis = d * (1.0f / l);
      q.p = r.ball + d * 0.5f;
      q.size = l;
      q.aspect = k < 0 ? 0.02f : 0.22f;
      q.v = V3{};
      break;
    }
    case kRoSpot: {
      const int k = static_cast<int>(q.b0);
      const bool onHand = k < 2 && s.hand[k].on;
      if (onHand) {  // the spotlight on your hand
        q.p = r.spot[k];
        q.orient = kOrFace;
        q.size = 0.2f;
      } else {
        q.p = V3{r.spot[k].x, s.floor + 0.008f, r.spot[k].z};
        q.orient = kOrFixed;
        q.spin = 0.0f;
        q_flat(0.0f, q.q);
        q.size = 0.45f;
      }
      q.c1 = hsv(0.16f * static_cast<float>(k) + rt * 0.15f, 0.5f, 1.0f);
      q.v = V3{};
      break;
    }
    default:
      break;
  }
}

void room_step(Show& s, float dt, V3 head) {
  Room& r = s.room;
  if (!r.on) return;
  const float rt = s.age - r.t0, left = r.dur - rt;
  if (!r.clearing && left <= 0.0f) {
    r.clearing = true;
    room_clear(s);
    GLOG("goalfx: the takeover clears (%d, %d, %d)", r.count[0], r.count[1], r.count[2]);
  }
  if (left < -1.4f) {
    r.on = false;
    return;
  }
  s.holds[0] = s.holds[1] = false;
  for (const P& q : g_parts)
    if (q.held >= 0 && q.held < 2) s.holds[q.held] = true;
  room_director(s, rt, left, dt, head);
  const size_t n0 = g_parts.size();
  for (size_t i = 0; i < n0; ++i) {
    P& q = g_parts[i];
    if (q.role == kRoNone || q.age >= q.life) continue;
    room_piece(s, q, rt, left, dt, head);
  }
  if (r.kind == kToFlip && r.count[1] >= 6 && !r.clearing) {  // your hands blew a lot round: a whoosh
    sfx(s, kSfxWhoosh, 0.35f + 0.02f * static_cast<float>(std::min(20, r.count[1])));
    if (s.age - r.next[2] > 0.3f) {
      buzz(0.1f, 0.04f);
      r.next[2] = s.age;
    }
  }
}

// ---- r207: the CASINO WIN PARTY's own pieces
float ease_back(float x) {  // 0..1 with a little overshoot (a pop)
  x = clampf(x, 0.0f, 1.0f);
  const float c = 1.9f;
  return 1.0f + (c + 1.0f) * (x - 1.0f) * (x - 1.0f) * (x - 1.0f) + c * (x - 1.0f) * (x - 1.0f);
}
uint32_t mix_rgb(uint32_t a, uint32_t b, float t) {
  t = clampf(t, 0.0f, 1.0f);
  auto ch = [&](int sh) {
    const float x = static_cast<float>((a >> sh) & 255), y = static_cast<float>((b >> sh) & 255);
    return static_cast<uint32_t>(x + (y - x) * t + 0.5f) << sh;
  };
  return ch(16) | ch(8) | ch(0);
}
// a coin on your floor (`y` its resting height): bounces (losing most of it), rolls / slides to a stop, then spins down flat like a
// real coin (its tilt dying away as it wobbles faster: Euler's disk), lies there a few seconds and fades
void coin_land(Show& s, P& q, float y, float dt) {
  q.p.y = y;
  if (q.v.y < -0.35f) {
    q.v.y = -q.v.y * 0.38f;
    q.v.x *= 0.72f;
    q.v.z *= 0.72f;
    q.spin *= 0.55f;
    return;
  }
  q.v.y = 0.0f;
  const float k = std::exp(-2.8f * dt);
  q.v.x *= k;
  q.v.z *= k;
  q.spin *= std::exp(-4.0f * dt);
  if (q.b1 <= 0.0f && q.v.x * q.v.x + q.v.z * q.v.z < 0.04f) {
    q.b1 = q.age;                       // (it settles from now)
    q.a0 = s.rng.f(0.0f, 6.2831853f);   // (its wobble's way round)
    q.a1 = s.rng.f(0.35f, 0.55f);       // (its tilt as it starts)
    q.life = std::min(q.life, q.age + s.rng.f(2.6f, 3.8f));
    q.fadeOut = 0.7f;
  }
}
void coin_settle(P& q, float dt, float floorY) {
  const float st = q.age - q.b1;
  const float tilt = q.a1 * std::exp(-2.4f * st);
  q.a0 += (7.0f + 26.0f * (1.0f - tilt / std::max(0.05f, q.a1))) * dt;  // (the wobble speeds up as it flattens)
  float flat[4], tq[4], target[4];
  q_flat(q.b2, flat);
  const V3 ax{std::cos(q.a0), 0.0f, std::sin(q.a0)};
  q_axis_angle(ax, tilt, tq);
  q_mul(tq, flat, target);
  const float blend = std::min(1.0f, st / 0.15f);  // (from how it landed into the wobble)
  float dotq = 0.0f;
  for (int k = 0; k < 4; ++k) dotq += q.q[k] * target[k];
  const float sgn = dotq < 0.0f ? -1.0f : 1.0f;
  float len2 = 0.0f;
  for (int k = 0; k < 4; ++k) q.q[k] = q.q[k] + (sgn * target[k] - q.q[k]) * blend, len2 += q.q[k] * q.q[k];
  const float inv = 1.0f / std::sqrt(std::max(1e-12f, len2));
  for (int k = 0; k < 4; ++k) q.q[k] *= inv;
  q.spin = 0.0f;
  q.v = V3{};
  q.grav = 0.0f;
  q.p.y = floorY + 0.065f * q.size + 0.5f * q.size * std::sin(tilt);
}
// the letters, the siren, its beams and glow - posed from the machine every frame (they don't fly)
void casino_step(Show& s, P& q, V3 head) {
  const V3 up{0.0f, 1.0f, 0.0f};
  q.v = V3{};
  if (q.cas == 2) {  // a giant letter: rises out of the cabinet's top one after another, bobs, faces you, flies off up at the end
    const float rt = q.age - 0.07f * q.a2;
    const float cap = q.a0;
    const float rise = rt <= 0.0f ? 0.0f : ease_back(rt / 0.55f);
    const float left = q.life - q.age;
    const float ex = left < 0.6f ? (0.6f - left) / 0.6f : 0.0f;
    V3 c = (s.pull ? s.stage : s.cabTop) +  // (r219: a pull's word on its own stage, 2 m out)
           up * (0.05f * s.mScale + (0.22f * s.mScale + 0.5f * cap) * rise + 0.025f * s.mScale * std::sin(q.age * 2.6f + q.a2 * 0.8f) +
                 1.3f * s.mScale * ex * ex);
    c = c + s.mFwd * (0.06f * s.mScale);  // (a little out over its front)
    float qq[4];
    q_upright(c, head, qq);
    V3 z{head.x - c.x, 0.0f, head.z - c.z};
    z = norm(z, s.mFwd);
    const V3 x{z.z, 0.0f, -z.x};  // (cross(up, z): your right as you face it)
    q.p = c + x * q.a1;
    // a wobble as it lands, a little tilt to the beat
    float wq[4], out[4];
    q_axis_angle(z, 0.10f * std::sin(q.age * 3.4f + q.a2 * 1.3f) * (0.3f + 0.7f * (1.0f - std::min(1.0f, rt / 0.9f))), wq);
    q_mul(wq, qq, out);
    std::memcpy(q.q, out, sizeof(out));
    q.size = cap * (rt <= 0.0f ? 0.02f : std::max(0.02f, ease_back(rt / 0.4f))) * (1.0f - 0.5f * ex);
    // the bulbs' chase across the word: a white-hot flash running along it
    const float chase = std::pow(std::max(0.0f, std::sin(q.age * 8.0f - q.a2 * 0.9f)), 6.0f);
    q.c1 = mix_rgb(0xFFC830, 0xFFF6D0, 0.75f * chase);
    return;
  }
  // the siren: out of the cabinet's top with a pop, its beams turning, back in at the end
  const float k = std::min(ease_back(q.age / 0.32f), clampf((q.life - q.age) / 0.35f, 0.0f, 1.0f));
  const float sz = q.a0;
  const float ang = q.age * q.a1 * 6.2831853f;
  const float headAng = std::atan2(head.z - s.cabTop.z, head.x - s.cabTop.x);
  const float facing = std::max(std::cos(ang - headAng), std::cos(ang + 3.14159265f - headAng));
  const float glowK = std::pow(std::max(0.0f, facing), 6.0f);
  const V3 lens = s.cabTop + up * ((0.55f * sz - 0.6f * sz * (1.0f - k)) * 1.0f);
  if (q.cas == 3) {
    q.p = s.cabTop + up * (-0.6f * sz * (1.0f - k));
    q.size = sz * std::max(0.02f, k);
    q.q[0] = q.q[1] = q.q[2] = 0.0f, q.q[3] = 1.0f;
    q.glow = 0.35f + 0.65f * glowK;
  } else if (q.cas == 5) {
    q.p = lens;
    q.size = 0.45f * s.mScale * (0.55f + 0.7f * glowK) * k;
    q.peak = (0.30f + 0.55f * glowK) * k;
  } else if (q.cas == 4) {
    const float a = ang + q.b0 * 3.14159265f;
    const V3 d = norm(V3{std::cos(a), s.pull ? 0.32f : 0.10f, std::sin(a)});  // (r219: a pull's siren stands on your floor: its beams tilted up)
    q.axis = d;
    q.p = lens + d * (0.5f * q.size);
    q.peak = 0.34f * k;
  }
}

void step_parts(Show& s, float dt, V3 head) {
  size_t w = 0;
  for (size_t i = 0; i < g_parts.size(); ++i) {
    P q = g_parts[i];
    q.age += dt;
    if (q.age >= q.life) continue;
    if (q.cas >= 2) {  // r207: the casino party's letters / siren (posed from the machine)
      casino_step(s, q, head);
      g_parts[w++] = q;
      continue;
    }
    if (q.cas == 1 && q.b1 > 0.0f) {  // r207: a coin spinning down flat on your floor
      coin_settle(q, dt, q.h0.y);
      g_parts[w++] = q;
      continue;
    }
    if (q.anim != kAnNone) animate(s, q, dt, head);
    if (q.fall > 0.0f) {
      const float settle = 1.0f - std::exp(-2.6f * dt), side = 1.0f - std::exp(-1.6f * dt);
      q.v.y += (-q.fall - q.v.y) * settle;
      const float tt = q.age * 1.3f + q.phase;
      q.v.x += (std::sin(tt) * q.flutter - q.v.x) * side;
      q.v.z += (std::cos(tt * 0.8f + 1.3f) * q.flutter * 0.7f - q.v.z) * side;
    } else {
      q.v.y -= q.grav * dt;
      if (q.drag > 0.0f) q.v = q.v * std::exp(-q.drag * dt);
      if (q.flutter > 0.0f && q.anim == kAnNone) {
        q.v.x += std::sin(q.age * 2.1f + q.phase) * q.flutter * dt;
        q.v.z += std::cos(q.age * 1.7f + q.phase) * q.flutter * dt;
      }
    }
    if (q.orbit != 0.0f) {
      const float a = q.orbit * dt, c = std::cos(a), sn = std::sin(a);
      const float rx = q.p.x - s.H.x, rz = q.p.z - s.H.z;
      q.p.x = s.H.x + rx * c - rz * sn;
      q.p.z = s.H.z + rx * sn + rz * c;
      const float vx = q.v.x, vz = q.v.z;
      q.v.x = vx * c - vz * sn;
      q.v.z = vx * sn + vz * c;
    }
    const V3 was = q.p;  // r197: (where it came from: the room scan's surface under it, its walls)
    if (q.anim != kAnColumn && q.anim != kAnStem && q.anim != kAnLeaf && q.anim != kAnHead && q.anim != kAnBomb)
      q.p = q.p + q.v * dt;
    q.size += q.grow * dt;
    q.roll += q.rollSpeed * dt;
    if (q.orient == kOrFixed && q.spin != 0.0f) {
      float dq[4], nq[4];
      q_axis_angle(q.axis, q.spin * dt, dq);
      q_mul(dq, q.q, nq);
      std::memcpy(q.q, nq, sizeof(nq));
    }
    if (q.inPicture) {
      const float d = dot(q.p - s.O, s.D);
      if (d < 0.015f) q.p = q.p + s.D * (0.015f - d);
    }
    // the floor
    const float rest = q.cas == 1 ? 0.065f * q.size : (q.mesh >= 0 ? 0.3f * q.size : 0.0f);  // (r207: a coin lies thin)
    // r197: your room scan - what falls lands on what's really under it (a table top, the couch seat, a shelf, the
    // floor) and bounces off your walls and the furniture's sides; the floor plane (s.floor) without one
    float floorY = s.floor;
    if (s.scan != nullptr && q.floor != kFlNone && q.anim == kAnNone && !q.inPicture) {
      float pp[3] = {q.p.x, q.p.y, q.p.z}, vv[3] = {q.v.x, q.v.y, q.v.z};
      const float pr[3] = {was.x, was.y, was.z};
      if (xrroom::collide(*s.scan, pp, vv, pr, std::max(rest, 0.01f), 0.35f, 0.55f, nullptr,
                          xrroom::kCollWalls | xrroom::kCollSides | xrroom::kCollCeiling) != xrroom::kHitNone) {
        q.p = V3{pp[0], pp[1], pp[2]};
        q.v = V3{vv[0], vv[1], vv[2]};
        q.spin *= 0.7f;
      }
      floorY = xrroom::surface_below(*s.scan, q.p.x, was.y - rest, q.p.z, nullptr);
    }
    if (q.floor != kFlNone && q.p.y < floorY + rest && q.anim == kAnNone) {
      switch (q.floor) {
        case kFlBounce:
          if (q.cas == 1) {  // r207: a casino coin
            q.h0.y = floorY;
            coin_land(s, q, floorY + rest, dt);
            break;
          }
          q.p.y = floorY + rest;
          q.v.y = q.v.y < -0.3f * s.u ? -q.v.y * 0.35f : 0.0f;
          q.v.x *= 0.55f;
          q.v.z *= 0.55f;
          q.spin *= 0.6f;
          break;
        case kFlDie:
          q.life = q.age;
          break;
        case kFlSettle:
          q.p.y = floorY + rest;
          q.v = V3{};
          q.fall = 0.0f;
          q.grav = 0.0f;
          q.spin = 0.0f;
          q.floor = kFlNone;
          q.life = std::min(q.life, q.age + 0.7f);
          q.fadeOut = 0.7f;
          break;
        case kFlShatter:
          for (int k = 0; k < 6; ++k) {
            V3 d = s.rng.unit();
            d.y = std::fabs(d.y);
            if (P* t = spawn(s, kShTwinkle, V3{q.p.x, floorY + 0.02f, q.p.z}, d * (0.9f * s.u), 0.04f * s.u, 0.4f, 0xFFFFFF, 0x8FDCFF)) {
              t->grav = 0.5f * G(s, s.u);
              t->drag = 1.5f;
            }
          }
          q.life = q.age;
          break;
        default:
          break;
      }
      if (q.age >= q.life) continue;
    }
    if (q.p.y < s.floor - 1.5f * s.u) continue;
    g_parts[w++] = q;
  }
  g_parts.resize(w);
}

// ------------------------------------------------------------------------------------------------------------------
// A goal / preview coming in: where everything is in this view
// ------------------------------------------------------------------------------------------------------------------
V3 head_of(const xrfx::Scene& sc) {
  return V3{0.5f * (sc.views[0].pose.position.x + sc.views[1].pose.position.x),
            0.5f * (sc.views[0].pose.position.y + sc.views[1].pose.position.y),
            0.5f * (sc.views[0].pose.position.z + sc.views[1].pose.position.z)};
}
V3 look_of(const xrfx::Scene& sc) { return rot(sc.views[0].pose.orientation, V3{0.0f, 0.0f, -1.0f}); }

// Game (x, y, z) -> LOCAL (xr_fx.cpp game_to_room)
V3 game_to_room(float scale, float yaw, const XrVector3f& origin, float gx, float gy, float gz) {
  const float a = gx, b = gz, c = -gy;
  const float cs = std::cos(yaw), sn = std::sin(yaw);
  return V3{(a * cs + c * sn) * scale + origin.x, b * scale + origin.y, (-a * sn + c * cs) * scale + origin.z};
}

void start(const xrfx::Scene& sc, const Pending& pd) {
  Show& s = g_show;
  s = Show{};
  s.on = true;
  s.def = pd.def;
  s.preview = pd.preview;
  s.theirs = pd.theirs;
  s.rng.x = 0x9E3779B9u ^ (g_seed++ * 0x85EBCA6Bu);
  if (s.rng.x == 0) s.rng.x = 1;
  s.acc.assign(static_cast<size_t>(s.def->beatCount), 0.0f);
  s.done.assign(static_cast<size_t>(s.def->beatCount), 0);
  s.H = head_of(sc);
  s.floor = sc.floorY;
  const char* view = "big screen";
  if (sc.mr) {
    s.u = 1.0f;
    s.R = 2.2f;
    s.top = std::max(s.floor + 2.35f, s.H.y + 0.45f);
    if (sc.room != nullptr) s.top = std::min(s.top, xrroom::ceiling_y(*sc.room) - 0.05f);  // r197: your real ceiling
  } else if (sc.roomShown || sc.suite) {
    s.u = 1.35f;
    s.R = 3.0f;
    s.top = s.floor + 3.1f;
  } else {
    s.u = 3.0f;
    s.R = 7.5f;
    s.top = s.floor + 8.5f;
  }
  if (pd.casino > 0) {
    // r207: the CASINO WIN PARTY at the TIZM machine: its "goal" the payout tray, its unit the machine's size (metres), the room's
    // own scale whatever the view (your room in MR, the VR room round you), its floor the machine's
    view = pd.pull ? "your pull" : "the STRIKERZ machine";
    s.casino = pd.casino;
    s.pull = pd.pull;  // r219
    s.stage = pd.stage;
    std::memcpy(s.word, pd.word, sizeof(s.word));
    s.mScale = std::max(0.5f, pd.mScale);
    s.mFwd = norm(pd.mFwd, V3{0.0f, 0.0f, 1.0f});
    s.cabTop = pd.cabTop;
    s.O = pd.tray;
    s.D = norm(s.mFwd + V3{0.0f, 0.35f, 0.0f});
    s.g = s.mScale;
    s.floor = pd.floorY;
    s.u = 1.0f;
    s.R = 2.2f;
    s.top = std::max(s.floor + 2.35f, s.H.y + 0.45f);
    if (sc.mr && sc.room != nullptr) s.top = std::min(s.top, xrroom::ceiling_y(*sc.room) - 0.05f);
  } else if (pd.preview) {
    view = "preview";
    s.O = v3(pd.where.position);
    s.D = norm(rot(pd.where.orientation, V3{0.0f, 0.0f, 1.0f}), V3{0.0f, 0.0f, 1.0f});
    s.g = 0.35f * s.u;
  } else if (sc.tabletop) {
    view = "Tabletop";
    s.O = game_to_room(sc.tableScale, sc.tableYaw, sc.tablePos, pd.gx, pd.gy, std::max(0.5f, pd.gz));
    const V3 toYou = norm(V3{s.H.x - s.O.x, 0.0f, s.H.z - s.O.z});
    s.D = norm(V3{0.0f, 1.0f, 0.0f} + toYou * 0.5f);
    s.g = clampf(sc.tableScale * 5.0f, 0.08f, 0.3f);
  } else if (!sc.mr && !sc.suite && !sc.roomShown && !sc.screenValid) {
    // r158 (Lance, Jumbotron · Stadium: "im not really ever looking at my goal in VR when im playing jumbotron"): with
    // the big screen up it goes off on the screen's goal (below), the life-size stadium's goal only without one
    view = "stadium";
    s.O = game_to_room(sc.stadiumScale, 0.0f, sc.stadiumOrigin, pd.gx, pd.gy, std::max(0.5f, pd.gz));
    const V3 toYou = norm(V3{s.H.x - s.O.x, 0.0f, s.H.z - s.O.z});
    s.D = norm(V3{0.0f, 1.0f, 0.0f} + toYou * 0.4f);
    s.g = 2.6f * sc.stadiumScale;
  } else if (sc.screenValid) {
    const XrPosef& p = sc.screenPose;
    s.S1 = rot(p.orientation, V3{1.0f, 0.0f, 0.0f});
    s.S2 = rot(p.orientation, V3{0.0f, 1.0f, 0.0f});
    s.D = rot(p.orientation, V3{0.0f, 0.0f, 1.0f});
    float nx = pd.ndcValid ? clampf(pd.ndcX, -0.92f, 0.92f) : (pd.gx > 0.0f ? 0.6f : -0.6f);
    float ny = pd.ndcValid ? clampf(pd.ndcY, -0.9f, 0.9f) : -0.1f;
    s.O = v3(p.position) + s.S1 * (nx * 0.5f * sc.screenWidth) + s.S2 * (ny * 0.5f * sc.screenHeight);
    s.g = std::max(0.15f, sc.screenWidth / 9.0f);
    s.picture = true;
  } else {
    view = "in front of you";
    V3 f = look_of(sc);
    f.y = 0.0f;
    f = norm(f);
    s.O = s.H + f * 2.5f;
    s.D = f * -1.0f;
    s.g = 0.35f;
  }
  V3 f{s.O.x - s.H.x, 0.0f, s.O.z - s.H.z};
  if (len(f) < 0.2f) {
    f = look_of(sc);
    f.y = 0.0f;
  }
  s.fwd = norm(f);
  s.right = V3{-s.fwd.z, 0.0f, s.fwd.x};
  const float dist = len(V3{s.O.x - s.H.x, 0.0f, s.O.z - s.H.z});
  const float df = std::max(0.5f * s.u, std::min(std::min(1.4f * s.u, 0.6f * dist), dist - 0.2f));
  s.front = V3{s.H.x + s.fwd.x * df, s.floor, s.H.z + s.fwd.z * df};
  s.side = V3{s.H.x + s.fwd.x * 1.2f * s.u + s.right.x * 0.95f * s.u, s.floor, s.H.z + s.fwd.z * 1.2f * s.u + s.right.z * 0.95f * s.u};
  s.arrive = clampf(len(s.O - s.H) / (s.def->waveSpeed * s.u), 0.12f, 1.1f);
  s.end = s.def->item.seconds + s.arrive;
  g_playing = true;
  GLOG("goalfx: %s%s%s in the %s view (goal unit %.2f m, room unit %.2f, the wave reaches you at %.2f s)",
       s.def->item.name, pd.preview ? " (preview)" : "", pd.theirs ? " (the friend's)" : "", view, s.g, s.u, s.arrive);
}

}  // namespace

// ------------------------------------------------------------------------------------------------------------------
// The shop's side
// ------------------------------------------------------------------------------------------------------------------
const std::vector<Item>& catalogue() {
  static const std::vector<Item> kItems = []() {
    std::vector<Item> v;
    for (const Def& d : kDefs) v.push_back(d.item);
    return v;
  }();
  return kItems;
}
const Item* find(const char* id) {
  const Def* d = def_of(id);
  return d != nullptr ? &d->item : nullptr;
}
const char* tier_name(int tier) {
  static const char* kNames[] = {"FREE", "COMMON", "RARE", "EPIC", "LEGENDARY", "MYTHIC"};
  return tier >= 0 && tier < 6 ? kNames[tier] : "";
}
uint32_t tier_rgb(int tier) {
  static const uint32_t kRgb[] = {0xC8CCD8, 0x5AD06A, 0x3A8CFF, 0xB050FF, 0xFFB020, 0xFF3A5A};
  return tier >= 0 && tier < 6 ? kRgb[tier] : 0xFFFFFF;
}

void set_wallet(const Wallet& wallet) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_wallet = wallet;
}

bool owned(const char* id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  load_locked();
  return owned_locked(id);
}

Buy try_buy(const char* id, long long* coinsLeft) {
  Wallet w;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
    w = g_wallet;
    if (def_of(id) == nullptr) return Buy::kUnknown;
    if (owned_locked(id)) {
      if (coinsLeft != nullptr) *coinsLeft = w.balance != nullptr ? w.balance() : 0;
      return Buy::kAlreadyOwned;
    }
  }
  if (w.balance == nullptr || w.spend == nullptr) return Buy::kNoWallet;
  const Def* d = def_of(id);
  // the coins first (the cards save them), then it is ours (saved here)
  const std::string what = std::string("goal explosion ") + d->item.name;
  if (w.balance() < d->item.price || !w.spend(d->item.price, what.c_str())) {
    if (coinsLeft != nullptr) *coinsLeft = w.balance();
    return Buy::kNotEnough;
  }
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!owned_locked(id)) g_owned.emplace_back(id);
    save_locked();
  }
  ++g_gen;
  if (coinsLeft != nullptr) *coinsLeft = w.balance();
  GLOG("goalfx: bought %s for %d BIT coins", d->item.name, d->item.price);
  return Buy::kBought;
}
bool buy(const char* id, long long* coinsLeft) {
  const Buy b = try_buy(id, coinsLeft);
  return b == Buy::kBought || b == Buy::kAlreadyOwned;
}

bool equip(const char* id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  load_locked();
  std::string want = id == nullptr || *id == '\0' ? "none" : id;
  if (want != "none" && !owned_locked(want.c_str())) return false;
  if (want != g_equipped) {
    g_equipped = want;
    save_locked();
    ++g_gen;
    GLOG("goalfx: %s equipped", g_equipped.c_str());
  }
  return true;
}

const char* equipped() {
  std::lock_guard<std::mutex> lock(g_mutex);
  load_locked();
  if (g_equipped == "none") return "none";
  const Def* d = def_of(g_equipped.c_str());
  return d != nullptr ? d->item.id : "classic";
}

void preview(const char* id, const XrPosef& where) {
  const Def* d = def_of(id);
  if (d == nullptr) return;
  std::lock_guard<std::mutex> lock(g_showMutex);
  g_pending = Pending{};
  g_pending.on = true;
  g_pending.def = d;
  g_pending.preview = true;
  g_pending.where = where;
  g_playing = true;
}

void casino_party(int tier, const XrVector3f& tray, const XrVector3f& cabTop, const XrVector3f& front, float scale, float floorY) {
  if (tier <= 0) return;
  const int t = std::min(4, tier);
  std::lock_guard<std::mutex> lock(g_showMutex);
  g_pending = Pending{};
  g_pending.on = true;
  g_pending.def = &kCasinoDefs[t - 1];
  g_pending.casino = t;
  g_pending.tray = v3(tray);
  g_pending.cabTop = v3(cabTop);
  g_pending.mFwd = V3{front.x, 0.0f, front.z};
  g_pending.mScale = scale;
  g_pending.floorY = floorY;
  g_playing = true;
}
float casino_flash() { return g_casFlash.load(); }

// r219: a big pull's party (BIG-PULLS.md 3) - r207's party engine with its own tables (kPullDefs), at the card in your hand: its "tray"
// the card (coins, confetti, the sparkle out of it), its word's stage 2 m out along where the card is (from your head, level; a little
// over your eyes), the siren's floor spot 2.2 m out beside the word, the room's own scale (1 m)
void pull_party(int tier, const char* word, const XrVector3f& card, const XrPosef& head, float floorY) {
  if (tier <= 0) return;
  const int t = std::min(4, tier);
  const V3 c = v3(card), h = v3(head.position);
  V3 f{c.x - h.x, 0.0f, c.z - h.z};
  if (len(f) < 0.12f) f = rot(head.orientation, V3{0.0f, 0.0f, -1.0f});  // (the card right under your eyes: where you look)
  f.y = 0.0f;
  f = norm(f, V3{0.0f, 0.0f, -1.0f});
  std::lock_guard<std::mutex> lock(g_showMutex);
  g_pending = Pending{};
  g_pending.on = true;
  g_pending.def = &kPullDefs[t - 1];
  g_pending.casino = t;
  g_pending.pull = true;
  g_pending.tray = c;
  g_pending.stage = V3{h.x + f.x * 2.0f, h.y + 0.05f, h.z + f.z * 2.0f};
  g_pending.cabTop = V3{h.x + f.x * 2.2f - f.z * 0.55f, floorY, h.z + f.z * 2.2f + f.x * 0.55f};  // (a little to your right of the word)
  g_pending.mFwd = f;
  g_pending.mScale = 1.0f;
  g_pending.floorY = floorY;
  std::snprintf(g_pending.word, sizeof(g_pending.word), "%s", word != nullptr ? word : "");
  g_playing = true;
}

bool playing() { return g_playing.load(); }

void stop() {
  std::lock_guard<std::mutex> lock(g_showMutex);
  g_pending.on = false;
  g_stopAsked = true;
}

int generation() { return g_gen.load(); }

// ------------------------------------------------------------------------------------------------------------------
// The game's side
// ------------------------------------------------------------------------------------------------------------------
void set_friend_equipped(const char* id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::string v = id != nullptr ? id : "";
  if (v != g_friend) {
    g_friend = v;
    ++g_gen;
    GLOG("goalfx: the friend's goal explosion: %s", g_friend.empty() ? "(none sent)" : g_friend.c_str());
  }
}
const char* friend_equipped() {
  std::lock_guard<std::mutex> lock(g_mutex);
  const Def* d = def_of(g_friend.c_str());
  return d != nullptr ? d->item.id : (g_friend == "none" ? "none" : "");
}

bool pieces_up() { return !g_parts.empty(); }

bool update(const xrfx::Scene& sc) {
  preload_sounds();  // (once, on a thread: read before the first goal)
  static bool s_loaded = false;
  if (!s_loaded) {  // (the saved choice read now, not at the goal)
    s_loaded = true;
    std::lock_guard<std::mutex> lock(g_mutex);
    load_locked();
  }
  if (g_parts.capacity() < kMaxParts + 64) g_parts.reserve(kMaxParts + 64);
  Pending pd;
  bool stopNow = false;
  {
    std::lock_guard<std::mutex> lock(g_showMutex);
    pd = g_pending;
    g_pending.on = false;
    stopNow = g_stopAsked;
    g_stopAsked = false;
  }
  const float dt = clampf(sc.dt, 0.0f, 0.1f);
  const V3 head = head_of(sc);
  if (stopNow) {
    g_show.on = false;
    g_show.room.on = false;  // r161
    for (P& q : g_parts) q.life = std::min(q.life, q.age + 0.25f), q.fadeOut = 0.25f, q.role = kRoNone, q.held = -1;
  }
  if (pd.on && pd.def != nullptr && pd.casino == 1 && g_show.on && g_show.casino >= 2 && pd.pull == g_show.pull) {  // (r219: a pull's pop in a pull's party)
    // r207: a small win while a big one's party is still going: its pop goes off inside that party (nothing cut short)
    for (int i = 0; i < pd.def->beatCount; ++i) emit(g_show, pd.def->beats[i], scaled(pd.def->beats[i], pd.def->beats[i].n), 0);
    GLOG("goalfx: a casino pop inside the party still going");
  } else if (pd.on && pd.def != nullptr) {
    // a new one: what the last one left in the air fades out quickly (r207: a casino party after a casino party keeps the coins
    // lying on your floor and the confetti still falling - only the last one's letters and siren go)
    const bool keep = pd.casino > 0 && g_show.casino > 0;
    for (P& q : g_parts) {
      if (keep && (q.cas == 1 || q.cas == 0)) continue;
      q.life = std::min(q.life, q.age + 0.3f), q.fadeOut = 0.3f, q.role = kRoNone, q.held = -1;
    }
    start(sc, pd);
  }
  Show& s = g_show;
  {  // r207: the casino party's gold room flash (casino_flash): its pulses, each a quick rise and an 0.12 s fall
    float f = 0.0f;
    if (s.casino > 0)
      for (int i = 0; i < s.pulses; ++i) {
        const float t = s.age - s.pulseAt[i];
        if (t < 0.0f || t > 1.0f) continue;
        f += s.pulsePeak[i] * (t < 0.04f ? t / 0.04f : std::exp(-(t - 0.04f) / 0.12f));
      }
    g_casFlash = std::min(1.0f, f);
  }
  s.scan = sc.mr && !s.preview ? sc.room : nullptr;  // r197
  {  // r161: your hands as hands() last saw them (gone if it hasn't seen them for a quarter second)
    std::lock_guard<std::mutex> lock(g_showMutex);
    const uint64_t now = mono_ns();
    for (int h = 0; h < 2; ++h) {
      HandSt& hd = s.hand[h];
      const HandIn& in = g_handIn[h];
      hd.gripWas = hd.grip;
      hd.trigWas = hd.trig;
      hd.on = in.on && now - in.ns < 250000000ull;
      hd.p = in.p;
      hd.v = hd.on ? in.v : V3{};
      hd.aimP = in.aimP;
      hd.aimD = in.aimD;
      hd.grip = hd.on && in.grip;
      hd.trig = hd.on ? in.trig : 0.0f;
    }
  }
  if (s.on && g_liveKnown.load() && !g_playLive.load()) s.room.sawDead = true;
  if (s.on) {
    const float before = s.age;
    s.age += dt;
    for (int i = 0; i < s.def->beatCount; ++i) {
      const Beat& b = s.def->beats[i];
      const float t0 = b.t + (b.base == kTA ? s.arrive : 0.0f);
      if (b.dur <= 0.0f || b.em == kTakeover) {  // (r161: a takeover's dur is its room's length - it starts once, at t0)
        if (!s.done[i] && s.age >= t0) {
          s.done[i] = 1;
          emit(s, b, scaled(b, b.n), 0);
        }
        continue;
      }
      const float t1 = t0 + b.dur;
      if (s.age < t0 || before >= t1) continue;
      const float covered = std::min(s.age, t1) - std::max(before, t0);
      s.acc[i] += static_cast<float>(scaled(b, b.n)) * covered / b.dur;
      const int k = static_cast<int>(s.acc[i]);
      s.acc[i] -= static_cast<float>(k);
      for (int j = 0; j < k; ++j) emit(s, b, 1, static_cast<int>(s.done[i]++));
    }
    if (s.age > s.end) s.on = false;
  } else if (s.room.on) {
    s.age += dt;  // (r161: a takeover still clearing keeps its clock)
  }
  // r161: a room takeover - the match live again (the goal's show over, or it never stopped) clears it; then its frame
  if (s.room.on && !s.room.clearing && !s.preview && g_liveKnown.load() && g_playLive.load() &&
      (s.room.sawDead || s.age - s.room.t0 > 2.5f)) {
    s.room.dur = s.age - s.room.t0;
    GLOG("goalfx: play is live again - the room takeover clears");
  }
  if (s.room.on) room_step(s, dt, head);
  g_ownHands = s.room.on && !s.room.clearing && (s.preview || !g_playLive.load());
  if (!g_parts.empty()) step_parts(s, dt, head);
  g_peakParts = std::max(g_peakParts, static_cast<int>(g_parts.size()));
  const bool alive = s.on || s.room.on || !g_parts.empty();
  if (!alive && g_playing.load()) {
    bool waiting = false;
    {
      std::lock_guard<std::mutex> lock(g_showMutex);
      waiting = g_pending.on;
    }
    if (!waiting) {
      g_playing = false;
      GLOG("goalfx: over (at most %d pieces at once)", g_peakParts);
      g_peakParts = 0;
    }
  }
  return alive;
}

uint32_t write(const xrfx::Scene& sc, Piece* out, uint32_t maxPieces, MeshRun* runs, int maxRuns, int* runCount,
               uint32_t* quadFirst) {
  *runCount = 0;
  *quadFirst = 0;
  if (g_parts.empty() || maxPieces == 0) return 0;
  const V3 head = head_of(sc);
  const V3 look = look_of(sc);
  Show& s = g_show;
  // the 3D pieces by mesh, then the quads: the dimming sheets first, the rest, the lit sheets last
  // (a 3D piece far from you or lying still is drawn with its mesh's reduced detail: its run's mesh + kReduced, as the
  // celebration's)
  constexpr int kReduced = 1000;
  const float farSq = (1.1f * s.u) * (1.1f * s.u);
  std::vector<int> meshOf(g_parts.size(), -1);
  std::vector<uint32_t> order;
  order.reserve(g_parts.size());
  for (uint32_t i = 0; i < g_parts.size(); ++i) {
    const P& q = g_parts[i];
    if (q.mesh < 0) continue;
    const V3 d = q.p - head;
    const bool still = q.floor == kFlNone && q.v.x == 0.0f && q.v.y == 0.0f && q.v.z == 0.0f;
    meshOf[i] = q.mesh + ((dot(d, d) > farSq || still) ? kReduced : 0);
    order.push_back(i);
  }
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return meshOf[a] < meshOf[b]; });
  const size_t meshes = order.size();
  for (int pass = 0; pass < 3; ++pass) {
    for (uint32_t i = 0; i < g_parts.size(); ++i) {
      const P& q = g_parts[i];
      if (q.mesh >= 0) continue;
      const bool veil = q.orient == kOrVeil;
      const int want = veil ? (q.glow < 0.5f ? 0 : 2) : 1;
      if (want == pass) order.push_back(i);
    }
  }
  uint32_t n = 0;
  for (size_t k = 0; k < order.size() && n < maxPieces; ++k) {
    const P& q = g_parts[order[k]];
    float alpha = q.peak;
    if (q.ringFade) {
      alpha *= std::max(0.0f, 1.0f - q.age / q.life);
    } else {
      if (q.fadeIn > 0.0f) alpha *= std::min(1.0f, q.age / q.fadeIn);
      alpha *= clampf((q.life - q.age) / std::max(0.02f, q.fadeOut), 0.0f, 1.0f);
    }
    if (q.flicker) alpha *= (1.0f - (q.age / q.life) * (q.age / q.life)) * s.rng.f(0.55f, 1.0f);
    // near your eyes it shrinks away (nothing in your face), but for the sheets and the wave meant to be round you
    float shrink = 1.0f;
    if (q.a3 < 0.5f && q.orient != kOrVeil && q.size < 0.6f * s.u) {
      const float d = len(q.p - head);
      shrink = clampf((d - 0.15f) / (0.25f + 0.6f * q.size), 0.0f, 1.0f);
      alpha *= std::min(1.0f, 1.5f * shrink);
    }
    if (alpha <= 0.003f || shrink <= 0.0f) continue;
    Piece& o = out[n];
    V3 p = q.p;
    float qq[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    switch (q.orient) {
      case kOrFace: break;
      case kOrFaceRoll: q_facing(p, head, q.roll, qq); break;
      case kOrUpright: q_upright(p, head, qq); break;
      case kOrAlong: q_along(p, q.v, head, qq); break;
      case kOrAlongFixed: q_along(p, q.axis, head, qq); break;
      case kOrFlat: q_flat(q.roll, qq); break;
      case kOrFixed: std::memcpy(qq, q.q, sizeof(qq)); break;
      case kOrVeil: p = head + look * 0.3f; break;
    }
    o.posSize[0] = p.x;
    o.posSize[1] = p.y;
    o.posSize[2] = p.z;
    o.posSize[3] = q.size * shrink;
    std::memcpy(o.quat, qq, sizeof(qq));
    o.color1 = pack(q.c1, 255);
    o.color2 = pack(q.c2, q.c2a);
    o.params[0] = static_cast<float>(q.shape);
    o.params[1] = q.aspect;
    o.params[2] = alpha;
    o.params[3] = q.glow;
    if (k < meshes) {
      const int m = meshOf[order[k]];
      if (*runCount > 0 && runs[*runCount - 1].mesh == m) {
        ++runs[*runCount - 1].count;
      } else if (*runCount < maxRuns) {
        runs[*runCount] = MeshRun{m, n, 1};
        ++*runCount;
      } else {
        continue;  // (no run left for it)
      }
      *quadFirst = n + 1;
    }
    ++n;
  }
  return n;
}

int take_earned() { return g_earned.exchange(0); }  // r164

float take_buzz(float* seconds) {
  std::lock_guard<std::mutex> lock(g_showMutex);
  const float a = g_buzzAmp;
  if (seconds != nullptr) *seconds = g_buzzSec;
  g_buzzAmp = 0.0f;
  g_buzzSec = 0.0f;
  return a;
}

bool hands(const XrPosef* grip[2], const XrPosef* aim[2], const bool gripHeld[2], const float trigger[2], bool playLive) {
  const uint64_t now = mono_ns();
  {
    std::lock_guard<std::mutex> lock(g_showMutex);
    for (int h = 0; h < 2; ++h) {
      HandIn& in = g_handIn[h];
      if (grip[h] == nullptr) {
        in.on = false;
        in.v = V3{};
        continue;
      }
      const V3 p = v3(grip[h]->position);
      if (in.on && in.ns != 0) {  // how fast it moves (smoothed a little: a punch reads, the tracking's jitter doesn't)
        const float dt = static_cast<float>(now - in.ns) * 1e-9f;
        if (dt > 1e-3f && dt < 0.2f) in.v = in.v * 0.35f + (p - in.p) * (0.65f / dt);
      } else {
        in.v = V3{};
      }
      in.on = true;
      in.p = p;
      in.ns = now;
      if (aim[h] != nullptr) {
        in.aimP = v3(aim[h]->position);
        in.aimD = norm(rot(aim[h]->orientation, V3{0.0f, 0.0f, -1.0f}));
      } else {  // (no aim pose: along the controller, a little down)
        in.aimP = p;
        in.aimD = norm(rot(grip[h]->orientation, V3{0.0f, -0.5f, -0.87f}));
      }
      in.grip = gripHeld[h];
      in.trig = trigger[h];
    }
  }
  g_playLive = playLive;
  g_liveKnown = true;
  return g_ownHands.load();
}

}  // namespace xrgoalfx

// ------------------------------------------------------------------------------------------------------------------
// Game.cpp's goal
// ------------------------------------------------------------------------------------------------------------------
extern "C" void PortGoalFxGoal(int side, int humanPad, int ownGoal, float x, float y, float z, float ndcX, float ndcY,
                               int ndcValid) {
  using namespace xrgoalfx;
  const int local = xrnet::local_pad();
  const bool online = xrnet::lockstep();
  const int mySide = xrhome::human_side();
  bool mine = false, theirs = false;
  if (!ownGoal && humanPad >= 0) {
    // a person scored: whose pad
    mine = humanPad == local;
    theirs = online && !mine;
  } else {
    // the computer scored for a side (or an own goal gave it): the side's owner
    mine = mySide >= 0 && side == mySide;
    theirs = online && mySide >= 0 && side != mySide;
  }
  const char* id = mine ? equipped() : (theirs ? friend_equipped() : "");
  const Def* d = def_of(id);
  if (d == nullptr) {
    if (mine || theirs) GLOG("goalfx: goal for side %d (%s): none equipped", side, mine ? "yours" : "the friend's");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_showMutex);
    g_pending = Pending{};
    g_pending.on = true;
    g_pending.def = d;
    g_pending.theirs = theirs;
    g_pending.gx = x;
    g_pending.gy = y;
    g_pending.gz = z;
    g_pending.ndcX = ndcX;
    g_pending.ndcY = ndcY;
    g_pending.ndcValid = ndcValid != 0;
  }
  g_playing = true;
  // the goal's usual show (confetti, fireworks, the flash) waits until this has had its moment
  xrfx::hold_celebration(std::max(1.2f, d->item.seconds - 0.7f));
  GLOG("goalfx: goal for side %d by pad %d%s: %s's %s", side, humanPad, ownGoal ? " (own goal)" : "", mine ? "your" : "the friend",
       d->item.name);
}

#else  // not the Quest's OpenXR build: nothing goes off

extern "C" void PortGoalFxGoal(int, int, int, float, float, float, float, float, int) {}

#endif
