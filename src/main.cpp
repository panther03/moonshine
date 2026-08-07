#include "Dolphin/GX_types.h"
#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "J2D/J2DTextBox.hxx"
#include "JKernel/JKRHeap.hxx"
#include "JUtility/JUTGamePad.hxx"
#include "SMS/System/Application.hxx"
#include "JSystem/J2D/J2DPane.hxx"
#include "JSystem/J2D/J2DPicture.hxx"
#include "JSystem/J2D/J2DOrthoGraph.hxx"
#include "Dolphin/THP.h"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"
#include "susamune/features.hxx"
#include "susamune/actions.hxx"
#include "susamune/binds.hxx"
#include "susamune/warp_wheel.hxx"
#if ENABLE_DEBUG_WARPS
#include "susamune/debug_warp.hxx"
#endif
#include "susamune/savestate.hxx"
#include "susamune/qftimer.hxx"
#include "susamune/addresses.hxx"
#if ENABLE_DRAW_CALIBRATION
#include "Dolphin/printf.h"
#endif
#include "SMS/Manager/RumbleManager.hxx"

SavestateManager* gSavestateMgr = nullptr;

// Replaces the game's OSGetArenaLo. The mod is linked into the bottom of the
// heap arena; reporting the raised floor here keeps the root heap from
// allocating over it. The top is avoided because the apploader keeps the FST
// there.
//
// The reserve is SUSAMUNE_ARENA_RESERVE_SIZE, not the region size: __OSArenaLo
// sits a debug stack below the __ArenaLo the blob links at. Adding only the
// region size puts the heap floor at MOD_BASE + 0x6000, inside the blob.
// SUSAMUNE_ARENA_RESERVE_SIZE must match arena_reserve in scripts/patches.py.
extern "C" void* getArenaLo() {
    return (void*)(*(volatile u32*)SUSAMUNE_ADDR_OS_ARENA_LO +
                   SUSAMUNE_ARENA_RESERVE_SIZE);
}

// Replaces the `bl TApplication::initialize` in main() (see patches.py), the
// last point before proc() starts the app-state machine. Settings must be live
// by here: proc() runs gameLoop() -- and so featuresApply() -- for the logo and
// title states too, so initialising any later leaves every feature reading
// zeroed BSS for the whole boot sequence.
extern "C" void onAppInit(TApplication* app) {
    app->initialize();
    gSettings.init();
    // Intro Skip patches the logo director, which is already directing by the
    // time the first onUpdate reaches featuresApply().
    featuresApplyEarly();
}

extern "C" u8 onUpdateGameMode(TMarDirector* director) {
    u8 state = director->updateGameMode();

    // Opening the menu must not also pause the game. The default menu bind
    // includes Start, which is what the director is reacting to here, so
    // swallow the transition into the pause state on the frame it fires.
    if (director->mCurState != state && state == 0x5 &&
        gBinds.wasPressed(BIND_MENU_TOGGLE)) {
        state = director->mCurState;
    }

    if (!gSettings.getBool(SETTING_DISABLE_WARPS))
        state = LevelWarp::kick(director, state);

#if ENABLE_DEBUG_WARPS
    if (Warp::pending()) {
        Warp::execute();
        // QF timer reset flag
        if (SUSAMUNE_ADDR_QF_TIMER_RESET != 0) {
            volatile u8* flag = reinterpret_cast<volatile u8*>(SUSAMUNE_ADDR_QF_TIMER_RESET);
            *flag = 1;
        }
        director->moveStage();
        state = 9;
    }
#endif

    return state;
}

// extern "C" void onFinishAppState(RumbleMgr* rumble) {
//     rumble->init();
// }

extern "C" void onSetup(TMarDirector* director) {
    static bool inited = false;
    director->setupObjects();

    // Runs on every stage load, so this must stay above the once-only guard.
    featuresOnStageLoad();
    // Upstream hooks the `unk260 = 1` store one instruction below this call;
    // running here is the same point in the same frame.
    qfTimerOnStageLoad();

    if (inited) return; else inited = true;

    // Settings are already initialised, much earlier, by onAppInit.

    JKRHeap *oldHeap = JKRHeap::sSystemHeap->becomeCurrentHeap();
    menuInit();
    gSavestateMgr = new SavestateManager();
    
    if (oldHeap) {
        oldHeap->becomeCurrentHeap();
    } else {
        JKRHeap::sCurrentHeap = nullptr;
    }
}


