#ifndef _SUSAMUNE_BINDS_HXX
#define _SUSAMUNE_BINDS_HXX

#include <Dolphin/types.h>

#include "susamune/binds_list.h"

// =====================================================================
// binds.hxx
//
// Configurable button combos for the mod's *actions* -- the ported gecko
// codes that do something when pressed rather than toggling a setting
// (see doc/gecko_codes.md, "Simple Actions/Binds"). Deliberately parallel
// to settings.*: one row in SUSAMUNE_BIND_LIST (binds_list.h) plus one row
// in binds_descs.inc gives a bind an id, an ini key, a menu label
// and a default combo; the menu renders and records them generically and
// actions.cpp reads them.
//
// A bind is a mask of at most SUSAMUNE_BIND_MAX_BUTTONS GameCube button
// bits, matched on *exact equality* -- the buttons held must be the bind's
// and nothing else. That is what almost every gecko code does, whether
// through the gecko VM (`28400D50 00000201`) or a plain `cmplwi`, so
// B+DPad-Left does not fire while B+X+DPad-Left is held. Binding two
// actions to the same combo fires both; nothing arbitrates.
//
// isHeldSubset/wasPressedSubset are the looser rule -- the bind's buttons
// held, extras allowed -- for the codes that ask for it (Pattern Selector
// tests a bare `andi. r0,r0,0x40`; Spawn Yoshi and Manual Attempt Counter
// mask the d-pad nibble out before comparing).
//
// Input comes straight from JUTGamePad::mPadStatus[0] -- the raw pad
// sample -- rather than from the game's TMarioGamePad. The gecko codes
// read the same place, and it keeps binds working while the game has the
// player's pad disabled (dialogue, cutscenes), which is exactly when
// fast-forward is wanted.
// =====================================================================

#define SUSAMUNE_BIND_ENUM(id, key) id,
enum BindId {
    SUSAMUNE_BIND_LIST(SUSAMUNE_BIND_ENUM)

    BIND_COUNT
};
#undef SUSAMUNE_BIND_ENUM
static_assert(BIND_COUNT <= 0x100, "bind ids no longer fit in recorder state");

// Buffer size for format(). Sized for *every* bindable button at once, not
// just the four a bind may hold, so a hand-edited susamune.ini with an
// over-long combo cannot overrun a caller's buffer before set() rejects it.
const int kBindTextMax = 64;

class Binds {
public:
    // Install compiled-in defaults. Call before anything reads a bind;
    // Settings::init() does it, then adopts persisted values.
    void resetDefaults();

    u16  get(BindId id) const { return mMask[id]; }
    void set(BindId id, u16 mask);

    // True when a value changed since the last save. The menu writes
    // susamune.ini on close only when this or Settings::dirty() is set.
    bool dirty() const { return mDirty; }
    void clearDirty() { mDirty = false; }

    // Sample the pad and advance the recorder. Call once per frame from
    // onUpdate, before any action reads a bind.
    void update();

    // A modal prompt still reads A/B directly, but no configured action may
    // inherit that press. Binds resume after the physical pad is released.
    void suppressUntilRelease() { mRecSilent = true; }

    // Bindable buttons currently held (raw, unaffected by suppression).
    u16 held() const { return mHeld; }

    // Is this bind's exact combo held / did it become held this frame?
    bool isHeld(BindId id) const;
    bool wasPressed(BindId id) const;
    // The menu must still recognise its own close combo after a page silences
    // conflicting actions. Call only after the page's modal input grab.
    bool wasPressedRaw(BindId id) const {
        const u16 m = mMask[id];
        return m != 0 && mHeld == m && mPrevHeld != m;
    }

    // Same, but tolerating other buttons: true whenever the bind's buttons are
    // all down.
    bool isHeldSubset(BindId id) const {
        u16 m = mMask[id];
        return m != 0 && live() && (mHeld & m) == m;
    }
    bool wasPressedSubset(BindId id) const {
        u16 m = mMask[id];
        return m != 0 && live() && (mHeld & m) == m && (mPrevHeld & m) != m;
    }

    // --- recording ---
    //
    // beginRecord() arms the recorder for one bind. It first waits for the
    // pad to go idle (the A press that started it is still down), then
    // accumulates held buttons and commits the combo as soon as either a
    // held button is released or SUSAMUNE_BIND_MAX_BUTTONS are down. Binds
    // do not fire at all while this is running.
    void   beginRecord(BindId id);
    void   cancelRecord();
    bool   recording() const { return mRecState != REC_OFF; }
    BindId recordTarget() const { return (BindId)mRecTarget; }
    // Buttons accumulated so far, for live feedback in the menu.
    u16    recordPreview() const { return mRecAccum; }

    // Render a combo with ampersand glyphs, or SUSAMUNE_BIND_NONE_TOKEN when empty.
    // `out` must hold kBindTextMax bytes.
    static void format(u16 mask, char *out);

    static const char *name(BindId id);

    // --- persistence (see settings.cpp, which owns the MEM2 handoff) ---
    void adopt(u16 mask, BindId id);  // like set(), but not a user edit
    void stageInto(volatile u16 *dst) const;

private:
    enum RecState {
        REC_OFF,
        REC_WAIT_RELEASE,  // pad must go idle before we start accumulating
        REC_CAPTURING,
    };

    void commitRecord();

    bool live() const { return mRecState == REC_OFF && !mRecSilent; }

    u16  mMask[BIND_COUNT];
    u16  mHeld;
    u16  mPrevHeld;
    u16  mRecAccum;
    u8   mRecState;
    u8   mRecTarget;
    // Binds stay dead after the recorder or a modal prompt consumes a press,
    // until the physical pad is released again.
    bool mRecSilent;
    bool mDirty;
};

static_assert(sizeof(Binds) == BIND_COUNT * sizeof(u16) + 10,
              "Binds live state layout changed");

extern Binds &gBinds;

#endif  // _SUSAMUNE_BINDS_HXX
