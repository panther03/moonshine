#include "susamune/stage_targets.hxx"

#include "Dolphin/OS.h"
#include "Dolphin/mem.h"
#include "Dolphin/printf.h"
#include "susamune/iling.hxx"
#include "susamune/mem2_map.h"
#include "susamune/menu.hxx"
#include "susamune/susamune_cfg.h"

namespace {

enum {
    kSaveTimeoutFrames = 30 * 15,
    kRetryFrames = 30 * 10,
};

#define sTargets (*reinterpret_cast<s32 (*)[SUSAMUNE_STAGE_TARGET_SLOT_COUNT]>( \
    SUSAMUNE_MEM2_STAGE_TARGETS_LIVE_PPC_BASE))

u32 sSaveSeq;
u32 sWaitFrames;
u32 sRetryFrames;
bool sBackend;
bool sDirty;
bool sPending;
bool sTimeoutNotified;

bool validTarget(s32 targetQf) {
    return targetQf >= SUSAMUNE_STAGE_TARGET_UNSET &&
           targetQf <= SUSAMUNE_ILING_PB_MAX_QF;
}

bool publish() {
#if IS_EMULATOR
    return false;
#else
    if (!sBackend || !sDirty || sPending || sRetryFrames != 0) return false;

    volatile SusamuneStageTargetsCfg *mailbox = SUSAMUNE_STAGE_TARGETS_PPC_PTR;
    memcpy((void *)mailbox->targets, sTargets, sizeof(sTargets));
    mailbox->magic = SUSAMUNE_STAGE_TARGET_MAGIC;
    mailbox->version = SUSAMUNE_STAGE_TARGET_VERSION;
    mailbox->slotCount = SUSAMUNE_STAGE_TARGET_SLOT_COUNT;
    DCStoreRange((void *)mailbox->targets, sizeof(mailbox->targets));

    sSaveSeq++;
    mailbox->saveSeq = sSaveSeq;
    DCStoreRange((void *)mailbox, 32);
    sDirty = false;
    sPending = true;
    sWaitFrames = 0;
    sRetryFrames = 0;
    sTimeoutNotified = false;
    return true;
#endif
}

}  // namespace

namespace StageTargets {

void init() {
    memset(sTargets, 0xff, sizeof(sTargets));
    sSaveSeq = 0;
    sWaitFrames = 0;
    sRetryFrames = 0;
    sBackend = false;
    sDirty = false;
    sPending = false;
    sTimeoutNotified = false;

#if !IS_EMULATOR
    volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    volatile SusamuneStageTargetsCfg *mailbox = SUSAMUNE_STAGE_TARGETS_PPC_PTR;
    DCInvalidateRange((void *)cfg, 32);
    DCInvalidateRange((void *)mailbox, sizeof(*mailbox));
    if (cfg->magic != SUSAMUNE_CFG_MAGIC ||
        cfg->version != SUSAMUNE_CFG_VERSION ||
        !(cfg->flags & SUSAMUNE_CFG_FLAG_STAGE_TARGETS) ||
        mailbox->magic != SUSAMUNE_STAGE_TARGET_MAGIC ||
        mailbox->version != SUSAMUNE_STAGE_TARGET_VERSION ||
        mailbox->slotCount != SUSAMUNE_STAGE_TARGET_SLOT_COUNT) {
        return;
    }

    for (u32 slot = 0; slot < SUSAMUNE_STAGE_TARGET_SLOT_COUNT; slot++) {
        const s32 value = mailbox->targets[slot];
        if (validTarget(value)) sTargets[slot] = value;
    }
    sSaveSeq = mailbox->saveSeq;
    sBackend = (mailbox->flags & SUSAMUNE_STAGE_TARGET_FLAG_WRITABLE) != 0;
#endif
}

void service(Menu *menu) {
#if !IS_EMULATOR
    if (sPending) {
        volatile SusamuneStageTargetsCfg *mailbox =
            SUSAMUNE_STAGE_TARGETS_PPC_PTR;
        DCInvalidateRange((void *)&mailbox->ackSeq, 32);
        if (mailbox->ackSeq == sSaveSeq) {
            sPending = false;
            sWaitFrames = 0;
            sTimeoutNotified = false;
            if (mailbox->status != 0) {
                sDirty = true;
                sRetryFrames = kRetryFrames;
                if (menu) {
                    char error[40];
                    snprintf(error, sizeof(error), "Target save failed: %u",
                             (unsigned)mailbox->status);
                    menu->toast(error);
                }
                return;
            }
        } else if (!sTimeoutNotified &&
                   ++sWaitFrames > kSaveTimeoutFrames) {
            sTimeoutNotified = true;
            if (menu) menu->toast("Target save timed out");
        }
    }
    if (sRetryFrames != 0) {
        --sRetryFrames;
        return;
    }
    publish();
#else
    (void)menu;
#endif
}

s32 get(int entry) {
    const int slot = ILing::persistentSlot(entry);
    return slot >= 0 && slot < SUSAMUNE_STAGE_TARGET_SLOT_COUNT
               ? sTargets[slot]
               : SUSAMUNE_STAGE_TARGET_UNSET;
}

void set(int entry, s32 targetQf) {
    const int slot = ILing::persistentSlot(entry);
    if (slot < 0 || slot >= SUSAMUNE_STAGE_TARGET_SLOT_COUNT ||
        !validTarget(targetQf) || sTargets[slot] == targetQf) {
        return;
    }
    sTargets[slot] = targetQf;
    if (sBackend) {
        sDirty = true;
        publish();
    }
}

bool available() { return sBackend; }

}  // namespace StageTargets
