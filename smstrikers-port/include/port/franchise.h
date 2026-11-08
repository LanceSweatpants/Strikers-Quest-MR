#ifndef PORT_FRANCHISE_H
#define PORT_FRANCHISE_H

// strikers-quest r70: franchise mode (Lance's design, C:\StrikersMR\FRANCHISE-PLAN.md; hook map in
// FRANCHISE-TECH.md). The Strikers Super League: 32 clubs (STRIKERS_HOME/franchise/clubs.txt, made by
// tools/outside/franchise_data.py from franchise/clubs.json) in 2 conferences of 4 divisions; when a
// career starts, every captain (the 8 Strikers captains and the guests) and a sidekick kind are dealt to
// the clubs at random (26 clubs also get a backup captain), each club's 3 sidekicks named; 20 weeks of
// regular season (6 division games, 10 more in the conference, 4 against the other one). Your club's
// match each week is played (a Friendly with both clubs' players, guests and named sidekicks), the
// others are worked out from the clubs' strength; standings, results, 3 career slots.
// Step 1: league, schedule, play, standings. Playoffs, evolutions, A-Life and Nemesis come later.
//
// Everything here runs on the game thread (the pad hook in xr_quest.cpp and PortDebugFrame); the
// headset's panels draw from the snapshots PortFranchiseView fills.

enum
{
    PORT_FR_SLOTS = 3,
    PORT_FR_CLUBS = 32,
    PORT_FR_WEEKS = 20,
    // r76: the co-op career (franchise/coop.txt), beside the three slots: played online with the friend
    PORT_FR_COOP_SLOT = 3,
    // r100: this headset's copy of its online league (franchise/league.txt; the league's code inside)
    PORT_FR_LEAGUE_SLOT = 4,
};

struct PortFranchiseSlot
{
    bool used;
    char club[64];  // the club's full name
    char clubId[32];
    int season, week, wins, losses;
    unsigned careerId, version;  // r76: its id (seed) and how many times saved
};
// The three career slots as saved (STRIKERS_HOME/franchise/career<1-3>.txt).
void PortFranchiseSlots(PortFranchiseSlot out[PORT_FR_SLOTS]);
bool PortFranchiseLoad(int slot);          // continue that career; false if empty / unreadable
bool PortFranchiseNew(int slot, int club); // a new career with that club; the players dealt at random
// r75 (Lance: "i didnt pick voltorb, it just chose him for me"; his call: "my club only"): a new career
// with your own club's players picked: the captain, the backup captain and the sidekick kind as the
// character select menu's picks (a guest's roster index, or PORT_GUEST_ORIGINAL(_SIDEKICK) + the
// Strikers id; unevolved captains only); backupPick -1 = no backup captain. Every other club is dealt at
// random from the rest, as before.
// r131 (Lance: "each character is only chosen once per franchise. you as the 1UP in single player, you pick first, and
// the league forms around your picks, never duplicating"): memberPicks = your three sidekick slots' menu picks (as
// PortFranchiseSetMembers takes them; null: generic sidekicks). A character (its whole evolution line, every costume of a
// hero) is on one club only: the 31 others are dealt around your picks - a captain, a backup and three named sidekick
// members each from what's left, a snake by rating - and only when that runs out a club has no backup / generic
// sidekicks (a roster with fewer captains than clubs: those clubs get a dealt captain again).
bool PortFranchiseNewPicked(int slot, int club, int captainPick, int backupPick, int sidekickPick,
                            const int* memberPicks = nullptr);
