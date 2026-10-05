#include "susamune/layout_profiles.hxx"

#include "Dolphin/mem.h"
#include "Dolphin/OS.h"
#include "Dolphin/string.h"
#include "susamune/creation_extras.hxx"
#include "susamune/fludd_colors.hxx"
#include "susamune/input_display.hxx"
#include "susamune/mario_colors.hxx"
#include "susamune/menu.hxx"
#include "susamune/metadata_display.hxx"
#include "susamune/qft_display.hxx"
#if IS_EMULATOR
#include "susamune/emulator_persistence.hxx"
#endif

namespace LayoutProfiles {
namespace {
u32 sSequence;
u8 sOperation;

void readReply() {
#if !IS_EMULATOR
    DCInvalidateRange(&MOONSHINE_LAYOUT_PPC_PTR->ackSeq, 128);
#endif
}

bool ready() {
    if (!available() || sOperation) return false;
    readReply();
    const volatile MoonshineLayoutMailbox *box = MOONSHINE_LAYOUT_PPC_PTR;
    return box->requestSeq == box->ackSeq;
}

void publish(u32 operation, u32 slot, u32 expected) {
    volatile MoonshineLayoutMailbox *box = MOONSHINE_LAYOUT_PPC_PTR;
    sSequence = box->requestSeq + 1;
    if (!sSequence) ++sSequence;
    sOperation = operation;
    box->operation = operation;
    box->slot = slot;
    box->expectedGeneration = expected;
    box->requestSeq = sSequence;
#if !IS_EMULATOR
    DCFlushRange(MOONSHINE_LAYOUT_PPC_PTR, 32);
#endif
}
}

bool layoutSetting(SettingId id) {
    switch (id) {
    case SETTING_TIMER_SUNSHINE_VISIBILITY:
    case SETTING_TIMER_QFT_VISIBILITY:
    case SETTING_TIMER_SECTIONS:
    case SETTING_LEVEL_SPLITS:
    case SETTING_SPLIT_COMPARISON:
    case SETTING_METADATA_HORIZONTAL:
    case SETTING_ILING_RECENT:
    case SETTING_ILING_SHORT_NAMES:
    case SETTING_ILING_POPUP:
    case SETTING_SAVESTATE_FEEDBACK:
    case SETTING_ACHIEVEMENT_NOTIFICATIONS:
    case SETTING_SHOW_BGM_SLOTS:
    case SETTING_RESTART_QUEUED_FEEDBACK:
    case SETTING_STAGE_SESSION_DISPLAY:
    case SETTING_WALLKICK_DISPLAY:
    case SETTING_ROLLOUT_DISPLAY:
    case SETTING_DUST_DISPLAY:
    case SETTING_GB_SKIP_DISPLAY:
    case SETTING_JUMP_DISPLAY:
    case SETTING_GHOST_DISPLAY:
    case SETTING_GHOST_OPACITY:
    case SETTING_GHOST_APPEARANCE:
    case SETTING_GHOST_INPUTS:
    case SETTING_HELMET_APPEARANCE:
    case SETTING_CAP_APPEARANCE:
    case SETTING_SHADES_APPEARANCE:
    case SETTING_SHINE_SHIRT_APPEARANCE:
    case SETTING_SHINE_OUTFIT:
    case SETTING_NATIVE_TIMER_X:
    case SETTING_NATIVE_TIMER_Y:
    case SETTING_NATIVE_TIMER_SCALE:
    case SETTING_TAS_BANNER:
    case SETTING_FREE_CAMERA_HIDE_HUD:
        return true;
    default:
        return false;
    }
}

void capture(MoonshineLayoutPayload *out) {
    memset(out, 0, sizeof(*out));
    memset(out->settings, SUSAMUNE_CFG_UNSET, sizeof(out->settings));
    for (unsigned i = 0; i < MOONSHINE_LAYOUT_SETTINGS_COUNT; ++i)
        if (layoutSetting((SettingId)i)) out->settings[i] = gSettings.get((SettingId)i);
    gInputDisplay.stageInto(&out->input);
    out->input.startVisible = gInputDisplay.visible();
    gInputDisplay.stageStyleInto(&out->inputStyle);
    gMetadataDisplay.stageInto(&out->metadata);
    gMetadataDisplay.stageStyleInto(&out->metadataStyle);
    gQftDisplay.stageInto(&out->qft);
    gCreationExtras.stageInto(&out->creation);
    gCreationExtras.stageWallkickInto(&out->wallkick);
    gCreationExtras.stageMovementInto(&out->movement);
    gCreationExtras.stageNativeTimerInto(&out->nativeTimer);
    MarioColors::stageInto(&out->mario);
    FluddColors::stageInto(&out->fludd);
    gCreationExtras.stagePracticeDisplaysInto(&out->practiceDisplays);
}

bool apply(const MoonshineLayoutPayload &layout) {
    if (gSettings.saveState() == SETTINGS_SAVE_PENDING ||
        gInputDisplay.editing() || gMetadataDisplay.editing() ||
        gQftDisplay.editing() || gCreationExtras.editing() ||
        MarioColors::editing() || FluddColors::editing()) return false;
#if IS_EMULATOR
    SusamuneCfg *cfg = EmulatorPersistence::lock();
    if (!cfg) return false;
#else
    SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
#endif
    // Metadata borrows its format string; the transfer buffer is reusable.
    memcpy(&cfg->metadataDisplay, &layout.metadata, sizeof(layout.metadata));
    gMetadataDisplay.adopt(&cfg->metadataDisplay);
#if IS_EMULATOR
    EmulatorPersistence::unlock();
#endif
    for (unsigned i = 0; i < MOONSHINE_LAYOUT_SETTINGS_COUNT; ++i)
        if (layoutSetting((SettingId)i) && layout.settings[i] != SUSAMUNE_CFG_UNSET)
            gSettings.set((SettingId)i, layout.settings[i]);
    gInputDisplay.adopt(&layout.input);
    gInputDisplay.adoptStyle(&layout.inputStyle);
    gMetadataDisplay.adoptStyle(&layout.metadataStyle);
    gQftDisplay.adopt(&layout.qft);
    gCreationExtras.adopt(&layout.creation);
    gCreationExtras.adoptWallkick(&layout.wallkick);
    gCreationExtras.adoptMovement(&layout.movement);
    gCreationExtras.adoptNativeTimer(&layout.nativeTimer);
    MarioColors::adopt(&layout.mario);
    FluddColors::adopt(&layout.fludd);
    gCreationExtras.adoptPracticeDisplays(&layout.practiceDisplays);
    gSettings.markDirty();
    if (gMenu) gMenu->scheduleSettingsSave();
    return true;
}

bool available() {
#if !IS_EMULATOR
    const volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    if (cfg->magic != SUSAMUNE_CFG_MAGIC || cfg->version != SUSAMUNE_CFG_VERSION ||
        !(cfg->flags & MOONSHINE_LAYOUT_CFG_FLAG)) return false;
#endif
    const volatile MoonshineLayoutMailbox *box = MOONSHINE_LAYOUT_PPC_PTR;
    return box->magic == MOONSHINE_LAYOUT_MAILBOX_MAGIC &&
           box->version == MOONSHINE_LAYOUT_MAILBOX_VERSION;
}
bool busy() { return sOperation != 0; }
bool present(u32 slot) {
    return slot < MOONSHINE_LAYOUT_COUNT && available() &&
           (MOONSHINE_LAYOUT_PPC_PTR->presentMask & (1u << slot));
}
bool damaged(u32 slot) {
    return slot < MOONSHINE_LAYOUT_COUNT && available() &&
           (MOONSHINE_LAYOUT_PPC_PTR->badMask & (1u << slot));
}
const char *name(u32 slot) {
    return present(slot) ? MOONSHINE_LAYOUT_PPC_PTR->names[slot] : "";
}
u32 generation(u32 slot) {
    return present(slot) ? MOONSHINE_LAYOUT_PPC_PTR->generations[slot] : 0;
}
bool refresh() {
    if (!ready()) return false;
    publish(MOONSHINE_LAYOUT_LIST, 0, 0);
    return true;
}
bool save(u32 slot, const char *label, u32 expected) {
    if (slot >= MOONSHINE_LAYOUT_COUNT || !label || !label[0] || !ready()) return false;
    MoonshineLayoutFile *file = &MOONSHINE_LAYOUT_PPC_PTR->file;
    memset(file, 0, sizeof(*file));
    file->magic = MOONSHINE_LAYOUT_MAGIC;
    file->version = MOONSHINE_LAYOUT_VERSION;
    file->bytes = sizeof(*file);
    file->generation = expected + 1;
    if (!file->generation) ++file->generation;
    strncpy(file->name, label, sizeof(file->name) - 1);
    capture(&file->layout);
    file->checksum = MoonshineLayoutChecksum(file);
#if !IS_EMULATOR
    DCFlushRange(file, sizeof(*file));
#endif
    publish(MOONSHINE_LAYOUT_SAVE, slot, expected);
    return true;
}
bool load(u32 slot) {
    if (!ready() || !present(slot)) return false;
    publish(MOONSHINE_LAYOUT_LOAD, slot, generation(slot));
    return true;
}
const char *poll() {
    if (!sOperation) return nullptr;
    readReply();
    const volatile MoonshineLayoutMailbox *box = MOONSHINE_LAYOUT_PPC_PTR;
    if (box->ackSeq != sSequence) return nullptr;
    const u32 status = box->status;
    const u8 operation = sOperation;
    if (!status && operation == MOONSHINE_LAYOUT_LOAD) {
        MoonshineLayoutFile *file = &MOONSHINE_LAYOUT_PPC_PTR->file;
#if !IS_EMULATOR
        DCInvalidateRange(file, sizeof(*file));
#endif
        if (!MoonshineLayoutValid(file) ||
            file->generation != box->expectedGeneration) {
            sOperation = 0;
            return "Layout file is damaged; current layout kept";
        }
        if (!apply(file->layout)) return nullptr;
    }
    sOperation = 0;
    if (status == MOONSHINE_LAYOUT_ERROR_CHANGED) return "Profile changed; reopen Layout profiles";
    if (status == MOONSHINE_LAYOUT_ERROR_EMPTY) return "That layout profile is empty";
    if (status) return "Layout storage error; current layout kept";
    if (operation == MOONSHINE_LAYOUT_SAVE) return "Layout profile saved";
    if (operation == MOONSHINE_LAYOUT_LOAD) return "Layout profile applied";
    return nullptr;
}
}
