// strikers-quest r70: franchise mode, step 1 - see include/port/franchise.h.
#include "port/franchise.h"
#include "port/edition.h"    // r73: PortPlusFeatures
#include "port/xr_guest.h"
#include "port/xr_net.h"     // r76: franchise co-op
#include "port/lineup.h"     // r78: the co-captain
#include "port/xr_scorer.h"  // r73: the nemesis face-off
#include "port/xr_league.h"  // r100: online leagues

#include "Game/GameInfo.h"
#include "Game/GameSceneManager.h"
#include "Game/Team.h"
#include "Game/AI/Fielder.h"  // r73: a captain as a cCharacter (the face-off)
#include "Game/DB/BasicGameInfo.h"
#include "Game/DB/StatsTracker.h"  // r77: the match's own goals and assists per player (live XP)
#include "Game/FE/feHelpFuncs.h"   // r78: ConvertToCharacterClass (the co-captain's class)
#include "NL/nlTask.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

// ---------------------------------------------------------------------------------------------- clubs
struct Club
{
    std::string id, place, mascot, conf, div;
    unsigned col[3];
    int stadium;
};
std::vector<Club> s_clubs;
std::unordered_map<std::string, int> s_rating;  // captain / sidekick kind key -> overall (ratings.txt)
bool s_dataRead = false;
// r70c: evolutions.txt "<from pack> <to pack> <level>" (tools/outside/franchise_data.py)
struct Evo
{
    std::string from, to;
    int level;
};
std::vector<Evo> s_evos;

std::string home_dir()
{
    const char* h = getenv("STRIKERS_HOME");
    return h != NULL ? std::string(h) : std::string(".");
}

void read_data()
{
    if (s_dataRead)
        return;
    s_dataRead = true;
    // clubs.txt: id <tab> place <tab> mascot <tab> conference <tab> division <tab> c1 c2 c3 <tab> stadium
    FILE* f = fopen((home_dir() + "/franchise/clubs.txt").c_str(), "r");
    if (f != NULL)
    {
        char line[512];
        while (fgets(line, sizeof line, f) != NULL)
        {
            if (line[0] == '#' || line[0] == '\n')
                continue;
            std::vector<std::string> p;
            std::string cur;
            for (const char* c = line; *c != '\0' && *c != '\n' && *c != '\r'; ++c)
            {
                if (*c == '\t')
                {
                    p.push_back(cur);
                    cur.clear();
                }
                else
                    cur += *c;
            }
            p.push_back(cur);
            if (p.size() < 7)
                continue;
            Club c;
            c.id = p[0];
            c.place = p[1];
            c.mascot = p[2];
            c.conf = p[3];
            c.div = p[4];
            c.col[0] = c.col[1] = c.col[2] = 0x808080;
            sscanf(p[5].c_str(), "%x %x %x", &c.col[0], &c.col[1], &c.col[2]);
            c.stadium = atoi(p[6].c_str());
            s_clubs.push_back(c);
        }
        fclose(f);
    }
    // ratings.txt: key overall (tools/outside/franchise_data.py, from the guests' stat cards)
    f = fopen((home_dir() + "/franchise/ratings.txt").c_str(), "r");
    if (f != NULL)
    {
        char key[128];
        int ovr = 0;
        while (fscanf(f, "%127s %d", key, &ovr) == 2)
            s_rating[key] = ovr;
        fclose(f);
    }
    f = fopen((home_dir() + "/franchise/evolutions.txt").c_str(), "r");
    if (f != NULL)
    {
        char from[96], to[96];
        int level = 0;
        while (fscanf(f, "%95s %95s %d", from, to, &level) == 3)
        {
            Evo e;
            e.from = from;
            e.to = to;
            e.level = level;
            s_evos.push_back(e);
        }
        fclose(f);
    }
    fprintf(stderr, "[franchise] %d clubs, %d ratings, %d evolutions\n", (int)s_clubs.size(), (int)s_rating.size(),
            (int)s_evos.size());
    fflush(stderr);
}

// ------------------------------------------------------------------------------------ the player pools
// A captain: a Strikers one (team 0-7, eTeamID) or a guest (roster index). Keyed by a stable string
// in the save: "strikers_<name>" or the guest's pack.
const char* const kTeamKey[8] = {"daisy", "donkeykong", "luigi", "mario", "peach", "waluigi", "wario", "yoshi"};
const char* const kTeamName[8] = {"Daisy", "Donkey Kong", "Luigi", "Mario", "Peach", "Waluigi", "Wario", "Yoshi"};

struct Captain
{
    std::string key, name;
    int team;   // eTeamID if a Strikers captain, else -1
    int guest;  // roster index if a guest, else -1
    int slot;   // the Strikers captain slot it plays in (its own / the guest's base)
    bool evolved;  // r70c: an evolved Pokemon (never dealt; a club's Pokemon evolves into it)
};
// A sidekick kind: the 4 Strikers ones (eSidekickID) and the Charged guest sidekicks.
struct Kind
{
    std::string key, name;
    int sidekick;  // eSidekickID it plays as
    int guest;     // roster index if a guest sidekick, else -1
};
const char* const kSkKey[4] = {"toad", "koopa", "hammerbros", "birdo"};
const char* const kSkName[4] = {"Toads", "Koopas", "Hammer Bros", "Birdos"};

std::vector<Captain> captains()
{
    std::vector<Captain> out;
    for (int t = 0; t < 8; t++)
    {
        Captain c;
        c.key = std::string("strikers_") + kTeamKey[t];
        c.name = kTeamName[t];
        c.team = t;
        c.guest = -1;
        c.slot = t;
        c.evolved = false;
        out.push_back(c);
    }
    const int n = PortGuestRosterCount(PORT_GUEST_CAPTAIN);
    for (int i = 0; i < n; i++)
    {
        const int idx = PortGuestRosterAt(PORT_GUEST_CAPTAIN, i);
        if (idx < 0)
            continue;
        Captain c;
        c.key = PortGuestRosterPack(idx);
        c.name = PortGuestRosterName(idx);
        c.team = -1;
        c.guest = idx;
        c.slot = PortGuestRosterBase(idx);
        c.evolved = PortGuestRosterEvolved(idx);
        out.push_back(c);
    }
    return out;
}

std::vector<Kind> kinds()
{
    std::vector<Kind> out;
    for (int s = 0; s < 4; s++)
    {
        Kind k;
        k.key = kSkKey[s];
        k.name = kSkName[s];
        k.sidekick = s;
        k.guest = -1;
        out.push_back(k);
    }
    const int n = PortGuestRosterCount(PORT_GUEST_SIDEKICK);
    for (int i = 0; i < n; i++)
    {
        const int idx = PortGuestRosterAt(PORT_GUEST_SIDEKICK, i);
        if (idx < 0)
            continue;
        Kind k;
        k.key = PortGuestRosterPack(idx);
        k.name = std::string(PortGuestRosterName(idx)) + "s";
        k.sidekick = PortGuestRosterBase(idx);
        k.guest = idx;
        out.push_back(k);
    }
    return out;
}

int rating(const std::string& key)
{
    std::unordered_map<std::string, int>::const_iterator it = s_rating.find(key);
    return it != s_rating.end() ? it->second : 75;
}

// ------------------------------------------------------------------------------------------- careers
// r70c: a player's growth (Lance: everyone levels, everyone starts at Lv 8; Pokemon evolve by level;
// each evolution rolls for shiny and shiny sticks; anyone can cancel an evolution).
struct Prog
{
    int level, xp;     // xp toward the next level (kXpPerLevel)
    bool shiny;
    int declinedAt;    // the level an evolution was cancelled at (asked again at the next level), 0 none
    int startLevel;    // r77: its level when the season began (the Evo Watch's levels this season)
};
// r71: A-Life (Lance's answers, FRANCHISE-PLAN.md): a player's record, form and personality.
enum
{
    PERS_NEUTRAL = -1,  // your own players start neutral and grow from play (later)
    PERS_HOTHEAD,
    PERS_SHOWBOAT,
    PERS_SILENT,
    PERS_HONORABLE,
    PERS_TRICKSTER,
    PERS_UNDERDOG,
    PERS_VETERAN,
    PERS_FRONTRUNNER,
    PERS_COUNT,
};
struct PStats
{
    int goals, games;   // career
    int seasonGoals;
    int form;           // -3 (a slump) .. +3 (on fire)
    int personality;    // PERS_*
};
struct ClubState
{
    std::string captain, backup;  // captain keys ("" no backup)
    std::string kind;             // sidekick kind key
    std::string names[3];
    bool startsBackup;
    int wins, losses, goalsFor, goalsAgainst;
    Prog cap, bk, side;           // r70c: the captain, the backup, the sidekicks (as a group)
    PStats capSt, bkSt, sk[3];    // r71: the captain, the backup, each named sidekick
    int skAge[3];                 // r71: seasons played (sidekicks retire; rookies take their place)
    int streak;                   // r71: + wins in a row / - losses in a row
};
struct Game
{
    int week, home, away, homeGoals, awayGoals;
    bool played, overtime;
    int series;  // r70b: the playoff series it belongs to, -1 a regular-season game
};
// r70b: a best-of-5 playoff series; hi = the higher seed (home in games 1, 2 and 5).
struct Series
{
    int round;  // 1 Kickoff Round, 2 conference semifinals, 3 conference finals, 4 Super Strike Series
    int conf;   // 0 Crown, 1 Star, -1 the Super Strike Series
    int hi, lo, hiSeed, loSeed, winsHi, winsLo;
};
enum
{
    PHASE_REGULAR = 0,
    PHASE_PLAYOFFS = 1,
    PHASE_DONE = 2,  // the champion crowned: next season from the hub
};
// r72: a nemesis (Lance's interview, FRANCHISE-PLAN.md "Nemesis system"): a player with beef against
// your captain. Never ended by a score: the heat cools when it stops being a real contest (lopsided) or
// they move on; the history stays.
struct Nemesis
{
    std::string key;     // a captain's key, or "sk|<club>|<name>" for a sidekick
    std::string name;
    int club;            // where they play now (-1 retired)
    bool teammate;       // one of yours turning on you
    int heat;            // 0-100; 35+ = a nemesis
    int rank;            // 1-5: grows each time they beat you as your nemesis
    int beatYou, lostToYou;
    int title;           // kTitles
    int traits;          // kTraits bits
    int since;           // season
    bool active;
    int last[5];         // the last meetings' margins (theirs - yours), newest first; 99 = none
    std::vector<std::string> log;  // what happened, newest first (up to 6)
    int owner;           // r100: whose nemesis it is in an online league (that member's club); -1 = the career's own
};

struct Career
{
    int slot;
    unsigned seed, rng;
    // r76, co-op: played together online (saved as franchise/coop.txt, PORT_FR_COOP_SLOT), and how many
    // times saved (the friend's copy is the same when its version is)
    bool coop = false;
    unsigned version = 0;
    int season, week;  // week = next to play: 1-20 the regular season, 21 on the playoffs' game days
    int myClub;
    ClubState clubs[PORT_FR_CLUBS];
    std::vector<Game> games;
    std::string headline;
    std::vector<std::string> results;  // last week's
    // r70b: the playoffs
    int phase, round, champion;
    int seeds[2][7];
    std::vector<Series> series;
    std::vector<std::pair<int, int> > champions;  // (season, club), every season's
    std::vector<std::string> news;                 // r70c: last week's league news (evolutions ...)
    std::vector<std::string> feed;                 // r71: last week's players' posts
    std::vector<std::string> awards;               // r71: the last awards night
    std::vector<std::string> awardKeys;            // r73: each award's portrait (guests/portraits/<key>.png)
    std::vector<std::string> history;              // r71: the hall of fame, newest first
    int recordGoals, recordSeason;                 // r71: most goals in a season (all careers' record here)
    std::string recordName;
    std::vector<Nemesis> nems;                     // r72
    // r97 (Lance: "we need a difficulty setting for franchise mode, bc that shit was becoming too easy and i
    // want a real life nemesis/A-Life system where im not always dominating or being the MVP"): the game's
    // own skill level for its matches (1 Rookie, 2 Professional, 3 Superstar, 4 Legend; they ran on the
    // Friendly options' skill, his Rookie) and how hard the league pushes back (diff_k)
    int difficulty = 2;
    // r100 (Lance: online franchise leagues, "UP TO 8 player online"): the league's code ("" a career of this
    // headset's own), its members' clubs (the server's; each headset's own is myClub), and the games whose
    // A-Life (scorers' records, beef) went in with their result (a played league game: finish_week skips them)
    std::string league;
    std::vector<int> humans;
    std::vector<int> lifeDone;
    int newsWeek = 0;  // r100: the week the news / feed is about (a league's played games add to it)
    int leagueV = 0;   // r100: the league's version this copy is (the server's count)
};

Career s_career;

// r97: 0 Rookie .. 3 Legend (the career's difficulty), for the league's push-back
int diff_k(const Career& c) { return std::max(0, std::min(3, c.difficulty - 1)); }

// r100: a club a person runs (this headset's, or another league member's): no CPU decisions for it (its
// evolutions, trades, starters), and the league's push-back is on it as on yours
bool is_human(const Career& c, int club)
{
    return club == c.myClub || std::find(c.humans.begin(), c.humans.end(), club) != c.humans.end();
}

// r100: whose nemesis `n` is (an online league member's club; the career's own player otherwise)
int nem_owner(const Career& c, const Nemesis& n) { return n.owner >= 0 ? n.owner : c.myClub; }

// r100: online leagues (below, before PortFranchiseFrame)
bool in_league();
bool league_do(const char* name, std::function<bool()> fn);
bool league_advance(Career& c, bool force);
bool s_active = false;
// r71: the scorers of the franchise match just played (side, who: 0 captain, 1-3 sidekicks), handed to
// the week's A-Life (finish_week) with the game they belong to. r78: 4 = the club's other captain on the
// field (the co-captain: every club with a backup captain fields both, include/port/lineup.h).
const int kScorerSlots = 5;
int s_matchGoals[2][kScorerSlots];
int s_lifeGame = -1;
int s_lifeScorers[2][kScorerSlots];
// r72: the match's beef: Super Strikes by side, hits on the other side's captain (by their captain, by
// anyone)
int s_matchSS[2], s_matchCapHits[2], s_matchAnyHits[2];
int s_lifeSS[2], s_lifeCapHits[2], s_lifeAnyHits[2];

// r100: a played match as it ended (its game, the score, everyone's goals, Super Strikes, hits, assists, and
// whether each side fielded a co-captain): an online league's result goes into the shared career with it, again
// on the newest copy if someone else's change landed first.
struct PlayedResult
{
    int game;
    int hg, ag;
    bool ot;
    int goals[2][kScorerSlots];
    int ss[2], capHits[2], anyHits[2];
    int assists[2][4];
    bool co[2];
};

void init_stats(PStats& s, int personality)
{
    s.goals = s.games = s.seasonGoals = s.form = 0;
    s.personality = personality;
}

void init_life(Career& c)
{
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = c.clubs[i];
        init_stats(s.capSt, PERS_NEUTRAL);
        init_stats(s.bkSt, PERS_NEUTRAL);
        for (int k = 0; k < 3; k++)
        {
            init_stats(s.sk[k], PERS_NEUTRAL);
            s.skAge[k] = 0;
        }
        s.streak = 0;
    }
    c.recordGoals = 0;
    c.recordSeason = 0;
    c.recordName.clear();
}

void init_prog(Prog& p)
{
    p.level = 8;
    p.xp = 0;
    p.shiny = false;
    p.declinedAt = 0;
    p.startLevel = 8;
}

void reset_playoffs(Career& c)
{
    c.phase = PHASE_REGULAR;
    c.round = 0;
    c.champion = -1;
    for (int conf = 0; conf < 2; conf++)
        for (int k = 0; k < 7; k++)
            c.seeds[conf][k] = -1;
    c.series.clear();
}

unsigned next_rand(unsigned& r)
{
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return r;
}
double rand01(unsigned& r) { return (next_rand(r) & 0xFFFFFF) / 16777216.0; }

// r71: every CPU player a personality (yours start neutral), every sidekick an age (0-3 seasons in).
void deal_life(Career& c)
{
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = c.clubs[i];
        const bool mine = i == c.myClub;
        PStats* st[5] = {&s.capSt, &s.bkSt, &s.sk[0], &s.sk[1], &s.sk[2]};
        for (int k = 0; k < 5; k++)
            st[k]->personality = mine ? PERS_NEUTRAL : (int)(next_rand(c.rng) % PERS_COUNT);
        for (int k = 0; k < 3; k++)
            s.skAge[k] = (int)(next_rand(c.rng) % 4);
    }
}

std::string career_path(int slot)
{
    if (slot == PORT_FR_COOP_SLOT)
        return home_dir() + "/franchise/coop.txt";  // r76: the co-op career
    if (slot == PORT_FR_LEAGUE_SLOT)
        return home_dir() + "/franchise/league.txt";  // r100: this headset's copy of its online league
    char buf[32];
    snprintf(buf, sizeof buf, "/franchise/career%d.txt", slot + 1);
    return home_dir() + buf;
}

// r76: a file's whole text ("" if none)
std::string read_text(const std::string& path)
{
    std::string out;
    FILE* f = fopen(path.c_str(), "rb");
    if (f == NULL)
        return out;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        out.append(buf, n);
    fclose(f);
    return out;
}

bool write_text(const std::string& path, const char* text, size_t size)
{
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (f == NULL)
        return false;
    const bool ok = fwrite(text, 1, size, f) == size;
    fclose(f);
    if (!ok)
        return false;
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

void coop_shared(const Career& c, const std::string& path);

// r76: in a co-op career only the host's headset acts (with the friend there): this one may not
bool coop_blocked()
{
    return s_active && s_career.coop && (!PortNetCoopFriend() || !PortNetCoopHost());
}

bool write_career(const Career& c, const std::string& path);  // below

void save()
{
    if (!s_active)
        return;
    ++s_career.version;  // r76
    const std::string path = career_path(s_career.slot);
    if (write_career(s_career, path) && s_career.coop)
        coop_shared(s_career, path);  // r76: the co-op career's host sends every change to the friend
}

// r100: a career as its save text, to `path` (save, and an online league's shared copy)
bool write_career(const Career& c, const std::string& path)
{
    const std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if (f == NULL)
    {
        fprintf(stderr, "[franchise] can't write %s\n", tmp.c_str());
        return false;
    }
    fprintf(f, "SSL1\nseed %u\nrng %u\nseason %d\nweek %d\nmyclub %d\n", c.seed, c.rng, c.season, c.week, c.myClub);
    fprintf(f, "version %u\n", c.version);  // r76
    fprintf(f, "difficulty %d\n", c.difficulty);  // r97
    if (c.coop)
        fprintf(f, "coop 1\n");
    fprintf(f, "headline %s\n", c.headline.c_str());
    for (size_t i = 0; i < c.results.size(); i++)
        fprintf(f, "result %s\n", c.results[i].c_str());
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        const ClubState& s = c.clubs[i];
        fprintf(f, "club %d %s %s %s %d %d %d %d %d\n", i, s.captain.c_str(), s.backup.empty() ? "-" : s.backup.c_str(),
                s.kind.c_str(), s.startsBackup ? 1 : 0, s.wins, s.losses, s.goalsFor, s.goalsAgainst);
        fprintf(f, "names %d %s|%s|%s\n", i, s.names[0].c_str(), s.names[1].c_str(), s.names[2].c_str());
        fprintf(f, "prog %d %d %d %d %d %d %d %d %d %d %d %d %d\n", i, s.cap.level, s.cap.xp, s.cap.shiny ? 1 : 0,
                s.cap.declinedAt, s.bk.level, s.bk.xp, s.bk.shiny ? 1 : 0, s.bk.declinedAt, s.side.level, s.side.xp,
                s.side.shiny ? 1 : 0, s.side.declinedAt);
        fprintf(f, "pstart %d %d %d %d\n", i, s.cap.startLevel, s.bk.startLevel, s.side.startLevel);  // r77
    }
    for (size_t i = 0; i < c.news.size(); i++)
        fprintf(f, "news %s\n", c.news[i].c_str());
    // r71: A-Life
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        const ClubState& s = c.clubs[i];
        const PStats* st[5] = {&s.capSt, &s.bkSt, &s.sk[0], &s.sk[1], &s.sk[2]};
        fprintf(f, "life %d %d %d %d %d", i, s.streak, s.skAge[0], s.skAge[1], s.skAge[2]);
        for (int k = 0; k < 5; k++)
            fprintf(f, " %d %d %d %d %d", st[k]->goals, st[k]->games, st[k]->seasonGoals, st[k]->form, st[k]->personality);
        fprintf(f, "\n");
    }
    for (size_t i = 0; i < c.feed.size(); i++)
        fprintf(f, "feed %s\n", c.feed[i].c_str());
    for (size_t i = 0; i < c.awards.size(); i++)
        fprintf(f, "award %s\n", c.awards[i].c_str());
    for (size_t i = 0; i < c.awardKeys.size(); i++)
        fprintf(f, "awardkey %s\n", c.awardKeys[i].c_str());
    for (size_t i = 0; i < c.history.size(); i++)
        fprintf(f, "fame %s\n", c.history[i].c_str());
    fprintf(f, "record %d %d %s\n", c.recordGoals, c.recordSeason, c.recordName.c_str());
    // r72: nemeses ("nem" numbers, then key <tab> name; their history as "nemlog <index> <text>")
    for (size_t i = 0; i < c.nems.size(); i++)
    {
        const Nemesis& n = c.nems[i];
        fprintf(f, "nem %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s\t%s\n", n.club, n.teammate ? 1 : 0, n.heat, n.rank,
                n.beatYou, n.lostToYou, n.title, n.traits, n.since, n.active ? 1 : 0, n.last[0], n.last[1], n.last[2],
                n.last[3], n.last[4], n.key.c_str(), n.name.c_str());
        for (size_t k = 0; k < n.log.size(); k++)
            fprintf(f, "nemlog %d %s\n", (int)i, n.log[k].c_str());
        if (n.owner >= 0)
            fprintf(f, "nemowner %d %d\n", (int)i, n.owner);  // r100
    }
    // r100: an online league's code, its members' clubs, the games whose A-Life went in with their result
    if (!c.league.empty())
        fprintf(f, "league %s\n", c.league.c_str());
    if (!c.humans.empty())
    {
        fprintf(f, "humans");
        for (size_t i = 0; i < c.humans.size(); i++)
            fprintf(f, " %d", c.humans[i]);
        fprintf(f, "\n");
    }
    for (size_t i = 0; i < c.lifeDone.size(); i++)
        fprintf(f, "lifedone %d\n", c.lifeDone[i]);
    if (!c.league.empty())
        fprintf(f, "newsweek %d\nleaguev %d\n", c.newsWeek, c.leagueV);
    for (size_t i = 0; i < c.games.size(); i++)
    {
        const Game& g = c.games[i];
        fprintf(f, "game %d %d %d %d %d %d %d %d\n", g.week, g.home, g.away, g.homeGoals, g.awayGoals, g.played ? 1 : 0,
                g.overtime ? 1 : 0, g.series);
    }
    fprintf(f, "phase %d %d %d\n", c.phase, c.round, c.champion);
    for (int conf = 0; conf < 2; conf++)
        fprintf(f, "seeds %d %d %d %d %d %d %d %d\n", conf, c.seeds[conf][0], c.seeds[conf][1], c.seeds[conf][2],
                c.seeds[conf][3], c.seeds[conf][4], c.seeds[conf][5], c.seeds[conf][6]);
    for (size_t i = 0; i < c.series.size(); i++)
    {
        const Series& s = c.series[i];
        fprintf(f, "series %d %d %d %d %d %d %d %d\n", s.round, s.conf, s.hi, s.lo, s.hiSeed, s.loSeed, s.winsHi, s.winsLo);
    }
    for (size_t i = 0; i < c.champions.size(); i++)
        fprintf(f, "champion %d %d\n", c.champions[i].first, c.champions[i].second);
    fclose(f);
    remove(path.c_str());
    return rename(tmp.c_str(), path.c_str()) == 0;
}

// r76: the co-op career file as last read (its card, its id and version for the friend), re-read when stale
bool s_coopFileStale = true, s_coopFileHas = false;
PortFranchiseSlot s_coopFile;

// r76: a co-op career saved here: the friend hears its id and version (xr_net, each second); the host
// sends the friend the whole file after every change (both keep the same career).
void coop_shared(const Career& c, const std::string& path)
{
    s_coopFileStale = true;
    PortNetCoopSetMine(1, c.seed, c.version, s_active && s_career.coop ? 1 : 0);
    if (!PortNetCoopFriend() || !PortNetCoopHost())
        return;
    const std::string text = read_text(path);
    if (!text.empty())
        PortNetCoopSendCareer(text.data(), (int)text.size(), c.seed, c.version);
}

bool load_path(const std::string& path, int slot, Career* out);  // below

bool load_slot(int slot, Career* out) { return load_path(career_path(slot), slot, out); }

