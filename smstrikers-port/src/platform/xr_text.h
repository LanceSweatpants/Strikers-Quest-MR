// strikers-quest r42: text panels for the headset, drawn on the CPU into RGBA8 images (sRGB,
// premultiplied alpha) that xr_quest.cpp shows as quad layers. Font: Luckiest Guy (Apache-2.0,
// extern/fonts), rasterized with stb_truetype.
#pragma once

#include <cstdint>

namespace xrtext {

// The goal banner: "GOAL SCORED BY:" over the scorer's name, on a rounded panel in the team colour
// (0xRRGGBB) with a gold border and stars. `rgba` holds w * h * 4 bytes. r43d: under the name, the
// scorer's goals and assists this game (goals < 0: no stats line), "HAT TRICK!" in gold with them
// when hatTrick. Laid out for w : h = 1024 : 560.
void goal_banner(uint8_t* rgba, int w, int h, const char* name, uint32_t teamRgb, int goals, int assists,
                 bool hatTrick);

// The crowd volume panel shown while the game is paused: a title, `steps` bar segments with `level`
// of them lit, and a hint line. r44: and the voice chat switch, `voice` 0 off, 1 on, 2 on but the
// microphone not allowed. r44g: the pause settings panel: those two and your eye height in the
// stadium (heightCm, + higher), with `row` (0 crowd, 1 voice, 2 height) picked; h about 0.6 w.
void settings_panel(uint8_t* rgba, int w, int h, int level, int steps, int voice, int heightCm, int row);

// r46: a RetroAchievements pop-up: `header` ("ACHIEVEMENT UNLOCKED") with the points (none when
// points <= 0), the title, and the description (wrapped onto two lines if it must), beside a gold
// star. Laid out for w : h = 1024 : 300.
void achievement_toast(uint8_t* rgba, int w, int h, const char* header, const char* title, const char* description,
                       int points);

// r51: the pause screen's achievements list. r51b: the whole list as one tall picture the headset
// scrolls through, `count` rows of `rowH` (title, description wrapped onto two lines if it must,
// points, and for a locked one its progress; unlocked ones in gold with a star, locked ones grey),
// h at least count * rowH + 24. With count 0, `message` in the middle instead.
struct AchievementLine {
  const char* title;
  const char* description;
  const char* progress;  // "" for none
  int points;
  bool unlocked;
};
void achievements_list(uint8_t* rgba, int w, int h, const AchievementLine* rows, int count, int rowH,
                       const char* message);

// r51b: the list's title bar, `title` over `hint`; w : h about 1000 : 150.
void achievements_header(uint8_t* rgba, int w, int h, const char* title, const char* hint);

}  // namespace xrtext
