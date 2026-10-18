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
#include "xr_net.hpp" // online 1v1 (include/port/xr_net.h)
#include "xr_text.h"  // r42: the goal banner and the crowd volume panel
#include "xr_voice.h" // r44: voice chat, its switch on the pause panel
#include "xr_ra.h"    // r46: RetroAchievements, its pop-ups
#include "../../include/port/xr_net.h"

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
constexpr const char* kBuildTag = "STRIKERS_XR build=r39b";

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
  PFN_xrCreatePassthroughFB create = nullptr;
  PFN_xrPassthroughStartFB start = nullptr;
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
  XLOG("extensions: passthrough=%d refresh_rate=%d perf_settings=%d thread_settings=%d", g.hasPassthroughExt ? 1 : 0,
       g.hasRefreshRateExt ? 1 : 0, g.hasPerfSettingsExt ? 1 : 0, g.hasThreadSettingsExt ? 1 : 0);

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
  XrSystemProperties sysProps{XR_TYPE_SYSTEM_PROPERTIES};
  if (g.hasPassthroughExt) sysProps.next = &pt2;
  if (xr_ok(xrGetSystemProperties(g.instance, g.system, &sysProps), "xrGetSystemProperties")) {
    g.colorPassthrough = g.hasPassthroughExt && (pt2.capabilities & XR_PASSTHROUGH_CAPABILITY_COLOR_BIT_FB) != 0;
    XLOG("headset: %s, %s passthrough", sysProps.systemName, g.colorPassthrough ? "colour" : "no colour");
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

  VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  deviceInfo.queueCreateInfoCount = 1;
  deviceInfo.pQueueCreateInfos = &queueInfo;
  deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExts.size());
  deviceInfo.ppEnabledExtensionNames = deviceExts.data();

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
  if (mr && !pt.layerRunning) {
    pt.layerRunning = xr_ok(pt.resumeLayer(pt.layer), "xrPassthroughLayerResumeFB");
  } else if (!mr && pt.layerRunning) {
    if (xr_ok(pt.pauseLayer(pt.layer), "xrPassthroughLayerPauseFB")) {
      pt.layerRunning = false;
    }
  }
  XLOG("mode: %s", mode_name());
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

