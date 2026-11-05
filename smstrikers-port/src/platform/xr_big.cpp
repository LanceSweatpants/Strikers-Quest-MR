// See xr_big.h. Built with xr_quest.cpp in strikers_xr (C++20, no port prelude).
//
// r137: how a big picture is made, and why this way (measured on the PC: tools/guestconv/test/BigTest.java):
//  - Drawn by the CPU renderer this app already ships and runs on every headset for the guests' portraits (Java:
//    guests/render/Portrait.java, the public converter's "make guests"), called through JNI on a worker thread of
//    ours: guests/render/Big.java poses the character's own pack by the first frame of its idle (the same pack and
//    pose file the hub's 3D figures use, fr_fig_read's order below) and draws it at 1024 with the portraits' own look
//    (tools/outside/portrait_render.py's Cycles render: --full --turn -70, the key and rim area lights with soft
//    shadows, the sky's occlusion), so the big pictures sit beside the 256 portraits as the same art, only sharp.
//  - Not the effects layer's Vulkan model pipeline: its shading is the room's (no sky occlusion, no soft shadows), so a
//    cover star would not look like the portraits; it would also need an offscreen pass, an MSAA target, a read-back
//    and frame time on the render thread, in a file two other people edit this round.
//  - Not a C++ copy of Portrait: the PC has no C++ compiler to check one on; the Java is checked on the PC exactly as it
//    runs here (BigTest calls Big.render on the PC's copies of the same files).
// Cost (PC, the same two threads): 1.4 - 2.5 s for a 1024 picture (about 3x that on the headset's CPU), off every
// frame-critical thread (nice 10, its ray threads the lowest priority); under 40 MB of Java heap while one draws; a 1024
// PNG 0.3 - 0.7 MB in STRIKERS_HOME/cache/big (+ <id>.png.src: the files it was drawn from, so a remade pack is drawn
// again); up to 64 MB of pictures kept decoded here (the oldest let go first).

#include "xr_big.h"

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#include <jni.h>
#include <android/log.h>
#include <SDL3/SDL_system.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

// stb_image (public domain / MIT), PNG only, private to this file: the cached pictures read back
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "third_party/stb_image.h"

