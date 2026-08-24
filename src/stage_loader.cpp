#include "susamune/stage_loader.hxx"

#include "Dolphin/mem.h"
#include "Dolphin/OS.h"
#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "JSystem/JUtility/JUTGamePad.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/binds.hxx"
#include "susamune/addresses.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/ghost.hxx"
#include "susamune/iling.hxx"
#include "susamune/menu.hxx"
#include "susamune/mem2_map.h"
#include "susamune/qft_timer.hxx"
#include "susamune/settings.hxx"
#include "susamune/susamune_cfg.h"
#include "susamune/warp_wheel.hxx"

namespace {

typedef JUtility::TColor Color;

enum SessionState {
    STATE_INACTIVE,
    STATE_REQUESTING,
    STATE_WAITING,
    STATE_RUNNING,
    STATE_RETRY_DELAY,
    STATE_RETRY_PENDING,
    STATE_RETRY_SAVEBOX,
    STATE_WAITING_POST_SAVE,
    STATE_COMPLETE,
    STATE_BLOCKED,
};

enum Outcome {
    OUTCOME_NONE,
    OUTCOME_SUCCESS,
    OUTCOME_RESET,
    OUTCOME_ENDED,
    OUTCOME_WRONG_ROUTE,
    OUTCOME_INELIGIBLE,
    OUTCOME_TARGET_MISS,
    OUTCOME_WARPS_DISABLED,
};

enum ModalState {
    MODAL_NONE,
    MODAL_PENDING,
    MODAL_VISIBLE,
};

enum {
    kRetryDelayFrames = 90,
    kResultDisplayFrames = 240,
    kModalMask = JUTGamePad::A | JUTGamePad::B | JUTGamePad::START,
    kPlaylistSaveTimeoutFrames = 30 * 15,
    kShinePublishLatchFrames = 30,
    kShineDemoLatchFrames = 60 * 5,
    kMarioWinDemoState = 0x1302,
    kShineObjectId = 0x20000013,
    kQueueActionBytes = (StageLoader::QUEUE_CAPACITY + 7) / 8,
};

constexpr u8 kFastAny[] = {
    1, 2, 5, 6, 121, 34, 78, 79, 80, 81, 82, 85, 86, 38, 39, 42, 43,
    46, 49, 13, 14, 16, 17, 20, 21, 22, 7, 10, 52, 53, 56, 57, 60, 61,
    62, 65, 66, 67, 68, 70, 71, 74, 92,
};
constexpr u8 kAllSecrets[] = {3, 8, 18, 26, 40, 47, 54, 58, 72};
constexpr u8 kRedsWorldTour[] = {
    4, 5, 9, 11, 19, 21, 27, 31, 33, 41, 42, 48,
    55, 59, 63, 67, 73, 75, 84, 87, 91, 98, 95, 97,
};
enum {
    kFastAnyPv5Position = 10,
    kInvalidPlaylistId = 0xff,
};

static_assert(sizeof(kFastAny) == 43 && sizeof(kAllSecrets) == 9 &&
                  sizeof(kRedsWorldTour) == 24,
              "built-in playlist contract changed");
static_assert(sizeof(kFastAny) <= StageLoader::QUEUE_CAPACITY &&
                  sizeof(kAllSecrets) <= StageLoader::QUEUE_CAPACITY &&
                  sizeof(kRedsWorldTour) <= StageLoader::QUEUE_CAPACITY,
              "built-in playlist exceeds the queue");

u32 sPlaylistSaveSeq;
u32 sPlaylistWaitFrames;
u32 sPlaylistLastError;
bool sPlaylistsAvailable;
bool sPlaylistSavePending;
enum PlaylistSaveKind {
    PLAYLIST_SAVE_NONE,
    PLAYLIST_SAVE_MAINTENANCE,
    PLAYLIST_SAVE_CUSTOM,
    PLAYLIST_SAVE_BEST,
};
u8 sPlaylistSaveKind;
u8 sPlaylistPendingDraftId;
u32 sPlaylistPendingDraftHash;

#if defined(SUSAMUNE_VERSION_JP)
enum { kPlaylistRegion = 0 };
#elif defined(SUSAMUNE_VERSION_US)
enum { kPlaylistRegion = 1 };
#else
enum { kPlaylistRegion = 2 };
#endif

struct StageLoaderRuntime {
    u64 totalObservedActiveQf;
    u64 completedQfTotal;
    u32 attemptSerial;
    u32 attempts;
    u32 eligibleCompletes;
    u32 qualifyingSuccesses;
    u32 golds;
    u32 lastShineSerial;
    u32 draftPlaylistHash;
    u32 activePlaylistHash;
    u32 priorPlaylistBestQf;
    u32 finalPlaylistQf;
    s32 targetQf;
    s32 lastQf;
    s32 lastObservedQf;
    u16 goal;
    u16 progress;
    u16 currentStreak;
    u16 bestStreak;
    u16 retryFrames;
    u16 displayFrames;
    u16 modalPrevious;
    u16 shinePublishLatchFrames;
    u8 draftCount;
    u8 activeCount;
    u8 activeIndex;
    u8 lastEntry;
    u8 state;
    u8 outcome;
    u8 mode;
    u8 modalState;
    u8 modalReady;
    u8 holdingDeparture;
    u8 shineDemoSeen;
    u8 modalWaitForShineDemo;
    u8 draftPlaylistId;
    u8 activePlaylistId;
    u8 playlistPbEligible;
    u8 playlistTimeOverflow;
    u8 activeActions[kQueueActionBytes];
};

struct StageLoaderQueues {
    u8 draft[StageLoader::QUEUE_CAPACITY];
    u8 active[StageLoader::QUEUE_CAPACITY];
    u8 draftActions[kQueueActionBytes];
    u8 reserved[SUSAMUNE_STAGE_LOADER_QUEUE_SIZE -
                StageLoader::QUEUE_CAPACITY * 2 - kQueueActionBytes];
};

#define sRuntime (*reinterpret_cast<StageLoaderRuntime *>( \
    SUSAMUNE_MEM2_STAGE_LOADER_RUNTIME_PPC_BASE))
#define sQueues (*reinterpret_cast<StageLoaderQueues *>( \
    SUSAMUNE_MEM2_STAGE_LOADER_QUEUE_PPC_BASE))
static_assert(sizeof(StageLoaderRuntime) == 120,
              "Stage Loader runtime layout changed");
static_assert(sizeof(StageLoaderRuntime) <= SUSAMUNE_STAGE_LOADER_RUNTIME_SIZE,
              "Stage Loader runtime exceeds its MEM2 window");
static_assert(sizeof(StageLoaderQueues) == SUSAMUNE_STAGE_LOADER_QUEUE_SIZE,
              "Stage Loader queues must exactly fill their MEM2 window");

bool actionAt(const u8 *bits, int position) {
    return position >= 0 && position < StageLoader::QUEUE_CAPACITY &&
           (bits[position >> 3] & (1u << (position & 7))) != 0;
}

void setAction(u8 *bits, int position, bool enabled) {
    const u8 mask = (u8)(1u << (position & 7));
    if (enabled) bits[position >> 3] |= mask;
    else bits[position >> 3] &= (u8)~mask;
}

u32 playlistHashWord(u32 hash, u32 value) {
    return (hash ^ value) * 16777619u;
}

u32 playlistContentHash(u8 playlistId, u32 revision, u8 count,
                        const u8 *entries, const u8 *actions) {
    u32 hash = 2166136261u;
    hash = playlistHashWord(
        hash, (SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA << 24) |
                  ((u32)playlistId << 16) | count);
    hash = playlistHashWord(hash, revision);
    for (int i = 0; i < StageLoader::QUEUE_CAPACITY; i++) {
        hash = (hash ^ entries[i]) * 16777619u;
        hash = (hash ^ (actionAt(actions, i) ? 1u : 0u)) * 16777619u;
    }
    return hash;
}

