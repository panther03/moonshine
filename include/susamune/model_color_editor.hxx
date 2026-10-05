#ifndef _SUSAMUNE_MODEL_COLOR_EDITOR_HXX
#define _SUSAMUNE_MODEL_COLOR_EDITOR_HXX

#include "susamune/creation.hxx"
#include "susamune/raw_prompt_input.hxx"
#include "SMS/Player/MarioGamePad.hxx"

// Internal UI state only. Each owner keeps its colour arrays and its existing
// wire-format serializer; the shared prefix does not enter settings or states.
struct ModelColorEditor {
    CreationStyle style;
    CreationEditor editor;
    RawPromptInput input;
    u16 enabled;
    u16 enabledBefore;
    bool dirty;
    bool dirtyBefore;
    bool releaseGuard;

    static constexpr u16 promptButtons = JUTGamePad::A | JUTGamePad::B |
        JUTGamePad::Z | JUTGamePad::START;
    static constexpr CreationStyle defaults = {320, 420, 100, 255, 0, 0, 0, 0, 100, 255};
    static constexpr u8 white[1][3] = {{255, 255, 255}};

    __attribute__((noinline, section(".foxtrot.text")))
    void reset() {
        enabled = enabledBefore = 0;
        editor.reset();
        input.clear();
        dirty = dirtyBefore = releaseGuard = false;
    }

    __attribute__((noinline, section(".foxtrot.text")))
    void begin(u8 (*colors)[3], u8 (*backup)[3], u16 count, const char *names) {
        if (editor.editing() || releaseGuard) return;
        dirtyBefore = dirty;
        dirty = false;
        enabledBefore = enabled;
        style = defaults;
        editor.begin(&style, colors, backup, count, count, names,
            CreationEditor::CAP_TEXT_COLOR | CreationEditor::CAP_COLOR_MODE |
            CreationEditor::CAP_RGB_ENABLES_CUSTOM, &enabled);
        input.begin(promptButtons);
    }

    __attribute__((noinline, section(".foxtrot.text")))
    void update(TMarioGamePad *pad) {
        if (!pad) return;
        if (releaseGuard) {
            if (!(JUTGamePad::mPadStatus[0].mButton & promptButtons))
                releaseGuard = false;
            return;
        }
        if (!editor.editing()) return;
        const u32 original = pad->mButtons.mRapidInput;
        pad->mButtons.mRapidInput = (original & ~promptButtons) | input.update();
        const u8 result = editor.update(pad, defaults, white);
        pad->mButtons.mRapidInput = original;
        if (result & CreationEditor::UPDATE_CHANGED) dirty = true;
        if (result & CreationEditor::UPDATE_CANCELLED) dirty = dirtyBefore;
        else if (result & CreationEditor::UPDATE_FINISHED) dirty |= dirtyBefore;
        // Keep the final confirmation press away from the menu underneath.
        if (result & CreationEditor::UPDATE_FINISHED) releaseGuard = true;
    }
};

#endif
