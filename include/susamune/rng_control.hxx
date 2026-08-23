#ifndef SUSAMUNE_RNG_CONTROL_HXX
#define SUSAMUNE_RNG_CONTROL_HXX

#include <Dolphin/types.h>

// Install the permanent crane dispatch wrappers and finish their tiny rand
// shims before the app state machine can load a stage.
void rngControlInit();

// Crane rand hooks run inside setupObjects(), so stale stage pointers must be
// dropped before that call begins.
void rngControlBeforeStageSetup();

// Re-adopt King Boo's stage-heap phase after a same-scenario restore.
void rngControlOnSavestateLoaded();

// Apply transition-based instruction controls before the director advances.
void rngControlApply();

// Boss steering makes the current attempt ineligible for leaderboard credit.
// Crane speed is deliberately excluded: it is a practice setup, not a boss
// outcome override.
bool rngControlInvalidatesIl();

extern "C" void susamuneForceKingBooFruit(void *slot, s32 reel);

// These arrays are executable two-word tail shims targeted by patches.py.
extern "C" u32 gCraneUpDownRandShim[];
extern "C" u32 gCraneRotYRandShim[];

#endif  // SUSAMUNE_RNG_CONTROL_HXX
