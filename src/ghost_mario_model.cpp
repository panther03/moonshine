// Clean Mario uses private materials, joints, shapes and transforms while the
// game-owned body asset supplies immutable geometry and texture pixels.
#include "susamune/ghost_mario_model.hxx"
#include "JSystem/J3D/J3DModel.hxx"
#include "Dolphin/types.h"

// This retail method is static despite the older project header declaration.
extern "C" void *newCleanTevBlock(int) asm("createTevBlock__11J3DMaterialFi");

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata"
namespace GhostMarioModel {

static bool owned(const void *pointer, u32 size, u32 begin, u32 end) {
    const u32 address = reinterpret_cast<u32>(pointer);
    return begin <= address && address <= end && size <= end - address;
}

static __attribute__((always_inline)) void configureMaterial(
        u8 *tev, u8 *texgen, u8 *color, u16 texture) {
        // Private TVB1 layout, audited against the retail classes/constructors.
        *reinterpret_cast<u16 *>(tev + 4) = texture;
        tev[6] = 0; // TEXCOORD0
        tev[7] = 0; // TEXMAP0, original base atlas
        tev[8] = 4; // COLOR0A0 (RGB white, ghost opacity in alpha)
        // (0 + texture * raster) -> PREV, clamped, both RGB and alpha.
        static const u8 stage[8] = {0xC0, 0x08, 0xF8, 0xAF,
                                    0xC1, 0x08, 0xF2, 0xF0};
        for (u32 b = 0; b < 8; ++b) tev[0x0A + b] = stage[b];
        for (u32 b = 0x12; b < 0x1E; ++b) tev[b] = 0; // no indirect sampling
        *reinterpret_cast<u32 *>(texgen + 4) = 1;
        for (u32 n = 0; n < 8; ++n) {
            u8 *coord = texgen + 8 + 4 * n;
            coord[0] = 1; // MTX2x4
            coord[1] = 4; // TEX0
            coord[2] = 60; // IDENTITY
            *reinterpret_cast<u32 *>(texgen + 0x28 + 4 * n) = 0;
        }
        // Retain the private allocated matrix objects unused; no shared edits.
        texgen[0x48] = 0; // no NBT scaling/bump matrix allocation
        for (u32 b = 4; b < 12; ++b) color[b] = 255;
        color[0x0C] = 1;
        for (u32 n = 0; n < 4; ++n)
            *reinterpret_cast<u16 *>(color + 0x0E + n * 2) = 0x0400;
}

bool prepare(J3DModelData *data, u32 heapBegin, u32 heapEnd) {
    if (!owned(data, sizeof(*data), heapBegin, heapEnd) ||
        data->getJointNum() != 29 || data->getMaterialNum() != 11 ||
        data->mShapeNum != 11 ||
        !owned(data->mMaterials, 11 * 4, heapBegin, heapEnd) ||
        !owned(data->mShapes, 11 * 4, heapBegin, heapEnd)) return false;

    for (u16 i = 0; i < 11; ++i) {
        u8 *material = reinterpret_cast<u8 *>(data->mMaterials[i]);
        if (!owned(material, 0x40, heapBegin, heapEnd)) return false;
        u8 *color = *reinterpret_cast<u8 **>(material + 0x20);
        u8 *texgen = *reinterpret_cast<u8 **>(material + 0x24);
        const u8 *oldTev = *reinterpret_cast<u8 **>(material + 0x28);
        if (!owned(color, 0x18, heapBegin, heapEnd) ||
            !owned(texgen, 0x5C, heapBegin, heapEnd) ||
            !owned(oldTev, 6, heapBegin, heapEnd)) return false;
        const u16 texture = *reinterpret_cast<const u16 *>(oldTev + 4);
        if (texture >= 59) return false;
        u8 *tev = static_cast<u8 *>(newCleanTevBlock(1));
        if (!owned(tev, 0x20, heapBegin, heapEnd)) return false;
        configureMaterial(tev, texgen, color, texture);
        *reinterpret_cast<u8 **>(material + 0x28) = tev;
        // The old block remains in the same heap; the bound includes it.
        // No free/delete ABI or allocator fragmentation assumptions are needed.
    }
    // ma_mdl1 contains the normal cap and hands. Hide its optional Aloha shirt.
    // Retail uses flag bit 1 to suppress shapes despite the old enum's name.
    u8 *shirt = reinterpret_cast<u8 *>(data->mShapes[10]);
    if (!owned(shirt, 0x60, heapBegin, heapEnd)) return false;
    *reinterpret_cast<u32 *>(shirt + 8) |= 1;
    return true;
}

} // namespace GhostMarioModel
