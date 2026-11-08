// strikers-quest r172: STADIUM PULSE - the home crowd as a college student section (include/port/stadium_pulse.h).
// ATMOSPHERE ONLY (Lance's call): nothing here is read by the match.
//
// THE PULSE (0-100, the home crowd's: side 0). Kept in milli-points, advanced in whole tenths of GAME time (the clock the
// match runs on), from the match alone: the game's own audio events (AudioScriptEventMgr), the goals (Game.cpp), the
// scores and the clock. Integer maths, no random numbers: both headsets of an online match compute the same curve.
//   - between events it drifts (1/150 of the gap every tenth: ~15 s) toward a baseline from the score and the clock:
//     a close game (a goal or less in it) 45 rising to 70 through the match, +12 in the last 60 s; sudden death 88;
//     the home club 2 up 52, 3+ up 42 and less (a blowout quietens it); 2 down 34, 3+ down 24 and less
//   - moments push it: a home goal +28 (+8 if it ties or takes the lead from behind, +10 in the last quarter when it
//     leaves the game close, +10 for a Super Strike's two), an away goal -18 (-8 more if it makes it 3+), home shots,
//     chances, Super Strikes, big hits and saves up; the away side's a little (the section gets loud on defence too);
//     the last quarter's rises x1.25
// THE CROWD (the game's own crowd sounds only, no synth: Lance's rule). Through the r42 living crowd (living_crowd.cpp):
//   - loudness: the mixer's crowd gain 0.6 + pulse (0.6 .. 1.6, +0.1 in a close finish), times YOUR crowd volume
//   - reactions by the home crowd's side of each moment: a chance / wind-up = the "ooooh" swell (Ant_Big / Ant_Small),
//     a home miss / post / their keeper's save = groans (Oh_Big), your keeper's save = Clap_Big + Yeah, an away miss =
//     a relieved Yeah, a home hit = Yeah, an away hit or Super Strike = boos / jeers
//   - a home goal = the r42 roar, longer and louder with the pulse; an away goal = Goal_Away (the game's own: a boo),
//     Boo_Big, Jeer_Big - hostile, short
//   - rhythm: while the pulse is 75+, or in a close game's last 60 s, or in sudden death, the crowd's chant stream plays
//     the game's own CHANTS_GEN_Stomp_Clap_01..07 (on the disc, left commented out of CrowdMood.ini by its makers) at
//     once and louder; in a close game's last 60 s the game's claps come faster and faster (2.4 s -> 1.0 s apart), the
//     last 10 s add the anticipation swell every 3 s
//   - at 90+ (MAX): the stand's crowd animation runs fast (CrowdManager's own speed, as its Happy state does), your
//     controllers buzz gently with the stomp-stomp-clap (if rumble is on in the game's options), the gauge glows
// THE GAUGE: a 12-segment bar under the score bar, in the game's own 2D overlay view (after the players, before the HUD,
// so the game's HUD art is never covered): on the big screen and on the Tabletop scoreboard (its crop of the picture).
// No text on it (the >= 40 px text rule does not come into it).

#include "port/stadium_pulse.h"
#include "port/edition.h"

#include "Game/Audio/AudioScriptEventMgr.h"
#include "Game/Audio/WorldAudio.h"
#include "Game/Game.h"
#include "Game/Team.h"
#include "Game/Render/CrowdManager.h"
#include "NL/gl/gl.h"
#include "NL/gl/glDraw2.h"
#include "NL/gl/glState.h"
#include "NL/platpad.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace Audio
{
extern bool gbGameIsPaused;
}
extern bool g_bWorldSFXInitialized; // audio.cpp, global
extern "C" float PortAudioGetCrowdUser(void);  // r172 audio_mix.cpp: your crowd volume (1 = the default)
extern "C" void PortXrPulseBuzz(float amplitude, float seconds);  // r172 xr_quest.cpp (a no-op off the headset)

