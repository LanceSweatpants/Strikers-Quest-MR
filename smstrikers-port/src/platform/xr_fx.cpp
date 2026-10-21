// See xr_fx.h. Built with xr_quest.cpp in strikers_xr (C++20, no port prelude).

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#define VK_USE_PLATFORM_ANDROID_KHR 1
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include "xr_fx.h"

#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <random>
#include <utility>
#include <vector>

#include "xr_fx_mesh.h"
#include "xr_ctrl_model.h"  // r54: the controllers in your hands
#include "xr_fx_shaders.h"

namespace xrfx {
namespace {

#define FXLOG(...) __android_log_print(ANDROID_LOG_INFO, "strikers_xr", __VA_ARGS__)
#define FXERR(...) __android_log_print(ANDROID_LOG_ERROR, "strikers_xr", __VA_ARGS__)

// ---------------------------------------------------------------------------------------------
// Themes: the scorer's colours and signature piece (VISION.md, Lance 2026-10-10)
// ---------------------------------------------------------------------------------------------

enum Shape : uint8_t {
  kPaper = 0, kCoin, kHeart, kStar, kPetal, kMushroom, kEgg, kBanana, kShell, kHammer, kRose, kGarlic, kBow,
  kSpark, kMetal = 15, kFlash = 16, kMarkerRing = 17, kMarkerArrow = 18, kWash = 19,
  kCap = 20,  // r43c: the hat trick's hats (mesh only)
  kController = 21,  // r53: the controllers in your hands (mesh only)
  kTrophyShape = 22, kShelfShape = 23,  // r57: the trophy shelf (mesh only)
  kFrameShape = 24,  // r60: a glowing outline (Scene::frames), thickness in colour 2's alpha
  kBooShape = 28     // r61: a Boo in Luigi's suite (mesh only, on the ball; params[1] = shy)
};

// The 3D mesh each piece is drawn with (xr_fx_mesh.h); sparks, flashes and the marker stay quads.
constexpr uint8_t kNoMesh = 0xFF;
uint8_t mesh_for(Shape shape) {
  switch (shape) {
  case kCoin: return mesh::kCoin;
  case kHeart: return mesh::kHeart;
  case kStar: return mesh::kStar;
  case kPetal: return mesh::kPetal;
  case kMushroom: return mesh::kMushroom;
  case kEgg: return mesh::kEgg;
  case kBanana: return mesh::kBanana;
  case kShell: return mesh::kShell;
  case kHammer: return mesh::kHammer;
  case kRose: return mesh::kRose;
  case kGarlic: return mesh::kGarlic;
  case kBow: return mesh::kBow;
  case kCap: return mesh::kCap;
  default: return kNoMesh;
  }
}

// How a piece comes to rest on the floor or the table: which of its own axes ends up pointing up
// (0 x, 1 y, 2 z) and how far its centre then sits above the surface, in piece sizes.
struct Rest {
  int up;
  float half;
};
Rest rest_for(uint8_t shape) {
  switch (shape) {
  case kCoin: return {2, 0.065f};
  case kHeart: return {2, 0.17f};
  case kStar: return {2, 0.16f};
  case kPetal: return {2, 0.06f};
  case kMushroom: return {1, 0.5f};   // stands up
  case kEgg: return {0, 0.36f};       // on its side
  case kBanana: return {2, 0.12f};
  case kShell: return {1, 0.14f};     // dome up
  case kHammer: return {2, 0.16f};
  case kRose: return {2, 0.27f};
  case kGarlic: return {1, 0.48f};
  case kBow: return {2, 0.1f};
  case kCap: return {1, 0.12f};       // crown up
  default: return {2, 0.02f};         // paper, foil
  }
}

// The Tabletop "that's you" marker colour, by the team captain's eCharacterClass.
uint32_t marker_colour(int captain) {
  switch (captain) {
  case 1: return 0xFF8C1A;   // Daisy
  case 2: return 0xFFC21F;   // Donkey Kong
  case 5: return 0x1FA83A;   // Luigi
  case 6: return 0xE4202A;   // Mario
  case 7: return 0xFF6FB5;   // Peach
  case 9: return 0x8A3FD1;   // Waluigi
  case 10: return 0xFFD21F;  // Wario
  case 11: return 0x3CC23C;  // Yoshi
  default: return 0x2F9BFF;
  }
}

struct Theme {
  uint32_t paper[3];  // 0xRRGGBB
  Shape piece;
  uint32_t pieceMain, pieceDetail;
};

// Indexed by eCharacterClass (Character.h): Birdo, Daisy, DK, Hammer Bro, Koopa, Luigi, Mario,
// Peach, Toad, Waluigi, Wario, Yoshi.
const Theme kThemes[12] = {
    {{0xFF6FAF, 0xE8202A, 0xFFFFFF}, kBow, 0xFF5FA0, 0xE8202A},
    {{0xFF8C1A, 0xFFD21F, 0xFFFFFF}, kPetal, 0xFFFFFF, 0xFFD21F},
    {{0x8B5A2B, 0xFFD21F, 0xD7262E}, kBanana, 0xFFD83A, 0x6B4423},
    {{0x2F6FE0, 0xC8CCD4, 0x2FB34A}, kHammer, 0x7A4A23, 0xC8CCD4},
    {{0x2FB34A, 0xFFD21F, 0xFFFFFF}, kShell, 0x22A13C, 0xF4F1E6},
    {{0x1FA83A, 0xFFFFFF, 0x1C2A6B}, kMushroom, 0x1FA83A, 0xFFFFFF},
    {{0xE4202A, 0xFFFFFF, 0x1D4FD6}, kCoin, 0xFFC21F, 0xFFE27A},
    {{0xFF8FC8, 0xFFFFFF, 0xFFC83A}, kHeart, 0xFF4F9A, 0xFFB0D6},
    {{0xE4202A, 0xFFFFFF, 0x2A56C6}, kMushroom, 0xE4202A, 0xFFFFFF},
    {{0x6B2FB3, 0x1A1A1A, 0xF0C419}, kRose, 0xD81E3A, 0x2E8B3A},
    {{0xFFD21F, 0x6E2C9C, 0xFFFFFF}, kGarlic, 0xF6F0DC, 0xD9CDAE},
    {{0x3CC23C, 0xFFFFFF, 0xE4202A}, kEgg, 0xFFFFFF, 0x3CC23C},
};
const Theme kNeutral = {{0xFFD21F, 0xFFFFFF, 0xE4202A}, kStar, 0xFFD21F, 0xFFF2A0};
const char* const kNames[12] = {"Birdo", "Daisy", "Donkey Kong", "Hammer Bro", "Koopa", "Luigi",
                                "Mario", "Peach", "Toad", "Waluigi", "Wario", "Yoshi"};

// 0xRRGGBB -> R8G8B8A8 in memory order (little-endian 0xAABBGGRR).
uint32_t pack(uint32_t rgb, uint8_t a = 255) {
  return ((rgb >> 16) & 0xFF) | (((rgb >> 8) & 0xFF) << 8) | ((rgb & 0xFF) << 16) | (uint32_t(a) << 24);
}

// ---------------------------------------------------------------------------------------------
// Simulation
// ---------------------------------------------------------------------------------------------

// r55: kKindEmber, an item blast's spark that falls for real and bounces on the floor / the field.
enum Kind : uint8_t { kKindPaper, kKindPiece, kKindSpark, kKindRocket, kKindFlash, kKindEmber };

// Firework burst styles.
enum Burst : uint8_t { kBurstSphere, kBurstRing, kBurstWillow, kBurstDouble };

struct Particle {
  float p[3];
  float v[3];
  float q[4];     // orientation; all zero = faces the eye
  float axis[3];  // spin axis (unit)
  float spin;     // rad/s
  float size, aspect;
  uint32_t c1, c2;
  uint8_t shape, kind;
  uint8_t mesh;   // mesh::Id, or kNoMesh for a quad
  bool landed;
  float age, life;
  float flutterPhase, flutterFreq, flutterAmp;
  float fallSpeed;
  float glow;
  float burstHeight;  // rockets: where they burst
  float drag;         // sparks: velocity kept per second (willows hang, spheres pop)
  float gravity;      // sparks: share of gravity
  bool onScorer;      // r42: resting on the life-size scorer's head (rides along, falls when it runs)
  float restOff[2];   // r42: where on the head, from its feet (x, z)
  float maxAge;       // r61: a suite piece still in the air is gone this old (fading out), 0 = 25 s
};

enum class Stage { Idle, Raining, Tapering };

// One instance as the vertex shader reads it (xr_fx.vert).
struct Instance {
  float posSize[4];
  float quat[4];
  uint32_t color1, color2;
  float params[4];  // shape, aspect, alpha, glow
};
static_assert(sizeof(Instance) == 56, "instance layout");

// Lance, 2026-10-10: "like we just won the Super Bowl with every goal".
constexpr uint32_t kMaxParticles = 16000;
constexpr uint32_t kSlots = 3;
constexpr uint32_t kMaxOccluderVerts = 256;  // r44e: + the scorer and the banner

struct Slot {
  VkBuffer instances = VK_NULL_HANDLE;
  VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
  void* instanceMapped = nullptr;
  VkBuffer occluders = VK_NULL_HANDLE;
  VkDeviceMemory occluderMemory = VK_NULL_HANDLE;
  void* occluderMapped = nullptr;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  bool pending = false;
};

struct Eye {
  XrSwapchain swapchain = XR_NULL_HANDLE;
  std::vector<XrSwapchainImageVulkanKHR> images;
  std::vector<VkImageView> views;
  std::vector<VkFramebuffer> framebuffers;
  VkImage depth = VK_NULL_HANDLE;
  VkDeviceMemory depthMemory = VK_NULL_HANDLE;
  VkImageView depthView = VK_NULL_HANDLE;
  // r62: the room layer's multisampled colour (resolved into the swapchain image); depth above is
  // multisampled too then.
  VkImage msaa = VK_NULL_HANDLE;
  VkDeviceMemory msaaMemory = VK_NULL_HANDLE;
  VkImageView msaaView = VK_NULL_HANDLE;
};

struct State {
  bool initTried = false;
  bool ready = false;
  DeviceInfo dev;

  Stage stage = Stage::Idle;
  float stageTime = 0.0f;
  float sinceGoal = 0.0f;
  Theme theme = kNeutral;
  float intensity = 1.0f;
  bool superStrike = false;
  int cannonWavesLeft = 0;  // waves still to fire, one every kWaveGap
  float cannonTimer = 0.0f;
  int cannonWave = 0;       // waves fired so far this goal (for the buzz pattern)
  int pendingRockets = 0;
  float rainAccum = 0.0f;
  float rocketTimer = 0.0f;
  float jetTime = 0.0f;     // pyro jets fire while > 0
  float jetAccum = 0.0f;
  bool pendingFlash = false;
  float drop = 0.0f, dropTarget = 0.0f, dropVel = 0.0f;
  bool markerShown = false;      // the player you control (Tabletop)
  float markerGame[3] = {0.0f, 0.0f, 0.0f};
  uint32_t markerColour = 0x2F9BFF;
  float markerTime = 0.0f;
  // The game's own screen flash (its white wipe flashes, the Super Strike photo flash), shown over
  // your whole view instead of on the game picture (Lance, 2026-10-10). The game reports it every
  // frame it draws one; it is gone once the reports stop.
  float washRgb[3] = {1.0f, 1.0f, 1.0f};
  float washAlpha = 0.0f;
  float washAge = 1.0f;
  float washPeak = 0.0f;
  bool washDumpArmed = false;  // self-test: dump the effects layer at the flash's height
  float nearAccum = 0.0f;      // pieces dropped close around you
  // r43c: the hat trick, hats thrown in from all around onto the floor around the scorer.
  int hatsLeft = 0;
  float hatAccum = 0.0f;
  XrVector3f hatCentre{0.0f, 0.0f, 0.0f};
  std::vector<Particle> parts;
  std::mt19937 rng{20261010u};
  // r55: item blasts waiting for the next update (blast()).
  struct Blast {
    XrVector3f at, dir;
    float unit, strength;
    int kind;
  };
  std::vector<Blast> pendingBlasts;
  float shelfSparkAccum = 0.0f;  // r57

