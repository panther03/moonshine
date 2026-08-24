#include "susamune/creation_extras.hxx"

#include "Dolphin/mem.h"
#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "JSystem/J2D/J2DPicture.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/menu.hxx"
#include "susamune/packed_text.hxx"
#include "susamune/rng_control.hxx"
#include "susamune/settings.hxx"

const char gCreationLettersLower[33] = "abcdefghijklmnopqrstuvwxyz.,!?-_";
const char gCreationLettersUpper[33] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.,!?-_";
const char gCreationSymbols[33] = "0123456789+-*/=()[]<>!?:;'\"_#.&@";

namespace {

typedef JUtility::TColor Color;

const u32 kHudPaneTags[] = {
    't_ba', 'c_ba', 'r_ba', 'd_ba', 'm_ba', 's_ba', 'm_tx',
    '\0m_x', 'm_n1', 'm_n2', 'm_n3', 't_n1', 't_n2', 't_n3', 't_n4',
    't_n5', 't_n6', 't_n7', 't_n8', 't_n9', 't_n0', 't_c1', 't_c2',
    't_c3', 't_tx',
};
const u8 kHudPaneColors[] = {
    SUSAMUNE_CREATION_TIMER_BG, SUSAMUNE_CREATION_COIN_BG,
    SUSAMUNE_CREATION_RED_BG,
    SUSAMUNE_CREATION_BLUE_BG, SUSAMUNE_CREATION_LIVES_BG,
    SUSAMUNE_CREATION_SHINES_BG, SUSAMUNE_CREATION_LIFE_TEXT,
    SUSAMUNE_CREATION_LIFE_TEXT, SUSAMUNE_CREATION_LIFE_TEXT,
    SUSAMUNE_CREATION_LIFE_TEXT, SUSAMUNE_CREATION_LIFE_TEXT,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 0,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 1,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 2,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 3,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 4,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 5,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 6,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 7,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 8,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 9,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 10,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 11,
    SUSAMUNE_CREATION_TIMER_CHAR_FIRST + 12,
    SUSAMUNE_CREATION_TIMER_LABEL,
};
static_assert(sizeof(kHudPaneTags) / sizeof(kHudPaneTags[0]) ==
                  CreationExtras::HUD_PANE_COUNT,
              "HUD pane cache changed");
static_assert(sizeof(kHudPaneColors) / sizeof(kHudPaneColors[0]) ==
                  CreationExtras::HUD_PANE_COUNT,
              "HUD colour table changed");

constexpr char kTimerNames[] =
    "Normal digit 1\0Normal digit 2\0Normal digit 3\0Normal digit 4\0"
    "Normal digit 5\0Normal digit 6\0Countdown digit 1\0Countdown digit 2\0"
    "Countdown digit 3\0Countdown digit 4\0Separator 1\0Separator 2\0Separator 3";

constexpr char kMenuText[] =
    "HUD\0FLUDD water\0Coin streak\0Red coin streak\0Blue coin streak\0"
    "Lives streak\0Shines streak\0Life counter\0CUSTOM TEXT\0"
    "Word 1 text\0Word 1 style\0Word 1 visible\0"
    "Word 2 text\0Word 2 style\0Word 2 visible\0"
    "Word 3 text\0Word 3 style\0Word 3 visible\0"
    "MOD MENU\0Menu background\0Achievement popup\0"
    "System notifications\0IL PB popup\0Stage session counter";

const u32 kPreviewRootTags[] = {
    '\0t_0', '\0c_0', '\0r_0', '\0d_0', '\0m_0', '\0s_0',
};

u8 sHudBeforeWarning[CreationExtras::HUD_PANE_COUNT][2][3];
u8 sWaterBeforeWarning[2][3];
bool sHudWarningApplied;
bool sHudWarningSnapshotValid;

constexpr int packedEntries(const char *pool, u32 bytes) {
    int count = 1;
    for (u32 i = 0; i + 1 < bytes; i++)
        if (!pool[i]) count++;
    return count;
}
static_assert(packedEntries(kMenuText, sizeof(kMenuText)) ==
                  CreationExtras::MENU_ROW_COUNT,
              "Creation menu row table changed");
static_assert(packedEntries(kTimerNames, sizeof(kTimerNames)) ==
                  SUSAMUNE_CREATION_TIMER_CHAR_COUNT,
              "Sunshine timer colour table changed");

const char kWallkickNames[] =
    "1st\0" "2nd\0" "3rd\0" "4th\0" "5th\0" "6th\0" "Late";
const char kRolloutNames[] = "1f\0" "2f\0" "3f\0" "4f\0" "5f";

inline int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void copyRgb(u8 dst[3], const u8 src[3]) {
    for (int c = 0; c < 3; c++) dst[c] = src[c];
}

void saveRgb(u8 dst[3], const Color &src) {
    dst[0] = src.r;
    dst[1] = src.g;
    dst[2] = src.b;
}

void loadRgb(Color &dst, const u8 src[3]) {
    dst.r = src[0];
    dst.g = src[1];
    dst.b = src[2];
}

void makeRed(Color &dst) {
    dst.r = 255;
    dst.g = 0;
    dst.b = 0;
}

bool isRed(const Color &color) {
    return color.r == 255 && color.g == 0 && color.b == 0;
}

void snapshotWarningColors(J2DPicture *const *pictures) {
    for (u32 i = 0; i < CreationExtras::HUD_PANE_COUNT; i++) {
        J2DPicture *picture = pictures[i];
        if (!picture) continue;
        saveRgb(sHudBeforeWarning[i][0], picture->mColorMask);
        saveRgb(sHudBeforeWarning[i][1], picture->mColorOverlay);
    }
    if (!gpMarDirector || !gpMarDirector->mGCConsole) return;
    saveRgb(sWaterBeforeWarning[0],
            gpMarDirector->mGCConsole->mWaterLeftPanelColor);
    saveRgb(sWaterBeforeWarning[1],
            gpMarDirector->mGCConsole->mWaterRightPanelColor);
}

bool sameRgb(const u8 a[3], const u8 b[3]) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

void clampStyle(CreationStyle &style) {
    style.x = (u16)clampi(style.x, 0, 640);
    style.y = (u16)clampi(style.y, 0, 456);
    style.scale = (u8)clampi(style.scale, 50, 200);
    style.textBrightness = (u8)clampi(style.textBrightness, 25, 200);
    if (style.padding != 0xff)
        style.padding = (u8)clampi(style.padding, 0, 16);
}

void loadStyle(CreationStyle &style, const volatile void *source) {
    memcpy(&style, const_cast<const void *>(source), sizeof(style));
    clampStyle(style);
}

void storeStyle(volatile void *destination, const CreationStyle &style) {
    memcpy(const_cast<void *>(destination), &style, sizeof(style));
}

void loadMovementOverlay(
    CreationStyle &style, u8 (*rgb)[3], int colors,
    const volatile SusamuneMovementOverlayStyleCfg *source) {
    loadStyle(style, &source->x);
    memcpy(rgb, (const void *)source->rgb, colors * 3);
}

void storeMovementOverlay(
    volatile SusamuneMovementOverlayStyleCfg *destination,
    const CreationStyle &style, const u8 (*rgb)[3], int colors) {
    storeStyle(&destination->x, style);
    memcpy((void *)destination->rgb, rgb, colors * 3);
}

}  // namespace

