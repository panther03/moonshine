#ifndef _SUSAMUNE_RECORDS_HXX
#define _SUSAMUNE_RECORDS_HXX

#include <Dolphin/types.h>

#include "susamune/assist.hxx"

namespace Records {

enum {
    ACHIEVEMENT_CAPACITY = 512,
    ACHIEVEMENT_BYTES    = ACHIEVEMENT_CAPACITY / 8,
    STAT_CAPACITY        = 64,
};

// Persistent stat IDs. Append only: these values index the progress journal.
enum StatId : u8 {
    STAT_PLAY_SECONDS,
    STAT_ATTEMPTS,
    STAT_IL_FINISHES,
    STAT_PBS_EARNED,
    STAT_DEATHS,
    STAT_CREATION_SECONDS,
    STAT_AREA_AIRSTRIP_SECONDS,
    STAT_AREA_DELFINO_SECONDS,
    STAT_AREA_BIANCO_SECONDS,
    STAT_AREA_RICCO_SECONDS,
    STAT_AREA_GELATO_SECONDS,
    STAT_AREA_PINNA_SECONDS,
    STAT_AREA_SIRENA_SECONDS,
    STAT_AREA_NOKI_SECONDS,
    STAT_AREA_PIANTA_SECONDS,
    STAT_AREA_CORONA_SECONDS,
    STAT_WORLD_BIANCO_ATTEMPTS,
    STAT_WORLD_RICCO_ATTEMPTS,
    STAT_WORLD_GELATO_ATTEMPTS,
    STAT_WORLD_PINNA_ATTEMPTS,
    STAT_WORLD_SIRENA_ATTEMPTS,
    STAT_WORLD_NOKI_ATTEMPTS,
    STAT_WORLD_PIANTA_ATTEMPTS,
    STAT_WORLD_DELFINO_ATTEMPTS,
    STAT_WORLD_BIANCO_FINISHES,
    STAT_WORLD_RICCO_FINISHES,
    STAT_WORLD_GELATO_FINISHES,
    STAT_WORLD_PINNA_FINISHES,
    STAT_WORLD_SIRENA_FINISHES,
    STAT_WORLD_NOKI_FINISHES,
    STAT_WORLD_PIANTA_FINISHES,
    STAT_WORLD_DELFINO_FINISHES,
    STAT_GHOSTS_SAVED,
    STAT_GHOST_TIME_SAVED_QF,
    STAT_COUNT,
};

enum Area : u8 {
    AREA_AIRSTRIP,
    AREA_DELFINO,
    AREA_BIANCO,
    AREA_RICCO,
    AREA_GELATO,
    AREA_PINNA,
    AREA_SIRENA,
    AREA_NOKI,
    AREA_PIANTA,
    AREA_CORONA,
    AREA_COUNT,
    AREA_INVALID = 0xff,
};

enum World : u8 {
    WORLD_BIANCO,
    WORLD_RICCO,
    WORLD_GELATO,
    WORLD_PINNA,
    WORLD_SIRENA,
    WORLD_NOKI,
    WORLD_PIANTA,
    WORLD_DELFINO,
    WORLD_COUNT,
    WORLD_INVALID = 0xff,
};

enum Tier : u8 {
    TIER_BRONZE,
    TIER_SILVER,
    TIER_GOLD,
    TIER_DIAMOND,
    TIER_DEMON,
    TIER_FRONTIER,
    TIER_COUNT,
};

enum Category : u8 {
    CATEGORY_TIMES,
    CATEGORY_CHALLENGES,
    CATEGORY_COURSE_MASTERY,
    CATEGORY_STREAKS,
    CATEGORY_SPECIAL,
    CATEGORY_COUNT,
};

enum GhostRaceSource : u8 {
    GHOST_RACE_NONE,
    GHOST_RACE_PERSONAL,
    GHOST_RACE_IMPORTED,
};

// IDs 0-13 belonged to the Pre-Release 1/2 test catalog. They are retired
// forever so an old test bit can never unlock a real achievement.
enum AchievementId : u16 {
    ACH_RETIRED_FIRST = 0,
    ACH_RETIRED_LAST  = 13,

