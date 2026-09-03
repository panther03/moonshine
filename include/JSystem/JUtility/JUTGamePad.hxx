#pragma once

#include <Dolphin/OS.h>
#include <Dolphin/PAD.h>
#include <Dolphin/types.h>
#include <JSystem/JKernel/JKRDisposer.hxx>

enum EPadPort { Port1, Port2, Port3, Port4, PortRecorder };

class JUTGamePad : public JKRDisposer {
public:
    JUTGamePad(EPadPort port);
    virtual ~JUTGamePad();

    static bool recalibrate(u32);

    void assign();
    void checkResetSwitch();
    void clearForReset();
    void init();
    void initList();
    void read();
    void setButtonRepeat(u32, u32, u32);
    void update();

    enum EButtons {
        MAINSTICK_UP    = 0x8000000,
        MAINSTICK_DOWN  = 0x4000000,
        MAINSTICK_RIGHT = 0x2000000,
        MAINSTICK_LEFT  = 0x1000000,
        CSTICK_UP       = 0x80000,
        CSTICK_DOWN     = 0x40000,
        CSTICK_RIGHT    = 0x20000,
        CSTICK_LEFT     = 0x10000,
        START           = 0x1000,
        Y               = 0x800,
        X               = 0x400,
        B               = 0x200,
        A               = 0x100,
        L               = 0x40,
        R               = 0x20,
        Z               = 0x10,
        DPAD_UP         = 0x8,
        DPAD_DOWN       = 0x4,
        DPAD_RIGHT      = 0x2,
        DPAD_LEFT       = 0x1
    };

    enum EStickMode { NonClamped, Clamped };

    enum EWhichStick { WhichStick_ControlStick, WhichStick_CStick };

    class CButton {
    public:
        CButton();

        void clear();
        void update(const PADStatus *, u32);
        void setRepeat(u32, u32, u32);

        u32 mInput;  // _0
        u32 mFrameInput;
        u32 _8;
        u8 mAnalogA;   // _C
        u8 mAnalogB;   // _D
        u8 mTriggerL;  // _E
        u8 mTriggerR;  // _F
        f32 mAnalogL;  // _10
        f32 mAnalogR;  // _14
        u32 mRapidInput;
        u32 mFramesHeld;
        u32 _20;
        u32 _24;
        u32 _28;
        u32 _2C;
    };

    class CStick {
    public:
        CStick();

        void clear();
        u32 update(s8 x, s8 y, JUTGamePad::EStickMode, JUTGamePad::EWhichStick);
        u32 getButton();

        f32 mStickX;
        f32 mStickY;
        f32 mLengthFromNeutral;
        s16 mAngle;
    };

    class CRumble {
    public:
        void clear(JUTGamePad *);
        static void stopMotor(s32);
        static void stopMotorHard(s32);
        void update(u16);
        void setEnable(u32);

        u32 _0;
        u32 _4;
        u32 _8;
        u32 _C;
    };

    class C3ButtonReset {
    public:
        static s32 sResetPattern;
    };

    // The raw pad samples PADRead() fills in, one per port, before any of the
    // per-pad processing (button repeat, the game's "disable input" states,
    // stick-derived pseudo-buttons) happens. The mod's bind matching reads this
    // directly -- see binds.cpp.
    static PADStatus mPadStatus[4];
    static CButton mPadButton[4];
    static CStick mPadMStick[4];
    static CStick mPadSStick[4];

    CButton mButtons;      // _18
    CStick mControlStick;  // _48
    CStick mCStick;        // _58
    CRumble mRumble;       // _68
    u16 mPort;             // _78
    u8 mPortConnected;     // _7A
    JSUPtrLink mPtrLink;   // _7C
    u32 _8C;
    u32 _90;
    u32 _94;
    u32 _98;
    u8 mResetFlagJUT;   // _9C
    OSTick mResetTime;  // _A0
};
