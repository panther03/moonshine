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
#include "susamune/layout_profile.h"

namespace EmulatorPersistence {
namespace {

constexpr u32 kRecordMagic = 0x53554346u;  // 'SUCF'
constexpr u16 kRecordVersion = 12;
constexpr u32 kCfgSizeV6 = 5144;
constexpr u32 kCfgSizeV7 = 5152;
constexpr u32 kRecordPayloadSizeV8 = sizeof(SusamuneCfg) + sizeof(SusamuneMarioColorsCfg);
constexpr u32 kRecordPayloadSizeV9 = kRecordPayloadSizeV8 + sizeof(SusamuneFluddColorsCfg);
constexpr u32 kRecordPayloadSizeV10 = kRecordPayloadSizeV9 + sizeof(SusamuneILEpisodesCfg);
constexpr u32 kRecordPayloadSizeV11 = kRecordPayloadSizeV10 + sizeof(SusamunePracticeDisplayStyleCfg);
constexpr u32 kRecordPayloadSize = kRecordPayloadSizeV11 + SUSAMUNE_CFG_SETTINGS_TAIL_SIZE;
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
    SusamuneMarioColorsCfg marioColors;
    SusamuneFluddColorsCfg fluddColors;
    SusamuneILEpisodesCfg ilEpisodes;
    SusamunePracticeDisplayStyleCfg practiceDisplays;
    u8 settingsTail[SUSAMUNE_CFG_SETTINGS_TAIL_SIZE];
    u8 padding[kSectorSize - 32 - kRecordPayloadSize];
};
static_assert(sizeof(Record) == kSectorSize, "card record must fill one sector");
static_assert(sizeof(SusamuneCfg) == kCfgSizeV7, "legacy card config prefix moved");
static_assert(__builtin_offsetof(Record, settingsTail) == 32 + kRecordPayloadSizeV11,
              "V12 settings must follow the complete unchanged V11 payload");

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
    SusamuneMarioColorsCfg marioColors;
    SusamuneFluddColorsCfg fluddColors;
    SusamuneILEpisodesCfg ilEpisodes;
    SusamunePracticeDisplayStyleCfg practiceDisplays;
    u8 settingsTail[SUSAMUNE_CFG_SETTINGS_TAIL_SIZE];
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
                 SUSAMUNE_CFG_FLAG_MOVEMENT_STYLE |
                 SUSAMUNE_CFG_FLAG_NATIVE_TIMER_STYLE |
                 SUSAMUNE_CFG_FLAG_MARIO_COLORS | SUSAMUNE_CFG_FLAG_FLUDD_COLORS |
                 SUSAMUNE_CFG_FLAG_IL_EPISODES | SUSAMUNE_CFG_FLAG_PRACTICE_DISPLAY_STYLE |
                 SUSAMUNE_CFG_FLAG_SETTINGS_TAIL;
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
                  SUSAMUNE_CFG_FLAG_MOVEMENT_STYLE |
                  SUSAMUNE_CFG_FLAG_NATIVE_TIMER_STYLE |
                  SUSAMUNE_CFG_FLAG_MARIO_COLORS | SUSAMUNE_CFG_FLAG_FLUDD_COLORS;
}

void initMarioColors(SusamuneMarioColorsCfg *colors) {
    memset(colors, 0, sizeof(*colors));
    colors->magic = SUSAMUNE_MARIO_COLORS_MAGIC;
    colors->version = SUSAMUNE_MARIO_COLORS_VERSION;
    memset(colors->rgb, 255, sizeof(colors->rgb));
}

void initFluddColors(SusamuneFluddColorsCfg *colors) {
    memset(colors, 0, sizeof(*colors));
    colors->magic = SUSAMUNE_FLUDD_COLORS_MAGIC;
    colors->version = SUSAMUNE_FLUDD_COLORS_VERSION;
    memset(colors->rgb, 255, sizeof(colors->rgb));
}