bool builtinDefinition(int preset, const u8 **entries, u8 *count) {
    if (!entries || !count) return false;
    switch (preset) {
    case 0:
        *entries = kFastAny;
        *count = sizeof(kFastAny);
        return true;
    case 1:
        *entries = kAllSecrets;
        *count = sizeof(kAllSecrets);
        return true;
    case 2:
        *entries = kRedsWorldTour;
        *count = sizeof(kRedsWorldTour);
        return true;
    default:
        return false;
    }
}

bool builtinActionAt(int preset, int position) {
    return preset == 0 && position == kFastAnyPv5Position;
}

u32 builtinContentHash(int preset) {
    const u8 *entries = nullptr;
    u8 count = 0;
    if (!builtinDefinition(preset, &entries, &count)) return 0;
    u32 hash = 2166136261u;
    hash = playlistHashWord(
        hash, (SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA << 24) |
                  ((u32)preset << 16) | count);
    hash = playlistHashWord(hash, 0);
    for (int i = 0; i < StageLoader::QUEUE_CAPACITY; i++) {
        hash = (hash ^ (i < count ? entries[i] : 0)) * 16777619u;
        hash = (hash ^ (builtinActionAt(preset, i) ? 1u : 0u)) *
               16777619u;
    }
    return hash;
}

void invalidateDraftIdentity() {
    sRuntime.draftPlaylistId = kInvalidPlaylistId;
    sRuntime.draftPlaylistHash = 0;
}

u32 shineSerial() {
    return *reinterpret_cast<volatile u32 *>(
        SUSAMUNE_ADDR_ATTEMPT_SHINE_SERIAL);
}

void clearShinePublishLatch() {
    sRuntime.lastShineSerial = shineSerial();
    sRuntime.shinePublishLatchFrames = 0;
    sRuntime.shineDemoSeen = 0;
}

bool shinePublishPending() {
    if (sRuntime.state != STATE_RUNNING) {
        sRuntime.shinePublishLatchFrames = 0;
        sRuntime.shineDemoSeen = 0;
        return false;
    }
    // WIN_DEMO and its Shine target are published at contact. The director's
    // Shine fields and fireGetStar serial are not published until landing.
    const bool shineDemo =
        gpApplication.mContext == TApplication::CONTEXT_DIRECT_STAGE &&
        gpMarDirector && gpMarDirector->_260 != 0 &&
        gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
        gpMarioOriginal && gpMarioOriginal->mState == kMarioWinDemoState &&
        gpMarioOriginal->mGrabTarget &&
        gpMarioOriginal->mGrabTarget->mObjectID == kShineObjectId;
    if (shineDemo && !sRuntime.shineDemoSeen) {
        sRuntime.shineDemoSeen = 1;
        sRuntime.shinePublishLatchFrames = kShineDemoLatchFrames;
    }
    const u32 serial = shineSerial();
    if (serial != sRuntime.lastShineSerial) {
        sRuntime.lastShineSerial = serial;
        sRuntime.shinePublishLatchFrames = kShinePublishLatchFrames;
    }
    return sRuntime.shinePublishLatchFrames != 0;
}

void resetSession() {
    sRuntime.totalObservedActiveQf = 0;
    sRuntime.completedQfTotal = 0;
    sRuntime.attemptSerial = 0;
    sRuntime.attempts = 0;
    sRuntime.eligibleCompletes = 0;
    sRuntime.qualifyingSuccesses = 0;
    sRuntime.golds = 0;
    sRuntime.activePlaylistHash = 0;
    sRuntime.priorPlaylistBestQf = SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET;
    sRuntime.finalPlaylistQf = SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET;
    sRuntime.targetQf = -1;
    sRuntime.lastQf = -1;
    sRuntime.lastObservedQf = -1;
    sRuntime.goal = 0;
    sRuntime.progress = 0;
    sRuntime.currentStreak = 0;
    sRuntime.bestStreak = 0;
    sRuntime.retryFrames = 0;
    sRuntime.displayFrames = 0;
    sRuntime.modalPrevious = 0;
    memset(sQueues.active, 0, sizeof(sQueues.active));
    sRuntime.activeCount = 0;
    sRuntime.activeIndex = 0;
    sRuntime.lastEntry = 0;
    sRuntime.state = STATE_INACTIVE;
    sRuntime.outcome = OUTCOME_NONE;
    sRuntime.mode = StageLoader::MODE_LOADER;
    sRuntime.modalState = MODAL_NONE;
    sRuntime.modalReady = 0;
    sRuntime.holdingDeparture = 0;
    sRuntime.modalWaitForShineDemo = 0;
    sRuntime.activePlaylistId = kInvalidPlaylistId;
    sRuntime.playlistPbEligible = 0;
    sRuntime.playlistTimeOverflow = 0;
    clearShinePublishLatch();
    memset(sRuntime.activeActions, 0, sizeof(sRuntime.activeActions));
}

void resetAll() {
    memset(&sRuntime, 0, sizeof(sRuntime));
    memset(&sQueues, 0, sizeof(sQueues));
    sRuntime.targetQf = -1;
    sRuntime.lastQf = -1;
    sRuntime.lastObservedQf = -1;
    sRuntime.priorPlaylistBestQf = SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET;
    sRuntime.finalPlaylistQf = SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET;
    invalidateDraftIdentity();
    sRuntime.activePlaylistId = kInvalidPlaylistId;
    clearShinePublishLatch();
}

bool validPlaylistMailbox(
    const volatile SusamuneStagePlaylistsCfg *playlists) {
    return playlists->magic == SUSAMUNE_STAGE_PLAYLIST_MAGIC &&
           playlists->version == SUSAMUNE_STAGE_PLAYLIST_VERSION &&
           playlists->slotCount == SUSAMUNE_STAGE_PLAYLIST_COUNT &&
           playlists->capacity == SUSAMUNE_STAGE_PLAYLIST_CAPACITY &&
           playlists->builtinCount == SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT &&
           playlists->regionCount == SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT &&
           playlists->actionBytes == SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES &&
           playlists->actionSchema == SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA;
}

void storePlaylistPayload(volatile SusamuneStagePlaylistsCfg *playlists) {
    DCStoreRange((void *)playlists->counts,
                 sizeof(*playlists) -
                     __builtin_offsetof(SusamuneStagePlaylistsCfg, counts));
}

bool publishPlaylistSave(PlaylistSaveKind kind) {
#if IS_EMULATOR
    (void)kind;
    return false;
#else
    if (!sPlaylistsAvailable || sPlaylistSavePending) return false;
    volatile SusamuneStagePlaylistsCfg *playlists =
        SUSAMUNE_STAGE_PLAYLIST_PPC_PTR;
    storePlaylistPayload(playlists);
    sPlaylistSaveSeq++;
    playlists->saveSeq = sPlaylistSaveSeq;
    DCStoreRange((void *)playlists, 32);
    sPlaylistSavePending = true;
    sPlaylistWaitFrames = 0;
    sPlaylistLastError = 0;
    sPlaylistSaveKind = (u8)kind;
    return true;
#endif
}

bool reconcileBuiltinHashes(
    volatile SusamuneStagePlaylistsCfg *playlists) {
    bool changed = false;
    for (int preset = 0; preset < StageLoader::BUILTIN_PLAYLIST_COUNT;
         preset++) {
        const u32 hash = builtinContentHash(preset);
        if (playlists->contentHashes[preset] == hash) continue;
        playlists->contentHashes[preset] = hash;
        for (int region = 0;
             region < SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT; region++) {
            playlists->bestQf[region][preset] =
                SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET;
        }
        changed = true;
    }
    return changed;
}

