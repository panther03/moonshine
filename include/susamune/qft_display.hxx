#ifndef _SUSAMUNE_QFT_DISPLAY_HXX
#define _SUSAMUNE_QFT_DISPLAY_HXX

#include "susamune/creation.hxx"
#include "susamune/susamune_cfg.h"

class Menu;
class TMarioGamePad;

class QftDisplay {
public:
    void resetDefaults();
    void adopt(const volatile SusamuneQftDisplayCfg *src);
    void stageInto(volatile SusamuneQftDisplayCfg *dst) const;

    void beginOverlayFrame();
    void draw(Menu *menu, const char *text) const;
    void drawTasFallback(Menu *menu) const;
    bool hasAnchor(const char *text) const;
    bool adjacentStyle(const char *anchorText, const char *text,
                       CreationStyle *out) const;

    void beginEditor();
    void updateEditor(TMarioGamePad *pad);
    void drawEditor(Menu *menu) const;
    bool editing() const { return mEditor.editing(); }

    bool leadingZero() const { return mLeadingZero; }
    void toggleLeadingZero();

    bool dirty() const { return mDirty; }
    void clearDirty() { mDirty = false; }

private:
    static CreationStyle defaults();
    void clamp();

    CreationStyle  mStyle;
    u8             mTextRgb[SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS][3];
    u8             mBackupRgb[SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS][3];
    CreationEditor mEditor;
    bool           mDirty;
    bool           mDirtyBeforeEdit;
    bool           mLeadingZero;
};

extern QftDisplay &gQftDisplay;

#endif  // _SUSAMUNE_QFT_DISPLAY_HXX
