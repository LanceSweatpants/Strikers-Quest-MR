// strikers-quest r137: weather + night games (Lance, 11-03: "sooooo fucking fire"). LOOKS ONLY: the game plays exactly
// the same - nothing here touches physics, AI, the ball or the clock.
//
// Kinds: RAIN, SNOW, NIGHT (night can come with rain or snow). Chosen once per match (PortWeatherNewMatch, from
// cGame::BeginGame through PortXrLayersNewMatch):
//   - a franchise match: the career's calendar (franchise.cpp, xr_weather_cal.h): late season 3 of the last 7 games
//     snowy, 2-3 rain games, night for derbies, nemesis games, every playoff game and a couple more
//   - any other match: the WEATHER setting (pause settings row / strikers.ini `weather = ...`): RANDOM (60% clear, 15%
//     rain, 10% snow, 15% night; rain / snow at night 30% of the time), CLEAR, RAIN, SNOW, NIGHT, OFF (OFF also
//     switches the franchise calendar off)
//   - stadium sense: the Forbidden Dome is covered (no rain / snow there, night still fine); the Inferno clubs' grounds
//     are too hot for snow
// Where it shows (each view's cost in NOTES.md r137):
//   - the game's own picture (the big screen / Jumbotron, every view): a colour grade as two blended full-screen quads
//     in the game's 2D views (the stadium darker before the players are drawn, all of it cooler after), rain / snow
//     falling in front of the game camera, splashes and floodlit pools on the pitch, snow drifting in at the pitch
//     edges, floodlight glows and beams at night (PortWeatherRenderGame, GameRenderTask::Run)
//   - the 3D views' eye layers: the compositor's colour scale / bias (XR_KHR_composition_layer_color_scale_bias, free),
//     plus the pitch's splashes / pools / drifts above (they are game-world quads, so they are on the Tabletop field and
//     the life-size pitch too)
//   - the effects layer (xr_fx.cpp): rain / snow over and onto the Tabletop field and your table (MR: splashes on the
//     table, snow settling round the field), round you in the life-size stadium; floodlight glows + beams
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

enum {
  PORT_WX_RAIN = 1,
  PORT_WX_SNOW = 2,
  PORT_WX_NIGHT = 4,
};
enum {
  PORT_WX_MODE_RANDOM = 0,
  PORT_WX_MODE_CLEAR = 1,
  PORT_WX_MODE_RAIN = 2,
  PORT_WX_MODE_SNOW = 3,
  PORT_WX_MODE_NIGHT = 4,
  PORT_WX_MODE_OFF = 5,
  // r196: one tier picked for sure (RAIN / SNOW pick their tier at random, as RANDOM does)
  PORT_WX_MODE_DRIZZLE = 6,
  PORT_WX_MODE_STORM = 7,
  PORT_WX_MODE_FLURRY = 8,
  PORT_WX_MODE_BLIZZARD = 9,
  PORT_WX_MODES = 10,
};
// r196 (WEATHER-UPGRADE.md, LOOKS AND SOUND ONLY): the match's tier, random per match like the weather itself (rain:
// DRIZZLE 30% / RAIN 40% / THUNDERSTORM 30%; snow: FLURRY 40% / BLIZZARD 60%, the blizzard ramping in from a flurry).
// r198: rain DRIZZLE 35 / RAIN 45 / THUNDERSTORM 20, snow FLURRY 65 / BLIZZARD 35 (xr_weather_cal.h rain_tier / snow_tier).
enum {
  PORT_WX_TIER_NONE = 0,
  PORT_WX_TIER_DRIZZLE = 1,
  PORT_WX_TIER_RAIN = 2,
  PORT_WX_TIER_STORM = 3,
  PORT_WX_TIER_FLURRY = 4,
  PORT_WX_TIER_BLIZZARD = 5,
};

// The setting (the pause row; xr_room_options.txt "weather", default strikers.ini `weather`).
int PortWeatherMode(void);
void PortWeatherSetMode(int mode);  // also re-picks the match on now (a quick match: the new pick; OFF: clear)
const char* PortWeatherModeName(int mode);  // "RANDOM" ... "OFF"