namespace xrbig {
namespace {

#define BLOG(...)                                                    \
  do {                                                               \
    __android_log_print(ANDROID_LOG_INFO, "strikers_xr", __VA_ARGS__); \
    std::fprintf(stderr, "[big] ");                                  \
    std::fprintf(stderr, __VA_ARGS__);                               \
    std::fprintf(stderr, "\n");                                      \
    std::fflush(stderr);                                             \
  } while (0)

constexpr int kMaxSize = 1024;
constexpr size_t kMemBudget = size_t(64) << 20;  // decoded pictures kept (16 at 1024)

enum State { kIdle, kQueued, kReady, kFailed };  // kIdle: not asked yet, or let go from memory (its PNG stays)
struct Pic {
  State state = kIdle;
  std::shared_ptr<const std::vector<uint8_t>> rgba;  // kReady
  uint64_t used = 0;
};
struct Job {
  std::string key, id;
  bool shiny = false;
  int size = 0;
};

std::mutex g_mutex;
std::condition_variable g_wake;
std::unordered_map<std::string, Pic> g_pics;  // by id: <key>[_shiny]_<size>
std::deque<Job> g_jobs;
bool g_worker = false;
size_t g_memory = 0;
uint64_t g_clock = 0;
std::atomic<unsigned> g_generation{0};

int fit_size(int size) { return std::max(32, std::min(kMaxSize, size)); }
std::string pic_id(const std::string& key, bool shiny, int size) {
  return key + (shiny ? "_shiny_" : "_") + std::to_string(size);
}
// a pack name: letters, digits, '_' / '-' (nothing that leaves the folder)
bool key_ok(const std::string& key) {
  if (key.empty() || key.size() > 64 || key.compare(0, 5, "club_") == 0 || key.compare(0, 10, "franchise_") == 0) return false;
  for (char c : key)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
  return true;
}
std::string home_dir() {
  const char* home = std::getenv("STRIKERS_HOME");
  return home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers";
}
std::string cache_path(const std::string& id) { return home_dir() + "/cache/big/" + id + ".png"; }
// a file's change time (0: not there / empty)
int64_t file_time(const std::string& path) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0 || st.st_size <= 0) return 0;
  return static_cast<int64_t>(st.st_mtime) > 0 ? static_cast<int64_t>(st.st_mtime) : 1;
}
bool magic(const std::string& path, const char* m) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  char b[4] = {};
  const bool ok = std::fread(b, 1, 4, f) == 4 && std::memcmp(b, m, 4) == 0;
  std::fclose(f);
  return ok;
}
// Its model and pose files on this headset, in xr_quest.cpp fr_fig_read's order (the hub's 3D figures): a pack (its
// own shiny one first) and its standing pose (guests/evo/), else its home figure (guests/home/); a Strikers character's
// r130 captain pack, else its Charged home figure. Only retarget packs (GPK2) with a pose file (GEV1), as xr_evo reads.
std::vector<std::pair<std::string, std::string>> sources(const std::string& key, bool shiny) {
  const std::string g = home_dir() + "/guests/";
  std::vector<std::pair<std::string, std::string>> tries, out;
  // (a roster's cap_<name>: the same Strikers captain as strikers_<name>)
  const bool cap = key.compare(0, 4, "cap_") == 0;
  if (key.compare(0, 9, "strikers_") == 0 || cap) {
    const std::string n = key.substr(cap ? 4 : 9);
    tries.push_back({g + "cap_" + n + ".gpk", g + "evo/cap_" + n + ".gev"});
    tries.push_back({g + "home/strikers_" + n + ".gpk", g + "home/strikers_" + n + ".gev"});
  } else {
    if (shiny) tries.push_back({g + key + "_shiny.gpk", g + "evo/" + key + ".gev"});
    tries.push_back({g + key + ".gpk", g + "evo/" + key + ".gev"});
    tries.push_back({g + "home/" + key + ".gpk", g + "home/" + key + ".gev"});
    tries.push_back({g + key + ".gpk", g + "home/" + key + ".gev"});
  }
  for (const auto& t : tries)
    if (file_time(t.first) != 0 && file_time(t.second) != 0 && magic(t.first, "GPK2") && magic(t.second, "GEV1")) out.push_back(t);
  return out;
}

