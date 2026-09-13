#include "susamune/practice_session.hxx"
#include "susamune/movement_timing_display.hxx"
#include "susamune/retail_input.hxx"
#include "susamune/state_crc.hxx"
#include "susamune/crash_report.hxx"
// =====================================================================
// savestate.cpp
//
// Emulator-style savestates for Super Mario Sunshine running under
// Nintendont. Hooked once per frame from main.cpp::onUpdate; D-pad LEFT
// snapshots, D-pad RIGHT restores.
//
// What gets snapshotted
// ---------------------
// 1. The current "stage" heap: gpApplication.mCurrentHeap is a
//    JKRSolidHeap (created in TApplication::initialize_nlogoAfter) that
//    fills the rest of the root heap. Almost every per-stage allocation
//    lives here -- TMario, every enemy, MapObj, particles, the scene
//    graph, the camera, etc. Each director cycle the game does a
//    freeAll() on this heap, so its high-water layout is stable for the
//    duration of one scenario.
//
// 2. The "game half" of .bss / .sdata / .sbss. Pointers from heap-resident
//    objects into BSS (and back) need to survive a load, so we restore the
//    parts of BSS that hold mutable game state -- the TFlagManager
//    singleton pointer, gpMarDirector, gpMSound, the libc rand() state,
//    every game module's static counters/caches. The boundaries below were
//    derived from the selected maps/<version>.map: the first game-side modules are
//    MarioUtil.a/DrawUtil.cpp in .bss/.sbss and MoveBG.a/MapObjGeneral.cpp
//    in .sdata. Everything below those addresses is JSystem / JAudio /
//    runtime / OS / DVD / VI / PAD / CARD / GX / SI / EXI / THP / debugger
//    state which we DO NOT touch -- restoring OS thread queues, DVD command
//    queues, audio DSP mailboxes, etc. would crash the console.
//
// What is intentionally not snapshotted
// -------------------------------------
// .data    : almost entirely vtables and static const tables. Read-only at
//            runtime, so no need to copy it back.
// stack    : we are running on it.
// system   : everything before the selected game-side range in each section.
// audio    : MSound has internal queues that reference DSP-side state. We
//            stopAllSound() before save AND before restore so the audio
//            engine never sees inconsistent state.
//
// Invariants for a successful load
// --------------------------------
// - Same build (vtable addresses, BSS layout)
// - Same scenario as the snapshot (same area + episode)
// - Heap object ended up at the same address (deterministic boot)
// - No async DVD / archive load in flight (best-effort: gameplay is
//   normally quiescent)
//
// Where the snapshot lives
// ------------------------
// On Wii via Nintendont, MEM2 cached lives at 0x90000000 / uncached at
// 0xD0000000. The custom launcher reserves a dedicated 16 MiB physical
// window and relocates all Nintendont buffers below it; mem2_map.h is the
// shared source of truth for the PPC mod/loader and ARM kernel. On Dolphin
// we just pick a spot in the emulator's larger virtual space.
// =====================================================================

#include "susamune/savestate.hxx"
#include "susamune/addresses.hxx"
#include "susamune/binds.hxx"
#include "susamune/creation_extras.hxx"
#include "susamune/features.hxx"
#include "susamune/ghost.hxx"
#include "susamune/ghost_storage.hxx"
#include "susamune/ghost_model.hxx"
#include "susamune/mario_colors.hxx"
#include "susamune/fludd_colors.hxx"
#include "susamune/mem2_map.h"
#include "susamune/qft_timer.hxx"
#include "susamune/split_events.hxx"
#include "susamune/split_stats.hxx"
#include "susamune/iling.hxx"
#include "susamune/records.hxx"
#include "susamune/rng_control.hxx"
#include "susamune/movement_display.hxx"
#include "susamune/warp_wheel.hxx"
#include "susamune/menu.hxx"
#include "susamune/settings.hxx"
#include "susamune/state_codec.hxx"
#include "susamune/state_slot_pool.h"
#include "susamune/state_pool_runtime.hxx"
#include "susamune/state_storage.hxx"
#include "susamune/state_archive_profile.hxx"
#include "susamune/state_live_video.hxx"
#include "susamune/state_restore_bindings.hxx"
#include "susamune/state_compatibility.h"
#include "Dolphin/CARD.h"
#include "Dolphin/GX.h"
#include "Dolphin/mem.h"
#include "Dolphin/OS.h"
#include "Dolphin/printf.h"
#include "Dolphin/string.h"
#include "JKernel/JKRHeap.hxx"
#include "JUtility/JUTGamePad.hxx"
#include "SMS/GC2D/SmplFader.hxx"
#include "SMS/MSound/MSound.hxx"
#include "SMS/Manager/FlagManager.hxx"
#include "SMS/Manager/RumbleManager.hxx"
#include "SMS/Player/MarioGamePad.hxx"
#include "SMS/System/Application.hxx"
#include "SMS/System/CardManager.hxx"
#include "SMS/System/MarDirector.hxx"


// ---------------------------------------------------------------------
// Build configuration
// ---------------------------------------------------------------------

#if IS_EMULATOR
// Dolphin: a region in the emulator's "free" space.
static const u32 kStagingBase = SUSAMUNE_DOLPHIN_STATE_STAGING_PPC_BASE;
#else
// Wii: a dedicated 16 MiB window. The custom Nintendont memory map relocates
// all of its former users below this address; the ARM kernel begins exactly at
// the window's exclusive end.
static const u32 kStagingBase = SUSAMUNE_STATE_STAGING_PPC_BASE;
#endif

// How much MEM2 we promise not to step outside of. The actual snapshot is
// (game-bss + game-sdata + game-sbss + heap), which should be under 16MiB.
static const u32 kSnapshotReservedSize = SUSAMUNE_MEM2_SNAPSHOT_SIZE;


// ---------------------------------------------------------------------
// Static memory regions to snapshot (game-side BSS / sdata / sbss).
// Boundaries come from maps/<version>.map. The first game modules are
// MarioUtil.a/DrawUtil.cpp in BSS/SBSS and MoveBG.a/MapObjGeneral.cpp in
// SDATA; every range stops before the JSystem/runtime portion of that linker
// section. Section ends are unsafe: they include live renderer, DSP, heap,
// and OS state that must not be restored.
//
// gpApplication itself sits at the very top of .bss (main.o) and holds
// pointers and scene-id fields TApplication touches every frame, so we
// pull it in as a one-off range.
// ---------------------------------------------------------------------

