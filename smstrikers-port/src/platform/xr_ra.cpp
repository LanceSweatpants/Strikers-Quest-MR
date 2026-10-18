// strikers-quest r46: see xr_ra.h.
//
// Three threads touch this. The game thread owns rcheevos (rc_client): every rc_client call, every
// server answer handed back to it and the memory it checks happen there, once per headset loop
// (frame). A worker thread of its own first hashes the disc image (how RA knows which game this is),
// then makes the HTTPS calls rcheevos asks for, one at a time, through RaHttp.java (Android's own
// TLS), and queues the answers for the game thread. The pop-up pictures are drawn on short threads
// of their own (xr_text.cpp takes tens of ms), like the goal banner.
#include "xr_ra.h"

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#include <SDL3/SDL_system.h>
#include <android/log.h>
#include <jni.h>

#include <rc_client.h>
#include <rc_consoles.h>
#include <rc_hash.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

#include "../../include/port/disc.h"
#include "../../include/port/ra_memory.h"
#include "../../include/port/region.h"
#include "xr_text.h"

namespace xrra {
namespace {

// Into the game log too (stderr -> strikers-log.txt), like the network's lines.
void ra_file_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void ra_file_log(const char* fmt, ...) {
  char line[768];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  std::fprintf(stderr, "[xr] %s\n", line);
}
#define RLOG(...)                                                       \
  do {                                                                  \
    __android_log_print(ANDROID_LOG_INFO, "strikers_xr", __VA_ARGS__); \
    xrra::ra_file_log(__VA_ARGS__);                                     \
  } while (0)

// RA's server adds this to the set of a client it has not approved: a notice, not one of the set's
// own achievements (the panel hides it too).
constexpr uint32_t kWarningAchievementId = 101000001;

// ---------------------------------------------------------------------------------------------
// The worker: the disc's hash, then the HTTPS calls.
// ---------------------------------------------------------------------------------------------

struct Request {
  std::string url, post, contentType;
  rc_client_server_callback_t callback = nullptr;
  void* callbackData = nullptr;
};
struct Response {
  rc_client_server_callback_t callback = nullptr;
  void* callbackData = nullptr;
  std::string body;
  int status = 0;
};

struct Shared {
  std::mutex m;
  std::condition_variable cv;
  std::deque<Request> requests;  // to make (under m)
  std::deque<Response> answers;  // made, for the game thread (under m)
  std::string imagePath;         // the disc image to hash
  std::string userAgent;
  std::string hash;              // under m, once hashDone
  std::atomic<bool> hashDone{false};
  std::atomic<bool> stop{false};
};
Shared sh;

struct Jni {
  jclass cls = nullptr;
  jmethodID post = nullptr;
};
Jni g_jni;

bool jni_init() {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) return false;
  jclass local = env->FindClass("io/github/newcoke/strikers/RaHttp");
  if (local == nullptr) {
    env->ExceptionClear();
    return false;
  }
  g_jni.cls = static_cast<jclass>(env->NewGlobalRef(local));
  env->DeleteLocalRef(local);
  g_jni.post = env->GetStaticMethodID(
      g_jni.cls, "post", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;[I)[B");
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    g_jni.post = nullptr;
  }
  return g_jni.post != nullptr;
}

// The disc image through port_disc (raw, CISO, GCZ, RVZ...: whatever the game itself reads), so the
// hash is of the disc's own bytes whatever the file's format. A GameCube hash only seeks from the
// start.
struct DiscFile {
  PortDisc* disc = nullptr;
  int64_t pos = 0;
  bool nkit = false;
};

// r46d: an NKit-processed image (players' "ROM" downloads often are) carries NKit's own header at
// 0x200, inside what the hash reads, where the disc itself has zeros (the end of the game name
// field, 0x020-0x3FF). The disc's data before main.dol's end is otherwise as pressed, so with that
// field read as the disc had it the image hashes as the disc it came from.
constexpr uint64_t kNkitFrom = 0x200, kNkitTo = 0x400;

// What the hash found, for the pop-up and the panel (ra-status.txt). Worker thread, then read once
// hashDone is set.
std::string g_format;
bool g_nkit = false;

void* RC_CCONV disc_open(const char* path) {
  char err[1024];
  PortDisc* d = port_disc_open(path, err, sizeof err);
  if (d == nullptr) {
    RLOG("ra: the game file did not open for its hash: %s", err);
    return nullptr;
  }
  auto* f = new DiscFile;
  f->disc = d;
  char magic[4] = {};
  f->nkit = port_disc_read(d, magic, sizeof magic, kNkitFrom) == 4 && std::memcmp(magic, "NKIT", 4) == 0;
  g_format = port_disc_format(d);
  g_nkit = f->nkit;
  return f;
}
void RC_CCONV disc_seek(void* handle, int64_t offset, int origin) {
  auto* f = static_cast<DiscFile*>(handle);
  if (origin == SEEK_SET) f->pos = offset;
  else if (origin == SEEK_CUR) f->pos += offset;
}
int64_t RC_CCONV disc_tell(void* handle) { return static_cast<DiscFile*>(handle)->pos; }
size_t RC_CCONV disc_read(void* handle, void* buffer, size_t bytes) {
  auto* f = static_cast<DiscFile*>(handle);
  if (f->pos < 0) return 0;
  const long n = port_disc_read(f->disc, buffer, bytes, static_cast<unsigned long long>(f->pos));
  if (n <= 0) return 0;
  if (f->nkit) {
    const uint64_t from = static_cast<uint64_t>(f->pos), to = from + static_cast<uint64_t>(n);
    const uint64_t a = std::max(from, kNkitFrom), b = std::min(to, kNkitTo);
    if (a < b) std::memset(static_cast<uint8_t*>(buffer) + (a - from), 0, static_cast<size_t>(b - a));
  }
  f->pos += n;
  return static_cast<size_t>(n);
}
void RC_CCONV disc_close(void* handle) {
  auto* f = static_cast<DiscFile*>(handle);
  port_disc_close(f->disc);
  delete f;
}
void RC_CCONV hash_message(const char* message, const rc_hash_iterator_t*) { RLOG("ra: hash: %s", message); }

std::string hash_disc(const std::string& path) {
  rc_hash_iterator_t it;
  rc_hash_initialize_iterator(&it, path.c_str(), nullptr, 0);
  it.callbacks.filereader.open = disc_open;
  it.callbacks.filereader.seek = disc_seek;
  it.callbacks.filereader.tell = disc_tell;
  it.callbacks.filereader.read = disc_read;
  it.callbacks.filereader.close = disc_close;
  it.callbacks.error_message = hash_message;
  char hash[33] = {};
  const int ok = rc_hash_generate(hash, RC_CONSOLE_GAMECUBE, &it);
  rc_hash_destroy_iterator(&it);
  return ok ? std::string(hash) : std::string();
}

void worker() {
  {
    const auto t0 = std::chrono::steady_clock::now();
    std::string hash = hash_disc(sh.imagePath);
    const long ms = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    RLOG("ra: game file hash %s (%ld ms, %s%s, disc %s)", hash.empty() ? "failed" : hash.c_str(), ms,
         g_format.empty() ? "?" : g_format.c_str(), g_nkit ? ", NKit" : "", port_disc_game_code());
    std::lock_guard<std::mutex> lock(sh.m);
    sh.hash = std::move(hash);
    sh.hashDone = true;
  }
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) {
    RLOG("ra: the worker has no Java environment; nothing will reach RetroAchievements");
    return;
  }
  jstring jua = env->NewStringUTF(sh.userAgent.c_str());
  for (;;) {
    Request r;
    {
      std::unique_lock<std::mutex> lock(sh.m);
      sh.cv.wait(lock, [] { return sh.stop.load() || !sh.requests.empty(); });
      if (sh.stop.load()) break;
      r = std::move(sh.requests.front());
      sh.requests.pop_front();
    }
    Response a;
    a.callback = r.callback;
    a.callbackData = r.callbackData;
    a.status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;  // no answer: rcheevos tries again
    jstring jurl = env->NewStringUTF(r.url.c_str());
    jstring jpost = r.post.empty() ? nullptr : env->NewStringUTF(r.post.c_str());
    jstring jtype = r.contentType.empty() ? nullptr : env->NewStringUTF(r.contentType.c_str());
    jintArray jstatus = env->NewIntArray(1);
    jbyteArray body = nullptr;
    if (!env->ExceptionCheck() && jurl != nullptr && jstatus != nullptr) {
      body = static_cast<jbyteArray>(env->CallStaticObjectMethod(g_jni.cls, g_jni.post, jurl, jpost, jtype, jua,
                                                                 jstatus));
    }
    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
      body = nullptr;
    } else if (jstatus != nullptr) {
      jint status = 0;
      env->GetIntArrayRegion(jstatus, 0, 1, &status);
      if (status > 0) a.status = status;
      if (body != nullptr) {
        const jsize n = env->GetArrayLength(body);
        a.body.resize(static_cast<size_t>(n));
        if (n > 0) env->GetByteArrayRegion(body, 0, n, reinterpret_cast<jbyte*>(a.body.data()));
      }
    }
    if (body != nullptr) env->DeleteLocalRef(body);
    if (jstatus != nullptr) env->DeleteLocalRef(jstatus);
    if (jtype != nullptr) env->DeleteLocalRef(jtype);
    if (jpost != nullptr) env->DeleteLocalRef(jpost);
    if (jurl != nullptr) env->DeleteLocalRef(jurl);
    std::lock_guard<std::mutex> lock(sh.m);
    sh.answers.push_back(std::move(a));
  }
  if (jua != nullptr) env->DeleteLocalRef(jua);
}

