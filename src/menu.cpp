// =====================================================================
// menu.cpp
//
// Rendering + navigation for the tabbed mod menu. Everything feature-
// specific is delegated: the warp tabs call into warp.*, the savestate
// tab into settings.*. See menu.hxx for the design.
//
// Memory: the menu owns exactly one J2DTextBox and re-points its mStrPtr
// at borrowed const strings each frame, so it never touches the (nearly
// full) system heap per item. The Menu object itself is placement-new'd
// once into a static BSS buffer -- no persistent heap allocation at all.
// =====================================================================

#include "susamune/menu.hxx"
#include "susamune/mem2_map.h"
#include "susamune/binds.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/glyphs.hxx"
#include "susamune/input_display.hxx"
#include "susamune/iling.hxx"
#include "susamune/metadata_display.hxx"
#include "susamune/packed_text.hxx"
#include "susamune/attempt_counter.hxx"
#include "susamune/qft_timer.hxx"
#include "susamune/ghost.hxx"
#include "susamune/ghost_storage.hxx"
#include "susamune/qft_display.hxx"
#include "susamune/raw_prompt_input.hxx"
#include "susamune/records.hxx"
#include "susamune/records_persistence.hxx"
#include "susamune/rng_control.hxx"
#include "susamune/settings.hxx"
#include "susamune/split_stats.hxx"
#include "susamune/stage_loader.hxx"
#include "susamune/stage_targets.hxx"
#include "susamune/susamune_cfg.h"
#include "susamune/wallkick_display.hxx"
#include "susamune/movement_display.hxx"
#include "susamune/warp_wheel.hxx"
#if ENABLE_DEBUG_WARPS
#include "susamune/debug_warp.hxx"
#endif

#include "Dolphin/string.h"
#include "Dolphin/printf.h"
#include "Dolphin/GX.h"
#include "JSystem/JAudio/JASystem/JASTrackMgr.hxx"
#include "J2D/J2DOrthoGraph.hxx"
#include "J2D/J2DPane.hxx"  // J2DFillBox
#include "J2D/J2DTextBox.hxx"
#include "SMS/MSound/MSound.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "JKernel/JKRHeap.hxx"  // placement new (operator new(size_t, void*))

// Objects are placement-new'd into BSS buffers so the menu needs no heap.

namespace {

typedef JUtility::TColor Color;

inline Color col(u8 r, u8 g, u8 b, u8 a) { return Color(r, g, b, a); }
inline Color warningForeground(Color color, bool menuShown) {
    if (!menuShown && rngControlInvalidatesIl() &&
        (u16)color.r + color.g + color.b > 192) {
        color.r = 255;
        color.g = 0;
        color.b = 0;
    }
    return color;
}
inline Color warningText(Color color, bool menuShown) {
    if (!menuShown && rngControlInvalidatesIl()) {
        color.r = 255;
        color.g = 0;
        color.b = 0;
    }
    return color;
}
inline int clampi(int value, int lo, int hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

// -------- palette (built inline so nothing lives in static storage) -------
inline Color cBackdrop()   { return col(6, 8, 14, 150); }    // full-screen dim
inline Color cPanel()      {
    const u8 *rgb = gCreationExtras.menuBackground();
    return col(rgb[0], rgb[1], rgb[2], 240);
}  // menu body
inline Color cAccent()     { return col(90, 170, 255, 255); }// primary accent
inline Color cTitle()      { return col(238, 242, 252, 255); }
inline Color cTabIdle()    { return col(150, 160, 182, 255); }
inline Color cTabOnText()  { return col(14, 18, 28, 255); }  // text on accent
inline Color cRowSelBg()   { return col(90, 170, 255, 60); } // selected row bar
inline Color cRowSel()     { return col(255, 255, 255, 255); }
inline Color cRow()        { return col(200, 206, 220, 255); }
inline Color cRowDim()     { return col(120, 130, 150, 255); }
inline Color cValue()      { return col(120, 220, 150, 255); }// setting value
inline Color cFooter()     { return col(104, 114, 136, 255); }

const char *settingsStorageError(u32 error) {
#if !IS_EMULATOR
    static const char kErrors[] =
        "storage I/O error\0storage internal error\0storage not ready\0"
        "settings file missing\0settings path missing\0invalid settings filename\0"
        "storage access denied\0settings file already exists\0invalid settings file\0"
        "storage is write-protected\0invalid storage device\0storage not mounted\0"
        "storage has no FAT filesystem\0format aborted\0storage timed out\0"
        "settings file locked\0not enough memory\0too many open files\0"
        "invalid storage parameter";
    return error >= 1 && error <= 19
               ? PackedText::at(kErrors, error - 1)
               : nullptr;
#else
    switch (error) {
    case 2:   return "wrong device in slot B";
    case 3:   return "no card in slot B";
    case 4:   return "settings file missing";
    case 5:   return "slot B card I/O error";
    case 6:   return "slot B card unformatted";
    case 7:   return "settings file already exists";
    case 8:   return "card directory is full";
    case 9:   return "slot B card is full";
    case 10:  return "settings file not writable";
    case 11:  return "slot B card limit reached";
    case 12:  return "settings filename too long";
    case 13:  return "slot B encoding mismatch";
    case 14:  return "card operation canceled";
    case 128: return "fatal slot B card error";
    case 256: return "not enough memory";
    default:  return nullptr;
    }
#endif
}

// -------- font metrics ----------------------------------------------------
// Cached in Menu::Menu so the static textWidth() can reach them. They describe
// the same font drawText() renders with (gpSystemFont), so measuring matches
// what J2DPrint (via J2DTextBox::draw) actually lays out.
JUTFont *sFont        = nullptr;
int      sFontWidth   = 1;      // design cell width; every advance scales by size/this
int      sCharSpacing = 0;      // J2DTextBox's spacing, handed to J2DPrint
bool     sFontFixed   = false;  // font advances every glyph by sFontFixedW
int      sFontFixedW  = 0;

#if defined(SUSAMUNE_VERSION_JP)
// JP's font has no one-byte slash. Keep raw UI/custom text portable without
// making every dynamic format string know the regional encoding.
char *const sFontSafeText =
    reinterpret_cast<char *>(SUSAMUNE_MEM2_MENU_TEXT_PPC_BASE);

const char *fontSafeText(const char *text) {
    u32 bytes = 0;
    u32 slashes = 0;
    const unsigned char *scan =
        reinterpret_cast<const unsigned char *>(text);
    while (*scan) {
        if (sFont && sFont->isLeadByte(*scan) && scan[1]) {
            bytes += 2;
            scan += 2;
        } else {
            if (*scan == '/') slashes++;
            bytes++;
            scan++;
        }
    }
    if (!slashes || bytes + slashes >= SUSAMUNE_MENU_TEXT_SIZE) return text;

    char *out = sFontSafeText;
    scan = reinterpret_cast<const unsigned char *>(text);
    while (*scan) {
        if (sFont && sFont->isLeadByte(*scan) && scan[1]) {
            *out++ = (char)*scan++;
            *out++ = (char)*scan++;
            continue;
        } else if (*scan == '/') {
            *out++ = (char)0x81;
            *out++ = (char)0x5e;
        } else {
            *out++ = (char)*scan;
        }
        scan++;
    }
    *out = '\0';
    return sFontSafeText;
}
#endif

// -------- layout ----------------------------------------------------------
const int PANEL_X = 40;
const int PANEL_Y = 20;
const int PANEL_W = 560;
const int PANEL_H = 400;

const int PAD       = 18;
const int TITLE_SZ  = 20;
const int TAB_SZ    = 18;
const int ROW_SZ    = 16;
const int ROW_H = ROW_SZ + 8;
const int FOOT_SZ   = 12;
const int HELP_H    = 32;

const int TAB_GAP   = 12;  // space between tabs
const int TAB_INNER = 10;  // highlight padding around a tab's text
const int TAB_CHEV  = 16;  // strip margin reserved for the scroll chevrons

const int TAB_STRIP_Y = PANEL_Y + 46;
const int TAB_STRIP_H = 30;
const int CONTENT_Y   = PANEL_Y + 92;
const int FOOTER_Y    = PANEL_Y + PANEL_H - 26;

}  // namespace

// =====================================================================
// Tab interface + concrete tabs
// =====================================================================

class MenuTab {
public:
    virtual const char *title() const              = 0;
    virtual const char *summary() const { return nullptr; }
    virtual void update(Menu *menu, TMarioGamePad *pad) = 0;
    // Render into the content rect [x, x+w) x [y, y+h).
    virtual void draw(Menu *menu, int x, int y, int w, int h) = 0;
    // While true, Menu::update hands the pad to this tab alone: no tab
    // switching, no close combo. The binds tab needs it, since every button
    // it might record is also a menu control.
    virtual bool grabsInput() const { return false; }
    virtual bool suppressesBinds() const { return false; }
    // A live editor can temporarily replace the normal panel while retaining
    // the menu's input grab and stage-freeze behaviour.
    virtual bool fullScreen() const { return false; }
    virtual bool favoriteHint() const { return false; }
    virtual bool available() const { return true; }
    // Nested hubs use a non-negative count as a compact Ready/blocked value.
    virtual int rootAlertCount() const { return -1; }
    virtual void focus() {}
    // Nested pages consume one Back press before their parent closes.
    virtual bool back() { return false; }
    // Protected PB saves may need to route through a nested Ghosts page.
    virtual bool beginProtectedPBSave(Menu *, u32) { return false; }

protected:
    void drawScrollHints(Menu *menu, int x, int y, int w, int h, int start, int end,
                         int count) {
        const int cx = x + w - 7;
        if (start > 0) {
            const int top = y - ROW_H + 5;
            const s16 up[6] = {
                (s16)cx,       (s16)top,
                (s16)(cx - 6), (s16)(top + 9),
                (s16)(cx + 6), (s16)(top + 9)
            };
            menu->fillPoly(up, 3, cRowDim());
        }
        if (end < count) {
            const int top = y + h - ROW_SZ + 2;
            const s16 down[6] = {
                (s16)(cx - 6), (s16)top,
                (s16)(cx + 6), (s16)top,
                (s16)cx,       (s16)(top + 9)
            };
            menu->fillPoly(down, 3, cRowDim());
        }
    }
};

namespace {

// Shared vertical-list helpers so every list-style tab scrolls the same way.
// Keep multi-call helpers out of line; duplicating them costs hundreds of bytes.
// Returns the index of the first row to draw so that `sel` stays visible.
__attribute__((noinline)) int listScrollStart(int sel, int count, int maxRows) {
    if (count <= maxRows) {
        return 0;
    }
    int start = sel - maxRows / 2;
    if (start < 0) {
        start = 0;
    }
    if (start > count - maxRows) {
        start = count - maxRows;
    }
    return start;
}

// Draw the selection highlight bar behind a row.
__attribute__((noinline)) void drawRowHighlight(Menu *menu, int x, int y, int w, int rowH) {
    menu->fillBox(x - 6, y - (rowH - 12) / 2, w + 12, rowH, cRowSelBg());
}

__attribute__((noinline)) void drawSectionHeader(Menu *menu, int x, int y, int w,
                                                 const char *label) {
    menu->drawText(label, x + 4, y + 3, 13, 13, cAccent());
    const int lineX = x + 14 + Menu::textWidth(label, 13);
    menu->fillBox(lineX, y + 11, x + w - lineX - 8, 1, cRowDim());
}

// Wrap helper for a cursor over [0, n).
__attribute__((noinline)) int wrap(int v, int n) {
    if (v < 0) {
        v += n;
    } else if (v >= n) {
        v -= n;
    }
    return v;
}

void bgmStatsDraw(Menu *menu) {
    if (!gSettings.getBool(SETTING_SHOW_BGM_SLOTS) || !JASystem::TrackMgr::sRootTrack) {
        return;
    }

    u32 freeRoots = 0;
    for (int i = 0; i < 8; i++) {
        if (!JASystem::TrackMgr::sRootTrack[i]) {
            freeRoots++;
        }
    }

    char text[24];
    snprintf(text, sizeof(text), "RT:%lu S:%lu", freeRoots,
             JASystem::TrackMgr::seqRemain);

    const int size = 16;
    const int tw = Menu::textWidth(text, size);
    const int x    = 640 - 20 - tw;
    const int y    = 480 - 80 - size;
    menu->fillBox(x, y, tw + 1, size + 1, col(0, 0, 0, 180));
    menu->drawText(text, x, y, size, size, col(255, 255, 255, 255));
}

}  // namespace

static void drawValueRow(Menu *menu, int x, int y, int w, const char *name,
                         const char *value, bool selected, bool starred,
                         bool arrow);
static void drawValueRowColored(Menu *menu, int x, int y, int w,
                                const char *name, const char *value,
                                bool selected, bool starred, bool arrow,
                                Color valueColor);
static void drawHelpLine(Menu *menu, int x, int y, int w, int h,
                         const char *text);

// ---------------------------------------------------------------------
// IL and travel practice
// ---------------------------------------------------------------------
class ILingTab : public MenuTab {
public:
    ILingTab()
        : mSel(0), mConfirmDelete(false), mEditingName(false),
          mNameCursor(0), mNamePage(0), mNameLength(0), mNameUpper(false),
          mShowingStats(false), mShowingSegments(false),
          mConfirmGoldDelete(false), mStatsEntry(-1), mStatsSegment(0) {
        mNameBuffer[0] = '\0';
    }

    const char *title() const override { return "ILs"; }
    bool grabsInput() const override {
        return mConfirmDelete || mEditingName || mShowingStats ||
               gCreationExtras.editing();
    }
    bool suppressesBinds() const override {
        if (grabsInput()) return true;
        u16 consumed = JUTGamePad::A;
        if (isOption()) {
            if (mSel >= 2 && mSel <= 6) consumed |= JUTGamePad::X;
        } else {
            consumed |= JUTGamePad::Y;
            if (ILing::pbQf(selectedEntry()) >= 0) consumed |= JUTGamePad::X;
        }
        const u16 held = JUTGamePad::mPadStatus[0].mButton;
        return (held & consumed) != 0;
    }
    bool fullScreen() const override {
        return mEditingName || mShowingStats || gCreationExtras.editing();
    }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (gCreationExtras.editing()) {
            gCreationExtras.updateEditor(pad);
            return;
        }
        if (mEditingName) {
            updateNameEditor(pad);
            return;
        }
        if (mShowingStats) {
            const u16 pressed = mPromptInput.update();
            if (mConfirmGoldDelete) {
                if (pressed & JUTGamePad::A) {
                    const SplitStats::DeleteGoldResult result =
                        SplitStats::deleteGold(mStatsEntry, mStatsSegment);
                    mConfirmGoldDelete = false;
                    mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                                       JUTGamePad::X | JUTGamePad::START);
                    if (result == SplitStats::DELETE_GOLD_OK) {
                        menu->toast(SplitStats::storageState() ==
                                            SplitStats::STORAGE_SESSION
                                        ? "Gold deleted (session only)"
                                        : "Gold deleted");
                    } else if (result == SplitStats::DELETE_GOLD_READ_ONLY) {
                        menu->toast("Stats are read only");
                    } else {
                        menu->toast("No Gold saved");
                    }
                } else if (pressed & JUTGamePad::B) {
                    mConfirmGoldDelete = false;
                    mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                                       JUTGamePad::X | JUTGamePad::START);
                }
                return;
            }
            if (!mShowingSegments && (pressed & JUTGamePad::A)) {
                mShowingSegments = true;
                mStatsSegment = 0;
                mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                                   JUTGamePad::X | JUTGamePad::START);
            } else if (mShowingSegments && (pressed & JUTGamePad::X)) {
                SplitStats::Summary stats;
                if (!SplitStats::summary(mStatsEntry, &stats) ||
                    mStatsSegment >= stats.segmentCount ||
                    stats.segments[mStatsSegment].goldQf < 0) {
                    menu->toast("No Gold saved");
                } else if (SplitStats::storageState() ==
                           SplitStats::STORAGE_READ_ONLY) {
                    menu->toast("Stats are read only");
                } else {
                    mConfirmGoldDelete = true;
                    mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                                       JUTGamePad::X);
                }
            } else if (mShowingSegments && (pressed & JUTGamePad::B)) {
                mShowingSegments = false;
                mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                                   JUTGamePad::X | JUTGamePad::START);
            } else if ((!mShowingSegments && (pressed & JUTGamePad::B)) ||
                       (pressed & JUTGamePad::START)) {
                mShowingStats = false;
                mShowingSegments = false;
                mConfirmGoldDelete = false;
                mStatsEntry = -1;
                mPromptInput.clear();
            }
            if (mShowingSegments) {
                SplitStats::Summary stats;
                const u32 navigation = menu->navigationInput(pad);
                if (SplitStats::summary(mStatsEntry, &stats) &&
                    stats.segmentCount > 0) {
                    if (navigation & TMarioGamePad::CSTICK_UP)
                        mStatsSegment = (u8)wrap(
                            (int)mStatsSegment - 1, stats.segmentCount);
                    else if (navigation & TMarioGamePad::CSTICK_DOWN)
                        mStatsSegment = (u8)wrap(
                            (int)mStatsSegment + 1, stats.segmentCount);
                }
            }
            return;
        }
        if (mConfirmDelete) {
            const u16 pressed = mPromptInput.update();
            if (pressed & JUTGamePad::A) {
                SplitStats::onPBDeleted(selectedEntry(), ILing::pbProfile());
                ILing::clearPB(selectedEntry());
                mConfirmDelete = false;
                mPromptInput.clear();
                menu->toast("PB deleted");
            } else if (pressed & JUTGamePad::B) {
                mConfirmDelete = false;
                mPromptInput.clear();
            }
            return;
        }
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, OPTION_COUNT + ILing::count());
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, OPTION_COUNT + ILing::count());
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            if (isOption()) {
                jumpFromOptions(-1);
            } else {
                jumpSection(-1);
            }
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            if (isOption()) {
                jumpFromOptions(+1);
            } else {
                jumpSection(+1);
            }
        }
        if (isOption() && mSel >= 2 && mSel <= 6 &&
            (rapid & TMarioGamePad::X)) {
            const SettingId id = optionSetting(mSel);
            gSettings.toggleFavorite(id);
            menu->toast(gSettings.favorite(id)
                            ? "Added to Shined"
                            : "Removed from Shined");
            return;
        }
        if (rapid & TMarioGamePad::A) {
            if (isOption()) {
                activateOption(menu, +1);
            } else {
                const int entry = selectedEntry();
                if (WarpWheel::requestExplicitILStart(entry)) {
                    menu->hide();
                } else {
                    menu->toast("Warps disabled");
                }
            }
        } else if (!isOption() && (rapid & TMarioGamePad::X) &&
                   ILing::pbQf(selectedEntry()) >= 0) {
            mConfirmDelete = true;
            mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
        } else if (!isOption() && (rapid & TMarioGamePad::Y)) {
            if (SplitStats::supportsEntry(selectedEntry())) {
                mStatsEntry = selectedEntry();
                mShowingStats = true;
                mShowingSegments = false;
                mConfirmGoldDelete = false;
                mStatsSegment = 0;
                mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                                   JUTGamePad::X | JUTGamePad::START);
            } else {
                menu->toast("Split stats coming later");
            }
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        if (gCreationExtras.editing()) {
            ILing::drawRecentPreview(menu);
            gCreationExtras.drawEditor(menu);
            return;
        }
        if (mEditingName) {
            drawNameEditor(menu);
            return;
        }
        if (mShowingStats) {
            drawStats(menu);
            return;
        }
        if (mConfirmDelete) {
            const char *question = "Delete PB?";
            const char *entry = ILing::label(selectedEntry());
            const char *hint = SUSAMUNE_GLYPH_A " Yes    " SUSAMUNE_GLYPH_B " No";
            menu->fillBox(x, y + 34, w, 104, JUtility::TColor(36, 30, 20, 245));
            menu->fillBox(x, y + 34, w, 3, cAccent());
            menu->drawText(question,
                           x + (w - Menu::textWidth(question, ROW_SZ)) / 2,
                           y + 52, ROW_SZ, ROW_SZ, cRowSel());
            menu->drawText(entry,
                           x + (w - Menu::textWidth(entry, ROW_SZ)) / 2,
                           y + 78, ROW_SZ, ROW_SZ, cValue());
            menu->drawText(hint,
                           x + (w - Menu::textWidth(hint, FOOT_SZ)) / 2,
                           y + 112, FOOT_SZ, FOOT_SZ, cFooter());
            return;
        }
        const int entries = ILing::count();
        const int listH = h - ROW_H;
        const int maxRows = listH / ROW_H;
        const int rows = menuRowCount(entries);
        const int start = listScrollStart(menuRowForSelection(mSel), rows, maxRows);
        int end = start + maxRows;
        if (end > rows) {
            end = rows;
        }

        int ry = y;
        int row = 0;

        if (row >= start && row < end) {
            drawSectionHeader(menu, x, ry, w, "PB PROFILE");
            ry += ROW_H;
        }
        row++;

        for (int i = 0; i < 2 && row < end; i++, row++) {
            if (row < start) {
                continue;
            }

            const bool selected = i == mSel;
            const char *value = optionValue(i);
            drawValueRow(menu, x, ry, w, optionName(i), value, selected,
                         false, true);
            ry += ROW_H;
        }

        if (ILing::pbProfile() == 0) {
            if (row >= start && row < end) {
                char theory[24];
                const char *value = "Incomplete";
                s32 qf;
                if (ILing::anyPercentTheoryQf(&qf)) {
                    const s32 millis = (qf * 1001) / 120;
                    snprintf(theory, sizeof(theory), "%d:%02d:%02d.%03d",
                             (int)(millis / 3600000),
                             (int)((millis / 60000) % 60),
                             (int)((millis / 1000) % 60),
                             (int)(millis % 1000));
                    value = theory;
                }
                drawValueRow(menu, x, ry, w, "Theoretical best", value,
                             false, false, true);
                ry += ROW_H;
            }
            row++;
        }

        if (row >= start && row < end) {
            drawSectionHeader(menu, x, ry, w, "PB OPTIONS");
            ry += ROW_H;
        }
        row++;

        for (int i = 2; i < OPTION_COUNT && row < end; i++, row++) {
            if (row < start) continue;
            const bool selected = i == mSel;
            const bool starred = i <= 6 &&
                gSettings.favorite(optionSetting(i));
            const char *value = optionValue(i);
            drawValueRow(menu, x, ry, w, optionName(i), value, selected,
                         starred, true);
            ry += ROW_H;
        }

        for (int position = 0; position < entries && row < end; position++) {
            if (ILing::beginsMenuGroup(position)) {
                if (row >= start) {
                    drawSectionHeader(menu, x, ry, w,
                                      ILing::menuGroupName(position));
                    ry += ROW_H;
                }
                row++;
                if (row >= end) break;
            }
            if (row < start) {
                row++;
                continue;
            }

            const int entry = ILing::menuEntryAt(position);
            const bool selected = !isOption() &&
                                  position == selectedPosition();
            char pb[24];
            const char *value = "(PB: --)";
            const s32 qf = ILing::pbQf(entry);
            if (qf >= 0) {
                    ILing::formatTime(qf, pb, sizeof(pb),
                                  "(PB: %d:%02d.%03d)");
                value = pb;
            }
            drawValueRow(menu, x, ry, w, ILing::label(entry), value, selected,
                         false, true);
            ry += ROW_H;
            row++;
        }

        drawScrollHints(menu, x, y, w, listH, start, end, rows);
        const char *hint = isOption()
            ? SUSAMUNE_GLYPH_A " Toggle" SUSAMUNE_GLYPH_SLASH "Edit  "
              SUSAMUNE_GLYPH_X " Shine  " SUSAMUNE_GLYPH_C
              " U" SUSAMUNE_GLYPH_SLASH "D Select L"
              SUSAMUNE_GLYPH_SLASH "R Section"
            : SUSAMUNE_GLYPH_A " Start  " SUSAMUNE_GLYPH_X " Delete  "
              SUSAMUNE_GLYPH_Y " Stats  " SUSAMUNE_GLYPH_C " Move";
        menu->drawText(hint, x + 4, y + h - FOOT_SZ,
                       FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    enum { OPTION_COUNT = 8 };

    bool isOption() const { return mSel < OPTION_COUNT; }
    int selectedPosition() const { return mSel - OPTION_COUNT; }
    int selectedEntry() const {
        return ILing::menuEntryAt(selectedPosition());
    }

    static SettingId optionSetting(int option) {
        static const u8 kSettings[5] = {
            SETTING_ILING_RECORDING,
            SETTING_ILING_POPUP,
            SETTING_ILING_FANFARE,
            SETTING_ILING_RECENT,
            SETTING_ILING_SHORT_NAMES,
        };
        return (SettingId)kSettings[option - 2];
    }

    static const char *optionName(int option) {
        static const char kNames[] =
            "PB profile\0Profile name\0Record IL PBs\0PB popup\0PB fanfare\0"
            "Recent IL history\0Recent IL names\0Recent IL display";
        return PackedText::at(kNames, option);
    }

    static const char *optionValue(int option) {
        if (option == 0) return ILing::pbProfileName(ILing::pbProfile());
        if (option == 1) {
            return ILing::pbProfileNameEditable(ILing::pbProfile())
                       ? "Edit"
                       : "Fixed";
        }
        if (option == OPTION_COUNT - 1) {
            return "Edit";
        }
        if (option == OPTION_COUNT - 2) {
            return gSettings.getBool(SETTING_ILING_SHORT_NAMES)
                       ? "Short"
                       : "Long";
        }
        if (option == 2 && gSettings.getBool(SETTING_STAGE_INTRO_SKIP)) {
            return "Off (Intro Skip)";
        }
        return gSettings.valueLabel(optionSetting(option));
    }

    void activateOption(Menu *menu, int direction) {
        if (mSel == 0) {
            ILing::cyclePbProfile(direction);
            menu->toast(ILing::pbProfileName(ILing::pbProfile()));
        } else if (mSel == 1) {
            if (ILing::pbProfileNameEditable(ILing::pbProfile())) {
                beginNameEditor();
            } else {
                menu->toast("This profile name is fixed");
            }
        } else
        if (mSel == OPTION_COUNT - 1) {
            gCreationExtras.beginRecentIlEditor();
        } else {
            gSettings.cycle(optionSetting(mSel), direction);
        }
    }

    void jumpSection(int direction) {
        const int position = selectedPosition();
        int groupFirst = position;
        while (groupFirst > 0 && !ILing::beginsMenuGroup(groupFirst)) {
            groupFirst--;
        }
        const int destination = ILing::jumpMenuGroup(position, direction);
        if ((direction < 0 && groupFirst == 0) ||
            (direction > 0 && destination == 0)) {
            mSel = 0;
            return;
        }
        mSel = OPTION_COUNT + destination;
    }

    void jumpFromOptions(int direction) {
        const int first = direction > 0 ? 0 : ILing::jumpMenuGroup(0, -1);
        mSel = OPTION_COUNT + first;
    }

    static int menuRowForPosition(int position) {
        int row = position;
        for (int i = 0; i <= position; i++) {
            if (ILing::beginsMenuGroup(i)) row++;
        }
        return row;
    }

    static int menuRowCount(int entries) {
        return 2 + OPTION_COUNT + theoryRows() +
               menuRowForPosition(entries - 1) + 1;
    }

    static int menuRowForSelection(int selection) {
        if (selection < 2) return 1 + selection;
        if (selection < OPTION_COUNT) return 2 + theoryRows() + selection;
        return 2 + OPTION_COUNT + theoryRows() +
               menuRowForPosition(selection - OPTION_COUNT);
    }

    static int theoryRows() { return ILing::pbProfile() == 0 ? 1 : 0; }

    void beginNameEditor() {
        const char *name = ILing::pbProfileName(ILing::pbProfile());
        mNameLength = 0;
        while (mNameLength + 1 < SUSAMUNE_ILING_PROFILE_NAME_SIZE &&
               name[mNameLength]) {
            mNameBuffer[mNameLength] = name[mNameLength];
            mNameLength++;
        }
        mNameBuffer[mNameLength] = '\0';
        mNameCursor = 0;
        mNamePage = 0;
        mNameUpper = false;
        mEditingName = true;
    }

    void updateNameEditor(TMarioGamePad *pad) {
        const u32 pressed = pad->mButtons.mRapidInput;
        if (pressed & TMarioGamePad::START) {
            if (!(pad->mButtons.mInput & TMarioGamePad::X)) {
                ILing::setPbProfileName(ILing::pbProfile(), mNameBuffer);
            }
            mEditingName = false;
            return;
        }
        if (pressed & TMarioGamePad::Z) {
            mNameLength = 0;
            mNameBuffer[0] = '\0';
            return;
        }
        updateCreationKeyboardText(pad, mNameBuffer, mNameLength,
                                   SUSAMUNE_ILING_PROFILE_NAME_SIZE - 1,
                                   mNamePage, mNameUpper, mNameCursor);
    }

    void drawNameEditor(Menu *menu) const {
        drawCreationKeyboard(menu, "Name PB profile",
                             mNameBuffer[0] ? mNameBuffer : "(Custom profile)",
                             mNamePage, mNameUpper, mNameCursor);
    }

    static void formatStatsTime(s32 qf, char *out, u32 size) {
        if (qf < 0) {
            snprintf(out, size, "--");
            return;
        }
        const u64 millis64 = (u64)(u32)qf * 1001u / 120u;
        const u32 millis = (u32)millis64;
        if (millis < 60000u)
            snprintf(out, size, "%lu.%03lu", millis / 1000u,
                     millis % 1000u);
        else
            snprintf(out, size, "%lu:%02lu.%03lu", millis / 60000u,
                     (millis / 1000u) % 60u, millis % 1000u);
    }

    static void formatPlayedTime(u32 qf, char *out, u32 size) {
        const u64 seconds = ((u64)qf * 1001u) / 120000u;
        snprintf(out, size, "%lu:%02lu:%02lu",
                 (u32)(seconds / 3600u),
                 (u32)((seconds / 60u) % 60u),
                 (u32)(seconds % 60u));
    }

    void drawStats(Menu *menu) const {
        SplitStats::Summary stats;
        if (!SplitStats::summary(mStatsEntry, &stats)) return;

        const int x = 52;
        const int y = 40;
        const int w = 536;
        const int h = mShowingSegments ? 360 : 320;
        menu->fillBox(0, 0, 640, 480, cBackdrop());
        menu->fillBox(x, y, w, h, cPanel());
        menu->fillBox(x, y, w, 3, cAccent());
        menu->drawText(mShowingSegments ? "IL SEGMENTS" : "IL STATS",
                       x + 20, y + 16, TITLE_SZ, TITLE_SZ, cTitle());
        menu->drawText(stats.routeName, x + 20, y + 47,
                       ROW_SZ, ROW_SZ, cRowSel());

        char scope[40];
#if defined(SUSAMUNE_VERSION_JP)
        const char *region = "JP";
#elif defined(SUSAMUNE_VERSION_US)
        const char *region = "US";
#else
        const char *region = "PAL";
#endif
        snprintf(scope, sizeof(scope), "%s / PB: %s", region,
                 ILing::pbProfileName(ILing::pbProfile()));
        menu->drawText(scope, x + 20, y + 71, FOOT_SZ, FOOT_SZ, cRowDim());

        int rowY = y + 100;
        if (mShowingSegments) {
            drawSectionHeader(menu, x + 16, rowY, w - 32,
                              "PB SPLIT (SEGMENT) / GOLD");
            rowY += ROW_H;
            for (u8 i = 0; i < stats.segmentCount; i++, rowY += ROW_H) {
                const SplitStats::Summary::Segment &segment = stats.segments[i];
                char label[16];
                char split[20];
                char duration[20];
                char gold[20];
                char value[64];
                if (stats.segmentCount == 1)
                    snprintf(label, sizeof(label), "Terminal");
                else
                    snprintf(label, sizeof(label), "Segment %d", (int)i + 1);
                formatStatsTime(segment.pbSplitQf, split, sizeof(split));
                formatStatsTime(segment.pbSegmentQf, duration,
                                sizeof(duration));
                formatStatsTime(segment.goldQf, gold, sizeof(gold));
                if (i == 0)
                    snprintf(value, sizeof(value), "%s   Gold: %s", split, gold);
                else
                    snprintf(value, sizeof(value), "%s (%ss)   Gold: %s",
                             split, duration, gold);
                drawValueRow(menu, x + 16, rowY, w - 32, label, value,
                             i == mStatsSegment, false, false);
            }
        } else {
            const SplitStats::StorageState storageState =
                SplitStats::storageState();
            static const char *const kStorageNames[] = {
                "Session only", "Read only", "Saving...", "Save failed",
                "SD-backed",
            };
            const char *storage = kStorageNames[storageState];
            menu->drawText(storage,
                           x + w - 20 - Menu::textWidth(storage, FOOT_SZ),
                           y + 71, FOOT_SZ, FOOT_SZ,
                           storageState == SplitStats::STORAGE_FAILED
                               ? col(245, 95, 85, 255)
                               : cRowDim());

            char values[5][32];
            drawSectionHeader(menu, x + 16, rowY, w - 32, "SUMMARY");
            rowY += ROW_H;
            snprintf(values[0], sizeof(values[0]), "%lu", stats.attempts);
            const u32 pct = stats.attempts
                                ? (u32)(((u64)stats.finishes * 10000u) /
                                        stats.attempts)
                                : 0;
            snprintf(values[1], sizeof(values[1]), "%lu (%lu.%02lu pct)",
                     stats.finishes, pct / 100u, pct % 100u);
            snprintf(values[2], sizeof(values[2]), "%lu", stats.golds);
            if (stats.sumBestQf >= 0)
                formatStatsTime(stats.sumBestQf, values[3], sizeof(values[3]));
            else
                snprintf(values[3], sizeof(values[3]), "Incomplete");
            formatPlayedTime(stats.playedQf, values[4], sizeof(values[4]));
            static const char *const kLabels[] = {
                "Total attempts", "Finished", "Golds", "Sum of best",
                "Time played",
            };
            for (u8 i = 0; i < 5; i++, rowY += ROW_H)
                drawValueRow(menu, x + 16, rowY, w - 32,
                             kLabels[i], values[i], false, false, false);
        }

        const char *hint = mShowingSegments
                               ? SUSAMUNE_GLYPH_C " Select   "
                                 SUSAMUNE_GLYPH_X " Delete Gold   "
                                 SUSAMUNE_GLYPH_B " Summary   START Close"
                               : SUSAMUNE_GLYPH_A " Segments   "
                                 SUSAMUNE_GLYPH_B SUSAMUNE_GLYPH_SLASH
                                 "START Back";
        menu->drawText(hint,
                       x + (w - Menu::textWidth(hint, FOOT_SZ)) / 2,
                       y + h - 25, FOOT_SZ, FOOT_SZ, cFooter());

        if (mConfirmGoldDelete) {
            char question[40];
            snprintf(question, sizeof(question), "Delete Segment %d Gold?",
                     (int)mStatsSegment + 1);
            const char *confirm =
                SUSAMUNE_GLYPH_A " Yes    " SUSAMUNE_GLYPH_B " No";
            menu->fillBox(x + 78, y + 132, w - 156, 104,
                          JUtility::TColor(36, 30, 20, 250));
            menu->fillBox(x + 78, y + 132, w - 156, 3, cAccent());
            menu->drawText(
                question,
                x + (w - Menu::textWidth(question, ROW_SZ)) / 2,
                y + 153, ROW_SZ, ROW_SZ, cRowSel());
            menu->drawText(
                confirm,
                x + (w - Menu::textWidth(confirm, FOOT_SZ)) / 2,
                y + 205, FOOT_SZ, FOOT_SZ, cFooter());
        }
    }

    int mSel;
    bool mConfirmDelete;
    RawPromptInput mPromptInput;
    bool mEditingName;
    u8 mNameCursor;
    u8 mNamePage;
    u8 mNameLength;
    bool mNameUpper;
    char mNameBuffer[SUSAMUNE_ILING_PROFILE_NAME_SIZE];
    bool mShowingStats;
    bool mShowingSegments;
    bool mConfirmGoldDelete;
    s16 mStatsEntry;
    u8 mStatsSegment;
};

