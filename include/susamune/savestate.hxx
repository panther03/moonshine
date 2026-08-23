#ifndef _SUSAMUNE_SAVESTATE_HXX
#define _SUSAMUNE_SAVESTATE_HXX

class Menu;

class SavestateManager {
public:
    SavestateManager();

    // Called once per frame from main.cpp's onUpdate hook. Polls the d-pad
    // and triggers saves. Loads are queued until the post-render hook so the
    // current frame cannot consume a mixture of live and restored state.
    void updateHook();

    // Called after the game's THPPlayerDrawDone()/GXDrawDone barrier. A queued
    // load is restored here, after director, fader, audio, and rendering work
    // for the current frame has finished.
    void processPendingLoad();

    // Drawn after the scene each frame; the production prompt uses the
    // configurable Creation style and the optional debug label stays separate.
    void draw(Menu *menu);

    // Public so callers can trigger from elsewhere (e.g. a debug menu).
    bool saveState();
    bool loadState();

private:
    void feedback(const char *debug, const char *message);

#if ENABLE_SAVESTATE_DBG
    void setStatus(const char *msg);
#endif
    char mFeedback[48];
    int  mFeedbackFrames;
    bool mLoadPending;
};
static_assert(sizeof(SavestateManager) == 56,
              "savestate controller layout changed");

#endif // _SUSAMUNE_SAVESTATE_HXX
