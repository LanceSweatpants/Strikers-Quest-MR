#ifndef PORT_XR_GUEST_H
#define PORT_XR_GUEST_H

// strikers-quest r65: guest characters (Lance: characters from his other games playable in
// Strikers; "boo should be moving like boo"). A guest stands in for one Strikers character class
// (guests.txt in STRIKERS_HOME, e.g. "toad boo": every Toad is drawn as Boo): the Strikers
// character keeps its own logic, moves, timing, goalie and Super Strikes; only what is drawn is the
// guest, posed from the guest's own animation of the same name (its game's own moves), skinned on
// the CPU and drawn with the Strikers character's material and lights. Guest packs (.gpk) are made
// from the player's own game files (tools/outside/guestpack.py); nothing of them ships.

class cCharacter;
class cPoseAccumulator;
class cSAnim;
struct glModel;

// The models to draw for `character` this frame instead of its own (`hostModel`, already set up by
// PrepareToRender, gives the material), *numModels of them; nullptr when it has no guest.
glModel** PortGuestFrameModels(const cCharacter& character, cPoseAccumulator* acc, const glModel* hostModel,
                               int* numModels);
// r94: one of those models is a shiny guest's sparkles (never the first): left out while the game skins
// the character with an effect (shocked, frozen), which would paint its texture on them.
bool PortGuestIsSparkleModel(const glModel* model);
// r105 (Lance: "grovyle was WAY too small"): a guest's own size over its pack's (guests/limbs.txt
// "<pack> size <scale>", a shiny pack takes its normal's), 1 = as built. The 3D evolution uses it too.
float PortGuestSizeScale(const char* pack);

// r77 (Lance: Diddy "just gliding" when he scored): the life-size scorer's copy is posed by hand
// (xr_scorer.cpp), not by the character's own animation controller, so a guest posed from its own
// clips (Charged) kept the field character's frozen frame while the show moved it. Around the copy's
// draw the scorer says what it posed: `clip` at `normTime` (0..1; the jog, a wave, idle), or clip null =
// one of the port's own dances (which only move the Strikers bones): the guest's own celebration,
// `celebrateSeconds` in. active false after the draw.
void PortGuestScorerPose(bool active, const cSAnim* clip, float normTime, float celebrateSeconds);
// r124: Spider-Man's web shots on the life-size scorer since the last call (his web move's wrist flicks), in the copy's
// game space: 6 floats each (the wrist, then the way the web flies, not unit length). Returns how many (up to max).
int PortGuestTakeWebShots(float* out, int max);

// r102 (Lance: Bowser Jr. "gliding across the field upon goals scored"): what a drawn character's pose (its
// DrawableCharacter's accumulator, `acc`) came from, for a guest posed from its own clips. Live: the
// character's own controller (DrawableCharacter::Grab). Tree: the goal cut-scene's controller
// (EvaluateFrom) or a replay's recorded pose tree, whose heaviest clip the guest plays. Blend: a snapshot
// between two others (replays), the nearer one's.
class cPoseNode;
void PortGuestNoteLive(const void* acc);
void PortGuestNoteTree(const void* acc, const cPoseNode* tree);
void PortGuestNoteBlend(const void* acc, const void* lhs, const void* rhs, float rhsWeight);

