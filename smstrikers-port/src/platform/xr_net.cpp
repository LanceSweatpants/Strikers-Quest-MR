// See include/port/xr_net.h (the game's side) and xr_net.hpp (xr_quest.cpp's side). Built with
// xr_quest.cpp in strikers_xr (C++20, no port prelude).
//
// The room (r39) is a room code on the game's own relay (server/ in this repository): each headset holds
// one outbound WebSocket to it (nothing to forward on anyone's router) and sends small binary
// messages, which the relay hands to everyone in the room, so each headset also hears its own and
// skips them. A network thread owns the connection; the game thread queues what to send and takes
// what arrived once a headset frame (poll), so the lockstep never waits on the network in a frame.
#include "../../include/port/xr_net.h"
#include "../../include/port/edition.h"  // strikers-quest r43: Classic / Plus, in the hello
#include "xr_net.hpp"

extern "C" void PortNetSetFixedStep(int on);  // src/platform/vi.c

#if defined(__ANDROID__) && defined(STRIKERS_XR)

#include <SDL3/SDL_system.h>
#include <android/log.h>
#include <jni.h>
#include <unistd.h>

#include "xr_voice.h"  // strikers-quest r44: voice frames ride on the input messages
#include "xr_home.h"   // strikers-quest r61: who the friend is, for the online rival

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace xrnet {
namespace {

// r39: also into the game log (stderr -> /sdcard/Strikers/strikers-log.txt). The headset's system
// log turns over within minutes (thermal audit spam), and the first online night's lines were gone
// before anyone could read them.
static void net_file_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void net_file_log(const char* fmt, ...) {
  char line[512];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  std::fprintf(stderr, "[xr] %s\n", line);
}
#define NLOG(...)                                                       \
  do {                                                                  \
    __android_log_print(ANDROID_LOG_INFO, "strikers_xr", __VA_ARGS__); \
    xrnet::net_file_log(__VA_ARGS__);                                   \
  } while (0)

// r39: the game's own room server (server/ in this repository, a Cloudflare Worker). Each headset holds
// one WebSocket to wss://<host>/room/<CODE>; every binary message goes to everyone in that room,
// the sender included (the game skips its own, and its echoes prove the link is alive). The code is
// the one the player picked in the launcher panel (LobbyActivity -> STRIKERS_ROOM, or
// StrikersActivity.nativeSetRoom). No code: no network at all, the game is offline.
// r33-r38 used one fixed room on free public MQTT brokers; two headsets could end up on different
// brokers, and the brokers dropped messages (the first online night froze).
constexpr const char* kServerHost = "strikers-rooms.strikers-rooms.workers.dev";
constexpr int kServerPort = 443;
// r39: room server + room codes + resend fix (r38 headsets: 1). r42: 3, the held goal replay (the
// post-goal show) changes the game's frames, so an r41 and an r42 headset would drift apart.
// strikers-quest r43: 4, the hello and the start carry the Version (Classic / Plus, edition.h).
// strikers-quest r44: 5, the input message ends with the voice frames (xr_voice.h), and quick match
// pairs players by Version and protocol (LobbyActivity, the server's /quick/<KEY>).
// strikers-quest r50: 6, the sounds draw their own random numbers now (nlAudioSeed, NL/nlMath.cpp), so
// the game's own numbers differ from an r49 headset's whenever a crowd chant or voice line picks one.
// strikers-quest r63: 7, online co-op Cup: the hello says Friendly or cup (and how far the cup is),
// the start can carry the host's whole cup.
// r67: 8, guest characters in Choice; r67b: 9, and their own stats. r76: 10, franchise co-op (the kCoop
// message, the start's 'F' tail, Choice.mode PORT_NET_MODE_FRANCHISE pairing only with its own kind).
// r78: 11, the 'F' tail carries each side's co-captain (class, guest, stats). r79: 12, so does the choice
// (a Grudge Match's co-captain, at its end). r80: 13, guest sidekicks keep their own stats at kick-off
// (Team.cpp ResetCharacters): an r79 headset would play the same match differently. r86: 14, franchise
// matches are 4 minutes (GameTweaks).
constexpr uint8_t kProtocol = 14;

uint64_t now_ms() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count());
}

// A room code: 3-8 letters or digits, upper case; anything else means no room.
std::string sanitize_room(const char* s) {
  std::string r;
  if (s == nullptr) return r;
  for (; *s != '\0' && r.size() < 8; ++s) {
    const char c = *s;
    if (c >= 'a' && c <= 'z') r.push_back(static_cast<char>(c - 'a' + 'A'));
    else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) r.push_back(c);
  }
  return r.size() >= 3 ? r : std::string();
}

// strikers-quest r43: this headset's Version on the wire, 0 Classic / 1 Plus (edition.h). Two headsets
// on different ones run different games (Plus holds the goal replay; its crowd plays samples of its
// own), so they never play each other.
uint8_t local_edition() { return PortPlusFeatures() ? 1 : 0; }
const char* edition_name(uint8_t edition) { return edition != 0 ? "Plus" : "Classic"; }

// strikers-quest r43: why an online match did not start, for the player. Before r43 that was only in
// the log, and the player saw a CPU opponent. online-note.txt beside the log (STRIKERS_HOME) holds one
// line, "edition=classic|plus" (the friend's Version) or "protocol=N" (the friend's build), which the
// launcher panel puts into words (LobbyActivity.showOnlineNote); StrikersActivity polls the flag
// (nativeTakeOnlineNote) and goes back to that panel, where the Version button is.
std::atomic<bool> g_notePending{false};

void online_note(const char* line) {
  const char* home = std::getenv("STRIKERS_HOME");
  const std::string path =
      std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/online-note.txt";
  if (FILE* f = std::fopen(path.c_str(), "w")) {
    std::fprintf(f, "%s\n", line);
    std::fclose(f);
  }
  g_notePending = true;
}

// ---------------------------------------------------------------------------------------------
// The connection: NetWs.java (TLS + WebSocket, Android's own TLS and certificate checks) over JNI.
// The class is looked up on the game thread, a Java thread whose class loader knows the app's
// classes; the network thread attaches itself through SDL and uses the cached global references.
// ---------------------------------------------------------------------------------------------

struct Jni {
  jclass cls = nullptr;
  jmethodID open = nullptr, send = nullptr, recv = nullptr, close = nullptr;
};
Jni g_jni;

bool jni_ok(JNIEnv* env, const char* what) {
  if (!env->ExceptionCheck()) return true;
  env->ExceptionDescribe();
  env->ExceptionClear();
  NLOG("net: the Java side of %s threw", what);
  return false;
}

bool jni_init() {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) return false;
  jclass local = env->FindClass("io/github/newcoke/strikers/NetWs");
  if (local == nullptr) {
    env->ExceptionClear();
    return false;
  }
  g_jni.cls = static_cast<jclass>(env->NewGlobalRef(local));
  env->DeleteLocalRef(local);
  // strikers-quest r39b: each lookup checked on its own. A missing method throws, and the next JNI
  // call made with that exception still pending aborts the app.
  auto method = [&](const char* name, const char* sig) -> jmethodID {
    const jmethodID id = env->GetStaticMethodID(g_jni.cls, name, sig);
    return jni_ok(env, name) ? id : nullptr;
  };
  g_jni.open = method("open", "(Ljava/lang/String;ILjava/lang/String;I)I");
  g_jni.send = method("send", "(I[B)Z");
  g_jni.recv = method("recv", "(I[BI)I");
  g_jni.close = method("close", "(I)V");
  return g_jni.open != nullptr && g_jni.send != nullptr && g_jni.recv != nullptr && g_jni.close != nullptr;
}

// ---------------------------------------------------------------------------------------------
// The network thread
// ---------------------------------------------------------------------------------------------

struct Shared {
  std::mutex m;
  std::deque<std::vector<uint8_t>> out;  // to send
  std::deque<std::vector<uint8_t>> in;   // received (every message in the room, ours included)
  std::string room;                      // the room picked, "" = none (under m)
  // strikers-quest r39b: the room the game has moved to (poll, never during a match), which the
  // connection follows (under m). `room` is only the pick, so a pick mid-match no longer cuts it.
  std::string activeRoom;
  bool roomSet = false;                  // room came from nativeSetRoom (under m)
  // strikers-quest r45: the tournament match this room is for (the panel's match id), "" = none
  // (under m): its result is written for the panel when the match ends.
  std::string tourney;
  // strikers-quest r64: this headset made the room (the panel's Host an online match), under m: in
  // co-op Cup the maker's cup is played and the maker plays the captain.
  bool roomMaker = false;
  std::atomic<bool> connected{false};
  std::atomic<bool> running{false};
  // strikers-quest r50: the round trips the network thread measured (under m), for the game thread's
  // median, and whether it should be measuring (not during an online match; the poll sets it).
  std::deque<uint32_t> rtts;
  std::atomic<bool> pinging{true};
};
Shared sh;

