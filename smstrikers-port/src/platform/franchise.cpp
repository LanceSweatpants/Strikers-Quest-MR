// strikers-quest r70: franchise mode, step 1 - see include/port/franchise.h.
#include "port/franchise.h"
#include "port/edition.h"    // r73: PortPlusFeatures
#include "port/xr_guest.h"
#include "port/xr_net.h"     // r76: franchise co-op
#include "port/lineup.h"     // r78: the co-captain
#include "port/xr_scorer.h"  // r73: the nemesis face-off
#include "port/xr_league.h"  // r100: online leagues
#include "xr_weather.h"      // r137: weather (looks only)
#include "xr_weather_cal.h"

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
        // r130: the Strikers captains as sidekicks (cap_<name>) are picks of your own, never a club's sidekick kind
        if (strncmp(PortGuestRosterPack(idx), "cap_", 4) == 0)
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

// r116: a sidekick kind's portrait key (the Strikers ones are strikers_<name>, Hammer Bro without the s)
std::string kind_portrait(const std::string& kind)
{
    for (int k = 0; k < 4; k++)
        if (kind == kSkKey[k])
            return k == 2 ? std::string("strikers_hammerbro") : std::string("strikers_") + kSkKey[k];
    return kind;
}

int rating(const std::string& key)
{
    std::unordered_map<std::string, int>::const_iterator it = s_rating.find(key);
    return it != s_rating.end() ? it->second : 75;
}

// ------------------------------------------------------------------- r131: one character, one club
// (Lance: "Make it so that each character is only chosen once per franchise. you as the 1UP in single player, you pick
// first, and the league forms around your picks, never duplicating. even if that means mad teams of 4 captains".)
// A character = its whole evolution line (a Pokemon's forms, a Marvel hero's costumes), keyed by its first form; packs
// shown under the same name are one character too (two games' copies of one). The Strikers sidekicks (Toads, Koopas,
// Hammer Bros, Birdos) are no characters: any club can field them.
bool generic_kind(const std::string& key)
{
    for (int k = 0; k < 4; k++)
        if (key == kSkKey[k])
            return true;
    return false;
}

// r131: a key's first form (evolutions.txt backwards, else the roster's "evolves from")
std::string char_root(const std::string& key)
{
    std::string k = key.compare(0, 4, "cap_") == 0 ? "strikers_" + key.substr(4) : key;  // (r130's first-take packs)
    for (int guard = 0; guard < 12; guard++)
    {
        std::string prev;
        for (size_t i = 0; i < s_evos.size() && prev.empty(); i++)
            if (s_evos[i].to == k)
                prev = s_evos[i].from;
        if (prev.empty())
        {
            const int idx = PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, k.c_str());
            const char* from = idx >= 0 ? PortGuestRosterEvolvesFrom(idx) : NULL;
            if (from != NULL && from[0] != '\0')
                prev = from;
        }
        if (prev.empty() || prev == k)
            break;
        k = prev;
    }
    return k;
}

// r131: the name a key is shown under ("" one this headset doesn't have)
std::string char_name(const std::string& key)
{
    for (int t = 0; t < 8; t++)
        if (key == std::string("strikers_") + kTeamKey[t])
            return kTeamName[t];
    int idx = PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, key.c_str());
    if (idx < 0)
        idx = PortGuestRosterFindAny(PORT_GUEST_SIDEKICK, key.c_str());
    const char* n = idx >= 0 ? PortGuestRosterName(idx) : NULL;
    return n != NULL ? std::string(n) : std::string();
}

// r131: the character a player key is ("" none: no key, a generic Strikers sidekick kind). Cached once its first form
// has a name here (a pack this headset lacks is asked again: the converter may add it).
std::string char_id(const std::string& key)
{
    if (key.empty() || key == "-" || generic_kind(key))
        return std::string();
    static std::unordered_map<std::string, std::string> s_ids;
    std::unordered_map<std::string, std::string>::const_iterator it = s_ids.find(key);
    if (it != s_ids.end())
        return it->second;
    read_data();
    const std::string root = char_root(key);
    const std::string name = char_name(root);
    std::string id;
    for (size_t i = 0; i < name.size(); i++)
    {
        const char ch = name[i];
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            id += ch;
        else if (ch >= 'A' && ch <= 'Z')
            id += (char)(ch - 'A' + 'a');
    }
    if (id.empty())
        return root;
    id = "n:" + id;
    s_ids[key] = id;
    return id;
}

// r131: a Charged sidekick guest (Boo ...): a character, but it may fill several of one club's sidekick slots (as a club's
// kind does)
bool sk_guest(const std::string& key)
{
    return !key.empty() && PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, key.c_str()) < 0 &&
           PortGuestRosterFindAny(PORT_GUEST_SIDEKICK, key.c_str()) >= 0;
}