// --- the Java renderer (guests/render/Big.java), found through the activity's class loader (this thread is ours,
// not one Java made, so FindClass would only see the system's classes)
struct Jni {
  bool tried = false;
  jclass cls = nullptr;
  jmethodID render = nullptr;
};
Jni g_jni;
bool jni_clear(JNIEnv* env) {
  if (!env->ExceptionCheck()) return false;
  env->ExceptionDescribe();
  env->ExceptionClear();
  return true;
}
JNIEnv* jni_env() {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());  // (attaches this thread)
  if (env == nullptr || g_jni.tried) return g_jni.cls != nullptr ? env : nullptr;
  g_jni.tried = true;
  auto* act = static_cast<jobject>(SDL_GetAndroidActivity());
  if (act == nullptr) {
    jni_clear(env);
    BLOG("no activity: no big pictures");
    return nullptr;
  }
  jclass actCls = env->GetObjectClass(act);
  jmethodID getLoader = env->GetMethodID(actCls, "getClassLoader", "()Ljava/lang/ClassLoader;");
  jobject loader = getLoader != nullptr && !jni_clear(env) ? env->CallObjectMethod(act, getLoader) : nullptr;
  jni_clear(env);
  jclass loaderCls = env->FindClass("java/lang/ClassLoader");
  jmethodID loadClass = loaderCls != nullptr ? env->GetMethodID(loaderCls, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
  jni_clear(env);
  jstring name = env->NewStringUTF("io.github.newcoke.strikers.guests.render.Big");
  jobject cls = loader != nullptr && loadClass != nullptr ? env->CallObjectMethod(loader, loadClass, name) : nullptr;
  jni_clear(env);
  if (cls != nullptr) {
    g_jni.cls = static_cast<jclass>(env->NewGlobalRef(cls));
    // r137 (Lance: every character in an action pose): Big.renderAction(gpk, gev, size, png, home, data)
    g_jni.render = env->GetStaticMethodID(g_jni.cls, "renderAction",
                                          "(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)[B");
    if (jni_clear(env) || g_jni.render == nullptr) {
      env->DeleteGlobalRef(g_jni.cls);
      g_jni.cls = nullptr;
    }
  }
  for (jobject o : {static_cast<jobject>(act), static_cast<jobject>(actCls), loader, static_cast<jobject>(loaderCls),
                    static_cast<jobject>(name), cls})
    if (o != nullptr) env->DeleteLocalRef(o);
  if (g_jni.cls == nullptr) BLOG("the renderer (guests.render.Big) not found: no big pictures");
  return g_jni.cls != nullptr ? env : nullptr;
}
// Big.renderAction: the picture (size x size straight RGBA) in its action pose (BigPose: its own celebrations / its
// class's shot keys; the Strikers files for those from STRIKERS_DATA, else found in STRIKERS_HOME) and its PNG at `png`;
// empty when it failed
std::vector<uint8_t> java_render(JNIEnv* env, const std::string& gpk, const std::string& gev, int size, const std::string& png) {
  std::vector<uint8_t> out;
  if (env->PushLocalFrame(10) != 0) {
    jni_clear(env);
    return out;
  }
  const char* data = std::getenv("STRIKERS_DATA");
  jstring a = env->NewStringUTF(gpk.c_str()), b = env->NewStringUTF(gev.c_str()), c = env->NewStringUTF(png.c_str());
  jstring d = env->NewStringUTF(home_dir().c_str()), e = env->NewStringUTF(data != nullptr ? data : "");
  auto* arr =
      static_cast<jbyteArray>(env->CallStaticObjectMethod(g_jni.cls, g_jni.render, a, b, static_cast<jint>(size), c, d, e));
  if (!jni_clear(env) && arr != nullptr && env->GetArrayLength(arr) == static_cast<jsize>(size_t(size) * size * 4)) {
    out.resize(size_t(size) * size * 4);
    env->GetByteArrayRegion(arr, 0, static_cast<jsize>(out.size()), reinterpret_cast<jbyte*>(out.data()));
    if (jni_clear(env)) out.clear();
  }
  env->PopLocalFrame(nullptr);
  return out;
}