// r100: a career from its save text at `path`, as the slot `slot`'s (an online league's shared copy comes in
// through a file too)
bool load_path(const std::string& path, int slot, Career* out)
{
    FILE* f = fopen(path.c_str(), "r");
    if (f == NULL)
        return false;
    Career c;
    c.slot = slot;
    c.seed = c.rng = 1;
    c.season = 1;
    c.week = 1;
    c.myClub = 0;
    reset_playoffs(c);
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        c.clubs[i].startsBackup = false;
        c.clubs[i].wins = c.clubs[i].losses = c.clubs[i].goalsFor = c.clubs[i].goalsAgainst = 0;
        init_prog(c.clubs[i].cap);  // (r70 / r70b saves have none: everyone at Lv 8)
        init_prog(c.clubs[i].bk);
        init_prog(c.clubs[i].side);
    }
    init_life(c);
    bool hadLife = false;
    char line[512];
    bool ok = false;
    while (fgets(line, sizeof line, f) != NULL)
    {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "SSL1") == 0)
            ok = true;
        else if (strncmp(line, "seed ", 5) == 0)
            c.seed = (unsigned)strtoul(line + 5, NULL, 10);
        else if (strncmp(line, "rng ", 4) == 0)
            c.rng = (unsigned)strtoul(line + 4, NULL, 10);
        else if (strncmp(line, "version ", 8) == 0)  // r76
            c.version = (unsigned)strtoul(line + 8, NULL, 10);
        else if (strcmp(line, "coop 1") == 0)
            c.coop = true;
        else if (strncmp(line, "difficulty ", 11) == 0)  // r97 (older careers: Professional)
            c.difficulty = std::max(1, std::min(4, atoi(line + 11)));
        else if (strncmp(line, "season ", 7) == 0)
            c.season = atoi(line + 7);
        else if (strncmp(line, "week ", 5) == 0)
            c.week = atoi(line + 5);
        else if (strncmp(line, "myclub ", 7) == 0)
            c.myClub = atoi(line + 7);
        else if (strncmp(line, "headline ", 9) == 0)
            c.headline = line + 9;
        else if (strncmp(line, "result ", 7) == 0)
            c.results.push_back(line + 7);
        else if (strncmp(line, "club ", 5) == 0)
        {
            int i = 0, sb = 0, w = 0, l = 0, gf = 0, ga = 0;
            char cap[96], bk[96], kd[96];
            if (sscanf(line + 5, "%d %95s %95s %95s %d %d %d %d %d", &i, cap, bk, kd, &sb, &w, &l, &gf, &ga) == 9 && i >= 0 &&
                i < PORT_FR_CLUBS)
            {
                ClubState& s = c.clubs[i];
                s.captain = cap;
                s.backup = strcmp(bk, "-") == 0 ? "" : bk;
                s.kind = kd;
                s.startsBackup = sb != 0;
                s.wins = w;
                s.losses = l;
                s.goalsFor = gf;
                s.goalsAgainst = ga;
            }
        }
        else if (strncmp(line, "names ", 6) == 0)
        {
            int i = atoi(line + 6);
            const char* p = strchr(line + 6, ' ');
            if (p != NULL && i >= 0 && i < PORT_FR_CLUBS)
            {
                std::string all = p + 1;
                for (int k = 0; k < 3; k++)
                {
                    const size_t bar = all.find('|');
                    c.clubs[i].names[k] = all.substr(0, bar);
                    all = bar == std::string::npos ? "" : all.substr(bar + 1);
                }
            }
        }
        else if (strncmp(line, "game ", 5) == 0)
        {
            Game g;
            int pl = 0, ot = 0;
            g.series = -1;
            if (sscanf(line + 5, "%d %d %d %d %d %d %d %d", &g.week, &g.home, &g.away, &g.homeGoals, &g.awayGoals, &pl, &ot,
                       &g.series) >= 7)
            {
                g.played = pl != 0;
                g.overtime = ot != 0;
                c.games.push_back(g);
            }
        }
        else if (strncmp(line, "prog ", 5) == 0)
        {
            int i = -1, v[12];
            if (sscanf(line + 5, "%d %d %d %d %d %d %d %d %d %d %d %d %d", &i, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                       &v[6], &v[7], &v[8], &v[9], &v[10], &v[11]) == 13 &&
                i >= 0 && i < PORT_FR_CLUBS)
            {
                Prog* p[3] = {&c.clubs[i].cap, &c.clubs[i].bk, &c.clubs[i].side};
                for (int k = 0; k < 3; k++)
                {
                    p[k]->level = v[k * 4];
                    p[k]->xp = v[k * 4 + 1];
                    p[k]->shiny = v[k * 4 + 2] != 0;
                    p[k]->declinedAt = v[k * 4 + 3];
                }
            }
        }
        else if (strncmp(line, "pstart ", 7) == 0)  // r77
        {
            int i = -1, a = 8, b = 8, k = 8;
            if (sscanf(line + 7, "%d %d %d %d", &i, &a, &b, &k) == 4 && i >= 0 && i < PORT_FR_CLUBS)
            {
                c.clubs[i].cap.startLevel = a;
                c.clubs[i].bk.startLevel = b;
                c.clubs[i].side.startLevel = k;
            }
        }
        else if (strncmp(line, "news ", 5) == 0)
            c.news.push_back(line + 5);
        else if (strncmp(line, "life ", 5) == 0)
        {
            int v[30];
            int n = 0;
            const char* p = line + 5;
            char* end = NULL;
            while (n < 30)
            {
                const long x = strtol(p, &end, 10);
                if (end == p)
                    break;
                v[n++] = (int)x;
                p = end;
            }
            if (n == 30 && v[0] >= 0 && v[0] < PORT_FR_CLUBS)
            {
                ClubState& s = c.clubs[v[0]];
                s.streak = v[1];
                for (int k = 0; k < 3; k++)
                    s.skAge[k] = v[2 + k];
                PStats* st[5] = {&s.capSt, &s.bkSt, &s.sk[0], &s.sk[1], &s.sk[2]};
                for (int k = 0; k < 5; k++)
                {
                    st[k]->goals = v[5 + k * 5];
                    st[k]->games = v[6 + k * 5];
                    st[k]->seasonGoals = v[7 + k * 5];
                    st[k]->form = v[8 + k * 5];
                    st[k]->personality = v[9 + k * 5];
                }
                hadLife = true;
            }
        }
        else if (strncmp(line, "nem ", 4) == 0)
        {
            Nemesis n;
            int v[15], used = 0;
            if (sscanf(line + 4, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %n", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                       &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &used) == 15 &&
                used > 0)
            {
                n.club = v[0];
                n.owner = -1;  // r100 (a "nemowner" line after it says otherwise)
                n.teammate = v[1] != 0;
                n.heat = v[2];
                n.rank = v[3];
                n.beatYou = v[4];
                n.lostToYou = v[5];
                n.title = v[6] >= 0 && v[6] < 11 ? v[6] : 0;  // (kTitles' 11, below)
                n.traits = v[7];
                n.since = v[8];
                n.active = v[9] != 0;
                for (int k = 0; k < 5; k++)
                    n.last[k] = v[10 + k];
                const std::string rest = line + 4 + used;
                const size_t tab = rest.find('\t');
                n.key = rest.substr(0, tab);
                n.name = tab == std::string::npos ? n.key : rest.substr(tab + 1);
                c.nems.push_back(n);
            }
        }
        else if (strncmp(line, "nemlog ", 7) == 0)
        {
            int i = -1, used = 0;
            if (sscanf(line + 7, "%d %n", &i, &used) >= 1 && i >= 0 && i < (int)c.nems.size())
                c.nems[i].log.push_back(line + 7 + used);
        }
        else if (strncmp(line, "nemowner ", 9) == 0)  // r100
        {
            int i = -1, club = -1;
            if (sscanf(line + 9, "%d %d", &i, &club) == 2 && i >= 0 && i < (int)c.nems.size() && club >= 0 &&
                club < PORT_FR_CLUBS)
                c.nems[i].owner = club;
        }
        else if (strncmp(line, "league ", 7) == 0)  // r100
            c.league = line + 7;
        else if (strncmp(line, "humans", 6) == 0)
        {
            c.humans.clear();
            const char* p = line + 6;
            int v = 0, used = 0;
            while (sscanf(p, "%d%n", &v, &used) == 1)
            {
                if (v >= 0 && v < PORT_FR_CLUBS)
                    c.humans.push_back(v);
                p += used;
            }
        }
        else if (strncmp(line, "lifedone ", 9) == 0)
            c.lifeDone.push_back(atoi(line + 9));
        else if (strncmp(line, "newsweek ", 9) == 0)
            c.newsWeek = atoi(line + 9);
        else if (strncmp(line, "leaguev ", 8) == 0)
            c.leagueV = atoi(line + 8);
        else if (strncmp(line, "feed ", 5) == 0)
            c.feed.push_back(line + 5);
        else if (strncmp(line, "award ", 6) == 0)
            c.awards.push_back(line + 6);
        else if (strncmp(line, "awardkey ", 9) == 0)
            c.awardKeys.push_back(line + 9);
        else if (strncmp(line, "fame ", 5) == 0)
            c.history.push_back(line + 5);
        else if (strncmp(line, "record ", 7) == 0)
        {
            int g = 0, s = 0, used = 0;
            if (sscanf(line + 7, "%d %d %n", &g, &s, &used) >= 2)
            {
                c.recordGoals = g;
                c.recordSeason = s;
                c.recordName = line + 7 + used;
            }
        }
        else if (strncmp(line, "phase ", 6) == 0)
            sscanf(line + 6, "%d %d %d", &c.phase, &c.round, &c.champion);
        else if (strncmp(line, "seeds ", 6) == 0)
        {
            int conf = -1, s[7];
            if (sscanf(line + 6, "%d %d %d %d %d %d %d %d", &conf, &s[0], &s[1], &s[2], &s[3], &s[4], &s[5], &s[6]) == 8 &&
                conf >= 0 && conf < 2)
                for (int k = 0; k < 7; k++)
                    c.seeds[conf][k] = s[k];
        }
        else if (strncmp(line, "series ", 7) == 0)
        {
            Series s;
            if (sscanf(line + 7, "%d %d %d %d %d %d %d %d", &s.round, &s.conf, &s.hi, &s.lo, &s.hiSeed, &s.loSeed, &s.winsHi,
                       &s.winsLo) == 8)
                c.series.push_back(s);
        }
        else if (strncmp(line, "champion ", 9) == 0)
        {
            int season = 0, club = -1;
            if (sscanf(line + 9, "%d %d", &season, &club) == 2)
                c.champions.push_back(std::make_pair(season, club));
        }
    }
    fclose(f);
    if (!ok || c.games.empty())
        return false;
    if (!hadLife)
        deal_life(c);  // r71: a career from before A-Life: personalities and ages dealt now
    if (slot == PORT_FR_COOP_SLOT)
        c.coop = true;  // r76
    *out = c;
    return true;
}

// ------------------------------------------------------------------------------------------ schedule
// 20 weeks, every club once a week: weeks of division games (a double round robin of the 4), then
// blocks where two divisions meet (4 weeks: everyone against each of the other division's 4): two in
// the conference, one against the other conference, and half of the conference's third pairing.
void add_round(std::vector<Game>& games, int week, int a, int b)
{
    Game g;
    g.week = week;
    g.home = a;
    g.away = b;
    g.homeGoals = g.awayGoals = 0;
    g.played = g.overtime = false;
    g.series = -1;
    games.push_back(g);
}

void division_weeks(std::vector<Game>& games, const int* order)
{
    // order[0..5] = which of the 6 weeks each round goes to
    static const int kPairs[3][4] = {{0, 1, 2, 3}, {0, 2, 1, 3}, {0, 3, 1, 2}};
    for (int conf = 0; conf < 2; conf++)
        for (int div = 0; div < 4; div++)
        {
            const int base = conf * 16 + div * 4;
            for (int r = 0; r < 6; r++)
            {
                const int* p = kPairs[r % 3];
                const bool flip = r >= 3;
                add_round(games, order[r], base + (flip ? p[1] : p[0]), base + (flip ? p[0] : p[1]));
                add_round(games, order[r], base + (flip ? p[3] : p[2]), base + (flip ? p[2] : p[3]));
            }
        }
}

void cross_weeks(std::vector<Game>& games, int divA, int divB, const int* weeks, int rounds)
{
    // divA / divB: global division index (conf * 4 + div); everyone in A meets each of B's 4
    for (int r = 0; r < rounds; r++)
        for (int i = 0; i < 4; i++)
        {
            const int a = divA * 4 + i, b = divB * 4 + (i + r) % 4;
            if ((i + r) % 2 == 0)
                add_round(games, weeks[r], a, b);
            else
                add_round(games, weeks[r], b, a);
        }
}

void make_schedule(Career& c)
{
    c.games.clear();
    c.lifeDone.clear();  // r100 (game indices of the last schedule)
    int weeks[PORT_FR_WEEKS];
    for (int i = 0; i < PORT_FR_WEEKS; i++)
        weeks[i] = i + 1;
    // the order of the weeks shuffled (the division weeks spread through the season)
    for (int i = PORT_FR_WEEKS - 1; i > 0; i--)
    {
        const int j = (int)(next_rand(c.rng) % (unsigned)(i + 1));
        std::swap(weeks[i], weeks[j]);
    }
    division_weeks(c.games, weeks);  // weeks[0..5]
    const int s = c.season;
    static const int kPairing[3][4] = {{1, 0, 3, 2}, {2, 3, 0, 1}, {3, 2, 1, 0}};  // partner of each division
    for (int conf = 0; conf < 2; conf++)
    {
        const int* pa = kPairing[s % 3];
        const int* pb = kPairing[(s + 1) % 3];
        const int* pc = kPairing[(s + 2) % 3];
        for (int d = 0; d < 4; d++)
        {
            if (pa[d] > d)
                cross_weeks(c.games, conf * 4 + d, conf * 4 + pa[d], weeks + 6, 4);
            if (pb[d] > d)
                cross_weeks(c.games, conf * 4 + d, conf * 4 + pb[d], weeks + 10, 4);
            if (pc[d] > d)
                cross_weeks(c.games, conf * 4 + d, conf * 4 + pc[d], weeks + 18, 2);
        }
    }
    for (int d = 0; d < 4; d++)  // the other conference: Crown d against Star (d + season) % 4
        cross_weeks(c.games, d, 4 + (d + s) % 4, weeks + 14, 4);
}

// ---------------------------------------------------------------------------------------- dealing
std::string make_name(Career& c, const std::string& kind, int club, int k, std::vector<std::string>& used)
{
    // reuse the guests' name pools (xr_guest.cpp) through a stable key per career and club
    for (int tries = 0; tries < 40; tries++)
    {
        char key[96];
        snprintf(key, sizeof key, "ssl%u|%d|%d|%d", c.seed, club, k, tries);
        const char* n = PortGuestSidekickNameFor(kind.c_str(), key);
        if (n == NULL)
            break;
        std::string s = n;
        if (std::find(used.begin(), used.end(), s.substr(0, s.find(' '))) == used.end())
        {
            used.push_back(s.substr(0, s.find(' ')));
            return s;
        }
    }
    return "Rookie";
}

// r75: your own club's players when you picked them (keys; "" = dealt at random as before)
struct MyPicks
{
    std::string captain, backup, kind;
    bool noBackup = false;
};

void deal(Career& c, const MyPicks& mine = MyPicks())
{
    std::vector<Captain> caps = captains();
    std::vector<Kind> ks = kinds();
    const bool picked = !mine.captain.empty();
    std::vector<int> order;
    for (size_t i = 0; i < caps.size(); i++)
        if (!caps[i].evolved && (!picked || (caps[i].key != mine.captain && caps[i].key != mine.backup)))
            order.push_back((int)i);  // r70c: careers start with the unevolved Pokemon only; r75: not yours
    for (int i = (int)order.size() - 1; i > 0; i--)
    {
        const int j = (int)(next_rand(c.rng) % (unsigned)(i + 1));
        std::swap(order[i], order[j]);
    }
    size_t next = 0;  // the next dealt captain in `order`
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = c.clubs[i];
        const bool me = picked && i == c.myClub;
        if (me)
            s.captain = mine.captain;
        else if (next < order.size())
            s.captain = caps[order[next++]].key;
        else  // 1.14: fewer captains than clubs (no guest games converted): round the dealt ones again
            s.captain = order.empty() ? std::string("strikers_mario") : caps[order[i % order.size()]].key;
        s.backup.clear();
        s.startsBackup = false;
        s.wins = s.losses = s.goalsFor = s.goalsAgainst = 0;
        s.kind = ks.empty() ? std::string("toad") : ks[next_rand(c.rng) % ks.size()].key;
        if (me && !mine.kind.empty())
            s.kind = mine.kind;
        init_prog(s.cap);
        init_prog(s.bk);
        init_prog(s.side);
        std::vector<std::string> used;
        for (int k = 0; k < 3; k++)
            s.names[k] = make_name(c, s.kind, i, k, used);
    }
    if (picked && !mine.noBackup && !mine.backup.empty())
        c.clubs[c.myClub].backup = mine.backup;
    // the rest: backup captains, to random clubs (no club gets two; r75: not yours when you picked)
    std::vector<int> clubs;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
        if (!picked || i != c.myClub)
            clubs.push_back(i);
    for (int i = (int)clubs.size() - 1; i > 0; i--)
    {
        const int j = (int)(next_rand(c.rng) % (unsigned)(i + 1));
        std::swap(clubs[i], clubs[j]);
    }
    for (size_t k = next; k < order.size() && k - next < clubs.size(); k++)
        c.clubs[clubs[k - next]].backup = caps[order[k]].key;
}

// ------------------------------------------------------------------------------------- simulating
const Captain* find_captain(const std::vector<Captain>& caps, const std::string& key)
{
    for (size_t i = 0; i < caps.size(); i++)
        if (caps[i].key == key)
            return &caps[i];
    return NULL;
}

const std::string& starter(const ClubState& s)
{
    return (s.startsBackup && !s.backup.empty()) ? s.backup : s.captain;
}

const Prog& starter_prog(const ClubState& s)
{
    return (s.startsBackup && !s.backup.empty()) ? s.bk : s.cap;
}

// r70c: a quarter point of strength a level over 8 (an evolution brings its own, better card)
double strength(const ClubState& s)
{
    if (s.backup.empty())
        return 0.62 * (rating(starter(s)) + 0.25 * (starter_prog(s).level - 8)) +
               0.38 * (rating(s.kind) + 0.25 * (s.side.level - 8));
    // r78: both captains on the field (the co-captain in a sidekick's spot)
    const bool bs = s.startsBackup;
    const std::string& other = bs ? s.captain : s.backup;
    const Prog& otherProg = bs ? s.cap : s.bk;
    return 0.38 * (rating(starter(s)) + 0.25 * (starter_prog(s).level - 8)) +
           0.32 * (rating(other) + 0.25 * (otherProg.level - 8)) + 0.30 * (rating(s.kind) + 0.25 * (s.side.level - 8));
}

// r70c: experience from a game: a win 70, a loss 40, 8 a goal; playoff games half as much again. The
// starting captain and the sidekicks get it all, the backup on the bench half. 100 a level, up to 100.
const int kXpPerLevel = 100;
void gain(Prog& p, int xp)
{
    p.xp += xp;
    while (p.xp >= kXpPerLevel && p.level < 100)
    {
        p.xp -= kXpPerLevel;
        p.level++;
    }
    if (p.level >= 100)
        p.xp = 0;
}

bool nemesis_match(const Career& c, const Game& g);  // r72 (below)

void award_xp(Career& c, const Game& g)
{
    // r72 (Lance): beef XP: a game against your nemesis speeds both sides' evolving (+25%)
    const bool beef = nemesis_match(c, g);
    for (int side = 0; side < 2; side++)
    {
        ClubState& s = c.clubs[side == 0 ? g.home : g.away];
        const int goals = side == 0 ? g.homeGoals : g.awayGoals;
        const bool won = (side == 0) == (g.homeGoals > g.awayGoals);
        int xp = (won ? 70 : 40) + 8 * goals;
        if (g.series >= 0)
            xp += xp / 2;
        if (beef)
            xp += xp / 4;
        // r97: the CPU clubs grow faster on the harder difficulties (+15% a step): stronger, sooner evolved
        if (!is_human(c, side == 0 ? g.home : g.away))  // r100: every person's club
            xp += xp * 15 * diff_k(c) / 100;
        // r78: both captains play (the co-captain), so both get it all (it was half for the one benched)
        gain(s.cap, xp);
        if (!s.backup.empty())
            gain(s.bk, xp);
        gain(s.side, xp);
    }
}

int poisson(unsigned& r, double lambda)
{
    const double l = exp(-lambda);
    int k = 0;
    double p = 1.0;
    do
    {
        k++;
        p *= rand01(r);
    } while (p > l && k < 20);
    return k - 1;
}

void apply_result(Career& c, Game& g)
{
    award_xp(c, g);  // r70c
    if (g.series >= 0 && g.series < (int)c.series.size())
    {
        // r70b: a playoff game counts for its series, not the regular-season table
        Series& s = c.series[g.series];
        const int winner = g.homeGoals > g.awayGoals ? g.home : g.away;
        if (winner == s.hi)
            s.winsHi++;
        else
            s.winsLo++;
        g.played = true;
        return;
    }
    ClubState& h = c.clubs[g.home];
    ClubState& a = c.clubs[g.away];
    h.goalsFor += g.homeGoals;
    h.goalsAgainst += g.awayGoals;
    a.goalsFor += g.awayGoals;
    a.goalsAgainst += g.homeGoals;
    if (g.homeGoals > g.awayGoals)
    {
        h.wins++;
        a.losses++;
    }
    else
    {
        a.wins++;
        h.losses++;
    }
    g.played = true;
}

double nemesis_edge(const Career& c, const Game& g, int side);  // r72 (below)

void sim_game(Career& c, Game& g)
{
    double sh = strength(c.clubs[g.home]) + nemesis_edge(c, g, 0), sa = strength(c.clubs[g.away]) + nemesis_edge(c, g, 1);
    // r97: the league pushes back on the harder difficulties: your club worked out a little weaker, and the
    // CPU clubs' own games score more (their stars' Golden Boot races keep pace with what you score)
    const int k = diff_k(c);
    if (is_human(c, g.home))  // r100: every person's club
        sh -= 2.5 * k;
    if (is_human(c, g.away))
        sa -= 2.5 * k;
    const double base = 2.1 + (!is_human(c, g.home) && !is_human(c, g.away) ? 0.2 * k : 0.0);
    const double d = (sh - sa) / 10.0;
    g.homeGoals = poisson(c.rng, base * exp(0.3 * d) * 1.06);
    g.awayGoals = poisson(c.rng, base * exp(-0.3 * d) * 0.96);
    g.overtime = false;
    if (g.homeGoals == g.awayGoals)  // golden goal
    {
        g.overtime = true;
        if (rand01(c.rng) < sh / (sh + sa))
            g.homeGoals++;
        else
            g.awayGoals++;
    }
    apply_result(c, g);
}

std::string upper(const std::string& s)
{
    std::string o = s;
    for (size_t i = 0; i < o.size(); i++)
        if (o[i] >= 'a' && o[i] <= 'z')
            o[i] = (char)(o[i] - 'a' + 'A');
    return o;
}

std::string result_line(const Game& g)
{
    char buf[160];
    snprintf(buf, sizeof buf, "%s %d - %d %s%s", upper(s_clubs[g.home].place).c_str(), g.homeGoals, g.awayGoals,
             upper(s_clubs[g.away].place).c_str(), g.overtime ? " (OT)" : "");
    return buf;
}

// ------------------------------------------------------------------------------- r70b: the playoffs
// The table's order: wins, then goal difference, then goals scored.
bool ranks_above(const Career& c, int a, int b)
{
    const ClubState& x = c.clubs[a];
    const ClubState& y = c.clubs[b];
    if (x.wins != y.wins)
        return x.wins > y.wins;
    const int dx = x.goalsFor - x.goalsAgainst, dy = y.goalsFor - y.goalsAgainst;
    if (dx != dy)
        return dx > dy;
    if (x.goalsFor != y.goalsFor)
        return x.goalsFor > y.goalsFor;
    return a < b;
}

struct RankBy
{
    const Career* c;
    bool operator()(int a, int b) const { return ranks_above(*c, a, b); }
};

// A conference's 7 seeds from the table as it stands: the 4 division winners (1-4), then the 3 best of
// the rest (the wild cards, 5-7).
void seed_conference(const Career& c, int conf, int out[7])
{
    RankBy by = {&c};
    std::vector<int> winners, rest;
    for (int div = 0; div < 4; div++)
    {
        std::vector<int> d;
        for (int k = 0; k < 4; k++)
            d.push_back(conf * 16 + div * 4 + k);
        std::sort(d.begin(), d.end(), by);
        winners.push_back(d[0]);
        for (int k = 1; k < 4; k++)
            rest.push_back(d[k]);
    }
    std::sort(winners.begin(), winners.end(), by);
    std::sort(rest.begin(), rest.end(), by);
    for (int k = 0; k < 4; k++)
        out[k] = winners[k];
    for (int k = 0; k < 3; k++)
        out[4 + k] = rest[k];
}

int series_winner(const Series& s)
{
    return s.winsHi >= 3 ? s.hi : (s.winsLo >= 3 ? s.lo : -1);
}

int seed_of(const Career& c, int club)
{
    for (int conf = 0; conf < 2; conf++)
        for (int k = 0; k < 7; k++)
            if (c.seeds[conf][k] == club)
                return k + 1;
    return 0;
}

void add_series(Career& c, int round, int conf, int a, int b)
{
    Series s;
    s.round = round;
    s.conf = conf;
    const int sa = seed_of(c, a), sb = seed_of(c, b);
    // the higher seed is home; in the Super Strike Series, the better regular season
    const bool aHigh = conf >= 0 ? sa < sb || (sa == sb && ranks_above(c, a, b)) : ranks_above(c, a, b);
    s.hi = aHigh ? a : b;
    s.lo = aHigh ? b : a;
    s.hiSeed = aHigh ? sa : sb;
    s.loSeed = aHigh ? sb : sa;
    s.winsHi = s.winsLo = 0;
    c.series.push_back(s);
}

// Today's game in every series of the round still going (home: the higher seed in games 1, 2 and 5).
void schedule_day(Career& c)
{
    for (size_t i = 0; i < c.series.size(); i++)
    {
        const Series& s = c.series[i];
        if (s.round != c.round || series_winner(s) >= 0)
            continue;
        const int n = s.winsHi + s.winsLo + 1;
        const bool loHome = n == 3 || n == 4;
        add_round(c.games, c.week, loHome ? s.lo : s.hi, loHome ? s.hi : s.lo);
        c.games.back().series = (int)i;
    }
}

std::string round_name(int round, int conf)
{
    const std::string cn = conf == 0 ? "Crown" : "Star";
    switch (round)
    {
    case 1:
        return "Kickoff Round";
    case 2:
        return cn + " Semifinals";
    case 3:
        return cn + " Conference Finals";
    case 4:
        return "Super Strike Series";
    }
    return "";
}

