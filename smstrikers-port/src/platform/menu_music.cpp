// strikers-quest r147: the lofi menu music's state for Java and the game's own front-end music's gain. See
// include/port/menu_music.h.

#include "port/menu_music.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace {

std::atomic<int> s_mode{-1};          // -1: not read yet (strikers.ini / BOTH)
std::atomic<int> s_match{0};          // a match's game state is up
std::atomic<int> s_quiet{0};          // a movie / the evolution's music: the lofi fades out under it
std::atomic<int> s_franchise{0};      // + 1 each time the hub opens from the main menu
std::atomic<int> s_active{0};         // Java plays the menu music (reported by Java)
std::atomic<int> s_ready{0};          // the game's audio has ticked once (before it: nothing plays)
std::atomic<int> s_mySongs{-1};       // songs in STRIKERS_HOME/music (reported by Java)
std::atomic<float> s_volume{1.0f};    // the game's music volume (VG_Music)
std::atomic<float> s_feGain{1.0f};    // the game's own front-end music's gain now

std::mutex s_npMutex;
std::string s_npTitle, s_npArtist;
int s_npSerial = 0, s_npTaken = 0;

constexpr float kCrossfadeS = 1.5f;  // every switch: the game's front-end music <-> the lofi

const char* const kModeNames[PORT_MENU_MUSIC_MODES] = {"LOFI", "MY MUSIC", "BOTH", "OFF"};

int mode_from_text(const char* t) {
  if (t == nullptr || *t == '\0') return PORT_MENU_MUSIC_BOTH;
  char s[32];
  size_t n = 0;
  for (; t[n] != '\0' && n + 1 < sizeof(s); ++n) {
    const char c = t[n];
    s[n] = (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
  s[n] = '\0';
  if (std::strcmp(s, "lofi") == 0 || std::strcmp(s, "0") == 0) return PORT_MENU_MUSIC_LOFI;
  if (std::strcmp(s, "mine") == 0 || std::strcmp(s, "my music") == 0 || std::strcmp(s, "my_music") == 0 ||
      std::strcmp(s, "mymusic") == 0 || std::strcmp(s, "1") == 0)
    return PORT_MENU_MUSIC_MINE;
  if (std::strcmp(s, "off") == 0 || std::strcmp(s, "game") == 0 || std::strcmp(s, "3") == 0) return PORT_MENU_MUSIC_OFF;
  return PORT_MENU_MUSIC_BOTH;
}

float env_float(const char* name, float dflt, float lo, float hi) {
  const char* e = std::getenv(name);
  if (e == nullptr || *e == '\0') return dflt;
  const float v = static_cast<float>(std::atof(e));
  return v < lo ? lo : (v > hi ? hi : v);
}

// strikers.ini menu_music_volume (0..1) x the port's master gain (audio_volume, as the mixer's), read once.
// Default 0.45, measured (r147, the disc's own files decoded on the PC): the game's menu music plays at about -24 to
// -27 LUFS (FE_Main_Options -12.9 LUFS in its file at FEStreams.txt volume 50 = MusyX vol 63 = x0.20, FE_Cup_Battle
// -14.2 at 60 = x0.32, FE_Main_Title -10.5 at 50); the lofi files are -16 LUFS, x0.45 = about -23 LUFS: as loud as
// the game's own, a touch over it.
float trim() {
  static const float t = env_float("STRIKERS_MENU_MUSIC_VOLUME", 0.45f, 0.0f, 1.0f) *
                         env_float("STRIKERS_AUDIO_VOLUME", 1.0f, 0.0f, 2.0f);
  return t;
}

void log_line(const char* fmt, const char* a = "", const char* b = "") {
  std::fprintf(stderr, "[music] ");
  std::fprintf(stderr, fmt, a, b);
  std::fprintf(stderr, "\n");
  std::fflush(stderr);
}

}  // namespace

extern "C" int PortMenuMusicMode(void) {
  int m = s_mode.load();
  if (m < 0) {
    m = mode_from_text(std::getenv("STRIKERS_MENU_MUSIC"));
    int expected = -1;
    if (!s_mode.compare_exchange_strong(expected, m)) m = s_mode.load();
  }
  return m;
}

extern "C" void PortMenuMusicSetMode(int mode) {
  if (mode < 0 || mode >= PORT_MENU_MUSIC_MODES) mode = PORT_MENU_MUSIC_BOTH;
  if (s_mode.exchange(mode) != mode) log_line("menu music setting: %s", kModeNames[mode]);
}

extern "C" const char* PortMenuMusicModeName(int mode) {
  return mode >= 0 && mode < PORT_MENU_MUSIC_MODES ? kModeNames[mode] : "BOTH";
}

extern "C" int PortMenuMusicMySongs(void) { return s_mySongs.load(); }

extern "C" void PortMenuMusicSetMatch(int inMatch) {
  const int v = inMatch != 0 ? 1 : 0;
  if (s_match.exchange(v) != v) log_line(v ? "a match: the game's own music (the lofi fades out)" : "the menus: the lofi");
}

extern "C" void PortMenuMusicFranchiseEnter(void) {
  s_franchise.fetch_add(1);
  log_line("franchise mode entered: the theme first");
}

extern "C" int PortMenuMusicInMatch(void) { return s_match.load(); }
extern "C" int PortMenuMusicActive(void) { return s_active.load(); }
extern "C" float PortMenuMusicFEGain(void) { return s_feGain.load(std::memory_order_relaxed); }

extern "C" int PortMenuMusicTick(float dT, float gameMusicVolume) {
  if (s_ready.exchange(1, std::memory_order_relaxed) == 0) log_line("the game's audio runs: the menu music may start (setting %s)", PortMenuMusicModeName(PortMenuMusicMode()));
  s_volume.store(gameMusicVolume, std::memory_order_relaxed);
  s_quiet.store(PortMovieActive() != 0 || PortEvoMusicPlaying() != 0 ? 1 : 0, std::memory_order_relaxed);
  // The game's own front-end music: silent while the lofi is in charge of the menus (in a match there are no FE
  // tracks, so this only decides how the next menu's music starts: silent, not faded out once more)
  const float want = s_active.load(std::memory_order_relaxed) != 0 ? 0.0f : 1.0f;
  const float was = s_feGain.load(std::memory_order_relaxed);
  if (was == want) return 0;
  const float step = (dT > 0.0f && dT < 0.25f ? dT : 1.0f / 60.0f) / kCrossfadeS;
  const float now = want < was ? (was - step < want ? want : was - step) : (was + step > want ? want : was + step);
  s_feGain.store(now, std::memory_order_relaxed);
  if (now == want) log_line(want == 0.0f ? "the game's front-end music silenced (the lofi plays)" : "the game's front-end music back");
  return 1;
}

extern "C" int PortMenuMusicTakeNowPlaying(char* title, int titleSize, char* artist, int artistSize) {
  std::lock_guard<std::mutex> lock(s_npMutex);
  if (s_npTaken == s_npSerial) return 0;
  s_npTaken = s_npSerial;
  if (title != nullptr && titleSize > 0) std::snprintf(title, static_cast<size_t>(titleSize), "%s", s_npTitle.c_str());
  if (artist != nullptr && artistSize > 0) std::snprintf(artist, static_cast<size_t>(artistSize), "%s", s_npArtist.c_str());
  return 1;
}

// ------------------------------------------------------------------------------------------- Java's side (Android)
#if defined(__ANDROID__)
#include <jni.h>

namespace {
std::string jstr(JNIEnv* env, jstring s) {
  if (s == nullptr) return std::string();
  const char* c = env->GetStringUTFChars(s, nullptr);
  std::string r = c != nullptr ? c : "";
  if (c != nullptr) env->ReleaseStringUTFChars(s, c);
  return r;
}
}  // namespace

// MenuMusic.java's poll: bits 0-1 the scene (0 the menus, 1 a match, 2 quiet: a movie / the evolution's music,
// 3 the game's audio not running yet), bits 2-3 the setting, bits 4-30 the franchise entries so far.
extern "C" JNIEXPORT jint JNICALL Java_io_github_newcoke_strikers_MenuMusic_nativeState(JNIEnv*, jclass) {
  const int scene = s_ready.load() == 0 ? 3 : (s_match.load() != 0 ? 1 : (s_quiet.load() != 0 ? 2 : 0));
  return static_cast<jint>(scene | (PortMenuMusicMode() << 2) | ((s_franchise.load() & 0x7FFFFFF) << 4));
}

// The lofi's volume: the game's music volume x menu_music_volume x audio_volume (MediaPlayer's 0..1).
extern "C" JNIEXPORT jfloat JNICALL Java_io_github_newcoke_strikers_MenuMusic_nativeVolume(JNIEnv*, jclass) {
  const float v = s_volume.load() * trim();
  return static_cast<jfloat>(v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v));
}

