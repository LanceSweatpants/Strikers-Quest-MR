// strikers-quest r213: COLLECTOR TRADES (xr_trade.h; C:\StrikersMR\COLLECTOR-TRADES.md).
#include "xr_trade.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <unordered_map>

#include "../../include/port/franchise.h"
#include "xr_coins.h"
#include "xr_menu.h"
#include "xr_exq.h"  // r230: LANCINI EXQUISITE (tiers, runs)

namespace xrtrade {

namespace {
using xrmarket::Holding;
using xrmarket::Variant;
enum { kBase = 0, kSilver, kGold, kBlack };
enum { kHitNone = 0, kHitBoom, kHitHome, kHitSigned };
enum { kSerStrikers = 0, kSerCharged, kSerMelee, kSerPokemon, kSerMarvel, kSerOriginals };
enum { kGrailHunter = 0, kLowballer, kPokeOnly, kMarvelFan, kSetBuilder, kFlipper, kNostalgia, kGradeSnob, kHypeChaser,
       // r235
       kSetChaser, kJamFan, kExqFan, kMeleeFan, kLoyalist, kStatsNerd, kBargain, kGrader, kRookieSpec, kSerialHunter, kWhale };
enum { kLAccept = 0, kLCounter, kLLowball, kLDecline, kLNotInto, kLPitch, kLIdle };

// ============================================================================================== the nine (COLLECTOR-TRADES.md 2)
struct Persona {
  const char* name;
  const char* handle;
  const char* kind;
  const char* bio;
  uint32_t c1, c2;
  int emblem;
  long long wallet0;
  int patience;
  double walk, need, moodSens, rapportK;
  int size, blacks;
  const char* lines[7][2];
};
const Persona kP[kCollectors] = {
    {"OSCAR VALE", "@ONEOFONE", "GRAIL HUNTER",
     "HEDGE-FUND MONEY AND A VAULT IN THE BASEMENT. ONLY THE BEST: ONE-OF-ONES, AUTOS, THE LEAGUE'S TOP 5. HE PAYS UP FOR THEM - AND MAKES YOU PAY "
     "FOR HIS.",
     0x15131C, 0xFFD23F, 0, 400000000LL, 3, 0.60, 1.00, 1.0, 1.0, 14, 0,
     {{"A FINE PIECE. DONE - SHAKE ON IT.", "NOW THAT'S A GRAIL. PLEASURE DOING BUSINESS."},
      {"CLOSE. SWEETEN IT AND IT'S YOURS.", "I DON'T HAGGLE MUCH. THIS IS MY NUMBER."},
      {"YOU'RE WASTING MY TIME.", "I OWN VAULTS. COME BACK WITH SOMETHING REAL."},
      {"NOT TODAY. BRING ME A GRAIL.", "PASS."},
      {"BULK? I DON'T DO BULK.", "BULK? I DON'T DO BULK."},
      {"I WANT IT. HERE'S MY OFFER.", "THAT BELONGS IN MY VAULT."},
      {"ONLY THE ONE-OF-ONES MATTER.", "SHOW ME SOMETHING RARE."}}},
    {"LOU BRASKI", "@THECARDSHARK", "LOWBALLER",
     "FORTY YEARS OF CARD SHOWS. NEVER PAID FULL PRICE IN HIS LIFE AND NEVER WILL. HE ALWAYS COUNTERS YOUR FIRST OFFER - IT'S THE "
     "FUN PART.",
     0x1F3A4A, 0x4FE0C8, 1, 8000000LL, 5, 0.55, 1.00, -0.5, 1.0, 30, 0,
     {{"HEH. YOU DRIVE A HARD BARGAIN. DEAL.", "FINE, FINE. YOU'RE ROBBING ME. DEAL."},
      {"I'LL MEET YOU... A LITTLE. HERE.", "NICE START. HERE'S MY NUMBER."},
      {"NOW YOU'RE SPEAKING MY LANGUAGE. STILL NO.", "HA! I LIKE YOU, KID. NO."},
      {"NOT EVEN CLOSE, KID.", "I'VE SEEN BETTER OFFERS AT A GARAGE SALE."},
      {"WHAT AM I GONNA DO WITH THAT?", "WHAT AM I GONNA DO WITH THAT?"},
      {"I'LL TAKE THAT OFF YOUR HANDS. CHEAP.", "TELL YOU WHAT I'LL DO FOR YOU..."},
      {"EVERYTHING'S NEGOTIABLE.", "THE FIRST OFFER'S NEVER THE REAL OFFER."}}},
    {"PIPPA OKAFOR", "@SHINYHUNTER", "POKEMON ONLY",
     "CAUGHT 'EM ALL - TWICE. SHINIES MAKE HER SCREAM. WON'T TOUCH A CARD THAT ISN'T A POKEMON, AND SHE GUARDS HER SHINIES WITH HER LIFE.",
     0xC8102E, 0xFFFFFF, 2, 25000000LL, 3, 0.65, 1.00, 0.6, 1.0, 28, 0,
     {{"YESSS! INTO THE BINDER IT GOES!", "DEAL! GOTTA CATCH 'EM ALL!"},
      {"SO CLOSE! A LITTLE MORE?", "ALMOST! MEET ME HALFWAY?"},
      {"THAT'S NOT VERY EFFECTIVE...", "NOPE. NOT EVEN A MAGIKARP DEAL."},
      {"SORRY, NOT THIS TIME.", "I'LL PASS ON THAT ONE."},
      {"ONLY POKEMON, FRIEND!", "ONLY POKEMON, FRIEND!"},
      {"I NEED IT FOR MY POKEDEX - PLEASE?", "THAT ONE! I'VE BEEN HUNTING IT FOREVER!"},
      {"SHINIES MAKE ME SCREAM.", "POKEMON ONLY. ALWAYS."}}},
    {"MAX ROMERO", "@MARVELMAX", "MARVEL FAN",
     "RUNS A COMIC SHOP. KNOWS EVERY SUIT AND EVERY VARIANT. MARVEL HEROES OR NOTHING WORTH TALKING ABOUT - THE ALT SUITS MOST OF ALL.",
     0xB0101E, 0x3A7BFF, 3, 20000000LL, 3, 0.65, 1.00, 0.6, 1.0, 26, 0,
     {{"EXCELSIOR! DEAL!", "ASSEMBLED. PLEASURE TRADING."},
      {"WITH GREAT CARDS COMES A GREAT PRICE. HERE.", "ALMOST, TRUE BELIEVER. TRY THIS."},
      {"THAT OFFER IS A VILLAIN ORIGIN STORY.", "NOT EVEN FOR A PAPER VARIANT."},
      {"NOT MY KIND OF ISSUE.", "PASS - NO CROSSOVER TODAY."},
      {"IF IT ISN'T MARVEL, IT ISN'T CANON.", "IF IT ISN'T MARVEL, IT ISN'T CANON."},
      {"I NEED THAT ONE FOR THE SHOP WALL!", "THAT HERO BELONGS WITH ME."},
      {"EVERY SUIT. EVERY VARIANT.", "MARVEL OR NOTHING."}}},
    {"SETH ADEYEMI", "@SETBUILDER", "SET BUILDER",
     "SPREADSHEETS. BINDERS SORTED BY NUMBER. HE'S MISSING SIX CARDS FROM HIS SET AND IT KEEPS HIM UP AT NIGHT - HE PAYS UP FOR THOSE.",
     0x0E5A3A, 0x6CFF9A, 4, 10000000LL, 3, 0.65, 1.00, 0.3, 1.0, 30, 0,
     {{"ONE CLOSER. DEAL!", "LOGGED. SPREADSHEET UPDATED - DEAL."},
      {"THE NUMBERS DON'T QUITE WORK. HOW ABOUT THIS?", "ADJUSTED FOR VALUE - HERE."},
      {"THAT'S NOT A REAL OFFER.", "THE MATH SAYS NO."},
      {"DOESN'T HELP MY SET.", "I'LL PASS."},
      {"THAT'S NOT ON MY LIST.", "THAT'S NOT ON MY LIST."},
      {"THAT'S ONE OF MY MISSING SIX - I'LL PAY UP.", "PLEASE. IT COMPLETES A PAGE."},
      {"SIX CARDS AND I'M DONE.", "SORTED BY NUMBER, ALWAYS."}}},
    {"FRANKIE DUVAL", "@FLIPFRANKIE", "FLIPPER",
     "BUYS LOW, SELLS HIGH, POSTS IT ALL ONLINE. ONLY TAKES A TRADE THAT MAKES HIM MONEY. BUYS THE DIP IN A CRASH, SELLS INTO A BOOM.",
     0x3A1E08, 0xFF8A1E, 5, 40000000LL, 2, 0.75, 1.06, 0.8, 1.0, 20, 0,
     {{"EASY MONEY. DEAL.", "PROFIT IS PROFIT. DONE."},
      {"THERE'S NO MARGIN IN THAT. THIS WORKS.", "I GOTTA EAT TOO. HERE."},
      {"YOU'RE TRYING TO FLIP ME? CUTE.", "NO MARGIN, NO DEAL."},
      {"CAN'T MAKE MONEY ON THAT.", "NAH."},
      {"NOBODY'S BUYING THAT FROM ME.", "NOBODY'S BUYING THAT FROM ME."},
      {"I'VE GOT A BUYER LINED UP. LET'S DEAL.", "QUICK FLIP - YOU IN?"},
      {"BUY LOW. SELL HIGH. POST IT.", "BOOMS ARE FOR SELLING."}}},
    {"VINNY CALDERA", "@VINTAGEVINNY", "NOSTALGIA",
     "PLAYED THE FIRST STRIKERS ON LAUNCH DAY. ONLY THE ORIGINALS - MARIO'S CREW AND THE CHARGED GANG. KIND TO ANYONE WHO LOVES THEM TOO.",
     0x3A1F5C, 0xFFE3A8, 6, 12000000LL, 4, 0.65, 1.00, 0.2, 2.0, 24, 0,
     {{"THE GOOD OLD DAYS! DEAL, PAL.", "NOW THAT'S A CLASSIC. DEAL."},
      {"AH, ALMOST, KID. MEET ME HERE?", "FOR AN OLD-TIMER? A BIT MORE."},
      {"THAT HURTS MY OLD HEART.", "THEY DON'T MAKE OFFERS LIKE THAT ANYMORE. GOOD."},
      {"NOT FOR ME, PAL.", "I'LL HOLD ONTO MINE."},
      {"THAT'S NEW-SCHOOL. NOT MY THING.", "THAT'S NEW-SCHOOL. NOT MY THING."},
      {"I PLAYED HIM ON LAUNCH DAY - CAN I HAVE IT?", "THE ORIGINALS! WHAT'LL IT TAKE?"},
      {"MARIO'S CREW. THE CHARGED GANG.", "NOTHING BEATS THE ORIGINALS."}}},
    {"GRACE KIM", "@GEMMINTGRACE", "GRADE SNOB",
     "A LOUPE IN HER POCKET, A CENTERING RULER IN HER BAG. TO HER A RAW CARD IS JUST A CARD WAITING TO BE GRADED. HER BINDER IS ALL SLABS.",
     0x0B3A4A, 0x7DF9FF, 7, 30000000LL, 3, 0.65, 1.00, 0.5, 1.0, 16, 0,
     {{"CENTERING'S PERFECT. DEAL.", "GEM MINT DEAL."},
      {"CORNERS ARE SOFT ON THAT OFFER. TRY THIS.", "THAT'S A 9, NOT A 10. HERE'S A 10."},
      {"THAT OFFER GRADES A 1.", "POOR CONDITION. NO."},
      {"DOESN'T MEET MY STANDARDS.", "PASS."},
      {"RAW? GRADE IT FIRST.", "RAW? GRADE IT FIRST."},
      {"THAT SLAB WOULD LOOK PERFECT ON MY WALL.", "A 10 LIKE THAT? I'M INTERESTED."},
      {"A RAW CARD IS A CARD WAITING TO BE GRADED.", "LOUPE IN MY POCKET. ALWAYS."}}},
    {"RAY OKONJO", "@ROOKIECARDRAY", "HYPE CHASER",
     "SIXTEEN AND WATCHES EVERY MATCH. WANTS WHOEVER IS HOT THIS MATCHDAY - AND DUMPS THEM THE SECOND THEY COOL OFF. SMALL WALLET, BIG HEART.",
     0x3A0A2E, 0xFFE94A, 8, 2500000LL, 3, 0.60, 1.00, 1.2, 1.0, 18, 0,
     {{"LET'S GOOO! DEAL!", "HE'S ON FIRE - DEAL!"},
      {"BRO, BUMP IT A LITTLE?", "ALMOST! LITTLE MORE?"},
      {"BRO. NO.", "THAT'S A BENCH-WARMER OFFER."},
      {"HE'S NOT HOT ANYMORE.", "NAH, PASS."},
      {"WHO EVEN IS THAT?", "WHO EVEN IS THAT?"},
      {"HE'S THE HOTTEST THING IN THE LEAGUE - TRADE ME!", "I SAW THAT GOAL - I NEED HIM!"},
      {"WHO'S HOT RIGHT NOW?", "ONLY THE HYPE."}}},
    // ---- r235 (Lance 11-12: "i also need more collectors"): eleven more, each his own taste and budget (COLLECTOR-TRADES.md 11)
    {"NOAH PARK", "@PACKCHASER", "SET CHASER",
     "RIPS A BOX OF LANCINI TIZM EVERY WEEK AND WON'T STOP TILL HE HAS ONE OF EVERY CARD. SELLS HIS DOUBLES CHEAP TO PAY FOR THE NEXT BOX.",
     0x10324A, 0x5FD0FF, 9, 6000000LL, 3, 0.65, 1.00, 0.4, 1.0, 30, 0,
     {{"ONE MORE FOR THE BINDER. DEAL!", "CHECKLIST UPDATED - DEAL."},
      {"CLOSE. A LITTLE MORE FOR THE NEXT BOX?", "MEET ME HERE AND IT'S DONE."},
      {"THAT WOULDN'T BUY ME A PACK.", "NOPE. NOT FOR THAT."},
      {"I'VE GOT THAT ONE ALREADY.", "PASS - IT'S A DOUBLE FOR ME."},
      {"THAT'S NOT IN THE SET I'M BUILDING.", "THAT'S NOT IN THE SET I'M BUILDING."},
      {"I'M MISSING THAT ONE - LET'S TRADE!", "THAT FILLS A HOLE IN MY BINDER."},
      {"ONE OF EVERY CARD. THAT'S THE GOAL.", "DOUBLES FOR SALE, CHEAP."}}},
    {"DEE WALLACE", "@JAM96DEE", "JAM '96 COMPLETIST",
     "GREW UP ON 90S INSERTS. RIPS JAM '96 BY THE BOX, CHASES EVERY DIE-CUT, AND KEEPS A WANT LIST TAPED TO HER FRIDGE.",
     0x2A0A3A, 0xFF4FD8, 10, 15000000LL, 3, 0.65, 1.00, 0.5, 1.0, 24, 0,
     {{"TOTALLY RAD. DEAL!", "INTO THE JAM BINDER - DEAL."},
      {"ALMOST! BUMP IT A LITTLE?", "SO CLOSE. MEET ME HERE?"},
      {"AS IF.", "THAT OFFER IS SO NOT JAM."},
      {"NOT FEELING IT.", "PASS."},
      {"ONLY JAM '96, BABY.", "ONLY JAM '96, BABY."},
      {"THAT DIE-CUT! I NEED IT!", "THAT ONE'S ON MY FRIDGE LIST!"},
      {"EVERY DIE-CUT. EVERY ONE.", "JAM '96 FOREVER."}}},
    {"VICTORIA NOIR", "@NOIRVICTORIA", "EXQUISITE ONLY",
     "OPENS EXQUISITE IN A QUIET ROOM WITH WHITE GLOVES. NUMBERED CARDS ONLY - THE LOWER THE RUN, THE BRIGHTER HER EYES.",
     0x0C0C10, 0xE6C774, 11, 120000000LL, 3, 0.65, 1.00, 0.6, 1.0, 12, 0,
     {{"EXQUISITE. DEAL.", "A FINE ADDITION. DEAL."},
      {"NEARLY. A TOUCH MORE, PLEASE.", "CLOSE - HERE IS MY NUMBER."},
      {"HOW VULGAR.", "THAT OFFER IS BENEATH US BOTH."},
      {"NOT FOR MY COLLECTION.", "I'LL DECLINE."},
      {"IF IT ISN'T NUMBERED, IT ISN'T ART.", "IF IT ISN'T NUMBERED, IT ISN'T ART."},
      {"THAT NUMBER BELONGS IN MY GALLERY.", "A LOW RUN LIKE THAT? I'M LISTENING."},
      {"WHITE GLOVES. ALWAYS.", "NUMBERED OR NOTHING."}}},
    {"KAI SATO", "@MELEEKAI", "MELEE NOSTALGIC",
     "STILL HAS HIS CUBE AND HIS CONTROLLER WITH THE WORN-OUT STICK. ONLY THE MELEE FIGHTERS MAKE HIS COLLECTION.",
     0x1A2A5A, 0xFF5A2A, 12, 8000000LL, 3, 0.65, 1.00, 0.4, 1.0, 24, 0,
     {{"GG. DEAL!", "THAT'S A COMBO. DEAL."},
      {"ALMOST - ONE MORE HIT?", "SO CLOSE. FINISH IT HERE?"},
      {"THAT'S A SELF-DESTRUCT.", "NO JOHNS. NO."},
      {"NOT MY MAIN.", "PASS."},
      {"NOT A MELEE FIGHTER.", "NOT A MELEE FIGHTER."},
      {"THAT FIGHTER! I MAINED HIM!", "THAT ONE'S GOING ON MY SHELF."},
      {"MELEE. FOREVER.", "THE CUBE STILL WORKS."}}},
    {"SAL MORETTI", "@CLUBSAL", "TEAM LOYALIST",
     "SEASON TICKET SINCE THE LEAGUE BEGAN. BUYS EVERY CARD OF HIS CLUB'S PLAYERS AND WON'T SELL ONE - NOT FOR ANY PRICE.",
     0x3A0E0E, 0xFFC23A, 13, 10000000LL, 4, 0.65, 1.00, 0.3, 1.5, 20, 0,
     {{"ONE FOR THE CLUB. DEAL!", "LOYALTY PAYS. DEAL."},
      {"FOR MY CLUB? A LITTLE MORE.", "CLOSE. MEET ME HERE."},
      {"THAT'S DISRESPECTFUL TO THE CLUB.", "NO WAY."},
      {"NOT ONE OF OURS.", "PASS."},
      {"WRONG CLUB, FRIEND.", "WRONG CLUB, FRIEND."},
      {"HE PLAYS FOR MY CLUB - I NEED HIM!", "THAT ONE'S GOING ON MY WALL."},
      {"MY CLUB. MY CARDS.", "SEASON TICKET, ROW ONE."}}},
    {"PRIYA DESAI", "@BOOTRACE", "STATS NERD",
     "SPREADSHEETS OF GOALS, ASSISTS AND THE GOLDEN BOOT RACE. BUYS THE LEAGUE'S TOP PERFORMERS AND SELLS THE ONES WHO COOL OFF.",
     0x0A2A2A, 0x39E6C8, 14, 35000000LL, 3, 0.65, 1.00, 0.5, 1.0, 18, 0,
     {{"THE NUMBERS SAY YES. DEAL.", "EFFICIENT. DEAL."},
      {"MY MODEL SAYS A BIT MORE.", "ADJUSTED FOR FORM - HERE."},
      {"STATISTICALLY ABSURD.", "THE DATA SAYS NO."},
      {"HIS NUMBERS DON'T JUSTIFY IT.", "PASS."},
      {"HE DOESN'T EVEN SCORE.", "HE DOESN'T EVEN SCORE."},
      {"HE'S TOP OF THE BOOT RACE - I'M BUYING.", "LOOK AT THOSE GOALS. TRADE?"},
      {"GOALS. ASSISTS. THE RACE.", "THE NUMBERS NEVER LIE."}}},
    {"GUS PEREZ", "@BARGAINBINGUS", "BARGAIN HUNTER",
     "DIGS THROUGH EVERY DOLLAR BIN AT EVERY SHOW. BUYS UNDER THE MARKET, SELLS AT IT, AND TELLS YOU ABOUT IT FOR AN HOUR.",
     0x2A2A10, 0xC8FF3A, 15, 1500000LL, 4, 0.60, 1.00, 0.2, 1.0, 30, 0,
     {{"STEAL! DEAL!", "A BARGAIN IS A BARGAIN. DEAL."},
      {"A LITTLE CHEAPER AND IT'S DONE.", "MEET ME IN THE BIN, HERE."},
      {"THAT'S RETAIL. I DON'T PAY RETAIL.", "NOPE."},
      {"NO MARGIN IN IT FOR ME.", "PASS."},
      {"I CAN'T FLIP THAT.", "I CAN'T FLIP THAT."},
      {"I'LL TAKE IT - CHEAP!", "THAT'S A DOLLAR-BIN GEM."},
      {"NEVER PAY FULL PRICE.", "THE DOLLAR BIN NEVER LIES."}}},
    {"DR. ELLIS FORD", "@SLABDOCTOR", "GRADER",
     "BUYS RAW CARDS, SENDS THEM TO TIZM GRADING AND SELLS THE SLABS. A LOUPE, A LIGHTBOX AND NO PATIENCE FOR SOFT CORNERS.",
     0x10203A, 0xB8E0FF, 16, 20000000LL, 3, 0.65, 1.00, 0.4, 1.0, 16, 0,
     {{"CLEAN CORNERS. DEAL.", "THAT'LL SLAB NICELY. DEAL."},
      {"THE SURFACE SAYS A LITTLE LESS - HERE.", "CLOSE. MY NUMBER."},
      {"THAT OFFER HAS CREASES.", "NO."},
      {"NOT WORTH THE GRADING FEE.", "PASS."},
      {"TOO CHEAP TO SEND IN.", "TOO CHEAP TO SEND IN."},
      {"THAT ONE COULD BE A 10. I WANT IT.", "SEND IT MY WAY - I'LL SLAB IT."},
      {"RAW TODAY, SLABBED TOMORROW.", "SOFT CORNERS? NO THANKS."}}},
    {"JADE WINTERS", "@RCJADE", "ROOKIE SPECULATOR",
     "BETS ON THE ORIGINALS BEFORE ANYONE KNOWS THEIR NAMES. ROOKIE CARDS, RPAS, AND A LOT OF HOPE.",
     0x2A103A, 0xB0FF5A, 17, 12000000LL, 3, 0.65, 1.00, 0.9, 1.0, 18, 0,
     {{"ROOKIE SEASON! DEAL!", "GETTING IN EARLY. DEAL."},
      {"A BIT MORE AND HE'S MINE.", "ALMOST - HERE?"},
      {"THAT'S NOT A ROOKIE PRICE.", "NO."},
      {"NOT A ROOKIE.", "PASS."},
      {"VETERANS DON'T MOVE THE NEEDLE.", "VETERANS DON'T MOVE THE NEEDLE."},
      {"THAT ROOKIE IS GOING TO POP - TRADE ME!", "RC! I NEED THAT RC!"},
      {"BUY THE ROOKIE, HOLD THE ROOKIE.", "THE NEXT BIG THING IS AN ORIGINAL."}}},
    {"MILO VANCE", "@NUMBER1MILO", "LOW-SERIAL HUNTER",
     "ONLY THE NUMBER MATTERS: THE 01, THE LAST ONE, THE ONE THAT MATCHES THE JERSEY. HE'LL PAY DOUBLE FOR THE RIGHT SERIAL.",
     0x1A1A1A, 0xFF8A3A, 18, 50000000LL, 3, 0.65, 1.00, 0.5, 1.0, 14, 0,
     {{"THE RIGHT NUMBER. DEAL.", "01 OF 10. DEAL."},
      {"WRONG NUMBER FOR THAT PRICE - HERE.", "CLOSE. MY NUMBER."},
      {"THAT'S A NUMBER I'D NEVER PAY.", "NO."},
      {"THE SERIAL'S BORING.", "PASS."},
      {"NO NUMBER ON IT? NO INTEREST.", "NO NUMBER ON IT? NO INTEREST."},
      {"THAT SERIAL - I'VE HUNTED IT FOR YEARS!", "THE JERSEY NUMBER! TRADE ME!"},
      {"THE 01. THE LAST ONE. THE JERSEY.", "ONLY THE NUMBER MATTERS."}}},
    {"AUGUSTUS KING", "@THEWHALE", "WHALE",
     "OWNS A JET AND THREE CARD SHOPS. BUYS WHATEVER IS MOST EXPENSIVE, RIPS EXQUISITE LIKE BUBBLEGUM AND NEVER ASKS THE PRICE.",
     0x2A2008, 0xFFE07A, 19, 2000000000LL, 4, 0.60, 1.00, 0.8, 1.0, 12, 0,
     {{"BOUGHT. NEXT.", "WRAP IT UP. DEAL."},
      {"SWEETEN IT. I'M IN A HURRY.", "HERE. TAKE IT OR LEAVE IT."},
      {"YOU KNOW WHO I AM?", "I DON'T HAVE TIME FOR THAT."},
      {"NOT EXPENSIVE ENOUGH TO INTEREST ME.", "PASS."},
      {"CHEAP CARDS BORE ME.", "CHEAP CARDS BORE ME."},
      {"THE PRICIEST THING IN THE ROOM. MINE.", "NAME YOUR PRICE."},
      {"IF IT'S THE BEST, IT'S MINE.", "BUBBLEGUM AND EXQUISITE."}}},
};
// her / his (PIPPA, GRACE, DEE, VICTORIA, PRIYA and JADE are women)
constexpr bool kShe[kCollectors] = {false, false, true, false, false, false, false, true, false, false, true, true, false, false, true, false, false, true, false, false};
const char* HE(int c) { return c >= 0 && c < kCollectors && kShe[c] ? "SHE" : "HE"; }
const char* HIS(int c) { return c >= 0 && c < kCollectors && kShe[c] ? "HER" : "HIS"; }
std::string HEs(int c, const char* rest) { return std::string(HE(c)) + rest; }
std::string HISs(int c, const char* rest) { return std::string(HIS(c)) + rest; }
const char* const kPatienceOut = "THAT'S ENOUGH FOR TODAY. COME BACK NEXT MATCHDAY.";
const char* const kNoCash = "I DON'T HAVE THAT KIND OF CASH.";
// the SET BUILDER's sets (a season each, in turn): the series he's building
const int kSetSeries[3][2] = {{kSerStrikers, kSerCharged}, {kSerMelee, kSerMelee}, {kSerOriginals, kSerOriginals}};
const char* const kSetName[3] = {"THE STRIKERS + CHARGED SET", "THE MELEE SET", "THE ORIGINALS SET"};
constexpr int kWants = 6, kMaxBinder = 72, kMaxHistory = 40, kMaxWire = 12;  // (r235: binders 48 -> 72: their pulls + buys)
// r235: each collector's HOBBY (COLLECTOR-TRADES.md 11): packs he rips a matchday (expected: LANCINI TIZM, JAM '96, EXQUISITE - his budget's
// worth), singles he may buy a matchday, his asking price (x the market: lo .. hi; `motivated` the share of his listings he prices to move:
// 90-98% of the market), how much of his binder he keeps up for sale, `stubborn` (never cuts an unsold price)
struct Hobby {
  double a, j, e;
  int buys;
  double lo, hi, motivated, share;
  bool stubborn;
};
const Hobby kHob[kCollectors] = {
    {0.5, 1.0, 1.0, 2, 1.25, 2.20, 0.03, 0.10, true},    // OSCAR: the premium packs; his prices the earth
    {1.0, 0.3, 0.0, 2, 0.98, 1.40, 0.20, 0.30, true},    // LOU: cheap packs, lists a lot, haggles
    {2.0, 0.6, 0.1, 1, 1.00, 1.30, 0.20, 0.15, false},   // PIPPA
    {1.5, 0.5, 0.1, 1, 1.00, 1.30, 0.20, 0.15, false},   // MAX
    {2.5, 0.3, 0.0, 1, 0.98, 1.15, 0.25, 0.20, false},   // SETH
    {1.5, 0.8, 0.3, 3, 1.05, 1.35, 0.30, 0.45, false},   // FRANKIE: a flipper - half his binder for sale
    {1.0, 1.2, 0.0, 1, 1.05, 1.40, 0.15, 0.10, false},   // VINNY
    {0.5, 0.5, 0.2, 1, 1.10, 1.50, 0.10, 0.15, false},   // GRACE
    {1.0, 0.2, 0.0, 1, 0.95, 1.20, 0.40, 0.35, false},   // RAY: dumps whoever cooled off
    {4.0, 0.0, 0.0, 1, 0.95, 1.10, 0.35, 0.25, false},   // NOAH: a box of LANCINI TIZM a week, doubles sold cheap
    {0.5, 3.0, 0.0, 1, 0.98, 1.20, 0.25, 0.25, false},   // DEE: JAM '96 by the box
    {0.0, 0.3, 2.0, 1, 1.05, 1.45, 0.10, 0.15, true},    // VICTORIA: EXQUISITE
    {2.0, 0.5, 0.0, 1, 1.00, 1.25, 0.20, 0.15, false},   // KAI
    {1.5, 0.5, 0.2, 1, 1.00, 1.30, 0.15, 0.10, false},   // SAL: never sells his club
    {1.0, 0.5, 0.3, 2, 1.00, 1.35, 0.20, 0.25, false},   // PRIYA
    {2.0, 0.0, 0.0, 3, 0.95, 1.08, 0.50, 0.50, false},   // GUS: the bargain bin - buys under, sells at
    {1.0, 0.5, 0.2, 1, 1.05, 1.40, 0.15, 0.20, false},   // ELLIS
    {1.5, 0.3, 0.5, 2, 1.00, 1.45, 0.20, 0.20, false},   // JADE
    {0.5, 0.5, 0.8, 1, 1.10, 1.60, 0.10, 0.15, true},    // MILO
    {1.0, 2.0, 4.0, 3, 1.20, 2.50, 0.05, 0.10, true},    // AUGUSTUS: rips EXQUISITE like bubblegum
};
constexpr long long kPackCost[3] = {50, 250, 1000};  // the shop's (xr_cards kPackPrice / kJamPackPrice / xrexq::kPackPrice)

uint64_t mix64(uint64_t z) {
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}
uint64_t hstr(const std::string& s) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
  return h;
}
double u01(uint64_t h) { return static_cast<double>(mix64(h) >> 11) * (1.0 / 9007199254740992.0); }
double clampd(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }
std::string upper(std::string s) {
  for (char& c : s)
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
  return s;
}
std::string T(long long n) { return xrcoin::text(n); }
// 3 significant figures, rounded up / down (a counter's ask rounds up: he never asks less than he needs; his coins round down)
long long nice_up(double d) {
  if (d <= 0) return 0;
  long long v = static_cast<long long>(std::ceil(d)), m = 1;
  while (v / m >= 1000) m *= 10;
  return xrcoin::clamp(((v + m - 1) / m) * m);
}
long long nice_down(double d) {
  if (d < 1) return 0;
  long long v = static_cast<long long>(std::floor(d)), m = 1;
  while (v / m >= 1000) m *= 10;
  return xrcoin::clamp((v / m) * m);
}

// ============================================================================================== the state
struct Col {
  double goodwill = 0.0;
  long long wallet = 0;
  int patience = 3, lowballs = 0, trades = 0;
  bool firstToday = true;  // (the LOWBALLER counters the first offer of a matchday)
  std::vector<Item> items;  // his binder, by value (most first)
  int setIdx = 0;           // the SET BUILDER's set
  std::vector<int> wants;   // his want list (cards); wantDone the ones you brought him
  std::vector<int> wantDone;
  bool hasCounter = false;
  Offer counter;
  std::string say;  // his last word (the bubble)
  int sayKind = -1;
  double sayAt = -1e9;
};
struct Incoming {
  uint32_t id = 0;
  int col = 0;
  Variant want;
  Offer give;
  int day = 0, expires = 0;
  bool final = false;
};
struct Trade {
  int day = 0, col = 0;
  std::vector<Variant> gave, got;
  long long gaveCoins = 0, gotCoins = 0, gaveMarket = 0, gotMarket = 0;
  std::string headline;
};
struct Wire {
  int day = 0;
  uint32_t rgb = 0xFFD23F;
  std::string text;
};
struct State {
  unsigned career = 0;
  bool loaded = false;
  int lastDay = -1, season = -1;
  uint32_t nextId = 1001;
  Col col[kCollectors];
  std::vector<Incoming> inc;
  std::vector<Trade> hist;  // newest first
  std::vector<Wire> wire;   // newest first
  // r235: THE DEALER'S CASE (an old career's pre-placed chase cards, bought out at its first r235 load: for sale to anyone), the hobby's
  // counts, the file's version (235: the rules of r235 - an older file is re-balanced once)
  std::vector<Item> deal;
  HobbyStats hob;
  int ver = 0;
};

std::mutex g_m;
Source g_src;
State g_st;
std::vector<xrmarket::CardInfo> g_set;
std::unordered_map<std::string, int> g_byKey;
std::vector<int> g_tier;           // demand tier per card (xr_market's)
std::vector<float> g_chg;          // this matchday's move per card (its BASE copy, %): the HYPE CHASER, the LOWBALLER's "hot"
std::vector<int> g_club;           // r235: each card's club in this career (xrmarket::club_of, refreshed with the moves): the TEAM LOYALIST
std::vector<int> g_rank;           // r235: each card's family's place in the TOP CARDS race (the STATS NERD, the listings' search)
bool g_indexDirty = true;          // r235: the listings changed - the market's search index pushed again (push_index)
std::unordered_map<uint32_t, long long> g_px;  // a binder item's market price (refreshed every matchday / when it comes in)
double g_cycle = 0.0;              // the market's mood (xr_market cycle_now) as of the last read
int g_gen = 1;
double g_now = 0.0, g_lastRead = -1e9;
std::string g_msg;
uint32_t g_msgRgb = 0xFFD23F;
double g_msgAt = -1e9, g_flashAt = -1e9;
// the market's sold copies (world_got / r235 world_sold: any thread) waiting for the next frame - their own leaf lock
struct Sold {
  Variant v;
  std::string who;
  long long coins = -1;  // (-1: r213's world_got - a 1/1, no price told)
};
std::mutex g_qm;
std::vector<Sold> g_queue;

// ---- the UI
constexpr int kTabCols = 0, kTabOffers = 1, kTabHistory = 2, kTabs = 3;
constexpr int kPaneHis = 0, kPaneMine = 1, kPaneCoins = 2;
constexpr int kGridCols = 5, kGridRows = 3;
int g_tab = 0, g_colCur = 0, g_pane = 0, g_coinCur = 0, g_incCur = 0, g_histTop = 0;
int g_paneCur[2] = {0, 0}, g_paneTop[2] = {0, 0};
bool g_desk = false, g_showCounter = false, g_incCountering = false;
int g_incPick = 1;
constexpr int kCounterPct[3] = {5, 10, 15};
Offer g_draft[kCollectors];
int g_ask = 0;  // 0 none, 1 send the offer, 2 accept an offer of theirs, 3 accept his counter
uint32_t g_askId = 0;
// YOUR CARDS (the pane): your copies by value, priced + sorted when the pane opens (and after a trade) - never per frame
struct MineRow {
  Holding h;
  long long px = 0;
  int lock = 0;  // 0 free, 1 listed (LIST FOR SALE), 2 held in an open trade
  int free = 0;  // copies of it you could put in
};
std::vector<MineRow> g_mineRows;
size_t g_mineSig = 0;
// the number pad (the market's keys): your coins (0) or his (1)
std::atomic<bool> g_pad{false};
int g_padFor = 0, g_padKey = 12, g_padHit = -1, g_padHover = -1;
std::string g_padDigits, g_padSay;
bool g_padFresh = true, g_pointDirty = false;
double g_padSayAt = -1e9, g_padHitAt = -1e9;
std::atomic<bool> g_stashClick{false};
std::atomic<float> g_stashX{0.0f}, g_stashY{0.0f};

std::string home_dir() {
  const char* h = std::getenv("STRIKERS_HOME");
  return std::string(h != nullptr && *h != '\0' ? h : "/storage/emulated/0/Strikers");
}
std::string trades_path(unsigned c) { return home_dir() + "/cards/trades-" + std::to_string(c) + ".txt"; }
std::string world_path(unsigned c) { return home_dir() + "/cards/world11-" + std::to_string(c) + ".txt"; }
void say(const std::string& m, uint32_t rgb) {
  g_msg = m;
  g_msgRgb = rgb;
  g_msgAt = g_now;
}
void sound(const char* n, float gain) {
  if (g_src.sound != nullptr) g_src.sound(n, gain);
}
void logl(const std::string& s) {
  if (g_src.log != nullptr) g_src.log(s.c_str());
}
long long balance() { return g_src.balance != nullptr ? g_src.balance() : 0; }
void load_set() {
  if (!g_set.empty() || g_src.catalogue == nullptr) return;
  g_src.catalogue(&g_set);
  g_byKey.clear();
  for (size_t i = 0; i < g_set.size(); ++i) g_byKey[g_set[i].key] = static_cast<int>(i);
  g_tier.assign(g_set.size(), 2);
  for (size_t i = 0; i < g_set.size(); ++i) g_tier[i] = xrmarket::hype_tier(static_cast<int>(i));
  g_chg.assign(g_set.size(), 0.0f);
  g_club.assign(g_set.size(), -1);  // r235
  g_rank.assign(g_set.size(), 0);
}
bool card_ok(int c) { return c >= 0 && c < static_cast<int>(g_set.size()); }
int tier(int c) { return card_ok(c) ? g_tier[static_cast<size_t>(c)] : 2; }
int series(int c) { return card_ok(c) ? g_set[static_cast<size_t>(c)].series : 0; }
// r217: a LANCINI TIZM JAM '96 card (the premium set: the hobby's most coveted - see taste_base / hold_base, PACKS-AB.md)
bool jam(int c) { return card_ok(c) && g_set[static_cast<size_t>(c)].set == 1; }
// r230: a LANCINI EXQUISITE card (every copy numbered: its tier + number in the serial) - the collectors want them MOST (taste / hold below)
bool exq(int c) { return card_ok(c) && g_set[static_cast<size_t>(c)].set == 2; }
int exq_t(const Variant& v) { return exq(v.card) ? xrexq::tier_of(v.serial) : 0; }
// how often a collector's binder rolls an Exquisite card (its pool weight): OSCAR 1 : 2, GRACE 1 : 3, the rest 1 : 8
double exq_weight(int c);
// r217: how often a collector's binder rolls a Jam card for every LANCINI TIZM one (its pool weight: VINNY 1:1 = half his binder,
// OSCAR 1:3, the rest 1:4)
double jam_weight(int c) {
  switch (c) {  // (r235: DEE JAM '96 most, NOAH / VICTORIA none)
    case kNostalgia: return 1.0;
    case kGrailHunter: return 1.0 / 3.0;
    case kJamFan: return 8.0;
    case kSetChaser: case kExqFan: return 0.0;
    default: return 0.25;
  }
}
std::string cname(int c) { return card_ok(c) ? upper(g_set[static_cast<size_t>(c)].name) : std::string("?"); }
float chg(int c) { return card_ok(c) ? g_chg[static_cast<size_t>(c)] : 0.0f; }
std::string sig(const Variant& v) { return xrmarket::variant_sig(g_set, v); }
long long px_of(const Variant& v) { return xrmarket::price(v); }  // (the market's lock: never held here by the market)
long long item_px(const Item& it) {
  auto f = g_px.find(it.id);
  if (f != g_px.end()) return f->second;
  const long long p = px_of(it.v);
  g_px[it.id] = p;
  return p;
}
// "GOLD 03/10 GENGAR", "BLACK 1/1 CHARIZARD", "SIGNED MEWTWO", "TIZM 10 PIKACHU", "SILVER LUIGI", "MARIO"
std::string short_words(const Variant& v) {
  std::string s;
  if (exq_t(v) > 0) {  // r230: "TIZM 10 EXQUISITE SAPPHIRE 03/10 MARIO"
    if (v.grade > 0) s += "TIZM " + std::to_string(v.grade) + " ";
    std::string nm = cname(v.card);  // (r235: its name without the set's word - "EXQUISITE MASTERPIECE 1/1 BOO", not "... EXQUISITE BOO")
    if (nm.compare(0, 10, "EXQUISITE ") == 0) nm = nm.substr(10);
    return s + "EXQUISITE " + xrexq::short_name(exq_t(v)) + " " + xrexq::num_text(v.serial) + " " + nm;
  }
  if (v.grade > 0) s += "TIZM " + std::to_string(v.grade) + " ";
  if (v.hit == kHitSigned) s += "SIGNED ";
  else if (v.hit == kHitBoom) s += "TIZM BOOM ";
  else if (v.hit == kHitHome) s += "HOMETOWN ";
  if (v.parallel == kBlack) s += "BLACK 1/1 ";
  else if (v.parallel == kGold) {
    char b[24];
    std::snprintf(b, sizeof(b), "GOLD %02d/10 ", v.serial);
    s += b;
  } else if (v.parallel == kSilver) s += "SILVER ";
  if (v.shiny) s += "SHINY ";
  return s + cname(v.card);
}
std::string finish_tag(const Variant& v) {  // (a grid cell's line: short)
  if (exq_t(v) > 0) return (v.grade > 0 ? "SLAB " + std::to_string(v.grade) + " " : std::string()) + xrexq::short_name(exq_t(v)) + " " + xrexq::num_text(v.serial);  // r230
  std::string s = v.parallel == kBlack ? "1/1" : (v.parallel == kGold ? (v.serial < 10 ? "GOLD 0" : "GOLD ") + std::to_string(v.serial) : (v.parallel == kSilver ? "SILVER" : ""));
  const char* hit = v.hit == kHitSigned ? "SIGNED" : (v.hit == kHitBoom ? "BOOM" : (v.hit == kHitHome ? "HOMETOWN" : ""));
  if (hit[0] != '\0') s = s.empty() ? hit : s + " " + hit;
  if (v.shiny) s = s.empty() ? "SHINY" : s + " *";
  if (v.grade > 0) s = "SLAB " + std::to_string(v.grade) + (s.empty() ? "" : " " + s);
  return s.empty() ? "BASE" : s;
}

// ============================================================================================== how they value a trade (3)
bool grail_copy(const Variant& v) {
  return v.parallel == kBlack || v.hit == kHitSigned || v.hit == kHitBoom || tier(v.card) == 5 || (tier(v.card) == 4 && v.parallel >= kGold);
}
bool in_set(int c, int card) {  // the SET BUILDER's set (this season's series; r217: its LANCINI TIZM cards - Jam ones only on his want list)
  const int si = std::max(0, std::min(2, g_st.col[c].setIdx));
  const int s = series(card);
  return card_ok(card) && !jam(card) && (s == kSetSeries[si][0] || s == kSetSeries[si][1]);
}
bool wanted(int c, int card) {
  const Col& k = g_st.col[c];
  for (size_t i = 0; i < k.wants.size(); ++i)
    if (k.wants[i] == card && !(i < k.wantDone.size() && k.wantDone[i] != 0)) return true;
  return false;
}
double mood_of(int c) { return clampd(kP[c].moodSens * 2.0 * g_cycle, -1.0, 1.0); }
double taste_base0(int c, const Variant& v);
void push_index();  // r235 (THE HOBBY, below)
bool has_id(const std::vector<uint32_t>& ids, uint32_t id) {  // (a plain loop: libc++ turns std::find on 32-bit ints into wmemchr)
  for (uint32_t x : ids)
    if (x == id) return true;
  return false;
}
// ---- r235: the eleven's tastes read these
int sal_club_of(unsigned career) { return static_cast<int>(mix64(static_cast<uint64_t>(career) * 0x5A1C1B0ull + 13) % static_cast<uint64_t>(PORT_FR_CLUBS)); }
int sal_club() { return sal_club_of(g_st.career); }
int club(int card) { return card_ok(card) ? g_club[static_cast<size_t>(card)] : -1; }
bool has_card(int c, int card) {
  for (const Item& it : g_st.col[c].items)
    if (it.v.card == card) return true;
  return false;
}
// a numbered copy's special number: a GOLD's 01 or 10, an EXQUISITE run's #1 / last / jersey number (its checklist No.), any 1/1
bool special_serial(const Variant& v) {
  if (exq(v.card)) {
    const int t = xrexq::tier_of(v.serial), n = xrexq::num_of(v.serial);
    return t == xrexq::kMasterpiece || n == 1 || (t > 0 && n == xrexq::kRun[t]) || xrexq::jersey(v.serial, g_set[static_cast<size_t>(v.card)].no);
  }
  return v.parallel == kBlack || (v.parallel == kGold && (v.serial == 1 || v.serial == 10));
}
bool numbered(const Variant& v) { return exq(v.card) || v.parallel >= kGold; }
// r217 (Lance 11-11: JAM '96 cards MORE COVETED): VINNY loves Jam '96 most (every Jam card 1.10), OSCAR the grail hunter wants the Jam 1/1s
// and autos above all (1.10; a Jam card he'd pass on as a LANCINI TIZM one still 0.90), the rest a little more (+0.05). Always under
// kTasteMax (the no-money-machine invariant: a Jam card's coins come from its market price, ~3-9x)
double taste_base(int c, const Variant& v) {
  const double b = taste_base0(c, v);
  if (exq(v.card) && b > 0.0) {  // r230: LANCINI EXQUISITE - the most coveted: OSCAR and GRACE (her slabs) pay the most (1.10), the rest +0.08
    if (c == kGrailHunter || c == kExqFan) return 1.10;  // (r235: VICTORIA - EXQUISITE only)
    if (c == kGradeSnob) return v.grade >= 9 ? 1.10 : std::min(1.10, b + 0.12);
    if (c == kSetBuilder || c == kSetChaser || c == kJamFan || c == kBargain || c == kWhale || c == kSerialHunter) return b;  // (r235: their own rule says it)
    return std::min(1.10, b + 0.08);
  }
  if (!jam(v.card) || b <= 0.0) return b;
  if (c == kNostalgia || c == kJamFan) return c == kJamFan && has_card(c, v.card) ? 1.02 : 1.10;  // (r235: DEE - JAM '96, the ones she lacks most)
  if (c == kSetChaser || c == kExqFan || c == kBargain || c == kWhale || c == kSerialHunter) return b;  // (r235)
  if (c == kGrailHunter) return v.parallel == kBlack || v.hit == kHitSigned || grail_copy(v) ? 1.10 : 0.90;
  if (c == kSetBuilder) return b;  // (his want list says it)
  return std::min(1.10, b + 0.05);
}
double taste_base0(int c, const Variant& v) {
  if (!card_ok(v.card)) return 0.0;
  const int s = series(v.card), t = tier(v.card);
  switch (c) {
    case kGrailHunter: return grail_copy(v) ? 1.10 : (t == 4 && v.parallel >= kSilver ? 1.00 : 0.70);
    case kLowballer: return chg(v.card) > 8.0f ? 0.90 : 0.82;
    case kPokeOnly: return s != kSerPokemon ? 0.0 : (v.shiny ? 1.10 : (t == 5 ? 1.08 : 1.05));
    case kMarvelFan: {
      if (s != kSerMarvel) return 0.80;
      const std::string& k = g_set[static_cast<size_t>(v.card)].key;
      return k.find("_c2") != std::string::npos || k.find("_c3") != std::string::npos || k.find("_c4") != std::string::npos ? 1.10 : 1.08;
    }
    case kSetBuilder: return wanted(c, v.card) ? 1.10 : 0.85;
    case kFlipper: return g_cycle < -0.06 ? 0.95 : 0.88;
    case kNostalgia: return s == kSerStrikers || s == kSerCharged ? 1.10 : (s == kSerMelee ? 0.95 : 0.80);
    case kGradeSnob: return v.grade >= 10 ? 1.10 : (v.grade == 9 ? 1.05 : (v.grade == 8 ? 0.95 : (v.grade > 0 ? 0.80 : 0.78)));
    case kHypeChaser: return clampd(0.86 + 0.012 * chg(v.card), 0.75, 1.10);
    // r235: the eleven (TASTE never over 1.10: the r213 invariant)
    case kSetChaser: return !jam(v.card) && !exq(v.card) ? (has_card(c, v.card) ? 0.85 : 1.06) : 0.80;
    case kJamFan: return 0.80;  // (a JAM '96 card: taste_base)
    case kExqFan: return 0.75;  // (an EXQUISITE card: taste_base)
    case kMeleeFan: return s == kSerMelee ? 1.10 : 0.80;
    case kLoyalist: return club(v.card) >= 0 && club(v.card) == sal_club() ? 1.10 : 0.80;
    case kStatsNerd: return t == 5 ? 1.10 : (t == 4 ? 1.03 : (t == 3 ? 0.95 : 0.78));
    case kBargain: return 0.80;  // (he only buys under the market: hobby_buy)
    case kGrader: return v.grade > 0 ? 0.95 : (px_of(v) >= 1000 ? 1.00 : 0.85);
    case kRookieSpec: return s == kSerOriginals ? 1.10 : 0.75;
    case kSerialHunter: return special_serial(v) ? 1.10 : (numbered(v) ? 1.00 : 0.75);
    case kWhale: {
      const long long p = px_of(v);
      return p >= 1000000 ? 1.08 : (p >= 50000 ? 1.00 : 0.70);
    }
  }
  return 0.85;
}
double hold_base(int c, const Variant& v) {
  if (!card_ok(v.card)) return 2.0;
  const int s = series(v.card);
  // r230: an EXQUISITE card - what they part with hardest of all: OSCAR its MASTERPIECE (1.70) / an auto or patch (1.50), GRACE its slab
  // (1.45), everyone else 1.30
  if (exq(v.card)) {
    const int et = exq_t(v);
    if (c == kGrailHunter) return et == xrexq::kMasterpiece ? 1.70 : (et >= xrexq::kMasterpiece || et == xrexq::kSapphire ? 1.50 : 1.35);
    if (c == kGradeSnob && v.grade > 0) return 1.45;
    if (c == kExqFan) return 1.40;                    // r235: VICTORIA
    if (c == kSerialHunter && special_serial(v)) return 1.35;
    return 1.30;
  }
  if (jam(v.card) && c == kJamFan) return 1.25;        // r235: DEE
  // r217: what they part with hardest - OSCAR a Jam 1/1 (1.60) / a Jam auto (1.45), VINNY any Jam card (1.30)
  if (jam(v.card) && c == kGrailHunter && v.parallel == kBlack) return 1.60;
  if (jam(v.card) && c == kGrailHunter && v.hit == kHitSigned) return 1.45;
  if (jam(v.card) && c == kNostalgia) return 1.30;
  switch (c) {
    case kGrailHunter: return grail_copy(v) ? 1.40 : 1.15;
    case kLowballer: return 1.25;
    case kPokeOnly: return v.shiny ? 1.30 : (s == kSerPokemon ? 1.15 : 1.12);
    case kMarvelFan: return s == kSerMarvel ? 1.20 : 1.12;
    case kSetBuilder: return in_set(c, v.card) ? 1.35 : 1.12;
    case kFlipper: return g_cycle > 0.08 ? 1.12 : 1.20;
    case kNostalgia: return s == kSerStrikers || s == kSerCharged ? 1.20 : 1.12;
    case kGradeSnob: return v.grade >= 10 ? 1.35 : (v.grade > 0 ? 1.25 : 1.12);
    case kHypeChaser: return chg(v.card) > 8.0f ? 1.25 : 1.12;
    // r235 (HOLD never under 1.12)
    case kSetChaser: return !jam(v.card) ? 1.15 : 1.12;
    case kMeleeFan: return s == kSerMelee ? 1.20 : 1.12;
    case kLoyalist: return club(v.card) >= 0 && club(v.card) == sal_club() ? 1.30 : 1.12;
    case kStatsNerd: return tier(v.card) >= 4 ? 1.25 : 1.12;
    case kGrader: return v.grade > 0 ? 1.20 : 1.12;
    case kRookieSpec: return s == kSerOriginals ? 1.30 : 1.12;
    case kSerialHunter: return special_serial(v) ? 1.35 : 1.12;
    case kWhale: return px_of(v) >= 1000000 ? 1.30 : 1.15;
  }
  return 1.12;
}
// NEED / TASTE / HOLD now: the personality x the mood x the rapport, held inside the invariant (TASTE <= 1.10, HOLD >= 1.12, NEED >= 1)
double need_of(int c) {
  const double g = g_st.col[c].goodwill, m = mood_of(c);
  return clampd(kP[c].need + 0.10 * std::max(0.0, -g) - 0.02 * std::max(0.0, g) - 0.02 * m, std::max(1.0, kP[c].need - 0.03), 1.30);
}
double taste_of(int c, const Variant& v) {
  const double b = taste_base(c, v);
  if (b <= 0.0) return 0.0;
  const double g = g_st.col[c].goodwill, m = mood_of(c);
  return clampd(b * (1.0 + 0.03 * m + 0.03 * std::max(0.0, g) - 0.06 * std::max(0.0, -g)), 0.0, kTasteMax);
}
double hold_of(int c, const Variant& v) { return std::max(kHoldMin, hold_base(c, v)); }
int patience_of(int c) {
  const double g = g_st.col[c].goodwill;
  return std::max(1, kP[c].patience + (g > 0.5 ? 1 : 0) - (g < -0.5 ? 1 : 0));
}
const Item* item_by(int c, uint32_t id) {
  for (const Item& it : g_st.col[c].items)
    if (it.id == id) return &it;
  return nullptr;
}
struct Value {
  double gets = 0, gives = 0;  // in HIS eyes: what he gets (your side x TASTE + coins), what he gives (his x HOLD + coins)
  long long mineMarket = 0, theirsMarket = 0;  // both sides at this week's market (+ coins)
  bool notInto = false;        // a card of yours he won't touch (TASTE 0)
  double ratio() const { return gives <= 0.0 ? (gets > 0 ? 1e9 : 0.0) : gets / gives; }
};
// r235: what he credits your copy at: his TASTE x its market - but a copy you bought on the market in the last 2 matchdays at no more than
// 98% of what you paid (THE FLIP RULE: an under-market listing bought today can't be traded to a collector for more the same week - a hop
// still loses >= 2%; after that it's a flip like any other, at the market's risk)
double credit(int c, const Variant& v) {
  const double t = taste_of(c, v) * static_cast<double>(px_of(v));
  long long paid = 0;
  if (t > 0.0 && xrmarket::paid_recent(v, &paid)) return std::min(t, 0.98 * static_cast<double>(paid));
  return t;
}
Value value_of(int c, const Offer& o) {
  Value v;
  for (const Variant& m : o.mine) {
    const long long p = px_of(m);
    const double t = taste_of(c, m);
    if (t <= 0.0) v.notInto = true;
    v.gets += credit(c, m);
    v.mineMarket = xrcoin::add(v.mineMarket, p);
  }
  for (uint32_t id : o.theirs) {
    const Item* it = item_by(c, id);
    if (it == nullptr) continue;
    const long long p = item_px(*it);
    v.gives += hold_of(c, it->v) * static_cast<double>(p);
    v.theirsMarket = xrcoin::add(v.theirsMarket, p);
  }
  v.gets += static_cast<double>(o.myCoins);
  v.gives += static_cast<double>(o.theirCoins);
  v.mineMarket = xrcoin::add(v.mineMarket, o.myCoins);
  v.theirsMarket = xrcoin::add(v.theirsMarket, o.theirCoins);
  return v;
}

// ============================================================================================== your copies (free / listed / held)
std::vector<Holding> fresh_holdings() {
  std::vector<Holding> h;
  if (g_src.holdings != nullptr) g_src.holdings(&h);
  return h;
}
// every copy held in an open counter (all collectors; `except` one's left out)
std::vector<Variant> held_copies(int except = -1) {
  std::vector<Variant> v;
  for (int c = 0; c < kCollectors; ++c)
    if (c != except && g_st.col[c].hasCounter)
      for (const Variant& m : g_st.col[c].counter.mine) v.push_back(m);
  return v;
}
void tell_held() { xrmarket::set_held_out(held_copies()); }
// copies of v's kind you could put in a trade now: held (not at grading) - listed - held in a counter (not `except`'s)
int free_count(const std::vector<Holding>& hold, const std::vector<Variant>& listed, const Variant& v, int except) {
  const std::string s = sig(v);
  int n = 0;
  for (const Holding& h : hold)
    if (!h.grading && sig(h) == s) n += h.count;
  for (const Variant& l : listed) n -= sig(l) == s ? 1 : 0;
  for (const Variant& r : held_copies(except)) n -= sig(r) == s ? 1 : 0;
  return n;
}
// what a copy of his would do in YOUR binder: a GOLD number / a 1/1 you have already can't come in
bool can_receive(const std::vector<Holding>& hold, const Variant& v) {
  for (const Holding& h : hold) {
    if (h.card != v.card) continue;
    if (v.parallel == kBlack && h.parallel == kBlack) return false;
    if (v.parallel == kGold && h.parallel == kGold && h.serial == v.serial && v.grade == 0 && h.grade == 0) return false;
    if (exq(v.card) && (h.serial == v.serial || (exq_t(v) == xrexq::kMasterpiece && exq_t(h) == xrexq::kMasterpiece))) return false;  // r230
    if (v.grade > 0 && h.grade > 0 && h.cert == v.cert) return false;
  }
  return true;
}

// ============================================================================================== the collector world's 1/1s
// r235: every copy in the collector world - the twenty binders + THE DEALER'S CASE (its copies are taken too: one of each)
template <typename F>
void each_world(F f) {
  for (int c = 0; c < kCollectors; ++c)
    for (const Item& it : g_st.col[c].items) f(c, it);
  for (const Item& it : g_st.deal) f(kDealer, it);
}
std::vector<std::string> black_keys() {
  std::vector<std::string> k;
  each_world([&](int, const Item& it) {
    if (it.v.parallel == kBlack && card_ok(it.v.card)) k.push_back(g_set[static_cast<size_t>(it.v.card)].key);
    else if (exq_t(it.v) > 0) k.push_back(g_set[static_cast<size_t>(it.v.card)].key + "#" + std::to_string(it.v.serial));  // r230: its numbered copies
  });
  std::sort(k.begin(), k.end());
  return k;
}
bool black_held(int card) {
  bool held = false;
  each_world([&](int, const Item& it) { held = held || (it.v.parallel == kBlack && it.v.card == card); });
  return held;
}
// r235: the pack roller told at once (memory only: a collector's rip must see the 1/1 / the number the one before him pulled); the file
// is written by tell_world
void tell_world_now() {
  if (g_st.career != 0 && g_src.world_blacks != nullptr) g_src.world_blacks(g_st.career, black_keys());
}
void tell_world() {  // (the pack roller's list: told + written beside the binder, read at a career's load)
  if (g_st.career == 0) return;
  const std::vector<std::string> k = black_keys();
  const std::string path = world_path(g_st.career), tmp = path + ".tmp";
  if (FILE* f = std::fopen(tmp.c_str(), "w")) {
    std::fprintf(f, "world11 1\n");
    for (const std::string& s : k) std::fprintf(f, "%s\n", s.c_str());
    std::fclose(f);
    std::rename(tmp.c_str(), path.c_str());
  }
  if (g_src.world_blacks != nullptr) g_src.world_blacks(g_st.career, k);
}

// one 1/1 per card ever: a collector's 1/1 that is somehow yours too (a pack pulled it before his binder was read) leaves his binder
bool reconcile_blacks() {
  if (g_src.black_mine == nullptr) return false;
  bool any = false;
  for (int c = 0; c < kCollectors; ++c) {
    std::vector<Item>& items = g_st.col[c].items;
    for (size_t i = items.size(); i-- > 0;)
      if (items[i].v.parallel == kBlack && g_src.black_mine(items[i].v.card)) {
        logl("COLLECTOR TRADES: the " + cname(items[i].v.card) + " 1/1 is yours - it leaves " + kP[c].handle + "'s binder");
        items.erase(items.begin() + static_cast<long>(i));
        any = true;
      }
  }
  for (size_t i = g_st.deal.size(); i-- > 0;)  // r235: THE DEALER'S CASE the same
    if (g_st.deal[i].v.parallel == kBlack && g_src.black_mine(g_st.deal[i].v.card)) {
      g_st.deal.erase(g_st.deal.begin() + static_cast<long>(i));
      any = true;
    }
  if (any) tell_world(), g_indexDirty = true;
  return any;
}

// ============================================================================================== their binders (6)
void sort_items(Col& k) {
  std::stable_sort(k.items.begin(), k.items.end(), [](const Item& a, const Item& b) { return item_px(a) > item_px(b); });
}
double exq_weight(int c) {
  switch (c) {  // (r235: VICTORIA EXQUISITE most, the WHALE / MILO some, NOAH / DEE none)
    case kGrailHunter: return 0.5;
    case kGradeSnob: return 1.0 / 3.0;
    case kExqFan: return 20.0;
    case kWhale: return 1.0;
    case kSerialHunter: return 0.5;
    case kSetChaser: case kJamFan: return 0.0;
    default: return 0.125;
  }
}
// r230: a number of an EXQUISITE run nobody has (you - binder, slab, a pack waiting -, any collector): this collector's copy
bool exq_taken_t(const std::vector<Holding>& hold, int card, int serial) {
  if (g_src.serial_mine != nullptr && g_src.serial_mine(card, serial)) return true;
  for (const Holding& h : hold)
    if (h.card == card && h.serial == serial) return true;
  bool t = false;
  each_world([&](int, const Item& it) { t = t || (it.v.card == card && it.v.serial == serial); });  // (r235: + the dealer's case)
  return t;
}
bool gold_taken(const std::vector<Holding>& hold, int card, int serial) {
  for (const Holding& h : hold)
    if (h.card == card && h.parallel == kGold && h.serial == serial) return true;
  bool t = false;
  each_world([&](int, const Item& it) { t = t || (it.v.card == card && it.v.parallel == kGold && it.v.serial == serial); });  // (r235: + the case)
  return t;
}
bool pool_has(int c, int card, uint64_t h) {
  if (!card_ok(card) || !g_set[static_cast<size_t>(card)].here) return false;
  const int s = series(card), t = tier(card);
  switch (c) {
    case kGrailHunter: return t >= 4;
    case kPokeOnly: return s == kSerPokemon;
    case kMarvelFan: return s == kSerMarvel || u01(h ^ 0x31) < 0.08;
    case kSetBuilder: return in_set(c, card) || (t >= 2 && u01(h ^ 0x32) < 0.15);
    case kFlipper: return t >= 2;
    case kNostalgia: return s == kSerStrikers || s == kSerCharged || (s == kSerMelee && u01(h ^ 0x33) < 0.5);
    case kGradeSnob: return t >= 2;
    case kHypeChaser: return t >= 2;
    // r235: the eleven
    case kSetChaser: return !jam(card) && !exq(card);
    case kJamFan: return jam(card) || u01(h ^ 0x34) < 0.1;
    case kExqFan: return exq(card) || u01(h ^ 0x35) < 0.05;
    case kMeleeFan: return s == kSerMelee;
    case kLoyalist: return (club(card) >= 0 && club(card) == sal_club()) || u01(h ^ 0x36) < 0.05;
    case kStatsNerd: return t >= 3;
    case kRookieSpec: return s == kSerOriginals;
    case kWhale: return t >= 3;
  }
  return true;  // (GUS, ELLIS, MILO: anything)
}
// r235: an EXQUISITE copy of a LOW tier (GALLERY /399 or SILVER LEAF /299) with a number nobody has; false none free
bool exq_low_number(Variant* v, uint64_t h, const std::vector<Holding>& hold) {
  const int t = v->parallel == kSilver ? xrexq::kSilverLeaf : xrexq::kGallery;
  for (int tt = t; tt > 0; tt = xrexq::down(tt)) {
    const int run = xrexq::kRun[tt], start = static_cast<int>(mix64(h ^ 0xE8) % static_cast<uint64_t>(run));
    for (int k = 0; k < run; ++k) {
      const int s = xrexq::serial_of(tt, 1 + (start + k) % run);
      if (exq_taken_t(hold, v->card, s)) continue;
      v->serial = s;
      v->parallel = xrexq::kClass[tt];
      v->hit = kHitNone;
      v->shiny = false;
      return true;
    }
  }
  return false;
}
// r235 (Lance 11-12: "not just give them good ass chase cards off the rip"): a copy for collector c's STARTING binder (or a thin binder's
// top-up) - its card from his pool, a MODEST finish: BASE, now and then a SILVER (an EXQUISITE card: a GALLERY /399 or a SILVER LEAF /299), a
// slab of one for the two graders. Never a GOLD /10, a 1/1, a case hit, a shiny: those come out of the packs they rip (hobby_day) or the
// singles they buy. (r213-r234: GOLDs, case hits, slabs of 10, shinies and 1-4 BLACK 1/1s each, hand-placed)
bool roll_copy(int c, uint64_t h, const std::vector<Holding>& hold, Variant* out) {
  std::vector<int> pool;
  std::vector<double> w;
  double tot = 0;
  for (size_t i = 0; i < g_set.size(); ++i) {
    if (!pool_has(c, static_cast<int>(i), h ^ (static_cast<uint64_t>(i) * 0x9E37ull))) continue;
    double wt = 1.0;
    if (c == kHypeChaser) wt = std::max(0.05, 1.0 + 0.08 * chg(static_cast<int>(i)));
    if (jam(static_cast<int>(i))) wt *= jam_weight(c);  // r217: some Jam cards in every binder (VINNY's half of his)
    if (exq(static_cast<int>(i))) wt *= exq_weight(c);  // r230: EXQUISITE cards
    if (wt <= 0.0) continue;
    if (c == kSetBuilder && std::find(g_st.col[c].wants.begin(), g_st.col[c].wants.end(), static_cast<int>(i)) != g_st.col[c].wants.end())
      continue;  // (his missing ones are missing)
    pool.push_back(static_cast<int>(i));
    w.push_back(wt);
    tot += wt;
  }
  if (pool.empty()) return false;
  double u = u01(h ^ 1) * tot;
  int card = pool.back();
  for (size_t i = 0; i < pool.size(); ++i)
    if ((u -= w[i]) <= 0) {
      card = pool[i];
      break;
    }
  Variant v;
  v.card = card;
  static const double kSilverStart[kCollectors] = {0.20, 0.10, 0.12, 0.12, 0.10, 0.15, 0.12, 0.20, 0.08, 0.08,
                                                   0.12, 0.25, 0.10, 0.12, 0.15, 0.05, 0.12, 0.10, 0.20, 0.25};
  if (u01(h ^ 2) < kSilverStart[c]) v.parallel = kSilver;
  if ((c == kGradeSnob || c == kGrader) && u01(h ^ 7) < (c == kGradeSnob ? 1.0 : 0.4)) {  // (their slabs: BASE / SILVER, graded 6-9)
    const double g = u01(h ^ 8);
    v.grade = g < 0.15 ? 6 : (g < 0.45 ? 7 : (g < 0.85 ? 8 : 9));
    v.cert = 40000000u + static_cast<uint32_t>(mix64(h ^ 9) % 9000000u);
  }
  if (exq(card) && !exq_low_number(&v, h, hold)) return false;
  *out = v;
  return true;
}
Item& add_item(int c, const Variant& v, int origin = kFromSeed, long long basis = 0) {
  Item it;
  it.id = g_st.nextId++;
  it.v = v;
  it.origin = origin;
  it.basis = std::max(0LL, basis);
  it.since = g_st.lastDay;
  g_st.col[c].items.push_back(it);
  return g_st.col[c].items.back();
}
void pick_wants(int c, uint64_t h) {
  Col& k = g_st.col[c];
  k.wants.clear();
  k.wantDone.clear();
  std::vector<int> set;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].here && in_set(c, static_cast<int>(i))) set.push_back(static_cast<int>(i));
  for (int n = 0; n < kWants && !set.empty(); ++n) {
    const size_t at = static_cast<size_t>(mix64(h ^ (0x57ull + n)) % set.size());
    int want = set[at];
    // r217: two of his six wants the JAM '96 card of it (its main card stays in his set)
    if (n >= kWants - 2 && card_ok(want) && g_set[static_cast<size_t>(want)].base >= 0) {
      for (size_t j = 0; j < g_set.size(); ++j)
        if (g_set[j].set == 1 && g_set[j].base == want && g_set[j].here) want = static_cast<int>(j);
    }
    k.wants.push_back(want);
    k.wantDone.push_back(0);
    set.erase(set.begin() + static_cast<long>(at));
  }
}
// a binder filled up to its size (a fresh one, or a thin one topped up): deterministic from the career, the collector, the season - r235:
// modest copies only (roll_copy), no 1/1s first (r213-r234 placed 1-4 each)
void fill_binder(int c, uint64_t h, const std::vector<Holding>& hold) {
  Col& k = g_st.col[c];
  if (c == kSetBuilder) {  // (his set but the missing six, once each)
    for (size_t i = 0; i < g_set.size() && static_cast<int>(k.items.size()) < kP[c].size; ++i) {
      const int card = static_cast<int>(i);
      if (!g_set[i].here || !in_set(c, card) || std::find(k.wants.begin(), k.wants.end(), card) != k.wants.end()) continue;
      bool have = false;
      for (const Item& it : k.items) have = have || it.v.card == card;
      if (have) continue;
      Variant v;
      v.card = card;
      if (u01(h ^ (0x5E7ull + i)) < 0.15) v.parallel = kSilver;
      add_item(c, v);
    }
  }
  for (int n = 0; static_cast<int>(k.items.size()) < kP[c].size && n < 400; ++n) {
    Variant v;
    if (roll_copy(c, mix64(h + static_cast<uint64_t>(n) * 0x9E3779B97F4A7C15ull), hold, &v)) add_item(c, v);
  }
  for (const Item& it : k.items) item_px(it);
  sort_items(k);
}
uint64_t col_hash(int c, int season) {
  return mix64(static_cast<uint64_t>(g_st.career) * 0xD1B54A32D192ED03ull ^ (static_cast<uint64_t>(c + 1) << 40) ^ static_cast<uint64_t>(season + 7) * 0x9E3779B97F4A7C15ull);
}
void gen_one(int c, int season, const std::vector<Holding>& hold) {
  Col& k = g_st.col[c];
  k = Col();
  k.wallet = kP[c].wallet0;
  k.patience = kP[c].patience;
  k.goodwill = c == kNostalgia ? 0.2 : 0.0;
  k.setIdx = std::max(0, season) % 3;
  if (c == kSetBuilder) pick_wants(c, col_hash(c, season));
  fill_binder(c, col_hash(c, season), hold);
}
void generate(int season) {
  const std::vector<Holding> hold = fresh_holdings();
  for (int c = 0; c < kCollectors; ++c) gen_one(c, season, hold);
}
// a new season: wallets grow 15%, a new set for the SET BUILDER - r235: no binder turnover any more (r213-r234 swapped ~30% for fresh
// rolled copies, GOLDs and hits among them): their packs, buys and sales move their binders now; a thin binder (half its start) topped up
// with modest copies
void restock(int season) {
  const std::vector<Holding> hold = fresh_holdings();
  for (int c = 0; c < kCollectors; ++c) {
    Col& k = g_st.col[c];
    const uint64_t h = col_hash(c, season);
    k.wallet = xrcoin::clamp(xrcoin::add(k.wallet, xrcoin::from_double(static_cast<double>(kP[c].wallet0) * 0.15)));
    if (c == kSetBuilder) {
      k.setIdx = std::max(0, season) % 3;
      pick_wants(c, h);
    }
    if (static_cast<int>(k.items.size()) < kP[c].size / 2) fill_binder(c, h, hold);
  }
  tell_world();  // (r230: their EXQUISITE numbers changed - the pack roller's list)
}
// ============================================================================================== the file
std::string vtext(const Variant& v) {
  char b[200];
  std::snprintf(b, sizeof(b), "%s|%d|%d|%d|%d|%d|%u", card_ok(v.card) ? g_set[static_cast<size_t>(v.card)].key.c_str() : "-", v.parallel, v.shiny ? 1 : 0, v.hit,
                v.serial, v.grade, v.cert);
  return b;
}
std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  size_t a = 0;
  for (;;) {
    const size_t b = s.find(sep, a);
    out.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
    if (b == std::string::npos) break;
    a = b + 1;
  }
  return out;
}
bool vparse(const std::string& s, Variant* v) {
  const std::vector<std::string> t = split(s, '|');
  if (t.size() < 7) return false;
  auto it = g_byKey.find(t[0]);
  if (it == g_byKey.end()) return false;
  v->card = it->second;
  v->parallel = std::max(0, std::min(3, std::atoi(t[1].c_str())));
  v->shiny = std::atoi(t[2].c_str()) != 0;
  v->hit = std::max(0, std::min(3, std::atoi(t[3].c_str())));
  v->serial = std::atoi(t[4].c_str());
  v->grade = std::max(0, std::min(10, std::atoi(t[5].c_str())));
  v->cert = static_cast<uint32_t>(std::strtoul(t[6].c_str(), nullptr, 10));
  return true;
}
std::string vlist(const std::vector<Variant>& vs) {
  std::string s;
  for (const Variant& v : vs) s += (s.empty() ? "" : ",") + vtext(v);
  return s.empty() ? "-" : s;
}
std::vector<Variant> vlist_parse(const std::string& s) {
  std::vector<Variant> out;
  if (s == "-" || s.empty()) return out;
  for (const std::string& p : split(s, ',')) {
    Variant v;
    if (vparse(p, &v)) out.push_back(v);
  }
  return out;
}
std::string idlist(const std::vector<uint32_t>& ids) {
  std::string s;
  for (uint32_t i : ids) s += (s.empty() ? "" : ",") + std::to_string(i);
  return s.empty() ? "-" : s;
}
std::vector<uint32_t> idlist_parse(const std::string& s) {
  std::vector<uint32_t> out;
  if (s == "-" || s.empty()) return out;
  for (const std::string& p : split(s, ',')) out.push_back(static_cast<uint32_t>(std::strtoul(p.c_str(), nullptr, 10)));
  return out;
}
std::string oneline(std::string s) {
  for (char& c : s)
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  return s;
}
void save() {  // (g_m held)
  if (g_st.career == 0 || !g_st.loaded) return;
  const std::string path = trades_path(g_st.career), tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) return;
  std::fprintf(f, "trades213 1\n");
  std::fprintf(f, "V\t235\n");  // r235: the twenty + their hobby (an older file: re-balanced once at its load - rebalance235)
  std::fprintf(f, "S\t%d\t%d\t%u\n", g_st.lastDay, g_st.season, g_st.nextId);
  {
    const HobbyStats& s = g_st.hob;
    std::fprintf(f, "Z\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\t%lld\n", s.packs[0], s.packs[1], s.packs[2], s.cards, s.blacks, s.golds,
                 s.hits, s.exqBig, s.buys, s.buyCoins, s.fromYou, s.bulked);
  }
  for (const Item& it : g_st.deal)  // r235: THE DEALER'S CASE
    std::fprintf(f, "E\t%u\t%s\t%d\t%lld\t%d\t%lld\t%d\n", it.id, vtext(it.v).c_str(), it.origin, it.basis, it.since, it.ask, it.listed);
  for (int c = 0; c < kCollectors; ++c) {
    const Col& k = g_st.col[c];
    std::string wants, done;
    for (size_t i = 0; i < k.wants.size(); ++i) {
      wants += (i ? "," : "") + (card_ok(k.wants[i]) ? g_set[static_cast<size_t>(k.wants[i])].key : std::string("-"));
      done += (i ? "," : "") + std::to_string(i < k.wantDone.size() ? k.wantDone[i] : 0);
    }
    std::fprintf(f, "C\t%d\t%.5f\t%lld\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%d\t%s\n", c, k.goodwill, k.wallet, k.patience, k.lowballs, k.trades, k.firstToday ? 1 : 0,
                 k.setIdx, wants.empty() ? "-" : wants.c_str(), done.empty() ? "-" : done.c_str(), k.sayKind, k.say.empty() ? "-" : oneline(k.say).c_str());
    for (const Item& it : k.items)  // (r235: + where it came from, what he paid, when, his asking price: an r213-r234 build reads the first 4)
      std::fprintf(f, "B\t%d\t%u\t%s\t%d\t%lld\t%d\t%lld\t%d\n", c, it.id, vtext(it.v).c_str(), it.origin, it.basis, it.since, it.ask, it.listed);
    if (k.hasCounter)
      std::fprintf(f, "K\t%d\t%lld\t%lld\t%s\t%s\n", c, k.counter.myCoins, k.counter.theirCoins, idlist(k.counter.theirs).c_str(), vlist(k.counter.mine).c_str());
  }
  for (const Incoming& i : g_st.inc)
    std::fprintf(f, "I\t%u\t%d\t%d\t%d\t%d\t%s\t%lld\t%s\n", i.id, i.col, i.day, i.expires, i.final ? 1 : 0, vtext(i.want).c_str(), i.give.theirCoins,
                 idlist(i.give.theirs).c_str());
  for (auto it = g_st.hist.rbegin(); it != g_st.hist.rend(); ++it)
    std::fprintf(f, "H\t%d\t%d\t%lld\t%lld\t%lld\t%lld\t%s\t%s\t%s\n", it->day, it->col, it->gaveCoins, it->gotCoins, it->gaveMarket, it->gotMarket,
                 vlist(it->gave).c_str(), vlist(it->got).c_str(), it->headline.empty() ? "-" : oneline(it->headline).c_str());
  for (auto it = g_st.wire.rbegin(); it != g_st.wire.rend(); ++it) std::fprintf(f, "N\t%d\t%u\t%s\n", it->day, it->rgb, oneline(it->text).c_str());
  std::fclose(f);
  std::rename(tmp.c_str(), path.c_str());
}
bool load(unsigned career) {  // (g_m held) false: no file (a career the collectors haven't met yet)
  g_st = State();
  g_st.career = career;
  g_px.clear();
  FILE* f = std::fopen(trades_path(career).c_str(), "r");
  if (f == nullptr) return false;
  std::string all;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
  std::fclose(f);
  if (all.compare(0, 9, "trades213") != 0) return false;
  for (const std::string& line : split(all, '\n')) {
    const std::vector<std::string> t = split(line, '\t');
    if (t.empty()) continue;
    if (t[0] == "S" && t.size() >= 4) {
      g_st.lastDay = std::atoi(t[1].c_str());
      g_st.season = std::atoi(t[2].c_str());
      g_st.nextId = static_cast<uint32_t>(std::strtoul(t[3].c_str(), nullptr, 10));
    } else if (t[0] == "C" && t.size() >= 13) {
      const int c = std::atoi(t[1].c_str());
      if (c < 0 || c >= kCollectors) continue;
      Col& k = g_st.col[c];
      k.goodwill = clampd(std::atof(t[2].c_str()), -1.0, 1.0);
      if (!std::isfinite(k.goodwill)) k.goodwill = 0.0;
      xrcoin::parse(t[3].c_str(), &k.wallet);
      k.wallet = xrcoin::clamp(k.wallet);
      k.patience = std::atoi(t[4].c_str());
      k.lowballs = std::atoi(t[5].c_str());
      k.trades = std::atoi(t[6].c_str());
      k.firstToday = std::atoi(t[7].c_str()) != 0;
      k.setIdx = std::max(0, std::min(2, std::atoi(t[8].c_str())));
      k.wants.clear();
      k.wantDone.clear();
      if (t[9] != "-")
        for (const std::string& w : split(t[9], ',')) {
          auto it = g_byKey.find(w);
          if (it != g_byKey.end()) k.wants.push_back(it->second);
        }
      if (t[10] != "-")
        for (const std::string& d : split(t[10], ',')) k.wantDone.push_back(std::atoi(d.c_str()));
      k.wantDone.resize(k.wants.size(), 0);
      k.sayKind = std::atoi(t[11].c_str());
      k.say = t[12] == "-" ? std::string() : t[12];
    } else if (t[0] == "B" && t.size() >= 4) {
      const int c = std::atoi(t[1].c_str());
      Item it;
      it.id = static_cast<uint32_t>(std::strtoul(t[2].c_str(), nullptr, 10));
      if (c < 0 || c >= kCollectors || !vparse(t[3], &it.v) || g_st.col[c].items.size() >= static_cast<size_t>(kMaxBinder) + 40) continue;
      if (t.size() >= 9) {  // r235
        it.origin = std::max(0, std::min(4, std::atoi(t[4].c_str())));
        xrcoin::parse(t[5].c_str(), &it.basis);
        it.since = std::atoi(t[6].c_str());
        xrcoin::parse(t[7].c_str(), &it.ask);
        it.listed = std::atoi(t[8].c_str());
        if (it.ask <= 0) it.ask = 0, it.listed = -1;
      }
      g_st.col[c].items.push_back(it);
    } else if (t[0] == "E" && t.size() >= 8) {  // r235: THE DEALER'S CASE
      Item it;
      it.id = static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10));
      if (!vparse(t[2], &it.v) || g_st.deal.size() >= 160) continue;
      it.origin = std::max(0, std::min(4, std::atoi(t[3].c_str())));
      xrcoin::parse(t[4].c_str(), &it.basis);
      it.since = std::atoi(t[5].c_str());
      xrcoin::parse(t[6].c_str(), &it.ask);
      it.listed = std::atoi(t[7].c_str());
      g_st.deal.push_back(it);
    } else if (t[0] == "V" && t.size() >= 2) {  // r235
      g_st.ver = std::atoi(t[1].c_str());
    } else if (t[0] == "Z" && t.size() >= 13) {  // r235
      HobbyStats& s = g_st.hob;
      long long* f[12] = {&s.packs[0], &s.packs[1], &s.packs[2], &s.cards, &s.blacks, &s.golds, &s.hits, &s.exqBig, &s.buys, &s.buyCoins, &s.fromYou, &s.bulked};
      for (int k = 0; k < 12; ++k) *f[k] = std::atoll(t[static_cast<size_t>(k + 1)].c_str());
    } else if (t[0] == "K" && t.size() >= 6) {
      const int c = std::atoi(t[1].c_str());
      if (c < 0 || c >= kCollectors) continue;
      Col& k = g_st.col[c];
      k.hasCounter = true;
      xrcoin::parse(t[2].c_str(), &k.counter.myCoins);
      xrcoin::parse(t[3].c_str(), &k.counter.theirCoins);
      k.counter.theirs = idlist_parse(t[4]);
      k.counter.mine = vlist_parse(t[5]);
    } else if (t[0] == "I" && t.size() >= 9) {
      Incoming i;
      i.id = static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10));
      i.col = std::atoi(t[2].c_str());
      i.day = std::atoi(t[3].c_str());
      i.expires = std::atoi(t[4].c_str());
      i.final = std::atoi(t[5].c_str()) != 0;
      if (i.col < 0 || i.col >= kCollectors || !vparse(t[6], &i.want)) continue;
      xrcoin::parse(t[7].c_str(), &i.give.theirCoins);
      i.give.theirs = idlist_parse(t[8]);
      if (static_cast<int>(g_st.inc.size()) < kMaxIncoming) g_st.inc.push_back(i);
    } else if (t[0] == "H" && t.size() >= 10) {
      Trade tr;
      tr.day = std::atoi(t[1].c_str());
      tr.col = std::max(0, std::min(kCollectors - 1, std::atoi(t[2].c_str())));
      xrcoin::parse(t[3].c_str(), &tr.gaveCoins);
      xrcoin::parse(t[4].c_str(), &tr.gotCoins);
      xrcoin::parse(t[5].c_str(), &tr.gaveMarket);
      xrcoin::parse(t[6].c_str(), &tr.gotMarket);
      tr.gave = vlist_parse(t[7]);
      tr.got = vlist_parse(t[8]);
      tr.headline = t[9] == "-" ? std::string() : t[9];
      g_st.hist.insert(g_st.hist.begin(), tr);
      if (g_st.hist.size() > static_cast<size_t>(kMaxHistory)) g_st.hist.resize(kMaxHistory);
    } else if (t[0] == "N" && t.size() >= 4) {
      Wire w;
      w.day = std::atoi(t[1].c_str());
      w.rgb = static_cast<uint32_t>(std::strtoul(t[2].c_str(), nullptr, 10));
      w.text = t[3];
      g_st.wire.insert(g_st.wire.begin(), w);
      if (g_st.wire.size() > static_cast<size_t>(kMaxWire)) g_st.wire.resize(kMaxWire);
    }
  }
  g_st.loaded = true;
  for (int c = 0; c < kCollectors; ++c) {
    for (const Item& it : g_st.col[c].items) item_px(it);
    sort_items(g_st.col[c]);
  }
  return true;
}

