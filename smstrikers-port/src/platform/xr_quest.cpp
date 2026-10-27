// See include/port/xr_quest.h.
//
// Built as its own object library (strikers_xr in CMakeLists.txt) rather than with the rest of
// src/platform: it is C++20 and must not get the port prelude, which the Vulkan and OpenXR headers
// were never written to sit under.
//
// Two picture paths:
//  - The flat game frame (Jumbotron, and the scoreboard over the Tabletop): Aurora reads the
//    presented frame back to the CPU every game frame (aurora_xr_set_frame_callback), and this file
//    uploads it into a quad-layer swapchain on its own Vulkan device.
//  - The 3D modes (Tabletop, Stadium): glPlat.cpp re-sends the frame's render lists once per eye
//    with the head's matrices (PortXrStereo*), into opaque-fd VkImages this device exports and
//    Dawn imports (create_pass_external); a SyncFD fence from Dawn gates a blit into the eye
//    swapchains (ref/TPVR-XR-EXTRACTION.md §3).

#include "../../include/port/xr_quest.h"

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>

#include <jni.h>
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <android/log.h>
#include <time.h>
#include <unistd.h>

#include "xr_fx.h"  // goal celebration effects
#include "xr_ctrl_model.h"  // r54: the headset's own controller models
#include "xr_net.hpp" // online 1v1 (include/port/xr_net.h)
#include "xr_text.h"  // r42: the goal banner and the crowd volume panel
#include "xr_voice.h" // r44: voice chat, its switch on the pause panel
#include "xr_ra.h"    // r46: RetroAchievements, its pop-ups
#include "xr_home.h"  // r61: the home ground
#include "xr_evo.h"   // r96: the 3D evolution
#include <cctype>
#include <ctime>
// r61: stb_image (public domain / MIT), JPEG only, its functions private to this file: the cup suite's
// 360 pictures.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG  // r67: the guest picker's portraits
#include "third_party/stb_image.h"
#include "../../include/port/xr_net.h"
#include "../../include/port/xr_guest.h"  // r67: guest scorers' names
#include <functional>  // r70c
#include "../../include/port/franchise.h"  // r70: franchise mode's hub
#include "../../include/port/lineup.h"     // r81: the co-captains

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>
#include <strings.h>  // r46: strcasecmp
#include <dirent.h>
#include <cstdarg>
#include <cstdio>

#include <SDL3/SDL_system.h>
#include <aurora/aurora.h>
#include <aurora/gfx.hpp>
#include "dolphin/pad.h"
#include "../../extern/aurora/lib/webgpu/gpu.hpp"

// src/platform/audio_out.cpp, for the underrun count in the stats line.
extern "C" void PortAudioStats(unsigned long* outBuffers, unsigned long* outUnderruns, int* outEverNonSilent);
// extern/aurora/lib/gfx/encoding.cpp: per-frame peak and reserved size of the fixed staging pools
// (vertex, uniform, index, storage, texture upload). The eye re-sends multiply the first three.
extern "C" void aurora_gfx_pool_stats(uint32_t* peakBytes, uint32_t* reservedBytes, size_t count);
// extern/aurora/lib/gfx/recording.cpp: bytes used so far this frame in those first four pools.
extern "C" void aurora_gfx_pool_usage(uint32_t* usedBytes, size_t count);
// Where Aurora's threads waited since the last call (extern/aurora/lib/xr_waits.hpp order).
extern "C" void aurora_xr_take_waits(uint64_t* outNs, int count);
// Decode-once vertex path (extern/aurora/lib/gx/vertex_decode.hpp; dolphin/gx/GXAurora.h).
extern "C" void aurora_xr_set_vertex_decode(int mode);
extern "C" void aurora_xr_take_decode_stats(uint32_t out[6]);
extern "C" unsigned long long aurora_xr_decoded_bytes(void);
extern "C" unsigned long long aurora_xr_decoded_bytes_last_frame(void);
// extern/aurora/lib/gx/gx.cpp: alpha = coverage for the eye passes over passthrough.
extern "C" void aurora_gx_set_xr_coverage(bool enabled);
// r43, dolphin/gx/GXAurora.h: single-pass stereo for the draws that follow (in the GX stream).
extern "C" void GXAuroraSetStereo(unsigned char on, const float* tLeft, const float* tRight, const float* clipLeft,
                                  const float* clipRight);
// r43: the single-pass eye pass opened / closed in the GX stream (no FIFO drain at the start), and
// the drain itself.
extern "C" void GXAuroraXrEyePass(unsigned char begin, unsigned char slot, unsigned char samples,
                                  unsigned char coverage);
// r44e: the coverage mode for the next draws inside the open eye pass (the life-size scorer, solid).
extern "C" void GXAuroraXrCoverage(unsigned char mode);
extern "C" void AuroraGXSync(void);
// extern/aurora/lib/gfx/texture.cpp: anisotropic filtering for every mipmapped texture.
extern "C" void aurora_xr_set_texture_anisotropy(unsigned int n);
// src/platform/xr_layers.cpp: stadium objects the per-view culling left out since the last call,
// and the self-test switches that leave categories out of the eye sends.
unsigned long PortXrLayersTakeCulled();
void PortXrTuneSkip(int crowd, int stands, int stadiumField);
void PortXrTakeDrawCensus(char* out, unsigned long size, unsigned long frames);  // r53
void PortXrProbe();
void PortXrSetStadiumEconomy(float minPx, float eyeHalfWidthPx, int skipTallField, int bothLayers);
// Below: starts the celebration renderer when first needed.
static void ensure_fx();
// r42, below: your crowd volume (load, the mixer's crowd controls, the paused right stick).
void load_crowd_level();
void update_crowd_mix();
void settings_input(XrVector2f stick);  // r44g: the pause settings panel
void reload_controls();                 // r46: the button map (xr_controls.txt)
void load_view_height();

namespace {

constexpr const char* kTag = "strikers_xr";
// Bumped every headset round; the first log line of a session says which build is running.
constexpr const char* kBuildTag = "STRIKERS_XR build=r103";

// r39: every line also goes to the game log (stderr -> /sdcard/Strikers/strikers-log.txt): the
// headset's system log turns over within minutes, so a session's stats and errors were lost.
void xr_file_log(const char* level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void xr_file_log(const char* level, const char* fmt, ...) {
  char line[768];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  std::fprintf(stderr, "[xr%s] %s\n", level, line);
}
#define XLOG(...)                                            \
  do {                                                       \
    __android_log_print(ANDROID_LOG_INFO, kTag, __VA_ARGS__); \
    xr_file_log("", __VA_ARGS__);                            \
  } while (0)
#define XWARN(...)                                            \
  do {                                                        \
    __android_log_print(ANDROID_LOG_WARN, kTag, __VA_ARGS__); \
    xr_file_log(" warn", __VA_ARGS__);                        \
  } while (0)
#define XERR(...)                                              \
  do {                                                         \
    __android_log_print(ANDROID_LOG_ERROR, kTag, __VA_ARGS__); \
    xr_file_log(" error", __VA_ARGS__);                        \
  } while (0)

bool xr_ok(XrResult r, const char* what) {
  if (XR_SUCCEEDED(r)) {
    return true;
  }
  XERR("%s failed: XrResult %d", what, static_cast<int>(r));
  return false;
}

bool vk_ok(VkResult r, const char* what) {
  if (r == VK_SUCCESS) {
    return true;
  }
  XERR("%s failed: VkResult %d", what, static_cast<int>(r));
  return false;
}

template <typename T>
bool get_proc(XrInstance instance, const char* name, T* out) {
  *out = nullptr;
  const XrResult r = xrGetInstanceProcAddr(instance, name, reinterpret_cast<PFN_xrVoidFunction*>(out));
  return XR_SUCCEEDED(r) && *out != nullptr;
}

// The three screen sizes the right stick click cycles through: width in meters, distance in meters.
struct ScreenPreset {
  float width;
  float distance;
};
constexpr ScreenPreset kScreenPresets[] = {
    {2.4f, 2.5f},  // living-room TV, a bit big
    {3.6f, 3.0f},  // big screen (default)
    {6.0f, 4.0f},  // cinema
};
constexpr int kScreenPresetCount = sizeof(kScreenPresets) / sizeof(kScreenPresets[0]);
constexpr int kDefaultScreenPreset = 1;
constexpr float kScreenDrop = -0.1f;  // room screen: its centre this far below your eyes
// r42: the same three sizes in the Stadium, where the room ones look tiny (from Charged r15).
constexpr ScreenPreset kStadiumScreenPresets[] = {
    {4.0f, 4.5f},
    {6.0f, 6.0f},
    {11.0f, 10.0f},
};

// r42: the Jumbotron screen by hand (Lance, after alpha 1.2; Charged r15's FreeScreen). One per
// room (0 = your room, 1 = the Stadium), saved to xr_screens.txt in the app's folder.
struct FreeScreen {
  bool placed = false;
  XrVector3f position{0.0f, 0.0f, 0.0f};  // centre, LOCAL
  float yaw = 0.0f;                       // about +Y; 0 faces +Z
  float width = 3.6f;
  int grabHands = 0;                      // 1 left grip, 3 both grips
  float gain = 1.0f;                      // hand motion -> screen motion (far screens move faster)
  XrVector3f grabPos{}, grabHand{};
  float twoDist = 1.0f, twoAngle = 0.0f, twoWidth = 3.6f, twoYaw = 0.0f;
  XrVector3f twoPos{}, twoMid{};
};

// One game frame every 1/60 s, whatever the headset refreshes at.
constexpr int64_t kGameTickNs = 16666667;

struct Actions {
  XrActionSet set = XR_NULL_HANDLE;
  XrAction a = XR_NULL_HANDLE, b = XR_NULL_HANDLE, x = XR_NULL_HANDLE, y = XR_NULL_HANDLE;
  XrAction menu = XR_NULL_HANDLE;
  XrAction triggerL = XR_NULL_HANDLE, triggerR = XR_NULL_HANDLE;
  XrAction squeezeL = XR_NULL_HANDLE, squeezeR = XR_NULL_HANDLE;
  XrAction stickL = XR_NULL_HANDLE, stickR = XR_NULL_HANDLE;
  XrAction stickClickL = XR_NULL_HANDLE, stickClickR = XR_NULL_HANDLE;
  XrAction gripPoseL = XR_NULL_HANDLE;  // the hands that grab the Tabletop field
  XrSpace gripSpaceL = XR_NULL_HANDLE;
  XrAction gripPoseR = XR_NULL_HANDLE;
  XrSpace gripSpaceR = XR_NULL_HANDLE;
  XrAction aimPoseL = XR_NULL_HANDLE, aimPoseR = XR_NULL_HANDLE;  // r60: pointing (screen edge, badges)
  XrSpace aimSpaceL = XR_NULL_HANDLE, aimSpaceR = XR_NULL_HANDLE;
  XrAction haptic = XR_NULL_HANDLE;     // both controllers: the game's pad rumble
};

// The game's rumble on pad 0, handed to the Touch controllers on the next input sync.
// RumbleActions.cpp picks the strength from the contact preset; cPlatPad starts and stops it.
struct Rumble {
  float amplitude = 0.5f;     // of the action that is running, 0..1
  bool startPending = false;
  float seconds = 0.0f;       // of the pending start
  bool stopPending = false;
};

// One eye's image for one frame slot: an optimally tiled VkImage on the XR device with exportable
// opaque-fd memory, the same memory imported into Dawn, and the sync objects of the hand-off.
struct EyeImage {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  wgpu::SharedTextureMemory shared;
  wgpu::Texture texture;
  wgpu::TextureView renderView;  // in aurora's scene format (RGBA8Unorm over the sRGB image)
  VkSemaphore dawnDone = VK_NULL_HANDLE;
  VkImageLayout dawnEndLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  bool accessOpen = false;
  // r43: Dawn's access ended (release_eye_image), its end fence exported here, not yet blitted.
  bool released = false;
  int releaseFd = -1;  // ours (dup'ed); the import takes it
  VkExternalSemaphoreHandleTypeFlagBits releaseFdType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
  bool releaseComplete = false;  // no fence because the work was already done
};

// The screen upload's staging buffer, command buffer and fence, two deep: the copy of game frame N
// is still on the GPU while frame N+1 is made, and nothing waits for it. Waiting on the copy every
// frame (r2-r6) also waited for everything queued before it, all of the frame's 3D, so the CPU and
// the GPU took turns instead of overlapping.
struct UploadSlot {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  void* mapped = nullptr;
  VkDeviceSize size = 0;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  bool pending = false;
};
constexpr int kUploadSlots = 2;

// r42: a picture drawn on the CPU (xr_text.cpp) shown as a quad layer of its own: the goal banner,
// the crowd volume panel. Uploaded only when it changes.
struct Panel {
  XrSwapchain swapchain = XR_NULL_HANDLE;
  std::vector<XrSwapchainImageVulkanKHR> images;
  uint32_t width = 0, height = 0;
  bool hasImage = false;
  UploadSlot upload;
};

// Aurora's fixed staging pools (vertex, uniform, index, storage) that the eye re-sends fill.
constexpr int kPools = 4;

// Eye image sets in flight. r37 tried three: the game thread's ~7 ms wait for this fence just moved
// into xrWaitFrame (the runtime paces to the GPU, which is the real limit), no frame-rate change.
constexpr uint32_t kStereoSlots = 2;
struct StereoSlot {
  EyeImage eye[2];
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  bool pending = false;
  XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};  // r43: the views it was drawn for
  // r43 split stadium (Tabletop · Stadium): the life-size stadium for one eye, its own layer.
  EyeImage stadium;
  int stadiumEye = -1;  // the eye drawn into it this frame, -1 none
  bool mainDrawn = false;  // the table / scorer pass drew into eye[0] this frame
  bool mainOverlay = false;  // ... with coverage alpha, over the split stadium
};
// r43, single pass: the slot's game frame is handed to the compositor when the render worker has
// submitted it (aurora_xr_set_submit_callback), not after the game thread waits for that. Per slot:
// 0 free, 1 drawn (waiting for its frame's submit), 2 Dawn's access ended (ready to blit).
// Outside State, which is reset by assignment.
std::atomic<uint32_t> g_slotState[kStereoSlots];
std::atomic<uint32_t> g_slotFrame[kStereoSlots];  // the Aurora frame that drew it

// Where the Tabletop field sits in the room (LOCAL space) and how big it is.
struct Table {
  XrVector3f position{0.0f, -0.55f, -0.95f};  // about waist height in front of where you started
  float yaw = 0.0f;                           // radians about the room's up axis
  float scale = 0.032f;                       // room meters per game unit: the 41 m pitch is ~1.3 m
  int grabHands = 0;                          // holding it: bit 0 left hand, bit 1 right hand
  bool rightGrab = false;                     // this right-grip squeeze started on the field
  XrVector3f grabOffset{0.0f, 0.0f, 0.0f};    // one hand: field position minus hand position
  // Two hands, at the moment the second one took hold: hand distance, the field's size, turn and
  // position, and the hands' midpoint; turn = how far the hands have turned around each other since.
  float twoDist = 1.0f, twoScale = 0.032f, twoYaw = 0.0f, prevAngle = 0.0f, turn = 0.0f;
  XrVector3f twoPos{0.0f, 0.0f, 0.0f}, twoMid{0.0f, 0.0f, 0.0f};
};

struct Passthrough {
  bool available = false;
  XrPassthroughFB handle = XR_NULL_HANDLE;
  XrPassthroughLayerFB layer = XR_NULL_HANDLE;
  bool layerRunning = false;
  bool running = false;  // r60: the passthrough itself (paused in the Stadium modes)
  PFN_xrCreatePassthroughFB create = nullptr;
  PFN_xrPassthroughStartFB start = nullptr;
  PFN_xrPassthroughPauseFB pause = nullptr;
  PFN_xrCreatePassthroughLayerFB createLayer = nullptr;
  PFN_xrPassthroughLayerPauseFB pauseLayer = nullptr;
  PFN_xrPassthroughLayerResumeFB resumeLayer = nullptr;
};

// r46, button mapping: the Quest buttons a GameCube button can come from, and the GameCube buttons.
// The names are the ones in xr_controls.txt (LobbyActivity's Controls page writes it).
// r47: a Bluetooth pad's buttons are the same sources (its A/B/X/Y, LT/RT, RB for the right grip,
// Start for the menu button), so one map covers both. And LONG, the long (lob) pass: the game's L + A
// from one button (players: "L2 + A ... is really awkward"), none by default.
enum ControlSource { kSrcA, kSrcB, kSrcX, kSrcY, kSrcTrigL, kSrcTrigR, kSrcGripR, kSrcMenu, kSrcCount, kSrcNone = kSrcCount };
enum ControlAction { kActA, kActB, kActX, kActY, kActZ, kActL, kActR, kActStart, kActLong, kActCount };
constexpr const char* kSourceNames[kSrcCount] = {"a", "b", "x", "y", "lt", "rt", "rg", "menu"};
constexpr const char* kActionNames[kActCount] = {"A", "B", "X", "Y", "Z", "L", "R", "START", "LONG"};
constexpr int kDefaultControls[kActCount] = {kSrcA, kSrcB, kSrcX, kSrcY, kSrcGripR, kSrcTrigL, kSrcTrigR, kSrcMenu,
                                             kSrcNone};

struct State {
  bool active = false;
  bool quit = false;

  JavaVM* vm = nullptr;
  jobject activity = nullptr;  // global ref

  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSpace local = XR_NULL_HANDLE;
  XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
  bool running = false;
  bool focused = false;

  bool hasPassthroughExt = false;
  bool colorPassthrough = true;  // r44f: the room in colour (Quest 3/3S/Pro); false on a Quest 2
  bool hasRefreshRateExt = false;
  bool hasRenderModelExt = false;      // r54: XR_FB_render_model (xr_ctrl_model.h)
  bool renderModelsSupported = false;
  bool hasPerfSettingsExt = false;
  bool hasThreadSettingsExt = false;

  VkInstance vkInstance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queueFamily = 0;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool pool = VK_NULL_HANDLE;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  VkPhysicalDeviceMemoryProperties memProps{};
  float maxAnisotropy = 0.0f;  // r61: samplerAnisotropy's limit when the device has it on
  uint32_t roomSamples = 1;    // r62: the cup suite room's multisampling

  VkBuffer staging = VK_NULL_HANDLE;  // the eye dump's
  VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
  void* stagingMapped = nullptr;
  VkDeviceSize stagingSize = 0;
  UploadSlot uploads[kUploadSlots];
  int uploadSlot = 0;

  XrSwapchain swapchain = XR_NULL_HANDLE;
  std::vector<XrSwapchainImageVulkanKHR> swapchainImages;
  uint32_t swapchainWidth = 0, swapchainHeight = 0;
  int64_t swapchainFormat = 0;
  bool swapchainBgra = false;
  bool swapchainHasImage = false;

  Passthrough pt;
  bool mrMode = true;  // passthrough on; VR mode is the dark room until the stadium lands

  PFN_xrEnumerateDisplayRefreshRatesFB enumerateRefreshRates = nullptr;
  PFN_xrRequestDisplayRefreshRateFB requestRefreshRate = nullptr;

  Actions actions;
  Rumble rumble;
  uint64_t rumblePulses = 0;  // since the last stats line
  bool prevStickClickL = false;
  bool prevStickClickR = false;
  uint64_t pendingTapL = 0;  // time of an unanswered first tap, 0 when none
  uint64_t pendingTapR = 0;
  int screenPreset = kDefaultScreenPreset;
  bool tabletop = false;     // Jumbotron (false) or Tabletop (true)
  // r42: the Jumbotron screen by hand, and where your head is (VIEW space, every input sync).
  FreeScreen screens[2];
  bool bringScreen = false;   // put the screen in front of you (left stick tap, size change)
  bool prevGripL = false;
  bool screensDirty = false;  // moved since the last save
  bool holdRightGrip = false; // the right grip took part in a grab: no Z until it is let go
  XrSpace view = XR_NULL_HANDLE;
  XrPosef headPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  bool headValid = false;
  unsigned long lastUnderruns = 0;

  // --- 3D modes ---
  bool hasExtMemFd = false;
  bool hasExtSemFd = false;
  bool stereoFailed = false;          // set once; the 3D modes then fall back to the Jumbotron
  uint32_t eyeWidth = 0, eyeHeight = 0;
  XrSwapchain eyeSwapchain[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  std::vector<XrSwapchainImageVulkanKHR> eyeImages[2];
  StereoSlot slots[kStereoSlots];
  uint32_t slot = 0;                  // the slot this frame renders into
  XrSpace stage = XR_NULL_HANDLE;     // floor-level space, for the life-size stadium
  float floorY = -1.6f;               // LOCAL-space height of the floor (STAGE origin) when known
  XrView views[2] = {{XR_TYPE_VIEW}, {XR_TYPE_VIEW}};
  bool viewsValid = false;            // this display frame located both eyes
  bool stereoThisFrame = false;       // glPlat re-sent the frame per eye this game frame
  int stereoLayers = 0;
  bool eyePassOpen[2] = {false, false};
  // r43: single-pass stereo (default; STRIKERS_XR_SPS=0 = the r4-r42 send per eye). Each layer is
  // sent once from the centre between the eyes into one double-wide image per slot (slot.eye[0]),
  // and Aurora encodes it into both halves (gx.hpp GXState::stereo). The eye re-sends were the
  // stadium modes' frame-rate limit: r42b measured ~13 ms of a ~23 ms game frame in them.
  bool singlePass = true;
  XrPosef centrePose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};  // this frame's single-pass view
  XrFovf unionFov{};                  // ... and the field of view that holds both eyes'
  float stereoT[2][12] = {};          // per eye: centre eye space -> that eye's (3x4, row-major)
  float stereoClip[2][4] = {};        // per eye: clip x/y from the union projection (ax, bx, ay, by)
  bool stereoOn = false;              // GXAuroraSetStereo(1) sent and not yet switched off
  // r43 split stadium: in Tabletop · Stadium the life-size stadium is its own projection layer
  // under the table's, drawn for one eye per game frame, left and right in turn (each eye 30 times
  // a second, the compositor turning them with your head between); the table and players stay
  // every frame. Measured: table alone 60, stadium alone ~53, both in one pass ~41-46 (GPU).
  // xr_tune "stadiumsplit <0|1>".
  bool stadiumSplit = true;
  // r87 (Lance: "FPS needs fixed within VR modes both presentation ways"; his r86 log, Konga Coliseum:
  // Jumbotron · Stadium 40 game fps, both eyes' stadium every frame = 628 draws x 2 eyes, the game
  // thread waiting ~9.5 ms a frame on the GPU): the same split there, the stadium alone in its layer,
  // one eye a frame; the scorer, when out, over it. xr_tune "jumbosplit <0|1>".
  bool jumboSplit = true;
  bool splitThisFrame = false;        // this game frame draws the stadium into slot.stadium
  int stadiumNextEye = 0;
  int openTarget = 0;                 // 0 none, 1 the main (wide) pass, 2 the stadium pass
  XrSwapchain stadiumSwapchain[2] = {XR_NULL_HANDLE, XR_NULL_HANDLE};
  std::vector<XrSwapchainImageVulkanKHR> stadiumImages[2];
  XrCompositionLayerProjectionView stadiumViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                      {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  bool stadiumValid[2] = {false, false};  // that eye's stadium swapchain holds a drawn image
  bool mainIsOverlay = false;         // the main projection layer was drawn over a split stadium
  XrCompositionLayerProjectionView projViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW},
                                                   {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
  bool hasProjection = false;         // projViews hold a submitted pair the compositor can reuse
  Table table;
  uint64_t lastTriggerPollNs = 0;
  bool dumpRequested = false;
  uint64_t stereoNsSinceStats = 0;
  uint64_t eyeSendStartNs = 0;
  uint64_t eyeSendNsSinceStats = 0;   // game-thread time inside the per-eye re-sends
  // Pool guard: what one eye send of each layer kind costs in each pool (running estimate), the
  // layer kinds this frame sends, and the open per-layer measurement.
  uint32_t poolReserved[kPools] = {};
  float perSend[3][kPools] = {};  // per LayerKind (r42: + the scorer)
  // r38: the players' decoded vertices land in the vertex pool once per frame (in whichever send
  // draws them first), not once per send: kept out of perSend, counted once in layers_fit.
  unsigned long long decodedAtMeasure = 0;  // aurora_xr_decoded_bytes when the open window began
  float decodedPerFrame = 0.0f;             // bytes, rise at once / fall slowly
  int frameLayerKind[3] = {0, 0, 0};
  bool measuring = false;
  int measuringKind = 0;
  uint32_t measureStart[kPools] = {};
  uint64_t layersDroppedSinceStats = 0;
  // main.cpp's loop phases summed over game frames since the last stats line (PortXrNoteLoopTimes).
  uint64_t loopNs[5] = {};
  uint64_t xrWaitNsSinceStats = 0;    // inside xrWaitFrame, every display frame
  // Headset-side waits and costs since the last stats line: the eye slot's fence, the eye
  // swapchains' acquire + wait, the CPU fallback wait for Dawn, the effects layer, xrEndFrame.
  uint64_t eyeFenceNs = 0, swapNs = 0, cpuWaitNs = 0, fxNs = 0, xrEndNs = 0;
  bool gameplayLive = false;          // the match is being played (not a cutscene/replay/menu)
  uint64_t superStrikeNs = 0;         // r44e: the last game frame a Super Strike was under way
  bool fxReady = false;               // the celebration renderer is up (started at the first goal)
  bool dropLogged = false;            // the jumbotron's down position was logged for this replay
  float screenLift = 0.0f;            // r43c: meters the screen is raised over the scorer (eases)
  // r44: meters the Tabletop field is raised out of the way while the scorer is out (eases), and
  // whether you took hold of it during this show (then it stays where you put it).
  float tableLift = 0.0f;
  bool tableLiftOff = false;
  bool bringTable = false;            // put the Tabletop field in front of you (left stick tap)
  bool celebBuzz = false;             // this goal is the player's: buzz with the cannon waves
  bool testWipe = false;              // self-test: Game.cpp plays the white-flash wipe once
  bool testItem = false;              // self-test: Game.cpp gives your team a green shell
  bool testSts = false;               // self-test: Game.cpp shows the Super Strike meter for 3 s
  struct {
    bool shown = false;               // the Super Strike meter is up in live play (PortXrSetStsMeter)
    bool logged = false;              // its Tabletop panel was logged for this Super Strike
    float game[3] = {0.0f, 0.0f, 0.0f};  // where the game hangs it, game units
    float halfU = 0.1f, halfV = 0.2f;    // its square in the middle of the picture
  } sts;
  // r42: the life-size goal scorer. xr_scorer.cpp poses it and reports the show every game frame;
  // this places it in the room: it runs over from the field to a spot beside you, celebrates facing
  // you, and runs back.
  struct {
    int phase = 0;                    // 0 hidden, 1 running over, 2 celebrating, 3 running back
    float phaseTime = 0.0f;
    float height = 1.4f;              // the character's own height, game units
    int cls = -1, side = 0, captainClass = -1;
    uint64_t reportNs = 0;            // its last report (a game that stops reporting hides it)
    bool planned = false;             // this show's path is set
    XrVector3f spawn{}, spot{};       // LOCAL, on the floor
    XrVector3f away{};                // unit, from the spot back toward the field
    XrVector3f leaveFrom{};           // where the run back starts (a skip can start it early)
    XrVector3f lastPos{};
    float lastYaw = 0.0f;
    int prevPhase = 0;
    bool logged = false;
    unsigned shows = 0;               // r43b: shows so far (which side it comes in from)
    int statGoals = -1, statAssists = 0;  // r43d: its goals / assists this game (PortXrScorerStats)
    bool statHatTrick = false;            // ... and this goal is its third regular one
  } scorer;
  Panel banner;                       // r42: "GOAL SCORED BY:" over the scorer
  bool bannerBusy = false;            // its picture is being drawn (g_bannerPixels, g_bannerReady)
  Panel slider;                       // r42: the crowd volume panel while paused
  bool paused = false;
  int crowdLevel = 6;                 // 0..10; 6 = the game's own loudness
  uint64_t sliderShownUntilNs = 0;
  uint64_t crowdRepeatNs = 0;         // right stick held: the next step
  int settingsStickDirY = 0;          // r44g: the right stick's last up/down push while paused
  int settingsRow = 0;                // r44g: the settings row picked (0 crowd, 1 voice, 2 height)
  // r46: the button map (ControlSource per ControlAction), and when xr_controls.txt was last looked at.
  int controls[kActCount] = {kSrcA, kSrcB, kSrcX, kSrcY, kSrcGripR, kSrcTrigL, kSrcTrigR, kSrcMenu, kSrcNone};
  uint64_t longPassSinceNs = 0;       // r47: the long-pass button went down (L first, then A)
  uint64_t controlsCheckNs = 0;
  long long controlsStamp = -1;
  int viewHeightCm = 0;               // r44g: your eye height in the stadium, + higher (xr_view_height.txt)
  bool sliderPlaced = false;
  XrPosef sliderPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  // r51: the pause screen's RetroAchievements list (a right-stick click while paused), the left stick
  // scrolling it a row at a time.
  bool achOpen = false, achPlaced = false;
  int achGen = 0;                     // r51b: the drawing this opening of the list wants
  bool achShown = false;              // its pictures are uploaded
  float achScroll = 0.0f;             // r51b: pixels of the tall picture scrolled past
  float achStickY = 0.0f;             // the left stick (or D-pad) up/down while the list is open
  uint32_t achListH = 0;              // the tall picture's height
  bool achPrevB = false;
  Panel achPanel, achHead;
  XrVector3f achPos{0.0f, 0.0f, 0.0f};
  // r67: the guest picker over the game's own pick screens (include/port/xr_guest.h); r68e: the
  // character select menu, everyone on one panel: opens by itself when a side starts choosing (Y opens
  // it again), the stick (or D-pad) moves, A picks, B closes it to the game's own screen.
  bool gpOffered = false;             // a pick screen offers it this input sync
  bool gpOpen = false;
  int gpSide = -1, gpRole = 0, gpCursor = 0, gpPage = 0, gpGen = 0;
  bool gpPrevY = false, gpPrevL = false, gpPrevR = false, gpHoldA = false, gpHoldB = false;
  int gpDir = 0;                      // the stick's way last sync (0 none, 1-4 left right up down)
  int gpAutoDone = -1;                // r68e: the offer (side * 2 + role) the menu already opened for
  uint64_t gpOpenNs = 0;              // r68g: when it opened
  // r79: a Grudge Match side's sidekick pick starts with its co-captain (the captains' menu; X: none)
  bool gpCo = false, gpPrevX = false;
  unsigned gpCoTaken = 0;             // the Strikers captains it can't be (bit = eTeamID)
  int scorerPlayerId = -1;           // r68h: the scorer's m_ID (PortXrScorerPlayer)
  bool lastGoalOwn = false;          // r85: the last goal was an own goal (PortXrGoal)
  bool scorerFaceOff = false;         // r73: the show is a franchise nemesis's face-off (PortXrScorerFaceOff)
  uint32_t maxLayers = 0;             // r73: the compositor's most layers a frame (0 = not known)
  uint64_t gpRepeatNs = 0;            // when a held stick moves the cursor again
  Panel gpPanel, gpTip;
  bool gpPlaced = false, gpTipPlaced = false;
  XrVector3f gpPos{0.0f, 0.0f, 0.0f}, gpTipPos{0.0f, 0.0f, 0.0f};
  // r70: franchise mode's hub (include/port/franchise.h): Y on the game's main menu opens it, on the big
  // screen like the character select menu. Modes: the 3 career slots, a new career's club, the hub
  // (pages: the club, the standings, last week's results).
  bool frOpen = false, frOnMenu = false;
  int frMode = 0, frCursor = 0, frSlot = 0, frPage = 0, frGen = 0, frDir = 0;
  bool frPrevY = false, frHoldA = false, frHoldB = false, frPrevX = false, frConfirmDelete = false;
  bool frAfterMatch = false;          // a franchise match was played: the hub opens back on the menu
  // r70c: the hub's mode 3, one of your Pokemon evolving (stage 0 a split's choice, 1 evolving, 2 evolved,
  // 3 stopped), from when (the animation's clock).
  int frEvoStage = 0, frEvoChoice = 0;
  bool frEvoNewShiny = false;
  bool frEvoFanfare = false;  // r99: its victory fanfare started (at the flash)
  uint64_t frEvoStartNs = 0;
  // r75: the hub's mode 4, a new career's own players on the character select menu: stage 0 your
  // captain, 1 your backup captain (X: none), 2 your sidekicks; the club and the picks so far.
  int frPickStage = 0, frPickClub = 0, frPickCap = -1, frPickBk = -1;
  // r76, co-op: the loaded career's outside changes seen, and the co-op card's state as last drawn
  unsigned frChangeSerial = 0;
  long long frCoopKey = -1;
  // r77: the hub's mode 5, trades: stage 0 what you give (0 captain, 1 backup, 2 sidekicks), 1 their
  // offers; the last answer; Y inside the hub (its own edge: frPrevY is the opener's)
  int frTradeStage = 0, frTradeWhat = 0;
  std::string frTradeMsg;
  bool frHubPrevY = false;
  // (the menu cursor kept per stage, so B goes back to where you were)
  int frPickCursor[3] = {0, 0, 0};
  // r100, online leagues: the hub's mode 6 (make one: how many people; join one: its code typed, stage 1) and
  // mode 7 (joining: the league's club picker)
  int frLgStage = 0, frLgMax = 8, frLgPos = 0;
  int frLgChar[6] = {0, 0, 0, 0, 0, 0};
  PortFranchiseEvolution frEvo = {};
  Panel frEvoPanel;
  // r73: the league in your room (franchise, outside matches): the news wire under the big screen,
  // the TV beside it; their data refreshed every 2 s on the game thread.
  bool frRoomOn = false;
  int frRoomGen = 0;
  uint64_t frRoomNextNs = 0;
  std::string frRoomKey;
  Panel frTicker, frTv;
  // r70b: the franchise match's intro card (10 s from its intro) and the playoff series board.
  int frCardSerial = 0, frCardGen = 0, frCardKey = -1;
  uint64_t frIntroUntilNs = 0;
  Panel frIntro, frSeries;
  Panel frLive;                // r77: the live XP meter
  long long frLiveKey = -2;
  int frLiveGen = 0;
  // r87: each row's level as last shown (-1 none yet), when its level-up light started (0 none), from what,
  // and the next redraw while one is lit (~15 a second)
  int frLiveShown[3] = {-1, -1, -1}, frLiveFrom[3] = {-1, -1, -1};
  uint64_t frLiveGlowNs[3] = {0, 0, 0}, frLiveTickNs = 0;
  bool frLiveLit = false;
  bool frIntroPlaced = false, frSeriesPlaced = false;
  XrVector3f frIntroPos{0.0f, 0.0f, 0.0f}, frSeriesPos{0.0f, 0.0f, 0.0f};
  uint64_t frOpenNs = 0, frRepeatNs = 0, frConfirmNs = 0;
  Panel frPanel, frTip;
  bool frPlaced = false, frTipPlaced = false;
  XrVector3f frPos{0.0f, 0.0f, 0.0f}, frTipPos{0.0f, 0.0f, 0.0f};
  Panel netWait;                      // r47: "waiting for your friend" at an online match start
  int netWaitDrawn = 0;               // the state its picture shows (xrnet::waiting_for_friend)
  bool netWaitPlaced = false;
  XrVector3f netWaitPos{0.0f, 0.0f, 0.0f};
  uint64_t holdBSinceNs = 0;          // B held while waiting (2 s stops the wait)
  Panel raToast;                      // r46: a RetroAchievements pop-up (xr_ra.h)
  uint64_t raToastShownNs = 0, raToastUntilNs = 0;
  bool raToastPlaced = false;
  XrVector3f raToastPos{0.0f, 0.0f, 0.0f};
  float charScale = 1.5f;             // Tabletop: players drawn this much bigger (xr_tune "charscale")
  int msaaTable = 4;                  // eye-pass multisampling, table only / with the stadium layer
  int msaaStadium = 4;                // (1 or 4; xr_tune "msaa <table> <stadium>")
  uint64_t testPressXNs = 0;          // self-test: press X (use item) from this time for 150 ms
  bool prevGripR = false;             // right grip last input sync (its squeeze decides grab or Z)
  PADStatus localPad{};               // this headset's pad, this input sync
  bool port1Active = false;           // pad 1 fed from the network (online match)
  double loopIntervalEma = 0.0;       // ns between headset loop iterations, smoothed
  uint64_t focusBackNs = 0;           // r44: the loop timing counts again from here (focus regained)
  uint64_t onlineSinceNs = 0;         // r44d: when this online match began, 0 offline
  uint64_t slowSinceNs = 0;           // r44d: since when the loop has been too slow, 0 = it isn't
  int lastModeKey = -1;               // r44g: the view (tabletop / room bits) last loop
  uint64_t modeChangedNs = 0;         // r44g: when it last changed
  bool onlineHalfRate = false;        // online and too slow: 3D view every other game frame
  uint32_t onlineParity = 0;
  bool stadiumHalf = false;           // r42b: Jumbotron · Stadium eyes every other frame (xr_tune)
  // r44 (players: input lag): wait up to kHandoffWaitNs after xrBeginFrame for the render worker to
  // submit the eyes just recorded, so they go out this display frame and not the next (xr_tune
  // "handoffwait <0|1>"), and how often that landed since the last stats line.
  bool handoffWait = true;
  uint32_t handoffSame = 0, handoffLate = 0;
  // r53: the wait is cut to what the next game frame can spare: when this display frame's
  // xrWaitFrame returned, a game frame's usual cost before PortXrEndFrame, cuts since the stats line.
  uint64_t lastWaitReturnNs = 0;
  double gameFrameEmaNs = 0.0;
  uint32_t handoffCut = 0;
  // r53: a goal replay draws the game twice in Tabletop, the table and the broadcast picture for the
  // screen (Lance's cups log: 928 draws a game frame in Tabletop · Living Room replays against 365 in
  // play, 1153 against 703 in Tabletop · Stadium; 42-46 fps for the ~10 s of every replay, the
  // scorer's walk-up included). While one runs (replayRunning, since replayStartNs): Living Room
  // leaves the broadcast picture out (the replay plays on the table; the screen is hidden), Stadium
  // draws it every other game frame and the split stadium on the frames between (showParity).
  // worldlessFrame / worldless: the decision for the Aurora frame being built; stadiumRest: this
  // frame's stadium is left as it was (the broadcast's turn).
  // r53: the controllers in your hands (xr_fx), in the skin of your team's captain (eCharacterClass,
  // the last one seen in live play; -1 before the first match).
  bool showControllers = true;
  int skinCaptain = -1;
  // r55: item blasts shake things: the Tabletop field (and, a little, the life-size stadium) on a
  // stiff damped spring, offset and velocity in meters (LOCAL), stepped every display frame.
  XrVector3f joltPos{0.0f, 0.0f, 0.0f}, joltVel{0.0f, 0.0f, 0.0f};
  uint64_t lastJoltNs = 0;
  uint32_t blastsLogged = 0;
  // r57: the trophy shelf in your room (xr_shelf.txt): where it is, which hands hold it, and the
  // cups won (the save's trophy bits and win counts, from Game.cpp), the last one won and when.
  struct ShelfState {
    bool placed = false;
    XrVector3f position{0.0f, 0.0f, 0.0f};
    float yaw = 0.0f;
    bool handL = false, handR = false;  // a grip that closed on it holds it
    int grabHands = 0;
    XrVector3f grabOffset{0.0f, 0.0f, 0.0f};
    float twoAngle = 0.0f, twoYaw = 0.0f;
    XrVector3f twoPos{0.0f, 0.0f, 0.0f}, twoMid{0.0f, 0.0f, 0.0f};
    bool prevGripL = false, prevGripR = false;
    // r57b (Lance: "i want those to be able to be picked up"): a trophy in a hand (-1 none), held
    // where it was taken (its middle and turn in the hand's grip frame); a trophy off its spot (held,
    // or gliding back to it after it is let go), where it is.
    int heldCup[2] = {-1, -1};
    XrVector3f heldOff[2] = {};
    XrQuaternionf heldTurn[2] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    bool cupFree[8] = {};
    XrPosef cupPose[8] = {};
    bool cupBack[8] = {};
    XrPosef cupFrom[8] = {};
    uint64_t cupBackNs[8] = {};
    // r60 (Lance: "make the trophy bigger when its in hand same way we make the screen bigger"): the
    // other grip closing while one hand holds a trophy resizes it by how far apart the hands go; its
    // size (1 = on the shelf), and what it was when it started gliding back (it shrinks on the way).
    float cupScale[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float cupFromScale[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    int resizeHand = -1;  // the hand resizing (the other one holds the trophy)
    float resizeDist0 = 0.1f, resizeScale0 = 1.0f;
    bool spent[2] = {};   // a grip still closed after its resize ended: carries nothing
  } shelf;
  // r60: the controllers' pointing rays (OpenXR aim poses), this input sync's.
  bool aimValid[2] = {};
  XrPosef aim[2] = {};
  // r60 (Lance: the grip should take the screen "only ... when we touch the border of the screen"):
  // the big screen as last shown (for the pointer test), and whether the left hand's pointer (or the
  // hand itself) is on its edge now.
  bool shownScreenValid = false;
  XrPosef shownScreenPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  float shownScreenW = 0.0f, shownScreenH = 0.0f;
  bool screenEdgeHover = false;
  XrVector3f screenEdgePoint{0.0f, 0.0f, 0.0f};
  // r60 (Lance: "hover over the achievements youve earned in that trophy room area, so that you can
  // see the ones you earned and have yet to earn"): the board's rows as drawn, the badge under a
  // pointer (-1 none) and the card shown for it.
  std::vector<xrra::AchievementRow> boardRows, boardRowsPending;
  uint32_t boardH = 0, boardHPending = 0;
  int boardHover = -1, tipFor = -1, tipShownFor = -1, tipGen = 0, tipShownGen = 0;
  Panel tip;
  // r61: the home ground's boards (xr_home.cpp; g_homeBoard / g_ticket / g_cupBoard draw them).
  Panel homePanel, ticketPanel, cupPanel;
  uint64_t homePollNs = 0;
  // r61, the cup suite: on while a cup is between its games (unless you switched view there), and the
  // view you had, which comes back for the matches.
  bool inSuite = false, suiteDismissed = false, suiteSavedMr = true, suiteSavedTabletop = false;
  // r61 (Lance: "let me walk within my environment ... dont move the game if i move in real life"):
  // the suite is a room fixed in your room, set where you stand and the way you face as it opens
  // (its middle on the floor under you, its window ahead); its baked room (the home captain's,
  // suites/suite_<name>.*) is loaded on a thread.
  int suiteCaptain = -2;  // the one loaded or loading
  std::string suiteLoaded;     // r73: the room loaded or loading ("mario" ... or a guest series' "pokemon")
  std::string suiteGuestName;  // r73: a guest series' suite: the guest's name for its plaque, "" none
  XrVector3f suiteOrigin{0.0f, 0.0f, 0.0f};
  float suiteYaw = 0.0f;
  unsigned trophyBits = 0;
  unsigned short trophyWins[8] = {};
  int newCup = -1;
  uint64_t newCupNs = 0;
  // r57: the badge board's panel and what it was last drawn from.
  Panel board;
  int boardGen = 0, boardShownGen = 0;
  uint64_t boardCheckNs = 0;
  uint32_t boardUnlocked = 0xFFFFFFFFu;
  int boardFiles = -1;
  bool replayRunning = false;
  uint64_t replayStartNs = 0;
  uint32_t worldlessFrame = 0xFFFFFFFFu;
  bool worldless = false;
  bool stadiumRest = false;
  bool splitSkipStadium = false;
  uint32_t showParity = 0;
  uint32_t stadiumParity = 0;
  uint32_t prevItemButtons = 0;       // X / Y / Z last input sync, for the press log
  int buzzWave = 0;                   // cannon waves already buzzed
  uint32_t eyeViewMask = 0xFFFFFFFFu; // GLV views the eye sends may draw (self-test tuning)
  uint32_t eyeViewMaskTable = 0xFFFFFFFFu;    // ... in the table layer only
  uint32_t eyeViewMaskStadium = 0xFFFFFFFFu;  // ... in the stadium layer only
  float stadiumYawDebug = 0.0f;       // self-test: turn the life-size stadium (radians) to see behind
  float lodMinPx = 0.0f;              // stadium details smaller than this in an eye are skipped
  bool skipTallField = false;         // Tabletop · Stadium: no life-size goals/benches/fence
  uint64_t lastFxNs = 0;

  XrFrameState frameState{XR_TYPE_FRAME_STATE};
  bool frameBegun = false;
  int64_t tickAccumNs = 0;
  uint64_t lastTickNs = 0;

  uint64_t framesShown = 0;
  uint64_t lastStatsNs = 0;
  uint64_t gameFramesSinceStats = 0;
  uint64_t displayFramesSinceStats = 0;
  uint64_t uploadNsSinceStats = 0;
};
State g;

// r42: the goal banner's picture, drawn on a thread of its own (xr_text.cpp takes tens of ms).
std::vector<uint8_t> g_bannerPixels;
std::atomic<bool> g_bannerReady{false};
constexpr uint32_t kBannerW = 1024, kBannerH = 560;  // r43d: 420 + the stats line
constexpr uint32_t kSliderW = 640, kSliderH = 380;  // r44g: the settings panel's three rows
// r51b: the achievements list: one tall picture of every row (xrtext::achievements_list), 8 rows of
// it in view at a time, under a title bar (xrtext::achievements_header).
constexpr uint32_t kAchW = 1000, kAchRowH = 94, kAchShownRows = 8, kAchHeadH = 150;
constexpr uint32_t kAchViewH = kAchShownRows * kAchRowH + 24;
// The list's pictures, drawn on a thread of their own (xrtext takes tens of ms a row block) and
// uploaded on the game thread once ready; achGen tells a stale drawing from the one wanted.
std::mutex g_achMutex;
std::vector<uint8_t> g_achListPixels, g_achHeadPixels;  // under g_achMutex
uint32_t g_achListH = 0;                                // under g_achMutex
std::atomic<int> g_achReadyGen{0};
// r57: the RetroAchievements badge board over the trophy shelf, drawn on a thread
// (xrtext::badge_board) whenever the unlocks or the saved badge pictures change.
std::mutex g_boardMutex;
std::vector<uint8_t> g_boardPixels;  // under g_boardMutex
uint32_t g_boardH = 0;               // under g_boardMutex
std::atomic<int> g_boardReadyGen{0};
constexpr uint32_t kBoardW = 640;
constexpr int kBoardColumns = 8;
constexpr int kBoardHeader = 110;  // the board's height = this + rows * 76 + 40 (pixels)
// r60: the card for the badge under a pointer (achievement_toast), drawn on a thread.
std::mutex g_tipMutex;
std::vector<uint8_t> g_tipPixels;  // under g_tipMutex
std::atomic<int> g_tipReadyGen{0};
constexpr uint32_t kTipW = 768, kTipH = 225;
// r61: a text panel drawn on a thread and shown once it is ready (the home ground's boards).
struct AsyncPanel {
  std::mutex mutex;
  std::vector<uint8_t> pixels;  // under mutex
  uint32_t width = 0, height = 0;
  std::atomic<int> ready{0};
  int requested = 0, shown = 0;
  int forGeneration = -1;  // what it was last asked to show (the caller's numbering)
};
AsyncPanel g_homeBoard, g_ticket, g_cupBoard;
AsyncPanel g_guestPicker, g_guestTip;  // r67
AsyncPanel g_franchise, g_frTip;       // r70
AsyncPanel g_frIntro, g_frSeries;      // r70b
AsyncPanel g_frLive;                   // r77: the live XP meter
PortFranchiseLive g_frLiveData = {};   // r77: what it shows (under g_frMutex)
// r87: the meter's level-up light per row (captain, co-captain, sidekicks): seconds into it (< 0 none) and
// the level it went up from (under g_frMutex).
struct FrLiveGlow {
  float t[3] = {-1.0f, -1.0f, -1.0f};
  int from[3] = {-1, -1, -1};
};
FrLiveGlow g_frLiveGlow;
AsyncPanel g_frEvo;                    // r70c: the evolution scene (redrawn ~15 times a second)
AsyncPanel g_frTickerP, g_frTvP;       // r73: the news wire and the TV in your room
constexpr uint32_t kFrTickerW = 1600, kFrTickerH = 72, kFrTvW = 960, kFrTvH = 600;
constexpr uint32_t kFrEvoW = 1152, kFrEvoH = 693;
// r70b: the franchise match on now (intro card, series board) and the room's bunting, set on the game
// thread (the pad hook); the card under g_frMutex, the bunting colours atomic (0 = none).
std::atomic<uint32_t> g_frBunting1{0}, g_frBunting2{0};
std::atomic<bool> g_frBuntingOn{false}, g_frChampion{false};

// r70: what the franchise hub's picture is drawn from: filled on the game thread (the pad hook) whenever
// it changes, read by the drawing threads under the mutex.
struct FrClubInfo {
  std::string id, name, place, div, conf;
  uint32_t c1 = 0, c2 = 0;
};
struct FrSnapshot {
  int mode = 0, cursor = 0, page = 0, slot = 0;
  bool confirmDelete = false, canPlay = false;
  PortFranchiseSlot slots[PORT_FR_SLOTS] = {};
  PortFranchiseView view = {};
  PortFranchiseToday today = {};  // r70b: today's other games (to watch one)
  std::vector<FrClubInfo> clubs;
  // r70c: an evolution on show (mode 3)
  PortFranchiseEvolution evo = {};
  int evoStage = 0, evoChoice = 0;
  bool evoNewShiny = false;
  // r75: picking your own players (mode 4): the stage, the club, the captain picked (menu pick, -1 none)
  int pickStage = 0, pickClub = 0, pickCap = -1;
  // r76, co-op: the careers screen's co-op card (shown with a friend in the room or one saved), its
  // state (PortFranchiseCoopState) and saved career; the loaded career is co-op (and run by the friend)
  bool coopShown = false, coopActive = false, coopGuest = false;
  int coopState = 0;
  PortFranchiseSlot coopSlot = {};
  std::string coopName;
  std::vector<PortFranchiseEvoRow> evoWatch;  // r77: the Evo Watch page
  std::string evoReadyName;                   // r95: one of yours ready to evolve (postponed too), "" none
  // r77: trades (mode 5): the stage, what you give and its value per option, their offers, the answer
  int tradeStage = 0, tradeWhat = 0;
  bool tradeOpen = false;
  std::string tradeGive[3];
  int tradeGiveValue[3] = {0, 0, 0};
  std::vector<PortFranchiseTradeOffer> offers;
  std::string tradeMsg;
  // r100, online leagues: the careers screen's league card (one saved here, its code), the make / join screen,
  // the join lobby, the open league
  bool leagueSaved = false;
  PortFranchiseSlot leagueSlot = {};
  std::string leagueCode;
  int lgStage = 0, lgMax = 8, lgPos = 0;
  std::string lgTyped;
  PortFranchiseLeagueLobby lobby = {};
  PortFranchiseLeagueView league = {};
};
// r100: a league code's letters (no 0 / O / 1 / I: xr_league.cpp's)
constexpr const char* kFrCodeChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
std::mutex g_frMutex;
FrSnapshot g_frSnap;
PortFranchiseCard g_frCard = {};  // r70b: under g_frMutex
// r73: what the news wire and the TV show (under g_frMutex)
struct FrRoom {
  std::vector<std::string> lines;  // the wire's lines, in turn
  PortFranchiseView view = {};
  std::vector<std::string> clubIds, clubPlaces;
};
FrRoom g_frRoom;
// The club picker's grid: 8 columns x 4 rows, a conference in two rows, a division in 4 columns.
int fr_club_at(int row, int col) {
  const int conf = row / 2, div = (row % 2) * 2 + col / 4, k = col % 4;
  return conf * 16 + div * 4 + k;
}
void fr_club_place(int club, int* row, int* col) {
  const int conf = club / 16, div = (club % 16) / 4, k = club % 4;
  *row = conf * 2 + div / 2;
  *col = (div % 2) * 4 + k;
}
// The hub's state as the game has it now, for the next picture (game thread).
void fr_refresh() {
  FrSnapshot s;
  s.mode = g.frMode;
  s.cursor = g.frCursor;
  s.page = g.frPage;
  s.slot = g.frSlot;
  s.confirmDelete = g.frConfirmDelete;
  s.canPlay = PortFranchiseCanPlay();
  if (g.frMode == 0) PortFranchiseSlots(s.slots);  // (reads the three save files)
  PortFranchiseGetView(&s.view);
  PortFranchiseTodayGames(&s.today);
  s.evo = g.frEvo;
  s.evoStage = g.frEvoStage;
  s.evoChoice = g.frEvoChoice;
  s.evoNewShiny = g.frEvoNewShiny;
  s.pickStage = g.frPickStage;
  s.pickClub = g.frPickClub;
  s.pickCap = g.frPickCap;
  // r76, co-op
  s.coopState = PortFranchiseCoopState();
  const bool coopSaved = g.frMode == 0 && PortFranchiseCoopFile(&s.coopSlot);
  s.coopShown = s.coopState != 0 || coopSaved;
  s.coopActive = PortFranchiseCoopActive();
  s.coopGuest = PortFranchiseCoopGuest();
  s.coopName = PortNetCoopFriendName();
  // r100, online leagues
  if (g.frMode == 0) {
    char code[12] = {};
    s.leagueSaved = PortFranchiseLeagueFile(&s.leagueSlot, code, sizeof(code));
    s.leagueCode = code;
  }
  s.lgStage = g.frLgStage;
  s.lgMax = g.frLgMax;
  s.lgPos = g.frLgPos;
  for (int i = 0; i < 6; ++i) s.lgTyped.push_back(kFrCodeChars[g.frLgChar[i] & 31]);
  if (g.frMode == 7) PortFranchiseLeagueLobbyGet(&s.lobby);
  PortFranchiseLeagueGetView(&s.league);
  if (s.league.active) s.today.count = 0;  // (no watching in a league: the week waits on its people)
  if (g.frMode == 2) {  // r95: the page bar marks the Evo Watch while one of yours is ready
    PortFranchiseEvolution ready;
    if (PortFranchiseReadyEvolution(&ready)) s.evoReadyName = ready.fromName;
  }
  if (g.frMode == 2 && g.frPage == 8) {  // r77
    s.evoWatch.resize(80);
    s.evoWatch.resize(
        static_cast<size_t>(PortFranchiseEvoWatch(s.evoWatch.data(), static_cast<int>(s.evoWatch.size()))));
  }
  if (g.frMode == 5) {  // r77: trades
    s.tradeStage = g.frTradeStage;
    s.tradeWhat = g.frTradeWhat;
    s.tradeOpen = PortFranchiseTradeOpen();
    s.tradeMsg = g.frTradeMsg;
    PortFranchiseTradeOffer scratch[1];
    for (int what = 0; what < 3; ++what) {
      char name[64] = {};
      const int n = PortFranchiseTradeOptions(what, scratch, 1, &s.tradeGiveValue[what], name, sizeof(name));
      s.tradeGive[what] = n > 0 || name[0] != '\0' ? name : "";
    }
    if (g.frTradeStage == 1) {
      int value = 0;
      s.offers.resize(70);
      s.offers.resize(static_cast<size_t>(PortFranchiseTradeOptions(g.frTradeWhat, s.offers.data(),
                                                                     static_cast<int>(s.offers.size()), &value, nullptr, 0)));
    }
  }
  const int n = PortFranchiseClubCount();
  for (int i = 0; i < n; ++i) {
    FrClubInfo c;
    c.id = PortFranchiseClubId(i);
    c.name = PortFranchiseClubName(i);
    c.place = PortFranchiseClubPlace(i);
    c.div = PortFranchiseDivisionName(i);
    c.conf = PortFranchiseConferenceName(i);
    c.c1 = PortFranchiseClubColor(i, 0);
    c.c2 = PortFranchiseClubColor(i, 1);
    s.clubs.push_back(c);
  }
  std::lock_guard<std::mutex> lock(g_frMutex);
  g_frSnap = std::move(s);
  ++g.frGen;
}
// r68e: the character select menu (Lance: "the large ass character selection menu"): everyone on one
// big panel, the Strikers characters and the guests, a section per game, each starting a row.
constexpr int kMenuCaptainCols = 12, kMenuSidekickCols = 4;
constexpr uint32_t kGuestW = 2160, kGuestH = 1300, kGuestTipW = 640, kGuestTipH = 90;

// r67: a guest's portrait, its own game's art (STRIKERS_HOME/guests/portraits/<pack>.png,
// tools/outside/build_portraits.py), read once; empty when there is none. Read from the picker's
// drawing threads, so behind a mutex; entries never move once made.
struct GuestPortrait {
  std::vector<uint8_t> rgba;
  int w = 0, h = 0;
};
const GuestPortrait& guest_portrait(const std::string& pack) {
  static std::mutex mutex;
  static std::unordered_map<std::string, GuestPortrait> cache;
  std::lock_guard<std::mutex> lock(mutex);
  auto it = cache.find(pack);
  if (it != cache.end()) return it->second;
  GuestPortrait& p = cache[pack];
  const char* home = std::getenv("STRIKERS_HOME");
  if (home != nullptr && !pack.empty()) {
    const std::string path = std::string(home) + "/guests/portraits/" + pack + ".png";
    int iw = 0, ih = 0, n = 0;
    unsigned char* px = stbi_load(path.c_str(), &iw, &ih, &n, 4);
    if (px != nullptr) {
      p.rgba.assign(px, px + static_cast<size_t>(iw) * ih * 4);
      p.w = iw;
      p.h = ih;
      stbi_image_free(px);
    }
  }
  return p;
}
void draw_franchise_pick(uint8_t* px, int w, int h, const FrSnapshot& s);  // r75 (after the menu's entries)
std::string fr_upper(std::string s);                                         // (below)
// r70: the franchise hub's picture (a drawing thread): the careers, a new career's club, or the hub's
// pages (the club, the standings, last week's results). Logos: guests/portraits/club_<id>.png and
// franchise_league.png (tools/outside/franchise_data.py).
void draw_franchise(uint8_t* px, int w, int h, const FrSnapshot& s) {
  const int nClubs = static_cast<int>(s.clubs.size());
  auto club_logo = [&](int club) -> const GuestPortrait& {
    static const GuestPortrait none;
    if (club < 0 || club >= nClubs) return none;
    return guest_portrait("club_" + s.clubs[club].id);
  };
  auto logo_line = [&](xrtext::FrLine& l, int club) {
    const GuestPortrait& p = club_logo(club);
    if (!p.rgba.empty()) {
      l.logo = p.rgba.data();
      l.logoW = p.w;
      l.logoH = p.h;
    }
  };
  auto line = [](const std::string& text, const std::string& right = std::string()) {
    xrtext::FrLine l;
    l.text = text;
    l.right = right;
    return l;
  };
  auto record = [](int wins, int losses) { return std::to_string(wins) + "-" + std::to_string(losses); };
  const GuestPortrait& league = guest_portrait("franchise_league");
  const uint8_t* leagueRgba = league.rgba.empty() ? nullptr : league.rgba.data();
  if (s.mode == 1) {
    // A new career's club: all 32 by conference (two rows each) and division (four across).
    std::vector<xrtext::GuestTile> tiles;
    std::vector<std::string> tags(nClubs);
    for (int i = 0; i < nClubs; ++i) {
      const FrClubInfo& c = s.clubs[i];
      tags[i] = c.conf + " - " + c.div;
      const GuestPortrait& p = club_logo(i);
      xrtext::GuestTile t{p.rgba.empty() ? nullptr : p.rgba.data(), p.w, p.h, c.name.c_str(), tags[i].c_str()};
      int r = 0, col = 0;
      fr_club_place(i, &r, &col);
      t.row = r;
      t.col = col;
      t.frame = c.c1;
      tiles.push_back(t);
    }
    char title[96];
    if (s.slot == PORT_FR_COOP_SLOT)  // r76
      std::snprintf(title, sizeof(title), "NEW CO-OP CAREER   -   CHOOSE YOUR CLUB");
    else if (s.slot == PORT_FR_LEAGUE_SLOT)  // r100
      std::snprintf(title, sizeof(title), "NEW ONLINE LEAGUE (UP TO %d PEOPLE)   -   CHOOSE YOUR CLUB", s.lgMax);
    else
      std::snprintf(title, sizeof(title), "NEW CAREER (SLOT %d)   -   CHOOSE YOUR CLUB", s.slot + 1);
    xrtext::guest_picker(px, w, h, title, tiles.data(), static_cast<int>(tiles.size()), 8, s.cursor,
                         "STICK: MOVE   -   A: THIS CLUB (THEN PICK YOUR PLAYERS)   -   B: BACK");
    return;
  }
  if (s.mode == 4) {
    draw_franchise_pick(px, w, h, s);
    return;
  }
  if (s.mode == 6) {
    // r100 (Lance: "UP TO 8 player online ... leagues"): make a league (how many people), or join one by its code
    std::vector<xrtext::FrCard> lc;
    xrtext::FrCard make;
    make.title = "MAKE A LEAGUE";
    make.lines.push_back(line("YOU PICK YOUR CLUB AND PLAYERS"));
    make.lines.push_back(line("YOUR FRIENDS JOIN WITH THE CODE"));
    make.lines.push_back(line("EVERY OTHER CLUB: THE COMPUTER"));
    make.lines.push_back(line(""));
    xrtext::FrLine people = line(s.cursor == 0 ? "UP / DOWN: PEOPLE" : "PEOPLE", "UP TO " + std::to_string(s.lgMax));
    people.gold = s.cursor == 0;
    make.lines.push_back(people);
    make.lines.push_back(line(""));
    xrtext::FrLine go = line(s.cursor == 0 ? "A: MAKE IT" : "MAKE IT");
    go.cursor = s.cursor == 0;
    go.dim = s.cursor != 0;
    make.lines.push_back(go);
    lc.push_back(make);
    xrtext::FrCard join;
    join.title = "JOIN A LEAGUE";
    join.lines.push_back(line("TYPE THE CODE YOUR FRIEND GAVE YOU"));
    join.lines.push_back(line("THEN PICK A FREE CLUB"));
    join.lines.push_back(line(""));
    std::string typed;
    for (int i = 0; i < 6; ++i) {
      const char ch = i < static_cast<int>(s.lgTyped.size()) ? s.lgTyped[i] : 'A';
      if (i > 0) typed += " ";
      typed += s.cursor == 1 && s.lgStage == 1 && i == s.lgPos ? std::string("[") + ch + "]" : std::string(" ") + ch + " ";
    }
    xrtext::FrLine code = line("CODE:  " + typed);
    code.gold = s.cursor == 1;
    join.lines.push_back(code);
    join.lines.push_back(line(""));
    xrtext::FrLine jg = line(s.cursor != 1 ? "JOIN" : (s.lgStage == 1 ? "A: JOIN THIS LEAGUE" : "A: TYPE THE CODE"));
    jg.cursor = s.cursor == 1;
    jg.dim = s.cursor != 1;
    join.lines.push_back(jg);
    lc.push_back(join);
    xrtext::franchise_board(px, w, h, "ONLINE LEAGUE", "UP TO 8 PEOPLE, ONE CLUB EACH, ONE SEASON TOGETHER", 0x16306E,
                            0xFFD23F, leagueRgba, league.w, league.h, lc,
                            s.lgStage == 1 ? "LEFT / RIGHT: WHICH LETTER   -   UP / DOWN: CHANGE IT   -   A: JOIN   -   B: BACK"
                                           : "LEFT / RIGHT: MAKE OR JOIN   -   A: CHOOSE   -   B: BACK");
    return;
  }
  if (s.mode == 7) {
    // r100: joining a league: connecting, then its clubs (the free ones to pick; who has the rest)
    const PortFranchiseLeagueLobby& lb = s.lobby;
    if (lb.state == 3) {
      std::vector<xrtext::GuestTile> tiles;
      std::vector<std::string> tags(nClubs);
      for (int i = 0; i < nClubs && i < PORT_FR_CLUBS; ++i) {
        const FrClubInfo& c = s.clubs[i];
        std::string who = lb.taken[i] ? std::string("TAKEN: ") + lb.takenBy[i] : std::string(lb.captain[i]);
        for (char& ch : who) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        tags[i] = who.empty() ? c.conf + " - " + c.div : who;
        const GuestPortrait& p = club_logo(i);
        xrtext::GuestTile t{p.rgba.empty() ? nullptr : p.rgba.data(), p.w, p.h, c.name.c_str(), tags[i].c_str()};
        int r = 0, col = 0;
        fr_club_place(i, &r, &col);
        t.row = r;
        t.col = col;
        t.frame = c.c1;
        t.taken = lb.taken[i];
        tiles.push_back(t);
      }
      char title[128];
      std::snprintf(title, sizeof(title), "LEAGUE %s (%d OF %d PEOPLE)   -   PICK A FREE CLUB%s%s", lb.code, lb.members,
                    lb.max, lb.error[0] != '\0' ? "   -   " : "", lb.error[0] != '\0' ? fr_upper(lb.error).c_str() : "");
      xrtext::guest_picker(px, w, h, title, tiles.data(), static_cast<int>(tiles.size()), 8, s.cursor,
                           "STICK: MOVE   -   A: THIS CLUB (ITS PLAYERS COME WITH IT)   -   B: BACK");
      return;
    }
    std::vector<xrtext::FrCard> lc;
    xrtext::FrCard c;
    c.title = std::string("LEAGUE ") + lb.code;
    c.weight = 2.0f;
    std::string what;
    switch (lb.state) {
      case 1: what = "CONNECTING..."; break;
      case 2: what = "NO LEAGUE WITH THAT CODE"; break;
      case 4: what = "JOINING..."; break;
      case 5: what = "JOINED!"; break;
      case 6: what = "THAT LEAGUE IS FULL"; break;
      default: what = "..."; break;
    }
    xrtext::FrLine l = line(what);
    l.gold = true;
    c.lines.push_back(l);
    if (lb.owner[0] != '\0') c.lines.push_back(line("ITS OWNER: " + fr_upper(lb.owner)));
    if (lb.max > 0 && lb.state != 2) c.lines.push_back(line(std::to_string(lb.members) + " OF " + std::to_string(lb.max) + " PEOPLE"));
    if (lb.error[0] != '\0') {
      xrtext::FrLine e = line(fr_upper(lb.error));
      e.dim = true;
      c.lines.push_back(e);
    }
    lc.push_back(c);
    xrtext::franchise_board(px, w, h, "ONLINE LEAGUE", "JOINING WITH A CODE", 0x16306E, 0xFFD23F, leagueRgba, league.w,
                            league.h, lc, "B: BACK");
    return;
  }
  if (s.mode == 5) {
    // r77 (Lance: "i couldnt see how tf to operate trades"): what you give, then who you could get for it
    // (best value first) and whether their club would say yes
    const int me = s.view.myClub;
    std::vector<xrtext::FrCard> tc;
    xrtext::FrCard give;
    give.title = "YOU GIVE";
    static const char* const kWhat[3] = {"YOUR CAPTAIN", "YOUR BACKUP CAPTAIN", "YOUR SIDEKICKS"};
    for (int i = 0; i < 3; ++i) {
      const bool has = !s.tradeGive[i].empty();
      const bool on = s.tradeStage == 0 ? s.cursor == i : s.tradeWhat == i;
      xrtext::FrLine l = line(std::string(kWhat[i]) + ": " + (has ? s.tradeGive[i] : std::string("NONE")),
                              has ? "VALUE " + std::to_string(s.tradeGiveValue[i]) : std::string());
      l.cursor = on;
      l.gold = on && s.tradeStage == 1;
      l.dim = !has;
      give.lines.push_back(l);
    }
    give.lines.push_back(line(""));
    xrtext::FrLine rule = line("THEY SAY YES WHEN WHAT THEY GET IS WORTH 90% OF WHAT THEY GIVE");
    rule.dim = true;
    give.lines.push_back(rule);
    xrtext::FrLine rule2 = line("VALUE = RATING + A POINT A LEVEL OVER 8");
    rule2.dim = true;
    give.lines.push_back(rule2);
    xrtext::FrLine when = line("OPEN UNTIL WEEK 12, AND AGAIN AFTER THE SEASON");
    when.dim = true;
    give.lines.push_back(when);
    if (!s.tradeOpen) {
      xrtext::FrLine shut = line("THE TRADE WINDOW IS SHUT RIGHT NOW");
      shut.gold = true;
      give.lines.push_back(shut);
    }
    if (!s.tradeMsg.empty()) {
      xrtext::FrLine m = line(s.tradeMsg);
      m.gold = true;
      give.lines.push_back(m);
    }
    tc.push_back(give);
    if (s.tradeStage == 1) {
      xrtext::FrCard offers;
      offers.title = "WHO YOU COULD GET";
      offers.weight = 1.6f;
      const int n = static_cast<int>(s.offers.size());
      const int rows = 12;
      const int first = std::max(0, std::min(s.cursor - rows / 2, n - rows));
      for (int i = first; i < n && i < first + rows; ++i) {
        const PortFranchiseTradeOffer& o = s.offers[i];
        std::string t = std::string(o.name) + (o.shiny ? " (SHINY)" : "") + "  LV " + std::to_string(o.level);
        if (s.tradeWhat != 2) t += o.backup ? "  (BACKUP)" : "  (CAPTAIN)";
        xrtext::FrLine l = line(i == s.cursor ? "A: " + t : t, std::to_string(o.value) + (o.fair ? "  YES" : "  NO"));
        logo_line(l, o.club);
        l.cursor = i == s.cursor;
        l.dim = !o.fair;
        offers.lines.push_back(l);
      }
      if (n == 0) offers.lines.push_back(line("NOBODY TO TRADE WITH"));
      tc.push_back(offers);
    }
    const FrClubInfo noClub;
    const FrClubInfo& myClub = me >= 0 && me < nClubs ? s.clubs[me] : noClub;
    const GuestPortrait& lg = club_logo(me);
    xrtext::franchise_board(px, w, h, "TRADES", myClub.name, myClub.c1 != 0 ? myClub.c1 : 0x16306E,
                            myClub.c2 != 0 ? myClub.c2 : 0xFFD23F, lg.rgba.empty() ? nullptr : lg.rgba.data(), lg.w, lg.h, tc,
                            s.tradeStage == 0
                                ? "UP / DOWN: WHAT YOU GIVE   -   A: SEE WHO YOU COULD GET   -   B: BACK TO THE HUB"
                                : "UP / DOWN: PICK A PLAYER   -   A: OFFER THE TRADE   -   B: BACK");
    return;
  }
  std::vector<xrtext::FrCard> cards;
  if (s.mode == 0) {
    // The three careers.
    for (int i = 0; i < PORT_FR_SLOTS; ++i) {
      const PortFranchiseSlot& sl = s.slots[i];
      const bool on = i == s.cursor;
      xrtext::FrCard c;
      c.title = "CAREER " + std::to_string(i + 1);
      if (sl.used) {
        int club = -1;
        for (int k = 0; k < nClubs; ++k)
          if (s.clubs[k].id == sl.clubId) club = k;
        xrtext::FrLine name = line(sl.club);
        logo_line(name, club);
        name.gold = true;
        c.lines.push_back(name);
        c.lines.push_back(line("SEASON " + std::to_string(sl.season)));
        c.lines.push_back(sl.week > PORT_FR_WEEKS ? line("PLAYOFFS")
                                                  : line("WEEK " + std::to_string(sl.week) + " OF 20"));
        c.lines.push_back(line("RECORD", record(sl.wins, sl.losses)));
        c.lines.push_back(line(""));
        xrtext::FrLine go = line(on ? "A: CONTINUE" : "CONTINUE");
        go.cursor = on;
        go.dim = !on;
        c.lines.push_back(go);
        xrtext::FrLine del = line(on && s.confirmDelete ? "X AGAIN: DELETE IT FOR GOOD" : "X: DELETE");
        del.gold = on && s.confirmDelete;
        del.dim = !on;
        c.lines.push_back(del);
      } else {
        xrtext::FrLine e = line("EMPTY");
        e.dim = true;
        c.lines.push_back(e);
        c.lines.push_back(line(""));
        c.lines.push_back(line(""));
        c.lines.push_back(line(""));
        c.lines.push_back(line(""));
        xrtext::FrLine go = line(on ? "A: NEW CAREER" : "NEW CAREER");
        go.cursor = on;
        go.dim = !on;
        c.lines.push_back(go);
        c.lines.push_back(line(""));
      }
      cards.push_back(c);
    }
    if (s.coopShown) {
      // r76: the co-op career (played online with the friend in the room, both keep it)
      const PortFranchiseSlot& sl = s.coopSlot;
      const bool on = s.cursor == PORT_FR_COOP_SLOT;
      std::string friendName = s.coopName;
      for (char& ch : friendName) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      xrtext::FrCard c;
      c.title = "CO-OP CAREER";
      xrtext::FrLine who = line(s.coopState == 0 ? "NO FRIEND IN THE ROOM"
                                                 : "WITH " + (friendName.empty() ? std::string("YOUR FRIEND") : friendName));
      who.dim = s.coopState == 0;
      c.lines.push_back(who);
      if (sl.used) {
        int club = -1;
        for (int k = 0; k < nClubs; ++k)
          if (s.clubs[k].id == sl.clubId) club = k;
        xrtext::FrLine name = line(sl.club);
        logo_line(name, club);
        name.gold = true;
        c.lines.push_back(name);
        c.lines.push_back(line("SEASON " + std::to_string(sl.season) + "  " +
                               (sl.week > PORT_FR_WEEKS ? std::string("PLAYOFFS") : "WEEK " + std::to_string(sl.week))));
        c.lines.push_back(line("RECORD", record(sl.wins, sl.losses)));
      } else {
        xrtext::FrLine e = line(s.coopState == 3 || s.coopState == 4 ? "THEIRS OR A NEW ONE" : "NONE YET");
        e.dim = true;
        c.lines.push_back(e);
        c.lines.push_back(line(""));
        c.lines.push_back(line(""));
      }
      std::string act;
      switch (s.coopState) {
        case 0: act = "HOST OR JOIN AN ONLINE MATCH FIRST"; break;
        case 1: act = "TALKING TO YOUR FRIEND..."; break;
        case 2: act = sl.used ? "PLAY TOGETHER" : "NEW CO-OP CAREER"; break;
        case 3: act = "YOUR FRIEND OPENS IT (THEY HOST)"; break;
        case 4: act = "JOIN YOUR FRIEND"; break;
        default: act = "GETTING THE CAREER..."; break;
      }
      const bool can = s.coopState == 2 || s.coopState == 4;
      xrtext::FrLine go = line(on && can ? "A: " + act : act);
      go.cursor = on && can;
      go.dim = !on || !can;
      c.lines.push_back(go);
      xrtext::FrLine sub = line(s.coopState == 0 ? "(THE PANEL: HOST AN ONLINE MATCH / JOIN WITH A CODE)"
                                                 : "SAME CLUB: YOU SPLIT THE TEAM");
      sub.dim = true;
      c.lines.push_back(sub);
      if (sl.used) {
        xrtext::FrLine del = line(on && s.confirmDelete ? "X AGAIN: DELETE IT FOR GOOD" : "X: DELETE");
        del.gold = on && s.confirmDelete;
        del.dim = !on;
        c.lines.push_back(del);
      }
      cards.push_back(c);
    }
    {
      // r100: the online league (up to 8 people, a club each): the one this headset is in, or make / join one
      const PortFranchiseSlot& sl = s.leagueSlot;
      const bool on = s.cursor == PORT_FR_LEAGUE_SLOT;
      xrtext::FrCard c;
      c.title = "ONLINE LEAGUE";
      if (s.leagueSaved) {
        int club = -1;
        for (int k = 0; k < nClubs; ++k)
          if (s.clubs[k].id == sl.clubId) club = k;
        xrtext::FrLine name = line(sl.club);
        logo_line(name, club);
        name.gold = true;
        c.lines.push_back(name);
        c.lines.push_back(line("CODE " + s.leagueCode));
        c.lines.push_back(line("SEASON " + std::to_string(sl.season) + "  " +
                               (sl.week > PORT_FR_WEEKS ? std::string("PLAYOFFS") : "WEEK " + std::to_string(sl.week))));
        c.lines.push_back(line("RECORD", record(sl.wins, sl.losses)));
        c.lines.push_back(line(""));
        xrtext::FrLine go = line(on ? "A: CONTINUE" : "CONTINUE");
        go.cursor = on;
        go.dim = !on;
        c.lines.push_back(go);
        xrtext::FrLine del = line(on && s.confirmDelete ? "X AGAIN: LEAVE IT FOR GOOD" : "X: LEAVE");
        del.gold = on && s.confirmDelete;
        del.dim = !on;
        c.lines.push_back(del);
      } else {
        xrtext::FrLine e = line("UP TO 8 PEOPLE");
        e.dim = true;
        c.lines.push_back(e);
        xrtext::FrLine e2 = line("A CLUB EACH, THE REST CPU");
        e2.dim = true;
        c.lines.push_back(e2);
        c.lines.push_back(line(""));
        c.lines.push_back(line(""));
        c.lines.push_back(line(""));
        xrtext::FrLine go = line(on ? "A: MAKE OR JOIN ONE" : "MAKE OR JOIN ONE");
        go.cursor = on;
        go.dim = !on;
        c.lines.push_back(go);
        c.lines.push_back(line(""));
      }
      cards.push_back(c);
    }
    xrtext::franchise_board(px, w, h, "FRANCHISE MODE", "STRIKERS SUPER LEAGUE", 0x16306E, 0xFFD23F, leagueRgba,
                            league.w, league.h, cards,
                            "STICK: CHOOSE A CAREER   -   A: PLAY IT   -   X: DELETE / LEAVE   -   B: CLOSE");
    return;
  }
  // The hub.
  const PortFranchiseView& v = s.view;
  const int me = v.myClub;
  const FrClubInfo none;
  const FrClubInfo& mine = me >= 0 && me < nClubs ? s.clubs[me] : none;
  const char* pages[11] = {"CLUB", "STANDINGS", "PLAYOFFS", "RESULTS", "LEADERS", "FEED", "HISTORY", "NEMESIS",
                           "EVO WATCH", "DIFFICULTY", "LEAGUE"};  // r77: the Evo Watch; r97: the difficulty; r100
  const PortFranchiseLeagueView& lg = s.league;  // r100: an online league's
  std::string pageBar;
  for (int i = 0; i < (lg.active ? 11 : 10); ++i) {
    if (i > 0) pageBar += "  ";
    std::string name = pages[i];
    if (i == 8 && !s.evoReadyName.empty()) name += " (READY!)";  // r95
    pageBar += i == s.page ? "[" + name + "]" : name;
  }
  const bool playoffs = v.phase == PORT_FR_PHASE_PLAYOFFS, done = v.phase == PORT_FR_PHASE_DONE;
  std::string subtitle = "SEASON " + std::to_string(v.season) + " - ";
  if (done)
    subtitle += "CHAMPIONS CROWNED";
  else if (playoffs)
    subtitle += std::string(v.roundName) + " (DAY " + std::to_string(v.week - PORT_FR_WEEKS) + ")";
  else
    subtitle += "WEEK " + std::to_string(v.week);
  {
    static const char* const kLevelShort[5] = {"", "ROOKIE", "PRO", "SUPERSTAR", "LEGEND"};  // r97
    subtitle += std::string(" - ") + kLevelShort[std::max(1, std::min(4, v.difficulty))];
  }
  std::string friendName = s.coopName;  // r76
  for (char& ch : friendName) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  if (friendName.empty()) friendName = "YOUR FRIEND";
  if (s.coopActive) subtitle = "CO-OP WITH " + friendName + " - " + subtitle;
  if (lg.active) subtitle = std::string("LEAGUE ") + lg.code + " - " + subtitle;  // r100
  std::string hint = "LEFT / RIGHT: " + pageBar;
  auto place_of = [&](int club) { return club >= 0 && club < nClubs ? s.clubs[club].place : std::string("?"); };
  // the band's logo: your club's; on the playoffs page, the round's
  const GuestPortrait* bandLogo = &club_logo(me);
  if (s.page == 0) {
    xrtext::FrCard club;
    club.title = "YOUR CLUB";
    club.weight = 1.15f;
    // r70c: levels (and shiny)
    auto lv = [](int level, bool shiny) {
      return "  LV " + std::to_string(level) + (shiny ? "  SHINY" : "");
    };
    xrtext::FrLine cap = line("CAPTAIN: " + std::string(v.captain) + lv(v.captainLevel, v.captainShiny),
                              std::to_string(v.captainGoals) + " G" + (v.startsBackup ? "" : "  STARTS"));
    cap.gold = !v.startsBackup;
    club.lines.push_back(cap);
    if (v.backup[0] != '\0') {
      xrtext::FrLine bk = line("BACKUP: " + std::string(v.backup) + lv(v.backupLevel, v.backupShiny),
                               std::to_string(v.backupGoals) + " G" + (v.startsBackup ? "  STARTS" : ""));
      bk.gold = v.startsBackup;
      club.lines.push_back(bk);
    } else {
      xrtext::FrLine bk = line("NO BACKUP CAPTAIN");
      bk.dim = true;
      club.lines.push_back(bk);
    }
    club.lines.push_back(line("SIDEKICKS: " + std::string(v.kind) + lv(v.sidekickLevel, false)));
    for (int k = 0; k < 3; ++k) {
      // r71: each one's goals this season; a rookie says so
      xrtext::FrLine sk = line("   " + std::string(v.sidekicks[k]) + (v.sidekickAge[k] == 0 ? "  ROOKIE" : ""),
                               std::to_string(v.sidekickGoals[k]) + " G");
      sk.dim = true;
      club.lines.push_back(sk);
    }
    club.lines.push_back(line("RECORD", record(v.wins, v.losses)));
    club.lines.push_back(line("GOALS FOR - AGAINST", std::to_string(v.goalsFor) + " - " + std::to_string(v.goalsAgainst)));
    club.lines.push_back(line(mine.div + " DIVISION", "#" + std::to_string(v.divisionRank) + " OF 4"));
    if (v.titles > 0) {
      xrtext::FrLine t = line("SUPER STRIKE SERIES TITLES", std::to_string(v.titles));
      t.gold = true;
      club.lines.push_back(t);
    }
    cards.push_back(club);
    xrtext::FrCard next;
    next.weight = 1.15f;
    if (playoffs && v.nextOpponent >= 0)
      next.title = std::string(v.roundName) + " - GAME " + std::to_string(v.myGameNo);
    else if (playoffs || done)
      next.title = done ? "THE SEASON'S OVER" : std::string(v.roundName);
    else
      next.title = "WEEK " + std::to_string(v.week) + " OF 20";
    if (v.nextOpponent >= 0 && v.nextOpponent < nClubs) {
      const FrClubInfo& o = s.clubs[v.nextOpponent];
      next.lines.push_back(line(v.nextHome ? "HOME, AT YOUR GROUND" : "AWAY, AT " + o.place));
      xrtext::FrLine opp = line("VS " + o.name);
      logo_line(opp, v.nextOpponent);
      opp.gold = true;
      next.lines.push_back(opp);
      if (playoffs) {
        const int a = v.mySeriesMe, b = v.mySeriesThem;
        next.lines.push_back(line(a == b ? "SERIES TIED" : (a > b ? "YOU LEAD THE SERIES" : "YOU TRAIL THE SERIES"),
                                  std::to_string(a) + "-" + std::to_string(b)));
        if (a == 2 || b == 2) {
          xrtext::FrLine el = line(a == 2 && b == 2 ? "GAME 5: WINNER TAKES THE SERIES"
                                                    : (a == 2 ? "WIN AND YOU'RE THROUGH" : "LOSE AND YOU'RE OUT"));
          el.gold = true;
          next.lines.push_back(el);
        }
      } else {
        next.lines.push_back(line("THEIR RECORD", record(v.oppWins, v.oppLosses)));
      }
      next.lines.push_back(line("THEIR CAPTAIN: " + std::string(v.oppCaptain)));
      if (v.nextNemesis >= 0) {  // r72
        const PortFranchiseView::Nem& n = v.nems[v.nextNemesis];
        xrtext::FrLine nl = line("NEMESIS: " + std::string(n.name) + " " + n.title,
                                 std::to_string(n.beatYou) + "-" + std::to_string(n.lostToYou));
        nl.gold = true;
        next.lines.push_back(nl);
      }
      if (lg.active && lg.vsMember >= 0 && lg.vsMember < lg.memberCount) {
        // r100: a person's club: played online, once you're both ready (the home club's headset hosts)
        const PortFranchiseLeagueView::Member& them = lg.members[lg.vsMember];
        const std::string who = fr_upper(them.name);
        xrtext::FrLine p = line("A PERSON'S CLUB: " + who, them.online ? "ONLINE" : "OFFLINE");
        p.gold = true;
        next.lines.push_back(p);
        std::string act;
        if (!s.canPlay && !lg.meReady)
          act = "BACK TO THE MAIN MENU TO PLAY";
        else if (!lg.synced && !lg.meReady)
          act = "GETTING THE LEAGUE'S NEWEST...";
        else if (lg.meReady)
          act = "READY: WAITING FOR " + who + "  (A: NOT READY)";
        else
          act = std::string("A: READY TO PLAY ") + who + " ONLINE" + (lg.themReady ? "  - THEY'RE READY!" : "");
        xrtext::FrLine go = line(act);
        go.cursor = s.cursor == 0 && (lg.meReady || (s.canPlay && lg.synced));
        go.dim = !go.cursor;
        next.lines.push_back(go);
        xrtext::FrLine how = line(them.online ? "YOU BOTH SAY READY, THE MATCH STARTS ON BOTH HEADSETS"
                                              : "THEY'RE OFFLINE: THE OWNER CAN SIM THE REST (LEAGUE PAGE)");
        how.dim = true;
        next.lines.push_back(how);
      } else {
        xrtext::FrLine go = line(!s.canPlay ? "BACK TO THE MAIN MENU TO PLAY"
                                            : (s.cursor == 0 ? "A: PLAY THIS MATCH" : "PLAY THIS MATCH"));
        go.cursor = s.canPlay && s.cursor == 0;
        go.dim = !s.canPlay;
        next.lines.push_back(go);
        // r70b: or work it out (and in the regular season, every week to the playoffs)
        xrtext::FrLine sim = line(s.cursor == 1 ? "A: SIM THIS MATCH" : "SIM THIS MATCH");
        sim.cursor = s.cursor == 1;
        sim.dim = s.cursor != 1;
        next.lines.push_back(sim);
      }
      if (!playoffs && !lg.active) {  // (r100: a league's weeks wait on its people)
        xrtext::FrLine all = line(s.cursor == 2 ? "A: SIM TO THE PLAYOFFS" : "SIM TO THE PLAYOFFS");
        all.cursor = s.cursor == 2;
        all.dim = s.cursor != 2;
        next.lines.push_back(all);
      }
      if (v.backup[0] != '\0') {
        xrtext::FrLine sw = line(v.startsBackup ? "X: YOUR CAPTAIN STARTS" : "X: YOUR BACKUP STARTS");
        sw.dim = true;
        next.lines.push_back(sw);
      }
      hint += lg.active && lg.vsMember >= 0 ? "   -   A: READY / NOT READY   -   X: SWAP STARTING CAPTAIN   -   B: CAREERS"
                                            : "   -   UP / DOWN + A: PLAY OR SIM   -   X: SWAP STARTING CAPTAIN   -   B: CAREERS";
    } else {
      xrtext::FrLine st = line(v.status[0] != '\0' ? std::string(v.status) : std::string("NO MATCH TODAY"));
      st.gold = true;
      next.lines.push_back(st);
      next.lines.push_back(line(""));
      if (v.canSimDay) {
        // r70b: play out the day, or watch one of today's games (CPU v CPU)
        xrtext::FrLine a = line(s.cursor == 0 ? "A: PLAY OUT THE DAY" : "PLAY OUT THE DAY");
        a.cursor = s.cursor == 0;
        next.lines.push_back(a);
        for (int i = 0; i < s.today.count && i < 6; ++i) {
          std::string t = "WATCH: " + place_of(s.today.home[i]) + " V " + place_of(s.today.away[i]);
          std::string r;
          const int si = s.today.series[i];
          if (si >= 0 && si < v.seriesCount) {
            const PortFranchiseSeries& sr = v.series[si];
            const int hw = sr.hi == s.today.home[i] ? sr.winsHi : sr.winsLo;
            r = std::to_string(hw) + "-" + std::to_string(sr.winsHi + sr.winsLo - hw);
          }
          xrtext::FrLine wl = line(s.cursor == i + 1 ? "A: " + t : t, r);
          logo_line(wl, s.today.home[i]);
          wl.cursor = s.cursor == i + 1;
          next.lines.push_back(wl);
        }
        xrtext::FrLine x = line("X: ON TO YOUR NEXT MATCH (OR THE FINAL WHISTLE)");
        x.dim = true;
        next.lines.push_back(x);
        hint += "   -   UP / DOWN + A: PLAY OUT THE DAY OR WATCH A GAME   -   X: SKIP AHEAD   -   B: CAREERS";
      } else if (done) {
        xrtext::FrLine a = line("A: START SEASON " + std::to_string(v.season + 1));
        a.cursor = true;
        next.lines.push_back(a);
        xrtext::FrLine k = line("SAME CLUBS AND PLAYERS, A NEW SCHEDULE");
        k.dim = true;
        next.lines.push_back(k);
        hint += "   -   A: NEXT SEASON   -   B: CAREERS";
      } else {
        hint += "   -   B: CAREERS";
      }
    }
    if (s.coopGuest) {
      // r76: the friend's headset runs the co-op career: its matches start from there (both join)
      xrtext::FrLine cl = line(friendName + " RUNS THIS CAREER: THEY START THE MATCHES, YOU JOIN IN");
      cl.gold = true;
      next.lines.insert(next.lines.begin(), cl);
      hint = "LEFT / RIGHT: " + pageBar + "   -   B: CAREERS";
    } else if (s.coopActive && s.coopState == 0) {
      xrtext::FrLine cl = line("CO-OP: YOUR FRIEND ISN'T IN THE ROOM, NOTHING PLAYS WITHOUT THEM");
      cl.dim = true;
      next.lines.insert(next.lines.begin(), cl);
    }
    if (lg.active && lg.link != 2) {  // r100
      xrtext::FrLine cl = line("CONNECTING TO THE LEAGUE...");
      cl.dim = true;
      next.lines.insert(next.lines.begin(), cl);
    }
    xrtext::FrLine head = line(v.headline);
    head.dim = true;
    next.lines.push_back(line(""));
    next.lines.push_back(head);
    cards.push_back(next);
    xrtext::FrCard div;
    div.title = mine.div + " DIVISION";
    for (int i = 0; i < 4; ++i) {
      const PortFranchiseRow& r = v.division[i];
      if (r.club < 0 || r.club >= nClubs) continue;
      xrtext::FrLine l = line(std::to_string(i + 1) + ". " + s.clubs[r.club].place, record(r.wins, r.losses));
      logo_line(l, r.club);
      l.gold = r.mine;
      div.lines.push_back(l);
    }
    cards.push_back(div);
  } else if (s.page == 1) {
    for (int conf = 0; conf < 2; ++conf) {
      xrtext::FrCard c;
      const int first = v.conference[conf][0].club;
      c.title = (first >= 0 && first < nClubs ? s.clubs[first].conf : std::string(conf == 0 ? "CROWN" : "STAR")) +
                " CONFERENCE";
      for (int i = 0; i < 16; ++i) {
        const PortFranchiseRow& r = v.conference[conf][i];
        if (r.club < 0 || r.club >= nClubs) continue;
        const int gd = r.goalsFor - r.goalsAgainst;
        const std::string gds = gd > 0 ? "+" + std::to_string(gd) : std::to_string(gd);
        bool seeded = false;
        for (int k = 0; k < 7; ++k)
          if (v.seeds[conf][k] == r.club) seeded = true;
        xrtext::FrLine l = line(std::to_string(i + 1) + ". " + s.clubs[r.club].name,
                                record(r.wins, r.losses) + "   " + gds);
        logo_line(l, r.club);
        l.gold = r.mine;
        l.dim = !seeded && !r.mine;  // (7 a conference make the playoffs: the division winners + 3)
        c.lines.push_back(l);
      }
      cards.push_back(c);
    }
    hint += "   -   BRIGHT: IN THE PLAYOFFS (DIVISION WINNERS + 3 WILD CARDS)   -   B: CAREERS";
  } else if (s.page == 2) {
    // The playoffs: each conference's bracket, the Super Strike Series in the middle. Before they start,
    // the picture as the table stands.
    auto series_line = [&](const PortFranchiseSeries& sr) {
      const std::string text = "(" + std::to_string(sr.hiSeed) + ") " + place_of(sr.hi) + "  VS  (" +
                               std::to_string(sr.loSeed) + ") " + place_of(sr.lo);
      xrtext::FrLine l = line(text, std::to_string(sr.winsHi) + "-" + std::to_string(sr.winsLo));
      logo_line(l, sr.winner >= 0 ? sr.winner : sr.hi);
      l.gold = sr.hi == me || sr.lo == me;
      l.dim = sr.winner >= 0 && !l.gold;
      return l;
    };
    xrtext::FrCard confCard[2];
    for (int conf = 0; conf < 2; ++conf) {
      xrtext::FrCard& c = confCard[conf];
      c.title = std::string(conf == 0 ? "CROWN" : "STAR") + " CONFERENCE";
      c.weight = 1.25f;
      if (v.phase == PORT_FR_PHASE_REGULAR) {
        xrtext::FrLine h = line("IF THE SEASON ENDED TODAY");
        h.dim = true;
        c.lines.push_back(h);
        for (int k = 0; k < 7; ++k) {
          const int club = v.seeds[conf][k];
          if (club < 0 || club >= nClubs) continue;
          xrtext::FrLine l = line("(" + std::to_string(k + 1) + ") " + s.clubs[club].name,
                                  k == 0 ? "BYE" : (k < 4 ? "DIV" : "WILD"));
          logo_line(l, club);
          l.gold = club == me;
          c.lines.push_back(l);
        }
        xrtext::FrLine r1 = line("KICKOFF ROUND: 2 V 7, 3 V 6, 4 V 5");
        r1.dim = true;
        c.lines.push_back(line(""));
        c.lines.push_back(r1);
        continue;
      }
      const int bye = v.seeds[conf][0];
      for (int round = 1; round <= 3; ++round) {
        bool any = false;
        for (int i = 0; i < v.seriesCount; ++i) {
          const PortFranchiseSeries& sr = v.series[i];
          if (sr.round != round || sr.conf != conf) continue;
          if (!any) {
            static const char* const kRound[4] = {"", "KICKOFF ROUND", "SEMIFINALS", "CONFERENCE FINALS"};
            xrtext::FrLine h = line(kRound[round], round == 1 ? "BYE: (1) " + place_of(bye) : std::string());
            h.dim = true;
            c.lines.push_back(h);
            any = true;
          }
          c.lines.push_back(series_line(sr));
        }
      }
    }
    xrtext::FrCard fin;
    fin.title = "SUPER STRIKE SERIES";
    fin.weight = 0.9f;
    for (int i = 0; i < v.seriesCount; ++i)
      if (v.series[i].round == 4) {
        const PortFranchiseSeries& sr = v.series[i];
        xrtext::FrLine a = line(s.clubs[sr.hi].name, std::to_string(sr.winsHi));
        logo_line(a, sr.hi);
        a.gold = sr.winner == sr.hi || sr.hi == me;
        xrtext::FrLine b = line(s.clubs[sr.lo].name, std::to_string(sr.winsLo));
        logo_line(b, sr.lo);
        b.gold = sr.winner == sr.lo || sr.lo == me;
        fin.lines.push_back(line("CROWN V STAR, BEST OF 5"));
        fin.lines.push_back(a);
        fin.lines.push_back(b);
      }
    if (v.champion >= 0 && v.champion < nClubs) {
      fin.lines.push_back(line(""));
      xrtext::FrLine ch = line("CHAMPIONS: " + s.clubs[v.champion].name);
      logo_line(ch, v.champion);
      ch.gold = true;
      fin.lines.push_back(ch);
    } else if (fin.lines.empty()) {
      xrtext::FrLine t = line("THE CROWN AND STAR CHAMPIONS");
      t.dim = true;
      fin.lines.push_back(t);
      xrtext::FrLine t2 = line("MEET HERE, BEST OF 5");
      t2.dim = true;
      fin.lines.push_back(t2);
    }
    cards.push_back(confCard[0]);
    cards.push_back(fin);
    cards.push_back(confCard[1]);
    if (v.phase != PORT_FR_PHASE_REGULAR) {
      const GuestPortrait& rl = guest_portrait(v.roundLogo);
      if (!rl.rgba.empty()) bandLogo = &rl;
    }
    hint += "   -   EVERY ROUND BEST OF 5, THE HIGHER SEED HOME IN GAMES 1, 2, 5   -   B: CAREERS";
  } else if (s.page == 4) {
    // r71: the scoring race, who's hot, who's not
    xrtext::FrCard race;
    race.title = "GOLDEN BOOT RACE";
    race.weight = 1.3f;
    for (int i = 0; i < v.leaderCount; ++i) {
      const PortFranchiseView::Leader& l = v.leaders[i];
      xrtext::FrLine ln = line(std::to_string(i + 1) + ". " + l.name + " (" + place_of(l.club) + ")", std::to_string(l.goals));
      logo_line(ln, l.club);
      ln.gold = l.club == me;
      race.lines.push_back(ln);
    }
    if (v.leaderCount == 0) race.lines.push_back(line("NO GOALS YET"));
    cards.push_back(race);
    xrtext::FrCard hot;
    hot.title = "ON FIRE";
    for (int i = 0; i < v.hotCount; ++i) {
      xrtext::FrLine ln = line(std::string(v.hot[i].name) + " (" + place_of(v.hot[i].club) + ")", "+" + std::to_string(v.hot[i].form));
      ln.gold = v.hot[i].club == me;
      hot.lines.push_back(ln);
    }
    if (v.hotCount == 0) hot.lines.push_back(line("NOBODY YET"));
    xrtext::FrLine gap = line("");
    hot.lines.push_back(gap);
    xrtext::FrLine coldHead = line("IN A SLUMP");
    coldHead.dim = true;
    hot.lines.push_back(coldHead);
    for (int i = 0; i < v.coldCount; ++i) {
      xrtext::FrLine ln = line(std::string(v.cold[i].name) + " (" + place_of(v.cold[i].club) + ")", std::to_string(v.cold[i].form));
      ln.dim = v.cold[i].club != me;
      hot.lines.push_back(ln);
    }
    cards.push_back(hot);
    hint += "   -   B: CAREERS";
  } else if (s.page == 5) {
    // r71: the players' feed
    xrtext::FrCard f;
    f.title = "PLAYER FEED";
    for (int i = 0; i < v.feedCount; ++i) f.lines.push_back(line(v.feed[i]));
    if (v.feedCount == 0) f.lines.push_back(line("QUIET WEEK"));
    cards.push_back(f);
    hint += "   -   B: CAREERS";
  } else if (s.page == 10 && lg.active) {
    // r100: the online league: its code, its people (online, their game this week in or not, ready), the owner's
    // SIM THE REST
    xrtext::FrCard ppl;
    ppl.title = std::string("LEAGUE ") + lg.code;
    ppl.weight = 1.6f;
    std::string waiting;
    for (int i = 0; i < lg.memberCount; ++i) {
      const PortFranchiseLeagueView::Member& m = lg.members[i];
      std::string state = !m.online ? "OFFLINE" : (m.ready ? "READY" : "ONLINE");
      state += m.done ? "  PLAYED" : "  TO PLAY";
      xrtext::FrLine l = line(fr_upper(m.name) + (m.owner ? " (OWNER)" : "") + "  -  " + place_of(m.club), state);
      logo_line(l, m.club);
      l.gold = m.me;
      l.dim = !m.online && !m.me;
      ppl.lines.push_back(l);
      if (!m.done) waiting += (waiting.empty() ? "" : ", ") + fr_upper(m.name);
    }
    xrtext::FrLine count = line(std::to_string(lg.memberCount) + " OF " + std::to_string(lg.max) + " PEOPLE");
    count.dim = true;
    ppl.lines.push_back(count);
    cards.push_back(ppl);
    xrtext::FrCard wk;
    wk.title = "THIS WEEK";
    xrtext::FrLine code = line(std::string("FRIENDS JOIN WITH CODE ") + lg.code);
    code.gold = true;
    wk.lines.push_back(code);
    wk.lines.push_back(line(waiting.empty() ? "EVERYONE'S GAME IS IN" : "WAITING ON: " + waiting));
    wk.lines.push_back(line(lg.link == 2 ? "CONNECTED" : "CONNECTING...", lg.synced ? "UP TO DATE" : "SYNCING"));
    if (lg.owner) {
      xrtext::FrLine sim = line(lg.canSimRest ? "A: SIM THE REST (THEIR GAMES WORKED OUT)" : "SIM THE REST");
      sim.cursor = lg.canSimRest;
      sim.dim = !lg.canSimRest;
      wk.lines.push_back(sim);
      xrtext::FrLine note = line("YOU OWN THIS LEAGUE: YOU SET ITS DIFFICULTY");
      note.dim = true;
      wk.lines.push_back(note);
    }
    if (lg.error[0] != '\0') {
      xrtext::FrLine e = line(fr_upper(lg.error));
      e.dim = true;
      wk.lines.push_back(e);
    }
    cards.push_back(wk);
    hint += lg.owner && lg.canSimRest ? "   -   A: SIM THE REST   -   B: CAREERS" : "   -   B: CAREERS";
  } else if (s.page == 9) {
    // r97 (Lance: "we need a difficulty setting for franchise mode, bc that shit was becoming too easy and i
    // want a real life nemesis/A-Life system where im not always dominating or being the MVP")
    static const char* const kLevels[4] = {"ROOKIE", "PROFESSIONAL", "SUPERSTAR", "LEGEND"};
    static const char* const kWhat[4] = {
        "THE GAME'S EASIEST CPU. NEMESES GO EASY, THE LEAGUE LETS YOU SHINE",
        "A REAL GAME. NEMESES HUNT YOU, CPU STARS SCORE",
        "THE CPU PLAYS HARD. NEMESES HOLD GRUDGES, CPU CLUBS GROW FASTER, STARS CHASE THE GOLDEN BOOT",
        "THE GAME'S HARDEST CPU. NEMESES HUNT YOUR CAPTAIN ALL GAME, THE LEAGUE'S STARS RUN THE MVP RACE"};
    xrtext::FrCard c;
    c.title = "DIFFICULTY";
    c.weight = 2.0f;
    const int now = std::max(1, std::min(4, v.difficulty));
    for (int i = 0; i < 4; ++i) {
      xrtext::FrLine l = line(std::string(s.cursor == i ? "> " : "  ") + kLevels[i], i + 1 == now ? "ON" : "");
      l.gold = i + 1 == now;
      c.lines.push_back(l);
      xrtext::FrLine d = line(std::string("     ") + kWhat[i]);
      d.dim = true;
      c.lines.push_back(d);
    }
    cards.push_back(c);
    xrtext::FrCard how;
    how.title = "WHAT IT CHANGES";
    how.lines.push_back(line("YOUR MATCHES: THE GAME'S OWN CPU SKILL"));
    how.lines.push_back(line("NEMESES: HOW HARD THEY HUNT YOUR CAPTAIN"));
    how.lines.push_back(line("GRUDGES: HOW FAST THEY BUILD"));
    how.lines.push_back(line("CPU CLUBS: HOW FAST THEY LEVEL + EVOLVE"));
    how.lines.push_back(line("CPU STARS: THEIR SHARE OF THEIR CLUB'S GOALS"));
    how.lines.push_back(line("WORKED-OUT GAMES OF YOURS: A LITTLE HARDER"));
    if (lg.active && !lg.owner) {  // r100
      xrtext::FrLine own = line("ONLINE LEAGUE: ITS OWNER SETS IT");
      own.gold = true;
      how.lines.push_back(own);
    }
    cards.push_back(how);
    hint += "   -   UP / DOWN: CHOOSE   -   A: SET IT   -   B: CAREERS";
  } else if (s.page == 8) {
    // r77 (Lance: "an evo watch where we see the current levels of all pokemon that are in our
    // franchise, as to see who's leveling up at what rate"): every Pokemon in the league, highest first:
    // its club, its level (+ levels this season), and its next form at what level
    const int perCard = 11;
    for (size_t at = 0; at < s.evoWatch.size() && cards.size() < 3; at += perCard) {
      xrtext::FrCard c;
      c.title = at == 0 ? "EVO WATCH" : " ";
      for (size_t i = at; i < s.evoWatch.size() && i < at + perCard; ++i) {
        const PortFranchiseEvoRow& r = s.evoWatch[i];
        std::string left = std::string(r.name) + (r.shiny ? " (SHINY)" : "");
        if (r.ready)  // r95
          left += "  READY TO EVOLVE!";
        else
          left += r.evoLevel > 0 ? "  > " + std::string(r.evoName) + " AT " + std::to_string(r.evoLevel) : "  FINAL FORM";
        std::string right = "LV " + std::to_string(r.level);
        if (r.seasonGain > 0) right += " (+" + std::to_string(r.seasonGain) + ")";
        xrtext::FrLine l = line(left, right);
        logo_line(l, r.club);
        l.gold = r.mine;
        l.dim = !r.mine && r.evoLevel == 0;
        c.lines.push_back(l);
      }
      cards.push_back(c);
    }
    if (s.evoWatch.empty()) {
      xrtext::FrCard c;
      c.title = "EVO WATCH";
      c.lines.push_back(line("NO POKEMON IN THIS LEAGUE"));
      cards.push_back(c);
    }
    if (!s.evoReadyName.empty()) hint += "   -   A: EVOLVE " + fr_upper(s.evoReadyName);  // r95
    hint += "   -   GOLD: YOURS   -   (+N): LEVELS THIS SEASON   -   B: CAREERS";
  } else if (s.page == 7) {
    // r72: the nemesis wall: who has beef with your captain, their titles, what they've become
    xrtext::FrCard wall;
    wall.title = "NEMESES";
    wall.weight = 1.5f;
    for (int i = 0; i < v.nemCount; ++i) {
      const PortFranchiseView::Nem& n = v.nems[i];
      std::string head = std::string(n.name) + " " + n.title;
      if (n.teammate) head += "  (YOUR OWN)";
      xrtext::FrLine l = line(head, "HEAT " + std::to_string(n.heat));
      if (n.club >= 0) logo_line(l, n.club);
      l.gold = n.active;
      l.dim = !n.active;
      if (i == v.nextNemesis) l.cursor = true;
      wall.lines.push_back(l);
      std::string detail = "   " + (n.club >= 0 ? place_of(n.club) : std::string("RETIRED")) + "  -  BEAT YOU " +
                           std::to_string(n.beatYou) + ", LOST " + std::to_string(n.lostToYou);
      if (n.rank > 0) detail += "  -  RANK " + std::to_string(n.rank);
      xrtext::FrLine d = line(detail);
      d.dim = true;
      wall.lines.push_back(d);
    }
    if (v.nemCount == 0) {
      wall.lines.push_back(line("NOBODY HAS BEEF WITH YOU... YET"));
      xrtext::FrLine d = line("BEAT THEM, EMBARRASS THEM, KNOCK THEM OUT - THEY'LL REMEMBER");
      d.dim = true;
      wall.lines.push_back(d);
    }
    cards.push_back(wall);
    // the hottest one's story: its traits and what happened
    if (v.nemCount > 0) {
      const PortFranchiseView::Nem& n = v.nems[v.nextNemesis >= 0 ? v.nextNemesis : 0];
      xrtext::FrCard story;
      story.title = v.nextNemesis >= 0 ? "NEXT UP: YOUR NEMESIS" : "THE HOTTEST BEEF";
      xrtext::FrLine nm = line(n.name);
      const GuestPortrait& p = guest_portrait(n.key);
      if (!p.rgba.empty()) {
        nm.logo = p.rgba.data();
        nm.logoW = p.w;
        nm.logoH = p.h;
      }
      nm.gold = true;
      story.lines.push_back(nm);
      story.lines.push_back(line(n.title));
      for (int t = 0; t < n.traitCount; ++t) story.lines.push_back(line(n.traits[t]));
      if (n.last[0] != '\0') {
        xrtext::FrLine ls = line(n.last);
        ls.dim = true;
        story.lines.push_back(ls);
      }
      cards.push_back(story);
    }
    hint += "   -   B: CAREERS";
  } else if (s.page == 6) {
    // r71: awards night and the hall of fame
    xrtext::FrCard a;
    a.title = v.phase == PORT_FR_PHASE_DONE ? "AWARDS NIGHT" : "LAST AWARDS NIGHT";
    for (int i = 0; i < v.awardCount; ++i) {
      xrtext::FrLine ln = line(v.awards[i]);
      std::string up = mine.place;
      for (char& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
      ln.gold = !up.empty() && std::string(v.awards[i]).find(up) != std::string::npos;
      a.lines.push_back(ln);
    }
    if (v.awardCount == 0) a.lines.push_back(line("AFTER THE SUPER STRIKE SERIES"));
    cards.push_back(a);
    xrtext::FrCard hf;
    hf.title = "HALL OF FAME";
    hf.weight = 1.2f;
    for (int i = 0; i < v.historyCount; ++i) hf.lines.push_back(line(v.history[i]));
    if (v.historyCount == 0) hf.lines.push_back(line("NO SEASONS FINISHED YET"));
    cards.push_back(hf);
    hint += "   -   B: CAREERS";
  } else {
    xrtext::FrCard c;
    c.title = v.resultCount > 0 ? (v.week > PORT_FR_WEEKS + 1 ? "LAST GAME DAY" : "LAST WEEK'S RESULTS") : "NO RESULTS YET";
    xrtext::FrCard c2;
    c2.title = " ";
    std::string myPlace = mine.place;  // the results name the places in capitals
    for (char& ch : myPlace) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    for (int i = 0; i < v.resultCount; ++i) {
      xrtext::FrLine l = line(v.results[i]);
      l.gold = !myPlace.empty() && std::string(v.results[i]).find(myPlace) != std::string::npos;
      (i < 8 ? c : c2).lines.push_back(l);
    }
    cards.push_back(c);
    if (!c2.lines.empty()) cards.push_back(c2);
    if (v.newsCount > 0) {  // r70c: the league's news (evolutions round the league)
      xrtext::FrCard n;
      n.title = "AROUND THE LEAGUE";
      for (int i = 0; i < v.newsCount; ++i) {
        xrtext::FrLine l = line(v.news[i]);
        l.gold = std::string(v.news[i]).find("SHINY") != std::string::npos;
        n.lines.push_back(l);
      }
      cards.push_back(n);
    }
    hint += "   -   B: CAREERS";
  }
  if (!s.coopGuest) hint += "   -   Y: TRADES";  // r77
  xrtext::franchise_board(px, w, h, mine.name, subtitle, mine.c1 != 0 ? mine.c1 : 0x16306E,
                          mine.c2 != 0 ? mine.c2 : 0xFFD23F, bandLogo->rgba.empty() ? nullptr : bandLogo->rgba.data(),
                          bandLogo->w, bandLogo->h, cards, hint);
}
// r70b: a franchise match's intro card and the playoff series board (under the big screen).
constexpr uint32_t kFrIntroW = 1440, kFrIntroH = 640, kFrSeriesW = 1000, kFrSeriesH = 96;
// r77: the live XP meter (your captain this match), under the series board / over Tabletop's score bar.
constexpr uint32_t kFrLiveW = 1000, kFrLiveH = 170;
// One captain's meter: its name and level bar, its goals / assists and next form; side = the right-hand note.
void draw_live_row(uint8_t* px, int w, int h, const char* name, int level, int xp, int newLevel, int newXp, int gain,
                   int goals, int assists, bool won, bool evolves, int evoLevel, const char* evoName, bool pokemon,
                   const std::string& side, float glow, int glowFrom, float sideGlow) {
  auto plural = [](int n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "S"); };
  std::string line = plural(goals, "GOAL") + ", " + plural(assists, "ASSIST") + (won ? "  -  WINNING" : "") + "  -  ";
  // r87: "LEVEL UP!" while it glows, not for the rest of the match
  const bool levelUp = glow >= 0.0f;
  bool gold = false;
  if (evolves) {
    line += std::string("EVOLUTION EARNED: ") + evoName + " (AFTER THE MATCH)";
    gold = true;
  } else if (evoLevel > 0) {
    line += std::string(levelUp ? "LEVEL UP!  " : "") + "EVOLVES INTO " + evoName + " AT LV " + std::to_string(evoLevel);
  } else {
    line += levelUp ? "LEVEL UP!" : (pokemon ? "FINAL FORM" : "LEVELING UP");
  }
  xrtext::xp_meter(px, w, h, name, level, xp, newLevel, newXp, gain, line.c_str(), gold, side.c_str(), glow, glowFrom,
                   sideGlow);
}
// r79: with a co-captain the panel is two meters high: your captain on top, the co-captain under it.
void draw_franchise_live(uint8_t* px, int w, int h, const PortFranchiseLive& l, const FrLiveGlow& glow) {
  const int rowH = l.co ? h / 2 : h;
  // the sidekicks' level as it stands (the old one for the first moment of its light)
  const int skShown = glow.t[2] >= 0.0f && glow.t[2] < 0.3f && glow.from[2] >= 0 ? glow.from[2] : l.skNewLevel;
  const std::string side = std::string(l.kind) + " LV " + std::to_string(skShown) + "  +" + std::to_string(l.skGain);
  draw_live_row(px, w, rowH, l.name, l.level, l.xp, l.newLevel, l.newXp, l.gain, l.goals, l.assists, l.won, l.evolves,
                l.evoLevel, l.evoName, l.pokemon, side, glow.t[0], glow.from[0], glow.t[2]);
  if (l.co)
    draw_live_row(px + static_cast<size_t>(w) * rowH * 4, w, h - rowH, l.coName, l.coLevel, l.coXp, l.coNewLevel,
                  l.coNewXp, l.coGain, l.coGoals, l.coAssists, l.won, l.coEvolves, l.coEvoLevel, l.coEvoName,
                  l.coPokemon, "CO-CAPTAIN", glow.t[1], glow.from[1], -1.0f);
}
std::string fr_upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}
std::string franchise_series_text(const PortFranchiseCard& c) {
  const int a = c.seriesWins[0], b = c.seriesWins[1];
  std::string state;
  if (a == b) {
    state = "SERIES TIED " + std::to_string(a) + "-" + std::to_string(b);
  } else {
    const int lead = a > b ? c.club[0] : c.club[1];
    state = fr_upper(PortFranchiseClubPlace(lead)) + " LEAD " + std::to_string(std::max(a, b)) + "-" +
            std::to_string(std::min(a, b));
  }
  return fr_upper(c.roundName) + "   -   GAME " + std::to_string(c.gameNo) + "   -   " + state;
}
void draw_franchise_intro(uint8_t* px, int w, int h, const PortFranchiseCard& c) {
  std::vector<xrtext::FrCard> cards;
  const bool playoff = c.round > 0;
  for (int side = 0; side < 2; ++side) {
    const int club = c.club[side];
    xrtext::FrCard k;
    k.title = side == 0 ? "HOME" : "AWAY";
    k.weight = 1.3f;
    xrtext::FrLine name;
    name.text = PortFranchiseClubName(club);
    name.gold = true;
    const GuestPortrait& p = guest_portrait(std::string("club_") + PortFranchiseClubId(club));
    if (!p.rgba.empty()) {
      name.logo = p.rgba.data();
      name.logoW = p.w;
      name.logoH = p.h;
    }
    k.lines.push_back(name);
    xrtext::FrLine rec;
    rec.text = playoff ? "#" + std::to_string(c.seed[side]) + " SEED, " + PortFranchiseConferenceName(club) + " CONFERENCE"
                       : std::string(PortFranchiseDivisionName(club)) + " DIVISION";
    rec.right = std::to_string(c.wins[side]) + "-" + std::to_string(c.losses[side]);
    k.lines.push_back(rec);
    if (playoff) {
      xrtext::FrLine sw;
      sw.text = "SERIES WINS";
      sw.right = std::to_string(c.seriesWins[side]);
      sw.gold = c.seriesWins[side] == 2;
      k.lines.push_back(sw);
    }
    if (c.nemesis && c.nemesisSide == side) {
      // r72: the face-off: your nemesis, its title, its record against you
      xrtext::FrLine nl;
      nl.text = "NEMESIS: " + std::string(c.nemesisName);
      const GuestPortrait& np = guest_portrait(c.nemesisKey);
      if (!np.rgba.empty()) {
        nl.logo = np.rgba.data();
        nl.logoW = np.w;
        nl.logoH = np.h;
      }
      nl.gold = true;
      k.lines.push_back(nl);
      xrtext::FrLine tl;
      tl.text = c.nemesisTitle;
      tl.right = "BEAT YOU " + std::to_string(c.nemesisBeatYou) + "X";
      k.lines.push_back(tl);
    }
    cards.push_back(k);
    if (side == 0) {
      xrtext::FrCard vs;
      vs.title = "VS";
      vs.weight = 0.55f;
      xrtext::FrLine l1;
      l1.text = playoff ? "GAME " + std::to_string(c.gameNo) : "WEEK " + std::to_string(c.week);
      vs.lines.push_back(l1);
      if (playoff) {
        xrtext::FrLine l2;
        l2.text = "BEST OF 5";
        l2.dim = true;
        vs.lines.push_back(l2);
        if (c.seriesWins[0] == 2 || c.seriesWins[1] == 2) {
          xrtext::FrLine l3;
          l3.text = c.seriesWins[0] == 2 && c.seriesWins[1] == 2 ? "WINNER TAKES ALL" : "CLOSE-OUT GAME";
          l3.gold = true;
          vs.lines.push_back(l3);
        }
      }
      if (c.spectating) {
        xrtext::FrLine l4;
        l4.text = "WATCHING";
        l4.dim = true;
        vs.lines.push_back(l4);
      }
      cards.push_back(vs);
    }
  }
  const GuestPortrait& logo = guest_portrait(c.roundLogo[0] != '\0' ? std::string(c.roundLogo) : "franchise_league");
  const std::string title = playoff ? fr_upper(c.roundName) : "STRIKERS SUPER LEAGUE";
  const std::string sub = playoff ? franchise_series_text(c).substr(franchise_series_text(c).rfind("   -   ") + 7)
                                  : "WEEK " + std::to_string(c.week) + " OF 20";
  xrtext::franchise_board(px, w, h, title, sub, playoff ? 0x1A1A4A : 0x16306E, 0xFFD23F,
                          logo.rgba.empty() ? nullptr : logo.rgba.data(), logo.w, logo.h, cards, "");
}
// r73: the TV in your room (franchise): "SSL TONIGHT", in turn the last scores, the top of both
// conferences, the Golden Boot race and who's on fire.
void draw_room_tv(uint8_t* px, int w, int h, const FrRoom& r, int page) {
  const PortFranchiseView& v = r.view;
  auto logo_line = [&](xrtext::FrLine& l, int club) {
    if (club < 0 || club >= static_cast<int>(r.clubIds.size())) return;
    const GuestPortrait& p = guest_portrait("club_" + r.clubIds[club]);
    if (!p.rgba.empty()) {
      l.logo = p.rgba.data();
      l.logoW = p.w;
      l.logoH = p.h;
    }
  };
  auto place = [&](int club) {
    return club >= 0 && club < static_cast<int>(r.clubPlaces.size()) ? r.clubPlaces[club] : std::string("?");
  };
  std::vector<xrtext::FrCard> cards;
  std::string sub;
  if (page == 0) {
    sub = "THE SCORES";
    xrtext::FrCard c;
    for (int i = 0; i < v.resultCount && i < 8; ++i) {
      xrtext::FrLine l;
      l.text = v.results[i];
      c.lines.push_back(l);
    }
    if (c.lines.empty()) {
      xrtext::FrLine l;
      l.text = "NO GAMES PLAYED YET";
      c.lines.push_back(l);
    }
    cards.push_back(c);
  } else if (page == 1) {
    sub = "TOP OF THE TABLE";
    for (int conf = 0; conf < 2; ++conf) {
      xrtext::FrCard c;
      c.title = conf == 0 ? "CROWN" : "STAR";
      for (int i = 0; i < 4; ++i) {
        const PortFranchiseRow& row = v.conference[conf][i];
        xrtext::FrLine l;
        l.text = std::to_string(i + 1) + ". " + place(row.club);
        l.right = std::to_string(row.wins) + "-" + std::to_string(row.losses);
        l.gold = row.mine;
        logo_line(l, row.club);
        c.lines.push_back(l);
      }
      cards.push_back(c);
    }
  } else {
    sub = "WHO'S HOT";
    xrtext::FrCard c;
    c.title = "GOLDEN BOOT";
    for (int i = 0; i < v.leaderCount && i < 4; ++i) {
      xrtext::FrLine l;
      l.text = v.leaders[i].name;
      l.right = std::to_string(v.leaders[i].goals);
      logo_line(l, v.leaders[i].club);
      c.lines.push_back(l);
    }
    if (c.lines.empty()) {
      xrtext::FrLine l;
      l.text = "NO GOALS YET";
      c.lines.push_back(l);
    }
    cards.push_back(c);
    xrtext::FrCard f;
    f.title = "ON FIRE";
    for (int i = 0; i < v.hotCount && i < 4; ++i) {
      xrtext::FrLine l;
      l.text = v.hot[i].name;
      logo_line(l, v.hot[i].club);
      f.lines.push_back(l);
    }
    if (f.lines.empty()) {
      xrtext::FrLine l;
      l.text = "NOBODY YET";
      f.lines.push_back(l);
    }
    cards.push_back(f);
  }
  const GuestPortrait& league = guest_portrait("franchise_league");
  xrtext::franchise_board(px, w, h, "SSL TONIGHT", sub, 0x16306E, 0xFFD23F,
                          league.rgba.empty() ? nullptr : league.rgba.data(), league.w, league.h, cards, "");
}

// r70c: the evolution scene's picture `t` seconds into its stage (xrtext::evolution_scene).
// r96: `threeD`: the Pokemon is standing in your room in 3D in front of it (update_evo): while it evolves
// and after, the card is its backdrop: the light and the words, no pictures.
void draw_franchise_evolution(uint8_t* px, int w, int h, const FrSnapshot& s, float t, bool threeD) {
  const PortFranchiseEvolution& e = s.evo;
  const bool pictures = !threeD || s.evoStage == 0 || s.evoStage == 3;
  auto img = [pictures](const char* key) {
    const GuestPortrait& p = guest_portrait(key);
    xrtext::EvoImage im;
    if (pictures && !p.rgba.empty()) {
      im.rgba = p.rgba.data();
      im.w = p.w;
      im.h = p.h;
    }
    return im;
  };
  const int choice = e.options == 2 ? std::max(0, std::min(1, s.evoChoice)) : 0;
  // shiny ones in their shiny colours (portraits/<pack>_shiny.png, when there)
  auto img_shiny = [&](const char* key, bool shiny) {
    if (shiny) {
      const xrtext::EvoImage im = img((std::string(key) + "_shiny").c_str());
      if (im.rgba != nullptr) return im;
    }
    return img(key);
  };
  const xrtext::EvoImage from = img_shiny(e.fromKey, e.shiny);
  const xrtext::EvoImage to = s.evoStage == 2 ? img_shiny(e.toKey[choice], e.shiny || s.evoNewShiny)
                                              : img(s.evoStage == 0 ? e.toKey[0] : e.toKey[choice]);
  const xrtext::EvoImage alt = img(e.options == 2 ? e.toKey[1] : e.toKey[0]);
  const std::string fromName = fr_upper(e.fromName), toName = fr_upper(e.toName[choice]);
  std::string title, line, hint;
  switch (s.evoStage) {
  case 0:  // r95: the EVO page
    title = "EVO! " + fromName + " IS READY TO EVOLVE!";
    if (e.options == 2) {
      line = "LV " + std::to_string(e.level) + ": INTO " + fr_upper(e.toName[0]) + " OR " + fr_upper(e.toName[1]) + "?";
      hint = "LEFT / RIGHT: CHOOSE   -   A: EVOLVE INTO " + toName + "   -   B: NOT NOW";
    } else {
      line = "LV " + std::to_string(e.level) + ": " + fromName + " > ???";
      hint = "A: EVOLVE (WATCH IT HAPPEN)   -   B: NOT NOW (EVOLVE IT LATER: EVO WATCH PAGE, A)";
    }
    break;
  case 1:
    title = "WHAT? " + fromName + " IS EVOLVING!";
    break;
  case 2:
    title = "CONGRATULATIONS!";
    line = "YOUR " + fromName + " EVOLVED INTO " + toName + "!" + (s.evoNewShiny ? "  IT'S SHINY!" : "");
    hint = "A: CONTINUE";
    break;
  default:  // r95: postponed
    title = "OKAY! " + fromName + " WILL WAIT";
    line = "EVOLVE IT ANY TIME: EVO WATCH PAGE, A";
    hint = "A: CONTINUE";
    break;
  }
  // r96: its clock runs 0.8 x through the evolving, so its flash is the 3D one's (8.5 s; it evolves at 8.8)
  const float tp = s.evoStage == 1 ? 0.8f * t : t;
  xrtext::evolution_scene(px, w, h, from, to, alt, s.evoStage, tp, choice, title.c_str(), line.c_str(), hint.c_str(),
                          s.evoNewShiny || e.shiny, e.options);
}
// r68e: one entry of the character select menu: what picking it sends (a guest's roster index, or
// PORT_GUEST_ORIGINAL(_SIDEKICK) + the Strikers character's id), its portrait (guests/portraits/
// <key>.png: the guests' own art; strikers_<name>: the game's own loading-screen pictures), its name,
// its game, and its place (sections: Strikers + Charged, Melee, Pokemon; each starts a new row).
struct MenuEntry {
  int pick = -1;
  int team = -1;  // a Strikers captain's eTeamID (taken when the other side has it), else -1
  std::string portrait, name, tag;
  int row = 0, col = 0;
  uint32_t frame = 0;
};
const std::vector<MenuEntry>& menu_entries(int role) {
  static std::vector<MenuEntry> built[2];
  static bool done[2] = {false, false};
  static std::mutex mutex;  // the game thread and the panel's drawing thread both ask
  if (role < 0 || role > 1) role = 0;
  std::lock_guard<std::mutex> lock(mutex);
  if (done[role]) return built[role];
  done[role] = true;
  std::vector<MenuEntry>& out = built[role];
  const int cols = role == 0 ? kMenuCaptainCols : kMenuSidekickCols;
  int row = 0, col = 0;
  auto add = [&](MenuEntry e) {
    if (col >= cols) {
      col = 0;
      ++row;
    }
    e.row = row;
    e.col = col++;
    out.push_back(e);
  };
  auto new_section = [&]() {
    if (col > 0) {
      col = 0;
      ++row;
    }
  };
  if (role == 0) {
    // The Strikers captains in their own grid's order (eTeamID: daisy 0, dk 1, luigi 2, mario 3, peach 4,
    // waluigi 5, wario 6, yoshi 7).
    static const struct { int team; const char* file; const char* name; } kCaptains[8] = {
        {3, "mario", "Mario"}, {2, "luigi", "Luigi"}, {4, "peach", "Peach"}, {0, "daisy", "Daisy"},
        {7, "yoshi", "Yoshi"}, {1, "donkeykong", "Donkey Kong"}, {6, "wario", "Wario"}, {5, "waluigi", "Waluigi"}};
    for (const auto& c : kCaptains) {
      MenuEntry e;
      e.pick = PORT_GUEST_ORIGINAL + c.team;
      e.team = c.team;
      e.portrait = std::string("strikers_") + c.file;
      e.name = c.name;
      e.tag = "Strikers";
      e.frame = 0x7A2430;
      add(e);
    }
  } else {
    // eSidekickID: toad 0, koopa 1, hammer bro 2, birdo 3
    static const struct { int id; const char* file; const char* name; } kSidekicks[4] = {
        {0, "toad", "Toad"}, {1, "koopa", "Koopa"}, {2, "hammerbro", "Hammer Bro"}, {3, "birdo", "Birdo"}};
    for (const auto& s : kSidekicks) {
      MenuEntry e;
      e.pick = PORT_GUEST_ORIGINAL_SIDEKICK + s.id;
      e.portrait = std::string("strikers_") + s.file;
      e.name = s.name;
      e.tag = "Strikers";
      e.frame = 0x7A2430;
      add(e);
    }
    new_section();
  }
  // The guests by game: Charged (beside the Strikers captains), Melee, Melee's Pokemon.
  static const struct { const char* game; const char* tag; uint32_t frame; } kGames[3] = {
      {"Charged", "Charged", 0x7A4A1E}, {"Melee", "Melee", 0x23407A}, {"Melee Pokemon", "Pokemon", 0x6E2440}};
  const int n = PortGuestRosterCount(role);
  // r70c: each evolved Pokemon right after the form it evolves from (Charmander, Charmeleon, Charizard)
  std::vector<int> order;
  std::function<void(const char*)> evolutions_of = [&](const char* from) {
    for (int k = 0; k < n; ++k) {
      const int idx = PortGuestRosterAt(role, k);
      if (PortGuestRosterEvolved(idx) && std::strcmp(PortGuestRosterEvolvesFrom(idx), from) == 0) {
        order.push_back(idx);
        evolutions_of(PortGuestRosterPack(idx));
      }
    }
  };
  for (int i = 0; i < n; ++i) {
    const int index = PortGuestRosterAt(role, i);
    if (PortGuestRosterEvolved(index) && PortGuestRosterEvolvesFrom(index)[0] != '\0') continue;
    order.push_back(index);
    evolutions_of(PortGuestRosterPack(index));
  }
  for (int i = 0; i < n; ++i) {  // (any evolved one whose earlier form isn't on this headset: at the end)
    const int index = PortGuestRosterAt(role, i);
    if (std::find(order.begin(), order.end(), index) == order.end()) order.push_back(index);
  }
  for (int gsec = 0; gsec < 3; ++gsec) {
    if (gsec > 0) new_section();
    for (int index : order) {
      const char* game = PortGuestRosterGame(index);
      // r69: Lance's Colosseum Pokemon take the Pokemon row (Melee's are off the roster)
      const bool colo = gsec == 2 && game != nullptr && std::strcmp(game, "Colosseum") == 0;
      if (game == nullptr || (!colo && std::strcmp(game, kGames[gsec].game) != 0)) continue;
      MenuEntry e;
      e.pick = index;
      const char* pack = PortGuestRosterPack(index);
      e.portrait = pack != nullptr ? pack : "";
      const char* name = PortGuestRosterName(index);
      e.name = name != nullptr ? name : e.portrait;
      e.tag = kGames[gsec].tag;
      e.frame = kGames[gsec].frame;
      add(e);
    }
  }
  return out;
}
// The entry the stick moves to from `from`: left / right along its row, up / down to the row above or
// below (skipping none: every row has someone), the nearest column there.
int menu_step(const std::vector<MenuEntry>& m, int from, int dir) {
  if (m.empty()) return 0;
  from = std::max(0, std::min(static_cast<int>(m.size()) - 1, from));
  const MenuEntry& at = m[from];
  if (dir == 1) return from > 0 && m[from - 1].row == at.row ? from - 1 : from;
  if (dir == 2) return from + 1 < static_cast<int>(m.size()) && m[from + 1].row == at.row ? from + 1 : from;
  const int want = at.row + (dir == 3 ? -1 : 1);
  int best = from, bestD = 1 << 30;
  for (int i = 0; i < static_cast<int>(m.size()); ++i) {
    if (m[i].row != want) continue;
    const int d = std::abs(m[i].col - at.col);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}
// r75: a new career's own players on the character select menu (the franchise hub's mode 4): your
// captain, your backup captain, your sidekicks. Evolved forms can't start a career (their silhouette, as
// on the menu); for the backup, the captain you just picked is darkened.
void draw_franchise_pick(uint8_t* px, int w, int h, const FrSnapshot& s) {
  const int stage = std::max(0, std::min(2, s.pickStage));
  const int role = stage == 2 ? 1 : 0;
  const std::vector<MenuEntry>& menu = menu_entries(role);
  std::vector<xrtext::GuestTile> tiles;
  tiles.reserve(menu.size());
  for (const MenuEntry& e : menu) {
    const GuestPortrait& p = guest_portrait(e.portrait);
    xrtext::GuestTile t{p.rgba.empty() ? nullptr : p.rgba.data(), p.w, p.h, e.name.c_str(), e.tag.c_str()};
    t.row = e.row;
    t.col = e.col;
    t.frame = e.frame;
    t.locked = role == 0 && e.team < 0 && PortGuestRosterEvolved(e.pick);
    t.taken = stage == 1 && e.pick == s.pickCap;
    tiles.push_back(t);
  }
  std::string club = s.pickClub >= 0 && s.pickClub < static_cast<int>(s.clubs.size()) ? s.clubs[s.pickClub].name : "";
  for (char& ch : club) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  static const char* const kWhat[3] = {"CHOOSE YOUR CAPTAIN", "CHOOSE YOUR BACKUP CAPTAIN", "CHOOSE YOUR SIDEKICKS"};
  static const char* const kHint[3] = {
      "STICK: MOVE   -   A: YOUR CAPTAIN   -   B: BACK TO THE CLUBS",
      "STICK: MOVE   -   A: YOUR BACKUP CAPTAIN   -   X: NO BACKUP   -   B: BACK",
      "STICK: MOVE   -   A: YOUR SIDEKICKS (THE CAREER STARTS)   -   B: BACK"};
  char title[192];
  std::snprintf(title, sizeof(title), "NEW CAREER   -   %s   -   %s", club.c_str(), kWhat[stage]);
  xrtext::guest_picker(px, w, h, title, tiles.data(), static_cast<int>(tiles.size()),
                       role == 0 ? kMenuCaptainCols : kMenuSidekickCols, s.cursor, kHint[stage]);
}
constexpr int kViewHeightMinCm = -60, kViewHeightMaxCm = 100;  // r44g

// xr_quest.cpp is built without include/ on its path: the mixer's crowd controls (port/audio.h).
extern "C" void PortAudioSetCrowdUser(float gain, int spatial, float headYaw);

// The latest game frame, written by the render worker inside aurora_end_frame's work, read on the
// game thread once aurora::gfx::synchronize() has returned.
std::mutex g_frameMutex;
std::vector<uint8_t> g_frame;  // tightly packed RGBA8
uint32_t g_frameWidth = 0, g_frameHeight = 0;
uint64_t g_frameSerial = 0;
uint64_t g_frameUploaded = 0;

uint64_t now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

void on_aurora_frame(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t bytesPerRow, bool bgra,
                     void*) {
  std::lock_guard<std::mutex> lock(g_frameMutex);
  const size_t tight = static_cast<size_t>(width) * 4;
  g_frame.resize(tight * height);
  uint8_t* dst = g_frame.data();
  for (uint32_t row = 0; row < height; ++row) {
    const uint8_t* s = pixels + static_cast<size_t>(row) * bytesPerRow;
    uint8_t* d = dst + static_cast<size_t>(row) * tight;
    if (!bgra) {
      std::memcpy(d, s, tight);
    } else {
      for (uint32_t col = 0; col < width; ++col) {
        d[col * 4 + 0] = s[col * 4 + 2];
        d[col * 4 + 1] = s[col * 4 + 1];
        d[col * 4 + 2] = s[col * 4 + 0];
        d[col * 4 + 3] = s[col * 4 + 3];
      }
    }
  }
  g_frameWidth = width;
  g_frameHeight = height;
  ++g_frameSerial;
}

// ---------------------------------------------------------------------------------------------
// Instance, system, Vulkan device, session
// ---------------------------------------------------------------------------------------------

bool init_loader() {
  PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
  if (!get_proc(XR_NULL_HANDLE, "xrInitializeLoaderKHR", &initializeLoader)) {
    XERR("xrInitializeLoaderKHR not found");
    return false;
  }
  XrLoaderInitInfoAndroidKHR info{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
  info.applicationVM = g.vm;
  info.applicationContext = g.activity;
  return xr_ok(initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&info)),
               "xrInitializeLoaderKHR");
}

bool create_instance() {
  uint32_t count = 0;
  xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
  std::vector<XrExtensionProperties> props(count, XrExtensionProperties{XR_TYPE_EXTENSION_PROPERTIES});
  if (count > 0) {
    xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data());
  }
  auto has = [&](const char* name) {
    for (const auto& p : props) {
      if (std::strcmp(p.extensionName, name) == 0) {
        return true;
      }
    }
    return false;
  };

  std::vector<const char*> exts;
  if (!has(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) || !has(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME)) {
    XERR("runtime lacks XR_KHR_vulkan_enable2 or XR_KHR_android_create_instance");
    return false;
  }
  exts.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
  exts.push_back(XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME);
  g.hasPassthroughExt = has(XR_FB_PASSTHROUGH_EXTENSION_NAME);
  if (g.hasPassthroughExt) {
    exts.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
  }
  g.hasRefreshRateExt = has(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
  if (g.hasRefreshRateExt) {
    exts.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
  }
  g.hasPerfSettingsExt = has(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
  if (g.hasPerfSettingsExt) {
    exts.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
  }
  g.hasThreadSettingsExt = has(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME);
  if (g.hasThreadSettingsExt) {
    exts.push_back(XR_KHR_ANDROID_THREAD_SETTINGS_EXTENSION_NAME);
  }
  // r54: the headset's own controller models, for the controllers in your hands (xr_ctrl_model.h).
  g.hasRenderModelExt = has(xrctrl::extension_name());
  if (g.hasRenderModelExt) {
    exts.push_back(xrctrl::extension_name());
  }
  XLOG("extensions: passthrough=%d refresh_rate=%d perf_settings=%d thread_settings=%d render_model=%d",
       g.hasPassthroughExt ? 1 : 0, g.hasRefreshRateExt ? 1 : 0, g.hasPerfSettingsExt ? 1 : 0,
       g.hasThreadSettingsExt ? 1 : 0, g.hasRenderModelExt ? 1 : 0);

  XrInstanceCreateInfoAndroidKHR android{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
  android.applicationVM = g.vm;
  android.applicationActivity = g.activity;

  XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
  info.next = &android;
  std::strncpy(info.applicationInfo.applicationName, "Strikers MR", XR_MAX_APPLICATION_NAME_SIZE - 1);
  info.applicationInfo.applicationVersion = 2;
  std::strncpy(info.applicationInfo.engineName, "Aurora", XR_MAX_ENGINE_NAME_SIZE - 1);
  info.applicationInfo.engineVersion = 1;
  info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
  info.enabledExtensionCount = static_cast<uint32_t>(exts.size());
  info.enabledExtensionNames = exts.data();
  if (!xr_ok(xrCreateInstance(&info, &g.instance), "xrCreateInstance")) {
    return false;
  }

  XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
  systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
  if (!xr_ok(xrGetSystem(g.instance, &systemInfo, &g.system), "xrGetSystem")) {
    return false;
  }
  // r44f: whether this headset shows your room in colour (Quest 3, 3S, Pro) or not (Quest 2: grey,
  // and its recordings show black there). Lance: the Super Strike screen is for the latter only; with
  // colour passthrough it was taken away on purpose, so the captain is seen in the air on the table.
  XrSystemPassthroughProperties2FB pt2{XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES2_FB};
  XrSystemRenderModelPropertiesFB rm{XR_TYPE_SYSTEM_RENDER_MODEL_PROPERTIES_FB};  // r54
  XrSystemProperties sysProps{XR_TYPE_SYSTEM_PROPERTIES};
  if (g.hasRenderModelExt) sysProps.next = &rm;
  if (g.hasPassthroughExt) {
    pt2.next = sysProps.next;
    sysProps.next = &pt2;
  }
  if (xr_ok(xrGetSystemProperties(g.instance, g.system, &sysProps), "xrGetSystemProperties")) {
    g.colorPassthrough = g.hasPassthroughExt && (pt2.capabilities & XR_PASSTHROUGH_CAPABILITY_COLOR_BIT_FB) != 0;
    g.renderModelsSupported = g.hasRenderModelExt && rm.supportsRenderModelLoading == XR_TRUE;
    // r73: the compositor's most layers a frame (the franchise's panels add up): xrEndFrame keeps to it
    g.maxLayers = sysProps.graphicsProperties.maxLayerCount > 0 ? sysProps.graphicsProperties.maxLayerCount : 16;
    XLOG("headset: %s, %s passthrough, render models %s, up to %u layers", sysProps.systemName,
         g.colorPassthrough ? "colour" : "no colour", g.renderModelsSupported ? "yes" : "no", g.maxLayers);
  }
  return true;
}

bool create_vulkan_device() {
  PFN_xrGetVulkanGraphicsRequirements2KHR getRequirements = nullptr;
  PFN_xrCreateVulkanInstanceKHR createInstance = nullptr;
  PFN_xrGetVulkanGraphicsDevice2KHR getDevice = nullptr;
  PFN_xrCreateVulkanDeviceKHR createDevice = nullptr;
  if (!get_proc(g.instance, "xrGetVulkanGraphicsRequirements2KHR", &getRequirements) ||
      !get_proc(g.instance, "xrCreateVulkanInstanceKHR", &createInstance) ||
      !get_proc(g.instance, "xrGetVulkanGraphicsDevice2KHR", &getDevice) ||
      !get_proc(g.instance, "xrCreateVulkanDeviceKHR", &createDevice)) {
    XERR("XR_KHR_vulkan_enable2 entry points missing");
    return false;
  }

  XrGraphicsRequirementsVulkan2KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR};
  if (!xr_ok(getRequirements(g.instance, g.system, &requirements), "xrGetVulkanGraphicsRequirements2KHR")) {
    return false;
  }
  // XrVersion is not a Vulkan version: convert, and ask for at least 1.1.
  const XrVersion xrMin = requirements.minApiVersionSupported;
  const XrVersion xrMax = requirements.maxApiVersionSupported;
  const uint32_t vkMin = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(xrMin), XR_VERSION_MINOR(xrMin), 0);
  const uint32_t vkMax = VK_MAKE_API_VERSION(0, XR_VERSION_MAJOR(xrMax), XR_VERSION_MINOR(xrMax), 0);
  uint32_t apiVersion = vkMin < VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : vkMin;
  if (xrMax != 0 && apiVersion > vkMax) {
    apiVersion = vkMax;
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "Strikers MR";
  app.applicationVersion = 2;
  app.pEngineName = "Aurora";
  app.apiVersion = apiVersion;
  VkInstanceCreateInfo vkInstanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  vkInstanceInfo.pApplicationInfo = &app;

  XrVulkanInstanceCreateInfoKHR xrVkInstance{XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
  xrVkInstance.systemId = g.system;
  xrVkInstance.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
  xrVkInstance.vulkanCreateInfo = &vkInstanceInfo;
  VkResult vkResult = VK_SUCCESS;
  if (!xr_ok(createInstance(g.instance, &xrVkInstance, &g.vkInstance, &vkResult), "xrCreateVulkanInstanceKHR") ||
      !vk_ok(vkResult, "vkCreateInstance (via OpenXR)")) {
    return false;
  }

  XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo{XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
  deviceGetInfo.systemId = g.system;
  deviceGetInfo.vulkanInstance = g.vkInstance;
  if (!xr_ok(getDevice(g.instance, &deviceGetInfo, &g.physical), "xrGetVulkanGraphicsDevice2KHR")) {
    return false;
  }

  uint32_t familyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(g.physical, &familyCount, nullptr);
  std::vector<VkQueueFamilyProperties> families(familyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(g.physical, &familyCount, families.data());
  bool found = false;
  for (uint32_t i = 0; i < familyCount; ++i) {
    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      g.queueFamily = i;
      found = true;
      break;
    }
  }
  if (!found) {
    XERR("no graphics queue family");
    return false;
  }

  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queueInfo.queueFamilyIndex = g.queueFamily;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &priority;

  // The 3D modes share images with Dawn's device: exportable opaque-fd memory one way, a sync-fd
  // semaphore back. Each enabled only if the device lists it (several are core in 1.1 anyway).
  uint32_t extCount = 0;
  vkEnumerateDeviceExtensionProperties(g.physical, nullptr, &extCount, nullptr);
  std::vector<VkExtensionProperties> extProps(extCount);
  if (extCount > 0) {
    vkEnumerateDeviceExtensionProperties(g.physical, nullptr, &extCount, extProps.data());
  }
  auto hasExt = [&](const char* name) {
    for (const auto& e : extProps) {
      if (std::strcmp(e.extensionName, name) == 0) {
        return true;
      }
    }
    return false;
  };
  std::vector<const char*> deviceExts;
  for (const char* name : {VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                           VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
                           VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
                           VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME}) {
    if (hasExt(name)) {
      deviceExts.push_back(name);
    }
  }
  g.hasExtMemFd = hasExt(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
  g.hasExtSemFd = hasExt(VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME);
  XLOG("XR device: external memory fd=%d, semaphore fd=%d", g.hasExtMemFd ? 1 : 0, g.hasExtSemFd ? 1 : 0);

  // r61: anisotropic filtering where the GPU has it (the cup suite's floor seen at a slant).
  VkPhysicalDeviceFeatures supported{};
  vkGetPhysicalDeviceFeatures(g.physical, &supported);
  VkPhysicalDeviceFeatures features{};
  features.samplerAnisotropy = supported.samplerAnisotropy;
  {
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(g.physical, &props);
    if (supported.samplerAnisotropy) g.maxAnisotropy = props.limits.maxSamplerAnisotropy;
    // r62: 4x multisampling for the cup suite's room where colour and depth both have it.
    const VkSampleCountFlags both = props.limits.framebufferColorSampleCounts & props.limits.framebufferDepthSampleCounts;
    g.roomSamples = (both & VK_SAMPLE_COUNT_4_BIT) ? 4u : 1u;
  }

  VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &queueInfo;
  deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExts.size());
  deviceInfo.ppEnabledExtensionNames = deviceExts.data();
  deviceInfo.pEnabledFeatures = &features;

  XrVulkanDeviceCreateInfoKHR xrVkDevice{XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
  xrVkDevice.systemId = g.system;
  xrVkDevice.pfnGetInstanceProcAddr = &vkGetInstanceProcAddr;
  xrVkDevice.vulkanPhysicalDevice = g.physical;
  xrVkDevice.vulkanCreateInfo = &deviceInfo;
  if (!xr_ok(createDevice(g.instance, &xrVkDevice, &g.device, &vkResult), "xrCreateVulkanDeviceKHR") ||
      !vk_ok(vkResult, "vkCreateDevice (via OpenXR)")) {
    return false;
  }
  vkGetDeviceQueue(g.device, g.queueFamily, 0, &g.queue);
  vkGetPhysicalDeviceMemoryProperties(g.physical, &g.memProps);

  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = g.queueFamily;
  if (!vk_ok(vkCreateCommandPool(g.device, &poolInfo, nullptr, &g.pool), "vkCreateCommandPool")) {
    return false;
  }
  VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocInfo.commandPool = g.pool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  if (!vk_ok(vkAllocateCommandBuffers(g.device, &allocInfo, &g.cmd), "vkAllocateCommandBuffers")) {
    return false;
  }
  VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  return vk_ok(vkCreateFence(g.device, &fenceInfo, nullptr, &g.fence), "vkCreateFence");
}

bool create_session() {
  XrGraphicsBindingVulkan2KHR binding{XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR};
  binding.instance = g.vkInstance;
  binding.physicalDevice = g.physical;
  binding.device = g.device;
  binding.queueFamilyIndex = g.queueFamily;
  binding.queueIndex = 0;
  XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
  info.next = &binding;
  info.systemId = g.system;
  if (!xr_ok(xrCreateSession(g.instance, &info, &g.session), "xrCreateSession")) {
    return false;
  }
  XrReferenceSpaceCreateInfo space{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
  space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
  space.poseInReferenceSpace.orientation.w = 1.0f;
  if (!xr_ok(xrCreateReferenceSpace(g.session, &space, &g.local), "xrCreateReferenceSpace(LOCAL)")) {
    return false;
  }
  // The floor, for the life-size stadium: STAGE's origin is on it. Optional.
  space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
  if (XR_FAILED(xrCreateReferenceSpace(g.session, &space, &g.stage))) {
    g.stage = XR_NULL_HANDLE;
    XWARN("no STAGE space; assuming the floor is 1.6 m below the start pose");
  }
  // r42: your head, in every mode (the eyes are only located in the 3D modes).
  space.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
  if (XR_FAILED(xrCreateReferenceSpace(g.session, &space, &g.view))) {
    g.view = XR_NULL_HANDLE;
    XWARN("no VIEW space; the screen is brought to the start pose");
  }

  // The eye images: the runtime's recommended size, scaled (STRIKERS_XR_EYE_SCALE) and kept to a
  // multiple of 8. Native size since r32 (Lance: the Tabletop looked grainy at 0.75; the eye passes
  // cost draws, not pixels).
  uint32_t viewCount = 0;
  xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount,
                                    nullptr);
  std::vector<XrViewConfigurationView> configViews(viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
  if (viewCount >= 2 &&
      XR_SUCCEEDED(xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                     viewCount, &viewCount, configViews.data()))) {
    const char* e = std::getenv("STRIKERS_XR_EYE_SCALE");
    float scale = (e != nullptr && *e != '\0') ? static_cast<float>(std::atof(e)) : 1.0f;
    scale = std::max(0.3f, std::min(1.5f, scale));
    g.eyeWidth = (static_cast<uint32_t>(configViews[0].recommendedImageRectWidth * scale) / 8) * 8;
    g.eyeHeight = (static_cast<uint32_t>(configViews[0].recommendedImageRectHeight * scale) / 8) * 8;
    XLOG("eye images %ux%u (recommended %ux%u x %.2f)", g.eyeWidth, g.eyeHeight,
         configViews[0].recommendedImageRectWidth, configViews[0].recommendedImageRectHeight, scale);
  }
  {
    const char* sps = std::getenv("STRIKERS_XR_SPS");
    g.singlePass = !(sps != nullptr && sps[0] == '0');
    XLOG("eye sends: %s", g.singlePass ? "single pass (both eyes per send)" : "one per eye");
  }
  return true;
}

void create_passthrough() {
  if (!g.hasPassthroughExt) {
    return;
  }
  Passthrough& pt = g.pt;
  if (!get_proc(g.instance, "xrCreatePassthroughFB", &pt.create) ||
      !get_proc(g.instance, "xrPassthroughStartFB", &pt.start) ||
      !get_proc(g.instance, "xrPassthroughPauseFB", &pt.pause) ||
      !get_proc(g.instance, "xrCreatePassthroughLayerFB", &pt.createLayer) ||
      !get_proc(g.instance, "xrPassthroughLayerPauseFB", &pt.pauseLayer) ||
      !get_proc(g.instance, "xrPassthroughLayerResumeFB", &pt.resumeLayer)) {
    XWARN("passthrough entry points missing");
    return;
  }
  XrPassthroughCreateInfoFB info{XR_TYPE_PASSTHROUGH_CREATE_INFO_FB};
  info.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
  if (!xr_ok(pt.create(g.session, &info, &pt.handle), "xrCreatePassthroughFB")) {
    return;
  }
  XrPassthroughLayerCreateInfoFB layerInfo{XR_TYPE_PASSTHROUGH_LAYER_CREATE_INFO_FB};
  layerInfo.passthrough = pt.handle;
  layerInfo.flags = XR_PASSTHROUGH_IS_RUNNING_AT_CREATION_BIT_FB;
  layerInfo.purpose = XR_PASSTHROUGH_LAYER_PURPOSE_RECONSTRUCTION_FB;
  if (!xr_ok(pt.createLayer(g.session, &layerInfo, &pt.layer), "xrCreatePassthroughLayerFB")) {
    return;
  }
  pt.available = true;
  pt.layerRunning = true;
  pt.running = true;
  XLOG("passthrough ready");
}

const char* mode_name() {
  if (g.tabletop) {
    return g.mrMode ? "Tabletop · Living Room" : "Tabletop · Stadium";
  }
  return g.mrMode ? "Jumbotron · Living Room" : "Jumbotron · Stadium";
}

void set_mr_mode(bool mr) {
  g.mrMode = mr;
  Passthrough& pt = g.pt;
  if (!pt.available) {
    return;
  }
  // r60 (Lance on Konga: "running horrendously"): pausing only the layer left the passthrough running,
  // and the headset kept treating the app as mixed reality in the Stadium modes too (his r57 logcat:
  // "Switching MainState: from_state: ImmersiveAppMr" only when he quit, from Tabletop · Stadium, GPU
  // level 2 -> 3 right after), which holds the GPU clock down. The Stadium modes now stop it.
  if (mr && !pt.running) {
    pt.running = xr_ok(pt.start(pt.handle), "xrPassthroughStartFB");
  }
  if (mr && !pt.layerRunning) {
    pt.layerRunning = xr_ok(pt.resumeLayer(pt.layer), "xrPassthroughLayerResumeFB");
  } else if (!mr && pt.layerRunning) {
    if (xr_ok(pt.pauseLayer(pt.layer), "xrPassthroughLayerPauseFB")) {
      pt.layerRunning = false;
    }
  }
  if (!mr && pt.running && xr_ok(pt.pause(pt.handle), "xrPassthroughPauseFB")) {
    pt.running = false;
  }
  XLOG("mode: %s (passthrough %s)", mode_name(), pt.running ? "running" : "stopped");
}

void init_refresh_rate() {
  if (!g.hasRefreshRateExt) {
    return;
  }
  if (!get_proc(g.instance, "xrEnumerateDisplayRefreshRatesFB", &g.enumerateRefreshRates) ||
      !get_proc(g.instance, "xrRequestDisplayRefreshRateFB", &g.requestRefreshRate)) {
    return;
  }
  uint32_t count = 0;
  g.enumerateRefreshRates(g.session, 0, &count, nullptr);
  std::vector<float> rates(count);
  if (count > 0) {
    g.enumerateRefreshRates(g.session, count, &count, rates.data());
  }
  std::string list;
  for (float r : rates) {
    list += std::to_string(static_cast<int>(r + 0.5f)) + " ";
  }
  XLOG("display refresh rates: %s", list.c_str());
  // r2 stays at 72 Hz: one game frame in six is shown twice, but every display frame that runs a
  // game frame also carries the CPU readback, and 72 Hz is the budget that fits it.
  if (std::find(rates.begin(), rates.end(), 72.0f) != rates.end()) {
    xr_ok(g.requestRefreshRate(g.session, 72.0f), "xrRequestDisplayRefreshRateFB(72)");
  }
}

// ---------------------------------------------------------------------------------------------
// Input: Touch controllers -> GameCube pad (port 0)
// ---------------------------------------------------------------------------------------------

XrPath path(const char* s) {
  XrPath p = XR_NULL_PATH;
  xrStringToPath(g.instance, s, &p);
  return p;
}

bool make_action(XrActionType type, const char* name, const char* localized, XrAction* out) {
  XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
  info.actionType = type;
  std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
  std::strncpy(info.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
  return xr_ok(xrCreateAction(g.actions.set, &info, out), name);
}

bool create_actions() {
  Actions& a = g.actions;
  XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
  std::strncpy(setInfo.actionSetName, "strikers", XR_MAX_ACTION_SET_NAME_SIZE - 1);
  std::strncpy(setInfo.localizedActionSetName, "Strikers", XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
  if (!xr_ok(xrCreateActionSet(g.instance, &setInfo, &a.set), "xrCreateActionSet")) {
    return false;
  }
  bool ok = true;
  ok &= make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a", "A", &a.a);
  ok &= make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b", "B", &a.b);
  ok &= make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x", "X", &a.x);
  ok &= make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y", "Y", &a.y);
  ok &= make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_start", "Start", &a.menu);
  ok &= make_action(XR_ACTION_TYPE_FLOAT_INPUT, "trigger_l", "L", &a.triggerL);
  ok &= make_action(XR_ACTION_TYPE_FLOAT_INPUT, "trigger_r", "R", &a.triggerR);
  ok &= make_action(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze_l", "Grab", &a.squeezeL);
  ok &= make_action(XR_ACTION_TYPE_FLOAT_INPUT, "squeeze_r", "Z", &a.squeezeR);
  ok &= make_action(XR_ACTION_TYPE_VECTOR2F_INPUT, "stick_l", "Control stick", &a.stickL);
  ok &= make_action(XR_ACTION_TYPE_VECTOR2F_INPUT, "stick_r", "C-stick", &a.stickR);
  ok &= make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click_l", "MR/VR switch", &a.stickClickL);
  ok &= make_action(XR_ACTION_TYPE_BOOLEAN_INPUT, "stick_click_r", "Screen size", &a.stickClickR);
  ok &= make_action(XR_ACTION_TYPE_POSE_INPUT, "grip_pose_l", "Left hand", &a.gripPoseL);
  ok &= make_action(XR_ACTION_TYPE_POSE_INPUT, "grip_pose_r", "Right hand", &a.gripPoseR);
  ok &= make_action(XR_ACTION_TYPE_POSE_INPUT, "aim_pose_l", "Left pointer", &a.aimPoseL);
  ok &= make_action(XR_ACTION_TYPE_POSE_INPUT, "aim_pose_r", "Right pointer", &a.aimPoseR);
  ok &= make_action(XR_ACTION_TYPE_VIBRATION_OUTPUT, "rumble", "Rumble", &a.haptic);
  if (!ok) {
    return false;
  }

  const XrActionSuggestedBinding bindings[] = {
      {a.a, path("/user/hand/right/input/a/click")},
      {a.b, path("/user/hand/right/input/b/click")},
      {a.x, path("/user/hand/left/input/x/click")},
      {a.y, path("/user/hand/left/input/y/click")},
      {a.menu, path("/user/hand/left/input/menu/click")},
      {a.triggerL, path("/user/hand/left/input/trigger/value")},
      {a.triggerR, path("/user/hand/right/input/trigger/value")},
      {a.squeezeL, path("/user/hand/left/input/squeeze/value")},
      {a.squeezeR, path("/user/hand/right/input/squeeze/value")},
      {a.stickL, path("/user/hand/left/input/thumbstick")},
      {a.stickR, path("/user/hand/right/input/thumbstick")},
      {a.stickClickL, path("/user/hand/left/input/thumbstick/click")},
      {a.stickClickR, path("/user/hand/right/input/thumbstick/click")},
      {a.gripPoseL, path("/user/hand/left/input/grip/pose")},
      {a.gripPoseR, path("/user/hand/right/input/grip/pose")},
      {a.aimPoseL, path("/user/hand/left/input/aim/pose")},
      {a.aimPoseR, path("/user/hand/right/input/aim/pose")},
      {a.haptic, path("/user/hand/left/output/haptic")},
      {a.haptic, path("/user/hand/right/output/haptic")},
  };
  XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
  suggested.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
  suggested.countSuggestedBindings = static_cast<uint32_t>(sizeof(bindings) / sizeof(bindings[0]));
  suggested.suggestedBindings = bindings;
  if (!xr_ok(xrSuggestInteractionProfileBindings(g.instance, &suggested), "xrSuggestInteractionProfileBindings")) {
    return false;
  }
  XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
  attach.countActionSets = 1;
  attach.actionSets = &a.set;
  if (!xr_ok(xrAttachSessionActionSets(g.session, &attach), "xrAttachSessionActionSets")) {
    return false;
  }
  XrActionSpaceCreateInfo spaceInfo{XR_TYPE_ACTION_SPACE_CREATE_INFO};
  spaceInfo.action = a.gripPoseL;
  spaceInfo.poseInActionSpace.orientation.w = 1.0f;
  if (XR_FAILED(xrCreateActionSpace(g.session, &spaceInfo, &a.gripSpaceL))) {
    a.gripSpaceL = XR_NULL_HANDLE;
  }
  spaceInfo.action = a.gripPoseR;
  if (XR_FAILED(xrCreateActionSpace(g.session, &spaceInfo, &a.gripSpaceR))) {
    a.gripSpaceR = XR_NULL_HANDLE;
  }
  spaceInfo.action = a.aimPoseL;
  if (XR_FAILED(xrCreateActionSpace(g.session, &spaceInfo, &a.aimSpaceL))) {
    a.aimSpaceL = XR_NULL_HANDLE;
  }
  spaceInfo.action = a.aimPoseR;
  if (XR_FAILED(xrCreateActionSpace(g.session, &spaceInfo, &a.aimSpaceR))) {
    a.aimSpaceR = XR_NULL_HANDLE;
  }
  return true;
}

bool get_bool(XrAction action) {
  XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
  info.action = action;
  XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
  if (XR_FAILED(xrGetActionStateBoolean(g.session, &info, &state))) {
    return false;
  }
  return state.isActive && state.currentState;
}

float get_float(XrAction action) {
  XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
  info.action = action;
  XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
  if (XR_FAILED(xrGetActionStateFloat(g.session, &info, &state)) || !state.isActive) {
    return 0.0f;
  }
  return state.currentState;
}

XrVector2f get_vec2(XrAction action) {
  XrActionStateGetInfo info{XR_TYPE_ACTION_STATE_GET_INFO};
  info.action = action;
  XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
  if (XR_FAILED(xrGetActionStateVector2f(g.session, &info, &state)) || !state.isActive) {
    return XrVector2f{0.0f, 0.0f};
  }
  return state.currentState;
}

// r48 (Lance: "make all analog stick movements easier to pick up on"; a player "could not travel
// diagonally at one point"): the game reads every stick through PADClampCircle (platpad.cpp), which
// takes a square deadzone of 15 off EACH axis and then clamps to a circle (radius 56 for the stick,
// 44 for the C-stick). So a light push read as nothing at all, and a part push near a diagonal lost
// its smaller axis and ran straight along the other. Each axis now goes out as 15 + exactly what it
// should be after that clamp: the game gets the stick's own direction at every push, and movement
// from the first bit of push past our small round deadzone, rising evenly to full at full tilt.
// (r44 had only lowered our own deadzone, 0.12 -> 0.05, with the game's still on top.)
constexpr float kTouchDeadzone = 0.05f;  // Touch thumbsticks rest close to centre
constexpr float kPadDeadzone = 0.12f;    // r48: Bluetooth pads, read raw now (PADGetPhysicalStatus)
constexpr int kGameStickMin = 15;        // PADClampCircle's per-axis deadzone, both sticks
constexpr int kGameStickRadius = 56, kGameSubstickRadius = 44;

void apply_stick(XrVector2f v, float deadzone, int radius, int8_t* outX, int8_t* outY) {
  const float mag = std::sqrt(v.x * v.x + v.y * v.y);
  if (mag <= deadzone) {
    *outX = 0;
    *outY = 0;
    return;
  }
  const float m = std::min(1.0f, (mag - deadzone) / (1.0f - deadzone));
  auto axis = [&](float u) -> int8_t {
    const int after = static_cast<int>(std::lround(std::fabs(u) / mag * m * radius));  // what the game should see
    if (after <= 0) return 0;
    const int out = std::min(127, kGameStickMin + after);
    return static_cast<int8_t>(u < 0.0f ? -out : out);
  };
  *outX = axis(v.x);
  *outY = axis(v.y);  // OpenXR +y is up, as is the GameCube's
}

uint8_t trigger_analog(float v) {
  if (v < 0.05f) {
    return 0;
  }
  return static_cast<uint8_t>(std::lround(std::min(1.0f, v) * 255.0f));
}

// The game's pad rumble on both Touch controllers. A start replaces whatever is buzzing (the
// runtime does the same), so the stop only goes out when no new start came with it.
void apply_rumble() {
  Rumble& r = g.rumble;
  if (!r.startPending && !r.stopPending) {
    return;
  }
  XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};
  info.action = g.actions.haptic;
  if (r.startPending) {
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};
    vibration.amplitude = r.amplitude;
    vibration.duration = static_cast<XrDuration>(std::max(0.02f, r.seconds) * 1e9f);
    vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
    xr_ok(xrApplyHapticFeedback(g.session, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration)),
          "xrApplyHapticFeedback");
    ++g.rumblePulses;
  } else {
    xr_ok(xrStopHapticFeedback(g.session, &info), "xrStopHapticFeedback");
  }
  r.startPending = false;
  r.stopPending = false;
}

bool locate_hand(XrSpace space, XrVector3f* out) {
  if (space == XR_NULL_HANDLE) return false;
  XrSpaceLocation hand{XR_TYPE_SPACE_LOCATION};
  if (XR_FAILED(xrLocateSpace(space, g.local, g.frameState.predictedDisplayTime, &hand)) ||
      (hand.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0) {
    return false;
  }
  *out = hand.pose.position;
  return true;
}

// The hand on or right next to the Tabletop field (within 8 cm of the pitch and its fence, from a
// little under the field to a little over the fence).
bool near_table(const XrVector3f& p) {
  const float dx = p.x - g.table.position.x, dz = p.z - g.table.position.z;
  const float cs = std::cos(g.table.yaw), sn = std::sin(g.table.yaw), s = g.table.scale;
  // Back into game units (the inverse of the field's room mapping, as xr_fx.cpp does).
  const float gx = (dx * cs - dz * sn) / s, gy = -(dx * sn + dz * cs) / s;
  const float margin = 0.08f / s;
  const float dy = p.y - g.table.position.y;
  return std::fabs(gx) < 22.5f + margin && std::fabs(gy) < 13.5f + margin && dy > -0.12f && dy < 2.5f * s + 0.15f;
}

// --- r42: the Jumbotron screen by hand (after Charged r15) ---

int screen_room() { return g.mrMode ? 0 : 1; }
const ScreenPreset& screen_preset(int room) {
  return room == 0 ? kScreenPresets[g.screenPreset] : kStadiumScreenPresets[g.screenPreset];
}

// The screen `distance` ahead of where you look (level), facing you. Without a head: straight ahead
// of the start pose, which is where r41 and earlier always put it.
void place_screen_ahead(FreeScreen& s, int room, const XrPosef* head) {
  const ScreenPreset& preset = screen_preset(room);
  float fx = 0.0f, fz = -1.0f;
  XrVector3f from{0.0f, 0.0f, 0.0f};
  if (head != nullptr) {
    const XrQuaternionf& o = head->orientation;
    fx = -(2 * (o.x * o.z + o.w * o.y));
    fz = -(1 - 2 * (o.x * o.x + o.y * o.y));
    const float len = std::max(1e-3f, std::sqrt(fx * fx + fz * fz));
    fx /= len;
    fz /= len;
    from = head->position;
  }
  // Room: a little below eye level. Stadium: its centre about a meter above your eyes.
  const float y = room == 0 ? from.y + kScreenDrop : std::max(from.y + 0.9f, g.floorY + 2.4f);
  s.position = {from.x + fx * preset.distance, y, from.z + fz * preset.distance};
  s.yaw = std::atan2(-fx, -fz);
  s.width = preset.width;
  s.grabHands = 0;
  s.placed = true;
}

// xr_screens.txt beside the game's log and settings (STRIKERS_HOME, StrikersHome.java).
std::string screens_path() {
  const char* home = std::getenv("STRIKERS_HOME");
  return std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/xr_screens.txt";
}

void save_screens() {
  if (FILE* f = std::fopen(screens_path().c_str(), "w")) {
    for (int room = 0; room < 2; ++room) {
      const FreeScreen& s = g.screens[room];
      std::fprintf(f, "%d %d %.3f %.3f %.3f %.4f %.3f\n", room, s.placed ? 1 : 0, s.position.x, s.position.y,
                   s.position.z, s.yaw, s.width);
    }
    std::fclose(f);
  }
}

// Where you left the screens last time. LOCAL starts where the session starts, so this is close as
// long as you play from the same spot; a left stick tap brings a screen back in front of you.
void load_screens() {
  if (FILE* f = std::fopen(screens_path().c_str(), "r")) {
    int room = 0, placed = 0;
    float x = 0, y = 0, z = 0, yaw = 0, width = 0;
    while (std::fscanf(f, "%d %d %f %f %f %f %f", &room, &placed, &x, &y, &z, &yaw, &width) == 7) {
      if (room < 0 || room > 1 || !placed || !std::isfinite(x + y + z + yaw + width)) continue;
      FreeScreen& s = g.screens[room];
      s.placed = true;
      s.position = {x, y, z};
      s.yaw = yaw;
      s.width = std::max(0.5f, std::min(40.0f, width));
      // r52 (Lance: his friend "couldnt join my game ... you can hear the menu but you cant see it"; his
      // headset had kept the room screen 3 m ahead but turned 135 degrees away, its back to him, and a
      // quad's back isn't drawn; since r49 a left stick click no longer brings it back): every start,
      // the screen faces where you start (LOCAL's origin), and one left behind or off to the side of
      // where you face comes back in front (placed again at the first frame, place_free_screen).
      const float ahead = -z, side = std::fabs(x);
      if (ahead < 0.5f || side > 1.2f * ahead) {
        s.placed = false;
        XLOG("screens: the %s screen was behind or beside you, it comes back in front", room ? "stadium" : "room");
      } else {
        s.yaw = std::atan2(-x, -z);  // turned to the origin, as place_screen_ahead turns it to you
        if (std::fabs(std::remainder(yaw - s.yaw, 6.2831853f)) > 0.3f) {
          XLOG("screens: the %s screen was turned %.0f degrees away, it faces you again", room ? "stadium" : "room",
               std::fabs(std::remainder(yaw - s.yaw, 6.2831853f)) * 57.29578f);
        }
      }
    }
    std::fclose(f);
    XLOG("screens: room (%.1f, %.1f, %.1f) %.1f m, stadium (%.1f, %.1f, %.1f) %.1f m", g.screens[0].position.x,
         g.screens[0].position.y, g.screens[0].position.z, g.screens[0].width, g.screens[1].position.x,
         g.screens[1].position.y, g.screens[1].position.z, g.screens[1].width);
  }
}

// Lance's grab rule: the left grip moves the screen, both grips turn and resize it. r60 (Lance: "the
// grips button to only activate when we touch the border of the screen, not just press the buttons
// randomly away from the screen and it move the screen"): the left grip takes it only while its
// pointer or hand is on the screen's edge (g.screenEdgeHover; the edge lights up); the right grip
// then joins from anywhere. Returns true while the right grip is part of a two-hand grab.
bool update_screen(bool gripL, bool gripR, bool haveL, bool haveR, const XrVector3f& handL,
                   const XrVector3f& handR) {
  const int room = screen_room();
  FreeScreen& s = g.screens[room];
  if (g.bringScreen && g.headValid) {
    g.bringScreen = false;
    place_screen_ahead(s, room, &g.headPose);
    g.screensDirty = true;
    XLOG("screen brought to (%.2f, %.2f, %.2f), %.1f m wide", s.position.x, s.position.y, s.position.z, s.width);
  }
  if (!s.placed) place_screen_ahead(s, room, nullptr);
  int hands = 0;
  if (gripL && haveL && (s.grabHands != 0 || (!g.prevGripL && g.screenEdgeHover))) {
    hands = 1;
    if (gripR && haveR) hands |= 2;
  }
  g.prevGripL = gripL;
  if (hands != s.grabHands) {
    if (hands == 0) {
      g.screensDirty = true;
    } else {
      const XrVector3f& h = g.headPose.position;
      const float dx = s.position.x - h.x, dy = s.position.y - h.y, dz = s.position.z - h.z;
      s.gain = g.headValid ? std::max(1.0f, std::min(15.0f, std::sqrt(dx * dx + dy * dy + dz * dz) / 0.6f)) : 1.0f;
    }
    s.grabPos = s.position;
    s.grabHand = handL;
    if (hands & 2) {
      const float dx = handR.x - handL.x, dz = handR.z - handL.z;
      s.twoDist = std::max(0.05f, std::sqrt(dx * dx + dz * dz));
      s.twoAngle = std::atan2(dx, dz);
      s.twoWidth = s.width;
      s.twoYaw = s.yaw;
      s.twoPos = s.position;
      s.twoMid = {0.5f * (handL.x + handR.x), 0.5f * (handL.y + handR.y), 0.5f * (handL.z + handR.z)};
    }
    s.grabHands = hands;
  }
  if (g.screensDirty && s.grabHands == 0) {
    g.screensDirty = false;
    save_screens();
  }
  if (s.grabHands & 2) {
    const float dx = handR.x - handL.x, dz = handR.z - handL.z;
    const float dist = std::max(0.05f, std::sqrt(dx * dx + dz * dz));
    float turn = std::atan2(dx, dz) - s.twoAngle;
    while (turn > 3.14159265f) turn -= 6.2831853f;
    while (turn < -3.14159265f) turn += 6.2831853f;
    s.width = std::max(0.5f, std::min(40.0f, s.twoWidth * dist / s.twoDist));
    s.yaw = s.twoYaw + turn;
    const XrVector3f mid{0.5f * (handL.x + handR.x), 0.5f * (handL.y + handR.y), 0.5f * (handL.z + handR.z)};
    s.position = {s.twoPos.x + (mid.x - s.twoMid.x) * s.gain, s.twoPos.y + (mid.y - s.twoMid.y) * s.gain,
                  s.twoPos.z + (mid.z - s.twoMid.z) * s.gain};
    return true;
  }
  if (s.grabHands & 1) {
    s.position = {s.grabPos.x + (handL.x - s.grabHand.x) * s.gain, s.grabPos.y + (handL.y - s.grabHand.y) * s.gain,
                  s.grabPos.z + (handL.z - s.grabHand.z) * s.gain};
  }
  return false;
}

void place_free_screen(XrCompositionLayerQuad& quad, float aspect) {
  const int room = screen_room();
  FreeScreen& s = g.screens[room];
  if (!s.placed) place_screen_ahead(s, room, nullptr);
  const float half = 0.5f * s.yaw;
  quad.pose.orientation = {0.0f, std::sin(half), 0.0f, std::cos(half)};
  // r55: shaken by item blasts like the field is.
  quad.pose.position = XrVector3f{s.position.x + g.joltPos.x, s.position.y + g.joltPos.y, s.position.z + g.joltPos.z};
  quad.size.width = s.width;
  quad.size.height = s.width * aspect;
}

// --- r57: the trophy shelf in your room (Lance: "a shelf you place in MR") ---

std::string shelf_path() {
  const char* home = std::getenv("STRIKERS_HOME");
  return std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/xr_shelf.txt";
}

void save_shelf() {
  if (FILE* f = std::fopen(shelf_path().c_str(), "w")) {
    std::fprintf(f, "%.3f %.3f %.3f %.4f\n", g.shelf.position.x, g.shelf.position.y, g.shelf.position.z, g.shelf.yaw);
    std::fclose(f);
  }
}

// Where you left it; one left far from where you start (over 5 m) is put next to you again.
void load_shelf() {
  if (FILE* f = std::fopen(shelf_path().c_str(), "r")) {
    float x = 0, y = 0, z = 0, yaw = 0;
    if (std::fscanf(f, "%f %f %f %f", &x, &y, &z, &yaw) == 4 && std::isfinite(x + y + z + yaw) &&
        x * x + z * z < 25.0f) {
      g.shelf.placed = true;
      g.shelf.position = {x, y, z};
      g.shelf.yaw = yaw;
      XLOG("shelf: at (%.2f, %.2f, %.2f)", x, y, z);
    }
    std::fclose(f);
  }
}

// The first time: off to your left, a little ahead, at about chest height, its front to you.
void place_shelf(const XrPosef& head) {
  const XrQuaternionf& o = head.orientation;
  float fx = -(2 * (o.x * o.z + o.w * o.y)), fz = -(1 - 2 * (o.x * o.x + o.y * o.y));
  const float len = std::max(1e-3f, std::sqrt(fx * fx + fz * fz));
  fx /= len;
  fz /= len;
  const float lx = fz, lz = -fx;  // your left, along the floor
  XrVector3f& p = g.shelf.position;
  p = {head.position.x + fx * 0.9f + lx * 0.8f, head.position.y - 0.3f, head.position.z + fz * 0.9f + lz * 0.8f};
  g.shelf.yaw = std::atan2(head.position.x - p.x, head.position.z - p.z);
  g.shelf.placed = true;
  save_shelf();
  XLOG("shelf: placed at (%.2f, %.2f, %.2f), grab it with a grip to move it", p.x, p.y, p.z);
}

// r62 (Lance: "you also took away the ability for me to grab and resize the cup trophies"): the shelf
// the trophies stand on now: in the cup suite its own (beside the pedestal, fixed), else the one you
// placed in your room.
XrVector3f suite_point(float x, float y, float z);  // below
XrVector3f shelf_at() { return g.inSuite ? suite_point(1.3f, 1.0f, -1.4f) : g.shelf.position; }
float shelf_turn() { return g.inSuite ? g.suiteYaw + std::atan2(-1.3f, 1.4f) : g.shelf.yaw; }

// A hand on or right by the shelf (its board, the trophies on it, a few centimeters round).
bool near_shelf(const XrVector3f& p) {
  const XrVector3f at = shelf_at();
  const float dx = p.x - at.x, dz = p.z - at.z;
  const float c = std::cos(shelf_turn()), s = std::sin(shelf_turn());
  const float lx = dx * c - dz * s, lz = dx * s + dz * c, ly = p.y - at.y;
  return std::fabs(lx) < 0.52f && std::fabs(lz) < 0.2f && ly > -0.12f && ly < 0.26f;
}

// r57b: quaternion helpers for the trophies in your hands (x, y, z, w).
XrQuaternionf q_mul(const XrQuaternionf& a, const XrQuaternionf& b) {
  return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
XrQuaternionf q_conj(const XrQuaternionf& q) { return {-q.x, -q.y, -q.z, q.w}; }
XrVector3f q_rot(const XrQuaternionf& q, const XrVector3f& v) {
  const XrQuaternionf p{v.x, v.y, v.z, 0.0f};
  const XrQuaternionf r = q_mul(q_mul(q, p), q_conj(q));
  return {r.x, r.y, r.z};
}

// r60: where a pointer (its pose; it points along -Z) meets a quad (its pose; the quad lies in its
// x/y plane): the hit in the quad's own x / y from its middle (meters) and how far along the ray.
// False when it points along the plane or away from it.
bool ray_quad(const XrPosef& ray, const XrPosef& quad, float* qx, float* qy, float* dist) {
  const XrVector3f dir = q_rot(ray.orientation, XrVector3f{0.0f, 0.0f, -1.0f});
  const XrVector3f n = q_rot(quad.orientation, XrVector3f{0.0f, 0.0f, 1.0f});
  const float denom = dir.x * n.x + dir.y * n.y + dir.z * n.z;
  if (std::fabs(denom) < 1e-4f) return false;
  const XrVector3f d{quad.position.x - ray.position.x, quad.position.y - ray.position.y,
                     quad.position.z - ray.position.z};
  const float t = (d.x * n.x + d.y * n.y + d.z * n.z) / denom;
  if (t < 0.0f || t > 40.0f) return false;
  const XrVector3f rel{ray.position.x + dir.x * t - quad.position.x, ray.position.y + dir.y * t - quad.position.y,
                       ray.position.z + dir.z * t - quad.position.z};
  const XrVector3f local = q_rot(q_conj(quad.orientation), rel);
  *qx = local.x;
  *qy = local.y;
  *dist = t;
  return true;
}

// r60: a point in a w x h quad's own x / y on its edge: within a band along each side (8% of its
// width, 8 cm to 60 cm) or a little past it (5%, up to 40 cm).
bool on_quad_edge(float qx, float qy, float w, float h) {
  const float band = std::max(0.08f, std::min(0.6f, 0.08f * w));
  const float out = std::min(0.4f, 0.05f * w);
  const bool inOuter = std::fabs(qx) <= 0.5f * w + out && std::fabs(qy) <= 0.5f * h + out;
  const bool inInner = std::fabs(qx) < 0.5f * w - band && std::fabs(qy) < 0.5f * h - band;
  return inOuter && !inInner;
}

// A trophy's spot on the shelf (LOCAL): xr_fx.cpp shelf_slot, turned with the shelf.
XrPosef cup_slot_pose(int i) {
  const float x = -0.405f + static_cast<float>(i) * 0.1157f, y = 0.016f + 0.06f;
  const XrVector3f at = shelf_at();
  const float yaw = shelf_turn();
  const float c = std::cos(yaw), s = std::sin(yaw);
  XrPosef p;
  p.position = {at.x + x * c, at.y + y, at.z - x * s};
  p.orientation = {0.0f, std::sin(0.5f * yaw), 0.0f, std::cos(0.5f * yaw)};
  return p;
}

// Where a trophy is shown now: off its spot (held, gliding back) or on it.
XrPosef cup_pose(int i) { return g.shelf.cupFree[i] ? g.shelf.cupPose[i] : cup_slot_pose(i); }

// The trophy nearest a hand, within 9 cm of its middle; -1 none (or held by the other hand).
int cup_near(const XrVector3f& p) {
  int best = -1;
  float bestD = 0.09f * 0.09f;
  for (int i = 0; i < 8; ++i) {
    if (g.shelf.heldCup[0] == i || g.shelf.heldCup[1] == i) continue;
    const XrVector3f c = cup_pose(i).position;
    const float d = (p.x - c.x) * (p.x - c.x) + (p.y - c.y) * (p.y - c.y) + (p.z - c.z) * (p.z - c.z);
    if (d < bestD) {
      bestD = d;
      best = i;
    }
  }
  return best;
}

// Lance's grab rule, for the shelf: one grip carries it, both turn it. A grip takes it only when it
// closes on the shelf, and then the table / screen / item swap leave that grip alone until it opens.
// r57b: a grip closing on a trophy takes the trophy instead, and it goes where the hand goes (turning
// with it); let go, it glides back to its spot in half a second.
void update_shelf(bool gripL, bool gripR, bool haveL, bool haveR, const XrVector3f& handL, const XrVector3f& handR,
                  const XrPosef* poseL, const XrPosef* poseR) {
  auto& sh = g.shelf;
  const bool here = (g.mrMode && sh.placed) || g.inSuite;  // r62: the suite's trophies too
  const bool grip[2] = {gripL, gripR}, have[2] = {haveL, haveR}, prev[2] = {sh.prevGripL, sh.prevGripR};
  const XrVector3f hand[2] = {handL, handR};
  const XrPosef* pose[2] = {poseL, poseR};
  bool* takes[2] = {&sh.handL, &sh.handR};
  const uint64_t now = now_ns();
  for (int h = 0; h < 2; ++h) {
    if (here && grip[h] && !prev[h] && have[h]) {
      const int cup = pose[h] != nullptr ? cup_near(hand[h]) : -1;
      const int other = 1 - h;
      if (cup < 0 && sh.heldCup[other] >= 0 && sh.heldCup[h] < 0 && have[other]) {
        // r60: the other hand holds a trophy: this grip resizes it (pull apart = bigger).
        const XrVector3f& a = hand[h];
        const XrVector3f& b = hand[other];
        sh.resizeHand = h;
        sh.resizeDist0 = std::max(0.05f, std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                                                   (a.z - b.z) * (a.z - b.z)));
        sh.resizeScale0 = sh.cupScale[sh.heldCup[other]];
        *takes[h] = true;
        XLOG("shelf: resizing trophy %d", sh.heldCup[other]);
      } else if (cup >= 0) {
        const XrPosef cp = cup_pose(cup);
        const XrQuaternionf inv = q_conj(pose[h]->orientation);
        sh.heldCup[h] = cup;
        sh.heldOff[h] = q_rot(inv, XrVector3f{cp.position.x - pose[h]->position.x, cp.position.y - pose[h]->position.y,
                                             cp.position.z - pose[h]->position.z});
        sh.heldTurn[h] = q_mul(inv, cp.orientation);
        sh.cupFree[cup] = true;
        sh.cupBack[cup] = false;
        sh.cupPose[cup] = cp;
        *takes[h] = true;
        XLOG("shelf: trophy %d picked up", cup);
      } else if (!g.inSuite && near_shelf(hand[h])) {  // the suite's shelf is part of the room
        *takes[h] = true;
      }
    }
    if (!grip[h] || !have[h] || !here) {
      *takes[h] = false;
      sh.spent[h] = false;
      if (sh.resizeHand == h) sh.resizeHand = -1;
      if (sh.heldCup[h] >= 0) {
        // Let go: back to its spot (and its shelf size).
        const int cup = sh.heldCup[h];
        sh.cupBack[cup] = true;
        sh.cupFrom[cup] = sh.cupPose[cup];
        sh.cupFromScale[cup] = sh.cupScale[cup];
        sh.cupBackNs[cup] = now;
        sh.heldCup[h] = -1;
        if (sh.resizeHand == 1 - h) {
          // The resizing hand stays taken (no table, screen or item swap) but never carries the
          // shelf, until it opens.
          sh.resizeHand = -1;
          sh.spent[1 - h] = true;
        }
      }
    }
    if (sh.resizeHand == h && sh.heldCup[1 - h] >= 0) {
      const XrVector3f& a = hand[h];
      const XrVector3f& b = hand[1 - h];
      const float d = std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
      sh.cupScale[sh.heldCup[1 - h]] = std::max(0.6f, std::min(8.0f, sh.resizeScale0 * d / sh.resizeDist0));
    }
    if (sh.heldCup[h] >= 0 && pose[h] != nullptr) {
      const int cup = sh.heldCup[h];
      const XrVector3f off = q_rot(pose[h]->orientation, sh.heldOff[h]);
      sh.cupPose[cup].position = {pose[h]->position.x + off.x, pose[h]->position.y + off.y, pose[h]->position.z + off.z};
      sh.cupPose[cup].orientation = q_mul(pose[h]->orientation, sh.heldTurn[h]);
    }
  }
  // Trophies on their way back.
  for (int i = 0; i < 8; ++i) {
    if (!sh.cupBack[i]) continue;
    const float t = static_cast<float>(now - sh.cupBackNs[i]) / 450e6f;
    if (t >= 1.0f) {
      sh.cupBack[i] = false;
      sh.cupFree[i] = false;
      sh.cupScale[i] = 1.0f;
      continue;
    }
    const float e = t * t * (3.0f - 2.0f * t);
    sh.cupScale[i] = sh.cupFromScale[i] + (1.0f - sh.cupFromScale[i]) * e;
    const XrPosef to = cup_slot_pose(i);
    const XrPosef& from = sh.cupFrom[i];
    sh.cupPose[i].position = {from.position.x + (to.position.x - from.position.x) * e,
                              from.position.y + (to.position.y - from.position.y) * e,
                              from.position.z + (to.position.z - from.position.z) * e};
    XrQuaternionf a = from.orientation, b = to.orientation;
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) b = {-b.x, -b.y, -b.z, -b.w};
    XrQuaternionf q{a.x + (b.x - a.x) * e, a.y + (b.y - a.y) * e, a.z + (b.z - a.z) * e, a.w + (b.w - a.w) * e};
    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l > 1e-6f) q = {q.x / l, q.y / l, q.z / l, q.w / l};
    sh.cupPose[i].orientation = q;
  }
  sh.prevGripL = gripL;
  sh.prevGripR = gripR;
  // The shelf itself moves only with the hands that took it (not ones holding or resizing a trophy).
  const int hands = (sh.handL && sh.heldCup[0] < 0 && sh.resizeHand != 0 && !sh.spent[0] ? 1 : 0) |
                    (sh.handR && sh.heldCup[1] < 0 && sh.resizeHand != 1 && !sh.spent[1] ? 2 : 0);
  if (hands != sh.grabHands) {
    if (hands == 3) {
      sh.twoAngle = std::atan2(handR.x - handL.x, handR.z - handL.z);
      sh.twoYaw = sh.yaw;
      sh.twoPos = sh.position;
      sh.twoMid = {0.5f * (handL.x + handR.x), 0.5f * (handL.y + handR.y), 0.5f * (handL.z + handR.z)};
    } else if (hands != 0) {
      const XrVector3f& h = hands == 1 ? handL : handR;
      sh.grabOffset = {sh.position.x - h.x, sh.position.y - h.y, sh.position.z - h.z};
    } else {
      save_shelf();
      XLOG("shelf: left at (%.2f, %.2f, %.2f)", sh.position.x, sh.position.y, sh.position.z);
    }
    sh.grabHands = hands;
  }
  if (hands == 3) {
    float turn = std::atan2(handR.x - handL.x, handR.z - handL.z) - sh.twoAngle;
    while (turn > 3.14159265f) turn -= 6.2831853f;
    while (turn < -3.14159265f) turn += 6.2831853f;
    sh.yaw = sh.twoYaw + turn;
    const XrVector3f mid{0.5f * (handL.x + handR.x), 0.5f * (handL.y + handR.y), 0.5f * (handL.z + handR.z)};
    sh.position = {sh.twoPos.x + mid.x - sh.twoMid.x, sh.twoPos.y + mid.y - sh.twoMid.y, sh.twoPos.z + mid.z - sh.twoMid.z};
  } else if (hands != 0) {
    const XrVector3f& h = hands == 1 ? handL : handR;
    sh.position = {h.x + sh.grabOffset.x, h.y + sh.grabOffset.y, h.z + sh.grabOffset.z};
  }
}

void sync_input() {
  PADStatus status{};
  status.err = PAD_ERR_NONE;

  if (g.focused) {
    XrActiveActionSet active{g.actions.set, XR_NULL_PATH};
    XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};
    sync.countActiveActionSets = 1;
    sync.activeActionSets = &active;
    if (XR_SUCCEEDED(xrSyncActions(g.session, &sync))) {
      const Actions& a = g.actions;
      // r46 (Lance: "some type of button mapping for this game so users can customize their
      // controls"): each GameCube button comes from the Quest button the panel's Controls page picked
      // for it (xr_controls.txt); the defaults are the layout r1-r45 had. The sticks, the stick clicks
      // (views) and the left grip (moving the table or the screen) stay as they are.
      reload_controls();
      float src[kSrcCount] = {};
      src[kSrcA] = get_bool(a.a) ? 1.0f : 0.0f;
      src[kSrcB] = get_bool(a.b) ? 1.0f : 0.0f;
      src[kSrcX] = get_bool(a.x) ? 1.0f : 0.0f;
      src[kSrcY] = get_bool(a.y) ? 1.0f : 0.0f;
      src[kSrcMenu] = get_bool(a.menu) ? 1.0f : 0.0f;
      src[kSrcTrigL] = get_float(a.triggerL);
      src[kSrcTrigR] = get_float(a.triggerR);
      src[kSrcGripR] = get_float(a.squeezeR);
      // r47 (players: a Bluetooth pad picked its side and the match then went back to the Touch
      // controllers; online it moved nobody): every paired pad is this headset's player too, through
      // the same button map (Aurora no longer puts pads in ports of their own, PADSetPhysicalExternal).
      float pad[kSrcCount] = {};
      PADStatus phys{};
      const bool havePad = PADGetPhysicalStatus(&phys);
      if (havePad) {
        pad[kSrcA] = (phys.button & PAD_BUTTON_A) ? 1.0f : 0.0f;
        pad[kSrcB] = (phys.button & PAD_BUTTON_B) ? 1.0f : 0.0f;
        pad[kSrcX] = (phys.button & PAD_BUTTON_X) ? 1.0f : 0.0f;
        pad[kSrcY] = (phys.button & PAD_BUTTON_Y) ? 1.0f : 0.0f;
        pad[kSrcMenu] = (phys.button & PAD_BUTTON_START) ? 1.0f : 0.0f;
        pad[kSrcGripR] = (phys.button & PAD_TRIGGER_Z) ? 1.0f : 0.0f;  // RB
        pad[kSrcTrigL] = (phys.button & PAD_TRIGGER_L) ? 1.0f : std::min(1.0f, phys.triggerLeft / 180.0f);
        pad[kSrcTrigR] = (phys.button & PAD_TRIGGER_R) ? 1.0f : std::min(1.0f, phys.triggerRight / 180.0f);
        // The D-pad as it is (the menus).
        status.button |= phys.button & (PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT);
      }
      auto from = [&](int act) {
        const int s = g.controls[act];
        return s < kSrcCount ? std::max(src[s], pad[s]) : 0.0f;
      };
      if (from(kActA) > 0.5f) status.button |= PAD_BUTTON_A;
      if (from(kActB) > 0.5f) status.button |= PAD_BUTTON_B;
      if (from(kActX) > 0.5f) status.button |= PAD_BUTTON_X;
      if (from(kActY) > 0.5f) status.button |= PAD_BUTTON_Y;
      if (from(kActStart) > 0.5f) status.button |= PAD_BUTTON_START;
      float tl = from(kActL);
      const float tr = from(kActR);
      // r47: the long pass, L held and then A: L alone for its first 50 ms, so the game has L down
      // before A's press arrives, then both while the button is held.
      if (from(kActLong) > 0.5f) {
        const uint64_t tLong = now_ns();
        if (g.longPassSinceNs == 0) g.longPassSinceNs = tLong;
        tl = 1.0f;
        if (tLong - g.longPassSinceNs >= 50000000ull) status.button |= PAD_BUTTON_A;
      } else {
        g.longPassSinceNs = 0;
      }
      status.triggerLeft = trigger_analog(tl);
      status.triggerRight = trigger_analog(tr);
      if (tl > 0.9f) status.button |= PAD_TRIGGER_L;
      if (tr > 0.9f) status.button |= PAD_TRIGGER_R;
      if (g.controls[kActZ] != kSrcGripR && from(kActZ) > 0.5f) status.button |= PAD_TRIGGER_Z;
      // The pad's RB never grabs the table: Z whenever Z is on the right grip.
      if (g.controls[kActZ] == kSrcGripR && pad[kSrcGripR] > 0.5f) status.button |= PAD_TRIGGER_Z;

      // Tabletop: grabbing the field (Lance, 2026-10-10). The left grip grabs it from anywhere; the
      // right grip grabs it when the hand is on or right next to it, and anywhere else stays the
      // GameCube Z (the item swap). One hand carries it, up and down too; with both hands, pulling
      // them apart or together resizes it and turning them around each other turns it. The left
      // hand alone still resizes (stick up/down) and turns (left/right) it with the stick.
      XrVector2f stickL = get_vec2(a.stickL);
      XrVector3f handL{}, handR{};
      const bool haveL = locate_hand(a.gripSpaceL, &handL);
      const bool haveR = locate_hand(a.gripSpaceR, &handR);
      // r42: your head, for bringing the screen in front of you.
      if (g.view != XR_NULL_HANDLE) {
        XrSpaceLocation head{XR_TYPE_SPACE_LOCATION};
        if (XR_SUCCEEDED(xrLocateSpace(g.view, g.local, g.frameState.predictedDisplayTime, &head)) &&
            (head.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
            (head.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0) {
          g.headPose = head.pose;
          g.headValid = true;
        }
      }
      const bool gripR = get_float(a.squeezeR) > 0.5f;
      const bool gripL = get_float(a.squeezeL) > 0.6f;
      // r60: the pointers, and whether the left one (or the left hand itself) is on the big screen's
      // edge, where its grip takes the screen.
      {
        const XrSpace aims[2] = {a.aimSpaceL, a.aimSpaceR};
        for (int h = 0; h < 2; ++h) {
          XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
          const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
          g.aimValid[h] = aims[h] != XR_NULL_HANDLE &&
                          XR_SUCCEEDED(xrLocateSpace(aims[h], g.local, g.frameState.predictedDisplayTime, &loc)) &&
                          (loc.locationFlags & need) == need;
          if (g.aimValid[h]) g.aim[h] = loc.pose;
        }
        g.screenEdgeHover = false;
        if (g.shownScreenValid && (!g.tabletop || g.stereoFailed)) {
          float qx = 0, qy = 0, dist = 0;
          if (g.aimValid[0] && ray_quad(g.aim[0], g.shownScreenPose, &qx, &qy, &dist) &&
              on_quad_edge(qx, qy, g.shownScreenW, g.shownScreenH)) {
            g.screenEdgeHover = true;
            const XrVector3f dir = q_rot(g.aim[0].orientation, XrVector3f{0.0f, 0.0f, -1.0f});
            g.screenEdgePoint = {g.aim[0].position.x + dir.x * dist, g.aim[0].position.y + dir.y * dist,
                                 g.aim[0].position.z + dir.z * dist};
          } else if (haveL) {
            // The hand itself on the edge (a screen pulled within reach).
            const XrPosef& sp = g.shownScreenPose;
            const XrVector3f local = q_rot(q_conj(sp.orientation), XrVector3f{handL.x - sp.position.x,
                                                                              handL.y - sp.position.y,
                                                                              handL.z - sp.position.z});
            if (std::fabs(local.z) < 0.12f && on_quad_edge(local.x, local.y, g.shownScreenW, g.shownScreenH)) {
              g.screenEdgeHover = true;
              g.screenEdgePoint = handL;
            }
          }
        }
      }
      // r57: the trophy shelf first: a grip that closes on it takes it from the table and screen.
      if (g.mrMode && !g.shelf.placed && g.headValid) place_shelf(g.headPose);
      {
        // r57b: the hands' whole poses, for a trophy picked up (it turns with the hand).
        XrPosef poseL{}, poseR{};
        auto full = [&](XrSpace space, XrPosef* out) {
          XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
          if (space == XR_NULL_HANDLE || XR_FAILED(xrLocateSpace(space, g.local, g.frameState.predictedDisplayTime, &loc)))
            return false;
          const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
          if ((loc.locationFlags & need) != need) return false;
          *out = loc.pose;
          return true;
        };
        const bool fullL = full(a.gripSpaceL, &poseL), fullR = full(a.gripSpaceR, &poseR);
        update_shelf(gripL, gripR, haveL, haveR, handL, handR, fullL ? &poseL : nullptr, fullR ? &poseR : nullptr);
      }
      if (gripR && !g.prevGripR) {
        g.table.rightGrab = g.tabletop && !g.stereoFailed && haveR && !g.shelf.handR && near_table(handR);
      }
      if (!gripR) g.table.rightGrab = false;
      // r42: the Jumbotron screen (the big screen modes, and Tabletop when 3D failed).
      const bool screenMode = !g.tabletop || g.stereoFailed;
      if (screenMode) {
        update_screen(gripL && !g.shelf.handL, gripR && !g.shelf.handR, haveL, haveR, handL, handR);
        // The left grip is busy with the screen: the right grip is not Z until it is let go.
        if (g.screens[screen_room()].grabHands != 0) g.holdRightGrip = true;
      } else {
        g.prevGripL = gripL;
        g.screens[screen_room()].grabHands = 0;
      }
      if (g.shelf.handR) g.holdRightGrip = true;  // r57: carrying the shelf is no item swap
      if (!gripR) g.holdRightGrip = false;
      // r46: Z from the right grip only while it is mapped there; it still grabs the table either way.
      if (g.controls[kActZ] == kSrcGripR && gripR && !g.table.rightGrab && !g.holdRightGrip) {
        status.button |= PAD_TRIGGER_Z;
      }
      g.prevGripR = gripR;
      const bool holdL = g.tabletop && get_float(a.squeezeL) > 0.6f && !g.shelf.handL;  // r57: not while it carries the shelf
      const int hands = (holdL && haveL ? 1 : 0) | (g.table.rightGrab && haveR ? 2 : 0);
      if (hands != g.table.grabHands) {
        // r44: taken hold of while it is raised for the scorer: it stays up there, in your hand.
        if (hands != 0 && g.tableLift > 0.0f) {
          g.table.position.y += g.tableLift;
          g.tableLift = 0.0f;
          g.tableLiftOff = true;
        }
        // Taking hold or letting go with one hand: fresh baselines, so the field never jumps.
        if (hands == 3) {
          const float dx = handR.x - handL.x, dy = handR.y - handL.y, dz = handR.z - handL.z;
          g.table.twoDist = std::max(0.05f, std::sqrt(dx * dx + dy * dy + dz * dz));
          g.table.prevAngle = std::atan2(dz, dx);
          g.table.turn = 0.0f;
          g.table.twoScale = g.table.scale;
          g.table.twoYaw = g.table.yaw;
          g.table.twoPos = g.table.position;
          g.table.twoMid = {0.5f * (handL.x + handR.x), 0.5f * (handL.y + handR.y), 0.5f * (handL.z + handR.z)};
        } else if (hands != 0) {
          const XrVector3f& p = hands == 1 ? handL : handR;
          g.table.grabOffset = {g.table.position.x - p.x, g.table.position.y - p.y, g.table.position.z - p.z};
        } else {
          XLOG("table at (%.2f, %.2f, %.2f) yaw %.2f scale %.4f", g.table.position.x, g.table.position.y,
               g.table.position.z, g.table.yaw, g.table.scale);
        }
        g.table.grabHands = hands;
      }
      if (hands == 3) {
        const float dx = handR.x - handL.x, dy = handR.y - handL.y, dz = handR.z - handL.z;
        const float dist = std::max(0.05f, std::sqrt(dx * dx + dy * dy + dz * dz));
        float step = std::atan2(dz, dx) - g.table.prevAngle;
        if (step > 3.14159265f) step -= 6.2831853f;
        if (step < -3.14159265f) step += 6.2831853f;
        g.table.prevAngle += step;
        g.table.turn += step;
        g.table.scale = std::max(0.008f, std::min(0.25f, g.table.twoScale * dist / g.table.twoDist));
        const float k = g.table.scale / g.table.twoScale;
        // The room's angle of the field's axes is -yaw, so turning the hands one way turns it with them.
        g.table.yaw = g.table.twoYaw - g.table.turn;
        // Around the hands' midpoint: the field keeps its place relative to your hands.
        const XrVector3f mid{0.5f * (handL.x + handR.x), 0.5f * (handL.y + handR.y), 0.5f * (handL.z + handR.z)};
        const float vx = g.table.twoPos.x - g.table.twoMid.x, vz = g.table.twoPos.z - g.table.twoMid.z;
        const float cs = std::cos(g.table.turn), sn = std::sin(g.table.turn);
        g.table.position = {mid.x + (vx * cs - vz * sn) * k, mid.y + (g.table.twoPos.y - g.table.twoMid.y) * k,
                            mid.z + (vx * sn + vz * cs) * k};
      } else if (hands != 0) {
        const XrVector3f& p = hands == 1 ? handL : handR;
        g.table.position = {p.x + g.table.grabOffset.x, p.y + g.table.grabOffset.y, p.z + g.table.grabOffset.z};
      }
      if (holdL) {
        if (hands == 1 && std::fabs(stickL.y) > 0.2f) {
          g.table.scale = std::max(0.008f, std::min(0.25f, g.table.scale * std::exp(stickL.y * 0.025f)));
        }
        if (hands == 1 && std::fabs(stickL.x) > 0.2f) {
          g.table.yaw -= stickL.x * 0.035f;
        }
        stickL = XrVector2f{0.0f, 0.0f};
      }
      // For the item check: which of the item buttons arrive (X use, Z swap, Y hit).
      const uint32_t itemButtons = status.button & (PAD_BUTTON_X | PAD_BUTTON_Y | PAD_TRIGGER_Z);
      if ((itemButtons & ~g.prevItemButtons) != 0) {
        XLOG("touch: %s%s%s", (itemButtons & ~g.prevItemButtons & PAD_BUTTON_X) ? "X (use item) " : "",
             (itemButtons & ~g.prevItemButtons & PAD_TRIGGER_Z) ? "Z (swap item) " : "",
             (itemButtons & ~g.prevItemButtons & PAD_BUTTON_Y) ? "Y " : "");
      }
      g.prevItemButtons = itemButtons;
      // Self-test (xr_fx "item"): X pressed for a moment, through this same path.
      const uint64_t tNow = now_ns();
      if (g.testPressXNs != 0 && tNow >= g.testPressXNs) {
        status.button |= PAD_BUTTON_X;
        if (tNow >= g.testPressXNs + 150000000ull) {
          g.testPressXNs = 0;
          XLOG("item test: X released");
        }
      }
      const XrVector2f stickR = get_vec2(a.stickR);
      // r47/r48: with a Bluetooth pad too, each stick is whichever of the two is pushed further, as a
      // whole (r47 took the stronger of each axis, which could mix two directions).
      XrVector2f moveL = stickL, moveR = stickR;
      float dzL = kTouchDeadzone, dzR = kTouchDeadzone;
      if (havePad) {
        auto push = [](XrVector2f v, float dz) { return std::sqrt(v.x * v.x + v.y * v.y) - dz; };
        const XrVector2f padL{phys.stickX / 127.0f, phys.stickY / 127.0f};
        const XrVector2f padR{phys.substickX / 127.0f, phys.substickY / 127.0f};
        if (push(padL, kPadDeadzone) > push(moveL, kTouchDeadzone)) {
          moveL = padL;
          dzL = kPadDeadzone;
        }
        if (push(padR, kPadDeadzone) > push(moveR, kTouchDeadzone)) {
          moveR = padR;
          dzR = kPadDeadzone;
        }
      }
      apply_stick(moveL, dzL, kGameStickRadius, &status.stickX, &status.stickY);
      apply_stick(moveR, dzR, kGameSubstickRadius, &status.substickX, &status.substickY);
      // r51: the achievements list, while it is open: the left stick (or the pad's D-pad) scrolls it a
      // row a push, held: on and on; B closes it. Neither those nor A reach the game's pause menu,
      // which would otherwise move its own pick, or take one, underneath the list.
      if (g.achOpen && !g.paused) g.achOpen = false;
      if (g.achOpen) {
        // r51b: the scroll itself runs every display frame, smoothly (PortXrEndFrame); here only the
        // stick (or the pad's D-pad) is taken, up = toward the top of the list.
        g.achStickY = moveL.y;
        if ((status.button & PAD_BUTTON_UP) != 0) g.achStickY = 1.0f;
        if ((status.button & PAD_BUTTON_DOWN) != 0) g.achStickY = -1.0f;
        const bool b = (status.button & PAD_BUTTON_B) != 0;
        if (b && !g.achPrevB) g.achOpen = false;
        g.achPrevB = b;
        status.stickX = status.stickY = 0;
        status.button &= static_cast<u16>(~(PAD_BUTTON_A | PAD_BUTTON_B | PAD_BUTTON_UP | PAD_BUTTON_DOWN |
                                            PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT));
      } else {
        g.achStickY = 0.0f;
        // B still held from closing the list: kept from the game until it is let go.
        if (g.achPrevB) {
          if ((status.button & PAD_BUTTON_B) != 0) status.button &= static_cast<u16>(~PAD_BUTTON_B);
          else g.achPrevB = false;
        }
      }
      // r67: the guest picker over the game's own pick screens (include/port/xr_guest.h). Y opens it
      // there; while it is open nothing reaches the game, and the A or B that closed it is kept from
      // the game until it is let go (the screen confirms the guest's slot on its own).
      {
        int offSide = -1, offRole = -1;
        g.gpOffered = !g.paused && PortGuestPickerOffered(&offSide, &offRole);
        if (g.gpOpen && (!g.gpOffered || offSide != g.gpSide || offRole != g.gpRole)) g.gpOpen = false;
        // r68e: the character select menu opens by itself each time a side starts choosing (once:
        // B closes it to the game's own screen, Y brings it back).
        const int offerKey = g.gpOffered ? offSide * 2 + offRole : -1;
        if (offerKey < 0) g.gpAutoDone = -1;
        const bool yDown = (status.button & PAD_BUTTON_Y) != 0;
        const bool autoOpen = g.gpOffered && !g.gpOpen && offerKey != g.gpAutoDone;
        if (g.gpOffered && !g.gpOpen && ((yDown && !g.gpPrevY) || autoOpen)) {
          g.gpAutoDone = offerKey;
          g.gpOpen = true;
          g.gpSide = offSide;
          g.gpRole = offRole;
          g.gpCursor = 0;
          g.gpDir = 0;
          g.gpPlaced = false;
          // an A or B already down (the press that brought this screen up) is not a choice here
          g.gpHoldA = (status.button & PAD_BUTTON_A) != 0;
          g.gpHoldB = (status.button & PAD_BUTTON_B) != 0;
          g.gpOpenNs = now_ns();  // r68g: and nothing picks in its first 0.4 s (Lance's "auto" Mario)
          // r79: a Grudge Match's sidekick pick starts with the side's co-captain (on the one it has now)
          g.gpCo = offRole == 1 && PortGuestCoCaptainOffered(offSide);
          g.gpPrevX = (status.button & PAD_BUTTON_X) != 0;
          if (g.gpCo) {
            const int now = PortGuestCoCaptainCurrent(offSide);
            const std::vector<MenuEntry>& captains = menu_entries(0);
            for (int i = 0; i < static_cast<int>(captains.size()); ++i)
              if (captains[i].pick == now) g.gpCursor = i;
          }
          ++g.gpGen;
          XLOG("character select: %s for side %d's %s (%d to pick from)", autoOpen ? "opens" : "Y opens", offSide,
               offRole ? "sidekick" : "captain", static_cast<int>(menu_entries(offRole).size()));
        }
        g.gpPrevY = yDown;
        if (g.gpOffered) status.button &= static_cast<u16>(~PAD_BUTTON_Y);
        // r75: whether A / B are really held, read before the open menu clears the buttons below: the
        // held-until-let-go checks at the end looked at the cleared state, so the press that picked was
        // "let go" at once and reached the game's own screen the next frame (a second, unasked press)
        const bool gpRawA = (status.button & PAD_BUTTON_A) != 0, gpRawB = (status.button & PAD_BUTTON_B) != 0;
        if (g.gpOpen) {
          const std::vector<MenuEntry>& menu = menu_entries(g.gpCo ? 0 : g.gpRole);
          if (g.gpCo) {
            // r79: the Strikers captains it can't be (a team's captain, the other side's co-captain)
            unsigned coTaken = 0;
            for (int t = 0; t < 8; ++t)
              if (!PortGuestCoCaptainFree(g.gpSide, t)) coTaken |= 1u << t;
            if (coTaken != g.gpCoTaken) {
              g.gpCoTaken = coTaken;
              ++g.gpGen;
            }
          }
          int dir = 0;
          if (moveL.x < -0.6f || (status.button & PAD_BUTTON_LEFT) != 0) dir = 1;
          else if (moveL.x > 0.6f || (status.button & PAD_BUTTON_RIGHT) != 0) dir = 2;
          else if (moveL.y > 0.6f || (status.button & PAD_BUTTON_UP) != 0) dir = 3;
          else if (moveL.y < -0.6f || (status.button & PAD_BUTTON_DOWN) != 0) dir = 4;
          const uint64_t nowGp = now_ns();
          bool step = false;
          if (dir != 0 && dir != g.gpDir) {
            step = true;
            g.gpRepeatNs = nowGp + 350000000ull;
          } else if (dir != 0 && nowGp >= g.gpRepeatNs) {
            step = true;
            g.gpRepeatNs = nowGp + 130000000ull;
          }
          g.gpDir = dir;
          const int oldCursor = g.gpCursor;
          if (step) g.gpCursor = menu_step(menu, g.gpCursor, dir);
          if (g.gpCursor != oldCursor) ++g.gpGen;
          const bool aDown = (status.button & PAD_BUTTON_A) != 0, bDown = (status.button & PAD_BUTTON_B) != 0;
          const bool xDown = (status.button & PAD_BUTTON_X) != 0;
          if (g.gpCo && xDown && !g.gpPrevX) {
            // r79: no co-captain; on to the sidekicks
            PortGuestCoCaptainPick(g.gpSide, -1);
            XLOG("character select: side %d has no co-captain", g.gpSide);
            g.gpCo = false;
            g.gpCursor = 0;
            ++g.gpGen;
          } else if (aDown && !g.gpHoldA && now_ns() - g.gpOpenNs < 400000000ull) {
            g.gpHoldA = true;  // too soon after it opened: not a pick, and kept until let go
          } else if (aDown && !g.gpHoldA && g.gpCo) {
            // r79: the co-captain (a captain no team has: a taken one or a locked form does nothing), then
            // the menu goes on to the sidekicks
            if (g.gpCursor >= 0 && g.gpCursor < static_cast<int>(menu.size())) {
              const MenuEntry& e = menu[g.gpCursor];
              if (PortGuestCoCaptainPick(g.gpSide, e.pick)) {
                XLOG("character select: side %d's co-captain is %s", g.gpSide, e.name.c_str());
                g.gpCo = false;
                g.gpCursor = 0;
                ++g.gpGen;
              }
            }
            g.gpHoldA = true;
          } else if (aDown && !g.gpHoldA) {
            if (g.gpCursor >= 0 && g.gpCursor < static_cast<int>(menu.size())) {
              const MenuEntry& e = menu[g.gpCursor];
              // a Strikers captain the other side has is no choice; r70c: nor a locked evolved Pokemon
              if ((e.team < 0 || PortGuestPickerCaptainFree(e.team)) && !PortGuestRosterLocked(e.pick)) {
                PortGuestPickerCommit(g.gpSide, g.gpRole, e.pick);
                XLOG("character select: side %d picks %s", g.gpSide, e.name.c_str());
                g.gpOpen = false;
              }
            }
            g.gpHoldA = true;
          } else if (bDown && !g.gpHoldB) {
            g.gpOpen = false;
            g.gpHoldB = true;
          }
          g.gpPrevX = xDown;
          status.stickX = status.stickY = status.substickX = status.substickY = 0;
          status.triggerLeft = status.triggerRight = 0;
          status.button &= static_cast<u16>(PAD_BUTTON_START);
        }
        if (g.gpHoldA) {
          if (gpRawA) status.button &= static_cast<u16>(~PAD_BUTTON_A);
          else g.gpHoldA = false;
        }
        if (g.gpHoldB) {
          if (gpRawB) status.button &= static_cast<u16>(~PAD_BUTTON_B);
          else g.gpHoldB = false;
        }
      }
      // r70: franchise mode's hub (include/port/franchise.h): Y on the game's main menu opens it. While it
      // is open nothing reaches the game; the A or B that acted is kept from the game until let go.
      {
        const bool onMenu = PortFranchiseOnMainMenu() && !g.paused && !g.gpOffered && !g.achOpen;
        if (onMenu != g.frOnMenu) ++g.frGen;
        g.frOnMenu = onMenu;
        if (g.frOpen && !onMenu) g.frOpen = false;
        if (PortFranchiseMatchActive()) g.frAfterMatch = true;
        {
          // r70b: the match on now: its intro card for 10 s from its intro, then the series board
          PortFranchiseCard card;
          PortFranchiseMatchCard(&card);
          const int key = card.valid ? card.serial * 64 + card.gameNo * 8 + card.seriesWins[0] * 3 + card.seriesWins[1] : -1;
          if (card.valid && card.serial != g.frCardSerial) {
            g.frCardSerial = card.serial;
            g.frIntroUntilNs = now_ns() + 10000000000ull;
            g.frIntroPlaced = false;
          }
          if (key != g.frCardKey) {
            g.frCardKey = key;
            {
              std::lock_guard<std::mutex> lock(g_frMutex);
              g_frCard = card;
            }
            ++g.frCardGen;
          }
          // r77: the live XP meter's numbers, when they change (a goal, an assist, the lead)
          {
            PortFranchiseLive live;
            const bool liveOn = PortFranchiseLiveXp(&live);
            const long long liveKey =
                liveOn ? (static_cast<long long>(live.gain) * 1000003LL + live.skGain * 7919LL + live.goals * 101LL +
                          live.assists * 11LL + (live.won ? 1 : 0) +
                          (live.co ? (live.coGain * 31LL + live.coGoals * 3LL + live.coAssists + 1) * 1000000007LL : 0))
                       : -1;
            // r87: a row whose level (as the match stands) goes up lights up for xrtext::kLevelGlowS
            const uint64_t tNow = now_ns();
            const int levels[3] = {liveOn ? live.newLevel : -1, liveOn && live.co ? live.coNewLevel : -1,
                                   liveOn ? live.skNewLevel : -1};
            FrLiveGlow glow;
            bool lit = false;
            for (int r = 0; r < 3; ++r) {
              if (levels[r] < 0) {
                g.frLiveShown[r] = -1;
                g.frLiveGlowNs[r] = 0;
                continue;
              }
              if (g.frLiveShown[r] >= 0 && levels[r] > g.frLiveShown[r]) {
                g.frLiveGlowNs[r] = tNow;
                g.frLiveFrom[r] = g.frLiveShown[r];
              }
              g.frLiveShown[r] = levels[r];
              if (g.frLiveGlowNs[r] == 0) continue;
              const float t = static_cast<float>(static_cast<double>(tNow - g.frLiveGlowNs[r]) * 1e-9);
              if (t >= xrtext::kLevelGlowS) {
                g.frLiveGlowNs[r] = 0;
                continue;
              }
              glow.t[r] = t;
              glow.from[r] = g.frLiveFrom[r];
              lit = true;
            }
            // redrawn when the numbers change, ~15 times a second while lit, and once as the light goes
            const bool redraw = liveKey != g.frLiveKey || (lit && tNow >= g.frLiveTickNs) || (g.frLiveLit && !lit);
            g.frLiveLit = lit;
            if (redraw) {
              g.frLiveKey = liveKey;
              g.frLiveTickNs = tNow + 66000000ull;
              {
                std::lock_guard<std::mutex> lock(g_frMutex);
                g_frLiveData = live;
                g_frLiveGlow = glow;
              }
              ++g.frLiveGen;
            }
          }
          unsigned b1 = 0, b2 = 0;
          bool champ = false;
          g_frBuntingOn = PortFranchiseBunting(&b1, &b2, &champ);
          g_frBunting1 = b1;
          g_frBunting2 = b2;
          g_frChampion = champ;
        }
        // r73: the league in your room: what the news wire and the TV show, every 2 s
        if (now_ns() >= g.frRoomNextNs) {
          g.frRoomNextNs = now_ns() + 2000000000ull;
          const bool on = PortFranchiseActive() && !PortFranchiseMatchActive();
          FrRoom r;
          std::string key = on ? "on" : "off";
          if (on) {
            PortFranchiseGetView(&r.view);
            const PortFranchiseView& v = r.view;
            if (v.headline[0] != '\0') r.lines.push_back(v.headline);
            for (int i = 0; i < v.resultCount; ++i) r.lines.push_back(std::string("FINAL: ") + v.results[i]);
            for (int i = 0; i < v.newsCount; ++i) r.lines.push_back(v.news[i]);
            for (int i = 0; i < v.feedCount && i < 3; ++i) r.lines.push_back(v.feed[i]);
            if (v.leaderCount > 0)
              r.lines.push_back(std::string("GOLDEN BOOT RACE: ") + v.leaders[0].name + ", " + std::to_string(v.leaders[0].goals) +
                                " GOALS");
            const int n = PortFranchiseClubCount();
            for (int i = 0; i < n; ++i) {
              r.clubIds.push_back(PortFranchiseClubId(i));
              r.clubPlaces.push_back(PortFranchiseClubPlace(i));
            }
            key += std::to_string(v.season) + "|" + std::to_string(v.week) + "|" + std::to_string(v.phase);
            for (const std::string& l : r.lines) key += "|" + l;
          }
          g.frRoomOn = on && !r.lines.empty();
          if (key != g.frRoomKey) {
            g.frRoomKey = key;
            std::lock_guard<std::mutex> lock(g_frMutex);
            g_frRoom = std::move(r);
            ++g.frRoomGen;
          }
        }
        const bool yDown = (status.button & PAD_BUTTON_Y) != 0;
        const bool backFromMatch = onMenu && g.frAfterMatch && PortFranchiseActive();
        if (onMenu) g.frAfterMatch = false;
        if (onMenu && !g.frOpen && !PortFranchiseMatchStarting() && ((yDown && !g.frPrevY) || backFromMatch)) {
          g.frOpen = true;
          g.frMode = PortFranchiseActive() ? 2 : 0;
          g.frCursor = 0;
          g.frPage = 0;
          g.frDir = 0;
          g.frPlaced = false;
          g.frConfirmDelete = false;
          g.frHoldA = (status.button & PAD_BUTTON_A) != 0;
          g.frHoldB = (status.button & PAD_BUTTON_B) != 0;
          g.frHubPrevY = true;  // r77: the Y that opened it isn't a trades press
          g.frOpenNs = now_ns();
          fr_refresh();
          XLOG("franchise: the hub opens (%s)", g.frMode == 2 ? "a career loaded" : "the careers");
        }
        g.frPrevY = yDown;
        if (onMenu) status.button &= static_cast<u16>(~PAD_BUTTON_Y);
        const bool xDown = (status.button & PAD_BUTTON_X) != 0;
        // r75 (Lance: "do not automatically start a game when i select my team"): A / B as really held,
        // read before the open hub clears the buttons below. The held-until-let-go checks at the end
        // used the cleared state, so the A that chose the club counted as let go at once and, still held,
        // was a new press the next frame: "PLAY" on the hub it had just opened (his week 1 match).
        const bool frRawA = (status.button & PAD_BUTTON_A) != 0, frRawB = (status.button & PAD_BUTTON_B) != 0;
        if (g.frOpen) {
          int dir = 0;
          if (moveL.x < -0.6f || (status.button & PAD_BUTTON_LEFT) != 0) dir = 1;
          else if (moveL.x > 0.6f || (status.button & PAD_BUTTON_RIGHT) != 0) dir = 2;
          else if (moveL.y > 0.6f || (status.button & PAD_BUTTON_UP) != 0) dir = 3;
          else if (moveL.y < -0.6f || (status.button & PAD_BUTTON_DOWN) != 0) dir = 4;
          const uint64_t nowFr = now_ns();
          bool step = false;
          if (dir != 0 && dir != g.frDir) {
            step = true;
            g.frRepeatNs = nowFr + 350000000ull;
          } else if (dir != 0 && nowFr >= g.frRepeatNs) {
            step = true;
            g.frRepeatNs = nowFr + 140000000ull;
          }
          g.frDir = dir;
          const bool aDown = (status.button & PAD_BUTTON_A) != 0, bDown = (status.button & PAD_BUTTON_B) != 0;
          const bool settled = nowFr - g.frOpenNs > 300000000ull;
          const bool aPress = aDown && !g.frHoldA && settled, bPress = bDown && !g.frHoldB;
          const bool xPress = xDown && !g.frPrevX;
          const bool yPress = yDown && !g.frHubPrevY && settled;  // r77: trades
          g.frHubPrevY = yDown;
          if (aDown) g.frHoldA = true;
          if (bDown) g.frHoldB = true;
          bool changed = step || aPress || bPress || xPress || yPress;
          if (g.frMode == 0) {  // the career slots
            // the three careers side by side: left / right (up / down too); r76: and the co-op one; r100: and the
            // online league's, last
            std::vector<int> order = {0, 1, 2};
            if (g_frSnap.coopShown) order.push_back(PORT_FR_COOP_SLOT);
            order.push_back(PORT_FR_LEAGUE_SLOT);
            int at = 0;
            for (size_t i = 0; i < order.size(); ++i)
              if (order[i] == g.frCursor) at = static_cast<int>(i);
            if (step && (dir == 1 || dir == 3)) at = std::max(0, at - 1);
            if (step && (dir == 2 || dir == 4)) at = std::min(static_cast<int>(order.size()) - 1, at + 1);
            g.frCursor = order[at];
            if (step) g.frConfirmDelete = false;
            if (aPress && g.frCursor == PORT_FR_LEAGUE_SLOT) {
              // r100: the league this headset is in, or make / join one
              PortFranchiseSlot ls;
              if (PortFranchiseLeagueFile(&ls, nullptr, 0)) {
                if (PortFranchiseLoad(PORT_FR_LEAGUE_SLOT)) {
                  g.frMode = 2;
                  g.frPage = 0;
                  g.frCursor = 0;
                  g.frOpenNs = nowFr;
                  XLOG("franchise: the online league opens");
                }
              } else {
                g.frMode = 6;
                g.frLgStage = 0;
                g.frCursor = 0;
                XLOG("franchise: online league: make or join");
              }
            } else if (xPress && g.frCursor == PORT_FR_LEAGUE_SLOT) {
              if (g.frConfirmDelete && nowFr - g.frConfirmNs < 3000000000ull) {
                PortFranchiseLeagueLeave();
                XLOG("franchise: left the online league");
                g.frConfirmDelete = false;
              } else if (g_frSnap.leagueSaved) {
                g.frConfirmDelete = true;
                g.frConfirmNs = nowFr;
              }
            } else if (aPress && g.frCursor == PORT_FR_COOP_SLOT) {
              // r76: open the co-op career (this headset runs it), join the friend's, or start a new one
              const int r = PortFranchiseCoopOpen();
              if (r == 1) {
                g.frMode = 2;
                g.frPage = 0;
                g.frCursor = 0;
                g.frOpenNs = nowFr;
                XLOG("franchise: the co-op career opens (%s)", PortFranchiseCoopGuest() ? "the friend runs it" : "run here");
              } else if (r == 2) {
                g.frSlot = PORT_FR_COOP_SLOT;
                g.frMode = 1;
                g.frCursor = 0;
                XLOG("franchise: a new co-op career: choosing a club");
              }
            } else if (aPress) {
              PortFranchiseSlot sl[PORT_FR_SLOTS];
              PortFranchiseSlots(sl);
              if (sl[g.frCursor].used) {
                if (PortFranchiseLoad(g.frCursor)) {
                  g.frMode = 2;
                  g.frPage = 0;
                  XLOG("franchise: career %d continues", g.frCursor + 1);
                }
              } else {
                g.frSlot = g.frCursor;
                g.frMode = 1;
                g.frCursor = 0;
                XLOG("franchise: a new career in slot %d: choosing a club", g.frSlot + 1);
              }
            } else if (xPress) {
              if (g.frConfirmDelete && nowFr - g.frConfirmNs < 3000000000ull) {
                PortFranchiseDelete(g.frCursor);
                XLOG("franchise: career %d deleted", g.frCursor + 1);
                g.frConfirmDelete = false;
              } else {
                g.frConfirmDelete = true;
                g.frConfirmNs = nowFr;
              }
            } else if (bPress) {
              g.frOpen = false;
            }
          } else if (g.frMode == 1) {  // a new career's club
            if (step) {
              int r = 0, c = 0;
              fr_club_place(g.frCursor, &r, &c);
              if (dir == 1) c = std::max(0, c - 1);
              if (dir == 2) c = std::min(7, c + 1);
              if (dir == 3) r = std::max(0, r - 1);
              if (dir == 4) r = std::min(3, r + 1);
              g.frCursor = fr_club_at(r, c);
            }
            if (aPress) {
              // r75 (Lance: "my club only"): then your own players on the character select menu
              g.frPickClub = g.frCursor;
              g.frMode = 4;
              g.frPickStage = 0;
              g.frPickCap = g.frPickBk = -1;
              g.frPickCursor[0] = g.frPickCursor[1] = g.frPickCursor[2] = 0;
              g.frCursor = 0;
              XLOG("franchise: career %d with %s: picking your players", g.frSlot + 1, PortFranchiseClubName(g.frPickClub));
            } else if (bPress) {
              g.frMode = 0;
              g.frCursor = g.frSlot;
            }
          } else if (g.frMode == 4) {  // r75: a new career's own players: captain, backup captain, sidekicks
            const std::vector<MenuEntry>& menu = menu_entries(g.frPickStage == 2 ? 1 : 0);
            if (step) g.frCursor = menu_step(menu, g.frCursor, dir);
            const int pick = g.frCursor >= 0 && g.frCursor < static_cast<int>(menu.size()) ? menu[g.frCursor].pick : -1;
            auto to_stage = [&](int stage) {
              g.frPickCursor[g.frPickStage] = g.frCursor;
              g.frPickStage = stage;
              g.frCursor = g.frPickCursor[stage];
            };
            if (aPress && pick >= 0) {
              if (g.frPickStage == 0 && PortFranchiseCaptainPickable(pick)) {
                g.frPickCap = pick;
                XLOG("franchise: your captain %s", menu[g.frCursor].name.c_str());
                to_stage(1);
              } else if (g.frPickStage == 1 && pick != g.frPickCap && PortFranchiseCaptainPickable(pick)) {
                g.frPickBk = pick;
                XLOG("franchise: your backup captain %s", menu[g.frCursor].name.c_str());
                to_stage(2);
              } else if (g.frPickStage == 2) {
                // The career starts on its hub, nothing played: you start your matches yourself (Lance)
                // r100: an online league's: made on the server too, its code to share
                if (g.frSlot == PORT_FR_LEAGUE_SLOT
                        ? PortFranchiseLeagueCreate(g.frPickClub, g.frPickCap, g.frPickBk, pick, g.frLgMax)
                        : PortFranchiseNewPicked(g.frSlot, g.frPickClub, g.frPickCap, g.frPickBk, pick)) {
                  XLOG("franchise: career %d with %s, sidekicks %s", g.frSlot + 1, PortFranchiseClubName(g.frPickClub),
                       menu[g.frCursor].name.c_str());
                  g.frMode = 2;
                  g.frPage = 0;
                  g.frCursor = 0;
                  g.frOpenNs = nowFr;  // and a moment before A acts on the hub (a fresh press plays)
                }
              }
            } else if (xPress && g.frPickStage == 1) {
              g.frPickBk = -1;  // no backup captain
              XLOG("franchise: no backup captain");
              to_stage(2);
            } else if (bPress) {
              if (g.frPickStage == 0) {
                g.frMode = 1;
                g.frCursor = g.frPickClub;
              } else {
                to_stage(g.frPickStage - 1);
              }
            }
          } else if (g.frMode == 6) {  // r100: make an online league (how many people) or join one (its code)
            if (g.frLgStage == 0) {
              if (step && (dir == 1 || dir == 2)) g.frCursor = dir == 2 ? 1 : 0;
              if (step && g.frCursor == 0 && dir == 3) g.frLgMax = std::min(8, g.frLgMax + 1);
              if (step && g.frCursor == 0 && dir == 4) g.frLgMax = std::max(2, g.frLgMax - 1);
              if (aPress && g.frCursor == 0) {
                g.frSlot = PORT_FR_LEAGUE_SLOT;
                g.frMode = 1;
                g.frCursor = 0;
                XLOG("franchise: a new online league (up to %d people): choosing a club", g.frLgMax);
              } else if (aPress) {
                g.frLgStage = 1;
                g.frLgPos = 0;
              } else if (bPress) {
                g.frMode = 0;
                g.frCursor = PORT_FR_LEAGUE_SLOT;
              }
            } else {
              if (step && dir == 1) g.frLgPos = std::max(0, g.frLgPos - 1);
              if (step && dir == 2) g.frLgPos = std::min(5, g.frLgPos + 1);
              if (step && dir == 3) g.frLgChar[g.frLgPos] = (g.frLgChar[g.frLgPos] + 1) % 32;
              if (step && dir == 4) g.frLgChar[g.frLgPos] = (g.frLgChar[g.frLgPos] + 31) % 32;
              if (aPress) {
                std::string code;
                for (int i = 0; i < 6; ++i) code.push_back(kFrCodeChars[g.frLgChar[i] & 31]);
                PortFranchiseLeagueJoin(code.c_str());
                g.frMode = 7;
                g.frCursor = 0;
                XLOG("franchise: joining online league %s", code.c_str());
              } else if (bPress) {
                g.frLgStage = 0;
              }
            }
          } else if (g.frMode == 7) {  // r100: joining a league: its free clubs
            const bool picking = g_frSnap.lobby.state == 3;
            if (step && picking) {
              int r = 0, c = 0;
              fr_club_place(g.frCursor, &r, &c);
              if (dir == 1) c = std::max(0, c - 1);
              if (dir == 2) c = std::min(7, c + 1);
              if (dir == 3) r = std::max(0, r - 1);
              if (dir == 4) r = std::min(3, r + 1);
              g.frCursor = fr_club_at(r, c);
            }
            if (aPress && picking) {
              if (PortFranchiseLeagueClaim(g.frCursor)) XLOG("franchise: league: claiming %s", PortFranchiseClubName(g.frCursor));
            } else if (bPress) {
              PortFranchiseLeagueJoin("");
              g.frMode = 6;
              g.frLgStage = 1;
              g.frCursor = 1;
            }
          } else if (g.frMode == 3) {  // r70c: one of your Pokemon evolving
            // r95 (Lance: "make an EVO page, where we can witness the EVO happening, as well as confirm or
            // postpone the evo"): stage 0 = the EVO page (a split: choose the form): A evolves it and you
            // watch, B postpones it (the Evo Watch page's A evolves it any time). Once it's going there is
            // no B: you said yes on the page (r70c's B-to-stop is what stopped his Pichu unseen).
            const float t = static_cast<float>(nowFr - g.frEvoStartNs) / 1e9f;
            if (g.frEvoStage == 0) {
              if (step && (dir == 1 || dir == 2) && g.frEvo.options == 2) g.frEvoChoice = dir == 2 ? 1 : 0;
              if (aPress && t > 0.5f) {
                g.frEvoStage = 1;
                g.frEvoStartNs = nowFr;
                g.frEvoFanfare = false;
                PortEvoMusic(0);  // r99: Pokemon Stadium (Melee) while it evolves
                XLOG("franchise: evolving %s into %s (A on the EVO page)", g.frEvo.fromKey, g.frEvo.toKey[g.frEvoChoice]);
              } else if (bPress && t > 0.5f) {
                bool sh = false, ns = false;
                PortFranchiseEvolveSlot(g.frEvo.backup, -1, &sh, &ns);
                g.frEvoStage = 3;
                g.frEvoStartNs = nowFr;
                XLOG("franchise: evolution of %s postponed (B on the EVO page)", g.frEvo.fromKey);
              }
            } else if (g.frEvoStage == 1) {
              if (t >= 8.5f && !g.frEvoFanfare) {  // r99: the flash: the Pokemon victory fanfare
                g.frEvoFanfare = true;
                PortEvoMusic(1);
              }
              if (t >= 8.8f) {  // r96: after the flash (8.5 s), in 3D or on the card
                bool sh = false, ns = false;
                PortFranchiseEvolveSlot(g.frEvo.backup, g.frEvoChoice, &sh, &ns);
                g.frEvoNewShiny = ns;
                g.frEvoStage = 2;
                g.frEvoStartNs = nowFr;
                changed = true;
                XLOG("franchise: %s evolved into %s%s", g.frEvo.fromKey, g.frEvo.toKey[g.frEvoChoice], ns ? " (shiny)" : "");
              }
            } else if ((aPress || bPress) && t > 1.0f) {  // evolved / postponed: back to the hub (or the next one)
              g.frMode = 2;
              g.frCursor = 0;
            }
          } else if (g.frMode == 5) {  // r77: trades
            if (g.frTradeStage == 0) {
              if (step && dir == 3) g.frCursor = std::max(0, g.frCursor - 1);
              if (step && dir == 4) g.frCursor = std::min(2, g.frCursor + 1);
              if (aPress && PortFranchiseTradeOpen()) {
                g.frTradeWhat = g.frCursor;
                g.frTradeStage = 1;
                g.frCursor = 0;
                g.frTradeMsg.clear();
              } else if (bPress || yPress) {
                g.frMode = 2;
                g.frCursor = 0;
              }
            } else {
              const int count = static_cast<int>(g_frSnap.offers.size());
              if (step && dir == 3) g.frCursor = std::max(0, g.frCursor - 1);
              if (step && dir == 4) g.frCursor = std::max(0, std::min(count - 1, g.frCursor + 1));
              if (aPress && g.frCursor < count) {
                const PortFranchiseTradeOffer o = g_frSnap.offers[g.frCursor];
                if (PortFranchiseTrade(g.frTradeWhat, o.club, o.backup)) {
                  g.frTradeMsg = std::string("DONE: ") + o.name + " IS YOURS";
                  XLOG("franchise: trade done, got %s", o.name);
                  g.frTradeStage = 0;
                  g.frCursor = g.frTradeWhat;
                } else {
                  g.frTradeMsg = std::string("THEY SAID NO: ") + o.name + " IS WORTH MORE TO THEM";
                }
              } else if (bPress) {
                g.frTradeStage = 0;
                g.frCursor = g.frTradeWhat;
              }
            }
          } else {  // the hub: the club, the standings, the playoffs, last week's results
            if (yPress && !PortFranchiseCoopGuest()) {  // r77: Y = trades (the friend's career: theirs to trade)
              g.frMode = 5;
              g.frTradeStage = 0;
              g.frCursor = 0;
              g.frTradeMsg.clear();
              XLOG("franchise: trades");
            }
            const PortFranchiseLeagueView& lgv = g_frSnap.league;  // r100: an online league's
            const int pageCount = lgv.active ? 11 : 10;
            if (g.frPage >= pageCount) g.frPage = 0;
            if (step && (dir == 1 || dir == 2)) {
              // r71/r72: 8 pages; r77: 9 (the Evo Watch); r97: 10 (the difficulty); r100: 11 in a league (it)
              g.frPage = (g.frPage + (dir == 2 ? 1 : pageCount - 1)) % pageCount;
              g.frCursor = g.frPage == 9 ? std::max(0, std::min(3, PortFranchiseDifficulty() - 1)) : 0;
            }
            // r70b: no match of yours today: up / down picks "play out the day" or one of today's games
            // to watch (CPU v CPU)
            // with a match of yours: play it (0), work it out (1), or every week to the playoffs (2)
            // r100: a league's: against a person only ready (0); against the CPU play or work it out (0-1)
            const bool hasMatch = g_frSnap.view.nextOpponent >= 0;
            const bool vsPerson = lgv.active && lgv.vsMember >= 0;
            const int lastOption = g.frPage == 9 ? 3  // r97: the four difficulties
                                   : g.frPage != 0 ? 0
                                   : vsPerson ? 0
                                   : hasMatch ? (g_frSnap.view.phase == PORT_FR_PHASE_REGULAR && !lgv.active ? 2 : 1)
                                              : (g_frSnap.view.canSimDay ? g_frSnap.today.count : 0);
            if (step && dir == 3) g.frCursor = std::max(0, g.frCursor - 1);
            if (step && dir == 4) g.frCursor = std::min(lastOption, g.frCursor + 1);
            if (g.frCursor > lastOption) g.frCursor = lastOption;
            if (aPress && g.frPage == 0 && vsPerson && hasMatch) {
              // r100: ready (or not) for the game against a person's club: both ready, it starts on both
              PortFranchiseLeagueView now;
              PortFranchiseLeagueGetView(&now);
              if (PortFranchiseLeagueSetReady(!now.meReady))
                XLOG("franchise: league: %s", now.meReady ? "not ready" : "ready for the online game");
            } else if (aPress && g.frPage == 10 && lgv.active) {
              if (PortFranchiseLeagueSimRest()) XLOG("franchise: league: the owner simmed the rest of the week");
            } else if ((aPress || xPress) && g.frPage == 0) {
              PortFranchiseView v;
              PortFranchiseGetView(&v);
              if (aPress && v.nextOpponent >= 0 && g.frCursor > 0) {
                PortFranchiseSimMine(g.frCursor == 2);
                g.frCursor = 0;
              } else if (aPress && PortFranchisePlayNext()) {
                XLOG("franchise: playing week %d", v.week);
                g.frOpen = false;
              } else if (aPress && v.canSimDay && g.frCursor > 0) {
                if (PortFranchiseWatch(g.frCursor - 1)) {
                  XLOG("franchise: watching game %d of the day", g.frCursor);
                  g.frOpen = false;
                }
              } else if (v.canSimDay) {
                // r70b: no match of yours today: A plays the day out, X every day until you play again
                PortFranchiseSimDay(xPress);
                g.frCursor = 0;
              } else if (aPress && v.phase == PORT_FR_PHASE_DONE) {
                PortFranchiseNextSeason();
              } else if (xPress) {
                PortFranchiseSwapStarter();
              }
            } else if (aPress && g.frPage == 9) {
              // r97: A on the difficulty page sets it
              if (PortFranchiseSetDifficulty(g.frCursor + 1)) XLOG("franchise: difficulty %d", g.frCursor + 1);
            } else if (aPress && g.frPage == 8) {
              // r95: A on the Evo Watch page: the EVO page for one of yours that's ready (postponed ones too)
              PortFranchiseEvolution e;
              if (PortFranchiseReadyEvolution(&e)) {
                g.frEvo = e;
                g.frMode = 3;
                g.frEvoStage = 0;
                g.frEvoChoice = 0;
                g.frEvoNewShiny = false;
                g.frEvoStartNs = nowFr;
                XLOG("franchise: the EVO page for %s (Lv %d, from the Evo Watch)", e.fromKey, e.level);
              }
            } else if (bPress) {
              PortFranchiseView v;
              PortFranchiseGetView(&v);
              g.frMode = 0;
              g.frCursor = v.active ? v.slot : 0;
            }
          }
          if (changed && g.frOpen) fr_refresh();
          // r76, co-op: the career this headset waited for came in (the hub opens on it), the loaded one
          // changed from the friend's side, or the co-op card's state moved (the friend came, opened it)
          if (g.frOpen && PortFranchiseCoopTakeOpened()) {
            g.frMode = 2;
            g.frPage = 0;
            g.frCursor = 0;
            g.frOpenNs = nowFr;
            XLOG("franchise: the friend's co-op career came in: open (%s)",
                 PortFranchiseCoopGuest() ? "the friend runs it" : "run here");
            fr_refresh();
          }
          if (g.frOpen && PortFranchiseLeagueTakeOpened()) {  // r100: a league joined: its hub
            g.frMode = 2;
            g.frPage = 0;
            g.frCursor = 0;
            g.frOpenNs = nowFr;
            XLOG("franchise: the online league joined: open");
            fr_refresh();
          }
          if (g.frOpen && PortFranchiseChangeSerial() != g.frChangeSerial) {
            g.frChangeSerial = PortFranchiseChangeSerial();
            fr_refresh();
          }
          {
            int has = 0, inHub = 0;
            unsigned id = 0, version = 0;
            const int known = PortNetCoopFriendCareer(&has, &id, &version, &inHub);
            const long long key = PortFranchiseCoopState() * 1000003LL + known * 11 + has * 5 + inHub * 2 +
                                  static_cast<long long>(version) * 37 + id;
            if (g.frOpen && key != g.frCoopKey) {
              g.frCoopKey = key;
              fr_refresh();
            }
          }
          if (g.frOpen && PortFranchiseMatchStarting()) g.frOpen = false;  // (the friend started the co-op match)
          // r70c: one of your Pokemon ready to evolve: the hub shows it first
          if (g.frOpen && g.frMode == 2 && g_frSnap.view.evolutionPending && PortFranchisePendingEvolution(&g.frEvo)) {
            g.frMode = 3;
            g.frEvoStage = 0;  // r95: the EVO page first (A: evolve and watch, B: not now)
            g.frEvoChoice = 0;
            g.frEvoNewShiny = false;
            g.frEvoStartNs = now_ns();
            XLOG("franchise: %s is evolving (Lv %d)", g.frEvo.fromKey, g.frEvo.level);
            fr_refresh();
          }
          status.stickX = status.stickY = status.substickX = status.substickY = 0;
          status.triggerLeft = status.triggerRight = 0;
          status.button &= static_cast<u16>(PAD_BUTTON_START);
        }
        g.frPrevX = xDown;
        if (g.frHoldA) {
          if (frRawA) status.button &= static_cast<u16>(~PAD_BUTTON_A);
          else g.frHoldA = false;
        }
        if (g.frHoldB) {
          if (frRawB) status.button &= static_cast<u16>(~PAD_BUTTON_B);
          else g.frHoldB = false;
        }
      }
      // r42/r44/r44g: while paused, the settings panel (r51: not while the achievements list covers it).
      settings_input(g.achOpen ? XrVector2f{0.0f, 0.0f} : stickR);
      update_crowd_mix();

      // Stick clicks, the two buttons a GameCube pad never had. Double taps switch modes, so a
      // stray click mid-play does nothing:
      //   left  double tap: Living Room <-> Stadium (MR <-> VR)
      //   right double tap: Jumbotron <-> Tabletop
      //   right single tap: next screen size (Jumbotron)
      // A single tap is only acted on once the double-tap window has passed without a second.
      constexpr uint64_t kDoubleTapNs = 350000000ull;
      const uint64_t t = now_ns();
      const bool clickL = get_bool(a.stickClickL);
      const bool clickR = get_bool(a.stickClickR);
      if (clickL && !g.prevStickClickL) {
        if (g.pendingTapL != 0 && t - g.pendingTapL < kDoubleTapNs) {
          g.pendingTapL = 0;
          if (g.inSuite) {  // r61: your own choice ends the cup suite until the cup's hub is left
            g.inSuite = false;
            g.suiteDismissed = true;
          }
          set_mr_mode(!g.mrMode);
        } else {
          g.pendingTapL = t;
        }
      }
      // r49 (Lance: "one click should do nothing at all, keep the grips as the only thing that moves
      // that"): a single left click is nothing. r24-r48 brought the table (or the big screen) in front
      // of you, and a hard push on the stick mid-play clicked it, so the field and its screen jumped.
      if (g.pendingTapL != 0 && t - g.pendingTapL >= kDoubleTapNs) {
        g.pendingTapL = 0;
      }
      if (clickR && !g.prevStickClickR && g.paused) {
        // r51 (a player: "a button combination to open the achievements menu in-game"; Lance: click
        // the right stick once while paused, "then you scroll down like you would any other thing, by
        // moving the left thumbstick down"): each click while paused opens the RetroAchievements list
        // or closes it, at once. Lance: a quick second click there must not turn Tabletop / Jumbotron,
        // so no double-tap is looked for while paused.
        g.pendingTapR = 0;
        if (!g.achOpen) {
          std::vector<xrra::AchievementRow> rows;
          uint32_t unlocked = 0, total = 0;
          std::string why;
          xrra::achievement_rows(&rows, &unlocked, &total, &why);
          XLOG("achievements list: %u of %u unlocked%s%s", unlocked, total, why.empty() ? "" : ", ", why.c_str());
          g.achOpen = true;
          g.achShown = false;
          g.achScroll = 0.0f;
          const int gen = ++g.achGen;
          // r51b: both pictures drawn off the game thread, once; PortXrEndFrame uploads them.
          std::thread([rows = std::move(rows), unlocked, total, why = std::move(why), gen] {
            const uint32_t listH =
                rows.empty() ? 3 * kAchRowH : static_cast<uint32_t>(rows.size()) * kAchRowH + 24;
            std::vector<uint8_t> list(static_cast<size_t>(kAchW) * listH * 4);
            std::vector<xrtext::AchievementLine> lines;
            lines.reserve(rows.size());
            for (const xrra::AchievementRow& r : rows) {
              lines.push_back({r.title.c_str(), r.description.c_str(), r.progress.c_str(), r.points, r.unlocked});
            }
            xrtext::achievements_list(list.data(), kAchW, listH, lines.data(), static_cast<int>(lines.size()),
                                      kAchRowH, why.c_str());
            char title[64];
            std::snprintf(title, sizeof(title), rows.empty() ? "ACHIEVEMENTS" : "ACHIEVEMENTS  -  %u OF %u UNLOCKED",
                          unlocked, total);
            std::vector<uint8_t> head(static_cast<size_t>(kAchW) * kAchHeadH * 4);
            xrtext::achievements_header(head.data(), kAchW, kAchHeadH, title,
                                        rows.empty() ? "RIGHT STICK CLICK OR B: CLOSE"
                                                     : "LEFT STICK: SCROLL   -   RIGHT STICK CLICK OR B: CLOSE");
            std::lock_guard<std::mutex> lock(g_achMutex);
            g_achListPixels = std::move(list);
            g_achHeadPixels = std::move(head);
            g_achListH = listH;
            g_achReadyGen = gen;
          }).detach();
        } else {
          g.achOpen = false;
        }
      } else if (clickR && !g.prevStickClickR) {
        if (g.pendingTapR != 0 && t - g.pendingTapR < kDoubleTapNs) {
          g.pendingTapR = 0;
          if (g.inSuite) {  // r61: as above
            g.inSuite = false;
            g.suiteDismissed = true;
          }
          g.tabletop = !g.tabletop;
          XLOG("mode: %s", mode_name());
        } else {
          g.pendingTapR = t;
        }
      }
      if (g.pendingTapR != 0 && t - g.pendingTapR >= kDoubleTapNs) {
        g.pendingTapR = 0;
        if (g.paused) {
          // A click from just before the pause: nothing.
        } else if (!g.tabletop || g.stereoFailed) {
          // r42: the next size, in front of you.
          g.screenPreset = (g.screenPreset + 1) % kScreenPresetCount;
          g.bringScreen = true;
          XLOG("screen preset %d (%.1f m wide at %.1f m)", g.screenPreset, screen_preset(screen_room()).width,
               screen_preset(screen_room()).distance);
        }
      }
      g.prevStickClickL = clickL;
      g.prevStickClickR = clickR;

      apply_rumble();
    }
  }
  // r76: a franchise match on its way from the main menu (a co-op one waits there for the friend's
  // headset): the menu gets nothing (it would move under the wait); B held still stops the wait (below).
  const bool holdB = (status.button & PAD_BUTTON_B) != 0;
  if (PortFranchiseMatchStarting()) {
    status.button = 0;
    status.stickX = status.stickY = status.substickX = status.substickY = 0;
    status.triggerLeft = status.triggerRight = 0;
  }
  // r47: waiting at the match start for the online friend, B held 2 s stops the wait (the match
  // then goes ahead against the computer).
  if (xrnet::waiting_for_friend() != 0 && holdB) {
    const uint64_t tb = now_ns();
    if (g.holdBSinceNs == 0) {
      g.holdBSinceNs = tb;
    } else if (tb - g.holdBSinceNs >= 2000000000ull) {
      xrnet::cancel_wait();
      g.holdBSinceNs = 0;
    }
  } else {
    g.holdBSinceNs = 0;
  }
  // Every frame, neutral or not: keeps port 0 connected. r47: a paired Bluetooth pad is already in
  // here (above); Aurora gives the ports nothing of its own (PADSetPhysicalExternal).
  // Handed to the pads at the end of PortXrBeginFrame, where an online match decides the frame.
  g.localPad = status;
}

// ---------------------------------------------------------------------------------------------
// Screen swapchain + upload
// ---------------------------------------------------------------------------------------------

uint32_t find_memory_type(uint32_t typeBits, VkMemoryPropertyFlags want) {
  for (uint32_t i = 0; i < g.memProps.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) != 0 && (g.memProps.memoryTypes[i].propertyFlags & want) == want) {
      return i;
    }
  }
  return UINT32_MAX;
}

void destroy_staging() {
  if (g.stagingMapped != nullptr) {
    vkUnmapMemory(g.device, g.stagingMemory);
    g.stagingMapped = nullptr;
  }
  if (g.staging != VK_NULL_HANDLE) {
    vkDestroyBuffer(g.device, g.staging, nullptr);
    g.staging = VK_NULL_HANDLE;
  }
  if (g.stagingMemory != VK_NULL_HANDLE) {
    vkFreeMemory(g.device, g.stagingMemory, nullptr);
    g.stagingMemory = VK_NULL_HANDLE;
  }
  g.stagingSize = 0;
}

// A host-visible, mapped transfer buffer of at least `size` bytes, (re)made in place.
bool ensure_host_buffer(VkBuffer& buffer, VkDeviceMemory& memory, void*& mapped, VkDeviceSize& current,
                        VkDeviceSize size, const char* what) {
  if (buffer != VK_NULL_HANDLE && current >= size) {
    return true;
  }
  if (mapped != nullptr) vkUnmapMemory(g.device, memory);
  if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(g.device, buffer, nullptr);
  if (memory != VK_NULL_HANDLE) vkFreeMemory(g.device, memory, nullptr);
  mapped = nullptr;
  buffer = VK_NULL_HANDLE;
  memory = VK_NULL_HANDLE;
  current = 0;
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = size;
  info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!vk_ok(vkCreateBuffer(g.device, &info, nullptr, &buffer), what)) {
    return false;
  }
  VkMemoryRequirements reqs{};
  vkGetBufferMemoryRequirements(g.device, buffer, &reqs);
  uint32_t type = find_memory_type(reqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                                            VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
  if (type == UINT32_MAX) {
    type = find_memory_type(reqs.memoryTypeBits,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  }
  if (type == UINT32_MAX) {
    XERR("no host-visible memory for %s", what);
    return false;
  }
  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  alloc.allocationSize = reqs.size;
  alloc.memoryTypeIndex = type;
  if (!vk_ok(vkAllocateMemory(g.device, &alloc, nullptr, &memory), what) ||
      !vk_ok(vkBindBufferMemory(g.device, buffer, memory, 0), what) ||
      !vk_ok(vkMapMemory(g.device, memory, 0, VK_WHOLE_SIZE, 0, &mapped), what)) {
    return false;
  }
  current = size;
  return true;
}

// The eye dump's buffer (a copy destination: it needs TRANSFER_DST, which r4-r6 lacked).
bool ensure_staging(VkDeviceSize size) {
  return ensure_host_buffer(g.staging, g.stagingMemory, g.stagingMapped, g.stagingSize, size, "staging");
}

void destroy_swapchain() {
  if (g.swapchain != XR_NULL_HANDLE) {
    xrDestroySwapchain(g.swapchain);
    g.swapchain = XR_NULL_HANDLE;
  }
  g.swapchainImages.clear();
  g.swapchainWidth = g.swapchainHeight = 0;
  g.swapchainHasImage = false;
}

bool ensure_swapchain(uint32_t width, uint32_t height) {
  if (g.swapchain != XR_NULL_HANDLE && g.swapchainWidth == width && g.swapchainHeight == height) {
    return true;
  }
  destroy_swapchain();

  uint32_t count = 0;
  xrEnumerateSwapchainFormats(g.session, 0, &count, nullptr);
  std::vector<int64_t> formats(count);
  if (count > 0) {
    xrEnumerateSwapchainFormats(g.session, count, &count, formats.data());
  }
  // The game's pixels are already display-encoded, so they go into an sRGB image byte for byte and
  // the compositor shows them as the TV would have.
  const int64_t preferred[] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB};
  g.swapchainFormat = 0;
  for (int64_t want : preferred) {
    if (std::find(formats.begin(), formats.end(), want) != formats.end()) {
      g.swapchainFormat = want;
      break;
    }
  }
  if (g.swapchainFormat == 0) {
    XERR("no sRGB RGBA8/BGRA8 swapchain format (runtime offers %u formats)", count);
    return false;
  }
  g.swapchainBgra = g.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB;

  XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
  info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                    XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
  info.format = g.swapchainFormat;
  info.sampleCount = 1;
  info.width = width;
  info.height = height;
  info.faceCount = 1;
  info.arraySize = 1;
  info.mipCount = 1;
  if (!xr_ok(xrCreateSwapchain(g.session, &info, &g.swapchain), "xrCreateSwapchain")) {
    return false;
  }
  uint32_t imageCount = 0;
  xrEnumerateSwapchainImages(g.swapchain, 0, &imageCount, nullptr);
  g.swapchainImages.assign(imageCount, XrSwapchainImageVulkanKHR{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
  if (!xr_ok(xrEnumerateSwapchainImages(g.swapchain, imageCount, &imageCount,
                                        reinterpret_cast<XrSwapchainImageBaseHeader*>(g.swapchainImages.data())),
             "xrEnumerateSwapchainImages")) {
    destroy_swapchain();
    return false;
  }
  g.swapchainWidth = width;
  g.swapchainHeight = height;
  XLOG("screen swapchain %ux%u format %lld (%u images)", width, height, static_cast<long long>(g.swapchainFormat),
       imageCount);
  return true;
}

// Copies the newest game frame into the screen swapchain. Game thread, after synchronize().
void upload_latest_frame() {
  const uint64_t start = now_ns();
  uint32_t width = 0, height = 0;
  UploadSlot& u = g.uploads[g.uploadSlot];
  {
    std::lock_guard<std::mutex> lock(g_frameMutex);
    if (g_frameSerial == g_frameUploaded || g_frameWidth == 0 || g_frameHeight == 0) {
      return;
    }
    width = g_frameWidth;
    height = g_frameHeight;
    if (u.cmd == VK_NULL_HANDLE) {
      VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
      allocInfo.commandPool = g.pool;
      allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocInfo.commandBufferCount = 1;
      VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
      if (!vk_ok(vkAllocateCommandBuffers(g.device, &allocInfo, &u.cmd), "vkAllocateCommandBuffers(upload)") ||
          !vk_ok(vkCreateFence(g.device, &fenceInfo, nullptr, &u.fence), "vkCreateFence(upload)")) {
        g_frameUploaded = g_frameSerial;
        return;
      }
    }
    if (u.pending) {
      // This slot's copy is two uploads old, long done in the normal case.
      vkWaitForFences(g.device, 1, &u.fence, VK_TRUE, UINT64_MAX);
      vkResetFences(g.device, 1, &u.fence);
      u.pending = false;
    }
    if (!ensure_swapchain(width, height) ||
        !ensure_host_buffer(u.buffer, u.memory, u.mapped, u.size, static_cast<VkDeviceSize>(width) * height * 4,
                            "upload")) {
      g_frameUploaded = g_frameSerial;
      return;
    }
    auto* dst = static_cast<uint8_t*>(u.mapped);
    const size_t bytes = static_cast<size_t>(width) * height * 4;
    if (!g.swapchainBgra) {
      std::memcpy(dst, g_frame.data(), bytes);
    } else {
      const uint8_t* src = g_frame.data();
      for (size_t i = 0; i < bytes; i += 4) {
        dst[i + 0] = src[i + 2];
        dst[i + 1] = src[i + 1];
        dst[i + 2] = src[i + 0];
        dst[i + 3] = src[i + 3];
      }
    }
    g_frameUploaded = g_frameSerial;
  }

  uint32_t index = 0;
  XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!xr_ok(xrAcquireSwapchainImage(g.swapchain, &acquire, &index), "xrAcquireSwapchainImage")) {
    return;
  }
  XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait.timeout = XR_INFINITE_DURATION;
  if (!xr_ok(xrWaitSwapchainImage(g.swapchain, &wait), "xrWaitSwapchainImage")) {
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(g.swapchain, &release);
    return;
  }
  const VkImage image = g.swapchainImages[index].image;

  VkCommandBuffer cmd = u.cmd;
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin);

  VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  toTransfer.srcAccessMask = 0;
  toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = image;
  toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toTransfer);

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(cmd, u.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  // OpenXR wants color swapchain images back in COLOR_ATTACHMENT_OPTIMAL on release.
  VkImageMemoryBarrier toColor = toTransfer;
  toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &toColor);
  vkEndCommandBuffer(cmd);

  // No wait: OpenXR only needs the copy submitted to the session's queue before the release; the
  // runtime orders its own reads after it.
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  if (vk_ok(vkQueueSubmit(g.queue, 1, &submit, u.fence), "vkQueueSubmit(upload)")) {
    u.pending = true;
  }
  g.uploadSlot = (g.uploadSlot + 1) % kUploadSlots;

  XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xr_ok(xrReleaseSwapchainImage(g.swapchain, &release), "xrReleaseSwapchainImage");
  g.swapchainHasImage = true;
  ++g.framesShown;
  g.uploadNsSinceStats += now_ns() - start;
}

// r42: a CPU picture (RGBA8, sRGB, premultiplied alpha) into a panel's own quad swapchain, made on
// first use. Only when the picture changes (a goal's banner, a crowd volume step).
bool panel_upload(Panel& p, const uint8_t* rgba, uint32_t width, uint32_t height) {
  UploadSlot& u = p.upload;
  if (p.swapchain == XR_NULL_HANDLE || p.width != width || p.height != height) {
    if (p.swapchain != XR_NULL_HANDLE) xrDestroySwapchain(p.swapchain);
    p.swapchain = XR_NULL_HANDLE;
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT |
                      XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = g.swapchainFormat != 0 ? g.swapchainFormat : VK_FORMAT_R8G8B8A8_SRGB;
    info.sampleCount = 1;
    info.width = width;
    info.height = height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!xr_ok(xrCreateSwapchain(g.session, &info, &p.swapchain), "xrCreateSwapchain(panel)")) {
      return false;
    }
    uint32_t count = 0;
    xrEnumerateSwapchainImages(p.swapchain, 0, &count, nullptr);
    p.images.assign(count, XrSwapchainImageVulkanKHR{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    xrEnumerateSwapchainImages(p.swapchain, count, &count,
                               reinterpret_cast<XrSwapchainImageBaseHeader*>(p.images.data()));
    p.width = width;
    p.height = height;
  }
  if (u.cmd == VK_NULL_HANDLE) {
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = g.pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!vk_ok(vkAllocateCommandBuffers(g.device, &allocInfo, &u.cmd), "vkAllocateCommandBuffers(panel)") ||
        !vk_ok(vkCreateFence(g.device, &fenceInfo, nullptr, &u.fence), "vkCreateFence(panel)")) {
      return false;
    }
  }
  if (u.pending) {
    vkWaitForFences(g.device, 1, &u.fence, VK_TRUE, UINT64_MAX);
    vkResetFences(g.device, 1, &u.fence);
    u.pending = false;
  }
  const size_t bytes = static_cast<size_t>(width) * height * 4;
  if (!ensure_host_buffer(u.buffer, u.memory, u.mapped, u.size, bytes, "panel")) {
    return false;
  }
  auto* dst = static_cast<uint8_t*>(u.mapped);
  if (g.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB) {
    for (size_t i = 0; i < bytes; i += 4) {
      dst[i + 0] = rgba[i + 2];
      dst[i + 1] = rgba[i + 1];
      dst[i + 2] = rgba[i + 0];
      dst[i + 3] = rgba[i + 3];
    }
  } else {
    std::memcpy(dst, rgba, bytes);
  }
  uint32_t index = 0;
  XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!xr_ok(xrAcquireSwapchainImage(p.swapchain, &acquire, &index), "xrAcquireSwapchainImage(panel)")) {
    return false;
  }
  XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  wait.timeout = XR_INFINITE_DURATION;
  xr_ok(xrWaitSwapchainImage(p.swapchain, &wait), "xrWaitSwapchainImage(panel)");
  VkCommandBuffer cmd = u.cmd;
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin);
  VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toTransfer.image = p.images[index].image;
  toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &toTransfer);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {width, height, 1};
  vkCmdCopyBufferToImage(cmd, u.buffer, p.images[index].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  VkImageMemoryBarrier toColor = toTransfer;
  toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &toColor);
  vkEndCommandBuffer(cmd);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  if (vk_ok(vkQueueSubmit(g.queue, 1, &submit, u.fence), "vkQueueSubmit(panel)")) {
    u.pending = true;
  }
  XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xr_ok(xrReleaseSwapchainImage(p.swapchain, &release), "xrReleaseSwapchainImage(panel)");
  p.hasImage = true;
  return true;
}

// A panel standing at `pos`, `width` m wide, turned to face your head about the vertical.
void place_panel(XrCompositionLayerQuad& quad, const Panel& p, const XrVector3f& pos, float width) {
  quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied
  quad.space = g.local;
  quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
  quad.subImage.swapchain = p.swapchain;
  quad.subImage.imageRect.offset = {0, 0};
  quad.subImage.imageRect.extent = {static_cast<int32_t>(p.width), static_cast<int32_t>(p.height)};
  quad.subImage.imageArrayIndex = 0;
  const float yaw = std::atan2(g.headPose.position.x - pos.x, g.headPose.position.z - pos.z);
  quad.pose.orientation = {0.0f, std::sin(0.5f * yaw), 0.0f, std::cos(0.5f * yaw)};
  quad.pose.position = pos;
  quad.size.width = std::max(0.001f, width);
  quad.size.height = std::max(0.001f, width * static_cast<float>(p.height) / static_cast<float>(p.width));
}

// --- r61: the home ground (xr_home.cpp) on show ---

XrVector3f table_point(float gx, float gy, float gz);  // below

// Asks for a panel to be drawn on a thread, when `generation` is not what it was last asked for.
template <class Draw>
void request_panel(AsyncPanel& a, int generation, uint32_t w, uint32_t h, Draw draw) {
  if (a.forGeneration == generation) return;
  a.forGeneration = generation;
  const int gen = ++a.requested;
  std::thread([&a, w, h, gen, draw]() {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    draw(px.data(), static_cast<int>(w), static_cast<int>(h));
    std::lock_guard<std::mutex> lock(a.mutex);
    if (gen < a.ready.load()) return;  // a newer one got there first
    a.pixels = std::move(px);
    a.width = w;
    a.height = h;
    a.ready = gen;
  }).detach();
}

// Uploads the newest drawing; true once there is one to show.
// r95 (Lance saw nothing of Pichu evolving, then "you stopped Pichu from evolving" when he pressed B): it
// waited for the drawing that was asked for LAST. The evolution scene asks for a new picture ~15 times a
// second, each drawn slower than that, so every finished one had a newer ask by the frame it was done
// and none ever went up (log: "pk_pichu is evolving", no picture, B 5 s later). Now the newest finished
// one goes up, whatever is asked for after it.
bool show_panel(AsyncPanel& a, Panel& p) {
  if (a.ready.load() > a.shown) {
    std::lock_guard<std::mutex> lock(a.mutex);
    const int ready = a.ready.load();
    panel_upload(p, a.pixels.data(), a.width, a.height);
    a.shown = ready;
  }
  return p.hasImage;
}

std::string upper(const std::string& s) {
  std::string r = s;
  for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return r;
}

std::string date_text(int64_t when) {
  if (when <= 0) return "";
  const std::time_t t = static_cast<std::time_t>(when);
  std::tm tm {};
  localtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%b %d %Y", &tm);
  return upper(buf);
}

// r87: a home-ground captain that may be an imported one (its pack, "" = the Strikers captain): its roster
// place (-1: none, or not on this headset any more), shown name, colours (xrfx: 100 + roster place) and
// banner motif (100 + its outline's cell, an evolved form's first form's; else class + 1).
int home_guest_index(const std::string& pack) {
  return pack.empty() ? -1 : PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, pack.c_str());
}
std::string home_name(int cls, const std::string& pack) {
  const int idx = home_guest_index(pack);
  const char* n = idx >= 0 ? PortGuestRosterName(idx) : nullptr;
  return upper(n != nullptr && n[0] != '\0' ? std::string(n) : std::string(xrhome::captain_name(cls)));
}
int home_colour_code(int cls, const std::string& pack) {
  const int idx = home_guest_index(pack);
  return idx >= 0 ? 100 + idx : cls;
}
// The imported captain picked (this mode) for the team of Strikers captain `cls`, "" none: the cup suite's
// table and banners (eTeamID order: Daisy, Donkey Kong, Luigi, Mario, Peach, Waluigi, Wario, Yoshi).
std::string cup_captain_guest(int cls) {
  static const int kTeamCaptain[8] = {1, 2, 5, 6, 7, 9, 10, 11};
  for (int team = 0; team < 8; ++team) {
    if (kTeamCaptain[team] != cls) continue;
    const int idx = PortGuestGetPick(team, PORT_GUEST_CAPTAIN);
    const char* pack = idx >= 0 ? PortGuestRosterPack(idx) : nullptr;
    return pack != nullptr ? std::string(pack) : std::string();
  }
  return std::string();
}
int home_motif(int cls, const std::string& pack) {
  int idx = home_guest_index(pack);
  if (idx < 0) return cls + 1;
  for (int guard = 0; guard < 4 && idx >= 0 && PortGuestRosterEvolved(idx); ++guard)
    idx = PortGuestRosterFindAny(0, PortGuestRosterEvolvesFrom(idx));
  return idx >= 0 && idx < 64 ? 100 + idx : 0;
}

// The home board's text, from the home ground's state.
void request_home_board(const xrhome::View& v) {
  uint32_t c1 = 0, c2 = 0;
  xrfx::team_colours(home_colour_code(v.captain, v.captainGuest), &c1, &c2);
  struct Text {
    std::string title, label[4], value[4], tag[4], note[4];
    uint32_t tagRgb[4] = {0x5A6680, 0x5A6680, 0x5A6680, 0x5A6680};
    uint32_t noteRgb[4] = {0x9AA6BE, 0x9AA6BE, 0x9AA6BE, 0x9AA6BE};
  } t;
  t.title = home_name(v.captain, v.captainGuest) + "'S HOME GROUND";
  auto record = [](const xrhome::Record& r, std::string* tag, uint32_t* rgb) {
    *tag = std::to_string(r.wins) + " - " + std::to_string(r.losses) + (r.draws > 0 ? " - " + std::to_string(r.draws) : "");
    *rgb = r.wins > r.losses ? 0x2FA84A : (r.wins < r.losses ? 0xD8303A : 0x5A6680);
  };
  t.label[0] = "LAST MATCH";
  if (v.last.valid) {
    const std::string them = v.last.online ? (v.last.theirName.empty() ? "A FRIEND" : upper(v.last.theirName))
                                           : home_name(v.last.theirCaptain, v.last.theirGuest);
    t.value[0] = home_name(v.last.myCaptain, v.last.myGuest) + " " + std::to_string(v.last.mine) + " - " +
                 std::to_string(v.last.theirs) + " " + them;
    const bool won = v.last.mine > v.last.theirs, lost = v.last.mine < v.last.theirs;
    t.tag[0] = won ? "WIN" : (lost ? "LOSS" : "DRAW");
    t.tagRgb[0] = won ? 0x2FA84A : (lost ? 0xD8303A : 0x5A6680);
    t.note[0] = date_text(v.last.when) + (v.last.overtime ? "  -  IN OVERTIME" : "") + (v.last.cup ? "  -  CUP GAME" : "") +
                (v.last.online ? "  -  ONLINE" : "");
  } else {
    t.value[0] = "NONE YET";
    t.note[0] = "PLAY A MATCH TO START YOUR RECORD";
  }
  t.label[1] = "CPU RIVAL";
  if (v.cpu.valid) {
    t.value[1] = home_name(v.cpu.captain, v.cpu.guest);
    record(v.cpu.record, &t.tag[1], &t.tagRgb[1]);
    if (v.cpu.record.lastLost) {
      t.note[1] = t.value[1] + " RUNS YOUR HOUSE UNTIL YOU BEAT THEM";
      t.noteRgb[1] = 0xFF6A6A;
    } else {
      t.note[1] = v.cpu.record.wins > v.cpu.record.losses ? "YOU OWN THIS ONE" : "YOUR WINS - THEIR WINS";
    }
  } else {
    t.value[1] = "NONE YET";
    t.note[1] = "PLAY THE SAME CPU TEAM TWICE";
  }
  t.label[2] = "ONLINE RIVAL";
  if (v.online.valid) {
    t.value[2] = v.online.name.empty() ? "A FRIEND" : upper(v.online.name);
    record(v.online.record, &t.tag[2], &t.tagRgb[2]);
    if (v.online.record.lastLost) {
      t.note[2] = "THEY GLOAT UNTIL YOUR NEXT WIN";
      t.noteRgb[2] = 0xFF6A6A;
    } else {
      t.note[2] = v.online.record.wins > v.online.record.losses ? "YOU OWN THIS ONE" : "YOUR WINS - THEIR WINS";
    }
  } else {
    t.value[2] = "NONE YET";
    t.note[2] = "PLAY A FRIEND ONLINE";
  }
  t.label[3] = "HONORS";
  t.value[3] = std::to_string(v.comebacks) + " COMEBACK" + (v.comebacks == 1 ? "" : "S") + "  /  " +
               std::to_string(v.superStrikes) + " SUPER STRIKE GOAL" + (v.superStrikes == 1 ? "" : "S");
  t.tag[3] = std::to_string(v.matches) + " GAME" + (v.matches == 1 ? "" : "S");
  if (v.ticket.valid) {
    t.note[3] = "FIRST WIN: " + home_name(v.ticket.myCaptain, v.ticket.myGuest) + " " + std::to_string(v.ticket.mine) +
                " - " + std::to_string(v.ticket.theirs) + " " +
                (v.ticket.online ? (v.ticket.theirName.empty() ? std::string("A FRIEND") : upper(v.ticket.theirName))
                                 : home_name(v.ticket.theirCaptain, v.ticket.theirGuest));
  }
  request_panel(g_homeBoard, v.generation, 640, 440, [t, c1, c2](uint8_t* px, int w, int h) {
    xrtext::HomeLine lines[4];
    for (int i = 0; i < 4; ++i) {
      lines[i] = {t.label[i].c_str(), t.value[i].c_str(), t.tag[i].c_str(), t.tagRgb[i], t.note[i].c_str(), t.noteRgb[i]};
    }
    xrtext::home_board(px, w, h, t.title.c_str(), c1, c2, lines, 4);
  });
  if (v.ticket.valid) {
    uint32_t t1 = 0, t2 = 0;
    xrfx::team_colours(home_colour_code(v.ticket.myCaptain, v.ticket.myGuest), &t1, &t2);
    const std::string score =
        home_name(v.ticket.myCaptain, v.ticket.myGuest) + "  " + std::to_string(v.ticket.mine) + " - " +
        std::to_string(v.ticket.theirs) + "  " +
        (v.ticket.online ? (v.ticket.theirName.empty() ? std::string("A FRIEND") : upper(v.ticket.theirName))
                         : home_name(v.ticket.theirCaptain, v.ticket.theirGuest));
    const std::string date = date_text(v.ticket.when);
    request_panel(g_ticket, static_cast<int>(v.ticket.when & 0x7FFFFFFF), 512, 256,
                  [score, date, t1, t2](uint8_t* px, int w, int h) {
                    xrtext::match_ticket(px, w, h, "FIRST WIN", score.c_str(), date.c_str(), t1, t2);
                  });
  }
}

// A home piece for the scene (up to its 24).
xrfx::Scene::HomePiece* add_piece(xrfx::Scene& scene, int kind, const XrVector3f& at, const XrQuaternionf& q, float size,
                                  uint32_t rgb1, uint32_t rgb2, int theme, float glow) {
  if (scene.homeCount >= static_cast<int>(sizeof(scene.homePieces) / sizeof(scene.homePieces[0]))) return nullptr;  // r89: 40
  xrfx::Scene::HomePiece& p = scene.homePieces[scene.homeCount++];
  p.kind = kind;
  p.pose.position = at;
  p.pose.orientation = q;
  p.size = size;
  p.rgb1 = rgb1;
  p.rgb2 = rgb2;
  p.theme = theme;
  p.glow = glow;
  return &p;
}

XrQuaternionf yaw_quat(float yaw) { return {0.0f, std::sin(0.5f * yaw), 0.0f, std::cos(0.5f * yaw)}; }

// A gentle sway (or a lively one) about the banner's rod.
XrQuaternionf sway(const XrQuaternionf& base, float t, float amount, float speed) {
  const float a = amount * std::sin(t * speed);
  const XrQuaternionf tilt{std::sin(0.5f * a), 0.0f, 0.0f, std::cos(0.5f * a)};
  return q_mul(base, tilt);
}

// The rival who gloats in your space after beating you (their captain, -1 none; r87: `guest` its imported
// one, "" none).
int gloating_rival(const xrhome::View& v, std::string* guest) {
  guest->clear();
  if (v.cpu.valid && v.cpu.record.lastLost) {
    *guest = v.cpu.guest;
    return v.cpu.captain;
  }
  if (v.online.valid && v.online.record.lastLost && v.last.valid && v.last.online) {
    *guest = v.last.theirGuest;
    return v.last.theirCaptain;
  }
  return -1;
}

// r89: the home corner's standee (the home captain's portrait over a gold plate, as awards night's).
AsyncPanel g_cornerP;
Panel g_cornerPanel;
XrCompositionLayerQuad g_cornerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
constexpr uint32_t kCornerW = 400, kCornerH = 640;
float mode_floor_y();  // below

// --- r96 (Lance: "i want to watch that shit occur in 3D and i want their celebration stuff minus the
// confetti to fall during that process ... for pichu, it should JUST be lightning that falls, for squirtle it
// should be water that falls/water cannons, charmander -- fire"): the evolution in your room. ---
//
// The hub's mode 3 with the Pokemon standing life-size on your floor between you and the big screen
// (xr_evo.cpp figures, drawn in the effects layer), facing you, in its own standing idle:
//   EVO page (stage 0): the old form in its colours.
//   evolving (stage 1, 8.8 s): its theme starts (xrfx::evo_theme: what falls in its celebration, its room
//     effects, its show - no confetti), growing; at 1.2 s it turns white (2.4 s all white, glowing); from
//     2.6 s the white shape flips between the old and the new form, faster and faster, pulsing; at 8.5 s the
//     flash (xrfx::evo_burst: rings, its pieces flung out, sparks, your view white).
//   evolved (stage 2): the new form comes out of the white in its colours (a shiny one twinkling), its
//     theme for 6 s more.
//   postponed (stage 3): the old form, no theme.
// The big screen's card is its backdrop then (the light, the words). The figures load on a thread
// (slot 0 the old form, 1 the new: a split's chosen one; the shiny pack's pictures when shiny), go up
// to the GPU here.
void floor_forward(const XrPosef& pose, float* fx, float* fz);  // below

struct EvoLoaded {
  int slot = 0;
  std::string pack;
  xrevo::Figure* fig = nullptr;
  xrfx::Model model;
};
std::mutex g_evoLoadMutex;
std::vector<EvoLoaded> g_evoLoaded;  // under g_evoLoadMutex

struct EvoShow {
  bool live = false;          // a figure stands in the room now (the card is its backdrop)
  std::string want[2], have[2], asked[2];
  xrevo::Figure* fig[2] = {nullptr, nullptr};
  bool placed = false;
  XrVector3f feet{0.0f, 0.0f, 0.0f};
  float yaw = 0.0f;
  XrVector3f from{0.0f, 0.0f, 0.0f};  // your head when it was placed, and the way to the big screen
  float dirX = 0.0f, dirZ = -1.0f, baseDist = 1.3f;
  std::vector<float> verts[2];
  int stage = -1;
  uint64_t burstStart = 0;    // the stage-1 start its burst was for
  uint64_t stage2Start = 0;
  bool themeEnded = true;
};
EvoShow g_evoShow;

void evo_request(int slot, const std::string& pack, const std::string& posePack) {
  std::thread([slot, pack, posePack]() {
    EvoLoaded l;
    l.slot = slot;
    l.pack = pack;
    l.fig = xrevo::load(pack, posePack, &l.model);
    if (l.fig == nullptr && pack != posePack) {  // a shiny pack not there: the normal one
      l.fig = xrevo::load(posePack, posePack, &l.model);
    }
    std::lock_guard<std::mutex> lock(g_evoLoadMutex);
    g_evoLoaded.push_back(std::move(l));
  }).detach();
}

void evo_clear() {
  EvoShow& es = g_evoShow;
  for (int i = 0; i < 2; ++i) {
    if (es.fig[i] != nullptr) {
      xrevo::release(es.fig[i]);
      es.fig[i] = nullptr;
      xrfx::set_model(i, xrfx::Model{});
    }
    es.want[i].clear();
    es.have[i].clear();
    es.asked[i].clear();
    es.verts[i].clear();
  }
  if (!es.themeEnded) xrfx::evo_end();
  es.themeEnded = true;
  es.live = false;
  es.placed = false;
  es.feet = {0.0f, 0.0f, 0.0f};
  es.stage = -1;
}

void update_evo(xrfx::Scene& scene, uint64_t nowNs) {
  EvoShow& es = g_evoShow;
  const bool on = g.fxReady && g.frOpen && g.frMode == 3 && g.headValid && !g.inSuite;
  // what the threads have read: kept if still wanted, else let go
  {
    std::vector<EvoLoaded> done;
    {
      std::lock_guard<std::mutex> lock(g_evoLoadMutex);
      done.swap(g_evoLoaded);
    }
    for (EvoLoaded& l : done) {
      if (!on || l.fig == nullptr || l.pack != es.want[l.slot]) {
        if (l.fig != nullptr) xrevo::release(l.fig);
        if (l.fig == nullptr && on && l.pack == es.want[l.slot]) XLOG("evo: no 3D figure for %s (the card shows it)", l.pack.c_str());
        continue;
      }
      if (es.fig[l.slot] != nullptr) xrevo::release(es.fig[l.slot]);
      es.fig[l.slot] = l.fig;
      es.have[l.slot] = l.pack;
      xrfx::set_model(l.slot, std::move(l.model));
    }
  }
  if (!on) {
    if (es.fig[0] != nullptr || es.fig[1] != nullptr || es.live || !es.themeEnded) evo_clear();
    return;
  }
  const PortFranchiseEvolution& e = g.frEvo;
  const int choice = e.options == 2 ? std::max(0, std::min(1, g.frEvoChoice)) : 0;
  const int stage = g.frEvoStage;
  const std::string from = e.fromKey, to = e.toKey[choice];
  const bool fromShiny = e.shiny, toShiny = e.shiny || (stage == 2 && g.frEvoNewShiny);
  es.want[0] = from + (fromShiny ? "_shiny" : "");
  es.want[1] = to + (toShiny ? "_shiny" : "");
  for (int i = 0; i < 2; ++i) {
    if (!es.want[i].empty() && es.want[i] != es.asked[i]) {
      es.asked[i] = es.want[i];
      evo_request(i, es.want[i], i == 0 ? from : to);
    }
  }
  if (stage != es.stage) {
    if (stage == 1) es.burstStart = 0;
    if (stage == 2) es.stage2Start = g.frEvoStartNs;
    es.stage = stage;
  }
  // its place: on your floor between you and the big screen (or ahead of you), facing you, once a show
  if (!es.placed) {
    const XrVector3f head = g.headPose.position;
    float fx = 0.0f, fz = -1.0f, d = 3.0f;
    if (scene.screenValid) {
      fx = scene.screenPose.position.x - head.x;
      fz = scene.screenPose.position.z - head.z;
      d = std::sqrt(fx * fx + fz * fz);
      if (d > 0.2f) {
        fx /= d;
        fz /= d;
      } else {
        floor_forward(g.headPose, &fx, &fz);
        d = 3.0f;
      }
    } else {
      floor_forward(g.headPose, &fx, &fz);
    }
    es.from = head;
    es.dirX = fx;
    es.dirZ = fz;
    es.baseDist = std::max(1.3f, std::min(2.1f, 0.5f * d));
    es.yaw = std::atan2(fz, -fx);  // its +X (its front) toward you: (cos yaw, -sin yaw) = (-fx, -fz)
    es.placed = true;
  }
  // how far: its nearest point (the farther-reaching form's, its head or snout) a metre from you; fixed once
  // it starts to evolve
  if (stage == 0 || (es.feet.x == 0.0f && es.feet.z == 0.0f)) {
    float reach = 0.0f;
    for (int i = 0; i < 2; ++i) {
      if (es.fig[i] == nullptr) continue;
      const float hh = xrevo::height(es.fig[i]);
      reach = std::max(reach, xrevo::front(es.fig[i]) * (hh > 1.9f ? 1.9f / hh : 1.0f));
    }
    const float dist = std::max(es.baseDist, reach + 1.0f);
    const XrVector3f feet{es.from.x + es.dirX * dist, scene.floorY, es.from.z + es.dirZ * dist};
    if (std::fabs(feet.x - es.feet.x) + std::fabs(feet.z - es.feet.z) > 0.01f) {
      XLOG("evo: the 3D evolution stands %.2f m ahead (reach %.2f, floor %.2f)", dist, reach, scene.floorY);
    }
    es.feet = feet;
  }
  const float t = static_cast<float>(nowNs - g.frEvoStartNs) / 1e9f;
  const float clock = static_cast<float>(static_cast<double>(nowNs % 3600000000000ull) / 1e9);
  // which figure, how white, how it glows
  int show = 0;
  float white = 0.0f, glow = 0.0f, pulse = 1.0f;
  if (stage == 1) {
    white = std::min(1.0f, std::max(0.0f, (t - 1.2f) / 1.2f));
    if (t >= 2.6f && t < 8.5f && es.fig[1] != nullptr) {
      const float W = 5.9f, u = t - 2.6f;
      const float phase = 1.1f * u + 3.0f * u * u * u / (W * W);
      show = (static_cast<int>(std::floor(2.0f * phase)) & 1) != 0 ? 1 : 0;
      pulse = 1.0f + 0.05f * std::sin(phase * 6.2831853f);
    }
    if (t >= 8.5f && es.fig[1] != nullptr) show = 1;
    glow = std::min(1.0f, std::max(0.0f, (t - 1.2f) / 7.3f));
  } else if (stage == 2) {
    show = es.fig[1] != nullptr ? 1 : 0;
    white = std::max(0.0f, 1.0f - (t + 0.3f) / 1.1f);
    glow = 0.6f * white;
  }
  if (es.fig[show] == nullptr) {
    es.live = false;
    scene.models[0].on = scene.models[1].on = false;
    return;
  }
  es.live = true;
  const xrevo::Figure* f = es.fig[show];
  const float h = xrevo::height(f);
  const float scale = h > 1.9f ? 1.9f / h : 1.0f;
  // the evolution's white growing a touch as it glows (the games' swelling silhouette)
  const float s = scale * pulse * (1.0f + 0.04f * glow);
  float lift = 0.0f;
  if (stage == 1 && white > 0.0f) lift = 0.12f * white * (0.6f + 0.4f * std::sin(clock * 2.2f));  // it rises in its light
  xrevo::pose(f, clock, es.feet, es.yaw, s, lift, &es.verts[show]);
  xrfx::Scene::ModelDraw& md = scene.models[show];
  md.on = true;
  md.posNrm = es.verts[show].data();
  md.white = white;
  md.alpha = 1.0f;
  md.glow = glow;
  md.glowRgb = 0xBFE0FF;
  md.under = stage == 1 || (stage == 2 && t < 6.0f) ? 0.35f : 0.0f;
  md.flameFrame = static_cast<int>((nowNs / 83000000ull) % 3);
  scene.models[1 - show].on = false;
  // its theme (no confetti): while it evolves and 6 s after
  const float H = h * s;
  const float R = std::max(0.7f, 1.6f * xrevo::radius(f) * s);
  if (stage == 1) {
    xrfx::evo_theme(from.c_str(), es.feet, R, std::max(2.0f, H + 0.8f), 0.5f + 1.3f * std::min(1.0f, t / 8.5f));
    es.themeEnded = false;
    if (t >= 8.5f && es.burstStart != g.frEvoStartNs) {
      es.burstStart = g.frEvoStartNs;
      xrfx::evo_burst(scene, es.feet, H, 0xFFF4C0);
      XLOG("evo: the flash");
    }
  } else if (stage == 2 && t < 6.0f) {
    xrfx::evo_theme(to.c_str(), es.feet, R, std::max(2.0f, H + 0.8f), 1.0f);
    es.themeEnded = false;
  } else if (!es.themeEnded) {
    xrfx::evo_end();
    es.themeEnded = true;
  }
  // a shiny one twinkles (in its colours: before it turns white, and once it's out of the white)
  const bool shinyNow = show == 0 ? fromShiny : toShiny;
  if (shinyNow && white < 0.5f) xrfx::evo_sparkle(es.feet, 1.2f * xrevo::radius(f) * s, H, scene.dt);
}

void update_home(xrfx::Scene& scene, const XrCompositionLayerBaseHeader** layers, uint32_t* layerCount,
                 XrCompositionLayerQuad* quads) {
  const xrhome::View v = xrhome::view();
  request_home_board(v);
  const bool board = show_panel(g_homeBoard, g.homePanel);
  const bool ticket = v.ticket.valid && show_panel(g_ticket, g.ticketPanel);
  uint32_t c1 = 0, c2 = 0;
  xrfx::team_colours(home_colour_code(v.captain, v.captainGuest), &c1, &c2);
  std::string rivalGuest;
  const int rival = gloating_rival(v, &rivalGuest);
  uint32_t r1 = 0, r2 = 0;
  if (rival >= 0) xrfx::team_colours(home_colour_code(rival, rivalGuest), &r1, &r2);
  // r87: the banners' motifs (an imported captain's own outline)
  const int homeMotif = home_motif(v.captain, v.captainGuest);
  const int rivalMotif = rival >= 0 ? home_motif(rival, rivalGuest) : 0;
  const float t = static_cast<float>(now_ns() % 1000000000000ull) / 1e9f;
  auto push_quad = [&](XrCompositionLayerQuad& q) {
    if (*layerCount < 24) layers[(*layerCount)++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&q);
  };

  // The home corner by your trophy shelf (your room): your banner, the board, a ledge for what you
  // have earned, and the rival's banner while they gloat.
  if (g.mrMode && g.shelf.placed && !g.inSuite) {
    const float yaw = g.shelf.yaw, cs = std::cos(yaw), sn = std::sin(yaw);
    auto at = [&](float x, float y, float z) {
      return XrVector3f{g.shelf.position.x + x * cs + z * sn, g.shelf.position.y + y, g.shelf.position.z - x * sn + z * cs};
    };
    const XrQuaternionf q = yaw_quat(yaw);
    add_piece(scene, 0, at(-0.66f, 0.62f, -0.06f), sway(q, t, 0.03f, 1.1f), 0.62f, c1, c2, homeMotif, 0.0f);
    if (rival >= 0) {
      add_piece(scene, 0, at(-0.98f, 0.66f, -0.06f), sway(q, t, 0.12f, 4.0f), 0.62f, r1, r2, rivalMotif,
                0.12f + 0.08f * std::sin(t * 6.0f));
    }
    if (board) {
      place_panel(quads[0], g.homePanel, at(0.72f, 0.47f, -0.09f), 0.46f);
      quads[0].pose.orientation = q;
      push_quad(quads[0]);
    }
    const float ledgeY = 0.06f;
    if (ticket || v.superStrikes > 0 || v.comebacks > 0) add_piece(scene, 3, at(0.72f, ledgeY, -0.02f), q, 0.38f, 0, 0, 0, 0.0f);
    if (ticket) {
      place_panel(quads[1], g.ticketPanel, at(0.59f, ledgeY + 0.016f + 0.05f, 0.0f), 0.17f);
      quads[1].pose.orientation = q;
      push_quad(quads[1]);
    }
    if (v.superStrikes > 0) {
      const XrVector3f ball = at(0.75f, ledgeY + 0.016f + 0.04f, 0.0f);
      add_piece(scene, 2, ball, q, 0.075f, 0xFFFFFF, 0x7FD8FF, 0, 0.55f + 0.25f * std::sin(t * 9.0f));
      if (scene.sparkCount < 2) {
        scene.sparkAt[scene.sparkCount] = ball;
        scene.sparkRadius[scene.sparkCount] = 0.05f;
        ++scene.sparkCount;
      }
    }
    if (v.comebacks > 0) {
      add_piece(scene, 1, at(0.85f, ledgeY + 0.016f + 0.1f, 0.0f), sway(q, t, 0.04f, 1.6f), 0.2f, c1, 0xFFD21F, 20, 0.0f);
    }
    // r89 (Lance: "a home ground for EVERY character" - its own corner in your room): on the floor in front
    // of the shelf, the captain's rug; on it a display pedestal with its things (what falls in its
    // celebration: swords and shields, bones, a Poke Ball ...; a flame burns on it, sparks crackle: xr_fx's
    // corner_ambient) and its portrait standing beside it.
    {
      const float floorY = mode_floor_y();
      auto on_floor = [&](float x, float z, float y) {
        const XrVector3f p = at(x, 0.0f, z);
        return XrVector3f{p.x, floorY + y, p.z};
      };
      const int guestIdx = home_guest_index(v.captainGuest);
      const char* pack = guestIdx >= 0 ? v.captainGuest.c_str() : "";
      // r91 (Lance: Cubone's "bone podium thing wasnt aligned with the center of the circle"): the display
      // stands in the rug's middle; the standee beside the rug, not on it
      constexpr float kRugX = -0.2f, kRugZ = 0.35f;
      add_piece(scene, 7, on_floor(kRugX, kRugZ, 0.0f), q, 1.1f, c1, c2, homeMotif, 0.0f);
      const float pedH = 0.8f;
      const XrVector3f pedAt = on_floor(kRugX, kRugZ, 0.5f * pedH);
      add_piece(scene, 4, pedAt, q, pedH, 0, 0, 0, 0.0f);
      const XrVector3f topAt{pedAt.x, floorY + pedH, pedAt.z};
      int shapes[3];
      uint32_t pc1[3], pc2[3];
      const int np = xrfx::corner_props(pack, v.captain, shapes, pc1, pc2);
      for (int k = 0; k < np; ++k) {
        if (k == 0) {
          // the main one hovering over the middle, turning
          const XrVector3f p{topAt.x, topAt.y + 0.17f + 0.02f * std::sin(t * 1.4f), topAt.z};
          add_piece(scene, 6, p, yaw_quat(0.6f * t), 0.2f, pc1[k], pc2[k], shapes[k], 0.0f);
        } else {
          // the others going round it
          const float ang = 0.45f * t + 6.2831853f * static_cast<float>(k - 1) / static_cast<float>(std::max(1, np - 1));
          const XrVector3f p{topAt.x + 0.17f * std::cos(ang), topAt.y + 0.09f + 0.015f * std::sin(t * 2.0f + k),
                             topAt.z + 0.17f * std::sin(ang)};
          add_piece(scene, 6, p, yaw_quat(-ang + 1.2f * t), 0.12f, pc1[k], pc2[k], shapes[k], 0.0f);
        }
      }
      scene.cornerOn = true;
      scene.cornerTop = topAt;
      scene.cornerRug = on_floor(kRugX, kRugZ, 0.0f);
      scene.cornerRadius = 0.55f;
      scene.cornerClass = v.captain;
      std::snprintf(scene.cornerPack, sizeof(scene.cornerPack), "%s", pack);
      // its portrait, standing on the rug beside the display
      static const char* const kPortraitName[12] = {"birdo", "daisy", "donkeykong", "hammerbro", "koopa", "luigi",
                                                    "mario", "peach", "toad", "waluigi", "wario", "yoshi"};
      const std::string portrait = guestIdx >= 0 ? v.captainGuest
                                                 : std::string("strikers_") +
                                                       (v.captain >= 0 && v.captain < 12 ? kPortraitName[v.captain] : "mario");
      const std::string name = home_name(v.captain, v.captainGuest);
      const int gen = static_cast<int>(std::hash<std::string>()(portrait + "|" + name) & 0x7FFFFFFF);
      request_panel(g_cornerP, gen, kCornerW, kCornerH, [portrait, name](uint8_t* px, int w, int h) {
        const GuestPortrait& p = guest_portrait(portrait);
        xrtext::award_standee(px, w, h, p.rgba.empty() ? nullptr : p.rgba.data(), p.w, p.h, "HOME GROUND", name.c_str());
      });
      if (show_panel(g_cornerP, g_cornerPanel)) {
        const float height = 0.95f;
        const float width = height * static_cast<float>(kCornerW) / static_cast<float>(kCornerH);
        place_panel(g_cornerQuad, g_cornerPanel, on_floor(kRugX + 0.8f, kRugZ - 0.05f, 0.5f * height), width);
        push_quad(g_cornerQuad);
        // r91 (Lance: Cubone's "flat ass picture not even being visible"): the effects layer goes over the
        // quad layers, so the rug, display and effects covered the standee wherever they crossed it: now a
        // stand-in for it there (as the cup suite's boards), hiding what is behind it and leaving it seen
        if (scene.panelCount < 4) {
          xrfx::Scene::Panel& pn = scene.panels[scene.panelCount++];
          pn.pose = g_cornerQuad.pose;
          pn.width = g_cornerQuad.size.width;
          pn.height = g_cornerQuad.size.height;
        }
      }
    }
  }

  // Beside the big screen in your room: your banners either side (the rival's on the right while
  // they gloat) and the board above it.
  if (g.mrMode && !g.tabletop && g.shownScreenValid && !g.inSuite) {
    const XrPosef& sp = g.shownScreenPose;
    const float w = g.shownScreenW, h = g.shownScreenH;
    const float size = std::max(0.45f, std::min(1.6f, 0.6f * h));
    auto along = [&](float x, float y, float z) {
      const XrVector3f o = q_rot(sp.orientation, XrVector3f{x, y, z});
      return XrVector3f{sp.position.x + o.x, sp.position.y + o.y, sp.position.z + o.z};
    };
    const float side = 0.5f * w + 0.3f * size;
    const float y = 0.5f * h - 0.5f * size;
    add_piece(scene, 0, along(-side, y, 0.02f), sway(sp.orientation, t, 0.03f, 1.0f), size, c1, c2, homeMotif, 0.0f);
    if (rival >= 0) {
      add_piece(scene, 0, along(side, y, 0.02f), sway(sp.orientation, t, 0.1f, 4.0f), size, r1, r2, rivalMotif, 0.1f);
    } else {
      add_piece(scene, 0, along(side, y, 0.02f), sway(sp.orientation, t, 0.03f, 1.2f), size, c1, c2, homeMotif, 0.0f);
    }
    // r70b: the Super Strike Series bunting while your franchise club is in the playoffs (or champion):
    // a row of little banners along the screen's top, gold with a star and your club's colours in turn
    // (champions: all gold, glowing).
    float buntingH = 0.0f;
    if (g_frBuntingOn.load()) {
      const bool champ = g_frChampion.load();
      const uint32_t f1 = g_frBunting1.load(), f2 = g_frBunting2.load();
      const int count = 9;
      const float bs = std::max(0.12f, std::min(0.26f, 0.9f * w / count / 0.6f));
      const float y0 = 0.5f * h + 0.02f + 0.5f * bs;
      for (int i = 0; i < count; ++i) {
        const float x = (static_cast<float>(i) - 0.5f * (count - 1)) * (w / count);
        const bool gold = champ || (i % 2) == 0;
        add_piece(scene, 0, along(x, y0, 0.03f), sway(sp.orientation, t + 0.37f * i, 0.05f, 1.3f), bs,
                  gold ? 0xFFD21F : f1, gold ? 0x16306E : f2, 0, champ ? 0.25f + 0.1f * std::sin(t * 3.0f + i) : 0.0f);
      }
      buntingH = bs + 0.03f;
    }
    if (board) {
      const float bw = std::max(0.5f, std::min(1.3f, 0.36f * w));
      const float bh = bw * 440.0f / 640.0f;
      place_panel(quads[2], g.homePanel, along(0.0f, 0.5f * h + 0.05f + buntingH + 0.5f * bh, 0.0f), bw);
      quads[2].pose.orientation = sp.orientation;
      push_quad(quads[2]);
    }
  }

  // The Tabletop field: a pennant at each corner, yours; the far two the rival's while they gloat.
  if (g.tabletop && !g.stereoFailed && !g.inSuite) {
    const float k = g.table.scale;
    const float size = 6.5f * k;
    for (int corner = 0; corner < 4; ++corner) {
      const float gx = (corner & 1) ? 22.5f : -22.5f, gy = (corner & 2) ? 14.5f : -14.5f;
      const bool far = gy > 0.0f;
      const XrVector3f p = table_point(gx, gy, 0.5f * 6.5f);
      // The flag points away from the field's middle, along the pitch.
      const XrQuaternionf q = yaw_quat(g.table.yaw + (gx < 0.0f ? 3.14159265f : 0.0f));
      if (far && rival >= 0) {
        add_piece(scene, 1, p, sway(q, t, 0.08f, 4.0f), size, r1, r2, rivalMotif, 0.1f);
      } else {
        add_piece(scene, 1, p, sway(q, t, 0.04f, 1.5f + 0.2f * corner), size, c1, c2, homeMotif, 0.0f);
      }
    }
  }
}

// r73: awards night in your room (franchise: the champion crowned, until the next season starts; the
// hub closed): the winners life-size in a row between you and the big screen, each a standee of its
// portrait over a gold plate (one layer each), a pedestal with a gold trophy beside it.
// r80: 6 (Sidekick of the Year joined)
constexpr int kAwardStandees = 6;
AsyncPanel g_awardP[kAwardStandees];
Panel g_awardPanel[kAwardStandees];
XrCompositionLayerQuad g_awardQuad[kAwardStandees] = {
    {XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD},
    {XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}};
constexpr uint32_t kAwardW = 400, kAwardH = 640;
float mode_floor_y();                                           // below
void floor_forward(const XrPosef& pose, float* fx, float* fz);  // below

void update_awards(xrfx::Scene& scene, const XrCompositionLayerBaseHeader** layers, uint32_t* layerCount) {
  if (!g.frRoomOn || !g.mrMode || g.tabletop || !g.shownScreenValid || g.frOpen || g.gpOpen || !g.headValid) return;
  std::vector<std::string> text, keys;
  {
    std::lock_guard<std::mutex> lock(g_frMutex);
    const PortFranchiseView& v = g_frRoom.view;
    if (v.phase != PORT_FR_PHASE_DONE) return;
    for (int i = 0; i < v.awardCount && i < kAwardStandees; ++i) {
      text.push_back(v.awards[i]);
      keys.push_back(v.awardKeys[i]);
    }
  }
  const int n = static_cast<int>(text.size());
  if (n == 0) return;
  const XrVector3f head = g.headPose.position, sp = g.shownScreenPose.position;
  float ux = sp.x - head.x, uz = sp.z - head.z;
  float d = std::sqrt(ux * ux + uz * uz);
  if (d < 0.5f) {
    floor_forward(g.headPose, &ux, &uz);
    d = 3.0f;
  } else {
    ux /= d;
    uz /= d;
  }
  const float dist = std::max(1.6f, std::min(3.0f, 0.55f * d));
  const float rx = -uz, rz = ux;  // your right, facing the screen
  const float floorY = mode_floor_y();
  const float t = static_cast<float>(now_ns() % 1000000000000ull) / 1e9f;
  for (int i = 0; i < n; ++i) {
    const float lateral = (static_cast<float>(i) - 0.5f * (n - 1)) * (n > 5 ? 0.85f : 0.95f);
    const XrVector3f at{head.x + ux * dist + rx * lateral, floorY, head.z + uz * dist + rz * lateral};
    // its standee: "MVP: CHARIZARD (PEWTER CITY)" -> "MVP" over "CHARIZARD (PEWTER CITY)"
    const std::string& line = text[i];
    const size_t colon = line.find(':');
    const std::string award = colon == std::string::npos ? line : line.substr(0, colon);
    const std::string name = colon == std::string::npos ? std::string() : line.substr(colon + 2);
    const std::string key = keys[i];
    request_panel(g_awardP[i], g.frRoomGen, kAwardW, kAwardH, [key, award, name](uint8_t* px, int w, int h) {
      const GuestPortrait& p = guest_portrait(key);
      xrtext::award_standee(px, w, h, p.rgba.empty() ? nullptr : p.rgba.data(), p.w, p.h, award.c_str(), name.c_str());
    });
    if (show_panel(g_awardP[i], g_awardPanel[i]) && *layerCount < 32) {
      const float height = 1.55f;  // life-size-ish, the plate included
      const float width = height * static_cast<float>(kAwardW) / static_cast<float>(kAwardH);
      place_panel(g_awardQuad[i], g_awardPanel[i], XrVector3f{at.x, floorY + 0.5f * height, at.z}, width);
      layers[(*layerCount)++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&g_awardQuad[i]);
    }
    // its gold trophy, held up in front of it (no pedestal: the room's pieces are 24 at most, and the
    // champion's bunting takes 9)
    const float yawToYou = std::atan2(head.x - at.x, head.z - at.z);
    const XrVector3f cup{at.x - ux * 0.18f, floorY + 0.8f + 0.03f * std::sin(t * 1.3f + i), at.z - uz * 0.18f};
    add_piece(scene, 5, cup, yaw_quat(yawToYou + 0.4f * std::sin(t * 0.7f + i)), 0.3f, 0xFFD21F, 0xFFF2A0, 0, 0.25f);
  }
}

// The cup suite (VR, between a cup's games): the cup you are playing for on a pedestal in front of
// you (a ghost until you win it), the table to your left, your banner and the next opponent's either
// side, your trophies to your right with the home board above them.
// r61: a point of the cup suite (meters: x right, y up from its floor, z back toward where it opened,
// as tools/suites builds its rooms) in your room (LOCAL), and a turn whose front faces that spot.
XrVector3f suite_point(float x, float y, float z) {
  const float c = std::cos(g.suiteYaw), sn = std::sin(g.suiteYaw);
  return XrVector3f{g.suiteOrigin.x + x * c + z * sn, g.floorY + y, g.suiteOrigin.z - x * sn + z * c};
}
XrQuaternionf suite_facing(float x, float z) { return yaw_quat(g.suiteYaw + std::atan2(-x, -z)); }

void update_suite(xrfx::Scene& scene, const XrCompositionLayerBaseHeader** layers, uint32_t* layerCount,
                  XrCompositionLayerQuad* quads) {
  if (!g.inSuite) return;
  const xrhome::Cup cup = xrhome::cup();
  const xrhome::View v = xrhome::view();
  const float t = static_cast<float>(now_ns() % 1000000000000ull) / 1e9f;
  // A board hangs as a quad layer over the room's layer; the effects over it see it as solid.
  auto push_quad = [&](XrCompositionLayerQuad& q) {
    if (*layerCount < 24) layers[(*layerCount)++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&q);
    if (scene.panelCount < 4) scene.panels[scene.panelCount++] = xrfx::Scene::Panel{q.pose, q.size.width, q.size.height};
  };
  // The pedestal and the cup.
  add_piece(scene, 4, suite_point(0.0f, 0.5f, -1.5f), suite_facing(0.0f, -1.5f), 1.0f, 0, 0, 0, 0.0f);
  if (cup.trophy >= 0 && cup.trophy < 8) {
    uint32_t cc = 0, cb = 0;
    xrfx::cup_colours(cup.trophy, &cc, &cb);
    const bool won = (g.trophyBits >> cup.trophy) & 1u;
    const float spin = 0.4f * t;
    const XrVector3f cupAt = suite_point(0.0f, 1.0f + 0.5f * 0.34f + 0.03f * std::sin(t * 1.3f), -1.5f);
    // Not won yet: a ghost of it, glowing; it fills in the day you win it.
    add_piece(scene, 5, cupAt, yaw_quat(g.suiteYaw + spin), 0.34f, won ? cc : 0x8FA8D8, won ? cb : 0x30405A, won ? 0 : 1,
              won ? 0.15f : 0.35f + 0.15f * std::sin(t * 2.2f));
  }
  // Your banner and the next opponent's, either side of the big screen's casing (r87: an imported captain's
  // own colours and outline).
  uint32_t c1 = 0, c2 = 0;
  xrfx::team_colours(home_colour_code(v.captain, v.captainGuest), &c1, &c2);
  add_piece(scene, 0, suite_point(-2.35f, 1.8f, -2.2f), sway(suite_facing(-2.35f, -2.2f), t, 0.03f, 1.0f), 1.1f, c1, c2,
            home_motif(v.captain, v.captainGuest), 0.0f);
  if (cup.nextOpponent >= 0) {
    const std::string oppGuest = cup_captain_guest(cup.nextOpponent);
    uint32_t o1 = 0, o2 = 0;
    xrfx::team_colours(home_colour_code(cup.nextOpponent, oppGuest), &o1, &o2);
    add_piece(scene, 0, suite_point(2.35f, 1.8f, -2.2f), sway(suite_facing(2.35f, -2.2f), t, 0.03f, 1.3f), 1.1f, o1, o2,
              home_motif(cup.nextOpponent, oppGuest), 0.0f);
  }
  // The table.
  if (cup.teams > 0) {
    static const char* const kCupNames[8] = {"MUSHROOM CUP", "FLOWER CUP", "STAR CUP", "BOWSER CUP",
                                             "SUPER MUSHROOM CUP", "SUPER FLOWER CUP", "SUPER STAR CUP",
                                             "SUPER BOWSER CUP"};
    struct Text {
      std::string title, footer, names[8];
      int wins[8] = {}, losses[8] = {}, points[8] = {};
      bool mine[8] = {};
      int count = 0;
    } tx;
    tx.title = std::string(cup.trophy >= 0 && cup.trophy < 8 ? kCupNames[cup.trophy] : "TOURNAMENT") +
               (cup.rounds > 0 ? "  -  ROUND " + std::to_string(std::min(cup.round + 1, cup.rounds)) + " OF " +
                                     std::to_string(cup.rounds)
                               : "");
    // Best first: points, then wins.
    int order[8];
    for (int i = 0; i < cup.teams; ++i) order[i] = i;
    std::sort(order, order + cup.teams, [&](int a, int b) {
      return cup.points[a] != cup.points[b] ? cup.points[a] > cup.points[b] : cup.wins[a] > cup.wins[b];
    });
    tx.count = cup.teams;
    for (int i = 0; i < cup.teams; ++i) {
      const int k = order[i];
      tx.names[i] = home_name(cup.captains[k], cup_captain_guest(cup.captains[k]));
      tx.wins[i] = cup.wins[k];
      tx.losses[i] = cup.losses[k];
      tx.points[i] = cup.points[k];
      tx.mine[i] = k == cup.user;
    }
    tx.footer = cup.nextOpponent >= 0
                    ? "NEXT: YOU VS " + home_name(cup.nextOpponent, cup_captain_guest(cup.nextOpponent))
                    : "";
    request_panel(g_cupBoard, cup.generation, 768, 560, [tx](uint8_t* px, int w, int h) {
      xrtext::CupLine lines[8];
      for (int i = 0; i < tx.count; ++i) lines[i] = {tx.names[i].c_str(), tx.wins[i], tx.losses[i], tx.points[i], tx.mine[i]};
      xrtext::cup_board(px, w, h, tx.title.c_str(), lines, tx.count, tx.footer.c_str());
    });
    if (show_panel(g_cupBoard, g.cupPanel)) {
      place_panel(quads[3], g.cupPanel, suite_point(-1.3f, 1.3f, -1.4f), 0.95f);
      quads[3].pose.orientation = suite_facing(-1.3f, -1.4f);
      push_quad(quads[3]);
    }
  }
  // The home board above your trophies.
  request_home_board(v);
  if (show_panel(g_homeBoard, g.homePanel)) {
    place_panel(quads[0], g.homePanel, suite_point(1.3f, 1.52f, -1.45f), 0.7f);
    quads[0].pose.orientation = suite_facing(1.3f, -1.45f);
    push_quad(quads[0]);
  }
  // (The captain's name plaque is part of the baked room: tools/suites/suite_scene.py name_plaque.)
  // r73: a guest series' room is baked with the plaque's name left off: the guest's own name over it
  // (the builder's plaque: its middle at (0, 1.3, 2.75) in the room, its face tipped 20 degrees down
  // toward you, the name 9 cm in front of it and 7 cm up: (0, 1.19, 2.785) there).
  if (!g.suiteGuestName.empty()) {
    static AsyncPanel s_plaque;
    static Panel s_plaquePanel;
    static XrCompositionLayerQuad s_plaqueQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    const std::string name = g.suiteGuestName;
    // each series' lettering: (fill top, fill bottom, outline)
    static const struct { const char* series; uint32_t top, bottom, outline; } kStyle[] = {
        {"pokemon", 0xFFDA30, 0xF5B400, 0x1F43AE},    {"starfox", 0xFFFFFF, 0xB8C4D8, 0x1F4AB8},
        {"zelda", 0xFFE070, 0xD8A020, 0x174A1C},      {"kirby", 0xFFB0D0, 0xF070A8, 0x5A1840},
        {"metroid", 0xFFB040, 0xE06A08, 0x1A2A1A},    {"earthbound", 0xFF5A48, 0xD02018, 0x1A2A6A},
        {"iceclimber", 0xF0FAFF, 0xA8D8FF, 0x1A3A8A}, {"fireemblem", 0xFFE070, 0xD8A020, 0x6A0A10},
        {"fzero", 0xFFFFFF, 0xC0D0F0, 0x1A3AD8},      {"gamewatch", 0x101010, 0x101010, 0xB8C0A8},
        {"bowser", 0xFFA030, 0xE05008, 0x2A0A04},     {"mario_guest", 0xFF4A3A, 0xD81810, 0x101828},
        {"dk_guest", 0xFFD040, 0xD89810, 0x4A2008}};
    uint32_t top = 0xFFDA30, bottom = 0xF5B400, outline = 0x1F43AE;
    for (const auto& s : kStyle)
      if (g.suiteLoaded == s.series) {
        top = s.top;
        bottom = s.bottom;
        outline = s.outline;
      }
    const int key = static_cast<int>(std::hash<std::string>()(name + g.suiteLoaded) & 0x7FFFFFFF);
    request_panel(s_plaque, key, 1024, 256, [name, top, bottom, outline](uint8_t* px, int w, int h) {
      xrtext::plaque_name(px, w, h, name.c_str(), top, bottom, outline);
    });
    if (show_panel(s_plaque, s_plaquePanel)) {
      place_panel(s_plaqueQuad, s_plaquePanel, suite_point(0.0f, 2.785f, -1.19f), 1.6f);
      const float tip = 0.349f;  // 20 degrees: the face turned down toward you
      const XrQuaternionf down{std::sin(0.5f * tip), 0.0f, 0.0f, std::cos(0.5f * tip)};
      s_plaqueQuad.pose.orientation = q_mul(suite_facing(0.0f, -1.19f), down);
      push_quad(s_plaqueQuad);
    }
  }
}

// r61: the suite's room for a home captain (suites/suite_<name>.room, _room.jpg and _outside.jpg,
// staged from the APK by StrikersHome.java), read and decoded on a thread, then handed to xr_fx.
const char* suite_name(int captain) {
  switch (captain) {
  case 1: return "daisy";
  case 2: return "dk";
  case 5: return "luigi";
  case 7: return "peach";
  case 9: return "waluigi";
  case 10: return "wario";
  case 11: return "yoshi";
  default: return "mario";
  }
}

std::atomic<int> g_suiteLoadGen{0};  // the newest room asked for; an older load gives up

std::string suites_dir() {
  const char* home = std::getenv("STRIKERS_HOME");
  return std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/suites/";
}

// r73 (guest cup suites, Lance's list): a guest captain's series has its own suite, baked once for
// the series (tools/suites/suite_scene.py, SUITE_NAME "-"), the guest's own name written over its
// plaque here: "pokemon" for Colosseum's. "" when the series has none on this headset (then the slot's).
std::string guest_suite(int captainClass, std::string* guestName) {
  // eCharacterClass -> eTeamID (daisy 0, dk 1, luigi 2, mario 3, peach 4, waluigi 5, wario 6, yoshi 7)
  static const int kTeamOf[12] = {-1, 0, 1, -1, -1, 2, 3, 4, -1, 5, 6, 7};
  if (captainClass < 0 || captainClass >= 12 || kTeamOf[captainClass] < 0) return "";
  const int pick = PortGuestGetPick(kTeamOf[captainClass], PORT_GUEST_CAPTAIN);
  if (pick < 0) return "";
  const char* game = PortGuestRosterGame(pick);
  const char* packC = PortGuestRosterPack(pick);
  const std::string pack = packC != nullptr ? packC : "";
  std::string series;
  if (game != nullptr && std::strcmp(game, "Colosseum") == 0) {
    series = "pokemon";
  } else {
    // Melee's fighters and Charged's captains, by their world
    static const std::pair<const char*, const char*> kSeries[] = {
        {"fox", "starfox"},        {"falco", "starfox"},       {"link", "zelda"},        {"younglink", "zelda"},
        {"zelda", "zelda"},        {"sheik", "zelda"},         {"ganondorf", "zelda"},   {"samus", "metroid"},
        {"kirby", "kirby"},        {"ness", "earthbound"},     {"popo", "iceclimber"},   {"nana", "iceclimber"},
        {"marth", "fireemblem"},   {"roy", "fireemblem"},      {"falcon", "fzero"},      {"gamewatch", "gamewatch"},
        {"drmario", "mario_guest"}, {"bowser", "bowser"},      {"bowserjr", "bowser"},   {"diddykong", "dk_guest"},
        {"petey", "mario_guest"}};
    for (const auto& s : kSeries)
      if (pack == s.first) series = s.second;
  }
  if (series.empty()) return "";
  static std::unordered_map<std::string, bool> s_have;  // (a room is 3 files; the mesh says enough)
  auto it = s_have.find(series);
  if (it == s_have.end()) {
    FILE* f = std::fopen((suites_dir() + "suite_" + series + ".room").c_str(), "rb");
    if (f != nullptr) std::fclose(f);
    it = s_have.emplace(series, f != nullptr).first;
  }
  if (!it->second) return "";
  const char* name = PortGuestRosterName(pick);
  if (guestName != nullptr) *guestName = name != nullptr ? name : "";
  return series;
}

void request_suite_room(const std::string& which) {
  if (g.suiteLoaded == which) return;
  g.suiteLoaded = which;
  const int gen = ++g_suiteLoadGen;
  const std::string base = suites_dir() + "suite_" + which;
  std::thread([base, gen]() {
    const uint64_t t0 = now_ns();
    xrfx::Room room;
    // The mesh (r62, "SRM2"): vertex, index and LED counts, the lamps' colour (+1 spare), then per
    // vertex x y z u v, the normal and the shine (gloss, metal), 32-bit indices, and 6 floats per LED
    // (xrfx::Room).
    if (FILE* f = std::fopen((base + ".room").c_str(), "rb")) {
      char magic[4] = {};
      uint32_t counts[3] = {};
      float lamp[4] = {};
      if (std::fread(magic, 1, 4, f) == 4 && std::memcmp(magic, "SRM2", 4) == 0 && std::fread(counts, 4, 3, f) == 3 &&
          std::fread(lamp, 4, 4, f) == 4 && counts[0] > 0 && counts[0] < 4000000 && counts[1] > 0 &&
          counts[1] < 12000000 && counts[1] % 3 == 0 && counts[2] < 4096) {
        room.verts.resize(static_cast<size_t>(counts[0]) * xrfx::Room::kRoomFloats);
        room.indices.resize(counts[1]);
        room.leds.resize(static_cast<size_t>(counts[2]) * xrfx::Room::kLedFloats);
        const bool read = std::fread(room.verts.data(), sizeof(float), room.verts.size(), f) == room.verts.size() &&
                          std::fread(room.indices.data(), 4, room.indices.size(), f) == room.indices.size() &&
                          std::fread(room.leds.data(), sizeof(float), room.leds.size(), f) == room.leds.size();
        const bool inRange = read && std::all_of(room.indices.begin(), room.indices.end(),
                                                 [&](uint32_t i) { return i < counts[0]; });
        if (inRange) {
          std::memcpy(room.lamp, lamp, sizeof(room.lamp));
        } else {
          room.verts.clear();
          room.indices.clear();
          room.leds.clear();
        }
      }
      std::fclose(f);
    }
    if (room.indices.empty()) {
      XWARN("cup suite: no room in %s.room", base.c_str());
      return;
    }
    auto load = [](const std::string& path, std::vector<uint8_t>& out, uint32_t& w, uint32_t& h) {
      int iw = 0, ih = 0, n = 0;
      unsigned char* px = stbi_load(path.c_str(), &iw, &ih, &n, 4);
      if (px == nullptr) return false;
      out.assign(px, px + static_cast<size_t>(iw) * ih * 4);
      w = static_cast<uint32_t>(iw);
      h = static_cast<uint32_t>(ih);
      stbi_image_free(px);
      return true;
    };
    if (!load(base + "_room.jpg", room.rgba, room.width, room.height)) {
      XWARN("cup suite: could not read %s_room.jpg", base.c_str());
      return;
    }
    if (!load(base + "_outside.jpg", room.outside, room.outsideWidth, room.outsideHeight)) {
      XWARN("cup suite: could not read %s_outside.jpg (the window shows black)", base.c_str());
    }
    if (gen != g_suiteLoadGen.load()) return;  // another captain's asked for since
    XLOG("cup suite: %s read in %.0f ms (%zu vertices, %zu LEDs, light %ux%u, outside %ux%u)", base.c_str(),
         static_cast<double>(now_ns() - t0) / 1e6, room.verts.size() / xrfx::Room::kRoomFloats,
         room.leds.size() / xrfx::Room::kLedFloats, room.width, room.height, room.outsideWidth, room.outsideHeight);
    xrfx::set_room(std::move(room));
  }).detach();
}

// r61: in a cup between its games the view goes to the suite (VR, big screen), the one change of view
// the game makes on its own (Lance's OK); your own view comes back for the matches. A stick double
// click there takes you out of it until the cup's hub is left.
void update_suite_mode() {
  const xrhome::Cup cup = xrhome::cup();
  if (!cup.inHub) g.suiteDismissed = false;
  const bool want = cup.inHub && !g.suiteDismissed && !g.stereoFailed;
  if (want == g.inSuite) return;
  if (want) {
    g.suiteSavedMr = g.mrMode;
    g.suiteSavedTabletop = g.tabletop;
    g.inSuite = true;
    g.tabletop = false;
    set_mr_mode(false);
    // The suite stands where you are, its window the way you face (level).
    const XrQuaternionf& o = g.headPose.orientation;
    const float fx = -(2 * (o.x * o.z + o.w * o.y)), fz = -(1 - 2 * (o.x * o.x + o.y * o.y));
    g.suiteYaw = g.headValid && (fx * fx + fz * fz) > 1e-6f ? std::atan2(-fx, -fz) : 0.0f;
    g.suiteOrigin = g.headValid ? XrVector3f{g.headPose.position.x, g.floorY, g.headPose.position.z}
                                : XrVector3f{0.0f, g.floorY, 0.0f};
    XLOG("cup suite: on (cup %d, round %d of %d), at (%.2f, %.2f) facing %.0f deg", cup.trophy, cup.round + 1,
         cup.rounds, g.suiteOrigin.x, g.suiteOrigin.z, g.suiteYaw * 57.29578f);
  } else {
    g.inSuite = false;
    g.tabletop = g.suiteSavedTabletop;
    set_mr_mode(g.suiteSavedMr);
    // Its room goes (about 90 MB of pictures) until the next time.
    ++g_suiteLoadGen;
    g.suiteCaptain = -2;
    g.suiteLoaded.clear();
    if (g.fxReady) xrfx::set_room(xrfx::Room{});
    XLOG("cup suite: off, back to %s", mode_name());
  }
}

// r60 (Lance: "a way to hover over the achievements youve earned in that trophy room area, so that
// you can see the ones you earned and have yet to earn"): the badge under either pointer on the board
// (the grid xrtext::badge_board draws), outlined (gold earned, white not yet), with a card for it
// (title, description, earned or not and how far along) just in front of the board over the badge,
// drawn on a thread when the pointed-at badge changes.
void update_board_hover(const XrCompositionLayerQuad& board, xrfx::Scene& scene,
                        const XrCompositionLayerBaseHeader** layers, uint32_t* layerCount, XrCompositionLayerQuad& tip) {
  const int count = static_cast<int>(g.boardRows.size());
  if (count == 0 || g.boardH == 0) {
    g.boardHover = -1;
    return;
  }
  const float w = static_cast<float>(kBoardW), h = static_cast<float>(g.boardH);
  const int rows = (count + kBoardColumns - 1) / kBoardColumns;
  const float header = 0.13f * h, margin = 0.04f * w;
  const float cell = std::min((w - 2.0f * margin) / kBoardColumns, (h - header - 2.0f * margin) / rows);
  const float x0 = 0.5f * (w - cell * kBoardColumns), y0 = header + margin;
  int hover = -1;
  XrVector3f hit{0.0f, 0.0f, 0.0f};
  int pointer = -1;
  for (int hand = 1; hand >= 0 && hover < 0; --hand) {  // the right pointer first
    if (!g.aimValid[hand]) continue;
    float qx = 0, qy = 0, dist = 0;
    if (!ray_quad(g.aim[hand], board.pose, &qx, &qy, &dist)) continue;
    const float px = (qx / board.size.width + 0.5f) * w, py = (0.5f - qy / board.size.height) * h;
    const int col = static_cast<int>(std::floor((px - x0) / cell)), row = static_cast<int>(std::floor((py - y0) / cell));
    if (col < 0 || col >= kBoardColumns || row < 0 || row >= rows || row * kBoardColumns + col >= count) continue;
    hover = row * kBoardColumns + col;
    const XrVector3f dir = q_rot(g.aim[hand].orientation, XrVector3f{0.0f, 0.0f, -1.0f});
    hit = {g.aim[hand].position.x + dir.x * dist, g.aim[hand].position.y + dir.y * dist,
           g.aim[hand].position.z + dir.z * dist};
    pointer = hand;
  }
  if (hover != g.boardHover) {
    g.boardHover = hover;
    if (hover >= 0) {
      const xrra::AchievementRow& r = g.boardRows[static_cast<size_t>(hover)];
      std::string head = r.unlocked ? "EARNED" : "NOT EARNED YET";
      if (!r.unlocked && !r.progress.empty()) head += "  " + r.progress;
      const int gen = ++g.tipGen;
      g.tipShownFor = -1;
      std::thread([head, title = r.title, desc = r.description, points = r.points, gen]() {
        std::vector<uint8_t> px(static_cast<size_t>(kTipW) * kTipH * 4);
        xrtext::achievement_toast(px.data(), static_cast<int>(kTipW), static_cast<int>(kTipH), head.c_str(),
                                  title.c_str(), desc.c_str(), points);
        std::lock_guard<std::mutex> lock(g_tipMutex);
        g_tipPixels = std::move(px);
        g_tipReadyGen = gen;
      }).detach();
      g.tipFor = hover;
    }
  }
  if (g.tipGen > 0 && g_tipReadyGen.load() == g.tipGen && g.tipShownGen != g.tipGen) {
    std::lock_guard<std::mutex> lock(g_tipMutex);
    panel_upload(g.tip, g_tipPixels.data(), kTipW, kTipH);
    g.tipShownGen = g.tipGen;
    g.tipShownFor = g.tipFor;
  }
  if (hover < 0) return;
  // The badge's middle and size on the board, in the room.
  const int col = hover % kBoardColumns, row = hover / kBoardColumns;
  const float cx = x0 + (col + 0.5f) * cell, cy = y0 + (row + 0.5f) * cell;
  const XrVector3f local{(cx / w - 0.5f) * board.size.width, (0.5f - cy / h) * board.size.height, 0.006f};
  const XrVector3f off = q_rot(board.pose.orientation, local);
  const XrVector3f centre{board.pose.position.x + off.x, board.pose.position.y + off.y, board.pose.position.z + off.z};
  const float tile = 0.92f * cell / w * board.size.width;
  const bool earned = g.boardRows[static_cast<size_t>(hover)].unlocked;
  if (scene.frameCount < 4) {
    xrfx::Scene::Frame& f = scene.frames[scene.frameCount++];
    f.pose = board.pose;
    f.pose.position = centre;
    f.thickness = 0.004f;
    f.width = f.height = tile + 2.0f * f.thickness;
    f.rgb = earned ? 0xFFD23F : 0xE8F0FF;
    f.alpha = 1.0f;
  }
  if (pointer >= 0) {
    scene.pointerValid[pointer] = true;
    scene.pointers[pointer] = hit;
  }
  if (g.tip.hasImage && g.tipShownFor == hover && *layerCount < 24) {
    const XrVector3f up = q_rot(board.pose.orientation, XrVector3f{0.0f, 0.5f * tile + 0.075f, 0.06f});
    place_panel(tip, g.tip, XrVector3f{centre.x + up.x, centre.y + up.y, centre.z + up.z}, 0.36f);
    layers[(*layerCount)++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&tip);
  }
}

// ---------------------------------------------------------------------------------------------
// 3D modes: eye images shared with Dawn, matrices, and the per-eye pass contract with glPlat.cpp
// ---------------------------------------------------------------------------------------------

// Which 3D layers the current mode draws. The flags match glPlat.cpp's XR_LAYER_*.
enum LayerKind { kLayerStadium, kLayerTable, kLayerScorer };
constexpr int kFlagSkipActors = 1;
constexpr int kFlagSkipSky = 2;
constexpr int kFlagTable = 4;
constexpr int kFlagScorer = 8;  // r42: the life-size scorer alone (glPlat.cpp XR_LAYER_SCORER)

// r61: the cup suite is VR without the game's 3D eyes (its own room around the big screen).
bool stereo_mode() { return (g.tabletop || !g.mrMode) && !g.inSuite; }

// r42: the life-size scorer is out (and was reported lately: a game that stops reporting hides it).
bool scorer_shown() {
  return g.scorer.phase != 0 && now_ns() - g.scorer.reportNs < 300000000ull && !g.stereoFailed;
}
// The modes with eye passes: the 3D modes, and every mode while the scorer is out.
bool eye_mode() { return stereo_mode() || scorer_shown(); }

// r43: Tabletop · Stadium's life-size stadium in its own layer, one eye a game frame; r87: Jumbotron ·
// Stadium's too (both Stadium modes: not over passthrough).
bool split_mode() {
  return g.singlePass && g.stadiumSplit && !g.mrMode && stereo_mode() && (g.tabletop || g.jumboSplit);
}

int world_layers() {
  if (!stereo_mode()) return 0;
  if (g.tabletop && !g.mrMode) return 2;  // Tabletop · Stadium: the stadium around you, then the field
  return 1;                               // Tabletop · Living Room (field) or Jumbotron · Stadium (stadium)
}

int layer_count() { return world_layers() + (scorer_shown() ? 1 : 0); }

LayerKind layer_kind(int layer) {
  if (layer >= world_layers()) return kLayerScorer;  // r42: always last, over the world
  if (g.tabletop && g.mrMode) return kLayerTable;
  if (g.tabletop && !g.mrMode) return layer == 0 ? kLayerStadium : kLayerTable;
  return kLayerStadium;
}

// Row-vector 4x4 in the game's nlMatrix4 convention: v' = v * M, translation in row 3.
struct M4 {
  float m[4][4];
};

M4 m4_identity() {
  M4 r{};
  for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
  return r;
}

M4 m4_mul(const M4& a, const M4& b) {
  M4 r{};
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j)
      for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
  return r;
}

// Inverse of a rotation + translation in the row form above.
M4 m4_rigid_inverse(const M4& a) {
  M4 r = m4_identity();
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[j][i];
  for (int j = 0; j < 3; ++j) r.m[3][j] = -(a.m[3][0] * r.m[0][j] + a.m[3][1] * r.m[1][j] + a.m[3][2] * r.m[2][j]);
  return r;
}

// Column-vector rotation matrix of a unit quaternion.
void quat_to_rc(const XrQuaternionf& q, float rc[3][3]) {
  const float x = q.x, y = q.y, z = q.z, w = q.w;
  rc[0][0] = 1 - 2 * (y * y + z * z); rc[0][1] = 2 * (x * y - w * z);     rc[0][2] = 2 * (x * z + w * y);
  rc[1][0] = 2 * (x * y + w * z);     rc[1][1] = 1 - 2 * (x * x + z * z); rc[1][2] = 2 * (y * z - w * x);
  rc[2][0] = 2 * (x * z - w * y);     rc[2][1] = 2 * (y * z + w * x);     rc[2][2] = 1 - 2 * (x * x + y * y);
}

// LOCAL space -> eye space, i.e. the inverse of the eye's pose: rotation part Rc, translation -p*Rc.
M4 m4_view_from_pose(const XrPosef& pose) {
  float rc[3][3];
  quat_to_rc(pose.orientation, rc);
  M4 r = m4_identity();
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) r.m[i][j] = rc[i][j];
  const float p[3] = {pose.position.x, pose.position.y, pose.position.z};
  for (int j = 0; j < 3; ++j) r.m[3][j] = -(p[0] * rc[0][j] + p[1] * rc[1][j] + p[2] * rc[2][j]);
  return r;
}

// Game world -> LOCAL space. The game is Z-up with the pitch in the XY plane; the room is Y-up
// looking down -Z. (x, y, z) -> (x, z, -y) keeps right-handedness and puts the game's +Y (across
// the pitch, away from the TV camera's side) in front of you. Then scale, turn about the room's
// up axis, and move to `origin`.
M4 m4_game_to_room(float scale, float yaw, const XrVector3f& origin) {
  // Row form of (x, y, z) -> (x, z, -y).
  const float gr[3][3] = {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}};
  // Row form of a rotation by `yaw` about +Y.
  const float c = std::cos(yaw), s = std::sin(yaw);
  const float yr[3][3] = {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};
  M4 r = m4_identity();
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) {
      float v = 0;
      for (int k = 0; k < 3; ++k) v += gr[i][k] * yr[k][j];
      r.m[i][j] = scale * v;
    }
  r.m[3][0] = origin.x;
  r.m[3][1] = origin.y;
  r.m[3][2] = origin.z;
  return r;
}

// GXSetProjection's perspective layout (C_MTXPerspective rows, clip z in [-w, 0]) from the eye's
// asymmetric field of view. Near/far are room meters.
void gx_projection(const XrFovf& fov, float n, float f, float out[16]) {
  const float l = std::tan(fov.angleLeft), r = std::tan(fov.angleRight);
  const float u = std::tan(fov.angleUp), d = std::tan(fov.angleDown);
  std::memset(out, 0, sizeof(float) * 16);
  out[0 * 4 + 0] = 2.0f / (r - l);
  out[0 * 4 + 2] = (r + l) / (r - l);
  out[1 * 4 + 1] = 2.0f / (u - d);
  out[1 * 4 + 2] = (u + d) / (u - d);
  out[2 * 4 + 2] = -n / (f - n);
  out[2 * 4 + 3] = -(f * n) / (f - n);
  out[3 * 4 + 2] = -1.0f;
}

// The stadium layer's placement: you stand on the near touchline at centre field, feet on the
// floor, facing across the pitch. STRIKERS_XR_STADIUM_SCALE overrides the game-unit size (1 = 1 m).
float stadium_scale() {
  static float s_scale = 0.0f;
  if (s_scale == 0.0f) {
    const char* e = std::getenv("STRIKERS_XR_STADIUM_SCALE");
    s_scale = (e != nullptr && *e != '\0') ? static_cast<float>(std::atof(e)) : 1.0f;
    if (s_scale <= 0.0f) s_scale = 1.0f;
  }
  return s_scale;
}

// Where you stand in the life-size stadium: game y = kStandY, on the pitch 1.6 m inside the near
// touchline (the pitch is 24.2 wide), facing across it. r4-r10 stood at -15.5, just outside the
// electric fence, whose wall then filled the near view.
constexpr float kStandY = -10.5f;

// r44g: the life-size stadium's ground: your floor, moved down by the height you picked in the pause
// settings (or up, for less), so a seated or a short player can see the pitch as from standing.
float stadium_floor_y() { return g.floorY - 0.01f * static_cast<float>(g.viewHeightCm); }

// The floor things stand on in the current mode: the room's, or the stadium's.
float mode_floor_y() { return g.mrMode ? g.floorY : stadium_floor_y(); }

// r44h (Lance: Waluigi "was still 5 feet tall ... i want the main characters to be taller than 5 feet,
// like walugi should be like 6'5" DK should be the same", and Daisy taller): the life-size scorer's
// height in meters by eCharacterClass. r42-r44g stood every one at PORT_SCORER_HEIGHT (5 ft).
float scorer_height_m() {
  static const float kHeights[12] = {
      1.73f,  // Birdo        5'8"
      1.78f,  // Daisy        5'10"
      1.96f,  // Donkey Kong  6'5"
      1.70f,  // Hammer Bro   5'7"
      1.57f,  // Koopa        5'2"
      1.83f,  // Luigi        6'0"
      1.73f,  // Mario        5'8"
      1.80f,  // Peach        5'11"
      1.42f,  // Toad         4'8"
      1.96f,  // Waluigi      6'5"
      1.75f,  // Wario        5'9"
      1.78f,  // Yoshi        5'10"
  };
  const int c = g.scorer.cls;
  return c >= 0 && c < 12 ? kHeights[c] : PORT_SCORER_HEIGHT;
}

M4 stadium_model() {
  // Game (0, kStandY, 0) lands on the floor under the start pose, whatever the (self-test) turn:
  // the stand point maps to (0, 0, -kStandY) before the turn and scale.
  const float s = stadium_scale(), yaw = g.stadiumYawDebug;
  const float c = -kStandY;
  // r55: the life-size stadium shakes with a blast (the Jumbotron · Stadium view; with the table, the
  // table does).
  const float shake = g.tabletop ? 0.0f : 1.0f;
  return m4_game_to_room(s, yaw,
                         XrVector3f{-c * std::sin(yaw) * s + shake * g.joltPos.x, stadium_floor_y() + shake * g.joltPos.y,
                                    -c * std::cos(yaw) * s + shake * g.joltPos.z});
}

// r44: where the field is shown: where you put it, raised while the scorer celebrates (tableLift).
XrVector3f table_shown_pos() {
  // r55: plus the jolt of an item blast (step_jolt).
  return XrVector3f{g.table.position.x + g.joltPos.x, g.table.position.y + g.tableLift + g.joltPos.y,
                    g.table.position.z + g.joltPos.z};
}

// r55: the jolt's spring, once a display frame: about 3.3 shakes a second, gone in about half a
// second.
void step_jolt() {
  const uint64_t t = now_ns();
  const float dt = g.lastJoltNs != 0 ? std::min(0.05f, static_cast<float>(t - g.lastJoltNs) / 1e9f) : 0.0f;
  g.lastJoltNs = t;
  if (dt <= 0.0f) return;
  constexpr float k = 420.0f, c = 12.0f;
  float* p[3] = {&g.joltPos.x, &g.joltPos.y, &g.joltPos.z};
  float* v[3] = {&g.joltVel.x, &g.joltVel.y, &g.joltVel.z};
  for (int a = 0; a < 3; ++a) {
    // Small steps, so a big kick never blows the spring up.
    for (int i = 0; i < 4; ++i) {
      const float h = dt / 4.0f;
      *v[a] += (-k * *p[a] - c * *v[a]) * h;
      *p[a] += *v[a] * h;
    }
    if (std::fabs(*p[a]) < 1e-5f && std::fabs(*v[a]) < 1e-4f) *p[a] = *v[a] = 0.0f;
  }
}

M4 table_model() { return m4_game_to_room(g.table.scale, g.table.yaw, table_shown_pos()); }

// --- r42: the life-size goal scorer in the room ---

// Forward along the floor from a pose (unit, LOCAL x/z).
void floor_forward(const XrPosef& pose, float* fx, float* fz) {
  const XrQuaternionf& o = pose.orientation;
  float x = -(2 * (o.x * o.z + o.w * o.y)), z = -(1 - 2 * (o.x * o.x + o.y * o.y));
  const float len = std::max(1e-3f, std::sqrt(x * x + z * z));
  *fx = x / len;
  *fz = z / len;
}

// The yaw m4_game_to_room needs to turn the game's +X (a character's forward) onto (dx, dz).
float yaw_facing(float dx, float dz) { return std::atan2(-dz, dx); }

float turn_toward(float from, float to, float u) {
  float d = to - from;
  while (d > 3.14159265f) d -= 6.2831853f;
  while (d < -3.14159265f) d += 6.2831853f;
  return from + d * std::max(0.0f, std::min(1.0f, u));
}

XrVector3f table_point(float gx, float gy, float gz);

// A show's path, from where you are when it starts: the spot is out in front (below the big screen)
// or beside the table, and the scorer comes from farther out, running in at PORT_SCORER_SPEED, and
// leaves the same way.
void plan_scorer() {
  const XrPosef& head = g.headPose;
  float fx = 0.0f, fz = -1.0f;
  if (g.headValid) floor_forward(head, &fx, &fz);
  // Turning a floor direction left by a (LOCAL: -Z forward, +X right).
  auto rotate = [](float x, float z, float a, float* ox, float* oz) {
    *ox = x * std::cos(a) + z * std::sin(a);
    *oz = -x * std::sin(a) + z * std::cos(a);
  };
  // r43b (Lance: "stopping at or near center field (usually where the game screen is placed) and not
  // getting so close"): the spot is out in front, toward the big screen (Jumbotron) or just beyond
  // the table (Tabletop), never nearer than 2 m. r42 had it beside you at 1.5 m.
  float tx = 0.0f, tz = 0.0f, want = 0.0f;
  if (!g.tabletop || g.stereoFailed) {
    const FreeScreen& s = g.screens[screen_room()];
    if (s.placed) {
      tx = s.position.x - head.position.x;
      tz = s.position.z - head.position.z;
    } else {
      tx = fx * screen_preset(screen_room()).distance;
      tz = fz * screen_preset(screen_room()).distance;
    }
    // A little in front of the screen, so it stands below it rather than inside it.
    want = std::max(2.5f, std::min(g.mrMode ? 4.0f : 8.0f, std::sqrt(tx * tx + tz * tz) - 0.4f));
  } else {
    // r46e (players: the field floating up out of the way for the show; Lance: "the goal scorer
    // stands next to the tabletop field"): the field stays where you put it, and the scorer stands
    // beside it, life-size, level with its middle, turned to you, a side per goal, alternating (r50:
    // on a Quest 2 too, its screen is over the field again). It runs in from that side and a little
    // beyond, and leaves the same way.
    // r43b-r46d stood it beyond the field, which floated up above your view meanwhile (tableLift).
    const XrVector3f t = g.table.position;
    float ux = t.x - head.position.x, uz = t.z - head.position.z;
    float ulen = std::sqrt(ux * ux + uz * uz);
    if (ulen < 0.2f) {
      ux = fx;
      uz = fz;
      ulen = 1.0f;
    }
    ux /= ulen;
    uz /= ulen;
    const float flip = (g.scorer.shows++ & 1) != 0 ? -1.0f : 1.0f;
    const float side = flip;
    const float sx = -uz * side, sz = ux * side;  // the side it stands on (+1 = your right)
    float reach = 0.0f;                           // how far the field reaches that way
    for (int i = 0; i < 4; ++i) {
      const XrVector3f c = table_point((i & 1) ? 20.6f : -20.6f, (i & 2) ? 12.1f : -12.1f, 0.0f);
      reach = std::max(reach, (c.x - t.x) * sx + (c.z - t.z) * sz);
    }
    const float out = reach + 0.45f;
    XrVector3f spot{t.x + sx * out, mode_floor_y(), t.z + sz * out};
    // Not right on top of you with the field close by.
    const float hx = spot.x - head.position.x, hz = spot.z - head.position.z;
    const float fromHead = std::sqrt(hx * hx + hz * hz);
    if (fromHead < 2.5f) {  // r49c: 2.5 m, as scorer_place keeps it (r49 1.5, r46e 1.4)
      spot.x += ux * (2.5f - fromHead);
      spot.z += uz * (2.5f - fromHead);
    }
    g.scorer.spot = spot;
    float ax = sx + 0.6f * ux, az = sz + 0.6f * uz;
    const float alen = std::max(1e-3f, std::sqrt(ax * ax + az * az));
    ax /= alen;
    az /= alen;
    g.scorer.away = {ax, 0.0f, az};
    const float run = PORT_SCORER_SPEED * (PORT_SCORER_ARRIVE - PORT_SCORER_APPEAR);
    g.scorer.spawn = {spot.x + ax * run, spot.y, spot.z + az * run};
    g.scorer.planned = true;
    XLOG("scorer: beside the field on your %s, spot (%.2f, %.2f, %.2f), field reach %.2f m, from (%.2f, %.2f)",
         side > 0.0f ? "right" : "left", spot.x, spot.y, spot.z, reach, g.scorer.spawn.x, g.scorer.spawn.z);
    return;
  }
  const float tlen = std::sqrt(tx * tx + tz * tz);
  float dx = fx, dz = fz;
  if (tlen > 0.2f) {
    dx = tx / tlen;
    dz = tz / tlen;
  }
  g.scorer.spot = {head.position.x + dx * want, mode_floor_y(), head.position.z + dz * want};  // r44g
  // It comes in from farther out, a little to one side (alternating per goal), and leaves that way.
  const float side = (g.scorer.shows++ & 1) != 0 ? -1.0f : 1.0f;
  float ax = 0.0f, az = 0.0f;
  rotate(dx, dz, 0.35f * side, &ax, &az);
  g.scorer.away = {ax, 0.0f, az};
  const float run = PORT_SCORER_SPEED * (PORT_SCORER_ARRIVE - PORT_SCORER_APPEAR);
  g.scorer.spawn = {g.scorer.spot.x + ax * run, g.scorer.spot.y, g.scorer.spot.z + az * run};
  g.scorer.planned = true;
  XLOG("scorer: spot (%.2f, %.2f, %.2f) %.1f m out, from (%.2f, %.2f)", g.scorer.spot.x, g.scorer.spot.y,
       g.scorer.spot.z, want, g.scorer.spawn.x, g.scorer.spawn.z);
}

// Where the scorer's feet are and which way it faces, now.
void scorer_place(XrVector3f* pos, float* yaw) {
  auto& sc = g.scorer;
  const float runIn = PORT_SCORER_ARRIVE - PORT_SCORER_APPEAR;
  // Facing you from where it stands.
  auto toward_head = [&](const XrVector3f& p) {
    return yaw_facing(g.headPose.position.x - p.x, g.headPose.position.z - p.z);
  };
  if (sc.phase == 1) {
    const float u = std::max(0.0f, std::min(1.0f, sc.phaseTime / runIn));
    *pos = {sc.spawn.x + (sc.spot.x - sc.spawn.x) * u, sc.spot.y, sc.spawn.z + (sc.spot.z - sc.spawn.z) * u};
    const float run = yaw_facing(-sc.away.x, -sc.away.z);
    // The last 0.35 s it turns to face you.
    *yaw = turn_toward(run, toward_head(sc.spot), (sc.phaseTime - (runIn - 0.35f)) / 0.35f);
  } else if (sc.phase == 2) {
    *pos = sc.spot;
    *yaw = toward_head(sc.spot);
  } else {
    const float t = sc.phaseTime;
    *pos = {sc.leaveFrom.x + sc.away.x * PORT_SCORER_SPEED * t, sc.spot.y,
            sc.leaveFrom.z + sc.away.z * PORT_SCORER_SPEED * t};
    // A quick turn away first.
    *yaw = turn_toward(toward_head(sc.leaveFrom), yaw_facing(sc.away.x, sc.away.z), t / 0.3f);
  }
  // r49 (a player: "sometimes the characters get way too close during the highlight. Koopa and Peach
  // nearly got within an inch of my face"): never nearer than kScorerPersonalSpace to your head, along
  // the floor, wherever you have moved since its path was planned (it is planned once, from where you
  // stood at the goal), and on the way in and out too. It slides round you instead of through you.
  constexpr float kScorerPersonalSpace = 2.5f;  // r49c (Lance: 1.5 m "was still close af"): 2.5 m
  if (g.headValid) {
    const float hx = pos->x - g.headPose.position.x, hz = pos->z - g.headPose.position.z;
    const float d = std::sqrt(hx * hx + hz * hz);
    if (d < kScorerPersonalSpace) {
      float ux = hx, uz = hz;
      if (d < 1e-3f) {  // right on top of you: back off the way it faces
        ux = std::cos(*yaw);
        uz = -std::sin(*yaw);
      }
      const float ul = std::max(1e-3f, std::sqrt(ux * ux + uz * uz));
      pos->x = g.headPose.position.x + ux / ul * kScorerPersonalSpace;
      pos->z = g.headPose.position.z + uz / ul * kScorerPersonalSpace;
      if (sc.phase == 2) *yaw = toward_head(*pos);
    }
  }
  sc.lastPos = *pos;
  sc.lastYaw = *yaw;
}

// r43c (Lance: "a separate drop down jumbotron for the replay like waaay above the head of the goal
// scorer"): how far a screen centred at `center` must rise so that, seen from your head, its bottom
// edge clears the scorer's banner. 0 when the screen is off to the side of the scorer.
float screen_lift_over_scorer(const XrVector3f& center, float width, float height) {
  const XrVector3f& head = g.headPose.position;
  const float sx = g.scorer.spot.x - head.x, sz = g.scorer.spot.z - head.z;
  const float qx = center.x - head.x, qz = center.z - head.z;
  const float ds = std::sqrt(sx * sx + sz * sz), dq = std::sqrt(qx * qx + qz * qz);
  if (ds < 0.5f || dq < 0.5f) return 0.0f;
  const float bannerW = 1.1f * std::max(1.0f, ds / 2.5f);  // as drawn (below, PortXrEndFrame)
  const float bannerTop = g.scorer.spot.y + scorer_height_m() + 0.18f +
                          bannerW * static_cast<float>(kBannerH) / static_cast<float>(kBannerW);
  // Side by side already: the angle between them beats their half widths.
  const float cosAngle = std::max(-1.0f, std::min(1.0f, (sx * qx + sz * qz) / (ds * dq)));
  if (std::acos(cosAngle) > std::atan(0.5f * width / dq) + std::atan(0.5f * bannerW / ds)) return 0.0f;
  const float bottomNeeded = head.y + (bannerTop - head.y + 0.3f) * dq / ds;
  return std::max(0.0f, bottomNeeded - (center.y - 0.5f * height));
}

M4 scorer_model() {
  XrVector3f pos{};
  float yaw = 0.0f;
  scorer_place(&pos, &yaw);
  const float scale = scorer_height_m() / std::max(0.2f, g.scorer.height);  // r44h: by character
  return m4_game_to_room(scale, yaw, pos);
}

// A game point on the Tabletop field, in LOCAL space.
XrVector3f table_point(float gx, float gy, float gz) {
  const M4 t = table_model();
  return XrVector3f{gx * t.m[0][0] + gy * t.m[1][0] + gz * t.m[2][0] + t.m[3][0],
                    gx * t.m[0][1] + gy * t.m[1][1] + gz * t.m[2][1] + t.m[3][1],
                    gx * t.m[0][2] + gy * t.m[1][2] + gz * t.m[2][2] + t.m[3][2]};
}

uint32_t find_device_local(uint32_t typeBits) {
  for (uint32_t i = 0; i < g.memProps.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) != 0 && (g.memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      return i;
    }
  }
  for (uint32_t i = 0; i < g.memProps.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) != 0) return i;
  }
  return UINT32_MAX;
}

// One exported eye image (TPVR's createExportedImage, trimmed). Same VkImageCreateInfo on both
// sides; MUTABLE + a two-entry format list so Dawn can render through an RGBA8Unorm view of the
// sRGB image while Adreno keeps its framebuffer compression. r43: `width` is two eyes wide for
// single-pass stereo.
bool create_eye_image(EyeImage& out, VkFormat srgb, VkFormat unorm, uint32_t width) {
  const VkFormat viewFormats[2] = {srgb, unorm};
  VkImageFormatListCreateInfo formatList{VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
  formatList.viewFormatCount = 2;
  formatList.pViewFormats = viewFormats;
  VkExternalMemoryImageCreateInfo extInfo{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
  extInfo.pNext = &formatList;
  extInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  ci.pNext = &extInfo;
  ci.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
  ci.imageType = VK_IMAGE_TYPE_2D;
  ci.format = srgb;
  ci.extent = {width, g.eyeHeight, 1};
  ci.mipLevels = 1;
  ci.arrayLayers = 1;
  ci.samples = VK_SAMPLE_COUNT_1_BIT;
  ci.tiling = VK_IMAGE_TILING_OPTIMAL;
  ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
             VK_IMAGE_USAGE_SAMPLED_BIT;
  ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!vk_ok(vkCreateImage(g.device, &ci, nullptr, &out.image), "vkCreateImage(eye)")) return false;

  VkMemoryDedicatedRequirements dedicatedReqs{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};
  VkMemoryRequirements2 reqs2{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicatedReqs};
  VkImageMemoryRequirementsInfo2 reqInfo{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};
  reqInfo.image = out.image;
  vkGetImageMemoryRequirements2(g.device, &reqInfo, &reqs2);
  const VkMemoryRequirements& reqs = reqs2.memoryRequirements;
  const uint32_t typeIndex = find_device_local(reqs.memoryTypeBits);
  if (typeIndex == UINT32_MAX) {
    XERR("no memory type for the eye image");
    return false;
  }
  VkMemoryDedicatedAllocateInfo dedicated{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
  dedicated.image = out.image;
  VkExportMemoryAllocateInfo exportInfo{VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO, &dedicated};
  exportInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &exportInfo};
  alloc.allocationSize = reqs.size;
  alloc.memoryTypeIndex = typeIndex;
  if (!vk_ok(vkAllocateMemory(g.device, &alloc, nullptr, &out.memory), "vkAllocateMemory(eye)") ||
      !vk_ok(vkBindImageMemory(g.device, out.image, out.memory, 0), "vkBindImageMemory(eye)")) {
    return false;
  }

  static PFN_vkGetMemoryFdKHR getMemoryFd = nullptr;
  if (getMemoryFd == nullptr) {
    getMemoryFd = reinterpret_cast<PFN_vkGetMemoryFdKHR>(vkGetDeviceProcAddr(g.device, "vkGetMemoryFdKHR"));
  }
  if (getMemoryFd == nullptr) {
    XERR("vkGetMemoryFdKHR unavailable");
    return false;
  }
  VkMemoryGetFdInfoKHR fdInfo{VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR};
  fdInfo.memory = out.memory;
  fdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
  int fd = -1;
  if (!vk_ok(getMemoryFd(g.device, &fdInfo, &fd), "vkGetMemoryFdKHR") || fd < 0) return false;

  // Dawn side, inside an error scope: Aurora treats any uncaptured Dawn error as fatal.
  auto& device = aurora::webgpu::g_device;
  device.PushErrorScope(wgpu::ErrorFilter::Validation);
  {
    wgpu::SharedTextureMemoryOpaqueFDDescriptor fdDesc{};
    fdDesc.vkImageCreateInfo = &ci;
    fdDesc.memoryFD = fd;
    fdDesc.memoryTypeIndex = typeIndex;
    fdDesc.allocationSize = reqs.size;
    fdDesc.dedicatedAllocation = true;
    wgpu::SharedTextureMemoryDescriptor desc{};
    desc.nextInChain = &fdDesc;
    out.shared = device.ImportSharedTextureMemory(&desc);
    const wgpu::TextureFormat viewFormat = aurora::gfx::color_format();
    wgpu::TextureDescriptor tex{};
    tex.format = wgpu::TextureFormat::RGBA8UnormSrgb;
    tex.size = {width, g.eyeHeight, 1};
    tex.usage = wgpu::TextureUsage::CopyDst | wgpu::TextureUsage::CopySrc | wgpu::TextureUsage::RenderAttachment |
                wgpu::TextureUsage::TextureBinding;
    tex.viewFormatCount = 1;
    tex.viewFormats = &viewFormat;
    if (out.shared) out.texture = out.shared.CreateTexture(&tex);
    if (out.texture) {
      wgpu::TextureViewDescriptor view{};
      view.format = viewFormat;
      view.dimension = wgpu::TextureViewDimension::e2D;
      view.mipLevelCount = 1;
      view.arrayLayerCount = 1;
      out.renderView = out.texture.CreateView(&view);
    }
  }
  close(fd);  // Dawn dup()s it on import
  bool done = false, failed = false;
  std::string message;
  const auto future = device.PopErrorScope(
      wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus status, wgpu::ErrorType type, wgpu::StringView msg) {
        done = true;
        if (status != wgpu::PopErrorScopeStatus::Success || type != wgpu::ErrorType::NoError) {
          failed = true;
          message = std::string{std::string_view{msg}};
        }
      });
  aurora::webgpu::g_instance.WaitAny(future, 5000000000ull);
  if (!done || failed || !out.texture || !out.renderView) {
    XERR("Dawn rejected the eye image import: %s", done ? message.c_str() : "error scope timed out");
    return false;
  }
  VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  return vk_ok(vkCreateSemaphore(g.device, &semInfo, nullptr, &out.dawnDone), "vkCreateSemaphore(eye)");
}

bool create_stereo_resources() {
  if (g.stereoFailed) return false;
  if (g.eyeSwapchain[0] != XR_NULL_HANDLE) return true;
  auto fail = [](const char* why) {
    XERR("3D modes unavailable: %s", why);
    g.stereoFailed = true;
    return false;
  };
  if (!aurora::webgpu::g_vulkanSharedImageExportSupported) return fail("Dawn has no opaque-fd/sync-fd sharing");
  if (!g.hasExtMemFd || !g.hasExtSemFd) return fail("XR device lacks external memory/semaphore fd");
  if (g.eyeWidth == 0 || g.eyeHeight == 0) return fail("no eye size from the runtime");
  if (aurora::gfx::color_format() != wgpu::TextureFormat::RGBA8Unorm) return fail("scene format is not RGBA8Unorm");

  // r43: single pass also gets the split stadium's own pair of swapchains.
  for (int i = 0; i < (g.singlePass ? 4 : 2); ++i) {
    XrSwapchain& chain = i < 2 ? g.eyeSwapchain[i] : g.stadiumSwapchain[i - 2];
    std::vector<XrSwapchainImageVulkanKHR>& images = i < 2 ? g.eyeImages[i] : g.stadiumImages[i - 2];
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT |
                      XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    info.format = VK_FORMAT_R8G8B8A8_SRGB;
    info.sampleCount = 1;
    info.width = g.eyeWidth;
    info.height = g.eyeHeight;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!xr_ok(xrCreateSwapchain(g.session, &info, &chain), "xrCreateSwapchain(eye)")) {
      return fail("eye swapchain");
    }
    uint32_t count = 0;
    xrEnumerateSwapchainImages(chain, 0, &count, nullptr);
    images.assign(count, XrSwapchainImageVulkanKHR{XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    xrEnumerateSwapchainImages(chain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
  }
  for (uint32_t s = 0; s < kStereoSlots; ++s) {
    StereoSlot& slot = g.slots[s];
    // r43: single pass = one image two eyes wide (eye[0]); eye[1] stays empty.
    for (int eye = 0; eye < (g.singlePass ? 1 : 2); ++eye) {
      const uint32_t width = g.singlePass ? 2 * g.eyeWidth : g.eyeWidth;
      if (!create_eye_image(slot.eye[eye], VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, width)) {
        return fail("eye image");
      }
    }
    if (g.singlePass) {
      // r43: the eye pass opens in the GX stream (GXAuroraXrEyePass), so Aurora keeps the target:
      // slot s's wide image as target s, its split-stadium image (one eye) as target 2 + s.
      if (!create_eye_image(slot.stadium, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, g.eyeWidth)) {
        return fail("stadium image");
      }
      aurora::gfx::ExternalPassTarget target;
      target.texture = slot.eye[0].texture;
      target.view = slot.eye[0].renderView;
      target.format = aurora::gfx::color_format();
      target.width = 2 * g.eyeWidth;
      target.height = g.eyeHeight;
      aurora::gfx::register_xr_eye_target(s, target);
      target.texture = slot.stadium.texture;
      target.view = slot.stadium.renderView;
      target.width = g.eyeWidth;
      aurora::gfx::register_xr_eye_target(2 + s, target);
    }
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = g.pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!vk_ok(vkAllocateCommandBuffers(g.device, &allocInfo, &slot.cmd), "vkAllocateCommandBuffers(slot)") ||
        !vk_ok(vkCreateFence(g.device, &fenceInfo, nullptr, &slot.fence), "vkCreateFence(slot)")) {
      return fail("slot sync objects");
    }
  }
  XLOG("3D modes ready: %ux%u per eye, GPU-direct hand-off, %s", g.eyeWidth, g.eyeHeight,
       g.singlePass ? "single pass" : "pass per eye");
  return true;
}

// Ends Dawn's access to an eye image and keeps its end-of-access fence (a dup of the fd) for the
// blit. All Dawn calls: on the game thread with the render worker idle (after synchronize), or, r43
// single pass, on the render worker itself right after it submitted the frame (on_aurora_submit).
void release_eye_image(EyeImage& img) {
  wgpu::SharedTextureMemoryVkImageLayoutEndState layoutEnd{};
  wgpu::SharedTextureMemoryEndAccessState endState{};
  endState.nextInChain = &layoutEnd;
  img.shared.EndAccess(img.texture, &endState);
  img.accessOpen = false;
  img.released = true;
  img.dawnEndLayout = static_cast<VkImageLayout>(layoutEnd.newLayout);
  img.releaseFd = -1;
  img.releaseComplete = false;
  if (endState.fenceCount == 0 || endState.fences == nullptr) return;
  wgpu::SharedFenceExportInfo typeQuery{};
  endState.fences[0].ExportInfo(&typeQuery);
  int dawnFd = -1;
  if (typeQuery.type == wgpu::SharedFenceType::SyncFD) {
    wgpu::SharedFenceSyncFDExportInfo info{};
    wgpu::SharedFenceExportInfo exportInfo{};
    exportInfo.nextInChain = &info;
    endState.fences[0].ExportInfo(&exportInfo);
    dawnFd = info.handle;
    img.releaseFdType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    if (dawnFd < 0) {
      img.releaseComplete = true;  // -1: already signaled
      return;
    }
  } else if (typeQuery.type == wgpu::SharedFenceType::VkSemaphoreOpaqueFD) {
    wgpu::SharedFenceVkSemaphoreOpaqueFDExportInfo info{};
    wgpu::SharedFenceExportInfo exportInfo{};
    exportInfo.nextInChain = &info;
    endState.fences[0].ExportInfo(&exportInfo);
    dawnFd = info.handle;
    img.releaseFdType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
  } else {
    return;
  }
  if (dawnFd >= 0) {
    img.releaseFd = dup(dawnFd);  // Dawn keeps and closes its own; Vulkan takes ownership of ours
  }
}

// The released image's fence as a VkSemaphore the XR queue can wait on. False: no usable fence
// (knownComplete says whether that is because the work is already done).
bool import_release_fence(EyeImage& img, VkSemaphore semaphore, bool* knownComplete) {
  *knownComplete = img.releaseComplete;
  const int importFd = img.releaseFd;
  img.releaseFd = -1;
  if (importFd < 0) return false;
  static PFN_vkImportSemaphoreFdKHR importSemaphoreFd = nullptr;
  if (importSemaphoreFd == nullptr) {
    importSemaphoreFd =
        reinterpret_cast<PFN_vkImportSemaphoreFdKHR>(vkGetDeviceProcAddr(g.device, "vkImportSemaphoreFdKHR"));
  }
  VkImportSemaphoreFdInfoKHR info{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR};
  info.semaphore = semaphore;
  info.flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
  info.handleType = img.releaseFdType;
  info.fd = importFd;
  if (importSemaphoreFd == nullptr || importSemaphoreFd(g.device, &info) != VK_SUCCESS) {
    close(importFd);
    return false;
  }
  return true;
}

// Writes the left eye of `slot` to /storage/emulated/0/Strikers/xr_eye.ppm (self-test aid). r43:
// single pass writes both eyes side by side, so the right eye's view can be checked too.
void dump_eye(StereoSlot& slot, const char* modeName) {
  const uint32_t width = g.singlePass ? 2 * g.eyeWidth : g.eyeWidth;
  const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * g.eyeHeight * 4;
  if (!ensure_staging(bytes)) return;
  VkCommandBuffer cmd = g.cmd;
  vkResetCommandBuffer(cmd, 0);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &begin);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {width, g.eyeHeight, 1};
  vkCmdCopyImageToBuffer(cmd, slot.eye[0].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g.staging, 1, &region);
  vkEndCommandBuffer(cmd);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  if (!vk_ok(vkQueueSubmit(g.queue, 1, &submit, g.fence), "vkQueueSubmit(dump)")) return;
  vkWaitForFences(g.device, 1, &g.fence, VK_TRUE, UINT64_MAX);
  vkResetFences(g.device, 1, &g.fence);
  FILE* f = std::fopen("/storage/emulated/0/Strikers/xr_eye.ppm", "wb");
  if (f == nullptr) return;
  std::fprintf(f, "P6\n%u %u\n255\n", width, g.eyeHeight);
  const auto* px = static_cast<const uint8_t*>(g.stagingMapped);
  std::vector<uint8_t> row(static_cast<size_t>(width) * 3);
  // Over passthrough the alpha matters: composite like the compositor (premultiplied) over magenta,
  // so anything see-through shows pink.
  const bool overBackground = g.mrMode || slot.mainOverlay;  // r43b: over the split stadium too
  for (uint32_t y = 0; y < g.eyeHeight; ++y) {
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t* p = px + (static_cast<size_t>(y) * width + x) * 4;
      const int clear = overBackground ? 255 - p[3] : 0;
      row[x * 3 + 0] = static_cast<uint8_t>(std::min(255, p[0] + clear));
      row[x * 3 + 1] = p[1];
      row[x * 3 + 2] = static_cast<uint8_t>(std::min(255, p[2] + clear));
    }
    std::fwrite(row.data(), 1, row.size(), f);
  }
  std::fclose(f);
  XLOG("dumped %s (%s) to xr_eye.ppm", g.singlePass ? "both eyes" : "left eye", modeName);
}

// Hands slot `s`'s eye images to the compositor: blits them into the eye swapchains, gated on Dawn's
// fences (no CPU wait in the normal case). Images still in Dawn's access are released first (the
// per-eye path, after synchronize()); r43 single pass finds them released by the render worker.
void finish_stereo_frame(uint32_t s) {
  StereoSlot& slot = g.slots[s];
  VkSemaphore waits[3];
  uint32_t waitCount = 0;
  bool needCpuWait = false;
  bool mainReleased = false, stadiumReleased = false;
  EyeImage* images[3] = {&slot.eye[0], &slot.eye[1], &slot.stadium};
  for (int i = 0; i < 3; ++i) {
    EyeImage& img = *images[i];
    if (img.accessOpen) release_eye_image(img);
    if (!img.released) continue;
    img.released = false;
    (i < 2 ? mainReleased : stadiumReleased) = true;
    bool knownComplete = false;
    if (import_release_fence(img, img.dawnDone, &knownComplete)) {
      waits[waitCount++] = img.dawnDone;
    } else if (!knownComplete) {
      needCpuWait = true;
    }
  }
  // r43: what this frame drew. Single pass: the main (wide) image only if its pass ran (a split
  // stadium frame can skip it), the split stadium for the one eye it was drawn for.
  struct Blit {
    EyeImage* img;
    int32_t srcX;
    int eye;
    bool stadium;
    uint32_t index;
  };
  Blit blits[3];
  int blitCount = 0;
  if (mainReleased && (!g.singlePass || slot.mainDrawn)) {
    for (int eye = 0; eye < 2; ++eye) {
      blits[blitCount++] = {g.singlePass ? &slot.eye[0] : &slot.eye[eye],
                            g.singlePass ? eye * static_cast<int32_t>(g.eyeWidth) : 0, eye, false, 0};
    }
  }
  if (stadiumReleased && slot.stadiumEye >= 0) {
    blits[blitCount++] = {&slot.stadium, 0, slot.stadiumEye, true, 0};
  }
  if (blitCount == 0) {  // nothing new to show (the fences, if any, were imported and are dropped)
    if (waitCount > 0) {
      // A semaphore imported as temporary must still be waited on once before it is imported again.
      VkSubmitInfo drop{VK_STRUCTURE_TYPE_SUBMIT_INFO};
      const VkPipelineStageFlags stages[3] = {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT};
      drop.waitSemaphoreCount = waitCount;
      drop.pWaitSemaphores = waits;
      drop.pWaitDstStageMask = stages;
      vkQueueSubmit(g.queue, 1, &drop, VK_NULL_HANDLE);
    }
    g_slotState[s].store(0, std::memory_order_release);
    return;
  }
  if (needCpuWait) {
    const uint64_t cpuWaitStart = now_ns();
    aurora::gfx::synchronize();  // r43: the render worker may still be using Dawn (single pass)
    bool workDone = false;
    const auto future = aurora::webgpu::g_queue.OnSubmittedWorkDone(
        wgpu::CallbackMode::WaitAnyOnly, [&workDone](wgpu::QueueWorkDoneStatus, wgpu::StringView) { workDone = true; });
    aurora::webgpu::g_instance.WaitAny(future, 5000000000ull);
    g.cpuWaitNs += now_ns() - cpuWaitStart;
  }

  const auto chain_of = [](const Blit& b) { return b.stadium ? g.stadiumSwapchain[b.eye] : g.eyeSwapchain[b.eye]; };
  const uint64_t swapStart = now_ns();
  for (int i = 0; i < blitCount; ++i) {
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xr_ok(xrAcquireSwapchainImage(chain_of(blits[i]), &acquire, &blits[i].index), "xrAcquireSwapchainImage(eye)");
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    xr_ok(xrWaitSwapchainImage(chain_of(blits[i]), &wait), "xrWaitSwapchainImage(eye)");
  }
  g.swapNs += now_ns() - swapStart;

  vkResetCommandBuffer(slot.cmd, 0);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(slot.cmd, &begin);
  for (int i = 0; i < blitCount; ++i) {
    // r43: single pass reads both eyes from the halves of slot.eye[0]; its second half finds the
    // image already moved to TRANSFER_SRC.
    EyeImage& img = *blits[i].img;
    const bool srcReady = i > 0 && blits[i - 1].img == blits[i].img;
    const int eye = blits[i].eye;
    const VkImage dst = (blits[i].stadium ? g.stadiumImages[eye] : g.eyeImages[eye])[blits[i].index].image;
    VkImageMemoryBarrier barriers[2]{};
    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barriers[0].oldLayout = img.dawnEndLayout;  // keeps Dawn's writes
    barriers[0].newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = img.image;
    barriers[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    barriers[1] = barriers[0];
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barriers[1].image = dst;
    vkCmdPipelineBarrier(slot.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, srcReady ? 1 : 2, srcReady ? &barriers[1] : barriers);
    // A same-format blit is a byte copy, and records in microseconds where vkCmdCopyImage into the
    // runtime's gralloc image costs milliseconds on Adreno (TPVR's measurement).
    const int32_t srcX = blits[i].srcX;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {srcX, 0, 0};
    blit.srcOffsets[1] = {srcX + static_cast<int32_t>(g.eyeWidth), static_cast<int32_t>(g.eyeHeight), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[1] = {static_cast<int32_t>(g.eyeWidth), static_cast<int32_t>(g.eyeHeight), 1};
    vkCmdBlitImage(slot.cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &blit, VK_FILTER_NEAREST);
    VkImageMemoryBarrier toColor = barriers[1];
    toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    vkCmdPipelineBarrier(slot.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &toColor);
  }
  vkEndCommandBuffer(slot.cmd);
  const VkPipelineStageFlags waitStages[3] = {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                              VK_PIPELINE_STAGE_TRANSFER_BIT};
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = waitCount;
  submit.pWaitSemaphores = waits;
  submit.pWaitDstStageMask = waitStages;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &slot.cmd;
  if (vk_ok(vkQueueSubmit(g.queue, 1, &submit, slot.fence), "vkQueueSubmit(eyes)")) {
    slot.pending = true;
  }
  bool mainBlitted = false;
  for (int i = 0; i < blitCount; ++i) {
    const int eye = blits[i].eye;
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xr_ok(xrReleaseSwapchainImage(chain_of(blits[i]), &release), "xrReleaseSwapchainImage(eye)");
    XrCompositionLayerProjectionView& view = blits[i].stadium ? g.stadiumViews[eye] : g.projViews[eye];
    view = XrCompositionLayerProjectionView{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    view.pose = slot.views[eye].pose;  // r43: what it was drawn for, maybe a frame ago
    view.fov = slot.views[eye].fov;
    view.subImage.swapchain = chain_of(blits[i]);
    view.subImage.imageRect.offset = {0, 0};
    view.subImage.imageRect.extent = {static_cast<int32_t>(g.eyeWidth), static_cast<int32_t>(g.eyeHeight)};
    view.subImage.imageArrayIndex = 0;
    if (blits[i].stadium) {
      g.stadiumValid[eye] = true;
    } else {
      mainBlitted = true;
    }
  }
  if (mainBlitted) {
    g.hasProjection = true;
    // Drawn over a split stadium: its alpha is coverage (the compositor blends it over).
    g.mainIsOverlay = slot.mainOverlay;
  } else if (g.singlePass && !g.tabletop && slot.stadiumEye >= 0 && !slot.mainDrawn) {
    // r87: Jumbotron · Stadium split, the stadium alone this frame: nothing over it (the last pair
    // over it would be a scorer who has gone).
    g.hasProjection = false;
  }

  if (g.dumpRequested && mainBlitted) {
    g.dumpRequested = false;
    vkWaitForFences(g.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    dump_eye(slot, mode_name());
  }
  g_slotState[s].store(0, std::memory_order_release);
}

// r43, single pass, on the render worker right after it submitted Aurora frame `frame`: the slot
// that frame drew into ends Dawn's access here, so the game thread never waits for the render worker
// to hand the eyes over (finish_stereo_frame blits it at the next display frame).
void on_aurora_submit(uint32_t frame, void*) {
  for (uint32_t s = 0; s < kStereoSlots; ++s) {
    if (g_slotState[s].load(std::memory_order_acquire) != 1 ||
        g_slotFrame[s].load(std::memory_order_relaxed) != frame) {
      continue;
    }
    StereoSlot& slot = g.slots[s];
    for (EyeImage* img : {&slot.eye[0], &slot.eye[1], &slot.stadium}) {
      if (img->accessOpen) release_eye_image(*img);
    }
    g_slotState[s].store(2, std::memory_order_release);
  }
}

// r43, single pass: blits every slot whose frame has been submitted, oldest first, so the newest
// pair is what the compositor gets.
void finish_released_slots() {
  for (int pass = 0; pass < 2; ++pass) {
    int oldest = -1;
    for (uint32_t s = 0; s < kStereoSlots; ++s) {
      if (g_slotState[s].load(std::memory_order_acquire) != 2) continue;
      if (oldest < 0 || static_cast<int32_t>(g_slotFrame[s].load(std::memory_order_relaxed) -
                                             g_slotFrame[oldest].load(std::memory_order_relaxed)) < 0) {
        oldest = static_cast<int>(s);
      }
    }
    if (oldest < 0) return;
    finish_stereo_frame(static_cast<uint32_t>(oldest));
  }
}

void set_perf_levels(int cpu, int gpu);

// Self-test hooks, polled about once a second: a file /storage/emulated/0/Strikers/xr_mode holding
// "jumbotron mr", "jumbotron vr", "tabletop mr" or "tabletop vr" sets the mode (then is deleted);
// a file xr_dump requests one left-eye dump.
void poll_triggers() {
  const uint64_t t = now_ns();
  if (t - g.lastTriggerPollNs < 1000000000ull) return;
  g.lastTriggerPollNs = t;
  const char* modePath = "/storage/emulated/0/Strikers/xr_mode";
  if (FILE* f = std::fopen(modePath, "r")) {
    char buf[64] = {};
    const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
    std::fclose(f);
    std::remove(modePath);
    buf[n] = '\0';
    const std::string s(buf);
    g.tabletop = s.find("tabletop") != std::string::npos;
    set_mr_mode(s.find("vr") == std::string::npos);
  }
  const char* dumpPath = "/storage/emulated/0/Strikers/xr_dump";
  struct stat st {};
  if (stat(dumpPath, &st) == 0) {
    std::remove(dumpPath);
    g.dumpRequested = true;
    // And what the screen / scoreboard quad shows (the newest game frame read back).
    std::lock_guard<std::mutex> lock(g_frameMutex);
    if (g_frameWidth > 0 && g_frameHeight > 0) {
      if (FILE* f = std::fopen("/storage/emulated/0/Strikers/xr_screen.ppm", "wb")) {
        std::fprintf(f, "P6\n%u %u\n255\n", g_frameWidth, g_frameHeight);
        std::vector<uint8_t> row(static_cast<size_t>(g_frameWidth) * 3);
        for (uint32_t y = 0; y < g_frameHeight; ++y) {
          const uint8_t* src = g_frame.data() + static_cast<size_t>(y) * g_frameWidth * 4;
          for (uint32_t x = 0; x < g_frameWidth; ++x) {
            row[x * 3 + 0] = src[x * 4 + 0];
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 2];
          }
          std::fwrite(row.data(), 1, row.size(), f);
        }
        std::fclose(f);
      }
    }
  }
  // Tuning: "viewmask <hex>" = which GLV views the eye sends draw (bit n = view n).
  const char* tunePath = "/storage/emulated/0/Strikers/xr_tune";
  if (FILE* f = std::fopen(tunePath, "r")) {
    char line[64] = {};
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      if (std::strncmp(line, "viewmask_table", 14) == 0) {
        g.eyeViewMaskTable = static_cast<uint32_t>(std::strtoul(line + 14, nullptr, 16));
      } else if (std::strncmp(line, "viewmask_stadium", 16) == 0) {
        g.eyeViewMaskStadium = static_cast<uint32_t>(std::strtoul(line + 16, nullptr, 16));
      } else if (std::strncmp(line, "viewmask", 8) == 0) {
        g.eyeViewMask = static_cast<uint32_t>(std::strtoul(line + 8, nullptr, 16));
      } else if (std::strncmp(line, "skip", 4) == 0) {
        // "skip <crowd> <stands> <stadiumfield>", each 0/1
        int crowd = 0, stands = 0, field = 0;
        std::sscanf(line + 4, "%d %d %d", &crowd, &stands, &field);
        PortXrTuneSkip(crowd, stands, field);
      } else if (std::strncmp(line, "lod", 3) == 0) {
        g.lodMinPx = static_cast<float>(std::atof(line + 3));
      } else if (std::strncmp(line, "tallfield", 9) == 0) {
        g.skipTallField = std::atoi(line + 9) != 0;
      } else if (std::strncmp(line, "stadiumsplit", 12) == 0) {
        // r43 "stadiumsplit <0|1>": Tabletop · Stadium's stadium as its own layer, one eye a frame
        g.stadiumSplit = std::atoi(line + 12) != 0;
      } else if (std::strncmp(line, "jumbosplit", 10) == 0) {
        // r87 "jumbosplit <0|1>": Jumbotron · Stadium's stadium as its own layer, one eye a frame
        g.jumboSplit = std::atoi(line + 10) != 0;
      } else if (std::strncmp(line, "handoffwait", 11) == 0) {
        // r44 "handoffwait <0|1>": wait briefly for this frame's eyes before ending the XR frame
        g.handoffWait = std::atoi(line + 11) != 0;
      } else if (std::strncmp(line, "stadiumhalf", 11) == 0) {
        // "stadiumhalf <0|1>": Jumbotron · Stadium's eyes every other game frame (A/B)
        g.stadiumHalf = std::atoi(line + 11) != 0;
      } else if (std::strncmp(line, "msaa", 4) == 0) {
        int table = 4, stadium = 4;
        std::sscanf(line + 4, "%d %d", &table, &stadium);
        g.msaaTable = table > 1 ? 4 : 1;
        g.msaaStadium = stadium > 1 ? 4 : 1;
      } else if (std::strncmp(line, "decode", 6) == 0) {
        // "decode <0-2>": vertex decode-once off / players / everything eligible
        aurora_xr_set_vertex_decode(std::atoi(line + 6));
      } else if (std::strncmp(line, "aniso", 5) == 0) {
        // "aniso <1-16>": the minimum anisotropic filtering (new samplers from the next draw on)
        aurora_xr_set_texture_anisotropy(static_cast<unsigned>(std::max(1, std::atoi(line + 5))));
      } else if (std::strncmp(line, "perf", 4) == 0) {
        // "perf <cpu> <gpu>": 0 power savings, 1 sustained low, 2 sustained high, 3 boost
        int cpu = 2, gpu = 2;
        std::sscanf(line + 4, "%d %d", &cpu, &gpu);
        set_perf_levels(cpu, gpu);
      } else if (std::strncmp(line, "charscale", 9) == 0) {
        g.charScale = std::max(1.0f, std::min(3.0f, static_cast<float>(std::atof(line + 9))));
      } else if (std::strncmp(line, "tablehere", 9) == 0) {
        g.bringTable = true;
      } else if (std::strncmp(line, "probe", 5) == 0) {
        PortXrProbe();
      } else if (std::strncmp(line, "stadiumyaw", 10) == 0) {
        g.stadiumYawDebug = static_cast<float>(std::atof(line + 10)) * 3.14159265f / 180.0f;
      } else if (std::strncmp(line, "reset", 5) == 0) {
        g.eyeViewMask = g.eyeViewMaskTable = g.eyeViewMaskStadium = 0xFFFFFFFFu;
        PortXrTuneSkip(0, 0, 0);
        g.lodMinPx = 0.0f;
        g.skipTallField = false;
      }
      XLOG("tune: %s", line);
    }
    std::fclose(f);
    std::remove(tunePath);
  }
  // Celebration self-test: "goal <class> [super]", "replay", "end", "skip", "dumpfx" (one per line).
  const char* fxPath = "/storage/emulated/0/Strikers/xr_fx";
  if (FILE* f = std::fopen(fxPath, "r")) {
    char line[64];
    std::vector<std::string> cmds;
    while (std::fgets(line, sizeof(line), f) != nullptr) cmds.emplace_back(line);
    std::fclose(f);
    std::remove(fxPath);
    for (const std::string& c : cmds) {
      if (c.rfind("goal", 0) == 0) {
        const int who = std::atoi(c.c_str() + 4);
        PortXrGoal(0, who, c.find("super") != std::string::npos ? 1 : 0, 0, 0, -1);
      } else if (c.rfind("replay", 0) == 0) {
        PortXrCelebrationPhase(1);
      } else if (c.rfind("end", 0) == 0) {
        PortXrCelebrationPhase(2);
      } else if (c.rfind("skip", 0) == 0) {
        PortXrCelebrationPhase(3);
      } else if (c.rfind("dumpfx", 0) == 0 && g.fxReady) {
        xrfx::request_dump("/storage/emulated/0/Strikers/xr_fx.ppm");
      } else if (c.rfind("item", 0) == 0) {
        // A green shell for your team, then X pressed a second later through the Touch path.
        g.testItem = true;
        g.testPressXNs = now_ns() + 1500000000ull;
      } else if (c.rfind("wipe", 0) == 0) {
        // The game's fade_to_white_and_back wipe, the effects layer dumped at the flash's height.
        g.testWipe = true;
        ensure_fx();
        if (g.fxReady) xrfx::dump_at_flash();
      } else if (c.rfind("sts", 0) == 0) {
        // The Super Strike meter over the player you control for 3 s, no shot taken.
        g.testSts = true;
      }
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Session events
// ---------------------------------------------------------------------------------------------

// The tid of this process's thread whose name starts with `prefix`, 0 when there is none.
pid_t find_thread(const char* prefix) {
  DIR* dir = opendir("/proc/self/task");
  if (dir == nullptr) return 0;
  pid_t found = 0;
  while (dirent* e = readdir(dir)) {
    if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/task/%s/comm", e->d_name);
    if (FILE* f = std::fopen(path, "r")) {
      char name[32] = {};
      std::fgets(name, sizeof(name), f);
      std::fclose(f);
      if (std::strncmp(name, prefix, std::strlen(prefix)) == 0) {
        found = static_cast<pid_t>(std::atoi(e->d_name));
        break;
      }
    }
  }
  closedir(dir);
  return found;
}

// CPU / GPU levels: 0 power savings, 1 sustained low, 2 sustained high, 3 boost (xr_tune "perf").
void set_perf_levels(int cpu, int gpu) {
  if (!g.hasPerfSettingsExt) return;
  PFN_xrPerfSettingsSetPerformanceLevelEXT setLevel = nullptr;
  if (!get_proc(g.instance, "xrPerfSettingsSetPerformanceLevelEXT", &setLevel)) return;
  static const XrPerfSettingsLevelEXT kLevels[] = {
      XR_PERF_SETTINGS_LEVEL_POWER_SAVINGS_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT,
      XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT, XR_PERF_SETTINGS_LEVEL_BOOST_EXT};
  xr_ok(setLevel(g.session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, kLevels[std::clamp(cpu, 0, 3)]),
        "xrPerfSettingsSetPerformanceLevelEXT(CPU)");
  xr_ok(setLevel(g.session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, kLevels[std::clamp(gpu, 0, 3)]),
        "xrPerfSettingsSetPerformanceLevelEXT(GPU)");
}

// Without these the runtime keeps the app on its default (dynamic, low) clocks and schedules the
// game and render threads like any background thread. Five sends a frame in Tabletop · Stadium
// need the sustained-high CPU and GPU levels and the big cores.
void apply_performance_settings() {
  set_perf_levels(2, 2);
  if (g.hasThreadSettingsExt) {
    PFN_xrSetAndroidApplicationThreadKHR setThread = nullptr;
    if (get_proc(g.instance, "xrSetAndroidApplicationThreadKHR", &setThread)) {
      // This is the game thread: it runs the game, the sends and the XR frame loop.
      const pid_t game = gettid();
      const pid_t render = find_thread("Aurora render");
      const pid_t fifo = find_thread("Aurora FIFO");  // as TPVR does: RENDERER_WORKER
      xr_ok(setThread(g.session, XR_ANDROID_THREAD_TYPE_APPLICATION_MAIN_KHR, game),
            "xrSetAndroidApplicationThreadKHR(game)");
      if (render != 0) {
        xr_ok(setThread(g.session, XR_ANDROID_THREAD_TYPE_RENDERER_MAIN_KHR, render),
              "xrSetAndroidApplicationThreadKHR(render)");
      }
      if (fifo != 0) {
        xr_ok(setThread(g.session, XR_ANDROID_THREAD_TYPE_RENDERER_WORKER_KHR, fifo),
              "xrSetAndroidApplicationThreadKHR(fifo)");
      }
      XLOG("thread hints: game tid %d, render worker tid %d, fifo tid %d", static_cast<int>(game),
           static_cast<int>(render), static_cast<int>(fifo));
    }
  }
}

void on_session_began() {
  init_refresh_rate();
  apply_performance_settings();
  set_mr_mode(g.mrMode);
  xrctrl::start(g.instance, g.session, g.renderModelsSupported);  // r54: the controllers' models
}

void poll_events() {
  XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
  while (xrPollEvent(g.instance, &event) == XR_SUCCESS) {
    switch (event.type) {
    case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
      const auto* e = reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
      g.sessionState = e->state;
      XLOG("session state %d", static_cast<int>(e->state));
      switch (e->state) {
      case XR_SESSION_STATE_READY: {
        XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
        begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        if (xr_ok(xrBeginSession(g.session, &begin), "xrBeginSession")) {
          g.running = true;
          on_session_began();
        }
        break;
      }
      case XR_SESSION_STATE_STOPPING:
        xr_ok(xrEndSession(g.session), "xrEndSession");
        g.running = false;
        g.focused = false;
        break;
      case XR_SESSION_STATE_FOCUSED:
        g.focused = true;
        break;
      case XR_SESSION_STATE_VISIBLE:
      case XR_SESSION_STATE_SYNCHRONIZED:
      case XR_SESSION_STATE_IDLE:
        g.focused = false;
        break;
      case XR_SESSION_STATE_EXITING:
      case XR_SESSION_STATE_LOSS_PENDING:
        g.running = false;
        g.focused = false;
        g.quit = true;
        break;
      default:
        break;
      }
      break;
    }
    case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
      g.quit = true;
      break;
    case XR_TYPE_EVENT_DATA_PERF_SETTINGS_EXT: {
      // r60: the headset warning that the CPU or GPU is at its limit (thermal or load).
      const auto* e = reinterpret_cast<const XrEventDataPerfSettingsEXT*>(&event);
      static const char* kSub[] = {"?", "compositing", "rendering", "thermal"};
      const int sub = static_cast<int>(e->subDomain);
      const auto level = [](XrPerfSettingsNotificationLevelEXT l) {
        return l == XR_PERF_SETTINGS_NOTIF_LEVEL_NORMAL_EXT    ? "normal"
               : l == XR_PERF_SETTINGS_NOTIF_LEVEL_WARNING_EXT ? "warning"
                                                                 : "impaired";
      };
      XLOG("perf notification: %s %s %s -> %s", e->domain == XR_PERF_SETTINGS_DOMAIN_CPU_EXT ? "CPU" : "GPU",
           sub >= 0 && sub < 4 ? kSub[sub] : "?", level(e->fromLevel), level(e->toLevel));
      break;
    }
    default:
      break;
    }
    event = XrEventDataBuffer{XR_TYPE_EVENT_DATA_BUFFER};
  }
}

void log_stats() {
  const uint64_t t = now_ns();
  if (g.lastStatsNs == 0) {
    g.lastStatsNs = t;
    return;
  }
  if (t - g.lastStatsNs < 5000000000ull) {
    return;
  }
  const double secs = static_cast<double>(t - g.lastStatsNs) / 1e9;
  const double uploadMs =
      g.gameFramesSinceStats ? static_cast<double>(g.uploadNsSinceStats) / 1e6 / g.gameFramesSinceStats : 0.0;
  unsigned long buffers = 0, underruns = 0;
  int everNonSilent = 0;
  PortAudioStats(&buffers, &underruns, &everNonSilent);
  const double frameEndMs =
      g.gameFramesSinceStats ? static_cast<double>(g.stereoNsSinceStats) / 1e6 / g.gameFramesSinceStats : 0.0;
  const double eyeSendMs =
      g.gameFramesSinceStats ? static_cast<double>(g.eyeSendNsSinceStats) / 1e6 / g.gameFramesSinceStats : 0.0;
  uint32_t poolPeak[4] = {}, poolSize[4] = {};
  aurora_gfx_pool_stats(poolPeak, poolSize, 4);
  const auto pct = [&](int i) { return poolSize[i] ? 100.0 * poolPeak[i] / poolSize[i] : 0.0; };
  XLOG("stats: display %.1f fps, game %.1f fps, upload %.2f ms, frame-end %.2f ms, eye-send %.2f ms, "
       "audio underruns +%lu, %s, 3D %s, screen %d, rumble %llu, pools v%.0f%% u%.0f%% i%.0f%% s%.0f%%, "
       "layers dropped %llu, culled %.0f/frame",
       g.displayFramesSinceStats / secs, g.gameFramesSinceStats / secs, uploadMs, frameEndMs, eyeSendMs,
       underruns - g.lastUnderruns, mode_name(), g.hasProjection ? "on" : "off", g.screenPreset,
       static_cast<unsigned long long>(g.rumblePulses), pct(0), pct(1), pct(2), pct(3),
       static_cast<unsigned long long>(g.layersDroppedSinceStats),
       g.gameFramesSinceStats ? static_cast<double>(PortXrLayersTakeCulled()) / g.gameFramesSinceStats : 0.0);
  uint32_t fxParts = 0, fxIndices = 0;
  if (g.fxReady && xrfx::take_stats(&fxParts, &fxIndices)) {
    XLOG("fx: up to %u pieces, %u mesh indices per eye", fxParts, fxIndices);
  }
  g.rumblePulses = 0;
  g.layersDroppedSinceStats = 0;
  if (g.gameFramesSinceStats > 0) {
    const double n = static_cast<double>(g.gameFramesSinceStats) * 1e6;
    XLOG("loop per game frame (ms): xrWaitFrame %.2f (all display frames), PortXrBeginFrame %.2f, "
         "aurora_begin_frame %.2f, tasks %.2f, aurora_end_frame %.2f, PortXrEndFrame %.2f",
         g.xrWaitNsSinceStats / n, g.loopNs[0] / n, g.loopNs[1] / n, g.loopNs[2] / n, g.loopNs[3] / n,
         g.loopNs[4] / n);
    uint64_t w[8] = {};
    aurora_xr_take_waits(w, 8);
    XLOG("waits per game frame (ms): fifo-drain %.2f, queue-full %.2f, sync %.2f, frame-slot %.2f, staging %.2f, "
         "pipeline %.2f, eye-fence %.2f, swapchain %.2f, cpu-wait %.2f | fx %.2f, xrEndFrame %.2f | busy: fifo "
         "%.2f, render %.2f",
         w[0] / n, w[1] / n, w[2] / n, w[3] / n, w[4] / n, w[5] / n, g.eyeFenceNs / n, g.swapNs / n,
         g.cpuWaitNs / n, g.fxNs / n, g.xrEndNs / n, w[6] / n, w[7] / n);
    uint32_t d[6] = {};
    aurora_xr_take_decode_stats(d);
    const double frames = static_cast<double>(g.gameFramesSinceStats);
    XLOG("decode per game frame: %.1f draws decoded, %.1f reused, %.0f vertices, %.2f ms CPU | %.1f waiting for "
         "their pipeline, %.1f not eligible",
         d[0] / frames, d[1] / frames, d[2] / frames, d[5] / 1000.0 / frames, d[3] / frames, d[4] / frames);
    // r53: where those draws come from, per game frame (send, then its biggest GL views).
    char census[512];
    PortXrTakeDrawCensus(census, sizeof(census), static_cast<unsigned long>(g.gameFramesSinceStats));
    if (census[0] != '\0') XLOG("draws per game frame: %s%s", census, g.replayRunning ? " [replay]" : "");
  }
  if (g.handoffSame + g.handoffLate > 0) {  // r44
    XLOG("eye hand-off: %u in their own display frame, %u a frame late (wait %s, cut short %u, game frame %.1f ms)",
         g.handoffSame, g.handoffLate, g.handoffWait ? "on" : "off", g.handoffCut, g.gameFrameEmaNs / 1e6);
  }
  g.handoffSame = g.handoffLate = g.handoffCut = 0;
  g.eyeFenceNs = g.swapNs = g.cpuWaitNs = g.fxNs = g.xrEndNs = 0;
  std::memset(g.loopNs, 0, sizeof(g.loopNs));
  g.xrWaitNsSinceStats = 0;
  g.lastUnderruns = underruns;
  g.stereoNsSinceStats = 0;
  g.eyeSendNsSinceStats = 0;
  g.lastStatsNs = t;
  g.displayFramesSinceStats = 0;
  g.gameFramesSinceStats = 0;
  g.uploadNsSinceStats = 0;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------------------------

extern "C" int PortXrWanted(void) {
  const char* off = std::getenv("STRIKERS_XR");
  return (off != nullptr && off[0] == '0') ? 0 : 1;
}

extern "C" int PortXrInit(void) {
  if (g.active) {
    return 1;
  }
  if (!PortXrWanted()) {
    XLOG("STRIKERS_XR=0: flat mode");
    return 0;
  }
  XLOG("%s starting", kBuildTag);

  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
  if (env == nullptr || activity == nullptr || env->GetJavaVM(&g.vm) != JNI_OK) {
    XERR("no JNI environment or activity from SDL");
    return 0;
  }
  g.activity = env->NewGlobalRef(activity);
  env->DeleteLocalRef(activity);

  if (!init_loader() || !create_instance() || !create_vulkan_device() || !create_session() || !create_actions()) {
    XERR("OpenXR bring-up failed; running flat");
    PortXrShutdown();
    return 0;
  }
  create_passthrough();
  load_screens();       // r42: where you left the big screens
  load_shelf();         // r57: where you left the trophy shelf
  load_crowd_level();   // r42: your crowd volume
  load_view_height();   // r44g: your stadium height

  aurora_xr_set_surface_suppressed(true);
  aurora_xr_set_frame_callback(&on_aurora_frame, nullptr);
  aurora_xr_set_submit_callback(&on_aurora_submit, nullptr);  // r43: single-pass eye hand-off
  // The immersive Activity's window is panel-sized (4128x2208 on Quest 3), and the port's render
  // target follows the window (launch.cpp PortFollowRenderScale) up to 3x: 2512x1344, read back
  // every frame. 2x is 896 rows, about 1600x896 on the 16:9 screen. A res_scale the player set wins.
  setenv("STRIKERS_RES_SCALE", "2.0", 0);
  // 50 ms of audio queued instead of 30: see audio_out.cpp target_buffers().
  setenv("STRIKERS_AUDIO_BUFFERS", "10", 0);
  // The Tabletop pitch is seen small and at a slant: 8x anisotropic filtering on mipmapped textures
  // keeps the grass and markings from shimmering (Lance, 2026-10-10: grainy). STRIKERS_XR_ANISO.
  const char* aniso = std::getenv("STRIKERS_XR_ANISO");
  aurora_xr_set_texture_anisotropy(aniso != nullptr && *aniso != '\0' ? static_cast<unsigned>(std::atoi(aniso)) : 8u);
  // r38: vertices are decoded to floats once per frame and reused by every send, instead of every
  // eye, binning pass and tile unpacking the GameCube bytes (Aurora vertex_decode.hpp). Measured
  // (r38c, same session): Tabletop · Living Room 48-51 -> 69-72 fps (game 60), Tabletop · Stadium
  // 26 -> 40-45, Jumbotron · Stadium 33 -> 48; identical pictures. 0 off, 1 players only,
  // 2 everything eligible (default). STRIKERS_XR_DECODE, xr_tune "decode <n>".
  const char* decode = std::getenv("STRIKERS_XR_DECODE");
  aurora_xr_set_vertex_decode(decode != nullptr && *decode != '\0' ? std::atoi(decode) : 2);
  // r47: Bluetooth pads join this headset's player (sync_input) instead of ports of their own.
  PADSetPhysicalExternal(true);
  g.active = true;
  XLOG("%s ready (session created, waiting for READY)", kBuildTag);
  return 1;
}

extern "C" int PortXrActive(void) { return g.active ? 1 : 0; }

// --- The per-eye re-send contract with glPlat.cpp (see glx_XrSendEyes) and world.cpp ---

extern "C" int PortXrStereoWorld(void) { return (g.active && stereo_mode() && !g.stereoFailed) ? 1 : 0; }

// Tabletop · Stadium shows the whole stadium life-size around you, so the gameplay camera's
// "drop the stands between me and the pitch" must not apply. Jumbotron · Stadium keeps it: there the
// broadcast view on the big screen is the main picture and must not be blocked.
extern "C" int PortXrStereoFullStands(void) {
  return (PortXrStereoWorld() && g.tabletop && !g.mrMode) ? 1 : 0;
}

namespace {

// Pool guard. The eye sends re-push vertices, indices, uniforms and storage arrays, so five sends
// a frame can outgrow Aurora's fixed pools, and an overflow aborts the game (r4, r6). The cost of
// one eye send of each layer kind is measured as it happens; a frame whose sends would not fit
// drops the stadium layer first, then its 3D altogether (the last pair is shown again).
constexpr double kPoolMargin = 1.25;

bool layers_fit(const uint32_t used[kPools], const int* kinds, int count) {
  const double sends = g.singlePass ? 1.0 : 2.0;  // r43: one send per layer for both eyes
  for (int p = 0; p < kPools; ++p) {
    if (g.poolReserved[p] == 0) continue;
    double need = used[p];
    for (int l = 0; l < count; ++l) need += sends * g.perSend[kinds[l]][p] * kPoolMargin;
    if (p == 0) need += g.decodedPerFrame * kPoolMargin;  // the decoded players, once per frame
    if (need > g.poolReserved[p] * 0.98) return false;
  }
  return true;
}

void close_pool_measurement() {
  if (!g.measuring) return;
  g.measuring = false;
  uint32_t used[kPools];
  aurora_gfx_pool_usage(used, kPools);
  const unsigned long long decoded = aurora_xr_decoded_bytes() - g.decodedAtMeasure;
  for (int p = 0; p < kPools; ++p) {
    float delta = used[p] > g.measureStart[p] ? static_cast<float>(used[p] - g.measureStart[p]) : 0.0f;
    if (p == 0) delta = std::max(0.0f, delta - static_cast<float>(decoded));
    float& est = g.perSend[g.measuringKind][p];
    est = delta > est ? delta : est * 0.99f + delta * 0.01f;  // rise at once, fall slowly
  }
}

// r43: this frame's single-pass geometry. The send is made from the centre between the eyes with a
// field of view holding both (the game culls against it). Each eye's own view is then a fixed move
// in eye space, t = centre view^-1 * eye view (a layer's model cancels out, so one t serves every
// layer), and each eye's frustum a remap of clip x and y: gx_projection gives x = sx * x_eye + ox *
// z_eye and w = -z_eye, so an eye's x is (sx_e / sx_u) * x_u + (sx_e * ox_u / sx_u - ox_e) * w.
void plan_single_pass() {
  const XrPosef& left = g.views[0].pose;
  const XrPosef& right = g.views[1].pose;
  g.centrePose.orientation = left.orientation;  // the Quest's eyes share one; t carries any difference
  g.centrePose.position = {(left.position.x + right.position.x) * 0.5f, (left.position.y + right.position.y) * 0.5f,
                           (left.position.z + right.position.z) * 0.5f};
  const XrFovf& fl = g.views[0].fov;
  const XrFovf& fr = g.views[1].fov;
  g.unionFov.angleLeft = std::min(fl.angleLeft, fr.angleLeft);
  g.unionFov.angleRight = std::max(fl.angleRight, fr.angleRight);
  g.unionFov.angleUp = std::max(fl.angleUp, fr.angleUp);
  g.unionFov.angleDown = std::min(fl.angleDown, fr.angleDown);
  float pu[16];
  gx_projection(g.unionFov, 0.05f, 1500.0f, pu);
  const M4 centreInv = m4_rigid_inverse(m4_view_from_pose(g.centrePose));
  for (int eye = 0; eye < 2; ++eye) {
    // Row form: p_eye = p_centre * d. GX takes the column form, 3x4 row-major: its rows are d's columns.
    const M4 d = m4_mul(centreInv, m4_view_from_pose(g.views[eye].pose));
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 4; ++c) g.stereoT[eye][r * 4 + c] = d.m[c][r];
    float pe[16];
    gx_projection(g.views[eye].fov, 0.05f, 1500.0f, pe);
    g.stereoClip[eye][0] = pe[0] / pu[0];
    g.stereoClip[eye][1] = pe[0] * pu[2] / pu[0] - pe[2];
    g.stereoClip[eye][2] = pe[5] / pu[5];
    g.stereoClip[eye][3] = pe[5] * pu[6] / pu[5] - pe[6];
  }
}

// r43: closes the open single-pass eye pass in the GX stream (stereo off first, after its last draw).
void close_single_pass() {
  if (g.openTarget == 0) return;
  if (g.stereoOn) {
    GXAuroraSetStereo(0, nullptr, nullptr, nullptr, nullptr);
    g.stereoOn = false;
  }
  GXAuroraXrEyePass(0, 0, 0, 0);
  g.openTarget = 0;
  g.eyePassOpen[0] = false;
}

}  // namespace

extern "C" int PortXrStereoFrameBegin(void) {
  if (!g.active || !g.frameBegun || !g.viewsValid || !eye_mode() || g.stereoFailed) {
    return 0;
  }
  // Online, a headset that cannot hold 60 game frames a second would slow the shared match down for
  // both (fixed step). Then its 3D view is redrawn every other game frame (the compositor reprojects
  // the last pair for head motion) and the match keeps its speed: presentation stays per player.
  if (PortNetOnline() && g.onlineHalfRate && (g.onlineParity++ & 1) != 0) {
    return 0;
  }
  // r42b (A/B, xr_tune "stadiumhalf"): Jumbotron · Stadium, the stadium around you every other game
  // frame; the compositor turns the last pair with your head between. Not while the scorer is out.
  if (g.stadiumHalf && !g.tabletop && !g.mrMode && !scorer_shown() && (g.stadiumParity++ & 1) != 0) {
    return 0;
  }
  if (g.poolReserved[0] == 0) {
    aurora_gfx_pool_stats(nullptr, g.poolReserved, kPools);
  }
  {
    // The previous game frame's decoded bytes (Aurora counts them per frame).
    const float lastFrame = static_cast<float>(aurora_xr_decoded_bytes_last_frame());
    g.decodedPerFrame = lastFrame > g.decodedPerFrame ? lastFrame : g.decodedPerFrame * 0.99f + lastFrame * 0.01f;
  }
  uint32_t used[kPools];
  aurora_gfx_pool_usage(used, kPools);
  for (int k = 0; k < 3; ++k) {
    if (g.perSend[k][0] == 0.0f) {
      // Until measured: half of the game view's own send (r5/r6 eye sends were a quarter to a half);
      // the scorer is one character, a tenth.
      const float part = k == kLayerScorer ? 0.1f : 0.5f;
      for (int p = 0; p < kPools; ++p) g.perSend[k][p] = part * static_cast<float>(used[p]);
    }
  }
  int kinds[3] = {0, 0, 0};
  int count = layer_count();
  for (int l = 0; l < count; ++l) kinds[l] = layer_kind(l);
  if (!layers_fit(used, kinds, count) && count > 1 && kinds[0] == kLayerStadium) {
    // Keep the field (and the scorer); the stadium estimate decays so the layer is tried (and
    // measured) again.
    for (int p = 0; p < kPools; ++p) g.perSend[kLayerStadium][p] *= 0.98f;
    for (int l = 1; l < count; ++l) kinds[l - 1] = kinds[l];
    --count;
    ++g.layersDroppedSinceStats;
  }
  if (!layers_fit(used, kinds, count)) {
    g.layersDroppedSinceStats += count;
    return 0;
  }
  g.frameLayerKind[0] = kinds[0];
  g.frameLayerKind[1] = kinds[1];
  g.frameLayerKind[2] = kinds[2];
  StereoSlot& slot = g.slots[g.slot];
  if (g_slotState[g.slot].load(std::memory_order_acquire) != 0) {
    // r43 single pass: this slot's last frame (two game frames ago) is normally submitted and
    // blitted by now; if not, wait for the render worker to submit it, then hand it over.
    const uint64_t waitStart = now_ns();
    while (g_slotState[g.slot].load(std::memory_order_acquire) == 1 && now_ns() - waitStart < 200000000ull) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    if (g_slotState[g.slot].load(std::memory_order_acquire) == 1) {
      // Its submit was never seen (should not happen): end the access here, with Dawn idle.
      XWARN("eye slot %u: frame %u never reported submitted", g.slot, g_slotFrame[g.slot].load());
      aurora::gfx::synchronize();
      for (EyeImage* img : {&slot.eye[0], &slot.eye[1], &slot.stadium}) {
        if (img->accessOpen) release_eye_image(*img);
      }
      g_slotState[g.slot].store(2, std::memory_order_release);
    }
    finish_stereo_frame(g.slot);
    g.eyeFenceNs += now_ns() - waitStart;
  }
  slot.views[0] = g.views[0];  // r43: what this frame's eyes are drawn for (finish_stereo_frame)
  slot.views[1] = g.views[1];
  if (slot.pending) {
    // This slot's blit from two frames ago must be done before Dawn draws into its images again.
    const uint64_t fenceStart = now_ns();
    vkWaitForFences(g.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    g.eyeFenceNs += now_ns() - fenceStart;
    vkResetFences(g.device, 1, &slot.fence);
    slot.pending = false;
  }
  for (int eye = 0; eye < 2; ++eye) {
    EyeImage& img = slot.eye[eye];
    if (!img.shared) {  // r43: single pass has one image per slot
      img.accessOpen = false;
      g.eyePassOpen[eye] = false;
      continue;
    }
    // Dawn's Vulkan backend requires the layout chain. UNDEFINED both ways: every pixel is
    // rewritten each frame, so the previous contents may go.
    wgpu::SharedTextureMemoryVkImageLayoutBeginState layoutBegin{};
    layoutBegin.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    layoutBegin.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    wgpu::SharedTextureMemoryBeginAccessDescriptor desc{};
    desc.nextInChain = &layoutBegin;
    desc.initialized = true;
    img.accessOpen = static_cast<bool>(img.shared.BeginAccess(img.texture, &desc));
    g.eyePassOpen[eye] = false;
  }
  // r43 split stadium: Tabletop · Stadium draws the stadium layer for one eye, in turn, into its own
  // image and layer; the table (and scorer) go over it with coverage alpha. r87: Jumbotron · Stadium the
  // same, its stadium often alone (the scorer over it when out).
  g.splitThisFrame = split_mode() && (count > 1 || !g.tabletop) && kinds[0] == kLayerStadium && slot.stadium.shared;
  slot.stadiumEye = -1;
  slot.mainDrawn = false;
  slot.mainOverlay = g.splitThisFrame;
  g.openTarget = 0;
  // r53: a replay frame that draws the broadcast picture leaves the split stadium as it was (each eye
  // keeps its last image and pose); the table still goes over it as an overlay.
  g.splitSkipStadium = g.splitThisFrame && g.stadiumRest && g.worldlessFrame == aurora_xr_current_frame();
  if (g.splitThisFrame && !g.splitSkipStadium) {
    wgpu::SharedTextureMemoryVkImageLayoutBeginState layoutBegin{};
    layoutBegin.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    layoutBegin.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    wgpu::SharedTextureMemoryBeginAccessDescriptor desc{};
    desc.nextInChain = &layoutBegin;
    desc.initialized = true;
    slot.stadium.accessOpen = static_cast<bool>(slot.stadium.shared.BeginAccess(slot.stadium.texture, &desc));
    if (slot.stadium.accessOpen) {
      slot.stadiumEye = g.stadiumNextEye;
      g.stadiumNextEye ^= 1;
    } else {
      g.splitThisFrame = false;
      slot.mainOverlay = false;
    }
  }
  if (g.singlePass) {
    plan_single_pass();
    // r43: in Dawn's access from here until the render worker has submitted this Aurora frame
    // (on_aurora_submit), whether or not the eye pass ends up drawing.
    g_slotFrame[g.slot].store(aurora_xr_current_frame(), std::memory_order_relaxed);
    g_slotState[g.slot].store(1, std::memory_order_release);
  }
  g.stereoLayers = count;
  g.stereoThisFrame = true;
  g.eyeSendStartNs = now_ns();
  return g.stereoLayers;
}

extern "C" int PortXrStereoSinglePass(void) { return g.singlePass ? 1 : 0; }

// eye 0 / 1: that eye's send; eye 2 (r43 single pass): one send for both.
extern "C" int PortXrStereoEyeLayer(int eye, int layer, float view[16], float proj[16], int* flags) {
  if (!g.stereoThisFrame || eye < 0 || eye > 2 || (eye == 2) != g.singlePass) {
    return 0;
  }
  const int idx = eye == 2 ? 0 : eye;  // the image / open pass
  StereoSlot& slot = g.slots[g.slot];
  EyeImage& img = slot.eye[idx];
  // 4x multisampling smooths the edges (Lance: grainy Tabletop); per mode, xr_tune "msaa".
  const bool stadiumLayer = !g.splitThisFrame && (g.frameLayerKind[0] == kLayerStadium ||
                                                  (g.stereoLayers > 1 && g.frameLayerKind[1] == kLayerStadium));
  const int samples = stadiumLayer ? g.msaaStadium : g.msaaTable;
  const bool stadiumHere = g.splitThisFrame && layer >= 0 && layer < 3 && g.frameLayerKind[layer] == kLayerStadium;
  if (stadiumHere && g.splitSkipStadium) {
    return 0;  // r53: the broadcast picture's turn in a replay
  }
  if (g.singlePass) {
    // r43: each layer goes into the main (wide, both eyes) pass or, split, the stadium's one-eye
    // pass; passes open and close in the GX stream, with their native-unit viewports and coverage
    // (Aurora does both when it gets there), so the game thread sends while the FIFO thread is
    // still on the normal view instead of waiting for it.
    const int want = stadiumHere ? 2 : 1;
    if (g.openTarget != want) {
      close_single_pass();
      EyeImage& target = stadiumHere ? slot.stadium : slot.eye[0];
      if (!target.accessOpen) {
        return 0;
      }
      if (stadiumHere) {
        GXAuroraXrEyePass(1, static_cast<unsigned char>(2 + g.slot), static_cast<unsigned char>(g.msaaStadium), 0);
      } else {
        // Over passthrough (1), or over the split stadium (2: solid objects drawn blended count as
        // covered, r43b's see-through ball), the image's alpha is coverage.
        GXAuroraXrEyePass(1, static_cast<unsigned char>(g.slot), static_cast<unsigned char>(samples),
                          g.mrMode ? 1 : (g.splitThisFrame ? 2 : 0));
        GXAuroraSetStereo(1, g.stereoT[0], g.stereoT[1], g.stereoClip[0], g.stereoClip[1]);
        g.stereoOn = true;
        slot.mainDrawn = true;
      }
      g.openTarget = want;
      g.eyePassOpen[0] = true;
    }
  } else if (layer == 0) {
    if (!img.accessOpen) {
      return 0;
    }
    aurora::gfx::ExternalPassTarget target;
    target.texture = img.texture;
    target.view = img.renderView;
    target.format = aurora::gfx::color_format();
    target.width = g.eyeWidth;
    target.height = g.eyeHeight;
    target.sampleCount = static_cast<uint32_t>(samples);
    // The re-send sets its viewport in the game's native units; let them span the eye image.
    aurora::gfx::set_offscreen_uses_native_logical_size(true);
    if (!aurora::gfx::create_pass_external(target)) {
      aurora::gfx::set_offscreen_uses_native_logical_size(false);
      return 0;
    }
    g.eyePassOpen[idx] = true;
    // create_pass_external drained the GX FIFO: safe to switch Aurora's coverage mode, which makes
    // solid draws write alpha 1 so the field is solid over passthrough (gx.hpp g_xrCoverage).
    aurora_gx_set_xr_coverage(g.mrMode);
  }
  if (!g.eyePassOpen[idx] || layer < 0 || layer >= g.stereoLayers || layer > 2) {
    return 0;
  }
  close_pool_measurement();
  const LayerKind kind = static_cast<LayerKind>(g.frameLayerKind[layer]);
  {
    // Last line of defence when a send outgrows its estimate mid-frame: leave this layer out.
    uint32_t used[kPools];
    aurora_gfx_pool_usage(used, kPools);
    for (int p = 0; p < kPools; ++p) {
      if (g.poolReserved[p] != 0 && used[p] + g.perSend[kind][p] * kPoolMargin > g.poolReserved[p] * 0.98) {
        ++g.layersDroppedSinceStats;
        return 0;
      }
    }
    g.measuring = true;
    g.measuringKind = kind;
    std::memcpy(g.measureStart, used, sizeof(used));
    g.decodedAtMeasure = aurora_xr_decoded_bytes();
  }
  // r44e (Lance: "make the characters that run up, SOLID, i should not be seeing through them"): over
  // passthrough a draw's own alpha was the scorer's coverage, and the characters' blended surfaces
  // (alpha-edged textures, drawn blended with depth writes) let the room show through. For the scorer
  // those count as solid, as over the split stadium (r43b's ball). It is the last layer; the pass's
  // end puts the mode back.
  if (kind == kLayerScorer && g.mrMode) GXAuroraXrCoverage(2);
  const M4 model = kind == kLayerTable ? table_model() : (kind == kLayerScorer ? scorer_model() : stadium_model());
  // r43: single pass sends from the centre with the union field of view (plan_single_pass); the
  // split stadium from the one eye it is drawn for this frame.
  const int viewEye = stadiumHere ? slot.stadiumEye : (eye == 2 ? 0 : eye);
  const bool centre = g.singlePass && !stadiumHere;
  const M4 v = m4_mul(model, m4_view_from_pose(centre ? g.centrePose : g.views[viewEye].pose));
  std::memcpy(view, v.m, sizeof(v.m));
  // One projection for every layer of an eye, so their depths compare.
  gx_projection(centre ? g.unionFov : g.views[viewEye].fov, 0.05f, 1500.0f, proj);
  *flags = kind == kLayerTable ? (kFlagSkipSky | kFlagTable)
                               : (kind == kLayerScorer ? (kFlagScorer | kFlagSkipSky) : kFlagSkipActors);
  return 1;
}

extern "C" void PortXrStereoEyeEnd(int eye) {
  const int idx = eye == 2 ? 0 : eye;
  if (eye == 2) {
    // r43: the open pass closed in the GX stream, after its last draw. The drain then only waits for
    // what the frame's end would wait for anyway, and lets the pool guard read what the sends used.
    if (g.openTarget != 0) {
      close_single_pass();
      AuroraGXSync();
    }
    close_pool_measurement();
    return;
  }
  close_pool_measurement();
  if (eye < 0 || eye > 1 || !g.eyePassOpen[idx]) {
    return;
  }
  // Closes the eye's pass; its output is the shared image itself, so nothing is snapshotted.
  aurora::gfx::ResolvedTargets out;
  aurora::gfx::ResolveDesc desc;
  desc.color = false;
  desc.depth = false;
  aurora::gfx::resolve_pass(desc, out);
  aurora_gx_set_xr_coverage(false);  // resolve_pass drained the FIFO
  aurora::gfx::set_offscreen_uses_native_logical_size(false);
  g.eyePassOpen[idx] = false;
}

extern "C" void PortXrStereoFrameEnd(void) {
  if (g.eyeSendStartNs != 0) {
    g.eyeSendNsSinceStats += now_ns() - g.eyeSendStartNs;
    g.eyeSendStartNs = 0;
  }
}

extern "C" int PortXrShouldQuit(void) { return g.quit ? 1 : 0; }

extern "C" void PortXrNoteLoopTimes(int ranGameFrame, unsigned long long xrBegin, unsigned long long auroraBegin,
                                    unsigned long long tasks, unsigned long long auroraEnd, unsigned long long xrEnd) {
  if (!g.active || !ranGameFrame) {
    return;
  }
  g.loopNs[0] += xrBegin;
  g.loopNs[1] += auroraBegin;
  g.loopNs[2] += tasks;
  g.loopNs[3] += auroraEnd;
  g.loopNs[4] += xrEnd;
  // r53: what a game frame takes before its PortXrEndFrame, for the hand-off wait's budget. Rises
  // at once, falls slowly, so one light frame does not open the wait for a heavy run.
  const double frame = static_cast<double>(xrBegin + auroraBegin + tasks + auroraEnd);
  g.gameFrameEmaNs = frame > g.gameFrameEmaNs ? frame : g.gameFrameEmaNs * 0.95 + frame * 0.05;
}

// --- Goal celebrations (Game.cpp) and the Tabletop player marker, drawn by xr_fx.cpp. ---

static void ensure_fx();

extern "C" void PortXrGoal(int team, int character, int superStrike, int hyper, int ownGoal, int humanPad) {
  if (!g.active) {
    return;
  }
  static const char* kNames[] = {"Birdo", "Daisy", "Donkey Kong", "Hammer Bro", "Koopa", "Luigi",
                                 "Mario", "Peach", "Toad", "Waluigi", "Wario", "Yoshi"};
  const char* who = (character >= 0 && character < 12) ? kNames[character] : "?";
  XLOG("celebration: GOAL team %d by %s (class %d)%s%s%s, scorer pad %d", team, who, character,
       superStrike ? " SUPER STRIKE" : "", hyper ? " perfect" : "", ownGoal ? " own goal" : "", humanPad);

  g.lastGoalOwn = ownGoal != 0;  // r85: the scorer card says so
  ensure_fx();
  if (g.fxReady) {
    xrfx::goal(team, character, superStrike != 0, hyper != 0, ownGoal != 0);
  }
  // The scorer's hands feel it: a long, strong buzz when the goal is yours (pad 0 = Touch), then a
  // thump with every cannon wave (PortXrEndFrame).
  g.celebBuzz = humanPad == xrnet::local_pad() && !ownGoal;
  g.buzzWave = 0;
  if (g.celebBuzz) {
    g.rumble.amplitude = 1.0f;
    g.rumble.seconds = superStrike ? 0.9f : 0.5f;
    g.rumble.startPending = true;
    g.rumble.stopPending = false;
  }
}

// The effects renderer starts when first needed (a goal, or the Tabletop marker): its eye images at
// 0.6 of the runtime's size (confetti does not need more, and the compositor scales the layer).
static void ensure_fx() {
  if (!g.fxReady && g.device != VK_NULL_HANDLE) {
    xrfx::DeviceInfo dev;
    dev.instance = g.instance;
    dev.session = g.session;
    dev.local = g.local;
    dev.device = g.device;
    dev.queue = g.queue;
    dev.queueFamily = g.queueFamily;
    dev.memProps = &g.memProps;
    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount,
                                      nullptr);
    std::vector<XrViewConfigurationView> views(viewCount, XrViewConfigurationView{XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (viewCount > 0 &&
        XR_SUCCEEDED(xrEnumerateViewConfigurationViews(g.instance, g.system, XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                                       viewCount, &viewCount, views.data()))) {
      dev.width = (static_cast<uint32_t>(views[0].recommendedImageRectWidth * 0.6f) / 8) * 8;
      dev.height = (static_cast<uint32_t>(views[0].recommendedImageRectHeight * 0.6f) / 8) * 8;
      // r61: the cup suite's room at the runtime's own size: you walk up to it and read it.
      dev.roomWidth = views[0].recommendedImageRectWidth;
      dev.roomHeight = views[0].recommendedImageRectHeight;
    }
    dev.maxAnisotropy = g.maxAnisotropy;
    dev.roomSamples = g.roomSamples;
    g.fxReady = xrfx::init(dev);
  }
}

extern "C" void PortXrSetGameplayLive(int live) {
  g.gameplayLive = live != 0;
  if (g.gameplayLive) {
    g.replayRunning = false;  // r53: play is back, whatever the phases said
    g.worldless = false;
  }
}

// --- r42: the life-size scorer's show, its banner, and the crowd volume panel ---

const char* scorer_name(int cls) {
  static const char* kNames[] = {"BIRDO", "DAISY", "DONKEY KONG", "HAMMER BRO", "KOOPA", "LUIGI", "MARIO",
                                 "PEACH", "TOAD", "WALUIGI", "WARIO", "YOSHI", "SUPER TEAM"};
  return cls >= 0 && cls < 13 ? kNames[cls] : "STRIKER";
}

// The banner in the scoring team's colours (by captain).
uint32_t team_colour(int captainClass) {
  switch (captainClass) {
    case 1: return 0xF08A1C;   // Daisy
    case 2: return 0x8B4A1C;   // Donkey Kong
    case 5: return 0x23A046;   // Luigi
    case 6: return 0xD8231E;   // Mario
    case 7: return 0xE8609E;   // Peach
    case 9: return 0x5B2C91;   // Waluigi
    case 10: return 0xB89A00;  // Wario
    case 11: return 0x4EA82A;  // Yoshi
    default: return 0x2E4A7A;  // Super Team / unknown
  }
}

extern "C" void PortXrScorerFrame(int phase, float phaseTime, float height, int characterClass, int side,
                                  int captainClass) {
  if (!g.active) {
    return;
  }
  auto& sc = g.scorer;
  if (phase == 0) {
    sc.phase = 0;
    sc.prevPhase = 0;
    sc.planned = false;
    sc.logged = false;
    return;
  }
  sc.reportNs = now_ns();
  if (sc.prevPhase == 0) {
    // A new show: its path from where you are now, and its banner drawn off this thread.
    sc.height = height;
    sc.cls = characterClass;
    sc.side = side;
    sc.captainClass = captainClass;
    plan_scorer();
    if (!g.bannerBusy) {
      g.bannerBusy = true;
      g_bannerReady = false;
      g.banner.hasImage = false;
      // r67: a guest scorer by its own name. r68h: a sidekick (Strikers' or a guest one) by its own
      // generated name (PortGuestSidekickName: the same every match).
      const char* guestName = PortGuestShownName(side, characterClass);
      const char* sidekickName = PortGuestSidekickName(side, g.scorerPlayerId);
      std::string name = scorer_name(characterClass);
      if (sidekickName != nullptr) {
        name = sidekickName;
      } else if (guestName != nullptr) {
        name = guestName;
      }
      for (char& ch : name) ch = (char)toupper((unsigned char)ch);
      const uint32_t colour = team_colour(captainClass);
      int goals = sc.statGoals;
      const int assists = sc.statAssists;
      const bool hatTrick = sc.statHatTrick;
      // r72: your franchise nemesis (their captain) scoring on you: called out, its title with it.
      // r73: or its face-off before kick-off (no stats line).
      std::string header;
      {
        std::lock_guard<std::mutex> lock(g_frMutex);
        // r81: a side's co-captain (include/port/lineup.h) is their nemesis only when it's the one by name
        const bool coCaptain = PortSecondCaptain(side) == characterClass;
        if (g_frCard.valid && g_frCard.nemesis && side == g_frCard.nemesisSide && sidekickName == nullptr &&
            (!coCaptain || strcasecmp(name.c_str(), g_frCard.nemesisName) == 0)) {
          header = g.scorerFaceOff ? "YOUR NEMESIS:" : "YOUR NEMESIS SCORES:";
          name += std::string(" ") + g_frCard.nemesisTitle;
          if (g.scorerFaceOff) goals = -1;
        }
      }
      const bool ownGoal = g.lastGoalOwn && !g.scorerFaceOff;  // r85
      std::thread([name, colour, goals, assists, hatTrick, header, ownGoal]() {
        std::vector<uint8_t> pixels(static_cast<size_t>(kBannerW) * kBannerH * 4);
        xrtext::goal_banner(pixels.data(), kBannerW, kBannerH, name.c_str(), colour, goals, assists, hatTrick,
                            header.empty() ? nullptr : header.c_str(), ownGoal);
        g_bannerPixels.swap(pixels);
        g_bannerReady = true;
      }).detach();
    }
    XLOG("scorer: %s (class %d, %.2f units tall -> x%.2f), side %d", scorer_name(characterClass), characterClass,
         height, scorer_height_m() / std::max(0.2f, height), side);
  }
  if (phase == 3 && sc.prevPhase != 3) {
    sc.leaveFrom = sc.prevPhase == 0 ? sc.spot : sc.lastPos;
  }
  // r43d: a hat trick throws the hats as the scorer arrives.
  if (phase == 2 && sc.prevPhase == 1 && sc.statHatTrick) {
    ensure_fx();
    if (g.fxReady) xrfx::hat_trick(sc.spot.x, sc.spot.y, sc.spot.z, g.mrMode);
  }
  sc.phase = phase;
  sc.phaseTime = phaseTime;
  sc.prevPhase = phase;
}

extern "C" void PortXrScorerStats(int goals, int assists, int hatTrick) {
  g.scorer.statGoals = goals;
  g.scorer.statAssists = assists;
  g.scorer.statHatTrick = hatTrick != 0;
}

extern "C" void PortXrScorerPlayer(int playerId) { g.scorerPlayerId = playerId; }  // r68h
extern "C" void PortXrScorerFaceOff(int faceoff) { g.scorerFaceOff = faceoff != 0; }  // r73

extern "C" void PortXrSetPaused(int paused) {
  const bool p = paused != 0;
  if (p && !g.paused) g.sliderPlaced = false;  // placed in front of you when the pause opens
  g.paused = p;
}

std::string crowd_path() {
  const char* home = std::getenv("STRIKERS_HOME");
  return std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/xr_crowd.txt";
}

// r46: the button map from xr_controls.txt ("A=a", "Z=rg", "L=none", ... one per line; anything
// missing or unknown keeps its default), looked at about once a second so a change made on the
// panel's Controls page while the game runs behind it applies when you come back.
void reload_controls() {
  const uint64_t t = now_ns();
  if (g.controlsCheckNs != 0 && t - g.controlsCheckNs < 1000000000ull) return;
  g.controlsCheckNs = t;
  const char* home = std::getenv("STRIKERS_HOME");
  const std::string path =
      std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/xr_controls.txt";
  struct stat st{};
  const long long stamp = ::stat(path.c_str(), &st) == 0 ? static_cast<long long>(st.st_mtime) * 1000 + st.st_size : 0;
  if (stamp == g.controlsStamp) return;
  g.controlsStamp = stamp;
  std::copy(kDefaultControls, kDefaultControls + kActCount, g.controls);
  if (FILE* f = std::fopen(path.c_str(), "r")) {
    char line[64];
    while (std::fgets(line, sizeof(line), f) != nullptr) {
      char key[16] = {}, value[16] = {};
      if (std::sscanf(line, " %15[A-Za-z] = %15[A-Za-z]", key, value) != 2) continue;
      for (int act = 0; act < kActCount; ++act) {
        if (strcasecmp(key, kActionNames[act]) != 0) continue;
        int source = strcasecmp(value, "none") == 0 ? kSrcNone : -1;
        for (int s = 0; s < kSrcCount && source < 0; ++s) {
          if (strcasecmp(value, kSourceNames[s]) == 0) source = s;
        }
        if (source >= 0) g.controls[act] = source;
      }
    }
    std::fclose(f);
  }
  char summary[160];
  int n = 0;
  for (int act = 0; act < kActCount && n < static_cast<int>(sizeof(summary)) - 16; ++act) {
    n += std::snprintf(summary + n, sizeof(summary) - n, "%s%s=%s", act ? " " : "", kActionNames[act],
                       g.controls[act] < kSrcCount ? kSourceNames[g.controls[act]] : "none");
  }
  XLOG("controls: %s", summary);
}

void load_crowd_level() {
  if (FILE* f = std::fopen(crowd_path().c_str(), "r")) {
    int level = 6;
    if (std::fscanf(f, "%d", &level) == 1) g.crowdLevel = std::max(0, std::min(10, level));
    std::fclose(f);
  }
}

void save_crowd_level() {
  if (FILE* f = std::fopen(crowd_path().c_str(), "w")) {
    std::fprintf(f, "%d\n", g.crowdLevel);
    std::fclose(f);
  }
}

// The crowd's loudness and place for the mixer: your level (6 = the game's own), and in the stadium
// where your head points (the crowd's voices sit round the stands).
void update_crowd_mix() {
  float fx = 0.0f, fz = -1.0f;
  if (g.headValid) floor_forward(g.headPose, &fx, &fz);
  PortAudioSetCrowdUser(static_cast<float>(g.crowdLevel) / 6.0f, g.mrMode ? 0 : 1, std::atan2(fx, -fz));
}

// r44g: your eye height in the life-size stadium (a friend of Lance's: "more controllability for the
// environment, as playing at different eye heights makes it hard to play comfortably"), kept beside
// the crowd volume. + puts you higher: the stadium goes down by that much under you.
std::string view_height_path() {
  const char* home = std::getenv("STRIKERS_HOME");
  return std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/xr_view_height.txt";
}

void load_view_height() {
  if (FILE* f = std::fopen(view_height_path().c_str(), "r")) {
    int cm = 0;
    if (std::fscanf(f, "%d", &cm) == 1) g.viewHeightCm = std::max(kViewHeightMinCm, std::min(kViewHeightMaxCm, cm));
    std::fclose(f);
  }
}

void save_view_height() {
  if (FILE* f = std::fopen(view_height_path().c_str(), "w")) {
    std::fprintf(f, "%d\n", g.viewHeightCm);
    std::fclose(f);
  }
}

// r44g, the pause settings panel (r42's crowd volume, r44's voice switch, the stadium height): the
// right stick up/down picks a row, left/right changes it (held: every quarter second).
void settings_input(XrVector2f stick) {
  if (!g.paused) {
    g.settingsStickDirY = 0;
    g.crowdRepeatNs = 0;
    return;
  }
  const uint64_t t = now_ns();
  auto changed = [&]() {
    g.slider.hasImage = false;  // redrawn at the next frame
    g.sliderShownUntilNs = t + 2000000000ull;
  };
  const int dirY = stick.y > 0.7f ? 1 : (stick.y < -0.7f ? -1 : 0);
  if (dirY != g.settingsStickDirY) {
    g.settingsStickDirY = dirY;
    const int row = std::max(0, std::min(2, g.settingsRow - dirY));  // up = the row above
    if (dirY != 0 && row != g.settingsRow) {
      g.settingsRow = row;
      changed();
    }
  }
  if (std::fabs(stick.x) < 0.6f || std::fabs(stick.y) > 0.7f) {
    g.crowdRepeatNs = 0;
    return;
  }
  if (g.crowdRepeatNs != 0 && t < g.crowdRepeatNs) return;
  const bool first = g.crowdRepeatNs == 0;
  g.crowdRepeatNs = t + (first ? 400000000ull : 220000000ull);
  const int step = stick.x > 0.0f ? 1 : -1;
  if (g.settingsRow == 0) {
    const int level = std::max(0, std::min(10, g.crowdLevel + step));
    if (level == g.crowdLevel) return;
    g.crowdLevel = level;
    save_crowd_level();
    XLOG("crowd volume %d/10", g.crowdLevel);
  } else if (g.settingsRow == 1) {
    if (!first || (step > 0) == xrvoice::enabled()) return;  // once per push: right on, left off
    xrvoice::set_enabled(step > 0);
  } else {
    const int cm = std::max(kViewHeightMinCm, std::min(kViewHeightMaxCm, g.viewHeightCm + 5 * step));
    if (cm == g.viewHeightCm) return;
    g.viewHeightCm = cm;
    save_view_height();
    XLOG("stadium height: you %+d cm", g.viewHeightCm);
  }
  changed();
}

// The game's full-screen flashes (the white flash in its replay wipes, the Super Strike photo
// flash) light up your whole view instead of only the game picture (Lance, 2026-10-10). Bright ones
// only: the black fades and the long dark tints stay on the game picture.
extern "C" int PortXrScreenFlash(float r, float gr, float b, float a) {
  // r42: not while the life-size scorer is out: over your whole view it washed the scorer out
  // (Lance: "they looked a bit transparent"); the flash stays on the game's own picture then.
  if (!g.active || std::max(r, std::max(gr, b)) < 0.5f || scorer_shown()) {
    return 0;
  }
  ensure_fx();
  if (!g.fxReady) {
    return 0;
  }
  xrfx::screen_flash(r, gr, b, a);
  return 1;
}

// Self-test (xr_fx "wipe"): Game.cpp plays the game's white-flash wipe once.
extern "C" int PortXrTakeTestWipe(void) {
  const bool take = g.testWipe;
  g.testWipe = false;
  return take ? 1 : 0;
}

// Self-test (xr_fx "item"): Game.cpp gives your team a green shell once.
extern "C" int PortXrTakeTestItem(void) {
  const bool take = g.testItem;
  g.testItem = false;
  return take ? 1 : 0;
}

// Self-test (xr_fx "sts"): Game.cpp shows the Super Strike meter once.
extern "C" int PortXrTakeTestSts(void) {
  const bool take = g.testSts;
  g.testSts = false;
  return take ? 1 : 0;
}

extern "C" void PortXrSetStsMeter(int shown, float x, float y, float z, float halfU, float halfV) {
  g.sts.shown = shown != 0;
  if (!g.sts.shown) {
    g.sts.logged = false;
    return;
  }
  g.sts.game[0] = x;
  g.sts.game[1] = y;
  g.sts.game[2] = z;
  g.sts.halfU = std::min(0.5f, std::max(0.01f, halfU));
  g.sts.halfV = std::min(0.5f, std::max(0.01f, halfV));
}

// Fielder.cpp UseTeamPowerup: the game took an item press (for the item check).
extern "C" void PortXrNoteItemUse(int current, int next, int blocked) {
  XLOG("item: use, current %d, next %d%s", current, next, blocked ? " (blocked)" : "");
}

extern "C" void PortXrLog(const char* msg) { XLOG("%s", msg); }

// The players on the Tabletop field, drawn bigger (Lance, 2026-10-10: tiny next to the pitch).
extern "C" float PortXrCharacterScale(void) {
  return (g.active && g.tabletop && !g.stereoFailed) ? g.charScale : 1.0f;
}

// The "that's you" marker over the player you control, in both Tabletop modes.
extern "C" void PortXrSetPlayerMarker(int shown, float x, float y, float z, int teamCaptain) {
  if (!g.active) {
    return;
  }
  if (teamCaptain >= 100 && teamCaptain != g.skinCaptain) {
    // r68c: a guest captain (Game.cpp, 100 + its roster place): its own colours and outline
    XLOG("controllers: your captain is guest %d, its own skin", teamCaptain - 100);
    g.skinCaptain = teamCaptain;
  }
  if (teamCaptain >= 0 && teamCaptain < 12 && teamCaptain != g.skinCaptain) {
    XLOG("controllers: your team's skin, captain class %d", teamCaptain);  // r53
    g.skinCaptain = teamCaptain;
  }
  const bool show = shown != 0 && g.tabletop && !g.stereoFailed;
  if (show) {
    ensure_fx();
  }
  if (g.fxReady) {
    xrfx::set_marker(show, x, y, z, teamCaptain);
  }
}

extern "C" int PortXrStadiumLayerActive(void) { return (PortXrStereoWorld() && !g.mrMode) ? 1 : 0; }

extern "C" int PortXrEyeViewAllowed(int view, int tableLayer) {
  const uint32_t mask = g.eyeViewMask & (tableLayer ? g.eyeViewMaskTable : g.eyeViewMaskStadium);
  return (view >= 0 && view < 32 && ((mask >> view) & 1u) != 0) ? 1 : 0;
}

// r44e-r49 showed a Quest 2 (no colour passthrough) the Super Strike's whole picture on a screen
// beside the field while one ran. r50 (Lance, for a Quest 2 player whose meter came out "oddly
// cropped" from that picture: "make sure that superstrike bar is appearing above the character"):
// every headset gets the meter alone over the captain, cut from the HUD-only picture, as r40-r44d
// did; the Super Strike itself is watched on the table. Game.cpp still reports one (kept for logs).
extern "C" void PortXrSetSuperStrike(int on) {
  if (!g.active || on == 0) return;
  if (now_ns() - g.superStrikeNs > 2000000000ull) XLOG("super strike under way");
  g.superStrikeNs = now_ns();
}

extern "C" int PortXrHudOnlyNormalView(void) {
  static int s_enabled = -1;
  if (s_enabled < 0) {
    const char* e = std::getenv("STRIKERS_XR_HUD_ONLY");
    s_enabled = (e != nullptr && e[0] == '0') ? 0 : 1;
  }
  return (s_enabled && g.active && g.tabletop && stereo_mode() && !g.stereoFailed && g.gameplayLive &&
          g.hasProjection)
             ? 1
             : 0;
}

// r53: glPlat.cpp asks once per normal send whether this frame's broadcast picture leaves the 3D world
// out during a Tabletop replay (see replayRunning); such a frame gets no readback, so the screen keeps
// the last real picture. Decided once per Aurora frame.
extern "C" int PortXrNormalViewWorldless(void) {
  const uint32_t frame = aurora_xr_current_frame();
  if (frame == g.worldlessFrame) return g.worldless ? 1 : 0;
  g.worldlessFrame = frame;
  g.worldless = false;
  g.stadiumRest = false;
  // A replay longer than the game's own hold (11.5 s, PORT_GOAL_SHOW_FRAMES) plus the skip and the
  // kickoff is a missed phase: drop it rather than hide the screen for good.
  if (g.replayRunning && now_ns() - g.replayStartNs > 30000000000ull) {
    XWARN("replay flag still up after 30 s: cleared");
    g.replayRunning = false;
  }
  if (g.replayRunning && g.active && g.tabletop && stereo_mode() && !g.stereoFailed && g.hasProjection && !g.paused &&
      !g.gameplayLive) {
    if (g.mrMode) {
      g.worldless = true;
    } else if (g.singlePass && g.stadiumSplit) {
      g.showParity ^= 1u;
      g.worldless = (g.showParity & 1u) != 0;
      g.stadiumRest = !g.worldless;
    }
  }
  if (g.worldless) aurora_xr_skip_readback(frame);
  return g.worldless ? 1 : 0;
}

extern "C" void PortXrCelebrationPhase(int phase) {
  if (!g.active) {
    return;
  }
  static const char* kPhases[] = {"?", "replay started", "replay played out", "skipped", "play live"};
  XLOG("celebration: %s", (phase >= 1 && phase <= 4) ? kPhases[phase] : "?");
  g.replayRunning = phase == 1;  // r53 (see replayRunning)
  if (phase == 1) g.replayStartNs = now_ns();
  if (g.fxReady) {
    xrfx::phase(phase);
  }
}

// --- The game's rumble (RumbleActions.cpp, platpad.cpp). Only pad 0 is the Touch controllers. ---

extern "C" void PortXrRumblePreset(int pad, int preset) {
  if (!g.active || pad != xrnet::local_pad()) {
    return;
  }
  // eRumbleActionPreset: small, medium, solid contact, shot contact, Super Strike (hyper), Super
  // Strike. The game's GameCube motor is on or off; the preset is how hard the hit was.
  constexpr float kAmplitude[] = {0.35f, 0.55f, 0.8f, 0.65f, 1.0f, 0.9f};
  if (preset >= 0 && preset < static_cast<int>(sizeof(kAmplitude) / sizeof(kAmplitude[0]))) {
    g.rumble.amplitude = kAmplitude[preset];
  }
}

extern "C" void PortXrRumbleStart(int pad, float seconds) {
  if (!g.active || pad != xrnet::local_pad()) {
    return;
  }
  g.rumble.startPending = true;
  g.rumble.stopPending = false;
  g.rumble.seconds = seconds;
}

extern "C" void PortXrRumbleStop(int pad) {
  if (!g.active || pad != xrnet::local_pad()) {
    return;
  }
  if (!g.rumble.startPending) {
    g.rumble.stopPending = true;
  }
}

// --- r55: item blasts in your room (Lance's "explosive MR": a Bob-omb blast "shakes the view and
// throws sparks onto your floor", a Chain Chomp lunge "makes the table jolt"). Presentation only.

namespace {

// Where a game point is in the room for the current view, and which way its embers fly: off the
// Tabletop field (up), across the life-size pitch (up), or out of the big screen toward you (from
// the broadcast picture's point). False when it is nowhere to be seen.
bool blast_point(float x, float y, float z, float ndcX, float ndcY, int ndcValid, XrVector3f* at, XrVector3f* dir,
                 float* unit) {
  if (g.tabletop && !g.stereoFailed && stereo_mode()) {
    *at = table_point(x, y, z);
    *dir = XrVector3f{0.0f, 1.0f, 0.0f};
    *unit = g.table.scale;
    return true;
  }
  if (!g.mrMode && stereo_mode()) {
    const M4 t = stadium_model();
    *at = XrVector3f{x * t.m[0][0] + y * t.m[1][0] + z * t.m[2][0] + t.m[3][0],
                     x * t.m[0][1] + y * t.m[1][1] + z * t.m[2][1] + t.m[3][1],
                     x * t.m[0][2] + y * t.m[1][2] + z * t.m[2][2] + t.m[3][2]};
    *dir = XrVector3f{0.0f, 1.0f, 0.0f};
    *unit = stadium_scale();
    return true;
  }
  // The big screen: the point on its picture, bursting out of it toward you.
  if (!ndcValid || !g.swapchainHasImage || g.swapchainWidth == 0) return false;
  const FreeScreen& s = g.screens[screen_room()];
  if (!s.placed) return false;
  const float w = s.width, h = s.width * static_cast<float>(g.swapchainHeight) / static_cast<float>(g.swapchainWidth);
  const float rx = std::cos(s.yaw), rz = -std::sin(s.yaw);  // the quad's +X
  const float nx = std::sin(s.yaw), nz = std::cos(s.yaw);   // its face, toward you
  *at = XrVector3f{s.position.x + rx * ndcX * 0.5f * w + nx * 0.02f, s.position.y + ndcY * 0.5f * h,
                   s.position.z + rz * ndcX * 0.5f * w + nz * 0.02f};
  *dir = XrVector3f{nx, 0.25f, nz};
  *unit = w / 40.0f;
  return true;
}

// A short buzz in both hands, unless the game's own rumble is already stronger.
void blast_buzz(float amplitude, float seconds) {
  if (g.rumble.startPending && g.rumble.amplitude >= amplitude) return;
  g.rumble.amplitude = amplitude;
  g.rumble.seconds = seconds;
  g.rumble.startPending = true;
  g.rumble.stopPending = false;
}

}  // namespace

extern "C" void PortXrItemBlast(float x, float y, float z, int size, float radius, int type, int throwerPad, float ndcX,
                                float ndcY, int ndcValid) {
  if (!g.active || !g.running) return;
  const float strength = 1.0f + static_cast<float>(std::max(0, std::min(2, size)));
  XrVector3f at{}, dir{};
  float unit = 1.0f;
  const bool seen = blast_point(x, y, z, ndcX, ndcY, ndcValid, &at, &dir, &unit);
  if (g.blastsLogged < 12) {
    ++g.blastsLogged;
    XLOG("items: blast (type %d, size %d, radius %.1f, thrower pad %d) at (%.1f, %.1f, %.1f) -> %s (%.2f, %.2f, %.2f)",
         type, size, radius, throwerPad, x, y, z, seen ? "room" : "not in view", at.x, at.y, at.z);
  }
  if (seen) {
    ensure_fx();
    if (g.fxReady) xrfx::blast(at, dir, unit, strength, 0);
  }
  // A warm flash through the room; a buzz. r89 (Lance on r88, Bowser's blasts in Jumbotron · Stadium:
  // "take the fucking screen shake OFF ... that shit got so fuckin absurd"): nothing jumps any more (the
  // life-size stadium moved with every blast, stacking).
  if (g.fxReady) xrfx::screen_flash(1.0f, 0.55f, 0.15f, (g.mrMode ? 0.1f : 0.14f) + 0.05f * strength);
  blast_buzz(0.25f + 0.2f * strength, 0.08f + 0.05f * strength);
}

extern "C" void PortXrChompSlam(float x, float y, float z, float ndcX, float ndcY, int ndcValid) {
  if (!g.active || !g.running) return;
  XrVector3f at{}, dir{};
  float unit = 1.0f;
  if (blast_point(x, y, z, ndcX, ndcY, ndcValid, &at, &dir, &unit)) {
    ensure_fx();
    if (g.fxReady) xrfx::blast(at, dir, unit, 2.0f, 1);
  }
  // r89: felt in the hands only (no screen shake, Lance)
  blast_buzz(0.7f, 0.18f);
  XLOG("items: Chain Chomp slam at (%.1f, %.1f)", x, y);
}

extern "C" void PortXrChompBite(float x, float y, float z, float vx, float vy) {
  if (!g.active || !g.running) return;
  (void)x, (void)y, (void)z, (void)vx, (void)vy;
  // r89: felt in the hands only (the shove of the field is gone with the rest of the shake, Lance)
  blast_buzz(0.55f, 0.12f);
}

// --- r57: the trophy shelf's cups (Game.cpp, StatsTracker.cpp) ---

extern "C" void PortXrSetTrophies(unsigned bits, const unsigned short* wins8) {
  if (!g.active || wins8 == nullptr) return;
  g.trophyBits = bits & 0xFFu;
  std::memcpy(g.trophyWins, wins8, sizeof(g.trophyWins));
  XLOG("shelf: cups won %02x (wins %u %u %u %u | super %u %u %u %u)", g.trophyBits, wins8[0], wins8[1], wins8[2], wins8[3],
       wins8[4], wins8[5], wins8[6], wins8[7]);
}

extern "C" void PortXrCupWon(int trophyType, int firstTime) {
  if (!g.active || trophyType < 0 || trophyType > 7) return;
  g.trophyBits |= 1u << trophyType;
  g.newCup = trophyType;
  g.newCupNs = now_ns();
  XLOG("shelf: cup %d won%s", trophyType, firstTime ? " for the first time" : "");
}
extern "C" int PortXrBeginFrame(void) {
  if (!g.active) {
    return 0;
  }
  poll_events();
  if (!g.running) {
    usleep(10000);
    return 0;
  }

  // xrWaitFrame/xrBeginFrame happen in PortXrEndFrame, after the game frame has been recorded and
  // handed to the GPU. The runtime holds xrWaitFrame until the GPU is done with the previous frame;
  // waiting there BEFORE recording (r2-r8) meant the CPU recorded while the GPU sat idle and then sat
  // idle itself while the GPU drew (Tabletop · Stadium: 25 ms + 28 ms a frame). Now the next frame
  // is recorded while the GPU draws this one. The eyes are predicted one display period past the
  // last frame's display time; the compositor reprojects from the poses each image was drawn with.
  g.frameBegun = true;  // this loop iteration ends with an XR frame (PortXrEndFrame)

  xrnet::poll();  // the online room, every headset frame
  sync_input();
  poll_triggers();
  // r61: the home ground: the panel's home captain pick (twice a second), and the cup suite.
  if (const uint64_t tHome = now_ns(); tHome - g.homePollNs > 500000000ull) {
    g.homePollNs = tHome;
    xrhome::poll();
  }
  update_suite_mode();
  // Before the game frame draws: xr_layers.cpp marks the life-size field parts while it classifies.
  PortXrSetStadiumEconomy(g.lodMinPx, 0.5f * static_cast<float>(g.eyeWidth ? g.eyeWidth : 1256),
                          g.skipTallField ? 1 : 0, (g.tabletop && !g.mrMode && !g.stereoFailed) ? 1 : 0);

  // The 3D modes need both eyes where they will be when this frame is shown, and the floor.
  g.viewsValid = false;
  const XrTime predicted = g.frameState.predictedDisplayTime != 0
                               ? g.frameState.predictedDisplayTime + g.frameState.predictedDisplayPeriod
                               : 0;
  if (!eye_mode()) {  // r42: or the life-size scorer, in any mode
    g.hasProjection = false;
  } else if (predicted != 0 && create_stereo_resources()) {
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = predicted;
    locate.space = g.local;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    g.views[0] = XrView{XR_TYPE_VIEW};
    g.views[1] = XrView{XR_TYPE_VIEW};
    if (XR_SUCCEEDED(xrLocateViews(g.session, &locate, &viewState, 2, &count, g.views)) && count == 2 &&
        (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
        (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0) {
      g.viewsValid = true;
    }
    if (g.stage != XR_NULL_HANDLE) {
      XrSpaceLocation floor{XR_TYPE_SPACE_LOCATION};
      if (XR_SUCCEEDED(xrLocateSpace(g.stage, g.local, predicted, &floor)) &&
          (floor.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
        g.floorY = floor.pose.position.y;
      }
    }
  }

  // The table comes to you: 0.9 m ahead of where you look, at about waist height, its long side
  // across your view (left stick tap, or xr_tune "tablehere").
  if (g.bringTable && g.viewsValid) {
    g.bringTable = false;
    const XrPosef& eye = g.views[0].pose;
    const XrQuaternionf& o = eye.orientation;
    float fx = -(2 * (o.x * o.z + o.w * o.y)), fz = -(1 - 2 * (o.x * o.x + o.y * o.y));
    const float len = std::max(1e-3f, std::sqrt(fx * fx + fz * fz));
    fx /= len;
    fz /= len;
    const XrVector3f head{0.5f * (g.views[0].pose.position.x + g.views[1].pose.position.x),
                          0.5f * (g.views[0].pose.position.y + g.views[1].pose.position.y),
                          0.5f * (g.views[0].pose.position.z + g.views[1].pose.position.z)};
    g.table.position = {head.x + fx * 0.9f, std::max(g.floorY + 0.75f, head.y - 0.55f), head.z + fz * 0.9f};
    g.table.yaw = std::atan2(-fx, -fz);
    XLOG("table brought to (%.2f, %.2f, %.2f) yaw %.2f", g.table.position.x, g.table.position.y,
         g.table.position.z, g.table.yaw);
  }

  // Real time since the last iteration, not the display period: when a 3D mode runs below 60 fps
  // every iteration takes longer than a period, and counting periods made one iteration in six skip
  // its game frame anyway (r2-r10). At 72 Hz with time to spare this still gives 60 game frames.
  const uint64_t tickNow = now_ns();
  if (g.lastTickNs != 0) {
    g.tickAccumNs += static_cast<int64_t>(tickNow - g.lastTickNs);
    // Online and slower than ~64 loops a second with game frames to run: halve the 3D view's rate
    // for the rest of the match (PortXrStereoFrameBegin).
    // r44 (Lance: online "got a bit wonky" with the Quest menu up): not while the app is out of focus
    // (the Quest menu, the headset off) nor for 3 s after: the system slows the app then, and that
    // alone used to halve the 3D view for the rest of the match. Back in focus, it starts over.
    // r44d (r44c log: "too slow for 60 online" a moment into every online match): the match loads in
    // one long frame just after it goes online, and that one interval tipped the average. Now only
    // ordinary iterations count (a load or a stall over 50 ms is not slowness), nothing in the first
    // 10 s online, and the average has to stay over the line for 3 s.
    const bool online = PortNetOnline() != 0;
    if (online && g.onlineSinceNs == 0) g.onlineSinceNs = tickNow;
    if (!online) g.onlineSinceNs = 0;
    // r44g (r44f log: tripped by the first seconds in the stadium, while its pipelines were built): a
    // view change starts it over, with the same 10 s grace as the match start.
    const int modeKey = (g.tabletop ? 1 : 0) | (g.mrMode ? 2 : 0);
    if (modeKey != g.lastModeKey) {
      g.lastModeKey = modeKey;
      g.modeChangedNs = tickNow;
      if (g.onlineHalfRate) XLOG("net: view changed, the 3D view back to every frame");
      g.onlineHalfRate = false;
    }
    const uint64_t interval = tickNow - g.lastTickNs;
    if (!g.focused) {
      g.focusBackNs = tickNow + 3000000000ull;
      g.loopIntervalEma = 13.9e6;
    } else if (tickNow >= g.focusBackNs && interval < 50000000ull) {
      g.loopIntervalEma = 0.9 * g.loopIntervalEma + 0.1 * static_cast<double>(interval);
    }
    const bool slow = online && g.focused && tickNow >= g.focusBackNs && tickNow - g.onlineSinceNs > 10000000000ull &&
                      tickNow - g.modeChangedNs > 10000000000ull && stereo_mode() && g.loopIntervalEma > 15.5e6;
    if (!slow) {
      g.slowSinceNs = 0;
    } else if (g.slowSinceNs == 0) {
      g.slowSinceNs = tickNow;
    }
    if (!online) {
      g.onlineHalfRate = false;
    } else if (!g.onlineHalfRate && slow && tickNow - g.slowSinceNs > 3000000000ull) {
      g.onlineHalfRate = true;
      XLOG("net: this headset is too slow for 60 online; its 3D view now updates every other frame");
    }
  } else {
    g.tickAccumNs += g.frameState.predictedDisplayPeriod;
  }
  g.lastTickNs = tickNow;
  if (g.tickAccumNs > 4 * kGameTickNs) {
    g.tickAccumNs = kGameTickNs;  // after a stall, catch up by one frame rather than a burst
  }
  const bool due = g.tickAccumNs >= kGameTickNs;

  // Online (include/port/xr_net.h): a game frame runs only once both players' input for it is
  // here; both pads come from the match, home on pad 0 and away on pad 1, the same on both
  // headsets. Waiting keeps the frame due, so it runs as soon as the friend's input arrives.
  // r39b: returning 1 does not use the frame up; main.cpp's PortNetCommitFrame does, once it runs.
  if (xrnet::lockstep()) {
    if (!due) return 0;
    auto to_net = [](const PADStatus& s) {
      xrnet::NetPad p;
      p.buttons = s.button;
      p.stickX = s.stickX;
      p.stickY = s.stickY;
      p.substickX = s.substickX;
      p.substickY = s.substickY;
      p.triggerLeft = s.triggerLeft;
      p.triggerRight = s.triggerRight;
      return p;
    };
    auto from_net = [](const xrnet::NetPad& p) {
      PADStatus s{};
      s.button = p.buttons;
      s.stickX = p.stickX;
      s.stickY = p.stickY;
      s.substickX = p.substickX;
      s.substickY = p.substickY;
      s.triggerLeft = p.triggerLeft;
      s.triggerRight = p.triggerRight;
      s.err = PAD_ERR_NONE;
      return s;
    };
    xrnet::NetPad pad0, pad1;
    if (!xrnet::step(to_net(g.localPad), &pad0, &pad1)) {
      g.tickAccumNs = std::min<int64_t>(g.tickAccumNs, kGameTickNs);
      return 0;
    }
    g.tickAccumNs -= kGameTickNs;
    const PADStatus s0 = from_net(pad0), s1 = from_net(pad1);
    PADSetVirtualStatus(0, &s0);
    PADSetVirtualStatus(1, &s1);
    g.port1Active = true;
    return 1;
  }
  // Offline: this headset is pad 0, every headset frame (keeps it connected without a Bluetooth pad;
  // Aurora merges one in, pad.cpp merge_virtual_status).
  PADSetVirtualStatus(0, &g.localPad);
  if (g.port1Active) {
    PADClearVirtualStatus(1);
    g.port1Active = false;
  }
  if (due) {
    g.tickAccumNs -= kGameTickNs;
    return 1;
  }
  return 0;
}

extern "C" void PortXrEndFrame(int ranGameFrame) {
  // r46: RetroAchievements, after the game frame: this frame's memory against the set (xr_ra.h).
  xrra::frame(ranGameFrame != 0);
  if (!g.active || !g.frameBegun) {
    return;
  }
  g.frameBegun = false;

  XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
  g.frameState = XrFrameState{XR_TYPE_FRAME_STATE};
  const uint64_t waitStart = now_ns();
  bool frameOk = xr_ok(xrWaitFrame(g.session, &waitInfo, &g.frameState), "xrWaitFrame");
  g.lastWaitReturnNs = now_ns();
  g.xrWaitNsSinceStats += g.lastWaitReturnNs - waitStart;
  if (frameOk) {
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    frameOk = xr_ok(xrBeginFrame(g.session, &beginInfo), "xrBeginFrame");
  }
  if (frameOk) {
    ++g.displayFramesSinceStats;
  }

  // Even without an XR frame to end, this game frame's eye images go back from Dawn (finish).
  int justRecorded = -1;  // r44: the eye slot this game frame drew into
  if (ranGameFrame) {
    const uint64_t t0 = now_ns();
    if (g.singlePass) {
      // r43: no wait for the render worker. This frame's eyes go out once it has submitted them
      // (finish_released_slots below, this or a following display frame), and the screen shows the
      // newest frame read back by then; the next game frame meanwhile runs alongside.
      // r44 (players: input lag): except with the big screen in your room, which has no eyes to
      // overlap: without the wait its picture was the frame before last (r42 waited here, at 60).
      if (!stereo_mode()) aurora::gfx::synchronize();
      upload_latest_frame();
      if (g.stereoThisFrame) {
        g.stereoThisFrame = false;
        justRecorded = static_cast<int>(g.slot);
        g.slot = (g.slot + 1) % kStereoSlots;
      }
    } else {
      aurora::gfx::synchronize();
      upload_latest_frame();
      if (g.stereoThisFrame) {
        g.stereoThisFrame = false;
        finish_stereo_frame(g.slot);
        g.slot = (g.slot + 1) % kStereoSlots;
      }
    }
    g.stereoNsSinceStats += now_ns() - t0;
    ++g.gameFramesSinceStats;
  }
  if (g.singlePass) {
    // r44: this frame's eyes usually reach the render worker's submit within a millisecond or two of
    // here; waiting that long puts them in this display frame instead of the next (a whole 13.9 ms
    // later, with poses a frame old). Bounded, so a slow worker costs no more than before.
    // r53 (Lance, cups 2026-10-20: Tabletop "did not feel like 60"): the wait only takes what the
    // next game frame can spare. His Tabletop · Stadium log: tasks 12.4 ms + 2.3 ms of this wait per
    // game frame (37% of them a frame late anyway) = 16.5 ms, past the 13.9 ms display period, so
    // the headset showed 59-60 frames a second instead of 72 for whole matches.
    if (justRecorded >= 0 && frameOk) {
      constexpr uint64_t kHandoffWaitNs = 2500000ull;
      const uint64_t waitFrom = now_ns();
      uint64_t budgetNs = kHandoffWaitNs;
      if (g.frameState.predictedDisplayPeriod > 0 && g.lastWaitReturnNs != 0) {
        // The next display frame's wait returns a period after this one did; before it, the rest
        // of this frame (~1 ms: effects, layers, xrEndFrame) and the next game frame must run.
        const int64_t slack = static_cast<int64_t>(g.lastWaitReturnNs) +
                              static_cast<int64_t>(g.frameState.predictedDisplayPeriod) -
                              static_cast<int64_t>(waitFrom) - static_cast<int64_t>(g.gameFrameEmaNs) - 1500000;
        budgetNs = slack <= 0 ? 0 : std::min<uint64_t>(kHandoffWaitNs, static_cast<uint64_t>(slack));
      }
      if (g.handoffWait && budgetNs > 0) {
        while (g_slotState[justRecorded].load(std::memory_order_acquire) == 1 &&
               now_ns() - waitFrom < budgetNs) {
          usleep(100);
        }
      }
      if (budgetNs < kHandoffWaitNs) ++g.handoffCut;
      if (g_slotState[justRecorded].load(std::memory_order_acquire) == 1) {
        ++g.handoffLate;
      } else {
        ++g.handoffSame;
      }
      g.cpuWaitNs += now_ns() - waitFrom;
    }
    finish_released_slots();
  }
  if (!frameOk) {
    return;
  }

  XrCompositionLayerPassthroughFB ptLayer{XR_TYPE_COMPOSITION_LAYER_PASSTHROUGH_FB};
  XrCompositionLayerProjection projLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  XrCompositionLayerProjection stadiumLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};  // r43 split
  XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
  XrCompositionLayerProjection fxLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  XrCompositionLayerQuad bannerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};  // r42
  XrCompositionLayerQuad sliderQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};  // r42
  XrCompositionLayerQuad raQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};      // r46
  XrCompositionLayerQuad achQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};     // r51
  XrCompositionLayerQuad achHeadQuad{XR_TYPE_COMPOSITION_LAYER_QUAD}; // r51b: its title bar
  XrCompositionLayerQuad waitQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};    // r47
  XrCompositionLayerQuad boardQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};  // r57: the badge board
  XrCompositionLayerQuad tipQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};    // r60: the pointed-at badge's card
  XrCompositionLayerQuad guestQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};    // r67: the guest picker
  XrCompositionLayerQuad guestTipQuad{XR_TYPE_COMPOSITION_LAYER_QUAD}; // r67: "Y: GUEST CHARACTERS"
  bool showGuestQuad = false, showGuestTip = false;                     // r68g: added last, on top
  XrCompositionLayerQuad frQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};       // r70: the franchise hub
  XrCompositionLayerQuad frTipQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};    // r70: "Y: FRANCHISE"
  bool showFrQuad = false, showFrTip = false;
  XrCompositionLayerQuad frIntroQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};  // r70b: the match's intro card
  XrCompositionLayerQuad frSeriesQuad{XR_TYPE_COMPOSITION_LAYER_QUAD}; // r70b: the playoff series board
  XrCompositionLayerQuad frLiveQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};   // r77: the live XP meter
  bool showFrIntro = false, showFrSeries = false, showFrLive = false;
  XrCompositionLayerQuad frTickerQuad{XR_TYPE_COMPOSITION_LAYER_QUAD}; // r73: the news wire
  XrCompositionLayerQuad frTvQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};     // r73: the TV
  bool showFrTicker = false, showFrTv = false;
  XrCompositionLayerProjection roomLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};  // r61: the cup suite's room
  // r61: the home ground's boards: by the shelf (or in the suite), its ticket, over the big screen,
  // the cup suite's table.
  XrCompositionLayerQuad homeQuads[4] = {{XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD},
                                         {XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD}};
  const XrCompositionLayerBaseHeader* layers[32];
  uint32_t layerCount = 0;

  // The celebration effects step every display frame, smooth whatever the game's frame rate, with
  // the eyes located for this display time (not the game frame's prediction).
  const uint64_t fxNow = now_ns();
  xrfx::Scene scene;
  scene.dt = g.lastFxNs != 0 ? static_cast<float>(fxNow - g.lastFxNs) / 1e9f : 0.0f;
  g.lastFxNs = fxNow;
  bool sceneViews = false;
  step_jolt();  // r55: item blasts shaking the field
  // r53: the controllers live in the effects layer; r61: and the home ground's pieces.
  if (g.focused) ensure_fx();
  if (g.fxReady) {
    XrViewLocateInfo locate{XR_TYPE_VIEW_LOCATE_INFO};
    locate.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locate.displayTime = g.frameState.predictedDisplayTime;
    locate.space = g.local;
    XrViewState viewState{XR_TYPE_VIEW_STATE};
    uint32_t count = 0;
    sceneViews = XR_SUCCEEDED(xrLocateViews(g.session, &locate, &viewState, 2, &count, scene.views)) && count == 2 &&
                 (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0 &&
                 (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
    if (g.stage != XR_NULL_HANDLE) {
      XrSpaceLocation floor{XR_TYPE_SPACE_LOCATION};
      if (XR_SUCCEEDED(xrLocateSpace(g.stage, g.local, g.frameState.predictedDisplayTime, &floor)) &&
          (floor.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0) {
        g.floorY = floor.pose.position.y;
      }
    }
    // r44g: the stadium's ground in the stadium; r61: the cup suite stands on your own floor.
    scene.floorY = g.inSuite ? g.floorY : mode_floor_y();
    scene.mr = g.mrMode;
    scene.tabletop = g.tabletop && !g.stereoFailed;
    // r46e (players): the field no longer floats up while the scorer celebrates (r44-r46d); the
    // scorer stands beside it instead (plan_scorer).
    g.tableLift = 0.0f;
    g.tableLiftOff = false;
    scene.tablePos = table_shown_pos();
    // r45d: your eyes under the field (raised for the scorer, or you crouched): its underside solid.
    scene.tableUnderside = scene.tabletop && g.headValid && g.headPose.position.y < scene.tablePos.y - 0.02f;
    if (scene.tableUnderside) ensure_fx();
    scene.tableYaw = g.table.yaw;
    scene.tableScale = g.table.scale;
    scene.charScale = PortXrCharacterScale();
    scene.stadiumScale = stadium_scale();
    scene.stadiumOrigin = XrVector3f{0.0f, stadium_floor_y(), kStandY * stadium_scale()};
    // r42: the confetti lands on the life-size scorer, not through it.
    scene.scorerValid = scorer_shown() && g.scorer.planned;
    scene.scorerStill = g.scorer.phase == 2;
    scene.scorerFeet = g.scorer.lastPos;
    scene.scorerRadius = 0.25f;  // r44e: nearer the body (it also hides what falls behind it, xr_fx)
    scene.scorerHeight = scorer_height_m();
    // r53: the controllers in your hands, where they are at this display time and what is pressed
    // (the action states hold until the next xrSyncActions), in your team's skin.
    scene.skinCaptain = g.skinCaptain;
    // r70c: an evolved Pokemon has no cell in etch.r8 (64 cells, the first 54 guests): its first form's
    scene.skinEtch = -1;
    if (g.skinCaptain >= 100) {
      int idx = g.skinCaptain - 100;
      for (int guard = 0; guard < 4 && PortGuestRosterEvolved(idx); ++guard)
        idx = PortGuestRosterFindAny(0, PortGuestRosterEvolvesFrom(idx));
      scene.skinEtch = idx >= 0 && idx < 64 ? 100 + idx : 0;
    }
    // r57: the trophy shelf (your room only).
    scene.shelfValid = g.mrMode && g.shelf.placed;
    scene.shelfPos = g.shelf.position;
    scene.shelfYaw = g.shelf.yaw;
    if (g.inSuite) {
      // r61: in the cup suite your trophies stand on a shelf to your right; r62: picked up and resized
      // there too (update_shelf, shelf_at).
      scene.suite = true;
      scene.shelfValid = true;
      scene.shelfPos = shelf_at();
      scene.shelfYaw = shelf_turn();
      // r61: the room you walk about in, and its live things (the home captain's).
      const int captain = xrhome::view().captain;
      // r73: a guest captain's series suite when this headset has it (its live extras are the
      // Strikers captains' own: none in a guest's), else the slot's
      std::string guestName;
      const std::string series = guest_suite(captain, &guestName);
      request_suite_room(series.empty() ? std::string(suite_name(captain)) : series);
      g.suiteGuestName = series.empty() ? std::string() : guestName;
      g.suiteCaptain = captain;
      scene.roomShown = true;
      scene.roomOrigin = g.suiteOrigin;
      scene.roomOrigin.y = g.floorY;
      scene.roomYaw = g.suiteYaw;
      scene.suiteCaptain = series.empty() ? captain : -1;
      ensure_fx();
    }
    scene.shelfCups = g.trophyBits;
    std::memcpy(scene.shelfWins, g.trophyWins, sizeof(scene.shelfWins));
    scene.shelfNewCup = g.newCup;
    scene.shelfNewAge = g.newCup >= 0 ? static_cast<float>(now_ns() - g.newCupNs) / 1e9f : 0.0f;
    for (int i = 0; i < 8; ++i) {
      scene.shelfCupFree[i] = g.shelf.cupFree[i];
      scene.shelfCupPose[i] = g.shelf.cupPose[i];
      scene.shelfCupScale[i] = g.shelf.cupScale[i];  // r60
    }
    if (g.focused && g.showControllers) {
      const XrSpace spaces[2] = {g.actions.gripSpaceL, g.actions.gripSpaceR};
      for (int h = 0; h < 2; ++h) {
        xrfx::Scene::Hand& hand = scene.hands[h];
        XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
        hand.valid = spaces[h] != XR_NULL_HANDLE &&
                     XR_SUCCEEDED(xrLocateSpace(spaces[h], g.local, g.frameState.predictedDisplayTime, &loc)) &&
                     (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
                     (loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
        if (!hand.valid) continue;
        hand.grip = loc.pose;
        hand.trigger = get_float(h == 0 ? g.actions.triggerL : g.actions.triggerR);
        hand.squeeze = get_float(h == 0 ? g.actions.squeezeL : g.actions.squeezeR);
        const XrVector2f stick = get_vec2(h == 0 ? g.actions.stickL : g.actions.stickR);
        hand.stick[0] = stick.x;
        hand.stick[1] = stick.y;
        hand.stickClick = get_bool(h == 0 ? g.actions.stickClickL : g.actions.stickClickR);
        hand.button1 = get_bool(h == 0 ? g.actions.x : g.actions.a);
        hand.button2 = get_bool(h == 0 ? g.actions.y : g.actions.b);
        hand.menu = h == 0 && get_bool(g.actions.menu);
      }
    }
  }

  if (g.frameState.shouldRender) {
    // r61: the cup suite: its room (and the world out of its window) behind everything else; the
    // game's screen and the boards hang over it, the effects over those. Called every frame so a room
    // let go after the suite also frees its memory.
    if (g.fxReady && sceneViews && xrfx::render_room(scene, &roomLayer)) {
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&roomLayer);
    }
    if (g.mrMode && g.pt.available && g.pt.layerRunning) {
      ptLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
      ptLayer.space = XR_NULL_HANDLE;
      ptLayer.layerHandle = g.pt.layer;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&ptLayer);
    }
    // The 3D picture. On display frames without a new game frame the last pair is submitted again
    // with the poses it was drawn for, and the compositor reprojects it.
    // r43 split stadium: the life-size stadium under the table, each eye as last drawn (the eyes take
    // turns), opaque. r87: Jumbotron · Stadium's too.
    const bool splitMode = split_mode();
    if (!splitMode) {
      // r87: two modes split now: back in one, its stadium starts again from fresh pictures
      g.stadiumValid[0] = g.stadiumValid[1] = false;
    }
    if (splitMode && g.stadiumValid[0] && g.stadiumValid[1]) {
      stadiumLayer.layerFlags = 0;
      stadiumLayer.space = g.local;
      stadiumLayer.viewCount = 2;
      stadiumLayer.views = g.stadiumViews;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&stadiumLayer);
    }
    if (stereo_mode() && g.hasProjection) {  // (r42: the scorer alone goes after the screen, below)
      // Over passthrough only what was drawn is solid: the eye passes clear to alpha 0 and force
      // alpha 255 on everything they draw (glPlat.cpp). r43: the same over the split stadium.
      projLayer.layerFlags =
          (g.mrMode || (splitMode && g.mainIsOverlay)) ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
      projLayer.space = g.local;
      projLayer.viewCount = 2;
      projLayer.views = g.projViews;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projLayer);
    }
    // r53: a Tabletop · Living Room replay plays on the table; the screen, which would only hold the
    // picture from before the goal, is away until play is live again.
    const bool replayOnTable = g.worldless && g.replayRunning && g.mrMode && g.tabletop;
    if (g.swapchainHasImage && !replayOnTable) {
      quad.layerFlags = 0;  // opaque: the GameCube framebuffer's alpha is not coverage
      quad.space = g.local;
      quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
      quad.subImage.swapchain = g.swapchain;
      quad.subImage.imageRect.offset = {0, 0};
      quad.subImage.imageRect.extent = {static_cast<int32_t>(g.swapchainWidth),
                                        static_cast<int32_t>(g.swapchainHeight)};
      quad.subImage.imageArrayIndex = 0;
      const float aspect = static_cast<float>(g.swapchainHeight) / static_cast<float>(g.swapchainWidth);
      const float drop = g.fxReady ? xrfx::jumbotron_drop() : 0.0f;
      // r49d (Lance, for a player: she "doesnt want it getting risen during a goal celebration"): in
      // Tabletop · Living Room the replay plays on the screen where it always is, over the field,
      // instead of a jumbotron hanging from the ceiling (r43c). Tabletop ·
      // Stadium keeps the stadium's own big screen coming down out of the sky.
      if (g.tabletop && !g.stereoFailed && drop > 0.0f && !g.mrMode) {
        // The replay jumbotron comes down from the top, beyond the field and facing you: from the
        // ceiling in the room, from the sky in the stadium (VISION.md).
        XrVector3f head = g.views[0].pose.position;
        if (sceneViews) {
          head = XrVector3f{0.5f * (scene.views[0].pose.position.x + scene.views[1].pose.position.x),
                            0.5f * (scene.views[0].pose.position.y + scene.views[1].pose.position.y),
                            0.5f * (scene.views[0].pose.position.z + scene.views[1].pose.position.z)};
        }
        float dx = g.table.position.x - head.x, dz = g.table.position.z - head.z;
        const float len = std::max(0.1f, std::sqrt(dx * dx + dz * dz));
        dx /= len;
        dz /= len;
        const float width = g.mrMode ? 1.5f : 9.0f;
        const float height = width * aspect;
        XrVector3f anchor;
        float startY, finalY;
        if (g.mrMode) {
          // Past the far side of the field, hanging from the ceiling: its top just under it, so it
          // sits above eye level rather than in front of your face (Lance: "not eye level").
          anchor = XrVector3f{g.table.position.x + dx * 0.6f, 0.0f, g.table.position.z + dz * 0.6f};
          const float ceiling = std::max(g.floorY + 2.3f, head.y + 0.45f);
          startY = ceiling + 0.5f * height;
          finalY = ceiling - 0.1f - 0.5f * height;
        } else {
          anchor = XrVector3f{head.x + dx * 14.0f, 0.0f, head.z + dz * 14.0f};
          startY = stadium_floor_y() + 30.0f;
          finalY = stadium_floor_y() + 3.0f + 0.5f * height;
        }
        anchor.y = startY + (finalY - startY) * drop;
        const float yaw = std::atan2(head.x - anchor.x, head.z - anchor.z);  // +Z of the quad toward you
        quad.pose.orientation = {0.0f, std::sin(0.5f * yaw), 0.0f, std::cos(0.5f * yaw)};
        quad.pose.position = anchor;
        quad.size.width = width;
        quad.size.height = height;
        if (drop > 0.95f && !g.dropLogged) {
          g.dropLogged = true;
          XLOG("jumbotron down: center (%.2f, %.2f, %.2f), %.1f x %.1f m, head (%.2f, %.2f, %.2f), floor %.2f",
               anchor.x, anchor.y, anchor.z, width, height, head.x, head.y, head.z, g.floorY);
        }
      } else if (g.tabletop && !g.stereoFailed) {
        g.dropLogged = false;
        // The scoreboard: the broadcast view (score, time, replays) hanging over the field.
        XrVector3f eyes = g.views[0].pose.position;
        if (sceneViews) {
          eyes = XrVector3f{0.5f * (scene.views[0].pose.position.x + scene.views[1].pose.position.x),
                            0.5f * (scene.views[0].pose.position.y + scene.views[1].pose.position.y),
                            0.5f * (scene.views[0].pose.position.z + scene.views[1].pose.position.z)};
        }
        // r49 (a player: "it would be cool if the scoreboard stayed still when you rotate the
        // camera"): turned to you, not to the table, so turning the field leaves it as it is.
        // r42-r48 turned it with the table.
        auto face_eyes = [&](XrCompositionLayerQuad& q) {
          const float yaw = std::atan2(eyes.x - q.pose.position.x, eyes.z - q.pose.position.z);
          q.pose.orientation = {0.0f, std::sin(0.5f * yaw), 0.0f, std::cos(0.5f * yaw)};
        };
        // The Super Strike meter, the middle of the game's picture (ShootToScoreMeter.cpp draws it
        // there), hanging over the captain turned to you, its bottom edge above the "that's you" arrow.
        auto meter_over_captain = [&](XrCompositionLayerQuad& q, float width, int32_t hw, int32_t hh) {
          q.subImage.imageRect.offset = {static_cast<int32_t>(g.swapchainWidth / 2) - hw,
                                         static_cast<int32_t>(g.swapchainHeight / 2) - hh};
          q.subImage.imageRect.extent = {2 * hw, 2 * hh};
          const float height = width * static_cast<float>(hh) / static_cast<float>(hw);
          XrVector3f anchor =
              table_point(g.sts.game[0], g.sts.game[1], g.sts.game[2] + 4.5f * PortXrCharacterScale());
          anchor.y += 0.5f * height;
          q.pose.position = anchor;
          face_eyes(q);
          q.size.width = width;
          q.size.height = height;
          return anchor;
        };
        // The meter square: readable at any table size; 8 game units (about a fifth of the field's
        // width) by default. ShootToScoreMeter.cpp's bars fit a 96-unit half square at the centre.
        const float meterWidth = std::min(0.5f, std::max(0.2f, 8.0f * g.table.scale));
        const int32_t meterHw = std::max(1, static_cast<int32_t>(g.swapchainWidth * g.sts.halfU));
        const int32_t meterHh = std::max(1, static_cast<int32_t>(g.swapchainHeight * g.sts.halfV));
        if (PortXrHudOnlyNormalView() && g.sts.shown) {
          // The Super Strike meter (Lance, alpha 1: the score-bar crop below cut it off, so the
          // timing presses had nothing to aim at): the HUD-only picture's middle, over the captain.
          const XrVector3f anchor = meter_over_captain(quad, meterWidth, meterHw, meterHh);
          if (!g.sts.logged) {
            g.sts.logged = true;
            XLOG("super strike meter: panel (%.2f, %.2f, %.2f), %.2f m wide, picture %dx%d at (%d, %d)", anchor.x,
                 anchor.y, anchor.z, meterWidth, 2 * meterHw, 2 * meterHh, quad.subImage.imageRect.offset.x,
                 quad.subImage.imageRect.offset.y);
          }
        } else if (PortXrHudOnlyNormalView()) {
          // Live play draws the HUD alone (glPlat.cpp): show just its score bar (names, score,
          // clock, items), a ribbon board over the field, instead of a mostly black screen.
          const int32_t x0 = static_cast<int32_t>(g.swapchainWidth * 0.13f);
          const int32_t y0 = static_cast<int32_t>(g.swapchainHeight * 0.03f);
          const int32_t w = static_cast<int32_t>(g.swapchainWidth * 0.74f);
          const int32_t h = std::max(1, static_cast<int32_t>(g.swapchainHeight * 0.11f));
          quad.subImage.imageRect.offset = {x0, y0};
          quad.subImage.imageRect.extent = {w, h};
          const float width = 0.9f;
          const float height = width * static_cast<float>(h) / static_cast<float>(w);
          quad.pose.position = {g.table.position.x, table_shown_pos().y + 0.4f + 0.5f * height,
                                g.table.position.z};
          face_eyes(quad);
          quad.size.width = width;
          quad.size.height = height;
        } else {
          // r50 (Lance, for a Quest 2 player: "make sure the scoreboard and screen back on in the
          // center"): the same over the field on every headset. r44f-r49 put a Quest 2's screen beside
          // the field and showed it the Super Strike's whole picture there.
          const float width = 0.75f;
          quad.pose.position = {g.table.position.x, table_shown_pos().y + 0.42f + 0.5f * width * aspect,
                                g.table.position.z};
          face_eyes(quad);
          quad.size.width = width;
          quad.size.height = width * aspect;
        }
      } else if (g.inSuite) {
        // r61: in the cup suite the screen hangs in its casing in front of the window (the room's,
        // tools/suites/suite_scene.py: 3.2 x 1.8 m, its middle 2.1 m up and 2.9 m ahead), fitted
        // into it whatever the picture's shape; it is part of the room, so it does not move.
        constexpr float kSuiteScreenW = 3.2f, kSuiteScreenH = 1.8f;
        const float width = std::min(kSuiteScreenW, kSuiteScreenH / std::max(aspect, 0.1f));
        quad.pose.position = suite_point(0.0f, 2.1f, -2.9f);
        quad.pose.orientation = yaw_quat(g.suiteYaw);
        quad.size.width = width;
        quad.size.height = width * aspect;
      } else {
        place_free_screen(quad, aspect);  // r42: wherever you put it
      }
      {
        // r43c: while the scorer is out, the screen rises to hang above its head and banner, and
        // comes back down after (eased over about half a second).
        // r49 (a player: "sometimes its in a good spot in the middle but sometimes its too high"): not
        // in Tabletop, where the scorer stands beside the field (r46e) and the screen stays put.
        const bool tableScreen = (g.tabletop && !g.stereoFailed) || g.inSuite;
        const float target = scorer_shown() && g.scorer.planned && !tableScreen
                                 ? screen_lift_over_scorer(quad.pose.position, quad.size.width, quad.size.height)
                                 : 0.0f;
        const float dt = scene.dt > 0.0f ? std::min(scene.dt, 0.1f) : 1.0f / 72.0f;
        g.screenLift += (target - g.screenLift) * std::min(1.0f, dt * 5.0f);
        if (g.screenLift < 0.005f && target == 0.0f) g.screenLift = 0.0f;
        quad.pose.position.y += g.screenLift;
      }
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quad);
      scene.screenValid = true;
      scene.screenPose = quad.pose;
      scene.screenWidth = quad.size.width;
      scene.screenHeight = quad.size.height;
      // r60: the free big screen, for the edge pointer (sync_input), and its edge lit while the left
      // pointer is on it (dim) or while it is held (bright).
      const bool freeScreen = !(g.tabletop && !g.stereoFailed) && !g.inSuite;  // r61: the suite's stays put
      g.shownScreenValid = freeScreen;
      g.shownScreenPose = quad.pose;
      g.shownScreenW = quad.size.width;
      g.shownScreenH = quad.size.height;
      const bool held = freeScreen && g.screens[screen_room()].grabHands != 0;
      if (freeScreen && (held || g.screenEdgeHover) && scene.frameCount < 4) {
        xrfx::Scene::Frame& f = scene.frames[scene.frameCount++];
        const XrVector3f fwd = q_rot(quad.pose.orientation, XrVector3f{0.0f, 0.0f, 0.01f});
        f.pose = quad.pose;
        f.pose.position = {quad.pose.position.x + fwd.x, quad.pose.position.y + fwd.y, quad.pose.position.z + fwd.z};
        f.thickness = std::max(0.015f, std::min(0.08f, 0.012f * quad.size.width));
        f.width = quad.size.width + 2.0f * f.thickness;
        f.height = quad.size.height + 2.0f * f.thickness;
        f.rgb = held ? 0xFFE27A : 0xFFFFFF;
        f.alpha = held ? 1.0f : 0.6f;
        if (!held) {
          scene.pointerValid[0] = true;
          scene.pointers[0] = g.screenEdgePoint;
        }
      }
    } else {
      g.shownScreenValid = false;
    }
    // r57: the RetroAchievements badge board over the trophy shelf (your room, with a login and the
    // pictures saved by the panel): looked at every 3 s, redrawn on a thread when anything changed,
    // hung above the shelf a little behind its trophies (the effects layer, over it, draws those).
    if (g.mrMode && g.shelf.placed) {
      const uint64_t tb = now_ns();
      if (tb - g.boardCheckNs > 3000000000ull && g_boardReadyGen.load() == g.boardGen) {
        g.boardCheckNs = tb;
        std::vector<xrra::AchievementRow> rows;
        uint32_t unl = 0, tot = 0;
        std::string why;
        xrra::achievement_rows(&rows, &unl, &tot, &why);
        const char* home = std::getenv("STRIKERS_HOME");
        const std::string dir = std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/ra-badges/";
        int files = 0;
        for (const xrra::AchievementRow& r : rows) {
          struct stat bst {};
          if (!r.badge.empty() && stat((dir + r.badge + ".rgba").c_str(), &bst) == 0 && bst.st_size == 64 * 64 * 4) ++files;
        }
        if (tot > 0 && (unl != g.boardUnlocked || files != g.boardFiles)) {
          g.boardUnlocked = unl;
          g.boardFiles = files;
          const int gen = ++g.boardGen;
          XLOG("shelf: badge board %u of %u unlocked, %d pictures", unl, tot, files);
          // r60: what this picture shows, for pointing at its badges once it is up.
          g.boardRowsPending = rows;
          g.boardHPending = static_cast<uint32_t>(kBoardHeader + ((rows.size() + kBoardColumns - 1) / kBoardColumns) * 76 + 40);
          std::thread([rows = std::move(rows), unl, tot, gen, dir]() {
            std::vector<std::vector<uint8_t>> pics(rows.size());
            std::vector<xrtext::BadgeTile> tiles(rows.size());
            for (size_t i = 0; i < rows.size(); ++i) {
              tiles[i].unlocked = rows[i].unlocked;
              tiles[i].rgba64 = nullptr;
              if (rows[i].badge.empty()) continue;
              if (FILE* f = std::fopen((dir + rows[i].badge + ".rgba").c_str(), "rb")) {
                pics[i].resize(64 * 64 * 4);
                if (std::fread(pics[i].data(), 1, pics[i].size(), f) == pics[i].size()) tiles[i].rgba64 = pics[i].data();
                std::fclose(f);
              }
            }
            const int boardRows = (static_cast<int>(rows.size()) + kBoardColumns - 1) / kBoardColumns;
            const uint32_t h = static_cast<uint32_t>(kBoardHeader + boardRows * 76 + 40);
            std::vector<uint8_t> px(static_cast<size_t>(kBoardW) * h * 4);
            char title[96];
            std::snprintf(title, sizeof(title), "RETROACHIEVEMENTS  %u / %u", unl, tot);
            xrtext::badge_board(px.data(), static_cast<int>(kBoardW), static_cast<int>(h), tiles.data(),
                                static_cast<int>(tiles.size()), kBoardColumns, title);
            std::lock_guard<std::mutex> lock(g_boardMutex);
            g_boardPixels = std::move(px);
            g_boardH = h;
            g_boardReadyGen = gen;
          }).detach();
        }
      }
      if (g.boardGen > 0 && g_boardReadyGen.load() == g.boardGen && g.boardShownGen != g.boardGen) {
        std::lock_guard<std::mutex> lock(g_boardMutex);
        panel_upload(g.board, g_boardPixels.data(), kBoardW, g_boardH);
        g.boardShownGen = g.boardGen;
        g.boardRows = g.boardRowsPending;
        g.boardH = g_boardH;
        g.boardHover = -1;
      }
      if (g.board.hasImage) {
        const float c = std::cos(g.shelf.yaw), sn = std::sin(g.shelf.yaw);
        const XrVector3f at{g.shelf.position.x - 0.09f * sn, g.shelf.position.y + 0.44f, g.shelf.position.z - 0.09f * c};
        place_panel(boardQuad, g.board, at, 0.62f);
        boardQuad.pose.orientation = {0.0f, std::sin(0.5f * g.shelf.yaw), 0.0f, std::cos(0.5f * g.shelf.yaw)};
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&boardQuad);
        update_board_hover(boardQuad, scene, layers, &layerCount, tipQuad);
      }
    } else {
      g.boardHover = -1;
    }
    // r61: the home ground: the home corner, the banners by the big screen, the Tabletop pennants, and
    // the cup suite.
    if (g.inSuite) {
      update_suite(scene, layers, &layerCount, homeQuads);
    } else {
      update_home(scene, layers, &layerCount, homeQuads);
      update_awards(scene, layers, &layerCount);  // r73
    }
    // r42: in the big screen room mode the eye picture holds only the life-size scorer, who stands
    // in front of the screen: over it.
    if (!stereo_mode() && scorer_shown() && g.hasProjection) {
      projLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
      projLayer.space = g.local;
      projLayer.viewCount = 2;
      projLayer.views = g.projViews;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projLayer);
    }
    // r42: the goal banner over the scorer, from when it arrives until it turns to run back.
    if (g_bannerReady.load()) {
      panel_upload(g.banner, g_bannerPixels.data(), kBannerW, kBannerH);
      g_bannerReady = false;
      g.bannerBusy = false;
    }
    // r53: a second after the scorer arrives, once its celebration's opening (the SIU's jump) is done.
    constexpr float kBannerDelay = 1.0f;
    if (scorer_shown() && g.banner.hasImage &&
        ((g.scorer.phase == 2 && g.scorer.phaseTime >= kBannerDelay) || g.scorer.phase == 3)) {
      const float t = g.scorer.phase == 2 ? g.scorer.phaseTime - kBannerDelay : g.scorer.phaseTime;
      // It pops up as the scorer arrives and is gone a quarter second into the run back.
      float pop = 1.0f;
      if (g.scorer.phase == 2) {
        pop = t < 0.22f ? 1.1f * t / 0.22f : (t < 0.38f ? 1.1f - 0.1f * (t - 0.22f) / 0.16f : 1.0f);
      } else {
        pop = std::max(0.0f, 1.0f - t / 0.25f);
      }
      if (pop > 0.01f) {
        // r43b: the scorer stops out in front now (up to 8 m); the banner keeps its size to the eye.
        // r49: over where the scorer really stands (kept out of your face, scorer_place).
        const XrVector3f& feet = g.scorer.lastPos;
        const float sdx = feet.x - g.headPose.position.x, sdz = feet.z - g.headPose.position.z;
        const float width = 1.1f * pop * std::max(1.0f, std::sqrt(sdx * sdx + sdz * sdz) / 2.5f);
        const float height = width * static_cast<float>(kBannerH) / static_cast<float>(kBannerW);
        const XrVector3f at{feet.x, g.scorer.spot.y + scorer_height_m() + 0.18f + 0.5f * height, feet.z};
        place_panel(bannerQuad, g.banner, at, width);
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&bannerQuad);
        // r44e: a solid for the celebration pieces (they land on it, bounce off it, hide behind it).
        scene.bannerValid = true;
        scene.bannerPose = bannerQuad.pose;
        scene.bannerWidth = bannerQuad.size.width;
        scene.bannerHeight = bannerQuad.size.height;
      }
    }
    // r42: the crowd volume while paused (and two seconds after a change), in front of you.
    const uint64_t nowNs = now_ns();
    // r51 / r51b: or, while open, the achievements list in its place, 0.8 m wide, about a metre out
    // at eye height: a window of eight rows over the tall picture, scrolled smoothly with the left
    // stick every display frame (r51 redrew a page per step on the game thread: frame drops and
    // skipping sound), under its title bar.
    if (g.achOpen && g.headValid) {
      if (!g.achShown && g_achReadyGen.load() == g.achGen) {
        std::lock_guard<std::mutex> lock(g_achMutex);
        panel_upload(g.achPanel, g_achListPixels.data(), kAchW, g_achListH);
        panel_upload(g.achHead, g_achHeadPixels.data(), kAchW, kAchHeadH);
        g.achListH = g_achListH;
        g.achShown = true;
      }
      if (!g.achPlaced) {
        float fx = 0.0f, fz = -1.0f;
        floor_forward(g.headPose, &fx, &fz);
        g.achPos = {g.headPose.position.x + fx * 1.1f, g.headPose.position.y - 0.12f,
                    g.headPose.position.z + fz * 1.1f};
        g.achPlaced = true;
      }
      if (g.achShown && g.achPanel.hasImage && g.achHead.hasImage) {
        const uint32_t viewH = std::min(kAchViewH, g.achListH);
        const float maxScroll = static_cast<float>(g.achListH - viewH);
        // Faster the further the stick is pushed, gentle near the middle: up to 7 rows a second.
        const float dt = scene.dt > 0.0f ? std::min(scene.dt, 0.1f) : 1.0f / 72.0f;
        const float push = std::fabs(g.achStickY);
        if (push > 0.15f) {
          const float v = (push - 0.15f) / 0.85f;
          g.achScroll -= (g.achStickY > 0.0f ? 1.0f : -1.0f) * v * v * 7.0f * kAchRowH * dt;
        }
        g.achScroll = std::max(0.0f, std::min(maxScroll, g.achScroll));
        const float width = 0.8f;
        place_panel(achQuad, g.achPanel, g.achPos, width);
        achQuad.subImage.imageRect.offset = {0, static_cast<int32_t>(std::lround(g.achScroll))};
        achQuad.subImage.imageRect.extent = {static_cast<int32_t>(kAchW), static_cast<int32_t>(viewH)};
        achQuad.size.height = width * static_cast<float>(viewH) / static_cast<float>(kAchW);
        const float headH = width * static_cast<float>(kAchHeadH) / static_cast<float>(kAchW);
        XrVector3f headAt = g.achPos;
        headAt.y += 0.5f * achQuad.size.height + 0.5f * headH + 0.01f;
        place_panel(achHeadQuad, g.achHead, headAt, width);
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&achHeadQuad);
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&achQuad);
      }
    } else {
      g.achPlaced = false;
    }
    // r67: the guest picker over the game's pick screens, about a metre out at eye height, drawn off
    // this thread on every move; or, while it is closed there, the small tip under it.
    if (g.gpOpen && g.headValid) {
      // r68e: the character select menu, big: everyone, a section per game.
      // r79: the co-captain step shows the captains (and which it can't be)
      const bool co = g.gpCo;
      const int role = co ? 0 : g.gpRole, side = g.gpSide, cursor = g.gpCursor;
      unsigned taken = co ? g.gpCoTaken : 0u;  // the Strikers captains the other side has (as the screen offers it now)
      if (!co)
        for (int t = 0; t < 8; ++t)
          if (!PortGuestPickerCaptainFree(t)) taken |= 1u << t;
      request_panel(g_guestPicker, g.gpGen, kGuestW, kGuestH, [co, role, side, cursor, taken](uint8_t* px, int w, int h) {
        const std::vector<MenuEntry>& menu = menu_entries(role);
        std::vector<xrtext::GuestTile> tiles;
        tiles.reserve(menu.size());
        for (const MenuEntry& e : menu) {
          const GuestPortrait& p = guest_portrait(e.portrait);
          xrtext::GuestTile t{p.rgba.empty() ? nullptr : p.rgba.data(), p.w, p.h, e.name.c_str(), e.tag.c_str()};
          t.row = e.row;
          t.col = e.col;
          t.frame = e.frame;
          t.taken = role == 0 && e.team >= 0 && (taken & (1u << e.team)) != 0;
          t.locked = PortGuestRosterLocked(e.pick);  // r70c: a silhouette until evolved in franchise
          tiles.push_back(t);
        }
        char title[128];
        std::snprintf(title, sizeof(title), "CHARACTER SELECT   -   TEAM %d: %s", side + 1,
                      co ? "CHOOSE A CO-CAPTAIN (2 CAPTAINS + 2 SIDEKICKS)"
                         : (role == 0 ? "CHOOSE YOUR CAPTAIN" : "CHOOSE YOUR SIDEKICKS"));
        xrtext::guest_picker(px, w, h, title, tiles.data(), static_cast<int>(tiles.size()),
                             role == 0 ? kMenuCaptainCols : kMenuSidekickCols, cursor,
                             co ? "STICK: MOVE   -   A: CO-CAPTAIN   -   X: NO CO-CAPTAIN   -   B: THE GAME'S OWN SCREEN"
                                : "STICK: MOVE   -   A: PICK   -   B: THE GAME'S OWN SCREEN   -   Y: THIS MENU");
      });
      if (show_panel(g_guestPicker, g.gpPanel)) {
        // r68g (Lance on r68f: "that shit was moving when i moved and it was disappearing behind the
        // game menu"): the menu is the game screen's own page: right on the big screen, as big as it,
        // facing the same way, 2 cm in front, and drawn over everything (after the effects layer,
        // below). Only without a screen in view, a spot in front of you, fixed once.
        if (scene.screenValid) {
          place_panel(guestQuad, g.gpPanel, scene.screenPose.position, 1.0f);
          const float panelHW = static_cast<float>(kGuestH) / static_cast<float>(kGuestW);
          // as big as the screen (a little over, to cover it), never under 1.4 m (Tabletop's is small)
          const float width =
              std::max(1.4f, std::min(scene.screenWidth * 1.04f, scene.screenHeight * 1.04f / panelHW));
          guestQuad.pose.orientation = scene.screenPose.orientation;
          const XrVector3f out = q_rot(scene.screenPose.orientation, XrVector3f{0.0f, 0.0f, 0.02f});
          guestQuad.pose.position = {scene.screenPose.position.x + out.x, scene.screenPose.position.y + out.y,
                                     scene.screenPose.position.z + out.z};
          guestQuad.size.width = width;
          guestQuad.size.height = width * panelHW;
          g.gpPlaced = false;
        } else {
          if (!g.gpPlaced) {
            float fx = 0.0f, fz = -1.0f;
            floor_forward(g.headPose, &fx, &fz);
            g.gpPos = {g.headPose.position.x + fx * 1.3f, g.headPose.position.y - 0.05f,
                       g.headPose.position.z + fz * 1.3f};
            g.gpPlaced = true;
          }
          place_panel(guestQuad, g.gpPanel, g.gpPos, 1.8f);
        }
        showGuestQuad = true;
      }
    } else {
      g.gpPlaced = false;
    }
    if (g.gpOffered && !g.gpOpen && g.headValid) {
      request_panel(g_guestTip, 1, kGuestTipW, kGuestTipH, [](uint8_t* px, int w, int h) {
        xrtext::guest_tip(px, w, h, "Y: CHARACTER SELECT");
      });
      if (show_panel(g_guestTip, g.gpTip)) {
        if (scene.screenValid) {
          // r68g: under the big screen's bottom edge, facing the same way
          place_panel(guestTipQuad, g.gpTip, scene.screenPose.position, 0.42f);
          guestTipQuad.pose.orientation = scene.screenPose.orientation;
          const XrVector3f down = q_rot(scene.screenPose.orientation,
                                        XrVector3f{0.0f, -0.5f * scene.screenHeight - 0.06f, 0.02f});
          guestTipQuad.pose.position = {scene.screenPose.position.x + down.x, scene.screenPose.position.y + down.y,
                                        scene.screenPose.position.z + down.z};
          g.gpTipPlaced = false;
        } else {
          if (!g.gpTipPlaced) {
            float fx = 0.0f, fz = -1.0f;
            floor_forward(g.headPose, &fx, &fz);
            g.gpTipPos = {g.headPose.position.x + fx * 1.0f, g.headPose.position.y - 0.42f,
                          g.headPose.position.z + fz * 1.0f};
            g.gpTipPlaced = true;
          }
          place_panel(guestTipQuad, g.gpTip, g.gpTipPos, 0.42f);
        }
        showGuestTip = true;
      }
    } else {
      g.gpTipPlaced = false;
    }
    // r70: franchise mode's hub, on the big screen like the character select menu; drawn off this thread
    // from the game thread's snapshot whenever it changes. While it is closed on the main menu, the tip.
    // The panels go where the character select menu's go (fr_place).
    auto fr_place = [&](XrCompositionLayerQuad& quad, Panel& panel, XrVector3f& pos, bool& placed, float panelHW,
                        bool tip) {
      if (scene.screenValid) {
        place_panel(quad, panel, scene.screenPose.position, 1.0f);
        quad.pose.orientation = scene.screenPose.orientation;
        XrVector3f off{0.0f, 0.0f, 0.02f};
        float width = std::max(1.4f, std::min(scene.screenWidth * 1.04f, scene.screenHeight * 1.04f / panelHW));
        if (tip) {
          off.y = -0.5f * scene.screenHeight - 0.06f;
          width = 0.42f;
        }
        const XrVector3f out = q_rot(scene.screenPose.orientation, off);
        quad.pose.position = {scene.screenPose.position.x + out.x, scene.screenPose.position.y + out.y,
                              scene.screenPose.position.z + out.z};
        quad.size.width = width;
        quad.size.height = width * panelHW;
        placed = false;
      } else {
        if (!placed) {
          float fx = 0.0f, fz = -1.0f;
          floor_forward(g.headPose, &fx, &fz);
          const float dist = tip ? 1.0f : 1.3f;
          pos = {g.headPose.position.x + fx * dist, g.headPose.position.y - (tip ? 0.42f : 0.05f),
                 g.headPose.position.z + fz * dist};
          placed = true;
        }
        place_panel(quad, panel, pos, tip ? 0.42f : 1.8f);
      }
    };
    if (g.frOpen && g.headValid && g.frMode == 3) {
      // r70c: the evolution scene, a new picture ~15 times a second while it moves (one at a time)
      const float t = static_cast<float>(nowNs - g.frEvoStartNs) / 1e9f;
      const bool moving = g.frEvoStage == 1 || (g.frEvoStage == 2 && t < 30.0f);
      const int tick = moving ? static_cast<int>(t * 15.0f) : 0;
      if (g_frEvo.ready.load() == g_frEvo.requested) {
        const bool threeD = g_evoShow.live;  // r96
        request_panel(g_frEvo, (g.frGen << 12) + tick * 2 + (threeD ? 1 : 0), kFrEvoW, kFrEvoH, [t, threeD](uint8_t* px, int w, int h) {
          FrSnapshot s;
          {
            std::lock_guard<std::mutex> lock(g_frMutex);
            s = g_frSnap;
          }
          draw_franchise_evolution(px, w, h, s, t, threeD);
        });
      }
      if (show_panel(g_frEvo, g.frEvoPanel)) {
        fr_place(frQuad, g.frEvoPanel, g.frPos, g.frPlaced, static_cast<float>(kFrEvoH) / static_cast<float>(kFrEvoW),
                 false);
        showFrQuad = true;
      }
    } else if (g.frOpen && g.headValid) {
      request_panel(g_franchise, g.frGen, kGuestW, kGuestH, [](uint8_t* px, int w, int h) {
        FrSnapshot s;
        {
          std::lock_guard<std::mutex> lock(g_frMutex);
          s = g_frSnap;
        }
        draw_franchise(px, w, h, s);
      });
      if (show_panel(g_franchise, g.frPanel)) {
        fr_place(frQuad, g.frPanel, g.frPos, g.frPlaced,
                 static_cast<float>(kGuestH) / static_cast<float>(kGuestW), false);
        showFrQuad = true;
      }
    } else {
      g.frPlaced = false;
    }
    if (g.frOnMenu && !g.frOpen && g.headValid) {
      request_panel(g_frTip, 1, kGuestTipW, kGuestTipH, [](uint8_t* px, int w, int h) {
        xrtext::guest_tip(px, w, h, "Y: FRANCHISE MODE");
      });
      if (show_panel(g_frTip, g.frTip)) {
        fr_place(frTipQuad, g.frTip, g.frTipPos, g.frTipPlaced,
                 static_cast<float>(kGuestTipH) / static_cast<float>(kGuestTipW), true);
        showFrTip = true;
      }
    } else {
      g.frTipPlaced = false;
    }
    // r73: the league in your room (a franchise career loaded, outside matches, the hub closed): the
    // news wire under the big screen (a line at a time, every 5 s) and the TV left of it (scores, the
    // top of the table, who's hot; a page every 7 s).
    if (g.frRoomOn && g.mrMode && !g.tabletop && scene.screenValid && g.headValid && !g.frOpen && !g.gpOpen &&
        !g.paused && !g.inSuite) {
      const uint64_t slice = nowNs / 5000000000ull;
      size_t lineCount = 0;
      {
        std::lock_guard<std::mutex> lock(g_frMutex);
        lineCount = g_frRoom.lines.size();
      }
      if (lineCount > 0) {
        const int idx = static_cast<int>(slice % lineCount);
        request_panel(g_frTickerP, g.frRoomGen * 1000 + idx, kFrTickerW, kFrTickerH, [idx](uint8_t* px, int w, int h) {
          std::string text;
          {
            std::lock_guard<std::mutex> lock(g_frMutex);
            if (idx < static_cast<int>(g_frRoom.lines.size())) text = g_frRoom.lines[idx];
          }
          xrtext::news_ticker(px, w, h, "SSL NEWS", text.c_str());
        });
        if (show_panel(g_frTickerP, g.frTicker)) {
          place_panel(frTickerQuad, g.frTicker, scene.screenPose.position, 1.0f);
          frTickerQuad.pose.orientation = scene.screenPose.orientation;
          const float width = std::max(1.0f, std::min(2.4f, 0.8f * scene.screenWidth));
          const float hw = static_cast<float>(kFrTickerH) / static_cast<float>(kFrTickerW);
          const float tipH = 0.42f * static_cast<float>(kGuestTipH) / static_cast<float>(kGuestTipW);
          const XrVector3f down = q_rot(scene.screenPose.orientation,
                                        XrVector3f{0.0f, -0.5f * scene.screenHeight - 0.08f - tipH - 0.5f * width * hw, 0.02f});
          frTickerQuad.pose.position = {scene.screenPose.position.x + down.x, scene.screenPose.position.y + down.y,
                                        scene.screenPose.position.z + down.z};
          frTickerQuad.size.width = width;
          frTickerQuad.size.height = width * hw;
          showFrTicker = true;
        }
      }
      const int page = static_cast<int>((nowNs / 7000000000ull) % 3);
      request_panel(g_frTvP, g.frRoomGen * 10 + page, kFrTvW, kFrTvH, [page](uint8_t* px, int w, int h) {
        FrRoom r;
        {
          std::lock_guard<std::mutex> lock(g_frMutex);
          r = g_frRoom;
        }
        draw_room_tv(px, w, h, r, page);
      });
      if (show_panel(g_frTvP, g.frTv)) {
        // left of the screen, past your banner there (home ground: size = 0.6 x the screen's height)
        const float bannerSize = std::max(0.45f, std::min(1.6f, 0.6f * scene.screenHeight));
        const float tvW = std::max(0.7f, std::min(1.1f, 0.42f * scene.screenWidth));
        const float x = -(0.5f * scene.screenWidth + 0.6f * bannerSize + 0.1f + 0.5f * tvW);
        place_panel(frTvQuad, g.frTv, scene.screenPose.position, tvW);
        frTvQuad.pose.orientation = scene.screenPose.orientation;
        const XrVector3f off = q_rot(scene.screenPose.orientation, XrVector3f{x, -0.1f * scene.screenHeight, 0.0f});
        frTvQuad.pose.position = {scene.screenPose.position.x + off.x, scene.screenPose.position.y + off.y,
                                  scene.screenPose.position.z + off.z};
        frTvQuad.size.width = tvW;
        frTvQuad.size.height = tvW * static_cast<float>(kFrTvH) / static_cast<float>(kFrTvW);
        showFrTv = true;
      }
    }
    // r70b: a franchise match's intro card (10 s from its intro: the round's logo, both clubs, the
    // series; in the regular season the week and records), over the big screen's lower half like a TV
    // graphic; then, through a playoff game, the series board under the screen.
    bool frCardValid = false, frPlayoff = false;
    {
      std::lock_guard<std::mutex> lock(g_frMutex);
      frCardValid = g_frCard.valid;
      frPlayoff = g_frCard.round > 0;
    }
    const bool frIntroOn = frCardValid && nowNs < g.frIntroUntilNs && g.headValid && !g.paused;
    if (frIntroOn) {
      request_panel(g_frIntro, g.frCardGen, kFrIntroW, kFrIntroH, [](uint8_t* px, int w, int h) {
        PortFranchiseCard c;
        {
          std::lock_guard<std::mutex> lock(g_frMutex);
          c = g_frCard;
        }
        draw_franchise_intro(px, w, h, c);
      });
      if (show_panel(g_frIntro, g.frIntro)) {
        const float hw = static_cast<float>(kFrIntroH) / static_cast<float>(kFrIntroW);
        if (scene.screenValid) {
          fr_place(frIntroQuad, g.frIntro, g.frIntroPos, g.frIntroPlaced, hw, false);
          // over the lower half, 80% of the screen's width
          const float width = 0.8f * scene.screenWidth;
          const XrVector3f out =
              q_rot(scene.screenPose.orientation, XrVector3f{0.0f, -0.18f * scene.screenHeight, 0.03f});
          frIntroQuad.pose.position = {scene.screenPose.position.x + out.x, scene.screenPose.position.y + out.y,
                                       scene.screenPose.position.z + out.z};
          frIntroQuad.size.width = width;
          frIntroQuad.size.height = width * hw;
        } else {
          fr_place(frIntroQuad, g.frIntro, g.frIntroPos, g.frIntroPlaced, hw, false);
        }
        showFrIntro = true;
      }
    } else {
      g.frIntroPlaced = false;
    }
    if (frCardValid && frPlayoff && !frIntroOn && scene.screenValid && g.headValid) {
      request_panel(g_frSeries, g.frCardGen, kFrSeriesW, kFrSeriesH, [](uint8_t* px, int w, int h) {
        PortFranchiseCard c;
        {
          std::lock_guard<std::mutex> lock(g_frMutex);
          c = g_frCard;
        }
        xrtext::guest_tip(px, w, h, franchise_series_text(c).c_str());
      });
      if (show_panel(g_frSeries, g.frSeries)) {
        place_panel(frSeriesQuad, g.frSeries, scene.screenPose.position, 0.6f);
        frSeriesQuad.pose.orientation = scene.screenPose.orientation;
        const float width = std::max(0.6f, std::min(1.4f, 0.45f * scene.screenWidth));
        const float hw = static_cast<float>(kFrSeriesH) / static_cast<float>(kFrSeriesW);
        const XrVector3f down = q_rot(scene.screenPose.orientation,
                                      XrVector3f{0.0f, -0.5f * scene.screenHeight - 0.5f * width * hw - 0.03f, 0.02f});
        frSeriesQuad.pose.position = {scene.screenPose.position.x + down.x, scene.screenPose.position.y + down.y,
                                      scene.screenPose.position.z + down.z};
        frSeriesQuad.size.width = width;
        frSeriesQuad.size.height = width * hw;
        showFrSeries = true;
      }
    }
    // r77: the live XP meter through your franchise match (not over the intro card, not while paused):
    // under the big screen (under the series board in the playoffs); in Tabletop over the score bar
    bool frLiveValid = false, frLiveCo = false;
    {
      std::lock_guard<std::mutex> lock(g_frMutex);
      frLiveValid = g_frLiveData.valid;
      frLiveCo = g_frLiveData.co;
    }
    if (frLiveValid && !frIntroOn && scene.screenValid && g.headValid && !g.paused) {
      // r79: two meters high with a co-captain
      request_panel(g_frLive, g.frLiveGen, kFrLiveW, frLiveCo ? 2 * kFrLiveH : kFrLiveH, [](uint8_t* px, int w, int h) {
        PortFranchiseLive l;
        FrLiveGlow glow;
        {
          std::lock_guard<std::mutex> lock(g_frMutex);
          l = g_frLiveData;
          glow = g_frLiveGlow;
        }
        draw_franchise_live(px, w, h, l, glow);
      });
      if (show_panel(g_frLive, g.frLive)) {
        place_panel(frLiveQuad, g.frLive, scene.screenPose.position, 0.6f);
        frLiveQuad.pose.orientation = scene.screenPose.orientation;
        const bool table = g.tabletop && !g.stereoFailed;
        const float width = table ? 0.6f : std::max(0.6f, std::min(1.2f, 0.4f * scene.screenWidth));
        const float hw = g.frLive.width > 0 ? static_cast<float>(g.frLive.height) / static_cast<float>(g.frLive.width)
                                            : static_cast<float>(kFrLiveH) / static_cast<float>(kFrLiveW);
        float dy;
        if (table) {
          dy = 0.5f * scene.screenHeight + 0.5f * width * hw + 0.03f;
        } else {
          dy = -0.5f * scene.screenHeight - 0.5f * width * hw - 0.03f;
          if (showFrSeries) dy -= frSeriesQuad.size.height + 0.02f;
        }
        const XrVector3f off = q_rot(scene.screenPose.orientation, XrVector3f{0.0f, dy, 0.02f});
        frLiveQuad.pose.position = {scene.screenPose.position.x + off.x, scene.screenPose.position.y + off.y,
                                    scene.screenPose.position.z + off.z};
        frLiveQuad.size.width = width;
        frLiveQuad.size.height = width * hw;
        showFrLive = true;
      }
    }
    if (!g.achOpen && (g.paused || nowNs < g.sliderShownUntilNs)) {
      if (!g.slider.hasImage) {
        std::vector<uint8_t> pixels(static_cast<size_t>(kSliderW) * kSliderH * 4);
        const int voice = !xrvoice::enabled() ? 0 : (xrvoice::mic_allowed() ? 1 : 2);  // r44
        xrtext::settings_panel(pixels.data(), kSliderW, kSliderH, g.crowdLevel, 10, voice, g.viewHeightCm,
                               g.settingsRow);
        panel_upload(g.slider, pixels.data(), kSliderW, kSliderH);
      }
      if (!g.sliderPlaced && g.headValid) {
        float fx = 0.0f, fz = -1.0f;
        floor_forward(g.headPose, &fx, &fz);
        g.sliderPose.position = {g.headPose.position.x + fx * 1.0f, g.headPose.position.y - 0.38f,
                                 g.headPose.position.z + fz * 1.0f};
        g.sliderPlaced = true;
      }
      if (g.slider.hasImage && g.sliderPlaced) {
        place_panel(sliderQuad, g.slider, g.sliderPose.position, 0.5f);
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&sliderQuad);
      }
    }
    // The celebration over everything else.
    const uint64_t fxStart = now_ns();
    if (g.fxReady && sceneViews) update_evo(scene, nowNs);  // r96: the 3D evolution
    // r96: while a Pokemon evolves in 3D in front of the big screen, the evolution card is under the
    // effects layer (its backdrop), not over it (it would hide the Pokemon standing before it)
    const bool frUnder = showFrQuad && g.frMode == 3 && g_evoShow.live;
    if (frUnder) {
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&frQuad);
      showFrQuad = false;
    }
    if (g.fxReady && sceneViews && xrfx::update(scene) && xrfx::render(scene, &fxLayer)) {
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&fxLayer);
    }
    g.fxNs += now_ns() - fxStart;
    // r68g: the character select menu (and its tip) over everything, the effects layer's flags,
    // pennants and controllers too.
    if (showGuestQuad) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&guestQuad);
    if (showGuestTip) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&guestTipQuad);
    if (showFrQuad) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&frQuad);
    if (showFrTip) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&frTipQuad);
    if (showFrIntro && layerCount < 32) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&frIntroQuad);
    if (showFrSeries && layerCount < 32) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&frSeriesQuad);
    if (showFrLive && layerCount < 32) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&frLiveQuad);
    if (showFrTicker && layerCount < 32) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&frTickerQuad);
    if (showFrTv && layerCount < 32) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&frTvQuad);
    // r47: waiting for the online friend at the match start: who we wait for and how to stop, a
    // little over a metre out at about eye level, following your head lazily.
    // r64: the cup (co-op) wording, and a friend waiting at the other kind of match (never pairs).
    const int waiting = xrnet::waiting_for_friend();
    const int waitShown = waiting == 0 ? 0
                                       : waiting + (xrnet::waiting_in_cup() ? 10 : 0) + (xrnet::room_maker() ? 20 : 0) +
                                             (xrnet::waiting_in_franchise() ? 40 : 0) + (xrnet::waiting_in_league() ? 80 : 0);
    if (waiting != 0 && g.headValid) {
      if (waitShown != g.netWaitDrawn || !g.netWait.hasImage) {
        std::vector<uint8_t> pixels(static_cast<size_t>(xrra::kToastW) * xrra::kToastH * 4);
        char header[48];
        const bool cup = xrnet::waiting_in_cup();
        const bool fr = xrnet::waiting_in_franchise();  // r76
        const bool lgWait = xrnet::waiting_in_league();  // r100
        std::snprintf(header, sizeof(header),
                      lgWait ? "ONLINE LEAGUE  -  ROOM %s"
                             : (fr ? "CO-OP FRANCHISE  -  ROOM %s" : (cup ? "CO-OP CUP  -  ROOM %s" : "ONLINE  -  ROOM %s")),
                      xrnet::room_code());
        const char* title = waiting == 1 ? "WAITING FOR YOUR FRIEND TO JOIN"
                            : waiting == 3 ? (fr ? "YOUR FRIEND WAITS AT ANOTHER MATCH"
                                                 : (cup ? "YOUR FRIEND STARTED A GRUDGE MATCH" : "YOUR FRIEND STARTED A CUP MATCH"))
                                           : "YOUR FRIEND IS HERE";
        const char* body;
        if (lgWait) {
          if (waiting != 3) title = waiting == 1 ? "WAITING FOR THE OTHER CLUB'S PERSON" : "THEY'RE HERE";
          body = "YOUR LEAGUE GAME STARTS ON BOTH HEADSETS BY ITSELF. HOLD B TO CALL IT OFF (READY AGAIN LATER)";
        } else if (fr) {
          body = waiting == 3 ? "THEY BACK OUT OF IT: THEIR HEADSET THEN JOINS THIS ONE BY ITSELF. HOLD B TO PLAY ALONE"
                              : "THEIR HEADSET JOINS THIS MATCH BY ITSELF (YOUR CO-OP CAREER). HOLD B TO PLAY ALONE";
        } else if (waiting == 3) {
          body = cup ? "FOR CO-OP THEY GO INTO A CUP AND START ITS NEXT MATCH. HOLD B TO PLAY ALONE"
                     : "FOR A GRUDGE MATCH THEY BACK OUT AND START ONE. HOLD B TO PLAY THE CPU";
        } else if (cup) {
          body = xrnet::room_maker() ? "STARTS WHEN THEY START A CUP MATCH. YOU ARE THE CAPTAIN. HOLD B TO PLAY ALONE"
                                     : "STARTS WHEN THEY START A CUP MATCH TOO. HOLD B TO PLAY ALONE";
        } else {
          body = waiting == 1 ? "STARTS WHEN THEY JOIN AND START A GRUDGE MATCH. HOLD B TO PLAY THE CPU"
                              : "STARTS WHEN THEY START A GRUDGE MATCH TOO. HOLD B TO PLAY THE CPU";
        }
        xrtext::achievement_toast(pixels.data(), xrra::kToastW, xrra::kToastH, header, title, body, 0);
        panel_upload(g.netWait, pixels.data(), xrra::kToastW, xrra::kToastH);
        g.netWaitDrawn = waitShown;
      }
      float fx = 0.0f, fz = -1.0f;
      floor_forward(g.headPose, &fx, &fz);
      const XrVector3f want{g.headPose.position.x + fx * 1.3f, g.headPose.position.y - 0.12f,
                            g.headPose.position.z + fz * 1.3f};
      if (!g.netWaitPlaced) {
        g.netWaitPos = want;
        g.netWaitPlaced = true;
      } else {
        const float dt = scene.dt > 0.0f ? std::min(scene.dt, 0.1f) : 1.0f / 72.0f;
        const float k = std::min(1.0f, dt * 2.0f);
        g.netWaitPos.x += (want.x - g.netWaitPos.x) * k;
        g.netWaitPos.y += (want.y - g.netWaitPos.y) * k;
        g.netWaitPos.z += (want.z - g.netWaitPos.z) * k;
      }
      if (g.netWait.hasImage) {
        place_panel(waitQuad, g.netWait, g.netWaitPos, 0.8f);
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&waitQuad);
      }
    } else {
      g.netWaitPlaced = false;
    }
    // r46: a RetroAchievements pop-up (xr_ra.h), over everything: five seconds, a metre out and a
    // little below your eyes, following your head lazily, with a short buzz as it appears.
    if (nowNs >= g.raToastUntilNs) {
      std::vector<uint8_t> pixels;
      if (xrra::take_toast(pixels) && pixels.size() == static_cast<size_t>(xrra::kToastW) * xrra::kToastH * 4) {
        panel_upload(g.raToast, pixels.data(), xrra::kToastW, xrra::kToastH);
        g.raToastShownNs = nowNs;
        g.raToastUntilNs = nowNs + 5000000000ull;
        g.raToastPlaced = false;
        g.rumble.amplitude = 0.5f;
        g.rumble.seconds = 0.15f;
        g.rumble.startPending = true;
        g.rumble.stopPending = false;
      }
    }
    if (nowNs < g.raToastUntilNs && g.raToast.hasImage && g.headValid) {
      float fx = 0.0f, fz = -1.0f;
      floor_forward(g.headPose, &fx, &fz);
      const XrVector3f want{g.headPose.position.x + fx * 1.0f, g.headPose.position.y - 0.3f,
                            g.headPose.position.z + fz * 1.0f};
      if (!g.raToastPlaced) {
        g.raToastPos = want;
        g.raToastPlaced = true;
      } else {
        const float dt = scene.dt > 0.0f ? std::min(scene.dt, 0.1f) : 1.0f / 72.0f;
        const float k = std::min(1.0f, dt * 3.0f);
        g.raToastPos.x += (want.x - g.raToastPos.x) * k;
        g.raToastPos.y += (want.y - g.raToastPos.y) * k;
        g.raToastPos.z += (want.z - g.raToastPos.z) * k;
      }
      // Pops up over a fifth of a second and shrinks away over the last quarter.
      const float t = static_cast<float>(nowNs - g.raToastShownNs) / 1e9f;
      const float left = static_cast<float>(g.raToastUntilNs - nowNs) / 1e9f;
      const float pop = std::min({1.0f, t / 0.2f, left / 0.25f});
      if (pop > 0.02f) {
        place_panel(raQuad, g.raToast, g.raToastPos, 0.62f * pop);
        layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&raQuad);
      }
    }
    if (g.fxReady && g.celebBuzz && xrfx::cannon_waves_fired() > g.buzzWave) {
      g.buzzWave = xrfx::cannon_waves_fired();
      if (g.buzzWave > 1) {  // the first wave rides on the goal buzz
        g.rumble.amplitude = 1.0f;
        g.rumble.seconds = 0.22f;
        g.rumble.startPending = true;
        g.rumble.stopPending = false;
      }
    }
  }
  XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
  endInfo.displayTime = g.frameState.predictedDisplayTime;
  endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
  // r73: never more layers than the compositor takes (a frame over it fails whole): the last ones
  // (the topmost extras) go, logged once a while
  if (g.maxLayers > 0 && layerCount > g.maxLayers) {
    static uint64_t s_loggedNs = 0;
    if (now_ns() - s_loggedNs > 10000000000ull) {
      s_loggedNs = now_ns();
      XLOG("layers: %u this frame, the compositor takes %u: the last %u left out", layerCount, g.maxLayers,
           layerCount - g.maxLayers);
    }
    layerCount = g.maxLayers;
  }
  endInfo.layerCount = layerCount;
  endInfo.layers = layers;
  const uint64_t endStart = now_ns();
  xr_ok(xrEndFrame(g.session, &endInfo), "xrEndFrame");
  g.xrEndNs += now_ns() - endStart;

  log_stats();
}

extern "C" void PortXrShutdown(void) {
  aurora_xr_set_frame_callback(nullptr, nullptr);
  aurora_xr_set_submit_callback(nullptr, nullptr);
  aurora_xr_set_surface_suppressed(false);
  xrra::shutdown();
  xrctrl::stop();  // r54: before the session goes
  xrfx::shutdown();
  if (g.device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(g.device);
    for (uint32_t s = 0; s < kStereoSlots; ++s) {
      StereoSlot& slot = g.slots[s];
      for (int eye = 0; eye < 2; ++eye) {
        EyeImage& img = slot.eye[eye];
        img.renderView = nullptr;
        img.texture = nullptr;
        img.shared = nullptr;
        if (img.dawnDone != VK_NULL_HANDLE) vkDestroySemaphore(g.device, img.dawnDone, nullptr);
        if (img.image != VK_NULL_HANDLE) vkDestroyImage(g.device, img.image, nullptr);
        if (img.memory != VK_NULL_HANDLE) vkFreeMemory(g.device, img.memory, nullptr);
      }
      if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(g.device, slot.fence, nullptr);
    }
  }
  for (int eye = 0; eye < 2; ++eye) {
    if (g.eyeSwapchain[eye] != XR_NULL_HANDLE) xrDestroySwapchain(g.eyeSwapchain[eye]);
  }
  if (g.stage != XR_NULL_HANDLE) xrDestroySpace(g.stage);
  if (g.view != XR_NULL_HANDLE) xrDestroySpace(g.view);
  if (g.actions.gripSpaceL != XR_NULL_HANDLE) xrDestroySpace(g.actions.gripSpaceL);
  if (g.actions.gripSpaceR != XR_NULL_HANDLE) xrDestroySpace(g.actions.gripSpaceR);
  if (g.actions.aimSpaceL != XR_NULL_HANDLE) xrDestroySpace(g.actions.aimSpaceL);
  if (g.actions.aimSpaceR != XR_NULL_HANDLE) xrDestroySpace(g.actions.aimSpaceR);
  if (g.device != VK_NULL_HANDLE) {
    for (UploadSlot& u : g.uploads) {
      if (u.mapped != nullptr) vkUnmapMemory(g.device, u.memory);
      if (u.buffer != VK_NULL_HANDLE) vkDestroyBuffer(g.device, u.buffer, nullptr);
      if (u.memory != VK_NULL_HANDLE) vkFreeMemory(g.device, u.memory, nullptr);
      if (u.fence != VK_NULL_HANDLE) vkDestroyFence(g.device, u.fence, nullptr);
    }
  }
  destroy_swapchain();
  if (g.device != VK_NULL_HANDLE) {
    destroy_staging();
    if (g.fence != VK_NULL_HANDLE) vkDestroyFence(g.device, g.fence, nullptr);
    if (g.pool != VK_NULL_HANDLE) vkDestroyCommandPool(g.device, g.pool, nullptr);
  }
  if (g.local != XR_NULL_HANDLE) xrDestroySpace(g.local);
  if (g.session != XR_NULL_HANDLE) xrDestroySession(g.session);
  if (g.device != VK_NULL_HANDLE) vkDestroyDevice(g.device, nullptr);
  if (g.vkInstance != VK_NULL_HANDLE) vkDestroyInstance(g.vkInstance, nullptr);
  if (g.instance != XR_NULL_HANDLE) xrDestroyInstance(g.instance);
  g = State{};
}

#else // !__ANDROID__ || !STRIKERS_XR

extern "C" int PortXrWanted(void) { return 0; }
extern "C" int PortXrInit(void) { return 0; }
extern "C" int PortXrActive(void) { return 0; }
extern "C" int PortXrStereoWorld(void) { return 0; }
extern "C" int PortXrStereoFullStands(void) { return 0; }
extern "C" int PortXrStereoFrameBegin(void) { return 0; }
extern "C" int PortXrStereoSinglePass(void) { return 0; }
extern "C" int PortXrStereoEyeLayer(int, int, float*, float*, int*) { return 0; }
extern "C" void PortXrStereoEyeEnd(int) {}
extern "C" void PortXrStereoFrameEnd(void) {}
extern "C" int PortXrShouldQuit(void) { return 0; }
extern "C" void PortXrNoteLoopTimes(int, unsigned long long, unsigned long long, unsigned long long,
                                    unsigned long long, unsigned long long) {}
extern "C" void PortXrGoal(int, int, int, int, int, int) {}
extern "C" void PortXrCelebrationPhase(int) {}
extern "C" void PortXrSetGameplayLive(int) {}
extern "C" void PortXrSetPlayerMarker(int, float, float, float, int) {}
extern "C" void PortXrItemBlast(float, float, float, int, float, int, int, float, float, int) {}
extern "C" void PortXrChompSlam(float, float, float, float, float, int) {}
extern "C" void PortXrChompBite(float, float, float, float, float) {}
extern "C" void PortXrSetTrophies(unsigned, const unsigned short*) {}
extern "C" void PortXrCupWon(int, int) {}
extern "C" int PortXrScreenFlash(float, float, float, float) { return 0; }
extern "C" int PortXrTakeTestWipe(void) { return 0; }
extern "C" int PortXrTakeTestItem(void) { return 0; }
extern "C" void PortXrSetStsMeter(int, float, float, float, float, float) {}
extern "C" void PortXrSetSuperStrike(int) {}
extern "C" int PortXrTakeTestSts(void) { return 0; }
extern "C" void PortXrScorerFrame(int, float, float, int, int, int) {}
extern "C" void PortXrScorerPlayer(int) {}
extern "C" void PortXrScorerFaceOff(int) {}
extern "C" void PortXrScorerStats(int, int, int) {}
extern "C" void PortXrSetPaused(int) {}
extern "C" void PortXrNoteItemUse(int, int, int) {}
extern "C" void PortXrLog(const char*) {}
extern "C" float PortXrCharacterScale(void) { return 1.0f; }
extern "C" int PortXrHudOnlyNormalView(void) { return 0; }
extern "C" int PortXrNormalViewWorldless(void) { return 0; }
extern "C" int PortXrEyeViewAllowed(int, int) { return 1; }
extern "C" int PortXrStadiumLayerActive(void) { return 0; }
extern "C" void PortXrRumblePreset(int, int) {}
extern "C" void PortXrRumbleStart(int, float) {}
extern "C" void PortXrRumbleStop(int) {}
extern "C" int PortXrBeginFrame(void) { return 0; }
extern "C" void PortXrEndFrame(int) {}
extern "C" void PortXrShutdown(void) {}

#endif
