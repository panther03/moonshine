#pragma once

#include <JSystem/JDrama/JDRViewObj.hxx>
#include <JSystem/JGadget/List.hxx>
#include <SMS/Strategic/LiveActor.hxx>

struct TAlphaShadowQuad {};

struct TCircleShadowRequest {
    TVec3f mTranslation;
    f32 mOffsetY;   // ?
    f32 mOffsetY2;  // ?
    f32 mRotationY;
    f32 _18;
    u8 mShadowType;
    u8 mNeedsProjection;
    u16 mPadding;
    u32 mActorType;
};

static_assert(sizeof(TCircleShadowRequest) == 0x24, "retail shadow request ABI");

class TMBindShadowBody {};

class TMBindShadowManager : public JDrama::TViewObj {
public:
    TMBindShadowManager(char *);
    ~TMBindShadowManager() override;

    void load(JSUMemoryInputStream &in) override;
    void perform(u32 flags, JDrama::TGraphics *graphics) override;

    void calcVtx();
    void drawShadow(u32 flags, JDrama::TGraphics *graphics);
    void drawShadowGD(u32 flags, JDrama::TGraphics *graphics);
    void drawShadowVolume(bool, TAlphaShadowQuad *);
    void forceRequest(const TCircleShadowRequest &, u32);
    void request(const TCircleShadowRequest &, u32);

    u32 _10;
    u32 _14;
    u32 _18;
    u32 _1C;
    u32 _20;
    u32 _24;
    u32 _28;
    u32 _2C;
    u32 _30;
    u32 _34;
    u32 _38;
    u32 _3C;
    u32 _40;
    u32 _44;
    u8 _48;
    u8 _49;
    JGadget::TList<TMBindShadowBody *> mBodies;
    u8 _5C;
    u8 _5D;
    u8 _5E;
    u8 _5F;
    f32 _60;
    u8 _64;
    u8 _65;
    f32 _68;
    f32 _6C;
    u32 _70;
};

extern TMBindShadowManager *gpBindShadowManager;
