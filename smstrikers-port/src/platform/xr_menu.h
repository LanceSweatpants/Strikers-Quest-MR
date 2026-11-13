// strikers-quest r157 (menu overhaul; Lance: "i want those menus to be easy to navigate, right now its just easy to get
// lost basically"): one look for the franchise hub and the pause panel, as pieces for xrtext::board. The same numbers
// as tools/outside/menus_kit.py (the PC mockups / checks), so the headset matches the preview: the frame, the section
// tabs (LT / RT), the breadcrumb, a section's rail of pages, big tiles, value rows, the ONE hint bar (always MOVE, A,
// B, then the sections, then the page's extras; a hint that does nothing stays in its place, faded), the ONE gold
// focus ring, the confirm pop-up (A YES / B NO) that replaced the press-twice timers. Any thread (pure drawing).
#pragma once

#include "xr_text.h"

#include <cstdint>
#include <string>
#include <vector>

namespace xrmenu {

// the design tokens (menus_kit.py)
constexpr uint32_t kGold = 0xFFD23F, kCream = 0xFFF7B0, kNavyTop = 0x1E2A4E, kNavyBot = 0x070A14;
constexpr uint32_t kTile = 0x151C36, kTileHi = 0x22305C, kTileOn = 0x3B5AA8, kPill = 0x26335E, kRowOn = 0x2B4690;
constexpr uint32_t kInkSoft = 0xD2DAEB, kInkDim = 0x8A94AA, kWhite = 0xFFFFFF, kDark = 0x0B1020;
constexpr uint32_t kGreen = 0x2EA043, kRed = 0xC0392B, kBlue = 0x2F6FD0, kYellow = 0xC9A227, kSlate = 0x3A4A70;
constexpr uint32_t kBadge = 0xFF5A4A;
// a colour per section, so where you are reads at a glance (the tab, the breadcrumb, the rail)
constexpr uint32_t kSecHome = 0x2EA043, kSecLeague = 0x2F6FD0, kSecClub = 0xD9542B, kSecCards = 0x8E3FD1, kSecNews = 0xC9A227;
constexpr uint32_t kSecResume = 0x2EA043, kSecView = 0x2F6FD0, kSecSettings = 0x16938C, kSecAch = 0xC9A227,
                   kSecMatch = 0x8E3FD1, kSecQuit = 0xC0392B;

uint32_t mix(uint32_t a, uint32_t b, float t);
uint32_t glyph_rgb(const std::string& g);  // A green, B red, X blue, Y yellow, the rest slate

// A builder over a piece list (absolute pixels, top-left origin).
struct Kit {
  std::vector<xrtext::BoardPiece>& v;
  explicit Kit(std::vector<xrtext::BoardPiece>& out) : v(out) {}
  void box(float x0, float y0, float x1, float y1, float r, uint32_t rgb, float a = 1.0f, uint32_t rgb2 = 0);
  void ring(float x0, float y0, float x1, float y1, float r, float width, uint32_t rgb, float a = 1.0f);
  void glow(float x0, float y0, float x1, float y1, uint32_t rgb, float a);
  void poly(std::initializer_list<float> xy, uint32_t rgb, float a = 1.0f);
  void poly(const std::vector<float>& xy, uint32_t rgb, float a = 1.0f);
  void text(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline = 0.0f);
  void left(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline = 0.0f);
  void right(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline = 0.0f);
  float width(const std::string& t, float px) const;  // (capitals)
};

// ---- r236: LOOK B "BROADCAST PREMIUM" (Lance 11-12, logs/r235-menus/optB_*.png) - the franchise hub only. While a
// BroadcastScope lives on this thread the pieces below draw the sports-TV package (italic condensed Roboto caps, glass panels with
// cut corners, parallelogram buttons and chips, a numbered rail, the header strip, your club's colour as the accent, the gold
// focus), board_text_width measures in that face, and broadcast_pieces() turns a page's own pieces (drawn with the old tokens) into
// the same look. Without one (the pause panel, every other board) nothing changes.
constexpr uint32_t kBGold = 0xF8C030, kBInk = 0xE6ECF5, kBSoft = 0xB8C4DC, kBDim = 0x7A86A0, kBGlass = 0x101827, kBNight = 0x0A0F1C,
                   kBRow = 0x111A2E, kBBg0 = 0x05070D, kBBg1 = 0x0B1326;
// the sections' colours (HOME is the accent: your club's)
constexpr uint32_t kBLeague = 0x2F8CFF, kBClub = 0xFF8A1E, kBCards = 0xA855F7, kBLocker = 0x14B8A6, kBNews = 0xF8C030;
bool broadcast();
uint32_t accent();
struct BroadcastScope {
  bool wasOn;
  uint32_t wasAccent;
  xrtext::FaceScope face;
  explicit BroadcastScope(uint32_t accentRgb);
  ~BroadcastScope();
};
// The other way round: a picture drawn on a thread inside the hub's scope that keeps its own look (the sportsbook MACHINE's screen)
struct PlainScope {
  bool wasOn;
  xrtext::FaceScope face;
  explicit PlainScope(bool active = true);  // (active false: nothing changes)
  ~PlainScope();
};
// A club's two colours -> an accent that reads under white words (saturated, not too dark, not too light); 0xE8402A if neither does
uint32_t club_accent(uint32_t c1, uint32_t c2);
// The page's own pieces from `from` on (boxes, rings, words drawn with the old tokens) in the broadcast look: words in the face
// (no outlines; a soft shadow on the big ones), panels / tiles with cut corners, pills as parallelograms, the old navy / gold
// tokens as the package's. Pieces marked styled, pictures, glows and small marks stay as they are.
void broadcast_pieces(std::vector<xrtext::BoardPiece>& v, size_t from);
// Words in the face: `italic` (kFaceBroadcast) or upright (kFaceBroadcastUp), `track` a share of px between letters; anchor 'l' 'c'
// 'r'; its width back
float say(Kit& K, const std::string& t, float x, float y, float px, uint32_t rgb, char anchor = 'l', float room = 0.0f,
          bool italic = true, float track = 0.0f, float shadow = 0.0f);
// A small tracked upright label (the mock's "NEXT MATCH - WEEK 7", "FOR YOU")
float label(Kit& K, const std::string& t, float x, float y, float px, uint32_t rgb = kBSoft, char anchor = 'l', float room = 0.0f);
// Glass: a panel with its top-left / bottom-right corners cut, a light line along its top; `tint` a soft colour wash (0 none)
void panel(Kit& K, float x0, float y0, float x1, float y1, uint32_t tint = 0, float alpha = 0.9f, float cut = 30.0f);
// A parallelogram button: primary = filled in the accent with a gold foot; `on` the gold focus; a glyph ("A") before its label
void button(Kit& K, float x0, float y0, float x1, float y1, const std::string& label, const std::string& glyphName, bool primary,
            bool on, bool can = true, float px = 0.0f);
// A parallelogram chip (a tag: NEMESIS, SNOW, a filter); its width back
float chip(Kit& K, const std::string& t, float x, float cy, uint32_t rgb, char anchor = 'c', float px = 34.0f, bool solid = true);
// A gold parallelogram count / word (a badge), its right edge at x1
void badge(Kit& K, const std::string& t, float x1, float y0, float px = 30.0f);
// Sub-tabs (MARKET's BUY / SELL / LISTED / OFFERS ...): parallelograms, `at` filled in `rgb`; `focused` the gold ring on it; badges
// gold; the x after the last back
float subtabs(Kit& K, const std::vector<std::string>& names, int at, float x0, float y0, const std::vector<std::string>& badges,
              uint32_t rgb, bool focused = false, float px = 40.0f, float h = 60.0f);
// The header strip under the tabs: the section (/ page) in its colour with an arrow, then `parts` chevron by chevron (the first
// white), coins right; logoX: where the caller puts the club's logo (0: none) - the x the parts start at comes back
struct StripPart {
  std::string text;
  uint32_t rgb = kBSoft;
};
float header_strip(Kit& K, float w, const std::string& where, uint32_t secRgb, bool logo, const std::vector<StripPart>& parts,
                   const std::string& coins, float* logoX0 = nullptr);
// A coin (the B mark) centred on cx, cy, s across
void bcoin(Kit& K, float cx, float cy, float s);

// The panel's frame: a gold edge, the navy inside. (r236: in a BroadcastScope the package's background instead)
void frame(Kit& K, float w, float h, uint32_t accent = kGold, float radius = 30.0f);
// THE focus: a thick gold ring with a soft glow, never anything else.
void focus(Kit& K, float x0, float y0, float x1, float y1, float r);
// A controller button (a disc; a pill for "LT", "MENU"; "L" / "R" a stick: a ring inside), left edge at x; its width.
float glyph(Kit& K, const std::string& g, float x, float cy, float size = 54.0f);
// A plain icon in a box s wide: "play" "gear" "trophy" "chart" "door" "eye" "home" "table" "shield" "cards" "news" "swap"
// "evo" "star".
void icon(Kit& K, const std::string& kind, float cx, float cy, float s, uint32_t rgb = kWhite, float a = 1.0f);

// WHERE YOU ARE: "FRANCHISE > LEAGUE > TABLE", the last part a pill in its section's colour. Returns the end x.
struct Crumb {
  std::string text;
  uint32_t rgb = 0;
};
float breadcrumb(Kit& K, float x, float cy, const std::vector<Crumb>& parts, float px = 44.0f);

// ONE hint bar: the same order everywhere (MOVE, A, B, LT RT, the page's extras). `glyph` may be two ("LT RT", "B MENU");
// dim: kept in its place, faded (nothing for it to do here).
struct Hint {
  std::string glyph, words;
  bool dim = false;
};
void hint_bar(Kit& K, float x0, float x1, float cy, const std::vector<Hint>& hints, float size = 54.0f, float px = 40.0f);

// The sections as big pills (LT / RT step through them; the LT / RT glyphs at the ends), `at` lit in its colour; a red
// count on a section that wants you ("" none).
void section_tabs(Kit& K, float x0, float x1, float y0, float y1, const std::vector<std::string>& names,
                  const std::vector<uint32_t>& colours, const std::vector<std::string>& icons, int at,
                  const std::vector<std::string>& badges, float px = 48.0f);

// A section's pages top to bottom: `at` the page showing (filled in the section's colour), `focused`: the stick is on
// the rail (the gold ring on `at`). Returns the y under the last.
struct RailItem {
  std::string name, badge;
  bool dim = false;
};
float rail(Kit& K, float x0, float x1, float y0, const std::vector<RailItem>& items, int at, bool focused, uint32_t colour,
           float row = 104.0f, float gap = 16.0f, float px = 48.0f);

// A big tile: a colour band along its top, an icon, its title and a line under it. layout 0 centred, 1 a row (icon
// left, words beside it).
struct TileSpec {
  std::string title, sub, icon, badge;
  uint32_t colour = kTileOn;
  bool on = false, dim = false;
  int layout = 0;
  float titlePx = 64.0f, subPx = 40.0f, iconS = 0.0f;
};
void tile(Kit& K, float x0, float y0, float x1, float y1, const TileSpec& t, float r = 34.0f);
// A colour strip along the top of a rounded box (its corners kept round: a box of the same radius, clipped by a cover).
void band(Kit& K, float x0, float y0, float x1, float y1, float r, float h, uint32_t colour, float a = 1.0f);

// A settings row: the label left, the value in a pill right (< VALUE > while focused), or a bar of `segs` blocks with
// `lit` of them gold (segs > 0). dim: greyed (it can't apply now), `why` printed in the pill's place.
void value_row(Kit& K, float x0, float y0, float x1, float y1, const std::string& label, const std::string& value, bool on,
               int lit = -1, int segs = 0, bool dim = false, const std::string& why = "", float px = 42.0f);

// The one confirm, over everything: `question` big, `line` under it, A YES / B NO (B's words: `no`).
void confirm(Kit& K, float w, float h, const std::string& question, const std::string& line, const std::string& yes = "YES",
             const std::string& no = "NO");

// A scrolling grid's note: "ROW 2 OF 5" between up / down arrows (centred on cx).
void scroll_note(Kit& K, float cx, float cy, int first, int shown, int total, const std::string& what = "ROW");

}  // namespace xrmenu