// ============================================================================================== headlines, rapport
void wire(const std::string& t, uint32_t rgb, bool toLeague) {
  Wire w;
  w.day = g_st.lastDay;
  w.rgb = rgb;
  w.text = t;
  g_st.wire.insert(g_st.wire.begin(), w);
  if (g_st.wire.size() > static_cast<size_t>(kMaxWire)) g_st.wire.resize(kMaxWire);
  if (toLeague) {
    const std::string line = "STRIKERZ COLLECTORS: " + t;  // (r217: STRIKERZ - the cards stay LANCINI TIZM)
    if (g_src.news != nullptr) g_src.news(line.c_str());
    else PortFranchiseBookNews(line.c_str());
  }
}
void rapport(int c, double d) {
  Col& k = g_st.col[c];
  k.goodwill = clampd(k.goodwill + (d > 0 ? d * kP[c].rapportK : d), -1.0, 1.0);
}
void speak(int c, int kind, uint64_t salt) {
  Col& k = g_st.col[c];
  k.sayKind = kind;
  k.say = kP[c].lines[std::max(0, std::min(6, kind))][mix64(salt ^ (static_cast<uint64_t>(c) << 20) ^ static_cast<uint64_t>(g_st.lastDay)) & 1];
  k.sayAt = g_now;
}
void speak_text(int c, int kind, const std::string& t) {
  Col& k = g_st.col[c];
  k.sayKind = kind;
  k.say = t;
  k.sayAt = g_now;
}
const char* mood_word(double m) { return m > 0.35 ? "THRILLED" : (m > 0.1 ? "UPBEAT" : (m < -0.35 ? "GRUMPY" : (m < -0.1 ? "WARY" : "STEADY"))); }
const char* rapport_word(double g) {
  return g > 0.45 ? "FRIENDLY" : (g > 0.15 ? "WARM" : (g < -0.5 ? "STINGY WITH YOU" : (g < -0.15 ? "COLD" : "NEUTRAL")));
}

