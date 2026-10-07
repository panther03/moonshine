// Clean Mario uses private materials, joints, shapes and transforms while the
// game-owned body asset supplies immutable geometry and texture pixels.
#include "susamune/ghost_mario_model.hxx"
#include "JSystem/J3D/J3DModel.hxx"
#include "Dolphin/types.h"
#include "Dolphin/mem.h"

// This retail method is static despite the older project header declaration.
extern "C" void *newCleanTevBlock(int) asm("createTevBlock__11J3DMaterialFi");

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata"
namespace GhostMarioModel {

static bool owned(const void *pointer, u32 size, u32 begin, u32 end) {
    const u32 address = reinterpret_cast<u32>(pointer);
    return begin <= address && address <= end && size <= end - address;
}

static __attribute__((always_inline)) void configureMaterial(
        u8 *tev, u8 *color, const u8 *original) {
        // Keep retail Mario's lighting lookup and specular combination. Only
        // the two pollution-mask stages are replaced by a clean base sample.
        *reinterpret_cast<u16 *>(tev + 4) = *reinterpret_cast<const u16 *>(original + 4);
        *reinterpret_cast<u16 *>(tev + 6) = *reinterpret_cast<const u16 *>(original + 10);
        static const u8 orders[12] = {0, 0, 4, 0, 3, 1, 4, 0, 255, 255, 5, 0};
        memcpy(tev + 0x0C, orders, sizeof(orders));
        tev[0x1C] = 3;
        memcpy(tev + 0x3E, original + 0xD6, 48); // TEV and konst colours
        memcpy(tev + 0x76, original + 0x126, 4); // swap tables
        for (u32 stage = 0; stage < 3; ++stage) {
            u8 *out = tev + 0x1D + stage * 8;
            if (stage) {
                const u32 source = stage + 2;
                memcpy(out, original + 0x55 + source * 8, 8);
                tev[0x6E + stage] = original[0x106 + source];
                tev[0x72 + stage] = original[0x116 + source];
            } else {
                out[1] = 8; out[2] = 0xFF; out[3] = 0xF8; // base texture -> PREV
            }
            out[0] = 0xC0 + stage * 2; out[4] = out[0] + 1;
            out[5] = 8; out[6] = 0xFF;
            out[7] = stage ? 0x80 : 0xD0; // alpha PREV / raster opacity
        }
        // The original texgens supply base UV0 and the COLOR0 lighting lookup.
        // Keep the BMD's diffuse channel, light mask and material colours.
        // Resetting every channel to 0x0400 made the body completely unlit.
        color[0x0C] = 2;
        // Opacity is independent of lighting (the material alpha register).
        *reinterpret_cast<u16 *>(color + 0x10) = 0x0400;
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
        const u8 *oldTev = *reinterpret_cast<u8 **>(material + 0x28);
        if (!owned(color, 0x18, heapBegin, heapEnd) ||
            !owned(oldTev, 0x12A, heapBegin, heapEnd)) return false;
        const u16 texture = *reinterpret_cast<const u16 *>(oldTev + 4);
        if (texture >= 59) return false;
        u8 *tev = static_cast<u8 *>(newCleanTevBlock(4));
        if (!owned(tev, 0xA0, heapBegin, heapEnd)) return false;
        configureMaterial(tev, color, oldTev);
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
