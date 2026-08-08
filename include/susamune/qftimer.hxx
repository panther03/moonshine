#ifndef _SUSAMUNE_QFTIMER_HXX
#define _SUSAMUNE_QFTIMER_HXX

// =====================================================================
// qftimer.hxx
//
// Quarterframe Timer + Quarterframe Section Timer, ported from the upstream
// practice codes of the same names. See doc/gecko_porting.md, "Quarterframe
// Timer", for the site-by-site derivation.
//
// Appearance is not a setting: it lives in the pinned config block
// (gui_config.hxx) that the web configurator overwrites with a Gecko code.
// Visibility, the section timer's reset behaviour and the eighteen freeze
// triggers are ordinary settings.
// =====================================================================

// Install / remove the injection sites to match the current settings. Once per
// frame from onUpdate, like featuresApply().
void qfTimerApply();

// Reset the timer for a new stage. From onSetup, i.e. straight after
// TMarDirector::setupObjects() -- the point the upstream code hooks.
void qfTimerOnStageLoad();

// Render whatever the last TGCConsole2::perform left to show, into `ortho`'s
// space. From afterDraw.
//
// The freeze counter and the section splits are NOT advanced here -- that
// happens in susamuneQfTick(), off the injected hook at the tail of
// TGCConsole2::perform, which is upstream's site. Reaching that hook is also
// what makes this draw anything at all, so the timers hide themselves outside
// a stage without anything having to ask.
void qfTimerDraw(void *ortho);

#endif  // _SUSAMUNE_QFTIMER_HXX