// r50: the ping and pong messages (header: [type][protocol][sender id], little-endian) built here,
// on the network thread: kPing [time ms u32], kPong [to id u64][time ms u32]. The type numbers are
// the game's (Type, below).
constexpr uint8_t kPingType = 5, kPongType = 6;
uint32_t rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t rd64(const uint8_t* p) {
  uint64_t x = 0;
  for (int i = 0; i < 8; ++i) x |= static_cast<uint64_t>(p[i]) << (8 * i);
  return x;
}
void wr32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
void wr64(std::vector<uint8_t>& v, uint64_t x) {
  for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

void net_thread(uint64_t myId) {
  auto* env = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
  if (env == nullptr) {
    NLOG("net: the network thread has no Java environment, staying offline");
    return;
  }
  auto rbuf = static_cast<jbyteArray>(env->NewGlobalRef(env->NewByteArray(65536)));
  auto jhost = static_cast<jstring>(env->NewGlobalRef(env->NewStringUTF(kServerHost)));
  std::vector<uint8_t> rx(65536);
  int handle = -1;
  std::string room;
  uint64_t retryAt = 0;
  // strikers-quest r39b: when the oldest send still waiting for its echo went out, 0 = none waiting.
  uint64_t unechoedSince = 0;
  unsigned failures = 0;
  auto drop = [&](const char* why) {
    if (handle >= 0) {
      env->CallStaticVoidMethod(g_jni.cls, g_jni.close, handle);
      jni_ok(env, "close");
      if (why != nullptr) NLOG("net: left room %s (%s)", room.c_str(), why);
    }
    handle = -1;
    sh.connected = false;
  };
  // strikers-quest r50 (Lance: online in Tabletop "the lag was ATROCIOUS"): pings go out and are
  // answered right here, not from the game thread's once-a-frame poll. The round trip used to include
  // up to a headset frame on each side, longer in Tabletop's heavier frames, and the host sets the
  // whole match's input delay from it: a friend in the same house measured ~220 ms and played 9 frames
  // (150 ms) behind. A message straight onto the link; false if it did not go.
  auto send_now = [&](const std::vector<uint8_t>& msg) {
    jbyteArray arr = env->NewByteArray(static_cast<jsize>(msg.size()));
    if (arr == nullptr) {
      jni_ok(env, "send");
      return false;
    }
    env->SetByteArrayRegion(arr, 0, static_cast<jsize>(msg.size()), reinterpret_cast<const jbyte*>(msg.data()));
    const jboolean ok = env->CallStaticBooleanMethod(g_jni.cls, g_jni.send, handle, arr);
    const bool sent = jni_ok(env, "send") && ok;
    env->DeleteLocalRef(arr);
    if (sent && unechoedSince == 0) unechoedSince = now_ms();
    return sent;
  };
  uint64_t lastPingMs = 0;
  while (sh.running.load()) {
    std::string want;
    {
      std::lock_guard<std::mutex> lock(sh.m);
      want = sh.activeRoom;
    }
    if (handle >= 0 && want != room) drop("the room changed");
    if (handle < 0) {
      if (want.empty() || now_ms() < retryAt) {
        usleep(50000);
        continue;
      }
      const std::string path = "/room/" + want;
      jstring jpath = env->NewStringUTF(path.c_str());
      // r39b: no call with an exception (out of memory) still pending from NewStringUTF.
      handle = jpath != nullptr ? env->CallStaticIntMethod(g_jni.cls, g_jni.open, jhost, kServerPort, jpath, 5000)
                                : -1;
      if (jpath != nullptr) env->DeleteLocalRef(jpath);
      if (!jni_ok(env, "open")) handle = -1;
      if (handle < 0) {
        ++failures;
        retryAt = now_ms() + std::min<uint64_t>(10000, 1000ull * failures);
        if (failures <= 3 || failures % 10 == 0) NLOG("net: the room server did not answer (try %u)", failures);
        continue;
      }
      failures = 0;
      room = want;
      unechoedSince = 0;
      sh.connected = true;
      NLOG("net: in room %s", room.c_str());
    }

    // Out, in order. What could not be sent goes back to the front of the queue for the next link.
    std::deque<std::vector<uint8_t>> out;
    {
      std::lock_guard<std::mutex> lock(sh.m);
      // r39b: the game has moved to another room: nothing of the new room's down this link (the
      // top of the loop leaves it first).
      if (sh.activeRoom != room) continue;
      out.swap(sh.out);
    }
    while (!out.empty()) {
      const std::vector<uint8_t>& msg = out.front();
      jbyteArray arr = env->NewByteArray(static_cast<jsize>(msg.size()));
      if (arr == nullptr) {  // r39b: out of memory; cleared before any other JNI call
        jni_ok(env, "send");
        break;
      }
      env->SetByteArrayRegion(arr, 0, static_cast<jsize>(msg.size()), reinterpret_cast<const jbyte*>(msg.data()));
      // strikers-quest r39b: the exception check first, whatever send returned. After `send() &&`
      // it was skipped on a false return, and the next JNI call (close) aborted the app.
      const jboolean ok = env->CallStaticBooleanMethod(g_jni.cls, g_jni.send, handle, arr);
      const bool sent = jni_ok(env, "send") && ok;
      env->DeleteLocalRef(arr);
      if (!sent) break;
      out.pop_front();
      if (unechoedSince == 0) unechoedSince = now_ms();
    }
    if (!out.empty()) {
      {
        std::lock_guard<std::mutex> lock(sh.m);
        while (sh.activeRoom == room && !out.empty()) {  // r39b: only while it is still this room's
          sh.out.push_front(std::move(out.back()));
          out.pop_back();
        }
      }
      drop("a send failed");
      retryAt = now_ms() + 200;
      continue;
    }

    // In: whole messages, until none arrives for 4 ms.
    for (;;) {
      const jint n = env->CallStaticIntMethod(g_jni.cls, g_jni.recv, handle, rbuf, 4);
      if (!jni_ok(env, "recv") || n == -2) break;
      if (n < 0) {
        drop("closed by the server");
        retryAt = now_ms() + 200;
        break;
      }
      env->GetByteArrayRegion(rbuf, 0, n, reinterpret_cast<jbyte*>(rx.data()));
      // strikers-quest r39b: one of this headset's own messages back (the sender id at byte 2, the
      // header handle() reads): the link carries ours both ways. The friend's messages don't count;
      // they say nothing about whether ours get out.
      if (n >= 10) {
        const uint64_t sender = rd64(rx.data() + 2);
        if (sender == myId) unechoedSince = 0;
        // r50: a ping answered at once; a pong for this headset timed at once (any build's).
        if (sender != myId && rx[1] == kProtocol) {
          if (rx[0] == kPingType && n >= 14) {
            std::vector<uint8_t> pong{kPongType, kProtocol};
            wr64(pong, myId);
            wr64(pong, sender);
            wr32(pong, rd32(rx.data() + 10));
            send_now(pong);
          } else if (rx[0] == kPongType && n >= 22 && rd64(rx.data() + 10) == myId) {
            const uint32_t rtt = static_cast<uint32_t>(now_ms()) - rd32(rx.data() + 18);
            if (rtt < 5000) {
              std::lock_guard<std::mutex> lock(sh.m);
              sh.rtts.push_back(rtt);
              if (sh.rtts.size() > 32) sh.rtts.pop_front();
            }
          }
        }
      }
      std::lock_guard<std::mutex> lock(sh.m);
      if (sh.activeRoom != room) continue;  // r39b: the old room's, after the game moved on
      sh.in.emplace_back(rx.begin(), rx.begin() + n);
      if (sh.in.size() > 8192) sh.in.pop_front();
    }

    // r50: a ping a second while no online match runs (the start's input delay comes from these).
    if (handle >= 0 && sh.pinging.load() && now_ms() - lastPingMs >= 1000) {
      std::vector<uint8_t> ping{kPingType, kProtocol};
      wr64(ping, myId);
      lastPingMs = now_ms();
      wr32(ping, static_cast<uint32_t>(lastPingMs));
      send_now(ping);
    }

    // The server echoes everything we send: none of ours back for 5 s is a dead link.
    // strikers-quest r39b: timed from the oldest send still unanswered, not from the last message
    // in: after any quiet spell (headset off, both loading) the first send found that one 5 s old
    // and dropped a healthy link before its echo could come back.
    if (handle >= 0 && unechoedSince != 0 && now_ms() - unechoedSince > 5000) {
      drop("no echo for 5 s");
      retryAt = now_ms() + 200;
    }
  }
  drop(nullptr);
}

// ---------------------------------------------------------------------------------------------
// Messages: [type][protocol][sender id: 8 bytes] then the body, little-endian.
// ---------------------------------------------------------------------------------------------

enum Type : uint8_t { kHello = 1, kStart = 2, kInput = 3, kHash = 4, kPing = 5, kPong = 6, kCoop = 7 };
// r76: kCoop's first byte, franchise co-op: a status each second ([maker][has][in hub][id u32][version u32]),
// a piece of a career file ([transfer u32][piece u32][pieces u32][total u32][id u32][version u32][size u32]
// [bytes]), its receipt ([transfer u32]), a request for the friend's career, a match start ([id u32]
// [version u32][watch game i32][serial u32]).
enum CoopOp : uint8_t { kCoopStatus = 1, kCoopPiece = 2, kCoopGot = 3, kCoopAsk = 4, kCoopPlay = 5 };
constexpr size_t kCoopPieceBytes = 3500;  // a piece + headers under the room server's 4096

struct Writer {
  std::vector<uint8_t> v;
  void u8(uint8_t x) { v.push_back(x); }
  void u32(uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
  }
  void u64(uint64_t x) {
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
  }
  void bytes(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    v.insert(v.end(), b, b + n);
  }
};

struct Reader {
  const std::vector<uint8_t>& v;
  size_t at = 0;
  bool ok = true;
  bool need(size_t n) {
    if (at + n > v.size()) ok = false;
    return ok;
  }
  uint8_t u8() { return need(1) ? v[at++] : 0; }
  uint32_t u32() {
    if (!need(4)) return 0;
    uint32_t x = 0;
    for (int i = 0; i < 4; ++i) x |= static_cast<uint32_t>(v[at++]) << (8 * i);
    return x;
  }
  uint64_t u64() {
    if (!need(8)) return 0;
    uint64_t x = 0;
    for (int i = 0; i < 8; ++i) x |= static_cast<uint64_t>(v[at++]) << (8 * i);
    return x;
  }
  void bytes(void* p, size_t n) {
    if (need(n)) {
      std::memcpy(p, v.data() + at, n);
      at += n;
    }
  }
};

// ---------------------------------------------------------------------------------------------
// The room and the match (game thread)
// ---------------------------------------------------------------------------------------------

constexpr uint32_t kRing = 512;  // frames of input kept either way (more than any delay)
constexpr uint32_t kRttSamples = 9;  // r39b: round trips kept for the median

struct Choice {
  int team = 0, sidekick = 0, stadium = 0;
  uint8_t options[64] = {};
  int optionsSize = 0;
  // r63: the game mode (0 Friendly, else a cup: co-op) and how far that cup is (games played).
  uint8_t mode = 0;
  uint32_t progress = 0;
  uint8_t maker = 0;  // r64: 1 = this headset made the room (Host an online match)
  char guests[2][24] = {};  // r67: this side's guest captain and sidekick (pack names, "" none)
  uint32_t statMask[2] = {};  // r67b: their own stats (bits = keys set), as this headset read them
  float stats[2][28] = {};
  // r79 (protocol 12): this side's Grudge Match co-captain (include/port/lineup.h): its class (-1 none), its
  // guest (pack name, "" none) and that guest's own stats
  int coCaptain = -1;
  char coGuest[24] = {};
  uint32_t coMask = 0;
  float coStats[28] = {};
};

// r63, co-op Cup: this headset's cup at its match start (Game.cpp port_net_pack_cup) and its team's side.
std::vector<uint8_t> g_localCup;
int g_localCoopSide = 0;
// r76, franchise co-op: this headset's whole match at its start (both sides, from its career), sent by
// the host in the start's 'F' tail.
PortNetMatch g_localFull{};

// r76: franchise co-op between the matches (game thread): what each headset has, the career files
// going over, and the host's match starts.
struct Coop {
  bool mineHas = false, mineInHub = false;
  uint32_t mineId = 0, mineVersion = 0;
  bool friendHeard = false, friendHas = false, friendInHub = false, friendMaker = false;
  uint32_t friendId = 0, friendVersion = 0;
  uint64_t lastStatusMs = 0;
  // the career going to the friend: resent whole until they say they have it
  std::string outText;
  uint32_t outTransfer = 0, outId = 0, outVersion = 0;
  bool outGot = true;
  int outTries = 0;
  uint64_t outLastMs = 0;
  // one coming in, piece by piece
  uint32_t inTransfer = 0, inPieces = 0, inTotal = 0, inId = 0, inVersion = 0, inDone = 0;
  std::vector<std::string> inParts;
  std::vector<bool> inHave;
  bool taken = true;  // the last whole one handed to the franchise
  std::string received;
  uint32_t receivedId = 0, receivedVersion = 0;
  std::string handedOut;  // what PortNetCoopTakeCareer's pointer shows
  bool asked = false;     // the friend asked for this headset's career
  // the host's match start
  bool playIn = false;
  uint32_t playId = 0, playVersion = 0, playSerial = 0, lastPlaySerial = 0;
  int playWatch = -1;
  bool playOut = false;
  uint32_t outPlayId = 0, outPlayVersion = 0, outPlaySerial = 0;
  int outPlayWatch = -1;
  uint64_t outPlaySinceMs = 0, outPlayLastMs = 0;
  std::string friendName;  // their team name, from their hello
};
Coop co;

struct State {
  bool threadStarted = false;
  bool netBroken = false;  // no Java bridge: offline for good
  std::string room;        // the room the state below belongs to
  std::string pickedRoom;  // r39b: the last pick seen (a pick that has to wait is logged once)
  uint64_t myId = 0;
  bool wasConnected = false;
  // The friend.
  uint64_t peerId = 0;
  uint64_t peerSeenMs = 0;
  uint64_t peerHelloMs = 0;  // r39b: their last hello, so their ready bit is a current one
  bool peerReady = false;
  // r39b: their first "not ready" hello after a ready one while this headset waited for them, 0 = none.
  uint64_t peerLeftHoldMs = 0;
  Choice peerChoice;
  uint32_t rttMs = 200;
  uint32_t rttSamples[kRttSamples] = {};  // r39b: the friend's last round trips; rttMs is their median
  uint32_t rttCount = 0, rttNext = 0;
  uint64_t lastHelloMs = 0, lastPingMs = 0;
  // r39b: a headset in the room on another protocol (the match start says so when it goes offline).
  uint64_t otherBuildMs = 0;
  uint8_t otherBuildProtocol = 0;
  bool otherBuildLogged = false;
  // strikers-quest r43: a headset in the room on the other Version: its id (nothing it sends is
  // taken), when its hello last came, and its Version; the match start says so like the above.
  uint64_t otherEditionId = 0;
  uint64_t otherEditionMs = 0;
  uint8_t otherEdition = 0;
  bool otherEditionLogged = false;
  // This headset at the match start (the loading screen).
  bool localReady = false;
  uint64_t readySinceMs = 0, lastHoldMs = 0;
  bool gaveUp = false;
  bool cancelWait = false;  // r47: the player held B to stop waiting for the friend (xr_quest.cpp)
  Choice localChoice;
  // The online match.
  bool startPending = false, online = false, isHost = false;
  std::vector<uint8_t> startMsg;
  uint64_t lastStartSendMs = 0, onlineSinceMs = 0;
  uint32_t seed = 1;
  int delay = 8;
  PortNetMatch match{};
  bool startReported = false;
  uint32_t frame = 0;      // the next game frame to run
  bool stepped = false;    // r39b: step() handed out `frame`; PortNetCommitFrame uses it up
  uint32_t localNext = 0;  // the next frame this headset's input goes to
  NetPad localBuf[kRing];
  NetPad remoteBuf[kRing];
  uint32_t remoteTag[kRing] = {};  // frame + 1 of what remoteBuf holds, 0 = nothing
  uint64_t lastRemoteMs = 0, lastSendMs = 0, stallSinceMs = 0;
  uint64_t lastProgressMs = 0;  // r39: last time a game frame ran online (the watchdog); r39b: or ended
  uint32_t myHash[kRing] = {}, myHashTag[kRing] = {};
  uint32_t peerHash[kRing] = {}, peerHashTag[kRing] = {};
  uint32_t desyncs = 0, checks = 0;
  int frontEndFrames = 0;
  uint32_t stalls = 0;
  // strikers-quest r45: a tournament match: its id (taken when the match goes online) and the last
  // score Game.cpp reported (home, away).
  std::string tourneyMid;
  int score[2] = {0, 0};
  bool scoreSeen = false;
};
State st;

// strikers-quest r45: a tournament match's result is waiting in tourney-result.txt (StrikersActivity
// takes the flag and brings the launcher panel up, which reports it).
std::atomic<bool> g_tourneyPending{false};

void tourney_result(const char* how) {
  const int mine = st.isHost ? 0 : 1;  // home = the host
  const char* home = std::getenv("STRIKERS_HOME");
  const std::string path =
      std::string(home != nullptr && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/tourney-result.txt";
  if (FILE* f = std::fopen(path.c_str(), "w")) {
    // The room code too: the hub refuses a report for a match it has replayed under a new code since.
    std::fprintf(f, "mid=%s\ncode=%s\nmy=%d\ntheir=%d\nhow=%s\n", st.tourneyMid.c_str(), st.room.c_str(),
                 st.score[mine], st.score[1 - mine], how);
    std::fclose(f);
  }
  NLOG("net: tournament match %s over: %d-%d (%s)", st.tourneyMid.c_str(), st.score[mine], st.score[1 - mine], how);
  g_tourneyPending = true;
}

void send(std::vector<uint8_t> msg) {
  std::lock_guard<std::mutex> lock(sh.m);
  sh.out.emplace_back(std::move(msg));
  if (sh.out.size() > 2048) sh.out.pop_front();
}

Writer header(Type t) {
  Writer w;
  w.u8(t);
  w.u8(kProtocol);
  w.u64(st.myId);
  return w;
}

void write_choice(Writer& w, const Choice& c) {
  w.u32(static_cast<uint32_t>(c.team));
  w.u32(static_cast<uint32_t>(c.sidekick));
  w.u32(static_cast<uint32_t>(c.stadium));
  w.u8(static_cast<uint8_t>(c.optionsSize));
  w.bytes(c.options, static_cast<size_t>(c.optionsSize));
  w.u8(c.mode);  // r63
  w.u32(c.progress);
  w.u8(c.maker);  // r64
  for (int g = 0; g < 2; ++g) {  // r67
    const size_t n = strnlen(c.guests[g], sizeof(c.guests[g]) - 1);
    w.u8(static_cast<uint8_t>(n));
    w.bytes(c.guests[g], n);
  }
  for (int g = 0; g < 2; ++g) {  // r67b: the set keys' values, in key order
    const uint32_t mask = c.statMask[g] & ((1u << 28) - 1u);
    w.u32(mask);
    for (int i = 0; i < 28; ++i) {
      if ((mask & (1u << i)) == 0) continue;
      uint32_t bits;
      std::memcpy(&bits, &c.stats[g][i], 4);
      w.u32(bits);
    }
  }
  // r79: the co-captain: [class + 1 (0 none)][guest name][stat mask][the set keys' values]
  w.u8(static_cast<uint8_t>(c.coCaptain >= 0 && c.coCaptain < 255 ? c.coCaptain + 1 : 0));
  const size_t coN = strnlen(c.coGuest, sizeof(c.coGuest) - 1);
  w.u8(static_cast<uint8_t>(coN));
  w.bytes(c.coGuest, coN);
  const uint32_t coMask = c.coMask & ((1u << 28) - 1u);
  w.u32(coMask);
  for (int i = 0; i < 28; ++i) {
    if ((coMask & (1u << i)) == 0) continue;
    uint32_t bits;
    std::memcpy(&bits, &c.coStats[i], 4);
    w.u32(bits);
  }
}

// r79: a Friendly's co-captains, home from `home`'s pick and away from `away`'s
void fill_co_captains(PortNetMatch& m, const Choice& home, const Choice& away) {
  const Choice* side[2] = {&home, &away};
  for (int s = 0; s < 2; ++s) {
    m.coCaptains[s] = side[s]->coCaptain;
    std::memcpy(m.coGuests[s], side[s]->coGuest, sizeof(m.coGuests[s]));
    m.coGuests[s][sizeof(m.coGuests[s]) - 1] = '\0';
    m.coStatMask[s] = side[s]->coMask;
    std::memcpy(m.coStats[s], side[s]->coStats, sizeof(m.coStats[s]));
  }
}

Choice read_choice(Reader& r) {
  Choice c;
  c.team = static_cast<int>(r.u32());
  c.sidekick = static_cast<int>(r.u32());
  c.stadium = static_cast<int>(r.u32());
  c.optionsSize = std::min<int>(r.u8(), 64);
  r.bytes(c.options, static_cast<size_t>(c.optionsSize));
  c.mode = r.u8();  // r63
  c.progress = r.u32();
  c.maker = r.u8();  // r64
  for (int g = 0; g < 2; ++g) {  // r67
    const size_t n = std::min<size_t>(r.u8(), sizeof(c.guests[g]) - 1);
    r.bytes(c.guests[g], n);
    c.guests[g][n] = '\0';
  }
  for (int g = 0; g < 2; ++g) {  // r67b
    c.statMask[g] = r.u32() & ((1u << 28) - 1u);
    for (int i = 0; i < 28; ++i) {
      if ((c.statMask[g] & (1u << i)) == 0) continue;
      const uint32_t bits = r.u32();
      std::memcpy(&c.stats[g][i], &bits, 4);
    }
  }
  {  // r79: the co-captain
    const uint8_t cls = r.u8();
    c.coCaptain = cls == 0 ? -1 : static_cast<int>(cls) - 1;
    const size_t n = std::min<size_t>(r.u8(), sizeof(c.coGuest) - 1);
    r.bytes(c.coGuest, n);
    c.coGuest[n] = '\0';
    c.coMask = r.u32() & ((1u << 28) - 1u);
    for (int i = 0; i < 28; ++i) {
      if ((c.coMask & (1u << i)) == 0) continue;
      const uint32_t bits = r.u32();
      std::memcpy(&c.coStats[i], &bits, 4);
    }
  }
  return c;
}

// r63: a Friendly pairs only with a Friendly, a cup only with a cup (co-op). r76: a co-op franchise match
// only with one.
int kind_of(uint8_t mode) { return mode == 0 ? 0 : (mode == PORT_NET_MODE_FRANCHISE ? 2 : 1); }
bool same_kind(const Choice& a, const Choice& b) { return kind_of(a.mode) == kind_of(b.mode); }

void send_hello() {
  Writer w = header(kHello);
  const bool ready = st.localReady && !st.online && !st.startPending;
  w.u8(ready ? 1 : 0);
  w.u8(local_edition());  // r43
  write_choice(w, st.localChoice);
  // r61: who this is, for the friend's home ground (their online rival): 'H', this headset's player
  // id and team name. After everything else, so a build without it reads the hello as before.
  const std::string& id = xrhome::my_id();
  const std::string& name = xrhome::my_name();
  w.u8('H');
  w.u8(static_cast<uint8_t>(std::min<size_t>(id.size(), 32)));
  w.bytes(id.data(), std::min<size_t>(id.size(), 32));
  w.u8(static_cast<uint8_t>(std::min<size_t>(name.size(), 24)));
  w.bytes(name.data(), std::min<size_t>(name.size(), 24));
  send(std::move(w.v));
  st.lastHelloMs = now_ms();
}

void send_inputs() {
  if (st.localNext <= static_cast<uint32_t>(st.delay)) return;
  // Everything the friend may still need: they are at most `delay` + 1 frames behind this headset
  // (each side fills its own input up to its frame + delay before it waits for the other's).
  // r39: was `delay`, one frame short: one lost message then stalled both headsets for good.
  const uint32_t d = static_cast<uint32_t>(st.delay);
  uint32_t first = std::max<uint32_t>(d, st.frame > d + 1 ? st.frame - d - 1 : 0);
  if (st.localNext - first > 255) first = st.localNext - 255;
  Writer w = header(kInput);
  w.u32(first);
  w.u8(static_cast<uint8_t>(st.localNext - first));
  for (uint32_t f = first; f < st.localNext; ++f) w.bytes(&st.localBuf[f % kRing], sizeof(NetPad));
  // r44: and the voice frames since the last one (the room server takes messages up to 4096 bytes).
  xrvoice::append_outgoing(w.v, 3900);
  send(std::move(w.v));
  st.lastSendMs = now_ms();
}

// r39b: the friend was heard from within the last `ms`.
bool peer_heard(uint64_t ms) { return st.peerId != 0 && now_ms() - st.peerSeenMs < ms; }
bool peer_present() { return peer_heard(5000); }


uint32_t frame_seed(uint32_t seed, uint32_t frame) {
  uint32_t x = seed ^ (frame * 0x9E3779B9u);
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x != 0 ? x : 1;
}

void end_online(const char* why) {
  if (!st.online && !st.startPending) return;
  NLOG("net: online match over (%s) after %u frames, %u/%u state checks differed, %u stalls", why, st.frame,
       st.desyncs, st.checks, st.stalls);
  // r45: a tournament match that really ran (10 s or more) reports how it ended: played to the end
  // (back in the menus, both headsets on the same frame) or the friend dropped out.
  if (!st.tourneyMid.empty() && st.online && st.frame > 600 && st.scoreSeen) {
    tourney_result(std::strcmp(why, "back in the menus") == 0 ? "played" : "left");
  }
  st.tourneyMid.clear();
  st.online = false;
  st.startPending = false;
  st.stepped = false;
  st.localReady = false;
  st.startReported = false;
  st.frontEndFrames = 0;
  std::memset(st.remoteTag, 0, sizeof(st.remoteTag));
  std::memset(st.myHashTag, 0, sizeof(st.myHashTag));
  std::memset(st.peerHashTag, 0, sizeof(st.peerHashTag));
  PortNetSetFixedStep(0);
  xrvoice::match_end();  // r44
}

void compare_hash(uint32_t f) {
  const uint32_t i = f % kRing;
  if (st.myHashTag[i] != f + 1 || st.peerHashTag[i] != f + 1) return;
  ++st.checks;
  if (st.myHash[i] != st.peerHash[i]) {
    ++st.desyncs;
    if (st.desyncs <= 5 || st.desyncs % 20 == 0) {
      NLOG("net: the matches differ at frame %u (%08x here, %08x there), %u times so far", f, st.myHash[i],
           st.peerHash[i], st.desyncs);
    }
  }
}

// r76, franchise co-op: the start's 'F' tail, the host's whole match: [side both play on + 1 (0 = none,
// watching)] [home team, sidekick, away team, sidekick, stadium: u32 each] then per side and role the
// guest's pack name ([size u8][bytes]) and its stats ([mask u32][the set values]).
void write_full(Writer& w, const PortNetMatch& m) {
  w.u8('F');
  // r100: 3 = an online league's game between two people (PORT_NET_COOP_VERSUS: each on their own club)
  w.u8(static_cast<uint8_t>(m.coopSide == 0 || m.coopSide == 1 ? m.coopSide + 1 : (m.coopSide == PORT_NET_COOP_VERSUS ? 3 : 0)));
  for (int s = 0; s < 2; ++s) {
    w.u32(static_cast<uint32_t>(m.teams[s]));
    w.u32(static_cast<uint32_t>(m.sidekicks[s]));
  }
  w.u32(static_cast<uint32_t>(m.stadium));
  for (int s = 0; s < 2; ++s) {
    for (int role = 0; role < 2; ++role) {
      const size_t n = strnlen(m.guests[s][role], sizeof(m.guests[s][role]) - 1);
      w.u8(static_cast<uint8_t>(n));
      w.bytes(m.guests[s][role], n);
      const uint32_t mask = m.guestStatMask[s][role] & ((1u << 28) - 1u);
      w.u32(mask);
      for (int i = 0; i < 28; ++i) {
        if ((mask & (1u << i)) == 0) continue;
        uint32_t bits;
        std::memcpy(&bits, &m.guestStats[s][role][i], 4);
        w.u32(bits);
      }
    }
  }
  // r78 (protocol 11): each side's co-captain: [class + 1 u8 (0 none)][guest: size u8, bytes][mask u32][values]
  for (int s = 0; s < 2; ++s) {
    w.u8(static_cast<uint8_t>(m.coCaptains[s] >= 0 && m.coCaptains[s] < 255 ? m.coCaptains[s] + 1 : 0));
    const size_t n = strnlen(m.coGuests[s], sizeof(m.coGuests[s]) - 1);
    w.u8(static_cast<uint8_t>(n));
    w.bytes(m.coGuests[s], n);
    const uint32_t mask = m.coStatMask[s] & ((1u << 28) - 1u);
    w.u32(mask);
    for (int i = 0; i < 28; ++i) {
      if ((mask & (1u << i)) == 0) continue;
      uint32_t bits;
      std::memcpy(&bits, &m.coStats[s][i], 4);
      w.u32(bits);
    }
  }
}

bool read_full(Reader& r, PortNetMatch* m) {
  const uint8_t side = r.u8();
  m->coopSide = side == 1 || side == 2 ? side - 1 : (side == 3 ? PORT_NET_COOP_VERSUS : -1);
  for (int s = 0; s < 2; ++s) {
    m->teams[s] = static_cast<int>(r.u32());
    m->sidekicks[s] = static_cast<int>(r.u32());
  }
  m->stadium = static_cast<int>(r.u32());
  for (int s = 0; s < 2; ++s) {
    for (int role = 0; role < 2; ++role) {
      const size_t n = std::min<size_t>(r.u8(), sizeof(m->guests[s][role]) - 1);
      std::memset(m->guests[s][role], 0, sizeof(m->guests[s][role]));
      r.bytes(m->guests[s][role], n);
      m->guestStatMask[s][role] = r.u32() & ((1u << 28) - 1u);
      for (int i = 0; i < 28; ++i) {
        if ((m->guestStatMask[s][role] & (1u << i)) == 0) continue;
        const uint32_t bits = r.u32();
        std::memcpy(&m->guestStats[s][role][i], &bits, 4);
      }
    }
  }
  for (int s = 0; s < 2; ++s) {  // r78: the co-captains
    const uint8_t cls = r.u8();
    m->coCaptains[s] = cls == 0 ? -1 : static_cast<int>(cls) - 1;
    const size_t n = std::min<size_t>(r.u8(), sizeof(m->coGuests[s]) - 1);
    std::memset(m->coGuests[s], 0, sizeof(m->coGuests[s]));
    r.bytes(m->coGuests[s], n);
    m->coStatMask[s] = r.u32() & ((1u << 28) - 1u);
    for (int i = 0; i < 28; ++i) {
      if ((m->coStatMask[s] & (1u << i)) == 0) continue;
      const uint32_t bits = r.u32();
      std::memcpy(&m->coStats[s][i], &bits, 4);
    }
  }
  return r.ok;
}

// The host has both choices: home = the host's pick, away = the guest's, the host's stadium and
// settings, a fresh seed, and an input delay that covers the trip through the broker.
void host_start() {
  st.isHost = true;
  std::random_device rd;
  st.seed = (rd() ^ static_cast<uint32_t>(now_ms())) | 1u;
  // One way is about half the round trip; a frame is 16.7 ms; three frames of slack for jitter and
  // the inputs going out in pairs.
  // strikers-quest r44 (players: input lag): two frames of slack (one for the pairs, one for jitter)
  // and no floor of 5: a 60 ms round trip now plays 4 frames behind instead of 5, a nearby friend 3
  // instead of 5. The stall count in the 5 s status line says if a link needs more.
  st.delay = std::clamp(static_cast<int>((st.rttMs / 2 + 16) / 17) + 2, 3, 20);
  Writer w = header(kStart);
  w.u64(st.myId);
  w.u64(st.peerId);
  w.u32(st.seed);
  w.u8(static_cast<uint8_t>(st.delay));
  w.u8(local_edition());  // r43: the guest checks it too
  write_choice(w, st.localChoice);
  write_choice(w, st.peerChoice);
  // r76: a co-op franchise match is no cup: its 'F' tail instead (below)
  const bool franchise = st.localChoice.mode == PORT_NET_MODE_FRANCHISE;
  const bool coop = st.localChoice.mode != 0 && !franchise;
  if (coop) {
    // r63, co-op Cup: 'C', the side both play on, the host's cup (the room server takes up to 4096
    // bytes a message; the biggest cup, a Super Bowser Cup with its knockout, is about 2.8 KB).
    w.u8('C');
    w.u8(static_cast<uint8_t>(g_localCoopSide));
    w.u32(static_cast<uint32_t>(g_localCup.size()));
    w.bytes(g_localCup.data(), g_localCup.size());
  }
  if (franchise) write_full(w, g_localFull);  // r76
  st.startMsg = w.v;
  st.match = PortNetMatch{};
  st.match.teams[0] = st.localChoice.team;
  st.match.teams[1] = st.peerChoice.team;
  st.match.sidekicks[0] = st.localChoice.sidekick;
  st.match.sidekicks[1] = st.peerChoice.sidekick;
  st.match.stadium = st.localChoice.stadium;
  st.match.optionsSize = st.localChoice.optionsSize;
  std::memcpy(st.match.options, st.localChoice.options, sizeof(st.match.options));
  st.match.mode = st.localChoice.mode;
  std::memcpy(st.match.guests[0], st.localChoice.guests, sizeof(st.match.guests[0]));  // r67
  std::memcpy(st.match.guests[1], st.peerChoice.guests, sizeof(st.match.guests[1]));
  for (int g = 0; g < 2; ++g) {  // r67b
    st.match.guestStatMask[0][g] = st.localChoice.statMask[g];
    st.match.guestStatMask[1][g] = st.peerChoice.statMask[g];
    std::memcpy(st.match.guestStats[0][g], st.localChoice.stats[g], sizeof(st.match.guestStats[0][g]));
    std::memcpy(st.match.guestStats[1][g], st.peerChoice.stats[g], sizeof(st.match.guestStats[1][g]));
  }
  fill_co_captains(st.match, st.localChoice, st.peerChoice);  // r79 (a franchise start's come from 'F')
  if (coop) {
    st.match.coopSide = g_localCoopSide;
    st.match.cupSize = static_cast<int>(std::min(g_localCup.size(), sizeof(st.match.cup)));
    std::memcpy(st.match.cup, g_localCup.data(), static_cast<size_t>(st.match.cupSize));
  }
  if (franchise) {
    // r76: the whole match from this headset's career (both sides), the host's settings
    const PortNetMatch options = st.match;
    st.match = g_localFull;
    st.match.mode = PORT_NET_MODE_FRANCHISE;
    st.match.optionsSize = options.optionsSize;
    std::memcpy(st.match.options, options.options, sizeof(st.match.options));
    st.match.cupSize = 0;
  }
  st.frame = 0;  // the friend's first inputs may arrive before this headset's frame 0
  std::memset(st.remoteTag, 0, sizeof(st.remoteTag));
  st.lastRemoteMs = 0;
  st.startPending = true;
  send(st.startMsg);
  st.lastStartSendMs = now_ms();
  if (franchise && g_localFull.coopSide == PORT_NET_COOP_VERSUS) {
    NLOG("net: starting the online league game as host: you the home club, the other person the away club "
         "(round trip %u ms, input delay %d frames)", st.rttMs, st.delay);
  } else if (franchise) {
    NLOG("net: starting the co-op franchise match as host: you the captain and a sidekick, the friend the other "
         "two (side %d, round trip %u ms, input delay %d frames)", g_localFull.coopSide, st.rttMs, st.delay);
  } else if (coop) {
    NLOG("net: starting the online co-op cup match as host: your cup, you the captain and a sidekick, the "
         "friend the other two (round trip %u ms, input delay %d frames, cup %zu bytes)", st.rttMs, st.delay,
         g_localCup.size());
  } else {
    NLOG("net: starting the online match as home (round trip %u ms, input delay %d frames)", st.rttMs, st.delay);
  }
}

// r63: who hosts once both wait at the match start: a Friendly, the lower id; a co-op cup, the headset
// whose cup is further along (its cup is the one played on), the lower id on a tie.
bool i_host() {
  if (st.localChoice.mode != 0) {
    // r64: co-op Cup: whoever made the room (Host an online match) hosts: their cup, the captain.
    // Quick match (nobody made it): the cup further along, then the lower id.
    if (st.localChoice.maker != st.peerChoice.maker) return st.localChoice.maker != 0;
    if (st.localChoice.progress != st.peerChoice.progress) return st.localChoice.progress > st.peerChoice.progress;
  }
  return st.myId < st.peerId;
}

// ---------------------------------------------------------------------------------------------
// r76, franchise co-op between the matches (include/port/xr_net.h PortNetCoop*)
// ---------------------------------------------------------------------------------------------

bool made_room() {
  std::lock_guard<std::mutex> lock(sh.m);
  return sh.roomMaker;
}

// Who runs the co-op career: the headset that made the room; else (both or neither did) the lower id.
// The same rule as i_host's for the match start, so the career's host is the lockstep's host (pad 0:
// the captain and a sidekick).
bool coop_host() {
  const bool mine = made_room();
  if (co.friendHeard && mine != co.friendMaker) return mine;
  return st.peerId == 0 || st.myId < st.peerId;
}

void send_coop_status() {
  Writer w = header(kCoop);
  w.u8(kCoopStatus);
  w.u8(made_room() ? 1 : 0);
  w.u8(co.mineHas ? 1 : 0);
  w.u8(co.mineInHub ? 1 : 0);
  w.u32(co.mineId);
  w.u32(co.mineVersion);
  send(std::move(w.v));
  co.lastStatusMs = now_ms();
}

// The career going over, all its pieces (the friend says when it has them all).
void send_career_pieces() {
  const size_t total = co.outText.size();
  const uint32_t pieces = static_cast<uint32_t>(std::max<size_t>(1, (total + kCoopPieceBytes - 1) / kCoopPieceBytes));
  for (uint32_t i = 0; i < pieces; ++i) {
    const size_t at = std::min(total, static_cast<size_t>(i) * kCoopPieceBytes);
    const size_t n = std::min(kCoopPieceBytes, total - at);
    Writer w = header(kCoop);
    w.u8(kCoopPiece);
    w.u32(co.outTransfer);
    w.u32(i);
    w.u32(pieces);
    w.u32(static_cast<uint32_t>(total));
    w.u32(co.outId);
    w.u32(co.outVersion);
    w.u32(static_cast<uint32_t>(n));
    w.bytes(co.outText.data() + at, n);
    send(std::move(w.v));
  }
  co.outLastMs = now_ms();
  ++co.outTries;
}

void send_coop_got(uint32_t transfer) {
  Writer w = header(kCoop);
  w.u8(kCoopGot);
  w.u32(transfer);
  send(std::move(w.v));
}

void send_coop_play() {
  Writer w = header(kCoop);
  w.u8(kCoopPlay);
  w.u32(co.outPlayId);
  w.u32(co.outPlayVersion);
  w.u32(static_cast<uint32_t>(co.outPlayWatch));
  w.u32(co.outPlaySerial);
  send(std::move(w.v));
  co.outPlayLastMs = now_ms();
}

void handle_coop(Reader& r, const std::vector<uint8_t>& msg) {
  const uint8_t op = r.u8();
  if (!r.ok) return;
  switch (op) {
  case kCoopStatus: {
    const bool maker = r.u8() != 0;
    const bool has = r.u8() != 0;
    const bool inHub = r.u8() != 0;
    const uint32_t id = r.u32(), version = r.u32();
    if (!r.ok) return;
    if (!co.friendHeard || has != co.friendHas || inHub != co.friendInHub || id != co.friendId ||
        version != co.friendVersion) {
      NLOG("net: co-op: the friend %s a co-op career%s (career %u, version %u)%s", has ? "has" : "has no",
           inHub ? ", open" : "", id, version, maker ? ", they made the room" : "");
    }
    co.friendMaker = maker;
    co.friendHas = has;
    co.friendInHub = inHub;
    co.friendId = id;
    co.friendVersion = version;
    co.friendHeard = true;
    break;
  }
  case kCoopPiece: {
    const uint32_t transfer = r.u32(), piece = r.u32(), pieces = r.u32(), total = r.u32(), id = r.u32(),
                   version = r.u32(), n = r.u32();
    if (!r.ok || pieces == 0 || pieces > 200 || piece >= pieces || total > 700000 || n > kCoopPieceBytes || !r.need(n))
      return;
    if (transfer == co.inDone) {  // a resend of one already whole here: say so again
      send_coop_got(transfer);
      return;
    }
    if (transfer != co.inTransfer || co.inPieces != pieces) {
      co.inTransfer = transfer;
      co.inPieces = pieces;
      co.inTotal = total;
      co.inId = id;
      co.inVersion = version;
      co.inParts.assign(pieces, std::string());
      co.inHave.assign(pieces, false);
    }
    if (!co.inHave[piece]) {
      co.inParts[piece].assign(reinterpret_cast<const char*>(msg.data() + r.at), n);
      co.inHave[piece] = true;
    }
    if (std::all_of(co.inHave.begin(), co.inHave.end(), [](bool b) { return b; })) {
      std::string whole;
      whole.reserve(co.inTotal);
      for (const std::string& p : co.inParts) whole += p;
      if (whole.size() == co.inTotal) {
        co.received = std::move(whole);
        co.receivedId = co.inId;
        co.receivedVersion = co.inVersion;
        co.taken = false;
        co.inDone = transfer;
        NLOG("net: co-op: the friend's career came in, %u bytes (career %u, version %u)", co.inTotal, co.inId,
             co.inVersion);
        send_coop_got(transfer);
      } else {
        NLOG("net: co-op: a career came in the wrong size (%zu of %u bytes), dropped", whole.size(), co.inTotal);
      }
      co.inParts.clear();
      co.inHave.clear();
      co.inTransfer = 0;
    }
    break;
  }
  case kCoopGot: {
    const uint32_t transfer = r.u32();
    if (r.ok && transfer == co.outTransfer && !co.outGot) {
      co.outGot = true;
      NLOG("net: co-op: the friend has the career (version %u, %zu bytes, %d sends)", co.outVersion, co.outText.size(),
           co.outTries);
    }
    break;
  }
  case kCoopAsk:
    co.asked = true;
    NLOG("net: co-op: the friend asks for this headset's career");
    break;
  case kCoopPlay: {
    const uint32_t id = r.u32(), version = r.u32();
    const int watch = static_cast<int>(r.u32());
    const uint32_t serial = r.u32();
    if (!r.ok || serial == co.lastPlaySerial) return;
    co.lastPlaySerial = serial;
    co.playIn = true;
    co.playId = id;
    co.playVersion = version;
    co.playWatch = watch;
    co.playSerial = serial;
    NLOG("net: co-op: the friend starts a match (career %u, version %u, %s)", id, version,
         watch >= 0 ? "watching a game" : "your club's");
    break;
  }
  default:
    break;
  }
}

void handle(const std::vector<uint8_t>& msg) {
  Reader r{msg};
  const uint8_t type = r.u8();
  const uint8_t proto = r.u8();
  const uint64_t sender = r.u64();
  if (!r.ok || sender == st.myId) return;
  const uint64_t now = now_ms();
  if (proto != kProtocol) {
    static uint64_t warned = 0;
    // r39b: both numbers, and remembered for the match start, which says why it goes offline.
    if (warned != sender) {
      NLOG("net: someone in the room runs another version of the game (online protocol %u there, %u here); "
           "the two cannot play each other", static_cast<unsigned>(proto), static_cast<unsigned>(kProtocol));
    }
    warned = sender;
    st.otherBuildMs = now;
    st.otherBuildProtocol = proto;
    return;
  }
  // strikers-quest r43: a headset on the other Version is never the friend, on either side, so
  // neither hosts a match with it. Its hello says which ([ready][edition] first); nothing else it
  // sends is taken. (A Version is fixed for a game's run, and a new run is a new sender id.)
  if (type == kHello) {
    Reader peek = r;
    peek.u8();  // ready
    const uint8_t edition = peek.u8();
    if (peek.ok && edition != local_edition()) {
      if (st.otherEditionId != sender) {
        NLOG("net: someone in the room plays %s, this headset %s; the two cannot play each other",
             edition_name(edition), edition_name(local_edition()));
      }
      st.otherEditionId = sender;
      st.otherEditionMs = now;
      st.otherEdition = edition;
      if (st.peerId == sender) {  // taken for the friend from a message before its hello
        st.peerId = 0;
        st.peerReady = false;
        st.peerLeftHoldMs = 0;
      }
      return;
    }
  }
  if (st.otherEditionId != 0 && sender == st.otherEditionId) return;
  // The friend: whoever else is here; a newcomer replaces one gone quiet.
  if (st.peerId != sender) {
    if (peer_present()) return;  // a third headset: ignored
    st.peerId = sender;
    st.peerReady = false;
    st.peerLeftHoldMs = 0;
    st.rttCount = st.rttNext = 0;  // r39b: this friend's round trips only
    st.rttMs = 200;
    co.friendHeard = false;  // r76: their co-op career as they say next
    co.lastStatusMs = 0;     // (and this headset's, to them, now)
    NLOG("net: friend in the room");
  }
  st.peerSeenMs = now;
  switch (type) {
  case kHello: {
    const bool ready = r.u8() != 0;
    r.u8();  // r43: the Version, the same as this headset's (checked above)
    const Choice c = read_choice(r);
    if (!r.ok) return;
    // r61: the friend's player id and team name, when their build sends them (xrhome).
    if (r.at < msg.size() && msg[r.at] == 'H') {
      Reader tail = r;
      tail.u8();
      char id[33] = {}, name[25] = {};
      const size_t idLen = std::min<size_t>(tail.u8(), 32);
      tail.bytes(id, idLen);
      const size_t nameLen = std::min<size_t>(tail.u8(), 24);
      tail.bytes(name, nameLen);
      if (tail.ok && idLen > 0) xrhome::set_peer(std::string(id, idLen), std::string(name, nameLen));
      if (tail.ok) co.friendName.assign(name, nameLen);  // r76
    }
    if (ready && !st.peerReady) {
      NLOG("net: friend started a %s, waiting at the loading screen",
           kind_of(c.mode) == 2 ? "co-op franchise match" : (c.mode != 0 ? "cup match (co-op)" : "Friendly"));
    }
    // r39b: the friend stopped waiting while this headset waits for them (see PortNetHoldMatchStart).
    if (ready) {
      st.peerLeftHoldMs = 0;
    } else if (st.peerReady && st.localReady && !st.online && !st.startPending) {
      st.peerLeftHoldMs = now;
    }
    st.peerReady = ready;
    st.peerHelloMs = now;
    if (ready) st.peerChoice = c;
    break;
  }
  case kPing:  // r50: answered by the network thread the moment it arrives (net_thread)
  case kPong:  // r50: timed there too; the round trips come in through sh.rtts (poll)
    break;
  case kStart: {
    const uint64_t host = r.u64();
    const uint64_t guest = r.u64();
    const uint32_t seed = r.u32();
    const int delay = r.u8();
    const uint8_t edition = r.u8();
    const Choice home = read_choice(r);
    const Choice away = read_choice(r);
    // r63: a co-op cup start: 'C', the side both play on, the host's cup.
    bool coop = false;
    int coopSide = 0;
    std::vector<uint8_t> cup;
    if (r.ok && r.at < msg.size() && msg[r.at] == 'C') {
      r.u8();
      coop = true;
      coopSide = r.u8();
      const uint32_t size = r.u32();
      if (r.ok && size <= sizeof(PortNetMatch::cup) && r.need(size)) {
        cup.assign(msg.begin() + static_cast<long>(r.at), msg.begin() + static_cast<long>(r.at + size));
        r.at += size;
      } else {
        return;
      }
    }
    // r76: a co-op franchise start: 'F', the host's whole match
    bool franchise = false;
    PortNetMatch full{};
    if (r.ok && r.at < msg.size() && msg[r.at] == 'F') {
      r.u8();
      if (!read_full(r, &full)) return;
      franchise = true;
    }
    // r44c: optional, from the test friend only (tools/fakefriend): which side the computer plays.
    const uint8_t cpuPlays = r.ok && r.at < msg.size() ? r.u8() : 0;
    if (!r.ok || guest != st.myId || host != sender) return;
    // r43: never a match on the other Version (the hellos keep the two apart already).
    if (edition != local_edition()) return;
    if (st.online || st.startPending || !st.localReady) return;  // already going, or not at the start
    // r63: a cup only with a cup, a Friendly with a Friendly; r76: a franchise match with one
    if (franchise != (st.localChoice.mode == PORT_NET_MODE_FRANCHISE)) return;
    if (coop != (st.localChoice.mode != 0 && !franchise)) return;
    st.isHost = false;
    st.seed = seed;
    st.delay = std::clamp(delay, 1, 60);
    st.match = PortNetMatch{};
    st.match.teams[0] = home.team;
    st.match.teams[1] = away.team;
    st.match.sidekicks[0] = home.sidekick;
    st.match.sidekicks[1] = away.sidekick;
    st.match.stadium = home.stadium;
    st.match.optionsSize = home.optionsSize;
    std::memcpy(st.match.options, home.options, sizeof(st.match.options));
    st.match.cpuPlays = cpuPlays == 1 ? 1 : 0;  // r44c: only the host's own side
    st.match.mode = home.mode;
    std::memcpy(st.match.guests[0], home.guests, sizeof(st.match.guests[0]));  // r67
    std::memcpy(st.match.guests[1], away.guests, sizeof(st.match.guests[1]));
    for (int g = 0; g < 2; ++g) {  // r67b
      st.match.guestStatMask[0][g] = home.statMask[g];
      st.match.guestStatMask[1][g] = away.statMask[g];
      std::memcpy(st.match.guestStats[0][g], home.stats[g], sizeof(st.match.guestStats[0][g]));
      std::memcpy(st.match.guestStats[1][g], away.stats[g], sizeof(st.match.guestStats[1][g]));
    }
    fill_co_captains(st.match, home, away);  // r79
    if (coop) {
      st.match.coopSide = coopSide == 1 ? 1 : 0;
      st.match.cupSize = static_cast<int>(cup.size());
      std::memcpy(st.match.cup, cup.data(), cup.size());
    }
    if (franchise) {
      // r76: the host's whole match (both sides from its career), its settings
      const PortNetMatch options = st.match;
      st.match = full;
      st.match.mode = PORT_NET_MODE_FRANCHISE;
      st.match.optionsSize = options.optionsSize;
      std::memcpy(st.match.options, options.options, sizeof(st.match.options));
      st.match.cpuPlays = 0;
    }
    st.frame = 0;  // the host's first inputs may arrive before this headset's frame 0
    std::memset(st.remoteTag, 0, sizeof(st.remoteTag));
    st.lastRemoteMs = 0;
    st.startPending = true;
    if (franchise && st.match.coopSide == PORT_NET_COOP_VERSUS) {
      NLOG("net: starting the online league game as guest: you the away club (input delay %d frames)", st.delay);
    } else if (franchise) {
      NLOG("net: starting the co-op franchise match as guest: the friend's career, you two of the sidekicks "
           "(side %d, input delay %d frames)", st.match.coopSide, st.delay);
    } else if (coop) {
      NLOG("net: starting the online co-op cup match as guest: the friend's cup, you two of the sidekicks "
           "(input delay %d frames, cup %zu bytes)", st.delay, cup.size());
    } else {
      NLOG("net: starting the online match as away (input delay %d frames)%s", st.delay,
           st.match.cpuPlays != 0 ? ", the test friend's side played by this headset's computer" : "");
    }
    break;
  }
  case kInput: {
    const uint32_t first = r.u32();
    const uint32_t count = r.u8();
    if (!r.ok || !(st.online || st.startPending)) return;
    for (uint32_t k = 0; k < count; ++k) {
      NetPad p;
      r.bytes(&p, sizeof(p));
      if (!r.ok) return;
      const uint32_t f = first + k;
      if (f < st.frame || f >= st.frame + kRing) continue;
      st.remoteBuf[f % kRing] = p;
      st.remoteTag[f % kRing] = f + 1;
    }
    st.lastRemoteMs = now;
    if (r.at < msg.size()) xrvoice::take_incoming(msg.data() + r.at, msg.size() - r.at);  // r44
    break;
  }
  case kCoop:  // r76, franchise co-op
    handle_coop(r, msg);
    break;
  case kHash: {
    const uint32_t f = r.u32();
    const uint32_t h = r.u32();
    if (!r.ok || !st.online) return;
    st.peerHash[f % kRing] = h;
    st.peerHashTag[f % kRing] = f + 1;
    compare_hash(f);
    break;
  }
  default:
    break;
  }
}

void begin_online() {
  st.startPending = false;
  st.online = true;
  st.onlineSinceMs = now_ms();
  st.lastProgressMs = st.onlineSinceMs;
  st.frame = 0;
  st.stepped = false;
  st.localNext = static_cast<uint32_t>(st.delay);  // the first `delay` frames are neutral on both
  st.stalls = 0;
  st.desyncs = st.checks = 0;
  st.startReported = false;
  st.frontEndFrames = 0;
  PortNetSetFixedStep(1);
  NLOG("net: online, frame 0");
  xrvoice::match_begin();  // r44
  {
    std::lock_guard<std::mutex> lock(sh.m);
    st.tourneyMid = sh.tourney;  // r45
  }
  st.score[0] = st.score[1] = 0;
  st.scoreSeen = false;
  if (!st.tourneyMid.empty()) NLOG("net: tournament match %s", st.tourneyMid.c_str());
}

}  // namespace

void poll() {
  // The room: the launcher's pick (STRIKERS_ROOM), or a later one from nativeSetRoom.
  std::string room;
  {
    std::lock_guard<std::mutex> lock(sh.m);
    if (!sh.roomSet) {
      sh.room = sanitize_room(std::getenv("STRIKERS_ROOM"));
      const char* tourney = std::getenv("STRIKERS_TOURNEY");  // r45
      sh.tourney = tourney != nullptr ? tourney : "";
      const char* maker = std::getenv("STRIKERS_ROOM_MAKER");  // r64
      sh.roomMaker = maker != nullptr && maker[0] == '1';
      sh.roomSet = true;
    }
    room = sh.room;
  }
  // strikers-quest r39b: a new pick waits while an online match runs or starts. Before, only the
  // game waited; the connection switched at once and the match froze on both headsets until its
  // watchdog. Not while this headset only waits at the loading screen: a pick there is the way out
  // of the wait (the switch below ends it, and that Friendly starts offline).
  const bool busy = st.online || st.startPending;
  if (room != st.pickedRoom) {
    st.pickedRoom = room;
    if (busy && room != st.room) {
      NLOG("net: room %s picked, it takes effect after this match", room.empty() ? "(none)" : room.c_str());
    }
  }
  if (room != st.room && !busy) {
    // A different room (or none): forget the old room's friend and any pending start.
    if (!room.empty()) NLOG("net: room %s", room.c_str());
    st.room = room;
    st.peerId = 0;
    st.peerReady = false;
    st.peerLeftHoldMs = 0;
    st.localReady = false;
    st.gaveUp = false;
    st.otherEditionMs = 0;  // r43: the old room's
    {
      // r76: and the old room's co-op talk (this headset's own career is told again by the franchise)
      const Coop mine = co;
      co = Coop{};
      co.mineHas = mine.mineHas;
      co.mineInHub = mine.mineInHub;
      co.mineId = mine.mineId;
      co.mineVersion = mine.mineVersion;
    }
    // r39b: and anything still queued from or for the old room; the connection follows from here
    // (net_thread drops what the old link still brings in after this).
    std::lock_guard<std::mutex> lock(sh.m);
    sh.activeRoom = room;
    sh.in.clear();
    sh.out.clear();
  }
  if (!st.threadStarted) {
    if (st.room.empty() || st.netBroken) return;  // offline: nothing on the network at all
    if (!jni_init()) {
      st.netBroken = true;
      NLOG("net: no Java bridge for the room connection, staying offline");
      return;
    }
    st.threadStarted = true;
    std::random_device rd;
    st.myId = (static_cast<uint64_t>(rd()) << 32) ^ rd() ^ now_ms();
    sh.running = true;
    std::thread(net_thread, st.myId).detach();
  }
  const bool connected = sh.connected.load() && !st.room.empty();
  if (connected != st.wasConnected) {
    st.wasConnected = connected;
    if (!connected) NLOG("net: lost the room, reconnecting");
  }
  std::deque<std::vector<uint8_t>> in;
  {
    std::lock_guard<std::mutex> lock(sh.m);
    in.swap(sh.in);
  }
  for (const auto& msg : in) handle(msg);

  const uint64_t now = now_ms();
  // r39 watchdogs, with or without a link: the friend's input stopped coming (they left), or it
  // keeps coming but neither side can run a frame (a deadlock: the r38 timeout only looked at the
  // first, and resends kept it quiet while both headsets stood still).
  if (st.online) {
    // strikers-quest r39b: 30 s, not 10, in the frames around the match load. Both headsets load the
    // whole match in one blocking frame (online frame 1); while the friend's load runs longer this
    // headset hears nothing and runs nothing, just as if the friend had gone. (This headset's own
    // long frame no longer counts at all: PortNetFrameEnd marks the progress when a frame ends.)
    const bool matchLoad = st.frame < static_cast<uint32_t>(st.delay) + 10;
    const uint64_t quietMs = matchLoad ? 30000 : 10000;
    if (st.lastRemoteMs == 0) {
      if (now - st.onlineSinceMs > 30000) end_online("the friend never sent input");
    } else if (now - st.lastRemoteMs > quietMs) {
      end_online("friend stopped answering");
    } else if (now - st.lastProgressMs > quietMs) {
      end_online(matchLoad ? "no game frame for 30 s at the match start" : "no game frame for 10 s");
    }
  }
  // r50: the round trips the network thread timed, into the median. strikers-quest r39b: the median
  // of the last few, not a running average: one sample that waited out a load or a pause could set the
  // whole match's input delay to 20 frames. Only the host's delay goes out (in the start).
  {
    std::deque<uint32_t> rtts;
    {
      std::lock_guard<std::mutex> lock(sh.m);
      rtts.swap(sh.rtts);
    }
    for (const uint32_t rtt : rtts) {
      st.rttSamples[st.rttNext] = rtt;
      st.rttNext = (st.rttNext + 1) % kRttSamples;
      st.rttCount = std::min(st.rttCount + 1, kRttSamples);
    }
    if (!rtts.empty()) {
      uint32_t sorted[kRttSamples];
      std::copy(st.rttSamples, st.rttSamples + st.rttCount, sorted);
      std::sort(sorted, sorted + st.rttCount);
      st.rttMs = sorted[(st.rttCount - 1) / 2];
    }
  }
  sh.pinging = !st.online;
  if (!connected) return;
  if (!st.online && now - st.lastHelloMs > 500) send_hello();
  // r76, franchise co-op: this headset's co-op career each second; a career going to the friend resent
  // whole every 1.5 s until they have it (20 tries); the host's match start repeated until the friend
  // waits at it too (their hello says so) or 20 s.
  if (!st.online && !st.startPending) {
    if (now - co.lastStatusMs > 1000) send_coop_status();
    if (!co.outGot && peer_present() && now - co.outLastMs > 1500) {
      if (co.outTries >= 20) {
        co.outGot = true;
        NLOG("net: co-op: the friend never said they have the career (version %u)", co.outVersion);
      } else {
        send_career_pieces();
      }
    }
    if (co.playOut) {
      const bool friendWaits = st.peerReady && st.peerChoice.mode == PORT_NET_MODE_FRANCHISE;
      if (friendWaits || now - co.outPlaySinceMs > 20000) {
        co.playOut = false;
      } else if (now - co.outPlayLastMs > 400) {
        send_coop_play();
      }
    }
  }
  // Both at the loading screen: the lower id hosts (home side). r39b: on a recent hello (they come
  // every 0.5 s); the hold now waits out a quiet friend, whose last ready bit may be an old one.
  // r63: a Friendly with a Friendly, a cup with a cup (co-op); i_host picks who hosts.
  if (st.localReady && st.peerReady && peer_present() && now - st.peerHelloMs < 2000 && !st.online &&
      !st.startPending) {
    if (!same_kind(st.localChoice, st.peerChoice)) {
      static uint64_t told = 0;
      if (now - told > 10000) {
        told = now;
        static const char* const kKind[3] = {"Friendly", "cup match", "co-op franchise match"};
        NLOG("net: the friend waits at a %s and this headset at a %s: both need the same (two Friendlies, "
             "both in a cup for co-op, or both in the co-op franchise)", kKind[kind_of(st.peerChoice.mode)],
             kKind[kind_of(st.localChoice.mode)]);
      }
    } else if (i_host()) {
      host_start();
    }
  }
  // The host repeats the start until the friend's input shows up.
  if (st.isHost && (st.online || st.startPending) && st.lastRemoteMs == 0 && now - st.lastStartSendMs > 300) {
    send(st.startMsg);
    st.lastStartSendMs = now;
  }
  if (st.online) {
    // Every 5 s: how the match keeps up (frames run, time spent waiting for the friend's input).
    static uint64_t lastStatusMs = 0, waitedMs = 0, lastPollMs = 0;
    static uint32_t lastStatusFrame = 0, lastStatusStalls = 0;
    if (st.stallSinceMs != 0 && lastPollMs != 0) waitedMs += now - lastPollMs;
    lastPollMs = now;
    if (lastStatusMs == 0 || st.frame < lastStatusFrame) {
      lastStatusMs = now;
      lastStatusFrame = st.frame;
      lastStatusStalls = st.stalls;
      waitedMs = 0;
    } else if (now - lastStatusMs >= 5000) {
      NLOG("net: %u frames in %.1f s (waited %llu ms for the friend, %u stalls), their input %llu ms old, "
           "delay %d", st.frame - lastStatusFrame, static_cast<double>(now - lastStatusMs) / 1000.0,
           static_cast<unsigned long long>(waitedMs), st.stalls - lastStatusStalls,
           static_cast<unsigned long long>(st.lastRemoteMs != 0 ? now - st.lastRemoteMs : 0), st.delay);
      lastStatusMs = now;
      lastStatusFrame = st.frame;
      lastStatusStalls = st.stalls;
      waitedMs = 0;
    }
    // Keep the latest input flowing while waiting (a lost message would otherwise stall both).
    if (now - st.lastSendMs > 100) send_inputs();
  }
}

bool lockstep() { return st.online || st.startPending; }

bool step(const NetPad& local, NetPad* pad0, NetPad* pad1) {
  if (st.startPending) begin_online();
  st.stepped = false;
  if (!st.online) return false;
  bool fresh = false;
  while (st.localNext <= st.frame + static_cast<uint32_t>(st.delay)) {
    st.localBuf[st.localNext % kRing] = local;
    ++st.localNext;
    fresh = true;
  }
  // Two frames a message (30 a second): gentler on the free broker; the input delay has the slack.
  if (fresh && ((st.localNext & 1) == 0 || now_ms() - st.lastSendMs > 30)) send_inputs();
  NetPad remote{};
  if (st.frame >= static_cast<uint32_t>(st.delay)) {
    const uint32_t i = st.frame % kRing;
    if (st.remoteTag[i] != st.frame + 1) {
      if (st.stallSinceMs == 0) {
        st.stallSinceMs = now_ms();
        ++st.stalls;
      }
      return false;
    }
    remote = st.remoteBuf[i];
  }
  st.stallSinceMs = 0;
  const NetPad mine = st.frame >= static_cast<uint32_t>(st.delay) ? st.localBuf[st.frame % kRing] : NetPad{};
  *pad0 = st.isHost ? mine : remote;
  *pad1 = st.isHost ? remote : mine;
  // strikers-quest r39b: handed out, not used up. PortNetCommitFrame (main.cpp) moves on to the next
  // frame once the game frame really runs. Aurora can still refuse it (aurora_begin_frame: surface
  // not ready, a staging buffer that would not map); then the next step() hands out this same frame,
  // where before its inputs and seed were gone and this headset ran one frame off for the rest of
  // the match.
  st.stepped = true;
  return true;
}

int local_pad() { return (st.online || st.startPending) && !st.isHost ? 1 : 0; }

}  // namespace xrnet

