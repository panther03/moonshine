#ifndef SUSAMUNE_MARIO_COLORS_HXX
#define SUSAMUNE_MARIO_COLORS_HXX

#include <Dolphin/types.h>

struct SusamuneMarioColorsCfg;
class Menu;
class TMarioGamePad;

namespace MarioColors {

enum Part {
    CAP, SHIRT, OVERALLS, GLOVES, SHOES, SUNGLASSES, SUNSHINE_SHIRT,
    PART_COUNT,
};

const u8 *rgb(unsigned part);
bool enabled(unsigned part);
void resetDefaults();
void adopt(const volatile SusamuneMarioColorsCfg *source);
void stageInto(volatile SusamuneMarioColorsCfg *destination);
bool dirty();
void clearDirty();
bool editing();
void beginEditor();
void updateEditor(TMarioGamePad *pad);
void drawEditor(Menu *menu);
void onStageSetup();
void update();
bool preserveSavestateBindings(bool (*keep)(const void *word));

} // namespace MarioColors

#endif
