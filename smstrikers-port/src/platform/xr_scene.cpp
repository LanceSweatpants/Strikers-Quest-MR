// strikers-quest r197: your room scan through OpenXR (xr_scene.h). Quest only; elsewhere nothing (room() null).
#if defined(__ANDROID__) && defined(STRIKERS_XR)

#define VK_USE_PLATFORM_ANDROID_KHR 1
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include "xr_scene.h"

#include <android/log.h>
#include <time.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "xr_roompick.h"
#include "xr_roomquery.h"

namespace xrscene {
namespace {

// r225: every room-scan line goes to strikers-log.txt too (stderr, like xr_quest's XLOG): r223's were logcat only, and
// the headset's logcat had turned over - Lance's log couldn't say whether his scan was found or which room was used
void scene_log(int prio, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void scene_log(int prio, const char* fmt, ...) {
  char line[1024];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  __android_log_write(prio, "strikers_xr", line);
  std::fprintf(stderr, "[xr%s] %s\n", prio == ANDROID_LOG_WARN ? " warn" : "", line);
}
#define SLOG(...) scene_log(ANDROID_LOG_INFO, __VA_ARGS__)
#define SWARN(...) scene_log(ANDROID_LOG_WARN, __VA_ARGS__)

const char* const kWanted[] = {
    XR_FB_SPATIAL_ENTITY_EXTENSION_NAME,           XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME,
    XR_FB_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME,   XR_FB_SPATIAL_ENTITY_CONTAINER_EXTENSION_NAME,
    XR_FB_SCENE_EXTENSION_NAME,                    XR_FB_SCENE_CAPTURE_EXTENSION_NAME,
    XR_META_SPATIAL_ENTITY_MESH_EXTENSION_NAME,
};
constexpr int kWantedN = sizeof(kWanted) / sizeof(kWanted[0]);
bool g_ext[kWantedN] = {};
bool has_ext(const char* name) {
  for (int i = 0; i < kWantedN; ++i)
    if (std::strcmp(kWanted[i], name) == 0) return g_ext[i];
  return false;
}

// the labels we understand (XR_FB_scene v2+: the runtime keeps to these words, DESK -> TABLE, open sides told apart)
const char kRecognized[] =
    "TABLE,COUCH,FLOOR,CEILING,WALL_FACE,WINDOW_FRAME,DOOR_FRAME,STORAGE,BED,SCREEN,LAMP,PLANT,WALL_ART,GLOBAL_MESH,"
    "INVISIBLE_WALL_FACE,OTHER";

struct Fns {
  PFN_xrQuerySpacesFB querySpaces = nullptr;
  PFN_xrRetrieveSpaceQueryResultsFB retrieve = nullptr;
  PFN_xrEnumerateSpaceSupportedComponentsFB components = nullptr;
  PFN_xrGetSpaceComponentStatusFB getStatus = nullptr;
  PFN_xrSetSpaceComponentStatusFB setStatus = nullptr;
  PFN_xrGetSpaceContainerFB container = nullptr;
  PFN_xrGetSpaceRoomLayoutFB roomLayout = nullptr;
  PFN_xrGetSpaceBoundingBox2DFB box2d = nullptr;
  PFN_xrGetSpaceBoundingBox3DFB box3d = nullptr;
  PFN_xrGetSpaceBoundary2DFB boundary = nullptr;
  PFN_xrGetSpaceSemanticLabelsFB labels = nullptr;
  PFN_xrRequestSceneCaptureFB capture = nullptr;
  PFN_xrGetSpaceTriangleMeshMETA mesh = nullptr;
};

struct Entity {
  XrSpace space = XR_NULL_HANDLE;
  XrUuidEXT uuid{};
  unsigned rooms = 0;  // r227: the rooms listing it (bit r = room r; 0 none) - a shared piece is each one's
  bool locatable = false, meshOn = false;
  bool has2d = false, has3d = false, hasLabels = false, hasMesh = false;
  bool bitsRead = false;  // r225: its labels, read once
  uint16_t bits = 0;
};
struct RoomRec {
  XrSpace space = XR_NULL_HANDLE;
  XrUuidEXT uuid{};
  XrUuidEXT floor{};
  std::vector<XrUuidEXT> kids;
  int containerResult = 0, layoutResult = 0;  // r227: what the headset answered (the log)
  uint32_t walls = 0;
};
// r227: one request for pieces (xr_roomquery.h: one room's, at most 50 uuids)
struct KidReq {
  XrAsyncRequestIdFB id = 0;
  int room = 0;
  int count = 0;      // uuids asked for
  int loaded = 0;     // pieces it gave
  int result = 0;     // its answer (XR_SUCCESS / the error)
  bool done = false;
};

// a build's input, owning its memory (handed to the worker thread). r225: one per scanned room (all of them are built;
// xr_roompick.h picks the one you're in every frame) - r197-r224 built only the room picked once at read time.
struct RoomJob {
  std::vector<xrroom::InPlane> planes;
  std::vector<int> planeBoundary;  // (index into boundaries, -1 none: the pointers are set on the worker)
  std::vector<xrroom::InVolume> volumes;
  std::vector<xrroom::InMesh> meshes;
  std::vector<std::vector<float>> boundaries;
  std::vector<std::vector<float>> verts;
  std::vector<std::vector<uint32_t>> idx;
  int found = -1;  // its index in the rooms the headset listed
  XrUuidEXT uuid{}, floor{};
};
struct Job {
  std::vector<RoomJob> rooms;
  int listed = 0;
  float head[3] = {0.0f, 1.6f, 0.0f};
};
// a built room
struct Model {
  std::unique_ptr<xrroom::Room> room;
  int found = -1;
  XrUuidEXT uuid{}, floor{};
  float area = 0.0f, w = 0.0f, d = 0.0f;
};
struct Built {
  std::vector<Model> models;
  int listed = 0;
};

struct S {
  XrInstance instance = XR_NULL_HANDLE;
  XrSession session = XR_NULL_HANDLE;
  XrSpace local = XR_NULL_HANDLE;
  JavaVM* vm = nullptr;
  jobject activity = nullptr;
  Fns f;
  bool started = false;
  State state = kUnsupported;
  XrAsyncRequestIdFB reqRooms = 0, reqCapture = 0;
  std::vector<KidReq> kidReqs;     // r227: the piece requests (one per room / 50 uuids; r197-r226: one for everything)
  std::vector<XrUuidEXT> kidUuids;  // their uuids (each once)
  std::vector<RoomRec> rooms;
  std::vector<Entity> ents;
  int pendingEnable = 0;
  bool readNow = false;     // everything locatable: read + build at the next frame (it needs a display time)
  uint64_t permCheckNs = 0;
  uint64_t lookStartNs = 0, enableStartNs = 0;
  bool use = false;
  // the rooms built + their anchor (one floor entity: its pose when read, followed every frame - every room was read at
  // the same moment in the same LOCAL, so one anchor carries them all through a recenter)
  std::vector<Model> models;
  int listed = 0;  // rooms the headset listed at the last read
  xrroompick::Picker picker;
  XrSpace anchor = XR_NULL_HANDLE;
  XrPosef anchorAt{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  XrPosef pendingAnchorAt{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
  XrSpace pendingAnchor = XR_NULL_HANDLE;
  std::atomic<Built*> built{nullptr};
  std::atomic<bool> building{false};
  char summary[160] = {};
  XrTime lastTime = 0;
  XrVector3f head{0.0f, 1.6f, 0.0f};
  // r225: the real floor (STAGE), retries, why it failed
  float floorHint = 0.0f;
  bool hasFloorHint = false;
  uint64_t lastFrameNs = 0;
  uint64_t retryReadNs = 0;   // floors not located yet: read again then
  bool waitLogged = false;
  uint64_t nextLookNs = 0;    // no scan / failed: look again then (while USE MY ROOM SCAN is on in MR)
  int looks = 0;
  char failWhy[48] = {};
  State loggedState = kUnsupported;
  bool dropModels = false;  // the headset listed no rooms: the ones in use go (frame())
} g;

void set_failed(const char* why) {
  g.state = kFailed;
  std::snprintf(g.failWhy, sizeof(g.failWhy), "%s", why);
}
// r227: a request of OURS the headset turned down (r197-r226 called every such failure "THE HEADSET SAID NO" - Lance's
// r226 had the permission granted; it was our request): its code, so the log + SETTINGS > ROOM tell the two apart
void set_failed_code(int code) {
  char why[40];
  std::snprintf(why, sizeof(why), "STRIKERS ERROR %d", code);
  set_failed(why);
}

std::string uuid_hex(const XrUuidEXT& u) {
  char b[XR_UUID_SIZE_EXT * 2 + 1];
  for (int i = 0; i < XR_UUID_SIZE_EXT; ++i) std::snprintf(b + 2 * i, 3, "%02x", u.data[i]);
  return std::string(b);
}

uint64_t now_ns() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

template <typename T>
bool proc(const char* name, T* out) {
  *out = nullptr;
  return XR_SUCCEEDED(xrGetInstanceProcAddr(g.instance, name, reinterpret_cast<PFN_xrVoidFunction*>(out))) && *out != nullptr;
}

bool same_uuid(const XrUuidEXT& a, const XrUuidEXT& b) { return std::memcmp(a.data, b.data, XR_UUID_SIZE_EXT) == 0; }
bool zero_uuid(const XrUuidEXT& a) {
  for (int i = 0; i < XR_UUID_SIZE_EXT; ++i)
    if (a.data[i] != 0) return false;
  return true;
}

JNIEnv* jni() {
  if (g.vm == nullptr) return nullptr;
  JNIEnv* env = nullptr;
  if (g.vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) return env;
  if (g.vm->AttachCurrentThread(&env, nullptr) == JNI_OK) return env;
  return nullptr;
}

constexpr const char* kPermission = "com.oculus.permission.USE_SCENE";

bool permission_granted() {
  JNIEnv* env = jni();
  if (env == nullptr || g.activity == nullptr) return false;
  jclass cls = env->GetObjectClass(g.activity);
  jmethodID m = env->GetMethodID(cls, "checkSelfPermission", "(Ljava/lang/String;)I");
  bool ok = false;
  if (m != nullptr) {
    jstring p = env->NewStringUTF(kPermission);
    ok = env->CallIntMethod(g.activity, m, p) == 0;  // PackageManager.PERMISSION_GRANTED
    env->DeleteLocalRef(p);
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    ok = false;
  }
  env->DeleteLocalRef(cls);
  return ok;
}

void request_permission() {
  JNIEnv* env = jni();
  if (env == nullptr || g.activity == nullptr) return;
  jclass cls = env->GetObjectClass(g.activity);
  jmethodID m = env->GetMethodID(cls, "requestScenePermission", "()V");  // StrikersActivity (r197): on its UI thread
  if (m != nullptr) env->CallVoidMethod(g.activity, m);
  if (env->ExceptionCheck()) env->ExceptionClear();
  env->DeleteLocalRef(cls);
  SLOG("room scan: asked for the spatial data permission");
}

void drop_entities() {
  for (Entity& e : g.ents)
    if (e.space != XR_NULL_HANDLE && e.space != g.anchor && e.space != g.pendingAnchor) xrDestroySpace(e.space);
  g.ents.clear();
  for (RoomRec& r : g.rooms)
    if (r.space != XR_NULL_HANDLE) xrDestroySpace(r.space);
  g.rooms.clear();
  g.kidReqs.clear();
  g.kidUuids.clear();
  g.pendingEnable = 0;
  g.readNow = false;
  g.retryReadNs = 0;
  g.waitLogged = false;
}

void begin_query() {
  if (g.f.querySpaces == nullptr) return;
  drop_entities();
  XrSpaceStorageLocationFilterInfoFB storage{XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB};
  storage.location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB;
  XrSpaceComponentFilterInfoFB comp{XR_TYPE_SPACE_COMPONENT_FILTER_INFO_FB};
  comp.next = &storage;
  comp.componentType = XR_SPACE_COMPONENT_TYPE_ROOM_LAYOUT_FB;
  XrSpaceQueryInfoFB info{XR_TYPE_SPACE_QUERY_INFO_FB};
  info.queryAction = XR_SPACE_QUERY_ACTION_LOAD_FB;
  info.maxResultCount = 16;
  info.timeout = 0;
  info.filter = reinterpret_cast<const XrSpaceFilterInfoBaseHeaderFB*>(&comp);
  const XrResult r = g.f.querySpaces(g.session, reinterpret_cast<const XrSpaceQueryInfoBaseHeaderFB*>(&info), &g.reqRooms);
  if (XR_FAILED(r)) {
    SWARN("room scan: xrQuerySpacesFB(rooms) failed: %d", static_cast<int>(r));
    if (r == XR_ERROR_PERMISSION_INSUFFICIENT_KHR) g.state = kNoPermission;
    else set_failed_code(static_cast<int>(r));
    return;
  }
  g.state = kLooking;
  g.lookStartNs = now_ns();
  ++g.looks;
  SLOG("room scan: looking for your rooms (look %d; %zu room(s) in use meanwhile)", g.looks, g.models.size());
}

std::vector<XrSpaceQueryResultFB> results(XrAsyncRequestIdFB id) {
  std::vector<XrSpaceQueryResultFB> out;
  XrSpaceQueryResultsFB res{XR_TYPE_SPACE_QUERY_RESULTS_FB};
  res.resultCapacityInput = 0;
  if (XR_FAILED(g.f.retrieve(g.session, id, &res)) || res.resultCountOutput == 0) return out;  // the count first
  out.resize(res.resultCountOutput);
  res.resultCapacityInput = res.resultCountOutput;
  res.results = out.data();
  if (XR_FAILED(g.f.retrieve(g.session, id, &res))) out.clear();
  else out.resize(res.resultCountOutput);
  return out;
}

std::vector<XrSpaceComponentTypeFB> components_of(XrSpace s) {
  std::vector<XrSpaceComponentTypeFB> out;
  uint32_t n = 0;
  if (g.f.components == nullptr || XR_FAILED(g.f.components(s, 0, &n, nullptr)) || n == 0) return out;
  out.resize(n);
  if (XR_FAILED(g.f.components(s, n, &n, out.data()))) out.clear();
  else out.resize(n);
  return out;
}

bool component_on(XrSpace s, XrSpaceComponentTypeFB c, bool* pending) {
  XrSpaceComponentStatusFB st{XR_TYPE_SPACE_COMPONENT_STATUS_FB};
  if (XR_FAILED(g.f.getStatus(s, c, &st))) return false;
  if (pending) *pending = st.changePending == XR_TRUE;
  return st.enabled == XR_TRUE;
}

// asks for a component to be on (async); true when it already is
bool enable(XrSpace s, XrSpaceComponentTypeFB c) {
  bool pending = false;
  if (component_on(s, c, &pending)) return true;
  if (pending) {
    ++g.pendingEnable;  // someone else asked: its completion comes as our event too
    return false;
  }
  XrSpaceComponentStatusSetInfoFB set{XR_TYPE_SPACE_COMPONENT_STATUS_SET_INFO_FB};
  set.componentType = c;
  set.enabled = XR_TRUE;
  set.timeout = 0;
  XrAsyncRequestIdFB id = 0;
  const XrResult r = g.f.setStatus(s, &set, &id);
  if (XR_SUCCEEDED(r)) ++g.pendingEnable;
  else if (r == XR_ERROR_SPACE_COMPONENT_STATUS_ALREADY_SET_FB) return true;
  return false;
}

void on_rooms(XrAsyncRequestIdFB id) {
  for (const XrSpaceQueryResultFB& q : results(id)) {
    RoomRec rr;
    rr.space = q.space;
    rr.uuid = q.uuid;
    // its pieces (two calls: the count first)
    XrSpaceContainerFB c{XR_TYPE_SPACE_CONTAINER_FB};
    XrResult cr = XR_ERROR_FUNCTION_UNSUPPORTED;
    if (g.f.container != nullptr) cr = g.f.container(g.session, q.space, &c);
    if (XR_SUCCEEDED(cr) && c.uuidCountOutput > 0) {
      rr.kids.resize(c.uuidCountOutput);
      c.uuidCapacityInput = c.uuidCountOutput;
      c.uuids = rr.kids.data();
      cr = g.f.container(g.session, q.space, &c);
      if (XR_FAILED(cr)) rr.kids.clear();
      else rr.kids.resize(c.uuidCountOutput);
    }
    rr.containerResult = static_cast<int>(cr);
    // r227: its layout the way the runtime fills it (two calls: the wall count, then the walls with the floor + ceiling -
    // r197-r226 made only the count call and read the floor off it: "floor none given" for all 3 of Lance's rooms; the
    // FLOOR-labelled piece is still the fallback in read_and_build)
    XrRoomLayoutFB lay{XR_TYPE_ROOM_LAYOUT_FB};
    XrResult lr = XR_ERROR_FUNCTION_UNSUPPORTED;
    std::vector<XrUuidEXT> walls;
    if (g.f.roomLayout != nullptr) {
      lr = g.f.roomLayout(g.session, q.space, &lay);
      if (XR_SUCCEEDED(lr) && lay.wallUuidCountOutput > 0) {
        walls.resize(lay.wallUuidCountOutput);
        lay.wallUuidCapacityInput = lay.wallUuidCountOutput;
        lay.wallUuids = walls.data();
        lr = g.f.roomLayout(g.session, q.space, &lay);
      }
    }
    rr.layoutResult = static_cast<int>(lr);
    if (XR_SUCCEEDED(lr)) {
      rr.floor = lay.floorUuid;
      rr.walls = lay.wallUuidCountOutput;
      // its floor is one of its pieces (if the container left it out: asked for too)
      bool listed = zero_uuid(rr.floor);
      for (size_t k = 0; k < rr.kids.size() && !listed; ++k) listed = same_uuid(rr.kids[k], rr.floor);
      if (!listed) rr.kids.push_back(rr.floor);
    }
    g.rooms.push_back(std::move(rr));
  }
}

void finish_kids();
void enable_all();

// r227: one request for pieces (its uuids: g.kidUuids[first .. first + count)); one the headset refuses outright is halved
// and asked again, down to single pieces (so one bad uuid costs only itself)
void submit_kids(int room, int first, int count, int depth) {
  // r228 (Lance's r227 log: even a 1-uuid request came back -1, VALIDATION_FAILURE): a uuid LOAD query needs the storage
  // location chained to its filter, as the rooms' query has it (and Meta's XrSceneModel sample does) - r197-r227 left it off
  XrSpaceStorageLocationFilterInfoFB storage{XR_TYPE_SPACE_STORAGE_LOCATION_FILTER_INFO_FB};
  storage.location = XR_SPACE_STORAGE_LOCATION_LOCAL_FB;
  XrSpaceUuidFilterInfoFB filter{XR_TYPE_SPACE_UUID_FILTER_INFO_FB};
  filter.next = &storage;
  filter.uuidCount = static_cast<uint32_t>(count);
  filter.uuids = g.kidUuids.data() + first;
  XrSpaceQueryInfoFB info{XR_TYPE_SPACE_QUERY_INFO_FB};
  info.queryAction = XR_SPACE_QUERY_ACTION_LOAD_FB;
  info.maxResultCount = static_cast<uint32_t>(count);
  info.timeout = 0;
  info.filter = reinterpret_cast<const XrSpaceFilterInfoBaseHeaderFB*>(&filter);
  KidReq kr;
  kr.room = room;
  kr.count = count;
  const XrResult r = g.f.querySpaces(g.session, reinterpret_cast<const XrSpaceQueryInfoBaseHeaderFB*>(&info), &kr.id);
  if (XR_SUCCEEDED(r)) {
    g.kidReqs.push_back(kr);
    return;
  }
  SWARN("room scan: room %d: xrQuerySpacesFB(pieces) failed: %d (%d uuid(s) from #%d, maxResultCount %d)%s", room + 1,
        static_cast<int>(r), count, first, count, count > 1 && r != XR_ERROR_PERMISSION_INSUFFICIENT_KHR ? " - asking in halves" : "");
  if (count > 1 && r != XR_ERROR_PERMISSION_INSUFFICIENT_KHR && depth < 8) {
    submit_kids(room, first, count / 2, depth + 1);
    submit_kids(room, first + count / 2, count - count / 2, depth + 1);
    return;
  }
  kr.done = true;
  kr.result = static_cast<int>(r);
  g.kidReqs.push_back(kr);
}

void query_kids() {
  g.kidReqs.clear();
  g.kidUuids.clear();
  for (size_t ri = 0; ri < g.rooms.size(); ++ri) {
    const RoomRec& rr = g.rooms[ri];
    SLOG("room scan: room %zu of %zu listed: uuid %s, %zu piece(s) (container %d), floor %s (room layout %d, %u wall(s))", ri + 1,
         g.rooms.size(), uuid_hex(rr.uuid).c_str(), rr.kids.size(), rr.containerResult,
         zero_uuid(rr.floor) ? "none given" : uuid_hex(rr.floor).c_str(), rr.layoutResult, static_cast<unsigned>(rr.walls));
  }
  // r227: each uuid once, no zero uuids, one request per room (at most 50 uuids each) - xr_roomquery.h
  std::vector<const XrUuidEXT*> lists(g.rooms.size());
  std::vector<int> counts(g.rooms.size());
  size_t total = 0;
  for (size_t ri = 0; ri < g.rooms.size(); ++ri) {
    lists[ri] = g.rooms[ri].kids.data();
    counts[ri] = static_cast<int>(g.rooms[ri].kids.size());
    total += g.rooms[ri].kids.size();
  }
  g.kidUuids.resize(total);
  std::vector<xrroomquery::Batch> batches(total + g.rooms.size());
  const xrroomquery::Stats st =
      xrroomquery::plan(lists.data(), counts.data(), static_cast<int>(g.rooms.size()), g.kidUuids.data(), static_cast<int>(total),
                        batches.data(), static_cast<int>(batches.size()));
  g.kidUuids.resize(static_cast<size_t>(st.kept));
  SLOG("room scan: pieces: %d uuid(s) listed, %d asked for in %d request(s) (at most %d each) | %d zero, %d listed twice in one room, "
       "%d shared with an earlier room (asked for once)",
       st.listed, st.kept, st.batches, xrroomquery::kMaxUuidsPerQuery, st.zeros, st.dupInRoom, st.dupAcross);
  if (st.kept == 0) {
    if (g.rooms.empty()) {
      g.state = kNoScan;
      g.dropModels = true;
    } else {
      set_failed("NOTHING IN THE ROOM");
    }
    SLOG("room scan: %s", g.rooms.empty() ? "no room set up (Quest Settings > Physical space > Space setup)" : "a room with nothing in it");
    return;
  }
  for (int b = 0; b < st.batches; ++b) submit_kids(batches[static_cast<size_t>(b)].room, batches[static_cast<size_t>(b)].first,
                                                   batches[static_cast<size_t>(b)].count, 0);
  finish_kids();  // (every request refused outright: done now)
}

// r227: every piece request answered: the rooms that loaded are used (one room's failure doesn't sink the rest)
void finish_kids() {
  for (const KidReq& k : g.kidReqs)
    if (!k.done) return;
  std::vector<int> asked(g.rooms.size(), 0), loaded(g.rooms.size(), 0), bad(g.rooms.size(), 0), code(g.rooms.size(), 0);
  int firstBad = 0;
  bool perm = false;
  for (const KidReq& k : g.kidReqs) {
    if (k.room < 0 || k.room >= static_cast<int>(g.rooms.size())) continue;
    asked[static_cast<size_t>(k.room)] += k.count;
    loaded[static_cast<size_t>(k.room)] += k.loaded;
    if (k.result < 0) {
      ++bad[static_cast<size_t>(k.room)];
      code[static_cast<size_t>(k.room)] = k.result;
      if (firstBad == 0) firstBad = k.result;
      if (k.result == XR_ERROR_PERMISSION_INSUFFICIENT_KHR) perm = true;
    }
  }
  for (size_t ri = 0; ri < g.rooms.size(); ++ri)
    SLOG("room scan: room %zu of %zu: %d piece(s) asked for, %d loaded%s%s", ri + 1, g.rooms.size(), asked[ri], loaded[ri],
         bad[ri] ? ", a request failed: " : "", bad[ri] ? std::to_string(code[ri]).c_str() : "");
  SLOG("room scan: %zu piece(s) found (%zu request(s))", g.ents.size(), g.kidReqs.size());
  if (g.ents.empty()) {
    if (perm) {
      g.state = kNoPermission;  // (a real refusal: SETTINGS > ROOM > A asks again)
    } else if (firstBad != 0) {
      set_failed_code(firstBad);
    } else {
      set_failed("NOTHING IN THE ROOM");
    }
    SWARN("room scan: no piece of any room loaded%s", perm ? " (no permission)" : "");
    return;
  }
  enable_all();
}

void on_kids(KidReq& kr) {
  std::vector<const XrUuidEXT*> lists(g.rooms.size());
  std::vector<int> counts(g.rooms.size());
  for (size_t ri = 0; ri < g.rooms.size(); ++ri) {
    lists[ri] = g.rooms[ri].kids.data();
    counts[ri] = static_cast<int>(g.rooms[ri].kids.size());
  }
  for (const XrSpaceQueryResultFB& q : results(kr.id)) {
    bool have = false;  // (a piece already loaded - can't happen with each uuid asked once: skipped, its handle left be)
    for (const Entity& o : g.ents) have = have || same_uuid(o.uuid, q.uuid);
    if (have) continue;
    Entity e;
    e.space = q.space;
    e.uuid = q.uuid;
    e.rooms = xrroomquery::rooms_of(q.uuid, lists.data(), counts.data(), static_cast<int>(g.rooms.size()));
    ++kr.loaded;
    for (XrSpaceComponentTypeFB c : components_of(q.space)) {
      if (c == XR_SPACE_COMPONENT_TYPE_BOUNDED_2D_FB) e.has2d = true;
      else if (c == XR_SPACE_COMPONENT_TYPE_BOUNDED_3D_FB) e.has3d = true;
      else if (c == XR_SPACE_COMPONENT_TYPE_SEMANTIC_LABELS_FB) e.hasLabels = true;
      else if (c == XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META) e.hasMesh = true;
    }
    g.ents.push_back(e);
  }
}

void enable_all() {
  g.pendingEnable = 0;
  g.enableStartNs = now_ns();
  for (Entity& e : g.ents) {
    e.locatable = enable(e.space, XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB);
    if (e.hasMesh && has_ext(XR_META_SPATIAL_ENTITY_MESH_EXTENSION_NAME))
      e.meshOn = enable(e.space, XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META);
  }
  if (g.pendingEnable == 0) g.readNow = true;
}

std::string labels_of(XrSpace s) {
  XrSemanticLabelsSupportInfoFB support{XR_TYPE_SEMANTIC_LABELS_SUPPORT_INFO_FB};
  support.flags = XR_SEMANTIC_LABELS_SUPPORT_MULTIPLE_SEMANTIC_LABELS_BIT_FB |
                  XR_SEMANTIC_LABELS_SUPPORT_ACCEPT_DESK_TO_TABLE_MIGRATION_BIT_FB |
                  XR_SEMANTIC_LABELS_SUPPORT_ACCEPT_INVISIBLE_WALL_FACE_BIT_FB;
  support.recognizedLabels = kRecognized;
  XrSemanticLabelsFB l{XR_TYPE_SEMANTIC_LABELS_FB};
  l.next = &support;
  if (XR_FAILED(g.f.labels(g.session, s, &l)) || l.bufferCountOutput == 0) return std::string();  // the size first
  std::string out(l.bufferCountOutput, '\0');
  l.bufferCapacityInput = l.bufferCountOutput;
  l.buffer = out.data();
  if (XR_FAILED(g.f.labels(g.session, s, &l))) return std::string();
  out.resize(std::strlen(out.c_str()));
  return out;
}

void to_pose(const XrPosef& p, xrroom::Pose& o) {
  o.p[0] = p.position.x;
  o.p[1] = p.position.y;
  o.p[2] = p.position.z;
  o.q[0] = p.orientation.x;
  o.q[1] = p.orientation.y;
  o.q[2] = p.orientation.z;
  o.q[3] = p.orientation.w;
}

bool locate(XrSpace s, XrTime t, XrPosef* out) {
  XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
  if (XR_FAILED(xrLocateSpace(s, g.local, t, &loc))) return false;
  const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
  if ((loc.locationFlags & need) != need) return false;
  *out = loc.pose;
  return true;
}

// r225: every listed room built (the worker): each one's pointers set, built, logged
void build_worker(std::unique_ptr<Job> job) {
  auto* out = new Built();
  out->listed = job->listed;
  const uint64_t t0 = now_ns();
  for (RoomJob& rj : job->rooms) {
    for (size_t i = 0; i < rj.planes.size(); ++i)
      if (rj.planeBoundary[i] >= 0) rj.planes[i].boundary = rj.boundaries[static_cast<size_t>(rj.planeBoundary[i])].data();
    for (size_t i = 0; i < rj.meshes.size(); ++i) {
      rj.meshes[i].verts = rj.verts[i].data();
      rj.meshes[i].idx = rj.idx[i].data();
    }
    xrroom::Input in;
    in.planes = rj.planes.data();
    in.planeCount = static_cast<int>(rj.planes.size());
    in.volumes = rj.volumes.data();
    in.volumeCount = static_cast<int>(rj.volumes.size());
    in.meshes = rj.meshes.data();
    in.meshCount = static_cast<int>(rj.meshes.size());
    in.head[0] = job->head[0];
    in.head[1] = job->head[1];
    in.head[2] = job->head[2];
    Model m;
    m.room.reset(new xrroom::Room());
    m.found = rj.found;
    m.uuid = rj.uuid;
    m.floor = rj.floor;
    const uint64_t t1 = now_ns();
    xrroom::build(*m.room, in);
    if (!m.room->valid) {
      SWARN("room scan: room %d of %d couldn't be built (its floor has no outline and fewer than 3 walls) - skipped", rj.found + 1, job->listed);
      continue;
    }
    m.area = xrroompick::outline_area(*m.room, &m.w, &m.d);
    char sum[160];
    xrroom::summary(*m.room, sum, sizeof(sum));
    const xrroom::Room& r = *m.room;
    SLOG("room scan: room %d of %d built in %.1f ms: %s | floor outline %d vertices, %.1f m2 (%.1f x %.1f m), floor y %.2f | %d planes, "
         "%d boxes, mesh %d tris / %d up-facing, %d top cells, %d piles, cell %.2f m",
         rj.found + 1, job->listed, static_cast<double>(now_ns() - t1) / 1e6, sum, r.outlineN, static_cast<double>(m.area),
         static_cast<double>(m.w), static_cast<double>(m.d), static_cast<double>(r.floorY), in.planeCount, in.volumeCount, r.meshTris,
         r.meshTopTris, r.topCells, r.spotN, static_cast<double>(r.cell));
    out->models.push_back(std::move(m));
  }
  SLOG("room scan: %zu of %d listed room(s) built in %.1f ms", out->models.size(), job->listed, static_cast<double>(now_ns() - t0) / 1e6);
  Built* old = g.built.exchange(out);
  delete old;
  g.building = false;
}

// a plane's outline (its plane space; two calls: the count first)
std::vector<XrVector2f> outline_of(XrSpace s) {
  std::vector<XrVector2f> v;
  XrBoundary2DFB b{XR_TYPE_BOUNDARY_2D_FB};
  if (g.f.boundary == nullptr || XR_FAILED(g.f.boundary(g.session, s, &b)) || b.vertexCountOutput < 3) return v;
  v.resize(b.vertexCountOutput);
  b.vertexCapacityInput = b.vertexCountOutput;
  b.vertices = v.data();
  if (XR_FAILED(g.f.boundary(g.session, s, &b))) v.clear();
  else v.resize(b.vertexCountOutput);
  return v;
}

// everything is locatable: read it all at `t` (LOCAL) and build on a worker thread. r225: EVERY listed room (each with its
// own floor, walls and furniture); a room whose floor isn't located yet is waited for (the headset finding its place in
// your home after a start: up to 15 s, read again every 0.5 s - r197-r224 read once and, with no floor located, failed
// for good, silently: the effects stayed on your play area's rectangle)
void read_and_build(XrTime t) {
  g.readNow = false;
  if (g.building.load()) {  // one at a time: try again next frame
    g.readNow = true;
    return;
  }
  const uint64_t now = now_ns();
  for (Entity& e : g.ents)
    if (!e.bitsRead) {
      e.bits = e.hasLabels ? xrroom::parse_labels(labels_of(e.space).c_str()) : static_cast<uint16_t>(0);
      e.bitsRead = true;
    }
  const size_t nr = g.rooms.size();
  std::vector<int> floorEnt(nr, -1);
  std::vector<XrPosef> floorAt(nr);
  std::vector<char> located(nr, 0);
  int nFloor = 0, nLoc = 0;
  for (size_t ri = 0; ri < nr; ++ri) {
    for (size_t ei = 0; ei < g.ents.size(); ++ei) {
      const Entity& e = g.ents[ei];
      if ((e.rooms & (1u << (ri & 31))) == 0 && !(nr == 1 && e.rooms == 0)) continue;
      const bool layoutFloor = !zero_uuid(g.rooms[ri].floor) && same_uuid(e.uuid, g.rooms[ri].floor);
      if (!layoutFloor && (e.bits & xrroom::kLabFloor) == 0) continue;
      if (floorEnt[ri] < 0 || layoutFloor) floorEnt[ri] = static_cast<int>(ei);  // (the room layout's own floor first)
    }
    if (floorEnt[ri] < 0) continue;
    ++nFloor;
    const Entity& f = g.ents[static_cast<size_t>(floorEnt[ri])];
    if (f.locatable && locate(f.space, t, &floorAt[ri])) {
      located[ri] = 1;
      ++nLoc;
    }
  }
  if (nLoc < nFloor && g.enableStartNs != 0 && now - g.enableStartNs < 15000000000ull) {
    if (!g.waitLogged) {
      SLOG("room scan: %d of %d room floor(s) not located yet (the headset placing your scan in your home) - reading again every 0.5 s, "
           "up to 15 s", nFloor - nLoc, nFloor);
      g.waitLogged = true;
    }
    g.retryReadNs = now + 500000000ull;
    return;
  }
  auto job = std::make_unique<Job>();
  job->listed = static_cast<int>(nr);
  job->head[0] = g.head.x;
  job->head[1] = g.head.y;
  job->head[2] = g.head.z;
  for (size_t ri = 0; ri < nr; ++ri) {
    if (floorEnt[ri] < 0) {
      SWARN("room scan: room %zu of %zu (uuid %s): no floor among its pieces - skipped", ri + 1, nr, uuid_hex(g.rooms[ri].uuid).c_str());
      continue;
    }
    const Entity& fe = g.ents[static_cast<size_t>(floorEnt[ri])];
    if (!located[ri]) {
      SWARN("room scan: room %zu of %zu (uuid %s): its floor can't be located (an old / removed scan, or not in this home) - never used",
            ri + 1, nr, uuid_hex(g.rooms[ri].uuid).c_str());
      continue;
    }
    // its floor outline (the log; xr_roompick.h does the picking on the built rooms)
    const XrPosef& p = floorAt[ri];
    const std::vector<XrVector2f> ol = outline_of(fe.space);
    float area = 0.0f, x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (size_t i = 0, j = ol.empty() ? 0 : ol.size() - 1; i < ol.size(); j = i++) {
      area += ol[j].x * ol[i].y - ol[i].x * ol[j].y;
      x0 = std::min(x0, ol[i].x);
      x1 = std::max(x1, ol[i].x);
      y0 = std::min(y0, ol[i].y);
      y1 = std::max(y1, ol[i].y);
    }
    // head into the floor's plane space (its inverse pose), crossing test there
    const XrQuaternionf& q = p.orientation;
    const float hx = g.head.x - p.position.x, hy = g.head.y - p.position.y, hz = g.head.z - p.position.z;
    const float cx = -q.x, cy = -q.y, cz = -q.z, cw = q.w;
    const float tx = 2.0f * (cy * hz - cz * hy), ty = 2.0f * (cz * hx - cx * hz), tz = 2.0f * (cx * hy - cy * hx);
    const float lx = hx + cw * tx + (cy * tz - cz * ty), ly = hy + cw * ty + (cz * tx - cx * tz);
    bool in = false;
    for (size_t i = 0, j = ol.empty() ? 0 : ol.size() - 1; i < ol.size(); j = i++)
      if ((ol[i].y > ly) != (ol[j].y > ly) && lx < (ol[j].x - ol[i].x) * (ly - ol[i].y) / (ol[j].y - ol[i].y) + ol[i].x) in = !in;
    RoomJob rj;
    rj.found = static_cast<int>(ri);
    rj.uuid = g.rooms[ri].uuid;
    rj.floor = fe.uuid;
    int nPlanes = 0, nVols = 0, nMesh = 0, unlocated = 0;
    for (size_t ei = 0; ei < g.ents.size(); ++ei) {
      const Entity& e = g.ents[ei];
      const bool mine = (e.rooms & (1u << (ri & 31))) != 0 || (e.rooms == 0 && (nr == 1 || (e.bits & xrroom::kLabMesh) != 0));
      if (!mine) continue;
      if ((e.bits & xrroom::kLabFloor) != 0 && static_cast<int>(ei) != floorEnt[ri]) continue;  // (only this room's floor)
      XrPosef ep;
      if (!e.locatable || !locate(e.space, t, &ep)) {
        ++unlocated;
        continue;
      }
      const uint16_t bits = e.bits;
      if (e.has2d && (bits & xrroom::kLabMesh) == 0) {
        XrRect2Df rect{};
        if (XR_SUCCEEDED(g.f.box2d(g.session, e.space, &rect))) {
          xrroom::InPlane pl;
          to_pose(ep, pl.pose);
          pl.rect[0] = rect.offset.x;
          pl.rect[1] = rect.offset.y;
          pl.rect[2] = rect.extent.width;
          pl.rect[3] = rect.extent.height;
          pl.labels = bits;
          int bi = -1;
          const std::vector<XrVector2f> v = outline_of(e.space);
          if (!v.empty()) {
            std::vector<float> flat;
            const size_t n = std::min<size_t>(v.size(), static_cast<size_t>(xrroom::kMaxOutline));
            for (size_t i = 0; i < n; ++i) {
              flat.push_back(v[i].x);
              flat.push_back(v[i].y);
            }
            rj.boundaries.push_back(std::move(flat));
            bi = static_cast<int>(rj.boundaries.size()) - 1;
            pl.boundaryCount = static_cast<int>(n);
          }
          rj.planes.push_back(pl);
          rj.planeBoundary.push_back(bi);
          ++nPlanes;
        }
      }
      if (e.has3d) {
        XrRect3DfFB box{};
        if (XR_SUCCEEDED(g.f.box3d(g.session, e.space, &box))) {
          xrroom::InVolume vo;
          to_pose(ep, vo.pose);
          vo.box[0] = box.offset.x;
          vo.box[1] = box.offset.y;
          vo.box[2] = box.offset.z;
          vo.box[3] = box.extent.width;
          vo.box[4] = box.extent.height;
          vo.box[5] = box.extent.depth;
          vo.labels = bits;
          rj.volumes.push_back(vo);
          ++nVols;
        }
      }
      if (e.hasMesh && e.meshOn && g.f.mesh != nullptr) {
        XrSpaceTriangleMeshGetInfoMETA gi{XR_TYPE_SPACE_TRIANGLE_MESH_GET_INFO_META};
        XrSpaceTriangleMeshMETA m{XR_TYPE_SPACE_TRIANGLE_MESH_META};
        if (XR_SUCCEEDED(g.f.mesh(e.space, &gi, &m)) && m.vertexCountOutput > 0 && m.indexCountOutput >= 3) {  // the counts first
          std::vector<float> vv(static_cast<size_t>(m.vertexCountOutput) * 3);
          std::vector<uint32_t> ii(m.indexCountOutput);
          m.vertexCapacityInput = m.vertexCountOutput;
          m.vertices = reinterpret_cast<XrVector3f*>(vv.data());
          m.indexCapacityInput = m.indexCountOutput;
          m.indices = ii.data();
          if (XR_SUCCEEDED(g.f.mesh(e.space, &gi, &m))) {
            xrroom::InMesh me;
            to_pose(ep, me.pose);
            me.vertCount = static_cast<int>(m.vertexCountOutput);
            me.idxCount = static_cast<int>(m.indexCountOutput);
            rj.verts.push_back(std::move(vv));
            rj.idx.push_back(std::move(ii));
            rj.meshes.push_back(me);
            ++nMesh;
          }
        }
      }
    }
    char hint[48];
    if (g.hasFloorHint) std::snprintf(hint, sizeof(hint), "%.2f", static_cast<double>(g.floorHint));
    else std::snprintf(hint, sizeof(hint), "not known yet");
    SLOG("room scan: room %zu of %zu (uuid %s): floor y %.2f (your real floor %s), outline %zu vertices, %.1f m2 (%.1f x %.1f m), "
         "your head %s it | %d planes, %d boxes, %d meshes, %d piece(s) not located",
         ri + 1, nr, uuid_hex(g.rooms[ri].uuid).c_str(), static_cast<double>(p.position.y), hint, ol.size(),
         static_cast<double>(std::fabs(0.5f * area)), static_cast<double>(ol.empty() ? 0.0f : x1 - x0),
         static_cast<double>(ol.empty() ? 0.0f : y1 - y0), in ? "over" : "not over", nPlanes, nVols, nMesh, unlocated);
    if (job->rooms.empty()) {  // the anchor: the first room's floor (one anchor carries every room through a recenter)
      g.pendingAnchor = fe.space;
      g.pendingAnchorAt = p;
    }
    job->rooms.push_back(std::move(rj));
  }
  if (job->rooms.empty()) {
    set_failed(nFloor == 0 ? "NO FLOOR IN THE SCAN" : "ITS FLOOR ISN'T FOUND");
    SWARN("room scan: no room to use (%zu listed, %d with a floor, %d located) - effects use your play area", nr, nFloor, nLoc);
    return;
  }
  g.building = true;
  std::thread(build_worker, std::move(job)).detach();
}

// conj(a) * b etc. for the anchor's motion
XrQuaternionf qmul(const XrQuaternionf& a, const XrQuaternionf& b) {
  return XrQuaternionf{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                       a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
XrVector3f qrot(const XrQuaternionf& q, const XrVector3f& v) {
  const float tx = 2.0f * (q.y * v.z - q.z * v.y), ty = 2.0f * (q.z * v.x - q.x * v.z), tz = 2.0f * (q.x * v.y - q.y * v.x);
  return XrVector3f{v.x + q.w * tx + (q.y * tz - q.z * ty), v.y + q.w * ty + (q.z * tx - q.x * tz),
                    v.z + q.w * tz + (q.x * ty - q.y * tx)};
}

}  // namespace

const char* const* wanted_extensions(int* count) {
  *count = kWantedN;
  return kWanted;
}

void set_extension_enabled(const char* name) {
  for (int i = 0; i < kWantedN; ++i)
    if (std::strcmp(kWanted[i], name) == 0) g_ext[i] = true;
}

void start(XrInstance instance, XrSession session, XrSpace local, JavaVM* vm, jobject activity) {
  if (g.started) return;
  g.started = true;
  g.instance = instance;
  g.session = session;
  g.local = local;
  g.vm = vm;
  g.activity = activity;
  const bool core = has_ext(XR_FB_SPATIAL_ENTITY_EXTENSION_NAME) && has_ext(XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME) &&
                    has_ext(XR_FB_SPATIAL_ENTITY_CONTAINER_EXTENSION_NAME) && has_ext(XR_FB_SCENE_EXTENSION_NAME);
  bool ok = core && proc("xrQuerySpacesFB", &g.f.querySpaces) && proc("xrRetrieveSpaceQueryResultsFB", &g.f.retrieve) &&
            proc("xrEnumerateSpaceSupportedComponentsFB", &g.f.components) &&
            proc("xrGetSpaceComponentStatusFB", &g.f.getStatus) && proc("xrSetSpaceComponentStatusFB", &g.f.setStatus) &&
            proc("xrGetSpaceContainerFB", &g.f.container) && proc("xrGetSpaceBoundingBox2DFB", &g.f.box2d) &&
            proc("xrGetSpaceBoundingBox3DFB", &g.f.box3d) && proc("xrGetSpaceSemanticLabelsFB", &g.f.labels) &&
            proc("xrGetSpaceBoundary2DFB", &g.f.boundary) && proc("xrGetSpaceRoomLayoutFB", &g.f.roomLayout);
  if (has_ext(XR_FB_SCENE_CAPTURE_EXTENSION_NAME)) proc("xrRequestSceneCaptureFB", &g.f.capture);
  if (has_ext(XR_META_SPATIAL_ENTITY_MESH_EXTENSION_NAME)) proc("xrGetSpaceTriangleMeshMETA", &g.f.mesh);
  SLOG("room scan: extensions entity=%d query=%d storage=%d container=%d scene=%d capture=%d mesh=%d -> %s",
       has_ext(XR_FB_SPATIAL_ENTITY_EXTENSION_NAME), has_ext(XR_FB_SPATIAL_ENTITY_QUERY_EXTENSION_NAME),
       has_ext(XR_FB_SPATIAL_ENTITY_STORAGE_EXTENSION_NAME), has_ext(XR_FB_SPATIAL_ENTITY_CONTAINER_EXTENSION_NAME),
       has_ext(XR_FB_SCENE_EXTENSION_NAME), has_ext(XR_FB_SCENE_CAPTURE_EXTENSION_NAME),
       has_ext(XR_META_SPATIAL_ENTITY_MESH_EXTENSION_NAME), ok ? "usable" : "not usable");
  if (!ok) {
    g.state = kUnsupported;
    return;
  }
  if (permission_granted()) {
    begin_query();
  } else {
    g.state = kNoPermission;
    SLOG("room scan: no spatial data permission yet (the launcher / SETTINGS > ROOM asks)");
  }
}

void stop() {
  Built* b = g.built.exchange(nullptr);
  delete b;
  drop_entities();
  if (g.anchor != XR_NULL_HANDLE) xrDestroySpace(g.anchor);
  if (g.pendingAnchor != XR_NULL_HANDLE && g.pendingAnchor != g.anchor) xrDestroySpace(g.pendingAnchor);
  g.anchor = g.pendingAnchor = XR_NULL_HANDLE;
  g.models.clear();
  g.picker = xrroompick::Picker{};
  g.started = false;
}

bool on_event(const void* buf) {
  if (!g.started || g.state == kUnsupported) return false;
  const auto* base = static_cast<const XrEventDataBaseHeader*>(buf);
  switch (base->type) {
    case XR_TYPE_EVENT_DATA_SPACE_QUERY_RESULTS_AVAILABLE_FB: {
      const auto* e = static_cast<const XrEventDataSpaceQueryResultsAvailableFB*>(buf);
      if (e->requestId == g.reqRooms) {
        on_rooms(e->requestId);
        return true;
      }
      for (KidReq& k : g.kidReqs)
        if (k.id == e->requestId && !k.done) {
          on_kids(k);
          return true;
        }
      return false;
    }
    case XR_TYPE_EVENT_DATA_SPACE_QUERY_COMPLETE_FB: {
      const auto* e = static_cast<const XrEventDataSpaceQueryCompleteFB*>(buf);
      if (e->requestId == g.reqRooms) {
        if (XR_FAILED(e->result) && e->result != XR_ERROR_SPACE_COMPONENT_NOT_ENABLED_FB) {
          SWARN("room scan: room query ended with %d", static_cast<int>(e->result));
        }
        SLOG("room scan: %zu room(s) found", g.rooms.size());
        query_kids();
        return true;
      }
      for (KidReq& k : g.kidReqs)
        if (k.id == e->requestId && !k.done) {
          k.done = true;
          k.result = static_cast<int>(e->result);
          SLOG("room scan: room %d: a request for %d piece(s) ended with %d, %d loaded", k.room + 1, k.count, k.result, k.loaded);
          finish_kids();
          return true;
        }
      return false;
    }
    case XR_TYPE_EVENT_DATA_SPACE_SET_STATUS_COMPLETE_FB: {
      const auto* e = static_cast<const XrEventDataSpaceSetStatusCompleteFB*>(buf);
      bool ours = false;
      for (Entity& en : g.ents) {
        if (en.space != e->space) continue;
        ours = true;
        if (XR_SUCCEEDED(e->result) || e->result == XR_ERROR_SPACE_COMPONENT_STATUS_ALREADY_SET_FB) {
          if (e->componentType == XR_SPACE_COMPONENT_TYPE_LOCATABLE_FB) en.locatable = true;
          if (e->componentType == XR_SPACE_COMPONENT_TYPE_TRIANGLE_MESH_META) en.meshOn = true;
        }
      }
      if (!ours) return false;
      if (g.pendingEnable > 0 && --g.pendingEnable == 0) g.readNow = true;
      return true;
    }
    case XR_TYPE_EVENT_DATA_SCENE_CAPTURE_COMPLETE_FB: {
      const auto* e = static_cast<const XrEventDataSceneCaptureCompleteFB*>(buf);
      SLOG("room scan: Space setup finished (%d): looking again", static_cast<int>(e->result));
      begin_query();
      return true;
    }
    default:
      return false;
  }
}

void on_focus() {
  if (!g.started || g.state == kUnsupported) return;
  // back from the Quest menu / a resume (maybe from Space setup or a rescan while Strikers was away, maybe from allowing
  // the permission): look again - the rooms in use stay until a new read replaces them (r225: r197-r224 dropped them for
  // the look, the effects falling back to your play area meanwhile); a look stuck 10 s+ starts over
  if (g.state == kNoPermission) {
    if (permission_granted()) begin_query();
  } else if (!g.building.load() && (g.state != kLooking || now_ns() - g.lookStartNs > 10000000000ull)) {
    begin_query();
  }
}

void floor_hint(float y) {
  g.floorHint = y;
  g.hasFloorHint = true;
}

namespace {
const char* state_name(State s) {
  switch (s) {
    case kNoPermission: return "spatial data not allowed for Strikers (SETTINGS > ROOM > A: ALLOW)";
    case kLooking: return "looking";
    case kNoScan: return "no room scan on this headset";
    case kReady: return "ready";
    case kFailed: return "couldn't be read";
    default: return "no room scan support";
  }
}
// why no room is in use (the log): every built room against your head
void explain_none() {
  for (size_t i = 0; i < g.models.size(); ++i) {
    const xrroom::Room& r = *g.models[i].room;
    const float fy = xrroom::floor_y(r);
    const int w = xrroompick::where(r, g.head.x, g.head.z);
    char real[40];
    if (g.hasFloorHint) std::snprintf(real, sizeof(real), "your real floor %.2f", static_cast<double>(g.floorHint));
    else std::snprintf(real, sizeof(real), "your real floor not known");
    SLOG("room scan:   room %d of %d: %.1f m2, floor y %.2f (%s), your head %.2f m over it, %s", g.models[i].found + 1, g.listed,
         static_cast<double>(g.models[i].area), static_cast<double>(fy), real, static_cast<double>(g.head.y - fy),
         w == xrroompick::kIn ? "over its floor" : (w == xrroompick::kNear ? "within 1 m of its floor" : "away from its floor"));
  }
}
}  // namespace

void frame(XrTime t, bool use, const float head[3]) {
  g.use = use;
  g.lastTime = t;
  g.head = XrVector3f{head[0], head[1], head[2]};
  if (!g.started || g.state == kUnsupported) return;
  const uint64_t now = now_ns();
  const float dt = g.lastFrameNs != 0 ? std::min(0.1f, static_cast<float>(now - g.lastFrameNs) / 1e9f) : 0.0f;
  g.lastFrameNs = now;
  if (g.pendingEnable > 0 && g.enableStartNs != 0 && now - g.enableStartNs > 5000000000ull) {
    SWARN("room scan: %d piece(s) never answered: reading the rest", g.pendingEnable);
    g.pendingEnable = 0;
    g.readNow = true;
  }
  if (g.state == kNoPermission && now > g.permCheckNs) {  // allowed from the launcher / Quest settings meanwhile?
    g.permCheckNs = now + 2000000000ull;
    if (permission_granted()) begin_query();
  }
  // r225: a look with no full answer in 25 s fails (r197: only when no room answered at all - a room whose pieces never
  // came stayed "looking" for good)
  if (g.state == kLooking && g.lookStartNs != 0 && now - g.lookStartNs > 25000000000ull && !g.readNow && g.retryReadNs == 0 &&
      g.pendingEnable == 0 && !g.building.load()) {
    SWARN("room scan: no full answer in 25 s (%zu room(s), %zu piece(s))", g.rooms.size(), g.ents.size());
    set_failed("NO ANSWER FROM THE HEADSET");
  }
  if (g.retryReadNs != 0 && now >= g.retryReadNs) {
    g.retryReadNs = 0;
    g.readNow = true;
  }
  if (g.readNow) read_and_build(t);
  // r225: no scan / it failed and nothing in use: look again by itself while it's wanted (15 s, then every 60 s) - a
  // rescan, a late start of the headset's scene service, a permission given in Quest settings
  if (g.state == kNoScan || g.state == kFailed) {
    if (g.nextLookNs == 0) g.nextLookNs = now + (g.looks <= 4 ? 15000000000ull : 60000000000ull);
    if (use && g.models.empty() && !g.building.load() && now >= g.nextLookNs) {
      SLOG("room scan: looking again by itself (%s%s%s)", state_name(g.state), g.state == kFailed ? ": " : "", g.state == kFailed ? g.failWhy : "");
      begin_query();
    }
  }
  if (g.state != kNoScan && g.state != kFailed) g.nextLookNs = 0;
  if (g.dropModels) {  // the headset lists no rooms now: the scan was removed
    g.dropModels = false;
    if (!g.models.empty()) {
      SLOG("room scan: the headset lists no rooms now (the scan was removed) - effects use your play area");
      g.models.clear();
      g.picker = xrroompick::Picker{};
      if (g.anchor != XR_NULL_HANDLE) xrDestroySpace(g.anchor);
      g.anchor = XR_NULL_HANDLE;
    }
  }
  // new rooms: swap them in with their anchor (the same rooms as in use: kept as they are - nothing you see restarts)
  if (Built* b = g.built.exchange(nullptr)) {
    bool same = !g.models.empty() && b->models.size() == g.models.size();
    for (size_t i = 0; same && i < b->models.size(); ++i) {
      const Model& a = g.models[i];
      const Model& c = b->models[i];
      same = same_uuid(a.floor, c.floor) && a.room->outlineN == c.room->outlineN && a.room->boxN == c.room->boxN &&
             a.room->wallN == c.room->wallN && std::fabs(a.area - c.area) < 0.03f * std::max(1.0f, a.area);
    }
    if (b->models.empty()) {
      set_failed("NO ROOM COULD BE BUILT");
      SWARN("room scan: no room could be built%s", g.models.empty() ? " - effects use your play area" : " - the rooms in use stay");
    } else if (same) {
      SLOG("room scan: looked again: the same %zu room(s) - kept as they are", g.models.size());
      g.state = kReady;
    } else {
      g.models = std::move(b->models);
      g.listed = b->listed;
      g.picker = xrroompick::Picker{};
      if (g.anchor != XR_NULL_HANDLE && g.anchor != g.pendingAnchor) xrDestroySpace(g.anchor);
      g.anchor = g.pendingAnchor;
      g.anchorAt = g.pendingAnchorAt;
      // the anchor is ours now: keep it out of drop_entities' hands
      for (Entity& e : g.ents)
        if (e.space == g.anchor) e.space = XR_NULL_HANDLE;
      g.state = kReady;
      SLOG("room scan: %zu room(s) ready (%d listed) - the one you stand in is used", g.models.size(), g.listed);
    }
    g.pendingAnchor = XR_NULL_HANDLE;  // (not kept: drop_entities lets it go with the rest)
    delete b;
    drop_entities();  // (the anchor stays)
  }
  // follow a recenter: the anchor's pose now against when it was read (every room moves the same)
  if (!g.models.empty() && g.anchor != XR_NULL_HANDLE) {
    XrPosef p;
    if (locate(g.anchor, t, &p)) {
      const XrQuaternionf c0{-g.anchorAt.orientation.x, -g.anchorAt.orientation.y, -g.anchorAt.orientation.z, g.anchorAt.orientation.w};
      const XrQuaternionf d = qmul(p.orientation, c0);
      const XrVector3f x = qrot(d, XrVector3f{1.0f, 0.0f, 0.0f});
      const float hl = std::sqrt(x.x * x.x + x.z * x.z);
      if (hl > 1e-4f) {
        const XrVector3f m = qrot(d, g.anchorAt.position);
        for (Model& md : g.models)
          xrroom::set_frame(*md.room, x.x / hl, -x.z / hl, p.position.x - m.x, p.position.y - m.y, p.position.z - m.z);
      }
    }
  }
  // r225: the room you're in (xr_roompick.h) - walking from one scanned room into another hands the effects over
  if (!g.models.empty()) {
    constexpr int kMax = 16;
    xrroompick::Info info[kMax];
    const int n = std::min(kMax, static_cast<int>(g.models.size()));
    for (int i = 0; i < n; ++i) info[i] = xrroompick::Info{g.models[static_cast<size_t>(i)].room.get(), g.models[static_cast<size_t>(i)].area};
    if (xrroompick::step(g.picker, info, n, g.head.x, g.head.y, g.head.z, g.floorHint, g.hasFloorHint, dt)) {
      const int c = g.picker.cur;
      if (c >= 0) {
        const Model& m = g.models[static_cast<size_t>(c)];
        xrroom::summary(*m.room, g.summary, sizeof(g.summary));
        SLOG("room scan: using room %d of %d (uuid %s, %.1f m2, %.1f x %.1f m): %s - %s", m.found + 1, g.listed, uuid_hex(m.uuid).c_str(),
             static_cast<double>(m.area), static_cast<double>(m.w), static_cast<double>(m.d),
             g.picker.how == xrroompick::kIn ? "your head is over its floor" : "you're within 1 m of its floor", g.summary);
      } else {
        SLOG("room scan: you're in none of the %zu scanned room(s) (head %.2f %.2f %.2f) - effects use your play area (THIS ROOM ISN'T "
             "SCANNED)", g.models.size(), static_cast<double>(g.head.x), static_cast<double>(g.head.y), static_cast<double>(g.head.z));
        explain_none();
      }
    }
  }
  if (g.state != g.loggedState) {
    SLOG("room scan: state %s%s%s (use %s)", state_name(g.state), g.state == kFailed ? ": " : "", g.state == kFailed ? g.failWhy : "",
         use ? "on" : "off");
    g.loggedState = g.state;
  }
}

const xrroom::Room* room() {
  const int c = g.picker.cur;
  return g.use && c >= 0 && c < static_cast<int>(g.models.size()) ? g.models[static_cast<size_t>(c)].room.get() : nullptr;
}

int room_count() { return static_cast<int>(g.models.size()); }
int room_index() { return g.picker.cur; }

State state() {
  if (!g.models.empty()) return g.picker.cur >= 0 ? kReady : kNoScan;  // (none of them is where you are: A opens Space setup)
  return g.state;
}

void status(char* buf, size_t n) {
  if (n == 0) return;
  const size_t rooms = g.models.size();
  if (rooms > 0 && g.picker.cur >= 0) {
    if (rooms == 1) std::snprintf(buf, n, "ROOM SCAN: USING YOUR ROOM - %s", g.summary);
    else std::snprintf(buf, n, "ROOM SCAN: USING THE ROOM YOU'RE IN (%zu ROOMS FOUND) - %s", rooms, g.summary);
    return;
  }
  if (rooms > 0) {
    std::snprintf(buf, n, "THIS ROOM ISN'T SCANNED (%zu ROOM%s FOUND) - QUEST SETTINGS > PHYSICAL SPACE > SPACE SETUP", rooms,
                  rooms == 1 ? "" : "S");
    return;
  }
  switch (g.state) {
    case kReady: std::snprintf(buf, n, "ROOM SCAN FOUND: %s", g.summary); break;
    case kNoScan: std::snprintf(buf, n, "NO ROOM SCAN FOUND - QUEST SETTINGS > PHYSICAL SPACE > SPACE SETUP (A: OPEN IT)"); break;
    case kNoPermission: std::snprintf(buf, n, "STRIKERS ISN'T ALLOWED YOUR SPATIAL DATA - A: ALLOW, SO WEATHER LANDS ON YOUR REAL FLOOR"); break;
    case kLooking: std::snprintf(buf, n, "LOOKING FOR YOUR ROOM SCAN..."); break;
    case kFailed: std::snprintf(buf, n, "YOUR ROOM SCAN COULDN'T BE READ (%s) - A: TRY AGAIN", g.failWhy[0] ? g.failWhy : "NO ANSWER"); break;
    default: std::snprintf(buf, n, "THIS HEADSET HAS NO ROOM SCAN SUPPORT - EFFECTS USE YOUR FLOOR"); break;
  }
}

void act() {
  if (!g.started) return;
  switch (state()) {
    case kNoPermission:
      if (permission_granted()) begin_query();
      else request_permission();
      break;
    case kNoScan:
      if (g.f.capture != nullptr) {
        XrSceneCaptureRequestInfoFB req{XR_TYPE_SCENE_CAPTURE_REQUEST_INFO_FB};
        const XrResult r = g.f.capture(g.session, &req, &g.reqCapture);
        SLOG("room scan: opening Space setup: %d", static_cast<int>(r));
      } else {
        begin_query();
      }
      break;
    case kFailed:
    case kReady:
      begin_query();
      break;
    default:
      break;
  }
}

}  // namespace xrscene

#else  // not the Quest's OpenXR build: no room scan

#endif
