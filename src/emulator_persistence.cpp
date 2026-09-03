#include "susamune/emulator_persistence.hxx"

#if IS_EMULATOR

#include <Dolphin/CARD.h>
#include <Dolphin/DVD.h>
#include <Dolphin/mem.h>
#include <Dolphin/OS.h>
#include <JSystem/JKernel/JKRHeap.hxx>
#include <SMS/System/Application.hxx>
#include <SMS/System/CardManager.hxx>
#include "susamune/addresses.hxx"

namespace EmulatorPersistence {
namespace {

constexpr u32 kRecordMagic = 0x53554346u;  // 'SUCF'
constexpr u16 kRecordVersion = 6;
constexpr u32 kSectorSize = 0x2000;
constexpr u32 kFileSize = kSectorSize * 2;
constexpr char kFileName[] = "susamune_settings";

struct Record {
    u32 magic;
    u16 version;
    u16 payloadSize;
    u32 generation;
    u32 checksum;
    u32 gameVersion;
    u8 reserved[12];
    SusamuneCfg cfg;
    u8 padding[kSectorSize - 32 - sizeof(SusamuneCfg)];
};
static_assert(sizeof(Record) == kSectorSize, "card record must fill one sector");

struct RecordV1 {
    u32 magic;
    u16 version;
    u16 payloadSize;
    u32 generation;
    u32 checksum;
    u32 gameVersion;
    u8 reserved[12];
    u8 cfg[2144];
    u8 padding[kSectorSize - 32 - 2144];
};
static_assert(sizeof(RecordV1) == kSectorSize, "old card record size changed");

struct RecordV2 {
    u32 magic;
    u16 version;
    u16 payloadSize;
    u32 generation;
    u32 checksum;
    u32 gameVersion;
    u8 reserved[12];
    u8 cfg[2720];
    u8 padding[kSectorSize - 32 - 2720];
};
static_assert(sizeof(RecordV2) == kSectorSize, "V2 card record size changed");

struct RecordV3 {
    u32 magic;
    u16 version;
    u16 payloadSize;
    u32 generation;
    u32 checksum;
    u32 gameVersion;
    u8 reserved[12];
    u8 cfg[2784];
    u8 padding[kSectorSize - 32 - 2784];
};
static_assert(sizeof(RecordV3) == kSectorSize, "V3 card record size changed");

struct RecordV4 {
    u32 magic;
    u16 version;
    u16 payloadSize;
    u32 generation;
    u32 checksum;
    u32 gameVersion;
    u8 reserved[12];
    u8 cfg[4928];
    u8 padding[kSectorSize - 32 - 4928];
};
static_assert(sizeof(RecordV4) == kSectorSize, "V4 card record size changed");

struct RecordV5 {
    u32 magic;
    u16 version;
    u16 payloadSize;
    u32 generation;
    u32 checksum;
    u32 gameVersion;
    u8 reserved[12];
    u8 cfg[5016];
    u8 padding[kSectorSize - 32 - 5016];
};
static_assert(sizeof(RecordV5) == kSectorSize, "V5 card record size changed");

// Only diskID is needed. The offset and stride come from the decomp's complete
// CARDControl definition; keep this view tied to its 0x110-byte retail layout.
struct CardControlIdentity {
    u8 pad[0x10c];
    DVDDiskID *diskID;
};
static_assert(sizeof(CardControlIdentity) == 0x110,
              "CARDControl identity view changed");

struct State {
    OSMutex mutex;
    SusamuneCfg cfg;
    DVDDiskID diskID;
    u32 requested;
    u32 completed;
    u32 generation;
    s32 completedStatus;
    s8 activeRecord;
    bool initialSave;
    bool idleObserved;
};
static_assert(sizeof(State) <= SUSAMUNE_DOLPHIN_PERSIST_SIZE,
              "emulator persistence state exceeds its MEM2 window");
static_assert((SUSAMUNE_DOLPHIN_PERSIST_PPC_BASE & 31u) == 0,
              "emulator persistence state is not cache-line aligned");

State *sState;
InitResult sInitResult = INIT_WAITING;
u32 sInitError;

constexpr u32 kErrorAllocation = 0x100u;

u32 errorCode(s32 result) {
    return result < 0 ? static_cast<u32>(-result) : static_cast<u32>(result);
}

u8 *align32(u8 *p) {
    return reinterpret_cast<u8 *>((reinterpret_cast<u32>(p) + 31u) & ~31u);
}

void initBlank(SusamuneCfg *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->magic = SUSAMUNE_CFG_MAGIC;
    cfg->version = SUSAMUNE_CFG_VERSION;
    cfg->flags = SUSAMUNE_CFG_FLAG_INPUT_DISPLAY |
                 SUSAMUNE_CFG_FLAG_METADATA_DISPLAY |
                 SUSAMUNE_CFG_FLAG_ILING_PBS |
                 SUSAMUNE_CFG_FLAG_QFT_DISPLAY |
                 SUSAMUNE_CFG_FLAG_METADATA_STYLE |
                 SUSAMUNE_CFG_FLAG_INPUT_STYLE |
                 SUSAMUNE_CFG_FLAG_CREATION |
                 SUSAMUNE_CFG_FLAG_WALLKICK_STYLE |
                 SUSAMUNE_CFG_FLAG_ILING_PROFILES |
                 SUSAMUNE_CFG_FLAG_MOVEMENT_STYLE;
    cfg->ilingPbs.magic = SUSAMUNE_ILING_PB_MAGIC;
    cfg->ilingPbs.version = SUSAMUNE_ILING_PB_VERSION;
    cfg->ilingPbs.count = SUSAMUNE_ILING_PB_LEGACY_SLOT_COUNT;
    for (u32 i = 0; i < SUSAMUNE_ILING_PB_LEGACY_MAX_SLOTS; i++) {
        cfg->ilingPbs.values[i] = SUSAMUNE_ILING_PB_UNSET;
    }
    cfg->ilingProfiles.magic = SUSAMUNE_ILING_PROFILE_MAGIC;
    cfg->ilingProfiles.version = SUSAMUNE_ILING_PROFILE_VERSION;
    cfg->ilingProfiles.profileCount = SUSAMUNE_ILING_PROFILE_COUNT;
    cfg->ilingProfiles.activeProfile = 0;
    cfg->ilingProfiles.slotCount = SUSAMUNE_ILING_PB_MAX_SLOTS;
    cfg->ilingProfiles.nameSize = SUSAMUNE_ILING_PROFILE_NAME_SIZE;
    for (u32 profile = 0; profile < SUSAMUNE_ILING_PROFILE_COUNT; profile++) {
        for (u32 slot = 0; slot < SUSAMUNE_ILING_PB_MAX_SLOTS; slot++) {
            cfg->ilingProfiles.values[profile][slot] = SUSAMUNE_ILING_PB_UNSET;
        }
    }
    memcpy(cfg->ilingProfiles.customNames[0], "Custom 1", sizeof("Custom 1"));
    memcpy(cfg->ilingProfiles.customNames[1], "Custom 2", sizeof("Custom 2"));
}

void migrateLegacyPBs(SusamuneCfg *cfg) {
    const SusamuneILingPbCfg &legacy = cfg->ilingPbs;
    if (!(cfg->flags & SUSAMUNE_CFG_FLAG_ILING_PBS) ||
        legacy.magic != SUSAMUNE_ILING_PB_MAGIC ||
        legacy.version != SUSAMUNE_ILING_PB_VERSION ||
        legacy.count > SUSAMUNE_ILING_PB_LEGACY_MAX_SLOTS) {
        return;
    }
    for (u16 slot = 0; slot < legacy.count; slot++) {
        const s32 value = legacy.values[slot];
        if (value >= 0 && value <= SUSAMUNE_ILING_PB_MAX_QF) {
            cfg->ilingProfiles.values[0][slot] = value;
        }
    }
}

constexpr u32 kProfilesOffsetV1 = 2784;
constexpr u32 kMovementOffsetV5 =
    kProfilesOffsetV1 + sizeof(SusamuneILingProfilesCfgV1);
static_assert(__builtin_offsetof(SusamuneCfg, ilingProfiles) ==
                  kProfilesOffsetV1,
              "profile mailbox prefix moved");
static_assert(kMovementOffsetV5 == 4928,
              "old movement-style offset changed");

bool validPBValue(s32 value) {
    return value >= SUSAMUNE_ILING_PB_UNSET &&
           value <= SUSAMUNE_ILING_PB_MAX_QF;
}

void migrateProfilesV1(SusamuneCfg *cfg, const u8 *oldCfg) {
    const SusamuneILingProfilesCfgV1 *old =
        reinterpret_cast<const SusamuneILingProfilesCfgV1 *>(
            oldCfg + kProfilesOffsetV1);
    SusamuneILingProfilesCfg &current = cfg->ilingProfiles;
    if (old->magic != SUSAMUNE_ILING_PROFILE_MAGIC ||
        old->version != SUSAMUNE_ILING_PROFILE_VERSION_V1 ||
        old->profileCount != SUSAMUNE_ILING_PROFILE_COUNT ||
        old->activeProfile >= SUSAMUNE_ILING_PROFILE_COUNT ||
        old->slotCount == 0 ||
        old->slotCount > SUSAMUNE_ILING_PB_LEGACY_MAX_SLOTS ||
        old->nameSize != SUSAMUNE_ILING_PROFILE_NAME_SIZE) {
        migrateLegacyPBs(cfg);
        return;
    }

    for (u32 profile = 0; profile < SUSAMUNE_ILING_PROFILE_COUNT; profile++) {
        for (u32 slot = 0; slot < old->slotCount; slot++) {
            if (!validPBValue(old->values[profile][slot])) {
                migrateLegacyPBs(cfg);
                return;
            }
        }
    }

    current.magic = SUSAMUNE_ILING_PROFILE_MAGIC;
    current.version = SUSAMUNE_ILING_PROFILE_VERSION;
    current.profileCount = SUSAMUNE_ILING_PROFILE_COUNT;
    current.activeProfile = old->activeProfile;
    current.slotCount = SUSAMUNE_ILING_PB_SLOT_COUNT;
    current.nameSize = SUSAMUNE_ILING_PROFILE_NAME_SIZE;
    current.saveSeq = old->saveSeq;
    current.ackSeq = old->ackSeq;
    current.status = old->status;
    for (u32 profile = 0; profile < SUSAMUNE_ILING_PROFILE_COUNT; profile++) {
        for (u32 slot = 0; slot < old->slotCount; slot++) {
            current.values[profile][slot] = old->values[profile][slot];
        }
    }
    memcpy(current.customNames, old->customNames,
           sizeof(current.customNames));
}

void migrateRecordCfg(SusamuneCfg *cfg, const u8 *oldCfg, u32 oldSize,
                      bool hasProfiles, bool hasMovement) {
    initBlank(cfg);
    const u32 prefixSize = hasProfiles ? kProfilesOffsetV1 : oldSize;
    memcpy(cfg, oldCfg, prefixSize);
    if (hasProfiles) {
        migrateProfilesV1(cfg, oldCfg);
    } else {
        migrateLegacyPBs(cfg);
    }
    if (hasMovement) {
        memcpy(&cfg->movementStyle, oldCfg + kMovementOffsetV5,
               sizeof(cfg->movementStyle));
    }
    cfg->flags |= SUSAMUNE_CFG_FLAG_QFT_DISPLAY |
                  SUSAMUNE_CFG_FLAG_METADATA_STYLE |
                  SUSAMUNE_CFG_FLAG_INPUT_STYLE |
                  SUSAMUNE_CFG_FLAG_CREATION |
                  SUSAMUNE_CFG_FLAG_WALLKICK_STYLE |
                  SUSAMUNE_CFG_FLAG_ILING_PROFILES |
                  SUSAMUNE_CFG_FLAG_MOVEMENT_STYLE;
}

u32 checksum(Record *record) {
    const u32 saved = record->checksum;
    record->checksum = 0;
    const u8 *bytes = reinterpret_cast<const u8 *>(record);
    u32 hash = 2166136261u;
    for (u32 i = 0; i < sizeof(*record); i++) {
        hash = (hash ^ bytes[i]) * 16777619u;
    }
    record->checksum = saved;
    return hash;
}

bool valid(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic &&
           record->version == kRecordVersion &&
           record->payloadSize == sizeof(SusamuneCfg) &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV1(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 1 &&
           record->payloadSize == 2144 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV2(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 2 &&
           record->payloadSize == 2720 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV3(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 3 &&
           record->payloadSize == 2784 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV4(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 4 &&
           record->payloadSize == 4928 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV5(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 5 &&
           record->payloadSize == 5016 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool newer(u32 a, u32 b) { return static_cast<s32>(a - b) > 0; }

s32 probe() {
    s32 sectorSize = 0;
    s32 result;
    do {
        result = CARDProbeEx(CARD_SLOTB, nullptr, &sectorSize);
        if (result == CARD_ERROR_BUSY) OSYieldThread();
    } while (result == CARD_ERROR_BUSY);
    if (result == CARD_ERROR_READY && sectorSize != static_cast<s32>(kSectorSize)) {
        return CARD_ERROR_WRONGDEVICE;
    }
    return result;
}

s32 mount(void *mountWork) {
    s32 result = probe();
    if (result != CARD_ERROR_READY) return result;
    volatile u16 *encoding = reinterpret_cast<volatile u16 *>(
        SUSAMUNE_ADDR_FONT_ENCODING);
    const u16 originalEncoding = *encoding;
    result = CARDMount(CARD_SLOTB, mountWork, nullptr);
    *encoding = originalEncoding;
    if (result != CARD_ERROR_READY) {
        CARDUnmount(CARD_SLOTB);
        return result;
    }
    result = CARDCheck(CARD_SLOTB);
    if (result != CARD_ERROR_READY) CARDUnmount(CARD_SLOTB);
    return result;
}

void unmount() { CARDUnmount(CARD_SLOTB); }

s32 openOrCreate(CARDFileInfo *file) {
    s32 result = CARDOpen(CARD_SLOTB, kFileName, file);
    if (result == CARD_ERROR_NOFILE) {
        result = CARDCreate(CARD_SLOTB, kFileName, kFileSize, file);
    }
    return result;
}

s32 writeRecordLocked() {
    // Saves happen only after the boot option payload has been consumed. The
    // manager refills its sector buffer before every later game operation.
    // service() owns its mutex here, so the slot-A worker cannot touch either
    // borrowed buffer until the slot-B write is finished.
    gpCardManager->unmount();
    void *mountWork = gpCardManager->mCardWorkArea;
    Record *record = reinterpret_cast<Record *>(gpCardManager->mCARDBlock);

    OSLockMutex(&sState->mutex);
    const s8 target = sState->activeRecord == 0 ? 1 : 0;
    const u32 generation = sState->generation + 1;
    memset(record, 0, sizeof(*record));
    record->magic = kRecordMagic;
    record->version = kRecordVersion;
    record->payloadSize = sizeof(SusamuneCfg);
    record->generation = generation;
    record->gameVersion = SUSAMUNE_GAME_VERSION;
    memcpy(&record->cfg, &sState->cfg, sizeof(sState->cfg));
    record->checksum = checksum(record);
    OSUnlockMutex(&sState->mutex);

    s32 result = mount(mountWork);
    if (result != CARD_ERROR_READY) {
        return result;
    }

    CARDFileInfo file;
    result = openOrCreate(&file);
    if (result == CARD_ERROR_READY) {
        result = CARDWrite(&file, record, sizeof(*record),
                           target * kSectorSize);
        const s32 closeResult = CARDClose(&file);
        if (result == CARD_ERROR_READY) result = closeResult;
    }
    unmount();

    if (result == CARD_ERROR_READY) {
        OSLockMutex(&sState->mutex);
        sState->activeRecord = target;
        sState->generation = generation;
        sState->initialSave = false;
        OSUnlockMutex(&sState->mutex);
    }
    return result;
}

void initState() {
    sState = reinterpret_cast<State *>(SUSAMUNE_DOLPHIN_PERSIST_PPC_BASE);
    memset(sState, 0, sizeof(*sState));
    sState->activeRecord = -1;
    OSInitMutex(&sState->mutex);
    initBlank(&sState->cfg);
}

void setIdentity() {
#if defined(SUSAMUNE_VERSION_JP)
    const char region = 'J';
#elif defined(SUSAMUNE_VERSION_US)
    const char region = 'E';
#else
    const char region = 'P';
#endif
    sState->diskID.mName[0] = 'G';
    sState->diskID.mName[1] = 'M';
    sState->diskID.mName[2] = 'S';
    sState->diskID.mName[3] = region;
    sState->diskID.mCompany[0] = 'S';
    sState->diskID.mCompany[1] = 'U';

    CardControlIdentity *cards = reinterpret_cast<CardControlIdentity *>(
        SUSAMUNE_ADDR_CARD_BLOCKS);
    cards[CARD_SLOTB].diskID = &sState->diskID;
}

s32 loadRecords(void *mountWork, Record *record) {
    OSLockMutex(&gpCardManager->mMutex);
    s32 result = mount(mountWork);
    if (result != CARD_ERROR_READY) {
        OSUnlockMutex(&gpCardManager->mMutex);
        return result;
    }

    CARDFileInfo file;
    result = CARDOpen(CARD_SLOTB, kFileName, &file);
    if (result == CARD_ERROR_NOFILE) {
        sState->initialSave = true;
        unmount();
        OSUnlockMutex(&gpCardManager->mMutex);
        return CARD_ERROR_READY;
    }
    if (result != CARD_ERROR_READY) {
        unmount();
        OSUnlockMutex(&gpCardManager->mMutex);
        return result;
    }

    bool haveRecord = false;
    u32 bestGeneration = 0;
    for (s8 slot = 0; slot < 2; slot++) {
        result = CARDRead(&file, record, sizeof(*record),
                          slot * kSectorSize);
        if (result != CARD_ERROR_READY) break;
        const bool current = valid(record);
        const bool v5 = !current && validV5(record);
        const bool v4 = !current && !v5 && validV4(record);
        const bool v3 = !current && !v5 && !v4 && validV3(record);
        const bool v2 = !current && !v5 && !v4 && !v3 && validV2(record);
        const bool v1 =
            !current && !v5 && !v4 && !v3 && !v2 && validV1(record);
        if ((current || v5 || v4 || v3 || v2 || v1) &&
            (!haveRecord || newer(record->generation, bestGeneration))) {
            if (current) {
                memcpy(&sState->cfg, &record->cfg, sizeof(sState->cfg));
            } else {
                const u32 oldSize =
                    v1 ? sizeof(((RecordV1 *)0)->cfg)
                       : v2 ? sizeof(((RecordV2 *)0)->cfg)
                       : v3 ? sizeof(((RecordV3 *)0)->cfg)
                       : v4 ? sizeof(((RecordV4 *)0)->cfg)
                            : sizeof(((RecordV5 *)0)->cfg);
                migrateRecordCfg(&sState->cfg,
                                 reinterpret_cast<const u8 *>(&record->cfg),
                                 oldSize, v4 || v5, v5);
            }
            bestGeneration = record->generation;
            sState->activeRecord = slot;
            sState->initialSave = !current;
            haveRecord = true;
        }
    }
    const s32 closeResult = CARDClose(&file);
    unmount();
    OSUnlockMutex(&gpCardManager->mMutex);
    if (result == CARD_ERROR_READY) result = closeResult;
    if (result != CARD_ERROR_READY) return result;

    if (haveRecord) {
        sState->generation = bestGeneration;
    } else {
        sState->initialSave = true;
    }
    return CARD_ERROR_READY;
}

}  // namespace

InitResult init() {
    if (sInitResult != INIT_WAITING) return sInitResult;
    if (!gpCardManager) return INIT_WAITING;
    const s32 probeResult = probe();
    if (probeResult != CARD_ERROR_READY) {
        sInitError = errorCode(probeResult);
        sInitResult = INIT_UNAVAILABLE;
        return sInitResult;
    }
    initState();

    // During the boot state this is an ordinary expandable 5 MiB heap. The
    // CARD buffers are needed only for this synchronous read and are released
    // before the logo director runs.
    const u32 temporarySize = CARD_WORKAREA + sizeof(Record) + 62;
    u8 *temporary = static_cast<u8 *>(
        JKRHeap::alloc(temporarySize, 32, gpApplication.mCurrentHeap));
    if (!temporary) {
        sInitError = kErrorAllocation;
        sInitResult = INIT_UNAVAILABLE;
        return sInitResult;
    }
    void *mountWork = align32(temporary);
    Record *record = reinterpret_cast<Record *>(
        align32(reinterpret_cast<u8 *>(mountWork) + CARD_WORKAREA));

    setIdentity();
    const s32 loadResult = loadRecords(mountWork, record);
    JKRHeap::free(temporary, gpApplication.mCurrentHeap);
    if (loadResult != CARD_ERROR_READY) {
        sInitError = errorCode(loadResult);
        sInitResult = INIT_UNAVAILABLE;
        return sInitResult;
    }

    sInitResult = INIT_READY;
    return sInitResult;
}

void service() {
    if (sInitResult != INIT_READY) return;

    OSLockMutex(&sState->mutex);
    const u32 ticket = sState->requested;
    const bool pending = ticket != sState->completed;
    OSUnlockMutex(&sState->mutex);
    if (!pending) {
        sState->idleObserved = false;
        return;
    }

    // Never wait behind Sunshine's worker. Its status and completed-read
    // payload remain untouched until the director has seen an idle frame.
    if (!OSTryLockMutex(&gpCardManager->mMutex)) {
        sState->idleObserved = false;
        return;
    }
    if (gpCardManager->mLastStatus == CARD_ERROR_BUSY) {
        sState->idleObserved = false;
        OSUnlockMutex(&gpCardManager->mMutex);
        return;
    }
    if (!sState->idleObserved) {
        sState->idleObserved = true;
        OSUnlockMutex(&gpCardManager->mMutex);
        return;
    }
    sState->idleObserved = false;

    // unmount() normally replaces this with CARDUnmount's result. Keep the
    // result Sunshine's state machine is waiting to observe.
    const s32 gameStatus = gpCardManager->mLastStatus;
    const s32 result = writeRecordLocked();
    gpCardManager->mLastStatus = gameStatus;
    OSUnlockMutex(&gpCardManager->mMutex);

    OSLockMutex(&sState->mutex);
    sState->completedStatus = result;
    sState->completed = ticket;
    OSUnlockMutex(&sState->mutex);
}

SusamuneCfg *lock() {
    if (sInitResult != INIT_READY) return nullptr;
    OSLockMutex(&sState->mutex);
    return &sState->cfg;
}

void unlock() {
    if (sInitResult == INIT_READY) OSUnlockMutex(&sState->mutex);
}

u32 commit() {
    if (sInitResult != INIT_READY) return 0;
    const u32 ticket = ++sState->requested;
    OSUnlockMutex(&sState->mutex);
    return ticket;
}

SaveResult poll(u32 ticket, u32 *error) {
    if (sInitResult != INIT_READY || ticket == 0) return SAVE_ERROR;
    OSLockMutex(&sState->mutex);
    const bool done = static_cast<s32>(sState->completed - ticket) >= 0;
    const s32 status = sState->completedStatus;
    OSUnlockMutex(&sState->mutex);
    if (!done) return SAVE_PENDING;
    if (status == CARD_ERROR_READY) return SAVE_OK;
    if (error) *error = static_cast<u32>(-status);
    return SAVE_ERROR;
}

bool needsInitialSave() {
    if (sInitResult != INIT_READY) return false;
    OSLockMutex(&sState->mutex);
    const bool needed = sState->initialSave;
    OSUnlockMutex(&sState->mutex);
    return needed;
}

u32 initError() { return sInitError; }

}  // namespace EmulatorPersistence

#endif  // IS_EMULATOR
