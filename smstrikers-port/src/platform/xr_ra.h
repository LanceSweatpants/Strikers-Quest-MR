// strikers-quest r46: RetroAchievements in the game (Lance: "THEIR strikers achievement set"), with
// rcheevos (MIT), RA's own client library: the set on retroachievements.org for Super Mario Strikers,
// checked against the game's memory every game frame, unlocks sent to RA, and a pop-up in the headset.
// Casual (softcore) only; hardcore is for clients RA has approved. The launcher panel logs in
// (RetroAch.java); this side starts only when it finds that login, and the game is a disc image.
// Built with xr_quest.cpp in strikers_xr; Quest only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xrra {

constexpr uint32_t kToastW = 1024, kToastH = 300;  // the pop-up's picture (xrtext::achievement_toast)

// Once per headset loop, on the game thread, after the game frame if one ran: starts the first time
// round (when the panel has a login), hands rcheevos the server's answers and, after a game frame,
// has it check the achievements against that frame's memory (ra_memory.h).
void frame(bool ranGameFrame);

// The next pop-up's picture (kToastW x kToastH, RGBA8 sRGB, premultiplied), each one once. False
// while none is ready; asking also starts drawing the next one waiting.
bool take_toast(std::vector<uint8_t>& rgba);

void shutdown();

// r51 (a player: "a button combination to open the achievements menu in-game"): the set as this run
// knows it, for the pause screen's list (xr_quest.cpp). Unlocked ones first, the server's notice to
// unapproved clients left out. When the list can't be shown, `rows` stays empty and `why` says why
// in the panel's words.
struct AchievementRow {
  std::string title, description, progress;  // progress: "12/25" for a locked one that counts
  std::string badge;                         // r57: its badge's name (RA's picture "<badge>.png")
  int points = 0;
  bool unlocked = false;
};
void achievement_rows(std::vector<AchievementRow>* rows, uint32_t* unlocked, uint32_t* total, std::string* why);

}  // namespace xrra