// ---------------------------------------------------------------------
// Ghost library -- fixed-slot, console-only asynchronous SD storage.
// ---------------------------------------------------------------------
class GhostsTab : public MenuTab {
public:
    GhostsTab()
        : mSel(0), mConfirmDelete(false), mDeleteImported(false),
          mDeleteSlot(-1), mConfirmSave(false), mSaveSlot(-1),
          mSaveIdentity(0), mChoice(CHOICE_NONE),
          mLaunch(LAUNCH_IDLE), mPBAction(PB_ACTION_NONE), mPBToken(0),
          mProtectedPBToken(0), mProtectedSavePending(false),
          mProtectedDeleteConfirm(false), mProtectedDeletePending(false) {
        mSaveName[0] = '\0';
        mPBName[0] = '\0';
        mPrimaryRef.selection = -1;
        mPrimaryRef.fingerprint = 0;
        mSecondaryRef.selection = -1;
        mSecondaryRef.fingerprint = 0;
    }

    const char *title() const override { return "Ghosts"; }
    bool grabsInput() const override {
        return mConfirmDelete || mConfirmSave || mChoice != CHOICE_NONE ||
               mLaunch != LAUNCH_IDLE || mPBAction != PB_ACTION_NONE ||
               mProtectedPBToken != 0;
    }
    bool suppressesBinds() const override {
        if (grabsInput()) return true;
        const u16 held = JUTGamePad::mPadStatus[0].mButton;
        return (held & (JUTGamePad::A | JUTGamePad::X |
                        JUTGamePad::Y | JUTGamePad::Z)) != 0;
    }

