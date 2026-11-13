// strikers-quest r219: included by xr_goalfx.cpp after the casino party's tables (its Beat / Def / B / PAL / BEATS)
// ---- r219 (Lance 11-11, C:\StrikersMR\BIG-PULLS.md 3): the CASINO PARTY ON BIG PULLS - r207's party pieces fired by a hit's reveal
// (xr_cards reveal_fx / a GOD PACK's Olympus show clearing), by the pull (xr_pulls.h): 1 POP (a SILVER, a shiny), 2 BIG (a GOLD /10, a
// case hit), 3 HUGE (a BLACK 1/1), 4 JACKPOT (a GOD PACK, a JAM '96 1/1 of a GRAIL). Anchors (pull_party): kAtGoal the card in your
// hand (the coins, the confetti, the sparkle out of it), kAtCab your floor 2.2 m out along where the card is (the siren rises out of
// it), the giant word 2.0 m out a little over your eyes (Show::stage), kAtAbove the air over the word (the fireworks); sizes in metres.
// Its room pulses end by 2.4 s (the 1/1's own dimming comes back after 2.8 s: never two whole-room layers at once). No sounds here
// (the cards play the card's own tier sound + the casino's coin clips). Never in the shop.
const Beat kPullPop[] = {
    B(0.00f, kT0, kCasPulse, kAtHead, 1, 0.00f, 0, 0.00f, 0.00f, 0.30f, 0xE8F0FF, 0, 0.30f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 0.14f, 0.00f, 0.22f, 0xFFFFFF, 0xC8D8FF, 1.00f, 0.80f),
    B(0.00f, kT0, kBurst, kAtGoal, 24, 0.00f, kShSpark, 0.012f, 1.10f, 0.70f, 0xFFFFFF, 0xC8D8FF, 1.50f, 0.30f),
    B(0.00f, kT0, kBurst, kAtGoal, 12, 0.00f, kShTwinkle, 0.028f, 0.60f, 0.70f, 0xFFFFFF, 0xE8F0FF, 1.20f, 0.10f),
    B(0.05f, kT0, kCasCoins, kAtGoal, 12, 0.00f, kMxCoin, 0.036f, 1.20f, 4.00f, 0xFFD21F, 0xC8901E, 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtGoal, 30, 0.00f, kMxPaper, 0.032f, 2.40f, 2.60f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
};
const Beat kPullBig[] = {
    B(0.00f, kT0, kCasPulse, kAtHead, 2, 0.60f, 0, 0.00f, 0.00f, 0.30f, 0xFFD040, 0, 0.55f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 0.22f, 0.00f, 0.25f, 0xFFFFFF, 0xFFD040, 1.00f, 1.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 0.30f, 0.00f, 0.18f, 0xFFFFFF, 0xFFE9A0, 1.00f, 0.40f),
    B(0.00f, kT0, kBurst, kAtGoal, 40, 0.00f, kShSpark, 0.014f, 1.40f, 0.80f, 0xFFF4C0, 0xFFC830, 1.50f, 0.30f),
    B(0.00f, kT0, kCasSiren, kAtCab, 1, 0.00f, 0, 0.16f, 1.40f, 4.30f, 0xFF2A1A, 0xD8DCE6, 0.00f, 0.00f),
    B(0.05f, kT0, kCasLetters, kAtCab, 2, 0.00f, 0, 0.20f, 0.00f, 4.00f, 0xFFC830, 0xFF2E88, 0.00f, 0.00f),
    B(0.08f, kT0, kCasCoins, kAtGoal, 50, 1.40f, kMxCoin, 0.040f, 1.70f, 5.50f, 0xFFD21F, 0xC8901E, 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtGoal, 90, 0.00f, kMxPaper, 0.036f, 3.00f, 3.00f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtGoal, 6, 0.00f, kMxStreamer, 0.18f, 2.60f, 3.00f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.40f, kT0, kCasFirework, kAtAbove, 4, 2.40f, kShStreak, 0.08f, 1.80f, 1.10f, PAL(kPalRainbow), 0xFFFFFF, 22.0f, 0.00f),
};
const Beat kPullHuge[] = {
    B(0.60f, kT0, kCasPulse, kAtHead, 2, 0.60f, 0, 0.00f, 0.00f, 0.30f, 0xFFD040, 0, 0.60f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 0.26f, 0.00f, 0.28f, 0xFFFFFF, 0xC8A0FF, 1.00f, 1.10f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 0.36f, 0.00f, 0.20f, 0xFFFFFF, 0xE8D8FF, 1.00f, 0.40f),
    B(0.00f, kT0, kBurst, kAtGoal, 50, 0.00f, kShSpark, 0.015f, 1.60f, 0.90f, 0xFFFFFF, 0xB08CFF, 1.50f, 0.30f),
    B(0.10f, kT0, kRings, kAtAbove, 1, 0.00f, kShRing, 1.60f, 0.00f, 0.40f, 0xFFF4D0, 0xFFC830, 1.00f, 0.00f),
    B(0.00f, kT0, kCasSiren, kAtCab, 1, 0.00f, 0, 0.17f, 1.60f, 5.30f, 0xFF2A1A, 0xD8DCE6, 0.00f, 0.00f),
    B(0.05f, kT0, kCasLetters, kAtCab, 3, 0.00f, 0, 0.24f, 0.00f, 5.00f, 0xFFC830, 0xFF2E88, 0.00f, 0.00f),
    B(0.08f, kT0, kCasCoins, kAtGoal, 90, 2.00f, kMxCoin, 0.040f, 1.90f, 6.00f, 0xFFD21F, 0xC8901E, 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtGoal, 150, 0.00f, kMxPaper, 0.036f, 3.40f, 3.40f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtGoal, 10, 0.00f, kMxStreamer, 0.20f, 3.00f, 3.40f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.30f, kT0, kCasFirework, kAtAbove, 8, 3.20f, kShStreak, 0.09f, 2.00f, 1.20f, PAL(kPalRainbow), 0xFFFFFF, 24.0f, 0.00f),
};
const Beat kPullJackpot[] = {
    B(0.00f, kT0, kCasPulse, kAtHead, 6, 2.10f, 0, 0.00f, 0.00f, 0.30f, 0xFFD040, 0, 0.90f, 0.00f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShSpark, 0.30f, 0.00f, 0.30f, 0xFFFFFF, 0xFFD040, 1.00f, 1.20f),
    B(0.00f, kT0, kFlash, kAtGoal, 1, 0.00f, kShTwinkle, 0.40f, 0.00f, 0.20f, 0xFFFFFF, 0xFFE9A0, 1.00f, 0.40f),
    B(0.00f, kT0, kBurst, kAtGoal, 60, 0.00f, kShSpark, 0.016f, 1.80f, 0.90f, 0xFFF4C0, 0xFFC830, 1.50f, 0.30f),
    B(0.10f, kT0, kRings, kAtAbove, 2, 0.30f, kShRing, 2.00f, 0.00f, 0.45f, 0xFFF4D0, 0xFFC830, 1.00f, 0.00f),
    B(0.10f, kT0, kRays, kAtAbove, 14, 0.00f, kShStreak, 1.10f, 0.00f, 0.30f, 0xFFFFFF, 0xFFC830, 0.035f, 0.00f),
    B(0.00f, kT0, kCasSiren, kAtCab, 1, 0.00f, 0, 0.18f, 1.80f, 7.20f, 0xFF2A1A, 0xD8DCE6, 0.00f, 0.00f),
    B(0.05f, kT0, kCasLetters, kAtCab, 4, 0.00f, 0, 0.28f, 0.00f, 6.80f, 0xFFC830, 0xFF2E88, 0.00f, 0.00f),
    B(0.08f, kT0, kCasCoins, kAtGoal, 140, 3.20f, kMxCoin, 0.042f, 2.10f, 6.50f, 0xFFD21F, 0xC8901E, 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtGoal, 200, 0.00f, kMxPaper, 0.036f, 3.80f, 3.60f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(0.05f, kT0, kCasConfetti, kAtGoal, 14, 0.00f, kMxStreamer, 0.22f, 3.20f, 3.60f, PAL(kPalConfetti), PAL(kPalConfetti), 0.00f, 0.00f),
    B(2.20f, kT0, kCasConfetti, kAtGoal, 100, 0.00f, kMxFoil, 0.036f, 3.20f, 3.20f, PAL(kPalGold), 0xC8901E, 0.00f, 0.00f),
    B(0.25f, kT0, kCasFirework, kAtAbove, 14, 5.00f, kShStreak, 0.10f, 2.30f, 1.30f, PAL(kPalRainbow), 0xFFFFFF, 24.0f, 0.00f),
    B(0.60f, kT0, kRain, kAtTop, 80, 2.40f, kMxFoil, 0.030f, 0.55f, 2.40f, PAL(kPalGold), 0xC8901E, 0.30f, 0.00f),
};
const Def kPullDefs[4] = {
    {{"pull1", "PULL POP", "", 0, 0, 0xE8F0FF, 0xFF2E88, 1.6f}, BEATS(kPullPop), 14.0f},
    {{"pull2", "BIG PULL", "", 0, 0, 0xFFD040, 0xFF2E88, 4.6f}, BEATS(kPullBig), 14.0f},
    {{"pull3", "HUGE PULL", "", 0, 0, 0xFFD040, 0xFF2E88, 5.6f}, BEATS(kPullHuge), 14.0f},
    {{"pull4", "JACKPOT PULL", "", 0, 0, 0xFFD040, 0xFF2E88, 7.6f}, BEATS(kPullJackpot), 14.0f},
};
