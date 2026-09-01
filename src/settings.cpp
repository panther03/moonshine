// =====================================================================
// settings.cpp
//
// The setting descriptor table and the tiny value store behind it. See
// settings.hxx for the design; features read settings through gSettings
// (e.g. savestate.cpp gates the RNG-seed snapshot on
// SETTING_SAVE_RNG_STATE) and menu.cpp renders/edits them generically off
// kSettingDescs.
// =====================================================================

#include "susamune/settings.hxx"

#include "Dolphin/mem.h"
#include "Dolphin/OS.h"  // DCInvalidateRange, DCStoreRange
#include "susamune/binds.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/input_display.hxx"
#include "susamune/metadata_display.hxx"
#include "susamune/mem2_map.h"
#include "susamune/qft_display.hxx"
#include "susamune/packed_text.hxx"
#include "susamune/susamune_cfg.h"

namespace {

enum ChoiceSet {
    CHOICES_BOOL,
    CHOICES_FLUDD,
    CHOICES_NOZZLE,
    CHOICES_SUNSHINE_TIMER,
    CHOICES_QFT_VISIBILITY,
    CHOICES_FREEZE_DURATION,
    CHOICES_BOX_GAME,
    CHOICES_PIANTISSIMO,
    CHOICES_GHOST_OPACITY,
    CHOICES_APPEARANCE,
    CHOICES_GHOST_APPEARANCE,
    CHOICES_STAGE_SESSION,
    CHOICES_PB_GHOST_SAVE,
    CHOICES_PETEY_ROUTE,
    CHOICES_RICCO_CRANE_SPEED,
    CHOICES_RICCO_FRUIT_MACHINE,
    CHOICES_GELATO_PATTERN,
    CHOICES_COUNT,
};

struct SettingDesc {
    u8 choices;
    u8 config;
};

const u8 kCategoryMask = 7;
const u8 kDefaultShift = 3;

#define SBOOL(name, def, cat) name "\0"
#define SCHOICE(name, def, choices, cat) name "\0"
const char kSettingNames[] =
#include "settings_descs.inc"
    ;
#undef SBOOL
#undef SCHOICE

const char kChoiceLabels[] =
    "Off\0On\0Completed\0No FLUDD\0All secrets\0Unlocked\0Rocket\0Turbo\0Hover\0"
    "Always\0Shine only\0Hidden\0On freeze\0"
    "0.5 s\0" "1 s\0" "2 s\0" "3 s\0" "5 s\0" "1\0" "2\0"
    "Slowest\0Fastest\0"
    "25 pct\0" "50 pct\0" "75 pct\0" "100 pct\0Default\0Never\0"
    "Shadow Mario\0Piantissimo\0Full notification\0Counter\0"
    "Ask\0Auto-Save\0Don't ask\0Retail\0N1-S1-S2-S3\0Slow\0Medium\0Fast\0"
    "Durians only\0"
    "Pattern 1\0Pattern 2\0Pattern 3\0Pattern 4";

const u8 kChoiceMap[] = {
    0, 1,              // bool
    2, 3, 4,           // FLUDD
    5, 6, 7, 8,        // nozzle
    9, 10, 11,         // Sunshine timer
    9, 12, 11,         // QFT visibility
    0, 13, 14, 15, 16, 17,  // freeze duration
    0, 18, 19,          // box game
    0, 20, 21,          // Piantissimo pattern
    22, 23, 24, 25,     // ghost opacity
    26, 9, 27,          // appearance: Default, Always, Never
    28, 29,             // ghost appearance
    30, 31, 0,          // stage session: Full notification, Counter, Off
    32, 33, 34,         // PB ghost save
    35, 36,              // Petey route
    35, 20, 37, 38, 39, 21,  // crane speed band
    35, 40,              // Ricco fruit machine
    35, 41, 42, 43, 44,  // course pattern
};
const u8 kChoiceFirst[CHOICES_COUNT + 1] = {
    0, 2, 5, 9, 12, 15, 21, 24, 27, 31, 34, 36, 39, 42, 44, 50, 52, 57
};

static_assert(sizeof(kChoiceMap) / sizeof(kChoiceMap[0]) == 57,
              "choice map size changed");
static_assert(SETTING_HELMET_APPEARANCE == SETTING_GHOST_OPACITY + 1 &&
                  SETTING_CAP_APPEARANCE == SETTING_HELMET_APPEARANCE + 1 &&
                  SETTING_SHADES_APPEARANCE == SETTING_CAP_APPEARANCE + 1 &&
                  SETTING_SHINE_SHIRT_APPEARANCE ==
                      SETTING_SHADES_APPEARANCE + 1 &&
                  SETTING_METADATA_HORIZONTAL ==
                      SETTING_SHINE_SHIRT_APPEARANCE + 1 &&
                  SETTING_DISABLE_RETAIL_PAUSE ==
                      SETTING_METADATA_HORIZONTAL + 1 &&
                  SETTING_GHOST_APPEARANCE ==
                      SETTING_DISABLE_RETAIL_PAUSE + 1 &&
                  SETTING_GHOST_LAST_SUCCESS ==
                      SETTING_GHOST_APPEARANCE + 1 &&
                  SETTING_STAGE_SESSION_DISPLAY ==
                      SETTING_GHOST_LAST_SUCCESS + 1 &&
                  SETTING_PB_GHOST_SAVE_POLICY ==
                      SETTING_STAGE_SESSION_DISPLAY + 1,
              "append-only V2 setting ids moved");

u8 choiceCount(const SettingDesc &desc) {
    return kChoiceFirst[desc.choices + 1] - kChoiceFirst[desc.choices];
}

u8 defaultValue(const SettingDesc &desc) { return desc.config >> kDefaultShift; }

SettingCategory settingCategory(const SettingDesc &desc) {
    return (SettingCategory)(desc.config & kCategoryMask);
}

int rngFavoriteBit(SettingId id) {
    if (id >= SETTING_KING_BOO_ALWAYS_FRUIT &&
        id <= SETTING_RICCO_FRUIT_MACHINE) {
        return id - SETTING_KING_BOO_ALWAYS_FRUIT;
    }
    if (id >= SETTING_GELATO_RED_COIN_FISH_PATTERN &&
        id <= SETTING_GELATO_BLUE_BIRD_PATTERN) {
        return 5 + id - SETTING_GELATO_RED_COIN_FISH_PATTERN;
    }
    return -1;
}

static_assert(SETTING_RICCO_FRUIT_MACHINE -
                      SETTING_KING_BOO_ALWAYS_FRUIT ==
                  4 &&
                  SETTING_GELATO_BLUE_BIRD_PATTERN -
                          SETTING_GELATO_RED_COIN_FISH_PATTERN ==
                      1,
              "RNG Shined bit mapping changed");

#define SETTING_CONFIG(def, cat) (u8)(((def) << kDefaultShift) | (cat))
#define SBOOL(name, def, cat) { CHOICES_BOOL, SETTING_CONFIG(def, cat) },
#define SCHOICE(name, def, choices, cat) { choices, SETTING_CONFIG(def, cat) },

// Descriptor table, indexed by SettingId. One row per setting.
const SettingDesc kSettingDescs[] = {
#include "settings_descs.inc"
};
#undef SBOOL
#undef SCHOICE
#undef SETTING_CONFIG

// How long to wait for the kernel to acknowledge a save before giving up.
// Generous: the kernel only services the doorbell between disc reads, and a
// FatFS write of a few hundred bytes can wait behind one.
const u32 kSaveTimeoutFrames = 300;  // ~5s at 60Hz

}  // namespace

