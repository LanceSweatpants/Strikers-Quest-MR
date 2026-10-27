// strikers-quest r68: guest characters' own voices (include/port/xr_guest.h).
//
// Every character sound goes through cGameSFX::Play (GameAudio.cpp). For a voice (volume group
// 5..19, the game's own test for dialogue) of a player who is a guest, the game's sound still plays,
// silently (its bookkeeping, emitters and cut-offs all go on as ever), and the guest's own clip of
// the same kind (STRIKERS_HOME/guests/voices/<pack>.gvo, tools/outside/voicepack.py: from the
// player's own discs) starts here instead, mixed into the port's software mixer (audio_mix.cpp,
// 32 kHz stereo, on the game thread) at the game's voice volume, panned and faded by where the
// player is from the camera, as the game's 3D voices are. Local only: nothing of it is simulated.

#include "port/xr_guest.h"

#include "Game/Character.h"
#include "Game/Player.h"
#include "Game/Team.h"
#include "Game/Sys/audio.h"
#include "NL/plat/plataudio.h"

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

extern f32 gfVolumeGroups[23];  // audio.cpp: per-group gains (StreamTrack.h)
namespace Audio
{
extern bool gbGameIsPaused;  // audio.cpp
}

namespace
{

const int kRate = 32000;  // the port mixer's rate (audio_out.cpp)

struct Clip
{
    std::string label;
    std::vector<short> pcm;  // mono, 32 kHz
};

struct Bank
{
    bool tried = false;
    std::vector<Clip> clips;
};

struct Playing
{
    const Clip* clip = NULL;
    size_t pos = 0;
    float gainL = 0.0f, gainR = 0.0f;
    const void* owner = NULL;
};

// A guest player's sound object: which guest, and the voice volumes its table had before they
// were set to nothing (the guest's clip plays at them).
struct Owner
{
    int guest = -1;
    std::vector<float> volume;
    unsigned lastCallMs = 0;
    bool refresh = false;  // its table was set again (SetSFX): look again at who it is and its volumes
};

std::unordered_map<int, Bank> s_banks;
std::unordered_map<const void*, Owner> s_owners;
const int kVoices = 6;
Playing s_playing[kVoices];
bool s_haveListener = false;
float s_listenPos[3], s_listenHeading[3], s_listenUp[3];
unsigned s_rng = 0x9E3779B9u;
unsigned s_msNow = 0;  // mixer time, ms (advanced by what was mixed)

unsigned next_rand()
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

std::string voice_dir()
{
    const char* home = getenv("STRIKERS_HOME");
    return std::string(home != NULL && *home != '\0' ? home : "/storage/emulated/0/Strikers") + "/guests/voices/";
}

// guests/voices/<pack>.gvo: 'GVO1', u32 count, then per clip label[24], u32 rate, u32 samples, s16[].
const Bank& bank_for(int guest)
{
    Bank& b = s_banks[guest];
    if (b.tried)
        return b;
    b.tried = true;
    const char* pack = PortGuestRosterPack(guest);
    if (pack == NULL)
        return b;
    const std::string path = voice_dir() + pack + ".gvo";
    FILE* f = fopen(path.c_str(), "rb");
    if (f == NULL)
        return b;
    char magic[4];
    unsigned count = 0;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "GVO1", 4) != 0 || fread(&count, 4, 1, f) != 1 || count > 512)
    {
        fclose(f);
        return b;
    }
    for (unsigned i = 0; i < count; i++)
    {
        char label[25] = {};
        unsigned rate = 0, n = 0;
        if (fread(label, 1, 24, f) != 24 || fread(&rate, 4, 1, f) != 1 || fread(&n, 4, 1, f) != 1 || rate < 4000 ||
            rate > 96000 || n > rate * 10u)
            break;
        std::vector<short> src(n);
        if (n != 0 && fread(src.data(), 2, n, f) != n)
            break;
        Clip c;
        c.label = label;
        // To the mixer's rate (linear: voice clips, a small step either way).
        const size_t out = (size_t)((double)n * kRate / rate);
        c.pcm.resize(out);
        for (size_t k = 0; k < out; k++)
        {
            const double at = (double)k * rate / kRate;
            const size_t i0 = (size_t)at;
            const double fr = at - (double)i0;
            const int a = i0 < n ? src[i0] : 0, bb = i0 + 1 < n ? src[i0 + 1] : a;
            c.pcm[k] = (short)(a + (bb - a) * fr);
        }
        b.clips.push_back(std::move(c));
    }
    fclose(f);
    fprintf(stderr, "[guest] %s's voice: %zu clips\n", pack, b.clips.size());
    fflush(stderr);
    return b;
}