// A picture shrunk to `to` x `to` (area-averaged, in premultiplied alpha; straight alpha out).
std::vector<uint8_t> shrink(const std::vector<uint8_t>& src, int from, int to) {
  std::vector<float> rows(size_t(from) * to * 4, 0.0f);  // across first: `from` rows of `to`
  const float step = static_cast<float>(from) / static_cast<float>(to);
  auto span = [&](int o, int* i0, int* i1) {
    *i0 = static_cast<int>(o * step);
    *i1 = std::min(from, static_cast<int>(std::ceil((o + 1) * step)));
  };
  for (int y = 0; y < from; ++y)
    for (int ox = 0; ox < to; ++ox) {
      int x0 = 0, x1 = 0;
      span(ox, &x0, &x1);
      float acc[4] = {}, wsum = 0.0f;
      for (int x = x0; x < x1; ++x) {
        const float w = std::min(static_cast<float>(x + 1), (ox + 1) * step) - std::max(static_cast<float>(x), ox * step);
        if (w <= 0.0f) continue;
        const uint8_t* p = &src[(size_t(y) * from + x) * 4];
        const float a = p[3] / 255.0f;
        acc[0] += p[0] * a * w;
        acc[1] += p[1] * a * w;
        acc[2] += p[2] * a * w;
        acc[3] += a * w;
        wsum += w;
      }
      float* o = &rows[(size_t(y) * to + ox) * 4];
      for (int k = 0; k < 4; ++k) o[k] = wsum > 0.0f ? acc[k] / wsum : 0.0f;
    }
  std::vector<uint8_t> out(size_t(to) * to * 4, 0);
  for (int oy = 0; oy < to; ++oy) {
    int y0 = 0, y1 = 0;
    span(oy, &y0, &y1);
    for (int ox = 0; ox < to; ++ox) {
      float acc[4] = {}, wsum = 0.0f;
      for (int y = y0; y < y1; ++y) {
        const float w = std::min(static_cast<float>(y + 1), (oy + 1) * step) - std::max(static_cast<float>(y), oy * step);
        if (w <= 0.0f) continue;
        const float* p = &rows[(size_t(y) * to + ox) * 4];
        for (int k = 0; k < 4; ++k) acc[k] += p[k] * w;
        wsum += w;
      }
      uint8_t* o = &out[(size_t(oy) * to + ox) * 4];
      const float a = wsum > 0.0f ? acc[3] / wsum : 0.0f;
      if (a <= 0.0f) continue;
      for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(std::min(255.0f, acc[k] / wsum / a + 0.5f));
      o[3] = static_cast<uint8_t>(std::min(255.0f, a * 255.0f + 0.5f));
    }
  }
  return out;
}

// What a picture was drawn from: its pack and pose files, their sizes and times (kept beside its PNG, <id>.png.src).
std::string file_sig(const std::string& path) {
  struct stat st {};
  if (::stat(path.c_str(), &st) != 0) return path + " -";
  return path + " " + std::to_string(static_cast<long long>(st.st_size)) + " " + std::to_string(static_cast<long long>(st.st_mtime));
}
// r144 (Lance on r143's cards: "contorted as fuck or dont show their eyes"): the reviewed list of action keys
// (STRIKERS_HOME/franchise/big_poses.txt, guests/render/BigPose.java: a pack on it in that key, every other one in its
// idle); a pack's line goes into its picture's stamp, so a key changed or taken off the list is drawn again (worker
// thread only)
const std::string& listed_pose(const std::string& gpkPath) {
  static std::unordered_map<std::string, std::string> list;
  static bool read = false;
  static const std::string idle = "idle";
  if (!read) {
    read = true;
    if (FILE* f = std::fopen((home_dir() + "/franchise/big_poses.txt").c_str(), "rb")) {
      char line[512];
      while (std::fgets(line, sizeof(line), f) != nullptr) {
        std::string l = line;
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
        const size_t tab = l.find('\t');
        if (l.empty() || l[0] == '#' || tab == std::string::npos || tab == 0) continue;
        list[l.substr(0, tab)] = l.substr(tab + 1);
      }
      std::fclose(f);
    }
  }
  std::string base = gpkPath.substr(gpkPath.find_last_of('/') + 1);
  if (base.size() > 4 && base.compare(base.size() - 4, 4, ".gpk") == 0) base.resize(base.size() - 4);
  if (base.size() > 6 && base.compare(base.size() - 6, 6, "_shiny") == 0) base.resize(base.size() - 6);
  auto it = list.find(base);
  return it != list.end() ? it->second : idle;
}
// (the first line: how it was drawn - r144's reviewed poses, v2: + the list's own cameras ("view <yaw> <pitch>", in its
// line, so in the stamp's "pose" line too) for the edge-on / back-turned ones; every earlier picture is drawn again - then
// its pose)
constexpr const char* kPoseStamp = "big r144 reviewed v2\n";
std::string src_sig(const std::pair<std::string, std::string>& s) {
  return kPoseStamp + ("pose " + listed_pose(s.first) + "\n") + file_sig(s.first) + "\n" + file_sig(s.second) + "\n";
}
std::string read_text(const std::string& path) {
  std::string t;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return t;
  char buf[512];
  size_t n = 0;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0 && t.size() < 4096) t.append(buf, n);
  std::fclose(f);
  return t;
}
// A cached PNG of exactly size x size, drawn from one of these files as they are now (the converter or a push made the
// pack / its pose again: drawn again; a time alone could go backwards with a push).
bool read_png(const std::string& path, int size, const std::vector<std::pair<std::string, std::string>>& src,
              std::vector<uint8_t>* out) {
  const std::string was = read_text(path + ".src");
  bool same = false;
  for (const auto& s : src) same = same || (!was.empty() && was == src_sig(s));
  if (!same || file_time(path) == 0) return false;
  int w = 0, h = 0, n = 0;
  unsigned char* px = stbi_load(path.c_str(), &w, &h, &n, 4);
  if (px == nullptr) return false;
  const bool ok = w == size && h == size;
  if (ok) out->assign(px, px + size_t(w) * h * 4);
  stbi_image_free(px);
  return ok;
}

