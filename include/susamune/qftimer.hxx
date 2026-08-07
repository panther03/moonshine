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

// Advance the freeze counter, record a section split, and render both
// overlays. Once per frame from afterDraw, which is this port's equivalent of
// upstream's hook at the tail of TGCConsole2::perform.
void qfTimerDraw();

#endif  // _SUSAMUNE_QFTIMER_HXX
