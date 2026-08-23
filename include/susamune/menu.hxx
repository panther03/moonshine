#ifndef _SUSAMUNE_MENU_HXX
#define _SUSAMUNE_MENU_HXX

#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DTextBox.hxx"
#include "SMS/Player/MarioGamePad.hxx"

// =====================================================================
// menu.hxx
//
// The on-screen menu: a tabbed overlay that renders and edits mod state.
// It ONLY handles presentation and navigation -- debug warping lives in
// debug_warp.cpp, settings in settings.*. Each tab is a small object
// implementing the internal MenuTab interface (defined in menu.cpp); Menu is a thin frame
// around a list of them, so a new tab is a new class plus one line in the
// constructor.
//
// Menu allocates nothing per item: it owns a single reusable J2DTextBox
// (drawText below) that it re-points at const strings each draw, so the
// pressured system heap is untouched no matter how many tabs/rows exist.
// =====================================================================

class MenuTab;  // internal to menu.cpp

class Menu {
public:
    Menu();

    // Per-frame input (from onUpdate) and render (from afterDraw).
    void update(TMarioGamePad *pad);
    void draw(J2DOrthoGraph *ortho);

    // Drawn after every other overlay so assisted footage cannot hide it.
    void drawInvalidIlWarning();

    bool shown() const { return mShown; }
    // A tab-owned confirmation may still read A/B directly while configured
    // actions stay silent until those buttons are released.
    bool suppressesBinds() const;
    void hide();

    // Open the Ghosts tab on a save confirmation bound to this exact accepted
    // PB. The caller retains ownership of any action held behind that PB.
    bool openGhostPBSave(u32 token);

    // Menu-only C-stick repeat. This never changes the game's pad state.
    u32 navigationInput(TMarioGamePad *pad);

    // Show a transient status message. Drawn on top of everything and even
    // while the menu is closed, since the thing it usually reports (a settings
    // save) is triggered by closing the menu. `msg` is copied into a small
    // internal buffer, so callers may pass scratch storage. Replaces any
    // message still on screen; expires on its own after kToastFrames.
    void toast(const char *msg);

    // Restore mod settings, binds and layouts, then persist them. IL PBs and
    // Records live in separate mailboxes and are deliberately untouched.
    void factoryReset();

    // Persist a feature-owned settings correction without restaging an
    // in-flight mailbox payload.
    void scheduleSettingsSave();

    // --- helpers a tab uses to render itself (implemented in menu.cpp) ---
    // Draw one line of text with the shared textbox. No allocation: `s` is
    // borrowed (a const literal or a caller-owned scratch buffer).
    void drawText(const char *s, int x, int y, int sizeX, int sizeY, JUtility::TColor color);
#if ENABLE_SAVESTATE_DBG
    // Same renderer with `y` already expressed as J2D's text baseline.
    void drawTextBaseline(const char *s, int x, int y, int sizeX, int sizeY,
                          JUtility::TColor color);
#endif
    // Draw a filled rectangle. Re-asserts the flat 2D GX vertex state first:
    // J2DFillBox relies on the current vertex descriptor, which text drawing
    // (JUTResFont) reconfigures, so a bare J2DFillBox after any text renders as
    // skewed garbage.
    void fillBox(int x, int y, int w, int h, JUtility::TColor color);
    // Same, for a convex polygon: `xy` is `n` interleaved x,y pairs.
    void fillPoly(const s16 *xy, int n, JUtility::TColor color);
    // Draw a closed polygon outline at the input display's original width.
    void strokePoly(const s16 *xy, int n, JUtility::TColor color);
    // Pixel width of `s` at the given cell size (for right-align / tab layout).
    // Reads the font's per-glyph advances, so it matches what drawText lays
    // out -- the font is proportional and any estimate makes right-aligned
    // text jitter as its content changes.
    static int textWidth(const char *s, int sizeX);

    static const int kMaxTabs = 14;
    // How long a toast stays up, in frames (~3s at 60Hz).
    static const int kToastFrames = 180;

private:
    void switchTab(int dir);
    void drawTabStrip(int x, int y, int w);
    int  tabWidth(int i) const;  // drawn width of tab `i`, padding included
    void drawToast();
    // Auto-save on close: fires a save when the menu is dismissed with edits
    // pending, then watches for the launcher's answer and toasts the result.
    void requestSettingsSave();
    void pollSettingsSave();

    J2DTextBox      mText;   // the one reusable textbox; see note above
    J2DOrthoGraph  *mOrtho;  // borrowed from draw(); used to re-enter 2D state
    int             mFontAscent;  // font metrics, cached: baseline<->top offset
    int             mFontHeight;
    bool            mShown;
    int             mCurTab;
    int             mNumTabs;
    int             mTabFirst;      // leftmost tab drawn; the strip scrolls by
                                    // whole tabs so it stays left-justified
    MenuTab        *mTabs[kMaxTabs];
    char            mToastBuf[48];  // toast text, owned (see drawText's note
                                    // on borrowed strings)
    int             mToastFrames;   // frames left before the toast expires
    bool            mSaveWatch;     // a save is in flight; poll for its result
    u8              mCRepeatFrames;
};

// One instance, constructed once at boot (placement-new into BSS -- no heap).
extern Menu *gMenu;
void menuInit();

#endif  // _SUSAMUNE_MENU_HXX
