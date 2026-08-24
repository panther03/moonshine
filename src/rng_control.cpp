// Narrow RNG controls for King Boo, Bianco's route Skeeter, and Ricco's five
// cranes. Retail rand still runs exactly once at every hooked call.

#include "susamune/rng_control.hxx"

#include "Dolphin/OS.h"
#include "Dolphin/math.h"
#include "Dolphin/types.h"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/features.hxx"
#include "susamune/iling.hxx"
#include "susamune/settings.hxx"

extern "C" int rand();

extern "C" int  susamuneCraneUpDownRandImpl(void *crane);
extern "C" int  susamuneCraneRotYRandImpl(void *crane);
extern "C" bool susamuneBiancoSkeeterSearch(const void *nerve, void *spine);
extern "C" void susamuneCraneUpDownControl(void *crane);
extern "C" void susamuneCraneRotYControl(void *crane);

// The hook replaces `bl rand` while each caller still owns its crane in a
// nonvolatile register. Tail-branching keeps the caller's LR as the resolver's
// return address.
extern "C" u32 gCraneUpDownRandShim[] = {
    0x7FE3FB78u,  // mr r3, r31
    0x48000000u,  // b susamuneCraneUpDownRandImpl
};
extern "C" u32 gCraneRotYRandShim[] = {
    0x7FC3F378u,  // mr r3, r30
    0x48000000u,  // b susamuneCraneRotYRandImpl
};