extern "C" JNIEXPORT void JNICALL Java_io_github_newcoke_strikers_MenuMusic_nativeSetMode(JNIEnv*, jclass, jint mode) {
  PortMenuMusicSetMode(static_cast<int>(mode));
}

// Java plays the menus' music (or stopped: OFF, nothing playable); `mySongs` its count of his songs.
extern "C" JNIEXPORT void JNICALL Java_io_github_newcoke_strikers_MenuMusic_nativeActive(JNIEnv*, jclass, jboolean active,
                                                                                       jint mySongs) {
  const int v = active ? 1 : 0;
  s_mySongs.store(static_cast<int>(mySongs));
  if (s_active.exchange(v) != v) log_line(v ? "the lofi player is playing the menus" : "the lofi player stopped: the game's own menu music");
}

extern "C" JNIEXPORT void JNICALL Java_io_github_newcoke_strikers_MenuMusic_nativeNowPlaying(JNIEnv* env, jclass, jstring title,
                                                                                           jstring artist) {
  const std::string t = jstr(env, title), a = jstr(env, artist);
  {
    std::lock_guard<std::mutex> lock(s_npMutex);
    s_npTitle = t;
    s_npArtist = a;
    ++s_npSerial;
  }
  log_line("now playing: %s - %s", t.c_str(), a.c_str());
}

// Java's own lines into the game's log (strikers-log.txt), beside these.
extern "C" JNIEXPORT void JNICALL Java_io_github_newcoke_strikers_MenuMusic_nativeLog(JNIEnv* env, jclass, jstring line) {
  const std::string l = jstr(env, line);
  log_line("%s", l.c_str());
}
#endif
