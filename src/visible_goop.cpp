#include "JSystem/J3D/J3DColor.hxx"
#include "JSystem/J3D/J3DMaterial.hxx"
#include "JSystem/J3D/J3DModel.hxx"
#include "SMS/Manager/PollutionManager.hxx"
#include "SMS/Map/PollutionLayer.hxx"
#include "SMS/System/MarDirector.hxx"
#include "susamune/settings.hxx"
#include "susamune/visible_goop.hxx"

namespace {

constexpr u32 kTevBlock2 = 0x54564232u;  // TVB2
constexpr u32 kTevBlock4 = 0x54564234u;  // TVB4

constexpr u32 kMaterialTevBlock = 0x28;
constexpr u32 kMaterialPEBlock = 0x30;
constexpr u32 kModelDataMaterialCount = 0x24;
constexpr u32 kModelDataMaterials = 0x28;
constexpr u32 kPEAlphaComp = 0x08;

struct TevStage {
    u8 colorReg;
    u8 colorOp;
    u8 colorAB;
    u8 colorCD;
    u8 alphaReg;
    u8 alphaOp;
    u8 alphaAB;
    u8 swapMode;
};
static_assert(sizeof(TevStage) == 8, "J3DTevStage layout changed");

struct AlphaComp {
    u16 id;
    u8 ref0;
    u8 ref1;
};
static_assert(sizeof(AlphaComp) == 4, "J3DAlphaComp layout changed");

constexpr u8 kRetailStage[] = {0x03, 0x39, 0x28, 0x00, 0xFF, 0xC0};
constexpr u8 kRevealStage[] = {0x0B, 0x79, 0x68, 0x3B, 0x72, 0x40};

constexpr AlphaComp kRetailAlpha = {0x00C3, 128, 255};
// The reveal stage doubles samples below 128. Retail stage 1 then maps zero
// to 64 and the first non-zero value to 65, keeping connector texels hidden.
constexpr AlphaComp kRevealAlpha = {0x0083, 64, 255};

enum ApplyPhase : u8 {
    APPLY_SCAN,
    APPLY_UNLOCK,
    APPLY_RESET_DL,
};

bool sApplied = false;
bool sTarget = false;
bool sRefreshDL = false;
ApplyPhase sPhase = APPLY_SCAN;
u32 sLayerIndex = 0;

J3DTevBlock *tevBlock(J3DMaterial *material) {
    return *reinterpret_cast<J3DTevBlock **>(
        reinterpret_cast<u8 *>(material) + kMaterialTevBlock);
}

AlphaComp *alphaComp(J3DMaterial *material) {
    u8 *pe = *reinterpret_cast<u8 **>(
        reinterpret_cast<u8 *>(material) + kMaterialPEBlock);
    return pe ? reinterpret_cast<AlphaComp *>(pe + kPEAlphaComp) : nullptr;
}

u16 materialCount(J3DModelData *data) {
    return *reinterpret_cast<u16 *>(
        reinterpret_cast<u8 *>(data) + kModelDataMaterialCount);
}

J3DMaterial **materials(J3DModelData *data) {
    return *reinterpret_cast<J3DMaterial ***>(
        reinterpret_cast<u8 *>(data) + kModelDataMaterials);
}

bool sameStage(const TevStage &stage, const u8 (&state)[6]) {
    return stage.colorOp == state[0] && stage.colorAB == state[1] &&
           stage.colorCD == state[2] && stage.alphaOp == state[3] &&
           stage.alphaAB == state[4] && stage.swapMode == state[5];
}

bool samePassAlpha(const TevStage &stage, bool biased) {
    return stage.alphaOp == (biased ? 0x31 : 0x00) &&
           stage.alphaAB == 0xFF && stage.swapMode == 0x80;
}

void setStage(TevStage &stage, const u8 (&state)[6]) {
    stage.colorOp = state[0];
    stage.colorAB = state[1];
    stage.colorCD = state[2];
    stage.alphaOp = state[3];
    stage.alphaAB = state[4];
    stage.swapMode = state[5];
}

bool sameAlpha(const AlphaComp &a, const AlphaComp &b) {
    return a.id == b.id && a.ref0 == b.ref0 && a.ref1 == b.ref1;
}

void setAlpha(AlphaComp &dst, const AlphaComp &src) {
    dst.id = src.id;
    dst.ref0 = src.ref0;
    dst.ref1 = src.ref1;
}

bool sameColor2(const s16 *color, bool revealed) {
    if (revealed) {
        return color[0] == -255 && color[1] == -255 &&
               color[2] == -255 && color[3] == 128;
    }
    return color[0] == 255 && color[1] == 255 &&
           color[2] == 255 && color[3] == 255;
}

void setColor2(s16 *color, bool revealed) {
    const s16 rgb = revealed ? -255 : 255;
    color[0] = rgb;
    color[1] = rgb;
    color[2] = rgb;
    color[3] = revealed ? 128 : 255;
}

bool updateMaterial(J3DMaterial *material, bool reveal) {
    if (!material) return false;

    J3DTevBlock *block = tevBlock(material);
    AlphaComp *alpha = alphaComp(material);
    if (!block || !alpha) return false;

    u8 *raw = reinterpret_cast<u8 *>(block);
    TevStage *stage = nullptr;
    s16 *color2 = nullptr;
    const u8 *order0 = nullptr;
    int stageCount = 0;
    const u32 type = block->getType();
    if (type == kTevBlock2) {
        stage = reinterpret_cast<TevStage *>(raw + 0x31);
        color2 = reinterpret_cast<s16 *>(raw + 0x20);
        order0 = raw + 0x08;
        stageCount = raw[0x30];
    } else if (type == kTevBlock4) {
        stage = reinterpret_cast<TevStage *>(raw + 0x1D);
        color2 = reinterpret_cast<s16 *>(raw + 0x4E);
        order0 = raw + 0x0C;
        stageCount = raw[0x1C];
    } else {
        return false;
    }

    // Only the standard ground/wall material has this exact pipeline. Wave
    // layers and unrelated material variants stay untouched.
    if ((type == kTevBlock2 && stageCount != 2) ||
        (type == kTevBlock4 && stageCount != 2 && stageCount != 3)) {
        return false;
    }

    // The fourth order byte is padding; retail factories leave it as 00 or FF.
    if (order0[0] != 0 || order0[1] != 0 || order0[2] != 4) {
        return false;
    }

    if (stage[0].colorReg != 0xC0 ||
        stage[0].alphaReg != 0xC1 || stage[1].colorReg != 0xC2 ||
        stage[1].alphaReg != 0xC3 ||
        (stageCount == 3 &&
         (stage[2].colorReg != 0xC4 || stage[2].alphaReg != 0xC5))) {
        return false;
    }

    if (!samePassAlpha(stage[1], true) ||
        (stageCount == 3 && !samePassAlpha(stage[2], false))) {
        return false;
    }

    const u8 (&fromStage)[6] = reveal ? kRetailStage : kRevealStage;
    const u8 (&toStage)[6] = reveal ? kRevealStage : kRetailStage;
    const AlphaComp &fromAlpha = reveal ? kRetailAlpha : kRevealAlpha;
    const AlphaComp &toAlpha = reveal ? kRevealAlpha : kRetailAlpha;

    // A loaded state can contain a material change captured between our
    // unlock/resetDL frames. Rebuild known materials even if their values
    // already match the setting; their saved display list may not match yet.
    if (sRefreshDL && sameStage(*stage, toStage) && sameAlpha(*alpha, toAlpha) &&
        sameColor2(color2, reveal)) return true;

    if (!sameStage(*stage, fromStage) || !sameAlpha(*alpha, fromAlpha) ||
        !sameColor2(color2, !reveal)) {
        return false;
    }

    setStage(*stage, toStage);
    setAlpha(*alpha, toAlpha);
    setColor2(color2, reveal);
    return true;
}

}  // namespace