CreationExtras &gCreationExtras = *reinterpret_cast<CreationExtras *>(
    SUSAMUNE_MEM2_CREATION_RUNTIME_PPC_BASE);
static_assert(sizeof(CreationExtras) <= SUSAMUNE_CREATION_RUNTIME_SIZE,
              "Creation extras exceed their MEM2 runtime window");

const char *wallkickDisplayLabel(int index) {
    return PackedText::at(kWallkickNames, index);
}

void drawCreationKeyboard(Menu *menu, const char *title, const char *text,
                          u8 pageIndex, bool uppercase, u8 cursor) {
    menu->fillBox(70, 188, 500, 262, Color(8, 11, 20, 238));
    menu->drawText(title, 86, 202, 17, 17, Color(255, 255, 255, 255));
    if (text) {
        menu->fillBox(86, 220, 468, 24, Color(24, 34, 54, 255));
        menu->drawText(text, 98, 223, 14, 14, Color(120, 220, 150, 255));
    }
    const char *page = pageIndex ? gCreationSymbols
                       : uppercase ? gCreationLettersUpper
                                   : gCreationLettersLower;
    const int count = pageIndex ? (int)sizeof(gCreationSymbols) - 1 : 32;
    for (int i = 0; i < count; i++) {
        const int x = 112 + (i % 8) * 46;
        const int y = 250 + (i / 8) * 37;
        if (i == cursor)
            menu->fillBox(x - 9, y - 5, 32, 29, Color(90, 170, 255, 100));
        char one[2] = {page[i], '\0'};
        menu->drawText(one, x, y, 18, 18,
                       i == cursor ? Color(255, 255, 255, 255)
                                   : Color(200, 206, 220, 255));
    }
    menu->drawText("D-pad Select   A Type   B Delete   X Space   Y Case   L"
                   SUSAMUNE_GLYPH_SLASH "R Page",
                   86, 401, 10, 10, Color(104, 114, 136, 255));
    menu->drawText("START: Keep   X+START: Cancel   Z: Clear",
                   86, 421, 10, 10, Color(104, 114, 136, 255));
}

bool updateCreationKeyboardText(TMarioGamePad *pad, char *text, u8 &length,
                                u8 capacity, u8 &pageIndex, bool &uppercase,
                                u8 &cursor) {
    const u32 pressed = pad->mButtons.mRapidInput;
    bool changed = false;
    const int count = pageIndex ? (int)sizeof(gCreationSymbols) - 1 : 32;
    if (pressed & TMarioGamePad::DPAD_LEFT)
        cursor = (u8)((cursor + count - 1) % count);
    else if (pressed & TMarioGamePad::DPAD_RIGHT)
        cursor = (u8)((cursor + 1) % count);
    else if (pressed & TMarioGamePad::DPAD_UP)
        cursor = (u8)((cursor + count - 8) % count);
    else if (pressed & TMarioGamePad::DPAD_DOWN)
        cursor = (u8)((cursor + 8) % count);
    if (pressed & (TMarioGamePad::L | TMarioGamePad::R)) {
        pageIndex ^= 1;
        cursor = 0;
    }
    if (pressed & TMarioGamePad::Y) uppercase = !uppercase;
    if ((pressed & TMarioGamePad::B) && length) {
        text[--length] = '\0';
        changed = true;
    }
    if ((pressed & TMarioGamePad::X) && length < capacity) {
        text[length++] = ' ';
        text[length] = '\0';
        changed = true;
    }
    if ((pressed & TMarioGamePad::A) && length < capacity) {
        const char *characters = pageIndex ? gCreationSymbols
                                 : uppercase ? gCreationLettersUpper
                                             : gCreationLettersLower;
        text[length++] = characters[cursor];
        text[length] = '\0';
        changed = true;
    }
    return changed;
}

CreationStyle CreationExtras::defaultWordStyle(int index) {
    return CreationStyle{
        220, (u16)(80 + index * 42), 100, 255,
        0, 0, 0, 128, 100, 2,
    };
}

static CreationStyle defaultRecentIlStyle() {
    return CreationStyle{
        382, 92, 100, 255,
        12, 20, 34, 205, 100, 10,
    };
}

static CreationStyle defaultSavestateFeedbackStyle() {
    return CreationStyle{
        30, 418, 80, 255,
        0, 0, 0, 200, 100, 8,
    };
}

static CreationStyle defaultAchievementBannerStyle() {
    return CreationStyle{
        115, 96, 100, 255,
        0, 0, 0, 225, 100, 4,
    };
}

static CreationStyle defaultToastStyle() {
    return CreationStyle{
        20, 412, 100, 255,
        0, 0, 0, 200, 100, 6,
    };
}

static CreationStyle defaultPbBannerStyle() {
    return CreationStyle{
        320, 42, 100, 255,
        90, 58, 4, 230, 100, 10,
    };
}

static CreationStyle defaultStageSessionStyle() {
    return CreationStyle{
        570, 40, 100, 255,
        8, 12, 20, 210, 100, 8,
    };
}

CreationStyle CreationExtras::defaultWallkickStyle() {
    return CreationStyle{
        300, 106, 90, 255,
        0, 0, 0, 185, 100, 5,
    };
}

void CreationExtras::resetDefaults() {
    Creation::fillWhite(mColors, SUSAMUNE_CREATION_COLOR_COUNT);
    Creation::fillWhite(mDefaultColors, SUSAMUNE_CREATION_COLOR_COUNT);
    const u8 menuBg[] = {24, 28, 40};
    copyRgb(mColors[SUSAMUNE_CREATION_MENU_BG], menuBg);
    copyRgb(mDefaultColors[SUSAMUNE_CREATION_MENU_BG], menuBg);
    for (int word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++) {
        mWordStyle[word] = defaultWordStyle(word);
        Creation::fillWhite(mWordRgb[word], SUSAMUNE_CREATION_WORD_CHARS);
        snprintf(mWords[word], SUSAMUNE_CREATION_WORD_TEXT_SIZE,
                 "Custom Text %d", word + 1);
        mWordLength[word] = (u8)strlen(mWords[word]);
        mWordVisible[word] = 0;
    }
    mRecentIlStyle = defaultRecentIlStyle();
    Creation::fillWhite(mRecentIlRgb, 1);
    mSavestateFeedbackStyle = defaultSavestateFeedbackStyle();
    Creation::fillWhite(mSavestateFeedbackRgb, 1);
    mWallkickStyle = defaultWallkickStyle();
    Creation::fillWhite(mWallkickRgb, SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT);
    mRolloutStyle = defaultWallkickStyle();
    Creation::fillWhite(mRolloutRgb, SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT);
    mDustStyle = defaultWallkickStyle();
    Creation::fillWhite(mDustRgb, SUSAMUNE_DUST_STYLE_COLOR_COUNT);
    mAchievementBannerStyle = defaultAchievementBannerStyle();
    mToastStyle = defaultToastStyle();
    mPbBannerStyle = defaultPbBannerStyle();
    mStageSessionStyle = defaultStageSessionStyle();
    for (u32 i = 0; i < sizeof(mHudPictures) / sizeof(mHudPictures[0]); i++)
        mHudPictures[i] = nullptr;
    mHudScreen = nullptr;
    mColorStyle = CreationStyle{
        0xffff, 0xffff, 100, 255, 0, 0, 0, 0xff, 100, 0xff,
    };
    mPreviewPaneCount = 0;
    mPreviewVisible = 0;
    mEditor.reset();
    mEditTitle = nullptr;
    mEditMode = EDIT_NONE;
    mKeyboard = false;
    mUppercase = false;
    mKeyboardConfirm = 0;
    mColorPresent = 0;
    mColorPresentBeforeEdit = 0;
    mTimerLabelVisible = 1;
    mDirty = false;
    mDirtyBeforeEdit = false;
}

