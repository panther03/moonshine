// Ricco's fruit launcher retries the selected fruit up to three times. The
// retail RNG still advances once at every reached site; only selector results
// are replaced, while both velocity rolls stay retail.

#include "susamune/ricco_fruit.hxx"

#include "Dolphin/types.h"
#include "SMS/System/MarDirector.hxx"
#include "susamune/addresses.hxx"
#include "susamune/features.hxx"
#include "susamune/settings.hxx"

extern "C" int rand();
extern "C" int susamuneRiccoFruitRandImpl(void *launcher, u32 returnAddress);

// Each hooked call owns the launcher in r31. LR identifies selector 1/2/3 or
// the first velocity roll, and the tail branch preserves LR for the caller.
extern "C" u32 gRiccoFruitRandShim[] = {
    0x7FE3FB78u,  // mr r3, r31
    0x7C8802A6u,  // mflr r4
    0x48000000u,  // b susamuneRiccoFruitRandImpl
};

namespace {

const u8 kRiccoArea = 3;
const int kDurianRoll = 0x6000;
const u32 kFireObj =
    SUSAMUNE_MEM1_ADDR(0x801A4FC4u, 0x801CD40Cu, 0x801C52C4u);

void *sPendingLauncher;

bool duriansEnabled(void *launcher) {
    return launcher && gpMarDirector &&
           gpMarDirector->mAreaID == kRiccoArea &&
           gSettings.get(SETTING_RICCO_FRUIT_MACHINE) == 1;
}

}  // namespace

void riccoFruitControlInit() {
    const u32 branch = reinterpret_cast<u32>(&gRiccoFruitRandShim[2]);
    writeGameCode(
        branch,
        branchWord(branch,
                   reinterpret_cast<u32>(&susamuneRiccoFruitRandImpl)));
}

void riccoFruitControlBeforeStageSetup() {
    sPendingLauncher = nullptr;
}

extern "C" int susamuneRiccoFruitRandImpl(void *launcher,
                                           u32 returnAddress) {
    const int retail = rand();
    const u32 site = returnAddress - 4;

    if (site == kFireObj + 0x134) {
        sPendingLauncher = duriansEnabled(launcher) ? launcher : nullptr;
        return sPendingLauncher ? kDurianRoll : retail;
    }

    if (site == kFireObj + 0x250) {
        if (sPendingLauncher == launcher && duriansEnabled(launcher))
            return kDurianRoll;
        sPendingLauncher = nullptr;
        return retail;
    }

    if (site == kFireObj + 0x36C) {
        const bool force = sPendingLauncher == launcher &&
                           duriansEnabled(launcher);
        sPendingLauncher = nullptr;
        return force ? kDurianRoll : retail;
    }

    // +0x4a0 is the first velocity roll and therefore proves that one of the
    // three allocations succeeded. Its return value is never substituted.
    if (site == kFireObj + 0x4A0 && sPendingLauncher == launcher)
        sPendingLauncher = nullptr;
    return retail;
}
