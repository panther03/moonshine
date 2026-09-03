#include "susamune/settings.hxx"

#if IS_EMULATOR

#include "susamune/binds.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/emulator_persistence.hxx"
#include "susamune/input_display.hxx"
#include "susamune/metadata_display.hxx"
#include "susamune/qft_display.hxx"

namespace {
constexpr u32 kSaveTimeoutFrames = 300;
constexpr u16 kSaveRetryFrames = 600;
}

void Settings::init() { resetDefaults(); }

bool Settings::finishInit() {
    const EmulatorPersistence::InitResult result = EmulatorPersistence::init();
    if (result == EmulatorPersistence::INIT_WAITING) return false;
    if (result == EmulatorPersistence::INIT_UNAVAILABLE) {
        mLastError = EmulatorPersistence::initError();
        mSaveState = SETTINGS_SAVE_UNSUPPORTED;
        return true;
    }

    SusamuneCfg *cfg = EmulatorPersistence::lock();
    adopt(cfg);
    EmulatorPersistence::unlock();

    if (EmulatorPersistence::needsInitialSave()) save();
    return true;
}

void Settings::save() {
    if (mSaveState == SETTINGS_SAVE_PENDING) return;

    SusamuneCfg *cfg = EmulatorPersistence::lock();
    if (!cfg) {
        mSaveState = SETTINGS_SAVE_UNSUPPORTED;
        mDirty = false;
        gBinds.clearDirty();
        gInputDisplay.clearDirty();
        gMetadataDisplay.clearDirty();
        gQftDisplay.clearDirty();
        gCreationExtras.clearDirty();
        return;
    }

    stageInto(cfg);
    mSaveSeq = EmulatorPersistence::commit();
    mDirty = false;
    mLastError = 0;
    mSaveWaitFrames = 0;
    mSaveState = SETTINGS_SAVE_PENDING;
}

SettingsSaveState Settings::pollSave() {
    if (mSaveState == SETTINGS_SAVE_ERROR && mDirty) {
        if (mSaveWaitFrames > 0) {
            mSaveWaitFrames--;
        }
        if (mSaveWaitFrames == 0) {
            save();
        }
        return static_cast<SettingsSaveState>(mSaveState);
    }
    if (mSaveState != SETTINGS_SAVE_PENDING) {
        return static_cast<SettingsSaveState>(mSaveState);
    }

    u32 error = 0;
    switch (EmulatorPersistence::poll(mSaveSeq, &error)) {
    case EmulatorPersistence::SAVE_OK:
        mSaveState = SETTINGS_SAVE_OK;
        if (mDirty || gBinds.dirty() || gInputDisplay.dirty() ||
            gMetadataDisplay.dirty() || gQftDisplay.dirty() ||
            gCreationExtras.dirty()) {
            save();
        }
        break;
    case EmulatorPersistence::SAVE_ERROR:
        mLastError = error;
        // stageInto() cleared every component's dirty flag. Keep one
        // aggregate retry marker until a later transaction succeeds.
        mDirty = true;
        mSaveWaitFrames = kSaveRetryFrames;
        mSaveState = SETTINGS_SAVE_ERROR;
        break;
    case EmulatorPersistence::SAVE_PENDING:
        if (mSaveWaitFrames <= kSaveTimeoutFrames) {
            ++mSaveWaitFrames;
            if (mSaveWaitFrames > kSaveTimeoutFrames) {
                // Keep polling the original immutable slot until the helper
                // acknowledges it; this is only a one-shot UI timeout.
                return SETTINGS_SAVE_TIMEOUT;
            }
        }
        break;
    }
    return static_cast<SettingsSaveState>(mSaveState);
}

#endif  // IS_EMULATOR