void publishMarioColors() {
    memcpy(SUSAMUNE_MARIO_COLORS_LIVE_PTR, &sState->marioColors,
           sizeof(sState->marioColors));
    DCStoreRange(SUSAMUNE_MARIO_COLORS_LIVE_PTR, sizeof(sState->marioColors));
}

void publishFluddColors() {
    memcpy(SUSAMUNE_FLUDD_COLORS_LIVE_PTR, &sState->fluddColors,
           sizeof(sState->fluddColors));
    DCStoreRange(SUSAMUNE_FLUDD_COLORS_LIVE_PTR, sizeof(sState->fluddColors));
}

void initILEpisodes(SusamuneILEpisodesCfg *choices) {
    memset(choices, 0, sizeof(*choices));
    choices->magic = SUSAMUNE_IL_EPISODE_MAGIC;
    choices->version = SUSAMUNE_IL_EPISODE_VERSION;
    choices->count = SUSAMUNE_IL_EPISODE_COUNT;
}

void publishILEpisodes() {
    memcpy(SUSAMUNE_IL_EPISODES_LIVE_PTR, &sState->ilEpisodes,
           sizeof(sState->ilEpisodes));
    DCStoreRange(SUSAMUNE_IL_EPISODES_LIVE_PTR, sizeof(sState->ilEpisodes));
}

