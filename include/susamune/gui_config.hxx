#ifndef _SUSAMUNE_GUI_CONFIG_HXX
#define _SUSAMUNE_GUI_CONFIG_HXX

#include <Dolphin/types.h>

// =====================================================================
// gui_config.hxx
//
// The on-screen GUI's appearance config and the QF timer's runtime state, as
// one structure pinned at SUSAMUNE_ADDR_GUI_BLOCK (the mod's link base). Both
// halves need a fixed address: the config is what the web configurator
// overwrites with a Gecko code, and the state is reached by absolute address
// from the asm caves in qftimer.cpp, which have no way to find a mod global.
// One struct is what makes both sets of offsets constant -- two objects have no
// guaranteed order within a section.
//
// The layout mirrors the upstream codes' own config words. site/FORMAT.md is
// the contract; a Gecko code writes only the config half.
// =====================================================================

// Upstream's fillRect params + drawTextOpt for one element, merged.
//
// The background rect is RESOLVED, not four padding offsets: the configurator
// measures the text with the game's own metrics, exactly as the upstream
// generator does, so the mod never needs font metrics at draw time.
struct SusamuneTextStyle {
    /* 0x00 */ s16 x, y;              // text anchor; y is the BASELINE
    /* 0x04 */ s16 bgX0, bgY0;        // background rect
    /* 0x08 */ s16 bgX1, bgY1;
    /* 0x0C */ u16 fontSize;          // 20 = the font's native size
    /* 0x0E */ u16 pad;
    /* 0x10 */ u32 fgTop, fgBottom;   // RGBA8888; equal unless a gradient
    /* 0x18 */ u32 bg;                // RGBA8888. Alpha 0 draws no background
};

// The Gecko-writable half. Offsets are a published interface: append only.
struct SusamuneGuiConfig {
    /* 0x00 */ SusamuneTextStyle qft;
    /* 0x1C */ u16 qftFreezeFrames;  // hold after a trigger; 0 disables freezing
    /* 0x1E */ u16 pad;
    /* 0x20 */ SusamuneTextStyle qfst;
};

// The private half. `stopped` and `resetPending` are adjacent because two caves
// clear both with one `sth`, as upstream does at 0x817F00B2.
struct SusamuneQfState {
    /* 0x00 */ u8  stopped;       // set: `base` already holds the final time
    /* 0x01 */ u8  resetPending;  // set: restart the timer on the next load
    /* 0x02 */ u16 count;         // section splits recorded since a reset
    /* 0x04 */ s32 base;          // quarterframes banked before this stage
    /* 0x08 */ s32 freezeQf;      // quarterframe the display is held at
    /* 0x0C */ s32 freezeCtr;     // frames of hold left; -1 = indefinitely
    /* 0x10 */ s32 caveScratch;   // r4 spill for the changePlayerStatus cave
    /* 0x14 */ s32 lastQf;        // quarterframe of the last split
    /* 0x18 */ s32 pad[2];
    /* 0x20 */ s32 entries[16];   // split ring, indexed by count & 15
};

// State first: the config's offsets are published, so it must be free to grow.
struct SusamuneGuiBlock {
    SusamuneQfState   qf;
    SusamuneGuiConfig cfg;
};

#define SUSAMUNE_GUI_OFF_CONFIG 0x60

extern SusamuneGuiBlock gGuiBlock;

#endif  // _SUSAMUNE_GUI_CONFIG_HXX