// r131: a character select menu pick as a player key: strikers_<name>, a guest's pack ("" a Strikers sidekick, none)
std::string pick_key(int pick)
{
    if (pick >= PORT_GUEST_ORIGINAL && pick < PORT_GUEST_ORIGINAL + 8)
        return std::string("strikers_") + kTeamKey[pick - PORT_GUEST_ORIGINAL];
    if (pick >= 0 && pick < PORT_GUEST_ORIGINAL)
    {
        const char* p = PortGuestRosterPack(pick);
        return p != NULL ? std::string(p) : std::string();
    }
    return std::string();
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
    // r165 (Lance: "post season success or failure doesnt make or break someones seasonal awards, it felt like whoever
    // made it furthest in the playoffs won the most awards"): the regular season's goals and form alone, for awards night
    int regGoals, regForm;
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
    // r130 (Lance: "you can have anybody play a sidekick now ... 4 different levels for 4 different people for 4 different
    // rewards"): each sidekick slot a member of its own: "" the club's generic sidekick of `kind` (names[k], the group's
    // level), else a character key (a Pokemon, a hero, a Melee fighter, a Charged character) with its own level, its own
    // evolutions / costumes and its own shiny (mp[k])
    std::string mem[3];
    Prog mp[3];
    int streak;                   // r71: + wins in a row / - losses in a row
    // r167 (EVOLUTION SELECTOR): each slot's highest form reached when it plays as an earlier one (top[slot]; "" = the one it
    // plays as, its key). Slots as slot_key: 0 captain, 1 backup, 2-4 members. Only a person's club sets it (top_of checks it)
    std::string top[5];
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

// r167: FREE AGENCY (franchise.h). A free agent: a character on no club, with its own level, record and personality.
struct FreeAgent
{
    std::string key;     // the form it is (a released Charizard stays a Charizard)
    Prog prog;
    PStats st;
    int from = -1;       // the club that released it (-1 never on one)
    int since = 0;       // the season it became free
    bool fresh = true;   // never on a club in this career: it joins at its spot's level (as r131's signings)
    int decide = 0;      // mid-season: decides at the end of this week (0: no offers yet)
};
// An offer: a club's role for an agent (PORT_FA_*), a backup's promised starts, a captain offer that releases the captain
// (else the captain goes to the bench and the backup is released); the week it was made.
struct FaBid
{
    std::string key;
    int club = -1, role = 0;
    bool promise = false, relCap = false;
    int week = 0;
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
    // r167: free agency - the free agents, the offers out, the last CPU bidding round (season * 100 + its week, 99 the
    // off-season's), your last result line
    std::vector<FreeAgent> fa;
    std::vector<FaBid> bids;
    int faRound = 0;
    std::string faLast;
    // r177 (Lance: retirements so every off-season has a real rookie class): by character (char_id) - who a person's club
    // got this season (never retired that season), who was told "thinking about retirement" (a person's own retire the
    // off-season after), lines retired that come back as rookies (the season they return), rookies by the season they
    // were rookies in (Rookie of the Year: any role)
    std::vector<std::pair<std::string, int> > joined;
    std::vector<std::string> retireWarn;
    std::vector<std::pair<std::string, int> > returning;
    std::vector<std::pair<std::string, int> > rookieOf;
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

// r146 (Lance: "bonuses for duplicates should you not trade em in as well as in game multipliers due to set bonuses"):
// the LANCINI TIZM card bonuses xr_cards.cpp works out from the collection (PortFranchiseSetCardBonuses), by card key.
// Only in a career of this headset's own (never co-op or an online league: the other headsets have other collections),
// only your club's players; every XP your players get (award_xp, personal_xp, the live meter) times (1 + its percent), a
// line's evolutions' shiny odds, the coins a played match pays (card_coins_earned).
struct CardBonus
{
    int xpPct = 0, shinyOdds = 0;
    std::string why;
};
std::unordered_map<std::string, CardBonus> s_cardBonus;
int s_cardCoinPct = 0;
std::string s_cardCoinPctWhy;
bool card_bonus_on(const Career& c) { return !c.coop && c.league.empty(); }
// a player's card: its own key, a Strikers sidekick kind's ("toad": strikers_toad), r130's first-take "cap_<name>"
const CardBonus* card_bonus(const std::string& key)
{
    if (key.empty())
        return NULL;
    std::unordered_map<std::string, CardBonus>::const_iterator it = s_cardBonus.find(key);
    if (it == s_cardBonus.end())
        it = s_cardBonus.find(key.compare(0, 4, "cap_") == 0 ? "strikers_" + key.substr(4) : "strikers_" + key);
    return it != s_cardBonus.end() ? &it->second : NULL;
}
int card_xp_pct(const Career& c, int club, const std::string& key)
{
    if (!card_bonus_on(c) || club != c.myClub)
        return 0;
    const CardBonus* b = card_bonus(key);
    return b != NULL ? std::max(0, std::min(60, b->xpPct)) : 0;
}
// `xp` as that player of that club gets it (its card bonus on top)
int card_xp(const Career& c, int club, const std::string& key, int xp)
{
    const int pct = card_xp_pct(c, club, key);
    return pct > 0 && xp > 0 ? xp + (xp * pct + 50) / 100 : xp;
}

// r131: the characters a club holds: its captain and backup, its sidekick members, a Charged sidekick guest as its kind
void club_ids(const ClubState& s, std::vector<std::string>& out)
{
    const std::string* keys[6] = {&s.captain, &s.backup, &s.mem[0], &s.mem[1], &s.mem[2], &s.kind};
    for (int k = 0; k < 6; k++)
    {
        const std::string id = char_id(*keys[k]);
        if (!id.empty())
            out.push_back(id);
    }
}

// r131: the club holding character `id` other than `except` (-1 none)
int holder_of(const Career& c, const std::string& id, int except)
{
    if (id.empty())
        return -1;
    std::vector<std::string> ids;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        if (i == except)
            continue;
        ids.clear();
        club_ids(c.clubs[i], ids);
        if (std::find(ids.begin(), ids.end(), id) != ids.end())
            return i;
    }
    return -1;
}

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
    s.regGoals = s.regForm = 0;  // r165
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
        if (!s.mem[0].empty() || !s.mem[1].empty() || !s.mem[2].empty())  // r130: its sidekick members
        {
            fprintf(f, "mem %d %s %s %s\n", i, s.mem[0].empty() ? "-" : s.mem[0].c_str(), s.mem[1].empty() ? "-" : s.mem[1].c_str(),
                    s.mem[2].empty() ? "-" : s.mem[2].c_str());
            fprintf(f, "mprog %d", i);
            for (int k = 0; k < 3; k++)
                fprintf(f, " %d %d %d %d %d", s.mp[k].level, s.mp[k].xp, s.mp[k].shiny ? 1 : 0, s.mp[k].declinedAt, s.mp[k].startLevel);
            fprintf(f, "\n");
        }
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
        fprintf(f, "lifereg %d", i);  // r165: the regular season's goals and form (awards night)
        for (int k = 0; k < 5; k++)
            fprintf(f, " %d %d", st[k]->regGoals, st[k]->regForm);
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
    // r167: the forms played below the highest reached ("top <club> <slot> <highest>"), free agency (careers from before
    // r167 have none of these: every player plays its highest form, nobody is free until the window refreshes the pool)
    for (int i = 0; i < PORT_FR_CLUBS; i++)
        for (int k = 0; k < 5; k++)
            if (!c.clubs[i].top[k].empty())
                fprintf(f, "top %d %d %s\n", i, k, c.clubs[i].top[k].c_str());
    for (size_t i = 0; i < c.fa.size(); i++)
    {
        const FreeAgent& a = c.fa[i];
        fprintf(f, "fa %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %s\n", a.fresh ? 1 : 0, a.from, a.since, a.decide,
                a.prog.level, a.prog.xp, a.prog.shiny ? 1 : 0, a.prog.declinedAt, a.prog.startLevel, a.st.goals, a.st.games,
                a.st.seasonGoals, a.st.form, a.st.personality, a.st.regGoals, a.st.regForm, a.key.c_str());
    }
    for (size_t i = 0; i < c.bids.size(); i++)
    {
        const FaBid& b = c.bids[i];
        fprintf(f, "fabid %d %d %d %d %d %s\n", b.club, b.role, b.promise ? 1 : 0, b.relCap ? 1 : 0, b.week, b.key.c_str());
    }
    fprintf(f, "faround %d\n", c.faRound);
    for (size_t i = 0; i < c.joined.size(); i++)  // r177
        fprintf(f, "joined %d %s\n", c.joined[i].second, c.joined[i].first.c_str());
    for (size_t i = 0; i < c.retireWarn.size(); i++)
        fprintf(f, "retwarn %s\n", c.retireWarn[i].c_str());
    for (size_t i = 0; i < c.returning.size(); i++)
        fprintf(f, "returning %d %s\n", c.returning[i].second, c.returning[i].first.c_str());
    for (size_t i = 0; i < c.rookieOf.size(); i++)
        fprintf(f, "rookieof %d %s\n", c.rookieOf[i].second, c.rookieOf[i].first.c_str());
    if (!c.faLast.empty())
        fprintf(f, "falast %s\n", c.faLast.c_str());
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
        for (int k = 0; k < 3; k++)  // r130
            init_prog(c.clubs[i].mp[k]);
    }
    init_life(c);
    bool hadLife = false;
    bool hadMprog[PORT_FR_CLUBS] = {};  // r130: older saves: members start at the sidekicks' level
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
        else if (strncmp(line, "mem ", 4) == 0)  // r130
        {
            int i = -1;
            char a[64], b[64], d[64];
            if (sscanf(line + 4, "%d %63s %63s %63s", &i, a, b, d) == 4 && i >= 0 && i < PORT_FR_CLUBS)
            {
                const char* m[3] = {a, b, d};
                for (int k = 0; k < 3; k++)
                    c.clubs[i].mem[k] = strcmp(m[k], "-") == 0 ? std::string() : std::string(m[k]);
            }
        }
        else if (strncmp(line, "mprog ", 6) == 0)  // r130
        {
            int i = -1, v[15];
            if (sscanf(line + 6, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", &i, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                       &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14]) == 16 &&
                i >= 0 && i < PORT_FR_CLUBS)
            {
                for (int k = 0; k < 3; k++)
                {
                    Prog& p = c.clubs[i].mp[k];
                    p.level = v[k * 5];
                    p.xp = v[k * 5 + 1];
                    p.shiny = v[k * 5 + 2] != 0;
                    p.declinedAt = v[k * 5 + 3];
                    p.startLevel = v[k * 5 + 4];
                }
                hadMprog[i] = true;
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
                    st[k]->regGoals = st[k]->seasonGoals;  // r165: a save from before lifereg (its line below wins)
                    st[k]->regForm = st[k]->form;
                }
                hadLife = true;
            }
        }
        else if (strncmp(line, "lifereg ", 8) == 0)  // r165
        {
            int v[11];
            int n = 0;
            const char* p = line + 8;
            while (n < 11)
            {
                char* end = NULL;
                const long x = strtol(p, &end, 10);
                if (end == p)
                    break;
                v[n++] = (int)x;
                p = end;
            }
            if (n == 11 && v[0] >= 0 && v[0] < PORT_FR_CLUBS)
            {
                ClubState& s = c.clubs[v[0]];
                PStats* st[5] = {&s.capSt, &s.bkSt, &s.sk[0], &s.sk[1], &s.sk[2]};
                for (int k = 0; k < 5; k++)
                {
                    st[k]->regGoals = v[1 + k * 2];
                    st[k]->regForm = v[2 + k * 2];
                }
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
                n.title = v[6] >= 0 && v[6] < 1000 ? v[6] : 0;  // (r173: title_name clamps to the table, kTitleDefs below)
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
        else if (strncmp(line, "top ", 4) == 0)  // r167
        {
            int i = -1, k = -1;
            char key[96];
            if (sscanf(line + 4, "%d %d %95s", &i, &k, key) == 3 && i >= 0 && i < PORT_FR_CLUBS && k >= 0 && k < 5)
                c.clubs[i].top[k] = key;
        }
        else if (strncmp(line, "fa ", 3) == 0)  // r167
        {
            int v[16];
            char key[96];
            if (sscanf(line + 3, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %95s", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                       &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], &v[14], &v[15], key) == 17)
            {
                FreeAgent a;
                a.key = key;
                a.fresh = v[0] != 0;
                a.from = v[1] >= 0 && v[1] < PORT_FR_CLUBS ? v[1] : -1;
                a.since = v[2];
                a.decide = v[3];
                init_prog(a.prog);
                a.prog.level = std::max(1, std::min(100, v[4]));
                a.prog.xp = v[5];
                a.prog.shiny = v[6] != 0;
                a.prog.declinedAt = v[7];
                a.prog.startLevel = v[8];
                init_stats(a.st, v[13]);
                a.st.goals = v[9];
                a.st.games = v[10];
                a.st.seasonGoals = v[11];
                a.st.form = v[12];
                a.st.regGoals = v[14];
                a.st.regForm = v[15];
                c.fa.push_back(a);
            }
        }
        else if (strncmp(line, "fabid ", 6) == 0)  // r167
        {
            int club = -1, role = 0, pr = 0, rc = 0, wk = 0;
            char key[96];
            if (sscanf(line + 6, "%d %d %d %d %d %95s", &club, &role, &pr, &rc, &wk, key) == 6 && club >= 0 && club < PORT_FR_CLUBS &&
                role >= 0 && role < PORT_FA_ROLES)
            {
                FaBid b;
                b.key = key;
                b.club = club;
                b.role = role;
                b.promise = pr != 0;
                b.relCap = rc != 0;
                b.week = wk;
                c.bids.push_back(b);
            }
        }
        else if (strncmp(line, "faround ", 8) == 0)  // r167
            c.faRound = atoi(line + 8);
        else if (strncmp(line, "joined ", 7) == 0 || strncmp(line, "returning ", 10) == 0 || strncmp(line, "rookieof ", 9) == 0)  // r177
        {
            const bool j = line[0] == 'j', r = line[0] == 'r' && line[1] == 'e';
            int season = 0;
            char id[128];
            if (sscanf(line + (j ? 7 : (r ? 10 : 9)), "%d %127s", &season, id) == 2)
                (j ? c.joined : (r ? c.returning : c.rookieOf)).push_back(std::make_pair(std::string(id), season));
        }
        else if (strncmp(line, "retwarn ", 8) == 0)  // r177
            c.retireWarn.push_back(line + 8);
        else if (strncmp(line, "falast ", 7) == 0)  // r167
            c.faLast = line + 7;
    }
    fclose(f);
    if (!ok || c.games.empty())
        return false;
    for (int i = 0; i < PORT_FR_CLUBS; i++)  // r130
        if (!hadMprog[i])
            for (int k = 0; k < 3; k++)
                c.clubs[i].mp[k] = c.clubs[i].side;
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
    std::string mem[3];  // r131: your three sidekick members ("" a generic one of your kind)
    bool noBackup = false;
};

// r131: a CPU club's sidekick kind from its members (the class its generic sidekicks, and a member whose captain class is
// taken in a match, play): a Charged sidekick guest member's own class, else the class most of them suit by build (as the
// menu's: Yoshi -> Toad, Mario / Luigi -> Koopa, Wario / DK -> Hammer Bro, Peach / Daisy / Waluigi -> Birdo), else one at
// random. Always a Strikers kind: the Charged sidekick guests are characters now, each on one club.
std::string kind_for_members(Career& c, const std::string mem[3])
{
    static const int kByCaptain[8] = {3, 2, 1, 1, 3, 3, 2, 0};  // eTeamID daisy dk luigi mario peach waluigi wario yoshi
    for (int k = 0; k < 3; k++)
        if (sk_guest(mem[k]))
        {
            const int idx = PortGuestRosterFindAny(PORT_GUEST_SIDEKICK, mem[k].c_str());
            const int b = idx >= 0 ? PortGuestRosterBase(idx) : -1;
            if (b >= 0 && b < 4)
                return kSkKey[b];
        }
    int votes[4] = {0, 0, 0, 0}, first = -1;
    for (int k = 0; k < 3; k++)
    {
        if (mem[k].empty())
            continue;
        int team = -1;
        for (int t = 0; t < 8; t++)
            if (mem[k] == std::string("strikers_") + kTeamKey[t])
                team = t;
        if (team < 0)
        {
            const int idx = PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, mem[k].c_str());
            team = idx >= 0 ? PortGuestRosterBase(idx) : -1;
        }
        if (team < 0 || team >= 8)
            continue;
        votes[kByCaptain[team]]++;
        if (first < 0)
            first = kByCaptain[team];
    }
    if (first < 0)
        return kSkKey[next_rand(c.rng) % 4];
    int cls = first;
    for (int v = 0; v < 4; v++)
        if (votes[v] > votes[cls])
            cls = v;
    return kSkKey[cls];
}

// r70 / r131: a new career's players. r131 (Lance: "you pick first, and the league forms around your picks, never
// duplicating. even if that means mad teams of 4 captains"): every character once (char_id). Yours (when you picked)
// are never dealt; the rest - the unevolved captains (the Strikers ones and every guest's first form) and the Charged
// sidekick guests - go to the other clubs in a snake by rating (up to 5 points of luck, so the tiers differ career to
// career), round by round: the captains, the backup captains (both from the captains), then three sidekick members each
// from anyone left (a club with a co-captain fills the two slots on the field first: its first slot sits). When the pool
// runs out a club has no backup / generic sidekicks; with fewer captains than clubs (a roster with few or no guests) the
// clubs left get a dealt captain again (never one of yours).
void deal(Career& c, const MyPicks& mine = MyPicks())
{
    const std::vector<Captain> caps = captains();
    const std::vector<Kind> ks = kinds();
    const bool picked = !mine.captain.empty();
    std::vector<std::string> seen;  // the characters taken (yours first)
    if (picked)
    {
        const std::string* yours[6] = {&mine.captain, &mine.backup, &mine.mem[0], &mine.mem[1], &mine.mem[2], &mine.kind};
        for (int k = 0; k < 6; k++)
        {
            const std::string id = char_id(*yours[k]);
            if (!id.empty())
                seen.push_back(id);
        }
    }
    struct Cand
    {
        std::string key;
        double score;
        bool captain;  // can be a club's captain (else a Charged sidekick guest: a member only)
    };
    std::vector<Cand> pool;
    auto add = [&](const std::string& key, bool captain) {
        const std::string id = char_id(key);
        if (id.empty() || std::find(seen.begin(), seen.end(), id) != seen.end())
            return;
        seen.push_back(id);
        Cand cd;
        cd.key = key;
        cd.score = rating(key) + 5.0 * rand01(c.rng);
        cd.captain = captain;
        pool.push_back(cd);
    };
    for (size_t i = 0; i < caps.size(); i++)
        if (!caps[i].evolved)  // r70c: careers start with the unevolved forms only
            add(caps[i].key, true);
    for (size_t i = 0; i < ks.size(); i++)
        if (ks[i].guest >= 0)  // (the Strikers kinds are anybody's)
            add(ks[i].key, false);
    std::stable_sort(pool.begin(), pool.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = c.clubs[i];
        s.captain.clear();
        s.backup.clear();
        s.kind.clear();
        s.startsBackup = false;
        s.wins = s.losses = s.goalsFor = s.goalsAgainst = 0;
        init_prog(s.cap);
        init_prog(s.bk);
        init_prog(s.side);
        for (int k = 0; k < 3; k++)  // r130: each member its own level (Lv 8, as everyone starts)
        {
            s.mem[k].clear();
            init_prog(s.mp[k]);
        }
    }
    if (picked)
    {
        ClubState& me = c.clubs[c.myClub];
        me.captain = mine.captain;
        me.backup = mine.noBackup ? std::string() : mine.backup;
        me.kind = mine.kind;
        for (int k = 0; k < 3; k++)
            me.mem[k] = mine.mem[k];
    }
    std::vector<int> order;  // the clubs being dealt, in a random order
    for (int i = 0; i < PORT_FR_CLUBS; i++)
        if (!picked || i != c.myClub)
            order.push_back(i);
    for (int i = (int)order.size() - 1; i > 0; i--)
    {
        const int j = (int)(next_rand(c.rng) % (unsigned)(i + 1));
        std::swap(order[i], order[j]);
    }
    std::vector<bool> used(pool.size(), false);
    std::vector<std::string> dealtCaps;  // (for the repeats)
    int counts[5] = {0, 0, 0, 0, 0}, repeats = 0;
    const int n = (int)order.size();
    for (int round = 0; round < 5; round++)
        for (int j = 0; j < n; j++)
        {
            ClubState& s = c.clubs[order[round % 2 == 0 ? j : n - 1 - j]];  // the snake: the last pick picks first next
            int at = -1;
            for (size_t i = 0; i < pool.size() && at < 0; i++)
                if (!used[i] && (round >= 2 || pool[i].captain))
                    at = (int)i;
            if (at < 0)
            {
                if (round == 0)  // 1.14: fewer captains than clubs: round the dealt ones again
                {
                    s.captain = dealtCaps.empty() ? std::string("strikers_mario") : dealtCaps[repeats % dealtCaps.size()];
                    repeats++;
                }
                continue;
            }
            used[at] = true;
            counts[round]++;
            const std::string& key = pool[at].key;
            if (round == 0)
            {
                s.captain = key;
                dealtCaps.push_back(key);
            }
            else if (round == 1)
                s.backup = key;
            else  // members: with a co-captain (a backup) slots 2 and 3 first, then slot 1 (on the bench)
                s.mem[s.backup.empty() ? round - 2 : (round - 1) % 3] = key;
        }
    for (int j = 0; j < n; j++)
        c.clubs[order[j]].kind = kind_for_members(c, c.clubs[order[j]].mem);
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = c.clubs[i];
        if (s.kind.empty())
            s.kind = ks.empty() ? std::string("toad") : ks[0].key;
        std::vector<std::string> usedNames;  // (the generic names: a member's slot keeps one too, for when it leaves)
        for (int k = 0; k < 3; k++)
            s.names[k] = make_name(c, s.kind, i, k, usedNames);
    }
    fprintf(stderr,
            "[franchise] dealt %d of %d characters to %d clubs: %d captains (+%d again: too few), %d backups, %d + %d + %d "
            "sidekick members\n",
            counts[0] + counts[1] + counts[2] + counts[3] + counts[4], (int)pool.size(), n, counts[0], repeats, counts[1],
            counts[2], counts[3], counts[4]);
    fflush(stderr);
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

// r131: the sidekicks on the field (slot 1 sits when a co-captain plays): each member its own rating and level, a
// generic one the kind's (a club with no members: the kind's, as before)
double sidekick_strength(const ClubState& s)
{
    double sum = 0.0;
    int n = 0;
    for (int k = 0; k < 3; k++)
    {
        if (k == 0 && !s.backup.empty())
            continue;
        sum += s.mem[k].empty() ? rating(s.kind) + 0.25 * (s.side.level - 8) : rating(s.mem[k]) + 0.25 * (s.mp[k].level - 8);
        n++;
    }
    return n > 0 ? sum / n : rating(s.kind) + 0.25 * (s.side.level - 8);
}

// r70c: a quarter point of strength a level over 8 (an evolution brings its own, better card)
double strength(const ClubState& s)
{
    if (s.backup.empty())
        return 0.62 * (rating(starter(s)) + 0.25 * (starter_prog(s).level - 8)) + 0.38 * sidekick_strength(s);  // r131
    // r78: both captains on the field (the co-captain in a sidekick's spot)
    const bool bs = s.startsBackup;
    const std::string& other = bs ? s.captain : s.backup;
    const Prog& otherProg = bs ? s.cap : s.bk;
    return 0.38 * (rating(starter(s)) + 0.25 * (starter_prog(s).level - 8)) +
           0.32 * (rating(other) + 0.25 * (otherProg.level - 8)) + 0.30 * sidekick_strength(s);
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
        // r146: your players each with their card bonus (card_xp: 0 for any other club, co-op, a league)
        const int club = side == 0 ? g.home : g.away;
        gain(s.cap, card_xp(c, club, s.captain, xp));
        if (!s.backup.empty())
            gain(s.bk, card_xp(c, club, s.backup, xp));
        gain(s.side, card_xp(c, club, s.kind, xp));
        for (int k = 0; k < 3; k++)  // r130: each sidekick member its own (a co-captain's slot 1: on the bench, half)
            gain(s.mp[k], card_xp(c, club, s.mem[k].empty() ? s.kind : s.mem[k],
                                  k == 0 && !s.backup.empty() && !s.mem[0].empty() ? xp / 2 : xp));
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
std::string sk_name(const ClubState& s, int k);  // r130: a sidekick slot's name: its member's, else the generic one

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
        const std::string names[kScorerSlots] = {capName, sk_name(s, 0), sk_name(s, 1), sk_name(s, 2),
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
            if (g.series < 0)  // r165: a regular-season game: what awards night counts
            {
                p.regGoals += goals;
                p.regForm = std::max(-3, std::min(3, p.regForm + (won ? 1 : -1) + (goals >= 2 ? 1 : 0)));
            }
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
// r173 (Lance: "you gave ppl like 2 names in all my franchises. its 'the avenger' or 'the giant slayer' ... there need to
// be wayyyy more titles"): a title is a storyline. Each belongs to one (TitleCat) with a strength; a beef record gets the
// strongest storyline its game told (a final knockout, a five-goal game, a Super Strike, a shutout, three straight wins
// over you, a hit parade, a golden goal ... down to its personality and its element / power), one of that storyline's
// titles not already held in the league this season (variety), the rare storylines' titles only for rare feats. A stronger
// story later re-titles an active nemesis ("NEMESIS X IS NOW THE ..."). Awards night names its winners too (the hall of
// fame, the news). The first 11 keep their old numbers (saved careers' "nem" lines hold the index).
enum TitleCat
{
    TC_GRUDGE, TC_KNOCKOUT, TC_KNOCKOUT_FINAL, TC_GIANT, TC_HAT3, TC_HAT4, TC_HAT5, TC_SS1, TC_SS2, TC_ENFORCER, TC_ENFORCER4,
    TC_BLOWOUT, TC_CLUTCH, TC_GOLDEN, TC_SHUTOUT, TC_CURSE, TC_REVENGE, TC_FIRE, TC_ROOKIE, TC_LEGEND, TC_TRAITOR, TC_EVOLVED,
    TC_SCORER, TC_P_HOTHEAD, TC_P_SHOWBOAT, TC_P_SILENT, TC_P_HONORABLE, TC_P_TRICKSTER, TC_P_UNDERDOG, TC_P_VETERAN,
    TC_P_FRONTRUNNER, TC_E_INFERNO, TC_E_THUNDER, TC_E_BOULDER, TC_E_TIDAL, TC_E_PHANTOM, TC_E_CYCLONE, TC_E_GLACIER, TC_E_BLOOM,
    TC_AW_MVP, TC_AW_BOOT, TC_AW_SIXTH, TC_AW_RECORD, TC_COUNT
};
// how strong a story each is (a re-title needs a stronger one; awards' only matter among themselves)
const int kTitleCatStrength[TC_COUNT] = {10, 100, 110, 72, 70, 88, 95, 60, 85, 58, 80, 75, 50, 62, 65, 68, 50, 45, 40, 105, 90,
                                         90, 55, 30, 30, 30, 30, 30, 30, 30, 30, 25, 25, 25, 25, 25, 25, 25, 25, 0, 0, 0, 0};
struct TitleDef
{
    const char* name;
    int cat;
};
const TitleDef kTitleDefs[] = {
    // (r72's eleven, their old numbers)
    {"THE GRUDGE", TC_GRUDGE}, {"THE PLAYOFF KILLER", TC_KNOCKOUT}, {"THE GIANT SLAYER", TC_GIANT}, {"THE ENFORCER", TC_ENFORCER},
    {"THE TORMENTOR", TC_BLOWOUT}, {"THE SHOWSTOPPER", TC_SS1}, {"THE GHOST", TC_P_SILENT}, {"THE AVENGER", TC_EVOLVED},
    {"THE MENACE", TC_P_HOTHEAD}, {"THE SNIPER", TC_HAT3}, {"THE TRAITOR", TC_TRAITOR},
    // r173
    {"THE THORN IN YOUR SIDE", TC_GRUDGE}, {"THE BAD BLOOD", TC_GRUDGE}, {"THE SWORN RIVAL", TC_GRUDGE}, {"THE OLD SCORE", TC_GRUDGE},
    {"THE LONG MEMORY", TC_GRUDGE},
    {"THE GUILLOTINE", TC_KNOCKOUT}, {"THE DREAM CRUSHER", TC_KNOCKOUT}, {"THE GATEKEEPER", TC_KNOCKOUT},
    {"THE SEASON ENDER", TC_KNOCKOUT}, {"THE HEARTBREAKER", TC_KNOCKOUT},
    {"THE CROWN THIEF", TC_KNOCKOUT_FINAL}, {"THE RING STEALER", TC_KNOCKOUT_FINAL}, {"THE LAST WORD", TC_KNOCKOUT_FINAL},
    {"THE UPSET ARTIST", TC_GIANT}, {"THE DARK HORSE", TC_GIANT}, {"THE SPOILER", TC_GIANT}, {"THE SLINGSHOT", TC_GIANT},
    {"THE LONG SHOT", TC_GIANT},
    {"THE HAT-TRICK HERO", TC_HAT3}, {"THE MARKSMAN", TC_HAT3}, {"THE TRIGGERMAN", TC_HAT3}, {"THE NET BUSTER", TC_HAT3},
    {"THE SHARPSHOOTER", TC_HAT3},
    {"THE ONE-MAN ARMY", TC_HAT4}, {"THE SCORING MACHINE", TC_HAT4}, {"THE GOAL RUSH", TC_HAT4},
    {"THE AVALANCHE", TC_HAT5}, {"THE FIVE-ALARM FIRE", TC_HAT5},
    {"THE THUNDERCLAP", TC_SS1}, {"THE SKYFALL", TC_SS1}, {"THE BOOMSTICK", TC_SS1}, {"THE FIREWORKS", TC_SS1},
    {"THE MEGATON", TC_SS2}, {"THE DOUBLE STRIKE", TC_SS2},
    {"THE WRECKING BALL", TC_ENFORCER}, {"THE BULLDOZER", TC_ENFORCER}, {"THE HAMMER", TC_ENFORCER}, {"THE BRUISER", TC_ENFORCER},
    {"THE AFTERSHOCK", TC_ENFORCER4}, {"THE EARTHQUAKE", TC_ENFORCER4},
    {"THE BULLY", TC_BLOWOUT}, {"THE NIGHTMARE", TC_BLOWOUT}, {"THE STEAMROLLER", TC_BLOWOUT}, {"THE BLOWTORCH", TC_BLOWOUT},
    {"THE CLOSER", TC_CLUTCH}, {"THE DAGGER", TC_CLUTCH}, {"THE ICE VEINS", TC_CLUTCH}, {"THE FINAL SAY", TC_CLUTCH},
    {"THE GOLDEN GOAL", TC_GOLDEN}, {"THE SUDDEN DEATH", TC_GOLDEN}, {"THE EXTRA-TIME HERO", TC_GOLDEN}, {"THE OVERTIME THIEF", TC_GOLDEN},
    {"THE WALL", TC_SHUTOUT}, {"THE FORTRESS", TC_SHUTOUT}, {"THE VAULT", TC_SHUTOUT}, {"THE LOCKDOWN", TC_SHUTOUT},
    {"THE CURSE", TC_CURSE}, {"THE JINX", TC_CURSE}, {"THE HAUNTING", TC_CURSE}, {"THE BOOGEYMAN", TC_CURSE},
    {"THE ACHILLES HEEL", TC_CURSE},
    {"THE VENDETTA", TC_REVENGE}, {"THE RECKONING", TC_REVENGE}, {"THE PAYBACK", TC_REVENGE}, {"THE COMEBACK KID", TC_REVENGE},
    {"THE HEAT WAVE", TC_FIRE}, {"THE HOT HAND", TC_FIRE}, {"THE WILDFIRE", TC_FIRE}, {"THE SUPERNOVA", TC_FIRE},
    {"THE ROOKIE SENSATION", TC_ROOKIE}, {"THE NEW KID", TC_ROOKIE}, {"THE PRODIGY", TC_ROOKIE}, {"THE PHENOM", TC_ROOKIE},
    {"THE FINAL BOSS", TC_LEGEND}, {"THE LEGEND KILLER", TC_LEGEND}, {"THE ETERNAL RIVAL", TC_LEGEND}, {"THE ARCHRIVAL", TC_LEGEND},
    {"THE TURNCOAT", TC_TRAITOR}, {"THE MUTINEER", TC_TRAITOR}, {"THE DEFECTOR", TC_TRAITOR}, {"THE SNAKE", TC_TRAITOR},
    {"THE REBORN", TC_EVOLVED}, {"THE ASCENDED", TC_EVOLVED}, {"THE AWAKENED", TC_EVOLVED}, {"THE NEW BREED", TC_EVOLVED},
    {"THE POACHER", TC_SCORER}, {"THE OPPORTUNIST", TC_SCORER}, {"THE SUPER SUB", TC_SCORER}, {"THE FOX IN THE BOX", TC_SCORER},
    {"THE LIVE WIRE", TC_P_HOTHEAD}, {"THE SPARK", TC_P_HOTHEAD}, {"THE SHORT FUSE", TC_P_HOTHEAD},
    {"THE SHOWMAN", TC_P_SHOWBOAT}, {"THE HEADLINER", TC_P_SHOWBOAT}, {"THE MAIN EVENT", TC_P_SHOWBOAT},
    {"THE SILENT STORM", TC_P_SILENT}, {"THE WHISPER", TC_P_SILENT}, {"THE NIGHTSHADE", TC_P_SILENT},
    {"THE NOBLE FOE", TC_P_HONORABLE}, {"THE HONOR GUARD", TC_P_HONORABLE}, {"THE WORTHY RIVAL", TC_P_HONORABLE},
    {"THE TRICKSTER", TC_P_TRICKSTER}, {"THE ILLUSIONIST", TC_P_TRICKSTER}, {"THE PRANKSTER", TC_P_TRICKSTER},
    {"THE MIRAGE", TC_P_TRICKSTER},
    {"THE UNDERDOG", TC_P_UNDERDOG}, {"THE SCRAPPER", TC_P_UNDERDOG}, {"THE GRINDER", TC_P_UNDERDOG},
    {"THE OLD GUARD", TC_P_VETERAN}, {"THE IRON VETERAN", TC_P_VETERAN}, {"THE LAST STAND", TC_P_VETERAN},
    {"THE FRONT-RUNNER", TC_P_FRONTRUNNER}, {"THE TABLE TOPPER", TC_P_FRONTRUNNER}, {"THE FAVORITE", TC_P_FRONTRUNNER},
    {"THE INFERNO", TC_E_INFERNO}, {"THE FLAMETHROWER", TC_E_INFERNO}, {"THE FIREBRAND", TC_E_INFERNO},
    {"THE STORMFRONT", TC_E_THUNDER}, {"THE LIVE CURRENT", TC_E_THUNDER}, {"THE STORM CALLER", TC_E_THUNDER},
    {"THE LANDSLIDE", TC_E_BOULDER}, {"THE IRONSIDE", TC_E_BOULDER}, {"THE ROCKSLIDE", TC_E_BOULDER},
    {"THE RIPTIDE", TC_E_TIDAL}, {"THE UNDERTOW", TC_E_TIDAL}, {"THE TIDEBREAKER", TC_E_TIDAL},
    {"THE POLTERGEIST", TC_E_PHANTOM}, {"THE APPARITION", TC_E_PHANTOM}, {"THE ENIGMA", TC_E_PHANTOM},
    {"THE TYPHOON", TC_E_CYCLONE}, {"THE TALON", TC_E_CYCLONE}, {"THE SKYBREAKER", TC_E_CYCLONE},
    {"THE BLIZZARD", TC_E_GLACIER}, {"THE DEEP FREEZE", TC_E_GLACIER}, {"THE FROSTBITE", TC_E_GLACIER},
    {"THE THORN", TC_E_BLOOM}, {"THE WILD VINE", TC_E_BLOOM}, {"THE WILDCAT", TC_E_BLOOM},
    {"THE FRANCHISE", TC_AW_MVP}, {"THE ENGINE", TC_AW_MVP}, {"THE CORNERSTONE", TC_AW_MVP}, {"THE PRIME MOVER", TC_AW_MVP},
    {"THE GOAL MACHINE", TC_AW_BOOT}, {"THE NET SHREDDER", TC_AW_BOOT}, {"THE GOLDEN FOOT", TC_AW_BOOT}, {"THE FINISHER", TC_AW_BOOT},
    {"THE SPARK PLUG", TC_AW_SIXTH}, {"THE GLUE", TC_AW_SIXTH}, {"THE SIXTH STAR", TC_AW_SIXTH},
    {"THE RECORD BREAKER", TC_AW_RECORD}, {"THE HISTORY MAKER", TC_AW_RECORD},
};
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
    T_COUNT = (int)(sizeof kTitleDefs / sizeof kTitleDefs[0]),
};
const char* title_name(int t) { return kTitleDefs[t >= 0 && t < T_COUNT ? t : 0].name; }
int title_cat(int t) { return kTitleDefs[t >= 0 && t < T_COUNT ? t : 0].cat; }
std::string fa_home(const std::string& key, int* club);  // r167 (below): the element / power division a title can come from
unsigned fa_hash(const Career& c, const std::string& key, int club, int salt);

// r173: a title from the strongest storyline in `cats` (strongest first) with one free this season (not held by another
// beef record active or started this season); every one of them taken: the strongest storyline's, by the hash
int pick_title(const Career& c, const std::string& key, std::vector<int> cats)
{
    if (cats.empty())
        cats.push_back(TC_GRUDGE);
    std::vector<int> taken;
    for (size_t i = 0; i < c.nems.size(); i++)
        if (c.nems[i].key != key && (c.nems[i].active || c.nems[i].since == c.season))
            taken.push_back(c.nems[i].title);
    for (int pass = 0; pass < 2; pass++)
        for (size_t ci = 0; ci < cats.size(); ci++)
        {
            std::vector<int> opts;
            for (int t = 0; t < T_COUNT; t++)
                if (kTitleDefs[t].cat == cats[ci] && (pass == 1 || std::find(taken.begin(), taken.end(), t) == taken.end()))
                    opts.push_back(t);
            if (!opts.empty())
                return opts[fa_hash(c, key, cats[ci], 173) % opts.size()];
            if (pass == 1)
                break;  // (the second pass: the strongest storyline's, held or not)
        }
    return T_GRUDGE;
}
// the storylines a character always has: its personality's, its element's / power's (weakest), then a plain grudge
void flavour_cats(const std::string& key, int personality, std::vector<std::pair<int, int> >& ev)
{
    if (personality >= 0 && personality < PERS_COUNT)
        ev.push_back(std::make_pair(30, TC_P_HOTHEAD + personality));
    int club = -1;
    const std::string div = key.compare(0, 3, "sk#") == 0 ? std::string() : fa_home(key, &club);
    static const char* const kDiv[8] = {"Inferno", "Thunder", "Boulder", "Tidal", "Phantom", "Cyclone", "Glacier", "Bloom"};
    for (int d = 0; d < 8; d++)
        if (div == kDiv[d])
            ev.push_back(std::make_pair(25, TC_E_INFERNO + d));
    ev.push_back(std::make_pair(10, TC_GRUDGE));
}
std::vector<int> ranked_cats(std::vector<std::pair<int, int> > ev)
{
    std::stable_sort(ev.begin(), ev.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first > b.first; });
    std::vector<int> out;
    for (size_t i = 0; i < ev.size(); i++)
        out.push_back(ev[i].second);
    return out;
}
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

// r114 (Lance: "can we turn up the nemesis system for the second difficulty ... 14 games into a season on the
// first save and i have yet to encounter a nemesis interaction"): the beef it takes to become your nemesis and
// to move on, by difficulty (Rookie as it was: 35 / 20; Pro 26 / 12, Superstar 22 / 10, Legend 18 / 8).
int nem_threshold(const Career& c)
{
    static const int k[4] = {35, 26, 22, 18};
    return k[diff_k(c)];
}
int nem_cool_below(const Career& c)
{
    static const int k[4] = {20, 12, 10, 8};
    return k[diff_k(c)];
}

// The beef grows (or cools); past nem_threshold (35 on Rookie) they become your nemesis, with a title from how
// it started.
void add_heat(Career& c, Nemesis& n, int delta, int titleHint, const std::string& why)
{
    n.heat = std::max(0, std::min(100, n.heat + delta));
    if (!n.active && n.heat >= nem_threshold(c))
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
            c.news.push_back("NEMESIS: " + upper(n.name) + " " + title_name(n.title) + " (" + place + ") HAS BEEF WITH " + you);
        nem_log(n, "SEASON " + std::to_string(c.season) + ": BECAME YOUR NEMESIS (" + why + ")");
        fprintf(stderr, "[franchise] nemesis: %s %s (%s)\n", n.name.c_str(), title_name(n.title), why.c_str());
        fflush(stderr);
    }
    else if (n.active && n.heat < nem_cool_below(c))
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
    int d = 0;
    // r173: the game's storylines (strength, TitleCat), the title comes from the strongest (pick_title, below)
    std::vector<std::pair<int, int> > ev;
    std::string why;
    if (margin > 0)
    {
        d += 12 + 4 * std::min(margin, 4);
        why = "BEAT YOU " + std::to_string(theirs) + "-" + std::to_string(mine);
        if ((seed_of(c, opp) > seed_of(c, me) && seed_of(c, me) > 0) || strength(os) + 3.0 < strength(c.clubs[me]))
            ev.push_back(std::make_pair(kTitleCatStrength[TC_GIANT], TC_GIANT));
        if (margin >= 4)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_BLOWOUT], TC_BLOWOUT));
        if (mine == 0)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_SHUTOUT], TC_SHUTOUT));
        if (g.overtime)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_GOLDEN], TC_GOLDEN));
        else if (margin == 1)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_CLUTCH], TC_CLUTCH));
        if (n.last[0] > 0 && n.last[0] != 99 && n.last[1] > 0 && n.last[1] != 99)  // (with this one: three straight over you)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_CURSE], TC_CURSE));
        if (n.rank >= 4 || n.beatYou + 1 >= 8)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_LEGEND], TC_LEGEND));
    }
    else if (-margin >= 3)
    {
        d += 15;
        why = "YOU HUMILIATED THEM " + std::to_string(mine) + "-" + std::to_string(theirs);
        ev.push_back(std::make_pair(kTitleCatStrength[TC_REVENGE], TC_REVENGE));
    }
    else
        why = "LOST TO YOU " + std::to_string(mine) + "-" + std::to_string(theirs);
    {
        const int goals = scorers[o][0];
        if (goals >= 5)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_HAT5], TC_HAT5));
        else if (goals >= 4)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_HAT4], TC_HAT4));
        else if (goals >= 3)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_HAT3], TC_HAT3));
        if (ss[o] >= 2)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_SS2], TC_SS2));
        else if (ss[o] >= 1)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_SS1], TC_SS1));
        if (capHits[o] >= 4)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_ENFORCER4], TC_ENFORCER4));
        else if (capHits[o] >= 2)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_ENFORCER], TC_ENFORCER));
        if (ost.form >= 3)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_FIRE], TC_FIRE));
        if (c.season >= 2 && ost.games <= PORT_FR_WEEKS)  // (new to the league this season: a signing, a newcomer)
            ev.push_back(std::make_pair(kTitleCatStrength[TC_ROOKIE], TC_ROOKIE));
    }
    if (margin == 1 || margin == -1 || g.overtime)
        d += 6;  // a real contest
    // r114: on Pro and up the clubs you beat don't forget either (+8 Pro, +10 Superstar, +12 Legend)
    if (margin < 0 && diff_k(c) > 0)
        d += 6 + 2 * diff_k(c);
    d += 8 * ss[m];
    d += 4 * capHits[o] + 3 * capHits[m] + anyHits[o];
    if (knockedYouOut)
    {
        d += 30;
        const bool final = c.series[g.series].round == 4;
        ev.push_back(std::make_pair(kTitleCatStrength[final ? TC_KNOCKOUT_FINAL : TC_KNOCKOUT], final ? TC_KNOCKOUT_FINAL : TC_KNOCKOUT));
        why = final ? "BEAT YOU IN THE SUPER STRIKE SERIES" : "KNOCKED YOU OUT OF THE PLAYOFFS";
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
    // r173: the title from the game's strongest storyline (its personality / element / a grudge when nothing big happened)
    const int topStory = ev.empty() ? 0 : std::max_element(ev.begin(), ev.end())->first;
    flavour_cats(key, ost.personality, ev);
    const int hint = pick_title(c, key, ranked_cats(ev));
    add_heat(c, n, d, hint, why);
    // r173: an active nemesis with a bigger story than its title's gets the new one (never down)
    if (wasActive && n.active && topStory >= 58 && topStory > kTitleCatStrength[title_cat(n.title)])
    {
        n.title = hint;
        const std::string whose = c.league.empty() ? std::string("YOUR NEMESIS ") : upper(s_clubs[me].place) + "'S NEMESIS ";
        c.news.push_back(whose + upper(n.name) + " IS NOW " + title_name(n.title));
        nem_log(n, "SEASON " + std::to_string(c.season) + ": NOW " + title_name(n.title) + " (" + why + ")");
    }
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
            const std::string sk = sk_name(os, k - 1);  // r131: a member by its own name
            Nemesis& sn = nemesis_for(c, "sk#" + std::to_string(opp) + "#" + sk, sk, opp, false);
            s_metThisWeek.push_back(sn.key);
            // r173: its storyline: a hat trick on you, else a scorer's; its personality, its element (a member's own key)
            std::vector<std::pair<int, int> > sev;
            sev.push_back(scorers[o][k] >= 3 ? std::make_pair(kTitleCatStrength[TC_HAT3], (int)TC_HAT3)
                                             : std::make_pair(kTitleCatStrength[TC_SCORER], (int)TC_SCORER));
            flavour_cats(os.mem[k - 1].empty() ? sn.key : os.mem[k - 1], os.sk[k - 1].personality, sev);
            add_heat(c, sn, 8 * scorers[o][k], pick_title(c, sn.key, ranked_cats(sev)),
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
        add_heat(c, n, d, n.active ? n.title : pick_title(c, n.key, {TC_TRAITOR}), "BENCHED WHILE YOU LOSE");  // r173
    }
    const PStats& cap = s.startsBackup && !s.backup.empty() ? s.bkSt : s.capSt;
    for (int k = 0; k < 3; k++)
    {
        const bool wantsSpot = s.sk[k].seasonGoals >= cap.seasonGoals + 3 && s.sk[k].form >= 2;
        const std::string skn = sk_name(s, k);  // r131: a member by its own name
        if (!wantsSpot && !find_nemesis(c, "sk#" + std::to_string(me) + "#" + skn))
            continue;
        Nemesis& n = nemesis_for(c, "sk#" + std::to_string(me) + "#" + skn, skn, me, true);
        add_heat(c, n, wantsSpot ? 3 : -3, n.active ? n.title : pick_title(c, n.key, {TC_TRAITOR}), "WANTS YOUR CAPTAIN'S SPOT");  // r173
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

// r114: on Pro and up a rivalry doesn't wait all season: past week 3 (Superstar 2, Legend 1) with no nemesis on,
// the hottest beef calls you out (the one you play within 3 weeks first) and becomes your nemesis.
void nemesis_callout(Career& c)
{
    const int k = diff_k(c);
    if (k == 0 || c.phase != PHASE_REGULAR || c.week < 4 - k)
        return;
    const int me = nem_me(c);
    for (size_t i = 0; i < c.nems.size(); i++)
        if (c.nems[i].active && !c.nems[i].teammate && nem_owner(c, c.nems[i]) == me)
            return;
    std::vector<int> soon(PORT_FR_CLUBS, 999);  // the next week each club plays you
    for (size_t i = 0; i < c.games.size(); i++)
    {
        const Game& g = c.games[i];
        if (g.played || g.week <= c.week || (g.home != me && g.away != me))
            continue;
        const int o = g.home == me ? g.away : g.home;
        soon[o] = std::min(soon[o], g.week);
    }
    Nemesis* best = NULL;
    int bestScore = 0;
    for (size_t i = 0; i < c.nems.size(); i++)
    {
        Nemesis& n = c.nems[i];
        if (n.teammate || n.club < 0 || n.club >= PORT_FR_CLUBS || nem_owner(c, n) != me || n.heat <= 0)
            continue;
        const int score = n.heat + (soon[n.club] - c.week <= 3 ? 20 : 0);
        if (score > bestScore)
        {
            best = &n;
            bestScore = score;
        }
    }
    if (best == NULL)
        return;
    add_heat(c, *best, nem_threshold(c) - best->heat, best->title, "CALLED YOU OUT");
}

// Every week: those you didn't meet cool off a little (the history stays). r114: half as fast on Pro and up.
void nemesis_week(Career& c)
{
    for (size_t i = 0; i < c.nems.size(); i++)
    {
        Nemesis& n = c.nems[i];
        if (n.teammate || std::find(s_metThisWeek.begin(), s_metThisWeek.end(), n.key) != s_metThisWeek.end())
            continue;
        if (n.heat > 0)
            add_heat(c, n, diff_k(c) > 0 ? -1 : -2, n.title, "");
    }
    s_metThisWeek.clear();
    // r114: every person's club (an online league's each in turn)
    for (int club = 0; club < PORT_FR_CLUBS; club++)
    {
        if (!is_human(c, club))
            continue;
        s_nemMe = club;
        nemesis_callout(c);
        s_nemMe = -1;
    }
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
        std::swap(x.mem[k], y.mem[k]);  // r131: the members go with their slots (their records are the slot's, sk[k])
        std::swap(x.mp[k], y.mp[k]);
    }
}

std::string kind_name(const std::string& key);
int fa_value(const std::string& key, const Prog& p);  // r167 (below)

// r173: one sidekick member of club a (slot ka) for one of club b (slot kb): the character, its level, its record, its
// seasons; its beef records ("sk#club#name") follow it to its new club
void swap_member_slots(Career& c, int a, int ka, int b, int kb)
{
    ClubState& x = c.clubs[a];
    ClubState& y = c.clubs[b];
    const std::string nx = name_of(x.mem[ka]), ny = name_of(y.mem[kb]);
    each_nemesis(c, "sk#" + std::to_string(a) + "#" + nx, [&](Nemesis* n) {
        n->key = "sk#" + std::to_string(b) + "#" + nx;
        n->club = b;
    });
    each_nemesis(c, "sk#" + std::to_string(b) + "#" + ny, [&](Nemesis* n) {
        n->key = "sk#" + std::to_string(a) + "#" + ny;
        n->club = a;
    });
    std::swap(x.mem[ka], y.mem[kb]);
    std::swap(x.mp[ka], y.mp[kb]);
    std::swap(x.sk[ka], y.sk[kb]);
    std::swap(x.skAge[ka], y.skAge[kb]);
    x.top[2 + ka].clear();
    y.top[2 + kb].clear();
}

// r131: a sidekick trio's trade value (each slot: a member's rating + a point a level over 8, a generic one the kind's;
// a club with no members: the kind's, as r77) and its name ("Toads", or its members': "Pikachu / Boo / Toads")
int trio_value(const ClubState& s)
{
    int sum = 0;
    for (int k = 0; k < 3; k++)
        sum += s.mem[k].empty() ? rating(s.kind) + std::max(0, s.side.level - 8) : rating(s.mem[k]) + std::max(0, s.mp[k].level - 8);
    return (sum + 1) / 3;
}

bool same_trio(const ClubState& a, const ClubState& b)
{
    return a.kind == b.kind && a.mem[0] == b.mem[0] && a.mem[1] == b.mem[1] && a.mem[2] == b.mem[2];
}

std::string trio_name(const ClubState& s)
{
    if (s.mem[0].empty() && s.mem[1].empty() && s.mem[2].empty())
        return kind_name(s.kind);
    std::string out;
    bool generic = false;
    for (int k = 0; k < 3; k++)
    {
        if (s.mem[k].empty())
        {
            generic = true;
            continue;
        }
        out += (out.empty() ? "" : " / ") + name_of(s.mem[k]);
    }
    return generic ? out + " / " + kind_name(s.kind) : out;
}

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
    // r173 (Lance: sidekicks one at a time, "not as a fuckin bunch deal"): a CPU club trades one named sidekick for another
    // club's - each valued on its own (rating + a point a level over 8), within 10% of each other - never the whole trio
    if (rand01(c.rng) < 0.12)
    {
        int a = (int)(next_rand(c.rng) % PORT_FR_CLUBS), b = (int)(next_rand(c.rng) % PORT_FR_CLUBS);
        const int ka = (int)(next_rand(c.rng) % 3), kb = (int)(next_rand(c.rng) % 3);
        if (a != b && !is_human(c, a) && !is_human(c, b) && !c.clubs[a].mem[ka].empty() && !c.clubs[b].mem[kb].empty())
        {
            const int va = fa_value(c.clubs[a].mem[ka], c.clubs[a].mp[ka]), vb = fa_value(c.clubs[b].mem[kb], c.clubs[b].mp[kb]);
            if (va * 10 >= vb * 9 && vb * 10 >= va * 9)
            {
                const std::string na = name_of(c.clubs[a].mem[ka]), nb = name_of(c.clubs[b].mem[kb]);
                swap_member_slots(c, a, ka, b, kb);
                c.news.push_back("TRADE: " + upper(s_clubs[a].place) + " SEND SIDEKICK " + upper(na) + " TO " + upper(s_clubs[b].place) +
                                 " FOR " + upper(nb));
                post(c, c.clubs[b].sk[kb].personality, EV_TRADED, na, s_clubs[b].place);
            }
        }
    }
    if (c.week == 12)
        c.news.push_back("THE TRADE DEADLINE HAS PASSED");
}