namespace
{

// The real indices of gWorldSoundTable (living_crowd.cpp: the WorldAudio.h names are 6 off).
enum
{
    kGoalHome = 146,
    kGoalAway = 147, // the game's own away-goal crowd: the Boo_Big_01 sample
    kYeahBig = 154,
    kYeahSmall1 = 155, // ..157
    kAntBig = 158,
    kAntSmall = 159,
    kOhBig1 = 160,   // ..162
    kOhSmall1 = 163, // ..166
    kClapBig = 167,
    kClapSmall = 168,
    kJeerBig = 169,
    kJeerSmall = 170,
    kBooSmall1 = 171, // ..173
    kBooBig1 = 174,   // ..176
};

// ------------------------------------------------------------------------------------------------ the pulse (shared)
// Everything in this block is the match's: the same on both headsets. tools/outside/pulse_sim172.py is its line-for-line
// PC copy (the proof chart); keep the two the same.
enum Kind
{
    K_HOME_SHOT,
    K_AWAY_SHOT,
    K_HOME_CHANCE,
    K_AWAY_CHANCE,
    K_HOME_SUPER,
    K_AWAY_SUPER,
    K_HOME_SAVE, // your keeper stopped them
    K_AWAY_SAVE, // their keeper stopped you
    K_HOME_HIT,
    K_AWAY_HIT,
    K_HOME_MISS,
    K_AWAY_MISS,
    K_HOME_POST,
    K_AWAY_POST,
    K_COUNT
};
const int kDelta[K_COUNT] = {4000, 2000, 3000, 2000, 7000, 3000, 6000, 2000, 3000, 1000, 1000, 1000, 4000, 1000};
const int kCool[K_COUNT] = {5, 5, 10, 10, 20, 20, 5, 5, 10, 10, 5, 5, 5, 5};  // tenths between two of a kind

struct Core
{
    int p;            // milli-points, 0..100000
    int tick;         // the last tenth of game time processed
    int coolUntil[K_COUNT];
    int superUntil;   // a goal before this tenth was a Super Strike's (a Super Strike started just before)
    int home, away;   // the score as last seen
};
Core s_core;
bool s_live = false;   // a match is on (Plus)
const cGame* s_game = NULL;

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int game_tick() { return (int)(g_pGame->GetGameTime() * 10.0f); }
int duration_ticks() { return (int)(g_pGame->m_fGameDuration * 10.0f); }
int score(int side) { return g_pTeams[side] != NULL ? g_pTeams[side]->m_nScore : 0; }

int core_base(int home, int away, int tick, int durTicks, int sudden)
{
    if (sudden)
        return 88000;
    const int d = home - away;
    const int t = durTicks > 0 ? clampi(tick * 1000 / durTicks, 0, 1000) : 0;
    const int rem = durTicks - tick;
    if (d >= -1 && d <= 1)
        return 45000 + 25 * t + (rem <= 600 ? 12000 : 0);
    if (d == 2)
        return 52000;
    if (d >= 3)
        return clampi(42000 - 3000 * (d - 3), 30000, 42000);
    if (d == -2)
        return 34000;
    return clampi(24000 - 4000 * (-d - 3), 12000, 24000);
}

void core_reset(int tick)
{
    memset(&s_core, 0, sizeof s_core);
    s_core.p = 45000;
    s_core.tick = tick;
    s_core.superUntil = -1;
}

void core_add(int delta, int tick, int durTicks)
{
    if (delta > 0 && durTicks > 0 && tick * 4 >= durTicks * 3)
        delta = delta * 5 / 4;  // the last quarter
    s_core.p = clampi(s_core.p + delta, 0, 100000);
}

// Catch the pulse up to the clock (a tenth at a time), then note the score.
void core_advance(int now)
{
    // r172: only what both headsets saw at the same point of the match: the score as of the last goal (PortPulseGoal
    // catches up to its tenth first) and the clock (past full time = sudden death), never the live score mid-catch-up
    const int dur = duration_ticks();
    const int home = s_core.home, away = s_core.away;
    int steps = now - s_core.tick;
    if (steps > 600)
        steps = 600;  // a jump (a menu, a load): no more than a minute's drift
    for (int i = 0; i < steps; i++)
    {
        const int tick = s_core.tick + 1 + i;
        const int base = core_base(home, away, tick, dur, dur > 0 && tick >= dur ? 1 : 0);
        s_core.p += (base - s_core.p) / 150;  // truncates toward zero (the sim does the same)
    }
    if (now > s_core.tick)
        s_core.tick = now;
}

// The match's clock, or a new match: true when the pulse is live.
bool core_sync()
{
    if (!PortPlusFeatures() || g_pGame == NULL || g_pTeams[0] == NULL || g_pTeams[1] == NULL)
    {
        s_live = false;
        return false;
    }
    const int now = game_tick();
    if (!s_live || s_game != g_pGame || now + 5 < s_core.tick ||
        (now < 20 && s_core.tick > 50 && score(0) == 0 && score(1) == 0))
    {
        core_reset(now);
        s_core.home = score(0);
        s_core.away = score(1);
        s_game = g_pGame;
        s_live = true;
        fprintf(stderr, "[pulse] r172: new match, pulse 45 (home crowd = side 0)\n");
    }
    core_advance(now);
    return true;
}

int core_kind(int event, int team)
{
    typedef AudioScriptEventMgr A;
    const bool home = team == A::AET_Home;
    const bool away = team == A::AET_Away;
    if (!home && !away)
        return -1;
    switch (event)
    {
    case A::AE_Shot:
        return home ? K_HOME_SHOT : K_AWAY_SHOT;
    case A::AE_GoodPosition:
    case A::AE_WindUp:
        return home ? K_HOME_CHANCE : K_AWAY_CHANCE;
    case A::AE_ShootToScore:
    case A::AE_CaptainS2S:
    case A::AE_HyperStrike:
        return home ? K_HOME_SUPER : K_AWAY_SUPER;
    case A::AE_GoalieSave:
        // AudioScriptEventMgr.cpp xors the keeper's side with AET_Special: Away = the home keeper saved
        return away ? K_HOME_SAVE : K_AWAY_SAVE;
    case A::AE_HitPlayer:
        // the same xor: Away = a home player put an away one down
        return away ? K_HOME_HIT : K_AWAY_HIT;
    case A::AE_MissShot:
        return home ? K_HOME_MISS : K_AWAY_MISS;
    case A::AE_HitPost:
        return home ? K_HOME_POST : K_AWAY_POST;
    default:
        return -1;
    }
}

// ------------------------------------------------------------------------------------------------ the crowd (local)
// What this headset plays and shows from the pulse: sounds, the gain, the buzz, the gauge. Free to differ per headset
// (nothing reads it back); the timing still follows the pulse and the clock.
float s_cool = 0.0f;       // until the next reaction may play
unsigned s_pick = 0;       // which variant next (a counter: no random)
float s_lateClap = 0.0f;   // until the next clap of a close finish
float s_lateAnt = 0.0f;    // until the next swell of its last 10 s
float s_beat = 0.0f;       // the stomp-stomp-clap's phase (s), for the buzz and the glow
int s_beatStep = 0;
float s_chantGap = 0.0f;   // since the last forced chant
unsigned s_chantNext = 0;  // which stomp-clap next
bool s_animTouched = false;
float s_shown = 45.0f;     // the gauge's eased value
float s_logIn = 0.0f;

struct Pending
{
    int sample;
    float at, volume;
};
Pending s_queue[6];  // a few timed sounds (the away goal's boo, jeer ...)
int s_queued = 0;

bool live_play() { return g_pGame != NULL && g_pGame->IsGameplayOrOvertime() && !Audio::gbGameIsPaused; }

float pulse01() { return s_core.p / 100000.0f; }

int gap_now() { return score(0) - score(1); }

bool close_finish()
{
    if (g_pGame == NULL)
        return false;
    const int d = gap_now();
    return d >= -1 && d <= 1 && duration_ticks() - game_tick() <= 600 && !g_pGame->mInSuddenDeath;
}

bool rhythm_wanted()
{
    if (!s_live || g_pGame == NULL || !g_pGame->IsGameplayOrOvertime())
        return false;
    return s_core.p >= 75000 || close_finish() || g_pGame->mInSuddenDeath;
}

void play(int sample, float volume)
{
    if (!::g_bWorldSFXInitialized)
        return;
    volume = volume > 1.0f ? 1.0f : (volume < 0.0f ? 0.0f : volume);
    Audio::gCrowdSFX.Play((Audio::eWorldSFX)sample, volume, -1.0f, false, 100.0f);
}

void queue(int sample, float in, float volume)
{
    if (s_queued < (int)(sizeof s_queue / sizeof s_queue[0]))
    {
        s_queue[s_queued].sample = sample;
        s_queue[s_queued].at = in;
        s_queue[s_queued].volume = volume;
        s_queued++;
    }
}

void react(int kind)
{
    // r172: a shot's outcome (groan, relief, the save's applause) is never held back by the shot's own "oh"
    const bool outcome = kind == K_HOME_MISS || kind == K_HOME_POST || kind == K_AWAY_SAVE || kind == K_HOME_SAVE ||
                         kind == K_AWAY_MISS || kind == K_AWAY_POST;
    if (!live_play() || (s_cool > 0.0f && !outcome))
        return;
    const float v = 0.45f + 0.5f * pulse01();
    int sample = -1;
    switch (kind)
    {
    case K_HOME_CHANCE:
    case K_HOME_SUPER:
        sample = kAntBig;  // the "ooooh" as you close in
        break;
    case K_AWAY_CHANCE:
        sample = kAntSmall;
        break;
    case K_AWAY_SUPER:
        sample = (s_pick & 1) ? kJeerSmall : kBooSmall1 + (int)(s_pick % 3);
        break;
    case K_HOME_MISS:
    case K_HOME_POST:
    case K_AWAY_SAVE:
        sample = kOhBig1 + (int)(s_pick % 3);  // the groan
        break;
    case K_HOME_SAVE:
        sample = kClapBig;
        queue(kYeahSmall1 + (int)(s_pick % 3), 0.5f, v * 0.8f);
        break;
    case K_AWAY_MISS:
    case K_AWAY_POST:
        sample = kYeahSmall1 + (int)(s_pick % 3);  // relief
        break;
    case K_HOME_HIT:
        sample = kYeahSmall1 + (int)(s_pick % 3);
        break;
    case K_AWAY_HIT:
        sample = kBooSmall1 + (int)(s_pick % 3);
        break;
    case K_HOME_SHOT:
        if (s_core.p < 55000)
            return;  // a shot's own sound is the game's; a rocking section adds a small "oh"
        sample = kOhSmall1 + (int)(s_pick % 4);
        break;
    default:
        return;
    }
    ++s_pick;
    s_cool = close_finish() ? 0.6f : 0.9f;
    play(sample, v);
}

void log_pulse(const char* why)
{
    fprintf(stderr, "[pulse] t %.1f  %d-%d  pulse %d.%d  %s\n", s_core.tick / 10.0f, score(0), score(1), s_core.p / 1000,
            (s_core.p / 100) % 10, why);
}

const char* const kKindNames[K_COUNT] = {"home shot", "away shot", "home chance", "away chance", "home super strike",
                                          "away super strike", "home save", "away save", "home hit", "away hit",
                                          "home miss", "away miss", "home post", "away post"};

const char* const kStompClap[7] = {
    "audio/data/Streams/Chants DSP Files/CHANTS_GEN_Stomp_Clap_01.idsp",
    "audio/data/Streams/Chants DSP Files/CHANTS_GEN_Stomp_Clap_02.idsp",
    "audio/data/Streams/Chants DSP Files/CHANTS_GEN_Stomp_Clap_03.idsp",
    "audio/data/Streams/Chants DSP Files/CHANTS_GEN_Stomp_Clap_04.idsp",
    "audio/data/Streams/Chants DSP Files/CHANTS_GEN_Stomp_Clap_05.idsp",
    "audio/data/Streams/Chants DSP Files/CHANTS_GEN_Stomp_Clap_06.idsp",
    "audio/data/Streams/Chants DSP Files/CHANTS_GEN_Stomp_Clap_07.idsp",
};

} // namespace