namespace {

#pragma clang section text=".foxtrot.text" bss=".foxtrot.bss"
extern "C" unsigned char ActivePlayer[];
extern "C" unsigned int THPPlayerCalcNeedMemory();
StateLiveVideo::Range sLiveVideo = {};
StateLiveVideo::Range sVideoReadRing = {};
StateRestoreBindings::Words sRestoreBindings = {};

bool preserveRestoreWord(const void *field) { return sRestoreBindings.add(field); }
bool captureRestoreBindings(u32 first, u32 last, bool durable) {
    sRestoreBindings.reset(first, last);
    return !durable || (MarioColors::preserveSavestateBindings(preserveRestoreWord) &&
        FluddColors::preserveSavestateBindings(preserveRestoreWord) &&
        GhostModel::preserveSavestateBindings(preserveRestoreWord));
}

bool captureLiveVideo() {
    // THPPlayer's threads and queues stay live; its heap buffer must stay with them.
    const u32 *player = reinterpret_cast<const u32 *>(ActivePlayer);
    sVideoReadRing = {0, 0};
    const u32 open = player[0xA0 / 4];
    if (!open) return StateLiveVideo::bufferRange(0, 0, 0, 0, sLiveVideo);
    const u32 onMemory = player[0xB0 / 4];
    if (onMemory > 1) return false;
    if (!StateLiveVideo::bufferRange(open, player[(onMemory ? 0xB4 : 0x100) / 4],
        player[0x9C / 4], THPPlayerCalcNeedMemory(), sLiveVideo)) return false;
    return onMemory || StateLiveVideo::readRingRange(player[0x44 / 4], sLiveVideo, sVideoReadRing);
}

void invalidateVideoReadBuffer() {
    // Saving may read a DVD buffer while DMA fills it. Keep dirty decoder work intact.
    if (sVideoReadRing.last > sVideoReadRing.first)
        DCInvalidateRange(reinterpret_cast<void *>(sVideoReadRing.first),
                          sVideoReadRing.last - sVideoReadRing.first);
}
#pragma clang section text="" bss=""

// A range is captured unconditionally when gate == kNoGate; otherwise it is
// only captured while the named setting is enabled. This lets a menu toggle
// exclude a range from the snapshot (e.g. the libc RNG seed) without touching
// the save/load machinery. Unsigned sentinel deliberately: a -1 in a `char`
// field only reads back as -1 while -fsigned-char is in force.
const u8 kNoGate = 0xFFu;

struct StaticRange {
    u32 start;
    u32 end;
    u8  gate;  // kNoGate, or a SettingId that must be enabled
};

const StaticRange kStaticRanges[] = {
    { SUSAMUNE_ADDR_APPLICATION, SUSAMUNE_ADDR_APPLICATION + sizeof(TApplication), kNoGate },
#if defined(SUSAMUNE_VERSION_JP)
    // The audio and THP blocks hold live thread/JAudio state, so every region
    // snapshots only the game-owned runs around them. Boundaries come directly
    // from each retail link map.
    { SUSAMUNE_ADDR_GAME_BSS_START, 0x803f2c38u, kNoGate }, // .. before MSoundMainSide
    { 0x803f2cf0u,                  0x803f44d0u, kNoGate }, // after MSoundMainSide .. before MSound.a
    { 0x803f57a0u,                  SUSAMUNE_ADDR_GAME_BSS_END, kNoGate }, // after MSound.a ..
#elif defined(SUSAMUNE_VERSION_US)
    { SUSAMUNE_ADDR_GAME_BSS_START, 0x803e9b10u, kNoGate }, // Animal .. before MSound.a
    { 0x803efcb0u, 0x803fd490u, kNoGate }, // after THP .. before MSoundMainSide
#elif defined(SUSAMUNE_VERSION_PAL)
    { SUSAMUNE_ADDR_GAME_BSS_START, 0x803e14d0u, kNoGate }, // Animal .. before MSound.a
    { 0x803e7670u, 0x803f4c30u, kNoGate }, // after THP .. before MSoundMainSide
#endif
#if defined(SUSAMUNE_VERSION_JP)
    { SUSAMUNE_ADDR_GAME_SDATA_START, 0x80408fc0u, kNoGate }, // before MSound.a
    { 0x80408fe8u, SUSAMUNE_ADDR_GAME_SDATA_END, kNoGate },  // after MSound.a

    { SUSAMUNE_ADDR_GAME_SBSS_START, 0x8040a268u, kNoGate }, // before JAL audio lists
    { 0x8040a290u, 0x8040a318u, kNoGate }, // after lists .. before MSoundMainSide
    { 0x8040a348u, 0x8040a4b0u, kNoGate }, // after MSoundMainSide .. before MSound.a
    { 0x8040a4d0u, SUSAMUNE_ADDR_GAME_SBSS_END, kNoGate }, // after MSound.a
#elif defined(SUSAMUNE_VERSION_US)
    { SUSAMUNE_ADDR_GAME_SDATA_START, SUSAMUNE_ADDR_GAME_SDATA_END, kNoGate },

    { SUSAMUNE_ADDR_GAME_SBSS_START, 0x8040cf08u, kNoGate }, // fishoid
    { 0x8040cfd0u, 0x8040d058u, kNoGate }, // Animal after JAL audio lists
    { 0x8040d0a8u, 0x8040e1e8u, kNoGate }, // after THP .. before MSoundMainSide
    { 0x8040e220u, SUSAMUNE_ADDR_GAME_SBSS_END, kNoGate }, // TargetArrow
#elif defined(SUSAMUNE_VERSION_PAL)
    { SUSAMUNE_ADDR_GAME_SDATA_START, SUSAMUNE_ADDR_GAME_SDATA_END, kNoGate },

    { SUSAMUNE_ADDR_GAME_SBSS_START, 0x80404668u, kNoGate }, // fishoid
    { 0x80404730u, 0x804047b8u, kNoGate }, // Animal after JAL audio lists
    { 0x80404808u, 0x804058c0u, kNoGate }, // after THP .. before MSoundMainSide
    { 0x804058f8u, SUSAMUNE_ADDR_GAME_SBSS_END, kNoGate }, // TargetArrow
#endif
    // MSL rand.c `next` -- the seed for libc rand()/srand(), which every
    // gameplay RNG funnels through (MarioUtil MsRandF/MsRandI, so King Boo's
    // fruit pulls, Gooper Blooper / manta patterns, enemy timers). It sits
    // below the game-sdata boundary, so it needs its own row. A plain counter
    // with no hardware linkage, so restoring it is safe. Off leaves the seed
    // advancing across a load instead of rewinding with the state.
    { SUSAMUNE_ADDR_LIBC_RAND_SEED, SUSAMUNE_ADDR_LIBC_RAND_SEED + sizeof(u32),
      SETTING_SAVE_RNG_STATE },
};
const int kNumStaticRanges = sizeof(kStaticRanges) / sizeof(kStaticRanges[0]);

// Pointed-to allocations: globals in BSS that hold a pointer to a
// root-heap-allocated object the game mutates every frame. The static
// ranges above already preserve the *pointer* (it lives in BSS), but the
// pointed-to *bytes* are on the root heap, which is outside our heap
// snapshot. So we follow each pointer at save time and capture its
// target as an additional region.
struct PointedAlloc {
    u32 ptr_addr;     // address of the global pointer (in BSS)
    u32 size;         // bytes to capture from *(*ptr_addr)
};

const PointedAlloc kPointedAllocs[] = {
    // TFlagManager::smInstance -- coin counts, shines, episode flags,
    // life count, etc. Without this, coin pickups are forgotten on load.
    { SUSAMUNE_ADDR_FLAG_MANAGER_INSTANCE, sizeof(TFlagManager) },
    // TTimeRec::_instance -- the input/profiler recorder. Object size is
    // 0x820 bytes; the constructor argument 0xDFC0 is unrelated to it.
    { SUSAMUNE_ADDR_TIME_REC_INSTANCE,     0x820u },
    // SMSRumbleMgr -- rumble channels' active state.
    { SUSAMUNE_ADDR_RUMBLE_MANAGER,        sizeof(RumbleMgr) },
    // gpApplication.mGamePads[0..3] -- the four TMarioGamePad objects on
    // the root heap. The pointers themselves live inside the gpApplication
    // static range, but the per-pad button-meaning state machine
    // (mMeaning, mFrameMeaning, mState.mDisable, mState.mIsTalking, ...)
    // mutates every frame. Without restoring it, reloading while a dialog
    // had disabled the pad leaves the meaning bits stuck.
    // Address = &gpApplication + offsetof(TApplication, mGamePads[i]).
    { SUSAMUNE_ADDR_APPLICATION_GAMEPAD(0), sizeof(TMarioGamePad) },
    { SUSAMUNE_ADDR_APPLICATION_GAMEPAD(1), sizeof(TMarioGamePad) },
    { SUSAMUNE_ADDR_APPLICATION_GAMEPAD(2), sizeof(TMarioGamePad) },
    { SUSAMUNE_ADDR_APPLICATION_GAMEPAD(3), sizeof(TMarioGamePad) },
    // gpApplication.mFader -- the screen fader's animation state lives on
    // the root heap. Without this the fader gets stuck mid-fade when you
    // load from inside a transition (shine-get fadeout, save card screen).
    // Address = &gpApplication + offsetof(TApplication, mFader).
    { SUSAMUNE_ADDR_APPLICATION_FADER, sizeof(TSmplFader) },
};
const int kNumPointedAllocs = sizeof(kPointedAllocs) / sizeof(kPointedAllocs[0]);

// One header lives at the very start of the snapshot buffer; the saved
// bytes follow at kHeaderSize.
const u32 kSnapshotMagic   = 0x53555341u; // 'SUSA'
const u32 kSnapshotVersion = 17u;
const u32 kHeaderSize      = 0x120u;
// One slot per static range, one per pointed alloc, plus one for the heap.
const int kMaxRegions      = kNumStaticRanges + kNumPointedAllocs + 1;
static_assert(kMaxRegions + Ghost::kSavestateSpanCount + PracticeSession::kSavestateSpanCount <= StateCodec::kMaxSpans,
              "game, ghost and practice state spans exceed the codec limit");

struct RegionEntry {
    u32 addr;       // virtual address restored to
    u32 size;       // bytes
    u32 buf_offset; // offset from snapshot buffer base (after header)
};

struct SavestateHeader {
    u32 magic;
    u32 version;
    u32 game_version;
    u32 heap_addr;        // value of gpApplication.mCurrentHeap at save time
    u32 heap_size;        // bytes between heap and heap->mEnd
    u8  area_id;
    u8  episode_id;
    u8  feature_state;
    u8  _pad0;
    u32 region_count;
    // OSGetTime() at save. TMarDirector::mStopwatch (the Piantissimo-chase /
    // blooper-race mission countdown) stores an absolute console-uptime
    // timestamp in mLast; a byte-for-byte restore of that field is not
    // enough; see the mission-timer correction in loadState().
    OSTime save_time;
    RegionEntry regions[kMaxRegions];
};
static_assert(sizeof(SavestateHeader) <= kHeaderSize,
              "savestate header no longer fits in its reserved space");

struct StoredState {
    SavestateHeader header;
    QFTTimer::SavestateData timer;
    ILing::SavestateData attempt;
    Ghost::SavestateData ghost;
    PracticeSession::SavestateData practice;
    StateArchiveProfile::Data archiveProfile;
    u32 generation;
    u32 rawSize;
    u32 packedSize;
    u32 adler32;
    u32 parentEpisode;
    u32 metadataTag;
};
static_assert(sizeof(StoredState) <= SUSAMUNE_STATE_METADATA_SIZE,
              "state archive metadata exceeds its mailbox");

StateSlotPool sPool;
StatePoolMemory sPoolMemory;
StoredState (&sSlots)[SavestateManager::kSlotCount] =
    *reinterpret_cast<StoredState (*)[SavestateManager::kSlotCount]>(SUSAMUNE_STATE_METADATA_PPC_BASE);
StoredState &sCandidate = *reinterpret_cast<StoredState *>(
    SUSAMUNE_STATE_METADATA_PPC_BASE + sizeof(sSlots));
// Live ownership survives restores; it must never share the archived records.
StateArchiveProfile::Data &sLiveArchiveProfile = *reinterpret_cast<StateArchiveProfile::Data *>(
    SUSAMUNE_STATE_METADATA_PPC_BASE + SUSAMUNE_STATE_LIVE_PROFILE_OFFSET);
bool sMetadataReady;
static_assert(SUSAMUNE_STATE_METADATA_SIZE * (SavestateManager::kSlotCount + 1) <=
                  SUSAMUNE_STATE_LIVE_PROFILE_OFFSET,
              "state metadata overlaps live owner profile");
static_assert(sizeof(sLiveArchiveProfile) == SUSAMUNE_STATE_LIVE_PROFILE_SIZE,
              "live state owner profile size changed");
static_assert(SUSAMUNE_GHOST_INPUT_MAX_COUNT * sizeof(SusamuneGhostInputSample) <=
                  SUSAMUNE_STATE_METADATA_OFFSET, "state metadata overlaps ghost inputs");
u32 sActiveSlot;
u32 sLoadSlot;
SusamuneStateCatalogEntry sSelectedSD, sPendingSD;
u32 sNextGeneration;
u32 sPendingSlot;
u32 sPendingGeneration;
bool sAwaitingLoadApproval;
bool sBusy;
u32 sDurableSlots;
u32 sPackedChecksums[SavestateManager::kSlotCount];
u32 sDiskSlot, sDiskGeneration, sDiskPoolUsed, sDiskScene;
OSTime sDiskStarted;
bool sDiskActive;
bool sExplicitTransfer, sTransferReady;
SavestateManager::TransferResult sTransferResult;
u32 sProjectStartKey[2], sProjectFrames, sProjectRole, sProjectScene;
bool sDiskRestore, sDiskLoadReady;
bool sDiskStream, sDiskRecovered;
SusamuneStateArchiveHeader sStreamHeader;
const u32 kStreamWindows = (SUSAMUNE_STATE_POOL_EXPANDED_SIZE + SUSAMUNE_STATE_STAGING_SIZE - 1) / SUSAMUNE_STATE_STAGING_SIZE;
static_assert(kStreamWindows <= 32, "SD window bitmap exceeded");
u32 sStreamChecksums[kStreamWindows], sStreamSeen, sStreamOffset, sStreamSize, sStreamCrc, sStreamId;
bool sStreamCommit;
const char *sDiskStatus = "SD states ready";
static_assert(STATE_SLOT_POOL_COUNT == SavestateManager::kSlotCount,
              "state slot counts differ");
static_assert(StateCodec::kWorkspaceLimit <= SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,
              "state codec workspace overlaps packed states");

bool stateMetadataAvailable() {
#if IS_EMULATOR
    return true;
#else
    const volatile SusamuneCfg *cfg = SUSAMUNE_CFG_PPC_PTR;
    const u32 flags = SUSAMUNE_CFG_FLAG_STATE_POOL_EXPANSION | SUSAMUNE_CFG_FLAG_STATE_CODEC_RELOCATED;
    if (cfg->magic != SUSAMUNE_CFG_MAGIC || cfg->version != SUSAMUNE_CFG_VERSION ||
        (cfg->flags & flags) != flags) return false;
    volatile SusamuneGhostStorageMailbox *mailbox = SUSAMUNE_GHOST_STORAGE_PPC_PTR;
    DCInvalidateRange((void *)&mailbox->response, 32);
    return mailbox->response.responseMagic == SUSAMUNE_GHOST_STORAGE_MAGIC &&
        mailbox->response.protocolVersion == SUSAMUNE_GHOST_STORAGE_VERSION;
#endif
}

void initStateMetadata() {
    sMetadataReady = stateMetadataAvailable();
    if (!sMetadataReady) return;
    memset(sSlots, 0, sizeof(sSlots));
    memset(&sCandidate, 0, sizeof(sCandidate));
}

u32 poolCapacity() { return StatePoolMemoryCapacity(&sPoolMemory); }

void poolWriteSpans(u32 offset, u32 size, StateCodec::WriteSpan *out) {
    out[0] = out[1] = {nullptr, 0};
    for (u32 i = 0; size && i < 2; ++i) {
        StatePoolMemorySpan span;
        if (!StatePoolMemorySpanAt(&sPoolMemory, offset, size, &span)) __builtin_trap();
        out[i] = {span.data, span.size};
        offset += span.size;
        size -= span.size;
    }
    if (size) __builtin_trap();
}

void poolReadSpans(u32 offset, u32 size, StateCodec::ReadSpan *out) {
    StateCodec::WriteSpan pieces[2];
    poolWriteSpans(offset, size, pieces);
    for (u32 i = 0; i < 2; ++i) out[i] = {pieces[i].data, pieces[i].size};
}

void *codecWorkspace() {
    return stateCodecWorkspace(sPoolMemory);
}

u32 packedChecksum(u32 offset, u32 size) {
    StateCodec::ReadSpan spans[2];
    poolReadSpans(offset, size, spans);
    void *workspace = codecWorkspace();
    if (!StateCrc::init(workspace, SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE)) __builtin_trap();
    u32 crc = 0xFFFFFFFFu;
    for (u32 i = 0; i < 2; ++i)
        crc = StateCrc::update(workspace, crc, spans[i].data, spans[i].size);
    return ~crc;
}

bool commitPackedState(const StateCodec::ReadSpan *source, u32 sourceCount,
                       u32 rawSize, u32 slot, const StateCodec::Result &first,
                       bool compact = false, bool quick = false) {
    if ((first.status != StateCodec::SUCCESS && first.status != StateCodec::OUTPUT_FULL) ||
        first.rawBytes != rawSize) return false;
    const int plan = StateSlotPoolPlanReplace(&sPool, poolCapacity(),
        slot, first.compressedBytes, SUSAMUNE_STATE_STAGING_SIZE);
    if (plan == STATE_SLOT_REPLACE_STAGED && first.status == StateCodec::SUCCESS)
        return StateSlotPoolCommitBanked(&sPool, &sPoolMemory,
            slot, first.compressedBytes, reinterpret_cast<const u8 *>(kStagingBase),
            SUSAMUNE_STATE_STAGING_SIZE);
    if (plan != STATE_SLOT_REPLACE_RECOMPRESS || first.status != StateCodec::OUTPUT_FULL)
        return false;

    // Interrupts remain off: the complete first pass proved this immutable input fits.
    if (!StateSlotPoolReclaimForReplaceBanked(&sPool, &sPoolMemory,
                                        slot, first.compressedBytes)) __builtin_trap();
    StateCodec::WriteSpan output[2];
    poolWriteSpans(sPool.used, poolCapacity() - sPool.used, output);
    const StateCodec::Result second = StateCodec::compress(codecWorkspace(),
        SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE, source, sourceCount, output, 2, compact, quick);
    if (second.status != StateCodec::SUCCESS || second.rawBytes != first.rawBytes ||
        second.compressedBytes != first.compressedBytes || second.adler32 != first.adler32)
        __builtin_trap();
    if (!StateSlotPoolCommitPreparedBanked(&sPool, &sPoolMemory,
                                     slot, second.compressedBytes)) __builtin_trap();
    return true;
}

u32 metadataTag(const StoredState &slot) {
    const u8 *bytes = reinterpret_cast<const u8 *>(&slot);
    u32 value = 2166136261u;
    for (u32 i = 0; i < __builtin_offsetof(StoredState, metadataTag); ++i)
        value = (value ^ bytes[i]) * 16777619u;
    return value;
}

bool validStore() {
    if (!sMetadataReady) return false;
    if (!StateSlotPoolValid(&sPool, poolCapacity())) return false;
    for (u32 i = 0; i < SavestateManager::kSlotCount; ++i) {
        const StoredState &slot = sSlots[i];
        if (!sPool.slots[i].size) {
            if (slot.header.magic) return false;
            continue;
        }
        if (slot.header.magic != kSnapshotMagic || !slot.generation ||
            slot.packedSize != sPool.slots[i].size ||
            slot.metadataTag != metadataTag(slot)) return false;
    }
    return true;
}

bool compressCandidate(const StateCodec::ReadSpan *source, u32 sourceCount,
                       u32 rawSize, u32 slot, StateCodec::Result &result) {
    StateCodec::WriteSpan output[3];
    output[0] = {reinterpret_cast<void *>(kStagingBase), SUSAMUNE_STATE_STAGING_SIZE};
    poolWriteSpans(sPool.used, poolCapacity() - sPool.used, output + 1);
    result = StateCodec::compress(codecWorkspace(), SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,
        source, sourceCount, output, 3, false, true);
    bool quick = true;
    bool compact = false;
    if ((result.status == StateCodec::SUCCESS || result.status == StateCodec::OUTPUT_FULL) &&
        StateSlotPoolPlanReplace(&sPool, poolCapacity(), slot, result.compressedBytes,
                                 SUSAMUNE_STATE_STAGING_SIZE) == STATE_SLOT_REPLACE_REFUSE) {
        quick = false;
        result = StateCodec::compress(codecWorkspace(), SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,
            source, sourceCount, output, 3);
    }
    if ((result.status == StateCodec::SUCCESS || result.status == StateCodec::OUTPUT_FULL) &&
        StateSlotPoolPlanReplace(&sPool, poolCapacity(), slot, result.compressedBytes,
                                 SUSAMUNE_STATE_STAGING_SIZE) == STATE_SLOT_REPLACE_REFUSE) {
        compact = true;
        result = StateCodec::compress(codecWorkspace(), SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,
            source, sourceCount, output, 3, compact);
    }
    return commitPackedState(source, sourceCount, rawSize, slot, result, compact, quick);
}

bool quickStoredState(u32 slot) {
    u8 magic[4];
    return sPool.slots[slot].size >= sizeof(magic) &&
        StatePoolMemoryCopyOut(&sPoolMemory, sPool.slots[slot].offset, magic, sizeof(magic)) &&
        magic[0] == 'M' && magic[1] == 'S' && magic[2] == 'L' && magic[3] == '4';
}

bool repackRetainedState(u32 slot, bool compact) {
    StoredState &saved = sSlots[slot];
    if (packedChecksum(sPool.slots[slot].offset, saved.packedSize) != sPackedChecksums[slot])
        return false;
    u8 *packWorkspace = reinterpret_cast<u8 *>(kStagingBase);
    u8 *staging = packWorkspace + SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE;
    const u32 stagingSize = SUSAMUNE_STATE_STAGING_SIZE - SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE;
    StateCodec::ReadSpan source[2];
    poolReadSpans(sPool.slots[slot].offset, saved.packedSize, source);
    StateCodec::WriteSpan output[3] = {{staging, stagingSize}, {nullptr, 0}, {nullptr, 0}};
    poolWriteSpans(sPool.used, poolCapacity() - sPool.used, output + 1);
    const StateCodec::Result result = StateCodec::repack(codecWorkspace(),
        SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE, packWorkspace, SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,
        source, 2, output, 3, saved.rawSize, saved.adler32, compact);
    // A retained source is never reclaimed to make room for its replacement.
    if (result.status != StateCodec::SUCCESS || result.rawBytes != saved.rawSize ||
        result.adler32 != saved.adler32 || result.compressedBytes >= saved.packedSize ||
        !StateSlotPoolCommitBanked(&sPool, &sPoolMemory, slot, result.compressedBytes,
                                  staging, stagingSize)) return false;
    saved.packedSize = result.compressedBytes;
    saved.metadataTag = metadataTag(saved);
    sPackedChecksums[slot] = packedChecksum(sPool.slots[slot].offset, saved.packedSize);
    return true;
}

bool repackForCandidate(const StateCodec::ReadSpan *source, u32 sourceCount,
                        u32 rawSize, u32 slot, StateCodec::Result &result) {
    if ((result.status != StateCodec::SUCCESS && result.status != StateCodec::OUTPUT_FULL) ||
        result.rawBytes != rawSize ||
        StateSlotPoolPlanReplace(&sPool, poolCapacity(), slot, result.compressedBytes,
                                 SUSAMUNE_STATE_STAGING_SIZE) != STATE_SLOT_REPLACE_REFUSE)
        return false;
    u32 retainedSlots = 0, quickSlots = 0;
    for (u32 i = 0; i < SavestateManager::kSlotCount; ++i) {
        if (i == slot || !sPool.slots[i].size) continue;
        retainedSlots |= 1u << i;
        if (quickStoredState(i)) quickSlots |= 1u << i;
    }
    for (u32 mode = 0; mode < 2; ++mode) {
        for (u32 i = 0; i < SavestateManager::kSlotCount; ++i) {
            if (!((mode ? retainedSlots : quickSlots) & (1u << i)) ||
                !repackRetainedState(i, mode != 0)) continue;
            if (StateSlotPoolPlanReplace(&sPool, poolCapacity(), slot, result.compressedBytes,
                                         SUSAMUNE_STATE_STAGING_SIZE) == STATE_SLOT_REPLACE_REFUSE)
                continue;
            if (compressCandidate(source, sourceCount, rawSize, slot, result)) return true;
            if ((result.status != StateCodec::SUCCESS && result.status != StateCodec::OUTPUT_FULL) ||
                result.rawBytes != rawSize) return false;
        }
    }
    return false;
}

u32 nextGeneration() {
    if (++sNextGeneration == 0) ++sNextGeneration;
    return sNextGeneration;
}

u32 parentEpisode() {
    return TFlagManager::smInstance
        ? TFlagManager::smInstance->getFlag(0x40003) : 0;
}

#pragma clang section text=".foxtrot.text"
u32 archiveBuildId() {
    return SUSAMUNE_STATE_COMPATIBILITY_ID;
}
__attribute__((noinline)) bool archiveBuildCompatible(u32 build) {
    return SusamuneStateBuildCompatible(SUSAMUNE_GAME_VERSION, build);
}
#pragma clang section text=""

u32 archiveGameId() {
    return SUSAMUNE_GAME_VERSION == 1 ? 0x474D534Au :
           SUSAMUNE_GAME_VERSION == 2 ? 0x474D5345u : 0x474D5350u;
}

u32 archiveSceneKey() {
    return (static_cast<u32>(gpApplication.mCurrentScene.mAreaID) << 24) |
        (static_cast<u32>(gpApplication.mCurrentScene.mEpisodeID) << 16) |
        (parentEpisode() & 0xFFFFu);
}

void captureArchiveProfile(StateArchiveProfile::Data &out) {
    if (!StateStorage::available() || !archiveBuildId() ||
        !StateArchiveProfile::capture(out, archiveBuildId(), StateStorage::configId()))
        memset(&out, 0, sizeof(out));
}

void rebaseMissionStopwatch(OSTime previousTime) {
    if (!gpMarDirector) return;
    gpMarDirector->mStopwatch.mLast += OSGetTime() - previousTime;
    DCStoreRange(&gpMarDirector->mStopwatch, sizeof(OSStopwatch));
}

__attribute__((noinline)) u32 captureRegion(SavestateHeader *h, u32 offset,
                                            u32 addr, u32 size) {
    RegionEntry &region = h->regions[h->region_count++];
    region.addr         = addr;
    region.size         = size;
    region.buf_offset   = offset;
    return offset + size;
}

bool consumeExpectedRegion(const SavestateHeader *h, u32 *index, u32 *offset,
                           u32 addr, u32 size) {
    if (*index >= h->region_count || size == 0) return false;
    const RegionEntry &region = h->regions[*index];
    const u32 capacity = kSnapshotReservedSize - kHeaderSize;
    if (region.addr != addr || region.size != size ||
        region.buf_offset != *offset || *offset > capacity ||
        size > capacity - *offset || addr > 0xffffffffu - size) {
        return false;
    }
    *offset += size;
    ++*index;
    return true;
}

bool validSnapshotRegions(const SavestateHeader *h, u32 heapStart,
                          u32 heapEnd) {
    if (h->region_count == 0 || h->region_count > (u32)kMaxRegions ||
        heapStart < 0x80000000u || heapEnd > 0x81800000u ||
        heapEnd <= heapStart) {
        return false;
    }

    u32 index = 0;
    u32 offset = 0;
    for (int i = 0; i < kNumStaticRanges; i++) {
        const StaticRange &range = kStaticRanges[i];
        const u32 size = range.end - range.start;
        // A gated range may legitimately be absent if its setting was off at
        // save time. Every present entry still has to match the compiled map.
        if (range.gate != kNoGate &&
            (index >= h->region_count ||
             h->regions[index].addr != range.start)) {
            continue;
        }
        if (!consumeExpectedRegion(h, &index, &offset, range.start, size))
            return false;
    }

    for (int i = 0; i < kNumPointedAllocs; i++) {
        const PointedAlloc &alloc = kPointedAllocs[i];
        const u32 target = *reinterpret_cast<const u32 *>(alloc.ptr_addr);
        if (target == 0) continue;
        const bool inHeap = target >= heapStart && target < heapEnd &&
                            alloc.size <= heapEnd - target;
        if (inHeap) continue;
        // All tracked root-heap allocations must remain inside physical MEM1.
        if (target < 0x80000000u || target >= 0x81800000u ||
            alloc.size > 0x81800000u - target) {
            return false;
        }
        if (!consumeExpectedRegion(h, &index, &offset, target, alloc.size))
            return false;
    }

    const u32 heapSize = heapEnd - heapStart;
    return consumeExpectedRegion(h, &index, &offset, heapStart, heapSize) &&
           index == h->region_count;
}

// ---------------------------------------------------------------------
// Hardware audio mute
// ---------------------------------------------------------------------
// The snapshot copy runs with interrupts disabled (see save/loadState). For
// a multi-megabyte heap that is several ms during which the DSP-driven audio
// DMA never gets its refill interrupt, so the DAC just replays its last block
// -> an unpleasant hiccup/buzz. stopAllSound() only quiets the software mixer;
// it does not stop the DMA that is already feeding the DAC.
//
// The master audio-out DMA enable is bit 0x8000 of DSPRegs[27] (0xCC005000 +
// 27*2). This is exactly the bit AIStartDMA sets / AIStopDMA clears -- SMS's
// own audio interrupt handler toggles it every block. Clearing it silences
// the DAC immediately at the hardware level, which is the only mute that
// survives our interrupts-disabled window; setting it back resumes output.
// (AIStopDMA itself is stripped from the game binary, so we poke the register
// directly. The 0xCC00_xxxx MMIO block is uncached, so no cache handling.)
volatile u16 *const kDspRegs      = reinterpret_cast<volatile u16 *>(0xCC005000u);
const u16           kAiDmaEnable  = 0x8000u;

inline bool muteAudioDma() {
    bool wasOn = (kDspRegs[27] & kAiDmaEnable) != 0;
    kDspRegs[27] = kDspRegs[27] & ~kAiDmaEnable;
    return wasOn;
}
inline void unmuteAudioDma(bool wasOn) {
    if (wasOn) {
        kDspRegs[27] = kDspRegs[27] | kAiDmaEnable;
    }
}

void reconcilePauseAudio(u8 previousState) {
    if (!gpMSound || !gpMarDirector) return;
    const u8 restoredState = gpMarDirector->mCurState;
    if (restoredState == TMarDirector::STATE_PAUSE_MENU) {
        if (previousState != TMarDirector::STATE_PAUSE_MENU) gpMSound->pauseOn(false);
    } else if (previousState == TMarDirector::STATE_PAUSE_MENU &&
               restoredState == TMarDirector::STATE_NORMAL && !gpMarDirector->mDemoState) {
        // JAudio is retained across a load, so bypassing retail unpause must
        // release its category mute through the same silent retail API.
        gpMSound->pauseOff(2);
    }
}

// ---------------------------------------------------------------------
// Load-in-flight / transition gate
// ---------------------------------------------------------------------
// Snapshotting during a stage load or the opening sequence captures (or
// scribbles over) a heap that the async setup thread is still populating ->
// crash. TMarDirector::direct() returns early every frame while _260 == 0,
// which is precisely the window where the setup thread (gSetupThread) has not
// been joined yet -- i.e. the all-black "loading" screen. It flips to 1 only
// after the load completes.
//
// After that, the opening runs: STATE_INTRO_INIT (0) then, on stages with an
// intro, STATE_INTRO_PLAYING (1) -- the demo-camera cutscene, which still
// crashed when snapshotted. We block those two. Once the intro's closing/
// opening wipe fades the stage back in, the director reaches
// STATE_GAME_STARTING (2) / the opening-wipe state (3), where Mario plays his
// materialise-in animation before the "GO": the whole stage is loaded and
// visible by then (SMS loads everything up front -- nothing streams in
// dynamically), so snapshotting is safe. Allow state >= 2.
bool inLoadTransition() {
    if (!gpMarDirector || RetailInput::stageDirector() != gpMarDirector) {
        return true;
    }
    if (gpMarDirector->_260 == 0) {
        return true; // setup thread still loading -- all-black screen
    }
    if (gpMarDirector->mCurState < TMarDirector::STATE_GAME_STARTING) {
        return true; // black init (0) / intro-cutscene (1) still playing
    }
    return false;
}

bool archiveStageReady() {
    return !inLoadTransition() && gpMarDirector->mCurState == TMarDirector::STATE_NORMAL;
}

bool admitArchiveStage() {
    if (archiveStageReady()) return true;
    sDiskStatus = "Return to normal play before using SD states";
    if (gMenu) gMenu->toast(sDiskStatus);
    return false;
}

bool archiveCandidateMatches(const SusamuneStateArchiveHeader &file) {
    const SavestateHeader &h = sCandidate.header;
    if (file.metadataSize != sizeof(sCandidate) || !archiveBuildCompatible(file.buildCrc) ||
        sCandidate.archiveProfile.build != file.buildCrc || sCandidate.archiveProfile.config != file.configId ||
        file.gameId != archiveGameId() || file.snapshotVersion != kSnapshotVersion ||
        file.sceneKey != archiveSceneKey() || file.packedSize != sCandidate.packedSize ||
        file.rawSize != sCandidate.rawSize || h.magic != kSnapshotMagic ||
        h.version != kSnapshotVersion || h.game_version != SUSAMUNE_GAME_VERSION ||
        sCandidate.metadataTag != metadataTag(sCandidate) || !sCandidate.generation ||
        h.area_id != gpApplication.mCurrentScene.mAreaID ||
        h.episode_id != gpApplication.mCurrentScene.mEpisodeID ||
        sCandidate.parentEpisode != parentEpisode() || inLoadTransition()) return false;
    JKRHeap *heap = gpApplication.mCurrentHeap;
    const u32 begin = reinterpret_cast<u32>(heap);
    if (!heap || begin < 0x80000000u || begin > 0x81800000u - sizeof(JKRHeap)) return false;
    const u32 end = reinterpret_cast<u32>(heap->mEnd);
    if (end <= begin || end > 0x81800000u || h.heap_addr != begin || h.heap_size != end - begin ||
        !validSnapshotRegions(&h, begin, end)) return false;
    StateCodec::WriteSpan ghost[Ghost::kSavestateSpanCount];
    if (!Ghost::savestateRestoreSpans(sCandidate.ghost, ghost)) return false;
    StateCodec::WriteSpan practice[PracticeSession::kSavestateSpanCount];
    if (!PracticeSession::savestateRestoreSpans(sCandidate.practice, practice)) return false;
    u32 raw = h.regions[h.region_count - 1].buf_offset + h.heap_size;
    for (u32 i = 0; i < Ghost::kSavestateSpanCount; ++i) raw += ghost[i].size;
    for (u32 i = 0; i < PracticeSession::kSavestateSpanCount; ++i) raw += practice[i].size;
    if (raw != sCandidate.rawSize) return false;
    captureArchiveProfile(sLiveArchiveProfile);
    return StateArchiveProfile::matches(sCandidate.archiveProfile, sLiveArchiveProfile);
}

bool archiveProjectCandidateMatches(const SusamuneStateArchiveHeader &file) {
    const SavestateHeader &h = sCandidate.header;
    if (file.metadataSize != sizeof(sCandidate) || !archiveBuildCompatible(file.buildCrc) ||
        file.gameId != archiveGameId() || file.snapshotVersion != kSnapshotVersion ||
        file.sceneKey != sProjectScene || file.packedSize != sCandidate.packedSize ||
        file.rawSize != sCandidate.rawSize || h.magic != kSnapshotMagic ||
        h.version != kSnapshotVersion || h.game_version != SUSAMUNE_GAME_VERSION ||
        sCandidate.metadataTag != metadataTag(sCandidate) || !sCandidate.generation ||
        file.sceneKey != ((static_cast<u32>(h.area_id) << 24) | (static_cast<u32>(h.episode_id) << 16) |
                          (sCandidate.parentEpisode & 0xFFFFu)) ||
        h.heap_addr < 0x80000000u || h.heap_addr >= 0x81800000u ||
        !h.heap_size || h.heap_size > 0x81800000u - h.heap_addr ||
        !validSnapshotRegions(&h, h.heap_addr, h.heap_addr + h.heap_size) ||
        !StateArchiveProfile::valid(sCandidate.archiveProfile) ||
        sCandidate.archiveProfile.game != SUSAMUNE_GAME_VERSION ||
        sCandidate.archiveProfile.build != file.buildCrc || sCandidate.archiveProfile.config != file.configId) return false;
    StateCodec::WriteSpan ghost[Ghost::kSavestateSpanCount];
    StateCodec::WriteSpan practice[PracticeSession::kSavestateSpanCount];
    if (!Ghost::savestateRestoreSpans(sCandidate.ghost, ghost) ||
        !PracticeSession::savestateRestoreSpans(sCandidate.practice, practice)) return false;
    u32 raw = h.regions[h.region_count - 1].buf_offset + h.heap_size;
    for (u32 i = 0; i < Ghost::kSavestateSpanCount; ++i) raw += ghost[i].size;
    for (u32 i = 0; i < PracticeSession::kSavestateSpanCount; ++i) raw += practice[i].size;
    // Import only retains validated bytes; loadSlot still checks live owners.
    return raw == sCandidate.rawSize;
}

#pragma clang section text=".foxtrot.text"
void copyBaseStateBytes(void *profile, void *destination, const void *source, unsigned int size) {
    if (profile) StateArchiveProfile::copyGameBytes(profile, destination, source, size);
    else memcpy(destination, source, size);
}

void copyOwnedStateBytes(void *profile, void *destination, const void *source, unsigned int size) {
    StateRestoreBindings::copyExcept(sRestoreBindings, profile, destination, source, size, copyBaseStateBytes);
}

void copyStateBytes(void *profile, void *destination, const void *source, unsigned int size) {
    if (PracticeSession::copySavestateBytes(destination, source, size)) return;
    StateLiveVideo::copyExcept(sLiveVideo, profile, destination, source, size, copyOwnedStateBytes);
}

#pragma clang section text=".foxtrot.text"
bool waitStateWindow(StateStorage::Result &result) {
    const OSTime started = OSGetTime();
    for (;;) {
        StateStorage::update();
        if (StateStorage::takeResult(result)) return result.status == SUSAMUNE_STATE_OK;
        if (OSGetTime() - started > OSSecondsToTicks(15)) {
            StateStorage::cancel();
            return false;
        }
    }
}

bool readStateWindow(void *, unsigned int offset, StateCodec::ReadSpan *out) {
    if (offset >= sCandidate.packedSize) return false;
    const u32 window = offset / SUSAMUNE_STATE_STAGING_SIZE;
    if (window >= kStreamWindows) return false;
    const u32 begin = window * SUSAMUNE_STATE_STAGING_SIZE;
    if (!sStreamSize || sStreamOffset != begin) {
        const u32 remaining = sCandidate.packedSize - begin;
        const u32 size = remaining < SUSAMUNE_STATE_STAGING_SIZE ? remaining : SUSAMUNE_STATE_STAGING_SIZE;
        StateStorage::Result result;
        if (!StateStorage::startWindow(sStreamId, sStreamHeader.headerCrc,
                sCandidate.packedSize, begin, size) || !waitStateWindow(result) ||
            result.command != SUSAMUNE_STATE_CMD_READ_WINDOW ||
            memcmp(&result.header, &sStreamHeader, sizeof(sStreamHeader)) || !result.metadata ||
            memcmp(result.metadata, &sCandidate, sizeof(sCandidate))) return false;
        const void *bytes = reinterpret_cast<const void *>(kStagingBase);
        const u32 checksum = SusamuneStateCrc(bytes, size);
        if (checksum != result.window.checksum) return false;
        if (sStreamCommit) {
            if (!(sStreamSeen & (1u << window)) || sStreamChecksums[window] != checksum) return false;
        } else if (!(sStreamSeen & (1u << window))) {
            sStreamChecksums[window] = checksum;
            sStreamSeen |= 1u << window;
            sStreamCrc = SusamuneStateCrcUpdate(sStreamCrc, bytes, size);
        } else if (sStreamChecksums[window] != checksum) return false;
        sStreamOffset = begin;
        sStreamSize = size;
    }
    *out = {reinterpret_cast<const u8 *>(kStagingBase) + offset - begin,
            sStreamSize - (offset - begin)};
    return true;
}

struct SDRecovery {
    u32 slot, count;
    StateCodec::ReadSpan source[2];
    StateCodec::WriteSpan destination[kMaxRegions + Ghost::kSavestateSpanCount + PracticeSession::kSavestateSpanCount];
};

bool prepareSDRecovery(SDRecovery &recovery, u32 heapStart, u32 heapEnd) {
    for (u32 n = 0; n < SavestateManager::kSlotCount; ++n) {
        const u32 slot = (sLoadSlot + n) % SavestateManager::kSlotCount;
        const StoredState &saved = sSlots[slot];
        const SavestateHeader &h = saved.header;
        if (!sPool.slots[slot].size || h.version != kSnapshotVersion || h.game_version != SUSAMUNE_GAME_VERSION ||
            h.heap_addr != heapStart || h.heap_size != heapEnd - heapStart ||
            h.area_id != gpApplication.mCurrentScene.mAreaID || h.episode_id != gpApplication.mCurrentScene.mEpisodeID ||
            saved.parentEpisode != parentEpisode() || !validSnapshotRegions(&h, heapStart, heapEnd) ||
            !StateArchiveProfile::matches(saved.archiveProfile, sLiveArchiveProfile)) continue;
        StateCodec::WriteSpan ghost[Ghost::kSavestateSpanCount];
        if (!Ghost::savestateRestoreSpans(saved.ghost, ghost)) continue;
        StateCodec::WriteSpan practice[PracticeSession::kSavestateSpanCount];
        if (!PracticeSession::savestateRestoreSpans(saved.practice, practice)) continue;
        u32 raw = h.regions[h.region_count - 1].buf_offset + h.heap_size;
        for (u32 i = 0; i < Ghost::kSavestateSpanCount; ++i) raw += ghost[i].size;
        for (u32 i = 0; i < PracticeSession::kSavestateSpanCount; ++i) raw += practice[i].size;
        if (raw != saved.rawSize || packedChecksum(sPool.slots[slot].offset, saved.packedSize) != sPackedChecksums[slot]) continue;
        poolReadSpans(sPool.slots[slot].offset, saved.packedSize, recovery.source);
        for (u32 i = 0; i < h.region_count; ++i)
            recovery.destination[i] = {reinterpret_cast<void *>(h.regions[i].addr), h.regions[i].size};
        for (u32 i = 0; i < Ghost::kSavestateSpanCount; ++i)
            recovery.destination[h.region_count + i] = ghost[i];
        for (u32 i = 0; i < PracticeSession::kSavestateSpanCount; ++i)
            recovery.destination[h.region_count + Ghost::kSavestateSpanCount + i] = practice[i];
        recovery.slot = slot;
        recovery.count = h.region_count + Ghost::kSavestateSpanCount + PracticeSession::kSavestateSpanCount;
        if (StateCodec::validateRestore(codecWorkspace(), SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,
            recovery.source, 2, recovery.destination, recovery.count, saved.rawSize, saved.adler32) != StateCodec::SUCCESS) continue;
        return true;
    }
    return false;
}
#pragma clang section text=""

const char *archiveStatusText(u32 status) {
    switch (status) {
    case SUSAMUNE_STATE_CANCELLED: return "SD transfer cancelled";
    case SUSAMUNE_STATE_FULL: return "Not enough state memory - clear a slot";
    case SUSAMUNE_STATE_BAD_FILE: return "SD state is damaged or unsupported";
    case SUSAMUNE_STATE_NOT_FOUND: return "SD state was not found";
    case SUSAMUNE_STATE_STALE: return "SD state changed - refresh the list";
    case SUSAMUNE_STATE_WRONG_CONFIG: return "SD state needs the same game setup";
    case SUSAMUNE_STATE_UNAVAILABLE: return "SD states unavailable";
    default: return "SD transfer failed - try again";
    }
}

#if ENABLE_SAVESTATE_DBG
// Kept out of the stage heap so the textbox survives stage transitions.
char sStatusBuf[12];
#endif

} // namespace