// r131: which of `count` menu picks are characters already taken (out[i] = 1): the same character as one of `held`
// (your captain, backup, earlier sidekick slots in this pick; a Charged sidekick guest may fill several of your slots),
// and with `career` (the member editor) one another club of the loaded career has. The Strikers sidekicks never are.
void PortFranchisePicksTaken(const int* picks, int count, const int* held, int heldCount, bool career, unsigned char* out);
// r75: whether a character select menu pick can start a career as your captain (an unevolved captain).
bool PortFranchiseCaptainPickable(int pick);
void PortFranchiseDelete(int slot);
// r76, franchise co-op (Lance: online on their own headsets, the same club, both keep the career;
// include/port/xr_net.h). PortFranchiseLoad / PortFranchiseNewPicked take PORT_FR_COOP_SLOT for it.
bool PortFranchiseCoopFile(PortFranchiseSlot* out);  // the co-op career saved on this headset
bool PortFranchiseCoopActive();  // the loaded career is the co-op one
bool PortFranchiseCoopGuest();   // ... and the friend's headset runs it (no hub actions here)
bool PortFranchiseCoopShare();   // the host: send the friend the career as saved now
// The careers card's state: 0 no friend in the room; 1 the friend is here, nothing heard from them yet;
// 2 here, ready (this headset hosts: A opens / starts it); 3 here, the friend hosts and hasn't opened it;
// 4 here, the friend hosts and has it open (A joins); 5 waiting for a career to come over.
int PortFranchiseCoopState();
// A press on the co-op card: opens it (host: the newer copy of both, asking the friend for theirs when
// it's newer; a new co-op career when neither has one: returns 2 for the club picker), joins it (guest),
// or starts waiting for it. 1 = the hub opens on it now, 2 = new career, 0 = not now.
int PortFranchiseCoopOpen();
// True once when a career this headset waited for (state 5) came in and is open: the hub goes to it.
bool PortFranchiseCoopTakeOpened();
// Counts up each time the loaded career changes from outside (the friend's copy came in), so the hub
// draws it again.
unsigned PortFranchiseChangeSerial();
// A match of the loaded career is starting (asked for, or waiting for the friend at its start).
bool PortFranchiseMatchStarting();
bool PortFranchiseActive();                // a career is loaded
// r153 (Lance: "i want EACH save/franchise to be a FRESH card binder with a fresh slate of RNG"): the loaded career's
// id (its seed; 0 none loaded) - xr_cards.cpp keeps a binder per career by it
unsigned PortFranchiseCareerSeed();
void PortFranchiseClose();                 // back to no career (the menu closed)

// The clubs (all careers share them).
int PortFranchiseClubCount();
const char* PortFranchiseClubId(int club);      // "deathmountain" (its logo: guests/portraits/club_<id>.png)
const char* PortFranchiseClubName(int club);    // "Death Mountain Dodongos"
const char* PortFranchiseClubPlace(int club);   // "Death Mountain"
const char* PortFranchiseClubMascot(int club);  // "Dodongos"
const char* PortFranchiseDivisionName(int club); // "Inferno"
const char* PortFranchiseConferenceName(int club); // "Crown"
unsigned PortFranchiseClubColor(int club, int which); // 0xRRGGBB: 0 main, 1 second, 2 accent