// ================================================================================================ the C API
extern "C" void PortPulseEvent(int event, int team)
{
    if (!core_sync())
        return;
    if (g_pGame == NULL || !g_pGame->IsGameplayOrOvertime())
        return;
    const int kind = core_kind(event, team);
    if (kind < 0)
        return;
    const int now = s_core.tick;
    if (now < s_core.coolUntil[kind])
        return;
    s_core.coolUntil[kind] = now + kCool[kind];
    if (kind == K_HOME_SUPER || kind == K_AWAY_SUPER)
        s_core.superUntil = now + 80;
    core_add(kDelta[kind], now, duration_ticks());
    log_pulse(kKindNames[kind]);
    react(kind);
}

extern "C" void PortPulseGoal(int side)
{
    if (!core_sync())
        return;
    const int now = s_core.tick, dur = duration_ticks();
    const int home = score(0), away = score(1);
    const int before = side == 0 ? s_core.home : s_core.away;
    const int after = side == 0 ? home : away;
    const bool super = after - before >= 2 || now < s_core.superUntil;
    const int dBefore = s_core.home - s_core.away, d = home - away;
    if (side == 0)
    {
        int delta = 28000;
        if (dBefore < 0 && d >= 0)
            delta += 8000;  // ties it / takes the lead from behind
        if (dur > 0 && now * 4 >= dur * 3 && d >= -1 && d <= 1)
            delta += 10000;  // a late goal that leaves it close
        if (super)
            delta += 10000;
        core_add(delta, now, dur);
    }
    else
    {
        int delta = -18000;
        if (d <= -3)
            delta -= 8000;
        core_add(delta, now, dur);
    }
    s_core.home = home;
    s_core.away = away;
    s_core.superUntil = -1;
    log_pulse(side == 0 ? "HOME GOAL" : "away goal");
    // the crowd: the home roar is living_crowd.cpp's (PortCrowdRoarStart reads the pulse); an away goal is booed
    if (side != 0)
    {
        const float v = 0.6f + 0.4f * pulse01();
        play(kGoalAway, v);
        queue(kBooBig1 + (int)(s_pick % 3), 1.2f, v);
        queue(kJeerBig, 2.6f, v * 0.9f);
        ++s_pick;
        s_cool = 3.5f;
    }
}

