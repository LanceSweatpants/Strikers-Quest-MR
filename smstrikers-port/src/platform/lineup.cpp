// strikers-quest r78: a team's co-captain (include/port/lineup.h). r130: every sidekick slot's own class.
#include "port/lineup.h"
#include "Game/FE/feHelpFuncs.h"
#include "Game/GameInfo.h"
#include "port/xr_guest.h"
#include "port/xr_net.h"

#include <stdio.h>
#include <string.h>

namespace
{
int s_secondCaptain[2] = {-1, -1};
// r130: each sidekick slot's class from its own pick (-1 the side's sidekick class); slot 0 under a co-captain unused
int s_slotClass[2][3] = {{-1, -1, -1}, {-1, -1, -1}};

bool captain_class(int cls)
{
    // a captain class only (eCharacterClass DAISY 1, DONKEYKONG 2, LUIGI 5, MARIO 6, PEACH 7, WALUIGI 9,
    // WARIO 10, YOSHI 11; never the Super Team's MYSTERY or a sidekick class)
    switch (cls)
    {
    case 1: case 2: case 5: case 6: case 7: case 9: case 10: case 11:
        return true;
    default:
        return false;
    }
}

// The eTeamID whose captain plays class `cls`, -1 none.
int team_of_class(int cls)
{
    if (!captain_class(cls))
        return -1;
    for (int t = 0; t < 8; t++)  // the eight captains' teams (not the Super Team)
        if ((int)ConvertToCharacterClass((eTeamID)t) == cls)
            return t;
    return -1;
}

// r130: GameInfo's two teams (eTeamID), when it has a mode: none yet at boot (GM_INVALID), where GetTeam would read
// before its table (asked every menu frame now, PortApplyPickedCoCaptains). -1 each when not.
bool match_teams(int teams[2])
{
    teams[0] = teams[1] = -1;
    GameInfoManager* gi = nlSingleton<GameInfoManager>::Instance();
    const int mode = gi != nullptr ? (int)gi->GetCurrentMode() : -1;
    if (mode < 0 || mode >= (int)GameInfoManager::GM_NUM_MODES)
        return false;
    teams[0] = (int)gi->GetTeam(0);
    teams[1] = (int)gi->GetTeam(1);
    return true;
}

const char* class_name(int cls)
{
    static const char* const kNames[13] = {"birdo", "daisy", "donkeykong", "hammerbros", "koopa", "luigi", "mario",
                                           "peach", "toad", "waluigi", "wario", "yoshi", "superteam"};
    return cls >= 0 && cls < 13 ? kNames[cls] : "(the sidekick class)";
}
}  // namespace

extern "C" void PortSetSecondCaptain(int side, int characterClass)
{
    if (side < 0 || side > 1)
        return;
    s_secondCaptain[side] = captain_class(characterClass) ? characterClass : -1;
}

extern "C" int PortSecondCaptain(int side)
{
    return side >= 0 && side <= 1 ? s_secondCaptain[side] : -1;
}

extern "C" int PortSlotCaptain(int side, int slot)
{
    if (side < 0 || side > 1 || slot < 0 || slot > 2)
        return -1;
    if (slot == 0 && s_secondCaptain[side] >= 0)
        return s_secondCaptain[side];
    return s_slotClass[side][slot];
}

extern "C" void PortSetSlotCaptain(int side, int slot, int characterClass)
{
    if (side < 0 || side > 1 || slot < 0 || slot > 2)
        return;
    s_slotClass[side][slot] = captain_class(characterClass) ? characterClass : -1;
}

extern "C" int PortIsCaptainSlot(int charIndex)
{
    if (charIndex >= 1 && charIndex <= 3)
        return PortSlotCaptain(0, charIndex - 1) >= 0 ? 1 : 0;
    if (charIndex >= 5 && charIndex <= 7)
        return PortSlotCaptain(1, charIndex - 5) >= 0 ? 1 : 0;
    return 0;
}

extern "C" int PortSlotOfClass(int side, int characterClass)
{
    if (side < 0 || side > 1 || !captain_class(characterClass))
        return -1;
    for (int slot = 0; slot < 3; slot++)
        if (PortSlotCaptain(side, slot) == characterClass)
            return slot;
    return -1;
}

