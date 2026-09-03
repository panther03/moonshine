#ifndef _SUSAMUNE_CREATION_EXTRAS_HXX
#define _SUSAMUNE_CREATION_EXTRAS_HXX

#include "susamune/creation.hxx"
#include "susamune/susamune_cfg.h"

class J2DPicture;
class J2DPane;
class J2DScreen;
class Menu;
class TMarioGamePad;

extern const char gCreationLettersLower[33];
extern const char gCreationLettersUpper[33];
extern const char gCreationSymbols[33];

void drawCreationKeyboard(Menu *menu, const char *title, const char *text,
                          u8 page, bool uppercase, u8 cursor);
bool updateCreationKeyboardText(TMarioGamePad *pad, char *text, u8 &length,
                                u8 capacity, u8 &page, bool &uppercase,
                                u8 &cursor);
const char *wallkickDisplayLabel(int index);

class CreationExtras {
public:
    enum {
        MENU_ROW_COUNT = 24,
        HUD_PANE_COUNT = 25,
        PREVIEW_PANE_COUNT = 2,
    };

    void resetDefaults();
    void adopt(const volatile SusamuneCreationCfg *src);
    void stageInto(volatile SusamuneCreationCfg *dst) const;
    void adoptWallkick(const volatile SusamuneWallkickStyleCfg *src);
    void stageWallkickInto(volatile SusamuneWallkickStyleCfg *dst) const;
    void adoptMovement(const volatile SusamuneMovementStyleCfg *src);
    void stageMovementInto(volatile SusamuneMovementStyleCfg *dst) const;

    void onStageSetup();
    void onSavestateLoaded();
    void restoreHudDefaults();
    void update();
    void draw(Menu *menu) const;
    void beginTimerCharacterEditor();
    void beginRecentIlEditor();
    void beginSavestateFeedbackEditor();
    void beginWallkickEditor();
    void beginRolloutEditor();
    void beginDustEditor();
    void beginAchievementBannerEditor();
    void beginToastEditor();
    void beginPbBannerEditor();
    void beginColorEditor(int first, int count, const char *title,
                          const char *names = nullptr);
    void toggleTimerLabel();
    bool timerLabelVisible() const { return mTimerLabelVisible != 0; }
    const CreationStyle &recentIlStyle() const { return mRecentIlStyle; }
    const u8 *recentIlTextRgb() const { return mRecentIlRgb[0]; }
    const CreationStyle &achievementBannerStyle() const {
        return mAchievementBannerStyle;
    }
    bool editingAchievementBanner() const {
        return mEditMode == EDIT_ACHIEVEMENT_BANNER && mEditor.editing();
    }
    const CreationStyle &toastStyle() const { return mToastStyle; }
    const CreationStyle &pbBannerStyle() const { return mPbBannerStyle; }
    const CreationStyle &stageSessionStyle() const {
        return mStageSessionStyle;
    }
    void drawSavestateFeedback(Menu *menu, const char *message) const;
    void drawWallkickDisplay(Menu *menu, const char *message,
                             int color) const;
    void drawRolloutDisplay(Menu *menu, const char *message, int color) const;
    void drawDustDisplay(Menu *menu, const char *message, int color) const;
    void drawToast(Menu *menu, const char *message) const;
    void drawPbBanner(Menu *menu, const char *message) const;
    void drawStageSessionCounter(Menu *menu, const char *message) const;

    static int menuRowCount() { return MENU_ROW_COUNT; }
    static bool menuRowSeparator(int row);
    static const char *menuRowName(int row);
    const char *menuRowValue(int row) const;
    void adjustMenuRow(int row, int direction);

    void updateEditor(TMarioGamePad *pad);
    void drawEditor(Menu *menu) const;
    bool editing() const { return mEditor.editing() || mKeyboard; }

