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

#include "Dolphin/OS.h"  // DCInvalidateRange, DCStoreRange
#include "susamune/binds.hxx"
#include "susamune/susamune_cfg.h"

namespace {

// CHOICE labels. Index 0 is the default / "restore original" state (see the
// choice-feature apply in features.cpp), so it must be the game's normal
// behaviour for the corresponding gecko code.
const char *const kFluddLabels[3]  = { "Completed", "No FLUDD", "All secrets" };
const char *const kNozzleLabels[4] = { "Unlocked", "Rocket", "Turbo", "Hover" };

#define SBOOL(name, def, cat) { name, nullptr, 2, def, cat }
#define SCHOICE(name, def, labels, cat) \
    { name, labels, (u8)(sizeof(labels) / sizeof((labels)[0])), def, cat }

// Descriptor table, indexed by SettingId. One row per setting.
const SettingDesc kSettingDescs[SETTING_COUNT] = {
    // Quality-of-life toggles. Defaults mirror the GCT generator's Standard
    // preset where it has an equivalent code.
    SBOOL("Fast text", 0, SETTING_CAT_QOL),
    SCHOICE("FLUDD in secrets", 0, kFluddLabels, SETTING_CAT_QOL),
    // Area lock: every departure restarts the area being left instead.
    SBOOL("Area lock", 0, SETTING_CAT_QOL),
    SBOOL("Infinite lives", 1, SETTING_CAT_QOL),
    SBOOL("Disable blue coin flag", 1, SETTING_CAT_QOL),
    SBOOL("FMV skips", 1, SETTING_CAT_QOL),
    SBOOL("Unlock Yoshi", 0, SETTING_CAT_QOL),
    SBOOL("Unlock nozzles", 0, SETTING_CAT_QOL),
    SBOOL("Free pause", 1, SETTING_CAT_QOL),
    SBOOL("Exit area everywhere", 1, SETTING_CAT_QOL),
    SBOOL("Any fruit opens Yoshi eggs", 0, SETTING_CAT_QOL),
    // Intro skip: On, as in the generator's Standard preset. It also removes
    // the boot prompt that picks progressive / 50-60Hz mode, so that has to be
    // set with this off.
    SBOOL("Infinite juice", 0, SETTING_CAT_QOL),
    SBOOL("Intro skip", 1, SETTING_CAT_QOL),
    SBOOL("Respawn one-time shines", 1, SETTING_CAT_QOL),
    SBOOL("Fast Piantissimo", 0, SETTING_CAT_QOL),

    // SETTING_SAVE_RNG_STATE: when On (default), a savestate load restores the
    // libc RNG seed so the RNG stream rewinds with the state -- the game's
    // historical behaviour. Off leaves the seed advancing across a load, so a
    // runner can practise varied RNG outcomes from the same snapshot.
    SBOOL("Save RNG state", 1, SETTING_CAT_SAVESTATE),

    // Misc toggles.
    SCHOICE("Nozzle lock", 0, kNozzleLabels, SETTING_CAT_MISC),
    SBOOL("Force plaza events", 1, SETTING_CAT_MISC),
    SBOOL("Never pause IGT", 1, SETTING_CAT_MISC),
    SBOOL("Shadow Mario HP meter", 0, SETTING_CAT_MISC),
    // Stage Intro Skip is an asm-hook feature (features.cpp) rather than a
    // plain patch; No Shine Get Animation is a one-word patch in the same file.
    SBOOL("Stage intro skip", 0, SETTING_CAT_MISC),
    SBOOL("Deathless blooper surfing", 0, SETTING_CAT_MISC),
    SBOOL("No shine get animation", 0, SETTING_CAT_MISC),
    SBOOL("Fruit never times out", 0, SETTING_CAT_MISC),
    // Disable Z Menu defaults on: Z is the warp wheel's bind.
    SBOOL("Disable Z Menu", 1, SETTING_CAT_MISC),
    SBOOL("Disable Susamune Warps", 0, SETTING_CAT_MISC),

    // Cosmetic toggles.
    SBOOL("Mute background music", 0, SETTING_CAT_COSMETIC),
    SBOOL("Episode names as IDs", 0, SETTING_CAT_COSMETIC),
    SBOOL("Shine outfit", 0, SETTING_CAT_COSMETIC),
    SBOOL("Shiny shines", 0, SETTING_CAT_COSMETIC),

    // UI settings.
    SBOOL("Show BGM slot counter", 0, SETTING_CAT_UI),

    // Quarterframe timer (qftimer.cpp). Both displays default Off, which is
    // also what leaves the game completely unpatched; their appearance is not
    // a setting but a Gecko-writable config block (gui_config.hxx).
    SBOOL("Quarterframe timer", 0, SETTING_CAT_TIMER),
    SBOOL("QF section timer", 0, SETTING_CAT_TIMER),
    SBOOL("Keep sections across restart", 0, SETTING_CAT_TIMER),

    // Freeze triggers. Defaults are the upstream generator's: everything but
    // the yellow coin, which fires far too often to be useful.
    SBOOL("Yellow coin", 0, SETTING_CAT_QF_FREEZE),
    SBOOL("Red coin", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Blue coin", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Item pickup", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Dialogue start", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Cutscene start", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("NPC cleaned", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Bowser platform broken", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Yoshi mounted", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Object held", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Object thrown", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Object put down", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Triple jump", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Spin jump", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Ledge grab", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Wall kick", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Bounce", 1, SETTING_CAT_QF_FREEZE),
    SBOOL("Rope jump", 1, SETTING_CAT_QF_FREEZE)
};

const u32 kSettingsMagic   = 0x53535454u;  // 'SSTT'
const u16 kSettingsVersion = 1u;

const char *const kBoolLabels[2] = { "Off", "On" };

// How long to wait for the kernel to acknowledge a save before giving up.
// Generous: the kernel only services the doorbell between disc reads, and a
// FatFS write of a few hundred bytes can wait behind one.
const u32 kSaveTimeoutFrames = 300;  // ~5s at 60Hz

}  // namespace

Settings gSettings;

void Settings::resetDefaults() {
    // Binds ride along on the same handoff block and the same doorbell, so
    // they are reset, loaded and staged wherever settings are. Their table and
    // recorder live in binds.* -- see binds.hxx.
    gBinds.resetDefaults();

    mData.magic   = kSettingsMagic;
    mData.version = kSettingsVersion;
    mData.count   = SETTING_COUNT;
    for (int i = 0; i < SETTING_COUNT; i++) {
        mData.values[i] = kSettingDescs[i].defaultValue;
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
// the MEM2 handoff block by the time the game boots (see SusamuneCfg.c), so
// loading is just "validate and copy". On emulator there is no launcher and
// hence no backend, so we keep the compiled-in defaults.
// ---------------------------------------------------------------------

#if IS_EMULATOR

void Settings::init() { resetDefaults(); }

void Settings::save() {
    mSaveState = SETTINGS_SAVE_UNSUPPORTED;
    mDirty     = false;
    gBinds.clearDirty();
}

SettingsSaveState Settings::pollSave() { return mSaveState; }

#else

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

    // Copy only what the writer actually filled in. A kernel older than the
    // mod carries fewer settings than SETTING_COUNT; the remainder keep their
    // compiled-in defaults, which is what makes adding a setting safe.
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

    // set() marks dirty; adopting persisted values is not a user edit.
    mDirty     = false;
    mSaveSeq   = cfg->saveSeq;
    mSaveState = SETTINGS_SAVE_IDLE;

    // First boot of this game version on this SD card: the kernel found no
    // sections for it and cannot author defaults, because those live here
    // rather than in the launcher. Write them out now so the user has
    // something to look at (and to hand-edit) without having to visit the menu
    // first. Fire-and-forget -- the menu that would report the result does not
    // exist this early.
    if (cfg->flags & SUSAMUNE_CFG_FLAG_NO_CONFIG) {
        save();
    }
}

void Settings::save() {
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;

    if (mSaveState == SETTINGS_SAVE_UNSUPPORTED) {
        mDirty = false;
        gBinds.clearDirty();
        return;
    }

    for (int i = 0; i < SETTING_COUNT; i++) {
        cfg->values[i] = mData.values[i];
    }
    cfg->count = SETTING_COUNT;

    u16 staged[BIND_COUNT];
    gBinds.stageInto(staged);
    for (int i = 0; i < BIND_COUNT; i++) {
        cfg->binds[i] = staged[i];
    }
    cfg->bindCount = BIND_COUNT;
    gBinds.clearDirty();

    // Publish the payload before the doorbell, so the kernel can never see a
    // bumped saveSeq alongside a half-written values[]/binds[]. Both live in
    // the same mod-owned run of cache lines starting at values[].
    DCStoreRange((void *)cfg->values, sizeof(cfg->values) + sizeof(cfg->binds));

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
        return mSaveState;
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
    return mSaveState;
}

#endif  // IS_EMULATOR

void Settings::set(SettingId id, u8 value) {
    value = value % kSettingDescs[id].numChoices;
    if (mData.values[id] != value) {
        mDirty = true;
    }
    mData.values[id] = value;
}

void Settings::cycle(SettingId id, int dir) {
    int n = kSettingDescs[id].numChoices;
    int v = (int)mData.values[id] + dir;
    // Wrap into [0, n). dir is +/-1, so one add/sub suffices.
    if (v < 0) {
        v += n;
    } else if (v >= n) {
        v -= n;
    }
    if (mData.values[id] != (u8)v) {
        mDirty = true;
    }
    mData.values[id] = (u8)v;
}

const char *Settings::valueLabel(SettingId id) const {
    const SettingDesc &d = kSettingDescs[id];
    u8 v = mData.values[id];
    if (d.choices) {
        return d.choices[v % d.numChoices];
    }
    return kBoolLabels[v ? 1 : 0];
}

const SettingDesc &Settings::desc(SettingId id) { return kSettingDescs[id]; }

// kSettingDescs is indexed by SettingId and must stay row-for-row aligned with
// SUSAMUNE_SETTING_LIST. Adding a list row without a descriptor row (or vice
// versa) fails here rather than silently shifting every setting's meaning.
static_assert(sizeof(kSettingDescs) / sizeof(kSettingDescs[0]) == SETTING_COUNT,
              "kSettingDescs must have one row per SUSAMUNE_SETTING_LIST entry");
static_assert(SETTING_COUNT <= SUSAMUNE_CFG_MAX_SETTINGS,
              "SETTING_COUNT exceeds the MEM2 handoff block's values[] capacity");
static_assert(BIND_COUNT <= SUSAMUNE_CFG_MAX_BINDS,
              "BIND_COUNT exceeds the MEM2 handoff block's binds[] capacity");