Settings &gSettings = *reinterpret_cast<Settings *>(
    SUSAMUNE_MEM2_CONFIG_RUNTIME_PPC_BASE +
    SUSAMUNE_CONFIG_SETTINGS_OFFSET);
static_assert(sizeof(Settings) <= SUSAMUNE_CONFIG_SETTINGS_SIZE,
              "settings exceed their MEM2 runtime slot");

void Settings::resetDefaults() {
    // Binds ride along on the same handoff block and the same doorbell, so
    // they are reset, loaded and staged wherever settings are. Their table and
    // recorder live in binds.* -- see binds.hxx.
    gBinds.resetDefaults();
    gInputDisplay.resetDefaults();
    gMetadataDisplay.resetDefaults();
    gQftDisplay.resetDefaults();
    gCreationExtras.resetDefaults();

    for (int i = 0; i < SETTING_COUNT; i++) {
        mValues[i] = defaultValue(kSettingDescs[i]);
    }
    mDirty          = false;
    mSaveState      = SETTINGS_SAVE_IDLE;
    mLastError      = 0;
    mSaveSeq        = 0;
    mSaveWaitFrames = 0;
}

// ---------------------------------------------------------------------
// Persistence
//
// On console the Nintendont ARM kernel has already parsed susamune.ini into
// the MEM2 handoff block by the time the game boots (see SusamuneCfg.c).
// Dolphin's implementation lives in settings_emulator.cpp and uses slot B.
// ---------------------------------------------------------------------