    bool dirty() const { return mDirty; }
    void clearDirty() { mDirty = false; }
    const u8 *menuBackground() const {
        return mColors[SUSAMUNE_CREATION_MENU_BG];
    }
private:
    enum EditMode {
        EDIT_NONE,
        EDIT_COLOR,
        EDIT_TIMER,
        EDIT_WORD_STYLE,
        EDIT_RECENT_ILS,
        EDIT_SAVESTATE_FEEDBACK,
        EDIT_WALLKICK,
        EDIT_ROLLOUT,
        EDIT_DUST,
        EDIT_ACHIEVEMENT_BANNER,
        EDIT_TOAST,
        EDIT_PB_BANNER,
        EDIT_STAGE_SESSION,
    };

    static CreationStyle defaultWordStyle(int index);
    static CreationStyle defaultWallkickStyle();
    void beginWordEditor(int index);
    void beginStageSessionEditor();
    void beginKeyboard(int index);
    void updateKeyboard(TMarioGamePad *pad);
    void drawKeyboard(Menu *menu) const;
    void applyHud();
    void beginHudPreview(int color);
    void endHudPreview();
    void addPreviewPane(J2DPane *pane);
    void clampWord(int index);

    CreationStyle mWordStyle[SUSAMUNE_CREATION_WORD_COUNT];
    CreationStyle mRecentIlStyle;
    CreationStyle mSavestateFeedbackStyle;
    CreationStyle mWallkickStyle;
    CreationStyle mRolloutStyle;
    CreationStyle mDustStyle;
    CreationStyle mAchievementBannerStyle;
    CreationStyle mToastStyle;
    CreationStyle mPbBannerStyle;
    CreationStyle mStageSessionStyle;
    CreationStyle mColorStyle;
    u8 mColors[SUSAMUNE_CREATION_COLOR_COUNT][3];
    u8 mDefaultColors[SUSAMUNE_CREATION_COLOR_COUNT][3];
    u8 mColorBackup[SUSAMUNE_CREATION_COLOR_COUNT][3];
    u8 mWordRgb[SUSAMUNE_CREATION_WORD_COUNT]
               [SUSAMUNE_CREATION_WORD_CHARS][3];
    u8 mWordBackup[SUSAMUNE_CREATION_WORD_CHARS][3];
    u8 mRecentIlRgb[1][3];
    u8 mRecentIlBackup[1][3];
    u8 mSavestateFeedbackRgb[1][3];
    u8 mSavestateFeedbackBackup[1][3];
    u8 mWallkickRgb[SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT][3];
    u8 mWallkickBackup[SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT][3];
    u8 mRolloutRgb[SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT][3];
    u8 mRolloutBackup[SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT][3];
    u8 mDustRgb[SUSAMUNE_DUST_STYLE_COLOR_COUNT][3];
    u8 mDustBackup[SUSAMUNE_DUST_STYLE_COLOR_COUNT][3];
    char mWords[SUSAMUNE_CREATION_WORD_COUNT]
               [SUSAMUNE_CREATION_WORD_TEXT_SIZE];
    char mTextBackup[SUSAMUNE_CREATION_WORD_TEXT_SIZE];
    J2DPicture *mHudPictures[HUD_PANE_COUNT];
    J2DScreen *mHudScreen;
    J2DPane *mPreviewPanes[PREVIEW_PANE_COUNT];
    CreationEditor mEditor;
    const char *mEditTitle;
    u8 mWordLength[SUSAMUNE_CREATION_WORD_COUNT];
    u8 mWordVisible[SUSAMUNE_CREATION_WORD_COUNT];
    u8 mEditMode;
    u8 mEditFirst;
    u8 mEditCount;
    u8 mEditWord;
    u8 mPreviewPaneCount;
    u8 mKeyboardCursor;
    u8 mKeyboardPage;
    u8 mKeyboardConfirm;
    u32 mColorPresent;
    u32 mColorPresentBeforeEdit;
    u32 mPreviewVisible;
    u8 mWaterFillDefault[2][3];
    u8 mTimerLabelVisible;
    bool mKeyboard;
    bool mUppercase;
    bool mDirty;
    bool mDirtyBeforeEdit;
};

extern CreationExtras &gCreationExtras;

#endif  // _SUSAMUNE_CREATION_EXTRAS_HXX