void initPlaylistPersistence() {
    sPlaylistSaveSeq = 0;
    sPlaylistWaitFrames = 0;
    sPlaylistLastError = 0;
    sPlaylistsAvailable = false;
    sPlaylistSavePending = false;
    sPlaylistSaveKind = PLAYLIST_SAVE_NONE;
    sPlaylistPendingDraftId = kInvalidPlaylistId;
    sPlaylistPendingDraftHash = 0;
#if !IS_EMULATOR
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    DCInvalidateRange((void *)cfg, 32);
    if (cfg->magic != SUSAMUNE_CFG_MAGIC ||
        cfg->version != SUSAMUNE_CFG_VERSION ||
        !(cfg->flags & SUSAMUNE_CFG_FLAG_STAGE_PLAYLISTS)) {
        return;
    }

    volatile SusamuneStagePlaylistsCfg *playlists =
        SUSAMUNE_STAGE_PLAYLIST_PPC_PTR;
    DCInvalidateRange((void *)playlists, sizeof(*playlists));
    if (!validPlaylistMailbox(playlists) ||
        !(playlists->flags & SUSAMUNE_STAGE_PLAYLIST_FLAG_WRITABLE)) {
        return;
    }
    sPlaylistSaveSeq = playlists->saveSeq;
    sPlaylistsAvailable = true;
    if (playlists->ackSeq != playlists->saveSeq) {
        // V1 migration already owns the immutable payload until this ACK.
        sPlaylistSavePending = true;
        sPlaylistSaveKind = PLAYLIST_SAVE_MAINTENANCE;
    } else if (reconcileBuiltinHashes(playlists)) {
        publishPlaylistSave(PLAYLIST_SAVE_MAINTENANCE);
    }
#endif
}

void pollPlaylistSave() {
#if !IS_EMULATOR
    if (!sPlaylistSavePending) return;
    volatile SusamuneStagePlaylistsCfg *playlists =
        SUSAMUNE_STAGE_PLAYLIST_PPC_PTR;
    DCInvalidateRange((void *)&playlists->ackSeq, 32);
    if (playlists->ackSeq == sPlaylistSaveSeq) {
        const PlaylistSaveKind kind = (PlaylistSaveKind)sPlaylistSaveKind;
        sPlaylistSavePending = false;
        sPlaylistWaitFrames = 0;
        sPlaylistLastError = playlists->status;
        sPlaylistSaveKind = PLAYLIST_SAVE_NONE;
        if (sPlaylistLastError != 0) {
            // The staged payload is no longer an acknowledged view of disk.
            // Reboot can safely recover the last valid V2 generation.
            sPlaylistsAvailable = false;
            if (kind == PLAYLIST_SAVE_CUSTOM &&
                sRuntime.draftPlaylistId == sPlaylistPendingDraftId &&
                sRuntime.draftPlaylistHash == sPlaylistPendingDraftHash) {
                invalidateDraftIdentity();
            }
        }
        sPlaylistPendingDraftId = kInvalidPlaylistId;
        sPlaylistPendingDraftHash = 0;
        if (sPlaylistLastError == 0 &&
            kind == PLAYLIST_SAVE_MAINTENANCE &&
            reconcileBuiltinHashes(playlists)) {
            publishPlaylistSave(PLAYLIST_SAVE_MAINTENANCE);
        }
        if (gMenu) {
            if (sPlaylistLastError == 0) {
                if (kind == PLAYLIST_SAVE_CUSTOM) {
                    gMenu->toast("Playlist saved");
                } else if (kind == PLAYLIST_SAVE_BEST) {
                    gMenu->toast("Playlist best saved");
                }
            } else {
                char message[40];
                snprintf(message, sizeof(message), "Playlist save failed: %u",
                         (unsigned)sPlaylistLastError);
                gMenu->toast(message);
            }
        }
    } else if (++sPlaylistWaitFrames == kPlaylistSaveTimeoutFrames + 1) {
        sPlaylistLastError = 0xffffffffu;
        if (gMenu) gMenu->toast("Playlist save timed out");
    }
#endif
}

void incrementSaturated(u32 &value) {
    if (value != 0xffffffffu) value++;
}

void addSaturated(u64 &total, u64 amount) {
    const u64 maximum = ~(u64)0;
    total = maximum - total < amount ? maximum : total + amount;
}

int expectedStartEntry() {
    return sRuntime.activeIndex < sRuntime.activeCount
               ? sQueues.active[sRuntime.activeIndex]
               : -1;
}

int expectedResultEntry() {
    const int entry = expectedStartEntry();
    if (!actionAt(sRuntime.activeActions, sRuntime.activeIndex)) return entry;
    if (entry == SUSAMUNE_STAGE_PLAYLIST_ACTION_BIANCO_1) return 1;
    if (entry == SUSAMUNE_STAGE_PLAYLIST_ACTION_GELATO_1) return 121;
    return entry;
}

s32 liveQf() {
    s32 qf = -1;
    return gQFTTimer.currentQf(&qf) && qf >= 0 ? qf : -1;
}

void observeQf(s32 qf) {
    if (qf < 0) return;
    if (sRuntime.lastObservedQf < 0) {
        addSaturated(sRuntime.totalObservedActiveQf, (u32)qf);
    } else if (qf > sRuntime.lastObservedQf) {
        addSaturated(sRuntime.totalObservedActiveQf,
                     (u32)(qf - sRuntime.lastObservedQf));
    }
    if (qf > sRuntime.lastObservedQf) sRuntime.lastObservedQf = qf;
}

void observeLive() {
    if (sRuntime.state == STATE_RUNNING &&
        sRuntime.attemptSerial == gQFTTimer.attemptSerial()) {
        observeQf(liveQf());
    }
}

bool safeToRetry() {
    return gpApplication.mContext == TApplication::CONTEXT_DIRECT_STAGE &&
           gpMarDirector && gpMarDirector->_260 != 0 &&
           gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
           gpMarDirector->mDemoState == 0 &&
           ((gpMarDirector->mGameState & 0x2) == 0 ||
            sRuntime.holdingDeparture) &&
           gpApplication.mGamePads[0] &&
           (!gMenu || !gMenu->shown()) &&
           !Ghost::observerStatsSuppressed() &&
           !WarpWheel::shown() && !WarpWheel::promptPending();
}

bool liveResultDirector(const TMarDirector *director) {
    return director &&
           gpApplication.mContext == TApplication::CONTEXT_DIRECT_STAGE &&
           gpMarDirector == director && director->_260 != 0 &&
           director->mCurState == TMarDirector::STATE_NORMAL &&
           director->mDemoState == 0;
}

bool resultPresentationReady(const TMarDirector *director) {
    if (sRuntime.modalWaitForShineDemo) {
        const bool shineDemoStarted =
            director &&
            gpApplication.mContext == TApplication::CONTEXT_DIRECT_STAGE &&
            gpMarDirector == director && director->_260 != 0 &&
            (director->mGameState & 0x1) == 0 && director->mDemoState != 0;
        if (shineDemoStarted) sRuntime.modalWaitForShineDemo = 0;
        return false;
    }
    return liveResultDirector(director);
}