// Lance's grab rule: the left grip moves the screen (from anywhere), both grips turn and resize it.
// Returns true while the right grip is part of a two-hand grab.
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
  if (gripL && haveL && (s.grabHands != 0 || !g.prevGripL)) {
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
  quad.pose.position = s.position;
  quad.size.width = s.width;
  quad.size.height = s.width * aspect;
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
      if (gripR && !g.prevGripR) {
        g.table.rightGrab = g.tabletop && !g.stereoFailed && haveR && near_table(handR);
      }
      if (!gripR) g.table.rightGrab = false;
      // r42: the Jumbotron screen (the big screen modes, and Tabletop when 3D failed).
      const bool screenMode = !g.tabletop || g.stereoFailed;
      const bool gripL = get_float(a.squeezeL) > 0.6f;
      if (screenMode) {
        update_screen(gripL, gripR, haveL, haveR, handL, handR);
        // The left grip is busy with the screen: the right grip is not Z until it is let go.
        if (g.screens[screen_room()].grabHands != 0) g.holdRightGrip = true;
      } else {
        g.prevGripL = gripL;
        g.screens[screen_room()].grabHands = 0;
      }
      if (!gripR) g.holdRightGrip = false;
      // r46: Z from the right grip only while it is mapped there; it still grabs the table either way.
      if (g.controls[kActZ] == kSrcGripR && gripR && !g.table.rightGrab && !g.holdRightGrip) {
        status.button |= PAD_TRIGGER_Z;
      }
      g.prevGripR = gripR;
      const bool holdL = g.tabletop && get_float(a.squeezeL) > 0.6f;
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
  // r47: waiting at the match start for the online friend, B held 2 s stops the wait (the match
  // then goes ahead against the computer).
  if (xrnet::waiting_for_friend() != 0 && (status.button & PAD_BUTTON_B) != 0) {
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

// ---------------------------------------------------------------------------------------------
// 3D modes: eye images shared with Dawn, matrices, and the per-eye pass contract with glPlat.cpp
// ---------------------------------------------------------------------------------------------

// Which 3D layers the current mode draws. The flags match glPlat.cpp's XR_LAYER_*.
enum LayerKind { kLayerStadium, kLayerTable, kLayerScorer };
constexpr int kFlagSkipActors = 1;
constexpr int kFlagSkipSky = 2;
constexpr int kFlagTable = 4;
constexpr int kFlagScorer = 8;  // r42: the life-size scorer alone (glPlat.cpp XR_LAYER_SCORER)

bool stereo_mode() { return g.tabletop || !g.mrMode; }

// r42: the life-size scorer is out (and was reported lately: a game that stops reporting hides it).
bool scorer_shown() {
  return g.scorer.phase != 0 && now_ns() - g.scorer.reportNs < 300000000ull && !g.stereoFailed;
}
// The modes with eye passes: the 3D modes, and every mode while the scorer is out.
bool eye_mode() { return stereo_mode() || scorer_shown(); }

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
  return m4_game_to_room(s, yaw,
                         XrVector3f{-c * std::sin(yaw) * s, stadium_floor_y(), -c * std::cos(yaw) * s});
}

// r44: where the field is shown: where you put it, raised while the scorer celebrates (tableLift).
XrVector3f table_shown_pos() {
  return XrVector3f{g.table.position.x, g.table.position.y + g.tableLift, g.table.position.z};
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
  }
  if (g.handoffSame + g.handoffLate > 0) {  // r44
    XLOG("eye hand-off: %u in their own display frame, %u a frame late (wait %s)", g.handoffSame, g.handoffLate,
         g.handoffWait ? "on" : "off");
  }
  g.handoffSame = g.handoffLate = 0;
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
  // image and layer; the table (and scorer) go over it with coverage alpha.
  g.splitThisFrame = g.singlePass && g.stadiumSplit && g.tabletop && !g.mrMode && count > 1 &&
                     kinds[0] == kLayerStadium && slot.stadium.shared;
  slot.stadiumEye = -1;
  slot.mainDrawn = false;
  slot.mainOverlay = g.splitThisFrame;
  g.openTarget = 0;
  if (g.splitThisFrame) {
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
    }
    g.fxReady = xrfx::init(dev);
  }
}