void CreationExtras::clampWord(int index) {
    CreationStyle &s = mWordStyle[index];
    clampStyle(s);
    mWordLength[index] = (u8)clampi(mWordLength[index], 0,
                                    SUSAMUNE_CREATION_WORD_CHARS);
    mWords[index][mWordLength[index]] = '\0';
    mWordVisible[index] = mWordVisible[index] ? 1 : 0;
}

void CreationExtras::adopt(const volatile SusamuneCreationCfg *src) {
    if (!src || src->magic != SUSAMUNE_CREATION_CFG_MAGIC ||
        src->version != SUSAMUNE_CREATION_CFG_VERSION) return;
    for (int i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++) {
        if (src->colorPresent & SUSAMUNE_CREATION_COLOR(i))
            for (int c = 0; c < 3; c++) mColors[i][c] = src->rgb[i][c];
    }
    mColorPresent = src->colorPresent &
                    ((1u << SUSAMUNE_CREATION_COLOR_COUNT) - 1u);
    mColorPresent &=
        ~SUSAMUNE_CREATION_COLOR(SUSAMUNE_CREATION_LEGACY_WATER_TEXT) &
        ~SUSAMUNE_CREATION_COLOR(SUSAMUNE_CREATION_LEGACY_MARIO_HAT);
    if (src->timerLabelVisiblePresent)
        mTimerLabelVisible = src->timerLabelVisible ? 1 : 0;
    if (src->recentIlPositionPresent) {
        mRecentIlStyle.x = src->recentIlX;
        mRecentIlStyle.y = src->recentIlY;
        mRecentIlStyle.scale = src->recentIlScale;
        clampStyle(mRecentIlStyle);
    }
    if (src->reserved0 == SUSAMUNE_CREATION_RECENT_STYLE_MAGIC) {
        for (int c = 0; c < 3; c++)
            mRecentIlRgb[0][c] = src->recentIlTextRgb[c];
        mRecentIlStyle.textA = src->recentIlTextA;
        mRecentIlStyle.bgR = src->recentIlBgR;
        mRecentIlStyle.bgG = src->recentIlBgG;
        mRecentIlStyle.bgB = src->recentIlBgB;
        mRecentIlStyle.bgA = src->recentIlBgA;
        mRecentIlStyle.textBrightness = src->recentIlTextBrightness;
        mRecentIlStyle.padding = src->recentIlPadding;
        clampStyle(mRecentIlStyle);
    }
    if (src->savestateStyleMagic ==
        SUSAMUNE_CREATION_SAVESTATE_STYLE_MAGIC) {
        loadStyle(mSavestateFeedbackStyle, &src->savestateX);
        memcpy(mSavestateFeedbackRgb, (const void *)src->savestateTextRgb,
               sizeof(mSavestateFeedbackRgb));
    }
    if (src->achievementStyleMagic ==
        SUSAMUNE_CREATION_ACHIEVEMENT_STYLE_MAGIC) {
        mAchievementBannerStyle.x = src->achievementX;
        mAchievementBannerStyle.y = src->achievementY;
        mAchievementBannerStyle.scale = src->achievementScale;
        clampStyle(mAchievementBannerStyle);
    }
    if (src->stageSessionStyleMagic ==
        SUSAMUNE_CREATION_STAGE_SESSION_STYLE_MAGIC) {
        mStageSessionStyle.x = src->stageSessionX;
        mStageSessionStyle.y = src->stageSessionY;
        mStageSessionStyle.scale = src->stageSessionScale;
        clampStyle(mStageSessionStyle);
    }
    for (int word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++) {
        const volatile SusamuneCreationWordCfg &in = src->words[word];
        loadStyle(mWordStyle[word], &in.x);
        mWordVisible[word] = in.visible;
        mWordLength[word] = in.length;
        memcpy(mWords[word], (const void *)in.text, sizeof(mWords[word]));
        memcpy(mWordRgb[word], (const void *)in.rgb, sizeof(mWordRgb[word]));
        clampWord(word);
    }
    mDirty = false;
}

void CreationExtras::stageInto(volatile SusamuneCreationCfg *dst) const {
    memset((void *)dst, 0, sizeof(*dst));
    dst->magic = SUSAMUNE_CREATION_CFG_MAGIC;
    dst->version = SUSAMUNE_CREATION_CFG_VERSION;
    dst->reserved0 = SUSAMUNE_CREATION_RECENT_STYLE_MAGIC;
    dst->colorPresent = mColorPresent;
    dst->recentIlScale = mRecentIlStyle.scale;
    dst->recentIlX = mRecentIlStyle.x;
    dst->recentIlY = mRecentIlStyle.y;
    dst->recentIlPositionPresent = 1;
    dst->timerLabelVisible = mTimerLabelVisible;
    dst->timerLabelVisiblePresent = 1;
    memcpy((void *)dst->rgb, mColors, sizeof(mColors));
    for (int word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++) {
        volatile SusamuneCreationWordCfg &out = dst->words[word];
        storeStyle(&out.x, mWordStyle[word]);
        out.visible = mWordVisible[word]; out.length = mWordLength[word];
        memcpy((void *)out.text, mWords[word], sizeof(mWords[word]));
        memcpy((void *)out.rgb, mWordRgb[word], sizeof(mWordRgb[word]));
    }
    for (int c = 0; c < 3; c++)
        dst->recentIlTextRgb[c] = mRecentIlRgb[0][c];
    dst->recentIlTextA = mRecentIlStyle.textA;
    dst->recentIlBgR = mRecentIlStyle.bgR;
    dst->recentIlBgG = mRecentIlStyle.bgG;
    dst->recentIlBgB = mRecentIlStyle.bgB;
    dst->recentIlBgA = mRecentIlStyle.bgA;
    dst->recentIlTextBrightness = mRecentIlStyle.textBrightness;
    dst->recentIlPadding = mRecentIlStyle.padding;
    dst->savestateStyleMagic = SUSAMUNE_CREATION_SAVESTATE_STYLE_MAGIC;
    storeStyle(&dst->savestateX, mSavestateFeedbackStyle);
    memcpy((void *)dst->savestateTextRgb, mSavestateFeedbackRgb,
           sizeof(mSavestateFeedbackRgb));
    dst->achievementStyleMagic =
        SUSAMUNE_CREATION_ACHIEVEMENT_STYLE_MAGIC;
    dst->reservedAchievement0 = 0;
    dst->achievementX = mAchievementBannerStyle.x;
    dst->achievementY = mAchievementBannerStyle.y;
    dst->achievementScale = mAchievementBannerStyle.scale;
    dst->stageSessionStyleMagic =
        SUSAMUNE_CREATION_STAGE_SESSION_STYLE_MAGIC;
    dst->stageSessionX = mStageSessionStyle.x;
    dst->stageSessionY = mStageSessionStyle.y;
    dst->stageSessionScale = mStageSessionStyle.scale;
    dst->stageSessionReserved = 0;
}

