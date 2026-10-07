#include "susamune/ghost_fludd.hxx"
#include "Dolphin/math.h"
#include "Dolphin/mem.h"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/Watergun.hxx"
#include "susamune/retail_input.hxx"

#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"

extern "C" f32 ghostSquareRoot(f32) asm("sqrtf__3stdFf");

extern "C" u8 ghostRetailWaterEmit(void *, const TWaterEmitInfo &)
    asm("emitRequest__18TModelWaterManagerFRC14TWaterEmitInfo");

namespace GhostFludd {
namespace {
TMario *sEmitter;
TVec3f sPosition, sDirection;
f32 sPower;
bool sEmitted;

bool finite(f32 value) { return value >= -1000000.0f && value <= 1000000.0f; }

void pack(SusamuneGhostFluddSample &out, TMario *mario, u8 nozzle,
          const TVec3f &position, const TVec3f &direction, bool spray, f32 power) {
    memset(&out, 0, sizeof(out));
    if (!finite(direction.x) || !finite(direction.y) || !finite(direction.z)) return;
    const f32 flat = ghostSquareRoot(direction.x * direction.x + direction.z * direction.z);
    if (flat + fabsf(direction.y) < 0.0001f) return;
    const f32 offset[3] = {position.x - mario->mTranslation.x,
                           position.y - mario->mTranslation.y,
                           position.z - mario->mTranslation.z};
    for (u32 i = 0; i < 3; ++i) {
        if (!finite(offset[i]) || offset[i] < -254.0f || offset[i] > 254.0f) return;
    }
    for (u32 i = 0; i < 3; ++i) {
        out.offset[i] = (s8)(offset[i] * 0.5f + (offset[i] < 0 ? -0.5f : 0.5f));
    }
    const f32 turnScale = 651.8986469f;
    const u32 yaw = (s32)(atan2f(direction.x, direction.z) * turnScale) & 4095u;
    const u32 pitch = (s32)(atan2f(direction.y, flat) * turnScale) & 4095u;
    out.aim[0] = (u8)(yaw >> 4);
    out.aim[1] = (u8)((yaw << 4) | (pitch >> 8));
    out.aim[2] = (u8)pitch;
    out.mode = nozzle | SUSAMUNE_GHOST_FLUDD_PRESENT |
        (spray ? SUSAMUNE_GHOST_FLUDD_SPRAYING : 0);
    if (spray && finite(power) && power > 0)
        out.power = (u8)(power >= 255 ? 255 : power + 0.5f);
}
}

void beginFrame() { sEmitter = nullptr; sEmitted = false; }

void direction(const SusamuneGhostFluddSample &sample, TVec3f &out) {
    const u32 yaw = (sample.aim[0] << 4) | (sample.aim[1] >> 4);
    s32 pitch = ((sample.aim[1] & 15) << 8) | sample.aim[2];
    if (pitch & 2048) pitch -= 4096;
    const f32 angle = 0.001533980788f;
    const f32 flat = cosf(pitch * angle);
    out.set(sinf(yaw * angle) * flat, sinf(pitch * angle), cosf(yaw * angle) * flat);
}

void capture(SusamuneGhostFluddSample &sample) {
    memset(&sample, 0, sizeof(sample));
    TMario *mario = gpMarioOriginal;
    if (!RetailInput::stageDirector() || !mario || !mario->mAttributes.mHasFludd ||
        !mario->mFludd || mario->mFludd->mCurrentNozzle >= 6) return;
    TWaterGun *gun = mario->mFludd;
    if (sEmitted && sEmitter == mario) {
        pack(sample, mario, gun->mCurrentNozzle, sPosition, sDirection, true, sPower);
    } else {
        Mtx *matrix = gun->getEmitMtx(0);
        if (!matrix) return;
        TVec3f position, direction;
        position.set((*matrix)[0][3], (*matrix)[1][3], (*matrix)[2][3]);
        direction.set((*matrix)[0][0], (*matrix)[1][0], (*matrix)[2][0]);
        pack(sample, mario, gun->mCurrentNozzle, position, direction, false, 0);
    }
}

void observe(const TWaterEmitInfo &info, u8 emitted) {
    TMario *mario = gpMarioOriginal;
    if (!emitted || !RetailInput::stageDirector() || !mario || !mario->mFludd ||
        mario->mFludd->mEmitInfo != &info) return;
    // Observe the actual accepted emission, not R: an empty tank, nozzle switch
    // or full retail particle pool can suppress water despite held input.
    sEmitter = mario;
    sPosition = info.mPos.get();
    sDirection = info.mDir.get();
    sPower = info.mPow.get();
    sEmitted = true;
}
}

extern "C" u8 susamuneGhostWaterEmit(void *manager, const TWaterEmitInfo &info) {
    const u8 emitted = ghostRetailWaterEmit(manager, info);
    GhostFludd::observe(info, emitted);
    return emitted;
}
