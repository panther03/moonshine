#ifndef _SUSAMUNE_MODEL_COLOR_DRAW_HXX
#define _SUSAMUNE_MODEL_COLOR_DRAW_HXX

#include "susamune/retail_input.hxx"
#include "Dolphin/GX.h"
#include "JSystem/J3D/J3DModel.hxx"
#include "JSystem/JUtility/JUTTexture.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/System/MarDirector.hxx"

namespace ModelColorDraw {

__attribute__((noinline, section(".foxtrot.text")))
inline bool mem1(const void *pointer, u32 size) {
    const u32 address = reinterpret_cast<u32>(pointer);
    return address >= 0x80000000u && address < 0x81800000u &&
           size <= 0x81800000u - address;
}

__attribute__((noinline, section(".foxtrot.text")))
inline bool live(TMarDirector *director, TMario *mario) {
    return gpMarDirector && RetailInput::stageDirector() == gpMarDirector &&
        gpMarDirector == director && gpMarDirector->_260 &&
        gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING &&
        gpMarioAddress && gpMarioAddress == mario;
}

__attribute__((noinline, section(".foxtrot.text")))
inline bool supportedModel(J3DModel *model, u16 shapes) {
    return mem1(model, sizeof(*model)) &&
        mem1(model->mModelData, sizeof(J3DModelData)) &&
        model->mModelData->mShapeNum == shapes &&
        mem1(model->mShapePackets, shapes * 0x34u);
}

__attribute__((noinline, section(".foxtrot.text")))
inline void initTexture(GXTexObj &object, const ResTIMG &image,
                        const void *pixels, u16 width, u16 height) {
    GXInitTexObj(&object, const_cast<void *>(pixels), width, height, GX_TF_CMPR,
                 image.mWrapSMode, image.mWrapTMode, GX_FALSE);
    GXInitTexObjLOD(&object, image.mFilterMinMode, image.mFilterMagMode,
                    0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
}

} // namespace ModelColorDraw

#endif
