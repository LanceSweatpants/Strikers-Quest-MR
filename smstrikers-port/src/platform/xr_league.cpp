// See include/port/xr_league.h. Built with xr_quest.cpp in strikers_xr (C++20, no port prelude).
//
// r100: one text WebSocket to wss://<room server>/league/<CODE> (NetWs.java, as xr_net.cpp's relay link), on a
// thread of its own: the game thread queues JSON messages and takes what came; nothing here waits on the
// network in a frame. The server keeps the league (server/src/index.js, the League class).
#include "../../include/port/xr_league.h"

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#include <SDL3/SDL_system.h>
#include <android/log.h>
#include <jni.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <thread>

namespace {

#define LLOG(...)                                                       \
  do {                                                                  \
    __android_log_print(ANDROID_LOG_INFO, "strikers_xr", __VA_ARGS__); \
    std::fprintf(stderr, "[league] ");                                  \
    std::fprintf(stderr, __VA_ARGS__);                                  \
    std::fprintf(stderr, "\n");                                         \
    std::fflush(stderr);                                                \
  } while (0)

constexpr const char* kServerHost = "strikers-rooms.strikers-rooms.workers.dev";
constexpr int kServerPort = 443;
constexpr int kProto = 15;  // xr_net.cpp kProtocol of the build that brought leagues
constexpr const char* kCodeChars = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

uint64_t now_ms() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// --- a small JSON reader (the server's messages: objects, arrays, strings, numbers, true/false/null) ---
struct J {
  enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
  bool b = false;
  double n = 0.0;
  std::string s;
  std::vector<J> a;
  std::map<std::string, J> o;
  const J& operator[](const char* k) const {
    static const J none;
    if (kind != Obj) return none;
    auto it = o.find(k);
    return it != o.end() ? it->second : none;
  }
  int i(int dflt = 0) const { return kind == Num ? static_cast<int>(n) : (kind == Bool ? (b ? 1 : 0) : dflt); }
  bool t() const { return kind == Bool ? b : (kind == Num ? n != 0.0 : false); }
};

struct Reader {
  const char* p;
  const char* e;
  void ws() {
    while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
  }
  bool str(std::string* out) {
    if (p >= e || *p != '"') return false;
    ++p;
    out->clear();
    while (p < e && *p != '"') {
      char c = *p++;
      if (c == '\\' && p < e) {
        const char x = *p++;
        if (x == 'n') c = '\n';
        else if (x == 't') c = '\t';
        else if (x == 'r') c = '\r';
        else if (x == 'b') c = '\b';
        else if (x == 'f') c = '\f';
        else if (x == 'u' && e - p >= 4) {
          const unsigned v = static_cast<unsigned>(std::strtoul(std::string(p, 4).c_str(), nullptr, 16));
          p += 4;
          if (v < 0x80) {
            out->push_back(static_cast<char>(v));
          } else if (v < 0x800) {
            out->push_back(static_cast<char>(0xC0 | (v >> 6)));
            out->push_back(static_cast<char>(0x80 | (v & 0x3F)));
          } else {
            out->push_back(static_cast<char>(0xE0 | (v >> 12)));
            out->push_back(static_cast<char>(0x80 | ((v >> 6) & 0x3F)));
            out->push_back(static_cast<char>(0x80 | (v & 0x3F)));
          }
          continue;
        } else c = x;
      }
      out->push_back(c);
    }
    if (p >= e) return false;
    ++p;
    return true;
  }
  bool val(J* out, int depth = 0) {
    if (depth > 32) return false;
    ws();
    if (p >= e) return false;
    if (*p == '{') {
      ++p;
      out->kind = J::Obj;
      ws();
      if (p < e && *p == '}') return ++p, true;
      for (;;) {
        ws();
        std::string k;
        if (!str(&k)) return false;
        ws();
        if (p >= e || *p != ':') return false;
        ++p;
        if (!val(&out->o[k], depth + 1)) return false;
        ws();
        if (p < e && *p == ',') { ++p; continue; }
        if (p < e && *p == '}') return ++p, true;
        return false;
      }
    }
    if (*p == '[') {
      ++p;
      out->kind = J::Arr;
      ws();
      if (p < e && *p == ']') return ++p, true;
      for (;;) {
        out->a.emplace_back();
        if (!val(&out->a.back(), depth + 1)) return false;
        ws();
        if (p < e && *p == ',') { ++p; continue; }
        if (p < e && *p == ']') return ++p, true;
        return false;
      }
    }
    if (*p == '"') {
      out->kind = J::Str;
      return str(&out->s);
    }
    if (e - p >= 4 && std::strncmp(p, "true", 4) == 0) { out->kind = J::Bool; out->b = true; p += 4; return true; }
    if (e - p >= 5 && std::strncmp(p, "false", 5) == 0) { out->kind = J::Bool; out->b = false; p += 5; return true; }
    if (e - p >= 4 && std::strncmp(p, "null", 4) == 0) { out->kind = J::Null; p += 4; return true; }
    char* end = nullptr;
    const std::string num(p, std::min<size_t>(32, static_cast<size_t>(e - p)));
    out->n = std::strtod(num.c_str(), &end);
    if (end == num.c_str()) return false;
    out->kind = J::Num;
    p += end - num.c_str();
    return true;
  }
};

bool parse(const std::string& text, J* out) {
  Reader r{text.data(), text.data() + text.size()};
  return r.val(out);
}

std::string quote(const std::string& s) {
  std::string o = "\"";
  for (const unsigned char c : s) {
    if (c == '"') o += "\\\"";
    else if (c == '\\') o += "\\\\";
    else if (c == '\n') o += "\\n";
    else if (c == '\r') o += "\\r";
    else if (c == '\t') o += "\\t";
    else if (c < 0x20) {
      char b[8];
      std::snprintf(b, sizeof b, "\\u%04x", c);
      o += b;
    } else o.push_back(static_cast<char>(c));
  }
  return o + "\"";
}

// --- the link ---
struct Jni {
  jclass cls = nullptr;
  jmethodID open = nullptr, sendText = nullptr, recv = nullptr, close = nullptr;
};
Jni g_jni;
bool g_jniTried = false;

bool jni_ok(JNIEnv* env) {
  if (!env->ExceptionCheck()) return true;
  env->ExceptionDescribe();
  env->ExceptionClear();
  return false;
}

bool jni_init() {
  if (g_jniTried) return g_jni.cls != nullptr;
  g_jniTried = true;
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) return false;
  jclass local = env->FindClass("io/github/newcoke/strikers/NetWs");
  if (local == nullptr) {
    env->ExceptionClear();
    return false;
  }
  g_jni.cls = static_cast<jclass>(env->NewGlobalRef(local));
  env->DeleteLocalRef(local);
  auto method = [&](const char* name, const char* sig) -> jmethodID {
    const jmethodID id = env->GetStaticMethodID(g_jni.cls, name, sig);
    return jni_ok(env) ? id : nullptr;
  };
  g_jni.open = method("open", "(Ljava/lang/String;ILjava/lang/String;I)I");
  g_jni.sendText = method("sendText", "(ILjava/lang/String;)Z");
  g_jni.recv = method("recv", "(I[BI)I");
  g_jni.close = method("close", "(I)V");
  if (!g_jni.open || !g_jni.sendText || !g_jni.recv || !g_jni.close) g_jni.cls = nullptr;
  return g_jni.cls != nullptr;
}