// r67: the roster (STRIKERS_HOME/guests/roster.txt, tools/outside/roster.py) and the picks the game's
// own pick screens make. A guest plays as one Strikers character (its "base": stats, Super Strike,
// moves) and is drawn, named (and later voiced) as itself. Picks are kept per game mode and per team,
// a team named by its Strikers captain (two teams never share one); guests.txt (one class -> one
// guest everywhere) still applies where a team has no pick.
enum
{
    PORT_GUEST_INHERIT = -2,  // no override: use guests.txt
    PORT_GUEST_CAPTAIN = 0,
    PORT_GUEST_SIDEKICK = 1,
};
int PortGuestRosterCount(int role);
// The i-th guest of a role, in page order: its roster index (for picks), or -1.
int PortGuestRosterAt(int role, int i);
const char* PortGuestRosterPack(int index);
const char* PortGuestRosterName(int index);  // UTF-8, as shown
const char* PortGuestRosterGame(int index);  // "Melee", "Charged", "Melee Pokemon"
int PortGuestRosterBase(int index);          // eTeamID for a captain, eSidekickID for a sidekick
// The roster index of a pack of that role by name, -1 if this headset has none ("" or null: -1).
int PortGuestRosterFind(int role, const char* pack);
// r70c (Lance): evolved Pokemon (roster.txt's 6th field "franchise", 7th the form it evolves from) are
// on the character select menu as locked silhouettes until evolved in franchise mode (any career);
// then unlocked for every mode (STRIKERS_HOME/franchise/unlocked.txt). Franchise careers deal only the
// unevolved ones.
int PortGuestRosterFindAny(int role, const char* pack);  // any role entry by pack, -1 none
bool PortGuestRosterEvolved(int index);
const char* PortGuestRosterEvolvesFrom(int index);  // its previous form's pack, "" none
bool PortGuestRosterLocked(int index);               // evolved and not unlocked yet
bool PortGuestUnlock(const char* pack);              // true when newly unlocked
// r183: the ORIGINALS - Quest 3 Strikers' own characters (orig_<name>), shipped inside the APK (assets/originals ->
// STRIKERS_HOME/guests/, their rows in guests/originals.txt, read after roster.txt; no game file needed). Five 3-form
// lines evolve like a Pokemon; the five singles' alternate uniforms (<single>_b / _c) are costumes like a Marvel hero's
// (no shiny, "suit up", unlocked by level, LT / RT "UNIFORMS").
inline bool PortOriginalKey(const char* k) { return k != nullptr && k[0] == 'o' && __builtin_strncmp(k, "orig_", 5) == 0; }
inline bool PortOriginalUniformLine(const char* k)
{
    if (!PortOriginalKey(k))
        return false;
    static const char* const kSingles[5] = {"luchador", "racedriver", "goalie", "mascot", "redline"};
    for (const char* s : kSingles)
    {
        const unsigned long n = __builtin_strlen(s);
        if (__builtin_strncmp(k + 5, s, n) == 0 && (k[5 + n] == '\0' || k[5 + n] == '_'))
            return true;
    }
    return false;
}
// a Marvel hero's costume line or an original single's uniforms (the base look counts, as mua_<hero> does)
inline bool PortCostumeKey(const char* k)
{
    return k != nullptr && (__builtin_strncmp(k, "mua_", 4) == 0 || PortOriginalUniformLine(k));
}
// an alternate uniform itself (orig_<single>_b / _c): its card / name from franchise/costume_names.txt
inline bool PortOriginalUniform(const char* k)
{
    if (!PortOriginalUniformLine(k))
        return false;
    const unsigned long n = __builtin_strlen(k);
    return n > 2 && k[n - 2] == '_' && (k[n - 1] == 'b' || k[n - 1] == 'c');
}
// r70c: this match's side (0/1) has a shiny Pokemon captain (franchise): its <pack>_shiny.gpk if there.
void PortGuestSetShiny(int side, bool shiny);
// r78: this match's side's co-captain (include/port/lineup.h) as a guest: its roster index (-1: none,
// the Strikers captain of that class itself) and whether it's shiny. Drawn, named, voiced and on its
// own stats like a captain pick.
void PortGuestSetCoCaptain(int side, int rosterIndex, bool shiny);
// r130 (Lance: "let ANY spot be ANY character"): what a sidekick slot's own pick plays as (include/port/lineup.h):
// the eTeamID of the captain class it wants (a Strikers captain picked for it, PORT_GUEST_ORIGINAL + team: its own,
// *mayMove 0; a guest captain: its base, *mayMove 1, it takes the nearest free one when that's taken), or -1: the
// team's sidekick class (a Strikers or Charged sidekick, none). PickWant from a pick value (any of PortGuestSetSlotPicks'),
// SlotWant from slot `slot` (0-2) of `side` (its team's own pick in the current mode; online the match setup's).
int PortGuestPickWant(int pick, int* mayMove);
int PortGuestSlotWant(int side, int slot, int* mayMove);
// r130: a guest's captain slot from its base: the base when free, else the nearest free captain (kSlotOrder); -1 none.
int PortGuestSlotForBase(int base, const bool freeCaptains[8]);
// The guest a team (its captain eTeamID) has in the current mode for a role: roster index or -1.
int PortGuestGetPick(int captainTeam, int role);
void PortGuestSetPick(int captainTeam, int role, int index);  // -1 = original, -2 = inherit guests.txt
void PortGuestClearPicks();                                     // every team of the current mode
void PortGuestClearLock();  // r158: the app closing cleanly (XR session stopping): guests.lock is not a crash's
// Text keys of guest names (the localization table carries them, see PortGuestLocInject); 0 = no guest.
unsigned long PortGuestLocTeamName(int captainTeam);
unsigned long PortGuestLocSidekickName(int captainTeam, int sidekick);
// r79: the side's co-captain guest (PortGuestSetCoCaptain); r130: any sidekick slot (0-2) playing a captain class
unsigned long PortGuestLocSlotCaptainName(int side, int slot);
unsigned long PortGuestLocName(int index);
// r68b: "From <its game>" for a team whose captain is a guest (cup / tournament screens' line under
// the captain's name, the Strikers captain's description there); 0 = none.
unsigned long PortGuestLocTeamDesc(int captainTeam);
class nlLocalization;
void PortGuestLocInject(nlLocalization* loc);  // after every nlLocalization::Load
// The shown name (UTF-8) of the guest playing class `cls` for `side` this match, or nullptr.
const char* PortGuestShownName(int side, int cls);