void mkdirs(const std::string& dir) {
  for (size_t at = dir.find('/', 1); at != std::string::npos; at = dir.find('/', at + 1)) ::mkdir(dir.substr(0, at).c_str(), 0775);
  ::mkdir(dir.c_str(), 0775);
}

// One picture: its cached PNG, else a bigger one of it shrunk (in memory or cached), else drawn.
std::vector<uint8_t> make(const Job& job, JNIEnv** env, bool* jniDone) {
  std::vector<uint8_t> px;
  const auto src = sources(job.key, job.shiny);
  if (src.empty()) return px;
  const auto t0 = std::chrono::steady_clock::now();
  if (read_png(cache_path(job.id), job.size, src, &px)) {
    BLOG("%s from the cache", job.id.c_str());
    return px;
  }
  if (job.size < kMaxSize) {
    const std::string bigId = pic_id(job.key, job.shiny, kMaxSize);
    std::shared_ptr<const std::vector<uint8_t>> big;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      auto it = g_pics.find(bigId);
      if (it != g_pics.end() && it->second.state == kReady) big = it->second.rgba;
    }
    std::vector<uint8_t> disk;
    if (big == nullptr && read_png(cache_path(bigId), kMaxSize, src, &disk))
      big = std::make_shared<const std::vector<uint8_t>>(std::move(disk));
    if (big != nullptr) {
      px = shrink(*big, kMaxSize, job.size);
      BLOG("%s shrunk from its %d picture", job.id.c_str(), kMaxSize);
      return px;
    }
  }
  if (!*jniDone) {
    *jniDone = true;
    *env = jni_env();
  }
  if (*env == nullptr) return px;
  mkdirs(home_dir() + "/cache/big");
  for (const auto& s : src) {
    px = java_render(*env, s.first, s.second, job.size, cache_path(job.id));
    if (!px.empty()) {
      const std::string sig = src_sig(s), side = cache_path(job.id) + ".src";
      if (FILE* f = std::fopen((side + ".tmp").c_str(), "wb")) {  // (what it was drawn from, for the next time)
        const bool ok = std::fwrite(sig.data(), 1, sig.size(), f) == sig.size();
        std::fclose(f);
        if (!ok || std::rename((side + ".tmp").c_str(), side.c_str()) != 0) std::remove((side + ".tmp").c_str());
      }
      BLOG("%s drawn in %.0f ms (%s)", job.id.c_str(),
           std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), s.first.c_str());
      return px;
    }
  }
  return px;
}

// Back in memory: the oldest let go while over the budget (they stay cached on disk).
void trim_memory(const std::string& keep) {
  while (g_memory > kMemBudget) {
    Pic* oldest = nullptr;
    for (auto& kv : g_pics)
      if (kv.second.state == kReady && kv.first != keep && (oldest == nullptr || kv.second.used < oldest->used)) oldest = &kv.second;
    if (oldest == nullptr) break;
    g_memory -= oldest->rgba->size();
    oldest->rgba.reset();
    oldest->state = kIdle;
  }
}

