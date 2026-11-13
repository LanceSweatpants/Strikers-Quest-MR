// strikers-quest r205 (Lance's r204 test, 2026-11-09: "any time I do move, it gets CHOPPY and it induces that SICKNESS feeling";
// his log logs\r204-choppy: 72 -> 50.1 -> 24.5 fps while he used the TIZM machine, "fx" (the machine's frame work) 0.46 -> 2.47
// -> 8.83 ms a frame): THE MACHINE'S SCREEN, OFF THE FRAME THREAD.
//
// r199-r204 drew the screen's WHOLE picture (1320 x 766, ~85 ms in WebAssembly) on a new thread per request - on every move and
// then 20 a second while the cursor popped (several drawings at once fighting the game's threads for the CPU) - and uploaded it
// on the frame thread: a 4 MB swizzle / copy, then a WAIT on the GPU for the last upload (behind all of the frame's 3D). Now:
//   - the picture doesn't change when the cursor moves: the cursor is an OVERLAY (a small picture of its own laid over the
//     screen as its own quad, posed from the item's box) - a move redraws nothing; the pop / the press flash are the overlay's
//     size + which of its pre-drawn cells is shown, never a redraw;
//   - when the content does change (a bet, a page, the ticker, the reels) only the ROWS that changed are drawn again (the
//     pieces compared with the last picture's: the ones gone or new mark their rows) - board_band draws exactly what board()
//     would there - and only those rows are copied up (per swapchain image: what it is missing since the picture it holds);
//   - all of it on two worker threads (xr_quest.cpp); the frame thread only records a buffer -> image copy of ready rows.
// Pure C++ (xr_text + xr_book), so the PC proof (tools/outside/machine205) runs this same file.
#pragma once

#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "xr_book.h"
#include "xr_text.h"

namespace xrscreen {

struct Band {  // rows y0 .. y1 (one past the last)
  int y0 = 0, y1 = 0;
};
// Sorted, rows within `gap` of each other joined, at most `maxBands` (the closest joined first), clamped to 0 .. h
void merge_bands(std::vector<Band>& b, int h, int gap = 12, int maxBands = 6);
int band_rows(const std::vector<Band>& b);

uint64_t piece_hash(const xrtext::BoardPiece& p);
uint64_t pieces_hash(const std::vector<xrtext::BoardPiece>& v);

// ---- the picture: the new pieces -> the rows that changed, drawn again in place ----
struct Rect {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
struct Change {
  bool any = false, full = false;
  std::vector<Band> bands;  // the rows to draw again (full: all of them)
  std::vector<Rect> rects;  // the changed pieces' boxes (conservative) - does a change touch the cursor's item?
  void add(const Change& o);  // (a picture skipped: its change folded into the next)
};
// The pieces' box (conservative: a text by its room / width + its outline's pad, a turned one its whole row)
Rect piece_box(const xrtext::BoardPiece& p, int w);
// Does a change touch a box (grown by `margin`)?
bool touches(const Change& c, float x0, float y0, float x1, float y1, float margin);
class Board {
 public:
  void reset() {
    prev_.clear();
    have_ = false;
  }
  // What changed since the last pieces (and remember these): the first call, a size change or a change over ~80% of it = full
  Change diff(const std::vector<xrtext::BoardPiece>& pieces, int w, int h);
  // Draw a change into rgba (the w x h picture kept between calls)
  static void draw(const std::vector<xrtext::BoardPiece>& pieces, int w, int h, uint8_t* rgba, const Change& c);
  // diff + draw. Returns the bands drawn again (empty: nothing changed); *full when it was the whole picture
  std::vector<Band> render(const std::vector<xrtext::BoardPiece>& pieces, int w, int h, uint8_t* rgba, bool* full = nullptr);

 private:
  struct Sig {
    uint64_t h;
    float y0, y1;
    Rect box;
  };
  std::vector<Sig> prev_;
  bool have_ = false;
  int w_ = 0, h_ = 0;
};

// ---- the cursor overlay ----
// Its picture: 4 cells, 2 x 2 (0 the cursor, 1-3 its picked flash: strong / mid / faint), in a fixed kAtlasW x kAtlasH swapchain
// (never made again: a new size only redraws the cells)
constexpr int kAtlasW = 2048, kAtlasH = 1024, kCellMaxW = kAtlasW / 2, kCellMaxH = kAtlasH / 2;
struct Atlas {
  bool on = false;
  int cellW = 0, cellH = 0;  // a cell's size (pixels of the atlas)
  float scale = 1.0f;        // atlas pixels per picture pixel (< 1 only for a box too big for a cell)
  float hx = 0, hy = 0;      // the frame's half size at rest (picture pixels): the pop / press scale from it
};
bool same_look(const xrbook::CursorBox& a, const xrbook::CursorBox& b);  // the same picture (size, corner, colour, glow)
// The 4 cells into rgba (stride `stride` pixels; only 2 cellW x 2 cellH written), premultiplied as xrtext's pictures
Atlas cursor_atlas(const xrbook::CursorBox& b, uint8_t* rgba, int stride);
struct Pose {
  bool on = false;
  int cell = 0;                        // the cell shown
  float cx = 0, cy = 0, w = 0, h = 0;  // the quad's middle + size (picture pixels)
};
// Where it goes now: the item's box, its pop (0.22 s after a move: out 16, settling) and press (0.28 s: in 9 + the flash)
Pose cursor_pose(const xrbook::CursorBox& b, const Atlas& a, float moveAgo, float pressAgo);

// ---- what each swapchain image is missing ----
class History {
 public:
  void add(uint64_t version, const std::vector<Band>& bands, int h);  // version's rows changed from version - 1
  // An image holding `have` shown `want`: the rows to copy (false: copy it all)
  bool since(uint64_t have, uint64_t want, int h, std::vector<Band>* out) const;

 private:
  std::deque<std::pair<uint64_t, std::vector<Band>>> q_;
};

}  // namespace xrscreen