struct Shared {
  std::mutex m;
  std::string code;                 // the league to be in ("" none)
  std::deque<std::string> out;      // to send (cleared on a new code)
  PortLeagueState st{0, false, "", 8, 0, -1, false, {}, ""};
  unsigned serial = 0;
  bool careerNew = false;
  std::string career;
  int careerV = 0;
  int putAnswer = 0, putV = 0;
  bool goNew = false;
  int goGame = -1;
  std::string goRoom;
  bool goHost = false;
  std::atomic<bool> running{false};
};
Shared g;
std::string g_myId, g_myName;

void ensure_me() {
  if (!g_myId.empty()) return;
  if (const char* id = std::getenv("STRIKERS_PLAYER_ID"); id != nullptr && std::strlen(id) == 16) g_myId = id;
  const char* home = std::getenv("STRIKERS_HOME");
  const std::string path = std::string(home != nullptr && *home ? home : "/storage/emulated/0/Strikers") + "/league-id.txt";
  if (g_myId.empty()) {
    if (FILE* f = std::fopen(path.c_str(), "r")) {
      char buf[32] = {};
      if (std::fgets(buf, sizeof buf, f) != nullptr) {
        std::string s(buf);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.size() == 16) g_myId = s;
      }
      std::fclose(f);
    }
  }
  if (g_myId.empty()) {
    std::random_device rd;
    char buf[17];
    std::snprintf(buf, sizeof buf, "%08x%08x", rd(), rd());
    g_myId = buf;
    if (FILE* f = std::fopen(path.c_str(), "w")) {
      std::fprintf(f, "%s\n", g_myId.c_str());
      std::fclose(f);
    }
  }
  for (char& c : g_myId) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  const char* n = std::getenv("STRIKERS_TEAM_NAME");
  g_myName = n != nullptr && *n ? n : "";
  if (g_myName.empty()) g_myName = "Player " + g_myId.substr(0, 4);
}

