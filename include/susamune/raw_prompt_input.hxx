#ifndef _SUSAMUNE_RAW_PROMPT_INPUT_HXX
#define _SUSAMUNE_RAW_PROMPT_INPUT_HXX

#include <Dolphin/types.h>
#include <JSystem/JUtility/JUTGamePad.hxx>

// Frozen directors may consume TMarioGamePad edge fields before overlays run.
// Prompt buttons therefore use the unfiltered PADRead sample and require a
// complete release after opening before accepting a new edge.
class RawPromptInput {
public:
    RawPromptInput() : mMask(0), mPrevious(0), mReady(false) {}

    // Shared by modal entry points; keeping setup out of line avoids repeating
    // the same release gate throughout the menu and colour editors.
    __attribute__((noinline)) void begin(u16 mask) {
        mMask = mask;
        mPrevious = held();
        mReady = mPrevious == 0;
    }

    void clear() {
        mMask = 0;
        mPrevious = 0;
        mReady = false;
    }

    u16 update() {
        const u16 current = held();
        if (!mReady) {
            mPrevious = current;
            if (current == 0) mReady = true;
            return 0;
        }
        const u16 pressed = static_cast<u16>(current & ~mPrevious);
        mPrevious = current;
        return pressed;
    }

private:
    u16 held() const {
        return static_cast<u16>(JUTGamePad::mPadStatus[0].mButton & mMask);
    }

    u16  mMask;
    u16  mPrevious;
    bool mReady;
};

#endif  // _SUSAMUNE_RAW_PROMPT_INPUT_HXX