void CreationExtras::adoptWallkick(
    const volatile SusamuneWallkickStyleCfg *src) {
    if (!src || src->magic != SUSAMUNE_WALLKICK_STYLE_MAGIC ||
        src->version == 0 || src->version > SUSAMUNE_WALLKICK_STYLE_VERSION)
        return;
    loadStyle(mWallkickStyle, &src->x);
    memcpy(mWallkickRgb, (const void *)src->rgb, sizeof(mWallkickRgb));
    if (src->version >= 2 &&
        src->notificationStyleMagic == SUSAMUNE_NOTIFICATION_STYLE_MAGIC) {
        mToastStyle.x = src->toastX;
        mToastStyle.y = src->toastY;
        mToastStyle.scale = src->toastScale;
        mPbBannerStyle.x = src->pbPopupX;
        mPbBannerStyle.y = src->pbPopupY;
        mPbBannerStyle.scale = src->pbPopupScale;
        clampStyle(mToastStyle);
        clampStyle(mPbBannerStyle);
    }
}

void CreationExtras::stageWallkickInto(
    volatile SusamuneWallkickStyleCfg *dst) const {
    dst->magic = SUSAMUNE_WALLKICK_STYLE_MAGIC;
    dst->version = SUSAMUNE_WALLKICK_STYLE_VERSION;
    dst->reserved0 = 0;
    storeStyle(&dst->x, mWallkickStyle);
    memcpy((void *)dst->rgb, mWallkickRgb, sizeof(mWallkickRgb));
    dst->notificationStyleMagic = SUSAMUNE_NOTIFICATION_STYLE_MAGIC;
    dst->toastX = mToastStyle.x;
    dst->toastY = mToastStyle.y;
    dst->toastScale = mToastStyle.scale;
    dst->reservedNotification0 = 0;
    dst->pbPopupX = mPbBannerStyle.x;
    dst->pbPopupY = mPbBannerStyle.y;
    dst->pbPopupScale = mPbBannerStyle.scale;
    memset((void *)dst->reserved1, 0, sizeof(dst->reserved1));
}

void CreationExtras::adoptMovement(
    const volatile SusamuneMovementStyleCfg *src) {
    if (!src || src->magic != SUSAMUNE_MOVEMENT_STYLE_MAGIC ||
        src->version == 0 || src->version > SUSAMUNE_MOVEMENT_STYLE_VERSION)
        return;
    loadMovementOverlay(mRolloutStyle, mRolloutRgb,
                        SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT, &src->rollout);
    loadMovementOverlay(mDustStyle, mDustRgb,
                        SUSAMUNE_DUST_STYLE_COLOR_COUNT, &src->dust);
}

void CreationExtras::stageMovementInto(
    volatile SusamuneMovementStyleCfg *dst) const {
    memset((void *)dst, 0, sizeof(*dst));
    dst->magic = SUSAMUNE_MOVEMENT_STYLE_MAGIC;
    dst->version = SUSAMUNE_MOVEMENT_STYLE_VERSION;
    storeMovementOverlay(&dst->rollout, mRolloutStyle, mRolloutRgb,
                         SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT);
    storeMovementOverlay(&dst->dust, mDustStyle, mDustRgb,
                         SUSAMUNE_DUST_STYLE_COLOR_COUNT);
}

void CreationExtras::onStageSetup() {
    sHudWarningApplied = false;
    sHudWarningSnapshotValid = false;
    memset(mHudPictures, 0, sizeof(mHudPictures));
    mHudScreen = nullptr;
    if (!gpMarDirector || !gpMarDirector->mGCConsole ||
        !gpMarDirector->mGCConsole->mMainScreen) return;
    J2DScreen *screen = gpMarDirector->mGCConsole->mMainScreen;
    mHudScreen = screen;
    mPreviewPaneCount = 0;
    mPreviewVisible = 0;
    u32 captured = 0;
    for (u32 i = 0; i < HUD_PANE_COUNT; i++) {
        J2DPane *pane = screen->search(kHudPaneTags[i]);
        if (pane && pane->mTypeMagic == 'PIC1') {
            mHudPictures[i] = static_cast<J2DPicture *>(pane);
            const u32 bit = SUSAMUNE_CREATION_COLOR(kHudPaneColors[i]);
            if (!(captured & bit)) {
                const JUtility::TColor &original = mHudPictures[i]->mColorMask;
                mDefaultColors[kHudPaneColors[i]][0] = original.r;
                mDefaultColors[kHudPaneColors[i]][1] = original.g;
                mDefaultColors[kHudPaneColors[i]][2] = original.b;
                if (!(mColorPresent & bit))
                    copyRgb(mColors[kHudPaneColors[i]],
                            mDefaultColors[kHudPaneColors[i]]);
                captured |= bit;
            }
        }
    }

    const JUtility::TColor water[] = {
        gpMarDirector->mGCConsole->mWaterLeftPanelColor,
        gpMarDirector->mGCConsole->mWaterRightPanelColor,
    };
    for (int i = 0; i < 2; i++) {
        mWaterFillDefault[i][0] = water[i].r;
        mWaterFillDefault[i][1] = water[i].g;
        mWaterFillDefault[i][2] = water[i].b;
    }
    if (!(mColorPresent & SUSAMUNE_CREATION_COLOR(
              SUSAMUNE_CREATION_FLUDD_WATER))) {
        copyRgb(mDefaultColors[SUSAMUNE_CREATION_FLUDD_WATER],
                mWaterFillDefault[0]);
        copyRgb(mColors[SUSAMUNE_CREATION_FLUDD_WATER],
                mWaterFillDefault[0]);
    }

    // A same-scenario savestate may outlive a restart of this stage heap.
    snapshotWarningColors(mHudPictures);
    sHudWarningSnapshotValid = true;
    applyHud();
}

void CreationExtras::onSavestateLoaded() {
    if (!gpMarDirector || !gpMarDirector->mGCConsole ||
        gpMarDirector->mGCConsole->mMainScreen != mHudScreen) return;
    applyHud();
}