// ---------------------------------------------------------------------------------------------
// The pop-ups
// ---------------------------------------------------------------------------------------------

struct Toast {
  std::string header, title, description;
  int points = 0;
};
std::mutex g_toastMutex;
std::deque<Toast> g_toasts;          // waiting (under g_toastMutex)
std::vector<uint8_t> g_toastPixels;  // the one drawn (under g_toastMutex)
std::atomic<bool> g_toastDrawing{false};
std::atomic<bool> g_toastReady{false};

void toast(const char* header, const char* title, const char* description, int points) {
  std::lock_guard<std::mutex> lock(g_toastMutex);
  if (g_toasts.size() >= 16) return;  // a flood (a reconnect's backlog): the rest only in the log
  g_toasts.push_back(Toast{header != nullptr ? header : "", title != nullptr ? title : "",
                           description != nullptr ? description : "", points});
}

// ---------------------------------------------------------------------------------------------
// rcheevos, on the game thread
// ---------------------------------------------------------------------------------------------

struct State {
  bool started = false;
  rc_client_t* client = nullptr;
  bool loggedIn = false;
  bool loadAsked = false;
  std::string hash;        // the game file's RA hash, once the worker has it
  bool tampered = false;   // a test setting changed the game this run (PortRaSetupTampered)
  uint32_t tamperCheck = 0;
  uint32_t dumpFrames = 0;
  std::thread worker;
  // r51: why there is no list to show (the pause screen's achievements, achievement_rows), "" = none.
  std::string offReason;
};
State s;

