#ifndef _SUSAMUNE_SETTINGS_HXX
#define _SUSAMUNE_SETTINGS_HXX

#include <Dolphin/types.h>

#include "susamune/settings_list.h"

// =====================================================================
// settings.hxx
//
// The mod's persistent settings, kept deliberately independent of the
// menu that edits them (menu.cpp) and of any feature that reads them
// (e.g. savestate.cpp). A setting is one entry in a static descriptor
// table plus one byte of live value; everything else -- how the menu
// renders it, how a feature queries it, how it serialises to
// susamune.ini on the SD card -- is driven off that table so adding a
// setting is a one-line change.
//
// Persistence (console only) is a two-step handoff with the Nintendont
// kernel; see susamune_cfg.h for the protocol and settings.cpp for the
// implementation. Values live in this BSS struct during gameplay, not in
// the MEM2 handoff block, so every read is a plain cached MEM1 load with
// no coherency rules -- features.cpp reads settings every frame.
// =====================================================================

// The full set of settings. Add a row to SUSAMUNE_SETTING_LIST
// (settings_list.h) and a matching row in kSettingDescs (settings.cpp);
// nothing else needs to change. The list order controls the values[] layout
// -- which is also the on-disc layout, so it must stay append-only -- and the
// within-category display order in the menu (which filters by category), so
// keep each category's entries grouped.
#define SUSAMUNE_SETTING_ENUM(id, key) id,
enum SettingId {
    SUSAMUNE_SETTING_LIST(SUSAMUNE_SETTING_ENUM)

    SETTING_COUNT
};
#undef SUSAMUNE_SETTING_ENUM

// Which menu tab a setting appears under. The menu builds one generic tab per
// category and renders the settings tagged with it.
enum SettingCategory {
    SETTING_CAT_QOL,
    SETTING_CAT_SAVESTATE,
    SETTING_CAT_MISC,
    SETTING_CAT_COSMETIC,
    SETTING_CAT_UI,
    SETTING_CAT_TIMER,
    // The eighteen quarterframe freeze triggers get a tab of their own -- they
    // are a list to scan, not settings to mix in with anything else.
    SETTING_CAT_QF_FREEZE,
    SETTING_CAT_COUNT
};

// Static, const description of one setting. Field order and widths are chosen
// so a row is 12 bytes rather than 24; there are ~30 of these.
struct SettingDesc {
    const char        *name;          // label shown in the menu
    const char *const *choices;       // null => bool, rendered On / Off
    u8                 numChoices;    // 2 for a bool
    u8                 defaultValue;
    u8                 category;      // a SettingCategory
};

// The live payload, in the mod's BSS. POD; magic + version mirror the
// savestate header so a stale blob is rejected rather than misread.
struct SettingsData {
    u32 magic;
    u16 version;
    u16 count;
    u8  values[SETTING_COUNT];
};

// Progress of an in-flight save, for the on-screen toast. The write itself is
// performed asynchronously by the Nintendont ARM kernel (the PPC cannot touch
// the SD card), so a save is a request plus a poll, never a blocking call.
enum SettingsSaveState {
    SETTINGS_SAVE_IDLE,     // nothing in flight
    SETTINGS_SAVE_PENDING,  // doorbell rung, waiting on the kernel
    SETTINGS_SAVE_OK,       // kernel wrote susamune.ini
    SETTINGS_SAVE_ERROR,    // kernel reported a FatFS error (see lastError)
    SETTINGS_SAVE_TIMEOUT,  // kernel never answered (old/stock launcher?)
    SETTINGS_SAVE_UNSUPPORTED,  // emulator build: no persistence backend
};

class Settings {
public:
    // Install compiled-in defaults, then adopt any values the launcher
    // persisted into the MEM2 handoff block. Call once at boot before
    // anything reads a setting (values live in BSS, so they are zero until
    // this runs) -- see onAppInit in main.cpp, which runs before
    // TApplication::proc() and therefore before the logo/title sequence.
    void init();

    // Install compiled-in defaults only. init() calls this first; it is also
    // the whole of init() on builds with no persistence backend.
    void resetDefaults();

    // --- persistence ---

    // Ask the launcher to write susamune.ini. Non-blocking: this stages the
    // values into the MEM2 block and rings the doorbell. No-op (and reports
    // SETTINGS_SAVE_UNSUPPORTED) when there is no backend. Clears dirty().
    void save();

    // Advance an in-flight save. Call once per frame; returns the current
    // state, transitioning PENDING -> OK / ERROR / TIMEOUT.
    SettingsSaveState pollSave();

    SettingsSaveState saveState() const { return mSaveState; }

    // FatFS FRESULT from the last failed save, for the toast text.
    u32 lastError() const { return mLastError; }

    // True when a value changed since the last save. The menu uses this to
    // avoid touching the SD card when nothing was edited.
    bool dirty() const { return mDirty; }

    u8   get(SettingId id) const { return mData.values[id]; }
    bool getBool(SettingId id) const { return mData.values[id] != 0; }
    void set(SettingId id, u8 value);

    // Advance a setting's value by dir (+1 / -1), wrapping. Toggles a bool,
    // steps a choice. Used by the menu for both A-press and left/right.
    void cycle(SettingId id, int dir);

    // Human-readable label for the current value ("On"/"Off" or a choice).
    const char *valueLabel(SettingId id) const;

    static const SettingDesc &desc(SettingId id);

private:
    SettingsData      mData;
    bool              mDirty;
    SettingsSaveState mSaveState;
    u32               mLastError;
    u32               mSaveSeq;      // the sequence number we are waiting on
    u32               mSaveWaitFrames;
};

// Single global instance. POD (no virtuals / no ctor), so it lives in BSS
// zero-initialised; resetDefaults() installs real defaults at boot.
extern Settings gSettings;

#endif  // _SUSAMUNE_SETTINGS_HXX