// r177: a character in its rookie season (signed as a rookie: rookieOf holds the season it first plays)
bool rookie_season(const Career& c, const std::string& key)
{
    const std::string id = char_id(key);
    for (size_t i = 0; i < c.rookieOf.size(); i++)
        if (c.rookieOf[i].first == id && c.rookieOf[i].second == c.season)
            return true;
    return false;
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
            // r165: the regular season only - its goals, its wins (s.wins is the table's), its form; no champion's bonus
            const double score = p.regGoals * 3.0 + s.wins * 2.0 + p.regForm;
            if (score > mvp.score)
                mvp = Pick{name_of(capKeys[b]), i, p.regGoals, score, capKeys[b]};
            if (p.regGoals > boot.goals)
                boot = Pick{name_of(capKeys[b]), i, p.regGoals, 0.0, capKeys[b]};
            // r177 (Lance: Rookie of the Year open to every rookie): a captain / backup in its first season counts too
            if (rookie_season(c, capKeys[b]) && p.regGoals > rookie.goals)
                rookie = Pick{name_of(capKeys[b]), i, p.regGoals, 0.0, capKeys[b]};
        }
        for (int k = 0; k < 3; k++)
        {
            const std::string face = s.mem[k].empty() ? kind_portrait(s.kind) : s.mem[k];  // r131: a member's own
            if (s.sk[k].regGoals > boot.goals)
                boot = Pick{sk_name(s, k), i, s.sk[k].regGoals, 0.0, face};
            // r177: a named member is a rookie in its first season (rookieOf); a generic sidekick at age 0 as before
            if ((s.mem[k].empty() ? s.skAge[k] == 0 : rookie_season(c, s.mem[k])) && s.sk[k].regGoals > rookie.goals)
                rookie = Pick{sk_name(s, k), i, s.sk[k].regGoals, 0.0, face};
            const double sixthScore = s.sk[k].regGoals * 3.0 + s.sk[k].regForm * 1.5 + s.wins * 0.5;  // r165
            if ((!s.names[k].empty() || !s.mem[k].empty()) && sixthScore > sixth.score)
                sixth = Pick{sk_name(s, k), i, s.sk[k].regGoals, sixthScore, face};
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
    // r173: the winners get a title of the night (no two the same; the record's is a rare one), into the hall of fame and
    // the news ("AWARDS NIGHT: CHARIZARD IS THE FRANCHISE")
    std::vector<int> nightUsed;
    auto night_title = [&](const std::string& key, int cat) -> std::string {
        std::vector<int> opts;
        for (int t = 0; t < T_COUNT; t++)
            if (kTitleDefs[t].cat == cat && std::find(nightUsed.begin(), nightUsed.end(), t) == nightUsed.end())
                opts.push_back(t);
        if (opts.empty())
            return std::string();
        const int t = opts[fa_hash(c, key, cat, 1730) % opts.size()];
        nightUsed.push_back(t);
        return title_name(t);
    };
    std::vector<std::string> awardTitle(c.awards.size());
    for (size_t i = 0; i < c.awards.size(); i++)
    {
        const std::string& a = c.awards[i];
        const std::string& key = i < c.awardKeys.size() ? c.awardKeys[i] : a;
        int cat = -1;
        if (a.compare(0, 4, "MVP:") == 0)
            cat = TC_AW_MVP;
        else if (a.compare(0, 12, "GOLDEN BOOT:") == 0)
            cat = TC_AW_BOOT;
        else if (a.compare(0, 20, "SIDEKICK OF THE YEAR") == 0)
            cat = TC_AW_SIXTH;
        else if (a.compare(0, 18, "ROOKIE OF THE YEAR") == 0)
            cat = TC_ROOKIE;
        else if (a.compare(0, 10, "NEW RECORD") == 0)
            cat = TC_AW_RECORD;
        if (cat < 0)
            continue;
        awardTitle[i] = night_title(key, cat);
        const size_t colon = a.find(": "), paren = a.find(" (");
        if (!awardTitle[i].empty() && colon != std::string::npos && cat != TC_AW_RECORD)
            c.news.push_back("AWARDS NIGHT: " + a.substr(colon + 2, (paren == std::string::npos ? a.size() : paren) - colon - 2) + " IS " +
                             awardTitle[i] + " (" + a.substr(0, colon) + ")");
    }
    // the hall of fame, newest first
    std::vector<std::string> fame;
    for (size_t i = 0; i < c.awards.size(); i++)
        if (c.awards[i].compare(0, 4, "BEST") != 0)
            fame.push_back(season + " " + c.awards[i] + (awardTitle[i].empty() ? std::string() : " - " + awardTitle[i]));
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
            if (!s.mem[k].empty())
                continue;  // r131: a member (a character, its own level) doesn't retire: only the generic sidekicks do
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
            all[k]->regGoals = 0;  // r165
            all[k]->regForm = all[k]->form;
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
// r131: a captain key this headset has (find_captain over captains(), without building the list: every club's slots
// are asked each week now)
bool has_captain(const std::string& key)
{
    for (int t = 0; t < 8; t++)
        if (key == std::string("strikers_") + kTeamKey[t])
            return true;
    return PortGuestRosterFind(PORT_GUEST_CAPTAIN, key.c_str()) >= 0;
}

int evolutions_for(const std::string& key, const Prog& p, std::string out[5])
{
    int n = 0;
    for (size_t i = 0; i < s_evos.size() && n < 5; i++)  // r130: up to 5 (Eevee)
        if (s_evos[i].from == key && p.level >= s_evos[i].level && has_captain(s_evos[i].to))
            out[n++] = s_evos[i].to;
    return n;
}

std::string name_of(const std::string& key)
{
    // a captain's name (a Strikers one, a guest's), r130: else a Charged sidekick member's; r131: without building captains()
    const std::string n = char_name(key);
    return n.empty() ? key : n;
}

// r152 (Lance, after Thor IV turned out to be Beta Ray Bill): a Marvel costume by its own name from Marvel: Ultimate
// Alliance on the cards' lines (the coin breakdown's card XP) - as its trading card's title (xr_cards.cpp load_set): a
// character of its own alone ("Beta Ray Bill"), a look of the hero's with the hero after it ("Ultimate Thor"); else its name
std::string card_name_of(const std::string& key)
{
    static std::vector<std::pair<std::string, std::string> > names;  // (key, title)
    static bool read = false;
    if (!read)
    {
        read = true;
        if (FILE* f = fopen((home_dir() + "/franchise/costume_names.txt").c_str(), "r"))
        {
            char line[512];
            while (fgets(line, sizeof line, f) != NULL)
            {
                if (line[0] == '#')
                    continue;
                std::vector<std::string> p(1);
                for (const char* s = line; *s != '\0' && *s != '\n' && *s != '\r'; ++s)
                {
                    if (*s == '\t')
                        p.emplace_back();
                    else
                        p.back() += *s;
                }
                if (p.size() < 3 || (p[0].find("_c") == std::string::npos && !PortOriginalUniform(p[0].c_str())) || p[1].empty())
                    continue;
                const bool own = (p.size() > 3 && p[3] == "char") || p[1].find(p[2]) != std::string::npos;
                names.push_back(std::make_pair(p[0], own ? p[1] : p[1] + " " + p[2]));
            }
            fclose(f);
        }
    }
    for (size_t i = 0; i < names.size(); ++i)
        if (names[i].first == key)
            return names[i].second;
    return name_of(key);
}

std::string sk_name(const ClubState& s, int k) { return s.mem[k].empty() ? s.names[k] : name_of(s.mem[k]); }

// r130 (Lance: "let ANY spot be ANY character"): a player key as a character select menu pick: a Strikers captain
// ("strikers_<name>"; r130's first take's "cap_<name>" packs too) PORT_GUEST_ORIGINAL + its eTeamID, a guest its roster
// index (a captain's entry first, else a sidekick's), -1 none / unknown
int member_pick(const std::string& key)
{
    if (key.empty())
        return -1;
    for (int t = 0; t < 8; t++)
        if (key == std::string("strikers_") + kTeamKey[t] || key == std::string("cap_") + kTeamKey[t])
            return PORT_GUEST_ORIGINAL + t;
    const int cap = PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, key.c_str());
    return cap >= 0 ? cap : PortGuestRosterFindAny(PORT_GUEST_SIDEKICK, key.c_str());
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

// r130: a club's player by slot: 0 the captain, 1 the backup, 2-4 the sidekick members (mem / mp)
std::string& slot_key(ClubState& s, int slot) { return slot <= 0 ? s.captain : (slot == 1 ? s.backup : s.mem[std::min(2, slot - 2)]); }
Prog& slot_prog(ClubState& s, int slot) { return slot <= 0 ? s.cap : (slot == 1 ? s.bk : s.mp[std::min(2, slot - 2)]); }
const std::string& slot_key_c(const ClubState& s, int slot)
{
    return slot <= 0 ? s.captain : (slot == 1 ? s.backup : s.mem[std::min(2, slot - 2)]);
}
const Prog& slot_prog_c(const ClubState& s, int slot) { return slot <= 0 ? s.cap : (slot == 1 ? s.bk : s.mp[std::min(2, slot - 2)]); }
PStats& slot_stats(ClubState& s, int slot) { return slot <= 0 ? s.capSt : (slot == 1 ? s.bkSt : s.sk[std::min(2, slot - 2)]); }

// r167 (EVOLUTION SELECTOR): a form's line from its first form up to it (evolutions.txt backwards, else the roster's
// "evolves from"), first form first
std::vector<std::string> form_chain(const std::string& key)
{
    std::vector<std::string> out;
    if (key.empty())
        return out;
    std::string k = key;
    out.push_back(k);
    for (int guard = 0; guard < 12; guard++)
    {
        std::string prev;
        for (size_t i = 0; i < s_evos.size() && prev.empty(); i++)
            if (s_evos[i].to == k)
                prev = s_evos[i].from;
        if (prev.empty())
        {
            const int idx = PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, k.c_str());
            const char* from = idx >= 0 ? PortGuestRosterEvolvesFrom(idx) : NULL;
            if (from != NULL && from[0] != '\0')
                prev = from;
        }
        if (prev.empty() || prev == k || std::find(out.begin(), out.end(), prev) != out.end())
            break;
        out.push_back(prev);
        k = prev;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// r167: the highest form a slot's player has reached (its key, unless it plays as an earlier form of that line)
std::string top_of(const ClubState& s, int slot)
{
    const std::string& key = slot_key_c(s, slot);
    const std::string& t = s.top[std::max(0, std::min(4, slot))];
    if (t.empty() || key.empty() || t == key)
        return key;
    const std::vector<std::string> ch = form_chain(t);
    return std::find(ch.begin(), ch.end(), key) != ch.end() ? t : key;
}

// r167: a player now playing as another form of its line: its beef records follow it (as do_evolve_slot's, without the
// anger: picking a form is no evolution)
void each_nemesis_rekey(Career& c, int club, int slot, const std::string& from, const std::string& to)
{
    if (slot <= 1)
        each_nemesis(c, from, [&](Nemesis* n) {
            n->key = to;
            n->name = name_of(to);
        });
    else
        each_nemesis(c, "sk#" + std::to_string(club) + "#" + name_of(from), [&](Nemesis* n) {
            n->key = "sk#" + std::to_string(club) + "#" + name_of(to);
            n->name = name_of(to);
        });
}

// r167: a slot back to its highest form (before it leaves the club, or evolves on): the selector's choice undone
void settle_form(Career& c, int club, int slot)
{
    ClubState& s = c.clubs[club];
    const std::string t = top_of(s, slot);
    std::string& key = slot_key(s, slot);
    if (!t.empty() && t != key)
    {
        const std::string was = key;
        key = t;
        each_nemesis_rekey(c, club, slot, was, t);
    }
    s.top[std::max(0, std::min(4, slot))].clear();
}
void settle_forms(Career& c, int club)
{
    for (int k = 0; k < 5; k++)
        settle_form(c, club, k);
}

// Evolves a club's captain (or backup; r130: or a sidekick member, slot 2-4) into `to`: a shiny roll, the news.
void do_evolve_slot(Career& c, int club, int slot, const std::string& to);
void do_evolve(Career& c, int club, bool backup, const std::string& to) { do_evolve_slot(c, club, backup ? 1 : 0, to); }
void do_evolve_slot(Career& c, int club, int slot, const std::string& to)
{
    ClubState& s = c.clubs[club];
    std::string& key = slot_key(s, slot);
    Prog& p = slot_prog(s, slot);
    const std::string from = key;
    const bool wasShiny = p.shiny;
    // r110: a Marvel hero's next costume (no shiny ones), in costume words
    const bool costume = PortCostumeKey(from.c_str());  // r183: and an original single's uniforms
    // r146: your Pokemon of an evolution line whose every card you have: a little luckier (card_bonus: 1 in 96)
    int odds = kShinyOdds;
    if (card_bonus_on(c) && club == c.myClub)
        if (const CardBonus* b = card_bonus(from))
            if (b->shinyOdds > 0 && b->shinyOdds < kShinyOdds)
                odds = b->shinyOdds;
    if (!costume && !PortOriginalKey(from.c_str()) && !p.shiny && next_rand(c.rng) % odds == 0)  // r183: originals have no shiny looks
        p.shiny = true;
    key = to;
    s.top[std::max(0, std::min(4, slot))].clear();  // r167: evolved: it plays as its new (highest) form
    p.declinedAt = 0;
    std::string line = upper(s_clubs[club].place) + "'S " + upper(name_of(from)) +
                       (costume ? " SUITED UP: " : " EVOLVED INTO ") + upper(name_of(to));
    if (p.shiny && !wasShiny)
        line += " - SHINY!";
    c.news.push_back(line);
    // r72 (Lance): a nemesis that evolves comes back angrier, with a new title (r130 review: captains / backups only -
    // a sidekick member can share its key with someone's nemesis elsewhere)
    if (slot <= 1)
    each_nemesis(c, from, [&](Nemesis* n) {  // r100: everyone's beef with it
        n->key = to;
        n->name = name_of(to);
        if (n->active && !n->teammate)
        {
            n->title = pick_title(c, n->key, {TC_EVOLVED});  // r173: one of the evolved storyline's (was always THE AVENGER)
            n->heat = std::min(100, n->heat + 15);
            const std::string whose = c.league.empty() ? std::string("YOUR NEMESIS") : upper(s_clubs[nem_owner(c, *n)].place) + "'S NEMESIS";
            c.news.push_back(whose + (costume ? " SUITED UP: " : " EVOLVED: ") + upper(n->name) + " " + title_name(n->title) +
                             " IS ANGRIER THAN EVER");
            nem_log(*n, "SEASON " + std::to_string(c.season) + (costume ? ": SUITED UP AS " : ": EVOLVED INTO ") +
                            upper(n->name) + ", ANGRIER");
        }
    });
    else  // r131: a sidekick member's beef (keyed by club and name) follows it into its new form
        each_nemesis(c, "sk#" + std::to_string(club) + "#" + name_of(from), [&](Nemesis* n) {
            n->key = "sk#" + std::to_string(club) + "#" + name_of(to);
            n->name = name_of(to);
        });
    fprintf(stderr, "[franchise] %s\n", line.c_str());
    fflush(stderr);
}

// The CPU clubs' Pokemon evolve when they can (a split: the stronger form). Yours wait for you.
// r131: their sidekick members too (slots 2-4: every club deals them now)
void evolve_cpu(Career& c)
{
    for (int club = 0; club < PORT_FR_CLUBS; club++)
    {
        if (is_human(c, club))  // r100: every person's Pokemon wait for them
            continue;
        for (int b = 0; b < 5; b++)
        {
            ClubState& s = c.clubs[club];
            for (int step = 0; step < 3; step++)  // (a big jump in levels can go two forms at once)
            {
                const std::string& key = slot_key(s, b);
                if (key.empty())
                    break;
                std::string to[5];
                const int n = evolutions_for(key, slot_prog(s, b), to);
                if (n == 0)
                    break;
                std::string pick = to[0];  // r130: the best rated of up to 5
                for (int k = 1; k < n; k++)
                    if (rating(to[k]) > rating(pick))
                        pick = to[k];
                do_evolve_slot(c, club, b, pick);
            }
        }
    }
}

void fa_week(Career& c);             // r167: free agency (below)
void fa_offseason_open(Career& c);

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
    fa_week(c);     // r167: free agency's offers and decisions (weeks 1-12)
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
            fa_offseason_open(c);  // r167: the off-season's free agency (decided when the next season starts)
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

// ================================================================================================ r167: FREE AGENCY
// (Lance: "leftovers become free agents ... do an offer system of sorts so teams are actually competing to land agents and
// its not richest team signs everyone"; franchise.h). No money anywhere: an offer is a ROLE. Each agent scores every offer
// (fa_eval: the role, the club as a contender, its personality, its old club, its home, squad fit, beef) and signs with the
// best one if it's good enough (kFaAccept). CPU clubs bid on their needs (an empty or weak spot; contenders chase stars), one
// offer at a time and one signing a round, so a strong club can't hoard: a star picks the captaincy of a weaker club over a
// contender's bench unless its personality says otherwise. Every roll is a hash of the career's seed (fa_noise) or its rng.
const char* const kPersName[PERS_COUNT + 1] = {"NEUTRAL",   "HOTHEAD",  "SHOWBOAT", "SILENT ASSASSIN", "HONORABLE",
                                               "TRICKSTER", "UNDERDOG", "VETERAN",  "FRONT-RUNNER"};
const int kFaDeadline = 12;      // the trade deadline (cpu_week's)
const double kFaAccept = 15.0;   // an agent signs only for an offer worth this much to it

bool fa_window(const Career& c) { return (c.phase == PHASE_REGULAR && c.week <= kFaDeadline) || c.phase == PHASE_DONE; }

unsigned fa_hash(const Career& c, const std::string& key, int club, int salt)
{
    unsigned h = 2166136261u ^ c.seed;
    for (size_t i = 0; i < key.size(); i++)
    {
        h ^= (unsigned char)key[i];
        h *= 16777619u;
    }
    const unsigned more[3] = {(unsigned)(club + 7), (unsigned)c.season, (unsigned)salt};
    for (int k = 0; k < 3; k++)
    {
        h ^= more[k];
        h *= 16777619u;
    }
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}
// -amp .. +amp, fixed for the agent, the club and the season (the page's "leads" is what it decides)
double fa_noise(const Career& c, const std::string& key, int club, double amp, int salt)
{
    return ((fa_hash(c, key, club, salt) % 20001u) / 10000.0 - 1.0) * amp;
}

int fa_value(const std::string& key, const Prog& p) { return rating(key) + std::max(0, p.level - 8); }

// a captain type can be a captain / backup; a Charged sidekick guest (Boo ...) a sidekick member only
bool fa_captain_type(const std::string& key) { return !sk_guest(key); }

const char* fa_game(const std::string& key)
{
    if (key.compare(0, 3, "pk_") == 0)
        return "POKEMON";
    if (key.compare(0, 4, "mua_") == 0)
        return "MARVEL";
    if (key.compare(0, 5, "orig_") == 0)  // r183
        return "ORIGINALS";
    if (key.compare(0, 9, "strikers_") == 0)
        return "STRIKERS";
    int idx = PortGuestRosterFindAny(PORT_GUEST_CAPTAIN, key.c_str());
    if (idx < 0)
        idx = PortGuestRosterFindAny(PORT_GUEST_SIDEKICK, key.c_str());
    const char* g = idx >= 0 ? PortGuestRosterGame(idx) : NULL;
    return g != NULL && strstr(g, "Charged") != NULL ? "CHARGED" : "MELEE";
}

int club_by_id(const std::string& id)
{
    for (size_t i = 0; i < s_clubs.size(); i++)
        if (s_clubs[i].id == id)
            return (int)i;
    return -1;
}

// A character's home: its home ground's club (its world's place among the 32) or, a Pokemon or a hero, the division of
// its element (the divisions are named for them: Inferno, Thunder, Boulder, Tidal, Phantom, Cyclone, Glacier, Bloom).
std::string fa_home(const std::string& key, int* club)
{
    *club = -1;
    static std::unordered_map<std::string, std::string> s_types;
    static bool s_typesRead = false;
    if (!s_typesRead)
    {
        s_typesRead = true;
        if (FILE* f = fopen((home_dir() + "/franchise/types.txt").c_str(), "r"))
        {
            char line[256];
            while (fgets(line, sizeof line, f) != NULL)
            {
                char k[96], t[32];
                if (line[0] != '#' && sscanf(line, "%95s %31s", k, t) == 2)
                    s_types[k] = t;
            }
            fclose(f);
        }
    }
    static const char* const kPlace[][2] = {
        {"strikers_mario", "mushroomkingdom"}, {"strikers_luigi", "mushroomkingdom"}, {"strikers_peach", "mushroomkingdom"},
        {"strikers_daisy", "mushroomkingdom"}, {"strikers_yoshi", "mushroomkingdom"}, {"strikers_waluigi", "mushroomkingdom"},
        {"strikers_wario", "mushroomkingdom"}, {"strikers_donkeykong", "konga"},     {"diddykong", "konga"},
        {"drmario", "mushroomkingdom"},        {"boo", "mushroomkingdom"},          {"drybones", "mushroomkingdom"},
        {"shyguy", "mushroomkingdom"},         {"montymole", "mushroomkingdom"},    {"bowser", "pipelinecentral"},
        {"bowserjr", "pipelinecentral"},       {"petey", "pipelinecentral"},        {"fox", "corneria"},
        {"falco", "corneria"},                 {"falcon", "mutecity"},              {"kirby", "dreamland"},
        {"link", "lostwoods"},                 {"younglink", "lostwoods"},          {"zelda", "lakehylia"},
        {"sheik", "lakehylia"},                {"ganondorf", "deathmountain"},      {"samus", "norfair"},
        {"marth", "archanea"},                 {"roy", "archanea"},                 {"ness", "onett"},
        {"popo", "infiniteglacier"},           {"nana", "infiniteglacier"},         {"gamewatch", "galactic"},
    };
    static const char* const kHero[][2] = {
        {"ghostrider", "Inferno"},  {"humantorch", "Inferno"},   {"thor", "Thunder"},          {"storm", "Thunder"},
        {"iceman", "Glacier"},      {"drstrange", "Phantom"},    {"invisiblewoman", "Phantom"}, {"jeangrey", "Phantom"},
        {"professorx", "Phantom"},  {"moonknight", "Phantom"},   {"nightcrawler", "Phantom"},  {"mysterio", "Phantom"},
        {"drdoom", "Phantom"},      {"ironman", "Cyclone"},      {"silversurfer", "Cyclone"},  {"captainmarvel", "Cyclone"},
        {"msmarvel", "Cyclone"},    {"vision", "Cyclone"},       {"hawkeye", "Cyclone"},       {"thing", "Boulder"},
        {"colossus", "Boulder"},    {"lukecage", "Boulder"},     {"rhino", "Boulder"},         {"wolverine", "Boulder"},
        {"mrfantastic", "Tidal"},   {"lizard", "Tidal"},         {"blackpanther", "Bloom"},
    };
    const std::string root = char_root(key);
    for (size_t i = 0; i < sizeof kPlace / sizeof kPlace[0]; i++)
        if (root == kPlace[i][0])
        {
            *club = club_by_id(kPlace[i][1]);
            return *club >= 0 ? s_clubs[*club].div : std::string();
        }
    if (root.compare(0, 4, "mua_") == 0)
    {
        const std::string hero = root.substr(4);
        for (size_t i = 0; i < sizeof kHero / sizeof kHero[0]; i++)
            if (hero == kHero[i][0])
                return kHero[i][1];
        return std::string();
    }
    std::unordered_map<std::string, std::string>::const_iterator it = s_types.find(key);
    if (it == s_types.end())
        it = s_types.find(root);
    if (it == s_types.end())
        return std::string();
    const std::string& t = it->second;
    if (t == "fire")
        return "Inferno";
    if (t == "electric")
        return "Thunder";
    if (t == "rock" || t == "ground" || t == "steel" || t == "fighting")
        return "Boulder";
    if (t == "water")
        return "Tidal";
    if (t == "ghost" || t == "dark" || t == "psychic" || t == "poison")
        return "Phantom";
    if (t == "flying" || t == "dragon")
        return "Cyclone";
    if (t == "ice")
        return "Glacier";
    return "Bloom";  // grass, bug, fairy, normal
}

double fa_avg_strength(const Career& c)
{
    double sum = 0.0;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
        sum += strength(c.clubs[i]);
    return sum / PORT_FR_CLUBS;
}
double fa_avg_goals(const Career& c)
{
    double sum = 0.0;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
        sum += c.clubs[i].goalsFor;
    return sum / PORT_FR_CLUBS;
}

// How much of a contender a club is: its record (a start of 2-2 counted in early, so week 1 isn't everything), its strength
// against the league's, the champion a little more
double fa_contend(const Career& c, int club, double avgStr)
{
    const ClubState& s = c.clubs[club];
    const double pct = (s.wins + 2.0) / (s.wins + s.losses + 4.0);
    double v = 40.0 * (pct - 0.5) + 1.2 * (strength(s) - avgStr);
    if (c.phase == PHASE_DONE && c.champion == club)
        v += 8.0;
    return v;
}

// The value a club's spot has now (its holder's, as its highest form; a generic sidekick the kind's; an empty backup 0)
int fa_occ_value(const Career& c, int club, int role)
{
    const ClubState& s = c.clubs[club];
    if (role == PORT_FA_CAPTAIN)
        return fa_value(top_of(s, 0), s.cap);
    if (role == PORT_FA_BACKUP)
        return s.backup.empty() ? 0 : fa_value(top_of(s, 1), s.bk);
    const int k = std::max(0, std::min(2, role - PORT_FA_SIDEKICK1));
    return s.mem[k].empty() ? rating(s.kind) + std::max(0, s.side.level - 8) : fa_value(top_of(s, 2 + k), s.mp[k]);
}

bool fa_role_ok(const FreeAgent& a, int role) { return role >= PORT_FA_SIDEKICK1 || fa_captain_type(a.key); }

// An agent with active beef against that club's captain (a person's club: nemeses are with people) never signs there
bool fa_refuses(const Career& c, const FreeAgent& a, int club)
{
    if (!is_human(c, club))
        return false;
    const std::string id = char_id(a.key), nm = name_of(a.key);
    for (size_t i = 0; i < c.nems.size(); i++)
    {
        const Nemesis& n = c.nems[i];
        if (!n.active || nem_owner(c, n) != club)
            continue;
        if (n.key == a.key || (!id.empty() && char_id(n.key) == id) || (n.key.compare(0, 3, "sk#") == 0 && n.name == nm))
            return true;
    }
    return false;
}

struct FaEval
{
    double score = 0.0;
    bool refuse = false;
    std::string why;
};
FaEval fa_eval(const Career& c, const FreeAgent& a, const FaBid& b, double avgStr, double avgGf)
{
    FaEval e;
    if (b.club < 0 || b.club >= PORT_FR_CLUBS)
    {
        e.refuse = true;
        return e;
    }
    if (fa_refuses(c, a, b.club))
    {
        e.refuse = true;
        e.why = "WON'T PLAY FOR A NEMESIS";
        return e;
    }
    const ClubState& s = c.clubs[b.club];
    const int p = a.st.personality;
    double rm = 1.0, cm = 1.0;  // how much the role / the club's chances weigh for it
    switch (p)
    {
        case PERS_HOTHEAD: rm = 1.4; cm = 0.6; break;
        case PERS_SHOWBOAT: rm = 1.25; cm = 0.8; break;
        case PERS_HONORABLE: rm = 1.0; cm = 0.8; break;
        case PERS_UNDERDOG: rm = 1.15; cm = -0.7; break;  // (it wants to carry a club, not join the best one)
        case PERS_VETERAN: rm = 0.75; cm = 1.5; break;
        case PERS_FRONTRUNNER: rm = 0.8; cm = 2.0; break;
        default: break;  // neutral, silent assassin, trickster
    }
    double base = 0.0;
    std::string roleWhy;
    if (b.role == PORT_FA_CAPTAIN)
    {
        base = 100.0;
        roleWhy = p == PERS_HOTHEAD ? "WANTS THE SPOTLIGHT" : "WANTS THE CAPTAINCY";
    }
    else if (b.role == PORT_FA_BACKUP)
    {
        base = 55.0 + (b.promise ? (p == PERS_HONORABLE || a.fresh ? 45.0 : 30.0) : 0.0);  // (r173: a rookie wants the starts)
        roleWhy = b.promise ? "PROMISED THE STARTS" : "A CO-CAPTAIN'S SPOT";
    }
    else
    {
        const bool bench = b.role == PORT_FA_SIDEKICK1 && !s.backup.empty();  // (slot 1 sits when a co-captain plays)
        base = bench ? (a.fresh ? 25.0 : 18.0) : (a.fresh ? 60.0 : 32.0);  // r173: a rookie wants to PLAY most of all
        roleWhy = bench ? "A BENCH SPOT" : (a.fresh ? "WANTS TO PLAY" : "A SIDEKICK SPOT");
    }
    std::vector<std::pair<double, std::string> > parts;
    const double role = base * rm, contend = fa_contend(c, b.club, avgStr) * cm * (a.fresh ? 0.5 : 1.0);  // (r173: a rookie: minutes first)
    e.score = role + contend;
    parts.push_back(std::make_pair(role - 45.0, roleWhy));
    parts.push_back(std::make_pair(contend, p == PERS_UNDERDOG ? "WANTS TO CARRY A CLUB"
                                                               : (p == PERS_FRONTRUNNER ? "CHASES A WINNER"
                                                                                        : (p == PERS_VETERAN ? "CHASING A RING" : "A CONTENDER"))));
    const int val = fa_value(a.key, a.prog);
    if (fa_captain_type(a.key) && b.role >= PORT_FA_SIDEKICK1 && !a.fresh)
        e.score -= 14.0;  // a captain would rather lead (r173: a rookie takes the minutes)
    if (b.role == PORT_FA_BACKUP && !b.promise && fa_occ_value(c, b.club, PORT_FA_CAPTAIN) >= val + 3)
        e.score -= 14.0;  // squad fit: won't sit behind a better captain
    if (p == PERS_HOTHEAD && b.role == PORT_FA_CAPTAIN)
        e.score += 10.0;
    if (p == PERS_SHOWBOAT)
    {
        const double show = 0.15 * (s.goalsFor - avgGf);
        e.score += show;
        parts.push_back(std::make_pair(show, "LOVES THEIR ATTACK"));
    }
    if (!a.fresh && a.from == b.club)
    {
        double back = -4.0;
        if (p == PERS_HONORABLE)
            back = 25.0;
        else if (p == PERS_VETERAN)
            back = 10.0;
        else if (p == PERS_HOTHEAD)
            back = -15.0;  // (holds the release against them)
        e.score += back;
        parts.push_back(std::make_pair(back, "LOYAL TO " + upper(s_clubs[b.club].place)));
    }
    int homeClub = -1;
    const std::string div = fa_home(a.key, &homeClub);
    if (homeClub == b.club)
    {
        e.score += 16.0;
        parts.push_back(std::make_pair(16.0, "HOMETOWN CLUB"));
    }
    else if (!div.empty() && s_clubs[b.club].div == div)
    {
        e.score += 9.0;
        parts.push_back(std::make_pair(9.0, "HOME REGION"));
    }
    const double luck = fa_noise(c, a.key, b.club, p == PERS_TRICKSTER ? 14.0 : 4.0, 0);
    e.score += luck;
    if (p == PERS_TRICKSTER)
        parts.push_back(std::make_pair(luck, "UNPREDICTABLE"));
    double best = -1e9;
    for (size_t i = 0; i < parts.size(); i++)
        if (parts[i].first > best)
        {
            best = parts[i].first;
            e.why = parts[i].second;
        }
    if (best <= 0.0)
        e.why = roleWhy;
    return e;
}

int fa_find(const Career& c, const std::string& key)
{
    for (size_t i = 0; i < c.fa.size(); i++)
        if (c.fa[i].key == key)
            return (int)i;
    return -1;
}

const char* fa_role_word(int role)
{
    static const char* const k[PORT_FA_ROLES] = {"CAPTAIN", "BACKUP", "SIDEKICK", "SIDEKICK", "SIDEKICK"};
    return k[std::max(0, std::min(PORT_FA_ROLES - 1, role))];
}

// The pool kept in step: the ones on a club now gone (signed by the r131 fill, traded), every character on no club in it
// (never dealt, added to the roster since: fresh, at the sidekicks' level, a personality of its own), offers for ones
// gone dropped. Pure bookkeeping (no random numbers): any headset gets the same.
bool s_holdReturning = false;  // r177 (season_signings)
void fa_refresh(Career& c)
{
    read_data();
    std::unordered_map<std::string, int> held;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        std::vector<std::string> ids;
        club_ids(c.clubs[i], ids);
        for (size_t k = 0; k < ids.size(); k++)
            held[ids[k]] = i;
    }
    int level = 0;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
        level += c.clubs[i].side.level;
    level = std::max(8, level / PORT_FR_CLUBS);
    std::vector<FreeAgent> keep;
    std::unordered_map<std::string, bool> listed;
    for (size_t i = 0; i < c.fa.size(); i++)
    {
        const std::string id = char_id(c.fa[i].key);
        if (id.empty() || held.count(id) != 0 || listed.count(id) != 0)
            continue;
        listed[id] = true;
        keep.push_back(c.fa[i]);
        if (keep.back().fresh)  // r173: a never-signed one is a ROOKIE: Lv 8 (everyone's first level), no XP, no record
        {
            init_prog(keep.back().prog);
            init_stats(keep.back().st, keep.back().st.personality);
        }
    }
    auto add = [&](const std::string& key) {
        const std::string id = char_id(key);
        if (id.empty() || held.count(id) != 0 || listed.count(id) != 0)
            return;
        // r177: a retired line comes back as a rookie the season after (not into the off-season it retired in, nor the
        // new season's first fill: s_holdReturning)
        for (size_t i = 0; i < c.returning.size(); i++)
            if (c.returning[i].first == id && (c.season < c.returning[i].second || (c.season == c.returning[i].second && s_holdReturning)))
                return;
        listed[id] = true;
        FreeAgent f;
        f.key = key;
        init_prog(f.prog);
        (void)level;  // r173: a rookie (init_prog: Lv 8), never the league's level (r167 trained them: they read as vets)
        init_stats(f.st, (int)(fa_hash(c, key, -1, 77) % PERS_COUNT));
        f.fresh = true;
        f.since = c.season;
        keep.push_back(f);
    };
    const std::vector<Captain> caps = captains();
    for (size_t i = 0; i < caps.size(); i++)
        if (!caps[i].evolved)
            add(caps[i].key);
    const std::vector<Kind> ks = kinds();
    for (size_t i = 0; i < ks.size(); i++)
        if (ks[i].guest >= 0)
            add(ks[i].key);
    c.fa.swap(keep);
    for (int i = (int)c.bids.size() - 1; i >= 0; i--)
        if (fa_find(c, c.bids[(size_t)i].key) < 0)
            c.bids.erase(c.bids.begin() + i);
}

// A club's player into free agency (its highest form, its level, record and personality kept). The slot is emptied
// (a backup gone, a member slot back to its generic sidekick); the captain's slot is the caller's to fill.
void fa_release(Career& c, int club, int slot, std::vector<std::string>& lines)
{
    ClubState& s = c.clubs[club];
    if (slot_key(s, slot).empty())
        return;
    settle_form(c, club, slot);
    const std::string key = slot_key(s, slot);
    FreeAgent f;
    f.key = key;
    f.prog = slot_prog(s, slot);
    f.st = slot_stats(s, slot);
    f.from = club;
    f.since = c.season;
    f.fresh = false;
    f.decide = 0;
    c.fa.push_back(f);
    const std::string place = upper(s_clubs[club].place);
    auto gone = [&](Nemesis* n) {
        if (n->club != club)
            return;
        n->club = -1;
        nem_log(*n, "SEASON " + std::to_string(c.season) + ": RELEASED BY " + place);
    };
    each_nemesis(c, key, gone);
    if (slot >= 2)
        each_nemesis(c, "sk#" + std::to_string(club) + "#" + name_of(key), gone);
    lines.push_back("RELEASED: " + upper(name_of(key)) + " (" + place + ")");
    const int pers = is_human(c, club) ? PERS_NEUTRAL : (int)(fa_hash(c, key, club, 78) % PERS_COUNT);
    if (slot == 1)
    {
        s.backup.clear();
        init_prog(s.bk);
        init_stats(s.bkSt, pers);
        s.startsBackup = false;
    }
    else if (slot >= 2)
    {
        const int k = std::min(2, slot - 2);
        s.mem[k].clear();
        s.mp[k] = s.side;
        init_stats(s.sk[k], pers);
        s.skAge[k] = 0;
    }
    s.top[std::max(0, std::min(4, slot))].clear();
}

// An agent joins a club in a role (the spot's holder released; a captain's offer puts the old captain on the bench - its
// backup released - or releases it)
void fa_sign(Career& c, int club, const std::string& key, int role, bool promise, bool relCap, std::vector<std::string>& lines)
{
    const int ai = fa_find(c, key);
    if (ai < 0 || club < 0 || club >= PORT_FR_CLUBS)
        return;
    const FreeAgent a = c.fa[(size_t)ai];
    c.fa.erase(c.fa.begin() + ai);
    ClubState& s = c.clubs[club];
    if (is_human(c, club))
        settle_forms(c, club);
    auto take = [&](int slot, int spotLevel) {
        Prog& p = slot_prog(s, slot);
        if (a.fresh)
        {
            init_prog(p);
            (void)spotLevel;  // r173: a rookie joins as a rookie (Lv 8), not at its spot's level
        }
        else
        {
            p = a.prog;
            p.startLevel = p.level;
            p.declinedAt = 0;
        }
        slot_stats(s, slot) = a.st;
        slot_key(s, slot) = a.key;
        s.top[slot].clear();
    };
    if (role == PORT_FA_CAPTAIN)
    {
        const int level = s.cap.level;
        if (relCap)
            fa_release(c, club, 0, lines);
        else
        {
            if (!s.backup.empty())
                fa_release(c, club, 1, lines);
            s.backup = s.captain;  // the old captain to the bench
            s.bk = s.cap;
            s.bkSt = s.capSt;
        }
        take(0, level);
        s.startsBackup = false;
    }
    else if (role == PORT_FA_BACKUP)
    {
        if (!s.backup.empty())
            fa_release(c, club, 1, lines);
        take(1, s.cap.level);
        s.startsBackup = promise;
    }
    else
    {
        const int k = std::max(0, std::min(2, role - PORT_FA_SIDEKICK1));
        if (!s.mem[k].empty())
            fa_release(c, club, 2 + k, lines);
        take(2 + k, s.side.level);
        s.skAge[k] = 0;
    }
    const std::string place = upper(s_clubs[club].place);
    each_nemesis(c, a.key, [&](Nemesis* n) {
        n->club = club;
        nem_log(*n, "SEASON " + std::to_string(c.season) + ": SIGNED WITH " + place);
    });
    post(c, a.st.personality, EV_TRADED, name_of(a.key), s_clubs[club].place);
    {
        // r177: the season it plays for its new club first (an off-season signing: the next one) - a person's signing never
        // retires that season; a rookie's first season is its Rookie of the Year season
        const int first = c.phase == PHASE_DONE ? c.season + 1 : c.season;
        if (is_human(c, club))
            c.joined.push_back(std::make_pair(char_id(a.key), first));
        if (a.fresh)
            c.rookieOf.push_back(std::make_pair(char_id(a.key), first));
    }
    fprintf(stderr, "[franchise] free agency: %s signs with %s as %s%s\n", a.key.c_str(), s_clubs[club].id.c_str(),
            fa_role_word(role), promise ? " (promised starts)" : "");
    fflush(stderr);
}

// An agent decides: the best offer worth kFaAccept signs it ("X SPURNS Y FOR Z" when it had a choice), else it turns them
// all down and stays free. A CPU club that signed someone sits the round's other decisions out (signedClubs).
bool fa_decide(Career& c, const std::string& key, std::vector<int>& signedClubs, std::vector<std::string>& lines)
{
    const int ai = fa_find(c, key);
    if (ai < 0)
        return false;
    const double avgStr = fa_avg_strength(c), avgGf = fa_avg_goals(c);
    struct Cand
    {
        size_t bid;
        double score;
        std::string why;
    };
    std::vector<Cand> cands;
    bool mine = false, refusedMe = false;
    for (size_t i = 0; i < c.bids.size(); i++)
    {
        const FaBid& b = c.bids[i];
        if (b.key != key)
            continue;
        if (b.club == c.myClub)
            mine = true;
        if (!fa_role_ok(c.fa[(size_t)ai], b.role))
            continue;
        if (!is_human(c, b.club) && std::find(signedClubs.begin(), signedClubs.end(), b.club) != signedClubs.end())
            continue;
        const FaEval e = fa_eval(c, c.fa[(size_t)ai], b, avgStr, avgGf);
        if (e.refuse)
        {
            if (b.club == c.myClub)
                refusedMe = true;
            continue;
        }
        Cand cd;
        cd.bid = i;
        cd.score = e.score;
        cd.why = e.why;
        cands.push_back(cd);
    }
    std::stable_sort(cands.begin(), cands.end(), [&](const Cand& x, const Cand& y) {
        return x.score != y.score ? x.score > y.score : c.bids[x.bid].club < c.bids[y.bid].club;
    });
    const std::string name = upper(name_of(key));
    auto drop_bids = [&](const std::string& k) {
        for (int i = (int)c.bids.size() - 1; i >= 0; i--)
            if (c.bids[(size_t)i].key == k)
                c.bids.erase(c.bids.begin() + i);
    };
    if (cands.empty() || cands[0].score < kFaAccept)
    {
        if (mine || cands.size() >= 2)
            lines.push_back(name + " TURNS DOWN EVERY OFFER");
        if (mine)
            c.faLast = refusedMe ? name + " WON'T PLAY FOR YOU: BEEF" : name + " TURNED DOWN YOUR OFFER";
        drop_bids(key);
        c.fa[(size_t)ai].decide = 0;
        fprintf(stderr, "[franchise] free agency: %s turned down %d offers\n", key.c_str(), (int)cands.size());
        fflush(stderr);
        return false;
    }
    const FaBid win = c.bids[cands[0].bid];
    const std::string why = cands[0].why;
    int runner = -1;
    for (size_t i = 1; i < cands.size() && runner < 0; i++)
        if (c.bids[cands[i].bid].club != win.club)
            runner = c.bids[cands[i].bid].club;
    std::string line = runner >= 0 ? name + " SPURNS " + upper(s_clubs[runner].place) + " FOR " + upper(s_clubs[win.club].place)
                                   : name + " SIGNS WITH " + upper(s_clubs[win.club].place);
    line += std::string(" (") + fa_role_word(win.role) + ")";
    lines.push_back(line);
    if (win.club == c.myClub)
    {
        c.faLast = name + " SIGNED WITH YOU: " + fa_role_word(win.role) + "!";
        c.headline = name_of(key) + " signed with you" + (runner >= 0 ? " over " + s_clubs[runner].place : std::string()) + "!";
    }
    else if (mine)
    {
        c.faLast = name + (refusedMe ? " WON'T PLAY FOR YOU: BEEF" : " SPURNED YOU FOR " + upper(s_clubs[win.club].place) + ": " + why);
    }
    drop_bids(key);
    if (!is_human(c, win.club))
    {
        for (int i = (int)c.bids.size() - 1; i >= 0; i--)  // one signing a round for a CPU club (its other offer withdrawn)
            if (c.bids[(size_t)i].club == win.club)
                c.bids.erase(c.bids.begin() + i);
        signedClubs.push_back(win.club);
    }
    fprintf(stderr, "[franchise] free agency: %s picks %s (%s, %.1f) of %d offers%s\n", key.c_str(), s_clubs[win.club].id.c_str(),
            why.c_str(), cands[0].score, (int)cands.size(), runner >= 0 ? (" over " + s_clubs[runner].id).c_str() : "");
    fflush(stderr);
    fa_sign(c, win.club, key, win.role, win.promise, win.relCap, lines);
    return true;
}

// The CPU clubs' offers: each without one out (and with `chance`) picks the agent who'd help it most - a better captain
// (the captaincy: its own goes to the bench), a backup for an empty or weak spot, a sidekick for a generic or weak one;
// contenders (the 8 strongest) lean to the stars. One offer a club; an offer starts an agent's clock (mid-season).
void fa_cpu_round(Career& c, double chance, int roundId)
{
    c.faRound = roundId;
    if (c.fa.empty())
        return;
    std::vector<int> order, byStr;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
        if (!is_human(c, i))
        {
            order.push_back(i);
            byStr.push_back(i);
        }
    for (int i = (int)order.size() - 1; i > 0; i--)
    {
        const int j = (int)(next_rand(c.rng) % (unsigned)(i + 1));
        std::swap(order[(size_t)i], order[(size_t)j]);
    }
    std::stable_sort(byStr.begin(), byStr.end(), [&](int a, int b) { return strength(c.clubs[a]) > strength(c.clubs[b]); });
    for (size_t oi = 0; oi < order.size(); oi++)
    {
        const int club = order[oi];
        bool has = false;
        for (size_t i = 0; i < c.bids.size() && !has; i++)
            has = c.bids[i].club == club;
        if (has || rand01(c.rng) >= chance)
            continue;
        const bool contender = std::find(byStr.begin(), byStr.begin() + std::min<size_t>(8, byStr.size()), club) !=
                               byStr.begin() + std::min<size_t>(8, byStr.size());
        const ClubState& s = c.clubs[club];
        const int capV = fa_occ_value(c, club, PORT_FA_CAPTAIN);
        double best = 0.0;
        int bestAgent = -1, bestRole = 0;
        for (size_t ai = 0; ai < c.fa.size(); ai++)
        {
            const FreeAgent& a = c.fa[ai];
            const int val = fa_value(a.key, a.prog);
            const bool ct = fa_captain_type(a.key);
            auto cand = [&](int role, double gain) {
                if (contender)
                    gain += 0.25 * (val - 75);
                gain += fa_noise(c, a.key, club, 3.0, 1);
                if (gain > best)
                {
                    best = gain;
                    bestAgent = (int)ai;
                    bestRole = role;
                }
            };
            if (ct)
            {
                if (val >= capV + 4)
                    cand(PORT_FA_CAPTAIN, val - capV + 6.0);
                if (s.backup.empty())
                {
                    if (val >= 66)
                        cand(PORT_FA_BACKUP, val - 66 + 8.0);
                }
                else
                {
                    const int bkV = fa_occ_value(c, club, PORT_FA_BACKUP);
                    if (val >= bkV + 4)
                        cand(PORT_FA_BACKUP, (double)(val - bkV));
                }
            }
            for (int k = 0; k < 3; k++)
            {
                const int occ = fa_occ_value(c, club, PORT_FA_SIDEKICK1 + k);
                if (val >= occ + 5)
                    cand(PORT_FA_SIDEKICK1 + k, val - occ - (ct ? 4.0 : 0.0) - (k == 0 && !s.backup.empty() ? 4.0 : 0.0));
                // r173 (Lance: "more freedom to sign people who are legit rookie level/never played before"): a rookie is
                // signed on talent (its rating against the member's own, levels aside: it'll grow), for a field spot
                if (a.fresh)
                {
                    const std::string& m = s.mem[k];
                    const int occRating = m.empty() ? rating(s.kind) : rating(top_of(s, 2 + k));
                    if (rating(a.key) >= occRating + 2)
                        cand(PORT_FA_SIDEKICK1 + k, 3.0 + rating(a.key) - occRating - (k == 0 && !s.backup.empty() ? 3.0 : 0.0));
                }
            }
        }
        if (bestAgent < 0)
            continue;
        FreeAgent& a = c.fa[(size_t)bestAgent];
        FaBid b;
        b.key = a.key;
        b.club = club;
        b.role = bestRole;
        b.promise = bestRole == PORT_FA_BACKUP && fa_value(a.key, a.prog) >= capV - 2;
        b.relCap = false;
        b.week = c.phase == PHASE_REGULAR ? c.week : 0;
        c.bids.push_back(b);
        if (c.phase == PHASE_REGULAR && a.decide == 0)
            a.decide = std::min(kFaDeadline, c.week + 1);
    }
}

// The front of the week's news: the free-agency lines (signings first), the rest counted
void fa_news(Career& c, const std::vector<std::string>& lines)
{
    if (lines.empty())
        return;
    std::vector<std::string> top, rest;
    for (size_t i = 0; i < lines.size(); i++)
        (lines[i].find(" SPURNS ") != std::string::npos || lines[i].find(" SIGNS WITH ") != std::string::npos ? top : rest)
            .push_back(lines[i]);
    top.insert(top.end(), rest.begin(), rest.end());
    std::vector<std::string> out;
    for (size_t i = 0; i < top.size() && out.size() < 4; i++)
        out.push_back(top[i]);
    if (top.size() > out.size())
        out.push_back(std::to_string(top.size() - out.size()) + " MORE FREE-AGENCY MOVES");
    // r173: after the week's own stories (results' hat tricks, streaks, beef): r167 put these first, which pushed the real
    // stories out of the 8 lines the hub and STRIKERS ILLUSTRATED read
    c.news.insert(c.news.end(), out.begin(), out.end());
}

// The agents with offers, the best first (the stars decide first; the clubs that miss bid again)
std::vector<std::string> fa_bid_keys(const Career& c, bool dueOnly)
{
    std::vector<std::pair<int, std::string> > v;
    for (size_t i = 0; i < c.fa.size(); i++)
    {
        const FreeAgent& a = c.fa[i];
        bool has = false;
        for (size_t k = 0; k < c.bids.size() && !has; k++)
            has = c.bids[k].key == a.key;
        if (!has || (dueOnly && a.decide > c.week && c.week < kFaDeadline))
            continue;
        v.push_back(std::make_pair(fa_value(a.key, a.prog), a.key));
    }
    std::stable_sort(v.begin(), v.end(), [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
        return a.first > b.first;
    });
    std::vector<std::string> out;
    for (size_t i = 0; i < v.size(); i++)
        out.push_back(v[i].second);
    return out;
}

// Mid-season (finish_week, weeks 1-12): CPU offers, then the agents whose day it is decide; at the deadline all of them.
void fa_week(Career& c)
{
    if (c.phase != PHASE_REGULAR || c.week > kFaDeadline)
        return;
    fa_refresh(c);
    fa_cpu_round(c, 0.3, c.season * 100 + c.week);
    std::vector<std::string> lines;
    std::vector<int> signedClubs;
    const std::vector<std::string> keys = fa_bid_keys(c, true);
    for (size_t i = 0; i < keys.size(); i++)
        fa_decide(c, keys[i], signedClubs, lines);
    if (c.week >= kFaDeadline)
    {
        c.bids.clear();
        for (size_t i = 0; i < c.fa.size(); i++)
            c.fa[i].decide = 0;
    }
    fa_news(c, lines);
}

// The champion crowned: the off-season window opens, every CPU club with a need makes its offer (you see them and can
// beat them until the next season starts)
// r167 unrest (FRANCHISE-PLAN.md's A-Life: "they want your spot (a grown sidekick wants to be a captain)"): with every
// character dealt, the ones left over are the league's weakest, so a market needs players worth chasing. At the
// off-season a CPU club's player may test free agency - one a club at most: a member who has outgrown its captain, a backup
// better than its captain, or a front-runner / veteran stuck on one of the 8 worst records. Its personality says how
// likely (a hothead, showboat or front-runner 70%, an honorable one 15%, the rest 45%).
void fa_unrest(Career& c, std::vector<std::string>& lines)
{
    std::vector<int> byWins;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
        byWins.push_back(i);
    std::stable_sort(byWins.begin(), byWins.end(), [&](int a, int b) { return ranks_above(c, b, a); });  // worst first
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        if (is_human(c, i))
            continue;
        ClubState& s = c.clubs[i];
        const int capV = fa_occ_value(c, i, PORT_FA_CAPTAIN);
        const bool losing = std::find(byWins.begin(), byWins.begin() + 8, i) != byWins.begin() + 8;
        int slot = -1;
        std::string why;
        for (int k = 0; k < 3 && slot < 0; k++)
            if (!s.mem[k].empty() && fa_captain_type(s.mem[k]) && fa_occ_value(c, i, PORT_FA_SIDEKICK1 + k) >= capV + 3)
            {
                slot = 2 + k;
                why = "WANTS TO BE A CAPTAIN";
            }
        if (slot < 0 && !s.backup.empty() && fa_occ_value(c, i, PORT_FA_BACKUP) >= capV + 4)
        {
            slot = 1;
            why = "WANTS A CLUB OF ITS OWN";
        }
        if (slot < 0 && losing)
            for (int k = 0; k < 2 && slot < 0; k++)
            {
                if (k == 1 && s.backup.empty())
                    continue;
                const int p = (k == 0 ? s.capSt : s.bkSt).personality;
                if ((p == PERS_FRONTRUNNER || p == PERS_VETERAN) && (k == 1 || !s.backup.empty()))
                {
                    slot = k;
                    why = "WANTS A WINNER";
                }
            }
        if (slot < 0)
            continue;
        const int p = slot_stats(s, slot).personality;
        const double chance = p == PERS_HOTHEAD || p == PERS_SHOWBOAT || p == PERS_FRONTRUNNER ? 0.7 : (p == PERS_HONORABLE ? 0.15 : 0.45);
        if (rand01(c.rng) >= chance)
            continue;
        const std::string name = upper(name_of(slot_key(s, slot)));
        std::vector<std::string> rel;
        fa_release(c, i, slot, rel);
        if (slot == 0)  // its backup takes the captaincy
        {
            s.captain = s.backup;
            s.cap = s.bk;
            s.capSt = s.bkSt;
            s.backup.clear();
            init_prog(s.bk);
            init_stats(s.bkSt, (int)(fa_hash(c, s.captain, i, 79) % PERS_COUNT));
            s.startsBackup = false;
        }
        lines.push_back(name + " (" + upper(s_clubs[i].place) + ") TESTS FREE AGENCY: " + why);
        fprintf(stderr, "[franchise] free agency: %s leaves %s (%s)\n", name.c_str(), s_clubs[i].id.c_str(), why.c_str());
    }
    fflush(stderr);
}

