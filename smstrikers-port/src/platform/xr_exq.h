// strikers-quest r230 (Lance 2026-11-12, C:\StrikersMR\EXQUISITE.md): LANCINI EXQUISITE - the third, ultra high-end card set (set C, card
// ids key + "_exq"). One pure header (no OpenXR, no state) shared by xr_cards.cpp (the packs, the pictures, the opening), xr_market.cpp
// (its prices), xr_trade.cpp (the collectors), xr_pulls.h (the big-pull show) and the PC proofs (tools/outside/exq230), so the proofs
// measure exactly what ships.
//
//   EVERY CARD SERIAL NUMBERED. A pack = 3 cards: 2 numbered + 1 hit. A copy's tier and number ride in its `serial` (tier x 1000 + its
//   number: a SAPPHIRE 03/10 = 5003) - every save / market / trade path that keeps a GOLD /10's number keeps it, nothing new to carry.
//   Its `parallel` / `hit` are the nearest LANCINI TIZM ones (a class: the tells, sounds, auras, the dark room of a 1/1 read those) - the
//   look, the price and the words come from its tier.
//     numbered slot: GALLERY /399 70% - SILVER LEAF /299 20% - GOLD LEAF /99 7% - EMERALD /25 2.2% - SAPPHIRE /10 0.7% - MASTERPIECE 1/1 0.1%
//     hit slot:      ON-CARD AUTO /49 60% - GRAND THREADS PATCH /25 28% - ROOKIE PATCH AUTO /99 10% (ORIGINALS rookies) -
//                    PATCH AUTO /10 1.8% (a rookie's: the GOLD RPA /10) - MASTERPIECE 1/1 0.2%
//   MASTERPIECE 1/1 = ONE per character per career (the rule of every set's 1/1): the patch-auto 1/1, whichever slot finds it first.
//   Every run is per character per career: a number taken (yours, a pack waiting, a CPU collector's) is gone; a sold-out run rolls to the
//   next tier down (1/1 -> /10 -> /25 -> /99 -> /299 -> /399; hit 1/1 -> PATCH AUTO /10 -> PATCH /25 -> AUTO /49; RPA /99 -> AUTO /49).
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace xrexq {

enum Tier {
  kNone = 0,
  kGallery = 1,      // /399
  kSilverLeaf = 2,   // /299
  kGoldLeaf = 3,     // /99
  kEmerald = 4,      // /25
  kSapphire = 5,     // /10
  kMasterpiece = 6,  // 1/1 (patch + on-card auto)
  kAuto = 7,         // ON-CARD AUTOGRAPH /49
  kPatch = 8,        // GRAND THREADS PATCH /25
  kRpa = 9,          // ROOKIE PATCH AUTO /99 (ORIGINALS rookies)
  kPatchAuto = 10,   // PATCH AUTO /10 (a rookie's: GOLD RPA /10)
  kTiers = 11
};
constexpr int kRun[kTiers] = {0, 399, 299, 99, 25, 10, 1, 49, 25, 99, 10};
constexpr int kPackCards = 3;                    // 2 numbered + 1 hit
constexpr int kPackPrice = 1000, kBoxPrice = 5000, kBoxPacks = 5;  // BIT COINS (Lance 11-12); shop only
constexpr const char* kSuffix = "_exq";
constexpr const char* kSetName = "2026-27 LANCINI EXQUISITE";
// the LANCINI TIZM finish / case hit each tier reads as (xr_cards' kBase .. kBlack, kHitNone / kHitSigned): the tells, the tier sounds,
// the auras, a 1/1's dark room
constexpr int kClass[kTiers] = {0, 0, 1, 2, 2, 2, 3, 0, 2, 1, 2};
constexpr bool kSigned[kTiers] = {false, false, false, false, false, false, true, true, false, true, true};
constexpr bool kPatchOn[kTiers] = {false, false, false, false, false, false, true, false, true, true, true};
inline bool numbered_slot(int t) { return t >= kGallery && t <= kMasterpiece; }
inline bool hit_tier(int t) { return t >= kMasterpiece && t <= kPatchAuto; }

// ---- a copy's serial: tier x 1000 + its number
inline int tier_of(int serial) { const int t = serial / 1000; return t > 0 && t < kTiers ? t : 0; }
inline int num_of(int serial) { return serial % 1000; }
inline int serial_of(int tier, int num) { return tier * 1000 + num; }
inline bool valid(int serial) {
  const int t = tier_of(serial), n = num_of(serial);
  return t > 0 && n >= 1 && n <= kRun[t];
}

// ---- the words
inline const char* name(int t, bool rookie = false) {
  static const char* const k[kTiers] = {"", "GALLERY", "SILVER LEAF", "GOLD LEAF", "EMERALD", "SAPPHIRE", "MASTERPIECE",
                                        "ON-CARD AUTOGRAPH", "GRAND THREADS PATCH", "ROOKIE PATCH AUTO", "PATCH AUTO"};
  if (t == kPatchAuto && rookie) return "GOLD ROOKIE PATCH AUTO";
  return t > 0 && t < kTiers ? k[t] : "";
}
inline const char* short_name(int t, bool rookie = false) {
  static const char* const k[kTiers] = {"", "GALLERY", "SILVER LEAF", "GOLD LEAF", "EMERALD", "SAPPHIRE", "MASTERPIECE", "AUTO", "PATCH", "RPA",
                                        "PATCH AUTO"};
  if (t == kPatchAuto && rookie) return "GOLD RPA";
  return t > 0 && t < kTiers ? k[t] : "";
}
// "03/10", "087/399", "1/1"
inline std::string num_text(int serial) {
  const int t = tier_of(serial), run = kRun[t], n = num_of(serial);
  if (run <= 1) return "1/1";
  char b[24];
  std::snprintf(b, sizeof(b), run >= 100 ? "%03d/%d" : "%02d/%d", n, run);
  return b;
}
// "SAPPHIRE 03/10", "MASTERPIECE 1/1 PATCH AUTO", "ROOKIE PATCH AUTO 42/99"
inline std::string words(int serial, bool rookie = false) {
  const int t = tier_of(serial);
  if (t == kNone) return "";
  std::string s = std::string(name(t, rookie)) + " " + num_text(serial);
  if (t == kMasterpiece) s += " PATCH AUTO";
  return s;
}

// ---- the pull rates (per slot, in 1 / 10,000) - exactly these (the pack back's ODDS page and the 1,000,000-pack proof read them)
constexpr int kNumOdds[kTiers] = {0, 7000, 2000, 700, 220, 70, 10, 0, 0, 0, 0};  // numbered slot (sums to 10,000)
constexpr int kHitOdds[kTiers] = {0, 0, 0, 0, 0, 0, 20, 6000, 2800, 1000, 180};  // hit slot (sums to 10,000)
// u in [0, 1): the slot's tier
inline int roll_num(double u) {
  int acc = 0;
  for (int t = kGallery; t <= kMasterpiece; ++t) {
    acc += kNumOdds[t];
    if (u * 10000.0 < acc) return t;
  }
  return kGallery;
}
inline int roll_hit(double u) {
  static const int kOrder[5] = {kAuto, kPatch, kRpa, kPatchAuto, kMasterpiece};
  int acc = 0;
  for (int t : kOrder) {
    acc += kHitOdds[t];
    if (u * 10000.0 < acc) return t;
  }
  return kAuto;
}
// a sold-out run: the next tier down (kNone: none - another character)
inline int down(int t) {
  switch (t) {
    case kMasterpiece: return -1;  // (by slot: the caller's numbered / hit chain)
    case kSapphire: return kEmerald;
    case kEmerald: return kGoldLeaf;
    case kGoldLeaf: return kSilverLeaf;
    case kSilverLeaf: return kGallery;
    case kPatchAuto: return kPatch;
    case kPatch: return kAuto;
    case kRpa: return kAuto;
    default: return kNone;
  }
}
inline int down_in_slot(int t, bool hitSlot) {
  if (t == kMasterpiece) return hitSlot ? kPatchAuto : kSapphire;
  return down(t);
}
// the reveal order in a pack (the biggest last)
inline int rank(int t) {
  static const int k[kTiers] = {0, 10, 20, 40, 60, 70, 100, 30, 35, 50, 80};
  return t > 0 && t < kTiers ? k[t] : 0;
}
// the pack back's ODDS page, one row a tier: "SAPPHIRE /10", "1 IN 71 PACKS"
struct OddsRow {
  const char* what;
  const char* odds;
};
constexpr OddsRow kOddsRows[] = {
    {"GALLERY /399", "1.4 A PACK"},          {"SILVER LEAF /299", "1 IN 2.5 PACKS"},   {"GOLD LEAF /99", "1 IN 7 PACKS"},
    {"EMERALD /25", "1 IN 23 PACKS"},       {"SAPPHIRE /10", "1 IN 71 PACKS"},        {"ON-CARD AUTO /49", "1 IN 1.7 PACKS"},
    {"GRAND THREADS PATCH /25", "1 IN 3.6 PACKS"}, {"ROOKIE PATCH AUTO /99", "1 IN 10 PACKS"}, {"PATCH AUTO /10", "1 IN 56 PACKS"},
    {"MASTERPIECE 1/1", "1 IN 250 PACKS"},
};
constexpr int kOddsRowsN = static_cast<int>(sizeof(kOddsRows) / sizeof(kOddsRows[0]));

// ---- the market (xr_market.cpp: price = its base (rarity, rating, DEMAND by the tier's scarcity s) x kMult[tier] x the premiums x the market's
// mood / heat / noise as every card). MARKET-PRICES.md section 11 / EXQUISITE.md: tuned so the MEDIAN pack sold at once stays under its 1,000
// (two GALLERYs + an auto, r211's rule) and each tier sits above its JAM '96 equal (by how hard each is to pull: GALLERY > Jam BASE, GOLD LEAF /99 > Jam SILVER, SAPPHIRE /10 >
// Jam GOLD /10, MASTERPIECE > Jam BLACK 1/1 auto) - tools/outside/exq230 market proof
constexpr double kS[kTiers] = {0.0, 0.35, 0.42, 0.55, 0.70, 0.80, 1.00, 0.62, 0.60, 0.66, 0.85};
constexpr double kMult[kTiers] = {1.0, 2.2, 6.0, 120.0, 600.0, 3500.0, 120000.0, 4.0, 5.0, 250.0, 6000.0};
constexpr double kRookie = 1.5;   // an ORIGINALS rookie (RC)
constexpr double kGrail = 1.25;   // a GRAIL character (demand tier 5)
constexpr double kFirst = 1.30;   // number 1 of its run
constexpr double kLast = 1.15;    // the last of its run (/399's 399)
constexpr double kJersey = 1.50;  // its "jersey number": the number = the character's own No. on the checklist
// the premiums of this copy's number (no number: 1)
inline double serial_premium(int serial, int checklistNo) {
  const int t = tier_of(serial), n = num_of(serial), run = kRun[t];
  if (t == kNone || run <= 1) return 1.0;
  double m = 1.0;
  if (n == 1) m *= kFirst;
  else if (n == run) m *= kLast;
  if (checklistNo > 0 && n == checklistNo) m *= kJersey;
  return m;
}
inline bool jersey(int serial, int checklistNo) {
  const int t = tier_of(serial);
  return t != kNone && kRun[t] > 1 && checklistNo > 0 && num_of(serial) == checklistNo;
}

// ---- the big pull (xr_pulls.h): its build-up tier (0 BASE's flip .. 3 the longest), its party (1 POP .. 4 JACKPOT), its word, the dark room
struct Show {
  int build = 0, party = 0;
  bool dark = false;
  const char* word = "";
};
// `grail` its character a GRAIL; `rookie` an ORIGINALS rookie
inline Show show(int t, bool grail, bool rookie) {
  Show s;
  switch (t) {
    case kGallery: break;  // (a plain flip - the serial's press at the reveal)
    case kSilverLeaf: s.build = 1, s.party = 1; break;
    case kGoldLeaf: s.build = 2, s.party = 2, s.word = "GOLD LEAF!"; break;
    case kAuto: s.build = 2, s.party = 2, s.word = "AUTO!"; break;
    case kPatch: s.build = 2, s.party = 2, s.word = "PATCH!"; break;
    case kRpa: s.build = 2, s.party = 2, s.word = "RPA!"; break;
    case kEmerald: s.build = 3, s.party = 3, s.word = "EMERALD!"; break;
    case kSapphire: s.build = 3, s.party = 3, s.word = "SAPPHIRE!"; break;
    case kPatchAuto:  // (a rookie's GOLD RPA /10 = the grail RPA: the JACKPOT, the lights out)
      s.build = 3, s.party = rookie ? 4 : 3, s.dark = rookie, s.word = rookie ? "GRAIL!" : "PATCH AUTO!";
      break;
    case kMasterpiece: s.build = 3, s.party = 4, s.dark = true, s.word = grail ? "GRAIL!" : "1 OF 1"; break;
    default: break;
  }
  return s;
}

}  // namespace xrexq
