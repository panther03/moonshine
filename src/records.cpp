#include "susamune/records.hxx"

#include "Dolphin/mem.h"
#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/Watergun.hxx"
#include "SMS/Enemy/Conductor.hxx"
#include "SMS/Manager/EnemyManager.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/iling.hxx"
#include "susamune/mem2_map.h"
#include "susamune/packed_text.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/settings.hxx"
#include "susamune/susamune_cfg.h"

namespace {

const u32 kU32Max = 0xffffffffu;
const u8 kUnlockQueueSize = 16;
const u16 kMashathonFrames = 15 * 30;
const s32 kTasbotQf = 1656;
const u8 kTrialGoal = 97;
const u8 kLaunchTimeCount = 29;
const u8 kRC1TimeCount = 10;
const u8 kLaunchStreakCount = 16;

constexpr u32 qfAtLeastSeconds(u32 seconds) {
    const u64 numerator = static_cast<u64>(seconds) * 120000u;
    return static_cast<u32>((numerator + 1000u) / 1001u);
}

constexpr u32 kGhostFiveMinutesQf = qfAtLeastSeconds(5u * 60u);
constexpr u32 kGhostOneHourQf = qfAtLeastSeconds(60u * 60u);
constexpr u32 kGhostOverFiftyHoursQf = qfAtLeastSeconds(50u * 60u * 60u);

static_assert(kGhostFiveMinutesQf == 35965u &&
                  kGhostOneHourQf == 431569u &&
                  kGhostOverFiftyHoursQf == 21578422u,
              "ghost lifetime QFT thresholds changed");
static_assert(static_cast<u64>(kGhostFiveMinutesQf - 1u) * 1001u <
                      static_cast<u64>(5u * 60u) * 120000u &&
                  static_cast<u64>(kGhostFiveMinutesQf) * 1001u >=
                      static_cast<u64>(5u * 60u) * 120000u &&
                  static_cast<u64>(kGhostOneHourQf - 1u) * 1001u <
                      static_cast<u64>(60u * 60u) * 120000u &&
                  static_cast<u64>(kGhostOneHourQf) * 1001u >=
                      static_cast<u64>(60u * 60u) * 120000u &&
                  static_cast<u64>(kGhostOverFiftyHoursQf - 1u) * 1001u <=
                      static_cast<u64>(50u * 60u * 60u) * 120000u &&
                  static_cast<u64>(kGhostOverFiftyHoursQf) * 1001u >
                      static_cast<u64>(50u * 60u * 60u) * 120000u,
              "ghost lifetime thresholds lost their exact QFT boundary");

enum TimeFlags {
    TIME_QFT = 0,
    TIME_IGT = 1 << 7,
};

enum StreakFlags {
    STREAK_FINISH = 0,
    STREAK_QFT = 1,
    STREAK_QFT_INCLUSIVE = 2,
    STREAK_IGT = 3,
};

enum ActionFlags {
    ACTION_HOVER  = 1 << 0,
    ACTION_ROCKET = 1 << 1,
    ACTION_TURBO  = 1 << 2,
    ACTION_YOSHI  = 1 << 3,
};

struct TimeRule {
    u16 valueCentis;
    u8 entry;
    u8 slotFlags;
};

// Rule order is persistent Times ID order. `valueCentis` is the threshold as
// named to the player; QFT conversion happens only at comparison time.
constexpr TimeRule kTimeRules[] = {
    {12500, 85, TIME_IGT | 63}, {12000, 77, 105},
    {9100, 21, TIME_IGT | 15}, {6000, 38, 30}, {3500, 83, 79},
    {3000, 66, 51}, {12600, 46, 121}, {5600, 43, 33},
    {9000, 87, 67}, {3500, 67, 52}, {3100, 72, 78},
    {8000, 1, 1}, {2900, 121, 125}, {12000, 24, 101},
    {6200, 9, TIME_IGT | 9}, {6000, 11, 7}, {7600, 92, 119},
    {4300, 5, 3}, {1300, 49, 36}, {4200, 41, TIME_IGT | 38},
    {2990, 72, 78}, {5100, 53, 41}, {15200, 61, TIME_IGT | 45},
    {3200, 42, 32}, {10000, 28, 21}, {8800, 68, 53},
    {5000, 71, 55}, {4300, 41, TIME_IGT | 38}, {2900, 54, 76},
    {4500, 27, 28}, {4400, 90, 86}, {5000, 19, 18},
    {4200, 2, 2}, {4000, 25, 20}, {3500, 59, 49},
    {3000, 18, 72}, {7101, 55, TIME_IGT | 48},
    {2300, 101, TIME_IGT | 95}, {4000, 84, 68},

    // RC2 Bronze
    {9000, 29, 22}, {12000, 77, 105},
    // RC2 Silver
    {5000, 19, 18}, {8000, 75, 57}, {11000, 89, 106},
    {2500, 108, 117}, {3000, 99, 93},
    // RC2 Gold
    {5100, 0, 0}, {5500, 33, 25}, {5000, 63, 47},
    {3200, 76, 59}, {2800, 105, 99},
    // RC2 Diamond
    {4000, 4, 8}, {7900, 51, 103}, {2500, 95, 89},
    // RC2 Demon
    {4000, 9, 9}, {12700, 12, 100}, {4500, 23, 17},
    {4350, 44, 34}, {3200, 59, 49}, {4100, 73, 58},
    {4000, 84, 68}, {3175, 109, 118},
    // RC2 Frontier
    {13900, 37, 102}, {12000, 64, 104},
};

constexpr u8 kNuttyTimeIndex = 56;

struct StreakRule {
    u16 valueCentis;
    u8 entry;
    u8 goal;
    u8 flags;
    u8 pad;
};

// Rule order is persistent Streaks ID order.
const StreakRule kStreakRules[] = {
    {0, 121, 5, STREAK_FINISH, 0},     // Getting There!
    {0, 82, 5, STREAK_FINISH, 0},      // Chuckster Chatter
    {5300, 17, 5, STREAK_QFT, 0},      // Pegged
    {3500, 93, 5, STREAK_QFT, 0},      // Disturbed the Family Vacation
    {13200, 85, 5, STREAK_IGT, 0},     // Powerwash Simulator
    {3700, 42, 5, STREAK_QFT, 0},      // Electrokoopas Are Heartless
    {2800, 47, 5, STREAK_QFT, 0},      // Clockwork Carousel
    {7800, 92, 5, STREAK_QFT_INCLUSIVE, 0}, // Five Alarm Fire
    {3000, 72, 5, STREAK_QFT, 0},      // Jonathan the Tortoise
    {9000, 68, 7, STREAK_QFT, 0},      // Oral Surgeon
    {7000, 7, 5, STREAK_QFT, 0},       // Dirty Work
    {4400, 17, 5, STREAK_QFT, 0},      // Tower Defense
    {4900, 57, 5, STREAK_QFT, 0},      // Loaded Dice
    {5250, 71, 5, STREAK_QFT, 0},      // Shell Game
    {3100, 54, 10, STREAK_QFT, 0},     // No Vacancy
    {11500, 24, 5, STREAK_QFT, 0},     // Deep End Dividend
    {0, 18, 5, STREAK_FINISH, 0},       // Run Killer Conquered
    {0, 90, 5, STREAK_FINISH, 0},       // Frequent Flyer
    {4500, 25, 5, STREAK_QFT, 0},       // Sandcastle Siege
    {5000, 17, 5, STREAK_QFT, 0},       // Harbor Habit
    {4400, 78, 5, STREAK_QFT_INCLUSIVE, 0}, // Dog Trainer
    {3800, 59, 5, STREAK_QFT, 0},       // Casino Circuit
    {4300, 5, 5, STREAK_QFT, 0},        // I Didn't Hear No Bell
    {5400, 65, 5, STREAK_QFT, 0},       // Mole Mangler
    {2750, 47, 5, STREAK_QFT, 0},       // Clockwork Crisis
    {2700, 121, 10, STREAK_QFT, 0},     // Lord of the Nut
};

struct WorldPBRule {
    u8 anyFirst;
    u8 anyCount;
    u8 allFirst;
    u8 allCount;
};

// Any-percent route slots are QFT slots, including the RTA red-coin rows.
const u8 kAnySlots[] = {
    1, 2, 3, 4, 5, 6,
    10, 11, 12, 13, 14, 15, 16,
    125, 26,
    30, 31, 32, 33, 121, 36,
    40, 41, 42, 43, 44, 45, 46,
    50, 51, 52, 53, 54, 55, 56,
    60, 65, 62, 61, 64, 63, 66,
    119, 80, 81, 82, 83, 84, 85, 108, 109, 120, 110, 111, 86,
};

const u8 kAllSlots[] = {
    0, 1, 2, 70, 8, 3, 4, 5, 71, 9, 6, 7, 100,
    10, 11, 19, 12, 13, 72, 18, 14, 15, 16, 17, 101,
    20, 73, 28, 21, 22, 23, 123, 24, 25, 26, 27, 125, 29, 102,
    30, 31, 74, 38, 32, 33, 34, 35, 121, 75, 39, 36, 37, 103,
    40, 41, 76, 48, 42, 43, 77, 49, 44, 45, 46, 47, 104,
    50, 51, 52, 53, 122, 54, 55, 78, 58, 56, 57, 59, 105,
    60, 65, 62, 61, 64, 79, 68, 63, 66, 67, 69, 106,
    86, 88, 119, 124,
    87, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 107, 116, 117, 118,
    80, 81, 82, 83, 84, 85, 108, 109, 120, 110, 111,
};

const WorldPBRule kWorldPBRules[] = {
    {0, 6, 0, 13},
    {6, 7, 13, 12},
    {13, 2, 25, 14},
    {15, 6, 39, 14},
    {21, 7, 53, 13},
    {28, 7, 66, 13},
    {35, 7, 79, 12},
    {42, 13, 91, 31},
};

struct CourseRule {
    u16 anySeconds;
    u16 jpAnySeconds;
    u16 allSeconds;
    u16 jpAllSeconds;
};

const CourseRule kCourseRules[] = {
    {420, 420, 765, 765}, // Bianco 7:00 / 12:45
    {315, 315, 585, 585}, // Ricco 5:15 / 9:45
    {60, 60, 675, 675},   // Gelato 1:00 / 11:15
    {320, 323, 660, 663}, // Pinna 5:20 (JP 5:23) / 11:00 (JP 11:03)
    {570, 575, 1030, 1035}, // Sirena 9:30 (JP 9:35) / 17:10 (JP 17:15)
    {345, 345, 640, 640}, // Noki 5:45 / 10:40
    {375, 375, 615, 615}, // Pianta 6:15 / 10:15
};

const u8 kTierCounts[Records::CATEGORY_COUNT][Records::TIER_COUNT] = {
    {8, 13, 11, 9, 16, 7},
    {2, 2, 3, 2, 0, 0},
    {7, 7, 7, 7, 0, 0},
    {4, 3, 3, 4, 9, 3},
    {3, 11, 5, 3, 3, 2},
};

const u8 kRCTierCounts[2][Records::TIER_COUNT] = {
    {1, 3, 1, 1, 2, 2},
    {2, 1, 1, 2, 3, 1},
};

const u8 kCategoryCounts[] = {64, 9, 28, 26, 27};

const u8 kLaunchTierFirst[Records::CATEGORY_COUNT][Records::TIER_COUNT] = {
    {14, 19, 24, 29, 34, 40},
    {43, 45, 47, 50, 52, 52},
    {53, 60, 67, 74, 81, 81},
    {81, 83, 85, 87, 89, 95},
    {97, 99, 109, 113, 115, 117},
};

const u8 kRCTierFirst[2][Records::TIER_COUNT] = {
    {118, 119, 122, 123, 124, 126},
    {128, 130, 131, 132, 134, 137},
};

const u8 kRC2TierCounts[2][Records::TIER_COUNT] = {
    {2, 5, 5, 3, 8, 2},
    {1, 1, 1, 1, 1, 1},
};

const u8 kRC2TierFirst[2][Records::TIER_COUNT] = {
    {138, 140, 145, 150, 153, 161},
    {163, 164, 165, 166, 167, 168},
};

const u8 kAreaMap[] = {
    Records::AREA_AIRSTRIP, Records::AREA_DELFINO,
    Records::AREA_BIANCO, Records::AREA_RICCO,
    Records::AREA_GELATO, Records::AREA_PINNA,
    Records::AREA_SIRENA, Records::AREA_SIRENA,
    Records::AREA_PIANTA, Records::AREA_NOKI,
    0xff, 0xff, 0xff, Records::AREA_PINNA, Records::AREA_SIRENA, 0xff,
    Records::AREA_NOKI, 0xff, 0xff, 0xff,
    Records::AREA_AIRSTRIP, Records::AREA_DELFINO,
    Records::AREA_DELFINO, Records::AREA_DELFINO,
    Records::AREA_DELFINO, 0xff, 0xff, 0xff, 0xff,
    Records::AREA_DELFINO, Records::AREA_RICCO, Records::AREA_NOKI,
    Records::AREA_GELATO, Records::AREA_GELATO,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    Records::AREA_SIRENA, Records::AREA_PINNA, Records::AREA_PIANTA, 0xff,
    Records::AREA_NOKI, 0xff, Records::AREA_BIANCO, Records::AREA_BIANCO,
    Records::AREA_RICCO, 0xff, Records::AREA_PINNA, Records::AREA_SIRENA,
    Records::AREA_CORONA, 0xff, 0xff, Records::AREA_BIANCO,
    Records::AREA_SIRENA, Records::AREA_NOKI, Records::AREA_PINNA,
    Records::AREA_RICCO, Records::AREA_CORONA,
};

constexpr char kCoreAchievementNames[] =
    "Hero of the Village\0Scrooge\0Surfin' Delfino\0Sniper\0Early Cycle\0"
    "Spring Clipper\0Yoshi Expert\0Waking Snoozakoopas\0Fluff Freighter\0"
    "Bottle Diver\0Green Demon\0Expert Spider Bouncer\0Coconut King\0"
    "Professional Diver\0Rocket Engineer\0Rope Dancer\0Bowser Bombardier\0"
    "The Classic\0Murderer\0Speedy Coins\0Ecuadorian Islands\0Boo Bouncer\0"
    "Beach Cleaner\0Boat Master\0Plungelo Plucker\0Dentist\0Shell Shocker\0"
    "Tiebreaker\0Ninja Warrior\0"
    "Platform Rider\0Oh My God\0Hot Tub\0The Long Way Up\0Exterminator\0"
    "Fish Finder\0NoHover\0Mushroom Trespassing\0Mashathon";

constexpr char kRCTimeNames[] =
    "Sand in the Hourglass\0I Just Landed\0Red Tide\0Hillside Hustle\0"
    "Then It's War!\0Casino Royale\0They See Me Rollin' (Out)\0"
    "Ghostly Reds\0Where There Is a Will There Is a Way\0Chuckster Change";

constexpr char kRC2TimeNames[] =
    "Wiggler Wrestling\0No Kidding\0"
    "Tower Titan\0Just Fishin'\0Village Life\0There it is!\0Captain Mario\0"
    "Plant Punisher\0Swimming with the fishes\0Where are they?\0"
    "Birds and Bees\0Squeaky Clean\0"
    "Red Freak\0My hat?!\0Traumatic Memories\0"
    "Flipping Insane\0Scrounger\0Nutty\0A boat a skip and a hop\0"
    "Make or Break\0Might makes right\0Dootsters?\0"
    "That bird that I hate\0Lord of the Sands\0High King";

constexpr char kStreakNames[] =
    "Getting There!\0Chuckster Chatter\0Pegged\0"
    "Disturbed the Family Vacation\0Powerwash Simulator\0"
    "Electrokoopas Are Heartless\0Clockwork Carousel\0Five Alarm Fire\0"
    "Jonathan the Tortoise\0Oral Surgeon\0Dirty Work\0Tower Defense\0"
    "Loaded Dice\0Shell Game\0No Vacancy\0Deep End Dividend\0"
    "Run Killer Conquered\0Frequent Flyer\0Sandcastle Siege\0Harbor Habit\0"
    "Dog Trainer\0Casino Circuit\0I Didn't Hear No Bell\0Mole Mangler\0"
    "Clockwork Crisis\0Lord of the Nut";

constexpr char kSpecialNames[] =
    "Grinder\0Full Run\0Completionist\0Grand Tour\0Bob Ross\0"
    "Serious Commitment\0Deja Vu\0Well Rounded\0Game Over\0All-Rounder\0"
    "Touch Grass?\0TASbot\0Trial By Sunshine";

constexpr char kChallengeDescriptions[] =
    "Finish Pinna EYG.\0Finish Gelato 8.\0Swim alive in Corona.\0"
    "Shine Gate Shine without Rocket, Turbo, or Yoshi.\0"
    "Kill all Cataquacks in Gelato 8.\0Beat Noki 8 without Hover.\0"
    "Get Gelato 8's Shine from Gelato 1 without Hover or Yoshi.\0"
    "Enter Pianta 5 Secret without Hover or Yoshi.\0"
    "Pinna 6 Secret: below Y800 to Y2700 within 15s.";

constexpr char kSpecialDescriptions[] =
    "Start 2,000 total IL attempts.\0"
    "PB every Any percent IL on one profile.\0PB every IL on one profile.\0"
    "PB every world on one profile.\0Spend two hours in Creation menus.\0"
    "Spend 30 hours in one world.\0Exactly tie an IL PB's QFT.\0"
    "Unlock one in every non-Special category.\0Die 100 times.\0"
    "Start 2,000 attempts per world.\0Play Moonshine for 1,000 hours.\0"
    "Bianco 3 Secret: exact 13.80 QFT benchmark.\0"
    "Unlock the original Bronze-through-Demon roster.";

constexpr char kRC2SpecialNames[] =
    "Ghastly!\0Spectral!\0Specular!\0Ectoplasmic!\0Wraithlike!\0Phantasmal!";

constexpr char kRC2SpecialDescriptions[] =
    "Save 1 ghost.\0Save 5 minutes of ghosts.\0Save 25 ghosts.\0"
    "Save 1 hour of ghosts.\0Save 250 ghosts lifetime.\0"
    "Save over 50 hours of ghosts lifetime.";

constexpr char kMasterySuffixes[] =
    "Beginnings!\0Specialist\0Graduate\0Master";

constexpr int packedCount(const char *pool, u32 bytes) {
    int count = 1;
    for (u32 i = 0; i + 1 < bytes; i++) {
        if (!pool[i]) count++;
    }
    return count;
}

static_assert(sizeof(kTimeRules) / sizeof(kTimeRules[0]) == 64,
              "Times rule count changed");
static_assert(kNuttyTimeIndex ==
                  kLaunchTimeCount + kRC1TimeCount +
                      Records::ACH_NUTTY - Records::ACH_WIGGLER_WRESTLING &&
                  kTimeRules[kNuttyTimeIndex].entry == 23 &&
                  kTimeRules[kNuttyTimeIndex].slotFlags == 17 &&
                  kTimeRules[kNuttyTimeIndex].valueCentis == 4500,
              "Nutty live-only rule moved");
static_assert(sizeof(kStreakRules) / sizeof(kStreakRules[0]) == 26,
              "Streak rule count changed");
static_assert(sizeof(kAnySlots) == 55, "Any-percent PB route changed");
static_assert(sizeof(kAllSlots) == 122, "all-IL PB route changed");
static_assert(sizeof(kWorldPBRules) / sizeof(kWorldPBRules[0]) ==
                  Records::WORLD_COUNT,
              "world PB ranges changed");
static_assert(sizeof(kCourseRules) / sizeof(kCourseRules[0]) == 7,
              "course mastery rules changed");
static_assert(sizeof(kAreaMap) == 0x3d, "area map changed");
static_assert(packedCount(kCoreAchievementNames,
                          sizeof(kCoreAchievementNames)) == 38,
              "core achievement name count changed");
static_assert(packedCount(kRCTimeNames, sizeof(kRCTimeNames)) == 10,
              "RC time name count changed");
static_assert(packedCount(kRC2TimeNames, sizeof(kRC2TimeNames)) == 25,
              "RC2 time name count changed");
static_assert(packedCount(kStreakNames, sizeof(kStreakNames)) == 26,
              "streak name count changed");
static_assert(packedCount(kSpecialNames, sizeof(kSpecialNames)) == 13,
              "special name count changed");
static_assert(packedCount(kChallengeDescriptions,
                          sizeof(kChallengeDescriptions)) == 9,
              "challenge description count changed");
static_assert(packedCount(kSpecialDescriptions,
                          sizeof(kSpecialDescriptions)) == 13,
              "special description count changed");
static_assert(packedCount(kRC2SpecialNames,
                          sizeof(kRC2SpecialNames)) == 6 &&
                  packedCount(kRC2SpecialDescriptions,
                              sizeof(kRC2SpecialDescriptions)) == 6,
              "RC2 special text count changed");
static_assert(Records::ACHIEVEMENT_ACTIVE_COUNT == 154,
              "achievement roster changed");
static_assert(Records::ACH_RETIRED_INDIANA_JONES + 1 ==
                  Records::ACH_BIANCO_BEGINNINGS,
              "retired achievement ID moved");
static_assert(Records::ACH_TRIAL_BY_SUNSHINE == 117,
              "persistent achievement IDs moved");
static_assert(Records::ACH_SAND_IN_THE_HOURGLASS == 118 &&
                  Records::ACH_LORD_OF_THE_NUT == 137,
              "RC1 achievement IDs moved");
static_assert(Records::ACH_WIGGLER_WRESTLING == 138 &&
                  Records::ACH_HIGH_KING == 162 &&
                  Records::ACH_GHASTLY == 163 &&
                  Records::ACH_PHANTASMAL == 168 &&
                  Records::ACHIEVEMENT_ID_END == 169,
              "RC2 achievement IDs moved");
static_assert(Records::ACHIEVEMENT_ID_END <= Records::ACHIEVEMENT_CAPACITY,
              "achievement storage capacity exceeded");
static_assert(Records::ACHIEVEMENT_ID_END <= 0x100,
              "unlock queue ID width exceeded");
static_assert(Records::ACHIEVEMENT_BYTES ==
                  SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES,
              "achievement storage capacity changed");
static_assert(Records::STAT_CAPACITY == SUSAMUNE_PROGRESS_STAT_COUNT,
              "stat storage capacity changed");
static_assert(Records::STAT_GHOSTS_SAVED == 32 &&
                  Records::STAT_GHOST_TIME_SAVED_QF == 33 &&
                  Records::STAT_COUNT == 34 &&
                  Records::STAT_COUNT <= Records::STAT_CAPACITY,
              "persistent ghost stat IDs moved");

struct RecordsState {
    u8 achievements[Records::ACHIEVEMENT_BYTES];
    u32 stats[Records::STAT_CAPACITY];
    u32 globalStats[Records::STAT_COUNT];
    Records::AchievementDesc descriptor;
    char achievementName[40];
    char achievementDescription[160];
    const s32 *activePBs;
    u32 lastAttemptSerial;
    u8 unlockQueue[kUnlockQueueSize];
    u8 streakProgress[sizeof(kStreakRules) / sizeof(kStreakRules[0])];
    u8 anyCoverage[Records::WORLD_COUNT];
    u8 allCoverage[Records::WORLD_COUNT];
    u8 categoryUnlocks[Records::CATEGORY_COUNT];
    u8 unlockedCount;
    u8 areaFrames[Records::AREA_COUNT];
    u16 activePBCount;
    s16 attemptEntry;
    u16 mashathonFrames;
    u8 unlockFirst;
    u8 unlockCount;
    u8 area;
    u8 gameArea;
    u8 episode;
    u8 actionFlags;
    u8 activeProfile;
    u8 attemptStartArea;
    u8 attemptStartEpisode;
    u8 playFrames;
    u8 creationFrames;
    bool dirty;
    bool achievementDirty;
    bool stageActive;
    bool stageEligible;
    bool wasDead;
    bool attemptActive;
    bool attemptEligible;
    bool ilAttemptAwaitingSerial;
    bool mashathonClimbing;
    bool evaluatingMeta;
    bool pbNotifyPending;
    u8 nonFrontierUnlocks;
    bool cataquacksObserved;
};

static_assert(sizeof(RecordsState) <= SUSAMUNE_RECORDS_RUNTIME_SIZE,
              "Records runtime exceeds its fixed MEM2 subwindow");

RecordsState *const sState = reinterpret_cast<RecordsState *>(
    SUSAMUNE_MEM2_RECORDS_RUNTIME_PPC_BASE);

#define sAchievements (sState->achievements)
#define sStats (sState->stats)
#define sGlobalStats (sState->globalStats)
#define sUnlockQueue (sState->unlockQueue)
#define sUnlockFirst (sState->unlockFirst)
#define sUnlockCount (sState->unlockCount)
#define sArea (sState->area)
#define sGameArea (sState->gameArea)
#define sEpisode (sState->episode)
#define sActionFlags (sState->actionFlags)
#define sStreakProgress (sState->streakProgress)
#define sAnyCoverage (sState->anyCoverage)
#define sAllCoverage (sState->allCoverage)
#define sCategoryUnlocks (sState->categoryUnlocks)
#define sUnlockedCount (sState->unlockedCount)
#define sActivePBs (sState->activePBs)
#define sActivePBCount (sState->activePBCount)
#define sActiveProfile (sState->activeProfile)
#define sAttemptEntry (sState->attemptEntry)
#define sAttemptStartArea (sState->attemptStartArea)
#define sAttemptStartEpisode (sState->attemptStartEpisode)
#define sPlayFrames (sState->playFrames)
#define sCreationFrames (sState->creationFrames)
#define sAreaFrames (sState->areaFrames)
#define sMashathonFrames (sState->mashathonFrames)
#define sLastAttemptSerial (sState->lastAttemptSerial)
#define sDirty (sState->dirty)
#define sAchievementDirty (sState->achievementDirty)
#define sStageActive (sState->stageActive)
#define sStageEligible (sState->stageEligible)
#define sWasDead (sState->wasDead)
#define sAttemptActive (sState->attemptActive)
#define sAttemptEligible (sState->attemptEligible)
#define sILAttemptAwaitingSerial (sState->ilAttemptAwaitingSerial)
#define sMashathonClimbing (sState->mashathonClimbing)
#define sEvaluatingMeta (sState->evaluatingMeta)
#define sPBNotifyPending (sState->pbNotifyPending)
#define sNonFrontierUnlocks (sState->nonFrontierUnlocks)
#define sCataquacksObserved (sState->cataquacksObserved)

__attribute__((noinline)) s32 strictQfForCentis(u32 centis) {
    if (centis == 0) return -1;
    return (s32)((centis * 1200u - 1u) / 1001u);
}

s32 inclusiveQfForCentis(u32 centis) {
    const u32 millis = centis * 10u;
    return (s32)((((millis + 1u) * 120u) - 1u) / 1001u);
}

u16 regionalValue(u16 normal, u16 jp) {
#if defined(SUSAMUNE_VERSION_JP)
    return jp;
#else
    return normal;
#endif
}

u16 timeRuleValue(u8 index) {
#if defined(SUSAMUNE_VERSION_JP)
    if (index == 6) return 12900;
    if (index == 22) return 14700;
#endif
    return kTimeRules[index].valueCentis;
}

u16 streakRuleValue(u8 index) {
    return index == kLaunchStreakCount + 4
        ? regionalValue(4400, 4300)
        : kStreakRules[index].valueCentis;
}

int timeRuleIndex(Records::AchievementId id) {
    if (id >= Records::ACH_HERO_OF_THE_VILLAGE &&
        id <= Records::ACH_NINJA_WARRIOR)
        return id - Records::ACH_HERO_OF_THE_VILLAGE;
    if (id >= Records::ACH_SAND_IN_THE_HOURGLASS &&
        id <= Records::ACH_CHUCKSTER_CHANGE)
        return kLaunchTimeCount + id - Records::ACH_SAND_IN_THE_HOURGLASS;
    if (id >= Records::ACH_WIGGLER_WRESTLING &&
        id <= Records::ACH_HIGH_KING)
        return kLaunchTimeCount + kRC1TimeCount +
               id - Records::ACH_WIGGLER_WRESTLING;
    return -1;
}

Records::AchievementId timeAchievement(u8 index) {
    if (index < kLaunchTimeCount)
        return (Records::AchievementId)(Records::ACH_HERO_OF_THE_VILLAGE + index);
    index -= kLaunchTimeCount;
    return index < kRC1TimeCount
        ? (Records::AchievementId)(Records::ACH_SAND_IN_THE_HOURGLASS + index)
        : (Records::AchievementId)(Records::ACH_WIGGLER_WRESTLING +
                                   index - kRC1TimeCount);
}

int streakRuleIndex(Records::AchievementId id) {
    if (id >= Records::ACH_GETTING_THERE &&
        id <= Records::ACH_DEEP_END_DIVIDEND)
        return id - Records::ACH_GETTING_THERE;
    if (id >= Records::ACH_RUN_KILLER_CONQUERED &&
        id <= Records::ACH_LORD_OF_THE_NUT)
        return kLaunchStreakCount + id - Records::ACH_RUN_KILLER_CONQUERED;
    return -1;
}

Records::AchievementId streakAchievement(u8 index) {
    return index < kLaunchStreakCount
        ? (Records::AchievementId)(Records::ACH_GETTING_THERE + index)
        : (Records::AchievementId)(Records::ACH_RUN_KILLER_CONQUERED +
                                   index - kLaunchStreakCount);
}

int rcTierTable(Records::Category category) {
    if (category == Records::CATEGORY_TIMES) return 0;
    if (category == Records::CATEGORY_STREAKS) return 1;
    return -1;
}

int rc2TierTable(Records::Category category) {
    if (category == Records::CATEGORY_TIMES) return 0;
    if (category == Records::CATEGORY_SPECIAL) return 1;
    return -1;
}

bool validAchievement(Records::AchievementId id) {
    return id >= Records::ACHIEVEMENT_FIRST &&
           id < Records::ACHIEVEMENT_ID_END &&
           id != Records::ACH_RETIRED_INDIANA_JONES;
}

Records::Category categoryFor(Records::AchievementId id) {
    if (id >= Records::ACH_WIGGLER_WRESTLING &&
        id <= Records::ACH_HIGH_KING)
        return Records::CATEGORY_TIMES;
    if (id >= Records::ACH_GHASTLY && id <= Records::ACH_PHANTASMAL)
        return Records::CATEGORY_SPECIAL;
    if (id >= Records::ACH_SAND_IN_THE_HOURGLASS &&
        id <= Records::ACH_CHUCKSTER_CHANGE)
        return Records::CATEGORY_TIMES;
    if (id >= Records::ACH_RUN_KILLER_CONQUERED &&
        id <= Records::ACH_LORD_OF_THE_NUT)
        return Records::CATEGORY_STREAKS;
    if (id < Records::ACH_PLATFORM_RIDER) return Records::CATEGORY_TIMES;
    if (id < Records::ACH_BIANCO_BEGINNINGS)
        return Records::CATEGORY_CHALLENGES;
    if (id < Records::ACH_GETTING_THERE)
        return Records::CATEGORY_COURSE_MASTERY;
    if (id < Records::ACH_GRINDER) return Records::CATEGORY_STREAKS;
    return id <= Records::ACH_TRIAL_BY_SUNSHINE
        ? Records::CATEGORY_SPECIAL : Records::CATEGORY_COUNT;
}

Records::Tier tierFor(Records::AchievementId id) {
    const Records::Category category = categoryFor(id);
    if (category >= Records::CATEGORY_COUNT) return Records::TIER_COUNT;
    const bool rc2 = id >= Records::ACH_WIGGLER_WRESTLING;
    const bool rc1 = !rc2 && id >= Records::ACH_SAND_IN_THE_HOURGLASS;
    const int rc1Table = rcTierTable(category);
    const int rc2Table = rc2TierTable(category);
    const u8 *first = rc2 ? kRC2TierFirst[rc2Table]
                          : rc1 ? kRCTierFirst[rc1Table]
                                : kLaunchTierFirst[category];
    for (int tier = Records::TIER_COUNT - 1; tier >= 0; tier--) {
        const u8 count = rc2
            ? kRC2TierCounts[rc2Table][tier]
            : rc1 ? kRCTierCounts[rc1Table][tier]
                  : kTierCounts[category][tier] -
                        (rc1Table >= 0 ? kRCTierCounts[rc1Table][tier] : 0) -
                        (rc2Table >= 0 ? kRC2TierCounts[rc2Table][tier] : 0);
        if (count && id >= first[tier]) return (Records::Tier)tier;
    }
    return Records::TIER_COUNT;
}

bool bitUnlocked(Records::AchievementId id) {
    if (!validAchievement(id)) return false;
    const u16 value = (u16)id;
    return (sAchievements[value >> 3] & (1u << (value & 7))) != 0;
}

void rebuildUnlockCounts() {
    memset(sCategoryUnlocks, 0, sizeof(sState->categoryUnlocks));
    sUnlockedCount = 0;
    sNonFrontierUnlocks = 0;
    for (u16 value = Records::ACHIEVEMENT_FIRST;
         value < Records::ACHIEVEMENT_ID_END; value++) {
        const Records::AchievementId id = (Records::AchievementId)value;
        if (!bitUnlocked(id)) continue;
        sUnlockedCount++;
        sCategoryUnlocks[categoryFor(id)]++;
        if (value <= Records::ACH_TASBOT &&
            tierFor(id) != Records::TIER_FRONTIER)
            sNonFrontierUnlocks++;
    }
}

void evaluateMetaAchievements(bool notify);

void queueUnlock(Records::AchievementId id) {
    if (sUnlockCount == kUnlockQueueSize) return;
    const u8 tail = (u8)((sUnlockFirst + sUnlockCount) % kUnlockQueueSize);
    sUnlockQueue[tail] = (u8)id;
    sUnlockCount++;
}

void unlock(Records::AchievementId id, bool notify = true) {
    if (!validAchievement(id)) return;
    const u16 value = (u16)id;
    const u8 mask = (u8)(1u << (value & 7));
    u8 &slot = sAchievements[value >> 3];
    if (slot & mask) return;
    slot |= mask;
    sUnlockedCount++;
    sCategoryUnlocks[categoryFor(id)]++;
    if (id <= Records::ACH_TASBOT && tierFor(id) != Records::TIER_FRONTIER)
        sNonFrontierUnlocks++;
    sDirty = true;
    sAchievementDirty = true;
    if (notify) queueUnlock(id);
    evaluateMetaAchievements(notify);
}

__attribute__((noinline)) u32 saturatedAdd(u32 a, u32 b) {
    return b > kU32Max - a ? kU32Max : a + b;
}

void evaluateStatAchievements(bool notify);

void addStat(Records::StatId id, u32 amount) {
    if (id >= Records::STAT_COUNT || amount == 0) return;
    const u32 next = saturatedAdd(sStats[id], amount);
    if (next == sStats[id]) return;
    sStats[id] = next;
    sGlobalStats[id] = saturatedAdd(sGlobalStats[id], amount);
    sDirty = true;
    evaluateStatAchievements(true);
}

void countSecond(Records::StatId id, u8 &frames) {
    // All supported Susamune revisions run at 30 fps.
    if (++frames < 30) return;
    frames = 0;
    addStat(id, 1);
}

bool liveStage() {
    return sStageActive && sArea < Records::AREA_COUNT &&
           gpApplication.mContext == TApplication::CONTEXT_DIRECT_STAGE &&
           gpMarDirector && gpMarDirector->_260 != 0 &&
           gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING;
}

__attribute__((noinline)) bool introSkipEnabled() {
    return gSettings.getBool(SETTING_STAGE_INTRO_SKIP);
}

__attribute__((noinline)) void resetAttemptActions() {
    sActionFlags = 0;
    sMashathonFrames = 0;
    sMashathonClimbing = false;
    sCataquacksObserved = false;
}

void updateCataquacks() {
    if (!sAttemptActive || !sAttemptEligible || sAttemptEntry != 35 ||
        sGameArea != 4 || sEpisode != 7 || !gpConductor)
        return;
    TEnemyManager *manager = reinterpret_cast<TEnemyManager *>(
        gpConductor->getManagerByName(
            "\x83\x7C\x83\x43\x83\x6E\x83\x69\x83\x7D\x83\x6C"
            "\x81\x5B\x83\x57\x83\x83\x81\x5B"));
    if (!manager || manager->mObjCount != 27) return;
    if (manager->countLivingEnemy() > 0)
        sCataquacksObserved = true;
    else if (sCataquacksObserved) {
        sCataquacksObserved = false;
        Records::onCataquacksCleared();
    }
}

void resetAllStreaks() {
    memset(sStreakProgress, 0, sizeof(sState->streakProgress));
}

void resetWrongEntryStreaks(int entry) {
    for (u32 i = 0; i < sizeof(kStreakRules) / sizeof(kStreakRules[0]); i++) {
        if (kStreakRules[i].entry != entry) sStreakProgress[i] = 0;
    }
}

int canonicalResultEntry(int entry) {
    // Older playlists express GBS as Gelato 1 plus a result alias to Gelato 8.
    return entry == 35 && sAttemptStartArea == 4 &&
                   sAttemptStartEpisode == 0
               ? 121
               : entry;
}

void failCurrentAttempt() {
    if (sAttemptActive) resetAllStreaks();
    sAttemptEligible = false;
    resetAttemptActions();
}

bool slotSet(const s32 *pbs, u16 count, u8 slot) {
    return pbs && slot < count && pbs[slot] >= 0;
}

// High byte is coverage; low 24 bits are the QFT sum. Every supported PB sum
// is far below 24 bits, so one scan serves both mastery and Records UI.
u32 pbMetrics(const s32 *pbs, u16 count, const u8 *slots, u8 slotCount) {
    if (!pbs) return 0;
    u32 total = 0;
    u8 found = 0;
    for (u8 i = 0; i < slotCount; i++) {
        const u8 slot = slots[i];
        if (slot < count && pbs[slot] >= 0) {
            total += pbs[slot];
            found++;
        }
    }
    return ((u32)found << 24) | total;
}

void evaluatePBProfile(const s32 *pbs, u16 count, bool notify, bool cache) {
    if (!pbs) return;

    for (u32 i = 0; i < sizeof(kTimeRules) / sizeof(kTimeRules[0]); i++) {
        const TimeRule &rule = kTimeRules[i];
        // An old PB has no route-action metadata, so Nutty is live-only.
        if ((rule.slotFlags & TIME_IGT) || i == kNuttyTimeIndex) continue;
        const u8 slot = rule.slotFlags & ~TIME_IGT;
        if (slotSet(pbs, count, slot) &&
            pbs[slot] <= strictQfForCentis(timeRuleValue(i))) {
            unlock(timeAchievement(i), notify);
        }
    }

    u8 anyTotal = 0;
    u8 allTotal = 0;
    u8 worldsVisited = 0;
    for (u8 course = 0; course < 7; course++) {
        const WorldPBRule &slots = kWorldPBRules[course];
        const CourseRule &rule = kCourseRules[course];
        const u32 anyMetrics = pbMetrics(pbs, count,
            kAnySlots + slots.anyFirst, slots.anyCount);
        const u32 allMetrics = pbMetrics(pbs, count,
            kAllSlots + slots.allFirst, slots.allCount);
        const u8 any = anyMetrics >> 24;
        const u8 all = allMetrics >> 24;
        anyTotal += any;
        allTotal += all;
        if (all) worldsVisited++;
        if (cache) {
            sAnyCoverage[course] = any;
            sAllCoverage[course] = all;
        }
        if (any == slots.anyCount) {
            unlock((Records::AchievementId)(Records::ACH_BIANCO_BEGINNINGS + course),
                   notify);
            if ((anyMetrics & 0xffffffu) <= (u32)strictQfForCentis(
                    regionalValue(rule.anySeconds, rule.jpAnySeconds) * 100u)) {
                unlock((Records::AchievementId)(Records::ACH_BIANCO_SPECIALIST + course),
                       notify);
            }
        }
        if (all == slots.allCount) {
            unlock((Records::AchievementId)(Records::ACH_BIANCO_GRADUATE + course),
                   notify);
            if ((allMetrics & 0xffffffu) <= (u32)strictQfForCentis(
                    regionalValue(rule.allSeconds, rule.jpAllSeconds) * 100u)) {
                unlock((Records::AchievementId)(Records::ACH_BIANCO_MASTER + course),
                       notify);
            }
        }
    }

    const WorldPBRule &delfino = kWorldPBRules[Records::WORLD_DELFINO];
    const u8 delfinoAny = pbMetrics(pbs, count,
        kAnySlots + delfino.anyFirst, delfino.anyCount) >> 24;
    const u8 delfinoAll = pbMetrics(pbs, count,
        kAllSlots + delfino.allFirst, delfino.allCount) >> 24;
    anyTotal += delfinoAny;
    allTotal += delfinoAll;
    if (delfinoAll) worldsVisited++;
    if (cache) {
        sAnyCoverage[Records::WORLD_DELFINO] = delfinoAny;
        sAllCoverage[Records::WORLD_DELFINO] = delfinoAll;
    }

    if (anyTotal == sizeof(kAnySlots))
        unlock(Records::ACH_FULL_RUN, notify);
    if (allTotal == sizeof(kAllSlots))
        unlock(Records::ACH_COMPLETIONIST, notify);
    if (worldsVisited == Records::WORLD_COUNT)
        unlock(Records::ACH_GRAND_TOUR, notify);
    if (70 < count && pbs[70] >= 0 && pbs[70] <= kTasbotQf)
        unlock(Records::ACH_TASBOT, notify);
}

u32 worldSeconds(const u32 *stats, Records::World world) {
    if (!stats || world >= Records::WORLD_COUNT) return 0;
    if (world == Records::WORLD_DELFINO) {
        u32 total = stats[Records::STAT_AREA_AIRSTRIP_SECONDS];
        total = saturatedAdd(total, stats[Records::STAT_AREA_DELFINO_SECONDS]);
        return saturatedAdd(total, stats[Records::STAT_AREA_CORONA_SECONDS]);
    }
    return stats[Records::STAT_AREA_BIANCO_SECONDS + world];
}

u32 maxWorldSeconds() {
    u32 best = 0;
    for (u8 world = 0; world < Records::WORLD_COUNT; world++) {
        const u32 value = worldSeconds(sGlobalStats, (Records::World)world);
        if (value > best) best = value;
    }
    return best;
}

u32 minWorldAttempts() {
    u32 least = kU32Max;
    for (u8 world = 0; world < Records::WORLD_COUNT; world++) {
        const u32 value = sGlobalStats[Records::STAT_WORLD_BIANCO_ATTEMPTS + world];
        if (value < least) least = value;
    }
    return least == kU32Max ? 0 : least;
}

void evaluateStatAchievements(bool notify) {
    if (sGlobalStats[Records::STAT_ATTEMPTS] >= 2000)
        unlock(Records::ACH_GRINDER, notify);
    for (u8 world = 0; world < Records::WORLD_COUNT; world++) {
        if (worldSeconds(sGlobalStats, (Records::World)world) >= 15u * 60u * 60u)
            unlock((Records::AchievementId)(Records::ACH_BIANCO_ENTHUSIAST + world),
                   notify);
    }
    if (sGlobalStats[Records::STAT_CREATION_SECONDS] >= 2u * 60u * 60u)
        unlock(Records::ACH_BOB_ROSS, notify);
    if (maxWorldSeconds() >= 30u * 60u * 60u)
        unlock(Records::ACH_SERIOUS_COMMITMENT, notify);
    if (sGlobalStats[Records::STAT_DEATHS] >= 100)
        unlock(Records::ACH_GAME_OVER, notify);
    if (minWorldAttempts() >= 2000)
        unlock(Records::ACH_ALL_ROUNDER, notify);
    if (sGlobalStats[Records::STAT_PLAY_SECONDS] >= 1000u * 60u * 60u)
        unlock(Records::ACH_TOUCH_GRASS, notify);

    const u32 ghosts = sGlobalStats[Records::STAT_GHOSTS_SAVED];
    const u32 ghostQf = sGlobalStats[Records::STAT_GHOST_TIME_SAVED_QF];
    if (ghosts >= 1u) unlock(Records::ACH_GHASTLY, notify);
    if (ghostQf >= kGhostFiveMinutesQf)
        unlock(Records::ACH_SPECTRAL, notify);
    if (ghosts >= 25u) unlock(Records::ACH_SPECULAR, notify);
    if (ghostQf >= kGhostOneHourQf)
        unlock(Records::ACH_ECTOPLASMIC, notify);
    if (ghosts >= 250u) unlock(Records::ACH_WRAITHLIKE, notify);
    if (ghostQf >= kGhostOverFiftyHoursQf)
        unlock(Records::ACH_PHANTASMAL, notify);
}

u8 unlockedCategoryCount(Records::Category category) {
    return category < Records::CATEGORY_COUNT ? sCategoryUnlocks[category] : 0;
}

void evaluateMetaAchievements(bool notify) {
    if (sEvaluatingMeta) return;
    sEvaluatingMeta = true;

    bool rounded = true;
    for (u8 category = Records::CATEGORY_TIMES;
         category < Records::CATEGORY_SPECIAL; category++) {
        if (unlockedCategoryCount((Records::Category)category) == 0) {
            rounded = false;
            break;
        }
    }
    if (rounded) unlock(Records::ACH_WELL_ROUNDED, notify);

    // This upper bound freezes the V1.2 launch roster. Future achievements do
    // not silently make Trial By Sunshine harder.
    if (sNonFrontierUnlocks == kTrialGoal)
        unlock(Records::ACH_TRIAL_BY_SUNSHINE, notify);

    sEvaluatingMeta = false;
}

void formatCentis(u32 centis, char *out, bool nativeIgt) {
    const u32 minutes = centis / 6000u;
    const u32 seconds = (centis / 100u) % 60u;
    const u32 fraction = centis % 100u;
    if (nativeIgt)
        sprintf(out, "%lu:%02lu.%02lu", minutes, seconds, fraction);
    else
        sprintf(out, "%lu:%02lu.%02lu0", minutes, seconds, fraction);
}

const char *challengeDescription(Records::AchievementId id) {
    return PackedText::at(kChallengeDescriptions,
                          id - Records::ACH_PLATFORM_RIDER);
}

const char *specialDescription(Records::AchievementId id) {
    const int index = id <= Records::ACH_COMPLETIONIST
        ? id - Records::ACH_GRINDER
        : 3 + id - Records::ACH_GRAND_TOUR;
    return PackedText::at(kSpecialDescriptions, index);
}

const char *makeDescription(Records::AchievementId id, char *out) {
    const int timeIndex = timeRuleIndex(id);
    if (timeIndex >= 0) {
        const TimeRule &rule = kTimeRules[timeIndex];
        const u16 value = timeRuleValue(timeIndex);
        char time[16];
        formatCentis(value, time, (rule.slotFlags & TIME_IGT) != 0);
        if (id == Records::ACH_NUTTY)
            sprintf(out, "%s: under %s QFT without Yoshi.",
                    ILing::label(rule.entry), time);
        else if (rule.slotFlags & TIME_IGT)
            sprintf(out, "%s: %s or more remaining on IGT.",
                    ILing::label(rule.entry), time);
        else
            sprintf(out, "%s: under %s QFT.", ILing::label(rule.entry), time);
        return out;
    }

    if (id >= Records::ACH_PLATFORM_RIDER && id <= Records::ACH_MASHATHON)
        return challengeDescription(id);

    if (id >= Records::ACH_BIANCO_BEGINNINGS &&
        id <= Records::ACH_PIANTA_MASTER) {
        const int ordinal = id - Records::ACH_BIANCO_BEGINNINGS;
        const u8 tier = ordinal / 7;
        const u8 course = ordinal % 7;
        const CourseRule &rule = kCourseRules[course];
        char time[16];
        if (tier == 0)
            sprintf(out, "%s Any percent: set every IL PB.",
                    Records::worldName((Records::World)course));
        else if (tier == 1) {
            formatCentis(regionalValue(rule.anySeconds, rule.jpAnySeconds) * 100u,
                         time, false);
            sprintf(out, "%s Any percent PB sum: under %s.",
                    Records::worldName((Records::World)course), time);
        } else if (tier == 2)
            sprintf(out, "%s: set every IL PB.",
                    Records::worldName((Records::World)course));
        else {
            formatCentis(regionalValue(rule.allSeconds, rule.jpAllSeconds) * 100u,
                         time, false);
            sprintf(out, "%s all-IL PB sum: under %s.",
                    Records::worldName((Records::World)course), time);
        }
        return out;
    }

    const int streakIndex = streakRuleIndex(id);
    if (streakIndex >= 0) {
        const StreakRule &rule = kStreakRules[streakIndex];
        const u16 value = streakRuleValue(streakIndex);
        if (rule.flags == STREAK_FINISH) {
            sprintf(out, "%s: %u finishes in a row.",
                    ILing::label(rule.entry), rule.goal);
        } else {
            char time[16];
            formatCentis(value, time, rule.flags == STREAK_IGT);
            if (rule.flags == STREAK_IGT)
                sprintf(out, "%s: %s IGT, %u in a row.",
                        ILing::label(rule.entry), time, rule.goal);
            else
                sprintf(out, "%s: %s or better, %u in a row.",
                        ILing::label(rule.entry), time, rule.goal);
        }
        return out;
    }

    if (id >= Records::ACH_BIANCO_ENTHUSIAST &&
        id <= Records::ACH_DELFINO_ENTHUSIAST) {
        const Records::World world =
            (Records::World)(id - Records::ACH_BIANCO_ENTHUSIAST);
        sprintf(out, "Spend 15 total hours in %s.", Records::worldName(world));
        return out;
    }
    if (id >= Records::ACH_GHASTLY && id <= Records::ACH_PHANTASMAL)
        return PackedText::at(kRC2SpecialDescriptions,
                              id - Records::ACH_GHASTLY);
    return specialDescription(id);
}

bool streakSucceeded(u8 index, s32 qf, s32 igtCentis) {
    const StreakRule &rule = kStreakRules[index];
    const u16 value = streakRuleValue(index);
    switch (rule.flags) {
    case STREAK_FINISH:
        return true;
    case STREAK_QFT:
        return qf >= 0 && qf <= strictQfForCentis(value);
    case STREAK_QFT_INCLUSIVE:
        return qf >= 0 && qf <= inclusiveQfForCentis(value);
    case STREAK_IGT:
        return igtCentis >= 0 && (u32)igtCentis >= value;
    default:
        return false;
    }
}

void resetRuntimeObservers() {
    sLastAttemptSerial = gQFTTimer.attemptSerial();
    sWasDead = gpMarioOriginal && gpMarioOriginal->mAttributes.mIsGameOver;
}

__attribute__((noinline)) void noteAction(u8 action) {
    if (sAttemptActive && sAttemptEligible) sActionFlags |= action;
}

}  // namespace