namespace {

const u8 kRiccoArea = 3;
const u8 kBiancoArea = 2;
const u8 kCraneCount = 5;
const u8 kRotYCount = 3;
const u16 kNoRoll = 0xFFFFu;

const f32 kSkeeterSpawnX = 4296.2368f;
const f32 kSkeeterSpawnY = 2300.0f;
const f32 kSkeeterSpawnZ = -10375.8877f;
const f32 kSkeeterSpawnTolerance = 400.0f;
const u16 kSkeeterRouteRolls[] = {
    0x0000u, 0x7000u, 0x7FFFu,
};
const u32 kBiancoSkeeterSearch =
    SUSAMUNE_MEM1_ADDR(0x8033DDA4u, 0x8012C4C8u, 0x801259CCu);

const u32 kPeteyTornadoSite =
    SUSAMUNE_MEM1_ADDR(0x802A33ACu, 0x80090590u, 0x80089C30u);
const u32 kPeteyTornadoRetail = 0x408000F0u;
const u32 kPeteyTornadoDisabled = 0x60000000u;

const u32 kUpDownControlSlot =
    SUSAMUNE_MEM1_ADDR(0x803C6638u, 0x803CEC40u, 0x803C6430u);
const u32 kRotYControlSlot =
    SUSAMUNE_MEM1_ADDR(0x803C679Cu, 0x803CEDA4u, 0x803C6594u);
const u32 kUpDownRetailControl =
    SUSAMUNE_MEM1_ADDR(0x801A5F20u, 0x801CE368u, 0x801C6220u);
const u32 kRotYRetailControl =
    SUSAMUNE_MEM1_ADDR(0x801A62E0u, 0x801CE728u, 0x801C65E0u);
const u32 kUpDownRetailSpeed =
    SUSAMUNE_MEM1_ADDR(0x804090C4u, 0x8040C834u, 0x80403F94u);
const u32 kKingBooForceStop =
    SUSAMUNE_MEM1_ADDR(0x802D6DE0u, 0x800C476Cu, 0x800BDE0Cu);
const u8 kKingBooFruit = 2;
const u8 kKingBooNoFruit = 3;
const u8 kKingBooNextNoFruit = 1 << 0;
const u8 kKingBooPending = 1 << 1;

const char *const kCraneNames[kCraneCount] = {
    "crane90 0", "crane90 1", "crane90 2",
    "craneUpDown 0", "craneUpDown 1",
};
const u16 kCraneKeys[kCraneCount] = {
    0x5C04u, 0x5C05u, 0x5C06u, 0x09CEu, 0x09CFu,
};

struct CraneRoll {
    void *actor;
    f32 retailSpeed;
    u16 roll;
    bool speedCaptured;
};

CraneRoll sCranes[kCraneCount];
u32 sBiancoSkeeterSearchTrampoline[2];
void *sKingBooSlot;
bool sKingBooRestorePending;
bool sPeteyPatchCaptured;
bool sPeteyPatchInstalled;
bool sSkeeterDecisionConsumed;
bool sSavedSkeeterDecisionConsumed;
bool sHaveSavedSkeeterDecision;

struct SkeeterQuat {
    f32 x;
    f32 y;
    f32 z;
    f32 w;
};

bool sameText(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

bool inRicco() {
    return gpMarDirector && gpMarDirector->mAreaID == kRiccoArea;
}

bool isBiancoRouteSkeeter(void *skeeter) {
    if (!skeeter || !gpMarDirector ||
        gpMarDirector->mAreaID != kBiancoArea ||
        gpMarDirector->mEpisodeID > 1) return false;
    const volatile f32 *position = reinterpret_cast<const volatile f32 *>(
        static_cast<const u8 *>(skeeter) + 0x194);
    const f32 dx = position[0] - kSkeeterSpawnX;
    const f32 dy = position[1] - kSkeeterSpawnY;
    const f32 dz = position[2] - kSkeeterSpawnZ;
    return dx >= -kSkeeterSpawnTolerance &&
           dx <= kSkeeterSpawnTolerance &&
           dy >= -kSkeeterSpawnTolerance &&
           dy <= kSkeeterSpawnTolerance &&
           dz >= -kSkeeterSpawnTolerance &&
           dz <= kSkeeterSpawnTolerance;
}

int craneIndex(void *crane, bool upDown) {
    if (!crane || !inRicco()) return -1;
    const u8 *bytes = static_cast<const u8 *>(crane);
    const char *name = *reinterpret_cast<const char *const *>(bytes + 4);
    const u16 key = *reinterpret_cast<const u16 *>(bytes + 8);
    const int first = upDown ? kRotYCount : 0;
    const int end = upDown ? kCraneCount : kRotYCount;
    for (int i = first; i < end; i++) {
        if (key == kCraneKeys[i] && sameText(name, kCraneNames[i])) return i;
    }
    return -1;
}

u8 cranePercent(u16 roll) {
    u8 choice = gSettings.get(SETTING_RICCO_CRANE_SPEED);
    if (choice < 1 || choice > 5) return 0;
    return (u8)(choice * 20 - 19 + ((u32)roll * 20u >> 15));
}

CraneRoll *findCrane(void *actor, bool upDown) {
    const int i = craneIndex(actor, upDown);
    if (i < 0 || sCranes[i].actor != actor || sCranes[i].roll == kNoRoll)
        return nullptr;
    return &sCranes[i];
}

void recordCrane(void *actor, int retail, bool upDown) {
    const int i = craneIndex(actor, upDown);
    if (i < 0) return;
    sCranes[i].actor = actor;
    sCranes[i].roll = (u16)retail;
    sCranes[i].speedCaptured = false;
}

void installControlWrapper(u32 slot, u32 retail, u32 wrapper) {
    const u32 current = *reinterpret_cast<volatile u32 *>(slot);
    if (current == retail) writeGameCode(slot, wrapper);
}

void installCapturedEntryHook(u32 site, const void *wrapper,
                              u32 *trampoline) {
    trampoline[0] = *reinterpret_cast<volatile const u32 *>(site);
    trampoline[1] = branchWord(reinterpret_cast<u32>(&trampoline[1]),
                               site + 4);
    DCFlushRange(trampoline, 2 * sizeof(u32));
    ICInvalidateRange(trampoline, 2 * sizeof(u32));
    writeGameCode(site, branchWord(site, reinterpret_cast<u32>(wrapper)));
}

void applySkeeterRoute(void *skeeter, u16 roll) {
    u8 *bytes = static_cast<u8 *>(skeeter);
    const SkeeterQuat start = *reinterpret_cast<const SkeeterQuat *>(
        bytes + 0x1C0);
    SkeeterQuat *target = reinterpret_cast<SkeeterQuat *>(bytes + 0x1D0);
    const f32 angle = (1.5f - (f32)roll * (1.0f / 32768.0f)) *
                      3.1415927f;
    const f32 rotY = sinf(angle * 0.5f);
    const f32 rotW = cosf(angle * 0.5f);
    target->x = rotW * start.x + rotY * start.z;
    target->y = rotW * start.y + rotY * start.w;
    target->z = rotW * start.z - rotY * start.x;
    target->w = rotW * start.w - rotY * start.y;
}

void applyPeteyTornadoControl() {
    volatile u32 *site = reinterpret_cast<volatile u32 *>(kPeteyTornadoSite);
    if (!sPeteyPatchCaptured) {
        if (*site != kPeteyTornadoRetail) return;
        sPeteyPatchCaptured = true;
    }

    const bool want = gSettings.getBool(SETTING_PETEY_NO_TORNADO);
    if (want == sPeteyPatchInstalled) return;
    if (want) {
        if (*site != kPeteyTornadoRetail) return;
        writeGameCode(kPeteyTornadoSite, kPeteyTornadoDisabled);
        sPeteyPatchInstalled = true;
    } else {
        if (*site == kPeteyTornadoDisabled)
            writeGameCode(kPeteyTornadoSite, kPeteyTornadoRetail);
        sPeteyPatchInstalled = false;
    }
}

}  // namespace

void rngControlInit() {
    const u32 upBranch = reinterpret_cast<u32>(&gCraneUpDownRandShim[1]);
    const u32 rotBranch = reinterpret_cast<u32>(&gCraneRotYRandShim[1]);
    writeGameCode(upBranch,
                  branchWord(upBranch,
                             reinterpret_cast<u32>(&susamuneCraneUpDownRandImpl)));
    writeGameCode(rotBranch,
                  branchWord(rotBranch,
                             reinterpret_cast<u32>(&susamuneCraneRotYRandImpl)));

    installCapturedEntryHook(
        kBiancoSkeeterSearch,
        reinterpret_cast<const void *>(&susamuneBiancoSkeeterSearch),
        sBiancoSkeeterSearchTrampoline);

    installControlWrapper(kUpDownControlSlot, kUpDownRetailControl,
                          reinterpret_cast<u32>(&susamuneCraneUpDownControl));
    installControlWrapper(kRotYControlSlot, kRotYRetailControl,
                          reinterpret_cast<u32>(&susamuneCraneRotYControl));
}

void rngControlBeforeStageSetup() {
    sKingBooSlot = nullptr;
    sKingBooRestorePending = false;
    sSkeeterDecisionConsumed = false;
    for (int i = 0; i < kCraneCount; i++) {
        sCranes[i].actor = nullptr;
        sCranes[i].retailSpeed = 0.0f;
        sCranes[i].roll = kNoRoll;
        sCranes[i].speedCaptured = false;
    }
}

void rngControlOnSavestateSaved() {
    sSavedSkeeterDecisionConsumed = sSkeeterDecisionConsumed;
    sHaveSavedSkeeterDecision = true;
}

void rngControlOnSavestateLoaded() {
    if (sHaveSavedSkeeterDecision)
        sSkeeterDecisionConsumed = sSavedSkeeterDecisionConsumed;
    sKingBooSlot = nullptr;
    sKingBooRestorePending =
        gSettings.getBool(SETTING_KING_BOO_ALWAYS_FRUIT);
}

void rngControlApply() {
    applyPeteyTornadoControl();
    if (rngControlInvalidatesIl()) ILing::invalidateForAssist();
}

bool rngControlInvalidatesIl() {
    return gSettings.getBool(SETTING_KING_BOO_ALWAYS_FRUIT) ||
           gSettings.getBool(SETTING_PETEY_NO_TORNADO) ||
           gSettings.get(SETTING_PETEY_ROUTE) != 0;
}

extern "C" int susamuneCraneUpDownRandImpl(void *crane) {
    const int retail = rand();
    recordCrane(crane, retail, true);
    return retail;
}

extern "C" int susamuneCraneRotYRandImpl(void *crane) {
    const int retail = rand();
    recordCrane(crane, retail, false);
    return retail;
}

extern "C" bool susamuneBiancoSkeeterSearch(const void *nerve, void *spine) {
    typedef bool (*SearchFn)(const void *, void *);
    u8 *skeeter = spine
        ? *reinterpret_cast<u8 **>(static_cast<u8 *>(spine))
        : nullptr;
    const bool firstDecision =
        skeeter && !sSkeeterDecisionConsumed &&
        *reinterpret_cast<const s32 *>(static_cast<const u8 *>(spine) + 0x20)
            == 0 &&
        isBiancoRouteSkeeter(skeeter);
    u8 choice = 0;
    u32 priorSearchCooldown = 0;
    if (firstDecision) {
        sSkeeterDecisionConsumed = true;
        choice = gSettings.get(SETTING_BIANCO_SKEETER_ROUTE);
        priorSearchCooldown = *reinterpret_cast<const u32 *>(skeeter + 0x1A8);
    }

    const bool result = reinterpret_cast<SearchFn>(
        sBiancoSkeeterSearchTrampoline)(nerve, spine);
    if (firstDecision && choice >= 1 && choice <= 3) {
        // Match the wandering branch even when retail spotted Mario first.
        skeeter[0x1A0] = 0;
        *reinterpret_cast<u32 *>(skeeter + 0x1A8) = priorSearchCooldown;
        applySkeeterRoute(skeeter, kSkeeterRouteRolls[choice - 1]);
    }
    return result;
}

extern "C" void susamuneCraneUpDownControl(void *crane) {
    typedef void (*ControlFn)(void *);
    volatile f32 *speed = reinterpret_cast<volatile f32 *>(kUpDownRetailSpeed);
    const f32 retailSpeed = *speed;
    CraneRoll *record = findCrane(crane, true);
    const u8 percent = record ? cranePercent(record->roll) : 0;
    if (percent) *speed = retailSpeed * (f32)percent * 0.01f;
    reinterpret_cast<ControlFn>(kUpDownRetailControl)(crane);
    if (percent) *speed = retailSpeed;
}

extern "C" void susamuneCraneRotYControl(void *crane) {
    typedef void (*ControlFn)(void *);
    CraneRoll *record = findCrane(crane, false);
    if (record) {
        volatile f32 *speed = reinterpret_cast<volatile f32 *>(
            static_cast<u8 *>(crane) + 0x144);
        if (!record->speedCaptured) {
            record->retailSpeed = *speed;
            record->speedCaptured = true;
        }
        const u8 percent = cranePercent(record->roll);
        *speed = percent ? 0.0015f * (f32)percent : record->retailSpeed;
    }
    reinterpret_cast<ControlFn>(kRotYRetailControl)(crane);
}

extern "C" void susamuneForceKingBooFruit(void *slot, s32 reel) {
    typedef void (*ForceStopFn)(void *, s32);
    reinterpret_cast<ForceStopFn>(kKingBooForceStop)(slot, reel);
    if (!gSettings.getBool(SETTING_KING_BOO_ALWAYS_FRUIT)) {
        sKingBooSlot = nullptr;
        sKingBooRestorePending = false;
        return;
    }
    if (!slot || (u32)reel >= 3u)
        return;

    u8 *bytes = static_cast<u8 *>(slot);
    u8 *const magic = bytes + 0x19D;
    u8 *const state = bytes + 0x1AB;
    const bool markerValid = magic[0] == 'K' && magic[1] == 'B' &&
                             magic[2] == 'R';
    const bool keepRestored = sKingBooSlot != slot &&
                              sKingBooRestorePending && markerValid;
    if (sKingBooSlot != slot || !markerValid) {
        sKingBooSlot = slot;
        sKingBooRestorePending = false;
        if (!keepRestored) {
            magic[0] = 'K';
            magic[1] = 'B';
            magic[2] = 'R';
            *state = 0;
        }
    }
    if (*state & kKingBooPending) {
        const s32 prior = (*state & kKingBooNextNoFruit)
                              ? kKingBooNoFruit
                              : kKingBooFruit;
        u8 *const boss = *reinterpret_cast<u8 **>(bytes + 0x1A0);
        // generateSlotItem writes this only after all three reels agree.
        if (boss && *reinterpret_cast<volatile s32 *>(boss + 0x1A8) == prior)
            *state ^= kKingBooNextNoFruit;
    }

    const s32 result = (*state & kKingBooNextNoFruit)
                           ? kKingBooNoFruit
                           : kKingBooFruit;
    *state = (*state & kKingBooNextNoFruit) | kKingBooPending;
    *reinterpret_cast<volatile s32 *>(bytes + 0x1A4) = result;
    // A forecast only steers the drum while both targeted-stop flags are set.
    *reinterpret_cast<volatile u8 *>(bytes + 0x198 + reel) = 1;
    *reinterpret_cast<volatile u8 *>(bytes + 0x1A8 + reel) = 1;
}
