// strikers-quest r42: text panels for the headset, drawn on the CPU into RGBA8 images (sRGB,
// premultiplied alpha) that xr_quest.cpp shows as quad layers. Font: Luckiest Guy (Apache-2.0,
// extern/fonts), rasterized with stb_truetype.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xrtext {

// The goal banner: "GOAL SCORED BY:" over the scorer's name, on a rounded panel in the team colour
// (0xRRGGBB) with a gold border and stars. `rgba` holds w * h * 4 bytes. r43d: under the name, the
// scorer's goals and assists this game (goals < 0: no stats line), "HAT TRICK!" in gold with them
// when hatTrick. Laid out for w : h = 1024 : 560.
// r72: `header` in place of "GOAL SCORED BY:" (red: your nemesis scoring on you), null = the usual.
// r85: ownGoal: "OWN GOAL - OFF A DEFENDER" over the name (orange), the name being who it counts for.
void goal_banner(uint8_t* rgba, int w, int h, const char* name, uint32_t teamRgb, int goals, int assists,
                 bool hatTrick, const char* header = nullptr, bool ownGoal = false);

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

// r57: the RetroAchievements badge board over the trophy shelf. Each tile: a 64 x 64 RGBA badge
// picture (straight alpha; null = none, a star instead) and whether it is unlocked (else grey).
struct BadgeTile {
  const uint8_t* rgba64;
  bool unlocked;
};
void badge_board(uint8_t* rgba, int w, int h, const BadgeTile* tiles, int count, int columns, const char* title);

// r61: the home ground's board: a title band in the team's colours (0xRRGGBB), then lines of a small
// label over a value, a coloured tag at the right (null = none) and a note under it in its own colour
// (null = none). Laid out for w : h about 640 : 440 with four lines.
struct HomeLine {
  const char* label;
  const char* value;
  const char* tag;
  uint32_t tagRgb;
  const char* note;
  uint32_t noteRgb;
};
void home_board(uint8_t* rgba, int w, int h, const char* title, uint32_t teamRgb, uint32_t teamRgb2,
                const HomeLine* lines, int count);

// r61: a framed match ticket (the first win): `header` over the score line, the date under it, in the
// team's colours. w : h = 2 : 1.
void match_ticket(uint8_t* rgba, int w, int h, const char* header, const char* score, const char* date,
                  uint32_t teamRgb, uint32_t teamRgb2);

// r61: the cup suite's table: `title` (the cup and round), then a line per team (rank, name, wins,
// losses, points; `mine` in gold), then `footer` (the next opponent). w : h about 3 : 2.
struct CupLine {
  const char* name;
  int wins, losses, points;
  bool mine;
};
void cup_board(uint8_t* rgba, int w, int h, const char* title, const CupLine* lines, int count, const char* footer);

// r67: the guest picker over the game's own pick screens: `title` over a grid of `columns` tiles
// (each the guest's own portrait from its game, straight alpha, any size, fitted in; null = its
// initials) with the name under it, `cursor` framed in gold, and `hint` at the foot.
struct GuestTile {
  const uint8_t* rgba;
  int w, h;
  const char* name;
  const char* game;  // a small tag under the name ("MELEE", "CHARGED", "POKEMON")
  // r68e, the character select menu: the tile's place (row, column; -1 = in order by `columns`), its
  // frame colour when the cursor isn't on it (0xRRGGBB, 0 = the usual), and taken (the other side has
  // this Strikers captain: darkened).
  int row = -1, col = -1;
  uint32_t frame = 0;
  bool taken = false;
  // r70c: an evolved Pokemon not unlocked yet (evolve it in franchise mode): its silhouette, "???".
  bool locked = false;
};
void guest_picker(uint8_t* rgba, int w, int h, const char* title, const GuestTile* tiles, int count, int columns,
                  int cursor, const char* hint);

// r67: the small "Y: GUEST CHARACTERS" tip shown on a pick screen while the picker is closed.
void guest_tip(uint8_t* rgba, int w, int h, const char* text);

