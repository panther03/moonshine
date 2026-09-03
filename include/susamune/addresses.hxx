#ifndef SUSAMUNE_ADDRESSES_HXX
#define SUSAMUNE_ADDRESSES_HXX

#include "susamune/mod_bin.h"

// Region-specific MEM1 layout for the supported retail revisions. Keep every
// game-memory address here; see the corresponding maps/<version>.map file.
// The build selects exactly one SUSAMUNE_VERSION_* definition in CMake.
#if (defined(SUSAMUNE_VERSION_JP) + defined(SUSAMUNE_VERSION_US) + \
     defined(SUSAMUNE_VERSION_PAL)) != 1
#error "Define exactly one SUSAMUNE_VERSION_JP, SUSAMUNE_VERSION_US, or SUSAMUNE_VERSION_PAL"
#endif

#if defined(SUSAMUNE_VERSION_JP)
#define SUSAMUNE_MEM1_ADDR(jp, us, pal) (jp)
#define SUSAMUNE_GAME_VERSION 1u
#elif defined(SUSAMUNE_VERSION_US)
#define SUSAMUNE_MEM1_ADDR(jp, us, pal) (us)
#define SUSAMUNE_GAME_VERSION 2u
#elif defined(SUSAMUNE_VERSION_PAL)
#define SUSAMUNE_MEM1_ADDR(jp, us, pal) (pal)
#define SUSAMUNE_GAME_VERSION 3u
#endif

// OS and TApplication globals.
#define SUSAMUNE_ADDR_OS_ARENA_LO \
    SUSAMUNE_MEM1_ADDR(0x80408d08u, 0x8040ce48u, 0x804045a8u)
#define SUSAMUNE_ADDR_APPLICATION \
    SUSAMUNE_MEM1_ADDR(0x803e6000u, 0x803e9700u, 0x803e10c0u)
#define SUSAMUNE_ADDR_APPLICATION_GAMEPAD(index) \
    (SUSAMUNE_ADDR_APPLICATION + 0x20u + 4u * (index))
#define SUSAMUNE_ADDR_APPLICATION_FADER \
    (SUSAMUNE_ADDR_APPLICATION + 0x34u)

// CARDCheck's synchronous wrapper helpers and OSFont.c's encoding selector.
// The latter is the halfword changed by the original Force ANSI/SJIS codes.
#define SUSAMUNE_ADDR_CARD_SYNC_CALLBACK \
    SUSAMUNE_MEM1_ADDR(0x8009ec50u, 0x80354330u, 0x8034c550u)
#define SUSAMUNE_ADDR_CARD_SYNC \
    SUSAMUNE_MEM1_ADDR(0x8009fe00u, 0x803554e0u, 0x8034d700u)
#define SUSAMUNE_ADDR_CARD_BLOCKS \
    SUSAMUNE_MEM1_ADDR(0x803ebf60u, 0x80403460u, 0x803fac00u)
#define SUSAMUNE_ADDR_FONT_ENCODING \
    SUSAMUNE_MEM1_ADDR(0x80408d18u, 0x8040ce58u, 0x804045b8u)

// Mutable static storage that belongs to the game, not JSystem or the OS.
//
// These are deliberately NOT linker-section ends. In each retail build the
// game globals are followed by JSystem / runtime globals in the same section;
// restoring those would desynchronise the renderer, DSP, heap, and OS state
// from their live hardware state. The end values are the first system-owned
// symbol in the corresponding map section.
#define SUSAMUNE_ADDR_GAME_BSS_START \
    SUSAMUNE_MEM1_ADDR(0x803f1c50u, 0x803e9750u, 0x803e1110u)
#define SUSAMUNE_ADDR_GAME_BSS_END \
    SUSAMUNE_MEM1_ADDR(0x80400b8cu, 0x803fd548u, 0x803f4ce8u)
#define SUSAMUNE_ADDR_GAME_SDATA_START \
    SUSAMUNE_MEM1_ADDR(0x80408dc0u, 0x8040c1e8u, 0x80403988u)
#define SUSAMUNE_ADDR_GAME_SDATA_END \
    SUSAMUNE_MEM1_ADDR(0x804097acu, 0x8040cc00u, 0x80404360u)