extern "C" void PortPulseUpdate(float dt)
{
    const bool on = core_sync();
    if (!on)
    {
        if (s_animTouched && CrowdManager::instance.m_State == Crowd_Idle)
            CrowdManager::instance.m_fAnimScale = 1.0f;
        s_animTouched = false;
        return;
    }
    if (dt <= 0.0f || dt > 0.25f)
        dt = dt <= 0.0f ? 0.0f : 0.25f;
    if (s_cool > 0.0f)
        s_cool -= dt;
    s_chantGap += dt;

    // the timed sounds
    if (!Audio::gbGameIsPaused)
        for (int i = 0; i < s_queued;)
        {
            s_queue[i].at -= dt;
            if (s_queue[i].at <= 0.0f)
            {
                play(s_queue[i].sample, s_queue[i].volume);
                s_queue[i] = s_queue[--s_queued];
            }
            else
                i++;
        }

    const float p = pulse01();
    const bool live = live_play();
    // a close finish: claps faster and faster, then the swell every 3 s in the last 10
    if (live && close_finish())
    {
        const float rem = (duration_ticks() - game_tick()) / 10.0f;
        const float k = rem < 0.0f ? 1.0f : 1.0f - rem / 60.0f;
        s_lateClap -= dt;
        if (s_lateClap <= 0.0f)
        {
            s_lateClap = 2.4f - 1.4f * (k > 1.0f ? 1.0f : k);
            play((s_pick++ % 3) == 2 ? kClapBig : kClapSmall, 0.45f + 0.45f * p);
        }
        if (rem <= 10.0f)
        {
            s_lateAnt -= dt;
            if (s_lateAnt <= 0.0f)
            {
                s_lateAnt = 3.0f;
                play(kAntBig, 0.6f + 0.4f * p);
            }
        }
    }
    else
    {
        s_lateClap = 0.5f;
        s_lateAnt = 0.0f;
    }

    // MAX: the stomp-stomp-clap in your hands (gentle), the stands' animation fast
    s_beat += dt;
    if (s_beat >= 1.45f)
    {
        s_beat -= 1.45f;
        s_beatStep = 0;
    }
    if (live && s_core.p >= 90000 && !cPlatPad::m_bDisableRumble)
    {
        static const float kAt[3] = {0.0f, 0.36f, 0.72f};
        static const float kAmp[3] = {0.22f, 0.18f, 0.12f};
        while (s_beatStep < 3 && s_beat >= kAt[s_beatStep])
        {
            PortXrPulseBuzz(kAmp[s_beatStep], 0.07f);
            s_beatStep++;
        }
    }
    else
        s_beatStep = 3;
    if (CrowdManager::instance.m_State == Crowd_Idle)
    {
        float scale = 1.0f;
        if (live)
            scale = s_core.p >= 50000 ? 1.0f + 0.8f * (s_core.p - 50000) / 50000.0f : (s_core.p < 25000 ? 0.85f : 1.0f);
        CrowdManager::instance.m_fAnimScale = scale;
        s_animTouched = true;
    }

    s_logIn -= dt;
    if (s_logIn <= 0.0f && live)
    {
        s_logIn = 5.0f;
        log_pulse(rhythm_wanted() ? "(rhythm)" : "");
    }
}