void start_playoffs(Career& c)
{
    c.phase = PHASE_PLAYOFFS;
    c.round = 1;
    c.series.clear();
    for (int conf = 0; conf < 2; conf++)
    {
        seed_conference(c, conf, c.seeds[conf]);
        // #1 rests (the bye); 2 v 7, 3 v 6, 4 v 5
        add_series(c, 1, conf, c.seeds[conf][1], c.seeds[conf][6]);
        add_series(c, 1, conf, c.seeds[conf][2], c.seeds[conf][5]);
        add_series(c, 1, conf, c.seeds[conf][3], c.seeds[conf][4]);
    }
}

// The round over: the next one drawn up (true), or the champion crowned.
void next_round(Career& c)
{
    std::vector<int> left[2];
    int finalists[2] = {-1, -1};
    for (size_t i = 0; i < c.series.size(); i++)
    {
        const Series& s = c.series[i];
        if (s.round != c.round)
            continue;
        const int w = series_winner(s);
        if (s.conf >= 0)
            left[s.conf].push_back(w);
        else
            c.champion = w;
    }
    if (c.round == 4)
    {
        c.phase = PHASE_DONE;
        c.champions.push_back(std::make_pair(c.season, c.champion));
        return;
    }
    const int round = c.round + 1;
    for (int conf = 0; conf < 2; conf++)
    {
        std::vector<int>& l = left[conf];
        if (c.round == 1)
            l.push_back(c.seeds[conf][0]);  // the #1 seed back from its bye
        // by seed: the best left meets the lowest left
        for (size_t a = 0; a < l.size(); a++)
            for (size_t b = a + 1; b < l.size(); b++)
                if (seed_of(c, l[b]) < seed_of(c, l[a]))
                    std::swap(l[a], l[b]);
        if (round == 2 && l.size() == 4)
        {
            add_series(c, round, conf, l[0], l[3]);
            add_series(c, round, conf, l[1], l[2]);
        }
        else if (round == 3 && l.size() == 2)
            add_series(c, round, conf, l[0], l[1]);
        else if (l.size() == 1)
            finalists[conf] = l[0];
    }
    if (round == 4 && finalists[0] >= 0 && finalists[1] >= 0)
        add_series(c, round, -1, finalists[0], finalists[1]);
    c.round = round;
}

bool round_over(const Career& c)
{
    for (size_t i = 0; i < c.series.size(); i++)
        if (c.series[i].round == c.round && series_winner(c.series[i]) < 0)
            return false;
    return true;
}

std::string club_name(int club) { return s_clubs[club].place + " " + s_clubs[club].mascot; }

// ------------------------------------------------------------------------------------ r71: A-Life
std::string name_of(const std::string& key);

// A simulated game's goals given to its scorers: the captain about 4 in 10, the sidekicks the rest. r78:
// a club with a backup captain fields both (the co-captain, 4): about 3 in 10 the starter, 2.5 the
// co-captain, the rest its two sidekicks on the field (2, 3).
void distribute_goals(Career& c, const Game& g, int out[2][kScorerSlots])
{
    memset(out, 0, sizeof(int) * 2 * kScorerSlots);
    for (int side = 0; side < 2; side++)
    {
        const int goals = side == 0 ? g.homeGoals : g.awayGoals;
        const int club = side == 0 ? g.home : g.away;
        const ClubState& s = c.clubs[club];
        const bool two = !s.backup.empty();
        // r97: a CPU club's star takes more of its goals on the harder difficulties, more again when it's on
        // form (the league's stars racing you for the Golden Boot and the MVP)
        double star = 0.0;
        if (!is_human(c, club))  // r100
        {
            const PStats& st = (s.startsBackup && two) ? s.bkSt : s.capSt;
            star = 0.04 * diff_k(c) + 0.03 * std::max(0, st.form);
        }
        for (int n = 0; n < goals; n++)
        {
            if (!two)
            {
                out[side][rand01(c.rng) < 0.42 + star ? 0 : 1 + (int)(next_rand(c.rng) % 3)]++;
                continue;
            }
            const double r = rand01(c.rng);
            out[side][r < 0.30 + star ? 0 : (r < 0.55 + star ? 4 : 2 + (int)(next_rand(c.rng) % 2))]++;
        }
    }
}

// The players' posts (Lance: "players posting social-media style"; PG-13, no swearing), by personality
// (PERS_* + 1; 0 = neutral) and what happened. %s = the other club / place.
enum
{
    EV_WIN,
    EV_LOSS,
    EV_HATTRICK,
    EV_HOT,
    EV_SLUMP,
    EV_TRADED,
    EV_BENCHED,
    EV_RETIRED,
    EV_COUNT,
};
const char* const kPosts[PERS_COUNT + 1][EV_COUNT] = {
    // neutral (your own, for now)
    {"Good win over %s. On to the next one.", "Tough one against %s. We go again.", "Three goals today. Team effort.",
     "Feeling sharp right now.", "Rough stretch. I'll work through it.", "New club, %s. Let's get to work.",
     "Bench this week. I'll be ready.", "That's the last whistle for me. Thank you, %s."},
    // Hothead
    {"Told everyone. %s couldn't handle us.", "That ref owes me an apology. %s got lucky.",
     "Hat trick. Write my name down.", "Nobody can stop me right now. Nobody.", "Don't talk to me about my form.",
     "Traded to %s? Fine. Watch what happens when we play them.", "Benched?! I'll remember this.",
     "Retiring. Still could've taken any of you."},
    // Showboat
    {"Smooth win vs %s. Did you see my celebration?", "Lost to %s but I looked great doing it.",
     "Three goals, zero sweat. Autographs after practice.", "Hottest player in the league. Not a debate.",
     "Even legends have off weeks.", "%s just got a lot more fun to watch.", "The bench is a stage too.",
     "Retiring at the top of my game. You're welcome, %s."},
    // Silent assassin
    {"...", ".", "3.", "...", "...", "%s.", "...", "Done."},
    // Honorable
    {"Respect to %s. They pushed us all the way.", "Credit to %s. They earned that one.",
     "Grateful for my teammates. Those goals were theirs too.", "Just trying to do my job well.",
     "I owe my club better. I'll find it.", "Thank you to my old club. Proud to join %s.",
     "Coach knows best. I'll earn my spot back.", "It's been an honour. Thank you, %s."},
    // Trickster
    {"%s never saw it coming. Neither did their keeper.", "Fine, %s. Enjoy it while it lasts.",
     "Three goals? I was trying for four.", "You can't mark what you can't find.",
     "Slump? I'm lulling everyone into a false sense of security.", "Surprise, %s! I'm yours now.",
     "Bench is the best seat for watching chaos.", "Retiring. Or am I? Yes. Probably."},
    // Underdog
    {"Nobody picked us against %s. Nobody.", "We'll be back, %s. We always come back.",
     "Hat trick. Not bad for someone nobody rated.", "Proving people wrong, one week at a time.",
     "Been down before. Always got back up.", "%s believed in me. I won't let them down.",
     "Back to grinding. I'll be back out there.", "Thanks for believing in a long shot, %s."},
    // Veteran
    {"Steady win against %s. That's how it's done.", "Learn from it. %s were the better side today.",
     "Still got it. Three today.", "Experience is a fine thing.", "Seen worse. It turns around.",
     "Packing my boots for %s. One more chapter.", "Let the young ones run. I'll be ready.",
     "Hanging them up. Thank you, %s, for everything."},
    // Front-runner
    {"Easy work vs %s. Winning is what I do.", "%s caught us on a bad day. Moving on.",
     "Hat trick, top of the table vibes.", "When we're winning, I'm unstoppable.",
     "Whole team's off right now. Not just me.", "Heading somewhere that wins: %s.",
     "Fine. Wake me when we're winning again.", "Leaving while we're on top, %s."},
};

void post(Career& c, int personality, int ev, const std::string& name, const std::string& other)
{
    if (c.feed.size() >= 24)
        return;
    const int p = personality < 0 || personality >= PERS_COUNT ? 0 : personality + 1;
    char text[160];
    snprintf(text, sizeof text, kPosts[p][ev], other.c_str());
    std::string handle = name;
    for (size_t i = 0; i < handle.size(); i++)
        if (handle[i] == ' ')
            handle[i] = '_';
    c.feed.push_back("@" + handle + ": " + text);
}

std::string upper(const std::string& s);

// One game's A-Life: the scorers' records, form (+1 a win, -1 a loss, +1 more for 2+ goals), each
// club's streak, the news (hat tricks, streaks, on fire / slumps) and a few posts.
void life_game(Career& c, const Game& g, const int scorers[2][kScorerSlots])
{
    for (int side = 0; side < 2; side++)
    {
        const int club = side == 0 ? g.home : g.away, other = side == 0 ? g.away : g.home;
        ClubState& s = c.clubs[club];
        const bool won = (side == 0) == (g.homeGoals > g.awayGoals);
        const bool backupStarts = s.startsBackup && !s.backup.empty();
        // r78: a club with a backup fields both captains (4 = the other one); its first named sidekick
        // sits out (the co-captain plays in that spot)
        const bool two = !s.backup.empty();
        PStats* who[kScorerSlots] = {backupStarts ? &s.bkSt : &s.capSt, &s.sk[0], &s.sk[1], &s.sk[2],
                                     backupStarts ? &s.capSt : &s.bkSt};
        const std::string capName = name_of(backupStarts ? s.backup : s.captain);
        const std::string names[kScorerSlots] = {capName, s.names[0], s.names[1], s.names[2],
                                                 two ? name_of(backupStarts ? s.captain : s.backup) : std::string()};
        const std::string place = upper(s_clubs[club].place), otherPlace = s_clubs[other].place;
        for (int k = 0; k < kScorerSlots; k++)
        {
            if (two ? k == 1 : k == 4)
                continue;
            PStats& p = *who[k];
            const int goals = scorers[side][k];
            p.games++;
            p.goals += goals;
            p.seasonGoals += goals;
            const int before = p.form;
            p.form = std::max(-3, std::min(3, p.form + (won ? 1 : -1) + (goals >= 2 ? 1 : 0)));
            if (goals >= 3)
            {
                c.news.push_back("HAT TRICK: " + upper(names[k]) + " SCORED " + std::to_string(goals) + " FOR " + place);
                post(c, p.personality, EV_HATTRICK, names[k], otherPlace);
            }
            else if (p.form == 3 && before < 3)
            {
                c.news.push_back(upper(names[k]) + " (" + place + ") IS ON FIRE");
                post(c, p.personality, EV_HOT, names[k], otherPlace);
            }
            else if (p.form == -3 && before > -3)
            {
                c.news.push_back(upper(names[k]) + " (" + place + ") IS IN A SLUMP");
                post(c, p.personality, EV_SLUMP, names[k], otherPlace);
            }
        }
        s.streak = won ? (s.streak > 0 ? s.streak + 1 : 1) : (s.streak < 0 ? s.streak - 1 : -1);
        if (s.streak >= 4 && s.streak % 2 == 0)
            c.news.push_back(place + " HAVE WON " + std::to_string(s.streak) + " IN A ROW");
        if (s.streak <= -4 && s.streak % 2 == 0)
            c.news.push_back(place + " HAVE LOST " + std::to_string(-s.streak) + " STRAIGHT");
        // the captain posts now and then (yours more often: it's your club's feed too)
        if (rand01(c.rng) < (is_human(c, club) ? 0.7 : 0.12))
            post(c, who[0]->personality, won ? EV_WIN : EV_LOSS, capName, otherPlace);
    }
}

// ----------------------------------------------------------------------------------- r72: Nemesis
const char* const kTitles[] = {"THE GRUDGE",     "THE PLAYOFF KILLER", "THE GIANT SLAYER", "THE ENFORCER",
                               "THE TORMENTOR",  "THE SHOWSTOPPER",    "THE GHOST",        "THE AVENGER",
                               "THE MENACE",     "THE SNIPER",         "THE TRAITOR"};
enum
{
    T_GRUDGE,
    T_PLAYOFF_KILLER,
    T_GIANT_SLAYER,
    T_ENFORCER,
    T_TORMENTOR,
    T_SHOWSTOPPER,
    T_GHOST,
    T_AVENGER,
    T_MENACE,
    T_SNIPER,
    T_TRAITOR,
    T_COUNT,
};
// What a nemesis gains each time it beats you (Lance: traits, a signature move, a bigger crowd; no
// raw stat boosts).
const char* const kTraits[] = {"SNIPER: SHOOTS ON SIGHT AGAINST YOU", "ENFORCER: GOES AFTER YOUR CAPTAIN",
                               "CLUTCH: DEADLY LATE ON",             "SHOWMAN: BIGGER CROWD, BIGGER ENTRANCE",
                               "WALL: SHUTS YOUR CAPTAIN DOWN",      "SIGNATURE MOVE: SAVES ITS BEST FOR YOU"};
const int kTraitCount = 6;
std::vector<std::string> s_metThisWeek;  // nemesis keys met this week (they don't cool)

int trait_count(int traits)
{
    int n = 0;
    for (int b = 0; b < kTraitCount; b++)
        n += (traits >> b) & 1;
    return n;
}

// r100: the person whose beef is being worked out (an online league member's club, each in turn); -1 = the
// career's own player (myClub)
int s_nemMe = -1;
int nem_me(const Career& c) { return s_nemMe >= 0 ? s_nemMe : c.myClub; }

// The beef `key` has with the person being worked out (nem_me).
Nemesis* find_nemesis(Career& c, const std::string& key)
{
    const int me = nem_me(c);
    for (size_t i = 0; i < c.nems.size(); i++)
        if (c.nems[i].key == key && nem_owner(c, c.nems[i]) == me)
            return &c.nems[i];
    return NULL;
}

// r100: every person's beef record for `key` (a player moving, retiring, evolving is news to all of them)
template <class F>
void each_nemesis(Career& c, const std::string& key, F f)
{
    for (size_t i = 0; i < c.nems.size(); i++)
        if (c.nems[i].key == key)
            f(&c.nems[i]);
}

Nemesis& nemesis_for(Career& c, const std::string& key, const std::string& name, int club, bool teammate)
{
    if (Nemesis* n = find_nemesis(c, key))
    {
        n->club = club;
        n->name = name;
        return *n;
    }
    Nemesis n;
    n.owner = c.league.empty() ? -1 : nem_me(c);  // r100
    n.key = key;
    n.name = name;
    n.club = club;
    n.teammate = teammate;
    n.heat = 0;
    n.rank = 0;
    n.beatYou = n.lostToYou = 0;
    n.title = T_GRUDGE;
    n.traits = 0;
    n.since = c.season;
    n.active = false;
    for (int k = 0; k < 5; k++)
        n.last[k] = 99;
    c.nems.push_back(n);
    return c.nems.back();
}

void nem_log(Nemesis& n, const std::string& line)
{
    n.log.insert(n.log.begin(), line);
    if (n.log.size() > 6)
        n.log.resize(6);
}

// The beef grows (or cools); past 35 they become your nemesis, with a title from how it started.
void add_heat(Career& c, Nemesis& n, int delta, int titleHint, const std::string& why)
{
    n.heat = std::max(0, std::min(100, n.heat + delta));
    if (!n.active && n.heat >= 35)
    {
        n.active = true;
        n.rank = std::max(n.rank, 1);
        n.title = titleHint;
        n.since = c.season;
        const std::string place = n.club >= 0 ? upper(s_clubs[n.club].place) : std::string("RETIRED");
        // r100: in an online league everyone reads the same news: whose nemesis, by club
        const std::string you = c.league.empty() ? std::string("YOU") : upper(s_clubs[nem_owner(c, n)].place);
        if (n.teammate)
            c.news.push_back(c.league.empty() ? "YOUR OWN " + upper(n.name) + " HAS TURNED ON YOU"
                                              : upper(n.name) + " HAS TURNED ON " + you);
        else
            c.news.push_back("NEMESIS: " + upper(n.name) + " " + kTitles[n.title] + " (" + place + ") HAS BEEF WITH " + you);
        nem_log(n, "SEASON " + std::to_string(c.season) + ": BECAME YOUR NEMESIS (" + why + ")");
        fprintf(stderr, "[franchise] nemesis: %s %s (%s)\n", n.name.c_str(), kTitles[n.title], why.c_str());
        fflush(stderr);
    }
    else if (n.active && n.heat < 20)
    {
        n.active = false;
        c.news.push_back(upper(n.name) + " HAS MOVED ON (FOR NOW)");
        nem_log(n, "SEASON " + std::to_string(c.season) + ": MOVED ON");
    }
}

bool active_nemesis_key(const Career& c, const std::string& key)
{
    for (size_t i = 0; i < c.nems.size(); i++)
        if (c.nems[i].active && !c.nems[i].teammate && c.nems[i].key == key)
            return true;
    return false;
}

int nemesis_title_for(int personality, int fallback)
{
    switch (personality)
    {
    case PERS_SHOWBOAT:
        return T_SHOWSTOPPER;
    case PERS_SILENT:
        return T_GHOST;
    case PERS_HOTHEAD:
        return T_MENACE;
    case PERS_TRICKSTER:
        return T_TORMENTOR;
    }
    return fallback;
}

// A game of yours: beef from how it went (Lance's triggers: they beat you / scored a lot / knocked you
// out; you humiliated them / Super Strikes / eliminated them; hits between the captains).
void nemesis_game(Career& c, const Game& g, const int scorers[2][kScorerSlots], const int ss[2], const int capHits[2],
                  const int anyHits[2])
{
    const int me = nem_me(c);  // r100: the person whose game it is (an online league's: each in turn)
    const int m = g.home == me ? 0 : 1, o = 1 - m;
    const int opp = m == 0 ? g.away : g.home;
    const int mine = m == 0 ? g.homeGoals : g.awayGoals, theirs = m == 0 ? g.awayGoals : g.homeGoals;
    const int margin = theirs - mine;
    const ClubState& os = c.clubs[opp];
    const bool oppBackup = os.startsBackup && !os.backup.empty();
    const std::string key = oppBackup ? os.backup : os.captain;
    const PStats& ost = oppBackup ? os.bkSt : os.capSt;
    Nemesis& n = nemesis_for(c, key, name_of(key), opp, false);
    s_metThisWeek.push_back(key);
    bool knockedYouOut = false, youKnockedThem = false;
    if (g.series >= 0 && g.series < (int)c.series.size())
    {
        const int w = series_winner(c.series[g.series]);
        knockedYouOut = w == opp;
        youKnockedThem = w == me;
    }
    int d = 0, hint = nemesis_title_for(ost.personality, T_GRUDGE);
    std::string why;
    if (margin > 0)
    {
        d += 12 + 4 * std::min(margin, 4);
        why = "BEAT YOU " + std::to_string(theirs) + "-" + std::to_string(mine);
        if (seed_of(c, opp) > seed_of(c, me) && seed_of(c, me) > 0)
            hint = T_GIANT_SLAYER;
    }
    else if (-margin >= 3)
    {
        d += 15;
        why = "YOU HUMILIATED THEM " + std::to_string(mine) + "-" + std::to_string(theirs);
        hint = T_AVENGER;
    }
    else
        why = "LOST TO YOU " + std::to_string(mine) + "-" + std::to_string(theirs);
    if (margin == 1 || margin == -1 || g.overtime)
        d += 6;  // a real contest
    d += 8 * ss[m];
    d += 4 * capHits[o] + 3 * capHits[m] + anyHits[o];
    if (capHits[o] >= 2)
        hint = T_ENFORCER;
    if (scorers[o][0] >= 3)
        hint = T_SNIPER;
    if (knockedYouOut)
    {
        d += 30;
        hint = T_PLAYOFF_KILLER;
        why = "KNOCKED YOU OUT OF THE PLAYOFFS";
    }
    if (youKnockedThem)
    {
        d += 25;
        why = "YOU KNOCKED THEM OUT";
    }
    // r97: grudges build faster on the harder difficulties (x0.8 Rookie .. x1.55 Legend)
    if (d > 0)
        d = d * (80 + 25 * diff_k(c)) / 100;
    for (int k = 4; k > 0; k--)
        n.last[k] = n.last[k - 1];
    n.last[0] = margin;
    if (margin > 0)
        n.beatYou++;
    else
        n.lostToYou++;
    const bool wasActive = n.active;
    add_heat(c, n, d, hint, why);
    const std::string when = n.club >= 0 ? (c.phase == PHASE_REGULAR ? "WEEK " + std::to_string(c.week)
                                                                       : "PLAYOFFS")
                                         : "";
    if (wasActive && n.active)
    {
        if (margin > 0)
        {
            // stronger each time it beats you: a new trait, up to five ranks
            n.rank = std::min(5, n.rank + 1);
            int free[kTraitCount], nf = 0;
            for (int b = 0; b < kTraitCount; b++)
                if (!((n.traits >> b) & 1))
                    free[nf++] = b;
            if (nf > 0)
            {
                const int t = free[next_rand(c.rng) % nf];
                n.traits |= 1 << t;
                c.news.push_back("NEMESIS " + upper(n.name) + " GETS STRONGER: " + kTraits[t]);
            }
            nem_log(n, when + ": " + why);
        }
        else
            nem_log(n, when + ": " + why);
    }
    // a lopsided run doesn't make a rivalry (Lance): four straight by 2+ the same way cools it
    bool lopsided = true;
    for (int k = 0; k < 4; k++)
        if (n.last[k] == 99 || (n.last[k] > -2 && n.last[k] < 2) || (n.last[k] > 0) != (n.last[0] > 0))
            lopsided = false;
    if (lopsided && n.active)
    {
        nem_log(n, "TOO ONE-SIDED TO BE A RIVALRY");
        add_heat(c, n, -25, n.title, "");
    }
    // their sidekicks who scored a lot against you get a taste for it too
    for (int k = 1; k < 4; k++)
        if (scorers[o][k] >= 2)
        {
            const std::string sk = os.names[k - 1];
            Nemesis& sn = nemesis_for(c, "sk#" + std::to_string(opp) + "#" + sk, sk, opp, false);
            s_metThisWeek.push_back(sn.key);
            add_heat(c, sn, 8 * scorers[o][k], nemesis_title_for(os.sk[k - 1].personality, T_SNIPER),
                     "SCORED " + std::to_string(scorers[o][k]) + " ON YOU");
        }
}

// Your own players turning on you (Lance: when losing, when a grown sidekick wants your spot): the
// benched backup while you lose, a sidekick outscoring your captain.
void teammates_week(Career& c, bool lost)
{
    const int me = nem_me(c);  // r100: the person whose club it is
    ClubState& s = c.clubs[me];
    if (!s.backup.empty())
    {
        const bool benched = !s.startsBackup;
        Nemesis& n = nemesis_for(c, s.backup, name_of(s.backup), me, true);
        int d = 0;
        if (benched && lost)
            d += 4 + (s.streak <= -3 ? 4 : 0);
        if (!benched)
            d -= 10;  // got the spot
        if (!lost)
            d -= 3;
        add_heat(c, n, d, T_TRAITOR, "BENCHED WHILE YOU LOSE");
    }
    const PStats& cap = s.startsBackup && !s.backup.empty() ? s.bkSt : s.capSt;
    for (int k = 0; k < 3; k++)
    {
        const bool wantsSpot = s.sk[k].seasonGoals >= cap.seasonGoals + 3 && s.sk[k].form >= 2;
        if (!wantsSpot && !find_nemesis(c, "sk#" + std::to_string(me) + "#" + s.names[k]))
            continue;
        Nemesis& n = nemesis_for(c, "sk#" + std::to_string(me) + "#" + s.names[k], s.names[k], me, true);
        add_heat(c, n, wantsSpot ? 3 : -3, T_TRAITOR, "WANTS YOUR CAPTAIN'S SPOT");
    }
}

// A game of `me`'s (default: this headset's club) against a club whose starting captain is me's nemesis.
const Nemesis* nemesis_in(const Career& c, const Game& g, int me = -1)
{
    if (me < 0)
        me = c.myClub;
    if (g.home != me && g.away != me)
        return NULL;
    const ClubState& os = c.clubs[g.home == me ? g.away : g.home];
    const std::string& key = (os.startsBackup && !os.backup.empty()) ? os.backup : os.captain;
    for (size_t i = 0; i < c.nems.size(); i++)
        if (c.nems[i].active && !c.nems[i].teammate && c.nems[i].key == key && nem_owner(c, c.nems[i]) == me)
            return &c.nems[i];
    return NULL;
}

// r100: a game with a person's nemesis in it (either side's person, in an online league)
bool nemesis_match(const Career& c, const Game& g)
{
    return (is_human(c, g.home) && nemesis_in(c, g, g.home) != NULL) ||
           (is_human(c, g.away) && nemesis_in(c, g, g.away) != NULL);
}

// Its traits show against you (a little edge in a worked-out game; no raw stat boosts in a played one).
// r100: the side's club gets it when the other side's person has it as a nemesis (not a person's club).
double nemesis_edge(const Career& c, const Game& g, int side)
{
    const int club = side == 0 ? g.home : g.away, other = side == 0 ? g.away : g.home;
    if (is_human(c, club) || !is_human(c, other))
        return 0.0;
    const Nemesis* n = nemesis_in(c, g, other);
    if (n == NULL)
        return 0.0;
    return 0.3 * trait_count(n->traits) * (1.0 + 0.35 * diff_k(c));  // r97: more on the harder difficulties
}

// Every week: those you didn't meet cool off a little (the history stays).
void nemesis_week(Career& c)
{
    for (size_t i = 0; i < c.nems.size(); i++)
    {
        Nemesis& n = c.nems[i];
        if (n.teammate || std::find(s_metThisWeek.begin(), s_metThisWeek.end(), n.key) != s_metThisWeek.end())
            continue;
        if (n.heat > 0)
            add_heat(c, n, -2, n.title, "");
    }
    s_metThisWeek.clear();
}

