#include "susamune/fludd_colors.hxx"

#include "Dolphin/mem.h"
#include "SMS/Player/MarioGamePad.hxx"
#include "susamune/creation.hxx"
#include "susamune/model_color_editor.hxx"
#include "susamune/susamune_cfg.h"

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"

namespace FluddColors {
namespace {

const char kPartNames[] =
    "Body paint\0Metal\0Straps\0Tank\0Spray nozzle\0Hover nozzle\0"
    "Rocket nozzle\0Turbo nozzle\0Sprayed water\0Water highlights";
struct State : ModelColorEditor {
    u8 colors[PART_COUNT][3];
    u8 backup[PART_COUNT][3];
} sState;
static_assert(PART_COUNT == SUSAMUNE_FLUDD_COLORS_COUNT, "FLUDD colour slots moved");
static_assert(sizeof(State) <= 224, "FLUDD colour editor state grew");

} // namespace

const u8 *rgb(unsigned part) { return part < PART_COUNT ? sState.colors[part] : nullptr; }
bool enabled(unsigned part) { return part < PART_COUNT && (sState.enabled & (1u << part)); }
bool dirty() { return sState.editor.editing() ? sState.dirtyBefore : sState.dirty; }
void clearDirty() {
    if (sState.editor.editing()) sState.dirtyBefore = false;
    else sState.dirty = false;
}
bool editing() { return sState.editor.editing() || sState.releaseGuard; }
void resetDefaults() {
    Creation::fillWhite(sState.colors, PART_COUNT);
    sState.reset();
}

void adopt(const volatile SusamuneFluddColorsCfg *source) {
    if (!source || source->magic != SUSAMUNE_FLUDD_COLORS_MAGIC ||
        source->version != SUSAMUNE_FLUDD_COLORS_VERSION ||
        (source->enabled & ~SUSAMUNE_FLUDD_COLORS_MASK)) return;
    memcpy(sState.colors, (const void *)source->rgb, sizeof(sState.colors));
    sState.enabled = source->enabled;
    sState.dirty = false;
}

void stageInto(volatile SusamuneFluddColorsCfg *destination) {
    // A prior settings save can finish while this preview is unconfirmed.
    const bool preview = sState.editor.editing();
    memset((void *)destination, 0, sizeof(*destination));
    destination->magic = SUSAMUNE_FLUDD_COLORS_MAGIC;
    destination->version = SUSAMUNE_FLUDD_COLORS_VERSION;
    destination->enabled = (u16)(preview ? sState.enabledBefore : sState.enabled);
    memcpy((void *)destination->rgb, preview ? sState.backup : sState.colors,
           sizeof(sState.colors));
}
void beginEditor() {
    sState.begin(sState.colors, sState.backup, PART_COUNT, kPartNames);
}
void updateEditor(TMarioGamePad *pad) { sState.update(pad); }

void drawEditor(Menu *menu) { sState.editor.draw(menu, "FLUDD colours", ""); }

} // namespace FluddColors