// ---------------------------------------------------------------------
// SavestateManager
// ---------------------------------------------------------------------

SavestateManager::SavestateManager() {
    mFeedback[0] = '\0';
    mFeedbackFrames = 0;
    mLoadPending = false;
    mLoadWaitFrames = 0;

#if ENABLE_SAVESTATE_DBG
    setStatus("ready");
#endif

    memset(&sPool, 0, sizeof(sPool));
    sPoolMemory = statePoolMemory();
    initStateMetadata();
    sActiveSlot = sLoadSlot = sNextGeneration = 0;
    memset(&sSelectedSD, 0, sizeof(sSelectedSD));
    memset(&sPendingSD, 0, sizeof(sPendingSD));
    sPendingSlot = sPendingGeneration = 0;
    sAwaitingLoadApproval = sBusy = false;
    sDurableSlots = 0;
    memset(sPackedChecksums, 0, sizeof(sPackedChecksums));
    sDiskActive = sExplicitTransfer = sTransferReady = false;
    sDiskRestore = sDiskLoadReady = false;
    sDiskStream = sDiskRecovered = false;
}

#if ENABLE_SAVESTATE_DBG
void SavestateManager::setStatus(const char *msg) {
    // Do NOT call J2DTextBox::setString; it reallocates on the stage heap.
    strncpy(sStatusBuf, msg, sizeof(sStatusBuf));
}
#define SET_STATUS(msg) setStatus(msg)
#else
#define SET_STATUS(msg) ((void)0)
#endif