uint32_t RC_CCONV read_memory(uint32_t address, uint8_t* buffer, uint32_t bytes, rc_client_t*) {
  return PortRaReadMemory(address, buffer, bytes);
}

void RC_CCONV server_call(const rc_api_request_t* request, rc_client_server_callback_t callback, void* data,
                          rc_client_t*) {
  Request r;
  r.url = request->url != nullptr ? request->url : "";
  r.post = request->post_data != nullptr ? request->post_data : "";
  r.contentType = request->content_type != nullptr ? request->content_type : "";
  r.callback = callback;
  r.callbackData = data;
  {
    std::lock_guard<std::mutex> lock(sh.m);
    sh.requests.push_back(std::move(r));
  }
  sh.cv.notify_one();
}

void RC_CCONV log_message(const char* message, const rc_client_t*) { RLOG("ra: %s", message); }

void summary_counts(uint32_t* unlocked, uint32_t* total) {
  rc_client_user_game_summary_t sum{};
  rc_client_get_user_game_summary(s.client, &sum);
  *unlocked = sum.num_unlocked_achievements;
  *total = sum.num_core_achievements;
  // The server's notice is not one to earn.
  if (const rc_client_achievement_t* w = rc_client_get_achievement_info(s.client, kWarningAchievementId)) {
    if (w->category & RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE) {
      if (*total > 0) --*total;
      if (w->unlocked != 0 && *unlocked > 0) --*unlocked;
    }
  }
}

