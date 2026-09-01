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
#if IS_EMULATOR
#include "susamune/emulator_persistence.hxx"
#endif
#include "susamune/features.hxx"
#include "susamune/actions.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/crash_report.hxx"
#include "susamune/binds.hxx"
#include "susamune/input_display.hxx"
#include "susamune/metadata_display.hxx"
#include "susamune/mem_diagnostics.hxx"
#include "susamune/iling.hxx"
#include "susamune/attempt_counter.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/ghost.hxx"
#include "susamune/ghost_model.hxx"
#include "susamune/ghost_storage.hxx"
#include "susamune/qft_display.hxx"
#include "susamune/records.hxx"
#include "susamune/practice_visuals.hxx"
#include "susamune/records_persistence.hxx"
#include "susamune/ricco_fruit.hxx"
#include "susamune/rng_control.hxx"
#include "susamune/split_events.hxx"
#include "susamune/split_stats.hxx"
#include "susamune/stage_loader.hxx"
#include "susamune/pattern_selector.hxx"
#include "susamune/warp_wheel.hxx"
#include "susamune/visible_goop.hxx"
#if ENABLE_DEBUG_WARPS
#include "susamune/debug_warp.hxx"
#endif
#include "susamune/savestate.hxx"
#include "susamune/addresses.hxx"
#include "SMS/Manager/RumbleManager.hxx"
#include "SMS/Manager/FlagManager.hxx"
#include "SMS/Manager/PollutionManager.hxx"
#include "susamune/nintendont_cfg.h"
#include "susamune/wallkick_display.hxx"
#include "susamune/movement_display.hxx"
#include "susamune/gameplay_polish.hxx"

namespace {
// The mod reservation is fixed, so BSS storage costs no additional game heap.
// Keep this persistent controller out of Sunshine's pressured system heap.
alignas(SavestateManager) u8 sSavestateManagerStorage[sizeof(SavestateManager)];

struct RetailPadInputSnapshot {
    u32 input;
    u32 frameInput;
    u32 releaseInput;
    u32 rapidInput;
    u32 meaning;
    u32 frameMeaning;
    u32 releaseMeaning;
};

void suppressRetailPad(TMarioGamePad *pad, RetailPadInputSnapshot &saved) {
    saved.input = pad->mButtons.mInput;
    saved.frameInput = pad->mButtons.mFrameInput;
    saved.releaseInput = pad->mButtons._8;
    saved.rapidInput = pad->mButtons.mRapidInput;
    saved.meaning = pad->mMeaning;
    saved.frameMeaning = pad->mFrameMeaning;
    saved.releaseMeaning = pad->_D8;
    pad->mButtons.mInput = 0;
    pad->mButtons.mFrameInput = 0;
    pad->mButtons._8 = 0;
    pad->mButtons.mRapidInput = 0;
    pad->mMeaning = 0;
    pad->mFrameMeaning = 0;
    pad->_D8 = 0;
}

void restoreRetailPad(TMarioGamePad *pad,
                      const RetailPadInputSnapshot &saved) {
    pad->mButtons.mInput = saved.input;
    pad->mButtons.mFrameInput = saved.frameInput;
    pad->mButtons._8 = saved.releaseInput;
    pad->mButtons.mRapidInput = saved.rapidInput;
    pad->mMeaning = saved.meaning;
    pad->mFrameMeaning = saved.frameMeaning;
    pad->_D8 = saved.releaseMeaning;
}
}

SavestateManager* gSavestateMgr = nullptr;