// ============================================================================================== a trade, done (cards + coins both ways, once)
bool execute(int c, const Offer& o, double ratio, const char* how) {
  Col& k = g_st.col[c];
  if (g_st.career == 0 || g_src.give == nullptr || g_src.take == nullptr || g_src.spend == nullptr || g_src.add == nullptr) return false;
  const std::vector<Holding> hold = fresh_holdings();
  const std::vector<Variant> listed = xrmarket::listed_copies();
  // every copy of yours free (two of a kind need two), every one of his still his, the coins there, nothing he gives you'd clash with
  std::map<std::string, int> needN;
  for (const Variant& m : o.mine) ++needN[sig(m)];
  for (const Variant& m : o.mine)
    if (free_count(hold, listed, m, c) < needN[sig(m)]) return false;
  std::vector<Item> theirs;
  for (uint32_t id : o.theirs) {
    const Item* it = item_by(c, id);
    if (it == nullptr) return false;
    for (const Item& t : theirs)
      if (t.id == id) return false;
    if (!can_receive(hold, it->v)) return false;
    theirs.push_back(*it);
  }
  if (o.myCoins < 0 || o.theirCoins < 0 || o.myCoins > balance() || o.theirCoins > k.wallet) return false;
  if (o.mine.empty() && theirs.empty()) return false;
  const unsigned career = g_st.career;
  // 1 your coins, 2 your cards out, 3 his cards in, 4 his coins - undone in reverse if a step fails
  if (o.myCoins > 0 && !g_src.spend(career, o.myCoins, "COLLECTOR TRADE")) return false;
  std::vector<Variant> taken, given;
  bool ok = true;
  for (const Variant& m : o.mine) {
    if (!g_src.take(m)) {
      ok = false;
      break;
    }
    taken.push_back(m);
  }
  for (size_t i = 0; ok && i < theirs.size(); ++i) {
    if (!g_src.give(theirs[i].v)) {
      ok = false;
      break;
    }
    given.push_back(theirs[i].v);
  }
  if (!ok) {
    for (const Variant& g : given) g_src.take(g);
    for (const Variant& t : taken) g_src.give(t);
    if (o.myCoins > 0) g_src.add(career, o.myCoins, "COLLECTOR TRADE REFUND");
    logl("COLLECTOR TRADE: a copy wouldn't move - undone, nothing changed");
    return false;
  }
  if (o.theirCoins > 0) g_src.add(career, o.theirCoins, "COLLECTOR TRADE");
  // his side: his copies out, yours in, the coins through his wallet
  long long gotMkt = 0, gaveMkt = 0;
  bool gotBlack = false, gaveBlack = false;
  for (const Item& it : theirs) {
    gotMkt = xrcoin::add(gotMkt, item_px(it));
    gotBlack = gotBlack || it.v.parallel == kBlack;
    for (size_t i = 0; i < k.items.size(); ++i)
      if (k.items[i].id == it.id) {
        k.items.erase(k.items.begin() + static_cast<long>(i));
        break;
      }
    g_px.erase(it.id);
  }
  for (const Variant& m : o.mine) {
    gaveMkt = xrcoin::add(gaveMkt, px_of(m));
    gaveBlack = gaveBlack || m.parallel == kBlack;
    // r235: it came from you - its basis what he credited it at (he never lists it under 105% of that: no sell-to-him, buy-it-back loop)
    if (static_cast<int>(k.items.size()) < kMaxBinder + 40 || m.parallel == kBlack) add_item(c, m, kFromYou, xrcoin::from_double(std::max(credit(c, m), static_cast<double>(px_of(m)))));
    g_indexDirty = true;
    if (c == kSetBuilder)
      for (size_t i = 0; i < k.wants.size(); ++i)
        if (k.wants[i] == m.card && i < k.wantDone.size() && k.wantDone[i] == 0) {
          k.wantDone[i] = 1;
          break;
        }
  }
  for (const Item& it : k.items) item_px(it);
  sort_items(k);
  k.wallet = xrcoin::clamp(xrcoin::add(xrcoin::add(k.wallet, o.myCoins), -o.theirCoins));
  ++k.trades;
  g_indexDirty = true;  // r235: (a listed copy of his may have gone)
  rapport(c, ratio >= 1.05 * need_of(c) ? 0.10 : 0.06);
  // the record + the headline
  Trade tr;
  tr.day = g_st.lastDay;
  tr.col = c;
  tr.gave = o.mine;
  for (const Item& it : theirs) tr.got.push_back(it.v);
  tr.gaveCoins = o.myCoins;
  tr.gotCoins = o.theirCoins;
  tr.gaveMarket = xrcoin::add(gaveMkt, o.myCoins);
  tr.gotMarket = xrcoin::add(gotMkt, o.theirCoins);
  auto top = [](const std::vector<Variant>& vs) {
    const Variant* b = nullptr;
    long long bp = -1;
    for (const Variant& v : vs) {
      const long long p = px_of(v);
      if (p > bp) bp = p, b = &v;
    }
    return b;
  };
  const Variant* myTop = top(tr.gave);
  const Variant* hisTop = top(tr.got);
  const std::string who = kP[c].handle;
  std::string h;
  if (hisTop != nullptr && hisTop->parallel == kBlack)
    h = "YOU LANDED THE " + cname(hisTop->card) + " 1/1 FROM " + who + " - FOR " + (myTop != nullptr ? "YOUR " + short_words(*myTop) : T(o.myCoins) + " COINS") +
        (myTop != nullptr && o.myCoins > 0 ? " + " + T(o.myCoins) + " COINS" : "");
  else if (myTop != nullptr && myTop->parallel == kBlack)
    h = who + " LANDS YOUR " + short_words(*myTop) + " - YOU GOT " + (hisTop != nullptr ? short_words(*hisTop) : T(o.theirCoins) + " COINS") +
        (hisTop != nullptr && o.theirCoins > 0 ? " + " + T(o.theirCoins) + " COINS" : "");
  else if (myTop != nullptr && hisTop != nullptr)
    h = "YOU FLIPPED " + short_words(*myTop) + (o.myCoins > 0 ? " + " + T(o.myCoins) + " COINS" : "") + " FOR " + who + "'S " + short_words(*hisTop) +
        (o.theirCoins > 0 ? " + " + T(o.theirCoins) + " COINS" : "");
  else if (myTop != nullptr)
    h = "YOU SOLD " + short_words(*myTop) + " TO " + who + " FOR " + T(o.theirCoins) + " COINS";
  else if (hisTop != nullptr)
    h = "YOU BOUGHT " + who + "'S " + short_words(*hisTop) + " FOR " + T(o.myCoins) + " COINS";
  tr.headline = h;
  g_st.hist.insert(g_st.hist.begin(), tr);
  if (g_st.hist.size() > static_cast<size_t>(kMaxHistory)) g_st.hist.resize(kMaxHistory);
  const bool big = gotBlack || gaveBlack || tr.gaveMarket >= 100000 || tr.gotMarket >= 100000;
  wire(h, gotBlack || gaveBlack ? 0xFFD23F : 0x39FF88, big);
  if (c == kSetBuilder && !k.wants.empty()) {
    bool all = true;
    for (int d : k.wantDone) all = all && d != 0;
    if (all) {
      wire(who + " COMPLETES " + kSetName[std::max(0, std::min(2, k.setIdx))] + " - THANKS TO YOU", 0x6CFF9A, true);
      rapport(c, 0.3);
      k.wants.clear();
      k.wantDone.clear();
    }
  }
  // an incoming offer for a copy that left (or one of his items that left) goes
  const std::vector<Holding> after = fresh_holdings();
  for (size_t i = 0; i < g_st.inc.size();) {
    const Incoming& in = g_st.inc[i];
    bool gone = free_count(after, listed, in.want, -1) <= 0;
    for (uint32_t id : in.give.theirs) gone = gone || item_by(in.col, id) == nullptr;
    if (gone) g_st.inc.erase(g_st.inc.begin() + static_cast<long>(i));
    else ++i;
  }
  if (gotBlack || gaveBlack) tell_world();
  char lb[400];
  std::snprintf(lb, sizeof(lb), "COLLECTOR TRADE (%s) with %s: gave %d cards + %lld coins (market %lld), got %d cards + %lld coins (market %lld)", how, who.c_str(),
                static_cast<int>(o.mine.size()), o.myCoins, tr.gaveMarket, static_cast<int>(theirs.size()), o.theirCoins, tr.gotMarket);
  logl(lb);
  g_flashAt = g_now;
  sound("book_collect", 0.9f);
  sound("book_coin", 0.7f);
  push_index();  // r235: (the search never shows a copy that just moved)
  ++g_gen;
  return true;
}

// ============================================================================================== your offer -> his answer
bool counter_build(int c, const Offer& o, double target, Offer* out, std::string* why) {
  Offer x = o;
  const long long bal = balance();
  for (int pass = 0; pass < 8; ++pass) {
    const Value v = value_of(c, x);
    double D = target * v.gives - v.gets;  // (coins short, in his eyes: never a fraction short - accepting it must clear his NEED)
    if (D <= 0.0) {
      if (x.theirs.empty() && x.theirCoins <= 0) break;  // (his side can't end up empty)
      *out = x;
      return true;
    }
    if (x.theirCoins > 0) {  // he trims his own coins first
      const long long cut = std::min<long long>(x.theirCoins, static_cast<long long>(std::ceil(D / target)) + 1);
      x.theirCoins -= cut;
      continue;
    }
    const long long add = nice_up(D + 1.0);
    if (bal - x.myCoins >= add) {  // then asks you to add coins
      x.myCoins += add;
      continue;
    }
    if (x.theirs.size() >= 2 || (!x.theirs.empty() && x.theirCoins > 0)) {  // else drops his cheapest card
      size_t lo = 0;
      long long lp = -1;
      for (size_t i = 0; i < x.theirs.size(); ++i) {
        const Item* it = item_by(c, x.theirs[i]);
        const long long p = it != nullptr ? item_px(*it) : 0;
        if (lp < 0 || p < lp) lp = p, lo = i;
      }
      x.theirs.erase(x.theirs.begin() + static_cast<long>(lo));
      continue;
    }
    *why = "YOU'D NEED " + T(add) + " MORE COINS";
    return false;
  }
  const Value v = value_of(c, x);
  if (v.ratio() >= target - 1e-9 && !(x.theirs.empty() && x.theirCoins <= 0)) {
    *out = x;
    return true;
  }
  *why = "NO DEAL HE COULD MAKE";
  return false;
}
Reply do_propose(int c, const Offer& in) {
  Reply r;
  if (c < 0 || c >= kCollectors || g_st.career == 0 || !g_st.loaded) {
    r.line = "LOAD A FRANCHISE CAREER";
    return r;
  }
  Col& k = g_st.col[c];
  if (in.mine.empty() && in.myCoins <= 0) {
    r.line = "PUT SOMETHING ON YOUR SIDE";
    return r;
  }
  if (in.theirs.empty() && in.theirCoins <= 0) {
    r.line = "PICK SOMETHING FROM THEIR SIDE (A CARD OR COINS)";
    return r;
  }
  if (in.mine.empty() && in.theirs.empty()) {
    r.line = "COINS FOR COINS? PUT A CARD IN";
    return r;
  }
  if (static_cast<int>(in.mine.size()) > kMaxSide || static_cast<int>(in.theirs.size()) > kMaxSide) {
    r.line = "5 CARDS A SIDE AT MOST";
    return r;
  }
  if (in.myCoins < 0 || in.theirCoins < 0) return r;
  if (k.hasCounter) {  // (a new offer replaces his counter: its copies free again)
    k.hasCounter = false;
    k.counter = Offer();
    tell_held();
  }
  if (in.myCoins > balance()) {
    r.line = "YOU HAVE ONLY " + T(balance()) + " COINS";
    return r;
  }
  const std::vector<Holding> hold = fresh_holdings();
  const std::vector<Variant> listed = xrmarket::listed_copies();
  std::map<std::string, int> needN;
  for (const Variant& m : in.mine) ++needN[sig(m)];
  for (const Variant& m : in.mine)
    if (free_count(hold, listed, m, c) < needN[sig(m)]) {
      r.line = short_words(m) + " ISN'T FREE (LISTED, AT GRADING, IN A TRADE OR GONE)";
      return r;
    }
  for (size_t i = 0; i < in.theirs.size(); ++i) {
    const Item* it = item_by(c, in.theirs[i]);
    if (it == nullptr) {
      r.line = "THAT CARD ISN'T IN THE BINDER ANY MORE";
      return r;
    }
    for (size_t j = 0; j < i; ++j)
      if (in.theirs[j] == in.theirs[i]) return r;
    if (!can_receive(hold, it->v)) {
      r.line = "YOU HAVE " + short_words(it->v) + " ALREADY (ITS NUMBER / THE 1/1)";
      return r;
    }
  }
  if (k.patience <= 0) {
    r.line = kPatienceOut;
    speak_text(c, kLDecline, kPatienceOut);
    return r;
  }
  const uint64_t salt = hstr(vlist(in.mine)) ^ static_cast<uint64_t>(in.myCoins) ^ (static_cast<uint64_t>(in.theirCoins) << 13) ^ static_cast<uint64_t>(k.patience);
  const Value v = value_of(c, in);
  r.ratio = v.ratio();
  const double need = need_of(c);
  if (v.notInto) {  // (a card he won't touch: POKEMON ONLY + a Marvel card)
    --k.patience;
    speak(c, kLNotInto, salt);
    r.kind = kDeclined;
    r.line = k.say;
    ++g_gen;
    return r;
  }
  if (in.theirCoins > k.wallet) {
    --k.patience;
    speak_text(c, kLDecline, kNoCash);
    r.kind = kDeclined;
    r.line = kNoCash;
    ++g_gen;
    return r;
  }
  const bool lowFirst = c == kLowballer && k.firstToday && r.ratio < 1.20;
  k.firstToday = false;
  if (r.ratio >= need && !lowFirst) {
    if (!execute(c, in, r.ratio, "your offer")) {
      r.line = "SOMETHING CHANGED - TRY AGAIN";
      return r;
    }
    speak(c, kLAccept, salt);
    r.kind = kAccepted;
    r.line = k.say;
    g_draft[c] = Offer();
    return r;
  }
  --k.patience;
  if (r.ratio >= kP[c].walk) {
    const double target = lowFirst ? std::max(need * 1.10, r.ratio * 1.06) : need;
    Offer co;
    std::string why;
    if (counter_build(c, in, target, &co, &why)) {
      k.hasCounter = true;
      k.counter = co;
      speak(c, kLCounter, salt);
      r.kind = kCountered;
      r.counter = co;
      r.line = k.say;
      tell_held();
      ++g_gen;
      return r;
    }
    speak_text(c, kLDecline, "CAN'T MAKE IT WORK - " + why + ".");
    r.kind = kDeclined;
    r.line = k.say;
    ++g_gen;
    return r;
  }
  if (r.ratio < 0.80) {  // a LOWBALL: he remembers
    ++k.lowballs;
    rapport(c, c == kLowballer ? -0.02 : (c == kFlipper ? -0.10 : -0.15));
    speak(c, kLLowball, salt);
  } else {
    speak(c, kLDecline, salt);
  }
  r.kind = kDeclined;
  r.line = k.say;
  ++g_gen;
  return r;
}
bool do_accept_counter(int c) {
  if (c < 0 || c >= kCollectors) return false;
  Col& k = g_st.col[c];
  if (!k.hasCounter) return false;
  const Offer o = k.counter;
  const Value v = value_of(c, o);
  if (v.ratio() < need_of(c) - 1e-9) {  // (his number moved: the market, his mood)
    k.hasCounter = false;
    tell_held();
    speak_text(c, kLDecline, "THE MARKET MOVED - THAT COUNTER'S OFF.");
    ++g_gen;
    return false;
  }
  k.hasCounter = false;  // (its copies freed first: the trade itself takes them)
  tell_held();
  if (!execute(c, o, v.ratio(), "his counter")) {
    k.hasCounter = true;
    k.counter = o;
    tell_held();
    return false;
  }
  speak(c, kLAccept, static_cast<uint64_t>(o.myCoins));
  g_draft[c] = Offer();
  return true;
}
bool do_decline_counter(int c) {
  if (c < 0 || c >= kCollectors || !g_st.col[c].hasCounter) return false;
  g_st.col[c].hasCounter = false;
  rapport(c, -0.03);
  tell_held();
  ++g_gen;
  return true;
}