// r67: a guest is its own character: its own stats (STRIKERS_HOME/guests/stats/<pack>.ini, the
// Strikers .ini keys, tools/outside) over the slot class's, for the player being made (CreateCharacter,
// before the fielder is built from them).
class FielderTweaks;
void PortGuestApplyTweaks(int side, int cls, FielderTweaks* tweaks);
// r130: the same for the player being made (its m_ID: a sidekick on its own slot's pick plays on that one's stats)
void PortGuestApplyTweaksPlayer(int side, int cls, int playerId, FielderTweaks* tweaks);
// r67b, online own stats: a guest's stats as its own headset reads them (PORT_GUEST_STATS values in
// CharacterTweaks key order, `mask` the keys its file sets); 1 if any. Sent in the match message,
// and on both headsets each side's guests play on what their own headset sent (SetOnlineStats,
// checked again here), so both simulate the same match.
enum
{
    PORT_GUEST_STATS = 28,
};
int PortGuestStatsFor(int index, float* values, unsigned* mask);

// r67b: a loading-screen picture the game is about to read (".../<team>_<l|r>[_bg|_white]", the
// pick screens' big portraits and the VS screen): for a team whose captain is a guest, the guest's
// own one (STRIKERS_HOME/guests/loading/<pack>_<l|r>[_bg|_white].gtx, tools/outside/loadres.py, the
// same 256 x 256 CI8 the game's are) read into `buf` instead; 1 if so.
int PortGuestLoadingPicture(const char* name, void* buf, unsigned long size);

