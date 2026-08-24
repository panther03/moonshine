#include "susamune/movement_display.hxx"

#include "JSystem/JUtility/JUTGamePad.hxx"
#include "SMS/Player/Mario.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/menu.hxx"
#include "susamune/packed_text.hxx"
#include "susamune/settings.hxx"

namespace MovementDisplay {
namespace {

enum PopupKind {
    POPUP_NONE,
    POPUP_ROLLOUT,
    POPUP_DUST,
};

const int kPopupFrames = 15;
const char kRolloutNames[] = "1f\0" "2f\0" "3f\0" "4f\0" "5f";

TMario *sMario;
u32 sStateBefore;
u8 sGroundFrames;
u8 sRolloutFrames;
u8 sPopupResult;
u8 sPopupFrames;
u8 sPopupKind;
u8 sPendingResult;
u8 sPendingKind;
bool sGroundTracking;
bool sRolloutTracking;

bool rolloutEnabled() {
    return gSettings.getBool(SETTING_ROLLOUT_DISPLAY);
}

bool dustEnabled() {
    return gSettings.getBool(SETTING_DUST_DISPLAY);
}

void resetTracking(bool clearPopup) {
    sMario = gpMarioOriginal;
    sStateBefore = sMario ? sMario->mState : 0;
    sGroundFrames = 0;
    sRolloutFrames = 0;
    sGroundTracking = false;
    sRolloutTracking = false;
    if (clearPopup) {
        sPopupResult = 0;
        sPopupFrames = 0;
        sPopupKind = POPUP_NONE;
        sPendingResult = 0;
        sPendingKind = POPUP_NONE;
    }
}

void showPopup(u8 kind, u8 result) {
    if (sPopupFrames && sPopupKind != kind) {
        sPendingKind = kind;
        sPendingResult = result;
        return;
    }
    sPopupKind = kind;
    sPopupResult = result;
    sPopupFrames = kPopupFrames;
}

void tickPopup() {
    if (!sPopupFrames || --sPopupFrames) return;
    sPopupKind = sPendingKind;
    sPopupResult = sPendingResult;
    sPendingKind = POPUP_NONE;
    sPendingResult = 0;
    if (sPopupKind != POPUP_NONE) sPopupFrames = kPopupFrames;
}

void finishRollout() {
    if (!sRolloutTracking) return;
    sRolloutTracking = false;
    if (rolloutEnabled() && sRolloutFrames)
        showPopup(POPUP_ROLLOUT, sRolloutFrames);
}

bool rawAHeld() {
    return (JUTGamePad::mPadStatus[0].mButton & JUTGamePad::A) != 0;
}

}  // namespace

void onStageSetup() { resetTracking(true); }

void onSavestateLoaded() { resetTracking(true); }

void beforeDirect(bool active) {
    tickPopup();
    if (!rolloutEnabled() && !dustEnabled()) {
        resetTracking(true);
        return;
    }
    if (!active || !gpMarioOriginal) return;
    if (gpMarioOriginal != sMario) resetTracking(true);

    if (sGroundTracking && sMario->mState == TMario::STATE_DIVESLIDE &&
        sGroundFrames < 7) {
        sGroundFrames++;
    }

    if (sRolloutTracking) {
        if (sMario->mState != TMario::STATE_DIVEJUMP || !rawAHeld()) {
            finishRollout();
        } else if (sRolloutFrames < 5) {
            sRolloutFrames++;
            if (sRolloutFrames == 5) finishRollout();
        }
    }
    sStateBefore = sMario->mState;
}

void afterDirect(bool active) {
    if (!active || !gpMarioOriginal ||
        (!rolloutEnabled() && !dustEnabled())) return;
    if (gpMarioOriginal != sMario) resetTracking(true);

    const u32 state = sMario->mState;
    const u32 previous = sMario->mPrevState;
    const bool landed = sStateBefore != TMario::STATE_DIVESLIDE &&
                        previous == TMario::STATE_DIVE &&
                        state == TMario::STATE_DIVESLIDE;
    if (landed) {
        sGroundFrames = 0;
        sGroundTracking = true;
    }

    const bool rollout = sStateBefore != TMario::STATE_DIVEJUMP &&
                         previous == TMario::STATE_DIVESLIDE &&
                         state == TMario::STATE_DIVEJUMP;
    if (rollout) {
        const bool landedThisDirect = sStateBefore == TMario::STATE_DIVE;
        if (dustEnabled() && (sGroundTracking || landedThisDirect)) {
            const u8 frames = landedThisDirect ? 1 : sGroundFrames;
            const u8 result = frames > 6 ? 7 : frames;
            showPopup(POPUP_DUST, result);
        }
        sGroundTracking = false;
        if (rolloutEnabled()) {
            sRolloutFrames = 1;
            sRolloutTracking = true;
        }
    } else if (sGroundTracking && state != TMario::STATE_DIVESLIDE) {
        sGroundTracking = false;
    }

    if (sRolloutTracking && state != TMario::STATE_DIVEJUMP) {
        finishRollout();
    }
}

void draw(Menu *menu) {
    if (!menu || !sPopupFrames || !sPopupResult) return;
    const char *text = nullptr;
    if (sPopupKind == POPUP_ROLLOUT && rolloutEnabled()) {
        text = PackedText::at(kRolloutNames, sPopupResult - 1);
    } else if (sPopupKind == POPUP_DUST && dustEnabled()) {
        text = wallkickDisplayLabel(sPopupResult - 1);
    }
    if (text)
        gCreationExtras.drawWallkickDisplay(menu, text, sPopupResult - 1);
}

}  // namespace MovementDisplay
