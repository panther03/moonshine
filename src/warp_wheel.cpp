// =====================================================================
// warp_wheel.cpp
//
// Warp destinations, the restart binds and the wheel UI. See
// warp_wheel.hxx for the two-phase warp design.
// =====================================================================

#include "susamune/warp_wheel.hxx"

#include "SMS/Manager/FlagManager.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/binds.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/gui_config.hxx"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"

namespace {

typedef JUtility::TColor Color;

const u8 kAreaHotel  = 7;
const u8 kAreaCasino = 14;

}  // namespace

// =====================================================================
// Warp mechanism
// =====================================================================

namespace {

bool             sArmed;
bool             sKeepSpawn;
LevelWarp::Dest  sDest;
LevelWarp::Dest  sLast;
bool             sLastValid;
bool             sTailPending;

u8 currentGameInt3() { return (u8)TFlagManager::smInstance->getFlag(0x40003); }

// A wheel warp reaches the next stage through moveStage(), so it passes none
// of the sites that would otherwise arm a restart. Arm it by hand, for both
// the mod's own timer and a QFT gecko code the user may still be loading.
void markQuickFreezeReset() {
    gGuiBlock.qf.resetPending = 1;
    if (SUSAMUNE_ADDR_QF_TIMER_RESET != 0) {
        *(volatile u8 *)SUSAMUNE_ADDR_QF_TIMER_RESET = 1;
    }
}

// Redirect the stage the app is about to load, plus the save flags the
// destination is entered with. Runs on the frame the exit fade finishes.
void applyDest(const LevelWarp::Dest &dest) {
    TFlagManager *flags = TFlagManager::smInstance;
    flags->setFlag(0x40003, dest.gameInt3);
    flags->setFlag(0x40002, 0);      // coin count
    flags->setBool(true, 0x30006);   // got a shine in the previous stage --
    flags->setBool(false, 0x30004);  // together these suppress the death and
                                     // Peach-kidnapped openings

    gpApplication.mNextScene.mAreaID    = dest.area;
    gpApplication.mNextScene.mEpisodeID = dest.episode;

    // Hold the stick neutral through the hotel/casino arrival, where Mario
    // otherwise walks off the spawn.
    TMarioGamePad *pad     = gpApplication.mGamePads[0];
    pad->_E4               = (dest.area == kAreaHotel || dest.area == kAreaCasino) ? 59 : 0;
    pad->mState.mReadInput = false;

    markQuickFreezeReset();
}

}  // namespace

namespace LevelWarp {

void warpTo(const Dest &dest) {
    sDest      = dest;
    sLast      = dest;
    sLastValid = true;
    sArmed     = true;
    sKeepSpawn = false;
}

void warpToLast() {
    if (sLastValid) {
        sDest      = sLast;
        sArmed     = true;
        sKeepSpawn = false;
    }
}

void restart(bool keepSpawn) {
    const TGameSequence &cur = gpApplication.mCurrentScene;
    sDest.area               = cur.mAreaID;
    sDest.episode            = cur.mEpisodeID;
    sDest.gameInt3           = currentGameInt3();
    sArmed                   = true;
    sKeepSpawn               = keepSpawn;
}

u8 kick(TMarDirector *director, u8 state) {
    if (!sArmed) {
        return state;
    }
    sArmed       = false;
    sTailPending = true;

    if (sKeepSpawn) {
        sKeepSpawn = false;
        // decideMarioPosIdx() picks Mario's entry point from mPrevScene, which
        // TApplication fills from mCurrentScene as it loads -- so naming the
        // area he arrived from leaves the entry point where it was and he comes
        // back out of the same pipe. Safe only because applyDest() rewrites
        // mNextScene at the end of the transition: the destination follows
        // mCurrentScene too, and would otherwise be dragged along with it.
        TGameSequence &cur = gpApplication.mCurrentScene;
        cur.mAreaID        = gpApplication.mPrevScene.mAreaID;
        cur.mEpisodeID     = gpApplication.mPrevScene.mEpisodeID;
    }

    gpApplication.mNextScene.mAreaID    = sDest.area;
    gpApplication.mNextScene.mEpisodeID = sDest.episode;

    director->moveStage();
    return TMarDirector::STATE_STAGE_EXIT;
}

s32 onDirected(s32 appState) {
    // Non-zero means the director has finished leaving the stage and is
    // handing the app its next state; anything else is an ordinary frame.
    if (appState == 0) {
        return appState;
    }

    if (sTailPending) {
        sTailPending = false;
        applyDest(sDest);
        return TApplication::CONTEXT_DIRECT_STAGE;
    }

    if (appState > 1 && gSettings.getBool(SETTING_AREA_LOCK)) {
        // Turn every departure into a restart of the area being left, with
        // the spawn point it would have had on the way in.
        TGameSequence &cur = gpApplication.mCurrentScene;
        Dest dest          = { cur.mAreaID, cur.mEpisodeID, currentGameInt3() };
        cur.mAreaID        = gpApplication.mPrevScene.mAreaID;
        cur.mEpisodeID     = gpApplication.mPrevScene.mEpisodeID;
        applyDest(dest);
        return TApplication::CONTEXT_DIRECT_STAGE;
    }

    return appState;
}

}  // namespace LevelWarp