// Replaces the game's OSGetArenaLo. The mod is linked into the bottom of the
// heap arena; reporting the raised floor here keeps the root heap from
// allocating over it. The top is avoided because the apploader keeps the FST
// there.
//
// The reserve is SUSAMUNE_ARENA_RESERVE_SIZE, not the region size: __OSArenaLo
// sits a debug stack below the __ArenaLo the blob links at. Adding only the
// region size puts the heap floor at MOD_BASE + 0x1E000, inside the blob.
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
    CrashReport::init();
    gSettings.init();
    rngControlInit();
    riccoFruitControlInit();
    gQFTTimer.init();
    Ghost::init();
    GhostModel::init();
    gAttemptCounter.init();
    Records::init();
    RecordsPersistence::init();
    ILing::init();
    StageLoader::init();
    SplitStats::init();
    SplitEvents::init();
    GhostStorage::init();
#if ENABLE_MEM_DIAGNOSTICS
    memDiagnosticsInit();
#endif

#if !IS_EMULATOR
    // The launcher owns this option because Sunshine cannot persist its own
    // rumble preference without a memory card. Apply it after initialize(),
    // when both the option flags and SMSRumbleMgr have been constructed.
    volatile u32* ninCfgConfig = reinterpret_cast<volatile u32*>(
        SUSAMUNE_NIN_CFG_CONFIG_PPC_ADDR);
    DCInvalidateRange((void*)ninCfgConfig, sizeof(*ninCfgConfig));
    if ((*ninCfgConfig & SUSAMUNE_NIN_CFG_DISABLE_RUMBLE) != 0) {
        SMSRumbleMgr->setActive(false);
        TFlagManager::smInstance->setFlag(0x90000u, 0);
    }
#endif

#if !IS_EMULATOR
    // The launcher made persisted settings available before initialize().
    featuresApplyEarly();
#endif
}

extern "C" u8 onUpdateGameMode(TMarDirector* director) {
    if (StageLoader::holdGameModeBeforeUpdate(director)) {
        return director->mCurState;
    }
    if (WarpWheel::holdGameModeBeforeUpdate(director)) {
        return director->mCurState;
    }

    u8 state = director->updateGameMode();

    // Opening the menu must not also pause the game. The default menu bind
    // includes Start, which is what the director is reacting to here, so
    // swallow the transition into the pause state on the frame it fires.
    if (director->mCurState != state &&
        state == TMarDirector::STATE_PAUSE_MENU &&
        (gBinds.wasPressed(BIND_MENU_TOGGLE) ||
         gSettings.getBool(SETTING_DISABLE_RETAIL_PAUSE) ||
         Ghost::observerActive())) {
        state = director->mCurState;
    }

    state = WarpWheel::applyPendingGameModeAction(director, state);

    if (gSettings.getBool(SETTING_DISABLE_WARPS)) {
        LevelWarp::cancelPending(true);
        if (WarpWheel::retailExitPending()) {
            state = LevelWarp::kick(director, state);
        }
    } else {
        state = LevelWarp::kick(director, state);
    }

#if ENABLE_DEBUG_WARPS
    if (Warp::pending()) {
        Warp::execute();
        gQFTTimer.requestReset();
        director->moveStage();
        state = 9;
    }
#endif

    return state;
}

extern "C" u8 onPauseMenuNextState(TPauseMenu2 *pauseMenu) {
    return WarpWheel::guardExitArea(pauseMenu->getNextState());
}

// extern "C" void onFinishAppState(RumbleMgr* rumble) {
//     rumble->init();
// }

