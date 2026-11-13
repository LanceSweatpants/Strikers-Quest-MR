// strikers-quest r219 (Lance 2026-11-11, C:\StrikersMR\BIG-PULLS.md): BIG PULLS - the numbers of a hit's reveal in one pure header
// (no OpenXR, no state), shared by xr_cards.cpp (the pack in your hand), xr_goalfx.cpp's pull party (the tier / word) and the PC
// proofs (tools/outside/pulls219), so the proof times exactly what the headset plays.
//
//   1 SLOW-MO BUILD-UP: a hit's card LIFTS toward your eyes, TURNS SLOWLY to edge-on with a speeding HEARTBEAT and the controllers
//     pulsing harder each beat, then SNAPS round - its reveal (xr_cards reveal_fx) where the snap passes half way. Its length +
//     strength by the hit: SILVER short, GOLD / the case hits longer, BLACK 1/1 + a GOD PACK's last card longest; a GRAIL character or a
//     JAM '96 card a notch more. BASE cards: none (the 0.5 s flip of r139, its moment at half way - unchanged).
//   2 THE ROOM GOES DARK for a BLACK 1/1 (either set): dark over 1.2 s, held, the lights SLAM back at the reveal (0.15 s) with one
//     white flash (~0.35 s). Smooth: no strobe, nothing moves your view.
//   3 THE PARTY: its tier (1 POP .. 4 JACKPOT) + its word for xr_goalfx's pull_party.
#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "xr_exq.h"  // r230: LANCINI EXQUISITE's show by tier