extern "C" s32 onUpdate(JDrama::TDirector* director) {
    // Sample the pad before direct(), not after: onUpdateGameMode runs inside
    // it and asks whether the menu bind was pressed this frame, which would
    // otherwise be answered from the previous frame's sample.
    gBinds.update();
    // Before direct(): while the wheel is open it takes the pad away from
    // the game.
    if (!gSettings.getBool(SETTING_DISABLE_WARPS))
        WarpWheel::update(gpApplication.mGamePads[0]);

    // Freeze the stage while an overlay is up. direct() runs the movement and
    // animation perform lists only outside the pause and stage-exit states, so
    // lending it one of those for the call is the entire pause; state 12 is the
    // one whose own branch does nothing while the fader is up. Any app state it
    // did produce would be a state change we never asked for, so drop it.
    const bool freeze = gpMarDirector &&
                        gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
                        ((gMenu && gMenu->shown()) || WarpWheel::shown());
    if (freeze) {
        gpMarDirector->mCurState = TMarDirector::STATE_STAGE_EXIT_2;
    }
    int state = director->direct();
    if (freeze) {
        gpMarDirector->mCurState = TMarDirector::STATE_NORMAL;
        state = 0;
    }
    if (!gSettings.getBool(SETTING_DISABLE_WARPS))
        state = LevelWarp::onDirected(state);

    // Apply/restore the toggled memory-patch features (ported gecko codes).
    // Runs every frame like the gecko handler; no-ops when nothing changed.
    featuresApply();

    actionsApply();

    qfTimerApply();

    if (gSavestateMgr) {
        gSavestateMgr->updateHook();
    }
    if (gMenu) {
        gMenu->update(gpApplication.mGamePads[0]);
    }

    return state;
}

#if ENABLE_DRAW_CALIBRATION
// A ruler in the declared 2D space, to find where the screen edges actually
// are. Every value below is a coordinate in that space, so a screenshot reads
// off directly: whichever labels are visible bound the visible band, and the
// L-shaped corner marks show whether x = 0 / y = 0 are on screen at all.
//
// Delete nothing when this is answered -- fix kScreen2D* in menu.hxx and turn
// the option back off.
static void drawCalibration() {
    const JUtility::TColor tick(255, 64, 64, 255);
    const JUtility::TColor major(64, 255, 64, 255);
    const JUtility::TColor corner(64, 160, 255, 255);

    for (int y = kScreen2DTop; y <= kScreen2DBottom; y += 16) {
        const bool label = ((y - kScreen2DTop) % 64) == 0;
        gMenu->fillBox(0, y, label ? 96 : 40, 1, label ? major : tick);
        gMenu->fillBox(kScreen2DWidth - (label ? 96 : 40), y, label ? 96 : 40, 1,
                       label ? major : tick);
        if (label) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%d", y);
            gMenu->drawText(buf, 100, y, 16, 16, major);
        }
    }
    // Corner brackets on the declared bounds: if a leg is missing, that edge is
    // off screen.
    for (int i = 0; i < 4; i++) {
        const int x = (i & 1) ? kScreen2DWidth - 64 : 0;
        const int y = (i & 2) ? kScreen2DBottom - 4 : kScreen2DTop;
        gMenu->fillBox(x, y, 64, 4, corner);
        gMenu->fillBox((i & 1) ? kScreen2DWidth - 4 : 0,
                       (i & 2) ? kScreen2DBottom - 64 : kScreen2DTop, 4, 64, corner);
    }
}
#endif

extern "C" void afterDraw() {
    // The original call is a full GXDrawDone barrier. Process queued loads
    // immediately afterward: director, fader, audio, and the current frame's
    // GPU work are all complete, while the next game frame has not begun.
    THPPlayerDrawDone();
    if (gSavestateMgr) gSavestateMgr->processPendingLoad();

    {
        // Establish the mod's 2D space (menu.hxx). mOrtho is assigned rather
        // than passed to the constructor because neither constructor can give a
        // space whose origin is not 0.
        //
        // **setPort(), not setup2D().** setup2D() only re-establishes vertex
        // and TEV state; setPort() is what calls GXSetViewport, sets the
        // scissor from mBounds, and builds the ortho projection from mOrtho.
        // Calling setup2D() and expecting a coordinate space fails silently:
        // the overlays still draw, in whatever space the game last set, and
        // under whatever scissor it last left behind. Menu::fillBox/fillPoly
        // call setup2D() deliberately, because they must NOT disturb this.
        J2DOrthoGraph ortho(0, 0, kScreen2DWidth, kScreen2DHeight);
        ortho.mOrtho.mY1 = kScreen2DTop;
        ortho.mOrtho.mY2 = kScreen2DBottom;
        ortho.setPort();

#if ENABLE_DRAW_CALIBRATION
        drawCalibration();
#endif
        if (gMenu)
            gMenu->draw(&ortho);
        qfTimerDraw();
        // After Menu::draw, which is what leaves mOrtho set for the shared
        // 2D primitives -- the same rule the warp wheel follows.
        if (!gSettings.getBool(SETTING_DISABLE_WARPS))
            WarpWheel::draw();
#if ENABLE_SAVESTATE_DBG
        if (gSavestateMgr)
            gSavestateMgr->draw(&ortho);
#endif
    }
}
