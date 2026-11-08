// strikers-quest r168 (Lance picked FIREBALL and PLANET EARTH from logs/balls-mockup): BALL SKINS, the headset's side.
// The ball's living surface is drawn by the game itself (include/port/ball_skin.h, src/platform/ball_skin.cpp: in the
// game's own picture, so on the big screen, the Tabletop field and the life-size stadium alike); this is the rest of it:
//   - the shop's side: the catalogue, what you own / have on (saved by xr_cards.cpp in STRIKERS_HOME/cards/looks.txt,
//     headset-wide like the binder skins: "ball <id>" / "ball_owned <id>" lines), the LOOKS page's tiles, its preview
//     (the ball spinning in front of you with its effects: xr_fx.frag's shapes 160 / 161 draw it, per pixel);
//   - the effects round the ball in play (xr_fx.cpp draws them in its effects layer): its aura (a flickering fire glow
//     and flame tongues licking off it / a blue atmosphere glow and rim), its trail while it flies (flame tongues and
//     embers / blue atmosphere wisps and cloud puffs) and its reactions (a kick: an ember burst and a fire ring / a cloud
//     puff ring; a bounce: an ember fountain, a fire ring and a scorch mark / an ocean splash, a cloud ring and ripples;
//     a goal: the big version) - on the Tabletop field, round the life-size ball, or on the big screen's picture where
//     the ball is in it;
//   - online: the friend's equipped ball (xr_net.cpp's 'B' tail): the ball wears the skin of the side that touched it last.
// Looks only: the match plays the same.
#pragma once

#include "xr_goalfx.h"  // (its Piece: the effects' instance; OpenXR's XrPosef)

#include <cstdint>
#include <string>
#include <vector>

namespace xrballs {

struct Item {
  const char* id;    // "classic", "fireball", "earth" (saved, sent online: never change)
  const char* name;  // "FIREBALL"
  const char* desc;  // one line for the shop
  int price;         // TIZM coins (0: everyone owns it)
  int tier;          // as the explosions' (xrgoalfx::tier_name / tier_rgb)
  uint32_t rgb1, rgb2;
};
const std::vector<Item>& catalogue();
int find(const char* id);  // its place, -1 none

// owned / on (any thread). The cards keep the file: they read the lines (load_line) and write save_lines() into it.
bool owned(const char* id);
void add_owned(const char* id);
bool equip(const char* id);   // false: not owned
const char* equipped();       // "classic" until another is
void load_line(const char* key, const char* value);  // a looks.txt line ("ball" / "ball_owned"), as the cards read it
void loaded();                // the cards have read the file (until then the first query reads it here, read-only)
std::string save_lines();
int generation();

// The shop's tile picture of ball `i` (RGBA, size x size, its background clear): rendered once from its own surface
std::vector<uint8_t> tile_rgba(int i, int size);

// The shop's preview (game thread): the ball spinning in front of you (`where`: its middle), its aura and, every few
// seconds, a bounce on an invisible floor under it; stop_preview() ends it. Owned or not.
void preview(const char* id, const XrPosef& where);
void stop_preview();
bool previewing();

// Online (any thread): the friend's ball ("" none / an older build)
void set_friend_equipped(const char* id);
const char* friend_equipped();

// xr_fx.cpp, inside xrfx::update (every display frame): tells the game which skins to wear, follows the ball, runs its
// pieces. True while any of them shows.
bool update(const xrfx::Scene& scene);
bool pieces_up();
// xr_fx.cpp, inside xrfx::render: its pieces as the effects' quads (the same 56 bytes as xr_fx.cpp's Instance), at most
// maxPieces; returns how many
uint32_t write(const xrfx::Scene& scene, xrgoalfx::Piece* out, uint32_t maxPieces);

// r179: the cards' BLACK 1/1 worlds use these real effects (any thread; queued, spawned at the next update, in the room's
// LOCAL space): a real-fire flipbook flame standing up `size` tall, an ember, smoke (colour c1), a crackling bolt along
// `axis` `size` long (c1 core, c2 glow), a water drop (falls), a bubble (rises), a glint, a mist puff (c1 / c2)
enum WorldFx { kWxFlame = 0, kWxEmber, kWxSmoke, kWxBolt, kWxDrop, kWxBubble, kWxGlint, kWxMist };
void world_fx(int kind, const XrVector3f& at, const XrVector3f& v, const XrVector3f& up, float size, float life, uint32_t c1,
              uint32_t c2, const XrVector3f& axis);
void world_take();  // (update's)

}  // namespace xrballs