// =====================================================================
// Destination tables
// =====================================================================

namespace {

// Slices run clockwise from the up notch; index 8 is the centre.
const int kNumSlots = 9;

// One region of a wheel. `slot` is where it sits: slices run clockwise from
// the up notch and 8 is the centre. Wheels are sparse, so only the positions
// that exist are listed -- `slot` rides in the padding the label pointer's
// alignment leaves behind and costs nothing.
struct Slot {
    const char *label;
    u8          slot;
    u8          area;
    u8          episode;
    u8          gameInt3;
};

// Every stored wheel's regions end to end, grouped in kWheels order.
const Slot kSlots[] = {
    // Bianco subareas
    { "Windmill", 1, 0x37, 0, 1 },
    { "Bianco 3", 2, 0x2F, 0, 2 },
    { "Bianco 6", 5, 0x2E, 0, 5 },
    // Ricco subareas. Both Gooper Blooper fights are the one scene ricco8
    // behind area 0x3B, which has a single scenario, so there is only one.
    { "Blooper", 0, 0x3B, 0, 0 },
    { "Blooper Race", 1, 0x1E, 0, 1 },
    { "Ricco 4", 3, 0x30, 0, 3 },
    // Gelato subareas
    { "Gelato 1", 0, 0x20, 0, 0 },
    { "Sand Bird", 3, 0x21, 0, 3 },
    // Pinna subareas
    { "Mecha-Bowser", 0, 0x3A, 1, 0 },
    { "Pinna 2", 1, 0x32, 0, 1 },
    { "Pinna 6", 5, 0x29, 0, 5 },
    { "Balloons", 7, 0x3A, 0, 7 },
    // Sirena subareas
    { "Sirena 2", 1, 0x33, 0, 1 },
    { "Sirena 4", 3, 0x28, 0, 3 },
    { "King Boo", 4, 0x38, 0, 4 },
    // Pianta subareas
    { "Pianta 5", 4, 0x2A, 0, 4 },
    // Noki subareas
    { "Bottle", 2, 0x2C, 0, 2 },
    { "Eel", 3, 0x39, 0, 3 },
    { "Noki 6", 5, 0x1F, 0, 5 },
    { "Red Fish", 7, 0x10, 0, 7 },
    // Sirena hotel. The hotel and the casino each hide several scenarios
    // behind one area id; gameInt3 is what separates them.
    { "Ep2 Hotel", 0, 7, 0, 1 },
    { "Ep3 Hotel", 1, 7, 1, 2 },
    { "Ep4 Hotel", 2, 7, 2, 3 },
    { "Ep4 Casino", 3, 14, 0, 3 },
    { "Ep5 Hotel", 4, 7, 2, 4 },
    { "Ep5 Casino", 5, 14, 1, 4 },
    { "Ep7 Hotel", 6, 7, 3, 6 },
    { "Ep8 Reds", 7, 7, 4, 7 },
    // Delfino Plaza
    { "Bianco Plant", 0, 1, 0, 0 },
    { "Bianco Chase", 1, 1, 1, 0 },
    { "R" SUSAMUNE_GLYPH_AMP "G Plants", 2, 1, 5, 0 },
    { "Peaceful", 3, 1, 2, 0 },
    { "Pinna Cut", 4, 1, 7, 0 },
    { "Yoshi", 5, 1, 8, 0 },
    { "Flooded", 6, 1, 9, 0 },
    // Delfino secrets
    { "Beach Pipe", 0, 0x15, 0, 0 },
    { "Pachinko", 1, 0x16, 0, 0 },
    { "Grass Pipe", 2, 0x17, 0, 0 },
    { "Lilypad", 3, 0x18, 0, 0 },
    { "Jail", 4, 0x1D, 0, 0 },
    { "Airstrip", 5, 0x00, 0, 0 },
    { "Air Reds", 6, 0x14, 0, 0 },
    { "Bowser", 7, 0x3C, 0, 0 },
    { "Corona", 8, 0x34, 0, 0 },
};

// A stored wheel: its title and its half-open range of kSlots. Holding the
// range rather than a pointer and a count keeps this at two words.
struct Wheel {
    const char *title;
    u8          first;
    u8          count;
};

constexpr Wheel kWheels[] = {
    { "Bianco Subareas", 0, 3 },
    { "Ricco Subareas", 3, 3 },
    { "Gelato Subareas", 6, 2 },
    { "Pinna Subareas", 8, 4 },
    { "Sirena Subareas", 12, 3 },
    { "Pianta Subareas", 15, 1 },
    { "Noki Subareas", 16, 4 },
    { "Sirena Hotel", 20, 8 },
    { "Delfino Plaza", 28, 7 },
    { "Delfino Secrets", 35, 9 },
};

constexpr int kNumWheels = sizeof(kWheels) / sizeof(kWheels[0]);

// The ranges must tile kSlots exactly, which catches a group whose size
// changed without the following groups' offsets moving with it.
static_assert(kWheels[kNumWheels - 1].first + kWheels[kNumWheels - 1].count ==
                  sizeof(kSlots) / sizeof(kSlots[0]),
              "kWheels ranges must cover kSlots exactly");

// A region of the root wheel. `episodeArea` non-zero means its main wheel is
// generated rather than stored: eight slices, one per episode of that area.
struct Root {
    const char *label;
    u8          episodeArea;
    s8          mainWheel;
    s8          subWheel;
};

const Root kRoot[kNumSlots] = {
    { "Bianco", 2, -1, 0 },
    { "Ricco", 3, -1, 1 },
    { "Gelato", 4, -1, 2 },
    { "Pinna", 5, -1, 3 },
    { "Sirena", 6, -1, 4 },
    { "Pianta", 8, -1, 5 },
    { "Noki", 9, -1, 6 },
    { "Hotel", 0, 7, -1 },
    { "Delfino", 0, 8, 9 },
};

const char kEpisodeLabels[8][2] = { "1", "2", "3", "4", "5", "6", "7", "8" };

}  // namespace