// The week's CPU club decisions: who starts (form), trades until the deadline (week 12): backup
// captains between clubs, now and then a whole sidekick trio.
void swap_backups(Career& c, int a, int b)
{
    ClubState& x = c.clubs[a];
    ClubState& y = c.clubs[b];
    std::swap(x.backup, y.backup);
    std::swap(x.bk, y.bk);
    std::swap(x.bkSt, y.bkSt);
    x.startsBackup = y.startsBackup = false;
}

void swap_sidekicks(Career& c, int a, int b)
{
    ClubState& x = c.clubs[a];
    ClubState& y = c.clubs[b];
    std::swap(x.kind, y.kind);
    std::swap(x.side, y.side);
    for (int k = 0; k < 3; k++)
    {
        std::swap(x.names[k], y.names[k]);
        std::swap(x.sk[k], y.sk[k]);
        std::swap(x.skAge[k], y.skAge[k]);
    }
}

std::string kind_name(const std::string& key);

void cpu_week(Career& c)
{
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        if (is_human(c, i))  // r100: every person's club
            continue;
        ClubState& s = c.clubs[i];
        if (s.backup.empty())
            continue;
        // the bench player's form drifts back to even
        if (s.startsBackup)
            s.capSt.form += s.capSt.form > 0 ? -1 : (s.capSt.form < 0 ? 1 : 0);
        else
            s.bkSt.form += s.bkSt.form > 0 ? -1 : (s.bkSt.form < 0 ? 1 : 0);
        const PStats& on = s.startsBackup ? s.bkSt : s.capSt;
        const PStats& off = s.startsBackup ? s.capSt : s.bkSt;
        if (off.form - on.form >= 3)
        {
            const std::string out = name_of(s.startsBackup ? s.backup : s.captain);
            s.startsBackup = !s.startsBackup;
            const std::string in = name_of(s.startsBackup ? s.backup : s.captain);
            c.news.push_back(upper(s_clubs[i].place) + " START " + upper(in) + " OVER " + upper(out));
            post(c, (s.startsBackup ? s.capSt : s.bkSt).personality, EV_BENCHED, out, s_clubs[i].place);
        }
    }
    if (c.phase != PHASE_REGULAR || c.week > 12)
        return;
    if (rand01(c.rng) < 0.3)
    {
        int a = (int)(next_rand(c.rng) % PORT_FR_CLUBS), b = (int)(next_rand(c.rng) % PORT_FR_CLUBS);
        if (a != b && !is_human(c, a) && !is_human(c, b) && !c.clubs[a].backup.empty() && !c.clubs[b].backup.empty())
        {
            const std::string na = name_of(c.clubs[a].backup), nb = name_of(c.clubs[b].backup);
            swap_backups(c, a, b);
            c.news.push_back("TRADE: " + upper(s_clubs[a].place) + " SEND " + upper(na) + " TO " + upper(s_clubs[b].place) +
                             " FOR " + upper(nb));
            post(c, c.clubs[b].bkSt.personality, EV_TRADED, na, s_clubs[b].place);
            post(c, c.clubs[a].bkSt.personality, EV_TRADED, nb, s_clubs[a].place);
        }
    }
    if (rand01(c.rng) < 0.06)
    {
        int a = (int)(next_rand(c.rng) % PORT_FR_CLUBS), b = (int)(next_rand(c.rng) % PORT_FR_CLUBS);
        if (a != b && !is_human(c, a) && !is_human(c, b) && c.clubs[a].kind != c.clubs[b].kind)
        {
            c.news.push_back("BIG TRADE: " + upper(s_clubs[a].place) + " AND " + upper(s_clubs[b].place) + " SWAP SIDEKICKS (" +
                             upper(kind_name(c.clubs[a].kind)) + " FOR " + upper(kind_name(c.clubs[b].kind)) + ")");
            swap_sidekicks(c, a, b);
        }
    }
    if (c.week == 12)
        c.news.push_back("THE TRADE DEADLINE HAS PASSED");
}

// r71: awards night once the champion is crowned (Lance: MVP, Golden Boot, Rookie of the Year, Best
// Keeper, records), into the hall of fame.
void awards_night(Career& c)
{
    struct Pick
    {
        std::string name;
        int club, goals;
        double score;
        std::string key;  // r73: its portrait
    };
    // r73: a sidekick's portrait: its kind's (the character select menu's)
    auto kind_portrait = [](const std::string& kind) -> std::string {
        if (kind == "toad" || kind == "koopa" || kind == "birdo")
            return "strikers_" + kind;
        if (kind == "hammerbros")
            return std::string("strikers_hammerbro");
        return kind;
    };
    Pick boot = {"", -1, -1, 0.0, ""}, mvp = {"", -1, -1, -1e9, ""}, rookie = {"", -1, 0, 0.0, ""};
    // r80 (Lance: a "sidekick of the year" award, "basically a 6th man of the year"): the best of the
    // league's named sidekicks: its goals, its form, how its club did
    Pick sixth = {"", -1, 0, -1e9, ""};
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        const ClubState& s = c.clubs[i];
        const PStats* caps[2] = {&s.capSt, &s.bkSt};
        const std::string capKeys[2] = {s.captain, s.backup};
        for (int b = 0; b < 2; b++)
        {
            if (capKeys[b].empty())
                continue;
            const PStats& p = *caps[b];
            const double score = p.seasonGoals * 3.0 + s.wins * 2.0 + (i == c.champion ? 6.0 : 0.0) + p.form;
            if (score > mvp.score)
                mvp = Pick{name_of(capKeys[b]), i, p.seasonGoals, score, capKeys[b]};
            if (p.seasonGoals > boot.goals)
                boot = Pick{name_of(capKeys[b]), i, p.seasonGoals, 0.0, capKeys[b]};
        }
        for (int k = 0; k < 3; k++)
        {
            if (s.sk[k].seasonGoals > boot.goals)
                boot = Pick{s.names[k], i, s.sk[k].seasonGoals, 0.0, kind_portrait(s.kind)};
            if (s.skAge[k] == 0 && s.sk[k].seasonGoals > rookie.goals)
                rookie = Pick{s.names[k], i, s.sk[k].seasonGoals, 0.0, kind_portrait(s.kind)};
            const double sixthScore = s.sk[k].seasonGoals * 3.0 + s.sk[k].form * 1.5 + s.wins * 0.5 +
                                      (i == c.champion ? 3.0 : 0.0);
            if (!s.names[k].empty() && sixthScore > sixth.score)
                sixth = Pick{s.names[k], i, s.sk[k].seasonGoals, sixthScore, kind_portrait(s.kind)};
        }
    }
    int keeper = 0;
    for (int i = 1; i < PORT_FR_CLUBS; i++)
        if (c.clubs[i].goalsAgainst < c.clubs[keeper].goalsAgainst)
            keeper = i;
    const std::string season = "SEASON " + std::to_string(c.season);
    c.awards.clear();
    c.awardKeys.clear();
    c.awards.push_back("CHAMPIONS: " + upper(club_name(c.champion)));
    c.awardKeys.push_back("club_" + s_clubs[c.champion].id);
    if (mvp.club >= 0)
    {
        c.awards.push_back("MVP: " + upper(mvp.name) + " (" + upper(s_clubs[mvp.club].place) + ")");
        c.awardKeys.push_back(mvp.key);
    }
    if (boot.club >= 0)
    {
        c.awards.push_back("GOLDEN BOOT: " + upper(boot.name) + " (" + upper(s_clubs[boot.club].place) + "), " +
                           std::to_string(boot.goals) + " GOALS");
        c.awardKeys.push_back(boot.key);
    }
    if (sixth.club >= 0)
    {
        c.awards.push_back("SIDEKICK OF THE YEAR: " + upper(sixth.name) + " (" + upper(s_clubs[sixth.club].place) + "), " +
                           std::to_string(sixth.goals) + (sixth.goals == 1 ? " GOAL" : " GOALS"));
        c.awardKeys.push_back(sixth.key);
    }
    if (rookie.club >= 0)
    {
        c.awards.push_back("ROOKIE OF THE YEAR: " + upper(rookie.name) + " (" + upper(s_clubs[rookie.club].place) + ")");
        c.awardKeys.push_back(rookie.key);
    }
    c.awards.push_back("BEST KEEPER: " + upper(s_clubs[keeper].place) + " (" + std::to_string(c.clubs[keeper].goalsAgainst) +
                       " CONCEDED)");
    c.awardKeys.push_back("club_" + s_clubs[keeper].id);
    if (boot.club >= 0 && boot.goals > c.recordGoals)
    {
        c.awards.push_back("NEW RECORD: " + std::to_string(boot.goals) + " GOALS IN A SEASON (" + upper(boot.name) + ")");
        c.awardKeys.push_back(boot.key);
        c.recordGoals = boot.goals;
        c.recordSeason = c.season;
        c.recordName = boot.name;
    }
    // the hall of fame, newest first
    std::vector<std::string> fame;
    for (size_t i = 0; i < c.awards.size(); i++)
        if (c.awards[i].compare(0, 4, "BEST") != 0)
            fame.push_back(season + " " + c.awards[i]);
    c.history.insert(c.history.begin(), fame.begin(), fame.end());
    if (c.history.size() > 60)
        c.history.resize(60);
    fprintf(stderr, "[franchise] awards night, season %d: %d awards\n", c.season, (int)c.awards.size());
    fflush(stderr);
}

// r71: the off-season (next_season): every sidekick a season older; the old ones may retire (a rookie
// takes the spot); season records start over; a few trades between CPU clubs.
void off_season(Career& c)
{
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = c.clubs[i];
        for (int k = 0; k < 3; k++)
        {
            s.skAge[k]++;
            const double p = s.skAge[k] >= 3 ? 0.25 * (s.skAge[k] - 2) : 0.0;
            if (rand01(c.rng) < p)
            {
                c.news.push_back("RETIRED: " + upper(s.names[k]) + " (" + upper(s_clubs[i].place) + ") AFTER " +
                                 std::to_string(s.skAge[k]) + " SEASONS, " + std::to_string(s.sk[k].goals) + " GOALS");
                post(c, s.sk[k].personality, EV_RETIRED, s.names[k], s_clubs[i].place);
                each_nemesis(c, "sk#" + std::to_string(i) + "#" + s.names[k], [&](Nemesis* n) {  // r72; r100: everyone's
                    n->club = -1;
                    n->active = false;
                    nem_log(*n, "SEASON " + std::to_string(c.season) + ": RETIRED");
                });
                // a rookie: a new name (not one already in the trio)
                std::string name = "Rookie";
                for (int tries = 0; tries < 40; tries++)
                {
                    char key[96];
                    snprintf(key, sizeof key, "ssl%u|rookie|%d|%d|%d|%d", c.seed, c.season, i, k, tries);
                    const char* n = PortGuestSidekickNameFor(s.kind.c_str(), key);
                    if (n == NULL)
                        break;
                    const std::string first = std::string(n).substr(0, std::string(n).find(' '));
                    bool clash = false;
                    for (int o = 0; o < 3; o++)
                        if (o != k && s.names[o].substr(0, s.names[o].find(' ')) == first)
                            clash = true;
                    if (!clash)
                    {
                        name = n;
                        break;
                    }
                }
                s.names[k] = name;
                init_stats(s.sk[k], i == c.myClub ? PERS_NEUTRAL : (int)(next_rand(c.rng) % PERS_COUNT));
                s.skAge[k] = 0;
                c.news.push_back("ROOKIE: " + upper(name) + " JOINS " + upper(s_clubs[i].place));
            }
        }
        PStats* all[5] = {&s.capSt, &s.bkSt, &s.sk[0], &s.sk[1], &s.sk[2]};
        for (int k = 0; k < 5; k++)
        {
            all[k]->seasonGoals = 0;
            all[k]->form /= 2;
        }
        s.streak = 0;
    }
    for (int t = 0; t < 3; t++)  // the off-season trade window
    {
        int a = (int)(next_rand(c.rng) % PORT_FR_CLUBS), b = (int)(next_rand(c.rng) % PORT_FR_CLUBS);
        if (a != b && !is_human(c, a) && !is_human(c, b) && !c.clubs[a].backup.empty() && !c.clubs[b].backup.empty())
        {
            const std::string na = name_of(c.clubs[a].backup), nb = name_of(c.clubs[b].backup);
            swap_backups(c, a, b);
            c.news.push_back("OFF-SEASON TRADE: " + upper(na) + " TO " + upper(s_clubs[b].place) + ", " + upper(nb) + " TO " +
                             upper(s_clubs[a].place));
        }
    }
}

// ------------------------------------------------------------------------------ r70c: evolutions
// What a captain can evolve into at its level (its forms on this headset only), 0-2 of them.
int evolutions_for(const std::string& key, const Prog& p, std::string out[2])
{
    int n = 0;
    const std::vector<Captain> caps = captains();
    for (size_t i = 0; i < s_evos.size() && n < 2; i++)
        if (s_evos[i].from == key && p.level >= s_evos[i].level && find_captain(caps, s_evos[i].to) != NULL)
            out[n++] = s_evos[i].to;
    return n;
}

std::string name_of(const std::string& key)
{
    const std::vector<Captain> caps = captains();
    const Captain* cp = find_captain(caps, key);
    return cp != NULL ? cp->name : key;
}

std::string kind_name(const std::string& key)  // r71: "Toads", "Boos"
{
    const std::vector<Kind> ks = kinds();
    for (size_t k = 0; k < ks.size(); k++)
        if (ks[k].key == key)
            return ks[k].name;
    return key;
}

const int kShinyOdds = 128;  // Lance: 1 in 128 an evolution (and once shiny, always shiny)

// Evolves a club's captain (or backup) into `to`: a shiny roll, the news.
void do_evolve(Career& c, int club, bool backup, const std::string& to)
{
    ClubState& s = c.clubs[club];
    std::string& key = backup ? s.backup : s.captain;
    Prog& p = backup ? s.bk : s.cap;
    const std::string from = key;
    const bool wasShiny = p.shiny;
    if (!p.shiny && next_rand(c.rng) % kShinyOdds == 0)
        p.shiny = true;
    key = to;
    p.declinedAt = 0;
    std::string line = upper(s_clubs[club].place) + "'S " + upper(name_of(from)) + " EVOLVED INTO " + upper(name_of(to));
    if (p.shiny && !wasShiny)
        line += " - SHINY!";
    c.news.push_back(line);
    // r72 (Lance): a nemesis that evolves comes back angrier, with a new title
    each_nemesis(c, from, [&](Nemesis* n) {  // r100: everyone's beef with it
        n->key = to;
        n->name = name_of(to);
        if (n->active && !n->teammate)
        {
            n->title = T_AVENGER;
            n->heat = std::min(100, n->heat + 15);
            const std::string whose = c.league.empty() ? std::string("YOUR NEMESIS") : upper(s_clubs[nem_owner(c, *n)].place) + "'S NEMESIS";
            c.news.push_back(whose + " EVOLVED: " + upper(n->name) + " " + kTitles[n->title] + " IS ANGRIER THAN EVER");
            nem_log(*n, "SEASON " + std::to_string(c.season) + ": EVOLVED INTO " + upper(n->name) + ", ANGRIER");
        }
    });
    fprintf(stderr, "[franchise] %s\n", line.c_str());
    fflush(stderr);
}

// The CPU clubs' Pokemon evolve when they can (a split: the stronger form). Yours wait for you.
void evolve_cpu(Career& c)
{
    for (int club = 0; club < PORT_FR_CLUBS; club++)
    {
        if (is_human(c, club))  // r100: every person's Pokemon wait for them
            continue;
        for (int b = 0; b < 2; b++)
        {
            ClubState& s = c.clubs[club];
            for (int step = 0; step < 3; step++)  // (a big jump in levels can go two forms at once)
            {
                const std::string& key = b ? s.backup : s.captain;
                if (key.empty())
                    break;
                std::string to[2];
                const int n = evolutions_for(key, b ? s.bk : s.cap, to);
                if (n == 0)
                    break;
                const std::string pick = n == 2 && rating(to[1]) > rating(to[0]) ? to[1] : to[0];
                do_evolve(c, club, b != 0, pick);
            }
        }
    }
}

// The rest of the week (or playoff day), then on to the next.
void finish_week(Career& c)
{
    c.results.clear();
    // r100: an online league's played games put their news in as they came (league_result): kept for this week
    if (c.league.empty() || c.newsWeek != c.week)
    {
        c.news.clear();
        c.feed.clear();
    }
    for (size_t i = 0; i < c.games.size(); i++)
    {
        Game& g = c.games[i];
        if (g.week != c.week)
            continue;
        if (!g.played)
            sim_game(c, g);
        // r100: a league game whose A-Life went in with its result: done
        if (std::find(c.lifeDone.begin(), c.lifeDone.end(), (int)i) == c.lifeDone.end())
        {
            // r71: the game's A-Life: a played match's real scorers, a simulated one's worked out
            int scorers[2][kScorerSlots];
            int ss[2] = {0, 0}, capHits[2] = {0, 0}, anyHits[2] = {0, 0};
            if ((int)i == s_lifeGame)
            {
                memcpy(scorers, s_lifeScorers, sizeof scorers);
                memcpy(ss, s_lifeSS, sizeof ss);
                memcpy(capHits, s_lifeCapHits, sizeof capHits);
                memcpy(anyHits, s_lifeAnyHits, sizeof anyHits);
            }
            else
                distribute_goals(c, g, scorers);
            life_game(c, g, scorers);
            // r72: beef; r100: every person's in the game (an online league can have two)
            for (int side = 0; side < 2; side++)
            {
                const int club = side == 0 ? g.home : g.away;
                if (!is_human(c, club))
                    continue;
                s_nemMe = club;
                nemesis_game(c, g, scorers, ss, capHits, anyHits);
                teammates_week(c, (g.home == club) != (g.homeGoals > g.awayGoals));
                s_nemMe = -1;
            }
        }
        std::string line = result_line(g);
        if (g.series >= 0 && g.series < (int)c.series.size())
        {
            const Series& s = c.series[g.series];
            char buf[48];
            const int w = series_winner(s);
            if (w >= 0)
                snprintf(buf, sizeof buf, "   %s WIN %d-%d", upper(s_clubs[w].place).c_str(), std::max(s.winsHi, s.winsLo),
                         std::min(s.winsHi, s.winsLo));
            else if (s.winsHi == s.winsLo)
                snprintf(buf, sizeof buf, "   SERIES %d-%d", s.winsHi, s.winsLo);
            else
                snprintf(buf, sizeof buf, "   %s LEAD %d-%d", upper(s_clubs[s.winsHi > s.winsLo ? s.hi : s.lo].place).c_str(),
                         std::max(s.winsHi, s.winsLo), std::min(s.winsHi, s.winsLo));
            line += buf;
        }
        c.results.push_back(line);
    }
    s_lifeGame = -1;
    nemesis_week(c);  // r72
    cpu_week(c);    // r71
    evolve_cpu(c);  // r70c
    c.week++;
    if (c.phase == PHASE_REGULAR && c.week > PORT_FR_WEEKS)
    {
        start_playoffs(c);
        const int s = seed_of(c, c.myClub);
        c.headline = s == 0 ? "Regular season over. You missed the playoffs."
                            : (s == 1 ? "Playoffs! You're the #1 seed: a bye through the Kickoff Round."
                                      : "Playoffs! You're the #" + std::to_string(s) + " seed.");
    }
    else if (c.phase == PHASE_PLAYOFFS && round_over(c))
    {
        next_round(c);
        if (c.phase == PHASE_DONE)
        {
            c.headline = club_name(c.champion) + " win the Super Strike Series!";
            awards_night(c);  // r71
        }
        else
        {
            bool in = false;
            for (size_t i = 0; i < c.series.size(); i++)
                if (c.series[i].round == c.round && (c.series[i].hi == c.myClub || c.series[i].lo == c.myClub))
                    in = true;
            c.headline = std::string(in ? "On to the " : "Up next: the ") +
                         round_name(c.round, c.round == 4 ? -1 : (c.myClub < 16 ? 0 : 1)) + "!";
        }
    }
    if (c.phase == PHASE_PLAYOFFS)
        schedule_day(c);
}

// r70b: the champion crowned: next season, the same clubs and players, a new schedule.
void next_season(Career& c)
{
    c.news.clear();
    c.feed.clear();
    off_season(c);  // r71: retirements, rookies, the trade window
    c.season++;
    c.week = 1;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = c.clubs[i];
        s.wins = s.losses = s.goalsFor = s.goalsAgainst = 0;
        s.cap.startLevel = s.cap.level;  // r77: this season's levels count from here
        s.bk.startLevel = s.bk.level;
        s.side.startLevel = s.side.level;
    }
    reset_playoffs(c);
    c.results.clear();
    make_schedule(c);
    c.headline = "Season " + std::to_string(c.season) + " kicks off!";
}

Game* my_game(Career& c)
{
    for (size_t i = 0; i < c.games.size(); i++)
    {
        Game& g = c.games[i];
        if (g.week == c.week && (g.home == c.myClub || g.away == c.myClub))
            return &g;
    }
    return NULL;
}

// ------------------------------------------------------------------------------- playing the match
enum
{
    MATCH_NONE,
    MATCH_PENDING,  // asked for: started on the main menu next frame
    MATCH_PLAYING,  // in the match (or its loading)
    MATCH_HOLD,     // r76, co-op: set up, waiting at its start for the friend's headset (xr_net)
};
int s_match = MATCH_NONE;
int s_matchGame = -1;
int s_matchClub[2] = {-1, -1};  // home, away
bool s_onMainMenu = false;
bool s_sawGame = false;  // the match's game state was reached (so being back on the menus = left it)
int s_watchGame = -1;    // r70b: a CPU v CPU game being watched (index into games), -1 your own
int s_introSerial = 0;   // r70b: + 1 at every franchise match's intro
// r76, co-op: the match as set up for the online start (both sides), the side this headset's players are on
PortNetMatch s_holdMatch;
int s_holdSide = 0;
// r100: the match is an online league's game between two people (the league's go: its own room, each on their club)
bool s_lgVersus = false;
unsigned s_huntRng = 0x2545F491u;  // r76: PortFranchiseHuntRoll, from the career at each match start
// r76: a co-op career change from outside; the friend's match start waiting for its career to arrive
unsigned s_changeSerial = 0;
bool s_coopWaitCareer = false;  // asked the friend for theirs (the careers card waits)
int s_coopWaitFrames = 0;
bool s_coopOpened = false;      // ... and it came in and was opened (the hub goes to it)
std::string s_coopLater;        // one that came in during a match, taken after it
unsigned s_coopLaterId = 0, s_coopLaterVersion = 0;
bool s_coopPlayWaiting = false;
unsigned s_coopPlayId = 0, s_coopPlayVersion = 0;
int s_coopPlayWatch = -1;
int s_coopPlayFrames = 0;  // waited for it (PortFranchiseFrame runs every headset frame, ~72 a second)

bool on_main_menu()
{
    GameSceneManager* fe = nlSingleton<GameSceneManager>::Instance();
    return fe != NULL && fe->mCurrentStackDepth > 0 && fe->m_sceneStack[fe->mCurrentStackDepth - 1] == SCENE_MAIN_MENU &&
           nlTaskManager::m_pInstance != NULL && nlTaskManager::m_pInstance->m_CurrState == 4;
}