void CreationExtras::applyHud() {
    const bool warning = rngControlInvalidatesIl();
    if (warning) {
        if (!sHudWarningApplied) {
            snapshotWarningColors(mHudPictures);
            sHudWarningApplied = true;
            sHudWarningSnapshotValid = true;
        }
        for (u32 i = 0; i < HUD_PANE_COUNT; i++) {
            J2DPicture *picture = mHudPictures[i];
            if (!picture) continue;
            makeRed(picture->mColorMask);
            makeRed(picture->mColorOverlay);
        }
        if (!mTimerLabelVisible && mHudPictures[HUD_PANE_COUNT - 1])
            mHudPictures[HUD_PANE_COUNT - 1]->mIsVisible = false;
        if (gpMarDirector && gpMarDirector->mGCConsole) {
            makeRed(gpMarDirector->mGCConsole->mWaterLeftPanelColor);
            makeRed(gpMarDirector->mGCConsole->mWaterRightPanelColor);
        }
        return;
    }

    if (sHudWarningApplied) {
        for (u32 i = 0; i < HUD_PANE_COUNT; i++) {
            J2DPicture *picture = mHudPictures[i];
            if (!picture) continue;
            loadRgb(picture->mColorMask, sHudBeforeWarning[i][0]);
            loadRgb(picture->mColorOverlay, sHudBeforeWarning[i][1]);
        }
        if (gpMarDirector && gpMarDirector->mGCConsole) {
            loadRgb(gpMarDirector->mGCConsole->mWaterLeftPanelColor,
                    sWaterBeforeWarning[0]);
            loadRgb(gpMarDirector->mGCConsole->mWaterRightPanelColor,
                    sWaterBeforeWarning[1]);
        }
        sHudWarningApplied = false;
    } else if (sHudWarningSnapshotValid) {
        // A savestate made while assisted can restore red stage-heap panes
        // after the setting itself has been turned off.
        for (u32 i = 0; i < HUD_PANE_COUNT; i++) {
            J2DPicture *picture = mHudPictures[i];
            if (!picture || !isRed(picture->mColorMask) ||
                !isRed(picture->mColorOverlay)) continue;
            loadRgb(picture->mColorMask, sHudBeforeWarning[i][0]);
            loadRgb(picture->mColorOverlay, sHudBeforeWarning[i][1]);
        }
        if (gpMarDirector && gpMarDirector->mGCConsole &&
            isRed(gpMarDirector->mGCConsole->mWaterLeftPanelColor) &&
            isRed(gpMarDirector->mGCConsole->mWaterRightPanelColor)) {
            loadRgb(gpMarDirector->mGCConsole->mWaterLeftPanelColor,
                    sWaterBeforeWarning[0]);
            loadRgb(gpMarDirector->mGCConsole->mWaterRightPanelColor,
                    sWaterBeforeWarning[1]);
        }
    }

    for (u32 i = 0; i < HUD_PANE_COUNT; i++) {
        J2DPicture *picture = mHudPictures[i];
        if (!picture) continue;
        if (!(mColorPresent &
              SUSAMUNE_CREATION_COLOR(kHudPaneColors[i]))) continue;
        const u8 *rgb = mColors[kHudPaneColors[i]];
        picture->mColorMask.r = rgb[0];
        picture->mColorMask.g = rgb[1];
        picture->mColorMask.b = rgb[2];
        const u8 color = kHudPaneColors[i];
        // Retail streaks encode their hue in both black/white endpoints.
        if (color >= SUSAMUNE_CREATION_TIMER_BG &&
            color <= SUSAMUNE_CREATION_SHINES_BG) {
            picture->mColorOverlay.r = rgb[0];
            picture->mColorOverlay.g = rgb[1];
            picture->mColorOverlay.b = rgb[2];
        }
    }

    if (!mTimerLabelVisible && mHudPictures[HUD_PANE_COUNT - 1])
        mHudPictures[HUD_PANE_COUNT - 1]->mIsVisible = false;

    if (!gpMarDirector || !gpMarDirector->mGCConsole ||
        !(mColorPresent & SUSAMUNE_CREATION_COLOR(
              SUSAMUNE_CREATION_FLUDD_WATER))) return;
    const u8 *rgb = mColors[SUSAMUNE_CREATION_FLUDD_WATER];
    JUtility::TColor *fill[] = {
        &gpMarDirector->mGCConsole->mWaterLeftPanelColor,
        &gpMarDirector->mGCConsole->mWaterRightPanelColor,
    };
    const bool original = sameRgb(
        rgb, mDefaultColors[SUSAMUNE_CREATION_FLUDD_WATER]);
    for (int i = 0; i < 2; i++) {
        fill[i]->r = original ? mWaterFillDefault[i][0] : rgb[0];
        fill[i]->g = original ? mWaterFillDefault[i][1] : rgb[1];
        fill[i]->b = original ? mWaterFillDefault[i][2] : rgb[2];
    }
}

void CreationExtras::addPreviewPane(J2DPane *pane) {
    if (!pane || mPreviewPaneCount >= PREVIEW_PANE_COUNT) return;
    const u32 index = mPreviewPaneCount++;
    mPreviewPanes[index] = pane;
    if (pane->mIsVisible) mPreviewVisible |= 1u << index;
    pane->mIsVisible = true;
}

void CreationExtras::beginHudPreview(int color) {
    endHudPreview();
    if (!mHudScreen) return;
    int root;
    if (color == SUSAMUNE_CREATION_TIMER_LABEL) {
        root = 0;
    } else {
        if (color < SUSAMUNE_CREATION_TIMER_BG ||
            color > SUSAMUNE_CREATION_SHINES_BG) return;
        root = color - SUSAMUNE_CREATION_TIMER_BG;
    }
    addPreviewPane(mHudScreen->search(kPreviewRootTags[root]));
    for (u32 i = 0; i < HUD_PANE_COUNT; i++)
        if (kHudPaneColors[i] == color) addPreviewPane(mHudPictures[i]);
}

void CreationExtras::endHudPreview() {
    for (u32 i = 0; i < mPreviewPaneCount; i++)
        if (mPreviewPanes[i])
            mPreviewPanes[i]->mIsVisible = (mPreviewVisible & (1u << i)) != 0;
    mPreviewPaneCount = 0;
    mPreviewVisible = 0;
}

void CreationExtras::update() {
    // The cached panes live in the stage heap. Do not follow them while that
    // heap is being torn down or rebuilt by the setup thread.
    if (!gpMarDirector || gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState != TMarDirector::STATE_NORMAL ||
        !gpMarDirector->mGCConsole ||
        gpMarDirector->mGCConsole->mMainScreen != mHudScreen) return;
    applyHud();
    for (u32 i = 0; i < mPreviewPaneCount; i++)
        if (mPreviewPanes[i]) mPreviewPanes[i]->mIsVisible = true;
}

void CreationExtras::draw(Menu *menu) const {
    if (!menu) return;
    for (int i = 0; i < SUSAMUNE_CREATION_WORD_COUNT; i++) {
        if (!mWordVisible[i] || !mWordLength[i]) continue;
        Creation::drawTextBox(menu, mWordStyle[i], mWordRgb[i],
                              SUSAMUNE_CREATION_WORD_CHARS, mWords[i]);
    }
}

bool CreationExtras::menuRowSeparator(int row) {
    return row == 0 || row == 8 || row == 18;
}

const char *CreationExtras::menuRowName(int row) {
    return row >= 0 && row < MENU_ROW_COUNT
               ? PackedText::at(kMenuText, row) : "";
}