extern "C" void onSetup(TMarDirector* director) {
    static bool inited = false;
    static bool recordsSceneKnown = false;
    static Records::Area recordsArea = Records::AREA_INVALID;

    CrashReport::note(SUSAMUNE_CRASH_EVENT_SETUP_ENTER,
                      static_cast<u32>(director->mAreaID) << 8 |
                          director->mEpisodeID,
                      reinterpret_cast<u32>(director));

    // TPollutionManager publishes itself through gpPollution but its retail
    // destructor never clears that global. Stages without a pollution manager
    // would otherwise inherit a pointer into the previous stage's freed heap.
    gpPollution = nullptr;
    GhostModel::beforeStageSetup();
    SplitEvents::beforeStageSetup();
    if (Ghost::observerCleanupPending())
        ILing::resetAfterObserver();
    ILing::beforeStageSetup();
    rngControlBeforeStageSetup();
    riccoFruitControlBeforeStageSetup();
    director->setupObjects();
    CrashReport::note(SUSAMUNE_CRASH_EVENT_SETUP_RETURN,
                      static_cast<u32>(director->mAreaID) << 8 |
                          director->mEpisodeID,
                      director->_260);
    ILing::onStageSetup();

    const Records::Area nextRecordsArea =
        Records::classifyArea(director->mAreaID);
    if (recordsSceneKnown && nextRecordsArea != recordsArea) {
        RecordsPersistence::checkpoint();
    }
    recordsSceneKnown = true;
    recordsArea = nextRecordsArea;

    // Runs on every stage load, so this must stay above the once-only guard.
    featuresOnStageLoad();
    actionsOnStageLoad();
    visibleGoopOnStageSetup();
    gQFTTimer.onStageSetup(director);
    SplitEvents::onStageSetup(director);
    SplitStats::onStageSetup();
    Ghost::onStageSetup(director);
    GhostModel::onStageSetup(director);
    const bool observerStage = Ghost::observerActive();
    if (!observerStage)
        gAttemptCounter.onStageSetup(director);
    gCreationExtras.onStageSetup();
    if (observerStage)
        Records::invalidateAttempt();
    Records::onStageSetup(director->mAreaID, director->mEpisodeID);
    if (observerStage)
        Records::invalidateAttempt();
    WallkickDisplay::onStageSetup();
    MovementDisplay::onStageSetup();
    CrashReport::note(SUSAMUNE_CRASH_EVENT_STAGE_READY,
                      static_cast<u32>(director->mAreaID) << 8 |
                          director->mEpisodeID,
                      director->_260);
#if ENABLE_MEM_DIAGNOSTICS
    memDiagnosticsOnStageSetup();
#endif

    if (inited) return; else inited = true;

    // Settings are already initialised, much earlier, by onAppInit.

    JKRHeap *oldHeap = JKRHeap::sSystemHeap->becomeCurrentHeap();
    menuInit();
    gSavestateMgr = new (sSavestateManagerStorage) SavestateManager();
    
    if (oldHeap) {
        oldHeap->becomeCurrentHeap();
    } else {
        JKRHeap::sCurrentHeap = nullptr;
    }
}