// What the hub shows, filled from the loaded career.
struct PortFranchiseRow
{
    int club;
    int wins, losses, goalsFor, goalsAgainst;
    bool mine;
};
// r70b: a playoff series (best of 5) as the hub shows it.
struct PortFranchiseSeries
{
    int round;  // 1 Kickoff Round, 2 conference semifinals, 3 conference finals, 4 Super Strike Series
    int conf;   // 0 Crown, 1 Star, -1 the Super Strike Series
    int hi, lo, hiSeed, loSeed, winsHi, winsLo;  // hi = the higher seed (home in games 1, 2, 5)
    int winner;                                  // club, -1 still going
};
enum
{
    PORT_FR_PHASE_REGULAR = 0,
    PORT_FR_PHASE_PLAYOFFS = 1,
    PORT_FR_PHASE_DONE = 2,  // champion crowned; A starts the next season
};
struct PortFranchiseView
{
    bool active;
    int slot, season, week;  // week 1-20 = next to play; 21+ = the playoffs' game days
    // r70b: the playoffs
    int phase, round, champion;          // round 1-4 while in the playoffs; champion club or -1
    int seeds[2][7];                     // each conference's seeds 1-7 (in the regular season: as it stands)
    PortFranchiseSeries series[15];
    int seriesCount;
    char roundName[48];                  // the round on now ("Kickoff Round")
    char roundLogo[48];                  // its logo (guests/portraits/<roundLogo>.png)
    char status[96];                     // why there's no match for you today ("BYE", "ELIMINATED" ...)
    bool canSimDay;                      // no match of yours today: A plays out the day
    int myGameNo, mySeriesMe, mySeriesThem; // your series today: game 1-5 and the wins so far
    int titles;                          // your club's Super Strike Series wins in this career
    // r70c: growth (Lv 8 at the start), shiny, the league's news (evolutions ...)
    int captainLevel, backupLevel, sidekickLevel;
    int captainXp, backupXp, sidekickXp; // toward the next level (100)
    bool captainShiny, backupShiny;
    char news[8][112];
    int newsCount;
    bool evolutionPending;
    int difficulty;  // r97: 1 Rookie, 2 Professional, 3 Superstar, 4 Legend
    // r71: A-Life. The scoring race, who's hot and cold, the players' feed, awards and the hall of fame.
    struct Leader
    {
        char name[48];
        int club, goals, form;  // form -3 (slump) .. +3 (on fire)
        char key[48];           // r113: its portrait (guests/portraits/<key>.png)
    } leaders[10], hot[5], cold[5];
    int leaderCount, hotCount, coldCount;
    char feed[8][128];           // "@NAME: ..." (last week's posts)
    int feedCount;
    char awards[8][112];         // this season's awards night (once crowned), else last season's
    char awardKeys[8][48];       // r73: each one's portrait (guests/portraits/<key>.png), "" none
    int awardCount;
    char history[16][112];       // the hall of fame: champions, MVPs, Golden Boots, records
    int historyCount;
    int captainGoals, backupGoals, sidekickGoals[3], sidekickAge[3];  // your players this season
    int captainForm, backupForm;
    bool tradeDeadlinePassed;
    // r72: your nemeses (hottest first; active ones before the cooled), and the next match's
    struct Nem
    {
        char name[48], title[32], key[48];  // key: a captain's portrait key ("" a sidekick)
        int club, heat, rank, beatYou, lostToYou;
        bool active, teammate;
        char traits[3][48];
        int traitCount;
        char last[96];                      // the newest history line
    } nems[8];
    int nemCount;
    int nextNemesis;                         // index into nems of the next opponent's nemesis, -1 none
    int myClub;
    int wins, losses, goalsFor, goalsAgainst, divisionRank;
    char captain[48], backup[48], kind[32], sidekicks[3][48];
    char captainKey[48], backupKey[48];  // portraits (guests/portraits/<key>.png)
    char kindKey[48];                    // r116: the sidekicks' portrait
    bool startsBackup;                   // your backup captain starts the next match
    int nextOpponent;                    // club, -1 none
    bool nextHome;
    int oppWins, oppLosses;
    char oppCaptain[48];
    PortFranchiseRow division[4];        // your division, ranked
    PortFranchiseRow conference[2][16];  // both conferences, ranked
    char results[16][96];                // last week's results ("CINDER CITY 3 - 1 MUTE CITY")
    int resultCount;
    // r113 (the newspaper, logo v logo): last week's / game day's games as numbers
    struct Played
    {
        int home, away, homeGoals, awayGoals;
        bool overtime, mine;
    } played[16];
    int playedCount;
    int lastWeek;                        // the week they were (0 none yet)
    char clubCaptainKey[PORT_FR_CLUBS][48];  // r113: each club's starting captain (its portrait key)
    char headline[224];                  // your last match, or the season's state
    // r131: your next opponent's other players: its co-captain ("" none) and the three sidekicks by name (a member's own)
    char oppCoCaptain[48];
    char oppSidekicks[3][48];
    char memberKeys[3][48];  // r173: your three sidekick slots' characters ("" a generic sidekick)
};
bool PortFranchiseGetView(PortFranchiseView* v);

// Your club's next match: started from the main menu on the next frame (PortFranchiseFrame).
bool PortFranchisePlayNext();
bool PortFranchiseCanPlay();             // on the main menu, a career loaded, a match left
void PortFranchiseSwapStarter();         // your backup captain starts instead (or back)
bool PortFranchiseOnMainMenu();          // the game is on its main menu (for the hub's tip)
// r70b: no match of yours today (a bye, out, or your series won and waiting): the day played out
// (toYourNext: every day until you have a match again, or the champion is crowned).
bool PortFranchiseSimDay(bool toYourNext);
bool PortFranchiseNextSeason();          // the champion crowned: season + 1 (same clubs, new schedule)
// r70c: one of your Pokemon is ready to evolve (Lv reached): you watch it, or cancel it (B; it asks
// again at its next level); a split (Poliwhirl) lets you choose. The first evolution of a form in any
// career unlocks it on the character select menu for every mode.
struct PortFranchiseEvolution
{
    bool valid;
    bool backup;                  // your backup captain (else your captain)
    int level;
    char fromKey[48], fromName[48];
    int options;                  // 1, or 2-5 for a split (r130: Eevee's five, Tyrogue's three)
    int slot;                     // r130: 0 your captain, 1 the backup, 2-4 your sidekick members
    char toKey[5][48], toName[5][48];
    bool shiny;                   // already shiny (stays shiny)
    bool postponed;               // r95: you said "not now" at this level (the hub doesn't ask again)
};
bool PortFranchisePendingEvolution(PortFranchiseEvolution* out);
// r95 (Lance: "make an EVO page, where we can witness the EVO happening, as well as confirm or postpone
// the evo"): any of yours ready to evolve, postponed ones too (the Evo Watch page's A), captain first.
bool PortFranchiseReadyEvolution(PortFranchiseEvolution* out);
// choice: the option (0/1) to evolve into, -1 to cancel. Returns true when it evolved; *shiny = shiny
// after it (a new shiny roll: *newShiny).
bool PortFranchiseEvolve(int choice, bool* shiny, bool* newShiny);
// r95: the same for your captain (backup false) or backup captain, postponed or not; choice -1 =
// postpone (not asked again until its next level; the Evo Watch page can still evolve it).
bool PortFranchiseEvolveSlot(bool backup, int choice, bool* shiny, bool* newShiny);
// r130: the same by slot: 0 captain, 1 backup, 2-4 your sidekick members
bool PortFranchiseEvolvePlayer(int slot, int choice, bool* shiny, bool* newShiny);
// r130: your club's three sidekick members (menu picks: a guest's roster index, a Strikers sidekick for a generic one);
// each its own level, evolutions / costumes and shiny. Not in an online league. r131: a character another club has (or
// your captain / backup, or one already in an earlier slot) is left out: a generic sidekick in that slot.
bool PortFranchiseSetMembers(const int picks[3]);
bool PortFranchiseMemberPicks(int members[3], int* captain, int* backup);  // r130: the member editor's start

