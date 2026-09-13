#include "susamune/fludd_colors.hxx"
#include "susamune/fludd_color_texture.hxx"
#include "susamune/retail_input.hxx"
#include "Dolphin/GX.h"
#include "Dolphin/mem.h"
#include "JSystem/J3D/J3DModel.hxx"
#include "JSystem/JUtility/JUTTexture.hxx"
#include "SMS/M3DUtil/MActor.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/Watergun.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/MarDirector.hxx"

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"

namespace FluddColors {
namespace {
typedef void (*PacketCallback)(J3DShapePacket *, int);
struct Packet {
    J3DShapePacket *packet;
    PacketCallback original;
    u8 texture;
    u8 tank;
};
struct State {
    TMarDirector *director;
    TMario *mario;
    const ResTIMG *source;
    GXTexObj original;
    GXTexObj textures[5];
    Packet packets[12];
    u8 colors[PART_COUNT][3];
    u16 enabled;
    u8 count;
    bool ready;
} sDraw;
alignas(32) u8 sAtlases[5][FluddColorTexture::kAtlasBytes];
const u8 kPaintParts[5] = {PAINT, SPRAY_NOZZLE, HOVER_NOZZLE, ROCKET_NOZZLE, TURBO_NOZZLE};
static_assert(__builtin_offsetof(TMario, mFludd) == 0x3E4 &&
              __builtin_offsetof(MActor, mModel) == 4,
              "retail FLUDD model owners moved");

__attribute__((noinline)) bool mem1(const void *p, u32 size) {
    const u32 address = reinterpret_cast<u32>(p);
    return address >= 0x80000000u && address < 0x81800000u &&
           size <= 0x81800000u - address;
}
bool live() {
    return gpMarDirector && RetailInput::stageDirector() == gpMarDirector &&
        gpMarDirector == sDraw.director && gpMarDirector->_260 &&
        gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING &&
        gpMarioAddress && gpMarioAddress == sDraw.mario;
}
J3DModel *modelAt(const void *owner, u32 offset, u16 shapes) {
    if (!mem1(owner, offset + 4)) return nullptr;
    MActor *actor = *reinterpret_cast<MActor *const *>(
        reinterpret_cast<const u8 *>(owner) + offset);
    if (!mem1(actor, sizeof(MActor))) return nullptr;
    J3DModel *model = actor->mModel;
    if (!mem1(model, sizeof(*model)) || !mem1(model->mModelData, sizeof(J3DModelData)) ||
        model->mModelData->mShapeNum != shapes ||
        !mem1(model->mShapePackets, shapes * 0x34u)) return nullptr;
    return model;
}
const ResTIMG *mainTexture(J3DModelData *data) {
    if (!mem1(data->_AC, 8)) return nullptr;
    const u8 *table = reinterpret_cast<const u8 *>(data->_AC);
    const u16 count = *reinterpret_cast<const u16 *>(table);
    const ResTIMG *images = *reinterpret_cast<const ResTIMG *const *>(table + 4);
    if (count != 12 || !mem1(images, count * sizeof(*images))) return nullptr;
    const ResTIMG &image = images[1];
    const u32 pixels = reinterpret_cast<u32>(&image) + image.mTextureOffset;
    if (image.mFormat != ResTIMG::CMPR || image.mWidth != 64 ||
        image.mHeight != 128 || image.mMipMaps != 1 ||
        !mem1(reinterpret_cast<void *>(pixels), FluddColorTexture::kAtlasBytes)) return nullptr;
    return &image;
}
void initTexture(GXTexObj &object, const ResTIMG &image, void *pixels) {
    GXInitTexObj(&object, pixels, 64, 128, GX_TF_CMPR,
                 image.mWrapSMode, image.mWrapTMode, GX_FALSE);
    GXInitTexObjLOD(&object, image.mFilterMinMode, image.mFilterMagMode,
                    0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
}
PacketCallback &callback(J3DShapePacket *packet) {
    return *reinterpret_cast<PacketCallback *>(reinterpret_cast<u8 *>(packet) + 0x10);
}
void drawPacket(J3DShapePacket *packet, int phase) {
    for (u32 i = 0; i < sDraw.count; ++i) {
        const Packet &state = sDraw.packets[i];
        if (state.packet != packet) continue;
        if (state.original) state.original(packet, phase);
        if (!live()) return;
        if (state.tank) {
            if (!(sDraw.enabled & (1u << TANK))) return;
            const u8 *rgb = sDraw.colors[TANK];
            GXColorS10 color = {255, 255, 255, 255};
            if (!phase) {
                color.r = rgb[0]; color.g = rgb[1]; color.b = rgb[2];
                GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_C2,
                                GX_CC_ONE, GX_CC_ZERO);
                GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO,
                               GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            } else {
                GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO,
                    state.tank == 1 ? GX_CC_CPREV : GX_CC_TEXC,
                    state.tank == 1 ? GX_CC_KONST : GX_CC_RASC,
                    state.tank == 1 ? GX_CC_RASC : GX_CC_ZERO);
                GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO,
                               GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
            }
            GXSetTevColorS10(GX_TEVREG2, color);
        } else if (sDraw.ready && (sDraw.enabled &
            ((1u << kPaintParts[state.texture]) | (1u << METAL) | (1u << STRAPS) | (1u << TANK)))) {
            GXLoadTexObj(phase ? &sDraw.original : &sDraw.textures[state.texture], GX_TEXMAP0);
        }
        return;
    }
}
void install(J3DModel *model, u32 shape, u8 texture, u8 tank = 0) {
    J3DShapePacket *packet = reinterpret_cast<J3DShapePacket *>(
        reinterpret_cast<u8 *>(model->mShapePackets) + shape * 0x34u);
    for (u32 i = 0; i < sDraw.count; ++i) {
        Packet &state = sDraw.packets[i];
        if (state.packet != packet) continue;
        if (callback(packet) != drawPacket) state.original = callback(packet);
        callback(packet) = drawPacket;
        return;
    }
    if (sDraw.count == 12 || callback(packet) == drawPacket) return;
    sDraw.packets[sDraw.count++] = {packet, callback(packet), texture, tank};
    callback(packet) = drawPacket;
}
} // namespace