// ---------------------------------------------------------------------------------------------
// The game's side (include/port/xr_net.h)
// ---------------------------------------------------------------------------------------------

using xrnet::st;

extern "C" int PortNetHoldMatchStart(const PortNetMatch* localChoice, int localSide) {
  const uint64_t now = xrnet::now_ms();
  if (now - st.lastHoldMs > 2000) {  // a new match start
    st.gaveUp = false;
    st.cancelWait = false;
    st.otherBuildLogged = false;
    st.otherEditionLogged = false;
  }
  st.lastHoldMs = now;
  if (st.online) return 0;
  if (st.startPending) return 1;
  if (st.gaveUp || !st.threadStarted) return 0;
  // strikers-quest r47 (Lance: invited a player, readied up, "and instead of just chillin in that
  // lobby til someone joined, i got launched into a single player game"): in a room, the match start
  // waits for the friend however long they take (to join the room, to load, to start their own
  // Friendly), until the player stops waiting (holds B, xr_quest.cpp: cancel_wait). r39-r46 went
  // offline at once with no friend in the room yet, after 20 s of a quiet friend, when the friend
  // backed out of their start, and after 3 minutes. Only a headset in the room on another build or
  // Version still sends the match offline (with the reason): it can never play this one.
  if (!xrnet::peer_present()) {
    if (st.otherBuildMs != 0 && now - st.otherBuildMs < 5000) {
      if (!st.otherBuildLogged) {
        // strikers-quest r39b: the reason, where a player would otherwise only see a CPU opponent.
        st.otherBuildLogged = true;
        NLOG("net: this match is offline: the other headset in the room runs another version of the game "
             "(online protocol %u there, %u here), both need the same one",
             static_cast<unsigned>(st.otherBuildProtocol), static_cast<unsigned>(xrnet::kProtocol));
        // strikers-quest r43: and the player is told (the launcher panel, which the game goes back to).
        char line[32];
        std::snprintf(line, sizeof(line), "protocol=%u", static_cast<unsigned>(st.otherBuildProtocol));
        xrnet::online_note(line);
      }
      st.localReady = false;
      return 0;
    }
    if (st.otherEditionMs != 0 && now - st.otherEditionMs < 5000) {
      if (!st.otherEditionLogged) {
        // strikers-quest r43: the other headset is on the other Version (Classic / Plus).
        st.otherEditionLogged = true;
        NLOG("net: this match is offline: the other headset in the room plays %s, this one %s; both need the "
             "same Version", xrnet::edition_name(st.otherEdition), xrnet::edition_name(xrnet::local_edition()));
        xrnet::online_note(st.otherEdition != 0 ? "edition=plus" : "edition=classic");
      }
      st.localReady = false;
      return 0;
    }
  }
  if (!st.localReady) {
    const int side = localSide == 1 ? 1 : 0;
    st.localChoice = xrnet::Choice{};
    st.localChoice.team = localChoice->teams[side];
    st.localChoice.sidekick = localChoice->sidekicks[side];
    st.localChoice.stadium = localChoice->stadium;
    st.localChoice.optionsSize = std::clamp(localChoice->optionsSize, 0, 64);
    std::memcpy(st.localChoice.options, localChoice->options, sizeof(st.localChoice.options));
    // r63: Friendly or cup; a cup's whole state and its team's side, for a co-op start as host.
    st.localChoice.mode = static_cast<uint8_t>(localChoice->mode);
    st.localChoice.progress = static_cast<uint32_t>(std::max(localChoice->cupProgress, 0));
    for (int g = 0; g < 2; ++g) {  // r67: this side's guests; r67b: and their own stats
      std::memcpy(st.localChoice.guests[g], localChoice->guests[side][g], sizeof(st.localChoice.guests[g]));
      st.localChoice.guests[g][sizeof(st.localChoice.guests[g]) - 1] = '\0';
      st.localChoice.statMask[g] = localChoice->guestStatMask[side][g];
      std::memcpy(st.localChoice.stats[g], localChoice->guestStats[side][g], sizeof(st.localChoice.stats[g]));
    }
    // r79: this side's Grudge Match co-captain
    st.localChoice.coCaptain = localChoice->coCaptains[side];
    std::memcpy(st.localChoice.coGuest, localChoice->coGuests[side], sizeof(st.localChoice.coGuest));
    st.localChoice.coGuest[sizeof(st.localChoice.coGuest) - 1] = '\0';
    st.localChoice.coMask = localChoice->coStatMask[side];
    std::memcpy(st.localChoice.coStats, localChoice->coStats[side], sizeof(st.localChoice.coStats));
    {
      std::lock_guard<std::mutex> lock(xrnet::sh.m);
      st.localChoice.maker = xrnet::sh.roomMaker ? 1 : 0;  // r64
    }
    xrnet::g_localCoopSide = localChoice->coopSide;
    xrnet::g_localCup.assign(localChoice->cup,
                             localChoice->cup + std::clamp(localChoice->cupSize, 0, static_cast<int>(sizeof(localChoice->cup))));
    if (localChoice->mode == PORT_NET_MODE_FRANCHISE) {
      // r76: the whole match from the career (the host sends it); no cup, no progress (i_host: who made
      // the room, then the lower id, as the career's host)
      xrnet::g_localFull = *localChoice;
      xrnet::g_localCup.clear();
      st.localChoice.progress = 0;
    }
    st.localReady = true;
    st.readySinceMs = now;
    st.peerLeftHoldMs = 0;
    xrnet::send_hello();
    const char* what = st.localChoice.mode == PORT_NET_MODE_FRANCHISE ? "co-op franchise match"
                       : st.localChoice.mode != 0                     ? "Cup match (co-op)"
                                                                       : "Friendly";
    if (xrnet::peer_present()) {
      NLOG("net: %s started with the friend in the room: waiting for them to start one too", what);
    } else {
      NLOG("net: %s started in room %s: waiting for the friend to join", what, st.room.c_str());
    }
  }
  if (st.cancelWait) {
    st.cancelWait = false;
    st.gaveUp = true;
    st.localReady = false;
    xrnet::send_hello();  // "not ready": a friend arriving now won't wait on this headset
    NLOG("net: the player stopped waiting for the friend (held B) after %llu s; this match is offline",
         static_cast<unsigned long long>((now - st.readySinceMs) / 1000));
    return 0;
  }
  return 1;
}