void fa_offseason_open(Career& c)
{
    std::vector<std::string> unrest;
    fa_unrest(c, unrest);  // (records are the season just finished)
    fa_refresh(c);
    for (size_t i = 0; i < c.fa.size(); i++)
        c.fa[i].decide = 0;
    fa_cpu_round(c, 1.0, c.season * 100 + 99);
    int agents = 0;
    for (size_t i = 0; i < c.fa.size(); i++)
        for (size_t k = 0; k < c.bids.size(); k++)
            if (c.bids[k].key == c.fa[i].key)
            {
                agents++;
                break;
            }
    int rookies = 0;
    for (size_t i = 0; i < c.fa.size(); i++)
        rookies += c.fa[i].fresh ? 1 : 0;
    c.news.push_back("FREE AGENCY IS OPEN: " + std::to_string(c.fa.size()) + " FREE AGENTS, " + std::to_string(agents) +
                     " WANTED ALREADY");
    if (rookies > 0)  // r173: next year's rookies (never played, never evolved: first forms, Lv 8)
        c.news.push_back("ROOKIE CLASS: " + std::to_string(rookies) + " ROOKIES ENTER FREE AGENCY");
    for (size_t i = 0; i < unrest.size() && i < 3; i++)
        c.news.push_back(unrest[i]);
    if (unrest.size() > 3)
        c.news.push_back(std::to_string(unrest.size() - 3) + " MORE PLAYERS TEST FREE AGENCY");
}

