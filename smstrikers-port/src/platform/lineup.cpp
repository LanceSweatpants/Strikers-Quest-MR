// strikers-quest r78: a team's co-captain (include/port/lineup.h).
#include "port/lineup.h"
#include "Game/FE/feHelpFuncs.h"
#include "Game/GameInfo.h"
#include "port/xr_guest.h"
#include "port/xr_net.h"

namespace
{
int s_secondCaptain[2] = {-1, -1};
}

extern "C" void PortSetSecondCaptain(int side, int characterClass)
{
    if (side < 0 || side > 1)
        return;
    // a captain class only (eCharacterClass DAISY 1, DONKEYKONG 2, LUIGI 5, MARIO 6, PEACH 7, WALUIGI 9,
    // WARIO 10, YOSHI 11; never the Super Team's MYSTERY or a sidekick class)
    switch (characterClass)
    {
    case 1: case 2: case 5: case 6: case 7: case 9: case 10: case 11:
        s_secondCaptain[side] = characterClass;
        break;
    default:
        s_secondCaptain[side] = -1;
        break;
    }
}

extern "C" int PortSecondCaptain(int side)
{
    return side >= 0 && side <= 1 ? s_secondCaptain[side] : -1;
}

extern "C" int PortIsCoCaptainSlot(int charIndex)
{
    return (charIndex == 1 && s_secondCaptain[0] >= 0) || (charIndex == 5 && s_secondCaptain[1] >= 0) ? 1 : 0;
}

extern "C" int PortSidekickSlot(int side, int k)
{
    if (side < 0 || side > 1 || k < 0)
        return 10;
    const int base = side == 0 ? 1 : 5;
    if (s_secondCaptain[side] < 0)
        return base + k;  // the game's own (index + 1 / + 5), unchanged
    return k < 2 ? base + 1 + k : 10;
}

namespace
{
int s_picked[2] = {-1, -1}, s_pickedGuest[2] = {-1, -1};

bool captain_class(int cls)
{
    switch (cls)
    {
    case 1: case 2: case 5: case 6: case 7: case 9: case 10: case 11:
        return true;
    default:
        return false;
    }
}
}  // namespace

extern "C" void PortPickCoCaptain(int side, int characterClass, int guest)
{
    if (side < 0 || side > 1)
        return;
    s_picked[side] = captain_class(characterClass) ? characterClass : -1;
    s_pickedGuest[side] = s_picked[side] >= 0 ? guest : -1;
}

extern "C" int PortPickedCoCaptain(int side, int* guest)
{
    if (side < 0 || side > 1)
        return -1;
    if (guest != nullptr)
        *guest = s_pickedGuest[side];
    return s_picked[side];
}

extern "C" void PortSetCoCaptainsForMatch(int class0, int guest0, int class1, int guest1)
{
    GameInfoManager* gi = nlSingleton<GameInfoManager>::Instance();
    int want[2] = {class0, class1};
    const int guest[2] = {guest0, guest1};
    for (int side = 0; side < 2; side++)
    {
        if (!captain_class(want[side]))
        {
            want[side] = -1;
            continue;
        }
        const eTeamID own = gi != nullptr ? gi->GetTeam((short)side) : TEAM_MYSTERY;
        if (own == TEAM_MYSTERY || own < 0 || own > TEAM_MYSTERY)
        {
            want[side] = -1;  // the Super Team fields its own (CharacterTemplate.cpp)
            continue;
        }
        for (int t = 0; t < 2; t++)
        {
            const eTeamID team = gi->GetTeam((short)t);
            if (team >= 0 && team < TEAM_MYSTERY && (int)ConvertToCharacterClass(team) == want[side])
                want[side] = -1;  // a captain already on the field
        }
    }
    if (want[0] >= 0 && want[0] == want[1])
        want[1] = -1;
    for (int side = 0; side < 2; side++)
    {
        PortSetSecondCaptain(side, want[side]);
        PortGuestSetCoCaptain(side, want[side] >= 0 ? guest[side] : -1, false);
    }
}

extern "C" void PortApplyPickedCoCaptains(void)
{
    if (PortNetOnline())
        return;  // the online start set this match's (Game.cpp PortNetGameFrameBegin)
    GameInfoManager* gi = nlSingleton<GameInfoManager>::Instance();
    const bool grudge = gi != nullptr && gi->IsInFriendlyMode();
    PortSetCoCaptainsForMatch(grudge ? s_picked[0] : -1, s_pickedGuest[0], grudge ? s_picked[1] : -1,
                              s_pickedGuest[1]);
}

extern "C" int PortStsTeam(int side, int characterClass, int teamId)
{
    if (side < 0 || side > 1 || s_secondCaptain[side] < 0 || characterClass != s_secondCaptain[side])
        return teamId;
    for (int t = 0; t < 8; t++)  // the eight captains' teams (not the Super Team)
    {
        if ((int)ConvertToCharacterClass((eTeamID)t) == characterClass)
            return t;
    }
    return teamId;
}