extern "C" void PortXrSetGameplayLive(int live) { g.gameplayLive = live != 0; }

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
      const std::string name = scorer_name(characterClass);
      const uint32_t colour = team_colour(captainClass);
      const int goals = sc.statGoals, assists = sc.statAssists;
      const bool hatTrick = sc.statHatTrick;
      std::thread([name, colour, goals, assists, hatTrick]() {
        std::vector<uint8_t> pixels(static_cast<size_t>(kBannerW) * kBannerH * 4);
        xrtext::goal_banner(pixels.data(), kBannerW, kBannerH, name.c_str(), colour, goals, assists, hatTrick);
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

extern "C" void PortXrCelebrationPhase(int phase) {
  if (!g.active) {
    return;
  }
  static const char* kPhases[] = {"?", "replay started", "replay played out", "skipped", "play live"};
  XLOG("celebration: %s", (phase >= 1 && phase <= 4) ? kPhases[phase] : "?");
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
  g.xrWaitNsSinceStats += now_ns() - waitStart;
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
    if (justRecorded >= 0 && frameOk) {
      constexpr uint64_t kHandoffWaitNs = 2500000ull;
      const uint64_t waitFrom = now_ns();
      if (g.handoffWait) {
        while (g_slotState[justRecorded].load(std::memory_order_acquire) == 1 &&
               now_ns() - waitFrom < kHandoffWaitNs) {
          usleep(100);
        }
      }
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
  const XrCompositionLayerBaseHeader* layers[13];
  uint32_t layerCount = 0;

  // The celebration effects step every display frame, smooth whatever the game's frame rate, with
  // the eyes located for this display time (not the game frame's prediction).
  const uint64_t fxNow = now_ns();
  xrfx::Scene scene;
  scene.dt = g.lastFxNs != 0 ? static_cast<float>(fxNow - g.lastFxNs) / 1e9f : 0.0f;
  g.lastFxNs = fxNow;
  bool sceneViews = false;
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
    scene.floorY = mode_floor_y();  // r44g: the stadium's ground in the stadium
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
  }

  if (g.frameState.shouldRender) {
    if (g.mrMode && g.pt.available && g.pt.layerRunning) {
      ptLayer.flags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
      ptLayer.space = XR_NULL_HANDLE;
      ptLayer.layerHandle = g.pt.layer;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&ptLayer);
    }
    // The 3D picture. On display frames without a new game frame the last pair is submitted again
    // with the poses it was drawn for, and the compositor reprojects it.
    // r43 split stadium: the life-size stadium under the table, each eye as last drawn (the eyes take
    // turns), opaque.
    const bool splitMode = g.singlePass && g.stadiumSplit && g.tabletop && !g.mrMode && stereo_mode();
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
    if (g.swapchainHasImage) {
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
      } else {
        place_free_screen(quad, aspect);  // r42: wherever you put it
      }
      {
        // r43c: while the scorer is out, the screen rises to hang above its head and banner, and
        // comes back down after (eased over about half a second).
        // r49 (a player: "sometimes its in a good spot in the middle but sometimes its too high"): not
        // in Tabletop, where the scorer stands beside the field (r46e) and the screen stays put.
        const bool tableScreen = g.tabletop && !g.stereoFailed;
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
    if (scorer_shown() && g.banner.hasImage && (g.scorer.phase == 2 || g.scorer.phase == 3)) {
      const float t = g.scorer.phaseTime;
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
    }    if (!g.achOpen && (g.paused || nowNs < g.sliderShownUntilNs)) {
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
    if (g.fxReady && sceneViews && xrfx::update(scene) && xrfx::render(scene, &fxLayer)) {
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&fxLayer);
    }
    g.fxNs += now_ns() - fxStart;
    // r47: waiting for the online friend at the match start: who we wait for and how to stop, a
    // little over a metre out at about eye level, following your head lazily.
    const int waiting = xrnet::waiting_for_friend();
    if (waiting != 0 && g.headValid) {
      if (waiting != g.netWaitDrawn || !g.netWait.hasImage) {
        std::vector<uint8_t> pixels(static_cast<size_t>(xrra::kToastW) * xrra::kToastH * 4);
        char header[48];
        std::snprintf(header, sizeof(header), "ONLINE  -  ROOM %s", xrnet::room_code());
        xrtext::achievement_toast(pixels.data(), xrra::kToastW, xrra::kToastH, header,
                                  waiting == 1 ? "WAITING FOR YOUR FRIEND TO JOIN" : "YOUR FRIEND IS HERE",
                                  waiting == 1 ? "STARTS WHEN THEY JOIN AND START A GRUDGE MATCH. HOLD B TO PLAY THE CPU"
                                               : "STARTS WHEN THEY START A GRUDGE MATCH TOO. HOLD B TO PLAY THE CPU",
                                  0);
        panel_upload(g.netWait, pixels.data(), xrra::kToastW, xrra::kToastH);
        g.netWaitDrawn = waiting;
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
extern "C" int PortXrScreenFlash(float, float, float, float) { return 0; }
extern "C" int PortXrTakeTestWipe(void) { return 0; }
extern "C" int PortXrTakeTestItem(void) { return 0; }
extern "C" void PortXrSetStsMeter(int, float, float, float, float, float) {}
extern "C" void PortXrSetSuperStrike(int) {}
extern "C" int PortXrTakeTestSts(void) { return 0; }
extern "C" void PortXrScorerFrame(int, float, float, int, int, int) {}
extern "C" void PortXrScorerStats(int, int, int) {}
extern "C" void PortXrSetPaused(int) {}
extern "C" void PortXrNoteItemUse(int, int, int) {}
extern "C" void PortXrLog(const char*) {}
extern "C" float PortXrCharacterScale(void) { return 1.0f; }
extern "C" int PortXrHudOnlyNormalView(void) { return 0; }
extern "C" int PortXrEyeViewAllowed(int, int) { return 1; }
extern "C" int PortXrStadiumLayerActive(void) { return 0; }
extern "C" void PortXrRumblePreset(int, int) {}
extern "C" void PortXrRumbleStart(int, float) {}
extern "C" void PortXrRumbleStop(int) {}
extern "C" int PortXrBeginFrame(void) { return 0; }
extern "C" void PortXrEndFrame(int) {}
extern "C" void PortXrShutdown(void) {}

#endif