// The guest's clips to try for a Strikers voice, by its type name, first that the guest has.
// Empty: nothing in its place (the breath loop, a wind-up grunt, the Super Strike float).
std::vector<const char*> labels_for(const char* type)
{
    std::vector<const char*> l;
    if (type == NULL)
        return l;
    auto has = [&](const char* s) { return strstr(type, s) != NULL; };
    if (has("BREATH") || has("SHOT_WINDUP") || has("STS_FLOAT") || has("HEAD_SHAKE"))
        return l;
    if (has("STS_JUMP"))
        l = {"jump", "attack"};
    else if (has("STS_KICK") || has("KICK_SUPER"))
        l = {"super", "special", "skillshot", "kick", "attack", "cry"};
    else if (has("ELECTROCUTE"))
        l = {"shock", "hurt_big", "ko", "hurt", "cry"};
    else if (has("GET_HIT"))
        l = {"hurt", "hurt_big", "dazed", "dizzy", "cry"};
    else if (has("PAIN"))
        l = {"hurt_big", "hurt", "ko", "cry"};
    else if (has("DAZED"))
        l = {"dazed", "dizzy", "hurt", "cry"};
    else if (has("EXERT") || has("PERFECT_PASS"))
        l = {"deke", "attack", "kick", "cry"};
    else if (has("EFFORTS_KICK"))
        l = {"kick", "special", "attack", "cry"};
    else if (has("EFFORTS_ATTACK") || has("EFFORTS_HIT"))
        l = {"attack", "kick", "special", "cry", "flap"};
    else if (has("CALL_"))
        l = {"select", "taunt", "quote", "cry"};
    else if (has("GOAL"))
        l = {"cheer", "taunt", "select", "quote", "cry", "attack", "flap"};
    else if (has("WIN") || has("END_GAME"))
        l = {"cheer", "quote", "taunt", "select", "cry", "flap"};
    else if (has("ENTER"))
        l = {"select", "taunt", "quote", "cry", "flap"};
    else if (has("ATTITUDE"))
        l = {"taunt", "select", "quote", "cry"};
    else
        l = {"attack", "cry"};
    return l;
}

// The player whose sound object this is, and the guest it plays as (-1 none).
int guest_of(const void* sfx, int* sideOut)
{
    for (int side = 0; side < 2; side++)
    {
        cTeam* team = g_pTeams[side];
        if (team == NULL)
            continue;
        for (int i = 0; i < 5; i++)
        {
            cPlayer* p = team->GetPlayer(i);
            if (p == NULL || (const void*)p->m_pCharacterSFX != sfx)
                continue;
            if (sideOut != NULL)
                *sideOut = side;
            return PortGuestIndexFor(side, (int)p->m_eCharacterClass);
        }
    }
    return -1;
}

}  // namespace

float PortGuestVoiceMute(const void* sfx, SoundStrToIDNode* table, unsigned long count, unsigned long type)
{
    if (sfx == NULL || table == NULL || type >= count)
        return -1.0f;
    auto it = s_owners.find(sfx);
    if (it == s_owners.end() || it->second.refresh)
    {
        if (it == s_owners.end())
            it = s_owners.emplace(sfx, Owner()).first;
        Owner& o = it->second;
        o.refresh = false;
        o.guest = guest_of(sfx, NULL);
        if (o.guest >= 0)
        {
            // Every voice of this player's table to nothing (the game re-reads the table for a
            // sound it keeps updating); the volumes kept for the guest's own clips. A table set
            // again (SetSFX: a new match, or the stadium's surfaces over the character's) brings
            // its own volumes back for what it set: those are taken, the rest kept from before.
            const size_t had = o.volume.size();
            if (had < count)
                o.volume.resize(count, 0.0f);
            for (unsigned long t = 0; t < count; t++)
            {
                if (table[t].volGrp < 5 || table[t].volGrp > 19)
                    continue;
                if (t >= had || table[t].fVolume != 0.0f)
                    o.volume[t] = table[t].fVolume;
                table[t].fVolume = 0.0f;
            }
        }
    }
    if (it->second.guest < 0)
        return -1.0f;
    return type < it->second.volume.size() ? it->second.volume[type] : 0.0f;
}

void PortGuestVoiceForget(const void* sfx)
{
    auto it = s_owners.find(sfx);
    if (it != s_owners.end())
        it->second.refresh = true;
    for (Playing& p : s_playing)
        if (p.owner == sfx)
            p.clip = NULL;
}