void visibleGoopOnStageSetup() {
    sApplied = false;
    sTarget = false;
    sRefreshDL = false;
    sPhase = APPLY_SCAN;
    sLayerIndex = 0;
}

void visibleGoopOnSavestateLoaded() {
    visibleGoopOnStageSetup();
    sApplied = true;
    sRefreshDL = true;
}

void visibleGoopUpdate() {
    // gpPollution is not cleared by retail destruction; gpMarDirector is.
    if (!gpMarDirector || !gpPollution) return;

    const bool reveal = gSettings.getBool(SETTING_VISIBLE_GOOP);
    if (sPhase == APPLY_SCAN && sLayerIndex == 0) {
        if (!sRefreshDL && reveal == sApplied) return;
        sTarget = reveal;
    }

    if (sLayerIndex >= gpPollution->mJointModelNum) {
        sApplied = sTarget;
        sRefreshDL = false;
        sLayerIndex = 0;
        sPhase = APPLY_SCAN;
        return;
    }

    TPollutionLayer *layer = static_cast<TPollutionLayer *>(
        gpPollution->mJointModels[sLayerIndex]);
    if (!layer || !layer->mModel || !layer->mModelData || !layer->mActor) {
        ++sLayerIndex;
        return;
    }

    if (sPhase == APPLY_UNLOCK) {
        layer->mModel->unlock();
        sPhase = APPLY_RESET_DL;
        return;
    }

    if (sPhase == APPLY_RESET_DL) {
        // resetDL re-locks only static packets; animated materials stay
        // writable for their normal per-frame updates.
        layer->mActor->resetDL();
        ++sLayerIndex;
        sPhase = APPLY_SCAN;
        return;
    }

    bool changed = false;
    J3DMaterial **modelMaterials = materials(layer->mModelData);
    const u16 count = materialCount(layer->mModelData);
    for (u16 j = 0; j < count; ++j) {
        changed |= updateMaterial(modelMaterials[j], sTarget);
    }

    if (changed) {
        // Console J3D needs a rendered frame between material mutation,
        // unlocking, and rebuilding the display list.
        sPhase = APPLY_UNLOCK;
    } else {
        ++sLayerIndex;
    }
}