// ============================================================================================== THEY COME TO YOU (5)
Incoming* inc_by(uint32_t id) {
  for (Incoming& i : g_st.inc)
    if (i.id == id) return &i;
  return nullptr;
}
// the most he'd give for `want` (his eyes: HOLD x market + coins) and what his side costs now
double inc_max(const Incoming& i) { return credit(i.col, i.want) / need_of(i.col); }  // (r235: credit - the flip rule)
double inc_cost(const Incoming& i) {
  double c = static_cast<double>(i.give.theirCoins);
  for (uint32_t id : i.give.theirs) {
    const Item* it = item_by(i.col, id);
    if (it != nullptr) c += hold_of(i.col, it->v) * static_cast<double>(item_px(*it));
  }
  return c;
}
// still worth it to him at today's prices? (else he trims his coins, or pulls it) - true: it stands
bool inc_recheck(Incoming& i) {
  for (uint32_t id : i.give.theirs)
    if (item_by(i.col, id) == nullptr) return false;
  const double mx = inc_max(i), cost = inc_cost(i);
  if (cost <= mx) return true;
  const long long cut = static_cast<long long>(std::ceil(cost - mx)) + 1;
  if (i.give.theirCoins >= cut) {
    i.give.theirCoins = nice_down(static_cast<double>(i.give.theirCoins - cut));
    return inc_cost(i) <= mx && (i.give.theirCoins > 0 || !i.give.theirs.empty());
  }
  return false;
}
void make_incoming(const std::vector<Holding>& hold, const std::vector<Variant>& listed) {
  if (static_cast<int>(g_st.inc.size()) >= kMaxIncoming) return;
  // your free copies worth 300+ (priced once here: a matchday)
  struct Mine {
    Variant v;
    long long p;
  };
  std::vector<Mine> mine;
  std::unordered_map<std::string, int> taken;  // (listed + held copies by kind: counted once, not per copy)
  for (const Variant& l : listed) ++taken[sig(l)];
  for (const Variant& r : held_copies()) ++taken[sig(r)];
  for (const Holding& h : hold) {
    if (h.grading) continue;
    auto t = taken.find(sig(h));
    if (t != taken.end() && h.count - t->second <= 0) continue;
    const long long p = px_of(h);
    if (p >= 300) mine.push_back({h, p});
  }
  if (mine.empty()) return;
  static const double kElig[kCollectors] = {1.10, 0.82, 1.05, 1.08, 1.10, 0.88, 1.10, 1.05, 1.0,
                                            1.06, 1.08, 1.10, 1.10, 1.10, 1.10, 9.0, 1.0, 1.10, 1.10, 1.08};  // (r235; GUS never chases: 9)
  for (int c = 0; c < kCollectors && static_cast<int>(g_st.inc.size()) < kMaxIncoming; ++c) {
    bool open = false;
    for (const Incoming& i : g_st.inc) open = open || i.col == c;
    if (open) continue;
    const uint64_t h = mix64(static_cast<uint64_t>(g_st.career) * 0x94D049BB133111EBull ^ (static_cast<uint64_t>(c) << 48) ^ static_cast<uint64_t>(g_st.lastDay + 1000) * 0x2545F4914F6CDD1Dull);
    const double chance = 0.12 * (1.0 + 0.5 * mood_of(c)) * (1.0 + 0.5 * std::max(0.0, g_st.col[c].goodwill));
    if (u01(h ^ 1) >= chance) continue;
    // the one he wants most (his taste x its value, a little luck)
    const Mine* best = nullptr;
    double bs = -1;
    for (const Mine& m : mine) {
      const double t = taste_of(c, m.v);
      if (t < kElig[c] - 1e-9 || (c == kLowballer && m.p < 2000) || (c == kFlipper && chg(m.v.card) < 5.0f)) continue;
      bool taken = false;
      for (const Incoming& i : g_st.inc) taken = taken || sig(i.want) == sig(m.v);
      if (taken) continue;
      const double s = t * std::sqrt(static_cast<double>(m.p)) * (0.5 + u01(h ^ hstr(sig(m.v))));
      if (s > bs) bs = s, best = &m;
    }
    if (best == nullptr) continue;
    Incoming in;
    in.id = g_st.nextId++;
    in.col = c;
    in.want = best->v;
    in.day = g_st.lastDay;
    in.expires = g_st.lastDay + kOfferDays;
    const double mx = credit(c, best->v) / need_of(c);  // (r235: the flip rule)
    const double X = mx * (c == kLowballer ? 0.70 + 0.12 * u01(h ^ 2) : 0.86 + 0.10 * u01(h ^ 2));
    double left = X;
    // 0-2 of his cards (he keeps his 1/1s unless it's a 1/1 / a million-coin card he's after; the SET BUILDER keeps his set)
    std::vector<std::pair<double, uint32_t>> cand;
    for (const Item& it : g_st.col[c].items) {
      if (it.v.parallel == kBlack && !(best->v.parallel == kBlack || best->p >= 1000000)) continue;
      if (c == kSetBuilder && in_set(c, it.v.card)) continue;
      if (!can_receive(hold, it.v)) continue;
      cand.push_back({hold_of(c, it.v) * static_cast<double>(item_px(it)), it.id});
    }
    std::sort(cand.begin(), cand.end(), [](const std::pair<double, uint32_t>& a, const std::pair<double, uint32_t>& b) { return a.first > b.first; });
    for (int pick = 0; pick < 2; ++pick) {
      if (u01(h ^ (3 + pick)) >= (pick == 0 ? 0.70 : 0.35)) break;
      std::vector<std::pair<double, uint32_t>> fit;
      for (const auto& cd : cand)
        if (cd.first <= 0.95 * left && cd.first >= (pick == 0 ? 0.20 : 0.15) * X &&
            std::find(in.give.theirs.begin(), in.give.theirs.end(), cd.second) == in.give.theirs.end())
          fit.push_back(cd);
      if (fit.empty()) break;
      const auto& got = fit[static_cast<size_t>(mix64(h ^ (5 + pick)) % std::min<size_t>(3, fit.size()))];
      in.give.theirs.push_back(got.second);
      left -= got.first;
    }
    in.give.theirCoins = std::min(g_st.col[c].wallet, nice_down(left));
    if (in.give.theirs.empty() && in.give.theirCoins < 1) continue;
    g_st.inc.push_back(in);
    speak(c, kLPitch, h);
    const long long p = best->p;
    if (best->v.parallel == kBlack || p >= 1000000)
      wire(std::string(kP[c].handle) + " WANTS YOUR " + short_words(best->v) + " - " + HEs(c, " MADE AN OFFER"), 0xFF2E88, false);
    sound("book_ding", 0.6f);
  }
}
bool do_accept_incoming(uint32_t id) {
  Incoming* i = inc_by(id);
  if (i == nullptr) return false;
  if (!inc_recheck(*i)) {
    const int c = i->col;
    for (size_t k = 0; k < g_st.inc.size(); ++k)
      if (g_st.inc[k].id == id) {
        g_st.inc.erase(g_st.inc.begin() + static_cast<long>(k));
        break;
      }
    speak_text(c, kLDecline, "PRICES MOVED - I PULLED THAT OFFER.");
    ++g_gen;
    return false;
  }
  const Incoming copy = *i;
  Offer o = copy.give;
  o.mine = {copy.want};
  const double ratio = inc_max(copy) * need_of(copy.col) / std::max(1.0, inc_cost(copy));
  if (!execute(copy.col, o, ratio, "his offer")) return false;
  rapport(copy.col, 0.02);
  for (size_t k = 0; k < g_st.inc.size(); ++k)
    if (g_st.inc[k].id == id) {
      g_st.inc.erase(g_st.inc.begin() + static_cast<long>(k));
      break;
    }
  speak(copy.col, kLAccept, id);
  return true;
}
int do_counter_incoming(uint32_t id, int pct) {
  Incoming* i = inc_by(id);
  if (i == nullptr || i->final) return 0;
  // he adds pct% of his side's market value in coins
  long long mkt = i->give.theirCoins;
  for (uint32_t tid : i->give.theirs) {
    const Item* it = item_by(i->col, tid);
    if (it != nullptr) mkt = xrcoin::add(mkt, item_px(*it));
  }
  const long long more = std::max(1LL, static_cast<long long>(std::floor(static_cast<double>(mkt) * pct / 100.0 + 0.5)));
  Incoming ask = *i;
  ask.give.theirCoins = xrcoin::add(ask.give.theirCoins, more);
  const double mx = inc_max(ask), cost = inc_cost(ask);
  const int c = i->col;
  if (cost <= mx && ask.give.theirCoins <= g_st.col[c].wallet) {
    *i = ask;
    return do_accept_incoming(id) ? 1 : 0;
  }
  if (cost <= mx * 1.06) {  // his FINAL OFFER: his coins up to his max
    const double base = cost - static_cast<double>(ask.give.theirCoins);
    i->give.theirCoins = std::min(g_st.col[c].wallet, std::max(i->give.theirCoins, nice_down(mx - base)));
    i->final = true;
    speak_text(c, kLCounter, "MY FINAL OFFER.");
    ++g_gen;
    return 2;
  }
  for (size_t k = 0; k < g_st.inc.size(); ++k)
    if (g_st.inc[k].id == id) {
      g_st.inc.erase(g_st.inc.begin() + static_cast<long>(k));
      break;
    }
  rapport(c, -0.05);
  speak_text(c, kLDecline, "TOO RICH FOR ME - I'M OUT.");
  ++g_gen;
  return 3;
}
bool do_decline_incoming(uint32_t id) {
  for (size_t k = 0; k < g_st.inc.size(); ++k)
    if (g_st.inc[k].id == id) {
      const int c = g_st.inc[k].col;
      g_st.inc.erase(g_st.inc.begin() + static_cast<long>(k));
      rapport(c, -0.02);
      ++g_gen;
      return true;
    }
  return false;
}

// ============================================================================================== a matchday (or k at once)
void refresh_prices() {
  g_px.clear();
  for (int c = 0; c < kCollectors; ++c) {
    for (const Item& it : g_st.col[c].items) item_px(it);
    sort_items(g_st.col[c]);
  }
  for (const Item& it : g_st.deal) item_px(it);  // r235
}
void refresh_moves() {
  for (size_t i = 0; i < g_set.size(); ++i) {
    Variant v;
    v.card = static_cast<int>(i);
    g_chg[i] = g_set[i].here ? xrmarket::change(v) : 0.0f;
    // r231: a card's demand band = its player's place in the league's TOP CARDS race by play this matchday (r213-r230: a fixed popularity
    // tier read once) - OSCAR's "grails" are the TOP 5 (the Golden Boot / MVP race's), PIPPA's too
    if (i < g_tier.size()) g_tier[i] = xrmarket::hype_tier(static_cast<int>(i));
    if (i < g_club.size()) g_club[i] = g_set[i].here ? xrmarket::club_of(static_cast<int>(i)) : -1;  // r235: SAL's club
    if (i < g_rank.size()) g_rank[i] = g_set[i].here ? xrmarket::demand_rank(static_cast<int>(i)) : 0;
  }
}

// ============================================================================================== r235: THE HOBBY (COLLECTOR-TRADES.md 11)
// (Lance 11-12: "if they're all ripping packs thats one thing ... you can have them start rich and be actively buying singles of cards and
// packs but not just give them good ass chase cards off the rip"). Each matchday every collector, deterministic from the career + the day:
//   RIPS his packs (kHob: his budget's worth of LANCINI TIZM / JAM '96 / EXQUISITE at the shop's prices) through the SHIPPING pack roller -
//     the same odds, one 1/1 per card per set ever (the world's list told after every rip), every EXQUISITE number once: a chase card in a
//     collector's hands is one he PULLED or BOUGHT, nothing else
//   GRADES (ELLIS: one raw card worth 1,000+ a matchday becomes a slab), BULKS OUT what he can't keep (past 72 copies: his cheapest
//     non-chase, unlisted copies to a dealer at SELL NOW - they leave the world)
//   LISTS cards for sale (doubles, off his taste, a flip in profit, a pulled chase card that isn't his thing) at HIS price: x the market
//     kHob lo .. hi, a share of them MOTIVATED (90-98%) - never under 90% of the market (over SELL NOW: no instant loop), never under 105%
//     of what he paid (no sell-to-him, buy-it-back loop); an unsold one cut 4% a matchday after two (not the stubborn), down after ten
//   BUYS singles (each other's listings, THE DEALER'S CASE) he values at the price (his taste x the market >= the ask; GUS only under 93%,
//     FRANKIE under 96% on a riser, the WHALE the priciest)
// Your listings: the market's collectors look each matchday (r210) - a named one who can afford it now buys it into his binder (world_sold)
bool chase_copy(const Variant& v) {  // a chase card: a pack's big moment (never in a starting binder)
  if (exq(v.card)) return exq_t(v) >= xrexq::kGoldLeaf;  // (GOLD LEAF /99 and up, every hit tier, the MASTERPIECE)
  return v.parallel >= kGold || v.hit != kHitNone || v.shiny || (v.grade >= 10 && v.parallel >= kSilver);
}
long long list_floor(const Item& it, long long px) {
  const double f = std::max(std::max(xrmarket::kListFloor * static_cast<double>(px), 1.05 * static_cast<double>(it.basis)),
                            static_cast<double>(xrmarket::bid(it.v)) + 1.0);
  return std::max(2LL, nice_up(f));
}
// "@ONEOFONE PULLED THE JAM '96 BLACK 1/1 TOGEPI!" - the copy as a pull reads
std::string pull_words(const Variant& v) {
  std::string nm = cname(v.card);
  if (nm.compare(0, 8, "JAM '96 ") == 0) nm = nm.substr(8);
  if (nm.compare(0, 10, "EXQUISITE ") == 0) nm = nm.substr(10);
  if (exq(v.card)) {
    const int t = exq_t(v);
    return t == xrexq::kMasterpiece ? "EXQUISITE MASTERPIECE 1/1 " + nm : "EXQUISITE " + std::string(xrexq::short_name(t)) + " " + xrexq::num_text(v.serial) + " " + nm;
  }
  std::string s = jam(v.card) ? "JAM '96 " : "";
  if (v.hit == kHitSigned) s += "SIGNED ";
  else if (v.hit == kHitBoom) s += "TIZM BOOM ";
  else if (v.hit == kHitHome) s += "HOMETOWN ";
  if (v.parallel == kBlack) s += "BLACK 1/1 ";
  else if (v.parallel == kGold) {
    char b[24];
    std::snprintf(b, sizeof(b), "GOLD %02d/10 ", v.serial);
    s += b;
  } else if (v.parallel == kSilver) s += "SILVER ";
  if (v.shiny) s += "SHINY ";
  return s + nm;
}
bool g_worldMoved = false;  // (a 1/1 / a number came into the world today: the list's file written at the day's end)
void hobby_rip(int c, int set, uint64_t seed, const std::vector<Holding>& hold) {
  Col& k = g_st.col[c];
  if (g_src.rip == nullptr || k.wallet < kPackCost[set]) return;
  std::vector<Variant> got;
  if (!g_src.rip(set, seed, &got) || got.empty()) return;
  k.wallet -= kPackCost[set];
  ++g_st.hob.packs[set];
  bool told = false;
  for (Variant v : got) {
    if (!card_ok(v.card)) continue;
    if (!exq(v.card) && v.parallel == kBlack && black_held(v.card)) v.parallel = kGold, v.serial = 1 + static_cast<int>(seed % 10);  // (belt + braces)
    if (!exq(v.card) && v.parallel == kGold && gold_taken(hold, v.card, v.serial)) {  // (a GOLD number the world has: another free one, else a SILVER)
      int s = 0;
      for (int n = 1; n <= 10 && s == 0; ++n) {
        const int cand = 1 + (v.serial - 1 + n) % 10;
        if (!gold_taken(hold, v.card, cand)) s = cand;
      }
      if (s == 0) v.parallel = kSilver, v.serial = 0;
      else v.serial = s;
    }
    Item& it = add_item(c, v, kFromPull, 0);
    item_px(it);
    ++g_st.hob.cards;
    const int et = exq_t(v);
    const bool one = v.parallel == kBlack;  // (an EXQUISITE MASTERPIECE reads as a BLACK 1/1)
    if (one) ++g_st.hob.blacks;
    else if (!exq(v.card) && v.parallel == kGold) ++g_st.hob.golds;
    if (!exq(v.card) && v.hit != kHitNone) ++g_st.hob.hits;
    if (et >= xrexq::kEmerald && et != xrexq::kAuto && et != xrexq::kPatch && et != xrexq::kRpa) ++g_st.hob.exqBig;
    told = told || one || et > 0;
    if (one) {  // the chase found: the league hears of it
      wire(std::string(kP[c].handle) + " PULLED THE " + pull_words(v) + "!", 0xFFD23F, true);
      logl("COLLECTOR HOBBY: " + std::string(kP[c].handle) + " pulled the " + pull_words(v));
    } else if ((!exq(v.card) && (v.parallel == kGold || v.hit != kHitNone)) || et == xrexq::kSapphire || et == xrexq::kPatchAuto) {
      wire(std::string(kP[c].handle) + " PULLED A " + pull_words(v), 0xFFB020, false);
    }
  }
  if (told) {
    tell_world_now();
    g_worldMoved = true;
  }
}
void hobby_grade(int c, int day) {  // ELLIS: one raw card worth 1,000+ a matchday sent in and back as a slab
  if (c != kGrader) return;
  Col& k = g_st.col[c];
  for (Item& it : k.items) {
    if (it.v.grade > 0 || it.ask > 0 || item_px(it) < 1000) continue;
    const double g = u01(mix64(static_cast<uint64_t>(it.id) * 0x9E37ull ^ static_cast<uint64_t>(day)));
    it.v.grade = g < 0.20 ? 7 : (g < 0.60 ? 8 : (g < 0.90 ? 9 : 10));
    it.v.cert = 40000000u + static_cast<uint32_t>(mix64(static_cast<uint64_t>(it.id) ^ 0x5AB) % 9000000u);
    it.basis = std::max(it.basis, item_px(it));
    g_px.erase(it.id);
    item_px(it);
    k.wallet = std::max(0LL, k.wallet - 100);
    return;
  }
}
// the copies a collector never bulks out (they leave his binder only by a sale / a trade): a 1/1, a GOLD /10, a case hit, a shiny, a 10 of a
// parallel, an EXQUISITE EMERALD / SAPPHIRE / RPA / PATCH AUTO / MASTERPIECE
bool precious(const Variant& v) {
  if (exq(v.card)) {
    const int t = exq_t(v);
    return t == xrexq::kEmerald || t == xrexq::kSapphire || t == xrexq::kMasterpiece || t == xrexq::kRpa || t == xrexq::kPatchAuto;
  }
  return v.parallel >= kGold || v.hit != kHitNone || v.shiny || (v.grade >= 10 && v.parallel >= kSilver);
}
void hobby_trim(int c) {  // past 72 copies: his cheapest unlisted, unwanted copies that aren't precious to a dealer at SELL NOW (they leave the world)
  Col& k = g_st.col[c];
  if (static_cast<int>(k.items.size()) <= kMaxBinder) return;
  std::vector<std::pair<long long, uint32_t>> cand;
  for (const Item& it : k.items)
    if (!precious(it.v) && it.ask <= 0 && !wanted(c, it.v.card)) cand.push_back({item_px(it), it.id});
  std::sort(cand.begin(), cand.end());
  const size_t drop = std::min(cand.size(), k.items.size() - static_cast<size_t>(kMaxBinder));
  for (size_t d = 0; d < drop; ++d)
    for (size_t i = 0; i < k.items.size(); ++i)
      if (k.items[i].id == cand[d].second) {
        k.wallet = xrcoin::clamp(xrcoin::add(k.wallet, xrmarket::bid(k.items[i].v)));
        g_px.erase(k.items[i].id);
        k.items.erase(k.items.begin() + static_cast<long>(i));
        ++g_st.hob.bulked;
        break;
      }
}
void hobby_list(int c, int day) {
  Col& k = g_st.col[c];
  const Hobby& hb = kHob[c];
  int listed = 0;
  for (Item& it : k.items) {  // the ones up: re-floored at today's market, an unsold one cut a little, a stale one taken down
    if (it.ask <= 0) continue;
    const long long px = item_px(it);
    if (day - it.listed >= 10) {
      it.ask = 0, it.listed = -1;
      continue;
    }
    if (!hb.stubborn && day - it.listed >= 2) it.ask = static_cast<long long>(static_cast<double>(it.ask) * 0.96);
    if (static_cast<double>(it.ask) > static_cast<double>(px) * hb.hi * 1.3) it.ask = nice_up(static_cast<double>(px) * hb.hi);  // (the market fell away)
    it.ask = std::max(it.ask, list_floor(it, px));
    ++listed;
  }
  int target = std::max(1, std::min(10, static_cast<int>(std::lround(hb.share * static_cast<double>(k.items.size())))));
  if (static_cast<int>(k.items.size()) > kMaxBinder) target = std::min(24, std::max(target, listed + static_cast<int>(k.items.size()) - kMaxBinder));  // (a full binder: the rest up for sale)
  if (listed >= target) return;
  std::unordered_map<int, int> count;
  for (const Item& it : k.items) ++count[it.v.card];
  std::vector<std::pair<double, size_t>> cand;
  for (size_t i = 0; i < k.items.size(); ++i) {
    const Item& it = k.items[i];
    if (it.ask > 0 || wanted(c, it.v.card) || (c == kLoyalist && club(it.v.card) >= 0 && club(it.v.card) == sal_club())) continue;  // (SAL never sells his club)
    const long long px = item_px(it);
    if (px < 20) continue;  // (nobody lists 5-coin bulk)
    const double tb = taste_base(c, it.v);
    double s = 0.0;
    if (count[it.v.card] > 1) s += 2.0;
    if (tb < 0.95) s += 1.5;
    if (it.basis > 0 && static_cast<double>(px) >= 1.12 * static_cast<double>(it.basis)) s += (c == kFlipper || c == kBargain) ? 4.0 : 2.0;
    if (chase_copy(it.v) && tb < 1.0) s += 3.0;
    if (c == kGrader && it.v.grade > 0) s += 2.0;
    if (c == kHypeChaser && chg(it.v.card) < 0.0f) s += 2.0;
    if (c == kStatsNerd && tier(it.v.card) <= 2) s += 1.5;
    if (it.origin == kFromSeed && tb >= 1.0) s -= 2.0;
    s += 0.5 * u01(mix64(static_cast<uint64_t>(it.id) * 0x51ull ^ static_cast<uint64_t>(day)));
    if (s >= 1.5) cand.push_back({-s, i});
  }
  std::sort(cand.begin(), cand.end());
  for (const auto& cd : cand) {
    if (listed >= target) break;
    Item& it = k.items[cd.second];
    const long long px = item_px(it);
    const uint64_t h = mix64(static_cast<uint64_t>(it.id) * 0xA5ull ^ static_cast<uint64_t>(day) * 0x9E3779B97F4A7C15ull);
    const double u = u01(h), m = u01(h ^ 1) < hb.motivated ? 0.90 + 0.08 * u : hb.lo + (hb.hi - hb.lo) * u;
    it.ask = std::max(nice_up(m * static_cast<double>(px)), list_floor(it, px));
    it.listed = day;
    ++listed;
  }
}
void dealer_prices(int day) {  // THE DEALER'S CASE: 112% of the market, 2% off a matchday unsold, never under the market
  for (Item& it : g_st.deal) {
    if (it.listed < 0) it.listed = day;
    const long long px = item_px(it);
    const double m = std::max(1.0, 1.12 - 0.02 * std::max(0, day - it.listed));
    it.ask = std::max(nice_up(m * static_cast<double>(px)), list_floor(it, px));
  }
}
// a copy moves from a seller (a collector, kDealer the case) to a collector buyer at its ask
void hobby_transfer(int seller, uint32_t id, int buyer, int day) {
  std::vector<Item>& from = seller == kDealer ? g_st.deal : g_st.col[seller].items;
  for (size_t i = 0; i < from.size(); ++i) {
    if (from[i].id != id) continue;
    const Item it = from[i];
    from.erase(from.begin() + static_cast<long>(i));
    g_px.erase(id);
    if (seller != kDealer) g_st.col[seller].wallet = xrcoin::clamp(xrcoin::add(g_st.col[seller].wallet, it.ask));
    Col& b = g_st.col[buyer];
    b.wallet = std::max(0LL, b.wallet - it.ask);
    Item& got = add_item(buyer, it.v, kFromBuy, it.ask);
    got.since = day;
    item_px(got);
    ++g_st.hob.buys;
    g_st.hob.buyCoins = xrcoin::add(g_st.hob.buyCoins, it.ask);
    const std::string from_who = seller == kDealer ? std::string("THE DEALER") : std::string(kP[seller].handle) + "'S";
    if (it.v.parallel == kBlack || it.ask >= 100000)
      wire(std::string(kP[buyer].handle) + " BOUGHT " + from_who + " " + pull_words(it.v) + " FOR " + T(it.ask) + " COINS", it.v.parallel == kBlack ? 0xFFD23F : 0x2EF2FF,
           it.v.parallel == kBlack);
    // (his trade offers / counters naming it: gone)
    for (size_t j = 0; j < g_st.inc.size();) {
      const Incoming& in = g_st.inc[j];
      if (in.col == seller && has_id(in.give.theirs, id)) g_st.inc.erase(g_st.inc.begin() + static_cast<long>(j));
      else ++j;
    }
    if (seller != kDealer && g_st.col[seller].hasCounter &&
        has_id(g_st.col[seller].counter.theirs, id)) {
      g_st.col[seller].hasCounter = false;
      g_st.col[seller].counter = Offer();
      tell_held();
    }
    g_indexDirty = true;
    return;
  }
}
void hobby_buys(int day) {
  struct Lst {
    int seller;
    uint32_t id;
    long long ask, px;
    Variant v;
    bool gone;
  };
  std::vector<Lst> L;
  for (int c = 0; c < kCollectors; ++c)
    for (const Item& it : g_st.col[c].items)
      if (it.ask > 0) L.push_back({c, it.id, it.ask, item_px(it), it.v, false});
  for (const Item& it : g_st.deal)
    if (it.ask > 0) L.push_back({kDealer, it.id, it.ask, item_px(it), it.v, false});
  if (L.empty()) return;
  const uint64_t h0 = mix64(static_cast<uint64_t>(g_st.career) * 0xB5297A4Dull ^ static_cast<uint64_t>(day) * 0x68E31DA4ull);
  const int start = static_cast<int>(h0 % kCollectors);
  for (int n = 0; n < kCollectors; ++n) {
    const int c = (start + n * 7) % kCollectors;  // (a different collector first each matchday)
    Col& k = g_st.col[c];
    for (int bought = 0; bought < kHob[c].buys; ++bought) {
      int best = -1;
      double bs = 0.0;
      for (size_t i = 0; i < L.size(); ++i) {
        const Lst& l = L[i];
        if (l.gone || l.seller == c || l.px <= 0) continue;
        if (l.ask > (c == kWhale ? k.wallet : k.wallet / 3)) continue;  // (never a third of his wallet on one card - the whale: anything)
        const double r = static_cast<double>(l.ask) / static_cast<double>(l.px);
        const double tb = taste_base(c, l.v) * (1.0 + 0.03 * mood_of(c));
        bool want;
        if (c == kBargain) want = r <= 0.93 && l.px >= 100;
        else if (c == kFlipper) want = r <= 0.96 && chg(l.v.card) > 0.0f;
        else if (c == kWhale) want = l.px >= 200000 && r <= 1.15;
        else want = tb >= 1.0 && tb >= r * kP[c].need;
        if (!want) continue;
        if ((c == kSetChaser || c == kJamFan) && has_card(c, l.v.card)) continue;  // (a double: he's selling those)
        const double s = (c == kWhale ? static_cast<double>(l.px) * 1e-6 : 0.0) + tb / std::max(0.05, r);
        if (s > bs) bs = s, best = static_cast<int>(i);
      }
      if (best < 0) break;
      L[static_cast<size_t>(best)].gone = true;
      hobby_transfer(L[static_cast<size_t>(best)].seller, L[static_cast<size_t>(best)].id, c, day);
    }
  }
}
void hobby_day(int day) {
  const std::vector<Holding> hold = fresh_holdings();
  g_worldMoved = false;
  for (int c = 0; c < kCollectors; ++c) {
    const uint64_t h = mix64(static_cast<uint64_t>(g_st.career) * 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(c + 1) << 33) ^ static_cast<uint64_t>(day + 5000) * 0xC2B2AE3D27D4EB4Full);
    for (int set = 0; set < 3; ++set) {
      const double rate = set == 0 ? kHob[c].a : (set == 1 ? kHob[c].j : kHob[c].e);
      const int n = static_cast<int>(std::floor(rate)) + (u01(h ^ (0x70ull + set)) < rate - std::floor(rate) ? 1 : 0);
      for (int i = 0; i < n; ++i) hobby_rip(c, set, mix64(h ^ (static_cast<uint64_t>(set) * 1000 + i + 1) * 0xD6E8FEB86659FD93ull), hold);
    }
    hobby_grade(c, day);
    hobby_trim(c);
  }
  dealer_prices(day);
  for (int c = 0; c < kCollectors; ++c) hobby_list(c, day);
  hobby_buys(day);
  for (int c = 0; c < kCollectors; ++c) sort_items(g_st.col[c]);
  if (g_worldMoved) tell_world();
  g_indexDirty = true;
}
// the market's search index (every copy up for sale) + the buyers it may pick for your listings - pushed when something changed (r206: never
// per frame)
void push_index() {
  if (!g_indexDirty || g_st.career == 0) return;
  g_indexDirty = false;
  std::vector<xrmarket::Offering> rows;
  int under = 0, listed = 0;
  for (int c = 0; c < kCollectors; ++c)
    for (const Item& it : g_st.col[c].items)
      if (it.ask > 0) {
        xrmarket::Offering o;
        o.id = it.id, o.seller = c, o.who = kP[c].handle, o.rgb = kP[c].c2 == 0xFFFFFF ? 0xFFB6C0 : kP[c].c2, o.v = it.v, o.ask = it.ask, o.day = it.listed;
        rows.push_back(o);
        ++listed;
        under += it.ask < item_px(it) ? 1 : 0;
      }
  for (const Item& it : g_st.deal)
    if (it.ask > 0) {
      xrmarket::Offering o;
      o.id = it.id, o.seller = kDealer, o.who = "THE DEALER", o.rgb = 0xFFD23F, o.v = it.v, o.ask = it.ask, o.day = it.listed;
      rows.push_back(o);
    }
  g_st.hob.listed = listed, g_st.hob.under = under, g_st.hob.dealerCase = static_cast<int>(g_st.deal.size());
  xrmarket::set_offerings(g_st.career, rows);
  std::vector<xrmarket::Buyer> b;
  for (int c = 0; c < kCollectors; ++c) b.push_back({kP[c].handle, g_st.col[c].wallet});
  xrmarket::set_buyers(b);
}

void advance(int days, int season) {
  if (season > g_st.season && g_st.season >= 0) restock(season);
  g_st.season = std::max(g_st.season, season);
  for (int d = 0; d < std::max(1, std::min(4, days)); ++d) {
    for (int c = 0; c < kCollectors; ++c) {
      Col& k = g_st.col[c];
      k.goodwill *= 0.9;
      k.firstToday = true;
      if (k.hasCounter) {  // (his counter stood till the matchday)
        k.hasCounter = false;
        k.counter = Offer();
      }
    }
  }
  for (int c = 0; c < kCollectors; ++c) g_st.col[c].patience = patience_of(c);
  tell_held();
  reconcile_blacks();
  refresh_prices();
  // r235: the hobby - each matchday simmed (4 at most at once): packs, grading, bulk, listings, buys
  {
    const int n = std::max(1, std::min(4, days));
    for (int d = 0; d < n; ++d) hobby_day(g_st.lastDay - (n - 1 - d));
  }
  // their open offers: ended, or re-checked at today's prices
  const std::vector<Holding> hold = fresh_holdings();
  const std::vector<Variant> listed = xrmarket::listed_copies();
  for (size_t i = 0; i < g_st.inc.size();) {
    Incoming& in = g_st.inc[i];
    const bool ended = g_st.lastDay >= in.expires || free_count(hold, listed, in.want, -1) <= 0;
    if (ended || !inc_recheck(in)) {
      if (!ended) wire(std::string(kP[in.col].handle) + " PULLED " + HISs(in.col, " OFFER - PRICES MOVED"), 0xFFB020, false);
      g_st.inc.erase(g_st.inc.begin() + static_cast<long>(i));
    } else {
      ++i;
    }
  }
  make_incoming(hold, listed);
  push_index();
  ++g_gen;
}

// the market's sold copies: r213 a 1/1 into the collector world (its buyer if he's one of the collectors, else the GRAIL HUNTER); r235 any copy
// a named collector bought (your listing, his offer you took) into his binder - what he paid its basis, his wallet paying it
bool drain_queue() {
  std::vector<Sold> q;
  {
    std::lock_guard<std::mutex> lk(g_qm);
    q.swap(g_queue);
  }
  bool any = false, blackIn = false;
  for (const Sold& e : q) {
    if (!card_ok(e.v.card)) continue;
    int to = -1;
    for (int c = 0; c < kCollectors; ++c)
      if (e.who == kP[c].handle) to = c;
    if (e.v.parallel == kBlack) {
      if (black_held(e.v.card)) continue;
      if (to < 0) to = kGrailHunter;
    } else if (to < 0) {
      continue;  // (an anonymous buyer: the copy left the collector world)
    }
    const long long paid = e.coins >= 0 ? e.coins : px_of(e.v);
    add_item(to, e.v, kFromYou, paid);
    item_px(g_st.col[to].items.back());
    sort_items(g_st.col[to]);
    if (e.coins > 0) g_st.col[to].wallet = std::max(0LL, g_st.col[to].wallet - e.coins);
    ++g_st.hob.fromYou;
    if (e.v.parallel == kBlack) {
      wire(std::string(kP[to].handle) + " NOW OWNS THE " + cname(e.v.card) + " 1/1 YOU SOLD", 0xFFD23F, false);
      blackIn = true;
    } else if (paid >= 1000) {
      wire(std::string(kP[to].handle) + " BOUGHT YOUR " + pull_words(e.v) + " FOR " + T(paid) + " COINS", 0x39FF88, false);
    }
    any = true;
    g_indexDirty = true;
  }
  if (blackIn) tell_world();
  return any;
}