// The next season starts: the off-season's offers decided, then two more rounds (the clubs that missed bid again)
void fa_offseason_resolve(Career& c)
{
    fa_refresh(c);
    std::vector<std::string> lines;
    for (int round = 0; round < 3; round++)
    {
        if (round > 0 || c.faRound != c.season * 100 + 99)
            fa_cpu_round(c, 1.0, c.season * 100 + 99);
        const std::vector<std::string> keys = fa_bid_keys(c, false);
        if (keys.empty())
            break;
        std::vector<int> signedClubs;
        for (size_t i = 0; i < keys.size(); i++)
            fa_decide(c, keys[i], signedClubs, lines);
        c.bids.clear();  // (offers that lost go; the clubs bid again next round)
    }
    c.bids.clear();
    for (size_t i = 0; i < c.fa.size(); i++)
        c.fa[i].decide = 0;
    fa_news(c, lines);
}

// r177 (Lance: retirements, so every off-season has a real rookie class): after the champion's crowned, at the next
// season's start (next_season, before free agency's decisions), characters retire - captains, backups, sidekick members,
// every club's, yours too. The chance grows with seasons played (its games / 20: 5% a season), more when its form has
// gone (-2 or worse), a veteran / an honorable one leaving on a high (form +2), a hothead / showboat / front-runner hanging
// on; at most kRetireMax a season, the likeliest first. Yours: never one you got this season, and only after a season's
// warning ("X IS THINKING ABOUT RETIREMENT" the off-season before). The send-off goes into the news, the feed, the hall
// of fame (its goals, seasons, titles); its beef ends. Its spot: the backup takes the captaincy (else a captain-class
// member, else the best captain in free agency); a member's slot goes back to its generic sidekick, the club refills it
// through free agency. Its line comes back the season after as a ROOKIE (fa_refresh: first form, Lv 8, no record, no
// shiny, a new personality) - its old career stays in the history.
const int kRetireMax = 14;
int seasons_played(const PStats& st) { return st.games / PORT_FR_WEEKS; }
double retire_chance(const PStats& st)
{
    const int n = seasons_played(st);
    if (n < 1)
        return 0.0;
    double p = 0.05 * n;  // (from the first off-season: every one has its returnees)
    if (st.form <= -2)
        p += 0.12;  // the legs have gone
    if ((st.personality == PERS_VETERAN && st.form >= 2) || (st.personality == PERS_HONORABLE && st.form >= 2))
        p += 0.12;  // out on a high
    if (st.personality == PERS_HOTHEAD || st.personality == PERS_SHOWBOAT || st.personality == PERS_FRONTRUNNER)
        p -= 0.05;  // hangs on
    return std::max(0.0, std::min(0.85, p));
}

void retire_player(Career& c, int club, int slot, std::vector<std::string>& lines)
{
    ClubState& s = c.clubs[club];
    settle_form(c, club, slot);
    const std::string key = slot_key(s, slot);
    PStats& st = slot_stats(s, slot);
    const std::string name = upper(name_of(key)), place = upper(s_clubs[club].place), id = char_id(key);
    const int seasons = std::max(1, seasons_played(st));
    int titles = 0;
    for (size_t i = 0; i < c.champions.size(); i++)
        if (c.champions[i].second == club && c.champions[i].first > c.season - seasons)
            titles++;
    const std::string career = std::to_string(st.goals) + " GOALS IN " + std::to_string(seasons) + (seasons == 1 ? " SEASON" : " SEASONS") +
                               (titles > 0 ? ", " + std::to_string(titles) + (titles == 1 ? " TITLE" : " TITLES") : std::string());
    lines.push_back("RETIRED: " + name + " (" + place + ") HANGS IT UP: " + career);
    c.history.insert(c.history.begin(), "SEASON " + std::to_string(c.season) + " RETIRED: " + name + " (" + place + "), " + career);
    post(c, st.personality, EV_RETIRED, name_of(key), s_clubs[club].place);
    auto done = [&](Nemesis* n) {
        n->club = -1;
        n->active = false;
        nem_log(*n, "SEASON " + std::to_string(c.season) + ": RETIRED");
    };
    each_nemesis(c, key, done);
    if (slot >= 2)
        each_nemesis(c, "sk#" + std::to_string(club) + "#" + name_of(key), done);
    fprintf(stderr, "[franchise] retired: %s (%s) after %d seasons, %d goals\n", key.c_str(), s_clubs[club].id.c_str(), seasons, st.goals);
    const int pers = is_human(c, club) ? PERS_NEUTRAL : (int)(fa_hash(c, key, club, 177) % PERS_COUNT);
    if (slot == 1)
    {
        s.backup.clear();
        init_prog(s.bk);
        init_stats(s.bkSt, pers);
        s.startsBackup = false;
    }
    else if (slot >= 2)
    {
        const int k = std::min(2, slot - 2);
        s.mem[k].clear();
        s.mp[k] = s.side;
        init_stats(s.sk[k], pers);
        s.skAge[k] = 0;
    }
    else if (!s.backup.empty())  // the captain: its backup takes the armband
    {
        s.captain = s.backup;
        s.cap = s.bk;
        s.capSt = s.bkSt;
        s.top[0] = s.top[1];
        s.backup.clear();
        s.top[1].clear();
        init_prog(s.bk);
        init_stats(s.bkSt, pers);
        s.startsBackup = false;
        lines.push_back(upper(s_clubs[club].place) + " MAKE " + upper(name_of(s.captain)) + " THEIR CAPTAIN");
    }
    else
    {
        int best = -1;  // a captain-class member, else the best captain in free agency
        for (int k = 0; k < 3; k++)
            if (!s.mem[k].empty() && fa_captain_type(s.mem[k]) && (best < 0 || fa_value(s.mem[k], s.mp[k]) > fa_value(s.mem[best], s.mp[best])))
                best = k;
        if (best >= 0)
        {
            s.captain = s.mem[best];
            s.cap = s.mp[best];
            s.capSt = s.sk[best];
            s.mem[best].clear();
            s.mp[best] = s.side;
            init_stats(s.sk[best], pers);
        }
        else
        {
            fa_refresh(c);
            int fi = -1;
            for (size_t i = 0; i < c.fa.size(); i++)
                if (fa_captain_type(c.fa[i].key) && (fi < 0 || fa_value(c.fa[i].key, c.fa[i].prog) > fa_value(c.fa[fi].key, c.fa[fi].prog)))
                    fi = (int)i;
            if (fi >= 0)
            {
                s.captain = c.fa[fi].key;
                s.cap = c.fa[fi].prog;
                s.capSt = c.fa[fi].st;
                c.fa.erase(c.fa.begin() + fi);
            }
            // (no captain anywhere: it plays on - a roster too small for this to come up)
            else
                return;
        }
        s.top[0].clear();
        lines.push_back(upper(s_clubs[club].place) + " MAKE " + upper(name_of(s.captain)) + " THEIR CAPTAIN");
    }
    s.top[std::max(0, std::min(4, slot))].clear();
    c.returning.push_back(std::make_pair(id, c.season + 1));  // (back as a rookie next season)
}

void fa_retirements(Career& c)
{
    struct Cand
    {
        int club, slot;
        double p;
    };
    std::vector<Cand> cands;
    std::vector<std::string> warnNow, lines;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = c.clubs[i];
        for (int slot = 0; slot < 5; slot++)
        {
            const std::string& key = slot_key(s, slot);
            if (key.empty())
                continue;
            const double p = retire_chance(slot_stats(s, slot));
            if (p <= 0.0 || rand01(c.rng) >= p)
                continue;
            if (is_human(c, i))
            {
                const std::string id = char_id(key);
                bool newHere = false, warned = false;
                for (size_t j = 0; j < c.joined.size(); j++)
                    newHere = newHere || (c.joined[j].first == id && c.joined[j].second >= c.season);
                warned = std::find(c.retireWarn.begin(), c.retireWarn.end(), id) != c.retireWarn.end();
                if (newHere)
                    continue;
                if (!warned)  // a heads-up first: it retires next off-season if it still wants to
                {
                    warnNow.push_back(id);
                    c.news.push_back(upper(name_of(key)) + " IS THINKING ABOUT RETIREMENT");
                    if (i == c.myClub)
                        c.headline = name_of(key) + " is thinking about retirement after this season.";
                    continue;
                }
            }
            cands.push_back(Cand{i, slot, p});
        }
    }
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.p > b.p; });
    if ((int)cands.size() > kRetireMax)
        cands.resize(kRetireMax);
    // (highest slots first within a club: a captain's retirement may move a member up)
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.club != b.club ? a.club < b.club : a.slot > b.slot; });
    for (size_t i = 0; i < cands.size(); i++)
        retire_player(c, cands[i].club, cands[i].slot, lines);
    c.retireWarn = warnNow;  // (last year's warnings are spent: retired, or changed their mind)
    for (int i = (int)c.joined.size() - 1; i >= 0; i--)  // (keep only this and next season's)
        if (c.joined[(size_t)i].second < c.season)
            c.joined.erase(c.joined.begin() + i);
    if (!lines.empty())
    {
        std::vector<std::string> out;
        for (size_t i = 0; i < lines.size() && out.size() < 4; i++)
            if (lines[i].compare(0, 8, "RETIRED:") == 0)
                out.push_back(lines[i]);
        int retired = 0;
        for (size_t i = 0; i < lines.size(); i++)
            retired += lines[i].compare(0, 8, "RETIRED:") == 0 ? 1 : 0;
        if (retired > (int)out.size())
            out.push_back(std::to_string(retired - (int)out.size()) + " MORE RETIREMENTS: THEIR LINES RETURN AS ROOKIES NEXT SEASON");
        c.news.insert(c.news.begin(), out.begin(), out.end());
    }
    fprintf(stderr, "[franchise] season %d retirements: %d (%d warned)\n", c.season, (int)cands.size(), (int)warnNow.size());
    fflush(stderr);
}

// r131 (Lance: "each character is only chosen once per franchise ... i wanna upload more pokemon and maybe digimon
// characters today"): the new season's signings, the CPU clubs' only (a person's club waits for its person: the member
// editor). A character on two clubs (a career from before r131: an r130 member pick of yours could be a CPU club's
// captain) stays with the first - a person's club before any other - and the others sign a free agent in its place (a
// captain with none free promotes its backup; a roster too small keeps it); then every CPU club fills its empty spots
// (no backup, a generic sidekick) from the free agents left - characters on no club, like ones added to the roster since -
// the worst record first, a snake as the deal's. Each joins at its spot's level (a backup at its captain's, a member at
// the sidekicks').
void season_signings(Career& c)
{
    const std::vector<Captain> caps = captains();
    const std::vector<Kind> ks = kinds();
    std::unordered_map<std::string, int> holder;  // a character -> the club keeping it
    for (int pass = 0; pass < 2; pass++)          // the people's clubs first
        for (int i = 0; i < PORT_FR_CLUBS; i++)
        {
            if (is_human(c, i) != (pass == 0))
                continue;
            std::vector<std::string> ids;
            club_ids(c.clubs[i], ids);
            for (size_t k = 0; k < ids.size(); k++)
                if (holder.find(ids[k]) == holder.end())
                    holder[ids[k]] = i;
        }
    // r167: the free agents are free agency's own (c.fa: the ones never dealt and the released, each with its level, record
    // and personality); what the bidding left, the best first (its value, up to 5 points of luck as r131's)
    (void)caps;
    (void)ks;
    s_holdReturning = true;  // r177: this off-season's retirees wait for next season's free agency
    fa_refresh(c);
    std::vector<int> faOrder;
    std::vector<double> faScore(c.fa.size(), 0.0);
    for (size_t i = 0; i < c.fa.size(); i++)
    {
        faOrder.push_back((int)i);
        faScore[i] = fa_value(c.fa[i].key, c.fa[i].prog) + 5.0 * rand01(c.rng);
    }
    std::stable_sort(faOrder.begin(), faOrder.end(), [&](int a, int b) { return faScore[a] > faScore[b]; });
    std::vector<bool> gone(c.fa.size(), false);
    int fi = -1;  // the one sign() took last
    auto sign = [&](bool captainOnly) -> std::string {
        fi = -1;
        for (size_t j = 0; j < faOrder.size(); j++)
        {
            const int i = faOrder[j];
            if (!gone[i] && (!captainOnly || fa_captain_type(c.fa[i].key)))
            {
                gone[i] = true;
                fi = i;
                return c.fa[i].key;
            }
        }
        return std::string();
    };
    // (r131: at its spot's level; r167: a released one keeps its own level and record)
    auto fresh = [&](Prog& p, int level) {
        if (fi >= 0 && !c.fa[fi].fresh)
        {
            p = c.fa[fi].prog;
            p.startLevel = p.level;
            p.declinedAt = 0;
            return;
        }
        init_prog(p);
        if (fi < 0)  // r173: a rookie signed here stays a rookie (Lv 8); r131's spot level only without one
            p.level = p.startLevel = std::max(8, std::min(100, level));
    };
    auto stats_of = [&](PStats& st) {
        if (fi >= 0 && c.fa[fi].fresh)  // r177: a rookie's first season (Rookie of the Year)
            c.rookieOf.push_back(std::make_pair(char_id(c.fa[fi].key), c.season));
        if (fi >= 0)
            st = c.fa[fi].st;
        else
            init_stats(st, (int)(next_rand(c.rng) % PERS_COUNT));
    };
    std::vector<std::string> lines;
    int signings = 0;
    auto joined = [&](int club, const std::string& key, const std::string& was) {
        holder[char_id(key)] = club;
        signings++;
        if (lines.size() < 6)
            lines.push_back("SIGNED: " + upper(name_of(key)) + " JOINS " + upper(s_clubs[club].place) +
                            (was.empty() ? std::string() : " (FOR " + upper(name_of(was)) + ")"));
    };
    // a released copy's beef goes with the one that kept the character
    auto released = [&](int club, const std::string& key) {
        std::unordered_map<std::string, int>::const_iterator it = holder.find(char_id(key));
        const int keeper = it != holder.end() ? it->second : -1;
        each_nemesis(c, key, [&](Nemesis* n) {
            if (n->club != club)
                return;
            n->club = keeper;
            n->active = false;
            nem_log(*n, "SEASON " + std::to_string(c.season) + ": LEFT " + upper(s_clubs[club].place));
        });
    };
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        if (is_human(c, i))
            continue;
        ClubState& s = c.clubs[i];
        auto dup = [&](const std::string& key) {
            std::unordered_map<std::string, int>::const_iterator it = holder.find(char_id(key));
            return !key.empty() && it != holder.end() && it->second != i;
        };
        if (dup(s.captain))
        {
            const std::string was = s.captain;
            const std::string fk = sign(true);
            if (!fk.empty() || (!s.backup.empty() && !dup(s.backup)))
                released(i, was);  // (else it stays: a roster with too few captains)
            if (!fk.empty())
            {
                s.captain = fk;
                fresh(s.cap, s.cap.level);
                stats_of(s.capSt);  // r167
                joined(i, fk, was);
            }
            else if (!s.backup.empty() && !dup(s.backup))
            {
                s.captain = s.backup;
                s.cap = s.bk;
                s.capSt = s.bkSt;
                s.backup.clear();
                init_prog(s.bk);
                init_stats(s.bkSt, (int)(next_rand(c.rng) % PERS_COUNT));
                s.startsBackup = false;
                c.news.push_back(upper(s_clubs[i].place) + " MAKE " + upper(name_of(s.captain)) + " THEIR CAPTAIN");
            }
        }
        if (dup(s.backup))
        {
            const std::string was = s.backup;
            released(i, was);
            s.backup = sign(true);
            s.startsBackup = false;
            fresh(s.bk, s.cap.level);
            stats_of(s.bkSt);  // r167
            if (!s.backup.empty())
                joined(i, s.backup, was);
        }
        for (int k = 0; k < 3; k++)
            if (dup(s.mem[k]))
            {
                const std::string was = s.mem[k];
                s.mem[k] = sign(false);
                fresh(s.mp[k], s.side.level);
                stats_of(s.sk[k]);  // r167
                s.skAge[k] = 0;
                if (!s.mem[k].empty())
                    joined(i, s.mem[k], was);
            }
        if (!generic_kind(s.kind) && dup(s.kind))
        {
            // its kind was a Charged sidekick guest another club keeps: Strikers sidekicks (new names) instead
            s.kind = kind_for_members(c, s.mem);
            std::vector<std::string> usedNames;
            for (int k = 0; k < 3; k++)
                s.names[k] = make_name(c, s.kind, i, k, usedNames);
        }
    }
    // the empty spots, the worst record first: backups (a captain), then the sidekick members (on the field first)
    std::vector<int> order;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
        if (!is_human(c, i))
            order.push_back(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return ranks_above(c, b, a); });
    const int n = (int)order.size();
    for (int round = 0; round < 4; round++)
        for (int j = 0; j < n; j++)
        {
            const int club = order[round % 2 == 0 ? j : n - 1 - j];
            ClubState& s = c.clubs[club];
            if (round == 0)
            {
                if (!s.backup.empty())
                    continue;
                const std::string fk = sign(true);
                if (fk.empty())
                    continue;
                s.backup = fk;
                s.startsBackup = false;
                fresh(s.bk, s.cap.level);
                stats_of(s.bkSt);  // r167
                joined(club, fk, std::string());
                continue;
            }
            const int prefs[2][3] = {{0, 1, 2}, {1, 2, 0}};  // (with a co-captain slot 1 sits: last)
            const int* pref = prefs[s.backup.empty() ? 0 : 1];
            int slot = -1;
            for (int p = 0; p < 3 && slot < 0; p++)
                if (s.mem[pref[p]].empty())
                    slot = pref[p];
            if (slot < 0)
                continue;
            const std::string fk = sign(false);
            if (fk.empty())
                continue;
            s.mem[slot] = fk;
            fresh(s.mp[slot], s.side.level);
            stats_of(s.sk[slot]);  // r167
            s.skAge[slot] = 0;
            joined(club, fk, std::string());
        }
    c.news.insert(c.news.end(), lines.begin(), lines.end());
    if (signings > (int)lines.size())
        c.news.push_back(std::to_string(signings - (int)lines.size()) + " MORE FREE AGENTS SIGNED AROUND THE LEAGUE");
    const int before = (int)c.fa.size();
    for (int i = (int)c.fa.size() - 1; i >= 0; i--)  // r167: the signed ones leave free agency
        if (gone[(size_t)i])
            c.fa.erase(c.fa.begin() + i);
    fa_refresh(c);
    s_holdReturning = false;
    fprintf(stderr, "[franchise] season %d signings: %d (%d free agents were left, %d still free)\n", c.season, signings, before,
            (int)c.fa.size());
    fflush(stderr);
}