// A match is being set up (cGame::BeginGame -> PortXrLayersNewMatch): pick its weather.
void PortWeatherNewMatch(int stadium);
// The match's weather (PORT_WX_* bits, 0 clear) and how far it has come in (0..1, the first second and a half).
int PortWeatherCurrent(void);
float PortWeatherFade(void);
// Snow lying at the pitch's edges / on your table: 0 at kick-off .. 1 after a couple of minutes of snow.
float PortWeatherSnowCover(void);
// r148: the effects layer's load scale (xr_fx, 0.25..1, down while the headset misses frames): the game-side counts
// (the camera's flakes / streaks, the pitch's splashes) follow it too.
void PortWeatherSetLoad(float load);
// r198: "RAIN GAME", "SNOW GAME", "NIGHT GAME", "RAIN NIGHT GAME", "SNOW NIGHT GAME" ("" for clear).
const char* PortWeatherLabel(int bits);

// ---- r196: tiers, wind, lightning, thunder, snow piling up (all looks / sound only) ----
// The tier (PORT_WX_TIER_*), and how strong it falls now 0..1 (DRIZZLE 0.35, RAIN 0.7, THUNDERSTORM 1, FLURRY 0.14,
// BLIZZARD ramping 0.14 -> 1 over its first ~70 s, then gusting 0.85..1). Includes nothing of the fade.
int PortWeatherTier(void);
float PortWeatherIntensity(void);
// The match card's words: r198 = PortWeatherLabel of the match (no tier names: RAIN GAME / SNOW GAME / NIGHT GAME).
const char* PortWeatherMatchLabel(void);
// The wind (GAME world: m/s along x = the pitch's length, y = across) and its gust 0..1. Rain slants with it, snow is
// blown by it, the howl pans with it.
void PortWeatherWind(float* vx, float* vy, float* gust);
// Lightning: the flash now (0 .. ~1.3; it lights the stadium, the Jumbotron picture, the room), and the bolt showing
// (returns 0 none): its strike number (a new one = a new bolt), how much it shows 0..1, kind 0 far 1 mid 2 close, x
// -1..1 across the view it is placed in, and its shape's seed.
float PortWeatherFlash(void);
int PortWeatherBolt(int* id, float* vis, int* kind, float* x, unsigned* seed);
// The bolt showing's shape: up to max segments (x0 y0 x1 y1 w each; x across from its top, y 0 the cloud .. 1 its
// end, w 1 the main channel / ~0.5 a branch); returns the count (0 none).
int PortWeatherBoltShape(float* xyw, int max);
// Snow lying on the pitch (GAME world): a cols x rows grid over x -kHalfX..kHalfX, y -kHalfY..kHalfY, 0..1 each,
// piling up where nobody has played for a while, trampled where the players / the ball go. Copies up to max cells,
// returns cols * rows (0 none).
int PortWeatherSnowGrid(float* out, int max, int* cols, int* rows);
// Online (xr_net.cpp): the host's weather setting + the match's shared seed, so both headsets pick the same weather,
// tier, wind and lightning (on=0 forgets it). Read by the next PortWeatherNewMatch while PortNetOnline().
void PortWeatherSetShared(int on, unsigned seed, int hostMode);
// The headset's listener for the weather's sound: your head's right (LOCAL x, z) and the room's wind turn (the game's
// x axis in LOCAL: its yaw) - the howl and the rain pan with the wind as you turn.
void PortWeatherSetListener(float rightX, float rightZ, float worldYaw);
// The port mixer (audio_mix.cpp, 32 kHz stereo, s16 scale into s32): the rain / wind beds and the thunder.
void PortWeatherAudioMix(int* left, int* right, int frames);

// The game's renderer, once per game frame (GameRenderTask::Run, after the world): the game-side looks. dt = the
// frame's game time (0 while paused).
void PortWeatherRenderGame(float dt);

// The headset's eye layers: the compositor's colour scale / bias for a layer (kind 0 the Tabletop field over your room
// (no bias: it would tint the room), 1 the table / the life-size scorer over the VR stadium, 2 the life-size stadium).
// Returns 0 when there is nothing to apply. Includes the fade.
int PortWeatherLayerGrade(int kind, float scale[4], float bias[4]);
// The stadium's floodlight banks (game world: metres, Z up, the centre spot at the origin), up to `max`, for the
// effects layer's glows in the life-size stadium. Found from the game's own floodlight flares each match.
int PortWeatherLights(float* xyz, int max);

// franchise.cpp: the franchise match being played (PORT_WX_* bits; -1 not a franchise match), and the human club's
// next game (the hub's card, the magazine; 0 clear or no game). Week: that week's game of the human club (-1 none).
int PortFranchiseMatchWeather(void);
int PortFranchiseNextWeather(void);
int PortFranchiseWeekWeather(int week);

#ifdef __cplusplus
}
#endif
