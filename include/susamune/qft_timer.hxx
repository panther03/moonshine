#ifndef _SUSAMUNE_QFT_TIMER_HXX
#define _SUSAMUNE_QFT_TIMER_HXX

#include <Dolphin/types.h>

class Menu;
class TMarDirector;

// Native Quarterframe Timer backend plus its two presentations: Sunshine's
// HUD timer (rounded to centiseconds) and the compact three-decimal readout.
// Timing state lives in fixed mod scratch because the small asm event hooks
// must be able to reach it without a linker relocation.
class QFTTimer {
  public:
  // Install the regional timing/freeze hooks. Called once after settings are
  // loaded and before the application state machine starts.
  void init();

  // The temporary-freeze countdown advances at the beginning of a frame, so
  // a freeze raised during director->direct() still renders for its complete
  // configured duration on that same frame.
  void beginFrame();

  // Stage lifecycle and post-direct display update.
  void onStageSetup(TMarDirector *director);
  void update();

  // Compact QFT overlay. Menu::draw supplies the already configured 2D
  // renderer; this draws only while the menu itself is closed.
  void draw(Menu *menu) const;

  // Susamune warps are new attempts, not an in-level transition.
  void requestReset();
  void freezeEvent();

  // Changes only when the timer actually starts a fresh stage attempt.
  u32 attemptSerial() const;

  // Read-only clock access for visual ghost samples. This does not alter the
  // QFT timing or capture hooks. `stopped` lets a recorder distinguish an
  // exact final result from an attempt-boundary rebase.
  bool currentQf(s32 *qf, bool *stopped = nullptr) const;

  // Read the entry edge behind the stage-loading transition capture without
  // consuming the transition. The retail completion hook runs one game frame
  // after Mario enters the loading zone; level checkpoints belong to the
  // entry frame while transition-finish ILs retain the completion timestamp.
  bool transitionEntryQf(s32 *qf, u16 *target) const;

  // Consume the exact QF captured by the stage-loading transition hook.
  // Returns once per stage transition.
  bool consumeTransition(s32 *qf, u16 *target);

  // Consume the final QF captured by the shine-stop hook.
  // Returns once per stage.
  bool consumeShine(s32 *qf);

  // Consume the final QF captured by Corona's Bowser-stop hook.
  bool consumeBowser(s32 *qf);

  // Consume exact custom endpoints used by the Any% Plaza ILs.
  bool consumeCustom(bool death, s32 *qf);

  // Keep the native timer in the same one-slot savestate as the director.
  void onSavestateSaved();
  void onSavestateLoaded();
};

extern QFTTimer gQFTTimer;

#endif  // _SUSAMUNE_QFT_TIMER_HXX
