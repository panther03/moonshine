#ifndef SUSAMUNE_FLUDD_COLORS_HXX
#define SUSAMUNE_FLUDD_COLORS_HXX

#include <Dolphin/types.h>

struct SusamuneFluddColorsCfg;
class Menu;
class TMarioGamePad;

namespace FluddColors {
enum Part {
    PAINT, METAL, STRAPS, TANK, SPRAY_NOZZLE, HOVER_NOZZLE,
    ROCKET_NOZZLE, TURBO_NOZZLE, WATER, WATER_HIGHLIGHT, PART_COUNT,
};
const u8 *rgb(unsigned part);
bool enabled(unsigned part);
void resetDefaults();
void adopt(const volatile SusamuneFluddColorsCfg *source);
void stageInto(volatile SusamuneFluddColorsCfg *destination);
bool dirty();
void clearDirty();
bool editing();
void beginEditor();
void updateEditor(TMarioGamePad *pad);
void drawEditor(Menu *menu);
void onStageSetup();
void update();
bool preserveSavestateBindings(bool (*keep)(const void *word));
} // namespace FluddColors

#endif