void SavestateManager::feedback(const char *debug, const char *message) {
    SET_STATUS(debug);
    // A failed action must remain visible when routine confirmations are off.
    if (debug[0] == 'E' && debug[1] == ':' && gMenu) {
        gMenu->toast(message);
        mFeedbackFrames = 0;
        return;
    }
    if (!gSettings.getBool(SETTING_SAVESTATE_FEEDBACK)) {
        mFeedbackFrames = 0;
        return;
    }
    strncpy(mFeedback, message, sizeof(mFeedback) - 1);
    mFeedback[sizeof(mFeedback) - 1] = '\0';
    mFeedbackFrames = Menu::kToastFrames;
}

u32 SavestateManager::activeSlot() const { return sActiveSlot; }
u32 SavestateManager::saveSlot() const { return sActiveSlot; }
u32 SavestateManager::loadSlot() const { return sLoadSlot; }
bool SavestateManager::loadSourceIsSD() const { return sSelectedSD.id != 0; }
u32 SavestateManager::selectedSDId() const { return sSelectedSD.id; }
const char *SavestateManager::selectedSDName() const { return sSelectedSD.name; }

SavestateManager::SlotInfo SavestateManager::slotInfo(u32 slot) const {
    SlotInfo info = {};
    if (slot >= kSlotCount || !validStore()) return info;
    const StoredState &saved = sSlots[slot];
    info.valid = saved.header.magic == kSnapshotMagic;
    info.area = saved.header.area_id;
    info.episode = saved.header.episode_id;
    info.generation = saved.generation;
    info.packedBytes = saved.packedSize;
    return info;
}