bool requestCurrent() {
    const int entry = expectedStartEntry();
    if (entry < 0 || entry >= ILing::count()) return false;

    sRuntime.state = STATE_REQUESTING;
    if (!WarpWheel::requestILStart(entry)) {
        sRuntime.state = STATE_BLOCKED;
        sRuntime.outcome = OUTCOME_WARPS_DISABLED;
        sRuntime.lastEntry = (u8)entry;
        sRuntime.lastQf = -1;
        sRuntime.displayFrames = kResultDisplayFrames;
        sRuntime.holdingDeparture = 0;
        return false;
    }
    if (sRuntime.state == STATE_REQUESTING) {
        sRuntime.state = STATE_WAITING;
    }
    sRuntime.holdingDeparture = 0;
    return true;
}

bool requestCurrentPostSave() {
    const int entry = expectedStartEntry();
    if (entry < 0 || entry >= ILing::count()) return false;

    sRuntime.state = STATE_REQUESTING;
    if (!WarpWheel::requestILStart(entry)) {
        sRuntime.state = STATE_BLOCKED;
        sRuntime.outcome = OUTCOME_WARPS_DISABLED;
        sRuntime.lastEntry = (u8)entry;
        sRuntime.lastQf = -1;
        sRuntime.displayFrames = kResultDisplayFrames;
        sRuntime.holdingDeparture = 0;
        return false;
    }
    if (sRuntime.state == STATE_REQUESTING) {
        sRuntime.state = STATE_WAITING_POST_SAVE;
    }
    sRuntime.holdingDeparture = 0;
    return true;
}

void beginAttempt(u32 serial) {
    sRuntime.attemptSerial = serial;
    clearShinePublishLatch();
    incrementSaturated(sRuntime.attempts);
    sRuntime.lastObservedQf = -1;
    observeQf(liveQf());
    sRuntime.retryFrames = 0;
    sRuntime.holdingDeparture = 0;
    sRuntime.state = STATE_RUNNING;
}

void queueFailure(Outcome outcome, s32 qf) {
    const bool waitForSavebox =
        outcome == OUTCOME_TARGET_MISS &&
        sRuntime.mode == StageLoader::MODE_STREAKING && gpMarioOriginal &&
        gpMarioOriginal->mState == kMarioWinDemoState &&
        !gSettings.getBool(SETTING_STREAK_AUTO_RESET);
    clearShinePublishLatch();
    sRuntime.modalWaitForShineDemo = 0;
    const int entry = expectedStartEntry();
    if (qf >= 0) observeQf(qf);
    if (entry >= 0) sRuntime.lastEntry = (u8)entry;
    sRuntime.lastQf = qf;
    sRuntime.outcome = outcome;
    sRuntime.currentStreak = 0;
    if (sRuntime.mode == StageLoader::MODE_STREAKING) {
        sRuntime.progress = 0;
    }
    sRuntime.retryFrames = waitForSavebox ? 0 : kRetryDelayFrames;
    sRuntime.displayFrames = kResultDisplayFrames;
    sRuntime.holdingDeparture = 0;
    sRuntime.state = waitForSavebox ? STATE_RETRY_SAVEBOX : STATE_RETRY_DELAY;
}

void finishPlaylistBest() {
    if (sRuntime.totalObservedActiveQf == 0 ||
        sRuntime.totalObservedActiveQf >=
            SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET) {
        sRuntime.playlistTimeOverflow = 1;
        sRuntime.finalPlaylistQf = SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET;
        return;
    }
    sRuntime.finalPlaylistQf = (u32)sRuntime.totalObservedActiveQf;
    if (!sPlaylistsAvailable || sPlaylistSavePending ||
        !sRuntime.playlistPbEligible ||
        sRuntime.activePlaylistId >= SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT) {
        return;
    }
    if (sRuntime.priorPlaylistBestQf !=
            SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET &&
        sRuntime.finalPlaylistQf >= sRuntime.priorPlaylistBestQf) {
        return;
    }

#if !IS_EMULATOR
    volatile SusamuneStagePlaylistsCfg *playlists =
        SUSAMUNE_STAGE_PLAYLIST_PPC_PTR;
    const u8 id = sRuntime.activePlaylistId;
    // Identity changes are committed separately with all regional bests
    // cleared. Never combine that migration with a newly earned best.
    if (playlists->contentHashes[id] != sRuntime.activePlaylistHash) return;
    playlists->bestQf[kPlaylistRegion][id] = sRuntime.finalPlaylistQf;
    publishPlaylistSave(PLAYLIST_SAVE_BEST);
#endif
}

void queueSuccess(int resultEntry, s32 qf) {
    const bool normalShine =
        gpMarioOriginal && gpMarioOriginal->mState == kMarioWinDemoState;
    clearShinePublishLatch();
    sRuntime.modalWaitForShineDemo = 0;
    observeQf(qf);
    sRuntime.lastEntry = (u8)resultEntry;
    sRuntime.lastQf = qf;
    sRuntime.outcome = OUTCOME_SUCCESS;
    incrementSaturated(sRuntime.qualifyingSuccesses);
    if (sRuntime.currentStreak != 0xffff) sRuntime.currentStreak++;
    if (sRuntime.currentStreak > sRuntime.bestStreak) {
        sRuntime.bestStreak = sRuntime.currentStreak;
    }
    sRuntime.displayFrames = kResultDisplayFrames;

    if (sRuntime.mode == StageLoader::MODE_STREAKING) {
        if (sRuntime.progress < sRuntime.goal) sRuntime.progress++;
        if (sRuntime.progress >= sRuntime.goal) {
            sRuntime.retryFrames = 0;
            sRuntime.state = STATE_COMPLETE;
            sRuntime.modalWaitForShineDemo = normalShine;
            sRuntime.modalState = MODAL_PENDING;
        } else {
            if (normalShine &&
                !gSettings.getBool(SETTING_STREAK_AUTO_RESET)) {
                sRuntime.retryFrames = 0;
                sRuntime.state = STATE_RETRY_SAVEBOX;
            } else {
                sRuntime.retryFrames = kRetryDelayFrames;
                sRuntime.state = STATE_RETRY_DELAY;
            }
        }
        return;
    }

    if (sRuntime.progress < sRuntime.activeCount) sRuntime.progress++;
    if (sRuntime.activeIndex < sRuntime.activeCount) sRuntime.activeIndex++;
    if (sRuntime.activeIndex >= sRuntime.activeCount) {
        sRuntime.retryFrames = 0;
        sRuntime.state = STATE_COMPLETE;
        finishPlaylistBest();
        sRuntime.modalWaitForShineDemo = normalShine;
        sRuntime.modalState = MODAL_PENDING;
    } else {
        sRuntime.retryFrames = kRetryDelayFrames;
        sRuntime.state = STATE_RETRY_DELAY;
    }
}

u16 modalHeld() {
    return (u16)(JUTGamePad::mPadStatus[0].mButton & kModalMask);
}

void showModal() {
    sRuntime.modalWaitForShineDemo = 0;
    sRuntime.modalState = MODAL_VISIBLE;
    sRuntime.modalPrevious = modalHeld();
    sRuntime.modalReady = sRuntime.modalPrevious == 0;
    gBinds.suppressUntilRelease();
}

void updateModal() {
    gBinds.suppressUntilRelease();
    const u16 current = modalHeld();
    if (!sRuntime.modalReady) {
        sRuntime.modalPrevious = current;
        if (current == 0) sRuntime.modalReady = 1;
        return;
    }
    const u16 pressed = (u16)(current & ~sRuntime.modalPrevious);
    sRuntime.modalPrevious = current;
    if (!pressed) return;

    sRuntime.modalState = MODAL_NONE;
    sRuntime.modalReady = 0;
    sRuntime.holdingDeparture = 0;
    sRuntime.modalWaitForShineDemo = 0;
    sRuntime.state = STATE_INACTIVE;
}