const char *CreationExtras::menuRowValue(int row) const {
    if (row >= 9 && row <= 17) {
        const int local = row - 9;
        if (local % 3 == 2)
            return mWordVisible[local / 3] ? "On" : "Off";
    }
    return menuRowSeparator(row) ? "" : "Edit";
}

void CreationExtras::beginColorEditor(int first, int count, const char *title,
                                      const char *names) {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mColorPresentBeforeEdit = mColorPresent;
    mEditMode = EDIT_COLOR;
    mEditFirst = (u8)first;
    mEditCount = (u8)count;
    mEditTitle = title;
    mEditor.begin(&mColorStyle, mColors + first, mColorBackup + first,
                  (u16)count, (u16)count, names ? names : title,
                  CreationEditor::CAP_TEXT_COLOR);
    beginHudPreview(first);
}

void CreationExtras::beginTimerCharacterEditor() {
    if (editing()) return;
    beginColorEditor(SUSAMUNE_CREATION_TIMER_CHAR_FIRST,
                     SUSAMUNE_CREATION_TIMER_CHAR_COUNT,
                     "Sunshine timer characters", kTimerNames);
    mEditMode = EDIT_TIMER;
}

void CreationExtras::restoreHudDefaults() {
    if (!gpMarDirector || gpMarDirector->_260 == 0 ||
        !gpMarDirector->mGCConsole ||
        gpMarDirector->mGCConsole->mMainScreen != mHudScreen) return;
    memcpy(mColors, mDefaultColors, sizeof(mColors));
    mColorPresent = (1u << SUSAMUNE_CREATION_COLOR_COUNT) - 1u;
    mTimerLabelVisible = 1;
    applyHud();
    if (mHudPictures[HUD_PANE_COUNT - 1])
        mHudPictures[HUD_PANE_COUNT - 1]->mIsVisible = true;
}

void CreationExtras::beginRecentIlEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_RECENT_ILS;
    mEditFirst = 0;
    mEditCount = 1;
    mEditTitle = "Recent IL display";
    mEditor.begin(&mRecentIlStyle, mRecentIlRgb, mRecentIlBackup, 1, 0,
                  nullptr, CreationEditor::CAP_ALL);
}

void CreationExtras::beginSavestateFeedbackEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_SAVESTATE_FEEDBACK;
    mEditFirst = 0;
    mEditCount = 1;
    mEditTitle = Settings::name(SETTING_SAVESTATE_FEEDBACK);
    mEditor.begin(&mSavestateFeedbackStyle, mSavestateFeedbackRgb,
                  mSavestateFeedbackBackup, 1, 0, nullptr,
                  CreationEditor::CAP_ALL);
}

void CreationExtras::beginWallkickEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_WALLKICK;
    mEditFirst = 0;
    mEditCount = SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT;
    mEditTitle = Settings::name(SETTING_WALLKICK_DISPLAY);
    mEditor.begin(&mWallkickStyle, mWallkickRgb, mWallkickBackup,
                  SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT,
                  SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT, kWallkickNames,
                  CreationEditor::CAP_ALL);
}

void CreationExtras::beginRolloutEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_ROLLOUT;
    mEditFirst = 0;
    mEditCount = SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT;
    mEditTitle = Settings::name(SETTING_ROLLOUT_DISPLAY);
    mEditor.begin(&mRolloutStyle, mRolloutRgb, mRolloutBackup,
                  SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT,
                  SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT, kRolloutNames,
                  CreationEditor::CAP_ALL);
}

void CreationExtras::beginDustEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_DUST;
    mEditFirst = 0;
    mEditCount = SUSAMUNE_DUST_STYLE_COLOR_COUNT;
    mEditTitle = Settings::name(SETTING_DUST_DISPLAY);
    mEditor.begin(&mDustStyle, mDustRgb, mDustBackup,
                  SUSAMUNE_DUST_STYLE_COLOR_COUNT,
                  SUSAMUNE_DUST_STYLE_COLOR_COUNT, kWallkickNames,
                  CreationEditor::CAP_ALL);
}

void CreationExtras::beginAchievementBannerEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_ACHIEVEMENT_BANNER;
    mEditFirst = 0;
    mEditCount = 1;
    mEditTitle = "Achievement popup";
    mEditor.begin(&mAchievementBannerStyle, mRecentIlRgb, mRecentIlBackup,
                  1, 0, nullptr,
                  CreationEditor::CAP_POSITION | CreationEditor::CAP_SCALE);
}

void CreationExtras::beginToastEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_TOAST;
    mEditFirst = 0;
    mEditCount = 1;
    mEditTitle = "System notifications";
    mEditor.begin(&mToastStyle, mRecentIlRgb, mRecentIlBackup,
                  1, 0, nullptr,
                  CreationEditor::CAP_POSITION | CreationEditor::CAP_SCALE);
}

void CreationExtras::beginPbBannerEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_PB_BANNER;
    mEditFirst = 0;
    mEditCount = 1;
    mEditTitle = "IL PB popup";
    mEditor.begin(&mPbBannerStyle, mRecentIlRgb, mRecentIlBackup,
                  1, 0, nullptr,
                  CreationEditor::CAP_POSITION | CreationEditor::CAP_SCALE);
}

void CreationExtras::beginStageSessionEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_STAGE_SESSION;
    mEditFirst = 0;
    mEditCount = 1;
    mEditTitle = "Stage session counter";
    mEditor.begin(&mStageSessionStyle, mRecentIlRgb, mRecentIlBackup,
                  1, 0, nullptr,
                  CreationEditor::CAP_POSITION | CreationEditor::CAP_SCALE);
}

void CreationExtras::drawSavestateFeedback(Menu *menu,
                                           const char *message) const {
    Creation::drawTextBox(menu, mSavestateFeedbackStyle,
                          mSavestateFeedbackRgb, 1, message);
}

void CreationExtras::drawWallkickDisplay(Menu *menu, const char *message,
                                         int color) const {
    if (!menu || !message) return;
    color = clampi(color, 0, SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT - 1);
    Creation::drawTextBox(menu, mWallkickStyle, mWallkickRgb + color, 1,
                          message);
}

void CreationExtras::drawRolloutDisplay(Menu *menu, const char *message,
                                        int color) const {
    if (!menu || !message) return;
    color = clampi(color, 0, SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT - 1);
    Creation::drawTextBox(menu, mRolloutStyle, mRolloutRgb + color, 1,
                          message);
}

void CreationExtras::drawDustDisplay(Menu *menu, const char *message,
                                     int color) const {
    if (!menu || !message) return;
    color = clampi(color, 0, SUSAMUNE_DUST_STYLE_COLOR_COUNT - 1);
    Creation::drawTextBox(menu, mDustStyle, mDustRgb + color, 1, message);
}