// r97 (Lance: "we need a difficulty setting for franchise mode ... a real life nemesis/A-Life system where im
// not always dominating or being the MVP"): the career's difficulty, 1 Rookie, 2 Professional (older
// careers), 3 Superstar, 4 Legend: the game's own skill level for its matches (GameInfo.cpp asks
// PortFranchiseSkillLevel, -1 when no franchise match is on), and the league pushing back (a worked-out
// game of yours a little harder, the CPU stars scoring more of their clubs' goals, CPU clubs growing
// faster, a nemesis hunting your captain more and its grudges building faster). Set from the hub's
// DIFFICULTY page (the friend's headset in co-op: the host's career decides).
int PortFranchiseSkillLevel();
int PortFranchiseDifficulty();
bool PortFranchiseSetDifficulty(int level);
// r77 (Lance: "an XP meter ... as they score, or assist or win, we see the XP going towards them,
// potentially seeing that we've earned an evolution during the game, so we can have it occur
// afterwards"): through a franchise match, your club's starting captain and sidekicks as the match
// stands: what they had, what it gives them if it ended now (the win / loss, the club's goals, their own
// goals and assists, playoff and nemesis bonuses), where that takes them, and whether the captain
// reaches its evolution (it happens after the match, on the hub).
struct PortFranchiseLive
{
    bool valid;
    bool pokemon;            // the captain can evolve at all
    bool costume;            // r111: a Marvel hero (its next form is its next costume)
    char name[48];
    int level, xp, gain, newLevel, newXp;
    int goals, assists;      // its own, this match
    bool won;                // as the score stands
    int evoLevel;            // the level its next form comes at (0 none)
    char evoName[48];
    bool evolves;            // newLevel reaches it
    char kind[48];           // the sidekicks (one level for the three)
    int skLevel, skXp, skGain, skNewLevel, skNewXp;
    // r79: the co-captain (your other captain, on the field since r78; co false: none), the same numbers
    bool co;
    char coName[48];
    int coLevel, coXp, coGain, coNewLevel, coNewXp;
    int coGoals, coAssists;
    int coEvoLevel;
    char coEvoName[48];
    bool coEvolves, coPokemon, coCostume;
};
bool PortFranchiseLiveXp(PortFranchiseLive* out);
// r173 (Lance: "since we can see everybody's XP/EVO level at the hub now, take that away from the game screen ... if someone
// levels up during a game, just have them luminated for a second as well as a 'level up' or '+1' graphic above them"): the
// in-match XP meter is gone; instead each player on the field, either club, is followed: its level as the match stands
// (the live meter's own numbers: the result, the club's goals, its own goals and assists, playoff / beef / difficulty /
// card bonuses) - one going higher than it has shown this match is a level-up moment. The XP itself is awarded at the end
// as always (nothing here changes it). Game thread; the headset takes them each frame.
struct PortFranchiseLevelUp
{
    int side;          // 0 home, 1 away
    int playerId;      // cPlayer m_ID: 0 the captain, 1-3 (1: the co-captain when one plays)
    int level;         // the level it reaches
    char name[48];
};
int PortFranchiseTakeLevelUps(PortFranchiseLevelUp* out, int max);
// r173: where that player is now (game units) and on the broadcast picture (ndc -1..1, *ndcValid 0 off screen); false gone
bool PortFranchisePlayerWhere(int side, int playerId, float pos[3], float ndc[2], int* ndcValid);
// r77 (Lance: "an evo watch where we see the current levels of all pokemon that are in our franchise"):
// every Pokemon in the league (each club's captain and backup), its level and XP, its next form and the
// level it comes at, and how many levels it gained this season, best first. Fills up to max, returns how
// many.
struct PortFranchiseEvoRow
{
    char name[48];
    char key[48];
    int club;                // the club's index
    bool mine, backup, shiny;
    int level, xp;
    int seasonGain;          // levels gained this season
    int evoLevel;            // 0: its last form
    char evoName[48];
    bool ready;              // r95: yours and ready to evolve now (A on the Evo Watch page)
    bool costume;            // r111: a Marvel hero (its next form is its next costume)
};
int PortFranchiseEvoWatch(PortFranchiseEvoRow* out, int max);
// r77 (Lance: "i couldnt see how tf to operate trades"): your own trades, until the deadline (week 12)
// and after the champion is crowned (the off-season): give your captain (what 0), your backup captain
// (1) or your sidekicks (2); get another club's captain or backup (for a captain) or its sidekicks (for
// yours). Value = the player's rating + a point a level over 8 (the trio: the kind's rating + its level);
// the other club says yes when what it gets is worth at least 90% of what it gives.
bool PortFranchiseTradeOpen();
struct PortFranchiseTradeOffer
{
    int club;
    bool backup;          // their backup (else their captain; sidekicks: unused)
    char name[48];
    int level, value;
    bool shiny, fair;
    char key[48];         // r116: its portrait
    int slot;             // r173: a sidekick's: its slot on their club (0-2), -1 a captain / backup
};
// r173 (Lance: "when it comes to sidekicks, i should be able to trade them individually, not as a fuckin bunch deal"): `what`
// 0 your captain, 1 your backup, 2-4 your sidekick in slot 1-3 (a named character: a generic sidekick is no trade piece),
// each for one player of theirs: a captain / backup for a captain or backup, a sidekick for one of their named sidekicks.
// The players you could get for `what`, best value first; *myValue / myName = what you'd give.
int PortFranchiseTradeOptions(int what, PortFranchiseTradeOffer* out, int max, int* myValue, char* myName, int nameSize);
// The trade (an offer from PortFranchiseTradeOptions); false when refused (not fair, window shut).
bool PortFranchiseTrade(int what, int club, bool backup, int theirSlot = -1);  // r173: theirSlot = an offer's slot
// r70b: your own match worked out instead of played (toPlayoffs: every week to the end of the regular
// season, yours too).
bool PortFranchiseSimMine(bool toPlayoffs);
// r70b: the playoff match being played (presentation): its round (1-4), conference (-1 the Super
// Strike Series), this game's number (1-5), each side's series wins before it (side 0 home, 1 away),
// its round's logo key. False outside a franchise playoff match.
bool PortFranchisePlayoffMatch(int* round, int* conf, int* gameNo, int winsBefore[2], const char** logoKey);
void PortFranchiseRenderPitch();         // GameRenderTask::Run: the round's logo + banners (franchise_show.cpp)
void PortFranchiseIntroBegin();          // cGame::BeginGame: a franchise match's intro starts
// r71: a goal in a franchise match (side 0 home / 1 away; who: 0 the captain, 1-3 a sidekick, -1 none)
void PortFranchiseGoal(int side, int who, int goals);
// r72: a hit in a franchise match (sides 0/1; who: 0 the captain, else the sidekick's m_ID)
void PortFranchiseHit(int attackerSide, int attacker, int victimSide, int victim);
// r73: how hard a CPU side hunts your captain (0 none: not a franchise match against your nemesis's
// club; more for the nemesis itself and its rank), and a roll of it (the port's own random numbers,
// never the game's).
float PortFranchiseHunt(int side, bool isCaptain);
bool PortFranchiseHuntRoll(float chance);