namespace xrnet {

int waiting_for_friend() {
  if (!st.localReady || st.online || st.startPending || st.gaveUp || now_ms() - st.lastHoldMs > 1000) return 0;
  if (!peer_present()) return 1;
  // r64: they wait at a match start of the other kind (a Grudge Match against this cup match, or the
  // other way round): the two never pair, so say so.
  if (st.peerReady && !same_kind(st.localChoice, st.peerChoice)) return 3;
  return 2;
}

bool waiting_in_cup() { return st.localChoice.mode != 0 && st.localChoice.mode != PORT_NET_MODE_FRANCHISE; }

bool waiting_in_franchise() { return st.localChoice.mode == PORT_NET_MODE_FRANCHISE; }

bool waiting_in_league() { return waiting_in_franchise() && g_localFull.coopSide == PORT_NET_COOP_VERSUS; }

bool room_maker() {
  std::lock_guard<std::mutex> lock(sh.m);
  return sh.roomMaker;
}

void cancel_wait() { st.cancelWait = true; }

const char* room_code() { return st.room.c_str(); }

}  // namespace xrnet

extern "C" int PortNetFrameBegin(unsigned int* seed, PortNetMatch* startMatch, int* isStartFrame) {
  *isStartFrame = 0;
  if (!st.online || st.frame == 0) return 0;
  *seed = xrnet::frame_seed(st.seed, st.frame - 1);
  if (!st.startReported) {
    st.startReported = true;
    *startMatch = st.match;
    *isStartFrame = 1;
  }
  return 1;
}