// r68: guest voices (src/platform/xr_guest_voice.cpp). The roster index of the guest playing class
// `cls` for `side` (picks only), or -1.
int PortGuestIndexFor(int side, int cls);
// r130: the same for one player (cPlayer m_ID): each sidekick its own slot's pick
int PortGuestIndexForPlayer(int side, int cls, int playerId);
// r68, the announcer: the guest captaining `side` this match (roster index) or -1; and whether a
// team (its Strikers captain eTeamID) has a guest captain in the current mode, picked or being
// picked. A guest's name is never called in a Strikers captain's voice: those calls stay silent.
int PortGuestCaptainFor(int side);
// r130: the sidekick menu's "your own captain": the captain just picked on this screen (else PortGuestCaptainFor)
int PortGuestMenuCaptainFor(int side);
// r130: the cup / tournament screens' captain for `side` (eTeamID) while its sidekicks are picked (the Friendly's
// screen says it through PortGuestCaptainChosen)
void PortGuestPickerCaptainIs(int side, int team);
// r130: the Strikers captains (a bit per eTeamID) `side`'s sidekick slots can't have, as known on the menu: this side's
// captain, the other side's once picked on this screen (and its slots' Strikers captains), both Grudge co-captains.
unsigned PortGuestSlotCaptainsTaken(int side);
bool PortGuestTeamHasGuestCaptain(int captainTeam);
// r68b: the same by the game's team name (GetTeamName: "mario", "donkeykong" ...): the pick screens'
// team logo (a Strikers captain's emblem) is hidden for a guest captain.
bool PortGuestTeamNameHasGuestCaptain(const char* teamName);
// r68c: a Strikers sidekick's or keeper's kit on a team whose captain is a guest: the guest's own
// colours (STRIKERS_HOME/guests/kits/<toad|koopa|hammer|birdo|goalie>_<pack>.gtx, tools/outside/
// kitres.py from the game's own kits), as a texture handle for the kit swap; 0 = the slot's kit.
unsigned long PortGuestKitTexture(const cCharacter& character);
// r68c: a guest captain's team colours (STRIKERS_HOME/guests/colors.txt, tools/outside/guestcolors.py)
// as 0xRRGGBB, by roster index; 0 if it has none. For its controller skin, confetti and marker (xr_fx).
int PortGuestThemeColors(int index, unsigned* main, unsigned* trim, unsigned* patch);
// r68h: a sidekick's own generated name ("Toadley Crossbar"), the same every match and on both headsets:
// from its kind, its captain and which of the three it is (cPlayer m_ID 1-3); null for a captain.
const char* PortGuestSidekickName(int side, int playerId);
// r70: a name from the same pools for a sidekick of `kind` ("toad" ... or a Charged guest's pack), the
// same for the same `key` every time (the franchise names its clubs' sidekicks with it).
const char* PortGuestSidekickNameFor(const char* kind, const char* key);
struct SoundStrToIDNode;
// cGameSFX::Play, for a character voice: -1 if the player isn't a guest, else the sound's own volume
// (the player's whole voice table is set to nothing the first time: the game's sound goes on, silent).
float PortGuestVoiceMute(const void* sfx, SoundStrToIDNode* table, unsigned long count, unsigned long type);
// ... and where the game's sound would start: the guest's own clip of that kind (by the type's name),
// at that volume, panned / faded from the camera by `pos` (null: centred, no fade).
void PortGuestVoiceStart(const void* sfx, const char* type, int volGroup, float volume, const float* pos, float maxDist);
void PortGuestVoiceForget(const void* sfx);  // cGameSFX::SetSFX: a fresh table
void PortGuestVoiceStopAll();                // Audio::Silence
// r72: a guest (roster index) says its first clip of `labels` ("taunt,quote,cry"), centred, at the
// game's voice volume x volume (franchise nemesis taunts: their own voice, no written lines).
void PortGuestVoiceSay(int guest, const char* labels, float volume);
// r141: a Strikers captain (eCharacterClass) says its own line, through the game's own voice playback: the character
// select's "accept" voice (WORLDSFX_FE_ACCEPT_*, the menus' character voice group) - a BLACK 1/1's cameo. False when the
// game's menu voices aren't loaded now (silent).
bool PortStrikersVoiceSay(int characterClass);
// r99: the franchise evolution's music (guests/evo/music_build.evm / music_fanfare.evm, from his Melee disc):
// 0 the build-up while it evolves, 1 the victory fanfare as it comes out, -1 stop. The game's own sound
// dips under it.
void PortEvoMusic(int which);
// r138: a trading card's own sound (xr_cards.cpp's chimes): a mono 32 kHz clip, centred, at the voice volume x volume.
void PortCardSound(const short* pcm, int frames, float volume);
// r202: the same at the game's SOUND > EFFECTS volume (the SFX group) - the casino machine's win / lose music
void PortCardSoundSfx(const short* pcm, int frames, float volume);
// r230 (LANCINI EXQUISITE's private gallery): its mood now (0 none .. 1) - the menus' music + the crowd go quiet under it
void PortCardGallerySet(float level);
float PortCardGallery();
void PortGuestVoiceListener(const float* pos, const float* heading, const float* up);
extern "C" void PortGuestVoiceMix(int* left, int* right, int frames);  // audio_mix.cpp, game thread
void PortGuestSetOnlineStats(int side, int role, const float* values, unsigned mask);
// r80 (Lance: "let the sidekicks keep their own"): which stats a player's tweaks (its FielderTweaks) got
// from its guest, a bit per stat in the stats file's order (0: none, not a guest). cTeam::ResetCharacters
// copies the captain's aggression / dekeing / passing / shot and pass speeds onto every sidekick at each
// kick-off; a guest sidekick keeps its own of those.
unsigned PortGuestOwnStats(const void* tweaks);
void PortGuestClearOnlineStats();