// Both clubs' captains in Strikers captain slots (two can't share one): Strikers captains in their own,
// guests in their base, or the nearest free one.
void start_match()
{
    Career& c = s_career;
    const bool watching = s_watchGame >= 0 && s_watchGame < (int)c.games.size();
    Game* g = watching ? &c.games[s_watchGame] : my_game(c);
    GameInfoManager* gi = GameInfoManager::Instance();
    if (g == NULL || gi == NULL)
    {
        s_match = MATCH_NONE;
        s_watchGame = -1;
        return;
    }
    const std::vector<Captain> caps = captains();
    const std::vector<Kind> ks = kinds();
    const int clubs[2] = {g->home, g->away};
    const Captain* cap[2];
    const Kind* kind[2];
    for (int side = 0; side < 2; side++)
    {
        cap[side] = find_captain(caps, starter(c.clubs[clubs[side]]));
        if (cap[side] == NULL)
            cap[side] = &caps[3];  // Mario
        kind[side] = &ks[0];
        for (size_t k = 0; k < ks.size(); k++)
            if (ks[k].key == c.clubs[clubs[side]].kind)
                kind[side] = &ks[k];
    }
    // r78 (Lance: "2 captains and 2 sidekicks share the field", "a CO-Captain"): a club with a backup
    // captain fields both: the other one is its co-captain, in the team's slot 1 (include/port/lineup.h)
    const Captain* co[2] = {NULL, NULL};
    for (int side = 0; side < 2; side++)
    {
        const ClubState& cs = c.clubs[clubs[side]];
        if (!cs.backup.empty())
            co[side] = find_captain(caps, (cs.startsBackup && !cs.backup.empty()) ? cs.captain : cs.backup);
    }
    // Every captain on the field in a Strikers captain slot of its own (four can't share one): the
    // Strikers characters first in their own (a guest there would draw them as someone else), then the
    // guests in their base or the nearest free one; the starting captains before the co-captains.
    const Captain* who4[4] = {cap[0], cap[1], co[0], co[1]};
    int slot4[4] = {-1, -1, -1, -1};
    bool free[8];
    for (int t = 0; t < 8; t++)
        free[t] = true;
    for (int k = 0; k < 4; k++)
        if (who4[k] != NULL && who4[k]->team >= 0 && free[who4[k]->team])
        {
            slot4[k] = who4[k]->team;
            free[slot4[k]] = false;
        }
    for (int k = 0; k < 4; k++)
        if (who4[k] != NULL && slot4[k] < 0)
        {
            const int s = who4[k]->guest >= 0 ? PortGuestSlotFor(who4[k]->guest, free) : -1;
            slot4[k] = s >= 0 ? s : (who4[k]->slot >= 0 && who4[k]->slot < 8 && free[who4[k]->slot] ? who4[k]->slot : -1);
            if (slot4[k] < 0)
                for (int t = 0; t < 8 && slot4[k] < 0; t++)
                    if (free[t])
                        slot4[k] = t;
            if (slot4[k] >= 0)
                free[slot4[k]] = false;
        }
    int slot[2] = {slot4[0] >= 0 ? slot4[0] : 3, slot4[1] >= 0 ? slot4[1] : 3};
    for (int side = 0; side < 2; side++)
    {
        const int cs = slot4[2 + side];
        PortSetSecondCaptain(side, co[side] != NULL && cs >= 0 ? (int)ConvertToCharacterClass((eTeamID)cs) : -1);
        PortGuestSetCoCaptain(side, co[side] != NULL && cs >= 0 ? co[side]->guest : -1,
                              co[side] != NULL && co[side]->guest >= 0 &&
                                  (c.clubs[clubs[side]].startsBackup ? c.clubs[clubs[side]].cap : c.clubs[clubs[side]].bk).shiny);
    }
    gi->SetMode(GameInfoManager::GM_FRIENDLY);
    for (int side = 0; side < 2; side++)
    {
        gi->SetTeam((short)side, (eTeamID)slot[side]);
        gi->SetSidekick((short)side, (eSidekickID)kind[side]->sidekick);
        PortGuestSetPick(slot[side], PORT_GUEST_CAPTAIN, cap[side]->guest >= 0 ? cap[side]->guest : -1);
        PortGuestSetPick(slot[side], PORT_GUEST_SIDEKICK, kind[side]->guest >= 0 ? kind[side]->guest : -1);
        PortGuestSetShiny(side, cap[side]->guest >= 0 && starter_prog(c.clubs[clubs[side]]).shiny);  // r70c
    }
    const int stadium = s_clubs[g->home].stadium;
    gi->SetStadium((eStadiumID)(stadium >= 0 && stadium < MAX_STADIUMS ? stadium : 0));
    // r70b: watching a CPU v CPU game: no pad on either side (both AI; Start still pauses, A ends it)
    const int mySide = watching ? -1 : (g->home == c.myClub ? 0 : 1);
    for (int pad = 0; pad < 4; pad++)
        gi->SetPlayingSide((unsigned short)pad, (short)(pad == 0 ? mySide : -1));
    s_matchGame = (int)(g - &c.games[0]);
    s_matchClub[0] = g->home;
    s_matchClub[1] = g->away;
    memset(s_matchGoals, 0, sizeof s_matchGoals);  // r71
    memset(s_matchSS, 0, sizeof s_matchSS);        // r72
    memset(s_matchCapHits, 0, sizeof s_matchCapHits);
    memset(s_matchAnyHits, 0, sizeof s_matchAnyHits);
    s_match = MATCH_PLAYING;
    s_sawGame = false;
    // r76: the nemesis's hunting draws from the career (the same on both co-op headsets), not from
    // whatever this run drew before
    s_huntRng = (c.rng ^ ((unsigned)s_matchGame * 2654435761u)) | 1u;
    if (s_lgVersus)  // r100: the two league headsets' copies may differ in what others did since (c.rng): the seed
        s_huntRng = (c.seed ^ ((unsigned)s_matchGame * 2654435761u)) | 1u;
    fprintf(stderr, "[franchise] week %d: %s (%s, slot %d) vs %s (%s, slot %d) at stadium %d, you are side %d%s\n", c.week,
            s_clubs[g->home].place.c_str(), cap[0]->key.c_str(), slot[0], s_clubs[g->away].place.c_str(),
            cap[1]->key.c_str(), slot[1], stadium, mySide, c.coop ? " (co-op)" : (s_lgVersus ? " (league, online)" : ""));
    fflush(stderr);
    if ((c.coop && PortNetCoopFriend()) || s_lgVersus)
    {
        // r76, co-op with the friend here: the match waits at its start for theirs (xr_net pairs the two
        // and starts both in lockstep, the host's set-up for both); PortFranchiseFrame lets it go
        PortNetMatch& m = s_holdMatch;
        memset(&m, 0, sizeof m);
        m.mode = PORT_NET_MODE_FRANCHISE;
        for (int side = 0; side < 2; side++)
        {
            m.teams[side] = slot[side];
            m.sidekicks[side] = kind[side]->sidekick;
            for (int role = 0; role < 2; role++)
            {
                const int pick = role == 0 ? cap[side]->guest : kind[side]->guest;
                const char* pack = pick >= 0 ? PortGuestRosterPack(pick) : NULL;
                if (pack == NULL)
                    continue;
                strncpy(m.guests[side][role], pack, sizeof(m.guests[side][role]) - 1);
                PortGuestStatsFor(pick, m.guestStats[side][role], &m.guestStatMask[side][role]);
            }
        }
        for (int side = 0; side < 2; side++)  // r78: the co-captains (class, guest, its stats)
        {
            m.coCaptains[side] = PortSecondCaptain(side);
            const char* pack = co[side] != NULL && co[side]->guest >= 0 ? PortGuestRosterPack(co[side]->guest) : NULL;
            if (m.coCaptains[side] >= 0 && pack != NULL)
            {
                strncpy(m.coGuests[side], pack, sizeof(m.coGuests[side]) - 1);
                PortGuestStatsFor(co[side]->guest, m.coStats[side], &m.coStatMask[side]);
            }
        }
        m.stadium = stadium >= 0 && stadium < MAX_STADIUMS ? stadium : 0;
        m.optionsSize = PortNetPackOptions(m.options);
        m.coopSide = s_lgVersus ? (int)PORT_NET_COOP_VERSUS : mySide;
        s_holdSide = mySide == 1 ? 1 : 0;
        s_match = MATCH_HOLD;
        fprintf(stderr, "[franchise] %s: waiting at the match start for %s\n", s_lgVersus ? "league" : "co-op",
                s_lgVersus ? "the other club's person" : "the friend");
        fflush(stderr);
        return;
    }
    nlTaskManager::SetNextState(2);
}

void fill_rows(const Career& c, std::vector<int> clubs, PortFranchiseRow* out)
{
    std::sort(clubs.begin(), clubs.end(), [&](int a, int b) {
        const ClubState& x = c.clubs[a];
        const ClubState& y = c.clubs[b];
        if (x.wins != y.wins)
            return x.wins > y.wins;
        const int dx = x.goalsFor - x.goalsAgainst, dy = y.goalsFor - y.goalsAgainst;
        if (dx != dy)
            return dx > dy;
        if (x.goalsFor != y.goalsFor)
            return x.goalsFor > y.goalsFor;
        return a < b;
    });
    for (size_t i = 0; i < clubs.size(); i++)
    {
        const ClubState& s = c.clubs[clubs[i]];
        out[i].club = clubs[i];
        out[i].wins = s.wins;
        out[i].losses = s.losses;
        out[i].goalsFor = s.goalsFor;
        out[i].goalsAgainst = s.goalsAgainst;
        out[i].mine = clubs[i] == c.myClub;
    }
}

std::string captain_name(const std::string& key)
{
    const std::vector<Captain> caps = captains();
    const Captain* cp = find_captain(caps, key);
    return cp != NULL ? cp->name : key;
}

void copy_str(char* dst, size_t n, const std::string& s)
{
    snprintf(dst, n, "%s", s.c_str());
}

}  // namespace

// -------------------------------------------------------------------------------------------- API
namespace
{
// A saved career's card (the careers screen); r76: and the co-op one's id and version.
bool slot_summary(int slot, PortFranchiseSlot* out)
{
    PortFranchiseSlot& o = *out;
    memset(&o, 0, sizeof o);
    Career c;
    if (!load_slot(slot, &c))
        return false;
    o.used = true;
    if (c.myClub >= 0 && c.myClub < (int)s_clubs.size())
    {
        copy_str(o.club, sizeof o.club, s_clubs[c.myClub].place + " " + s_clubs[c.myClub].mascot);
        copy_str(o.clubId, sizeof o.clubId, s_clubs[c.myClub].id);
    }
    o.season = c.season;
    o.week = c.week;
    o.wins = c.clubs[c.myClub].wins;
    o.losses = c.clubs[c.myClub].losses;
    o.careerId = c.seed;
    o.version = c.version;
    return true;
}
}  // namespace

void PortFranchiseSlots(PortFranchiseSlot out[PORT_FR_SLOTS])
{
    read_data();
    for (int i = 0; i < PORT_FR_SLOTS; i++)
        slot_summary(i, &out[i]);
}

bool PortFranchiseCoopFile(PortFranchiseSlot* out)
{
    read_data();
    return slot_summary(PORT_FR_COOP_SLOT, out);
}

bool PortFranchiseLoad(int slot)
{
    read_data();
    if (slot < 0 || (slot >= PORT_FR_SLOTS && slot != PORT_FR_COOP_SLOT && slot != PORT_FR_LEAGUE_SLOT) ||
        (int)s_clubs.size() != PORT_FR_CLUBS)
        return false;
    Career c;
    if (!load_slot(slot, &c))
        return false;
    s_career = c;
    s_active = true;
    fprintf(stderr, "[franchise] career %d: %s, season %d week %d%s%s\n", slot + 1, s_clubs[c.myClub].id.c_str(), c.season,
            c.week, c.coop ? " (co-op)" : "", c.league.empty() ? "" : (" (league " + c.league + ")").c_str());
    fflush(stderr);
    return true;
}

// ------------------------------------------------------------------------------------ r76, co-op
bool PortFranchiseCoopActive() { return s_active && s_career.coop; }
bool PortFranchiseCoopGuest() { return PortFranchiseCoopActive() && PortNetCoopFriend() && !PortNetCoopHost(); }

bool PortFranchiseCoopShare()
{
    if (!PortFranchiseCoopActive())
        return false;
    const std::string text = read_text(career_path(PORT_FR_COOP_SLOT));
    if (text.empty())
        return false;
    PortNetCoopSendCareer(text.data(), (int)text.size(), s_career.seed, s_career.version);
    return true;
}

unsigned PortFranchiseChangeSerial() { return s_changeSerial; }
bool PortFranchiseMatchStarting() { return s_match == MATCH_PENDING || s_match == MATCH_HOLD; }

int PortFranchiseCoopState()
{
    if (!PortNetCoopFriend())
        return 0;
    if (s_coopWaitCareer)
        return 5;
    int has = 0, inHub = 0;
    unsigned id = 0, version = 0;
    if (!PortNetCoopFriendCareer(&has, &id, &version, &inHub))
        return 1;
    if (PortNetCoopHost())
        return 2;
    return inHub ? 4 : 3;
}

int PortFranchiseCoopOpen()
{
    read_data();
    const int state = PortFranchiseCoopState();
    PortFranchiseSlot mine;
    const bool have = PortFranchiseCoopFile(&mine);
    int fHas = 0, fIn = 0;
    unsigned fId = 0, fVersion = 0;
    PortNetCoopFriendCareer(&fHas, &fId, &fVersion, &fIn);
    bool ask = false;
    if (state == 2)
    {
        // this headset runs it: the friend's copy when it's the only one, or the same career saved more
        // times there (they ran it last time); else this one (a new one when neither has any)
        if (fHas && (!have || (fId == mine.careerId && fVersion > mine.version)))
            ask = true;
        else if (!have)
            return 2;
        else if (PortFranchiseLoad(PORT_FR_COOP_SLOT))
        {
            PortFranchiseCoopShare();
            return 1;
        }
        else
            return 0;
    }
    else if (state == 4)
    {
        // the friend runs it and has it open: join (theirs comes over unless this copy is the same)
        if (have && mine.careerId == fId && mine.version == fVersion)
            return PortFranchiseLoad(PORT_FR_COOP_SLOT) ? 1 : 0;
        ask = true;
    }
    if (!ask)
        return 0;
    PortNetCoopRequestCareer();
    s_coopWaitCareer = true;
    s_coopWaitFrames = 0;
    fprintf(stderr, "[franchise] co-op: asked the friend for their career (career %u, version %u)\n", fId, fVersion);
    fflush(stderr);
    return 0;
}

bool PortFranchiseCoopTakeOpened()
{
    const bool was = s_coopOpened;
    s_coopOpened = false;
    return was;
}

namespace
{
// r75: a character select menu pick as a franchise key ("strikers_mario", "pk_voltorb", "toad", "boo"), or
// "" when it is none of the dealt players (an evolved form, an unknown pick).
std::string captain_key_for(int pick)
{
    std::string key;
    if (pick >= PORT_GUEST_ORIGINAL && pick < PORT_GUEST_ORIGINAL + 8)
        key = std::string("strikers_") + kTeamKey[pick - PORT_GUEST_ORIGINAL];
    else if (pick >= 0 && pick < PORT_GUEST_ORIGINAL && PortGuestRosterPack(pick) != NULL)
        key = PortGuestRosterPack(pick);
    const std::vector<Captain> caps = captains();
    const Captain* cap = find_captain(caps, key);
    return cap != NULL && !cap->evolved ? key : std::string();
}

std::string kind_key_for(int pick)
{
    std::string key;
    if (pick >= PORT_GUEST_ORIGINAL_SIDEKICK && pick < PORT_GUEST_ORIGINAL_SIDEKICK + 4)
        key = kSkKey[pick - PORT_GUEST_ORIGINAL_SIDEKICK];
    else if (pick >= 0 && pick < PORT_GUEST_ORIGINAL && PortGuestRosterPack(pick) != NULL)
        key = PortGuestRosterPack(pick);
    const std::vector<Kind> ks = kinds();
    for (size_t k = 0; k < ks.size(); k++)
        if (ks[k].key == key)
            return key;
    return std::string();
}

bool new_career(int slot, int club, const MyPicks& mine);
}  // namespace

bool PortFranchiseCaptainPickable(int pick) { return !captain_key_for(pick).empty(); }

bool PortFranchiseNewPicked(int slot, int club, int captainPick, int backupPick, int sidekickPick)
{
    read_data();
    MyPicks mine;
    mine.captain = captain_key_for(captainPick);
    mine.backup = backupPick < 0 ? std::string() : captain_key_for(backupPick);
    mine.noBackup = backupPick < 0;
    mine.kind = kind_key_for(sidekickPick);
    if (mine.captain.empty() || mine.kind.empty() || mine.backup == mine.captain || (!mine.noBackup && mine.backup.empty()))
    {
        fprintf(stderr, "[franchise] new career: picks %d/%d/%d not usable\n", captainPick, backupPick, sidekickPick);
        fflush(stderr);
        return false;
    }
    fprintf(stderr, "[franchise] your picks: captain %s, backup %s, sidekicks %s\n", mine.captain.c_str(),
            mine.noBackup ? "(none)" : mine.backup.c_str(), mine.kind.c_str());
    fflush(stderr);
    return new_career(slot, club, mine);
}

bool PortFranchiseNew(int slot, int club)
{
    read_data();
    return new_career(slot, club, MyPicks());
}

namespace
{
bool new_career(int slot, int club, const MyPicks& mine)
{
    if (slot < 0 || (slot >= PORT_FR_SLOTS && slot != PORT_FR_COOP_SLOT && slot != PORT_FR_LEAGUE_SLOT) || club < 0 ||
        club >= PORT_FR_CLUBS || (int)s_clubs.size() != PORT_FR_CLUBS)
        return false;
    Career c;
    c.slot = slot;
    c.coop = slot == PORT_FR_COOP_SLOT;  // r76
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)(club * 2654435761u) ^ (unsigned)(slot * 40503u);
    if (seed == 0)
        seed = 12345;
    c.seed = seed;
    c.rng = seed;
    c.season = 1;
    c.week = 1;
    c.myClub = club;
    reset_playoffs(c);
    deal(c, mine);
    init_life(c);  // r71
    deal_life(c);
    make_schedule(c);
    c.headline = "Season 1 kicks off!";
    s_career = c;
    s_active = true;
    save();
    fprintf(stderr, "[franchise] new career %d: %s (seed %u), %d games, captain %s\n", slot + 1, s_clubs[club].id.c_str(),
            seed, (int)c.games.size(), c.clubs[club].captain.c_str());
    fflush(stderr);
    return true;
}
}  // namespace

void PortFranchiseDelete(int slot)
{
    if (slot < 0 || (slot >= PORT_FR_SLOTS && slot != PORT_FR_COOP_SLOT))
        return;
    if (s_active && s_career.slot == slot)
        s_active = false;
    remove(career_path(slot).c_str());
    if (slot == PORT_FR_COOP_SLOT)
        s_coopFileStale = true;  // r76
}

bool PortFranchiseActive() { return s_active; }
void PortFranchiseClose()
{
    if (s_match == MATCH_NONE)
        s_active = false;
}

int PortFranchiseClubCount()
{
    read_data();
    return (int)s_clubs.size();
}
#define CLUB_FIELD(fn, field)                                                                      \
    const char* fn(int club)                                                                       \
    {                                                                                              \
        read_data();                                                                               \
        return club >= 0 && club < (int)s_clubs.size() ? s_clubs[club].field.c_str() : "";        \
    }
CLUB_FIELD(PortFranchiseClubId, id)
CLUB_FIELD(PortFranchiseClubPlace, place)
CLUB_FIELD(PortFranchiseClubMascot, mascot)
CLUB_FIELD(PortFranchiseDivisionName, div)
CLUB_FIELD(PortFranchiseConferenceName, conf)
#undef CLUB_FIELD

const char* PortFranchiseClubName(int club)
{
    static std::string s_names[PORT_FR_CLUBS];
    read_data();
    if (club < 0 || club >= (int)s_clubs.size() || club >= PORT_FR_CLUBS)
        return "";
    if (s_names[club].empty())
        s_names[club] = s_clubs[club].place + " " + s_clubs[club].mascot;
    return s_names[club].c_str();
}

unsigned PortFranchiseClubColor(int club, int which)
{
    read_data();
    if (club < 0 || club >= (int)s_clubs.size() || which < 0 || which > 2)
        return 0x808080;
    return s_clubs[club].col[which];
}

bool PortFranchiseGetView(PortFranchiseView* v)
{
    memset(v, 0, sizeof *v);
    if (!s_active)
        return false;
    Career& c = s_career;
    const ClubState& me = c.clubs[c.myClub];
    v->active = true;
    v->slot = c.slot;
    v->season = c.season;
    v->week = c.week;
    v->myClub = c.myClub;
    v->wins = me.wins;
    v->losses = me.losses;
    v->goalsFor = me.goalsFor;
    v->goalsAgainst = me.goalsAgainst;
    copy_str(v->captain, sizeof v->captain, captain_name(me.captain));
    copy_str(v->captainKey, sizeof v->captainKey, me.captain);
    copy_str(v->backup, sizeof v->backup, me.backup.empty() ? std::string() : captain_name(me.backup));
    copy_str(v->backupKey, sizeof v->backupKey, me.backup);
    v->startsBackup = me.startsBackup && !me.backup.empty();
    {
        const std::vector<Kind> ks = kinds();
        std::string kn = me.kind;
        for (size_t k = 0; k < ks.size(); k++)
            if (ks[k].key == me.kind)
                kn = ks[k].name;
        copy_str(v->kind, sizeof v->kind, kn);
    }
    for (int k = 0; k < 3; k++)
        copy_str(v->sidekicks[k], sizeof v->sidekicks[k], me.names[k]);
    v->nextOpponent = -1;
    if (Game* g = my_game(c))
    {
        v->nextHome = g->home == c.myClub;
        v->nextOpponent = v->nextHome ? g->away : g->home;
        const ClubState& o = c.clubs[v->nextOpponent];
        v->oppWins = o.wins;
        v->oppLosses = o.losses;
        copy_str(v->oppCaptain, sizeof v->oppCaptain, captain_name(starter(o)));
    }
    std::vector<int> div;
    const int base = (c.myClub / 4) * 4;
    for (int i = 0; i < 4; i++)
        div.push_back(base + i);
    fill_rows(c, div, v->division);
    for (int i = 0; i < 4; i++)
        if (v->division[i].mine)
            v->divisionRank = i + 1;
    for (int conf = 0; conf < 2; conf++)
    {
        std::vector<int> all;
        for (int i = 0; i < 16; i++)
            all.push_back(conf * 16 + i);
        fill_rows(c, all, v->conference[conf]);
    }
    v->resultCount = (int)std::min<size_t>(16, c.results.size());
    for (int i = 0; i < v->resultCount; i++)
        copy_str(v->results[i], sizeof v->results[i], c.results[i]);
    copy_str(v->headline, sizeof v->headline, c.headline);

    // r70b: the playoffs (in the regular season, the seeds as the table stands)
    v->phase = c.phase;
    v->round = c.round;
    v->champion = c.champion;
    for (int conf = 0; conf < 2; conf++)
    {
        int seeds[7];
        if (c.phase == PHASE_REGULAR)
            seed_conference(c, conf, seeds);
        for (int k = 0; k < 7; k++)
            v->seeds[conf][k] = c.phase == PHASE_REGULAR ? seeds[k] : c.seeds[conf][k];
    }
    v->seriesCount = (int)std::min<size_t>(15, c.series.size());
    for (int i = 0; i < v->seriesCount; i++)
    {
        const Series& s = c.series[i];
        PortFranchiseSeries& o = v->series[i];
        o.round = s.round;
        o.conf = s.conf;
        o.hi = s.hi;
        o.lo = s.lo;
        o.hiSeed = s.hiSeed;
        o.loSeed = s.loSeed;
        o.winsHi = s.winsHi;
        o.winsLo = s.winsLo;
        o.winner = series_winner(s);
    }
    const int myConf = c.myClub < 16 ? 0 : 1;
    const int shownRound = c.phase == PHASE_DONE ? 4 : c.round;
    copy_str(v->roundName, sizeof v->roundName, c.phase == PHASE_REGULAR ? std::string() : round_name(shownRound, shownRound == 4 ? -1 : myConf));
    {
        static const char* const kLogo[5] = {"franchise_league", "playoff_kickoff_round", "semifinals", "conference_finals",
                                             "playoff_super_strike_series"};
        std::string key = kLogo[shownRound >= 0 && shownRound <= 4 ? shownRound : 0];
        if (shownRound == 2 || shownRound == 3)
            key = std::string("playoff_") + (myConf == 0 ? "crown_" : "star_") + key;
        copy_str(v->roundLogo, sizeof v->roundLogo, key);
    }
    const Game* mg = my_game(c);
    v->canSimDay = s_match == MATCH_NONE && c.phase == PHASE_PLAYOFFS && mg == NULL;
    if (mg != NULL && mg->series >= 0 && mg->series < (int)c.series.size())
    {
        const Series& s = c.series[mg->series];
        v->myGameNo = s.winsHi + s.winsLo + 1;
        v->mySeriesMe = s.hi == c.myClub ? s.winsHi : s.winsLo;
        v->mySeriesThem = s.hi == c.myClub ? s.winsLo : s.winsHi;
    }
    if (mg == NULL)
    {
        std::string st;
        if (c.phase == PHASE_DONE)
            st = c.champion == c.myClub ? "CHAMPIONS! YOU WON THE SUPER STRIKE SERIES" : upper(club_name(c.champion)) + " ARE CHAMPIONS";
        else if (c.phase == PHASE_PLAYOFFS)
        {
            int lostIn = 0, wonThis = 0;
            for (size_t i = 0; i < c.series.size(); i++)
            {
                const Series& s = c.series[i];
                if (s.hi != c.myClub && s.lo != c.myClub)
                    continue;
                const int w = series_winner(s);
                if (w >= 0 && w != c.myClub)
                    lostIn = s.round;
                if (w == c.myClub && s.round == c.round)
                    wonThis = 1;
            }
            if (seed_of(c, c.myClub) == 0)
                st = "YOU MISSED THE PLAYOFFS";
            else if (lostIn > 0)
                st = "OUT: KNOCKED OUT IN THE " + upper(round_name(lostIn, lostIn == 4 ? -1 : myConf));
            else if (c.round == 1 && seed_of(c, c.myClub) == 1)
                st = "FIRST-ROUND BYE (THE #1 SEED RESTS)";
            else if (wonThis)
                st = "SERIES WON: THE OTHER SERIES PLAY ON";
        }
        copy_str(v->status, sizeof v->status, st);
    }
    if (in_league() && mg != NULL && mg->played)
    {
        // r100: yours is in; the league goes on once every person's game of it is (or its owner sims the rest)
        v->nextOpponent = -1;
        copy_str(v->status, sizeof v->status, "PLAYED: WAITING ON THE LEAGUE");
    }
    for (size_t i = 0; i < c.champions.size(); i++)
        if (c.champions[i].second == c.myClub)
            v->titles++;
    // r70c
    v->captainLevel = me.cap.level;
    v->backupLevel = me.bk.level;
    v->sidekickLevel = me.side.level;
    v->captainXp = me.cap.xp;
    v->backupXp = me.bk.xp;
    v->sidekickXp = me.side.xp;
    v->captainShiny = me.cap.shiny;
    v->backupShiny = me.bk.shiny;
    v->newsCount = (int)std::min<size_t>(8, c.news.size());
    for (int i = 0; i < v->newsCount; i++)
        copy_str(v->news[i], sizeof v->news[i], c.news[i]);
    PortFranchiseEvolution e;
    v->evolutionPending = PortFranchisePendingEvolution(&e);

    // r71: A-Life
    struct Who
    {
        std::string name;
        int club, goals, form;
    };
    std::vector<Who> all;
    const std::vector<Captain> caps = captains();
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        const ClubState& s = c.clubs[i];
        const Captain* cp = find_captain(caps, s.captain);
        all.push_back(Who{cp != NULL ? cp->name : s.captain, i, s.capSt.seasonGoals, s.capSt.form});
        if (!s.backup.empty())
        {
            const Captain* bp = find_captain(caps, s.backup);
            all.push_back(Who{bp != NULL ? bp->name : s.backup, i, s.bkSt.seasonGoals, s.bkSt.form});
        }
        for (int k = 0; k < 3; k++)
            all.push_back(Who{s.names[k], i, s.sk[k].seasonGoals, s.sk[k].form});
    }
    auto fill = [&](PortFranchiseView::Leader* out, int max, int* count, bool (*keep)(const Who&),
                    bool (*before)(const Who&, const Who&)) {
        std::vector<Who> l;
        for (size_t i = 0; i < all.size(); i++)
            if (keep(all[i]))
                l.push_back(all[i]);
        std::stable_sort(l.begin(), l.end(), before);
        *count = (int)std::min<size_t>((size_t)max, l.size());
        for (int i = 0; i < *count; i++)
        {
            copy_str(out[i].name, sizeof out[i].name, l[i].name);
            out[i].club = l[i].club;
            out[i].goals = l[i].goals;
            out[i].form = l[i].form;
        }
    };
    fill(v->leaders, 10, &v->leaderCount, [](const Who& w) { return w.goals > 0; },
         [](const Who& a, const Who& b) { return a.goals > b.goals; });
    fill(v->hot, 5, &v->hotCount, [](const Who& w) { return w.form >= 2; },
         [](const Who& a, const Who& b) { return a.form > b.form; });
    fill(v->cold, 5, &v->coldCount, [](const Who& w) { return w.form <= -2; },
         [](const Who& a, const Who& b) { return a.form < b.form; });
    v->feedCount = (int)std::min<size_t>(8, c.feed.size());
    for (int i = 0; i < v->feedCount; i++)
        copy_str(v->feed[i], sizeof v->feed[i], c.feed[i]);
    v->difficulty = c.difficulty;  // r97
    v->awardCount = (int)std::min<size_t>(8, c.awards.size());
    for (int i = 0; i < v->awardCount; i++)
    {
        copy_str(v->awards[i], sizeof v->awards[i], c.awards[i]);
        copy_str(v->awardKeys[i], sizeof v->awardKeys[i], i < (int)c.awardKeys.size() ? c.awardKeys[i] : std::string());
    }
    v->historyCount = (int)std::min<size_t>(16, c.history.size());
    for (int i = 0; i < v->historyCount; i++)
        copy_str(v->history[i], sizeof v->history[i], c.history[i]);
    v->captainGoals = me.capSt.seasonGoals;
    v->backupGoals = me.bkSt.seasonGoals;
    v->captainForm = me.capSt.form;
    v->backupForm = me.bkSt.form;
    for (int k = 0; k < 3; k++)
    {
        v->sidekickGoals[k] = me.sk[k].seasonGoals;
        v->sidekickAge[k] = me.skAge[k];
    }
    v->tradeDeadlinePassed = c.phase != PHASE_REGULAR || c.week > 12;

    // r72: nemeses, the active ones first, hottest first
    std::vector<int> order;
    for (size_t i = 0; i < c.nems.size(); i++)
        if (c.nems[i].active || !c.nems[i].log.empty())
            order.push_back((int)i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        if (c.nems[a].active != c.nems[b].active)
            return c.nems[a].active;
        return c.nems[a].heat > c.nems[b].heat;
    });
    v->nemCount = (int)std::min<size_t>(8, order.size());
    v->nextNemesis = -1;
    std::string nextKey;
    if (const Game* ng = my_game(c))
    {
        const ClubState& os = c.clubs[ng->home == c.myClub ? ng->away : ng->home];
        nextKey = (os.startsBackup && !os.backup.empty()) ? os.backup : os.captain;
    }
    for (int i = 0; i < v->nemCount; i++)
    {
        const Nemesis& n = c.nems[order[i]];
        PortFranchiseView::Nem& o = v->nems[i];
        copy_str(o.name, sizeof o.name, n.name);
        copy_str(o.title, sizeof o.title, n.title >= 0 && n.title < T_COUNT ? kTitles[n.title] : "");
        copy_str(o.key, sizeof o.key, n.key.compare(0, 3, "sk#") == 0 ? std::string() : n.key);
        o.club = n.club;
        o.heat = n.heat;
        o.rank = n.rank;
        o.beatYou = n.beatYou;
        o.lostToYou = n.lostToYou;
        o.active = n.active;
        o.teammate = n.teammate;
        o.traitCount = 0;
        for (int b = 0; b < kTraitCount && o.traitCount < 3; b++)
            if ((n.traits >> b) & 1)
                copy_str(o.traits[o.traitCount++], sizeof o.traits[0], kTraits[b]);
        copy_str(o.last, sizeof o.last, n.log.empty() ? std::string() : n.log[0]);
        if (n.active && !n.teammate && n.key == nextKey)
            v->nextNemesis = i;
    }
    return true;
}