// strikers-quest r39b: main.cpp, once the game frame really runs (aurora_begin_frame took it): the
// frame step() handed out is used up only now, so a refused frame comes back with the same inputs.
extern "C" void PortNetCommitFrame(void) {
  if (!st.online || !st.stepped) return;
  st.stepped = false;
  ++st.frame;
  st.lastProgressMs = xrnet::now_ms();
}

extern "C" void PortNetFrameEnd(unsigned int stateHash, int inFrontEnd) {
  if (!st.online || st.frame == 0) return;
  // r39b: progress also counts from when a frame ends, so this headset's own long frame (the whole
  // match loads in online frame 1) is not taken for a stall by the watchdog in poll().
  st.lastProgressMs = xrnet::now_ms();
  const uint32_t f = st.frame - 1;
  if (f % 300 == 0) NLOG("net: frame %u state %08x", f, stateHash);  // two runs compare line by line
  if (f % 30 == 0) {
    st.myHash[f % xrnet::kRing] = stateHash;
    st.myHashTag[f % xrnet::kRing] = f + 1;
    xrnet::Writer w = xrnet::header(xrnet::kHash);
    w.u32(f);
    w.u32(stateHash);
    xrnet::send(std::move(w.v));
    xrnet::compare_hash(f);
  }
  // Back in the menus after the match (both headsets reach it on the same frame).
  if (inFrontEnd && f > 120) {
    if (++st.frontEndFrames > 30) xrnet::end_online("back in the menus");
  } else {
    st.frontEndFrames = 0;
  }
}