// =====================================================================
// Wheel UI
// =====================================================================

namespace {

// Unit vectors every 22.5 degrees, Q12, clockwise from the up notch. Even
// entries point at a slice's centre, odd ones at the boundary between two.
const s16 kUnit[16][2] = {
    { 0, -4096 },     { 1567, -3784 }, { 2896, -2896 }, { 3784, -1567 },
    { 4096, 0 },      { 3784, 1567 },  { 2896, 2896 },  { 1567, 3784 },
    { 0, 4096 },      { -1567, 3784 }, { -2896, 2896 }, { -3784, 1567 },
    { -4096, 0 },     { -3784, -1567 },{ -2896, -2896 },{ -1567, -3784 },
};

// The wheel is an octagon of circumradius kRadiusOuter with an octagon of
// kRadiusInner taken out of the middle; the ring between them is cut on the
// eight boundary directions and each piece slid kGap out along its own centre
// direction. kRadiusLabel is the mid-ring apothem, where the flat edges are.
// Game 2D space; the visible band is y 16..464 (menu.hxx).
const int kCx = kScreen2DWidth / 2;
const int kCy = kScreen2DTop + 262;
const int kRadiusOuter = 180;
const int kRadiusInner = 78;
const int kRadiusLabel = 125;
const int kGap = 6;

const int kTitleY   = kScreen2DTop + 44;
const int kTitleSz  = 20;
const int kLabelSz  = 12;
const int kRootSz   = 16;
const int kDigitSz  = 26;
// Sat on the bottom edge of the framebuffer before the 2D space was
// corrected, so the hint never rendered at all.
const int kFooterY  = kScreen2DBottom - 16;
const int kFooterSz = 12;

const int kCentreSlot = 8;

inline Color col(u8 r, u8 g, u8 b, u8 a) { return Color(r, g, b, a); }

inline Color cBackdrop() { return col(6, 8, 14, 150); }
inline Color cSlice()    { return col(34, 48, 61, 235); }
inline Color cSelected() { return col(255, 194, 61, 255); }
inline Color cLabel()    { return col(233, 241, 247, 255); }
inline Color cOnAccent() { return col(27, 18, 6, 255); }
inline Color cFooter()   { return col(127, 149, 166, 255); }

bool  sShown;
s8    sRoot = -1;  // -1 while the root wheel is up
bool  sSubMode;

// Index into kWheels for the wheel on screen; -1 means the episodes of
// kRoot[sRoot].episodeArea, which are generated rather than stored.
s8 currentWheel() {
    const Root &root = kRoot[sRoot];
    return (sSubMode && root.subWheel >= 0) ? root.subWheel : root.mainWheel;
}

// The wheel on screen, or null when it is a generated episode wheel.
const Wheel *currentWheelData() {
    if (sRoot < 0) {
        return nullptr;
    }
    s8 wheel = currentWheel();
    return wheel >= 0 ? &kWheels[wheel] : nullptr;
}

// The region at slice `i` of `wheel`, or null when that angle is empty.
const Slot *findSlot(const Wheel *wheel, int i) {
    for (int k = 0; k < wheel->count; k++) {
        if (kSlots[wheel->first + k].slot == i) {
            return &kSlots[wheel->first + k];
        }
    }
    return nullptr;
}

const char *currentTitle() {
    if (sRoot < 0) {
        return "Warp";
    }
    s8 wheel = currentWheel();
    return wheel >= 0 ? kWheels[wheel].title : kRoot[sRoot].label;
}

// Label for slice `i` of the wheel on screen, or null when it is empty.
const char *slotLabel(int i) {
    if (sRoot < 0) {
        return kRoot[i].label;
    }
    const Wheel *wheel = currentWheelData();
    if (wheel) {
        const Slot *slot = findSlot(wheel, i);
        return slot ? slot->label : nullptr;
    }
    return i < 8 ? kEpisodeLabels[i] : nullptr;
}

// Resolve slice `i` to a destination. False when the slice is empty.
bool slotDest(int i, LevelWarp::Dest *out) {
    const Wheel *wheel = currentWheelData();
    if (wheel) {
        const Slot *slot = findSlot(wheel, i);
        if (!slot) {
            return false;
        }
        out->area     = slot->area;
        out->episode  = slot->episode;
        out->gameInt3 = slot->gameInt3;
        return true;
    }
    if (i >= 8) {
        return false;
    }
    out->area     = kRoot[sRoot].episodeArea;
    out->episode  = (u8)i;
    out->gameInt3 = (u8)i;
    return true;
}

int stickSlot(TMarioGamePad *pad) {
    f32 x = pad->mControlStick.mStickX;
    f32 y = pad->mControlStick.mStickY;
    if (x * x + y * y < 0.25f) {
        return kCentreSlot;
    }
    int best    = 0;
    f32 bestDot = -2.0f;
    for (int i = 0; i < 8; i++) {
        // Stick Y points up, screen Y points down.
        f32 dot = x * kUnit[i * 2][0] - y * kUnit[i * 2][1];
        if (dot > bestDot) {
            bestDot = dot;
            best    = i;
        }
    }
    return best;
}

// Octagon vertex `k` at `radius`, displaced by kGap along direction centreK.
void corner(int k, int centreK, int radius, s16 *out) {
    out[0] = (s16)(kCx + (kUnit[k][0] * radius + kUnit[centreK][0] * kGap) / 4096);
    out[1] = (s16)(kCy + (kUnit[k][1] * radius + kUnit[centreK][1] * kGap) / 4096);
}

void drawCentred(const char *text, int cx, int y, int size, Color color) {
    gMenu->drawText(text, cx - Menu::textWidth(text, size) / 2, y, size, size, color);
}

void drawSlice(int i, bool selected) {
    const char *label = slotLabel(i);
    if (!label) {
        return;
    }

    const int centreK = i * 2;
    s16 quad[8];
    corner((centreK + 15) & 15, centreK, kRadiusOuter, quad + 0);
    corner((centreK + 1) & 15, centreK, kRadiusOuter, quad + 2);
    corner((centreK + 1) & 15, centreK, kRadiusInner, quad + 4);
    corner((centreK + 15) & 15, centreK, kRadiusInner, quad + 6);
    gMenu->fillPoly(quad, 4, selected ? cSelected() : cSlice());

    const int size = (sRoot < 0) ? kRootSz : ((!currentWheelData()) ? kDigitSz : kLabelSz);
    drawCentred(label, kCx + kUnit[centreK][0] * kRadiusLabel / 4096,
                kCy + kUnit[centreK][1] * kRadiusLabel / 4096 - size / 2, size,
                selected ? cOnAccent() : cLabel());
}

void drawCentre(bool selected) {
    const char *label = slotLabel(kCentreSlot);
    if (!label) {
        return;
    }
    s16 octagon[16];
    for (int k = 0; k < 8; k++) {
        octagon[k * 2]     = (s16)(kCx + kUnit[k * 2 + 1][0] * kRadiusInner / 4096);
        octagon[k * 2 + 1] = (s16)(kCy + kUnit[k * 2 + 1][1] * kRadiusInner / 4096);
    }
    const int size = (sRoot < 0) ? kRootSz : kLabelSz;
    gMenu->fillPoly(octagon, 8, selected ? cSelected() : cSlice());
    drawCentred(label, kCx, kCy - size / 2, size,
                selected ? cOnAccent() : cLabel());
}

void close() {
    sShown   = false;
    sRoot    = -1;
    sSubMode = false;
}

}  // namespace

