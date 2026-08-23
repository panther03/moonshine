// =====================================================================
// attempt_counter.cpp
//
// Native port of sup39's Attempt Counter, In-Stage Attempt Counter and
// Manual Attempt Counter. The display and default controls intentionally
// stay familiar to runners who learned the Gecko versions.
// =====================================================================

#include "susamune/attempt_counter.hxx"
#include "susamune/mem2_map.h"

#include "Dolphin/printf.h"
#include "JSystem/JUtility/JUTColor.hxx"
#include "SMS/MoveBG/Shine.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/binds.hxx"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"

namespace {

const u16 kDisplayDuration = 60;
const int kDisplayX        = 152;
const int kDisplayY        = 125;
const int kFontSize        = 32;
const int kLineGap         = 2;
const int kPadding         = 4;

volatile u32 *shineSerial() {
    return reinterpret_cast<volatile u32 *>(
        SUSAMUNE_ADDR_ATTEMPT_SHINE_SERIAL);
}

volatile u32 *departureSerial() {
    return reinterpret_cast<volatile u32 *>(
        SUSAMUNE_ADDR_ATTEMPT_DEPARTURE_SERIAL);
}

volatile u32 *lastShineId() {
    return reinterpret_cast<volatile u32 *>(SUSAMUNE_ADDR_LAST_SHINE_ID);
}

u16 packedArea(u8 area, u8 episode) {
    return (u16)(((u16)area << 8) | episode);
}

u16 currentArea() {
    return packedArea(gpApplication.mCurrentScene.mAreaID,
                      gpApplication.mCurrentScene.mEpisodeID);
}

}  // namespace

AttemptCounter &gAttemptCounter = *reinterpret_cast<AttemptCounter *>(
    SUSAMUNE_MEM2_CONFIG_RUNTIME_PPC_BASE +
    SUSAMUNE_CONFIG_ATTEMPTS_OFFSET);
static_assert(sizeof(AttemptCounter) <= SUSAMUNE_CONFIG_ATTEMPTS_SIZE,
              "attempt counter exceeds its MEM2 runtime slot");

// Replaces TMario::winDemo's call to fireGetStar. Publishing through a fixed
// serial lets the normal call and No Shine Get Animation's asm cave feed one
// counter without hooking fireGetStar itself (where QFT/Gecko hooks may live).
extern "C" void onFireGetStar(TMarDirector *director, TShine *shine) {
    // mMapObjID is the decomp's mEventId at TShine+0x134.
    *lastShineId() = shine->mMapObjID;
    *shineSerial() = *shineSerial() + 1;
    director->fireGetStar(shine);
}

void AttemptCounter::init() {
    mDirector            = 0;
    mLastShineSerial     = 0;
    mLastDepartureSerial = 0;
    mSuccessCount        = 0;
    mAttemptCount        = 0;
    mPreviousArea        = 0;
    mDisplayFrames       = 0;
    mHaveArea            = false;
    mGotShine            = false;
    mWasEnabled          = false;
    *shineSerial()       = 0;
    *departureSerial()   = 0;
    *lastShineId()       = 0xFFFFFFFFu;
}

void AttemptCounter::show() { mDisplayFrames = kDisplayDuration; }

void AttemptCounter::addAttempt() {
    if (mAttemptCount != 0xFFFFu) {
        mAttemptCount++;
    }
    show();
}

void AttemptCounter::addSuccess() {
    if (mSuccessCount != 0xFFFFu) {
        mSuccessCount++;
    }
    show();
}

void AttemptCounter::onStageSetup(TMarDirector *director) {
    mDirector            = director;
    mLastShineSerial     = *shineSerial();
    mLastDepartureSerial = *departureSerial();
    mGotShine            = false;

    if (!gSettings.getBool(SETTING_ATTEMPT_COUNTER)) {
        mWasEnabled = false;
        return;
    }

    const u16 area = packedArea(director->mAreaID, director->mEpisodeID);
    if (mHaveArea && area == mPreviousArea) {
        addAttempt();
    } else {
        // The original Gecko code writes the packed pair 0x00000001 here:
        // a different area starts a fresh counter at 0 successes / 1 attempt.
        // This also discards the old area's departure result when entering a
        // newly selected episode from the plaza.
        mSuccessCount = 0;
        mAttemptCount = 1;
        show();
    }
    mPreviousArea = area;
    mHaveArea     = true;
    mWasEnabled   = true;
}