#define SUSAMUNE_ADDR_GAME_SBSS_START \
    SUSAMUNE_MEM1_ADDR(0x8040a208u, 0x8040cf00u, 0x80404660u)
#define SUSAMUNE_ADDR_GAME_SBSS_END \
    SUSAMUNE_MEM1_ADDR(0x8040b45cu, 0x8040e228u, 0x80405900u)
#define SUSAMUNE_ADDR_LIBC_RAND_SEED \
    SUSAMUNE_MEM1_ADDR(0x80408cf0u, 0x8040ce30u, 0x80404590u)

// Root-heap object pointers. The pointer variables are static, while their
// targets live outside the stage heap and must be captured separately.
#define SUSAMUNE_ADDR_RUMBLE_MANAGER \
    SUSAMUNE_MEM1_ADDR(0x8040a248u, 0x8040e0d0u, 0x80405798u)
#define SUSAMUNE_ADDR_FLAG_MANAGER_INSTANCE \
    SUSAMUNE_MEM1_ADDR(0x8040a290u, 0x8040e160u, 0x80405828u)
#define SUSAMUNE_ADDR_TIME_REC_INSTANCE \
    SUSAMUNE_MEM1_ADDR(0x8040a2f8u, 0x8040e1c8u, 0x804058a0u)

// Enemy vtables used by Pattern Selector. The original Gecko code compares
// these same two values at TSpineEnemy::goToRandomNextGraphNode + 0x50.
#define SUSAMUNE_VT_FIRE_WANWAN \
    SUSAMUNE_MEM1_ADDR(0x803d87c0u, 0x803b3f88u, 0x803abda8u)
#define SUSAMUNE_VT_BOSS_WANWAN \
    SUSAMUNE_MEM1_ADDR(0x803da9c0u, 0x803b6178u, 0x803adf98u)
#define SUSAMUNE_VT_BOSS_PAKKUN \
    SUSAMUNE_MEM1_ADDR(0x803d8e0cu, 0x803b45d4u, 0x803ac3f4u)
#define SUSAMUNE_VT_POIHANA_COLLISION \
    SUSAMUNE_MEM1_ADDR(0x803dbd78u, 0x803b7530u, 0x803af350u)
#define SUSAMUNE_VT_BOSS_EEL \
    SUSAMUNE_MEM1_ADDR(0x803dd4f4u, 0x803b8e1cu, 0x803b0c3cu)
#define SUSAMUNE_VT_BOSS_EEL_BODY_COLLISION \
    SUSAMUNE_MEM1_ADDR(0x803dd80cu, 0x803b9134u, 0x803b0f54u)

// TGraphWeb::findNearestNodeIndex. This symbol is absent from the project's
// generated linker scripts in some checkouts, so Pattern Selector calls the
// verified retail address directly instead of depending on a linker alias.
#define SUSAMUNE_ADDR_GRAPH_FIND_NEAREST_NODE \
    SUSAMUNE_MEM1_ADDR(0x8025f410u, 0x8004c2c0u, 0x8004b414u)

// Arena window reserved for the injected mod.
#define SUSAMUNE_ADDR_MOD_BASE \
    SUSAMUNE_MEM1_ADDR(SUSAMUNE_MOD_BASE_JP, SUSAMUNE_MOD_BASE_US, \
                       SUSAMUNE_MOD_BASE_PAL)
#define SUSAMUNE_ADDR_MOD_ATTACHMENT_HEAP \
    (SUSAMUNE_ADDR_MOD_BASE + SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET)

// The mod links at __ArenaLo, but OSInit hands the debug stack back to the
// arena when no debug monitor is present (BI2DebugFlag < 2), leaving the
// runtime __OSArenaLo at ALIGN32(_stack_addr) -- exactly this much BELOW
// __ArenaLo in all three regions. getArenaLo() reserves from the runtime
// value, so it must cover this gap as well as the region itself, or the root
// heap starts 0x2000 INTO the blob. link_mod.py re-checks the gap per region.
// This is an optional Quick-Freeze practice-code heap flag, not a game-map symbol.
#define SUSAMUNE_ADDR_QF_TIMER_RESET \
    SUSAMUNE_MEM1_ADDR(0x817f00b3u, 0x817f00b3u, 0x817f00b3u)

