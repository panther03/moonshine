#include "susamune/mario_colors.hxx"
#include "susamune/mario_color_texture.hxx"
#include "susamune/retail_input.hxx"

#include "Dolphin/GX.h"
#include "Dolphin/mem.h"
#include "Dolphin/string.h"
#include "JSystem/J3D/J3DModel.hxx"
#include "JSystem/JUtility/JUTTexture.hxx"
#include "SMS/M3DUtil/M3UModel.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/MarioCap.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"

namespace MarioColors {
namespace {

typedef void (*PacketCallback)(J3DShapePacket *, int);
struct PacketState {
    J3DShapePacket *packet;
    PacketCallback original;
    u8 parts;
    u8 glasses;
};
struct DrawState {
    TMarDirector *director;
    TMario *mario;
    const ResTIMG *source;
    GXTexObj originalTexture;
    GXTexObj customTexture;
    PacketState packets[16];
    u8 colors[PART_COUNT][3];
    u8 enabled;
    u8 packetCount;
    bool textureReady;
};
DrawState sDraw;
alignas(32) u8 sAtlas[MarioColorTexture::kAtlasBytes];

static_assert(__builtin_offsetof(TMario, mModelData) == 0x3A8 &&
              __builtin_offsetof(TMario, mCap) == 0x3E0,
              "retail Mario model owners moved");
static_assert(__builtin_offsetof(TMarioCap, mCap1) == 0x10 &&
              __builtin_offsetof(TMarioCap, maGlass1) == 0x1C,
              "retail cap model offsets moved");
static_assert(__builtin_offsetof(J3DModel, mShapePackets) == 0x84,
              "retail shape packet array moved");

__attribute__((noinline)) bool mem1(const void *pointer, u32 size) {
    const u32 address = reinterpret_cast<u32>(pointer);
    return address >= 0x80000000u && address < 0x81800000u &&
           size <= 0x81800000u - address;
}

bool live() {
    return gpMarDirector && RetailInput::stageDirector() == gpMarDirector &&
        gpMarDirector == sDraw.director &&
        gpMarDirector->_260 &&
        gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING &&
        gpMarioAddress && gpMarioAddress == sDraw.mario;
}

const ResTIMG *mainTexture(J3DModelData *data) {
    if (!mem1(data, sizeof(*data)) || !mem1(data->_AC, 8)) return nullptr;
    // Retail J3DTexture is an eight-byte non-polymorphic pair.
    const u8 *texture = reinterpret_cast<const u8 *>(data->_AC);
    const u16 count = *reinterpret_cast<const u16 *>(texture);
    const ResTIMG *images = *reinterpret_cast<const ResTIMG *const *>(texture + 4);
    if (!count || count > 256 || !mem1(images, count * sizeof(*images)))
        return nullptr;
    const ResTIMG &image = images[0];
    const u8 *pixels = reinterpret_cast<const u8 *>(&image) + image.mTextureOffset;
    if (image.mFormat != ResTIMG::CMPR || image.mWidth != 256 ||
        image.mHeight != 256 || image.mMipMaps != 1 ||
        !mem1(pixels, MarioColorTexture::kAtlasBytes)) return nullptr;
    return &image;
}

void initTexture(GXTexObj &object, const ResTIMG &image, const void *pixels) {
    GXInitTexObj(&object, const_cast<void *>(pixels), 256, 256, GX_TF_CMPR,
                 image.mWrapSMode, image.mWrapTMode, GX_FALSE);
    GXInitTexObjLOD(&object, image.mFilterMinMode, image.mFilterMagMode,
                    0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
}

PacketCallback &callback(J3DShapePacket *packet) {
    return *reinterpret_cast<PacketCallback *>(reinterpret_cast<u8 *>(packet) + 0x10);
}

void drawPacket(J3DShapePacket *packet, int phase) {
    for (u32 i = 0; i < sDraw.packetCount; ++i) {
        const PacketState &state = sDraw.packets[i];
        if (state.packet != packet) continue;
        // Keep retail fog, dirt and its callback user area intact.
        if (state.original) state.original(packet, phase);
        if (!live() || !(state.parts & sDraw.enabled)) return;
        if (state.glasses) {
            if (phase == 0) {
                const u8 *color = sDraw.colors[SUNGLASSES];
                GXColorS10 shade = {static_cast<s16>(color[0]),
                    static_cast<s16>(color[1]), static_cast<s16>(color[2]), 255};
                GXSetTevColorS10(GX_TEVREG2, shade);
                if (state.glasses == 1) {
                    // Preserve the frame highlights while replacing its dark palette.
                    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_C2, GX_CC_ONE,
                                    GX_CC_TEXC, GX_CC_ZERO);
                    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV,
                                    GX_CC_ONE, GX_CC_ZERO);
                } else {
                    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_C2,
                                    GX_CC_ONE, GX_CC_ZERO);
                    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO,
                                   GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
                }
            } else {
                GXColorS10 white = {255, 255, 255, 255};
                GXSetTevColorS10(GX_TEVREG2, white);
                if (state.glasses == 1) {
                    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_C1,
                                    GX_CC_TEXC, GX_CC_RASC);
                    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV,
                                    GX_CC_RASC, GX_CC_ZERO);
                } else {
                    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV,
                                    GX_CC_KONST, GX_CC_RASC);
                    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO,
                                   GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
                }
            }
        } else if (sDraw.textureReady) {
            GXLoadTexObj(phase == 0 ? &sDraw.customTexture : &sDraw.originalTexture,
                         GX_TEXMAP0);
        }
        return;
    }
}

