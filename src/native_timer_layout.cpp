#include "susamune/native_timer_layout.hxx"
#include "susamune/retail_input.hxx"

#include "Dolphin/mem.h"
#include "JSystem/J2D/J2DScreen.hxx"
#include "JSystem/J2D/J2DPicture.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/native_timer_transform.h"
#include "susamune/creation_extras.hxx"
#include "susamune/mem2_map.h"

extern "C" void *retailPaneVtable[] asm("__vt__7J2DPane");
extern "C" void *retailPictureVtable[] asm("__vt__10J2DPicture");
extern "C" void *retailTextVtable[] asm("__vt__10J2DTextBox");

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"

namespace NativeTimerLayout {
namespace {

constexpr unsigned kMaxPanes = 32;
constexpr unsigned kVtableWords = 11;
constexpr unsigned kMakeMatrixSlot = 10;
constexpr unsigned kGeometryBytes = 0xB4 - 0x24;
typedef void (*MakeMatrix)(J2DPane *, int, int);

struct PaneState {
    J2DPane *pane;
    void **vtable;
    u8 geometry[kGeometryBytes];
    u8 alphaCopy;
    u8 alpha;
    bool visible;
    JUtility::TColor white;
    JUtility::TColor black;
};

struct DrawState {
    PaneState panes[kMaxPanes];
    void *drawVtables[3][kVtableWords];
    int rootRect[4];
    unsigned count;
    int percent;
};

#if defined(__powerpc__)
#define sDraw (*reinterpret_cast<DrawState *>(SUSAMUNE_NATIVE_TIMER_DRAW_PPC_BASE))
#else
DrawState sDraw;
#endif
bool sActive;

static_assert(__builtin_offsetof(J2DPane, mCRect) == 0x24,
              "J2DPane draw bounds moved");
static_assert(__builtin_offsetof(J2DPane, mScreenMtx) == 0x54,
              "J2DPane local matrix moved");
static_assert(__builtin_offsetof(J2DPane, _B4) == 0xB4,
              "J2DPane global matrix end moved");
static_assert(sizeof(DrawState) <= 0x1600, "timer draw snapshot grew");

void applyBrightness(JUtility::TColor &color, unsigned percent) {
    u8 *rgb = &color.r;
    for (unsigned i = 0; i < 3; ++i) {
        const unsigned value = rgb[i] * percent / 100;
        rgb[i] = value > 255 ? 255 : value;
    }
}

void makeDrawMatrix(J2DPane *pane, int x, int y) {
    for (unsigned i = 0; i < sDraw.count; ++i) {
        PaneState &saved = sDraw.panes[i];
        if (saved.pane != pane) continue;
        reinterpret_cast<MakeMatrix>(saved.vtable[kMakeMatrixSlot])(pane, x, y);

        JUTRect &bounds = pane->mCRect;
        if (i == 0) {
            SusamuneTimerScaleBasis(&pane->mScreenMtx[0][0], sDraw.percent);
            bounds.mX2 = bounds.mX1 + SusamuneTimerScaleCeil(
                bounds.mX2 - bounds.mX1, sDraw.percent);
            bounds.mY2 = bounds.mY1 + SusamuneTimerScaleCeil(
                bounds.mY2 - bounds.mY1, sDraw.percent);
        } else {
            bounds.mX1 = SusamuneTimerScaleFloor(bounds.mX1, sDraw.percent);
            bounds.mY1 = SusamuneTimerScaleFloor(bounds.mY1, sDraw.percent);
            bounds.mX2 = SusamuneTimerScaleCeil(bounds.mX2, sDraw.percent);
            bounds.mY2 = SusamuneTimerScaleCeil(bounds.mY2, sDraw.percent);
        }
        pane->mClipRect = bounds;
        return;
    }
}

bool collect(J2DPane *root) {
    sDraw.count = 1;
    sDraw.panes[0].pane = root;
    for (unsigned i = 0; i < sDraw.count; ++i) {
        J2DPane *pane = sDraw.panes[i].pane;
        void **vtable = *reinterpret_cast<void ***>(pane);
        if (vtable != retailPaneVtable && vtable != retailPictureVtable &&
            vtable != retailTextVtable) return false;
        for (JSUPtrLink *link = pane->mChildrenList.mFirst; link;
             link = link->mNextLink) {
            if (sDraw.count == kMaxPanes || !link->mItemPtr ||
                link->mParentList != &pane->mChildrenList) return false;
            J2DPane *child = static_cast<J2DPane *>(link->mItemPtr);
            for (unsigned j = 0; j < sDraw.count; ++j)
                if (sDraw.panes[j].pane == child) return false;
            sDraw.panes[sDraw.count++].pane = child;
        }
    }
    return true;
}

}  // namespace

bool beginDraw(J2DScreen *screen) {
    if (sActive) return false;
    const CreationStyle &style = gCreationExtras.nativeTimerStyle();
    const bool preview = gCreationExtras.editingNativeTimer();
    if (style.x > 1280 || style.y > 960 || style.scale < 50 || style.scale > 200 ||
        (!preview && !gCreationExtras.nativeTimerColorsEnabled() &&
         style.x == 640 && style.y == 480 && style.scale == 100 &&
         style.textA == 255 && style.textBrightness == 100) || !screen ||
        RetailInput::stageDirector() != gpMarDirector ||
        !gpMarDirector || !gpMarDirector->_260 ||
        !gpMarDirector->mGCConsole ||
        gpMarDirector->mGCConsole->mMainScreen != screen) return false;
    J2DPane *root = screen->search('\0t_0');
    if (!root || (!root->mIsVisible && !preview) || !collect(root)) return false;

    memcpy(sDraw.rootRect, &root->mRect, sizeof(sDraw.rootRect));
    sDraw.percent = style.scale;
    void **const originals[] = {
        retailPaneVtable, retailPictureVtable, retailTextVtable,
    };
    for (unsigned i = 0; i < 3; ++i) {
        memcpy(sDraw.drawVtables[i], originals[i], sizeof(sDraw.drawVtables[i]));
        sDraw.drawVtables[i][kMakeMatrixSlot] = reinterpret_cast<void *>(makeDrawMatrix);
    }
    for (unsigned i = 0; i < sDraw.count; ++i) {
        PaneState &saved = sDraw.panes[i];
        saved.vtable = *reinterpret_cast<void ***>(saved.pane);
        memcpy(saved.geometry, &saved.pane->mCRect, sizeof(saved.geometry));
        saved.alphaCopy = saved.pane->mAlphaCopy;
        saved.alpha = saved.pane->mAlpha;
        saved.visible = saved.pane->mIsVisible;
        if (saved.vtable == retailPictureVtable) {
            J2DPicture *picture = static_cast<J2DPicture *>(saved.pane);
            saved.white = picture->mColorMask;
            saved.black = picture->mColorOverlay;
            bool custom = false;
            const u8 *rgb = gCreationExtras.nativeTimerRgb(picture, &custom);
            if (rgb) {
                for (unsigned c = 0; c < 3; ++c) {
                    (&picture->mColorMask.r)[c] = rgb[c];
                    if (custom) (&picture->mColorOverlay.r)[c] = rgb[c];
                }
            }
            applyBrightness(picture->mColorMask, style.textBrightness);
            applyBrightness(picture->mColorOverlay, style.textBrightness);
            picture->mAlpha = (u8)((unsigned)picture->mAlpha * style.textA / 255);
        }
        if (preview) {
            const unsigned target = gCreationExtras.nativeTimerTarget();
            const bool countdown = target >= 7 && target <= 10;
            saved.pane->mIsVisible =
                saved.pane->mTag == '\0t_1' ? !countdown :
                saved.pane->mTag == '\0t_2' ? countdown : true;
            if (saved.pane->mTag == 't_tx' && target != 14)
                saved.pane->mIsVisible = gCreationExtras.timerLabelVisible();
        }
    }
    sActive = true;
    root->add(static_cast<int>(style.x) - 640, static_cast<int>(style.y) - 480);
    // Retail draw rebuilds matrices before clipping and walking children.
    // Lend each pane its original vtable with only that draw step wrapped.
    for (unsigned i = 0; i < sDraw.count; ++i) {
        PaneState &saved = sDraw.panes[i];
        const unsigned type = saved.vtable == retailPaneVtable ? 0 :
                              saved.vtable == retailPictureVtable ? 1 : 2;
        *reinterpret_cast<void ***>(saved.pane) = sDraw.drawVtables[type];
    }
    return true;
}

void endDraw() {
    if (!sActive) return;
    for (unsigned i = 0; i < sDraw.count; ++i) {
        PaneState &saved = sDraw.panes[i];
        *reinterpret_cast<void ***>(saved.pane) = saved.vtable;
        memcpy(&saved.pane->mCRect, saved.geometry, sizeof(saved.geometry));
        saved.pane->mAlphaCopy = saved.alphaCopy;
        saved.pane->mAlpha = saved.alpha;
        saved.pane->mIsVisible = saved.visible;
        if (saved.vtable == retailPictureVtable) {
            J2DPicture *picture = static_cast<J2DPicture *>(saved.pane);
            picture->mColorMask = saved.white;
            picture->mColorOverlay = saved.black;
        }
    }
    memcpy(&sDraw.panes[0].pane->mRect, sDraw.rootRect, sizeof(sDraw.rootRect));
    sActive = false;
}

}  // namespace NativeTimerLayout
