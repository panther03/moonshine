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
#include "susamune/ghost.hxx"
#include "susamune/ghost_storage.hxx"
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
#include "Dolphin/CARD.h"
#include "Dolphin/GX.h"
#include "Dolphin/mem.h"
#include "Dolphin/OS.h"
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
static const u32 kSnapshotBase = SUSAMUNE_DOLPHIN_SNAPSHOT_PPC_BASE;
#else
// Wii: a dedicated 16 MiB window. The custom Nintendont memory map relocates
// all of its former users below this address; the ARM kernel begins exactly at
// the window's exclusive end.
static const u32 kSnapshotBase = SUSAMUNE_MEM2_SNAPSHOT_PPC_BASE;
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
    // JP only: the game half of .bss [GAME_BSS_START, GAME_BSS_END) with the SMS
    // audio modules carved OUT. Those modules (System.a/MSoundMainSide and the
    // whole MSound.a cluster) sit inside the game-bss window yet hold live
    // JAudio handles/track pointers. We stopAllSound() on both save and load,
    // which resets JAudio, so restoring an OLD copy of those handles points them
    // at track state that has since been freed/reused -> dangling deref, an
    // intermittent crash when loading during e.g. a Pianta-talk shine-get
    // (talkModeIn + shine BGM leave those handles live). Leaving the audio BSS
    // untouched keeps it consistent with the post-stopAllSound JAudio state,
    // matching the "we deliberately don't snapshot audio" design. Boundaries
    // from maps/jp.map:
    //   MSoundMainSide.cpp .bss = [0x803f2c38, 0x803f2cf0)
    //   MSound.a cluster   .bss = [0x803f44d0, 0x803f57a0)  (MAnmSound..MSModBgm)
    // US/PAL keep the window contiguous below until their audio modules are
    // mapped in maps/us.map / maps/pal.map.
    { SUSAMUNE_ADDR_GAME_BSS_START, 0x803f2c38u, kNoGate }, // .. before MSoundMainSide
    { 0x803f2cf0u,                  0x803f44d0u, kNoGate }, // after MSoundMainSide .. before MSound.a
    { 0x803f57a0u,                  SUSAMUNE_ADDR_GAME_BSS_END, kNoGate }, // after MSound.a ..
#else
    { SUSAMUNE_ADDR_GAME_BSS_START, SUSAMUNE_ADDR_GAME_BSS_END, kNoGate },
#endif
    { SUSAMUNE_ADDR_GAME_SDATA_START, SUSAMUNE_ADDR_GAME_SDATA_END, kNoGate },
    { SUSAMUNE_ADDR_GAME_SBSS_START, SUSAMUNE_ADDR_GAME_SBSS_END, kNoGate },
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
const u32 kSnapshotVersion = 11u;         // 512 KiB mod region: the stage heap moved
const u32 kHeaderSize      = 0x100u;
// One slot per static range, one per pointed alloc, plus one for the heap.
const int kMaxRegions      = kNumStaticRanges + kNumPointedAllocs + 1;

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
    u16 _pad0;
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

inline SavestateHeader *headerPtr() {
    return reinterpret_cast<SavestateHeader *>(kSnapshotBase);
}
inline u8 *bufferPtr() {
    return reinterpret_cast<u8 *>(kSnapshotBase + kHeaderSize);
}