// r70b: the franchise match on now, for the headset's intro card and series board (game thread).
struct PortFranchiseCard
{
    bool valid, spectating;
    int serial;                // + 1 at every match intro (BeginGame)
    int club[2];               // home, away
    int seed[2];               // playoff seeds (0 in the regular season)
    int wins[2], losses[2];    // regular-season records
    int week;                  // regular season: the week
    int round, conf, gameNo;   // playoffs: round 1-4 (0 regular season), conference, game 1-5
    int seriesWins[2];         // each side's series wins before this game
    char roundName[48];
    char roundLogo[48];
    // r72: the face-off: the opponent's nemesis (if it starts), with its title and record against you
    bool nemesis;
    int nemesisSide;
    char nemesisName[48], nemesisTitle[32], nemesisKey[48];
    int nemesisBeatYou, nemesisLostToYou, nemesisRank;
};
bool PortFranchiseMatchCard(PortFranchiseCard* out);
// r70b: playoff bunting in your room while your club is still in it (or the champion): your colours.
bool PortFranchiseBunting(unsigned* rgb1, unsigned* rgb2, bool* champion);
// r70b: today's other games (none of yours today): watch one live, CPU v CPU (its result counts; the
// rest of the day is played out after it).
struct PortFranchiseToday
{
    int count;
    int home[16], away[16], series[16];  // series: index into the view's series, -1 none
};
void PortFranchiseTodayGames(PortFranchiseToday* out);
bool PortFranchiseWatch(int index);      // index into PortFranchiseTodayGames

