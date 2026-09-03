#ifndef _SUSAMUNE_FEATURES_HXX
#define _SUSAMUNE_FEATURES_HXX

#include <Dolphin/types.h>

// =====================================================================
// features.hxx
//
// Ports of the SMS practice "gecko" codes that reduce to memory patches,
// each driven by a setting. See doc/gecko_porting.md for the method and for
// which codes belong in C instead.
// =====================================================================

// Apply enabled features' patches and restore disabled ones'. Call once per
// frame (from onUpdate); early-outs on patches already holding the right word.
void featuresApply();

// Apply the features that have to be live before the app state machine runs
// (Intro Skip). Call once from onAppInit, right after gSettings.init().
// featuresApply() does not touch these rows and nothing restores them: their
// sites have run long before the menu exists, so turning one off takes effect
// on the next boot, when the game's own code is loaded from disc again.
void featuresApplyEarly();

// Reset per-stage feature state. Call on every stage load (from onSetup).
void featuresOnStageLoad();

// The snapshot rewinds two mutable patch sites (the fruit timeout datum and
// Fast Text's message buffer), but feature bookkeeping and settings stay live.
// Preserve the sites' save-time state and reconcile them after a restore.
u8 featuresSavestateState();
void featuresOnSavestateLoaded(u8 savedState);

// --- shared patch primitives (also used by actions.cpp) ---

// Write one instruction word into the running game and make it visible to the
// core: flush the store out of the data cache, drop any stale copy from the
// instruction cache. Mandatory on console; a no-op on Dolphin, which does not
// emulate the instruction cache.
void writeGameCode(u32 addr, u32 word);

// Encode a PPC `b` (no link) from `from` to `to` -- the same math the gecko
// code handler uses for C2/C6 (launcher/codehandler/codehandler.s, _hook1).
u32 branchWord(u32 from, u32 to);

#endif  // _SUSAMUNE_FEATURES_HXX