// r70b: the champion crowned: next season, the same clubs and players, a new schedule.
void next_season(Career& c)
{
    c.news.clear();
    c.feed.clear();
    off_season(c);  // r71: retirements, rookies, the trade window
    fa_retirements(c);  // r177: the characters who retire (their lines return as rookies next season)
    fa_offseason_resolve(c);  // r167: the free agents decide (last season's records count), before r131's fill
    c.season++;
    c.week = 1;
    season_signings(c);  // r131: one character, one club; free agents (new guests too) fill the CPU clubs' empty spots
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = c.clubs[i];
        s.wins = s.losses = s.goalsFor = s.goalsAgainst = 0;
        s.cap.startLevel = s.cap.level;  // r77: this season's levels count from here
        s.bk.startLevel = s.bk.level;
        s.side.startLevel = s.side.level;
        for (int m = 0; m < 3; m++)  // r130: the sidekick members' too
            s.mp[m].startLevel = s.mp[m].level;
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
    // r130 (Lance: "let ANY spot be ANY character"): the clubs' Strikers-captain sidekick members play their own class
    // only (include/port/lineup.h), so the guest captains below (who can move) leave those classes free when they can; a
    // guest captain that finds nothing else free still takes one (that member then plays the club's sidekick class, logged)
    bool memberHeld[8] = {};
    for (int side = 0; side < 2; side++)
    {
        const ClubState& ms = c.clubs[clubs[side]];
        for (int k = co[side] != NULL ? 1 : 0; k < 3; k++)
        {
            const int p = member_pick(ms.mem[k]);
            if (p >= PORT_GUEST_ORIGINAL && p < PORT_GUEST_ORIGINAL + 8 && free[p - PORT_GUEST_ORIGINAL])
            {
                free[p - PORT_GUEST_ORIGINAL] = false;
                memberHeld[p - PORT_GUEST_ORIGINAL] = true;
            }
        }
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
            if (slot4[k] < 0)  // r130: nothing else free: a member's class after all
            {
                bool any[8];
                for (int t = 0; t < 8; t++)
                    any[t] = free[t] || memberHeld[t];
                slot4[k] = who4[k]->guest >= 0 ? PortGuestSlotFor(who4[k]->guest, any) : -1;
                for (int t = 0; t < 8 && slot4[k] < 0; t++)
                    if (any[t])
                        slot4[k] = t;
                if (slot4[k] >= 0)
                    memberHeld[slot4[k]] = false;
            }
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
        // r130: the club's sidekick members, each in its own slot (m_ID 1-3; slot 1 is the co-captain's when there is
        // one), each with its own shiny; a generic one is the club's kind
        const ClubState& cs = c.clubs[clubs[side]];
        const bool members = !cs.mem[0].empty() || !cs.mem[1].empty() || !cs.mem[2].empty();
        if (members)
        {
            int picks[3];
            for (int k = 0; k < 3; k++)
            {
                picks[k] = kind[side]->guest >= 0 ? kind[side]->guest : -1;
                if (!cs.mem[k].empty())
                    picks[k] = member_pick(cs.mem[k]);  // r130: a Strikers captain too (its own class, below)
            }
            PortGuestSetSlotPicks(slot[side], picks, co[side] != NULL ? 1 : 0);
        }
        for (int k = 0; k < 3; k++)
            PortGuestSetSlotShiny(side, k + 1, members && !cs.mem[k].empty() && cs.mp[k].shiny);
    }
    // r130 (Lance: "let ANY spot be ANY character"): each member's own class (a Strikers captain, a guest captain on its
    // base or the nearest free captain class; include/port/lineup.h), each captain class once on the field after both
    // captains and the co-captains; one whose class is taken this match plays the club's sidekick class (logged). The CPU
    // clubs (no members: their slot picks cleared by the sidekick pick above) keep their generic sidekicks.
    PortApplySlotCaptains();
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

// r130 / r131: three sidekick-slot menu picks as member keys (a guest's pack, strikers_<name>; "" a generic one: a Strikers
// sidekick, or the club's kind's own Charged sidekick guest). r131: never the same character as the club's captain or
// backup, nor a named character twice (a Charged sidekick guest may fill several slots, as a kind does): those slots
// get a generic sidekick.
void member_keys(const int* picks, const std::string& kind, const std::string& captain, const std::string& backup,
                 std::string out[3])
{
    const std::string kindGuest = generic_kind(kind) ? std::string() : kind;
    const std::string capId = char_id(captain), bkId = char_id(backup);
    for (int k = 0; k < 3; k++)
    {
        out[k].clear();
        if (picks == NULL)
            continue;
        const char* pack = picks[k] >= 0 && picks[k] < PORT_GUEST_ORIGINAL ? PortGuestRosterPack(picks[k]) : NULL;
        if (pack != NULL && kindGuest != pack)
            out[k] = pack;
        // r130 (Lance: "let ANY spot be ANY character"): a Strikers captain itself, keyed as the franchise's captains are
        if (picks[k] >= PORT_GUEST_ORIGINAL && picks[k] < PORT_GUEST_ORIGINAL + 8)
            out[k] = std::string("strikers_") + kTeamKey[picks[k] - PORT_GUEST_ORIGINAL];
        const std::string id = char_id(out[k]);
        if (id.empty())
            continue;
        bool twice = id == capId || id == bkId;
        for (int j = 0; j < k && !twice && !sk_guest(out[k]); j++)
            twice = char_id(out[j]) == id;
        if (twice)
        {
            fprintf(stderr, "[franchise] sidekick %d: %s is one of your players already: a generic sidekick there\n", k + 1,
                    out[k].c_str());
            out[k].clear();
        }
    }
}

bool new_career(int slot, int club, const MyPicks& mine);
}  // namespace

bool PortFranchiseCaptainPickable(int pick) { return !captain_key_for(pick).empty(); }

// r130 (Lance: "4 different levels for 4 different people for 4 different rewards"): your club's three sidekick members
// (menu picks: a guest's roster index, or a Strikers sidekick / the club's own kind for a generic one). A new member joins
// at the sidekicks' level and grows on its own from there.
bool PortFranchiseSetMembers(const int picks[3])
{
    if (!s_active || s_match != MATCH_NONE || coop_blocked() || in_league())
        return false;
    Career& c = s_career;
    ClubState& s = c.clubs[c.myClub];
    for (int k = 2; k < 5; k++)
        settle_form(c, c.myClub, k);  // r167: the members as their highest forms (a slot's pick doesn't follow a move)
    // r130 (review): a member kept (in any slot) keeps its own level, evolutions and shiny
    const std::string oldMem[3] = {s.mem[0], s.mem[1], s.mem[2]};
    const Prog oldMp[3] = {s.mp[0], s.mp[1], s.mp[2]};
    bool oldUsed[3] = {false, false, false};
    std::string keys[3];
    member_keys(picks, s.kind, s.captain, s.backup, keys);  // r131: not your captains again, no one twice
    // r131 (Lance: "each character is only chosen once per franchise"): one another club has stays there (the menu greys
    // them); yours already stay yours (a duplicate from a career before r131 too)
    for (int k = 0; k < 3; k++)
    {
        if (keys[k].empty() || keys[k] == oldMem[0] || keys[k] == oldMem[1] || keys[k] == oldMem[2])
            continue;
        const int h = holder_of(c, char_id(keys[k]), c.myClub);
        if (h >= 0)
        {
            fprintf(stderr, "[franchise] sidekick %d: %s plays for %s: a generic sidekick there\n", k + 1, keys[k].c_str(),
                    s_clubs[h].id.c_str());
            keys[k].clear();
        }
    }
    bool placed[3] = {false, false, false};
    for (int pass = 0; pass < 2; pass++)  // the same slot first, then any other
        for (int k = 0; k < 3; k++)
        {
            if (placed[k] || keys[k].empty())
                continue;
            for (int j = 0; j < 3; j++)
                if (!oldUsed[j] && oldMem[j] == keys[k] && (pass == 1 || j == k))
                {
                    oldUsed[j] = placed[k] = true;
                    s.mem[k] = keys[k];
                    s.mp[k] = oldMp[j];
                    break;
                }
        }
    for (int k = 0; k < 3; k++)
    {
        if (placed[k])
            continue;
        s.mem[k] = keys[k];
        s.mp[k] = s.side;  // joins at the sidekicks' level
        s.mp[k].shiny = false;
        s.mp[k].declinedAt = 0;
        s.mp[k].startLevel = s.mp[k].level;
    }
    c.headline = "Your sidekicks: " + (s.mem[0].empty() ? s.names[0] : name_of(s.mem[0])) + ", " +
                 (s.mem[1].empty() ? s.names[1] : name_of(s.mem[1])) + " and " + (s.mem[2].empty() ? s.names[2] : name_of(s.mem[2])) + ".";
    save();
    fprintf(stderr, "[franchise] your sidekicks: %s / %s / %s\n", s.mem[0].empty() ? "(generic)" : s.mem[0].c_str(),
            s.mem[1].empty() ? "(generic)" : s.mem[1].c_str(), s.mem[2].empty() ? "(generic)" : s.mem[2].c_str());
    fflush(stderr);
    return true;
}

// r130 (review): the member editor's start: your members now (roster indexes, -1 a generic one) + your captain and
// backup (so they can't be picked again)
bool PortFranchiseMemberPicks(int members[3], int* captain, int* backup)
{
    members[0] = members[1] = members[2] = -1;
    *captain = *backup = -1;
    if (!s_active)
        return false;
    const ClubState& s = s_career.clubs[s_career.myClub];
    // r130: a Strikers captain (member, captain or backup) as its menu pick, PORT_GUEST_ORIGINAL + its eTeamID
    for (int k = 0; k < 3; k++)
        if (!s.mem[k].empty())
            members[k] = member_pick(s.mem[k]);
    *captain = member_pick(s.captain);
    if (!s.backup.empty())
        *backup = member_pick(s.backup);
    return true;
}

bool PortFranchiseNewPicked(int slot, int club, int captainPick, int backupPick, int sidekickPick, const int* memberPicks)
{
    read_data();
    MyPicks mine;
    mine.captain = captain_key_for(captainPick);
    mine.backup = backupPick < 0 ? std::string() : captain_key_for(backupPick);
    mine.noBackup = backupPick < 0;
    mine.kind = kind_key_for(sidekickPick);
    if (mine.captain.empty() || mine.kind.empty() || mine.backup == mine.captain || (!mine.noBackup && mine.backup.empty()) ||
        (!mine.backup.empty() && char_id(mine.backup) == char_id(mine.captain)))  // r131: one character, one spot
    {
        fprintf(stderr, "[franchise] new career: picks %d/%d/%d not usable\n", captainPick, backupPick, sidekickPick);
        fflush(stderr);
        return false;
    }
    // r131 (Lance: "you pick first, and the league forms around your picks"): your three sidekick members, dealt with you
    member_keys(memberPicks, mine.kind, mine.captain, mine.backup, mine.mem);
    fprintf(stderr, "[franchise] your picks: captain %s, backup %s, sidekicks %s (%s / %s / %s)\n", mine.captain.c_str(),
            mine.noBackup ? "(none)" : mine.backup.c_str(), mine.kind.c_str(), mine.mem[0].empty() ? "-" : mine.mem[0].c_str(),
            mine.mem[1].empty() ? "-" : mine.mem[1].c_str(), mine.mem[2].empty() ? "-" : mine.mem[2].c_str());
    fflush(stderr);
    return new_career(slot, club, mine);
}