void RC_CCONV on_event(const rc_client_event_t* e, rc_client_t*) {
  switch (e->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
      const rc_client_achievement_t* a = e->achievement;
      if (a == nullptr || a->id == kWarningAchievementId) break;
      RLOG("ra: UNLOCKED %u \"%s\" (%u points)", a->id, a->title, a->points);
      toast("ACHIEVEMENT UNLOCKED", a->title, a->description, static_cast<int>(a->points));
      break;
    }
    case RC_CLIENT_EVENT_GAME_COMPLETED:
      RLOG("ra: every achievement in the set unlocked");
      toast("RETROACHIEVEMENTS", "SET COMPLETE!", "EVERY ACHIEVEMENT IN SUPER MARIO STRIKERS UNLOCKED", 0);
      break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
      if (e->server_error != nullptr) {
        RLOG("ra: the server refused %s: %s", e->server_error->api != nullptr ? e->server_error->api : "?",
             e->server_error->error_message != nullptr ? e->server_error->error_message : "?");
      }
      break;
    case RC_CLIENT_EVENT_DISCONNECTED:
      RLOG("ra: an unlock could not reach RetroAchievements yet; it will keep trying");
      break;
    case RC_CLIENT_EVENT_RECONNECTED:
      RLOG("ra: the waiting unlocks reached RetroAchievements");
      break;
    default:
      break;
  }
}

// The one copy RA lists for the set: Super Mario Strikers (USA), the Redump disc.
constexpr const char* kUsaDiscHash = "ea02ff3bdb24aa6ff6f13d5483e3f6c6";

// r46d: what this run found, for the launcher panel's RetroAchievements page (RetroAch.java reads
// ra-status.txt beside the log): on / unknown / unreadable, the file's format, NKit or not, the disc
// code and its RA hash.
void write_status(const char* state) {
  const char* home = std::getenv("STRIKERS_HOME");
  if (home == nullptr || *home == '\0') return;
  if (FILE* f = std::fopen((std::string(home) + "/ra-status.txt").c_str(), "w")) {
    std::fprintf(f, "state=%s\nformat=%s\nnkit=%d\ndisc=%s\nhash=%s\nwant=%s\n", state, g_format.c_str(),
                 g_nkit ? 1 : 0, port_disc_game_code(), s.hash.c_str(), kUsaDiscHash);
    std::fclose(f);
  }
}

// Why RA turned the file down, in the pop-up's words.
std::string unknown_reason() {
  const char* code = port_disc_game_code();
  char line[160];
  if (std::strcmp(code, "G4QE") != 0) {
    std::snprintf(line, sizeof(line), "RETROACHIEVEMENTS ONLY HAS THE USA DISC (G4QE); THIS IS %s, ANOTHER REGION",
                  code);
  } else {
    std::snprintf(line, sizeof(line), "THIS USA COPY ISN'T THE DUMP RETROACHIEVEMENTS KNOWS (ITS ID %.8s, NEEDS %.8s)",
                  s.hash.c_str(), kUsaDiscHash);
  }
  return line;
}

