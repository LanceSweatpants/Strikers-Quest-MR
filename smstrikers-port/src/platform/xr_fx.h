// strikers-quest: goal celebration effects for the headset (xr_fx.cpp), driven by xr_quest.cpp.
//
// Confetti in the scorer's colours with their signature piece mixed in (small lit 3D meshes,
// xr_fx_mesh.h) raining from the sky and fading where it lands (r44: no heaps), fireworks, and the
// game's own screen flashes over your whole view, drawn by a small renderer of its own on the OpenXR
// Vulkan device into a projection layer of its own, composited over everything else. It works the
// same in all four modes and at the display rate, whatever the game's frame rate. The Tabletop field
// box and the screen are drawn depth-only first, so pieces behind them stay hidden.
//
// Include after the Vulkan/OpenXR platform defines (VK_USE_PLATFORM_ANDROID_KHR,
// XR_USE_PLATFORM_ANDROID, XR_USE_GRAPHICS_API_VULKAN).
#pragma once

#include <vulkan/vulkan.h>
#include <jni.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <vector>

namespace xrroom {
struct Room;  // r197: xr_room.h
}

namespace xrfx {

// r117: the 3D character models drawn at once (Scene::models, set_model's slots): the 3D evolution's two /
// the character select menu's figure, and the home corner's
// r131: + 18 for the franchise's characters (the hub's pages, trades, awards night: xr_quest.cpp fr_figs_frame), slots 3-20
// r131: + 1, the last: STRIKERS ILLUSTRATED's pages as bending paper (xr_quest.cpp; ModelDraw::under < 0 = paper)
// r137: + 2 before it (21, 22): a STRIKERS trading-card pack and its cards in your hands (xr_cards.h kPackSlot /
// kCardSlot; ModelDraw::under < -1.5 = a card: paper with its foil)
// r138: + 3 more before the magazine's (23-25): two special cards' insides (a window's room, a pop-out, obsidian: under
// -3 / -3.5, in their card's frame: ModelDraw::local) and the cards' light (auras, the spotlight, the dimming: -4)
// r141: + 1 more before the trading cards' (21): a BLACK 1/1's world round you and on its card (under -4.5: tinted light;
// xr_cards.h kFxSlot) - the franchise's 18 stay 3-20 (xr_quest.cpp kFrFigs), the cards 22-26, the magazine 27
// r160 (binder skins + card sleeves): + 2 more before the 1/1's world (21, 22): the 3D binder in your room (its skin, its
// pages: ModelDraw::under -5.5) and the cards' sleeves / top-loaders (-6.5) - xr_cards.h kBinderSlot / kSleeveSlot; the
// franchise's 18 stay 3-20 (xr_quest.cpp kFrFigs = kModelSlots - 12), the cards 23-28, the magazine 29
// r163: + 1 more (20 + 1): the binder's halos / light pool (xr_binderfx.h kBinderFxSlot); the sleeves' slot is the binder's pages'
// r199: + 3 before the binder's halos (21-23): the slips posted on the TIZM SPORTSBOOK machine (kPinSlot), the cabinet
// (kBookSlot) and the ticket its printer prints (kTicketSlot) - xr_quest.cpp; the franchise's 18 stay 3-20 (kFrFigs =
// kModelSlots - 16)
// r201: + 6 before the sportsbook's (21-26): the CARD MUSEUM's pieces out in your room (kMuseumSlot0 ..; xr_quest.cpp); the
// franchise's 18 stay 3-20 (kFrFigs = kModelSlots - 22)
constexpr int kMuseumSlots = 6;
constexpr int kModelSlots = 3 + 18 + kMuseumSlots + 3 + 1 + 2 + 3 + 1 + 2 + 1;
constexpr int kMuseumSlot0 = kModelSlots - 13 - kMuseumSlots;
constexpr int kPinSlot = kModelSlots - 13;
constexpr int kBookSlot = kModelSlots - 12;
constexpr int kTicketSlot = kModelSlots - 11;
// r135: the guests' outline cells in guests/etch.r8 (make_etch, xr_fx_mesh.frag sdGuest): 16 x 32 (r121-r134: 16 x 16),
// one per pickable roster place; a theme of 100 + a cell from 0 to kEtchCells - 1
constexpr int kEtchCells = 512;

struct DeviceInfo {
  XrInstance instance = XR_NULL_HANDLE;
  XrSession session = XR_NULL_HANDLE;
  XrSpace local = XR_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  const VkPhysicalDeviceMemoryProperties* memProps = nullptr;
  uint32_t width = 0, height = 0;  // per eye
  float maxAnisotropy = 0.0f;      // r61: the device's samplerAnisotropy limit when enabled, else 0
  uint32_t roomWidth = 0, roomHeight = 0;  // r61: the cup suite's room layer per eye (full size)
  uint32_t roomSamples = 1;  // r62: its multisampling (4 where colour and depth both have it)
  // r117 (Lance: the home ground's banners "blurry as fuck"): the effects layer's multisampling (1 or 4, never
  // more than roomSamples); it was single-sampled
  uint32_t fxSamples = 1;
};

// Where things are this display frame, in LOCAL space (meters, Y up).
struct Scene {
  float dt = 0.0f;                 // seconds since the last update
  XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
  float floorY = -1.6f;
  bool mr = true;                  // passthrough (Living Room) or Stadium
  bool tabletop = false;
  // Tabletop field: the game's (x, y, z) pitch maps to the room as (x, z, -y) * scale, turned by
  // yaw about +Y, then moved to pos. The pitch is 41.2 x 24.2 game units, ground at z = 0.
  XrVector3f tablePos{0.0f, 0.0f, 0.0f};
  float tableYaw = 0.0f;
  float tableScale = 0.03f;
  float charScale = 1.0f;          // the players are drawn this much bigger (the marker rides higher)
  // The life-size stadium: same mapping with this scale and origin.
  float stadiumScale = 1.0f;
  XrVector3f stadiumOrigin{0.0f, 0.0f, 0.0f};
  // r42: the life-size goal scorer: where its feet are (LOCAL) and how tall it is (meters; where hats and themed
  // pieces start over its head). scorerStill: it stands (pieces that land on it ride there until it runs).
  // r143 (Lance: Arbok's "see through invisible pole in the back of him", the characters see-through to what falls
  // behind them): no more upright cylinder (r42's collision, r44e's ten-sided depth column). The body itself
  // (port/xr_body.h: its posed vertices every game frame, the copy's game space) is drawn depth-only and voxelized
  // for the pieces to bounce off and land on; scorerModel puts that space in the room (column-major, xr_quest.cpp's
  // scorer_model(), the matrix the scorer's own layer is drawn with).
  bool scorerValid = false;
  bool scorerStill = false;
  XrVector3f scorerFeet{0.0f, 0.0f, 0.0f};
  float scorerHeight = 1.52f;
  bool scorerModelValid = false;
  float scorerModel[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
  // The screen quad, for occlusion.
  bool screenValid = false;
  XrPosef screenPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  float screenWidth = 0.0f, screenHeight = 0.0f;
  // r44e: the goal banner over the scorer (a quad turned to you), a solid the pieces bounce off and
  // land on, and that hides what falls behind it.
  // r45d: your eyes are under the Tabletop field (it floats up while the scorer is out): its underside
  // is drawn solid, where the game's field is only seen from above.
  bool tableUnderside = false;
  bool bannerValid = false;
  XrPosef bannerPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  float bannerWidth = 0.0f, bannerHeight = 0.0f;
  // r53 (Lance: "3D Quest controllers in your hands that move exactly with your real ones ... buttons
  // light up, sticks tilt", in "a skin based off whomever character's team you are"): each Touch
  // controller's grip pose (LOCAL) and what is pressed. [0] left, [1] right.
  struct Hand {
    bool valid = false;
    XrPosef grip{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float trigger = 0.0f, squeeze = 0.0f;
    float stick[2] = {0.0f, 0.0f};
    bool stickClick = false;
    bool button1 = false, button2 = false;  // A / X and B / Y
    bool menu = false;                      // r54: the menu button (left)
  };
  Hand hands[2];
  int skinCaptain = -1;  // your team's captain (eCharacterClass), -1 before a match
  // r70c: the etched outline's theme for a guest captain (100 + its cell in etch.r8): an evolved
  // Pokemon (no cell of its own) wears its first form's; 0 = none
  int skinEtch = -1;
  // r57 (Lance: "every cup you've won appears as a little 3D trophy on a shelf you place in MR"): the
  // shelf in your room (Living Room modes only): its board's middle (LOCAL), turned by yaw about +Y
  // (its front faces +Z at 0); the cups won (eTrophyType bits 0-7: Mushroom, Flower, Star, Bowser,
  // then the Super ones) and how often each; a cup just won (it shines and sparkles), -1 none.
  bool shelfValid = false;
  XrVector3f shelfPos{0.0f, 0.0f, 0.0f};
  float shelfYaw = 0.0f;
  unsigned shelfCups = 0;
  unsigned short shelfWins[8] = {};
  int shelfNewCup = -1;
  float shelfNewAge = 0.0f;  // seconds since it was won
  // r57b (Lance: "i want those to be able to be picked up"): a trophy off its spot (in a hand, or
  // gliding back), where it is (LOCAL; its middle, its turn).
  bool shelfCupFree[8] = {};
  XrPosef shelfCupPose[8] = {};
  // r60: its size while off its spot (1 = shelf size; resized in your hands).
  float shelfCupScale[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
  // r60: glowing outlines drawn over everything (the big screen's edge while your pointer is on it or
  // you hold it; the badge your pointer is on): a w x h rectangle (outer size, meters) lying in the
  // x/y plane of `pose`, `thickness` wide inside its edge, in sRGB `rgb`.
  struct Frame {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float width = 0.0f, height = 0.0f, thickness = 0.01f;
    uint32_t rgb = 0xFFFFFF;
    float alpha = 1.0f;
  };
  Frame frames[4];
  int frameCount = 0;
  // r60: where each pointer meets what it points at ([0] left, [1] right), shown as a small dot.
  bool pointerValid[2] = {};
  XrVector3f pointers[2] = {};
  // r61: the home ground's solid pieces (xr_quest.cpp places them): a banner on its rod (kind 0, a
  // unit = `size` meters tall, its middle at the pose, front +Z), a pennant on its pole (1), a ball
  // (2, `size` across), a small ledge (3, the shelf's board at `size` times its width), a pedestal (4,
  // `size` tall), a trophy (5, `size` tall; theme 1 = a ghost of one not won yet). rgb1 / rgb2: the
  // team's colours (a trophy's cup and base); theme: the captain's class + 1 (20 = the comeback
  // arrow); glow 0..1.
  // r89 (Lance: "a home ground for EVERY character": its own corner in your room): a prop on the corner's
  // display (6: theme = the piece's shape, as the celebrations' pieces, `size` tall) and the corner's rug
  // (7: `size` across, lying on the floor, theme = the captain's emblem as a banner's).
  struct HomePiece {
    int kind = 0;
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float size = 1.0f;
    uint32_t rgb1 = 0xFFFFFF, rgb2 = 0xFFD21F;
    int theme = 0;
    float glow = 0.0f;
  };
  HomePiece homePieces[40];
  int homeCount = 0;
  // r89: the home captain's corner (by the trophy shelf, in your room): whose it is (a guest pack, or a
  // Strikers captain's class with the pack empty) and where its display's top and its rug are (LOCAL), for
  // its own little effects there (embers, a flame burning, sparks, fog on the rug, falling petals ...).
  bool cornerOn = false;
  XrVector3f cornerTop{0.0f, 0.0f, 0.0f};
  XrVector3f cornerRug{0.0f, 0.0f, 0.0f};
  float cornerRadius = 0.7f;
  int cornerClass = -1;
  char cornerPack[48] = {};
  bool suite = false;  // r61: the cup suite (VR): the shelf shows there too
  // r61: little electric sparks crackling round these points (the Super Strike ball), in `sparkRgb`.
  XrVector3f sparkAt[2] = {};
  float sparkRadius[2] = {};
  int sparkCount = 0;
  uint32_t sparkRgb = 0x7FD8FF;
  // r61 (Lance: "let me walk within my environment, to inspect this stuff up close, aka dont move the
  // game if i move in real life"): the cup suite as a room you walk about in (set_room, render_room),
  // standing where its middle is on the floor (LOCAL, the floor's height), turned by roomYaw about +Y
  // (its window, ahead, is -Z at 0). Its walls hide the effects behind them; its live things follow
  // suiteCaptain's world (eCharacterClass).
  bool roomShown = false;
  bool roomEvo = false;  // r119: an evolution's backdrop (evo_<theme>): none of the cup suite's live things
  XrVector3f roomOrigin{0.0f, 0.0f, 0.0f};
  float roomYaw = 0.0f;
  int suiteCaptain = -1;
  // r61: the suite's boards and name plaque (quad layers under the effects): solid stand-ins, each a
  // width x height rectangle in the x/y plane of its pose (LOCAL).
  struct Panel {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float width = 0.0f, height = 0.0f;
  };
  Panel panels[4];
  int panelCount = 0;
  // r96 (Lance: "i want to watch that shit occur in 3D"): characters standing in your room (the 3D
  // evolution, xr_evo.cpp): the model set_model put in slot i, its vertices posed this frame (LOCAL, 6
  // floats each: position, normal; set_model's vertexCount of them), how white it is (the evolution's
  // silhouette, 0..1), how see-through (alpha 0..1), its glow and the glow's colour, the light from
  // below in that colour, and which frame of a flame flipbook shows (-1 all).
  struct ModelDraw {
    bool on = false;
    const float* posNrm = nullptr;
    float white = 0.0f, alpha = 1.0f, glow = 0.0f, under = 0.0f;
    uint32_t glowRgb = 0xFFFFFF;
    int flameFrame = -1;
    float shine = 0.0f;  // r127: a shiny one's glint sweeping over it (0 none .. 1)
    // r131: a baked model (Model::frames, posNrm unused): which of its frames shows, and where its own space (feet
    // at the origin, facing +X, Y up) stands in the room: `scale` times, turned by `yaw` about +Y (0 faces +X), its
    // feet at `at` (LOCAL)
    int frame = -1;
    XrVector3f at{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f, scale = 1.0f;
    // r138: a model in its own frame (a trading card's: xr_cards.cpp; posNrm in that frame): `toWorld` (column-major: a
    // turn, a uniform scale, a place) puts it in the room, and your eye goes into that frame for its light
    bool local = false;
    float toWorld[16] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    // r147: a BLACK 1/1's kaleidoscope (xr_fx_model.frag, its slab's back wall): its card's colour 0xRRGGBB (0 none; a
    // local model only)
    uint32_t kaleido = 0;
    // r193: a special trading card's 3D (xr_fx_model.frag): `reveal` >= 0 cuts it to its face's side (nothing of it while
    // your eye is behind the card) and shows what stands on its top edge only that far risen (0 none .. 1 all); < 0 no
    // cut. `dim` darkens its figure / throne (0 .. 1: its autograph's moment)
    float reveal = -1.0f, dim = 0.0f;
  };
  ModelDraw models[kModelSlots];  // r117: 0-1 the 3D evolution / the menu's figure, 2 the home corner's; r131: 3- franchise
  // r137: the match's weather (xr_weather.h PORT_WX_* bits: 1 rain, 2 snow, 4 night; 0 none here), how far it has come
  // in (0..1), how much snow lies (0..1), and the stadium's floodlight banks (GAME world: metres, Z up) for night.
  // Rain / snow fall over the Tabletop field and onto your table when `tabletop && mr`, round you in the life-size
  // stadium when !mr.
  uint8_t weather = 0;
  float weatherAmt = 0.0f, snowCover = 0.0f;
  XrVector3f wxLights[8] = {};
  int wxLightCount = 0;
  // r196 (WEATHER-UPGRADE.md, looks and sound only): the tier (xr_weather.h PORT_WX_TIER_*), how strong it falls now
  // (0..1), the wind (GAME world m/s: x along the pitch, y across) + its gust (0..1), the lightning's flash (0..~1.3) and
  // the bolt showing (its strike id, how much it shows, kind 0 far 1 mid 2 close, x -1..1, its segments x0 y0 x1 y1 w in
  // its own space), the snow lying on the pitch (a cols x rows grid of 0..1 over the pitch, GAME world) and your play
  // area (the Guardian's rectangle on the floor: its middle + turn as a pose in LOCAL, half its size) for the dusting.
  int wxTier = 0;
  float wxInten = 0.0f;
  float wxWind[2] = {0.0f, 0.0f};
  float wxGust = 0.0f;
  float wxFlash = 0.0f;
  int wxBoltId = -1, wxBoltKind = 0, wxBoltSegs = 0;
  float wxBoltVis = 0.0f, wxBoltX = 0.0f;
  float wxBolt[200 * 5] = {};
  float wxSnow[32 * 18] = {};
  int wxSnowCols = 0, wxSnowRows = 0;
  bool playValid = false;
  XrPosef playPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  float playHalf[2] = {0.0f, 0.0f};
  // r197 (Lance: "have Strikers use that space as my environment"): YOUR ROOM SCAN (xr_scene.h / xr_room.h) - the walls,
  // floor, ceiling, furniture tops and the scan's mesh as a collision model in LOCAL, set only in MR while SETTINGS > ROOM >
  // USE MY ROOM SCAN is on and a scan was read (null: the floor plane / play area as r196). Rain and snow land on your
  // real furniture and pile against your walls; confetti, hats, embers and the goal explosions' debris land on it too.
  const xrroom::Room* room = nullptr;
};

// r96: a character model for Scene::models (xr_evo.cpp builds it from a guest pack): its pictures (RGBA,
// rows top first), its texture coordinates (2 floats a vertex) and triangles, in parts by picture. A soft
// part is blended with its picture's alpha (gas, fire), drawn after the solid ones; flameFrame >= 0: one
// frame of a flame flipbook (shown only on its turn, ModelDraw::flameFrame).
struct ModelPart {
  int tex = -1;
  bool soft = false;
  int flameFrame = -1;
  uint32_t firstIndex = 0, indexCount = 0;
};
struct Model {
  std::vector<std::vector<uint8_t>> texRgba;
  std::vector<uint32_t> texW, texH;
  std::vector<float> uv;
  std::vector<uint32_t> indices;
  std::vector<ModelPart> parts;
  uint32_t vertexCount = 0;
  // r131: its poses baked ahead (frameCount x vertexCount x 6 floats: position, normal, in its own space): they go up
  // to the GPU with it and a frame is picked per draw (ModelDraw::frame), nothing posed or copied per frame
  std::vector<float> frames;
  uint32_t frameCount = 0;
  // r210: a picture's whole mip chain made ahead on a worker (texMips[i] = levels 1..n of picture i, each half the last, straight
  // RGBA): it goes up as it is (copies only - no blits on the frame thread); empty = the GPU filters it as before. texSharp[i]:
  // that picture sampled with up to 16x anisotropy (the museum's card atlas; empty / 0 = the models' usual sampler)
  std::vector<std::vector<std::vector<uint8_t>>> texMips;
  std::vector<uint8_t> texSharp;
};
// Render thread only: puts a model in slot 0 / 1 (its pictures go up to the GPU now, waiting for it), or
// with an empty one lets that slot's go.
void set_model(int slot, Model&& model);
// r131: a model is up in that slot (set_model worked, the renderer not restarted since)
bool model_up(int slot);
// r226: one picture of the model up in that slot replaced (render thread only; waits for the frames in flight, one upload - the
// rest of the model untouched): a card front drawn again once its big picture came in. mips / sharp as Model::texMips / texSharp.
// False: no model there / no such picture / the upload failed (the old picture is gone then: grey, as set_model's).
bool set_model_tex(int slot, int tex, const std::vector<uint8_t>& rgba, uint32_t w, uint32_t h,
                   const std::vector<std::vector<uint8_t>>* mips = nullptr, bool sharp = false);

// r96 (Lance: "their celebration stuff minus the confetti to fall during that process ... for pichu, it
// should JUST be lightning that falls, for squirtle it should be water that falls/water cannons,
// charmander -- fire"): a character's own celebration theme (special_for_pack: what falls, its room
// effects, its show: water cannons, fire jets, lightning) WITHOUT any confetti, its pieces falling round
// `at` (LOCAL, on the floor) within `radius` from `height` above it. Called every frame while it lasts
// (intensity 0.3 calm .. 2 the peak); evo_burst = the moment it evolves (a ring of light and a shower of
// its pieces); evo_end lets it taper off. pack null / unknown: nothing.
void evo_theme(const char* pack, const XrVector3f& at, float radius, float height, float intensity);
void evo_burst(const Scene& scene, const XrVector3f& at, float height, uint32_t rgb);
void evo_end();
// r96: a shiny one's twinkle round a standing model: stars popping round `at` (its feet), `height` tall,
// for this frame's `dt` seconds.
void evo_sparkle(const XrVector3f& at, float radius, float height, float dt);
// r138: the same in colours of your own (a trading card's: gold, a rainbow's), `rate` a second
void card_sparkle(const XrVector3f& at, float radius, float height, float rate, float dt, uint32_t rgb1, uint32_t rgb2);
// r193: an autograph's moment (xr_cards.cpp sign_air): sparks of light drawing in to `at` from `radius` m round it; tiny
// sparks dropping off its pen tip (falling, fading); its landing on the card - a ring facing you (`head`) and a burst of
// sparks, no white-out (`size` m: the autograph's width on the card)
void ink_gather(const XrVector3f& at, float radius, float rate, float dt, uint32_t core, uint32_t outer);
void ink_sparks(const XrVector3f& at, float rate, float dt, uint32_t core, uint32_t outer);
void ink_land(const XrVector3f& at, const XrVector3f& head, float size, uint32_t rgb);

// r61: the cup suite's room, baked in Blender (tools/suites): one mesh with its light in one picture,
// and the world outside as a 360 picture all round, infinitely far.
struct Room {
  // Per vertex (kRoomFloats): x, y, z (meters: x right, y up from the floor, -z ahead), u, v, the
  // normal, and its shine: gloss 0..1, metal 0 / 1 (r62).
  static constexpr int kRoomFloats = 10;
  std::vector<float> verts;
  std::vector<uint32_t> indices;  // triangles
  std::vector<uint8_t> rgba;      // the light: width x height, rows top first
  uint32_t width = 0, height = 0;
  std::vector<uint8_t> outside;   // equirectangular, its middle straight ahead, rows top first
  uint32_t outsideWidth = 0, outsideHeight = 0;
  float lamp[3] = {1.0f, 0.92f, 0.8f};  // r62: its ceiling lamps' colour (the live highlights)
  // r62: its LED lights (kLedFloats each): x, y, z (as the vertices), group (0 the strips round the
  // walls, 1 the bulbs round the big screen, 2 round the name plaque), order along it, colour 0-3
  // (Mario-logo red, yellow, green, blue).
  static constexpr int kLedFloats = 6;
  std::vector<float> leds;
};
// Hands a room over (any thread); it goes up to the GPU at the next render_room. An empty one lets
// the last one go (and its memory) at the next render or render_room.
void set_room(Room&& room);
// r170: the shop's preview ball's picture (xr_balls.cpp: its baked frames, an RGBA atlas w x h, rows top first), up at
// the next render (xr_fx.frag shape 160 reads it). Any thread.
void set_ball_atlas(std::vector<uint8_t>&& rgba, uint32_t w, uint32_t h);
// Draws the room and the world outside into a full-size projection layer of their own (solid, the
// first layer: the game's screen and the boards go over it, the effects over those). False while
// there is no room to show.
bool render_room(const Scene& scene, XrCompositionLayerProjection* layer);

bool init(const DeviceInfo& device);
void shutdown();

// team 0/1; character = eCharacterClass (0 Birdo .. 11 Yoshi, -1 unknown).
void goal(int team, int character, bool superStrike, bool hyper, bool ownGoal);
// 1 replay started, 2 replay played out, 3 skipped, 4 play live again.
void phase(int phase);

// Steps the effects. True while anything is alive or still to come.
bool update(const Scene& scene);
// Draws into the effects swapchains and fills `layer` (space and views). False if nothing to show.
bool render(const Scene& scene, XrCompositionLayerProjection* layer);

// The replay jumbotron, 0 = up and out of the way, 1 = all the way down (eased, may overshoot).
float jumbotron_drop();

// r221: the effects layer's own GPU time per frame (two timestamps round its command buffer). set_gpu_timing once after
// init (the device's timestamp period, the queue's valid bits; 0 bits = off); take_gpu_stats: the sum / max / frames
// since the last call (false when timing is off); last_frame_done: whether the newest effects frame is done on the GPU
// (1), still running (0), none yet (-1) - asked when the next xrWaitFrame returns
void set_gpu_timing(float timestampPeriodNs, uint32_t validBits);
bool take_gpu_stats(uint64_t* sumNs, uint64_t* maxNs, uint32_t* frames);
int last_frame_done();

// Cannon waves fired since the last goal (xr_quest.cpp buzzes the controllers on each new one).
int cannon_waves_fired();

// r43c: a hat trick. Caps are thrown in from all around and land on the floor around (x, y, z),
// LOCAL (the scorer's spot); `room` = your room (fewer, from around you) rather than the stadium.
void hat_trick(float x, float y, float z, bool room);

// The Tabletop "that's you" marker: the controlled player's position in game units and his team's
// captain (eCharacterClass) for the colour. Drawn only while scene.tabletop.
void set_marker(bool shown, float x, float y, float z, int teamCaptain);

// r55 (Lance: "a Bob-omb blast ... throws sparks onto your floor"): an item blast in your room. Embers
// burst from `at` (LOCAL), mostly along `dir` (unit: up for one on the Tabletop field or the life-size
// pitch, out of the big screen toward you for one shown there), bounce on the floor and the Tabletop
// field and die out. `unit`: meters per game unit where it happens (the table's scale, the stadium's,
// or the screen's size); strength 1..3 (the item's size + 1); kind 0 fire, 1 dust (Chain Chomp).
void blast(const XrVector3f& at, const XrVector3f& dir, float unit, float strength, int kind);

// r124 (Lance: Spider-Man's "signature wrist flick trying to shoot webs"): a web line shot from `from` (LOCAL, his
// wrist) along `dir` (any length), `length` meters out: the strand, and a little web opening where it ends.
void web_shot(const XrVector3f& from, const XrVector3f& dir, float length);

// The game's own full-screen flash (colour 0..1, opacity), reported every game frame it draws one:
// shown over your whole view while the reports keep coming.
void screen_flash(float r, float g, float b, float a);
// r61: a captain's (eCharacterClass) colours for the home ground: the main one and a second one
// that is never white (it carries the white emblem).
void team_colours(int captain, uint32_t* main, uint32_t* accent);
// r61: a cup's (eTrophyType 0-7) colours on the shelf: the cup and its base.
void cup_colours(int trophy, uint32_t* cup, uint32_t* base);

// r89: the props on a home corner's display: up to 3 solid pieces (their shape for HomePiece kind 6, and
// colours) from the captain's theme: a guest pack (its celebration's falling pieces; a Pokemon without solid
// ones a Poke Ball), or a Strikers captain's class (pack null / empty). Returns how many (at least 1).
int corner_props(const char* pack, int captainClass, int shapes[3], uint32_t c1[3], uint32_t c2[3]);

// Self-test: dump the effects layer (xr_fx.ppm) when the next screen flash is at its height.
void dump_at_flash();

// For the stats line: the most pieces drawn in one frame, and mesh indices per eye, since the last
// call. False when nothing was drawn.
bool take_stats(uint32_t* peakParts, uint32_t* peakIndices);
// r130: the celebration pieces' budget scale (0.25 .. 1): xr_quest lowers it while the headset misses display
// frames and raises it back while it holds them
void set_load_scale(float scale);
float load_scale();
bool pieces_alive();  // r130: any celebration piece up (the load scale only steps down then)

// r153: the goal explosions (xr_goalfx.cpp). Its 3D pieces are the celebration's meshes: the mesh a celebration shape
// is drawn with (paper / foil: variant 0-2 its three bends, 3 a streamer), -1 a quad. And a goal explosion has the
// goal's moment first: the usual show (the flash, the confetti waves, the rain, the fireworks, the pyro, a themed show)
// waits this long after the goal (what is up already keeps moving).
int piece_mesh(int shape, int variant);
void hold_celebration(float seconds);
// r207 (the CASINO WIN PARTY, xr_goalfx.cpp casino_party): a giant 3D letter's mesh (B I G W N H U E J A C K P O T; -1 none) and
// its width (cap height 1), the siren light's mesh
int glyph_mesh(char ch, float* width);
int siren_mesh();
int casino_coin_mesh();  // (a light coin: ~100 triangles)

// Self-test: the next rendered left eye of the effects layer is written to `path` as a PPM, the
// pieces over mid grey so the transparent parts read.
void request_dump(const char* path);

}  // namespace xrfx