__attribute__((noinline)) u32 captureRegion(SavestateHeader *h, u32 offset,
                                            u32 addr, u32 size) {
    memcpy(bufferPtr() + offset, reinterpret_cast<const void *>(addr), size);
    RegionEntry &region = h->regions[h->region_count++];
    region.addr         = addr;
    region.size         = size;
    region.buf_offset   = offset;
    return offset + size;
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
    if (!gpMarDirector) {
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

#if ENABLE_SAVESTATE_DBG
    setStatus("ready");
#endif

    // Mark the snapshot buffer empty so a stale MEM2 load doesn't
    // accidentally pass the magic check after a cold boot.
    headerPtr()->magic = 0;
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
    if (!gSettings.getBool(SETTING_SAVESTATE_FEEDBACK)) {
        mFeedbackFrames = 0;
        return;
    }
    strncpy(mFeedback, message, sizeof(mFeedback) - 1);
    mFeedback[sizeof(mFeedback) - 1] = '\0';
    mFeedbackFrames = Menu::kToastFrames;
}

bool SavestateManager::saveState() {
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
    const u32 heapEnd   = reinterpret_cast<u32>(heap->mEnd);
    if (heapEnd <= heapStart) {
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

    // Audio engine has DSP-side state we can't snapshot; quiet it before
    // we touch anything so the current-sounds list won't reference freed
    // tracks after a future restore.
    if (gpMSound) {
        gpMSound->stopAllSound();
    }

    // NOTE (disabled): draining the GP here (GXDrawDone) would guarantee the
    // frame's async GXCopyTex writes -- e.g. the pollution "goop" texture
    // copied back to mPollutionMap -- have landed in RAM before we read it,
    // at the cost of a one-frame stall. Not needed in practice: on console
    // the buffer is at worst one frame stale, and on Dolphin correctness
    // depends on Texture Cache Accuracy = Safe, not on this. Re-enable if a
    // console test shows a torn/stale goop snapshot. (#include "Dolphin/GX.h")
    // GXDrawDone();

    // We are called from inside onUpdate, which runs on the main thread
    // between director->direct() and rendering. That is already the most
    // quiescent point in the frame, but disable interrupts anyway so we
    // don't race a VI retrace callback that touches heap objects.
    bool ints = OSDisableInterrupts();
    // Silence the DAC for the whole interrupts-off window so the frozen audio
    // DMA doesn't buzz; restored just before interrupts come back.
    bool dma = muteAudioDma();

    SavestateHeader *h = headerPtr();
    h->magic        = 0; // committed at end as a torn-write guard
    h->version      = kSnapshotVersion;
    h->game_version = SUSAMUNE_GAME_VERSION;
    h->heap_addr    = heapStart;
    h->heap_size    = heapSize;
    h->area_id      = gpApplication.mCurrentScene.mAreaID;
    h->episode_id   = gpApplication.mCurrentScene.mEpisodeID;
    h->_pad0        = 0;
    h->region_count = 0;
    h->save_time    = OSGetTime();

    u32 offset = 0;
    for (int i = 0; i < kNumStaticRanges; i++) {
        // Skip a setting-gated range when its setting is disabled (e.g. the RNG
        // seed when "Save RNG state" is Off). It simply won't be in the region
        // list, so a later load leaves that memory untouched.
        if (kStaticRanges[i].gate != kNoGate &&
            !gSettings.getBool((SettingId)kStaticRanges[i].gate)) {
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
        if (target >= heapStart && target + pa.size <= heapEnd) {
            continue;
        }
        offset = captureRegion(h, offset, target, pa.size);
    }

    // Heap last (largest payload).
    offset = captureRegion(h, offset, heapStart, heapSize);

    // Push the bytes out of dcache so a subsequent uncached read (e.g. by
    // a future load that swaps the BAT or by debug tooling) sees them.
    DCStoreRange(headerPtr(), kHeaderSize);
    DCStoreRange(bufferPtr(), offset);

    // Commit magic last. After this point the snapshot is loadable.
    h->magic = kSnapshotMagic;
    DCStoreRange(&h->magic, sizeof(h->magic));

    unmuteAudioDma(dma);
    OSRestoreInterrupts(ints);

    gQFTTimer.onSavestateSaved();
    ILing::onSavestateSaved();
    feedback("saved", "Savestate saved");
    return true;
}

bool SavestateManager::loadState() {
    // Refuse while a stage load is in flight or the intro sequence is playing;
    // overwriting a heap the setup thread is still filling crashes. See
    // inLoadTransition().
    if (inLoadTransition()) {
        feedback("E:loading", "Can't load during stage loading");
        return false;
    }

    SavestateHeader *h = headerPtr();
    if (h->magic != kSnapshotMagic) {
        feedback("E:nosnap", "No savestate yet");
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

    // Pointers in the snapshotted heap are absolute. If the heap moved
    // (different scenario, different boot path), restoring would scribble
    // stale pointers all over the place. Refuse the load.
    if (reinterpret_cast<u32>(heap) != h->heap_addr) {
        feedback("E:hpaddr", "Stage layout changed - save again");
        return false;
    }
    const u32 heapSize = reinterpret_cast<u32>(heap->mEnd)
                       - reinterpret_cast<u32>(heap);
    if (heapSize != h->heap_size) {
        feedback("E:hpsize", "Stage layout changed - save again");
        return false;
    }

    // Same-scenario only. Restoring across a moveStage() is more
    // complicated -- the heap freeAll()s and gets re-populated by the
    // new director's setup -- and not the use case we're after.
    if (h->area_id    != gpApplication.mCurrentScene.mAreaID
     || h->episode_id != gpApplication.mCurrentScene.mEpisodeID) {
        feedback("E:scene", "Savestate belongs to another area");
        return false;
    }

    // gpCardManager has its own worker thread, mutex, and cond var on the
    // root heap. Snapshotting/restoring it would trash kernel-side thread
    // bookkeeping, so we don't -- but we also can't safely tear down the
    // rest of the world while the card thread is mid-transaction (this is
    // why loading from the blue save screen used to crash). Spin here with
    // interrupts enabled until the card thread reports idle. CARD_ERROR_BUSY
    // is -1; any other value (including ready / error codes) means the
    // worker is parked waiting for its next command.
    if (gpCardManager) {
        int spins = 0;
        while (gpCardManager->getLastStatus() == CARD_ERROR_BUSY) {
            if (++spins > 600) { // ~10 s @ 60 Hz of yielding
                feedback("E:cardbsy", "Memory card busy - try again");
                return false;
            }
            OSYieldThread();
        }
    }

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

    bool ints = OSDisableInterrupts();
    // Silence the DAC across the interrupts-off restore so the frozen audio
    // DMA doesn't buzz; restored just before interrupts come back.
    bool dma = muteAudioDma();

    for (u32 i = 0; i < h->region_count; i++) {
        const RegionEntry &r = h->regions[i];
        memcpy(reinterpret_cast<void *>(r.addr), bufferPtr() + r.buf_offset,
               r.size);
        // memcpy() has already placed the restored bytes in D-cache, so the
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
    if (gpMarDirector) {
        OSTime delta                = OSGetTime() - h->save_time;
        gpMarDirector->mStopwatch.mLast += delta;
        DCStoreRange(&gpMarDirector->mStopwatch, sizeof(OSStopwatch));
    }

    unmuteAudioDma(dma);
    OSRestoreInterrupts(ints);

    gQFTTimer.onSavestateLoaded();
    SplitEvents::onSavestateLoaded();
    SplitStats::onSavestateLoaded();
    Ghost::onSavestateLoaded();
    GhostStorage::onSavestateLoaded();
    rngControlOnSavestateLoaded();
    MovementDisplay::onSavestateLoaded();
    gCreationExtras.onSavestateLoaded();
    // An armed warp lives in mod BSS, outside the restored game snapshot.
    // Cancel it before ILing adopts the save-time attempt state.
    LevelWarp::cancelPending();
    ILing::onSavestateLoaded();
    Records::onSavestateLoaded();
    feedback("loaded", "Savestate loaded");
    return true;
}

void SavestateManager::updateHook() {
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
    if (approvedLoad) {
        mLoadPending = true;
        SET_STATUS("loading");
    } else if (!counterOwnsSave &&
               gBinds.wasPressed(BIND_SAVESTATE_SAVE)) {
        saveState();
    } else if (!counterOwnsLoad &&
               gBinds.wasPressed(BIND_SAVESTATE_LOAD) &&
               WarpWheel::requestSavestateLoad()) {
        // TApplication still runs the fader and gpMSound->mainLoop(), then
        // submits the rest of the frame after this hook returns. Restoring here
        // made those systems consume half-live/half-restored state. Defer the
        // operation until after the post-render GXDrawDone barrier instead.
        mLoadPending = true;
        SET_STATUS("loading");
    }
}

void SavestateManager::processPendingLoad() {
    if (!mLoadPending) {
        return;
    }

    // Clear first so a rejected load is not retried every frame.
    mLoadPending = false;
    loadState();
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