void RC_CCONV on_loaded(int result, const char* error, rc_client_t*, void*) {
  if (result != RC_OK) {
    RLOG("ra: the game did not load on RetroAchievements (%d): %s", result, error != nullptr ? error : "");
    if (result == RC_NO_GAME_LOADED) {
      write_status("unknown");
      toast("RETROACHIEVEMENTS", "OFF FOR THIS GAME FILE", unknown_reason().c_str(), 0);
      s.offReason = "OFF FOR THIS GAME FILE: " + unknown_reason();
    } else {
      write_status("error");
      toast("RETROACHIEVEMENTS", "OFF FOR NOW", error != nullptr ? error : "RETROACHIEVEMENTS DID NOT ANSWER", 0);
      s.offReason = "OFF FOR NOW: RETROACHIEVEMENTS DID NOT ANSWER";
    }
    return;
  }
  write_status("on");
  const rc_client_game_t* game = rc_client_get_game_info(s.client);
  uint32_t unlocked = 0, total = 0;
  summary_counts(&unlocked, &total);
  RLOG("ra: playing %s (game %u): %u of %u unlocked, casual", game != nullptr ? game->title : "?",
       game != nullptr ? game->id : 0, unlocked, total);
  char line[96];
  std::snprintf(line, sizeof(line), "%u OF %u ACHIEVEMENTS UNLOCKED  -  CASUAL", unlocked, total);
  toast("RETROACHIEVEMENTS", game != nullptr && game->title != nullptr ? game->title : "SUPER MARIO STRIKERS", line,
        0);
}

void RC_CCONV on_login(int result, const char* error, rc_client_t*, void*) {
  if (result != RC_OK) {
    RLOG("ra: login refused (%d): %s", result, error != nullptr ? error : "");
    toast("RETROACHIEVEMENTS", "LOG IN AGAIN",
          "IN THE LAUNCHER PANEL'S RETROACHIEVEMENTS PAGE (THE SAVED LOGIN STOPPED WORKING)", 0);
    return;
  }
  const rc_client_user_t* user = rc_client_get_user_info(s.client);
  RLOG("ra: logged in as %s", user != nullptr && user->display_name != nullptr ? user->display_name : "?");
  s.loggedIn = true;
}

// The panel's saved login (RetroAch.java): "user=" and "token=" lines, in the app's private folder.
bool read_login(std::string* user, std::string* token) {
  const char* path = std::getenv("STRIKERS_RA_LOGIN");
  if (path == nullptr || *path == '\0') return false;
  FILE* f = std::fopen(path, "r");
  if (f == nullptr) return false;
  char line[512];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    std::string l(line);
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
    if (l.rfind("user=", 0) == 0) *user = l.substr(5);
    else if (l.rfind("token=", 0) == 0) *token = l.substr(6);
  }
  std::fclose(f);
  return !user->empty() && !token->empty();
}