void on_message(const std::string& text) {
  J m;
  if (!parse(text, &m) || m.kind != J::Obj) return;
  const std::string t = m["t"].s;
  std::lock_guard<std::mutex> lock(g.m);
  if (t == "state") {
    PortLeagueState& st = g.st;
    st.link = 2;
    st.exists = m["exists"].t();
    st.code = m["code"].s;
    st.max = m["max"].i(8);
    st.version = m["v"].i(0);
    st.myClub = m["me"]["club"].i(-1);
    st.owner = m["me"]["owner"].t();
    st.members.clear();
    for (const J& x : m["members"].a) {
      PortLeagueMember pm{x["name"].s, x["club"].i(-1), x["online"].t(), x["owner"].t(), x["ready"].i(-1)};
      st.members.push_back(pm);
    }
    ++g.serial;
  } else if (t == "career") {
    g.career = m["text"].s;
    g.careerV = m["v"].i(0);
    g.careerNew = true;
    ++g.serial;
  } else if (t == "put_ok" || t == "put_conflict") {
    g.putAnswer = t == "put_ok" ? 1 : 2;
    g.putV = m["v"].i(0);
    ++g.serial;
  } else if (t == "go") {
    g.goNew = true;
    g.goGame = m["game"].i(-1);
    g.goRoom = m["room"].s;
    g.goHost = m["host"].t();
    ++g.serial;
    LLOG("match: game %d in room %s (%s)", g.goGame, g.goRoom.c_str(), g.goHost ? "host" : "guest");
  } else if (t == "error") {
    g.st.error = m["msg"].s;
    ++g.st.errors;
    ++g.serial;
    LLOG("the server says: %s", g.st.error.c_str());
  }
}