    ACH_HERO_OF_THE_VILLAGE = 14,
    ACH_SCROOGE,
    ACH_SURFIN_DELFINO,
    ACH_SNIPER,
    ACH_EARLY_CYCLE,
    ACH_SPRING_CLIPPER,
    ACH_YOSHI_EXPERT,
    ACH_WAKING_SNOOZAKOOPAS,
    ACH_FLUFF_FREIGHTER,
    ACH_BOTTLE_DIVER,
    ACH_GREEN_DEMON,
    ACH_EXPERT_SPIDER_BOUNCER,
    ACH_COCONUT_KING,
    ACH_PROFESSIONAL_DIVER,
    ACH_ROCKET_ENGINEER,
    ACH_ROPE_DANCER,
    ACH_BOWSER_BOMBARDIER,
    ACH_THE_CLASSIC,
    ACH_MURDERER,
    ACH_SPEEDY_COINS,
    ACH_ECUADORIAN_ISLANDS,
    ACH_BOO_BOUNCER,
    ACH_BEACH_CLEANER,
    ACH_BOAT_MASTER,
    ACH_PLUNGELO_PLUCKER,
    ACH_DENTIST,
    ACH_SHELL_SHOCKER,
    ACH_TIEBREAKER,
    ACH_NINJA_WARRIOR,

    ACH_PLATFORM_RIDER,
    ACH_OH_MY_GOD,
    ACH_HOT_TUB,
    ACH_THE_LONG_WAY_UP,
    ACH_EXTERMINATOR,
    ACH_FISH_FINDER,
    ACH_NOHOVER,
    ACH_MUSHROOM_TRESPASSING,
    ACH_MASHATHON,
    // Retired in PR4: the route is not possible on every retail revision.
    // Keep the ID reserved so later persisted achievement bits never move.
    ACH_RETIRED_INDIANA_JONES,

    ACH_BIANCO_BEGINNINGS,
    ACH_RICCO_BEGINNINGS,
    ACH_GELATO_BEGINNINGS,
    ACH_PINNA_BEGINNINGS,
    ACH_SIRENA_BEGINNINGS,
    ACH_NOKI_BEGINNINGS,
    ACH_PIANTA_BEGINNINGS,
    ACH_BIANCO_SPECIALIST,
    ACH_RICCO_SPECIALIST,
    ACH_GELATO_SPECIALIST,
    ACH_PINNA_SPECIALIST,
    ACH_SIRENA_SPECIALIST,
    ACH_NOKI_SPECIALIST,
    ACH_PIANTA_SPECIALIST,
    ACH_BIANCO_GRADUATE,
    ACH_RICCO_GRADUATE,
    ACH_GELATO_GRADUATE,
    ACH_PINNA_GRADUATE,
    ACH_SIRENA_GRADUATE,
    ACH_NOKI_GRADUATE,
    ACH_PIANTA_GRADUATE,
    ACH_BIANCO_MASTER,
    ACH_RICCO_MASTER,
    ACH_GELATO_MASTER,
    ACH_PINNA_MASTER,
    ACH_SIRENA_MASTER,
    ACH_NOKI_MASTER,
    ACH_PIANTA_MASTER,

    ACH_GETTING_THERE,
    ACH_CHUCKSTER_CHATTER,
    ACH_PEGGED,
    ACH_DISTURBED_FAMILY_VACATION,
    ACH_POWERWASH_SIMULATOR,
    ACH_ELECTROKOOPAS_HEARTLESS,
    ACH_CLOCKWORK_CAROUSEL,
    ACH_FIVE_ALARM_FIRE,
    ACH_JONATHAN_THE_TORTOISE,
    ACH_ORAL_SURGEON,
    ACH_DIRTY_WORK,
    ACH_TOWER_DEFENSE,
    ACH_LOADED_DICE,
    ACH_SHELL_GAME,
    ACH_NO_VACANCY,
    ACH_DEEP_END_DIVIDEND,

    ACH_GRINDER,
    ACH_FULL_RUN,
    ACH_COMPLETIONIST,
    ACH_BIANCO_ENTHUSIAST,
    ACH_RICCO_ENTHUSIAST,
    ACH_GELATO_ENTHUSIAST,
    ACH_PINNA_ENTHUSIAST,
    ACH_SIRENA_ENTHUSIAST,
    ACH_NOKI_ENTHUSIAST,
    ACH_PIANTA_ENTHUSIAST,
    ACH_DELFINO_ENTHUSIAST,
    ACH_GRAND_TOUR,
    ACH_BOB_ROSS,
    ACH_SERIOUS_COMMITMENT,
    ACH_DEJA_VU,
    ACH_WELL_ROUNDED,
    ACH_GAME_OVER,
    ACH_ALL_ROUNDER,
    ACH_TOUCH_GRASS,
    ACH_TASBOT,
    ACH_TRIAL_BY_SUNSHINE,