bool PortFranchiseCanPlay()
{
    const Game* g = s_active ? my_game(s_career) : NULL;  // (r100: an online league's stays in its week once played)
    return s_active && s_match == MATCH_NONE && s_onMainMenu && g != NULL && !g->played;
}

bool PortFranchiseSimDay(bool toYourNext)
{
    if (!s_active || s_match != MATCH_NONE || s_career.phase != PHASE_PLAYOFFS || my_game(s_career) != NULL ||
        coop_blocked())
        return false;
    if (in_league())  // r100: an online league's playoff days go on once every person's game of them is in
        return league_do("advance", []() {
            if (!league_advance(s_career, false))
                return false;
            save();
            return true;
        });
    for (int day = 0; day < 40; day++)
    {
        finish_week(s_career);
        if (!toYourNext || s_career.phase != PHASE_PLAYOFFS || my_game(s_career) != NULL)
            break;
    }
    save();
    fprintf(stderr, "[franchise] simmed to day %d (round %d, phase %d)\n", s_career.week - PORT_FR_WEEKS,
            s_career.round, s_career.phase);
    fflush(stderr);
    return true;
}

// One of your captains ready to evolve: postponed ones too or not, either slot or just one (0 captain,
// 1 backup).
static bool find_evolution(PortFranchiseEvolution* out, bool postponedToo, int onlySlot)
{
    memset(out, 0, sizeof *out);
    if (!s_active || s_match != MATCH_NONE || coop_blocked())  // r76: co-op: the host's headset asks
        return false;
    const ClubState& s = s_career.clubs[s_career.myClub];
    for (int b = 0; b < 2; b++)
    {
        if (onlySlot >= 0 && b != onlySlot)
            continue;
        const std::string& key = b ? s.backup : s.captain;
        const Prog& p = b ? s.bk : s.cap;
        if (key.empty() || (!postponedToo && p.declinedAt == p.level))
            continue;
        std::string to[2];
        const int n = evolutions_for(key, p, to);
        if (n == 0)
            continue;
        out->valid = true;
        out->backup = b != 0;
        out->level = p.level;
        out->shiny = p.shiny;
        out->postponed = p.declinedAt == p.level;
        copy_str(out->fromKey, sizeof out->fromKey, key);
        copy_str(out->fromName, sizeof out->fromName, name_of(key));
        out->options = n;
        for (int k = 0; k < n; k++)
        {
            copy_str(out->toKey[k], sizeof out->toKey[k], to[k]);
            copy_str(out->toName[k], sizeof out->toName[k], name_of(to[k]));
        }
        return true;
    }
    return false;
}

bool PortFranchisePendingEvolution(PortFranchiseEvolution* out) { return find_evolution(out, false, -1); }

bool PortFranchiseReadyEvolution(PortFranchiseEvolution* out) { return find_evolution(out, true, -1); }

bool PortFranchiseEvolve(int choice, bool* shiny, bool* newShiny)
{
    *shiny = *newShiny = false;
    PortFranchiseEvolution e;
    if (!PortFranchisePendingEvolution(&e))
        return false;
    return PortFranchiseEvolveSlot(e.backup, choice, shiny, newShiny);
}

static bool evolve_slot_impl(bool backup, int choice, bool* shiny, bool* newShiny);

bool PortFranchiseEvolveSlot(bool backup, int choice, bool* shiny, bool* newShiny)
{
    *shiny = *newShiny = false;
    if (!in_league())
        return evolve_slot_impl(backup, choice, shiny, newShiny);
    // r100: an online league's: an action on the league's career (kept when it's still due on a newer copy)
    static bool s_evolved, s_shiny, s_newShiny;
    s_evolved = s_shiny = s_newShiny = false;
    league_do(choice < 0 ? "postpone" : "evolve", [backup, choice]() {
        PortFranchiseEvolution e;
        if (!find_evolution(&e, true, backup ? 1 : 0))
            return false;  // no longer due (evolved already)
        s_evolved = evolve_slot_impl(backup, choice, &s_shiny, &s_newShiny);
        return true;
    });
    *shiny = s_shiny;
    *newShiny = s_newShiny;
    return s_evolved;
}

static bool evolve_slot_impl(bool backup, int choice, bool* shiny, bool* newShiny)
{
    *shiny = *newShiny = false;
    PortFranchiseEvolution e;
    if (!find_evolution(&e, true, backup ? 1 : 0))
        return false;
    Career& c = s_career;
    ClubState& s = c.clubs[c.myClub];
    Prog& p = e.backup ? s.bk : s.cap;
    if (choice < 0 || choice >= e.options)
    {
        // r95: postponed: not asked again until its next level; the Evo Watch page's A evolves it any time
        p.declinedAt = p.level;
        c.headline = std::string(e.fromName) + " will wait. Evolve it any time: Evo Watch page, A.";
        save();
        fprintf(stderr, "[franchise] evolution postponed: %s at Lv %d\n", e.fromKey, p.level);
        fflush(stderr);
        return false;
    }
    const bool was = p.shiny;
    do_evolve(c, c.myClub, e.backup, e.toKey[choice]);
    *shiny = p.shiny;
    *newShiny = p.shiny && !was;
    c.headline = std::string("Your ") + e.fromName + " evolved into " + e.toName[choice] + (*newShiny ? " - and it's SHINY!" : "!");
    if (PortGuestUnlock(e.toKey[choice]))
        c.headline += " Unlocked for every mode.";
    save();
    return true;
}

bool PortFranchiseSimMine(bool toPlayoffs)
{
    if (!s_active || s_match != MATCH_NONE || my_game(s_career) == NULL || coop_blocked())
        return false;
    Career& c = s_career;
    if (in_league())
    {
        // r100: an online league's: your game only (not to the playoffs: the others play theirs), and not against a
        // person's club (that one is played)
        Game* mg = my_game(c);
        if (toPlayoffs || mg == NULL || mg->played || is_human(c, mg->home == c.myClub ? mg->away : mg->home))
            return false;
        const int gi = (int)(mg - &c.games[0]);
        const int me = c.myClub;
        return league_do("sim", [gi, me]() {
            Career& lc = s_career;
            if (gi < 0 || gi >= (int)lc.games.size())
                return false;
            Game& g = lc.games[gi];
            if (g.played || g.week != lc.week)
                return false;
            sim_game(lc, g);
            const bool won = (g.home == me) == (g.homeGoals > g.awayGoals);
            char buf[160];
            snprintf(buf, sizeof buf, "%s simmed: %s %s %d - %d", s_clubs[me].place.c_str(), won ? "WIN vs" : "LOSS vs",
                     s_clubs[g.home == me ? g.away : g.home].place.c_str(), g.home == me ? g.homeGoals : g.awayGoals,
                     g.home == me ? g.awayGoals : g.homeGoals);
            lc.headline = buf;
            league_advance(lc, false);
            save();
            return true;
        });
    }
    for (int week = 0; week < 40; week++)
    {
        Game* g = my_game(c);
        if (g != NULL && !g->played)
        {
            sim_game(c, *g);
            const bool won = (g->home == c.myClub) == (g->homeGoals > g->awayGoals);
            const int mine = g->home == c.myClub ? g->homeGoals : g->awayGoals;
            const int theirs = g->home == c.myClub ? g->awayGoals : g->homeGoals;
            char buf[160];
            snprintf(buf, sizeof buf, "Simmed: %s %s %d - %d%s", won ? "WIN vs" : "LOSS vs",
                     s_clubs[g->home == c.myClub ? g->away : g->home].place.c_str(), mine, theirs, g->overtime ? " (OT)" : "");
            c.headline = buf;
        }
        const std::string before = c.headline;
        finish_week(c);
        if (c.headline != before)
            c.headline = before + ". " + c.headline;
        if (!toPlayoffs || c.phase != PHASE_REGULAR)
            break;
    }
    save();
    fprintf(stderr, "[franchise] simmed your match%s: week %d, phase %d\n", toPlayoffs ? "es to the playoffs" : "", c.week,
            c.phase);
    fflush(stderr);
    return true;
}

bool PortFranchiseNextSeason()
{
    if (!s_active || s_match != MATCH_NONE || s_career.phase != PHASE_DONE || coop_blocked())
        return false;
    return league_do("season", []() {  // r100: an online league's, an action (whoever gets there first)
        if (s_career.phase != PHASE_DONE)
            return false;
        next_season(s_career);
        save();
        fprintf(stderr, "[franchise] season %d\n", s_career.season);
        fflush(stderr);
        return true;
    });
}

bool PortFranchiseLeagueSimRest()
{
    // r100: the league's owner moves the week on: the games of it still to play (the people's too) worked out
    unsigned serial = 0;
    if (!in_league() || s_match != MATCH_NONE || !PortLeagueGetState(&serial).owner)
        return false;
    return league_do("simrest", []() {
        if (s_career.phase == PHASE_DONE)
            return false;
        const int week = s_career.week;
        league_advance(s_career, true);
        save();
        fprintf(stderr, "[franchise] league: week %d's games worked out (the owner)\n", week);
        fflush(stderr);
        return true;
    });
}

bool PortFranchisePlayoffMatch(int* round, int* conf, int* gameNo, int winsBefore[2], const char** logoKey)
{
    if (s_match != MATCH_PLAYING || !s_active || s_matchGame < 0 || s_matchGame >= (int)s_career.games.size())
        return false;
    const Game& g = s_career.games[s_matchGame];
    if (g.series < 0 || g.series >= (int)s_career.series.size())
        return false;
    const Series& s = s_career.series[g.series];
    static std::string s_logo;
    *round = s.round;
    *conf = s.conf;
    *gameNo = s.winsHi + s.winsLo + 1;
    winsBefore[0] = g.home == s.hi ? s.winsHi : s.winsLo;
    winsBefore[1] = g.home == s.hi ? s.winsLo : s.winsHi;
    switch (s.round)
    {
    case 1:
        s_logo = "playoff_kickoff_round";
        break;
    case 2:
        s_logo = s.conf == 0 ? "playoff_crown_semifinals" : "playoff_star_semifinals";
        break;
    case 3:
        s_logo = s.conf == 0 ? "playoff_crown_conference_finals" : "playoff_star_conference_finals";
        break;
    default:
        s_logo = "playoff_super_strike_series";
        break;
    }
    *logoKey = s_logo.c_str();
    return true;
}

bool PortFranchisePlayNext()
{
    if (!PortFranchiseCanPlay() || coop_blocked())
        return false;
    if (in_league())
    {
        // r100: against a person's club it's played online (both ready: the league's go), not here
        const Game* mg = my_game(s_career);
        if (mg == NULL || is_human(s_career, mg->home == s_career.myClub ? mg->away : mg->home))
            return false;
    }
    s_match = MATCH_PENDING;
    if (s_career.coop)  // r76: the friend's headset starts the same match (its copy of the career)
        PortNetCoopSendPlay(s_career.seed, s_career.version, -1);
    return true;
}

void PortFranchiseSwapStarter()
{
    if (!s_active || coop_blocked())
        return;
    ClubState& me = s_career.clubs[s_career.myClub];
    if (me.backup.empty())
        return;
    // r100: who starts, as an action (an online league's: set to this, again on a newer copy)
    const bool want = !me.startsBackup;
    const int club = s_career.myClub;
    league_do("starter", [want, club]() {
        ClubState& s = s_career.clubs[club];
        if (s.backup.empty())
            return false;
        s.startsBackup = want;
        save();
        return true;
    });
}

bool PortFranchiseOnMainMenu() { return s_onMainMenu; }
bool PortFranchiseMatchActive() { return s_match == MATCH_PLAYING; }
// r86: a franchise match (yours, a co-op one, a CPU game watched) is 4 minutes, whatever the game's
// options say (personal_bonus' kMatchSeconds); also while it is still starting (a co-op guest's load).
int PortFranchiseMatchSeconds()
{
    return (s_match == MATCH_PLAYING || s_match == MATCH_PENDING || s_match == MATCH_HOLD) ? 240 : 0;
}

namespace
{
// r77 (Lance: XP "as they score, or assist or win"): a played match's own scorers. On top of the club's
// (award_xp: the win / loss and its goals, for everyone), each side's starting captain gets 10 a goal of
// its own (a Super Strike counts 2) and 5 an assist, its sidekicks (one level for the three) the same for
// theirs; playoff and nemesis bonuses as the club's. Simmed games stay the club's alone.
// r86 (Lance: "make the minute length 4 minutes ... add a weight that will make up for the lost time in
// terms of XP"): a franchise match is 4 minutes (GameTweaks, PortFranchiseMatchSeconds), a fifth fewer
// goals than the game's usual 5; what a played match earns from its goals and assists is worth 5/4 of
// it, so a match earns about what a 5-minute one did. Wins / losses and simmed games are unchanged.
constexpr int kMatchSeconds = 240, kUsualMatchSeconds = 300;
constexpr float kPlayedWeight = static_cast<float>(kUsualMatchSeconds) / static_cast<float>(kMatchSeconds);

int personal_bonus(const Career& c, const Game& g, int goals, int assists)
{
    int xp = static_cast<int>(lroundf((10 * goals + 5 * assists) * kPlayedWeight));
    if (g.series >= 0)
        xp += xp / 2;
    if (nemesis_match(c, g))
        xp += xp / 4;
    return xp;
}

// r86: the club's goals in a played match, made up to 5/4 like the players' (award_xp gives 8 a goal):
// what each of its players gets on top.
int played_goal_bonus(const Career& c, const Game& g, int clubGoals)
{
    int xp = static_cast<int>(lroundf(8.0f * clubGoals * (kPlayedWeight - 1.0f)));
    if (g.series >= 0)
        xp += xp / 2;
    if (nemesis_match(c, g))
        xp += xp / 4;
    return xp;
}

void match_assists(int side, int out[4])
{
    memset(out, 0, sizeof(int) * 4);
    StatsTracker* st = nlSingleton<StatsTracker>::s_pInstance;
    if (st == NULL || side < 0 || side > 1)
        return;
    for (int k = 0; k < 4; k++)
        out[k] = st->mCurrentPlayerStats[side][k].mNumAssists;
}

// r100: the match just played as it ended (PlayedResult), from this frame's tallies and the game's stats
PlayedResult capture_played(int game, int hg, int ag, bool ot)
{
    PlayedResult r;
    memset(&r, 0, sizeof r);
    r.game = game;
    r.hg = hg;
    r.ag = ag;
    r.ot = ot;
    memcpy(r.goals, s_matchGoals, sizeof r.goals);
    memcpy(r.ss, s_matchSS, sizeof r.ss);
    memcpy(r.capHits, s_matchCapHits, sizeof r.capHits);
    memcpy(r.anyHits, s_matchAnyHits, sizeof r.anyHits);
    for (int side = 0; side < 2; side++)
    {
        match_assists(side, r.assists[side]);
        r.co[side] = PortSecondCaptain(side) >= 0;
    }
    return r;
}

void personal_xp_from(Career& c, const Game& g, const PlayedResult& r);

void personal_xp(Career& c, const Game& g)
{
    const PlayedResult r = capture_played(-1, g.homeGoals, g.awayGoals, g.overtime);
    personal_xp_from(c, g, r);
}

// r100: the same from a played match's tallies (an online league's result, done again on a newer copy)
void personal_xp_from(Career& c, const Game& g, const PlayedResult& r)
{
    for (int side = 0; side < 2; side++)
    {
        ClubState& s = c.clubs[side == 0 ? g.home : g.away];
        const int* assists = r.assists[side];
        // r78: the co-captain (slot 1, scorer 4) when this side fielded one
        const bool co = r.co[side];
        const int capXp = personal_bonus(c, g, r.goals[side][0], assists[0]);
        const int coXp = co ? personal_bonus(c, g, r.goals[side][4], assists[1]) : 0;
        const int skXp = personal_bonus(c, g, r.goals[side][1] + r.goals[side][2] + r.goals[side][3],
                                        (co ? 0 : assists[1]) + assists[2] + assists[3]);
        const bool bs = s.startsBackup && !s.backup.empty();
        // r86: the club's goals made up for the 4-minute match, for all its players as award_xp's
        const int clubGoals = side == 0 ? g.homeGoals : g.awayGoals;
        const int teamXp = played_goal_bonus(c, g, clubGoals);
        gain(bs ? s.bk : s.cap, capXp + teamXp);
        if (!s.backup.empty())
            gain(bs ? s.cap : s.bk, (co ? coXp : 0) + teamXp);
        gain(s.side, skXp + teamXp);
    }
}

// The next form a captain evolves into and its level (0: none, its last form).
int next_evolution(const std::string& key, std::string* to)
{
    int best = 0;
    for (size_t i = 0; i < s_evos.size(); i++)
        if (s_evos[i].from == key && (best == 0 || s_evos[i].level < best))
        {
            best = s_evos[i].level;
            if (to != NULL)
                *to = s_evos[i].to;
        }
    return best;
}

// r76: a co-op career from the friend: written as this headset's co-op career (one that differs and is
// another career is kept beside it as coop-<id>.txt), and loaded when it is the one open here or the
// one this headset waited for. During a match it waits for the match's end.
void take_coop_career(const char* text, size_t size, unsigned id, unsigned version)
{
    const std::string path = career_path(PORT_FR_COOP_SLOT);
    const std::string mine = read_text(path);
    if (mine.size() == size && memcmp(mine.data(), text, size) == 0)
    {
        fprintf(stderr, "[franchise] co-op: the friend's career (version %u) is the same as this one\n", version);
    }
    else
    {
        if (s_coopFileStale)
        {
            s_coopFileHas = slot_summary(PORT_FR_COOP_SLOT, &s_coopFile);
            s_coopFileStale = false;
        }
        if (s_coopFileHas && s_coopFile.careerId != id)
        {
            char keep[48];
            snprintf(keep, sizeof keep, "/franchise/coop-%u.txt", s_coopFile.careerId);
            rename(path.c_str(), (home_dir() + keep).c_str());
            fprintf(stderr, "[franchise] co-op: this headset's other co-op career kept as %s\n", keep);
        }
        if (!write_text(path, text, size))
        {
            fprintf(stderr, "[franchise] co-op: can't write the friend's career\n");
            fflush(stderr);
            return;
        }
        s_coopFileStale = true;
        fprintf(stderr, "[franchise] co-op: the friend's career saved here (career %u, version %u, %u bytes)%s\n", id,
                version, (unsigned)size, mine.empty() ? "" : ", it differed from this headset's");
    }
    if ((s_active && s_career.coop) || s_coopWaitCareer)
    {
        Career c;
        if (load_slot(PORT_FR_COOP_SLOT, &c))
        {
            s_career = c;
            s_active = true;
            ++s_changeSerial;
            if (s_coopWaitCareer)
            {
                s_coopWaitCareer = false;
                s_coopOpened = true;
            }
        }
    }
    fflush(stderr);
}

// r76, every frame: what this headset has for the friend, the careers coming in, the friend's requests
// and the host's match starts.
void coop_frame()
{
    // (nothing read before there's a friend in a room or a career open: a solo boot stays as it was)
    if (s_coopFileStale && (PortNetCoopFriend() || s_active))
    {
        read_data();
        s_coopFileHas = slot_summary(PORT_FR_COOP_SLOT, &s_coopFile);
        s_coopFileStale = false;
    }
    const bool open = s_active && s_career.coop;
    PortNetCoopSetMine(s_coopFileHas || open ? 1 : 0, open ? s_career.seed : s_coopFile.careerId,
                       open ? s_career.version : s_coopFile.version, open ? 1 : 0);
    if (PortNetCoopTakeRequest() && s_coopFileHas)
    {
        const std::string text = read_text(career_path(PORT_FR_COOP_SLOT));
        if (!text.empty())
            PortNetCoopSendCareer(text.data(), (int)text.size(), s_coopFile.careerId, s_coopFile.version);
    }
    int size = 0;
    unsigned id = 0, version = 0;
    if (const char* text = PortNetCoopTakeCareer(&size, &id, &version))
    {
        if (s_match == MATCH_NONE)
            take_coop_career(text, (size_t)size, id, version);
        else
        {
            s_coopLater.assign(text, (size_t)size);
            s_coopLaterId = id;
            s_coopLaterVersion = version;
        }
    }
    if (!s_coopLater.empty() && s_match == MATCH_NONE)
    {
        take_coop_career(s_coopLater.data(), s_coopLater.size(), s_coopLaterId, s_coopLaterVersion);
        s_coopLater.clear();
    }
    if (s_coopWaitCareer && ++s_coopWaitFrames > 72 * 20)
    {
        s_coopWaitCareer = false;
        fprintf(stderr, "[franchise] co-op: the friend's career never came\n");
        fflush(stderr);
    }
    // the friend (running the career) started a match: the same here once this copy is theirs
    unsigned pid = 0, pversion = 0;
    int pwatch = -1;
    if (PortNetCoopTakePlay(&pid, &pversion, &pwatch))
    {
        s_coopPlayWaiting = true;
        s_coopPlayId = pid;
        s_coopPlayVersion = pversion;
        s_coopPlayWatch = pwatch;
        s_coopPlayFrames = 0;
    }
    if (s_coopPlayWaiting)
    {
        const bool same = s_active && s_career.coop && s_career.seed == s_coopPlayId && s_career.version == s_coopPlayVersion;
        if (s_match != MATCH_NONE || ++s_coopPlayFrames > 72 * 20)
        {
            s_coopPlayWaiting = false;
            if (s_match == MATCH_NONE)
            {
                fprintf(stderr, "[franchise] co-op: the friend's match start not taken (career %u v%u here %s v%u)\n",
                        s_coopPlayId, s_coopPlayVersion, open ? "open" : "not open", open ? s_career.version : 0u);
                fflush(stderr);
            }
        }
        else if (same && s_onMainMenu)
        {
            s_coopPlayWaiting = false;
            s_watchGame = s_coopPlayWatch >= 0 && s_coopPlayWatch < (int)s_career.games.size() ? s_coopPlayWatch : -1;
            s_match = MATCH_PENDING;
            fprintf(stderr, "[franchise] co-op: the friend started the %s: starting it here too\n",
                    s_watchGame >= 0 ? "game to watch" : "match");
            fflush(stderr);
        }
    }
}
}  // namespace