void publishPracticeDisplays() {
    memcpy((void *)SUSAMUNE_CFG_SETTINGS_TAIL(&sState->cfg), sState->settingsTail, sizeof(sState->settingsTail));
    DCStoreRange((void *)SUSAMUNE_CFG_SETTINGS_TAIL(&sState->cfg), sizeof(sState->settingsTail));
    memcpy(SUSAMUNE_PRACTICE_DISPLAY_STYLE_LIVE_PTR, &sState->practiceDisplays,
           sizeof(sState->practiceDisplays));
    DCStoreRange(SUSAMUNE_PRACTICE_DISPLAY_STYLE_LIVE_PTR, sizeof(sState->practiceDisplays));
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
           record->payloadSize == kRecordPayloadSize &&
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

bool validV6(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 6 &&
           record->payloadSize == kCfgSizeV6 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV7(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 7 &&
           record->payloadSize == kCfgSizeV7 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV8(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 8 &&
           record->payloadSize == kRecordPayloadSizeV8 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV9(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 9 &&
           record->payloadSize == kRecordPayloadSizeV9 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV11(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 11 &&
           record->payloadSize == kRecordPayloadSizeV11 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

bool validV10(const Record *source) {
    Record *record = const_cast<Record *>(source);
    return record->magic == kRecordMagic && record->version == 10 &&
           record->payloadSize == kRecordPayloadSizeV10 &&
           record->gameVersion == SUSAMUNE_GAME_VERSION &&
           record->cfg.magic == SUSAMUNE_CFG_MAGIC &&
           record->cfg.version == SUSAMUNE_CFG_VERSION &&
           checksum(record) == record->checksum;
}

void migrateRecordV7(SusamuneCfg *cfg, SusamuneMarioColorsCfg *colors,
                     const SusamuneCfg *old) {
    memcpy(cfg, old, kCfgSizeV7);
    cfg->flags |= SUSAMUNE_CFG_FLAG_MARIO_COLORS | SUSAMUNE_CFG_FLAG_FLUDD_COLORS;
    initMarioColors(colors);
}

void migrateRecordV6(SusamuneCfg *cfg, const SusamuneCfg *old) {
    initBlank(cfg);
    memcpy(cfg, old, kCfgSizeV6);
    // V6 padding is checksum-covered but never an initialized style payload.
    cfg->flags |= SUSAMUNE_CFG_FLAG_NATIVE_TIMER_STYLE |
                  SUSAMUNE_CFG_FLAG_MARIO_COLORS | SUSAMUNE_CFG_FLAG_FLUDD_COLORS;
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
    record->payloadSize = kRecordPayloadSize;
    record->generation = generation;
    record->gameVersion = SUSAMUNE_GAME_VERSION;
    memcpy(&record->cfg, &sState->cfg, sizeof(sState->cfg));
    memcpy(&record->marioColors, &sState->marioColors, sizeof(sState->marioColors));
    memcpy(&record->fluddColors, &sState->fluddColors, sizeof(sState->fluddColors));
    memcpy(&record->ilEpisodes, &sState->ilEpisodes, sizeof(sState->ilEpisodes));
    memcpy(&record->practiceDisplays, &sState->practiceDisplays, sizeof(sState->practiceDisplays));
    memcpy(record->settingsTail, sState->settingsTail, sizeof(sState->settingsTail));
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

u32 layoutSlotLocked(MoonshineLayoutMailbox *mailbox, u32 slot, u32 operation) {
    char name[] = "moonshine_layout_1";
    name[sizeof(name) - 2] = (char)('1' + slot);
    CARDFileInfo file;
    bool created = false;
    s32 result = CARDOpen(CARD_SLOTB, name, &file);
    if (result == CARD_ERROR_NOFILE) {
        mailbox->presentMask &= ~(1u << slot);
        mailbox->badMask &= ~(1u << slot);
        mailbox->generations[slot] = 0;
        memset(mailbox->names[slot], 0, MOONSHINE_LAYOUT_NAME_SIZE);
        if (operation == MOONSHINE_LAYOUT_LIST) return 0;
        if (mailbox->expectedGeneration) return MOONSHINE_LAYOUT_ERROR_CHANGED;
        if (operation == MOONSHINE_LAYOUT_LOAD) return MOONSHINE_LAYOUT_ERROR_EMPTY;
        result = CARDCreate(CARD_SLOTB, name, kFileSize, &file);
        created = result == CARD_ERROR_READY;
    }
    if (result != CARD_ERROR_READY) return errorCode(result);
    CARDStat fileStatus;
    result = CARDGetStatus(CARD_SLOTB, file.mFileNo, &fileStatus);
    if (result != CARD_ERROR_READY || fileStatus.mLength != kFileSize) {
        CARDClose(&file);
        mailbox->badMask |= 1u << slot;
        return result == CARD_ERROR_READY ? MOONSHINE_LAYOUT_ERROR_INVALID : errorCode(result);
    }
    // CARDCreate publishes recycled blocks before any file data is written.
    // The directory's comment address commits initialization independently.
    if (fileStatus.mCommentAddr != kSectorSize - 64u) {
        mailbox->presentMask &= ~(1u << slot);
        mailbox->badMask |= 1u << slot;
        mailbox->generations[slot] = 0;
        memset(mailbox->names[slot], 0, MOONSHINE_LAYOUT_NAME_SIZE);
        if (operation != MOONSHINE_LAYOUT_SAVE || mailbox->expectedGeneration) {
            const s32 closed = CARDClose(&file);
            if (closed != CARD_ERROR_READY) return errorCode(closed);
            return mailbox->expectedGeneration ? MOONSHINE_LAYOUT_ERROR_CHANGED :
                operation == MOONSHINE_LAYOUT_LIST ? 0u : MOONSHINE_LAYOUT_ERROR_INVALID;
        }
        created = true;
    }
    u8 *sector = reinterpret_cast<u8 *>(gpCardManager->mCARDBlock);
    MoonshineLayoutFile *record = reinterpret_cast<MoonshineLayoutFile *>(sector);
    static_assert(sizeof(*record) <= kSectorSize, "layout must fit a CARD sector");
    u32 generation = 0, best = 1;
    char bestName[MOONSHINE_LAYOUT_NAME_SIZE] = {};
    bool invalid = false;
    for (u32 copy = 0; !created && copy < 2; ++copy) {
        result = CARDRead(&file, sector, kSectorSize, copy * kSectorSize);
        if (result != CARD_ERROR_READY) break;
        if (!MoonshineLayoutValid(record)) { invalid = true; continue; }
        if (!generation || newer(record->generation, generation)) {
            generation = record->generation;
            best = copy;
            memcpy(bestName, record->name, sizeof(bestName));
        }
    }
    if (created) {
        memset(sector, 0, kSectorSize);
        result = CARDWrite(&file, sector, kSectorSize, kSectorSize);
        if (result == CARD_ERROR_READY)
            result = CARDRead(&file, sector, kSectorSize, kSectorSize);
        if (result == CARD_ERROR_READY && MoonshineLayoutValid(record))
            result = CARD_ERROR_IOERROR;
    }
    const s32 closed = CARDClose(&file);
    if (result == CARD_ERROR_READY) result = closed;
    if (result != CARD_ERROR_READY) {
        mailbox->badMask |= 1u << slot;
        return errorCode(result);
    }
    mailbox->presentMask &= ~(1u << slot);
    mailbox->badMask &= ~(1u << slot);
    if (generation) mailbox->presentMask |= 1u << slot;
    else if (invalid) mailbox->badMask |= 1u << slot;
    mailbox->generations[slot] = generation;
    memcpy(mailbox->names[slot], bestName, sizeof(bestName));
    if (operation == MOONSHINE_LAYOUT_LIST) return 0;
    if (generation != mailbox->expectedGeneration) return MOONSHINE_LAYOUT_ERROR_CHANGED;
    if (operation == MOONSHINE_LAYOUT_LOAD && !generation)
        return invalid ? MOONSHINE_LAYOUT_ERROR_INVALID : MOONSHINE_LAYOUT_ERROR_EMPTY;

    u32 expectedChecksum = 0;
    if (operation == MOONSHINE_LAYOUT_SAVE) {
        memset(sector, 0, kSectorSize);
        memcpy(record, &mailbox->file, sizeof(*record));
        u32 next = generation + 1;
        if (!next) next = 1;
        if (record->version != MOONSHINE_LAYOUT_VERSION || record->bytes != sizeof(*record) ||
            !MoonshineLayoutValid(record) || record->generation != next)
            return MOONSHINE_LAYOUT_ERROR_INVALID;
        generation = next;
        expectedChecksum = record->checksum;
        best ^= 1u;
    }
    result = CARDOpen(CARD_SLOTB, name, &file);
    if (result != CARD_ERROR_READY) return errorCode(result);
    if (operation == MOONSHINE_LAYOUT_SAVE)
        result = CARDWrite(&file, sector, kSectorSize, best * kSectorSize);
    else result = CARDRead(&file, sector, kSectorSize, best * kSectorSize);
    s32 closeResult = CARDClose(&file);
    if (result == CARD_ERROR_READY) result = closeResult;
    if (result != CARD_ERROR_READY) return errorCode(result);
    if (operation == MOONSHINE_LAYOUT_SAVE) {
        result = CARDOpen(CARD_SLOTB, name, &file);
        if (result != CARD_ERROR_READY) return errorCode(result);
        result = CARDRead(&file, sector, kSectorSize, best * kSectorSize);
        closeResult = CARDClose(&file);
        if (result == CARD_ERROR_READY) result = closeResult;
        if (result != CARD_ERROR_READY) return errorCode(result);
    }
    if (!MoonshineLayoutValid(record)) return MOONSHINE_LAYOUT_ERROR_INVALID;
    if (record->generation != generation || (expectedChecksum && record->checksum != expectedChecksum))
        return MOONSHINE_LAYOUT_ERROR_CHANGED;
    if (created) {
        fileStatus.mCommentAddr = kSectorSize - 64u;
        result = CARDSetStatus(CARD_SLOTB, file.mFileNo, &fileStatus);
        if (result != CARD_ERROR_READY) return errorCode(result);
    }
    MoonshineLayoutUpgrade(record);
    memcpy(&mailbox->file, record, sizeof(*record));
    mailbox->presentMask |= 1u << slot;
    mailbox->badMask &= ~(1u << slot);
    mailbox->generations[slot] = generation;
    memcpy(mailbox->names[slot], record->name, MOONSHINE_LAYOUT_NAME_SIZE);
    return 0;
}

void layoutProfilesLocked() {
    MoonshineLayoutMailbox *mailbox = MOONSHINE_LAYOUT_PPC_PTR;
    const u32 operation = mailbox->operation;
    u32 status = 0;
    if (mailbox->magic != MOONSHINE_LAYOUT_MAILBOX_MAGIC ||
        mailbox->version != MOONSHINE_LAYOUT_MAILBOX_VERSION ||
        operation < MOONSHINE_LAYOUT_LIST || operation > MOONSHINE_LAYOUT_LOAD ||
        mailbox->reservedControl[0] || mailbox->reservedControl[1] ||
        (operation != MOONSHINE_LAYOUT_LIST && mailbox->slot >= MOONSHINE_LAYOUT_COUNT)) {
        status = MOONSHINE_LAYOUT_ERROR_INVALID;
    } else if (operation != MOONSHINE_LAYOUT_LIST &&
               mailbox->expectedGeneration != mailbox->generations[mailbox->slot]) {
        status = MOONSHINE_LAYOUT_ERROR_CHANGED;
    } else if (operation == MOONSHINE_LAYOUT_SAVE &&
        (mailbox->file.version != MOONSHINE_LAYOUT_VERSION || mailbox->file.bytes != sizeof(mailbox->file) ||
         !MoonshineLayoutValid(&mailbox->file) || mailbox->file.generation !=
         (mailbox->expectedGeneration == 0xffffffffu ? 1u : mailbox->expectedGeneration + 1u))) {
        status = MOONSHINE_LAYOUT_ERROR_INVALID;
    } else {
        gpCardManager->unmount();
        const s32 mounted = mount(gpCardManager->mCardWorkArea);
        if (mounted != CARD_ERROR_READY) status = errorCode(mounted);
        else {
            if (operation == MOONSHINE_LAYOUT_LIST) {
                for (u32 slot = 0; slot < MOONSHINE_LAYOUT_COUNT && !status; ++slot)
                    status = layoutSlotLocked(mailbox, slot, operation);
            } else status = layoutSlotLocked(mailbox, mailbox->slot, operation);
            unmount();
        }
    }
    mailbox->status = status;
    mailbox->ackSeq = mailbox->requestSeq;
}

void initState() {
    sState = reinterpret_cast<State *>(SUSAMUNE_DOLPHIN_PERSIST_PPC_BASE);
    memset(sState, 0, sizeof(*sState));
    sState->activeRecord = -1;
    OSInitMutex(&sState->mutex);
    initBlank(&sState->cfg);
    initMarioColors(&sState->marioColors);
    initFluddColors(&sState->fluddColors);
    initILEpisodes(&sState->ilEpisodes);
    SusamunePracticeDisplayStyleInit(&sState->practiceDisplays);
    memset(sState->settingsTail, SUSAMUNE_CFG_UNSET, sizeof(sState->settingsTail));
    publishMarioColors();
    publishFluddColors();
    publishILEpisodes();
    publishPracticeDisplays();
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
        const bool v11 = !current && validV11(record);
        const bool v10 = !current && validV10(record);
        const bool v9 = !current && validV9(record);
        const bool v8 = !current && validV8(record);
        const bool v7 = !current && !v8 && validV7(record);
        const bool v6 = !current && !v7 && validV6(record);
        const bool v5 = !current && !v7 && !v6 && validV5(record);
        const bool v4 = !current && !v5 && validV4(record);
        const bool v3 = !current && !v5 && !v4 && validV3(record);
        const bool v2 = !current && !v5 && !v4 && !v3 && validV2(record);
        const bool v1 =
            !current && !v5 && !v4 && !v3 && !v2 && validV1(record);
        if ((current || v11 || v10 || v9 || v8 || v7 || v6 || v5 || v4 || v3 || v2 || v1) &&
            (!haveRecord || newer(record->generation, bestGeneration))) {
            initMarioColors(&sState->marioColors);
            initFluddColors(&sState->fluddColors);
            initILEpisodes(&sState->ilEpisodes);
            if (current || v11 || v10 || v9 || v8) {
                memcpy(&sState->cfg, &record->cfg, sizeof(sState->cfg));
                memcpy(&sState->marioColors, &record->marioColors,
                       sizeof(sState->marioColors));
                if (current || v11 || v10 || v9) memcpy(&sState->fluddColors, &record->fluddColors, sizeof(sState->fluddColors));
                if (current || v11 || v10) memcpy(&sState->ilEpisodes, &record->ilEpisodes, sizeof(sState->ilEpisodes));
            } else if (v7) {
                migrateRecordV7(&sState->cfg, &sState->marioColors, &record->cfg);
            } else if (v6) {
                migrateRecordV6(&sState->cfg, &record->cfg);
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
            if (current || v11) memcpy(&sState->practiceDisplays, &record->practiceDisplays,
                                sizeof(sState->practiceDisplays));
            else SusamunePracticeDisplayStyleFromWallkick(&sState->practiceDisplays, &sState->cfg.wallkickStyle);
            sState->cfg.flags |= SUSAMUNE_CFG_FLAG_FLUDD_COLORS | SUSAMUNE_CFG_FLAG_IL_EPISODES |
                                SUSAMUNE_CFG_FLAG_PRACTICE_DISPLAY_STYLE;
            if (current) memcpy(sState->settingsTail, record->settingsTail, sizeof(sState->settingsTail));
            else memset(sState->settingsTail, SUSAMUNE_CFG_UNSET, sizeof(sState->settingsTail));
            sState->cfg.flags |= SUSAMUNE_CFG_FLAG_SETTINGS_TAIL;
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
    memset(MOONSHINE_LAYOUT_PPC_PTR, 0, sizeof(MoonshineLayoutMailbox));
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

    publishMarioColors();
    publishFluddColors();
    publishILEpisodes();
    publishPracticeDisplays();
    MoonshineLayoutMailbox *layout = MOONSHINE_LAYOUT_PPC_PTR;
    memset(layout, 0, sizeof(*layout));
    layout->magic = MOONSHINE_LAYOUT_MAILBOX_MAGIC;
    layout->version = MOONSHINE_LAYOUT_MAILBOX_VERSION;
    sState->cfg.flags |= MOONSHINE_LAYOUT_CFG_FLAG;
    sInitResult = INIT_READY;
    return sInitResult;
}

void service() {
    if (sInitResult != INIT_READY) return;

    OSLockMutex(&sState->mutex);
    const u32 ticket = sState->requested;
    const bool pending = ticket != sState->completed;
    OSUnlockMutex(&sState->mutex);
    const MoonshineLayoutMailbox *layout = MOONSHINE_LAYOUT_PPC_PTR;
    const bool layoutPending = layout->requestSeq != layout->ackSeq;
    if (!pending && !layoutPending) {
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
    s32 result = CARD_ERROR_READY;
    if (pending) result = writeRecordLocked();
    else layoutProfilesLocked();
    gpCardManager->mLastStatus = gameStatus;
    OSUnlockMutex(&gpCardManager->mMutex);

    if (pending) {
        OSLockMutex(&sState->mutex);
        sState->completedStatus = result;
        sState->completed = ticket;
        OSUnlockMutex(&sState->mutex);
    }
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
    memcpy(&sState->marioColors, SUSAMUNE_MARIO_COLORS_LIVE_PTR,
           sizeof(sState->marioColors));
    memcpy(&sState->fluddColors, SUSAMUNE_FLUDD_COLORS_LIVE_PTR,
           sizeof(sState->fluddColors));
    memcpy(&sState->ilEpisodes, SUSAMUNE_IL_EPISODES_LIVE_PTR,
           sizeof(sState->ilEpisodes));
    memcpy(sState->settingsTail, (const void *)SUSAMUNE_CFG_SETTINGS_TAIL(&sState->cfg), sizeof(sState->settingsTail));
    memcpy(&sState->practiceDisplays, SUSAMUNE_PRACTICE_DISPLAY_STYLE_LIVE_PTR,
           sizeof(sState->practiceDisplays));
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