bool SavestateManager::practiceData(u32 slot, PracticeSession::SavestateData *out) const {
    if (!out || slot >= kSlotCount || !validStore()) return false;
    const StoredState &saved = sSlots[slot];
    if (saved.header.magic != kSnapshotMagic || saved.header.version != kSnapshotVersion ||
        saved.header.game_version != SUSAMUNE_GAME_VERSION) return false;
    StateCodec::WriteSpan spans[PracticeSession::kSavestateSpanCount];
    if (!PracticeSession::savestateRestoreSpans(saved.practice, spans)) return false;
    *out = saved.practice;
    return true;
}

bool SavestateManager::selectSlot(u32 slot) {
    if (slot >= kSlotCount || sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval) return false;
    sActiveSlot = slot;
    sLoadSlot = slot;
    memset(&sSelectedSD, 0, sizeof(sSelectedSD));
    char text[48];
    snprintf(text, sizeof(text), "State %lu selected - %s", slot + 1,
             slotInfo(slot).valid ? "saved" : "empty");
    if (gMenu) gMenu->toast(text);
    return true;
}

bool SavestateManager::cycleSlot() { return selectSlot((sActiveSlot + 1) % kSlotCount); }

bool SavestateManager::selectSaveSlot(u32 slot) {
    if (slot >= kSlotCount || sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval) return false;
    sActiveSlot = slot;
    char text[48];
    snprintf(text, sizeof(text), "Save slot: %lu - %s", slot + 1, slotInfo(slot).valid ? "saved" : "empty");
    if (gMenu) gMenu->toast(text);
    return true;
}

bool SavestateManager::selectLoadSlot(u32 slot) {
    if (slot >= kSlotCount || sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval) return false;
    sLoadSlot = slot;
    memset(&sSelectedSD, 0, sizeof(sSelectedSD));
    char text[48];
    snprintf(text, sizeof(text), "Load slot: %lu - %s", slot + 1, slotInfo(slot).valid ? "saved" : "empty");
    if (gMenu) gMenu->toast(text);
    return true;
}

bool SavestateManager::cycleSaveSlot() { return selectSaveSlot((sActiveSlot + 1) % kSlotCount); }
bool SavestateManager::cycleLoadSlot() { return selectLoadSlot((sLoadSlot + 1) % kSlotCount); }

bool SavestateManager::selectSDForLoad(u32 id, u32 crc, u32 packed, const char *name) {
    if (!id || id > SUSAMUNE_STATE_MAX_ARCHIVE_ID || !crc || !packed || packed > poolCapacity() ||
        sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval || !sdAvailable()) return false;
    memset(&sSelectedSD, 0, sizeof(sSelectedSD));
    sSelectedSD.id = id;
    sSelectedSD.headerCrc = crc;
    sSelectedSD.packedSize = packed;
    if (name) strncpy(sSelectedSD.name, name, sizeof(sSelectedSD.name) - 1);
    if (gMenu) gMenu->toast("SD state selected - use your Load bind");
    return true;
}

bool SavestateManager::diskBusy() { return sDiskActive || sDiskLoadReady || StateStorage::busy(); }
bool SavestateManager::sdAvailable() const { return sMetadataReady && StateStorage::available(); }
bool SavestateManager::sdCatalogReady() const { return StateStorage::catalogReady(); }
const SusamuneStateCatalog &SavestateManager::sdCatalog() const { return StateStorage::catalog(); }
const char *SavestateManager::sdStatus() const {
    return sdAvailable() ? sDiskStatus : "Savestates need the matching Moonshine Launcher";
}

bool SavestateManager::projectCompatible(const SusamuneTasManifest &project) const {
    return project.gameId == archiveGameId() && archiveBuildCompatible(project.buildCrc) &&
        project.configId == StateStorage::configId();
}
u32 SavestateManager::slotSceneKey(u32 slot) const {
    if (!slotInfo(slot).valid) return 0;
    const StoredState &saved = sSlots[slot];
    return (static_cast<u32>(saved.header.area_id) << 24) |
        (static_cast<u32>(saved.header.episode_id) << 16) | (saved.parentEpisode & 0xFFFFu);
}
u32 SavestateManager::currentSceneKey() const { return archiveSceneKey(); }

bool SavestateManager::takeTransferResult(TransferResult &out) {
    if (!sTransferReady) return false;
    out = sTransferResult;
    sTransferReady = false;
    return true;
}

bool SavestateManager::saveToSD(const char *name) {
    return beginSDExport(sActiveSlot, slotInfo(sActiveSlot).generation, name, nullptr);
}

bool SavestateManager::exportSlotExplicit(u32 slot, u32 expectedGeneration,
                                          const SusamuneTasRequest *project) {
    if (!project || !expectedGeneration || sTransferReady) return false;
    return beginSDExport(slot, expectedGeneration, nullptr, project);
}

#pragma clang section text=".foxtrot.text"
bool SavestateManager::beginSDExport(u32 slot, u32 expectedGeneration, const char *name,
                                     const SusamuneTasRequest *project) {
    if (slot >= kSlotCount || sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval || !validStore()) return false;
    if (sSlots[slot].generation != expectedGeneration) return false;
    if (!admitArchiveStage()) return false;
    const StoredState &saved = sSlots[slot];
    if (!StateArchiveProfile::valid(saved.archiveProfile)) {
        sDiskStatus = "Save a supported stage state first";
        if (gMenu) gMenu->toast(sDiskStatus);
        return false;
    }
    SusamuneStateArchiveHeader h = {};
    h.metadataSize = sizeof(saved);
    h.packedSize = saved.packedSize;
    h.rawSize = saved.rawSize;
    h.gameId = archiveGameId();
    h.buildCrc = archiveBuildId();
    h.snapshotVersion = kSnapshotVersion;
    h.sceneKey = (static_cast<u32>(saved.header.area_id) << 24) |
        (static_cast<u32>(saved.header.episode_id) << 16) | (saved.parentEpisode & 0xFFFFu);
    if (name) strncpy(h.name, name, sizeof(h.name) - 1);
    else snprintf(h.name, sizeof(h.name), "State %lu - area %u episode %u", slot + 1,
                  saved.header.area_id, saved.header.episode_id);
    // Export a compatible header without changing the retained state or its file.
    sCandidate = saved;
    if (!StateArchiveProfile::reidentify(sCandidate.archiveProfile, archiveBuildId())) return false;
    sCandidate.metadataTag = metadataTag(sCandidate);
    if (!StateStorage::startExport(h, &sCandidate, sPool.slots[slot].offset, project)) return false;
    sDiskSlot = slot;
    sDiskGeneration = expectedGeneration;
    sExplicitTransfer = project != nullptr;
    sDiskStarted = OSGetTime();
    sDiskActive = true;
    sDiskStatus = "Saving state to SD...";
    return true;
}
#pragma clang section text=""

bool SavestateManager::loadFromSD(u32 id, u32 crc, u32 packed) {
    return beginSDLoad(id, crc, packed, false);
}

bool SavestateManager::importSlotExplicit(u32 slot, u32 expectedGeneration, u32 id,
                                          u32 crc, u32 packed, const SusamuneTasRequest *project,
                                          const SusamuneTasManifest *manifest) {
    if (!project || !manifest || !SusamuneTasManifestValid(manifest) ||
        !projectCompatible(*manifest) || project->role >= SUSAMUNE_TAS_COMPONENTS ||
        manifest->projectId != project->projectId || manifest->generation != project->projectGeneration ||
        manifest->checksum != project->expectedProjectCrc || sTransferReady || slot >= kSlotCount ||
        slotInfo(slot).generation != expectedGeneration) return false;
    const SusamuneTasComponent &component = manifest->components[project->role];
    if (component.componentId != id || project->componentId != id ||
        component.headerCrc != crc || component.packedBytes != packed ||
        !beginSDLoad(id, crc, packed, false, slot, project)) return false;
    sProjectStartKey[0] = manifest->startKey[0];
    sProjectStartKey[1] = manifest->startKey[1];
    sProjectFrames = component.frames;
    sProjectRole = project->role;
    sProjectScene = component.sceneKey;
    return true;
}

bool SavestateManager::beginSDLoad(u32 id, u32 crc, u32 packed, bool restore,
                                  u32 slot, const SusamuneTasRequest *project) {
    if (slot == kSlotCount) slot = sActiveSlot;
    if (slot >= kSlotCount) return false;
    if (sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval || !validStore()) return false;
    if (!admitArchiveStage()) return false;
    if (!restore && !StateSlotPoolCanCommit(&sPool, poolCapacity(), slot, packed, SUSAMUNE_STATE_STAGING_SIZE)) {
        sDiskStatus = "Not enough state memory - clear a slot";
        if (gMenu) gMenu->toast(sDiskStatus);
        return false;
    }
    const bool stream = restore && packed > SUSAMUNE_STATE_STAGING_SIZE + poolCapacity() - sPool.used;
    if (stream ? !StateStorage::startWindow(id, crc, packed, 0, SUSAMUNE_STATE_STAGING_SIZE, project) :
        !StateStorage::startImport(id, crc, packed, sPool.used, project)) return false;
    sDiskSlot = slot;
    sExplicitTransfer = project != nullptr;
    sDiskGeneration = sSlots[sDiskSlot].generation;
    sDiskPoolUsed = sPool.used;
    sDiskScene = archiveSceneKey();
    sDiskStarted = OSGetTime();
    sDiskActive = true;
    sDiskRestore = restore;
    sDiskStream = stream;
    sDiskRecovered = false;
    sStreamId = id;
    sDiskStatus = "Reading state from SD...";
    return true;
}