void link_thread() {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) return;
  auto rbuf = static_cast<jbyteArray>(env->NewGlobalRef(env->NewByteArray(4 * 1024 * 1024)));
  auto jhost = static_cast<jstring>(env->NewGlobalRef(env->NewStringUTF(kServerHost)));
  std::vector<char> rx(4 * 1024 * 1024);
  int handle = -1;
  std::string code;
  uint64_t retryAt = 0, lastPing = 0;
  unsigned failures = 0;
  auto drop = [&]() {
    if (handle >= 0) {
      env->CallStaticVoidMethod(g_jni.cls, g_jni.close, handle);
      jni_ok(env);
    }
    handle = -1;
    std::lock_guard<std::mutex> lock(g.m);
    g.st.link = g.code.empty() ? 0 : 1;
    ++g.serial;
  };
  while (g.running.load()) {
    std::string want;
    {
      std::lock_guard<std::mutex> lock(g.m);
      want = g.code;
    }
    if (handle >= 0 && want != code) drop();
    if (handle < 0) {
      if (want.empty() || now_ms() < retryAt) {
        usleep(100000);
        continue;
      }
      const std::string path = "/league/" + want;
      jstring jpath = env->NewStringUTF(path.c_str());
      handle = jpath != nullptr ? env->CallStaticIntMethod(g_jni.cls, g_jni.open, jhost, kServerPort, jpath, 6000) : -1;
      if (jpath != nullptr) env->DeleteLocalRef(jpath);
      if (!jni_ok(env)) handle = -1;
      if (handle < 0) {
        ++failures;
        retryAt = now_ms() + std::min<uint64_t>(15000, 1000ull * failures);
        if (failures <= 3 || failures % 10 == 0) LLOG("the server did not answer (try %u)", failures);
        continue;
      }
      failures = 0;
      code = want;
      LLOG("in league %s", code.c_str());
      const std::string hello = "{\"t\":\"hello\",\"pid\":" + quote(g_myId) + ",\"name\":" + quote(g_myName) +
                                ",\"proto\":" + std::to_string(kProto) + "}";
      std::lock_guard<std::mutex> lock(g.m);
      g.out.push_front(hello);
    }
    std::deque<std::string> out;
    {
      std::lock_guard<std::mutex> lock(g.m);
      if (g.code != code) continue;
      out.swap(g.out);
    }
    if (now_ms() - lastPing > 20000) {
      out.push_back("{\"t\":\"ping\"}");
      lastPing = now_ms();
    }
    bool failed = false;
    while (!out.empty()) {
      jstring js = env->NewStringUTF(out.front().c_str());
      const jboolean ok = js != nullptr ? env->CallStaticBooleanMethod(g_jni.cls, g_jni.sendText, handle, js) : JNI_FALSE;
      if (js != nullptr) env->DeleteLocalRef(js);
      if (!jni_ok(env) || !ok) {
        failed = true;
        break;
      }
      out.pop_front();
    }
    if (failed) {
      {
        std::lock_guard<std::mutex> lock(g.m);
        while (!out.empty()) {  // (the hello goes again on the next link anyway)
          if (out.back().find("\"t\":\"hello\"") == std::string::npos && out.back() != "{\"t\":\"ping\"}")
            g.out.push_front(out.back());
          out.pop_back();
        }
      }
      drop();
      retryAt = now_ms() + 500;
      continue;
    }
    for (;;) {
      const jint n = env->CallStaticIntMethod(g_jni.cls, g_jni.recv, handle, rbuf, 50);
      if (!jni_ok(env) || n == -2) break;
      if (n < 0) {
        LLOG("the server closed the link");
        drop();
        retryAt = now_ms() + 1000;
        break;
      }
      env->GetByteArrayRegion(rbuf, 0, n, reinterpret_cast<jbyte*>(rx.data()));
      on_message(std::string(rx.data(), static_cast<size_t>(n)));
    }
  }
  drop();
}

void ensure_thread() {
  if (g.running.load()) return;
  if (!jni_init()) {
    LLOG("no Java link class: leagues are off");
    return;
  }
  ensure_me();
  g.running = true;
  std::thread(link_thread).detach();
}

void queue(const std::string& msg) {
  std::lock_guard<std::mutex> lock(g.m);
  if (!g.code.empty()) g.out.push_back(msg);
}

}  // namespace

void PortLeagueConnect(const std::string& raw) {
  std::string code;
  for (const char c : raw) {
    if (c >= 'a' && c <= 'z') code.push_back(static_cast<char>(c - 'a' + 'A'));
    else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) code.push_back(c);
  }
  if (code.size() < 4 || code.size() > 8) code.clear();
  if (!code.empty()) ensure_thread();
  std::lock_guard<std::mutex> lock(g.m);
  if (code == g.code) return;
  g.code = code;
  g.out.clear();
  g.st = PortLeagueState{code.empty() ? 0 : 1, false, code, 8, 0, -1, false, {}, ""};
  g.careerNew = false;
  g.putAnswer = 0;
  g.goNew = false;
  ++g.serial;
}

