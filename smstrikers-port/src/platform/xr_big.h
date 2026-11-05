// strikers-quest r137: big character pictures.
//
// A character's picture rendered BIG and SHARP on the headset from its own 3D model (the same
// model, pose and studio light on every headset), for the magazine's covers / inside pictures and
// the trading cards. Replaces the 256 x 256 portraits stretched 3-4x.
//
// Thread-safe: get / prefetch may be called from any thread (the magazine builds its pages on a
// worker thread). The render itself happens on the render thread inside the effects layer
// (xr_fx), a little at a time, so it never stalls a frame.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace xrbig {

// A character's big picture: its own 3D model standing in its idle pose, 3/4 view, studio light,
// transparent background (straight alpha RGBA, size x size, size up to 1024). key = the roster pack
// name as in guests/roster.txt (pk_*, mua_* incl. costume packs, Melee/Charged guests, and the 8
// Strikers captains + Strikers sidekicks / clubs as the hub's 3D figures use them). Returns true +
// fills *rgba when ready; false while it renders (asynchronous, never stalls a frame; call again
// later). Cached on disk (STRIKERS_HOME/cache/big/<key>[_shiny]_<size>.png) so each character
// renders once. Notes: size is clamped to 32..1024; a size below 1024 is shrunk from the 1024
// picture when that one exists (else drawn at its size); each true get() copies the picture
// (4 MB at 1024): call it until true, then keep your copy (rgba may be null: just "is it ready").
// A shiny picture is drawn from <key>_shiny.gpk (the normal pack when there is none).
bool get(const std::string& key, bool shiny, int size, std::vector<uint8_t>* rgba);
void prefetch(const std::string& key, bool shiny, int size);

// True when this key can never render (no model on this headset, or the render failed): callers
// stop waiting and use their fallback (the old 256 portrait). False while it may still come (a
// key never asked for is queued, as prefetch). Never touches the disk on the caller's thread.
bool failed(const std::string& key, bool shiny, int size);

// + 1 each time a picture is ready or has failed: a caller that drew without one (its portrait in
// its place) draws again when this has moved on.
unsigned generation();

}  // namespace xrbig