#if !IS_EMULATOR

void Settings::init() {
    resetDefaults();

    // The kernel wrote this block from the ARM side before the game booted;
    // drop anything our cache may have speculatively pulled in first.
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    DCInvalidateRange((void *)cfg, sizeof(SusamuneCfg));

    if (cfg->magic != SUSAMUNE_CFG_MAGIC || cfg->version != SUSAMUNE_CFG_VERSION) {
        // No launcher, a stock Nintendont, or a build mismatch. Defaults stand
        // and save() will still work if a compatible kernel is listening --
        // but not against a block we could not identify, so leave it alone.
        mSaveState = SETTINGS_SAVE_UNSUPPORTED;
        return;
    }

    adopt(cfg);
    mSaveSeq = cfg->saveSeq;

    // First boot of this game version on this SD card: the kernel found no
    // sections for it and cannot author defaults, because those live here
    // rather than in the launcher.
    if (cfg->flags & SUSAMUNE_CFG_FLAG_NO_CONFIG) {
        save();
    }
}

bool Settings::finishInit() { return true; }

void Settings::save() {
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;

    if (mSaveState == SETTINGS_SAVE_PENDING) return;

    if (mSaveState == SETTINGS_SAVE_UNSUPPORTED) {
        mDirty = false;
        gBinds.clearDirty();
        gInputDisplay.clearDirty();
        gMetadataDisplay.clearDirty();
        gQftDisplay.clearDirty();
        gCreationExtras.clearDirty();
        return;
    }

    stageInto(cfg);

    // Publish the payload before the doorbell, so the kernel can never see a
    // bumped saveSeq alongside a half-written values[]/binds[]. Both live in
    // the same mod-owned run of cache lines starting at values[].
    DCStoreRange((void *)cfg->values,
                 sizeof(cfg->values) + sizeof(cfg->binds) + sizeof(cfg->inputDisplay) +
                 sizeof(cfg->metadataDisplay));
    DCStoreRange((void *)&cfg->qftDisplay,
                 sizeof(cfg->qftDisplay) + sizeof(cfg->metadataStyle) +
                 sizeof(cfg->inputStyle) + sizeof(cfg->creation) +
                 sizeof(cfg->wallkickStyle));
    DCStoreRange((void *)&cfg->movementStyle, sizeof(cfg->movementStyle));

    mSaveSeq     = cfg->saveSeq + 1;
    cfg->saveSeq = mSaveSeq;
    DCStoreRange((void *)cfg, 32);  // line 0: magic/version/count/saveSeq

    mDirty          = false;
    mLastError      = 0;
    mSaveWaitFrames = 0;
    mSaveState      = SETTINGS_SAVE_PENDING;
}