  VkFormat colorFormat = VK_FORMAT_UNDEFINED;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipeline fxPipeline = VK_NULL_HANDLE;
  VkPipeline occluderPipeline = VK_NULL_HANDLE;
  VkPipeline markerPipeline = VK_NULL_HANDLE;  // the effects pipeline without the depth test
  VkPipeline meshPipeline = VK_NULL_HANDLE;    // the 3D pieces: depth tested and written
  VkBuffer meshVerts = VK_NULL_HANDLE, meshIndices = VK_NULL_HANDLE;
  VkDeviceMemory meshVertMemory = VK_NULL_HANDLE, meshIndexMemory = VK_NULL_HANDLE;
  // [0] full detail (pieces near you), [1] reduced (far away or lying on the floor).
  mesh::Range meshRanges[2][mesh::kCount];
  uint32_t statPeakParts = 0, statPeakIndices = 0;  // for the stats line (largest frame since)
  VkCommandPool pool = VK_NULL_HANDLE;
  Eye eyes[2];
  Slot slots[kSlots];
  uint32_t slot = 0;
  XrCompositionLayerProjectionView layerViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                    {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  char dumpPath[256] = {};
  VkBuffer dumpBuffer = VK_NULL_HANDLE;
  VkDeviceMemory dumpMemory = VK_NULL_HANDLE;
  void* dumpMapped = nullptr;

  // r61: the walkable cup suite (render_room): full-size eye images of its own, the room's light and
  // the world outside as mipmapped textures, the room's and the sky dome's triangles.
  struct Tex {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
  };
  bool roomLayerTried = false, roomLayerReady = false;
  VkRenderPass roomRenderPass = VK_NULL_HANDLE;  // r62: multisampled, resolved into the eye images
  Eye roomEyes[2];
  float roomLamp[3] = {1.0f, 0.92f, 0.8f};      // r62: the room's lamp colour (live highlights)
  std::vector<float> roomLeds;                   // r62: its LED lights (Room::leds)
  Slot roomSlots[kSlots];  // their command buffers and fences
  uint32_t roomSlot = 0;
  Tex roomTex, outsideTex;
  VkDescriptorSetLayout texSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool texPool = VK_NULL_HANDLE;
  VkDescriptorSet roomSet = VK_NULL_HANDLE, outsideSet = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkPipelineLayout roomLayout = VK_NULL_HANDLE;
  VkPipeline roomPipeline = VK_NULL_HANDLE;       // render_room: textured, solid
  VkPipeline roomDepthPipeline = VK_NULL_HANDLE;  // the effects' pass: the room's walls, depth only
  VkBuffer roomVerts = VK_NULL_HANDLE, roomIndices = VK_NULL_HANDLE;
  VkDeviceMemory roomVertMemory = VK_NULL_HANDLE, roomIndexMemory = VK_NULL_HANDLE;
  uint32_t roomIndexCount = 0;
  VkBuffer skyVerts = VK_NULL_HANDLE, skyIndices = VK_NULL_HANDLE;
  VkDeviceMemory skyVertMemory = VK_NULL_HANDLE, skyIndexMemory = VK_NULL_HANDLE;
  uint32_t skyIndexCount = 0;
  XrCompositionLayerProjectionView roomLayerViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                        {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  // r61: the suite's live things (suite_live, write_suite).
  float suiteTime = 0.0f;
  float flashAccum = 0.0f, moteAccum = 0.0f, themeAccum = 0.0f;
  float rocketIn = 2.0f, popIn = 2.0f, dropIn = 1.5f;
  float booShy[5] = {};
};
State s;

// r61: a room handed over by set_room (any thread), taken by the next render or render_room.
std::mutex g_roomMutex;
bool g_roomPending = false;
Room g_roomNext;

float frand(float a, float b) { return std::uniform_real_distribution<float>(a, b)(s.rng); }
int irand(int n) { return std::uniform_int_distribution<int>(0, n - 1)(s.rng); }

void quat_axis_angle(const float axis[3], float angle, float out[4]) {
  const float h = 0.5f * angle, sh = std::sin(h);
  out[0] = axis[0] * sh;
  out[1] = axis[1] * sh;
  out[2] = axis[2] * sh;
  out[3] = std::cos(h);
}

void quat_mul(const float a[4], const float b[4], float out[4]) {
  const float x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
  const float y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
  const float z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
  const float w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
  out[0] = x;
  out[1] = y;
  out[2] = z;
  out[3] = w;
}

void random_unit(float out[3]) {
  const float z = frand(-1.0f, 1.0f), a = frand(0.0f, 6.2831853f), r = std::sqrt(std::max(0.0f, 1.0f - z * z));
  out[0] = r * std::cos(a);
  out[1] = r * std::sin(a);
  out[2] = z;
}

void random_quat(float out[4]) {
  float axis[3];
  random_unit(axis);
  quat_axis_angle(axis, frand(0.0f, 6.2831853f), out);
}

// Game (x, y, z) -> LOCAL, the mapping xr_quest.cpp's m4_game_to_room uses.
XrVector3f game_to_room(float scale, float yaw, const XrVector3f& origin, float gx, float gy, float gz) {
  const float a = gx, b = gz, c = -gy;
  const float cs = std::cos(yaw), sn = std::sin(yaw);
  return XrVector3f{(a * cs + c * sn) * scale + origin.x, b * scale + origin.y, (-a * sn + c * cs) * scale + origin.z};
}

XrVector3f head_of(const Scene& sc) {
  return XrVector3f{0.5f * (sc.views[0].pose.position.x + sc.views[1].pose.position.x),
                    0.5f * (sc.views[0].pose.position.y + sc.views[1].pose.position.y),
                    0.5f * (sc.views[0].pose.position.z + sc.views[1].pose.position.z)};
}

// Size of everything: the room is small, the stadium is not.
struct Scale {
  float paper, piece, spark;
  float paperFall, pieceFall;
  float streamer;           // streamer length
  float rainRadius, rainHeight;
  float rainRate;
  float cannonSpeed;
  int cannonPapers, cannonPieces;
  float flutter;
  float burstSpeed;         // firework sparks
  int burstSparks;
  float jetSpeed, jetSpark; // pyro jets
};
Scale scale_for(const Scene& sc) {
  if (sc.mr) {
    return Scale{0.035f, 0.075f, 0.022f, 0.7f, 1.05f, 0.24f, 1.8f, 0.0f, 260.0f, 3.4f, 240, 30, 0.38f,
                 1.1f, 90, 1.6f, 0.02f};
  }
  return Scale{0.12f, 0.26f, 0.65f, 2.0f, 2.8f, 0.5f, 8.0f, 10.0f, 650.0f, 10.0f, 420, 45, 0.95f,
               11.0f, 160, 15.0f, 0.55f};
}

bool room_full() { return s.parts.size() >= kMaxParticles; }

void add_paper(const Scene& sc, const Scale& k, const XrVector3f& p, const float v[3], bool streamer = false) {
  if (room_full()) return;
  Particle q{};
  q.p[0] = p.x; q.p[1] = p.y; q.p[2] = p.z;
  std::memcpy(q.v, v, sizeof(q.v));
  random_quat(q.q);
  random_unit(q.axis);
  q.kind = kKindPaper;
  q.shape = kPaper;
  q.mesh = streamer ? mesh::kStreamer : static_cast<uint8_t>(mesh::kPaperBent + irand(3));
  if (streamer) {
    // Long thin ribbons that drift down slowly, curling.
    q.size = k.streamer * frand(0.75f, 1.2f);
    q.aspect = frand(0.05f, 0.08f);
    q.spin = frand(1.5f, 4.0f);
    q.fallSpeed = k.paperFall * frand(0.4f, 0.6f);
    q.flutterAmp = k.flutter * frand(0.9f, 1.4f);
  } else {
    q.size = k.paper * frand(0.8f, 1.25f);
    q.aspect = frand(0.45f, 0.75f);
    q.spin = frand(3.0f, 9.0f);
    q.fallSpeed = k.paperFall * frand(0.75f, 1.2f);
    q.flutterAmp = k.flutter * frand(0.5f, 1.0f);
  }
  if (!streamer && irand(100) < 18) {
    // Metallic: gold or silver foil that glints as it turns.
    const bool gold = irand(2) == 0;
    q.shape = kMetal;
    q.c1 = pack(gold ? 0xFFD34A : 0xEDEEF5);
    q.c2 = pack(gold ? 0xC8901E : 0xA4A9B4);
  } else {
    const int a = irand(3), b = (a + 1 + irand(2)) % 3;
    q.c1 = pack(s.theme.paper[a]);
    q.c2 = pack(s.theme.paper[b]);
  }
  q.life = sc.mr ? 0.7f : 0.6f;  // r44: once landed it fades out in this long (step_particles)
  q.flutterPhase = frand(0.0f, 6.28f);
  q.flutterFreq = frand(1.5f, 3.2f);
  s.parts.push_back(q);
}

void add_piece(const Scene& sc, const Scale& k, const XrVector3f& p, const float v[3]) {
  if (room_full()) return;
  Particle q{};
  q.p[0] = p.x; q.p[1] = p.y; q.p[2] = p.z;
  std::memcpy(q.v, v, sizeof(q.v));
  random_quat(q.q);
  random_unit(q.axis);
  q.spin = frand(1.2f, 3.5f);
  q.size = k.piece * frand(0.85f, 1.15f);
  q.aspect = 1.0f;
  // Super Strikes mix gold stars in with the signature piece.
  const bool star = s.superStrike && irand(3) == 0;
  q.shape = star ? kStar : s.theme.piece;
  q.mesh = mesh_for(static_cast<Shape>(q.shape));
  q.c1 = pack(star ? 0xFFD21F : s.theme.pieceMain);
  q.c2 = pack(star ? 0xFFF2A0 : s.theme.pieceDetail);
  q.kind = kKindPiece;
  q.life = sc.mr ? 0.8f : 0.7f;  // r44: once landed it fades out in this long (step_particles)
  q.flutterPhase = frand(0.0f, 6.28f);
  q.flutterFreq = frand(0.8f, 1.6f);
  q.flutterAmp = k.flutter * 0.35f;
  q.fallSpeed = k.pieceFall * frand(0.85f, 1.15f);
  s.parts.push_back(q);
}

void add_spark(const XrVector3f& p, const float v[3], float size, uint32_t core, uint32_t outer, float life,
               float drag, float gravity) {
  if (room_full()) return;
  Particle q{};
  q.p[0] = p.x; q.p[1] = p.y; q.p[2] = p.z;
  std::memcpy(q.v, v, sizeof(q.v));
  q.size = size;
  q.aspect = 1.0f;
  q.c1 = pack(core);
  q.c2 = pack(outer);
  q.shape = kSpark;
  q.kind = kKindSpark;
  q.glow = 1.0f;
  q.life = life;
  q.drag = drag;
  q.gravity = gravity;
  s.parts.push_back(q);
}

// r55: an item blast's embers (State::Blast): fast at first, then real gravity; they bounce on the
// floor and the Tabletop field (step_particles) and fade. Plus a short bright flash where it went off.
void spawn_blast(const Scene& sc, const State::Blast& b) {
  const bool room = sc.mr;
  const float st = std::min(3.0f, std::max(1.0f, b.strength));
  const int n = static_cast<int>((room ? 22.0f : 40.0f) + (room ? 18.0f : 30.0f) * st);
  // Speeds and sizes in meters: a field on a table throws them off it and onto the floor; in the
  // stadium a life-size blast throws them across the pitch.
  const float speed = room ? 1.3f + 0.35f * st : 6.0f + 2.0f * st;
  const float size = room ? 0.012f : 0.09f;
  const bool dust = b.kind == 1;
  for (int i = 0; i < n && !room_full(); ++i) {
    float d[3];
    random_unit(d);
    d[0] += b.dir.x * 1.3f;
    d[1] += b.dir.y * 1.3f;
    d[2] += b.dir.z * 1.3f;
    const float len = std::max(1e-3f, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
    const float sp = speed * frand(0.45f, 1.0f) * (dust ? 0.6f : 1.0f);
    const float v[3] = {d[0] / len * sp, d[1] / len * sp, d[2] / len * sp};
    Particle q{};
    q.p[0] = b.at.x;
    q.p[1] = b.at.y;
    q.p[2] = b.at.z;
    std::memcpy(q.v, v, sizeof(q.v));
    q.size = size * frand(0.7f, 1.4f);
    q.aspect = 1.0f;
    if (dust) {
      q.c1 = pack(irand(2) ? 0xD8C8B0 : 0xB8A890);
      q.c2 = pack(0x6A5A48);
      q.glow = 0.15f;
    } else {
      q.c1 = pack(irand(3) == 0 ? 0xFFFFFF : 0xFFF0B0);
      q.c2 = pack(irand(2) ? 0xFF7A1A : 0xFF3A10);
      q.glow = 1.0f;
    }
    q.shape = kSpark;
    q.kind = kKindEmber;
    q.life = frand(1.1f, 2.0f) * (dust ? 0.8f : 1.0f);
    q.drag = dust ? 2.5f : 0.8f;
    q.gravity = 1.0f;
    s.parts.push_back(q);
  }
  if (!dust) {
    // The flash at the heart of it.
    const float v0[3] = {0.0f, 0.0f, 0.0f};
    add_spark(b.at, v0, (room ? 0.07f : 0.9f) * st, 0xFFFFFF, 0xFFB040, 0.16f, 0.0f, 0.0f);
  }
}

// A firework: sphere, ring, willow (long falling gold trails) or two-colour sphere.
void add_burst(const Scale& k, const XrVector3f& p) {
  const uint32_t main = s.theme.paper[irand(3)] == 0xFFFFFF ? s.theme.pieceMain : s.theme.paper[irand(3)];
  const uint32_t other = s.theme.paper[irand(3)];
  const Burst style = static_cast<Burst>(irand(4));
  float ringAxis[3];
  random_unit(ringAxis);
  const int n = style == kBurstWillow ? k.burstSparks * 3 / 4 : k.burstSparks;
  for (int i = 0; i < n && !room_full(); ++i) {
    float d[3];
    random_unit(d);
    if (style == kBurstRing) {
      // Flatten onto the plane across ringAxis.
      const float along = d[0] * ringAxis[0] + d[1] * ringAxis[1] + d[2] * ringAxis[2];
      for (int a = 0; a < 3; ++a) d[a] -= along * ringAxis[a];
      const float len = std::max(1e-3f, std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]));
      for (float& c : d) c /= len;
    }
    const float sp = k.burstSpeed * (style == kBurstRing ? frand(0.95f, 1.05f) : frand(0.6f, 1.0f));
    const float v[3] = {d[0] * sp, d[1] * sp, d[2] * sp};
    if (style == kBurstWillow) {
      add_spark(p, v, k.spark * frand(0.7f, 1.0f), 0xFFF4C0, 0xE09A20, frand(2.2f, 3.2f), 2.2f, 0.55f);
    } else {
      const uint32_t outer = (style == kBurstDouble && (i & 1)) ? other : main;
      add_spark(p, v, k.spark * frand(0.8f, 1.3f), 0xFFFFFF, outer, frand(1.1f, 1.8f), 1.3f, 0.25f);
    }
  }
}

void add_rocket(const Scale& k, const XrVector3f& from, float burstHeight, float speed) {
  if (room_full()) return;
  Particle q{};
  q.p[0] = from.x; q.p[1] = from.y; q.p[2] = from.z;
  q.v[0] = frand(-0.08f, 0.08f) * speed;
  q.v[1] = speed;
  q.v[2] = frand(-0.08f, 0.08f) * speed;
  q.size = k.spark * 1.4f;
  q.aspect = 1.0f;
  q.c1 = pack(0xFFFFFF);
  q.c2 = pack(0xFFC83A);
  q.shape = kSpark;
  q.kind = kKindRocket;
  q.glow = 1.0f;
  q.life = 6.0f;
  q.burstHeight = burstHeight;
  s.parts.push_back(q);
}

// Where the cannons and pyro jets stand: the four corners of the Tabletop field in the room,
// otherwise four spots around you (front left/right, back left/right).
int launch_points(const Scene& sc, XrVector3f out[4], XrVector3f outward[4]) {
  const XrVector3f head = head_of(sc);
  if (sc.tabletop && sc.mr) {
    for (int i = 0; i < 4; ++i) {
      const float gx = (i & 1) ? 21.0f : -21.0f, gy = (i & 2) ? 12.5f : -12.5f;
      out[i] = game_to_room(sc.tableScale, sc.tableYaw, sc.tablePos, gx, gy, 0.5f);
    }
  } else {
    const XrQuaternionf& o = sc.views[0].pose.orientation;
    float fwd[3] = {-(2 * (o.x * o.z + o.w * o.y)), 0.0f, -(1 - 2 * (o.x * o.x + o.y * o.y))};
    const float len = std::max(1e-3f, std::sqrt(fwd[0] * fwd[0] + fwd[2] * fwd[2]));
    fwd[0] /= len; fwd[2] /= len;
    const float right[3] = {-fwd[2], 0.0f, fwd[0]};
    const float side = sc.mr ? 0.9f : 4.5f, ahead = sc.mr ? 0.8f : 4.0f;
    const float y = sc.mr ? head.y - 0.6f : sc.floorY;
    for (int i = 0; i < 4; ++i) {
      const float sx = (i & 1) ? 1.0f : -1.0f, sz = (i & 2) ? -0.8f : 1.0f;
      out[i] = XrVector3f{head.x + right[0] * side * sx + fwd[0] * ahead * sz, y,
                          head.z + right[2] * side * sx + fwd[2] * ahead * sz};
    }
  }
  XrVector3f c{0.0f, 0.0f, 0.0f};
  for (int i = 0; i < 4; ++i) { c.x += out[i].x * 0.25f; c.z += out[i].z * 0.25f; }
  for (int i = 0; i < 4; ++i) {
    const float dx = out[i].x - c.x, dz = out[i].z - c.z;
    const float len = std::max(1e-3f, std::sqrt(dx * dx + dz * dz));
    outward[i] = XrVector3f{dx / len, 0.0f, dz / len};
  }
  return 4;
}

// Where the rain falls: a disc around you (around you and the table in Tabletop), from just under
// the ceiling in your room, high over the pitch in the stadium.
struct RainArea {
  XrVector3f centre;
  float top;
};
RainArea rain_area(const Scene& sc, const Scale& k) {
  const XrVector3f head = head_of(sc);
  RainArea r{head, sc.mr ? std::max(sc.floorY + 2.3f, head.y + 0.45f) : sc.floorY + k.rainHeight};
  if (sc.tabletop && sc.mr) {
    r.centre.x = 0.5f * (head.x + sc.tablePos.x);
    r.centre.z = 0.5f * (head.z + sc.tablePos.z);
  }
  return r;
}

// strikers-quest r44, a wave (Lance: "it shouldnt just pile up in random ass piles it should just fall
// from the sky if its confetti, same with the mushrooms and other items"): the cannons at the field's
// corners are gone. Each wave is a burst of the same confetti and pieces let go from the sky, spread
// over the whole rain disc and over a band of heights, so it falls as a cloud and nowhere in a heap.
void fire_cannon_wave(const Scene& sc, const Scale& k) {
  const RainArea area = rain_area(sc, k);
  const int papers = static_cast<int>(k.cannonPapers * s.intensity) * 2;  // the old four corners' worth,
  const int pieces = static_cast<int>(k.cannonPieces * s.intensity) * 2;  // halved: spread out it reads fuller
  const float band = sc.mr ? 0.6f : 6.0f;
  for (int m = 0; m < papers + pieces; ++m) {
    const float a = frand(0.0f, 6.2831853f), r = k.rainRadius * 1.15f * std::sqrt(frand(0.0f, 1.0f));
    const XrVector3f p{area.centre.x + std::cos(a) * r, area.top + frand(0.0f, band), area.centre.z + std::sin(a) * r};
    if (m < papers) {
      const float v[3] = {0.0f, -k.paperFall * frand(0.3f, 0.8f), 0.0f};
      add_paper(sc, k, p, v, irand(100) < (sc.mr ? 15 : 7));
    } else {
      const float v[3] = {0.0f, -k.pieceFall * frand(0.3f, 0.8f), 0.0f};
      add_piece(sc, k, p, v);
    }
  }
  ++s.cannonWave;
}

// r43d, the hat trick (Lance: "we get hats thrown on the field ... they can just land all over the
// field"): caps in the characters' colours with a white badge. strikers-quest r44: they come down
// from the sky like the rest (no more throws from the edges into one heap), anywhere on the
// life-size pitch, on the Tabletop field (small), or, with the big screen, on the floor all around
// the scorer, and fade soon after they land.
void throw_hat(const Scene& sc, const Scale& k) {
  if (room_full()) return;
  static const uint32_t kCapColours[] = {0xE4202A, 0x1FA83A, 0xFFD21F, 0x6B2FB3, 0x2F6FE0};
  const bool tableTop = sc.tabletop && sc.mr;
  const float size = tableTop ? 0.05f : (sc.mr ? 0.42f : 0.55f);
  const RainArea area = rain_area(sc, k);
  XrVector3f from;
  if (!sc.mr) {
    // Anywhere over the pitch (game units, as the game lays it out).
    from = game_to_room(sc.stadiumScale, 0.0f, sc.stadiumOrigin, frand(-20.0f, 20.0f), frand(-12.0f, 12.0f), 0.0f);
    from.y = area.top + frand(0.0f, 8.0f);
  } else if (tableTop) {
    from = game_to_room(sc.tableScale, sc.tableYaw, sc.tablePos, frand(-20.0f, 20.0f), frand(-12.0f, 12.0f), 0.0f);
    from.y = area.top + frand(0.0f, 0.3f);
  } else {
    const float ta = frand(0.0f, 6.2831853f), tr = 2.6f * std::sqrt(frand(0.0f, 1.0f));
    from = XrVector3f{s.hatCentre.x + std::cos(ta) * tr, area.top + frand(0.0f, 0.4f), s.hatCentre.z + std::sin(ta) * tr};
  }
  Particle q{};
  q.p[0] = from.x;
  q.p[1] = from.y;
  q.p[2] = from.z;
  q.v[1] = -k.pieceFall;
  random_quat(q.q);
  q.axis[0] = frand(-0.2f, 0.2f);  // spinning mostly flat, like a tossed cap
  q.axis[1] = 1.0f;
  q.axis[2] = frand(-0.2f, 0.2f);
  const float len = std::sqrt(q.axis[0] * q.axis[0] + 1.0f + q.axis[2] * q.axis[2]);
  for (float& a : q.axis) a /= len;
  q.spin = frand(2.0f, 5.0f);
  q.size = size;
  q.aspect = 1.0f;
  q.shape = kCap;
  q.mesh = mesh::kCap;
  const uint32_t main = irand(6) == 0 ? (s.theme.pieceMain == 0xFFFFFF ? s.theme.paper[0] : s.theme.pieceMain)
                                      : kCapColours[irand(5)];
  q.c1 = pack(main);
  q.c2 = pack(0xFFFFFF);
  q.kind = kKindPiece;
  q.flutterPhase = frand(0.0f, 6.28f);
  q.flutterFreq = frand(0.8f, 1.6f);
  q.flutterAmp = k.flutter * 0.35f;
  q.fallSpeed = k.pieceFall * (tableTop ? frand(0.8f, 1.0f) : frand(1.0f, 1.3f));
  q.life = 1.6f;  // once landed (step_particles)
  s.parts.push_back(q);
}

void launch_firework(const Scene& sc, const Scale& k) {
  if (sc.mr) {
    // Small ones from the field's corners (or around you) up to just under the ceiling.
    const XrVector3f head = head_of(sc);
    const float ceiling = std::max(sc.floorY + 2.3f, head.y + 0.45f);
    XrVector3f from[4], out[4];
    launch_points(sc, from, out);
    add_rocket(k, from[irand(4)], ceiling - frand(0.2f, 0.55f), 2.4f);
    return;
  }
  // Over the stands, all around the stadium.
  const float a = frand(0.0f, 6.2831853f), r = frand(38.0f, 55.0f);
  const XrVector3f from =
      game_to_room(sc.stadiumScale, 0.0f, sc.stadiumOrigin, std::cos(a) * r * 1.2f, std::sin(a) * r * 0.8f, 8.0f);
  add_rocket(k, from, sc.floorY + frand(26.0f, 46.0f), 26.0f);
}

// Pyro: columns of fire shooting up, at the field's corners in the room, along the stands' front in
// the stadium.
void fire_jets(const Scene& sc, const Scale& k, float dt) {
  const float rate = sc.mr ? 220.0f : 900.0f;  // sparks per second, all jets
  s.jetAccum += rate * dt;
  XrVector3f from[8];
  int n = 0;
  if (sc.mr) {
    XrVector3f out[4];
    n = launch_points(sc, from, out);
  } else {
    for (int i = 0; i < 8; ++i) {
      const float a = 6.2831853f * (i + 0.5f) / 8.0f;
      from[i] = game_to_room(sc.stadiumScale, 0.0f, sc.stadiumOrigin, std::cos(a) * 30.0f, std::sin(a) * 20.0f, 2.0f);
    }
    n = 8;
  }
  while (s.jetAccum >= 1.0f) {
    s.jetAccum -= 1.0f;
    const XrVector3f& p = from[irand(n)];
    const float sp = k.jetSpeed;
    const float v[3] = {frand(-0.08f, 0.08f) * sp, sp * frand(0.8f, 1.1f), frand(-0.08f, 0.08f) * sp};
    const uint32_t outer = irand(3) == 0 ? 0xFF3A10 : 0xFF8A1A;
    add_spark(p, v, k.jetSpark * frand(0.8f, 1.3f), 0xFFE9A0, outer, frand(0.5f, 0.8f), 1.0f, 0.6f);
  }
}

// The goal flash: a wash of the scorer's colour over the whole view, fading fast.
void add_flash(const Scene& sc) {
  if (room_full()) return;
  const XrVector3f head = head_of(sc);
  const XrQuaternionf& o = sc.views[0].pose.orientation;
  const float fwd[3] = {-(2 * (o.x * o.z + o.w * o.y)), -(2 * (o.y * o.z - o.w * o.x)),
                        -(1 - 2 * (o.x * o.x + o.y * o.y))};
  Particle q{};
  q.p[0] = head.x + fwd[0] * 0.3f;
  q.p[1] = head.y + fwd[1] * 0.3f;
  q.p[2] = head.z + fwd[2] * 0.3f;
  q.size = 3.0f;
  q.aspect = 1.0f;
  const uint32_t c = s.theme.pieceMain == 0xFFFFFF ? s.theme.paper[0] : s.theme.pieceMain;
  q.c1 = pack(c);
  q.c2 = pack(c);
  q.shape = kFlash;
  q.kind = kKindFlash;
  q.glow = 0.5f;
  q.life = 0.5f;
  s.parts.push_back(q);
}

void step_particles(const Scene& sc, const Scale& k, float dt) {
  const float g = sc.mr ? 1.6f : 9.8f;
  // The Tabletop field top, to land on.
  const float tableTop = sc.tablePos.y + 0.002f;
  const float cs = std::cos(sc.tableYaw), sn = std::sin(sc.tableYaw);
  const XrVector3f head = head_of(sc);
  size_t w = 0;
  for (size_t i = 0; i < s.parts.size(); ++i) {
    Particle& q = s.parts[i];
    q.age += dt;
    bool keep = true;
    if (q.kind == kKindFlash) {
      // Stays in front of you while it fades.
      const XrQuaternionf& o = sc.views[0].pose.orientation;
      q.p[0] = head.x - 0.3f * (2 * (o.x * o.z + o.w * o.y));
      q.p[1] = head.y - 0.3f * (2 * (o.y * o.z - o.w * o.x));
      q.p[2] = head.z - 0.3f * (1 - 2 * (o.x * o.x + o.y * o.y));
      keep = q.age < q.life;
    } else if (q.kind == kKindSpark) {
      q.v[1] -= g * q.gravity * dt;
      const float keepV = std::exp(-q.drag * dt);
      for (float& c : q.v) c *= keepV;
      for (int a = 0; a < 3; ++a) q.p[a] += q.v[a] * dt;
      keep = q.age < q.life;
    } else if (q.kind == kKindEmber) {
      // r55: real gravity (the confetti's is slowed), a little air drag, and a bounce on whatever is
      // under it: the Tabletop field's top while over the pitch, else the floor.
      q.v[1] -= 9.8f * q.gravity * dt;
      const float keepV = std::exp(-q.drag * dt);
      q.v[0] *= keepV;
      q.v[2] *= keepV;
      const float prevY = q.p[1];
      for (int a = 0; a < 3; ++a) q.p[a] += q.v[a] * dt;
      float ground = sc.floorY + 0.004f;
      if (sc.tabletop && sc.mr && prevY >= tableTop) {
        const float dx = (q.p[0] - sc.tablePos.x) / sc.tableScale, dz = (q.p[2] - sc.tablePos.z) / sc.tableScale;
        const float gx = dx * cs - dz * sn, gy = -(dx * sn + dz * cs);
        if (std::fabs(gx) < 23.0f && std::fabs(gy) < 14.0f) ground = tableTop;
      }
      if (q.p[1] < ground) {
        q.p[1] = ground;
        q.v[1] = q.v[1] < -0.3f ? -q.v[1] * 0.35f : 0.0f;
        q.v[0] *= 0.55f;
        q.v[2] *= 0.55f;
      }
      keep = q.age < q.life;
    } else if (q.kind == kKindRocket) {
      for (int a = 0; a < 3; ++a) q.p[a] += q.v[a] * dt;
      // A short trail behind the rocket.
      if (irand(2) == 0) {
        const float tv[3] = {frand(-0.1f, 0.1f) * k.burstSpeed * 0.1f, -0.05f * k.burstSpeed,
                             frand(-0.1f, 0.1f) * k.burstSpeed * 0.1f};
        add_spark(XrVector3f{q.p[0], q.p[1], q.p[2]}, tv, k.spark * 0.6f, 0xFFE0A0, 0xFF8A1A, 0.4f, 1.5f, 0.3f);
      }
      if (q.p[1] >= q.burstHeight || q.age > 3.0f) {
        add_burst(k, XrVector3f{q.p[0], q.p[1], q.p[2]});
        keep = false;
      }
    } else if (!q.landed) {
      // Falls at its own pace, drifting side to side, tumbling.
      const float settle = 1.0f - std::exp(-2.6f * dt);
      q.v[1] += (-q.fallSpeed - q.v[1]) * settle;
      if (q.v[1] > 0.0f && q.fallSpeed > 0.0f) q.v[1] -= g * dt;  // still rising (knocked off the scorer's head)
      const float t = q.age * q.flutterFreq + q.flutterPhase;
      const float tx = std::sin(t) * q.flutterAmp, tz = std::cos(t * 0.8f + 1.3f) * q.flutterAmp * 0.7f;
      const float side = 1.0f - std::exp(-1.6f * dt);
      q.v[0] += (tx - q.v[0]) * side;
      q.v[2] += (tz - q.v[2]) * side;
      const float prevX = q.p[0], prevY = q.p[1], prevZ = q.p[2];
      for (int a = 0; a < 3; ++a) q.p[a] += q.v[a] * dt;
      float dq[4];
      quat_axis_angle(q.axis, q.spin * dt, dq);
      float nq[4];
      quat_mul(dq, q.q, nq);
      std::memcpy(q.q, nq, sizeof(nq));
      // Come to rest the way the piece would: paper and coins flat, a mushroom standing, an egg on its
      // side, sitting on the surface rather than half through it; turned at random.
      const Rest rest = rest_for(q.shape);
      const float lift = rest.half * q.size;
      float landY = sc.floorY + 0.003f + lift;
      if (sc.tabletop && sc.mr && prevY >= tableTop + lift && q.p[1] < tableTop + lift) {
        // Over the field? Back into game units, where the pitch is a plain rectangle.
        const float dx = (q.p[0] - sc.tablePos.x) / sc.tableScale, dz = (q.p[2] - sc.tablePos.z) / sc.tableScale;
        const float gx = dx * cs - dz * sn, gy = -(dx * sn + dz * cs);
        if (std::fabs(gx) < 23.0f && std::fabs(gy) < 14.0f) landY = tableTop + lift;
      }
      // r42: the life-size scorer is solid. Over its head (while it stands) a piece settles there;
      // against its body it bounces off outward.
      bool onHead = false;
      if (sc.scorerValid) {
        const float R = sc.scorerRadius;
        const float ox = q.p[0] - sc.scorerFeet.x, oz = q.p[2] - sc.scorerFeet.z;
        const float r2 = ox * ox + oz * oz;
        const float top = sc.scorerFeet.y + sc.scorerHeight;
        if (r2 < R * R && q.p[1] < top + lift && q.p[1] > sc.scorerFeet.y) {
          if (sc.scorerStill && prevY >= top + lift - 0.03f && r2 < 0.7f * R * 0.7f * R) {
            landY = top + lift;
            onHead = true;
          } else {
            const float r = std::max(1e-3f, std::sqrt(r2));
            q.p[0] = sc.scorerFeet.x + ox / r * (R + 0.01f);
            q.p[2] = sc.scorerFeet.z + oz / r * (R + 0.01f);
            const float out = q.v[0] * ox / r + q.v[2] * oz / r;
            if (out < 0.0f) {  // moving inward: reflect, losing most of it
              q.v[0] -= 1.6f * out * ox / r;
              q.v[2] -= 1.6f * out * oz / r;
            }
            q.v[0] += 0.4f * ox / r;  // and a nudge clear of the body
            q.v[2] += 0.4f * oz / r;
          }
        }
      }
      // r44e (Lance: the pieces "should react to falling on" the banner too): it is a solid panel.
      // A piece that would pass through its face goes back out the side it came from, bouncing off;
      // one that comes down on its top edge stays there (and fades like everywhere else).
      bool onBanner = false;
      if (sc.bannerValid) {
        const float yawB = 2.0f * std::atan2(sc.bannerPose.orientation.y, sc.bannerPose.orientation.w);
        const float nx = std::sin(yawB), nz = std::cos(yawB);  // its face's normal, toward you
        const float rx = nz, rz = -nx;                          // along its width
        const XrVector3f& c = sc.bannerPose.position;
        const float hw = 0.5f * sc.bannerWidth, hh = 0.5f * sc.bannerHeight;
        const float lx = (q.p[0] - c.x) * rx + (q.p[2] - c.z) * rz;
        const float ly = q.p[1] - c.y;
        const float lz = (q.p[0] - c.x) * nx + (q.p[2] - c.z) * nz;
        const float prevLz = (prevX - c.x) * nx + (prevZ - c.z) * nz;
        if (std::fabs(lx) < hw) {
          if (prevY - c.y >= hh + lift - 0.02f && ly < hh + lift && std::fabs(lz) < 0.05f) {
            landY = c.y + hh + lift;
            onBanner = true;
            q.p[0] -= lz * nx;  // onto the edge itself
            q.p[2] -= lz * nz;
          } else if (std::fabs(ly) < hh && (lz > 0.0f) != (prevLz > 0.0f)) {
            const float side = prevLz > 0.0f ? 1.0f : -1.0f;
            q.p[0] += (side * 0.012f - lz) * nx;
            q.p[2] += (side * 0.012f - lz) * nz;
            const float vn = q.v[0] * nx + q.v[2] * nz;
            if (vn * side < 0.0f) {  // still going into it: reflected, losing most of it
              q.v[0] -= 1.6f * vn * nx;
              q.v[2] -= 1.6f * vn * nz;
            }
            q.v[0] += 0.3f * side * nx;  // and a nudge clear of it
            q.v[2] += 0.3f * side * nz;
          }
        }
      }
      if (q.p[1] <= landY || onHead || onBanner) {
        q.p[1] = landY;
        q.landed = true;
        q.onScorer = onHead;
        q.restOff[0] = q.p[0] - sc.scorerFeet.x;
        q.restOff[1] = q.p[2] - sc.scorerFeet.z;
        q.age = 0.0f;
        const float xAxis[3] = {1.0f, 0.0f, 0.0f}, yAxis[3] = {0.0f, 1.0f, 0.0f}, zAxis[3] = {0.0f, 0.0f, 1.0f};
        float lay[4] = {0.0f, 0.0f, 0.0f, 1.0f}, yaw[4];
        if (rest.up == 2) quat_axis_angle(xAxis, -1.5707963f, lay);
        else if (rest.up == 0) quat_axis_angle(zAxis, 1.5707963f, lay);
        quat_axis_angle(yAxis, frand(0.0f, 6.2831853f), yaw);
        quat_mul(yaw, lay, q.q);
      }
      keep = q.age < (q.maxAge > 0.0f ? q.maxAge : 25.0f) && q.p[1] > sc.floorY - 1.0f;
    } else if (q.onScorer) {
      // r42: rides on the scorer's head; once it runs (or is gone) the piece falls off.
      if (sc.scorerValid && sc.scorerStill) {
        const Rest rest = rest_for(q.shape);
        q.p[0] = sc.scorerFeet.x + q.restOff[0];
        q.p[1] = sc.scorerFeet.y + sc.scorerHeight + rest.half * q.size;
        q.p[2] = sc.scorerFeet.z + q.restOff[1];
      } else {
        q.onScorer = false;
        q.landed = false;
        q.v[0] = frand(-0.4f, 0.4f);
        q.v[1] = 0.6f;
        q.v[2] = frand(-0.4f, 0.4f);
      }
      keep = q.age < q.life;  // r44: fades there like on the floor, no heap on the head either
    } else {
      // r44: a landed piece only fades out where it fell (q.life is that fade, a fraction of a
      // second), so nothing builds up into heaps on the floor or the table.
      keep = q.age < q.life;
    }
    if (keep) s.parts[w++] = q;
  }
  s.parts.resize(w);
}

// ---------------------------------------------------------------------------------------------
// Vulkan
// ---------------------------------------------------------------------------------------------

bool vk_ok(VkResult r, const char* what) {
  if (r == VK_SUCCESS) return true;
  FXERR("fx: %s failed: VkResult %d", what, static_cast<int>(r));
  return false;
}

bool xr_ok(XrResult r, const char* what) {
  if (XR_SUCCEEDED(r)) return true;
  FXERR("fx: %s failed: XrResult %d", what, static_cast<int>(r));
  return false;
}

uint32_t memory_type(uint32_t bits, VkMemoryPropertyFlags want) {
  for (uint32_t i = 0; i < s.dev.memProps->memoryTypeCount; ++i) {
    if ((bits & (1u << i)) && (s.dev.memProps->memoryTypes[i].propertyFlags & want) == want) return i;
  }
  return UINT32_MAX;
}

bool make_host_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* buffer, VkDeviceMemory* memory,
                      void** mapped) {
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!vk_ok(vkCreateBuffer(s.dev.device, &info, nullptr, buffer), "vkCreateBuffer")) return false;
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(s.dev.device, *buffer, &req);
  const uint32_t type =
      memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (type == UINT32_MAX) return false;
  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = type;
  return vk_ok(vkAllocateMemory(s.dev.device, &alloc, nullptr, memory), "vkAllocateMemory") &&
         vk_ok(vkBindBufferMemory(s.dev.device, *buffer, *memory, 0), "vkBindBufferMemory") &&
         vk_ok(vkMapMemory(s.dev.device, *memory, 0, VK_WHOLE_SIZE, 0, mapped), "vkMapMemory");
}

VkShaderModule make_shader(const uint32_t* code, size_t bytes) {
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = bytes;
  info.pCode = code;
  VkShaderModule m = VK_NULL_HANDLE;
  vk_ok(vkCreateShaderModule(s.dev.device, &info, nullptr, &m), "vkCreateShaderModule");
  return m;
}

bool choose_format() {
  uint32_t count = 0;
  xrEnumerateSwapchainFormats(s.dev.session, 0, &count, nullptr);
  std::vector<int64_t> formats(count);
  if (count > 0) xrEnumerateSwapchainFormats(s.dev.session, count, &count, formats.data());
  for (VkFormat want : {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB}) {
    if (std::find(formats.begin(), formats.end(), static_cast<int64_t>(want)) != formats.end()) {
      s.colorFormat = want;
      return true;
    }
  }
  FXERR("fx: no sRGB swapchain format");
  return false;
}

bool make_render_pass() {
  VkAttachmentDescription att[2]{};
  att[0].format = s.colorFormat;
  att[0].samples = VK_SAMPLE_COUNT_1_BIT;
  att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // what OpenXR wants back
  att[1].format = VK_FORMAT_D16_UNORM;
  att[1].samples = VK_SAMPLE_COUNT_1_BIT;
  att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &colorRef;
  sub.pDepthStencilAttachment = &depthRef;
  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = 2;
  info.pAttachments = att;
  info.subpassCount = 1;
  info.pSubpasses = &sub;
  info.dependencyCount = 1;
  info.pDependencies = &dep;
  return vk_ok(vkCreateRenderPass(s.dev.device, &info, nullptr, &s.renderPass), "vkCreateRenderPass");
}

struct PushConstants {
  float viewProj[16];
  float camRight[4];
  float camUp[4];
  float camPos[4];  // the eye, for the 3D pieces' highlights
};

// r62: the room layer's (xr_fx_room.vert / .frag).
struct RoomPush {
  float mvp[16];
  float eye[4];    // the eye in the room's own space; w = seconds
  float light[4];  // the room's lamp colour
};

bool make_pipelines() {
  VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants)};
  VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &range;
  if (!vk_ok(vkCreatePipelineLayout(s.dev.device, &layoutInfo, nullptr, &s.layout), "vkCreatePipelineLayout")) {
    return false;
  }
  VkShaderModule fxVert = make_shader(kXrFxVert, sizeof(kXrFxVert));
  VkShaderModule fxFrag = make_shader(kXrFxFrag, sizeof(kXrFxFrag));
  VkShaderModule ocVert = make_shader(kXrFxOccluderVert, sizeof(kXrFxOccluderVert));
  VkShaderModule ocFrag = make_shader(kXrFxOccluderFrag, sizeof(kXrFxOccluderFrag));
  VkShaderModule meshVert = make_shader(kXrFxMeshVert, sizeof(kXrFxMeshVert));
  VkShaderModule meshFrag = make_shader(kXrFxMeshFrag, sizeof(kXrFxMeshFrag));
  if (!fxVert || !fxFrag || !ocVert || !ocFrag || !meshVert || !meshFrag) return false;

  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dyn;
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Effects: instanced quads, premultiplied alpha, tested against the stand-ins' depth.
  VkVertexInputBindingDescription fxBinding{0, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE};
  const VkVertexInputAttributeDescription fxAttrs[] = {
      {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, posSize)},
      {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, quat)},
      {2, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Instance, color1)},
      {3, 0, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Instance, color2)},
      {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, params)},
  };
  VkPipelineVertexInputStateCreateInfo fxInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  fxInput.vertexBindingDescriptionCount = 1;
  fxInput.pVertexBindingDescriptions = &fxBinding;
  fxInput.vertexAttributeDescriptionCount = 5;
  fxInput.pVertexAttributeDescriptions = fxAttrs;
  VkPipelineInputAssemblyStateCreateInfo strip{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  strip.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  VkPipelineDepthStencilStateCreateInfo fxDepth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  fxDepth.depthTestEnable = VK_TRUE;
  fxDepth.depthWriteEnable = VK_FALSE;
  fxDepth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState fxBlend{};
  fxBlend.blendEnable = VK_TRUE;
  fxBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
  fxBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  fxBlend.colorBlendOp = VK_BLEND_OP_ADD;
  fxBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  fxBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  fxBlend.alphaBlendOp = VK_BLEND_OP_ADD;
  fxBlend.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo fxBlendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  fxBlendState.attachmentCount = 1;
  fxBlendState.pAttachments = &fxBlend;
  VkPipelineShaderStageCreateInfo fxStages[2]{};
  fxStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  fxStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  fxStages[0].module = fxVert;
  fxStages[0].pName = "main";
  fxStages[1] = fxStages[0];
  fxStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fxStages[1].module = fxFrag;
  VkGraphicsPipelineCreateInfo fxInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  fxInfo.stageCount = 2;
  fxInfo.pStages = fxStages;
  fxInfo.pVertexInputState = &fxInput;
  fxInfo.pInputAssemblyState = &strip;
  fxInfo.pViewportState = &viewport;
  fxInfo.pRasterizationState = &raster;
  fxInfo.pMultisampleState = &ms;
  fxInfo.pDepthStencilState = &fxDepth;
  fxInfo.pColorBlendState = &fxBlendState;
  fxInfo.pDynamicState = &dynamic;
  fxInfo.layout = s.layout;
  fxInfo.renderPass = s.renderPass;

  // Stand-ins: plain triangles, depth only.
  VkVertexInputBindingDescription ocBinding{0, sizeof(float) * 3, VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription ocAttr{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  VkPipelineVertexInputStateCreateInfo ocInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  ocInput.vertexBindingDescriptionCount = 1;
  ocInput.pVertexBindingDescriptions = &ocBinding;
  ocInput.vertexAttributeDescriptionCount = 1;
  ocInput.pVertexAttributeDescriptions = &ocAttr;
  VkPipelineInputAssemblyStateCreateInfo list{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  list.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineDepthStencilStateCreateInfo ocDepth = fxDepth;
  ocDepth.depthWriteEnable = VK_TRUE;
  ocDepth.depthCompareOp = VK_COMPARE_OP_LESS;
  VkPipelineColorBlendAttachmentState ocBlend{};
  ocBlend.colorWriteMask = 0;
  VkPipelineColorBlendStateCreateInfo ocBlendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  ocBlendState.attachmentCount = 1;
  ocBlendState.pAttachments = &ocBlend;
  VkPipelineShaderStageCreateInfo ocStages[2] = {fxStages[0], fxStages[1]};
  ocStages[0].module = ocVert;
  ocStages[1].module = ocFrag;
  VkGraphicsPipelineCreateInfo ocInfo = fxInfo;
  ocInfo.pStages = ocStages;
  ocInfo.pVertexInputState = &ocInput;
  ocInfo.pInputAssemblyState = &list;
  ocInfo.pDepthStencilState = &ocDepth;
  ocInfo.pColorBlendState = &ocBlendState;

  // r61: the cup suite's walls and things as the same depth-only stand-in, straight from the room's
  // vertices (Room::verts), so effects behind them stay hidden.
  VkVertexInputBindingDescription roomBinding{0, sizeof(float) * Room::kRoomFloats, VK_VERTEX_INPUT_RATE_VERTEX};
  VkPipelineVertexInputStateCreateInfo roomDepthInput = ocInput;
  roomDepthInput.pVertexBindingDescriptions = &roomBinding;
  VkGraphicsPipelineCreateInfo roomDepthInfo = ocInfo;
  roomDepthInfo.pVertexInputState = &roomDepthInput;

  // The player marker draws over everything: it must read even behind other players or the fence.
  VkPipelineDepthStencilStateCreateInfo markerDepth = fxDepth;
  markerDepth.depthTestEnable = VK_FALSE;
  VkGraphicsPipelineCreateInfo markerInfo = fxInfo;
  markerInfo.pDepthStencilState = &markerDepth;

  // The 3D pieces: mesh vertices (binding 0) times the same instances (binding 1), solid, so they
  // write depth and hide each other and their own far side. Both faces drawn (paper is one sheet).
  const VkVertexInputBindingDescription meshBindings[] = {
      {0, sizeof(mesh::Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE},
  };
  const VkVertexInputAttributeDescription meshAttrs[] = {
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::Vertex, pos)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::Vertex, wid)},
      {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(mesh::Vertex, nrm)},
      {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(mesh::Vertex, mat)},
      {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, posSize)},
      {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, quat)},
      {6, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Instance, color1)},
      {7, 1, VK_FORMAT_R8G8B8A8_UNORM, offsetof(Instance, color2)},
      {8, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, params)},
  };
  VkPipelineVertexInputStateCreateInfo meshInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  meshInput.vertexBindingDescriptionCount = 2;
  meshInput.pVertexBindingDescriptions = meshBindings;
  meshInput.vertexAttributeDescriptionCount = 9;
  meshInput.pVertexAttributeDescriptions = meshAttrs;
  VkPipelineDepthStencilStateCreateInfo meshDepth = fxDepth;
  meshDepth.depthWriteEnable = VK_TRUE;
  VkPipelineShaderStageCreateInfo meshStages[2] = {fxStages[0], fxStages[1]};
  meshStages[0].module = meshVert;
  meshStages[1].module = meshFrag;
  VkGraphicsPipelineCreateInfo meshInfo = fxInfo;
  meshInfo.pStages = meshStages;
  meshInfo.pVertexInputState = &meshInput;
  meshInfo.pInputAssemblyState = &list;
  meshInfo.pDepthStencilState = &meshDepth;

  const bool ok =
      vk_ok(vkCreateGraphicsPipelines(s.dev.device, VK_NULL_HANDLE, 1, &fxInfo, nullptr, &s.fxPipeline),
            "vkCreateGraphicsPipelines(fx)") &&
      vk_ok(vkCreateGraphicsPipelines(s.dev.device, VK_NULL_HANDLE, 1, &meshInfo, nullptr, &s.meshPipeline),
            "vkCreateGraphicsPipelines(mesh)") &&
      vk_ok(vkCreateGraphicsPipelines(s.dev.device, VK_NULL_HANDLE, 1, &markerInfo, nullptr, &s.markerPipeline),
            "vkCreateGraphicsPipelines(marker)") &&
      vk_ok(vkCreateGraphicsPipelines(s.dev.device, VK_NULL_HANDLE, 1, &ocInfo, nullptr, &s.occluderPipeline),
            "vkCreateGraphicsPipelines(occluder)") &&
      vk_ok(vkCreateGraphicsPipelines(s.dev.device, VK_NULL_HANDLE, 1, &roomDepthInfo, nullptr, &s.roomDepthPipeline),
            "vkCreateGraphicsPipelines(room depth)");
  vkDestroyShaderModule(s.dev.device, fxVert, nullptr);
  vkDestroyShaderModule(s.dev.device, fxFrag, nullptr);
  vkDestroyShaderModule(s.dev.device, ocVert, nullptr);
  vkDestroyShaderModule(s.dev.device, ocFrag, nullptr);
  vkDestroyShaderModule(s.dev.device, meshVert, nullptr);
  vkDestroyShaderModule(s.dev.device, meshFrag, nullptr);
  return ok;
}