Color accentColor() {
    if (sRuntime.state == STATE_COMPLETE ||
        sRuntime.outcome == OUTCOME_SUCCESS) {
        return Color(80, 220, 120, 255);
    }
    if (sRuntime.state == STATE_BLOCKED ||
        (sRuntime.outcome >= OUTCOME_RESET &&
         sRuntime.outcome <= OUTCOME_TARGET_MISS)) {
        return Color(245, 95, 85, 255);
    }
    return Color(80, 180, 255, 255);
}

void formatDuration(u64 qf, char *out, u32 size) {
    // Keep this in 32-bit arithmetic: the freestanding PPC link has no
    // 64-bit modulo helper. The cap is roughly 49 days of active QFT.
    const u32 maxQf = (0xffffffffu / 1001u) * 120u;
    const u32 frames = qf > maxQf ? maxQf : (u32)qf;
    const u32 millis = (frames / 120u) * 1001u +
                       ((frames % 120u) * 1001u) / 120u;
    snprintf(out, size, "%u:%02u:%02u.%03u",
             (unsigned)(millis / 3600000u),
             (unsigned)((millis / 60000u) % 60u),
             (unsigned)((millis / 1000u) % 60u),
             (unsigned)(millis % 1000u));
}

void drawModalRow(Menu *menu, int y, const char *name, const char *value) {
    const Color color(120, 220, 150, 255);
    menu->drawText(name, 108, y, 15, 15, Color(200, 206, 220, 255));
    menu->drawText(value, 532 - Menu::textWidth(value, 15), y, 15, 15,
                   color);
}

void drawModalRowColor(Menu *menu, int y, const char *name,
                       const char *value, const Color &color) {
    menu->drawText(name, 108, y, 15, 15, Color(200, 206, 220, 255));
    menu->drawText(value, 532 - Menu::textWidth(value, 15), y, 15, 15,
                   color);
}

u32 completionPercent() {
    if (sRuntime.attempts == 0) return 0;
    if (sRuntime.eligibleCompletes >= sRuntime.attempts) return 100;
    return (u32)(((u64)sRuntime.eligibleCompletes * 100u +
                  sRuntime.attempts / 2u) /
                 sRuntime.attempts);
}

void formatPlaylistDelta(char *out, u32 size) {
    if (sRuntime.activePlaylistId >= SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT ||
        !sPlaylistsAvailable) {
        strcpy(out, "--");
        return;
    }
    if (!sRuntime.playlistPbEligible) {
        strcpy(out, "Ineligible");
        return;
    }
    if (sRuntime.playlistTimeOverflow) {
        strcpy(out, "Overflow");
        return;
    }
    if (sRuntime.priorPlaylistBestQf ==
        SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET) {
        strcpy(out, "--");
        return;
    }
    const bool faster = sRuntime.finalPlaylistQf <
                        sRuntime.priorPlaylistBestQf;
    const u32 difference = faster
        ? sRuntime.priorPlaylistBestQf - sRuntime.finalPlaylistQf
        : sRuntime.finalPlaylistQf - sRuntime.priorPlaylistBestQf;
    char time[24];
    formatDuration(difference, time, sizeof(time));
    snprintf(out, size, "%c%s", faster ? '-' : '+', time);
}

void drawFinalModal(Menu *menu) {
    const int x = 82;
    const int y = 66;
    const int w = 476;
    const int h = 326;
    menu->fillBox(0, 0, 640, 480, Color(0, 0, 0, 150));
    menu->fillBox(x, y, w, h, Color(8, 12, 20, 245));
    menu->fillBox(x, y, w, 4, Color(80, 220, 120, 255));

    const bool loader = sRuntime.mode == StageLoader::MODE_LOADER;
    const char *title = loader ? "PLAYLIST COMPLETE" : "STREAK COMPLETE";
    menu->drawText(title, 320 - Menu::textWidth(title, 20) / 2,
                   y + 16, 20, 20, Color(255, 255, 255, 255));

    char playlistName[28];
    const char *route = ILing::label(sRuntime.lastEntry);
    if (loader) {
        if (sRuntime.activePlaylistId <
            StageLoader::BUILTIN_PLAYLIST_COUNT) {
            route = StageLoader::builtinPlaylistName(
                sRuntime.activePlaylistId);
        } else if (sRuntime.activePlaylistId <
                   SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT) {
            snprintf(playlistName, sizeof(playlistName), "Custom %u",
                     (unsigned)(sRuntime.activePlaylistId -
                                StageLoader::BUILTIN_PLAYLIST_COUNT + 1));
            route = playlistName;
        } else {
            route = "Unsaved playlist";
        }
    }
    int routeSize = 16;
    while (routeSize > 10 && Menu::textWidth(route, routeSize) > w - 44) {
        routeSize--;
    }
    menu->drawText(route, 320 - Menu::textWidth(route, routeSize) / 2,
                   y + 48, routeSize, routeSize,
                   Color(120, 220, 150, 255));

    char target[32];
    if (loader) {
        snprintf(target, sizeof(target), "Playlist: %u levels",
                 (unsigned)sRuntime.activeCount);
    } else if (sRuntime.targetQf < 0) {
        strcpy(target, "Target: Any finish");
    } else {
        char time[20];
        ILing::formatTime(sRuntime.targetQf, time, sizeof(time));
        snprintf(target, sizeof(target), "Target: %s", time);
    }
    char streak[28];
    snprintf(streak, sizeof(streak), loader ? "Completed: %u/%u"
                                             : "Streak: %u/%u",
             (unsigned)sRuntime.progress,
             (unsigned)(loader ? sRuntime.activeCount : sRuntime.goal));
    menu->drawText(target, x + 28, y + 77, 13, 13,
                   Color(184, 194, 214, 255));
    menu->drawText(streak, x + w - 28 - Menu::textWidth(streak, 13),
                   y + 77, 13, 13, Color(184, 194, 214, 255));

    char value[32];
    int rowY = y + 119;
    snprintf(value, sizeof(value), "%u/%u (%u pct)",
             (unsigned)sRuntime.eligibleCompletes,
             (unsigned)sRuntime.attempts,
             (unsigned)completionPercent());
    drawModalRow(menu, rowY, "Completes / attempts", value);
    rowY += 34;
    if (loader && sRuntime.playlistTimeOverflow) {
        strcpy(value, "Overflow");
    } else {
        formatDuration(sRuntime.totalObservedActiveQf, value, sizeof(value));
    }
    drawModalRow(menu, rowY, loader ? "Playlist time" : "Active time", value);
    rowY += 34;
    if (loader) {
        u32 best = sRuntime.priorPlaylistBestQf;
        if (sPlaylistsAvailable && sRuntime.playlistPbEligible &&
            !sRuntime.playlistTimeOverflow &&
            sRuntime.activePlaylistId <
                SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT &&
            (best == SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET ||
             sRuntime.finalPlaylistQf < best)) {
            best = sRuntime.finalPlaylistQf;
        }
        if (best == SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET ||
            !sPlaylistsAvailable) {
            strcpy(value, "--");
        } else {
            formatDuration(best, value, sizeof(value));
        }
        drawModalRow(menu, rowY, "Best time", value);
        rowY += 34;
        formatPlaylistDelta(value, sizeof(value));
        const bool slower = value[0] == '+' && strcmp(value, "+0:00:00.000");
        drawModalRowColor(menu, rowY, "Delta", value,
                          slower ? Color(245, 95, 85, 255)
                                 : Color(120, 220, 150, 255));
    } else {
        if (sRuntime.eligibleCompletes) {
            formatDuration(sRuntime.completedQfTotal /
                               sRuntime.eligibleCompletes,
                           value, sizeof(value));
        } else {
            strcpy(value, "--");
        }
        drawModalRow(menu, rowY, "Average time", value);
    }
    rowY += 34;
    drawModalRow(menu, rowY, "Golds", "--");

    const char *hint = "A / B / START  Continue";
    menu->drawText(hint, 320 - Menu::textWidth(hint, 12) / 2,
                   y + h - 25, 12, 12, Color(104, 114, 136, 255));
}