SettingsSaveState Settings::pollSave() {
    if (mSaveState != SETTINGS_SAVE_PENDING) {
        return (SettingsSaveState)mSaveState;
    }

    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    // Line 1 is written exclusively by the kernel, so invalidating it can
    // never discard a pending write of ours (see the ownership note in
    // susamune_cfg.h).
    DCInvalidateRange((void *)&cfg->ackSeq, 32);

    if (cfg->ackSeq == mSaveSeq) {
        mLastError = cfg->status;
        mSaveState = mLastError ? SETTINGS_SAVE_ERROR : SETTINGS_SAVE_OK;
    } else if (++mSaveWaitFrames > kSaveTimeoutFrames) {
        mSaveState = SETTINGS_SAVE_TIMEOUT;
    }
    return (SettingsSaveState)mSaveState;
}

#endif  // !IS_EMULATOR

void Settings::adopt(const volatile SusamuneCfg *cfg) {
    // Copy only what the writer actually filled in. An older backend carries
    // fewer settings; the remainder keep their compiled-in defaults.
    u16 n = cfg->count;
    if (n > SETTING_COUNT) {
        n = SETTING_COUNT;
    }
    for (u16 i = 0; i < n; i++) {
        u8 v = cfg->values[i];
        if (v == SUSAMUNE_CFG_UNSET) {
            continue;  // absent from the ini -- keep the default
        }
        // Route through set() so an out-of-range value in a hand-edited ini
        // is clamped into the setting's choice range rather than trusted.
        set((SettingId)i, v);
    }

    // Binds, same deal. bindCount is 0 on a launcher built before binds
    // existed (it zeroes only the part of the block it knows), which leaves
    // every compiled-in default in place.
    u16 nb = cfg->bindCount;
    if (nb > BIND_COUNT) {
        nb = BIND_COUNT;
    }
    for (u16 i = 0; i < nb; i++) {
        u16 m = cfg->binds[i];
        if (m != SUSAMUNE_CFG_BIND_UNSET) {
            gBinds.adopt(m, (BindId)i);
        }
    }
    gBinds.clearDirty();
    // An older kernel leaves the bytes beyond binds[] untouched. Its missing
    // capability flag keeps us from interpreting that stale MEM2 as config.
    if (cfg->flags & SUSAMUNE_CFG_FLAG_INPUT_DISPLAY) {
        gInputDisplay.adopt(&cfg->inputDisplay);
    }
    if (cfg->flags & SUSAMUNE_CFG_FLAG_INPUT_STYLE) {
        gInputDisplay.adoptStyle(&cfg->inputStyle);
    }
    if (cfg->flags & SUSAMUNE_CFG_FLAG_METADATA_DISPLAY) {
        gMetadataDisplay.adopt(&cfg->metadataDisplay);
    }
    if (cfg->flags & SUSAMUNE_CFG_FLAG_METADATA_STYLE) {
        gMetadataDisplay.adoptStyle(&cfg->metadataStyle);
    }
    if (cfg->flags & SUSAMUNE_CFG_FLAG_QFT_DISPLAY) {
        gQftDisplay.adopt(&cfg->qftDisplay);
    }
    if (cfg->flags & SUSAMUNE_CFG_FLAG_CREATION) {
        gCreationExtras.adopt(&cfg->creation);
    }
    if (cfg->flags & SUSAMUNE_CFG_FLAG_WALLKICK_STYLE) {
        gCreationExtras.adoptWallkick(&cfg->wallkickStyle);
    }
    if (cfg->flags & SUSAMUNE_CFG_FLAG_MOVEMENT_STYLE) {
        gCreationExtras.adoptMovement(&cfg->movementStyle);
    }

    // set() marks dirty; adopting persisted values is not a user edit.
    mDirty     = false;
    mSaveState = SETTINGS_SAVE_IDLE;
}