extern "C" int PortNetLocalPad(void) { return xrnet::local_pad(); }

extern "C" int PortNetOnline(void) { return st.online ? 1 : 0; }

extern "C" void PortNetNoteScore(int home, int away) {
  if (!st.online) return;
  st.score[0] = home;
  st.score[1] = away;
  st.scoreSeen = true;
}

// r76, franchise co-op (include/port/xr_net.h): the franchise's side, all on the game thread.
using xrnet::co;

extern "C" int PortNetCoopFriend(void) {
  return st.threadStarted && !st.room.empty() && xrnet::sh.connected.load() && xrnet::peer_present() ? 1 : 0;
}

extern "C" int PortNetCoopHost(void) { return xrnet::coop_host() ? 1 : 0; }

extern "C" const char* PortNetCoopFriendName(void) { return co.friendName.c_str(); }

extern "C" void PortNetCoopSetMine(int has, unsigned careerId, unsigned version, int inHub) {
  const bool h = has != 0, in = inHub != 0;
  if (h != co.mineHas || in != co.mineInHub || careerId != co.mineId || version != co.mineVersion) {
    co.mineHas = h;
    co.mineInHub = in;
    co.mineId = careerId;
    co.mineVersion = version;
    co.lastStatusMs = 0;  // told at the next poll
  }
}