bool SavestateManager::renameSD(u32 id, u32 crc, const char *name) {
    if (sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval || !admitArchiveStage()) return false;
    if (!StateStorage::rename(id, crc, name)) return false;
    sDiskStarted = OSGetTime();
    sDiskActive = true;
    sDiskStatus = "Renaming SD state...";
    return true;
}

bool SavestateManager::deleteSD(u32 id, u32 crc) {
    if (sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval || !admitArchiveStage()) return false;
    if (!StateStorage::remove(id, crc)) return false;
    sDiskStarted = OSGetTime();
    sDiskActive = true;
    sDiskStatus = "Deleting SD state...";
    return true;
}

bool SavestateManager::refreshSD(u32 afterId) {
    if (sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval) return false;
    if (!admitArchiveStage()) return false;
    if (!StateStorage::refresh(afterId)) return false;
    sDiskStarted = OSGetTime();
    sDiskActive = true;
    sDiskStatus = "Reading SD states...";
    return true;
}

bool SavestateManager::cancelSD() {
    if (!StateStorage::cancel()) return false;
    sDiskStatus = "Cancelling SD transfer...";
    return true;
}

void SavestateManager::updateDisk() {
    StateStorage::update();
    if (!sDiskActive) { StateStorage::discardCancelledResult(); return; }
    StateStorage::Result result;
    if (!StateStorage::takeResult(result)) return;
    const bool wasActive = sDiskActive;
    bool imported = false;
    if (result.status == SUSAMUNE_STATE_OK && (result.command == SUSAMUNE_STATE_CMD_IMPORT ||
            result.command == SUSAMUNE_STATE_CMD_READ_WINDOW)) {
        GXDrawDone();
        const bool ints = OSDisableInterrupts();
        const bool dma = muteAudioDma();
        bool valid = wasActive && validStore() && sDiskSlot < kSlotCount &&
            sPool.used == sDiskPoolUsed && sSlots[sDiskSlot].generation == sDiskGeneration &&
            archiveSceneKey() == sDiskScene && result.header.metadataSize == sizeof(sCandidate) && result.metadata;
        if (valid) {
            memcpy(&sCandidate, result.metadata, sizeof(sCandidate));
            valid = sExplicitTransfer && !sDiskRestore ? archiveProjectCandidateMatches(result.header) :
                archiveCandidateMatches(result.header);
        }
        if (valid && sExplicitTransfer)
            valid = PracticeSession::projectSavestateMatches(sCandidate.practice,
                sProjectStartKey, sProjectRole, sProjectFrames);
        if (valid && !sDiskRestore) {
            const u32 first = sCandidate.packedSize < SUSAMUNE_STATE_STAGING_SIZE ?
                sCandidate.packedSize : SUSAMUNE_STATE_STAGING_SIZE;
            StateCodec::ReadSpan spans[3];
            spans[0] = {reinterpret_cast<const void *>(kStagingBase), first};
            poolReadSpans(sDiskPoolUsed, sCandidate.packedSize - first, spans + 1);
            valid = StateCodec::validate(codecWorkspace(), SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,
                spans, 3, sCandidate.rawSize, sCandidate.adler32) == StateCodec::SUCCESS;
        }
        if (valid && sDiskRestore) {
            if (sDiskStream) {
                sStreamHeader = result.header;
                sStreamOffset = 0;
                sStreamSize = result.window.size;
                sStreamSeen = 1;
                sStreamChecksums[0] = SusamuneStateCrc(reinterpret_cast<const void *>(kStagingBase), sStreamSize);
                valid = sStreamChecksums[0] == result.window.checksum;
                sStreamCrc = ~sStreamChecksums[0];
                sStreamCommit = false;
            }
            // Keep temporary compressed bytes owned until the post-draw restore.
            if (valid) {
                sDiskLoadReady = true;
                mLoadPending = true;
                mLoadWaitFrames = 0;
                sPendingSlot = kSlotCount;
                sPendingGeneration = sCandidate.generation;
            }
        } else if (valid) {
            valid = StateSlotPoolCommitBanked(&sPool, &sPoolMemory, sDiskSlot,
                sCandidate.packedSize, reinterpret_cast<const u8 *>(kStagingBase), SUSAMUNE_STATE_STAGING_SIZE);
            if (valid) {
                sCandidate.generation = nextGeneration();
                sCandidate.metadataTag = metadataTag(sCandidate);
                sSlots[sDiskSlot] = sCandidate;
                sDurableSlots |= 1u << sDiskSlot;
                sPackedChecksums[sDiskSlot] = result.header.payloadCrc;
                imported = true;
            }
        }
        if (wasActive && !sDiskLoadReady) rebaseMissionStopwatch(sDiskStarted);
        unmuteAudioDma(dma);
        OSRestoreInterrupts(ints);
        if (!valid) result.status = SUSAMUNE_STATE_BAD_FILE;
    } else if (wasActive) rebaseMissionStopwatch(sDiskStarted);
    if (wasActive && sExplicitTransfer) {
        sTransferResult = {result.command, result.status, result.id, sDiskSlot,
            imported ? sSlots[sDiskSlot].generation : sDiskGeneration, result.header};
        sTransferReady = true;
    }
    const bool projectTransfer = sExplicitTransfer;
    sExplicitTransfer = false;
    sDiskActive = false;
    sDiskRestore = false;
    if (result.status != SUSAMUNE_STATE_OK) {
        sDiskStatus = archiveStatusText(result.status);
        PracticeSession::cancelLoadHold();
    }
    else if (result.command == SUSAMUNE_STATE_CMD_EXPORT) sDiskStatus = projectTransfer ?
        "TAS checkpoint saved" : "State saved in Moonshine data/states";
    else if (result.command == SUSAMUNE_STATE_CMD_CATALOG) sDiskStatus = "SD states ready";
    else if (result.command == SUSAMUNE_STATE_CMD_RENAME) {
        sDiskStatus = "SD state renamed";
        if (sSelectedSD.id == result.id)
            memcpy(sSelectedSD.name, result.name, sizeof(sSelectedSD.name));
    } else if (result.command == SUSAMUNE_STATE_CMD_DELETE) {
        sDiskStatus = "SD state deleted";
        if (sSelectedSD.id == result.id) memset(&sSelectedSD, 0, sizeof(sSelectedSD));
    }
    if (sDiskLoadReady) sDiskStatus = "Restoring SD state...";
    if (imported) {
        PracticeSession::onSavestateCleared(sDiskSlot, sDiskGeneration);
        sDiskStatus = "SD state imported into memory";
        char message[48];
        snprintf(message, sizeof(message), "Imported into state %lu", sDiskSlot + 1);
        if (gMenu && !projectTransfer) gMenu->toast(message);
    } else if (wasActive && gMenu && !projectTransfer) gMenu->toast(sDiskStatus);
}

bool SavestateManager::clearSlot(u32 slot, u32 expectedGeneration) {
    if (sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval || !validStore() ||
        slot >= kSlotCount || !sSlots[slot].header.magic ||
        sSlots[slot].generation != expectedGeneration) return false;
    sBusy = true;
    if (!StateSlotPoolClearBanked(&sPool, &sPoolMemory, slot)) {
        sBusy = false;
        return false;
    }
    memset(&sSlots[slot], 0, sizeof(sSlots[slot]));
    sDurableSlots &= ~(1u << slot);
    sPackedChecksums[slot] = 0;
    sSlots[slot].generation = nextGeneration();
    sBusy = false;
    PracticeSession::onSavestateCleared(slot, expectedGeneration);
    return true;
}

bool SavestateManager::saveState() {
    return saveSlotExplicit(sActiveSlot);
}