void Settings::stageInto(volatile SusamuneCfg *cfg) {
    memcpy((void *)cfg->values, mValues, sizeof(mValues));
    cfg->count = SETTING_COUNT;

    gBinds.stageInto(cfg->binds);
    cfg->bindCount = BIND_COUNT;
    gBinds.clearDirty();

    gInputDisplay.stageInto(&cfg->inputDisplay);
    gInputDisplay.stageStyleInto(&cfg->inputStyle);
    gInputDisplay.clearDirty();
    gMetadataDisplay.stageInto(&cfg->metadataDisplay);
    gMetadataDisplay.stageStyleInto(&cfg->metadataStyle);
    gMetadataDisplay.clearDirty();
    gQftDisplay.stageInto(&cfg->qftDisplay);
    gQftDisplay.clearDirty();
    gCreationExtras.stageInto(&cfg->creation);
    gCreationExtras.stageWallkickInto(&cfg->wallkickStyle);
    gCreationExtras.stageMovementInto(&cfg->movementStyle);
    gCreationExtras.clearDirty();
}

void Settings::set(SettingId id, u8 value) {
    if ((id >= SETTING_FAVORITES_0 && id <= SETTING_FAVORITES_10) ||
        id == SETTING_RNG_FAVORITES) {
        value &= 0x7F;
    } else {
        value = value % choiceCount(kSettingDescs[id]);
    }
    if (mValues[id] != value) {
        mDirty = true;
    }
    mValues[id] = value;
}

bool Settings::favoriteable(SettingId id) {
    return (id >= 0 && id < SETTING_FAVORITES_0) ||
           rngFavoriteBit(id) >= 0;
}

bool Settings::favorite(SettingId id) const {
    if (id >= 0 && id < SETTING_FAVORITES_0) {
        const int index = (int)id;
        const SettingId storage =
            (SettingId)(SETTING_FAVORITES_0 + index / 7);
        return (mValues[storage] & (1u << (index % 7))) != 0;
    }
    const int bit = rngFavoriteBit(id);
    return bit >= 0 &&
           (mValues[SETTING_RNG_FAVORITES] & (1u << bit)) != 0;
}

void Settings::toggleFavorite(SettingId id) {
    SettingId storage;
    int bit;
    if (id >= 0 && id < SETTING_FAVORITES_0) {
        const int index = (int)id;
        storage = (SettingId)(SETTING_FAVORITES_0 + index / 7);
        bit = index % 7;
    } else {
        bit = rngFavoriteBit(id);
        if (bit < 0) return;
        storage = SETTING_RNG_FAVORITES;
    }
    mValues[storage] ^= (u8)(1u << bit);
    mDirty = true;
}

void Settings::cycle(SettingId id, int dir) {
    int n = choiceCount(kSettingDescs[id]);
    int v = (int)mValues[id] + dir;
    // Wrap into [0, n). dir is +/-1, so one add/sub suffices.
    if (v < 0) {
        v += n;
    } else if (v >= n) {
        v -= n;
    }
    if (mValues[id] != (u8)v) {
        mDirty = true;
    }
    mValues[id] = (u8)v;
}

const char *Settings::valueLabel(SettingId id) const {
    const SettingDesc &d = kSettingDescs[id];
    u8 index = kChoiceFirst[d.choices] + mValues[id] % choiceCount(d);
    return PackedText::at(kChoiceLabels, kChoiceMap[index]);
}

const char *Settings::name(SettingId id) {
    return PackedText::at(kSettingNames, (int)id);
}

SettingCategory Settings::category(SettingId id) {
    return settingCategory(kSettingDescs[id]);
}

// kSettingDescs is indexed by SettingId and must stay row-for-row aligned with
// SUSAMUNE_SETTING_LIST. Adding a list row without a descriptor row (or vice
// versa) fails here rather than silently shifting every setting's meaning.
static_assert(sizeof(kSettingDescs) / sizeof(kSettingDescs[0]) == SETTING_COUNT,
              "kSettingDescs must have one row per SUSAMUNE_SETTING_LIST entry");
static_assert(SETTING_CAT_COUNT <= kCategoryMask + 1,
              "SettingCategory no longer fits packed descriptor");
static_assert(SETTING_COUNT <= SUSAMUNE_CFG_MAX_SETTINGS,
              "SETTING_COUNT exceeds the MEM2 handoff block's values[] capacity");
static_assert(BIND_COUNT <= SUSAMUNE_CFG_MAX_BINDS,
              "BIND_COUNT exceeds the MEM2 handoff block's binds[] capacity");