extern "C" int PortPulseValue(void) { return s_live ? s_core.p / 1000 : -1; }

extern "C" float PortPulseCrowdGain(void)
{
    if (!s_live)
        return 1.0f;
    float g = 0.6f + 1.0f * pulse01();
    if (close_finish())
        g += 0.1f;
    return g > 1.7f ? 1.7f : g;
}

extern "C" const char* PortPulseChantPick(void)
{
    if (!rhythm_wanted())
        return NULL;
    const char* f = kStompClap[s_chantNext % 7];
    s_chantNext++;
    fprintf(stderr, "[pulse] chant: warming %s\n", f + 36);
    return f;
}

extern "C" int PortPulseChantNow(float* volume)
{
    if (!rhythm_wanted() || !live_play() || s_chantGap < 3.0f)
        return 0;
    if (volume != NULL)
        *volume = (0.85f + 0.55f * pulse01()) * PortAudioGetCrowdUser();
    return 1;
}

extern "C" void PortPulseChantStarted(void)
{
    s_chantGap = 0.0f;
    log_pulse("chant stream started (rhythm)");
}

// ------------------------------------------------------------------------------------------------ the gauge
namespace
{
unsigned char byte01(float f) { return (unsigned char)(f <= 0.0f ? 0 : (f >= 1.0f ? 255 : (int)(f * 255.0f + 0.5f))); }

void rect(float x, float y, float w, float h, const float* top, const float* bottom, float aTop, float aBottom)
{
    glPoly2 poly;
    poly.SetupRectangle(x, y, w, h, -1.0f);
    for (int i = 0; i < 4; i++)
    {
        const float t = h > 0.0f ? (poly.m_pos[i].y - y) / h : 0.0f;  // 0 top .. 1 bottom
        const float k = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        for (int c = 0; c < 3; c++)
            poly.m_colour[i].c[c] = byte01(top[c] + (bottom[c] - top[c]) * k);
        poly.m_colour[i].c[3] = byte01(aTop + (aBottom - aTop) * k);
    }
    poly.Attach(GLV_UnsortedOrtho, 0, NULL, (unsigned long)-1);
}

// cool blue -> gold -> white-hot orange-red along the bar
void ramp(float u, float* c)
{
    static const float k[3][3] = {{0.22f, 0.55f, 1.0f}, {1.0f, 0.80f, 0.16f}, {1.0f, 0.30f, 0.10f}};
    const int i = u < 0.5f ? 0 : 1;
    const float f = u < 0.5f ? u / 0.5f : (u - 0.5f) / 0.5f;
    for (int j = 0; j < 3; j++)
        c[j] = k[i][j] + (k[i + 1][j] - k[i][j]) * f;
}
} // namespace