// r100, online leagues (Lance: "UP TO 8 player online ... leagues"; ONLINE-LEAGUE.md): one career shared by 2-8
// headsets through the room server (include/port/xr_league.h), each person on a club of their own, the rest the
// computer's. This headset's copy is PORT_FR_LEAGUE_SLOT (PortFranchiseLoad opens it). A week goes on once every
// person's game of it is in (the league's owner can sim the rest); a game between two people is played online
// when both say ready (the hub), the home club's headset hosting it.
bool PortFranchiseLeagueFile(PortFranchiseSlot* out, char* code, int codeSize);  // the league saved here, its code
// A new league with your club and picks (as PortFranchiseNewPicked), up to maxMembers people; its code comes out
// in the hub's headline and the league view. r131: memberPicks as PortFranchiseNewPicked's (the league dealt around them).
bool PortFranchiseLeagueCreate(int club, int captainPick, int backupPick, int sidekickPick, int maxMembers,
                               const int* memberPicks = nullptr);
// Joining one by its code ("" stops): the lobby below says how it goes; pick a free club, then it opens
// (PortFranchiseLeagueTakeOpened, once: the hub goes to it).
void PortFranchiseLeagueJoin(const char* code);
struct PortFranchiseLeagueLobby
{
    int state;  // 0 none, 1 connecting, 2 no league with that code, 3 pick a club, 4 joining, 5 joined, 6 full
    char code[12];
    int max, members;
    char owner[32];
    bool taken[PORT_FR_CLUBS];
    char takenBy[PORT_FR_CLUBS][32];
    char captain[PORT_FR_CLUBS][48];  // each club's starting captain in the league ("" not read yet)
    int season, week;
    char error[96];                   // the server's last refusal
};
bool PortFranchiseLeagueLobbyGet(PortFranchiseLeagueLobby* out);
bool PortFranchiseLeagueClaim(int club);
bool PortFranchiseLeagueTakeOpened();
// The open league as the hub shows it.
struct PortFranchiseLeagueView
{
    bool active;     // the open career is an online league's
    char code[12];
    int link;        // 0 off, 1 connecting, 2 connected
    bool owner;      // this headset owns the league (SIM THE REST, DIFFICULTY)
    int max;
    struct Member
    {
        char name[32];
        int club;
        bool online, owner, me;
        bool done;   // their game this week is in (or they have none)
        bool ready;  // waiting for their online game
    } members[8];
    int memberCount;
    bool synced;     // this copy is the league's newest, nothing of yours still to send
    int toSend;
    int vsMember;    // your game this week is against a person: their index in members, -1 not
    bool meReady, themReady;
    bool canSimRest; // the owner, people's games of this week still to play
    char error[96];
};
bool PortFranchiseLeagueGetView(PortFranchiseLeagueView* out);
// Ready (or not) for this week's game against a person's club: once both are, it starts on both headsets.
// False: no such game, or this copy isn't the newest yet (try again in a moment).
bool PortFranchiseLeagueSetReady(bool on);
// The owner: this week's games still to play (the people's too) worked out, the league on.
bool PortFranchiseLeagueSimRest();
// Leave the league (your club back to the computer) and delete this headset's copy.
void PortFranchiseLeagueLeave();

