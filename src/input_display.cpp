// =====================================================================
// input_display.cpp
//
// Native, live-editable controller overlay. Its default proportions and
// palette follow sup39's Controller Input Display as represented by BitPatty's
// Apache-2.0 gct-generator; the renderer itself is a Susamune implementation
// using the menu's existing J2D/GX primitives rather than a Gecko-code blob.
// =====================================================================

#include "susamune/input_display.hxx"
#include "susamune/mem2_map.h"

#include "Dolphin/PAD.h"
#include "Dolphin/printf.h"
#include "JSystem/JUtility/JUTGamePad.hxx"
#include "SMS/System/Application.hxx"
#include "susamune/binds.hxx"
#include "susamune/menu.hxx"
#include "susamune/packed_text.hxx"

namespace {

typedef JUtility::TColor Color;

const int kDesignW = 182;
const int kDesignH = 120;
const int kValueLineH = 14;
const int kSafeBottom = 456;
const int kTriggerClick = 170;
const int kTriggerAnalogFill = 52;

constexpr u8 kDefaultColors[SUSAMUNE_INPUT_COLOR_COUNT][3] = {
    {238, 238, 238}, {255, 211,   0}, { 46, 229, 184},
    {255,  26,  26}, {238, 238, 238}, {238, 238, 238},
    {223, 223, 223}, {223, 223, 223}, {238, 238, 238},
    {148, 148, 255}, {255, 255, 255}, {238, 238, 238},
};
constexpr char kColorNames[] =
    "Main stick\0C-stick\0A button\0B button\0X button\0Y button\0"
    "L trigger\0R trigger\0Start\0Z button\0Value text\0Trigger outlines";

inline int clampi(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

inline Color color(u8 r, u8 g, u8 b, u8 a) { return Color(r, g, b, a); }

struct Painter {
    Menu                *menu;
    const CreationStyle *style;
    const u8            (*rgb)[3];

    int scale(int v) const { return v * (int)style->scale / 100; }
    int x(int v) const { return (int)style->x + scale(v); }
    int y(int v) const { return (int)style->y + scale(v); }

    Color lit(int slot, u8 alpha) const {
        const int q = style->textBrightness;
        return color((u8)clampi((int)rgb[slot][0] * q / 100, 0, 255),
                     (u8)clampi((int)rgb[slot][1] * q / 100, 0, 255),
                     (u8)clampi((int)rgb[slot][2] * q / 100, 0, 255),
                     (u8)((int)alpha * style->textA / 255));
    }

    void box(int lx, int ly, int lw, int lh, Color c) const {
        menu->fillBox(x(lx), y(ly), scale(lw), scale(lh), c);
    }

    void regularVertices(int lx, int ly, int lr, s16 *xy,
                         int count, int step) const {
        // sup39's renderer uses 32-sided n-gons. These fixed-point unit-circle
        // points reproduce that silhouette without needing a runtime trig
        // implementation in the injected mod.
        static const s16 ux[32] = {
            1000, 981, 924, 831, 707, 556, 383, 195,
            0, -195, -383, -556, -707, -831, -924, -981,
            -1000, -981, -924, -831, -707, -556, -383, -195,
            0, 195, 383, 556, 707, 831, 924, 981
        };
        int cx = x(lx);
        int cy = y(ly);
        int r  = scale(lr);
        for (int i = 0; i < count; i++) {
            const int j = i * step;
            xy[i * 2]     = (s16)(cx + r * ux[j] / 1000);
            xy[i * 2 + 1] = (s16)(cy + r * ux[(j + 24) & 31] / 1000);
        }
    }

    void fillCircle(int lx, int ly, int lr, Color c) const {
        s16 xy[64];
        regularVertices(lx, ly, lr, xy, 32, 1);
        menu->fillPoly(xy, 32, c);
    }

    void strokeCircle(int lx, int ly, int lr, Color c) const {
        s16 xy[64];
        regularVertices(lx, ly, lr, xy, 32, 1);
        menu->strokePoly(xy, 32, c);
    }

    void strokeGate(int lx, int ly, int lr, Color c) const {
        s16 xy[16];
        regularVertices(lx, ly, lr, xy, 8, 4);
        menu->strokePoly(xy, 8, c);
    }

    void button(int lx, int ly, int radius, bool down, int slot) const {
        const Color c = lit(slot, 0xbf);
        // The original display is an outline at rest and gains a solid fill
        // only for the frames where the button is held.
        if (down) fillCircle(lx, ly, radius, c);
        strokeCircle(lx, ly, radius, c);
        // The Start circle becomes only two pixels wide at minimum scale.
        if (down && scale(radius) <= 2)
            menu->fillBox(x(lx) - 1, y(ly) - 1, 3, 3, c);
    }
};

int valueLines(const InputDisplayLiveCfg &cfg) {
    if (cfg.valueMode == SUSAMUNE_INPUT_VALUES_STICKS) return 2;
    if (cfg.valueMode == SUSAMUNE_INPUT_VALUES_FULL) return 3;
    return 0;
}

const char kInputText[] =
    "Input display\0Value readout\0Value source\0Value position\0"
    "Input style\0Reset layout\0Off\0On\0Sticks\0Full\0Raw\0Processed\0"
    "Below\0Above\0Inside";

enum InputTextGroup {
    TEXT_ROWS          = 0,
    TEXT_ON_OFF        = 6,
    TEXT_STICKS        = 8,
    TEXT_SOURCES       = 10,
    TEXT_PLACEMENTS    = 12,
};

__attribute__((noinline)) const char *inputText(int index) {
    return PackedText::at(kInputText, index);
}

static_assert(sizeof(kInputText) == 133, "input text offsets changed");

}  // namespace

InputDisplay &gInputDisplay = *reinterpret_cast<InputDisplay *>(
    SUSAMUNE_MEM2_CONFIG_RUNTIME_PPC_BASE + SUSAMUNE_CONFIG_INPUT_OFFSET);
static_assert(sizeof(InputDisplay) <= SUSAMUNE_CONFIG_INPUT_SIZE,
              "input display exceeds its MEM2 runtime slot");

CreationStyle InputDisplay::defaultStyle() {
    return CreationStyle{16, 314, 100, 255, 0, 0, 0, 0x7f, 100, 0};
}

void InputDisplay::resetDefaults() {
    mStyle              = defaultStyle();
    mCfg.startVisible   = 1;
    mCfg.valueMode      = SUSAMUNE_INPUT_VALUES_OFF;
    mCfg.valueSource    = SUSAMUNE_INPUT_SOURCE_RAW;
    mCfg.valuePlacement = SUSAMUNE_INPUT_VALUES_BELOW;
    for (int i = 0; i < SUSAMUNE_INPUT_COLOR_COUNT; i++) {
        for (int c = 0; c < 3; c++) mColors[i][c] = kDefaultColors[i][c];
    }

    mEditor.reset();
    mVisible           = true;
    mVisibleBeforeEdit = true;
    mDirty             = false;
    mDirtyBeforeEdit   = false;
}

void InputDisplay::adopt(const volatile SusamuneInputDisplayCfg *src) {
    if (src->magic != SUSAMUNE_INPUT_CFG_MAGIC ||
        src->version != SUSAMUNE_INPUT_CFG_VERSION) {
        mVisible = mCfg.startVisible != 0;
        return;
    }

    if (src->x != SUSAMUNE_INPUT_CFG_U16_UNSET) mStyle.x = src->x;
    if (src->y != SUSAMUNE_INPUT_CFG_U16_UNSET) mStyle.y = src->y;
    if (src->startVisible != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.startVisible = src->startVisible != 0;
    if (src->scale != SUSAMUNE_INPUT_CFG_U8_UNSET) mStyle.scale = src->scale;
    if (src->bgR != SUSAMUNE_INPUT_CFG_U8_UNSET) mStyle.bgR = src->bgR;
    if (src->bgG != SUSAMUNE_INPUT_CFG_U8_UNSET) mStyle.bgG = src->bgG;
    if (src->bgB != SUSAMUNE_INPUT_CFG_U8_UNSET) mStyle.bgB = src->bgB;
    if (src->bgA != SUSAMUNE_INPUT_CFG_U8_UNSET) mStyle.bgA = src->bgA;
    if (src->brightness != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mStyle.textBrightness = src->brightness;
    if (src->valueMode != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.valueMode = src->valueMode;
    if (src->valueSource != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.valueSource = src->valueSource;
    if (src->valuePlacement != SUSAMUNE_INPUT_CFG_U8_UNSET)
        mCfg.valuePlacement = src->valuePlacement;

    mCfg.startVisible   = mCfg.startVisible ? 1 : 0;
    mCfg.valueMode      = (u8)clampi(mCfg.valueMode, 0, 2);
    mCfg.valueSource    = (u8)clampi(mCfg.valueSource, 0, 1);
    mCfg.valuePlacement = (u8)clampi(mCfg.valuePlacement, 0, 2);
    clampLayout();
    mVisible = mCfg.startVisible != 0;
    mDirty   = false;
}

void InputDisplay::adoptStyle(const volatile SusamuneInputStyleCfg *src) {
    if (!src || src->magic != SUSAMUNE_INPUT_STYLE_MAGIC ||
        src->version != SUSAMUNE_INPUT_STYLE_VERSION) return;

    if (src->present & SUSAMUNE_INPUT_STYLE_OPACITY)
        mStyle.textA = src->elementOpacity;
    if (src->present & SUSAMUNE_INPUT_STYLE_PADDING)
        mStyle.padding = src->padding;
    for (int i = 0; i < SUSAMUNE_INPUT_COLOR_COUNT; i++) {
        if (src->present & SUSAMUNE_INPUT_STYLE_COLOR(i)) {
            for (int c = 0; c < 3; c++) mColors[i][c] = src->rgb[i][c];
        }
    }
    clampLayout();
}

void InputDisplay::stageInto(volatile SusamuneInputDisplayCfg *dst) const {
    dst->magic          = SUSAMUNE_INPUT_CFG_MAGIC;
    dst->version        = SUSAMUNE_INPUT_CFG_VERSION;
    dst->x              = mStyle.x;
    dst->y              = mStyle.y;
    dst->startVisible   = mCfg.startVisible;
    dst->scale          = mStyle.scale;
    dst->bgR            = mStyle.bgR;
    dst->bgG            = mStyle.bgG;
    dst->bgB            = mStyle.bgB;
    dst->bgA            = mStyle.bgA;
    dst->brightness     = mStyle.textBrightness;
    dst->valueMode      = mCfg.valueMode;
    dst->valueSource    = mCfg.valueSource;
    dst->valuePlacement = mCfg.valuePlacement;
    for (u32 i = 0; i < sizeof(dst->reserved); i++) dst->reserved[i] = 0;
}

void InputDisplay::stageStyleInto(volatile SusamuneInputStyleCfg *dst) const {
    dst->magic          = SUSAMUNE_INPUT_STYLE_MAGIC;
    dst->version        = SUSAMUNE_INPUT_STYLE_VERSION;
    dst->present        = SUSAMUNE_INPUT_STYLE_ALL;
    dst->elementOpacity = mStyle.textA;
    dst->padding        = mStyle.padding;
    for (int i = 0; i < SUSAMUNE_INPUT_COLOR_COUNT; i++) {
        for (int c = 0; c < 3; c++) dst->rgb[i][c] = mColors[i][c];
    }
    for (u32 i = 0; i < sizeof(dst->reserved); i++) dst->reserved[i] = 0;
}

void InputDisplay::markDirty() {
    mDirty = true;
    clampLayout();
}

void InputDisplay::resetLayout() {
    mStyle = defaultStyle();
    markDirty();
}

void InputDisplay::clampLayout() {
    mStyle.scale          = (u8)clampi(mStyle.scale, 50, 200);
    mStyle.textBrightness = (u8)clampi(mStyle.textBrightness, 25, 200);
    if (mStyle.padding != 0xff)
        mStyle.padding = (u8)clampi(mStyle.padding, 0, 16);
    const int pad = mStyle.padding == 0xff ? 0 : mStyle.padding;
    int w = kDesignW * (int)mStyle.scale / 100;
    int h = kDesignH * (int)mStyle.scale / 100;
    int lines = valueLines(mCfg);
    int extra = lines * kValueLineH;
    int minY = (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_ABOVE) ? extra : 0;
    minY += pad;
    int maxY = kSafeBottom - h - pad;
    if (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_BELOW) maxY -= extra;
    if (maxY < minY) maxY = minY;
    mStyle.x = (u16)clampi(mStyle.x, pad, 640 - w - pad);
    mStyle.y = (u16)clampi(mStyle.y, minY, maxY);
}

void InputDisplay::update() {
    if (!editing() && (!gMenu || !gMenu->shown()) &&
        gBinds.wasPressed(BIND_TOGGLE_INPUT_DISPLAY)) {
        mVisible = !mVisible;
    }
}

const char *InputDisplay::menuRowName(int row) {
    return (row >= 0 && row < menuRowCount()) ? inputText(TEXT_ROWS + row) : "";
}

const char *InputDisplay::menuRowValue(int row) const {
    switch (row) {
    case 0: return inputText(TEXT_ON_OFF + (mCfg.startVisible ? 1 : 0));
    case 1:
        return inputText(mCfg.valueMode ? TEXT_STICKS + mCfg.valueMode - 1
                                        : TEXT_ON_OFF);
    case 2: return inputText(TEXT_SOURCES + mCfg.valueSource);
    case 3: return inputText(TEXT_PLACEMENTS + mCfg.valuePlacement);
    case 4: return "Open";
    case 5: return "Default";
    default: return "";
    }
}

void InputDisplay::adjustMenuRow(int row, int dir) {
    if (dir == 0) dir = 1;
    switch (row) {
    case 0:
        mCfg.startVisible = !mCfg.startVisible;
        mVisible = mCfg.startVisible != 0;
        markDirty();
        break;
    case 1:
        mCfg.valueMode = (u8)((mCfg.valueMode + (dir > 0 ? 1 : 2)) % 3);
        markDirty();
        break;
    case 2:
        mCfg.valueSource = !mCfg.valueSource;
        markDirty();
        break;
    case 3:
        mCfg.valuePlacement = (u8)((mCfg.valuePlacement + (dir > 0 ? 1 : 2)) % 3);
        markDirty();
        break;
    case 4:
        beginEditor();
        break;
    case 5:
        resetLayout();
        break;
    }
}

void InputDisplay::beginEditor() {
    if (editing()) return;
    mDirtyBeforeEdit   = mDirty;
    mVisibleBeforeEdit = mVisible;
    mVisible           = true;
    mEditor.begin(&mStyle, mColors, mBackupRgb, SUSAMUNE_INPUT_COLOR_COUNT,
                  SUSAMUNE_INPUT_COLOR_COUNT, kColorNames);
}

void InputDisplay::updateEditor(TMarioGamePad *pad) {
    const u8 result = mEditor.update(pad, defaultStyle(), kDefaultColors,
                                     SUSAMUNE_INPUT_COLOR_COUNT);
    if (result & CreationEditor::UPDATE_CHANGED) markDirty();
    if (result & CreationEditor::UPDATE_CANCELLED)
        mDirty = mDirtyBeforeEdit;
    if (result & CreationEditor::UPDATE_FINISHED)
        mVisible = mVisibleBeforeEdit;
}

void InputDisplay::draw(Menu *menu, bool force) const {
    if ((!mVisible && !force) || !menu) return;

    const PADStatus &raw = JUTGamePad::mPadStatus[0];
    const u16 buttons = raw.mButton;
    Painter p = { menu, &mStyle, mColors };

    const int lines = valueLines(mCfg);
    const int extra = lines * kValueLineH;
    const int graphicH = p.scale(kDesignH);
    int bgY = mStyle.y;
    int bgH = graphicH;
    if (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_ABOVE) {
        bgY -= extra;
        bgH += extra;
    } else if (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_BELOW) {
        bgH += extra;
    }
    if (mStyle.padding != 0xff) {
        const int pad = mStyle.padding;
        menu->fillBox(mStyle.x - pad, bgY - pad,
                      p.scale(kDesignW) + pad * 2, bgH + pad * 2,
                      color(mStyle.bgR, mStyle.bgG, mStyle.bgB, mStyle.bgA));
    }

    // PAD clicks at 170; analog travel stops near 80% so the click is visible.
    const int lFill = (buttons & JUTGamePad::L) ? 64
        : clampi((int)raw.mTriggerLeft * kTriggerAnalogFill / kTriggerClick,
                 0, kTriggerAnalogFill);
    const int rFill = (buttons & JUTGamePad::R) ? 64
        : clampi((int)raw.mTriggerRight * kTriggerAnalogFill / kTriggerClick,
                 0, kTriggerAnalogFill);
    p.box(12, 10, lFill, 8, p.lit(SUSAMUNE_INPUT_COLOR_L, 0xbf));
    p.box(170 - rFill, 10, rFill, 8,
          p.lit(SUSAMUNE_INPUT_COLOR_R, 0xbf));
    const Color triggerStroke =
        p.lit(SUSAMUNE_INPUT_COLOR_TRIGGER_OUTLINE, 0xbf);
    s16 trigger[8] = {
        (s16)p.x(12), (s16)p.y(10), (s16)p.x(76), (s16)p.y(10),
        (s16)p.x(76), (s16)p.y(18), (s16)p.x(12), (s16)p.y(18)
    };
    menu->strokePoly(trigger, 4, triggerStroke);
    trigger[0] = trigger[6] = (s16)p.x(106);
    trigger[2] = trigger[4] = (s16)p.x(170);
    menu->strokePoly(trigger, 4, triggerStroke);

    // The original overlay reads JUT's clamped floats, not the raw PAD bytes.
    const JUTGamePad::CStick &main = JUTGamePad::mPadMStick[0];
    const JUTGamePad::CStick &sub = JUTGamePad::mPadSStick[0];
    const int mx = clampi((int)(main.mStickX * 14.0f), -14, 14);
    const int my = clampi((int)(main.mStickY * 14.0f), -14, 14);
    const int cx = clampi((int)(sub.mStickX * 14.0f), -14, 14);
    const int cy = clampi((int)(sub.mStickY * 14.0f), -14, 14);
    const Color mainStick = p.lit(SUSAMUNE_INPUT_COLOR_MAIN_STICK, 0xef);
    p.fillCircle(32 + mx, 52 - my, 12, mainStick);
    p.strokeGate(32, 52, 19, mainStick);
    const Color cStick = p.lit(SUSAMUNE_INPUT_COLOR_C_STICK, 0xef);
    p.fillCircle(64 + cx, 92 - cy, 12, cStick);
    p.strokeGate(64, 92, 19, cStick);

    p.button(138, 66, 18, buttons & JUTGamePad::A, SUSAMUNE_INPUT_COLOR_A);
    p.button(113, 89, 9, buttons & JUTGamePad::B, SUSAMUNE_INPUT_COLOR_B);
    p.button(164, 50, 8, buttons & JUTGamePad::X, SUSAMUNE_INPUT_COLOR_X);
    p.button(119, 41, 8, buttons & JUTGamePad::Y, SUSAMUNE_INPUT_COLOR_Y);
    p.button(144, 34, 6, buttons & JUTGamePad::Z, SUSAMUNE_INPUT_COLOR_Z);
    p.button(91, 64, 5, buttons & JUTGamePad::START,
             SUSAMUNE_INPUT_COLOR_START);

    if (lines == 0) return;

    int textSize = clampi(10 * (int)mStyle.scale / 100, 8, 14);
    int textY;
    if (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_ABOVE) {
        textY = (int)mStyle.y - lines * kValueLineH;
    } else if (mCfg.valuePlacement == SUSAMUNE_INPUT_VALUES_INSIDE) {
        textY = (int)mStyle.y + graphicH - lines * kValueLineH - 2;
    } else {
        textY = (int)mStyle.y + graphicH;
    }

    int mainX, mainY, cX, cY, triggerL, triggerR;
    TMarioGamePad *pad = gpApplication.mGamePads[0];
    if (mCfg.valueSource == SUSAMUNE_INPUT_SOURCE_PROCESSED && pad) {
        mainX = clampi((int)(pad->mControlStick.mStickX * 100.0f), -100, 100);
        mainY = clampi((int)(pad->mControlStick.mStickY * 100.0f), -100, 100);
        cX = clampi((int)(pad->mCStick.mStickX * 100.0f), -100, 100);
        cY = clampi((int)(pad->mCStick.mStickY * 100.0f), -100, 100);
        triggerL = clampi((int)(pad->mButtons.mAnalogL * 100.0f), 0, 100);
        triggerR = clampi((int)(pad->mButtons.mAnalogR * 100.0f), 0, 100);
    } else {
        mainX = (s8)raw.mStickX;
        mainY = (s8)raw.mStickY;
        cX = (s8)raw.mSubStickX;
        cY = (s8)raw.mSubStickY;
        triggerL = raw.mTriggerLeft;
        triggerR = raw.mTriggerRight;
    }

    char text[48];
    const Color valueText = p.lit(SUSAMUNE_INPUT_COLOR_VALUES, 255);
    snprintf(text, sizeof(text), "M  X:%+04d  Y:%+04d", mainX, mainY);
    menu->drawText(text, mStyle.x + 4, textY + 2, textSize, textSize,
                   valueText);
    snprintf(text, sizeof(text), "C  X:%+04d  Y:%+04d", cX, cY);
    menu->drawText(text, mStyle.x + 4, textY + kValueLineH + 1,
                   textSize, textSize, valueText);
    if (lines == 3) {
        snprintf(text, sizeof(text), "L:%03d  R:%03d", triggerL, triggerR);
        menu->drawText(text, mStyle.x + 4, textY + kValueLineH * 2 + 1,
                       textSize, textSize, valueText);
    }
}

void InputDisplay::drawEditor(Menu *menu) const {
    draw(menu, true);
    mEditor.draw(menu, "Input Display editor", nullptr);
}