void start() {
  const char* off = std::getenv("STRIKERS_RA");
  if (off != nullptr && std::strcmp(off, "0") == 0) {
    RLOG("ra: off (STRIKERS_RA=0)");
    s.offReason = "RETROACHIEVEMENTS IS OFF ON THIS HEADSET";
    return;
  }
  std::string user, token;
  if (!read_login(&user, &token)) {
    RLOG("ra: not logged in (the launcher panel's RetroAchievements page); achievements off");
    s.offReason = "NOT LOGGED IN: LOG IN ON THE APP'S RETROACHIEVEMENTS PAGE, THEN START THE GAME AGAIN";
    return;
  }
  if (const char* why = PortRaSetupTampered()) {
    RLOG("ra: off for this run, a test setting changes the game: %s", why);
    s.offReason = "OFF FOR THIS RUN: A TEST SETTING CHANGES THE GAME";
    return;
  }
  const char* image = port_dvd_image_path();
  if (image == nullptr || *image == '\0') {
    RLOG("ra: the game is read from an extracted folder, which RetroAchievements cannot identify; off");
    write_status("folder");
    toast("RETROACHIEVEMENTS", "OFF FOR THIS GAME FILE",
          "THE GAME IS AN UNPACKED FOLDER; RETROACHIEVEMENTS NEEDS THE DISC IMAGE (.ISO, .RVZ OR .WIA)", 0);
    s.offReason = "OFF: THE GAME IS AN UNPACKED FOLDER; RETROACHIEVEMENTS NEEDS THE DISC IMAGE FILE";
    return;
  }
  if (!jni_init()) {
    RLOG("ra: RaHttp is missing; achievements off");
    s.offReason = "RETROACHIEVEMENTS COULD NOT START";
    return;
  }
  s.client = rc_client_create(read_memory, server_call);
  if (s.client == nullptr) return;
  rc_client_enable_logging(s.client, RC_CLIENT_LOG_LEVEL_WARN, log_message);
  rc_client_set_hardcore_enabled(s.client, 0);  // casual: hardcore is for clients RA has approved
  rc_client_set_unofficial_enabled(s.client, 0);
  rc_client_set_event_handler(s.client, on_event);
  {
    const char* ua = std::getenv("STRIKERS_RA_UA");
    char clause[64] = {};
    rc_client_get_user_agent_clause(s.client, clause, sizeof(clause));
    sh.userAgent = std::string(ua != nullptr && *ua != '\0' ? ua : "QuestStrikers/0") + " " + clause;
  }
  sh.imagePath = image;
  s.worker = std::thread(worker);
  RLOG("ra: starting as %s (%s)", user.c_str(), sh.userAgent.c_str());
  rc_client_begin_login_with_token(s.client, user.c_str(), token.c_str(), on_login, nullptr);
}

}  // namespace

void frame(bool ranGameFrame) {
  if (!s.started) {
    s.started = true;
    start();
  }
  if (s.client == nullptr) return;

  std::deque<Response> answers;
  {
    std::lock_guard<std::mutex> lock(sh.m);
    answers.swap(sh.answers);
  }
  for (Response& a : answers) {
    rc_api_server_response_t r{};
    r.body = a.body.data();
    r.body_length = a.body.size();
    r.http_status_code = a.status;
    a.callback(&r, a.callbackData);
  }

  if (!s.loadAsked && s.loggedIn && sh.hashDone.load()) {
    s.loadAsked = true;
    {
      std::lock_guard<std::mutex> lock(sh.m);
      s.hash = sh.hash;
    }
    if (s.hash.empty()) {
      write_status("unreadable");
      toast("RETROACHIEVEMENTS", "OFF FOR THIS GAME FILE", "THE GAME FILE COULD NOT BE READ FOR ITS ID", 0);
    } else {
      rc_client_begin_load_game(s.client, s.hash.c_str(), on_loaded, nullptr);
    }
  }

  // A test setting can also come in mid-run (a control-channel line): from then on nothing counts.
  if (!s.tampered && ranGameFrame && ++s.tamperCheck % 60 == 0) {
    if (const char* why = PortRaSetupTampered()) {
      s.tampered = true;
      RLOG("ra: stopped for this run, a test setting changed the game: %s", why);
      rc_client_unload_game(s.client);
    }
  }

  if (!s.tampered && ranGameFrame && rc_client_is_game_loaded(s.client)) {
    PortRaMemoryFrame();
    rc_client_do_frame(s.client);
    // What the set sees, every 10 s, beside the log (ra-memory.txt), to check against a play session.
    if (++s.dumpFrames % 600 == 0) {
      const char* home = std::getenv("STRIKERS_HOME");
      if (home != nullptr && *home != '\0') PortRaMemoryDump((std::string(home) + "/ra-memory.txt").c_str());
    }
  } else {
    rc_client_idle(s.client);  // the server's keep-alive and retries go on while no game frame runs
  }
}

