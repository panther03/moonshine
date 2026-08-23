#include "susamune/split_stats.hxx"

#include <Dolphin/mem.h>
#include <Dolphin/printf.h>
#include <Dolphin/string.h>

#include "susamune/creation.hxx"
#include "susamune/iling.hxx"
#include "susamune/mem2_map.h"
#include "susamune/menu.hxx"
#include "susamune/qft_display.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/settings.hxx"
#include "susamune/stage_loader.hxx"
#include "susamune/susamune_cfg.h"

namespace {

const u32 kCheckpointFrames = 30u * 60u;
const u32 kRetryFrames = 30u * 10u;
const u32 kSaveTimeoutFrames = 30u * 15u;
const u8 kFreezeFrames[] = {0, 15, 30, 60, 90, 150};

#if defined(SUSAMUNE_VERSION_JP)
const u8 kRegion = SUSAMUNE_PROGRESS_REGION_JP;
#elif defined(SUSAMUNE_VERSION_US)
const u8 kRegion = SUSAMUNE_PROGRESS_REGION_US;
#else
const u8 kRegion = SUSAMUNE_PROGRESS_REGION_PAL;
#endif

enum RuntimeFlag {
    FLAG_PERSISTENT       = 1 << 0,
    FLAG_WRITABLE         = 1 << 1,
    FLAG_PENDING          = 1 << 2,
    FLAG_DIRTY            = 1 << 3,
    FLAG_URGENT           = 1 << 4,
    FLAG_ATTEMPT_ACTIVE   = 1 << 5,
    FLAG_ATTEMPT_ELIGIBLE = 1 << 6,
    FLAG_TIME_ACTIVE      = 1 << 7,
};

enum OverlayColor {
    OVERLAY_RED,
    OVERLAY_WHITE,
    OVERLAY_GREEN,
    OVERLAY_GOLD,
};

struct RouteDesc {
    u16 firstSegment;
    u8 entry;
    u8 checkpointCount;
};

const RouteDesc kRoutes[SplitStats::ROUTE_COUNT] = {
    {0, 5, 3},       {4, 121, 2},     {7, 85, 4},
    {12, 13, 3},     {16, 14, 4},     {21, 16, 2},
    {24, 17, 3},     {28, 20, 3},     {32, 21, 3},
    {36, 22, 1},     {38, 90, 0},     {39, 110, 3},
    {43, 111, 1},    {45, 1, 5},      {51, 2, 3},
    {55, 3, 1},      {57, 112, 4},    {62, 6, 4},
    {67, 7, 4},      {72, 8, 2},      {75, 10, 1},
    {77, 113, 4},    {82, 34, 1},     {84, 78, 3},
    {88, 79, 1},     {90, 80, 2},     {93, 81, 2},
    {96, 82, 5},     {102, 83, 3},    {106, 86, 1},
    {108, 115, 1},   {110, 38, 0},    {111, 39, 2},
    {114, 40, 1},    {116, 42, 3},    {120, 43, 2},
    {123, 46, 3},    {127, 47, 1},    {129, 49, 1},
    {131, 52, 2},    {134, 53, 4},    {139, 54, 2},
    {142, 56, 2},    {145, 57, 5},    {151, 58, 2},
    {154, 60, 5},    {160, 61, 2},    {163, 62, 2},
    {166, 65, 4},    {171, 66, 3},    {175, 67, 2},
    {178, 68, 4},    {183, 69, 3},    {187, 70, 1},
    {189, 71, 4},    {194, 72, 2},    {197, 74, 1},
    {199, 92, 3},    {203, 93, 4},    {208, 15, 3},
    {212, 18, 1},    {214, 0, 0},     {215, 4, 0},
    {216, 9, 0},     {217, 11, 0},    {218, 12, 0},
    {219, 19, 0},    {220, 23, 0},    {221, 24, 0},
    {222, 25, 0},    {223, 26, 0},    {224, 27, 0},
    {225, 28, 0},    {226, 29, 0},    {227, 30, 0},
    {228, 31, 0},    {229, 32, 0},    {230, 33, 0},
    {231, 35, 0},    {232, 36, 0},    {233, 37, 0},
    {234, 41, 0},    {235, 44, 0},    {236, 45, 0},
    {237, 48, 0},    {238, 50, 0},    {239, 51, 0},
    {240, 55, 0},    {241, 59, 0},    {242, 63, 0},
    {243, 64, 0},    {244, 73, 0},    {245, 75, 0},
    {246, 76, 0},    {247, 77, 0},    {248, 84, 0},
    {249, 87, 0},    {250, 88, 0},    {251, 89, 0},
    {252, 91, 0},    {253, 94, 0},    {254, 95, 0},
    {255, 96, 0},    {256, 97, 0},    {257, 98, 0},
    {258, 99, 0},    {259, 100, 0},   {260, 101, 0},
    {261, 102, 0},   {262, 103, 0},   {263, 104, 0},
    {264, 105, 0},   {265, 106, 0},   {266, 107, 0},
    {267, 108, 0},   {268, 109, 0},   {269, 114, 0},
    {270, 116, 0},   {271, 117, 0},   {272, 118, 0},
    {273, 119, 0},   {274, 120, 0},
};

struct Runtime {
    SusamuneSplitStatsPayload payload;
    u32 attemptQf[6];
    u32 attemptPlayedQf;
    s32 lastPlayedClockQf;
    u32 lastAttemptSerial;
    u32 saveSeq;
    u32 waitFrames;
    u32 saveDelay;
    u32 lastError;
    s32 lastSplitQf;
    s32 overlayAnchorQf;
    char overlayText[20];
    u16 overlayFrames;
    u8 candidateGoldMask;
    u8 activeRoute;
    u8 activeProfile;
    u8 lastCountedRoute;
    u8 expectedEvent;
    u8 flags;
    u8 overlayColor;
};

Runtime sStateStorage;
Runtime *const sState = &sStateStorage;
static_assert(sizeof(Runtime) == 0x6E90,
              "split runtime layout drifted");
static_assert(SplitStats::ROUTE_COUNT == SUSAMUNE_SPLIT_STATS_ROUTE_COUNT,
              "split route schema drifted");
static_assert(sizeof(RouteDesc) == 4, "split route descriptor drifted");
static_assert(274 + 1 == SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT,
              "split segment ranges no longer tile the schema");
static_assert(sizeof(kFreezeFrames) == 6,
              "split overlay duration choices changed");

u32 saturatedIncrement(u32 value) {
    return value == 0xffffffffu ? value : value + 1;
}

u32 saturatedAdd(u32 value, u32 addend) {
    return value > 0xffffffffu - addend ? 0xffffffffu : value + addend;
}

bool qfValid(u32 qf) {
    return qf == SUSAMUNE_SPLIT_STATS_QF_UNSET ||
           qf <= (u32)SUSAMUNE_ILING_PB_MAX_QF;
}

int routeForEntry(int entry) {
    for (u8 route = 0; route < SplitStats::ROUTE_COUNT; route++)
        if (kRoutes[route].entry == entry) return route;
    return -1;
}

int routeForAttempt(int entry) {
    if (entry == 25 && StageLoader::activeRouteMatches(25, 121))
        return SplitStats::ROUTE_GELATO_8_GBS;
    if (entry == 0 && StageLoader::activeRouteMatches(0, 1))
        return SplitStats::ROUTE_BIANCO_2;
    return routeForEntry(entry);
}

int routeForResult(int entry) {
    if (sState->activeRoute == SplitStats::ROUTE_GELATO_8_GBS &&
        entry == 121) {
        return SplitStats::ROUTE_GELATO_8_GBS;
    }
    if (sState->activeRoute == SplitStats::ROUTE_BIANCO_2 && entry == 0)
        return SplitStats::ROUTE_BIANCO_2;
    return routeForEntry(entry);
}

u8 segmentCount(const RouteDesc &route) {
    return route.checkpointCount + 1;
}

bool payloadValid(const SusamuneSplitStatsPayload &payload) {
    const SusamuneSplitRouteStats *routeStats = &payload.routeStats[0][0];
    for (u16 i = 0; i < SUSAMUNE_SPLIT_STATS_REGION_COUNT *
                             SUSAMUNE_SPLIT_STATS_ROUTE_COUNT; i++) {
        if (routeStats[i].finishes > routeStats[i].attempts) return false;
    }

    const unsigned int *qfs = &payload.bestQf[0][0];
    const u16 qfCount = (sizeof(payload.bestQf) +
                         sizeof(payload.pbIdentityQf) +
                         sizeof(payload.pbQf)) / sizeof(*qfs);
    for (u16 i = 0; i < qfCount; i++) {
        if (!qfValid(qfs[i])) return false;
    }

    return true;
}

void resetPayload() {
    memset(&sState->payload.routeStats, 0, sizeof(sState->payload.routeStats));
    memset(&sState->payload.playedQf, 0, sizeof(sState->payload.playedQf));
    memset(&sState->payload.bestQf, 0xff,
           sizeof(sState->payload.bestQf) +
               sizeof(sState->payload.pbIdentityQf) +
               sizeof(sState->payload.pbQf));
}

void clearOverlay() {
    sState->overlayFrames = 0;
    sState->overlayAnchorQf = -1;
    sState->overlayText[0] = '\0';
}

void clearAttemptSamples(bool overlay = true) {
    memset(sState->attemptQf, 0xff, sizeof(sState->attemptQf));
    sState->lastSplitQf = 0;
    sState->expectedEvent = 0;
    sState->candidateGoldMask = 0;
    if (overlay) clearOverlay();
}

void markDirty(bool urgent) {
    if (!(sState->flags & FLAG_DIRTY)) sState->saveDelay = kCheckpointFrames;
    sState->flags |= FLAG_DIRTY;
    if (urgent) {
        sState->flags |= FLAG_URGENT;
        sState->saveDelay = 0;
    }
}

void beginAttemptTime() {
    sState->attemptPlayedQf = 0;
    sState->lastPlayedClockQf = 0;
    s32 qf;
    if (gQFTTimer.currentQf(&qf) && qf >= 0)
        sState->lastPlayedClockQf = qf;
    sState->flags |= FLAG_TIME_ACTIVE;
}

void sampleAttemptTime(s32 exactQf = -1) {
    if (!(sState->flags & FLAG_TIME_ACTIVE)) return;
    s32 qf = exactQf;
    if (qf < 0 && !gQFTTimer.currentQf(&qf)) return;
    if (qf < sState->lastPlayedClockQf) return;
    sState->attemptPlayedQf = saturatedAdd(
        sState->attemptPlayedQf,
        (u32)(qf - sState->lastPlayedClockQf));
    sState->lastPlayedClockQf = qf;
}

void commitAttemptTime(s32 exactQf = -1) {
    if (!(sState->flags & FLAG_TIME_ACTIVE) ||
        sState->activeRoute >= SplitStats::ROUTE_COUNT) {
        sState->flags &= ~FLAG_TIME_ACTIVE;
        return;
    }
    sampleAttemptTime(exactQf);
    unsigned int &played =
        sState->payload.playedQf[kRegion][sState->activeRoute];
    played = (unsigned int)saturatedAdd((u32)played,
                                       sState->attemptPlayedQf);
    sState->attemptPlayedQf = 0;
    sState->flags &= ~FLAG_TIME_ACTIVE;
    markDirty(false);
}

u32 pendingAttemptTime(u8 route) {
    if (!(sState->flags & FLAG_TIME_ACTIVE) ||
        !(sState->flags & FLAG_ATTEMPT_ACTIVE) ||
        sState->activeRoute != route) {
        return 0;
    }
    u32 pending = sState->attemptPlayedQf;
    s32 qf;
    if (gQFTTimer.currentQf(&qf) && qf >= sState->lastPlayedClockQf)
        pending = saturatedAdd(
            pending, (u32)(qf - sState->lastPlayedClockQf));
    return pending;
}

void commitAttemptGolds() {
    if (!(sState->flags & FLAG_ATTEMPT_ACTIVE) ||
        !(sState->flags & FLAG_ATTEMPT_ELIGIBLE) ||
        sState->candidateGoldMask == 0 ||
        sState->activeRoute >= SplitStats::ROUTE_COUNT) {
        sState->candidateGoldMask = 0;
        return;
    }

    SusamuneSplitRouteStats &stats =
        sState->payload.routeStats[kRegion][sState->activeRoute];
    const RouteDesc &route = kRoutes[sState->activeRoute];
    for (u8 local = 0; local < segmentCount(route); local++) {
        if (!(sState->candidateGoldMask & (1u << local))) continue;
        const u16 segment = route.firstSegment + local;
        sState->payload.bestQf[kRegion][segment] = sState->attemptQf[local];
        stats.golds = saturatedIncrement(stats.golds);
    }
    sState->candidateGoldMask = 0;
    markDirty(true);
}

void endAttempt() {
    commitAttemptTime();
    commitAttemptGolds();
    sState->flags &= ~(FLAG_ATTEMPT_ACTIVE | FLAG_ATTEMPT_ELIGIBLE |
                       FLAG_TIME_ACTIVE);
    sState->activeRoute = 0xff;
    clearAttemptSamples(false);
    if (sState->flags & FLAG_DIRTY) markDirty(true);
}

void formatDelta(s32 deltaQf, char *out, u32 size) {
    if (deltaQf == 0) {
        snprintf(out, size, "0.000");
        return;
    }
    const char sign = deltaQf < 0 ? '-' : '+';
    const u32 qf = deltaQf < 0 ? (u32)-deltaQf : (u32)deltaQf;
    const u32 millis = (qf * 1001u) / 120u;
    if (millis < 60000u) {
        snprintf(out, size, "%c%lu.%03lu", sign, millis / 1000u,
                 millis % 1000u);
    } else {
        snprintf(out, size, "%c%lu:%02lu.%03lu", sign, millis / 60000u,
                 (millis / 1000u) % 60u, millis % 1000u);
    }
}

void armOverlay(OverlayColor color, bool havePb, s32 deltaQf,
                s32 absoluteQf) {
    u8 choice = gSettings.get(SETTING_TIMER_FREEZE_DURATION);
    if (choice >= sizeof(kFreezeFrames)) choice = 0;
    const u8 frames = kFreezeFrames[choice];
    if (frames == 0) {
        clearOverlay();
        return;
    }
    if (havePb)
        formatDelta(deltaQf, sState->overlayText,
                    sizeof(sState->overlayText));
    else
        snprintf(sState->overlayText, sizeof(sState->overlayText), "--");
    sState->overlayColor = (u8)color;
    sState->overlayAnchorQf = absoluteQf;
    sState->overlayFrames = frames;
}

void formatAnchorQf(s32 qf, char *out, u32 size) {
    const s32 millis = (qf * 1001) / 120;
    const int minutes = (int)(millis / 60000);
    const int seconds = (int)((millis / 1000) % 60);
    const int remainder = (int)(millis % 1000);
    if (gQftDisplay.leadingZero() && minutes < 10) {
        snprintf(out, size, "0%d:%02d.%03d", minutes, seconds, remainder);
    } else {
        snprintf(out, size, "%d:%02d.%03d", minutes, seconds, remainder);
    }
}

bool captureSegment(u16 routeId, u8 local, s32 absoluteQf) {
    if (routeId >= SplitStats::ROUTE_COUNT ||
        !(sState->flags & FLAG_ATTEMPT_ACTIVE) ||
        sState->activeRoute != routeId || local != sState->expectedEvent ||
        local >= segmentCount(kRoutes[routeId]) || absoluteQf < 0 ||
        absoluteQf > SUSAMUNE_ILING_PB_MAX_QF ||
        absoluteQf < sState->lastSplitQf) {
        return false;
    }

    const u16 segment = kRoutes[routeId].firstSegment + local;
    const u32 duration = (u32)(absoluteQf - sState->lastSplitQf);
    sState->attemptQf[local] = duration;
    sState->lastSplitQf = absoluteQf;
    sState->expectedEvent++;

    OverlayColor color = OVERLAY_WHITE;
    const u32 best = sState->payload.bestQf[kRegion][segment];
    if ((sState->flags & FLAG_ATTEMPT_ELIGIBLE) &&
        (best == SUSAMUNE_SPLIT_STATS_QF_UNSET || duration < best)) {
        sState->candidateGoldMask |= (u8)(1u << local);
        color = OVERLAY_GOLD;
    }

    bool havePb = sState->activeProfile <
                          SUSAMUNE_SPLIT_STATS_PROFILE_COUNT &&
                      sState->activeProfile == ILing::pbProfile();
    s32 delta = 0;
    u32 pbElapsed = 0;
    for (u8 prior = 0; havePb && prior <= local; prior++) {
        const u32 pb = sState->payload.pbQf[kRegion][sState->activeProfile]
                                               [kRoutes[routeId].firstSegment +
                                                prior];
        if (pb == SUSAMUNE_SPLIT_STATS_QF_UNSET) {
            havePb = false;
        } else {
            pbElapsed += pb;
        }
    }
    if (havePb) {
        delta = absoluteQf - (s32)pbElapsed;
        if (color != OVERLAY_GOLD) {
            color = delta < 0 ? OVERLAY_GREEN
                              : delta > 0 ? OVERLAY_RED : OVERLAY_WHITE;
        }
    }
    armOverlay(color, havePb, delta, absoluteQf);
    return true;
}

bool validMailbox(const volatile SusamuneSplitStatsCfg *stats) {
    const volatile unsigned int *reserved =
        reinterpret_cast<const volatile unsigned int *>(stats->reserved);
    const volatile unsigned int *tail =
        reinterpret_cast<const volatile unsigned int *>(stats->tailPad);
    return stats->magic == SUSAMUNE_SPLIT_STATS_MAGIC &&
           stats->version == SUSAMUNE_SPLIT_STATS_VERSION &&
           stats->routeCount == SUSAMUNE_SPLIT_STATS_ROUTE_COUNT &&
           stats->regionCount == SUSAMUNE_SPLIT_STATS_REGION_COUNT &&
           stats->segmentCount == SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT &&
           stats->profileCount == SUSAMUNE_SPLIT_STATS_PROFILE_COUNT &&
           stats->headerReserved == 0 &&
           stats->payloadBytes == sizeof(stats->payload) &&
           stats->schemaHash == SUSAMUNE_SPLIT_STATS_SCHEMA_HASH &&
           reserved[0] == 0 && reserved[1] == 0 &&
           reserved[2] == 0 && reserved[3] == 0 && tail[0] == 0 &&
           payloadValid(*(const SusamuneSplitStatsPayload *)&stats->payload);
}

#if !IS_EMULATOR
void beginSave() {
    volatile SusamuneSplitStatsCfg *stats = SUSAMUNE_SPLIT_STATS_PPC_PTR;
    stats->magic = SUSAMUNE_SPLIT_STATS_MAGIC;
    stats->version = SUSAMUNE_SPLIT_STATS_VERSION;
    stats->routeCount = SUSAMUNE_SPLIT_STATS_ROUTE_COUNT;
    stats->regionCount = SUSAMUNE_SPLIT_STATS_REGION_COUNT;
    stats->segmentCount = SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT;
    stats->profileCount = SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
    stats->headerReserved = 0;
    stats->payloadBytes = sizeof(stats->payload);
    stats->schemaHash = SUSAMUNE_SPLIT_STATS_SCHEMA_HASH;
    stats->flags = SUSAMUNE_SPLIT_STATS_FLAG_WRITABLE;
    memcpy((void *)&stats->payload, &sState->payload, sizeof(stats->payload));
    memset((void *)stats->reserved, 0, sizeof(stats->reserved));
    memset((void *)stats->tailPad, 0, sizeof(stats->tailPad));
    DCStoreRange((void *)&stats->payload,
                 sizeof(stats->payload) + sizeof(stats->reserved) +
                     sizeof(stats->tailPad));

    sState->saveSeq++;
    stats->saveSeq = sState->saveSeq;
    DCStoreRange((void *)stats, 32);

    sState->flags |= FLAG_PENDING;
    sState->flags &= ~(FLAG_DIRTY | FLAG_URGENT);
    sState->waitFrames = 0;
    sState->lastError = 0;
}

void pollSave() {
    if (!(sState->flags & FLAG_PENDING)) return;

    volatile SusamuneSplitStatsCfg *stats = SUSAMUNE_SPLIT_STATS_PPC_PTR;
    DCInvalidateRange((void *)&stats->ackSeq, 32);
    if (stats->ackSeq == sState->saveSeq) {
        sState->flags &= ~FLAG_PENDING;
        sState->lastError = stats->status;
        sState->waitFrames = 0;
        if (sState->lastError != 0) {
            markDirty(false);
            sState->saveDelay = kRetryFrames;
        }
    } else if (++sState->waitFrames > kSaveTimeoutFrames) {
        // Keep the immutable request staged; DI traffic can delay the ARM.
        sState->lastError = 0xffffffffu;
    }
}
#endif

}  // namespace