std::string PortLeagueNewCode() {
  std::random_device rd;
  std::string c;
  for (int i = 0; i < 6; ++i) c.push_back(kCodeChars[rd() % 32]);
  return c;
}

std::string PortLeagueMyId() {
  ensure_me();
  return g_myId;
}

std::string PortLeagueMyName() {
  ensure_me();
  return g_myName;
}

PortLeagueState PortLeagueGetState(unsigned* serial) {
  std::lock_guard<std::mutex> lock(g.m);
  if (serial != nullptr) *serial = g.serial;
  return g.st;
}

void PortLeagueCreate(int club, const std::string& career, int maxMembers) {
  queue("{\"t\":\"create\",\"club\":" + std::to_string(club) + ",\"max\":" + std::to_string(maxMembers) +
        ",\"career\":" + quote(career) + "}");
}

void PortLeagueClaim(int club) { queue("{\"t\":\"claim\",\"club\":" + std::to_string(club) + "}"); }
void PortLeagueLeave() { queue("{\"t\":\"leave\"}"); }
void PortLeagueGet() { queue("{\"t\":\"get\"}"); }

bool PortLeagueTakeCareer(std::string* text, int* version) {
  std::lock_guard<std::mutex> lock(g.m);
  if (!g.careerNew) return false;
  g.careerNew = false;
  *text = std::move(g.career);
  g.career.clear();
  *version = g.careerV;
  return true;
}

void PortLeaguePut(int base, const std::string& career, const char* op) {
  {
    std::lock_guard<std::mutex> lock(g.m);
    g.putAnswer = 0;
  }
  queue("{\"t\":\"put\",\"base\":" + std::to_string(base) + ",\"op\":" + quote(op != nullptr ? op : "") +
        ",\"text\":" + quote(career) + "}");
}

int PortLeagueTakePutAnswer(int* version) {
  std::lock_guard<std::mutex> lock(g.m);
  const int a = g.putAnswer;
  if (a != 0) {
    *version = g.putV;
    g.putAnswer = 0;
  }
  return a;
}

void PortLeagueReady(int game, int home, int away) {
  queue("{\"t\":\"ready\",\"game\":" + std::to_string(game) + ",\"home\":" + std::to_string(home) +
        ",\"away\":" + std::to_string(away) + "}");
}

void PortLeagueUnready() { queue("{\"t\":\"unready\"}"); }

bool PortLeagueTakeGo(int* game, std::string* room, bool* host) {
  std::lock_guard<std::mutex> lock(g.m);
  if (!g.goNew) return false;
  g.goNew = false;
  *game = g.goGame;
  *room = g.goRoom;
  *host = g.goHost;
  return true;
}

#else  // not the Quest build: no leagues

void PortLeagueConnect(const std::string&) {}
std::string PortLeagueNewCode() { return "TEST01"; }
std::string PortLeagueMyId() { return "0000000000000000"; }
std::string PortLeagueMyName() { return "Player"; }
PortLeagueState PortLeagueGetState(unsigned* serial) {
  if (serial) *serial = 0;
  return PortLeagueState{0, false, "", 8, 0, -1, false, {}, ""};
}
void PortLeagueCreate(int, const std::string&, int) {}
void PortLeagueClaim(int) {}
void PortLeagueLeave() {}
void PortLeagueGet() {}
bool PortLeagueTakeCareer(std::string*, int*) { return false; }
void PortLeaguePut(int, const std::string&, const char*) {}
int PortLeagueTakePutAnswer(int*) { return 0; }
void PortLeagueReady(int, int, int) {}
void PortLeagueUnready() {}
bool PortLeagueTakeGo(int*, std::string*, bool*) { return false; }

#endif