// r70: franchise mode's boards (the Strikers Super League). A board: a title band in the club's colours
// with its logo (straight-alpha RGBA, null = none) at the left, then cards side by side (each a title
// and lines; a line can carry a small logo, be highlighted in gold, or be dimmed), then a hint. Laid
// out for w : h about 2160 : 1300 (the character select menu's size), up to 4 cards.
struct FrLine {
  std::string text;
  std::string right;  // a right-aligned value ("5-2", "+4"), "" none
  const uint8_t* logo = nullptr;
  int logoW = 0, logoH = 0;
  bool gold = false, dim = false, cursor = false;
};
struct FrCard {
  std::string title;
  std::vector<FrLine> lines;
  float weight = 1.0f;  // its share of the width
};
void franchise_board(uint8_t* rgba, int w, int h, const std::string& title, const std::string& subtitle,
                     uint32_t rgb1, uint32_t rgb2, const uint8_t* logo, int logoW, int logoH,
                     const std::vector<FrCard>& cards, const std::string& hint);

// r73: an awards night winner standing in your room (franchise): its portrait (straight alpha, fitted,
// standing on the plate) over a gold plate with the award (`award`, "MVP") and its name. Transparent
// round it. w : h about 400 : 640.
void award_standee(uint8_t* rgba, int w, int h, const uint8_t* pic, int pw, int ph, const char* award, const char* name);

// r73: a guest's name for its series' cup suite plaque (the room is baked without one): `name` in big
// letters on a clear background, filled top to bottom (0xRRGGBB), with an outline and a dark rim (the
// series' own lettering: Pokemon's yellow on blue ...). w : h about 4 : 1.
void plaque_name(uint8_t* rgba, int w, int h, const char* name, uint32_t top = 0xFFDA30, uint32_t bottom = 0xF5B400,
                 uint32_t outline = 0x1F43AE);

// r73: the news wire in your room (franchise): a gold `tag` box at the left ("SSL NEWS"), one line of
// news beside it. w : h about 1600 : 72.
void news_ticker(uint8_t* rgba, int w, int h, const char* tag, const char* text);

// r77 (Lance: an XP meter "as they score, or assist or win ... seeing that we've earned an evolution
// during the game"): a franchise match's live XP: the captain's name, "LV n" (r87: the level as the match
// stands, newLevel), "+gain XP", a bar (what it had gold, what this match adds green), then `line` (gold
// when `gold`: an evolution earned) and `side` (the sidekicks) at the foot. About 1000 x 170.
// r87 (Lance: no "LV 9 > 11"; "if i level up, it like glows for a second going from 9 to 10"): `glow` =
// seconds into a level up (< 0 none): kLevelGlowS of gold light behind "LV", the old level (`glowFrom`)
// for its first moment, then the new one; `sideGlow` the same behind `side`.
constexpr float kLevelGlowS = 1.2f;
void xp_meter(uint8_t* rgba, int w, int h, const char* name, int level, int xp, int newLevel, int newXp, int gain,
              const char* line, bool gold, const char* side, float glow = -1.0f, int glowFrom = -1,
              float sideGlow = -1.0f);

// r70c: a Pokemon evolving (franchise mode), the classic way: `stage` 0 a split's choice (to / alt side
// by side, `choice` framed), 1 evolving (`t` seconds in: the old form, it turns white, the two shapes
// back and forth faster and faster, a flash at ~7 s), 2 evolved (the new form, sparkles if `shiny`), 3
// stopped (the old form). Title over it, a line under it, the hint at the foot. Portraits straight alpha.
// r95: stage 0 is the EVO page for one form too (`options` 1): the old form beside the new one's white
// silhouette, an arrow between.
struct EvoImage {
  const uint8_t* rgba = nullptr;
  int w = 0, h = 0;
};
void evolution_scene(uint8_t* rgba, int w, int h, const EvoImage& from, const EvoImage& to, const EvoImage& alt,
                     int stage, float t, int choice, const char* title, const char* line, const char* hint, bool shiny,
                     int options = 2);

}  // namespace xrtext