namespace WarpWheel {

void update(TMarioGamePad *pad) {
    // Only normal gameplay reaches the game-mode hook that starts a warp.
    if (!gpMarDirector || gpMarDirector->mCurState != TMarDirector::STATE_NORMAL) {
        close();
        return;
    }

    if (gBinds.wasPressed(BIND_INSTANT_RESTART)) {
        LevelWarp::restart(true);
    } else if (gBinds.wasPressed(BIND_FULL_RESTART)) {
        LevelWarp::restart(false);
    } else if (gBinds.wasPressed(BIND_WARP_LAST)) {
        LevelWarp::warpToLast();
    }

    if (gMenu && gMenu->shown()) {
        close();
        return;
    }

    if (gBinds.wasPressed(BIND_WARP_WHEEL)) {
        if (sShown) {
            close();
        } else {
            sShown = true;
        }
    }
    if (!sShown) {
        return;
    }

    const u32 pressed = pad->mButtons.mFrameInput;
    const int slot    = stickSlot(pad);

    if (pressed & JUTGamePad::B) {
        if (sRoot < 0) {
            close();
        } else {
            sRoot = -1;
        }
    } else if (pressed & JUTGamePad::X) {
        sSubMode = !sSubMode;
    } else if (pressed & JUTGamePad::A) {
        if (sRoot < 0) {
            sRoot = (s8)slot;
        } else {
            LevelWarp::Dest dest;
            if (slotDest(slot, &dest)) {
                LevelWarp::warpTo(dest);
                close();
            }
        }
    }

    if (sShown) {
        // The stage is frozen while the wheel is up, but the buttons are still
        // sampled by anything outside the director's perform lists.
        pad->mButtons.mInput      = 0;
        pad->mButtons.mFrameInput = 0;
        pad->mButtons.mRapidInput = 0;
    }
}

bool shown() { return sShown; }

void draw() {
    if (!sShown || !gMenu) {
        return;
    }

    const int selected = stickSlot(gpApplication.mGamePads[0]);

    gMenu->fillBox(0, kScreen2DTop, kScreen2DWidth, kScreen2DHeight, cBackdrop());
    drawCentred(currentTitle(), kCx, kTitleY, kTitleSz, cSelected());

    for (int i = 0; i < 8; i++) {
        drawSlice(i, i == selected);
    }
    drawCentre(selected == kCentreSlot);

    const char *hint = SUSAMUNE_GLYPH_A " Select    " SUSAMUNE_GLYPH_B
                       " Back    " SUSAMUNE_GLYPH_X " Subareas";
    drawCentred(hint, kCx, kFooterY, kFooterSz, cFooter());
}

}  // namespace WarpWheel
