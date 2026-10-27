#include "NL/nlBind.h"
#include "NL/nlString.h"
#include "Game/Sys/GCStream.h"
#include "Game/Audio/AudioStream.h"
#include "Game/Sys/PlatStream.h"
#include "Game/Audio/AudioLoader.h"
#include "Game/Audio/PriorityStream.h"
#include "NL/nlMemory.h"
#include "NL/nlConfig.h"
#include "NL/nlPrint.h"
#include "NL/nlMath.h"
#include "Game/GameInfo.h"
#include "Game/Team.h"
#include "Game/Player.h"
#include <stdlib.h>

#include "Game/FE/feHelpFuncs.h"
#include "port/xr_guest.h"  // strikers-quest r68

extern cTeam* g_pTeams[2];
extern unsigned int nlAudioSeed;  // strikers-quest r50: the sounds' own (NL/nlMath.cpp)

AudioStreamTrack::TrackManagerBase* g_pTrackManager;
PriorityStream* g_pPriorityStream;

namespace Audio
{
extern cPlayer* g_pLastScorer;
} // namespace Audio

/**
 * Offset/Address/Size: 0x69C | 0x8014BEB4 | size: 0x20
 */
void Audio::InitStreaming()
{
    PlatAudio::InitStreaming();
}

/**
 * Offset/Address/Size: 0x65C | 0x8014BE74 | size: 0x40
 */
void Audio::StopStreaming()
{
    if (AudioLoader::gbDisableAudio == false)
    {
        // PORT: null when audio is disabled, nothing to command.
        if (g_pTrackManager != NULL) g_pTrackManager->StopAllTracks(0);
    }
}

// strikers-quest r68: the announcer line just looked up names a guest captain by the Strikers
// captain's name ("HomeCaptain" etc.): StreamTrack::QueueStream plays it silent (it still runs, so
// whatever waits on it goes on as before). A guest scoring gets the line the game has for a
// sidekick's goal, which names nobody.
static bool s_guestNameCall = false;

bool Audio::TrackMgrTakeGuestNameCall()
{
    const bool was = s_guestNameCall;
    s_guestNameCall = false;
    return was;
}

/**
 * Offset/Address/Size: 0x2EC | 0x8014BB04 | size: 0x370
 */
bool Audio::TrackMgrFileNameParamLookup(const char* param, char* out, unsigned long size)
{
    s32 hash = nlStringLowerHash(param);
    const char* value;
    s_guestNameCall = false;

    switch (hash)
    {
    case 0x3B762F28:
        s_guestNameCall = PortGuestCaptainFor(0) >= 0;
        value = GetTeamName(nlSingleton<GameInfoManager>::Instance()->GetTeam(0));
        nlStrNCpy(out, value, size);
        break;

    case (s32)0xB59118F1:
    {
        s_guestNameCall = PortGuestCaptainFor(1) >= 0;
        uintptr_t outValue = (uintptr_t)GetTeamName(nlSingleton<GameInfoManager>::Instance()->GetTeam(1));
        nlStrNCpy(out, (const char*)outValue, size);
        break;
    }

    case (s32)0xBDD1CBB6:
        switch (nlSingleton<GameInfoManager>::Instance()->GetStadium())
        {
        case 0:
            nlStrNCpy(out, "pipeline", size);
            break;
        case 1:
            nlStrNCpy(out, "palace", size);
            break;
        case 2:
            nlStrNCpy(out, "konga", size);
            break;
        case 3:
            nlStrNCpy(out, "underground", size);
            break;
        case 4:
            nlStrNCpy(out, "yoshi", size);
            break;
        case 5:
            nlStrNCpy(out, "super", size);
            break;
        case 6:
            nlStrNCpy(out, "forbidden", size);
            break;
        default:
            return false;
        }
        break;

    case (s32)0xAEDB83D0:
        if (Audio::g_pLastScorer->GetTeam() != NULL
            && PortGuestIndexFor(Audio::g_pLastScorer->GetTeam()->m_nSide, (int)Audio::g_pLastScorer->m_eCharacterClass) >= 0)
        {
            nlStrNCpy(out, "Side", size);
            break;
        }
        switch (Audio::g_pLastScorer->m_eCharacterClass)
        {
        case 0:
        case 3:
        case 4:
        case 8:
        case 12:
            nlStrNCpy(out, "Side", size);
            break;

        case 1:
            nlStrNCpy(out, "Daisy", size);
            break;

        case 2:
            nlStrNCpy(out, "DonkeyKong", size);
            break;

        case 5:
            nlStrNCpy(out, "Luigi", size);
            break;

        case 6:
            nlStrNCpy(out, "Mario", size);
            break;

        case 7:
            nlStrNCpy(out, "Peach", size);
            break;

        case 9:
            nlStrNCpy(out, "Waluigi", size);
            break;

        case 10:
            nlStrNCpy(out, "Wario", size);
            break;

        case 11:
            nlStrNCpy(out, "Yoshi", size);
            break;

        default:
            return false;
        }
        break;

    case 0x3F76E1B9:
    {
        int sideInt = g_pTeams[0]->m_nScore < g_pTeams[1]->m_nScore;
        s16 side = (s16)sideInt;
        s_guestNameCall = PortGuestCaptainFor(side) >= 0;
        uintptr_t outValue = (uintptr_t)GetTeamName(nlSingleton<GameInfoManager>::Instance()->GetTeam(side));
        nlStrNCpy(out, (const char*)outValue, size);
        break;
    }

    default:
        if (nlStrNICmp<char>(param, "RAND", 4) == 0)
        {
            u32 num = atoi(param + 4);
            const char* format = num < 10 ? "%d" : "%02d";
            nlSNPrintf(out, size, format, nlRandom(num, &nlAudioSeed) + 1);
            break;
        }

        nlPrintf("Unknown stream param \"%s\"\n", param);
        return false;
    }

    return true;
}

void Audio::DestroyTrackMgr()
{
    delete g_pTrackManager;
    g_pTrackManager = NULL;
}

/**
 * Offset/Address/Size: 0x27C | 0x8014BA94 | size: 0x70
 */
void Audio::CreatePriorityStreams()
{
    PriorityStream* stream = new (nlMalloc(sizeof(PriorityStream), 8, false)) PriorityStream(*g_pTrackManager->CreateTrack("Priority", MasterVolume::VG_Special));
    g_pPriorityStream = stream;
}

/**
 * Offset/Address/Size: 0xDC | 0x8014B8F4 | size: 0x1A0
 */
void Audio::DestroyPriorityStreams()
{
    PriorityStream* ps;
    if ((ps = g_pPriorityStream) != NULL)
    {
        AudioStreamTrack::StreamTrack& track = ps->m_Track;
        {
            Function<FnVoidVoid> callback;
            PriorityStreamSetIdleCallback(&track, callback);
        }
        delete ps;
    }
    g_pPriorityStream = NULL;
}

/**
 * Offset/Address/Size: 0xD4 | 0x8014B8EC | size: 0x8
 */
PriorityStream* Audio::GetPriorityStream()
{
    return g_pPriorityStream;
}

/**
 * Offset/Address/Size: 0x0 | 0x8014B818 | size: 0xD4
 */
void Audio::ConfigureStreamBuffers(unsigned long count)
{
    if (!GetConfigBool(Config::Global(), "no_stream", false))
    {
        PlatAudio::ConfigureStreamBuffers(count);
    }
}
