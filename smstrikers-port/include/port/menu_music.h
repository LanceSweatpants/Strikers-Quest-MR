// strikers-quest r147 (Lance: "change the franchise menu/home screen/not in game music to some cc0 lofi playlist
// that we can play on loop"): the menu music outside matches.
//
// Who plays it: Java (io.github.newcoke.strikers.MenuMusic, Android's MediaPlayer on a thread of its own), so the
// decoding costs the game thread nothing and the music is its own output stream beside the game's SDL / AAudio one
// (the system mixes them). This side is only the state Java polls (~10 times a second) and the game's own
// front-end music, which is silenced while the lofi plays:
//   - the menus vs a match: TransitionTask (the game state's InitializeGameState end -> match, DestroyGameState
//     start -> menus). Matches, goal shows, replays, cut-scenes and the post-match screens keep the game's music.
//   - quiet: a THP movie (intro, Strikers 101) or the franchise evolution's own music: the lofi fades out under it.
//   - the franchise hub opening from the main menu (xr_quest.cpp): the theme first, then the shuffle.
//   - the MENU MUSIC setting (pause panel row 8 / the launcher's button / strikers.ini menu_music).
//   - the game's music volume (its Options, VG_Music) x strikers.ini menu_music_volume (default 0.45, matched to the
//     game's own menu music's measured loudness: see menu_music.cpp trim()).
// The game's own front-end music (the FE / FE2 stream tracks, the only ones the menus play music on) is scaled by
// PortMenuMusicFEGain at the stream's buffers (GCStream.h AudioStream::SetVolume): 1 = as the game plays it, ramped
// to 0 over 1.5 s while Java reports the lofi in charge, and back. Menu clicks, voices and every other sound stay.
#ifndef PORT_MENU_MUSIC_H
#define PORT_MENU_MUSIC_H

#ifdef __cplusplus
extern "C" {
#endif

// The MENU MUSIC setting, in the order the pause row / the launcher button step through it.
enum {
    PORT_MENU_MUSIC_LOFI = 0,  // the CC0 lofi playlist (and its franchise theme)
    PORT_MENU_MUSIC_MINE = 1,  // only the songs in STRIKERS_HOME/music (the lofi when that folder is empty)
    PORT_MENU_MUSIC_BOTH = 2,  // the default: the lofi + his songs
    PORT_MENU_MUSIC_OFF = 3,   // the game's own menu music, as before r147
    PORT_MENU_MUSIC_MODES = 4
};

// The setting: the pause row / launcher's pick (xr_room_options.txt "music N"), else strikers.ini
// `menu_music = lofi | mine | both | off` (STRIKERS_MENU_MUSIC), else BOTH.
int PortMenuMusicMode(void);
void PortMenuMusicSetMode(int mode);
const char* PortMenuMusicModeName(int mode);  // "LOFI", "MY MUSIC", "BOTH", "OFF"
// How many songs Java found in STRIKERS_HOME/music (-1 before it looked), for the pause row's words.
int PortMenuMusicMySongs(void);

// TransitionTask: 1 once a match's game state is up, 0 as it starts going (back to the menus).
void PortMenuMusicSetMatch(int inMatch);
// xr_quest.cpp: the franchise hub opened from the game's main menu (Y), not its return after a franchise match.
void PortMenuMusicFranchiseEnter(void);

// The game thread, once a frame (TrackManagerBase::Update): `dT` seconds, `gameMusicVolume` the game's music
// volume now (Audio::MasterVolume VG_Music, 0..1). Steps the front-end music's gain; 1 when it changed (the FE
// streams' volumes are then set again).
int PortMenuMusicTick(float dT, float gameMusicVolume);
// The game's own front-end music's gain now (1 = untouched).
float PortMenuMusicFEGain(void);

// xr_quest.cpp: the track that just started ("NOW PLAYING"), each one once. 0 when there is none new.
int PortMenuMusicTakeNowPlaying(char* title, int titleSize, char* artist, int artistSize);
// Java is playing the menu music (the lofi is in charge of the menus, the game's own front-end music silenced).
int PortMenuMusicActive(void);
// 1 while a match is up (TransitionTask), for the toast.
int PortMenuMusicInMatch(void);

// Defined elsewhere, read by the tick: a THP movie is playing (src/Game/Sys/movie.cpp); the franchise evolution's
// music is playing (src/platform/xr_guest_voice.cpp).
int PortMovieActive(void);
int PortEvoMusicPlaying(void);
// r230: an EXQUISITE pack's private gallery is up (its lounge music: the lofi fades out under it) - src/platform/xr_guest_voice.cpp
int PortCardGalleryQuiet(void);

#ifdef __cplusplus
}
#endif

#endif
