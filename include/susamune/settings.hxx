#ifndef _SUSAMUNE_SETTINGS_HXX
#define _SUSAMUNE_SETTINGS_HXX

#include <Dolphin/types.h>

#include "susamune/settings_list.h"

struct SusamuneCfg;

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
// Persistence uses the Nintendont handoff on console and a private slot-B
// file on Dolphin. Values live in this BSS struct during gameplay, so every
// read remains a plain cached MEM1 load -- features.cpp reads them every frame.
// =====================================================================

// The full set of settings. Append a row to SUSAMUNE_SETTING_LIST
// (settings_list.h) and the same ordinal to src/settings_descs.inc. The list
// order is the persisted values[] layout, so new IDs go only at the global
// tail; the descriptor category controls which menu tab displays each row.
#define SUSAMUNE_SETTING_ENUM(id, key) id,
enum SettingId {
    SUSAMUNE_SETTING_LIST(SUSAMUNE_SETTING_ENUM)

    SETTING_COUNT
};
#undef SUSAMUNE_SETTING_ENUM

// Which menu tab a setting appears under. CUSTOM is rendered by its owning
// feature tab; the others use generic category tabs.
enum SettingCategory {
    SETTING_CAT_QOL,
    SETTING_CAT_SAVESTATE,
    SETTING_CAT_MISC,
    SETTING_CAT_COSMETIC,
    SETTING_CAT_UI,
    SETTING_CAT_TIMER,
    SETTING_CAT_CUSTOM,
    SETTING_CAT_HIDDEN,
    SETTING_CAT_COUNT
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
    SETTINGS_SAVE_UNSUPPORTED,  // launcher/card backend unavailable
};
static_assert(SETTINGS_SAVE_UNSUPPORTED <= 0xFF,
              "settings save state no longer fits in a byte");

class Settings {
public:
    // Install compiled-in defaults, then adopt the launcher's values on
    // console. Dolphin finishes the adoption through finishInit() once CARD
    // is ready. Call from onAppInit before anything reads a setting.
    void init();

    // Dolphin cannot read slot B until Sunshine has initialized CARD during
    // the boot state. onUpdate polls this before the next director runs.
    // Returns true once loading has completed or the backend is unavailable.
    bool finishInit();

    // Install compiled-in defaults only. init() calls this first; it is also
    // the whole of init() on builds with no persistence backend.
    void resetDefaults();

    // --- persistence ---

    // Ask the active backend to save. Both backends queue the write and expose
    // completion through the same pollable API. Reports UNSUPPORTED without
    // configured storage.
    void save();

    // Advance an in-flight save. Call once per frame; returns the current
    // state, transitioning PENDING -> OK / ERROR / TIMEOUT.
    SettingsSaveState pollSave();

    SettingsSaveState saveState() const { return (SettingsSaveState)mSaveState; }

    // Positive backend error code from the last failed save.
    u32 lastError() const { return mLastError; }

    // True when a value changed since the last save.
    bool dirty() const { return mDirty; }

    u8   get(SettingId id) const { return mValues[id]; }
    bool getBool(SettingId id) const { return mValues[id] != 0; }
    void set(SettingId id, u8 value);

    // Advance a setting's value by dir (+1 / -1), wrapping. Toggles a bool,
    // steps a choice. Used by the menu for both A-press and left/right.
    void cycle(SettingId id, int dir);

    // Shined settings use packed hidden bytes without changing their values.
    static bool favoriteable(SettingId id);
    bool favorite(SettingId id) const;
    void toggleFavorite(SettingId id);

    // Human-readable label for the current value ("On"/"Off" or a choice).
    const char *valueLabel(SettingId id) const;

    static const char     *name(SettingId id);
    static SettingCategory category(SettingId id);

private:
    void adopt(const volatile SusamuneCfg *cfg);
    void stageInto(volatile SusamuneCfg *cfg);

    u8   mValues[SETTING_COUNT];
    bool mDirty;
    u8   mSaveState;
    u16  mSaveWaitFrames;
    u32  mLastError;
    u32  mSaveSeq;      // the sequence number we are waiting on
};

// The save timeout only reaches 300. Keeping its counter before the words
// consumes the alignment hole instead of enlarging the fixed MEM2 slot.
static_assert(sizeof(Settings) ==
                  (((((SETTING_COUNT + 3) & ~1) + 5) & ~3) + 8),
              "Settings live state layout changed");

extern Settings &gSettings;

#endif  // _SUSAMUNE_SETTINGS_HXX