void CreationExtras::drawToast(Menu *menu, const char *message) const {
    if (!menu || !message || !message[0]) return;
    const int scale = mToastStyle.scale;
    int size = clampi((16 * scale + 50) / 100, 8, 32);
    const int padX = (10 * scale + 50) / 100;
    const int padY = (6 * scale + 50) / 100;
    while (size > 8 && Menu::textWidth(message, size) + padX * 2 > 640)
        size--;
    const int w = Menu::textWidth(message, size) + padX * 2;
    const int h = size + padY * 2;
    const int x = clampi(mToastStyle.x, 0, 640 - w);
    const int y = clampi(mToastStyle.y, 0, 480 - h);
    menu->fillBox(x, y, w, h, Color(0, 0, 0, 200));
    menu->fillBox(x, y, clampi((3 * scale + 50) / 100, 1, 6), h,
                  Color(90, 170, 255, 255));
    menu->drawText(message, x + padX, y + padY, size, size,
                   Color(255, 255, 255, 255));
}

void CreationExtras::drawPbBanner(Menu *menu, const char *message) const {
    if (!menu || !message || !message[0]) return;
    const int scale = mPbBannerStyle.scale;
    int size = clampi((22 * scale + 50) / 100, 11, 44);
    const int padX = (14 * scale + 50) / 100;
    const int textY = (10 * scale + 50) / 100;
    while (size > 11 && Menu::textWidth(message, size) + padX * 2 > 640)
        size--;
    const int w = Menu::textWidth(message, size) + padX * 2;
    const int h = (42 * scale + 50) / 100;
    const int x = clampi((int)mPbBannerStyle.x - w / 2, 0, 640 - w);
    const int y = clampi(mPbBannerStyle.y, 0, 480 - h);
    menu->fillBox(x, y, w, h, Color(90, 58, 4, 230));
    menu->fillBox(x, y, clampi((4 * scale + 50) / 100, 1, 8), h,
                  Color(255, 196, 40, 255));
    menu->drawText(message, x + padX, y + textY, size, size,
                   Color(255, 239, 178, 255));
}

void CreationExtras::drawStageSessionCounter(Menu *menu,
                                              const char *message) const {
    if (!menu || !message || !message[0]) return;
    const int scale = mStageSessionStyle.scale;
    const int size = clampi((18 * scale + 50) / 100, 9, 36);
    const int padX = (9 * scale + 50) / 100;
    const int padY = (5 * scale + 50) / 100;
    const int w = Menu::textWidth(message, size) + padX * 2;
    const int h = size + padY * 2;
    const int x = clampi(mStageSessionStyle.x, 0, 640 - w);
    const int y = clampi(mStageSessionStyle.y, 0, 480 - h);
    const int bar = clampi((3 * scale + 50) / 100, 1, 6);
    menu->fillBox(x, y, w, h,
                  Color(mStageSessionStyle.bgR, mStageSessionStyle.bgG,
                        mStageSessionStyle.bgB, mStageSessionStyle.bgA));
    menu->fillBox(x, y, bar, h, Color(80, 180, 255, 255));
    menu->drawText(message, x + padX, y + padY, size, size,
                   Color(255, 255, 255, mStageSessionStyle.textA));
}

void CreationExtras::toggleTimerLabel() {
    mTimerLabelVisible ^= 1;
    mDirty = true;
    if (mHudPictures[HUD_PANE_COUNT - 1])
        mHudPictures[HUD_PANE_COUNT - 1]->mIsVisible = mTimerLabelVisible != 0;
}

void CreationExtras::beginWordEditor(int index) {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditMode = EDIT_WORD_STYLE;
    mEditFirst = 0;
    mEditWord = (u8)index;
    mEditTitle = "Custom text style";
    mEditor.begin(&mWordStyle[index], mWordRgb[index], mWordBackup,
                  SUSAMUNE_CREATION_WORD_CHARS, mWordLength[index]);
}

void CreationExtras::beginKeyboard(int index) {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditWord = (u8)index;
    for (int i = 0; i < SUSAMUNE_CREATION_WORD_TEXT_SIZE; i++)
        mTextBackup[i] = mWords[index][i];
    mKeyboardCursor = 0;
    mKeyboardPage = 0;
    mKeyboardConfirm = 0;
    mUppercase = false;
    mKeyboard = true;
}

void CreationExtras::adjustMenuRow(int row, int direction) {
    (void)direction;
    if (row >= 1 && row <= 7) {
        const int color = row == 1 ? SUSAMUNE_CREATION_FLUDD_WATER
                                   : SUSAMUNE_CREATION_COIN_BG + row - 2;
        beginColorEditor(color, 1, menuRowName(row));
    } else if (row >= 9 && row <= 17) {
        const int local = row - 9;
        const int word = local / 3;
        if (local % 3 == 0) beginKeyboard(word);
        else if (local % 3 == 1) beginWordEditor(word);
        else {
            mWordVisible[word] = !mWordVisible[word];
            mDirty = true;
        }
    } else if (row == 19) {
        beginColorEditor(SUSAMUNE_CREATION_MENU_BG, 1, menuRowName(row));
    } else if (row == 20) {
        beginAchievementBannerEditor();
    } else if (row == 21) {
        beginToastEditor();
    } else if (row == 22) {
        beginPbBannerEditor();
    } else if (row == 23) {
        beginStageSessionEditor();
    }
}

void CreationExtras::updateEditor(TMarioGamePad *pad) {
    if (mKeyboard) {
        updateKeyboard(pad);
        return;
    }
    if (!mEditor.editing()) return;
    static const u8 kOverlayDefaults[1][3] = {{255, 255, 255}};
    const bool overlayStyle = mEditMode == EDIT_RECENT_ILS ||
                              mEditMode == EDIT_SAVESTATE_FEEDBACK ||
                              mEditMode == EDIT_WALLKICK ||
                              mEditMode == EDIT_ROLLOUT ||
                              mEditMode == EDIT_DUST ||
                              mEditMode == EDIT_ACHIEVEMENT_BANNER ||
                              mEditMode == EDIT_TOAST ||
                              mEditMode == EDIT_PB_BANNER ||
                              mEditMode == EDIT_STAGE_SESSION;
    const u8 (*defaults)[3] = mEditMode == EDIT_WORD_STYLE
                                  ? mDefaultColors
        : overlayStyle ? kOverlayDefaults : mDefaultColors + mEditFirst;
    const CreationStyle defaultStyle =
        mEditMode == EDIT_WORD_STYLE ? defaultWordStyle(mEditWord)
        : mEditMode == EDIT_RECENT_ILS ? defaultRecentIlStyle()
        : mEditMode == EDIT_SAVESTATE_FEEDBACK
              ? defaultSavestateFeedbackStyle()
        : mEditMode == EDIT_WALLKICK ? defaultWallkickStyle()
        : mEditMode == EDIT_ROLLOUT ? defaultWallkickStyle()
        : mEditMode == EDIT_DUST ? defaultWallkickStyle()
        : mEditMode == EDIT_ACHIEVEMENT_BANNER
              ? defaultAchievementBannerStyle()
        : mEditMode == EDIT_TOAST ? defaultToastStyle()
        : mEditMode == EDIT_PB_BANNER ? defaultPbBannerStyle()
        : mEditMode == EDIT_STAGE_SESSION ? defaultStageSessionStyle()
                                          : mColorStyle;
    const u8 result = mEditor.update(
        pad, defaultStyle, defaults,
        mEditMode != EDIT_WORD_STYLE && !overlayStyle ? mEditCount : 1);
    if (result & CreationEditor::UPDATE_CHANGED) {
        if (mEditMode == EDIT_WORD_STYLE) {
            clampWord(mEditWord);
        } else if (!overlayStyle) {
            for (int i = 0; i < mEditCount; i++) {
                const int slot = mEditFirst + i;
                mColorPresent |= SUSAMUNE_CREATION_COLOR(slot);
            }
        }
        mDirty = true;
        applyHud();
    }
    if (result & CreationEditor::UPDATE_CANCELLED) {
        mDirty = mDirtyBeforeEdit;
        if (mEditMode != EDIT_WORD_STYLE && !overlayStyle) {
            mColorPresent = mColorPresentBeforeEdit;
            const u32 savedPresent = mColorPresent;
            for (int i = 0; i < mEditCount; i++)
                mColorPresent |= SUSAMUNE_CREATION_COLOR(mEditFirst + i);
            applyHud();
            mColorPresent = savedPresent;
        }
    }
    if (!mEditor.editing()) {
        endHudPreview();
        mEditMode = EDIT_NONE;
    }
}