namespace SplitStats {

void init() {
    memset(sState, 0, sizeof(*sState));
    resetPayload();
    clearAttemptSamples();
    sState->activeRoute = 0xff;
    sState->lastCountedRoute = 0xff;
    sState->lastAttemptSerial = gQFTTimer.attemptSerial();

#if !IS_EMULATOR
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    DCInvalidateRange((void *)cfg, 32);
    if (cfg->magic != SUSAMUNE_CFG_MAGIC ||
        cfg->version != SUSAMUNE_CFG_VERSION ||
        !(cfg->flags & SUSAMUNE_CFG_FLAG_SPLIT_STATS)) {
        return;
    }

    volatile SusamuneSplitStatsCfg *stats = SUSAMUNE_SPLIT_STATS_PPC_PTR;
    DCInvalidateRange((void *)stats, sizeof(*stats));
    if (!validMailbox(stats)) return;
    memcpy(&sState->payload, (const void *)&stats->payload,
           sizeof(sState->payload));
    sState->saveSeq = stats->saveSeq;
    sState->flags |= FLAG_PERSISTENT;
    if (stats->flags & SUSAMUNE_SPLIT_STATS_FLAG_WRITABLE)
        sState->flags |= FLAG_WRITABLE;
    if (stats->flags & SUSAMUNE_SPLIT_STATS_FLAG_MIGRATED)
        markDirty(true);
#endif
}

void beginFrame() {
    if (sState->overlayFrames > 0) sState->overlayFrames--;
}

void update() {
    sampleAttemptTime();
#if !IS_EMULATOR
    pollSave();
#endif

#if !IS_EMULATOR
    if (!(sState->flags & FLAG_PERSISTENT) ||
        !(sState->flags & FLAG_WRITABLE) ||
        (sState->flags & FLAG_PENDING) ||
        !(sState->flags & FLAG_DIRTY)) {
        return;
    }
    if (!(sState->flags & FLAG_URGENT) && sState->saveDelay != 0) {
        sState->saveDelay--;
        return;
    }
    beginSave();
#endif
}

void onStageSetup() { clearOverlay(); }

void onILAttemptStarted(int entry, bool eligible) {
    const int route = routeForAttempt(entry);
    const u32 serial = gQFTTimer.attemptSerial();
    const bool duplicate = sState->lastAttemptSerial == serial &&
                           sState->lastCountedRoute == route;
    if (duplicate && (sState->flags & FLAG_ATTEMPT_ACTIVE) &&
        sState->activeRoute == route) {
        if (!eligible) sState->flags &= ~FLAG_ATTEMPT_ELIGIBLE;
        return;
    }
    if (sState->flags & FLAG_ATTEMPT_ACTIVE) {
        commitAttemptTime();
        commitAttemptGolds();
    }
    clearAttemptSamples();
    if (route < 0) {
        sState->flags &= ~(FLAG_ATTEMPT_ACTIVE | FLAG_ATTEMPT_ELIGIBLE |
                           FLAG_TIME_ACTIVE);
        sState->activeRoute = 0xff;
        return;
    }

    sState->activeRoute = (u8)route;
    sState->activeProfile = (u8)ILing::pbProfile();
    sState->flags |= FLAG_ATTEMPT_ACTIVE;
    if (eligible)
        sState->flags |= FLAG_ATTEMPT_ELIGIBLE;
    else
        sState->flags &= ~FLAG_ATTEMPT_ELIGIBLE;
    beginAttemptTime();
    if (!duplicate) {
        SusamuneSplitRouteStats &stats =
            sState->payload.routeStats[kRegion][route];
        stats.attempts = saturatedIncrement(stats.attempts);
        sState->lastAttemptSerial = serial;
        sState->lastCountedRoute = (u8)route;
        markDirty(false);
    }
}

void onILAttemptEnded() { endAttempt(); }

void invalidateAttempt() {
    commitAttemptTime();
    sState->flags &= ~FLAG_ATTEMPT_ELIGIBLE;
    sState->candidateGoldMask = 0;
}

void onILResult(int entry, s32 qf) {
    const int routeIndex = routeForResult(entry);
    if (routeIndex < 0 || qf < 0 || qf > SUSAMUNE_ILING_PB_MAX_QF ||
        !(sState->flags & FLAG_ATTEMPT_ACTIVE) ||
        sState->activeRoute != routeIndex) {
        return;
    }
    const u8 route = (u8)routeIndex;
    const RouteDesc &desc = kRoutes[route];
    commitAttemptTime(qf);
    if (sState->expectedEvent == desc.checkpointCount)
        captureSegment(route, desc.checkpointCount, qf);

    SusamuneSplitRouteStats &stats =
        sState->payload.routeStats[kRegion][route];
    // A missing start hook must not manufacture an attempt or make an invalid
    // finishes>attempts journal. Normal completions always take this branch.
    if (stats.finishes < stats.attempts)
        stats.finishes = saturatedIncrement(stats.finishes);

    commitAttemptGolds();
    if (sState->activeProfile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT &&
        sState->activeProfile == ILing::pbProfile() &&
        (sState->flags & FLAG_ATTEMPT_ELIGIBLE)) {
        const bool complete = sState->expectedEvent == segmentCount(desc);
        unsigned int &identity =
            sState->payload.pbIdentityQf[kRegion][sState->activeProfile]
                                             [route];
        u32 completedPb = (u32)qf;
        if (desc.checkpointCount == 0) {
            const s32 catalogPb = ILing::pbQf(entry);
            if (catalogPb >= 0 && catalogPb <= SUSAMUNE_ILING_PB_MAX_QF)
                completedPb = (u32)catalogPb;
        }
        const bool updatePb = complete &&
            (identity == SUSAMUNE_SPLIT_STATS_QF_UNSET || completedPb < identity);
        if (updatePb) {
            identity = completedPb;
            for (u8 local = 0; local < segmentCount(desc); local++) {
                const u16 segment = desc.firstSegment + local;
                sState->payload.pbQf[kRegion][sState->activeProfile][segment] =
                    desc.checkpointCount == 0 ? completedPb
                                              : sState->attemptQf[local];
            }
        }
    }
    markDirty(true);
}

void onPBDeleted(int entry, int profile) {
    const int routeIndex = routeForEntry(entry);
    if (routeIndex < 0 || profile < 0 ||
        profile >= SUSAMUNE_SPLIT_STATS_PROFILE_COUNT) {
        return;
    }
    const u8 route = (u8)routeIndex;
    const RouteDesc &desc = kRoutes[route];
    sState->payload.pbIdentityQf[kRegion][profile][route] =
        SUSAMUNE_SPLIT_STATS_QF_UNSET;
    for (u8 local = 0; local < segmentCount(desc); local++) {
        sState->payload.pbQf[kRegion][profile][desc.firstSegment + local] =
            SUSAMUNE_SPLIT_STATS_QF_UNSET;
    }
    markDirty(true);
}

void onSavestateLoaded() {
    commitAttemptTime();
    sState->flags &= ~(FLAG_ATTEMPT_ACTIVE | FLAG_ATTEMPT_ELIGIBLE |
                       FLAG_TIME_ACTIVE);
    sState->activeRoute = 0xff;
    sState->lastCountedRoute = 0xff;
    sState->lastAttemptSerial = gQFTTimer.attemptSerial();
    clearAttemptSamples();
}

bool onRouteEvent(u16 routeId, u8 eventId, s32 absoluteQf) {
    if (routeId >= ROUTE_COUNT ||
        eventId >= kRoutes[routeId].checkpointCount) return false;
    return captureSegment(routeId, eventId, absoluteQf);
}

bool routeActive(u16 routeId) {
    return routeId < ROUTE_COUNT &&
           (sState->flags & FLAG_ATTEMPT_ACTIVE) &&
           sState->activeRoute == routeId;
}

bool supportsEntry(int entry) { return routeForEntry(entry) >= 0; }

bool summary(int entry, Summary *out) {
    const int routeIndex = routeForEntry(entry);
    if (routeIndex < 0 || !out) return false;
    const u8 route = (u8)routeIndex;
    const SusamuneSplitRouteStats &stats =
        sState->payload.routeStats[kRegion][route];
    out->attempts = stats.attempts;
    out->finishes = stats.finishes;
    out->golds = stats.golds;
    out->playedQf = saturatedAdd(
        sState->payload.playedQf[kRegion][route], pendingAttemptTime(route));
    const RouteDesc &desc = kRoutes[route];
    out->routeName = ILing::label(kRoutes[route].entry);
    out->segmentCount = segmentCount(desc);
    out->sumBestQf = 0;
    s32 pbSplitQf = 0;
    const int profile = ILing::pbProfile();
    bool havePb = profile >= 0 && profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
    for (u8 local = 0; local < out->segmentCount; local++) {
        const u16 segment = desc.firstSegment + local;
        const u32 best = sState->payload.bestQf[kRegion][segment];
        u32 pb = havePb
                     ? sState->payload.pbQf[kRegion][profile][segment]
                     : SUSAMUNE_SPLIT_STATS_QF_UNSET;
        if (desc.checkpointCount == 0 &&
            pb == SUSAMUNE_SPLIT_STATS_QF_UNSET) {
            const s32 catalogPb = ILing::pbQf(entry);
            if (catalogPb >= 0 && catalogPb <= SUSAMUNE_ILING_PB_MAX_QF)
                pb = (u32)catalogPb;
        }
        Summary::Segment &item = out->segments[local];
        item.goldQf = best == SUSAMUNE_SPLIT_STATS_QF_UNSET ? -1 : (s32)best;
        item.pbSegmentQf = pb == SUSAMUNE_SPLIT_STATS_QF_UNSET ? -1 : (s32)pb;
        if (item.pbSegmentQf < 0) havePb = false;
        if (havePb) pbSplitQf += item.pbSegmentQf;
        item.pbSplitQf = havePb ? pbSplitQf : -1;
        if (best == SUSAMUNE_SPLIT_STATS_QF_UNSET) {
            out->sumBestQf = -1;
        } else if (out->sumBestQf >= 0) {
            out->sumBestQf += (s32)best;
        }
    }
    return true;
}

DeleteGoldResult deleteGold(int entry, u8 localSegment) {
    const int routeIndex = routeForEntry(entry);
    if (routeIndex < 0) return DELETE_GOLD_INVALID;
    if ((sState->flags & FLAG_PERSISTENT) &&
        !(sState->flags & FLAG_WRITABLE)) {
        return DELETE_GOLD_READ_ONLY;
    }

    const u8 route = (u8)routeIndex;
    const RouteDesc &desc = kRoutes[route];
    if (localSegment >= segmentCount(desc)) return DELETE_GOLD_INVALID;
    const u16 segment = desc.firstSegment + localSegment;
    if (sState->payload.bestQf[kRegion][segment] ==
        SUSAMUNE_SPLIT_STATS_QF_UNSET) {
        return DELETE_GOLD_NONE;
    }

    sState->payload.bestQf[kRegion][segment] =
        SUSAMUNE_SPLIT_STATS_QF_UNSET;
    if ((sState->flags & FLAG_ATTEMPT_ACTIVE) &&
        sState->activeRoute == route) {
        sState->candidateGoldMask &= (u8)~(1u << localSegment);
    }
    markDirty(true);
    return DELETE_GOLD_OK;
}

StorageState storageState() {
    if (!(sState->flags & FLAG_PERSISTENT)) return STORAGE_SESSION;
    if (!(sState->flags & FLAG_WRITABLE)) return STORAGE_READ_ONLY;
    if (sState->flags & FLAG_PENDING) return STORAGE_SAVING;
    return sState->lastError ? STORAGE_FAILED : STORAGE_SD;
}

#if defined(SUSAMUNE_VERSION_JP)
u8 overlayBrightness(u8 value, u8 brightness) {
    const int result = (int)value * brightness / 100;
    return (u8)(result > 255 ? 255 : result);
}

void drawJpPositiveDelta(Menu *menu, const CreationStyle &style,
                         const u8 *rgb, const char *text,
                         const char *layoutText) {
    const int size = 20 * (int)style.scale / 100;
    const int pad = style.padding == 0xff ? 0 : style.padding;
    const int width = Creation::textWidth(layoutText, size);
    if (style.padding != 0xff) {
        menu->fillBox((int)style.x - pad, (int)style.y - pad,
                      width + pad * 2, size + pad * 2,
                      JUtility::TColor(style.bgR, style.bgG, style.bgB,
                                       style.bgA));
    }

    const int cell = Menu::textWidth("P", size);
    const int stroke = size / 10 > 0 ? size / 10 : 1;
    const int arm = cell * 2 / 3 > stroke ? cell * 2 / 3 : stroke;
    const int cx = (int)style.x + cell / 2;
    const int cy = (int)style.y + size / 2;
    const JUtility::TColor color(
        overlayBrightness(rgb[0], style.textBrightness),
        overlayBrightness(rgb[1], style.textBrightness),
        overlayBrightness(rgb[2], style.textBrightness), style.textA);
    menu->fillBox(cx - arm / 2, cy - stroke / 2, arm, stroke, color);
    menu->fillBox(cx - stroke / 2, cy - arm / 2, stroke, arm, color);
    menu->drawText(text + 1, (int)style.x + cell, style.y, size, size, color);
}
#endif

void draw(Menu *menu) {
    if (!menu || !gSettings.getBool(SETTING_LEVEL_SPLITS) ||
        sState->overlayFrames == 0 || !sState->overlayText[0] ||
        sState->overlayAnchorQf < 0) {
        return;
    }

    char anchor[20];
    formatAnchorQf(sState->overlayAnchorQf, anchor, sizeof(anchor));
    CreationStyle style;
#if defined(SUSAMUNE_VERSION_JP)
    char layoutText[sizeof(sState->overlayText)];
    const bool customPlus = sState->overlayText[0] == '+';
    const char *deltaLayout = sState->overlayText;
    if (customPlus) {
        strncpy(layoutText, sState->overlayText, sizeof(layoutText));
        layoutText[sizeof(layoutText) - 1] = '\0';
        // JP maps Shift-JIS plus onto the controller X icon. Reserve one
        // ordinary glyph cell, then draw the sign geometrically below.
        layoutText[0] = 'P';
        deltaLayout = layoutText;
    }
    if (!gQftDisplay.adjacentStyle(anchor, deltaLayout, &style)) {
#else
    if (!gQftDisplay.adjacentStyle(anchor, sState->overlayText, &style)) {
#endif
        if (gQftDisplay.hasAnchor(anchor)) return;
        // Coordinate/boss checkpoints have no native QFT freezer. Render the
        // captured clock in the user's compact style for the same lifetime.
        gQftDisplay.draw(menu, anchor);
#if defined(SUSAMUNE_VERSION_JP)
        if (!gQftDisplay.adjacentStyle(anchor, deltaLayout, &style)) return;
#else
        if (!gQftDisplay.adjacentStyle(anchor, sState->overlayText, &style))
            return;
#endif
    }
    static const u8 kColors[][3] = {
        {245, 95, 85},
        {245, 248, 255},
        {120, 220, 150},
        {255, 196, 40},
    };
    const u8 color = sState->overlayColor < 4 ? sState->overlayColor : 1;
#if defined(SUSAMUNE_VERSION_JP)
    if (customPlus) {
        drawJpPositiveDelta(menu, style, kColors[color], sState->overlayText,
                            deltaLayout);
        return;
    }
#endif
    Creation::drawTextBox(menu, style, &kColors[color], 1,
                          sState->overlayText, true);
}

}  // namespace SplitStats