void PortGuestVoiceStart(const void* sfx, const char* type, int volGroup, float volume, const float* pos, float maxDist)
{
    auto it = s_owners.find(sfx);
    if (it == s_owners.end() || it->second.guest < 0)
        return;
    Owner& o = it->second;
    const std::vector<const char*> want = labels_for(type);
    if (want.empty())
        return;
    // Calls for the ball come often: one every few seconds at most.
    const bool call = strstr(type, "CALL_") != NULL;
    if (call && s_msNow - o.lastCallMs < 2500u && o.lastCallMs != 0)
        return;
    const Bank& b = bank_for(o.guest);
    std::vector<const Clip*> pick;
    for (const char* label : want)
    {
        for (const Clip& c : b.clips)
            if (c.label == label)
                pick.push_back(&c);
        if (!pick.empty())
            break;
    }
    if (pick.empty())
        return;
    if (call)
        o.lastCallMs = s_msNow + 1;
    const Clip* clip = pick[next_rand() % pick.size()];
    // Gain: the game's voice volume, this group's, the sound's own, by distance as its 3D voices are.
    float gain = Audio::MasterVolume::GetVoiceVolume() * volume;
    if (volGroup >= 5 && volGroup <= 19 && volGroup + 3 < 23)
        gain *= gfVolumeGroups[volGroup + 3];
    float pan = 0.0f;
    if (pos != NULL && s_haveListener)
    {
        const float d[3] = {pos[0] - s_listenPos[0], pos[1] - s_listenPos[1], pos[2] - s_listenPos[2]};
        const float dist = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (maxDist > 0.0f)
            gain *= dist >= maxDist ? 0.0f : 1.0f - dist / maxDist;
        // right = heading x up
        const float* h = s_listenHeading;
        const float* u = s_listenUp;
        float r[3] = {h[1] * u[2] - h[2] * u[1], h[2] * u[0] - h[0] * u[2], h[0] * u[1] - h[1] * u[0]};
        const float rl = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        if (rl > 1e-6f && dist > 1e-4f)
            pan = (d[0] * r[0] + d[1] * r[1] + d[2] * r[2]) / (rl * dist);
    }
    if (!(gain > 0.001f))
        return;
    pan = pan < -1.0f ? -1.0f : (pan > 1.0f ? 1.0f : pan);
    // One line at a time from one player: a new one cuts the last (as the game does).
    Playing* slot = NULL;
    for (Playing& p : s_playing)
        if (p.clip != NULL && p.owner == sfx)
            slot = &p;
    if (slot == NULL)
        for (Playing& p : s_playing)
            if (p.clip == NULL)
            {
                slot = &p;
                break;
            }
    if (slot == NULL)  // all busy: the one furthest along gives way
    {
        slot = &s_playing[0];
        for (Playing& p : s_playing)
            if (p.pos * slot->clip->pcm.size() > slot->pos * p.clip->pcm.size())
                slot = &p;
    }
    slot->clip = clip;
    slot->pos = 0;
    slot->owner = sfx;
    slot->gainL = gain * sqrtf(0.5f * (1.0f - pan));
    slot->gainR = gain * sqrtf(0.5f * (1.0f + pan));
}

void PortGuestVoiceListener(const float* pos, const float* heading, const float* up)
{
    for (int i = 0; i < 3; i++)
    {
        s_listenPos[i] = pos[i];
        s_listenHeading[i] = heading[i];
        s_listenUp[i] = up[i];
    }
    s_haveListener = true;
}

// r72: a guest says one of its own clips, not from a player (franchise nemesis taunts): the first of
// `labels` ("taunt,quote,cry") it has, in the middle, at the game's voice volume.
void PortGuestVoiceSay(int guest, const char* labels, float volume)
{
    static int s_sayOwner;
    if (guest < 0 || labels == NULL)
        return;
    const Bank& b = bank_for(guest);
    std::vector<const Clip*> pick;
    std::string all = labels;
    size_t at = 0;
    while (pick.empty() && at <= all.size())
    {
        const size_t comma = all.find(',', at);
        const std::string label = all.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
        for (const Clip& c : b.clips)
            if (c.label == label)
                pick.push_back(&c);
        if (comma == std::string::npos)
            break;
        at = comma + 1;
    }
    if (pick.empty())
        return;
    const float gain = Audio::MasterVolume::GetVoiceVolume() * volume;
    if (!(gain > 0.001f))
        return;
    Playing* slot = NULL;
    for (Playing& p : s_playing)
        if (p.clip == NULL || p.owner == &s_sayOwner)
        {
            slot = &p;
            break;
        }
    if (slot == NULL)
        slot = &s_playing[0];
    slot->clip = pick[next_rand() % pick.size()];
    slot->pos = 0;
    slot->owner = &s_sayOwner;
    slot->gainL = slot->gainR = gain * 0.7071f;
    fprintf(stderr, "[guest] %s says \"%s\"\n", PortGuestRosterPack(guest), slot->clip->label.c_str());
    fflush(stderr);
}