namespace xrpulls {

enum { kTierNone = 0, kTierShort = 1, kTierLong = 2, kTierLongest = 3, kTierGod = 4 };
enum { kPartyNone = 0, kPartyPop = 1, kPartyBig = 2, kPartyHuge = 3, kPartyJackpot = 4 };

// What's being turned over (xr_cards' Ref + what the market knows of its character)
struct Pull {
  int parallel = 0;     // 0 BASE, 1 SILVER TIZM, 2 GOLD TIZM /10, 3 BLACK TIZM 1/1
  bool shiny = false;
  int hit = 0;          // 0 none, 1 TIZM BOOM, 2 HOMETOWN, 3 SIGNED
  bool grail = false;   // its character a GRAIL (the market's demand tier 5)
  bool jam = false;     // a JAM '96 card
  bool godLast = false; // the last card of a GOD PACK
  int exq = 0;           // r230: a LANCINI EXQUISITE card's tier (xr_exq.h; 0 not one)
  bool rookie = false;   // r230: an ORIGINALS rookie
};

struct Plan {
  int tier = kTierNone;
  int notches = 0;            // a grail / a JAM card: 1 each
  float lift = 0.0f;          // s: up out of the stack toward you
  float slow = 0.0f;          // s: the slow turn to edge-on
  float snap = 0.0f;          // s: edge-on -> face up
  float bpm0 = 0.0f, bpm1 = 0.0f;   // the heartbeat's pace, start -> end of the slow turn
  float amp0 = 0.0f, amp1 = 0.0f;   // a beat's buzz, start -> peak
  float gain = 0.0f;          // the heartbeat's loudness at its peak
  bool dark = false;          // the room goes dark (a BLACK 1/1)
  int party = kPartyNone;
  const char* word = "";      // the party's giant 3D word ("" none)
};

constexpr float kBaseFlip = 0.5f;     // r139: a card's turn over (s)
constexpr float kEdge = 0.47f;        // the slow turn ends here (0 face down .. 1 face up: 85 degrees)
constexpr float kLift = 0.35f, kSnap = 0.28f;
constexpr float kDarkIn = 1.2f, kSlam = 0.15f, kFlashLen = 0.35f;
constexpr float kAfterParty = 2.8f, kEaseBack = 1.2f;  // the 1/1's own show (cameo, its dimming) after the party's peak

inline Plan plan(const Pull& p) {
  Plan pl;
  if (p.exq > 0) {  // r230 (Lance: the r219 show + the r220 reveal scaled up for EXQUISITE): GOLD LEAF+ / a hit BIG, EMERALD / SAPPHIRE HUGE,
    // a MASTERPIECE 1/1 / a rookie's GOLD RPA /10 the JACKPOT with the room going dark; one notch more than LANCINI TIZM (as a JAM card), a
    // GRAIL character another
    const xrexq::Show s = xrexq::show(p.exq, p.grail, p.rookie);
    pl.tier = s.build;
    if (pl.tier == kTierNone) return pl;  // (a GALLERY /399: a plain flip, its serial stamped at the reveal)
    pl.notches = 1 + (p.grail ? 1 : 0);
    static const float kSlowX[4] = {0.0f, 1.0f, 2.0f, 3.4f};
    static const float kBpm0x[4] = {0, 84, 72, 64}, kBpm1x[4] = {0, 104, 124, 140};
    static const float kAmp0x[4] = {0, 0.20f, 0.25f, 0.30f}, kAmp1x[4] = {0, 0.45f, 0.70f, 0.90f};
    static const float kGainx[4] = {0, 0.55f, 0.75f, 0.95f};
    const float k = 1.0f + 0.25f * static_cast<float>(pl.notches);
    pl.lift = kLift;
    pl.slow = kSlowX[pl.tier] * k;
    pl.snap = kSnap;
    pl.bpm0 = kBpm0x[pl.tier];
    pl.bpm1 = kBpm1x[pl.tier] + 6.0f * static_cast<float>(pl.notches);
    pl.amp0 = kAmp0x[pl.tier];
    pl.amp1 = std::fmin(1.0f, kAmp1x[pl.tier] + 0.1f * static_cast<float>(pl.notches));
    pl.gain = std::fmin(1.0f, kGainx[pl.tier] + 0.05f * static_cast<float>(pl.notches));
    pl.dark = s.dark;
    pl.party = s.party;
    pl.word = s.word;
    return pl;
  }
  if (p.godLast) pl.tier = kTierGod;
  else if (p.parallel == 3) pl.tier = kTierLongest;
  else if (p.parallel == 2 || p.hit != 0) pl.tier = kTierLong;
  else if (p.parallel == 1 || p.shiny) pl.tier = kTierShort;
  if (pl.tier == kTierNone) return pl;  // a BASE card: as it always was
  pl.notches = (p.grail ? 1 : 0) + (p.jam ? 1 : 0);
  static const float kSlow[5] = {0.0f, 1.0f, 2.0f, 3.4f, 3.8f};
  static const float kBpm0[5] = {0, 84, 72, 64, 60}, kBpm1[5] = {0, 104, 124, 140, 148};
  static const float kAmp0[5] = {0, 0.20f, 0.25f, 0.30f, 0.35f}, kAmp1[5] = {0, 0.45f, 0.70f, 0.90f, 1.00f};
  static const float kGain[5] = {0, 0.55f, 0.75f, 0.95f, 1.0f};
  const float k = 1.0f + 0.25f * static_cast<float>(pl.notches);
  pl.lift = kLift;
  pl.slow = kSlow[pl.tier] * k;
  pl.snap = kSnap;
  pl.bpm0 = kBpm0[pl.tier];
  pl.bpm1 = kBpm1[pl.tier] + 6.0f * static_cast<float>(pl.notches);
  pl.amp0 = kAmp0[pl.tier];
  pl.amp1 = std::fmin(1.0f, kAmp1[pl.tier] + 0.1f * static_cast<float>(pl.notches));
  pl.gain = std::fmin(1.0f, kGain[pl.tier] + 0.05f * static_cast<float>(pl.notches));
  pl.dark = p.parallel == 3;
  // the party: SILVER (a shiny) a pop, GOLD / case hits BIG, a 1/1 HUGE, a JAM 1/1 of a GRAIL the JACKPOT (a GOD PACK's comes with
  // its Olympus show: xr_cards god_frame); the card's own party for a god pack's last card
  if (p.parallel == 3) {
    pl.party = p.jam && p.grail ? kPartyJackpot : kPartyHuge;
    pl.word = p.grail ? "GRAIL!" : "1 OF 1";
  } else if (p.hit == 1) {
    pl.party = kPartyBig, pl.word = "CASE HIT!";
  } else if (p.hit == 2) {
    pl.party = kPartyBig, pl.word = "HOMETOWN!";
  } else if (p.hit == 3) {
    pl.party = kPartyBig, pl.word = "SIGNED!";
  } else if (p.parallel == 2) {
    pl.party = kPartyBig, pl.word = "GOLD!";
  } else {
    pl.party = kPartyPop, pl.word = "";
  }
  if (p.godLast) pl.party = kPartyNone, pl.word = "";  // (its Olympus show is its party; the JACKPOT goes off as that clears)
  return pl;
}
inline float total(const Plan& pl) { return pl.tier == kTierNone ? kBaseFlip : pl.lift + pl.slow + pl.snap; }

inline float smooth01(float t) {
  t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
  return t * t * (3.0f - 2.0f * t);
}
// The turn over (0 face down .. 1 face up) t s after it starts (xr_cards' flip: from pi to 0 by this). BASE: r139's smooth 0.5 s.
inline float progress(const Plan& pl, float t) {
  if (pl.tier == kTierNone) return smooth01(t / kBaseFlip);
  if (t <= pl.lift) return 0.0f;
  t -= pl.lift;
  if (t < pl.slow) {  // slowly to edge-on: it eases in (a creep), keeps going, eases right at the edge (the held breath)
    const float u = t / pl.slow;
    const float e = u < 0.5f ? 2.0f * u * u : 1.0f - 2.0f * (1.0f - u) * (1.0f - u);
    return kEdge * (0.85f * e + 0.15f * u);
  }
  t -= pl.slow;
  const float v = t >= pl.snap ? 1.0f : t / pl.snap;
  return kEdge + (1.0f - kEdge) * (1.0f - (1.0f - v) * (1.0f - v) * (1.0f - v));  // the snap: fast off the edge, landing soft
}
// When the face comes round (progress 0.5): the reveal's moment
inline float reveal_at(const Plan& pl) {
  if (pl.tier == kTierNone) return 0.5f * kBaseFlip;
  // kEdge + (1 - kEdge) (1 - (1 - v)^3) = 0.5
  const float v = 1.0f - std::cbrt(1.0f - (0.5f - kEdge) / (1.0f - kEdge));
  return pl.lift + pl.slow + v * pl.snap;
}
// The extra lift toward your eyes (0..1): up in its first moments, held through the slow turn, eased back after the snap
inline float lift(const Plan& pl, float t) {
  if (pl.tier == kTierNone) return 0.0f;
  if (t < pl.lift) return smooth01(t / pl.lift);
  const float end = pl.lift + pl.slow + pl.snap;
  if (t < end) return 1.0f;
  return 1.0f - smooth01((t - end) / 0.5f);
}
// The room's darkness (0 lit .. 1 dark) for a 1/1: in over kDarkIn from the trigger, held, slammed back at the reveal
inline float dark(const Plan& pl, float t) {
  if (!pl.dark) return 0.0f;
  const float r = reveal_at(pl);
  if (t < r) return smooth01(t / kDarkIn);
  const float s = (t - r) / kSlam;
  return s >= 1.0f ? 0.0f : (1.0f - s) * smooth01(r / kDarkIn);
}
// The reveal's white flash of the room (0..1), s after the reveal: one quick rise, gone in kFlashLen (never a second one)
inline float flash(const Plan& pl, float sinceReveal) {
  if (!pl.dark || sinceReveal < 0.0f || sinceReveal > kFlashLen) return 0.0f;
  return sinceReveal < 0.04f ? sinceReveal / 0.04f : std::exp(-(sinceReveal - 0.04f) / 0.09f);
}
// The heartbeat through the build-up: beat k's time (from the trigger), its strength (0..1 of the plan's buzz range). The beats
// start with the lift and go till the snap; the pace climbs bpm0 -> bpm1 with the turn. Each beat = a lub, then a dub 0.17 s later.
// Calls f(atSeconds, buzzAmp, gainOfPeak, dub) for every lub / dub in [t0, t1).
template <typename F>
void beats(const Plan& pl, float t0, float t1, F f) {
  if (pl.tier == kTierNone) return;
  const float end = pl.lift + pl.slow;
  float t = 0.0f;
  for (int k = 0; k < 64 && t < end; ++k) {
    const float u = end > 0.0f ? t / end : 1.0f;
    const float amp = pl.amp0 + (pl.amp1 - pl.amp0) * std::pow(u, 1.5f);
    const float g = pl.gain * (0.55f + 0.45f * u);
    if (t >= t0 && t < t1) f(t, amp, g, false);
    const float d = t + 0.17f;
    if (d >= t0 && d < t1 && d < end + 0.05f) f(d, 0.65f * amp, 0.7f * g, true);
    const float bpm = pl.bpm0 + (pl.bpm1 - pl.bpm0) * u;
    t += 60.0f / bpm;
  }
}
// The heartbeat's sound (made here, 32 kHz mono): a lub - a low thump (a sine sweeping 62 -> 38 Hz, its body + a soft click of the
// valve) - or the dub (higher, shorter, softer). Peak ~ -3 dBFS x gain. No file: nothing downloaded.
inline void heart_pcm(bool dub, std::vector<short>* out) {
  constexpr int kRate = 32000;
  const float len = dub ? 0.20f : 0.26f;
  out->assign(static_cast<size_t>(len * kRate), 0);
  const float f0 = dub ? 78.0f : 62.0f, f1 = dub ? 50.0f : 38.0f, dec = dub ? 0.055f : 0.075f;
  double ph = 0.0;
  for (size_t s = 0; s < out->size(); ++s) {
    const float t = static_cast<float>(s) / kRate;
    const float f = f1 + (f0 - f1) * std::exp(-t / 0.05f);
    ph += 6.2831853 * f / kRate;
    const float att = std::fmin(1.0f, t / 0.006f);
    const float body = std::sin(static_cast<float>(ph)) * att * std::exp(-t / dec);
    const float under = 0.35f * std::sin(static_cast<float>(ph) * 0.5f) * att * std::exp(-t / (1.6f * dec));
    const float click = (dub ? 0.10f : 0.14f) * std::sin(6.2831853f * 180.0f * t) * std::exp(-t / 0.008f);
    const float v = (dub ? 0.62f : 0.70f) * (body + under) + click;
    out->at(s) = static_cast<short>(std::fmax(-32000.0f, std::fmin(32000.0f, v * 32000.0f)));
  }
}

// ============================================================================================ r220: THE PUNCH (JAM '96 die-cuts)
// Lance 11-11: every JAM '96 card shows the plain crest face down (no give-away; the tells + build-up above unchanged); at its reveal
// (reveal_at: where the snap passes half way) its edge SNAPS to its real cut (kPunchMorph, the crest's 64 points lerped onto the cut's)
// with a die-press ka-chunk (made here) and the cut-away pieces fall off as small bits (kBitLife, shrinking away at the end).
constexpr float kPunchMorph = 0.10f;               // s: the edge from the crest to its cut
constexpr float kBitLife = 1.10f, kBitFade = 0.35f;  // s: a bit falls this long; shrinks away over its last kBitFade
constexpr float kBitGravity = 2.6f;                 // m/s2 (a light card chip: a little floaty)
inline float punch_morph(float since) { return since <= 0.0f ? 0.0f : (since >= kPunchMorph ? 1.0f : smooth01(since / kPunchMorph)); }
inline float bit_scale(float since) {
  if (since < 0.0f || since >= kBitLife) return 0.0f;
  const float f = (kBitLife - since) / kBitFade;
  return f >= 1.0f ? 1.0f : smooth01(f);
}
// a bit's own throw (deterministic from its number and the card's): out across the card from its middle, out of its face, up; a tumble
struct BitThrow {
  float out, face, up;      // m/s: along the card from its middle, out of its front, up in the room
  float ax, ay, az, spin;   // its tumble's axis (unit) and rate (rad/s)
};
inline float bit_rand(uint32_t k) {
  k ^= k >> 16, k *= 0x7FEB352Du, k ^= k >> 15, k *= 0x846CA68Bu, k ^= k >> 16;
  return static_cast<float>(k & 0xFFFFFF) / 16777215.0f;
}
inline BitThrow bit_throw(int bit, uint32_t seed) {
  const uint32_t s = seed * 2654435761u + static_cast<uint32_t>(bit) * 40503u;
  BitThrow b;
  b.out = 0.05f + 0.08f * bit_rand(s + 1);
  b.face = 0.05f + 0.08f * bit_rand(s + 2);
  b.up = 0.10f + 0.12f * bit_rand(s + 3);
  float x = bit_rand(s + 4) * 2.0f - 1.0f, y = bit_rand(s + 5) * 2.0f - 1.0f, z = bit_rand(s + 6) * 2.0f - 1.0f;
  const float l = std::sqrt(x * x + y * y + z * z);
  if (l < 1e-3f) x = 0.3f, y = 1.0f, z = 0.2f;
  const float L = std::sqrt(x * x + y * y + z * z);
  b.ax = x / L, b.ay = y / L, b.az = z / L;
  b.spin = (7.0f + 7.0f * bit_rand(s + 7)) * (bit_rand(s + 8) < 0.5f ? -1.0f : 1.0f);
  return b;
}
// The die-press (made here, 32 kHz mono, no file): a bright KA (the knife meeting the board) and, 35 ms on, the CHUNK (the press
// bottoming out: a low thud sweeping 95 -> 52 Hz + the board's dull body). r231 (Lance: no static / hiss in VR): made of damped sine
// partials only - r220's KA was 5 ms of high-passed white noise and its CHUNK carried two bursts of low-passed noise; now the KA = the
// steel's inharmonic ping + a short knock of the board, the CHUNK = the thud + the board's wooden modes. Peak ~ -3 dBFS.
inline void die_press_pcm(std::vector<short>* out) {
  constexpr int kRate = 32000;
  constexpr float kTau = 6.2831853f;
  out->assign(static_cast<size_t>(0.30f * kRate), 0);
  std::vector<float> v(out->size(), 0.0f);
  auto mode = [&](float t, float hz, float amp, float dec, float rise) {
    return t < 0.0f ? 0.0f : amp * std::fmin(1.0f, t / rise) * std::exp(-t / dec) * std::sin(kTau * hz * t);
  };
  double ph = 0.0;
  for (size_t s = 0; s < v.size(); ++s) {
    const float t = static_cast<float>(s) / kRate;
    // KA: the steel's ping (inharmonic partials) + the board's knock
    const float ka = mode(t, 2350.0f, 0.16f, 0.035f, 0.0005f) + mode(t, 3720.0f, 0.10f, 0.014f, 0.0005f) + mode(t, 5310.0f, 0.05f, 0.006f, 0.0005f) +
                     mode(t, 880.0f, 0.18f, 0.012f, 0.0008f) + mode(t, 1370.0f, 0.10f, 0.008f, 0.0008f);
    // CHUNK at 35 ms: the thud + the board's body
    float ch = 0.0f;
    const float u = t - 0.035f;
    if (u >= 0.0f) {
      const float f = 52.0f + 43.0f * std::exp(-u / 0.04f);
      ph += kTau * f / kRate;
      ch = 0.85f * std::sin(static_cast<float>(ph)) * std::fmin(1.0f, u / 0.003f) * std::exp(-u / 0.085f) + mode(u, 148.0f, 0.30f, 0.045f, 0.002f) +
           mode(u, 233.0f, 0.20f, 0.030f, 0.002f) + mode(u, 361.0f, 0.10f, 0.018f, 0.002f) + mode(u, 610.0f, 0.06f, 0.010f, 0.001f);
    }
    v[s] = ka + ch;
  }
  float peak = 1e-6f;
  for (float x : v) peak = std::fmax(peak, std::fabs(x));
  const float k = 0.70f / peak;
  const size_t fade = static_cast<size_t>(0.02f * kRate);
  for (size_t s = 0; s < v.size(); ++s) {
    const float f = s + fade > v.size() ? static_cast<float>(v.size() - s) / static_cast<float>(fade) : 1.0f;
    out->at(s) = static_cast<short>(std::fmax(-32000.0f, std::fmin(32000.0f, v[s] * k * f * 32000.0f)));
  }
}

}  // namespace xrpulls
