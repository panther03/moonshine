// =====================================================================
// binds.cpp
//
// The bind table and the recorder behind it. See binds.hxx for the design;
// actions.cpp reads binds through gBinds and menu.cpp's Binds tab renders
// and records them generically off kBindDescs.
// =====================================================================

#include "susamune/binds.hxx"

#include "JSystem/JUtility/JUTGamePad.hxx"
#include "susamune/glyphs.hxx"
#include "Dolphin/string.h"

namespace {

// Button bit -> menu glyph, in the order a combo is rendered. The ini token in
// SUSAMUNE_BIND_BUTTON_LIST is dropped: only the launcher reads susamune.ini.
struct BindButton {
    u16         bit;
    const char *display;
};
#define SUSAMUNE_BIND_BUTTON_ROW(bit, token, display) { (u16)(bit), display },
const BindButton kButtons[] = { SUSAMUNE_BIND_BUTTON_LIST(SUSAMUNE_BIND_BUTTON_ROW) };
#undef SUSAMUNE_BIND_BUTTON_ROW

const int kNumButtons = (int)(sizeof(kButtons) / sizeof(kButtons[0]));

const char kDisplaySeparator[] = SUSAMUNE_GLYPH_AMP;

// Button bits, spelled with the JUTGamePad names so the defaults read like the
// combos in doc/gecko_codes.md.
const u16 kA     = JUTGamePad::A;
const u16 kB     = JUTGamePad::B;
const u16 kX     = JUTGamePad::X;
const u16 kY     = JUTGamePad::Y;
const u16 kZ     = JUTGamePad::Z;
const u16 kStart = JUTGamePad::START;
const u16 kDUp   = JUTGamePad::DPAD_UP;
const u16 kDDown = JUTGamePad::DPAD_DOWN;
const u16 kDLeft = JUTGamePad::DPAD_LEFT;
const u16 kDRght = JUTGamePad::DPAD_RIGHT;

// Descriptor table, indexed by BindId. Defaults are the button combinations
// the original gecko codes hardcode (doc/gecko_codes.md).
const BindDesc kBindDescs[BIND_COUNT] = {
    { "Regrab last held object", (u16)(kX | kDUp) },
    { "Spawn Yoshi: green", (u16)(kY | kDUp) },
    { "Spawn Yoshi: orange", (u16)(kY | kDLeft) },
    { "Spawn Yoshi: purple", (u16)(kY | kDRght) },
    { "Spawn Yoshi: pink", (u16)(kY | kDDown) },
    { "Fast forward 4x", (u16)(kB | kDLeft) },
    { "Fast forward 8x", (u16)(kB | kDRght) },
    { "Open " SUSAMUNE_GLYPH_AMP " close menu", (u16)(kY | kStart) },
    { "Savestate: save", kDLeft },
    { "Savestate: load", kDRght },
    { "Open warp wheel", kZ },
    { "Full restart", (u16)(kZ | kB | kDUp) },
    { "Warp to last selected", (u16)(kY | kB | kDUp) },
    { "Instant restart", (u16)(kB | kDUp) },
};

int popCount(u16 v) {
    int n = 0;
    for (; v; v &= (u16)(v - 1)) {
        n++;
    }
    return n;
}

}  // namespace

Binds gBinds;

void Binds::resetDefaults() {
    for (int i = 0; i < BIND_COUNT; i++) {
        mMask[i] = kBindDescs[i].defaultMask;
    }
    mHeld       = 0;
    mPrevHeld   = 0;
    mRecAccum   = 0;
    mRecState   = REC_OFF;
    mRecTarget  = (BindId)0;
    mRecSilent  = false;
    mDirty      = false;
}

namespace {

// Reject anything the recorder could not have produced. The mask arrives from
// susamune.ini as well, where it may have been hand-written with any number of
// buttons; over-long combos are dropped rather than trimmed, so an obviously
// wrong line leaves the compiled-in default visible in the menu.
bool validMask(u16 mask) {
    return popCount((u16)(mask & SUSAMUNE_BIND_BUTTON_MASK)) <=
           SUSAMUNE_BIND_MAX_BUTTONS;
}

}  // namespace

void Binds::set(BindId id, u16 mask) {
    mask &= SUSAMUNE_BIND_BUTTON_MASK;
    if (!validMask(mask)) {
        return;
    }
    if (mMask[id] != mask) {
        mDirty = true;
    }
    mMask[id] = mask;
}

void Binds::adopt(u16 mask, BindId id) {
    mask &= SUSAMUNE_BIND_BUTTON_MASK;
    if (validMask(mask)) {
        mMask[id] = mask;
    }
}

void Binds::stageInto(u16 *dst) const {
    for (int i = 0; i < BIND_COUNT; i++) {
        dst[i] = mMask[i];
    }
}

bool Binds::isHeld(BindId id) const {
    u16 m = mMask[id];
    return m != 0 && live() && mHeld == m;
}

bool Binds::wasPressed(BindId id) const {
    u16 m = mMask[id];
    return m != 0 && live() && mHeld == m && mPrevHeld != m;
}

void Binds::update() {
    mPrevHeld = mHeld;
    mHeld = (u16)(JUTGamePad::mPadStatus[0].mButton & SUSAMUNE_BIND_BUTTON_MASK);

    if (mRecSilent && mRecState == REC_OFF && mHeld == 0) {
        mRecSilent = false;
    }

    switch (mRecState) {
    case REC_OFF:
        break;

    case REC_WAIT_RELEASE:
        // The A press that opened the recorder is still down; capturing from
        // here would immediately record "A". Wait for a clean pad first.
        if (mHeld == 0) {
            mRecState = REC_CAPTURING;
        }
        break;

    case REC_CAPTURING:
        if ((mRecAccum & ~mHeld) != 0) {
            // A button that was down has come back up: the combo is whatever
            // was held immediately before that, not what is left now.
            commitRecord();
        } else {
            mRecAccum = mHeld;
            if (popCount(mRecAccum) >= SUSAMUNE_BIND_MAX_BUTTONS) {
                commitRecord();  // full combo -- no need to wait for a release
            }
        }
        break;
    }
}

void Binds::beginRecord(BindId id) {
    mRecTarget = id;
    mRecAccum  = 0;
    mRecState  = REC_WAIT_RELEASE;
    mRecSilent = true;
}

void Binds::cancelRecord() {
    mRecState = REC_OFF;
    mRecAccum = 0;
}

void Binds::commitRecord() {
    if (mRecAccum != 0) {
        set(mRecTarget, mRecAccum);
    }
    mRecState = REC_OFF;
    mRecAccum = 0;
}

void Binds::format(u16 mask, char *out) {
    out[0] = '\0';
    mask &= SUSAMUNE_BIND_BUTTON_MASK;

    for (int i = 0; i < kNumButtons; i++) {
        if (!(mask & kButtons[i].bit)) {
            continue;
        }
        if (out[0]) {
            strcat(out, kDisplaySeparator);
        }
        strcat(out, kButtons[i].display);
    }

    if (!out[0]) {
        strcat(out, SUSAMUNE_BIND_NONE_TOKEN);
    }
}

const BindDesc &Binds::desc(BindId id) { return kBindDescs[id]; }

// kBindDescs is indexed by BindId and must stay row-for-row aligned with
// SUSAMUNE_BIND_LIST, exactly as kSettingDescs is with SUSAMUNE_SETTING_LIST.
static_assert(sizeof(kBindDescs) / sizeof(kBindDescs[0]) == BIND_COUNT,
              "kBindDescs must have one row per SUSAMUNE_BIND_LIST entry");