void PortGuestVoiceStopAll()
{
    for (Playing& p : s_playing)
        p.clip = NULL;
}

// r99 (Lance: "a victorious ass song that plays while they evolve (search for actual songs do notttt create
// one)"): the franchise evolution's music, from his own Melee disc (tools/outside/hps2evm.py: Pokemon Stadium's
// opening while it evolves, the Pokemon victory fanfare as it comes out), STRIKERS_HOME/guests/evo/music_build.evm
// and music_fanfare.evm ('EVM1', u32 rate 32000, u32 frames, s16 stereo). Played in this mixer at the game's
// music volume; everything else the game plays dips under it meanwhile.
namespace
{
struct EvoMusic
{
    bool tried = false;
    std::vector<short> pcm;  // stereo
};
EvoMusic s_evoMusic[2];
int s_evoPlaying = -1;
size_t s_evoPos = 0;
float s_evoDuck = 1.0f;

const EvoMusic& evo_music(int which)
{
    EvoMusic& m = s_evoMusic[which];
    if (m.tried)
        return m;
    m.tried = true;
    const std::string path = voice_dir() + "../evo/" + (which == 0 ? "music_build.evm" : "music_fanfare.evm");
    if (FILE* f = fopen(path.c_str(), "rb"))
    {
        char magic[4];
        unsigned rate = 0, frames = 0;
        if (fread(magic, 1, 4, f) == 4 && memcmp(magic, "EVM1", 4) == 0 && fread(&rate, 4, 1, f) == 1 &&
            fread(&frames, 4, 1, f) == 1 && rate == (unsigned)kRate && frames < 32000u * 60u)
        {
            m.pcm.resize((size_t)frames * 2);
            if (fread(m.pcm.data(), 2, m.pcm.size(), f) != m.pcm.size())
                m.pcm.clear();
        }
        fclose(f);
    }
    fprintf(stderr, "[guest] evolution music %s: %s\n", which == 0 ? "build" : "fanfare",
            m.pcm.empty() ? "not there" : "ready");
    fflush(stderr);
    return m;
}
}  // namespace

void PortEvoMusic(int which)
{
    if (which < 0 || which > 1)
    {
        s_evoPlaying = -1;  // (the duck comes back up in the mixer)
        return;
    }
    if (evo_music(which).pcm.empty())
        return;
    s_evoPlaying = which;
    s_evoPos = 0;
}

extern "C" void PortGuestVoiceMix(int* left, int* right, int frames)
{
    // r99: the evolution's music, over everything else the game plays (dipped under it)
    if (s_evoPlaying >= 0 || s_evoDuck < 1.0f)
    {
        const float want = s_evoPlaying >= 0 ? 0.25f : 1.0f;
        const float step = 1.0f / (0.35f * kRate);
        const float gain = Audio::MasterVolume::GetVolume(Audio::MasterVolume::VG_Music) * 0.9f;
        const std::vector<short>* pcm = s_evoPlaying >= 0 ? &s_evoMusic[s_evoPlaying].pcm : NULL;
        for (int f = 0; f < frames; f++)
        {
            s_evoDuck += s_evoDuck < want ? std::min(step, want - s_evoDuck) : -std::min(step, s_evoDuck - want);
            left[f] = (int)(left[f] * s_evoDuck);
            right[f] = (int)(right[f] * s_evoDuck);
            if (pcm != NULL && s_evoPos + 1 < pcm->size())
            {
                left[f] += (int)((*pcm)[s_evoPos] * gain);
                right[f] += (int)((*pcm)[s_evoPos + 1] * gain);
                s_evoPos += 2;
            }
        }
        if (pcm != NULL && s_evoPos + 1 >= pcm->size())
            s_evoPlaying = -1;
    }
    if (Audio::gbGameIsPaused)
        return;  // held where they are, as the game's paused sounds are
    s_msNow += (unsigned)(frames * 1000 / kRate);
    for (Playing& p : s_playing)
    {
        if (p.clip == NULL)
            continue;
        const std::vector<short>& pcm = p.clip->pcm;
        int f = 0;
        for (; f < frames && p.pos < pcm.size(); f++, p.pos++)
        {
            const float s = (float)pcm[p.pos];
            left[f] += (int)(s * p.gainL);
            right[f] += (int)(s * p.gainR);
        }
        if (p.pos >= pcm.size())
            p.clip = NULL;
    }
}
