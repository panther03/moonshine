// =====================================================================
// actions.cpp
//
// The bind-driven gecko-code ports: Regrab Last Held Object, Spawn Yoshi
// and Fast Forward. None of these is a memory patch of someone else's asm
// (the exception is noted below) -- each one was reversed out of its gecko
// original and rewritten as C against the decomp's types, per the "prefer
// C" rule in doc/gecko_porting.md. The gecko lines each behaviour came from
// are quoted with the code that replaces them.
//
// Every action is gated on a bind from binds.*, replacing the fixed button
// combination its gecko original hardcodes.
// =====================================================================

#include "susamune/actions.hxx"

#include "susamune/addresses.hxx"  // SUSAMUNE_MEM1_ADDR
#include "susamune/binds.hxx"
#include "susamune/features.hxx"  // writeGameCode, branchWord
#include "susamune/iling.hxx"

#include "Dolphin/OS.h"  // DCFlushRange, ICInvalidateRange
#include "SMS/Camera/PolarSubCamera.hxx"
#include "SMS/MapObj/MapObjBase.hxx"
#include "SMS/MoveBG/EggYoshi.hxx"
#include "SMS/Player/Mario.hxx"
#include "SMS/Player/Watergun.hxx"
#include "SMS/Player/Yoshi.hxx"
#include "SMS/System/MarDirector.hxx"

// Called from the TEggYoshi::control trampoline below. Defined at namespace
// scope so its address is a plain external symbol the cave can `bl` to.
extern "C" void susamuneOnEggYoshiControl(TEggYoshi *egg);

