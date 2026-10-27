#ifndef PORT_LINEUP_H
#define PORT_LINEUP_H

// strikers-quest r78 (Lance: "2 captains and 2 sidekicks share the field at the same time", "a
// CO-Captain"): a team's co-captain. A second captain character class plays in the team's slot 1
// (cTeam::m_pPlayers[1], g_pCharacters[1] / [5]) in place of the first sidekick: a real captain (the
// game's captain-ness is the class: cPlayer::IsCaptain, CharacterTemplate.cpp ::IsCaptain), with its own
// Super Strike, meter, moves, stats and voice. Slots 2-3 keep the team's sidekicks; slot 0 stays the
// team's captain (the HUD, the team's name, its celebrations). Set by the franchise at its match start
// (src/platform/franchise.cpp), cleared for every other match.

#ifdef __cplusplus
extern "C" {
#endif

// The co-captain's character class (eCharacterClass, a captain class), or -1 for none, for `side`.
void PortSetSecondCaptain(int side, int characterClass);
int PortSecondCaptain(int side);
// g_pCharacters index `charIndex` (0-9) is a co-captain's (slot 1 of a side that has one).
int PortIsCoCaptainSlot(int charIndex);
// The g_pCharacters index of the k-th sidekick (k 0-2) of `side`: the game's own base + k without a
// co-captain; with one, slots 2-3 and 10 (none) past them.
int PortSidekickSlot(int side, int k);
// r79: whose Super Strike look (the "<team>_ShootToScoreCamera" camera, "<team>_captain_sts_effect",
// "<team>_shoot_to_score_shot / _catch" effects) a player of `side` and class `characterClass` gets: the
// co-captain his own captain's team (eTeamID), everyone else `teamId` (the side's team), as the game did.
int PortStsTeam(int side, int characterClass, int teamId);

// r79: Grudge Match co-captains, picked on the character select menu before a side's sidekicks
// (xr_quest.cpp, through xr_guest.cpp PortGuestCoCaptainPick): a captain class (-1 none) and the guest
// drawn as it (roster index, -1: the Strikers captain itself). Kept until picked again.
void PortPickCoCaptain(int side, int characterClass, int guest);
int PortPickedCoCaptain(int side, int* guest);
// Sets both sides' co-captains for a match, each only if it can play: a captain class that neither team's
// captain is (nor the Super Team's side), and not the home side's co-captain too (the away one drops).
void PortSetCoCaptainsForMatch(int class0, int guest0, int class1, int guest1);
// Every frame outside the franchise's own matches (franchise.cpp PortFranchiseFrame): in a Grudge Match
// (Friendly) the picked ones, in every other mode none; an online match keeps what its start set (Game.cpp).
void PortApplyPickedCoCaptains(void);

#ifdef __cplusplus
}
#endif

#endif