    bool beginProtectedPBSave(Menu *menu, u32 token) override {
        char name[SUSAMUNE_GHOST_NAME_SIZE];
        u32 identity = 0;
        if (token == 0 || !Ghost::hasUnsavedPBToken(token) ||
            !Ghost::copySaveableName(name, sizeof(name), &identity) ||
            identity != token) {
            return false;
        }

        mConfirmDelete = false;
        mConfirmSave = false;
        mChoice = CHOICE_NONE;
        mLaunch = LAUNCH_IDLE;
        mPBAction = PB_ACTION_NONE;
        mPBToken = 0;
        mProtectedPBToken = token;
        mProtectedSavePending = false;
        mProtectedDeleteConfirm = false;
        mProtectedDeletePending = false;
        mSaveIdentity = token;
        mSaveSlot = -2;
        strncpy(mSaveName, name, sizeof(mSaveName));
        mSaveName[sizeof(mSaveName) - 1] = '\0';
        mPromptInput.begin(JUTGamePad::B);
        prepareProtectedPBSave(menu);
        return true;
    }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (mProtectedPBToken != 0 && !mConfirmSave) {
            updateProtectedPBSave(menu, pad);
            return;
        }
        if (mPBAction != PB_ACTION_NONE) {
            updatePBAction(menu);
            return;
        }
        if (updateObserverLaunch(menu, pad)) return;
        if (mChoice != CHOICE_NONE) {
            updateChoice(menu, pad);
            return;
        }
        if (mConfirmDelete) {
            const u16 pressed = mPromptInput.update();
            if (pressed & JUTGamePad::A) {
                const bool started = mDeleteImported
                    ? GhostStorage::removeImported(mDeleteSlot)
                    : GhostStorage::remove(mDeleteSlot);
                if (started) {
                    menu->toast("Deleting ghost...");
                } else {
                    menu->toast(storageStatus());
                }
                mConfirmDelete = false;
                mPromptInput.clear();
            } else if (pressed & JUTGamePad::B) {
                mConfirmDelete = false;
                mPromptInput.clear();
            }
            return;
        }
        if (mConfirmSave) {
            const u16 pressed = mPromptInput.update();
            char name[SUSAMUNE_GHOST_NAME_SIZE];
            u32 identity = 0;
            const bool saveable = Ghost::copySaveableName(
                name, sizeof(name), &identity);
            const bool protectedSave = mProtectedPBToken != 0;
            if (protectedSave &&
                (!Ghost::hasUnsavedPBToken(mProtectedPBToken) ||
                 !saveable || identity != mProtectedPBToken)) {
                clearProtectedPBSave();
                return;
            }
            const bool changed = saveable && identity != mSaveIdentity;
            if (saveable) {
                strncpy(mSaveName, name, sizeof(mSaveName));
                mSaveName[sizeof(mSaveName) - 1] = '\0';
            }
            if (changed) {
                if (protectedSave) return;
                mSaveIdentity = identity;
                mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
                if (pressed & JUTGamePad::A) {
                    menu->toast("Ghost changed; confirm save again");
                }
                return;
            }
            if (pressed & JUTGamePad::A) {
                if (!saveable) {
                    menu->toast("No ghost recording to save");
                    mConfirmSave = false;
                } else if (GhostStorage::save(mSaveSlot, mSaveIdentity)) {
                    menu->toast("Saving ghost...");
                    mConfirmSave = false;
                    if (protectedSave) {
                        mProtectedSavePending = true;
                        mPromptInput.begin(JUTGamePad::B);
                    }
                } else {
                    menu->toast(storageStatus());
                    mConfirmSave = protectedSave;
                    if (protectedSave)
                        mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
                }
                if (!mConfirmSave && !mProtectedSavePending)
                    mPromptInput.clear();
            } else if (pressed & JUTGamePad::B) {
                mConfirmSave = false;
                mPromptInput.clear();
                if (protectedSave) cancelProtectedPBSave(menu);
            }
            return;
        }
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, SELECTION_COUNT);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, SELECTION_COUNT);
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            jumpSection(-1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            jumpSection(+1);
        }

        if (rapid & TMarioGamePad::Y) {
            const bool imports = mSel >= IMPORT_SCAN_SELECTION;
            const bool started = imports ? GhostStorage::scanImports()
                                         : GhostStorage::refresh();
            if (started) {
                menu->toast(imports ? "Scanning import folder..."
                                    : "Refreshing ghosts...");
            } else {
                menu->toast(storageStatus());
            }
            return;
        }

        if (rapid & TMarioGamePad::A) {
            activate(menu);
        } else if (rapid & TMarioGamePad::Z) {
            share(menu);
        } else if (rapid & TMarioGamePad::X) {
            if (mSel == TARGET_ROW) {
                if (GhostStorage::busy()) {
                    menu->toast(storageStatus());
                } else if (Ghost::observerActive()) {
                    Ghost::stopObserver();
                    menu->toast("Ghost watch ended");
                } else {
                    beginPBAction(menu, PB_ACTION_CLEAR_TARGET);
                }
            } else if (isPersonalSlot() || isImportedSlot()) {
                const bool imported = isImportedSlot();
                const int slot = imported ? selectedImportedSlot()
                                          : selectedPersonalSlot();
                const SusamuneGhostSlotInfo *info = imported
                    ? GhostStorage::importedSlot(slot)
                    : GhostStorage::slot(slot);
                if (info && (info->flags & SUSAMUNE_GHOST_SLOT_PRESENT) &&
                    (imported ||
                     !(info->flags & SUSAMUNE_GHOST_SLOT_UNSAFE)) &&
                    !GhostStorage::busy()) {
                    mDeleteImported = imported;
                    mDeleteSlot = slot;
                    mConfirmDelete = true;
                    mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
                }
            }
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        if (mProtectedPBToken != 0 && !mConfirmSave) {
            drawProtectedPBSave(menu, x, y, w);
            return;
        }
        if (mPBAction != PB_ACTION_NONE) {
            drawPBConfirmation(menu, x, y, w);
            return;
        }
        if (mLaunch != LAUNCH_IDLE) {
            drawWatchLoading(menu, x, y, w);
            return;
        }
        if (mChoice == CHOICE_ACTION) {
            drawActionChoice(menu, x, y, w);
            return;
        }
        if (mConfirmDelete) {
            drawDeleteConfirmation(menu, x, y, w);
            return;
        }
        if (mConfirmSave) {
            drawSaveConfirmation(menu, x, y, w);
            return;
        }

        const int listH = h - ROW_H - HELP_H;
        const int maxRows = listH / ROW_H;
        const int selectedRow = selectionRow(mSel);
        const int start = listScrollStart(selectedRow, DISPLAY_ROW_COUNT,
                                          maxRows);
        int end = start + maxRows;
        if (end > DISPLAY_ROW_COUNT) end = DISPLAY_ROW_COUNT;

        int ry = y;
        for (int row = start; row < end; row++, ry += ROW_H) {
            if (row == 0) {
                drawSectionHeader(menu, x, ry, w, "GHOST LIBRARY");
            } else if (row == 1) {
                drawValueRow(menu, x, ry, w, "Ghost display",
                             gSettings.valueLabel(SETTING_GHOST_DISPLAY),
                             mSel == DISPLAY_ROW, false, true);
            } else if (row == 2) {
                drawValueRow(menu, x, ry, w, "Ghost opacity",
                             gSettings.valueLabel(SETTING_GHOST_OPACITY),
                             mSel == OPACITY_ROW, false, true);
            } else if (row == 3) {
                drawValueRow(menu, x, ry, w, "Ghost appearance",
                             gSettings.valueLabel(SETTING_GHOST_APPEARANCE),
                             mSel == APPEARANCE_ROW, false, true);
            } else if (row == 4) {
                drawValueRow(
                    menu, x, ry, w, "Auto race target",
                    gSettings.getBool(SETTING_GHOST_LAST_SUCCESS)
                        ? "Last success" : "Last attempt",
                    mSel == AUTO_TARGET_ROW, false, true);
            } else if (row == 5) {
                drawValueRow(
                    menu, x, ry, w, "PB ghost save",
                    gSettings.valueLabel(SETTING_PB_GHOST_SAVE_POLICY),
                    mSel == PB_SAVE_ROW, false, true);
            } else if (row == 6) {
                drawValueRow(menu, x, ry, w, "PB profile",
                             ILing::pbProfileName(ILing::pbProfile()),
                             mSel == PROFILE_ROW, false, true);
            } else if (row == 7) {
                char target[24];
                targetValue(target, sizeof(target));
                drawValueRow(menu, x, ry, w, "Race target", target,
                             mSel == TARGET_ROW, false, true);
            } else if (row == PERSONAL_SUMMARY_DISPLAY) {
                char summary[48];
                catalogSummary(summary, sizeof(summary));
                drawSectionHeader(menu, x, ry, w, summary);
            } else if (row >= PERSONAL_DISPLAY_FIRST &&
                       row < IMPORTED_SUMMARY_DISPLAY) {
                int index;
                if (rangeRowSlot(row, PERSONAL_DISPLAY_FIRST, &index)) {
                    drawPersonalSlot(menu, x, ry, w, index);
                } else {
                    drawRangeHeader(menu, x, ry, w, "PERSONAL", index,
                                    PERSONAL_SELECTION_COUNT);
                }
            } else if (row == IMPORTED_SUMMARY_DISPLAY) {
                char summary[64];
                importedSummary(summary, sizeof(summary));
                drawSectionHeader(menu, x, ry, w, summary);
            } else if (row == IMPORT_SCAN_DISPLAY) {
                drawValueRow(menu, x, ry, w, "Scan import folder",
                             "drag and drop .smsghost",
                             mSel == IMPORT_SCAN_SELECTION, false, true);
            } else {
                int index;
                if (rangeRowSlot(row, IMPORTED_DISPLAY_FIRST, &index)) {
                    drawImportedSlot(menu, x, ry, w, index);
                } else {
                    drawRangeHeader(menu, x, ry, w, "IMPORTED", index,
                                    IMPORTED_SELECTION_COUNT);
                }
            }
        }

        drawScrollHints(menu, x, y, w, listH, start, end,
                        DISPLAY_ROW_COUNT);
        drawHelpLine(menu, x, y, w, h - ROW_H, selectionHelp());
        const bool settingRow = mSel == DISPLAY_ROW || mSel == OPACITY_ROW ||
                                mSel == APPEARANCE_ROW ||
                                mSel == AUTO_TARGET_ROW ||
                                mSel == PB_SAVE_ROW;
        const char *footer = mChoice == CHOICE_SECOND
            ? SUSAMUNE_GLYPH_A " Choose ghost 2  " SUSAMUNE_GLYPH_C
              " L" SUSAMUNE_GLYPH_SLASH "R Section  "
              SUSAMUNE_GLYPH_B " Back"
            : settingRow
              ? SUSAMUNE_GLYPH_A " Change  " SUSAMUNE_GLYPH_C
                " L" SUSAMUNE_GLYPH_SLASH "R Section  "
                SUSAMUNE_GLYPH_Y " Rescan"
            : SUSAMUNE_GLYPH_A " Select  " SUSAMUNE_GLYPH_C
              " L" SUSAMUNE_GLYPH_SLASH "R Section  "
              SUSAMUNE_GLYPH_X " Delete  " SUSAMUNE_GLYPH_Z " Export";
        menu->drawText(footer, x + 4, y + h - FOOT_SZ,
                       FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    const char *selectionHelp() const {
        if (mSel == DISPLAY_ROW)
            return "Shows or hides the selected ghost while racing or watching.";
        if (mSel == OPACITY_ROW)
            return "Changes how transparent the ghost looks in the stage.";
        if (mSel == APPEARANCE_ROW)
            return "Chooses Shadow Mario or Piantissimo as the ghost model.";
        if (mSel == AUTO_TARGET_ROW)
            return "Chooses whether a restart races the last attempt or success.";
        if (mSel == PB_SAVE_ROW)
            return "Chooses when a newly accepted PB ghost is saved to the SD.";
        if (mSel == PROFILE_ROW)
            return "Selects which IL profile supplies PBs and ghost names.";
        if (mSel == TARGET_ROW)
            return "Shows the pinned race ghost; press A or X to clear it.";
        if (mSel == IMPORT_SCAN_SELECTION)
            return "Finds .smsghost files copied into the import folder.";
        if (isPersonalSlot())
            return "A personal SD slot. Select to Race or Watch; X deletes it.";
        if (isImportedSlot())
            return "An imported ghost. Select to Race, Watch or export it.";
        return "Saved and imported ghosts available for racing or watching.";
    }

    enum Choice : u8 {
        CHOICE_NONE,
        CHOICE_ACTION,
        CHOICE_SECOND,
    };

    enum Launch : u8 {
        LAUNCH_IDLE,
        LAUNCH_ONE_PRIMARY,
        LAUNCH_TWO_PRIMARY,
        LAUNCH_TWO_SECONDARY,
    };

    enum PBAction : u8 {
        PB_ACTION_NONE,
        PB_ACTION_RACE,
        PB_ACTION_WATCH_ONE,
        PB_ACTION_WATCH_TWO,
        PB_ACTION_CLEAR_TARGET,
    };

    struct GhostRef {
        s16 selection;
        u32 fingerprint;
    };

    enum {
        RANGE_SIZE = 10,
        DISPLAY_ROW = 0,
        OPACITY_ROW = 1,
        APPEARANCE_ROW = 2,
        AUTO_TARGET_ROW = 3,
        PB_SAVE_ROW = 4,
        PROFILE_ROW = 5,
        TARGET_ROW = 6,
        PERSONAL_SELECTION_FIRST = 7,
        PERSONAL_SELECTION_COUNT = SUSAMUNE_GHOST_PROFILE_WRITABLE_ENTRIES,
        IMPORT_SCAN_SELECTION = PERSONAL_SELECTION_FIRST +
                                PERSONAL_SELECTION_COUNT,
        IMPORTED_SELECTION_FIRST = IMPORT_SCAN_SELECTION + 1,
        IMPORTED_SELECTION_COUNT = SUSAMUNE_GHOST_IMPORTED_MAX_ENTRIES,
        SELECTION_COUNT = IMPORTED_SELECTION_FIRST +
                          IMPORTED_SELECTION_COUNT,

        PERSONAL_RANGE_COUNT =
            (PERSONAL_SELECTION_COUNT + RANGE_SIZE - 1) / RANGE_SIZE,
        IMPORTED_RANGE_COUNT =
            (IMPORTED_SELECTION_COUNT + RANGE_SIZE - 1) / RANGE_SIZE,
        PERSONAL_SUMMARY_DISPLAY = 8,
        PERSONAL_DISPLAY_FIRST = PERSONAL_SUMMARY_DISPLAY + 1,
        PERSONAL_DISPLAY_COUNT = PERSONAL_SELECTION_COUNT +
                                 PERSONAL_RANGE_COUNT,
        IMPORTED_SUMMARY_DISPLAY = PERSONAL_DISPLAY_FIRST +
                                   PERSONAL_DISPLAY_COUNT,
        IMPORT_SCAN_DISPLAY = IMPORTED_SUMMARY_DISPLAY + 1,
        IMPORTED_DISPLAY_FIRST = IMPORT_SCAN_DISPLAY + 1,
        DISPLAY_ROW_COUNT = IMPORTED_DISPLAY_FIRST +
                            IMPORTED_SELECTION_COUNT +
                            IMPORTED_RANGE_COUNT,
        SECTION_COUNT = 1 + PERSONAL_RANGE_COUNT + 1 +
                        IMPORTED_RANGE_COUNT,
    };

    bool isPersonalSlot() const {
        return mSel >= PERSONAL_SELECTION_FIRST &&
               mSel < IMPORT_SCAN_SELECTION;
    }
    bool isImportedSlot() const {
        return mSel >= IMPORTED_SELECTION_FIRST;
    }
    int selectedPersonalSlot() const {
        return mSel - PERSONAL_SELECTION_FIRST;
    }
    int selectedImportedSlot() const {
        return mSel - IMPORTED_SELECTION_FIRST;
    }
    static int slotDisplayRow(int first, int slot) {
        return first + slot + slot / RANGE_SIZE + 1;
    }
    static int selectionRow(int selection) {
        if (selection < PERSONAL_SELECTION_FIRST) return selection + 1;
        if (selection < IMPORT_SCAN_SELECTION) {
            return slotDisplayRow(PERSONAL_DISPLAY_FIRST,
                                  selection - PERSONAL_SELECTION_FIRST);
        }
        if (selection == IMPORT_SCAN_SELECTION) return IMPORT_SCAN_DISPLAY;
        return slotDisplayRow(IMPORTED_DISPLAY_FIRST,
                              selection - IMPORTED_SELECTION_FIRST);
    }

    // Each range is one non-selectable heading followed by up to ten slots.
    // Selection IDs remain the storage slot IDs; only their display rows move.
    static bool rangeRowSlot(int row, int first, int *index) {
        const int offset = row - first;
        const int range = offset / (RANGE_SIZE + 1);
        const int within = offset % (RANGE_SIZE + 1);
        if (within == 0) {
            *index = range;
            return false;
        }
        *index = range * RANGE_SIZE + within - 1;
        return true;
    }

    static int sectionSelection(int section) {
        if (section == 0) return DISPLAY_ROW;
        section--;
        if (section < PERSONAL_RANGE_COUNT) {
            return PERSONAL_SELECTION_FIRST + section * RANGE_SIZE;
        }
        section -= PERSONAL_RANGE_COUNT;
        if (section == 0) return IMPORT_SCAN_SELECTION;
        section--;
        return IMPORTED_SELECTION_FIRST + section * RANGE_SIZE;
    }

    void jumpSection(int direction) {
        if (direction > 0) {
            for (int section = 0; section < SECTION_COUNT; section++) {
                const int selection = sectionSelection(section);
                if (selection > mSel) {
                    mSel = selection;
                    return;
                }
            }
            mSel = sectionSelection(0);
            return;
        }

        for (int section = SECTION_COUNT - 1; section >= 0; section--) {
            const int selection = sectionSelection(section);
            if (selection < mSel) {
                mSel = selection;
                return;
            }
        }
        mSel = sectionSelection(SECTION_COUNT - 1);
    }

    void clearProtectedPBSave() {
        mProtectedPBToken = 0;
        mConfirmSave = false;
        mSaveSlot = -1;
        mSaveIdentity = 0;
        mProtectedSavePending = false;
        mProtectedDeleteConfirm = false;
        mProtectedDeletePending = false;
        mPromptInput.clear();
    }

    void cancelProtectedPBSave(Menu *menu) {
        const u32 token = mProtectedPBToken;
        clearProtectedPBSave();
        WarpWheel::resumePBPrompt(token);
        menu->hide();
    }

    void prepareProtectedPBSave(Menu *menu) {
        if (!Ghost::hasUnsavedPBToken(mProtectedPBToken)) return;
        if (GhostStorage::busy()) return;
        if (!GhostStorage::catalogReady()) {
            if (GhostStorage::refresh())
                menu->toast("Refreshing ghosts...");
            return;
        }

        for (int slot = 0; slot < PERSONAL_SELECTION_COUNT; slot++) {
            const SusamuneGhostSlotInfo *info = GhostStorage::slot(slot);
            if (!info ||
                (info->flags & (SUSAMUNE_GHOST_SLOT_PRESENT |
                                SUSAMUNE_GHOST_SLOT_UNSAFE))) {
                continue;
            }
            mSel = PERSONAL_SELECTION_FIRST + slot;
            mSaveSlot = slot;
            mConfirmSave = true;
            mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
            return;
        }

        mSaveSlot = -1;
        if (!isPersonalSlot()) mSel = PERSONAL_SELECTION_FIRST;
        mPromptInput.begin(JUTGamePad::B | JUTGamePad::X);
        menu->toast("Choose a saved slot to delete first");
    }

    void updateProtectedPBSave(Menu *menu, TMarioGamePad *pad) {
        if (!Ghost::hasUnsavedPBToken(mProtectedPBToken)) {
            clearProtectedPBSave();
            return;
        }
        if (mProtectedDeleteConfirm) {
            const u16 pressed = mPromptInput.update();
            if (pressed & JUTGamePad::B) {
                mProtectedDeleteConfirm = false;
                mPromptInput.begin(JUTGamePad::B | JUTGamePad::X);
            } else if (pressed & JUTGamePad::A) {
                if (GhostStorage::remove(mDeleteSlot)) {
                    mProtectedDeleteConfirm = false;
                    mProtectedDeletePending = true;
                    mPromptInput.begin(JUTGamePad::B);
                    menu->toast("Deleting ghost...");
                } else {
                    menu->toast(storageStatus());
                    mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
                }
            }
            return;
        }
        if (mProtectedDeletePending) {
            if (mPromptInput.update() & JUTGamePad::B) {
                cancelProtectedPBSave(menu);
                return;
            }
            if (GhostStorage::busy()) return;
            mProtectedDeletePending = false;
            mSaveSlot = -2;
            mPromptInput.begin(JUTGamePad::B);
            prepareProtectedPBSave(menu);
            return;
        }
        if (mProtectedSavePending) {
            if (mPromptInput.update() & JUTGamePad::B) {
                cancelProtectedPBSave(menu);
                return;
            }
            if (GhostStorage::busy()) return;
            mProtectedSavePending = false;
            mConfirmSave = true;
            mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
            return;
        }
        if (mSaveSlot == -2) {
            if (mPromptInput.update() & JUTGamePad::B) {
                cancelProtectedPBSave(menu);
                return;
            }
            if (!GhostStorage::busy()) prepareProtectedPBSave(menu);
            return;
        }
        if (mSaveSlot == -1) {
            const u32 rapid = menu->navigationInput(pad);
            int slot = selectedPersonalSlot();
            if (rapid & TMarioGamePad::CSTICK_UP) {
                slot = wrap(slot - 1, PERSONAL_SELECTION_COUNT);
            } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
                slot = wrap(slot + 1, PERSONAL_SELECTION_COUNT);
            }
            mSel = PERSONAL_SELECTION_FIRST + slot;

            const u16 pressed = mPromptInput.update();
            if (pressed & JUTGamePad::B) {
                cancelProtectedPBSave(menu);
            } else if (pressed & JUTGamePad::X) {
                const SusamuneGhostSlotInfo *info = GhostStorage::slot(slot);
                if (!info ||
                    (info->flags & (SUSAMUNE_GHOST_SLOT_PRESENT |
                                    SUSAMUNE_GHOST_SLOT_UNSAFE)) !=
                        SUSAMUNE_GHOST_SLOT_PRESENT) {
                    menu->toast("Choose a writable saved ghost");
                    return;
                }
                mDeleteImported = false;
                mDeleteSlot = slot;
                mProtectedDeleteConfirm = true;
                mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
            }
            return;
        }

        // A completed save clears the PB token on its storage ACK. Reaching
        // idle with the same token means the request failed; offer a retry.
        mConfirmSave = true;
        mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
    }

    static const char *storageStatus() {
        return GhostStorage::statusText();
    }

    static u32 slotFingerprint(const SusamuneGhostSlotInfo &info) {
        const u8 *bytes = reinterpret_cast<const u8 *>(&info);
        u32 hash = 2166136261u;
        for (u32 i = 0; i < sizeof(info); i++) {
            hash = (hash ^ bytes[i]) * 16777619u;
        }
        return hash ? hash : 1u;
    }

    static bool selectionImported(int selection) {
        return selection >= IMPORTED_SELECTION_FIRST;
    }

    static int selectionSlot(int selection) {
        return selectionImported(selection)
            ? selection - IMPORTED_SELECTION_FIRST
            : selection - PERSONAL_SELECTION_FIRST;
    }

    static const SusamuneGhostSlotInfo *selectionInfo(int selection) {
        if (selection >= PERSONAL_SELECTION_FIRST &&
            selection < IMPORT_SCAN_SELECTION) {
            return GhostStorage::slot(selectionSlot(selection));
        }
        if (selection >= IMPORTED_SELECTION_FIRST &&
            selection < SELECTION_COUNT) {
            return GhostStorage::importedSlot(selectionSlot(selection));
        }
        return nullptr;
    }

    static bool captureRef(int selection, GhostRef *out) {
        if (!out) return false;
        const SusamuneGhostSlotInfo *info = selectionInfo(selection);
        if (!info ||
            (info->flags & (SUSAMUNE_GHOST_SLOT_PRESENT |
                            SUSAMUNE_GHOST_SLOT_UNSAFE)) !=
                SUSAMUNE_GHOST_SLOT_PRESENT) {
            return false;
        }
        out->selection = static_cast<s16>(selection);
        out->fingerprint = slotFingerprint(*info);
        return true;
    }

    static bool refStillValid(const GhostRef &ref) {
        GhostRef current;
        return captureRef(ref.selection, &current) &&
               current.fingerprint == ref.fingerprint;
    }

    static bool loadRef(const GhostRef &ref, bool observer,
                        bool secondary) {
        if (!refStillValid(ref)) return false;
        const int slot = selectionSlot(ref.selection);
        if (selectionImported(ref.selection)) {
            return observer
                ? GhostStorage::loadImportedObserver(slot, secondary)
                : GhostStorage::loadImported(slot);
        }
        return observer ? GhostStorage::loadObserver(slot, secondary)
                        : GhostStorage::load(slot);
    }

    static bool copyRefName(const GhostRef &ref, char *out, u32 size) {
        if (!out || size == 0 || !refStillValid(ref)) return false;
        const int slot = selectionSlot(ref.selection);
        return selectionImported(ref.selection)
            ? GhostStorage::copyImportedSlotName(slot, out, size)
            : GhostStorage::copySlotName(slot, out, size);
    }

    bool copyPBActionName(PBAction action, char *out, u32 size,
                          u32 *token) const {
        return action == PB_ACTION_CLEAR_TARGET
            ? Ghost::copyPlaybackUnsavedPBName(out, size, token)
            : Ghost::copyUnsavedPBName(out, size, token);
    }

    bool refreshPBAction() {
        u32 token = 0;
        char name[SUSAMUNE_GHOST_NAME_SIZE];
        if (!copyPBActionName(mPBAction, name, sizeof(name), &token)) {
            return false;
        }
        mPBToken = token;
        strncpy(mPBName, name, sizeof(mPBName));
        mPBName[sizeof(mPBName) - 1] = '\0';
        mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
        return true;
    }

    void executePBAction(Menu *menu, PBAction action) {
        switch (action) {
        case PB_ACTION_RACE:
            if (GhostStorage::busy()) {
                menu->toast(storageStatus());
            } else if (!loadRef(mPrimaryRef, false, false)) {
                menu->toast("Ghost row changed; choose again");
            } else {
                menu->toast("Loading ghost to race...");
            }
            mChoice = CHOICE_NONE;
            break;
        case PB_ACTION_WATCH_ONE:
            beginWatch(menu, false);
            break;
        case PB_ACTION_WATCH_TWO:
            beginWatch(menu, true);
            break;
        case PB_ACTION_CLEAR_TARGET:
            if (Ghost::observerActive()) {
                Ghost::stopObserver();
                menu->toast("Ghost watch ended");
            } else {
                Ghost::clearPlayback();
                menu->toast("Ghost race target cleared");
            }
            break;
        default:
            break;
        }
    }

    void finishPBAction(Menu *menu) {
        const PBAction action = mPBAction;
        mPBAction = PB_ACTION_NONE;
        mPBToken = 0;
        mPBName[0] = '\0';
        mPromptInput.clear();
        executePBAction(menu, action);
    }

    void beginPBAction(Menu *menu, PBAction action) {
        mPBAction = action;
        if (!refreshPBAction()) {
            finishPBAction(menu);
            return;
        }
        mChoice = CHOICE_NONE;
        gBinds.suppressUntilRelease();
    }

    void updatePBAction(Menu *menu) {
        if (!Ghost::hasUnsavedPBToken(mPBToken)) {
            if (!refreshPBAction()) finishPBAction(menu);
            return;
        }

        const u16 pressed = mPromptInput.update();
        if (pressed & JUTGamePad::B) {
            const bool returnToChoice = mPBAction == PB_ACTION_RACE ||
                mPBAction == PB_ACTION_WATCH_ONE ||
                mPBAction == PB_ACTION_WATCH_TWO;
            mPBAction = PB_ACTION_NONE;
            mPBToken = 0;
            mPBName[0] = '\0';
            mPromptInput.clear();
            if (returnToChoice) {
                mChoice = CHOICE_ACTION;
                mSel = mPrimaryRef.selection;
                mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                                   JUTGamePad::X | JUTGamePad::Y);
            }
            gBinds.suppressUntilRelease();
            return;
        }
        if (!(pressed & JUTGamePad::A)) return;

        if (!Ghost::discardUnsavedPB(mPBToken)) {
            if (!refreshPBAction()) finishPBAction(menu);
            return;
        }
        if (!refreshPBAction()) finishPBAction(menu);
    }

    void cancelLaunch(Menu *menu, const char *message) {
        Ghost::stopObserver();
        mLaunch = LAUNCH_IDLE;
        mPromptInput.clear();
        if (message) menu->toast(message);
    }

    bool beginWatch(Menu *menu, bool two) {
        // Every Watch attempt leaves the action/second-ghost modal first. A
        // rejected launch must give the surrounding menu its input back.
        mChoice = CHOICE_NONE;
        if (GhostStorage::busy()) {
            menu->toast(storageStatus());
            return false;
        }
        if (!gSettings.getBool(SETTING_GHOST_DISPLAY)) {
            mSel = DISPLAY_ROW;
            menu->toast("Enable Ghost Display before Watch");
            return false;
        }
        if (!refStillValid(mPrimaryRef) ||
            (two && (!refStillValid(mSecondaryRef) ||
                     mSecondaryRef.selection == mPrimaryRef.selection))) {
            menu->toast("Ghost row changed; choose again");
            return false;
        }
        if (!Ghost::beginObserverPreparation(two)) {
            menu->toast("Watch unavailable in this stage");
            return false;
        }
        if (!loadRef(mPrimaryRef, true, false)) {
            cancelLaunch(menu, storageStatus());
            return false;
        }
        mLaunch = two ? LAUNCH_TWO_PRIMARY : LAUNCH_ONE_PRIMARY;
        mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
        menu->toast(two ? "Loading ghost 1 of 2..."
                        : "Loading ghost to watch...");
        return true;
    }

    bool finishWatch(Menu *menu, int count) {
        if (!Ghost::startObserver()) {
            cancelLaunch(menu, "Ghost routes cannot be watched together");
            return false;
        }
        mLaunch = LAUNCH_IDLE;
        mPromptInput.clear();
        menu->hide();
        menu->toast(count == 2
            ? "Warping to 2-ghost watch - B/Start exits"
            : "Warping to ghost watch - B/Start exits");
        return true;
    }

    bool updateObserverLaunch(Menu *menu, TMarioGamePad *) {
        if (mLaunch == LAUNCH_IDLE) return false;
        if (mPromptInput.update() & JUTGamePad::B) {
            cancelLaunch(menu, "Ghost watch canceled");
            mPromptInput.clear();
            return true;
        }
        if (!Ghost::observerPreparing()) {
            mLaunch = LAUNCH_IDLE;
            mPromptInput.clear();
            menu->toast(storageStatus());
            return true;
        }
        if (GhostStorage::busy()) return true;

        if (mLaunch == LAUNCH_ONE_PRIMARY) {
            if (!Ghost::observerTrackReady(false)) {
                cancelLaunch(menu, storageStatus());
            } else {
                finishWatch(menu, 1);
            }
            return true;
        }
        if (mLaunch == LAUNCH_TWO_PRIMARY) {
            if (!Ghost::observerTrackReady(false) ||
                !refStillValid(mSecondaryRef) ||
                !loadRef(mSecondaryRef, true, true)) {
                cancelLaunch(menu, "Ghost 2 changed or could not load");
            } else {
                mLaunch = LAUNCH_TWO_SECONDARY;
                menu->toast("Loading ghost 2 of 2...");
            }
            return true;
        }
        if (!Ghost::observerTrackReady(true)) {
            cancelLaunch(menu, storageStatus());
        } else {
            finishWatch(menu, 2);
        }
        return true;
    }

    void updateChoice(Menu *menu, TMarioGamePad *pad) {
        if (mChoice == CHOICE_ACTION) {
            const u16 pressed = mPromptInput.update();
            if (pressed & JUTGamePad::B) {
                mChoice = CHOICE_NONE;
                mPromptInput.clear();
            } else if (pressed & JUTGamePad::A) {
                beginPBAction(menu, PB_ACTION_RACE);
            } else if (pressed & JUTGamePad::Y) {
                beginPBAction(menu, PB_ACTION_WATCH_ONE);
            } else if (pressed & JUTGamePad::X) {
                mChoice = CHOICE_SECOND;
                mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                                   JUTGamePad::X | JUTGamePad::Y);
                menu->toast("Choose a different ghost for marker 2");
            }
            return;
        }

        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, SELECTION_COUNT);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, SELECTION_COUNT);
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            jumpSection(-1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            jumpSection(+1);
        }
        const u16 pressed = mPromptInput.update();
        if (pressed & JUTGamePad::B) {
            mChoice = CHOICE_ACTION;
            mSel = mPrimaryRef.selection;
            mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                               JUTGamePad::X | JUTGamePad::Y);
            return;
        }
        if (pressed & JUTGamePad::A) {
            if (!captureRef(mSel, &mSecondaryRef)) {
                menu->toast("Choose a saved, validated ghost");
            } else if (mSecondaryRef.selection == mPrimaryRef.selection) {
                menu->toast("Choose a different second ghost");
            } else {
                beginPBAction(menu, PB_ACTION_WATCH_TWO);
            }
        }
    }

    void activate(Menu *menu) {
        if (mSel == DISPLAY_ROW) {
            gSettings.cycle(SETTING_GHOST_DISPLAY, 1);
            return;
        }
        if (mSel == OPACITY_ROW) {
            gSettings.cycle(SETTING_GHOST_OPACITY, 1);
            return;
        }
        if (mSel == APPEARANCE_ROW) {
            gSettings.cycle(SETTING_GHOST_APPEARANCE, 1);
            return;
        }
        if (mSel == AUTO_TARGET_ROW) {
            gSettings.cycle(SETTING_GHOST_LAST_SUCCESS, 1);
            return;
        }
        if (mSel == PB_SAVE_ROW) {
            gSettings.cycle(SETTING_PB_GHOST_SAVE_POLICY, 1);
            return;
        }
        if (GhostStorage::busy()) {
            menu->toast(storageStatus());
            return;
        }
        if (mSel == PROFILE_ROW) {
            ILing::cyclePbProfile(1);
            menu->toast(ILing::pbProfileName(ILing::pbProfile()));
            return;
        }
        if (mSel == TARGET_ROW) {
            if (Ghost::observerActive()) {
                Ghost::stopObserver();
                menu->toast("Ghost watch ended");
            } else if (Ghost::playbackPinned()) {
                Ghost::unpinPlayback();
                menu->toast("Library ghost unpinned");
            } else {
                menu->toast("No pinned library ghost");
            }
            return;
        }
        if (mSel == IMPORT_SCAN_SELECTION) {
            const bool started = GhostStorage::scanImports();
            menu->toast(started ? "Scanning import folder..."
                                : storageStatus());
            return;
        }

        const bool imported = isImportedSlot();
        const int index = imported ? selectedImportedSlot()
                                   : selectedPersonalSlot();
        const SusamuneGhostSlotInfo *info = imported
            ? GhostStorage::importedSlot(index) : GhostStorage::slot(index);
        if (!info) {
            const bool started = imported ? GhostStorage::refreshImported()
                                          : GhostStorage::refresh();
            if (started) {
                menu->toast(imported ? "Refreshing imports..."
                                     : "Refreshing ghosts...");
            } else {
                menu->toast(storageStatus());
            }
            return;
        }
        if (info->flags & SUSAMUNE_GHOST_SLOT_UNSAFE) {
            menu->toast("Ghost slot is read-only/unsafe");
            return;
        }
        const bool present = info->flags & SUSAMUNE_GHOST_SLOT_PRESENT;
        if (imported) {
            if (!present) {
                menu->toast("No imported ghost in this row");
                return;
            }
            if (!captureRef(mSel, &mPrimaryRef)) {
                menu->toast("Imported ghost changed; rescan");
                return;
            }
            mChoice = CHOICE_ACTION;
            mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                               JUTGamePad::X | JUTGamePad::Y);
            return;
        }
        if (present) {
            if (!captureRef(mSel, &mPrimaryRef)) {
                menu->toast("Ghost row changed; refresh");
                return;
            }
            mChoice = CHOICE_ACTION;
            mPromptInput.begin(JUTGamePad::A | JUTGamePad::B |
                               JUTGamePad::X | JUTGamePad::Y);
            return;
        }
        u32 identity = 0;
        if (!Ghost::copySaveableName(mSaveName, sizeof(mSaveName),
                                     &identity)) {
            menu->toast("No ghost recording to save");
            return;
        }
        mSaveSlot = index;
        mSaveIdentity = identity;
        mConfirmSave = true;
        mPromptInput.begin(JUTGamePad::A | JUTGamePad::B);
    }

    void share(Menu *menu) {
        if (!isPersonalSlot()) {
            menu->toast(isImportedSlot()
                            ? "Imported files are already shareable"
                            : "Select a personal ghost to export");
            return;
        }
        if (GhostStorage::busy()) {
            menu->toast(storageStatus());
            return;
        }
        const int index = selectedPersonalSlot();
        const SusamuneGhostSlotInfo *info = GhostStorage::slot(index);
        if (!info) {
            if (GhostStorage::refresh()) menu->toast("Refreshing ghosts...");
            else menu->toast(storageStatus());
            return;
        }
        if (info->flags & SUSAMUNE_GHOST_SLOT_UNSAFE) {
            menu->toast("Ghost slot is read-only/unsafe");
            return;
        }
        if (!(info->flags & SUSAMUNE_GHOST_SLOT_PRESENT)) {
            menu->toast("Select a saved ghost to export");
            return;
        }
        const bool started = GhostStorage::exportShare(index);
        menu->toast(started ? "Exporting .smsghost..." : storageStatus());
    }

    static void targetValue(char *out, u32 size) {
        Ghost::PlaybackInfo info;
        if (Ghost::observerActive()) {
            const int count = Ghost::observerGhostCount();
            if (Ghost::observerLoading()) {
                snprintf(out, size, "Watch %d loading", count);
            } else {
                snprintf(out, size, "Watching %d (%d/%d)", count,
                         Ghost::observerVisibleCount(), count);
            }
        } else if (Ghost::playbackPinned()) {
            if (GhostStorage::loadedImported()) {
                snprintf(out, size, "Imported %02d (pinned)",
                         GhostStorage::loadedImportedSlot() + 1);
            } else {
                const int loaded = GhostStorage::loadedSlot();
                if (loaded >= 0)
                    snprintf(out, size, "Slot %02d (pinned)", loaded + 1);
                else strncpy(out, "Pinned ghost", size);
            }
        } else if (Ghost::playbackInfo(&info)) {
            strncpy(out, "Auto target", size);
        } else {
            strncpy(out, "None", size);
        }
        if (size) out[size - 1] = '\0';
    }

    static void catalogSummary(char *out, u32 size) {
        int count = 0;
        u32 duration = 0;
        if (GhostStorage::catalogReady()) {
            for (int i = 0; i < PERSONAL_SELECTION_COUNT; i++) {
                const SusamuneGhostSlotInfo *info = GhostStorage::slot(i);
                if (info && (info->flags & SUSAMUNE_GHOST_SLOT_PRESENT)) {
                    count++;
                    duration += info->durationQf;
                }
            }
        }
        const u32 seconds = static_cast<u32>(
            static_cast<u64>(duration) * 1001u / 120000u);
        snprintf(out, size, "PERSONAL %d/45  %lu:%02lu / 10h", count,
                 seconds / 3600u, (seconds / 60u) % 60u);
    }

    static void importedSummary(char *out, u32 size) {
        int count = 0;
        if (GhostStorage::importedCatalogReady()) {
            for (int i = 0; i < IMPORTED_SELECTION_COUNT; i++) {
                const SusamuneGhostSlotInfo *info =
                    GhostStorage::importedSlot(i);
                if (info && (info->flags & SUSAMUNE_GHOST_SLOT_PRESENT))
                    count++;
            }
        }
        const u32 seconds = static_cast<u32>(
            static_cast<u64>(GhostStorage::importedTotalDurationQf()) *
            1001u / 120000u);
        const u32 overflow = GhostStorage::importedOverflowCount();
        if (overflow) {
            snprintf(out, size, "IMPORTED %d/12  %lu:%02lu  +%lu MORE",
                     count, seconds / 3600u, (seconds / 60u) % 60u,
                     overflow);
        } else {
            snprintf(out, size, "IMPORTED %d/12  %lu:%02lu", count,
                     seconds / 3600u, (seconds / 60u) % 60u);
        }
    }

    static const char *regionTag(u8 region) {
        switch (region) {
        case SUSAMUNE_GHOST_REGION_JP: return "[JP]";
        case SUSAMUNE_GHOST_REGION_US: return "[US]";
        case SUSAMUNE_GHOST_REGION_PAL: return "[PAL]";
        default: return "[?]";
        }
    }

    static void drawRangeHeader(Menu *menu, int x, int y, int w,
                                const char *catalog, int range,
                                int slotCount) {
        const int first = range * RANGE_SIZE + 1;
        int last = first + RANGE_SIZE - 1;
        if (last > slotCount) last = slotCount;
        char label[24];
        snprintf(label, sizeof(label), "%s %02d-%02d", catalog,
                 first, last);
        drawSectionHeader(menu, x, y, w, label);
    }

    void drawPersonalSlot(Menu *menu, int x, int y, int w, int index) const {
        char name[34];
        char label[40];
        char value[24];
        const SusamuneGhostSlotInfo *info = GhostStorage::slot(index);
        const char *shownValue = GhostStorage::catalogReady() ? "Empty"
                                                              : "Not scanned";
        if (info && (info->flags & SUSAMUNE_GHOST_SLOT_UNSAFE)) {
            snprintf(label, sizeof(label), "%02d (unsafe)", index + 1);
            shownValue = "Read-only";
        } else if (info && (info->flags & SUSAMUNE_GHOST_SLOT_PRESENT)) {
            if (!GhostStorage::copySlotName(index, name, sizeof(name))) {
                strncpy(name, "Unnamed ghost", sizeof(name));
                name[sizeof(name) - 1] = '\0';
            }
            snprintf(label, sizeof(label), "%02d %s", index + 1, name);
            ILing::formatTime(static_cast<s32>(info->durationQf), value,
                              sizeof(value));
            shownValue = value;
            if (GhostStorage::loadedSlot() == index && Ghost::playbackPinned())
                shownValue = "RACING";
            if (mChoice == CHOICE_SECOND &&
                mPrimaryRef.selection == PERSONAL_SELECTION_FIRST + index) {
                shownValue = "GHOST 1";
            }
        } else {
            snprintf(label, sizeof(label), "%02d (empty)", index + 1);
        }
        drawValueRow(menu, x, y, w, label, shownValue,
                     isPersonalSlot() && selectedPersonalSlot() == index,
                     false, true);
    }

    void drawImportedSlot(Menu *menu, int x, int y, int w, int index) const {
        char name[24];
        char label[40];
        char value[24];
        const SusamuneGhostSlotInfo *info =
            GhostStorage::importedSlot(index);
        const char *shownValue = GhostStorage::importedCatalogReady()
            ? "Empty" : "Not scanned";
        if (info && (info->flags & SUSAMUNE_GHOST_SLOT_UNSAFE)) {
            snprintf(label, sizeof(label), "%02d (unsafe)", index + 1);
            shownValue = "Rejected";
        } else if (info && (info->flags & SUSAMUNE_GHOST_SLOT_PRESENT)) {
            if (!GhostStorage::copyImportedSlotName(index, name,
                                                    sizeof(name))) {
                strncpy(name, "Unnamed ghost", sizeof(name));
                name[sizeof(name) - 1] = '\0';
            }
            snprintf(label, sizeof(label), "%02d %s %s", index + 1,
                     regionTag(info->region), name);
            ILing::formatTime(static_cast<s32>(info->durationQf), value,
                              sizeof(value));
            shownValue = value;
            if (GhostStorage::loadedImportedSlot() == index &&
                Ghost::playbackPinned()) {
                shownValue = "RACING";
            }
            if (mChoice == CHOICE_SECOND &&
                mPrimaryRef.selection == IMPORTED_SELECTION_FIRST + index) {
                shownValue = "GHOST 1";
            }
        } else {
            snprintf(label, sizeof(label), "%02d (empty)", index + 1);
        }
        drawValueRow(menu, x, y, w, label, shownValue,
                     isImportedSlot() && selectedImportedSlot() == index,
                     false, true);
    }

    void drawActionChoice(Menu *menu, int x, int y, int w) const {
        char name[48];
        if (!copyRefName(mPrimaryRef, name, sizeof(name))) {
            strncpy(name, "Selected ghost", sizeof(name));
            name[sizeof(name) - 1] = '\0';
        }
        const char *title = "What should this ghost do?";
        const char *actions =
            SUSAMUNE_GLYPH_A " Race   " SUSAMUNE_GLYPH_Y " Watch   "
            SUSAMUNE_GLYPH_X " Watch 2";
        const char *note = "Watch is visual-only for this loaded area";
        const char *cancel = SUSAMUNE_GLYPH_B " Cancel";
        menu->fillBox(x, y + 24, w, 150,
                      JUtility::TColor(18, 32, 46, 245));
        menu->fillBox(x, y + 24, w, 3, cAccent());
        menu->drawText(title,
                       x + (w - Menu::textWidth(title, ROW_SZ)) / 2,
                       y + 42, ROW_SZ, ROW_SZ, cRowSel());
        menu->drawText(name,
                       x + (w - Menu::textWidth(name, ROW_SZ)) / 2,
                       y + 68, ROW_SZ, ROW_SZ, cValue());
        menu->drawText(actions,
                       x + (w - Menu::textWidth(actions, ROW_SZ)) / 2,
                       y + 100, ROW_SZ, ROW_SZ, cRowSel());
        menu->drawText(note,
                       x + (w - Menu::textWidth(note, FOOT_SZ)) / 2,
                       y + 128, FOOT_SZ, FOOT_SZ, cRowDim());
        menu->drawText(cancel,
                       x + (w - Menu::textWidth(cancel, FOOT_SZ)) / 2,
                       y + 150, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawPBConfirmation(Menu *menu, int x, int y, int w) const {
        const char *title = "Discard unsaved PB ghost?";
        const char *note = "The selected ghost action will destroy it.";
        const char *hint = SUSAMUNE_GLYPH_A " Discard and continue    "
                           SUSAMUNE_GLYPH_B " Cancel";
        menu->fillBox(x, y + 24, w, 150,
                      JUtility::TColor(36, 30, 20, 245));
        menu->fillBox(x, y + 24, w, 3, cAccent());
        menu->drawText(title,
                       x + (w - Menu::textWidth(title, ROW_SZ)) / 2,
                       y + 42, ROW_SZ, ROW_SZ, cRowSel());
        int nameSize = ROW_SZ;
        while (nameSize > 12 &&
               Menu::textWidth(mPBName, nameSize) > w - 16) {
            nameSize--;
        }
        menu->drawText(mPBName,
                       x + (w - Menu::textWidth(mPBName, nameSize)) / 2,
                       y + 70, nameSize, nameSize, cValue());
        menu->drawText(note,
                       x + (w - Menu::textWidth(note, FOOT_SZ)) / 2,
                       y + 104, FOOT_SZ, FOOT_SZ, cRowDim());
        menu->drawText(hint,
                       x + (w - Menu::textWidth(hint, FOOT_SZ)) / 2,
                       y + 140, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawWatchLoading(Menu *menu, int x, int y, int w) const {
        const char *title = mLaunch == LAUNCH_TWO_SECONDARY
            ? "Loading visual ghost 2 of 2..."
            : mLaunch == LAUNCH_TWO_PRIMARY
                ? "Loading visual ghost 1 of 2..."
                : "Loading visual ghost...";
        const char *note = "Recording is paused while Watch prepares";
        const char *cancel = SUSAMUNE_GLYPH_B " Cancel";
        menu->fillBox(x, y + 34, w, 112,
                      JUtility::TColor(18, 32, 46, 245));
        menu->fillBox(x, y + 34, w, 3, cAccent());
        menu->drawText(title,
                       x + (w - Menu::textWidth(title, ROW_SZ)) / 2,
                       y + 56, ROW_SZ, ROW_SZ, cRowSel());
        menu->drawText(note,
                       x + (w - Menu::textWidth(note, FOOT_SZ)) / 2,
                       y + 88, FOOT_SZ, FOOT_SZ, cRowDim());
        menu->drawText(cancel,
                       x + (w - Menu::textWidth(cancel, FOOT_SZ)) / 2,
                       y + 116, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawDeleteConfirmation(Menu *menu, int x, int y, int w) const {
        char name[40];
        const bool copied = mDeleteImported
            ? GhostStorage::copyImportedSlotName(mDeleteSlot, name,
                                                 sizeof(name))
            : GhostStorage::copySlotName(mDeleteSlot, name, sizeof(name));
        if (!copied) {
            strncpy(name, "Selected ghost", sizeof(name));
            name[sizeof(name) - 1] = '\0';
        }
        const char *question = mDeleteImported
            ? "Delete imported file from SD?" : "Delete ghost from SD?";
        const char *hint = SUSAMUNE_GLYPH_A " Yes    " SUSAMUNE_GLYPH_B " No";
        menu->fillBox(x, y + 34, w, 104, JUtility::TColor(36, 30, 20, 245));
        menu->fillBox(x, y + 34, w, 3, cAccent());
        menu->drawText(question,
                       x + (w - Menu::textWidth(question, ROW_SZ)) / 2,
                       y + 52, ROW_SZ, ROW_SZ, cRowSel());
        menu->drawText(name,
                       x + (w - Menu::textWidth(name, ROW_SZ)) / 2,
                       y + 78, ROW_SZ, ROW_SZ, cValue());
        menu->drawText(hint,
                       x + (w - Menu::textWidth(hint, FOOT_SZ)) / 2,
                       y + 112, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawSaveConfirmation(Menu *menu, int x, int y, int w) const {
        char question[SUSAMUNE_GHOST_NAME_SIZE + 8];
        snprintf(question, sizeof(question), "Save %s?", mSaveName);
        int textSize = ROW_SZ;
        while (textSize > 12 &&
               Menu::textWidth(question, textSize) > w - 16) {
            textSize--;
        }
        char destination[32];
        snprintf(destination, sizeof(destination), "Personal slot %02d",
                 mSaveSlot + 1);
        const char *hint = SUSAMUNE_GLYPH_A " Yes    " SUSAMUNE_GLYPH_B " No";
        menu->fillBox(x, y + 34, w, 104, JUtility::TColor(22, 34, 42, 245));
        menu->fillBox(x, y + 34, w, 3, cAccent());
        menu->drawText(question,
                       x + (w - Menu::textWidth(question, textSize)) / 2,
                       y + 52, textSize, textSize, cRowSel());
        menu->drawText(destination,
                       x + (w - Menu::textWidth(destination, ROW_SZ)) / 2,
                       y + 80, ROW_SZ, ROW_SZ, cValue());
        menu->drawText(hint,
                       x + (w - Menu::textWidth(hint, FOOT_SZ)) / 2,
                       y + 112, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawProtectedPBSave(Menu *menu, int x, int y, int w) const {
        if (mProtectedDeleteConfirm) {
            drawDeleteConfirmation(menu, x, y, w);
            return;
        }
        const bool noSlot = mSaveSlot == -1;
        const char *title = mProtectedDeletePending
            ? "Deleting ghost to free a slot..."
            : mProtectedSavePending
                ? "Saving protected PB ghost..."
                : noSlot ? "All personal ghost slots are full"
                         : "Preparing protected PB save...";
        char selected[48];
        const char *note = storageStatus();
        if (noSlot) {
            char name[32];
            if (!GhostStorage::copySlotName(selectedPersonalSlot(), name,
                                            sizeof(name))) {
                strncpy(name, "Saved ghost", sizeof(name));
                name[sizeof(name) - 1] = '\0';
            }
            snprintf(selected, sizeof(selected), "Slot %02d: %s",
                     selectedPersonalSlot() + 1, name);
            note = selected;
        }
        const char *hint = noSlot
            ? SUSAMUNE_GLYPH_C " Up/Down Choose  " SUSAMUNE_GLYPH_X
              " Delete  " SUSAMUNE_GLYPH_B " Back"
            : SUSAMUNE_GLYPH_B " Back to PB protection";
        menu->fillBox(x, y + 34, w, 112,
                      JUtility::TColor(22, 34, 42, 245));
        menu->fillBox(x, y + 34, w, 3, cAccent());
        menu->drawText(title,
                       x + (w - Menu::textWidth(title, ROW_SZ)) / 2,
                       y + 54, ROW_SZ, ROW_SZ, cRowSel());
        menu->drawText(note,
                       x + (w - Menu::textWidth(note, FOOT_SZ)) / 2,
                       y + 86, FOOT_SZ, FOOT_SZ, cRowDim());
        menu->drawText(hint,
                       x + (w - Menu::textWidth(hint, FOOT_SZ)) / 2,
                       y + 116, FOOT_SZ, FOOT_SZ, cFooter());
    }

    int mSel;
    bool mConfirmDelete;
    bool mDeleteImported;
    int mDeleteSlot;
    bool mConfirmSave;
    int mSaveSlot;
    u32 mSaveIdentity;
    char mSaveName[SUSAMUNE_GHOST_NAME_SIZE];
    Choice mChoice;
    Launch mLaunch;
    PBAction mPBAction;
    u32 mPBToken;
    char mPBName[SUSAMUNE_GHOST_NAME_SIZE];
    u32 mProtectedPBToken;
    bool mProtectedSavePending;
    bool mProtectedDeleteConfirm;
    bool mProtectedDeletePending;
    RawPromptInput mPromptInput;
    GhostRef mPrimaryRef;
    GhostRef mSecondaryRef;
};

// ---------------------------------------------------------------------
// Records -- nested achievement details and regional/global statistics.
// ---------------------------------------------------------------------
namespace {

const char *recordTierName(Records::Tier tier) {
    const char *name = Records::tierName(tier);
    return name ? name : "";
}

Color recordTierColor(Records::Tier tier) {
    switch (tier) {
    case Records::TIER_BRONZE:   return col(205, 127, 50, 255);
    case Records::TIER_SILVER:   return col(205, 215, 225, 255);
    case Records::TIER_GOLD:     return col(255, 196, 40, 255);
    case Records::TIER_DIAMOND:  return col(60, 160, 255, 255);
    case Records::TIER_DEMON:    return col(186, 65, 230, 255);
    case Records::TIER_FRONTIER: return col(60, 210, 100, 255);
    default:                     return cRow();
    }
}

const char *recordText(const char *text, const char *fallback = "") {
    return text && text[0] ? text : fallback;
}

int fittedRecordTextSize(const char *text, int maxWidth, int largest,
                         int smallest) {
    text = recordText(text);
    for (int size = largest; size > smallest; size--) {
        if (Menu::textWidth(text, size) <= maxWidth) return size;
    }
    return smallest;
}

}  // namespace

class RecordsTab : public MenuTab {
public:
    RecordsTab()
        : mPage(PAGE_ROOT), mSel(0), mCategory(0), mAchievement(0), mWorld(0),
          mScope(RecordsPersistence::SCOPE_GLOBAL) {}

    const char *title() const override { return "Records"; }
    bool suppressesBinds() const override {
        u16 consumed = mPage == PAGE_ROOT ? 0 : JUTGamePad::B;
        if (mPage == PAGE_ROOT || mPage == PAGE_ACHIEVEMENT_CATEGORIES ||
            mPage == PAGE_WORLDS ||
            (mPage == PAGE_ACHIEVEMENTS &&
             Records::categoryAchievementCount(
                 (Records::Category)mCategory) > 0)) {
            consumed |= JUTGamePad::A;
        }
        const u16 held = JUTGamePad::mPadStatus[0].mButton;
        return (held & consumed) != 0;
    }
    void update(Menu *menu, TMarioGamePad *pad) override {
        const u32 rapid = menu->navigationInput(pad);
        if ((rapid & TMarioGamePad::B) && mPage != PAGE_ROOT) {
            if (mPage == PAGE_ACHIEVEMENT_DETAIL) {
                mPage = PAGE_ACHIEVEMENTS;
            } else if (mPage == PAGE_ACHIEVEMENTS) {
                mPage = PAGE_ACHIEVEMENT_CATEGORIES;
            } else if (mPage == PAGE_WORLD_DETAIL) {
                mPage = PAGE_WORLDS;
            } else {
                mPage = PAGE_ROOT;
            }
            return;
        }

        switch (mPage) {
        case PAGE_ROOT:
            moveSelection(rapid, 5);
            if (mSel == 3 &&
                (rapid & (TMarioGamePad::CSTICK_LEFT |
                          TMarioGamePad::CSTICK_RIGHT | TMarioGamePad::A))) {
                const int direction =
                    (rapid & TMarioGamePad::CSTICK_LEFT) ? -1 : 1;
                mScope = (u8)wrap(mScope + direction,
                                  RecordsPersistence::SCOPE_COUNT);
            } else if (mSel == 4 &&
                       (rapid & (TMarioGamePad::CSTICK_LEFT |
                                 TMarioGamePad::CSTICK_RIGHT |
                                 TMarioGamePad::A))) {
                gSettings.cycle(SETTING_ACHIEVEMENT_NOTIFICATIONS, +1);
            } else if (rapid & TMarioGamePad::A) {
                mPage = mSel == 0 ? PAGE_ACHIEVEMENT_CATEGORIES
                                  : mSel == 1 ? PAGE_STATS_OVERVIEW
                                              : PAGE_WORLDS;
                mSel = 0;
            }
            break;
        case PAGE_ACHIEVEMENT_CATEGORIES:
            if (rapid & TMarioGamePad::CSTICK_UP)
                mCategory = wrap(mCategory - 1, Records::CATEGORY_COUNT);
            else if (rapid & TMarioGamePad::CSTICK_DOWN)
                mCategory = wrap(mCategory + 1, Records::CATEGORY_COUNT);
            if (rapid & TMarioGamePad::A) {
                mAchievement = 0;
                mPage = PAGE_ACHIEVEMENTS;
            }
            break;
        case PAGE_ACHIEVEMENTS:
            {
            const int count = Records::categoryAchievementCount(
                (Records::Category)mCategory);
            if (count <= 0) {
                mAchievement = 0;
                break;
            }
            if (rapid & TMarioGamePad::CSTICK_UP)
                mAchievement = wrap(mAchievement - 1, count);
            else if (rapid & TMarioGamePad::CSTICK_DOWN)
                mAchievement = wrap(mAchievement + 1, count);
            if (rapid & TMarioGamePad::A)
                mPage = PAGE_ACHIEVEMENT_DETAIL;
            break;
            }
        case PAGE_ACHIEVEMENT_DETAIL:
            break;
        case PAGE_STATS_OVERVIEW:
            break;
        case PAGE_WORLDS:
            if (rapid & TMarioGamePad::CSTICK_UP)
                mWorld = wrap(mWorld - 1, Records::WORLD_COUNT);
            else if (rapid & TMarioGamePad::CSTICK_DOWN)
                mWorld = wrap(mWorld + 1, Records::WORLD_COUNT);
            if (rapid & TMarioGamePad::A)
                mPage = PAGE_WORLD_DETAIL;
            break;
        case PAGE_WORLD_DETAIL:
            break;
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        switch (mPage) {
        case PAGE_ROOT: drawRoot(menu, x, y, w, h); break;
        case PAGE_ACHIEVEMENT_CATEGORIES:
            drawAchievementCategories(menu, x, y, w, h);
            break;
        case PAGE_ACHIEVEMENTS: drawAchievements(menu, x, y, w, h); break;
        case PAGE_ACHIEVEMENT_DETAIL:
            drawAchievementDetail(menu, x, y, w, h);
            break;
        case PAGE_STATS_OVERVIEW: drawOverview(menu, x, y, w, h); break;
        case PAGE_WORLDS: drawWorlds(menu, x, y, w, h); break;
        case PAGE_WORLD_DETAIL: drawWorldDetail(menu, x, y, w, h); break;
        }
    }

private:
    enum Page : u8 {
        PAGE_ROOT,
        PAGE_ACHIEVEMENT_CATEGORIES,
        PAGE_ACHIEVEMENTS,
        PAGE_ACHIEVEMENT_DETAIL,
        PAGE_STATS_OVERVIEW,
        PAGE_WORLDS,
        PAGE_WORLD_DETAIL,
    };

    Records::AchievementId selectedAchievement() const {
        int selection = mAchievement;
        const Records::Category category = (Records::Category)mCategory;
        for (int tier = 0; tier < Records::TIER_COUNT; tier++) {
            const int count = Records::categoryTierAchievementCount(
                category, (Records::Tier)tier);
            if (selection < count) {
                return Records::categoryTierAchievement(
                    category, (Records::Tier)tier, selection);
            }
            selection -= count;
        }
        return Records::ACHIEVEMENT_INVALID;
    }

    static int categoryVisualRows(Records::Category category) {
        int rows = 0;
        for (int tier = 0; tier < Records::TIER_COUNT; tier++) {
            const int count = Records::categoryTierAchievementCount(
                category, (Records::Tier)tier);
            if (count > 0) rows += count + 1;
        }
        return rows;
    }

    static int achievementVisualRow(Records::Category category,
                                    int selection) {
        int row = 0;
        for (int tier = 0; tier < Records::TIER_COUNT; tier++) {
            const int count = Records::categoryTierAchievementCount(
                category, (Records::Tier)tier);
            if (count <= 0) continue;
            row++;
            if (selection < count) return row + selection;
            selection -= count;
            row += count;
        }
        return 0;
    }

    void moveSelection(u32 rapid, int count) {
        if (rapid & TMarioGamePad::CSTICK_UP)
            mSel = wrap(mSel - 1, count);
        else if (rapid & TMarioGamePad::CSTICK_DOWN)
            mSel = wrap(mSel + 1, count);
    }

    static void formatDuration(u32 seconds, char *out, u32 size) {
        snprintf(out, size, "%lu:%02lu", seconds / 3600,
                 (seconds / 60) % 60);
    }

    static u32 successRate(u32 finishes, u32 attempts) {
        if (!attempts) return 0;
        if (finishes >= attempts) return 100;
        return (finishes * 100u + attempts / 2) / attempts;
    }

    static u32 scopedWorldTime(RecordsPersistence::Scope scope,
                               Records::World world) {
        static const u8 stats[] = {
            Records::STAT_AREA_BIANCO_SECONDS,
            Records::STAT_AREA_RICCO_SECONDS,
            Records::STAT_AREA_GELATO_SECONDS,
            Records::STAT_AREA_PINNA_SECONDS,
            Records::STAT_AREA_SIRENA_SECONDS,
            Records::STAT_AREA_NOKI_SECONDS,
            Records::STAT_AREA_PIANTA_SECONDS,
        };
        if (world == Records::WORLD_DELFINO) {
            return RecordsPersistence::stat(
                       scope, Records::STAT_AREA_AIRSTRIP_SECONDS) +
                   RecordsPersistence::stat(
                       scope, Records::STAT_AREA_DELFINO_SECONDS) +
                   RecordsPersistence::stat(
                       scope, Records::STAT_AREA_CORONA_SECONDS);
        }
        return world < Records::WORLD_DELFINO
                   ? RecordsPersistence::stat(
                         scope, (Records::StatId)stats[world])
                   : 0;
    }

    static const char *storageStatus() {
#if IS_EMULATOR
        return "Session only (Dolphin alpha)";
#else
        if (!RecordsPersistence::persistent()) return "Progress unavailable";
        if (!RecordsPersistence::writable()) return "Progress is read-only";
        if (RecordsPersistence::lastError() != 0) return "Progress save error";
        if (RecordsPersistence::pending()) return "Saving progress...";
        if (RecordsPersistence::dirty()) return "Progress not checkpointed";
        return "Progress saved";
#endif
    }

    void drawRoot(Menu *menu, int x, int y, int w, int h) const {
        char unlocked[20];
        snprintf(unlocked, sizeof(unlocked), "%d " SUSAMUNE_GLYPH_SLASH
                 " %d", Records::unlockedCount(), Records::achievementCount());
        int ry = y;
        drawSectionHeader(menu, x, ry, w, "BROWSE");
        ry += ROW_H;
        drawValueRow(menu, x, ry, w, "Achievements  >", unlocked,
                     mSel == 0, false, true);
        ry += ROW_H;
        drawValueRow(menu, x, ry, w, "Statistics overview  >", nullptr,
                     mSel == 1, false, true);
        ry += ROW_H;
        drawValueRow(menu, x, ry, w, "Worlds  >", nullptr,
                     mSel == 2, false, true);
        ry += ROW_H;
        drawSectionHeader(menu, x, ry, w, "OPTIONS");
        ry += ROW_H;
        drawValueRow(menu, x, ry, w, "Region",
                     RecordsPersistence::scopeName(
                         (RecordsPersistence::Scope)mScope),
                     mSel == 3, false, true);
        ry += ROW_H;
        drawValueRow(menu, x, ry, w, "Unlock popup and chime",
                     gSettings.valueLabel(SETTING_ACHIEVEMENT_NOTIFICATIONS),
                     mSel == 4, false, true);
        const char *help = mSel == 0
            ? "Browse every achievement and its unlock requirements."
            : mSel == 1
                ? "View overall play, attempt, PB and ghost statistics."
                : mSel == 2
                    ? "View time and completion statistics for each world."
                    : mSel == 3
                        ? "Switch Records between this region and all regions."
                        : "Shows a popup and chime when an achievement unlocks.";
        drawHelpLine(menu, x, y, w, h - 52, help);
        menu->drawText("Moonshine V2.2.0 RC3",
                       x + 4, y + h - 44, FOOT_SZ, FOOT_SZ, cRowDim());
        menu->drawText(storageStatus(), x + 4, y + h - 24,
                       FOOT_SZ, FOOT_SZ,
                       RecordsPersistence::lastError() ?
                           col(255, 130, 100, 255) : cFooter());
    }

    void drawAchievementCategories(Menu *menu, int x, int y, int w,
                                   int h) const {
        int ry = y;
        for (int i = 0; i < Records::CATEGORY_COUNT; i++, ry += ROW_H) {
            const Records::Category category = (Records::Category)i;
            char count[20];
            snprintf(count, sizeof(count), "%d " SUSAMUNE_GLYPH_SLASH " %d",
                     Records::categoryUnlockedCount(category),
                     Records::categoryAchievementCount(category));
            drawValueRow(menu, x, ry, w,
                         recordText(Records::categoryName(category),
                                    "(unnamed)"),
                         count, i == mCategory, false, true);
        }
        menu->drawText(SUSAMUNE_GLYPH_A " Open    "
                       SUSAMUNE_GLYPH_B " Back",
                       x + 4, y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawAchievements(Menu *menu, int x, int y, int w, int h) {
        const Records::Category category = (Records::Category)mCategory;
        const int count = Records::categoryAchievementCount(category);
        drawSectionHeader(menu, x, y, w,
                          recordText(Records::categoryName(category),
                                     "Achievements"));
        if (count <= 0) {
            menu->drawText("(none)", x + 4, y + ROW_H,
                           ROW_SZ, ROW_SZ, cRowDim());
            menu->drawText(SUSAMUNE_GLYPH_B " Back", x + 4,
                           y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
            return;
        }
        const int listY = y + ROW_H;
        const int listH = h - ROW_H - FOOT_SZ;
        const int maxRows = listH / ROW_H;
        const int rows = categoryVisualRows(category);
        const int selectedRow = achievementVisualRow(category, mAchievement);
        const int start = listScrollStart(selectedRow, rows, maxRows);
        int end = start + maxRows;
        if (end > rows) end = rows;
        int ry = listY;
        int row = 0;
        int selection = 0;
        for (int tier = 0; tier < Records::TIER_COUNT; tier++) {
            const Records::Tier recordTier = (Records::Tier)tier;
            const int tierCount = Records::categoryTierAchievementCount(
                category, recordTier);
            if (tierCount <= 0) continue;

            if (row >= start && row < end) {
                drawSectionHeader(menu, x, ry, w,
                                  recordTierName(recordTier));
                ry += ROW_H;
            }
            row++;
            for (int i = 0; i < tierCount; i++, row++, selection++) {
                if (row < start || row >= end) continue;
                const Records::AchievementId id =
                    Records::categoryTierAchievement(category, recordTier, i);
                const Records::AchievementDesc *desc =
                    id == Records::ACHIEVEMENT_INVALID
                        ? nullptr : Records::achievement(id);
                const bool isUnlocked = desc && Records::unlocked(id);
                char progress[12];
                const char *value = nullptr;
                const u16 streak = category == Records::CATEGORY_STREAKS
                    ? Records::streakProgress(id)
                    : 0;
                if (streak) {
                    const u32 goal = streak & 0xff;
                    snprintf(progress, sizeof(progress),
                             "%lu " SUSAMUNE_GLYPH_SLASH " %lu",
                             isUnlocked ? goal : (u32)(streak >> 8), goal);
                    value = progress;
                }
                drawValueRow(menu, x, ry, w,
                             desc ? recordText(desc->name, "(unnamed)")
                                  : "(unavailable)",
                             value, selection == mAchievement,
                             isUnlocked, true);
                ry += ROW_H;
            }
        }
        drawScrollHints(menu, x, listY, w, listH, start, end, rows);
        menu->drawText(SUSAMUNE_GLYPH_A " Details    "
                       SUSAMUNE_GLYPH_B " Back",
                       x + 4, y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawAchievementDetail(Menu *menu, int x, int y, int w, int h) const {
        const Records::AchievementId id = selectedAchievement();
        const Records::AchievementDesc *desc =
            id == Records::ACHIEVEMENT_INVALID ? nullptr
                                               : Records::achievement(id);
        if (!desc) return;
        const Color color = recordTierColor(desc->tier);
        const char *name = recordText(desc->name, "Unnamed achievement");
        const bool isUnlocked = Records::unlocked(id);
        menu->fillBox(x, y + 4, w, 3, color);
        const int nameSize = fittedRecordTextSize(name, w - 8, 22, 13);
        menu->drawText(name, x + 4, y + 20, nameSize, nameSize, cRowSel());
        menu->drawText(recordTierName(desc->tier), x + 4, y + 56,
                       ROW_SZ, ROW_SZ, color);
        const char *state = isUnlocked ? "UNLOCKED" : "LOCKED";
        menu->drawText(state,
                       x + w - Menu::textWidth(state, ROW_SZ) - 8,
                       y + 56, ROW_SZ, ROW_SZ,
                       isUnlocked ? cValue() : cRowDim());

        const char *description = recordText(desc->description,
                                             "Description unavailable.");
        const int descriptionSize = fittedRecordTextSize(
            description, w - 8, 14, 10);
        menu->drawText(description, x + 4, y + 88,
                       descriptionSize, descriptionSize, cRow());

        menu->drawText(SUSAMUNE_GLYPH_B " Back", x + 4,
                       y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawOverview(Menu *menu, int x, int y, int w, int h) const {
        static const char names[] =
            "Time played\0Attempts\0ILs finished\0PBs earned\0Deaths\0"
            "Creation time\0Ghosts saved\0Ghost time saved";
        static const u8 offsets[] = {0, 12, 21, 34, 45, 52, 66, 79};
        static const u8 stats[] = {
            Records::STAT_PLAY_SECONDS, Records::STAT_ATTEMPTS,
            Records::STAT_IL_FINISHES, Records::STAT_PBS_EARNED,
            Records::STAT_DEATHS, Records::STAT_CREATION_SECONDS,
            Records::STAT_GHOSTS_SAVED, Records::STAT_GHOST_TIME_SAVED_QF,
        };
        char value[24];
        const RecordsPersistence::Scope scope =
            (RecordsPersistence::Scope)mScope;
        drawSectionHeader(menu, x, y, w,
                          recordText(RecordsPersistence::scopeName(scope),
                                     "Statistics"));
        int ry = y + ROW_H;
        for (u32 i = 0; i < sizeof(stats); i++, ry += ROW_H) {
            const u32 amount = RecordsPersistence::stat(
                scope, (Records::StatId)stats[i]);
            if (stats[i] == Records::STAT_PLAY_SECONDS ||
                stats[i] == Records::STAT_CREATION_SECONDS) {
                formatDuration(amount, value, sizeof(value));
            } else if (stats[i] == Records::STAT_GHOST_TIME_SAVED_QF) {
                const u32 seconds = static_cast<u32>(
                    static_cast<u64>(amount) * 1001u / 120000u);
                formatDuration(seconds, value, sizeof(value));
            } else if (stats[i] == Records::STAT_IL_FINISHES) {
                const u32 attempts = RecordsPersistence::stat(
                    scope, Records::STAT_ATTEMPTS);
                snprintf(value, sizeof(value),
                         "%lu (%lu pct)", amount,
                         successRate(amount, attempts));
            } else {
                snprintf(value, sizeof(value), "%lu", amount);
            }
            drawValueRow(menu, x, ry, w, names + offsets[i], value,
                         false, false, false);
        }

        char achievements[20];
        snprintf(achievements, sizeof(achievements),
                 "%d " SUSAMUNE_GLYPH_SLASH " %d", Records::unlockedCount(),
                 Records::achievementCount());
        drawValueRow(menu, x, ry, w, "Achievements", achievements,
                     false, false, false);
        menu->drawText(SUSAMUNE_GLYPH_B " Back", x + 4,
                       y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void drawWorlds(Menu *menu, int x, int y, int w, int h) {
        const int maxRows = (h - ROW_H - FOOT_SZ) / ROW_H;
        const int start = listScrollStart(mWorld, Records::WORLD_COUNT,
                                          maxRows);
        int end = start + maxRows;
        if (end > Records::WORLD_COUNT) end = Records::WORLD_COUNT;
        const RecordsPersistence::Scope scope =
            (RecordsPersistence::Scope)mScope;
        drawSectionHeader(menu, x, y, w,
                          recordText(RecordsPersistence::scopeName(scope),
                                     "Worlds"));
        int ry = y + ROW_H;
        for (int i = start; i < end; i++, ry += ROW_H) {
            char duration[24];
            formatDuration(scopedWorldTime(scope, (Records::World)i),
                           duration, sizeof(duration));
            drawValueRow(menu, x, ry, w,
                         recordText(Records::worldName((Records::World)i),
                                    "(unknown)"),
                         duration, i == mWorld, false, true);
        }
        drawScrollHints(menu, x, y + ROW_H, w, h - ROW_H - FOOT_SZ,
                        start, end, Records::WORLD_COUNT);
        menu->drawText(SUSAMUNE_GLYPH_A " Open    "
                       SUSAMUNE_GLYPH_B " Back", x + 4,
                       y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    void formatPBSummary(Records::World world, bool anyPercent,
                         char *out, u32 size) const {
        u8 coverage = 0;
        u8 goal = 0;
        s32 qf = 0;
        const bool complete = Records::worldPBSummary(
            world, !anyPercent, &coverage, &goal, &qf);
        if (!goal) {
            snprintf(out, size, "Unavailable");
            return;
        }

        if (!complete) {
            snprintf(out, size,
                     "Incomplete %lu " SUSAMUNE_GLYPH_SLASH " %lu",
                     (u32)coverage, (u32)goal);
            return;
        }

        char time[24];
        ILing::formatTime(qf, time, sizeof(time));
        snprintf(out, size, "%s (%lu " SUSAMUNE_GLYPH_SLASH " %lu)",
                 time, (u32)coverage, (u32)goal);
    }

    void drawWorldDetail(Menu *menu, int x, int y, int w, int h) const {
        const Records::World world = (Records::World)mWorld;
        const RecordsPersistence::Scope scope =
            (RecordsPersistence::Scope)mScope;
        char header[48];
        snprintf(header, sizeof(header), "%s - %s",
                 recordText(Records::worldName(world), "World"),
                 recordText(RecordsPersistence::scopeName(scope), "Region"));
        drawSectionHeader(menu, x, y, w, header);

        const u32 attempts = RecordsPersistence::stat(
            scope, Records::worldAttemptStat(world));
        const u32 finishes = RecordsPersistence::stat(
            scope, Records::worldFinishStat(world));
        char value[48];
        int ry = y + ROW_H;
        formatDuration(scopedWorldTime(scope, world), value, sizeof(value));
        drawValueRow(menu, x, ry, w, "Time", value, false, false, false);
        ry += ROW_H;
        snprintf(value, sizeof(value), "%lu", attempts);
        drawValueRow(menu, x, ry, w, "Attempts", value,
                     false, false, false);
        ry += ROW_H;
        snprintf(value, sizeof(value), "%lu", finishes);
        drawValueRow(menu, x, ry, w, "Finishes", value,
                     false, false, false);
        ry += ROW_H;
        snprintf(value, sizeof(value), "%lu pct",
                 successRate(finishes, attempts));
        drawValueRow(menu, x, ry, w, "Success rate", value,
                     false, false, false);
        ry += ROW_H;

        char pbHeader[64];
        snprintf(pbHeader, sizeof(pbHeader), "PBs: %s - %s",
                 recordText(Records::currentRegionScope(), "Region"),
                 recordText(ILing::pbProfileName(ILing::pbProfile()),
                            "Profile"));
        drawSectionHeader(menu, x, ry, w, pbHeader);
        ry += ROW_H;

        formatPBSummary(world, true, value, sizeof(value));
        drawValueRow(menu, x, ry, w, "Any percent PBs", value,
                     false, false, false);
        ry += ROW_H;
        formatPBSummary(world, false, value, sizeof(value));
        drawValueRow(menu, x, ry, w, "All-IL PBs", value,
                     false, false, false);

        menu->drawText(SUSAMUNE_GLYPH_B " Back", x + 4,
                       y + h - FOOT_SZ, FOOT_SZ, FOOT_SZ, cFooter());
    }

    u8 mPage;
    int mSel;
    int mCategory;
    int mAchievement;
    int mWorld;
    u8 mScope;
};

#if ENABLE_DEBUG_WARPS
class WarpPresetsTab : public MenuTab {
public:
    WarpPresetsTab() : mSel(0) {}
    const char *title() const override { return "Warps"; }
    bool suppressesBinds() const override {
        return (JUTGamePad::mPadStatus[0].mButton & JUTGamePad::A) != 0;
    }

    void update(Menu *menu, TMarioGamePad *pad) override {
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP)
            mSel = wrap(mSel - 1, Warp::kNumPresets);
        else if (rapid & TMarioGamePad::CSTICK_DOWN)
            mSel = wrap(mSel + 1, Warp::kNumPresets);
        if (rapid & TMarioGamePad::A) {
            const WarpDescriptor &d = Warp::kPresets[mSel];
            StageLoader::cancel();
            Warp::request(d.area, d.episode, d.overrideArea, d.extraFlag);
            menu->hide();
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        const int maxRows = h / ROW_H;
        const int start = listScrollStart(mSel, Warp::kNumPresets, maxRows);
        int end = start + maxRows;
        if (end > Warp::kNumPresets) end = Warp::kNumPresets;
        int ry = y;
        for (int i = start; i < end; i++, ry += ROW_H) {
            const bool selected = i == mSel;
            if (selected) {
                drawRowHighlight(menu, x, ry, w, ROW_H);
                menu->drawText(">", x - 2, ry, ROW_SZ, ROW_SZ, cAccent());
            }
            menu->drawText(Warp::kPresets[i].name, x + 22, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());
        }
        drawScrollHints(menu, x, y, w, h, start, end, Warp::kNumPresets);
    }

private:
    int mSel;
};

class WarpStagesTab : public MenuTab {
public:
    WarpStagesTab() : mArea(0), mEpisode(0) {}
    const char *title() const override { return "Stages"; }
    bool suppressesBinds() const override {
        return (JUTGamePad::mPadStatus[0].mButton & JUTGamePad::A) != 0;
    }

    void update(Menu *menu, TMarioGamePad *pad) override {
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP)
            mArea = wrap(mArea - 1, WARP_NUM_STAGES);
        else if (rapid & TMarioGamePad::CSTICK_DOWN)
            mArea = wrap(mArea + 1, WARP_NUM_STAGES);
        if (rapid & TMarioGamePad::CSTICK_LEFT)
            mEpisode = wrap(mEpisode - 1, WARP_NUM_EPISODES);
        else if (rapid & TMarioGamePad::CSTICK_RIGHT)
            mEpisode = wrap(mEpisode + 1, WARP_NUM_EPISODES);
        if (rapid & TMarioGamePad::A) {
            StageLoader::cancel();
            Warp::request(mArea, mEpisode, -1, -1);
            menu->hide();
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        const int maxRows = h / ROW_H;
        const int start = listScrollStart(mArea, WARP_NUM_STAGES, maxRows);
        int end = start + maxRows;
        if (end > WARP_NUM_STAGES) end = WARP_NUM_STAGES;
        const int epX = x + 230;
        char digit[2] = {'1', '\0'};
        int ry = y;
        for (int i = start; i < end; i++, ry += ROW_H) {
            const bool selected = i == mArea;
            if (selected) drawRowHighlight(menu, x, ry, w, ROW_H);
            menu->drawText(Warp::kStageNames[i], x + 4, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());
            for (int e = 0; e < WARP_NUM_EPISODES; e++) {
                digit[0] = (char)('1' + e);
                menu->drawText(digit, epX + e * 26, ry, ROW_SZ, ROW_SZ,
                               selected && e == mEpisode ? cAccent()
                               : selected ? cRow() : cRowDim());
            }
        }
    }

private:
    int mArea;
    int mEpisode;
};
#endif

// ---------------------------------------------------------------------
// PB Safety
// ---------------------------------------------------------------------
class PBSafetyTab final : public MenuTab {
public:
    PBSafetyTab() : mSel(0) {}

    const char *title() const override { return "PB Safety"; }
    const char *summary() const override {
        return "Find and fix every setting that can stop an IL PB.";
    }
    int rootAlertCount() const override {
        return gSettings.ilPbBlockerCount();
    }
    bool suppressesBinds() const override {
        const u16 held = JUTGamePad::mPadStatus[0].mButton;
        return (held & (JUTGamePad::A | JUTGamePad::X)) != 0;
    }

    void update(Menu *menu, TMarioGamePad *pad) override {
        const int count = Settings::ilPbSettingCount();
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP)
            mSel = (u8)wrap(mSel - 1, count);
        else if (rapid & TMarioGamePad::CSTICK_DOWN)
            mSel = (u8)wrap(mSel + 1, count);

        if (rapid & TMarioGamePad::X) {
            int fixed = 0;
            bool restart = false;
            for (int i = 0; i < count; i++) {
                const SettingId id = Settings::ilPbSettingAt(i);
                if (!gSettings.blocksIlPb(id)) continue;
                restart |= Settings::invalidatesIlAttempt(id);
                gSettings.set(id, Settings::ilPbSafeValue(id));
                fixed++;
            }
            menu->toast(!fixed ? "PB settings already safe"
                        : restart ? "PB settings fixed - restart the IL"
                                  : "PB settings fixed");
        } else if (rapid & TMarioGamePad::A) {
            const SettingId id = Settings::ilPbSettingAt(mSel);
            if (gSettings.blocksIlPb(id)) {
                gSettings.set(id, Settings::ilPbSafeValue(id));
                menu->toast(Settings::invalidatesIlAttempt(id)
                                ? "PB setting fixed - restart the IL"
                                : "PB setting fixed");
            } else {
                menu->toast("Setting is already PB-safe");
            }
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        const int count = Settings::ilPbSettingCount();
        const int footerH = ROW_H;
        const int listH = h - HELP_H - footerH;
        const int maxRows = listH / ROW_H;
        const int start = listScrollStart(mSel, count, maxRows);
        int end = start + maxRows;
        if (end > count) end = count;
        int ry = y;
        for (int i = start; i < end; i++, ry += ROW_H) {
            const SettingId id = Settings::ilPbSettingAt(i);
            const bool blocked = gSettings.blocksIlPb(id);
            char value[32];
            snprintf(value, sizeof(value), "%s - %s",
                     gSettings.valueLabel(id), blocked ? "PB OFF" : "Safe");
            drawValueRowColored(menu, x, ry, w, Settings::name(id), value,
                                i == mSel, false, false,
                                blocked ? col(255, 72, 72, 255) : cValue());
        }
        drawScrollHints(menu, x, y, w, listH, start, end, count);
        const SettingId selected = Settings::ilPbSettingAt(mSel);
        const char *help = !gSettings.blocksIlPb(selected)
            ? "This setting is PB-safe."
            : selected == SETTING_ILING_RECORDING
                  ? "PB recording is off; other Records remain eligible."
                  : "This setting invalidates an active IL attempt.";
        drawHelpLine(menu, x, y, w, h - footerH, help);
        menu->drawText(SUSAMUNE_GLYPH_A " Fix   " SUSAMUNE_GLYPH_X
                       " Fix all",
                       x + 4, y + h - FOOT_SZ,
                       FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    u8 mSel;
};

static_assert(sizeof(PBSafetyTab) <= 8, "PB Safety tab grew unexpectedly");

// ---------------------------------------------------------------------
// Category settings tab (generic settings renderer)
//
// Renders every setting tagged with `mCat` -- one tab per SettingCategory.
// The setting/value store lives in settings.*; this only navigates and
// draws. The filtered id list is rebuilt each frame from Settings metadata
// (SETTING_COUNT is tiny), so adding a setting needs no change here.
// ---------------------------------------------------------------------
namespace {

const char kCategoryTitles[] =
    "Gameplay and QoL\0Savestates\0Practice tools\0Appearance\0"
    "HUD and displays\0Timer and splits\0RNG controls\0Shined";

enum CategoryTitleOffset {
    TITLE_QOL       = 0,
    TITLE_SAVESTATE = TITLE_QOL + sizeof("Gameplay and QoL"),
    TITLE_MISC      = TITLE_SAVESTATE + sizeof("Savestates"),
    TITLE_COSMETIC  = TITLE_MISC + sizeof("Practice tools"),
    TITLE_UI        = TITLE_COSMETIC + sizeof("Appearance"),
    TITLE_TIMER     = TITLE_UI + sizeof("HUD and displays"),
    TITLE_RNG       = TITLE_TIMER + sizeof("Timer and splits"),
    TITLE_STARRED   = TITLE_RNG + sizeof("RNG controls"),
};

static_assert(TITLE_STARRED + sizeof("Shined") == sizeof(kCategoryTitles),
              "category title offsets changed");
static_assert(SETTING_COUNT <= 0x100, "setting ids no longer fit in a byte");
static_assert(SETTING_CAT_COUNT <= 0x100, "setting categories no longer fit in a byte");
const u8 kStarredCategory = SETTING_CAT_COUNT;
const u8 kRngCategory = SETTING_CAT_COUNT + 1;

struct SettingPage {
    const char *name;
    const char *help;
    const u8 *ids;
    u8 count;
};

const u8 kGameplayCoreSettings[] = {
    SETTING_FAST_TEXT,
    SETTING_INFINITE_LIVES,
    SETTING_INFINITE_JUICE,
    SETTING_FREE_PAUSE,
    SETTING_EXIT_AREA_EVERYWHERE,
};
const u8 kGameplaySkipSettings[] = {
    SETTING_FMV_SKIPS,
    SETTING_INTRO_SKIP,
    SETTING_UNLOCK_YOSHI,
    SETTING_UNLOCK_NOZZLES,
    SETTING_RESPAWN_SHINES,
};
const u8 kGameplayWorldSettings[] = {
    SETTING_FLUDD_SECRETS,
    SETTING_AREA_LOCK,
    SETTING_DISABLE_BLUE_COIN,
    SETTING_FAST_PIANTISSIMO,
    SETTING_DISABLE_THIRD_CHOMPLET_AGGRO,
    SETTING_YOSHI_NOZZLE_SAVE_PROMPT,
    SETTING_DISABLE_RETAIL_PAUSE,
};
const SettingPage kGameplayPages[] = {
    {"General gameplay", "Everyday speed and convenience options.",
     kGameplayCoreSettings, sizeof(kGameplayCoreSettings)},
    {"Skips and unlocks", "Skip downtime or unlock route requirements.",
     kGameplaySkipSettings, sizeof(kGameplaySkipSettings)},
    {"World rules", "Change how stages and their objects behave.",
     kGameplayWorldSettings, sizeof(kGameplayWorldSettings)},
};

const u8 kTimerDisplaySettings[] = {
    SETTING_TIMER_SUNSHINE_VISIBILITY,
    SETTING_TIMER_QFT_VISIBILITY,
    SETTING_TIMER_SECTIONS,
    SETTING_LEVEL_SPLITS,
};
const u8 kTimerFreezeSettings[] = {
    SETTING_TIMER_FREEZE_DURATION,
    SETTING_TIMER_FREEZE_YELLOW_COIN,
    SETTING_TIMER_FREEZE_RED_COIN,
    SETTING_TIMER_FREEZE_BLUE_COIN,
    SETTING_TIMER_FREEZE_ITEM,
    SETTING_TIMER_FREEZE_TAKE,
    SETTING_TIMER_FREEZE_DROP,
    SETTING_TIMER_FREEZE_PUT,
    SETTING_TIMER_FREEZE_JUMP,
    SETTING_TIMER_FREEZE_DOUBLE_JUMP,
    SETTING_TIMER_FREEZE_TRIPLE_JUMP,
    SETTING_TIMER_FREEZE_SPIN_JUMP,
    SETTING_TIMER_FREEZE_DIVE,
    SETTING_TIMER_FREEZE_DIVE_ROLLOUT,
    SETTING_TIMER_FREEZE_DIVE_GETUP,
    SETTING_TIMER_FREEZE_LEDGE_GRAB,
    SETTING_TIMER_FREEZE_WALL_KICK,
    SETTING_TIMER_FREEZE_ROPE_JUMP,
    SETTING_TIMER_FREEZE_BOUNCE,
    SETTING_TIMER_FREEZE_MOVING_PLATFORM,
    SETTING_TIMER_FREEZE_AIRGRAB,
    SETTING_TIMER_FREEZE_TALK,
    SETTING_TIMER_FREEZE_DEMO,
    SETTING_TIMER_FREEZE_CLEANED,
    SETTING_TIMER_FREEZE_BOWSER,
    SETTING_TIMER_FREEZE_PETEY_WAKEUP,
    SETTING_TIMER_FREEZE_EEL_ACTIVATE,
    SETTING_TIMER_FREEZE_EEL_TOOTH,
    SETTING_TIMER_FREEZE_YOSHI,
};
const SettingPage kTimerPages[] = {
    {"Timer and splits", "Choose the timers, history and split overlays shown.",
     kTimerDisplaySettings, sizeof(kTimerDisplaySettings)},
    {"QFT freezes", "Choose which actions briefly freeze the QFT display.",
     kTimerFreezeSettings, sizeof(kTimerFreezeSettings)},
};

const u8 kRngGeneralSettings[] = {
    SETTING_PATTERN_SELECTOR,
    SETTING_ANY_FRUIT_YOSHI,
};
const u8 kRngBossSettings[] = {
    SETTING_KING_BOO_ALWAYS_FRUIT,
    SETTING_PETEY_NO_TORNADO,
    SETTING_PETEY_ROUTE,
};
const u8 kRngRiccoSettings[] = {
    SETTING_RICCO_CRANE_SPEED,
    SETTING_RICCO_FRUIT_MACHINE,
};
const u8 kRngCourseSettings[] = {
    SETTING_GELATO_RED_COIN_FISH_PATTERN,
    SETTING_GELATO_BLUE_BIRD_PATTERN,
};
const SettingPage kRngPages[] = {
    {"General patterns", "General-purpose deterministic practice helpers.",
     kRngGeneralSettings, sizeof(kRngGeneralSettings)},
    {"Boss fights", "Force the supported Petey and King Boo outcomes.",
     kRngBossSettings, sizeof(kRngBossSettings)},
    {"Ricco Harbor", "Control Ricco's crane and casino fruit machine.",
     kRngRiccoSettings, sizeof(kRngRiccoSettings)},
    {"Birds and fish (testing)", "Experimental repeatable course patterns.",
     kRngCourseSettings, sizeof(kRngCourseSettings)},
};

const u8 kDisplayMovementSettings[] = {
    SETTING_WALLKICK_DISPLAY,
    SETTING_ROLLOUT_DISPLAY,
    SETTING_DUST_DISPLAY,
};
const u8 kDisplayPracticeSettings[] = {
    SETTING_PINNA_HIDDEN_ITEMS,
    SETTING_HIDDEN_ITEM_LABELS,
    SETTING_ENEMY_HURTBOXES,
    SETTING_HURTBOX_TARGET,
    SETTING_RICCO_RACE_CHECKPOINTS,
};
const u8 kDisplayOtherSettings[] = {
    SETTING_SHOW_BGM_SLOTS,
    SETTING_RESTART_QUEUED_FEEDBACK,
};
const SettingPage kDisplayPages[] = {
    {"Movement displays", "Frame feedback for movement practice.",
     kDisplayMovementSettings,
     sizeof(kDisplayMovementSettings)},
    {"Practice visuals", "Reveal practice-only objects, volumes and routes.",
     kDisplayPracticeSettings, sizeof(kDisplayPracticeSettings)},
    {"Other HUD", "Small status messages and diagnostic counters.",
     kDisplayOtherSettings, sizeof(kDisplayOtherSettings)},
};

const u8 kAppearanceMarioSettings[] = {
    SETTING_SHINE_OUTFIT,
    SETTING_HELMET_APPEARANCE,
    SETTING_CAP_APPEARANCE,
    SETTING_SHADES_APPEARANCE,
    SETTING_SHINE_SHIRT_APPEARANCE,
};
const u8 kAppearanceWorldSettings[] = {
    SETTING_MUTE_BGM,
    SETTING_REPLACE_EPISODE_NAMES,
    SETTING_SHINY_SHINES,
    SETTING_VISIBLE_GOOP,
};
const SettingPage kAppearancePages[] = {
    {"Mario appearance", "Choose Mario's optional costume pieces.",
     kAppearanceMarioSettings,
     sizeof(kAppearanceMarioSettings)},
    {"World and audio", "Cosmetic stage, Shine and music changes.",
     kAppearanceWorldSettings,
     sizeof(kAppearanceWorldSettings)},
};

const u8 kSettingSectionStarts[] = {
    SETTING_NOZZLE_LOCK,
    SETTING_DISABLE_Z_MENU,
    SETTING_ATTEMPT_COUNTER,
    SETTING_FORCE_BOX_GAME,
    SETTING_SAVE_RNG_STATE,
    SETTING_SAVESTATE_FEEDBACK,
};
const char kSettingSectionNames[] =
    "LEVEL RULES\0MENU CONTROLS\0COUNTERS\0BOX GAME\0STATE\0FEEDBACK";

const char *settingHelp(SettingId id) {
    if ((id >= SETTING_TIMER_FREEZE_YELLOW_COIN &&
         id <= SETTING_TIMER_FREEZE_BOUNCE) ||
        (id >= SETTING_TIMER_FREEZE_JUMP &&
         id <= SETTING_TIMER_FREEZE_EEL_TOOTH) ||
        id == SETTING_TIMER_FREEZE_DIVE_ROLLOUT ||
        id == SETTING_TIMER_FREEZE_DIVE_GETUP ||
        id == SETTING_TIMER_FREEZE_MOVING_PLATFORM ||
        id == SETTING_TIMER_FREEZE_AIRGRAB) {
        return "Freezes the QFT display when this event occurs.";
    }

    switch (id) {
    case SETTING_FAST_TEXT: return "Speeds up dialogue and message boxes.";
    case SETTING_INFINITE_LIVES: return "Prevents Mario's life count from dropping.";
    case SETTING_INFINITE_JUICE: return "Prevents Yoshi's juice meter from draining.";
    case SETTING_FREE_PAUSE: return "Allows pausing during normally locked moments.";
    case SETTING_EXIT_AREA_EVERYWHERE: return "Allows Exit Area wherever pausing is available.";
    case SETTING_FMV_SKIPS: return "Skips supported pre-rendered movies.";
    case SETTING_INTRO_SKIP: return "Skips the boot logos on the next launch.";
    case SETTING_UNLOCK_YOSHI: return "Makes Yoshi available without story progression.";
    case SETTING_UNLOCK_NOZZLES: return "Makes Rocket and Turbo boxes available.";
    case SETTING_RESPAWN_SHINES: return "Respawns one-time Shines for repeat practice.";
    case SETTING_FLUDD_SECRETS: return "Controls when FLUDD is removed in secret stages.";
    case SETTING_AREA_LOCK: return "Turns any stage exit into a restart of that area.";
    case SETTING_DISABLE_BLUE_COIN: return "Stops blue coins setting their collected flag.";
    case SETTING_FAST_PIANTISSIMO: return "Forces a Piantissimo race speed preset.";
    case SETTING_DISABLE_THIRD_CHOMPLET_AGGRO: return "Stops the third Chomplet targeting Mario early.";
    case SETTING_YOSHI_NOZZLE_SAVE_PROMPT: return "Shows a warning before Yoshi or nozzle progress saves.";
    case SETTING_DISABLE_RETAIL_PAUSE: return "Uses only Moonshine's pause handling.";
    case SETTING_SAVE_RNG_STATE: return "Includes the game RNG seed in practice savestates.";
    case SETTING_SAVESTATE_FEEDBACK: return "Shows a status popup after saving or loading a state.";
    case SETTING_NOZZLE_LOCK: return "Forces Mario to keep the selected FLUDD nozzle.";
    case SETTING_FORCE_PLAZA_EVENTS: return "Keeps Delfino Plaza story events available.";
    case SETTING_NEVER_PAUSE_IGT: return "Keeps in-game time running while paused.";
    case SETTING_SHADOW_MARIO_HP: return "Shows Shadow Mario's remaining health.";
    case SETTING_STAGE_INTRO_SKIP: return "Skips the stage-opening camera sequence.";
    case SETTING_DEATHLESS_BLOOPER: return "Prevents Blooper surfing crashes from killing Mario.";
    case SETTING_NO_SHINE_ANIM: return "Skips the Shine-get animation.";
    case SETTING_FRUIT_NEVER_TIMEOUT: return "Stops loose fruit disappearing over time.";
    case SETTING_DISABLE_Z_MENU: return "Stops Z opening the retail debug-style menu.";
    case SETTING_DISABLE_WARPS: return "Disables Moonshine's quick stage warps.";
    case SETTING_ATTEMPT_COUNTER: return "Tracks and displays attempts for the current practice.";
    case SETTING_ATTEMPT_IN_STAGE_CONTROLS: return "Allows attempt-count binds while inside a stage.";
    case SETTING_FORCE_BOX_GAME: return "Forces a selected Delfino crate-game layout.";
    case SETTING_MUTE_BGM: return "Mutes background music without muting sound effects.";
    case SETTING_REPLACE_EPISODE_NAMES: return "Shows internal IDs instead of episode names.";
    case SETTING_SHINE_OUTFIT: return "Uses Mario's Shine celebration outfit.";
    case SETTING_SHINY_SHINES: return "Adds a stronger gleam to Shine Sprites.";
    case SETTING_VISIBLE_GOOP: return "Makes normally hidden goop easier to see.";
    case SETTING_HELMET_APPEARANCE: return "Controls when Mario's helmet is visible.";
    case SETTING_CAP_APPEARANCE: return "Controls when Mario's cap is visible.";
    case SETTING_SHADES_APPEARANCE: return "Controls when Mario's sunglasses are visible.";
    case SETTING_SHINE_SHIRT_APPEARANCE: return "Controls when Mario's Shine shirt is visible.";
    case SETTING_TIMER_SUNSHINE_VISIBILITY: return "Chooses when the main Sunshine timer is shown.";
    case SETTING_TIMER_QFT_VISIBILITY: return "Chooses when the bottom-left QFT is shown.";
    case SETTING_TIMER_FREEZE_DURATION: return "Sets how long event-triggered QFT freezes remain.";
    case SETTING_TIMER_SECTIONS: return "Shows the recent QFT section history.";
    case SETTING_LEVEL_SPLITS: return "Shows route splits during supported ILs.";
    case SETTING_PATTERN_SELECTOR: return "Enables repeatable patterns for supported practice.";
    case SETTING_ILING_FANFARE: return "Plays a fanfare when an IL personal best is accepted.";
    case SETTING_ILING_RECENT: return "Shows a short history of recently completed ILs.";
    case SETTING_ILING_RECORDING: return "Allows completed ILs to update their saved PB.";
    case SETTING_ILING_POPUP: return "Shows a popup when an IL PB is accepted.";
    case SETTING_ILING_SHORT_NAMES: return "Uses compact route names in the recent-IL display.";
    case SETTING_ANY_FRUIT_YOSHI: return "Lets any fruit colour open a Yoshi egg.";
    case SETTING_KING_BOO_ALWAYS_FRUIT: return "Forces King Boo's valid fruit and no-fruit cycle.";
    case SETTING_PETEY_NO_TORNADO: return "Prevents Petey using his tornado attack.";
    case SETTING_PETEY_ROUTE: return "Forces Petey's N1, S1, S2, S3 flight route.";
    case SETTING_RICCO_CRANE_SPEED: return "Randomises the crane within the selected speed band.";
    case SETTING_RICCO_FRUIT_MACHINE: return "Forces the Ricco fruit machine to give durians.";
    case SETTING_GELATO_RED_COIN_FISH_PATTERN: return "Selects a repeatable Gelato 6 fish pattern.";
    case SETTING_GELATO_BLUE_BIRD_PATTERN: return "Selects a repeatable blue-bird pattern.";
    case SETTING_WALLKICK_DISPLAY: return "Shows the timing of Mario's last wall kick.";
    case SETTING_ROLLOUT_DISPLAY: return "Shows the effective A-hold frames of a rollout.";
    case SETTING_DUST_DISPLAY: return "Shows frames from landing until the rollout input.";
    case SETTING_SHOW_BGM_SLOTS: return "Shows free music slots for audio diagnostics.";
    case SETTING_RESTART_QUEUED_FEEDBACK: return "Shows when a restart has been queued.";
    case SETTING_PINNA_HIDDEN_ITEMS: return "Reveals spray-hidden fruit and coin locations globally.";
    case SETTING_HIDDEN_ITEM_LABELS: return "Adds Fruit and Coin names to hidden-item markers.";
    case SETTING_ENEMY_HURTBOXES: return "Chooses how enemy damage volumes are drawn.";
    case SETTING_HURTBOX_TARGET: return "Shows all enemies or only Eely's teeth.";
    case SETTING_RICCO_RACE_CHECKPOINTS: return "Reveals the ordered checkpoints used by the Ricco race.";
    default: return "Changes this option for practice or presentation.";
    }
}

}  // namespace

class CategorySettingsTab : public MenuTab {
public:
    CategorySettingsTab(u8 titleOffset, u8 cat)
        : mSel(0), mCat(cat), mTitleOffset(titleOffset), mMode(0) {}

    const char *title() const override { return kCategoryTitles + mTitleOffset; }
    const char *summary() const override {
        if (isStarred()) return "Your favourite settings in one quick list.";
        if (isGameplay()) return "Core gameplay rules, skips and unlocks.";
        if (isTimer()) return "Timer visibility, splits and QFT freezes.";
        if (isRng()) return "Repeatable boss, object and course outcomes.";
        if (isDisplay()) return "Movement feedback and practice overlays.";
        if (isAppearance()) return "Mario, world and audio cosmetics.";
        if (mCat == SETTING_CAT_SAVESTATE)
            return "Savestate behaviour and feedback.";
        return "Practice helpers, counters and menu rules.";
    }
    bool favoriteHint() const override {
        if (pageRoot()) return false;
        u8 ids[SETTING_COUNT];
        const int settings = buildList(ids);
        return mSel < settings &&
               Settings::favoriteable((SettingId)ids[mSel]);
    }
    bool suppressesBinds() const override {
        if (grabsInput()) return true;
        const u16 held = JUTGamePad::mPadStatus[0].mButton;
        return (held & (JUTGamePad::A | JUTGamePad::X)) != 0;
    }
    bool back() override {
        if (!hasPages() || !mMode) return false;
        mSel = mMode - 1;
        mMode = 0;
        return true;
    }
    bool grabsInput() const override {
        return resetConfirm() ||
               (hasVisualEditor() && gCreationExtras.editing());
    }
    bool fullScreen() const override { return grabsInput(); }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (resetConfirm()) {
            const u32 rapid = pad->mButtons.mRapidInput;
            if (rapid & TMarioGamePad::B) {
                mMode = 0;
            } else if (rapid & TMarioGamePad::A) {
                if (mMode & 1) mMode++;
                else {
                    const bool records = mMode == 4;
                    mMode = 0;
                    if (records) {
                        RecordsPersistence::resetAll();
                        menu->toast("Records reset");
                    } else {
                        menu->factoryReset();
                    }
                }
            }
            return;
        }
        if (hasVisualEditor() && gCreationExtras.editing()) {
            gCreationExtras.updateEditor(pad);
            return;
        }
        if (pageRoot()) {
            updatePageRoot(menu, pad);
            return;
        }
        u8  ids[SETTING_COUNT];
        const int settings = buildList(ids);
        const int n = settings + extraRows();
        if (n == 0) {
            mSel = 0;
            return;
        }
        u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, n);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, n);
        }
        if (mSel >= n) {
            mSel = n - 1;
        }
        if (rapid & TMarioGamePad::CSTICK_LEFT) {
            jumpSection(ids, settings, n, -1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            jumpSection(ids, settings, n, +1);
        }
        if (mSel >= settings) {
            if (rapid & TMarioGamePad::A) {
                if (hasFeedbackEditor())
                    gCreationExtras.beginSavestateFeedbackEditor();
                else if (hasMovementEditors()) {
                    const int editor = mSel - settings;
                    if (editor == 0) gCreationExtras.beginWallkickEditor();
                    else if (editor == 1) gCreationExtras.beginRolloutEditor();
                    else gCreationExtras.beginDustEditor();
                }
                else if (hasFactoryReset())
                    mMode = mSel == settings ? 1 : 3;
            }
            return;
        }
        SettingId id = (SettingId)ids[mSel];
        if ((rapid & TMarioGamePad::X) && Settings::favoriteable(id)) {
            gSettings.toggleFavorite(id);
            menu->toast(gSettings.favorite(id)
                            ? "Added to Shined"
                            : "Removed from Shined");
            if (isStarred() && mSel >= settings - 1 && mSel > 0) mSel--;
        } else if (rapid & TMarioGamePad::A) {
            gSettings.cycle(id, +1);
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        if (resetConfirm()) {
            const bool records = mMode >= 3;
            const bool first = mMode & 1;
            const char *title = records
                ? (first ? "Reset all Records progress?"
                         : "Reset Records now?")
                : (first ? "Reset settings, binds and layouts?"
                         : "Reset settings now?");
            const char *detail = records
                ? (first ? "PBs and settings stay."
                         : "PB achievements can return.")
                : (first ? "This requires one more confirmation."
                         : "PBs and Records stay.");
            const char *hint = first
                ? SUSAMUNE_GLYPH_A " Continue    " SUSAMUNE_GLYPH_B " Cancel"
                : SUSAMUNE_GLYPH_A " Reset    " SUSAMUNE_GLYPH_B " Cancel";
            menu->fillBox(88, 176, 464, 128, Color(8, 11, 20, 245));
            menu->fillBox(88, 176, 464, 3, cAccent());
            menu->drawText(title, 320 - Menu::textWidth(title, 15) / 2,
                           200, 15, 15, cRowSel());
            menu->drawText(detail, 320 - Menu::textWidth(detail, 12) / 2,
                           232, 12, 12, cRow());
            menu->drawText(hint, 320 - Menu::textWidth(hint, 12) / 2,
                           270, 12, 12, cFooter());
            return;
        }
        if (hasVisualEditor() && gCreationExtras.editing()) {
            gCreationExtras.drawEditor(menu);
            return;
        }
        if (pageRoot()) {
            drawPageRoot(menu, x, y, w, h);
            return;
        }
        u8  ids[SETTING_COUNT];
        const int settings = buildList(ids);
        const int n = settings + extraRows();
        if (n == 0) {
            menu->drawText(isStarred() ? "Nothing Shined yet" : "(none)",
                           x + 4, y, ROW_SZ, ROW_SZ, cRowDim());
            if (isStarred())
                menu->drawText("Press X on a setting to add it here.", x + 4,
                               y + ROW_H, FOOT_SZ, FOOT_SZ, cFooter());
            return;
        }
        const char *help = selectionHelp(ids, settings);
        const int listH = help ? h - HELP_H : h;
        const u32 metrics = displayMetrics(ids, settings, n);
        const int rows = metrics >> 16;
        const int maxRows = listH / ROW_H;
        const int start = listScrollStart(
            (u16)metrics, rows, maxRows);
        int end = start + maxRows;
        if (end > rows) end = rows;
        int ry = y;
        int row = 0;
        for (int i = 0; i < n && row < end; i++) {
            const char *section = sectionName(ids, settings, i);
            if (section) {
                if (row >= start) {
                    drawSectionHeader(menu, x, ry, w, section);
                    ry += ROW_H;
                }
                row++;
                if (row >= end) break;
            }
            if (row < start) {
                row++;
                continue;
            }
            const bool selected = i == mSel;
            const char *name;
            const char *val;
            if (i < settings) {
                const SettingId id = (SettingId)ids[i];
                name = Settings::name(id);
                val = gSettings.valueLabel(id);
            } else {
                name = hasFeedbackEditor() ? "Feedback display"
                     : hasMovementEditors()
                           ? movementEditorName(i - settings)
                     : i == settings ? "Factory reset"
                                     : "Reset Records";
                val = hasVisualEditor() ? "Edit" : "Reset";
            }
            const bool starred = i < settings &&
                gSettings.favorite((SettingId)ids[i]);
            const bool pbSetting = i < settings &&
                Settings::affectsIlPb((SettingId)ids[i]);
            const bool pbBlocked = pbSetting &&
                gSettings.blocksIlPb((SettingId)ids[i]);
            drawValueRowColored(
                menu, x, ry, w, name, val, selected, starred, false,
                pbBlocked ? col(255, 72, 72, 255) : cValue());
            if (pbSetting) {
                const char *tag = pbBlocked ? "PB OFF" : "PB";
                const int valueX = x + w - Menu::textWidth(val, ROW_SZ) - 8;
                const int tagSize = 10;
                menu->drawText(tag,
                    valueX - Menu::textWidth(tag, tagSize) - 10,
                    ry + 2, tagSize, tagSize,
                    pbBlocked ? col(255, 72, 72, 255) : cRowDim());
            }
            ry += ROW_H;
            row++;
        }
    
        drawScrollHints(menu, x, y, w, listH, start, end, rows);
        drawHelpLine(menu, x, y, w, h, help);
    }

private:
    bool isStarred() const { return mCat == kStarredCategory; }
    bool isGameplay() const { return mCat == SETTING_CAT_QOL; }
    bool isTimer() const { return mCat == SETTING_CAT_TIMER; }
    bool isRng() const { return mCat == kRngCategory; }
    bool isDisplay() const { return mCat == SETTING_CAT_UI; }
    bool isAppearance() const { return mCat == SETTING_CAT_COSMETIC; }
    bool hasPages() const {
        return isGameplay() || isTimer() || isRng() || isDisplay() ||
               isAppearance();
    }
    bool pageRoot() const { return hasPages() && !mMode; }
    bool resetConfirm() const { return hasFactoryReset() && mMode; }
    bool hasFeedbackEditor() const {
        return mCat == SETTING_CAT_SAVESTATE;
    }
    bool hasMovementEditors() const {
        return isDisplay() && (!hasPages() || mMode == 1);
    }
    bool hasVisualEditor() const {
        return hasFeedbackEditor() || hasMovementEditors();
    }
    bool hasFactoryReset() const { return mCat == SETTING_CAT_MISC; }
    int extraRows() const {
        return hasFactoryReset() ? 2 : hasMovementEditors() ? 3
             : hasFeedbackEditor() ? 1 : 0;
    }

    const SettingPage *pages() const {
        if (isGameplay()) return kGameplayPages;
        if (isTimer()) return kTimerPages;
        if (isRng()) return kRngPages;
        return isDisplay() ? kDisplayPages : kAppearancePages;
    }
    int pageCount() const {
        if (isGameplay())
            return sizeof(kGameplayPages) / sizeof(kGameplayPages[0]);
        if (isTimer()) return sizeof(kTimerPages) / sizeof(kTimerPages[0]);
        if (isRng()) return sizeof(kRngPages) / sizeof(kRngPages[0]);
        if (isDisplay())
            return sizeof(kDisplayPages) / sizeof(kDisplayPages[0]);
        return sizeof(kAppearancePages) / sizeof(kAppearancePages[0]);
    }
    const SettingPage &currentPage() const { return pages()[mMode - 1]; }

    const char *movementEditorName(int editor) const {
        return editor == 0 ? "Wallkick display style"
             : editor == 1 ? "Rollout display style"
                           : "Dust display style";
    }

    const char *pageRootSection(int page) const {
        if (isTimer()) return page == 0 ? "TIMING AND SPLITS" : "QFT";
        if (isRng()) {
            if (page == 0) return "PRACTICE CODES";
            if (page == 1) return "ASSISTED RNG";
            return page == 2 ? "COURSE RNG" : "TESTING";
        }
        if (isGameplay()) {
            if (page == 0) return "CORE";
            return page == 1 ? "ROUTE SETUP" : "WORLD";
        }
        if (isDisplay()) {
            if (page == 0) return "MOVEMENT";
            return page == 1 ? "PRACTICE VISUALS" : "GENERAL HUD";
        }
        return page == 0 ? "MARIO" : "WORLD AND AUDIO";
    }

    void updatePageRoot(Menu *menu, TMarioGamePad *pad) {
        const int count = pageCount();
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP)
            mSel = wrap(mSel - 1, count);
        else if (rapid & TMarioGamePad::CSTICK_DOWN)
            mSel = wrap(mSel + 1, count);
        if (rapid & TMarioGamePad::A) {
            mMode = mSel + 1;
            mSel = 0;
        }
    }

    void drawPageRoot(Menu *menu, int x, int y, int w, int h) const {
        int ry = y;
        for (int page = 0; page < pageCount(); page++) {
            const char *section = pageRootSection(page);
            if (section) {
                drawSectionHeader(menu, x, ry, w, section);
                ry += ROW_H;
            }
            drawValueRow(menu, x, ry, w, pages()[page].name, nullptr,
                         page == mSel, false, true);
            ry += ROW_H;
        }
        drawHelpLine(menu, x, y, w, h, pages()[mSel].help);
    }

    const char *selectionHelp(const u8 *ids, int settings) const {
        if (mSel < settings) return settingHelp((SettingId)ids[mSel]);
        if (hasFeedbackEditor()) return "Changes the savestate status popup layout.";
        if (hasMovementEditors()) return "Changes this display's position, size and colours.";
        return mSel == settings
                   ? "Restores settings, binds and layouts to defaults."
                   : "Clears Records progress without deleting IL PBs.";
    }

    int buildList(u8 *out) const {
        if (hasPages()) {
            if (!mMode) return 0;
            const SettingPage &page = currentPage();
            for (u8 i = 0; i < page.count; i++) out[i] = page.ids[i];
            return page.count;
        }
        int n = 0;
        const int count = SETTING_COUNT;
        for (int i = 0; i < count; i++) {
            const SettingId id = (SettingId)i;
            const bool include = isStarred()
                    ? Settings::favoriteable(id) &&
                          Settings::category(id) != SETTING_CAT_HIDDEN &&
                          gSettings.favorite(id)
                    : Settings::category(id) == (SettingCategory)mCat;
            if (include) {
                out[n++] = (u8)i;
            }
        }
        return n;
    }

    const char *sectionName(const u8 *ids, int settings, int logical) const {
        if (logical < settings) {
            if (isStarred()) return nullptr;
            if (hasPages()) {
                if (!isTimer())
                    return logical == 0 ? currentPage().name : nullptr;
                if (mMode == 1) {
                    if (logical == 0) return "TIMER DISPLAY";
                    return logical == 2 ? "HISTORY AND SPLITS" : nullptr;
                }
                if (logical == 0) return "DURATION";
                if (logical == 1) return "COINS AND OBJECTS";
                if (logical == 8) return "MOVEMENT";
                return logical == 21 ? "EVENTS AND BOSSES" : nullptr;
            }
            const SettingId id = (SettingId)ids[logical];
            for (u32 i = 0; i < sizeof(kSettingSectionStarts); i++) {
                if (kSettingSectionStarts[i] == id)
                    return PackedText::at(kSettingSectionNames, i);
            }
            return nullptr;
        }
        if (hasFeedbackEditor()) return "FEEDBACK STYLE";
        if (hasMovementEditors())
            return logical == settings ? "DISPLAY STYLE" : nullptr;
        if (hasFactoryReset()) return logical == settings ? "RESET" : nullptr;
        return nullptr;
    }

    __attribute__((always_inline))
    u32 displayMetrics(const u8 *ids, int settings, int logicalCount) const {
        int rows = logicalCount;
        int selectedRow = mSel;
        for (int i = 0; i < logicalCount; i++) {
            if (!sectionName(ids, settings, i)) continue;
            rows++;
            if (i <= mSel) selectedRow++;
        }
        return ((u32)rows << 16) | (u16)selectedRow;
    }

    void jumpSection(const u8 *ids, int settings, int n, int direction) {
        int row = mSel;
        for (int left = n; left; left--) {
            row = wrap(row + direction, n);
            if (sectionName(ids, settings, row)) {
                mSel = row;
                return;
            }
        }
    }

    u8 mSel;
    u8 mCat;
    u8 mTitleOffset;
    u8 mMode;
};

static_assert(sizeof(CategorySettingsTab) == 8, "category tab must stay one slot");

// ---------------------------------------------------------------------
// Creation -- shared visual editor for compact QFT, Input and Metadata.
// ---------------------------------------------------------------------
class CreationTab final : public MenuTab {
public:
    CreationTab() : mSel(ROW_QFT_EDITOR) {}

    const char *title() const override { return "Layout editor"; }
    const char *summary() const override {
        return "Move, resize and recolour Moonshine HUD elements.";
    }
    bool available() const override { return !rngControlInvalidatesIl(); }
    bool grabsInput() const override {
        return gQftDisplay.editing() || gInputDisplay.editing() ||
               gMetadataDisplay.editing() || gCreationExtras.editing();
    }
    bool suppressesBinds() const override {
        if (grabsInput()) return true;
        return (JUTGamePad::mPadStatus[0].mButton & JUTGamePad::A) != 0;
    }
    bool fullScreen() const override { return grabsInput(); }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (gQftDisplay.editing()) {
            gQftDisplay.updateEditor(pad);
            return;
        }
        if (gInputDisplay.editing()) {
            gInputDisplay.updateEditor(pad);
            return;
        }
        if (gMetadataDisplay.editing()) {
            gMetadataDisplay.updateEditor(pad);
            return;
        }
        if (gCreationExtras.editing()) {
            gCreationExtras.updateEditor(pad);
            return;
        }
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            moveSelection(-1);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            moveSelection(+1);
        }
        if (rapid & TMarioGamePad::CSTICK_LEFT) {
            jumpSection(-1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            jumpSection(+1);
        } else if (rapid & TMarioGamePad::A) {
            activate(+1);
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        if (gQftDisplay.editing()) {
            gQftDisplay.drawEditor(menu);
            return;
        }
        if (gInputDisplay.editing()) {
            gInputDisplay.drawEditor(menu);
            return;
        }
        if (gMetadataDisplay.editing()) {
            gMetadataDisplay.drawEditor(menu);
            return;
        }
        if (gCreationExtras.editing()) {
            gCreationExtras.drawEditor(menu);
            return;
        }

        const int hintY = y + h - FOOT_SZ;
        const int listH = h - ROW_H - HELP_H;
        const int count = ROW_COUNT;
        const int maxRows = listH / ROW_H;
        const int start = listScrollStart(mSel, count, maxRows);
        int end = start + maxRows;
        if (end > count) end = count;

        int ry = y;
        for (int row = start; row < end; row++) {
            if (isSeparator(row)) {
                const char *label = row == ROW_QFT_HEADER ? "QFT"
                    : row == ROW_INPUT_HEADER ? "INPUT DISPLAY"
                    : row == ROW_METADATA_HEADER ? "METADATA"
                    : gCreationExtras.menuRowName(extraRow(row));
                drawSectionHeader(menu, x, ry, w, label);
            } else {
                const bool selected = row == mSel;
                drawValueRow(menu, x, ry, w, rowName(row), rowValue(row),
                             selected, false, false);
            }
            ry += ROW_H;
        }
        drawScrollHints(menu, x, y, w, listH, start, end, count);
        drawHelpLine(menu, x, y, w, h - ROW_H, rowHelp(mSel));
        menu->drawText(SUSAMUNE_GLYPH_A " Open" SUSAMUNE_GLYPH_SLASH
                       "Change   " SUSAMUNE_GLYPH_C " L"
                       SUSAMUNE_GLYPH_SLASH "R Section   Saved on close",
                       x + 4, hintY, FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    enum Row {
        ROW_QFT_HEADER,
        ROW_QFT_EDITOR,
        ROW_QFT_LEADING_ZERO,
        ROW_SUNSHINE_TIMER_CHARACTERS,
        ROW_SUNSHINE_TIMER_STREAK,
        ROW_SUNSHINE_TIMER_LABEL,
        ROW_SUNSHINE_TIMER_LABEL_VISIBLE,
        ROW_INPUT_HEADER,
        ROW_INPUT_FIRST,
        ROW_INPUT_END = ROW_INPUT_FIRST + InputDisplay::MENU_ROW_COUNT,
        ROW_METADATA_HEADER = ROW_INPUT_END,
        ROW_METADATA_FIRST,
        ROW_METADATA_END = ROW_METADATA_FIRST + MetadataDisplay::menuRowCount(),
        ROW_EXTRAS_FIRST = ROW_METADATA_END,
        ROW_COUNT = ROW_EXTRAS_FIRST + CreationExtras::MENU_ROW_COUNT,
    };

    static bool isSeparator(int row) {
        return row == ROW_QFT_HEADER || row == ROW_INPUT_HEADER ||
               row == ROW_METADATA_HEADER ||
               (row >= ROW_EXTRAS_FIRST &&
                gCreationExtras.menuRowSeparator(extraRow(row)));
    }

    void moveSelection(int dir) {
        do {
            mSel = (u8)wrap(mSel + dir, ROW_COUNT);
        } while (isSeparator(mSel));
    }

    void jumpSection(int dir) {
        if (dir > 0) {
            for (int row = mSel + 1; row < ROW_COUNT; row++) {
                if (isSeparator(row)) {
                    mSel = (u8)row;
                    moveSelection(+1);
                    return;
                }
            }
            mSel = 0;
            moveSelection(+1);
            return;
        }

        int current = -1;
        for (int row = 0; row < mSel; row++) {
            if (isSeparator(row)) current = row;
        }
        if (current >= 0 && mSel > current + 1) {
            mSel = (u8)current;
            moveSelection(+1);
            return;
        }
        for (int row = current - 1; row >= 0; row--) {
            if (isSeparator(row)) {
                mSel = (u8)row;
                moveSelection(+1);
                return;
            }
        }
        for (int row = ROW_COUNT - 1; row >= 0; row--) {
            if (isSeparator(row)) {
                mSel = (u8)row;
                moveSelection(+1);
                return;
            }
        }
    }

    void activate(int dir) {
        if (mSel == ROW_QFT_EDITOR) {
            gQftDisplay.beginEditor();
        } else if (mSel == ROW_QFT_LEADING_ZERO) {
            gQftDisplay.toggleLeadingZero();
        } else if (mSel == ROW_SUNSHINE_TIMER_CHARACTERS) {
            gCreationExtras.beginTimerCharacterEditor();
        } else if (mSel == ROW_SUNSHINE_TIMER_STREAK) {
            gCreationExtras.beginColorEditor(SUSAMUNE_CREATION_TIMER_BG, 1,
                                             "Sunshine timer streak");
        } else if (mSel == ROW_SUNSHINE_TIMER_LABEL) {
            gCreationExtras.beginColorEditor(
                SUSAMUNE_CREATION_TIMER_LABEL, 1,
                "TIME" SUSAMUNE_GLYPH_SLASH "TEMPO label colour");
        } else if (mSel == ROW_SUNSHINE_TIMER_LABEL_VISIBLE) {
            gCreationExtras.toggleTimerLabel();
        } else if (mSel >= ROW_INPUT_FIRST && mSel < ROW_INPUT_END) {
            gInputDisplay.adjustMenuRow(inputRow(mSel), dir);
        } else if (mSel >= ROW_METADATA_FIRST && mSel < ROW_METADATA_END) {
            gMetadataDisplay.adjustMenuRow(metadataRow(mSel), dir);
        } else if (mSel >= ROW_EXTRAS_FIRST) {
            gCreationExtras.adjustMenuRow(extraRow(mSel), dir);
        }
    }

    const char *rowName(int row) const {
        if (row == ROW_QFT_EDITOR) return "QFT timer";
        if (row == ROW_QFT_LEADING_ZERO) return "QFT leading zero";
        if (row == ROW_SUNSHINE_TIMER_CHARACTERS)
            return "Sunshine timer characters";
        if (row == ROW_SUNSHINE_TIMER_STREAK)
            return "Sunshine timer streak";
        if (row == ROW_SUNSHINE_TIMER_LABEL)
            return "TIME" SUSAMUNE_GLYPH_SLASH "TEMPO label colour";
        if (row == ROW_SUNSHINE_TIMER_LABEL_VISIBLE)
            return "Show TIME" SUSAMUNE_GLYPH_SLASH "TEMPO label";
        if (row >= ROW_INPUT_FIRST && row < ROW_INPUT_END)
            return InputDisplay::menuRowName(inputRow(row));
        if (row >= ROW_METADATA_FIRST && row < ROW_METADATA_END)
            return gMetadataDisplay.menuRowName(metadataRow(row));
        return gCreationExtras.menuRowName(extraRow(row));
    }

    const char *rowValue(int row) const {
        if (row == ROW_QFT_EDITOR) return "Edit";
        if (row == ROW_QFT_LEADING_ZERO)
            return gQftDisplay.leadingZero() ? "On" : "Off";
        if (row >= ROW_SUNSHINE_TIMER_CHARACTERS &&
            row <= ROW_SUNSHINE_TIMER_LABEL)
            return "Edit";
        if (row == ROW_SUNSHINE_TIMER_LABEL_VISIBLE)
            return gCreationExtras.timerLabelVisible() ? "On" : "Off";
        if (row >= ROW_INPUT_FIRST && row < ROW_INPUT_END)
            return gInputDisplay.menuRowValue(inputRow(row));
        if (row >= ROW_METADATA_FIRST && row < ROW_METADATA_END)
            return gMetadataDisplay.menuRowValue(metadataRow(row));
        return gCreationExtras.menuRowValue(extraRow(row));
    }

    const char *rowHelp(int row) const {
        if (row == ROW_QFT_EDITOR)
            return "Moves, resizes and recolours the compact QFT display.";
        if (row == ROW_QFT_LEADING_ZERO)
            return "Shows a leading zero before single-digit QFT values.";
        if (row == ROW_SUNSHINE_TIMER_CHARACTERS)
            return "Sets colours for the timer digits and separators.";
        if (row == ROW_SUNSHINE_TIMER_STREAK)
            return "Changes the colour streak behind the Sunshine timer.";
        if (row == ROW_SUNSHINE_TIMER_LABEL)
            return "Changes the TIME or TEMPO label colour.";
        if (row == ROW_SUNSHINE_TIMER_LABEL_VISIBLE)
            return "Shows or hides the TIME or TEMPO label.";
        if (row >= ROW_INPUT_FIRST && row < ROW_INPUT_END) {
            switch (inputRow(row)) {
            case 0: return "Shows the controller overlay when Moonshine starts.";
            case 1: return "Adds stick-only or full numeric input values.";
            case 2: return "Chooses raw controller data or Mario-processed input.";
            case 3: return "Places numeric values above, below or inside the pad.";
            case 4: return "Moves, resizes and recolours the controller overlay.";
            default: return "Restores the controller overlay's default layout.";
            }
        }
        if (row >= ROW_METADATA_FIRST && row < ROW_METADATA_END) {
            const int local = metadataRow(row);
            if (local == 0) return "Shows the metadata overlay when Moonshine starts.";
            if (local == 1) return "Chooses short, long or custom field labels.";
            if (local >= 2 && local < 2 + MetadataDisplay::FIELD_COUNT)
                return "Shows or hides this value in the metadata overlay.";
            if (local == 2 + MetadataDisplay::FIELD_COUNT)
                return "Stacks metadata vertically or lays it out in one row.";
            if (local == 3 + MetadataDisplay::FIELD_COUNT)
                return "Moves, resizes and recolours the metadata overlay.";
            return "Restores the metadata overlay's default layout.";
        }
        const int local = extraRow(row);
        if (local >= 1 && local <= 7)
            return "Changes this part of the retail HUD's colour theme.";
        if (local == 9 || local == 12 || local == 15)
            return "Edits the custom word shown on the game HUD.";
        if (local == 10 || local == 13 || local == 16)
            return "Moves, resizes and recolours this custom HUD word.";
        if (local == 11 || local == 14 || local == 17)
            return "Shows or hides this custom HUD word.";
        if (local == 19) return "Changes the mod menu's panel colour.";
        if (local == 20) return "Moves and styles the achievement popup.";
        if (local == 21) return "Moves and styles Moonshine status messages.";
        if (local == 22) return "Moves and styles the IL PB popup.";
        if (local == 23) return "Moves and styles the Stage Loader counter.";
        return "Changes this HUD element's layout or appearance.";
    }

    static int inputRow(int row) {
        const int local = row - ROW_INPUT_FIRST;
        if (local == 0) return 4;
        if (local <= 4) return local - 1;
        return 5;
    }

    static int metadataRow(int row) {
        const int local = row - ROW_METADATA_FIRST;
        const int style = 3 + MetadataDisplay::FIELD_COUNT;
        if (local == 0) return style;
        if (local <= style) return local - 1;
        return style + 1;
    }

    static int extraRow(int row) { return row - ROW_EXTRAS_FIRST; }

    u8 mSel;
};

// ---------------------------------------------------------------------
// Binds tab
//
// One row per BindId. A on a row arms gBinds' recorder, which watches the
// raw pad and commits a combo when a held button comes back up (or when
// four are down). All of the recorder's logic is in binds.cpp; this tab
// only starts it, reports it, and takes the pad away from the rest of the
// menu while it runs -- otherwise pressing L to record would switch tabs
// and Y+Start would close the menu.
// ---------------------------------------------------------------------
namespace {

const u8 kBindSectionStarts[] = {
    BIND_REGRAB_OBJECT,
    BIND_MENU_TOGGLE,
    BIND_SAVESTATE_SAVE,
    BIND_WARP_WHEEL,
    BIND_TOGGLE_INPUT_DISPLAY,
    BIND_ATTEMPT_SHOW,
    BIND_POSITION_SAVE,
};
const char kBindSectionNames[] =
    "ACTIONS\0MENU\0SAVESTATES\0WARPS AND RESTARTS\0DISPLAY\0ATTEMPT COUNTER\0POSITION";
enum {
    kBindSectionCount =
        sizeof(kBindSectionStarts) / sizeof(kBindSectionStarts[0]),
};
static_assert(kBindSectionCount == 7,
              "bind section metadata must be validated together");
static_assert(BIND_REGRAB_OBJECT == 0 &&
                  BIND_REGRAB_OBJECT < BIND_MENU_TOGGLE &&
                  BIND_MENU_TOGGLE < BIND_SAVESTATE_SAVE &&
                  BIND_SAVESTATE_SAVE < BIND_WARP_WHEEL &&
                  BIND_WARP_WHEEL < BIND_TOGGLE_INPUT_DISPLAY &&
                  BIND_TOGGLE_INPUT_DISPLAY < BIND_ATTEMPT_SHOW &&
                  BIND_ATTEMPT_SHOW < BIND_POSITION_SAVE &&
                  BIND_POSITION_SAVE < BIND_COUNT,
              "bind section starts must be ordered and in bounds");

}  // namespace

class BindsTab : public MenuTab {
public:
    BindsTab() : mSel(0) {}

    const char *title() const override { return "Button binds"; }
    const char *summary() const override {
        return "Choose the controller combo for each Moonshine action.";
    }

    bool grabsInput() const override { return gBinds.recording(); }
    bool suppressesBinds() const override {
        if (grabsInput()) return true;
        const u16 held = JUTGamePad::mPadStatus[0].mButton;
        return (held & (JUTGamePad::A | JUTGamePad::X)) != 0;
    }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (gBinds.recording()) {
            const u32 rapid = pad->mButtons.mRapidInput;
            // Only the C-stick is safe to react to here: every real button is
            // a candidate for the combo being recorded.
            if (rapid & (TMarioGamePad::CSTICK_LEFT | TMarioGamePad::CSTICK_RIGHT |
                         TMarioGamePad::CSTICK_UP | TMarioGamePad::CSTICK_DOWN)) {
                gBinds.cancelRecord();
            }
            return;
        }

        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, BIND_COUNT);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, BIND_COUNT);
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            jumpSection(-1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            jumpSection(+1);
        }
        if (rapid & TMarioGamePad::A) {
            gBinds.beginRecord((BindId)mSel);
        } else if (rapid & TMarioGamePad::X) {
            gBinds.set((BindId)mSel, 0);  // clear
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        // Last line of the content area is this tab's own hint: its controls
        // differ enough from the rest of the menu to be worth spelling out.
        const int hintY = y + h - FOOT_SZ;
        h -= ROW_H;

        const u32 metrics = displayMetrics();
        const int rows = metrics >> 16;
        int maxRows = h / ROW_H;
        int start   = listScrollStart((u16)metrics, rows, maxRows);
        int end     = start + maxRows;
        if (end > rows) {
            end = rows;
        }

        char text[kBindTextMax];
        int  ry = y;
        int row = 0;
        for (int i = 0; i < BIND_COUNT && row < end; i++) {
            const char *section = sectionName(i);
            if (section) {
                if (row >= start) {
                    drawSectionHeader(menu, x, ry, w, section);
                    ry += ROW_H;
                }
                row++;
                if (row >= end) break;
            }
            if (row < start) {
                row++;
                continue;
            }
            BindId id       = (BindId)i;
            bool   selected = (i == mSel);
            if (selected) {
                drawRowHighlight(menu, x, ry, w, ROW_H);
            }
            menu->drawText(Binds::name(id), x + 4, ry, ROW_SZ, ROW_SZ,
                           selected ? cRowSel() : cRow());

            const char *val;
            Color       valCol = cValue();
            if (gBinds.recording() && gBinds.recordTarget() == id) {
                if (gBinds.recordPreview() != 0) {
                    Binds::format(gBinds.recordPreview(), text);
                    val = text;
                } else {
                    val = "press buttons...";
                }
                valCol = cAccent();
            } else {
                Binds::format(gBinds.get(id), text);
                val = text;
            }
            int vx = x + w - Menu::textWidth(val, ROW_SZ) - 8;
            menu->drawText(val, vx, ry, ROW_SZ, ROW_SZ, valCol);
            ry += ROW_H;
            row++;
        }

        drawScrollHints(menu, x, y, w, h, start, end, rows);

        menu->drawText(gBinds.recording()
                           ? "Release a button to set, or " SUSAMUNE_GLYPH_C " to cancel"
                           : SUSAMUNE_GLYPH_A " Set bind    "
                             SUSAMUNE_GLYPH_X " Clear",
                       x + 4, hintY, FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    const char *sectionName(int bind) const {
        for (int i = 0; i < kBindSectionCount; i++) {
            if (kBindSectionStarts[i] == bind)
                return PackedText::at(kBindSectionNames, i);
        }
        return nullptr;
    }

    __attribute__((always_inline)) u32 displayMetrics() const {
        int selectedRow = mSel;
        for (int i = 0; i < kBindSectionCount; i++) {
            if (kBindSectionStarts[i] <= mSel) selectedRow++;
        }
        return ((u32)(BIND_COUNT + kBindSectionCount) << 16) |
               (u16)selectedRow;
    }

    void jumpSection(int direction) {
        if (direction > 0) {
            for (int i = 0; i < kBindSectionCount; i++) {
                if (kBindSectionStarts[i] > mSel) {
                    mSel = kBindSectionStarts[i];
                    return;
                }
            }
            mSel = kBindSectionStarts[0];
        } else if (direction < 0) {
            for (int i = kBindSectionCount - 1; i >= 0; i--) {
                if (kBindSectionStarts[i] < mSel) {
                    mSel = kBindSectionStarts[i];
                    return;
                }
            }
            mSel = kBindSectionStarts[kBindSectionCount - 1];
        }
    }

    int mSel;
};

// ---------------------------------------------------------------------
// Stage Loader -- the IL catalogue with an optional repeated-finish session.
// ---------------------------------------------------------------------
class StageLoaderTab final : public MenuTab {
public:
    StageLoaderTab()
        : mSel(0), mGoal(5), mTargetQf(-1), mStreakEntry(0),
          mStreaking(false), mBuiltinPlaylist(0), mCustomSlot(0),
          mEditor(EDIT_NONE), mTextCursor(0), mTextPage(1), mTextLength(0),
          mTextUpper(false) {
        mText[0] = '\0';
        StageTargets::init();
        mTargetQf = StageTargets::get(mStreakEntry);
    }

    const char *title() const override { return "Stage Loader"; }
    bool grabsInput() const override { return mEditor != EDIT_NONE; }
    bool suppressesBinds() const override {
        if (mEditor != EDIT_NONE) return true;
        const u16 held = JUTGamePad::mPadStatus[0].mButton;
        return (held & (JUTGamePad::A | JUTGamePad::X)) != 0;
    }
    bool fullScreen() const override { return mEditor != EDIT_NONE; }

    void update(Menu *menu, TMarioGamePad *pad) override {
        if (mEditor != EDIT_NONE) {
            updateTextEditor(menu);
            return;
        }

        const int count = selectionCount();
        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = wrap(mSel - 1, count);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = wrap(mSel + 1, count);
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            moveHorizontal(-1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            moveHorizontal(+1);
        }

        if ((rapid & TMarioGamePad::X) &&
            selectedOption() == OPTION_TARGET &&
            mStreaking) {
            mTargetQf = -1;
            StageTargets::set(mStreakEntry, mTargetQf);
            menu->toast("Any finish counts");
            return;
        }
        if (!(rapid & TMarioGamePad::A)) return;

        if (isOption()) {
            activateOption(menu);
            return;
        }

        if (isQueueRow()) {
            const int position = queuePosition();
            if (StageLoader::removeQueue(position)) {
                if (mSel >= selectionCount()) mSel = selectionCount() - 1;
                menu->toast("Removed from playlist");
            }
            return;
        }

        const int entry = catalogueEntry();
        if (mStreaking) {
            mStreakEntry = entry;
            mTargetQf = StageTargets::get(mStreakEntry);
            menu->toast("Streak level selected");
        } else {
            if (StageLoader::appendQueue(entry)) {
                // The inserted queue row shifts the catalogue selection down.
                mSel++;
                menu->toast("Added to playlist");
            } else {
                menu->toast("Playlist is full");
            }
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        if (mEditor != EDIT_NONE) {
            drawCreationKeyboard(
                menu,
                mEditor == EDIT_FINISHES ? "Set streak finishes"
                                         : "Set streak target time",
                mText[0] ? mText
                         : mEditor == EDIT_FINISHES
                               ? "(enter 1-999)"
                               : "(blank = any finish)",
                mTextPage, mTextUpper, mTextCursor);
            return;
        }

        const int entries = ILing::count();
        const int listH = h - ROW_H;
        const int maxRows = listH / ROW_H;
        const int rows = menuRowCount();
        const int start = listScrollStart(menuRowForSelection(mSel), rows,
                                          maxRows);
        int end = start + maxRows;
        if (end > rows) end = rows;

        char goal[8];
        char target[24];
        char queued[16];
        char custom[32];
        snprintf(goal, sizeof(goal), "%u", (unsigned)mGoal);
        snprintf(queued, sizeof(queued), "%d / %d",
                 StageLoader::queueCount(), StageLoader::QUEUE_CAPACITY);
        if (mTargetQf < 0) {
            strcpy(target, "Any finish");
        } else {
            ILing::formatTime(mTargetQf, target, sizeof(target));
        }

        int ry = y;
        int row = 0;
        for (int optionRow = 0; optionRow < optionCount();
             optionRow++, row++) {
            if (row < start || row >= end) continue;
            const Option option = optionAt(optionRow);
            const char *name;
            const char *value;
            if (option == OPTION_MODE) {
                name = "Mode";
                value = mStreaking ? "Streaking" : "Stageloader";
            } else if (option == OPTION_RUN) {
                name = mStreaking ? "Start streak" : "Run playlist";
                value = mStreaking ? ILing::shortLabel(mStreakEntry) : "Start";
            } else if (option == OPTION_FINISHES) {
                name = mStreaking ? "Finishes" : "Playlist entries";
                value = mStreaking ? goal : queued;
            } else if (option == OPTION_TARGET) {
                name = mStreaking ? "Target time" : "Clear playlist";
                value = mStreaking ? target : "Clear";
            } else if (option == OPTION_DISPLAY) {
                name = "Session display";
                value = gSettings.valueLabel(SETTING_STAGE_SESSION_DISPLAY);
            } else if (option == OPTION_AUTO_RESET) {
                name = "Streak auto-reset";
                value = gSettings.valueLabel(SETTING_STREAK_AUTO_RESET);
            } else if (option == OPTION_BUILTIN) {
                name = "Built-in preset";
                value = StageLoader::builtinPlaylistName(mBuiltinPlaylist);
            } else {
                const int saved =
                    StageLoader::customPlaylistEntryCount(mCustomSlot);
                if (!StageLoader::customPlaylistsAvailable()) {
                    strcpy(custom, "Unavailable");
                } else if (saved > 0) {
                    snprintf(custom, sizeof(custom), "Custom %u (%d)",
                             (unsigned)mCustomSlot + 1, saved);
                } else {
                    snprintf(custom, sizeof(custom), "Custom %u (empty)",
                             (unsigned)mCustomSlot + 1);
                }
                name = option == OPTION_LOAD ? "Load playlist"
                                             : "Save playlist";
                value = custom;
            }
            drawValueRow(menu, x, ry, w, name, value, mSel == optionRow,
                         false, true);
            ry += ROW_H;
        }

        if (!mStreaking) {
            if (row >= start && row < end) {
                drawSectionHeader(menu, x, ry, w, "PLAYLIST");
                ry += ROW_H;
            }
            row++;

            const int count = StageLoader::queueCount();
            if (!count) {
                if (row >= start && row < end) {
                    menu->drawText("(empty - add levels below)", x + 4, ry + 3,
                                   13, 13, cRowDim());
                    ry += ROW_H;
                }
                row++;
            } else {
                for (int position = 0; position < count; position++, row++) {
                    if (row < start || row >= end) continue;
                    char label[80];
                    snprintf(label, sizeof(label), "%02d. %s", position + 1,
                             ILing::label(StageLoader::queueEntry(position)));
                    drawValueRow(menu, x, ry, w, label, nullptr,
                                 mSel == optionCount() + position,
                                 false, true);
                    ry += ROW_H;
                }
            }
        }

        if (row >= start && row < end) {
            drawSectionHeader(menu, x, ry, w,
                              mStreaking ? "SELECT LEVEL" : "ADD LEVELS");
            ry += ROW_H;
        }
        row++;

        for (int position = 0; position < entries && row < end;
             position++) {
            const int entry = ILing::menuEntryAt(position);
            if (!catalogueIncludesEntry(entry)) continue;
            if (ILing::beginsMenuGroup(position)) {
                if (row >= start) {
                    drawSectionHeader(menu, x, ry, w,
                                      ILing::menuGroupName(position));
                    ry += ROW_H;
                }
                row++;
                if (row >= end) break;
            }
            if (row < start) {
                row++;
                continue;
            }
            const char *value = mStreaking && entry == mStreakEntry
                                    ? "Selected"
                                    : !mStreaking ? "Add" : nullptr;
            drawValueRow(menu, x, ry, w, ILing::label(entry), value,
                         isCatalogueRow() && catalogueEntry() == entry,
                         false, true);
            ry += ROW_H;
            row++;
        }

        drawScrollHints(menu, x, y, w, listH, start, end, rows);
        const Option selected = selectedOption();
        const bool customOption = !mStreaking && isOption() &&
            (selected == OPTION_LOAD || selected == OPTION_SAVE);
        const bool builtinOption = !mStreaking && isOption() &&
            selected == OPTION_BUILTIN;
        const bool displayOption = isOption() &&
            (selected == OPTION_DISPLAY || selected == OPTION_AUTO_RESET);
        const char *hint = customOption
            ? SUSAMUNE_GLYPH_A " Select   " SUSAMUNE_GLYPH_C
              " L" SUSAMUNE_GLYPH_SLASH "R Slot"
            : builtinOption
            ? SUSAMUNE_GLYPH_A " Load   " SUSAMUNE_GLYPH_C
              " L" SUSAMUNE_GLYPH_SLASH "R Preset"
            : displayOption
            ? SUSAMUNE_GLYPH_A " Change   " SUSAMUNE_GLYPH_C
              " L" SUSAMUNE_GLYPH_SLASH "R Option"
            : isQueueRow()
            ? SUSAMUNE_GLYPH_A " Remove   " SUSAMUNE_GLYPH_C
              " L" SUSAMUNE_GLYPH_SLASH "R Reorder"
            : isCatalogueRow()
                  ? mStreaking
                        ? SUSAMUNE_GLYPH_A " Select   " SUSAMUNE_GLYPH_C
                          " L" SUSAMUNE_GLYPH_SLASH "R World"
                        : SUSAMUNE_GLYPH_A " Add   " SUSAMUNE_GLYPH_C
                          " L" SUSAMUNE_GLYPH_SLASH "R World"
                  : SUSAMUNE_GLYPH_A " Select   " SUSAMUNE_GLYPH_C
                    " L" SUSAMUNE_GLYPH_SLASH "R Levels";
        menu->drawText(hint, x + 4, y + h - FOOT_SZ,
                       FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    enum Option {
        OPTION_MODE,
        OPTION_RUN,
        OPTION_FINISHES,
        OPTION_TARGET,
        OPTION_DISPLAY,
        OPTION_BUILTIN,
        OPTION_LOAD,
        OPTION_SAVE,
        OPTION_AUTO_RESET,
        OPTION_COUNT_MAX,
    };

    enum Editor {
        EDIT_NONE,
        EDIT_FINISHES,
        EDIT_TARGET,
    };

    int optionCount() const {
        return mStreaking ? OPTION_BUILTIN + 1 : OPTION_AUTO_RESET;
    }
    Option optionAt(int row) const {
        return mStreaking && row == OPTION_BUILTIN
                   ? OPTION_AUTO_RESET
                   : (Option)row;
    }
    Option selectedOption() const {
        return isOption() ? optionAt(mSel) : OPTION_COUNT_MAX;
    }
    bool isOption() const { return mSel < optionCount(); }
    int catalogueFirst() const {
        return optionCount() + (mStreaking ? 0 : StageLoader::queueCount());
    }
    bool isQueueRow() const {
        return !mStreaking && mSel >= optionCount() &&
               mSel < catalogueFirst();
    }
    bool isCatalogueRow() const { return mSel >= catalogueFirst(); }
    int queuePosition() const { return mSel - optionCount(); }
    bool catalogueIncludesEntry(int entry) const {
        return !mStreaking || ILing::streakEntrySelectable(entry);
    }
    int catalogueCount() const {
        if (!mStreaking) return ILing::count();
        int count = 0;
        for (int entry = 0; entry < ILing::count(); entry++) {
            if (catalogueIncludesEntry(entry)) count++;
        }
        return count;
    }
    int catalogueEntryAt(int index) const {
        for (int position = 0; position < ILing::count(); position++) {
            const int entry = ILing::menuEntryAt(position);
            if (!catalogueIncludesEntry(entry)) continue;
            if (index-- == 0) return entry;
        }
        return -1;
    }
    int catalogueIndexForEntry(int selected) const {
        int index = 0;
        for (int position = 0; position < ILing::count(); position++) {
            const int entry = ILing::menuEntryAt(position);
            if (!catalogueIncludesEntry(entry)) continue;
            if (entry == selected) return index;
            index++;
        }
        return -1;
    }
    int catalogueEntry() const {
        return catalogueEntryAt(mSel - catalogueFirst());
    }
    int selectionCount() const {
        return catalogueFirst() + catalogueCount();
    }

    void activateOption(Menu *menu) {
        const Option option = selectedOption();
        if (option == OPTION_MODE) {
            mStreaking = !mStreaking;
            if (mStreaking) {
                if (!ILing::streakEntrySelectable(mStreakEntry)) {
                    mStreakEntry = catalogueEntryAt(0);
                }
                mTargetQf = StageTargets::get(mStreakEntry);
            }
            mSel = OPTION_MODE;
            return;
        }
        if (option == OPTION_RUN) {
            if (!mStreaking && StageLoader::queueCount() == 0) {
                menu->toast("Playlist is empty");
                return;
            }
            const bool started = mStreaking
                                     ? StageLoader::startStreak(
                                           mStreakEntry, mGoal, mTargetQf)
                                     : StageLoader::startLoader();
            if (started) {
                menu->hide();
            } else {
                menu->toast("Session could not start");
            }
            return;
        }
        if (option == OPTION_FINISHES) {
            if (mStreaking) beginTextEditor(EDIT_FINISHES);
            return;
        }
        if (option == OPTION_TARGET) {
            if (mStreaking) {
                beginTextEditor(EDIT_TARGET);
            } else if (StageLoader::queueCount()) {
                StageLoader::clearQueue();
                menu->toast("Playlist cleared");
            } else {
                menu->toast("Playlist is already empty");
            }
            return;
        }
        if (option == OPTION_DISPLAY) {
            gSettings.cycle(SETTING_STAGE_SESSION_DISPLAY, 1);
            return;
        }
        if (option == OPTION_AUTO_RESET) {
            gSettings.cycle(SETTING_STREAK_AUTO_RESET, 1);
            return;
        }
        if (option == OPTION_BUILTIN) {
            if (StageLoader::loadBuiltinPlaylist(mBuiltinPlaylist)) {
                mSel = OPTION_FINISHES;
                menu->toast("Preset loaded");
            } else {
                menu->toast("Preset could not load");
            }
        } else if (option == OPTION_LOAD) {
            if (StageLoader::loadCustomPlaylist(mCustomSlot)) {
                mSel = OPTION_FINISHES;
                menu->toast("Playlist loaded");
            } else {
                menu->toast("Playlist storage unavailable");
            }
        } else if (option == OPTION_SAVE) {
            if (StageLoader::customPlaylistSavePending()) {
                menu->toast("Playlist save already pending");
            } else if (!StageLoader::queueCount()) {
                menu->toast("Playlist is empty");
            } else if (StageLoader::saveCustomPlaylist(mCustomSlot)) {
                menu->toast("Playlist save queued");
            } else {
                menu->toast("Playlist storage unavailable");
            }
        }
    }

    void moveHorizontal(int direction) {
        if (isOption()) {
            const Option option = selectedOption();
            if (option == OPTION_DISPLAY || option == OPTION_AUTO_RESET) {
                gSettings.cycle(option == OPTION_DISPLAY
                                    ? SETTING_STAGE_SESSION_DISPLAY
                                    : SETTING_STREAK_AUTO_RESET,
                                direction);
            } else if (!mStreaking && option == OPTION_BUILTIN) {
                mBuiltinPlaylist = (u8)wrap(
                    mBuiltinPlaylist + (direction < 0 ? -1 : 1),
                    StageLoader::BUILTIN_PLAYLIST_COUNT);
            } else if (!mStreaking &&
                (option == OPTION_LOAD || option == OPTION_SAVE)) {
                mCustomSlot = (u8)wrap(
                    mCustomSlot + (direction < 0 ? -1 : 1),
                    StageLoader::CUSTOM_PLAYLIST_COUNT);
            } else {
                jumpFromOptions(direction);
            }
        } else if (isQueueRow()) {
            const int position = queuePosition();
            if (StageLoader::moveQueue(position, direction)) {
                mSel += direction < 0 ? -1 : 1;
            }
        } else {
            jumpCatalogue(direction);
        }
    }

    void jumpCatalogue(int direction) {
        const int entry = catalogueEntry();
        const int position = ILing::menuPositionOf(entry);
        int groupFirst = position;
        while (groupFirst > 0 && !ILing::beginsMenuGroup(groupFirst)) {
            groupFirst--;
        }
        const int destination = ILing::jumpMenuGroup(position, direction);
        if ((direction < 0 && groupFirst == 0) ||
            (direction > 0 && destination == 0)) {
            mSel = OPTION_MODE;
            return;
        }
        mSel = catalogueFirst() + catalogueIndexForEntry(
            ILing::menuEntryAt(destination));
    }

    void jumpFromOptions(int direction) {
        const int entry = direction > 0 ? 0 : ILing::jumpGroup(0, -1);
        mSel = catalogueFirst() + catalogueIndexForEntry(entry);
    }

    void beginTextEditor(Editor editor) {
        mText[0] = '\0';
        if (editor == EDIT_FINISHES) {
            snprintf(mText, sizeof(mText), "%u", (unsigned)mGoal);
        } else if (mTargetQf >= 0) {
            ILing::formatTime(mTargetQf, mText, sizeof(mText));
        }
        mTextLength = (u8)strlen(mText);
        mTextCursor = 0;
        mTextPage = 1;
        mTextUpper = false;
        mTextInput.begin(keyboardMask());
        mEditor = editor;
    }

    void updateTextEditor(Menu *menu) {
        const u16 pressed = mTextInput.update();
        if (pressed & JUTGamePad::START) {
            if (JUTGamePad::mPadStatus[0].mButton & JUTGamePad::X) {
                mEditor = EDIT_NONE;
                return;
            }
            if (mEditor == EDIT_FINISHES) {
                u16 parsed;
                if (!parseFinishes(mText, &parsed)) {
                    menu->toast("Enter a number from 1 to 999");
                    return;
                }
                mGoal = parsed;
                mEditor = EDIT_NONE;
                menu->toast("Finish count set");
            } else {
                s32 parsed;
                if (!parseTarget(mText, &parsed)) {
                    menu->toast("Use M:SS.mmm, or leave blank");
                    return;
                }
                mTargetQf = parsed;
                StageTargets::set(mStreakEntry, mTargetQf);
                mEditor = EDIT_NONE;
                menu->toast(parsed < 0 ? "Any finish counts"
                                       : "Target time set");
            }
            return;
        }
        if (pressed & JUTGamePad::Z) {
            mTextLength = 0;
            mText[0] = '\0';
            return;
        }
        updateTimeKeyboard(pressed);
    }

    static u16 keyboardMask() {
        return JUTGamePad::A | JUTGamePad::B | JUTGamePad::X |
               JUTGamePad::Y | JUTGamePad::Z | JUTGamePad::START |
               JUTGamePad::L | JUTGamePad::R |
               JUTGamePad::DPAD_LEFT | JUTGamePad::DPAD_RIGHT |
               JUTGamePad::DPAD_UP | JUTGamePad::DPAD_DOWN;
    }

    void updateTimeKeyboard(u16 pressed) {
        const int count = 32;
        if (pressed & JUTGamePad::DPAD_LEFT)
            mTextCursor = (u8)((mTextCursor + count - 1) % count);
        else if (pressed & JUTGamePad::DPAD_RIGHT)
            mTextCursor = (u8)((mTextCursor + 1) % count);
        else if (pressed & JUTGamePad::DPAD_UP)
            mTextCursor = (u8)((mTextCursor + count - 8) % count);
        else if (pressed & JUTGamePad::DPAD_DOWN)
            mTextCursor = (u8)((mTextCursor + 8) % count);
        if (pressed & (JUTGamePad::L | JUTGamePad::R)) {
            mTextPage ^= 1;
            mTextCursor = 0;
        }
        if (pressed & JUTGamePad::Y) mTextUpper = !mTextUpper;
        if ((pressed & JUTGamePad::B) && mTextLength) {
            mText[--mTextLength] = '\0';
        }
        if ((pressed & JUTGamePad::X) &&
            mTextLength + 1 < sizeof(mText)) {
            mText[mTextLength++] = ' ';
            mText[mTextLength] = '\0';
        }
        if ((pressed & JUTGamePad::A) &&
            mTextLength + 1 < sizeof(mText)) {
            const char *characters = mTextPage ? gCreationSymbols
                                              : mTextUpper
                                                    ? gCreationLettersUpper
                                                    : gCreationLettersLower;
            mText[mTextLength++] = characters[mTextCursor];
            mText[mTextLength] = '\0';
        }
    }

    static bool parseFinishes(const char *text, u16 *out) {
        if (!text[0]) return false;
        u32 value = 0;
        for (const char *p = text; *p; p++) {
            if (*p < '0' || *p > '9') return false;
            value = value * 10 + (u32)(*p - '0');
            if (value > 999) return false;
        }
        if (value == 0) return false;
        *out = (u16)value;
        return true;
    }

    static bool parseTarget(const char *text, s32 *out) {
        if (!text[0]) {
            *out = -1;
            return true;
        }

        const char *p = text;
        u64 first = 0;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            first = first * 10 + (u64)(*p++ - '0');
            digits++;
        }
        if (!digits) return false;

        u64 minutes = 0;
        u64 seconds = first;
        bool hasColon = false;
        if (*p == ':') {
            hasColon = true;
            minutes = first;
            seconds = 0;
            digits = 0;
            p++;
            while (*p >= '0' && *p <= '9') {
                seconds = seconds * 10 + (u64)(*p++ - '0');
                digits++;
            }
            if (!digits || seconds >= 60) return false;
        }

        u64 fraction = 0;
        int fractionDigits = 0;
        if (*p == '.') {
            p++;
            while (*p >= '0' && *p <= '9' && fractionDigits < 3) {
                fraction = fraction * 10 + (u64)(*p++ - '0');
                fractionDigits++;
            }
            if (!fractionDigits || (*p >= '0' && *p <= '9')) return false;
            if (fractionDigits == 1) fraction *= 100;
            if (fractionDigits == 2) fraction *= 10;
        }
        if (*p) return false;
        if (minutes > 999 || (!hasColon && seconds > 59999)) return false;

        const u64 millis = (minutes * 60 + seconds) * 1000 + fraction;
        const u64 qf = (((millis + 1) * 120) - 1) / 1001;
        // ILing::formatTime currently multiplies qf by 1001 in signed s32.
        if (qf > 0x7FFFFFFFu / 1001u) return false;
        *out = (s32)qf;
        return true;
    }

    int catalogueRowForEntry(int selected) const {
        int row = 0;
        for (int position = 0; position < ILing::count(); position++) {
            const int entry = ILing::menuEntryAt(position);
            if (!catalogueIncludesEntry(entry)) continue;
            if (ILing::beginsMenuGroup(position)) row++;
            if (entry == selected) return row;
            row++;
        }
        return row;
    }

    int catalogueRowCount() const {
        int rows = 0;
        for (int position = 0; position < ILing::count(); position++) {
            const int entry = ILing::menuEntryAt(position);
            if (!catalogueIncludesEntry(entry)) continue;
            if (ILing::beginsMenuGroup(position)) rows++;
            rows++;
        }
        return rows;
    }

    int catalogueBaseRow() const {
        int row = optionCount();
        if (!mStreaking) {
            row += 1 + (StageLoader::queueCount()
                            ? StageLoader::queueCount()
                            : 1);
        }
        return row + 1;
    }

    int menuRowCount() const {
        return catalogueBaseRow() + catalogueRowCount();
    }

    int menuRowForSelection(int selection) const {
        if (selection < optionCount()) return selection;
        if (!mStreaking && selection < catalogueFirst()) {
            return optionCount() + 1 + selection - optionCount();
        }
        return catalogueBaseRow() + catalogueRowForEntry(
            catalogueEntryAt(selection - catalogueFirst()));
    }

    int mSel;
    u16 mGoal;
    s32 mTargetQf;
    int mStreakEntry;
    bool mStreaking;
    u8 mBuiltinPlaylist;
    u8 mCustomSlot;
    u8 mEditor;
    u8 mTextCursor;
    u8 mTextPage;
    u8 mTextLength;
    bool mTextUpper;
    char mText[16];
    RawPromptInput mTextInput;
};

// The top level stays small while existing stateful pages remain persistent.
class NestedMenuTab final : public MenuTab {
public:
    enum SectionStyle {
        SECTIONS_NONE,
        SECTIONS_SETTINGS,
        SECTIONS_ILS,
    };

    NestedMenuTab(const char *name, MenuTab *const *children, int count,
                  SectionStyle sectionStyle = SECTIONS_NONE)
        : mName(name), mCount((u8)count), mSel(0), mPage(-1),
          mSectionStyle((u8)sectionStyle) {
        for (int i = 0; i < MAX_CHILDREN; i++) {
            mChildren[i] = i < count ? children[i] : nullptr;
        }
        mNavInput.begin(JUTGamePad::A);
    }

    const char *title() const override { return mName; }
    bool grabsInput() const override {
        return current() && current()->grabsInput();
    }
    bool suppressesBinds() const override {
        MenuTab *child = current();
        const u16 held = JUTGamePad::mPadStatus[0].mButton;
        if (!child) return (held & JUTGamePad::A) != 0;
        if (!child->grabsInput() && (held & JUTGamePad::B)) return true;
        return child->suppressesBinds();
    }
    bool fullScreen() const override {
        return current() && current()->fullScreen();
    }
    bool favoriteHint() const override {
        return current() && current()->favoriteHint();
    }
    void focus() override {
        mNavInput.begin(current() ? JUTGamePad::B : JUTGamePad::A);
    }

    bool beginProtectedPBSave(Menu *menu, u32 token) override {
        for (int i = 0; i < mCount; i++) {
            if (!mChildren[i]->beginProtectedPBSave(menu, token)) continue;
            mSel = (u8)i;
            mPage = (s8)i;
            mNavInput.begin(JUTGamePad::B);
            return true;
        }
        return false;
    }

    void update(Menu *menu, TMarioGamePad *pad) override {
        MenuTab *child = current();
        if (child) {
            if (child->grabsInput()) {
                // A raw modal may close on B. Require a fresh release before
                // the same button can also leave its nested page.
                mNavInput.begin(JUTGamePad::B);
                child->update(menu, pad);
                return;
            }
            if (mNavInput.update() & JUTGamePad::B) {
                if (child->back()) {
                    mNavInput.begin(JUTGamePad::B);
                    return;
                }
                mPage = -1;
                mNavInput.begin(JUTGamePad::A);
                return;
            }
            child->update(menu, pad);
            return;
        }

        const u32 rapid = menu->navigationInput(pad);
        if (rapid & TMarioGamePad::CSTICK_UP) {
            mSel = (u8)wrap(mSel - 1, mCount);
        } else if (rapid & TMarioGamePad::CSTICK_DOWN) {
            mSel = (u8)wrap(mSel + 1, mCount);
        } else if (rapid & TMarioGamePad::CSTICK_LEFT) {
            jumpRootSection(-1);
        } else if (rapid & TMarioGamePad::CSTICK_RIGHT) {
            jumpRootSection(+1);
        }
        if (mNavInput.update() & JUTGamePad::A) {
            if (!mChildren[mSel]->available()) {
                menu->toast("Disable boss RNG controls first");
                return;
            }
            mPage = (s8)mSel;
            mNavInput.begin(JUTGamePad::B);
        }
    }

    void draw(Menu *menu, int x, int y, int w, int h) override {
        MenuTab *child = current();
        if (!child) {
            const char *help = mChildren[mSel]->summary();
            const int listH = help ? h - HELP_H : h;
            int rows = mCount;
            int selectedRow = mSel;
            for (int i = 0; i < mCount; i++) {
                if (!sectionName(i)) continue;
                rows++;
                if (i <= mSel) selectedRow++;
            }
            const int maxRows = listH / ROW_H;
            const int start = listScrollStart(selectedRow, rows, maxRows);
            int end = start + maxRows;
            if (end > rows) end = rows;
            int ry = y;
            int row = 0;
            for (int i = 0; i < mCount && row < end; i++) {
                const char *section = sectionName(i);
                if (section) {
                    if (row >= start) {
                        drawSectionHeader(menu, x, ry, w, section);
                        ry += ROW_H;
                    }
                    row++;
                    if (row >= end) break;
                }
                if (row < start) {
                    row++;
                    continue;
                }
                const bool available = mChildren[i]->available();
                const int alertCount = available
                    ? mChildren[i]->rootAlertCount() : -1;
                char alert[20];
                const char *value = available ? nullptr : "Disabled";
                Color valueColor = cValue();
                if (alertCount >= 0) {
                    if (alertCount == 0) {
                        value = "Ready";
                    } else {
                        snprintf(alert, sizeof(alert), "%d blocked", alertCount);
                        value = alert;
                        valueColor = col(255, 72, 72, 255);
                    }
                }
                drawValueRowColored(menu, x, ry, w,
                                    mChildren[i]->title(), value,
                                    i == mSel, false, available, valueColor);
                ry += ROW_H;
                row++;
            }
            drawScrollHints(menu, x, y, w, listH, start, end, rows);
            drawHelpLine(menu, x, y, w, h, help);
            return;
        }

        if (child->fullScreen()) {
            child->draw(menu, x, y, w, h);
            return;
        }

        child->draw(menu, x, y, w, h - ROW_H);
        menu->drawText(SUSAMUNE_GLYPH_B " Back", x + 4,
                       y + h - FOOT_SZ,
                       FOOT_SZ, FOOT_SZ, cFooter());
    }

private:
    enum { MAX_CHILDREN = 10 };

    MenuTab *current() const {
        MenuTab *child = mPage >= 0 && mPage < mCount
                             ? mChildren[mPage] : nullptr;
        return child && child->available() ? child : nullptr;
    }

    const char *sectionName(int child) const {
        if (mSectionStyle == SECTIONS_SETTINGS) {
            if (child == 0) return "PB STATUS";
            if (child == 1) return "GAMEPLAY AND PRACTICE";
            if (child == 5) return "TIMING AND HUD";
            if (child == 8) return "LAYOUT AND CONTROLS";
        } else if (mSectionStyle == SECTIONS_ILS) {
            if (child == 0) return "PRACTICE";
            if (child == 2) return "SESSIONS";
        }
        return nullptr;
    }

    void jumpRootSection(int direction) {
        if (mSectionStyle == SECTIONS_NONE) return;
        if (direction > 0) {
            for (int child = mSel + 1; child < mCount; child++) {
                if (sectionName(child)) {
                    mSel = (u8)child;
                    return;
                }
            }
            for (int child = 0; child <= mSel; child++) {
                if (sectionName(child)) {
                    mSel = (u8)child;
                    return;
                }
            }
        } else {
            for (int child = mSel - 1; child >= 0; child--) {
                if (sectionName(child)) {
                    mSel = (u8)child;
                    return;
                }
            }
            for (int child = mCount - 1; child >= mSel; child--) {
                if (sectionName(child)) {
                    mSel = (u8)child;
                    return;
                }
            }
        }
    }

    const char *mName;
    MenuTab *mChildren[MAX_CHILDREN];
    u8 mCount;
    u8 mSel;
    s8 mPage;
    u8 mSectionStyle;
    RawPromptInput mNavInput;
};

static_assert(sizeof(NestedMenuTab) <= 64,
              "nested menu router grew unexpectedly");

// =====================================================================
// Menu
// =====================================================================

namespace {

Records::AchievementId sAchievementBannerId = Records::ACHIEVEMENT_INVALID;
int sAchievementBannerFrames = 0;
bool sAchievementChimePending = false;
bool sAchievementBatchActive = false;

void discardAchievementNotifications() {
    sAchievementBannerId = Records::ACHIEVEMENT_INVALID;
    sAchievementBannerFrames = 0;
    sAchievementChimePending = false;
    sAchievementBatchActive = false;
    Records::AchievementId ignored;
    while (Records::popUnlock(&ignored)) {}
}

void updateAchievementChime() {
    if (!sAchievementChimePending) return;
    if (ILing::achievementChimeBlocked()) {
        // A PB already owns this celebration batch.
        sAchievementChimePending = false;
        return;
    }
    if (!gpMSound || gpApplication.mContext !=
                            TApplication::CONTEXT_DIRECT_STAGE ||
        !gpMarDirector || gpMarDirector->_260 == 0 ||
        gpMarDirector->mCurState < TMarDirector::STATE_GAME_STARTING ||
        gpMarDirector->mCurState == TMarDirector::STATE_STAGE_EXIT ||
        gpMarDirector->mCurState == TMarDirector::STATE_STAGE_EXIT_2) {
        return;
    }
    gpMSound->startSoundSystemSE(MSD_SE_SY_MENU_SHINE_LIGHT, 0, nullptr, 0);
    sAchievementChimePending = false;
}

void updateAchievementBanner() {
    if (!gSettings.getBool(SETTING_ACHIEVEMENT_NOTIFICATIONS)) {
        discardAchievementNotifications();
        return;
    }
    if (sAchievementBannerFrames > 0) sAchievementBannerFrames--;
    if (sAchievementBannerFrames != 0) {
        updateAchievementChime();
        return;
    }

    Records::AchievementId id;
    if (Records::popUnlock(&id)) {
        sAchievementBannerId = id;
        sAchievementBannerFrames = 120;  // four seconds at Susamune's 30 fps
        if (!sAchievementBatchActive) {
            sAchievementBatchActive = true;
            sAchievementChimePending = true;
        }
        updateAchievementChime();
    } else {
        sAchievementBatchActive = false;
        sAchievementChimePending = false;
    }
}

void drawAchievementBanner(Menu *menu) {
    const bool preview = gCreationExtras.editingAchievementBanner();
    if (!menu || (!preview &&
        (!gSettings.getBool(SETTING_ACHIEVEMENT_NOTIFICATIONS) ||
         sAchievementBannerFrames == 0))) return;

    Records::Tier tier = Records::TIER_GOLD;
    Records::Category category = Records::CATEGORY_TIMES;
    const char *name = "Achievement preview";
    if (!preview) {
        const Records::AchievementDesc *desc =
            Records::achievement(sAchievementBannerId);
        if (!desc) return;
        tier = desc->tier;
        category = desc->category;
        name = recordText(desc->name, "Unnamed achievement");
    }

    const CreationStyle &style = gCreationExtras.achievementBannerStyle();
    const int scale = clampi(style.scale, 50, 156);
    const int w = 410 * scale / 100;
    const int h = 58 * scale / 100;
    const int x = w < 640 ? clampi(style.x, 0, 640 - w) : 0;
    const int y = h < 480 ? clampi(style.y, 0, 480 - h) : 0;
    const int border = 4 * scale / 100;
    const int pad = 14 * scale / 100;
    const int labelSize = 12 * scale / 100;
    const char *tierName = recordTierName(tier);
    const char *categoryName = recordText(Records::categoryName(category));
    const Color outline = recordTierColor(tier);

    menu->fillBox(x, y, w, h, outline);
    menu->fillBox(x + border, y + border, w - border * 2, h - border * 2,
                  col(4, 6, 12, 225));
    menu->drawText("ACHIEVEMENT UNLOCKED", x + pad, y + 10 * scale / 100,
                   labelSize, labelSize, cRowDim());
    const int tierX = x + w - Menu::textWidth(tierName, labelSize) - pad;
    menu->drawText(tierName, tierX, y + 10 * scale / 100,
                   labelSize, labelSize, outline);
    const int slashX = tierX - Menu::textWidth(" / ", labelSize);
    menu->drawText(" / ", slashX, y + 10 * scale / 100,
                   labelSize, labelSize, cRowDim());
    menu->drawText(categoryName,
                   slashX - Menu::textWidth(categoryName, labelSize),
                   y + 10 * scale / 100,
                   labelSize, labelSize, cRowSel());
    const int nameSize = fittedRecordTextSize(
        name, w - pad * 2, 18 * scale / 100, labelSize);
    menu->drawText(name, x + pad, y + 31 * scale / 100,
                   nameSize, nameSize, cRowSel());
}

}  // namespace

// Static tab instances (constructed via placement new in Menu::Menu so their
// vtables are set without relying on C++ static-init, which the injected mod
// does not run).
namespace {
struct __attribute__((aligned(8))) MenuRuntime {
#if ENABLE_DEBUG_WARPS
    u8 presets[sizeof(WarpPresetsTab)] __attribute__((aligned(8)));
    u8 stages[sizeof(WarpStagesTab)] __attribute__((aligned(8)));
#endif
    u8 pbSafety[sizeof(PBSafetyTab)] __attribute__((aligned(8)));
    u8 starred[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 qol[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 cosmetic[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 misc[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 savestate[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 ui[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 timer[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 rng[sizeof(CategorySettingsTab)] __attribute__((aligned(8)));
    u8 creation[sizeof(CreationTab)] __attribute__((aligned(8)));
    u8 iling[sizeof(ILingTab)] __attribute__((aligned(8)));
    u8 ghosts[sizeof(GhostsTab)] __attribute__((aligned(8)));
    u8 records[sizeof(RecordsTab)] __attribute__((aligned(8)));
    u8 binds[sizeof(BindsTab)] __attribute__((aligned(8)));
    u8 stageLoader[sizeof(StageLoaderTab)] __attribute__((aligned(8)));
    u8 settingsHub[sizeof(NestedMenuTab)] __attribute__((aligned(8)));
    u8 ilsHub[sizeof(NestedMenuTab)] __attribute__((aligned(8)));
    u8 menu[sizeof(Menu)] __attribute__((aligned(8)));
};

MenuRuntime &sMenuRuntime = *reinterpret_cast<MenuRuntime *>(
    SUSAMUNE_MEM2_MENU_RUNTIME_PPC_BASE);
static_assert(sizeof(MenuRuntime) <= SUSAMUNE_MENU_RUNTIME_SIZE,
              "menu state exceeds its MEM2 runtime window");

#if ENABLE_DEBUG_WARPS
#define sPresetsBuf sMenuRuntime.presets
#define sStagesBuf sMenuRuntime.stages
#endif
#define sPbSafetyBuf sMenuRuntime.pbSafety
#define sStarredBuf sMenuRuntime.starred
#define sQolBuf sMenuRuntime.qol
#define sCosmeticBuf sMenuRuntime.cosmetic
#define sMiscBuf sMenuRuntime.misc
#define sSavestateBuf sMenuRuntime.savestate
#define sUiBuf sMenuRuntime.ui
#define sTimerBuf sMenuRuntime.timer
#define sRngBuf sMenuRuntime.rng
#define sCreationBuf sMenuRuntime.creation
#define sILingBuf sMenuRuntime.iling
#define sGhostsBuf sMenuRuntime.ghosts
#define sRecordsBuf sMenuRuntime.records
#define sBindsBuf sMenuRuntime.binds
#define sStageLoaderBuf sMenuRuntime.stageLoader
#define sSettingsHubBuf sMenuRuntime.settingsHub
#define sILsHubBuf sMenuRuntime.ilsHub
#define sMenuBuf sMenuRuntime.menu
}  // namespace

Menu::Menu() : mText(gpSystemFont->mFont, " ") {
    mOrtho       = nullptr;
    mShown       = false;
    mCurTab      = 0;
    mNumTabs     = 0;
    mTabFirst    = 0;
    mToastBuf[0] = '\0';
    mToastFrames = 0;
    mSaveWatch   = false;
    mCRepeatFrames = 0;

    // Cache font metrics. J2DTextBox::draw(x, y) places the text *baseline* at
    // y (glyphs render upward from it: top = y - ascent*size/height). drawText
    // converts a top-anchored y into that baseline so text lines up with the
    // fills drawn behind it.
    mFontAscent = mText.mFont->getAscent();
    mFontHeight = mText.mFont->getHeight();
    if (mFontHeight <= 0) {
        mFontHeight = 1;
    }

    // Advance metrics for the static textWidth(). mFont->_4/_8 are JUTFont's
    // fixed-advance override: when set, every glyph advances by _8 regardless
    // of its width entry.
    sFont        = mText.mFont;
    sFontWidth   = sFont->getWidth();
    if (sFontWidth <= 0) {
        sFontWidth = 1;
    }
    sCharSpacing = (int)mText.mCharSpacing;
    sFontFixed   = (sFont->_4 != 0);
    sFontFixedW  = (int)sFont->_8;

    // The ctor above allocated a small string buffer on the current heap via
    // setString(); free it and never let mText reallocate again. From here on
    // drawText() only assigns mStrPtr to borrowed const strings.
    if (mText.mStrPtr) {
        delete[] mText.mStrPtr;
        mText.mStrPtr = nullptr;
    }

#if ENABLE_DEBUG_WARPS
    mTabs[mNumTabs++] = new (sPresetsBuf) WarpPresetsTab();
    mTabs[mNumTabs++] = new (sStagesBuf) WarpStagesTab();
#endif
    MenuTab *starred =
        new (sStarredBuf) CategorySettingsTab(TITLE_STARRED,
                                              kStarredCategory);
    MenuTab *pbSafety = new (sPbSafetyBuf) PBSafetyTab();
    MenuTab *practice =
        new (sMiscBuf) CategorySettingsTab(TITLE_MISC, SETTING_CAT_MISC);
    MenuTab *savestate =
        new (sSavestateBuf) CategorySettingsTab(TITLE_SAVESTATE,
                                                SETTING_CAT_SAVESTATE);
    MenuTab *timer =
        new (sTimerBuf) CategorySettingsTab(TITLE_TIMER, SETTING_CAT_TIMER);
    MenuTab *gameplay =
        new (sQolBuf) CategorySettingsTab(TITLE_QOL, SETTING_CAT_QOL);
    MenuTab *rng =
        new (sRngBuf) CategorySettingsTab(TITLE_RNG, kRngCategory);
    MenuTab *display =
        new (sUiBuf) CategorySettingsTab(TITLE_UI, SETTING_CAT_UI);
    MenuTab *cosmetics =
        new (sCosmeticBuf) CategorySettingsTab(TITLE_COSMETIC,
                                               SETTING_CAT_COSMETIC);
    MenuTab *creation = new (sCreationBuf) CreationTab();
    MenuTab *binds = new (sBindsBuf) BindsTab();
    MenuTab *iling = new (sILingBuf) ILingTab();
    MenuTab *ghosts = new (sGhostsBuf) GhostsTab();
    MenuTab *stageLoader = new (sStageLoaderBuf) StageLoaderTab();
    MenuTab *records = new (sRecordsBuf) RecordsTab();

    MenuTab *settingsChildren[] = {
        pbSafety, gameplay, practice, rng, savestate,
        timer, display, cosmetics, creation, binds,
    };
    MenuTab *ilChildren[] = {
        iling, ghosts, stageLoader,
    };
    static_assert(sizeof(settingsChildren) / sizeof(settingsChildren[0]) <= 10,
                  "Settings hub exceeds nested menu capacity");
    static_assert(sizeof(ilChildren) / sizeof(ilChildren[0]) <= 8,
                  "IL hub exceeds nested menu capacity");

    mTabs[mNumTabs++] = starred;
    mTabs[mNumTabs++] =
        new (sSettingsHubBuf) NestedMenuTab(
            "Settings", settingsChildren,
            sizeof(settingsChildren) / sizeof(settingsChildren[0]),
            NestedMenuTab::SECTIONS_SETTINGS);
    mTabs[mNumTabs++] =
        new (sILsHubBuf) NestedMenuTab(
            "ILs", ilChildren, sizeof(ilChildren) / sizeof(ilChildren[0]),
            NestedMenuTab::SECTIONS_ILS);
    mTabs[mNumTabs++] = records;
}

bool Menu::openGhostPBSave(u32 token) {
    for (int i = 0; i < mNumTabs; i++) {
        if (!mTabs[i]->beginProtectedPBSave(this, token)) continue;
        mCurTab = i;
        mTabFirst = 0;
        mCRepeatFrames = 0;
        mTabs[i]->focus();
        mShown = true;
        return true;
    }
    return false;
}

int Menu::textWidth(const char *s, int sizeX) {
    if (!sFont) {
        return 0;
    }

    // Mirror J2DPrint::parse, which is what J2DTextBox::draw runs through. Per
    // glyph the pen advances by TWidth::mWidth (both width bytes are read
    // unsigned there), and only the first glyph of the line also pays its
    // mMargin left bearing -- later glyphs are drawn back-shifted by their own
    // margin instead. The font is proportional, so anything that assumes a
    // fixed advance drifts and right-aligned text stops lining up.
    int units = 0;  // total advance in font design units
    int count = 0;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(s); *p;) {
        int code = *p;
        if (sFont->isLeadByte(code) && p[1]) {
            code = (code << 8) | p[1];
            p += 2;
        } else {
            p += 1;
        }
#if defined(SUSAMUNE_VERSION_JP)
        if (code == '/') code = 0x815e;
#endif
        if (sFontFixed) {
            units += sFontFixedW;
        } else {
            JUTFont::TWidth tw;
            sFont->getWidthEntry(code, &tw);
            units += (u8)tw.mWidth;
            if (count == 0) {
                units += (u8)tw.mMargin;
            }
        }
        count++;
    }

    int w = units * sizeX / sFontWidth;
    if (count > 1) {
        w += sCharSpacing * (count - 1);
    }
    return w;
}

void Menu::drawText(const char *s, int x, int y, int sizeX, int sizeY, Color color) {
    color = warningText(color, mShown);
    mText.mCharSizeX      = sizeX;
    mText.mCharSizeY      = sizeY;
    mText.mGradientTop    = color;
    mText.mGradientBottom = color;
#if defined(SUSAMUNE_VERSION_JP)
    s = fontSafeText(s);
#endif
    mText.mStrPtr         = const_cast<char *>(s);
    // `y` is the cell top; convert to the baseline J2DTextBox::draw expects.
    int baseline = y + mFontAscent * sizeY / mFontHeight;
    mText.draw(x, baseline);
}

#if ENABLE_SAVESTATE_DBG
void Menu::drawTextBaseline(const char *s, int x, int y, int sizeX, int sizeY,
                            Color color) {
    color = warningText(color, mShown);
    mText.mCharSizeX      = sizeX;
    mText.mCharSizeY      = sizeY;
    mText.mGradientTop    = color;
    mText.mGradientBottom = color;
#if defined(SUSAMUNE_VERSION_JP)
    s = fontSafeText(s);
#endif
    mText.mStrPtr = const_cast<char *>(s);
    mText.draw(x, y);
}
#endif

void Menu::fillBox(int x, int y, int w, int h, Color color) {
    color = warningForeground(color, mShown);
    // Restore the flat pos+color vertex state before filling. J2DFillBox draws
    // with whatever GX vertex descriptor is current, and J2DTextBox/JUTResFont
    // switch it to a textured layout; without this, any fill after a text draw
    // renders as skewed garbage. setup2D() leaves the projection alone.
    if (mOrtho) {
        mOrtho->setup2D();
    }
    J2DFillBox(x, y, w, h, color);
}

namespace {

__attribute__((noinline)) void drawPolygon(J2DOrthoGraph *ortho,
                                           const s16 *xy, int n, bool closed,
                                           const Color &color) {
    // setup2D() reasserts the flat vertex state and the position matrix, the
    // same preconditions J2DGrafContext::fillBox relies on.
    if (!ortho || (closed && n < 2)) {
        return;
    }
    // Vertices go to the write-gather pipe by address: GX.h reaches it through
    // a `wgPipe` object that none of the game maps export.
    volatile u16 *const wgU16 = (volatile u16 *)0xCC008000;
    volatile u32 *const wgU32 = (volatile u32 *)0xCC008000;

    ortho->setup2D();
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_SET);
    if (closed) {
        GXSetLineWidth(20, GX_TO_ZERO);
    }
    GXBegin(closed ? GX_LINESTRIP : GX_TRIANGLEFAN, GX_VTXFMT0,
            (u16)(n + closed));
    const u32 packed = ((u32)color.r << 24) | ((u32)color.g << 16) |
                       ((u32)color.b << 8) | (u32)color.a;
    for (int i = 0; i < n + closed; i++) {
        const int j = (i == n) ? 0 : i;
        *wgU16 = (u16)xy[j * 2];
        *wgU16 = (u16)xy[j * 2 + 1];
        *wgU16 = 0;
        *wgU32 = packed;
    }
}

}  // namespace

void Menu::fillPoly(const s16 *xy, int n, Color color) {
    drawPolygon(mOrtho, xy, n, false, warningForeground(color, mShown));
}

void Menu::strokePoly(const s16 *xy, int n, Color color) {
    drawPolygon(mOrtho, xy, n, true, warningForeground(color, mShown));
}

void Menu::switchTab(int dir) {
    mCurTab = wrap(mCurTab + dir, mNumTabs);
    mCRepeatFrames = 0;
    mTabs[mCurTab]->focus();
}

u32 Menu::navigationInput(TMarioGamePad *pad) {
    u32 rapid = pad->mButtons.mRapidInput;
    const u32 vertical = pad->mButtons.mInput &
        (TMarioGamePad::CSTICK_UP | TMarioGamePad::CSTICK_DOWN);
    if (!vertical || (rapid & vertical)) {
        mCRepeatFrames = 0;
    } else if (++mCRepeatFrames >= 18) {
        rapid |= vertical;
        mCRepeatFrames = 14;
    }
    return rapid;
}

// =====================================================================
// Toast + settings auto-save
// =====================================================================

void Menu::toast(const char *msg) {
    strncpy(mToastBuf, msg, sizeof(mToastBuf));
    mToastFrames = kToastFrames;
}

void Menu::drawToast() {
    if (mToastFrames <= 0 || mToastBuf[0] == '\0') {
        return;
    }
    gCreationExtras.drawToast(this, mToastBuf);
}

void Menu::requestSettingsSave() {
    // The console kernel reads this mailbox asynchronously. Keep its staged
    // payload immutable; dirty edits are queued after the current ack.
    if (gSettings.saveState() == SETTINGS_SAVE_PENDING) {
        mSaveWatch = true;
        toast("Saving settings...");
        return;
    }
    gSettings.save();
    if (gSettings.saveState() == SETTINGS_SAVE_UNSUPPORTED) {
        const char *error = settingsStorageError(gSettings.lastError());
        if (error) {
            snprintf(mToastBuf, sizeof(mToastBuf), "Settings unavailable: %s", error);
        } else {
            snprintf(mToastBuf, sizeof(mToastBuf), "Settings unavailable (error %lu)",
                     gSettings.lastError());
        }
        mToastFrames = kToastFrames;
        return;
    }
    mSaveWatch = true;
    toast("Saving settings...");
}

__attribute__((noinline)) static void drawValueRow(
    Menu *menu, int x, int y, int w, const char *name, const char *value,
    bool selected, bool starred, bool arrow) {
    drawValueRowColored(menu, x, y, w, name, value, selected, starred, arrow,
                        cValue());
}

__attribute__((noinline)) static void drawValueRowColored(
    Menu *menu, int x, int y, int w, const char *name, const char *value,
    bool selected, bool starred, bool arrow, Color valueColor) {
    if (selected) drawRowHighlight(menu, x, y, w, ROW_H);
    if (selected && arrow)
        menu->drawText(">", x - 2, y, ROW_SZ, ROW_SZ, cAccent());
    if (starred)
        menu->drawText(SUSAMUNE_GLYPH_SHINED, x + (arrow ? 8 : 4), y,
                       ROW_SZ, ROW_SZ, cAccent());
    menu->drawText(name, x + (arrow ? 12 : 4) +
                         (starred ? (arrow ? 12 : 16) : 0), y,
                   ROW_SZ, ROW_SZ, selected ? cRowSel() : cRow());
    if (value)
        menu->drawText(value, x + w - Menu::textWidth(value, ROW_SZ) - 8,
                       y, ROW_SZ, ROW_SZ, valueColor);
}

__attribute__((noinline)) static void drawHelpLine(
    Menu *menu, int x, int y, int w, int h, const char *text) {
    if (!text || !text[0]) return;
    const int top = y + h - HELP_H;
    menu->fillBox(x + 4, top, w - 8, 1, cRowDim());
    menu->drawText(text, x + 6, top + 10, FOOT_SZ, FOOT_SZ, cFooter());
}

void Menu::factoryReset() {
    if (gSettings.saveState() == SETTINGS_SAVE_PENDING) {
        mSaveWatch = true;
        toast("Wait for settings save");
        return;
    }
    gCreationExtras.restoreHudDefaults();
    gSettings.resetDefaults();
    requestSettingsSave();
}

void Menu::scheduleSettingsSave() { requestSettingsSave(); }

void Menu::hide() {
    mShown = false;
    if (gSettings.dirty() || gBinds.dirty() || gInputDisplay.dirty() ||
        gMetadataDisplay.dirty() || gQftDisplay.dirty() ||
        gCreationExtras.dirty()) {
        requestSettingsSave();
    }
}

void Menu::pollSettingsSave() {
    SettingsSaveState st = gSettings.pollSave();
    if (!mSaveWatch) {
        return;
    }

    if (st == SETTINGS_SAVE_PENDING) {
        // Hold the "saving" message up rather than letting it time out.
        mToastFrames = kToastFrames;
        return;
    }

    mSaveWatch = false;
    switch (st) {
    case SETTINGS_SAVE_OK:
        // A guarded IL start can be cancelled while its first settings write
        // is in flight. Persist the restored value instead of acknowledging a
        // stale snapshot and leaving the dirty correction only in RAM.
        if (gSettings.dirty() || gBinds.dirty() ||
            gInputDisplay.dirty() || gMetadataDisplay.dirty() ||
            gQftDisplay.dirty() || gCreationExtras.dirty()) {
            requestSettingsSave();
        } else {
            toast("Settings saved");
        }
        break;
    case SETTINGS_SAVE_ERROR: {
        const char *error = settingsStorageError(gSettings.lastError());
        if (error) {
            snprintf(mToastBuf, sizeof(mToastBuf), "Settings save failed: %s", error);
        } else {
            snprintf(mToastBuf, sizeof(mToastBuf), "Settings save failed (error %lu)",
                     gSettings.lastError());
        }
        mToastFrames = kToastFrames;
        break;
    }
    case SETTINGS_SAVE_TIMEOUT:
        toast("Settings save timed out");
        break;
    default:
        break;
    }
}

void Menu::drawTabStrip(int x, int y, int w) {
    const int stripX = x + TAB_CHEV;
    const int visW   = w - TAB_CHEV * 2;

    // The strip scrolls by whole tabs, not by pixels: mTabFirst is the leftmost
    // one drawn and it always sits flush at stripX. Scrolling by pixels instead
    // leaves a ragged part-tab-wide gap on the left, since a tab that does not
    // fit entirely is skipped.
    // Derive the window from the selected tab so navigation history and
    // regional font widths cannot leave different tabs visible.
    mTabFirst = 0;
    // Advance the window until the selected tab fits at its right end.
    while (mTabFirst < mCurTab) {
        int span = 0;
        for (int i = mTabFirst; i <= mCurTab; i++) {
            span += tabWidth(i) + (i > mTabFirst ? TAB_GAP : 0);
        }
        if (span <= visW) {
            break;
        }
        mTabFirst++;
    }

    int cursor = stripX;
    int last   = mTabFirst;
    for (int i = mTabFirst; i < mNumTabs; i++) {
        int tw = tabWidth(i);
        if (i > mTabFirst && cursor + tw > stripX + visW) {
            break;  // no room for this one; the rest are further right still
        }
        bool active = (i == mCurTab);
        if (active) {
            fillBox(cursor, y, tw, TAB_STRIP_H, cAccent());
        }
        drawText(mTabs[i]->title(), cursor + TAB_INNER, y + 6, TAB_SZ, TAB_SZ,
                 active ? cTabOnText() : cTabIdle());
        cursor += tw + TAB_GAP;
        last = i;
    }

    // Scroll chevrons when tabs overflow either edge.
    if (mTabFirst > 0) {
        drawText(SUSAMUNE_GLYPH_LEFT, x - 2, y + 6, TAB_SZ, TAB_SZ, cAccent());
    }
    if (last < mNumTabs - 1) {
        drawText(SUSAMUNE_GLYPH_RIGHT, x + w - TAB_CHEV + 2, y + 6,
                 TAB_SZ, TAB_SZ, cAccent());
    }
}

int Menu::tabWidth(int i) const {
    return textWidth(mTabs[i]->title(), TAB_SZ) + TAB_INNER * 2;
}

bool Menu::suppressesBinds() const {
    if (WarpWheel::promptPending()) return true;
    if (gBinds.wasPressedRaw(BIND_MENU_TOGGLE)) return true;
    if (!mShown) return false;
    const u16 held = JUTGamePad::mPadStatus[0].mButton;
    return (held & (JUTGamePad::L | JUTGamePad::R)) != 0 ||
           mTabs[mCurTab]->suppressesBinds();
}

void Menu::update(TMarioGamePad *pad) {
    updateAchievementBanner();

    // Toast bookkeeping runs whether or not the menu is open -- the save it
    // reports is normally started by the menu closing.
    if (mToastFrames > 0) {
        mToastFrames--;
    }
    pollSettingsSave();
    StageTargets::service(this);

    if (WarpWheel::promptShown()) {
        mCRepeatFrames = 0;
        return;
    }

    u32 rapid = pad->mButtons.mRapidInput;

    // A tab recording a button combo owns the pad outright: the close combo and
    // the tab-switch buttons are all bindable, so nothing else may look at them.
    if (mShown && mTabs[mCurTab]->grabsInput()) {
        mCRepeatFrames = 0;
        mTabs[mCurTab]->update(this, pad);
        return;
    }

    if (gBinds.wasPressedRaw(BIND_MENU_TOGGLE)) {
        mShown = !mShown;
        if (mShown) mTabs[mCurTab]->focus();
        // Closing with edits pending writes them back to the SD card. Gated on
        // dirty() so merely opening and closing the menu never touches storage.
        if (!mShown && (gSettings.dirty() || gBinds.dirty() ||
                        gInputDisplay.dirty() || gMetadataDisplay.dirty() ||
                        gQftDisplay.dirty() || gCreationExtras.dirty())) {
            requestSettingsSave();
        }
        return;
    }
    if (!mShown) {
        mCRepeatFrames = 0;
        return;
    }

    if (rapid & TMarioGamePad::L) {
        switchTab(-1);
    }
    if (rapid & TMarioGamePad::R) {
        switchTab(+1);
    }

    const bool invalidBefore = rngControlInvalidatesIl();
    mTabs[mCurTab]->update(this, pad);
    if (invalidBefore != rngControlInvalidatesIl())
        gCreationExtras.update();
}

void Menu::draw(J2DOrthoGraph *ortho) {
    mOrtho = ortho;  // used by fillBox() to re-enter 2D state
    if (!mShown) {
        Ghost::draw(this);
        gInputDisplay.draw(this);
        gMetadataDisplay.draw(this);
        gQftDisplay.beginOverlayFrame();
        gQFTTimer.draw(this);
        SplitStats::draw(this);
        gAttemptCounter.draw(this);
        gCreationExtras.draw(this);
        WallkickDisplay::draw(this);
        MovementDisplay::draw(this);
        drawToast();  // still visible with the menu closed
        drawAchievementBanner(this);
        bgmStatsDraw(this);
        return;
    }

    if (mTabs[mCurTab]->fullScreen()) {
        mTabs[mCurTab]->draw(this, 0, 0, 640, 480);
        drawToast();
        drawAchievementBanner(this);
        return;
    }

    // Dim the whole frame, then the panel on top.
    fillBox(0, 0, 640, 480, cBackdrop());
    fillBox(PANEL_X, PANEL_Y, PANEL_W, PANEL_H, cPanel());
    // Accent rule at the top edge of the panel.
    fillBox(PANEL_X, PANEL_Y, PANEL_W, 3, cAccent());

    // Title + accent underline.
    drawText("Moonshine", PANEL_X + PAD - 2, PANEL_Y + 12,
             TITLE_SZ, TITLE_SZ, cTitle());
    fillBox(PANEL_X + PAD, PANEL_Y + 12 + TITLE_SZ + 1, 150, 2, cAccent());

    drawTabStrip(PANEL_X + PAD, TAB_STRIP_Y, PANEL_W - PAD * 2);

    int cx = PANEL_X + PAD;
    int cy = CONTENT_Y;
    int cw = PANEL_W - PAD * 2;
    int ch = FOOTER_Y - CONTENT_Y - 8;
    mTabs[mCurTab]->draw(this, cx, cy, cw, ch);

    // The close hint names the live menu bind rather than a fixed combo, since
    // it is user-configurable (and re-bindable to something unguessable).
    {
        const char *hint = mTabs[mCurTab]->favoriteHint()
            ? SUSAMUNE_GLYPH_L SUSAMUNE_GLYPH_SLASH SUSAMUNE_GLYPH_R " Tabs  "
              SUSAMUNE_GLYPH_C " Move  " SUSAMUNE_GLYPH_A " Select  "
              SUSAMUNE_GLYPH_X " Shine  "
            : SUSAMUNE_GLYPH_L SUSAMUNE_GLYPH_SLASH SUSAMUNE_GLYPH_R " Tabs    "
              SUSAMUNE_GLYPH_C " Move    "
              SUSAMUNE_GLYPH_A " Select    ";
        int hx = PANEL_X + PAD;
        drawText(hint, hx, FOOTER_Y, FOOT_SZ, FOOT_SZ, cFooter());
        hx += textWidth(hint, FOOT_SZ);

        char combo[kBindTextMax];
        Binds::format(gBinds.get(BIND_MENU_TOGGLE), combo);
        drawText(combo, hx, FOOTER_Y, FOOT_SZ, FOOT_SZ, cFooter());
        hx += textWidth(combo, FOOT_SZ);
        drawText(" Close", hx, FOOTER_Y, FOOT_SZ, FOOT_SZ, cFooter());
    }

    // Last, so it sits above the panel rather than under the backdrop.
    drawToast();
    drawAchievementBanner(this);
    bgmStatsDraw(this);
}

void Menu::drawInvalidIlWarning() {
    if (!rngControlInvalidatesIl()) return;
    const char *text = "INVALID IL";
    const int size = 18;
    const int visibleBottom = 448;
    const int bottomInset = 12;
    const int x = 616 - textWidth(text, size);
    const int y = visibleBottom - size - bottomInset;
    static_assert(bottomInset >= 2, "INVALID IL warning exceeds the EFB");
    fillBox(x - 4, y - 2, textWidth(text, size) + 8, size + 4,
            col(0, 0, 0, 190));
    drawText(text, x, y, size, size, col(255, 0, 0, 255));
}

// =====================================================================
// Global instance
// =====================================================================

Menu *gMenu = nullptr;

void menuInit() { gMenu = new (sMenuBuf) Menu(); }