void drawFullNotice(Menu *menu) {
    char status[96];
    char time[24];
    time[0] = '\0';
    if (sRuntime.lastQf >= 0) {
        ILing::formatTime(sRuntime.lastQf, time, sizeof(time));
    }

    if (sRuntime.mode == StageLoader::MODE_LOADER) {
        const unsigned item = sRuntime.activeIndex < sRuntime.activeCount
                                  ? sRuntime.activeIndex + 1
                                  : sRuntime.activeCount;
        switch ((Outcome)sRuntime.outcome) {
        case OUTCOME_SUCCESS:
            snprintf(status, sizeof(status), "%s %u/%u%s%s",
                     sRuntime.state == STATE_COMPLETE ? "Playlist complete"
                                                      : "Finished",
                     (unsigned)sRuntime.progress,
                     (unsigned)sRuntime.activeCount,
                     time[0] ? "  " : "", time);
            break;
        case OUTCOME_RESET:
            snprintf(status, sizeof(status), "Reset - retry %u/%u", item,
                     (unsigned)sRuntime.activeCount);
            break;
        case OUTCOME_ENDED:
            snprintf(status, sizeof(status), "Ended - retry %u/%u", item,
                     (unsigned)sRuntime.activeCount);
            break;
        case OUTCOME_WRONG_ROUTE:
            snprintf(status, sizeof(status), "Wrong finish - retry %u/%u",
                     item, (unsigned)sRuntime.activeCount);
            break;
        case OUTCOME_INELIGIBLE:
            snprintf(status, sizeof(status), "Ineligible - retry %u/%u",
                     item, (unsigned)sRuntime.activeCount);
            break;
        case OUTCOME_WARPS_DISABLED:
            strcpy(status, "Stopped - warps disabled");
            break;
        default:
            return;
        }
    } else {
        switch ((Outcome)sRuntime.outcome) {
        case OUTCOME_SUCCESS:
            snprintf(status, sizeof(status), "%s %u/%u%s%s",
                     sRuntime.state == STATE_COMPLETE ? "Complete" : "Success",
                     (unsigned)sRuntime.progress, (unsigned)sRuntime.goal,
                     time[0] ? "  " : "", time);
            break;
        case OUTCOME_RESET:
            snprintf(status, sizeof(status), "Reset - streak 0/%u",
                     (unsigned)sRuntime.goal);
            break;
        case OUTCOME_ENDED:
            snprintf(status, sizeof(status), "Ended - streak 0/%u",
                     (unsigned)sRuntime.goal);
            break;
        case OUTCOME_WRONG_ROUTE:
            snprintf(status, sizeof(status), "Wrong finish - streak 0/%u",
                     (unsigned)sRuntime.goal);
            break;
        case OUTCOME_INELIGIBLE:
            snprintf(status, sizeof(status), "Ineligible - streak 0/%u",
                     (unsigned)sRuntime.goal);
            break;
        case OUTCOME_TARGET_MISS:
            snprintf(status, sizeof(status),
                     "Target missed - streak 0/%u%s%s",
                     (unsigned)sRuntime.goal, time[0] ? "  " : "", time);
            break;
        case OUTCOME_WARPS_DISABLED:
            strcpy(status, "Stopped - warps disabled");
            break;
        default:
            return;
        }
    }

    const int x = 150;
    const int y = 350;
    const int w = 340;
    const int h = 58;
    menu->fillBox(x, y, w, h, Color(8, 12, 20, 210));
    menu->fillBox(x, y, 4, h, accentColor());
    const char *name = ILing::label(sRuntime.lastEntry);
    int size = 15;
    while (size > 10 && Menu::textWidth(name, size) > w - 22) size--;
    menu->drawText(name, x + 12, y + 7, size, size,
                   Color(255, 255, 255, 255));
    menu->drawText(status, x + 12, y + 32, 13, 13,
                   Color(230, 236, 245, 255));
}

void drawCounter(Menu *menu) {
    char text[32];
    if (sRuntime.state == STATE_BLOCKED) {
        strcpy(text, "Stopped");
    } else if (sRuntime.mode == StageLoader::MODE_STREAKING) {
        snprintf(text, sizeof(text), "%u/%u",
                 (unsigned)sRuntime.progress, (unsigned)sRuntime.goal);
    } else if (sRuntime.state == STATE_COMPLETE) {
        strcpy(text, "Playlist complete");
    } else {
        snprintf(text, sizeof(text), "%u/%u",
                 (unsigned)sRuntime.progress,
                 (unsigned)sRuntime.activeCount);
    }
    gCreationExtras.drawStageSessionCounter(menu, text);
}

}  // namespace

