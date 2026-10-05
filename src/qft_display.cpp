#include "susamune/qft_display.hxx"

#include "Dolphin/string.h"

#include "susamune/menu.hxx"
#include "susamune/mem2_map.h"
#include "susamune/practice_session.hxx"
#include "susamune/qft_timer.hxx"

// Keep display/editor code in the existing upper span; neither heap moves.
#if defined(__powerpc__)
#pragma clang section text=".foxtrot.text"
#endif

namespace {

bool sAnchorDrawn;
int sAnchorWidth;
char sAnchorText[20];

inline int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

}  // namespace

QftDisplay &gQftDisplay = *reinterpret_cast<QftDisplay *>(
    SUSAMUNE_MEM2_CONFIG_RUNTIME_PPC_BASE + SUSAMUNE_CONFIG_QFT_OFFSET);
static_assert(sizeof(QftDisplay) <= SUSAMUNE_CONFIG_QFT_SIZE,
              "QFT display exceeds its MEM2 runtime slot");

#pragma clang section text=""
CreationStyle QftDisplay::defaults() {
    return CreationStyle{
        16, 416, 100, 255,
        0, 0, 0, 128,
        100, 2,
    };
}

void QftDisplay::resetDefaults() {
    mStyle           = defaults();
    Creation::fillWhite(mTextRgb, SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS);
    mEditor.reset();
    mDirty           = false;
    mDirtyBeforeEdit = false;
    mLeadingZero     = false;
    sAnchorDrawn     = false;
    sAnchorWidth     = 0;
    sAnchorText[0]   = '\0';
}

void QftDisplay::beginOverlayFrame() {
    sAnchorDrawn = false;
    sAnchorWidth = 0;
    sAnchorText[0] = '\0';
}

void QftDisplay::clamp() {
    mStyle.x              = (u16)clampi(mStyle.x, 0, 640);
    mStyle.y              = (u16)clampi(mStyle.y, 0, 480);
    mStyle.scale          = (u8)clampi(mStyle.scale, 50, 200);
    mStyle.textBrightness = (u8)clampi(mStyle.textBrightness, 25, 200);
    if (mStyle.padding != 0xff)
        mStyle.padding = (u8)clampi(mStyle.padding, 0, 16);
}

void QftDisplay::adopt(const volatile SusamuneQftDisplayCfg *src) {
    if (!src || src->magic != SUSAMUNE_QFT_DISPLAY_CFG_MAGIC ||
        (src->version != 1 &&
         src->version != SUSAMUNE_QFT_DISPLAY_CFG_VERSION)) {
        return;
    }

    const u16 p = src->present;
    if (p & SUSAMUNE_QFT_DISPLAY_X)          mStyle.x = src->x;
    if (p & SUSAMUNE_QFT_DISPLAY_Y)          mStyle.y = src->y;
    if (p & SUSAMUNE_QFT_DISPLAY_SCALE)      mStyle.scale = src->scale;
    if (p & SUSAMUNE_QFT_DISPLAY_TEXT_A)     mStyle.textA = src->textA;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_R)       mStyle.bgR = src->bgR;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_G)       mStyle.bgG = src->bgG;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_B)       mStyle.bgB = src->bgB;
    if (p & SUSAMUNE_QFT_DISPLAY_BG_A)       mStyle.bgA = src->bgA;
    if (p & SUSAMUNE_QFT_DISPLAY_TEXT_BRIGHTNESS)
        mStyle.textBrightness = src->textBrightness;
    if (p & SUSAMUNE_QFT_DISPLAY_PADDING)    mStyle.padding = src->padding;
    if (p & SUSAMUNE_QFT_DISPLAY_LEADING_ZERO)
        mLeadingZero = src->leadingZero != 0;

    for (int i = 0; i < SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS; i++) {
        if (p & SUSAMUNE_QFT_DISPLAY_TEXT_R)
            mTextRgb[i][0] = src->textR;
        if (p & SUSAMUNE_QFT_DISPLAY_TEXT_G)
            mTextRgb[i][1] = src->textG;
        if (p & SUSAMUNE_QFT_DISPLAY_TEXT_B)
            mTextRgb[i][2] = src->textB;
        if (src->version == SUSAMUNE_QFT_DISPLAY_CFG_VERSION &&
            (src->slotPresent & SUSAMUNE_QFT_DISPLAY_SLOT(i))) {
            for (int c = 0; c < 3; c++)
                mTextRgb[i][c] = src->textRgb[i][c];
        }
    }
    clamp();
    mDirty = false;
}