// The pieces' meshes at two levels of detail, built once into two host-visible buffers.
bool make_meshes() {
  mesh::Set set = mesh::build(1.0f);
  const mesh::Set far = mesh::build(0.4f);
  std::memcpy(s.meshRanges[0], set.ranges, sizeof(s.meshRanges[0]));
  const uint32_t vbase = static_cast<uint32_t>(set.verts.size());
  const uint32_t ibase = static_cast<uint32_t>(set.indices.size());
  set.verts.insert(set.verts.end(), far.verts.begin(), far.verts.end());
  for (uint16_t i : far.indices) set.indices.push_back(static_cast<uint16_t>(vbase + i));
  for (uint32_t m = 0; m < mesh::kCount; ++m) {
    s.meshRanges[1][m] = mesh::Range{far.ranges[m].firstIndex + ibase, far.ranges[m].indexCount};
  }
  if (set.verts.size() > 65535) {
    FXERR("fx: piece meshes too large (%zu vertices)", set.verts.size());
    return false;
  }
  void* mapped = nullptr;
  const VkDeviceSize vbytes = sizeof(mesh::Vertex) * set.verts.size();
  const VkDeviceSize ibytes = sizeof(uint16_t) * set.indices.size();
  if (!make_host_buffer(vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &s.meshVerts, &s.meshVertMemory, &mapped)) {
    return false;
  }
  std::memcpy(mapped, set.verts.data(), vbytes);
  vkUnmapMemory(s.dev.device, s.meshVertMemory);
  if (!make_host_buffer(ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &s.meshIndices, &s.meshIndexMemory, &mapped)) {
    return false;
  }
  std::memcpy(mapped, set.indices.data(), ibytes);
  vkUnmapMemory(s.dev.device, s.meshIndexMemory);
  FXLOG("fx: piece meshes ready (%zu vertices, %zu triangles)", set.verts.size(), set.indices.size() / 3);
  return true;
}

// An image only the GPU's tiles ever hold (depth, multisampled colour): lazily allocated where the
// device has that, so it takes no memory on a tiler.
bool make_transient(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect, uint32_t width,
                    uint32_t height, VkSampleCountFlagBits samples, VkImage* image, VkDeviceMemory* memory,
                    VkImageView* view) {
  VkImageCreateInfo di{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  di.imageType = VK_IMAGE_TYPE_2D;
  di.format = format;
  di.extent = {width, height, 1};
  di.mipLevels = 1;
  di.arrayLayers = 1;
  di.samples = samples;
  di.tiling = VK_IMAGE_TILING_OPTIMAL;
  di.usage = usage | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
  di.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!vk_ok(vkCreateImage(s.dev.device, &di, nullptr, image), "vkCreateImage(fx attachment)")) return false;
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(s.dev.device, *image, &req);
  uint32_t type = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT);
  if (type == UINT32_MAX) type = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == UINT32_MAX) return false;
  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = type;
  if (!vk_ok(vkAllocateMemory(s.dev.device, &alloc, nullptr, memory), "vkAllocateMemory(fx attachment)") ||
      !vk_ok(vkBindImageMemory(s.dev.device, *image, *memory, 0), "vkBindImageMemory(fx attachment)")) {
    return false;
  }
  VkImageViewCreateInfo dv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  dv.image = *image;
  dv.viewType = VK_IMAGE_VIEW_TYPE_2D;
  dv.format = format;
  dv.subresourceRange = {aspect, 0, 1, 0, 1};
  return vk_ok(vkCreateImageView(s.dev.device, &dv, nullptr, view), "vkCreateImageView(fx attachment)");
}