namespace StageLoader {

void init() {
    resetAll();
    initPlaylistPersistence();
}

int queueCount() {
    return sRuntime.draftCount;
}

int queueEntry(int position) {
    return position >= 0 && position < sRuntime.draftCount
               ? sQueues.draft[position]
               : -1;
}

bool appendQueue(int entry) {
    if (entry < 0 || entry >= ILing::count() || entry > 0xff ||
        sRuntime.draftCount >= QUEUE_CAPACITY) {
        return false;
    }
    // Repeated routes are distinct playlist positions by design.
    invalidateDraftIdentity();
    const int position = sRuntime.draftCount++;
    sQueues.draft[position] = (u8)entry;
    setAction(sQueues.draftActions, position, false);
    return true;
}

bool removeQueue(int position) {
    if (position < 0 || position >= sRuntime.draftCount) return false;
    invalidateDraftIdentity();
    for (int i = position + 1; i < sRuntime.draftCount; i++) {
        sQueues.draft[i - 1] = sQueues.draft[i];
        setAction(sQueues.draftActions, i - 1,
                  actionAt(sQueues.draftActions, i));
    }
    sQueues.draft[--sRuntime.draftCount] = 0;
    setAction(sQueues.draftActions, sRuntime.draftCount, false);
    return true;
}

bool moveQueue(int position, int direction) {
    const int destination = position +
        (direction < 0 ? -1 : direction > 0 ? 1 : 0);
    if (position < 0 || position >= sRuntime.draftCount ||
        destination < 0 || destination >= sRuntime.draftCount ||
        destination == position) {
        return false;
    }
    invalidateDraftIdentity();
    const u8 entry = sQueues.draft[position];
    sQueues.draft[position] = sQueues.draft[destination];
    sQueues.draft[destination] = entry;
    const bool action = actionAt(sQueues.draftActions, position);
    setAction(sQueues.draftActions, position,
              actionAt(sQueues.draftActions, destination));
    setAction(sQueues.draftActions, destination, action);
    return true;
}

void clearQueue() {
    memset(sQueues.draft, 0, sizeof(sQueues.draft));
    memset(sQueues.draftActions, 0, sizeof(sQueues.draftActions));
    sRuntime.draftCount = 0;
    invalidateDraftIdentity();
}

const char *builtinPlaylistName(int preset) {
    switch (preset) {
    case 0: return "Fast Any percent";
    case 1: return "All Secrets";
    case 2: return "Reds World Tour";
    default: return "Unknown";
    }
}

bool loadBuiltinPlaylist(int preset) {
    const u8 *entries = nullptr;
    u8 count = 0;
    if (!builtinDefinition(preset, &entries, &count)) return false;

    clearQueue();
    memcpy(sQueues.draft, entries, count);
    sRuntime.draftCount = (u8)count;
    if (preset == 0) {
        setAction(sQueues.draftActions, kFastAnyPv5Position, true);
    }
    sRuntime.draftPlaylistId = (u8)preset;
    sRuntime.draftPlaylistHash = builtinContentHash(preset);
    return true;
}

bool loadCustomPlaylist(int slot) {
#if IS_EMULATOR
    (void)slot;
    return false;
#else
    if (!sPlaylistsAvailable || slot < 0 ||
        slot >= CUSTOM_PLAYLIST_COUNT) {
        return false;
    }
    volatile SusamuneStagePlaylistsCfg *playlists =
        SUSAMUNE_STAGE_PLAYLIST_PPC_PTR;
    const u8 count = playlists->counts[slot];
    if (count > QUEUE_CAPACITY) return false;
    for (u8 i = 0; i < count; i++) {
        if (playlists->entries[slot][i] >= ILing::count()) return false;
    }

    memset(sQueues.draft, 0, sizeof(sQueues.draft));
    memset(sQueues.draftActions, 0, sizeof(sQueues.draftActions));
    memcpy(sQueues.draft, (const void *)playlists->entries[slot], count);
    memcpy(sQueues.draftActions, (const void *)playlists->actions[slot],
           sizeof(sQueues.draftActions));
    sRuntime.draftCount = count;
    sRuntime.draftPlaylistId = (u8)(BUILTIN_PLAYLIST_COUNT + slot);
    sRuntime.draftPlaylistHash =
        playlists->contentHashes[sRuntime.draftPlaylistId];
    return true;
#endif
}

bool saveCustomPlaylist(int slot) {
#if IS_EMULATOR
    (void)slot;
    return false;
#else
    if (!sPlaylistsAvailable || sPlaylistSavePending ||
        sRuntime.state != STATE_INACTIVE ||
        sRuntime.draftCount == 0 || slot < 0 ||
        slot >= CUSTOM_PLAYLIST_COUNT) {
        return false;
    }
    volatile SusamuneStagePlaylistsCfg *playlists =
        SUSAMUNE_STAGE_PLAYLIST_PPC_PTR;
    playlists->counts[slot] = sRuntime.draftCount;
    memset((void *)playlists->entries[slot], 0,
           sizeof(playlists->entries[slot]));
    memcpy((void *)playlists->entries[slot], sQueues.draft,
           sRuntime.draftCount);
    memcpy((void *)playlists->actions[slot], sQueues.draftActions,
           sizeof(playlists->actions[slot]));
    playlists->revisions[slot]++;
    const u8 id = (u8)(BUILTIN_PLAYLIST_COUNT + slot);
    const u32 hash = playlistContentHash(
        id, playlists->revisions[slot], sRuntime.draftCount,
        sQueues.draft, sQueues.draftActions);
    playlists->contentHashes[id] = hash;
    for (int region = 0;
         region < SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT; region++) {
        playlists->bestQf[region][id] =
            SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET;
    }

    sRuntime.draftPlaylistId = id;
    sRuntime.draftPlaylistHash = hash;
    sPlaylistPendingDraftId = id;
    sPlaylistPendingDraftHash = hash;
    return publishPlaylistSave(PLAYLIST_SAVE_CUSTOM);
#endif
}

bool customPlaylistsAvailable() {
    return sPlaylistsAvailable;
}

bool customPlaylistSavePending() {
    return sPlaylistSavePending;
}

int customPlaylistEntryCount(int slot) {
#if IS_EMULATOR
    (void)slot;
    return -1;
#else
    if (!sPlaylistsAvailable || slot < 0 ||
        slot >= CUSTOM_PLAYLIST_COUNT) {
        return -1;
    }
    const u8 count = SUSAMUNE_STAGE_PLAYLIST_PPC_PTR->counts[slot];
    return count <= QUEUE_CAPACITY ? count : -1;
#endif
}

u32 customPlaylistLastError() {
    return sPlaylistLastError;
}

bool startLoader() {
    if (!sRuntime.draftCount || sPlaylistSavePending ||
        Ghost::observerPreparing() ||
        Ghost::observerStatsSuppressed()) {
        return false;
    }
    const u8 count = sRuntime.draftCount;
    const u8 playlistId = sRuntime.draftPlaylistId;
    const u32 playlistHash = sRuntime.draftPlaylistHash;
    resetSession();
    memcpy(sQueues.active, sQueues.draft, count);
    memcpy(sRuntime.activeActions, sQueues.draftActions,
           sizeof(sRuntime.activeActions));
    sRuntime.activeCount = count;
    sRuntime.goal = count;
    sRuntime.mode = MODE_LOADER;
    sRuntime.activePlaylistId = playlistId;
    sRuntime.activePlaylistHash = playlistHash;
    sRuntime.playlistPbEligible = 1;
#if !IS_EMULATOR
    if (sPlaylistsAvailable &&
        playlistId < SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT) {
        volatile SusamuneStagePlaylistsCfg *playlists =
            SUSAMUNE_STAGE_PLAYLIST_PPC_PTR;
        if (playlists->contentHashes[playlistId] == playlistHash) {
            sRuntime.priorPlaylistBestQf =
                playlists->bestQf[kPlaylistRegion][playlistId];
        }
    }
#endif
    if (requestCurrent()) return true;
    resetSession();
    return false;
}

bool startStreak(int entry, u16 finishes, s32 targetQf) {
    if (entry < 0 || entry >= ILing::count() || entry > 0xff ||
        finishes == 0 || targetQf < -1 || Ghost::observerPreparing() ||
        Ghost::observerStatsSuppressed()) {
        return false;
    }
    resetSession();
    sQueues.active[0] = (u8)entry;
    sRuntime.activeCount = 1;
    sRuntime.goal = finishes;
    sRuntime.targetQf = targetQf;
    sRuntime.mode = MODE_STREAKING;
    if (requestCurrent()) return true;
    resetSession();
    return false;
}

bool start(int entry, u16 finishes, s32 targetQf) {
    return startStreak(entry, finishes, targetQf);
}

void cancel() {
    resetSession();
}

bool active() {
    return sRuntime.state != STATE_INACTIVE;
}

Mode mode() {
    return (Mode)sRuntime.mode;
}

void getStats(SessionStats *out) {
    if (!out) return;
    out->attempts = sRuntime.attempts;
    out->eligibleCompletes = sRuntime.eligibleCompletes;
    out->qualifyingSuccesses = sRuntime.qualifyingSuccesses;
    out->totalObservedActiveQf = sRuntime.totalObservedActiveQf;
    out->completedAverageQf = sRuntime.eligibleCompletes
                                  ? (s32)(sRuntime.completedQfTotal /
                                          sRuntime.eligibleCompletes)
                                  : -1;
    out->bestStreak = sRuntime.bestStreak;
    out->golds = sRuntime.golds;
}

bool modal() {
    return sRuntime.modalState == MODAL_VISIBLE;
}

bool resultOwnsInput() {
    return sRuntime.modalState == MODAL_VISIBLE ||
           (sRuntime.modalState == MODAL_PENDING &&
            liveResultDirector(gpMarDirector));
}

bool resultPending() {
    return sRuntime.modalState != MODAL_NONE;
}

bool departureResultPending() {
    return resultPending() || shinePublishPending();
}

bool fastTextSuppressed() {
    return sRuntime.state != STATE_INACTIVE &&
           sRuntime.state != STATE_COMPLETE &&
           sRuntime.state != STATE_BLOCKED &&
           sRuntime.activeIndex < sRuntime.activeCount &&
           expectedStartEntry() ==
               SUSAMUNE_STAGE_PLAYLIST_ACTION_PIANTA_5 &&
           actionAt(sRuntime.activeActions, sRuntime.activeIndex);
}

bool activeRouteMatches(int startEntry, int resultEntry) {
    return sRuntime.state == STATE_RUNNING &&
           expectedStartEntry() == startEntry &&
           expectedResultEntry() == resultEntry;
}

bool deferRestartInput() {
    return sRuntime.state == STATE_RUNNING;
}

bool acceptDeferredRestart() {
    return sRuntime.state == STATE_RUNNING &&
           sRuntime.modalState == MODAL_NONE;
}

bool retryOwnsDeparture() {
    return sRuntime.state == STATE_REQUESTING ||
           sRuntime.state == STATE_WAITING ||
           sRuntime.state == STATE_RETRY_DELAY ||
           sRuntime.state == STATE_RETRY_PENDING ||
           sRuntime.state == STATE_RETRY_SAVEBOX ||
           sRuntime.state == STATE_WAITING_POST_SAVE;
}

bool holdGameModeBeforeUpdate(TMarDirector *director) {
    const bool live = liveResultDirector(director);

    if (sRuntime.modalState == MODAL_VISIBLE) return live;
    if (sRuntime.modalState == MODAL_PENDING) {
        if (!resultPresentationReady(director)) return false;
        showModal();
        return true;
    }
    if (live && (director->mGameState & 0x2) && shinePublishPending()) {
        return true;
    }

    if ((sRuntime.state == STATE_RETRY_DELAY ||
         sRuntime.state == STATE_RETRY_PENDING) &&
        live &&
        (director->mGameState & 0x2)) {
        sRuntime.holdingDeparture = 1;
        return true;
    }
    if (sRuntime.state != STATE_RETRY_DELAY &&
        sRuntime.state != STATE_RETRY_PENDING) {
        sRuntime.holdingDeparture = 0;
    }
    return false;
}

bool holdPostSaveDeparture() {
    if (sRuntime.state == STATE_RETRY_SAVEBOX) {
        return requestCurrentPostSave();
    }
    return sRuntime.state == STATE_WAITING_POST_SAVE;
}

bool copyDeathRetryDest(LevelWarp::Dest *out) {
    if (!out || sRuntime.state != STATE_RUNNING ||
        sRuntime.activeIndex >= sRuntime.activeCount ||
        gpApplication.mContext != TApplication::CONTEXT_DIRECT_STAGE ||
        !gpMarDirector || gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState != TMarDirector::STATE_DEATH) {
        return false;
    }
    return ILing::copySessionDeathRetryDest(expectedStartEntry(), out);
}

void update() {
    pollPlaylistSave();
    if (gSettings.getBool(SETTING_DISABLE_WARPS) &&
        (sRuntime.state == STATE_RETRY_SAVEBOX ||
         sRuntime.state == STATE_WAITING_POST_SAVE)) {
        const int entry = expectedStartEntry();
        sRuntime.state = STATE_BLOCKED;
        sRuntime.outcome = OUTCOME_WARPS_DISABLED;
        if (entry >= 0) sRuntime.lastEntry = (u8)entry;
        sRuntime.lastQf = -1;
        sRuntime.displayFrames = kResultDisplayFrames;
        sRuntime.holdingDeparture = 0;
        return;
    }
    const bool shinePending = shinePublishPending();
    if (sRuntime.modalState == MODAL_VISIBLE) {
        updateModal();
        return;
    }
    if (sRuntime.modalState == MODAL_PENDING) {
        if (resultPresentationReady(gpMarDirector) &&
            (!gMenu || !gMenu->shown()) &&
            !WarpWheel::shown() && !WarpWheel::promptPending()) {
            showModal();
        }
        return;
    }

    if (shinePending && sRuntime.shinePublishLatchFrames > 0) {
        sRuntime.shinePublishLatchFrames--;
    }

    observeLive();
    if (sRuntime.displayFrames > 0) sRuntime.displayFrames--;
    if (sRuntime.state == STATE_BLOCKED) {
        if (sRuntime.displayFrames == 0) resetSession();
        return;
    }
    if (sRuntime.state == STATE_INACTIVE ||
        sRuntime.state == STATE_COMPLETE) {
        return;
    }

    if (sRuntime.state == STATE_RETRY_DELAY) {
        if (sRuntime.retryFrames > 0) sRuntime.retryFrames--;
        if (sRuntime.retryFrames == 0) {
            sRuntime.state = STATE_RETRY_PENDING;
        }
    }
    if (sRuntime.state != STATE_RETRY_PENDING || !safeToRetry()) return;
    requestCurrent();
}

void onILAttemptStarted(int entry) {
    if (!active() || sRuntime.state == STATE_COMPLETE ||
        sRuntime.state == STATE_BLOCKED) {
        return;
    }

    const u32 serial = gQFTTimer.attemptSerial();
    if (entry != expectedStartEntry()) {
        if (sRuntime.state == STATE_RUNNING) {
            queueFailure(OUTCOME_WRONG_ROUTE, -1);
        } else if (sRuntime.state == STATE_WAITING ||
                   sRuntime.state == STATE_WAITING_POST_SAVE) {
            sRuntime.state = STATE_RETRY_PENDING;
        }
        return;
    }

    if (sRuntime.state == STATE_RUNNING) {
        if (serial == sRuntime.attemptSerial) return;
        queueFailure(OUTCOME_RESET, -1);
    }
    beginAttempt(serial);
}

void onILAttemptEnded() {
    if (sRuntime.state == STATE_RUNNING) {
        queueFailure(OUTCOME_ENDED, liveQf());
    } else if (sRuntime.state == STATE_WAITING ||
               sRuntime.state == STATE_WAITING_POST_SAVE) {
        sRuntime.state = STATE_RETRY_PENDING;
    }
}

void onILResult(int entry, s32 qf, bool eligible) {
    if (sRuntime.state != STATE_RUNNING) return;
    if (!eligible) invalidatePlaylistBest();
    if (entry != expectedResultEntry()) {
        queueFailure(OUTCOME_WRONG_ROUTE, qf);
        return;
    }
    if (!eligible || qf < 0) {
        queueFailure(OUTCOME_INELIGIBLE, qf);
        return;
    }

    incrementSaturated(sRuntime.eligibleCompletes);
    addSaturated(sRuntime.completedQfTotal, (u32)qf);
    if (sRuntime.mode == MODE_STREAKING && sRuntime.targetQf >= 0 &&
        qf > sRuntime.targetQf) {
        queueFailure(OUTCOME_TARGET_MISS, qf);
        return;
    }
    queueSuccess(entry, qf);
}

void onILWarpCancelled() {
    if (sRuntime.state == STATE_WAITING ||
        sRuntime.state == STATE_WAITING_POST_SAVE) {
        sRuntime.retryFrames = 0;
        sRuntime.state = STATE_RETRY_PENDING;
    }
}

void invalidatePlaylistBest() {
    if (sRuntime.mode == MODE_LOADER && sRuntime.state != STATE_INACTIVE) {
        sRuntime.playlistPbEligible = 0;
    }
}

void draw(Menu *menu) {
    if (!menu) return;
    if (sRuntime.modalState == MODAL_VISIBLE) {
        drawFinalModal(menu);
        return;
    }
    if (menu->shown() || !active()) return;

    const u8 display = gSettings.get(SETTING_STAGE_SESSION_DISPLAY);
    if (display == 1) {
        drawCounter(menu);
    } else if (display == 0 &&
               (sRuntime.displayFrames > 0 ||
                sRuntime.state == STATE_BLOCKED)) {
        drawFullNotice(menu);
    }
}

}  // namespace StageLoader