// ------------------------------------------------------------------------------------ r100: online leagues
// (Lance: "UP TO 8 player online ... leagues"; ONLINE-LEAGUE.md, include/port/xr_league.h.) The league's career
// lives on the room server with a version; this headset keeps a copy (franchise/league.txt). Anything that
// changes it is an action (league_do): done here at once, then uploaded on the version this copy is; when
// someone else's upload landed first, the newest copy comes down and this headset's actions since are done again
// on it (each checks it still applies: a game already in, an evolution no longer due, are left out).
namespace
{
struct LeagueOp
{
    std::string name;
    std::function<bool()> fn;
};
std::deque<LeagueOp> s_lgOps;  // done here, not yet in the league's copy (oldest first)
int s_lgInFlight = 0;          // actions in the upload awaiting its answer
bool s_lgFetching = false;     // the newest copy asked for
bool s_lgReplaying = false;
std::string s_lgCode;          // the league connected to ("" none)
int s_lgLink = 0;
int s_lgLeaveFrames = 0;       // left: the link kept up this many frames more (the leave goes out)
unsigned s_lgSerialSeen = 0;   // the link's state as last seen (a change: the hub redraws)
// Joining a league by its code (the careers page): its state (PortFranchiseLeagueLobby.state), its career as read
// (each club's captain for the club picker), the club claimed.
struct LeagueJoin
{
    int state = 0;
    std::string code;
    int errorsSeen = 0;
    std::string error;
    Career preview;
    bool havePreview = false;
    int club = -1;
    bool asked = false;       // the career asked for since the club came through
    bool opened = false;      // taken by PortFranchiseLeagueTakeOpened
};
LeagueJoin s_lgJoin;

bool in_league() { return s_active && !s_career.league.empty(); }

std::string league_text(const Career& c)
{
    const std::string path = home_dir() + "/franchise/league-out.tmp";
    if (!write_career(c, path))
        return std::string();
    return read_text(path);
}

bool league_from_text(const std::string& text, Career* out)
{
    const std::string path = home_dir() + "/franchise/league-in.tmp";
    return write_text(path, text.data(), text.size()) && load_path(path, PORT_FR_LEAGUE_SLOT, out);
}

bool league_do(const char* name, std::function<bool()> fn)
{
    if (!in_league() || s_lgReplaying)
        return fn();
    if (!fn())
        return false;
    LeagueOp op;
    op.name = name;
    op.fn = fn;
    s_lgOps.push_back(op);
    fprintf(stderr, "[franchise] league: %s (%d to send)\n", name, (int)s_lgOps.size());
    fflush(stderr);
    return true;
}

// Every person's game of this week is in.
bool league_week_ready(const Career& c)
{
    for (size_t i = 0; i < c.games.size(); i++)
    {
        const Game& g = c.games[i];
        if (g.week == c.week && !g.played && (is_human(c, g.home) || is_human(c, g.away)))
            return false;
    }
    return true;
}

// The week (playoff day) goes on while every person's game of it is in; `force` (the owner's SIM THE REST):
// this one goes on anyway, its people's games worked out.
bool league_advance(Career& c, bool force)
{
    bool moved = false;
    for (int guard = 0; guard < 60 && c.phase != PHASE_DONE; guard++)
    {
        if (!league_week_ready(c) && !(force && guard == 0))
            break;
        finish_week(c);
        moved = true;
    }
    return moved;
}

// A played game into the league's career: its result and the players' XP, its A-Life and every person's beef in it
// (both, a game between two people); the news week turns over with the first of a new week. False: it was in
// already (the other person's headset sent it).
bool league_apply_played(Career& c, const PlayedResult& r)
{
    if (r.game < 0 || r.game >= (int)c.games.size())
        return false;
    Game& g = c.games[r.game];
    if (g.played || g.week != c.week)
        return false;
    if (c.newsWeek != c.week)
    {
        c.news.clear();
        c.feed.clear();
        c.newsWeek = c.week;
    }
    g.homeGoals = r.hg;
    g.awayGoals = r.ag;
    if (g.homeGoals == g.awayGoals)
        g.homeGoals++;
    g.overtime = r.ot;
    apply_result(c, g);
    personal_xp_from(c, g, r);
    life_game(c, g, r.goals);
    for (int side = 0; side < 2; side++)
    {
        const int club = side == 0 ? g.home : g.away;
        if (!is_human(c, club))
            continue;
        s_nemMe = club;
        nemesis_game(c, g, r.goals, r.ss, r.capHits, r.anyHits);
        teammates_week(c, (g.home == club) != (g.homeGoals > g.awayGoals));
        s_nemMe = -1;
    }
    c.lifeDone.push_back(r.game);
    return true;
}

// Once a frame: connected to the loaded league; its members' clubs; this copy kept the newest, this headset's
// actions uploaded.
void league_frame()
{
    if (!in_league())
    {
        if (s_lgLeaveFrames > 0)  // (the leave goes out first)
        {
            --s_lgLeaveFrames;
            return;
        }
        if (s_lgJoin.state != 0 && s_lgJoin.state != 5)  // a league being joined owns the link
            return;
        if (!s_lgCode.empty())
        {
            PortLeagueConnect(std::string());
            s_lgCode.clear();
            s_lgOps.clear();
            s_lgInFlight = 0;
            s_lgFetching = false;
        }
        return;
    }
    Career& c = s_career;
    if (s_lgCode != c.league)
    {
        s_lgCode = c.league;
        PortLeagueConnect(c.league);
        s_lgInFlight = 0;
        s_lgFetching = false;
    }
    unsigned serial = 0;
    const PortLeagueState st = PortLeagueGetState(&serial);
    if (serial != s_lgSerialSeen)  // (who's online, ready, done: the hub draws it again)
    {
        s_lgSerialSeen = serial;
        ++s_changeSerial;
    }
    if (st.link != s_lgLink)
    {
        // a new link: whatever was asked on the old one may never be answered
        s_lgLink = st.link;
        s_lgInFlight = 0;
        s_lgFetching = false;
        ++s_changeSerial;
    }
    if (st.link != 2 || !st.exists)
        return;
    if (st.myClub >= 0 && st.myClub != c.myClub)
    {
        c.myClub = st.myClub;
        ++s_changeSerial;
    }
    std::vector<int> hs;
    for (size_t i = 0; i < st.members.size(); i++)
        if (st.members[i].club >= 0 && st.members[i].club < PORT_FR_CLUBS)
            hs.push_back(st.members[i].club);
    std::sort(hs.begin(), hs.end());
    if (hs != c.humans)
    {
        c.humans = hs;
        ++s_changeSerial;
    }
    int v = 0;
    const int put = PortLeagueTakePutAnswer(&v);
    if (put == 1)
    {
        for (int k = 0; k < s_lgInFlight && !s_lgOps.empty(); k++)
            s_lgOps.pop_front();
        s_lgInFlight = 0;
        c.leagueV = v;
        save();
        fprintf(stderr, "[franchise] league: version %d is ours\n", v);
        fflush(stderr);
    }
    else if (put == 2)
    {
        s_lgInFlight = 0;
        s_lgFetching = true;
        PortLeagueGet();
        fprintf(stderr, "[franchise] league: someone else's change landed first (version %d): fetching it\n", v);
        fflush(stderr);
    }
    std::string text;
    int tv = 0;
    if (PortLeagueTakeCareer(&text, &tv))
    {
        s_lgFetching = false;
        Career fresh;
        if (league_from_text(text, &fresh))
        {
            fresh.slot = PORT_FR_LEAGUE_SLOT;
            fresh.league = c.league;
            fresh.myClub = st.myClub >= 0 ? st.myClub : c.myClub;
            fresh.humans = hs;
            fresh.leagueV = tv;
            s_career = fresh;
            // this headset's actions since, again on the newest (each checks it still applies)
            s_lgReplaying = true;
            std::deque<LeagueOp> keep;
            for (size_t i = 0; i < s_lgOps.size(); i++)
                if (s_lgOps[i].fn())
                    keep.push_back(s_lgOps[i]);
            s_lgReplaying = false;
            s_lgOps.swap(keep);
            save();
            ++s_changeSerial;
            fprintf(stderr, "[franchise] league: version %d in (%d of ours on top), week %d\n", tv, (int)s_lgOps.size(),
                    s_career.week);
            fflush(stderr);
        }
        else
        {
            fprintf(stderr, "[franchise] league: version %d didn't read\n", tv);
            fflush(stderr);
        }
    }
    // a game between two people: both ready, the server's go: played online in a room of its own (host = home)
    int goGame = -1;
    std::string goRoom;
    bool goHost = false;
    if (PortLeagueTakeGo(&goGame, &goRoom, &goHost))
    {
        const Game* mg = my_game(s_career);
        if (s_match == MATCH_NONE && mg != NULL && !mg->played && (int)(mg - &s_career.games[0]) == goGame)
        {
            PortNetLeagueRoom(goRoom.c_str(), goHost ? 1 : 0);
            s_lgVersus = true;
            s_watchGame = -1;
            s_match = MATCH_PENDING;
            fprintf(stderr, "[franchise] league: game %d on: room %s, %s\n", goGame, goRoom.c_str(),
                    goHost ? "hosting (home)" : "away");
        }
        else
            fprintf(stderr, "[franchise] league: the go for game %d not taken (match %d, my game %d)\n", goGame,
                    (int)s_match, mg != NULL ? (int)(mg - &s_career.games[0]) : -1);
        fflush(stderr);
    }
    // someone else moved the league on: the newest copy (not mid-match: it comes after)
    if (!s_lgFetching && s_lgInFlight == 0 && s_lgOps.empty() && st.version > s_career.leagueV && s_match == MATCH_NONE)
    {
        s_lgFetching = true;
        PortLeagueGet();
    }
    // ours up, on the version this copy is
    if (!s_lgFetching && s_lgInFlight == 0 && !s_lgOps.empty())
    {
        const std::string t = league_text(s_career);
        if (!t.empty())
        {
            s_lgInFlight = (int)s_lgOps.size();
            PortLeaguePut(s_career.leagueV, t, s_lgOps.back().name.c_str());
        }
    }
}
// Joining a league by its code (no career open): the league read (the club picker), a club claimed, the newest copy
// of its career in and opened as this headset's league career.
void league_join_frame()
{
    LeagueJoin& j = s_lgJoin;
    if (j.state == 0 || j.state == 5)
        return;
    unsigned serial = 0;
    const PortLeagueState st = PortLeagueGetState(&serial);
    if (serial != s_lgSerialSeen)  // (the lobby draws it again)
    {
        s_lgSerialSeen = serial;
        ++s_changeSerial;
    }
    std::string text;
    int tv = 0;
    if (PortLeagueTakeCareer(&text, &tv))
    {
        Career fresh;
        if (league_from_text(text, &fresh))
        {
            fresh.leagueV = tv;
            j.preview = fresh;
            j.havePreview = true;
        }
    }
    if (st.errors != j.errorsSeen)
    {
        j.errorsSeen = st.errors;
        j.error = st.error;
        if (j.state == 4 && st.myClub < 0)
            j.club = -1;  // (refused: taken first, or full)
    }
    if (st.link != 2)
    {
        if (j.state != 4)
            j.state = 1;
        return;
    }
    if (!st.exists)
    {
        j.state = 2;
        return;
    }
    if (st.myClub < 0)
    {
        if (j.club >= 0)
        {
            j.state = 4;  // the claim on its way
            return;
        }
        j.state = (int)st.members.size() >= st.max ? 6 : 3;
        return;
    }
    // a member now (or this headset was already): the newest copy, then open it
    j.state = 4;
    if (!j.asked)
    {
        j.asked = true;
        j.havePreview = false;
        PortLeagueGet();
        return;
    }
    if (!j.havePreview)
        return;
    Career c = j.preview;
    c.slot = PORT_FR_LEAGUE_SLOT;
    c.coop = false;
    c.league = j.code;
    c.myClub = st.myClub;
    c.humans.clear();
    for (size_t i = 0; i < st.members.size(); i++)
        if (st.members[i].club >= 0 && st.members[i].club < PORT_FR_CLUBS)
            c.humans.push_back(st.members[i].club);
    std::sort(c.humans.begin(), c.humans.end());
    s_career = c;
    s_active = true;
    save();
    s_lgCode = j.code;  // (league_frame carries on on this link)
    s_lgLink = st.link;
    s_lgOps.clear();
    s_lgInFlight = 0;
    s_lgFetching = false;
    j.state = 5;
    j.opened = true;
    ++s_changeSerial;
    fprintf(stderr, "[franchise] league %s joined: %s, version %d, week %d\n", j.code.c_str(),
            s_clubs[c.myClub].id.c_str(), c.leagueV, c.week);
    fflush(stderr);
}
}  // namespace

bool PortFranchiseLeagueFile(PortFranchiseSlot* out, char* code, int codeSize)
{
    read_data();
    if (code != NULL && codeSize > 0)
        code[0] = '\0';
    if (!slot_summary(PORT_FR_LEAGUE_SLOT, out))
        return false;
    Career c;
    if (code != NULL && codeSize > 0 && load_slot(PORT_FR_LEAGUE_SLOT, &c))
        snprintf(code, (size_t)codeSize, "%s", c.league.c_str());
    return true;
}

bool PortFranchiseLeagueCreate(int club, int captainPick, int backupPick, int sidekickPick, int maxMembers)
{
    if (s_match != MATCH_NONE)
        return false;
    s_lgJoin = LeagueJoin();
    if (!PortFranchiseNewPicked(PORT_FR_LEAGUE_SLOT, club, captainPick, backupPick, sidekickPick))
        return false;
    Career& c = s_career;
    c.league = PortLeagueNewCode();
    c.humans.assign(1, club);
    c.leagueV = 1;
    c.headline = "League " + c.league + " is open: give your friends the code. Season 1 kicks off!";
    save();
    const std::string text = league_text(c);
    s_lgCode = c.league;
    s_lgOps.clear();
    s_lgInFlight = 0;
    s_lgFetching = false;
    PortLeagueConnect(c.league);
    PortLeagueCreate(club, text, std::max(2, std::min(8, maxMembers)));
    ++s_changeSerial;
    fprintf(stderr, "[franchise] league %s made: %s, up to %d people (%d bytes)\n", c.league.c_str(),
            s_clubs[club].id.c_str(), std::max(2, std::min(8, maxMembers)), (int)text.size());
    fflush(stderr);
    return true;
}

void PortFranchiseLeagueJoin(const char* raw)
{
    s_lgJoin = LeagueJoin();
    std::string code;
    for (const char* p = raw; p != NULL && *p != '\0'; p++)
    {
        const char ch = *p;
        if (ch >= 'a' && ch <= 'z')
            code.push_back((char)(ch - 'a' + 'A'));
        else if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))
            code.push_back(ch);
    }
    if (in_league() || s_match != MATCH_NONE)
        return;
    s_lgCode.clear();
    s_lgLeaveFrames = 0;
    if (code.size() < 4 || code.size() > 8)
    {
        PortLeagueConnect(std::string());
        return;
    }
    read_data();
    s_lgJoin.code = code;
    s_lgJoin.state = 1;
    PortLeagueConnect(code);
    PortLeagueGet();  // its career as it stands: the club picker shows who plays where
    fprintf(stderr, "[franchise] league %s: joining\n", code.c_str());
    fflush(stderr);
}

bool PortFranchiseLeagueLobbyGet(PortFranchiseLeagueLobby* out)
{
    memset(out, 0, sizeof *out);
    const LeagueJoin& j = s_lgJoin;
    if (j.state == 0)
        return false;
    unsigned serial = 0;
    const PortLeagueState st = PortLeagueGetState(&serial);
    out->state = j.state;
    copy_str(out->code, sizeof out->code, j.code);
    out->max = st.max;
    out->members = (int)st.members.size();
    for (size_t i = 0; i < st.members.size(); i++)
    {
        const PortLeagueMember& m = st.members[i];
        if (m.owner)
            copy_str(out->owner, sizeof out->owner, m.name);
        if (m.club >= 0 && m.club < PORT_FR_CLUBS)
        {
            out->taken[m.club] = true;
            copy_str(out->takenBy[m.club], sizeof out->takenBy[m.club], m.name);
        }
    }
    if (j.havePreview)
    {
        out->season = j.preview.season;
        out->week = j.preview.week;
        for (int k = 0; k < PORT_FR_CLUBS; k++)
            copy_str(out->captain[k], sizeof out->captain[k], captain_name(starter(j.preview.clubs[k])));
    }
    copy_str(out->error, sizeof out->error, j.error);
    return true;
}

bool PortFranchiseLeagueClaim(int club)
{
    LeagueJoin& j = s_lgJoin;
    if (j.state != 3 || club < 0 || club >= PORT_FR_CLUBS)
        return false;
    unsigned serial = 0;
    const PortLeagueState st = PortLeagueGetState(&serial);
    for (size_t i = 0; i < st.members.size(); i++)
        if (st.members[i].club == club)
            return false;
    j.club = club;
    j.state = 4;
    j.error.clear();
    PortLeagueClaim(club);
    fprintf(stderr, "[franchise] league %s: claiming %s\n", j.code.c_str(), PortFranchiseClubId(club));
    fflush(stderr);
    return true;
}

bool PortFranchiseLeagueTakeOpened()
{
    const bool was = s_lgJoin.opened;
    s_lgJoin.opened = false;
    return was;
}

bool PortFranchiseLeagueGetView(PortFranchiseLeagueView* out)
{
    memset(out, 0, sizeof *out);
    out->vsMember = -1;
    if (!in_league())
        return false;
    unsigned serial = 0;
    const PortLeagueState st = PortLeagueGetState(&serial);
    Career& c = s_career;
    out->active = true;
    copy_str(out->code, sizeof out->code, c.league);
    out->link = st.link;
    out->owner = st.owner;
    out->max = st.max;
    const Game* mg = my_game(c);
    const int gi = mg != NULL ? (int)(mg - &c.games[0]) : -1;
    const int opp = mg != NULL && !mg->played ? (mg->home == c.myClub ? mg->away : mg->home) : -1;
    for (size_t i = 0; i < st.members.size() && out->memberCount < 8; i++)
    {
        const PortLeagueMember& m = st.members[i];
        PortFranchiseLeagueView::Member& o = out->members[out->memberCount];
        copy_str(o.name, sizeof o.name, m.name);
        o.club = m.club;
        o.online = m.online;
        o.owner = m.owner;
        o.me = m.club == c.myClub;
        o.ready = m.ready >= 0;
        o.done = true;  // their game this week is in (or they have none)
        for (size_t k = 0; k < c.games.size(); k++)
        {
            const Game& g = c.games[k];
            if (g.week == c.week && !g.played && (g.home == m.club || g.away == m.club))
                o.done = false;
        }
        if (opp >= 0 && m.club == opp)
        {
            out->vsMember = out->memberCount;
            out->themReady = m.ready == gi;
        }
        if (o.me)
            out->meReady = gi >= 0 && m.ready == gi;
        out->memberCount++;
    }
    out->toSend = (int)s_lgOps.size();
    out->synced = st.link == 2 && s_lgOps.empty() && s_lgInFlight == 0 && !s_lgFetching && c.leagueV >= st.version;
    out->canSimRest = st.owner && c.phase != PHASE_DONE && !league_week_ready(c);
    copy_str(out->error, sizeof out->error, st.error);
    return true;
}

bool PortFranchiseLeagueSetReady(bool on)
{
    if (!in_league() || s_match != MATCH_NONE)
        return false;
    if (!on)
    {
        PortLeagueUnready();
        return true;
    }
    Career& c = s_career;
    const Game* mg = my_game(c);
    if (mg == NULL || mg->played || !is_human(c, mg->home == c.myClub ? mg->away : mg->home))
        return false;
    // both on the same copy of the league: nothing of yours still to send, the newest in
    unsigned serial = 0;
    const PortLeagueState st = PortLeagueGetState(&serial);
    if (st.link != 2 || !s_lgOps.empty() || s_lgInFlight != 0 || s_lgFetching || c.leagueV < st.version)
        return false;
    PortLeagueReady((int)(mg - &c.games[0]), mg->home, mg->away);
    fprintf(stderr, "[franchise] league: ready for game %d\n", (int)(mg - &c.games[0]));
    fflush(stderr);
    return true;
}

void PortFranchiseLeagueLeave()
{
    if (s_match != MATCH_NONE)
        return;
    std::string code;
    Career c;
    if (in_league())
        code = s_career.league;
    else if (load_slot(PORT_FR_LEAGUE_SLOT, &c))
        code = c.league;
    if (s_active && s_career.slot == PORT_FR_LEAGUE_SLOT)
        s_active = false;
    remove(career_path(PORT_FR_LEAGUE_SLOT).c_str());
    s_lgJoin = LeagueJoin();
    s_lgOps.clear();
    s_lgInFlight = 0;
    s_lgFetching = false;
    if (code.empty())
        return;
    PortLeagueConnect(code);  // (on it already, or again just for this)
    PortLeagueLeave();        // the club goes back to the computer
    s_lgCode = code;
    s_lgLeaveFrames = 72 * 10;
    fprintf(stderr, "[franchise] league %s: left\n", code.c_str());
    fflush(stderr);
}

void PortFranchiseFrame()
{
    s_onMainMenu = on_main_menu();
    coop_frame();  // r76
    league_join_frame();  // r100
    league_frame();  // r100
    if (s_match == MATCH_NONE)
    {
        // r78: outside the franchise's matches its co-captains go; r79: a Grudge Match's picked ones
        // instead (none in any other mode). r87: only once back on the menus: the match is over
        // (MATCH_NONE) before its highlights and summary, and those still show its co-captains (r86:
        // the highlights went back to Luigi and Peach).
        const unsigned st = nlTaskManager::m_pInstance != NULL ? nlTaskManager::m_pInstance->m_CurrState : 0;
        if (st != 2)
            PortApplyPickedCoCaptains();
    }
    if (s_match == MATCH_HOLD)
    {
        // r76, co-op: waiting at the match start for the friend's headset (xr_net pairs the two). 0 = go:
        // online (both headsets start on the same frame), or alone (no friend any more, B held to stop)
        if (PortNetHoldMatchStart(&s_holdMatch, s_holdSide))
            return;
        if (s_lgVersus && !PortNetOnline())
        {
            // r100: an online league's game between two people, and the other one never came (B held): not played
            s_match = MATCH_NONE;
            s_lgVersus = false;
            PortNetLeagueRoom(NULL, 0);
            s_career.headline = "The online game didn't start. Ready up again from the hub.";
            ++s_changeSerial;
            fprintf(stderr, "[franchise] league: the online game didn't start (the other person wasn't there)\n");
            fflush(stderr);
            return;
        }
        s_match = MATCH_PLAYING;
        fprintf(stderr, "[franchise] co-op: the match starts %s\n",
                PortNetOnline() ? "online with the friend" : "on this headset alone (the friend wasn't there)");
        fflush(stderr);
        nlTaskManager::SetNextState(2);
        return;
    }
    if (s_match == MATCH_PENDING)
    {
        if (s_onMainMenu && (!s_lgVersus || PortNetLeagueRoomReady()))  // r100: once in the league game's room
            start_match();
        return;
    }
    if (s_match == MATCH_PLAYING)
    {
        // back on the menus without a result (quit from the pause menu): not played
        const unsigned st = nlTaskManager::m_pInstance != NULL ? nlTaskManager::m_pInstance->m_CurrState : 0;
        if (st == 2)
            s_sawGame = true;
        else if (st == 4 && s_sawGame)
        {
            fprintf(stderr, "[franchise] week %d's match left without a result: not played\n", s_career.week);
            fflush(stderr);
            s_match = MATCH_NONE;
            s_watchGame = -1;
            if (s_lgVersus)  // r100
            {
                s_lgVersus = false;
                PortNetLeagueRoom(NULL, 0);
            }
        }
    }
}