void worker() {
  setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), 10);  // (behind the game's own threads)
  JNIEnv* env = nullptr;
  bool jniDone = false;
  for (;;) {
    Job job;
    {
      std::unique_lock<std::mutex> lock(g_mutex);
      g_wake.wait(lock, [] { return !g_jobs.empty(); });
      job = std::move(g_jobs.front());
      g_jobs.pop_front();
    }
    std::vector<uint8_t> px = make(job, &env, &jniDone);
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      Pic& p = g_pics[job.id];
      if (px.size() == size_t(job.size) * job.size * 4) {
        p.state = kReady;
        p.rgba = std::make_shared<const std::vector<uint8_t>>(std::move(px));
        p.used = ++g_clock;
        g_memory += p.rgba->size();
        trim_memory(job.id);
      } else {
        p.state = kFailed;
        BLOG("%s: no big picture (no model on this headset, or it failed): its portrait stays", job.id.c_str());
      }
    }
    ++g_generation;
  }
}

// Asked for (g_mutex held): queued unless it's there or failed; `urgent` (someone waits on it) goes first.
void ask(const std::string& key, bool shiny, int size, const std::string& id, bool urgent) {
  Pic& p = g_pics[id];
  if (p.state == kReady || p.state == kFailed) return;
  if (p.state == kQueued) {
    if (!urgent) return;
    for (auto it = g_jobs.begin(); it != g_jobs.end(); ++it)
      if (it->id == id) {
        Job j = std::move(*it);
        g_jobs.erase(it);
        g_jobs.push_front(std::move(j));
        break;
      }
    return;
  }
  p.state = kQueued;
  Job j;
  j.key = key;
  j.id = id;
  j.shiny = shiny;
  j.size = size;
  if (urgent) g_jobs.push_front(std::move(j));
  else g_jobs.push_back(std::move(j));
  if (!g_worker) {
    g_worker = true;
    std::thread(worker).detach();
  }
  g_wake.notify_one();
}

}  // namespace

bool get(const std::string& key, bool shiny, int size, std::vector<uint8_t>* rgba) {
  if (!key_ok(key)) return false;
  size = fit_size(size);
  const std::string id = pic_id(key, shiny, size);
  std::shared_ptr<const std::vector<uint8_t>> px;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_pics.find(id);
    if (it != g_pics.end() && it->second.state == kReady) {
      it->second.used = ++g_clock;
      px = it->second.rgba;
    } else {
      ask(key, shiny, size, id, true);
    }
  }
  if (px == nullptr) return false;
  if (rgba != nullptr) *rgba = *px;  // (copied outside the lock)
  return true;
}

void prefetch(const std::string& key, bool shiny, int size) {
  if (!key_ok(key)) return;
  size = fit_size(size);
  std::lock_guard<std::mutex> lock(g_mutex);
  ask(key, shiny, size, pic_id(key, shiny, size), false);
}

bool failed(const std::string& key, bool shiny, int size) {
  if (!key_ok(key)) return true;
  size = fit_size(size);
  const std::string id = pic_id(key, shiny, size);
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_pics.find(id);
  if (it != g_pics.end()) return it->second.state == kFailed;
  ask(key, shiny, size, id, false);  // (never asked: the worker finds out, without touching the disk here)
  return false;
}

unsigned generation() { return g_generation.load(); }

}  // namespace xrbig

#else  // not the Quest build: no pictures (callers keep their portraits)

namespace xrbig {
bool get(const std::string&, bool, int, std::vector<uint8_t>*) { return false; }
void prefetch(const std::string&, bool, int) {}
bool failed(const std::string&, bool, int) { return true; }
unsigned generation() { return 0; }
}  // namespace xrbig

#endif