void CreationExtras::updateKeyboard(TMarioGamePad *pad) {
    const u32 pressed = pad->mButtons.mRapidInput;
    if (mKeyboardConfirm) {
        if (pressed & TMarioGamePad::A) {
            if (mKeyboardConfirm == 1) {
                mDirty = true;
                mKeyboard = false;
            } else if (mKeyboardConfirm == 2) {
                for (int i = 0; i < SUSAMUNE_CREATION_WORD_TEXT_SIZE; i++)
                    mWords[mEditWord][i] = mTextBackup[i];
                mWordLength[mEditWord] = (u8)strlen(mWords[mEditWord]);
                mDirty = mDirtyBeforeEdit;
                mKeyboard = false;
            } else {
                mWords[mEditWord][0] = '\0';
                mWordLength[mEditWord] = 0;
                mDirty = true;
            }
            mKeyboardConfirm = 0;
        } else if (pressed & TMarioGamePad::B) {
            mKeyboardConfirm = 0;
        }
        return;
    }
    if (pressed & TMarioGamePad::START) {
        mKeyboardConfirm = (pad->mButtons.mInput & TMarioGamePad::X) ? 2 : 1;
        return;
    }
    if (pressed & TMarioGamePad::Z) {
        mKeyboardConfirm = 3;
        return;
    }
    if (updateCreationKeyboardText(
            pad, mWords[mEditWord], mWordLength[mEditWord],
            SUSAMUNE_CREATION_WORD_CHARS, mKeyboardPage, mUppercase,
            mKeyboardCursor))
        mDirty = true;
}

void CreationExtras::drawKeyboard(Menu *menu) const {
    const int word = mEditWord;
    Creation::drawTextBox(menu, mWordStyle[word], mWordRgb[word],
                          SUSAMUNE_CREATION_WORD_CHARS, mWords[word]);
    char status[64];
    snprintf(status, sizeof(status),
             "Custom text %d   %u" SUSAMUNE_GLYPH_SLASH "%u", word + 1,
             mWordLength[word], SUSAMUNE_CREATION_WORD_CHARS);
    drawCreationKeyboard(menu, status, nullptr, mKeyboardPage, mUppercase,
                         mKeyboardCursor);
    if (mKeyboardConfirm) {
        menu->fillBox(128, 272, 384, 78, Color(8, 11, 20, 250));
        const char *prompt = mKeyboardConfirm == 1 ? "Keep this text?"
                             : mKeyboardConfirm == 2 ? "Discard text changes?"
                                                     : "Clear this text?";
        menu->drawText(prompt, 320 - Menu::textWidth(prompt, 15) / 2,
                       286, 15, 15, Color(255, 255, 255, 255));
        const char *answer = SUSAMUNE_GLYPH_A " Confirm    "
                             SUSAMUNE_GLYPH_B " Go Back";
        menu->drawText(answer, 320 - Menu::textWidth(answer, 12) / 2,
                       320, 12, 12, Color(190, 220, 255, 255));
    }
}

void CreationExtras::drawEditor(Menu *menu) const {
    if (mKeyboard) {
        drawKeyboard(menu);
        return;
    }
    if (!mEditor.editing()) return;
    if (mEditMode == EDIT_WORD_STYLE) {
        const int word = mEditWord;
        const u16 selected = mEditor.target() ? mEditor.target() - 1 : 0xffff;
        Creation::drawTextBox(menu, mWordStyle[word], mWordRgb[word],
                              SUSAMUNE_CREATION_WORD_CHARS, mWords[word],
                              false, selected);
        mEditor.draw(menu, mEditTitle, mWords[word]);
        return;
    }
    if (mEditMode == EDIT_TIMER) {
        mEditor.draw(menu, mEditTitle, "12:34:567");
        return;
    }
    if (mEditMode == EDIT_RECENT_ILS) {
        mEditor.draw(menu, mEditTitle, "Recent ILs");
        return;
    }
    if (mEditMode == EDIT_SAVESTATE_FEEDBACK) {
        drawSavestateFeedback(menu, "Stage layout changed - save again");
        mEditor.draw(menu, mEditTitle, "Stage layout changed - save again");
        return;
    }
    if (mEditMode == EDIT_WALLKICK) {
        const u16 target = mEditor.target();
        const int color = target ? target - 1 : 0;
        const char *preview = wallkickDisplayLabel(color);
        drawWallkickDisplay(menu, preview, color);
        mEditor.draw(menu, mEditTitle, preview);
        return;
    }
    if (mEditMode == EDIT_ROLLOUT) {
        const u16 target = mEditor.target();
        const int color = target ? target - 1 : 0;
        const char *preview = PackedText::at(kRolloutNames, color);
        drawRolloutDisplay(menu, preview, color);
        mEditor.draw(menu, mEditTitle, preview);
        return;
    }
    if (mEditMode == EDIT_DUST) {
        const u16 target = mEditor.target();
        const int color = target ? target - 1 : 0;
        const char *preview = wallkickDisplayLabel(color);
        drawDustDisplay(menu, preview, color);
        mEditor.draw(menu, mEditTitle, preview);
        return;
    }
    if (mEditMode == EDIT_ACHIEVEMENT_BANNER) {
        mEditor.draw(menu, mEditTitle, "Achievement preview");
        return;
    }
    if (mEditMode == EDIT_TOAST) {
        drawToast(menu, "System notification preview");
        mEditor.draw(menu, mEditTitle, "System notification preview");
        return;
    }
    if (mEditMode == EDIT_PB_BANNER) {
        drawPbBanner(menu, "New PB!  1:23.456");
        mEditor.draw(menu, mEditTitle, "New PB!  1:23.456");
        return;
    }
    if (mEditMode == EDIT_STAGE_SESSION) {
        drawStageSessionCounter(menu, "3/5");
        mEditor.draw(menu, mEditTitle, "3/5");
        return;
    }
    mEditor.draw(menu, mEditTitle, mEditTitle);
}