// r235: an r213-r234 career's collectors re-balanced ONCE to the new rules (Lance: "less 1/1's up front ... not just give them good ass chase
// cards off the rip"), nothing of yours touched: what each of the old nine got FROM YOU (MY TRADES' "gave" + the wire's "... 1/1 YOU SOLD")
// stays his; every other chase card he was HANDED at the start / a restock (BLACK 1/1, GOLD /10, a case hit, a shiny, a 10 slab, EXQUISITE
// GOLD LEAF+ / a hit) goes to THE DEALER'S CASE - still the one copy in the world (packs keep giving a GOLD for its 1/1), now FOR SALE to
// anyone (you, the collectors) at 112% of the market falling 2% a matchday to the market: the chase open again, nothing destroyed, nothing
// duplicated (an "anonymous buyer" would have locked those 1/1s away for good). The rest of an old binder: its best copies up to his new
// starting size; the eleven new collectors start fresh
void rebalance235() {
  std::map<std::string, int> fromYou;
  for (const Trade& tr : g_st.hist)
    for (const Variant& v : tr.gave) ++fromYou[sig(v)];
  std::vector<std::string> sold;
  for (const Wire& w : g_st.wire)
    if (w.text.find(" 1/1 YOU SOLD") != std::string::npos) sold.push_back(w.text);
  const std::vector<Holding> hold = fresh_holdings();
  int moved = 0, yours = 0, trimmed = 0, blacks = 0;
  for (int c = 0; c < 9 && c < kCollectors; ++c) {
    Col& k = g_st.col[c];
    std::vector<Item> keepYours, keepRest;
    for (Item it : k.items) {
      bool you = false;
      auto f = fromYou.find(sig(it.v));
      if (f != fromYou.end() && f->second > 0) --f->second, you = true;
      if (!you && it.v.parallel == kBlack)
        for (const std::string& line : sold) you = you || line.find("THE " + cname(it.v.card) + " 1/1 YOU SOLD") != std::string::npos;
      if (you) {
        it.origin = kFromYou;
        it.basis = item_px(it);
        keepYours.push_back(it);
        ++yours;
      } else if (chase_copy(it.v)) {
        it.origin = kFromDealer;
        it.basis = 0;
        it.ask = 0;
        it.listed = std::max(0, g_st.lastDay);
        g_st.deal.push_back(it);
        ++moved;
        blacks += it.v.parallel == kBlack ? 1 : 0;
        logl("COLLECTOR TRADES r235: " + pull_words(it.v) + " from " + kP[c].handle + " -> THE DEALER'S CASE");
      } else {
        it.origin = kFromSeed;
        keepRest.push_back(it);
      }
    }
    std::stable_sort(keepRest.begin(), keepRest.end(), [](const Item& a, const Item& b) { return item_px(a) > item_px(b); });
    if (static_cast<int>(keepRest.size()) > kP[c].size) {
      trimmed += static_cast<int>(keepRest.size()) - kP[c].size;
      for (size_t i = static_cast<size_t>(kP[c].size); i < keepRest.size(); ++i) g_px.erase(keepRest[i].id);
      keepRest.resize(static_cast<size_t>(kP[c].size));
    }
    k.items = keepYours;
    k.items.insert(k.items.end(), keepRest.begin(), keepRest.end());
    for (Item& it : k.items) it.ask = 0, it.listed = -1;
    sort_items(k);
  }
  for (int c = 9; c < kCollectors; ++c) gen_one(c, std::max(1, g_st.season), hold);
  // (their offers / counters naming a copy that moved: gone)
  for (size_t j = 0; j < g_st.inc.size();) {
    bool gone = false;
    for (uint32_t id : g_st.inc[j].give.theirs) gone = gone || item_by(g_st.inc[j].col, id) == nullptr;
    if (gone) g_st.inc.erase(g_st.inc.begin() + static_cast<long>(j));
    else ++j;
  }
  for (int c = 0; c < kCollectors; ++c) {
    bool gone = false;
    for (uint32_t id : g_st.col[c].counter.theirs) gone = gone || item_by(c, id) == nullptr;
    if (g_st.col[c].hasCounter && gone) g_st.col[c].hasCounter = false, g_st.col[c].counter = Offer();
  }
  dealer_prices(std::max(0, g_st.lastDay));
  g_st.ver = 235;
  if (moved > 0)
    wire("THE DEALER BOUGHT OUT " + std::to_string(moved) + " CHASE CARDS FROM THE OLD COLLECTORS' VAULTS" + (blacks > 0 ? " (" + std::to_string(blacks) + " 1/1S)" : "") +
             " - ALL FOR SALE: MARKET > BUY",
         0xFFD23F, false);
  wire("ELEVEN NEW COLLECTORS OPEN THEIR BINDERS - CARDS > COLLECTORS", 0x2EF2FF, false);
  logl("COLLECTOR TRADES r235: re-balanced career " + std::to_string(g_st.career) + " - " + std::to_string(moved) + " chase cards to the dealer's case (" +
       std::to_string(blacks) + " 1/1s), " + std::to_string(yours) + " copies from you kept, " + std::to_string(trimmed) + " bulk copies trimmed, 11 new collectors");
  tell_held();
  tell_world();
  g_indexDirty = true;
}
// ============================================================================================== YOUR CARDS (the pane)
void build_mine_rows() {  // (g_m held) your copies priced + sorted: when the pane opens / after a trade - never per frame
  const std::vector<Holding> hold = fresh_holdings();
  const std::vector<Variant> listed = xrmarket::listed_copies();
  const std::vector<Variant> held = held_copies();
  g_mineRows.clear();
  for (const Holding& h : hold) {
    if (h.grading || !card_ok(h.card)) continue;
    MineRow r;
    r.h = h;
    r.px = px_of(h);
    const std::string s = sig(h);
    int nl = 0, nh = 0;
    for (const Variant& l : listed) nl += sig(l) == s ? 1 : 0;
    for (const Variant& x : held) nh += sig(x) == s ? 1 : 0;
    r.free = std::max(0, h.count - nl - nh);
    r.lock = r.free > 0 ? 0 : (nl > 0 ? 1 : 2);
    g_mineRows.push_back(r);
  }
  std::stable_sort(g_mineRows.begin(), g_mineRows.end(), [](const MineRow& a, const MineRow& b) { return a.px > b.px; });
  size_t sg = hold.size();
  for (const Holding& h : hold) sg = sg * 31 + static_cast<size_t>(h.count) + static_cast<size_t>(h.card) * 7;
  g_mineSig = sg;
}
int in_draft(const Offer& o, const Variant& v) {
  const std::string s = sig(v);
  int n = 0;
  for (const Variant& m : o.mine) n += sig(m) == s ? 1 : 0;
  return n;
}

// ============================================================================================== the pad (the market's keys)
constexpr float kPadX0 = 430, kPadX1 = 1220, kPadY0 = 196, kPadY1 = 892;
void pad_key_box(int k, float* x0, float* y0, float* x1, float* y1) {
  if (k >= 12) {
    *x0 = 640, *x1 = 1178, *y0 = 824, *y1 = 878;
    return;
  }
  const int r = k / 3, c = k % 3;
  *x0 = 640 + c * 182.0f, *x1 = *x0 + 174, *y0 = 590 + r * 58.0f, *y1 = *y0 + 52;
}
const char* const kPadKey[13] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "CLR", "0", "DEL", "OK"};
long long pad_value() {
  xrcoin::Coins v = 0;
  if (!g_padDigits.empty()) xrcoin::parse(g_padDigits.c_str(), &v);
  return v;
}
void pad_say(const std::string& s) {
  g_padSay = s;
  g_padSayAt = g_now;
}
void pad_open_for(int forWho) {
  const Offer& d = g_draft[g_colCur];
  g_pad = true;
  g_padFor = forWho;
  const long long start = forWho == 0 ? d.myCoins : d.theirCoins;
  g_padDigits = start > 0 ? std::to_string(start) : "";
  g_padFresh = true;
  g_padKey = start > 0 ? 12 : 0;
  g_padSay.clear();
  g_padSayAt = -1e9;
  g_padHover = -1;
}
void pad_press(int k) {
  g_padHit = k;
  g_padHitAt = g_now;
  if (k >= 0 && k <= 10 && k != 9) {
    const char d = k == 10 ? '0' : static_cast<char>('1' + k);
    if (g_padFresh) g_padDigits.clear();
    g_padFresh = false;
    if (g_padDigits.size() >= 15) {
      pad_say("15 DIGITS AT MOST");
      sound("book_tick", 0.4f);
      return;
    }
    if (!(g_padDigits.empty() && d == '0')) g_padDigits += d;
    sound("book_tick", 0.6f);
  } else if (k == 9) {
    g_padDigits.clear();
    g_padFresh = false;
    sound("book_tick", 0.6f);
  } else if (k == 11) {
    if (g_padFresh) g_padDigits.clear();
    g_padFresh = false;
    if (!g_padDigits.empty()) g_padDigits.pop_back();
    sound("book_tick", 0.6f);
  } else if (k >= 12) {
    const long long v = xrcoin::clamp(pad_value());
    Offer& d = g_draft[g_colCur];
    if (g_padFor == 0) {
      if (v > balance()) {
        pad_say("YOU HAVE ONLY " + T(balance()) + " COINS");
        return;
      }
      d.myCoins = v;
    } else {
      d.theirCoins = v;
    }
    say(v > 0 ? (g_padFor == 0 ? std::string("YOU ADD ") : HEs(g_colCur, " ADDS ")) + T(v) + " COINS" : (g_padFor == 0 ? std::string("NO COINS FROM YOU") : std::string(kShe[g_colCur] ? "NO COINS FROM HER" : "NO COINS FROM HIM")), 0x2EF2FF);
    sound("book_ding", 0.6f);
    g_pad = false;
  }
}
void pad_move(int dir) {
  int k = g_padKey;
  if (k >= 12) {
    if (dir == 3) k = 10;
  } else {
    const int r = k / 3, c = k % 3;
    if (dir == 1 && c > 0) --k;
    else if (dir == 2 && c < 2) ++k;
    else if (dir == 3 && r > 0) k -= 3;
    else if (dir == 4) k = r < 3 ? k + 3 : 12;
  }
  if (k != g_padKey) {
    g_padKey = k;
    sound("book_tick", 0.4f);
  }
}
// the copy under the desk's cursor (his binder / your cards); false none
bool cursor_his(const Item** out) {
  const Col& k = g_st.col[g_colCur];
  const int at = g_paneCur[0];
  if (at < 0 || at >= static_cast<int>(k.items.size())) return false;
  *out = &k.items[static_cast<size_t>(at)];
  return true;
}
bool cursor_mine(const MineRow** out) {
  const int at = g_paneCur[1];
  if (at < 0 || at >= static_cast<int>(g_mineRows.size())) return false;
  *out = &g_mineRows[static_cast<size_t>(at)];
  return true;
}
void open_desk(int c) {
  g_desk = true;
  g_colCur = c;
  g_pane = kPaneHis;
  g_paneCur[0] = g_paneTop[0] = 0;
  g_paneCur[1] = g_paneTop[1] = 0;
  g_coinCur = 0;
  g_showCounter = g_st.col[c].hasCounter;
  if (g_st.col[c].say.empty() || g_now - g_st.col[c].sayAt > 30.0) speak(c, kLIdle, static_cast<uint64_t>(g_now * 10));
  build_mine_rows();
}
}  // namespace

// ============================================================================================== the public model
void set_source(const Source& s) {
  std::lock_guard<std::mutex> lk(g_m);
  g_src = s;
}
void world_got(const Variant& v, const char* who) {
  if (v.parallel != kBlack) return;
  std::lock_guard<std::mutex> lk(g_qm);
  Sold s;
  s.v = v;
  s.who = who != nullptr ? std::string(who) : std::string();
  g_queue.push_back(s);
}
void world_sold(const Variant& v, const char* who, long long coins) {  // r235
  std::lock_guard<std::mutex> lk(g_qm);
  Sold s;
  s.v = v;
  s.who = who != nullptr ? std::string(who) : std::string();
  s.coins = std::max(0LL, coins);
  g_queue.push_back(s);
}
bool frame(double now) {
  std::lock_guard<std::mutex> lk(g_m);
  g_now = now;
  if (now - g_lastRead < 0.5) return false;
  g_lastRead = now;
  load_set();
  const unsigned career = g_src.career != nullptr ? g_src.career() : 0;
  if (career == 0 || g_set.empty()) {
    if (g_st.career != 0) {
      g_st = State();
      g_desk = false;
      g_pad = false;
      g_ask = 0;
      ++g_gen;
      return true;
    }
    return false;
  }
  const xrmarket::Stats ms = xrmarket::stats();
  if (ms.career != career) return false;  // (the market first: its prices are this career's)
  bool changed = false;
  if (career != g_st.career) {
    g_tab = 0;
    g_desk = g_showCounter = g_incCountering = false;
    g_pad = false;
    g_ask = 0;
    g_colCur = g_incCur = g_histTop = 0;
    for (Offer& d : g_draft) d = Offer();
    g_cycle = xrmarket::cycle_now();
    refresh_moves();
    if (!load(career)) {  // (a career the collectors haven't met: their binders built now - nothing of yours touched)
      g_st.lastDay = ms.day;
      g_st.season = std::max(1, ms.season);
      g_st.loaded = true;
      g_st.ver = 235;
      generate(g_st.season);
      for (int c = 0; c < kCollectors; ++c) speak(c, kLIdle, static_cast<uint64_t>(c));
      wire("TWENTY COLLECTORS OPEN THEIR BINDERS - CARDS > COLLECTORS", 0x2EF2FF, false);
      logl("COLLECTOR TRADES: the twenty collectors met career " + std::to_string(career));
    } else if (g_st.ver < 235) {
      rebalance235();  // r235: an r213-r234 career's collectors, once (nothing of yours touched)
    }
    for (int c = 0; c < kCollectors; ++c)  // (r235: a collector new to this career - the file had nine: his voice)
      if (g_st.col[c].say.empty()) speak(c, kLIdle, static_cast<uint64_t>(c));
    reconcile_blacks();
    tell_held();
    tell_world();
    g_indexDirty = true;
    changed = true;
  }
  if (drain_queue()) changed = true;
  if (ms.day > g_st.lastDay) {
    const int days = ms.day - g_st.lastDay;
    g_st.lastDay = ms.day;
    g_cycle = xrmarket::cycle_now();
    refresh_moves();
    advance(days, ms.season);
    if (g_desk && g_pane == kPaneMine) build_mine_rows();
    changed = true;
  }
  if ((g_desk && g_pane == kPaneMine) || !g_st.inc.empty()) {  // (your binder changed - a pack, a sale: at most twice a second)
    std::vector<Holding> h = fresh_holdings();
    size_t sg = h.size();
    for (const Holding& x : h) sg = sg * 31 + static_cast<size_t>(x.count) + static_cast<size_t>(x.card) * 7;
    static size_t s_incSig = 0;
    if (!g_st.inc.empty() && sg != s_incSig) {  // an offer of theirs for a copy that left (sold, listed, traded in) goes
      s_incSig = sg;
      const std::vector<Variant> listed = xrmarket::listed_copies();
      for (size_t i = 0; i < g_st.inc.size();)
        if (free_count(h, listed, g_st.inc[i].want, -1) <= 0) {
          g_st.inc.erase(g_st.inc.begin() + static_cast<long>(i));
          changed = true;
        } else {
          ++i;
        }
    }
    if (g_desk && g_pane == kPaneMine && sg != g_mineSig) {  // the YOUR CARDS pane re-priced
      build_mine_rows();
      ++g_gen;
    }
  }
  if (g_indexDirty) push_index();  // r235: the market's search index (only when a listing moved)
  if (changed) {
    save();
    ++g_gen;
  }
  return changed;
}
int generation() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_gen;
}
bool redraw_due() {
  std::lock_guard<std::mutex> lk(g_m);
  static double s_last = -1e9;
  static bool s_fresh = false;
  bool due = false;
  const double fl = g_now - g_flashAt;
  if (fl >= 0.0 && fl < 0.62 && g_now - s_last >= 0.12) due = true;
  const bool fresh = g_now - g_msgAt < 6.0;
  if (fresh != s_fresh) {
    s_fresh = fresh;
    due = true;
  }
  if (g_pointDirty) {
    g_pointDirty = false;
    due = true;
  }
  const double ph = g_now - g_padHitAt;
  if (g_pad && ph >= 0.0 && ph < 0.3 && g_now - s_last >= 0.08) due = true;
  if (due) {
    s_last = g_now;
    ++g_gen;
  }
  return due;
}
int offers_waiting() {
  std::lock_guard<std::mutex> lk(g_m);
  return static_cast<int>(g_st.inc.size());
}
void open_tab(int tab) {
  std::lock_guard<std::mutex> lk(g_m);
  g_tab = std::max(0, std::min(kTabs - 1, tab));
  g_desk = false;
  g_ask = 0;
  g_pad = false;
  ++g_gen;
}

bool input(const Input& in) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_st.career == 0 || !g_st.loaded) return false;
  if (drain_queue()) ++g_gen, push_index();  // r235: (a sale the market just made lands before any desk move)
  bool used = false;
  auto done = [&]() {
    ++g_gen;
    used = true;
  };
  if (g_pad) {  // the number pad has every button while it's up
    if (in.step && in.dir != 0) pad_move(in.dir);
    if (in.a) pad_press(g_padKey >= 12 ? 12 : g_padKey);
    else if (in.x) pad_press(11);
    else if (in.b) g_pad = false;
    ++g_gen;
    return true;
  }
  if (g_ask != 0) {  // the hub's confirm: A yes, B no
    if (in.a) {
      const int c = g_colCur;
      if (g_ask == 1) {
        const Reply r = do_propose(c, g_draft[c]);
        if (r.kind == kAccepted) say("DEAL! " + std::string(kP[c].handle) + " TOOK IT - IT'S IN YOUR BINDER", 0x39FF88), build_mine_rows();
        else if (r.kind == kCountered) say(std::string(kP[c].handle) + " COUNTERED", 0xFFB020), g_showCounter = true, sound("book_ding", 0.6f);
        else if (r.kind == kDeclined) say(std::string(kP[c].handle) + " SAID NO", 0xFF4D5E), sound("book_tick", 0.5f);
        else say(r.line, 0xFFB020);
      } else if (g_ask == 2) {
        Incoming* i = inc_by(g_askId);
        const int c = i != nullptr ? i->col : 0;
        if (do_accept_incoming(g_askId)) say("TRADED WITH " + std::string(kP[c].handle) + " - CHECK YOUR BINDER", 0x39FF88);
        else say("THAT OFFER IS OFF", 0xFF4D5E);
      } else if (g_ask == 3) {
        if (do_accept_counter(c)) say("DEAL! " + std::string(kP[c].handle) + "'S COUNTER ACCEPTED", 0x39FF88), g_showCounter = false, build_mine_rows();
        else say("THAT COUNTER IS OFF", 0xFF4D5E), g_showCounter = g_st.col[c].hasCounter;
      }
      g_ask = 0;
      save();
      done();
    } else if (in.b) {
      g_ask = 0;
      done();
    }
    return used || in.dir != 0;
  }
  if (g_tab == kTabCols && g_desk) {
    const int c = g_colCur;
    Col& k = g_st.col[c];
    if (g_showCounter && k.hasCounter) {  // his counter over the desk: A accept, X decline, Y edit, B later
      if (in.a) {
        g_ask = 3;
        done();
      } else if (in.x) {
        do_decline_counter(c);
        g_showCounter = false;
        say("COUNTER DECLINED", 0x9A92B8);
        save();
        done();
      } else if (in.y) {
        g_draft[c] = k.counter;
        k.hasCounter = false;
        tell_held();
        g_showCounter = false;
        say(HISs(c, " COUNTER IS IN YOUR OFFER - CHANGE IT AND SEND AGAIN"), 0x2EF2FF);
        build_mine_rows();
        save();
        done();
      } else if (in.b) {
        g_showCounter = false;
        say(HISs(c, " COUNTER STANDS TILL THE NEXT MATCHDAY"), 0x9A92B8);
        done();
      }
      return true;
    }
    if (in.b) {
      g_desk = false;
      done();
      return true;
    }
    if (in.x) {
      g_pane = (g_pane + 1) % 3;
      if (g_pane == kPaneMine) build_mine_rows();
      sound("book_tick", 0.5f);
      done();
      return true;
    }
    if (in.y) {
      if (k.hasCounter) g_showCounter = true;
      else g_ask = 1;
      done();
      return true;
    }
    if (g_pane == kPaneCoins) {
      if (in.step && (in.dir == 3 || in.dir == 4)) {
        g_coinCur = std::max(0, std::min(2, g_coinCur + (in.dir == 4 ? 1 : -1)));
        sound("book_tick", 0.4f);
        done();
      } else if (in.a) {
        if (g_coinCur == 2) {
          g_draft[c] = Offer();
          say("OFFER CLEARED", 0x9A92B8);
        } else {
          pad_open_for(g_coinCur);
        }
        done();
      }
      return true;
    }
    const int n = g_pane == kPaneHis ? static_cast<int>(k.items.size()) : static_cast<int>(g_mineRows.size());
    int& cur = g_paneCur[g_pane];
    if (in.step && in.dir != 0 && n > 0) {
      const int col = cur % kGridCols;
      int to = cur;
      if (in.dir == 1 && col > 0) to = cur - 1;
      else if (in.dir == 2 && col < kGridCols - 1 && cur + 1 < n) to = cur + 1;
      else if (in.dir == 3 && cur - kGridCols >= 0) to = cur - kGridCols;
      else if (in.dir == 4) to = std::min(n - 1, cur + kGridCols);
      if (to != cur) {
        cur = to;
        sound("book_tick", 0.4f);
        done();
      }
      return true;
    }
    cur = std::max(0, std::min(std::max(0, n - 1), cur));
    if (in.a && n > 0) {
      Offer& d = g_draft[c];
      if (g_pane == kPaneHis) {
        const Item* it = nullptr;
        if (cursor_his(&it)) {
          auto f = std::find(d.theirs.begin(), d.theirs.end(), it->id);
          if (f != d.theirs.end()) {
            d.theirs.erase(f);
            sound("book_tick", 0.5f);
          } else if (static_cast<int>(d.theirs.size()) >= kMaxSide) {
            say("5 OF " + HISs(c, " CARDS AT MOST"), 0xFFB020);
          } else {
            d.theirs.push_back(it->id);
            sound("book_ding", 0.5f);
            const double h = hold_of(c, it->v);
            if (h >= 1.3) speak_text(c, kLIdle, h >= 1.38 ? "THAT ONE? IT'LL COST YOU." : "I'M FOND OF THAT ONE.");
          }
        }
      } else {
        const MineRow* r = nullptr;
        if (cursor_mine(&r)) {
          const int inD = in_draft(d, r->h);
          if (inD > 0) {  // (a press on a card in the offer takes it out: one copy of a kind a trade)
            for (size_t i = 0; i < d.mine.size(); ++i)
              if (sig(d.mine[i]) == sig(r->h)) {
                d.mine.erase(d.mine.begin() + static_cast<long>(i));
                break;
              }
            sound("book_tick", 0.5f);
          } else if (r->free <= inD) {
            say(r->lock == 1 ? "IT'S LISTED FOR SALE - TAKE THE LISTING DOWN FIRST (MARKET)" : "IT'S HELD IN AN OPEN TRADE", 0xFFB020);
          } else if (static_cast<int>(d.mine.size()) >= kMaxSide) {
            say("5 OF YOUR CARDS AT MOST", 0xFFB020);
          } else {
            Variant v = r->h;
            d.mine.push_back(v);
            sound("book_ding", 0.5f);
            const double t = taste_of(c, v);
            if (t <= 0.0) speak(c, kLNotInto, hstr(sig(v)));
            else if (t >= 1.08) speak(c, kLPitch, hstr(sig(v)));
            else if (t < 0.85) speak_text(c, kLIdle, "MEH. THAT ONE DOESN'T DO MUCH FOR ME.");
          }
        }
      }
      done();
      return true;
    }
    return in.dir != 0 || in.a;
  }
  if (in.step && (in.dir == 1 || in.dir == 2)) {  // the tabs
    g_tab = (g_tab + (in.dir == 2 ? 1 : kTabs - 1)) % kTabs;
    g_incCountering = false;
    sound("book_tick", 0.5f);
    done();
    return true;
  }
  if (g_tab == kTabCols) {
    if (in.step && (in.dir == 3 || in.dir == 4)) {
      g_colCur = std::max(0, std::min(kCollectors - 1, g_colCur + (in.dir == 4 ? 1 : -1)));
      sound("book_tick", 0.4f);
      done();
      return true;
    }
    if (in.a) {
      open_desk(g_colCur);
      sound("book_ding", 0.5f);
      done();
      return true;
    }
    return in.x || in.y;
  }
  if (g_tab == kTabOffers) {
    const int n = static_cast<int>(g_st.inc.size());
    g_incCur = std::max(0, std::min(std::max(0, n - 1), g_incCur));
    if (g_incCountering) {
      if (in.step && (in.dir == 1 || in.dir == 2)) {
        g_incPick = std::max(0, std::min(2, g_incPick + (in.dir == 2 ? 1 : -1)));
        sound("book_tick", 0.5f);
        done();
      } else if (in.a && g_incCur < n) {
        const int c = g_st.inc[static_cast<size_t>(g_incCur)].col;
        const int r = do_counter_incoming(g_st.inc[static_cast<size_t>(g_incCur)].id, kCounterPct[g_incPick]);
        if (r == 1) say("THEY TOOK IT - TRADED AT +" + std::to_string(kCounterPct[g_incPick]) + "%", 0x39FF88);
        else if (r == 2) say(std::string(kP[c].handle) + " CAME BACK WITH A FINAL OFFER", 0xFFB020), sound("book_ding", 0.6f);
        else if (r == 3) say("TOO RICH FOR " + std::string(kP[c].handle) + " - " + HEs(c, " WALKED AWAY"), 0xFF4D5E);
        g_incCountering = false;
        save();
        done();
      } else if (in.b) {
        g_incCountering = false;
        done();
      }
      return true;
    }
    if (in.step && (in.dir == 3 || in.dir == 4) && n > 0) {
      g_incCur = std::max(0, std::min(n - 1, g_incCur + (in.dir == 4 ? 1 : -1)));
      sound("book_tick", 0.4f);
      done();
      return true;
    }
    if (n > 0 && in.a) {
      g_ask = 2;
      g_askId = g_st.inc[static_cast<size_t>(g_incCur)].id;
      done();
      return true;
    }
    if (n > 0 && in.x) {
      const int c = g_st.inc[static_cast<size_t>(g_incCur)].col;
      do_decline_incoming(g_st.inc[static_cast<size_t>(g_incCur)].id);
      say("OFFER DECLINED - " + std::string(kP[c].handle) + " SHRUGS", 0x9A92B8);
      save();
      done();
      return true;
    }
    if (n > 0 && in.y && !g_st.inc[static_cast<size_t>(g_incCur)].final) {
      g_incCountering = true;
      g_incPick = 1;
      done();
      return true;
    }
    return in.x || in.y;
  }
  // MY TRADES: up / down scroll
  if (in.step && (in.dir == 3 || in.dir == 4)) {
    const int n = static_cast<int>(g_st.hist.size());
    g_histTop = std::max(0, std::min(std::max(0, n - 4), g_histTop + (in.dir == 4 ? 1 : -1)));
    sound("book_tick", 0.4f);
    done();
    return true;
  }
  return in.x || in.y;
}
bool point(float x, float y, bool click) {
  std::unique_lock<std::mutex> lk(g_m, std::try_to_lock);
  const bool inside = x >= kPadX0 && x <= kPadX1 && y >= kPadY0 && y <= kPadY1;
  if (!lk.owns_lock()) {
    if (click && g_pad.load() && inside) g_stashX = x, g_stashY = y, g_stashClick = true;
    return g_pad.load() && inside;
  }
  if (!g_pad) {
    g_padHover = -1;
    g_stashClick = false;
    return false;
  }
  if (g_stashClick.exchange(false)) {
    x = g_stashX.load(), y = g_stashY.load();
    click = true;
  }
  if (x < kPadX0 || x > kPadX1 || y < kPadY0 || y > kPadY1) {
    if (g_padHover != -1) g_padHover = -1, g_pointDirty = true;
    return false;
  }
  int best = -1;
  float bd = 1e9f;
  for (int k = 0; k < 13; ++k) {
    float x0, y0, x1, y1;
    pad_key_box(k, &x0, &y0, &x1, &y1);
    const float dx = std::max(0.0f, std::max(x0 - x, x - x1)), dy = std::max(0.0f, std::max(y0 - y, y - y1));
    const float d = dx * dx + dy * dy;
    if (d < 144.0f && d < bd) bd = d, best = k;
  }
  if (best != g_padHover) {
    g_padHover = best;
    if (best >= 0) g_padKey = best;
    g_pointDirty = true;
  }
  if (click && best >= 0) {
    pad_press(best);
    g_pointDirty = true;
    ++g_gen;
  }
  return true;
}
bool pad_open() { return g_pad.load(); }
bool asking(std::string* q, std::string* line) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_ask == 0) return false;
  const int c = g_colCur;
  if (g_ask == 1) {
    const Value v = value_of(c, g_draft[c]);
    *q = "SEND THIS OFFER TO " + std::string(kP[c].handle) + "?";
    *line = "YOU GIVE " + T(v.mineMarket) + "  -  YOU GET " + T(v.theirsMarket) + "  (THIS WEEK'S MARKET)";
  } else if (g_ask == 2) {
    const Incoming* i = inc_by(g_askId);
    if (i == nullptr) return false;
    long long mk = i->give.theirCoins;
    for (uint32_t id : i->give.theirs)
      if (const Item* it = item_by(i->col, id)) mk = xrcoin::add(mk, item_px(*it));
    *q = "TRADE YOUR " + short_words(i->want) + "?";
    *line = std::string(kP[i->col].handle) + " GIVES " + T(mk) + " AT MARKET  -  YOURS IS WORTH " + T(px_of(i->want));
  } else {
    const Value v = value_of(c, g_st.col[c].counter);
    *q = "ACCEPT " + std::string(kP[c].handle) + "'S COUNTER?";
    *line = "YOU GIVE " + T(v.mineMarket) + "  -  YOU GET " + T(v.theirsMarket) + "  (THIS WEEK'S MARKET)";
  }
  return true;
}
void hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord) {
  std::lock_guard<std::mutex> lk(g_m);
  *xWord = *yWord = "";
  *aDim = false;
  if (g_pad) {
    *aWord = "PRESS KEY";
    *xWord = "DEL";
    return;
  }
  if (g_tab == kTabCols && g_desk) {
    const Col& k = g_st.col[g_colCur];
    if (g_showCounter && k.hasCounter) {
      *aWord = "ACCEPT COUNTER";
      *xWord = "DECLINE";
      *yWord = "EDIT";
      return;
    }
    const Offer& d = g_draft[g_colCur];
    if (g_pane == kPaneCoins) *aWord = g_coinCur == 2 ? "CLEAR OFFER" : "SET COINS";
    else if (g_pane == kPaneHis) {
      const Item* it = nullptr;
      const bool on = cursor_his(&it);
      *aWord = on && std::find(d.theirs.begin(), d.theirs.end(), it->id) != d.theirs.end() ? "TAKE OUT" : "WANT IT";
      *aDim = !on;
    } else {
      const MineRow* r = nullptr;
      const bool on = cursor_mine(&r);
      *aWord = on && in_draft(d, r->h) > 0 ? "TAKE OUT" : "OFFER IT";
      *aDim = !on;
    }
    *xWord = g_pane == kPaneHis ? "YOUR CARDS" : (g_pane == kPaneMine ? "COINS" : HISs(g_colCur, " BINDER"));
    *yWord = k.hasCounter ? "THE COUNTER" : "SEND OFFER";
    return;
  }
  if (g_tab == kTabCols) {
    *aWord = "OPEN THE DESK";
  } else if (g_tab == kTabOffers) {
    if (g_incCountering) {
      *aWord = "SEND COUNTER";
      return;
    }
    *aWord = "ACCEPT";
    *aDim = g_st.inc.empty();
    if (!g_st.inc.empty()) {
      *xWord = "DECLINE";
      if (g_incCur < static_cast<int>(g_st.inc.size()) && !g_st.inc[static_cast<size_t>(g_incCur)].final) *yWord = "COUNTER";
    }
  } else {
    *aWord = "";
    *aDim = true;
  }
}