// r67: the guest picker, a panel xr_quest.cpp shows over the game's own pick screens (Y opens it).
// The pick screen offers it every frame a side is choosing (its role, and which Strikers captains are
// still free); a pick made there comes back to that screen once (Take), which moves its own grid to
// the guest's slot and confirms, so the game goes on as if that slot was picked; the confirm reports
// the slot chosen (CaptainChosen / SidekickChosen), which keeps or clears the guest for that team.
// r68e: the character select menu lists the Strikers characters too; one picked there goes through the
// same hand-off as a guest, as PORT_GUEST_ORIGINAL + its eTeamID (a captain) or
// PORT_GUEST_ORIGINAL_SIDEKICK + its eSidekickID: its own slot, no guest (PortGuestSetPick takes any
// number that is no guest as "no guest").
enum
{
    PORT_GUEST_ORIGINAL = 1000,
    PORT_GUEST_ORIGINAL_SIDEKICK = 1100,
    PORT_GUEST_TRIO = 1200,  // r130: + the eSidekickID the three sidekicks picked on the menu play as
};
bool PortGuestPickerCaptainFree(int team);  // as the screen choosing offers it (the other side's taken)
void PortGuestPickerOffer(int side, int role, const bool freeCaptains[8]);
bool PortGuestPickerOffered(int* side, int* role);  // offered within the last few frames
void PortGuestPickerCommit(int side, int role, int index);
bool PortGuestPickerTake(int side, int role, int* index);
// r130 (Lance: "you can have anybody play a sidekick now"): the menu's three sidekick picks (roster indexes of any role,
// PORT_GUEST_ORIGINAL + eTeamID a Strikers captain, -1 the Strikers sidekick; slots before firstSlot are the
// co-captain's) go to the pick screen as one pick,
// PORT_GUEST_TRIO + the sidekick class they play as; its confirm (PortGuestSidekickChosen) gives each slot its own.
void PortGuestPickerCommitTrio(int side, const int picks[3], int firstSlot, int sidekickClass);
// A team's three sidekick slots' own picks (picks null: none, the team's one sidekick pick for all three, as before).
// r130: a pick is a roster index (any role), PORT_GUEST_ORIGINAL + eTeamID (a Strikers captain: its own class), or
// anything else = the Strikers sidekick.
void PortGuestSetSlotPicks(int captainTeam, const int* picks, int firstSlot);
// r130: one of them in the current mode (slot 1-3 = cPlayer m_ID): a roster index, PORT_GUEST_ORIGINAL + eTeamID, -1 the
// Strikers sidekick, PORT_GUEST_INHERIT none (the team's one sidekick pick). The online match setup carries them (Game.cpp).
int PortGuestGetSlotPick(int captainTeam, int slot);
// r130, online: a side's three sidekick slots as the match setup carried them (both headsets the same): kinds[k] 0 none
// (the side's one sidekick pick), 1 the Strikers sidekick, 2 a guest of any role by pack (one this headset lacks: drawn
// as the slot's own class, logged), 3 a Strikers captain (wants[k]: its eTeamID), on the stats its own headset sent
// (masks: the keys set). This match's players use them online (until PortGuestClearOnlineStats); also kept as the team's
// slot picks (PortGuestSetSlotPicks). The classes they play: include/port/lineup.h PortSetSlotCaptains.
void PortGuestSetOnlineSlots(int side, int captainTeam, const unsigned char kinds[3], const int wants[3],
                             const char packs[3][24], const float stats[3][PORT_GUEST_STATS], const unsigned masks[3]);
// A sidekick slot's (m_ID 1-3) shiny this match (franchise members).
void PortGuestSetSlotShiny(int side, int playerId, bool shiny);
// The Strikers slot a guest plays in: its own base when free, else the nearest free captain; -1 none.
int PortGuestSlotFor(int index, const bool freeCaptains[8]);
void PortGuestCaptainChosen(int side, int team);
void PortGuestSidekickChosen(int side, int team, int sidekick);
// r79: Grudge Match co-captains (include/port/lineup.h), picked on the character select menu before a
// side's sidekicks: whether it offers one (a Friendly, not for the Super Team); whether a Strikers captain
// (eTeamID) can be `side`'s co-captain (no team's captain, not the other side's co-captain); the slot a
// menu pick (PORT_GUEST_ORIGINAL + team, or a guest captain) plays in as one (-1: none free); picking it
// (pick < 0: no co-captain; false: not one it can be); and the menu pick it is now (-1 none).
bool PortGuestCoCaptainOffered(int side);
bool PortGuestCoCaptainFree(int side, int team);
int PortGuestCoCaptainSlotFor(int side, int pick);
bool PortGuestCoCaptainPick(int side, int pick);
int PortGuestCoCaptainCurrent(int side);
// After a Take, the screen holds the guest for that side and role until its slot is confirmed
// (the screen may still be animating); PortGuestPendingSlot is that slot while held, else -1.
void PortGuestPickerHold(int side, int role, int index, int slot);
int PortGuestPendingSlot(int side, int role);

#endif // PORT_XR_GUEST_H