bool take_toast(std::vector<uint8_t>& rgba) {
  if (g_toastReady.load()) {
    {
      std::lock_guard<std::mutex> lock(g_toastMutex);
      rgba.swap(g_toastPixels);
    }
    g_toastReady = false;
    g_toastDrawing = false;
    return true;
  }
  if (g_toastDrawing.load()) return false;
  Toast t;
  {
    std::lock_guard<std::mutex> lock(g_toastMutex);
    if (g_toasts.empty()) return false;
    t = std::move(g_toasts.front());
    g_toasts.pop_front();
  }
  g_toastDrawing = true;
  std::thread([t = std::move(t)] {
    std::vector<uint8_t> pixels(static_cast<size_t>(kToastW) * kToastH * 4);
    xrtext::achievement_toast(pixels.data(), kToastW, kToastH, t.header.c_str(), t.title.c_str(),
                              t.description.c_str(), t.points);
    {
      std::lock_guard<std::mutex> lock(g_toastMutex);
      g_toastPixels = std::move(pixels);
    }
    g_toastReady = true;
  }).detach();
  return false;
}

void achievement_rows(std::vector<AchievementRow>* rows, uint32_t* unlocked, uint32_t* total, std::string* why) {
  rows->clear();
  *unlocked = *total = 0;
  why->clear();
  if (s.client == nullptr) {
    *why = !s.offReason.empty() ? s.offReason : "RETROACHIEVEMENTS IS STARTING...";
    return;
  }
  if (s.tampered) {
    *why = "OFF FOR THIS RUN: A TEST SETTING CHANGED THE GAME";
    return;
  }
  if (!rc_client_is_game_loaded(s.client)) {
    *why = !s.offReason.empty() ? s.offReason : "STILL LOADING THE ACHIEVEMENTS FROM RETROACHIEVEMENTS...";
    return;
  }
  // Grouped unlocked / locked by rcheevos, in the set's own order within each group.
  rc_client_achievement_list_t* list = rc_client_create_achievement_list(
      s.client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
  if (list == nullptr) {
    *why = "THE ACHIEVEMENTS COULD NOT BE LISTED";
    return;
  }
  std::vector<AchievementRow> locked;
  for (uint32_t b = 0; b < list->num_buckets; ++b) {
    const rc_client_achievement_bucket_t& bucket = list->buckets[b];
    for (uint32_t i = 0; i < bucket.num_achievements; ++i) {
      const rc_client_achievement_t* a = bucket.achievements[i];
      if (a == nullptr || a->id == kWarningAchievementId) continue;
      AchievementRow r;
      r.title = a->title != nullptr ? a->title : "";
      r.description = a->description != nullptr ? a->description : "";
      r.points = static_cast<int>(a->points);
      r.unlocked = a->unlocked != 0;
      if (!r.unlocked) r.progress = a->measured_progress;
      (r.unlocked ? *rows : locked).push_back(std::move(r));
    }
  }
  rc_client_destroy_achievement_list(list);
  *unlocked = static_cast<uint32_t>(rows->size());
  rows->insert(rows->end(), std::make_move_iterator(locked.begin()), std::make_move_iterator(locked.end()));
  *total = static_cast<uint32_t>(rows->size());
}

void shutdown() {
  if (s.client == nullptr) return;
  sh.stop = true;
  sh.cv.notify_all();
  // Not joined: a call can be waiting on the network for seconds, and the process is ending.
  if (s.worker.joinable()) s.worker.detach();
}

}  // namespace xrra

#else

namespace xrra {
void frame(bool) {}
bool take_toast(std::vector<uint8_t>&) { return false; }
void achievement_rows(std::vector<AchievementRow>* rows, uint32_t* unlocked, uint32_t* total, std::string* why) {
  rows->clear();
  *unlocked = *total = 0;
  *why = "RETROACHIEVEMENTS IS NOT IN THIS BUILD";
}
void shutdown() {}
}  // namespace xrra

#endif
