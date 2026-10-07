#ifndef _SUSAMUNE_VISIBLE_GOOP_HXX
#define _SUSAMUNE_VISIBLE_GOOP_HXX

// Reset the lazy-activation state after a stage has finished setting up.
void visibleGoopOnStageSetup();

// Heap materials/display lists rewind, while this module's cursor does not.
void visibleGoopOnSavestateLoaded();

// Reconcile the current stage's pollution materials with the setting. While
// disabled this is inert unless it has materials to restore.
void visibleGoopUpdate();

#endif  // _SUSAMUNE_VISIBLE_GOOP_HXX