// r131: see franchise.h
void PortFranchisePicksTaken(const int* picks, int count, const int* held, int heldCount, bool career, unsigned char* out)
{
    if (out == NULL || count <= 0)
        return;
    memset(out, 0, (size_t)count);
    if (picks == NULL)
        return;
    read_data();
    std::vector<std::string> ids;  // the characters taken
    for (int i = 0; held != NULL && i < heldCount; i++)
    {
        const std::string key = pick_key(held[i]);
        if (!sk_guest(key))  // (a Charged sidekick guest may fill several of your slots)
            ids.push_back(char_id(key));
    }
    if (career && s_active)
    {
        const Career& c = s_career;
        const ClubState& me = c.clubs[c.myClub];
        ids.push_back(char_id(me.captain));
        ids.push_back(char_id(me.backup));
        std::vector<std::string> mine;  // your members stay yours (a duplicate from a career before r131 too)
        for (int k = 0; k < 3; k++)
            mine.push_back(char_id(me.mem[k]));
        std::vector<std::string> theirs;
        for (int i = 0; i < PORT_FR_CLUBS; i++)
            if (i != c.myClub)
                club_ids(c.clubs[i], theirs);
        for (size_t k = 0; k < theirs.size(); k++)
            if (std::find(mine.begin(), mine.end(), theirs[k]) == mine.end())
                ids.push_back(theirs[k]);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    for (int i = 0; i < count; i++)
    {
        const std::string id = char_id(pick_key(picks[i]));
        if (!id.empty() && std::binary_search(ids.begin(), ids.end(), id))
            out[i] = 1;
    }
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
unsigned PortFranchiseCareerSeed() { return s_active ? s_career.seed : 0u; }  // r153
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
        copy_str(v->kindKey, sizeof v->kindKey, kind_portrait(me.kind));  // r116
    }
    for (int k = 0; k < 3; k++)
        copy_str(v->sidekicks[k], sizeof v->sidekicks[k], sk_name(me, k));
    v->nextOpponent = -1;
    if (Game* g = my_game(c))
    {
        v->nextHome = g->home == c.myClub;
        v->nextOpponent = v->nextHome ? g->away : g->home;
        const ClubState& o = c.clubs[v->nextOpponent];
        v->oppWins = o.wins;
        v->oppLosses = o.losses;
        copy_str(v->oppCaptain, sizeof v->oppCaptain, captain_name(starter(o)));
        // r131: and its co-captain (the other captain: both play) and its three sidekicks by name
        if (!o.backup.empty())
            copy_str(v->oppCoCaptain, sizeof v->oppCoCaptain, captain_name(o.startsBackup ? o.captain : o.backup));
        for (int k = 0; k < 3; k++)
            copy_str(v->oppSidekicks[k], sizeof v->oppSidekicks[k], sk_name(o, k));
    }
    for (int k = 0; k < 3; k++)  // r173: your sidekick slots' characters (the trades' pieces)
        copy_str(v->memberKeys[k], sizeof v->memberKeys[k], c.clubs[c.myClub].mem[k]);
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
    // r113: the games behind those results, as numbers (the newspaper's logo v logo)
    v->playedCount = 0;
    v->lastWeek = 0;
    for (size_t i = 0; i < c.games.size() && v->playedCount < 16; i++)
    {
        const Game& g = c.games[i];
        if (g.week != c.week - 1 || !g.played)
            continue;
        PortFranchiseView::Played& p = v->played[v->playedCount++];
        p.home = g.home;
        p.away = g.away;
        p.homeGoals = g.homeGoals;
        p.awayGoals = g.awayGoals;
        p.overtime = g.overtime;
        p.mine = g.home == c.myClub || g.away == c.myClub;
        v->lastWeek = g.week;
    }
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        const ClubState& cs = c.clubs[i];
        copy_str(v->clubCaptainKey[i], sizeof v->clubCaptainKey[i],
                 cs.startsBackup && !cs.backup.empty() ? cs.backup : cs.captain);
    }
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
        std::string key;  // r113: its portrait
    };
    std::vector<Who> all;
    const std::vector<Captain> caps = captains();
    // r113: a sidekick kind's portrait (the Strikers ones are strikers_<name>, Hammer Bro without the s)
    auto kind_portrait = [](const std::string& kind) {
        for (int k = 0; k < 4; k++)
            if (kind == kSkKey[k])
                return std::string(k == 2 ? "strikers_hammerbro" : std::string("strikers_") + kSkKey[k]);
        return kind;
    };
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        const ClubState& s = c.clubs[i];
        const Captain* cp = find_captain(caps, s.captain);
        all.push_back(Who{cp != NULL ? cp->name : s.captain, i, s.capSt.seasonGoals, s.capSt.form, s.captain});
        if (!s.backup.empty())
        {
            const Captain* bp = find_captain(caps, s.backup);
            all.push_back(Who{bp != NULL ? bp->name : s.backup, i, s.bkSt.seasonGoals, s.bkSt.form, s.backup});
        }
        for (int k = 0; k < 3; k++)
            all.push_back(Who{sk_name(s, k), i, s.sk[k].seasonGoals, s.sk[k].form,
                              s.mem[k].empty() ? kind_portrait(s.kind) : s.mem[k]});  // r131: a member's own portrait
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
            copy_str(out[i].key, sizeof out[i].key, l[i].key);
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
        copy_str(o.title, sizeof o.title, title_name(n.title));
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
    ClubState& s = s_career.clubs[s_career.myClub];
    for (int b = 0; b < 5; b++)  // r130: and the sidekick members (slots 2-4)
    {
        if (onlySlot >= 0 && b != onlySlot)
            continue;
        const std::string key = top_of(s, b);  // r167: evolving goes on from its highest form (it may play as an earlier one)
        const Prog& p = slot_prog(s, b);
        if (key.empty() || (!postponedToo && p.declinedAt == p.level))
            continue;
        std::string to[5];
        const int n = evolutions_for(key, p, to);
        if (n == 0)
            continue;
        out->valid = true;
        out->backup = b == 1;
        out->slot = b;
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

bool PortFranchiseReadyEvolutionFor(int slot, PortFranchiseEvolution* out) { return find_evolution(out, true, slot); }  // r167

bool PortFranchiseEvolve(int choice, bool* shiny, bool* newShiny)
{
    *shiny = *newShiny = false;
    PortFranchiseEvolution e;
    if (!PortFranchisePendingEvolution(&e))
        return false;
    return PortFranchiseEvolvePlayer(e.slot, choice, shiny, newShiny);
}

static bool evolve_slot_impl(int slot, int choice, bool* shiny, bool* newShiny);

bool PortFranchiseEvolveSlot(bool backup, int choice, bool* shiny, bool* newShiny)
{
    return PortFranchiseEvolvePlayer(backup ? 1 : 0, choice, shiny, newShiny);
}

// r130: any of your players by slot (0 captain, 1 backup, 2-4 the sidekick members)
bool PortFranchiseEvolvePlayer(int slot, int choice, bool* shiny, bool* newShiny)
{
    const bool backup = slot == 1;
    *shiny = *newShiny = false;
    if (!in_league())
        return evolve_slot_impl(slot, choice, shiny, newShiny);
    // r100: an online league's: an action on the league's career (kept when it's still due on a newer copy)
    static bool s_evolved, s_shiny, s_newShiny;
    s_evolved = s_shiny = s_newShiny = false;
    (void)backup;
    league_do(choice < 0 ? "postpone" : "evolve", [slot, choice]() {
        PortFranchiseEvolution e;
        if (!find_evolution(&e, true, slot))
            return false;  // no longer due (evolved already)
        s_evolved = evolve_slot_impl(slot, choice, &s_shiny, &s_newShiny);
        return true;
    });
    *shiny = s_shiny;
    *newShiny = s_newShiny;
    return s_evolved;
}

static bool evolve_slot_impl(int slot, int choice, bool* shiny, bool* newShiny)
{
    *shiny = *newShiny = false;
    PortFranchiseEvolution e;
    if (!find_evolution(&e, true, slot))
        return false;
    Career& c = s_career;
    ClubState& s = c.clubs[c.myClub];
    Prog& p = slot_prog(s, e.slot);
    if (choice < 0 || choice >= e.options)
    {
        // r95: postponed: not asked again until its next level; the Evo Watch page's A evolves it any time
        p.declinedAt = p.level;
        c.headline = PortCostumeKey(e.fromKey)  // r111; r183: an original's uniforms: a Marvel hero's next costume
                         ? std::string(e.fromName) + " will wait. Suit up any time: Evo Watch page, A."
                         : std::string(e.fromName) + " will wait. Evolve it any time: Evo Watch page, A.";
        save();
        fprintf(stderr, "[franchise] evolution postponed: %s at Lv %d\n", e.fromKey, p.level);
        fflush(stderr);
        return false;
    }
    const bool was = p.shiny;
    settle_form(c, c.myClub, e.slot);  // r167: from its highest form (picked an earlier one: back to it first)
    do_evolve_slot(c, c.myClub, e.slot, e.toKey[choice]);
    *shiny = p.shiny;
    *newShiny = p.shiny && !was;
    c.headline = PortCostumeKey(e.fromKey)  // r110; r183: a Marvel hero's next costume
                     ? std::string("Your ") + e.fromName + " suited up: " + e.toName[choice] + "!"
                     : std::string("Your ") + e.fromName + " evolved into " + e.toName[choice] + (*newShiny ? " - and it's SHINY!" : "!");
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

// r137 (Lance: weather + night games, looks only; src/platform/xr_weather.h): the calendar. A club's regular season
// (xr_weather_cal.h): the last 7 games 3 snowy, 2-3 rain games before, night for derbies (the second meeting with a
// division rival) + 2 more; the nemesis games and every playoff game at night; one snowy playoff game at most, at a
// cold home. Stable across reloads: the career's seed, the season and the club's own schedule only.
static int wx_climate(int club)
{
    if (club < 0 || club >= (int)s_clubs.size())
        return wxcal::kClimateNormal;
    if (s_clubs[club].stadium == 6)
        return wxcal::kClimateCovered;  // the Forbidden Dome
    if (s_clubs[club].div == "Inferno")
        return wxcal::kClimateHot;
    if (s_clubs[club].div == "Glacier")
        return wxcal::kClimateCold;
    return wxcal::kClimateNormal;
}

static int wx_game(const Career& c, const Game& g, int club)
{
    const int where = wx_climate(g.home);
    if (g.series >= 0)
    {
        bool earlier = false;
        for (size_t i = 0; i < c.games.size(); i++)
        {
            const Game& h = c.games[i];
            if (h.series >= 0 && h.week < g.week && (h.home == club || h.away == club) &&
                wx_climate(h.home) == wxcal::kClimateCold)
                earlier = true;
        }
        return wxcal::filter(wxcal::playoff(c.seed, c.season, where, earlier), where);
    }
    std::vector<const Game*> mine;
    for (size_t i = 0; i < c.games.size(); i++)
        if (c.games[i].series < 0 && (c.games[i].home == club || c.games[i].away == club))
            mine.push_back(&c.games[i]);
    std::stable_sort(mine.begin(), mine.end(), [](const Game* a, const Game* b) { return a->week < b->week; });
    std::vector<wxcal::Game> cal(mine.size());
    std::vector<uint8_t> out(mine.size());
    int at = -1;
    for (size_t i = 0; i < mine.size(); i++)
    {
        const Game& h = *mine[i];
        const int opp = h.home == club ? h.away : h.home;
        int met = 0;
        for (size_t j = 0; j < i; j++)
            if (mine[j]->home == opp || mine[j]->away == opp)
                met++;
        const bool division = club >= 0 && club < (int)s_clubs.size() && opp >= 0 && opp < (int)s_clubs.size() &&
                              s_clubs[club].div == s_clubs[opp].div;
        cal[i].climate = wx_climate(h.home);
        cal[i].derby = division && met >= 1 ? 1 : 0;
        if (&h == &g)
            at = (int)i;
    }
    if (!mine.empty())
        wxcal::season(c.seed ^ ((uint32_t)club * 2654435761u), c.season, cal.data(), (int)mine.size(), out.data());
    int bits = at >= 0 ? out[at] : 0;
    if (nemesis_match(c, g))
        bits |= wxcal::kNight;
    return bits;
}

// Whose calendar a game follows: yours when you play in it (a league's two humans: the home club's, the same on both
// headsets), else the home club's (watching).
static int wx_club(const Career& c, const Game& g)
{
    const bool mine = g.home == c.myClub || g.away == c.myClub;
    return mine && !in_league() ? c.myClub : g.home;
}

int PortFranchiseMatchWeather(void)
{
    if (s_match != MATCH_PLAYING || !s_active || s_matchGame < 0 || s_matchGame >= (int)s_career.games.size())
        return -1;
    const Game& g = s_career.games[s_matchGame];
    const int bits = wx_game(s_career, g, wx_club(s_career, g));
    // the season's whole calendar in the log, once a season (R rain, S snow, N night, - clear)
    static unsigned s_loggedSeed = 0;
    static int s_loggedSeason = -1;
    if (s_loggedSeed != s_career.seed || s_loggedSeason != s_career.season)
    {
        s_loggedSeed = s_career.seed;
        s_loggedSeason = s_career.season;
        std::string line;
        for (int week = 1; week <= PORT_FR_WEEKS; week++)
        {
            const int b = PortFranchiseWeekWeather(week);
            line += b < 0 ? " ?" : (std::string(" ") + ((b & wxcal::kRain) ? "R" : "") + ((b & wxcal::kSnow) ? "S" : "") +
                                    ((b & wxcal::kNight) ? "N" : "") + (b == 0 ? "-" : ""));
        }
        fprintf(stderr, "[franchise] weather calendar, season %d, weeks 1-%d:%s\n", s_career.season, PORT_FR_WEEKS,
                line.c_str());
    }
    fprintf(stderr, "[franchise] weather, season %d week %d (%s at home): %s\n", s_career.season, g.week,
            g.home >= 0 && g.home < (int)s_clubs.size() ? s_clubs[g.home].place.c_str() : "?",
            bits == 0 ? "clear" : PortWeatherLabel(bits));
    fflush(stderr);
    return bits;
}

int PortFranchiseNextWeather(void)
{
    if (!s_active)
        return 0;
    const Game* g = my_game(s_career);
    return g != NULL ? wx_game(s_career, *g, wx_club(s_career, *g)) : 0;
}

int PortFranchiseWeekWeather(int week)
{
    if (!s_active)
        return -1;
    for (size_t i = 0; i < s_career.games.size(); i++)
    {
        const Game& g = s_career.games[i];
        if (g.week == week && (g.home == s_career.myClub || g.away == s_career.myClub))
            return wx_game(s_career, g, wx_club(s_career, g));
    }
    return -1;
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
        const int club = side == 0 ? g.home : g.away;  // r146: your players' card bonuses (card_xp)
        gain(bs ? s.bk : s.cap, card_xp(c, club, bs ? s.backup : s.captain, capXp + teamXp));
        if (!s.backup.empty())
            gain(bs ? s.cap : s.bk, card_xp(c, club, bs ? s.captain : s.backup, (co ? coXp : 0) + teamXp));
        gain(s.side, card_xp(c, club, s.kind, skXp + teamXp));
        // r130: each sidekick member its own goals and assists (m_ID k+1; slot 1 is the co-captain's when there is one)
        for (int k = 0; k < 3; k++)
        {
            const bool onField = !(co && k == 0);
            gain(s.mp[k], card_xp(c, club, s.mem[k].empty() ? s.kind : s.mem[k],
                                  onField ? personal_bonus(c, g, r.goals[side][k + 1], assists[k + 1]) + teamXp : teamXp / 2));
        }
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

bool PortFranchiseLeagueCreate(int club, int captainPick, int backupPick, int sidekickPick, int maxMembers,
                               const int* memberPicks)
{
    if (s_match != MATCH_NONE)
        return false;
    s_lgJoin = LeagueJoin();
    // r131: the league dealt around your picks, your sidekick members too (the people who join take a club as dealt)
    if (!PortFranchiseNewPicked(PORT_FR_LEAGUE_SLOT, club, captainPick, backupPick, sidekickPick, memberPicks))
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

// r137: STRIKERS TRADING CARDS (Lance: packs from franchise matches only): your match played earns 1 pack, a win 2, +1 a
// playoff series won, +1 a trophy (the Super Strike Series). The headset takes them (xr_cards.cpp: rolled and saved there).
static int s_cardPacks = 0;
static unsigned s_cardFlags = 0;
static void card_packs_earned(bool won, bool seriesWon, bool trophy)
{
    const int n = 1 + (won ? 1 : 0) + (seriesWon ? 1 : 0) + (trophy ? 1 : 0);
    s_cardPacks += n;
    s_cardFlags |= (won ? 1u : 0u) | (seriesWon ? 2u : 0u) | (trophy ? 4u : 0u);
    fprintf(stderr, "[franchise] cards: %d pack%s earned\n", n, n == 1 ? "" : "s");
    fflush(stderr);
}
int PortFranchiseTakeCardPacks(unsigned* flags)
{
    const int n = s_cardPacks;
    if (flags != NULL)
        *flags = s_cardFlags;
    s_cardPacks = 0;
    s_cardFlags = 0;
    return n;
}

// r140: TIZM COINS (Lance: "you should earn coins from goals, assists, tackles, etc. and those coins will be used to buy
// packs. the shit shouldnt be expensive seeing as we're STILL getting our 1 free pack per game and 2 per win!!"): a
// franchise match you PLAY pays your side's work in coins, only to buy LANCINI TIZM packs (xr_cards.cpp's CARDS shop: 50
// a pack, 200 a box of 5); a simmed game pays none (as packs). From the game's own match stats (StatsTracker: your five's
// assists, hits, steals, item hits) and this match's tallies (the score, its Super Strikes). Tuned so an average played
// match pays about a pack (~56), a big win about two (tools/outside/coins_sim.py).
static const int kCoinGoal = 6, kCoinSuperStrike = 6, kCoinAssist = 4, kCoinTacklesPer = 2, kCoinSteal = 1, kCoinItem = 1,
                 kCoinWin = 10, kCoinShutout = 10;
static int s_cardCoins = 0;
static std::string s_cardCoinWhy;

// r146: your club's players' card XP bonuses ("Charizard +45%, Toads +10%"; with `whys` each one's reasons too), "" none
static std::string card_bonus_line(const Career& c, bool whys = false)
{
    if (!s_active || !card_bonus_on(c) || c.myClub < 0 || c.myClub >= PORT_FR_CLUBS)
        return std::string();
    const ClubState& s = c.clubs[c.myClub];
    std::string out;
    std::vector<std::string> seen;
    auto add = [&](const std::string& key, const std::string& name) {
        const int pct = card_xp_pct(c, c.myClub, key);
        if (pct <= 0 || std::find(seen.begin(), seen.end(), key) != seen.end())
            return;
        seen.push_back(key);
        out += (out.empty() ? "" : ", ") + name + " +" + std::to_string(pct) + "%";
        const CardBonus* b = card_bonus(key);
        if (whys && b != NULL && !b->why.empty())
            out += " (" + b->why + ")";
    };
    add(s.captain, card_name_of(s.captain));  // (r152: a costume by its own name)
    if (!s.backup.empty())
        add(s.backup, card_name_of(s.backup));
    bool generic = false;
    for (int k = 0; k < 3; k++)
    {
        if (s.mem[k].empty())
            generic = true;
        else
            add(s.mem[k], card_name_of(s.mem[k]));
    }
    if (generic)
        add(s.kind, kind_name(s.kind));
    return out;
}

void PortFranchiseSetCardBonuses(const PortFranchiseCardBonus* list, int count, int coinPct, const char* coinWhy)
{
    s_cardBonus.clear();
    for (int i = 0; list != NULL && i < count; i++)
    {
        CardBonus b;
        b.xpPct = std::max(0, std::min(60, list[i].xpPct));
        b.shinyOdds = list[i].shinyOdds;
        b.why = list[i].why;
        if (b.xpPct > 0 || b.shinyOdds > 0)
            s_cardBonus[list[i].key] = b;
    }
    s_cardCoinPct = std::max(0, std::min(20, coinPct));
    s_cardCoinPctWhy = coinWhy != NULL ? coinWhy : "";
    fprintf(stderr, "[franchise] r146 card bonuses: %d characters, coins +%d%% (%s)\n", (int)s_cardBonus.size(), s_cardCoinPct,
            s_cardCoinPctWhy.c_str());
    fflush(stderr);
}
static void card_coins_earned(int me, int mine, int theirs)
{
    if (me < 0 || me > 1)
        return;
    int assists = 0, hits = 0, steals = 0, items = 0;
    if (StatsTracker* st = nlSingleton<StatsTracker>::s_pInstance)
        for (int k = 0; k < 5; k++)
        {
            const PlayerStats& p = st->mCurrentPlayerStats[me][k];
            assists += p.mNumAssists;
            hits += p.mNumHitsMade;
            steals += p.mNumSteals;
            items += p.mNumPowerupsHit;
        }
    const int ss = s_matchSS[me];
    struct Part
    {
        int n, coins;
        const char *one, *many;
        bool once;  // ("win 10", not "1 win 10")
    };
    const Part parts[] = {{mine, mine * kCoinGoal, "goal", "goals", false},
                          {ss, ss * kCoinSuperStrike, "Super Strike", "Super Strikes", false},
                          {assists, assists * kCoinAssist, "assist", "assists", false},
                          {hits, hits / kCoinTacklesPer, "tackle", "tackles", false},
                          {steals, steals * kCoinSteal, "steal", "steals", false},
                          {items, items * kCoinItem, "item hit", "item hits", false},
                          {mine > theirs ? 1 : 0, mine > theirs ? kCoinWin : 0, "win", "wins", true},
                          {theirs == 0 ? 1 : 0, theirs == 0 ? kCoinShutout : 0, "shutout", "shutouts", true}};
    int total = 0;
    std::string why;
    for (const Part& p : parts)
    {
        if (p.coins <= 0)
            continue;
        total += p.coins;
        char b[64];
        if (p.once)
            snprintf(b, sizeof b, "%s %d", p.one, p.coins);  // "win 10", "shutout 10"
        else
            snprintf(b, sizeof b, "%d %s %d", p.n, p.n == 1 ? p.one : p.many, p.coins);  // "3 goals 18"
        why += (why.empty() ? "" : ", ") + std::string(b);
    }
    // r146: your full binder pages' coin multiplier (a career of this headset's own: card_bonus_on), and your players'
    // card XP bonuses this match, on the breakdown ("..., win 10, binder pages x1.06 +3; card XP: Charizard +45%")
    if (card_bonus_on(s_career) && s_cardCoinPct > 0 && total > 0)
    {
        const int extra = (total * s_cardCoinPct + 50) / 100;
        if (extra > 0)
        {
            char b[80];
            snprintf(b, sizeof b, "binder pages x%d.%02d +%d", 1 + s_cardCoinPct / 100, s_cardCoinPct % 100, extra);
            why += (why.empty() ? "" : ", ") + std::string(b);
            total += extra;
        }
    }
    const std::string xpLine = card_bonus_line(s_career);
    if (!xpLine.empty())
        why += "; card XP: " + xpLine;
    s_cardCoins += total;
    s_cardCoinWhy = why;
    fprintf(stderr, "[franchise] cards: %d TIZM coins (%s) - stats: %d-%d, %d SS, %d assists, %d hits, %d steals, %d item hits\n",
            total, why.c_str(), mine, theirs, ss, assists, hits, steals, items);
    fflush(stderr);
}
int PortFranchiseTakeCardCoins(char* why, int cap)
{
    const int n = s_cardCoins;
    if (why != NULL && cap > 0)
        copy_str(why, cap, s_cardCoinWhy);
    s_cardCoins = 0;
    s_cardCoinWhy.clear();
    return n;
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
        card_packs_earned(won, false, false);  // r137
        card_coins_earned(g.home == c.myClub ? 0 : 1, mine, theirs);  // r140
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
    bool cardSeries = false, cardTrophy = false;  // r137: the cards' bonus packs
    if (g.series >= 0 && g.series < (int)c.series.size())
    {
        // r70b: a playoff game: the series as it stands
        const Series& s = c.series[g.series];
        const int me = s.hi == c.myClub ? s.winsHi : s.winsLo, them = s.hi == c.myClub ? s.winsLo : s.winsHi;
        cardSeries = won && me >= 3;
        cardTrophy = cardSeries && s.round == 4;  // the Super Strike Series
        const std::string state = me >= 3 ? "SERIES WON " : (them >= 3 ? "SERIES LOST " : (me > them ? "you lead " : (me < them ? "you trail " : "series tied ")));
        snprintf(buf, sizeof buf, "%s game %d: %s %s %d - %d%s, %s%d-%d", round_name(s.round, s.conf).c_str(),
                 s.winsHi + s.winsLo, won ? "WIN vs" : "LOSS vs", opp, mine, theirs, overtime ? " (OT)" : "",
                 state.c_str(), std::max(me, them), std::min(me, them));
    }
    else
        snprintf(buf, sizeof buf, "Week %d: %s %s %d - %d%s", c.week, won ? "WIN vs" : "LOSS vs", opp, mine, theirs,
                 overtime ? " (OT)" : "");
    card_packs_earned(won, cardSeries, cardTrophy);  // r137
    {  // r146: the card bonuses this match's XP and coins got, logged
        const std::string bl = card_bonus_line(c, true);
        if (card_bonus_on(c) && (!bl.empty() || s_cardCoinPct > 0))
            fprintf(stderr, "[franchise] r146 card bonuses this match: XP %s; coins +%d%% (%s)\n", bl.empty() ? "none" : bl.c_str(),
                    s_cardCoinPct, s_cardCoinPctWhy.c_str());
    }
    card_coins_earned(g.home == c.myClub ? 0 : 1, mine, theirs);  // r140
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
    if (PortNetOnline())  // r130 (review): online plays the generic sidekicks (members are offline only)
        return NULL;
    const int club = s_matchClub[side];
    if (club < 0 || club >= PORT_FR_CLUBS)
        return NULL;
    // r81 (Lance: "you have the co-captain, named as if its a sidekick, with generated names"): slot 1 is
    // the co-captain when the side has one - a captain, called by its own name; the sidekicks on the field
    // are slots 2-3 (names[1], names[2])
    if (playerId == 1 && PortSecondCaptain(side) >= 0)
        return NULL;
    // r130: a sidekick member (a Pokemon, a hero ...): its own name
    const std::string& mem = s_career.clubs[club].mem[playerId - 1];
    if (!mem.empty())
    {
        static std::string s_memName[2][4];
        s_memName[side][playerId] = name_of(mem);
        return s_memName[side][playerId].c_str();
    }
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
    out->gain = card_xp(c, c.myClub, key, team + personal_bonus(c, g, out->goals, out->assists));  // r146: its card bonus
    out->level = p.level;
    out->xp = p.xp;
    Prog q = p;
    gain(q, out->gain);
    out->newLevel = q.level;
    out->newXp = q.xp;
    copy_str(out->name, sizeof out->name, name_of(key));
    std::string to;
    out->evoLevel = next_evolution(top_of(s, backupStarts ? 1 : 0), &to);  // r167: from its highest form reached
    out->pokemon = out->evoLevel > 0 || key.compare(0, 3, "pk_") == 0;
    out->costume = PortCostumeKey(key.c_str());  // r111; r183
    if (out->evoLevel > 0)
    {
        copy_str(out->evoName, sizeof out->evoName, name_of(to));
        out->evolves = out->newLevel >= out->evoLevel && p.declinedAt != out->newLevel;
    }
    copy_str(out->kind, sizeof out->kind, kind_name(s.kind));
    out->skLevel = s.side.level;
    out->skXp = s.side.xp;
    out->skGain = card_xp(c, c.myClub, s.kind,
                          team + personal_bonus(c, g, s_matchGoals[me][1] + s_matchGoals[me][2] + s_matchGoals[me][3],
                                                (PortSecondCaptain(me) >= 0 ? 0 : assists[1]) + assists[2] + assists[3]));
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
        out->coGain = card_xp(c, c.myClub, coKey, team + personal_bonus(c, g, out->coGoals, out->coAssists));  // r146
        out->coLevel = cp.level;
        out->coXp = cp.xp;
        Prog cq = cp;
        gain(cq, out->coGain);
        out->coNewLevel = cq.level;
        out->coNewXp = cq.xp;
        copy_str(out->coName, sizeof out->coName, name_of(coKey));
        std::string coTo;
        out->coEvoLevel = next_evolution(top_of(s, backupStarts ? 0 : 1), &coTo);  // r167
        out->coPokemon = out->coEvoLevel > 0 || coKey.compare(0, 3, "pk_") == 0;
        out->coCostume = PortCostumeKey(coKey.c_str());  // r111; r183
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

// ------------------------------------------------------------------- r173: level-ups through a match (franchise.h)
extern "C" int PortBroadcastNdc(float x, float y, float z, float* ndcX, float* ndcY);  // glPlat.cpp
namespace
{
int s_lvGame = -2;            // the match (game index) the levels below are of
int s_lvMax[2][4];            // each side's m_ID 0-3: the highest level shown this match (-1 not looked at yet)
long long s_lvKey = -1;       // the match's numbers last worked out from
std::vector<PortFranchiseLevelUp> s_lvOut;

// a field player (side, m_ID) of a franchise match: its key, its progress, its goals (s_matchGoals' slot) and assists slot
bool lv_player(const Career& c, int side, int id, std::string* key, const Prog** p, int* goalSlot)
{
    const int club = s_matchClub[side];
    if (club < 0 || club >= PORT_FR_CLUBS)
        return false;
    const ClubState& s = c.clubs[club];
    const bool backupStarts = s.startsBackup && !s.backup.empty();
    const bool co = !s.backup.empty() && PortSecondCaptain(side) >= 0;
    if (id == 0)
    {
        *key = backupStarts ? s.backup : s.captain;
        *p = backupStarts ? &s.bk : &s.cap;
        *goalSlot = 0;
        return true;
    }
    if (id == 1 && co)
    {
        *key = backupStarts ? s.captain : s.backup;
        *p = backupStarts ? &s.cap : &s.bk;
        *goalSlot = 4;
        return true;
    }
    if (id < 1 || id > 3)
        return false;
    const int k = id - 1;
    *key = s.mem[k].empty() ? s.names[k] : s.mem[k];
    *p = s.mem[k].empty() ? &s.side : &s.mp[k];
    *goalSlot = id;
    return !key->empty();
}

void lv_frame()
{
    if (s_match != MATCH_PLAYING || !s_active || s_matchGame < 0 || s_matchGame >= (int)s_career.games.size() || s_watchGame >= 0)
    {
        s_lvGame = -2;
        return;
    }
    const Career& c = s_career;
    const Game& g = c.games[s_matchGame];
    if (s_lvGame != s_matchGame)
    {
        s_lvGame = s_matchGame;
        s_lvKey = -1;
        for (int sd = 0; sd < 2; sd++)
            for (int k = 0; k < 4; k++)
                s_lvMax[sd][k] = -1;
    }
    int assists[2][4];
    match_assists(0, assists[0]);
    match_assists(1, assists[1]);
    long long key = 17;
    for (int sd = 0; sd < 2; sd++)
        for (int k = 0; k < kScorerSlots; k++)
            key = key * 131 + s_matchGoals[sd][k] * 17 + (k < 4 ? assists[sd][k] : 0);
    if (key == s_lvKey)
        return;  // (worked out again only when the match's numbers move)
    s_lvKey = key;
    int goals[2] = {0, 0};
    for (int sd = 0; sd < 2; sd++)
        for (int k = 0; k < kScorerSlots; k++)
            goals[sd] += s_matchGoals[sd][k];
    for (int sd = 0; sd < 2; sd++)
    {
        const int club = s_matchClub[sd];
        if (club < 0 || club >= PORT_FR_CLUBS)
            continue;
        // the club's part as award_xp gives it if the match ended now (the r77 live meter's numbers)
        const bool won = goals[sd] > goals[1 - sd];
        int team = (won ? 70 : 40) + 8 * goals[sd];
        if (g.series >= 0)
            team += team / 2;
        if (nemesis_match(c, g))
            team += team / 4;
        if (!is_human(c, club))
            team += team * 15 * diff_k(c) / 100;
        team += played_goal_bonus(c, g, goals[sd]);
        for (int id = 0; id < 4; id++)
        {
            std::string pk;
            const Prog* p = NULL;
            int slot = 0;
            if (!lv_player(c, sd, id, &pk, &p, &slot))
                continue;
            const int ast = assists[sd][id];
            const int xp = card_xp(c, club, pk, team + personal_bonus(c, g, s_matchGoals[sd][slot], ast));
            Prog q = *p;
            gain(q, xp);
            if (s_lvMax[sd][id] < 0)
            {
                s_lvMax[sd][id] = q.level;  // (where it stands at the first look: no moment for that)
                continue;
            }
            if (q.level > s_lvMax[sd][id])
            {
                s_lvMax[sd][id] = q.level;
                PortFranchiseLevelUp e;
                memset(&e, 0, sizeof e);
                e.side = sd;
                e.playerId = id;
                e.level = q.level;
                copy_str(e.name, sizeof e.name, name_of(pk));  // (a generic sidekick's key is its name)
                if (s_lvOut.size() < 16)
                    s_lvOut.push_back(e);
                fprintf(stderr, "[franchise] level up in the match: %s (side %d, player %d) -> LV %d\n", e.name, sd, id, e.level);
                fflush(stderr);
            }
        }
    }
}
}  // namespace

int PortFranchiseTakeLevelUps(PortFranchiseLevelUp* out, int max)
{
    lv_frame();
    int n = 0;
    for (size_t i = 0; i < s_lvOut.size() && n < max; i++)
        out[n++] = s_lvOut[i];
    s_lvOut.clear();
    return n;
}

bool PortFranchisePlayerWhere(int side, int playerId, float pos[3], float ndc[2], int* ndcValid)
{
    *ndcValid = 0;
    if (s_match != MATCH_PLAYING || side < 0 || side > 1 || g_pTeams[side] == NULL)
        return false;
    for (int i = 0; i < 5; i++)
    {
        cPlayer* p = g_pTeams[side]->GetPlayer(i);
        if (p == NULL || p->m_ID != playerId)
            continue;
        const nlVector3& v = p->GetPosition();
        pos[0] = v.x;
        pos[1] = v.y;
        pos[2] = v.z;
        *ndcValid = PortBroadcastNdc(pos[0], pos[1], pos[2] + 1.0f, &ndc[0], &ndc[1]);
        return true;
    }
    return false;
}

int PortFranchiseEvoWatch(PortFranchiseEvoRow* out, int max)
{
    if (!s_active || out == NULL || max <= 0)
        return 0;
    const Career& c = s_career;
    std::vector<PortFranchiseEvoRow> rows;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        ClubState& s = const_cast<ClubState&>(c.clubs[i]);
        for (int b = 0; b < 5; b++)  // r130: and the sidekick members (slots 2-4)
        {
            const std::string& key = slot_key(s, b);
            if (key.compare(0, 3, "pk_") != 0 && key.compare(0, 4, "mua_") != 0 && !PortOriginalKey(key.c_str()))  // r183: originals
                continue;  // Pokemon only (r110: and the Marvel heroes, whose next costume is their "evolution")
            const Prog& p = slot_prog(s, b);
            PortFranchiseEvoRow r;
            memset(&r, 0, sizeof r);
            copy_str(r.name, sizeof r.name, name_of(key));
            copy_str(r.key, sizeof r.key, key);
            r.club = i;
            r.mine = i == c.myClub;
            r.backup = b == 1;
            r.shiny = p.shiny;
            r.level = p.level;
            r.xp = p.xp;
            r.seasonGain = std::max(0, p.level - p.startLevel);
            std::string to;
            const std::string topKey = top_of(s, b);  // r167: its next form from its highest one reached
            r.evoLevel = next_evolution(topKey, &to);
            if (r.evoLevel > 0)
                copy_str(r.evoName, sizeof r.evoName, name_of(to));
            std::string now[5];
            r.ready = r.mine && evolutions_for(topKey, p, now) > 0;  // r95
            r.costume = PortCostumeKey(key.c_str());            // r111; r183
            rows.push_back(r);
        }
    }
    // r111: yours first (the page shows 33; with the Marvel heroes a league can deal more than that)
    std::sort(rows.begin(), rows.end(), [](const PortFranchiseEvoRow& a, const PortFranchiseEvoRow& b) {
        if (a.mine != b.mine)
            return a.mine;
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

// r173 (Lance: "when it comes to sidekicks, i should be able to trade them individually, not as a fuckin bunch deal"): a
// sidekick is a trade piece of its own - one of your named sidekick members (what 2-4: its slot) for one of another club's
// named members, never the trio as a bundle (r77-r172 swapped all three). A generic sidekick (no character) is no piece.
// Captains / backups as before. Each player's value: its rating + a point a level over 8 (as its highest form).
int PortFranchiseTradeOptions(int what, PortFranchiseTradeOffer* out, int max, int* myValue, char* myName, int nameSize)
{
    *myValue = 0;
    if (myName != NULL && nameSize > 0)
        myName[0] = '\0';
    if (!s_active || what < 0 || what > 4 || out == NULL || max <= 0)
        return 0;
    Career& c = s_career;
    ClubState& me = c.clubs[c.myClub];
    if (what == 1 && me.backup.empty())
        return 0;
    const bool sidekick = what >= 2;
    const int myK = what - 2;
    if (sidekick)
    {
        if (me.mem[myK].empty())
        {
            if (myName != NULL)  // (a generic sidekick: shown, not tradeable)
                copy_str(myName, (size_t)nameSize, sk_name(me, myK));
            return 0;
        }
        *myValue = player_value(top_of(me, 2 + myK), me.mp[myK]);
        if (myName != NULL)
            copy_str(myName, (size_t)nameSize, name_of(top_of(me, 2 + myK)) + " LV " + std::to_string(me.mp[myK].level));
    }
    else
    {
        const Slot mine = slot_of(me, what == 1);
        *myValue = player_value(top_of(me, what == 1 ? 1 : 0), *mine.prog);  // r167: as its highest form
        if (myName != NULL)
            copy_str(myName, (size_t)nameSize, name_of(*mine.key) + " LV " + std::to_string(mine.prog->level));
    }
    std::vector<PortFranchiseTradeOffer> offers;
    for (int i = 0; i < PORT_FR_CLUBS; i++)
    {
        if (i == c.myClub)
            continue;
        ClubState& s = c.clubs[i];
        for (int b = 0; b < (sidekick ? 3 : 2); b++)
        {
            PortFranchiseTradeOffer o;
            memset(&o, 0, sizeof o);
            o.club = i;
            o.slot = -1;
            if (sidekick)
            {
                if (s.mem[b].empty())
                    continue;
                o.slot = b;
                copy_str(o.name, sizeof o.name, name_of(s.mem[b]));
                copy_str(o.key, sizeof o.key, s.mem[b]);
                o.level = s.mp[b].level;
                o.shiny = s.mp[b].shiny;
                o.value = player_value(s.mem[b], s.mp[b]);
            }
            else
            {
                o.backup = b != 0;
                const Slot their = slot_of(s, b != 0);
                if (their.key->empty())
                    continue;
                copy_str(o.name, sizeof o.name, name_of(*their.key));
                copy_str(o.key, sizeof o.key, *their.key);  // r116
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

static bool trade_impl(int what, int club, bool backup, int theirSlot);

bool PortFranchiseTrade(int what, int club, bool backup, int theirSlot)
{
    // r100: an online league's: never with a person's club; an action (again on a newer copy, if it still stands)
    if (in_league() && is_human(s_career, club))
        return false;
    static bool s_done;
    s_done = false;
    league_do("trade", [what, club, backup, theirSlot]() {
        s_done = trade_impl(what, club, backup, theirSlot);
        return s_done;
    });
    return s_done;
}

static bool trade_impl(int what, int club, bool backup, int theirSlot)
{
    if (!PortFranchiseTradeOpen() || what < 0 || what > 4 || club < 0 || club >= PORT_FR_CLUBS)
        return false;
    Career& c = s_career;
    if (club == c.myClub)
        return false;
    ClubState& me = c.clubs[c.myClub];
    ClubState& them = c.clubs[club];
    std::string gave, got;
    int myValue = 0, theirValue = 0;
    PStats* theirStats = NULL;
    if (what >= 2)
    {
        // r173: one sidekick for one sidekick (both named characters)
        const int k = what - 2;
        if (theirSlot < 0 || theirSlot > 2 || me.mem[k].empty() || them.mem[theirSlot].empty())
            return false;
        myValue = player_value(top_of(me, 2 + k), me.mp[k]);
        theirValue = player_value(them.mem[theirSlot], them.mp[theirSlot]);
        if (myValue * 10 < theirValue * 9)
            return false;
        settle_form(c, c.myClub, 2 + k);  // r167: it leaves as its highest form
        gave = name_of(me.mem[k]);
        got = name_of(them.mem[theirSlot]);
        swap_member_slots(c, c.myClub, k, club, theirSlot);
        theirStats = &them.sk[theirSlot];
    }
    else
    {
        if (what == 1 && me.backup.empty())
            return false;
        Slot mine = slot_of(me, what == 1), their = slot_of(them, backup);
        if (their.key->empty())
            return false;
        myValue = player_value(top_of(me, what == 1 ? 1 : 0), *mine.prog);  // r167: as its highest form
        theirValue = player_value(*their.key, *their.prog);
        if (myValue * 10 < theirValue * 9)
            return false;
        settle_form(c, c.myClub, what == 1 ? 1 : 0);  // r167: it leaves as its highest form
        gave = name_of(*mine.key);
        got = name_of(*their.key);
        std::swap(*mine.key, *their.key);
        std::swap(*mine.prog, *their.prog);
        std::swap(*mine.st, *their.st);
        // a nemesis that moved plays for its new club now
        each_nemesis(c, *their.key, [&](Nemesis* n) { n->club = club; });  // r100: everyone's records
        each_nemesis(c, *mine.key, [&](Nemesis* n) { n->club = c.myClub; });
        theirStats = backup ? &them.bkSt : &them.capSt;
    }
    const std::string place = s_clubs[club].place;
    // r177: who you got never retires this season
    c.joined.push_back(std::make_pair(what >= 2 ? char_id(me.mem[what - 2]) : char_id(what == 1 ? me.backup : me.captain),
                                      c.phase == PHASE_DONE ? c.season + 1 : c.season));
    c.headline = "Trade: you sent " + gave + " to " + place + " for " + got + ".";
    c.news.push_back("TRADE: " + upper(s_clubs[c.myClub].place) + " SEND " + upper(gave) + " TO " + upper(place) + " FOR " +
                     upper(got));
    if (theirStats != NULL)
        post(c, theirStats->personality, EV_TRADED, gave, place);
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
        for (int s = 0; s < 2; s++)  // r130: and shiny sidekick members (review: carried into the next cup match)
            for (int k = 1; k <= 3; k++)
                PortGuestSetSlotShiny(s, k, false);
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
        copy_str(out->nemesisTitle, sizeof out->nemesisTitle, title_name(n->title));
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

// ======================================================================================= r167: FREE AGENCY (franchise.h)
namespace
{
// the spots an offer touches (bits: 0 captain, 1 backup, 2-4 the sidekick slots): a captain's offer that benches the
// captain touches the backup's spot too
int fa_spots(int role, bool relCap)
{
    if (role == PORT_FA_CAPTAIN)
        return relCap ? 1 : 3;
    return 1 << std::max(1, std::min(4, role));
}

void fa_copy_agent(const Career& c, const FreeAgent& a, PortFranchiseFreeAgent& r, double avgStr, double avgGf)
{
    memset(&r, 0, sizeof r);
    copy_str(r.key, sizeof r.key, a.key);
    copy_str(r.name, sizeof r.name, name_of(a.key));
    r.rating = rating(a.key);
    r.level = a.prog.level;
    r.value = fa_value(a.key, a.prog);
    r.shiny = a.prog.shiny;
    r.captainType = fa_captain_type(a.key);
    copy_str(r.game, sizeof r.game, fa_game(a.key));
    const std::vector<std::string> back = form_chain(a.key);
    r.stage = std::max(1, (int)back.size());
    r.stages = r.stage;
    std::string k = a.key;
    for (int guard = 0; guard < 6; guard++)  // the forms still to come (the first branch of a split)
    {
        std::string next;
        for (size_t i = 0; i < s_evos.size() && next.empty(); i++)
            if (s_evos[i].from == k && has_captain(s_evos[i].to))
                next = s_evos[i].to;
        if (next.empty())
            break;
        r.stages++;
        k = next;
    }
    r.personality = a.st.personality < 0 || a.st.personality >= PERS_COUNT ? 0 : a.st.personality + 1;
    copy_str(r.persName, sizeof r.persName, kPersName[r.personality]);
    int homeClub = -1;
    const std::string div = fa_home(a.key, &homeClub);
    r.homeClub = homeClub;
    copy_str(r.home, sizeof r.home, homeClub >= 0 ? upper(s_clubs[homeClub].place) : upper(div));
    r.formerClub = a.fresh ? -1 : a.from;
    r.leader = -1;
    r.myRole = -1;
    r.refusesMe = fa_refuses(c, a, c.myClub);
    r.decideWeek = a.decide;
    r.rookie = a.fresh;  // r173
    double best = -1e9;
    std::vector<int> clubs;
    for (size_t i = 0; i < c.bids.size(); i++)
    {
        const FaBid& b = c.bids[i];
        if (b.key != a.key)
            continue;
        if (std::find(clubs.begin(), clubs.end(), b.club) == clubs.end())
            clubs.push_back(b.club);
        if (b.club == c.myClub)
        {
            r.myRole = b.role;
            r.myPromise = b.promise;
        }
        if (!fa_role_ok(a, b.role))
            continue;
        const FaEval e = fa_eval(c, a, b, avgStr, avgGf);
        if (!e.refuse && e.score > best)
        {
            best = e.score;
            r.leader = b.club;
            r.leaderRole = b.role;
            r.leaderPromise = b.promise;
            copy_str(r.leaderWhy, sizeof r.leaderWhy, best >= kFaAccept ? e.why : std::string("NOT ENOUGH YET"));
        }
    }
    r.interested = (int)clubs.size();
    r.myLead = r.myRole >= 0 && r.leader == c.myClub && best >= kFaAccept;
}

bool fa_offer_impl(const std::string& key, int role, bool promise, bool relCap)
{
    Career& c = s_career;
    if (!fa_window(c) || role < 0 || role >= PORT_FA_ROLES)
        return false;
    fa_refresh(c);
    const int ai = fa_find(c, key);
    if (ai < 0)
        return false;
    FreeAgent& a = c.fa[(size_t)ai];
    if (!fa_role_ok(a, role) || fa_refuses(c, a, c.myClub))
        return false;
    if (role != PORT_FA_BACKUP)
        promise = false;
    if (role != PORT_FA_CAPTAIN)
        relCap = false;
    // one offer a spot: yours to this agent, and yours to another for a spot this one touches, are replaced
    const int spots = fa_spots(role, relCap);
    for (int i = (int)c.bids.size() - 1; i >= 0; i--)
    {
        const FaBid& b = c.bids[(size_t)i];
        if (b.club == c.myClub && (b.key == key || (fa_spots(b.role, b.relCap) & spots) != 0))
            c.bids.erase(c.bids.begin() + i);
    }
    FaBid b;
    b.key = key;
    b.club = c.myClub;
    b.role = role;
    b.promise = promise;
    b.relCap = relCap;
    b.week = c.phase == PHASE_REGULAR ? c.week : 0;
    c.bids.push_back(b);
    FreeAgent& a2 = c.fa[(size_t)fa_find(c, key)];
    if (c.phase == PHASE_REGULAR && a2.decide == 0)
        a2.decide = std::min(kFaDeadline, c.week + 1);
    c.faLast = "OFFER OUT: " + upper(name_of(key)) + " AS YOUR " + fa_role_word(role) + (promise ? " (PROMISED STARTS)" : "");
    save();
    fprintf(stderr, "[franchise] free agency: your offer to %s (%s%s%s)\n", key.c_str(), fa_role_word(role),
            promise ? ", promised starts" : "", relCap ? ", captain released" : "");
    fflush(stderr);
    return true;
}

bool fa_withdraw_impl(const std::string& key)
{
    Career& c = s_career;
    bool any = false;
    for (int i = (int)c.bids.size() - 1; i >= 0; i--)
        if (c.bids[(size_t)i].club == c.myClub && c.bids[(size_t)i].key == key)
        {
            c.bids.erase(c.bids.begin() + i);
            any = true;
        }
    if (!any)
        return false;
    bool others = false;
    for (size_t i = 0; i < c.bids.size() && !others; i++)
        others = c.bids[i].key == key;
    const int ai = fa_find(c, key);
    if (ai >= 0 && !others)
        c.fa[(size_t)ai].decide = 0;
    c.faLast = "OFFER WITHDRAWN: " + upper(name_of(key));
    save();
    return true;
}

// r167: the forms tiles of one of your slots (reached ones, then the next forms still locked)
bool forms_of(const Career& c, int slot, PortFranchiseForms& f)
{
    const ClubState& s = c.clubs[c.myClub];
    const std::string& key = slot_key_c(s, slot);
    if (key.empty())
        return false;
    memset(&f, 0, sizeof f);
    f.slot = slot;
    static const char* const kRole[5] = {"CAPTAIN", "BACKUP", "SIDEKICK 1", "SIDEKICK 2", "SIDEKICK 3"};
    copy_str(f.role, sizeof f.role, kRole[slot]);
    const Prog& p = slot_prog_c(s, slot);
    f.level = p.level;
    f.shiny = p.shiny;
    f.costume = PortCostumeKey(key.c_str());  // r183
    const std::string top = top_of(s, slot);
    std::vector<std::string> keys;
    std::vector<int> at;
    const std::vector<std::string> chain = form_chain(top);
    for (size_t i = 0; i < chain.size(); i++)
        if (has_captain(chain[i]) || chain[i] == key)
        {
            keys.push_back(chain[i]);
            at.push_back(0);
        }
    std::string k = top;
    for (int guard = 0; guard < 6 && keys.size() < 6; guard++)
    {
        std::vector<std::pair<std::string, int> > opts;
        for (size_t i = 0; i < s_evos.size(); i++)
            if (s_evos[i].from == k && has_captain(s_evos[i].to))
                opts.push_back(std::make_pair(s_evos[i].to, s_evos[i].level));
        for (size_t i = 0; i < opts.size() && keys.size() < 6; i++)
        {
            keys.push_back(opts[i].first);
            at.push_back(std::max(1, opts[i].second));
        }
        if (opts.size() != 1)
            break;  // (a split: its options shown, no further)
        k = opts[0].first;
    }
    if (keys.size() < 2)
        return false;
    f.count = (int)keys.size();
    for (int i = 0; i < f.count; i++)
    {
        copy_str(f.key[i], sizeof f.key[i], keys[(size_t)i]);
        copy_str(f.name[i], sizeof f.name[i], name_of(keys[(size_t)i]));
        f.reached[i] = at[(size_t)i] == 0;
        f.at[i] = at[(size_t)i];
        if (keys[(size_t)i] == key)
            f.current = i;
    }
    std::string now[5];
    f.ready = evolutions_for(top, p, now) > 0;
    return true;
}

bool set_form_impl(int slot, const std::string& key)
{
    Career& c = s_career;
    if (slot < 0 || slot > 4)
        return false;
    ClubState& s = c.clubs[c.myClub];
    std::string& cur = slot_key(s, slot);
    if (cur.empty() || key == cur || !has_captain(key))
        return false;
    const std::string top = top_of(s, slot);
    const std::vector<std::string> chain = form_chain(top);
    if (std::find(chain.begin(), chain.end(), key) == chain.end())
        return false;
    const std::string was = cur;
    cur = key;
    s.top[slot] = key == top ? std::string() : top;
    each_nemesis_rekey(c, c.myClub, slot, was, key);
    c.headline = key == top ? name_of(key) + " is back in its top form." : name_of(top) + " plays as " + name_of(key) + " now.";
    save();
    fprintf(stderr, "[franchise] form: slot %d plays as %s (highest %s)\n", slot, key.c_str(), top.c_str());
    fflush(stderr);
    return true;
}
}  // namespace

bool PortFranchiseFaWindowGet(PortFranchiseFaWindow* out)
{
    memset(out, 0, sizeof *out);
    if (!s_active)
        return false;
    Career& c = s_career;
    fa_refresh(c);
    out->active = true;
    out->offSeason = c.phase == PHASE_DONE;
    out->open = fa_window(c) && s_match == MATCH_NONE && !coop_blocked();
    out->week = c.week;
    out->agents = (int)c.fa.size();
    for (size_t i = 0; i < c.bids.size(); i++)
        out->myOffers += c.bids[i].club == c.myClub ? 1 : 0;
    std::string st;
    if (coop_blocked())
        st = "YOUR FRIEND RUNS THIS CAREER: THEY MAKE THE OFFERS";
    else if (c.phase == PHASE_DONE)
        st = "OFF-SEASON: THEY ALL DECIDE WHEN SEASON " + std::to_string(c.season + 1) + " STARTS";
    else if (c.phase == PHASE_REGULAR && c.week <= kFaDeadline)
        st = "OPEN TO WEEK " + std::to_string(kFaDeadline) + ": EACH DECIDES 2 MATCH DAYS AFTER ITS FIRST OFFER";
    else
        st = "SHUT: IT OPENS WHEN THE CHAMPION IS CROWNED";
    copy_str(out->status, sizeof out->status, st);
    copy_str(out->last, sizeof out->last, c.faLast);
    return true;
}

int PortFranchiseFreeAgents(PortFranchiseFreeAgent* out, int max, int filter, int sort)
{
    if (!s_active || out == NULL || max <= 0)
        return 0;
    Career& c = s_career;
    fa_refresh(c);
    const double avgStr = fa_avg_strength(c), avgGf = fa_avg_goals(c);
    std::vector<PortFranchiseFreeAgent> rows;
    for (size_t i = 0; i < c.fa.size(); i++)
    {
        PortFranchiseFreeAgent r;
        fa_copy_agent(c, c.fa[i], r, avgStr, avgGf);
        if ((filter == 1 && !r.captainType) || (filter == 2 && r.captainType) || (filter == 3 && r.myRole < 0) ||
            (filter == 4 && !r.rookie))
            continue;
        rows.push_back(r);
    }
    std::stable_sort(rows.begin(), rows.end(), [sort](const PortFranchiseFreeAgent& a, const PortFranchiseFreeAgent& b) {
        if (sort == 1 && a.interested != b.interested)
            return a.interested > b.interested;
        if (sort == 2)
            return strcmp(a.name, b.name) < 0;
        if (a.rookie && b.rookie && a.rating != b.rating)  // r173: rookies (all Lv 8) by their talent
            return a.rating > b.rating;
        if (a.value != b.value)
            return a.value > b.value;
        return strcmp(a.name, b.name) < 0;
    });
    const int n = std::min(max, (int)rows.size());
    for (int i = 0; i < n; i++)
        out[i] = rows[(size_t)i];
    return n;
}

int PortFranchiseFaRoles(const char* key, PortFranchiseFaRole* out, int max)
{
    if (!s_active || key == NULL || out == NULL || max <= 0)
        return 0;
    Career& c = s_career;
    const int ai = fa_find(c, key);
    if (ai < 0)
        return 0;
    const FreeAgent& a = c.fa[(size_t)ai];
    const ClubState& me = c.clubs[c.myClub];
    const double avgStr = fa_avg_strength(c), avgGf = fa_avg_goals(c);
    const bool open = fa_window(c) && s_match == MATCH_NONE && !coop_blocked();
    const bool refuses = fa_refuses(c, a, c.myClub);
    // the best offer of the other clubs now
    double other = -1e9;
    int otherClub = -1, otherRole = 0;
    for (size_t i = 0; i < c.bids.size(); i++)
    {
        const FaBid& b = c.bids[i];
        if (b.key != a.key || b.club == c.myClub || !fa_role_ok(a, b.role))
            continue;
        const FaEval e = fa_eval(c, a, b, avgStr, avgGf);
        if (!e.refuse && e.score > other)
        {
            other = e.score;
            otherClub = b.club;
            otherRole = b.role;
        }
    }
    const FaBid* mineNow = NULL;
    for (size_t i = 0; i < c.bids.size(); i++)
        if (c.bids[i].key == a.key && c.bids[i].club == c.myClub)
            mineNow = &c.bids[i];
    const std::string capName = upper(name_of(top_of(me, 0))), bkName = me.backup.empty() ? std::string() : upper(name_of(top_of(me, 1)));
    struct Row
    {
        int role;
        bool promise, relCap;
        std::string label, effect, release, releaseKey;
    };
    std::vector<Row> rows;
    rows.push_back({PORT_FA_CAPTAIN, false, false, "CAPTAIN",
                    bkName.empty() ? capName + " TO THE BENCH (YOUR BACKUP)" : capName + " TO THE BENCH, " + bkName + " RELEASED",
                    bkName, me.backup.empty() ? std::string() : top_of(me, 1)});
    rows.push_back({PORT_FA_CAPTAIN, false, true, "CAPTAIN (RELEASE YOURS)", capName + " RELEASED", capName, top_of(me, 0)});
    rows.push_back({PORT_FA_BACKUP, false, false, "BACKUP",
                    bkName.empty() ? std::string("FILLS YOUR EMPTY BACKUP SPOT") : bkName + " RELEASED", bkName,
                    me.backup.empty() ? std::string() : top_of(me, 1)});
    rows.push_back({PORT_FA_BACKUP, true, false, "BACKUP + PROMISED STARTS",
                    "STARTS OVER " + capName + (bkName.empty() ? std::string() : ", " + bkName + " RELEASED"), bkName,
                    me.backup.empty() ? std::string() : top_of(me, 1)});
    for (int k = 0; k < 3; k++)
    {
        const bool bench = k == 0 && !me.backup.empty();
        const std::string who = me.mem[k].empty() ? std::string() : upper(name_of(top_of(me, 2 + k)));
        std::string eff = who.empty() ? "REPLACES " + upper(me.names[k].empty() ? kind_name(me.kind) : me.names[k]) + " (NOBODY RELEASED)"
                                      : who + " RELEASED";
        if (bench)
            eff += ", SITS WHEN YOUR CO-CAPTAIN PLAYS";
        rows.push_back({PORT_FA_SIDEKICK1 + k, false, false, "SIDEKICK " + std::to_string(k + 1), eff, who,
                        me.mem[k].empty() ? std::string() : top_of(me, 2 + k)});
    }
    int n = 0;
    for (size_t i = 0; i < rows.size() && n < max; i++)
    {
        const Row& rw = rows[i];
        PortFranchiseFaRole& o = out[n++];
        memset(&o, 0, sizeof o);
        o.role = rw.role;
        o.promise = rw.promise;
        o.releaseCaptain = rw.relCap;
        o.allowed = open && !refuses && fa_role_ok(a, rw.role);
        copy_str(o.label, sizeof o.label, rw.label);
        copy_str(o.effect, sizeof o.effect, rw.effect);
        copy_str(o.release, sizeof o.release, rw.release);
        copy_str(o.releaseKey, sizeof o.releaseKey, rw.releaseKey);
        FaBid b;
        b.key = a.key;
        b.club = c.myClub;
        b.role = rw.role;
        b.promise = rw.promise;
        b.relCap = rw.relCap;
        const FaEval e = fa_eval(c, a, b, avgStr, avgGf);
        std::string vs;
        if (!fa_role_ok(a, rw.role))
            vs = "A SIDEKICK ONLY";
        else if (e.refuse)
            vs = "REFUSES YOU: BEEF";
        else if (e.score < kFaAccept)
            vs = "NOT ENOUGH YET";
        else if (otherClub < 0)
        {
            vs = "NO OTHER OFFERS";
            o.wouldLead = true;
        }
        else if (e.score > other)
        {
            vs = "WOULD LEAD " + upper(s_clubs[otherClub].place);
            o.wouldLead = true;
        }
        else
            vs = "BEHIND " + upper(s_clubs[otherClub].place) + " (" + fa_role_word(otherRole) + ")";
        copy_str(o.versus, sizeof o.versus, vs);
        o.current = mineNow != NULL && mineNow->role == rw.role && mineNow->promise == rw.promise && mineNow->relCap == rw.relCap;
        const int spots = fa_spots(rw.role, rw.relCap);
        for (size_t k = 0; k < c.bids.size(); k++)
            if (c.bids[k].club == c.myClub && c.bids[k].key != a.key && (fa_spots(c.bids[k].role, c.bids[k].relCap) & spots) != 0)
            {
                copy_str(o.replaces, sizeof o.replaces, "REPLACES YOUR OFFER TO " + upper(name_of(c.bids[k].key)));
                break;
            }
    }
    return n;
}

bool PortFranchiseFaOffer(const char* key, int role, bool promise, bool releaseCaptain)
{
    if (!s_active || s_match != MATCH_NONE || coop_blocked() || key == NULL)
        return false;
    const std::string k = key;
    static bool s_done;
    s_done = false;
    league_do("fa offer", [k, role, promise, releaseCaptain]() {  // (an online league's: an action, as a trade)
        s_done = fa_offer_impl(k, role, promise, releaseCaptain);
        return s_done;
    });
    return s_done;
}

bool PortFranchiseFaWithdraw(const char* key)
{
    if (!s_active || s_match != MATCH_NONE || coop_blocked() || key == NULL)
        return false;
    const std::string k = key;
    static bool s_done;
    s_done = false;
    league_do("fa withdraw", [k]() {
        s_done = fa_withdraw_impl(k);
        return s_done;
    });
    return s_done;
}

int PortFranchiseFormsList(PortFranchiseForms* out, int max)
{
    if (!s_active || out == NULL || max <= 0)
        return 0;
    int n = 0;
    for (int slot = 0; slot < 5 && n < max; slot++)
        if (forms_of(s_career, slot, out[n]))
            n++;
    return n;
}

bool PortFranchiseSetForm(int slot, const char* key)
{
    if (!s_active || s_match != MATCH_NONE || coop_blocked() || key == NULL)
        return false;
    const std::string k = key;
    static bool s_done;
    s_done = false;
    league_do("form", [slot, k]() {
        s_done = set_form_impl(slot, k);
        return s_done;
    });
    return s_done;
}