extern "C" s32 onUpdate(JDrama::TDirector* director) {
    CrashReport::observeContext(gpApplication.mContext);
    static bool recordsStageContext = false;
    const bool stageContext =
        gpApplication.mContext == TApplication::CONTEXT_DIRECT_STAGE;
    if (recordsStageContext && !stageContext) {
        Records::onStageExit();
        RecordsPersistence::checkpoint();
    }
    recordsStageContext = stageContext;

#if IS_EMULATOR
    static bool persistenceReady = false;
    if (!persistenceReady && gSettings.finishInit()) {
        persistenceReady = true;
        ILing::onPersistenceReady();
        // This is before direct() for the first Nintendo-logo frame. Applying
        // the boot patches after direct() is too late for that director.
        featuresApplyEarly();
    }
#endif

    // Sample the pad before direct(), not after: onUpdateGameMode runs inside
    // it and asks whether the menu bind was pressed this frame, which would
    // otherwise be answered from the previous frame's sample.
    gBinds.update();
    if (gMenu && gMenu->suppressesBinds()) {
        gBinds.suppressUntilRelease();
    }
    const bool creationEditing = gQftDisplay.editing() ||
                                 gInputDisplay.editing() ||
                                 gMetadataDisplay.editing() ||
                                 gCreationExtras.editing();
    const bool sessionModalBeforeDirect = StageLoader::modal();
    const bool sessionResultBeforeDirect = StageLoader::resultOwnsInput();
    const bool menuOpenBeforeDirect = gMenu && gMenu->shown();
    const bool menuOwnsRetailPad = menuOpenBeforeDirect ||
        (gMenu && gBinds.wasPressed(BIND_MENU_TOGGLE));
    const bool wheelOpenBeforeDirect = WarpWheel::shown();
    const bool wheelOwnsInputBeforeDirect =
        wheelOpenBeforeDirect || WarpWheel::promptPending();
    // A pending result must block new consumers without trapping an overlay
    // that was already open before the finish was recorded.
    const bool sessionBlocksNewInput = sessionModalBeforeDirect ||
        (sessionResultBeforeDirect &&
         !menuOpenBeforeDirect && !wheelOwnsInputBeforeDirect);
    if (sessionResultBeforeDirect) {
        WarpWheel::suppressClassicInstantUntilRelease();
    }
    if (sessionBlocksNewInput) gBinds.suppressUntilRelease();
    if (sessionModalBeforeDirect && gpApplication.mGamePads[0]) {
        // The modal dismisses from raw PAD state. Do not leave its fresh edge
        // queued in the retail pad when the frozen director resumes.
        gpApplication.mGamePads[0]->mButtons.mInput = 0;
        gpApplication.mGamePads[0]->mButtons.mFrameInput = 0;
        gpApplication.mGamePads[0]->mButtons.mRapidInput = 0;
    }
    gQFTTimer.beginFrame();
    SplitStats::beginFrame();
    gQFTTimer.update();
    GhostModel::beginFrame();
    // Before direct(): while the wheel is open it takes the pad away from
    // the game. A PB save can hide the prompt behind the Ghosts tab, but its
    // held action still needs storage ACK polling when warps are disabled.
    if (!creationEditing &&
        (!sessionResultBeforeDirect || wheelOwnsInputBeforeDirect) &&
        (!gSettings.getBool(SETTING_DISABLE_WARPS) ||
         wheelOpenBeforeDirect || WarpWheel::promptPending()))
        WarpWheel::update(gpApplication.mGamePads[0]);
    PatternSelector::update(!creationEditing && !sessionResultBeforeDirect);
    rngControlApply();

    // Freeze the stage while an overlay is up. direct() runs the movement and
    // animation perform lists only outside the pause and stage-exit states, so
    // lending it one of those for the call is the entire pause; state 12 is the
    // one whose own branch does nothing while the fader is up. Any app state it
    // did produce would be a state change we never asked for, so drop it.
    const bool freeze = gpMarDirector &&
                        gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
                        ((gMenu && gMenu->shown()) || WarpWheel::shown() ||
                         sessionModalBeforeDirect);
    const bool marioActive = gpMarDirector &&
                             gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
                             !freeze;
    WallkickDisplay::beforeDirect(marioActive);
    MovementDisplay::beforeDirect(marioActive);
    GameplayPolish::beforeDirect();
    if (freeze) {
        gpMarDirector->mCurState = TMarDirector::STATE_STAGE_EXIT_2;
    }
    Ghost::beforeDirect();
    SplitEvents::beginFrame();
    TMarioGamePad *const retailPad = gpApplication.mGamePads[0];
    RetailPadInputSnapshot retailInput;
    if (menuOwnsRetailPad && retailPad)
        suppressRetailPad(retailPad, retailInput);
    int state = director->direct();
    if (menuOwnsRetailPad && retailPad)
        restoreRetailPad(retailPad, retailInput);
    if (freeze) {
        gpMarDirector->mCurState = TMarDirector::STATE_NORMAL;
        state = 0;
    }
    Ghost::afterDirect(state);
    WallkickDisplay::afterDirect(marioActive);
    MovementDisplay::afterDirect(marioActive);
    GameplayPolish::afterDirect();
    if (gSettings.getBool(SETTING_DISABLE_WARPS) &&
        !WarpWheel::retailExitPending()) {
        LevelWarp::cancelPending(true);
    } else {
        state = LevelWarp::onDirected(state);
    }

    // Exit Area is identified inside direct(). Delay the one pre-direct
    // bind-driven overlay toggle so its confirming A cannot leak into it.
    const bool sessionResultAfterDirect = StageLoader::resultOwnsInput();
    if (!creationEditing && !sessionResultAfterDirect)
        gInputDisplay.update();

#if IS_EMULATOR
    EmulatorPersistence::service();
#endif

    gQFTTimer.update();
    SplitEvents::update();
    const bool observerFrame = Ghost::observerStatsSuppressed();
    Ghost::update();
    Records::update(creationEditing, observerFrame);
    ILing::update();
    StageLoader::update();
    SplitStats::update();
    WarpWheel::resolveDeferredRestart();
    const bool sessionOwnsInput = sessionResultBeforeDirect ||
                                  sessionResultAfterDirect ||
                                  StageLoader::resultOwnsInput();
    GhostStorage::update();
    RecordsPersistence::update();
    if (observerFrame || !creationEditing)
        gAttemptCounter.update(observerFrame);

    // Apply/restore the toggled memory-patch features (ported gecko codes).
    // Runs every frame like the gecko handler; no-ops when nothing changed.
    featuresApply();

    actionsApply(!creationEditing && !sessionOwnsInput);
    gCreationExtras.update();

    if (gSavestateMgr && !creationEditing && !sessionOwnsInput) {
        gSavestateMgr->updateHook();
    }
    const bool allowExistingMenuToClose =
        StageLoader::resultOwnsInput() && !StageLoader::modal() &&
        menuOpenBeforeDirect;
    if (gMenu && (!sessionOwnsInput || allowExistingMenuToClose)) {
        gMenu->update(gpApplication.mGamePads[0]);
    }
#if ENABLE_MEM_DIAGNOSTICS
    memDiagnosticsUpdate();
#endif

    return state;
}

