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

namespace xrfx {

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
  // r42: the life-size goal scorer, a solid the pieces fall onto: an upright cylinder from its feet
  // (LOCAL), radius and height in meters. Pieces bounce off its sides and, while it stands
  // (scorerStill), come to rest on its head and ride there until it runs.
  bool scorerValid = false;
  bool scorerStill = false;
  XrVector3f scorerFeet{0.0f, 0.0f, 0.0f};
  float scorerRadius = 0.28f, scorerHeight = 1.52f;
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
  struct HomePiece {
    int kind = 0;
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float size = 1.0f;
    uint32_t rgb1 = 0xFFFFFF, rgb2 = 0xFFD21F;
    int theme = 0;
    float glow = 0.0f;
  };
  HomePiece homePieces[24];
  int homeCount = 0;
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
};

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

// The game's own full-screen flash (colour 0..1, opacity), reported every game frame it draws one:
// shown over your whole view while the reports keep coming.
void screen_flash(float r, float g, float b, float a);
// r61: a captain's (eCharacterClass) colours for the home ground: the main one and a second one
// that is never white (it carries the white emblem).
void team_colours(int captain, uint32_t* main, uint32_t* accent);
// r61: a cup's (eTrophyType 0-7) colours on the shelf: the cup and its base.
void cup_colours(int trophy, uint32_t* cup, uint32_t* base);

// Self-test: dump the effects layer (xr_fx.ppm) when the next screen flash is at its height.
void dump_at_flash();

// For the stats line: the most pieces drawn in one frame, and mesh indices per eye, since the last
// call. False when nothing was drawn.
bool take_stats(uint32_t* peakParts, uint32_t* peakIndices);

// Self-test: the next rendered left eye of the effects layer is written to `path` as a PPM, the
// pieces over mid grey so the transparent parts read.
void request_dump(const char* path);

}  // namespace xrfx