bool SavestateManager::saveSlotExplicit(u32 slot, bool forceRng, bool omitPracticeTake) {
    if (sBusy || diskBusy() || mLoadPending || sAwaitingLoadApproval) {
        feedback("E:busy", "Wait for the pending state load");
        return false;
    }
    if (slot >= kSlotCount || !validStore()) {
        feedback("E:store", sMetadataReady ? "State memory damaged - restart game" :
            "Savestates need matching Moonshine Launcher");
        return false;
    }
    // Refuse while a stage load is in flight or the intro sequence is playing;
    // the heap is not yet stable there. See inLoadTransition().
    if (inLoadTransition()) {
        feedback("E:loading", "Can't save during stage loading");
        return false;
    }

    JKRHeap *heap = gpApplication.mCurrentHeap;
    if (!heap) {
        feedback("E:noheap", "Savestate unavailable");
        return false;
    }

    const u32 heapStart = reinterpret_cast<u32>(heap);
    if (heapStart < 0x80000000u ||
        heapStart > 0x81800000u - sizeof(JKRHeap)) {
        feedback("E:badheap", "Savestate unavailable");
        return false;
    }
    const u32 heapEnd = reinterpret_cast<u32>(heap->mEnd);
    if (heapEnd > 0x81800000u ||
        heapEnd <= heapStart) {
        feedback("E:badheap", "Savestate unavailable");
        return false;
    }
    const u32 heapSize  = heapEnd - heapStart;

    // Bounds check before we write a single byte.
    u32 total = 0;
    for (int i = 0; i < kNumStaticRanges; i++) {
        total += kStaticRanges[i].end - kStaticRanges[i].start;
    }
    for (int i = 0; i < kNumPointedAllocs; i++) {
        total += kPointedAllocs[i].size;
    }
    total += heapSize;
    if (total + kHeaderSize > kSnapshotReservedSize) {
        feedback("E:size", "Savestate is too large");
        return false;
    }

    // These globals are trusted in a healthy stage, but validate every target
    // before the first copy so a damaged live pointer cannot turn Save into an
    // arbitrary MEM1 read or wrap the in-heap containment check.
    for (int i = 0; i < kNumPointedAllocs; i++) {
        const PointedAlloc &alloc = kPointedAllocs[i];
        const u32 target = *reinterpret_cast<const u32 *>(alloc.ptr_addr);
        if (target == 0) continue;
        const bool inHeap = target >= heapStart && target < heapEnd &&
                            alloc.size <= heapEnd - target;
        if (!inHeap &&
            (target < 0x80000000u || target >= 0x81800000u ||
             alloc.size > 0x81800000u - target)) {
            feedback("E:rootptr", "Stage layout changed - save again");
            return false;
        }
    }

    // Audio engine has DSP-side state we can't snapshot; quiet it before
    // we touch anything so the current-sounds list won't reference freed
    // tracks after a future restore.
    if (gpMSound) {
        gpMSound->stopAllSound();
    }

    // Compression reads live regions for longer than the old raw copy.
    GXDrawDone();

    // We are called from inside onUpdate, which runs on the main thread
    // between director->direct() and rendering. That is already the most
    // quiescent point in the frame, but disable interrupts anyway so we
    // don't race a VI retrace callback that touches heap objects.
    sBusy = true;
    bool ints = OSDisableInterrupts();
    // Silence the DAC for the whole interrupts-off window so the frozen audio
    // DMA doesn't buzz; restored just before interrupts come back.
    bool dma = muteAudioDma();

    if (!captureLiveVideo()) {
        unmuteAudioDma(dma);
        OSRestoreInterrupts(ints);
        sBusy = false;
        feedback("E:video", "Video player busy - try again");
        return false;
    }
    memset(&sCandidate, 0, sizeof(sCandidate));
    SavestateHeader *h = &sCandidate.header;
    h->magic        = 0; // committed at end as a torn-write guard
    h->version      = kSnapshotVersion;
    h->game_version = SUSAMUNE_GAME_VERSION;
    h->heap_addr    = heapStart;
    h->heap_size    = heapSize;
    h->area_id      = gpApplication.mCurrentScene.mAreaID;
    h->episode_id   = gpApplication.mCurrentScene.mEpisodeID;
    h->feature_state = featuresSavestateState();
    h->_pad0        = 0;
    h->region_count = 0;
    h->save_time    = OSGetTime();

    u32 offset = 0;
    for (int i = 0; i < kNumStaticRanges; i++) {
        // Skip a setting-gated range when its setting is disabled (e.g. the RNG
        // seed when "Save RNG state" is Off). It simply won't be in the region
        // list, so a later load leaves that memory untouched.
        if (kStaticRanges[i].gate != kNoGate &&
            !gSettings.getBool((SettingId)kStaticRanges[i].gate) &&
            !(forceRng && kStaticRanges[i].gate == SETTING_SAVE_RNG_STATE)) {
            continue;
        }
        u32 sz = kStaticRanges[i].end - kStaticRanges[i].start;
        offset = captureRegion(h, offset, kStaticRanges[i].start, sz);
    }

    // Follow each tracked pointer and capture its target. These objects
    // live on the root heap, which the JKRSolidHeap snapshot does not
    // cover -- without this, e.g. coin counts (TFlagManager) and shine
    // flags survive *only* because their pointer in BSS is restored, but
    // the bytes it points at are whatever is live at load time.
    for (int i = 0; i < kNumPointedAllocs; i++) {
        const PointedAlloc &pa = kPointedAllocs[i];
        u32 target = *reinterpret_cast<u32 *>(pa.ptr_addr);
        if (target == 0) {
            continue; // not yet initialised
        }
        // If the target happens to live inside the JKRSolidHeap range,
        // skip it -- it'll already be covered by the heap snapshot.
        if (target >= heapStart && target < heapEnd &&
            pa.size <= heapEnd - target) {
            continue;
        }
        offset = captureRegion(h, offset, target, pa.size);
    }

    // Heap last (largest payload).
    offset = captureRegion(h, offset, heapStart, heapSize);

    sCandidate.parentEpisode = parentEpisode();
    gQFTTimer.captureSavestate(sCandidate.timer);
    ILing::captureSavestate(sCandidate.attempt);
    captureArchiveProfile(sCandidate.archiveProfile);
    StateCodec::ReadSpan ghostSource[Ghost::kSavestateSpanCount];
    if (!Ghost::captureSavestate(sCandidate.ghost, ghostSource)) {
        rebaseMissionStopwatch(h->save_time);
        invalidateVideoReadBuffer();
        unmuteAudioDma(dma);
        OSRestoreInterrupts(ints);
        sBusy = false;
        feedback("E:ghost", "Ghost recording unavailable - try again");
        return false;
    }
    StateCodec::ReadSpan practiceSource[PracticeSession::kSavestateSpanCount];
    if (!PracticeSession::captureSavestate(sCandidate.practice, practiceSource, forceRng, omitPracticeTake)) {
        rebaseMissionStopwatch(h->save_time);
        invalidateVideoReadBuffer();
        unmuteAudioDma(dma);
        OSRestoreInterrupts(ints);
        sBusy = false;
        feedback("E:practice", "Input recording unavailable - try again");
        return false;
    }
    StateCodec::ReadSpan source[kMaxRegions + Ghost::kSavestateSpanCount + PracticeSession::kSavestateSpanCount];
    for (u32 i = 0; i < h->region_count; ++i)
        source[i] = {reinterpret_cast<const void *>(h->regions[i].addr), h->regions[i].size};
    u32 rawSize = offset;
    for (u32 i = 0; i < Ghost::kSavestateSpanCount; ++i) {
        source[h->region_count + i] = ghostSource[i];
        rawSize += ghostSource[i].size;
    }
    for (u32 i = 0; i < PracticeSession::kSavestateSpanCount; ++i) {
        source[h->region_count + Ghost::kSavestateSpanCount + i] = practiceSource[i];
        rawSize += practiceSource[i].size;
    }
    StateCodec::Result result;
    bool fits = compressCandidate(source, h->region_count + Ghost::kSavestateSpanCount +
        PracticeSession::kSavestateSpanCount, rawSize, slot, result);
    if (!fits) fits = repackForCandidate(source, h->region_count + Ghost::kSavestateSpanCount +
        PracticeSession::kSavestateSpanCount, rawSize, slot, result);
    if (!fits) {
        rebaseMissionStopwatch(h->save_time);
        invalidateVideoReadBuffer();
        unmuteAudioDma(dma);
        OSRestoreInterrupts(ints);
        sBusy = false;
        char text[48];
        snprintf(text, sizeof(text), result.status == StateCodec::SUCCESS ||
            result.status == StateCodec::OUTPUT_FULL ?
            "State %lu won't fit - clear another slot" : "State %lu could not be compressed", slot + 1);
        feedback("E:space", text);
        return false;
    }
    sCandidate.rawSize = rawSize;
    sCandidate.packedSize = result.compressedBytes;
    sCandidate.adler32 = result.adler32;
    sCandidate.generation = nextGeneration();
    h->magic = kSnapshotMagic;
    sCandidate.metadataTag = metadataTag(sCandidate);
    sSlots[slot] = sCandidate;
    sDurableSlots &= ~(1u << slot);
    sPackedChecksums[slot] = packedChecksum(sPool.slots[slot].offset, sCandidate.packedSize);
    // The pool stays PPC-owned; SD export flushes its exact spans before handoff.

    // The mission countdown must not charge time spent compressing a state.
    rebaseMissionStopwatch(h->save_time);
    invalidateVideoReadBuffer();
    unmuteAudioDma(dma);
    OSRestoreInterrupts(ints);
    sBusy = false;

    PracticeSession::onSavestateSaved(slot, sCandidate.generation);
    CrashReport::note(SUSAMUNE_CRASH_EVENT_SAVESTATE, 1, slot + 1);
    char text[48];
    snprintf(text, sizeof(text), "State %lu saved", slot + 1);
    if (!forceRng) feedback("saved", text);
    return true;
}

bool SavestateManager::loadState() {
    if (sSelectedSD.id)
        return beginSDLoad(sSelectedSD.id, sSelectedSD.headerCrc, sSelectedSD.packedSize, true);
    return loadSlot(sLoadSlot, slotInfo(sLoadSlot).generation);
}