// Hooks (game thread).
void PortFranchiseFrame();               // PortDebugFrame, every frame
void PortFranchiseMatchEnd(int scoreHome, int scoreAway, bool overtime); // StatsTracker::TrackWinner
bool PortFranchiseMatchActive();         // a franchise match is being played
int PortFranchiseMatchSeconds();         // r86: its length (240) while one is starting or played, else 0
// A sidekick's name in a franchise match (side 0 home / 1 away, cPlayer m_ID 1-3); null otherwise.
const char* PortFranchiseSidekickName(int side, int playerId);

// r137: STRIKERS TRADING CARDS: the packs your franchise matches earned since the last call (1 a match played, 2 for a
// win, +1 a playoff series won, +1 a trophy), and what earned them (*flags: 1 a win, 2 a series won, 4 a trophy).
// The headset takes them every frame (src/platform/xr_cards.cpp rolls and keeps them).
int PortFranchiseTakeCardPacks(unsigned* flags);
// r140: TIZM COINS your played franchise matches earned since the last call (your side's goals, Super Strikes, assists,
// tackles, steals, item hits, a win, a shutout; simmed games none), and the last match's breakdown into why ("3 goals 18,
// 2 assists 8, 14 tackles 7, win 10"). The headset takes them every frame (xr_cards.cpp keeps them, for its shop).
int PortFranchiseTakeCardCoins(char* why, int cap);
// r146 (Lance: "bonuses for duplicates should you not trade em in as well as in game multipliers due to set bonuses"):
// LANCINI TIZM card bonuses, worked out by xr_cards.cpp from your collection and set again whenever it changes. Franchise
// careers on this headset only (never co-op, an online league, or a quick match), your club's players only: a
// character's XP (its card's level - copies you keep: x2 LV2 .. x10 LV5, +10% a level over 1 -, its evolution line complete
// +15%, each of its Pokemon types complete +10%; all together at most +60%: more XP, so it evolves sooner), its
// evolutions' shiny odds (its line complete: 1 in 96, not 128), and the TIZM COINS your played matches pay (+2% a full
// binder page, at most +20%). key: a card key ("pk_charizard", "strikers_toad" - a sidekick kind's card).
struct PortFranchiseCardBonus
{
    char key[48];
    int xpPct;      // more XP, percent (0 - 60)
    int shinyOdds;  // its evolutions shiny 1 in this (0: the usual 128)
    char why[64];   // "LV3 +20%, LINE +15%, FIRE +10%"
};
void PortFranchiseSetCardBonuses(const PortFranchiseCardBonus* list, int count, int coinPct, const char* coinWhy);