// Scratch for the mod's asm caves. A cave can only reach a *fixed* address --
// it has no way to find a mod global -- so this is the final 64 bytes of the
// mod's own reserved arena window, [arena_lo, arena_lo + mod_region_size) from
// scripts/patches.py, which getArenaLo() keeps the game's heap out of. The
// The build ceiling is deliberately lower than this physical tail. The blob
// still cannot grow into scratch, and link_mod.py checks both shared values.
//
// Do NOT put mod scratch in the practice codes' region at 0x817f0000+: that
// sits ABOVE __ArenaHi (0x81700000), where the apploader's FST and, on
// console, Nintendont's cheat/code-handler area live. It is only free when a
// .gct and its handler have been loaded and have lowered the arena top --
// susamune reserves nothing there.
#define SUSAMUNE_ADDR_MOD_SCRATCH \
    (SUSAMUNE_ADDR_MOD_BASE + SUSAMUNE_MOD_SCRATCH_OFFSET)

// One word: the frame of the last TShine::touchPlayer, for No Shine Get
// Animation's debounce (features.cpp).
#define SUSAMUNE_ADDR_SHINE_TOUCH_FRAME (SUSAMUNE_ADDR_MOD_SCRATCH + 0x0u)

// Monotonic shine-grab event serial shared by the winDemo call-site hook and
// No Shine Get Animation's replacement cave. AttemptCounter polls it after
// the director update, so both paths report the same event without touching
// the game's or Gecko's storage.
#define SUSAMUNE_ADDR_ATTEMPT_SHINE_SERIAL \
    (SUSAMUNE_ADDR_MOD_SCRATCH + 0x4u)

// Monotonic count of moveStage calls where the game's current and previous
// scenes differ, sampled at the same instruction as the original Gecko
// Attempt Counter. Sampling later sees the newly selected episode and falsely
// treats entering it as a success.
#define SUSAMUNE_ADDR_ATTEMPT_DEPARTURE_SERIAL \
    (SUSAMUNE_ADDR_MOD_SCRATCH + 0x8u)

// The TShine event id published immediately before the shine-grab serial.
#define SUSAMUNE_ADDR_LAST_SHINE_ID (SUSAMUNE_ADDR_MOD_SCRATCH + 0xCu)

// Sixteen-byte native QFT state. Fixed scratch is used because the timer's
// tiny regional asm hooks cannot address a linker-placed C++ global.
#define SUSAMUNE_ADDR_QFT_STATE (SUSAMUNE_ADDR_MOD_SCRATCH + 0x10u)
#define SUSAMUNE_QFT_STATE_STOP_OFF          0x0u
#define SUSAMUNE_QFT_STATE_RESTART_OFF       0x1u
#define SUSAMUNE_QFT_STATE_STOP_REASON_OFF   0x2u
#define SUSAMUNE_QFT_STATE_OFFSET_OFF        0x4u
#define SUSAMUNE_QFT_STATE_FREEZE_QF_OFF     0x8u
#define SUSAMUNE_QFT_STATE_FREEZE_FRAMES_OFF 0xCu

// Exact local-QF events used by Plaza IL endpoints. A negative value means
// no unconsumed event; the PPC hooks and native timer share this scratch.
#define SUSAMUNE_ADDR_QFT_DEATH_QF (SUSAMUNE_ADDR_MOD_SCRATCH + 0x20u)
#define SUSAMUNE_ADDR_QFT_PLANT_QF (SUSAMUNE_ADDR_MOD_SCRATCH + 0x24u)

// Actor loading zones publish timing and destination together. This is
// separate from freezeQf because demo freezes may overwrite that display slot.
#define SUSAMUNE_ADDR_QFT_TRANSITION_QF (SUSAMUNE_ADDR_MOD_SCRATCH + 0x28u)
#define SUSAMUNE_ADDR_QFT_TRANSITION_TARGET (SUSAMUNE_ADDR_MOD_SCRATCH + 0x2Cu)

#endif // SUSAMUNE_ADDRESSES_HXX
