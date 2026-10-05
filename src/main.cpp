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
#include "susamune/mario_colors.hxx"
#include "susamune/fludd_colors.hxx"
#include "susamune/water_colors.hxx"
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
#include "susamune/state_storage.hxx"
#include "susamune/qft_display.hxx"
#include "susamune/records.hxx"
#include "susamune/practice_visuals.hxx"
#include "susamune/practice_session.hxx"
#include "susamune/retail_input.hxx"
#include "susamune/tas_project.hxx"
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
#include "susamune/movement_timing_display.hxx"
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
// region size leaves the top 8 KiB exposed to heap allocations.
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
    StateStorage::init();
    PracticeSession::init();
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
    if (SavestateManager::diskBusy()) return director->mCurState;
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
        (gBinds.wasPressedRaw(BIND_MENU_TOGGLE) ||
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
    PracticeSession::beforeStageSetup();
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
    MarioColors::onStageSetup();
    FluddColors::onStageSetup();
    WaterColors::onStageSetup();
    if (observerStage)
        Records::invalidateAttempt();
    Records::onStageSetup(director->mAreaID, director->mEpisodeID);
    if (observerStage)
        Records::invalidateAttempt();
    WallkickDisplay::onStageSetup();
    MovementDisplay::onStageSetup();
    MovementTimingDisplay::onStageSetup();
    CrashReport::note(SUSAMUNE_CRASH_EVENT_STAGE_READY,
                      static_cast<u32>(director->mAreaID) << 8 |
                          director->mEpisodeID,
                      director->_260);
#if ENABLE_MEM_DIAGNOSTICS
    memDiagnosticsOnStageSetup();
#endif
    PracticeSession::afterStageSetup();

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
    TMarDirector *const stageDirector = RetailInput::stageDirector();
    const bool stageContext = stageDirector != nullptr;
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

    if (gSavestateMgr) gSavestateMgr->updateDisk();
    else StateStorage::update();
    TasProject::update();
    bool stateDiskBusy = SavestateManager::diskBusy() || TasProject::busy();

    // Sample the pad before direct(), not after: onUpdateGameMode runs inside
    // it and asks whether the menu bind was pressed this frame, which would
    // otherwise be answered from the previous frame's sample.
    gBinds.update();
    if (gMenu && gMenu->suppressesBinds()) {
        gBinds.suppressUntilRelease();
    }
    if (!stageDirector) {
        // Movie skips belong to the movie, even when they resemble a menu bind.
        if (!gBinds.recording()) {
            if (gBinds.wasPressedPracticeRaw(BIND_PRACTICE_STEP)) PracticeSession::requestStep();
            else if (gBinds.wasPressedPracticeRaw(BIND_PRACTICE_PAUSE)) PracticeSession::requestPauseToggle();
            if (gBinds.wasPressed(BIND_PRACTICE_STOP)) PracticeSession::requestStop();
        }
        PracticeSession::beforeDirect(false);
        featuresApply();
        Ghost::beforeDirect();
        const s32 state = director->direct();
        PracticeSession::afterDirect(state, true);
        Ghost::update();
        return state;
    }
    const bool creationEditing = gQftDisplay.editing() ||
                                 gInputDisplay.editing() ||
                                 gMetadataDisplay.editing() ||
                                 gCreationExtras.editing() || MarioColors::editing() || FluddColors::editing();
    const bool sessionModalBeforeDirect = StageLoader::modal();
    const bool sessionResultBeforeDirect = StageLoader::resultOwnsInput();
    const bool menuOpenBeforeDirect = gMenu && gMenu->shown();
    const bool tasCinematic = (PracticeSession::recording() || PracticeSession::replaying()) &&
        stageDirector->mCurState != TMarDirector::STATE_NORMAL &&
        stageDirector->mCurState != TMarDirector::STATE_PAUSE_MENU;
    const bool stepOverridesShortcut = !gBinds.recording() &&
        PracticeSession::paused() && gBinds.wasPressedSubsetRaw(BIND_PRACTICE_STEP);
    bool menuOwnsRetailPad = menuOpenBeforeDirect ||
        (gMenu && !tasCinematic && !stepOverridesShortcut && gBinds.wasPressedRaw(BIND_MENU_TOGGLE));
    const bool wheelOpenBeforeDirect = WarpWheel::shown();
    const bool wheelToggleBeforeDirect = stageDirector->_260 &&
        stageDirector->mCurState == TMarDirector::STATE_NORMAL && !creationEditing && !stateDiskBusy &&
        !sessionResultBeforeDirect && !menuOwnsRetailPad && !stepOverridesShortcut &&
        !gSettings.getBool(SETTING_DISABLE_WARPS) &&
        gBinds.wasPressed(BIND_WARP_WHEEL);
    const bool wheelOwnsInputBeforeDirect =
        wheelOpenBeforeDirect || WarpWheel::promptPending() || wheelToggleBeforeDirect;
    // A pending result must block new consumers without trapping an overlay
    // that was already open before the finish was recorded.
    const bool sessionBlocksNewInput = sessionModalBeforeDirect ||
        (sessionResultBeforeDirect &&
         !menuOpenBeforeDirect && !wheelOwnsInputBeforeDirect);
    if (sessionResultBeforeDirect || menuOwnsRetailPad) {
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
    bool practiceModal = creationEditing || sessionBlocksNewInput ||
        menuOwnsRetailPad || wheelOwnsInputBeforeDirect || stateDiskBusy;
    bool practiceStepConsumed = false;
    if (!practiceModal) {
        const bool pausePressed = !gBinds.recording() &&
            gBinds.wasPressedPracticeRaw(BIND_PRACTICE_PAUSE);
        const bool stepPressed = !gBinds.recording() &&
            (stepOverridesShortcut || gBinds.wasPressedPracticeRaw(BIND_PRACTICE_STEP));
        if (stepPressed) {
            practiceStepConsumed = PracticeSession::requestStep();
            if (practiceStepConsumed) {
                gBinds.suppressUntilRelease();
                WarpWheel::suppressClassicInstantUntilRelease();
            }
        } else if (pausePressed) PracticeSession::requestPauseToggle();
        if (gBinds.wasPressed(BIND_FREE_CAMERA)) PracticeSession::requestFreeCameraToggle();
        if (gBinds.wasPressed(BIND_PRACTICE_RECORD)) PracticeSession::requestRecord();
        if (gBinds.wasPressed(BIND_PRACTICE_REPLAY)) PracticeSession::requestPlayback();
        if (gBinds.wasPressed(BIND_PRACTICE_STOP)) PracticeSession::requestStop();
        for (int id = BIND_TAS_BEGINNING; id <= BIND_TAS_REPLAY; ++id) {
            const BindId bind = static_cast<BindId>(id);
            if (!gBinds.wasPressed(bind) || !TasProject::dispatchShortcut(bind)) continue;
            PracticeSession::stripShortcutButtons(gBinds.get(bind));
            gBinds.suppressUntilRelease();
            WarpWheel::suppressClassicInstantUntilRelease();
            if (gMenu) {
                if (TasProject::promptPending()) gMenu->openTasProject();
                else gMenu->toast(TasProject::status());
            }
            break;
        }
    }
    // A shortcut can queue a state operation or open its confirmation now.
    stateDiskBusy = SavestateManager::diskBusy() || TasProject::busy();
    menuOwnsRetailPad = menuOwnsRetailPad || (gMenu && gMenu->shown());
    practiceModal = practiceModal || stateDiskBusy || menuOwnsRetailPad;
    PracticeSession::beforeDirect(practiceModal);
    gQFTTimer.beginFrame();
    if (PracticeSession::freezeRequested()) gQFTTimer.beginPracticePause();
    SplitStats::beginFrame();
    gQFTTimer.update();
    GhostModel::beginFrame();
    MarioColors::update();
    FluddColors::update();
    // Before direct(): while the wheel is open it takes the pad away from
    // the game. A PB save can hide the prompt behind the Ghosts tab, but its
    // held action still needs storage ACK polling when warps are disabled.
    if (!creationEditing && !stateDiskBusy &&
        (!sessionResultBeforeDirect || wheelOwnsInputBeforeDirect) &&
        (!gSettings.getBool(SETTING_DISABLE_WARPS) ||
         wheelOpenBeforeDirect || WarpWheel::promptPending()))
        WarpWheel::update(gpApplication.mGamePads[0]);
    PatternSelector::update(!menuOwnsRetailPad && !creationEditing &&
                            !sessionResultBeforeDirect && !stateDiskBusy);
    PracticeVisuals::update();
    rngControlApply();

    // Freeze the stage while an overlay is up. direct() runs the movement and
    // animation perform lists only outside the pause and stage-exit states, so
    // lending it one of those for the call is the entire pause; state 12 is the
    // one whose own branch does nothing while the fader is up. Any app state it
    // did produce would be a state change we never asked for, so drop it.
    const bool freeze = stageDirector && stageDirector->_260 &&
                        ((stageDirector->mCurState == TMarDirector::STATE_NORMAL &&
                          (menuOwnsRetailPad || WarpWheel::shown() ||
                           sessionModalBeforeDirect || PracticeSession::freezeRequested() || stateDiskBusy)) ||
                         (stateDiskBusy && SavestateManager::saveDialogOpen()));
    // A direct SD load may start from a save box. Keep that box from advancing
    // into the next area while its state is read, then return to its exact
    // state if the read fails. The CARD worker remains live throughout.
    const u8 heldDirectorState = stageDirector ? stageDirector->mCurState : 0;
    const bool marioActive = gpMarDirector &&
                             gpMarDirector->mCurState == TMarDirector::STATE_NORMAL &&
                             !freeze;
    Ghost::frameControl(freeze || (gpMarDirector &&
        gpMarDirector->mCurState == TMarDirector::STATE_PAUSE_MENU),
        PracticeSession::assisted());
    WallkickDisplay::beforeDirect(marioActive);
    MovementDisplay::beforeDirect(marioActive);
    MovementTimingDisplay::beforeDirect(marioActive);
    GameplayPolish::beforeDirect();
    if (freeze) {
        gpMarDirector->mCurState = TMarDirector::STATE_STAGE_EXIT_2;
    }
    Ghost::beforeDirect();
    SplitEvents::beginFrame();
    TMarioGamePad *const retailPad = gpApplication.mGamePads[0];
    RetailPadInputSnapshot retailInput;
    const bool suppressPad = menuOwnsRetailPad || stateDiskBusy ||
        (PracticeSession::freeCamera() && !PracticeSession::resumingNativePause());
    if (suppressPad && retailPad)
        suppressRetailPad(retailPad, retailInput);
    int state = director->direct();
    if (suppressPad && retailPad)
        restoreRetailPad(retailPad, retailInput);
    if (freeze) {
        gpMarDirector->mCurState = heldDirectorState;
        state = 0;
    }
    PracticeSession::afterDirect(state, !freeze && !menuOwnsRetailPad && !stateDiskBusy);
    Ghost::afterDirect(state);
    WallkickDisplay::afterDirect(marioActive);
    MovementDisplay::afterDirect(marioActive);
    MovementTimingDisplay::afterDirect(marioActive);
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
    if (PracticeSession::assisted()) Ghost::invalidateForAssist();
    SusamunePracticeInput consumedInput;
    if (PracticeSession::consumedInput(&consumedInput))
        Ghost::captureInput(consumedInput);
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

    // State shortcuts accept held gameplay buttons. Keep the entire opening,
    // editing and dismissal frame owned by modals, and let a paused Step win.
    // Dispatch before ordinary action binds so a state shortcut can silence
    // a larger overlapping combo without consuming the held Mario buttons.
    if (gSavestateMgr && !observerFrame && !creationEditing && !sessionOwnsInput && !stateDiskBusy &&
        !practiceModal && !practiceStepConsumed) {
        gSavestateMgr->updateHook();
    }
    actionsApply(!observerFrame && !creationEditing && !sessionOwnsInput && !stateDiskBusy &&
                 !PracticeSession::ownsGameplayInput());
    gCreationExtras.update();
    const bool allowExistingMenuToClose =
        StageLoader::resultOwnsInput() && !StageLoader::modal() &&
        menuOpenBeforeDirect;
    if (gMenu && (!tasCinematic || menuOpenBeforeDirect) &&
        !practiceStepConsumed && (!sessionOwnsInput || allowExistingMenuToClose)) {
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
    TMarDirector *const stageDirector = RetailInput::stageDirector();
    if (stageDirector && stageDirector->_260 && gSavestateMgr && !gQftDisplay.editing() && !gInputDisplay.editing() &&
        !gMetadataDisplay.editing() && !gCreationExtras.editing() && !MarioColors::editing() && !FluddColors::editing() &&
        !StageLoader::resultOwnsInput() && !Ghost::observerStatsSuppressed())
        gSavestateMgr->processPendingLoad();
    PracticeSession::afterDraw();
    TasProject::afterDraw();
    // gpPollution is stale until the async setup thread reaches onSetup.
    if (stageDirector && stageDirector->_260 != 0 &&
        stageDirector->mCurState >= TMarDirector::STATE_GAME_STARTING) {
        visibleGoopUpdate();
    }

    if (!stageDirector || !stageDirector->_260) return;

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
        if (PracticeSession::hideHud()) {
            if (StageLoader::modal()) StageLoader::draw(gMenu);
            if (WarpWheel::shown() || WarpWheel::promptPending())
                WarpWheel::draw();
            return;
        }
        GameplayPolish::draw(gMenu);
        PracticeSession::draw(gMenu);
        if (gMenu && !gMenu->shown())
            Ghost::drawInputs(gMenu, gSettings.get(SETTING_GHOST_INPUTS));
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
    }
}
