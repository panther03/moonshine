#pragma once

#include <SMS/MapObj/MapObjBase.hxx>

class THideObjBase : public TMapObjBase {
public:
    TMapObjBase *mHiddenObj;  // 0x0138
    f32 mAppearSpeed;         // 0x013C
    f32 mAppearYSpeed;        // 0x0140
    char *mHiddenShineDemoName;  // 0x0144
    s32 _148;
    bool mAllowReveal;  // 0x014C
};
static_assert(sizeof(THideObjBase) == 0x150, "THideObjBase layout changed");