extern "C" int PortFirstSidekickPlayer(int side)
{
    if (side < 0 || side > 1)
        return -1;
    for (int slot = 0; slot < 3; slot++)
        if (PortSlotCaptain(side, slot) < 0)
            return slot + 1;
    return -1;
}

extern "C" int PortSidekickSlot(int side, int k)
{
    if (side < 0 || side > 1 || k < 0)
        return 10;
    const int base = side == 0 ? 1 : 5;
    if (PortSlotCaptain(side, 0) < 0 && PortSlotCaptain(side, 1) < 0 && PortSlotCaptain(side, 2) < 0)
        return base + k;  // the game's own (index + 1 / + 5), unchanged
    // r130: the k-th slot that plays the sidekick class (r78's co-captain: slots 2-3, then none)
    for (int slot = 0; slot < 3; slot++)
        if (PortSlotCaptain(side, slot) < 0 && k-- == 0)
            return base + slot;
    return 10;
}

namespace
{
int s_picked[2] = {-1, -1}, s_pickedGuest[2] = {-1, -1};
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

extern "C" void PortCoCaptainsAsPlayed(const int teams[2], const int want[2], int out[2])
{
    for (int side = 0; side < 2; side++)
    {
        out[side] = captain_class(want[side]) ? want[side] : -1;
        if (out[side] < 0)
            continue;
        if (teams[side] < 0 || teams[side] >= 8)
        {
            out[side] = -1;  // the Super Team fields its own (CharacterTemplate.cpp)
            continue;
        }
        for (int t = 0; t < 2; t++)
            if (teams[t] >= 0 && teams[t] < 8 && (int)ConvertToCharacterClass((eTeamID)teams[t]) == out[side])
                out[side] = -1;  // a captain already on the field
    }
    if (out[0] >= 0 && out[0] == out[1])
        out[1] = -1;
}

extern "C" void PortSetCoCaptainsForMatch(int class0, int guest0, int class1, int guest1)
{
    int teams[2];
    match_teams(teams);  // (no mode yet: no teams, no co-captains)
    const int want[2] = {class0, class1};
    const int guest[2] = {guest0, guest1};
    int out[2];
    PortCoCaptainsAsPlayed(teams, want, out);
    for (int side = 0; side < 2; side++)
    {
        PortSetSecondCaptain(side, out[side]);
        PortGuestSetCoCaptain(side, out[side] >= 0 ? guest[side] : -1, false);
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
    PortApplySlotCaptains();  // r130: and every mode's sidekick slots
}

extern "C" int PortStsTeam(int side, int characterClass, int teamId)
{
    if (PortSlotOfClass(side, characterClass) < 0)
        return teamId;  // the side's own captain, a sidekick class, the Super Team: the side's team, as the game did
    const int t = team_of_class(characterClass);
    return t >= 0 ? t : teamId;
}

// ------------------------------------------------------------------------------------------- r130: the slots

extern "C" void PortResolveSlotClasses(const int teams[2], const int coClasses[2], const int wantTeam[6],
                                       const int mayMove[6], int outClass[6])
{
    bool free[8];
    for (int t = 0; t < 8; t++)
        free[t] = true;
    for (int side = 0; side < 2; side++)
    {
        if (teams[side] >= 0 && teams[side] < 8)
            free[teams[side]] = false;
        const int ct = team_of_class(coClasses[side]);
        if (ct >= 0)
            free[ct] = false;
    }
    for (int i = 0; i < 6; i++)
        outClass[i] = -1;
    // the Strikers captains first (each only its own class), then the guests (theirs, else the nearest free one)
    for (int pass = 0; pass < 2; pass++)
    {
        for (int i = 0; i < 6; i++)
        {
            const int side = i / 3, slot = i % 3;
            if (teams[side] < 0 || teams[side] >= 8)
                continue;  // the Super Team: its own class everywhere (or no team)
            if (slot == 0 && captain_class(coClasses[side]))
                continue;  // the co-captain's slot
            const int t = wantTeam[i];
            const bool move = mayMove[i] != 0;
            if (t < 0 || t >= 8 || move != (pass == 1))
                continue;
            const int got = free[t] ? t : (move ? PortGuestSlotForBase(t, free) : -1);
            if (got < 0 || got >= 8)
                continue;  // none free: the side's sidekick class
            free[got] = false;
            outClass[i] = (int)ConvertToCharacterClass((eTeamID)got);
        }
    }
}

namespace
{
// Sets the slot-pick classes, each once on the field (GameInfo's two captains, the co-captains, the earlier slots in
// order: home 1-3, then away), and logs what changed (`why`: where they came from).
void set_slots(const int classes[6], const int want[6], const char* why)
{
    int teams[2];
    match_teams(teams);
    bool used[13] = {};
    for (int side = 0; side < 2; side++)
    {
        const int team = teams[side];
        if (team >= 0 && team < 8)
            used[(int)ConvertToCharacterClass((eTeamID)team)] = true;
        if (captain_class(s_secondCaptain[side]))
            used[s_secondCaptain[side]] = true;
    }
    int out[6];
    for (int i = 0; i < 6; i++)
    {
        const int side = i / 3, slot = i % 3;
        const int team = teams[side];
        int cls = captain_class(classes[i]) ? classes[i] : -1;
        if (team < 0 || team >= 8 || (slot == 0 && s_secondCaptain[side] >= 0))
            cls = -1;  // the Super Team's side; the co-captain's slot
        if (cls >= 0 && used[cls])
        {
            fprintf(stderr, "[lineup] side %d slot %d: %s is on the field already: it plays the sidekick class\n", side,
                    slot + 1, class_name(cls));
            cls = -1;
        }
        if (cls >= 0)
            used[cls] = true;
        out[i] = cls;
    }
    // logged once per change (asked every menu frame)
    static int s_lastOut[6] = {-2, -2, -2, -2, -2, -2}, s_lastWant[6] = {-2, -2, -2, -2, -2, -2};
    bool changed = false;
    for (int i = 0; i < 6; i++)
    {
        changed = changed || s_lastOut[i] != out[i] || s_lastWant[i] != (want != nullptr ? want[i] : -1);
        s_lastOut[i] = out[i];
        s_lastWant[i] = want != nullptr ? want[i] : -1;
        s_slotClass[i / 3][i % 3] = out[i];
    }
    if (!changed)
        return;
    for (int side = 0; side < 2; side++)
    {
        fprintf(stderr, "[lineup] %s: side %d's sidekick slots play %s / %s / %s\n", why, side,
                class_name(PortSlotCaptain(side, 0)), class_name(PortSlotCaptain(side, 1)),
                class_name(PortSlotCaptain(side, 2)));
        for (int slot = 0; slot < 3 && want != nullptr; slot++)
        {
            const int w = want[side * 3 + slot];
            if (w >= 0 && w < 8 && out[side * 3 + slot] < 0 && !(slot == 0 && s_secondCaptain[side] >= 0))
                fprintf(stderr, "[lineup] side %d slot %d wanted %s: no free captain class, it plays the sidekick class\n",
                        side, slot + 1, class_name((int)ConvertToCharacterClass((eTeamID)w)));
        }
    }
    fflush(stderr);
}
}  // namespace

extern "C" void PortApplySlotCaptains(void)
{
    int teams[2];
    const bool known = match_teams(teams);  // (no mode yet: every slot its sidekick class)
    const int co[2] = {s_secondCaptain[0], s_secondCaptain[1]};
    int want[6], move[6], out[6];
    for (int i = 0; i < 6; i++)
    {
        move[i] = 0;
        want[i] = known ? PortGuestSlotWant(i / 3, i % 3, &move[i]) : -1;
    }
    PortResolveSlotClasses(teams, co, want, move, out);
    set_slots(out, want, "this match");
}

extern "C" void PortSetSlotCaptains(const int classes[6])
{
    int c[6];
    for (int i = 0; i < 6; i++)
        c[i] = classes != nullptr ? classes[i] : -1;
    set_slots(c, nullptr, "online");
}

extern "C" void PortClearSlotCaptains(void)
{
    const int none[6] = {-1, -1, -1, -1, -1, -1};
    set_slots(none, nullptr, "every slot its sidekick class");
}

extern "C" void PortDropSlotCaptain(int side, int slot)
{
    if (side < 0 || side > 1 || slot < 0 || slot > 2)
        return;
    fprintf(stderr, "[lineup] side %d slot %d: %s is on the field already: it plays the sidekick class\n", side, slot + 1,
            class_name(PortSlotCaptain(side, slot)));
    fflush(stderr);
    if (slot == 0 && s_secondCaptain[side] >= 0)
        s_secondCaptain[side] = -1;
    else
        s_slotClass[side][slot] = -1;
}