namespace {

// Mario status words, from the decomp's MarioStatus.hpp.
const u32 kMarioStatusTake = 0x00000383u;  // MARIO_STATUS_TAKE
const u32 kMarioStatusWait = 0x0C400201u;  // MARIO_STATUS_WAIT
const u32 kHitMessageTake  = 0x00000004u;  // HIT_MESSAGE_TAKE

TTakeActor *gLastHeldObject;

// MARIO_STATUS_FLAG_UNK1000: set while a demo/cutscene drives Mario.
// TMario::checkCollision returns immediately when it is set, and both Spawn
// Yoshi hooks sit after that check, so neither fires during a cutscene.
const u32 kMarioStatusFlagDemo = 0x00001000u;

// True once the stage heap is populated and the director is running the level
// proper. Mirrors savestate.cpp's inLoadTransition(): gpMarioOriginal is a
// plain sbss pointer that is not cleared between stages, so dereferencing it
// during a load would follow a pointer into a freed heap.
bool stageActive() {
    if (!gpMarDirector || gpMarDirector->_260 == 0) {
        return false;
    }
    return gpMarDirector->mCurState >= TMarDirector::STATE_GAME_STARTING;
}

// =====================================================================
// Regrab Last Held Object  (DPad Functions, mode bit 0x408)
//
//   48000000 8040A398   ; pointer = gpMarioAddress
//   1400007C 00000383   ; *(u32*)(pointer + 0x7C) = 0x383
//
// TMario+0x7C is mState and 0x383 is MARIO_STATUS_TAKE. A throw clears
// mHeldObject, so the native port also retains the actual object and sends it
// through its retail TAKE handler.
//
// Match the Gecko handler's per-frame write while the combo is held.
// =====================================================================

void regrabLastHeldObject() {
    if (!stageActive()) {
        return;
    }
    TMario *mario = gpMarioAddress;
    if (!mario) {
        return;
    }

    if (mario->mHeldObject) {
        gLastHeldObject = mario->mHeldObject;
        return;
    }
    if (!gLastHeldObject || !gBinds.isHeld(BIND_REGRAB_OBJECT)) {
        return;
    }

    ILing::invalidateForAssist();
    mario->mGrabTarget = gLastHeldObject;
    mario->mState      = kMarioStatusTake;
    if (gLastHeldObject->receiveMessage(mario, kHitMessageTake)) {
        mario->mHeldObject = gLastHeldObject;
        mario->mGrabTarget = nullptr;
    }
}

// =====================================================================
// Spawn Yoshi
//
// Two C2 injections upstream. The first, at TMario::checkCollision+0x44,
// paints the requested colour onto Mario's TYoshi, refills its juice and then
// branches into the middle of the function's existing "Mario landed on Yoshi"
// path, at checkCollision+0x1C0 -- skipping the part that would teleport Mario
// onto the (unhatched, positionless) Yoshi. What is left of that path is:
//
//     mModelAngleY = mAngle.y;
//     if (hasFludd) { save nozzle + water level for the dismount }
//     mYoshi->ride();
//     hasFludd = true;                          // Yoshi's juice IS the FLUDD
//     mFludd->changeNozzle(Yoshi, true);
//     changePlayerStatus(MARIO_STATUS_WAIT, 0, false);
//
// which is what spawnYoshi() below does directly, with no patch at all.
//
// The colour ids come from the gecko's `rlwnm r0, 0x63000000, r4, 30, 31`
// table and match TYoshi::Color: green 0, orange 1, purple 2, pink 3.
//
// The second injection, at TEggYoshi::control+0x1C, is the one thing that
// cannot be done from a per-frame hook: it runs once per egg in the stage and
// makes the level's egg vanish, remembering it on the Yoshi so that the egg is
// restored (TYoshi::mEgg -> startFruit()) when the Yoshi eventually expires.
// We keep an injection there, but only as a ten-word trampoline into C -- see
// applyEggHook() below.
// =====================================================================

// Frames left during which the egg trampoline should act. Set when a Yoshi is
// spawned and counted down at the end of actionsApply(): TEggYoshi::control
// runs inside director->direct(), i.e. *before* the next actionsApply(), so the
// arm has to survive one full frame.
u8 gEggKillFrames = 0;

// TEggYoshi::control+0x1C, replacing `lhz r0, 0xFC(r31)`. The site sits on the
// instruction after `bl TMapObjBase::control()`, so every volatile register
// (and LR, CR, f0-f13) is dead there and the trampoline may clobber freely; r31
// is `this`. Word 4 is patched to a `bl` into the C above, and the trailing
// word to `b -> site+4`, both at install time.
const int kEggCaveCallIdx = 4;
u32       gCaveEggYoshi[] = {
    0x7C0802A6u,  // mflr r0
    0x9421FFE0u,  // stwu r1, -0x20(r1)
    0x9001001Cu,  // stw r0, 0x1C(r1)
    0x7FE3FB78u,  // mr r3, r31          (the TEggYoshi)
    0x48000001u,  // [patched] bl susamuneOnEggYoshiControl
    0x8001001Cu,  // lwz r0, 0x1C(r1)
    0x7C0803A6u,  // mtlr r0
    0x38210020u,  // addi r1, r1, 0x20
    0xA01F00FCu,  // lhz r0, 0xFC(r31)   (displaced original)
    0x00000000u,  // [patched] b -> site+4
};

const u32 kEggSite = SUSAMUNE_MEM1_ADDR(0x801945ACu, 0x801BC5C4u, 0x801B447Cu);

const int kEggCaveWords = (int)(sizeof(gCaveEggYoshi) / sizeof(u32));

u32  gEggOrig;  // retail word at the site
u32  gEggOn;    // b site -> cave
bool gEggInited;
bool gEggInstalled;

// Splice the trampoline in only while a spawn is armed, so the game runs
// unpatched the rest of the time.
void applyEggHook(bool on) {
    if (!gEggInited) {
        u32 callAddr = reinterpret_cast<u32>(&gCaveEggYoshi[kEggCaveCallIdx]);
        gCaveEggYoshi[kEggCaveCallIdx] =
            branchWord(callAddr,
                       reinterpret_cast<u32>(&susamuneOnEggYoshiControl)) |
            1u;  // set LK: this is a bl, not a b
        u32 backAddr = reinterpret_cast<u32>(&gCaveEggYoshi[kEggCaveWords - 1]);
        gCaveEggYoshi[kEggCaveWords - 1] = branchWord(backAddr, kEggSite + 4);

        DCFlushRange(gCaveEggYoshi, sizeof(gCaveEggYoshi));
        ICInvalidateRange(gCaveEggYoshi, sizeof(gCaveEggYoshi));

        gEggOrig   = *reinterpret_cast<volatile u32 *>(kEggSite);
        gEggOn     = branchWord(kEggSite, reinterpret_cast<u32>(&gCaveEggYoshi[0]));
        gEggInited = true;
    }

    if (on == gEggInstalled) {
        return;
    }
    writeGameCode(kEggSite, on ? gEggOn : gEggOrig);
    gEggInstalled = on;
}

void spawnYoshi(u8 color) {
    TMario *mario = gpMarioAddress;
    if (!mario || (mario->mState & kMarioStatusFlagDemo)) {
        return;
    }
    TYoshi *yoshi = mario->mYoshi;
    if (!yoshi) {
        return;
    }

    yoshi->mType     = (s8)color;
    yoshi->mCurJuice = yoshi->mMaxJuice;

    mario->mModelAngleY = mario->mAngle.y;

    // The null check is ours: upstream dereferences mFludd unconditionally.
    TWaterGun *fludd = mario->mFludd;

    if (fludd && mario->mAttributes.mHasFludd) {
        // Stashed so dismounting restores the nozzle and water level. The
        // integer division is the game's own (see TMario::checkCollision in the
        // decomp), not a transcription slip.
        mario->_3E8 = fludd->mSecondNozzle;
        s32 amountMax =
            fludd->mNozzleList[fludd->mCurrentNozzle]->mEmitParams.mAmountMax.get();
        mario->_3EC = amountMax ? (f32)(fludd->mCurrentWater / amountMax) : 0.0f;
    }

    yoshi->ride();

    if (fludd) {
        // Riding Yoshi runs through the FLUDD: the flag goes on whether or not
        // Mario had it, then the nozzle is swapped to the Yoshi one.
        mario->mAttributes.mHasFludd = true;
        fludd->changeNozzle(TWaterGun::Yoshi, true);
    }

    mario->changePlayerStatus(kMarioStatusWait, 0, false);

    gEggKillFrames = 2;  // survives this frame's countdown, see gEggKillFrames
    ILing::invalidateForAssist();
}

void spawnYoshiFromBinds() {
    if (!stageActive()) {
        return;
    }
    if (gBinds.wasPressed(BIND_SPAWN_YOSHI_GREEN)) {
        spawnYoshi(TYoshi::GREEN);
    } else if (gBinds.wasPressed(BIND_SPAWN_YOSHI_ORANGE)) {
        spawnYoshi(TYoshi::ORANGE);
    } else if (gBinds.wasPressed(BIND_SPAWN_YOSHI_PURPLE)) {
        spawnYoshi(TYoshi::PURPLE);
    } else if (gBinds.wasPressed(BIND_SPAWN_YOSHI_PINK)) {
        spawnYoshi(TYoshi::PINK);
    }
}

// =====================================================================
// Save / Load Position  (DPad Functions, Dan Salvato)
// =====================================================================

struct PositionSnapshot {
    TVec3f position;
    s16    marioAngleY;
    s16    cameraHorizontalAngle;
    f32    cameraInterpolateDistance;
};

PositionSnapshot gPositionSnapshot;
bool             gPositionSnapshotValid;

bool positionActorsReady() {
    return stageActive() && gpMarioPos && gpMarioAngleY && gpCamera;
}

void savePosition() {
    if (!positionActorsReady()) {
        return;
    }

    gPositionSnapshot.position                  = *gpMarioPos;
    gPositionSnapshot.marioAngleY               = *gpMarioAngleY;
    gPositionSnapshot.cameraHorizontalAngle     = gpCamera->mHorizontalAngle;
    gPositionSnapshot.cameraInterpolateDistance = gpCamera->mInterpolateDistance;
    gPositionSnapshotValid                      = true;
}

void loadPosition() {
    if (!gPositionSnapshotValid || !positionActorsReady()) {
        return;
    }

    *gpMarioPos = gPositionSnapshot.position;
    *gpMarioAngleY = gPositionSnapshot.marioAngleY;
    gpCamera->mHorizontalAngle = gPositionSnapshot.cameraHorizontalAngle;
    gpCamera->mInterpolateDistance = gPositionSnapshot.cameraInterpolateDistance;
    ILing::invalidateForAssist();
}

void positionFromBinds() {
    if (gBinds.isHeld(BIND_POSITION_SAVE)) {
        savePosition();
    }
    if (gBinds.isHeld(BIND_POSITION_LOAD)) {
        loadPosition();
    }
}

// =====================================================================
// Fast Forward
//
//   020ECDE2 00000258   ; default
//   28400D50 00000201   ; if buttons == B + DPad Left
//   020ECDE2 00000960
//   28400D51 00000202   ; else if buttons == B + DPad Right
//   020ECDE2 000012C0
//
// The patched halfword is the immediate of `li r3, 600` in
// TMarDirector::direct -- the per-frame logic-tick budget the director spends
// before it renders. Quadrupling it runs four times the game logic per
// rendered frame. This is the whole code: there is nothing to reimplement in
// C, so it stays a masked halfword write, applied while the bind is *held*.
//
// The word is captured on first apply, so releasing the bind writes the retail
// value back on any revision. (Not to be confused with Stage Intro Skip, which
// refills the same budget mid-loop -- see doc/gecko_porting.md.)
// =====================================================================

const u32 kFfSite = SUSAMUNE_MEM1_ADDR(0x800ECDE0u, 0x8029985Cu, 0x802916F4u);

const u32 kFf4x = 0x0960u;  // 2400 ticks
const u32 kFf8x = 0x12C0u;  // 4800 ticks

u32  gFfOrig;
u32  gFfLast;
bool gFfCaptured;

void fastForward(bool allowBinds) {
    if (!gFfCaptured) {
        gFfOrig     = *reinterpret_cast<volatile u32 *>(kFfSite);
        gFfLast     = gFfOrig;
        gFfCaptured = true;
    }

    u32 want = gFfOrig;
    if (allowBinds && gBinds.isHeld(BIND_FAST_FORWARD_8X)) {
        want = (gFfOrig & 0xFFFF0000u) | kFf8x;
    } else if (allowBinds && gBinds.isHeld(BIND_FAST_FORWARD_4X)) {
        want = (gFfOrig & 0xFFFF0000u) | kFf4x;
    }

    if (want != gFfLast) {
        // The patched budget affects the next direct(), so revoke credit now.
        if (want != gFfOrig) ILing::invalidateForAssist();
        writeGameCode(kFfSite, want);
        gFfLast = want;
    }
}

}  // namespace

void actionsOnStageLoad() {
    // Stage objects live on the heap that setupObjects replaces.
    gLastHeldObject = nullptr;
}

extern "C" void susamuneOnEggYoshiControl(TEggYoshi *egg) {
    if (gEggKillFrames == 0 || !egg) {
        return;
    }
    TMario *mario = gpMarioAddress;
    if (mario && mario->mYoshi) {
        mario->mYoshi->mEgg = egg;
    }
    // Not a virtual dispatch on purpose: TEggYoshi does not override this, and
    // the slot the gecko calls (vtable+0x104) resolves to exactly this function
    // on all three revisions.
    egg->TMapObjBase::makeObjDead();
}

void actionsApply(bool allowBinds) {
    fastForward(allowBinds);
    if (allowBinds) {
        regrabLastHeldObject();
        spawnYoshiFromBinds();
        positionFromBinds();
    }

    applyEggHook(gEggKillFrames != 0);
    if (gEggKillFrames > 0) {
        gEggKillFrames--;
    }
}

bool actionsFastForwardActive() {
    return gFfCaptured && gFfLast != gFfOrig;
}