// An eye's swapchain and what it draws with. Single-sampled: colour straight into the swapchain image
// and a depth (s.renderPass). r62, multisampled (the room layer): a multisampled colour and depth,
// resolved into the swapchain image (`pass`, make_room_render_pass).
bool make_eye(Eye& eye, uint32_t width, uint32_t height, VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
              VkRenderPass pass = VK_NULL_HANDLE) {
  XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
  info.format = s.colorFormat;
  info.sampleCount = 1;
  info.width = width;
  info.height = height;
  info.faceCount = 1;
  info.arraySize = 1;
  info.mipCount = 1;
  if (!xr_ok(xrCreateSwapchain(s.dev.session, &info, &eye.swapchain), "xrCreateSwapchain(fx)")) return false;
  uint32_t count = 0;
  xrEnumerateSwapchainImages(eye.swapchain, 0, &count, nullptr);
  eye.images.assign(count, XrSwapchainImageVulkanKHR{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
  if (!xr_ok(xrEnumerateSwapchainImages(eye.swapchain, count, &count,
                                        reinterpret_cast<XrSwapchainImageBaseHeader*>(eye.images.data())),
             "xrEnumerateSwapchainImages(fx)")) {
    return false;
  }
  const bool multi = samples != VK_SAMPLE_COUNT_1_BIT;
  if (!make_transient(VK_FORMAT_D16_UNORM, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, width,
                      height, samples, &eye.depth, &eye.depthMemory, &eye.depthView) ||
      (multi && !make_transient(s.colorFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, width,
                                height, samples, &eye.msaa, &eye.msaaMemory, &eye.msaaView))) {
    return false;
  }

  for (const auto& img : eye.images) {
    VkImageViewCreateInfo cv{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    cv.image = img.image;
    cv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    cv.format = s.colorFormat;
    cv.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    if (!vk_ok(vkCreateImageView(s.dev.device, &cv, nullptr, &view), "vkCreateImageView(fx)")) return false;
    eye.views.push_back(view);
    const VkImageView single[2] = {view, eye.depthView};
    const VkImageView resolved[3] = {eye.msaaView, eye.depthView, view};
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = multi ? pass : s.renderPass;
    fb.attachmentCount = multi ? 3 : 2;
    fb.pAttachments = multi ? resolved : single;
    fb.width = width;
    fb.height = height;
    fb.layers = 1;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    if (!vk_ok(vkCreateFramebuffer(s.dev.device, &fb, nullptr, &framebuffer), "vkCreateFramebuffer(fx)")) {
      return false;
    }
    eye.framebuffers.push_back(framebuffer);
  }
  return true;
}

// r62: the room layer's render pass when multisampled: colour and depth multisampled (cleared, never
// stored: they stay in the tiles), the colour resolved into the swapchain image.
bool make_room_render_pass(VkSampleCountFlagBits samples) {
  VkAttachmentDescription att[3]{};
  att[0].format = s.colorFormat;
  att[0].samples = samples;
  att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  att[1] = att[0];
  att[1].format = VK_FORMAT_D16_UNORM;
  att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  att[2] = att[0];
  att[2].samples = VK_SAMPLE_COUNT_1_BIT;
  att[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkAttachmentReference resolveRef{2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sub{};
  sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sub.colorAttachmentCount = 1;
  sub.pColorAttachments = &colorRef;
  sub.pResolveAttachments = &resolveRef;
  sub.pDepthStencilAttachment = &depthRef;
  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = 3;
  info.pAttachments = att;
  info.subpassCount = 1;
  info.pSubpasses = &sub;
  info.dependencyCount = 1;
  info.pDependencies = &dep;
  return vk_ok(vkCreateRenderPass(s.dev.device, &info, nullptr, &s.roomRenderPass), "vkCreateRenderPass(room)");
}

bool make_slots() {
  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = s.dev.queueFamily;
  if (!vk_ok(vkCreateCommandPool(s.dev.device, &poolInfo, nullptr, &s.pool), "vkCreateCommandPool(fx)")) return false;
  for (Slot& slot : s.slots) {
    if (!make_host_buffer(sizeof(Instance) * kMaxParticles, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &slot.instances,
                          &slot.instanceMemory, &slot.instanceMapped) ||
        !make_host_buffer(sizeof(float) * 3 * kMaxOccluderVerts, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &slot.occluders,
                          &slot.occluderMemory, &slot.occluderMapped)) {
      return false;
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = s.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!vk_ok(vkAllocateCommandBuffers(s.dev.device, &alloc, &slot.cmd), "vkAllocateCommandBuffers(fx)") ||
        !vk_ok(vkCreateFence(s.dev.device, &fence, nullptr, &slot.fence), "vkCreateFence(fx)")) {
      return false;
    }
  }
  return true;
}

// Column-major 4x4 (GLSL's layout).
void mat_mul(const float a[16], const float b[16], float out[16]) {
  float r[16];
  for (int c = 0; c < 4; ++c)
    for (int rr = 0; rr < 4; ++rr) {
      float v = 0.0f;
      for (int k = 0; k < 4; ++k) v += a[k * 4 + rr] * b[c * 4 + k];
      r[c * 4 + rr] = v;
    }
  std::memcpy(out, r, sizeof(r));
}

// Vulkan clip space (y down, z 0..1) from an OpenXR field of view, as the OpenXR SDK's
// XrMatrix4x4f_CreateProjectionFov does for GRAPHICS_VULKAN.
void projection(const XrFovf& fov, float n, float f, float out[16]) {
  const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
  const float u = std::tan(fov.angleUp), d = std::tan(fov.angleDown);
  const float w = r - l, h = d - u;
  std::memset(out, 0, sizeof(float) * 16);
  out[0] = 2.0f / w;
  out[8] = (r + l) / w;
  out[5] = 2.0f / h;
  out[9] = (u + d) / h;
  out[10] = -f / (f - n);
  out[14] = -(f * n) / (f - n);
  out[11] = -1.0f;
}

void rotate(const XrQuaternionf& q, const float v[3], float out[3]) {
  const float t[3] = {2 * (q.y * v[2] - q.z * v[1]), 2 * (q.z * v[0] - q.x * v[2]), 2 * (q.x * v[1] - q.y * v[0])};
  out[0] = v[0] + q.w * t[0] + (q.y * t[2] - q.z * t[1]);
  out[1] = v[1] + q.w * t[1] + (q.z * t[0] - q.x * t[2]);
  out[2] = v[2] + q.w * t[2] + (q.x * t[1] - q.y * t[0]);
}

// LOCAL -> eye (inverse of the pose), column-major.
void view_matrix(const XrPosef& pose, float out[16]) {
  const float ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0}, ez[3] = {0, 0, 1};
  float rx[3], ry[3], rz[3];
  rotate(pose.orientation, ex, rx);
  rotate(pose.orientation, ey, ry);
  rotate(pose.orientation, ez, rz);
  const float p[3] = {pose.position.x, pose.position.y, pose.position.z};
  // Rows of the inverse rotation are the rotated axes.
  out[0] = rx[0]; out[4] = rx[1]; out[8] = rx[2];  out[12] = -(rx[0] * p[0] + rx[1] * p[1] + rx[2] * p[2]);
  out[1] = ry[0]; out[5] = ry[1]; out[9] = ry[2];  out[13] = -(ry[0] * p[0] + ry[1] * p[1] + ry[2] * p[2]);
  out[2] = rz[0]; out[6] = rz[1]; out[10] = rz[2]; out[14] = -(rz[0] * p[0] + rz[1] * p[1] + rz[2] * p[2]);
  out[3] = 0;     out[7] = 0;     out[11] = 0;     out[15] = 1;
}

// The stand-ins: the Tabletop field as a box up to the fence top, and the screen.
uint32_t write_occluders(const Scene& sc, float* out) {
  uint32_t n = 0;
  auto tri = [&](const XrVector3f& a, const XrVector3f& b, const XrVector3f& c) {
    if (n + 3 > kMaxOccluderVerts) return;
    for (const XrVector3f* v : {&a, &b, &c}) {
      out[n * 3 + 0] = v->x;
      out[n * 3 + 1] = v->y;
      out[n * 3 + 2] = v->z;
      ++n;
    }
  };
  if (sc.tabletop) {
    XrVector3f c[8];
    for (int i = 0; i < 8; ++i) {
      c[i] = game_to_room(sc.tableScale, sc.tableYaw, sc.tablePos, (i & 1) ? 22.5f : -22.5f, (i & 2) ? 13.5f : -13.5f,
                          (i & 4) ? 2.0f : -0.2f);
    }
    const int faces[6][4] = {{0, 1, 3, 2}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 3, 7, 5}};
    for (const auto& f : faces) {
      tri(c[f[0]], c[f[1]], c[f[2]]);
      tri(c[f[0]], c[f[2]], c[f[3]]);
    }
  }
  if (sc.screenValid) {
    const float hw = 0.5f * sc.screenWidth, hh = 0.5f * sc.screenHeight;
    const float corners[4][3] = {{-hw, -hh, 0}, {hw, -hh, 0}, {hw, hh, 0}, {-hw, hh, 0}};
    XrVector3f w[4];
    for (int i = 0; i < 4; ++i) {
      float r[3];
      rotate(sc.screenPose.orientation, corners[i], r);
      w[i] = XrVector3f{r[0] + sc.screenPose.position.x, r[1] + sc.screenPose.position.y,
                        r[2] + sc.screenPose.position.z};
    }
    tri(w[0], w[1], w[2]);
    tri(w[0], w[2], w[3]);
  }
  // r44e (Lance: the celebration pieces "should not go through them even appearance wise"): the
  // life-size scorer as a ten-sided column a little inside the one the pieces bounce off, and the
  // goal banner, so a piece behind either is hidden by it instead of drawn over it.
  if (sc.scorerValid) {
    constexpr int kSides = 10;
    const float r = 0.9f * sc.scorerRadius;
    const float y0 = sc.scorerFeet.y, y1 = sc.scorerFeet.y + sc.scorerHeight;
    XrVector3f ring0[kSides], ring1[kSides];
    for (int i = 0; i < kSides; ++i) {
      const float a = 6.2831853f * i / kSides;
      const float x = sc.scorerFeet.x + r * std::cos(a), z = sc.scorerFeet.z + r * std::sin(a);
      ring0[i] = XrVector3f{x, y0, z};
      ring1[i] = XrVector3f{x, y1, z};
    }
    const XrVector3f top{sc.scorerFeet.x, y1, sc.scorerFeet.z};
    for (int i = 0; i < kSides; ++i) {
      const int j = (i + 1) % kSides;
      tri(ring0[i], ring0[j], ring1[j]);
      tri(ring0[i], ring1[j], ring1[i]);
      tri(top, ring1[i], ring1[j]);
    }
  }
  if (sc.bannerValid) {
    const float hw = 0.5f * sc.bannerWidth, hh = 0.5f * sc.bannerHeight;
    const float corners[4][3] = {{-hw, -hh, 0}, {hw, -hh, 0}, {hw, hh, 0}, {-hw, hh, 0}};
    XrVector3f w[4];
    for (int i = 0; i < 4; ++i) {
      float r[3];
      rotate(sc.bannerPose.orientation, corners[i], r);
      w[i] = XrVector3f{r[0] + sc.bannerPose.position.x, r[1] + sc.bannerPose.position.y,
                        r[2] + sc.bannerPose.position.z};
    }
    tri(w[0], w[1], w[2]);
    tri(w[0], w[2], w[3]);
  }
  // r61: the cup suite's boards and name plaque.
  for (int p = 0; p < sc.panelCount && p < 4; ++p) {
    const Scene::Panel& pn = sc.panels[p];
    const float hw = 0.5f * pn.width, hh = 0.5f * pn.height;
    const float corners[4][3] = {{-hw, -hh, 0}, {hw, -hh, 0}, {hw, hh, 0}, {-hw, hh, 0}};
    XrVector3f w[4];
    for (int i = 0; i < 4; ++i) {
      float r[3];
      rotate(pn.pose.orientation, corners[i], r);
      w[i] = XrVector3f{r[0] + pn.pose.position.x, r[1] + pn.pose.position.y, r[2] + pn.pose.position.z};
    }
    tri(w[0], w[1], w[2]);
    tri(w[0], w[2], w[3]);
  }
  return n;
}

// r61: the cup suite's place in your room as a matrix (column-major): its own (x, y up from the
// floor, z) turned by roomYaw about +Y, then moved to roomOrigin.
void room_model(const Scene& sc, float out[16]) {
  const float c = std::cos(sc.roomYaw), sn = std::sin(sc.roomYaw);
  std::memset(out, 0, sizeof(float) * 16);
  out[0] = c;
  out[2] = -sn;
  out[5] = 1.0f;
  out[8] = sn;
  out[10] = c;
  out[12] = sc.roomOrigin.x;
  out[13] = sc.roomOrigin.y;
  out[14] = sc.roomOrigin.z;
  out[15] = 1.0f;
}

// A point of the suite (meters: x right, y up from its floor, z back toward you, as the room's
// vertices are) where it is in your room (LOCAL).
XrVector3f suite_to_room(const Scene& sc, float x, float y, float z) {
  const float c = std::cos(sc.roomYaw), sn = std::sin(sc.roomYaw);
  return XrVector3f{sc.roomOrigin.x + x * c + z * sn, sc.roomOrigin.y + y, sc.roomOrigin.z - x * sn + z * c};
}

// --- r61: the walkable suite's room on the GPU ---

// Waits out every frame still being drawn (the effects' and the room's), so what they use can go.
void wait_frames() {
  for (Slot* ring : {s.slots, s.roomSlots}) {
    for (uint32_t i = 0; i < kSlots; ++i) {
      Slot& slot = ring[i];
      if (!slot.pending) continue;
      vkWaitForFences(s.dev.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
      vkResetFences(s.dev.device, 1, &slot.fence);
      slot.pending = false;
    }
  }
}

void destroy_buffer(VkBuffer& b, VkDeviceMemory& m) {
  if (b) vkDestroyBuffer(s.dev.device, b, nullptr);
  if (m) vkFreeMemory(s.dev.device, m, nullptr);
  b = VK_NULL_HANDLE;
  m = VK_NULL_HANDLE;
}

void destroy_tex(State::Tex& t) {
  if (t.view) vkDestroyImageView(s.dev.device, t.view, nullptr);
  if (t.image) vkDestroyImage(s.dev.device, t.image, nullptr);
  if (t.memory) vkFreeMemory(s.dev.device, t.memory, nullptr);
  t = State::Tex{};
}

// A host-visible buffer holding a copy of `data`.
bool make_filled_buffer(const void* data, VkDeviceSize bytes, VkBufferUsageFlags usage, VkBuffer* b, VkDeviceMemory* m) {
  void* mapped = nullptr;
  if (!make_host_buffer(bytes, usage, b, m, &mapped)) return false;
  std::memcpy(mapped, data, bytes);
  vkUnmapMemory(s.dev.device, *m);
  return true;
}

// A picture as an sRGB texture with its whole chain of mipmaps (each level filtered on the GPU from
// the one above), bound to `set`. Waits for the upload.
bool make_tex(State::Tex& t, VkDescriptorSet set, const uint8_t* rgba, uint32_t w, uint32_t h) {
  uint32_t levels = 1;
  while ((std::max(w, h) >> levels) > 0) ++levels;
  VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = VK_FORMAT_R8G8B8A8_SRGB;
  ii.extent = {w, h, 1};
  ii.mipLevels = levels;
  ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!vk_ok(vkCreateImage(s.dev.device, &ii, nullptr, &t.image), "vkCreateImage(room)")) return false;
  VkMemoryRequirements req{};
  vkGetImageMemoryRequirements(s.dev.device, t.image, &req);
  const uint32_t type = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (type == UINT32_MAX) return false;
  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = type;
  if (!vk_ok(vkAllocateMemory(s.dev.device, &alloc, nullptr, &t.memory), "vkAllocateMemory(room)") ||
      !vk_ok(vkBindImageMemory(s.dev.device, t.image, t.memory, 0), "vkBindImageMemory(room)")) {
    return false;
  }
  VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  vi.image = t.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = VK_FORMAT_R8G8B8A8_SRGB;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
  if (!vk_ok(vkCreateImageView(s.dev.device, &vi, nullptr, &t.view), "vkCreateImageView(room)")) return false;

  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  if (!make_filled_buffer(rgba, VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, &staging, &stagingMemory)) {
    destroy_buffer(staging, stagingMemory);
    return false;
  }
  VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  ca.commandPool = s.pool;
  ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ca.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  bool ok = vk_ok(vkAllocateCommandBuffers(s.dev.device, &ca, &cmd), "vkAllocateCommandBuffers(room)") &&
            vk_ok(vkCreateFence(s.dev.device, &fi, nullptr, &fence), "vkCreateFence(room)");
  if (ok) {
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    auto barrier = [&](uint32_t level, uint32_t count, VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
                       VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
      VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
      b.srcAccessMask = srcAccess;
      b.dstAccessMask = dstAccess;
      b.oldLayout = from;
      b.newLayout = to;
      b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      b.image = t.image;
      b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level, count, 0, 1};
      vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
    };
    barrier(0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    for (uint32_t i = 1; i < levels; ++i) {
      barrier(i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
              VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_TRANSFER_BIT);
      VkImageBlit blit{};
      blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
      blit.srcOffsets[1] = {static_cast<int32_t>(std::max(1u, w >> (i - 1))), static_cast<int32_t>(std::max(1u, h >> (i - 1))), 1};
      blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
      blit.dstOffsets[1] = {static_cast<int32_t>(std::max(1u, w >> i)), static_cast<int32_t>(std::max(1u, h >> i)), 1};
      vkCmdBlitImage(cmd, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                     &blit, VK_FILTER_LINEAR);
      barrier(i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
              VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    }
    barrier(levels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    ok = vk_ok(vkQueueSubmit(s.dev.queue, 1, &submit, fence), "vkQueueSubmit(room)") &&
         vk_ok(vkWaitForFences(s.dev.device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences(room)");
  }
  if (fence) vkDestroyFence(s.dev.device, fence, nullptr);
  if (cmd) vkFreeCommandBuffers(s.dev.device, s.pool, 1, &cmd);
  destroy_buffer(staging, stagingMemory);
  if (!ok) return false;
  VkDescriptorImageInfo di{s.sampler, t.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
  wr.dstSet = set;
  wr.dstBinding = 0;
  wr.descriptorCount = 1;
  wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  wr.pImageInfo = &di;
  vkUpdateDescriptorSets(s.dev.device, 1, &wr, 0, nullptr);
  return true;
}

// The room layer's own things, made the first time a suite shows: the textured pipeline, the sky
// dome, full-size eye images, command buffers.
bool make_room_layer() {
  if (s.roomLayerTried) return s.roomLayerReady;
  s.roomLayerTried = true;
  const uint32_t w = s.dev.roomWidth > 0 ? s.dev.roomWidth : s.dev.width;
  const uint32_t h = s.dev.roomHeight > 0 ? s.dev.roomHeight : s.dev.height;

  VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  li.bindingCount = 1;
  li.pBindings = &binding;
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
  VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  pi.maxSets = 2;
  pi.poolSizeCount = 1;
  pi.pPoolSizes = &ps;
  if (!vk_ok(vkCreateDescriptorSetLayout(s.dev.device, &li, nullptr, &s.texSetLayout), "vkCreateDescriptorSetLayout(room)") ||
      !vk_ok(vkCreateDescriptorPool(s.dev.device, &pi, nullptr, &s.texPool), "vkCreateDescriptorPool(room)")) {
    return false;
  }
  const VkDescriptorSetLayout layouts[2] = {s.texSetLayout, s.texSetLayout};
  VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  ai.descriptorPool = s.texPool;
  ai.descriptorSetCount = 2;
  ai.pSetLayouts = layouts;
  VkDescriptorSet sets[2] = {};
  if (!vk_ok(vkAllocateDescriptorSets(s.dev.device, &ai, sets), "vkAllocateDescriptorSets(room)")) return false;
  s.roomSet = sets[0];
  s.outsideSet = sets[1];
  // Trilinear, and anisotropic where the device has it on (the floor seen at a slant stays sharp).
  VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
  si.magFilter = VK_FILTER_LINEAR;
  si.minFilter = VK_FILTER_LINEAR;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.maxLod = VK_LOD_CLAMP_NONE;
  si.anisotropyEnable = s.dev.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
  si.maxAnisotropy = std::min(8.0f, std::max(1.0f, s.dev.maxAnisotropy));
  if (!vk_ok(vkCreateSampler(s.dev.device, &si, nullptr, &s.sampler), "vkCreateSampler(room)")) return false;

  // r62: 4x multisampled where the device has it (clean edges), resolved into the eye images.
  const VkSampleCountFlagBits samples = s.dev.roomSamples >= 4 ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
  if (samples != VK_SAMPLE_COUNT_1_BIT && !make_room_render_pass(samples)) return false;
  VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(RoomPush)};
  VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  pl.setLayoutCount = 1;
  pl.pSetLayouts = &s.texSetLayout;
  pl.pushConstantRangeCount = 1;
  pl.pPushConstantRanges = &range;
  if (!vk_ok(vkCreatePipelineLayout(s.dev.device, &pl, nullptr, &s.roomLayout), "vkCreatePipelineLayout(room)")) {
    return false;
  }
  VkShaderModule vert = make_shader(kXrFxRoomVert, sizeof(kXrFxRoomVert));
  VkShaderModule frag = make_shader(kXrFxRoomFrag, sizeof(kXrFxRoomFrag));
  if (!vert || !frag) return false;
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";
  VkVertexInputBindingDescription vb{0, sizeof(float) * Room::kRoomFloats, VK_VERTEX_INPUT_RATE_VERTEX};
  const VkVertexInputAttributeDescription va[] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                                  {1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 3},
                                                  {2, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 5},
                                                  {3, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 8}};
  VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  input.vertexBindingDescriptionCount = 1;
  input.pVertexBindingDescriptions = &vb;
  input.vertexAttributeDescriptionCount = 4;
  input.pVertexAttributeDescriptions = va;
  VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport.viewportCount = 1;
  viewport.scissorCount = 1;
  const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dynamic.dynamicStateCount = 2;
  dynamic.pDynamicStates = dyn;
  VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;  // Blender's faces wind either way
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = samples;
  VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  depth.depthTestEnable = VK_TRUE;
  depth.depthWriteEnable = VK_TRUE;
  depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo blendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blendState.attachmentCount = 1;
  blendState.pAttachments = &blend;
  VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &input;
  info.pInputAssemblyState = &ia;
  info.pViewportState = &viewport;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &ms;
  info.pDepthStencilState = &depth;
  info.pColorBlendState = &blendState;
  info.pDynamicState = &dynamic;
  info.layout = s.roomLayout;
  info.renderPass = samples != VK_SAMPLE_COUNT_1_BIT ? s.roomRenderPass : s.renderPass;
  const bool piped = vk_ok(vkCreateGraphicsPipelines(s.dev.device, VK_NULL_HANDLE, 1, &info, nullptr, &s.roomPipeline),
                           "vkCreateGraphicsPipelines(room)");
  vkDestroyShaderModule(s.dev.device, vert, nullptr);
  vkDestroyShaderModule(s.dev.device, frag, nullptr);
  if (!piped) return false;

  // The sky dome, 300 m out (the far plane is 600): a latitude-longitude grid whose uv reads the
  // outside picture as Blender's panorama camera made it (u = 0.5 straight ahead, -Z; rising to the
  // right, +X; v = 0 overhead).
  constexpr int kCols = 64, kRows = 32;
  constexpr float kSky = 300.0f, kPi = 3.14159265f;
  std::vector<float> sv;
  sv.reserve((kCols + 1) * (kRows + 1) * Room::kRoomFloats);
  for (int i = 0; i <= kRows; ++i) {
    const float v = static_cast<float>(i) / kRows, phi = (0.5f - v) * kPi;
    for (int j = 0; j <= kCols; ++j) {
      const float u = static_cast<float>(j) / kCols, th = (u - 0.5f) * 2.0f * kPi;
      const float dx = std::sin(th) * std::cos(phi), dy = std::sin(phi), dz = -std::cos(th) * std::cos(phi);
      // (No shine: the shader keeps its picture as it is.)
      sv.insert(sv.end(), {kSky * dx, kSky * dy, kSky * dz, u, v, -dx, -dy, -dz, 0.0f, 0.0f});
    }
  }
  std::vector<uint32_t> si2;
  si2.reserve(kCols * kRows * 6);
  for (int i = 0; i < kRows; ++i) {
    for (int j = 0; j < kCols; ++j) {
      const uint32_t a = static_cast<uint32_t>(i * (kCols + 1) + j), b = a + 1, c = a + kCols + 1, d = c + 1;
      si2.insert(si2.end(), {a, c, b, b, c, d});
    }
  }
  if (!make_filled_buffer(sv.data(), sizeof(float) * sv.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &s.skyVerts, &s.skyVertMemory) ||
      !make_filled_buffer(si2.data(), sizeof(uint32_t) * si2.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &s.skyIndices,
                          &s.skyIndexMemory)) {
    return false;
  }
  s.skyIndexCount = static_cast<uint32_t>(si2.size());

  if (!make_eye(s.roomEyes[0], w, h, samples, s.roomRenderPass) || !make_eye(s.roomEyes[1], w, h, samples, s.roomRenderPass)) {
    return false;
  }
  for (Slot& slot : s.roomSlots) {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = s.pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!vk_ok(vkAllocateCommandBuffers(s.dev.device, &alloc, &slot.cmd), "vkAllocateCommandBuffers(room)") ||
        !vk_ok(vkCreateFence(s.dev.device, &fence, nullptr, &slot.fence), "vkCreateFence(room)")) {
      return false;
    }
  }
  s.roomLayerReady = true;
  FXLOG("fx: suite room layer ready (%ux%u per eye, %ux multisampled, anisotropy %.0f)", w, h,
        static_cast<unsigned>(samples), si.anisotropyEnable ? si.maxAnisotropy : 0.0f);
  return true;
}

// The room set_room handed over, if any: the last one let go, the new one put up (`canUpload`: the
// room layer is ready; otherwise only a letting go is taken).
void take_room(bool canUpload) {
  Room next;
  {
    std::lock_guard<std::mutex> lock(g_roomMutex);
    if (!g_roomPending) return;
    if (!canUpload && !g_roomNext.indices.empty()) return;
    next = std::move(g_roomNext);
    g_roomNext = Room{};
    g_roomPending = false;
  }
  wait_frames();
  destroy_buffer(s.roomVerts, s.roomVertMemory);
  destroy_buffer(s.roomIndices, s.roomIndexMemory);
  destroy_tex(s.roomTex);
  destroy_tex(s.outsideTex);
  const bool had = s.roomIndexCount > 0;
  s.roomIndexCount = 0;
  s.roomLeds.clear();
  if (next.indices.empty()) {
    if (had) FXLOG("fx: suite room let go");
    return;
  }
  const auto t0 = std::chrono::steady_clock::now();
  const bool ok =
      next.rgba.size() >= size_t(next.width) * next.height * 4 && next.width > 0 &&
      make_filled_buffer(next.verts.data(), sizeof(float) * next.verts.size(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         &s.roomVerts, &s.roomVertMemory) &&
      make_filled_buffer(next.indices.data(), sizeof(uint32_t) * next.indices.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                         &s.roomIndices, &s.roomIndexMemory) &&
      make_tex(s.roomTex, s.roomSet, next.rgba.data(), next.width, next.height) &&
      (next.outside.size() < size_t(next.outsideWidth) * next.outsideHeight * 4 || next.outsideWidth == 0 ||
       make_tex(s.outsideTex, s.outsideSet, next.outside.data(), next.outsideWidth, next.outsideHeight));
  if (!ok) {
    FXERR("fx: suite room upload failed");
    destroy_buffer(s.roomVerts, s.roomVertMemory);
    destroy_buffer(s.roomIndices, s.roomIndexMemory);
    destroy_tex(s.roomTex);
    destroy_tex(s.outsideTex);
    return;
  }
  s.roomIndexCount = static_cast<uint32_t>(next.indices.size());
  std::memcpy(s.roomLamp, next.lamp, sizeof(s.roomLamp));
  s.roomLeds = std::move(next.leds);
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  FXLOG("fx: suite room up in %.0f ms: %zu vertices, %zu triangles, %zu LEDs, light %ux%u, outside %ux%u", ms,
        next.verts.size() / Room::kRoomFloats, next.indices.size() / 3, s.roomLeds.size() / Room::kLedFloats, next.width,
        next.height, next.outsideWidth, next.outsideHeight);
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Public
// ---------------------------------------------------------------------------------------------

bool init(const DeviceInfo& device) {
  if (s.initTried) return s.ready;
  s.initTried = true;
  s.dev = device;
  s.ready = s.dev.device != VK_NULL_HANDLE && s.dev.width > 0 && choose_format() && make_render_pass() &&
            make_pipelines() && make_meshes() && make_eye(s.eyes[0], s.dev.width, s.dev.height) &&
            make_eye(s.eyes[1], s.dev.width, s.dev.height) && make_slots();
  if (s.ready) {
    FXLOG("fx: celebration renderer ready (%ux%u per eye, format %d)", s.dev.width, s.dev.height,
          static_cast<int>(s.colorFormat));
    s.parts.reserve(kMaxParticles);
  } else {
    FXERR("fx: celebration renderer failed to start; goals will not show effects");
  }
  return s.ready;
}

static void destroy_models();  // r54, below

void shutdown() {
  if (s.dev.device == VK_NULL_HANDLE) return;
  vkDeviceWaitIdle(s.dev.device);
  destroy_models();  // r54
  for (Slot& slot : s.slots) {
    if (slot.instanceMapped) vkUnmapMemory(s.dev.device, slot.instanceMemory);
    if (slot.occluderMapped) vkUnmapMemory(s.dev.device, slot.occluderMemory);
    if (slot.instances) vkDestroyBuffer(s.dev.device, slot.instances, nullptr);
    if (slot.occluders) vkDestroyBuffer(s.dev.device, slot.occluders, nullptr);
    if (slot.instanceMemory) vkFreeMemory(s.dev.device, slot.instanceMemory, nullptr);
    if (slot.occluderMemory) vkFreeMemory(s.dev.device, slot.occluderMemory, nullptr);
    if (slot.fence) vkDestroyFence(s.dev.device, slot.fence, nullptr);
  }
  for (Eye& eye : s.eyes) {
    for (VkFramebuffer fb : eye.framebuffers) vkDestroyFramebuffer(s.dev.device, fb, nullptr);
    for (VkImageView v : eye.views) vkDestroyImageView(s.dev.device, v, nullptr);
    if (eye.depthView) vkDestroyImageView(s.dev.device, eye.depthView, nullptr);
    if (eye.depth) vkDestroyImage(s.dev.device, eye.depth, nullptr);
    if (eye.depthMemory) vkFreeMemory(s.dev.device, eye.depthMemory, nullptr);
    if (eye.swapchain) xrDestroySwapchain(eye.swapchain);
  }
  // r61: the suite room's layer.
  for (Slot& slot : s.roomSlots) {
    if (slot.fence) vkDestroyFence(s.dev.device, slot.fence, nullptr);
  }
  for (Eye& eye : s.roomEyes) {
    for (VkFramebuffer fb : eye.framebuffers) vkDestroyFramebuffer(s.dev.device, fb, nullptr);
    for (VkImageView v : eye.views) vkDestroyImageView(s.dev.device, v, nullptr);
    if (eye.depthView) vkDestroyImageView(s.dev.device, eye.depthView, nullptr);
    if (eye.depth) vkDestroyImage(s.dev.device, eye.depth, nullptr);
    if (eye.depthMemory) vkFreeMemory(s.dev.device, eye.depthMemory, nullptr);
    if (eye.msaaView) vkDestroyImageView(s.dev.device, eye.msaaView, nullptr);  // r62
    if (eye.msaa) vkDestroyImage(s.dev.device, eye.msaa, nullptr);
    if (eye.msaaMemory) vkFreeMemory(s.dev.device, eye.msaaMemory, nullptr);
    if (eye.swapchain) xrDestroySwapchain(eye.swapchain);
  }
  if (s.roomRenderPass) vkDestroyRenderPass(s.dev.device, s.roomRenderPass, nullptr);
  destroy_buffer(s.roomVerts, s.roomVertMemory);
  destroy_buffer(s.roomIndices, s.roomIndexMemory);
  destroy_buffer(s.skyVerts, s.skyVertMemory);
  destroy_buffer(s.skyIndices, s.skyIndexMemory);
  destroy_tex(s.roomTex);
  destroy_tex(s.outsideTex);
  if (s.sampler) vkDestroySampler(s.dev.device, s.sampler, nullptr);
  if (s.texPool) vkDestroyDescriptorPool(s.dev.device, s.texPool, nullptr);
  if (s.texSetLayout) vkDestroyDescriptorSetLayout(s.dev.device, s.texSetLayout, nullptr);
  if (s.roomPipeline) vkDestroyPipeline(s.dev.device, s.roomPipeline, nullptr);
  if (s.roomLayout) vkDestroyPipelineLayout(s.dev.device, s.roomLayout, nullptr);
  if (s.roomDepthPipeline) vkDestroyPipeline(s.dev.device, s.roomDepthPipeline, nullptr);
  if (s.dumpMapped) vkUnmapMemory(s.dev.device, s.dumpMemory);
  if (s.dumpBuffer) vkDestroyBuffer(s.dev.device, s.dumpBuffer, nullptr);
  if (s.dumpMemory) vkFreeMemory(s.dev.device, s.dumpMemory, nullptr);
  if (s.pool) vkDestroyCommandPool(s.dev.device, s.pool, nullptr);
  if (s.fxPipeline) vkDestroyPipeline(s.dev.device, s.fxPipeline, nullptr);
  if (s.markerPipeline) vkDestroyPipeline(s.dev.device, s.markerPipeline, nullptr);
  if (s.meshPipeline) vkDestroyPipeline(s.dev.device, s.meshPipeline, nullptr);
  if (s.meshVerts) vkDestroyBuffer(s.dev.device, s.meshVerts, nullptr);
  if (s.meshIndices) vkDestroyBuffer(s.dev.device, s.meshIndices, nullptr);
  if (s.meshVertMemory) vkFreeMemory(s.dev.device, s.meshVertMemory, nullptr);
  if (s.meshIndexMemory) vkFreeMemory(s.dev.device, s.meshIndexMemory, nullptr);
  if (s.occluderPipeline) vkDestroyPipeline(s.dev.device, s.occluderPipeline, nullptr);
  if (s.layout) vkDestroyPipelineLayout(s.dev.device, s.layout, nullptr);
  if (s.renderPass) vkDestroyRenderPass(s.dev.device, s.renderPass, nullptr);
  s = State{};
}

// The cannons fire in waves this far apart; the Touch buzz follows them (cannon_waves_fired).
constexpr float kWaveGap = 0.45f;

void goal(int team, int character, bool superStrike, bool hyper, bool ownGoal) {
  s.theme = (character >= 0 && character < 12) ? kThemes[character] : kNeutral;
  s.superStrike = superStrike || hyper;
  s.intensity = ownGoal ? 0.45f : (superStrike ? 1.4f : 1.0f);
  s.stage = Stage::Raining;
  s.stageTime = 0.0f;
  s.sinceGoal = 0.0f;
  // Super Bowl, every goal: three waves of confetti from the sky, a fireworks barrage, pyro, a flash.
  s.cannonWavesLeft = ownGoal ? 1 : 3;
  s.cannonTimer = 0.0f;
  s.cannonWave = 0;
  s.pendingRockets = ownGoal ? 2 : (superStrike ? 24 : 14);
  s.rocketTimer = 0.0f;
  s.jetTime = ownGoal ? 0.0f : (superStrike ? 4.0f : 2.6f);
  s.pendingFlash = !ownGoal;
  FXLOG("fx: goal for team %d, %s theme%s%s", team,
        (character >= 0 && character < 12) ? kNames[character] : "neutral", superStrike ? ", Super Strike" : "",
        ownGoal ? ", own goal (toned down)" : "");
}

void phase(int p) {
  switch (p) {
  case 1:  // replay started: the jumbotron comes down, and a second volley goes up
    s.dropTarget = 1.0f;
    if (s.stage == Stage::Raining && s.intensity > 0.5f) {
      s.cannonWavesLeft += 2;
      s.pendingRockets += 8;
      s.jetTime = std::max(s.jetTime, 1.6f);
    }
    break;
  case 2:  // replay played out: taper off, a few stragglers
  case 4:  // play live again
    if (s.stage == Stage::Raining) {
      s.stage = Stage::Tapering;
      s.stageTime = 0.0f;
    }
    s.dropTarget = 0.0f;
    break;
  case 3:  // skipped: everything stops at once
    s.parts.clear();
    s.stage = Stage::Idle;
    s.stageTime = 0.0f;
    s.cannonWavesLeft = 0;
    s.pendingRockets = 0;
    s.jetTime = 0.0f;
    s.pendingFlash = false;
    s.drop = s.dropTarget = s.dropVel = 0.0f;
    s.hatsLeft = 0;
    break;
  default:
    break;
  }
}

int cannon_waves_fired() { return s.cannonWave; }

void hat_trick(float x, float y, float z, bool room) {
  s.hatCentre = XrVector3f{x, y, z};
  s.hatsLeft = room ? 45 : 110;
  s.hatAccum = 0.0f;
  FXLOG("fx: hat trick, %d hats", s.hatsLeft);
}

void set_marker(bool shown, float x, float y, float z, int teamCaptain) {
  s.markerShown = shown;
  s.markerGame[0] = x;
  s.markerGame[1] = y;
  s.markerGame[2] = z;
  s.markerColour = marker_colour(teamCaptain);
}

static bool marker_active(const Scene& sc) { return s.markerShown && sc.tabletop; }

// r53/r54: the controllers in your hands (Scene::hands), drawn every display frame they are tracked.
// r54 (Lance on r53's built ones: "fat as hell ... did not look like a quest 3 controller ... just make
// em look like fancy quest 3 controllers, themed around whoevers team"): the headset's own models
// (xr_ctrl_model.h), in grip space, one instance per part, in the team's colours with its motif etched
// into the body (xr_fx_mesh.frag, shape 21). Until a hand's model has loaded, that hand shows nothing.
struct CtrlMesh {
  bool ready = false;
  VkBuffer verts = VK_NULL_HANDLE, indices = VK_NULL_HANDLE;
  VkDeviceMemory vertMemory = VK_NULL_HANDLE, indexMemory = VK_NULL_HANDLE;
  std::vector<xrctrl::Part> parts;
  float faceUp[3] = {0.0f, 0.0f, -1.0f};
  float faceFwd[3] = {0.0f, -1.0f, 0.0f};  // r54b
};
CtrlMesh s_ctrl[2];

static bool hands_shown(const Scene& sc) {
  return (sc.hands[0].valid && s_ctrl[0].ready) || (sc.hands[1].valid && s_ctrl[1].ready);
}

constexpr uint32_t kHandParts = 2 * xrctrl::kPartKinds;  // instances for both controllers

// A newly loaded model into its buffers (the old ones, if any, after the GPU is done with them).
static void take_models() {
  for (int h = 0; h < 2; ++h) {
    xrctrl::Model m;
    if (!xrctrl::take(h, &m) || m.verts.empty() || m.indices.empty()) continue;
    CtrlMesh& c = s_ctrl[h];
    if (c.verts != VK_NULL_HANDLE || c.indices != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(s.dev.device);
      if (c.verts) vkDestroyBuffer(s.dev.device, c.verts, nullptr);
      if (c.indices) vkDestroyBuffer(s.dev.device, c.indices, nullptr);
      if (c.vertMemory) vkFreeMemory(s.dev.device, c.vertMemory, nullptr);
      if (c.indexMemory) vkFreeMemory(s.dev.device, c.indexMemory, nullptr);
      c = CtrlMesh{};
    }
    void* mapped = nullptr;
    const VkDeviceSize vbytes = sizeof(mesh::Vertex) * m.verts.size();
    const VkDeviceSize ibytes = sizeof(uint32_t) * m.indices.size();
    if (!make_host_buffer(vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &c.verts, &c.vertMemory, &mapped)) continue;
    std::memcpy(mapped, m.verts.data(), vbytes);
    if (!make_host_buffer(ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &c.indices, &c.indexMemory, &mapped)) continue;
    std::memcpy(mapped, m.indices.data(), ibytes);
    c.parts = m.parts;
    std::memcpy(c.faceUp, m.faceUp, sizeof(c.faceUp));
    std::memcpy(c.faceFwd, m.faceFwd, sizeof(c.faceFwd));
    c.ready = true;
    FXLOG("fx: controller model %d ready (%s, %zu parts)", h, m.name.c_str(), c.parts.size());
  }
}

static void destroy_models() {
  for (CtrlMesh& c : s_ctrl) {
    if (c.verts) vkDestroyBuffer(s.dev.device, c.verts, nullptr);
    if (c.indices) vkDestroyBuffer(s.dev.device, c.indices, nullptr);
    if (c.vertMemory) vkFreeMemory(s.dev.device, c.vertMemory, nullptr);
    if (c.indexMemory) vkFreeMemory(s.dev.device, c.indexMemory, nullptr);
    c = CtrlMesh{};
  }
}

// 0xRRGGBB colours mixed, t = 0 a, 1 b.
static uint32_t mix_rgb(uint32_t a, uint32_t b, float t) {
  t = std::min(std::max(t, 0.0f), 1.0f);
  uint32_t out = 0;
  for (int sh = 0; sh < 24; sh += 8) {
    const float ca = static_cast<float>((a >> sh) & 0xFF), cb = static_cast<float>((b >> sh) & 0xFF);
    out |= static_cast<uint32_t>(ca + (cb - ca) * t + 0.5f) << sh;
  }
  return out;
}

static void v_norm3(float v[3]) {
  const float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (l > 1e-8f)
    for (int k = 0; k < 3; ++k) v[k] /= l;
}
static void v_cross3(const float a[3], const float b[3], float out[3]) {
  out[0] = a[1] * b[2] - a[2] * b[1];
  out[1] = a[2] * b[0] - a[0] * b[2];
  out[2] = a[0] * b[1] - a[1] * b[0];
}

// One controller part to draw: which hand's buffers, which part.
struct HandDraw {
  uint8_t hand, part;
};

// The controllers' instances into `inst` (at most kHandParts), with what each draws. The body in the
// captain's main colour (its motif etched into it, shader), the stick, buttons, trigger, grip and menu
// button in the detail colour; pressed ones move and light up in the team's bright colour (white where
// that is too close to the detail colour).
static uint32_t write_hands(const Scene& sc, Instance* inst, HandDraw* draws) {
  const bool themed = sc.skinCaptain >= 0 && sc.skinCaptain < 12;
  const Theme& th = themed ? kThemes[sc.skinCaptain] : kNeutral;
  const uint32_t bright = marker_colour(themed ? sc.skinCaptain : -1);
  // r54b: the stick, buttons, trigger and grip in the team's third colour (Mario's blue on his red),
  // the body's inlay in the second (his white).
  const uint32_t accent = th.paper[2];
  int diff = 0;
  for (int sh = 0; sh < 24; sh += 8) diff += std::abs(int((bright >> sh) & 0xFF) - int((accent >> sh) & 0xFF));
  const uint32_t litRgb = diff > 120 ? bright : 0xFFFFFF;
  const uint32_t body = pack(th.paper[0]);
  const uint32_t inlay = pack(th.paper[1]);
  const float theme = static_cast<float>(themed ? sc.skinCaptain + 1 : 0);
  uint32_t n = 0;
  for (int h = 0; h < 2; ++h) {
    const Scene::Hand& hd = sc.hands[h];
    const CtrlMesh& cm = s_ctrl[h];
    if (!hd.valid || !cm.ready) continue;
    const XrQuaternionf& g = hd.grip.orientation;
    const float gq[4] = {g.x, g.y, g.z, g.w};
    // The face's own axes in grip space: up out of it, "forward" across it (away from you), right.
    float up[3] = {cm.faceUp[0], cm.faceUp[1], cm.faceUp[2]};
    v_norm3(up);
    float fwd[3] = {cm.faceFwd[0], cm.faceFwd[1], cm.faceFwd[2]};
    const float fu = fwd[0] * up[0] + fwd[1] * up[1] + fwd[2] * up[2];
    for (int k = 0; k < 3; ++k) fwd[k] -= fu * up[k];
    v_norm3(fwd);
    float right[3];
    v_cross3(fwd, up, right);
    v_norm3(right);
    for (size_t pi = 0; pi < cm.parts.size() && n < kHandParts; ++pi) {
      const xrctrl::Part& part = cm.parts[pi];
      float q[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // the part's own turn about its pivot (grip space)
      float t[3] = {0.0f, 0.0f, 0.0f};        // and move (grip space)
      float press = 0.0f;
      switch (part.kind) {
      case xrctrl::kStick: {
        // Tilted the way it is pushed: its top toward `right` for +x, toward `fwd` for +y.
        float kx[3], ky[3], qx[4], qy[4];
        v_cross3(up, right, kx);
        v_cross3(up, fwd, ky);
        quat_axis_angle(kx, 0.35f * hd.stick[0], qx);
        quat_axis_angle(ky, 0.35f * hd.stick[1], qy);
        quat_mul(qx, qy, q);
        press = hd.stickClick ? 1.0f : 0.0f;
        break;
      }
      case xrctrl::kButton1:
      case xrctrl::kButton2:
      case xrctrl::kMenu:
        press = part.kind == xrctrl::kButton1 ? (hd.button1 ? 1.0f : 0.0f)
                : part.kind == xrctrl::kButton2 ? (hd.button2 ? 1.0f : 0.0f)
                                                : (hd.menu ? 1.0f : 0.0f);
        if (!part.hasPress)
          for (int k = 0; k < 3; ++k) t[k] = -0.0015f * press * up[k];
        break;
      case xrctrl::kTrigger:
        press = hd.trigger;
        if (!part.hasPress) {
          const float xAxis[3] = {1.0f, 0.0f, 0.0f};
          quat_axis_angle(xAxis, -0.25f * press, q);
        }
        break;
      case xrctrl::kGrip:
        press = hd.squeeze;
        if (!part.hasPress) {
          float in[3] = {-part.centre[0], -part.centre[1], 0.0f};
          v_norm3(in);
          for (int k = 0; k < 3; ++k) t[k] = 0.003f * press * in[k];
        }
        break;
      default:
        break;
      }
      // r54b: the model's own press (its animation's pressed pose), as far as it is pressed.
      if (part.kind != xrctrl::kStick && part.kind != xrctrl::kBody && part.hasPress && press > 0.0f) {
        const float w = std::min(1.0f, std::max(-1.0f, part.pressTurn[3]));
        const float angle = 2.0f * std::acos(w);
        const float s = std::sqrt(std::max(0.0f, 1.0f - w * w));
        if (s > 1e-5f) {
          const float axis[3] = {part.pressTurn[0] / s, part.pressTurn[1] / s, part.pressTurn[2] / s};
          quat_axis_angle(axis, angle * press, q);
        }
        for (int k = 0; k < 3; ++k) t[k] = part.pressMove[k] * press;
      }
      // About the pivot p: v -> p + R (v - p) + t, as the instance's turn and offset.
      float rp[3];
      const XrQuaternionf qr{q[0], q[1], q[2], q[3]};
      rotate(qr, part.pivot, rp);
      const float local[3] = {part.pivot[0] - rp[0] + t[0], part.pivot[1] - rp[1] + t[1], part.pivot[2] - rp[2] + t[2]};
      float w[3];
      rotate(g, local, w);
      Instance& o = inst[n];
      o.posSize[0] = hd.grip.position.x + w[0];
      o.posSize[1] = hd.grip.position.y + w[1];
      o.posSize[2] = hd.grip.position.z + w[2];
      o.posSize[3] = 1.0f;
      quat_mul(gq, q, o.quat);
      o.color1 = body;
      o.color2 = part.kind == xrctrl::kBody ? inlay : pack(mix_rgb(accent, litRgb, press));
      o.params[0] = static_cast<float>(kController);
      o.params[1] = theme;  // the etched motif (shader); meshes have no width, so no aspect is needed
      o.params[2] = 1.0f;
      o.params[3] = part.kind == xrctrl::kBody ? 0.0f : 0.9f * press;
      draws[n].hand = static_cast<uint8_t>(h);
      draws[n].part = static_cast<uint8_t>(pi);
      ++n;
    }
  }
  return n;
}

// r57: the trophy shelf in your room (Scene::shelf*).
constexpr uint32_t kShelfParts = 56;  // the shelf, 8 trophies, 8 emblems, up to 4 win stars each
static bool shelf_shown(const Scene& sc) { return sc.shelfValid && (sc.mr || sc.suite); }

// The cups, in eTrophyType order: cup and base colours, the emblem piece and its colours. The four
// cups gold, the Super ones platinum.
struct CupLook {
  uint32_t cup, base;
  Shape emblem;
  uint32_t e1, e2;
};
const CupLook kCups[8] = {
    {0xFFC83A, 0x7A1E1A, kMushroom, 0xE4202A, 0xFFFFFF}, {0xFFC83A, 0x1E5A2A, kRose, 0xFF6A1A, 0x2E8B3A},
    {0xFFC83A, 0x1E2A6A, kStar, 0xFFD21F, 0xFFF2A0},     {0xFFC83A, 0x3A2A1E, kShell, 0x2FB34A, 0xF4F1E6},
    {0xE6EEFF, 0x7A1E1A, kMushroom, 0xE4202A, 0xFFFFFF}, {0xE6EEFF, 0x1E5A2A, kRose, 0xFF6A1A, 0x2E8B3A},
    {0xE6EEFF, 0x1E2A6A, kStar, 0xFFD21F, 0xFFF2A0},     {0xE6EEFF, 0x3A2A1E, kShell, 0x2FB34A, 0xF4F1E6},
};
constexpr float kTrophySize = 0.12f;  // meters tall
constexpr float kShelfTop = 0.016f;   // the board's top above its middle

// A slot's trophy middle on the shelf (shelf frame: x right, y up, z front).
static void shelf_slot(int i, float* x, float* y) {
  *x = -0.405f + static_cast<float>(i) * 0.1157f;
  *y = kShelfTop + 0.5f * kTrophySize;
}

static XrVector3f shelf_to_room(const Scene& sc, float x, float y, float z) {
  const float c = std::cos(sc.shelfYaw), s2 = std::sin(sc.shelfYaw);
  return XrVector3f{sc.shelfPos.x + x * c + z * s2, sc.shelfPos.y + y, sc.shelfPos.z - x * s2 + z * c};
}

// The shelf's instances into `inst` (at most kShelfParts), each one's mesh into `meshOf`. Cups not
// yet won stand as dull grey stand-ins (aspect 1 tells the shader), the ones won shine, with their
// emblem in front, a little star on the shelf's edge for every extra win (up to four), and the one
// just won turning round and glowing for a few seconds.
static uint32_t write_shelf(const Scene& sc, Instance* inst, uint8_t* meshOf) {
  uint32_t n = 0;
  float qShelf[4];
  const float yAxis[3] = {0.0f, 1.0f, 0.0f};
  quat_axis_angle(yAxis, sc.shelfYaw, qShelf);
  auto put = [&](uint8_t meshId, Shape shape, const XrVector3f& p, const float q[4], float size, uint32_t c1, uint32_t c2,
                 float aspect, float glow) {
    if (n >= kShelfParts || meshId >= mesh::kCount) return;
    Instance& o = inst[n];
    o.posSize[0] = p.x;
    o.posSize[1] = p.y;
    o.posSize[2] = p.z;
    o.posSize[3] = size;
    std::memcpy(o.quat, q, sizeof(o.quat));
    o.color1 = c1;
    o.color2 = c2;
    o.params[0] = static_cast<float>(shape);
    o.params[1] = aspect;
    o.params[2] = 1.0f;
    o.params[3] = glow;
    meshOf[n++] = meshId;
  };
  put(mesh::kShelf, kShelfShape, sc.shelfPos, qShelf, 1.0f, pack(0x9A6A3A), pack(0xFFD21F), 0.0f, 0.0f);
  for (int i = 0; i < 8; ++i) {
    float x, y;
    shelf_slot(i, &x, &y);
    const bool won = (sc.shelfCups >> i) & 1u;
    XrVector3f at = shelf_to_room(sc, x, y, 0.0f);
    float q[4];
    std::memcpy(q, qShelf, sizeof(q));
    float glow = 0.0f;
    float size = kTrophySize;
    if (sc.shelfCupFree[i]) {
      // r57b: in your hand, or on its way back to its spot. r60: at the size your hands gave it.
      const XrPosef& pose = sc.shelfCupPose[i];
      size = kTrophySize * std::max(0.3f, std::min(10.0f, sc.shelfCupScale[i]));
      at = pose.position;
      q[0] = pose.orientation.x;
      q[1] = pose.orientation.y;
      q[2] = pose.orientation.z;
      q[3] = pose.orientation.w;
    } else if (won && i == sc.shelfNewCup && sc.shelfNewAge < 6.0f) {
      // Just won: it turns round (slowing to rest facing you) and glows.
      const float left = 1.0f - sc.shelfNewAge / 6.0f;
      float spin[4];
      quat_axis_angle(yAxis, 6.2831853f * 3.0f * (1.0f - left * left), spin);
      quat_mul(qShelf, spin, q);
      glow = 0.7f * left;
    }
    const CupLook& look = kCups[i];
    if (!won) {
      put(mesh::kTrophy, kTrophyShape, at, q, size, pack(0x4A505A), pack(0x30343C), 1.0f, 0.0f);
      continue;
    }
    put(mesh::kTrophy, kTrophyShape, at, q, size, pack(look.cup), pack(look.base), 0.0f, glow);
    // The emblem on the bowl's front, turning with the cup.
    const XrQuaternionf qq{q[0], q[1], q[2], q[3]};
    const float front[3] = {0.0f, 0.2f * size, 0.4f * size};
    float off[3];
    rotate(qq, front, off);
    put(mesh_for(look.emblem), look.emblem, XrVector3f{at.x + off[0], at.y + off[1], at.z + off[2]}, q, 0.3f * size,
        pack(look.e1), pack(look.e2), 1.0f, 0.0f);
    // A little gold star on the shelf's front edge for each further win.
    const int extra = std::min(4, std::max(0, static_cast<int>(sc.shelfWins[i]) - 1));
    for (int k = 0; k < extra; ++k) {
      const XrVector3f sp = shelf_to_room(sc, x - 0.024f + 0.016f * static_cast<float>(k), kShelfTop + 0.01f, 0.09f);
      put(mesh::kStar, kStar, sp, qShelf, 0.018f, pack(0xFFD21F), pack(0xFFF2A0), 1.0f, 0.0f);
    }
  }
  return n;
}

// r61: the home ground's pieces (Scene::homePieces), solid like the shelf.
constexpr uint32_t kHomeParts = 24;
enum : uint8_t { kBannerShape = 25, kPennantShape = 26, kBallShape = 27 };
static uint32_t write_home(const Scene& sc, Instance* inst, uint8_t* meshOf) {
  uint32_t n = 0;
  for (int i = 0; i < sc.homeCount && i < static_cast<int>(kHomeParts); ++i) {
    const Scene::HomePiece& hp = sc.homePieces[i];
    uint8_t meshId, shape;
    switch (hp.kind) {
    case 0: meshId = mesh::kBanner; shape = kBannerShape; break;
    case 1: meshId = mesh::kPennant; shape = kPennantShape; break;
    case 2: meshId = mesh::kBall; shape = kBallShape; break;
    case 4: meshId = mesh::kPedestal; shape = kShelfShape; break;
    case 5: meshId = mesh::kTrophy; shape = kTrophyShape; break;  // theme 1 = a ghost (not won yet)
    default: meshId = mesh::kShelf; shape = kShelfShape; break;
    }
    const bool wood = hp.kind == 3 || hp.kind == 4;
    Instance& o = inst[n];
    o.posSize[0] = hp.pose.position.x;
    o.posSize[1] = hp.pose.position.y;
    o.posSize[2] = hp.pose.position.z;
    o.posSize[3] = hp.size;
    o.quat[0] = hp.pose.orientation.x;
    o.quat[1] = hp.pose.orientation.y;
    o.quat[2] = hp.pose.orientation.z;
    o.quat[3] = hp.pose.orientation.w;
    o.color1 = pack(wood ? 0x9A6A3A : hp.rgb1);
    o.color2 = pack(wood ? 0xFFD21F : hp.rgb2);
    o.params[0] = static_cast<float>(shape);
    o.params[1] = static_cast<float>(hp.theme);
    o.params[2] = 1.0f;
    o.params[3] = hp.glow;
    meshOf[n++] = meshId;
  }
  return n;
}

// r61: sparks crackling round the Super Strike ball, a few a second.
static void home_sparks(const Scene& sc, float dt) {
  static float s_wait = 0.0f;
  s_wait -= dt;
  if (sc.sparkCount <= 0 || s_wait > 0.0f) return;
  s_wait = frand(0.06f, 0.16f);
  for (int i = 0; i < sc.sparkCount && i < 2; ++i) {
    float d[3];
    random_unit(d);
    const float r = sc.sparkRadius[i];
    const XrVector3f at{sc.sparkAt[i].x + d[0] * r, sc.sparkAt[i].y + d[1] * r, sc.sparkAt[i].z + d[2] * r};
    const float v[3] = {d[0] * 0.25f, d[1] * 0.25f + 0.05f, d[2] * 0.25f};
    add_spark(at, v, frand(0.012f, 0.022f), 0xFFFFFF, sc.sparkRgb, frand(0.18f, 0.35f), 0.3f, 0.0f);
  }
}

// ---------------------------------------------------------------------------------------------
// r61: the cup suite's live things (Lance: "make the stuff within the suite, fell alive. they dont
// HAVE to move per se but just animate"). Places are the suite's own (suite_to_room), matching
// tools/suites/suite_scene.py's rooms: Blender's (x, y, z) is the suite's (x, z, -y).
// ---------------------------------------------------------------------------------------------

// A rotation from the axes it turns x, y and z into (unit, at right angles).
static void quat_from_axes(const float x[3], const float y[3], const float z[3], float q[4]) {
  const float tr = x[0] + y[1] + z[2];
  if (tr > 0.0f) {
    const float S = std::sqrt(tr + 1.0f) * 2.0f;
    q[3] = 0.25f * S; q[0] = (y[2] - z[1]) / S; q[1] = (z[0] - x[2]) / S; q[2] = (x[1] - y[0]) / S;
  } else if (x[0] > y[1] && x[0] > z[2]) {
    const float S = std::sqrt(1.0f + x[0] - y[1] - z[2]) * 2.0f;
    q[3] = (y[2] - z[1]) / S; q[0] = 0.25f * S; q[1] = (y[0] + x[1]) / S; q[2] = (z[0] + x[2]) / S;
  } else if (y[1] > z[2]) {
    const float S = std::sqrt(1.0f + y[1] - x[0] - z[2]) * 2.0f;
    q[3] = (z[0] - x[2]) / S; q[0] = (y[0] + x[1]) / S; q[1] = 0.25f * S; q[2] = (z[1] + y[2]) / S;
  } else {
    const float S = std::sqrt(1.0f + z[2] - x[0] - y[1]) * 2.0f;
    q[3] = (x[1] - y[0]) / S; q[0] = (z[0] + x[2]) / S; q[1] = (z[1] + y[2]) / S; q[2] = 0.25f * S;
  }
}

static void yaw_quat(float yaw, float q[4]) {
  q[0] = 0.0f;
  q[1] = std::sin(0.5f * yaw);
  q[2] = 0.0f;
  q[3] = std::cos(0.5f * yaw);
}

// How many to start this frame at `rate` a second.
static int spawn_count(float& acc, float rate, float dt) {
  acc += rate * dt;
  const int n = static_cast<int>(acc);
  acc -= static_cast<float>(n);
  return n;
}

// A suite piece: a mesh falling (or, fallSpeed < 0, rising) at its own pace, tumbling; it fades where
// it lands, or at maxAge.
static void add_suite_piece(const XrVector3f& p, const float v[3], Shape shape, float size, uint32_t c1, uint32_t c2,
                            float fallSpeed, float maxAge, float flutter) {
  if (room_full()) return;
  Particle q{};
  q.p[0] = p.x; q.p[1] = p.y; q.p[2] = p.z;
  std::memcpy(q.v, v, sizeof(q.v));
  random_quat(q.q);
  random_unit(q.axis);
  q.spin = frand(1.0f, 3.0f);
  q.size = size;
  q.aspect = 1.0f;
  q.shape = shape;
  q.mesh = mesh_for(shape);
  q.c1 = pack(c1);
  q.c2 = pack(c2);
  q.kind = kKindPiece;
  q.life = 0.6f;  // the fade once landed
  q.flutterPhase = frand(0.0f, 6.28f);
  q.flutterFreq = frand(0.8f, 1.6f);
  q.flutterAmp = flutter;
  q.fallSpeed = fallSpeed;
  q.maxAge = maxAge;
  s.parts.push_back(q);
}

// Flames licking up from a bowl or a wick at `at` (the suite's), `spread` wide.
static void suite_flames(const Scene& sc, float& acc, float rate, float dt, const XrVector3f* at, int count,
                         float spread, float size, float rise, uint32_t core, uint32_t outer) {
  for (int i = spawn_count(acc, rate, dt); i > 0; --i) {
    const XrVector3f& c = at[irand(count)];
    const XrVector3f p = suite_to_room(sc, c.x + frand(-spread, spread), c.y + frand(0.0f, 0.4f * spread),
                                       c.z + frand(-spread, spread));
    const float v[3] = {frand(-0.04f, 0.04f), rise * frand(0.6f, 1.0f), frand(-0.04f, 0.04f)};
    add_spark(p, v, size * frand(0.7f, 1.2f), core, outer, frand(0.3f, 0.55f), 1.5f, -0.04f);
  }
}

// Little lights wandering in the air (wisps, fireflies, pollen): at random in the box given.
static void suite_motes(const Scene& sc, float& acc, float rate, float dt, float x0, float x1, float y0, float y1,
                        float z0, float z1, float size, float speed, uint32_t core, uint32_t outer, float life) {
  for (int i = spawn_count(acc, rate, dt); i > 0; --i) {
    const XrVector3f p = suite_to_room(sc, frand(x0, x1), frand(y0, y1), frand(z0, z1));
    const float v[3] = {frand(-speed, speed), frand(-0.5f * speed, speed), frand(-speed, speed)};
    add_spark(p, v, size * frand(0.7f, 1.3f), core, outer, life * frand(0.7f, 1.2f), 0.0f, 0.0f);
  }
}

// What starts in the suite this frame: camera flashes in the stands and fireworks over the stadium
// out of the window, dust glinting in the light, and each captain's own.
static float s_live[4];  // the captains' own spawn accumulators
static void suite_live(const Scene& sc, float dt) {
  if (!sc.roomShown) return;
  s.suiteTime += dt;
  // About twenty flashes a second in the stands round the pitch (suite_scene.py's tiers: the far one
  // and the two sides, 9 m below the box, eight tiers up).
  for (int i = spawn_count(s.flashAccum, 20.0f, dt); i > 0; --i) {
    const int stand = irand(3);
    const float tier = static_cast<float>(irand(8));
    const float y = -6.0f + 3.0f * tier + 0.5f;
    const XrVector3f p = stand == 0 ? suite_to_room(sc, frand(-45.0f, 45.0f), y, -(72.9f + 3.2f * tier))
                                    : suite_to_room(sc, (stand == 1 ? -1.0f : 1.0f) * (29.9f + 3.2f * tier), y,
                                                    -(55.0f + frand(-30.0f, 30.0f)));
    const float v0[3] = {0.0f, 0.0f, 0.0f};
    add_spark(p, v0, frand(0.25f, 0.4f), 0xFFFFFF, 0xCFE4FF, frand(0.06f, 0.12f), 0.0f, 0.0f);
  }
  // Now and then a few rockets go up over the stadium, in the home captain's colours.
  s.rocketIn -= dt;
  if (s.rocketIn <= 0.0f) {
    s.rocketIn = frand(4.0f, 8.0f);
    if (s.stage == Stage::Idle) {
      s.theme = sc.suiteCaptain >= 0 && sc.suiteCaptain < 12 ? kThemes[sc.suiteCaptain] : kNeutral;
    }
    const Scale k = scale_for(sc);
    for (int i = 1 + irand(3); i > 0; --i) {
      const XrVector3f from = suite_to_room(sc, frand(-35.0f, 35.0f), -9.0f, -frand(60.0f, 95.0f));
      add_rocket(k, from, from.y + frand(28.0f, 42.0f), frand(22.0f, 28.0f));
    }
  }
  // Dust in the light.
  suite_motes(sc, s.moteAccum, 5.0f, dt, -4.0f, 4.0f, 0.4f, 3.0f, -2.8f, 3.2f, 0.008f, 0.03f, 0xFFFFFF, 0xFFE9A8, 2.8f);

  switch (sc.suiteCaptain) {
  case 5: {  // Luigi: green candle flames on the four candelabras, wisps drifting
    static const XrVector3f kWicks[12] = {
        {-3.7f, 1.49f, -1.2f}, {-3.5f, 1.39f, -1.2f}, {-3.3f, 1.49f, -1.2f}, {3.3f, 1.49f, -1.2f},
        {3.5f, 1.39f, -1.2f},  {3.7f, 1.49f, -1.2f},  {-3.7f, 1.49f, 2.0f},  {-3.5f, 1.39f, 2.0f},
        {-3.3f, 1.49f, 2.0f},  {3.3f, 1.49f, 2.0f},   {3.5f, 1.39f, 2.0f},   {3.7f, 1.49f, 2.0f}};
    suite_flames(sc, s_live[0], 36.0f, dt, kWicks, 12, 0.01f, 0.016f, 0.15f, 0xE8FFE0, 0x58FF7A);
    suite_motes(sc, s_live[1], 1.5f, dt, -3.8f, 3.8f, 1.0f, 2.8f, -2.6f, 3.0f, 0.025f, 0.08f, 0xCCFFD8, 0x3CFF8A, 4.0f);
    break;
  }
  case 7: {  // Peach: hearts rising from her crown, sparkles round it
    for (int i = spawn_count(s_live[0], 0.8f, dt); i > 0; --i) {
      const XrVector3f p = suite_to_room(sc, frand(-0.1f, 0.1f), 1.3f, 2.8f + frand(-0.1f, 0.1f));
      const float v[3] = {frand(-0.05f, 0.05f), 0.2f, frand(-0.05f, 0.05f)};
      add_suite_piece(p, v, kHeart, 0.09f, 0xFF4F9A, 0xFFB0D6, -0.25f, 3.5f, 0.12f);
    }
    suite_motes(sc, s_live[1], 6.0f, dt, -0.25f, 0.25f, 1.0f, 1.45f, 2.55f, 3.05f, 0.012f, 0.05f, 0xFFFFFF, 0xFFD86A, 0.6f);
    break;
  }
  case 1: {  // Daisy: petals drifting down all over, embers from the three lanterns
    for (int i = spawn_count(s_live[0], 2.0f, dt); i > 0; --i) {
      const XrVector3f p = suite_to_room(sc, frand(-3.8f, 3.8f), 3.3f, frand(-2.6f, 3.0f));
      const float v[3] = {0.0f, 0.0f, 0.0f};
      add_suite_piece(p, v, kPetal, 0.07f, 0xFFFFFF, 0xFFD21F, 0.3f, 14.0f, 0.35f);
    }
    static const XrVector3f kLanterns[3] = {{-2.0f, 2.62f, 1.6f}, {0.0f, 2.62f, 1.6f}, {2.0f, 2.62f, 1.6f}};
    for (int i = spawn_count(s_live[1], 8.0f, dt); i > 0; --i) {
      const XrVector3f& c = kLanterns[irand(3)];
      const XrVector3f p = suite_to_room(sc, c.x + frand(-0.12f, 0.12f), c.y, c.z + frand(-0.12f, 0.12f));
      const float v[3] = {frand(-0.05f, 0.05f), frand(-0.15f, -0.05f), frand(-0.05f, 0.05f)};
      add_spark(p, v, frand(0.008f, 0.013f), 0xFFF0C0, 0xFF8A1A, frand(0.8f, 1.4f), 0.5f, 0.01f);
    }
    break;
  }
  case 11:  // Yoshi: pollen off the smiling flowers
    suite_motes(sc, s_live[0], 4.0f, dt, -3.9f, 3.9f, 0.6f, 1.6f, -2.8f, 1.4f, 0.01f, 0.05f, 0xFFFFE0, 0xFFE03A, 2.0f);
    break;
  case 10: {  // Wario: coins raining onto his piles, glints on them
    static const XrVector3f kPiles[3] = {{-3.0f, 0.0f, 2.3f}, {3.0f, 0.0f, 2.3f}, {-3.3f, 0.0f, -1.0f}};
    for (int i = spawn_count(s_live[0], 1.2f, dt); i > 0; --i) {
      const XrVector3f& c = kPiles[irand(3)];
      const XrVector3f p = suite_to_room(sc, c.x + frand(-0.35f, 0.35f), 3.25f, c.z + frand(-0.35f, 0.35f));
      const float v[3] = {0.0f, 0.0f, 0.0f};
      add_suite_piece(p, v, kCoin, 0.13f, 0xFFC21F, 0xFFE27A, 1.6f, 4.0f, 0.15f);
    }
    for (int i = spawn_count(s_live[1], 4.0f, dt); i > 0; --i) {
      const XrVector3f& c = kPiles[irand(3)];
      const float a = frand(0.0f, 6.2831853f), r = frand(0.0f, 0.6f);
      const XrVector3f p = suite_to_room(sc, c.x + r * std::cos(a), 0.32f * (1.0f - r * r / 0.5f) + 0.05f,
                                         c.z + r * std::sin(a));
      const float v0[3] = {0.0f, 0.0f, 0.0f};
      add_spark(p, v0, frand(0.015f, 0.03f), 0xFFFFFF, 0xFFE27A, frand(0.15f, 0.3f), 0.0f, 0.0f);
    }
    break;
  }
  case 9: {  // Waluigi: purple fire in the four braziers
    static const XrVector3f kBowls[4] = {{-3.5f, 1.0f, -2.2f}, {3.5f, 1.0f, -2.2f}, {-3.5f, 1.0f, 2.8f}, {3.5f, 1.0f, 2.8f}};
    suite_flames(sc, s_live[0], 44.0f, dt, kBowls, 4, 0.12f, 0.055f, 0.75f, 0xF6D8FF, 0x9A3FE0);
    break;
  }
  case 2: {  // Donkey Kong: a banana off the hoards now and then, tiki fire, fireflies
    s.dropIn -= dt;
    if (s.dropIn <= 0.0f) {
      s.dropIn = frand(2.0f, 4.0f);
      static const XrVector3f kHoards[2] = {{-3.1f, 0.0f, 0.2f}, {3.2f, 0.0f, 2.5f}};
      const XrVector3f& c = kHoards[irand(2)];
      const XrVector3f p = suite_to_room(sc, c.x + frand(-0.4f, 0.4f), 3.2f, c.z + frand(-0.4f, 0.4f));
      const float v[3] = {0.0f, 0.0f, 0.0f};
      add_suite_piece(p, v, kBanana, 0.2f, 0xFFD83A, 0x6B4423, 2.0f, 3.0f, 0.1f);
    }
    static const XrVector3f kTorches[4] = {{-3.0f, 1.6f, -2.7f}, {3.0f, 1.6f, -2.7f}, {-2.0f, 1.6f, 2.95f}, {2.0f, 1.6f, 2.95f}};
    suite_flames(sc, s_live[0], 40.0f, dt, kTorches, 4, 0.06f, 0.05f, 0.7f, 0xFFF0C0, 0xFF7A1A);
    suite_motes(sc, s_live[1], 2.0f, dt, -3.8f, 3.8f, 0.5f, 2.5f, -2.6f, 3.0f, 0.014f, 0.1f, 0xF8FFC0, 0xB8FF3A, 3.8f);
    break;
  }
  default: {  // Mario: a coin pops out from under a ? block every few seconds
    s.popIn -= dt;
    if (s.popIn <= 0.0f) {
      s.popIn = frand(2.5f, 4.5f);
      static const float kBlocks[3] = {-3.6f, 2.5f, 3.6f};  // the used block (-2.5) gives nothing
      const XrVector3f p = suite_to_room(sc, kBlocks[irand(3)], 2.5f, -2.5f);
      float v[3] = {frand(-0.3f, 0.3f), 0.6f, 0.9f};  // out toward you
      const float c = std::cos(sc.roomYaw), sn = std::sin(sc.roomYaw);
      const float vx = v[0] * c + v[2] * sn, vz = -v[0] * sn + v[2] * c;
      v[0] = vx;
      v[2] = vz;
      add_suite_piece(p, v, kCoin, 0.22f, 0xFFC21F, 0xFFE27A, 1.2f, 5.0f, 0.1f);
      for (int i = 0; i < 8; ++i) {
        float d[3];
        random_unit(d);
        const float sv[3] = {d[0] * 0.6f, d[1] * 0.6f + 0.3f, d[2] * 0.6f};
        add_spark(p, sv, 0.012f, 0xFFFFFF, 0xFFD21F, frand(0.25f, 0.45f), 2.0f, 0.2f);
      }
    }
    break;
  }
  }
}

// The suite's things that stay (spinning, bobbing, hopping, drifting): instances like the shelf's.
constexpr uint32_t kSuiteParts = 16;
static uint32_t write_suite(const Scene& sc, Instance* inst, uint8_t* meshOf) {
  if (!sc.roomShown) return 0;
  uint32_t n = 0;
  const float t = s.suiteTime;
  const XrVector3f head = head_of(sc);
  auto put = [&](uint8_t meshId, uint8_t shape, const XrVector3f& p, const float q[4], float size, uint32_t c1,
                 uint32_t c2, float aux, float alpha) {
    if (n >= kSuiteParts) return;
    Instance& o = inst[n];
    o.posSize[0] = p.x;
    o.posSize[1] = p.y;
    o.posSize[2] = p.z;
    o.posSize[3] = size;
    std::memcpy(o.quat, q, sizeof(o.quat));
    o.color1 = pack(c1);
    o.color2 = pack(c2);
    o.params[0] = static_cast<float>(shape);
    o.params[1] = aux;
    o.params[2] = alpha;
    o.params[3] = 0.0f;
    meshOf[n++] = meshId;
  };
  // Turned to face you (about the vertical).
  auto facing_you = [&](const XrVector3f& p, float extra, float q[4]) {
    yaw_quat(std::atan2(head.x - p.x, head.z - p.z) + extra, q);
  };
  float q[4];
  switch (sc.suiteCaptain) {
  case 5: {  // Luigi: Boos drifting about; look at one and it hides its face (and stops creeping up)
    // Clear of the banners, King Boo (3.0, 2.0, -0.9) and the chandelier (0, 2.75, 2.4).
    static const XrVector3f kHomes[5] = {{-3.1f, 2.5f, -0.6f}, {-3.0f, 2.6f, 1.0f}, {2.6f, 2.5f, 0.9f},
                                         {1.6f, 2.8f, 2.0f}, {-0.9f, 2.7f, 2.7f}};
    float fwd[3];
    const float ahead[3] = {0.0f, 0.0f, -1.0f};
    rotate(sc.views[0].pose.orientation, ahead, fwd);
    for (int i = 0; i < 5; ++i) {
      const float fi = static_cast<float>(i);
      XrVector3f p = suite_to_room(sc, kHomes[i].x + 0.25f * std::sin(0.31f * t + fi),
                                   kHomes[i].y + 0.12f * std::sin(0.53f * t + 2.0f * fi),
                                   kHomes[i].z + 0.25f * std::cos(0.27f * t + 1.7f * fi));
      // Creeping a little toward you while you look away.
      const float creep = 0.35f * (1.0f - s.booShy[i]);
      float dx = head.x - p.x, dy = head.y - p.y, dz = head.z - p.z;
      const float dist = std::max(0.3f, std::sqrt(dx * dx + dy * dy + dz * dz));
      p.x += dx / dist * creep;
      p.y += dy / dist * creep * 0.4f;
      p.z += dz / dist * creep;
      dx = p.x - head.x;
      dy = p.y - head.y;
      dz = p.z - head.z;
      const float d = std::max(1e-3f, std::sqrt(dx * dx + dy * dy + dz * dz));
      const bool looked = (fwd[0] * dx + fwd[1] * dy + fwd[2] * dz) / d > 0.927f;  // within 22 degrees
      s.booShy[i] += ((looked ? 1.0f : 0.0f) - s.booShy[i]) * std::min(1.0f, 4.0f * sc.dt);
      facing_you(p, 0.7f * s.booShy[i], q);
      put(mesh::kBall, kBooShape, p, q, 0.5f, 0xFFFFFF, 0xFFFFFF, s.booShy[i], 0.92f);
    }
    break;
  }
  case 7:  // Peach: Power Stars turning along the walls
    for (int i = 0; i < 4; ++i) {
      const float x = (i & 1) ? 3.85f : -3.85f, z = (i & 2) ? 2.2f : -1.6f;
      yaw_quat(1.3f * t + static_cast<float>(i), q);
      put(mesh::kStar, kStar, suite_to_room(sc, x, 2.6f + 0.05f * std::sin(1.7f * t + static_cast<float>(i)), z), q,
          0.42f, 0xFFD21F, 0xFFF2A0, 0.0f, 1.0f);
    }
    break;
  case 11: {  // Yoshi: three eggs hopping in turn, butterflies about the room
    static const XrVector3f kEggs[3] = {{2.2f, 0.0f, 1.2f}, {2.6f, 0.0f, 1.5f}, {1.8f, 0.0f, 1.6f}};
    static const uint32_t kSpots[3] = {0x3CC23C, 0xE4202A, 0x2F6FE0};
    for (int i = 0; i < 3; ++i) {
      const float fi = static_cast<float>(i);
      const float hop = 0.16f * std::max(0.0f, std::sin(3.2f * t - 0.9f * fi));
      const float xAxis[3] = {1.0f, 0.0f, 0.0f};
      float tilt[4], yaw[4];
      quat_axis_angle(xAxis, 0.15f * std::sin(6.4f * t - 0.9f * fi), tilt);
      yaw_quat(sc.roomYaw + fi, yaw);
      quat_mul(yaw, tilt, q);
      put(mesh::kEgg, kEgg, suite_to_room(sc, kEggs[i].x, 0.16f + hop, kEggs[i].z), q, 0.32f, 0xFFFFFF, kSpots[i], 0.0f,
          1.0f);
    }
    static const uint32_t kWings[3] = {0xFF6FB5, 0xFFD21F, 0x4FB8FF};
    for (int b = 0; b < 3 && n + 2 <= kSuiteParts; ++b) {
      const float fb = static_cast<float>(b) * 2.1f;
      const float cx = b == 1 ? 0.0f : (b == 0 ? -2.4f : 2.4f);
      const float x = cx + 0.9f * std::sin(0.5f * t + fb), y = 1.6f + 0.3f * std::sin(1.3f * t + fb),
                  z = 0.8f + 0.7f * std::sin(0.37f * t + fb);
      const float vx = 0.45f * std::cos(0.5f * t + fb), vz = 0.26f * std::cos(0.37f * t + fb);
      const XrVector3f body = suite_to_room(sc, x, y, z);
      const float h = std::atan2(vx, vz) + sc.roomYaw;  // heading
      const float F[3] = {std::sin(h), 0.0f, std::cos(h)}, S[3] = {std::cos(h), 0.0f, -std::sin(h)};
      const float flap = 0.9f * std::sin(14.0f * t + fb);
      for (int side = -1; side <= 1; side += 2) {
        const float cf = std::cos(flap), sf = std::sin(flap);
        const float L[3] = {side * cf * S[0], sf, side * cf * S[2]};
        const float Z[3] = {F[1] * L[2] - F[2] * L[1], F[2] * L[0] - F[0] * L[2], F[0] * L[1] - F[1] * L[0]};
        quat_from_axes(F, L, Z, q);
        const float wing = 0.09f;
        const XrVector3f at{body.x + L[0] * 0.45f * wing, body.y + L[1] * 0.45f * wing, body.z + L[2] * 0.45f * wing};
        put(mesh::kPetal, kPetal, at, q, wing, kWings[b], 0x2A2A2A, 0.0f, 1.0f);
      }
    }
    break;
  }
  case 10:  // Wario: gold garlic turning on the two plinths
    for (int i = 0; i < 2; ++i) {
      yaw_quat(0.9f * t + 2.0f * static_cast<float>(i), q);
      put(mesh::kGarlic, kGarlic,
          suite_to_room(sc, i ? 3.3f : -3.3f, 1.19f + 0.03f * std::sin(1.4f * t + 3.0f * static_cast<float>(i)), -2.6f),
          q, 0.6f, 0xFFD34A, 0xC8901E, 0.0f, 1.0f);
    }
    break;
  case 9:  // Waluigi: roses drifting round the room
    for (int i = 0; i < 5; ++i) {
      const float a = 0.12f * t + static_cast<float>(i) * 1.2566f;
      const XrVector3f p = suite_to_room(sc, 2.4f * std::cos(a), 1.9f + 0.35f * std::sin(0.7f * t + static_cast<float>(i)),
                                         0.8f + 1.6f * std::sin(a));
      const float xAxis[3] = {1.0f, 0.0f, 0.0f};
      float tilt[4], yaw[4];
      quat_axis_angle(xAxis, 0.4f, tilt);
      yaw_quat(0.5f * t + static_cast<float>(i), yaw);
      quat_mul(yaw, tilt, q);
      put(mesh::kRose, kRose, p, q, 0.24f, 0xD81E3A, 0x2E8B3A, 0.0f, 1.0f);
    }
    break;
  case 1:
  case 2:
    break;  // Daisy, Donkey Kong: all theirs comes and goes (suite_live)
  default: {  // Mario: coins turning along the back wall, a 1-Up sliding to and fro, a Super Star hopping round the table
    for (int i = 0; i < 7; ++i) {
      const float fi = static_cast<float>(i);
      yaw_quat(sc.roomYaw + 2.2f * t + 0.6f * fi, q);
      put(mesh::kCoin, kCoin, suite_to_room(sc, -2.2f + 0.75f * fi, 2.5f + 0.04f * std::sin(2.0f * t + fi), 3.15f), q,
          0.3f, 0xFFC21F, 0xFFE27A, 0.0f, 1.0f);
    }
    const float along = std::cos(0.45f * t);
    yaw_quat(sc.roomYaw + 1.5707963f * std::max(-1.0f, std::min(1.0f, 4.0f * along)), q);
    put(mesh::kMushroom, kMushroom,
        suite_to_room(sc, -2.3f + 0.75f * std::sin(0.45f * t), 0.21f + 0.03f * std::fabs(std::sin(6.0f * t)), 1.0f), q,
        0.42f, 0x1FA83A, 0xFFFFFF, 0.0f, 1.0f);
    const float a = 0.7f * t;
    const XrVector3f star =
        suite_to_room(sc, 0.95f * std::cos(a), 0.75f + 0.42f * std::fabs(std::sin(2.6f * t)), 2.0f + 0.95f * std::sin(a));
    facing_you(star, 0.4f * std::sin(3.0f * t), q);
    put(mesh::kStar, kStar, star, q, 0.28f, 0xFFD21F, 0xFFF2A0, 0.0f, 1.0f);
    break;
  }
  }
  return n;
}

// r62 (Lance: "i want shit more shiny and LED looking"): the room's LED lights (Room::leds) live: a
// bright pulse running along the strips round the walls, marquee bulbs chasing round the big screen
// and the name plaque, in Mario-logo colours. Glowing dots just in front of the baked strips and bulbs.
constexpr uint32_t kLedParts = 512;
static uint32_t write_leds(const Scene& sc, Instance* inst) {
  if (!sc.roomShown || s.roomLeds.empty()) return 0;
  static const uint32_t kColours[4] = {0xFF2A1A, 0xFFC21F, 0x2BE84A, 0x3A7BFF};
  const float t = s.suiteTime;
  const XrVector3f head = head_of(sc);
  const int chase = static_cast<int>(t * 7.0f);
  uint32_t n = 0;
  for (size_t i = 0; i + Room::kLedFloats <= s.roomLeds.size() && n < kLedParts; i += Room::kLedFloats) {
    const float* l = &s.roomLeds[i];
    const int group = static_cast<int>(l[3]), order = static_cast<int>(l[4]), colour = static_cast<int>(l[5]) & 3;
    XrVector3f p = suite_to_room(sc, l[0], l[1], l[2]);
    // A little toward you, in front of the bulb or the strip it lights.
    const float dx = head.x - p.x, dy = head.y - p.y, dz = head.z - p.z;
    const float d = std::max(1e-3f, std::sqrt(dx * dx + dy * dy + dz * dz));
    p.x += 0.03f * dx / d;
    p.y += 0.03f * dy / d;
    p.z += 0.03f * dz / d;
    float bright, size;
    if (group == 0) {
      const float wave = 0.5f + 0.5f * std::sin(static_cast<float>(order) * 0.21f - t * 3.2f);
      bright = 0.3f + 0.7f * wave * wave * wave * wave;
      size = 0.07f;
    } else {
      const bool lit = (order + chase) % 3 == 0;
      bright = lit ? 1.0f : 0.22f;
      size = lit ? 0.075f : 0.05f;
    }
    Instance& o = inst[n++];
    o.posSize[0] = p.x;
    o.posSize[1] = p.y;
    o.posSize[2] = p.z;
    o.posSize[3] = size;
    std::memset(o.quat, 0, sizeof(o.quat));  // faces you
    o.color1 = pack(bright > 0.8f ? 0xFFFFFF : kColours[colour]);
    o.color2 = pack(kColours[colour]);
    o.params[0] = static_cast<float>(kSpark);
    o.params[1] = 1.0f;
    o.params[2] = bright;
    o.params[3] = 0.9f;
  }
  return n;
}

// r60: the outlines and the pointer dots (Scene::frames, Scene::pointers), drawn last, over
// everything (no depth test), so a pointer always sees what it is on.
constexpr uint32_t kUiParts = 6;
static bool ui_shown(const Scene& sc) { return sc.frameCount > 0 || sc.pointerValid[0] || sc.pointerValid[1]; }
static uint32_t write_ui(const Scene& sc, Instance* inst) {
  uint32_t n = 0;
  for (int i = 0; i < sc.frameCount && i < 4 && n < kUiParts; ++i) {
    const Scene::Frame& f = sc.frames[i];
    if (f.width <= 0.0f || f.height <= 0.0f) continue;
    Instance& o = inst[n++];
    o.posSize[0] = f.pose.position.x;
    o.posSize[1] = f.pose.position.y;
    o.posSize[2] = f.pose.position.z;
    o.posSize[3] = f.height;
    o.quat[0] = f.pose.orientation.x;
    o.quat[1] = f.pose.orientation.y;
    o.quat[2] = f.pose.orientation.z;
    o.quat[3] = f.pose.orientation.w;
    // The band's width over half the height (the shader's units), in colour 2's alpha.
    const float t = std::max(0.004f, std::min(1.0f, f.thickness / (0.5f * f.height)));
    o.color1 = pack(f.rgb);
    o.color2 = pack(f.rgb, static_cast<uint8_t>(t * 255.0f + 0.5f));
    o.params[0] = static_cast<float>(kFrameShape);
    o.params[1] = f.width / f.height;
    o.params[2] = std::max(0.0f, std::min(1.0f, f.alpha));
    o.params[3] = 0.35f;
  }
  for (int h = 0; h < 2 && n < kUiParts; ++h) {
    if (!sc.pointerValid[h]) continue;
    Instance& o = inst[n++];
    o.posSize[0] = sc.pointers[h].x;
    o.posSize[1] = sc.pointers[h].y;
    o.posSize[2] = sc.pointers[h].z;
    o.posSize[3] = 0.03f;
    std::memset(o.quat, 0, sizeof(o.quat));  // faces you
    o.color1 = pack(0xFFFFFF);
    o.color2 = pack(0xFFD23F);
    o.params[0] = static_cast<float>(kSpark);
    o.params[1] = 1.0f;
    o.params[2] = 1.0f;
    o.params[3] = 0.3f;
  }
  return n;
}

void cup_colours(int trophy, uint32_t* cup, uint32_t* base) {
  const CupLook& look = kCups[trophy >= 0 && trophy < 8 ? trophy : 0];
  *cup = look.cup;
  *base = look.base;
}

void team_colours(int captain, uint32_t* main, uint32_t* accent) {
  const Theme& t = captain >= 0 && captain < 12 ? kThemes[captain] : kNeutral;
  *main = t.paper[0];
  *accent = t.paper[1] != 0xFFFFFF ? t.paper[1] : t.paper[2];
  if (*accent == 0xFFFFFF) *accent = 0xFFD21F;
}

// The game reports its flash once a game frame; between reports (the display runs faster, or the
// game frame is slow) the last one holds, and it is over once they stop.
static bool wash_active() { return s.washAge < 0.1f && s.washAlpha > 0.003f; }

void screen_flash(float r, float g, float b, float a) {
  if (!wash_active() && a > 0.003f) {
    FXLOG("fx: game screen flash (%.2f %.2f %.2f) -> whole view", r, g, b);
    s.washPeak = 0.0f;
  }
  s.washRgb[0] = r;
  s.washRgb[1] = g;
  s.washRgb[2] = b;
  // A touch under full: a solid white field right at the eyes is harsher than on a TV.
  s.washAlpha = std::min(std::max(a, 0.0f), 1.0f) * 0.9f;
  s.washAge = 0.0f;
  s.washPeak = std::max(s.washPeak, s.washAlpha);
  if (s.washDumpArmed && s.washAlpha >= 0.5f) {
    s.washDumpArmed = false;
    request_dump("/storage/emulated/0/Strikers/xr_fx.ppm");
  }
}

void dump_at_flash() { s.washDumpArmed = true; }

void blast(const XrVector3f& at, const XrVector3f& dir, float unit, float strength, int kind) {
  if (s.pendingBlasts.size() >= 8) return;  // a chain reaction's worth
  s.pendingBlasts.push_back(State::Blast{at, dir, unit, strength, kind});
}

bool update(const Scene& sc) {
  // r54: the controllers' models, once loaded (here: render only runs while something shows).
  if (s.ready) take_models();
  // r55: item blasts since the last update.
  for (const State::Blast& b : s.pendingBlasts) spawn_blast(sc, b);
  s.pendingBlasts.clear();
  const float dt = std::min(std::max(sc.dt, 0.0f), 0.1f);
  // r57: a cup just won sparkles on the shelf for a few seconds.
  if (shelf_shown(sc) && sc.shelfNewCup >= 0 && sc.shelfNewCup < 8 && sc.shelfNewAge < 6.0f) {
    s.shelfSparkAccum += 30.0f * dt;
    float x, y;
    shelf_slot(sc.shelfNewCup, &x, &y);
    while (s.shelfSparkAccum >= 1.0f) {
      s.shelfSparkAccum -= 1.0f;
      const XrVector3f p = shelf_to_room(sc, x + frand(-0.06f, 0.06f), y + frand(-0.05f, 0.09f), frand(-0.05f, 0.06f));
      const float v[3] = {frand(-0.05f, 0.05f), frand(0.05f, 0.2f), frand(-0.05f, 0.05f)};
      add_spark(p, v, frand(0.006f, 0.012f), 0xFFFFFF, irand(2) ? 0xFFD21F : 0xFFF2A0, frand(0.4f, 0.8f), 1.0f, 0.1f);
    }
  }
  s.stageTime += dt;
  s.sinceGoal += dt;
  s.markerTime += dt;
  s.washAge += dt;
  const bool washing = wash_active();

  // The jumbotron: a slightly bouncy spring.
  const float acc = 70.0f * (s.dropTarget - s.drop) - 10.0f * s.dropVel;
  s.dropVel += acc * dt;
  s.drop += s.dropVel * dt;
  if (s.dropTarget == 0.0f && s.drop < 0.002f && std::fabs(s.dropVel) < 0.01f) s.drop = s.dropVel = 0.0f;

  // r61: what starts on its own (the Super Strike ball's sparks, the cup suite's life) starts here,
  // ahead of the idle check below: r61's first cut had the ball's sparks after it, so with nothing
  // else flying they never began.
  home_sparks(sc, dt);
  suite_live(sc, dt);
  const bool steady = marker_active(sc) || washing || sc.tableUnderside || hands_shown(sc) || shelf_shown(sc) ||
                      ui_shown(sc) || sc.homeCount > 0 || sc.roomShown;
  if (s.stage == Stage::Idle && s.parts.empty() && s.hatsLeft == 0) {
    return s.drop > 0.0f || steady;
  }
  const Scale k = scale_for(sc);

  // r43c: the hat trick's hats, thrown over about two and a half seconds.
  if (s.hatsLeft > 0) {
    s.hatAccum += dt * (sc.mr ? 18.0f : 44.0f);
    while (s.hatAccum >= 1.0f && s.hatsLeft > 0) {
      s.hatAccum -= 1.0f;
      --s.hatsLeft;
      throw_hat(sc, k);
    }
  }

  if (s.stage == Stage::Raining && s.stageTime > 30.0f) {  // in case the replay never reports
    s.stage = Stage::Tapering;
    s.stageTime = 0.0f;
  }

  if (s.pendingFlash) {
    s.pendingFlash = false;
    add_flash(sc);
  }
  if (s.cannonWavesLeft > 0) {
    s.cannonTimer -= dt;
    if (s.cannonTimer <= 0.0f) {
      fire_cannon_wave(sc, k);
      --s.cannonWavesLeft;
      s.cannonTimer = kWaveGap;
    }
  }
  if (s.jetTime > 0.0f) {
    s.jetTime -= dt;
    fire_jets(sc, k, dt);
  }

  // Rain from above, streamers among it.
  float rate = 0.0f;
  if (s.stage == Stage::Raining) {
    rate = k.rainRate * s.intensity * std::min(1.0f, 0.4f + s.stageTime);
  } else if (s.stage == Stage::Tapering) {
    rate = k.rainRate * s.intensity * std::exp(-s.stageTime / 1.3f);
    if (s.stageTime < 8.0f) rate += (sc.mr ? 2.0f : 8.0f) * (1.0f - s.stageTime / 8.0f);  // stragglers
    if (s.stageTime > 8.0f) s.stage = Stage::Idle;
  }
  s.rainAccum += rate * dt;
  const XrVector3f head = head_of(sc);
  const RainArea area = rain_area(sc, k);
  const XrVector3f center = area.centre;
  const float top = area.top;
  while (s.rainAccum >= 1.0f) {
    s.rainAccum -= 1.0f;
    const float a = frand(0.0f, 6.2831853f), r = k.rainRadius * std::sqrt(frand(0.0f, 1.0f));
    const XrVector3f p{center.x + std::cos(a) * r, top + frand(0.0f, sc.mr ? 0.2f : 2.0f), center.z + std::sin(a) * r};
    const float v[3] = {0.0f, -k.paperFall * 0.5f, 0.0f};
    const int roll = irand(100);
    if (roll < 9) add_piece(sc, k, p, v);
    else add_paper(sc, k, p, v, roll < (sc.mr ? 21 : 13));  // streamers: fewer in the stadium
  }
  // A few signature pieces drift down close around you, where their shape reads best (never
  // closer than 40 cm to your head).
  if (s.stage == Stage::Raining) {
    s.nearAccum += (sc.mr ? 6.0f : 10.0f) * s.intensity * dt;
    while (s.nearAccum >= 1.0f) {
      s.nearAccum -= 1.0f;
      const float a = frand(0.0f, 6.2831853f), r = sc.mr ? frand(0.4f, 1.0f) : frand(0.7f, 2.0f);
      const XrVector3f p{head.x + std::cos(a) * r, top + frand(0.0f, sc.mr ? 0.2f : 1.0f), head.z + std::sin(a) * r};
      const float v[3] = {0.0f, -k.pieceFall * 0.5f, 0.0f};
      add_piece(sc, k, p, v);
    }
  }

  // Fireworks: a barrage at the goal, then steadily while the party lasts.
  if (s.stage == Stage::Raining || (s.stage == Stage::Tapering && s.stageTime < 1.0f)) {
    s.rocketTimer -= dt;
    if (s.rocketTimer <= 0.0f) {
      launch_firework(sc, k);
      if (s.pendingRockets > 0) {
        --s.pendingRockets;
        s.rocketTimer = sc.mr ? frand(0.2f, 0.35f) : frand(0.08f, 0.18f);
      } else if (sc.mr) {
        s.rocketTimer = s.superStrike ? frand(0.4f, 0.6f) : frand(0.8f, 1.2f);
      } else {
        s.rocketTimer = s.superStrike ? frand(0.15f, 0.25f) : frand(0.3f, 0.45f);
      }
    }
  }

  step_particles(sc, k, dt);
  return !s.parts.empty() || s.stage != Stage::Idle || s.drop > 0.0f || steady;
}

bool render(const Scene& sc, XrCompositionLayerProjection* layer) {
  const bool marker = marker_active(sc);
  const bool washing = wash_active();
  const bool hands = hands_shown(sc);
  const bool shelf = shelf_shown(sc);
  const bool ui = ui_shown(sc);
  const bool home = sc.homeCount > 0;  // r61
  const bool suite = sc.roomShown;     // r61: the cup suite's live things
  if (s.ready) take_room(false);       // r61: a room let go (render_room puts new ones up)
  if (!s.ready ||
      (s.parts.empty() && !marker && !washing && !sc.tableUnderside && !hands && !shelf && !ui && !home && !suite)) {
    return false;
  }
  Slot& slot = s.slots[s.slot];
  s.slot = (s.slot + 1) % kSlots;
  if (slot.pending) {
    vkWaitForFences(s.dev.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(s.dev.device, 1, &slot.fence);
    slot.pending = false;
  }

  // Instances, grouped: the 3D pieces mesh by mesh, then the quads (sparks, rockets, the goal
  // flash), then the marker and the screen flash, for which two slots stay free.
  // Buckets: full-detail meshes, reduced meshes (pieces far away or lying still), then quads.
  constexpr uint32_t kQuadBucket = 2 * mesh::kCount;
  const XrVector3f head = head_of(sc);
  const float farSq = sc.mr ? 1.1f * 1.1f : 4.0f * 4.0f;
  auto bucket_of = [&](const Particle& q) -> uint32_t {
    if ((q.kind != kKindPaper && q.kind != kKindPiece) || q.mesh >= mesh::kCount) return kQuadBucket;
    if (q.kind == kKindPaper) return q.mesh;  // a few triangles either way
    const float dx = q.p[0] - head.x, dy = q.p[1] - head.y, dz = q.p[2] - head.z;
    const bool far = q.landed || dx * dx + dy * dy + dz * dz > farSq;
    return q.mesh + (far ? mesh::kCount : 0u);
  };
  uint32_t first[kQuadBucket + 1] = {}, filled[kQuadBucket + 1] = {};
  uint32_t count = 0;
  for (const Particle& q : s.parts) {
    // r45d: three left: the marker, the flash, the underside; r53: and the controllers; r57: the shelf.
    if (count >= kMaxParticles - 3 - kHandParts - kShelfParts - kUiParts - kHomeParts - kSuiteParts - kLedParts) break;
    ++filled[bucket_of(q)];
    ++count;
  }
  for (uint32_t b = 0, at = 0; b <= kQuadBucket; ++b) {
    first[b] = at;
    at += filled[b];
    filled[b] = 0;
  }
  auto* inst = static_cast<Instance*>(slot.instanceMapped);
  uint32_t written = 0;
  for (const Particle& q : s.parts) {
    if (written++ >= count) break;
    float alpha = 1.0f;
    if (q.kind == kKindSpark || q.kind == kKindEmber) {
      const float t = q.age / q.life;
      alpha = 1.0f - t * t;
    } else if (q.landed) {
      alpha = std::max(0.0f, 1.0f - q.age / std::max(q.life, 0.05f));  // r44: the whole rest is the fade
    } else if (q.maxAge > 0.0f) {
      alpha = std::max(0.0f, std::min(1.0f, (q.maxAge - q.age) / 0.6f));  // r61: a suite piece's last moments
    }
    const uint32_t b = bucket_of(q);
    Instance& o = inst[first[b] + filled[b]++];
    o.posSize[0] = q.p[0];
    o.posSize[1] = q.p[1];
    o.posSize[2] = q.p[2];
    // r44: and shrinks as it fades (the 3D pieces write depth, so alpha alone reads as a pop).
    o.posSize[3] = (q.landed || q.maxAge > 0.0f) ? q.size * (0.3f + 0.7f * alpha) : q.size;
    std::memcpy(o.quat, q.q, sizeof(o.quat));
    o.color1 = q.c1;
    o.color2 = q.c2;
    o.params[0] = static_cast<float>(q.shape);
    o.params[1] = q.aspect;
    o.params[2] = alpha;
    o.params[3] = q.glow;
  }
  // The player marker, after the particles: an arrow bobbing over his head, scaled with the table
  // (but never too small to read). r24 also had a ring at his feet; Lance: the arrow suffices.
  uint32_t markerCount = 0;
  if (marker) {
    const float scale = sc.tableScale;
    const float bob = 0.25f * std::sin(s.markerTime * 5.0f);
    const XrVector3f above = game_to_room(scale, sc.tableYaw, sc.tablePos, s.markerGame[0], s.markerGame[1],
                                          s.markerGame[2] + 3.3f * sc.charScale + bob);
    const uint32_t colour = pack(s.markerColour);
    Instance& arrow = inst[count + markerCount++];
    arrow.posSize[0] = above.x;
    arrow.posSize[1] = above.y;
    arrow.posSize[2] = above.z;
    arrow.posSize[3] = std::max(0.03f, 1.7f * scale);
    std::memset(arrow.quat, 0, sizeof(arrow.quat));  // faces you from any side
    arrow.color1 = colour;
    arrow.color2 = colour;
    arrow.params[0] = static_cast<float>(kMarkerArrow);
    arrow.params[1] = 1.0f;
    arrow.params[2] = 1.0f;
    arrow.params[3] = 0.0f;
  }
  // The game's screen flash, last, over everything in your view.
  if (washing) {
    Instance& wash = inst[count + markerCount++];
    std::memset(&wash, 0, sizeof(wash));
    auto byte = [](float c) { return static_cast<uint32_t>(std::min(std::max(c, 0.0f), 1.0f) * 255.0f + 0.5f); };
    wash.color1 = pack((byte(s.washRgb[0]) << 16) | (byte(s.washRgb[1]) << 8) | byte(s.washRgb[2]));
    wash.color2 = wash.color1;
    wash.params[0] = static_cast<float>(kWash);
    wash.params[1] = 1.0f;
    wash.params[2] = s.washAlpha;
  }
  // r45d (Lance: when the table rises over you, "make the bottom of the field solid so i dont see
  // right through the field when its above me"): a solid dark card the size of the pitch, just under
  // its ground (below the stand-in box's floor, so the box keeps hiding what falls on the field).
  // Only while your eyes are below the field; from above the game's own field is what you see.
  const uint32_t undersideAt = count + markerCount;
  const bool underside = sc.tableUnderside && sc.tabletop;
  if (underside) {
    Instance& u = inst[undersideAt];
    const XrVector3f c = game_to_room(sc.tableScale, sc.tableYaw, sc.tablePos, 0.0f, 0.0f, -0.3f);
    u.posSize[0] = c.x;
    u.posSize[1] = c.y;
    u.posSize[2] = c.z;
    u.posSize[3] = 27.0f * sc.tableScale;  // across the pitch (game y), with the fence
    const float xAxis[3] = {1.0f, 0.0f, 0.0f}, yAxis[3] = {0.0f, 1.0f, 0.0f};
    float flat[4], yaw[4];
    quat_axis_angle(xAxis, 1.5707963f, flat);    // the card's face down
    quat_axis_angle(yAxis, sc.tableYaw, yaw);    // its length along the pitch
    quat_mul(yaw, flat, u.quat);
    u.color1 = pack(0x22301F);
    u.color2 = pack(0x22301F);
    u.params[0] = static_cast<float>(kPaper);
    u.params[1] = 45.0f / 27.0f;                 // along the pitch (game x)
    u.params[2] = 1.0f;
    u.params[3] = 0.0f;
  }
  // r53: the controllers, after the underside's slot.
  const uint32_t handAt = undersideAt + 1;
  HandDraw handDraw[kHandParts];
  const uint32_t handCount = hands ? write_hands(sc, inst + handAt, handDraw) : 0;
  // r57: the trophy shelf, after the controllers' slots.
  const uint32_t shelfAt = handAt + kHandParts;
  uint8_t shelfMesh[kShelfParts];
  const uint32_t shelfCount = shelf ? write_shelf(sc, inst + shelfAt, shelfMesh) : 0;
  // r60: the outlines and pointer dots, after the shelf's slots.
  const uint32_t uiAt = shelfAt + kShelfParts;
  const uint32_t uiCount = ui ? write_ui(sc, inst + uiAt) : 0;
  // r61: the home ground's pieces, after the outlines' slots.
  const uint32_t homeAt = uiAt + kUiParts;
  uint8_t homeMesh[kHomeParts];
  const uint32_t homeCount = home ? write_home(sc, inst + homeAt, homeMesh) : 0;
  // r61: the cup suite's live things, after the home ground's slots.
  const uint32_t suiteAt = homeAt + kHomeParts;
  uint8_t suiteMesh[kSuiteParts];
  const uint32_t suiteCount = suite ? write_suite(sc, inst + suiteAt, suiteMesh) : 0;
  const bool roomDepth = suite && s.roomIndexCount > 0;
  // r62: its LED lights, after the suite's slots.
  const uint32_t ledAt = suiteAt + kSuiteParts;
  const uint32_t ledCount = roomDepth ? write_leds(sc, inst + ledAt) : 0;
  uint32_t indices = 0;
  for (uint32_t b = 0; b < kQuadBucket; ++b) indices += filled[b] * s.meshRanges[b / mesh::kCount][b % mesh::kCount].indexCount;
  s.statPeakParts = std::max(s.statPeakParts, count);
  s.statPeakIndices = std::max(s.statPeakIndices, indices);
  const uint32_t occluderVerts = write_occluders(sc, static_cast<float*>(slot.occluderMapped));

  uint32_t index[2] = {0, 0};
  for (int e = 0; e < 2; ++e) {
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!xr_ok(xrAcquireSwapchainImage(s.eyes[e].swapchain, &acquire, &index[e]), "xrAcquireSwapchainImage(fx)")) {
      return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    xr_ok(xrWaitSwapchainImage(s.eyes[e].swapchain, &wait), "xrWaitSwapchainImage(fx)");
  }

  vkResetCommandBuffer(slot.cmd, 0);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(slot.cmd, &begin);
  for (int e = 0; e < 2; ++e) {
    VkClearValue clears[2];
    clears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = s.renderPass;
    rp.framebuffer = s.eyes[e].framebuffers[index[e]];
    rp.renderArea = {{0, 0}, {s.dev.width, s.dev.height}};
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(slot.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(s.dev.width), static_cast<float>(s.dev.height), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {s.dev.width, s.dev.height}};
    vkCmdSetViewport(slot.cmd, 0, 1, &vp);
    vkCmdSetScissor(slot.cmd, 0, 1, &scissor);

    PushConstants pc{};
    float proj[16], view[16];
    projection(sc.views[e].fov, 0.05f, 600.0f, proj);
    view_matrix(sc.views[e].pose, view);
    mat_mul(proj, view, pc.viewProj);
    const float ex[3] = {1, 0, 0}, ey[3] = {0, 1, 0};
    rotate(sc.views[e].pose.orientation, ex, pc.camRight);
    rotate(sc.views[e].pose.orientation, ey, pc.camUp);
    pc.camPos[0] = sc.views[e].pose.position.x;
    pc.camPos[1] = sc.views[e].pose.position.y;
    pc.camPos[2] = sc.views[e].pose.position.z;
    const VkDeviceSize zero = 0;
    // r53: the controllers first, so the stand-in solids (the table's box, the banner) never hide a
    // hand that reaches into them.
    if (handCount > 0) {
      vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.meshPipeline);
      vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
      int bound = -1;
      for (uint32_t i = 0; i < handCount; ++i) {
        const CtrlMesh& cm = s_ctrl[handDraw[i].hand];
        if (bound != handDraw[i].hand) {
          bound = handDraw[i].hand;
          const VkBuffer buffers[2] = {cm.verts, slot.instances};
          const VkDeviceSize offsets[2] = {0, 0};
          vkCmdBindVertexBuffers(slot.cmd, 0, 2, buffers, offsets);
          vkCmdBindIndexBuffer(slot.cmd, cm.indices, 0, VK_INDEX_TYPE_UINT32);
        }
        const xrctrl::Part& part = cm.parts[handDraw[i].part];
        vkCmdDrawIndexed(slot.cmd, part.indexCount, 1, part.firstIndex, 0, handAt + i);
      }
    }
    if (occluderVerts > 0) {
      vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.occluderPipeline);
      vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
      vkCmdBindVertexBuffers(slot.cmd, 0, 1, &slot.occluders, &zero);
      vkCmdDraw(slot.cmd, occluderVerts, 1, 0, 0);
    }
    // r61: the cup suite's room (its own layer, under this one) as depth, so its walls and things
    // hide what is behind them.
    if (roomDepth) {
      PushConstants rpc = pc;
      float model[16];
      room_model(sc, model);
      mat_mul(pc.viewProj, model, rpc.viewProj);
      vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.roomDepthPipeline);
      vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(rpc), &rpc);
      vkCmdBindVertexBuffers(slot.cmd, 0, 1, &s.roomVerts, &zero);
      vkCmdBindIndexBuffer(slot.cmd, s.roomIndices, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(slot.cmd, s.roomIndexCount, 1, 0, 0, 0);
    }
    // r57: the trophy shelf (solid, depth tested and written like the pieces).
    if (shelfCount > 0) {
      vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.meshPipeline);
      vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
      const VkBuffer buffers[2] = {s.meshVerts, slot.instances};
      const VkDeviceSize offsets[2] = {0, 0};
      vkCmdBindVertexBuffers(slot.cmd, 0, 2, buffers, offsets);
      vkCmdBindIndexBuffer(slot.cmd, s.meshIndices, 0, VK_INDEX_TYPE_UINT16);
      for (uint32_t i = 0; i < shelfCount; ++i) {
        const mesh::Range& range = s.meshRanges[0][shelfMesh[i]];
        if (range.indexCount == 0) continue;
        vkCmdDrawIndexed(slot.cmd, range.indexCount, 1, range.firstIndex, 0, shelfAt + i);
      }
    }
    // r61: the home ground's banners, pennants and ball (solid, like the shelf).
    if (homeCount > 0) {
      vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.meshPipeline);
      vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
      const VkBuffer buffers[2] = {s.meshVerts, slot.instances};
      const VkDeviceSize offsets[2] = {0, 0};
      vkCmdBindVertexBuffers(slot.cmd, 0, 2, buffers, offsets);
      vkCmdBindIndexBuffer(slot.cmd, s.meshIndices, 0, VK_INDEX_TYPE_UINT16);
      for (uint32_t i = 0; i < homeCount; ++i) {
        const mesh::Range& range = s.meshRanges[0][homeMesh[i]];
        if (range.indexCount == 0) continue;
        vkCmdDrawIndexed(slot.cmd, range.indexCount, 1, range.firstIndex, 0, homeAt + i);
      }
    }
    // r61: the cup suite's live things (solid, like the shelf).
    if (suiteCount > 0) {
      vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.meshPipeline);
      vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
      const VkBuffer buffers[2] = {s.meshVerts, slot.instances};
      const VkDeviceSize offsets[2] = {0, 0};
      vkCmdBindVertexBuffers(slot.cmd, 0, 2, buffers, offsets);
      vkCmdBindIndexBuffer(slot.cmd, s.meshIndices, 0, VK_INDEX_TYPE_UINT16);
      for (uint32_t i = 0; i < suiteCount; ++i) {
        const mesh::Range& range = s.meshRanges[0][suiteMesh[i]];
        if (range.indexCount == 0) continue;
        vkCmdDrawIndexed(slot.cmd, range.indexCount, 1, range.firstIndex, 0, suiteAt + i);
      }
    }
    // The 3D pieces first (solid, depth written), then the glowing quads among them.
    if (first[kQuadBucket] > 0) {
      vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.meshPipeline);
      vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
      const VkBuffer buffers[2] = {s.meshVerts, slot.instances};
      const VkDeviceSize offsets[2] = {0, 0};
      vkCmdBindVertexBuffers(slot.cmd, 0, 2, buffers, offsets);
      vkCmdBindIndexBuffer(slot.cmd, s.meshIndices, 0, VK_INDEX_TYPE_UINT16);
      for (uint32_t b = 0; b < kQuadBucket; ++b) {
        const mesh::Range& range = s.meshRanges[b / mesh::kCount][b % mesh::kCount];
        if (filled[b] == 0 || range.indexCount == 0) continue;
        vkCmdDrawIndexed(slot.cmd, range.indexCount, filled[b], range.firstIndex, 0, first[b]);
      }
    }
    vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.fxPipeline);
    vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    vkCmdBindVertexBuffers(slot.cmd, 0, 1, &slot.instances, &zero);
    if (underside) {
      vkCmdDraw(slot.cmd, 4, 1, 0, undersideAt);  // r45d: after the solid pieces, before the glowing quads
    }
    if (filled[kQuadBucket] > 0) {
      vkCmdDraw(slot.cmd, 4, filled[kQuadBucket], 0, first[kQuadBucket]);
    }
    if (ledCount > 0) vkCmdDraw(slot.cmd, 4, ledCount, 0, ledAt);  // r62: the suite's LEDs
    if (markerCount > 0) {
      vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.markerPipeline);
      vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
      vkCmdDraw(slot.cmd, 4, markerCount, 0, count);
    }
    if (uiCount > 0) {  // r60
      vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.markerPipeline);
      vkCmdPushConstants(slot.cmd, s.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
      vkCmdDraw(slot.cmd, 4, uiCount, 0, uiAt);
    }
    vkCmdEndRenderPass(slot.cmd);
  }
  // Self-test: copy the left eye out before it goes back to the runtime.
  const bool dumping = s.dumpPath[0] != '\0' &&
                       (s.dumpBuffer != VK_NULL_HANDLE ||
                        make_host_buffer(VkDeviceSize(s.dev.width) * s.dev.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                         &s.dumpBuffer, &s.dumpMemory, &s.dumpMapped));
  if (dumping) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = s.eyes[0].images[index[0]].image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(slot.cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {s.dev.width, s.dev.height, 1};
    vkCmdCopyImageToBuffer(slot.cmd, b.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s.dumpBuffer, 1, &region);
    VkImageMemoryBarrier back = b;
    back.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    back.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    back.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    back.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(slot.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &back);
  }
  vkEndCommandBuffer(slot.cmd);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &slot.cmd;
  if (vk_ok(vkQueueSubmit(s.dev.queue, 1, &submit, slot.fence), "vkQueueSubmit(fx)")) slot.pending = true;
  if (dumping && slot.pending) {
    vkWaitForFences(s.dev.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    if (FILE* f = std::fopen(s.dumpPath, "wb")) {
      std::fprintf(f, "P6\n%u %u\n255\n", s.dev.width, s.dev.height);
      const auto* px = static_cast<const uint8_t*>(s.dumpMapped);
      std::vector<uint8_t> row(size_t(s.dev.width) * 3);
      const bool bgra = s.colorFormat == VK_FORMAT_B8G8R8A8_SRGB;
      for (uint32_t y = 0; y < s.dev.height; ++y) {
        for (uint32_t x = 0; x < s.dev.width; ++x) {
          const uint8_t* p = px + (size_t(y) * s.dev.width + x) * 4;
          const uint8_t r = bgra ? p[2] : p[0], gch = p[1], bch = bgra ? p[0] : p[2], a = p[3];
          // Premultiplied over mid grey.
          row[x * 3 + 0] = static_cast<uint8_t>(std::min(255, r + (128 * (255 - a)) / 255));
          row[x * 3 + 1] = static_cast<uint8_t>(std::min(255, gch + (128 * (255 - a)) / 255));
          row[x * 3 + 2] = static_cast<uint8_t>(std::min(255, bch + (128 * (255 - a)) / 255));
        }
        std::fwrite(row.data(), 1, row.size(), f);
      }
      std::fclose(f);
      FXLOG("fx: dumped left eye (%zu pieces) to %s", s.parts.size(), s.dumpPath);
    }
    s.dumpPath[0] = '\0';
  }

  for (int e = 0; e < 2; ++e) {
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr_ok(xrReleaseSwapchainImage(s.eyes[e].swapchain, &release), "xrReleaseSwapchainImage(fx)");
    s.layerViews[e] = XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    s.layerViews[e].pose = sc.views[e].pose;
    s.layerViews[e].fov = sc.views[e].fov;
    s.layerViews[e].subImage.swapchain = s.eyes[e].swapchain;
    s.layerViews[e].subImage.imageRect = {{0, 0}, {static_cast<int32_t>(s.dev.width), static_cast<int32_t>(s.dev.height)}};
    s.layerViews[e].subImage.imageArrayIndex = 0;
  }
  *layer = XrCompositionLayerProjection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  layer->layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  layer->space = s.dev.local;
  layer->viewCount = 2;
  layer->views = s.layerViews;
  return true;
}

float jumbotron_drop() { return s.drop; }

bool take_stats(uint32_t* peakParts, uint32_t* peakIndices) {
  *peakParts = s.statPeakParts;
  *peakIndices = s.statPeakIndices;
  s.statPeakParts = s.statPeakIndices = 0;
  return *peakParts > 0;
}

void request_dump(const char* path) {
  std::snprintf(s.dumpPath, sizeof(s.dumpPath), "%s", path);
}

void set_room(Room&& room) {
  std::lock_guard<std::mutex> lock(g_roomMutex);
  g_roomNext = std::move(room);
  g_roomPending = true;
}

bool render_room(const Scene& sc, XrCompositionLayerProjection* layer) {
  if (!s.ready) return false;
  if (sc.roomShown) make_room_layer();
  take_room(s.roomLayerReady);
  if (!sc.roomShown || !s.roomLayerReady || s.roomIndexCount == 0) return false;
  Slot& slot = s.roomSlots[s.roomSlot];
  s.roomSlot = (s.roomSlot + 1) % kSlots;
  if (slot.pending) {
    vkWaitForFences(s.dev.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(s.dev.device, 1, &slot.fence);
    slot.pending = false;
  }
  const uint32_t w = s.dev.roomWidth > 0 ? s.dev.roomWidth : s.dev.width;
  const uint32_t h = s.dev.roomHeight > 0 ? s.dev.roomHeight : s.dev.height;
  uint32_t index[2] = {0, 0};
  for (int e = 0; e < 2; ++e) {
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!xr_ok(xrAcquireSwapchainImage(s.roomEyes[e].swapchain, &acquire, &index[e]), "xrAcquireSwapchainImage(room)")) {
      if (e == 1) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(s.roomEyes[0].swapchain, &release);
      }
      return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    xr_ok(xrWaitSwapchainImage(s.roomEyes[e].swapchain, &wait), "xrWaitSwapchainImage(room)");
  }
  float model[16];
  room_model(sc, model);
  vkResetCommandBuffer(slot.cmd, 0);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(slot.cmd, &begin);
  for (int e = 0; e < 2; ++e) {
    VkClearValue clears[2];
    clears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = s.roomRenderPass != VK_NULL_HANDLE ? s.roomRenderPass : s.renderPass;  // r62: multisampled
    rp.framebuffer = s.roomEyes[e].framebuffers[index[e]];
    rp.renderArea = {{0, 0}, {w, h}};
    rp.clearValueCount = 2;
    rp.pClearValues = clears;
    vkCmdBeginRenderPass(slot.cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport vp{0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h), 0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, {w, h}};
    vkCmdSetViewport(slot.cmd, 0, 1, &vp);
    vkCmdSetScissor(slot.cmd, 0, 1, &scissor);
    // Near at 10 cm: the 16-bit depth keeps the rug off the floor across the room (you can still put
    // your nose nearly to a wall).
    float proj[16], view[16], viewProj[16];
    projection(sc.views[e].fov, 0.1f, 600.0f, proj);
    view_matrix(sc.views[e].pose, view);
    mat_mul(proj, view, viewProj);
    // r62: the eye in the room's own space (the model's inverse), for the live highlights.
    RoomPush push{};
    mat_mul(viewProj, model, push.mvp);
    {
      const float c = std::cos(sc.roomYaw), sn = std::sin(sc.roomYaw);
      const float dx = sc.views[e].pose.position.x - sc.roomOrigin.x, dz = sc.views[e].pose.position.z - sc.roomOrigin.z;
      push.eye[0] = dx * c - dz * sn;
      push.eye[1] = sc.views[e].pose.position.y - sc.roomOrigin.y;
      push.eye[2] = dx * sn + dz * c;
      push.eye[3] = s.suiteTime;
      std::memcpy(push.light, s.roomLamp, sizeof(s.roomLamp));
    }
    const VkShaderStageFlags stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    const VkDeviceSize zero = 0;
    vkCmdBindPipeline(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.roomPipeline);
    vkCmdPushConstants(slot.cmd, s.roomLayout, stages, 0, sizeof(push), &push);
    vkCmdBindDescriptorSets(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.roomLayout, 0, 1, &s.roomSet, 0, nullptr);
    vkCmdBindVertexBuffers(slot.cmd, 0, 1, &s.roomVerts, &zero);
    vkCmdBindIndexBuffer(slot.cmd, s.roomIndices, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(slot.cmd, s.roomIndexCount, 1, 0, 0, 0);
    // The world outside: the dome round this eye (so it never comes nearer), turned with the suite,
    // filling only what the room leaves open (the window).
    if (s.outsideTex.image != VK_NULL_HANDLE) {
      float sky[16];
      std::memcpy(sky, model, sizeof(sky));
      sky[12] = sc.views[e].pose.position.x;
      sky[13] = sc.views[e].pose.position.y;
      sky[14] = sc.views[e].pose.position.z;
      mat_mul(viewProj, sky, push.mvp);
      vkCmdPushConstants(slot.cmd, s.roomLayout, stages, 0, sizeof(push), &push);
      vkCmdBindDescriptorSets(slot.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s.roomLayout, 0, 1, &s.outsideSet, 0, nullptr);
      vkCmdBindVertexBuffers(slot.cmd, 0, 1, &s.skyVerts, &zero);
      vkCmdBindIndexBuffer(slot.cmd, s.skyIndices, 0, VK_INDEX_TYPE_UINT32);
      vkCmdDrawIndexed(slot.cmd, s.skyIndexCount, 1, 0, 0, 0);
    }
    vkCmdEndRenderPass(slot.cmd);
  }
  vkEndCommandBuffer(slot.cmd);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &slot.cmd;
  if (vk_ok(vkQueueSubmit(s.dev.queue, 1, &submit, slot.fence), "vkQueueSubmit(room)")) slot.pending = true;
  for (int e = 0; e < 2; ++e) {
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr_ok(xrReleaseSwapchainImage(s.roomEyes[e].swapchain, &release), "xrReleaseSwapchainImage(room)");
    s.roomLayerViews[e] = XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    s.roomLayerViews[e].pose = sc.views[e].pose;
    s.roomLayerViews[e].fov = sc.views[e].fov;
    s.roomLayerViews[e].subImage.swapchain = s.roomEyes[e].swapchain;
    s.roomLayerViews[e].subImage.imageRect = {{0, 0}, {static_cast<int32_t>(w), static_cast<int32_t>(h)}};
    s.roomLayerViews[e].subImage.imageArrayIndex = 0;
  }
  *layer = XrCompositionLayerProjection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  layer->layerFlags = 0;  // solid
  layer->space = s.dev.local;
  layer->viewCount = 2;
  layer->views = s.roomLayerViews;
  return true;
}

}  // namespace xrfx

#endif  // __ANDROID__ && STRIKERS_XR