// ============================================================================================== the drawing
namespace {
constexpr uint32_t kBg0 = 0x0A1024, kBg1 = 0x05070F, kPanel = 0x121A33, kPanelHi = 0x1C2850, kLine = 0x2C3A66, kInk = 0xF4F6FF, kDim = 0x8E99B8,
                   kUp = 0x39FF88, kDown = 0xFF4D5E, kGoldC = 0xFFD23F, kCyan = 0x2EF2FF, kPink = 0xFF2E88, kAmber = 0xFFB020;
struct Pen {
  std::vector<xrtext::BoardPiece> v;
  xrmenu::Kit K{v};
  std::vector<std::shared_ptr<void>>* keep = nullptr;
};
// what draw needs, copied under the lock (then drawn without it)
struct Snap {
  State st;
  int tab, colCur, pane, coinCur, incCur, histTop, paneCur[2], paneTop[2], incPick, ask;
  bool desk, showCounter, incCountering, pad, padFresh;
  Offer draft;
  std::vector<MineRow> mineRows;
  std::unordered_map<uint32_t, long long> px;
  std::string msg, padDigits, padSay;
  uint32_t msgRgb;
  double now, msgAt, flashAt, padSayAt, padHitAt, cycle;
  int padFor, padKey, padHit;
  long long balance;
  double mood[kCollectors], need[kCollectors];
  int patienceMax[kCollectors];
  std::vector<double> incMax;  // per incoming: the most he'd give (his eyes) / what it costs him now - the counter's +% preview
};
void chrome(Pen& P, const std::string& t, float x, float y, float px, float room, int align) {
  xrtext::BoardPiece p;
  p.kind = align == 0 ? xrtext::BoardPiece::kText : (align < 0 ? xrtext::BoardPiece::kTextLeft : xrtext::BoardPiece::kTextRight);
  p.x0 = x;
  p.y0 = y;
  p.x1 = room;
  p.y1 = px;
  p.rgb = 0xFFFFFF;
  p.rgb2 = 0x9FB2D6;
  p.outline = std::max(3.0f, px * 0.08f);
  p.outlineRgb = 0x0E1430;
  p.shadow = px * 0.06f;
  p.shadowRgb = kCyan;
  p.shadowA = 0.8f;
  p.text = t;
  P.v.push_back(p);
}
void gold_title(Pen& P, const std::string& t, float x, float y, float px, float room) {
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kTextLeft;
  p.x0 = x;
  p.y0 = y;
  p.x1 = room;
  p.y1 = px;
  p.rgb = 0xFFF2B0;
  p.rgb2 = kGoldC;
  p.outline = 4.0f;
  p.outlineRgb = 0x2A1A00;
  p.text = t;
  P.v.push_back(p);
}
std::string money(long long n, float px, float room) {
  const std::string t = T(n);
  return xrtext::board_text_width(t, px) <= room ? t : xrcoin::compact(n);
}
void coin(Pen& P, float cx, float cy, float r) {
  P.K.glow(cx - 2.0f * r, cy - 2.0f * r, cx + 2.0f * r, cy + 2.0f * r, kGoldC, 0.3f);
  P.K.box(cx - r, cy - r, cx + r, cy + r, r, 0xB8860B);
  P.K.box(cx - 0.86f * r, cy - 0.86f * r, cx + 0.86f * r, cy + 0.86f * r, 0.86f * r, kGoldC, 1.0f, 0xE0A800);
  // r216: a BIT COIN - the pixel ring + the 8-bit B (was a "T")
  xrcoin::bit_face([&](float x0, float y0, float x1, float y1, uint32_t rgb) { P.K.box(x0, y0, x1, y1, 0, rgb); }, cx, cy, r);
}
void disc(Pen& P, float cx, float cy, float r, uint32_t rgb, float a = 1.0f, uint32_t rgb2 = 0) { P.K.box(cx - r, cy - r, cx + r, cy + r, r, rgb, a, rgb2); }
// a stroke through points (quads per segment)
void stroke(Pen& P, const std::vector<float>& pts, float w, uint32_t rgb, float a = 1.0f) {
  for (size_t i = 0; i + 3 < pts.size(); i += 2) {
    const float ax = pts[i], ay = pts[i + 1], bx = pts[i + 2], by = pts[i + 3];
    const float dx = bx - ax, dy = by - ay, l = std::max(0.01f, std::sqrt(dx * dx + dy * dy));
    const float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    P.K.poly({ax + nx, ay + ny, bx + nx, by + ny, bx - nx, by - ny, ax - nx, ay - ny}, rgb, a);
  }
}
// (the board fills a polygon as a fan from its first point: every shape here is built from convex pieces)
void star(Pen& P, float cx, float cy, float ro, float ri, uint32_t rgb, float a = 1.0f, int points = 5) {
  std::vector<float> inner;
  for (int i = 0; i < points; ++i) {
    const float ang = -1.5708f + 6.2831853f * (i + 0.5f) / points;
    inner.push_back(cx + ri * std::cos(ang));
    inner.push_back(cy + ri * std::sin(ang));
  }
  P.K.poly(inner, rgb, a);
  for (int i = 0; i < points; ++i) {
    const float ang = -1.5708f + 6.2831853f * i / points;
    const size_t p = static_cast<size_t>((i + points - 1) % points), q = static_cast<size_t>(i);
    P.K.poly({cx + ro * std::cos(ang), cy + ro * std::sin(ang), inner[2 * p], inner[2 * p + 1], inner[2 * q], inner[2 * q + 1]}, rgb, a);
  }
}
// each collector's emblem (s = its size): crown, shark fin, ball, shield, puzzle piece, arrows, retro star, gem, bolt
void emblem(Pen& P, int kind, float cx, float cy, float s, uint32_t c1, uint32_t c2) {
  xrmenu::Kit& K = P.K;
  K.glow(cx - 0.75f * s, cy - 0.75f * s, cx + 0.75f * s, cy + 0.75f * s, c2, 0.28f);
  disc(P, cx, cy, 0.5f * s, xrmenu::mix(c1, 0x000000, 0.25f), 1.0f, xrmenu::mix(c1, 0x000000, 0.7f));
  K.ring(cx - 0.5f * s, cy - 0.5f * s, cx + 0.5f * s, cy + 0.5f * s, 0.5f * s, std::max(2.0f, 0.045f * s), c2, 0.95f);
  K.ring(cx - 0.43f * s, cy - 0.43f * s, cx + 0.43f * s, cy + 0.43f * s, 0.43f * s, std::max(1.0f, 0.015f * s), c2, 0.35f);
  const float u = s;
  switch (kind) {
    case 0: {  // a crown: three spikes on a band, its jewels
      K.poly({cx - 0.33f * u, cy + 0.06f * u, cx - 0.09f * u, cy + 0.06f * u, cx - 0.30f * u, cy - 0.20f * u}, 0xFFD23F);
      K.poly({cx + 0.09f * u, cy + 0.06f * u, cx + 0.33f * u, cy + 0.06f * u, cx + 0.30f * u, cy - 0.20f * u}, 0xFFD23F);
      K.poly({cx - 0.17f * u, cy + 0.06f * u, cx + 0.17f * u, cy + 0.06f * u, cx, cy - 0.29f * u}, 0xFFE27A);
      K.box(cx - 0.33f * u, cy - 0.01f * u, cx + 0.33f * u, cy + 0.14f * u, 0.02f * u, 0xFFD23F, 1.0f, 0xC89A1A);
      K.box(cx - 0.35f * u, cy + 0.12f * u, cx + 0.35f * u, cy + 0.23f * u, 0.03f * u, 0xFFE27A, 1.0f, 0xB8860B);
      disc(P, cx - 0.30f * u, cy - 0.21f * u, 0.045f * u, 0xFF2E88);
      disc(P, cx, cy - 0.30f * u, 0.05f * u, 0x2EF2FF);
      disc(P, cx + 0.30f * u, cy - 0.21f * u, 0.045f * u, 0x39FF88);
      disc(P, cx, cy + 0.175f * u, 0.04f * u, 0xFF2E88);
      disc(P, cx - 0.2f * u, cy + 0.175f * u, 0.028f * u, 0x2EF2FF);
      disc(P, cx + 0.2f * u, cy + 0.175f * u, 0.028f * u, 0x2EF2FF);
      break;
    }
    case 1: {  // a shark fin over waves
      K.poly({cx - 0.24f * u, cy + 0.10f * u, cx + 0.04f * u, cy - 0.30f * u, cx + 0.26f * u, cy + 0.10f * u}, 0x9CC2D6);
      K.poly({cx + 0.04f * u, cy - 0.30f * u, cx + 0.26f * u, cy + 0.10f * u, cx + 0.07f * u, cy + 0.10f * u}, 0x5C8094);
      for (int w = 0; w < 2; ++w) {
        std::vector<float> pts;
        for (int i = 0; i <= 16; ++i) {
          const float x = cx - 0.34f * u + 0.68f * u * i / 16.0f;
          pts.push_back(x);
          pts.push_back(cy + (0.15f + 0.09f * w) * u + 0.03f * u * std::sin(i * 0.9f + w));
        }
        stroke(P, pts, 0.035f * u, w == 0 ? c2 : xrmenu::mix(c2, 0x000000, 0.4f));
      }
      break;
    }
    case 2: {  // a ball (red top, white bottom, the band, the button)
      disc(P, cx, cy, 0.33f * u, 0xF4F4F4, 1.0f, 0xC8CCD6);
      std::vector<float> top;
      for (int i = 0; i <= 20; ++i) {
        const float a = 3.14159265f + 3.14159265f * i / 20.0f;
        top.push_back(cx + 0.33f * u * std::cos(a));
        top.push_back(cy + 0.33f * u * std::sin(a));
      }
      K.poly(top, 0xE3263A);
      K.box(cx - 0.33f * u, cy - 0.03f * u, cx + 0.33f * u, cy + 0.03f * u, 0.0f, 0x1A1A22);
      disc(P, cx, cy, 0.10f * u, 0x1A1A22);
      disc(P, cx, cy, 0.065f * u, 0xFFFFFF);
      disc(P, cx - 0.14f * u, cy - 0.18f * u, 0.05f * u, 0xFFFFFF, 0.45f);
      break;
    }
    case 3: {  // a shield + star
      K.poly({cx - 0.27f * u, cy - 0.28f * u, cx + 0.27f * u, cy - 0.28f * u, cx + 0.27f * u, cy + 0.02f * u, cx, cy + 0.32f * u, cx - 0.27f * u, cy + 0.02f * u}, 0xE01E2E);
      K.poly({cx - 0.19f * u, cy - 0.21f * u, cx + 0.19f * u, cy - 0.21f * u, cx + 0.19f * u, cy + 0.0f * u, cx, cy + 0.22f * u, cx - 0.19f * u, cy + 0.0f * u}, 0x2A5BD7);
      star(P, cx, cy - 0.02f * u, 0.15f * u, 0.065f * u, 0xFFFFFF);
      break;
    }
    case 4: {  // a puzzle piece
      K.box(cx - 0.22f * u, cy - 0.18f * u, cx + 0.18f * u, cy + 0.22f * u, 0.04f * u, 0x39D47A, 1.0f, 0x1E8A4A);
      disc(P, cx - 0.02f * u, cy - 0.2f * u, 0.085f * u, 0x39D47A);
      disc(P, cx + 0.2f * u, cy + 0.02f * u, 0.085f * u, 0x2FB868);
      disc(P, cx - 0.22f * u, cy + 0.02f * u, 0.07f * u, xrmenu::mix(c1, 0x000000, 0.45f));
      K.ring(cx - 0.22f * u, cy - 0.18f * u, cx + 0.18f * u, cy + 0.22f * u, 0.04f * u, 0.02f * u, 0xBFFFD8, 0.5f);
      break;
    }
    case 5: {  // arrows: up (green) + down (orange) - a head + a shaft each
      K.poly({cx - 0.33f * u, cy - 0.04f * u, cx + 0.01f * u, cy - 0.04f * u, cx - 0.16f * u, cy - 0.31f * u}, 0x39FF88);
      K.box(cx - 0.22f * u, cy - 0.06f * u, cx - 0.10f * u, cy + 0.27f * u, 0.01f * u, 0x39FF88, 1.0f, 0x1E9E50);
      K.poly({cx - 0.01f * u, cy + 0.04f * u, cx + 0.33f * u, cy + 0.04f * u, cx + 0.16f * u, cy + 0.31f * u}, 0xFF8A1E);
      K.box(cx + 0.10f * u, cy - 0.27f * u, cx + 0.22f * u, cy + 0.06f * u, 0.01f * u, 0xFFB050, 1.0f, 0xFF8A1E);
      break;
    }
    case 6: {  // a retro star badge with a sunburst
      for (int i = 0; i < 16; ++i) {
        const float a0 = 6.2831853f * i / 16.0f, a1 = a0 + 6.2831853f / 32.0f;
        K.poly({cx, cy, cx + 0.40f * u * std::cos(a0), cy + 0.40f * u * std::sin(a0), cx + 0.40f * u * std::cos(a1), cy + 0.40f * u * std::sin(a1)}, c2, 0.22f);
      }
      disc(P, cx, cy, 0.24f * u, 0x8A4FD8, 1.0f, 0x4A1F8A);
      K.ring(cx - 0.24f * u, cy - 0.24f * u, cx + 0.24f * u, cy + 0.24f * u, 0.24f * u, 0.025f * u, 0xFFE3A8);
      star(P, cx, cy, 0.17f * u, 0.07f * u, 0xFFE3A8);
      break;
    }
    case 7: {  // a cut gem
      K.poly({cx - 0.30f * u, cy - 0.08f * u, cx - 0.16f * u, cy - 0.26f * u, cx + 0.16f * u, cy - 0.26f * u, cx + 0.30f * u, cy - 0.08f * u}, 0xB8FCFF);
      K.poly({cx - 0.30f * u, cy - 0.08f * u, cx + 0.30f * u, cy - 0.08f * u, cx, cy + 0.32f * u}, 0x3ED8F0);
      K.poly({cx - 0.10f * u, cy - 0.08f * u, cx + 0.10f * u, cy - 0.08f * u, cx, cy + 0.32f * u}, 0xFFFFFF, 0.35f);
      K.poly({cx - 0.16f * u, cy - 0.26f * u, cx + 0.16f * u, cy - 0.26f * u, cx + 0.08f * u, cy - 0.08f * u, cx - 0.08f * u, cy - 0.08f * u}, 0xFFFFFF, 0.4f);
      star(P, cx + 0.22f * u, cy - 0.26f * u, 0.07f * u, 0.015f * u, 0xFFFFFF, 0.9f, 4);
      break;
    }
    // ---- r235: the eleven's
    case 9: {  // a fan of three cards (the set chaser)
      for (int i = 0; i < 3; ++i) {
        const float dx = (i - 1) * 0.13f * u, dy = std::fabs(static_cast<float>(i - 1)) * 0.04f * u;
        K.box(cx + dx - 0.14f * u, cy + dy - 0.22f * u, cx + dx + 0.14f * u, cy + dy + 0.20f * u, 0.03f * u, i == 1 ? c2 : xrmenu::mix(c2, 0x000000, 0.35f), 1.0f,
              xrmenu::mix(c2, 0x000000, 0.6f));
        K.ring(cx + dx - 0.14f * u, cy + dy - 0.22f * u, cx + dx + 0.14f * u, cy + dy + 0.20f * u, 0.03f * u, 0.015f * u, 0xFFFFFF, 0.7f);
      }
      K.text("A", cx, cy - 0.01f * u, 0.2f * u, 0.3f * u, 0x10324A, 0.0f);
      break;
    }
    case 10: {  // a '96 sticker on a burst (the Jam completist)
      star(P, cx, cy, 0.36f * u, 0.24f * u, 0x18C6C0, 0.95f, 10);
      disc(P, cx, cy, 0.22f * u, 0xE0218A, 1.0f, 0x7A0A4A);
      K.text("'96", cx, cy + 0.01f * u, 0.2f * u, 0.4f * u, 0xFFD400, 2.0f);
      break;
    }
    case 11: {  // a black NOIR card, gold frame, EXQ
      K.box(cx - 0.2f * u, cy - 0.28f * u, cx + 0.2f * u, cy + 0.28f * u, 0.03f * u, 0x0C0C0E);
      K.ring(cx - 0.2f * u, cy - 0.28f * u, cx + 0.2f * u, cy + 0.28f * u, 0.03f * u, 0.03f * u, 0xE6C774);
      K.text("EXQ", cx, cy, 0.14f * u, 0.36f * u, 0xE6C774, 0.0f);
      break;
    }
    case 12: {  // a smash ball (a ring + its cross)
      disc(P, cx, cy, 0.3f * u, 0xFF5A2A, 1.0f, 0xB0300A);
      K.box(cx - 0.3f * u, cy - 0.03f * u, cx + 0.3f * u, cy + 0.03f * u, 0.0f, 0xFFFFFF, 0.9f);
      K.box(cx - 0.03f * u, cy - 0.3f * u, cx + 0.03f * u, cy + 0.3f * u, 0.0f, 0xFFFFFF, 0.9f);
      K.ring(cx - 0.3f * u, cy - 0.3f * u, cx + 0.3f * u, cy + 0.3f * u, 0.3f * u, 0.03f * u, 0xFFE0A0);
      break;
    }
    case 13: {  // a club scarf: a shield in stripes
      K.poly({cx - 0.25f * u, cy - 0.26f * u, cx + 0.25f * u, cy - 0.26f * u, cx + 0.25f * u, cy + 0.04f * u, cx, cy + 0.3f * u, cx - 0.25f * u, cy + 0.04f * u}, c2);
      for (int i = 0; i < 2; ++i)
        K.box(cx - 0.25f * u, cy - 0.16f * u + i * 0.16f * u, cx + 0.25f * u, cy - 0.10f * u + i * 0.16f * u, 0.0f, xrmenu::mix(c1, 0x000000, 0.2f), 0.9f);
      break;
    }
    case 14: {  // a bar chart going up
      for (int i = 0; i < 3; ++i) K.box(cx - 0.27f * u + i * 0.19f * u, cy + 0.25f * u - (0.18f + 0.14f * i) * u, cx - 0.13f * u + i * 0.19f * u, cy + 0.25f * u, 0.02f * u, i == 2 ? c2 : xrmenu::mix(c2, 0x000000, 0.4f));
      K.box(cx - 0.32f * u, cy + 0.25f * u, cx + 0.32f * u, cy + 0.28f * u, 0.0f, 0xFFFFFF, 0.8f);
      break;
    }
    case 15: {  // a price tag, %
      K.poly({cx - 0.3f * u, cy - 0.2f * u, cx + 0.16f * u, cy - 0.2f * u, cx + 0.32f * u, cy, cx + 0.16f * u, cy + 0.2f * u, cx - 0.3f * u, cy + 0.2f * u}, c2);
      disc(P, cx + 0.14f * u, cy, 0.04f * u, xrmenu::mix(c1, 0x000000, 0.4f));
      K.text("%", cx - 0.08f * u, cy + 0.01f * u, 0.24f * u, 0.3f * u, xrmenu::mix(c1, 0x000000, 0.4f), 0.0f);
      break;
    }
    case 16: {  // a loupe
      K.ring(cx - 0.24f * u, cy - 0.26f * u, cx + 0.12f * u, cy + 0.10f * u, 0.18f * u, 0.05f * u, c2);
      disc(P, cx - 0.06f * u, cy - 0.08f * u, 0.13f * u, 0xFFFFFF, 0.25f);
      stroke(P, {cx + 0.08f * u, cy + 0.08f * u, cx + 0.28f * u, cy + 0.28f * u}, 0.08f * u, c2);
      break;
    }
    case 17: {  // the RC shield
      K.poly({cx - 0.24f * u, cy - 0.26f * u, cx + 0.24f * u, cy - 0.26f * u, cx + 0.24f * u, cy + 0.06f * u, cx, cy + 0.3f * u, cx - 0.24f * u, cy + 0.06f * u}, c2);
      K.text("RC", cx, cy - 0.02f * u, 0.22f * u, 0.4f * u, xrmenu::mix(c1, 0x000000, 0.3f), 0.0f);
      break;
    }
    case 18: {  // #1
      disc(P, cx, cy, 0.28f * u, xrmenu::mix(c2, 0x000000, 0.25f), 1.0f, xrmenu::mix(c2, 0x000000, 0.6f));
      K.text("#1", cx, cy + 0.01f * u, 0.28f * u, 0.5f * u, 0xFFFFFF, 2.0f);
      break;
    }
    case 19: {  // a tower of coins
      for (int i = 0; i < 4; ++i) {
        const float y = cy + 0.2f * u - i * 0.12f * u;
        K.box(cx - 0.24f * u, y - 0.07f * u, cx + 0.24f * u, y + 0.07f * u, 0.07f * u, 0xE0A800, 1.0f, 0x8A6000);
        K.ring(cx - 0.24f * u, y - 0.07f * u, cx + 0.24f * u, y + 0.07f * u, 0.07f * u, 0.015f * u, 0xFFE07A, 0.8f);
      }
      K.text("B", cx, cy - 0.17f * u, 0.12f * u, 0.2f * u, 0x5A3A00, 0.0f);
      break;
    }
    default: {  // a lightning bolt (two convex pieces + a glint)
      K.poly({cx + 0.02f * u, cy - 0.35f * u, cx + 0.21f * u, cy - 0.35f * u, cx + 0.05f * u, cy + 0.04f * u, cx - 0.17f * u, cy + 0.04f * u}, 0xFFE94A);
      K.poly({cx - 0.07f * u, cy - 0.05f * u, cx + 0.23f * u, cy - 0.05f * u, cx - 0.11f * u, cy + 0.37f * u}, 0xFFD000);
      K.poly({cx + 0.04f * u, cy - 0.33f * u, cx + 0.12f * u, cy - 0.33f * u, cx - 0.04f * u, cy + 0.0f * u}, 0xFFFFFF, 0.5f);
      break;
    }
  }
}
// a collector's portrait card (the TIZM card look: foil frame, prism, his emblem in the art window, his name plate)
void portrait(Pen& P, int c, float x0, float y0, float x1, float y1, bool big, float t) {
  const Persona& p = kP[c];
  const float w = x1 - x0, h = y1 - y0, r = 0.07f * w;
  P.K.glow(x0 - 0.15f * w, y0 - 0.1f * h, x1 + 0.15f * w, y1 + 0.1f * h, p.c2, big ? 0.22f + 0.05f * std::sin(t * 2.0f) : 0.12f);
  P.K.box(x0, y0, x1, y1, r, xrmenu::mix(p.c1, 0x000000, 0.35f), 1.0f, xrmenu::mix(p.c1, 0x000000, 0.8f));
  P.K.ring(x0, y0, x1, y1, r, std::max(2.0f, 0.02f * w), p.c2, 0.95f);
  const float m = 0.07f * w, ay1 = y0 + 0.68f * h;
  P.K.box(x0 + m, y0 + m, x1 - m, ay1, r * 0.6f, xrmenu::mix(p.c1, 0xFFFFFF, 0.08f), 1.0f, xrmenu::mix(p.c1, 0x000000, 0.6f));
  static const uint32_t kBand[6] = {0xFF2E88, 0xFFB020, 0xFFE94A, 0x39FF88, 0x2EF2FF, 0x9B5CFF};
  for (int i = 0; i < 5; ++i) {  // (the prism)
    const float a = x0 + m + (w - 2 * m) * (i * 0.21f), b = a + 0.08f * w;
    P.K.poly({a, ay1, b, ay1, b + 0.22f * w, y0 + m, a + 0.22f * w, y0 + m}, kBand[i % 6], 0.12f);
  }
  P.K.glow(x0 + 0.1f * w, y0 + 0.08f * h, x1 - 0.1f * w, ay1, p.c2, 0.3f);
  emblem(P, p.emblem, 0.5f * (x0 + x1), 0.5f * (y0 + m + ay1), std::min(w - 2 * m, ay1 - y0 - m) * 0.78f, p.c1, p.c2);
  P.K.box(x0 + m * 0.6f, ay1 + 0.02f * h, x1 - m * 0.6f, y1 - m * 0.6f, r * 0.5f, 0x0A0C14, 0.92f);
  P.K.text(p.name, 0.5f * (x0 + x1), ay1 + 0.105f * h, std::min(0.085f * h, 0.11f * w), w - 2.4f * m, 0xFFFFFF, big ? 2.0f : 0.0f);
  P.K.text(p.handle, 0.5f * (x0 + x1), ay1 + 0.2f * h, std::min(0.055f * h, 0.075f * w), w - 2.4f * m, p.c2, 0.0f);
  // the kind on a ribbon across the art's top corner
  const float kpx = std::min(0.045f * h, 0.062f * w), kw = xrtext::board_text_width(p.kind, kpx) + kpx * 1.4f;
  P.K.box(x0 + m * 0.5f, y0 + m * 0.5f, x0 + m * 0.5f + kw, y0 + m * 0.5f + kpx * 1.5f, kpx * 0.75f, p.c2, 0.95f);
  P.K.text(p.kind, x0 + m * 0.5f + 0.5f * kw, y0 + m * 0.5f + kpx * 0.78f, kpx, kw, xrmenu::mix(p.c1, 0x000000, 0.4f), 0.0f);
}
// text wrapped into the room (up to `maxLines` lines)
std::vector<std::string> wrap(const std::string& t, float px, float room, int maxLines) {
  std::vector<std::string> out(1);
  size_t at = 0;
  while (at <= t.size()) {
    size_t sp = t.find(' ', at);
    if (sp == std::string::npos) sp = t.size();
    const std::string word = t.substr(at, sp - at), tryLine = out.back().empty() ? word : out.back() + " " + word;
    if (!out.back().empty() && xrtext::board_text_width(tryLine, px) > room && static_cast<int>(out.size()) < maxLines) out.push_back(word);
    else out.back() = tryLine;
    at = sp + 1;
  }
  return out;
}
void pill(Pen& P, const std::string& t, float x, float cy, float px, uint32_t rgb) {
  const float tw = xrtext::board_text_width(t, px) + px * 1.2f;
  P.K.box(x, cy - px * 0.62f, x + tw, cy + px * 0.62f, px * 0.62f, xrmenu::mix(rgb, 0x000000, 0.72f));
  P.K.ring(x, cy - px * 0.62f, x + tw, cy + px * 0.62f, px * 0.62f, 2.0f, rgb, 0.9f);
  P.K.text(t, x + 0.5f * tw, cy + 1, px, tw, rgb, 0.0f);
}
float pill_w(const std::string& t, float px) { return xrtext::board_text_width(t, px) + px * 1.2f; }
uint32_t mood_rgb(double m) { return m > 0.1 ? kUp : (m < -0.1 ? kDown : kDim); }
uint32_t rapport_rgb(double g) { return g > 0.15 ? kUp : (g < -0.15 ? kDown : kDim); }
// the rapport as a little bar (-1 .. +1, the middle marked)
void rapport_bar(Pen& P, double g, float x0, float cy, float w) {
  P.K.box(x0, cy - 6, x0 + w, cy + 6, 6, 0x222A44);
  const float mid = x0 + 0.5f * w, at = mid + 0.5f * w * static_cast<float>(clampd(g, -1, 1));
  P.K.box(std::min(mid, at), cy - 6, std::max(mid, at) + 1, cy + 6, 6, rapport_rgb(g));
  P.K.box(mid - 1.5f, cy - 10, mid + 1.5f, cy + 10, 0, 0xFFFFFF, 0.6f);
}
// a speech bubble from collector c (his emblem, his last word)
void bubble(Pen& P, int c, const std::string& line, int kind, float x0, float y0, float x1, float y1) {
  const uint32_t col = kind == kLAccept ? kUp : (kind == kLCounter ? kAmber : (kind == kLDecline || kind == kLLowball || kind == kLNotInto ? kDown : kCyan));
  emblem(P, kP[c].emblem, x0 + 0.5f * (y1 - y0), 0.5f * (y0 + y1), (y1 - y0) * 0.9f, kP[c].c1, kP[c].c2);
  const float bx0 = x0 + (y1 - y0) + 14;
  P.K.box(bx0, y0, x1, y1, 22, 0x0E1430, 0.95f);
  P.K.ring(bx0, y0, x1, y1, 22, 3, col, 0.9f);
  P.K.poly({bx0 + 2, 0.5f * (y0 + y1) - 10, bx0 - 14, 0.5f * (y0 + y1), bx0 + 2, 0.5f * (y0 + y1) + 10}, col, 0.9f);
  const std::vector<std::string> ls = wrap("\"" + line + "\"", 24, x1 - bx0 - 30, 2);
  const float lh = 28, top = 0.5f * (y0 + y1) - 0.5f * lh * (ls.size() - 1);
  for (size_t i = 0; i < ls.size(); ++i) P.K.left(ls[i], bx0 + 16, top + i * lh + 1, 24, x1 - bx0 - 30, kInk, 0.0f);
}
void glyph_label(Pen& P, const std::string& g, const std::string& t, float x, float cy, float px, uint32_t rgb) {
  xrmenu::glyph(P.K, g, x, cy, px * 1.5f);
  P.K.left(t, x + px * 1.75f, cy + 1, px, 400, rgb, 0.0f);
}
long long snap_px(const Snap& S, const Variant& v) {
  const std::string s = xrmarket::variant_sig(g_set, v);
  for (const MineRow& r : S.mineRows)
    if (xrmarket::variant_sig(g_set, r.h) == s) return r.px;
  return xrmarket::price(v);
}
long long snap_item_px(const Snap& S, const Item& it) {
  auto f = S.px.find(it.id);
  return f != S.px.end() ? f->second : xrmarket::price(it.v);
}
const Item* snap_item(const Snap& S, int c, uint32_t id) {
  for (const Item& it : S.st.col[c].items)
    if (it.id == id) return &it;
  return nullptr;
}
// the trade-value meter: both sides at this week's market
void meter(Pen& P, long long give, long long get, float x0, float y0, float x1, float y1, bool small) {
  const double tot = static_cast<double>(give) + static_cast<double>(get);
  const float split = tot > 0 ? x0 + (x1 - x0) * static_cast<float>(static_cast<double>(give) / tot) : 0.5f * (x0 + x1);
  const float r = 0.5f * (y1 - y0);
  P.K.box(x0, y0, x1, y1, r, 0x0A0E1E);
  if (give > 0) P.K.box(x0, y0, std::max(x0 + 2 * r, split), y1, r, 0x1E8AB8, 1.0f, 0x0E4A70);
  if (get > 0) P.K.box(std::min(x1 - 2 * r, split), y0, x1, y1, r, 0xE0A800, 1.0f, 0x8A6000);
  P.K.box(split - 2, y0 - 6, split + 2, y1 + 6, 0, 0xFFFFFF, 0.9f);
  P.K.ring(x0, y0, x1, y1, r, 2, kLine);
  const float px = small ? 18.0f : 20.0f;
  P.K.left("YOU GIVE " + money(give, px, 0.3f * (x1 - x0)), x0 + 10, 0.5f * (y0 + y1) + 1, px, 0.45f * (x1 - x0), 0xFFFFFF, 2.0f);
  P.K.right("YOU GET " + money(get, px, 0.3f * (x1 - x0)), x1 - 10, 0.5f * (y0 + y1) + 1, px, 0.45f * (x1 - x0), 0xFFFFFF, 2.0f);
}
std::string ratio_words(long long give, long long get, uint32_t* rgb) {
  if (give <= 0 && get <= 0) {
    *rgb = kDim;
    return "AN EMPTY OFFER";
  }
  if (give <= 0) {
    *rgb = kUp;
    return "YOU GIVE NOTHING?";
  }
  const double r = static_cast<double>(get) / static_cast<double>(give);
  *rgb = r > 1.05 ? kUp : (r >= 0.95 ? kGoldC : kDown);
  if (r >= 0.995 && r <= 1.005) return "AN EVEN TRADE AT MARKET";
  return "YOU GET " + std::to_string(static_cast<int>(std::lround(std::min(99999.0, r * 100.0)))) + "% OF WHAT YOU GIVE";
}
// a row of up to 5 card slots (minis, empty dashed slots) + the coins chip
void side_row(Pen& P, const Snap& S, const std::vector<Variant>& cards, long long coins, float x0, float y0, float x1, float h, uint32_t accent) {
  const float sw = h * 0.72f, gap = 10;
  for (int i = 0; i < kMaxSide; ++i) {
    const float sx0 = x0 + i * (sw + gap), sx1 = sx0 + sw;
    if (i < static_cast<int>(cards.size())) {
      xrmarket::draw_mini(P.v, *P.keep, cards[static_cast<size_t>(i)], sx0, y0 + (cards[static_cast<size_t>(i)].grade > 0 ? 0.11f * h : 0.0f), sx1, y0 + h);
    } else {
      P.K.box(sx0, y0, sx1, y0 + h, 10, 0x0A0E1E, 0.8f);
      P.K.ring(sx0, y0, sx1, y0 + h, 10, 2, accent, 0.25f);
      P.K.text("+", 0.5f * (sx0 + sx1), y0 + 0.5f * h, 30, sw, accent, 0.0f);
    }
  }
  (void)S;
  const float cx0 = x0 + kMaxSide * (sw + gap) + 4;
  if (cx0 < x1 - 60) {
    P.K.box(cx0, y0 + 0.2f * h, x1, y0 + 0.8f * h, 0.3f * h, coins > 0 ? 0x2A2008 : 0x0A0E1E, 1.0f);
    P.K.ring(cx0, y0 + 0.2f * h, x1, y0 + 0.8f * h, 0.3f * h, 2, coins > 0 ? kGoldC : kLine, 0.8f);
    coin(P, cx0 + 0.3f * h, y0 + 0.5f * h, 0.13f * h);
    P.K.text(coins > 0 ? "+" + money(coins, 26, x1 - cx0 - 0.6f * h) : "NO COINS", 0.5f * (cx0 + 0.55f * h + x1), y0 + 0.5f * h + 1, coins > 0 ? 26 : 20,
             x1 - cx0 - 0.6f * h, coins > 0 ? kGoldC : kDim, 0.0f);
  }
}

// ---- the tabs' pages
// r236: what a collector wants, in words (the COLLECTORS page's and the hub's LOOK B panel's)
std::string wants_of(const Snap& S, int c) {
  const Col& k = S.st.col[c];
  std::string wants;
  if (c == kSetBuilder) {
    int done = 0;
    for (size_t i = 0; i < k.wants.size(); ++i) {
      const bool d = i < k.wantDone.size() && k.wantDone[i] != 0;
      done += d ? 1 : 0;
      if (!d) wants += (wants.empty() ? "" : ", ") + cname(k.wants[i]);
    }
    wants = k.wants.empty() ? "HIS SET IS DONE - A NEW ONE NEXT SEASON" : "MISSING: " + wants;
  } else {
    static const char* const kWant[kCollectors] = {"ONE-OF-ONES, AUTOS, BOOMS, THE LEAGUE'S TOP 5", "A BARGAIN", "POKEMON - SHINIES MOST OF ALL", "MARVEL - THE ALT SUITS MOST",
                                                   "", "A PROFIT", "THE STRIKERS + CHARGED ORIGINALS", "TIZM 9S AND 10S", "WHOEVER IS HOT THIS MATCHDAY",
                                                   // r235
                                                   "EVERY LANCINI TIZM CARD HE'S MISSING", "EVERY JAM '96 CARD SHE'S MISSING", "EXQUISITE - THE LOWER THE RUN THE BETTER",
                                                   "THE MELEE FIGHTERS", "", "THE GOLDEN BOOT + ASSISTS RACE - THE TOP 5", "ANYTHING UNDER THE MARKET",
                                                   "RAW CARDS WORTH GRADING", "ORIGINALS ROOKIES + RPAS", "01s, LAST NUMBERS, JERSEY NUMBERS, 1/1s", "THE MOST EXPENSIVE CARD IN THE ROOM"};
    wants = std::string("WANTS: ") + kWant[c];
    if (c == kLoyalist) {  // r235: SAL's club in this career
      const int sc = sal_club_of(S.st.career);
      wants = "WANTS: HIS CLUB'S PLAYERS - " + (PortFranchiseClubCount() > sc && PortFranchiseClubName(sc) != nullptr ? upper(PortFranchiseClubName(sc)) : std::string("HIS CLUB"));
    }
  }
  return wants;
}

// r236 (LOOK B): COLLECTORS in the hub - 7 rows (the emblem, the name, an OFFER / COUNTER tag when there is one, his cards: one
// number) and the focused one's panel: his portrait, name, @handle - kind, WANTS, mood, ONE button (OPEN DESK). Rapport, patience, the
// bio and his top card are the desk's (A).
void draw_collectors_b(Pen& P, const Snap& S, float t) {
  constexpr int kRowsB = 7;
  xrmenu::Kit& K = P.K;
  const int top = std::max(0, std::min(kCollectors - kRowsB, S.colCur - kRowsB / 2));
  const float lx1 = 970.0f, ry0 = 104.0f, rh = (898.0f - ry0 - (kRowsB - 1) * 10.0f) / kRowsB;
  for (int c = top; c < top + kRowsB && c < kCollectors; ++c) {
    const Col& k = S.st.col[c];
    const float y0 = ry0 + (c - top) * (rh + 10.0f), y1 = y0 + rh;
    xrmenu::TileSpec ts;
    ts.colour = c == S.colCur ? xrmenu::kBCards : 0x2F8CFF;
    ts.on = c == S.colCur;
    xrmenu::tile(K, 0, y0, lx1, y1, ts);
    emblem(P, kP[c].emblem, 56, 0.5f * (y0 + y1), 70, kP[c].c1, kP[c].c2);
    xrmenu::say(K, kP[c].name, 110, 0.5f * (y0 + y1), 46, 0xFFFFFF, 'l', 420);
    bool offer = false;
    for (const Incoming& i : S.st.inc) offer = offer || i.col == c;
    if (k.hasCounter) xrmenu::chip(K, "COUNTER", 640, 0.5f * (y0 + y1), 0x8A5A10, 'c', 30);
    else if (offer) xrmenu::chip(K, "OFFER", 640, 0.5f * (y0 + y1), 0x8E2A6A, 'c', 30);
    xrmenu::say(K, std::to_string(k.items.size()) + " CARDS", lx1 - 30, 0.5f * (y0 + y1), 40, xrmenu::kBGold, 'r', 220);
  }
  if (top > 0) K.poly({lx1 + 20, ry0 + 24, lx1 + 32, ry0, lx1 + 44, ry0 + 24}, xrmenu::kBGold);
  if (top + kRowsB < kCollectors) K.poly({lx1 + 20, 874, lx1 + 44, 874, lx1 + 32, 898}, xrmenu::kBGold);
  // the focused one
  const int c = S.colCur;
  const float fx0 = 1010, fx1 = kW, fcx = 0.5f * (fx0 + fx1);
  xrmenu::panel(K, fx0, ry0, fx1, 898, kP[c].c2 != 0xFFFFFF ? kP[c].c2 : kGoldC, 0.94f);
  K.glow(fcx - 200, ry0 + 20, fcx + 200, ry0 + 360, kGoldC, 0.25f);
  {
    const size_t art0 = P.v.size();
    portrait(P, c, fcx - 120, ry0 + 24, fcx + 120, ry0 + 300, true, t);
    for (size_t i = art0; i < P.v.size(); ++i) P.v[i].art = true;  // (his card: art)
  }
  xrmenu::say(K, kP[c].name, fcx, ry0 + 350, 56, 0xFFFFFF, 'c', fx1 - fx0 - 40, true, 0.0f, 3.0f);
  xrmenu::label(K, std::string(kP[c].handle) + "  -  " + kP[c].kind, fcx, ry0 + 404, 30, xrmenu::kBSoft, 'c', fx1 - fx0 - 40);
  std::string w = wants_of(S, c);
  if (w.compare(0, 7, "WANTS: ") == 0) w = w.substr(7);
  xrmenu::label(K, "WANTS", fx0 + 40, ry0 + 462, 30);
  {  // (two lines at most, 34 px - never shrunk under 30)
    const std::vector<std::string> wl = wrap(w, 34, fx1 - fx0 - 80, 2);
    for (size_t i = 0; i < wl.size(); ++i) xrmenu::say(K, wl[i], fx0 + 40, ry0 + 500 + i * 40.0f, 34, 0xFFFFFF, 'l', fx1 - fx0 - 80);
  }
  xrmenu::label(K, "MOOD", fx0 + 40, ry0 + 590, 30);
  xrmenu::say(K, mood_word(S.mood[c]), fx0 + 40, ry0 + 632, 40, mood_rgb(S.mood[c]), 'l', fx1 - fx0 - 80);
  xrmenu::button(K, fx0 + 30, 898 - 120, fx1 - 30, 898 - 24, "OPEN DESK", "A", true, true);
}
void draw_collectors(Pen& P, const Snap& S, float t) {
  // r235: twenty collectors - nine rows at a time, the list scrolling with the ring
  constexpr int kRowsShown = 9;
  const int top = std::max(0, std::min(kCollectors - kRowsShown, S.colCur - kRowsShown / 2));
  if (top > 0) P.K.poly({1000, 190, 1010, 180, 1020, 190}, kCyan, 0.9f);
  if (top + kRowsShown < kCollectors) P.K.poly({1000, 880, 1010, 890, 1020, 880}, kCyan, 0.9f);
  for (int c = top; c < top + kRowsShown && c < kCollectors; ++c) {
    const Col& k = S.st.col[c];
    const float y0 = 196 + (c - top) * 76.0f, y1 = y0 + 70;
    const bool on = c == S.colCur;
    P.K.box(0, y0, 1010, y1, 20, on ? kPanelHi : kPanel);
    if (on) xrmenu::focus(P.K, 0, y0, 1010, y1, 20);
    P.K.box(0, y0 + 10, 6, y1 - 10, 3, kP[c].c2, 0.9f);
    emblem(P, kP[c].emblem, 50, 0.5f * (y0 + y1), 56, kP[c].c1, kP[c].c2);
    P.K.left(kP[c].name, 92, y0 + 26, 30, 300, kInk, 2.0f);
    P.K.left(std::string(kP[c].handle) + "  -  " + kP[c].kind, 92, y0 + 54, 20, 320, kP[c].c2 == 0xFFFFFF ? 0xFFB6C0 : kP[c].c2, 0.0f);
    const std::string mw = mood_word(S.mood[c]);
    pill(P, mw, 430, 0.5f * (y0 + y1), 20, mood_rgb(S.mood[c]));
    P.K.left(rapport_word(k.goodwill), 580, y0 + 24, 18, 170, rapport_rgb(k.goodwill), 0.0f);
    rapport_bar(P, k.goodwill, 580, y0 + 50, 150);
    int blacks = 0;
    for (const Item& it : k.items) blacks += it.v.parallel == kBlack ? 1 : 0;
    P.K.left(std::to_string(k.items.size()) + " CARDS", 760, y0 + 26, 22, 120, kInk, 0.0f);
    P.K.left(blacks > 0 ? (blacks == 1 ? std::string("1 BLACK 1/1") : "BLACK 1/1 X" + std::to_string(blacks)) : std::string("NO 1/1"), 760, y0 + 52, 18, 120, blacks > 0 ? kGoldC : kDim, 0.0f);
    bool offer = false;
    for (const Incoming& i : S.st.inc) offer = offer || i.col == c;
    if (k.hasCounter) {
      P.K.box(880, y0 + 18, 1000, y1 - 18, 17, 0x4A2A08);
      P.K.text("COUNTER", 940, 0.5f * (y0 + y1) + 1, 18, 112, kAmber, 0.0f);
    } else if (offer) {
      P.K.box(880, y0 + 18, 1000, y1 - 18, 17, 0x4A1440);
      P.K.text("OFFER", 940, 0.5f * (y0 + y1) + 1, 20, 112, kPink, 0.0f);
    }
  }
  // the one under the ring, big
  const int c = S.colCur;
  const Col& k = S.st.col[c];
  const float X0 = 1030, X1 = kW;
  P.K.box(X0, 196, X1, 890, 26, 0x0E152C);
  P.K.ring(X0, 196, X1, 890, 26, 2, kLine);
  portrait(P, c, X0 + 24, 216, X0 + 254, 536, true, t);
  P.K.left(kP[c].name, X0 + 274, 248, 34, X1 - X0 - 290, kInk, 2.0f);
  P.K.left(kP[c].handle, X0 + 274, 284, 24, X1 - X0 - 290, kP[c].c2 == 0xFFFFFF ? 0xFFB6C0 : kP[c].c2, 0.0f);
  pill(P, kP[c].kind, X0 + 274, 322, 20, kP[c].c2 == 0xFFFFFF ? 0xFF6A7A : kP[c].c2);
  P.K.left("MOOD", X0 + 274, 366, 18, 100, kDim, 0.0f);
  P.K.left(std::string(mood_word(S.mood[c])) + (S.cycle > 0.08 ? "  (MARKET HOT)" : (S.cycle < -0.06 ? "  (MARKET COOL)" : "")), X0 + 274, 392, 22, X1 - X0 - 290,
           mood_rgb(S.mood[c]), 0.0f);
  P.K.left("RAPPORT", X0 + 274, 428, 18, 120, kDim, 0.0f);
  P.K.left(rapport_word(k.goodwill), X0 + 274, 454, 22, 200, rapport_rgb(k.goodwill), 0.0f);
  rapport_bar(P, k.goodwill, X0 + 274, 480, X1 - X0 - 320);
  P.K.left("PATIENCE TODAY", X0 + 274, 512, 18, 200, kDim, 0.0f);
  for (int i = 0; i < S.patienceMax[c]; ++i) disc(P, X0 + 290 + i * 30.0f, 536, 10, i < k.patience ? kCyan : 0x2A3150);
  const std::vector<std::string> bio = wrap(kP[c].bio, 22, X1 - X0 - 60, 4);
  for (size_t i = 0; i < bio.size(); ++i) P.K.left(bio[i], X0 + 30, 576 + i * 28.0f, 22, X1 - X0 - 60, 0xC4CCE4, 0.0f);
  // what he wants / holds
  const std::string wants = wants_of(S, c);
  P.K.left(wants, X0 + 30, 702, 22, X1 - X0 - 60, kGoldC, 0.0f);
  // his best card
  const Item* best = k.items.empty() ? nullptr : &k.items[0];
  if (best != nullptr) {
    xrmarket::draw_mini(P.v, *P.keep, best->v, X0 + 30, 730 + (best->v.grade > 0 ? 10 : 0), X0 + 30 + 64, 820);
    P.K.left(HISs(c, " TOP CARD"), X0 + 110, 748, 18, 200, kDim, 0.0f);
    P.K.left(short_words(best->v), X0 + 110, 776, 22, X1 - X0 - 140, kInk, 0.0f);
    P.K.left(money(snap_item_px(S, *best), 26, 300) + " COINS", X0 + 110, 806, 24, X1 - X0 - 140, kGoldC, 0.0f);
  }
  P.K.box(X0 + 24, 832, X1 - 24, 880, 24, 0x0F4A2A, 1.0f, 0x0A2E1A);
  P.K.ring(X0 + 24, 832, X1 - 24, 880, 24, 3, kUp);
  xrmenu::glyph(P.K, "A", X0 + 36, 856, 36);
  P.K.text("OPEN " + HISs(c, " DESK - TRADE"), 0.5f * (X0 + X1) + 20, 857, 24, X1 - X0 - 140, 0xFFFFFF, 2.0f);
}
void draw_desk(Pen& P, const Snap& S, float t) {
  const int c = S.colCur;
  const Col& k = S.st.col[c];
  // the banner
  P.K.box(0, 196, kW, 262, 22, xrmenu::mix(kP[c].c1, 0x000000, 0.3f), 1.0f, xrmenu::mix(kP[c].c1, 0x000000, 0.75f));
  P.K.ring(0, 196, kW, 262, 22, 2, kP[c].c2, 0.8f);
  emblem(P, kP[c].emblem, 40, 229, 54, kP[c].c1, kP[c].c2);
  P.K.left(kP[c].name + std::string("'S DESK"), 80, 222, 30, 420, kInk, 2.0f);
  P.K.left(std::string(kP[c].handle) + "  -  " + kP[c].kind, 80, 250, 18, 420, kP[c].c2 == 0xFFFFFF ? 0xFFB6C0 : kP[c].c2, 0.0f);
  pill(P, mood_word(S.mood[c]), 560, 229, 20, mood_rgb(S.mood[c]));
  P.K.left(rapport_word(k.goodwill), 720, 218, 18, 180, rapport_rgb(k.goodwill), 0.0f);
  rapport_bar(P, k.goodwill, 720, 242, 150);
  P.K.left("PATIENCE", 920, 218, 18, 140, kDim, 0.0f);
  for (int i = 0; i < S.patienceMax[c]; ++i) disc(P, 930 + i * 24.0f, 244, 8, i < k.patience ? kCyan : 0x2A3150);
  xrmenu::glyph(P.K, "B", kW - 150, 229, 36);
  P.K.left("BACK", kW - 108, 230, 22, 100, kDim, 0.0f);
  // the pane tabs
  const std::string kPane[3] = {HISs(c, " BINDER"), "YOUR CARDS", "COINS"};
  const int counts[3] = {static_cast<int>(k.items.size()), static_cast<int>(S.mineRows.size()), -1};
  for (int i = 0; i < 3; ++i) {
    const float tx0 = i * 240.0f, tx1 = tx0 + 230;
    const bool on = S.pane == i;
    if (on) P.K.glow(tx0 - 14, 262, tx1 + 14, 326, kCyan, 0.25f);
    P.K.box(tx0, 274, tx1, 316, 21, on ? 0x153A60 : 0x0F1630);
    P.K.ring(tx0, 274, tx1, 316, 21, on ? 3.0f : 2.0f, on ? kCyan : kLine);
    P.K.text(kPane[i] + (counts[i] >= 0 ? " (" + std::to_string(counts[i]) + ")" : ""), 0.5f * (tx0 + tx1), 296, 22, 220, on ? 0xFFFFFF : kDim,
             on ? 2.0f : 0.0f);
  }
  xrmenu::glyph(P.K, "X", 730, 295, 32);
  P.K.left("SWITCH", 764, 296, 18, 80, kDim, 0.0f);
  const Offer& d = S.draft;
  if (S.pane == kPaneCoins) {
    const std::string kBtn[3] = {"YOU ADD COINS", HEs(c, " ADDS COINS"), "CLEAR THE OFFER"};
    for (int i = 0; i < 3; ++i) {
      const float y0 = 352 + i * 150.0f, y1 = y0 + 130;
      const bool on = S.coinCur == i;
      P.K.box(20, y0, 990, y1, 30, on ? kPanelHi : kPanel);
      if (on) xrmenu::focus(P.K, 20, y0, 990, y1, 30);
      if (i < 2) coin(P, 90, 0.5f * (y0 + y1), 34);
      else P.K.text("X", 90, 0.5f * (y0 + y1), 56, 80, kDown, 2.0f);
      P.K.left(kBtn[i], 150, y0 + 46, 34, 500, kInk, 2.0f);
      const long long v = i == 0 ? d.myCoins : (i == 1 ? d.theirCoins : 0);
      if (i < 2) P.K.right(v > 0 ? money(v, 48, 420) : "NONE", 960, 0.5f * (y0 + y1) + 2, 48, 430, v > 0 ? kGoldC : kDim, 2.0f);
      P.K.left(i == 0 ? "YOU HAVE " + money(S.balance, 22, 400) + " COINS" : (i == 1 ? HISs(c, " WALLET IS ") + HISs(c, " BUSINESS - ASK, ") + HEs(c, "'LL SAY") : std::string("EVERY CARD AND COIN OUT OF IT")),
               150, y0 + 92, 22, 600, kDim, 0.0f);
    }
    P.K.left("A  OPENS THE NUMBER PAD (OR POINT + TRIGGER ON ITS KEYS)", 20, 840, 22, 970, kDim, 0.0f);
  } else {
    const bool his = S.pane == kPaneHis;
    const int n = his ? static_cast<int>(k.items.size()) : static_cast<int>(S.mineRows.size());
    const int cur = S.paneCur[S.pane];
    int top = (cur / kGridCols - 1) * kGridCols;
    top = std::max(0, std::min(top, std::max(0, ((n + kGridCols - 1) / kGridCols - kGridRows) * kGridCols)));
    if (n == 0)
      P.K.text(his ? HISs(c, " BINDER IS EMPTY") : std::string("NO CARDS TO TRADE - OPEN SOME PACKS"), 500, 560, 40, 960, kDim, 2.0f);
    for (int i = 0; i < kGridCols * kGridRows && top + i < n; ++i) {
      const int at = top + i;
      const float cx0 = (i % kGridCols) * 200.0f, cy0 = 326 + (i / kGridCols) * 172.0f, cx1 = cx0 + 192, cy1 = cy0 + 164;
      const bool on = at == cur;
      Variant v;
      long long p = 0;
      int inOffer = 0, lock = 0, count = 1;
      if (his) {
        const Item& it = k.items[static_cast<size_t>(at)];
        v = it.v;
        p = snap_item_px(S, it);
        inOffer = std::find(d.theirs.begin(), d.theirs.end(), it.id) != d.theirs.end() ? 1 : 0;
      } else {
        const MineRow& r = S.mineRows[static_cast<size_t>(at)];
        v = r.h;
        p = r.px;
        count = r.h.count;
        const std::string s = xrmarket::variant_sig(g_set, v);
        for (const Variant& m : d.mine) inOffer += xrmarket::variant_sig(g_set, m) == s ? 1 : 0;
        lock = r.free <= 0 ? r.lock : 0;
      }
      P.K.box(cx0, cy0, cx1, cy1, 16, on ? kPanelHi : (inOffer > 0 ? 0x1A2A1A : kPanel));
      if (inOffer > 0) P.K.ring(cx0, cy0, cx1, cy1, 16, 3, kUp, 0.9f);
      if (on) xrmenu::focus(P.K, cx0, cy0, cx1, cy1, 16);
      const float mw = 78, mx0 = 0.5f * (cx0 + cx1) - 0.5f * mw;
      xrmarket::draw_mini(P.v, *P.keep, v, mx0, cy0 + 10 + (v.grade > 0 ? 12 : 0), mx0 + mw, cy0 + 118);
      if (lock != 0) {
        P.K.box(cx0 + 6, cy0 + 50, cx1 - 6, cy0 + 82, 10, 0x000000, 0.7f);
        P.K.text(lock == 1 ? "LISTED" : "IN A TRADE", 0.5f * (cx0 + cx1), cy0 + 66, 20, 170, kPink, 0.0f);
      }
      if (inOffer > 0) {
        disc(P, cx1 - 22, cy0 + 22, 15, kUp);
        P.K.text(inOffer > 1 ? std::to_string(inOffer) : "+", cx1 - 22, cy0 + 23, 20, 26, 0x062012, 0.0f);
      }
      if (count > 1) P.K.text("X" + std::to_string(count), cx0 + 26, cy0 + 22, 18, 40, kInk, 2.0f);
      P.K.text(finish_tag(v), 0.5f * (cx0 + cx1), cy0 + 132, 17, 184, v.parallel == kGold ? kGoldC : (v.parallel == kBlack ? 0xFFE9A0 : 0xB8C2E0), 0.0f);
      P.K.text(money(p, 24, 180), 0.5f * (cx0 + cx1), cy0 + 154, 24, 184, kInk, 2.0f);
    }
    if (n > kGridCols * kGridRows) {
      const int rows = (n + kGridCols - 1) / kGridCols;
      P.K.right("ROW " + std::to_string(top / kGridCols + 1) + "-" + std::to_string(std::min(rows, top / kGridCols + kGridRows)) + " OF " + std::to_string(rows),
                995, 296, 16, 145, kDim, 0.0f);
    }
    // the card under the cursor in words
    if (cur >= 0 && cur < n) {
      const Variant v = his ? k.items[static_cast<size_t>(cur)].v : static_cast<Variant>(S.mineRows[static_cast<size_t>(cur)].h);
      P.K.box(0, 848, 1000, 892, 18, 0x0A0E1E);
      P.K.left(xrmarket::card_name(v.card) + "  -  " + xrmarket::variant_words(v), 16, 871, 22, 690, kInk, 0.0f);
      P.K.right("MARKET " + money(his ? snap_item_px(S, k.items[static_cast<size_t>(cur)]) : S.mineRows[static_cast<size_t>(cur)].px, 22, 240), 990, 871, 22, 280,
                kGoldC, 0.0f);
    }
  }
  // THE OFFER
  const float X0 = 1016, X1 = kW;
  P.K.box(X0, 272, X1, 892, 26, 0x0E152C);
  P.K.ring(X0, 272, X1, 892, 26, 2, kLine);
  std::vector<Variant> theirs;
  long long theirsMkt = d.theirCoins, mineMkt = d.myCoins;
  for (uint32_t id : d.theirs)
    if (const Item* it = snap_item(S, c, id)) theirs.push_back(it->v), theirsMkt = xrcoin::add(theirsMkt, snap_item_px(S, *it));
  for (const Variant& m : d.mine) mineMkt = xrcoin::add(mineMkt, snap_px(S, m));
  gold_title(P, "THE OFFER", X0 + 22, 302, 30, 260);
  P.K.left("THEY GIVE", X0 + 22, 338, 20, 200, kGoldC, 0.0f);
  P.K.right("AT MARKET " + money(theirsMkt, 20, 260), X1 - 22, 338, 20, 340, kDim, 0.0f);
  side_row(P, S, theirs, d.theirCoins, X0 + 22, 352, X1 - 22, 104, kGoldC);
  P.K.left("YOU GIVE", X0 + 22, 486, 20, 200, kCyan, 0.0f);
  P.K.right("AT MARKET " + money(mineMkt, 20, 260), X1 - 22, 486, 20, 340, kDim, 0.0f);
  side_row(P, S, d.mine, d.myCoins, X0 + 22, 500, X1 - 22, 104, kCyan);
  P.K.left("TRADE VALUE AT THIS WEEK'S MARKET", X0 + 22, 632, 18, 420, kDim, 0.0f);
  meter(P, mineMkt, theirsMkt, X0 + 22, 646, X1 - 22, 680, false);
  uint32_t rc = kDim;
  const std::string rw = ratio_words(mineMkt, theirsMkt, &rc);
  P.K.text(rw, 0.5f * (X0 + X1), 706, 26, X1 - X0 - 40, rc, 2.0f);
  if (!k.say.empty()) bubble(P, c, k.say, k.sayKind, X0 + 18, 728, X1 - 18, 806);
  const bool can = (!d.mine.empty() || d.myCoins > 0) && (!d.theirs.empty() || d.theirCoins > 0) && (!d.mine.empty() || !d.theirs.empty());
  const bool counter = k.hasCounter;
  P.K.box(X0 + 22, 822, X1 - 22, 878, 28, counter ? 0x4A2A08 : (can ? 0x0F4A2A : 0x161C2C), 1.0f, counter ? 0x2A1804 : (can ? 0x0A2E1A : 0x10141F));
  P.K.ring(X0 + 22, 822, X1 - 22, 878, 28, 3, counter ? kAmber : (can ? kUp : kLine));
  xrmenu::glyph(P.K, "Y", X0 + 34, 850, 38);
  P.K.text(counter ? HISs(c, " COUNTER IS WAITING") : std::string(can ? "SEND OFFER" : "PICK CARDS / COINS TO SEND"), 0.5f * (X0 + X1) + 22, 851, 26, X1 - X0 - 120,
           can || counter ? 0xFFFFFF : kDim, 2.0f);
  (void)t;
}
void draw_counter(Pen& P, const Snap& S, float t) {
  const int c = S.colCur;
  const Col& k = S.st.col[c];
  const Offer& o = k.counter;
  P.K.box(0, 190, kW, 898, 26, 0x02040A, 0.75f);
  const float x0 = 170, x1 = kW - 170, y0 = 214, y1 = 880;
  P.K.glow(x0 - 60, y0 - 40, x1 + 60, y1 + 40, kAmber, 0.16f + 0.04f * std::sin(t * 3.0f));
  P.K.box(x0, y0, x1, y1, 30, 0x1A1208, 1.0f, 0x0B0804);
  P.K.ring(x0, y0, x1, y1, 30, 4, kAmber);
  chrome(P, "COUNTER-OFFER", x0 + 30, y0 + 46, 44, 600, -1);
  P.K.right(std::string("FROM ") + kP[c].handle, x1 - 30, y0 + 46, 26, 500, kP[c].c2 == 0xFFFFFF ? 0xFFB6C0 : kP[c].c2, 0.0f);
  std::vector<Variant> theirs;
  long long theirsMkt = o.theirCoins, mineMkt = o.myCoins;
  for (uint32_t id : o.theirs)
    if (const Item* it = snap_item(S, c, id)) theirs.push_back(it->v), theirsMkt = xrcoin::add(theirsMkt, snap_item_px(S, *it));
  for (const Variant& m : o.mine) mineMkt = xrcoin::add(mineMkt, snap_px(S, m));
  P.K.left(HEs(c, " GIVES"), x0 + 30, y0 + 96, 22, 200, kGoldC, 0.0f);
  P.K.right("AT MARKET " + money(theirsMkt, 22, 300), x1 - 30, y0 + 96, 22, 400, kDim, 0.0f);
  side_row(P, S, theirs, o.theirCoins, x0 + 30, y0 + 110, x1 - 30, 112, kGoldC);
  P.K.left("YOU GIVE", x0 + 30, y0 + 254, 22, 200, kCyan, 0.0f);
  P.K.right("AT MARKET " + money(mineMkt, 22, 300), x1 - 30, y0 + 254, 22, 400, kDim, 0.0f);
  side_row(P, S, o.mine, o.myCoins, x0 + 30, y0 + 268, x1 - 30, 112, kCyan);
  // what changed against your draft
  std::string changed;
  const Offer& d = S.draft;
  if (o.myCoins > d.myCoins) changed = HEs(c, " WANTS YOU TO ADD ") + T(o.myCoins - d.myCoins) + " COINS";
  else if (o.theirCoins < d.theirCoins) changed = HEs(c, " CUT ") + HISs(c, " COINS BY ") + T(d.theirCoins - o.theirCoins);
  if (o.theirs.size() < d.theirs.size()) changed += (changed.empty() ? "" : "  -  ") + HEs(c, " KEEPS ") + std::to_string(d.theirs.size() - o.theirs.size()) + " OF " + HISs(c, " CARDS");
  if (changed.empty()) changed = HISs(c, " TERMS");
  P.K.text(changed, 0.5f * (x0 + x1), y0 + 412, 28, x1 - x0 - 60, kPink, 2.0f);
  meter(P, mineMkt, theirsMkt, x0 + 30, y0 + 436, x1 - 30, y0 + 470, false);
  uint32_t rc = kDim;
  P.K.text(ratio_words(mineMkt, theirsMkt, &rc), 0.5f * (x0 + x1), y0 + 498, 24, x1 - x0 - 60, rc, 0.0f);
  if (!k.say.empty()) bubble(P, c, k.say, kLCounter, x0 + 30, y0 + 518, x1 - 30, y0 + 586);
  P.K.box(x0 + 20, y1 - 66, x1 - 20, y1 - 14, 26, 0x0A0804, 0.8f);
  const char* const kG[4] = {"A", "X", "Y", "B"};
  const char* const kW4[4] = {"ACCEPT", "DECLINE", "EDIT IT", "LATER"};
  for (int i = 0; i < 4; ++i) {
    const float bx = x0 + 40 + i * ((x1 - x0 - 80) / 4.0f);
    glyph_label(P, kG[i], kW4[i], bx, y1 - 40, 24, i == 0 ? kUp : kInk);
  }
}
void draw_offers(Pen& P, const Snap& S, float t) {
  if (!xrmenu::broadcast()) {  // (r236: always-on sentences - not in the hub: each offer says its days, the bar says Y)
    P.K.left("COLLECTORS WANT YOUR CARDS", 24, 214, 28, 700, kDim, 0.0f);
    P.K.right("THEIR OFFERS STAND 3 MATCHDAYS  -  Y ASKS FOR MORE COINS", kW - 24, 214, 24, 900, kDim, 0.0f);
  }
  const int n = static_cast<int>(S.st.inc.size());
  if (n == 0) {
    P.K.text("NO OFFERS RIGHT NOW", 825, 470, 64, 1400, kDim, 2.0f);
    P.K.text("EACH MATCHDAY A COLLECTOR MAY COME FOR A CARD HE LOVES - GRAILS, SHINIES, SLABS, THE ORIGINALS, WHOEVER IS HOT", 825, 550, 26, 1500, kDim, 0.0f);
    return;
  }
  for (int i = 0; i < n && i < kMaxIncoming; ++i) {
    const Incoming& in = S.st.inc[static_cast<size_t>(i)];
    const int c = in.col;
    const float y0 = 234 + i * 164.0f, y1 = y0 + 154;
    const bool on = i == S.incCur;
    P.K.box(0, y0, kW, y1, 22, on ? kPanelHi : kPanel);
    if (on) xrmenu::focus(P.K, 0, y0, kW, y1, 22);
    emblem(P, kP[c].emblem, 56, 0.5f * (y0 + y1), 80, kP[c].c1, kP[c].c2);
    P.K.left(std::string(kP[c].handle) + (in.final ? "  FINAL OFFER  -  " : "  WANTS YOUR"), 112, y0 + 32, 22, 420, in.final ? kAmber : kCyan, 0.0f);
    P.K.left(short_words(in.want), 112, y0 + 66, 28, 380, kInk, 2.0f);
    const long long mine = snap_px(S, in.want);
    P.K.left("YOURS: MARKET " + money(mine, 22, 220), 112, y0 + 100, 22, 380, kDim, 0.0f);
    const int left = std::max(0, in.expires - S.st.lastDay);
    P.K.left(left <= 1 ? "ENDS AT THE NEXT MATCHDAY" : std::to_string(left) + " MATCHDAYS LEFT", 112, y0 + 132, 18, 380, left <= 1 ? kAmber : kDim, 0.0f);
    xrmarket::draw_mini(P.v, *P.keep, in.want, 510, y0 + 14 + (in.want.grade > 0 ? 10 : 0), 590, y1 - 14);
    // the swap arrows
    P.K.box(612, y0 + 62, 678, y0 + 78, 3, kGoldC, 0.9f);  // (his side comes to you: gold; yours goes to him: cyan - shaft + head each)
    P.K.poly({676, y0 + 48, 704, y0 + 70, 676, y0 + 92}, kGoldC, 0.9f);
    P.K.box(634, y0 + 100, 700, y0 + 116, 3, kCyan, 0.9f);
    P.K.poly({636, y0 + 86, 608, y0 + 108, 636, y0 + 130}, kCyan, 0.9f);
    std::vector<Variant> theirs;
    long long mkt = in.give.theirCoins;
    for (uint32_t id : in.give.theirs)
      if (const Item* it = snap_item(S, c, id)) theirs.push_back(it->v), mkt = xrcoin::add(mkt, snap_item_px(S, *it));
    float x = 720;
    for (const Variant& v : theirs) {
      xrmarket::draw_mini(P.v, *P.keep, v, x, y0 + 14 + (v.grade > 0 ? 10 : 0), x + 80, y1 - 14);
      x += 92;
    }
    if (in.give.theirCoins > 0) {
      const std::string ct = "+" + money(in.give.theirCoins, 40, 1220 - x - 60);
      coin(P, x + 22, 0.5f * (y0 + y1), 18);
      P.K.left(ct, x + 48, 0.5f * (y0 + y1) + 2, 40, 1220 - x - 60, kGoldC, 2.0f);
    }
    // the meter + his line
    meter(P, mine, mkt, 1240, y0 + 30, kW - 20, y0 + 62, true);
    uint32_t rc = kDim;
    P.K.text(ratio_words(mine, mkt, &rc), 0.5f * (1240 + kW - 20), y0 + 90, 22, kW - 1270, rc, 0.0f);
    P.K.text(std::string("\"") + kP[c].lines[kLPitch][in.id & 1] + "\"", 0.5f * (1240 + kW - 20), y0 + 124, 18, kW - 1270, 0xC4CCE4, 0.0f);
    if (on && S.incCountering) {  // the counter: +5 / +10 / +15% more coins
      P.K.box(720, y0 - 8, kW - 8, y1 + 8, 26, 0x1A1036, 1.0f);
      P.K.ring(720, y0 - 8, kW - 8, y1 + 8, 26, 4, kPink);
      P.K.left("ASK FOR MORE", 746, y0 + 48, 30, 260, kPink, 2.0f);
      // (r236: its "LEFT / RIGHT - A SEND - B DROP IT" on the page: gone - the bar says them)
      for (int kk = 0; kk < 3; ++kk) {
        const float cx0 = 1010 + kk * 205.0f, cx1 = cx0 + 190;
        const bool sel = kk == S.incPick;
        P.K.box(cx0, y0 + 14, cx1, y1 - 14, 30, sel ? 0x4A1440 : 0x150E2C);
        P.K.ring(cx0, y0 + 14, cx1, y1 - 14, 30, sel ? 4.0f : 2.0f, sel ? kPink : kLine);
        P.K.text("+" + std::to_string(kCounterPct[kk]) + "%", 0.5f * (cx0 + cx1), y0 + 56, 34, 170, 0xFFFFFF, 0.0f);
        const long long more = static_cast<long long>(std::floor(static_cast<double>(mkt) * kCounterPct[kk] / 100.0 + 0.5));
        P.K.text("+" + money(more, 24, 170) + " COINS", 0.5f * (cx0 + cx1), y0 + 98, 22, 170, kGoldC, 0.0f);
      }
    }
  }
}
void draw_history(Pen& P, const Snap& S, float t) {
  const std::vector<Trade>& H = S.st.hist;
  long long gave = 0, got = 0;
  for (const Trade& tr : H) gave = xrcoin::add(gave, tr.gaveMarket), got = xrcoin::add(got, tr.gotMarket);
  P.K.box(0, 196, kW, 296, 22, 0x0E152C);
  P.K.ring(0, 196, kW, 296, 22, 2, kLine);
  const char* const kL[4] = {"TRADES", "YOU GAVE (MARKET THEN)", "YOU GOT (MARKET THEN)", "NET"};
  const long long net = got - gave;
  for (int i = 0; i < 4; ++i) {
    const float x = 30 + i * 405.0f;
    P.K.left(kL[i], x, 226, 20, 380, kDim, 0.0f);
    const std::string v = i == 0 ? std::to_string(H.size()) : (i == 1 ? money(gave, 40, 370) : (i == 2 ? money(got, 40, 370) : (net >= 0 ? "+" : "-") + money(net >= 0 ? net : -net, 40, 360)));
    P.K.left(v, x, 268, 40, 380, i == 3 ? (net >= 0 ? kUp : kDown) : kInk, 2.0f);
  }
  if (H.empty()) {
    P.K.text("NO TRADES YET", 825, 470, 60, 1400, kDim, 2.0f);
    P.K.text("CARDS > COLLECTORS: OPEN A DESK, PICK THEIR CARDS + YOURS, SEND", 825, 540, 26, 1400, kDim, 0.0f);
  }
  const int top = std::max(0, std::min(S.histTop, std::max(0, static_cast<int>(H.size()) - 4)));
  for (int r = 0; r < 4 && top + r < static_cast<int>(H.size()); ++r) {
    const Trade& tr = H[static_cast<size_t>(top + r)];
    const float y0 = 310 + r * 98.0f, y1 = y0 + 90;
    P.K.box(0, y0, kW, y1, 18, kPanel);
    emblem(P, kP[tr.col].emblem, 46, 0.5f * (y0 + y1), 60, kP[tr.col].c1, kP[tr.col].c2);
    P.K.left("MATCHDAY " + std::to_string(tr.day) + "  -  " + kP[tr.col].handle, 90, y0 + 24, 18, 400, kDim, 0.0f);
    P.K.left(tr.headline, 90, y0 + 52, 22, 1010, kInk, 0.0f);
    float x = 1110;
    for (size_t i = 0; i < tr.gave.size() && i < 2; ++i, x += 50) xrmarket::draw_mini(P.v, *P.keep, tr.gave[i], x, y0 + 10, x + 44, y1 - 10);
    P.K.text(">", x + 14, 0.5f * (y0 + y1), 30, 30, kGoldC, 0.0f);
    x += 34;
    for (size_t i = 0; i < tr.got.size() && i < 2; ++i, x += 50) xrmarket::draw_mini(P.v, *P.keep, tr.got[i], x, y0 + 10, x + 44, y1 - 10);
    const long long d = tr.gotMarket - tr.gaveMarket;
    P.K.right((d >= 0 ? "+" : "-") + money(d >= 0 ? d : -d, 24, 150), kW - 20, y0 + 32, 24, 160, d >= 0 ? kUp : kDown, 2.0f);
    P.K.right("AT MARKET", kW - 20, y0 + 62, 16, 160, kDim, 0.0f);
  }
  if (H.size() > 4) xrmenu::scroll_note(P.K, 825, 732, top, 4, static_cast<int>(H.size()), "TRADE");
  P.K.box(0, 770, kW, 892, 18, 0x0E152C);
  P.K.left("TRADE WIRE", 24, 800, 22, 300, kGoldC, 2.0f);
  for (size_t i = 0; i < S.st.wire.size() && i < 3; ++i) P.K.left(S.st.wire[i].text, 200, 800 + i * 32.0f, 20, kW - 230, i == 0 ? kInk : 0xC4CCE4, 0.0f);
  (void)t;
}
void draw_pad(Pen& P, const Snap& S, float t) {
  P.K.box(0, 190, kW, 898, 26, 0x02040A, 0.72f);
  P.K.glow(kPadX0 - 60, kPadY0 - 40, kPadX1 + 60, kPadY1 + 40, kGoldC, 0.14f);
  P.K.box(kPadX0, kPadY0, kPadX1, kPadY1, 30, 0x15120A, 1.0f, 0x0B0905);
  P.K.ring(kPadX0, kPadY0, kPadX1, kPadY1, 30, 4, kGoldC);
  chrome(P, S.padFor == 0 ? std::string("YOU ADD COINS") : HEs(S.colCur, " ADDS COINS"), kPadX0 + 30, kPadY0 + 46, 44, 600, -1);
  coin(P, kPadX0 + 70, kPadY0 + 150, 40);
  P.K.left(S.padFor == 0 ? "COINS FROM YOUR WALLET ON YOUR SIDE" : "COINS FROM " + std::string(kP[S.colCur].handle) + " ON " + HISs(S.colCur, " SIDE"), kPadX0 + 130, kPadY0 + 130, 26,
           kPadX1 - kPadX0 - 160, kInk, 0.0f);
  P.K.left(S.padFor == 0 ? "YOU HAVE " + T(S.balance) + " COINS" : HEs(S.colCur, " SAYS NO IF ") + HISs(S.colCur, " WALLET CAN'T COVER IT"), kPadX0 + 130, kPadY0 + 166, 22, kPadX1 - kPadX0 - 160,
           kDim, 0.0f);
  xrcoin::Coins val = 0;
  if (!S.padDigits.empty()) xrcoin::parse(S.padDigits.c_str(), &val);
  const std::string shown = S.padDigits.empty() ? "0" : T(val);
  P.K.box(kPadX0 + 180, 456, kPadX1 - 30, 536, 18, 0x07050F);
  P.K.left("COINS", kPadX0 + 30, 496, 26, 150, kDim, 0.0f);
  P.K.ring(kPadX0 + 180, 456, kPadX1 - 30, 536, 18, 3, S.padFresh ? kLine : kGoldC);
  P.K.right(shown, kPadX1 - 64, 498, 54, kPadX1 - kPadX0 - 270, S.padFresh ? kDim : 0xFFFFFF, 2.0f);
  if (!S.padFresh && std::fmod(t, 1.0f) < 0.6f) P.K.box(kPadX1 - 56, 472, kPadX1 - 50, 522, 2, kGoldC);
  if (S.padFor == 0 && val > S.balance) P.K.left("MORE THAN YOU HAVE", kPadX0 + 30, 566, 22, 600, kDown, 0.0f);
  for (int k = 0; k < 13; ++k) {
    float x0, y0, x1, y1;
    pad_key_box(k, &x0, &y0, &x1, &y1);
    const bool on = k == S.padKey || (k == 12 && S.padKey >= 12);
    const bool hit = k == S.padHit && S.now - S.padHitAt < 0.25;
    const uint32_t face = k == 12 ? (hit ? 0x3AE07A : 0x1E7A44) : (k == 9 || k == 11 ? (hit ? 0x7A4A9A : 0x2E1E4A) : (hit ? 0x4A5A9A : 0x1C2448));
    P.K.box(x0, y0, x1, y1, 16, face, 1.0f, xrmenu::mix(face, 0x000000, 0.35f));
    P.K.ring(x0, y0, x1, y1, 16, on ? 5.0f : 2.0f, on ? (k == 12 ? kUp : kCyan) : kLine);
    if (on) P.K.glow(x0 - 14, y0 - 14, x1 + 14, y1 + 14, k == 12 ? kUp : kCyan, 0.25f);
    P.K.text(kPadKey[k], 0.5f * (x0 + x1), 0.5f * (y0 + y1) + 1, k == 12 ? 34 : (k == 9 || k == 11 ? 28 : 36), x1 - x0 - 16, 0xFFFFFF, 2.0f);
  }
  P.K.left("A  PRESS", kPadX0 + 30, 636, 22, 170, kDim, 0.0f);
  P.K.left("X  DEL", kPadX0 + 30, 668, 22, 170, kDim, 0.0f);
  P.K.left("B  CANCEL", kPadX0 + 30, 700, 22, 170, kDim, 0.0f);
  P.K.left("OR POINT + TRIGGER", kPadX0 + 30, 740, 20, 170, kDim, 0.0f);
  if (S.now - S.padSayAt < 4.0) P.K.text(S.padSay, 0.5f * (kPadX0 + kPadX1), kPadY1 - 2, 22, kPadX1 - kPadX0 - 40, kDown, 0.0f);
}
}  // namespace

void draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t) {
  Snap S;
  {  // (a snapshot under the lock: the drawing itself runs without it - the game thread never waits on a picture)
    std::lock_guard<std::mutex> lk(g_m);
    load_set();
    S.st = g_st;
    S.tab = g_tab, S.colCur = g_colCur, S.pane = g_pane, S.coinCur = g_coinCur, S.incCur = g_incCur, S.histTop = g_histTop, S.incPick = g_incPick, S.ask = g_ask;
    S.paneCur[0] = g_paneCur[0], S.paneCur[1] = g_paneCur[1], S.paneTop[0] = g_paneTop[0], S.paneTop[1] = g_paneTop[1];
    S.desk = g_desk, S.showCounter = g_showCounter && g_st.col[g_colCur].hasCounter, S.incCountering = g_incCountering, S.pad = g_pad.load(), S.padFresh = g_padFresh;
    S.draft = g_draft[g_colCur];
    S.mineRows = g_desk ? g_mineRows : std::vector<MineRow>();
    S.px = g_px;
    S.msg = g_msg, S.msgRgb = g_msgRgb, S.now = g_now, S.msgAt = g_msgAt, S.flashAt = g_flashAt, S.cycle = g_cycle;
    S.padDigits = g_padDigits, S.padSay = g_padSay, S.padSayAt = g_padSayAt, S.padHitAt = g_padHitAt, S.padFor = g_padFor, S.padKey = g_padKey, S.padHit = g_padHit;
    for (int c = 0; c < kCollectors; ++c) {
      S.mood[c] = mood_of(c);
      S.need[c] = need_of(c);
      S.patienceMax[c] = patience_of(c);
    }
  }
  S.balance = balance();
  Pen P;
  P.keep = &keep;
  const bool bcast = xrmenu::broadcast();  // r236: inside the hub (LOOK B)
  if (bcast) {
    if (S.st.career == 0 || !S.st.loaded) {
      xrmenu::say(P.K, "LOAD A FRANCHISE CAREER", 825, 480, 64, xrmenu::kBDim, 'c', 1400);
    } else {
      const std::vector<std::string> names = {"COLLECTORS", "OFFERS", "HISTORY"};
      std::vector<std::string> badges = {"", S.st.inc.empty() ? std::string() : std::to_string(S.st.inc.size()), ""};
      xrmenu::subtabs(P.K, names, S.tab, 0, 14, badges, xrmenu::kBCards);
      const size_t from = P.v.size();
      bool moveUp = true;
      if (S.tab == kTabCols) {
        if (S.desk) {
          draw_desk(P, S, t);
          if (S.showCounter) draw_counter(P, S, t);
        } else {
          draw_collectors_b(P, S, t);
          moveUp = false;
        }
      } else if (S.tab == kTabOffers) {
        draw_offers(P, S, t);
      } else {
        draw_history(P, S, t);
      }
      if (moveUp)  // (r213-r235's pages start at 196 under the old header: up under the sub-tabs - no pointer on them)
        for (size_t i = from; i < P.v.size(); ++i) {
          xrtext::BoardPiece& p = P.v[i];
          p.y0 -= 110.0f;
          const bool text = p.kind == xrtext::BoardPiece::kText || p.kind == xrtext::BoardPiece::kTextLeft || p.kind == xrtext::BoardPiece::kTextRight;
          if (!text) p.y1 -= 110.0f;
          for (size_t q = 1; q < p.pts.size(); q += 2) p.pts[q] -= 110.0f;
        }
      if (S.pad) draw_pad(P, S, t);  // (the coin pad: the pointer's keys - not moved)
      if (S.now - S.msgAt < 6.0) xrmenu::say(P.K, S.msg, 0.5f * kW, 930, 34, S.msgRgb, 'c', kW - 60);  // (its last word only: no ticker)
    }
  } else {
  P.K.box(0, 0, kW, kH, 30, kBg0, 1.0f, kBg1);
  {  // the prism's shards + glows behind the header (the TIZM brand's)
    static const uint32_t kBand[6] = {0xFF2E88, 0xFFB020, 0xFFE94A, 0x39FF88, 0x2EF2FF, 0x9B5CFF};
    for (int i = 0; i < 9; ++i) {
      const float a = i * 190.0f - 60.0f, b = a + 90.0f;
      P.K.poly({a, 120, b, 120, b + 70, 0, a + 70, 0}, kBand[i % 6], 0.08f);
    }
    P.K.glow(-240, -200, 520, 380, kPink, 0.16f);
    P.K.glow(kW - 520, -200, kW + 240, 300, kGoldC, 0.14f);
  }
  chrome(P, "STRIKERZ", 22, 60, 88, 290, -1);  // r217 (Lance: STRIKERZ)
  gold_title(P, "COLLECTOR TRADES", 330, 64, 56, 430);
  if (S.st.career == 0 || !S.st.loaded) {
    P.K.text("LOAD A FRANCHISE CAREER", 825, 520, 64, 1400, kDim, 2.0f);
  } else {
    P.K.text("MATCHDAY " + std::to_string(std::max(0, S.st.lastDay)) + "  -  SEASON " + std::to_string(std::max(1, S.st.season)), 980, 44, 32, 420, kInk, 2.0f);
    const char* mood = S.cycle > 0.25 ? "MARKET BOOMING" : (S.cycle > 0.08 ? "MARKET HOT" : (S.cycle < -0.2 ? "MARKET CRASHING" : (S.cycle < -0.06 ? "MARKET COOL" : "MARKET CALM")));
    P.K.text(std::string(mood) + "  -  COLLECTORS' MOODS FOLLOW IT", 980, 88, 20, 440, S.cycle > 0.08 ? kUp : (S.cycle < -0.06 ? kDown : kDim), 0.0f);
    coin(P, 1300, 60, 24);
    P.K.right(money(S.balance, 46, 300), kW - 16, 56, 46, 300, kGoldC, 2.0f);
    P.K.right("BIT COINS", kW - 16, 100, 22, 200, kDim, 0.0f);
    const char* const kTabName[kTabs] = {"COLLECTORS", "OFFERS FOR YOU", "MY TRADES"};
    for (int i = 0; i < kTabs; ++i) {
      const float tx0 = i * 262.0f, tx1 = tx0 + 252;
      const bool on = S.tab == i;
      if (on) P.K.glow(tx0 - 18, 122, tx1 + 18, 196, kCyan, 0.3f);
      P.K.box(tx0, 134, tx1, 182, 24, on ? 0x153A60 : 0x0F1630);
      P.K.ring(tx0, 134, tx1, 182, 24, on ? 4.0f : 2.0f, on ? kCyan : kLine);
      P.K.text(kTabName[i], 0.5f * (tx0 + tx1), 159, 26, 236, on ? 0xFFFFFF : kDim, on ? 2.0f : 0.0f);
      const int badge = i == kTabOffers ? static_cast<int>(S.st.inc.size()) : 0;
      if (badge > 0) {
        P.K.box(tx1 - 30, 120, tx1 + 12, 156, 18, xrmenu::kBadge);
        P.K.text(std::to_string(badge), tx1 - 9, 139, 26, 36, 0xFFFFFF, 0.0f);
      }
    }
    if (!(S.tab == kTabCols && S.desk)) {
      xrmenu::glyph(P.K, "L", 806, 158, 40);
      P.K.left("LEFT / RIGHT: TABS", 854, 159, 22, 200, kDim, 0.0f);
    }
    int trades = 0;
    for (int c = 0; c < kCollectors; ++c) trades += S.st.col[c].trades;
    P.K.right(std::to_string(trades) + (trades == 1 ? " TRADE DONE" : " TRADES DONE") + "  -  NO FEES", kW - 16, 159, 22, 400, kDim, 0.0f);
    if (S.tab == kTabCols) {
      if (S.desk) {
        draw_desk(P, S, t);
        if (S.showCounter) draw_counter(P, S, t);
      } else {
        draw_collectors(P, S, t);
      }
    } else if (S.tab == kTabOffers) {
      draw_offers(P, S, t);
    } else {
      draw_history(P, S, t);
    }
    if (S.pad) draw_pad(P, S, t);
    const bool fresh = S.now - S.msgAt < 6.0;
    P.K.box(0, 904, kW, 958, 26, 0x070B18);
    P.K.ring(0, 904, kW, 958, 26, 2, fresh ? S.msgRgb : 0x2C3A66);
    if (fresh) {
      P.K.text(S.msg, 0.5f * kW, 932, 28, kW - 60, S.msgRgb, 0.0f);
    } else {
      std::string w;
      for (size_t i = 0; i < S.st.wire.size() && i < 2; ++i) w += (i ? "      -      " : "") + S.st.wire[i].text;
      if (w.empty()) w = "CARD FOR CARD, COINS ON EITHER SIDE  -  THEY ACCEPT, DECLINE OR COUNTER  -  LOWBALL THEM AND THEY REMEMBER";
      P.K.text(w, 0.5f * kW, 932, 24, kW - 60, kAmber, 0.0f);
    }
  }
  }  // (r236: the old frame - outside the hub)
  const float fl = static_cast<float>(S.now - S.flashAt);
  if (fl >= 0.0f && fl < 0.5f) P.K.box(0, 0, kW, kH, 30, 0xFFFFFF, 0.3f * (1.0f - fl / 0.5f));
  for (xrtext::BoardPiece& p : P.v) {
    p.x0 = ox + p.x0 * k;
    p.y0 = oy + p.y0 * k;
    const bool text = p.kind == xrtext::BoardPiece::kText || p.kind == xrtext::BoardPiece::kTextLeft || p.kind == xrtext::BoardPiece::kTextRight;
    if (text) {
      p.x1 *= k;
      p.y1 *= k;
    } else {
      p.x1 = ox + p.x1 * k;
      p.y1 = oy + p.y1 * k;
    }
    p.radius *= k;
    p.outline *= k;
    p.shadow *= k;
    p.cut *= k;  // r236
    p.lean *= k;
    if (bcast && text && !p.styled && !p.art && p.y1 >= 14.0f && p.y1 < 30.0f * k) p.y1 = 30.0f * k;  // r236: nothing under 30 px in the hub (card art: styled)
    for (size_t i = 0; i < p.pts.size(); ++i) p.pts[i] = (i % 2 == 0 ? ox : oy) + p.pts[i] * k;
    out.push_back(std::move(p));
  }
}