    // RC1 additions are append-only even though their categories are split
    // from the launch catalog.
    ACH_SAND_IN_THE_HOURGLASS,
    ACH_I_JUST_LANDED,
    ACH_RED_TIDE,
    ACH_HILLSIDE_HUSTLE,
    ACH_THEN_ITS_WAR,
    ACH_CASINO_ROYALE,
    ACH_THEY_SEE_ME_ROLLIN_OUT,
    ACH_GHOSTLY_REDS,
    ACH_WHERE_THERE_IS_A_WILL,
    // Retired in V2.2: Dootsters? has the exact same requirement.
    ACH_RETIRED_CHUCKSTER_CHANGE,

    ACH_RUN_KILLER_CONQUERED,
    ACH_FREQUENT_FLYER,
    ACH_SANDCASTLE_SIEGE,
    ACH_HARBOR_HABIT,
    ACH_DOG_TRAINER,
    ACH_CASINO_CIRCUIT,
    ACH_I_DIDNT_HEAR_NO_BELL,
    ACH_MOLE_MANGLER,
    ACH_CLOCKWORK_CRISIS,
    ACH_LORD_OF_THE_NUT,

    // RC2 Times. IDs remain tier-grouped so the Records browser can preserve
    // its stable category/tier order without moving the RC1 catalog.
    ACH_WIGGLER_WRESTLING,
    // Retired in V2.2: Scrooge has the exact same requirement.
    ACH_RETIRED_NO_KIDDING,

    // Retired in V2.2: Red Tide has the exact same requirement.
    ACH_RETIRED_TOWER_TITAN,
    ACH_JUST_FISHIN,
    ACH_VILLAGE_LIFE,
    ACH_THERE_IT_IS,
    ACH_CAPTAIN_MARIO,

    ACH_PLANT_PUNISHER,
    ACH_SWIMMING_WITH_THE_FISHES,
    ACH_WHERE_ARE_THEY,
    ACH_BIRDS_AND_BEES,
    ACH_SQUEAKY_CLEAN,

    ACH_RED_FREAK,
    ACH_MY_HAT,
    ACH_TRAUMATIC_MEMORIES,

    ACH_FLIPPING_INSANE,
    ACH_SCROUNGER,
    ACH_NUTTY,
    ACH_A_BOAT_A_SKIP_AND_A_HOP,
    ACH_MAKE_OR_BREAK,
    ACH_MIGHT_MAKES_RIGHT,
    ACH_DOOTSTERS,
    ACH_THAT_BIRD_THAT_I_HATE,

    ACH_LORD_OF_THE_SANDS,
    ACH_HIGH_KING,

    // RC2 Special achievements, one per tier.
    ACH_GHASTLY,
    ACH_SPECTRAL,
    ACH_SPECULAR,
    ACH_ECTOPLASMIC,
    ACH_WRAITHLIKE,
    ACH_PHANTASMAL,

    // V2.2 Times. Each category is tier-grouped for the Records browser.
    ACH_DOCK_KNOCK,
    ACH_CHAIN_REACTION,
    ACH_SQUID_PRO_QUO,
    ACH_LEAF_ME_HERE,
    ACH_PRAISE_THE_SUN,
    ACH_UNCORKED,
    ACH_GOOPY_BUSINESS,
    ACH_STOP_THIEF,
    ACH_HILLSIDE_HEIST,
    ACH_TRIPLE_TERROR,
    ACH_I_HAVE_THE_HIGH_GROUND,
    ACH_STOP_RIGHT_THERE_CRIMINAL_SCUM,
    ACH_SECURITY,
    ACH_BY_ORDER_OF_THE_ELDER,
    ACH_THE_HOUSE_ALWAYS_WINS,
    ACH_MANTASTIC,
    ACH_BOMBASTIC_BALLOONS,
    ACH_SKIPPED_SUMI,
    ACH_PERFECT_PETER,
    ACH_THE_CULMINATION,

    // V2.2 Challenges.
    ACH_LOST_AND_FLUDD,
    ACH_A_NEW_LEAF,
    ACH_REEF_RUNNER,
    ACH_RUINS_RAMPAGE,
    ACH_NO_SAFETY_NET,
    ACH_ORANGES_QUEST,

    // V2.2 Course Mastery.
    ACH_PLAZA_BEGINNINGS,
    ACH_PLAZA_SPECIALIST,
    ACH_PLAZA_GRADUATE,
    ACH_PLAZA_MASTER,
    ACH_SHADOW_SLAYER,

