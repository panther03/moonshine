#include "susamune/wallkick_display.hxx"

#include "SMS/Player/Mario.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"

namespace WallkickDisplay {
namespace {

const int kPopupFrames = 15;

TMario *sMario;
u8 sWallFrames;
u8 sResult;
u8 sPopupFrames;
bool sWasWallslide;

void resetTracking() {
    sMario = gpMarioOriginal;
    sWallFrames = 0;
    sWasWallslide = false;
}

}  // namespace

void onStageSetup() {
    resetTracking();
    sResult = 0;
    sPopupFrames = 0;
}

void beforeDirect(bool active) {
    if (sPopupFrames) sPopupFrames--;
    if (!gSettings.getBool(SETTING_WALLKICK_DISPLAY)) {
        resetTracking();
        sPopupFrames = 0;
        return;
    }
    if (!active || !gpMarioOriginal) return;
    if (gpMarioOriginal != sMario) resetTracking();

    const bool wallslide = sMario->mState == TMario::STATE_WALLSLIDE;
    if (wallslide) {
        if (!sWasWallslide) sWallFrames = 1;
        else if (sWallFrames < 7) sWallFrames++;
    } else if (sMario->mState != TMario::STATE_WALLJUMP) {
        sWallFrames = 0;
    }
    sWasWallslide = wallslide;
}

void afterDirect(bool active) {
    if (!active || !sMario ||
        !gSettings.getBool(SETTING_WALLKICK_DISPLAY)) return;
    const bool walljump = sMario->mState == TMario::STATE_WALLJUMP &&
                          sMario->mPrevState == TMario::STATE_WALLSLIDE;
    const bool instantDive =
        (sMario->mState == TMario::STATE_DIVE ||
         sMario->mState == TMario::STATE_DIVEJUMP) &&
        sMario->mPrevState == TMario::STATE_WALLJUMP;
    const bool spin =
        (sMario->mState == TMario::STATE_JUMPSPIN ||
         sMario->mState == TMario::STATE_JUMPSPINR ||
         sMario->mState == TMario::STATE_JUMPSPINL) &&
        (sMario->mPrevState == TMario::STATE_WALLSLIDE ||
         sMario->mPrevState == TMario::STATE_WALLJUMP);
    const bool trackedWall = sWasWallslide ||
        (sWallFrames && sMario->mPrevState == TMario::STATE_WALLJUMP);
    if (trackedWall && (walljump || instantDive || spin)) {
        sResult = sWallFrames > 6 ? 7 : sWallFrames;
        sPopupFrames = kPopupFrames;
    }
    if (sMario->mState != TMario::STATE_WALLSLIDE) sWallFrames = 0;
}

void draw(Menu *menu) {
    if (!menu || !sPopupFrames || sResult == 0 ||
        !gSettings.getBool(SETTING_WALLKICK_DISPLAY)) return;
    const char *text = wallkickDisplayLabel(sResult - 1);
    gCreationExtras.drawWallkickDisplay(menu, text, sResult - 1);
}

}  // namespace WallkickDisplay