void QftDisplay::stageInto(volatile SusamuneQftDisplayCfg *dst) const {
    dst->magic          = SUSAMUNE_QFT_DISPLAY_CFG_MAGIC;
    dst->version        = SUSAMUNE_QFT_DISPLAY_CFG_VERSION;
    dst->present        = SUSAMUNE_QFT_DISPLAY_ALL;
    dst->x              = mStyle.x;
    dst->y              = mStyle.y;
    dst->scale          = mStyle.scale;
    dst->textR          = mTextRgb[0][0];
    dst->textG          = mTextRgb[0][1];
    dst->textB          = mTextRgb[0][2];
    dst->textA          = mStyle.textA;
    dst->bgR            = mStyle.bgR;
    dst->bgG            = mStyle.bgG;
    dst->bgB            = mStyle.bgB;
    dst->bgA            = mStyle.bgA;
    dst->textBrightness = mStyle.textBrightness;
    dst->padding        = mStyle.padding;
    dst->leadingZero    = mLeadingZero ? 1 : 0;
    for (u32 i = 0; i < sizeof(dst->reservedV1); i++) dst->reservedV1[i] = 0;
    dst->slotPresent = SUSAMUNE_QFT_DISPLAY_ALL_SLOTS;
    for (int i = 0; i < SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS; i++) {
        for (int c = 0; c < 3; c++)
            dst->textRgb[i][c] = mTextRgb[i][c];
    }
    for (u32 i = 0; i < sizeof(dst->reserved); i++) dst->reserved[i] = 0;
}

void QftDisplay::draw(Menu *menu, const char *text) const {
    if (!menu || !text) return;
    const int size = clampi(20 * (int)mStyle.scale / 100, 10, 40);
    sAnchorWidth = Creation::textWidth(text, size);
    strncpy(sAnchorText, text, sizeof(sAnchorText));
    sAnchorText[sizeof(sAnchorText) - 1] = '\0';
    sAnchorDrawn = true;
    Creation::drawTextBox(menu, mStyle, mTextRgb,
                          SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS, text, true);
    if (PracticeSession::assisted() || gQFTTimer.practiceAssisted()) {
        const int labelSize = clampi(size * 3 / 5, 10, 18);
        const int labelWidth = Creation::textWidth("TAS", labelSize);
        const int gap = 6;
        const int pad = mStyle.padding == 0xff ? 0 : mStyle.padding;
        int labelX = mStyle.x + sAnchorWidth + gap + pad;
        int labelY = clampi(mStyle.y + size - labelSize, 0, 478 - labelSize);
        if (labelX + labelWidth + pad + 2 <= 640) {
            sAnchorWidth += gap + labelWidth + pad;
        } else {
            labelX = clampi((int)mStyle.x, 2, 638 - labelWidth);
            labelY = clampi((int)mStyle.y - labelSize - 4, 0, 478 - labelSize);
        }
        menu->fillBox(labelX - 2, labelY, labelWidth + 4, labelSize + 2,
                      JUtility::TColor(8, 17, 31, mStyle.textA));
        menu->drawText("TAS", labelX, labelY, labelSize, labelSize,
                       JUtility::TColor(130, 225, 255, mStyle.textA));
    }
}

#pragma clang section text=".foxtrot.text"

bool QftDisplay::hasAnchor(const char *text) const {
    return sAnchorDrawn && text && strcmp(sAnchorText, text) == 0;
}

void QftDisplay::drawTasFallback(Menu *menu) const {
    if (!menu || sAnchorDrawn ||
        (!PracticeSession::assisted() && !gQFTTimer.practiceAssisted())) return;
    menu->fillBox(8, 446, 35, 18, JUtility::TColor(8, 17, 31, 210));
    menu->drawText("TAS", 10, 447, 14, 14, JUtility::TColor(130, 225, 255, 255));
}

bool QftDisplay::adjacentStyle(const char *anchorText, const char *text,
                               CreationStyle *out) const {
    if (!hasAnchor(anchorText) || !text || !out) return false;

    const int pad = mStyle.padding == 0xff ? 0 : mStyle.padding;
    const int anchorRight = (int)mStyle.x + sAnchorWidth + pad;
    const int rightX = anchorRight + 10 + pad;
    const int available = 640 - pad - rightX;
    int size = clampi(20 * (int)mStyle.scale / 100, 10, 40);
    while (size >= 10 && Creation::textWidth(text, size) > available) size--;
    if (size < 10) return false;

    *out = mStyle;
    // Keep both backgrounds disjoint; a split delta never swaps sides.
    out->x = (u16)rightX;
    out->scale = (u8)(size * 5);
    return true;
}

void QftDisplay::beginEditor() {
    if (editing()) return;
    mDirtyBeforeEdit = mDirty;
    mEditor.begin(&mStyle, mTextRgb, mBackupRgb,
                  SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS);
}

void QftDisplay::updateEditor(TMarioGamePad *pad) {
    const u8 defaultsRgb[1][3] = {{255, 255, 255}};
    const u8 result = mEditor.update(pad, defaults(), defaultsRgb);
    if (result & CreationEditor::UPDATE_CHANGED) {
        clamp();
        mDirty = true;
    }
    if (result & CreationEditor::UPDATE_CANCELLED) {
        mDirty = mDirtyBeforeEdit;
    }
}

void QftDisplay::drawEditor(Menu *menu) const {
    const u16 selected = mEditor.target() ? mEditor.target() - 1 : 0xffff;
    Creation::drawTextBox(menu, mStyle, mTextRgb,
                          SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS, "12:34:567",
                          true, selected);
    mEditor.draw(menu, "QFT timer editor", "12:34:567");
}

void QftDisplay::toggleLeadingZero() {
    mLeadingZero = !mLeadingZero;
    mDirty = true;
}
