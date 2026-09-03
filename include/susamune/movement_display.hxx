#ifndef _SUSAMUNE_MOVEMENT_DISPLAY_HXX
#define _SUSAMUNE_MOVEMENT_DISPLAY_HXX

class Menu;

namespace MovementDisplay {

void onStageSetup();
void onSavestateLoaded();
void beforeDirect(bool active);
void afterDirect(bool active);
void draw(Menu *menu);

}  // namespace MovementDisplay

#endif  // _SUSAMUNE_MOVEMENT_DISPLAY_HXX