bool SavestateManager::loadSlot(u32 slot, u32 expectedGeneration) {
    const bool fromSD = slot == kSlotCount && sDiskLoadReady;
    if (sBusy || (diskBusy() && !fromSD) || mLoadPending || sAwaitingLoadApproval) {
        feedback("E:busy", "Wait for the pending state load");
        return false;
    }
    // Refuse while a stage load is in flight or the intro sequence is playing;
    // overwriting a heap the setup thread is still filling crashes. See
    // inLoadTransition().
    if (inLoadTransition()) {
        feedback("E:loading", "Can't load during stage loading");
        return false;
    }

    if (!validStore() || (slot >= kSlotCount && !fromSD)) {
        feedback("E:store", sMetadataReady ? "State memory damaged - restart game" :
            "Savestates need matching Moonshine Launcher");
        return false;
    }
    StoredState &saved = fromSD ? sCandidate : sSlots[slot];
    SavestateHeader *h = &saved.header;
    if (h->magic != kSnapshotMagic) {
        char text[48];
        snprintf(text, sizeof(text), "State %lu is empty - choose a saved state", slot + 1);
        feedback("E:nosnap", text);
        return false;
    }
    if (!expectedGeneration || saved.generation != expectedGeneration) {
        feedback("E:changed", "That state changed - choose it again");
        return false;
    }
    if (h->version != kSnapshotVersion) {
        feedback("E:version", "Savestate is from another build");
        return false;
    }
    if (h->game_version != SUSAMUNE_GAME_VERSION) {
        feedback("E:region", "Savestate is from another region");
        return false;
    }

    JKRHeap *heap = gpApplication.mCurrentHeap;
    if (!heap) {
        feedback("E:noheap", "Savestate unavailable");
        return false;
    }

    const u32 heapStart = reinterpret_cast<u32>(heap);
    if (heapStart < 0x80000000u ||
        heapStart > 0x81800000u - sizeof(JKRHeap)) {
        feedback("E:badheap", "Savestate unavailable");
        return false;
    }

    // Pointers in the snapshotted heap are absolute. If the heap moved
    // (different scenario, different boot path), restoring would scribble
    // stale pointers all over the place. Refuse the load.
    if (heapStart != h->heap_addr) {
        feedback("E:hpaddr", "Stage layout changed - save again");
        return false;
    }
    const u32 heapEnd = reinterpret_cast<u32>(heap->mEnd);
    if (heapEnd > 0x81800000u || heapEnd <= heapStart) {
        feedback("E:badheap", "Savestate unavailable");
        return false;
    }
    const u32 heapSize = heapEnd - heapStart;
    if (heapSize != h->heap_size) {
        feedback("E:hpsize", "Stage layout changed - save again");
        return false;
    }

    // Same-scenario only. Restoring across a moveStage() is more
    // complicated -- the heap freeAll()s and gets re-populated by the
    // new director's setup -- and not the use case we're after.
    if (h->area_id    != gpApplication.mCurrentScene.mAreaID
     || h->episode_id != gpApplication.mCurrentScene.mEpisodeID
     || saved.parentEpisode != parentEpisode()) {
        feedback("E:scene", "Savestate belongs to another area");
        return false;
    }

    if (!validSnapshotRegions(h, heapStart, heapEnd)) {
        feedback("E:badsnap", "Savestate is damaged - save again");
        return false;
    }
    StateCodec::WriteSpan ghostDestinations[Ghost::kSavestateSpanCount];
    if (!Ghost::savestateRestoreSpans(saved.ghost, ghostDestinations)) {
        feedback("E:ghost", "Ghost recording unavailable - try again");
        return false;
    }
    StateCodec::WriteSpan practiceDestinations[PracticeSession::kSavestateSpanCount];
    if (!PracticeSession::savestateRestoreSpans(saved.practice, practiceDestinations)) {
        feedback("E:practice", "Input recording unavailable - try again");
        return false;
    }
    u32 rawSize = h->regions[h->region_count - 1].buf_offset + heapSize;
    for (u32 i = 0; i < Ghost::kSavestateSpanCount; ++i)
        rawSize += ghostDestinations[i].size;
    for (u32 i = 0; i < PracticeSession::kSavestateSpanCount; ++i)
        rawSize += practiceDestinations[i].size;
    if (rawSize != saved.rawSize) {
        feedback("E:badsnap", "Savestate is damaged - save again");
        return false;
    }

    // gpCardManager has its own worker thread, mutex, and cond var on the
    // root heap. Snapshotting/restoring it would trash kernel-side thread
    // bookkeeping, so we don't -- but we also can't safely tear down the
    // rest of the world while the card thread is mid-transaction. Queued
    // loads wait frame-by-frame in processPendingLoad(); keep this guard for
    // direct callers and the tiny race between that check and this one.
    if (gpCardManager && gpCardManager->getLastStatus() == CARD_ERROR_BUSY) {
        feedback("E:cardbsy", "Memory card busy - try again");
        return false;
    }

    const u8 previousDirectorState = gpMarDirector ? gpMarDirector->mCurState : 0;
    // Same reasoning as save().
    if (gpMSound) {
        gpMSound->stopAllSound();
    }

    // Never overwrite heap-resident textures or display-list backing storage
    // while the graphics processor can still be reading the current frame.
    // D-pad loads normally arrive from processPendingLoad(), immediately after
    // THPPlayerDrawDone() has already issued this barrier. Keep it here too so
    // direct callers of loadState() receive the same safety guarantee.
    GXDrawDone();

    sBusy = true;
    bool ints = OSDisableInterrupts();
    // Silence the DAC across the interrupts-off restore so the frozen audio
    // DMA doesn't buzz; restored just before interrupts come back.
    bool dma = muteAudioDma();

    const OSTime restoreStarted = OSGetTime();
    if (!captureLiveVideo()) {
        unmuteAudioDma(dma);
        OSRestoreInterrupts(ints);
        sBusy = false;
        feedback("E:video", "Video player busy - try again");
        return false;
    }
    const bool durable = fromSD || (sDurableSlots & (1u << slot)) != 0;
    if (durable) {
        captureArchiveProfile(sLiveArchiveProfile);
        if (!StateArchiveProfile::matches(saved.archiveProfile, sLiveArchiveProfile)) {
            if (!fromSD) rebaseMissionStopwatch(restoreStarted);
            unmuteAudioDma(dma);
            OSRestoreInterrupts(ints);
            sBusy = false;
            feedback("E:owners", "SD state needs the same stage setup");
            return false;
        }
    }
    if (!captureRestoreBindings(heapStart, heapEnd, durable)) {
        unmuteAudioDma(dma);
        OSRestoreInterrupts(ints);
        sBusy = false;
        feedback("E:models", "Wait for models to finish loading");
        return false;
    }
    StateCodec::ReadSpan compressed[3] = {};
    if (fromSD && !sDiskStream) {
        const u32 first = saved.packedSize < SUSAMUNE_STATE_STAGING_SIZE ?
            saved.packedSize : SUSAMUNE_STATE_STAGING_SIZE;
        compressed[0] = {reinterpret_cast<const void *>(kStagingBase), first};
        poolReadSpans(sDiskPoolUsed, saved.packedSize - first, compressed + 1);
    } else if (!fromSD) poolReadSpans(sPool.slots[slot].offset, saved.packedSize, compressed);
    StateCodec::WriteSpan destinations[kMaxRegions + Ghost::kSavestateSpanCount + PracticeSession::kSavestateSpanCount];
    for (u32 i = 0; i < h->region_count; ++i)
        destinations[i] = {reinterpret_cast<void *>(h->regions[i].addr), h->regions[i].size};
    for (u32 i = 0; i < Ghost::kSavestateSpanCount; ++i)
        destinations[h->region_count + i] = ghostDestinations[i];
    for (u32 i = 0; i < PracticeSession::kSavestateSpanCount; ++i)
        destinations[h->region_count + Ghost::kSavestateSpanCount + i] = practiceDestinations[i];
    StateCodec::Status restored;
    if (fromSD && sDiskStream) {
        SDRecovery recovery;
        if (!prepareSDRecovery(recovery, heapStart, heapEnd)) {
            unmuteAudioDma(dma);
            OSRestoreInterrupts(ints);
            sBusy = false;
            feedback("E:recover", "Save a RAM state here before streaming SD");
            return false;
        }
        StateCodec::StreamSource source = {
            {reinterpret_cast<const void *>(kStagingBase), SUSAMUNE_STATE_STAGING_SIZE},
            saved.packedSize, readStateWindow, nullptr};
        restored = StateCodec::validateStream(codecWorkspace(), SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,
            source, saved.rawSize, saved.adler32);
        if (restored == StateCodec::SUCCESS && ~sStreamCrc != sStreamHeader.payloadCrc)
            restored = StateCodec::CORRUPT_STREAM;
        if (restored == StateCodec::SUCCESS) {
            captureArchiveProfile(sLiveArchiveProfile);
            if (!StateArchiveProfile::matches(saved.archiveProfile, sLiveArchiveProfile))
                restored = StateCodec::CORRUPT_STREAM;
        }
        if (restored == StateCodec::SUCCESS) {
            sStreamCommit = true;
            sStreamSize = 0;
            restored = StateCodec::decompressStreamVerified(codecWorkspace(),
                SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE, source, destinations,
                h->region_count + Ghost::kSavestateSpanCount + PracticeSession::kSavestateSpanCount, saved.rawSize, saved.adler32,
                copyStateBytes, &sLiveArchiveProfile);
            if (restored == StateCodec::COMMIT_FAILED) {
                const StoredState &fallback = sSlots[recovery.slot];
                if (StateCodec::decompressVerified(codecWorkspace(), SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE,
                    recovery.source, 2, recovery.destination, recovery.count, fallback.rawSize,
                    fallback.adler32, copyStateBytes, &sLiveArchiveProfile) != StateCodec::SUCCESS)
                    __builtin_trap();
                sCandidate = fallback;
                slot = recovery.slot;
                sDiskRecovered = true;
                restored = StateCodec::SUCCESS;
            }
        }
    } else if (fromSD) {
        restored = StateCodec::decompress(codecWorkspace(),
            SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE, compressed, 3, destinations,
            h->region_count + Ghost::kSavestateSpanCount + PracticeSession::kSavestateSpanCount, saved.rawSize, saved.adler32,
            copyStateBytes, &sLiveArchiveProfile);
    } else if (packedChecksum(sPool.slots[slot].offset, saved.packedSize) != sPackedChecksums[slot]) {
        restored = StateCodec::CORRUPT_STREAM;
    } else {
        restored = StateCodec::decompressVerified(codecWorkspace(),
            SUSAMUNE_STATE_CODEC_WORKSPACE_SIZE, compressed, 3, destinations,
            h->region_count + Ghost::kSavestateSpanCount + PracticeSession::kSavestateSpanCount, saved.rawSize, saved.adler32,
            copyStateBytes,
            durable ? &sLiveArchiveProfile : nullptr);
    }
    if (restored == StateCodec::COMMIT_FAILED) __builtin_trap();
    if (restored != StateCodec::SUCCESS) {
        if (!fromSD) rebaseMissionStopwatch(restoreStarted);
        unmuteAudioDma(dma);
        OSRestoreInterrupts(ints);
        sBusy = false;
        feedback("E:badsnap", "State is damaged - save again");
        return false;
    }
    GhostModel::onSavestateLoaded();
    for (u32 i = 0; i < h->region_count; i++) {
        const RegionEntry &r = h->regions[i];
        // Decompression has placed the restored bytes in D-cache, so the
        // CPU can use them immediately. Store them for GX/DMA visibility, but
        // do not flush-and-invalidate the whole stage heap: doing so makes the
        // next frame fault every restored line back in from RAM.
        DCStoreRange(reinterpret_cast<void *>(r.addr), r.size);
        // No instruction-cache invalidation: we never restore .text.
    }

    // The restored heap contains texture/image bytes from the saved frame.
    // Invalidate the GP texture cache before any subsequent draw so it cannot
    // keep sampling lines cached from the pre-load state.
    GXInvalidateTexAll();

    // TMarDirector::mStopwatch (the Piantissimo-chase / blooper-race mission
    // countdown, restored above as part of the heap region) stores an
    // absolute OSGetTime() timestamp in mLast rather than an elapsed
    // duration -- OSCheckStopwatch() computes `total + (now - mLast)`. A
    // byte-for-byte restore puts back the OLD mLast, so on the very next
    // check the timer would read as if it had kept running in real time
    // across the save/load gap instead of rewinding. Shift mLast forward by
    // exactly that real-time gap so OSCheckStopwatch() reproduces the same
    // value it had at save time.
    rebaseMissionStopwatch(h->save_time);

    unmuteAudioDma(dma);
    OSRestoreInterrupts(ints);
    sBusy = false;

    reconcilePauseAudio(previousDirectorState);
    featuresOnSavestateLoaded(h->feature_state);
    gQFTTimer.restoreSavestate(saved.timer);
    SplitEvents::onSavestateLoaded();
    SplitStats::onSavestateLoaded();
    Ghost::restoreSavestate(saved.ghost);
    GhostStorage::onSavestateLoaded();
    rngControlOnSavestateLoaded();
    MovementDisplay::onSavestateLoaded();
    MovementTimingDisplay::onSavestateLoaded();
    gCreationExtras.onSavestateLoaded();
    // An armed warp lives in mod BSS, outside the restored game snapshot.
    // Cancel it before ILing adopts the save-time attempt state.
    LevelWarp::cancelPending();
    ILing::restoreSavestate(saved.attempt);
    Records::onSavestateLoaded();
    if (fromSD && sDiskRecovered) PracticeSession::cancelLoadHold();
    PracticeSession::onSavestateLoaded();
    const bool practiceRestored = PracticeSession::restoreSavestate(saved.practice, slot, saved.generation);
    CrashReport::note(SUSAMUNE_CRASH_EVENT_SAVESTATE, 2, slot + 1);
    char text[48];
    if (sDiskRecovered && fromSD) snprintf(text, sizeof(text), "SD read failed; restored state %lu", slot + 1);
    else if (fromSD) strcpy(text, "SD state loaded");
    else snprintf(text, sizeof(text), "State %lu loaded", slot + 1);
    if (practiceRestored) feedback(sDiskRecovered && fromSD ? "E:recovered" : "loaded", text);
    else if (sDiskRecovered && fromSD) {
        snprintf(text, sizeof(text), "SD read failed; state %lu restored, TAS stopped", slot + 1);
        feedback("E:recovered", text);
    }
    return true;
}

void SavestateManager::updateHook() {
    if (diskBusy()) return;
    // The original optional in-stage counter uses bare D-pad Left/Right, the
    // same defaults as full savestates. When that option is explicitly on and
    // the live binds actually collide, the counter owns those two presses;
    // rebinding either action removes the suppression automatically.
    const bool counterControls =
        gSettings.getBool(SETTING_ATTEMPT_COUNTER) &&
        gSettings.getBool(SETTING_ATTEMPT_IN_STAGE_CONTROLS);
    const bool counterOwnsSave =
        counterControls && gBinds.get(BIND_ATTEMPT_SHOW) != 0 &&
        gBinds.get(BIND_ATTEMPT_SHOW) == gBinds.get(BIND_SAVESTATE_SAVE);
    const bool counterOwnsLoad =
        counterControls && gBinds.get(BIND_ATTEMPT_ADD) != 0 &&
        gBinds.get(BIND_ATTEMPT_ADD) == gBinds.get(BIND_SAVESTATE_LOAD);

    const bool approvedLoad = WarpWheel::takeSavestateLoadApproval();
    if (sAwaitingLoadApproval && !approvedLoad && !WarpWheel::promptPending()) {
        sAwaitingLoadApproval = false;
        PracticeSession::cancelLoadHold();
    }
    if (mLoadPending) return;
    if (approvedLoad && sAwaitingLoadApproval) {
        sAwaitingLoadApproval = false;
        mLoadPending = true;
        mLoadWaitFrames = 0;
        SET_STATUS("loading");
    } else if (sAwaitingLoadApproval) {
        return;
    } else if (!counterOwnsSave &&
               gBinds.wasPressed(BIND_SAVESTATE_SAVE)) {
        saveState();
    } else if (!counterOwnsLoad &&
               gBinds.wasPressed(BIND_SAVESTATE_LOAD)) {
        PracticeSession::armLoadHold(gBinds.get(BIND_SAVESTATE_LOAD));
        // Pin before the unsaved-ghost prompt; changing selection cannot
        // redirect a confirmation or a card-busy load to another state.
        sPendingSD = sSelectedSD;
        sPendingSlot = sLoadSlot;
        sPendingGeneration = slotInfo(sPendingSlot).generation;
        if (!WarpWheel::requestSavestateLoad()) {
            sAwaitingLoadApproval = true;
            return;
        }
        // TApplication still runs the fader and gpMSound->mainLoop(), then
        // submits the rest of the frame after this hook returns. Restoring here
        // made those systems consume half-live/half-restored state. Defer the
        // operation until after the post-render GXDrawDone barrier instead.
        mLoadPending = true;
        mLoadWaitFrames = 0;
        SET_STATUS("loading");
    } else if (gBinds.wasPressed(BIND_SAVESTATE_CYCLE)) {
        cycleSlot();
    } else if (gBinds.wasPressed(BIND_SAVESTATE_CYCLE_SAVE)) {
        cycleSaveSlot();
    } else if (gBinds.wasPressed(BIND_SAVESTATE_CYCLE_LOAD)) {
        cycleLoadSlot();
    }
}

void SavestateManager::processPendingLoad() {
    if (diskBusy() && !sDiskLoadReady) return;
    if (!mLoadPending) {
        return;
    }

    // The card worker can remain busy across many scheduler yields. Wait in
    // actual rendered frames so an ordinary save finishes without dropping
    // the user's one-shot load request.
    if (gpCardManager && gpCardManager->getLastStatus() == CARD_ERROR_BUSY) {
        if (++mLoadWaitFrames < 600) return;
        mLoadPending = false;
        mLoadWaitFrames = 0;
        if (sDiskLoadReady) rebaseMissionStopwatch(sDiskStarted);
        sDiskLoadReady = false;
        PracticeSession::cancelLoadHold();
        feedback("E:cardbsy", "Memory card busy - try again");
        return;
    }

    // Clear first so a rejected snapshot is not retried every frame.
    mLoadPending = false;
    mLoadWaitFrames = 0;
    if (sDiskLoadReady) {
        const bool restored = loadSlot(kSlotCount, sPendingGeneration);
        if (!restored) {
            rebaseMissionStopwatch(sDiskStarted);
            PracticeSession::cancelLoadHold();
        }
        sDiskLoadReady = false;
        sDiskStatus = sDiskRecovered ? "SD read failed; RAM state restored" :
            restored ? "SD state loaded" : "SD state could not be restored";
    } else if (sPendingSD.id) {
        if (!beginSDLoad(sPendingSD.id, sPendingSD.headerCrc, sPendingSD.packedSize, true))
            PracticeSession::cancelLoadHold();
    } else if (!loadSlot(sPendingSlot, sPendingGeneration)) PracticeSession::cancelLoadHold();
    memset(&sPendingSD, 0, sizeof(sPendingSD));
}

void SavestateManager::draw(Menu *menu) {
    if (mFeedbackFrames > 0) mFeedbackFrames--;
#if ENABLE_SAVESTATE_DBG
    if (menu)
        menu->drawTextBaseline(sStatusBuf, 20, 60, 18, 18,
                               JUtility::TColor(255, 200, 0, 255));
#endif
    if (!menu || menu->shown() || mFeedbackFrames <= 0 ||
        !gSettings.getBool(SETTING_SAVESTATE_FEEDBACK)) return;
    gCreationExtras.drawSavestateFeedback(menu, mFeedback);
}
