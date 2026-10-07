#pragma once

#include <JSystem/JDrama/JDRViewObj.hxx>

class J2DSetScreen;

class TPauseMenu2 : public JDrama::TViewObj {
public:
    u8 getNextState();
    void setDrawEnd();

    enum State {
        MENU_APPEARING    = 0,
        MENU_OPEN         = 1,
        MENU_SAVING       = 3,
        MENU_DISAPPEARING = 4,
        MENU_CLOSED       = 5
    };

    State mState;          // 0x0010
    J2DSetScreen *mScreen; // 0x0014
};