extern "C" void PortStadiumPulseRender(float dt)
{
    if (!s_live || g_pGame == NULL || !g_pGame->IsGameplayOrOvertime())
        return;
    if (dt < 0.0f || dt > 0.1f)
        dt = dt < 0.0f ? 0.0f : 0.1f;
    const float target = s_core.p / 1000.0f;
    s_shown += (target - s_shown) * (dt * 4.0f > 1.0f ? 1.0f : dt * 4.0f);
    const float W = glGetOrthographicWidth(), H = glGetOrthographicHeight();
    if (W <= 0.0f || H <= 0.0f)
        return;
    // under the score bar, inside the strip the Tabletop scoreboard crops (xr_quest.cpp: 3% .. 14% of the height)
    const float w = 0.20f * W, h = 0.020f * H;
    const float x = 0.5f * (W - w), y = 0.116f * H;
    const bool atMax = s_core.p >= 90000;
    // the stomp-stomp-clap's beat, for the glow
    const float beat = s_beat < 0.12f ? 1.0f - s_beat / 0.12f
                       : (s_beat >= 0.36f && s_beat < 0.48f) ? 1.0f - (s_beat - 0.36f) / 0.12f
                       : (s_beat >= 0.72f && s_beat < 0.84f) ? 0.6f * (1.0f - (s_beat - 0.72f) / 0.12f)
                                                              : 0.0f;

    glSetDefaultState(true);
    glSetRasterState(GLS_DepthTest, 0);
    glSetRasterState(GLS_DepthWrite, 0);
    glSetRasterState(GLS_Culling, 0);
    glSetCurrentTexture(glGetTexture("global/white"), GLTT_Diffuse);
    glSetCurrentTextureState(glHandleizeTextureState());

    if (atMax)
    {
        // the glow: additive halos round the bar, breathing with the stomps
        glSetRasterState(GLS_AlphaBlend, 2);
        glSetCurrentRasterState(glHandleizeRasterState());
        const float hot[3] = {1.0f, 0.42f, 0.10f};
        const float g = 0.25f + 0.45f * beat;
        for (int r = 3; r >= 1; r--)
        {
            const float e = 2.2f * r;
            const float a = g / (float)(r + 1);
            const float c[3] = {hot[0] * a, hot[1] * a, hot[2] * a};
            rect(x - e, y - e, w + 2.0f * e, h + 2.0f * e, c, c, 1.0f, 1.0f);
        }
    }
    glSetRasterState(GLS_AlphaBlend, 1);
    glSetCurrentRasterState(glHandleizeRasterState());
    // the casing: a dark capsule with a bevel (lighter top edge, darker bottom), ends stepped in to round it
    const float edgeTop[3] = {0.36f, 0.40f, 0.58f}, edgeBot[3] = {0.05f, 0.06f, 0.12f};
    const float bodyTop[3] = {0.10f, 0.11f, 0.20f}, bodyBot[3] = {0.03f, 0.03f, 0.07f};
    rect(x + 1.0f, y - 1.6f, w - 2.0f, h + 3.2f, edgeTop, edgeBot, 0.92f, 0.92f);
    rect(x - 0.6f, y - 0.6f, w + 1.2f, h + 1.2f, edgeTop, edgeBot, 0.92f, 0.92f);
    rect(x + 0.6f, y, w - 1.2f, h, bodyTop, bodyBot, 0.95f, 0.95f);
    // the 12 segments
    const int kSeg = 12;
    const float pad = 1.4f, gap = 1.0f;
    const float sw = (w - 2.0f * pad - (kSeg - 1) * gap) / kSeg, sh = h - 2.0f * pad;
    const float fill = s_shown / 100.0f * kSeg;
    for (int i = 0; i < kSeg; i++)
    {
        const float sx = x + pad + i * (sw + gap), sy = y + pad;
        const float lit = fill - i > 1.0f ? 1.0f : (fill - i < 0.0f ? 0.0f : fill - i);
        float c[3];
        ramp((i + 0.5f) / kSeg, c);
        if (lit > 0.0f)
        {
            const float boost = atMax ? 0.25f * beat : 0.0f;
            const float top[3] = {c[0] + (1.0f - c[0]) * (0.45f + boost), c[1] + (1.0f - c[1]) * (0.45f + boost),
                                  c[2] + (1.0f - c[2]) * (0.45f + boost)};
            const float bot[3] = {c[0] * 0.78f, c[1] * 0.78f, c[2] * 0.78f};
            rect(sx, sy, sw * lit, sh, top, bot, 1.0f, 1.0f);
            // the shine along its top third
            const float white[3] = {1.0f, 1.0f, 1.0f};
            rect(sx, sy, sw * lit, sh * 0.32f, white, white, 0.38f, 0.0f);
        }
        if (lit < 1.0f)
        {
            const float offTop[3] = {0.16f, 0.18f, 0.30f}, offBot[3] = {0.09f, 0.10f, 0.18f};
            rect(sx + sw * lit, sy, sw * (1.0f - lit), sh, offTop, offBot, 0.9f, 0.9f);
        }
    }
    glSetDefaultState(false);
}
