#ifndef PORT_LINEUP_H
#define PORT_LINEUP_H

// strikers-quest r78 (Lance: "2 captains and 2 sidekicks share the field at the same time", "a
// CO-Captain"): a team's co-captain. A second captain character class plays in the team's slot 1
// (cTeam::m_pPlayers[1], g_pCharacters[1] / [5]) in place of the first sidekick: a real captain (the
// game's captain-ness is the class: cPlayer::IsCaptain, CharacterTemplate.cpp ::IsCaptain), with its own
// Super Strike, meter, moves, stats and voice. Slots 2-3 keep the team's sidekicks; slot 0 stays the
// team's captain (the HUD, the team's name, its celebrations). Set by the franchise at its match start
// (src/platform/franchise.cpp), cleared for every other match.
//
// r130 (Lance: "you coulda just let ANY spot be ANY character"): every sidekick slot plays a class of its
// own. Slot k (0-2) of a side is its player m_ID k + 1 (g_pCharacters 1-3 / 5-7): a captain class picked
// for it (a Strikers captain itself, or a guest on its base / the nearest free captain class: its own
// model, moves, Super Strike, voice, AI), else the team's sidekick class as the game does. The co-captain
// above is slot 0's when the side has one. Each Strikers captain class plays at most once on the field
// (both teams' captains, the co-captains, every slot); a slot that can't have its own plays the side's
// sidekick class (a guest then drawn on it, as r130's first take did).

#ifdef __cplusplus
extern "C" {
#endif

// The co-captain's character class (eCharacterClass, a captain class), or -1 for none, for `side`
// (slot 0; only the co-captain: a slot pick's class is PortSlotCaptain's).
void PortSetSecondCaptain(int side, int characterClass);
int PortSecondCaptain(int side);
// r130: the captain class sidekick slot `slot` (0-2) of `side` plays (the co-captain's for slot 0 when the side has
// one), or -1: the side's sidekick class.
int PortSlotCaptain(int side, int slot);
// r130: a slot pick's class (slot 0 under a co-captain: kept, unused while the co-captain is there); -1 the sidekick class.
void PortSetSlotCaptain(int side, int slot, int characterClass);
// r130: g_pCharacters index `charIndex` (0-9) is a sidekick slot playing a captain class (1-3 / 5-7).
int PortIsCaptainSlot(int charIndex);
// r130: the slot (0-2) of `side` playing captain class `characterClass`, -1 none (classes are once on the field).
int PortSlotOfClass(int side, int characterClass);
// r130: the m_ID (1-3) of `side`'s first player of its sidekick class, -1 when every slot plays a captain class.
int PortFirstSidekickPlayer(int side);
// The g_pCharacters index of the k-th player (k 0-2) of `side` playing its sidekick class: the game's own base + k
// when every slot does; with captain classes in some, the ones that don't, and 10 (none) past them.
int PortSidekickSlot(int side, int k);
// r79: whose Super Strike look (the "<team>_ShootToScoreCamera" camera, "<team>_captain_sts_effect",
// "<team>_shoot_to_score_shot / _catch" effects) a player of `side` and class `characterClass` gets: a
// captain class in a sidekick slot (r130: any of them) its own captain's team (eTeamID), everyone else
// `teamId` (the side's team), as the game did.
int PortStsTeam(int side, int characterClass, int teamId);

// r79: Grudge Match co-captains, picked on the character select menu before a side's sidekicks
// (xr_quest.cpp, through xr_guest.cpp PortGuestCoCaptainPick): a captain class (-1 none) and the guest
// drawn as it (roster index, -1: the Strikers captain itself). Kept until picked again.
void PortPickCoCaptain(int side, int characterClass, int guest);
int PortPickedCoCaptain(int side, int* guest);
// The co-captains as a match plays them (pure): each only if it can play: a captain class that neither team's
// captain is (teams: eTeamID; the Super Team's side none), and not the home side's co-captain too (the away
// one drops).
void PortCoCaptainsAsPlayed(const int teams[2], const int want[2], int out[2]);
// Sets both sides' co-captains for a match by that rule (GameInfo's teams).
void PortSetCoCaptainsForMatch(int class0, int guest0, int class1, int guest1);
// Every frame outside the franchise's own matches (franchise.cpp PortFranchiseFrame): in a Grudge Match
// (Friendly) the picked ones, in every other mode none; an online match keeps what its start set (Game.cpp).
// r130: then every offline mode's sidekick slots' classes (PortApplySlotCaptains).
void PortApplyPickedCoCaptains(void);

// r130: the once-per-field rule for a match's sidekick slots (pure; arrays by side * 3 + slot, home first).
// teams: eTeamID (not 0-7: the Super Team or none, its slots its own class); coClasses: the co-captains as played
// (PortCoCaptainsAsPlayed; a side's slot 0 is its co-captain's then); wantTeam: the captain each slot wants
// (eTeamID, -1 the sidekick class); mayMove: 1 a guest (its base, else the nearest free captain class), 0 a
// Strikers captain (its own or none). Both teams' captains and the co-captains first, then the Strikers captains
// (home slots 1-3, then away), then the guests (the same order). outClass: each slot's class, -1 the sidekick class.
void PortResolveSlotClasses(const int teams[2], const int coClasses[2], const int wantTeam[6], const int mayMove[6],
                            int outClass[6]);
// r130: every offline match (PortApplyPickedCoCaptains every menu frame; the franchise's match start): each side's slot
// classes from its team's own slot picks in the current mode (xr_guest.cpp PortGuestSlotWant), by the rule above. A
// change is logged once (a slot whose captain class is taken: logged, it plays the sidekick class).
void PortApplySlotCaptains(void);
// r130, online: the slot classes the match start carried (both headsets the same; -1 the sidekick class), checked
// once more against GameInfo's captains and the co-captains (a clash plays the sidekick class, the same on both).
void PortSetSlotCaptains(const int classes[6]);
void PortClearSlotCaptains(void);  // every slot its side's sidekick class (an online franchise match)
// r130: CreateCharacters' last check found that slot's captain class already on the field: it plays the sidekick class.
void PortDropSlotCaptain(int side, int slot);

#ifdef __cplusplus
}
#endif

#endif