extern "C" void afterDraw() {
    // The original call is a full GXDrawDone barrier. Process queued loads
    // immediately afterward: director, fader, audio, and the current frame's
    // GPU work are all complete, while the next game frame has not begun.
    THPPlayerDrawDone();
    if (gSavestateMgr && !gQftDisplay.editing() && !gInputDisplay.editing() &&
        !gMetadataDisplay.editing() && !gCreationExtras.editing() &&
        !StageLoader::resultOwnsInput())
        gSavestateMgr->processPendingLoad();
    // gpPollution is stale until the async setup thread reaches onSetup.
    if (gpMarDirector && gpMarDirector->_260 != 0 &&
        gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING) {
        visibleGoopUpdate();
    }

    {
        J2DOrthoGraph ortho(0, 0, 640, 480);
        ortho.setup2D();

        GXSetViewport(0, 0, 640, 480, 0, 1);
        {
            Mtx44 mtx;
            C_MTXOrtho(mtx, 0, 480,0, 640, -1, 1);
            GXSetProjection(mtx, GX_ORTHOGRAPHIC);
        }        
        
        if (gMenu)
            gMenu->draw(&ortho);
        GameplayPolish::draw(gMenu);
        if (gSavestateMgr)
            gSavestateMgr->draw(gMenu);
#if ENABLE_MEM_DIAGNOSTICS
        memDiagnosticsDraw(gMenu);
#endif
        ILing::draw(gMenu);
        StageLoader::draw(gMenu);
        const bool sessionModal = StageLoader::modal();
        if (!sessionModal && (!gMenu || !gMenu->shown()))
            PatternSelector::draw(gMenu);
        if (!sessionModal && (!gMenu || !gMenu->shown()) &&
            !WarpWheel::shown())
            PracticeVisuals::draw(gMenu);
        if (!sessionModal &&
            (!gSettings.getBool(SETTING_DISABLE_WARPS) || WarpWheel::shown()))
            WarpWheel::draw();
        if (gMenu)
            gMenu->drawInvalidIlWarning();
    }
}