namespace Records {

void init() {
    memset(sState, 0, sizeof(*sState));
    sArea = AREA_INVALID;
    sGameArea = 0xff;
    sEpisode = 0xff;
    sAttemptEntry = -1;
    sAttemptStartArea = 0xff;
    sAttemptStartEpisode = 0xff;
    resetRuntimeObservers();
}

void adopt(const u8 achievements[ACHIEVEMENT_BYTES],
           const u32 stats[STAT_CAPACITY]) {
    if (achievements) memcpy(sAchievements, achievements, sizeof(sAchievements));
    if (stats) {
        memcpy(sStats, stats, sizeof(sStats));
        memcpy(sGlobalStats, stats, sizeof(sGlobalStats));
    }
    sUnlockFirst = 0;
    sUnlockCount = 0;
    rebuildUnlockCounts();
    sDirty = false;
    sAchievementDirty = false;
    resetRuntimeObservers();
    evaluateMetaAchievements(false);
}

void stageInto(u8 achievements[ACHIEVEMENT_BYTES],
               u32 stats[STAT_CAPACITY]) {
    if (achievements) memcpy(achievements, sAchievements, sizeof(sAchievements));
    if (stats) memcpy(stats, sStats, sizeof(sStats));
}

void reconcileRegionalStats(const u32 regional[][STAT_CAPACITY],
                            u8 regionCount) {
    if (!regional) return;
    if (regionCount > SUSAMUNE_PROGRESS_REGION_COUNT)
        regionCount = SUSAMUNE_PROGRESS_REGION_COUNT;
    memset(sGlobalStats, 0, sizeof(sGlobalStats));
    for (u8 region = 0; region < regionCount; region++) {
        for (u8 id = 0; id < STAT_COUNT; id++)
            sGlobalStats[id] = saturatedAdd(sGlobalStats[id], regional[region][id]);
    }
    evaluateStatAchievements(false);
    sUnlockFirst = 0;
    sUnlockCount = 0;
}

void update(bool creationEditing, bool observerFrame) {
    const u32 serial = gQFTTimer.attemptSerial();
    if (observerFrame) {
        // Watch owns the stage but not the runner's persistent statistics.
        sLastAttemptSerial = serial;
        sWasDead = false;
        return;
    }

    const bool stageLive = liveStage();
    countSecond(STAT_PLAY_SECONDS, sPlayFrames);
    if (stageLive) {
        if (creationEditing)
            countSecond(STAT_CREATION_SECONDS, sCreationFrames);
        countSecond(areaStat((Area)sArea), sAreaFrames[sArea]);
    }

    if (introSkipEnabled() && sAttemptEligible) invalidateAttempt();

    if (serial != sLastAttemptSerial) {
        sLastAttemptSerial = serial;
        if (stageLive) {
            addStat(STAT_ATTEMPTS, 1);
            const World world = worldForArea((Area)sArea);
            addStat(worldAttemptStat(world), 1);
        }
        // onILAttemptStarted arms before QFT advances its serial. Any later
        // serial change is an abandoned same-scene attempt and starts fresh.
        if (sAttemptActive && !sILAttemptAwaitingSerial) {
            resetAllStreaks();
            resetAttemptActions();
            sAttemptEligible = !introSkipEnabled();
            sStageEligible = sAttemptEligible;
            sAttemptStartArea = sGameArea;
            sAttemptStartEpisode = sEpisode;
        }
        sILAttemptAwaitingSerial = false;
    }

    const bool dead = stageLive && gpMarioOriginal &&
                      gpMarioOriginal->mAttributes.mIsGameOver;
    if (dead && !sWasDead) {
        addStat(STAT_DEATHS, 1);
        failCurrentAttempt();
        sStageEligible = false;
    }
    sWasDead = dead;

    if (stageLive && gpMarioOriginal) {
        if (sAttemptActive && sAttemptEligible && gpMarioOriginal->mYoshi &&
            gpMarioOriginal->mYoshi->mState == TYoshi::MOUNTED)
            sActionFlags |= ACTION_YOSHI;
        if (sAttemptActive && sAttemptEligible && gpMarioOriginal->mFludd) {
            const TWaterGun *fludd = gpMarioOriginal->mFludd;
            if (fludd->mIsEmitWater) {
                if (fludd->mCurrentNozzle == TWaterGun::Hover)
                    sActionFlags |= ACTION_HOVER;
                else if (fludd->mCurrentNozzle == TWaterGun::Rocket)
                    sActionFlags |= ACTION_ROCKET;
                else if (fludd->mCurrentNozzle == TWaterGun::Turbo)
                    sActionFlags |= ACTION_TURBO;
            }
        }
        if (gpMarioOriginal->checkStatusType(0x2000)) onHotTubSwimming();
        onMarioHeight(gpMarioOriginal->mTranslation.y);
        updateCataquacks();
    }

    if (sMashathonClimbing && stageLive) {
        if (sMashathonFrames < 0xffff) sMashathonFrames++;
        if (sMashathonFrames > kMashathonFrames) {
            sMashathonClimbing = false;
            sMashathonFrames = 0;
        }
    }
}

void onStageSetup(u8 area, u8 episode) {
    sGameArea = area;
    sEpisode = episode;
    sArea = classifyArea(area);
    sStageActive = sArea != AREA_INVALID;
    // ILing arms before setupObjects(), so the first loaded scene owns the
    // attempt origin. Child-scene transitions happen after this flag clears.
    if (sAttemptActive && sILAttemptAwaitingSerial) {
        sAttemptStartArea = area;
        sAttemptStartEpisode = episode;
    }
    if (!sAttemptActive) sStageEligible = !introSkipEnabled();
    sWasDead = false;

    if (area == 0x2A && sAttemptActive && sAttemptEligible &&
        sAttemptEntry == 82 &&
        !(sActionFlags & (ACTION_HOVER | ACTION_YOSHI))) {
        unlock(ACH_MUSHROOM_TRESPASSING);
    }
}

void onStageExit() {
    sStageActive = false;
    sArea = AREA_INVALID;
    sWasDead = false;
}

void onILAttemptStarted(int entry) {
    if (sAttemptActive) failCurrentAttempt();
    sAttemptEntry = (s16)entry;
    sAttemptActive = true;
    sAttemptEligible = !introSkipEnabled();
    sStageEligible = sAttemptEligible;
    sAttemptStartArea = sGameArea;
    sAttemptStartEpisode = sEpisode;
    sILAttemptAwaitingSerial = true;
    resetAttemptActions();
}

void onILAttemptEnded() {
    if (sAttemptActive) failCurrentAttempt();
    sAttemptActive = false;
    sAttemptEligible = false;
    sAttemptEntry = -1;
    sILAttemptAwaitingSerial = false;
    resetAttemptActions();
}

void onILResult(int entry, u8 pbSlot, s32 qf, s32 igtCentis,
                bool challengeEligible) {
    const int routeEntry = canonicalResultEntry(entry);
    addStat(STAT_IL_FINISHES, 1);
    addStat(worldFinishStat(worldForEntry(routeEntry)), 1);

    const bool eligible = challengeEligible && sAttemptEligible &&
                          !introSkipEnabled();
    if (!eligible) {
        failCurrentAttempt();
        sAttemptActive = false;
        sAttemptEntry = -1;
        sILAttemptAwaitingSerial = false;
        return;
    }

    // Streaks follow consecutive completed results. Some valid routes start
    // from a different catalog entry than the result they produce.
    resetWrongEntryStreaks(routeEntry);

    if (sActivePBs && pbSlot < sActivePBCount &&
        sActivePBs[pbSlot] >= 0 && sActivePBs[pbSlot] == qf)
        unlock(ACH_DEJA_VU);

    for (u32 i = 0; i < sizeof(kTimeRules) / sizeof(kTimeRules[0]); i++) {
        const TimeRule &rule = kTimeRules[i];
        if (routeEntry != rule.entry) continue;
        const u16 value = timeRuleValue(i);
        const bool passed = (rule.slotFlags & TIME_IGT)
            ? igtCentis >= 0 && (u32)igtCentis >= value
            : qf >= 0 && qf <= strictQfForCentis(value);
        if (passed &&
            (i != kNuttyTimeIndex || !(sActionFlags & ACTION_YOSHI)))
            unlock(timeAchievement(i));
    }

    if (entry == 46) unlock(ACH_PLATFORM_RIDER);
    if (entry == 35 || routeEntry == 121) unlock(ACH_OH_MY_GOD);
    if (entry == 75 && !(sActionFlags & ACTION_HOVER))
        unlock(ACH_FISH_FINDER);
    if (entry == 105 &&
        !(sActionFlags & (ACTION_ROCKET | ACTION_TURBO | ACTION_YOSHI)))
        unlock(ACH_THE_LONG_WAY_UP);
    if (routeEntry == 121 &&
        !(sActionFlags & (ACTION_YOSHI | ACTION_HOVER)))
        unlock(ACH_NOHOVER);

    for (u32 i = 0; i < sizeof(kStreakRules) / sizeof(kStreakRules[0]); i++) {
        const StreakRule &rule = kStreakRules[i];
        if (routeEntry != rule.entry) continue;
        if (streakSucceeded(i, qf, igtCentis)) {
            if (sStreakProgress[i] < rule.goal) sStreakProgress[i]++;
            if (sStreakProgress[i] >= rule.goal)
                unlock(streakAchievement(i));
        } else {
            sStreakProgress[i] = 0;
        }
    }

    if (entry == 3 && qf >= 0 && qf <= kTasbotQf) unlock(ACH_TASBOT);

    sAttemptActive = false;
    sAttemptEligible = false;
    sAttemptEntry = -1;
    sILAttemptAwaitingSerial = false;
    resetAttemptActions();
}

void onPBAccepted(int, u8) {
    addStat(STAT_PBS_EARNED, 1);
    sPBNotifyPending = true;
}

void onGhostSaved(u32 durationQf) {
    if (durationQf == 0) return;
    addStat(STAT_GHOSTS_SAVED, 1);
    addStat(STAT_GHOST_TIME_SAVED_QF, durationQf);
}

void reconcilePBProfiles(const s32 *profiles, u16 slotCount, u8 profileCount,
                         u8 activeProfile) {
    if (!profiles || slotCount == 0 || profileCount == 0) {
        sActivePBs = nullptr;
        sActivePBCount = 0;
        memset(sAnyCoverage, 0, sizeof(sAnyCoverage));
        memset(sAllCoverage, 0, sizeof(sAllCoverage));
        return;
    }
    if (activeProfile >= profileCount) activeProfile = 0;
    const bool notifyPB = sPBNotifyPending;
    for (u8 profile = 0; profile < profileCount; profile++)
        evaluatePBProfile(profiles + (u32)profile * slotCount,
                          slotCount,
                          notifyPB && profile == activeProfile,
                          profile == activeProfile);
    sPBNotifyPending = false;
    sActiveProfile = activeProfile;
    sActivePBs = profiles + (u32)activeProfile * slotCount;
    sActivePBCount = slotCount;
    if (!notifyPB) {
        sUnlockFirst = 0;
        sUnlockCount = 0;
    }
}

void onYoshiMounted() {
    noteAction(ACTION_YOSHI);
}

void onHotTubSwimming() {
    if (liveStage() && sStageEligible && sGameArea == 0x34 &&
        gpMarioOriginal && !gpMarioOriginal->mAttributes.mIsGameOver)
        unlock(ACH_HOT_TUB);
}

void onMarioHeight(float y) {
    if (!sAttemptActive || !sAttemptEligible || sGameArea != 0x29) return;
    if (!sMashathonClimbing) {
        if (y < 800.0f) {
            sMashathonClimbing = true;
            sMashathonFrames = 0;
        }
        return;
    }
    if (y >= 2700.0f && sMashathonFrames <= kMashathonFrames) {
        unlock(ACH_MASHATHON);
        sMashathonClimbing = false;
    }
}

void onCataquacksCleared() {
    if (sAttemptActive && sAttemptEligible && sAttemptEntry == 35)
        unlock(ACH_EXTERMINATOR);
}

void invalidateAttempt() {
    failCurrentAttempt();
    sStageEligible = false;
}

void onSavestateLoaded() {
    invalidateAttempt();
    resetAllStreaks();
    sAttemptActive = false;
    sAttemptEntry = -1;
    sILAttemptAwaitingSerial = false;
    resetRuntimeObservers();
}

u32 stat(StatId id) {
    return id < STAT_COUNT ? sStats[id] : 0;
}

StatId areaStat(Area area) {
    return area < AREA_COUNT
        ? (StatId)(STAT_AREA_AIRSTRIP_SECONDS + area)
        : STAT_COUNT;
}

StatId worldAttemptStat(World world) {
    return world < WORLD_COUNT
        ? (StatId)(STAT_WORLD_BIANCO_ATTEMPTS + world)
        : STAT_COUNT;
}

StatId worldFinishStat(World world) {
    return world < WORLD_COUNT
        ? (StatId)(STAT_WORLD_BIANCO_FINISHES + world)
        : STAT_COUNT;
}

const char *worldName(World world) {
    static const char names[] =
        "Bianco\0Ricco\0Gelato\0Pinna\0Sirena\0Noki\0Pianta\0Delfino";
    static const u8 offsets[] = {0, 7, 13, 20, 26, 33, 38, 45};
    return world < WORLD_COUNT ? names + offsets[world] : "";
}

const char *currentRegionScope() {
#if defined(SUSAMUNE_VERSION_JP)
    return "JP";
#elif defined(SUSAMUNE_VERSION_US)
    return "US";
#else
    return "PAL";
#endif
}

Area classifyArea(u8 area) {
    return area < sizeof(kAreaMap) ? (Area)kAreaMap[area] : AREA_INVALID;
}

World worldForArea(Area area) {
    switch (area) {
    case AREA_BIANCO: return WORLD_BIANCO;
    case AREA_RICCO: return WORLD_RICCO;
    case AREA_GELATO: return WORLD_GELATO;
    case AREA_PINNA: return WORLD_PINNA;
    case AREA_SIRENA: return WORLD_SIRENA;
    case AREA_NOKI: return WORLD_NOKI;
    case AREA_PIANTA: return WORLD_PIANTA;
    case AREA_AIRSTRIP:
    case AREA_DELFINO:
    case AREA_CORONA: return WORLD_DELFINO;
    default: return WORLD_INVALID;
    }
}

World worldForEntry(int entry) {
    if (entry == 121) return WORLD_GELATO;
    if (entry >= 0 && entry < 13) return WORLD_BIANCO;
    if (entry < 25) return entry >= 13 ? WORLD_RICCO : WORLD_INVALID;
    if (entry < 38) return WORLD_GELATO;
    if (entry < 52) return WORLD_PINNA;
    if (entry < 65) return WORLD_SIRENA;
    if (entry < 78) return WORLD_NOKI;
    if (entry < 90) return WORLD_PIANTA;
    if (entry < 121) return WORLD_DELFINO;
    return WORLD_INVALID;
}

bool worldPBSummary(World world, bool allILs, u8 *covered, u8 *goal,
                    s32 *sumQf) {
    if (world >= WORLD_COUNT) return false;
    const WorldPBRule &rule = kWorldPBRules[world];
    const u8 count = allILs ? rule.allCount : rule.anyCount;
    const u8 first = allILs ? rule.allFirst : rule.anyFirst;
    const u8 *slots = allILs ? kAllSlots : kAnySlots;
    const u8 found = allILs ? sAllCoverage[world] : sAnyCoverage[world];
    if (covered) *covered = found;
    if (goal) *goal = count;
    if (!sumQf || !sActivePBs || found != count) return false;
    *sumQf = pbMetrics(sActivePBs, sActivePBCount,
                       slots + first, count) & 0xffffffu;
    return true;
}

int achievementCount() { return ACHIEVEMENT_ACTIVE_COUNT; }

int unlockedCount() { return sUnlockedCount; }

int categoryUnlockedCount(Category category) {
    return category < CATEGORY_COUNT ? sCategoryUnlocks[category] : 0;
}

const AchievementDesc *achievement(AchievementId id) {
    AchievementDesc &desc = sState->descriptor;
    char *name = sState->achievementName;
    char *description = sState->achievementDescription;
    if (!validAchievement(id)) return nullptr;
    desc.id = id;
    desc.tier = tierFor(id);
    desc.category = categoryFor(id);
    if (id <= ACH_MASHATHON) {
        desc.name = PackedText::at(kCoreAchievementNames,
                                   id - ACHIEVEMENT_FIRST);
    } else if (id <= ACH_PIANTA_MASTER) {
        const int mastery = id - ACH_BIANCO_BEGINNINGS;
        sprintf(name, "%s %s", worldName((World)(mastery % 7)),
                PackedText::at(kMasterySuffixes, mastery / 7));
        desc.name = name;
    } else if (id <= ACH_DEEP_END_DIVIDEND) {
        desc.name = PackedText::at(kStreakNames, id - ACH_GETTING_THERE);
    } else if (id >= ACH_BIANCO_ENTHUSIAST &&
               id <= ACH_DELFINO_ENTHUSIAST) {
        sprintf(name, "%s Enthusiast",
                worldName((World)(id - ACH_BIANCO_ENTHUSIAST)));
        desc.name = name;
    } else if (id <= ACH_TRIAL_BY_SUNSHINE) {
        const int special = id <= ACH_COMPLETIONIST
            ? id - ACH_GRINDER : 3 + id - ACH_GRAND_TOUR;
        desc.name = PackedText::at(kSpecialNames, special);
    } else if (id <= ACH_CHUCKSTER_CHANGE) {
        desc.name = PackedText::at(kRCTimeNames,
                                   id - ACH_SAND_IN_THE_HOURGLASS);
    } else if (id <= ACH_LORD_OF_THE_NUT) {
        desc.name = PackedText::at(
            kStreakNames,
            kLaunchStreakCount + id - ACH_RUN_KILLER_CONQUERED);
    } else if (id <= ACH_HIGH_KING) {
        desc.name = PackedText::at(kRC2TimeNames,
                                   id - ACH_WIGGLER_WRESTLING);
    } else {
        desc.name = PackedText::at(kRC2SpecialNames, id - ACH_GHASTLY);
    }
    desc.description = makeDescription(id, description);
    return &desc;
}

const char *categoryName(Category category) {
    static const char names[] =
        "Times\0Challenges\0Course Mastery\0Streaks\0Special";
    static const u8 offsets[] = {0, 6, 17, 32, 40};
    return category < CATEGORY_COUNT ? names + offsets[category] : "";
}

const char *tierName(Tier tier) {
    static const char names[] =
        "Bronze\0Silver\0Gold\0Diamond\0Demon\0Frontier";
    static const u8 offsets[] = {0, 7, 14, 19, 27, 33};
    return tier < TIER_COUNT ? names + offsets[tier] : "";
}

int categoryAchievementCount(Category category) {
    return category < CATEGORY_COUNT ? kCategoryCounts[category] : 0;
}

int categoryTierAchievementCount(Category category, Tier tier) {
    return category < CATEGORY_COUNT && tier < TIER_COUNT
        ? kTierCounts[category][tier] : 0;
}

AchievementId categoryTierAchievement(Category category, Tier tier,
                                      int index) {
    if (category >= CATEGORY_COUNT || tier >= TIER_COUNT || index < 0)
        return ACHIEVEMENT_INVALID;
    if (index >= kTierCounts[category][tier]) return ACHIEVEMENT_INVALID;
    const int rc1Table = rcTierTable(category);
    const int rc2Table = rc2TierTable(category);
    const u8 rc1Count = rc1Table >= 0 ? kRCTierCounts[rc1Table][tier] : 0;
    const u8 rc2Count = rc2Table >= 0 ? kRC2TierCounts[rc2Table][tier] : 0;
    const u8 launchCount = kTierCounts[category][tier] - rc1Count - rc2Count;
    if (index < launchCount)
        return (AchievementId)(kLaunchTierFirst[category][tier] + index);
    index -= launchCount;
    if (index < rc1Count)
        return (AchievementId)(kRCTierFirst[rc1Table][tier] + index);
    return (AchievementId)(kRC2TierFirst[rc2Table][tier] + index - rc1Count);
}

bool unlocked(AchievementId id) { return bitUnlocked(id); }

bool popUnlock(AchievementId *id) {
    if (!id || sUnlockCount == 0) return false;
    *id = (AchievementId)sUnlockQueue[sUnlockFirst];
    sUnlockFirst = (u8)((sUnlockFirst + 1) % kUnlockQueueSize);
    sUnlockCount--;
    return true;
}

u16 streakProgress(AchievementId id) {
    const int index = streakRuleIndex(id);
    return index >= 0
        ? ((u16)sStreakProgress[index] << 8) | kStreakRules[index].goal
        : 0;
}

void resetProgress() {
    memset(sAchievements, 0, sizeof(sAchievements));
    memset(sStats, 0, sizeof(sStats));
    memset(sGlobalStats, 0, sizeof(sGlobalStats));
    memset(sCategoryUnlocks, 0, sizeof(sState->categoryUnlocks));
    sUnlockedCount = 0;
    resetAllStreaks();
    memset(sAreaFrames, 0, sizeof(sState->areaFrames));
    sUnlockFirst = 0;
    sUnlockCount = 0;
    sNonFrontierUnlocks = 0;
    sPlayFrames = 0;
    sCreationFrames = 0;
    sPBNotifyPending = false;
    sAttemptActive = false;
    sAttemptEligible = false;
    sStageEligible = false;
    sAttemptEntry = -1;
    sILAttemptAwaitingSerial = false;
    resetAttemptActions();
    resetRuntimeObservers();
    sDirty = true;
    sAchievementDirty = true;
}

bool dirty() { return sDirty; }

bool achievementDirty() { return sAchievementDirty; }

void clearDirty() {
    sDirty = false;
    sAchievementDirty = false;
}

}  // namespace Records