void onStageSetup() {
    memset(&sDraw, 0, sizeof(sDraw));
    sDraw.director = gpMarDirector;
    sDraw.mario = gpMarioAddress;
}
bool preserveSavestateBindings(bool (*keep)(const void *word)) {
    if (!keep || !live() || !sDraw.count || sDraw.count > 12) return false;
    for (u32 i = 0; i < sDraw.count; ++i) {
        J3DShapePacket *packet = sDraw.packets[i].packet;
        if (!mem1(packet, 0x34) || callback(packet) != drawPacket ||
            !keep(&callback(packet))) return false;
    }
    return true;
}
void update() {
    if (!live()) return;
    TWaterGun *gun = gpMarioAddress->mFludd;
    J3DModel *body = modelAt(gun, 0x1CD4, 6);
    if (!body) return;
    const ResTIMG *texture = mainTexture(body->mModelData);
    if (!texture) return;
    bool changed = !sDraw.ready || sDraw.source != texture;
    u16 mask = 0;
    for (u32 part = 0; part <= TURBO_NOZZLE; ++part) {
        if (enabled(part)) mask |= 1u << part;
        for (u32 c = 0; c < 3; ++c) {
            const u8 color = rgb(part)[c];
            changed |= color != sDraw.colors[part][c];
            sDraw.colors[part][c] = color;
        }
    }
    changed |= sDraw.enabled != mask;
    sDraw.enabled = mask;
    if (changed && (mask & 0xFFu)) {
        const u32 pixels = reinterpret_cast<u32>(texture) + texture->mTextureOffset;
        initTexture(sDraw.original, *texture, reinterpret_cast<void *>(pixels));
        for (u32 i = 0; i < 5; ++i) {
            FluddColorTexture::recolor(reinterpret_cast<const u8 *>(pixels), sAtlases[i],
                sDraw.colors, mask, kPaintParts[i]);
            initTexture(sDraw.textures[i], *texture, sAtlases[i]);
        }
        DCStoreRange(sAtlases, sizeof(sAtlases));
        GXInvalidateTexAll();
        sDraw.source = texture;
        sDraw.ready = true;
    }
    // Register originals even when disabled: loaded states contain these wrappers.
    for (u32 i = 0; i < 6; ++i) install(body, i, 0, i == 2 ? 1 : i == 0 ? 2 : 0);
    const u8 banks[6] = {1, 3, 2, 0, 2, 4};
    for (u32 i = 0; i < 6; ++i) {
        if (i == TWaterGun::Yoshi) continue;
        J3DModel *nozzle = modelAt(gun->mNozzleList[i], 0x380, 1);
        if (nozzle) install(nozzle, 0, banks[i]);
    }
}
} // namespace FluddColors