extern "C" int PortNetCoopFriendCareer(int* has, unsigned* careerId, unsigned* version, int* inHub) {
  if (!co.friendHeard || !xrnet::peer_present()) return 0;
  *has = co.friendHas ? 1 : 0;
  *careerId = co.friendId;
  *version = co.friendVersion;
  *inHub = co.friendInHub ? 1 : 0;
  return 1;
}

extern "C" void PortNetCoopSendCareer(const char* text, int size, unsigned careerId, unsigned version) {
  if (text == nullptr || size <= 0 || size > 700000) return;
  static uint32_t count = 0;
  co.outText.assign(text, static_cast<size_t>(size));
  co.outTransfer = (static_cast<uint32_t>(st.myId) ^ (++count * 0x9E3779B9u)) | 1u;
  co.outId = careerId;
  co.outVersion = version;
  co.outGot = false;
  co.outTries = 0;
  co.outLastMs = 0;  // sent at the next poll
}

extern "C" const char* PortNetCoopTakeCareer(int* size, unsigned* careerId, unsigned* version) {
  if (co.taken) return nullptr;
  co.taken = true;
  co.handedOut = std::move(co.received);
  co.received.clear();
  *size = static_cast<int>(co.handedOut.size());
  *careerId = co.receivedId;
  *version = co.receivedVersion;
  return co.handedOut.c_str();
}

