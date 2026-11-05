// See include/port/xr_quest.h.
//
// Built as its own object library (strikers_xr in CMakeLists.txt) rather than with the rest of
// src/platform: it is C++20 and must not get the port prelude, which the Vulkan and OpenXR headers
// were never written to sit under.
//
// Two picture paths:
//  - The flat game frame (Jumbotron, and the scoreboard over the Tabletop): Aurora reads the
//    presented frame back to the CPU every game frame (aurora_xr_set_frame_callback), and this file
//    uploads it into a quad-layer swapchain on its own Vulkan device. r131: Jumbotron · Living Room
//    (and the cup suite) instead GPU-direct (ScreenSlot): Dawn copies the frame into a shared image,
//    a SyncFD fence gates its blit into that swapchain.
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
#include <poll.h>  // r131: Dawn's sync fd, polled (finish_screen_slots)
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
#include "xr_big.h"   // r137: big character pictures (the magazine's)
#include "xr_weather.h"  // r137: weather + night games (looks only)
#include "xr_cards.h"    // r137: STRIKERS trading cards (franchise packs, the binder)
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
#include "../../include/port/xr_body.h"   // r144: the life-size scorer's real reach and height
#include <functional>  // r70c
#include "../../include/port/franchise.h"  // r70: franchise mode's hub
#include "../../include/port/lineup.h"     // r81: the co-captains
#include "../../include/port/menu_music.h" // r147: the lofi menu music (its setting row, the theme, NOW PLAYING)

#include <algorithm>
#include <array>  // r112
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>  // r137
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
void load_room_options();               // r104: the trophy shelf / badge board / achievement pop-up switches

namespace {

constexpr const char* kTag = "strikers_xr";
// Bumped every headset round; the first log line of a session says which build is running.
constexpr const char* kBuildTag = "STRIKERS_XR build=r151";

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

// r131 (Lance's r130 play: Jumbotron · Living Room 65.8-69 fps in matches, "the Hz just felt lower than 72"; its loop:
// aurora_end_frame's FIFO drain 3-4 ms + PortXrEndFrame's synchronize 1-2.8 + upload 0.9 on the game thread): the
// GPU-direct big screen. A game frame's picture is copied on Dawn's queue into one of these shared images
// (aurora::gfx::set_xr_screen_copy, recorded just before the frame's submit) and blitted into the screen swapchain on the
// XR queue gated on Dawn's fence: no CPU readback, no upload, no synchronize, and the frame can be left open (pipelined)
// like the 3D views'. Four: the open frame's, the one being submitted, one whose copy is still on the GPU (its blit
// waits for that, finish_screen_slots), the one last blitted (its blit's fence) - none of them waited for on the CPU.
constexpr uint32_t kScreenSlots = 4;
struct ScreenSlot {
  EyeImage img;            // the shared image and its hand-off (as an eye image's)
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  bool pending = false;    // its last submit (the blit) not yet seen done
};
// Per screen slot (outside State, which is reset by assignment): 0 free, 1 armed (Dawn's access open, waiting for its
// frame's submit), 2 released (the render worker ended the access after the submit); the Aurora frame it holds; whether
// that frame's copy was recorded (0: no picture in it)
std::atomic<uint32_t> g_screenState[kScreenSlots];
std::atomic<uint32_t> g_screenFrame[kScreenSlots];
std::atomic<uint32_t> g_screenCopied[kScreenSlots];

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

// r131: what STRIKERS ILLUSTRATED knows beyond the league's view: your last played franchise match's own numbers
// (a hat trick) and your player's evolution / new costume, each with the season and week it was in.
struct MagNews {
  PortFranchiseLive live = {};
  int liveSeason = -1, liveWeek = -1;
  std::string evoFrom, evoTo, evoToKey;  // names (as shown) and the new form's portrait key
  bool evoShiny = false, evoCostume = false;
  int evoSeason = -1, evoWeek = -1;
};
// r131 (Lance: "i do want the strikers illustrated to be produced at random, it shouldnt always be the hottest team or
// player to be on the cover ... make sure no cover is duplicated"): an issue's cover as drawn at random (mag_pick_issue):
// its season / week, template, headline (index into its pool), variant (colours, sash, mirrored star), the cover star
// (anyone in the league) and a rival (FACEOFF), and the issue's own seed (its cover lines). Kept per career in
// STRIKERS_HOME/franchise/magazine_<slot>.txt: no star + template + headline twice, never the same star twice running.
struct MagPick {
  bool valid = false;
  int season = -1, week = -1, tmpl = 0, head = 0, variant = 0, club = -1, rivalClub = -1;
  std::string key, name, rival, rivalName;
  uint32_t seed = 1;
};

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
  PFN_xrGetDisplayRefreshRateFB getRefreshRate = nullptr;  // r124
  float wantRefresh = 72.0f;  // r124: asked for again once the session runs (its first ask, before, was ignored)
  uint64_t refreshCheckNs = 0;

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
    // r144: its posed body's reach round its feet, its top and its rise toward its front (port/xr_body.h, game units),
    // the most of each over this show; how far its feet are kept from your head for it (scorer_place)
    bool shapeOk = false;
    float reach[8] = {};
    float top = 0.0f;
    float rise[kPortXrBodySlopeCount] = {};
    float sideReach[2][kPortXrBodySlopeCount] = {};
    PortXrBodyShape cur{};            // its latest pose's own (the banner follows it)
    float shapeView = 0.0f;           // r144b: the direction rise / side are along (PortXrBodySetView)
    float turn = 0.0f, turnTarget = 0.0f;  // r144b: turned this far from facing you (a long body side-on)
    float standTop = 0.0f;            // r144b: its top as it ran in (its size, not a jump's: the clearance)
    bool standTopOk = false;
    uint64_t shapeGen = 0, planNs = 0, spaceNs = 0;
    float keepOff = 2.5f;
    bool spaceLogged = false;
    // r144: its banner: over it (1) or beside it (2) for this show (0 not decided yet), its bottom edge as it follows
    // the body (LOCAL y), and where it went this frame (screen_lift_over_scorer)
    int bannerMode = 0;
    float bannerBottom = 0.0f;
    uint64_t bannerNs = 0;
    bool bannerKnown = false;
    XrVector3f bannerAt{};
    float bannerTop = 0.0f, bannerWidth = 0.0f;
  } scorer;
  Panel banner;                       // r42: "GOAL SCORED BY:" over the scorer
  bool bannerBusy = false;            // its picture is being drawn (g_bannerPixels, g_bannerReady)
  Panel slider;                       // r42: the crowd volume panel while paused
  bool paused = false;
  int crowdLevel = 6;                 // 0..10; 6 = the game's own loudness
  uint64_t sliderShownUntilNs = 0;
  uint64_t crowdRepeatNs = 0;         // right stick held: the next step
  int settingsStickDirY = 0;          // r44g: the right stick's last up/down push while paused
  int settingsRow = 0;                // r44g: the settings row picked (0 crowd, 1 voice, 2 height; r104: 3 shelf +
                                      // badge board, 4 achievement pop-ups)
  // r104 (Lance: the trophy shelf and the RetroAchievements board "optional, or togglable"; "bundled up"):
  // one switch for the trophy shelf (your room and the cup suite, picking trophies up too) and the
  // RetroAchievements badge board over it. xr_room_options.txt
  bool showShelf = true;
  bool showHome = true;  // r133 (Lance: "make the home ground an optional thing"): the board, banners, corner + shelf
  int raPopups = 2;                   // RetroAchievements pop-ups: 2 all, 1 unlocks only, 0 none
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
  // r130: the sidekick pick, one slot at a time (SIDEKICK 1-3; a co-captain has slot 1): the slot being picked
  // (-1 not started), the first one, and the menu picks so far
  int gpSkSlot = -1, gpSkFirst = 0;
  int gpSkPick[3] = {-1, -1, -1};
  unsigned gpCoTaken = 0;             // the Strikers captains it can't be (bit = eTeamID)
  unsigned gpSkCapTaken = 0;          // r130: the Strikers captains its sidekick slots can't have (as last drawn)
  // r112: the menu by game: the stick on the game tabs; LT / RT through a character's forms (as last sync)
  bool gpOnTabs = false, gpPrevLT = false, gpPrevRT = false;
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
  bool frOnTabs = false, frPrevLT = false, frPrevRT = false;  // r112: the new-career picks (as gpOnTabs ...)
  // r113 (Lance: "an actual newspaper ... that ppl can literally flip through after each game"): THE DAILY
  // STRIKE, in front of you after each franchise match (and the hub's PAPER page, A). r131: STRIKERS ILLUSTRATED,
  // a magazine (Lance: "make the magazine grabbable, and with that, the pages flippable"): open (due after a match),
  // placed in front of you, its pages drawn (generation), the triggers as last sync.
  bool paperOpen = false, paperDue = false, paperPlaced = false;
  int paperGen = 0;
  bool paperPrevLT = false, paperPrevRT = false;
  // r131: its leaves turned to the left (0 = closed on the cover, 3 = closed on the back cover); the leaf turning (-1
  // none; magSide 0 flat on the right .. 1 flat on the left), animated magFrom -> magTo since magTurnNs, or held by
  // a hand's trigger (magDrag the hand, from magDragX / magDragSide); where it is (magPos its middle, magRelYaw its
  // turn away from facing you, magScale its size); the grips holding it (magHand; magSpent: a grip that took it,
  // until it opens), the two-hand grab's start; the triggers it took; the free hand's sweeps (a swipe turns a page).
  int magLeaves = 0, magTurn = -1, magDrag = -1;
  float magSide = 0.0f, magFrom = 0.0f, magTo = 0.0f, magDragX = 0.0f, magDragSide = 0.0f;
  uint64_t magTurnNs = 0;
  uint64_t magOpenNs = 0;  // r131: when it opened (buttons wait 0.9 s: the A mashed through the match's end screens)
  // r131 (Lance: "make the magazine more flimsy"): its pages as paper in the effects layer (mag_paper_frame): the
  // issue whose pictures are up (-1 none), the posed vertices, shown this frame; each side's free edge bent back
  // (radians) and how fast, the magazine's own motion (velocity, acceleration, its turn), a flutter
  int magModelGen = -1;
  std::vector<float> magVerts;
  bool magMeshShown = false;
  float magFlop[2] = {0.0f, 0.0f}, magFlopV[2] = {0.0f, 0.0f}, magFlutter = 0.0f, magLastAng = 0.0f;
  XrVector3f magVel{0.0f, 0.0f, 0.0f}, magAcc{0.0f, 0.0f, 0.0f}, magLastPos{0.0f, 0.0f, 0.0f};
  uint64_t magPhysNs = 0;
  XrVector3f magPos{0.0f, 0.0f, 0.0f};
  float magRelYaw = 0.0f, magScale = 1.0f;
  bool magHand[2] = {false, false}, magSpent[2] = {false, false}, magPrevGrip[2] = {false, false};
  bool magPrevTrig[2] = {false, false}, magTrigBusy[2] = {false, false};
  int magGrabHands = 0;
  XrVector3f magGrabOff{0.0f, 0.0f, 0.0f}, magTwoPos{0.0f, 0.0f, 0.0f}, magTwoMid{0.0f, 0.0f, 0.0f};
  float magTwoAngle = 0.0f, magTwoYaw = 0.0f, magTwoDist = 1.0f, magTwoScale = 1.0f;
  struct SwipeAt {
    uint64_t ns;
    float x, y, z;  // the free hand in the magazine's own frame (m)
  } magSweep[24] = {};
  int magSweepAt = 0, magSwipeDir = 0;
  uint64_t magSwipeNs = 0;
  MagNews magNews;  // r131: the last match's numbers, the week's evolution (copied into the hub's snapshot)
  std::vector<MagPick> magHistory;  // r131: this career's covers so far (its magazine file), read for magHistSlot
  int magHistSlot = -1;
  bool frWasMatch = false;  // r113: a franchise match was on last sync (its photos start fresh)
  Panel paperPanel[6];      // r113: the paper's pages (g_paperP draws them); r131: the magazine's 6
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
  // r130: stages 2-4 your three sidekicks one at a time (menu picks), and whether this is the EVO page's X (your
  // career's sidekick members, not a new career)
  int frPickSk[3] = {-1, -1, -1};
  bool frEditMembers = false;
  int frMemNow[3] = {-1, -1, -1};  // r130: the members when the editor opened (kept ones may be evolved forms)
  // r76, co-op: the loaded career's outside changes seen, and the co-op card's state as last drawn
  unsigned frChangeSerial = 0;
  long long frCoopKey = -1;
  // r77: the hub's mode 5, trades: stage 0 what you give (0 captain, 1 backup, 2 sidekicks), 1 their
  // offers; the last answer; Y inside the hub (its own edge: frPrevY is the opener's)
  int frTradeStage = 0, frTradeWhat = 0;
  std::string frTradeMsg;
  bool frHubPrevY = false;
  // (the menu cursor kept per stage, so B goes back to where you were)
  int frPickCursor[5] = {0, 0, 0, 0, 0};  // r130: 5 stages
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
  Panel nowPlaying;                   // r147: the menu music's "NOW PLAYING" card (port/menu_music.h)
  uint64_t npShownNs = 0;             // when it came up (0: not on show)
  int npGen = 0, npShownGen = -1;     // the track it is for / the picture on show
  bool npPlaced = false;
  XrVector3f npPos{0.0f, 0.0f, 0.0f};
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
  // r106 (Lance: "lets try and actually reach 72FPS"): offline, a game frame every display frame while
  // this view keeps up (the sim is its own fixed 50 Hz step and every frame is drawn blended between
  // two steps, so more frames = smoother, not faster); a view that can't is held at the 60 cadence
  // until the view changes. frameScale = 60 / game frames a second, for what counts frames (1 at 60).
  bool fullRate = false;
  int fullRateBlockedKey = -1;        // the view (modeKey) that couldn't keep up
  uint64_t fullRateSlowSinceNs = 0;
  // r129: a view held at 60 tries full rate again after fullRateRetryNs (10 s, doubling to 60 s; one slow moment -
  // the scorer's show, the stadium's pipelines compiling - no longer holds it at 60 for the session)
  uint64_t fullRateBlockedNs = 0, fullRateRetryNs = 0;
  // r129, the pipelined frame (xr_quest.h): on (xr_tune "pipeline <0|1>"); this game frame's Aurora frame is to be
  // left open (decided as its eyes begin); one is open from the last loop; time spent at the boundary and in the
  // simulation before the send (loopStartNs .. the send), and pipelined game frames, since the stats line
  bool pipelineOn = true, pipelineThisFrame = false, framePending = false;
  // r130: missed display frames since the last load check (xrfx::set_load_scale), the last predicted display time;
  // a goal's show (from the goal to a few seconds after play is live again): slowness then doesn't hold the view at
  // 60; full rate's run without a block since (the retry's backoff starts over after a minute of it)
  XrTime lastPredictedDisplayTime = 0;
  uint64_t scaleCheckNs = 0, celebStartNs = 0, celebEndNs = 0, fullRateOkSinceNs = 0;
  uint32_t missedSinceScale = 0;
  uint64_t loopStartNs = 0, boundaryNsSinceStats = 0, simNsSinceStats = 0;
  uint32_t pipelinedSinceStats = 0, sendsSinceStats = 0;
  // r131, the GPU-direct big screen (ScreenSlot): its images (one size, the game picture's), on (xr_tune "screendirect
  // <0|1>"), unavailable (set once: the readback path stays), this game frame's picture goes that way, the Aurora frame
  // armed last; this game frame sent (PortXrFrameBoundary ran: only such a 2D frame is left open); counts since the stats
  // line: game frames copied, pictures shown (and how many frames behind the newest drawn, summed), older ones dropped,
  // frames with no free image, released with no picture, CPU fence waits, display frames whose newest picture was still
  // on the GPU; 2D game frames pipelined
  ScreenSlot screenSlots[kScreenSlots];
  uint32_t screenW = 0, screenH = 0;
  bool screenDirectOn = true, screenDirectFailed = false, screenDirectThisFrame = false;
  bool screenArmedValid = false, sentThisFrame = false;
  uint32_t screenArmedFrame = 0;
  uint32_t screenCopiesSinceStats = 0, screenShownSinceStats = 0, screenDroppedSinceStats = 0;
  uint32_t screenNoSlotSinceStats = 0, screenBlankSinceStats = 0, screenCpuWaitsSinceStats = 0, pipelined2DSinceStats = 0;
  uint32_t screenNotDoneSinceStats = 0;
  uint64_t screenBehindSinceStats = 0;
  float frameScale = 1.0f;
  float raAccum = 0.0f;               // RetroAchievements checks 60 times a second (rcheevos counts frames)
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
  // r120 (Lance: "scyther is RIGHT in my face, back him the fuck up, and make it so theyre anchored and we can move
  // them so theyre not in the way"): the home figure's own spot on your floor (xr_homespot.txt), off the shelf;
  // one grip drags it, both turn it and size it. `shown`, `feet`, `reach`, `height`: last frame's (for the grab).
  struct HomeSpot {
    bool placed = false;
    float x = 0.0f, z = 0.0f, yaw = 0.0f, scale = 1.0f;
    bool handL = false, handR = false;
    int grabHands = 0;
    XrVector3f grabOffset{0.0f, 0.0f, 0.0f};
    float twoAngle = 0.0f, twoYaw = 0.0f, twoDist = 1.0f, twoScale = 1.0f;
    XrVector3f twoPos{0.0f, 0.0f, 0.0f}, twoMid{0.0f, 0.0f, 0.0f};
    bool prevGripL = false, prevGripR = false;
    bool shown = false;
    float floorY = 0.0f, reach = 0.5f, height = 1.8f;
  } homeSpot;
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
  bool evoRoomOn = false;      // r119: an evolution's backdrop is up (VR)
  bool homeEnvOn = false;      // r124: the home screen's suite is up (VR, out of a match)
  bool worldQuietLogged = false;  // r128: the last "world:" log line said there's no stadium (g_worldDrawnNs)
  XrVector3f homeEnvOrigin{0.0f, 0.0f, 0.0f};
  float homeEnvYaw = 0.0f;
  std::string evoRoomTheme;    // r119: which (evo_<theme>), "" none on this headset
  std::string evoRoomKey;      // r132: whose evolution it was picked for
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
// r134: the home ground shows unless switched off here (pause settings) or NONE on the Home ground panel
bool home_on() { return g.showHome && !xrhome::home_none(); }

// r128: the last time the game drew its stadium (World::Render, the game thread: PortXrWorldRendered). r126 took any
// eye / split-stadium picture going out instead, but the Stadium views send one every frame at the menus too (black),
// so the home screen's suite never came up there (Lance's r127 test: "it just goes to a black screen").
std::atomic<uint64_t> g_worldDrawnNs{0};

// r42: the goal banner's picture, drawn on a thread of its own (xr_text.cpp takes tens of ms).
std::vector<uint8_t> g_bannerPixels;
std::atomic<bool> g_bannerReady{false};
constexpr uint32_t kBannerW = 1024, kBannerH = 560;  // r43d: 420 + the stats line
constexpr uint32_t kSliderW = 640, kSliderH = 800;  // r147: eight rows (+ MENU MUSIC); r137: seven rows (+ WEATHER); r133: six rows (+ HOME GROUND)  // r44g: the settings panel's rows (r104: five, was 380 for three)
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
AsyncPanel g_nowPlayingP;              // r147: the menu music's "NOW PLAYING" card (xrtext::now_playing_toast)
constexpr uint32_t kNowPlayingW = 1024, kNowPlayingH = 220;
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
  unsigned serial = 0;  // r131: + 1 each snapshot (fr_refresh): a picture drawn from an older one says so
  int mode = 0, cursor = 0, page = 0, slot = 0;
  bool confirmDelete = false, canPlay = false;
  bool onTabs = false;  // r112: the new-career picks' stick is on the game tabs
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
  int pickSk[3] = {-1, -1, -1};  // r130
  int pickBk = -1;               // r130: the backup captain picked (a Strikers one is no sidekick then)
  std::vector<unsigned char> pickTaken;  // r131: per menu entry of the stage's menu: a character taken already
  // r76, co-op: the careers screen's co-op card (shown with a friend in the room or one saved), its
  // state (PortFranchiseCoopState) and saved career; the loaded career is co-op (and run by the friend)
  bool coopShown = false, coopActive = false, coopGuest = false;
  int coopState = 0;
  PortFranchiseSlot coopSlot = {};
  std::string coopName;
  std::vector<PortFranchiseEvoRow> evoWatch;  // r77: the Evo Watch page
  std::string evoReadyName;                   // r95: one of yours ready to evolve (postponed too), "" none
  bool evoReadyCostume = false;               // r111: ... a Marvel hero's next costume
  MagNews mag;                                // r131: STRIKERS ILLUSTRATED's own news (g.magNews)
  MagPick pick;                               // r131: this issue's random cover (mag_pick_issue)
  std::string cardPull;                       // r137: a big trading-card pull for the cover ("SHINY CHARIZARD"), "" none
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
  int nextWeather = 0;  // r137: your next game's weather (xr_weather.h PORT_WX_* bits; the calendar)
};
// r100: a league code's letters (no 0 / O / 1 / I: xr_league.cpp's)
constexpr const char* kFrCodeChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
std::mutex g_frMutex;
FrSnapshot g_frSnap;
// (r143: r113's newspaper photo - the game's own frame after your goal - is gone: Lance: "i should never see gameplay
// as a cover"; STRIKERS ILLUSTRATED draws its own covers)
AsyncPanel g_paperP[6];  // r113: the newspaper's pages; r131: STRIKERS ILLUSTRATED's 6
// r137: its big pictures (xr_big): the issue (paperGen) a page of which was drawn without one, still being drawn (-1
// none), xrbig::generation() when that page started, and when the issue was last drawn again for them (render thread)
std::atomic<int> g_magBigWait{-1};
std::atomic<unsigned> g_magBigAt{0};
uint64_t g_magBigRedrawNs = 0;
int g_magBigRedrawGen = -1;  // the paperGen of that drawing again (its paper keeps the pages it has till it's done)
std::atomic<bool> g_hubBigWait{false};  // r137: the hub's MAG page drew this week's cover without its big picture
std::atomic<unsigned> g_hubBigAt{0};
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
void fr_pick_taken_fill(std::vector<unsigned char>& out);  // r131 (after the menu's entries)
// The hub's state as the game has it now, for the next picture (game thread).
void mag_pick_issue(FrSnapshot& s);  // r131 (below)
void mag_prefetch(const FrSnapshot& s);  // r137 (below)
void fr_refresh() {
  FrSnapshot s;
  s.mode = g.frMode;
  s.cursor = g.frCursor;
  s.onTabs = g.frOnTabs;  // r112
  s.page = g.frPage;
  s.slot = g.frSlot;
  s.confirmDelete = g.frConfirmDelete;
  s.canPlay = PortFranchiseCanPlay();
  if (g.frMode == 0) PortFranchiseSlots(s.slots);  // (reads the three save files)
  PortFranchiseGetView(&s.view);
  PortFranchiseTodayGames(&s.today);
  s.nextWeather = PortWeatherMode() == PORT_WX_MODE_OFF ? 0 : PortFranchiseNextWeather();  // r137
  s.evo = g.frEvo;
  s.evoStage = g.frEvoStage;
  s.evoChoice = g.frEvoChoice;
  s.evoNewShiny = g.frEvoNewShiny;
  s.pickStage = g.frPickStage;
  for (int k = 0; k < 3; ++k) s.pickSk[k] = g.frPickSk[k];  // r130
  s.pickClub = g.frPickClub;
  s.pickCap = g.frPickCap;
  s.pickBk = g.frPickBk;  // r130
  if (g.frMode == 4) fr_pick_taken_fill(s.pickTaken);  // r131: one character, one club
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
    if (PortFranchiseReadyEvolution(&ready)) {
      s.evoReadyName = ready.fromName;
      s.evoReadyCostume = std::strncmp(ready.fromKey, "mua_", 4) == 0;  // r111
    }
  }
  if (g.frMode == 2) {  // r77 (r113: the paper's star watch too, every page)
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
  static std::atomic<unsigned> serial{0};  // r131
  s.serial = ++serial;
  s.mag = g.magNews;  // r131
  s.cardPull = xrcards::magazine_pull();  // r137
  if (g.frMode == 2 && s.view.active) {
    mag_pick_issue(s);  // r131: this week's random cover (kept in the career's file)
    mag_prefetch(s);    // r137: its big pictures drawn now (xr_big), ready when it's opened
  }
  std::lock_guard<std::mutex> lock(g_frMutex);
  g_frSnap = std::move(s);
  ++g.frGen;
  // r113: the paper's pages too. r131: not while the magazine is open (its pages don't change while you read it;
  // every stick flip came here and drew all of them again)
  if (!g.paperOpen) ++g.paperGen;
}
// r113: THE DAILY STRIKE's pages (front page, around the league, the table, star watch, rivals), opened in front
// of you, turned with the stick / triggers / a swipe of your right hand.
// r131: STRIKERS ILLUSTRATED (Lance: "make the magazine grabbable, and with that, the pages flippable ... right now
// the cover shows up for .34 seconds then disappears. i want the cover to be the first page by default"). Why the
// cover went (his r129 / r130 logs: "today's paper" and at once "the paper swiped on", then on / back / on): r113's
// swipe took any 16 cm sweep of the right hand within 40 cm above or below the paper and up to 35 cm in front of it,
// where a resting hand is, so the front page turned by itself (and "back" when the hand came back). Now 6 pages on 3
// leaves (each leaf a page on either side: cover | scorecard, the table | star watch, rivals | back cover); the closed
// magazine shows its cover alone; only the stick / triggers / A, a trigger pulled ON a page (dragging that page over
// the spine) or a sweep of your free hand across it while the other hand holds it turn a page.
constexpr int kMagPages = 6, kMagLeaves = 3;
constexpr uint32_t kMagW = 1080, kMagH = 1440;  // a page (3 : 4)
constexpr float kMagPageW = 0.31f;              // a page's width at size 1 (m)
XrVector3f q_rot(const XrQuaternionf& q, const XrVector3f& v);  // below
XrQuaternionf q_conj(const XrQuaternionf& q);
XrQuaternionf q_mul(const XrQuaternionf& a, const XrQuaternionf& b);
XrQuaternionf yaw_quat(float yaw);
float get_float(XrAction action);
uint64_t now_ns();
std::string mag_cover_name(const FrSnapshot& s);  // (below)
// Where the spine is from the magazine's middle, in pages: closed on its cover the cover's middle is there (the spine
// half a page left), open the spine is, closed on its back the back cover's middle.
float mag_shift(int leaves) { return leaves <= 0 ? 0.5f : (leaves >= kMagLeaves ? -0.5f : 0.0f); }
struct MagFrame {
  XrQuaternionf q{0.0f, 0.0f, 0.0f, 1.0f};  // its turn (its pages face +z)
  XrVector3f fold{0.0f, 0.0f, 0.0f};        // the spine's middle
  float pageW = kMagPageW, pageH = kMagPageW * 4.0f / 3.0f;
  float shift = 0.5f;                       // the spine from the middle (pages, + = left)
  float x0 = 0.0f, x1 = 0.0f;               // the pages showing, from the spine (m)
};
// As it is now: facing you (turned magRelYaw more), leaning back to your eyes as a magazine held to read.
MagFrame mag_frame() {
  MagFrame f;
  f.pageW = kMagPageW * g.magScale;
  f.pageH = f.pageW * static_cast<float>(kMagH) / static_cast<float>(kMagW);
  const XrVector3f& p = g.magPos;
  const XrVector3f& h = g.headPose.position;
  const float dx = h.x - p.x, dz = h.z - p.z;
  const float yaw = std::atan2(dx, dz) + g.magRelYaw;
  const float up = std::atan2(h.y - p.y, std::max(0.05f, std::sqrt(dx * dx + dz * dz)));
  const float tilt = std::max(-1.2f, std::min(1.0f, -(up + 0.13f)));  // its top away from you
  f.q = q_mul(yaw_quat(yaw), XrQuaternionf{std::sin(0.5f * tilt), 0.0f, 0.0f, std::cos(0.5f * tilt)});
  const bool turning = g.magTurn >= 0;
  f.shift = turning ? mag_shift(g.magTurn) + (mag_shift(g.magTurn + 1) - mag_shift(g.magTurn)) * g.magSide
                    : mag_shift(g.magLeaves);
  const XrVector3f off = q_rot(f.q, XrVector3f{-f.shift * f.pageW, 0.0f, 0.0f});
  f.fold = {p.x + off.x, p.y + off.y, p.z + off.z};
  f.x0 = turning || g.magLeaves > 0 ? -f.pageW : 0.0f;
  f.x1 = turning || g.magLeaves < kMagLeaves ? f.pageW : 0.0f;
  return f;
}
// A point in the magazine's own frame, from its middle (m).
XrVector3f mag_local(const MagFrame& f, const XrVector3f& w) {
  return q_rot(q_conj(f.q), XrVector3f{w.x - g.magPos.x, w.y - g.magPos.y, w.z - g.magPos.z});
}
// Where hand h's pointer is on the magazine's plane, from its middle: its tip when that's at the page (12 cm), else
// where its ray meets the plane (up to 2.5 m). False: pointing away.
bool mag_point(const MagFrame& f, int h, float* x, float* y) {
  if (!g.aimValid[h]) return false;
  const XrVector3f tip = mag_local(f, g.aim[h].position);
  if (std::fabs(tip.z) < 0.12f) {
    *x = tip.x;
    *y = tip.y;
    return true;
  }
  const XrVector3f d = q_rot(q_conj(f.q), q_rot(g.aim[h].orientation, XrVector3f{0.0f, 0.0f, -1.0f}));
  if (std::fabs(d.z) < 1e-3f) return false;
  const float t = -tip.z / d.z;
  if (t < 0.0f || t > 2.5f) return false;
  *x = tip.x + d.x * t;
  *y = tip.y + d.y * t;
  return true;
}
// On a page showing (x, y from the middle), `margin` round it.
bool mag_on(const MagFrame& f, float x, float y, float margin) {
  const float xf = x + f.shift * f.pageW;  // from the spine
  return xf >= f.x0 - margin && xf <= f.x1 + margin && std::fabs(y) <= 0.5f * f.pageH + margin;
}
// The leaf turning comes down where it's going (or where it is, past half way).
void mag_settle() {
  if (g.magTurn < 0) return;
  g.magLeaves = g.magTo >= 0.5f ? std::min(kMagLeaves, g.magTurn + 1) : g.magTurn;
  g.magTurn = -1;
  g.magSide = 0.0f;
}
void mag_animate(float to) {
  g.magFrom = g.magSide;
  g.magTo = to;
  g.magTurnNs = now_ns();
}
// A page turned by the stick / triggers / A / a swipe (+1 on, -1 back): over the spine in 0.45 s, the face to you.
// False: no page that way (or a hand holds one).
bool mag_flip(int dir) {
  if (g.magDrag >= 0) return false;
  mag_settle();
  if (dir > 0 && g.magLeaves < kMagLeaves) {
    g.magTurn = g.magLeaves;
    g.magSide = 0.0f;
    mag_animate(1.0f);
    return true;
  }
  if (dir < 0 && g.magLeaves > 0) {
    --g.magLeaves;
    g.magTurn = g.magLeaves;
    g.magSide = 1.0f;
    mag_animate(0.0f);
    return true;
  }
  return false;
}
void mag_tick(uint64_t now) {
  if (g.magTurn < 0 || g.magDrag >= 0) return;
  const float dur = std::max(0.08f, 0.45f * std::fabs(g.magTo - g.magFrom));
  const float t = static_cast<float>(static_cast<double>(now - g.magTurnNs) * 1e-9) / dur;
  if (t >= 1.0f) {
    g.magSide = g.magTo;
    mag_settle();
    return;
  }
  const float e = t * t * (3.0f - 2.0f * t);
  g.magSide = g.magFrom + (g.magTo - g.magFrom) * e;
}
// Opened closed, its cover to you (placed in front of you when it's next drawn); its size stays what you made it.
void paper_open() {
  g.paperOpen = true;
  g.paperPlaced = false;
  g.magLeaves = 0;
  g.magTurn = -1;
  g.magDrag = -1;
  g.magSide = 0.0f;
  g.magRelYaw = 0.0f;
  g.magGrabHands = 0;
  g.magHand[0] = g.magHand[1] = false;
  g.magSweepAt = 0;
  g.magSwipeDir = 0;
  for (auto& s : g.magSweep) s.ns = 0;
  g.magOpenNs = now_ns();
  ++g.paperGen;
  XLOG("franchise: this week's STRIKERS ILLUSTRATED (the %s cover)", mag_cover_name(g_frSnap).c_str());
}
// r131: the magazine in your hands (Lance's rule, every project: one grip moves it, both grips turn and size it).
// A grip closing on it (the hand at it, or its pointer on it) takes it: one hand carries it (it keeps facing you),
// both turn it (as the hands turn round each other) and size it (as they part: 0.5 - 2.2 of 31 cm a page); let go, it
// floats where it is. A trigger pulled on a page (pointing at it or touching it) holds that page: it follows the
// hand over the spine and goes over past a third of the way (or back). A sweep of the free hand across the pages
// while the other hand holds it turns one (right to left on, left to right back; the hand coming back is no swipe).
// The grips it took (g.magHand / magSpent) and triggers (magTrigBusy) are the magazine's until let go.
void update_magazine(const bool grip[2], const bool have[2], const XrVector3f hand[2], uint64_t now) {
  mag_tick(now);
  const bool live = g.paperOpen && g.paperPlaced && g.frOpen && g.frMode != 3 && g.headValid;
  const MagFrame f = mag_frame();
  const float trig[2] = {get_float(g.actions.triggerL), get_float(g.actions.triggerR)};
  for (int h = 0; h < 2; ++h) {
    if (live && grip[h] && !g.magPrevGrip[h] && have[h]) {
      const XrVector3f l = mag_local(f, hand[h]);
      float px = 0.0f, py = 0.0f;
      const bool at = std::fabs(l.z) < 0.15f && mag_on(f, l.x, l.y, 0.08f);
      const bool aimed = !at && mag_point(f, h, &px, &py) && mag_on(f, px, py, 0.03f);
      if (at || aimed) {
        g.magHand[h] = true;
        g.magSpent[h] = true;
        XLOG("franchise: the magazine taken by the %s grip%s", h == 0 ? "left" : "right", aimed ? " (pointing at it)" : "");
      }
    }
    if (!grip[h] || !have[h] || !live) g.magHand[h] = false;
    if (!grip[h]) g.magSpent[h] = false;
    g.magPrevGrip[h] = grip[h];
  }
  const int hands = (g.magHand[0] ? 1 : 0) | (g.magHand[1] ? 2 : 0);
  const XrVector3f mid{0.5f * (hand[0].x + hand[1].x), 0.5f * (hand[0].y + hand[1].y), 0.5f * (hand[0].z + hand[1].z)};
  const float apart = std::sqrt((hand[1].x - hand[0].x) * (hand[1].x - hand[0].x) + (hand[1].y - hand[0].y) * (hand[1].y - hand[0].y) +
                                (hand[1].z - hand[0].z) * (hand[1].z - hand[0].z));
  if (hands != g.magGrabHands) {
    if (hands == 3) {
      g.magTwoAngle = std::atan2(hand[1].x - hand[0].x, hand[1].z - hand[0].z);
      g.magTwoYaw = g.magRelYaw;
      g.magTwoPos = g.magPos;
      g.magTwoMid = mid;
      g.magTwoDist = std::max(0.05f, apart);
      g.magTwoScale = g.magScale;
    } else if (hands != 0) {
      const XrVector3f& p = hand[hands == 1 ? 0 : 1];
      g.magGrabOff = {g.magPos.x - p.x, g.magPos.y - p.y, g.magPos.z - p.z};
    } else {
      XLOG("franchise: the magazine left at (%.2f, %.2f, %.2f), size %.2f, turned %.0f deg", g.magPos.x, g.magPos.y,
           g.magPos.z, g.magScale, g.magRelYaw * 57.29578f);
    }
    g.magGrabHands = hands;
  }
  if (hands == 3) {
    float turn = std::atan2(hand[1].x - hand[0].x, hand[1].z - hand[0].z) - g.magTwoAngle;
    while (turn > 3.14159265f) turn -= 6.2831853f;
    while (turn < -3.14159265f) turn += 6.2831853f;
    g.magRelYaw = std::max(-1.3f, std::min(1.3f, g.magTwoYaw + turn));
    g.magPos = {g.magTwoPos.x + mid.x - g.magTwoMid.x, g.magTwoPos.y + mid.y - g.magTwoMid.y,
                g.magTwoPos.z + mid.z - g.magTwoMid.z};
    g.magScale = std::max(0.5f, std::min(2.2f, g.magTwoScale * apart / g.magTwoDist));
  } else if (hands != 0) {
    const XrVector3f& p = hand[hands == 1 ? 0 : 1];
    g.magPos = {p.x + g.magGrabOff.x, p.y + g.magGrabOff.y, p.z + g.magGrabOff.z};
  }
  // r131 (Lance: "make it so the Right trigger can turn the page forward and the L trigger turns the page backwards"):
  // the right trigger (whichever hand holds it) turns a page on, the left one back, one page a pull (its curl; the
  // trigger-drag of a page is gone). Not in its first 0.9 s (a trigger squeezed through the match's end screens). The
  // hub keeps the triggers from the game while it's open.
  for (int h = 0; h < 2; ++h) {
    const bool down = trig[h] > 0.55f;
    if (live && down && !g.magPrevTrig[h] && now - g.magOpenNs > 900000000ull) {
      const int dir = h == 1 ? 1 : -1;
      if (mag_flip(dir)) XLOG("franchise: a magazine page turned %s (%s trigger), leaf %d", dir > 0 ? "on" : "back", h == 1 ? "right" : "left", g.magTurn);
    }
    if (down) g.magPrevTrig[h] = true;
    g.magTrigBusy[h] = g.magPrevTrig[h];  // (the hub's own LT / RT turns stay off while a trigger is the magazine's)
    if (trig[h] < 0.35f) {
      g.magPrevTrig[h] = false;
      g.magTrigBusy[h] = false;
    }
  }
  g.magDrag = -1;
  // the free hand sweeping across it while the other holds it
  const int freeHand = g.magGrabHands == 1 ? 1 : (g.magGrabHands == 2 ? 0 : -1);
  int swipe = 0;
  if (live && freeHand >= 0 && g.aimValid[freeHand] && g.magDrag < 0 && trig[freeHand] < 0.35f) {
    const XrVector3f l = mag_local(f, g.aim[freeHand].position);
    const bool near = l.z > -0.05f && l.z < 0.2f && mag_on(f, l.x, l.y, 0.06f);
    g.magSweep[g.magSweepAt] = {near ? now : 0, l.x, l.y, l.z};
    g.magSweepAt = (g.magSweepAt + 1) % 24;
    if (near && now - g.magSwipeNs > 450000000ull) {
      for (const auto& s : g.magSweep) {
        if (s.ns == 0 || now - s.ns > 300000000ull) continue;
        const float dx = l.x - s.x;
        if (std::fabs(dx) > 0.15f) {
          const int dir = dx < 0.0f ? 1 : -1;
          if (!(dir == -g.magSwipeDir && now - g.magSwipeNs < 900000000ull)) swipe = dir;
          break;
        }
      }
    }
  } else {
    for (auto& s : g.magSweep) s.ns = 0;
  }
  if (swipe != 0) {
    g.magSwipeNs = now;
    g.magSwipeDir = swipe;
    for (auto& s : g.magSweep) s.ns = 0;
    if (mag_flip(swipe)) XLOG("franchise: a magazine page swiped %s", swipe > 0 ? "on" : "back");
  }
}
// r131 (Lance: "im nitpicking so i want you to make the magazine more flimsy like a magazine would actually be???"):
// the pages as paper, a bending grid each in the effects layer (model slot kMagSlot, its pictures the pages as drawn,
// mipmapped + anisotropic, the paper shading of xr_fx_model.frag: as drawn where it faces you). Each leaf's cross
// section across the page is walked along its own length (so the paper never stretches): an open magazine's pages
// rise a little from the spine (a gull wing) and each side's free edge bends back by its "flop", a damped spring
// (underdamped: it overshoots and sways when it stops / is let go): resting it hangs slightly bent; held in one hand
// the side farther from the hand droops most (more open than closed); held in both it pulls nearly flat; the
// magazine's own acceleration (toward / away from you, its turn) throws the edges back and forth, and moving fast it
// flutters. A turning leaf rolls over the spine with its outer part lagging (a curl travelling across it, the bottom
// corner first), its back page showing on its other side. Until all six pages are drawn (or without the effects
// layer) the flat quads stand in.
constexpr int kMagSlot = xrfx::kModelSlots - 1;
constexpr int kMagNX = 24, kMagNY = 8;  // a page's grid: across (it bends this way), down
constexpr int kMagNV = (kMagNX + 1) * (kMagNY + 1);
bool mag_paper_frame(xrfx::Scene& scene, const MagFrame& f, uint64_t now) {
  if (!g.fxReady) return false;
  // r137: drawn again for its big pictures while open, the pages it has stay up till all six are drawn again
  bool drawn = g.magModelGen != g.paperGen;
  for (int pg = 0; drawn && pg < kMagPages; ++pg) {
    AsyncPanel& a = g_paperP[pg];
    if (a.forGeneration != g.paperGen || a.ready.load() != a.requested || a.width != kMagW || a.height != kMagH) drawn = false;
  }
  if (g.magModelGen != g.paperGen && !drawn &&
      (g.paperGen != g_magBigRedrawGen || g.magModelGen < 0 || !xrfx::model_up(kMagSlot)))
    return false;
  if (drawn) {
    // this issue's pictures up once all six are drawn
    xrfx::Model m;
    m.vertexCount = static_cast<uint32_t>(kMagPages * kMagNV);
    for (int pg = 0; pg < kMagPages; ++pg) {
      {
        std::lock_guard<std::mutex> lock(g_paperP[pg].mutex);
        m.texRgba.push_back(g_paperP[pg].pixels);
      }
      m.texW.push_back(kMagW);
      m.texH.push_back(kMagH);
      const uint32_t base = static_cast<uint32_t>(pg * kMagNV);
      for (int i = 0; i <= kMagNY; ++i)
        for (int j = 0; j <= kMagNX; ++j) {
          const float u = static_cast<float>(j) / kMagNX;
          m.uv.push_back(pg % 2 == 0 ? u : 1.0f - u);  // a back page (odd) is read from the leaf's other side
          m.uv.push_back(static_cast<float>(i) / kMagNY);
        }
      xrfx::ModelPart part;
      part.tex = pg;
      part.firstIndex = static_cast<uint32_t>(m.indices.size());
      for (int i = 0; i < kMagNY; ++i)
        for (int j = 0; j < kMagNX; ++j) {
          const uint32_t a = base + static_cast<uint32_t>(i * (kMagNX + 1) + j), b = a + 1, c = a + kMagNX + 1, d = c + 1;
          m.indices.insert(m.indices.end(), {a, c, b, b, c, d});
        }
      part.indexCount = static_cast<uint32_t>(m.indices.size()) - part.firstIndex;
      m.parts.push_back(part);
    }
    xrfx::set_model(kMagSlot, std::move(m));
    g.magModelGen = g.paperGen;
    g.magVerts.assign(static_cast<size_t>(kMagPages) * kMagNV * 6, 0.0f);
    g.magPhysNs = 0;
    XLOG("franchise: the magazine's pages up as paper (%d x %d a page)%s", kMagNX, kMagNY,
         xrfx::model_up(kMagSlot) ? "" : " - failed, flat pages");
  }
  if (!xrfx::model_up(kMagSlot)) return false;
  // its motion: velocity, acceleration (smoothed), its turn's rate
  const float dt = g.magPhysNs != 0 ? std::min(0.05f, static_cast<float>(static_cast<double>(now - g.magPhysNs) * 1e-9)) : 0.0f;
  const XrVector3f xa = q_rot(f.q, XrVector3f{1.0f, 0.0f, 0.0f});
  const float ang = std::atan2(-xa.z, xa.x);
  if (dt > 1e-4f) {
    const XrVector3f v{(g.magPos.x - g.magLastPos.x) / dt, (g.magPos.y - g.magLastPos.y) / dt, (g.magPos.z - g.magLastPos.z) / dt};
    const float k = std::min(1.0f, dt * 20.0f);
    const XrVector3f a{(v.x - g.magVel.x) / dt, (v.y - g.magVel.y) / dt, (v.z - g.magVel.z) / dt};
    g.magAcc = {g.magAcc.x + (a.x - g.magAcc.x) * k, g.magAcc.y + (a.y - g.magAcc.y) * k, g.magAcc.z + (a.z - g.magAcc.z) * k};
    g.magVel = {g.magVel.x + (v.x - g.magVel.x) * k, g.magVel.y + (v.y - g.magVel.y) * k, g.magVel.z + (v.z - g.magVel.z) * k};
  } else {
    g.magVel = g.magAcc = {0.0f, 0.0f, 0.0f};
  }
  float turnRate = 0.0f;
  if (dt > 1e-4f) {
    float d = ang - g.magLastAng;
    while (d > 3.14159265f) d -= 6.2831853f;
    while (d < -3.14159265f) d += 6.2831853f;
    turnRate = std::max(-8.0f, std::min(8.0f, d / dt));
  }
  g.magLastAng = ang;
  g.magLastPos = g.magPos;
  g.magPhysNs = now;
  const int T = g.magTurn, L = g.magLeaves;
  const bool open = T >= 0 || (L > 0 && L < kMagLeaves);
  const float W = f.pageW, H = f.pageH;
  // each side's flop: where it's going (how it's held), what throws it, a spring
  const XrVector3f accL = q_rot(q_conj(f.q), g.magAcc);
  float handX = 0.0f;  // the holding hand across the magazine, from the spine
  if (g.magGrabHands == 1 || g.magGrabHands == 2)
    handX = q_rot(q_conj(f.q), XrVector3f{-g.magGrabOff.x, -g.magGrabOff.y, -g.magGrabOff.z}).x + f.shift * W;
  for (int side = 0; side < 2; ++side) {
    float target = open ? 0.10f : 0.05f;  // floating: a slight bend
    if (g.magGrabHands == 3) {
      target = 0.02f;  // both hands pull it flat
    } else if (g.magGrabHands != 0) {
      const float edge = side == 0 ? -W : W;
      const float far = std::min(1.0f, std::fabs(edge - handX) / (2.0f * W));
      target = (open ? 0.42f : 0.2f) * (0.3f + 0.9f * far);  // the side away from the hand droops most
    }
    const float push = -3.0f * accL.z + (side == 1 ? -0.9f : 0.9f) * turnRate;
    g.magFlopV[side] += (70.0f * (target - g.magFlop[side]) - 5.0f * g.magFlopV[side] + push) * dt;
    g.magFlop[side] = std::max(-0.6f, std::min(1.2f, g.magFlop[side] + g.magFlopV[side] * dt));
  }
  const float speed = std::sqrt(g.magVel.x * g.magVel.x + g.magVel.y * g.magVel.y + g.magVel.z * g.magVel.z);
  g.magFlutter += (std::min(0.07f, 0.05f * speed) - g.magFlutter) * std::min(1.0f, dt * 4.0f);
  const float t = static_cast<float>(static_cast<double>(now % 100000000000ull) * 1e-9);
  const float rest = open ? 0.16f : 0.03f;
  auto theta = [&](int leafSide, float u, float v) {  // 0 the right side, 1 the left, the angle across (from +x to +z)
    const float flut = g.magFlutter * u * std::sin(20.0f * t + 6.0f * u + 4.0f * v);
    if (leafSide == 0) return rest * (1.0f - u) * (1.0f - u) - g.magFlop[1] * u * u + flut;
    return 3.14159265f - rest * (1.0f - u) * (1.0f - u) + g.magFlop[0] * u * u - flut;
  };
  float* out = g.magVerts.data();
  for (int i = 0; i < kMagPages * kMagNV; ++i) {  // nothing shows but the pages filled below
    float* o = out + static_cast<size_t>(i) * 6;
    o[0] = f.fold.x;
    o[1] = f.fold.y;
    o[2] = f.fold.z;
    o[3] = o[4] = o[5] = 0.0f;
  }
  // a leaf (its front page 2 x leaf, its back 2 x leaf + 1): e 0 flat on the right .. 1 flat on the left; which of its
  // sides to fill; lifted toward you (the turning one over the stacks)
  const float dir = g.magTo >= g.magFrom ? 1.0f : -1.0f;
  auto leaf = [&](int idx, float e, bool front, bool back, float lift, bool turning) {
    for (int i = 0; i <= kMagNY; ++i) {
      const float v = static_cast<float>(i) / kMagNY;
      const float y = (0.5f - v) * H;
      const float lagRow = 0.55f + 0.45f * std::fabs(v - 0.95f);  // the bottom corner leads
      auto ang2 = [&](float u) {
        float a = (1.0f - e) * theta(0, u, v) + e * theta(1, u, v);
        if (turning) a -= dir * 1.05f * std::sin(3.14159265f * e) * std::pow(u, 1.4f) * lagRow;
        return a;
      };
      float x = 0.0f, z = 0.0f;
      for (int j = 0; j <= kMagNX; ++j) {
        if (j > 0) {
          const float am = ang2((j - 0.5f) / kMagNX);
          x += std::cos(am) * W / kMagNX;
          z += std::sin(am) * W / kMagNX;
        }
        const float a = ang2(static_cast<float>(j) / kMagNX);
        const XrVector3f nl{-std::sin(a), 0.0f, std::cos(a)};
        const XrVector3f p = q_rot(f.q, XrVector3f{x, y, z + lift});
        const XrVector3f n = q_rot(f.q, nl);
        const XrVector3f at{f.fold.x + p.x, f.fold.y + p.y, f.fold.z + p.z};
        const int vi = i * (kMagNX + 1) + j;
        if (front) {
          float* o = out + (static_cast<size_t>(2 * idx) * kMagNV + vi) * 6;
          o[0] = at.x;
          o[1] = at.y;
          o[2] = at.z;
          o[3] = n.x;
          o[4] = n.y;
          o[5] = n.z;
        }
        if (back && 2 * idx + 1 < kMagPages) {
          float* o = out + (static_cast<size_t>(2 * idx + 1) * kMagNV + vi) * 6;
          o[0] = at.x - n.x * 0.0012f;
          o[1] = at.y - n.y * 0.0012f;
          o[2] = at.z - n.z * 0.0012f;
          o[3] = -n.x;
          o[4] = -n.y;
          o[5] = -n.z;
        }
      }
    }
  };
  if (T >= 0) {
    if (T >= 1) leaf(T - 1, 1.0f, false, true, 0.0f, false);
    if (T + 1 < kMagLeaves) leaf(T + 1, 0.0f, true, false, 0.0f, false);
    leaf(T, std::max(0.0f, std::min(1.0f, g.magSide)), true, true, 0.003f, true);
  } else {
    if (L >= 1) leaf(L - 1, 1.0f, false, true, 0.0f, false);
    if (L < kMagLeaves) leaf(L, 0.0f, true, false, 0.0f, false);
  }
  xrfx::Scene::ModelDraw& md = scene.models[kMagSlot];
  md.on = true;
  md.posNrm = g.magVerts.data();
  md.white = 0.0f;
  md.alpha = 1.0f;
  md.glow = 0.0f;
  md.under = -1.0f;  // the paper shading
  md.glowRgb = 0xFFFFFF;
  md.flameFrame = -1;
  md.shine = 0.0f;
  md.frame = -1;
  return true;
}
// Its pictures let go (the magazine put down).
void mag_paper_drop() {
  if (g.magModelGen < 0) return;
  xrfx::set_model(kMagSlot, xrfx::Model{});
  g.magModelGen = -1;
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

// r113 (Lance: "the franchise menu's to be redone and more attention grabbing/less words, maybe make an actual
// newspaper ... we need more images or VR native experiences that can do the same thing we're trying to force
// people to read"): the franchise hub's pages and THE DAILY STRIKE's, laid out as pieces for xrtext::board.
// A transform (ox, oy, k) lets a page be drawn small inside another (the paper's front page on the hub).
struct Pieces {
  std::vector<xrtext::BoardPiece> v;
  float ox = 0.0f, oy = 0.0f, k = 1.0f;
  // r137: the big pictures its pieces show (mag_pic), kept as long as it is (a deque: they never move), and whether one
  // was left out (still being drawn: its portrait in its place)
  std::deque<GuestPortrait> hold;
  bool bigMissing = false;
  // r131 (STRIKERS ILLUSTRATED): how the words look until changed: leaning, condensed, turned, a drop shadow, the
  // outline's colour (plain(): upright, full width, none, dark)
  float tSlant = 0.0f, tXscale = 1.0f, tAngle = 0.0f, tShadow = 0.0f, tShadowA = 0.55f;
  uint32_t tShadowRgb = 0x000000, tRim = 0x05070C;
  void plain() {
    tSlant = tAngle = tShadow = 0.0f;
    tXscale = 1.0f;
    tShadowA = 0.55f;
    tShadowRgb = 0x000000;
    tRim = 0x05070C;
  }
  void style(float slant, float xscale = 1.0f, float angle = 0.0f, float shadow = 0.0f, uint32_t shadowRgb = 0x000000,
             float shadowA = 0.55f) {
    tSlant = slant;
    tXscale = xscale;
    tAngle = angle;
    tShadow = shadow;
    tShadowRgb = shadowRgb;
    tShadowA = shadowA;
  }
  void rim(uint32_t rgb) { tRim = rgb; }
  xrtext::BoardPiece& add(xrtext::BoardPiece::Kind kind, float x0, float y0, float x1, float y1) {
    xrtext::BoardPiece p;
    p.kind = kind;
    p.x0 = ox + x0 * k;
    p.y0 = oy + y0 * k;
    p.x1 = ox + x1 * k;
    p.y1 = oy + y1 * k;
    v.push_back(p);
    return v.back();
  }
  void box(float x0, float y0, float x1, float y1, float r, uint32_t rgb, float a = 1.0f, uint32_t rgb2 = 0,
           float a2 = -1.0f) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kBox, x0, y0, x1, y1);
    p.radius = r * k;
    p.rgb = rgb;
    p.alpha = a;
    p.rgb2 = rgb2;
    p.alpha2 = a2;  // r131: fading to it at the bottom
  }
  void ring(float x0, float y0, float x1, float y1, float r, float width, uint32_t rgb, float a = 1.0f) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kRing, x0, y0, x1, y1);
    p.radius = r * k;
    p.outline = width * k;
    p.rgb = rgb;
    p.alpha = a;
  }
  void glow(float x0, float y0, float x1, float y1, uint32_t rgb, float a) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kGlow, x0, y0, x1, y1);
    p.rgb = rgb;
    p.alpha = a;
  }
  void image(const uint8_t* rgba, int w, int h, float x0, float y0, float x1, float y1, bool bottom, float a = 1.0f,
             uint32_t silhouette = 0) {
    if (rgba == nullptr || w <= 0 || h <= 0) return;
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kImage, x0, y0, x1, y1);
    p.img = rgba;
    p.iw = w;
    p.ih = h;
    p.bottom = bottom;
    p.alpha = a;
    p.silhouette = silhouette != 0;
    p.rgb = silhouette;
  }
  void image(const GuestPortrait& g, float x0, float y0, float x1, float y1, bool bottom, float a = 1.0f,
             uint32_t silhouette = 0) {
    image(g.rgba.empty() ? nullptr : g.rgba.data(), g.w, g.h, x0, y0, x1, y1, bottom, a, silhouette);
  }
  // r131: a cut-out (a portrait), its see-through margins trimmed: fitted with its feet on the box's bottom, or
  // cropped to fill the box (fx, fy: the part kept); mirrored; duotone dark -> light (both 0: its own colours)
  void figure(const GuestPortrait& g, float x0, float y0, float x1, float y1, bool crop, float fx, float fy,
              bool flip = false, uint32_t dark = 0, uint32_t light = 0, float a = 1.0f) {
    if (g.rgba.empty() || g.w <= 0 || g.h <= 0) return;
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kImage, x0, y0, x1, y1);
    p.img = g.rgba.data();
    p.iw = g.w;
    p.ih = g.h;
    p.bottom = true;
    p.alpha = a;
    p.trim = true;
    p.crop = crop;
    p.focusX = fx;
    p.focusY = fy;
    p.flip = flip;
    p.duo = (dark | light) != 0;
    p.rgb = dark;
    p.rgb2 = light;
  }
  // r131: a photo cropped to fill the box (fx, fy: the part kept)
  void photo(const uint8_t* rgba, int w, int h, float x0, float y0, float x1, float y1, float fx, float fy,
             float a = 1.0f) {
    if (rgba == nullptr || w <= 0 || h <= 0) return;
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kImage, x0, y0, x1, y1);
    p.img = rgba;
    p.iw = w;
    p.ih = h;
    p.alpha = a;
    p.crop = true;
    p.focusX = fx;
    p.focusY = fy;
  }
  // r131: a convex polygon (x, y pairs), a star burst, halftone dots
  void poly(std::initializer_list<float> xy, uint32_t rgb, float a = 1.0f) {
    xrtext::BoardPiece p;
    p.kind = xrtext::BoardPiece::kPoly;
    int i = 0;
    for (float f : xy) p.pts.push_back((i++ % 2) == 0 ? ox + f * k : oy + f * k);
    p.rgb = rgb;
    p.alpha = a;
    v.push_back(p);
  }
  void burst(float x0, float y0, float x1, float y1, int points, float inner, float angle, uint32_t rgb, float a = 1.0f) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kBurst, x0, y0, x1, y1);
    p.points = points;
    p.inner = inner;
    p.angle = angle;
    p.rgb = rgb;
    p.alpha = a;
  }
  void dots(float x0, float y0, float x1, float y1, float cell, float f0, float f1, uint32_t rgb, float a) {
    xrtext::BoardPiece& p = add(xrtext::BoardPiece::kDots, x0, y0, x1, y1);
    p.radius = cell * k;
    p.dot0 = f0;
    p.dot1 = f1;
    p.rgb = rgb;
    p.alpha = a;
  }
  // words: centred on x (kText), from x (kTextLeft) or ending at x (kTextRight), `px` high, shrunk into `room`
  // (r131: in the style set, the outline in the rim colour)
  void words(xrtext::BoardPiece::Kind kind, const std::string& t, float x, float y, float px, float room, uint32_t rgb,
             float outline = 0.0f, uint32_t rgb2 = 0) {
    xrtext::BoardPiece p;
    p.kind = kind;
    p.x0 = ox + x * k;
    p.y0 = oy + y * k;
    p.x1 = room * k;
    p.y1 = px * k;
    p.rgb = rgb;
    p.rgb2 = rgb2;
    p.outline = outline * k;
    p.outlineRgb = tRim;
    p.text = t;
    p.slant = tSlant;
    p.xscale = tXscale;
    p.angle = tAngle;
    p.shadow = tShadow * k;
    p.shadowRgb = tShadowRgb;
    p.shadowA = tShadowA;
    v.push_back(p);
  }
  void text(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline = 0.0f,
            uint32_t rgb2 = 0) {
    words(xrtext::BoardPiece::kText, t, x, y, px, room, rgb, outline, rgb2);
  }
  void left(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline = 0.0f,
            uint32_t rgb2 = 0) {
    words(xrtext::BoardPiece::kTextLeft, t, x, y, px, room, rgb, outline, rgb2);
  }
  void right(const std::string& t, float x, float y, float px, float room, uint32_t rgb, float outline = 0.0f,
             uint32_t rgb2 = 0) {
    words(xrtext::BoardPiece::kTextRight, t, x, y, px, room, rgb, outline, rgb2);
  }
  float width(const std::string& t, float px) const { return xrtext::board_text_width(t, px * k) / k * tXscale; }
  // a controller button (a circle, or a pill for "LT RT"), left edge at x; its width
  float button(const std::string& glyph, float x, float cy, float size, uint32_t rgb) {
    struct {
      float tSlant, tXscale, tAngle, tShadow, tShadowA;
      uint32_t tShadowRgb, tRim;
    } const keep = {tSlant, tXscale, tAngle, tShadow, tShadowA, tShadowRgb, tRim};  // r131: plain words for it
    plain();
    const float r = 0.5f * size;
    const float wide = glyph.size() > 1 ? std::max(size, width(glyph, 0.62f * size) + 0.7f * size) : size;
    box(x - 3.0f, cy - r - 3.0f, x + wide + 3.0f, cy + r + 3.0f, r + 3.0f, 0xFFFFFF);
    box(x, cy - r, x + wide, cy + r, r, rgb);
    text(glyph, x + 0.5f * wide, cy, 0.62f * size, wide, 0xFFFFFF, 2.0f);
    tSlant = keep.tSlant;
    tXscale = keep.tXscale;
    tAngle = keep.tAngle;
    tShadow = keep.tShadow;
    tShadowA = keep.tShadowA;
    tShadowRgb = keep.tShadowRgb;
    tRim = keep.tRim;
    return wide;
  }
};
struct HubHint {
  std::string glyph, words;
  uint32_t rgb;
};
// the foot: buttons and a word or two each, centred
void hub_hints(Pieces& P, float w, float cy, const std::vector<HubHint>& hints) {
  const float size = 54.0f, px = 40.0f;
  float total = 0.0f;
  for (const HubHint& h : hints) {
    const float gw = h.glyph.size() > 1 ? std::max(size, P.width(h.glyph, 0.62f * size) + 0.7f * size) : size;
    total += gw + 16.0f + P.width(h.words, px) + 56.0f;
  }
  float x = 0.5f * (w - total + 56.0f);
  for (const HubHint& h : hints) {
    x += P.button(h.glyph, x, cy, size, h.rgb) + 16.0f;
    P.left(h.words, x, cy, px, 600.0f, 0xE6ECF5, 2.0f);
    x += P.width(h.words, px) + 56.0f;
  }
}
// r137 (Lance OK'd "the cover star rendered big on the headset when each issue comes out"; the 256 portraits were
// stretched 3-4x on the 1080 x 1440 pages): a character's big picture (xr_big: its own 3D model drawn `size` px on this
// headset, the portraits' look, sharp), kept with the page's pieces; else its portrait (no model here: for good; still
// being drawn: P.bigMissing, and the issue is drawn again once it's there). shiny: its shiny pack's colours.
const GuestPortrait& mag_pic(Pieces& P, const std::string& key, bool shiny, int size) {
  if (!key.empty()) {
    GuestPortrait big;
    if (xrbig::get(key, shiny, size, &big.rgba)) {
      big.w = big.h = static_cast<int>(std::lround(std::sqrt(static_cast<double>(big.rgba.size() / 4))));
      P.hold.push_back(std::move(big));
      return P.hold.back();
    }
    if (!xrbig::failed(key, shiny, size)) P.bigMissing = true;
  }
  return guest_portrait(key);
}
// The form a franchise form evolves / suits up into (franchise/evolutions.txt's first line from it), "" none.
std::string next_form_key(const std::string& from) {
  static std::mutex mutex;
  static std::unordered_map<std::string, std::string> next;
  static bool read = false;
  std::lock_guard<std::mutex> lock(mutex);
  if (!read) {
    read = true;
    const char* home = std::getenv("STRIKERS_HOME");
    FILE* f = home != nullptr ? std::fopen((std::string(home) + "/franchise/evolutions.txt").c_str(), "r") : nullptr;
    if (f != nullptr) {
      char a[96], b[96], line[256];
      int level = 0;
      while (std::fgets(line, sizeof(line), f) != nullptr)
        if (line[0] != '#' && std::sscanf(line, "%95s %95s %d", a, b, &level) == 3 && next.find(a) == next.end())
          next[a] = b;
      std::fclose(f);
    }
  }
  auto it = next.find(from);
  return it != next.end() ? it->second : "";
}
// A club's mascot: its full name without its place ("Death Mountain Dodongos" -> "Dodongos").
std::string club_mascot(const FrClubInfo& c) {
  if (c.name.size() > c.place.size() + 1 && c.name.compare(0, c.place.size(), c.place) == 0)
    return c.name.substr(c.place.size() + 1);
  return c.name;
}
// r135 (Lance: "make the strikers illustrated more clear. its kinda blurry text/image wise"): words too wide for one
// line at `px` (`roomOne`) split in two at the space where they fit best (line 1 in `room1`, line 2 in `room2`), so
// they stay big instead of shrinking to fit; *b empty = one line. (Was split_two: by letter count, then shrunk.)
void mag_fit_two(const Pieces& P, const std::string& s, float px, float roomOne, float room1, float room2,
                 std::string* a, std::string* b) {
  *a = s;
  b->clear();
  if (P.width(s, px) <= roomOne) return;
  float best = 1e9f;
  for (size_t i = 1; i + 1 < s.size(); ++i) {
    if (s[i] != ' ') continue;
    const float f = std::max(P.width(s.substr(0, i), px) / room1, P.width(s.substr(i + 1), px) / room2);
    if (f < best) {
      best = f;
      *a = s.substr(0, i);
      *b = s.substr(i + 1);
    }
  }
}

// --- r131 (Lance: "i said magazine, changing it from newspaper to magazine, and the reason is i want you to get freaky
// with some of these magazine cover designs ... each cover should be an early 2000's nod to the sports illustrated
// covers. we can even call this magazine "Strikers Illustrated""): STRIKERS ILLUSTRATED (r113's THE DAILY STRIKE), a
// page 1080 x 1440: 0 the cover, 1 scorecard (around the league), 2 the table, 3 star watch, 4 rivals, 5 the back
// cover (a made-up ad). The cover's template comes from what happened (mag_story): CLASSIC (a win, a hat trick, the
// golden boot's leader), SPECIAL (your player evolved / suited up), GOLD (champions), MOODY (a big loss, a losing
// streak "THE JINX?", the season over), FACEOFF (your nemesis), BRACKET (the playoffs), STREAK (4+ wins in a row),
// PREVIEW (a new season); each a big condensed masthead behind the cover star's head, a huge leaning headline, cover
// lines, a corner sash, the date / price / a barcode. All our own drawing on the player's own goal photo and the
// portraits / logos made on the headset. PC check: tools\outside\magazine_preview.py (this, line for line) ->
// C:\StrikersMR\logs\magazine.
enum { kMagClassic, kMagSpecial, kMagGold, kMagMoody, kMagFaceoff, kMagBracket, kMagStreak, kMagPreview };
const char* const kMagTemplates[8] = {"CLASSIC", "SPECIAL", "GOLD", "MOODY", "FACEOFF", "BRACKET", "STREAK", "PREVIEW"};
constexpr float kMagWf = 1080.0f, kMagHf = 1440.0f;
constexpr uint32_t kMagRed = 0xD3202A, kMagYellow = 0xFFD23F, kMagInk = 0x111111;
constexpr uint32_t kPaperRed = 0xB3261E;  // r113's red (the hub's wanted posters still use it)
struct MagStory {
  int tmpl = kMagClassic;
  std::string kicker, head1, head2, sub, banner, star, starName, rival, rivalName, big, date, issue, before;
  std::string inset, insetText;  // CLASSIC's small "PLUS:" picture
  int starClub = -1, rivalClub = -1;
  uint32_t seed = 0, c1 = 0x1E5AC8, c2 = 0xF2C230;
  bool shiny = false;
  int variant = 0;                 // r131: the random cover's colours / sash / mirrored star
  std::string bigWord = "STRAIGHT";  // r131: STREAK's burst word under the number
  std::vector<std::pair<std::string, std::string>> lines;  // the cover lines: a label, its words
  bool win = false;  // r143: the cover star's club won its last game (CLASSIC's confetti)
};
// The form a franchise form evolves / suits up FROM (evolutions.txt's first line to it), "" none.
std::string prev_form_key(const std::string& to) {
  static std::mutex mutex;
  static std::unordered_map<std::string, std::string> prev;
  static bool read = false;
  std::lock_guard<std::mutex> lock(mutex);
  if (!read) {
    read = true;
    const char* home = std::getenv("STRIKERS_HOME");
    FILE* f = home != nullptr ? std::fopen((std::string(home) + "/franchise/evolutions.txt").c_str(), "r") : nullptr;
    if (f != nullptr) {
      char a[96], b[96], line[256];
      int level = 0;
      while (std::fgets(line, sizeof(line), f) != nullptr)
        if (line[0] != '#' && std::sscanf(line, "%95s %95s %d", a, b, &level) == 3 && prev.find(b) == prev.end())
          prev[b] = a;
      std::fclose(f);
    }
  }
  auto it = prev.find(to);
  return it != prev.end() ? it->second : "";
}
std::string mag_ordinal(int n) {
  const int t = n % 100, o = n % 10;
  const char* suf = (t >= 10 && t <= 20) ? "TH" : (o == 1 ? "ST" : (o == 2 ? "ND" : (o == 3 ? "RD" : "TH")));
  return std::to_string(n) + suf;
}
// Today as a magazine's cover date ("NOVEMBER 1, 2026").
std::string mag_date() {
  static const char* const kMonths[12] = {"JANUARY", "FEBRUARY", "MARCH",     "APRIL",   "MAY",      "JUNE",
                                          "JULY",    "AUGUST",   "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"};
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  return std::string(kMonths[std::max(0, std::min(11, tm.tm_mon))]) + " " + std::to_string(tm.tm_mday) + ", " +
         std::to_string(1900 + tm.tm_year);
}
// What this issue is about: the cover's template, its words, its stars (your last game, the week's news about you,
// the last played match's own numbers, your nemesis, the playoffs, the season).
MagStory mag_story_real(const FrSnapshot& s) {
  const PortFranchiseView& v = s.view;
  const int n = static_cast<int>(s.clubs.size());
  const int me = v.myClub;
  MagStory st;
  auto place = [&](int c) { return c >= 0 && c < n ? fr_upper(s.clubs[c].place) : std::string("?"); };
  auto mascot = [&](int c) { return c >= 0 && c < n ? fr_upper(club_mascot(s.clubs[c])) : std::string("?"); };
  st.date = mag_date();
  if (v.lastWeek > PORT_FR_WEEKS) st.issue = "PLAYOFFS, DAY " + std::to_string(v.lastWeek - PORT_FR_WEEKS);
  else if (v.lastWeek > 0) st.issue = "WEEK " + std::to_string(v.lastWeek);
  else st.issue = "PRESEASON";
  st.seed = static_cast<uint32_t>(v.season * 131 + v.lastWeek * 7 + std::max(0, me));
  st.starClub = me;
  // your last game
  const PortFranchiseView::Played* mine = nullptr;
  for (int i = 0; i < v.playedCount && i < 16; ++i)
    if (v.played[i].mine) mine = &v.played[i];
  if (me < 0 || me >= n) mine = nullptr;
  int opp = -1, my = 0, their = 0, margin = 0;
  bool won = false, ot = false;
  std::string verb;
  if (mine != nullptr) {
    const bool home = mine->home == me;
    opp = home ? mine->away : mine->home;
    my = home ? mine->homeGoals : mine->awayGoals;
    their = home ? mine->awayGoals : mine->homeGoals;
    won = my > their;
    st.win = won;  // r143
    margin = std::abs(my - their);
    ot = mine->overtime;
    if (won) verb = ot ? " STUN " : (margin >= 4 ? " CRUSH " : (margin >= 2 ? " BEAT " : " EDGE "));
    else verb = ot ? " FALL IN OT TO " : (margin >= 4 ? " CRUSHED BY " : " FALL TO ");
  }
  const std::string result =
      mascot(me) + verb + place(opp) + " " + std::to_string(my) + "-" + std::to_string(their) + (ot ? " (OT)" : "");
  // the cover star: your starting captain
  st.star = me >= 0 && me < n ? std::string(v.clubCaptainKey[me]) : std::string(v.captainKey);
  st.starName = fr_upper(v.startsBackup && v.backup[0] != '\0' ? v.backup : v.captain);
  const std::string myPlace = place(me);
  // the week's news about you: a hat trick, a streak, an evolution / new costume
  std::string hat, evoFrom, evoTo, evoKey;
  int hatGoals = 0, streakWon = 0, streakLost = 0;
  bool costume = false;
  for (int i = 0; i < v.newsCount && i < 8; ++i) {
    const std::string line = v.news[i];
    const size_t scored = line.find(" SCORED ");
    if (line.starts_with("HAT TRICK: ") && line.ends_with(" FOR " + myPlace) && scored != std::string::npos) {
      hat = line.substr(11, scored - 11);
      hatGoals = std::atoi(line.c_str() + scored + 8);
    }
    if (line.starts_with(myPlace + " HAVE WON ") && line.ends_with(" IN A ROW"))
      streakWon = std::atoi(line.c_str() + myPlace.size() + 10);
    if (line.starts_with(myPlace + " HAVE LOST ") && line.ends_with(" STRAIGHT"))
      streakLost = std::atoi(line.c_str() + myPlace.size() + 11);
    const bool evolved = line.find(" EVOLVED INTO ") != std::string::npos, suited = line.find(" SUITED UP: ") != std::string::npos;
    if (line.starts_with(myPlace + "'S ") && (evolved || suited)) {
      costume = suited;
      const std::string sep = costume ? " SUITED UP: " : " EVOLVED INTO ";
      const size_t at = line.find(sep);
      evoFrom = line.substr(myPlace.size() + 3, at - myPlace.size() - 3);
      evoTo = line.substr(at + sep.size());
      st.shiny = evoTo.ends_with(" - SHINY!");
      if (st.shiny) evoTo.resize(evoTo.size() - 9);
    }
  }
  // what the hub saw itself (r131 MagNews): your evolution this week, the last played match's own numbers
  const MagNews& mn = s.mag;
  if (!mn.evoTo.empty() && mn.evoSeason == v.season && mn.evoWeek == v.lastWeek) {
    evoFrom = fr_upper(mn.evoFrom);
    evoTo = fr_upper(mn.evoTo);
    evoKey = mn.evoToKey;
    costume = mn.evoCostume;
    st.shiny = mn.evoShiny;
  }
  const bool liveHere = mn.live.valid && mn.liveSeason == v.season && mn.liveWeek == v.lastWeek;
  if (hat.empty() && liveHere && mn.live.goals >= 3) {
    hat = fr_upper(mn.live.name);
    hatGoals = mn.live.goals;
  }
  if (hat.empty() && liveHere && mn.live.co && mn.live.coGoals >= 3) {
    hat = fr_upper(mn.live.coName);
    hatGoals = mn.live.coGoals;
  }
  // your nemesis: the last game against its club, or the next
  int nemLast = -1;
  for (int i = 0; i < v.nemCount && i < 8; ++i)
    if (v.nems[i].active && !v.nems[i].teammate && v.nems[i].club == opp && opp >= 0) nemLast = i;
  const int nem = nemLast >= 0 ? nemLast : v.nextNemesis;
  auto key_of = [&](const std::string& name) -> std::string {
    if (name == fr_upper(v.captain)) return v.captainKey;
    if (v.backup[0] != '\0' && name == fr_upper(v.backup)) return v.backupKey;
    for (const PortFranchiseEvoRow& r : s.evoWatch)
      if (r.mine && fr_upper(r.name) == name) return r.key;
    return v.kindKey[0] != '\0' ? std::string(v.kindKey) : st.star;
  };
  const PortFranchiseView::Leader* lead = v.leaderCount > 0 ? &v.leaders[0] : nullptr;
  if (v.phase == PORT_FR_PHASE_DONE && v.champion == me && me >= 0 && me < n) {
    st.tmpl = kMagGold;
    st.kicker = "SEASON " + std::to_string(v.season) + " SUPER STRIKE SERIES";
    st.head1 = v.titles <= 1 ? "CHAMPIONS!" : (v.titles == 2 ? "BACK-TO-BACK!" : "DYNASTY!");
    st.sub = "THE " + mascot(me) + " WIN IT ALL";
    st.banner = "SOUVENIR ISSUE";  // r135: was COMMEMORATIVE ISSUE (too long for the corner's band at 40 px)
  } else if (v.phase == PORT_FR_PHASE_DONE) {
    st.tmpl = kMagMoody;
    st.kicker = "SEASON " + std::to_string(v.season) + " IS OVER";
    st.head1 = "WAIT TILL";
    st.head2 = "NEXT YEAR";
    st.sub = place(v.champion) + " WON IT ALL. THE " + mascot(me) + " WATCHED.";
    st.banner = "SEASON WRAP";
  } else if (!evoTo.empty()) {
    st.tmpl = kMagSpecial;
    st.kicker = costume ? "A NEW LOOK" : "IT'S HAPPENING";
    st.head1 = costume ? "SUITED UP!" : "EVOLVED!";
    st.sub = evoFrom + " IS NOW " + evoTo;
    st.banner = "COLLECTOR'S EDITION";
    st.star = !evoKey.empty() ? evoKey : key_of(evoTo);
    st.starName = evoTo;
    st.before = prev_form_key(st.star);
  } else if (v.phase == PORT_FR_PHASE_PLAYOFFS) {
    st.tmpl = kMagBracket;
    st.kicker = fr_upper(v.roundName);
    if (fr_upper(v.status).find("ELIMINATED") != std::string::npos) st.head1 = "IT'S OVER";
    else if (v.round == 4) st.head1 = "THE FINAL";
    else if (mine != nullptr && won) st.head1 = v.mySeriesMe < 3 ? "ONE STEP CLOSER" : "SURVIVE & ADVANCE";
    else if (mine != nullptr) st.head1 = "BACKS TO THE WALL";
    else st.head1 = "PLAYOFF FEVER";
    if (mine != nullptr) st.sub = result;
    if (v.myGameNo > 0 && v.nextOpponent >= 0)
      st.sub = "SERIES: " + myPlace + " " + std::to_string(v.mySeriesMe) + " - " + std::to_string(v.mySeriesThem) + " " +
               place(v.nextOpponent);
    st.banner = "PLAYOFF PREVIEW";
  } else if (nem >= 0 && nem < v.nemCount && nem < 8) {
    st.tmpl = kMagFaceoff;
    const PortFranchiseView::Nem& nm = v.nems[nem];
    st.kicker = fr_upper(nm.title);
    st.head1 = nemLast >= 0 ? (won ? "SCORE SETTLED" : "BAD BLOOD") : "GRUDGE MATCH";
    st.rival = nm.key[0] != '\0' ? std::string(nm.key) : (nm.club >= 0 && nm.club < n ? std::string(v.clubCaptainKey[nm.club]) : "");
    st.rivalName = fr_upper(nm.name);
    st.rivalClub = nm.club;
    st.sub = "BEAT YOU " + std::to_string(nm.beatYou) + "  -  LOST TO YOU " + std::to_string(nm.lostToYou);
    st.banner = "THE RIVALRY ISSUE";
  } else if (!hat.empty()) {
    st.tmpl = kMagClassic;
    st.kicker = hat + " GOES OFF";
    st.head1 = "HAT TRICK!";
    st.sub = hat + " SCORES " + std::to_string(hatGoals) + (mine != nullptr ? " AS THE " + result : std::string());
    st.banner = "EXCLUSIVE";
    st.star = key_of(hat);
    st.starName = hat;
  } else if (streakLost >= 4) {
    st.tmpl = kMagMoody;
    st.kicker = std::to_string(streakLost) + " STRAIGHT LOSSES";
    st.head1 = "THE";
    st.head2 = "JINX?";
    st.sub = "IS IT THIS COVER? (SORRY, " + mascot(me) + ".)";
    st.banner = "SPECIAL REPORT";
  } else if (streakWon >= 4) {
    st.tmpl = kMagStreak;
    st.kicker = "THE " + mascot(me) + " CAN'T LOSE";
    st.head1 = "UNSTOPPABLE";
    st.big = std::to_string(streakWon);
    if (mine != nullptr) st.sub = result;
    st.banner = "HOT STREAK";
  } else if (mine != nullptr && !won && margin >= 3) {
    st.tmpl = kMagMoody;
    st.kicker = "OUCH.";
    st.head1 = "WHAT WENT";
    st.head2 = "WRONG?";
    st.sub = place(opp) + " " + std::to_string(their) + "-" + std::to_string(my) + " " + myPlace;
    st.banner = "SPECIAL REPORT";
  } else if (v.lastWeek == 0) {
    st.tmpl = kMagPreview;
    st.kicker = "SEASON " + std::to_string(v.season) + " PREVIEW";
    st.head1 = "YEAR OF THE";
    st.head2 = mascot(me) + "?";
    st.sub = v.nextOpponent >= 0 ? "UP FIRST: " + place(v.nextOpponent) : std::string("THE STRIKERS SUPER LEAGUE IS BACK");
    st.banner = "PREVIEW ISSUE";
  } else if (mine != nullptr && won) {
    st.tmpl = kMagClassic;
    st.kicker = st.issue;
    st.head1 = ot ? "OVERTIME!" : (margin >= 4 ? "BLOWOUT!" : (margin >= 2 ? "STATEMENT WIN" : "CLUTCH!"));
    st.sub = result;
    st.banner = "EXCLUSIVE";
  } else if (mine != nullptr) {
    st.tmpl = kMagMoody;
    st.kicker = st.issue;
    st.head1 = margin <= 1 ? "HEART" : "REALITY";
    st.head2 = margin <= 1 ? "BREAK" : "CHECK";
    st.sub = result;
    st.banner = "SPECIAL REPORT";
  } else {
    st.tmpl = kMagClassic;
    if (lead != nullptr) {
      st.kicker = "THE GOLDEN BOOT RACE";
      st.head1 = "THE MACHINE";
      st.sub = fr_upper(lead->name) + " LEADS THE LEAGUE WITH " + std::to_string(lead->goals) + " GOALS";
      st.star = lead->key;
      st.starName = fr_upper(lead->name);
      st.starClub = lead->club;
    } else {
      st.kicker = st.issue;
      st.head1 = "WATCH AND WAIT";
      st.sub = v.status[0] != '\0' ? fr_upper(v.status) : std::string("NO GAME FOR YOU THIS TIME");
    }
    st.banner = "EXCLUSIVE";
  }
  if (st.starClub >= 0 && st.starClub < n) {
    st.c1 = s.clubs[st.starClub].c1;
    st.c2 = s.clubs[st.starClub].c2;
  }
  // the cover lines: the rest of the issue
  if (lead != nullptr && st.head1 != "THE MACHINE")
    st.lines.push_back({"GOLDEN BOOT", fr_upper(lead->name) + ", " + std::to_string(lead->goals) + " GOALS"});
  if (v.nemCount > 0 && st.tmpl != kMagFaceoff)
    st.lines.push_back({"WANTED", fr_upper(v.nems[0].name) + " " + fr_upper(v.nems[0].title)});
  if (me >= 0 && me < n) {
    int rank = 0;
    for (int c = 0; c < 2; ++c)
      for (int i = 0; i < 16; ++i)
        if (v.conference[c][i].club == me) rank = i + 1;
    st.lines.push_back({"THE TABLE", myPlace + " " + std::to_string(v.wins) + "-" + std::to_string(v.losses) + ", " +
                                         mag_ordinal(std::max(1, rank))});
  }
  for (const PortFranchiseEvoRow& r : s.evoWatch) {
    if (st.lines.size() < 4 && r.mine && r.evoLevel > 0 && fr_upper(r.name) != st.starName) {
      st.lines.push_back({"RISING", fr_upper(r.name) + " NEARS LV " + std::to_string(r.evoLevel)});
      break;
    }
  }
  if (v.nemCount > 0 && st.tmpl != kMagFaceoff && v.nems[0].key[0] != '\0') {
    st.inset = v.nems[0].key;
    st.insetText = "WANTED";  // r135: "PLUS: " cut (the label 40 px in the picture's width)
  } else if (lead != nullptr && st.star != lead->key) {
    st.inset = lead->key;
    st.insetText = fr_upper(lead->name);
  }
  if (st.lines.size() < 4 && v.feedCount > 0) {
    const std::string f = v.feed[0];
    const size_t at = f.find(": ");
    if (f.starts_with("@") && at != std::string::npos && at > 0)
      st.lines.push_back({fr_upper(f.substr(0, at)), "\"" + fr_upper(f.substr(at + 2)) + "\""});
  }
  return st;
}
// --- r131 (Lance: "i do want the strikers illustrated to be produced at random, it shouldnt always be the hottest team
// or player to be on the cover, i almost want them randomly generated, as so to make sure no cover is duplicated or the
// idea gets bland"): each issue's cover drawn at random, once, on the game thread (mag_pick_issue) and kept in the
// career's magazine file: the star anyone in the league (each club's captain, the Pokemon / heroes, the scorers, your
// own players; the week's real stories weighted up), the template at random where it fits (GOLD only for champions,
// BRACKET only in the playoffs, SPECIAL only when your player evolved / suited up that week), a headline from its
// template's pool, a variant (colours, sash words, the star mirrored). Never the same star + template + headline twice
// in a career, never the same star two issues running, the same template twice running only for GOLD / SPECIAL.
// Seeded from the league as it stands (season, week, every score): both co-op headsets roll the same cover.
// The pools (PC check tools\outside\magazine_preview.py reads them from here): {N} the star, {M} its club's mascot,
// {P} its club's place; "A|B" two lines (MOODY, PREVIEW); a leading '-' = only when its club is losing.
static const char* const kMagHeadClassic[] = {
    "THE CHOSEN ONE", "SHOWTIME", "PRIME TIME", "THE NATURAL", "HOW DOES {N} DO IT?", "BETTER THAN EVER?",
    "THE REAL DEAL", "MONEY", "LIGHTS OUT", "BELIEVE IT", "STAR POWER", "THE FRANCHISE", "MAKE WAY", "GAME CHANGER",
    "NEXT LEVEL", "HERE TO STAY", "ALL IN", "THE NEXT ONE?", "MR. CLUTCH", "NO DOUBT"};
static const char* const kMagHeadSpecial[] = {
    "EVOLVED!", "THE NEW {N}", "LEVEL UP!", "REBORN", "TRANSFORMED", "GLOW UP", "NEW AND IMPROVED", "THE UPGRADE",
    "FINAL FORM?", "BIGGER. BADDER.", "IT'S HERE", "A NEW ERA", "WHOLE NEW LOOK", "NEXT CHAPTER", "THE REVEAL",
    "METAMORPHOSIS"};
static const char* const kMagHeadGold[] = {
    "CHAMPIONS!", "ON TOP OF THE WORLD", "DYNASTY?", "KINGS OF THE LEAGUE", "PARADE TIME", "HOIST IT!",
    "THE BEST. PERIOD.", "FINALLY!", "WORTH THE WAIT", "CROWNED", "TITLE TOWN", "GLORY!", "NUMBER ONE",
    "THE LAST WORD", "PERFECT ENDING", "CHAMPS!"};
static const char* const kMagHeadMoody[] = {
    "-WHAT WENT|WRONG?", "-THE|JINX?", "-WAIT TILL|NEXT YEAR", "REDEMPTION|SONG", "SNUBBED", "THE QUIET|STORM",
    "ALONE AT|THE TOP", "-THE|COMEBACK?", "UNDER|PRESSURE", "WHAT|NOW?", "THE|DARK HORSE", "-HEART|BREAK",
    "-REALITY|CHECK", "PROVE|THEM WRONG", "STILL|HUNGRY", "THE|SURVIVOR", "NO|EXCUSES", "THE LONELY|ROAD"};
static const char* const kMagHeadFaceoff[] = {
    "SHOWDOWN", "BAD BLOOD", "GRUDGE MATCH", "HEAD TO HEAD", "CLASH OF TITANS", "ONLY ONE CAN WIN", "FACE OFF",
    "THE RIVALRY", "TALE OF THE TAPE", "BEEF", "MANO A MANO", "OLD SCORES", "DUEL", "THE BIG ONE",
    "WHO BLINKS FIRST?", "IT'S PERSONAL"};
static const char* const kMagHeadBracket[] = {
    "PLAYOFF FEVER", "ONE STEP CLOSER", "SURVIVE & ADVANCE", "WHO STOPS THEM?", "THE ROAD TO GLORY",
    "MARCH TO THE FINAL", "WIN OR GO HOME", "POSTSEASON MAGIC", "BRACKET BUSTERS", "CROWN IN SIGHT",
    "THE CHASE IS ON", "NO TOMORROW", "DO OR DIE", "MONEY TIME", "HEAT CHECK", "SUDDEN DEATH"};
static const char* const kMagHeadStreak[] = {
    "UNSTOPPABLE", "WHO STOPS THEM?", "ON FIRE", "RED HOT", "ROLLING", "THE MACHINE", "CAN'T STOP. WON'T STOP.",
    "FULL THROTTLE", "RUNAWAY TRAIN", "ON A ROLL", "STREAKING", "UNBEATABLE?", "HEATING UP", "NO BRAKES", "MOMENTUM",
    "HOT HOT HOT"};
static const char* const kMagHeadPreview[] = {
    "YEAR OF THE|{M}?", "THE NEXT|ONE?", "WATCH|OUT", "RISING|STAR", "THE|NATURAL", "BIG THINGS|AHEAD", "THE|CONTENDER",
    "READY OR|NOT", "DON'T SLEEP ON|THE {M}", "THE|FUTURE", "HERE COMES|{N}", "SECOND HALF|SURGE", "SLEEPER|ALERT",
    "THE|BREAKOUT", "HYPE|TRAIN", "ALL EYES|ON {N}"};
static const char* const kMagKickers[] = {
    "{N} UNCOVERED", "THE {N} STORY", "{N} SPEAKS", "MEET {N}", "{N}: THE INTERVIEW", "INSIDE {P}", "THE {M} FILES",
    "{N} UP CLOSE", "THE TRUTH ABOUT {N}", "{N} LIKE NEVER BEFORE", "WHY {N} MATTERS", "{P}'S SECRET WEAPON"};
static const char* const kMagSashes[] = {
    "EXCLUSIVE", "SPECIAL REPORT", "BONUS ISSUE", "DOUBLE ISSUE", "NEW!", "FIRST LOOK", "HOT!", "MUST READ",
    "ALL-ACCESS", "SNEAK PEEK", "POWER RANKINGS", "INSIDE!"};
const char* const* mag_heads(int tmpl, int* n) {
  switch (tmpl) {
    case kMagSpecial: *n = static_cast<int>(sizeof(kMagHeadSpecial) / sizeof(kMagHeadSpecial[0])); return kMagHeadSpecial;
    case kMagGold: *n = static_cast<int>(sizeof(kMagHeadGold) / sizeof(kMagHeadGold[0])); return kMagHeadGold;
    case kMagMoody: *n = static_cast<int>(sizeof(kMagHeadMoody) / sizeof(kMagHeadMoody[0])); return kMagHeadMoody;
    case kMagFaceoff: *n = static_cast<int>(sizeof(kMagHeadFaceoff) / sizeof(kMagHeadFaceoff[0])); return kMagHeadFaceoff;
    case kMagBracket: *n = static_cast<int>(sizeof(kMagHeadBracket) / sizeof(kMagHeadBracket[0])); return kMagHeadBracket;
    case kMagStreak: *n = static_cast<int>(sizeof(kMagHeadStreak) / sizeof(kMagHeadStreak[0])); return kMagHeadStreak;
    case kMagPreview: *n = static_cast<int>(sizeof(kMagHeadPreview) / sizeof(kMagHeadPreview[0])); return kMagHeadPreview;
    default: *n = static_cast<int>(sizeof(kMagHeadClassic) / sizeof(kMagHeadClassic[0])); return kMagHeadClassic;
  }
}
uint32_t mag_rand(uint32_t& s) {  // xorshift32
  s ^= s << 13;
  s ^= s >> 17;
  s ^= s << 5;
  return s;
}
uint32_t mag_hash(const std::string& t) {  // FNV-1a
  uint32_t h = 2166136261u;
  for (unsigned char c : t) h = (h ^ c) * 16777619u;
  return h != 0 ? h : 1u;
}
std::string mag_fill(std::string t, const std::string& name, const std::string& mascot, const std::string& place) {
  const std::pair<const char*, const std::string*> subs[3] = {{"{N}", &name}, {"{M}", &mascot}, {"{P}", &place}};
  for (const auto& sb : subs)
    for (size_t at = t.find(sb.first); at != std::string::npos; at = t.find(sb.first, at + sb.second->size()))
      t.replace(at, 3, *sb.second);
  return t;
}
// A club's record and place in its conference (rank 0: not found).
void mag_record(const PortFranchiseView& v, int club, int* wins, int* losses, int* rank) {
  *wins = *losses = *rank = 0;
  for (int c = 0; c < 2; ++c)
    for (int i = 0; i < 16; ++i)
      if (v.conference[c][i].club == club) {
        *wins = v.conference[c][i].wins;
        *losses = v.conference[c][i].losses;
        *rank = i + 1;
      }
}
// A club's last game (the issue's week): its goals, the other's, the other club; false none.
bool mag_last_game(const PortFranchiseView& v, int club, int* mine, int* theirs, int* opp, bool* ot) {
  for (int i = 0; i < v.playedCount && i < 16; ++i) {
    const PortFranchiseView::Played& p = v.played[i];
    if (p.home != club && p.away != club) continue;
    const bool home = p.home == club;
    *mine = home ? p.homeGoals : p.awayGoals;
    *theirs = home ? p.awayGoals : p.homeGoals;
    *opp = home ? p.away : p.home;
    *ot = p.overtime;
    return true;
  }
  return false;
}
// A cover star's name from its portrait key (the roster's name for a guest; else the key's words).
std::string mag_name_of(const std::string& key) {
  const int idx = PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, key.c_str());
  const char* n = idx >= 0 ? PortGuestRosterName(idx) : nullptr;
  if (n != nullptr && n[0] != '\0') return fr_upper(n);
  std::string k = key;
  for (const char* pre : {"strikers_", "pk_", "mua_"})
    if (k.starts_with(pre)) k = k.substr(std::strlen(pre));
  if (k == "donkeykong") k = "donkey kong";
  for (char& c : k)
    if (c == '_') c = ' ';
  return fr_upper(k);
}
std::string mag_history_path(int slot) {
  const char* home = std::getenv("STRIKERS_HOME");
  return std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/franchise/magazine_" +
         std::to_string(slot) + ".txt";
}
std::vector<MagPick> mag_history_load(int slot) {
  std::vector<MagPick> out;
  FILE* f = std::fopen(mag_history_path(slot).c_str(), "r");
  if (f == nullptr) return out;
  char line[512];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    if (line[0] == '#') continue;
    std::vector<std::string> col;
    std::string cur;
    for (const char* c = line; *c != '\0' && *c != '\n' && *c != '\r'; ++c) {
      if (*c == '\t') {
        col.push_back(cur);
        cur.clear();
      } else {
        cur += *c;
      }
    }
    col.push_back(cur);
    if (col.size() < 12) continue;
    MagPick p;
    p.valid = true;
    p.season = std::atoi(col[0].c_str());
    p.week = std::atoi(col[1].c_str());
    p.tmpl = std::max(0, std::min(7, std::atoi(col[2].c_str())));
    p.head = std::max(0, std::atoi(col[3].c_str()));
    p.variant = std::max(0, std::atoi(col[4].c_str()));
    p.club = std::atoi(col[5].c_str());
    p.rivalClub = std::atoi(col[6].c_str());
    p.seed = static_cast<uint32_t>(std::strtoul(col[7].c_str(), nullptr, 10));
    p.key = col[8];
    p.name = col[9];
    p.rival = col[10];
    p.rivalName = col[11];
    out.push_back(p);
  }
  std::fclose(f);
  return out;
}
void mag_history_save(int slot, const std::vector<MagPick>& h) {
  FILE* f = std::fopen(mag_history_path(slot).c_str(), "w");
  if (f == nullptr) return;
  std::fprintf(f, "# STRIKERS ILLUSTRATED covers (r131): season week template headline variant club rivalclub seed star name rival rivalname\n");
  for (const MagPick& p : h)
    std::fprintf(f, "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%u\t%s\t%s\t%s\t%s\n", p.season, p.week, p.tmpl, p.head, p.variant, p.club,
                 p.rivalClub, p.seed, p.key.c_str(), p.name.c_str(), p.rival.c_str(), p.rivalName.c_str());
  std::fclose(f);
}
// This week's cover (game thread, from fr_refresh): the one already drawn for it, else a new one at random.
void mag_pick_issue(FrSnapshot& s) {
  const PortFranchiseView& v = s.view;
  if (v.evolutionPending) return;  // the week's evolution first (its SPECIAL cover)
  if (v.slot != g.magHistSlot) {
    g.magHistory = mag_history_load(v.slot);
    g.magHistSlot = v.slot;
  }
  if (!g.magHistory.empty()) {  // a new career in this slot: its file starts again
    const MagPick& last = g.magHistory.back();
    if (last.season > v.season || (last.season == v.season && last.week > v.lastWeek)) {
      g.magHistory.clear();
      mag_history_save(v.slot, g.magHistory);
    }
  }
  for (const MagPick& p : g.magHistory)
    if (p.season == v.season && p.week == v.lastWeek) {
      s.pick = p;
      return;
    }
  const MagStory real = mag_story_real(s);
  const int nClubs = static_cast<int>(s.clubs.size());
  const int me = v.myClub;
  // who could be on it, and how likely
  struct Cand {
    std::string key, name;
    int club;
    float w;
  };
  std::vector<Cand> cands;
  const char* home = std::getenv("STRIKERS_HOME");
  auto has_picture = [&](const std::string& key) {  // (its file there: not loaded here, on the game thread)
    if (home == nullptr) return false;
    FILE* f = std::fopen((std::string(home) + "/guests/portraits/" + key + ".png").c_str(), "rb");
    if (f != nullptr) std::fclose(f);
    return f != nullptr;
  };
  auto add = [&](const std::string& key, const std::string& name, int club, float w) {
    if (key.empty() || !has_picture(key)) return;  // only someone with a picture
    for (Cand& c : cands)
      if (c.key == key) {
        c.w += w;
        return;
      }
    cands.push_back({key, name.empty() ? mag_name_of(key) : name, club, w});
  };
  for (int c = 0; c < nClubs && c < PORT_FR_CLUBS; ++c) add(v.clubCaptainKey[c], "", c, c == me ? 2.0f : 1.0f);
  for (const PortFranchiseEvoRow& r : s.evoWatch) add(r.key, fr_upper(r.name), r.club, 1.0f);
  for (int i = 0; i < v.leaderCount && i < 10; ++i) add(v.leaders[i].key, fr_upper(v.leaders[i].name), v.leaders[i].club, 1.5f);
  for (int i = 0; i < v.nemCount && i < 8; ++i) add(v.nems[i].key, fr_upper(v.nems[i].name), v.nems[i].club, 1.0f);
  add(v.captainKey, fr_upper(v.captain), me, 1.0f);
  add(v.backupKey, fr_upper(v.backup), me, 1.0f);
  add(v.kindKey, fr_upper(v.kind), me, 0.5f);
  add(real.star, real.starName, real.starClub, real.tmpl == kMagSpecial ? 10.0f : 4.0f);  // the week's real story
  if (!real.rival.empty()) add(real.rival, real.rivalName, real.rivalClub, 2.0f);
  if (cands.empty()) return;
  // the templates that fit this week
  const bool champs = v.phase == PORT_FR_PHASE_DONE && v.champion == me;
  float tw[8] = {3.0f, 0.0f, 0.0f, 2.0f, 2.0f, 0.0f, 2.0f, 1.5f};
  if (champs) tw[kMagGold] = 8.0f;
  if (v.phase == PORT_FR_PHASE_PLAYOFFS) tw[kMagBracket] = 3.0f;
  if (real.tmpl == kMagSpecial) tw[kMagSpecial] = 10.0f;
  if (v.lastWeek == 0) tw[kMagPreview] = 6.0f;
  std::string seedText = std::to_string(v.season) + "/" + std::to_string(v.lastWeek) + "/" + std::to_string(me) + "/" +
                         std::to_string(v.slot);
  for (int i = 0; i < v.playedCount && i < 16; ++i)
    seedText += "/" + std::to_string(v.played[i].home) + ":" + std::to_string(v.played[i].homeGoals) + "-" +
                std::to_string(v.played[i].away) + ":" + std::to_string(v.played[i].awayGoals);
  uint32_t rng = mag_hash(seedText) ^ (static_cast<uint32_t>(g.magHistory.size()) * 2654435761u);
  if (rng == 0) rng = 1;
  for (int k = 0; k < 4; ++k) mag_rand(rng);
  auto pick_weighted = [&](const float* w, int n) {
    float total = 0.0f;
    for (int i = 0; i < n; ++i) total += w[i];
    float r = static_cast<float>(mag_rand(rng) % 100000u) / 100000.0f * total;
    for (int i = 0; i < n; ++i) {
      if (r < w[i]) return i;
      r -= w[i];
    }
    return n - 1;
  };
  std::vector<float> cw;
  for (const Cand& c : cands) cw.push_back(c.w);
  const MagPick* prev = g.magHistory.empty() ? nullptr : &g.magHistory.back();
  MagPick p;
  for (int attempt = 0; attempt < 400; ++attempt) {
    p = MagPick();
    p.valid = true;
    p.season = v.season;
    p.week = v.lastWeek;
    p.tmpl = pick_weighted(tw, 8);
    int ci = pick_weighted(cw.data(), static_cast<int>(cw.size()));
    if (p.tmpl == kMagSpecial) {  // the one who evolved
      for (size_t i = 0; i < cands.size(); ++i)
        if (cands[i].key == real.star) ci = static_cast<int>(i);
    } else if (p.tmpl == kMagGold && cands[ci].club != me) {  // one of the champions
      std::vector<int> mine;
      for (size_t i = 0; i < cands.size(); ++i)
        if (cands[i].club == me) mine.push_back(static_cast<int>(i));
      if (!mine.empty()) ci = mine[mag_rand(rng) % mine.size()];
    }
    const Cand& c = cands[ci];
    p.key = c.key;
    p.name = c.name;
    p.club = c.club;
    int n = 0;
    const char* const* heads = mag_heads(p.tmpl, &n);
    p.head = static_cast<int>(mag_rand(rng) % static_cast<uint32_t>(n));
    p.variant = static_cast<int>(mag_rand(rng) % 4u);
    p.seed = mag_rand(rng);
    if (p.seed == 0) p.seed = 1;
    {
      // a down headline only for a club that lost its last game or is losing; STREAK only for one that won or is winning
      int w = 0, l = 0, rank = 0, a = 0, b = 0, o = -1;
      bool ot = false;
      mag_record(v, p.club, &w, &l, &rank);
      const bool playedIt = mag_last_game(v, p.club, &a, &b, &o, &ot);
      if (heads[p.head][0] == '-' && !(playedIt && a < b) && w >= l) continue;
      if (p.tmpl == kMagStreak && !(playedIt && a > b) && w <= l) continue;
    }
    if (p.tmpl == kMagFaceoff) {  // a rival: your nemesis (yours), its last opponent's captain, or any other captain
      if (p.club == me && !real.rival.empty() && real.rival != p.key) {
        p.rival = real.rival;
        p.rivalName = real.rivalName;
        p.rivalClub = real.rivalClub;
      } else {
        int a = 0, b = 0, o = -1;
        bool ot = false;
        int rc = mag_last_game(v, p.club, &a, &b, &o, &ot) ? o : static_cast<int>(mag_rand(rng) % static_cast<uint32_t>(std::max(1, nClubs)));
        if (rc == p.club) rc = (rc + 1) % std::max(1, nClubs);
        if (rc >= 0 && rc < nClubs) {
          p.rival = v.clubCaptainKey[rc];
          p.rivalName = mag_name_of(p.rival);
          p.rivalClub = rc;
        }
      }
      if (p.rival.empty() || p.rival == p.key) continue;
    }
    if (attempt >= 380) break;  // (nothing new left: take this one)
    bool seen = false;
    for (const MagPick& h : g.magHistory)
      if (h.key == p.key && h.tmpl == p.tmpl && h.head == p.head) seen = true;
    if (seen) continue;
    if (prev != nullptr && prev->key == p.key) continue;
    if (prev != nullptr && prev->tmpl == p.tmpl && p.tmpl != kMagGold && p.tmpl != kMagSpecial) continue;
    break;
  }
  g.magHistory.push_back(p);
  mag_history_save(v.slot, g.magHistory);
  s.pick = p;
  int n = 0;
  const char* const* heads = mag_heads(p.tmpl, &n);
  XLOG("franchise: STRIKERS ILLUSTRATED season %d week %d: %s cover, %s, \"%s\" (variant %d, %d covers in this career)",
       p.season, p.week, kMagTemplates[p.tmpl], p.name.c_str(), heads[p.head], p.variant, static_cast<int>(g.magHistory.size()));
}
// The issue: the true story of the week (mag_story_real), then this issue's random cover on it: its template, star,
// headline from the pool, kicker, sash, variant; the subline a true fact about that star (its club's last game, its
// goals, its record); four cover lines drawn at random from the league.
MagStory mag_story(const FrSnapshot& s) {
  MagStory st = mag_story_real(s);
  const MagPick& p = s.pick;
  const PortFranchiseView& v = s.view;
  if (!p.valid || p.season != v.season || p.week != v.lastWeek) return st;
  const int nClubs = static_cast<int>(s.clubs.size());
  auto place = [&](int c) { return c >= 0 && c < nClubs ? fr_upper(s.clubs[c].place) : std::string("?"); };
  auto mascot = [&](int c) { return c >= 0 && c < nClubs ? fr_upper(club_mascot(s.clubs[c])) : std::string("?"); };
  const bool storyStar = p.key == st.star;
  const int realTmpl = st.tmpl;
  const std::string realSub = st.sub, realKicker = st.kicker, realBanner = st.banner;
  st.tmpl = p.tmpl;
  st.variant = p.variant;
  st.star = p.key;
  st.starName = p.name;
  st.starClub = p.club;
  if (p.club >= 0 && p.club < nClubs) {
    st.c1 = s.clubs[p.club].c1;
    st.c2 = s.clubs[p.club].c2;
  }
  if (p.tmpl == kMagFaceoff) {
    st.rival = p.rival;
    st.rivalName = p.rivalName;
    st.rivalClub = p.rivalClub;
  }
  uint32_t rng = p.seed;
  const std::string M = mascot(p.club), P = place(p.club);
  int nh = 0;
  const char* const* heads = mag_heads(p.tmpl, &nh);
  std::string head = heads[std::min(p.head, nh - 1)];
  if (!head.empty() && head[0] == '-') head = head.substr(1);
  if (p.tmpl == kMagSpecial && p.head == 0 && realKicker == "A NEW LOOK") head = "SUITED UP!";
  head = mag_fill(head, p.name, M, P);
  const size_t bar = head.find('|');
  if (bar == std::string::npos) {
    st.head1 = head;
    st.head2.clear();
  } else if (p.tmpl == kMagMoody || p.tmpl == kMagPreview) {
    st.head1 = head.substr(0, bar);
    st.head2 = head.substr(bar + 1);
  } else {
    st.head1 = head.substr(0, bar) + " " + head.substr(bar + 1);
    st.head2.clear();
  }
  if (p.tmpl == kMagPreview && st.head2.empty()) {  // one-line pool entries split for its two lines
    const size_t sp = st.head1.rfind(' ');
    st.head2 = sp != std::string::npos ? st.head1.substr(sp + 1) : st.head1;
    st.head1 = sp != std::string::npos ? st.head1.substr(0, sp) : std::string();
  }
  // true facts about the star
  int w = 0, l = 0, rank = 0, a = 0, b = 0, opp = -1;
  bool ot = false;
  mag_record(v, p.club, &w, &l, &rank);
  const bool played = mag_last_game(v, p.club, &a, &b, &opp, &ot);
  st.win = played && a > b;  // r143: this issue's star's club
  std::string fact;
  if (played) {
    fact = M + (a > b ? (ot ? " STUN " : (a - b >= 4 ? " CRUSH " : (a - b >= 2 ? " BEAT " : " EDGE ")))
                      : (ot ? " FALL IN OT TO " : (b - a >= 4 ? " CRUSHED BY " : " FALL TO "))) +
           place(opp) + " " + std::to_string(a) + "-" + std::to_string(b);
  } else if (rank > 0) {
    fact = P + " ARE " + std::to_string(w) + "-" + std::to_string(l) + ", " + mag_ordinal(rank) + " IN THEIR CONFERENCE";
  }
  for (int i = 0; i < v.leaderCount && i < 10; ++i)
    if (p.key == v.leaders[i].key && (!played || (mag_rand(rng) & 1u) != 0))
      fact = p.name + ": " + std::to_string(v.leaders[i].goals) + " GOALS, " + mag_ordinal(i + 1) + " IN THE GOLDEN BOOT RACE";
  const bool keepStory = storyStar && p.tmpl == realTmpl;
  int nk = static_cast<int>(sizeof(kMagKickers) / sizeof(kMagKickers[0]));
  const std::string kick = mag_fill(kMagKickers[mag_rand(rng) % static_cast<uint32_t>(nk)], p.name, M, P);
  int ns = static_cast<int>(sizeof(kMagSashes) / sizeof(kMagSashes[0]));
  const std::string sash = kMagSashes[mag_rand(rng) % static_cast<uint32_t>(ns)];
  switch (p.tmpl) {
    case kMagSpecial:  // the evolution's own words
      st.kicker = realKicker;
      st.sub = realSub;
      st.banner = "COLLECTOR'S EDITION";
      break;
    case kMagGold:
      st.kicker = realKicker;
      st.sub = "THE " + mascot(v.myClub) + " WIN IT ALL";
      st.banner = (mag_rand(rng) & 1u) ? "SOUVENIR ISSUE" : "CHAMPIONS EDITION";
      break;
    case kMagBracket:
      st.kicker = realKicker;
      st.sub = p.club == v.myClub ? realSub : (fact.empty() ? realSub : fact);
      st.banner = (mag_rand(rng) & 1u) ? "PLAYOFF PREVIEW" : "POSTSEASON SPECIAL";
      break;
    case kMagFaceoff: {
      st.kicker = kick;
      int rw = 0, rl = 0, rr = 0;
      mag_record(v, p.rivalClub, &rw, &rl, &rr);
      st.sub = (p.club == v.myClub && p.rival == s.view.nems[0].key && s.view.nemCount > 0)
                   ? "BEAT YOU " + std::to_string(s.view.nems[0].beatYou) + "  -  LOST TO YOU " + std::to_string(s.view.nems[0].lostToYou)
                   : P + " " + std::to_string(w) + "-" + std::to_string(l) + "  VS  " + place(p.rivalClub) + " " +
                         std::to_string(rw) + "-" + std::to_string(rl);
      st.banner = sash;
      break;
    }
    case kMagStreak: {
      st.kicker = kick;
      st.sub = fact;
      // the number: a real win streak (the week's news), else the club's wins
      int streak = 0;
      for (int i = 0; i < v.newsCount && i < 8; ++i) {
        const std::string line = v.news[i];
        if (line.starts_with(P + " HAVE WON ") && line.ends_with(" IN A ROW")) streak = std::atoi(line.c_str() + P.size() + 10);
      }
      st.big = std::to_string(streak > 0 ? streak : w);
      st.bigWord = streak > 0 ? "STRAIGHT" : (w == 1 ? "WIN" : "WINS");
      st.banner = sash;
      break;
    }
    default:
      st.kicker = keepStory ? realKicker : kick;
      st.sub = keepStory ? realSub : fact;
      st.banner = sash;
      break;
  }
  if (st.sub.empty()) st.sub = realSub;
  // the cover lines: four true things from around the league, at random
  std::vector<std::pair<std::string, std::string>> pool;
  for (int i = 0; i < v.leaderCount && i < 3; ++i)
    if (v.leaders[i].key != p.key)
      pool.push_back({i == 0 ? "GOLDEN BOOT" : "SCORING RACE",
                      fr_upper(v.leaders[i].name) + ", " + std::to_string(v.leaders[i].goals) + " GOALS"});
  for (int i = 0; i < v.nemCount && i < 2; ++i)
    if (v.nems[i].key != p.key) pool.push_back({"WANTED", fr_upper(v.nems[i].name) + " " + fr_upper(v.nems[i].title)});
  for (int k = 0; k < 2 && v.playedCount > 0; ++k) {
    const PortFranchiseView::Played& g2 = v.played[mag_rand(rng) % static_cast<uint32_t>(std::min(16, v.playedCount))];
    pool.push_back({k == 0 ? "FINAL" : "ALSO", place(g2.home) + " " + std::to_string(g2.homeGoals) + ", " + place(g2.away) + " " +
                                              std::to_string(g2.awayGoals) + (g2.overtime ? " (OT)" : "")});
  }
  for (int k = 0; k < 2; ++k) {
    const int c = static_cast<int>(mag_rand(rng) % 2u), i = static_cast<int>(mag_rand(rng) % 16u);
    const PortFranchiseRow& r = v.conference[c][i];
    if (r.club >= 0 && r.club < nClubs)
      pool.push_back({i < 3 ? "CONTENDERS" : (i > 11 ? "FALLING" : "THE TABLE"),
                      place(r.club) + " " + std::to_string(r.wins) + "-" + std::to_string(r.losses) + ", " + mag_ordinal(i + 1)});
  }
  for (const PortFranchiseEvoRow& r : s.evoWatch)
    if (r.evoLevel > 0 && r.key != p.key && (mag_rand(rng) % 4u) == 0) {
      pool.push_back({"RISING", fr_upper(r.name) + " NEARS LV " + std::to_string(r.evoLevel)});
      break;
    }
  if (v.feedCount > 0) {
    const std::string f = v.feed[mag_rand(rng) % static_cast<uint32_t>(std::min(8, v.feedCount))];
    const size_t at = f.find(": ");
    if (f.starts_with("@") && at != std::string::npos && at > 0)
      pool.push_back({fr_upper(f.substr(0, at)), "\"" + fr_upper(f.substr(at + 2)) + "\""});
  }
  st.lines.clear();
  while (!pool.empty() && st.lines.size() < 4) {
    const size_t i = mag_rand(rng) % pool.size();
    st.lines.push_back(pool[i]);
    pool.erase(pool.begin() + static_cast<long>(i));
  }
  // r137: the forecast for your next game when the calendar has one (SNOW GAME, UNDER THE LIGHTS ...): the first cover
  // line, in the same four slots (no layout change; the dice above are untouched, so both co-op headsets still agree)
  if (s.nextWeather != 0 && v.nextOpponent >= 0 && v.nextOpponent < nClubs) {
    const std::string wl = PortWeatherLabel(s.nextWeather);
    if (!wl.empty()) {
      st.lines.insert(st.lines.begin(), {"FORECAST", wl + " VS " + place(v.nextOpponent)});
      if (st.lines.size() > 4) st.lines.pop_back();
    }
  }
  // r137: your latest big trading-card pull (a super rare or a shiny from the last week, xr_cards.cpp): a cover line, after
  // the forecast (the same four slots)
  if (!s.cardPull.empty()) {
    const size_t at = !st.lines.empty() && st.lines[0].first == "FORECAST" ? 1 : 0;
    st.lines.insert(st.lines.begin() + static_cast<long>(std::min(at, st.lines.size())), {"PULLED", s.cardPull});
    if (st.lines.size() > 4) st.lines.pop_back();
  }
  // CLASSIC's "PLUS:" picture: someone else
  if (st.inset == st.star) st.inset.clear();
  return st;
}
std::string mag_cover_name(const FrSnapshot& s) {
  const MagStory st = mag_story(s);
  return std::string(kMagTemplates[st.tmpl]) + ": " + st.head1 + (st.head2.empty() ? "" : " " + st.head2);
}

// --- the shared bits
uint32_t mag_darker(uint32_t c, float k) {
  const float r = static_cast<float>((c >> 16) & 255), gg = static_cast<float>((c >> 8) & 255), b = static_cast<float>(c & 255);
  return (static_cast<uint32_t>(r * k) << 16) | (static_cast<uint32_t>(gg * k) << 8) | static_cast<uint32_t>(b * k);
}
uint32_t mag_lighter(uint32_t c, float k) {
  const float r = static_cast<float>((c >> 16) & 255), gg = static_cast<float>((c >> 8) & 255), b = static_cast<float>(c & 255);
  return (static_cast<uint32_t>(r + (255.0f - r) * k) << 16) | (static_cast<uint32_t>(gg + (255.0f - gg) * k) << 8) |
         static_cast<uint32_t>(b + (255.0f - b) * k);
}
bool mag_has_sash(const MagStory& st) { return st.tmpl != kMagSpecial && st.tmpl != kMagMoody; }
// the name across the top, condensed (the star's head goes in front of it); left of the corner's sash
void mag_masthead(Pieces& P, const MagStory& st, float y, float px, uint32_t rgb, uint32_t rim, float outline,
                  uint32_t rgb2 = 0) {
  P.style(0.0f, 0.62f);
  P.rim(rim);
  // r135: 860 wide (was 890) beside the corner's wider band
  if (mag_has_sash(st)) P.text("STRIKERS ILLUSTRATED", 40.0f + 430.0f, y, px, 860, rgb, outline, rgb2);
  else P.text("STRIKERS ILLUSTRATED", kMagWf / 2, y, px, 1020, rgb, outline, rgb2);
  P.plain();
}
// r135: the date and the issue 40 px (were 26: a few pixels tall on the headset); the price only when it fits; an
// outline where it crosses a light picture (FACEOFF's white stripe)
void mag_dateline(Pieces& P, const MagStory& st, float y, uint32_t rgb, float outline = 0.0f) {
  P.plain();
  P.left(st.date, 44, y, 40, 360, rgb, outline);
  std::string issue = st.issue + " - $3.99";
  if (P.width(issue, 40) > 370) issue = st.issue;
  P.right(issue, mag_has_sash(st) ? kMagWf - 284 : kMagWf - 44, y, 40, 370, rgb, outline);
}
uint32_t mag_lcg(uint32_t r) { return (r * 1103515245u + 12345u) & 0x7FFFFFFFu; }
// r135: bars only (its 22 px digits were a blur), the bars the box's height
void mag_barcode(Pieces& P, float x0, float y0, const MagStory& st) {
  P.box(x0, y0, x0 + 190, y0 + 118, 4, 0xFFFFFF);
  uint32_t r = st.seed * 7919u + 17u;
  float x = x0 + 14.0f;
  while (x < x0 + 172) {
    r = mag_lcg(r);
    const float bw = static_cast<float>(2 + (r >> 8) % 4);
    if ((r >> 4) % 3 != 0) P.box(x, y0 + 12, x + bw, y0 + 106, 0, kMagInk);
    x += bw + 1 + static_cast<float>((r >> 12) % 3);
  }
}
// a band across the top right corner (r135: wider, its words 42 px on one line or 40 px on two, were 38 shrunk to
// 24-31 to fit). The band: (W - x) + y from 150 to 280; a line centred on its part of the band.
void mag_sash(Pieces& P, const std::string& text, uint32_t bg, uint32_t fg) {
  const float W = kMagWf;
  P.poly({W - 270, 0, W - 140, 0, W, 140, W, 270}, mag_darker(bg, 0.55f), 0.6f);
  P.poly({W - 280, 0, W - 150, 0, W, 150, W, 280}, bg);
  P.style(0.0f, 0.88f, 45.0f);
  std::string a = text, b;
  if (P.width(text, 42) > 244) mag_fit_two(P, text, 40, 0, 208, 284, &a, &b);
  if (b.empty()) {
    P.text(a, W - 107.5f, 107.5f, 42, 244, fg);
  } else {
    P.text(a, W - 94, 94, 40, 208, fg);
    P.text(b, W - 121, 121, 40, 284, fg);
  }
  P.plain();
}
// r135: the words 40 px (were 30) on up to two lines, split where they fit; the outline as thick as the label's
void mag_cover_lines(Pieces& P, const MagStory& st, float x, float y0, float step, int align, uint32_t label,
                     uint32_t detail, float outline, int count) {
  float y = y0;
  for (int i = 0; i < std::min(count, static_cast<int>(st.lines.size())); ++i) {
    std::string d1, d2;
    mag_fit_two(P, st.lines[i].second, 40, 420, 420, 420, &d1, &d2);
    if (align < 0) {
      P.left(st.lines[i].first, x, y, 46, 420, label, outline);
      P.left(d1, x, y + 48, 40, 420, detail, outline);
      if (!d2.empty()) P.left(d2, x, y + 90, 40, 420, detail, outline);
    } else {
      P.right(st.lines[i].first, x, y, 46, 420, label, outline);
      P.right(d1, x, y + 48, 40, 420, detail, outline);
      if (!d2.empty()) P.right(d2, x, y + 90, 40, 420, detail, outline);
    }
    y += step;
  }
}
void mag_confetti(Pieces& P, const MagStory& st, int count, const std::vector<uint32_t>& colours, float y0, float y1) {
  uint32_t r = st.seed * 31u + 5u;
  for (int i = 0; i < count; ++i) {
    r = mag_lcg(r);
    const float x = static_cast<float>((r >> 4) % 1080);
    r = mag_lcg(r);
    const float y = y0 + static_cast<float>((r >> 4) % static_cast<uint32_t>(y1 - y0));
    r = mag_lcg(r);
    const float a = static_cast<float>((r >> 4) % 628) / 100.0f;
    r = mag_lcg(r);
    const uint32_t c = colours[(r >> 4) % colours.size()];
    const float dx = 14 * std::cos(a), dy = 14 * std::sin(a), ex = -6 * std::sin(a), ey = 6 * std::cos(a);
    P.poly({x - dx - ex, y - dy - ey, x + dx - ex, y + dy - ey, x + dx + ex, y + dy + ey, x - dx + ex, y - dy + ey}, c);
  }
}
const GuestPortrait& mag_logo(const FrSnapshot& s, int club) {
  static const GuestPortrait none;
  return club >= 0 && club < static_cast<int>(s.clubs.size()) ? guest_portrait("club_" + s.clubs[club].id) : none;
}

// --- the covers
// r143 (Lance: "i should never see gameplay as a cover ... we need those to be generated, copying the famous sports
// illustrated covers"): CLASSIC's backdrop (it was the game's own frame after your goal, r113's photo), a night game's
// photo shoot drawn from nothing: a dark sky graded from a club colour, the floodlight banks (their lamps, halos and
// beams down through the haze), out-of-focus bokeh, the stands' crowd in silhouette with flashes going off, the grass
// under the lights with a spot where the star stands, the star's rim light in its club's two colours, and confetti in
// the air after a win. Seeded per issue (st.seed): the palette, how many banks and where, the beams, the bokeh, the
// crowd. PC check: tools\outside\magazine_preview.py stadium_night (the same draws, the same numbers).
void mag_stadium_night(Pieces& P, const MagStory& st, uint32_t c1, uint32_t c2) {
  const float W = kMagWf, H = kMagHf;
  uint32_t r = st.seed * 2654435761u + 977u;
  auto rnd = [&](uint32_t n) {  // 0 .. n-1
    r = mag_lcg(r);
    return static_cast<int>((r >> 4) % std::max(1u, n));
  };
  auto frand = [&]() {  // 0 .. 1
    r = mag_lcg(r);
    return static_cast<float>((r >> 4) % 10000u) / 10000.0f;
  };
  const int pal = rnd(4);
  const uint32_t sky[4] = {mag_darker(c1, 0.35f), 0x0B1430, 0x1A0B2E, mag_darker(c2, 0.3f)};
  P.box(0, 0, W, H, 0, sky[pal], 1.0f, 0x030409);
  P.glow(-200, -300, W + 200, 700, mag_lighter(c1, 0.2f), 0.25f);  // haze lit by the lamps
  const int banks = 2 + rnd(2);
  for (int b = 0; b < banks; ++b) {
    const int jx = rnd(200), jy = rnd(140);
    const float bx = banks == 2 ? (b == 0 ? 60.0f + jx : W - 60.0f - jx) : 120.0f + b * (W - 240.0f) / 2.0f + jx * 0.6f - 60.0f;
    const float by = 60.0f + jy;
    for (int k = 0; k < 2; ++k) {  // its beams, down toward the pitch
      const float tx = W * (0.2f + 0.6f * frand());
      P.poly({bx - 16, by, bx + 16, by, tx + 130, H * 0.74f, tx - 130, H * 0.74f}, 0xFFF4D8, 0.05f);
    }
    P.glow(bx - 280, by - 280, bx + 280, by + 280, 0xFFF1C8, 0.45f);
    for (int l = 0; l < 6; ++l) {  // its lamps, 3 x 2
      const float lx = bx - 45 + (l % 3) * 45.0f, ly = by - 18 + (l / 3) * 36.0f;
      P.glow(lx - 26, ly - 26, lx + 26, ly + 26, 0xFFFFFF, 0.95f);
    }
  }
  const int nb = 14 + rnd(10);
  for (int i = 0; i < nb; ++i) {  // bokeh: lights far behind, out of focus
    const float x = frand() * W, y = 40 + frand() * 640, d = 30 + frand() * 110, a = 0.06f + 0.1f * frand();
    const uint32_t col = i % 3 == 0 ? mag_lighter(c1, 0.5f) : (i % 3 == 1 ? 0xFFE9B0u : 0xFFFFFFu);
    P.box(x - d / 2, y - d / 2, x + d / 2, y + d / 2, d / 2, col, a);
  }
  // the stands: rows of heads and shoulders against a dark wall, flashes going off in them
  const float top = 780.0f + rnd(80);
  P.box(0, top + 40, W, 1010, 0, 0x05060A, 0.92f, 0x020304);
  for (int row = 0; row < 3; ++row) {
    const float yy = top + row * 34.0f;
    for (float x = -20.0f + rnd(30); x < W + 20;) {
      const float hr = 13.0f + rnd(6), hy = yy + rnd(10);
      P.box(x - hr, hy - hr, x + hr, hy + hr, hr, 0x07080C, 0.95f);
      P.box(x - hr * 1.7f, hy + hr * 0.6f, x + hr * 1.7f, hy + hr * 3.2f, hr, 0x07080C, 0.95f);
      x += 26.0f + rnd(14);
    }
  }
  for (int i = 0; i < 14; ++i) {
    const float x = frand() * W, y = top + 10 + frand() * 140;
    P.glow(x - 14, y - 14, x + 14, y + 14, 0xFFFFFF, 0.8f);
  }
  // the grass under the lights, a spot where the star stands; the star's rim light in its two colours
  P.box(0, 1000, W, H, 0, 0x1F6B2C, 1.0f, 0x061A0B);
  P.glow(W / 2 - 420, 1080, W / 2 + 420, 1330, 0xE8FFD8, 0.3f);
  P.glow(80, 220, 760, 1280, mag_lighter(c1, 0.45f), 0.55f);
  P.glow(320, 160, 1000, 1220, mag_lighter(c2, 0.35f), 0.45f);
  if (st.win) mag_confetti(P, st, 70, {st.c1, st.c2, 0xFFFFFF, kMagYellow}, 40, 980);
}
void mag_cover_classic(Pieces& P, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  const int va = st.variant & 3;  // r131: the random cover's variant
  const uint32_t c1 = va == 1 ? st.c2 : (va == 2 ? 0x3A1A6A : st.c1);
  const uint32_t mast[4] = {kMagRed, kMagYellow, 0xFFFFFF, kMagRed}, mastRim[4] = {0xFFFFFF, 0x05070C, kMagRed, kMagYellow};
  mag_stadium_night(P, st, c1, va == 1 ? st.c1 : st.c2);  // r143 (no game picture: generated)
  mag_masthead(P, st, 150, 210, mast[va], mastRim[va], 6);
  P.figure(mag_pic(P, st.star, false, 1024), 110, 120, W - 30, 1330, false, 0.5f, 1.0f, (va & 1) != 0);  // r137: big
  mag_dateline(P, st, 34, 0xFFFFFF);
  mag_cover_lines(P, st, 44, 330, 150, -1, kMagYellow, 0xFFFFFF, 4, 3);
  // PLUS: a small picture of the next story (your nemesis, or the golden boot's leader) at the right
  if (!st.inset.empty()) {
    P.box(W - 252, 286, W - 40, 498, 6, 0xFFFFFF);
    P.box(W - 244, 294, W - 48, 490, 4, mag_darker(st.c1, 0.4f), 1.0f, 0x05070C);
    P.figure(guest_portrait(st.inset), W - 240, 300, W - 52, 486, false, 0.5f, 1.0f);
    // r135: its label 40 px (was 30), a long name on two lines
    std::string a, b;
    mag_fit_two(P, st.insetText, 40, 196, 196, 196, &a, &b);
    P.box(W - 252, 498, W - 40, b.empty() ? 554 : 600, 0, kMagRed);
    P.text(a, W - 146, 526, 40, 196, 0xFFFFFF);
    if (!b.empty()) P.text(b, W - 146, 570, 40, 196, 0xFFFFFF);
  }
  // the headline: a red kicker tab, the words huge and leaning, the story under them (r135: the kicker 44 px, was
  // 38; the story 44 px on up to two lines, was 40 shrunk to fit one)
  P.plain();
  const float kw = P.width(st.kicker, 44) + 36;
  P.box(40, 998, 40 + std::min(1000.0f, kw), 1062, 6, kMagRed);
  P.left(st.kicker, 58, 1030, 44, 960, 0xFFFFFF);
  P.style(0.16f, 0.86f, 0.0f, 10, 0x000000, 0.6f);
  P.left(st.head1, 34, 1158, 220, 1012, 0xFFFFFF, 8, 0xFFE27A);
  P.plain();
  std::string s1, s2;
  mag_fit_two(P, st.sub, 44, 790, 790, 790, &s1, &s2);
  P.left(s1, 44, 1282, 44, 790, kMagYellow, 3);
  if (!s2.empty()) P.left(s2, 44, 1334, 44, 790, kMagYellow, 3);
  mag_sash(P, st.banner, va == 1 ? kMagRed : (va == 2 ? 0xFFFFFF : kMagYellow), va == 1 ? 0xFFFFFF : kMagInk);
  mag_barcode(P, W - 234, H - 152, st);
}
void mag_cover_special(Pieces& P, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  const uint32_t gold = 0xC9A227;
  const int va = st.variant & 3;  // r131
  const uint32_t aura[4] = {mag_lighter(st.c1, 0.2f), 0x5AC8FF, 0xFF6AD8, 0x6AFF9A};
  P.box(0, 0, W, H, 0, 0x050505);
  P.burst(W / 2 - 900, 600 - 900, W / 2 + 900, 600 + 900, 30, 0.8f, 0.0f, 0x1C1A14);
  P.glow(W / 2 - 560, 180, W / 2 + 560, 1180, aura[va], 0.8f);
  P.glow(W / 2 - 330, 360, W / 2 + 330, 1000, 0xFFF4C8, 0.55f);
  P.box(0, 0, W, 72, 0, kMagRed);
  P.text("SPECIAL " + st.banner, W / 2, 37, 44, 1000, 0xFFFFFF);  // r135: 44 px (was 36)
  mag_masthead(P, st, 176, 200, 0xFFF1B0, 0x2A1A00, 4, 0xC9A227);
  P.glow(16, 760, 356, 1160, 0x8A8A8A, 0.35f);
  P.figure(mag_pic(P, st.before, false, 512), 36, 820, 336, 1110, false, 0.5f, 1.0f, false, 0x4A4A4A, 0x4A4A4A);  // r137
  P.text("BEFORE", 186, 1138, 40, 280, 0xA8A8A8);  // r135: 40 px (was 30), a touch brighter
  P.figure(mag_pic(P, st.star, st.shiny, 1024), 170, 250, W - 40, 1150, false, 0.5f, 1.0f);  // r137: big (a shiny one shiny)
  P.ring(22, 22, W - 22, H - 22, 0, 4, gold);
  if (st.shiny) {
    P.burst(W - 360, 290, W - 70, 580, 16, 0.62f, 10.0f, kMagYellow);
    P.style(0.0f, 1.0f, -12.0f);
    P.text("SHINY!", W - 215, 435, 66, 230, kMagRed, 4);
    P.plain();
  }
  P.text(st.kicker, W / 2, 1172, 44, 900, gold);  // r135: 44 px (was 40)
  P.style(0.12f, 0.88f, 0.0f, 0.0f);
  P.rim(0x2A1A00);
  P.text(st.head1, W / 2, 1270, 200, 1000, 0xFFF1B0, 6, 0xC98A1A);
  P.plain();
  P.text(st.sub, W / 2, 1376, 44, 960, 0xFFFFFF);  // r135: 44 px (was 40)
  mag_cover_lines(P, st, 52, 330, 150, -1, gold, 0xFFFFFF, 3, 3);
}
void mag_cover_gold(Pieces& P, const FrSnapshot& s, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  P.box(0, 0, W, H, 0, 0xFFE27A, 1.0f, 0xC8901C);
  P.burst(W / 2 - 1000, 640 - 1000, W / 2 + 1000, 640 + 1000, 36, 0.84f, 0.0f, 0xFFF3C0, 0.5f);
  P.glow(W / 2 - 600, 160, W / 2 + 600, 1200, 0xFFFFFF, 0.75f);
  mag_confetti(P, st, 90, {st.c1, st.c2, 0xFFFFFF, kMagRed, 0x1E5AC8}, 60, 1100);
  mag_masthead(P, st, 150, 210, kMagRed, 0xFFFFFF, 7);
  P.image(mag_logo(s, s.view.myClub), W / 2 - 400, 260, W / 2 + 400, 1060, false, 0.3f);
  P.figure(mag_pic(P, st.star, false, 1024), 130, 250, W - 130, 1130, false, 0.5f, 1.0f);  // r137: big
  mag_dateline(P, st, 34, 0x3A1A00);
  P.box(W / 2 - 330, 1090, W / 2 + 330, 1148, 29, 0x3A1A00);
  P.text(st.kicker, W / 2, 1119, 42, 620, kMagYellow);  // r135: 42 px (was 34)
  P.style(0.1f, 0.84f, 0.0f, 12, 0x5A2A00, 0.45f);
  P.rim(0xFFFFFF);
  P.text(st.head1, W / 2, 1236, 210, 1030, kMagRed, 9, 0x8E1010);
  P.plain();
  P.text(st.sub, W / 2 - 110, 1350, 46, 800, 0x3A1A00);
  mag_sash(P, st.banner, kMagRed, 0xFFFFFF);
  mag_barcode(P, W - 234, H - 152, st);
}
void mag_cover_moody(Pieces& P, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  const int va = st.variant & 3;  // r131: black and white, sepia, cold blue, blood red
  const uint32_t light[4] = {0xF4F4F4, 0xF2DCB4, 0xC8DCFF, 0xFFC8C0};
  P.box(0, 0, W, H, 0, 0x1A1B20, 1.0f, 0x050506);
  P.glow(W / 2 - 650, 40, W / 2 + 650, 1200, 0x7A7E88, 0.5f);
  mag_masthead(P, st, 150, 210, va == 3 ? kMagRed : 0xFFFFFF, 0x000000, 0);
  P.figure(mag_pic(P, st.star, false, 1024), -140, 170, W + 140, 1500, false, 0.5f, 1.0f, (va & 1) != 0, 0x0A0A0C,
           light[va]);  // r137: big
  P.dots(0, 170, W, H, 9, 0.5f, 0.05f, 0x000000, 0.28f);
  P.box(0, 900, W, H, 0, 0x000001, 0.0f, 0, 0.94f);
  mag_dateline(P, st, 34, 0xB8BCC6);
  // r135: the tag, the kicker and the story 40 / 44 / 44 px (were 30 / 40 / 36), the cover lines 150 apart (140)
  P.box(40, 232, 40 + P.width(st.banner, 40) + 30, 286, 0, kMagRed);
  P.left(st.banner, 55, 259, 40, 600, 0xFFFFFF);
  P.text(st.kicker, W / 2, 1030, 44, 960, kMagRed);
  P.style(0.0f, 0.8f);
  if (!st.head2.empty()) {
    P.text(st.head1, W / 2, 1140, 150, 1030, 0xFFFFFF);
    P.text(st.head2, W / 2, 1282, 170, 1030, 0xFFFFFF);
  } else {
    P.text(st.head1, W / 2, 1210, 170, 1030, 0xFFFFFF);
  }
  P.plain();
  P.text(st.sub, W / 2, 1386, 44, 960, 0xC8CCD6);
  mag_cover_lines(P, st, W - 44, 330, 150, 1, 0xFFFFFF, 0xD8DCE6, 3, 3);
}
void mag_cover_faceoff(Pieces& P, const FrSnapshot& s, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  const uint32_t them = st.rivalClub >= 0 && st.rivalClub < static_cast<int>(s.clubs.size()) ? s.clubs[st.rivalClub].c1 : 0x6E1414;
  P.poly({0, 0, 610, 0, 470, H, 0, H}, mag_darker(st.c1, 0.6f));
  P.poly({610, 0, W, 0, W, H, 470, H}, mag_darker(them, 0.6f));
  P.glow(-200, 300, 700, 1300, mag_lighter(st.c1, 0.3f), 0.6f);
  P.glow(380, 300, 1280, 1300, mag_lighter(them, 0.3f), 0.6f);
  P.dots(0, 0, W, H, 12, 0.05f, 0.3f, 0x000000, 0.3f);
  P.box(0, 0, W, 300, 0, 0x000001, 0.85f, 0, 0.0f);
  mag_masthead(P, st, 140, 200, 0xFFFFFF, kMagRed, 5);
  const int va = st.variant & 3;  // r131
  const uint32_t vs[4] = {kMagYellow, 0xFFFFFF, 0xFF8A1E, 0x5AE8FF};
  P.figure(mag_pic(P, st.star, false, 1024), -60, 330, 610, 1190, false, 0.5f, 1.0f, (va & 1) != 0);  // r137: big
  P.figure(mag_pic(P, st.rival, false, 1024), 470, 330, W + 60, 1190, false, 0.5f, 1.0f, (va & 1) == 0);
  P.poly({592, 0, 628, 0, 488, H, 452, H}, 0xFFFFFF);
  P.text(st.kicker, W / 2, 272, 44, 900, kMagYellow, 4);
  P.burst(W / 2 - 170, 600, W / 2 + 170, 940, 14, 0.62f, 0.0f, vs[va]);
  P.style(0.12f);
  P.rim(0xFFFFFF);
  P.text("VS", W / 2, 770, 140, 260, kMagRed, 6);
  P.plain();
  P.box(0, 1000, W, H, 0, 0x000001, 0.0f, 0, 0.9f);
  P.text(st.starName, 250, 1206, 50, 440, 0xFFFFFF, 4);
  P.text(st.rivalName, 830, 1206, 50, 440, 0xFFFFFF, 4);
  P.style(0.16f, 0.84f, 0.0f, 10, 0x000000, 0.6f);
  P.text(st.head1, W / 2, 1306, 170, 1030, 0xFFFFFF, 7, 0xFF8A80);
  P.plain();
  P.text(st.sub, W / 2, 1400, 44, 980, 0xE6ECF5);  // r135: 44 px (was 32)
  mag_dateline(P, st, 34, 0xFFFFFF, 3);
  mag_sash(P, st.banner, kMagRed, 0xFFFFFF);
}
// one playoff series: two logos stacked, their wins; the one out faded; yours ringed gold; nil = not decided yet
void mag_bracket_slot(Pieces& P, const FrSnapshot& s, float x0, float y0, float w, const PortFranchiseSeries* sr) {
  const int me = s.view.myClub;
  P.box(x0, y0, x0 + w, y0 + 128, 12, 0x0A1430, 0.85f);
  if (sr == nullptr) {
    P.ring(x0, y0, x0 + w, y0 + 128, 12, 3, 0x3A5AA8);
    P.text("?", x0 + w / 2, y0 + 64, 60, w, 0x3A5AA8);
    return;
  }
  const bool mineHere = sr->hi == me || sr->lo == me;
  P.ring(x0, y0, x0 + w, y0 + 128, 12, mineHere ? 4.0f : 3.0f, mineHere ? kMagYellow : 0x3A5AA8);
  for (int k = 0; k < 2; ++k) {
    const int club = k == 0 ? sr->hi : sr->lo;
    const int wins = k == 0 ? sr->winsHi : sr->winsLo;
    const bool out = sr->winner >= 0 && sr->winner != club;
    const float yy = y0 + 8 + k * 58;
    P.image(mag_logo(s, club), x0 + 8, yy, x0 + 62, yy + 54, false, out ? 0.35f : 1.0f);
    P.text(std::to_string(wins), x0 + w - 28, yy + 28, 44, 50, out ? 0x6A7690 : 0xFFFFFF);
  }
}
void mag_cover_bracket(Pieces& P, const FrSnapshot& s, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  const PortFranchiseView& v = s.view;
  const int va = st.variant & 3;  // r131
  const uint32_t top[4] = {0x0B1B4A, 0x4A0B1B, 0x0B3A1B, 0x202020}, bottom[4] = {0x040818, 0x18040A, 0x041808, 0x050505};
  P.box(0, 0, W, H, 0, top[va], 1.0f, bottom[va]);
  P.dots(0, 0, W, H, 16, 0.06f, 0.28f, 0x2A4A9A, 0.55f);
  P.glow(W / 2 - 520, 280, W / 2 + 520, 1000, 0x2A5AD8, 0.45f);
  mag_masthead(P, st, 150, 210, kMagYellow, 0x05070C, 6);
  P.text(st.kicker, W / 2, 278, 44, 900, 0xFFFFFF, 3);
  // the bracket: each conference's rounds toward the middle, the final on top
  auto nth = [&](int round, int conf, int k) -> const PortFranchiseSeries* {
    int seen = 0;
    for (int i = 0; i < v.seriesCount && i < 15; ++i)
      if (v.series[i].round == round && v.series[i].conf == conf && seen++ == k) return &v.series[i];
    return nullptr;
  };
  static const float kColX[3] = {30, 196, 362};
  static const float kColY[3][4] = {{330, 480, 630, 780}, {405, 705, 0, 0}, {555, 0, 0, 0}};
  static const int kColN[3] = {4, 2, 1};
  for (int conf = 0; conf < 2; ++conf)
    for (int c = 0; c < 3; ++c)
      for (int k = 0; k < kColN[c]; ++k)
        mag_bracket_slot(P, s, conf == 0 ? kColX[c] : W - kColX[c] - 150, kColY[c][k], 150, nth(c + 1, conf, k));
  P.text("THE FINAL", W / 2, 350, 40, 220, kMagYellow);  // r135: 40 px (was 30)
  mag_bracket_slot(P, s, W / 2 - 85, 374, 170, nth(4, -1, 0));
  P.figure(mag_pic(P, st.star, false, 512), 300, 640, 780, 1190, false, 0.5f, 1.0f, (va & 1) != 0);  // r137: big
  P.box(0, 1020, W, H, 0, 0x000001, 0.0f, 0, 0.9f);
  P.style(0.16f, 0.84f, 0.0f, 10, 0x000000, 0.6f);
  P.text(st.head1, W / 2, 1250, 170, 1030, 0xFFFFFF, 7, kMagYellow);
  P.plain();
  P.text(st.sub, W / 2, 1354, 44, 980, kMagYellow, 2);  // r135: 44 px (was 38)
  mag_dateline(P, st, 34, 0xC8D2E6);
  mag_sash(P, st.banner, va == 1 ? 0xFFFFFF : kMagYellow, kMagInk);
}
void mag_cover_streak(Pieces& P, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  const int va = st.variant & 3;  // r131: electric blue, fire, poison green, black and gold
  const uint32_t top[4] = {0x1440C8, 0xD8301E, 0x0FA36A, 0x222222}, bottom[4] = {0x2A0A6A, 0x5A0A10, 0x063A3A, 0x000001};
  const uint32_t halo[4] = {0x6AD8FF, 0xFFB84A, 0xB8FF6A, 0xFFD23F};
  P.box(0, 0, W, H, 0, top[va], 1.0f, bottom[va]);
  for (int i = 0; i < 7; ++i) {
    const float x = 140.0f + i * 190.0f;
    P.poly({x, 0, x + 90, 0, x - 410, H, x - 500, H}, 0xFFFFFF, 0.07f + 0.02f * (i % 3));
  }
  P.glow(80, 300, 900, 1240, halo[va], 0.5f);
  mag_masthead(P, st, 150, 210, va == 1 ? 0xFFFFFF : 0xFFE600, 0x05070C, 7);
  P.figure(mag_pic(P, st.star, false, 1024), 40, 230, 860, 1240, false, 0.5f, 1.0f, (va & 1) != 0);  // r137: big
  P.burst(W - 450, 310, W - 30, 730, 18, 0.7f, 8.0f, 0xFFE600);
  P.style(0.1f);
  P.rim(0xFFFFFF);
  P.text(st.big, W - 240, 490, 200, 300, kMagRed, 6);
  P.plain();
  P.text(st.bigWord, W - 240, 618, 48, 280, kMagInk);
  P.box(0, 1060, W, H, 0, 0x000001, 0.0f, 0, 0.85f);
  P.text(st.kicker, W / 2, 1126, 44, 960, 0xFFE600, 3);  // r135: 44 px (was 42)
  P.style(0.2f, 0.82f, 0.0f, 10, 0x000000, 0.6f);
  P.text(st.head1, W / 2, 1250, 200, 1030, 0xFFFFFF, 8, 0x9AE8FF);
  P.plain();
  // r135: the story 44 px on up to two lines (was 38 shrunk to fit one)
  std::string s1, s2;
  mag_fit_two(P, st.sub, 44, 780, 780, 780, &s1, &s2);
  P.text(s1, W / 2 - 110, s2.empty() ? 1366 : 1362, 44, 780, 0xFFFFFF, 2);
  if (!s2.empty()) P.text(s2, W / 2 - 110, 1410, 44, 780, 0xFFFFFF, 2);
  mag_dateline(P, st, 34, 0xFFFFFF);
  mag_cover_lines(P, st, 44, 330, 150, -1, 0xFFE600, 0xFFFFFF, 4, 2);
  mag_barcode(P, W - 234, H - 152, st);
  mag_sash(P, st.banner, kMagRed, 0xFFFFFF);
}
void mag_cover_preview(Pieces& P, const FrSnapshot& s, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  const PortFranchiseView& v = s.view;
  const int va = st.variant & 3;  // r131: teal and orange, purple and lime, red and yellow, navy and pink
  const uint32_t top[4] = {0x12B5B0, 0x7A3AD8, 0xE8402A, 0x1E2A6E}, bottom[4] = {0x064C6E, 0x2A0A6A, 0x7A0A10, 0x05070C};
  const uint32_t band1[4] = {0xFF8A1E, 0xB8FF3A, 0xFFD23F, 0xFF5AB8}, band2[4] = {0xFFD23F, 0xFFFFFF, 0xFFFFFF, 0x5AE8FF};
  P.box(0, 0, W, H, 0, top[va], 1.0f, bottom[va]);
  P.style(0.0f, 0.7f);
  P.text(std::to_string(v.season), W / 2 + 120, 760, 1000, 1200, 0xFFFFFF);
  P.v.back().alpha = 0.12f;
  P.plain();
  P.poly({0, 1000, W, 820, W, 900, 0, 1080}, band1[va]);
  P.poly({0, 1090, W, 910, W, 940, 0, 1120}, band2[va]);
  mag_masthead(P, st, 150, 210, 0xFFFFFF, mag_darker(band1[va], 0.8f), 6);
  P.image(mag_logo(s, st.starClub >= 0 ? st.starClub : v.myClub), 40, 300, 330, 590, false);
  P.figure(mag_pic(P, st.star, false, 1024), 200, 230, W - 20, 1180, false, 0.5f, 1.0f, (va & 1) != 0);  // r137: big
  P.box(0, 1100, W, H, 0, 0x000001, 0.0f, 0, 0.75f);
  P.text(st.kicker, W / 2, 1150, 44, 900, 0xFFD23F, 3);  // r135: 44 px (was 40)
  P.style(0.1f, 0.84f);
  P.text(st.head1, W / 2, 1222, 76, 900, 0xFFFFFF, 4);
  P.style(0.14f, 0.82f, 0.0f, 10, 0x000000, 0.5f);
  P.text(st.head2, W / 2, 1318, 150, 1030, band1[va], 7, band2[va]);
  P.plain();
  P.text(st.sub, W / 2, 1404, 44, 980, 0xFFFFFF);  // r135: 44 px (was 32)
  mag_dateline(P, st, 34, 0xFFFFFF);
  mag_cover_lines(P, st, 44, 640, 150, -1, 0xFFD23F, 0xFFFFFF, 4, 2);
  mag_sash(P, st.banner, band1[va], va == 1 ? kMagInk : 0xFFFFFF);
}

// --- inside: magazine pages (a red section tab at the outer corner, the title condensed, the fold's shadow on the
// inner edge, the page number and the running foot)
void mag_inside_frame(Pieces& P, const MagStory& st, int page, const std::string& section, const std::string& title,
                      const std::string& sub) {
  const float W = kMagWf, H = kMagHf;
  const bool leftPage = page % 2 == 1;
  P.box(0, 0, W, H, 0, 0xFCFBF8, 1.0f, 0xEFEDE6);
  for (int i = 0; i < 14; ++i) {
    const float x = leftPage ? W - 70 + i * 5.0f : 65 - i * 5.0f;
    P.box(x, 0, x + 5, H, 0, 0x000001, 0.012f * (i + 1));
  }
  // r135: the tab, the line under the title, the page number and the running foot 44 / 44 / 44 / 40 px (were 34 / 30
  // / 34 / 22: a few pixels tall on the headset), the greys darker
  const float outer = leftPage ? 44 : W - 44;
  const float tw = P.width(section, 44) + 40;
  if (leftPage) {
    P.box(outer, 36, outer + tw, 100, 0, kMagRed);
    P.left(section, outer + 20, 68, 44, 600, 0xFFFFFF);
  } else {
    P.box(outer - tw, 36, outer, 100, 0, kMagRed);
    P.right(section, outer - 20, 68, 44, 600, 0xFFFFFF);
  }
  P.box(44, 118, W - 44, 122, 0, kMagInk);
  P.style(0.0f, 0.8f);
  P.text(title, W / 2, 196, 116, 1000, kMagInk);
  P.plain();
  P.text(sub, W / 2, 278, 44, 1000, 0x4E4840);
  if (leftPage) {
    P.left(std::to_string(page + 1), 44, H - 44, 44, 100, kMagInk);
    P.right("STRIKERS ILLUSTRATED - " + st.date, W - 90, H - 44, 40, 800, 0x6A645A);
  } else {
    P.right(std::to_string(page + 1), W - 44, H - 44, 44, 100, kMagInk);
    P.left("STRIKERS ILLUSTRATED - " + st.date, 90, H - 44, 40, 800, 0x6A645A);
  }
}
void mag_page_scores(Pieces& P, const FrSnapshot& s, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  const PortFranchiseView& v = s.view;
  mag_inside_frame(P, st, 1, "SCORECARD", "AROUND THE LEAGUE", st.issue);
  const int n = std::min(16, v.playedCount);
  if (n == 0) P.text("NO GAMES PLAYED YET", W / 2, 760, 60, 1000, 0x6A645A);
  const int rows = std::max(1, (n + 1) / 2);
  const float th = std::min(250.0f, (H - 330.0f - 110.0f) / rows - 12.0f);
  for (int i = 0; i < n; ++i) {
    const PortFranchiseView::Played& g2 = v.played[i];
    const int c = i % 2, r = i / 2;
    const float x0 = 50.0f + c * 500, y0 = 330.0f + r * (th + 12);
    P.box(x0, y0, x0 + 480, y0 + th, 14, g2.mine ? 0xFFF1B0 : 0xFFFFFF);
    P.ring(x0, y0, x0 + 480, y0 + th, 14, g2.mine ? 4.0f : 2.0f, g2.mine ? kMagRed : 0xD6D0C4);
    const bool homeWon = g2.homeGoals > g2.awayGoals;
    P.image(mag_logo(s, g2.home), x0 + 10, y0 + 8, x0 + 10 + th - 16, y0 + th - 8, false, homeWon ? 1.0f : 0.5f);
    P.image(mag_logo(s, g2.away), x0 + 470 - (th - 16), y0 + 8, x0 + 470, y0 + th - 8, false, homeWon ? 0.5f : 1.0f);
    P.style(0.0f, 0.85f);
    P.text(std::to_string(g2.homeGoals) + "-" + std::to_string(g2.awayGoals), x0 + 240,
           y0 + (g2.overtime ? 0.42f : 0.47f) * th, 0.62f * th, 470 - 2 * th, kMagInk);
    P.plain();
    if (g2.overtime) P.text("OT", x0 + 240, y0 + th - 22, 40, 100, kMagRed);  // r135: 40 px (was 24)
  }
}
void mag_page_table(Pieces& P, const FrSnapshot& s, const MagStory& st) {
  const PortFranchiseView& v = s.view;
  const int nClubs = static_cast<int>(s.clubs.size());
  mag_inside_frame(P, st, 2, "THE TABLE", "THE STANDINGS", "GOLD = IN THE PLAYOFFS");
  // r135: the conferences one above the other, the page's width each, the top 8 of each (yours in the 8th row when
  // it is lower down), the words 44 / 46 px (were two columns of all 16 at 30 / 36: a few pixels tall on the
  // headset), a goal difference column
  const float W = kMagWf;
  for (int conf = 0; conf < 2; ++conf) {
    const float yb = conf == 0 ? 316.0f : 836.0f;
    const int first = v.conference[conf][0].club;
    P.box(44, yb, W - 44, yb + 50, 0, kMagInk);
    P.left(first >= 0 && first < nClubs ? fr_upper(s.clubs[first].conf) : std::string(conf == 0 ? "CROWN" : "STAR"), 64,
           yb + 25, 40, 500, 0xFFFFFF);
    P.right("GD", W - 250, yb + 25, 40, 120, 0xFFFFFF);
    P.right("W-L", W - 64, yb + 25, 40, 150, 0xFFFFFF);
    int mineAt = -1;
    for (int i = 0; i < 16; ++i)
      if (v.conference[conf][i].mine) mineAt = i;
    for (int row = 0; row < 8; ++row) {
      const int i = row == 7 && mineAt > 7 ? mineAt : row;
      const PortFranchiseRow& r = v.conference[conf][i];
      if (r.club < 0 || r.club >= nClubs) continue;
      const float y = yb + 56 + row * 56.0f;
      bool seeded = false;
      for (int k = 0; k < 7; ++k)
        if (v.seeds[conf][k] == r.club) seeded = true;
      if (r.mine) P.box(44, y, W - 44, y + 52, 6, 0xFFE27A);
      else if (row % 2 == 1) P.box(44, y, W - 44, y + 52, 0, 0x000001, 0.05f);
      if (seeded) P.box(44, y + 4, 54, y + 48, 0, 0xC89B2A);
      P.text(std::to_string(i + 1), 92, y + 26, 44, 70, 0x4E4840);
      P.image(mag_logo(s, r.club), 134, y + 2, 182, y + 50, false);
      P.left(fr_upper(s.clubs[r.club].place), 200, y + 26, 44, 560, kMagInk);
      const int gd = r.goalsFor - r.goalsAgainst;
      P.right((gd > 0 ? "+" : "") + std::to_string(gd), W - 250, y + 26, 44, 120, 0x4E4840);
      P.right(std::to_string(r.wins) + "-" + std::to_string(r.losses), W - 64, y + 26, 46, 150, kMagInk);
    }
  }
}
// r137: a character's rating (franchise/ratings.txt, as franchise.cpp reads it; 75 without one)
int mag_rating(const std::string& key) {
  static std::mutex mutex;
  static std::unordered_map<std::string, int> ratings;
  static bool read = false;
  std::lock_guard<std::mutex> lock(mutex);
  if (!read) {
    read = true;
    const char* home = std::getenv("STRIKERS_HOME");
    FILE* f = home != nullptr ? std::fopen((std::string(home) + "/franchise/ratings.txt").c_str(), "r") : nullptr;
    if (f != nullptr) {
      char k[128];
      int ovr = 0;
      while (std::fscanf(f, "%127s %d", k, &ovr) == 2) ratings[k] = ovr;
      std::fclose(f);
    }
  }
  auto it = ratings.find(key);
  return it != ratings.end() ? it->second : 75;
}
// r137 (Lance's clip: week 1's Golden Boot cards a black #1 and two white "-"): the places the scorers don't fill yet
// (no goals yet) go to the ones to watch: the league's best-rated stars (each club's captain, by rating), (key, club)
std::vector<std::pair<std::string, int>> mag_watch(const FrSnapshot& s) {
  const PortFranchiseView& v = s.view;
  const int leaders = std::max(0, std::min(3, v.leaderCount));
  std::vector<std::pair<std::string, int>> watch;
  if (leaders >= 3) return watch;
  std::vector<std::pair<int, int>> byRating;  // (-rating, club)
  for (int c = 0; c < static_cast<int>(s.clubs.size()) && c < PORT_FR_CLUBS; ++c)
    if (v.clubCaptainKey[c][0] != '\0') byRating.push_back({-mag_rating(v.clubCaptainKey[c]), c});
  std::stable_sort(byRating.begin(), byRating.end());
  for (const auto& r : byRating) {
    const std::string key = v.clubCaptainKey[r.second];
    bool taken = false;
    for (int i = 0; i < leaders; ++i) taken = taken || key == v.leaders[i].key;
    for (const auto& w2 : watch) taken = taken || key == w2.first;
    if (!taken) watch.push_back({key, r.second});
    if (leaders + static_cast<int>(watch.size()) >= 3) break;
  }
  return watch;
}
void mag_page_stars(Pieces& P, const FrSnapshot& s, const MagStory& st) {
  const float W = kMagWf;
  const PortFranchiseView& v = s.view;
  // r137: no goals yet: the ones to watch in the empty places, and the line under the title says so
  const int leaders = std::max(0, std::min(3, v.leaderCount));
  const std::vector<std::pair<std::string, int>> watch = mag_watch(s);
  mag_inside_frame(P, st, 3, "STAR WATCH", "THE GOLDEN BOOT RACE",
                   leaders > 0 ? "WHO'S SCORING, WHO'S RISING"
                               : (v.lastWeek <= 1 ? "NO GOALS YET - KICKOFF WEEK" : "NO GOALS YET - THE ONES TO WATCH"));
  for (int i = 0; i < 3; ++i) {
    const float x0 = 44.0f + i * 336, y0 = 320;
    P.box(x0, y0, x0 + 320, y0 + 500, 16, i == 0 ? kMagInk : 0xFFFFFF);
    P.ring(x0, y0, x0 + 320, y0 + 500, 16, 3, i == 0 ? 0xC9A227 : 0xD6D0C4);
    if (i >= leaders) {  // r137: one to watch
      const int wi = i - leaders;
      if (wi >= static_cast<int>(watch.size())) continue;
      const std::string& key = watch[wi].first;
      P.glow(x0 + 10, y0 + 40, x0 + 310, y0 + 400, i == 0 ? 0xFFD23F : 0xC8D2E6, 0.5f);
      P.left("TO WATCH", x0 + 22, y0 + 44, 40, 200, i == 0 ? kMagYellow : 0x6A645A);
      P.image(mag_logo(s, watch[wi].second), x0 + 236, y0 + 12, x0 + 308, y0 + 84, false);
      P.figure(mag_pic(P, key, false, 512), x0 + 15, y0 + 70, x0 + 305, y0 + 390, false, 0.5f, 1.0f);
      P.text(mag_name_of(key), x0 + 160, y0 + 418, 44, 300, i == 0 ? 0xFFFFFF : kMagInk);
      P.text("RATED " + std::to_string(mag_rating(key)), x0 + 160, y0 + 468, 46, 300, i == 0 ? kMagYellow : kMagRed);
      continue;
    }
    const PortFranchiseView::Leader& l = v.leaders[i];
    P.glow(x0 + 10, y0 + 40, x0 + 310, y0 + 400, i == 0 ? 0xFFD23F : 0xC8D2E6, 0.5f);
    P.text("#" + std::to_string(i + 1), x0 + 44, y0 + 44, 48, 80, i == 0 ? kMagYellow : 0x6A645A);
    P.image(mag_logo(s, l.club), x0 + 236, y0 + 12, x0 + 308, y0 + 84, false);
    P.figure(mag_pic(P, l.key, false, 512), x0 + 15, y0 + 70, x0 + 305, y0 + 390, false, 0.5f, 1.0f);  // r137: big
    // r135: 44 / 46 px (were 38 / 40)
    P.text(fr_upper(l.name), x0 + 160, y0 + 418, 44, 300, i == 0 ? 0xFFFFFF : kMagInk);
    P.text(std::to_string(l.goals) + " GOALS", x0 + 160, y0 + 468, 46, 300, i == 0 ? kMagYellow : kMagRed);
  }
  P.box(44, 862, W - 44, 922, 0, kMagRed);
  P.text("RISING", W / 2, 892, 44, 600, 0xFFFFFF);
  // the nearest to their next form (yours first)
  std::vector<const PortFranchiseEvoRow*> rising;
  for (const PortFranchiseEvoRow& r : s.evoWatch)
    if (r.evoLevel > 0) rising.push_back(&r);
  std::stable_sort(rising.begin(), rising.end(), [](const PortFranchiseEvoRow* a, const PortFranchiseEvoRow* b) {
    if (a->mine != b->mine) return a->mine;
    return a->evoLevel - a->level < b->evoLevel - b->level;
  });
  // r135: the name on its own line (48 px, the row's width), under it the level and the next form 44 px (were 34 /
  // 32 beside the name)
  for (int i = 0; i < 3 && i < static_cast<int>(rising.size()); ++i) {
    const PortFranchiseEvoRow& r = *rising[i];
    const float y0 = 936.0f + i * 142;
    P.box(44, y0, W - 44, y0 + 130, 12, r.mine ? 0xFFF1B0 : 0xFFFFFF);
    P.ring(44, y0, W - 44, y0 + 130, 12, 2, 0xD6D0C4);
    P.figure(guest_portrait(r.key), 60, y0 + 8, 190, y0 + 124, false, 0.5f, 1.0f);
    P.left(fr_upper(r.name), 210, y0 + 42, 48, 630, kMagInk);
    P.left("LV " + std::to_string(r.level), 210, y0 + 96, 44, 200, 0x4E4840);
    P.figure(guest_portrait(next_form_key(r.key)), W - 210, y0 + 8, W - 64, y0 + 124, false, 0.5f, 1.0f, false, 0x2A2620,
             0x2A2620);
    P.right((r.costume ? "COSTUME AT LV " : "EVOLVES AT LV ") + std::to_string(r.evoLevel), W - 230, y0 + 96, 44, 400,
            kMagRed);
  }
  if (rising.empty()) P.text("NOBODY CLOSE TO A NEW FORM", W / 2, 1100, 44, 1000, 0x4E4840);
}
void mag_page_rivals(Pieces& P, const FrSnapshot& s, const MagStory& st) {
  const float W = kMagWf, H = kMagHf;
  const PortFranchiseView& v = s.view;
  mag_inside_frame(P, st, 4, "RIVALS", "THE MOST WANTED", "BEAT THEM BADLY. THEY'LL REMEMBER.");
  if (v.nemCount == 0) {
    P.text("NOBODY HAS BEEF", W / 2, 700, 80, 1000, kMagInk);
    P.text("WITH YOU... YET", W / 2, 800, 80, 1000, kMagInk);
    return;
  }
  const PortFranchiseView::Nem& n = v.nems[v.nextNemesis >= 0 && v.nextNemesis < 8 ? v.nextNemesis : 0];
  P.box(110, 320, W - 110, H - 100, 10, 0xE9D2A0, 1.0f, 0xD8BC84);
  P.ring(110, 320, W - 110, H - 100, 10, 6, 0x5A3A1A);
  P.text("WANTED", W / 2, 420, 160, 800, 0x5A1A10);
  // r135: 44 px words (were 38 / 26 / 36), the heat bar taller round its word
  P.text(v.nextNemesis >= 0 ? "YOUR NEXT GAME!" : "FOR BEEF WITH YOUR CAPTAIN", W / 2, 518, 44, 800, kMagRed);
  P.box(200, 560, W - 200, 1060, 6, 0xF4E6C4);
  P.figure(mag_pic(P, n.key, false, 512), 220, 580, W - 220, 1050, false, 0.5f, 1.0f);  // r137: big
  if (n.club >= 0) P.image(mag_logo(s, n.club), W - 320, 570, W - 210, 680, false);
  P.ring(200, 560, W - 200, 1060, 6, 4, 0x5A3A1A);
  P.text(fr_upper(n.name), W / 2, 1116, 84, 800, 0x2A1A0E);
  P.text(fr_upper(n.title), W / 2, 1186, 44, 800, kMagRed);
  P.box(230, 1222, W - 230, 1270, 24, 0x5A3A1A, 0.35f);
  P.box(230, 1222, 230 + (W - 460) * std::max(0.05f, std::min(1.0f, n.heat / 100.0f)), 1270, 24, kMagRed);
  P.text("HEAT", W / 2, 1246, 40, 300, 0xFFFFFF, 2);
  P.text("BEAT YOU " + std::to_string(n.beatYou) + "   -   LOST TO YOU " + std::to_string(n.lostToYou), W / 2, 1304, 44,
         800, 0x2A1A0E);
}
// the back cover: an ad, as the magazines had (made up: nobody's real product), one of two by the week
void mag_page_back(Pieces& P, const FrSnapshot& s) {
  const float W = kMagWf, H = kMagHf;
  const PortFranchiseView& v = s.view;
  if ((v.lastWeek + v.season) % 2 == 0) {
    P.box(0, 0, W, H, 0, 0x0A0C12, 1.0f, 0x000001);
    P.burst(W / 2 - 900, 560 - 900, W / 2 + 900, 560 + 900, 24, 0.7f, 0.0f, 0x1C2333);
    P.glow(W / 2 - 420, 200, W / 2 + 420, 920, 0xFFD23F, 0.35f);
    P.style(0.14f, 0.86f, -8.0f, 10, 0x000000, 0.6f);
    P.text("FREE!", W / 2, 190, 230, 900, kMagYellow, 8, 0xFF8A1E);
    P.plain();
    // the ball phone: a ball with a keypad on it
    P.box(W / 2 - 250, 330, W / 2 + 250, 830, 250, 0xF4F4F4, 1.0f, 0xC8CCD4);
    P.poly({W / 2, 470, W / 2 + 76, 525, W / 2 + 47, 615, W / 2 - 47, 615, W / 2 - 76, 525}, kMagInk);
    for (int k = 0; k < 5; ++k) {
      const float a = -1.5708f + k * 1.2566f;
      const float cx = W / 2 + 205 * std::cos(a), cy = 580 + 205 * std::sin(a);
      P.poly({cx + 40 * std::cos(a + 1.5708f), cy + 40 * std::sin(a + 1.5708f), cx + 45 * std::cos(a), cy + 45 * std::sin(a),
              cx - 40 * std::cos(a + 1.5708f), cy - 40 * std::sin(a + 1.5708f)},
             kMagInk);
    }
    for (int r = 0; r < 4; ++r)
      for (int c = 0; c < 3; ++c) {
        const float x = W / 2 - 66 + c * 44.0f, y = 640.0f + r * 40;
        P.box(x - 16, y - 14, x + 16, y + 14, 6, 0x2A4A9A);
      }
    P.box(W / 2 + 180, 300, W / 2 + 196, 420, 6, 0x9AA0AA);
    // r135: 44 / 44 / 40 px (were 36 / 40 / 22), the small print shorter
    P.text("THE STRIKERS BALL PHONE", W / 2, 920, 70, 1000, 0xFFFFFF, 4);
    P.text("WHEN YOU SUBSCRIBE TO STRIKERS ILLUSTRATED", W / 2, 1000, 44, 1000, 0xC8D2E6);
    P.box(140, 1060, W - 140, 1220, 24, kMagRed);
    P.text("52 ISSUES", W / 2, 1110, 70, 760, 0xFFFFFF);
    P.text("ONLY 99 COINS AN ISSUE", W / 2, 1180, 44, 760, kMagYellow);
    P.text("CALL 1-800-SUPER-STRIKE", W / 2, 1300, 46, 1000, 0xFFFFFF);
    P.text("MAY NOT CONNECT TO ANYTHING. NOT A REAL BALL.", W / 2, 1380, 40, 1000, 0x8A96B0);
  } else {
    const std::string cap = v.myClub >= 0 && v.myClub < static_cast<int>(s.clubs.size()) ? std::string(v.clubCaptainKey[v.myClub]) : "";
    P.box(0, 0, W, H, 0, 0xFFE600, 1.0f, 0xFF8A1E);
    for (int i = 0; i < 9; ++i) {
      const float x = i * 150.0f;
      P.poly({x - 60, 0, x + 20, 0, x - 240, H, x - 320, H}, 0xFFFFFF, 0.12f);
    }
    P.style(0.18f, 0.8f, 0.0f, 10, 0x7A2A00, 0.5f);
    P.rim(0xFFFFFF);
    P.text("BANANA BLAST", W / 2, 170, 190, 1030, 0x3A2A9A, 8);
    P.plain();
    P.text("THE SPORTS DRINK OF THE STRIKERS SUPER LEAGUE", W / 2, 280, 42, 1000, 0x3A1A00);  // r135: 42 px (was 34)
    // the bottle (r135: its label taller, the words 46 px unshrunk)
    P.box(W / 2 + 60, 420, W / 2 + 130, 480, 8, 0x3A2A9A);
    P.box(W / 2 + 20, 470, W / 2 + 170, 1130, 60, 0x3A2A9A, 1.0f, 0x1A124A);
    P.box(W / 2 + 34, 600, W / 2 + 156, 940, 16, 0xFFE600);
    P.style(0.0f, 1.0f, -90.0f);
    P.text("BANANA BLAST", W / 2 + 95, 770, 46, 320, 0x3A2A9A);
    P.plain();
    P.box(W / 2 + 40, 500, W / 2 + 60, 1100, 10, 0xFFFFFF, 0.25f);
    P.figure(mag_pic(P, cap, false, 1024), 40, 360, W / 2 + 40, 1180, false, 0.5f, 1.0f);  // r137: big
    P.style(0.12f, 0.86f);
    P.text("DRINK LIKE A STRIKER.", W / 2, 1270, 80, 1000, 0x3A2A9A);
    P.plain();
    P.text("100% BANANA. 0% PEEL. MAY CAUSE SUPER STRIKES.", W / 2, 1362, 42, 1000, 0x3A1A00);  // r135: 42 px (was 30)
  }
}
// One page of this week's issue. r143: generated art and character renders only (no game picture: r113's goal photo
// on CLASSIC is gone, and its capture with it).
void mag_page(Pieces& P, const FrSnapshot& s, int page) {
  const MagStory st = mag_story(s);
  if (page == 0) {
    switch (st.tmpl) {
      case kMagSpecial: mag_cover_special(P, st); break;
      case kMagGold: mag_cover_gold(P, s, st); break;
      case kMagMoody: mag_cover_moody(P, st); break;
      case kMagFaceoff: mag_cover_faceoff(P, s, st); break;
      case kMagBracket: mag_cover_bracket(P, s, st); break;
      case kMagStreak: mag_cover_streak(P, st); break;
      case kMagPreview: mag_cover_preview(P, s, st); break;
      default: mag_cover_classic(P, st); break;
    }
  } else if (page == 1) {
    mag_page_scores(P, s, st);
  } else if (page == 2) {
    mag_page_table(P, s, st);
  } else if (page == 3) {
    mag_page_stars(P, s, st);
  } else if (page == 4) {
    mag_page_rivals(P, s, st);
  } else {
    mag_page_back(P, s);
  }
}
// r137: an issue's big pictures asked for as soon as it's made (game thread, fr_refresh), the cover's first, at the
// sizes its pages use (mag_pic's), so xr_big has drawn them by the time it's opened; once an issue.
void mag_prefetch(const FrSnapshot& s) {
  const PortFranchiseView& v = s.view;
  static std::string done;
  const std::string sig = std::to_string(v.slot) + "/" + std::to_string(v.season) + "/" + std::to_string(v.lastWeek) + "/" +
                          std::to_string(s.pick.valid ? s.pick.tmpl : -1) + "/" + s.pick.key + "/" + s.pick.rival + "/" +
                          std::to_string(v.leaderCount) + "/" + std::to_string(v.nemCount) + "/" + std::to_string(v.myClub);
  if (sig == done) return;
  done = sig;
  const MagStory st = mag_story(s);
  int asked = 0;
  auto want = [&](const std::string& key, bool shiny, int size) {
    if (key.empty()) return;
    xrbig::prefetch(key, shiny, size);
    ++asked;
  };
  want(st.star, st.tmpl == kMagSpecial && st.shiny, st.tmpl == kMagBracket ? 512 : 1024);  // the cover's
  if (st.tmpl == kMagFaceoff) want(st.rival, false, 1024);
  if (st.tmpl == kMagSpecial) want(st.before, false, 512);
  for (int i = 0; i < 3 && i < v.leaderCount; ++i) want(v.leaders[i].key, false, 512);  // star watch
  for (const auto& w : mag_watch(s)) want(w.first, false, 512);
  if (v.nemCount > 0) want(v.nems[v.nextNemesis >= 0 && v.nextNemesis < 8 ? v.nextNemesis : 0].key, false, 512);  // rivals
  if ((v.lastWeek + v.season) % 2 != 0 && v.myClub >= 0 && v.myClub < static_cast<int>(s.clubs.size()))
    want(v.clubCaptainKey[v.myClub], false, 1024);  // the back cover's ad
  XLOG("franchise: STRIKERS ILLUSTRATED's %d big pictures asked for (the cover's: %s)", asked, st.star.c_str());
}

// --- r131 (Lance: "in franchise mode, you have the awards, in 3D (trophies) but everything and everyone else is 2D.
// fix that shit with haste, it should all be 3D models at that point"): the characters on the hub's pages and the
// trades screen stand as 3D figures in front of the panel, each where its portrait was, and the awards night's
// winners in your room (fr_figs_frame, the render thread). A picture's drawing thread says who stands where
// (FrFigAsk); once a figure stands, the picture is drawn again without its portrait. A character with no 3D figure on
// this headset (no pack + pose file: guests/<pack>.gpk + guests/evo/<pack>.gev, ...) keeps its portrait.
struct FrFigAsk {
  std::string key;  // its portrait key (a guest pack, strikers_<name>)
  bool shiny = false;
  bool omitted = false;  // this picture left its portrait out (its figure stands there)
  float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;  // its portrait's box in the picture (0..1 across, 0..1 down)
  bool operator==(const FrFigAsk& o) const {
    return key == o.key && shiny == o.shiny && omitted == o.omitted && x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1;
  }
};
constexpr int kFrFigs = xrfx::kModelSlots - 10;  // their model slots 3.. (0-1 the evolution / the menu's, 2 the home corner's; r131: the last the magazine's; r137: the two before it the trading cards'; r138: + their specials' and light; r141: + a 1/1's world)
struct FrFigShared {
  std::mutex mutex;
  std::vector<FrFigAsk> asks;         // the newest picture's
  unsigned serial = 0;                // its snapshot's (an older snapshot's picture finishing late changes nothing)
  int askGen = 0;                     // + 1 whenever the asks change
  std::vector<std::string> standing;  // the figures standing in 3D now (the pictures leave their portraits out)
};
FrFigShared g_frFig;
std::atomic<int> g_frFigGen{0};  // + 1 when `standing` changes: the hub's picture is drawn again

std::string fr_fig_name(const std::string& key, bool shiny) { return shiny ? key + "_shiny" : key; }
// A portrait key a figure can stand for: a character's (not a club's logo, the league's)
bool fr_fig_key(const std::string& key) {
  return !key.empty() && key.compare(0, 5, "club_") != 0 && key.compare(0, 10, "franchise_") != 0;
}
bool fr_fig_standing(const std::string& name) {
  std::lock_guard<std::mutex> lock(g_frFig.mutex);
  return std::find(g_frFig.standing.begin(), g_frFig.standing.end(), name) != g_frFig.standing.end();
}
// (a picture `w` x `h` pixels: its boxes as parts of it)
void fr_figs_publish(unsigned serial, std::vector<FrFigAsk> asks, float w, float h) {
  for (FrFigAsk& a : asks) {
    a.x0 /= std::max(1.0f, w);
    a.x1 /= std::max(1.0f, w);
    a.y0 /= std::max(1.0f, h);
    a.y1 /= std::max(1.0f, h);
  }
  std::lock_guard<std::mutex> lock(g_frFig.mutex);
  if (serial < g_frFig.serial) return;
  g_frFig.serial = serial;
  if (asks == g_frFig.asks) return;
  g_frFig.asks = std::move(asks);
  ++g_frFig.askGen;
}
// A picture's characters: each one asked for, its portrait drawn unless its figure stands there (the same one twice
// on a page: the second a portrait - a model stands in one place)
struct FrFigPicture {
  std::vector<FrFigAsk> asks;
  void person(Pieces& P, const std::string& key, bool shiny, float x0, float y0, float x1, float y1) {
    bool twice = false;
    for (const FrFigAsk& b : asks) twice = twice || (b.key == key && b.shiny == shiny);
    if (fr_fig_key(key) && !twice && static_cast<int>(asks.size()) < kFrFigs && x1 > x0 && y1 > y0) {
      FrFigAsk a;
      a.key = key;
      a.shiny = shiny;
      a.x0 = P.ox + x0 * P.k;
      a.y0 = P.oy + y0 * P.k;
      a.x1 = P.ox + x1 * P.k;
      a.y1 = P.oy + y1 * P.k;
      a.omitted = fr_fig_standing(fr_fig_name(key, shiny));
      asks.push_back(a);
      if (a.omitted) return;
    }
    P.image(guest_portrait(key), x0, y0, x1, y1, true);
  }
};

// --- the hub (mode 2), 2160 x 1300: pill tabs, a big visual page, button hints
bool draw_hub(uint8_t* px, int w, int h, const FrSnapshot& s) {
  const PortFranchiseView& v = s.view;
  const PortFranchiseLeagueView& lg = s.league;
  const int nClubs = static_cast<int>(s.clubs.size());
  const int me = v.myClub;
  auto logo = [&](int club) -> const GuestPortrait& {
    static const GuestPortrait none;
    return club >= 0 && club < nClubs ? guest_portrait("club_" + s.clubs[club].id) : none;
  };
  auto place = [&](int club) { return club >= 0 && club < nClubs ? fr_upper(s.clubs[club].place) : std::string("?"); };
  const float W = static_cast<float>(w), H = static_cast<float>(h);
  std::vector<uint8_t> magCover;    // r131: the MAG page's cover, drawn on its own (lives until the board is drawn)
  xrcards::HubPage cardsPage;  // r137: the CARDS page's pieces (their pictures live until the board is drawn)
  const int packsWaiting = xrcards::packs_waiting();
  Pieces P;
  FrFigPicture figs;  // r131: the page's characters, standing in 3D in front of it
  P.box(6, 6, W - 6, H - 6, 30, 0xFFD23F);
  P.box(16, 16, W - 16, H - 16, 24, 0x1E2A4E, 0.97f, 0x070A14);
  // the pages as pills
  static const char* const kTabs[11] = {"HOME", "TABLE", "PLAYOFFS", "RESULTS", "STARS", "MAG",  // r131: MAG
                                        "HISTORY", "RIVALS", "EVO", "LEVEL", "LEAGUE"};
  const int pages = (lg.active ? 11 : 10) + 1;  // r137: + CARDS, the last
  const float tw = (W - 60.0f) / pages;
  for (int i = 0; i < pages; ++i) {
    const float x0 = 30 + i * tw + 5, x1 = x0 + tw - 10;
    const bool on = i == s.page;
    P.box(x0, 28, x1, 104, 38, on ? 0xFFD23F : 0x26335E);
    P.text(i == pages - 1 ? "CARDS" : kTabs[i], 0.5f * (x0 + x1), 66, 38, x1 - x0 - 24, on ? 0x14141E : 0xD2DAEB);
    if (i == 8 && !s.evoReadyName.empty()) P.box(x1 - 26, 34, x1 - 8, 52, 9, 0xFF5A4A);  // one of yours is ready
    if (i == pages - 1 && packsWaiting > 0) {  // r137: packs to open, how many
      P.box(x1 - 50, 20, x1 + 2, 58, 19, 0xFF5A4A);
      P.text(std::to_string(std::min(99, packsWaiting)), x1 - 24, 39, 30, 44, 0xFFFFFF, 2.0f);
    }
  }
  // the line under them: the season, the week, the difficulty (and co-op / league)
  static const char* const kLevelShort[5] = {"", "ROOKIE", "PRO", "SUPERSTAR", "LEGEND"};
  std::string sub = "SEASON " + std::to_string(v.season) + "  -  ";
  if (v.phase == PORT_FR_PHASE_DONE) sub += "CHAMPIONS CROWNED";
  else if (v.phase == PORT_FR_PHASE_PLAYOFFS) sub += fr_upper(v.roundName);
  else sub += "WEEK " + std::to_string(v.week);
  sub += std::string("  -  ") + kLevelShort[std::max(1, std::min(4, v.difficulty))];
  if (s.coopActive) sub = "CO-OP  -  " + sub;
  if (lg.active) sub = std::string("LEAGUE ") + lg.code + "  -  " + sub;
  if (packsWaiting > 0) sub += "  -  " + std::to_string(packsWaiting) + (packsWaiting == 1 ? " PACK" : " PACKS") + " TO OPEN";  // r137
  if (s.page == pages - 1) sub += "  -  " + std::to_string(xrcards::coins()) + " TIZM COINS";  // r140: on the CARDS page
  P.text(sub, W / 2, 148, 42, W - 200, 0xFFD23F, 2.0f);
  std::vector<HubHint> hints = {{"L", "PAGES", 0x3A4A70}};
  const float top = 190, bottom = 1190;
  bool cardsOwnB = false;  // r137: a pack open: B is the cards' (skip / done), Y nothing
  if (s.page == pages - 1) {
    // r137: CARDS (xr_cards.cpp): the binder (two pages of pockets), your packs, the card under the cursor; while a
    // pack is open in your hands, how to open it (the binder behind would give its cards away)
    cardsPage = xrcards::hub_page(W, H, top, bottom);
    P.v.insert(P.v.end(), cardsPage.pieces.begin(), cardsPage.pieces.end());
    hints.clear();
    for (const xrcards::Hint& ch : cardsPage.hints) {
      hints.push_back({ch.glyph, ch.words, ch.rgb});
      cardsOwnB = cardsOwnB || ch.glyph == "B";
    }
  } else if (s.page == 0) {
    // HOME: your club v theirs, both captains, the buttons stacked in the middle (up / down + A)
    auto team = [&](int club, const char* capKey, bool left, const std::string& record, bool shiny) {
      const float cx = left ? 420.0f : W - 420.0f;
      const uint32_t c1 = club >= 0 && club < nClubs ? s.clubs[club].c1 : 0x31406A;
      P.glow(cx - 400, 230, cx + 400, 900, c1 != 0 ? c1 : 0x31406A, 0.7f);
      P.image(logo(club), left ? cx - 380 : cx - 20, 230, left ? cx + 20 : cx + 380, 650, false);
      figs.person(P, capKey, shiny, left ? cx - 10 : cx - 330, 300, left ? cx + 330 : cx + 10, 860);  // r131: in 3D
      P.text(club >= 0 && club < nClubs ? place(club) : std::string(""), cx, 920, 80, 760, 0xFFFFFF, 5.0f);
      if (!record.empty()) P.text(record, cx, 1000, 62, 400, 0xFFD23F, 4.0f);
    };
    auto record = [](int wins, int losses) { return std::to_string(wins) + "-" + std::to_string(losses); };
    const bool hasMatch = v.nextOpponent >= 0 && v.nextOpponent < nClubs;
    const bool backupStarts = v.startsBackup && v.backup[0] != '\0';
    team(me, backupStarts ? v.backupKey : v.captainKey, true, record(v.wins, v.losses),
         backupStarts ? v.backupShiny : v.captainShiny);
    auto button = [&](int row, const std::string& label, uint32_t rgb, bool on, bool can) {
      const float y0 = 600 + row * 135.0f;
      P.box(W / 2 - 300, y0, W / 2 + 300, y0 + 115, 44, can ? rgb : 0x30384E);
      if (on) P.ring(W / 2 - 308, y0 - 8, W / 2 + 308, y0 + 123, 50, 9, 0xFFD23F);
      P.text(label, W / 2, y0 + 58, 68, 560, can ? 0xFFFFFF : 0x8A94AA, 4.0f);
    };
    if (hasMatch) {
      const int o = v.nextOpponent;
      team(o, v.clubCaptainKey[o], false, v.phase == PORT_FR_PHASE_PLAYOFFS ? std::string("") : record(v.oppWins, v.oppLosses),
           false);
      P.text("VS", W / 2, 320, 200, 400, 0xFFF7B0, 8.0f);
      std::string where = v.nextHome ? "HOME" : "AWAY";
      if (v.phase == PORT_FR_PHASE_PLAYOFFS)
        where += "  -  GAME " + std::to_string(v.myGameNo) + "  -  " + std::to_string(v.mySeriesMe) + "-" +
                 std::to_string(v.mySeriesThem);
      P.text(where, W / 2, 450, 40, 560, 0xC8D2E6, 2.0f);
      // r137: the game's weather from the calendar (SNOW GAME, RAIN, UNDER THE LIGHTS ...), beside a nemesis
      const int wx = s.nextWeather;
      const bool wxTag = wx != 0 && PortWeatherLabel(wx)[0] != '\0';
      if (v.nextNemesis >= 0) {
        const float nx = wxTag ? W / 2 - 185.0f : W / 2.0f;
        P.box(nx - 170, 490, nx + 170, 560, 34, 0x6E1414);
        P.ring(nx - 170, 490, nx + 170, 560, 34, 4, 0xFF5A4A);
        P.text("NEMESIS!", nx, 525, 48, 320, 0xFFFFFF);
      }
      if (wxTag) {
        const bool night = (wx & PORT_WX_NIGHT) != 0, snow = (wx & PORT_WX_SNOW) != 0, rain = (wx & PORT_WX_RAIN) != 0;
        const float half = v.nextNemesis >= 0 ? 170.0f : 230.0f;
        const float cx = v.nextNemesis >= 0 ? W / 2 + 185.0f : W / 2.0f;
        const uint32_t fill = snow ? 0x2A4C78 : (rain ? 0x1E3A5A : 0x111A3C);
        const uint32_t edge = night ? 0xFFD23F : (snow ? 0xF2F8FF : 0x8FC8FF);
        P.box(cx - half, 490, cx + half, 560, 34, fill);
        P.ring(cx - half, 490, cx + half, 560, 34, 4, edge);
        P.text(PortWeatherLabel(wx), cx, 525, 40, 2 * half - 30, night && !snow && !rain ? 0xFFE9A0 : 0xFFFFFF);
      }
      if (lg.active && lg.vsMember >= 0 && lg.vsMember < lg.memberCount) {
        const std::string who = fr_upper(lg.members[lg.vsMember].name);
        const bool can = lg.meReady || (s.canPlay && lg.synced);
        button(0, lg.meReady ? "WAITING..." : "READY", 0x2EA043, s.cursor == 0 && can, can);
        P.text((lg.themReady ? "VS " + who + " - THEY'RE READY!" : "VS " + who + (lg.members[lg.vsMember].online ? " (ONLINE)" : " (OFFLINE)")),
               W / 2, 800, 40, 700, 0xFFD23F, 2.0f);
        hints.push_back({"A", lg.meReady ? "NOT READY" : "READY", 0x2EA043});
      } else {
        button(0, s.canPlay ? "PLAY" : "MAIN MENU", 0x2EA043, s.cursor == 0 && s.canPlay, s.canPlay);
        button(1, "SIM", 0x3A4A70, s.cursor == 1, true);
        if (v.phase == PORT_FR_PHASE_REGULAR && !lg.active) button(2, "SIM SEASON", 0x3A4A70, s.cursor == 2, true);
        hints.push_back({"A", "GO", 0x2EA043});
      }
      if (v.backup[0] != '\0') hints.push_back({"X", "SWAP CAPTAIN", 0x2F6FD0});
    } else {
      // no game of yours today: the day's status big, play it out or watch one of today's games
      std::string st = v.phase == PORT_FR_PHASE_DONE ? std::string("CHAMPIONS!")
                                                      : (v.status[0] != '\0' ? fr_upper(v.status) : std::string("NO GAME TODAY"));
      if (v.phase == PORT_FR_PHASE_DONE && v.champion >= 0 && v.champion < nClubs) {
        P.glow(W - 820, 230, W - 20, 900, 0xFFD23F, 0.6f);
        P.image(logo(v.champion), W - 640, 240, W - 200, 680, false);
        P.text(place(v.champion), W - 420, 760, 80, 760, 0xFFFFFF, 5.0f);
        P.text("CHAMPIONS!", W - 420, 860, 70, 760, 0xFFD23F, 4.0f);
        button(0, "SEASON " + std::to_string(v.season + 1), 0x2EA043, true, true);
        hints.push_back({"A", "NEXT SEASON", 0x2EA043});
      } else {
        P.text(st, W / 2, 360, 110, 700, 0xFFF7B0, 6.0f);
        if (v.canSimDay) {
          const float y0 = 470;
          P.box(W / 2 - 300, y0, W / 2 + 300, y0 + 115, 44, 0x2EA043);
          if (s.cursor == 0) P.ring(W / 2 - 308, y0 - 8, W / 2 + 308, y0 + 123, 50, 9, 0xFFD23F);
          P.text("PLAY THE DAY", W / 2, y0 + 58, 64, 560, 0xFFFFFF, 4.0f);
          // today's games to watch, logo v logo, under it on the right
          for (int i = 0; i < s.today.count && i < 6; ++i) {
            const float y = 610 + i * 92.0f, x0 = W - 820, x1 = W - 60;
            const bool on = s.cursor == i + 1;
            P.box(x0, y, x1, y + 82, 30, on ? 0x2B4690 : 0x151C36);
            if (on) P.ring(x0 - 4, y - 4, x1 + 4, y + 86, 34, 6, 0xFFD23F);
            P.image(logo(s.today.home[i]), x0 + 14, y + 4, x0 + 88, y + 78, false);
            P.image(logo(s.today.away[i]), x1 - 88, y + 4, x1 - 14, y + 78, false);
            P.text(place(s.today.home[i]) + "  V  " + place(s.today.away[i]), 0.5f * (x0 + x1), y + 41, 34, x1 - x0 - 220,
                   0xFFFFFF, 2.0f);
          }
          if (s.today.count > 0) P.text("WATCH ONE", W - 440, 575, 36, 400, 0xC8D2E6, 2.0f);
          hints.push_back({"A", "GO", 0x2EA043});
          hints.push_back({"X", "SKIP AHEAD", 0x2F6FD0});
        }
      }
    }
    if (s.coopGuest) P.text("YOUR FRIEND RUNS THIS CAREER: THEY START THE MATCHES", W / 2, 1100, 36, 1600, 0xFFD23F, 2.0f);
    else if (v.headline[0] != '\0') P.text(fr_upper(v.headline), W / 2, 1110, 34, 1700, 0xB8C2D6, 2.0f);
    {  // r140: the TIZM COINS the last match you played paid (for a few hours after it), and why
      std::string why;
      long long when = 0;
      const int got = xrcards::last_coins(&why, &when);
      if (got > 0 && !s.coopGuest && static_cast<long long>(std::time(nullptr)) - when < 3LL * 3600)
        P.text("+" + std::to_string(got) + " TIZM COINS:  " + fr_upper(why), W / 2, 1154, 32, 1800, 0xFFD23F, 2.0f);
    }
  } else if (s.page == 1) {
    // TABLE: both conferences, logos, the record big
    for (int conf = 0; conf < 2; ++conf) {
      const float x0 = 60 + conf * 1030, x1 = x0 + 980;
      const int first = v.conference[conf][0].club;
      P.text((first >= 0 && first < nClubs ? fr_upper(s.clubs[first].conf) : std::string(conf == 0 ? "CROWN" : "STAR")),
             0.5f * (x0 + x1), 215, 48, 900, 0xFFD23F, 3.0f);
      for (int i = 0; i < 16; ++i) {
        const PortFranchiseRow& r = v.conference[conf][i];
        if (r.club < 0 || r.club >= nClubs) continue;
        const float y = 250 + i * 58.0f;
        bool seeded = false;
        for (int k = 0; k < 7; ++k)
          if (v.seeds[conf][k] == r.club) seeded = true;
        P.box(x0, y, x1, y + 52, 14, r.mine ? 0x3B5AA8 : (seeded ? 0x22305A : 0x121A30));
        if (seeded) P.box(x0, y + 6, x0 + 8, y + 46, 3, 0xFFD23F);
        P.text(std::to_string(i + 1), x0 + 42, y + 26, 32, 60, 0xB8C2D6);
        P.image(logo(r.club), x0 + 76, y + 1, x0 + 126, y + 51, false);
        P.left(place(r.club), x0 + 140, y + 26, 34, 520, r.mine ? 0xFFF7B0 : 0xFFFFFF, 2.0f);
        const int gd = r.goalsFor - r.goalsAgainst;
        P.right((gd > 0 ? "+" : "") + std::to_string(gd), x1 - 190, y + 26, 30, 120, 0x9AA6BE);
        P.right(std::to_string(r.wins) + "-" + std::to_string(r.losses), x1 - 20, y + 26, 40, 160, 0xFFFFFF, 2.0f);
      }
    }
    P.text("GOLD EDGE = IN THE PLAYOFFS", W / 2, 1200 - 20, 30, 900, 0xB8C2D6);
  } else if (s.page == 2) {
    // r130 (Lance: "fix that playoff tree. it should mimic a NCAA college basketball bracket"): a bracket. Each
    // conference's half runs in toward the middle - Kickoff (the 1 seed's bye on top), Semis, Conference Final - each
    // matchup a box of two team rows (seed, logo, place, series wins; the loser dimmed), lines joining each pair to the
    // box it feeds; the Super Strike Series in the middle under the round's logo (the champion once crowned). In the
    // regular season: the bracket if the season ended today (seeds 2 v 7, 3 v 6, 4 v 5).
    const bool projected = v.phase == PORT_FR_PHASE_REGULAR;
    struct Match {
      int hi = -1, lo = -1, hiSeed = 0, loSeed = 0, winsHi = -1, winsLo = -1, winner = -1;
    };
    constexpr float kBw = 270.0f, kGap = 28.0f, kLine = 60.0f, kMargin = 24.0f;
    constexpr uint32_t kWire = 0x4A5A82, kWon = 0xFFD23F;
    const float centreY = 675.0f;
    static const float kKickY[4] = {330.0f, 560.0f, 790.0f, 1020.0f};  // the bye, then the three Kickoff matchups
    const float semiY[2] = {0.5f * (kKickY[0] + kKickY[1]), 0.5f * (kKickY[2] + kKickY[3])};
    auto colX = [&](int conf, int col) {  // a box's left edge
      return conf == 0 ? kMargin + col * (kBw + kGap) : W - kMargin - kBw - col * (kBw + kGap);
    };
    auto seed_of = [&](int conf, int club) {
      for (int k = 0; k < 7; ++k)
        if (club >= 0 && v.seeds[conf][k] == club) return k + 1;
      return 0;
    };
    auto from = [&](const PortFranchiseSeries& sr) {
      Match m;
      m.hi = sr.hi;
      m.lo = sr.lo;
      m.hiSeed = sr.hiSeed;
      m.loSeed = sr.loSeed;
      m.winsHi = sr.winsHi;
      m.winsLo = sr.winsLo;
      m.winner = sr.winner;
      return m;
    };
    auto has = [](const Match& m, int club) { return club >= 0 && (m.hi == club || m.lo == club); };
    // one team's row: seed, logo, place, wins (or a word: BYE / TBD)
    auto row = [&](int club, int seed, int wins, bool lost, float x0, float y0, const char* word) {
      const float yc = y0 + kLine * 0.5f;
      if (club < 0 || club >= nClubs) {
        P.left("TBD", x0 + 70, yc, 28, kBw - 90, 0x6A7896);
        return;
      }
      const uint32_t ink = lost ? 0x7A849A : 0xFFFFFF;
      if (seed > 0) P.text(std::to_string(seed), x0 + 22, yc, 28, 34, lost ? 0x7A849A : 0xFFD23F, 2.0f);
      P.image(logo(club), x0 + 42, y0 + 5, x0 + 92, y0 + kLine - 5, false, lost ? 0.35f : 1.0f);
      P.left(place(club), x0 + 100, yc, 30, kBw - (word != nullptr ? 168 : 150), ink, 2.0f);
      if (word != nullptr)
        P.right(word, x0 + kBw - 14, yc, 26, 70, 0xB8C2D6, 1.5f);
      else if (wins >= 0)
        P.right(std::to_string(wins), x0 + kBw - 16, yc, 40, 44, lost ? 0x7A849A : 0xFFFFFF, 3.0f);
    };
    // a matchup's box, centred on yc
    auto box = [&](const Match& m, float x0, float yc) {
      const bool mineM = has(m, me);
      const float y0 = yc - kLine, y1 = yc + kLine;
      P.box(x0, y0, x0 + kBw, y1, 18, mineM ? 0x3B5AA8 : 0x151C36);
      if (mineM) P.ring(x0 - 4, y0 - 4, x0 + kBw + 4, y1 + 4, 22, 5, 0xFFD23F);
      P.box(x0 + 12, yc - 1.5f, x0 + kBw - 12, yc + 1.5f, 0, 0x2C3858);
      row(m.hi, m.hiSeed, m.winsHi, m.winner >= 0 && m.winner != m.hi, x0, y0, nullptr);
      row(m.lo, m.loSeed, m.winsLo, m.winner >= 0 && m.winner != m.lo, x0, yc, nullptr);
    };
    // the lines from two feeding boxes (centres ya, yb in column col) to the box they feed (centre yt, column col + 1)
    auto wires = [&](int conf, int col, float ya, bool aWon, float yb, bool bWon, float yt) {
      const float edge = conf == 0 ? colX(conf, col) + kBw : colX(conf, col);
      const float next = conf == 0 ? colX(conf, col + 1) : colX(conf, col + 1) + kBw;
      const float xm = 0.5f * (edge + next), xa = std::min(edge, xm), xb = std::max(edge, xm);
      P.box(xa, ya - 2, xb, ya + 2, 0, aWon ? kWon : kWire);
      P.box(xa, yb - 2, xb, yb + 2, 0, bWon ? kWon : kWire);
      P.box(xm - 2, std::min(ya, yb) - 2, xm + 2, std::max(ya, yb) + 2, 0, kWire);
      P.box(std::min(xm, next), yt - 2, std::max(xm, next), yt + 2, 0, (aWon || bWon) ? kWon : kWire);
    };
    Match finalM;
    bool finalSet = false;
    int confChamp[2] = {-1, -1};
    float confFinalEdge[2] = {0, 0};
    static const char* const kConf[2] = {"CROWN CONFERENCE", "STAR CONFERENCE"};
    for (int conf = 0; conf < 2; ++conf) {
      std::vector<Match> kick, semi, cfin;
      if (projected) {
        for (int k = 0; k < 3; ++k) {
          Match m;
          m.hiSeed = 2 + k;
          m.loSeed = 7 - k;
          m.hi = v.seeds[conf][m.hiSeed - 1];
          m.lo = v.seeds[conf][m.loSeed - 1];
          kick.push_back(m);
        }
      } else {
        for (int i = 0; i < v.seriesCount; ++i) {
          const PortFranchiseSeries& sr = v.series[i];
          if (sr.round == 4 && !finalSet) {
            finalM = from(sr);
            finalSet = true;
          }
          if ((sr.conf == 1 ? 1 : 0) != conf || sr.round < 1 || sr.round > 3) continue;
          (sr.round == 1 ? kick : (sr.round == 2 ? semi : cfin)).push_back(from(sr));
        }
      }
      const int bye = v.seeds[conf][0];
      // the semis: the one with the 1 seed on top; before they're drawn, the bye v the 4 / 5 winner and the other two
      Match semiM[2];
      for (const Match& m : semi) semiM[has(m, bye) ? 0 : 1] = m;
      // the Kickoff boxes in the order they feed: the one into the 1 seed's semi first, then the other two by seed
      std::sort(kick.begin(), kick.end(), [](const Match& a, const Match& b) { return a.hiSeed < b.hiSeed; });
      Match kickM[3];
      bool kickSet[3] = {false, false, false};
      int intoA = -1;
      for (int k = 0; k < static_cast<int>(kick.size()); ++k)
        if (kick[k].winner >= 0 && has(semiM[0], kick[k].winner)) intoA = k;
      if (intoA < 0)
        for (int k = 0; k < static_cast<int>(kick.size()); ++k)
          if (kick[k].hiSeed == 4) intoA = k;
      int slot = 1;
      for (int k = 0; k < static_cast<int>(kick.size()) && k < 3; ++k) {
        const int to = k == intoA ? 0 : slot++;
        if (to < 3) {
          kickM[to] = kick[k];
          kickSet[to] = true;
        }
      }
      // a semi not drawn yet: who's through so far
      if (semiM[0].hi < 0 && semiM[0].lo < 0) {
        semiM[0].hi = bye;
        semiM[0].hiSeed = 1;
        if (kickSet[0] && kickM[0].winner >= 0) {
          semiM[0].lo = kickM[0].winner;
          semiM[0].loSeed = seed_of(conf, kickM[0].winner);
        }
      }
      if (semiM[1].hi < 0 && semiM[1].lo < 0) {
        int a = kickSet[1] ? kickM[1].winner : -1, b = kickSet[2] ? kickM[2].winner : -1;
        if (a >= 0 && b >= 0 && seed_of(conf, b) < seed_of(conf, a)) std::swap(a, b);
        semiM[1].hi = a >= 0 ? a : b;
        semiM[1].lo = a >= 0 ? b : -1;
        semiM[1].hiSeed = seed_of(conf, semiM[1].hi);
        semiM[1].loSeed = seed_of(conf, semiM[1].lo);
      }
      Match cf;
      if (!cfin.empty()) {
        cf = cfin[0];
      } else {
        int a = semiM[0].winner, b = semiM[1].winner;
        if (a >= 0 && b >= 0 && seed_of(conf, b) < seed_of(conf, a)) std::swap(a, b);
        cf.hi = a >= 0 ? a : b;
        cf.lo = a >= 0 ? b : -1;
        cf.hiSeed = seed_of(conf, cf.hi);
        cf.loSeed = seed_of(conf, cf.lo);
      }
      confChamp[conf] = cf.winner;
      // headers
      static const char* const kHead[3] = {"KICKOFF", "SEMIS", "CONF FINAL"};
      for (int col = 0; col < 3; ++col) P.text(kHead[col], colX(conf, col) + kBw / 2, 226, 32, kBw, 0xFFD23F, 2.0f);
      P.text(kConf[conf], conf == 0 ? kMargin + (3 * kBw + 2 * kGap) / 2 : W - kMargin - (3 * kBw + 2 * kGap) / 2, 1150, 34,
             3 * kBw, 0xB8C2D6, 2.0f);
      // Kickoff: the 1 seed's bye on top, then the three
      const float x0 = colX(conf, 0);
      P.box(x0, kKickY[0] - kLine / 2, x0 + kBw, kKickY[0] + kLine / 2, 18, bye == me ? 0x3B5AA8 : 0x151C36);
      row(bye, 1, -1, false, x0, kKickY[0] - kLine / 2, "BYE");
      for (int k = 0; k < 3; ++k)
        if (kickSet[k]) box(kickM[k], x0, kKickY[k + 1]);
      // the wires and the later rounds
      wires(conf, 0, kKickY[0], !semi.empty(), kKickY[1], kickSet[0] && kickM[0].winner >= 0, semiY[0]);
      wires(conf, 0, kKickY[2], kickSet[1] && kickM[1].winner >= 0, kKickY[3], kickSet[2] && kickM[2].winner >= 0, semiY[1]);
      box(semiM[0], colX(conf, 1), semiY[0]);
      box(semiM[1], colX(conf, 1), semiY[1]);
      wires(conf, 1, semiY[0], semiM[0].winner >= 0, semiY[1], semiM[1].winner >= 0, centreY);
      box(cf, colX(conf, 2), centreY);
      confFinalEdge[conf] = conf == 0 ? colX(conf, 2) + kBw : colX(conf, 2);
    }
    // the middle: the Super Strike Series
    const float fx0 = W / 2 - 150, fx1 = W / 2 + 150;
    if (!finalSet) {
      finalM.hi = confChamp[0];
      finalM.lo = confChamp[1];
      finalM.hiSeed = seed_of(0, confChamp[0]);
      finalM.loSeed = seed_of(1, confChamp[1]);
    }
    P.box(confFinalEdge[0], centreY - 2, fx0, centreY + 2, 0, confChamp[0] >= 0 ? kWon : kWire);
    P.box(fx1, centreY - 2, confFinalEdge[1], centreY + 2, 0, confChamp[1] >= 0 ? kWon : kWire);
    P.text(projected ? "IF THE SEASON ENDED TODAY" : "SUPER STRIKE SERIES", W / 2, 226, 32, 300, 0xFFD23F, 2.0f);
    {
      // (the final's box is 300 wide: its rows use the same layout, a little wider)
      const bool mineM = has(finalM, me);
      P.box(fx0, centreY - kLine, fx1, centreY + kLine, 18, mineM ? 0x3B5AA8 : 0x151C36);
      P.ring(fx0 - 5, centreY - kLine - 5, fx1 + 5, centreY + kLine + 5, 23, 5, 0xFFD23F);
      P.box(fx0 + 12, centreY - 1.5f, fx1 - 12, centreY + 1.5f, 0, 0x2C3858);
      row(finalM.hi, 0, finalM.winsHi, finalM.winner >= 0 && finalM.winner != finalM.hi, fx0 + 15, centreY - kLine, nullptr);
      row(finalM.lo, 0, finalM.winsLo, finalM.winner >= 0 && finalM.winner != finalM.lo, fx0 + 15, centreY, nullptr);
    }
    if (v.champion >= 0 && v.champion < nClubs) {
      P.glow(W / 2 - 170, 270, W / 2 + 170, 590, 0xFFD23F, 0.5f);
      P.image(logo(v.champion), W / 2 - 140, 290, W / 2 + 140, 570, false);
      P.text("CHAMPIONS", W / 2, 820, 56, 320, 0xFFD23F, 4.0f);
    } else if (!projected && v.roundLogo[0] != '\0') {
      P.image(guest_portrait(v.roundLogo), W / 2 - 140, 290, W / 2 + 140, 570, false, 0.9f);
    } else {
      P.image(guest_portrait("franchise_league"), W / 2 - 140, 290, W / 2 + 140, 570, false, 0.9f);
    }
  } else if (s.page == 3) {
    // RESULTS: last week's games, logo v logo, the score big
    P.text(v.lastWeek > PORT_FR_WEEKS ? "LAST GAME DAY" : (v.lastWeek > 0 ? "WEEK " + std::to_string(v.lastWeek) : "NO GAMES YET"),
           W / 2, 215, 48, 900, 0xFFD23F, 3.0f);
    const int n = std::min(16, v.playedCount);
    for (int i = 0; i < n; ++i) {
      const PortFranchiseView::Played& g2 = v.played[i];
      const int c = i % 4, r = i / 4;
      const float x0 = 60 + c * 515, y0 = 255 + r * 230;
      P.box(x0, y0, x0 + 495, y0 + 215, 26, g2.mine ? 0x3B5AA8 : 0x151C36);
      if (g2.mine) P.ring(x0 - 4, y0 - 4, x0 + 499, y0 + 219, 30, 6, 0xFFD23F);
      const bool homeWon = g2.homeGoals > g2.awayGoals;
      P.image(logo(g2.home), x0 + 12, y0 + 22, x0 + 172, y0 + 182, false, homeWon ? 1.0f : 0.45f);
      P.image(logo(g2.away), x0 + 323, y0 + 22, x0 + 483, y0 + 182, false, homeWon ? 0.45f : 1.0f);
      P.text(std::to_string(g2.homeGoals) + "-" + std::to_string(g2.awayGoals), x0 + 247, y0 + 100, 84, 150, 0xFFFFFF, 4.0f);
      if (g2.overtime) P.text("OT", x0 + 247, y0 + 180, 32, 100, 0xFFD23F, 2.0f);
    }
  } else if (s.page == 4) {
    // STARS: the golden boot's top 3 big, the rest small, who's hot
    P.text("GOLDEN BOOT", W / 2, 215, 48, 900, 0xFFD23F, 3.0f);
    for (int i = 0; i < 3 && i < v.leaderCount; ++i) {
      const PortFranchiseView::Leader& l = v.leaders[i];
      const float cx = W / 2 + (i == 0 ? 0.0f : (i == 1 ? -560.0f : 560.0f)), big = i == 0 ? 1.0f : 0.82f;
      const float y0 = 250 + (i == 0 ? 0.0f : 60.0f);
      P.glow(cx - 300 * big, y0, cx + 300 * big, y0 + 640 * big, i == 0 ? 0xFFD23F : 0x6A9AE0, 0.45f);
      figs.person(P, l.key, false, cx - 260 * big, y0, cx + 260 * big, y0 + 540 * big);  // r131: in 3D
      P.image(logo(l.club), cx + 150 * big, y0, cx + 260 * big, y0 + 110 * big, false);
      P.text("#" + std::to_string(i + 1), cx - 200 * big, y0 + 40, 70 * big, 140, i == 0 ? 0xFFD23F : 0xC8D2E6, 4.0f);
      P.text(fr_upper(l.name), cx, y0 + 580 * big, 56 * big, 520, 0xFFFFFF, 4.0f);
      P.text(std::to_string(l.goals) + " GOALS", cx, y0 + 650 * big, 50 * big, 400, 0xFFD23F, 3.0f);
    }
    if (v.leaderCount == 0) P.text("NO GOALS YET", W / 2, 600, 90, 1200, 0xC8D2E6, 4.0f);
    // the rest of the top 10, small
    for (int i = 3; i < v.leaderCount && i < 10; ++i) {
      const PortFranchiseView::Leader& l = v.leaders[i];
      const float x0 = 110 + (i - 3) * 280.0f, y0 = 990;
      P.box(x0, y0, x0 + 260, y0 + 190, 20, l.club == me ? 0x3B5AA8 : 0x151C36);
      figs.person(P, l.key, false, x0 + 10, y0 + 6, x0 + 130, y0 + 150);  // r131: in 3D
      P.text(std::to_string(l.goals), x0 + 196, y0 + 70, 64, 110, 0xFFD23F, 3.0f);
      P.text(fr_upper(l.name), x0 + 130, y0 + 168, 28, 240, 0xFFFFFF, 2.0f);
    }
  } else if (s.page == 5) {
    // PAPER: today's front page, small, and A to read it in front of you. r131: MAG, this week's STRIKERS
    // ILLUSTRATED cover small
    // (drawn on its own and then placed: the cover's lights and its star run past its edges)
    const int coverW = static_cast<int>(kMagW * 0.66f), coverH = static_cast<int>(kMagH * 0.66f);
    Pieces F;
    F.k = 0.66f;
    const unsigned bigAt = xrbig::generation();  // r137
    mag_page(F, s, 0);  // r143: no game picture
    if (F.bigMissing) {  // r137: its star's big picture still being drawn: the hub drawn again once it's there
      g_hubBigAt = bigAt;
      g_hubBigWait = true;
    }
    magCover.assign(static_cast<size_t>(coverW) * coverH * 4, 0);
    xrtext::board(magCover.data(), coverW, coverH, F.v);
    const float coverX = W / 2 - 0.5f * coverW - 300;
    P.image(magCover.data(), coverW, coverH, coverX, 190, coverX + coverW, 190.0f + coverH, false);
    P.box(W / 2 + 330, 560, W / 2 + 830, 700, 50, 0x2EA043);
    P.ring(W / 2 + 322, 552, W / 2 + 838, 708, 56, 9, 0xFFD23F);
    P.text("READ IT", W / 2 + 580, 630, 80, 460, 0xFFFFFF, 4.0f);
    P.text("GRAB IT. FLIP IT.", W / 2 + 580, 760, 38, 520, 0xC8D2E6, 2.0f);
    hints.push_back({"A", "READ", 0x2EA043});
  } else if (s.page == 6) {
    // HISTORY: this season's awards (with who won them) and the hall of fame
    P.text(v.phase == PORT_FR_PHASE_DONE ? "AWARDS NIGHT" : "LAST AWARDS NIGHT", 560, 215, 46, 900, 0xFFD23F, 3.0f);
    for (int i = 0; i < v.awardCount && i < 8; ++i) {
      const float y = 250 + i * 116.0f;
      P.box(60, y, 1060, y + 104, 20, 0x151C36);
      figs.person(P, v.awardKeys[i], false, 70, y + 4, 166, y + 100);  // r131: a winner in 3D (a club's logo stays)
      P.left(fr_upper(v.awards[i]), 186, y + 52, 34, 860, 0xFFFFFF, 2.0f);
    }
    if (v.awardCount == 0) P.text("AFTER THE SUPER STRIKE SERIES", 560, 400, 40, 900, 0xC8D2E6, 2.0f);
    P.text("HALL OF FAME", 1620, 215, 46, 900, 0xFFD23F, 3.0f);
    for (int i = 0; i < v.historyCount && i < 16; ++i)
      P.left(fr_upper(v.history[i]), 1130, 262 + i * 58.0f, 32, 960, 0xE6ECF5, 2.0f);
    if (v.historyCount == 0) P.text("NO SEASONS FINISHED YET", 1620, 400, 40, 900, 0xC8D2E6, 2.0f);
  } else if (s.page == 7) {
    // RIVALS: wanted posters
    if (v.nemCount == 0) {
      P.text("NOBODY HAS BEEF WITH YOU... YET", W / 2, 560, 90, 1800, 0xFFF7B0, 5.0f);
      P.text("BEAT THEM, EMBARRASS THEM, KNOCK THEM OUT. THEY'LL REMEMBER.", W / 2, 700, 40, 1800, 0xC8D2E6, 2.0f);
    }
    const int n = std::min(4, v.nemCount);
    for (int i = 0; i < n; ++i) {
      const PortFranchiseView::Nem& nm = v.nems[i];
      const float pw2 = 470, x0 = W / 2 - 0.5f * (n * pw2 + (n - 1) * 30) + i * (pw2 + 30), y0 = 220;
      P.box(x0, y0, x0 + pw2, y0 + 960, 10, nm.active ? 0xE9D2A0 : 0x9A9080, 1.0f, nm.active ? 0xD8BC84 : 0x7A7262);
      P.ring(x0, y0, x0 + pw2, y0 + 960, 10, 5, 0x5A3A1A);
      if (i == v.nextNemesis) P.ring(x0 - 8, y0 - 8, x0 + pw2 + 8, y0 + 968, 16, 8, 0xFF5A4A);
      P.text("WANTED", x0 + pw2 / 2, y0 + 80, 96, pw2 - 40, 0x5A1A10);
      P.box(x0 + 40, y0 + 140, x0 + pw2 - 40, y0 + 620, 6, 0xF4E6C4);
      figs.person(P, nm.key, false, x0 + 50, y0 + 150, x0 + pw2 - 50, y0 + 610);  // r131: in 3D
      if (nm.club >= 0) P.image(logo(nm.club), x0 + pw2 - 150, y0 + 150, x0 + pw2 - 50, y0 + 250, false);
      P.text(fr_upper(nm.name), x0 + pw2 / 2, y0 + 680, 52, pw2 - 40, 0x2A1A0E);
      P.text(fr_upper(nm.title), x0 + pw2 / 2, y0 + 740, 34, pw2 - 40, kPaperRed);
      P.box(x0 + 50, y0 + 790, x0 + pw2 - 50, y0 + 826, 18, 0x5A3A1A, 0.35f);
      P.box(x0 + 50, y0 + 790, x0 + 50 + (pw2 - 100) * std::max(0.05f, std::min(1.0f, nm.heat / 100.0f)), y0 + 826, 18, kPaperRed);
      P.text("HEAT", x0 + pw2 / 2, y0 + 808, 24, 200, 0xFFFFFF);
      P.text(std::to_string(nm.beatYou) + " - " + std::to_string(nm.lostToYou), x0 + pw2 / 2, y0 + 890, 64, pw2 - 60, 0x2A1A0E);
      P.text(i == v.nextNemesis ? "YOUR NEXT GAME!" : (nm.teammate ? "YOUR OWN TEAMMATE" : "THEM - YOU"), x0 + pw2 / 2,
             y0 + 940, 28, pw2 - 60, i == v.nextNemesis ? kPaperRed : 0x5A3A1A);
    }
  } else if (s.page == 8) {
    // EVO: everyone in the league with a next form, as portraits: its level, its next form at what level
    const int cols = 6, rows = 3;
    for (int i = 0; i < cols * rows && i < static_cast<int>(s.evoWatch.size()); ++i) {
      const PortFranchiseEvoRow& r = s.evoWatch[i];
      const int c = i % cols, rw = i / cols;
      const float x0 = 60 + c * 343.0f, y0 = 200 + rw * 330.0f, x1 = x0 + 325, y1 = y0 + 315;
      P.box(x0, y0, x1, y1, 24, r.mine ? 0x3B5AA8 : 0x151C36);
      if (r.ready) P.ring(x0 - 4, y0 - 4, x1 + 4, y1 + 4, 28, 7, 0xFFD23F);
      P.image(logo(r.club), x0 + 8, y0 + 8, x0 + 72, y0 + 72, false);
      figs.person(P, r.key, r.shiny, x0 + 20, y0 + 20, x1 - 110, y1 - 70);  // r131: in 3D (its next form stays a shadow)
      if (r.evoLevel > 0)
        P.image(guest_portrait(next_form_key(r.key)), x1 - 120, y0 + 100, x1 - 10, y1 - 70, true, 1.0f, 0x0A0E1A);
      P.text("LV " + std::to_string(r.level), x0 + 90, y1 - 36, 40, 150, 0xFFFFFF, 3.0f);
      P.text(r.ready ? (r.costume ? "SUIT UP!" : "EVOLVE!") : (r.evoLevel > 0 ? "> LV " + std::to_string(r.evoLevel) : "TOP FORM"),
             x1 - 80, y1 - 36, 30, 140, r.ready ? 0xFFD23F : 0xB8C2D6, 2.0f);
      if (r.shiny) P.text("SHINY", x1 - 60, y0 + 40, 28, 110, 0xFFD23F, 2.0f);
    }
    if (s.evoWatch.empty()) P.text("NO POKEMON OR HEROES IN THIS LEAGUE", W / 2, 600, 70, 1800, 0xC8D2E6, 3.0f);
    if (!s.evoReadyName.empty())
      hints.push_back({"A", (s.evoReadyCostume ? "SUIT UP " : "EVOLVE ") + fr_upper(s.evoReadyName), 0x2EA043});
    if (!lg.active) hints.push_back({"X", "YOUR SIDEKICKS (ANYBODY)", 0x2E6FD0});  // r130: pick your sidekick members
  } else if (s.page == 9) {
    // LEVEL: four big cards
    static const char* const kLevels[4] = {"ROOKIE", "PRO", "SUPERSTAR", "LEGEND"};
    static const char* const kOne[4] = {"THE LEAGUE LETS YOU SHINE", "A REAL GAME", "THE CPU PLAYS HARD",
                                        "THEY HUNT YOU ALL GAME"};
    const int now = std::max(1, std::min(4, v.difficulty));
    for (int i = 0; i < 4; ++i) {
      const float x0 = 80 + i * 510.0f, y0 = 260, x1 = x0 + 470, y1 = 1080;
      const bool on = i + 1 == now, cur = s.cursor == i;
      P.box(x0, y0, x1, y1, 30, on ? 0x3B5AA8 : 0x151C36);
      if (cur) P.ring(x0 - 8, y0 - 8, x1 + 8, y1 + 8, 36, 9, 0xFFD23F);
      for (int k = 0; k <= i; ++k) {
        const float sx = 0.5f * (x0 + x1) + (k - 0.5f * i) * 90.0f;
        P.text("*", sx, y0 + 220, 150, 120, on ? 0xFFD23F : 0x6A748A, 4.0f);
      }
      P.text(kLevels[i], 0.5f * (x0 + x1), y0 + 470, 76, x1 - x0 - 40, 0xFFFFFF, 4.0f);
      P.text(kOne[i], 0.5f * (x0 + x1), y0 + 570, 34, x1 - x0 - 40, 0xC8D2E6, 2.0f);
      if (on) P.text("ON", 0.5f * (x0 + x1), y0 + 700, 70, 200, 0xFFD23F, 4.0f);
    }
    if (lg.active && !lg.owner) P.text("ONLINE LEAGUE: ITS OWNER SETS IT", W / 2, 1140, 36, 1200, 0xFFD23F, 2.0f);
    hints.push_back({"A", "SET IT", 0x2EA043});
  } else if (s.page == 10 && lg.active) {
    // LEAGUE: its people, their clubs, who's in
    P.text(std::string("LEAGUE ") + lg.code, W / 2, 220, 60, 900, 0xFFD23F, 3.0f);
    for (int i = 0; i < lg.memberCount; ++i) {
      const PortFranchiseLeagueView::Member& m = lg.members[i];
      const int c = i % 2, r = i / 2;
      const float x0 = 120 + c * 980, y0 = 280 + r * 190.0f;
      P.box(x0, y0, x0 + 940, y0 + 170, 26, m.me ? 0x3B5AA8 : 0x151C36);
      P.image(logo(m.club), x0 + 10, y0 + 10, x0 + 160, y0 + 160, false, m.online || m.me ? 1.0f : 0.45f);
      P.left(fr_upper(m.name) + (m.owner ? " (OWNER)" : ""), x0 + 180, y0 + 60, 50, 560, 0xFFFFFF, 3.0f);
      P.left(place(m.club), x0 + 180, y0 + 122, 34, 560, 0xB8C2D6, 2.0f);
      P.right(!m.online ? "OFFLINE" : (m.ready ? "READY" : "ONLINE"), x0 + 920, y0 + 60, 40, 260,
              !m.online ? 0x8A94AA : 0x6ED36E, 2.0f);
      P.right(m.done ? "PLAYED" : "TO PLAY", x0 + 920, y0 + 122, 34, 260, m.done ? 0xFFD23F : 0xB8C2D6, 2.0f);
    }
    if (lg.owner && lg.canSimRest) hints.push_back({"A", "SIM THE REST", 0x2EA043});
  }
  if (!cardsOwnB) {  // r137: (not while a pack is open)
    hints.push_back({"B", "CAREERS", 0xC0392B});
    if (!s.coopGuest) hints.push_back({"Y", "TRADES", 0xC9A227});
  }
  hub_hints(P, W, 1245, hints);
  xrtext::board(px, w, h, P.v);
  fr_figs_publish(s.serial, std::move(figs.asks), W, H);  // r131
  return true;
}

// r116 (Lance: "for the franchise screens do that all over"): the careers, a new career's club, trades and the
// online league's screens in the hub's look (big pictures, buttons, a few words). Modes 0, 1, 5, 6, 7.
void fr_frame(Pieces& P, float W, float H, const std::string& title, const std::string& sub, const GuestPortrait* logo) {
  P.box(6, 6, W - 6, H - 6, 30, 0xFFD23F);
  P.box(16, 16, W - 16, H - 16, 24, 0x1E2A4E, 0.97f, 0x070A14);
  if (logo != nullptr) P.image(*logo, 40, 24, 180, 164, false);
  P.text(title, W / 2, 80, 86, W - 520, 0xFFF7B0, 5.0f);
  if (!sub.empty()) P.text(sub, W / 2, 152, 38, W - 420, 0xC8D2E6, 2.0f);
}
void fr_button(Pieces& P, float cx, float y0, float wide, const std::string& label, uint32_t rgb, bool on, bool can = true) {
  P.box(cx - wide / 2, y0, cx + wide / 2, y0 + 104, 40, can ? rgb : 0x30384E);
  if (on) P.ring(cx - wide / 2 - 8, y0 - 8, cx + wide / 2 + 8, y0 + 112, 46, 8, 0xFFD23F);
  P.text(label, cx, y0 + 52, 58, wide - 40, can ? 0xFFFFFF : 0x8A94AA, 4.0f);
}
bool draw_fr_screens(uint8_t* px, int w, int h, const FrSnapshot& s) {
  if (s.mode != 0 && s.mode != 1 && s.mode != 5 && s.mode != 6 && s.mode != 7) return false;
  const int nClubs = static_cast<int>(s.clubs.size());
  auto logo = [&](int club) -> const GuestPortrait& {
    static const GuestPortrait none;
    return club >= 0 && club < nClubs ? guest_portrait("club_" + s.clubs[club].id) : none;
  };
  auto club_by_id = [&](const char* id) {
    for (int k = 0; k < nClubs; ++k)
      if (s.clubs[k].id == id) return k;
    return -1;
  };
  const float W = static_cast<float>(w), H = static_cast<float>(h);
  Pieces P;
  FrFigPicture figs;  // r131: the trades' characters, standing in 3D in front of it
  std::vector<HubHint> hints;
  const GuestPortrait& league = guest_portrait("franchise_league");
  // the 32 clubs as a grid (a new career's club, an online league's free ones): conference by conference, a
  // division to four columns
  auto club_grid = [&](const bool* taken, const char (*who)[32]) {
    const float x0 = 40, tw = (W - 80 - 24) / 8.0f, th = 205;
    P.text("CROWN CONFERENCE", W / 2, 214, 34, 800, 0xFFD23F, 2.0f);
    P.text("STAR CONFERENCE", W / 2, 676, 34, 800, 0xFFD23F, 2.0f);
    for (int i = 0; i < nClubs && i < PORT_FR_CLUBS; ++i) {
      int r = 0, c = 0;
      fr_club_place(i, &r, &c);
      const float tx0 = x0 + c * tw + (c >= 4 ? 24 : 0) + 6, ty0 = (r < 2 ? 236 : 698) + (r % 2) * (th + 8);
      const float tx1 = tx0 + tw - 12, ty1 = ty0 + th;
      const bool on = i == s.cursor, gone = taken != nullptr && taken[i];
      if (on) P.glow(tx0 - 60, ty0 - 60, tx1 + 60, ty1 + 60, 0xFFD23F, 0.55f);
      P.box(tx0, ty0, tx1, ty1, 22, on ? 0x2B4690 : 0x151C36);
      if (on) P.ring(tx0 - 4, ty0 - 4, tx1 + 4, ty1 + 4, 26, 7, 0xFFD23F);
      P.image(logo(i), tx0 + 10, ty0 + 6, tx1 - 10, ty1 - 44, false, gone ? 0.3f : 1.0f);
      P.text(fr_upper(s.clubs[i].place), 0.5f * (tx0 + tx1), ty1 - 24, 30, tx1 - tx0 - 16, on ? 0xFFF7B0 : 0xFFFFFF, 2.0f);
      if (gone) {
        P.text("TAKEN", 0.5f * (tx0 + tx1), ty0 + 70, 46, tx1 - tx0 - 20, 0xFF8A80, 3.0f);
        if (who != nullptr && who[i][0] != '\0') P.text(fr_upper(who[i]), 0.5f * (tx0 + tx1), ty0 + 118, 28, tx1 - tx0 - 20, 0xFFD23F, 2.0f);
      }
    }
    if (s.cursor >= 0 && s.cursor < nClubs) {
      const FrClubInfo& c = s.clubs[s.cursor];
      P.text(fr_upper(c.name) + "   -   " + fr_upper(c.conf) + " - " + fr_upper(c.div), W / 2, 1172, 44, W - 200, 0xFFFFFF, 3.0f);
    }
  };
  if (s.mode == 0) {
    // the careers: three slots, the co-op one (a friend in the room or one saved), the online league
    fr_frame(P, W, H, "FRANCHISE MODE", "STRIKERS SUPER LEAGUE", &league);
    std::vector<int> order = {0, 1, 2};
    if (s.coopShown) order.push_back(PORT_FR_COOP_SLOT);
    order.push_back(PORT_FR_LEAGUE_SLOT);
    const int n = static_cast<int>(order.size());
    const float gap = 30, cw = (W - 80 - gap * (n - 1)) / n, y0 = 200, y1 = 1170;
    bool usedHere = false;
    for (int k = 0; k < n; ++k) {
      const int slot = order[k];
      const bool on = s.cursor == slot;
      const float x0 = 40 + k * (cw + gap), x1 = x0 + cw, cx = 0.5f * (x0 + x1);
      if (on) P.glow(x0 - 40, y0 - 40, x1 + 40, y1 + 40, 0xFFD23F, 0.35f);
      P.box(x0, y0, x1, y1, 30, on ? 0x2B4690 : 0x151C36);
      if (on) P.ring(x0 - 5, y0 - 5, x1 + 5, y1 + 5, 34, 8, 0xFFD23F);
      const PortFranchiseSlot* sl = slot < PORT_FR_SLOTS ? &s.slots[slot] : (slot == PORT_FR_COOP_SLOT ? &s.coopSlot : &s.leagueSlot);
      const bool used = slot == PORT_FR_LEAGUE_SLOT ? s.leagueSaved : sl->used;
      std::string head = slot < PORT_FR_SLOTS ? "CAREER " + std::to_string(slot + 1) : (slot == PORT_FR_COOP_SLOT ? "CO-OP" : "ONLINE");
      P.text(head, cx, y0 + 50, 46, cw - 40, 0xFFD23F, 3.0f);
      std::string action;
      bool can = true;
      if (used) {
        const int club = club_by_id(sl->clubId);
        P.image(logo(club), x0 + 30, y0 + 90, x1 - 30, y0 + 420, false);
        P.text(club >= 0 ? fr_upper(s.clubs[club].place) : fr_upper(sl->club), cx, y0 + 470, 52, cw - 40, 0xFFFFFF, 3.0f);
        P.text("SEASON " + std::to_string(sl->season), cx, y0 + 540, 36, cw - 40, 0xC8D2E6, 2.0f);
        P.text(sl->week > PORT_FR_WEEKS ? std::string("PLAYOFFS") : "WEEK " + std::to_string(sl->week), cx, y0 + 590, 36,
               cw - 40, 0xC8D2E6, 2.0f);
        P.text(std::to_string(sl->wins) + "-" + std::to_string(sl->losses), cx, y0 + 690, 96, cw - 40, 0xFFD23F, 5.0f);
        action = "CONTINUE";
        usedHere = usedHere || on;
      } else {
        P.box(cx - 120, y0 + 160, cx + 120, y0 + 400, 120, 0x26335E);
        P.text("+", cx, y0 + 285, 200, 200, 0xFFD23F, 5.0f);
        action = slot == PORT_FR_LEAGUE_SLOT ? "MAKE / JOIN" : "NEW";
      }
      if (slot == PORT_FR_COOP_SLOT) {
        std::string friendName = fr_upper(s.coopName);
        if (friendName.empty()) friendName = "YOUR FRIEND";
        P.text(s.coopState == 0 ? std::string("NO FRIEND HERE") : "WITH " + friendName, cx, y0 + 760, 32, cw - 40,
               s.coopState == 0 ? 0x8A94AA : 0xFFF7B0, 2.0f);
        switch (s.coopState) {
          case 0: action = "HOST FIRST"; can = false; break;
          case 1: action = "WAIT..."; can = false; break;
          case 2: action = used ? "PLAY" : "NEW"; break;
          case 3: action = "THEY HOST"; can = false; break;
          case 4: action = "JOIN"; break;
          default: action = "GETTING IT..."; can = false; break;
        }
      }
      if (slot == PORT_FR_LEAGUE_SLOT) {
        P.text(used ? "CODE " + s.leagueCode : std::string("UP TO 8 PEOPLE"), cx, y0 + 760, 36, cw - 40, 0xFFF7B0, 2.0f);
        if (used) action = "CONTINUE";
      }
      fr_button(P, cx, y1 - 200, cw - 60, action, 0x2EA043, on && can, can);
      if (on && s.confirmDelete) P.text(slot == PORT_FR_LEAGUE_SLOT ? "X AGAIN: LEAVE IT" : "X AGAIN: DELETE IT", cx, y1 - 50, 34,
                                        cw - 30, 0xFF5A4A, 2.0f);
    }
    hints = {{"L", "MOVE", 0x3A4A70}, {"A", "GO", 0x2EA043}};
    if (usedHere || (s.cursor == PORT_FR_LEAGUE_SLOT && s.leagueSaved)) hints.push_back({"X", s.cursor == PORT_FR_LEAGUE_SLOT ? "LEAVE" : "DELETE", 0x2F6FD0});
    hints.push_back({"B", "CLOSE", 0xC0392B});
  } else if (s.mode == 1) {
    std::string sub = s.slot == PORT_FR_COOP_SLOT ? std::string("NEW CO-OP CAREER")
                      : s.slot == PORT_FR_LEAGUE_SLOT ? "NEW ONLINE LEAGUE (UP TO " + std::to_string(s.lgMax) + " PEOPLE)"
                                                      : "NEW CAREER " + std::to_string(s.slot + 1);
    fr_frame(P, W, H, "CHOOSE YOUR CLUB", sub, &league);
    club_grid(nullptr, nullptr);
    hints = {{"L", "MOVE", 0x3A4A70}, {"A", "THIS CLUB", 0x2EA043}, {"B", "BACK", 0xC0392B}};
  } else if (s.mode == 7) {
    const PortFranchiseLeagueLobby& lb = s.lobby;
    if (lb.state == 3) {
      fr_frame(P, W, H, "PICK A FREE CLUB",
               std::string("LEAGUE ") + lb.code + "  -  " + std::to_string(lb.members) + " OF " + std::to_string(lb.max) + " PEOPLE" +
                   (lb.error[0] != '\0' ? "  -  " + fr_upper(lb.error) : std::string()),
               &league);
      club_grid(lb.taken, lb.takenBy);
      hints = {{"L", "MOVE", 0x3A4A70}, {"A", "THIS CLUB", 0x2EA043}, {"B", "BACK", 0xC0392B}};
    } else {
      fr_frame(P, W, H, std::string("LEAGUE ") + lb.code, "JOINING WITH A CODE", &league);
      std::string what;
      switch (lb.state) {
        case 1: what = "CONNECTING..."; break;
        case 2: what = "NO LEAGUE WITH THAT CODE"; break;
        case 4: what = "JOINING..."; break;
        case 5: what = "JOINED!"; break;
        case 6: what = "THAT LEAGUE IS FULL"; break;
        default: what = "..."; break;
      }
      P.text(what, W / 2, 560, 110, W - 300, 0xFFF7B0, 6.0f);
      if (lb.owner[0] != '\0') P.text("ITS OWNER: " + fr_upper(lb.owner), W / 2, 720, 50, W - 400, 0xFFFFFF, 3.0f);
      if (lb.max > 0 && lb.state != 2)
        P.text(std::to_string(lb.members) + " OF " + std::to_string(lb.max) + " PEOPLE", W / 2, 800, 46, W - 400, 0xC8D2E6, 2.0f);
      if (lb.error[0] != '\0') P.text(fr_upper(lb.error), W / 2, 900, 38, W - 400, 0xFF8A80, 2.0f);
      hints = {{"B", "BACK", 0xC0392B}};
    }
  } else if (s.mode == 6) {
    // make a league (how many people) or join one by its code
    fr_frame(P, W, H, "ONLINE LEAGUE", "UP TO 8 PEOPLE, ONE CLUB EACH, ONE SEASON TOGETHER", &league);
    for (int k = 0; k < 2; ++k) {
      const bool on = s.cursor == k;
      const float x0 = k == 0 ? 120 : W / 2 + 40, x1 = k == 0 ? W / 2 - 40 : W - 120, cx = 0.5f * (x0 + x1);
      if (on) P.glow(x0 - 40, 180, x1 + 40, 1170, 0xFFD23F, 0.35f);
      P.box(x0, 210, x1, 1150, 30, on ? 0x2B4690 : 0x151C36);
      if (on) P.ring(x0 - 5, 205, x1 + 5, 1155, 34, 8, 0xFFD23F);
      P.text(k == 0 ? "MAKE ONE" : "JOIN ONE", cx, 290, 76, x1 - x0 - 60, 0xFFF7B0, 4.0f);
      if (k == 0) {
        P.text("PEOPLE", cx, 420, 44, 400, 0xC8D2E6, 2.0f);
        P.text(std::to_string(s.lgMax), cx, 600, 260, 400, 0xFFFFFF, 7.0f);
        if (on) {
          P.text("UP", cx, 470, 34, 200, 0xFFD23F, 2.0f);
          P.text("DOWN", cx, 750, 34, 200, 0xFFD23F, 2.0f);
        }
        P.text("YOUR FRIENDS JOIN WITH THE CODE", cx, 850, 34, x1 - x0 - 80, 0xC8D2E6, 2.0f);
        fr_button(P, cx, 960, x1 - x0 - 160, "MAKE IT", 0x2EA043, on);
      } else {
        P.text("THE CODE YOUR FRIEND GAVE YOU", cx, 420, 36, x1 - x0 - 80, 0xC8D2E6, 2.0f);
        const float bw = 104, gap = 18, bx0 = cx - 0.5f * (6 * bw + 5 * gap);
        for (int i = 0; i < 6; ++i) {
          const char ch = i < static_cast<int>(s.lgTyped.size()) ? s.lgTyped[i] : 'A';
          const bool here = on && s.lgStage == 1 && i == s.lgPos;
          const float bx = bx0 + i * (bw + gap);
          P.box(bx, 500, bx + bw, 660, 18, here ? 0xFFD23F : 0x0B1020);
          P.ring(bx, 500, bx + bw, 660, 18, 4, 0x6A748A);
          P.text(std::string(1, ch), bx + bw / 2, 580, 110, bw - 10, here ? 0x14141E : 0xFFFFFF, here ? 0.0f : 3.0f);
        }
        P.text("THEN PICK A FREE CLUB", cx, 850, 34, x1 - x0 - 80, 0xC8D2E6, 2.0f);
        fr_button(P, cx, 960, x1 - x0 - 160, on && s.lgStage == 1 ? "JOIN" : "TYPE THE CODE", 0x2EA043, on);
      }
    }
    if (s.lgStage == 1)
      hints = {{"L", "LETTER", 0x3A4A70}, {"A", "JOIN", 0x2EA043}, {"B", "BACK", 0xC0392B}};
    else
      hints = {{"L", "MAKE OR JOIN", 0x3A4A70}, {"A", "CHOOSE", 0x2EA043}, {"B", "BACK", 0xC0392B}};
  } else {
    // trades: what you give (your captain, backup, sidekicks), then who you could get and whether they'd say yes
    const PortFranchiseView& v = s.view;
    const int me = v.myClub;
    fr_frame(P, W, H, "TRADES", me >= 0 && me < nClubs ? fr_upper(s.clubs[me].name) : std::string(""), &logo(me));
    static const char* const kWhat[3] = {"YOUR CAPTAIN", "YOUR BACKUP", "YOUR SIDEKICKS"};
    const char* keys[3] = {v.captainKey, v.backupKey, v.kindKey};
    auto give_card = [&](int i, float x0, float y0, float x1, float y1, bool on) {
      const bool has = !s.tradeGive[i].empty();
      P.box(x0, y0, x1, y1, 30, on ? 0x2B4690 : 0x151C36);
      if (on) P.ring(x0 - 5, y0 - 5, x1 + 5, y1 + 5, 34, 8, 0xFFD23F);
      const float cx = 0.5f * (x0 + x1);
      P.text(kWhat[i], cx, y0 + 50, 42, x1 - x0 - 40, 0xFFD23F, 3.0f);
      if (!has) {
        P.text("NONE", cx, 0.5f * (y0 + y1), 90, x1 - x0 - 40, 0x6A748A, 4.0f);
        return;
      }
      figs.person(P, keys[i], i == 0 ? v.captainShiny : (i == 1 && v.backupShiny), x0 + 40, y0 + 90, x1 - 40, y1 - 240);  // r131
      P.text(fr_upper(s.tradeGive[i]), cx, y1 - 190, 44, x1 - x0 - 40, 0xFFFFFF, 3.0f);
      P.text(std::to_string(s.tradeGiveValue[i]), cx, y1 - 105, 90, x1 - x0 - 40, 0xFFD23F, 5.0f);
      P.text("VALUE", cx, y1 - 40, 30, 300, 0xC8D2E6, 2.0f);
    };
    if (s.tradeStage == 0) {
      for (int i = 0; i < 3; ++i) {
        const float x0 = 100 + i * 670.0f;
        give_card(i, x0, 210, x0 + 620, 1080, s.cursor == i);
      }
      hints = {{"L", "WHO", 0x3A4A70}, {"A", "SEE OFFERS", 0x2EA043}, {"B", "BACK", 0xC0392B}};
    } else {
      give_card(s.tradeWhat, 60, 210, 640, 1080, false);
      P.text("FOR", 720, 640, 60, 120, 0xFFD23F, 4.0f);
      const int n = static_cast<int>(s.offers.size());
      const int rows = 7;
      const int first = std::max(0, std::min(s.cursor - rows / 2, n - rows));
      for (int i = first; i < n && i < first + rows; ++i) {
        const PortFranchiseTradeOffer& o = s.offers[i];
        const float y0 = 210 + (i - first) * 126.0f, x0 = 800, x1 = W - 60;
        const bool on = i == s.cursor;
        P.box(x0, y0, x1, y0 + 114, 24, on ? 0x2B4690 : 0x151C36);
        if (on) P.ring(x0 - 4, y0 - 4, x1 + 4, y0 + 118, 28, 7, 0xFFD23F);
        P.image(logo(o.club), x0 + 10, y0 + 7, x0 + 110, y0 + 107, false);
        figs.person(P, o.key, o.shiny, x0 + 120, y0 + 4, x0 + 230, y0 + 110);  // r131: in 3D
        std::string t = fr_upper(o.name) + (o.shiny ? "  SHINY" : "");
        P.left(t, x0 + 250, y0 + 40, 42, 560, o.fair ? 0xFFFFFF : 0x8A94AA, 3.0f);
        P.left("LV " + std::to_string(o.level) + (s.tradeWhat != 2 ? (o.backup ? "  -  BACKUP" : "  -  CAPTAIN") : ""), x0 + 250,
               y0 + 86, 30, 560, 0xB8C2D6, 2.0f);
        P.right(std::to_string(o.value), x1 - 200, y0 + 57, 64, 160, 0xFFD23F, 4.0f);
        P.box(x1 - 170, y0 + 30, x1 - 20, y0 + 84, 27, o.fair ? 0x2EA043 : 0x8E2A2A);
        P.text(o.fair ? "YES" : "NO", x1 - 95, y0 + 57, 38, 130, 0xFFFFFF, 2.0f);
      }
      if (n == 0) P.text("NOBODY TO TRADE WITH", 1400, 600, 70, 1100, 0xC8D2E6, 3.0f);
      hints = {{"L", "WHO", 0x3A4A70}, {"A", "OFFER", 0x2EA043}, {"B", "BACK", 0xC0392B}};
    }
    if (!s.tradeOpen) {
      P.box(W / 2 - 520, 1100, W / 2 + 520, 1180, 40, 0x8E2A2A);
      P.text("THE TRADE WINDOW IS SHUT (OPEN TO WEEK 12 + AFTER THE SEASON)", W / 2, 1140, 36, 1000, 0xFFFFFF, 2.0f);
    } else if (!s.tradeMsg.empty()) {
      P.box(W / 2 - 620, 1100, W / 2 + 620, 1180, 40, s.tradeMsg.compare(0, 4, "DONE") == 0 ? 0x2EA043 : 0x8E2A2A);
      P.text(fr_upper(s.tradeMsg), W / 2, 1140, 40, 1200, 0xFFFFFF, 2.0f);
    } else {
      P.text("THEY SAY YES WHEN THEY GET 90% OF WHAT THEY GIVE", W / 2, 1140, 32, 1400, 0x9AA6BE, 2.0f);
    }
  }
  hub_hints(P, W, 1245, hints);
  xrtext::board(px, w, h, P.v);
  fr_figs_publish(s.serial, std::move(figs.asks), W, H);  // r131 (none but the trades')
  return true;
}
// r70: the franchise hub's picture (a drawing thread): the careers, a new career's club, or the hub's
// pages (the club, the standings, last week's results). Logos: guests/portraits/club_<id>.png and
// franchise_league.png (tools/outside/franchise_data.py).
void draw_franchise(uint8_t* px, int w, int h, const FrSnapshot& s) {
  if (s.mode != 2 && s.mode != 5) fr_figs_publish(s.serial, std::vector<FrFigAsk>(), 1.0f, 1.0f);  // r131: nobody in 3D
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
  if (draw_fr_screens(px, w, h, s)) return;  // r116: the careers, club, trades and league screens' new look
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
  // The hub. r113: its new look (draw_hub: pills, pictures, a few words)
  if (draw_hub(px, w, h, s)) return;
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
      // r131: the rest of their players by name (their co-captain; their sidekicks, members by their own)
      if (v.oppCoCaptain[0] != '\0') next.lines.push_back(line("THEIR CO-CAPTAIN: " + std::string(v.oppCoCaptain)));
      if (v.oppSidekicks[0][0] != '\0') {
        xrtext::FrLine osk = line("   " + std::string(v.oppSidekicks[0]) + ", " + v.oppSidekicks[1] + ", " + v.oppSidekicks[2]);
        osk.dim = true;
        next.lines.push_back(osk);
      }
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
        if (r.ready)  // r95 (r111: a Marvel hero's next costume in costume words)
          left += r.costume ? "  NEW COSTUME READY!" : "  READY TO EVOLVE!";
        else
          left += r.evoLevel > 0 ? "  > " + std::string(r.evoName) + " AT " + std::to_string(r.evoLevel)
                                 : (r.costume ? "  ALL COSTUMES" : "  FINAL FORM");
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
      c.lines.push_back(line("NO POKEMON OR HEROES IN THIS LEAGUE"));
      cards.push_back(c);
    }
    if (!s.evoReadyName.empty())  // r95 (r111: a hero suits up)
      hint += (s.evoReadyCostume ? "   -   A: SUIT UP " : "   -   A: EVOLVE ") + fr_upper(s.evoReadyName);
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
                   bool costume, const std::string& side, float glow, int glowFrom, float sideGlow) {
  auto plural = [](int n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "S"); };
  std::string line = plural(goals, "GOAL") + ", " + plural(assists, "ASSIST") + (won ? "  -  WINNING" : "") + "  -  ";
  // r87: "LEVEL UP!" while it glows, not for the rest of the match
  const bool levelUp = glow >= 0.0f;
  bool gold = false;
  // r110: a Marvel hero's next costume in costume words (r111: told by franchise.cpp, not guessed from the names)
  if (evolves) {
    line += std::string(costume ? "NEW COSTUME EARNED: " : "EVOLUTION EARNED: ") + evoName + " (AFTER THE MATCH)";
    gold = true;
  } else if (evoLevel > 0) {
    line += std::string(levelUp ? "LEVEL UP!  " : "") + (costume ? "NEW COSTUME (" + std::string(evoName) + ")"
                                                                 : "EVOLVES INTO " + std::string(evoName)) +
            " AT LV " + std::to_string(evoLevel);
  } else {
    line += levelUp ? "LEVEL UP!" : (costume ? "ALL COSTUMES" : pokemon ? "FINAL FORM" : "LEVELING UP");
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
                l.evoLevel, l.evoName, l.pokemon, l.costume, side, glow.t[0], glow.from[0], glow.t[2]);
  if (l.co)
    draw_live_row(px + static_cast<size_t>(w) * rowH * 4, w, h - rowH, l.coName, l.coLevel, l.coXp, l.coNewLevel,
                  l.coNewXp, l.coGain, l.coGoals, l.coAssists, l.won, l.coEvolves, l.coEvoLevel, l.coEvoName,
                  l.coPokemon, l.coCostume, "CO-CAPTAIN", glow.t[1], glow.from[1], -1.0f);
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
  // r116 (Lance: "for the franchise screens do that all over"): the match's intro card, the hub's look: both
  // clubs' logos big, VS, the week (or the playoff game and the series), your nemesis if it starts
  const bool playoff = c.round > 0;
  const float W = static_cast<float>(w), H = static_cast<float>(h);
  Pieces P;
  P.box(6, 6, W - 6, H - 6, 28, 0xFFD23F);
  P.box(14, 14, W - 14, H - 14, 22, playoff ? 0x2A1E52 : 0x1E2A4E, 0.97f, 0x070A14);
  for (int side = 0; side < 2; ++side) {
    const int club = c.club[side];
    const float cx = side == 0 ? 300.0f : W - 300.0f;
    P.glow(cx - 280, 20, cx + 280, 560, PortFranchiseClubColor(club, 0), 0.6f);
    P.image(guest_portrait(std::string("club_") + PortFranchiseClubId(club)), cx - 200, 40, cx + 200, 430, false);
    P.text(fr_upper(PortFranchiseClubPlace(club)), cx, 480, 54, 520, 0xFFFFFF, 4.0f);
    std::string rec = std::to_string(c.wins[side]) + "-" + std::to_string(c.losses[side]);
    if (playoff) rec = "#" + std::to_string(c.seed[side]) + " SEED  -  " + std::to_string(c.seriesWins[side]) + " WINS";
    P.text(rec, cx, 548, 40, 520, 0xFFD23F, 3.0f);
  }
  P.text("VS", W / 2, 200, 170, 300, 0xFFF7B0, 7.0f);
  P.text(playoff ? fr_upper(c.roundName) : "WEEK " + std::to_string(c.week), W / 2, 330, 40, 420, 0xFFFFFF, 3.0f);
  if (playoff) {
    P.text("GAME " + std::to_string(c.gameNo) + " OF 5", W / 2, 385, 36, 420, 0xC8D2E6, 2.0f);
    if (c.seriesWins[0] == 2 || c.seriesWins[1] == 2) {
      P.box(W / 2 - 200, 420, W / 2 + 200, 480, 30, 0xFFD23F);
      P.text(c.seriesWins[0] == 2 && c.seriesWins[1] == 2 ? "WINNER TAKES ALL" : "CLOSE-OUT GAME", W / 2, 450, 34, 380, 0x14141E);
    }
  }
  if (c.spectating) P.text("WATCHING", W / 2, 400, 34, 300, 0x9AA6BE, 2.0f);
  {
    // r137: tonight's weather (the match's: PortWeatherNewMatch ran at the match's set-up, before this card)
    const int wx = PortWeatherCurrent();
    const char* label = PortWeatherLabel(wx);
    if (label[0] != '\0') {
      const bool pill = playoff && (c.seriesWins[0] == 2 || c.seriesWins[1] == 2);
      const float y = playoff ? (pill ? 493.0f : 440.0f) : (c.spectating ? 446.0f : 395.0f);
      const uint32_t col = (wx & (PORT_WX_RAIN | PORT_WX_SNOW)) == 0 ? 0xFFE9A0 : ((wx & PORT_WX_SNOW) ? 0xF2F8FF : 0xA8D4FF);
      P.text(label, W / 2, y, pill ? 24 : 34, 420, col, 2.0f);
    }
  }
  if (c.nemesis) {
    // the face-off: your nemesis between the two, in red
    const float nx = c.nemesisSide == 0 ? 560.0f : W - 560.0f;
    P.glow(nx - 140, 300, nx + 140, 600, 0xFF3A2A, 0.55f);
    P.image(guest_portrait(c.nemesisKey), nx - 110, 350, nx + 110, 600, true);
    P.box(W / 2 - 230, 500, W / 2 + 230, 610, 30, 0x6E1414);
    P.ring(W / 2 - 230, 500, W / 2 + 230, 610, 30, 4, 0xFF5A4A);
    P.text("NEMESIS: " + fr_upper(c.nemesisName), W / 2, 535, 34, 440, 0xFFFFFF, 2.0f);
    P.text(fr_upper(c.nemesisTitle) + "  -  BEAT YOU " + std::to_string(c.nemesisBeatYou) + "X", W / 2, 580, 26, 440,
           0xFFB0A8, 1.5f);
  }
  xrtext::board(px, w, h, P.v);
}
// r116: the playoff series under the big screen: both logos, the game, the series score (was a line of words)
void draw_franchise_series(uint8_t* px, int w, int h, const PortFranchiseCard& c) {
  const float W = static_cast<float>(w), H = static_cast<float>(h);
  Pieces P;
  P.box(3, 3, W - 3, H - 3, H / 2 - 3, 0xFFD23F);
  P.box(8, 8, W - 8, H - 8, H / 2 - 8, 0x1E2A4E, 0.97f, 0x070A14);
  for (int side = 0; side < 2; ++side) {
    const float x0 = side == 0 ? 20.0f : W - 20.0f - (H - 16);
    P.image(guest_portrait(std::string("club_") + PortFranchiseClubId(c.club[side])), x0, 8, x0 + H - 16, H - 8, false);
    P.text(std::to_string(c.seriesWins[side]), side == 0 ? 20 + H + 30 : W - 20 - H - 30, H / 2, 0.7f * H, 80, 0xFFD23F, 3.0f);
  }
  P.text(fr_upper(c.roundName) + "  -  GAME " + std::to_string(c.gameNo), W / 2, H / 2, 0.42f * H, W - 2 * H - 200, 0xFFFFFF, 2.0f);
  xrtext::board(px, w, h, P.v);
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
  const int choice = e.options >= 2 ? std::max(0, std::min(e.options - 1, s.evoChoice)) : 0;  // r130: up to 5
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
  const xrtext::EvoImage alt = img(e.options >= 2 ? e.toKey[1] : e.toKey[0]);
  xrtext::EvoImage all[5];  // r130: every option (Eevee's five)
  for (int k = 0; k < std::min(e.options, 5); ++k) all[k] = img(e.toKey[k]);
  const std::string fromName = fr_upper(e.fromName), toName = fr_upper(e.toName[choice]);
  std::string title, line, hint;
  // r110 (Lance: Marvel heroes' "alternate costumes being rewarded as xp is gained"): a hero's next costume rides the
  // evolution (roster "franchise" forms, evolutions.txt levels), in costume words
  if (std::strncmp(e.fromKey, "mua_", 4) == 0) {
    switch (s.evoStage) {
    case 0:
      title = "NEW COSTUME! " + fromName + " EARNED A NEW LOOK!";
      line = "LV " + std::to_string(e.level) + ": " + fromName + " > " + toName;
      hint = "A: SUIT UP (WATCH IT)   -   B: NOT NOW (SUIT UP LATER: EVO WATCH PAGE, A)";
      break;
    case 1:
      title = fromName + " IS SUITING UP!";
      break;
    case 2:
      title = "NEW COSTUME!";
      line = "YOUR " + fromName + " IS NOW " + toName + "! (ON THE CHARACTER SELECT MENU IN EVERY MODE)";
      hint = "A: CONTINUE";
      break;
    default:
      title = "OKAY! " + fromName + " WILL WAIT";
      line = "SUIT UP ANY TIME: EVO WATCH PAGE, A";
      hint = "A: CONTINUE";
      break;
    }
    const float tpc = s.evoStage == 1 ? 0.8f * t : t;
    xrtext::evolution_scene(px, w, h, from, to, alt, s.evoStage, tpc, choice, title.c_str(), line.c_str(), hint.c_str(),
                            false, e.options);
    return;
  }
  switch (s.evoStage) {
  case 0:  // r95: the EVO page
    title = "EVO! " + fromName + " IS READY TO EVOLVE!";
    if (e.options > 2) {  // r130
      line = "LV " + std::to_string(e.level) + ": " + std::to_string(e.options) + " WAYS TO EVOLVE - WHICH ONE?";
      hint = "LEFT / RIGHT: CHOOSE   -   A: EVOLVE INTO " + toName + "   -   B: NOT NOW";
    } else if (e.options == 2) {
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
                          s.evoNewShiny || e.shiny, e.options, all);
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
// r130 (Lance: "you can have anybody play a sidekick now" ... "give "sidekicks" their own tab"): role 2 = a sidekick
// slot: a Sidekicks tab (the Strikers sidekicks and the Charged sidekick guests), then every guest captain's tab.
const std::vector<MenuEntry>& menu_entries(int role) {
  static std::vector<MenuEntry> built[3];
  static bool done[3] = {false, false, false};
  static std::mutex mutex;  // the game thread and the panel's drawing thread both ask
  if (role < 0 || role > 2) role = 0;
  std::lock_guard<std::mutex> lock(mutex);
  if (done[role]) return built[role];
  done[role] = true;
  std::vector<MenuEntry>& out = built[role];
  const int cols = role == 1 ? kMenuSidekickCols : kMenuCaptainCols;
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
  } else if (role == 2) {
    // r130: the Sidekicks tab: the Strikers four, then the Charged sidekick guests (Boo, Dry Bones ...)
    static const struct { int id; const char* file; const char* name; } kSk[4] = {
        {0, "toad", "Toad"}, {1, "koopa", "Koopa"}, {2, "hammerbro", "Hammer Bro"}, {3, "birdo", "Birdo"}};
    for (const auto& s : kSk) {
      MenuEntry e;
      e.pick = PORT_GUEST_ORIGINAL_SIDEKICK + s.id;
      e.portrait = std::string("strikers_") + s.file;
      e.name = s.name;
      e.tag = "Sidekicks";
      e.frame = 0x1E6A3A;
      add(e);
    }
    // r130 (Lance: "you coulda just let ANY spot be ANY character"): the eight Strikers captains themselves, each its own
    // class in the slot (include/port/lineup.h; once on the field: sk_taken)
    static const struct { int team; const char* file; const char* name; } kCaps[8] = {
        {3, "mario", "Mario"}, {2, "luigi", "Luigi"}, {4, "peach", "Peach"}, {0, "daisy", "Daisy"},
        {7, "yoshi", "Yoshi"}, {1, "donkeykong", "Donkey Kong"}, {6, "wario", "Wario"}, {5, "waluigi", "Waluigi"}};
    for (const auto& c : kCaps) {
      MenuEntry e;
      e.pick = PORT_GUEST_ORIGINAL + c.team;
      e.team = c.team;
      e.portrait = std::string("strikers_") + c.file;
      e.name = c.name;
      e.tag = "Sidekicks";
      e.frame = 0x1E6A3A;
      add(e);
    }
    for (int k = 0; k < PortGuestRosterCount(1); ++k) {
      const int index = PortGuestRosterAt(1, k);
      MenuEntry e;
      e.pick = index;
      const char* pack = PortGuestRosterPack(index);
      if (pack != nullptr && std::strncmp(pack, "cap_", 4) == 0) continue;  // r130: the real captains are above
      e.portrait = pack != nullptr ? pack : "";
      const char* name = PortGuestRosterName(index);
      e.name = name != nullptr ? name : e.portrait;
      e.tag = "Sidekicks";
      e.frame = 0x1E6A3A;
      add(e);
    }
    new_section();
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
  // The guests by game: Charged (beside the Strikers captains), Melee, Melee's Pokemon; r107: Marvel: Ultimate
  // Alliance's heroes (Lance's overnight ask, 2026-10-27), a row of their own in Marvel red.
  static const struct { const char* game; const char* tag; uint32_t frame; } kGames[4] = {
      {"Charged", "Charged", 0x7A4A1E}, {"Melee", "Melee", 0x23407A}, {"Melee Pokemon", "Pokemon", 0x6E2440},
      {"Ultimate Alliance", "Marvel", 0x9A1414}};
  const int rr = role == 2 ? 0 : role;  // r130: a sidekick slot lists the guest captains
  const int n = PortGuestRosterCount(rr);
  // r70c: each evolved Pokemon right after the form it evolves from (Charmander, Charmeleon, Charizard)
  std::vector<int> order;
  std::function<void(const char*)> evolutions_of = [&](const char* from) {
    for (int k = 0; k < n; ++k) {
      const int idx = PortGuestRosterAt(rr, k);
      if (PortGuestRosterEvolved(idx) && std::strcmp(PortGuestRosterEvolvesFrom(idx), from) == 0) {
        order.push_back(idx);
        evolutions_of(PortGuestRosterPack(idx));
      }
    }
  };
  for (int i = 0; i < n; ++i) {
    const int index = PortGuestRosterAt(rr, i);
    if (PortGuestRosterEvolved(index) && PortGuestRosterEvolvesFrom(index)[0] != '\0') continue;
    order.push_back(index);
    evolutions_of(PortGuestRosterPack(index));
  }
  for (int i = 0; i < n; ++i) {  // (any evolved one whose earlier form isn't on this headset: at the end)
    const int index = PortGuestRosterAt(rr, i);
    if (std::find(order.begin(), order.end(), index) == order.end()) order.push_back(index);
  }
  for (int gsec = 0; gsec < 4; ++gsec) {
    if (gsec > 0) new_section();
    for (int index : order) {
      const char* pk = PortGuestRosterPack(index);
      if (pk != nullptr && std::strncmp(pk, "cap_", 4) == 0) continue;  // r130: never (the real captains play now)
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
// r130: a sidekick slot's menu entry it can't be: a second Strikers sidekick kind (a team has one), a named character
// already in an earlier slot of this team, this side's own captain. r130 (any slot any character): a Strikers captain
// plays its own class, once on the field: not one in `capTaken` (a bit per eTeamID: either team's captain, the
// co-captains, the other side's slots - PortGuestSlotCaptainsTaken), nor one an earlier slot of this team has.
bool sk_taken(const MenuEntry& e, const int* picks, int slot, int first, int captainGuest, unsigned capTaken) {
  const bool native = e.pick >= PORT_GUEST_ORIGINAL_SIDEKICK && e.pick < PORT_GUEST_ORIGINAL_SIDEKICK + 4;
  if (e.team >= 0 && e.team < 8 && (capTaken & (1u << e.team)) != 0) return true;
  for (int k = std::max(0, first); k < slot && k < 3; ++k) {
    const int p = picks[k];
    const bool pNative = p >= PORT_GUEST_ORIGINAL_SIDEKICK && p < PORT_GUEST_ORIGINAL_SIDEKICK + 4;
    if (native && pNative && p != e.pick) return true;
    if (!native && (e.tag != "Sidekicks" || e.team >= 0) && p == e.pick) return true;
  }
  return !native && captainGuest >= 0 && e.pick == captainGuest;
}
// r130: the Strikers captains a franchise club's own picks hold (its captain, its backup: menu picks), a bit per eTeamID
unsigned fr_cap_bits(int captainPick, int backupPick) {
  unsigned bits = 0;
  for (int p : {captainPick, backupPick})
    if (p >= PORT_GUEST_ORIGINAL && p < PORT_GUEST_ORIGINAL + 8) bits |= 1u << (p - PORT_GUEST_ORIGINAL);
  return bits;
}
// r130: a menu entry the X button may put in every slot left: a Strikers sidekick or a Charged sidekick guest (never a
// named character: a Strikers captain, a guest captain)
bool sk_repeatable(const MenuEntry& e) { return e.tag == "Sidekicks" && e.team < 0; }
// r130: the three sidekick picks to the pick screen. All the same Strikers sidekick or the same Charged sidekick guest:
// that one pick, as before. Otherwise one pick for the three (PORT_GUEST_TRIO + the class they play as): a Strikers
// sidekick among them sets the class; else the class most of them suit (a Charged sidekick guest's own; a guest
// captain's by build: Yoshi -> Toad, Mario / Luigi -> Koopa, Wario / DK -> Hammer Bro, Peach / Daisy / Waluigi -> Birdo),
// a tie the first slot's.
void sk_commit(int side, const int* menuPicks, int first) {
  const int ref = menuPicks[first];
  bool same = true;
  for (int k = first; k < 3; ++k) same = same && menuPicks[k] == ref;
  const bool refNative = ref >= PORT_GUEST_ORIGINAL_SIDEKICK && ref < PORT_GUEST_ORIGINAL_SIDEKICK + 4;
  const bool refSidekickGuest = ref >= 0 && ref < PORT_GUEST_ORIGINAL && PortGuestRosterFind(PORT_GUEST_SIDEKICK, PortGuestRosterPack(ref)) == ref;
  if (same && (refNative || refSidekickGuest) && first == 0) {
    PortGuestPickerCommit(side, PORT_GUEST_SIDEKICK, ref);
    XLOG("character select: side %d's three sidekicks: one pick (%d)", side, ref);
    return;
  }
  // r130: online keeps the three too: the match setup carries each side's slots (xr_net.cpp 'L' tails, Game.cpp), the
  // same on both headsets; with an older build on the other headset both play the class's Strikers sidekick
  static const int kByCaptain[8] = {3, 2, 1, 1, 3, 3, 2, 0};  // eTeamID daisy dk luigi mario peach waluigi wario yoshi
  // r130 (any slot any character): the vote is among the slots that play the sidekick class (Charged sidekick guests);
  // the captains (Strikers ones and guest captains: their own classes now) vote by build only when none does - the
  // class one of them falls back to if its own is taken at the match start
  int votes[4] = {0, 0, 0, 0}, firstVote = -1, native = -1;
  int capVotes[4] = {0, 0, 0, 0}, firstCapVote = -1;
  int picks[3] = {-1, -1, -1};
  for (int k = std::max(0, first); k < 3; ++k) {
    const int p = menuPicks[k];
    int v = -1, cv = -1;
    if (p >= PORT_GUEST_ORIGINAL_SIDEKICK && p < PORT_GUEST_ORIGINAL_SIDEKICK + 4) {
      if (native < 0) native = p - PORT_GUEST_ORIGINAL_SIDEKICK;
      picks[k] = -1;  // the Strikers sidekick itself
      continue;
    }
    picks[k] = p;
    if (p >= PORT_GUEST_ORIGINAL && p < PORT_GUEST_ORIGINAL + 8) {
      cv = kByCaptain[p - PORT_GUEST_ORIGINAL];  // a Strikers captain itself
    } else if (p >= 0 && p < PORT_GUEST_ORIGINAL) {
      const int base = PortGuestRosterBase(p);
      if (PortGuestRosterFind(PORT_GUEST_SIDEKICK, PortGuestRosterPack(p)) == p) v = base >= 0 && base < 4 ? base : -1;
      else cv = base >= 0 && base < 8 ? kByCaptain[base] : -1;
    }
    if (v >= 0) {
      ++votes[v];
      if (firstVote < 0) firstVote = v;
    }
    if (cv >= 0) {
      ++capVotes[cv];
      if (firstCapVote < 0) firstCapVote = cv;
    }
  }
  int cls = native;
  if (cls < 0 && firstVote >= 0) {
    cls = firstVote;
    for (int c = 0; c < 4; ++c)
      if (votes[c] > votes[cls]) cls = c;
  }
  if (cls < 0) {
    cls = firstCapVote >= 0 ? firstCapVote : 0;
    for (int c = 0; c < 4; ++c)
      if (capVotes[c] > capVotes[cls]) cls = c;
  }
  PortGuestPickerCommitTrio(side, picks, first, cls);
  XLOG("character select: side %d's sidekicks %d / %d / %d (from slot %d) play as sidekick class %d", side, picks[0], picks[1],
       picks[2], first + 1, cls);
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
// r112 (Lance: "make that fuckin character selection screen way better. shit is waaay too puny"): the menu by
// game, a tab each (in the menu's order: Strikers, Charged, Melee, Pokemon, Marvel), and in each tab its
// characters as families: a character, then its evolutions / costumes (the forms the stick doesn't step
// through: LT / RT do). The menu's entries stay as they were (picking is unchanged); this is how they're shown
// and walked.
constexpr int kSelectCols = 5, kSelectRows = 3;
struct MenuLayout {
  struct Tab {
    std::string name;
    uint32_t rgb = 0;
    std::vector<std::vector<int>> fams;  // menu indexes, the first form first
  };
  std::vector<Tab> tabs;
  std::vector<int> tabOf, famOf, formOf;  // by menu index
};
const MenuLayout& menu_layout(int role) {
  static MenuLayout built[3];  // r130: role 2, a sidekick slot
  static bool done[3] = {false, false, false};
  static std::mutex mutex;
  if (role < 0 || role > 2) role = 0;
  const std::vector<MenuEntry>& menu = menu_entries(role);
  std::lock_guard<std::mutex> lock(mutex);
  if (done[role]) return built[role];
  done[role] = true;
  MenuLayout& L = built[role];
  const int n = static_cast<int>(menu.size());
  L.tabOf.assign(n, 0);
  L.famOf.assign(n, 0);
  L.formOf.assign(n, 0);
  std::unordered_map<std::string, int> byPack;  // a guest's pack -> its menu index
  for (int i = 0; i < n; ++i) {
    const MenuEntry& e = menu[i];
    int t = -1;
    for (int k = 0; k < static_cast<int>(L.tabs.size()); ++k)
      if (L.tabs[k].name == e.tag) t = k;
    if (t < 0) {
      MenuLayout::Tab tab;
      tab.name = e.tag;
      tab.rgb = e.frame;
      L.tabs.push_back(tab);
      t = static_cast<int>(L.tabs.size()) - 1;
    }
    // an evolved form / a costume goes with the first form of its line, when that's in this tab
    int root = -1;
    if (e.team < 0 && e.pick >= 0 && e.pick < PORT_GUEST_ORIGINAL && PortGuestRosterEvolved(e.pick)) {
      std::string from = PortGuestRosterEvolvesFrom(e.pick);
      for (int guard = 0; guard < 8 && !from.empty(); ++guard) {
        auto it = byPack.find(from);
        if (it == byPack.end()) break;
        root = it->second;
        const int rp = menu[root].pick;
        from = rp >= 0 && rp < PORT_GUEST_ORIGINAL && PortGuestRosterEvolved(rp) ? PortGuestRosterEvolvesFrom(rp) : "";
      }
    }
    if (root >= 0 && L.tabOf[root] == t) {
      std::vector<int>& fam = L.tabs[t].fams[L.famOf[root]];
      L.famOf[i] = L.famOf[root];
      L.formOf[i] = static_cast<int>(fam.size());
      fam.push_back(i);
    } else {
      L.famOf[i] = static_cast<int>(L.tabs[t].fams.size());
      L.formOf[i] = 0;
      L.tabs[t].fams.push_back({i});
    }
    L.tabOf[i] = t;
    if (e.team < 0 && !e.portrait.empty()) byPack[e.portrait] = i;
  }
  return L;
}
// r112: the stick (dir 1 left, 2 right, 3 up, 4 down) or LT / RT (form -1 / +1) on the menu: among a tab's
// characters, kSelectCols a row; up off the top row onto the tabs, where left / right changes the game and
// down comes back; LT / RT through the one's forms (round and round). Returns the new menu index.
int menu_nav(int role, int cursor, bool* onTabs, int dir, int form) {
  const MenuLayout& L = menu_layout(role);
  const int n = static_cast<int>(L.tabOf.size());
  if (n == 0 || L.tabs.empty()) return 0;
  cursor = std::max(0, std::min(n - 1, cursor));
  int t = L.tabOf[cursor], f = L.famOf[cursor], k = L.formOf[cursor];
  const std::vector<std::vector<int>>& fams = L.tabs[t].fams;
  if (form != 0) {
    const int size = static_cast<int>(fams[f].size());
    k = ((k + form) % size + size) % size;
    return fams[f][k];
  }
  if (*onTabs) {
    if (dir == 1 && t > 0) return L.tabs[t - 1].fams[0][0];
    if (dir == 2 && t + 1 < static_cast<int>(L.tabs.size())) return L.tabs[t + 1].fams[0][0];
    if (dir == 4) *onTabs = false;
    return cursor;
  }
  const int count = static_cast<int>(fams.size());
  const int c = f % kSelectCols;
  if (dir == 1 && c > 0) f -= 1;
  else if (dir == 2 && c < kSelectCols - 1 && f + 1 < count) f += 1;
  else if (dir == 3) {
    if (f < kSelectCols) {
      *onTabs = true;
      return cursor;
    }
    f -= kSelectCols;
  } else if (dir == 4) {
    if (f + kSelectCols < count) f += kSelectCols;
    else if (f / kSelectCols < (count - 1) / kSelectCols) f = count - 1;
  } else {
    return cursor;
  }
  return fams[f][0];
}
// r112: the menu's stat bars (franchise/cards.txt, tools/outside/menu_cards.py: speed shoot pass tackle) and
// the level each evolved form / costume comes at (franchise/evolutions.txt), read once.
const float* menu_card(const std::string& key) {
  static std::mutex mutex;
  static std::unordered_map<std::string, std::array<float, 4>> cards;
  static bool read = false;
  std::lock_guard<std::mutex> lock(mutex);
  if (!read) {
    read = true;
    const char* home = std::getenv("STRIKERS_HOME");
    FILE* f = home != nullptr ? std::fopen((std::string(home) + "/franchise/cards.txt").c_str(), "r") : nullptr;
    if (f != nullptr) {
      char k[96];
      std::array<float, 4> v{};
      char line[256];
      while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (line[0] == '#') continue;
        if (std::sscanf(line, "%95s %f %f %f %f", k, &v[0], &v[1], &v[2], &v[3]) == 5) cards[k] = v;
      }
      std::fclose(f);
    }
  }
  std::string base = key;
  if (base.size() > 6 && base.compare(base.size() - 6, 6, "_shiny") == 0) base.resize(base.size() - 6);
  auto it = cards.find(base);
  return it != cards.end() ? it->second.data() : nullptr;
}
int menu_form_level(const std::string& pack) {
  static std::mutex mutex;
  static std::unordered_map<std::string, int> levels;
  static bool read = false;
  std::lock_guard<std::mutex> lock(mutex);
  if (!read) {
    read = true;
    const char* home = std::getenv("STRIKERS_HOME");
    FILE* f = home != nullptr ? std::fopen((std::string(home) + "/franchise/evolutions.txt").c_str(), "r") : nullptr;
    if (f != nullptr) {
      char from[96], to[96];
      int level = 0;
      char line[256];
      while (std::fgets(line, sizeof(line), f) != nullptr) {
        if (line[0] == '#') continue;
        if (std::sscanf(line, "%95s %95s %d", from, to, &level) == 3) levels[to] = level;
      }
      std::fclose(f);
    }
  }
  auto it = levels.find(pack);
  return it != levels.end() ? it->second : 0;
}
// r112: the menu drawn: `taken(i)` / `locked(i)` say which entries can't be picked (another side's captain; a
// form not unlocked), `title` its small label, the hints its buttons.
void draw_select(uint8_t* px, int w, int h, int role, int cursor, bool onTabs, const std::string& title,
                 const std::function<bool(int)>& taken, const std::function<bool(int)>& locked,
                 const std::vector<xrtext::SelectHint>& hints) {
  const std::vector<MenuEntry>& menu = menu_entries(role);
  const MenuLayout& L = menu_layout(role);
  const int n = static_cast<int>(menu.size());
  if (n == 0 || L.tabs.empty()) {
    xrtext::character_select(px, w, h, title.c_str(), nullptr, 0, 0, false, nullptr, 0, 0, 0, xrtext::SelectSpot{},
                             hints.data(), static_cast<int>(hints.size()));
    return;
  }
  cursor = std::max(0, std::min(n - 1, cursor));
  const int tab = L.tabOf[cursor], fam = L.famOf[cursor];
  std::vector<xrtext::SelectTab> tabs;
  for (const MenuLayout::Tab& t : L.tabs)
    tabs.push_back(xrtext::SelectTab{t.name.c_str(), t.rgb, static_cast<int>(t.fams.size())});
  std::vector<xrtext::SelectTile> tiles;
  for (const std::vector<int>& f : L.tabs[tab].fams) {
    const MenuEntry& e = menu[f[0]];
    const GuestPortrait& p = guest_portrait(e.portrait);
    xrtext::SelectTile t;
    t.rgba = p.rgba.empty() ? nullptr : p.rgba.data();
    t.w = p.w;
    t.h = p.h;
    t.name = e.name.c_str();
    t.forms = static_cast<int>(f.size());
    t.taken = taken(f[0]);
    tiles.push_back(t);
  }
  const MenuEntry& e = menu[cursor];
  const GuestPortrait& sp = guest_portrait(e.portrait);
  xrtext::SelectSpot spot;
  spot.rgba = sp.rgba.empty() ? nullptr : sp.rgba.data();
  spot.w = sp.w;
  spot.h = sp.h;
  spot.name = e.name.c_str();
  spot.locked = locked(cursor);
  spot.taken = taken(cursor);
  const bool hero = e.tag == "Marvel";
  if (spot.locked) spot.note = hero ? "UNLOCK IN FRANCHISE" : "EVOLVE IN FRANCHISE";
  if (const float* c = menu_card(e.portrait)) {
    spot.hasCard = true;
    for (int k = 0; k < 4; ++k) spot.card[k] = c[k];
  }
  for (int i : L.tabs[tab].fams[fam]) {
    const GuestPortrait& fp = guest_portrait(menu[i].portrait);
    xrtext::SelectForm f;
    f.rgba = fp.rgba.empty() ? nullptr : fp.rgba.data();
    f.w = fp.w;
    f.h = fp.h;
    f.locked = locked(i);
    f.level = f.locked ? menu_form_level(menu[i].portrait) : 0;
    spot.forms.push_back(f);
  }
  spot.formAt = L.formOf[cursor];
  const int rowsAll = (static_cast<int>(tiles.size()) + kSelectCols - 1) / kSelectCols;
  const int firstRow = std::max(0, std::min(fam / kSelectCols - 1, rowsAll - kSelectRows));
  // the LT / RT hint only for one with forms; a hero's are costumes
  std::vector<xrtext::SelectHint> shown;
  for (const xrtext::SelectHint& k : hints) {
    if (std::strcmp(k.glyph, "LT RT") == 0) {
      if (spot.forms.size() < 2) continue;
      shown.push_back({k.glyph, k.rgb, hero ? "COSTUMES" : k.words});
    } else {
      shown.push_back(k);
    }
  }
  xrtext::character_select(px, w, h, title.c_str(), tabs.data(), static_cast<int>(tabs.size()), tab, onTabs, tiles.data(),
                           static_cast<int>(tiles.size()), fam, firstRow, spot, shown.data(), static_cast<int>(shown.size()));
}
// r112: the hints, a button and a word or two each (the menu's own way to say it)
constexpr uint32_t kHintStick = 0x3A4A70, kHintA = 0x2EA043, kHintB = 0xC0392B, kHintX = 0x2F6FD0;
// r75: a new career's own players on the character select menu (the franchise hub's mode 4): your
// captain, your backup captain, your sidekicks. Evolved forms can't start a career (their silhouette, as
// on the menu); for the backup, the captain you just picked is darkened.
// r130: the franchise's three sidekick picks (stages 2-4 of the hub's mode 4): the club's kind for a new career (a
// Strikers sidekick among them, else a Charged sidekick guest among them, else the class most of them suit) and the
// members. Returns the kind pick for PortFranchiseNewPicked.
int fr_kind_from(const int* picks) {
  static const int kByCaptain[8] = {3, 2, 1, 1, 3, 3, 2, 0};  // eTeamID daisy dk luigi mario peach waluigi wario yoshi
  int votes[4] = {0, 0, 0, 0}, firstVote = -1;
  for (int k = 0; k < 3; ++k)
    if (picks[k] >= PORT_GUEST_ORIGINAL_SIDEKICK && picks[k] < PORT_GUEST_ORIGINAL_SIDEKICK + 4) return picks[k];
  // (r130: a Strikers captain as a sidekick, cap_<name>, is never a club's kind: it votes its class below)
  auto capSk = [](int p) {
    const char* pk = PortGuestRosterPack(p);
    return pk != nullptr && std::strncmp(pk, "cap_", 4) == 0;
  };
  for (int k = 0; k < 3; ++k)
    if (picks[k] >= 0 && picks[k] < PORT_GUEST_ORIGINAL && !capSk(picks[k]) &&
        PortGuestRosterFind(PORT_GUEST_SIDEKICK, PortGuestRosterPack(picks[k])) == picks[k])
      return picks[k];
  for (int k = 0; k < 3; ++k) {
    if (picks[k] >= PORT_GUEST_ORIGINAL && picks[k] < PORT_GUEST_ORIGINAL + 8) {  // r130: a Strikers captain, by build
      const int v = kByCaptain[picks[k] - PORT_GUEST_ORIGINAL];
      ++votes[v];
      if (firstVote < 0) firstVote = v;
      continue;
    }
    if (picks[k] < 0 || picks[k] >= PORT_GUEST_ORIGINAL) continue;
    const int base = PortGuestRosterBase(picks[k]);
    const bool sideRole = PortGuestRosterFind(PORT_GUEST_SIDEKICK, PortGuestRosterPack(picks[k])) == picks[k];
    if (base < 0 || base >= (sideRole ? 4 : 8)) continue;
    const int v = sideRole ? base : kByCaptain[base];
    ++votes[v];
    if (firstVote < 0) firstVote = v;
  }
  int cls = firstVote >= 0 ? firstVote : 0;
  for (int c = 0; c < 4; ++c)
    if (votes[c] > votes[cls]) cls = c;
  return PORT_GUEST_ORIGINAL_SIDEKICK + cls;
}
// r131 (Lance: "each character is only chosen once per franchise"): the franchise pick menu's characters already taken
// (PortFranchisePicksTaken): one your club holds in this pick - your captain, backup, an earlier sidekick slot, its whole
// evolution line - and in the member editor one another club has (greyed like the r130 ones; A / X won't take it)
int fr_held(int* held) {
  int n = 0;
  if (g.frPickStage >= 1 && g.frPickCap >= 0) held[n++] = g.frPickCap;
  if (g.frPickStage >= 2 && g.frPickBk >= 0) held[n++] = g.frPickBk;
  for (int k = 0; k < g.frPickStage - 2 && k < 3; ++k)
    if (g.frPickSk[k] >= 0) held[n++] = g.frPickSk[k];
  return n;
}
bool fr_pick_taken(int pick) {
  int held[5];
  const int n = fr_held(held);
  unsigned char t = 0;
  PortFranchisePicksTaken(&pick, 1, held, n, g.frEditMembers, &t);
  return t != 0;
}
void fr_pick_taken_fill(std::vector<unsigned char>& out) {
  const int stage = std::max(0, std::min(4, g.frPickStage));
  const std::vector<MenuEntry>& menu = menu_entries(stage >= 2 ? 2 : 0);
  std::vector<int> picks(menu.size());
  for (size_t i = 0; i < menu.size(); ++i) picks[i] = menu[i].pick;
  int held[5];
  const int n = fr_held(held);
  out.assign(menu.size(), 0);
  if (!picks.empty()) PortFranchisePicksTaken(picks.data(), static_cast<int>(picks.size()), held, n, g.frEditMembers, out.data());
}
void draw_franchise_pick(uint8_t* px, int w, int h, const FrSnapshot& s) {
  const int stage = std::max(0, std::min(4, s.pickStage));
  const int role = stage >= 2 ? 2 : 0;  // r130: the sidekicks one slot at a time on the sidekick-slot menu
  const std::vector<MenuEntry>& menu = menu_entries(role);
  const int n = static_cast<int>(menu.size());
  // r112: the new menu (draw_select); an evolved form / a costume can't start a career, its silhouette
  auto locked = [&](int i) {
    return i >= 0 && i < n && menu[i].team < 0 && menu[i].pick >= 0 && menu[i].pick < PORT_GUEST_ORIGINAL &&
           PortGuestRosterEvolved(menu[i].pick);
  };
  auto taken = [&](int i) {
    if (i >= 0 && i < static_cast<int>(s.pickTaken.size()) && s.pickTaken[i] != 0) return true;  // r131
    // (r130: nor your backup, nor a Strikers captain you have as one)
    if (stage >= 2)
      return i >= 0 && i < n &&
             (sk_taken(menu[i], s.pickSk, stage - 2, 0, s.pickCap, fr_cap_bits(s.pickCap, s.pickBk)) ||
              (s.pickBk >= 0 && menu[i].pick == s.pickBk));
    return i >= 0 && i < n && stage == 1 && menu[i].pick == s.pickCap;
  };
  static const char* const kWhat[5] = {"YOUR CAPTAIN", "YOUR BACKUP CAPTAIN", "YOUR SIDEKICK 1 OF 3", "YOUR SIDEKICK 2 OF 3",
                                       "YOUR SIDEKICK 3 OF 3"};
  std::vector<xrtext::SelectHint> hints = {{"L", kHintStick, "MOVE"}, {"A", kHintA, "PICK"}};
  hints.push_back({"LT RT", kHintStick, "FORMS"});
  if (stage == 1) hints.push_back({"X", kHintX, "NO BACKUP"});
  if (stage == 2 || stage == 3) hints.push_back({"X", kHintX, "ALL THE SAME"});
  hints.push_back({"B", kHintB, "BACK"});
  draw_select(px, w, h, role, s.cursor, s.onTabs, kWhat[stage], taken, locked, hints);
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
  // (r143: r113's newspaper photo is no longer taken from this frame: the magazine draws its own covers)
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

// r137: the compositor's per-layer colour scale / bias (the weather's grade on the eye layers: free, no extra pass)
bool g_hasColorScaleBias = false;

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
  g_hasColorScaleBias = has(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);  // r137: the weather's grade
  if (g_hasColorScaleBias) {
    exts.push_back(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);
  }
  XLOG("extensions: passthrough=%d refresh_rate=%d perf_settings=%d thread_settings=%d render_model=%d "
       "color_scale_bias=%d",
       g.hasPassthroughExt ? 1 : 0, g.hasRefreshRateExt ? 1 : 0, g.hasPerfSettingsExt ? 1 : 0,
       g.hasThreadSettingsExt ? 1 : 0, g.hasRenderModelExt ? 1 : 0, g_hasColorScaleBias ? 1 : 0);

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
  // r123 (Lance, on 72: "that shit felt CRYSTAL clear ... is there a way to reach even higher???"): strikers.ini
  // refresh_rate = 80 / 90 / 120 asks for that instead (the game frames follow every display frame where a view keeps
  // up, else 60 a second in that view: its log says which); anything the headset doesn't offer stays at 72.
  float want = 72.0f;
  if (const char* env = std::getenv("STRIKERS_REFRESH_RATE")) {
    const float asked = static_cast<float>(std::atof(env));
    for (float r : rates)
      if (asked > 0.0f && std::fabs(r - asked) < 0.5f) want = r;
    if (want != asked) XLOG("refresh_rate %s isn't offered here: 72 Hz", env);
  }
  get_proc(g.instance, "xrGetDisplayRefreshRateFB", &g.getRefreshRate);
  if (std::find(rates.begin(), rates.end(), want) != rates.end()) {
    g.wantRefresh = want;
    xr_ok(g.requestRefreshRate(g.session, want), "xrRequestDisplayRefreshRateFB");
    XLOG("display: %.0f Hz asked", want);
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
  // r62: the suite's trophies too; r104: none while the shelf is switched off (a held trophy goes back)
  const bool here = g.showShelf && home_on() && ((g.mrMode && sh.placed) || g.inSuite);
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

// r120: the home figure's spot (x z yaw scale), like the shelf's; one over 5 m from where you start is placed again.
std::string homespot_path() {
  const char* home = std::getenv("STRIKERS_HOME");
  return std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/xr_homespot.txt";
}
void save_homespot() {
  if (FILE* f = std::fopen(homespot_path().c_str(), "w")) {
    std::fprintf(f, "%.3f %.3f %.4f %.3f\n", g.homeSpot.x, g.homeSpot.z, g.homeSpot.yaw, g.homeSpot.scale);
    std::fclose(f);
  }
}
void load_homespot() {
  if (FILE* f = std::fopen(homespot_path().c_str(), "r")) {
    float x = 0, z = 0, yaw = 0, sc = 1;
    if (std::fscanf(f, "%f %f %f %f", &x, &z, &yaw, &sc) == 4 && std::isfinite(x + z + yaw + sc) && x * x + z * z < 25.0f) {
      g.homeSpot.placed = true;
      g.homeSpot.x = x;
      g.homeSpot.z = z;
      g.homeSpot.yaw = yaw;
      g.homeSpot.scale = std::max(0.4f, std::min(1.5f, sc));
      XLOG("home: figure at (%.2f, %.2f), size %.2f", x, z, g.homeSpot.scale);
    }
    std::fclose(f);
  }
}

// r120: one grip closing on the home figure (round it, floor to over its head) drags it along the floor; both
// turn it (as the hands turn) and size it (as they part: 0.4 - 1.5 of life size). Let go: it stays there.
void update_home_spot(bool gripL, bool gripR, bool haveL, bool haveR, const XrVector3f& handL, const XrVector3f& handR) {
  auto& hs = g.homeSpot;
  const bool grip[2] = {gripL, gripR}, have[2] = {haveL, haveR}, prev[2] = {hs.prevGripL, hs.prevGripR};
  const bool shelfHas[2] = {g.shelf.handL, g.shelf.handR};
  const XrVector3f hand[2] = {handL, handR};
  bool* takes[2] = {&hs.handL, &hs.handR};
  for (int h = 0; h < 2; ++h) {
    if (hs.shown && grip[h] && !prev[h] && have[h] && !shelfHas[h]) {
      const float dx = hand[h].x - hs.x, dz = hand[h].z - hs.z, dy = hand[h].y - hs.floorY;
      if (dx * dx + dz * dz < std::pow(std::max(0.35f, hs.reach + 0.15f), 2.0f) && dy > -0.1f && dy < hs.height + 0.25f) {
        *takes[h] = true;
        XLOG("home: figure taken by the %s grip", h == 0 ? "left" : "right");
      }
    }
    if (!grip[h] || !have[h] || !hs.shown) *takes[h] = false;
  }
  hs.prevGripL = gripL;
  hs.prevGripR = gripR;
  const int hands = (hs.handL ? 1 : 0) | (hs.handR ? 2 : 0);
  if (hands != hs.grabHands) {
    if (hands == 3) {
      hs.twoAngle = std::atan2(handR.x - handL.x, handR.z - handL.z);
      hs.twoYaw = hs.yaw;
      hs.twoPos = {hs.x, 0.0f, hs.z};
      hs.twoMid = {0.5f * (handL.x + handR.x), 0.0f, 0.5f * (handL.z + handR.z)};
      hs.twoDist = std::max(0.05f, std::sqrt((handR.x - handL.x) * (handR.x - handL.x) + (handR.y - handL.y) * (handR.y - handL.y) +
                                             (handR.z - handL.z) * (handR.z - handL.z)));
      hs.twoScale = hs.scale;
    } else if (hands != 0) {
      const XrVector3f& p = hands == 1 ? handL : handR;
      hs.grabOffset = {hs.x - p.x, 0.0f, hs.z - p.z};
    } else {
      hs.placed = true;
      save_homespot();
      XLOG("home: figure left at (%.2f, %.2f), %.0f deg, size %.2f", hs.x, hs.z, hs.yaw * 57.29578f, hs.scale);
    }
    hs.grabHands = hands;
  }
  if (hands == 3) {
    float turn = std::atan2(handR.x - handL.x, handR.z - handL.z) - hs.twoAngle;
    while (turn > 3.14159265f) turn -= 6.2831853f;
    while (turn < -3.14159265f) turn += 6.2831853f;
    hs.yaw = hs.twoYaw + turn;
    hs.x = hs.twoPos.x + 0.5f * (handL.x + handR.x) - hs.twoMid.x;
    hs.z = hs.twoPos.z + 0.5f * (handL.z + handR.z) - hs.twoMid.z;
    const float d = std::sqrt((handR.x - handL.x) * (handR.x - handL.x) + (handR.y - handL.y) * (handR.y - handL.y) +
                              (handR.z - handL.z) * (handR.z - handL.z));
    hs.scale = std::max(0.4f, std::min(1.5f, hs.twoScale * d / hs.twoDist));
  } else if (hands != 0) {
    const XrVector3f& p = hands == 1 ? handL : handR;
    hs.x = p.x + hs.grabOffset.x;
    hs.z = p.z + hs.grabOffset.z;
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
      const bool gripRawR = get_float(a.squeezeR) > 0.5f;  // r131: as held (gripL / gripR below: not the magazine's)
      const bool gripRawL = get_float(a.squeezeL) > 0.6f;
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
        // r135 (Lance's clip: "the magazine is still letting the light shine into it"): not while STRIKERS
        // ILLUSTRATED is open - the hand holding it rested its pointer on the big screen's bottom edge, and the lit
        // edge (white, drawn over everything in the effects layer) ran across the pages
        if (g.shownScreenValid && (!g.tabletop || g.stereoFailed) && !g.paperOpen) {
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
      // r131: STRIKERS ILLUSTRATED before all: a grip that closes on the magazine is the magazine's until it opens
      // (no shelf, home figure, screen, table or item swap with it); its triggers on a page, its swipes.
      {
        const bool rawGrip[2] = {gripRawL, gripRawR}, haveHand[2] = {haveL, haveR};
        const XrVector3f handAt[2] = {handL, handR};
        update_magazine(rawGrip, haveHand, handAt, now_ns());
        // r137: a trading-card pack / a card in your hands (xr_cards.cpp), or a grip pointing at the binder's pocket
        const XrPosef* aims[2] = {g.aimValid[0] ? &g.aim[0] : nullptr, g.aimValid[1] ? &g.aim[1] : nullptr};
        // r142 (Lance: "make the cards more manipulable"): the grips' whole poses - a card in a hand turns with it
        XrPosef gripAt[2]{};
        bool gripOk[2] = {false, false};
        const XrSpace gripSpace[2] = {a.gripSpaceL, a.gripSpaceR};
        for (int k = 0; k < 2; ++k) {
          XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
          const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
          gripOk[k] = gripSpace[k] != XR_NULL_HANDLE &&
                      XR_SUCCEEDED(xrLocateSpace(gripSpace[k], g.local, g.frameState.predictedDisplayTime, &loc)) &&
                      (loc.locationFlags & need) == need;
          if (gripOk[k]) gripAt[k] = loc.pose;
        }
        const XrPosef* grips[2] = {gripOk[0] ? &gripAt[0] : nullptr, gripOk[1] ? &gripAt[1] : nullptr};
        xrcards::hands(rawGrip, haveHand, handAt, aims, grips, now_ns());
        // r139: the hands' index triggers (the hand not holding a pack rips it, then turns its cards over one by one)
        const float cardTrig[2] = {src[kSrcTrigL], src[kSrcTrigR]};
        xrcards::triggers(cardTrig, now_ns());
      }
      const bool gripL = gripRawL && !g.magHand[0] && !g.magSpent[0] && !xrcards::hand_busy(0);  // r137: nor a card's
      const bool gripR = gripRawR && !g.magHand[1] && !g.magSpent[1] && !xrcards::hand_busy(1);
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
        update_home_spot(gripL, gripR, haveL, haveR, handL, handR);  // r120
      }
      if (gripR && !g.prevGripR) {
        g.table.rightGrab = g.tabletop && !g.stereoFailed && haveR && !g.shelf.handR && !g.homeSpot.handR && near_table(handR);
      }
      if (!gripR) g.table.rightGrab = false;
      // r42: the Jumbotron screen (the big screen modes, and Tabletop when 3D failed).
      const bool screenMode = !g.tabletop || g.stereoFailed;
      if (screenMode) {
        update_screen(gripL && !g.shelf.handL && !g.homeSpot.handL, gripR && !g.shelf.handR && !g.homeSpot.handR, haveL, haveR, handL, handR);
        // The left grip is busy with the screen: the right grip is not Z until it is let go.
        if (g.screens[screen_room()].grabHands != 0) g.holdRightGrip = true;
      } else {
        g.prevGripL = gripL;
        g.screens[screen_room()].grabHands = 0;
      }
      if (g.shelf.handR || g.homeSpot.handR) g.holdRightGrip = true;  // r57: carrying the shelf is no item swap (r120: nor the home figure)
      if (!gripR) g.holdRightGrip = false;
      // r46: Z from the right grip only while it is mapped there; it still grabs the table either way.
      if (g.controls[kActZ] == kSrcGripR && gripR && !g.table.rightGrab && !g.holdRightGrip) {
        status.button |= PAD_TRIGGER_Z;
      }
      g.prevGripR = gripR;
      const bool holdL = g.tabletop && gripL && !g.shelf.handL && !g.homeSpot.handL;  // r57: not while it carries the shelf (r131: nor the magazine)
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
          g.gpSkSlot = -1;  // r130: the sidekick slots start once the co-captain (if any) is set
          g.gpPrevX = (status.button & PAD_BUTTON_X) != 0;
          g.gpOnTabs = false;  // r112
          g.gpPrevLT = (status.button & PAD_TRIGGER_L) != 0 || status.triggerLeft > 160;
          g.gpPrevRT = (status.button & PAD_TRIGGER_R) != 0 || status.triggerRight > 160;
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
          // r130: the sidekicks one slot at a time on the sidekick-slot menu (role 2)
          if (!g.gpCo && g.gpRole == 1 && g.gpSkSlot < 0) {
            g.gpSkFirst = PortGuestCoCaptainOffered(g.gpSide) && PortGuestCoCaptainCurrent(g.gpSide) >= 0 ? 1 : 0;  // (review: a grudge co-captain stays picked)
            g.gpSkSlot = g.gpSkFirst;
            for (int k = 0; k < 3; ++k) g.gpSkPick[k] = -1;
            g.gpCursor = 0;
            ++g.gpGen;
          }
          const std::vector<MenuEntry>& menu = menu_entries(g.gpCo ? 0 : (g.gpRole == 1 ? 2 : g.gpRole));
          if (g.gpCo) {
            // r79: the Strikers captains it can't be (a team's captain, the other side's co-captain)
            unsigned coTaken = 0;
            for (int t = 0; t < 8; ++t)
              if (!PortGuestCoCaptainFree(g.gpSide, t)) coTaken |= 1u << t;
            if (coTaken != g.gpCoTaken) {
              g.gpCoTaken = coTaken;
              ++g.gpGen;
            }
          } else if (g.gpRole == 1) {
            // r130: the Strikers captains its sidekick slots can't have (redrawn when they change)
            const unsigned skCapTaken = PortGuestSlotCaptainsTaken(g.gpSide);
            if (skCapTaken != g.gpSkCapTaken) {
              g.gpSkCapTaken = skCapTaken;
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
          const bool oldTabs = g.gpOnTabs;
          // r112: the menu by game (menu_nav): the stick among a game's characters and onto its tabs, LT / RT
          // through the one's forms
          const int navRole = g.gpCo ? 0 : (g.gpRole == 1 ? 2 : g.gpRole);  // r130
          const bool ltDown = (status.button & PAD_TRIGGER_L) != 0 || status.triggerLeft > 160;
          const bool rtDown = (status.button & PAD_TRIGGER_R) != 0 || status.triggerRight > 160;
          if (step) g.gpCursor = menu_nav(navRole, g.gpCursor, &g.gpOnTabs, dir, 0);
          if (ltDown && !g.gpPrevLT) g.gpCursor = menu_nav(navRole, g.gpCursor, &g.gpOnTabs, 0, -1);
          if (rtDown && !g.gpPrevRT) g.gpCursor = menu_nav(navRole, g.gpCursor, &g.gpOnTabs, 0, 1);
          g.gpPrevLT = ltDown;
          g.gpPrevRT = rtDown;
          if (g.gpCursor != oldCursor || g.gpOnTabs != oldTabs) ++g.gpGen;
          const bool aDown = (status.button & PAD_BUTTON_A) != 0, bDown = (status.button & PAD_BUTTON_B) != 0;
          const bool xDown = (status.button & PAD_BUTTON_X) != 0;
          if (g.gpOnTabs && aDown && !g.gpHoldA) {
            // r112: A on the tabs: down into that game's characters (not a pick)
            g.gpOnTabs = false;
            g.gpHoldA = true;
            ++g.gpGen;
          } else if (g.gpCo && xDown && !g.gpPrevX) {
            // r79: no co-captain; on to the sidekicks
            PortGuestCoCaptainPick(g.gpSide, -1);
            XLOG("character select: side %d has no co-captain", g.gpSide);
            g.gpCo = false;
            g.gpCursor = 0;
            g.gpOnTabs = false;
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
                g.gpOnTabs = false;
                ++g.gpGen;
              }
            }
            g.gpHoldA = true;
          } else if (g.gpRole == 1 && g.gpSkSlot >= 0 && ((aDown && !g.gpHoldA) || (xDown && !g.gpPrevX))) {
            // r130: a sidekick slot: A picks it, X picks it for every slot left; after the third the three go to the screen
            // (r130, any slot any character: X only for a sidekick kind, never a named character - one of each a team)
            if (g.gpCursor >= 0 && g.gpCursor < static_cast<int>(menu.size())) {
              const MenuEntry& e = menu[g.gpCursor];
              const bool rest = !(aDown && !g.gpHoldA);
              if (!PortGuestRosterLocked(e.pick) && (!rest || sk_repeatable(e)) &&
                  !sk_taken(e, g.gpSkPick, g.gpSkSlot, g.gpSkFirst, PortGuestMenuCaptainFor(g.gpSide),
                            PortGuestSlotCaptainsTaken(g.gpSide))) {
                do {
                  g.gpSkPick[g.gpSkSlot++] = e.pick;
                } while (rest && g.gpSkSlot < 3);
                XLOG("character select: side %d's sidekick %d is %s%s", g.gpSide, g.gpSkSlot, e.name.c_str(), rest ? " (and the rest)" : "");
                if (g.gpSkSlot >= 3) {
                  sk_commit(g.gpSide, g.gpSkPick, g.gpSkFirst);
                  g.gpOpen = false;
                } else {
                  g.gpOnTabs = false;
                }
                ++g.gpGen;
              }
            }
            if (aDown) g.gpHoldA = true;
          } else if (g.gpRole == 1 && g.gpSkSlot > g.gpSkFirst && bDown && !g.gpHoldB) {
            --g.gpSkSlot;  // r130: back a slot
            g.gpHoldB = true;
            ++g.gpGen;
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
          // r113: a franchise match starting: the paper waits for the hub (r143: no newspaper photo any more)
          const bool matchOn = PortFranchiseMatchActive();
          g.frWasMatch = matchOn;
          if (matchOn) g.paperOpen = false;
        }
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
            if (liveOn && live.valid) {  // r131: STRIKERS ILLUSTRATED's own numbers of the match (a hat trick)
              g.magNews.live = live;
              g.magNews.liveSeason = g_frSnap.view.season;
              g.magNews.liveWeek = g_frSnap.view.week;
            }
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
          g.paperDue = backFromMatch;  // r113: back from a franchise match: today's paper (after any evolution)
          g.paperOpen = false;
          fr_refresh();
          XLOG("franchise: the hub opens (%s)", g.frMode == 2 ? "a career loaded" : "the careers");
          // r147 (Lance: the lofi loop "has to be the new theme song for the franchise mode. that plays first every time
          // you go into franchise mode, and then its random from there on"): going in from the main menu (Y), not the
          // hub coming back after a franchise match (the playlist carries on there)
          if (!backFromMatch) PortMenuMusicFranchiseEnter();
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
              g.frPickCursor[0] = g.frPickCursor[1] = g.frPickCursor[2] = g.frPickCursor[3] = g.frPickCursor[4] = 0;
              g.frEditMembers = false;  // r130
              g.frPickSk[0] = g.frPickSk[1] = g.frPickSk[2] = -1;
              g.frCursor = 0;
              g.frOnTabs = false;  // r112
              XLOG("franchise: career %d with %s: picking your players", g.frSlot + 1, PortFranchiseClubName(g.frPickClub));
            } else if (bPress) {
              g.frMode = 0;
              g.frCursor = g.frSlot;
            }
          } else if (g.frMode == 4) {  // r75: a new career's own players: captain, backup captain, sidekicks
            const int navRole = g.frPickStage >= 2 ? 2 : 0;  // r130: the sidekicks on the sidekick-slot menu
            const std::vector<MenuEntry>& menu = menu_entries(navRole);
            // r112: the menu by game (menu_nav), LT / RT through a character's forms
            const bool ltDown = (status.button & PAD_TRIGGER_L) != 0 || status.triggerLeft > 160;
            const bool rtDown = (status.button & PAD_TRIGGER_R) != 0 || status.triggerRight > 160;
            const bool wasTabs = g.frOnTabs;
            if (step) g.frCursor = menu_nav(navRole, g.frCursor, &g.frOnTabs, dir, 0);
            if (ltDown && !g.frPrevLT) {
              g.frCursor = menu_nav(navRole, g.frCursor, &g.frOnTabs, 0, -1);
              changed = true;
            }
            if (rtDown && !g.frPrevRT) {
              g.frCursor = menu_nav(navRole, g.frCursor, &g.frOnTabs, 0, 1);
              changed = true;
            }
            g.frPrevLT = ltDown;
            g.frPrevRT = rtDown;
            if (g.frOnTabs != wasTabs) changed = true;
            const int pick = g.frCursor >= 0 && g.frCursor < static_cast<int>(menu.size()) ? menu[g.frCursor].pick : -1;
            auto to_stage = [&](int stage) {
              g.frPickCursor[g.frPickStage] = g.frCursor;
              g.frPickStage = stage;
              g.frCursor = g.frPickCursor[stage];
              g.frOnTabs = false;
            };
            if (aPress && g.frOnTabs) {
              g.frOnTabs = false;  // r112: A on the tabs: down into that game's characters
            } else if (aPress && pick >= 0) {
              if (g.frPickStage == 0 && PortFranchiseCaptainPickable(pick)) {
                g.frPickCap = pick;
                XLOG("franchise: your captain %s", menu[g.frCursor].name.c_str());
                to_stage(1);
              } else if (g.frPickStage == 1 && pick != g.frPickCap && PortFranchiseCaptainPickable(pick) &&
                         !fr_pick_taken(pick)) {  // r131: one character, one spot
                g.frPickBk = pick;
                XLOG("franchise: your backup captain %s", menu[g.frCursor].name.c_str());
                to_stage(2);
              } else if (g.frPickStage >= 2 && g.frPickStage <= 4 &&
                         !(pick < PORT_GUEST_ORIGINAL && PortGuestRosterEvolved(pick) &&
                           !(g.frEditMembers && (pick == g.frMemNow[0] || pick == g.frMemNow[1] || pick == g.frMemNow[2]))) &&
                         !(pick == g.frPickBk) &&  // r130: a Strikers backup captain too
                         !fr_pick_taken(pick) &&   // r131: one character, one club
                         !sk_taken(menu[g.frCursor], g.frPickSk, g.frPickStage - 2, 0, g.frPickCap,
                                   fr_cap_bits(g.frPickCap, g.frPickBk))) {
                // r130 (Lance: "you can have anybody play a sidekick now"): your three sidekicks, one at a time
                g.frPickSk[g.frPickStage - 2] = pick;
                XLOG("franchise: your sidekick %d %s", g.frPickStage - 1, menu[g.frCursor].name.c_str());
                if (g.frPickStage < 4) {
                  to_stage(g.frPickStage + 1);
                } else if (g.frEditMembers) {
                  if (PortFranchiseSetMembers(g.frPickSk)) XLOG("franchise: your sidekick members set");
                  g.frEditMembers = false;
                  g.frMode = 2;
                  g.frCursor = 0;
                  g.frOpenNs = nowFr;
                } else {
                  // The career starts on its hub, nothing played: you start your matches yourself (Lance)
                  // r100: an online league's: made on the server too, its code to share (its sidekicks one kind)
                  // r131: your sidekick members go in with the deal (the league forms around all your picks)
                  const int kindPick = fr_kind_from(g.frPickSk);
                  if (g.frSlot == PORT_FR_LEAGUE_SLOT
                          ? PortFranchiseLeagueCreate(g.frPickClub, g.frPickCap, g.frPickBk, kindPick, g.frLgMax, g.frPickSk)
                          : PortFranchiseNewPicked(g.frSlot, g.frPickClub, g.frPickCap, g.frPickBk, kindPick, g.frPickSk)) {
                    if (g.frSlot != PORT_FR_LEAGUE_SLOT) PortFranchiseSetMembers(g.frPickSk);  // r130: each its own level
                    XLOG("franchise: career %d with %s, sidekick kind pick %d", g.frSlot + 1, PortFranchiseClubName(g.frPickClub), kindPick);
                    g.frMode = 2;
                    g.frPage = 0;
                    g.frCursor = 0;
                    g.frOpenNs = nowFr;  // and a moment before A acts on the hub (a fresh press plays)
                  }
                }
              }
            } else if (xPress && (g.frPickStage == 2 || g.frPickStage == 3) && pick >= 0 && !g.frOnTabs &&
                       sk_repeatable(menu[g.frCursor]) &&  // r130 (review): X = the same for the rest: sidekicks only
                       !(pick < PORT_GUEST_ORIGINAL && PortGuestRosterEvolved(pick)) &&
                       !fr_pick_taken(pick) &&  // r131: a Charged sidekick guest another club has
                       !sk_taken(menu[g.frCursor], g.frPickSk, g.frPickStage - 2, 0, g.frPickCap,
                                 fr_cap_bits(g.frPickCap, g.frPickBk))) {
              // r130: X: this one for every slot left (then A on the last confirms it, as a pick)
              for (int k = g.frPickStage - 2; k < 2; ++k) g.frPickSk[k] = pick;
              g.frPickCursor[4] = g.frCursor;
              to_stage(4);
            } else if (xPress && g.frPickStage == 1) {
              g.frPickBk = -1;  // no backup captain
              XLOG("franchise: no backup captain");
              to_stage(2);
            } else if (bPress) {
              if (g.frEditMembers && g.frPickStage <= 2) {  // r130: back to the hub, nothing changed
                g.frEditMembers = false;
                g.frMode = 2;
                g.frCursor = 0;
              } else if (g.frPickStage == 0) {
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
              if (step && (dir == 1 || dir == 2) && g.frEvo.options >= 2)  // r130: round the 2-5 options
                g.frEvoChoice = (g.frEvoChoice + (dir == 2 ? 1 : g.frEvo.options - 1)) % g.frEvo.options;
              if (aPress && t > 0.5f) {
                g.frEvoStage = 1;
                g.frEvoStartNs = nowFr;
                g.frEvoFanfare = false;
                PortEvoMusic(std::strncmp(g.frEvo.fromKey, "mua_", 4) == 0 ? 2 : 0);  // r99: Pokemon Stadium (Melee) while it evolves; r130: a hero's costume, its MUA music
                XLOG("franchise: evolving %s into %s (A on the EVO page)", g.frEvo.fromKey, g.frEvo.toKey[g.frEvoChoice]);
              } else if (bPress && t > 0.5f) {
                bool sh = false, ns = false;
                PortFranchiseEvolvePlayer(g.frEvo.slot, -1, &sh, &ns);  // r130: by slot (a sidekick member too)
                g.frEvoStage = 3;
                g.frEvoStartNs = nowFr;
                XLOG("franchise: evolution of %s postponed (B on the EVO page)", g.frEvo.fromKey);
              }
            } else if (g.frEvoStage == 1) {
              if (t >= 8.5f && !g.frEvoFanfare) {  // r99: the flash: the Pokemon victory fanfare
                g.frEvoFanfare = true;
                PortEvoMusic(std::strncmp(g.frEvo.fromKey, "mua_", 4) == 0 ? 3 : 1);  // r130: a hero's costume: its MUA win
              }
              if (t >= 8.8f) {  // r96: after the flash (8.5 s), in 3D or on the card
                bool sh = false, ns = false;
                const bool evolvedNow = PortFranchiseEvolvePlayer(g.frEvo.slot, g.frEvoChoice, &sh, &ns);  // r130
                if (evolvedNow && g.frEvoChoice >= 0 && g.frEvoChoice < 5) {
                  // r131: this week's STRIKERS ILLUSTRATED puts it on its cover (the SPECIAL one)
                  g.magNews.evoFrom = g.frEvo.fromName;
                  g.magNews.evoTo = g.frEvo.toName[g.frEvoChoice];
                  g.magNews.evoToKey = g.frEvo.toKey[g.frEvoChoice];
                  g.magNews.evoShiny = ns;
                  g.magNews.evoCostume = std::strncmp(g.frEvo.fromKey, "mua_", 4) == 0;
                  g.magNews.evoSeason = g_frSnap.view.season;
                  g.magNews.evoWeek = g_frSnap.view.lastWeek;
                }
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
          } else if (g.paperOpen || (g.paperDue && !g_frSnap.view.evolutionPending)) {
            // r113: THE DAILY STRIKE (Lance: "a newspaper ... that ppl can literally flip through after each
            // game"): due after a match (or a worked-out one) once nothing else (an evolution) comes first, or
            // the PAPER page's A. Left / right (or LT / RT, or a swipe of your right hand across it) turns the
            // page, A the next one (on the last: puts it down), B / Y puts it down.
            // r131: STRIKERS ILLUSTRATED: opens closed on its cover. Stick left / right, LT / RT turn a page (not a
            // trigger that's holding a page: update_magazine), A the next (closed on the back cover: puts it down),
            // B / Y put it down; the hands: update_magazine (grab, a trigger on a page, a swipe while held).
            const bool ltDown = (status.button & PAD_TRIGGER_L) != 0 || status.triggerLeft > 160;
            const bool rtDown = (status.button & PAD_TRIGGER_R) != 0 || status.triggerRight > 160;
            if (!g.paperOpen) {
              g.paperDue = false;
              paper_open();
            } else {
              const bool trigHeld = g.magTrigBusy[0] || g.magTrigBusy[1] || g.magDrag >= 0;
              // r131: the cover stays a moment first: the hub's A counts 0.3 s after it opens, and an A mashed
              // through the match's end screens turned the front page then too (the paper opens with the hub)
              const bool magReady = nowFr - g.magOpenNs > 900000000ull;
              int turn = 0;
              const char* by = "";
              if (step && dir == 1) turn = -1, by = "stick";
              if (step && dir == 2) turn = 1, by = "stick";
              (void)trigHeld;  // r131: the triggers turn pages in update_magazine (right on, left back)
              if (aPress && magReady) {
                if (g.magLeaves >= kMagLeaves && g.magTurn < 0 && g.magDrag < 0) {
                  g.paperOpen = false;
                } else {
                  turn = 1;
                  by = "A";
                }
              }
              if ((bPress || yPress) && magReady) g.paperOpen = false;
              if (!magReady) turn = 0;
              if (turn != 0 && g.paperOpen && mag_flip(turn))
                XLOG("franchise: a magazine page turned %s (%s), leaf %d", turn > 0 ? "on" : "back", by, g.magTurn);
              if (!g.paperOpen) {
                g.magDrag = -1;
                mag_settle();
                XLOG("franchise: the magazine put down");
              }
            }
            g.paperPrevLT = ltDown;
            g.paperPrevRT = rtDown;
          } else {  // the hub: the club, the standings, the playoffs, last week's results
            if (yPress && !PortFranchiseCoopGuest() && !xrcards::busy()) {  // r77: Y = trades (the friend's career: theirs to trade)
              g.frMode = 5;
              g.frTradeStage = 0;
              g.frCursor = 0;
              g.frTradeMsg.clear();
              XLOG("franchise: trades");
            }
            const PortFranchiseLeagueView& lgv = g_frSnap.league;  // r100: an online league's
            const int pageCount = (lgv.active ? 11 : 10) + 1;  // r137: + CARDS, the last
            if (g.frPage >= pageCount) g.frPage = 0;
            // r137: the CARDS page (xr_cards.cpp): the binder (stick on its pockets, LT / RT its pages, A opens a pack / X
            // takes a card out) and a pack open in your hands (A / triggers tear it and turn its cards over, B skips /
            // puts them away) use the buttons they use; the stick left / right at the binder's edge is the hub's pages
            bool cardsStick = false, cardsButton = false;
            if (g.frPage == pageCount - 1 || xrcards::busy()) {
              xrcards::Input ci;
              ci.dir = dir;
              ci.step = step;
              ci.a = aPress;
              ci.b = bPress;
              ci.x = xPress;
              ci.lt = (status.button & PAD_TRIGGER_L) != 0 || status.triggerLeft > 160;
              ci.rt = (status.button & PAD_TRIGGER_R) != 0 || status.triggerRight > 160;
              bool redraw = false;
              cardsButton = xrcards::hub_input(ci, &redraw, &cardsStick);
              if (redraw) changed = true;
            }
            if (step && (dir == 1 || dir == 2) && !cardsStick) {
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
            if (cardsButton) {
              // r137: the cards had it
            } else if (aPress && g.frPage == 0 && vsPerson && hasMatch) {
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
                g.paperDue = true;  // r113: the paper on what happened
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
                g.paperDue = true;  // r113
              } else if (aPress && v.phase == PORT_FR_PHASE_DONE) {
                PortFranchiseNextSeason();
              } else if (xPress) {
                PortFranchiseSwapStarter();
              }
            } else if (aPress && g.frPage == 5) {
              paper_open();  // r113: the PAPER page: read today's paper
            } else if (aPress && g.frPage == 9) {
              // r97: A on the difficulty page sets it
              if (PortFranchiseSetDifficulty(g.frCursor + 1)) XLOG("franchise: difficulty %d", g.frCursor + 1);
            } else if (xPress && g.frPage == 8 && !PortFranchiseCoopGuest() && !lgv.active) {
              // r130: X on the Evo Watch page: your three sidekick members (anybody: each its own level and rewards)
              g.frMode = 4;
              g.frEditMembers = true;
              g.frPickStage = 2;
              // r130 (review): start from your members now (kept ones keep their level), your captain / backup taken
              PortFranchiseMemberPicks(g.frMemNow, &g.frPickCap, &g.frPickBk);
              for (int k = 0; k < 3; ++k) g.frPickSk[k] = g.frMemNow[k];
              g.frPickCursor[2] = g.frPickCursor[3] = g.frPickCursor[4] = 0;
              g.frCursor = 0;
              g.frOnTabs = false;
              XLOG("franchise: picking your sidekick members");
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
            static int s_cardsGen = -1;  // r137: the trading cards changed (a pack earned / opened, one out): drawn again
            if (g.frOpen && xrcards::generation() != s_cardsGen) {
              s_cardsGen = xrcards::generation();
              fr_refresh();
            }
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
  // r131 (review): a blit / upload into the old screen images may still be on the GPU (the GPU-direct screen slots)
  if (g.swapchain != XR_NULL_HANDLE && g.queue != VK_NULL_HANDLE) vkQueueWaitIdle(g.queue);
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
  return idx >= 0 && idx < xrfx::kEtchCells ? 100 + idx : 0;  // r121: 256 cells (the heroes 95-104); r135: 512
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
  const int choice = e.options >= 2 ? std::max(0, std::min(e.options - 1, g.frEvoChoice)) : 0;  // r130
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
    // r132 (Lance: "they go low as fuck lower than my floor"): on YOUR floor (where the evolution room stands), not the
    // view's - the Stadium view's pitch is 2.4 m below your head and stood it under the room's floor
    const XrVector3f feet{es.from.x + es.dirX * dist, g.floorY, es.from.z + es.dirZ * dist};
    if (std::fabs(feet.x - es.feet.x) + std::fabs(feet.z - es.feet.z) > 0.01f) {
      XLOG("evo: the 3D evolution stands %.2f m ahead (reach %.2f, floor %.2f)", dist, reach, g.floorY);
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
  md.shine = ((show == 0 ? fromShiny : toShiny) && white < 0.5f) ? 1.0f : 0.0f;  // r127: a shiny one glints
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

// r112 (Lance: "we need more images or VR native experiences that can do the same thing we're trying to force
// people to read"): the character under the character select menu's cursor stands in 3D beside the menu, on
// your floor, life-size, turning a little side to side; its pack and standing pose as the 3D evolution's
// (xr_evo: the Pokemon, the Melee guests, the Marvel heroes; the Strikers and Charged ones have no pose file:
// the menu's portrait only). It pops up out of a little white light when the cursor rests on it. Model slot
// 0 (the evolution's, never on at the same time).
struct SelLoaded {
  std::string pack;
  xrevo::Figure* fig = nullptr;
  xrfx::Model model;
};
std::mutex g_selLoadMutex;
std::vector<SelLoaded> g_selLoaded;  // under g_selLoadMutex
struct SelShow {
  std::string want, asked, have;
  xrevo::Figure* fig = nullptr;
  std::vector<float> verts;
  uint64_t wantSince = 0, since = 0;
};
SelShow g_selShow;

void sel_clear() {
  SelShow& ss = g_selShow;
  if (ss.fig != nullptr) {
    xrevo::release(ss.fig);
    ss.fig = nullptr;
    xrfx::set_model(0, xrfx::Model{});
  }
  ss.want.clear();
  ss.asked.clear();
  ss.have.clear();
  ss.verts.clear();
}

// The menu's guest under the cursor with a figure to show ("" a Strikers character or a locked form).
std::string select_pack(int role, int cursor, bool locked) {
  const std::vector<MenuEntry>& menu = menu_entries(role);
  if (locked || cursor < 0 || cursor >= static_cast<int>(menu.size())) return "";
  const MenuEntry& e = menu[cursor];
  if (e.team >= 0 || e.pick < 0 || e.pick >= PORT_GUEST_ORIGINAL) return "";
  return e.portrait;
}

// r117 (Lance: "no more 2D placeholder image of your character in your homeground. get rid of the little
// podium/pole thing, and just add the actual 3D character life size, doing certain things that character would
// do"): your captain standing on its rug in your room, life size, in a loop of its own moves. Its home pack and
// loop (guests/home/<pack>.gpk / .gev, tools/outside/mua/build_home.py: a hero's own Ultimate Alliance moves,
// Wolverine's claws going in and out, Spider-Man's web), else its pack and its standing idle (guests/evo/).
// Model slot 2. Loaded on a thread when the captain changes.
struct HomeLoaded {
  std::string pack;
  xrevo::Figure* fig = nullptr;
  xrfx::Model model;
};
std::mutex g_homeLoadMutex;
std::vector<HomeLoaded> g_homeLoaded;  // under g_homeLoadMutex
struct HomeShow {
  std::string want, asked, have;
  xrevo::Figure* fig = nullptr;
  std::vector<float> verts, vertsB;
  bool missing = false;  // no 3D figure for this one (the standee stays)
  // r119: the move it's in and the one it's blending out of; you near; whose turn among its moves
  int seg = -1, prevSeg = -1;
  float segT = 0.0f, prevT = 0.0f, fade = 1.0f, idleFor = 0.0f, autoIn = 12.0f, touchCool = 0.0f, handUpFor = 0.0f;
  float lookYaw = 0.0f, lookPitch = 0.0f;
  bool near = false, greeted = false;
  int greetTurn = 0, bigTurn = 0, reactTurn = 0, byeTurn = 0, autoTurn = 0;
  uint64_t lastNs = 0;
  // r129: the last goal it reacted to (g_homeReactNs), its cheer / upset turns, and the hop / slump of a figure
  // with no such moves of its own (procKind 5 cheer, 6 upset, 0 none; procT its time in)
  uint64_t reactSeen = 0;
  int cheerTurn = 0, upsetTurn = 0, procKind = 0;
  float procT = 0.0f;
};
void blast_buzz(float amplitude, float seconds);  // below
HomeShow g_homeShow;
// r129 (Lance: "make the home ground people cheer with you on goals you score and get upset at goals scored on
// you"): PortXrGoal (the game thread) says whose goal it was: 5 = yours (cheer), 6 = against you (upset).
std::atomic<int> g_homeReactKind{0};
std::atomic<uint64_t> g_homeReactNs{0};

// Asks for `pack`'s figure ("" none); true when one is standing (drawn this frame at `feet`, facing `yaw`).
bool update_home_figure(xrfx::Scene& scene, const std::string& pack, const XrVector3f& feet, float yaw, uint64_t nowNs,
                        float userScale) {
  HomeShow& hs = g_homeShow;
  {
    std::vector<HomeLoaded> done;
    {
      std::lock_guard<std::mutex> lock(g_homeLoadMutex);
      done.swap(g_homeLoaded);
    }
    for (HomeLoaded& l : done) {
      if (l.pack != hs.want) {
        if (l.fig != nullptr) xrevo::release(l.fig);
        continue;
      }
      if (hs.fig != nullptr) xrevo::release(hs.fig);
      hs.fig = l.fig;
      hs.have = l.pack;
      hs.missing = l.fig == nullptr;
      if (l.fig != nullptr) xrfx::set_model(2, std::move(l.model));
      hs.seg = hs.prevSeg = -1;
      hs.greeted = hs.near = false;
      hs.lookYaw = hs.lookPitch = 0.0f;
      XLOG("home: %s %s", l.pack.c_str(), l.fig != nullptr ? "stands in your corner in 3D" : "has no 3D figure (its standee)");
    }
  }
  if (pack != hs.want) {
    hs.want = pack;
    hs.missing = false;
    if (pack.empty() && hs.fig != nullptr) {
      xrevo::release(hs.fig);
      hs.fig = nullptr;
      hs.have.clear();
      xrfx::set_model(2, xrfx::Model{});
    }
  }
  if (!hs.want.empty() && hs.want != hs.asked) {
    hs.asked = hs.want;
    const std::string p = hs.want;
    std::thread([p]() {
      HomeLoaded l;
      l.pack = p;
      const char* env = std::getenv("STRIKERS_HOME");
      const std::string home = env != nullptr ? env : "/storage/emulated/0/Strikers";
      l.fig = xrevo::load_files(home + "/guests/home/" + p + ".gpk", home + "/guests/home/" + p + ".gev", &l.model);
      if (l.fig == nullptr) l.fig = xrevo::load_files(home + "/guests/" + p + ".gpk", home + "/guests/home/" + p + ".gev", &l.model);
      if (l.fig == nullptr) l.fig = xrevo::load(p, p, &l.model);
      std::lock_guard<std::mutex> lock(g_homeLoadMutex);
      g_homeLoaded.push_back(std::move(l));
    }).detach();
  }
  if (hs.fig == nullptr || hs.have != hs.want) return false;
  const float clock = static_cast<float>(static_cast<double>(nowNs % 3600000000000ull) / 1e9);
  const float h = xrevo::height(hs.fig);
  // r118: up to 2.3 m (Bowser and Petey life size, 2.25 m; was 1.95)
  const float s = (h > 2.3f ? 2.3f / h : 1.0f) * userScale;  // r120: x the size you gave it
  const float H = h * s;
  g.homeSpot.reach = xrevo::radius(hs.fig) * s;  // r120: for grabbing it
  g.homeSpot.height = H;
  const float dt = hs.lastNs != 0 && nowNs > hs.lastNs ? std::min(0.1f, static_cast<float>(nowNs - hs.lastNs) / 1e9f) : 0.0f;
  hs.lastNs = nowNs;
  // r119 (Lance: "i want more interaction"): it looks at you, greets you when you come up, reacts when you touch
  // it, celebrates when you put your hand up in front of it, waves when you walk off; on its own now and then
  // does one of its moves. Its moves are the loop file's segments (charged_home.py / build_home.py).
  const XrVector3f eye = g.headPose.position;
  const float ex = eye.x - feet.x, ez = eye.z - feet.z;
  const float dist = std::sqrt(ex * ex + ez * ez);
  const float cs = std::cos(yaw), sn = std::sin(yaw);
  const std::vector<xrevo::Segment>& segs = xrevo::segments(hs.fig);
  const float fps = xrevo::fps(hs.fig);
  auto seg_secs = [&](int i) { return static_cast<float>(segs[i].frames) / fps; };
  auto pick = [&](uint32_t kind, int* turn) {
    std::vector<int> of;
    for (int i = 0; i < static_cast<int>(segs.size()); ++i)
      if (segs[i].kind == kind) of.push_back(i);
    if (of.empty()) return -1;
    return of[(*turn)++ % of.size()];
  };
  auto play = [&](int i, const char* why) {
    if (i < 0 || i == hs.seg) return;
    hs.prevSeg = hs.seg;
    hs.prevT = hs.segT;
    hs.fade = hs.seg >= 0 ? 0.0f : 1.0f;
    hs.seg = i;
    hs.segT = 0.0f;
    XLOG("home: %s %s (move %d, kind %u, %.1f s)", hs.have.c_str(), why, i, segs[i].kind, seg_secs(i));
  };
  // r129: a goal just went in: its own cheer / upset move (loop files' kind 5 / 6: charged_home.py, build_home.py),
  // else a hop for joy / a slump (below, on the posed figure)
  {
    const uint64_t rNs = g_homeReactNs.load(std::memory_order_acquire);
    if (rNs != 0 && rNs != hs.reactSeen) {
      hs.reactSeen = rNs;
      const int want = g_homeReactKind.load(std::memory_order_relaxed);
      if ((want == 5 || want == 6) && nowNs >= rNs && nowNs - rNs < 4000000000ull) {
        const int i = pick(static_cast<uint32_t>(want), want == 5 ? &hs.cheerTurn : &hs.upsetTurn);
        if (i >= 0) {
          play(i, want == 5 ? "cheers your goal" : "is gutted: a goal against you");
          hs.procKind = 0;
        } else {
          hs.procKind = want;
          hs.procT = 0.0f;
          XLOG("home: %s %s", hs.have.c_str(), want == 5 ? "hops for joy: your goal" : "slumps: a goal against you");
        }
      }
    }
  }
  float lookW = 0.0f;
  if (!segs.empty()) {
    if (hs.seg < 0 || hs.seg >= static_cast<int>(segs.size())) {
      int t0 = 0;
      hs.seg = std::max(0, pick(0, &t0));
      hs.segT = 0.0f;
      hs.fade = 1.0f;
      hs.idleFor = 0.0f;
    }
    hs.segT += dt;
    hs.prevT += dt;
    hs.fade = std::min(1.0f, hs.fade + dt / 0.3f);
    hs.touchCool = std::max(0.0f, hs.touchCool - dt);
    const bool idle = segs[hs.seg].kind == 0;
    if (hs.segT >= seg_secs(hs.seg)) {
      if (idle) {
        hs.segT = std::fmod(hs.segT, seg_secs(hs.seg));
      } else {
        int t0 = 0;
        play(pick(0, &t0), "back to its idle");
        hs.idleFor = 0.0f;
      }
    }
    if (segs[hs.seg].kind == 0) hs.idleFor += dt;
    // you: near, a hand on it, a hand up in front of it
    const bool nearNow = dist < 1.4f;
    XrVector3f hand[2];
    bool touch = false, handUp = false;
    for (int k = 0; k < 2; ++k) {
      if (!locate_hand(k == 0 ? g.actions.gripSpaceL : g.actions.gripSpaceR, &hand[k])) continue;
      // r120: not a hand grabbing it (or about to)
      if (g.homeSpot.grabHands != 0 || get_float(k == 0 ? g.actions.squeezeL : g.actions.squeezeR) > 0.5f) continue;
      const float hx = hand[k].x - feet.x, hz = hand[k].z - feet.z, hy = hand[k].y - feet.y;
      const float hd = std::sqrt(hx * hx + hz * hz);
      if (hd < std::max(0.22f, 0.45f * xrevo::radius(hs.fig) * s) && hy > 0.15f * H && hy < 1.05f * H) touch = true;
      if (hand[k].y > eye.y + 0.05f && hd < 1.8f && dist < 2.4f) handUp = true;
    }
    hs.handUpFor = handUp ? hs.handUpFor + dt : 0.0f;
    const uint32_t kind = segs[hs.seg].kind;
    if (touch && hs.touchCool <= 0.0f && kind != 3 && kind != 5 && kind != 6) {  // r129: not over a goal reaction
      hs.touchCool = 1.5f;
      blast_buzz(0.45f, 0.1f);
      const int r = pick(3, &hs.reactTurn);
      play(r >= 0 ? r : pick(1, &hs.greetTurn), "touched");
    } else if (hs.handUpFor > 0.4f && (kind == 0 || kind == 1)) {
      hs.handUpFor = -3.0f;  // (once a hand-up)
      play(pick(2, &hs.bigTurn), "hyped up by your hand");
    } else if (nearNow && !hs.near && kind == 0 && hs.idleFor > 1.0f) {
      play(pick(1, &hs.greetTurn), "greets you");
      hs.greeted = true;
    } else if (hs.greeted && dist > 2.4f && kind == 0) {
      hs.greeted = false;
      play(pick(4, &hs.byeTurn), "waves you off");
    } else if (kind == 0 && hs.idleFor > hs.autoIn) {
      hs.autoIn = 10.0f + 10.0f * static_cast<float>((nowNs / 7919ull) % 1000ull) / 1000.0f;
      hs.idleFor = 0.0f;
      play((hs.autoTurn++ % 3) == 2 ? pick(1, &hs.greetTurn) : pick(2, &hs.bigTurn), "does its thing");
    }
    hs.near = nearNow;
    lookW = (segs[hs.seg].kind == 0 || segs[hs.seg].kind == 1 || segs[hs.seg].kind == 4) ? 1.0f : 0.0f;
  } else {
    lookW = 1.0f;
  }
  // where you are, as it sees it (its +X ahead, +Y its left, +Z up): its head turned to you, eased
  {
    const float a = ex * cs - ez * sn, c = ex * sn + ez * cs;
    float wantYaw = std::atan2(-c, a), wantPitch = std::atan2(eye.y - (feet.y + 0.9f * H), std::max(0.3f, dist));
    wantYaw = std::max(-1.0f, std::min(1.0f, wantYaw)) * lookW;
    wantPitch = std::max(-0.4f, std::min(0.45f, wantPitch)) * lookW;
    if (!xrevo::looks(hs.fig) || dist > 6.0f) wantYaw = wantPitch = 0.0f;
    const float k = 1.0f - std::exp(-dt * 4.0f);
    hs.lookYaw += (wantYaw - hs.lookYaw) * k;
    hs.lookPitch += (wantPitch - hs.lookPitch) * k;
  }
  if (segs.empty()) {
    xrevo::pose(hs.fig, clock, feet, yaw, s, 0.0f, &hs.verts, hs.lookYaw, hs.lookPitch);
  } else {
    auto at = [&](int i, float tt) {
      const float fr = std::min(tt * fps, static_cast<float>(segs[i].frames) - 1.01f);
      return (static_cast<float>(segs[i].start) + std::max(0.0f, fr)) / fps;
    };
    xrevo::pose(hs.fig, at(hs.seg, hs.segT), feet, yaw, s, 0.0f, &hs.verts, hs.lookYaw, hs.lookPitch);
    if (hs.fade < 1.0f && hs.prevSeg >= 0 && hs.prevSeg < static_cast<int>(segs.size())) {
      // the move it was in, carried on, blended out over 0.3 s
      const float pt = segs[hs.prevSeg].kind == 0 ? std::fmod(hs.prevT, seg_secs(hs.prevSeg)) : hs.prevT;
      xrevo::pose(hs.fig, at(hs.prevSeg, pt), feet, yaw, s, 0.0f, &hs.vertsB, hs.lookYaw, hs.lookPitch);
      const float f = hs.fade * hs.fade * (3.0f - 2.0f * hs.fade);
      if (hs.vertsB.size() == hs.verts.size())
        for (size_t i = 0; i < hs.verts.size(); ++i) hs.verts[i] = hs.vertsB[i] + (hs.verts[i] - hs.vertsB[i]) * f;
    }
  }
  // r129: a figure with no cheer / upset moves of its own (the Pokemon, the Melee fighters: one standing loop) -
  // your goal: three hops with a wiggle (2.4 s); a goal against you: it slumps forward, sinks a little and shakes
  // its head (3.2 s). Turned round its feet: its way ahead (cs, -sn), its left (sn, cs), up.
  if (hs.procKind != 0) {
    hs.procT += dt;
    const float T = hs.procT;
    const bool cheer = hs.procKind == 5;
    const float dur = cheer ? 2.4f : 3.2f;
    float turn = 0.0f, lean = 0.0f, squash = 1.0f, up = 0.0f;
    if (T >= dur) {
      hs.procKind = 0;
    } else if (cheer) {
      const float hop = 0.55f;
      if (T < 3.0f * hop) up = 0.16f * H * std::sin(3.14159265f * std::fmod(T, hop) / hop);
      const float w = std::min(1.0f, (dur - T) / 0.4f);
      turn = 0.26f * w * std::sin(2.0f * 3.14159265f * T / (2.0f * hop));
    } else {
      const float w = std::min({1.0f, T / 0.45f, (dur - T) / 0.6f});
      const float e = w * w * (3.0f - 2.0f * w);
      lean = 0.38f * e;
      squash = 1.0f - 0.08f * e;
      turn = 0.14f * e * std::sin(2.0f * 3.14159265f * 1.3f * T);
    }
    if (hs.procKind != 0) {
      const float ct = std::cos(turn), st = std::sin(turn), cl = std::cos(lean), sl = std::sin(lean);
      const float fx = cs, fz = -sn, lx = sn, lz = cs;
      for (size_t v = 0; v + 5 < hs.verts.size(); v += 6) {
        for (int part = 0; part < 2; ++part) {
          float* p = &hs.verts[v + part * 3];
          const float rx = part == 0 ? p[0] - feet.x : p[0], ry = part == 0 ? p[1] - feet.y : p[1];
          const float rz = part == 0 ? p[2] - feet.z : p[2];
          const float a = rx * fx + rz * fz, b = rx * lx + rz * lz;
          const float a1 = a * ct - b * st, b1 = a * st + b * ct;
          const float a2 = a1 * cl + ry * sl;
          float u2 = -a1 * sl + ry * cl;
          if (part == 0) u2 = u2 * squash + up;
          p[0] = a2 * fx + b1 * lx + (part == 0 ? feet.x : 0.0f);
          p[1] = u2 + (part == 0 ? feet.y : 0.0f);
          p[2] = a2 * fz + b1 * lz + (part == 0 ? feet.z : 0.0f);
        }
      }
    }
  }
  xrfx::Scene::ModelDraw& md = scene.models[2];
  md.on = true;
  md.posNrm = hs.verts.data();
  md.white = 0.0f;
  md.alpha = 1.0f;
  md.glow = 0.0f;
  md.under = 0.15f;
  md.flameFrame = static_cast<int>((nowNs / 83000000ull) % 3);
  md.shine = 0.0f;
  return true;
}

// `pack` the one to show ("" none); the menu panel's middle, its right (a unit vector) and half its width.
void update_select(xrfx::Scene& scene, const std::string& pack, const XrVector3f& panelAt, const XrVector3f& right,
                   float halfW, uint64_t nowNs) {
  SelShow& ss = g_selShow;
  const bool on = g.fxReady && g.headValid && !pack.empty() && !g_evoShow.live && !(g.frOpen && g.frMode == 3);
  {
    std::vector<SelLoaded> done;
    {
      std::lock_guard<std::mutex> lock(g_selLoadMutex);
      done.swap(g_selLoaded);
    }
    for (SelLoaded& l : done) {
      if (!on || l.fig == nullptr || l.pack != ss.want) {
        if (l.fig != nullptr) xrevo::release(l.fig);
        continue;
      }
      if (ss.fig != nullptr) xrevo::release(ss.fig);
      ss.fig = l.fig;
      ss.have = l.pack;
      ss.since = nowNs;
      xrfx::set_model(0, std::move(l.model));
    }
  }
  if (!on) {
    if (ss.fig != nullptr || !ss.want.empty()) sel_clear();
    return;
  }
  if (pack != ss.want) {
    ss.want = pack;
    ss.wantSince = nowNs;
  }
  // asked for once the cursor rests on it a moment (a held stick runs past many)
  if (ss.want != ss.asked && nowNs - ss.wantSince > 150000000ull) {
    ss.asked = ss.want;
    const std::string p = ss.want;
    std::thread([p]() {
      SelLoaded l;
      l.pack = p;
      l.fig = xrevo::load(p, p, &l.model);
      std::lock_guard<std::mutex> lock(g_selLoadMutex);
      g_selLoaded.push_back(std::move(l));
    }).detach();
  }
  if (ss.fig == nullptr || ss.have != ss.want) return;
  // its place: 2 m from you, just right of the menu's right edge (the menu is drawn over everything: never
  // behind it), on your floor, facing you
  const XrVector3f head = g.headPose.position;
  float dx = panelAt.x + right.x * halfW - head.x, dz = panelAt.z + right.z * halfW - head.z;
  const float dl = std::sqrt(dx * dx + dz * dz);
  if (dl < 0.05f) {
    floor_forward(g.headPose, &dx, &dz);
  } else {
    dx /= dl;
    dz /= dl;
  }
  const float a = 0.26f;  // ~15 degrees further right
  const float bx = dx * std::cos(a) - dz * std::sin(a), bz = dx * std::sin(a) + dz * std::cos(a);
  const XrVector3f feet{head.x + bx * 2.0f, scene.floorY, head.z + bz * 2.0f};
  const float clock = static_cast<float>(static_cast<double>(nowNs % 3600000000000ull) / 1e9);
  const float yaw = std::atan2(bz, -bx) + 0.35f * std::sin(clock * 0.7f);
  const float h = xrevo::height(ss.fig);
  const float t = static_cast<float>(nowNs - ss.since) / 1e9f;
  const float pop = std::min(1.0f, t / 0.25f);
  const float s = (h > 1.9f ? 1.9f / h : 1.0f) * (0.6f + 0.4f * pop);
  xrevo::pose(ss.fig, clock, feet, yaw, s, 0.0f, &ss.verts);
  xrfx::Scene::ModelDraw& md = scene.models[0];
  md.on = true;
  md.posNrm = ss.verts.data();
  md.white = std::max(0.0f, 1.0f - t / 0.35f);
  md.alpha = 1.0f;
  md.glow = 0.0f;
  md.under = 0.2f;
  md.flameFrame = static_cast<int>((nowNs / 83000000ull) % 3);
  md.shine = pack.find("_shiny") != std::string::npos ? 1.0f : 0.0f;  // r127: a shiny one on the menu glints
  scene.models[1].on = false;
}

// --- r131: the franchise's characters in 3D (FrFigAsk above): the hub's pages and the trades screen (each in front of
// the panel where its portrait was, fitted to its box, feet on its bottom edge, facing you) and awards night's winners
// in your room (life size, update_awards). Model slots 3-20 (kFrFigs). Each is read on a loader thread, one at a time:
// its pack and pose as the character select's and the home corner's (a Strikers captain: its r130 captain pack
// cap_<name>, else its Charged home figure), and its standing idle is baked there into at most 34 frames in its own
// space (Model::frames: they go up to the GPU with it, a frame is picked per draw and the renderer places it), so
// nothing is posed or copied per frame: a figure standing costs its draw calls. One goes up to the GPU a frame. They
// stay while the hub is open (its pages share them; the one shown longest ago makes room) and go when it shuts (but
// awards night's).
struct FrFigWant {
  std::string name;                   // its pack name: the portrait key, + "_shiny" for a shiny one
  bool shiny = false;
  bool draw = true;                   // shown this frame (else only loaded: its portrait / standee is on show still)
  int bake = 34;                      // frames of its idle at most
  XrVector3f base{0.0f, 0.0f, 0.0f};  // where its feet go (LOCAL) ...
  XrVector3f out{0.0f, 0.0f, 0.0f};   // ... moved this way (a unit vector, or none) by its depth
  float maxH = 1.0f, maxR = 0.5f;     // the height and half-width it fits in (metres)
  bool grow = true;                   // bigger than life to fill them (else at most life size)
};
struct FrFigLoaded {
  std::string name;
  bool ok = false, skipped = false;
  xrfx::Model model;
  float height = 1.0f, radius = 0.3f, fps = 30.0f;
};
struct FrFigSlot {
  std::string name;  // "" free
  float height = 1.0f, radius = 0.3f, fps = 30.0f;
  uint32_t frames = 0;
  uint64_t usedNs = 0, shownNs = 0, popNs = 0;
};
struct FrFigState {
  FrFigSlot slot[kFrFigs];
  std::vector<FrFigWant> wants;     // this frame's (awards night's, then the hub's)
  XrTime wantsFor = 0;              // the frame they're for (its display time)
  std::vector<std::string> none;    // no 3D figure on this headset (not asked again until the hub opens again)
  std::vector<std::string> queued;  // asked of the loader, not back yet
  std::vector<FrFigLoaded> loaded;  // read, waiting to go up
  std::string wantSig;              // the names wanted, as one string
  uint64_t wantSince = 0;           // since when (a stick scrolling past waits)
  int askGen = -1, fixedGen = -1;
  std::vector<FrFigAsk> asks;       // the hub's picture's
  bool hubWasOpen = false;
  bool redraw = false;              // who stands changed: the hub's picture is drawn again (at most 5 times a second)
  uint64_t redrawNs = 0;
};
FrFigState g_frFigs;
std::mutex g_frFigLoadMutex;  // the loader's: its queue, what's still wanted, what it read
std::vector<std::pair<std::string, int>> g_frFigQueue;
std::vector<std::string> g_frFigWanted;
std::vector<FrFigLoaded> g_frFigDone;
bool g_frFigLoading = false;

bool fr_fig_has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
void fr_fig_drop(std::vector<std::string>& v, const std::string& s) { v.erase(std::remove(v.begin(), v.end(), s), v.end()); }
bool fr_fig_file(const std::string& path) {
  struct stat st {};
  return ::stat(path.c_str(), &st) == 0 && st.st_size > 0;
}
// Its 3D figure off this headset's files (any thread), nullptr when it has none: a pack (its own shiny one first) and
// its standing pose (guests/evo/), else its home figure (guests/home/); a Strikers captain's r130 captain pack, else
// its Charged home figure.
xrevo::Figure* fr_fig_read(const std::string& name, xrfx::Model* model) {
  const char* env = std::getenv("STRIKERS_HOME");
  const std::string g = std::string(env != nullptr && *env != '\0' ? env : "/storage/emulated/0/Strikers") + "/guests/";
  const bool shiny = name.size() > 6 && name.compare(name.size() - 6, 6, "_shiny") == 0;
  const std::string key = shiny ? name.substr(0, name.size() - 6) : name;
  std::vector<std::pair<std::string, std::string>> tries;  // (pack, pose)
  if (key.compare(0, 9, "strikers_") == 0) {
    const std::string n = key.substr(9);
    tries.push_back({g + "cap_" + n + ".gpk", g + "evo/cap_" + n + ".gev"});
    tries.push_back({g + "home/" + key + ".gpk", g + "home/" + key + ".gev"});
  } else {
    if (shiny) tries.push_back({g + name + ".gpk", g + "evo/" + key + ".gev"});
    tries.push_back({g + key + ".gpk", g + "evo/" + key + ".gev"});
    tries.push_back({g + "home/" + key + ".gpk", g + "home/" + key + ".gev"});
    tries.push_back({g + key + ".gpk", g + "home/" + key + ".gev"});
  }
  for (const auto& t : tries) {
    if (!fr_fig_file(t.first) || !fr_fig_file(t.second)) continue;
    if (xrevo::Figure* f = xrevo::load_files(t.first, t.second, model)) return f;
  }
  return nullptr;
}
// Its standing idle (a loop file's first idle move, else its first move, else the whole pose file), every stride-th
// frame up to `most`, posed in its own space (feet at the origin, facing +X, Y up, its own size) into the model's
// frames; the frames a second they play at (0: none).
float fr_fig_bake(const xrevo::Figure* f, int most, xrfx::Model* m) {
  const float fps = xrevo::fps(f);
  const size_t per = size_t(m->vertexCount) * 6;
  if (fps <= 0.0f || per == 0) return 0.0f;
  const std::vector<xrevo::Segment>& segs = xrevo::segments(f);
  const xrevo::Segment* idle = nullptr;
  for (const xrevo::Segment& sg : segs)
    if (idle == nullptr && sg.kind == 0) idle = &sg;
  if (idle == nullptr && !segs.empty()) idle = &segs[0];
  uint32_t start = 0, n = static_cast<uint32_t>(std::lround(xrevo::duration(f) * fps));
  if (idle != nullptr) {
    start = idle->start;
    n = idle->frames;
  }
  n = std::max(1u, n);
  const uint32_t want = static_cast<uint32_t>(std::max(1, most));
  const uint32_t stride = std::max(1u, (n + want - 1) / want), count = std::max(1u, n / stride);
  m->frames.assign(per * count, 0.0f);
  std::vector<float> one;
  for (uint32_t i = 0; i < count; ++i) {
    xrevo::pose(f, (static_cast<float>(start + i * stride) + 0.001f) / fps, XrVector3f{0.0f, 0.0f, 0.0f}, 0.0f, 1.0f, 0.0f,
                &one);
    if (one.size() != per) {
      m->frames.clear();
      m->frameCount = 0;
      return 0.0f;
    }
    std::memcpy(m->frames.data() + per * i, one.data(), per * sizeof(float));
  }
  m->frameCount = count;
  return fps / static_cast<float>(stride);
}
void fr_figs_loader() {
  for (;;) {
    std::pair<std::string, int> job;
    {
      std::lock_guard<std::mutex> lock(g_frFigLoadMutex);
      if (g_frFigQueue.empty()) {
        g_frFigLoading = false;
        return;
      }
      job = g_frFigQueue.front();
      g_frFigQueue.erase(g_frFigQueue.begin());
      if (!fr_fig_has(g_frFigWanted, job.first)) {  // (scrolled past)
        FrFigLoaded l;
        l.name = job.first;
        l.skipped = true;
        g_frFigDone.push_back(std::move(l));
        continue;
      }
    }
    FrFigLoaded l;
    l.name = job.first;
    const auto t0 = std::chrono::steady_clock::now();
    if (xrevo::Figure* f = fr_fig_read(job.first, &l.model)) {
      l.height = xrevo::height(f);
      l.radius = xrevo::radius(f);
      l.fps = fr_fig_bake(f, job.second, &l.model);
      l.ok = l.fps > 0.0f && l.model.frameCount > 0;
      xrevo::release(f);
    }
    if (!l.ok) {
      l.model = xrfx::Model{};
    } else {
      XLOG("franchise 3D: %s read and baked in %.0f ms (%u vertices x %u frames)", l.name.c_str(),
           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), l.model.vertexCount,
           l.model.frameCount);
    }
    std::lock_guard<std::mutex> lock(g_frFigLoadMutex);
    g_frFigDone.push_back(std::move(l));
  }
}
// Asks for a figure this frame (awards night's, before fr_figs_frame); false when it's asked for twice (a model stands
// in one place: the second keeps its standee) or there's no slot for it.
bool fr_figs_want(const FrFigWant& w) {
  FrFigState& st = g_frFigs;
  if (st.wantsFor != g.frameState.predictedDisplayTime) {
    st.wants.clear();
    st.wantsFor = g.frameState.predictedDisplayTime;
  }
  for (const FrFigWant& o : st.wants)
    if (o.name == w.name) return false;
  if (static_cast<int>(st.wants.size()) >= kFrFigs) return false;
  st.wants.push_back(w);
  return true;
}
// Its figure is up (in a slot, on the GPU).
bool fr_fig_up(const std::string& name) {
  for (int k = 0; k < kFrFigs; ++k)
    if (g_frFigs.slot[k].name == name) return xrfx::model_up(3 + k);
  return false;
}
// Its size fitted into maxH x maxR (bigger than life: grow) - fr_figs_frame's - and how tall it stands then (0: not up).
float fr_fig_fit(float height, float radius, float maxH, float maxR, bool grow) {
  float s = std::min(maxH / std::max(0.05f, height), maxR / std::max(0.05f, radius));
  if (!grow) s = std::min(1.0f, s);
  return std::max(0.01f, s);
}
float fr_fig_tall(const std::string& name, float maxH, float maxR, bool grow) {
  for (int k = 0; k < kFrFigs; ++k) {
    const FrFigSlot& sl = g_frFigs.slot[k];
    if (sl.name == name) return fr_fig_fit(sl.height, sl.radius, maxH, maxR, grow) * sl.height;
  }
  return 0.0f;
}
// Its portrait left out of the hub's pictures (or back in): they're drawn again.
void fr_fig_stand(const std::string& name, bool on) {
  {
    std::lock_guard<std::mutex> lock(g_frFig.mutex);
    if (on == fr_fig_has(g_frFig.standing, name)) return;
    if (on) g_frFig.standing.push_back(name);
    else fr_fig_drop(g_frFig.standing, name);
  }
  g_frFigs.redraw = true;
}
// A slot let go (gpu: its model too; else the next set_model on it replaces it).
void fr_fig_free(int k, bool gpu) {
  FrFigSlot& sl = g_frFigs.slot[k];
  if (sl.name.empty()) return;
  fr_fig_stand(sl.name, false);
  if (gpu) xrfx::set_model(3 + k, xrfx::Model{});
  sl = FrFigSlot{};
}

// Every frame on the render thread, after update_awards: `hubQuad` the hub's / trades' picture on show (else null),
// `canDraw` the effects layer drawn this frame. True when a figure stands in front of that picture (it goes under the
// effects layer then, as the 3D evolution's card).
bool fr_figs_frame(xrfx::Scene& scene, const XrCompositionLayerQuad* hubQuad, bool canDraw, uint64_t nowNs) {
  FrFigState& st = g_frFigs;
  if (st.wantsFor != g.frameState.predictedDisplayTime) {  // (none of awards night's this frame)
    st.wants.clear();
    st.wantsFor = g.frameState.predictedDisplayTime;
  }
  const bool hubOpen = g.frOpen;
  if (hubOpen && !st.hubWasOpen) st.none.clear();  // (a pack the converter made since is tried again)
  st.hubWasOpen = hubOpen;
  if (st.redraw && nowNs - st.redrawNs > 200000000ull) {  // (figures arriving one after another: one picture a few)
    st.redraw = false;
    st.redrawNs = nowNs;
    ++g_frFigGen;
  }
  bool busy = !st.wants.empty() || !st.loaded.empty() || !st.queued.empty() || hubQuad != nullptr || st.redraw;
  for (int k = 0; k < kFrFigs && !busy; ++k) busy = !st.slot[k].name.empty();
  if (!busy) return false;  // (a match: nothing to do)
  {
    std::lock_guard<std::mutex> lock(g_frFig.mutex);
    if (g_frFig.askGen != st.askGen) {
      st.askGen = g_frFig.askGen;
      st.asks = g_frFig.asks;
    }
  }
  // the hub's picture's characters: their boxes on the panel as it stands now (moved, resized: they follow it)
  const size_t hubFirst = st.wants.size();
  if (hubQuad != nullptr) {
    const XrQuaternionf& q = hubQuad->pose.orientation;
    const XrVector3f& c = hubQuad->pose.position;
    const float qw = hubQuad->size.width, qh = hubQuad->size.height;
    const XrVector3f out = q_rot(q, XrVector3f{0.0f, 0.0f, 1.0f});
    bool fix = false;
    for (const FrFigAsk& a : st.asks) {
      if (static_cast<int>(st.wants.size()) >= kFrFigs) break;
      FrFigWant w;
      w.name = fr_fig_name(a.key, a.shiny);
      bool twice = false;
      for (const FrFigWant& o : st.wants) twice = twice || o.name == w.name;
      if (twice) continue;
      w.shiny = a.shiny;
      w.draw = a.omitted;
      w.bake = (a.y1 - a.y0) * static_cast<float>(kGuestH) >= 300.0f ? 34 : 17;  // (a small one: half its idle's frames)
      const XrVector3f at = q_rot(q, XrVector3f{(0.5f * (a.x0 + a.x1) - 0.5f) * qw, (0.5f - a.y1) * qh, 0.0f});
      w.base = XrVector3f{c.x + at.x, c.y + at.y, c.z + at.z};
      w.out = out;
      w.maxH = 0.98f * (a.y1 - a.y0) * qh;
      w.maxR = 0.5f * (a.x1 - a.x0) * qw;
      w.grow = true;
      if (a.omitted && !fr_fig_up(w.name)) fix = true;  // its picture left it out, but it's gone
      st.wants.push_back(w);
    }
    if (fix && st.fixedGen != st.askGen) {  // drawn again, with its portrait
      st.fixedGen = st.askGen;
      st.redraw = true;
    }
  }
  auto wanted = [&](const std::string& n) {
    for (const FrFigWant& w : st.wants)
      if (w.name == n) return true;
    return false;
  };
  std::string sig;
  for (const FrFigWant& w : st.wants) sig += w.name + "|";
  if (sig != st.wantSig) {
    st.wantSig = sig;
    st.wantSince = nowNs;
    std::lock_guard<std::mutex> lock(g_frFigLoadMutex);
    g_frFigWanted.clear();
    for (const FrFigWant& w : st.wants) g_frFigWanted.push_back(w.name);
  }
  // what the loader read
  {
    std::vector<FrFigLoaded> done;
    {
      std::lock_guard<std::mutex> lock(g_frFigLoadMutex);
      done.swap(g_frFigDone);
    }
    for (FrFigLoaded& l : done) {
      fr_fig_drop(st.queued, l.name);
      if (l.skipped) continue;
      if (!l.ok) {
        if (!fr_fig_has(st.none, l.name)) st.none.push_back(l.name);
        XLOG("franchise 3D: no 3D figure for %s on this headset (its picture stays)", l.name.c_str());
        continue;
      }
      if (wanted(l.name)) st.loaded.push_back(std::move(l));
    }
    st.loaded.erase(std::remove_if(st.loaded.begin(), st.loaded.end(), [&](const FrFigLoaded& l) { return !wanted(l.name); }),
                    st.loaded.end());
  }
  // the slots: gone from the GPU (the renderer restarted), shown, or not wanted with the hub shut
  for (int k = 0; k < kFrFigs; ++k) {
    FrFigSlot& sl = st.slot[k];
    if (sl.name.empty()) continue;
    if (!xrfx::model_up(3 + k)) {
      fr_fig_free(k, false);
    } else if (wanted(sl.name)) {
      sl.usedNs = nowNs;
    } else if (!hubOpen) {
      fr_fig_free(k, true);
    }
  }
  // asked of the loader once the wants have settled a moment (a stick scrolling past asks for nothing)
  if (!st.wants.empty() && nowNs - st.wantSince > 150000000ull) {
    std::vector<std::pair<std::string, int>> jobs;
    for (const FrFigWant& w : st.wants) {
      if (fr_fig_has(st.none, w.name) || fr_fig_has(st.queued, w.name)) continue;
      bool have = false;
      for (int k = 0; k < kFrFigs && !have; ++k) have = st.slot[k].name == w.name;
      for (const FrFigLoaded& l : st.loaded) have = have || l.name == w.name;
      if (have) continue;
      st.queued.push_back(w.name);
      jobs.push_back({w.name, w.bake});
    }
    if (!jobs.empty()) {
      std::lock_guard<std::mutex> lock(g_frFigLoadMutex);
      for (const auto& j : jobs) g_frFigQueue.push_back(j);
      if (!g_frFigLoading) {
        g_frFigLoading = true;
        std::thread(fr_figs_loader).detach();
      }
    }
  }
  // one up to the GPU a frame (its pictures' upload waits for the GPU): a free slot, else the one shown longest ago
  if (canDraw && !st.loaded.empty()) {
    int k = -1;
    for (int i = 0; i < kFrFigs && k < 0; ++i)
      if (st.slot[i].name.empty()) k = i;
    if (k < 0) {
      uint64_t oldest = UINT64_MAX;
      for (int i = 0; i < kFrFigs; ++i) {
        if (!wanted(st.slot[i].name) && st.slot[i].usedNs < oldest) {
          oldest = st.slot[i].usedNs;
          k = i;
        }
      }
    }
    if (k >= 0) {
      FrFigLoaded l = std::move(st.loaded.front());
      st.loaded.erase(st.loaded.begin());
      fr_fig_free(k, false);
      const uint32_t frames = l.model.frameCount;
      xrfx::set_model(3 + k, std::move(l.model));
      if (xrfx::model_up(3 + k)) {
        FrFigSlot& sl = st.slot[k];
        sl.name = l.name;
        sl.height = l.height;
        sl.radius = l.radius;
        sl.fps = l.fps;
        sl.frames = frames;
        sl.usedNs = nowNs;
        fr_fig_stand(l.name, true);
        XLOG("franchise 3D: %s stands in 3D (model slot %d, %u frames at %.0f a second, %.2f m tall)", l.name.c_str(), 3 + k,
             frames, l.fps, l.height);
      } else {
        if (!fr_fig_has(st.none, l.name)) st.none.push_back(l.name);
        XLOG("franchise 3D: %s didn't go up to the GPU (its picture stays)", l.name.c_str());
      }
    }
  }
  // where they stand this frame
  bool hubShown = false;
  if (canDraw && g.headValid) {
    const float clock = static_cast<float>(static_cast<double>(nowNs % 3600000000000ull) / 1e9);
    const XrVector3f head = g.headPose.position;
    for (size_t wi = 0; wi < st.wants.size(); ++wi) {
      const FrFigWant& w = st.wants[wi];
      if (!w.draw) continue;
      int k = -1;
      for (int i = 0; i < kFrFigs && k < 0; ++i)
        if (st.slot[i].name == w.name) k = i;
      if (k < 0 || st.slot[k].frames == 0) continue;
      FrFigSlot& sl = st.slot[k];
      const float s = fr_fig_fit(sl.height, sl.radius, w.maxH, w.maxR, w.grow);
      // it pops up out of a little white light when it comes on (as the character select's)
      if (nowNs - sl.shownNs > 300000000ull) sl.popNs = nowNs;
      sl.shownNs = nowNs;
      const float t = static_cast<float>(nowNs - sl.popNs) / 1e9f;
      const float pop = std::min(1.0f, t / 0.25f);
      // out of its panel (or back from its spot) by its depth, so none of it goes through
      const float depth = sl.radius * s + 0.01f;
      const XrVector3f feet{w.base.x + w.out.x * depth, w.base.y + w.out.y * depth, w.base.z + w.out.z * depth};
      const float dx = head.x - feet.x, dz = head.z - feet.z;
      float yaw = dx * dx + dz * dz > 1e-4f ? std::atan2(-dz, dx) : 0.0f;  // its front (+X) to you
      yaw += 0.12f * std::sin(clock * 0.5f + 1.7f * static_cast<float>(k));  // a little turn of its own
      xrfx::Scene::ModelDraw& md = scene.models[3 + k];
      md.on = true;
      md.posNrm = nullptr;
      md.frame = std::min(static_cast<int>(sl.frames) - 1,
                          static_cast<int>(std::fmod((clock + 0.31f * static_cast<float>(k)) * sl.fps, static_cast<float>(sl.frames))));
      md.at = feet;
      md.yaw = yaw;
      md.scale = s * (0.6f + 0.4f * pop);
      md.white = std::max(0.0f, 1.0f - t / 0.35f);
      md.alpha = 1.0f;
      md.glow = 0.0f;
      md.under = 0.12f;
      md.flameFrame = static_cast<int>((nowNs / 83000000ull) % 3);
      md.shine = w.shiny ? 1.0f : 0.0f;  // (r127: a shiny one glints)
      if (wi >= hubFirst) hubShown = true;
    }
  }
  // the panel as a solid in the effects layer: what's behind it in there stays hidden once it's under it
  if (hubShown && hubQuad != nullptr && scene.panelCount < 4) {
    scene.panels[scene.panelCount++] = xrfx::Scene::Panel{hubQuad->pose, hubQuad->size.width, hubQuad->size.height};
  }
  return hubShown;
}

void update_home(xrfx::Scene& scene, const XrCompositionLayerBaseHeader** layers, uint32_t* layerCount,
                 XrCompositionLayerQuad* quads) {
  g.homeSpot.shown = false;  // r120: set again below while the corner's up
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
      // r120: the rug and the figure on their own spot (g.homeSpot): the first time beside the shelf along its wall
      // (past its banner), at least 1.8 m from you, facing you; then where you leave it (grab it to move it)
      auto& spot = g.homeSpot;
      spot.floorY = floorY;
      if (!spot.placed && g.headValid) {
        XrVector3f p = at(-1.6f, 0.0f, 0.2f);
        const float hx = g.headPose.position.x, hz = g.headPose.position.z;
        float dx = p.x - hx, dz = p.z - hz;
        float d = std::sqrt(dx * dx + dz * dz);
        if (d < 0.05f) {
          floor_forward(g.headPose, &dx, &dz);
          d = 1.0f;
        }
        if (d < 1.8f) p = XrVector3f{hx + dx / d * 1.8f, p.y, hz + dz / d * 1.8f};
        spot.x = p.x;
        spot.z = p.z;
        spot.yaw = std::atan2(hx - p.x, hz - p.z);
        spot.scale = 1.0f;
        spot.placed = true;
        save_homespot();
        XLOG("home: figure placed at (%.2f, %.2f), %.2f m from you; grab it with a grip to move it", p.x, p.z,
             std::sqrt((p.x - hx) * (p.x - hx) + (p.z - hz) * (p.z - hz)));
      }
      const float spotCy = std::cos(spot.yaw), spotSy = std::sin(spot.yaw);
      auto on_spot = [&](float x, float z, float y) {
        return XrVector3f{spot.x + x * spotCy + z * spotSy, floorY + y, spot.z - x * spotSy + z * spotCy};
      };
      add_piece(scene, 7, on_spot(0.0f, 0.0f, 0.0f), yaw_quat(spot.yaw), 1.1f * spot.scale, c1, c2, homeMotif, 0.0f);
      spot.shown = true;
      spot.reach = 0.55f * spot.scale;
      spot.height = 1.0f;
      // r117 (Lance: "get rid of the little podium/pole thing, and just add the actual 3D character life size"):
      // no pedestal; the captain itself stands in the rug's middle, turned to you (up to 60 degrees off the
      // corner's own facing); the old display only for one with no 3D figure, floating where its top was
      const XrVector3f feet = on_spot(0.0f, 0.0f, 0.0f);
      float outYaw = std::atan2(-spotCy, spotSy);  // the figure's front (cos y, -sin y) = its spot's facing (sin, cos)
      {
        const float dx = g.headPose.position.x - feet.x, dz = g.headPose.position.z - feet.z;
        if (dx * dx + dz * dz > 0.01f) {
          float toYou = std::atan2(-dz, dx);
          float d = std::remainder(toYou - outYaw, 6.2831853f);
          d = std::max(-1.05f, std::min(1.05f, d));
          outYaw += d;
        }
      }
      // r118 (Lance: "make the fuckin 3d models for the strikers/charged ppl"): a Strikers captain's own figure too -
      // Charged's model and moves (tools/outside/charged_home.py: guests/home/strikers_<name>.gpk / .gev); the
      // Charged guests' (bowser, bowserjr, diddykong, petey) by their pack names
      std::string homeFig = pack;
      if (guestIdx < 0 && v.captain >= 0 && v.captain < 12) {
        static const char* const kCaptainFig[12] = {"", "daisy", "donkeykong", "", "", "luigi",
                                                    "mario", "peach", "", "waluigi", "wario", "yoshi"};
        if (kCaptainFig[v.captain][0] != '\0') homeFig = std::string("strikers_") + kCaptainFig[v.captain];
      }
      const bool figure = !homeFig.empty() && update_home_figure(scene, homeFig, feet, outYaw, now_ns(), spot.scale);
      if (homeFig.empty()) update_home_figure(scene, "", feet, outYaw, now_ns(), spot.scale);
      const bool standee = !figure && (homeFig.empty() || g_homeShow.missing);
      const float pedH = 0.8f;
      const XrVector3f topAt = figure ? XrVector3f{feet.x, floorY + 0.05f, feet.z} : XrVector3f{feet.x, floorY + pedH, feet.z};
      int shapes[3];
      uint32_t pc1[3], pc2[3];
      const int np = figure ? 0 : xrfx::corner_props(pack, v.captain, shapes, pc1, pc2);
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
      scene.cornerRug = feet;
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
      if (standee) {
        request_panel(g_cornerP, gen, kCornerW, kCornerH, [portrait, name](uint8_t* px, int w, int h) {
          const GuestPortrait& p = guest_portrait(portrait);
          xrtext::award_standee(px, w, h, p.rgba.empty() ? nullptr : p.rgba.data(), p.w, p.h, "HOME GROUND", name.c_str());
        });
      }
      if (standee && show_panel(g_cornerP, g_cornerPanel)) {
        const float height = 0.95f;
        const float width = height * static_cast<float>(kCornerW) / static_cast<float>(kCornerH);
        place_panel(g_cornerQuad, g_cornerPanel, on_spot(0.8f, -0.05f, 0.5f * height), width);
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
  std::string champCaptain;  // r131: the champions' captain (it lifts their cup in 3D)
  {
    std::lock_guard<std::mutex> lock(g_frMutex);
    const PortFranchiseView& v = g_frRoom.view;
    if (v.phase != PORT_FR_PHASE_DONE) return;
    for (int i = 0; i < v.awardCount && i < kAwardStandees; ++i) {
      text.push_back(v.awards[i]);
      keys.push_back(v.awardKeys[i]);
    }
    if (v.champion >= 0 && v.champion < PORT_FR_CLUBS) champCaptain = v.clubCaptainKey[v.champion];
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
  std::vector<std::string> figUsed;  // r131
  for (int i = 0; i < n; ++i) {
    const float lateral = (static_cast<float>(i) - 0.5f * (n - 1)) * (n > 5 ? 0.85f : 0.95f);
    const XrVector3f at{head.x + ux * dist + rx * lateral, floorY, head.z + uz * dist + rz * lateral};
    // its standee: "MVP: CHARIZARD (PEWTER CITY)" -> "MVP" over "CHARIZARD (PEWTER CITY)"
    const std::string& line = text[i];
    const size_t colon = line.find(':');
    const std::string award = colon == std::string::npos ? line : line.substr(0, colon);
    const std::string name = colon == std::string::npos ? std::string() : line.substr(colon + 2);
    const std::string key = keys[i];
    // r131 (Lance: "you have the awards, in 3D (trophies) but everything and everyone else is 2D"): the winner stands
    // there in 3D, life size (fr_figs_frame; the champions: their captain), its standee only its name plate, lying on
    // the floor in front of it; a club's award (the best keeper) and a winner with no 3D figure keep the standee
    std::string figKey = i == 0 && key.compare(0, 5, "club_") == 0 ? champCaptain : key;
    if (fr_fig_has(figUsed, figKey)) figKey.clear();  // (a model stands in one place: a second award keeps its standee)
    figUsed.push_back(figKey);
    const bool up = fr_fig_key(figKey) && fr_fig_up(figKey);
    request_panel(g_awardP[i], g.frRoomGen * 2 + (up ? 1 : 0), kAwardW, kAwardH, [key, award, name, up](uint8_t* px, int w, int h) {
      const GuestPortrait& p = guest_portrait(key);
      xrtext::award_standee(px, w, h, up || p.rgba.empty() ? nullptr : p.rgba.data(), p.w, p.h, award.c_str(), name.c_str());
    });
    const bool shown = show_panel(g_awardP[i], g_awardPanel[i]);
    const bool plate = up && shown && g_awardP[i].shown >= g_awardP[i].requested;  // (the plate-only picture on show)
    if (fr_fig_key(figKey)) {
      FrFigWant fw;
      fw.name = figKey;
      fw.draw = plate;
      fw.base = at;
      fw.out = XrVector3f{ux, 0.0f, uz};  // back from its spot by its depth: its cup is held up in front of it
      fw.maxH = 1.9f;
      fw.maxR = n > 5 ? 0.4f : 0.45f;
      fw.grow = false;
      fr_figs_want(fw);
    }
    if (shown && *layerCount < 32) {
      const float height = 1.55f;  // life-size-ish, the plate included
      const float width = height * static_cast<float>(kAwardW) / static_cast<float>(kAwardH);
      if (plate) {
        // r131: the picture's bottom fifth (award_standee's plate), 0.62 m wide, its top tipped away from you
        const Panel& pp = g_awardPanel[i];
        const int32_t py0 = static_cast<int32_t>(0.79f * static_cast<float>(pp.height));
        const float pw = 0.62f, ph = pw * static_cast<float>(static_cast<int32_t>(pp.height) - py0) / std::max(1.0f, static_cast<float>(pp.width));
        const float tilt = -0.95f;
        place_panel(g_awardQuad[i], pp, XrVector3f{at.x - ux * 0.42f, floorY + 0.5f * ph * std::cos(tilt) + 0.01f, at.z - uz * 0.42f}, pw);
        g_awardQuad[i].subImage.imageRect.offset = {0, py0};
        g_awardQuad[i].subImage.imageRect.extent = {static_cast<int32_t>(pp.width), static_cast<int32_t>(pp.height) - py0};
        g_awardQuad[i].size.height = ph;
        g_awardQuad[i].pose.orientation =
            q_mul(g_awardQuad[i].pose.orientation, XrQuaternionf{std::sin(0.5f * tilt), 0.0f, 0.0f, std::cos(0.5f * tilt)});
      } else {
        place_panel(g_awardQuad[i], g_awardPanel[i], XrVector3f{at.x, floorY + 0.5f * height, at.z}, width);
      }
      layers[(*layerCount)++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&g_awardQuad[i]);
    }
    // its gold trophy, held up in front of it (no pedestal: the room's pieces are 24 at most, and the
    // champion's bunting takes 9)
    const float yawToYou = std::atan2(head.x - at.x, head.z - at.z);
    // r131: in front of its chest when it stands in 3D
    const float tall = plate ? fr_fig_tall(figKey, 1.9f, n > 5 ? 0.4f : 0.45f, false) : 0.0f;
    const float cupY = tall > 0.0f ? std::max(0.45f, std::min(0.95f, 0.62f * tall)) : 0.8f;
    const XrVector3f cup{at.x - ux * 0.18f, floorY + cupY + 0.03f * std::sin(t * 1.3f + i), at.z - uz * 0.18f};
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

// r119: any room (a cup suite, an evolution's backdrop: evo_<theme>) from its three files <base>.room / _room.jpg /
// _outside.jpg; `key` what's loaded (one room at a time)
void request_room_files(const std::string& key, const std::string& base) {
  if (g.suiteLoaded == key) return;
  g.suiteLoaded = key;
  const int gen = ++g_suiteLoadGen;
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
      XWARN("room: none in %s.room", base.c_str());
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
      XWARN("room: could not read %s_room.jpg", base.c_str());
      return;
    }
    if (!load(base + "_outside.jpg", room.outside, room.outsideWidth, room.outsideHeight)) {
      XWARN("cup suite: could not read %s_outside.jpg (the window shows black)", base.c_str());
    }
    if (gen != g_suiteLoadGen.load()) return;  // another captain's asked for since
    XLOG("room: %s read in %.0f ms (%zu vertices, %zu LEDs, light %ux%u, outside %ux%u)", base.c_str(),
         static_cast<double>(now_ns() - t0) / 1e6, room.verts.size() / xrfx::Room::kRoomFloats,
         room.leds.size() / xrfx::Room::kLedFloats, room.width, room.height, room.outsideWidth, room.outsideHeight);
    xrfx::set_room(std::move(room));
  }).detach();
}

void request_suite_room(const std::string& which) { request_room_files(which, suites_dir() + "suite_" + which); }

// r119 (Lance: "the black screen that occurs when you're in VR, for an evolution ... a VR environment for evolutions
// to take place"; "the themed backdrops, thats fire"): the evolving one's setting, by its kind (tools/suites/
// evo_scene.py: suites/evo_<theme>.room / _room.jpg / _outside.jpg, you at the middle facing -Z, the evolution's
// spot clear in front)
const char* evo_backdrop(const std::string& key) {
  std::string k = key;
  const size_t shiny = k.find("_shiny");
  if (shiny != std::string::npos) k.resize(shiny);
  // r132 (Lance: "fire creatures/monsters/heroes have the fire background, water ... the water background"): the heroes
  // by their powers too; the street-level ones a city rooftop at night
  if (k.compare(0, 4, "mua_") == 0) {
    std::string h = k.substr(4);
    const size_t costume = h.find("_c");
    if (costume != std::string::npos) h.resize(costume);
    static const std::pair<const char*, const char*> kHero[] = {
        {"ghostrider", "fire"},      {"humantorch", "fire"},  {"thor", "electric"},   {"storm", "electric"},
        {"iceman", "water"},         {"drstrange", "psychic"}, {"invisiblewoman", "psychic"}, {"ironman", "sky"},
        {"silversurfer", "sky"},     {"msmarvel", "sky"},     {"blade", "ghost"},     {"moonknight", "ghost"},
        {"thing", "rock"},           {"colossus", "rock"},
        // r135: three more heroes and the nine NPCs, by their powers
        {"captainmarvel", "sky"},    {"lukecage", "rock"},    {"ronin", "city"},      {"nightcrawler", "ghost"},
        {"jeangrey", "psychic"},     {"professorx", "psychic"}, {"vision", "sky"},    {"drdoom", "electric"},  // his MUA blasts are hand lightning
        {"mysterio", "psychic"},     {"lizard", "grass"},     {"rhino", "rock"},      {"wintersoldier", "city"}};
    for (const auto& e : kHero)
      if (h == e.first) return e.second;
    return "city";
  }
  static const std::pair<const char*, const char*> kKind[] = {
      // fire: a volcano's rim
      {"pk_charmander", "fire"}, {"pk_charmeleon", "fire"}, {"pk_charizard", "fire"}, {"pk_torchic", "fire"},
      {"pk_combusken", "fire"}, {"pk_blaziken", "fire"}, {"pk_magby", "fire"}, {"pk_magmar", "fire"},
      {"pk_houndour", "fire"}, {"pk_houndoom", "fire"},
      // water: a lagoon
      {"pk_squirtle", "water"}, {"pk_wartortle", "water"}, {"pk_blastoise", "water"}, {"pk_poliwag", "water"},
      {"pk_poliwhirl", "water"}, {"pk_poliwrath", "water"}, {"pk_politoed", "water"}, {"pk_mudkip", "water"},
      {"pk_marshtomp", "water"}, {"pk_swampert", "water"},
      // electric: a plateau in a storm
      {"pk_pichu", "electric"}, {"pk_pikachu", "electric"}, {"pk_raichu", "electric"}, {"pk_voltorb", "electric"},
      {"pk_electrode", "electric"},
      // grass (and bugs): a forest clearing
      {"pk_bulbasaur", "grass"}, {"pk_ivysaur", "grass"}, {"pk_venusaur", "grass"}, {"pk_treecko", "grass"},
      {"pk_grovyle", "grass"}, {"pk_sceptile", "grass"}, {"pk_scyther", "grass"}, {"pk_scizor", "grass"},
      // ghost (and poison): a graveyard
      {"pk_gastly", "ghost"}, {"pk_haunter", "ghost"}, {"pk_gengar", "ghost"}, {"pk_shuppet", "ghost"},
      {"pk_banette", "ghost"}, {"pk_duskull", "ghost"}, {"pk_dusclops", "ghost"}, {"pk_koffing", "ghost"},
      {"pk_weezing", "ghost"},
      // psychic (and the Jigglypuff line): a cosmic void
      {"pk_drowzee", "psychic"}, {"pk_hypno", "psychic"}, {"pk_beldum", "psychic"}, {"pk_metang", "psychic"},
      {"pk_metagross", "psychic"}, {"pk_igglybuff", "psychic"}, {"pk_jigglypuff", "psychic"},
      {"pk_wigglytuff", "psychic"},
      // dragons: above the clouds
      {"pk_dratini", "sky"}, {"pk_dragonair", "sky"}, {"pk_dragonite", "sky"}, {"pk_bagon", "sky"},
      {"pk_shelgon", "sky"}, {"pk_salamence", "sky"},
      // rock, ground and fighting: a canyon arena
      {"pk_geodude", "rock"}, {"pk_graveler", "rock"}, {"pk_golem", "rock"}, {"pk_onix", "rock"},
      {"pk_steelix", "rock"}, {"pk_cubone", "rock"}, {"pk_marowak", "rock"}, {"pk_machop", "rock"},
      {"pk_machoke", "rock"}, {"pk_machamp", "rock"}, {"pk_mankey", "rock"}, {"pk_primeape", "rock"},
      // r127: the ten more and their evolutions
      {"pk_cyndaquil", "fire"}, {"pk_quilava", "fire"}, {"pk_typhlosion", "fire"}, {"pk_psyduck", "water"},
      {"pk_golduck", "water"}, {"pk_totodile", "water"}, {"pk_croconaw", "water"}, {"pk_feraligatr", "water"},
      {"pk_chikorita", "grass"}, {"pk_bayleef", "grass"}, {"pk_meganium", "grass"}, {"pk_abra", "psychic"},
      {"pk_kadabra", "psychic"}, {"pk_alakazam", "psychic"}, {"pk_ralts", "psychic"}, {"pk_kirlia", "psychic"},
      {"pk_gardevoir", "psychic"}, {"pk_larvitar", "rock"}, {"pk_pupitar", "rock"}, {"pk_tyranitar", "rock"},
      {"pk_aron", "rock"}, {"pk_lairon", "rock"}, {"pk_aggron", "rock"}, {"pk_trapinch", "rock"},
      {"pk_vibrava", "sky"}, {"pk_flygon", "sky"}, {"pk_elekid", "electric"}, {"pk_electabuzz", "electric"},
      // r130: the 20 lines after them
      {"pk_eevee", "sky"}, {"pk_vaporeon", "water"}, {"pk_jolteon", "electric"}, {"pk_flareon", "fire"},
      {"pk_espeon", "psychic"}, {"pk_umbreon", "ghost"}, {"pk_tyrogue", "rock"}, {"pk_hitmonlee", "rock"},
      {"pk_hitmonchan", "rock"}, {"pk_hitmontop", "rock"}, {"pk_mareep", "electric"}, {"pk_flaaffy", "electric"},
      {"pk_ampharos", "electric"}, {"pk_teddiursa", "grass"}, {"pk_ursaring", "grass"}, {"pk_lotad", "water"},
      {"pk_lombre", "water"}, {"pk_ludicolo", "water"}, {"pk_seedot", "grass"}, {"pk_nuzleaf", "grass"},
      {"pk_shiftry", "grass"}, {"pk_shroomish", "grass"}, {"pk_breloom", "grass"}, {"pk_slakoth", "grass"},
      {"pk_vigoroth", "grass"}, {"pk_slaking", "grass"}, {"pk_makuhita", "rock"}, {"pk_hariyama", "rock"},
      {"pk_whismur", "city"}, {"pk_loudred", "city"}, {"pk_exploud", "city"}, {"pk_meditite", "psychic"},
      {"pk_medicham", "psychic"}, {"pk_cacnea", "rock"}, {"pk_cacturne", "ghost"}, {"pk_nidoranm", "ghost"},
      {"pk_nidorino", "ghost"}, {"pk_nidoking", "rock"}, {"pk_nidoranf", "ghost"}, {"pk_nidorina", "ghost"},
      {"pk_nidoqueen", "rock"}, {"pk_kabuto", "water"}, {"pk_kabutops", "water"}, {"pk_anorith", "rock"},
      {"pk_armaldo", "rock"}, {"pk_wooper", "water"}, {"pk_quagsire", "water"}, {"pk_smoochum", "psychic"},
      {"pk_jynx", "psychic"}, {"pk_sandshrew", "rock"}, {"pk_sandslash", "rock"}, {"pk_slowpoke", "water"},
      {"pk_slowbro", "water"}, {"pk_slowking", "psychic"},
      // r135: the 162 more, by type (bugs grass; ice water; poison / dark ghost; fairies and round normal ones psychic;
      // fliers sky; the street's rats and cats city)
      {"pk_caterpie", "grass"}, {"pk_metapod", "grass"}, {"pk_butterfree", "grass"}, {"pk_weedle", "grass"},
      {"pk_kakuna", "grass"}, {"pk_beedrill", "grass"}, {"pk_pidgey", "sky"}, {"pk_pidgeotto", "sky"},
      {"pk_pidgeot", "sky"}, {"pk_rattata", "city"}, {"pk_raticate", "city"}, {"pk_spearow", "sky"},
      {"pk_fearow", "sky"}, {"pk_ekans", "ghost"}, {"pk_arbok", "ghost"}, {"pk_vulpix", "fire"},
      {"pk_ninetales", "fire"}, {"pk_zubat", "ghost"}, {"pk_golbat", "ghost"}, {"pk_crobat", "ghost"},
      {"pk_oddish", "grass"}, {"pk_gloom", "grass"}, {"pk_vileplume", "grass"}, {"pk_bellossom", "grass"},
      {"pk_paras", "grass"}, {"pk_parasect", "grass"}, {"pk_venonat", "grass"}, {"pk_venomoth", "grass"},
      {"pk_diglett", "rock"}, {"pk_dugtrio", "rock"}, {"pk_meowth", "city"}, {"pk_persian", "city"},
      {"pk_bellsprout", "grass"}, {"pk_weepinbell", "grass"}, {"pk_victreebel", "grass"}, {"pk_tentacool", "water"},
      {"pk_tentacruel", "water"}, {"pk_ponyta", "fire"}, {"pk_rapidash", "fire"}, {"pk_magnemite", "electric"},
      {"pk_magneton", "electric"}, {"pk_doduo", "sky"}, {"pk_dodrio", "sky"}, {"pk_seel", "water"},
      {"pk_dewgong", "water"}, {"pk_grimer", "ghost"}, {"pk_muk", "ghost"}, {"pk_shellder", "water"},
      {"pk_cloyster", "water"}, {"pk_krabby", "water"}, {"pk_kingler", "water"}, {"pk_exeggcute", "grass"},
      {"pk_exeggutor", "grass"}, {"pk_rhyhorn", "rock"}, {"pk_rhydon", "rock"}, {"pk_chansey", "psychic"},
      {"pk_blissey", "psychic"}, {"pk_horsea", "water"}, {"pk_seadra", "water"}, {"pk_kingdra", "water"},
      {"pk_goldeen", "water"}, {"pk_seaking", "water"}, {"pk_staryu", "water"}, {"pk_starmie", "water"},
      {"pk_magikarp", "water"}, {"pk_gyarados", "water"}, {"pk_porygon", "city"}, {"pk_porygon2", "city"},
      {"pk_omanyte", "water"}, {"pk_omastar", "water"}, {"pk_sentret", "grass"}, {"pk_furret", "grass"},
      {"pk_hoothoot", "sky"}, {"pk_noctowl", "sky"}, {"pk_ledyba", "grass"}, {"pk_ledian", "grass"},
      {"pk_spinarak", "grass"}, {"pk_ariados", "grass"}, {"pk_chinchou", "water"}, {"pk_lanturn", "water"},
      {"pk_cleffa", "psychic"}, {"pk_clefairy", "psychic"}, {"pk_clefable", "psychic"}, {"pk_togepi", "psychic"},
      {"pk_togetic", "psychic"}, {"pk_natu", "psychic"}, {"pk_xatu", "psychic"}, {"pk_hoppip", "grass"},
      {"pk_skiploom", "grass"}, {"pk_jumpluff", "grass"}, {"pk_sunkern", "grass"}, {"pk_sunflora", "grass"},
      {"pk_pineco", "grass"}, {"pk_forretress", "rock"}, {"pk_snubbull", "psychic"}, {"pk_granbull", "psychic"},
      {"pk_slugma", "fire"}, {"pk_magcargo", "fire"}, {"pk_swinub", "water"}, {"pk_piloswine", "water"},
      {"pk_remoraid", "water"}, {"pk_octillery", "water"}, {"pk_phanpy", "rock"}, {"pk_donphan", "rock"},
      {"pk_poochyena", "ghost"}, {"pk_mightyena", "ghost"}, {"pk_zigzagoon", "grass"}, {"pk_linoone", "grass"},
      {"pk_wurmple", "grass"}, {"pk_silcoon", "grass"}, {"pk_beautifly", "grass"}, {"pk_cascoon", "grass"},
      {"pk_dustox", "grass"}, {"pk_taillow", "sky"}, {"pk_swellow", "sky"}, {"pk_wingull", "water"},
      {"pk_pelipper", "water"}, {"pk_surskit", "grass"}, {"pk_masquerain", "grass"}, {"pk_nincada", "grass"},
      {"pk_ninjask", "grass"}, {"pk_shedinja", "ghost"}, {"pk_azurill", "psychic"}, {"pk_marill", "water"},
      {"pk_azumarill", "water"}, {"pk_skitty", "city"}, {"pk_delcatty", "city"}, {"pk_electrike", "electric"},
      {"pk_manectric", "electric"}, {"pk_gulpin", "ghost"}, {"pk_swalot", "ghost"}, {"pk_carvanha", "water"},
      {"pk_sharpedo", "water"}, {"pk_wailmer", "water"}, {"pk_wailord", "water"}, {"pk_numel", "fire"},
      {"pk_camerupt", "fire"}, {"pk_spoink", "psychic"}, {"pk_grumpig", "psychic"}, {"pk_swablu", "sky"},
      {"pk_altaria", "sky"}, {"pk_barboach", "water"}, {"pk_whiscash", "water"}, {"pk_corphish", "water"},
      {"pk_crawdaunt", "water"}, {"pk_baltoy", "psychic"}, {"pk_claydol", "psychic"}, {"pk_lileep", "rock"},
      {"pk_cradily", "rock"}, {"pk_feebas", "water"}, {"pk_milotic", "water"}, {"pk_wynaut", "psychic"},
      {"pk_wobbuffet", "psychic"}, {"pk_snorunt", "water"}, {"pk_glalie", "water"}, {"pk_spheal", "water"},
      {"pk_sealeo", "water"}, {"pk_walrein", "water"}, {"pk_clamperl", "water"}, {"pk_huntail", "water"},
      {"pk_gorebyss", "water"}};
  for (const auto& e : kKind)
    if (k == e.first) return e.second;
  return "sky";  // anyone else: above the clouds
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

// r49 (a player: "sometimes the characters get way too close during the highlight. Koopa and Peach nearly got within
// an inch of my face"): its feet never nearer than this to your head along the floor (r49c, Lance: 1.5 m "was still
// close af": 2.5 m).
constexpr float kScorerPersonalSpace = 2.5f;
// r144 (Lance on r143: "Arbok is getting too fucking close to me like he was gonna eat me"): and its BODY never
// nearer than this - its real posed reach toward you, a snake's coils and head or a hero's lunge, on top of its feet's
// distance (his r142 photo: Arbok's head reaches ~2.4 m out in front of its feet; at 2.5 m it was in his face): 1.3 m,
// more for the big ones (0.75 x their real height), up to 2.2 m.
constexpr float kScorerClearMin = 1.3f, kScorerClearMax = 2.2f;

// Meters a game unit of the scorer's copy (scorer_model's scale).
float scorer_scale() { return scorer_height_m() / std::max(0.2f, g.scorer.height); }

// r144: how far the body reaches from its feet toward a floor direction (ux, uz: unit, LOCAL), in meters, facing `yaw`
// (m4_game_to_room's): from its 8 reaches round it, the corner of the two that bracket the direction (it lies inside
// both, so this is never short).
float scorer_reach_toward(float yaw, float ux, float uz) {
  const auto& sc = g.scorer;
  if (!sc.shapeOk) return 0.0f;
  const float fx = std::cos(yaw), fz = -std::sin(yaw);   // its front (game +X) along the floor
  const float lx = -std::sin(yaw), lz = -std::cos(yaw);  // its left (game +Y)
  float a = std::atan2(ux * lx + uz * lz, ux * fx + uz * fz);
  if (a < 0.0f) a += 6.2831853f;
  const float step = 0.78539816f;
  const int k = std::min(7, std::max(0, static_cast<int>(a / step)));
  const float a0 = static_cast<float>(k) * step;
  const float r = (sc.reach[k] * std::sin(a0 + step - a) + sc.reach[(k + 1) & 7] * std::sin(a - a0)) / std::sin(step);
  return std::max(0.0f, r) * scorer_scale();
}

// r144b (the coordinator: Lance plays MR in his living room, and 3.9-4.8 m put Arbok / Onix in or past his walls): a body
// whose reach toward you would keep its feet farther out than this turns side-on instead (scorer_place: up to 90
// degrees, the way that needs less), so its length runs across your view and its reach toward you fits.
constexpr float kScorerFarthest = 3.2f;

// r144: once a display frame, before the scorer is placed: its body's latest shape folded into this show's (the most
// of each, so a lunge or a long tail once seen stays counted), its turn (r144b), and how far its feet must stay from
// your head for its body to stay clear. While it runs in that is set at once (as it will stand, turned to you); once
// it is here it backs off at up to 3 m/s for a reach it only now shows (a lunge), and never comes back nearer in the
// show.
void update_scorer_space() {
  auto& sc = g.scorer;
  const uint64_t now = now_ns();
  const float dt = sc.spaceNs != 0 && now > sc.spaceNs ? std::min(0.1f, static_cast<float>(now - sc.spaceNs) / 1e9f) : 0.0f;
  sc.spaceNs = now;
  if (sc.phase == 0 || !sc.planned) return;
  PortXrBodyShape shape;
  if (PortXrBodyShapeNow(&shape) && shape.gen != sc.shapeGen && shape.postedNs + 100000000ull >= sc.planNs) {
    sc.shapeGen = shape.gen;
    sc.cur = shape;
    // (rise / side are along the direction it is seen from: a new one starts them afresh)
    const bool sameView = sc.shapeOk && std::fabs(shape.view - sc.shapeView) < 0.1f;
    if (!sameView) sc.shapeView = shape.view;
    for (int k = 0; k < 8; ++k) sc.reach[k] = sc.shapeOk ? std::max(sc.reach[k], shape.reach[k]) : shape.reach[k];
    for (int k = 0; k < kPortXrBodySlopeCount; ++k) {
      sc.rise[k] = sameView ? std::max(sc.rise[k], shape.rise[k]) : shape.rise[k];
      for (int d = 0; d < 2; ++d) sc.sideReach[d][k] = sameView ? std::max(sc.sideReach[d][k], shape.side[d][k]) : shape.side[d][k];
    }
    sc.top = sc.shapeOk ? std::max(sc.top, shape.top) : shape.top;
    if (sc.phase == 1 || !sc.standTopOk) {  // r144b: its size as it runs in (then kept)
      sc.standTop = sc.standTopOk ? std::max(sc.standTop, shape.top) : shape.top;
      sc.standTopOk = true;
    }
    sc.shapeOk = true;
  }
  if (!sc.shapeOk || !g.headValid) return;
  const float s = scorer_scale();
  const float clear = std::min(kScorerClearMax, std::max(kScorerClearMin, 0.75f * sc.standTop * s));
  // where it stands (or will: its spot while it runs in), and you from there
  const XrVector3f& at = sc.phase == 1 ? sc.spot : sc.lastPos;
  const float hx = g.headPose.position.x - at.x, hz = g.headPose.position.z - at.z;
  const float hl = std::sqrt(hx * hx + hz * hz);
  const float ux = hl > 1e-3f ? hx / hl : 0.0f, uz = hl > 1e-3f ? hz / hl : -1.0f;
  const float yaw0 = yaw_facing(ux, uz);  // facing you
  // its feet's distance for it turned phi from facing you
  auto need = [&](float phi) { return std::max(kScorerPersonalSpace, clear + scorer_reach_toward(yaw0 + phi, ux, uz)); };
  // r144b: its turn - none if facing you fits in kScorerFarthest; else the least turn (22.5-degree steps, up to 90, the
  // way that needs less) that fits, else the one that needs least. Chosen freely while it runs in; once here, only
  // again if a reach it shows now (a lunge) no longer fits, and only for a real gain.
  if (sc.phase == 1 || need(sc.turnTarget) > kScorerFarthest + 0.05f) {
    float best = 0.0f, bestNeed = need(0.0f);
    if (bestNeed > kScorerFarthest) {
      for (int i = 1; i <= 4; ++i) {
        const float a = static_cast<float>(i) * 0.39269908f;
        const float np = need(a), nm = need(-a);
        const float n = std::min(np, nm);
        if (n < bestNeed - 0.05f) {
          bestNeed = n;
          // the way that needs less; within 10 cm, the way it already turns (no flip-flopping as it runs in)
          best = std::fabs(np - nm) < 0.1f && sc.turnTarget != 0.0f ? (sc.turnTarget > 0.0f ? a : -a) : (np <= nm ? a : -a);
        }
        if (n <= kScorerFarthest) break;
      }
      // only a long body (the turn wins it half a metre or more): a hero lunging at you stays facing you
      if (need(0.0f) - bestNeed < 0.5f) {
        best = 0.0f;
        bestNeed = need(0.0f);
      }
    }
    if (sc.phase == 1 || bestNeed < need(sc.turnTarget) - 0.1f) {
      if (best != sc.turnTarget)
        XLOG("scorer: its body turned %.0f degrees from facing you (its feet %.2f m out instead of %.2f)",
             best * 57.29578f, bestNeed, need(0.0f));
      sc.turnTarget = best;
    }
  }
  // the turn as it stands: at once while it runs in (it turns to you at the end of its run), else eased
  if (sc.phase == 1) {
    sc.turn = sc.turnTarget;
  } else {
    const float d = sc.turnTarget - sc.turn, step = 1.5f * dt;
    sc.turn += std::max(-step, std::min(step, d));
  }
  PortXrBodySetView(-sc.turn);  // (its rise / side worked out as you see it, for the banner)
  float want = std::max(need(sc.turnTarget), need(sc.turn));
  if (sc.phase != 1) {  // and as it stands right now (a turn on its way out, its tail)
    const float cx = g.headPose.position.x - sc.lastPos.x, cz = g.headPose.position.z - sc.lastPos.z;
    const float cl = std::sqrt(cx * cx + cz * cz);
    if (cl > 1e-3f) want = std::max(want, clear + scorer_reach_toward(sc.lastYaw, cx / cl, cz / cl));
  }
  if (want > sc.keepOff) sc.keepOff = sc.phase == 1 ? want : std::min(want, sc.keepOff + 3.0f * dt);
  if (sc.phase == 2 && !sc.spaceLogged) {
    sc.spaceLogged = true;
    XLOG("scorer: its body %.2f m tall, reaching %.2f m in front of its feet (%.2f m to the side), turned %.0f degrees "
         "-> feet kept %.2f m from you (its body %.2f m clear)",
         sc.top * s, std::max(0.0f, sc.reach[0]) * s, std::max(sc.reach[2], sc.reach[6]) * s, sc.turnTarget * 57.29578f,
         sc.keepOff, clear);
  }
}
// A show's path, from where you are when it starts: the spot is out in front (below the big screen)
// or beside the table, and the scorer comes from farther out, running in at PORT_SCORER_SPEED, and
// leaves the same way.
void plan_scorer() {
  // r144: a new show: its body's reach is measured afresh (update_scorer_space)
  g.scorer.shapeOk = false;
  g.scorer.keepOff = kScorerPersonalSpace;
  g.scorer.planNs = now_ns();
  g.scorer.spaceLogged = false;
  g.scorer.bannerKnown = false;
  g.scorer.bannerMode = 0;
  g.scorer.bannerNs = 0;
  g.scorer.turn = g.scorer.turnTarget = 0.0f;  // r144b
  g.scorer.standTopOk = false;
  g.scorer.shapeView = 0.0f;
  PortXrBodySetView(0.0f);
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
    // The last 0.35 s it turns to face you (r144b: or side-on, a long body: sc.turn, update_scorer_space).
    *yaw = turn_toward(run, toward_head(sc.spot) + sc.turn, (sc.phaseTime - (runIn - 0.35f)) / 0.35f);
  } else if (sc.phase == 2) {
    *pos = sc.spot;
    *yaw = toward_head(sc.spot) + sc.turn;
  } else {
    const float t = sc.phaseTime;
    *pos = {sc.leaveFrom.x + sc.away.x * PORT_SCORER_SPEED * t, sc.spot.y,
            sc.leaveFrom.z + sc.away.z * PORT_SCORER_SPEED * t};
    // A quick turn away first.
    *yaw = turn_toward(toward_head(sc.leaveFrom) + sc.turn, yaw_facing(sc.away.x, sc.away.z), t / 0.3f);
  }
  // r49 (a player: "sometimes the characters get way too close during the highlight. Koopa and Peach
  // nearly got within an inch of my face"): never nearer than kScorerPersonalSpace to your head, along
  // the floor, wherever you have moved since its path was planned (it is planned once, from where you
  // stood at the goal), and on the way in and out too. It slides round you instead of through you.
  // r144: or farther, for its real body to stay clear of you (keepOff, update_scorer_space): it stays out in front.
  const float space = std::max(kScorerPersonalSpace, sc.keepOff);
  if (g.headValid) {
    const float hx = pos->x - g.headPose.position.x, hz = pos->z - g.headPose.position.z;
    const float d = std::sqrt(hx * hx + hz * hz);
    if (d < space) {
      float ux = hx, uz = hz;
      if (d < 1e-3f) {  // right on top of you: back off the way it faces
        ux = std::cos(*yaw);
        uz = -std::sin(*yaw);
      }
      const float ul = std::max(1e-3f, std::sqrt(ux * ux + uz * uz));
      pos->x = g.headPose.position.x + ux / ul * space;
      pos->z = g.headPose.position.z + uz / ul * space;
      if (sc.phase == 2) *yaw = toward_head(*pos) + sc.turn;
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
  // r144: where its banner really is (scorer_banner_place: over its real body, or beside it), once placed
  const XrVector3f& b = g.scorer.bannerKnown ? g.scorer.bannerAt : g.scorer.spot;
  const float sx = b.x - head.x, sz = b.z - head.z;
  const float qx = center.x - head.x, qz = center.z - head.z;
  const float ds = std::sqrt(sx * sx + sz * sz), dq = std::sqrt(qx * qx + qz * qz);
  if (ds < 0.5f || dq < 0.5f) return 0.0f;
  const float bannerW = g.scorer.bannerKnown ? g.scorer.bannerWidth : 1.1f * std::max(1.0f, ds / 2.5f);  // as drawn
  const float bannerTop = g.scorer.bannerKnown ? g.scorer.bannerTop
                                               : g.scorer.spot.y + scorer_height_m() + 0.18f +
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

// r144: the smallest slope m (from kPortXrBodySlopes) with s x support(m) - m x D - off <= 0, i.e. every point of the
// body under (or beside) the line from your eyes at that slope: g(m) falls as m grows and is convex, so between the
// last slope it fails and the first it holds the chord's root is a safe bound. False when even the steepest fails.
bool scorer_slope_bound(const float* support, float s, float D, float off, float* mu) {
  float prevM = 0.0f, prevG = 0.0f;
  for (int i = 0; i < kPortXrBodySlopeCount; ++i) {
    const float m = kPortXrBodySlopes[i];
    const float gm = s * support[i] - m * D - off;
    if (gm <= 0.0f) {
      *mu = i == 0 ? m : prevM + (m - prevM) * prevG / std::max(1e-6f, prevG - gm);
      return true;
    }
    prevM = m;
    prevG = gm;
  }
  return false;
}

// r144 (Lance on r143: the banner "right over the goal scorers faces literally" - Ghost Rider II's: a hero taller than
// his slot's class height, which r42-r143 hung it over): where the goal banner goes (its middle) and how wide it is,
// `pop` its pop-up size (it grows up from its bottom edge); once a display frame. Over the scorer AS YOU SEE IT: its
// bottom edge above every point of the real body from your eyes (the steepest the body rises toward you: rise[]) and
// 20 cm (at 2.5 m; more farther out) over its top, turned to you (place_panel); it follows the pose - up at once when
// the body rises (a jump: never across its face), back down slowly. If the body's whole show so far would put it off
// the top of your view (a giant close by: its top edge over ~39 degrees up), it goes beside the body for the show,
// at your eye height, on the side the body reaches least toward, past every point of it as you see it (side[]).
void scorer_banner_place(float pop, XrVector3f* at, float* width) {
  auto& sc = g.scorer;
  const XrVector3f& feet = sc.lastPos;
  const XrVector3f& eye = g.headPose.position;
  const float dx = feet.x - eye.x, dz = feet.z - eye.z;
  const float D = std::max(0.5f, std::sqrt(dx * dx + dz * dz));
  const float k = std::max(1.0f, D / 2.5f);  // r43b: it keeps its size to the eye
  const float full = 1.1f * k;
  const float h = full * static_cast<float>(kBannerH) / static_cast<float>(kBannerW);
  *width = full * pop;
  const float hNow = *width * static_cast<float>(kBannerH) / static_cast<float>(kBannerW);
  const uint64_t now = now_ns();
  const float dt = sc.bannerNs != 0 && now > sc.bannerNs ? std::min(0.1f, static_cast<float>(now - sc.bannerNs) / 1e9f) : 0.0f;
  sc.bannerNs = now;
  float bottom = sc.spot.y + scorer_height_m() + 0.18f;  // r42-r143 (until its body's shape is known)
  *at = {feet.x, bottom + 0.5f * hNow, feet.z};
  if (sc.shapeOk) {
    const float s = scorer_scale();
    const float e = eye.y - feet.y;
    // over a shape: its bottom edge (LOCAL y); false when that is off the top of your view
    auto over = [&](const float* rise, float top, float* b) {
      float mu = 2.0f;
      const bool ok = scorer_slope_bound(rise, s, D, e, &mu);
      *b = std::max(feet.y + top * s + 0.2f * k, eye.y + mu * D + 0.12f * k);
      return ok && (*b + h - eye.y) / D <= 0.8f;  // (its top edge ~39 degrees up at most)
    };
    if (sc.bannerMode == 0) {
      float b = 0.0f;
      sc.bannerMode = over(sc.rise, sc.top, &b) ? 1 : 2;
      XLOG("scorer: its banner %s it (its body %.2f m tall, %.2f m out)", sc.bannerMode == 1 ? "over" : "beside",
           sc.top * s, D);
    }
    if (sc.bannerMode == 1) {
      float target = 0.0f;
      over(sc.cur.rise, sc.cur.top, &target);
      sc.bannerBottom = sc.bannerKnown ? std::max(target, sc.bannerBottom - 0.5f * dt) : target;
      bottom = sc.bannerBottom;
      *at = {feet.x, bottom + 0.5f * hNow, feet.z};
    } else {
      // the side it reaches least toward you (its left, game +Y, is your right)
      float lamL = 2.0f, lamR = 2.0f;
      scorer_slope_bound(sc.sideReach[0], s, D, 0.0f, &lamL);
      scorer_slope_bound(sc.sideReach[1], s, D, 0.0f, &lamR);
      const bool onLeft = lamL <= lamR;
      const float off = (std::max(0.0f, onLeft ? lamL : lamR) + 0.06f) * D + 0.5f * full;
      const float sgn = onLeft ? 1.0f : -1.0f;
      // across your view: its left as it faces you (side[] are worked out along you, PortXrBodySetView; r144b: turned too)
      const float lx = -dz / D, lz = dx / D;
      bottom = std::max(feet.y + 0.3f, eye.y - 0.5f * h);
      *at = {feet.x + sgn * lx * off, bottom + 0.5f * hNow, feet.z + sgn * lz * off};
    }
  }
  sc.bannerKnown = true;
  sc.bannerAt = *at;
  sc.bannerAt.y = bottom + 0.5f * h;
  sc.bannerTop = bottom + h;
  sc.bannerWidth = full;
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
// single-pass stereo. r131: `height` 0 = an eye's (the big screen's images are the game picture's size).
bool create_eye_image(EyeImage& out, VkFormat srgb, VkFormat unorm, uint32_t width, uint32_t height = 0) {
  if (height == 0) height = g.eyeHeight;
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
  ci.extent = {width, height, 1};
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
    tex.size = {width, height, 1};
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
  bool noFence[3] = {false, false, false};  // r124b: released without a fence to wait on
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
      noFence[i] = true;
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
  // r125 (Lance: "get the FPS up in stadium jumbotron mode"; his r123 log, Jumbotron · Stadium: 49-51 fps, PortXrEndFrame
  // 7.0-7.5 ms a game frame against Tabletop · Stadium's ~2, all of it cpu-wait ~5 ms + sync ~1.6): there the main eye
  // image is opened every game frame but not drawn (only the split stadium and the screen are), so Dawn ends its access
  // with no fence, and that "no fence" made every frame wait on the CPU for ALL of Dawn's queue. Only an image that is
  // going to be shown needs that fallback; one that is not is never read.
  for (int i = 0; i < blitCount; ++i) {
    const int which = blits[i].stadium ? 2 : (blits[i].img == &slot.eye[1] ? 1 : 0);
    if (noFence[which]) needCpuWait = true;
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
  // r131: the big screen's image this frame copied its picture into (GPU-direct): its access ends here too, its fence
  // kept for finish_screen_slots' blit
  for (uint32_t s = 0; s < kScreenSlots; ++s) {
    if (g_screenState[s].load(std::memory_order_acquire) != 1 ||
        g_screenFrame[s].load(std::memory_order_relaxed) != frame) {
      continue;
    }
    ScreenSlot& slot = g.screenSlots[s];
    g_screenCopied[s].store(aurora::gfx::xr_screen_copied(frame) ? 1u : 0u, std::memory_order_relaxed);
    if (slot.img.accessOpen) release_eye_image(slot.img);
    g_screenState[s].store(2, std::memory_order_release);
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

// r131: the GPU-direct big screen's images gone (a new picture size, unavailable, shutdown): Dawn done with them first
// (the render worker idle: every frame ended has been submitted and its access ended in on_aurora_submit; one still
// open, for a frame never ended, ends here and its copy is called off), their blits done on the XR queue.
void destroy_screen_slots() {
  bool any = false;
  for (const ScreenSlot& slot : g.screenSlots) {
    any = any || slot.img.image != VK_NULL_HANDLE || slot.cmd != VK_NULL_HANDLE || slot.fence != VK_NULL_HANDLE;
  }
  if (!any) return;
  aurora::gfx::synchronize();
  for (uint32_t s = 0; s < kScreenSlots; ++s) {
    ScreenSlot& slot = g.screenSlots[s];
    EyeImage& img = slot.img;
    if (g_screenState[s].load(std::memory_order_acquire) == 1) {
      aurora::gfx::set_xr_screen_copy(g_screenFrame[s].load(std::memory_order_relaxed), wgpu::Texture{}, true);
    }
    if (img.accessOpen) release_eye_image(img);
    if (img.releaseFd >= 0) close(img.releaseFd);
    img.releaseFd = -1;
    if (slot.pending && slot.fence != VK_NULL_HANDLE) vkWaitForFences(g.device, 1, &slot.fence, VK_TRUE, UINT64_MAX);
    img.renderView = nullptr;
    img.texture = nullptr;
    img.shared = nullptr;
    if (img.dawnDone != VK_NULL_HANDLE) vkDestroySemaphore(g.device, img.dawnDone, nullptr);
    if (img.image != VK_NULL_HANDLE) vkDestroyImage(g.device, img.image, nullptr);
    if (img.memory != VK_NULL_HANDLE) vkFreeMemory(g.device, img.memory, nullptr);
    if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(g.device, slot.fence, nullptr);
    if (slot.cmd != VK_NULL_HANDLE) vkFreeCommandBuffers(g.device, g.pool, 1, &slot.cmd);
    slot = ScreenSlot{};
    g_screenState[s].store(0, std::memory_order_release);
    g_screenFrame[s].store(0, std::memory_order_relaxed);
    g_screenCopied[s].store(0, std::memory_order_relaxed);
  }
  g.screenW = g.screenH = 0;
  g.screenArmedValid = false;
}

// r131: the screen images at the game picture's size: made on first use, remade when the size changes (rare), with the
// FIFO and render workers idle (Dawn's import runs in an error scope). False: none (the picture is read back as before).
bool ensure_screen_slots(uint32_t width, uint32_t height) {
  if (g.screenW == width && g.screenH == height) return true;
  if (g.screenDirectFailed) return false;
  auto fail = [](const char* why) {
    XERR("big screen: GPU-direct unavailable (%s); its picture is read back and uploaded as before", why);
    g.screenDirectFailed = true;
    destroy_screen_slots();
    return false;
  };
  if (!aurora::webgpu::g_vulkanSharedImageExportSupported) return fail("Dawn has no opaque-fd/sync-fd sharing");
  if (!g.hasExtMemFd || !g.hasExtSemFd) return fail("XR device lacks external memory/semaphore fd");
  if (aurora::webgpu::present_source().format != wgpu::TextureFormat::RGBA8Unorm) return fail("picture not RGBA8Unorm");
  AuroraGXSync();
  destroy_screen_slots();  // the old size's (its render worker wait)
  aurora::gfx::synchronize();
  for (uint32_t s = 0; s < kScreenSlots; ++s) {
    ScreenSlot& slot = g.screenSlots[s];
    if (!create_eye_image(slot.img, VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_R8G8B8A8_UNORM, width, height)) {
      return fail("shared image");
    }
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = g.pool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!vk_ok(vkAllocateCommandBuffers(g.device, &allocInfo, &slot.cmd), "vkAllocateCommandBuffers(screen)") ||
        !vk_ok(vkCreateFence(g.device, &fenceInfo, nullptr, &slot.fence), "vkCreateFence(screen)")) {
      return fail("sync objects");
    }
  }
  g.screenW = width;
  g.screenH = height;
  XLOG("big screen: GPU-direct picture %ux%u (%u shared images): no readback, no upload", width, height, kScreenSlots);
  return true;
}

// r131: this game frame's big-screen picture, GPU-direct (Jumbotron · Living Room, the cup suite; not the 3D views): a
// free screen image opened to Dawn and the open Aurora frame told to copy its picture into it. Game thread, once a game
// frame (PortXrFrameLeaveOpen), the frame still open. g.screenDirectThisFrame: this frame's picture goes this way, with
// no readback (unless the newspaper wants a photo); with no free image it has none (the screen keeps the last picture).
void arm_screen_slot() {
  g.screenDirectThisFrame = false;
  if (!g.active || !g.screenDirectOn || g.screenDirectFailed || stereo_mode() || !aurora_frame_is_open()) return;
  const uint32_t frame = aurora_xr_current_frame();
  if (g.screenArmedValid && g.screenArmedFrame == frame) {
    g.screenDirectThisFrame = true;  // armed already: the frame stayed open over a loop that sent nothing
    return;
  }
  const auto& src = aurora::webgpu::present_source();
  const uint32_t width = src.size.width, height = src.size.height;
  if (width == 0 || height == 0 || !ensure_screen_slots(width, height)) return;
  int pick = -1;
  for (uint32_t s = 0; s < kScreenSlots && pick < 0; ++s) {
    ScreenSlot& slot = g.screenSlots[s];
    const uint32_t state = g_screenState[s].load(std::memory_order_acquire);
    if (state == 1 && static_cast<int32_t>(frame - g_screenFrame[s].load(std::memory_order_relaxed)) > 4) {
      // Its frame's submit was never seen (should not happen): its access ends here, with the render worker idle; no
      // picture (finish_screen_slots frees it)
      XWARN("screen image %u: frame %u never reported submitted", s, g_screenFrame[s].load());
      aurora::gfx::synchronize();
      if (g_screenState[s].load(std::memory_order_acquire) == 1) {
        if (slot.img.accessOpen) release_eye_image(slot.img);
        g_screenCopied[s].store(0, std::memory_order_relaxed);
        g_screenState[s].store(2, std::memory_order_release);
      }
      continue;
    }
    if (state != 0) continue;
    if (slot.pending) {
      if (vkGetFenceStatus(g.device, slot.fence) != VK_SUCCESS) continue;  // its blit still on the GPU: no CPU wait
      vkResetFences(g.device, 1, &slot.fence);
      slot.pending = false;
    }
    pick = static_cast<int>(s);
  }
  g.screenDirectThisFrame = true;
  g.screenArmedValid = true;
  g.screenArmedFrame = frame;
  const bool photo = false;  // r143: no read-back for a newspaper photo any more (r113's, from the read-back picture)
  if (pick >= 0) {
    ScreenSlot& slot = g.screenSlots[pick];
    // Dawn's Vulkan backend requires the layout chain; UNDEFINED both ways: the copy rewrites every pixel
    wgpu::SharedTextureMemoryVkImageLayoutBeginState layoutBegin{};
    layoutBegin.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    layoutBegin.newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    wgpu::SharedTextureMemoryBeginAccessDescriptor desc{};
    desc.nextInChain = &layoutBegin;
    desc.initialized = true;
    slot.img.accessOpen = static_cast<bool>(slot.img.shared.BeginAccess(slot.img.texture, &desc));
    if (slot.img.accessOpen) {
      aurora::gfx::set_xr_screen_copy(frame, slot.img.texture, photo);
      g_screenCopied[pick].store(0, std::memory_order_relaxed);
      g_screenFrame[pick].store(frame, std::memory_order_relaxed);
      g_screenState[pick].store(1, std::memory_order_release);
      ++g.screenCopiesSinceStats;
      return;
    }
  }
  ++g.screenNoSlotSinceStats;
  aurora::gfx::set_xr_screen_copy(frame, wgpu::Texture{}, photo);  // no copy, and no readback, this frame
}

// r131: whether Dawn's copy into a released screen image is done on the GPU: its sync fd polled, never waited on. Not a
// sync fd (Dawn's opaque-semaphore fallback) can't be asked: true, and its blit waits for it on the GPU instead.
bool screen_picture_done(const EyeImage& img) {
  if (img.releaseFd < 0 || img.releaseFdType != VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT) return true;
  pollfd p{};
  p.fd = img.releaseFd;
  p.events = POLLIN;
  return poll(&p, 1, 0) != 0;  // signalled (or an error: the import then falls back as the eyes' does)
}

// r131: the newest GPU-direct picture whose copy the GPU has finished goes into the screen swapchain (a blit on the XR
// queue; its wait on Dawn's fence is then already met, so nothing on the XR queue - the effects, the next frame's layers
// - ever waits behind Dawn's frame: with passthrough the GPU runs slow, his r129 log: 11-16 ms submit to done); older
// pictures are dropped, a newer one not done yet waits for the next display frame. Game thread, every display frame
// (PortXrEndFrame).
void finish_screen_slots() {
  if (g.screenW == 0) return;
  // (states as of now: a slot the render worker releases meanwhile, newer, waits for the next call)
  uint32_t state[kScreenSlots];
  int newest = -1;
  bool waiting = false;
  auto newer = [](uint32_t a, uint32_t b) {
    return static_cast<int32_t>(g_screenFrame[a].load(std::memory_order_relaxed) -
                                g_screenFrame[b].load(std::memory_order_relaxed)) > 0;
  };
  for (uint32_t s = 0; s < kScreenSlots; ++s) {
    state[s] = g_screenState[s].load(std::memory_order_acquire);
    if (state[s] != 2 || g_screenCopied[s].load(std::memory_order_relaxed) == 0) continue;
    if (!screen_picture_done(g.screenSlots[s].img)) continue;
    if (newest < 0 || newer(s, static_cast<uint32_t>(newest))) newest = static_cast<int>(s);
  }
  for (uint32_t s = 0; s < kScreenSlots; ++s) {
    if (static_cast<int>(s) == newest || state[s] != 2) continue;
    if (g_screenCopied[s].load(std::memory_order_relaxed) != 0 &&
        (newest < 0 || newer(s, static_cast<uint32_t>(newest)))) {
      waiting = true;  // newer than any done: its copy is still on the GPU
      continue;
    }
    // An older picture, or none (its frame copied nothing): dropped, its fence not waited on (Dawn's next copy into the
    // image comes after this one on Dawn's own queue)
    EyeImage& img = g.screenSlots[s].img;
    if (img.releaseFd >= 0) close(img.releaseFd);
    img.releaseFd = -1;
    img.released = false;
    if (g_screenCopied[s].load(std::memory_order_relaxed) != 0) {
      ++g.screenDroppedSinceStats;
    } else {
      ++g.screenBlankSinceStats;
    }
    g_screenState[s].store(0, std::memory_order_release);
  }
  if (waiting) ++g.screenNotDoneSinceStats;
  if (newest < 0) return;

  const uint64_t start = now_ns();
  const uint32_t s = static_cast<uint32_t>(newest);
  ScreenSlot& slot = g.screenSlots[s];
  EyeImage& img = slot.img;
  img.released = false;
  const uint32_t pictureFrame = g_screenFrame[s].load(std::memory_order_relaxed);
  if (!ensure_swapchain(g.screenW, g.screenH)) {
    if (img.releaseFd >= 0) close(img.releaseFd);
    img.releaseFd = -1;
    g_screenState[s].store(0, std::memory_order_release);
    return;
  }
  // (Dawn's fence, done already when it's a sync fd, still waited on by the blit: the formal hand-off, as the eyes')
  bool knownComplete = false;
  const VkSemaphore wait = import_release_fence(img, img.dawnDone, &knownComplete) ? img.dawnDone : VK_NULL_HANDLE;
  if (wait == VK_NULL_HANDLE && !knownComplete) {
    // No fence from Dawn for an image its submit wrote (should not happen): all of Dawn's work waited for on the CPU
    const uint64_t cpuWaitStart = now_ns();
    aurora::gfx::synchronize();
    bool workDone = false;
    const auto future = aurora::webgpu::g_queue.OnSubmittedWorkDone(
        wgpu::CallbackMode::WaitAnyOnly, [&workDone](wgpu::QueueWorkDoneStatus, wgpu::StringView) { workDone = true; });
    aurora::webgpu::g_instance.WaitAny(future, 5000000000ull);
    g.cpuWaitNs += now_ns() - cpuWaitStart;
    ++g.screenCpuWaitsSinceStats;
  }
  const VkPipelineStageFlags blitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  const VkPipelineStageFlags dropStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
  // No picture after all (the swapchain refused): Dawn's fence, if imported, is still waited on once (a semaphore
  // imported as temporary must be before it is imported again; the slot is reused once this submit's fence is done)
  auto drop = [&]() {
    VkSubmitInfo none{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    none.waitSemaphoreCount = wait != VK_NULL_HANDLE ? 1 : 0;
    none.pWaitSemaphores = &wait;
    none.pWaitDstStageMask = &dropStage;
    if (vk_ok(vkQueueSubmit(g.queue, 1, &none, slot.fence), "vkQueueSubmit(screen drop)")) slot.pending = true;
    g_screenState[s].store(0, std::memory_order_release);
  };
  uint32_t index = 0;
  XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
  if (!xr_ok(xrAcquireSwapchainImage(g.swapchain, &acquire, &index), "xrAcquireSwapchainImage(screen)")) {
    drop();
    return;
  }
  XrSwapchainImageWaitInfo waitInfo{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
  waitInfo.timeout = XR_INFINITE_DURATION;
  if (!xr_ok(xrWaitSwapchainImage(g.swapchain, &waitInfo), "xrWaitSwapchainImage(screen)")) {
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(g.swapchain, &release);
    drop();
    return;
  }
  const VkImage dst = g.swapchainImages[index].image;
  vkResetCommandBuffer(slot.cmd, 0);
  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(slot.cmd, &begin);
  VkImageMemoryBarrier barriers[2]{};
  barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barriers[0].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
  barriers[0].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  barriers[0].oldLayout = img.dawnEndLayout;  // keeps Dawn's copy
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
                       nullptr, 2, barriers);
  // Same size, same sRGB encoding: a byte copy (the eyes' hand-off's blit: vkCmdCopyImage into the runtime's image costs
  // milliseconds on Adreno)
  VkImageBlit blit{};
  blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.srcOffsets[1] = {static_cast<int32_t>(g.screenW), static_cast<int32_t>(g.screenH), 1};
  blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  blit.dstOffsets[1] = {static_cast<int32_t>(g.screenW), static_cast<int32_t>(g.screenH), 1};
  vkCmdBlitImage(slot.cmd, img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                 &blit, VK_FILTER_NEAREST);
  // OpenXR wants color swapchain images back in COLOR_ATTACHMENT_OPTIMAL on release.
  VkImageMemoryBarrier toColor = barriers[1];
  toColor.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  toColor.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  toColor.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  vkCmdPipelineBarrier(slot.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &toColor);
  vkEndCommandBuffer(slot.cmd);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.waitSemaphoreCount = wait != VK_NULL_HANDLE ? 1 : 0;
  submit.pWaitSemaphores = &wait;
  submit.pWaitDstStageMask = &blitStage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &slot.cmd;
  if (vk_ok(vkQueueSubmit(g.queue, 1, &submit, slot.fence), "vkQueueSubmit(screen)")) slot.pending = true;
  XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
  xr_ok(xrReleaseSwapchainImage(g.swapchain, &release), "xrReleaseSwapchainImage(screen)");
  g.swapchainHasImage = true;
  ++g.framesShown;
  ++g.screenShownSinceStats;
  // how far behind the newest game frame drawn (the open one when pipelined: 1 = the frame before it)
  const uint32_t current = aurora_xr_current_frame();
  const uint32_t newestDrawn = aurora_frame_is_open() ? current : current - 1;
  g.screenBehindSinceStats += static_cast<uint64_t>(std::max<int32_t>(0, static_cast<int32_t>(newestDrawn - pictureFrame)));
  {
    // a read-back picture held meanwhile (the newspaper photo's) is no newer: never uploaded over this one later
    std::lock_guard<std::mutex> lock(g_frameMutex);
    g_frameUploaded = g_frameSerial;
  }
  g_screenState[s].store(0, std::memory_order_release);
  g.uploadNsSinceStats += now_ns() - start;
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
      } else if (std::strncmp(line, "pipeline", 8) == 0) {
        // r129 "pipeline <0|1>": the worker finishes a 3D frame while the next one simulates (xr_quest.h)
        g.pipelineOn = std::atoi(line + 8) != 0;
      } else if (std::strncmp(line, "screendirect", 12) == 0) {
        // r131 "screendirect <0|1>": the big screen's picture GPU-direct (0: read back + uploaded, as before r131)
        g.screenDirectOn = std::atoi(line + 12) != 0;
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
        // r124 (r123's refresh_rate = 80 stayed at 72: the runtime ignored the ask made before the session ran):
        // asked again each time the app has focus, and what the display runs at checked a moment later
        if (g.requestRefreshRate != nullptr && g.wantRefresh != 72.0f) {
          xr_ok(g.requestRefreshRate(g.session, g.wantRefresh), "xrRequestDisplayRefreshRateFB(focused)");
          g.refreshCheckNs = now_ns() + 3000000000ull;
        }
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
  // r131: the pipeline line for the 2D big screen's frames too (Jumbotron · Living Room: no eye hand-off there)
  const uint32_t screenFrames = g.screenCopiesSinceStats + g.screenNoSlotSinceStats;
  if (g.handoffSame + g.handoffLate > 0 || screenFrames > 0) {
    // r129: the pipelined frame: how many game frames went out pipelined, the simulation before each send (the
    // FIFO worker's idle start), the boundary's drain + end + begin; the GPU's own frame time
    const double sends = g.sendsSinceStats ? static_cast<double>(g.sendsSinceStats) : 1.0;
    uint64_t gpuLast = 0, gpuMean = 0, gpuCount = 0;
    aurora_gpu_frame_time(&gpuLast, &gpuMean, nullptr, &gpuCount);
    // (Aurora keeps a running mean: this stretch's own from the totals' difference; submit to done, queue included)
    static uint64_t s_gpuTotal = 0, s_gpuCount = 0;
    const uint64_t total = gpuMean * gpuCount;
    const double gpuHere = gpuCount > s_gpuCount && total > s_gpuTotal
                               ? static_cast<double>(total - s_gpuTotal) / static_cast<double>(gpuCount - s_gpuCount) / 1e6
                               : 0.0;
    s_gpuTotal = total;
    s_gpuCount = gpuCount;
    XLOG("pipeline %s: %u of %u game frames pipelined (2D big screen: %u); per send (ms): simulation before it %.2f, boundary %.2f | GPU frame (submit to done) %.2f, last %.2f",
         g.pipelineOn ? "on" : "off", g.pipelinedSinceStats, g.sendsSinceStats, g.pipelined2DSinceStats,
         g.simNsSinceStats / 1e6 / sends, g.boundaryNsSinceStats / 1e6 / sends, gpuHere, gpuLast / 1e6);
  }
  if (screenFrames + g.screenShownSinceStats > 0) {
    // r131: the GPU-direct big screen: game frames whose picture was copied GPU-side, pictures blitted to the screen and
    // how far behind the newest game frame drawn (pipelined: 1 = the one before the open one), display frames whose
    // newest picture was still on the GPU (shown a display frame later), older ones dropped for a newer, game frames
    // with no free image, released with no picture, CPU fence waits (should stay 0)
    XLOG("screen GPU-direct: %u game frames copied, %u shown (%.2f frames behind the newest drawn), %u display frames its copy still on the GPU, %u dropped for a newer, %u without a free image, %u no picture, %u CPU fence waits",
         g.screenCopiesSinceStats, g.screenShownSinceStats,
         g.screenShownSinceStats ? static_cast<double>(g.screenBehindSinceStats) / g.screenShownSinceStats : 0.0,
         g.screenNotDoneSinceStats, g.screenDroppedSinceStats, g.screenNoSlotSinceStats, g.screenBlankSinceStats,
         g.screenCpuWaitsSinceStats);
  }
  g.pipelinedSinceStats = g.sendsSinceStats = g.pipelined2DSinceStats = 0;
  g.simNsSinceStats = g.boundaryNsSinceStats = 0;
  g.screenCopiesSinceStats = g.screenShownSinceStats = g.screenDroppedSinceStats = g.screenNotDoneSinceStats = 0;
  g.screenNoSlotSinceStats = g.screenBlankSinceStats = g.screenCpuWaitsSinceStats = 0;
  g.screenBehindSinceStats = 0;
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
  load_homespot();      // r120: where you left the home figure
  load_crowd_level();   // r42: your crowd volume
  load_view_height();   // r44g: your stadium height
  load_room_options();  // r104: shelf, badge board, achievement pop-ups on or off

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
  if (!g.active || !g.frameBegun || !g.viewsValid || !eye_mode() || g.stereoFailed || !aurora_frame_is_open()) {
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
  // r129: this frame's Aurora frame stays open past the loop (xr_quest.h): the 3D views, single pass. r130: and the
  // life-size scorer's frames in Jumbotron · Living Room (his r129 log: those frames 15-17 ms, the eyes' drain 3 ms;
  // the big screen shows the frame before for the show's length)
  g.pipelineThisFrame = g.pipelineOn && g.singlePass;
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
      // r129: pipelined, no wait here: the worker finishes these sends while the next frame simulates; the next
      // send's PortXrFrameBoundary drains, then closes this last layer's pool measurement
      if (g.pipelineThisFrame) return;
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

// r129 (Lance: "get the FPS STEADY at 72+ FPS across the board"; his r123 / r125 logs: Jumbotron · Stadium and
// Tabletop · Stadium 13.2-13.3 ms of tasks a game frame, 4.4-4.5 of it the game thread waiting on the FIFO worker
// (fifo-drain) at the eyes' end, while that worker sat idle for the ~3.9 ms of simulation before the send): the
// pipelined frame (xr_quest.h). A 3D frame's Aurora frame is left open at the loop's end; the worker finishes its
// draws while the next game frame simulates, and that frame's send (glPlat.cpp, before glxSwapPre's own drain)
// drains it, ends it and begins its own here. The GameCube did the same (hitz_Post only sets the draw-done token,
// hitz_Pre waits for it); the frame memory the queued draws read is double-buffered (glxMemory p_frame[2]).
extern "C" int PortXrFramePending(void) {
  // (main.cpp, once a loop right after PortXrBeginFrame: the loop's start, for the simulation time)
  g.loopStartNs = now_ns();
  g.pipelineThisFrame = false;
  g.sentThisFrame = false;  // r131
  return (g.active && g.framePending) ? 1 : 0;
}

static void close_pending_frame();

extern "C" int PortXrFrameLeaveOpen(void) {
  // r131: the big screen's picture of this game frame, GPU-direct (Jumbotron · Living Room, the cup suite); such a 2D
  // frame is left open like the 3D views' when it sent (the boundary ran: one that sent nothing ends its frame as
  // before), and, with the scorer's eye pass, only single pass (as theirs)
  arm_screen_slot();
  if (g.screenDirectThisFrame && g.pipelineOn && g.sentThisFrame && (g.singlePass || !g.stereoThisFrame)) {
    g.pipelineThisFrame = true;
  }
  const bool leave = g.active && g.pipelineThisFrame && aurora_frame_is_open();
  // r130 (review): an old frame still pending (this game frame sent nothing to end it): closed here, measured
  if (!leave && g.framePending) close_pending_frame();
  g.pipelineThisFrame = leave;  // (PortXrEndFrame: no hand-off wait for a frame that isn't submitted yet)
  g.framePending = leave;
  if (leave) ++g.pipelinedSinceStats;
  if (leave && g.screenDirectThisFrame && !g.stereoThisFrame) ++g.pipelined2DSinceStats;  // r131
  return leave ? 1 : 0;
}

static void close_pending_frame() {
  g.framePending = false;
  AuroraGXSync();            // its last draws, done by the worker meanwhile (usually already)
  close_pool_measurement();  // its last layer's pool use, now complete
  aurora_end_frame();        // to the render worker: encoded, submitted, its eyes handed off
}

extern "C" void PortXrFrameClosePending(void) {
  if (!g.framePending) return;
  const uint64_t t0 = now_ns();
  close_pending_frame();
  g.boundaryNsSinceStats += now_ns() - t0;
}

extern "C" void PortXrFrameBoundary(void) {
  if (!g.active) return;
  const uint64_t t0 = now_ns();
  if (g.loopStartNs != 0 && t0 > g.loopStartNs) g.simNsSinceStats += t0 - g.loopStartNs;
  ++g.sendsSinceStats;
  g.sentThisFrame = true;  // r131: a 2D frame that sent may be left open (PortXrFrameLeaveOpen)
  if (!g.framePending) return;
  // r130 (review): Aurora can't begin another (the surface went away mid-frame): this send goes into the open one,
  // which the loop's end closes as usual, rather than the rest of the game frame drawing with no frame open
  if (!aurora_frame_ready()) return;
  close_pending_frame();
  if (!aurora_begin_frame()) XERR("pipeline: the next frame could not begin (Aurora refused)");
  g.boundaryNsSinceStats += now_ns() - t0;
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
  g.celebStartNs = now_ns();  // r130: a goal's show: its slowness doesn't hold the view at 60
  g.celebEndNs = 0;
  // r129: your corner figure cheers your goals and is gutted at goals against you (update_home_figure)
  {
    const int human = xrhome::human_side();
    if (human == 0 || human == 1) {
      g_homeReactKind.store(team == human ? 5 : 6, std::memory_order_relaxed);
      g_homeReactNs.store(now_ns(), std::memory_order_release);
    }
  }
  // (r143: r113's newspaper photo is no longer taken here: the magazine draws its own covers, no game picture)
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
      // r117 (Lance: the home ground's banners "blurry as fuck"): 0.8 of the runtime's size (was 0.6) and 4x
      // multisampled below; strikers.ini xr_fx_scale (0.4 .. 1.0) / xr_fx_msaa (1 or 4) to take it back down.
      // r124 (Lance: "those banners are still blurry as shit"; his r123 log: eyes 1680x1760, this layer 1344x1408, so
      // the headset stretched everything in it 1.25x): the eyes' own size. xr_fx_scale now 0.4 .. 1.3.
      float fxScale = 1.0f;
      if (const char* e = std::getenv("STRIKERS_XR_FX_SCALE")) {
        const float v = static_cast<float>(std::atof(e));
        if (v >= 0.4f && v <= 1.3f) fxScale = v;
      }
      dev.width = (static_cast<uint32_t>(views[0].recommendedImageRectWidth * fxScale) / 8) * 8;
      dev.height = (static_cast<uint32_t>(views[0].recommendedImageRectHeight * fxScale) / 8) * 8;
      // r61: the cup suite's room at the runtime's own size: you walk up to it and read it.
      dev.roomWidth = views[0].recommendedImageRectWidth;
      dev.roomHeight = views[0].recommendedImageRectHeight;
    }
    dev.maxAnisotropy = g.maxAnisotropy;
    dev.roomSamples = g.roomSamples;
    dev.fxSamples = g.roomSamples >= 4 ? 4u : 1u;  // r117
    if (const char* e = std::getenv("STRIKERS_XR_FX_MSAA")) {
      if (std::atoi(e) <= 1) dev.fxSamples = 1;
    }
    XLOG("fx: effects layer %ux%u per eye, %ux multisampled", dev.width, dev.height, dev.fxSamples);
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
        // r81: a side's co-captain (include/port/lineup.h) is their nemesis only when it's the one by name; r130: any
        // sidekick slot playing a captain class
        const bool coCaptain = PortSlotOfClass(side, characterClass) >= 0;
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
    if (g.fxReady) xrfx::hat_trick(sc.lastPos.x, sc.spot.y, sc.lastPos.z, g.mrMode);  // r144: where it stands
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

// r104 (Lance: make the trophy shelf and the RetroAchievements board "optional, or togglable", "bundled up"):
// the shelf with its badge board (one switch) and which RetroAchievements pop-ups show, kept beside the
// stadium height.
std::string room_options_path() {
  const char* home = std::getenv("STRIKERS_HOME");
  return std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/xr_room_options.txt";
}

void load_room_options() {
  if (FILE* f = std::fopen(room_options_path().c_str(), "r")) {
    char key[32];
    int value = 0;
    while (std::fscanf(f, "%31s %d", key, &value) == 2) {
      if (std::strcmp(key, "shelf") == 0) g.showShelf = value != 0;
      else if (std::strcmp(key, "home") == 0) g.showHome = value != 0;
      else if (std::strcmp(key, "popups") == 0) g.raPopups = std::max(0, std::min(2, value));
      else if (std::strcmp(key, "weather") == 0) PortWeatherSetMode(value);  // r137 (else strikers.ini `weather`)
      else if (std::strcmp(key, "music") == 0) PortMenuMusicSetMode(value);  // r147 (else strikers.ini `menu_music`)
    }
    std::fclose(f);
  }
  xrra::set_popups(g.raPopups);
  XLOG("room options: home ground %s, trophy shelf + badge board %s, achievement pop-ups %s, weather %s, menu music %s", g.showHome ? "on" : "off", g.showShelf ? "on" : "off",
       g.raPopups == 2 ? "all" : (g.raPopups == 1 ? "unlocks only" : "off"), PortWeatherModeName(PortWeatherMode()),
       PortMenuMusicModeName(PortMenuMusicMode()));
}

void save_room_options() {
  if (FILE* f = std::fopen(room_options_path().c_str(), "w")) {
    // r147: + music (the launcher's Menu music button writes the same line: LobbyActivity / MenuMusic.java)
    std::fprintf(f, "shelf %d\npopups %d\nhome %d\nweather %d\nmusic %d\n", g.showShelf ? 1 : 0, g.raPopups, g.showHome ? 1 : 0,
                 PortWeatherMode(), PortMenuMusicMode());
    std::fclose(f);
  }
}

// r44g, the pause settings panel (r42's crowd volume, r44's voice switch, the stadium height; r104: the
// shelf + badge board, the achievement pop-ups): the right stick up/down picks a row, left/right
// changes it (held: every quarter second).
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
    const int row = std::max(0, std::min(7, g.settingsRow - dirY));  // up = the row above (r133: 6 rows; r137: 7; r147: 8)
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
  } else if (g.settingsRow == 3) {
    // r104: the trophy shelf and its badge board together, once per push: right on, left off
    if (!first || (step > 0) == g.showShelf) return;
    g.showShelf = step > 0;
    save_room_options();
    XLOG("trophy shelf + badge board %s", g.showShelf ? "on" : "off");
  } else if (g.settingsRow == 5) {
    // r133: the whole home ground (its board, the banners by the screen, your corner and figure, the trophy shelf)
    if (!first || (step > 0) == home_on()) return;
    g.showHome = step > 0;
    if (g.showHome) xrhome::clear_none();  // r134: on here undoes the Home ground panel's NONE
    save_room_options();
    XLOG("home ground %s", g.showHome ? "on" : "off");
  } else if (g.settingsRow == 4) {
    // r104: the achievement pop-ups, once per push: right = more (off, unlocks only, all), left = fewer
    const int mode = std::max(0, std::min(2, g.raPopups + step));
    if (!first || mode == g.raPopups) return;
    g.raPopups = mode;
    xrra::set_popups(mode);
    save_room_options();
    XLOG("achievement pop-ups %s", mode == 2 ? "all" : (mode == 1 ? "unlocks only" : "off"));
  } else if (g.settingsRow == 6) {
    // r137: the weather (xr_weather.h), once per push, round the six: RANDOM, CLEAR, RAIN, SNOW, NIGHT, OFF. A quick
    // match on now changes at once; a franchise match keeps its calendar (OFF clears it).
    if (!first) return;
    const int mode = (PortWeatherMode() + step + PORT_WX_MODES) % PORT_WX_MODES;
    PortWeatherSetMode(mode);
    save_room_options();
    XLOG("weather %s", PortWeatherModeName(mode));
  } else if (g.settingsRow == 7) {
    // r147: the menus' music (port/menu_music.h), once per push, round the four: LOFI, MY MUSIC, BOTH, OFF (the game's
    // own). Heard from the next menu on (this panel is a match's pause); the launcher's Menu music button is the same.
    if (!first) return;
    const int mode = (PortMenuMusicMode() + step + PORT_MENU_MUSIC_MODES) % PORT_MENU_MUSIC_MODES;
    PortMenuMusicSetMode(mode);
    save_room_options();
    XLOG("menu music %s", PortMenuMusicModeName(mode));
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

// r128: World::Render (src/Game/world.cpp) calls this each time the game draws its stadium.
extern "C" void PortXrWorldRendered(void) { g_worldDrawnNs.store(now_ns(), std::memory_order_relaxed); }

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
  if (phase == 3 || phase == 4) g.celebEndNs = now_ns() + 3000000000ull;  // r130: the show's over in 3 s
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
  // r106: full rate offline, view by view. Online stays on the 60 cadence (both headsets step the same
  // frames). Tried in every view; one whose loop runs over its display period by 6% for 2 s (after 5 s in
  // it) goes back to 60 until the view changes.
  {
    const int64_t period = g.frameState.predictedDisplayPeriod > 0 ? g.frameState.predictedDisplayPeriod : 13888889;
    if (g.refreshCheckNs != 0 && tickNow >= g.refreshCheckNs) {  // r124: what the display really runs at
      g.refreshCheckNs = 0;
      float now = 0.0f;
      if (g.getRefreshRate != nullptr) g.getRefreshRate(g.session, &now);
      XLOG("display: runs at %.0f Hz (asked %.0f), frame period %.2f ms", now, g.wantRefresh, static_cast<double>(period) / 1e6);
    }    const int viewKey = (g.tabletop ? 1 : 0) | (g.mrMode ? 2 : 0);
    static int s_lastViewKey = -1;
    if (viewKey != s_lastViewKey) {
      s_lastViewKey = viewKey;
      if (g.fullRateBlockedKey >= 0 && g.fullRateBlockedKey != viewKey) g.fullRateBlockedKey = -1;
      g.fullRateSlowSinceNs = 0;
      g.fullRateRetryNs = 0;  // r129
    }
    // r129: held at 60, it tries full rate again after a while (one slow moment no longer holds it all session)
    if (g.fullRateBlockedKey == viewKey && g.fullRateBlockedNs != 0 && tickNow - g.fullRateBlockedNs > g.fullRateRetryNs) {
      g.fullRateBlockedKey = -1;
      XLOG("game frames: trying every display frame again (after %.0f s at 60)", (tickNow - g.fullRateBlockedNs) / 1e9);
    }
    const bool locked = xrnet::lockstep() || PortNetOnline() != 0;
    const bool want = !locked && viewKey != g.fullRateBlockedKey;
    if (want != g.fullRate) {
      g.fullRate = want;
      g.fullRateSlowSinceNs = 0;
      XLOG("game frames: %s (%s)", want ? "every display frame" : "60 a second",
           locked ? "online" : (want ? "this view keeps up" : "this view couldn't keep up"));
    }
    // r130 (his r129 log: every goal's show dropped Jumbotron · Living Room to 60, the retries backing off to a minute,
    // so back-to-back goals left it at 60): during a goal's show (the goal to 3 s after play is live again, at most
    // 40 s) a slow loop doesn't count; the retry's backoff tops out at 30 s and starts over after a minute at full rate
    const bool celebrating = g.celebStartNs != 0 && tickNow - g.celebStartNs < 40000000000ull &&
                             (g.celebEndNs == 0 || tickNow < g.celebEndNs);
    if (g.fullRate) {
      if (g.fullRateOkSinceNs == 0) g.fullRateOkSinceNs = tickNow;
      if (tickNow - g.fullRateOkSinceNs > 60000000000ull) g.fullRateRetryNs = 0;
    } else {
      g.fullRateOkSinceNs = 0;
    }
    if (g.fullRate) {
      const bool slow = !celebrating && g.focused && tickNow >= g.focusBackNs && tickNow - g.modeChangedNs > 5000000000ull &&
                        g.loopIntervalEma > 1.06 * static_cast<double>(period);
      if (!slow) {
        g.fullRateSlowSinceNs = 0;
      } else if (g.fullRateSlowSinceNs == 0) {
        g.fullRateSlowSinceNs = tickNow;
      } else if (tickNow - g.fullRateSlowSinceNs > 2000000000ull) {
        g.fullRate = false;
        g.fullRateBlockedKey = viewKey;
        g.fullRateBlockedNs = tickNow;  // r129: retried after 10 s, then 20, 40, 60 s
        g.fullRateRetryNs = g.fullRateRetryNs == 0 ? 10000000000ull : std::min<uint64_t>(30000000000ull, g.fullRateRetryNs * 2);
        XLOG("game frames: 60 a second (this view couldn't keep up: %.1f ms a loop, the display needs %.1f)",
             g.loopIntervalEma / 1e6, static_cast<double>(period) / 1e6);
      }
    }
    g.frameScale = g.fullRate ? static_cast<float>(60.0 * static_cast<double>(period) / 1e9) : 1.0f;
    if (g.fullRate) g.tickAccumNs = kGameTickNs;  // due every display frame (the offline branch uses it up)
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

extern "C" float PortXrFrameScale(void) { return g.frameScale; }

extern "C" void PortXrEndFrame(int ranGameFrame) {
  // r46: RetroAchievements, after the game frame: this frame's memory against the set (xr_ra.h).
  // r106: still 60 times a second at full rate (the set's hit counts are 60 Hz frames).
  bool raFrame = false;
  if (ranGameFrame != 0) {
    g.raAccum += g.frameScale;
    if (g.raAccum >= 0.999f) {
      g.raAccum -= 1.0f;
      raFrame = true;
    }
  }
  xrra::frame(raFrame);
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
  // r130: missed display frames (the predicted display time jumped more than a period and a half) scale the
  // celebration pieces' budget down (xrfx::set_load_scale), 0.8x a half second with 2+ missed; back up 0.1 a half
  // second once it holds every frame
  if (frameOk && g.frameState.predictedDisplayPeriod > 0) {
    const int64_t period = g.frameState.predictedDisplayPeriod;
    if (g.lastPredictedDisplayTime != 0) {
      const int64_t d = static_cast<int64_t>(g.frameState.predictedDisplayTime - g.lastPredictedDisplayTime);
      if (d > period * 3 / 2) g.missedSinceScale += static_cast<uint32_t>(std::min<int64_t>(8, d / period - 1));
    }
    g.lastPredictedDisplayTime = g.frameState.predictedDisplayTime;
    const uint64_t t = g.lastWaitReturnNs;
    if (g.scaleCheckNs == 0) g.scaleCheckNs = t;
    if (t - g.scaleCheckNs > 500000000ull) {
      float k = g.fxReady ? xrfx::load_scale() : 1.0f;
      const float was = k;
      // (review: only while pieces are up - a view slow on its own doesn't keep the next goal's pieces cut)
      const bool busy = g.fxReady && xrfx::pieces_alive();
      if (g.missedSinceScale >= 2 && busy) {
        k *= 0.8f;
      } else if (g.missedSinceScale == 0 || !busy) {
        k += 0.1f;
      }
      if (g.fxReady) {
        xrfx::set_load_scale(k);
        if (std::fabs(xrfx::load_scale() - was) > 0.001f && (xrfx::load_scale() < 0.99f || was < 0.99f))
          XLOG("fx: load %.2f (%u display frames missed in 0.5 s)", xrfx::load_scale(), g.missedSinceScale);
      }
      g.missedSinceScale = 0;
      g.scaleCheckNs = t;
    }
  }
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
      // r131: a GPU-direct big-screen frame (arm_screen_slot) has no readback to upload and no wait: its picture is
      // blitted once the GPU has done its copy (finish_screen_slots below, a following display frame: waiting here for
      // its submit would not get it done on the GPU any sooner)
      if (!g.screenDirectThisFrame) {
        if (!stereo_mode() && !g.pipelineThisFrame) aurora::gfx::synchronize();  // r130: not when left open
        upload_latest_frame();
      }
      if (g.stereoThisFrame) {
        g.stereoThisFrame = false;
        justRecorded = static_cast<int>(g.slot);
        g.slot = (g.slot + 1) % kStereoSlots;
      }
    } else {
      // r131: not for a GPU-direct big-screen frame without eyes (left open or not: finish_screen_slots below); the eyes'
      // finish_stereo_frame still needs it
      if (!g.screenDirectThisFrame || g.stereoThisFrame) aurora::gfx::synchronize();
      if (!g.screenDirectThisFrame) upload_latest_frame();
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
    // r129: not for a pipelined frame: it's submitted during the next one (PortXrFrameBoundary)
    if (justRecorded >= 0 && frameOk && !g.pipelineThisFrame) {
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
  // r131: the newest GPU-direct big-screen picture the render worker has submitted, every display frame
  finish_screen_slots();
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
  XrCompositionLayerQuad npQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};      // r147: NOW PLAYING
  XrCompositionLayerColorScaleBiasKHR npFade{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};  // r147: its fade
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
  // r113: the newspaper (r115: open: the left page, the right page, and the one turning over the fold)
  XrCompositionLayerQuad paperQuad[3] = {{XR_TYPE_COMPOSITION_LAYER_QUAD}, {XR_TYPE_COMPOSITION_LAYER_QUAD},
                                         {XR_TYPE_COMPOSITION_LAYER_QUAD}};
  bool showPaper[3] = {false, false, false};
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

  // r144: the life-size scorer's real reach this frame, before it is placed (scorer_place, its banner)
  update_scorer_space();
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
    // r124: Spider-Man's web shots from his wrists (xr_guest: his web move's flicks), out into your room: 1.7 m, but
    // never ending nearer than 0.8 m to your head
    {
      float shots[8 * 6];
      const int ns = PortGuestTakeWebShots(shots, 8);
      if (ns > 0 && scene.scorerValid && g.fxReady) {
        const M4 m = scorer_model();
        const XrVector3f& head = g.headPose.position;
        for (int i = 0; i < ns; ++i) {
          const float* s = shots + 6 * i;
          XrVector3f from{}, dir{};
          float* fp = &from.x;
          float* dp = &dir.x;
          for (int c = 0; c < 3; ++c) {
            fp[c] = s[0] * m.m[0][c] + s[1] * m.m[1][c] + s[2] * m.m[2][c] + m.m[3][c];
            dp[c] = s[3] * m.m[0][c] + s[4] * m.m[1][c] + s[5] * m.m[2][c];
          }
          const float dl = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
          if (dl < 1e-6f) continue;
          dir = XrVector3f{dir.x / dl, dir.y / dl, dir.z / dl};
          float len = 1.7f;
          for (int k = 0; k < 12 && len > 0.5f; ++k) {
            const float ex = from.x + dir.x * len - head.x, ey = from.y + dir.y * len - head.y, ez = from.z + dir.z * len - head.z;
            if (ex * ex + ey * ey + ez * ez >= 0.8f * 0.8f) break;
            len -= 0.1f;
          }
          xrfx::web_shot(from, dir, len);
        }
      }
    }
    // r143: no cylinder any more (r44e's 0.25 m "pole"): xr_fx gets the scorer's posed body (port/xr_body.h) and puts it
    // where its layer is drawn with this matrix (game space -> LOCAL; M4's rows = the column-major columns)
    scene.scorerModelValid = scene.scorerValid;
    if (scene.scorerValid) {
      const M4 sm = scorer_model();
      std::memcpy(scene.scorerModel, sm.m, sizeof(scene.scorerModel));
    }
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
      scene.skinEtch = idx >= 0 && idx < xrfx::kEtchCells ? 100 + idx : 0;  // r121: 256; r135: 512
    }
    // r57: the trophy shelf (your room only).
    scene.shelfValid = g.showShelf && home_on() && g.mrMode && g.shelf.placed;  // r104 / r133: unless switched off
    scene.shelfPos = g.shelf.position;
    scene.shelfYaw = g.shelf.yaw;
    if (g.inSuite) {
      // r61: in the cup suite your trophies stand on a shelf to your right; r62: picked up and resized
      // there too (update_shelf, shelf_at).
      scene.suite = true;
      scene.shelfValid = g.showShelf && home_on();
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
    // r119: in VR an evolution happens in its own setting (evo_backdrop), you at its middle, facing the way the
    // evolution stands; MR keeps your room. Its pictures go when the evolution screen closes.
    // r132 (Lance: "whats up with the evolution vr screens??? i aint seen one yet"): in every view now, the Living Room
    // too (the room covers your own while it evolves), and picked again for each evolution (a second one in a row kept
    // the first one's: Scyther evolved in Charmeleon's fire)
    if (!g.inSuite && g.fxReady && g.headValid && g.frOpen && g.frMode == 3) {
      if (g.evoRoomOn && g.evoRoomKey != g.frEvo.fromKey) {
        ++g_suiteLoadGen;
        g.suiteLoaded.clear();
        xrfx::set_room(xrfx::Room{});
        g.evoRoomOn = false;
      }
      if (!g.evoRoomOn) {
        g.evoRoomKey = g.frEvo.fromKey;
        // its own kind's setting, else the first one this headset has (never the black)
        static const char* const kAny[] = {"sky", "fire", "water", "grass", "electric", "rock", "psychic", "ghost", "city"};
        const std::string want = evo_backdrop(g.frEvo.fromKey);
        g.evoRoomTheme.clear();
        for (int i = -1; i < 9 && g.evoRoomTheme.empty(); ++i) {
          const std::string th = i < 0 ? want : std::string(kAny[i]);
          if (FILE* f = std::fopen((suites_dir() + "evo_" + th + ".room").c_str(), "rb")) {
            std::fclose(f);
            g.evoRoomTheme = th;
          }
        }
        XLOG("evo: backdrop %s for %s (its own: %s)", g.evoRoomTheme.empty() ? "none on this headset" : g.evoRoomTheme.c_str(),
             g.frEvo.fromKey, want.c_str());
      }
      if (!g.evoRoomTheme.empty()) request_room_files("evo:" + g.evoRoomTheme, suites_dir() + "evo_" + g.evoRoomTheme);
      g.evoRoomOn = true;
      const EvoShow& es = g_evoShow;
      float fx = es.dirX, fz = es.dirZ;
      XrVector3f at = es.from;
      if (!es.placed) {
        floor_forward(g.headPose, &fx, &fz);
        at = g.headPose.position;
      }
      scene.roomShown = !g.evoRoomTheme.empty();
      scene.roomEvo = scene.roomShown;
      scene.roomOrigin = XrVector3f{at.x, g.floorY, at.z};
      scene.roomYaw = std::atan2(-fx, -fz);
      scene.suiteCaptain = -1;
      ensure_fx();
    } else if (g.evoRoomOn) {
      g.evoRoomOn = false;
      if (!g.inSuite) {
        ++g_suiteLoadGen;
        g.suiteLoaded.clear();
        if (g.fxReady) xrfx::set_room(xrfx::Room{});
      }
      XLOG("evo: backdrop off");
    }
    // r124 (Lance: "we need a home screen VR environment. that shit is just a black screen man"): in VR, out of a
    // match, your captain's suite round you (its window on the stadium, the flashes and rockets out there), placed
    // where you are when it comes up, its window the way the big screen is. (Not waiting on the effects renderer: it
    // only starts at the first goal otherwise; ensure_fx below starts it.)
    // r126 (Lance's r125 clip: "fix the damn celebration at the end, that shit is not supposed to be there"): "match
    // over" comes while the game still plays the match's highlights in the stadium, and the suite came up then: its
    // live things (the flashes in its stands, its rockets, its LED strings, drawn in the effects layer over
    // everything) went off over the stadium. Now only once the game has drawn no 3D world for 1.5 s (menus), and gone
    // the moment it draws one again.
    // r128: "drawn no 3D world" = the game's stadium (World::Render) not drawn for 1.5 s, not any picture sent.
    const bool worldQuiet = now_ns() - g_worldDrawnNs.load(std::memory_order_relaxed) > 1500000000ull;
    if (worldQuiet != g.worldQuietLogged) {
      g.worldQuietLogged = worldQuiet;
      XLOG("world: %s", worldQuiet ? "no stadium drawn for 1.5 s (menus)" : "the game draws its stadium");
    }
    if (!g.inSuite && !g.mrMode && !g.evoRoomOn && g.device != VK_NULL_HANDLE && g.headValid && !g.stereoFailed &&
        !xrhome::in_match() && worldQuiet) {
      if (!g.homeEnvOn) {
        float fx = 0.0f, fz = -1.0f;
        if (g.shownScreenValid) {
          fx = g.shownScreenPose.position.x - g.headPose.position.x;
          fz = g.shownScreenPose.position.z - g.headPose.position.z;
          const float d = std::sqrt(fx * fx + fz * fz);
          if (d > 0.2f) {
            fx /= d;
            fz /= d;
          } else {
            floor_forward(g.headPose, &fx, &fz);
          }
        } else {
          floor_forward(g.headPose, &fx, &fz);
        }
        g.homeEnvYaw = std::atan2(-fx, -fz);
        g.homeEnvOrigin = XrVector3f{g.headPose.position.x, g.floorY, g.headPose.position.z};
        g.homeEnvOn = true;
        XLOG("home screen: your suite round you (VR), facing %.0f deg", g.homeEnvYaw * 57.29578f);
      }
      const int captain = xrhome::view().captain;
      std::string guestName;
      const std::string series = guest_suite(captain, &guestName);
      request_suite_room(series.empty() ? std::string(suite_name(captain)) : series);
      scene.roomShown = true;
      scene.roomOrigin = g.homeEnvOrigin;
      scene.roomOrigin.y = g.floorY;
      scene.roomYaw = g.homeEnvYaw;
      scene.suiteCaptain = series.empty() ? captain : -1;
      ensure_fx();
    } else if (g.homeEnvOn) {
      g.homeEnvOn = false;
      if (!g.inSuite && !g.evoRoomOn) {
        ++g_suiteLoadGen;
        g.suiteLoaded.clear();
        if (g.fxReady) xrfx::set_room(xrfx::Room{});
      }
      XLOG("home screen: suite off");
    }
    // r137: the weather in the effects layer (xr_fx weather block): rain / snow over the Tabletop field and onto your
    // table (MR), round you in the life-size stadium with its floodlights at night (VR). Not the Jumbotron in your room
    // (its picture carries the weather), and only while the game draws its stadium.
    {
      const int wx = PortWeatherCurrent();
      if (wx != 0 && now_ns() - g_worldDrawnNs.load(std::memory_order_relaxed) < 1500000000ull && !g.inSuite &&
          !g.homeEnvOn && !g.evoRoomOn && !g.stereoFailed && (scene.tabletop || !g.mrMode)) {
        scene.weather = static_cast<uint8_t>(wx);
        scene.weatherAmt = PortWeatherFade();
        scene.snowCover = PortWeatherSnowCover();
        float xyz[8 * 3];
        const int n = PortWeatherLights(xyz, 8);
        for (int i = 0; i < n; ++i) scene.wxLights[i] = XrVector3f{xyz[3 * i], xyz[3 * i + 1], xyz[3 * i + 2]};
        scene.wxLightCount = n;
        ensure_fx();
      }
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
    // r134: an evolution's room covers everything while it's up: your room's camera picture (MR) and the game's own
    // picture (below) aren't laid over it
    const bool evoRoomUp = g.evoRoomOn && !g.evoRoomTheme.empty();
    if (g.mrMode && g.pt.available && g.pt.layerRunning && !evoRoomUp) {
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
    // r126: not while the home screen's suite is up (the game has drawn no world for a while: the last stadium
    // picture would stand frozen over the suite)
    // r134 (Lance's clip: "the VR shit for evolution is STILL black and you can only see the environment for .4 of a
    // second before it goes black"): nor while an evolution's room is up - the game's menu picture (black) was laid
    // over the room every frame; it only showed before the first one came
    // r137: the weather's grade on the 3D eye layers: the compositor's own colour scale / bias (free: no extra pass over
    // the eye images), only while the game draws its stadium (a match, its highlights; never the menus or the suites).
    // The life-size stadium the most (kind 2), the Tabletop field / the life-size scorer less (the players stay
    // readable), and over your room no bias (it would tint the room).
    XrCompositionLayerColorScaleBiasKHR wxStadium{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
    XrCompositionLayerColorScaleBiasKHR wxMain{XR_TYPE_COMPOSITION_LAYER_COLOR_SCALE_BIAS_KHR};
    {
      static int s_wxLogged = -1;
      int wxBits = 0;
      if (g_hasColorScaleBias && now_ns() - g_worldDrawnNs.load(std::memory_order_relaxed) < 1500000000ull) {
        float sc[4], bi[4];
        if (PortWeatherLayerGrade(2, sc, bi)) {
          wxStadium.colorScale = XrColor4f{sc[0], sc[1], sc[2], sc[3]};
          wxStadium.colorBias = XrColor4f{bi[0], bi[1], bi[2], bi[3]};
          stadiumLayer.next = &wxStadium;
          wxBits = PortWeatherCurrent();
        }
        // (not the Jumbotron in your room: its eye layer is only the life-size scorer standing in your own light)
        const int mainKind = g.mrMode ? 0 : ((splitMode || g.tabletop) ? 1 : 2);
        if (!(g.mrMode && !g.tabletop) && PortWeatherLayerGrade(mainKind, sc, bi)) {
          wxMain.colorScale = XrColor4f{sc[0], sc[1], sc[2], sc[3]};
          wxMain.colorBias = XrColor4f{bi[0], bi[1], bi[2], bi[3]};
          projLayer.next = &wxMain;
        }
      }
      if (wxBits != s_wxLogged) {
        s_wxLogged = wxBits;
        XLOG("weather: eye layers %s%s", wxBits == 0 ? "clear" : PortWeatherLabel(wxBits),
             g_hasColorScaleBias ? "" : " (no colour scale on this runtime: not graded)");
      }
    }
    if (splitMode && g.stadiumValid[0] && g.stadiumValid[1] && !g.homeEnvOn && !evoRoomUp) {
      stadiumLayer.layerFlags = 0;
      stadiumLayer.space = g.local;
      stadiumLayer.viewCount = 2;
      stadiumLayer.views = g.stadiumViews;
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&stadiumLayer);
    }
    if (stereo_mode() && g.hasProjection && !g.homeEnvOn && !evoRoomUp) {  // (r42: the scorer alone goes after the screen, below)
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
    // r104: unless switched off in the pause settings (one switch with the shelf).
    if (g.showShelf && home_on() && g.mrMode && g.shelf.placed) {
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
      if (home_on()) update_home(scene, layers, &layerCount, homeQuads);  // r133 / r134: unless switched off
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
        // r144: over its real body as you see it, never across its face (scorer_banner_place)
        XrVector3f at{};
        float width = 0.0f;
        scorer_banner_place(pop, &at, &width);
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
      // r130: the sidekicks on the sidekick-slot menu (role 2), one slot at a time
      const int role = co ? 0 : (g.gpRole == 1 ? 2 : g.gpRole), side = g.gpSide, cursor = g.gpCursor;
      const int skSlot = g.gpSkSlot, skFirst = g.gpSkFirst, skCaptain = PortGuestMenuCaptainFor(g.gpSide);
      const unsigned skCapTaken = PortGuestSlotCaptainsTaken(g.gpSide);  // r130: the Strikers captains its slots can't have
      const std::array<int, 3> skPicks = {g.gpSkPick[0], g.gpSkPick[1], g.gpSkPick[2]};
      unsigned taken = co ? g.gpCoTaken : 0u;  // the Strikers captains the other side has (as the screen offers it now)
      if (!co)
        for (int t = 0; t < 8; ++t)
          if (!PortGuestPickerCaptainFree(t)) taken |= 1u << t;
      const bool onTabs = g.gpOnTabs;
      request_panel(g_guestPicker, g.gpGen, kGuestW, kGuestH, [co, role, side, cursor, taken, onTabs, skSlot, skFirst, skCaptain, skCapTaken, skPicks](uint8_t* px, int w, int h) {
        // r112: the menu by game (draw_select): big tiles, a spotlight, a few words
        const std::vector<MenuEntry>& menu = menu_entries(role);
        const int n = static_cast<int>(menu.size());
        auto takenAt = [&](int i) {
          if (role == 2) return i >= 0 && i < n && sk_taken(menu[i], skPicks.data(), std::max(0, skSlot), skFirst, skCaptain, skCapTaken);
          return i >= 0 && i < n && role == 0 && menu[i].team >= 0 && (taken & (1u << menu[i].team)) != 0;
        };
        auto lockedAt = [&](int i) { return i >= 0 && i < n && PortGuestRosterLocked(menu[i].pick); };  // r70c
        char title[96];
        if (role == 2)
          std::snprintf(title, sizeof(title), "TEAM %d - SIDEKICK %d OF 3", side + 1, std::max(1, std::min(3, skSlot + 1)));
        else
          std::snprintf(title, sizeof(title), "TEAM %d - %s", side + 1, co ? "CO-CAPTAIN" : (role == 0 ? "CAPTAIN" : "SIDEKICKS"));
        std::vector<xrtext::SelectHint> hints = {{"L", kHintStick, "MOVE"}, {"A", kHintA, "PICK"}};
        if (role == 0 || role == 2) hints.push_back({"LT RT", kHintStick, "FORMS"});
        if (co) hints.push_back({"X", kHintX, "NONE"});
        if (role == 2 && skSlot < 2) hints.push_back({"X", kHintX, "ALL THE SAME"});  // r130
        hints.push_back({"B", kHintB, role == 2 && skSlot > skFirst ? "BACK A SLOT" : "GAME'S SCREEN"});
        draw_select(px, w, h, role, cursor, onTabs, title, takenAt, lockedAt, hints);
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
      // r137: ... and when the MAG page's cover star's big picture is there (xr_big; at most every 1.5 s)
      if (g_hubBigWait.load() && xrbig::generation() != g_hubBigAt.load() && nowNs - g_magBigRedrawNs > 1500000000ull) {
        g_hubBigWait = false;
        g_magBigRedrawNs = nowNs;
        ++g_frFigGen;
      }
      // r131: drawn again too when a character's 3D figure comes to stand in front of it (its portrait left out)
      request_panel(g_franchise, g.frGen * 64 + (g_frFigGen.load() & 63), kGuestW, kGuestH, [](uint8_t* px, int w, int h) {
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
      // r113: THE DAILY STRIKE in front of you, 40 cm wide at arm's length, tilted to your eyes.
      // r131: STRIKERS ILLUSTRATED, placed 55 cm in front of you and 12 cm below your eyes, closed on its cover (31 cm
      // a page; where it is, its size and turn: grab it, update_magazine). Open, the pages either side of the spine; a
      // page turning over the spine with its face to you all the way (its front up to standing on the right, then its
      // back down onto the left; back the other way), the spine sliding as it opens / closes so what shows stays
      // in the middle (mag_frame). Every page goes up as soon as it's drawn, so a turn never waits for one.
      if (g.paperOpen) {
        // r137: big pictures (xr_big) ready since a page was drawn without one: the issue drawn again (at most every
        // 1.5 s; the paper keeps the pages it has till all six are drawn again)
        if (g_magBigWait.load() == g.paperGen && xrbig::generation() != g_magBigAt.load() &&
            nowNs - g_magBigRedrawNs > 1500000000ull) {
          g_magBigWait = -1;
          g_magBigRedrawNs = nowNs;
          g_magBigRedrawGen = ++g.paperGen;
        }
        for (int pg = 0; pg < kMagPages; ++pg) {
          request_panel(g_paperP[pg], g.paperGen, kMagW, kMagH, [pg, gen = g.paperGen](uint8_t* px, int w, int h) {
            const unsigned bigAt = xrbig::generation();  // r137
            FrSnapshot s;
            {
              std::lock_guard<std::mutex> lock(g_frMutex);
              s = g_frSnap;
            }
            Pieces P;
            mag_page(P, s, pg);  // r143: generated art and character renders only
            xrtext::board(px, w, h, P.v);
            if (P.bigMissing) {  // r137: drawn with a portrait in a big picture's place
              g_magBigAt = bigAt;
              g_magBigWait = gen;
            }
          });
          show_panel(g_paperP[pg], g.paperPanel[pg]);
        }
        if (!g.paperPlaced) {
          float fx = 0.0f, fz = -1.0f;
          floor_forward(g.headPose, &fx, &fz);
          const XrVector3f head = g.headPose.position;
          g.magPos = {head.x + fx * 0.55f, head.y - 0.12f, head.z + fz * 0.55f};
          g.magRelYaw = 0.0f;
          g.paperPlaced = true;
        }
        const MagFrame f = mag_frame();
        // r131: as bending paper in the effects layer once its pages are up (mag_paper_frame); the flat quads till then
        g.magMeshShown = mag_paper_frame(scene, f, nowNs);
        // the pages showing: settled, the back of the last leaf turned (left) and the front of the next (right);
        // turning leaf T, the pages under it and its front (still on the right half) or back (on the left half)
        const int T = g.magMeshShown ? -2 : g.magTurn, L = g.magMeshShown ? -1 : g.magLeaves;
        int left = -1, right = -1, turning = -1;
        if (T >= 0) {
          left = 2 * T - 1;
          right = 2 * T + 2 < kMagPages ? 2 * T + 2 : -1;
          turning = g.magSide <= 0.5f ? 2 * T : 2 * T + 1;
        } else {
          left = 2 * L - 1;
          right = L < kMagLeaves ? 2 * L : -1;
        }
        auto lay = [&](XrCompositionLayerQuad& quad, int page, const XrQuaternionf& o, float along) {
          // a page flat or turning: `o` its turn, its middle `along` (+ right, - left) half a page from the spine
          place_panel(quad, g.paperPanel[page], f.fold, f.pageW);
          const XrVector3f off = q_rot(o, XrVector3f{along * 0.5f * f.pageW, 0.0f, 0.0f});
          quad.pose.orientation = o;
          quad.pose.position = {f.fold.x + off.x, f.fold.y + off.y, f.fold.z + off.z};
        };
        if (left >= 0 && left < kMagPages && g.paperPanel[left].hasImage) {
          lay(paperQuad[0], left, f.q, -1.0f);
          showPaper[0] = true;
        }
        if (right >= 0 && right < kMagPages && g.paperPanel[right].hasImage) {
          lay(paperQuad[1], right, f.q, 1.0f);
          showPaper[1] = true;
        }
        if (turning >= 0 && turning < kMagPages && g.paperPanel[turning].hasImage) {
          const float a = 3.14159265f * g.magSide;
          if (g.magSide <= 0.5f) lay(paperQuad[2], turning, q_mul(f.q, yaw_quat(-a)), 1.0f);
          else lay(paperQuad[2], turning, q_mul(f.q, yaw_quat(3.14159265f - a)), -1.0f);
          showPaper[2] = true;
        }
      } else {
        g.paperPlaced = false;
        g.magMeshShown = false;  // r131: its paper put away
        if (g.fxReady) mag_paper_drop();
      }
    } else {
      g.frPlaced = false;
      g.paperPlaced = false;
      g.magMeshShown = false;  // r131
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
    // r151 (Lance: "i also want that card that comes up at the intro, to be skipped if i skip the intro, it stays on
    // the screen too long otherwise"): the intro card goes the moment the match is actually played (the intro skipped
    // or over), and doesn't come back for a later cut-scene
    if (g.gameplayLive && nowNs < g.frIntroUntilNs) g.frIntroUntilNs = 0;
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
        draw_franchise_series(px, w, h, c);  // r116
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
                               g.settingsRow, g.showShelf, g.raPopups, home_on(), PortWeatherMode(),  // r137: + WEATHER
                               PortMenuMusicMode(), PortMenuMusicMySongs());                          // r147: + MENU MUSIC
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
    if (g.fxReady && sceneViews) {
      // r112: the character select menu's one under the cursor, in 3D beside it (the match's menu, or a new
      // franchise career's picks)
      std::string selPack;
      const XrCompositionLayerQuad* selQuad = nullptr;
      if (showGuestQuad && g.gpOpen) {
        const int role = g.gpCo ? 0 : (g.gpRole == 1 ? 2 : g.gpRole);  // r130: the sidekick-slot menu
        const std::vector<MenuEntry>& menu = menu_entries(role);
        const bool locked = g.gpCursor >= 0 && g.gpCursor < static_cast<int>(menu.size()) &&
                            PortGuestRosterLocked(menu[g.gpCursor].pick);
        if (!g.gpOnTabs) selPack = select_pack(role, g.gpCursor, locked);
        selQuad = &guestQuad;
      } else if (showFrQuad && g.frOpen && g.frMode == 4) {
        const int role = g.frPickStage >= 2 ? 2 : 0;  // r130
        const std::vector<MenuEntry>& menu = menu_entries(role);
        const bool locked = role == 0 && g.frCursor >= 0 && g.frCursor < static_cast<int>(menu.size()) &&
                            menu[g.frCursor].team < 0 && PortGuestRosterEvolved(menu[g.frCursor].pick);
        if (!g.frOnTabs) selPack = select_pack(role, g.frCursor, locked);
        selQuad = &frQuad;
      }
      if (selQuad != nullptr) {
        update_select(scene, selPack, selQuad->pose.position, q_rot(selQuad->pose.orientation, XrVector3f{1.0f, 0.0f, 0.0f}),
                      0.5f * selQuad->size.width, nowNs);
      } else {
        update_select(scene, "", XrVector3f{0.0f, 0.0f, 0.0f}, XrVector3f{1.0f, 0.0f, 0.0f}, 0.0f, nowNs);
      }
    }
    // r137: STRIKERS TRADING CARDS: the packs earned taken every frame; a pack / its cards / a card out of the binder in
    // front of you while the hub is up on its pages (xr_cards.cpp), put away when it isn't; their buzz
    xrcards::set_evo_backdrop(evo_backdrop);  // r138: a shiny Pokemon card's window shows its evolution room
    xrcards::set_floor(g.floorY);             // r141: where a BLACK 1/1's life-size cameo stands
    const bool cardsShown = xrcards::frame(scene, g.headPose, g.headValid && g.frOpen && g.frMode == 2 && !g.paperOpen,
                                           g.fxReady && sceneViews, showFrQuad ? &frQuad : nullptr, nowNs);
    {
      float buzzS = 0.0f;
      const float buzz = xrcards::take_buzz(&buzzS);
      if (buzz > 0.0f) {
        g.rumble.amplitude = buzz;
        g.rumble.seconds = buzzS;
        g.rumble.startPending = true;
        g.rumble.stopPending = false;
      }
    }
    // r131: the franchise's characters in 3D: the hub's / trades' in front of its panel, awards night's in your room
    const bool frFigPanel = showFrQuad && g.frOpen && (g.frMode == 2 || g.frMode == 5) && !g.gpOpen;
    const bool frFigsUp = fr_figs_frame(scene, frFigPanel ? &frQuad : nullptr, g.fxReady && sceneViews, nowNs);
    // r96: while a Pokemon evolves in 3D in front of the big screen, the evolution card is under the
    // effects layer (its backdrop), not over it (it would hide the Pokemon standing before it); r131: so is the hub's
    // picture while its characters stand in front of it
    const bool frUnder = showFrQuad && ((g.frMode == 3 && g_evoShow.live) || frFigsUp ||
                                        (g.magMeshShown && g.paperOpen) ||  // r131: the magazine's paper in front of it
                                        cardsShown);                        // r137: the trading cards in front of it
    if (frUnder) {
      layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&frQuad);
      showFrQuad = false;
      // r134 (Lance's clip: "i kept seeing the lights behind the magazine, through the magazine"): under the effects
      // layer the hub's panel must be a solid there too, or the suite's LED lights behind it shine through it - the
      // figures' case added it (fr_figs_frame), the magazine's and the evolution card's didn't
      if (!frFigsUp && scene.panelCount < 4)
        scene.panels[scene.panelCount++] = xrfx::Scene::Panel{frQuad.pose, frQuad.size.width, frQuad.size.height};
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
    for (int k = 0; k < 3; ++k)  // r113: the paper over the hub (the page turning last)
      if (showPaper[k] && layerCount < 32) layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&paperQuad[k]);
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
    // r147: the menu music's NOW PLAYING card (port/menu_music.h) as a track starts: 4 s in the big screen's top right
    // corner (the game's menus and the franchise hub are on it; in front of the hub), else up and right of your view
    // following lazily; it pops in and fades out (the compositor's colour scale; without it, it shrinks away). Never in
    // a match. Drawn off this thread.
    {
      char npTitle[160], npArtist[160];
      if (PortMenuMusicTakeNowPlaying(npTitle, sizeof(npTitle), npArtist, sizeof(npArtist))) {
        const std::string t = npTitle, a = npArtist;
        request_panel(g_nowPlayingP, ++g.npGen, kNowPlayingW, kNowPlayingH, [t, a](uint8_t* px, int w, int h) {
          xrtext::now_playing_toast(px, w, h, t.c_str(), a.c_str());
        });
      }
      if (show_panel(g_nowPlayingP, g.nowPlaying) && g_nowPlayingP.shown != g.npShownGen) {
        g.npShownGen = g_nowPlayingP.shown;
        g.npShownNs = nowNs;
        g.npPlaced = false;
      }
      constexpr float kNpHoldS = 4.0f, kNpFadeS = 0.6f;
      const float npT = g.npShownNs != 0 ? static_cast<float>(nowNs - g.npShownNs) / 1e9f : 0.0f;
      if (g.npShownNs != 0 && (npT >= kNpHoldS + kNpFadeS || PortMenuMusicInMatch() != 0)) g.npShownNs = 0;
      if (g.npShownNs != 0 && g.nowPlaying.hasImage && g.headValid && layerCount < 32) {
        const float pop = std::min(1.0f, npT / 0.25f);
        const float fade = npT > kNpHoldS ? std::max(0.0f, 1.0f - (npT - kNpHoldS) / kNpFadeS) : 1.0f;
        const float aspect = static_cast<float>(kNowPlayingH) / static_cast<float>(kNowPlayingW);
        float width;
        if (scene.screenValid) {
          width = std::max(0.30f, std::min(0.55f, 0.24f * scene.screenWidth));
          place_panel(npQuad, g.nowPlaying, scene.screenPose.position, width);
          npQuad.pose.orientation = scene.screenPose.orientation;
          const XrVector3f off{0.5f * scene.screenWidth - 0.5f * width - 0.025f * scene.screenWidth,
                               0.5f * scene.screenHeight - 0.5f * width * aspect - 0.03f * scene.screenHeight, 0.05f};
          const XrVector3f out = q_rot(scene.screenPose.orientation, off);
          npQuad.pose.position = {scene.screenPose.position.x + out.x, scene.screenPose.position.y + out.y,
                                  scene.screenPose.position.z + out.z};
          g.npPlaced = false;
        } else {
          width = 0.36f;
          float fx = 0.0f, fz = -1.0f;
          floor_forward(g.headPose, &fx, &fz);
          const XrVector3f want{g.headPose.position.x + fx * 1.1f - fz * 0.34f, g.headPose.position.y + 0.17f,
                                g.headPose.position.z + fz * 1.1f + fx * 0.34f};
          if (!g.npPlaced) {
            g.npPos = want;
            g.npPlaced = true;
          } else {
            const float dt = scene.dt > 0.0f ? std::min(scene.dt, 0.1f) : 1.0f / 72.0f;
            const float k = std::min(1.0f, dt * 2.5f);
            g.npPos.x += (want.x - g.npPos.x) * k;
            g.npPos.y += (want.y - g.npPos.y) * k;
            g.npPos.z += (want.z - g.npPos.z) * k;
          }
          place_panel(npQuad, g.nowPlaying, g.npPos, width);
        }
        const float grow = (0.85f + 0.15f * pop) * (g_hasColorScaleBias ? 1.0f : fade);
        npQuad.size.width = width * grow;
        npQuad.size.height = width * aspect * grow;
        const float alpha = g_hasColorScaleBias ? pop * fade : 1.0f;
        if (g_hasColorScaleBias) {
          npFade.colorScale = XrColor4f{alpha, alpha, alpha, alpha};
          npFade.colorBias = XrColor4f{0.0f, 0.0f, 0.0f, 0.0f};
          npQuad.next = &npFade;
        }
        if (alpha > 0.01f && npQuad.size.width > 0.02f)
          layers[layerCount++] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&npQuad);
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
  // r131: the big screen's shared images, while the submit callback still ends their access (main.cpp has closed a
  // pipelined frame already: PortXrFrameClosePending)
  destroy_screen_slots();
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
extern "C" void PortXrWorldRendered(void) {}
extern "C" int PortXrStereoFullStands(void) { return 0; }
extern "C" int PortXrStereoFrameBegin(void) { return 0; }
extern "C" int PortXrFramePending(void) { return 0; }
extern "C" int PortXrFrameLeaveOpen(void) { return 0; }
extern "C" void PortXrFrameClosePending(void) {}
extern "C" void PortXrFrameBoundary(void) {}
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
extern "C" float PortXrFrameScale(void) { return 1.0f; }
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