// r167 (Lance: "leftovers become free agents ... do an offer system of sorts so teams are actually competing to land agents
// and its not richest team signs everyone"): FREE AGENCY. Every character on no club (never dealt, or released) is a free
// agent with its own level, record and personality. Signing is free (no coins); the window is the trade window: weeks 1-12
// and the off-season (champion crowned -> the next season's start). You (and every CPU club, on its needs) make offers: a
// ROLE (captain / backup / a sidekick slot; a backup can be promised the starts) and, for the spot, who goes (its holder is
// released into free agency when the agent signs). Each agent weighs its offers - the role, the club's record and strength
// (contenders), its personality (a front-runner chases winners, a hothead wants the captaincy, an honorable one stays loyal
// to its old club, an underdog wants to carry a weak one ...), its home region, squad fit (no sitting behind a better
// captain) - and never joins a club whose captain it has active beef with. Mid-season it decides after the next match day
// (by the week-12 deadline at the latest); off-season offers are decided when the next season starts. Deterministic from
// the career (its seed / rng), so a save, co-op and an online league agree. The r131 rule holds: one character, one club.
enum
{
    PORT_FA_CAPTAIN = 0,
    PORT_FA_BACKUP = 1,
    PORT_FA_SIDEKICK1 = 2,  // + 0..2 (the sidekick slots)
    PORT_FA_ROLES = 5,
};
struct PortFranchiseFaWindow
{
    bool active;         // a career loaded
    bool open;           // offers can be made now
    bool offSeason;      // the off-season window (decisions at the next season's start)
    int week;            // the week now (regular season)
    int agents;          // free agents now
    int myOffers;        // your offers out
    char status[112];    // "OPEN TO WEEK 12: AGENTS DECIDE AFTER THE NEXT MATCH DAY" / "SHUT ..."
    char last[112];      // your last free-agency result ("CHARIZARD SIGNED WITH YOU" ...), "" none
};
bool PortFranchiseFaWindowGet(PortFranchiseFaWindow* out);
struct PortFranchiseFreeAgent
{
    char key[48], name[48];
    int rating, level, value;     // value = rating + a point a level over 8 (the trades' number)
    bool shiny, captainType;      // captainType: can be a captain / backup (else a sidekick only)
    char game[16];                // "POKEMON", "MARVEL", "MELEE", "STRIKERS", "CHARGED"
    int stage, stages;            // its form in its line (stage 2 of 3), 1 / 1 a one-form character
    int personality;              // PERS + 1: 0 neutral, 1 hothead .. 8 front-runner
    char persName[24];
    int homeClub;                 // its home ground's club, -1 none (a Pokemon's home is a division: homeName)
    char home[32];                // "INFERNO" (a division) / "LAKE HYLIA" (a club) / ""
    int formerClub;               // the club that released it, -1 never on one
    int interested;               // clubs with an offer out
    int leader;                   // the club whose offer leads now, -1 none
    int leaderRole;               // its role (PORT_FA_*)
    bool leaderPromise;
    char leaderWhy[48];           // why it leads ("WANTS THE CAPTAINCY", "CHASES A WINNER", "HOMETOWN CLUB" ...)
    int myRole;                   // your offer's role, -1 none
    bool myPromise, myLead;       // ... promised starts, it leads now
    bool refusesMe;               // beef with your captain: never signs with you
    int decideWeek;               // mid-season: decides after this week; 0: at the next season's start / no offers yet
    bool rookie;                  // r173: never played in this career (a first form, never evolved, Lv 8, no record)
};
// The free agents, filtered (0 all, 1 captain types, 2 sidekick types, 3 the ones you made an offer to, r173: 4 rookies) and
// sorted (0 value - rookies by their rating, 1 the most interest, 2 by name). Fills up to max, returns how many.
int PortFranchiseFreeAgents(PortFranchiseFreeAgent* out, int max, int filter, int sort);
// What each offer you could make that agent means: rows for CAPTAIN (your captain to the bench: your backup goes), CAPTAIN
// (your captain released), BACKUP, BACKUP + PROMISED STARTS, SIDEKICK 1-3. Fills up to max, returns how many.
struct PortFranchiseFaRole
{
    int role;
    bool promise, releaseCaptain;
    bool allowed;                 // (a sidekick-only agent can't be a captain / backup; refused: beef)
    char label[48];               // "CAPTAIN", "BACKUP + PROMISED STARTS", "SIDEKICK 2"
    char effect[112];             // "GHOST RIDER TO THE BENCH, WOLVERINE RELEASED" / "NOBODY RELEASED"
    char release[48];             // who goes into free agency if it signs ("" nobody)
    char releaseKey[48];
    bool wouldLead;               // this offer would lead now
    char versus[64];              // "LEADS" / "BEHIND MUTE CITY (CAPTAIN)" / "NO OTHER OFFERS"
    bool current;                 // your offer now is this one
    char replaces[64];            // "REPLACES YOUR OFFER TO PIKACHU" ("" none)
};
int PortFranchiseFaRoles(const char* key, PortFranchiseFaRole* out, int max);
// Your offer to that agent (it replaces yours to it, and yours to another agent for the same spot). False: not now.
bool PortFranchiseFaOffer(const char* key, int role, bool promise, bool releaseCaptain);
bool PortFranchiseFaWithdraw(const char* key);

// r167 (Lance: an EVOLUTION SELECTOR): any form a player of yours has reached stays unlocked; you pick which one plays, any
// time but a match (each form its own model and stats; the level, XP and shiny are the player's and stay). Evolving goes on
// from its highest form reached. Your players whose line has more than one form:
struct PortFranchiseForms
{
    int slot;                     // 0 captain, 1 backup, 2-4 your sidekick members
    char role[16];                // "CAPTAIN" ...
    int level;
    bool shiny, costume;          // costume: a Marvel hero (forms = costumes)
    int count;                    // tiles: the forms reached, then the next ones (locked)
    int current;                  // the tile playing now
    char key[6][48], name[6][48];
    bool reached[6];
    int at[6];                    // a locked form's level, 0 reached
    bool ready;                   // its next form is ready to evolve (A on it: the EVO page)
};
int PortFranchiseFormsList(PortFranchiseForms* out, int max);
// Play that slot as `key` (a form it has reached). False: not one of its forms, a match on, not this headset's to do.
bool PortFranchiseSetForm(int slot, const char* key);
// r167: that slot's evolution when it's ready (postponed too): the selector's EVOLVE! tile opens the EVO page with it
bool PortFranchiseReadyEvolutionFor(int slot, PortFranchiseEvolution* out);

#endif // PORT_FRANCHISE_H
