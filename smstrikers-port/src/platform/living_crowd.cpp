// strikers-quest r42: the living crowd and the goal roar (Lance, after alpha 1.2; VISION.md "stadium
// pulse"). The crowd follows the match:
//   - a close game (a goal or less in it) is loud and gets louder as the clock runs down, and the
//     crowd reacts to every shot, hit, item and save; the last fifth of a close game adds claps and
//     anticipation;
//   - a blowout (three goals or more) goes quiet and bored, and each goal is cheered less;
//   - after every goal the goal roar keeps going for up to ten seconds.
// Everything here is decided from the match itself (scores, clock, the game's own audio events) and
// plays the game's own crowd samples by number, never through the shared random seed, so both
// headsets of an online match make the same calls. The loudness is applied in the mixer
// (audio_mix.cpp, PortAudioSetCrowdLiving), together with your own crowd volume and, in the stadium,
// where each voice comes from.
// Sample numbers: the real indices of gWorldSoundTable (the WorldAudio.h names are 6 off).

#include "port/audio.h"
#include "port/edition.h"
#include "port/living_crowd.h"
#include "port/stadium_pulse.h" // strikers-quest r172: the stadium pulse drives the living crowd (Plus)

#include "Game/Audio/AudioScriptEventMgr.h"
#include "Game/Audio/WorldAudio.h"
#include "Game/Game.h"
#include "Game/Team.h"

#include <math.h>
#include <stdio.h>

namespace Audio
{
extern bool gbGameIsPaused;
}
extern bool g_bWorldSFXInitialized; // audio.cpp, global (not WorldAudio.h's Audio:: one)