// ============================================================================================== the proof's handles
Reply propose(int collector, const Offer& o) {
  std::lock_guard<std::mutex> lk(g_m);
  const Reply r = do_propose(collector, o);
  if (r.kind != kRefused) save();
  return r;
}
bool accept_counter(int collector) {
  std::lock_guard<std::mutex> lk(g_m);
  const bool ok = do_accept_counter(collector);
  save();
  return ok;
}
bool decline_counter(int collector) {
  std::lock_guard<std::mutex> lk(g_m);
  const bool ok = do_decline_counter(collector);
  if (ok) save();
  return ok;
}
bool has_counter(int collector, Offer* out) {
  std::lock_guard<std::mutex> lk(g_m);
  if (collector < 0 || collector >= kCollectors || !g_st.col[collector].hasCounter) return false;
  if (out != nullptr) *out = g_st.col[collector].counter;
  return true;
}
std::vector<CollectorView> collectors() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<CollectorView> v;
  for (int c = 0; c < kCollectors; ++c) {
    const Col& k = g_st.col[c];
    CollectorView cv;
    cv.name = kP[c].name;
    cv.handle = kP[c].handle;
    cv.kind = kP[c].kind;
    cv.goodwill = k.goodwill;
    cv.mood = mood_of(c);
    cv.need = need_of(c);
    cv.wallet = k.wallet;
    cv.patience = k.patience;
    cv.cards = static_cast<int>(k.items.size());
    for (const Item& it : k.items) cv.blacks += it.v.parallel == kBlack ? 1 : 0;
    cv.trades = k.trades;
    cv.lowballs = k.lowballs;
    for (size_t i = 0; i < k.wants.size(); ++i)
      if (!(i < k.wantDone.size() && k.wantDone[i] != 0)) cv.wants.push_back(k.wants[i]);
    v.push_back(cv);
  }
  return v;
}
std::vector<Item> binder(int collector) {
  std::lock_guard<std::mutex> lk(g_m);
  return collector >= 0 && collector < kCollectors ? g_st.col[collector].items : std::vector<Item>();
}
double taste(int collector, const Variant& v) {
  std::lock_guard<std::mutex> lk(g_m);
  return collector >= 0 && collector < kCollectors ? taste_of(collector, v) : 0.0;
}
double hold(int collector, const Variant& v) {
  std::lock_guard<std::mutex> lk(g_m);
  return collector >= 0 && collector < kCollectors ? hold_of(collector, v) : 0.0;
}
double need(int collector) {
  std::lock_guard<std::mutex> lk(g_m);
  return collector >= 0 && collector < kCollectors ? need_of(collector) : 1.0;
}
std::vector<IncomingView> incoming() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<IncomingView> v;
  for (const Incoming& i : g_st.inc) v.push_back({i.id, i.col, i.want, i.give, std::max(0, i.expires - g_st.lastDay), i.final});
  return v;
}
bool accept_incoming(uint32_t id) {
  std::lock_guard<std::mutex> lk(g_m);
  const bool ok = do_accept_incoming(id);
  save();
  return ok;
}
int counter_incoming(uint32_t id, int percent) {
  std::lock_guard<std::mutex> lk(g_m);
  const int r = do_counter_incoming(id, percent);
  save();
  return r;
}
bool decline_incoming(uint32_t id) {
  std::lock_guard<std::mutex> lk(g_m);
  const bool ok = do_decline_incoming(id);
  if (ok) save();
  return ok;
}
std::vector<TradeView> history() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<TradeView> v;
  for (const Trade& t : g_st.hist) v.push_back({t.day, t.col, t.gave, t.got, t.gaveCoins, t.gotCoins, t.gaveMarket, t.gotMarket, t.headline});
  return v;
}
std::vector<std::string> world_black_keys() {
  std::lock_guard<std::mutex> lk(g_m);
  return black_keys();
}
// r235: you buy a collector's / the dealer's listed copy (the market's SEARCH: A > YES - no market lock held here)
bool buy_listing(uint32_t id, long long ask, std::string* line) {
  std::lock_guard<std::mutex> lk(g_m);
  std::string dummy;
  std::string& L = line != nullptr ? *line : dummy;
  drain_queue();
  if (g_st.career == 0 || !g_st.loaded || g_src.spend == nullptr || g_src.give == nullptr || g_src.add == nullptr) {
    L = "LOAD A FRANCHISE CAREER";
    return false;
  }
  int seller = -1;
  Item it;
  for (int c = 0; c < kCollectors && seller < 0; ++c)
    for (const Item& x : g_st.col[c].items)
      if (x.id == id && x.ask > 0) seller = c, it = x;
  if (seller < 0)
    for (const Item& x : g_st.deal)
      if (x.id == id && x.ask > 0) seller = kDealer, it = x;
  if (seller < 0) {
    L = "SOLD ALREADY - SOMEONE GOT THERE FIRST";
    g_indexDirty = true;
    push_index();
    return false;
  }
  if (it.ask > ask) {
    L = "THE PRICE CHANGED - IT'S " + T(it.ask) + " COINS NOW";
    g_indexDirty = true;
    push_index();
    return false;
  }
  const long long cost = it.ask;
  if (!can_receive(fresh_holdings(), it.v)) {
    L = "YOU HAVE " + short_words(it.v) + " ALREADY (ITS NUMBER / THE 1/1)";
    return false;
  }
  if (balance() < cost) {
    L = "NOT ENOUGH COINS - " + T(cost) + " NEEDED";
    return false;
  }
  if (!g_src.spend(g_st.career, cost, "STRIKERZ MARKET - A SINGLE")) {
    L = "NOT ENOUGH COINS - " + T(cost) + " NEEDED";
    return false;
  }
  if (!g_src.give(it.v)) {
    g_src.add(g_st.career, cost, "STRIKERZ MARKET - A SINGLE REFUNDED");
    L = "THAT COPY COULDN'T GO IN YOUR BINDER";
    return false;
  }
  const long long mkt = item_px(it);
  std::vector<Item>& from = seller == kDealer ? g_st.deal : g_st.col[seller].items;
  for (size_t i = 0; i < from.size(); ++i)
    if (from[i].id == id) {
      from.erase(from.begin() + static_cast<long>(i));
      break;
    }
  g_px.erase(id);
  const std::string who = seller == kDealer ? std::string("THE DEALER") : std::string(kP[seller].handle);
  if (seller != kDealer) {
    g_st.col[seller].wallet = xrcoin::clamp(xrcoin::add(g_st.col[seller].wallet, cost));
    for (size_t j = 0; j < g_st.inc.size();) {  // (his offers / his counter naming it: gone)
      const Incoming& in = g_st.inc[j];
      if (in.col == seller && has_id(in.give.theirs, id)) g_st.inc.erase(g_st.inc.begin() + static_cast<long>(j));
      else ++j;
    }
    Col& k = g_st.col[seller];
    if (k.hasCounter && has_id(k.counter.theirs, id)) {
      k.hasCounter = false;
      k.counter = Offer();
      tell_held();
    }
    Trade tr;  // (MY TRADES: coins for his card)
    tr.day = g_st.lastDay;
    tr.col = seller;
    tr.got.push_back(it.v);
    tr.gaveCoins = cost;
    tr.gaveMarket = cost;
    tr.gotMarket = mkt;
    tr.headline = "YOU BOUGHT " + who + "'S " + pull_words(it.v) + " FOR " + T(cost) + " COINS";
    g_st.hist.insert(g_st.hist.begin(), tr);
    if (g_st.hist.size() > static_cast<size_t>(kMaxHistory)) g_st.hist.resize(kMaxHistory);
  }
  wire("YOU BOUGHT " + (seller == kDealer ? std::string("THE DEALER'S") : who + "'S") + " " + pull_words(it.v) + " FOR " + T(cost) + " COINS",
       it.v.parallel == kBlack ? 0xFFD23F : 0x39FF88, it.v.parallel == kBlack || cost >= 100000);
  if (it.v.parallel == kBlack || exq_t(it.v) > 0) tell_world();
  char lb[300];
  std::snprintf(lb, sizeof(lb), "STRIKERZ MARKET (r235 search): bought %s's %s for %lld coins (market %lld)", who.c_str(), pull_words(it.v).c_str(), cost, mkt);
  logl(lb);
  g_indexDirty = true;
  push_index();
  save();
  ++g_gen;
  L = "BOUGHT FROM " + who + ": " + pull_words(it.v) + " - IT'S IN YOUR BINDER";
  return true;
}
HobbyStats hobby_stats() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_st.hob;
}
std::vector<Item> dealer_case() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_st.deal;
}
std::vector<xrmarket::Offering> for_sale() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<xrmarket::Offering> rows;
  each_world([&](int c, const Item& it) {
    if (it.ask <= 0) return;
    xrmarket::Offering o;
    o.id = it.id, o.seller = c, o.who = c == kDealer ? "THE DEALER" : kP[c].handle, o.v = it.v, o.ask = it.ask, o.day = it.listed;
    rows.push_back(o);
  });
  return rows;
}
std::vector<std::string> wire_lines() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<std::string> v;
  for (const Wire& w : g_st.wire) v.push_back(w.text);
  return v;
}
void debug_put(int collector, const Variant& v, long long ask) {
  std::lock_guard<std::mutex> lk(g_m);
  if (collector < 0 || collector >= kCollectors) return;
  Item& it = add_item(collector, v, kFromPull, 0);
  if (ask > 0) it.ask = ask, it.listed = g_st.lastDay;
  item_px(it);
  g_indexDirty = true;
  push_index();
}
// r219 (BIG-PULLS.md 4): STRIKERS ILLUSTRATED's COLLECTOR SPOTLIGHT - one of the nine a matchday, in turn
bool spotlight(int which, Spotlight* out) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_st.career == 0) return false;
  const int c = ((which % kCollectors) + kCollectors) % kCollectors;
  const Persona& p = kP[c];
  const Col& k = g_st.col[c];
  Spotlight s;
  s.collector = c;
  s.name = p.name;
  s.handle = p.handle;
  s.kind = p.kind;
  s.mood = mood_of(c);
  s.moodWord = mood_word(s.mood);
  s.quote = p.lines[kLIdle][(((which / kCollectors) % 2) + 2) % 2];
  s.c1 = p.c1;
  s.c2 = p.c2;
  s.cards = static_cast<int>(k.items.size());
  for (const Item& it : k.items) s.blacks += it.v.parallel == kBlack ? 1 : 0;
  static const char* const kHunt[kCollectors] = {"1/1S + AUTOS OF THE MVP + BOOT RACE", "ANYTHING UNDER THE MARKET",      "SHINY POKEMON - ANY OF THEM",
                                                 "MARVEL ALT SUITS",                 "",                               "WHATEVER JUST CRASHED",
                                                 "JAM '96 + THE STRIKERS / CHARGED CREW", "GEM MINT 10 SLABS",            "",
                                                 // r235
                                                 "THE LANCINI TIZM CARDS HE'S MISSING", "JAM '96 DIE-CUTS",               "LOW-NUMBERED EXQUISITE",
                                                 "THE MELEE FIGHTERS",               "HIS CLUB'S PLAYERS",             "THE GOLDEN BOOT RACE'S CARDS",
                                                 "ANYTHING UNDER THE MARKET",        "RAW CARDS TO GRADE",             "ORIGINALS ROOKIE CARDS",
                                                 "01s AND JERSEY NUMBERS",           "THE PRICIEST CARD FOR SALE"};
  s.hunting = kHunt[c];
  if (c == kSetBuilder) {  // (his want list: the first he's still missing)
    int left = 0;
    for (int w : k.wants) {
      if (std::find(k.wantDone.begin(), k.wantDone.end(), w) != k.wantDone.end() || !card_ok(w)) continue;
      if (left++ == 0) s.huntCard.card = w;
    }
    s.hunting = s.huntCard.card >= 0 ? cname(s.huntCard.card) + (left > 1 ? " + " + std::to_string(left - 1) + " MORE" : "") + " FOR " + kSetName[std::max(0, std::min(2, k.setIdx))]
                                     : std::string("NOTHING - ") + kSetName[std::max(0, std::min(2, k.setIdx))] + " IS DONE";
  } else if (c == kHypeChaser) {  // (whoever's hottest this matchday)
    int best = -1;
    for (size_t i = 0; i < g_chg.size() && i < g_set.size(); ++i)
      if (g_set[i].here && (best < 0 || g_chg[i] > g_chg[static_cast<size_t>(best)])) best = static_cast<int>(i);
    if (best >= 0 && g_chg[static_cast<size_t>(best)] > 0.0f) {
      s.huntCard.card = best;
      s.hunting = cname(best) + " - HE'S ON FIRE";
    } else {
      s.hunting = "WHOEVER SCORES NEXT";
    }
  }
  *out = s;
  return true;
}
void draw_portrait(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, int collector, float x0, float y0, float x1, float y1) {
  if (collector < 0 || collector >= kCollectors) return;
  Pen P;
  P.keep = &keep;
  portrait(P, collector, x0, y0, x1, y1, true, 0.0f);
  out.insert(out.end(), P.v.begin(), P.v.end());
}
void reset_for_test() {
  std::lock_guard<std::mutex> lk(g_m);
  g_st = State();
  g_px.clear();
  g_lastRead = -1e9;
  g_pad = false;
  g_desk = false;
  g_ask = 0;
  g_mineRows.clear();
  for (Offer& d : g_draft) d = Offer();
  std::lock_guard<std::mutex> q(g_qm);
  g_queue.clear();
}
void debug_view(int tab, int collector, bool desk, int pane, int cursor, const Offer* draft, bool counterShown, int padFor) {
  std::lock_guard<std::mutex> lk(g_m);
  g_tab = std::max(0, std::min(kTabs - 1, tab));
  g_colCur = std::max(0, std::min(kCollectors - 1, collector));
  g_desk = desk;
  g_pane = std::max(0, std::min(2, pane));
  if (g_pane < 2) g_paneCur[g_pane] = std::max(0, cursor);
  else g_coinCur = std::max(0, std::min(2, cursor));
  if (tab == kTabOffers) {
    g_incCur = std::max(0, cursor);
    g_incCountering = counterShown;
  } else if (tab == kTabHistory) {
    g_histTop = std::max(0, cursor);
  }
  if (draft != nullptr) g_draft[g_colCur] = *draft;
  g_showCounter = tab == kTabCols && counterShown;
  if (desk) build_mine_rows();
  g_pad = padFor >= 0;
  if (padFor >= 0) {
    pad_open_for(padFor);
    g_padDigits = padFor == 0 ? "2500000" : "999999999999";
    g_padFresh = false;
  }
  ++g_gen;
}
void debug_mood(double cycle) {
  std::lock_guard<std::mutex> lk(g_m);
  g_cycle = cycle;
}
void debug_goodwill(int collector, double g) {
  std::lock_guard<std::mutex> lk(g_m);
  if (collector >= 0 && collector < kCollectors) g_st.col[collector].goodwill = clampd(g, -1.0, 1.0);
}
void debug_patience(int collector) {
  std::lock_guard<std::mutex> lk(g_m);
  if (collector >= 0 && collector < kCollectors) g_st.col[collector].patience = patience_of(collector);
}
void debug_say(int collector, const std::string& line, int kind) {
  std::lock_guard<std::mutex> lk(g_m);
  if (collector < 0 || collector >= kCollectors) return;
  speak_text(collector, kind, line);
  ++g_gen;
}

}  // namespace xrtrade