bool supportedModel(J3DModel *model, u16 shapes) {
    return mem1(model, sizeof(*model)) &&
        mem1(model->mModelData, sizeof(J3DModelData)) &&
        model->mModelData->mShapeNum == shapes &&
        mem1(model->mShapePackets, shapes * 0x34u);
}

void install(J3DModel *model, u16 shape, u8 parts, u8 glasses = 0) {
    J3DShapePacket *packet = reinterpret_cast<J3DShapePacket *>(
        reinterpret_cast<u8 *>(model->mShapePackets) + shape * 0x34u);
    for (u32 i = 0; i < sDraw.packetCount; ++i) {
        PacketState &state = sDraw.packets[i];
        if (state.packet != packet) continue;
        if (callback(packet) != drawPacket) state.original = callback(packet);
        callback(packet) = drawPacket;
        return;
    }
    if (sDraw.packetCount >= 16 || callback(packet) == drawPacket) return;
    PacketState &state = sDraw.packets[sDraw.packetCount++];
    state = {packet, callback(packet), parts, glasses};
    callback(packet) = drawPacket;
}

} // namespace

void onStageSetup() {
    memset(&sDraw, 0, sizeof(sDraw));
    sDraw.director = gpMarDirector;
    sDraw.mario = gpMarioAddress;
}

bool preserveSavestateBindings(bool (*keep)(const void *word)) {
    if (!keep || !live() || !sDraw.packetCount || sDraw.packetCount > 16) return false;
    for (u32 i = 0; i < sDraw.packetCount; ++i) {
        J3DShapePacket *packet = sDraw.packets[i].packet;
        if (!mem1(packet, 0x34) || callback(packet) != drawPacket ||
            !keep(&callback(packet))) return false;
    }
    return true;
}

void update() {
    if (!live() || !mem1(gpMarioAddress->mModelData, 12)) return;
    J3DModel *body = gpMarioAddress->mModelData->mModel;
    if (!supportedModel(body, 11)) return;
    const ResTIMG *texture = mainTexture(body->mModelData);
    if (!texture) return;
    u8 mask = 0;
    bool changed = !sDraw.textureReady || sDraw.source != texture;
    for (u32 part = 0; part < PART_COUNT; ++part) {
        if (enabled(part)) mask |= 1u << part;
        for (u32 channel = 0; channel < 3; ++channel) {
            const u8 value = rgb(part)[channel];
            changed |= value != sDraw.colors[part][channel];
            sDraw.colors[part][channel] = value;
        }
    }
    changed |= mask != sDraw.enabled;
    sDraw.enabled = mask;
    if (changed && mask) {
        const u8 *pixels = reinterpret_cast<const u8 *>(texture) + texture->mTextureOffset;
        MarioColorTexture::recolor(pixels, sAtlas, sDraw.colors, mask);
        DCStoreRange(sAtlas, sizeof(sAtlas));
        GXInvalidateTexAll();
        initTexture(sDraw.originalTexture, *texture, pixels);
        initTexture(sDraw.customTexture, *texture, sAtlas);
        sDraw.source = texture;
        sDraw.textureReady = true;
    }
    // Register originals even when disabled: a loaded state may contain our wrapper.
    install(body, 4, 1u << CAP);
    install(body, 5, 1u << GLOVES);
    install(body, 6, 1u << GLOVES);
    install(body, 7, 1u << SHOES);
    install(body, 8, 1u << SHOES);
    install(body, 9, (1u << SHIRT) | (1u << OVERALLS));
    install(body, 10, 1u << SUNSHINE_SHIRT);
    J3DModel *hands[] = {gpMarioAddress->mHandModel2R, gpMarioAddress->mHandModel2L,
        gpMarioAddress->mHandModel3R, gpMarioAddress->mHandModel3L, gpMarioAddress->mHandModel4R};
    for (u32 i = 0; i < 5; ++i)
        if (supportedModel(hands[i], 1))
            install(hands[i], 0, (1u << GLOVES) | (i == 4 ? 1u << CAP : 0u));
    if (mem1(gpMarioAddress->mCap, 0x20)) {
        if (supportedModel(gpMarioAddress->mCap->mCap1, 1))
            install(gpMarioAddress->mCap->mCap1, 0, 1u << CAP);
        J3DModel *glasses = gpMarioAddress->mCap->maGlass1;
        if (supportedModel(glasses, 2)) {
            install(glasses, 0, 1u << SUNGLASSES, 1);
            install(glasses, 1, 1u << SUNGLASSES, 2);
        }
    }
}

} // namespace MarioColors
