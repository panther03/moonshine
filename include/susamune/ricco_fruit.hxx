#ifndef SUSAMUNE_RICCO_FRUIT_HXX
#define SUSAMUNE_RICCO_FRUIT_HXX

#include <Dolphin/types.h>

// Finish the tail shim before the app state machine can enter Ricco.
void riccoFruitControlInit();

// Drop pointers into the previous stage heap before new actors are created.
void riccoFruitControlBeforeStageSetup();

// All four call-site hooks use this r31/LR forwarding shim.
extern "C" u32 gRiccoFruitRandShim[];

#endif  // SUSAMUNE_RICCO_FRUIT_HXX