    // V2.2 Streaks.
    ACH_ROOTED,
    ACH_NO_REFUNDS,
    ACH_SCOOBY_DOOBY_DOO,
    ACH_PIECES_ROUGES_DE_LA_GROTTE,
    ACH_SURFS_UP,
    ACH_ROBBING_THE_VILLAGE,
    ACH_HOTEL_LOYALTY,
    ACH_BRUSH_YOUR_TEETH,
    ACH_ROUND_AND_ROUND,
    ACH_CLEAN_SWEEP,
    ACH_HE_LOVES_SQUIDS,
    ACH_MOTION_SICKNESS,
    ACH_BAYWATCH,
    ACH_THANKS_FOR_MY_GUN,
    ACH_A_CERTAIN_SOMEONE,

    // V2.2 Special.
    ACH_NEW_KID_ON_THE_BLOCK,
    ACH_BY_A_NOSE,
    ACH_MIRROR_MATCH,
    ACH_UPSTART,
    ACH_SERIOUS_BUSINESS,
    ACH_MAGNATE,
    ACH_INDUSTRIALIST,
    ACH_STRANGER_DANGER,
    ACH_DEAD_HEAT,
    ACH_ROBBER_BARON,
    ACH_MOGUL,
    ACH_TYCOON,
    ACH_EMPEROR,

    ACHIEVEMENT_ID_END,
    ACHIEVEMENT_INVALID = 0xffff,
};

enum {
    ACHIEVEMENT_FIRST        = ACH_HERO_OF_THE_VILLAGE,
    ACHIEVEMENT_ACTIVE_COUNT = ACHIEVEMENT_ID_END - ACHIEVEMENT_FIRST - 4,
};

struct AchievementDesc {
    AchievementId id;
    Tier tier;
    Category category;
    const char *name;
    const char *description;
};

void init();

// Persistence supplies the shared achievement bits and this revision's stat
// bank. Imported unlocks do not enter the notification queue.
void adopt(const u8 achievements[ACHIEVEMENT_BYTES],
           const u32 stats[STAT_CAPACITY]);
void stageInto(u8 achievements[ACHIEVEMENT_BYTES],
               u32 stats[STAT_CAPACITY]);
void reconcileRegionalStats(const u32 regional[][STAT_CAPACITY],
                            u8 regionCount);

// Called once per app frame after director->direct().
void update(bool creationEditing, bool observerFrame);
void onStageSetup(u8 area, u8 episode);
void onStageExit();

// `entry` is ILing's stable catalog index. The extended result form lets
// ILing pass the stable PB slot and native countdown result without Records
// reaching into ILing's private catalog.
void onILAttemptStarted(int entry);
void onILAttemptEnded();
void onILResult(int entry, u8 pbSlot, s32 qf, s32 igtCentis,
                bool challengeEligible, GhostRaceSource ghostSource = GHOST_RACE_NONE,
                s32 ghostQf = -1, s32 priorPbQf = -1);
void onPBAccepted(int entry, u8 profile, s32 previousQf, s32 newQf);
// Called only after storage acknowledges the canonical file commit.
void onGhostSaved(u32 durationQf);

// Profiles are laid out consecutively with `slotCount` s32 values each. All
// profiles are checked independently; the active profile supplies menu stats.
void reconcilePBProfiles(const s32 *profiles, u16 slotCount, u8 profileCount,
                         u8 activeProfile);

// Challenge observers are deliberately semantic. Integration should invoke
// them only after validating the corresponding retail event/object identity.
void onYoshiMounted();
void onHotTubSwimming();
void onMarioHeight(float y);
void onCataquacksCleared();
void invalidateAttempt(u8 assistReasons = Assist::OTHER);
void onSavestateLoaded();

u32 stat(StatId id);
StatId areaStat(Area area);
StatId worldAttemptStat(World world);
StatId worldFinishStat(World world);
const char *worldName(World world);
const char *currentRegionScope();
Area classifyArea(u8 gameArea);
World worldForArea(Area area);
World worldForEntry(int entry);
bool worldPBSummary(World world, bool allILs, u8 *coverage, u8 *goal,
                    s32 *sumQf);

int achievementCount();
int unlockedCount();
int categoryUnlockedCount(Category category);
const AchievementDesc *achievement(AchievementId id);
const char *categoryName(Category category);
const char *tierName(Tier tier);
int categoryAchievementCount(Category category);
int categoryTierAchievementCount(Category category, Tier tier);
AchievementId categoryTierAchievement(Category category, Tier tier,
                                      int index);
bool unlocked(AchievementId id);
bool popUnlock(AchievementId *id);
// High byte is current progress; low byte is the goal.
u16 streakProgress(AchievementId id);

// Clear the shared Records state without touching settings or IL PBs.
void resetProgress();

bool dirty();
bool achievementDirty();
void clearDirty();

}  // namespace Records

#endif  // _SUSAMUNE_RECORDS_HXX