namespace
{

enum
{
    kGoalHome = 146,
    kGoalAway = 147,
    kYeahBig = 154,
    kYeahSmall1 = 155, // ..157
    kAntBig = 158,
    kAntSmall = 159,
    kOhBig1 = 160,   // ..162
    kOhSmall1 = 163, // ..166
    kClapBig = 167,
    kClapSmall = 168,
};

float s_intensity = 0.5f; // 0 bored .. 1 on its feet
float s_cooldown = 0.0f;  // until the next extra reaction may play
float s_tension = 0.0f;   // until the next clap/anticipation in a close finish
unsigned s_pick = 0;      // which variant next (no random: both headsets agree)

int s_roarSide = 0;
float s_roarLeft = 0.0f;
float s_roarNext = 0.0f;
float s_roarVolume = 1.0f;
unsigned s_roarCount = 0;

bool live_play()
{
    return g_pGame != NULL && g_pGame->IsGameplayOrOvertime() && !Audio::gbGameIsPaused;
}

int score_gap()
{
    if (g_pTeams[0] == NULL || g_pTeams[1] == NULL)
        return 0;
    const int d = g_pTeams[0]->m_nScore - g_pTeams[1]->m_nScore;
    return d < 0 ? -d : d;
}

float clock_fraction()
{
    if (g_pGame == NULL || g_pGame->m_fGameDuration <= 0.0f)
        return 0.0f;
    const float f = g_pGame->GetGameTime() / g_pGame->m_fGameDuration;
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

// What the crowd should feel right now, from the score and the clock.
float target_intensity()
{
    if (g_pGame == NULL)
        return 0.5f;
    if (g_pGame->mInSuddenDeath)
        return 1.0f;
    const int gap = score_gap();
    const float t = clock_fraction();
    if (gap <= 1)
        return (gap == 0 ? 0.65f : 0.6f) + 0.35f * t;
    if (gap == 2)
        return 0.45f;
    const float bored = 0.32f - 0.06f * (float)(gap - 3);
    return bored < 0.12f ? 0.12f : bored;
}

void play(int sample, float volume)
{
    if (!::g_bWorldSFXInitialized)
        return;
    if (volume > 1.0f)
        volume = 1.0f;
    Audio::gCrowdSFX.Play((Audio::eWorldSFX)sample, volume, -1.0f, false, 100.0f);
}

} // namespace

void PortLivingCrowdUpdate(float dt)
{
    // strikers-quest r43: Classic is alpha 1.2's crowd: the game's own loudness (1, which with your
    // crowd volume at its default 6 leaves the mixer's crowd voices untouched) and nothing played here.
    if (!PortPlusFeatures())
    {
        PortAudioSetCrowdLiving(1.0f);
        return;
    }
    if (dt <= 0.0f)
        return;
    // Follows the match over a few seconds rather than jumping at a goal.
    const float target = target_intensity();
    const float k = dt * 0.4f > 1.0f ? 1.0f : dt * 0.4f;
    s_intensity += (target - s_intensity) * k;
    // strikers-quest r172: the stadium pulse (stadium_pulse.cpp) sets the loudness and plays the close finish while a
    // match is on; r42's intensity stays for the roar's length off the pulse.
    PortPulseUpdate(dt);
    const bool pulse = PortPulseValue() >= 0;
    // Lance (r42 play): "i didnt notice anything different" - a wider range than r42's 0.75-1.3.
    PortAudioSetCrowdLiving(pulse ? PortPulseCrowdGain() : 0.6f + 1.0f * s_intensity);

    if (s_cooldown > 0.0f)
        s_cooldown -= dt;

    // The goal roar: the game's goal cheer again every 1.8 s, with a big "yeah" in between.
    if (s_roarLeft > 0.0f && !Audio::gbGameIsPaused)
    {
        s_roarLeft -= dt;
        s_roarNext -= dt;
        if (s_roarNext <= 0.0f && s_roarLeft > 0.0f)
        {
            // The goal cheer every time, a big "yeah" over every other one.
            s_roarNext = 1.6f;
            play(s_roarSide == 0 ? kGoalHome : kGoalAway, s_roarVolume);
            if ((s_roarCount++ & 1) == 0)
                play(kYeahBig, s_roarVolume);
        }
    }

    // A close finish: claps and anticipation every few seconds through the last fifth.
    if (!pulse && live_play() && score_gap() <= 1 && clock_fraction() > 0.8f) // r172: the pulse's own when it is on
    {
        s_tension -= dt;
        if (s_tension <= 0.0f)
        {
            s_tension = 3.5f;
            play((s_pick++ & 1) == 0 ? kClapBig : kAntSmall, 0.55f + 0.35f * s_intensity);
        }
    }
    else
    {
        s_tension = 1.0f;
    }
}

void PortLivingCrowdEvent(int event, int team)
{
    (void)team;
    if (!PortPlusFeatures())
        return; // strikers-quest r43: no extra reactions in Classic
    typedef AudioScriptEventMgr A;
    if (event == A::AE_Goal || event == A::AE_SDWin)
        return; // the roar starts from PortCrowdRoarStart
    // strikers-quest r172: the stadium pulse takes the moment and the home crowd's reaction to it
    PortPulseEvent(event, team);
    if (PortPulseValue() >= 0)
        return;
    // Only a game worth watching gets the extra noise.
    if (!live_play() || s_intensity < 0.55f || s_cooldown > 0.0f)
        return;
    int sample = -1;
    switch (event)
    {
    case A::AE_Shot:
    case A::AE_MissShot:
    case A::AE_HitPost:
        sample = kOhBig1 + (int)(s_pick % 3);
        break;
    case A::AE_GoalieSave:
        sample = (s_pick & 1) ? kOhBig1 + (int)(s_pick % 3) : kClapBig;
        break;
    case A::AE_HitPlayer:
    case A::AE_Attack:
        sample = kOhSmall1 + (int)(s_pick % 4);
        break;
    case A::AE_PowerUpActivate:
    case A::AE_PowerUpHit:
        sample = kYeahSmall1 + (int)(s_pick % 3);
        break;
    case A::AE_PerfectPass:
        sample = kClapSmall;
        break;
    case A::AE_WindUp:
    case A::AE_ShootToScore:
    case A::AE_CaptainS2S:
        sample = kAntBig;
        break;
    default:
        return;
    }
    ++s_pick;
    s_cooldown = clock_fraction() > 0.8f ? 0.6f : 0.9f;
    play(sample, 0.35f + 0.55f * (s_intensity - 0.55f) / 0.45f);
}

void PortCrowdRoarStart(int side)
{
    // Blowouts get a shorter, quieter roar: the gap after this goal.
    const int gap = score_gap();
    const float fade = gap >= 3 ? 1.0f - 0.2f * (float)(gap - 2) : 1.0f;
    const float k = fade < 0.35f ? 0.35f : fade;
    s_roarSide = side;
    s_roarLeft = 10.0f * k;
    s_roarNext = 0.7f;
    s_roarVolume = 0.6f + 0.4f * k;
    s_roarCount = 0;
    // strikers-quest r172: with the stadium pulse on, the crowd is the HOME club's: a home goal's roar runs 6-12 s and
    // as loud as the pulse (after the goal's push); an away goal is booed (stadium_pulse.cpp), no roar.
    if (PortPlusFeatures())
    {
        PortPulseGoal(side);
        const int pulse = PortPulseValue();
        if (pulse >= 0)
        {
            const float p = pulse / 100.0f;
            s_roarLeft = side == 0 ? (6.0f + 6.0f * p) * (k < 0.6f ? 0.6f : k) : 0.0f;
            s_roarVolume = 0.65f + 0.35f * p;
        }
    }
    fprintf(stderr, "[port] crowd: roar for %.1f s (gap %d, intensity %.2f)\n", s_roarLeft, gap, s_intensity);
}