void AttemptCounter::update(bool observerFrame) {
    const bool enabled    = gSettings.getBool(SETTING_ATTEMPT_COUNTER);
    const u32 serial      = *shineSerial();
    const u32 departures = *departureSerial();
    const bool stageActive = mDirector && gpMarDirector == mDirector;

    if (observerFrame) {
        mLastShineSerial = serial;
        mLastDepartureSerial = departures;
        mGotShine = false;
        mDisplayFrames = 0;
        return;
    }

    if (!enabled) {
        mWasEnabled          = false;
        mLastShineSerial     = serial;
        mLastDepartureSerial = departures;
        mDisplayFrames       = 0;
        return;
    }

    // Enabling in the middle of a stage starts a fresh session with that
    // stage as the baseline. It must not consume a shine event from before the
    // setting was switched on.
    if (!mWasEnabled) {
        mSuccessCount    = 0;
        mAttemptCount    = stageActive ? 1 : 0;
        mPreviousArea    = stageActive
                         ? packedArea(mDirector->mAreaID, mDirector->mEpisodeID)
                         : currentArea();
        mHaveArea        = stageActive;
        mGotShine        = false;
        mLastShineSerial = serial;
        mLastDepartureSerial = departures;
        mWasEnabled      = true;
        show();
    }

    // A serial (rather than a bool) cannot lose a second event should two
    // callbacks occur before this once-per-frame poll.
    u32 successEvents = serial - mLastShineSerial;
    mLastShineSerial = serial;
    if (successEvents != 0) {
        mGotShine = true;
    }

    // This serial is published at moveStage+0x3c, before the application
    // advances its scene fields. That timing is important: comparing after
    // direct() falsely identifies entering a selected episode as a success.
    const u32 departureEvents = departures - mLastDepartureSerial;
    mLastDepartureSerial = departures;
    if (departureEvents != 0 && !mGotShine) {
        successEvents = departureEvents;
    }
    if (successEvents != 0) {
        const u32 room = 0xFFFFu - mSuccessCount;
        mSuccessCount = (u16)(mSuccessCount +
                              (successEvents < room ? successEvents : room));
        show();
    }

    // The original optional in-stage code uses bare D-pad Left/Right. Keep it
    // separately gated because those are Susamune's default savestate binds.
    if (gSettings.getBool(SETTING_ATTEMPT_IN_STAGE_CONTROLS)) {
        if (gBinds.wasPressed(BIND_ATTEMPT_SHOW)) {
            show();
        }
        if (gBinds.wasPressed(BIND_ATTEMPT_ADD)) {
            addAttempt();
        }
    }

    // Manual controls remain available whenever the counter is enabled.
    if (gBinds.wasPressed(BIND_ATTEMPT_DEC)) {
        if (mAttemptCount != 0) mAttemptCount--;
        show();
    }
    if (gBinds.wasPressed(BIND_ATTEMPT_INC)) {
        addAttempt();
    }
    if (gBinds.wasPressed(BIND_SUCCESS_DEC)) {
        if (mSuccessCount != 0) mSuccessCount--;
        show();
    }
    if (gBinds.wasPressed(BIND_SUCCESS_INC)) {
        addSuccess();
    }

    if (mDisplayFrames != 0) {
        mDisplayFrames--;
    }
}

void AttemptCounter::draw(Menu *menu) const {
    if (!menu || !mWasEnabled || mDisplayFrames == 0 ||
        !mDirector || gpMarDirector != mDirector) {
        return;
    }

    char success[8];
    char attempts[8];
    snprintf(success, sizeof(success), "%u", (unsigned)mSuccessCount);
    snprintf(attempts, sizeof(attempts), "%u", (unsigned)mAttemptCount);

    int successW = Menu::textWidth(success, kFontSize);
    int attemptW = Menu::textWidth(attempts, kFontSize);
    int width    = successW > attemptW ? successW : attemptW;
    int height   = kFontSize * 2 + kLineGap;

    menu->fillBox(kDisplayX - kPadding, kDisplayY - kPadding,
                  width + kPadding * 2, height + kPadding * 2,
                  JUtility::TColor(0, 0, 0, 64));
    menu->drawText(success, kDisplayX, kDisplayY, kFontSize, kFontSize,
                   JUtility::TColor(255, 255, 153, 255));
    menu->drawText(attempts, kDisplayX, kDisplayY + kFontSize + kLineGap,
                   kFontSize, kFontSize,
                   JUtility::TColor(255, 255, 153, 255));
}