void PortFranchiseMatchEnd(int scoreHome, int scoreAway, bool overtime)
{
    if (s_match != MATCH_PLAYING || !s_active || s_matchGame < 0 || s_matchGame >= (int)s_career.games.size())
        return;
    Career& c = s_career;
    Game& g = c.games[s_matchGame];
    if (g.played)
        return;
    // r100: an online league's game goes into the league's career as an action (every person's beef, the week on
    // once everyone's game is in): done here now, uploaded, done again on a newer copy if need be
    if (in_league())
    {
        const PlayedResult r = capture_played(s_matchGame, scoreHome, scoreAway, overtime);
        const int mine = g.home == c.myClub ? scoreHome : scoreAway, theirs = g.home == c.myClub ? scoreAway : scoreHome;
        const bool won = mine > theirs;
        const std::string opp = s_clubs[g.home == c.myClub ? g.away : g.home].place;
        league_do("result", [r]() {
            if (!league_apply_played(s_career, r))
                return false;
            league_advance(s_career, false);
            save();
            return true;
        });
        char lbuf[200];  // (everyone in the league reads it: whose game)
        snprintf(lbuf, sizeof lbuf, "%s: %s %s %d - %d%s", s_clubs[c.myClub].place.c_str(), won ? "WIN vs" : "LOSS vs",
                 opp.c_str(), mine, theirs, overtime ? " (OT)" : "");
        s_career.headline = lbuf;
        save();
        s_match = MATCH_NONE;
        if (s_lgVersus)  // the game between two people: back to the launcher's room
        {
            s_lgVersus = false;
            PortNetLeagueRoom(NULL, 0);
        }
        fprintf(stderr, "[franchise] league game %d: %s\n", r.game, lbuf);
        fflush(stderr);
        return;
    }
    g.homeGoals = scoreHome;
    g.awayGoals = scoreAway;
    if (g.homeGoals == g.awayGoals)  // (a draw can't happen: golden goal; just in case)
        g.homeGoals++;
    g.overtime = overtime;
    apply_result(c, g);
    personal_xp(c, g);  // r77: the players' own goals and assists
    // r71: the match's real scorers for the week's A-Life (finish_week); r72: its Super Strikes and hits
    memcpy(s_lifeScorers, s_matchGoals, sizeof s_lifeScorers);
    memcpy(s_lifeSS, s_matchSS, sizeof s_lifeSS);
    memcpy(s_lifeCapHits, s_matchCapHits, sizeof s_lifeCapHits);
    memcpy(s_lifeAnyHits, s_matchAnyHits, sizeof s_lifeAnyHits);
    s_lifeGame = s_matchGame;
    char buf[200];
    if (g.home != c.myClub && g.away != c.myClub)
    {
        // r70b: a CPU v CPU game you watched: the rest of the day played out after it
        std::string line = result_line(g);
        if (g.series >= 0 && g.series < (int)c.series.size())
        {
            const Series& s = c.series[g.series];
            line += " (" + round_name(s.round, s.conf) + ", series " + std::to_string(std::max(s.winsHi, s.winsLo)) + "-" +
                    std::to_string(std::min(s.winsHi, s.winsLo)) + ")";
        }
        snprintf(buf, sizeof buf, "You watched %s", line.c_str());
        c.headline = buf;
        finish_week(c);
        if (c.headline != buf)
            c.headline = std::string(buf) + ". " + c.headline;
        save();
        s_match = MATCH_NONE;
        s_watchGame = -1;
        fprintf(stderr, "[franchise] %s\n", buf);
        fflush(stderr);
        return;
    }
    const bool won = (g.home == c.myClub) == (g.homeGoals > g.awayGoals);
    // r72: your nemesis's reaction in its own voice: it gloats when it beat you, it seethes when it didn't
    if (const Nemesis* n = nemesis_in(c, g))
        PortGuestVoiceSay(PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, n->key.c_str()),
                          won ? "hurt_big,ko,hurt,dazed,cry" : "cheer,taunt,quote,cry", 1.0f);
    const char* opp = s_clubs[g.home == c.myClub ? g.away : g.home].place.c_str();
    const int mine = g.home == c.myClub ? g.homeGoals : g.awayGoals, theirs = g.home == c.myClub ? g.awayGoals : g.homeGoals;
    if (g.series >= 0 && g.series < (int)c.series.size())
    {
        // r70b: a playoff game: the series as it stands
        const Series& s = c.series[g.series];
        const int me = s.hi == c.myClub ? s.winsHi : s.winsLo, them = s.hi == c.myClub ? s.winsLo : s.winsHi;
        const std::string state = me >= 3 ? "SERIES WON " : (them >= 3 ? "SERIES LOST " : (me > them ? "you lead " : (me < them ? "you trail " : "series tied ")));
        snprintf(buf, sizeof buf, "%s game %d: %s %s %d - %d%s, %s%d-%d", round_name(s.round, s.conf).c_str(),
                 s.winsHi + s.winsLo, won ? "WIN vs" : "LOSS vs", opp, mine, theirs, overtime ? " (OT)" : "",
                 state.c_str(), std::max(me, them), std::min(me, them));
    }
    else
        snprintf(buf, sizeof buf, "Week %d: %s %s %d - %d%s", c.week, won ? "WIN vs" : "LOSS vs", opp, mine, theirs,
                 overtime ? " (OT)" : "");
    c.headline = buf;
    finish_week(c);
    if (c.headline != buf)  // the season moved on too (playoffs, next round, champion): both
        c.headline = std::string(buf) + ". " + c.headline;
    save();
    s_match = MATCH_NONE;
    fprintf(stderr, "[franchise] %s\n", buf);
    fflush(stderr);
}

const char* PortFranchiseSidekickName(int side, int playerId)
{
    if (s_match != MATCH_PLAYING || side < 0 || side > 1 || playerId < 1 || playerId > 3)
        return NULL;
    const int club = s_matchClub[side];
    if (club < 0 || club >= PORT_FR_CLUBS)
        return NULL;
    // r81 (Lance: "you have the co-captain, named as if its a sidekick, with generated names"): slot 1 is
    // the co-captain when the side has one - a captain, called by its own name; the sidekicks on the field
    // are slots 2-3 (names[1], names[2])
    if (playerId == 1 && PortSecondCaptain(side) >= 0)
        return NULL;
    const std::string& n = s_career.clubs[club].names[playerId - 1];
    return n.empty() ? NULL : n.c_str();
}

bool PortFranchiseLiveXp(PortFranchiseLive* out)
{
    memset(out, 0, sizeof *out);
    if (s_match != MATCH_PLAYING || !s_active || s_matchGame < 0 || s_matchGame >= (int)s_career.games.size())
        return false;
    const Career& c = s_career;
    const Game& g = c.games[s_matchGame];
    const int me = g.home == c.myClub ? 0 : (g.away == c.myClub ? 1 : -1);
    if (me < 0)
        return false;  // a game you're watching
    const ClubState& s = c.clubs[c.myClub];
    int goals[2] = {0, 0};
    for (int side = 0; side < 2; side++)
        for (int k = 0; k < kScorerSlots; k++)  // r78: the co-captain's (4) too
            goals[side] += s_matchGoals[side][k];
    int assists[4];
    match_assists(me, assists);
    // (asked every frame: worked out again only when the match's numbers move)
    static PortFranchiseLive s_last;
    static long long s_lastKey = -1;
    long long cacheKey = ((long long)c.version * 4099LL + s_matchGame) * 1000003LL + (s.startsBackup ? 7 : 0);
    for (int k = 0; k < 4; k++)
        cacheKey = cacheKey * 131 + s_matchGoals[me][k] * 17 + assists[k];
    cacheKey = cacheKey * 131 + s_matchGoals[me][4] * 17 + goals[1 - me];  // r78: the co-captain's too
    if (cacheKey == s_lastKey && s_last.valid)
    {
        *out = s_last;
        return true;
    }
    const bool won = goals[me] > goals[1 - me];
    // the club's part as award_xp gives it, if the match ended now
    int team = (won ? 70 : 40) + 8 * goals[me];
    if (g.series >= 0)
        team += team / 2;
    if (nemesis_match(c, g))
        team += team / 4;
    team += played_goal_bonus(c, g, goals[me]);  // r86: as personal_xp gives it
    const bool backupStarts = s.startsBackup && !s.backup.empty();
    const std::string& key = backupStarts ? s.backup : s.captain;
    const Prog& p = backupStarts ? s.bk : s.cap;
    out->valid = true;
    out->won = won;
    out->goals = s_matchGoals[me][0];
    out->assists = assists[0];
    out->gain = team + personal_bonus(c, g, out->goals, out->assists);
    out->level = p.level;
    out->xp = p.xp;
    Prog q = p;
    gain(q, out->gain);
    out->newLevel = q.level;
    out->newXp = q.xp;
    copy_str(out->name, sizeof out->name, name_of(key));
    std::string to;
    out->evoLevel = next_evolution(key, &to);
    out->pokemon = out->evoLevel > 0 || key.compare(0, 3, "pk_") == 0;
    if (out->evoLevel > 0)
    {
        copy_str(out->evoName, sizeof out->evoName, name_of(to));
        out->evolves = out->newLevel >= out->evoLevel && p.declinedAt != out->newLevel;
    }
    copy_str(out->kind, sizeof out->kind, kind_name(s.kind));
    out->skLevel = s.side.level;
    out->skXp = s.side.xp;
    out->skGain = team + personal_bonus(c, g, s_matchGoals[me][1] + s_matchGoals[me][2] + s_matchGoals[me][3],
                                        (PortSecondCaptain(me) >= 0 ? 0 : assists[1]) + assists[2] + assists[3]);
    Prog k = s.side;
    gain(k, out->skGain);
    out->skNewLevel = k.level;
    out->skNewXp = k.xp;
    // r79: the co-captain (your other captain, slot 1 / scorer 4), as personal_xp gives it
    if (PortSecondCaptain(me) >= 0 && !s.backup.empty())
    {
        const std::string& coKey = backupStarts ? s.captain : s.backup;
        const Prog& cp = backupStarts ? s.cap : s.bk;
        out->co = true;
        out->coGoals = s_matchGoals[me][4];
        out->coAssists = assists[1];
        out->coGain = team + personal_bonus(c, g, out->coGoals, out->coAssists);
        out->coLevel = cp.level;
        out->coXp = cp.xp;
        Prog cq = cp;
        gain(cq, out->coGain);
        out->coNewLevel = cq.level;
        out->coNewXp = cq.xp;
        copy_str(out->coName, sizeof out->coName, name_of(coKey));
        std::string coTo;
        out->coEvoLevel = next_evolution(coKey, &coTo);
        out->coPokemon = out->coEvoLevel > 0 || coKey.compare(0, 3, "pk_") == 0;
        if (out->coEvoLevel > 0)
        {
            copy_str(out->coEvoName, sizeof out->coEvoName, name_of(coTo));
            out->coEvolves = out->coNewLevel >= out->coEvoLevel && cp.declinedAt != out->coNewLevel;
        }
    }
    s_last = *out;
    s_lastKey = cacheKey;
    return true;
}

int PortFranchiseEvoWatch(PortFranchiseEvoRow* out, int max)
{
    if (!s_active || out == NULL || max <= 0)
        return 0;
    const Career& c = s_career;
    std::vector<PortFranchiseEvoRow> rows;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        const ClubState& s = c.clubs[i];
        for (int b = 0; b < 2; b++)
        {
            const std::string& key = b ? s.backup : s.captain;
            if (key.compare(0, 3, "pk_") != 0)
                continue;  // Pokemon only
            const Prog& p = b ? s.bk : s.cap;
            PortFranchiseEvoRow r;
            memset(&r, 0, sizeof r);
            copy_str(r.name, sizeof r.name, name_of(key));
            copy_str(r.key, sizeof r.key, key);
            r.club = i;
            r.mine = i == c.myClub;
            r.backup = b != 0;
            r.shiny = p.shiny;
            r.level = p.level;
            r.xp = p.xp;
            r.seasonGain = std::max(0, p.level - p.startLevel);
            std::string to;
            r.evoLevel = next_evolution(key, &to);
            if (r.evoLevel > 0)
                copy_str(r.evoName, sizeof r.evoName, name_of(to));
            std::string now[2];
            r.ready = r.mine && evolutions_for(key, p, now) > 0;  // r95
            rows.push_back(r);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const PortFranchiseEvoRow& a, const PortFranchiseEvoRow& b) {
        if (a.level != b.level)
            return a.level > b.level;
        if (a.xp != b.xp)
            return a.xp > b.xp;
        return strcmp(a.name, b.name) < 0;
    });
    const int n = std::min(max, (int)rows.size());
    for (int i = 0; i < n; i++)
        out[i] = rows[i];
    return n;
}

// ---------------------------------------------------------------------------- r77, your own trades
namespace
{
int player_value(const std::string& key, const Prog& p) { return rating(key) + std::max(0, p.level - 8); }

// A captain slot of a club: its key, progress and record (backup or captain)
struct Slot
{
    std::string* key;
    Prog* prog;
    PStats* st;
};
Slot slot_of(ClubState& s, bool backup)
{
    return backup ? Slot{&s.backup, &s.bk, &s.bkSt} : Slot{&s.captain, &s.cap, &s.capSt};
}
}  // namespace

bool PortFranchiseTradeOpen()
{
    if (!s_active || s_match != MATCH_NONE || coop_blocked())
        return false;
    return (s_career.phase == PHASE_REGULAR && s_career.week <= 12) || s_career.phase == PHASE_DONE;
}

int PortFranchiseTradeOptions(int what, PortFranchiseTradeOffer* out, int max, int* myValue, char* myName, int nameSize)
{
    *myValue = 0;
    if (myName != NULL && nameSize > 0)
        myName[0] = '\0';
    if (!s_active || what < 0 || what > 2 || out == NULL || max <= 0)
        return 0;
    Career& c = s_career;
    ClubState& me = c.clubs[c.myClub];
    if (what == 1 && me.backup.empty())
        return 0;
    if (what == 2)
    {
        *myValue = rating(me.kind) + std::max(0, me.side.level - 8);
        if (myName != NULL)
            copy_str(myName, (size_t)nameSize, kind_name(me.kind) + " LV " + std::to_string(me.side.level));
    }
    else
    {
        const Slot mine = slot_of(me, what == 1);
        *myValue = player_value(*mine.key, *mine.prog);
        if (myName != NULL)
            copy_str(myName, (size_t)nameSize, name_of(*mine.key) + " LV " + std::to_string(mine.prog->level));
    }
    std::vector<PortFranchiseTradeOffer> offers;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        if (i == c.myClub)
            continue;
        ClubState& s = c.clubs[i];
        for (int b = 0; b < (what == 2 ? 1 : 2); b++)
        {
            PortFranchiseTradeOffer o;
            memset(&o, 0, sizeof o);
            o.club = i;
            o.backup = b != 0;
            if (what == 2)
            {
                if (s.kind == me.kind)
                    continue;
                copy_str(o.name, sizeof o.name, kind_name(s.kind));
                o.level = s.side.level;
                o.value = rating(s.kind) + std::max(0, s.side.level - 8);
            }
            else
            {
                const Slot their = slot_of(s, b != 0);
                if (their.key->empty())
                    continue;
                copy_str(o.name, sizeof o.name, name_of(*their.key));
                o.level = their.prog->level;
                o.shiny = their.prog->shiny;
                o.value = player_value(*their.key, *their.prog);
            }
            o.fair = *myValue * 10 >= o.value * 9;
            offers.push_back(o);
        }
    }
    std::sort(offers.begin(), offers.end(), [](const PortFranchiseTradeOffer& a, const PortFranchiseTradeOffer& b) {
        return a.value != b.value ? a.value > b.value : a.club < b.club;
    });
    const int n = std::min(max, (int)offers.size());
    for (int i = 0; i < n; i++)
        out[i] = offers[i];
    return n;
}

static bool trade_impl(int what, int club, bool backup);

bool PortFranchiseTrade(int what, int club, bool backup)
{
    // r100: an online league's: never with a person's club; an action (again on a newer copy, if it still stands)
    if (in_league() && is_human(s_career, club))
        return false;
    static bool s_done;
    s_done = false;
    league_do("trade", [what, club, backup]() {
        s_done = trade_impl(what, club, backup);
        return s_done;
    });
    return s_done;
}

static bool trade_impl(int what, int club, bool backup)
{
    if (!PortFranchiseTradeOpen() || what < 0 || what > 2 || club < 0 || club >= PORT_FR_CLUBS)
        return false;
    Career& c = s_career;
    if (club == c.myClub)
        return false;
    ClubState& me = c.clubs[c.myClub];
    ClubState& them = c.clubs[club];
    std::string gave, got;
    int myValue = 0, theirValue = 0;
    if (what == 2)
    {
        if (them.kind == me.kind)
            return false;
        myValue = rating(me.kind) + std::max(0, me.side.level - 8);
        theirValue = rating(them.kind) + std::max(0, them.side.level - 8);
        if (myValue * 10 < theirValue * 9)
            return false;
        gave = kind_name(me.kind);
        got = kind_name(them.kind);
        swap_sidekicks(c, c.myClub, club);
    }
    else
    {
        if (what == 1 && me.backup.empty())
            return false;
        Slot mine = slot_of(me, what == 1), their = slot_of(them, backup);
        if (their.key->empty())
            return false;
        myValue = player_value(*mine.key, *mine.prog);
        theirValue = player_value(*their.key, *their.prog);
        if (myValue * 10 < theirValue * 9)
            return false;
        gave = name_of(*mine.key);
        got = name_of(*their.key);
        std::swap(*mine.key, *their.key);
        std::swap(*mine.prog, *their.prog);
        std::swap(*mine.st, *their.st);
        // a nemesis that moved plays for its new club now
        each_nemesis(c, *their.key, [&](Nemesis* n) { n->club = club; });  // r100: everyone's records
        each_nemesis(c, *mine.key, [&](Nemesis* n) { n->club = c.myClub; });
    }
    const std::string place = s_clubs[club].place;
    c.headline = "Trade: you sent " + gave + " to " + place + " for " + got + ".";
    c.news.push_back("TRADE: " + upper(s_clubs[c.myClub].place) + " SEND " + upper(gave) + " TO " + upper(place) + " FOR " +
                     upper(got));
    if (what != 2)
        post(c, (backup ? them.bkSt : them.capSt).personality, EV_TRADED, gave, place);
    save();
    fprintf(stderr, "[franchise] your trade: %s to %s for %s (value %d for %d)\n", gave.c_str(), s_clubs[club].id.c_str(),
            got.c_str(), myValue, theirValue);
    fflush(stderr);
    return true;
}

void PortFranchiseGoal(int side, int who, int goals)
{
    if (s_match != MATCH_PLAYING || side < 0 || side > 1 || goals <= 0)
        return;
    if (goals >= 2)
        s_matchSS[side]++;  // r72: a Super Strike
    if (who >= 0 && who < kScorerSlots)  // r78: 4 = the co-captain
        s_matchGoals[side][who] += goals;
}

float PortFranchiseHunt(int side, bool isCaptain)
{
    if (s_match != MATCH_PLAYING || !s_active || s_matchGame < 0 || s_matchGame >= (int)s_career.games.size())
        return 0.0f;
    const Game& g = s_career.games[s_matchGame];
    // the person hunted: you; r100, an online league's game between two people: each side's person by the other
    // side's nemesis of theirs (the same on both headsets)
    const int victim = s_lgVersus ? (side == 0 ? g.away : g.home) : s_career.myClub;
    const Nemesis* n = nemesis_in(s_career, g, victim);
    if (n == NULL)
        return 0.0f;
    const int nemSide = g.home == victim ? 1 : 0;
    if (side != nemSide)
        return 0.0f;
    // subtle (Lance): 12-28% of the nemesis's hits by its rank; its teammates join in at 60% of that
    // r97: x0.7 on Rookie, x1.05 Professional, x1.4 Superstar, x1.75 Legend (up to 49% of its hits)
    const float base = (0.08f + 0.04f * (float)std::max(1, std::min(5, n->rank))) * (0.7f + 0.35f * (float)diff_k(s_career));
    return isCaptain ? base : base * 0.6f;
}

// r97: the game's skill level for the franchise match being played (GameInfo.cpp), -1 none
int PortFranchiseSkillLevel()
{
    // (asked for, held at the start for the co-op friend, or playing: the same on both co-op headsets,
    // the career being the host's)
    if (!s_active || s_match == MATCH_NONE)
        return -1;
    return std::max(1, std::min(4, s_career.difficulty));
}

int PortFranchiseDifficulty() { return s_active ? s_career.difficulty : 2; }

bool PortFranchiseSetDifficulty(int level)
{
    if (!s_active || coop_blocked() || s_match != MATCH_NONE || level < 1 || level > 4)
        return false;
    unsigned serial = 0;
    if (in_league() && !PortLeagueGetState(&serial).owner)
        return false;  // r100: an online league's is its owner's to set
    return league_do("difficulty", [level]() {
        static const char* const kNames[5] = {"", "Rookie", "Professional", "Superstar", "Legend"};
        s_career.difficulty = level;
        s_career.headline = std::string("Difficulty: ") + kNames[level] + ".";
        save();
        fprintf(stderr, "[franchise] difficulty: %s\n", kNames[level]);
        fflush(stderr);
        return true;
    });
}

bool PortFranchiseHuntRoll(float chance)
{
    // r76: s_huntRng starts from the career at each match (start_match): a co-op match draws the same
    // on both headsets (a number kept from earlier in the run would differ: out of step)
    s_huntRng ^= s_huntRng << 13;
    s_huntRng ^= s_huntRng >> 17;
    s_huntRng ^= s_huntRng << 5;
    return (s_huntRng & 0xFFFFFF) / 16777216.0f < chance;
}

void PortFranchiseHit(int attackerSide, int attacker, int victimSide, int victim)
{
    if (s_match != MATCH_PLAYING || attackerSide < 0 || attackerSide > 1 || victimSide != 1 - attackerSide || victim != 0)
        return;
    s_matchAnyHits[attackerSide]++;  // someone flattened the other side's captain
    if (attacker == 0)
        s_matchCapHits[attackerSide]++;  // captain on captain
}

// ------------------------------------------------------------------- r70b: presentation and watching
void PortFranchiseIntroBegin()
{
    if (s_match != MATCH_PLAYING)
    {
        PortGuestSetShiny(0, false);  // r70c: shiny captains are franchise matches' only
        PortGuestSetShiny(1, false);
        return;
    }
    s_introSerial++;
    fprintf(stderr, "[franchise] match intro %d\n", s_introSerial);
    fflush(stderr);
    // r72: the face-off: your nemesis taunts you in its own voice (Lance: no written lines). r73: and
    // comes up to you life-size (the goal scorer's show, Plus), shows off, runs back before kick-off.
    if (s_matchGame >= 0 && s_matchGame < (int)s_career.games.size())
        if (const Nemesis* n = nemesis_in(s_career, s_career.games[s_matchGame]))
        {
            PortGuestVoiceSay(PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, n->key.c_str()), "taunt,quote,select,cry", 1.0f);
            const Game& g = s_career.games[s_matchGame];
            const int side = g.home == s_career.myClub ? 1 : 0;
            if (PortPlusFeatures() && g_pTeams[side] != NULL)
                XrScorerFaceOff(g_pTeams[side]->GetCaptain(), side);
        }
}

bool PortFranchiseMatchCard(PortFranchiseCard* out)
{
    memset(out, 0, sizeof *out);
    if (s_match != MATCH_PLAYING || !s_active || s_matchGame < 0 || s_matchGame >= (int)s_career.games.size())
        return false;
    const Career& c = s_career;
    const Game& g = c.games[s_matchGame];
    out->valid = true;
    out->spectating = g.home != c.myClub && g.away != c.myClub;
    out->serial = s_introSerial;
    out->club[0] = g.home;
    out->club[1] = g.away;
    for (int side = 0; side < 2; side++)
    {
        const ClubState& s = c.clubs[out->club[side]];
        out->wins[side] = s.wins;
        out->losses[side] = s.losses;
        out->seed[side] = c.phase == PHASE_REGULAR ? 0 : seed_of(c, out->club[side]);
    }
    out->week = c.week;
    if (g.series >= 0 && g.series < (int)c.series.size())
    {
        const Series& s = c.series[g.series];
        out->round = s.round;
        out->conf = s.conf;
        out->gameNo = s.winsHi + s.winsLo + 1;
        out->seriesWins[0] = g.home == s.hi ? s.winsHi : s.winsLo;
        out->seriesWins[1] = g.home == s.hi ? s.winsLo : s.winsHi;
        copy_str(out->roundName, sizeof out->roundName, round_name(s.round, s.conf));
        int r = 0, cf = 0, n = 0, w[2];
        const char* key = NULL;
        if (PortFranchisePlayoffMatch(&r, &cf, &n, w, &key) && key != NULL)
            copy_str(out->roundLogo, sizeof out->roundLogo, key);
    }
    else
        copy_str(out->roundLogo, sizeof out->roundLogo, "franchise_league");
    // r72: the face-off
    if (const Nemesis* n = nemesis_in(c, g))
    {
        out->nemesis = true;
        out->nemesisSide = g.home == c.myClub ? 1 : 0;
        copy_str(out->nemesisName, sizeof out->nemesisName, n->name);
        copy_str(out->nemesisTitle, sizeof out->nemesisTitle, kTitles[n->title]);
        copy_str(out->nemesisKey, sizeof out->nemesisKey, n->key);
        out->nemesisBeatYou = n->beatYou;
        out->nemesisLostToYou = n->lostToYou;
        out->nemesisRank = n->rank;
    }
    return true;
}

bool PortFranchiseBunting(unsigned* rgb1, unsigned* rgb2, bool* champion)
{
    if (!s_active)
        return false;
    const Career& c = s_career;
    *champion = c.phase == PHASE_DONE && c.champion == c.myClub;
    bool alive = false;
    if (c.phase == PHASE_PLAYOFFS && seed_of(c, c.myClub) > 0)
    {
        alive = true;
        for (size_t i = 0; i < c.series.size(); i++)
        {
            const Series& s = c.series[i];
            const int w = series_winner(s);
            if ((s.hi == c.myClub || s.lo == c.myClub) && w >= 0 && w != c.myClub)
                alive = false;
        }
    }
    if (!alive && !*champion)
        return false;
    *rgb1 = s_clubs[c.myClub].col[0];
    *rgb2 = s_clubs[c.myClub].col[1];
    return true;
}

void PortFranchiseTodayGames(PortFranchiseToday* out)
{
    memset(out, 0, sizeof *out);
    if (!s_active)
        return;
    const Career& c = s_career;
    for (size_t i = 0; i < c.games.size() && out->count < 16; i++)
    {
        const Game& g = c.games[i];
        if (g.week != c.week || g.played)
            continue;
        out->home[out->count] = g.home;
        out->away[out->count] = g.away;
        out->series[out->count] = g.series;
        out->count++;
    }
}

bool PortFranchiseWatch(int index)
{
    if (!s_active || s_match != MATCH_NONE || !s_onMainMenu || my_game(s_career) != NULL || coop_blocked() ||
        in_league())  // r100: a watched game would have to sim before the league's other headsets weigh in
        return false;
    int n = 0;
    for (size_t i = 0; i < s_career.games.size(); i++)
    {
        const Game& g = s_career.games[i];
        if (g.week != s_career.week || g.played)
            continue;
        if (n++ == index)
        {
            s_watchGame = (int)i;
            s_match = MATCH_PENDING;
            if (s_career.coop)  // r76: watched together
                PortNetCoopSendPlay(s_career.seed, s_career.version, (int)i);
            fprintf(stderr, "[franchise] watching %s v %s\n", s_clubs[g.home].id.c_str(), s_clubs[g.away].id.c_str());
            fflush(stderr);
            return true;
        }
    }
    return false;
}