extern "C" void PortNetCoopRequestCareer(void) {
  if (!xrnet::sh.connected.load()) return;
  xrnet::Writer w = xrnet::header(xrnet::kCoop);
  w.u8(xrnet::kCoopAsk);
  xrnet::send(std::move(w.v));
}

extern "C" int PortNetCoopTakeRequest(void) {
  if (!co.asked) return 0;
  co.asked = false;
  return 1;
}

extern "C" void PortNetCoopSendPlay(unsigned careerId, unsigned version, int watchGame) {
  std::random_device rd;
  co.outPlayId = careerId;
  co.outPlayVersion = version;
  co.outPlayWatch = watchGame;
  co.outPlaySerial = rd() | 1u;
  co.playOut = true;
  co.outPlaySinceMs = xrnet::now_ms();
  co.outPlayLastMs = 0;
  xrnet::send_coop_play();
}

extern "C" int PortNetCoopTakePlay(unsigned* careerId, unsigned* version, int* watchGame) {
  if (!co.playIn) return 0;
  co.playIn = false;
  *careerId = co.playId;
  *version = co.playVersion;
  *watchGame = co.playWatch;
  return 1;
}

// strikers-quest r45: the tournament match the panel's room is for ("" for none), with the room
// (StrikersActivity, before nativeSetRoom); taken when the next online match starts.
extern "C" JNIEXPORT void JNICALL Java_io_github_newcoke_strikers_StrikersActivity_nativeSetTourney(JNIEnv* env, jclass,
                                                                                                  jstring jmid) {
  const char* s = jmid != nullptr ? env->GetStringUTFChars(jmid, nullptr) : nullptr;
  std::string mid;
  for (const char* c = s; c != nullptr && *c != '\0' && mid.size() < 40; ++c) {
    // r45d: '.' too: the hub's match ids are <tid>.<round>.<index>. r45c dropped the dots, so the
    // final's result (the first match started through here, not the environment) named no match.
    if ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' ||
        *c == '.') {
      mid.push_back(*c);
    }
  }
  if (s != nullptr) env->ReleaseStringUTFChars(jmid, s);
  std::lock_guard<std::mutex> lock(xrnet::sh.m);
  xrnet::sh.tourney = mid;
}

// strikers-quest r64: the panel's room was made here (Host an online match) or not (Join, Quick
// match), with the room (StrikersActivity, before nativeSetRoom); taken at the next match start.
extern "C" JNIEXPORT void JNICALL Java_io_github_newcoke_strikers_StrikersActivity_nativeSetRoomMaker(JNIEnv*, jclass,
                                                                                                    jboolean maker) {
  std::lock_guard<std::mutex> lock(xrnet::sh.m);
  xrnet::sh.roomMaker = maker == JNI_TRUE;
}

// strikers-quest r45: StrikersActivity, once a second: true once after a tournament match ended and
// its result was written (tourney-result.txt), so the panel comes up to report it.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_newcoke_strikers_StrikersActivity_nativeTakeTourneyResult(JNIEnv*,
                                                                                                            jclass) {
  return xrnet::g_tourneyPending.exchange(false) ? JNI_TRUE : JNI_FALSE;
}

// r39: the room code from the launcher panel when the game was already running (StrikersActivity
// .onNewIntent). Takes effect between matches; "" is offline. r39b: only the pick is set here; poll()
// moves the game and the connection to it (sh.activeRoom) once no match needs the old room.
extern "C" JNIEXPORT void JNICALL Java_io_github_newcoke_strikers_StrikersActivity_nativeSetRoom(JNIEnv* env, jclass,
                                                                                               jstring jroom) {
  const char* s = jroom != nullptr ? env->GetStringUTFChars(jroom, nullptr) : nullptr;
  const std::string room = xrnet::sanitize_room(s);
  if (s != nullptr) env->ReleaseStringUTFChars(jroom, s);
  std::lock_guard<std::mutex> lock(xrnet::sh.m);
  xrnet::sh.room = room;
  xrnet::sh.roomSet = true;
}

// r100: an online league's match room (the league server's go), this headset hosting it (the home club's) or
// not; null or "": back to the launcher's room as it was. Taken by poll() between matches, like a pick.
extern "C" void PortNetLeagueRoom(const char* room, int host) {
  static bool s_in = false;
  static std::string s_room, s_tourney;
  static bool s_maker = false;
  std::lock_guard<std::mutex> lock(xrnet::sh.m);
  if (!xrnet::sh.roomSet) {  // (the launcher's, as poll() would take it)
    xrnet::sh.room = xrnet::sanitize_room(std::getenv("STRIKERS_ROOM"));
    const char* tourney = std::getenv("STRIKERS_TOURNEY");
    xrnet::sh.tourney = tourney != nullptr ? tourney : "";
    const char* maker = std::getenv("STRIKERS_ROOM_MAKER");
    xrnet::sh.roomMaker = maker != nullptr && maker[0] == '1';
    xrnet::sh.roomSet = true;
  }
  const std::string code = xrnet::sanitize_room(room);
  if (!code.empty()) {
    if (!s_in) {
      s_in = true;
      s_room = xrnet::sh.room;
      s_tourney = xrnet::sh.tourney;
      s_maker = xrnet::sh.roomMaker;
    }
    xrnet::sh.room = code;
    xrnet::sh.tourney.clear();
    xrnet::sh.roomMaker = host != 0;  // (i_host: the maker hosts: the home club's headset)
    NLOG("net: league game room %s (%s)", code.c_str(), host != 0 ? "hosting it" : "joining it");
  } else if (s_in) {
    s_in = false;
    xrnet::sh.room = s_room;
    xrnet::sh.tourney = s_tourney;
    xrnet::sh.roomMaker = s_maker;
    NLOG("net: league game over: back to room %s", s_room.empty() ? "(none)" : s_room.c_str());
  }
}

// r100: the connection has moved to the league game's room (poll() took it): its match start can wait there.
extern "C" int PortNetLeagueRoomReady(void) {
  std::string want;
  {
    std::lock_guard<std::mutex> lock(xrnet::sh.m);
    want = xrnet::sh.room;
  }
  return st.threadStarted && !want.empty() && st.room == want ? 1 : 0;
}

// strikers-quest r43: StrikersActivity, once a second: true once after an online match was refused
// (online_note: the friend is on the other Version or another build), so the game goes back to the
// launcher panel, which says why and has the Version button.
extern "C" JNIEXPORT jboolean JNICALL Java_io_github_newcoke_strikers_StrikersActivity_nativeTakeOnlineNote(JNIEnv*,
                                                                                                         jclass) {
  return xrnet::g_notePending.exchange(false) ? JNI_TRUE : JNI_FALSE;
}

#else  // !__ANDROID__ || !STRIKERS_XR

namespace xrnet {
void poll() {}
bool lockstep() { return false; }
bool step(const NetPad&, NetPad*, NetPad*) { return false; }
int local_pad() { return 0; }
int waiting_for_friend() { return 0; }
bool waiting_in_cup() { return false; }
bool waiting_in_franchise() { return false; }
bool waiting_in_league() { return false; }
bool room_maker() { return false; }
void cancel_wait() {}
const char* room_code() { return ""; }
}  // namespace xrnet

extern "C" int PortNetHoldMatchStart(const PortNetMatch*, int) { return 0; }
extern "C" int PortNetFrameBegin(unsigned int*, PortNetMatch*, int* isStartFrame) {
  *isStartFrame = 0;
  return 0;
}
extern "C" void PortNetCommitFrame(void) {}
extern "C" void PortNetFrameEnd(unsigned int, int) {}
extern "C" int PortNetLocalPad(void) { return 0; }
extern "C" int PortNetOnline(void) { return 0; }
extern "C" void PortNetNoteScore(int, int) {}
extern "C" int PortNetCoopFriend(void) { return 0; }
extern "C" int PortNetCoopHost(void) { return 1; }
extern "C" const char* PortNetCoopFriendName(void) { return ""; }
extern "C" void PortNetCoopSetMine(int, unsigned, unsigned, int) {}
extern "C" int PortNetCoopFriendCareer(int*, unsigned*, unsigned*, int*) { return 0; }
extern "C" void PortNetCoopSendCareer(const char*, int, unsigned, unsigned) {}
extern "C" const char* PortNetCoopTakeCareer(int*, unsigned*, unsigned*) { return nullptr; }
extern "C" void PortNetCoopRequestCareer(void) {}
extern "C" int PortNetCoopTakeRequest(void) { return 0; }
extern "C" void PortNetCoopSendPlay(unsigned, unsigned, int) {}
extern "C" int PortNetCoopTakePlay(unsigned*, unsigned*, int*) { return 0; }
extern "C" void PortNetLeagueRoom(const char*, int) {}
extern "C" int PortNetLeagueRoomReady(void) { return 0; }

#endif
