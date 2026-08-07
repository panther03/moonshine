#ifndef SUSAMUNE_ADDRESSES_HXX
#define SUSAMUNE_ADDRESSES_HXX

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

// Mutable static storage that belongs to the game, not JSystem or the OS.
//
// These are deliberately NOT linker-section ends. In each retail build the
// game globals are followed by JSystem / runtime globals in the same section;
// restoring those would desynchronise the renderer, DSP, heap, and OS state
// from their live hardware state. The end values are the first system-owned
// symbol in the corresponding map section.
#define SUSAMUNE_ADDR_GAME_BSS_START \
    SUSAMUNE_MEM1_ADDR(0x803f1c50u, 0x803fb2a0u, 0x803f2a40u)
#define SUSAMUNE_ADDR_GAME_BSS_END \
    SUSAMUNE_MEM1_ADDR(0x80400b8cu, 0x803fd548u, 0x803f4ce8u)
#define SUSAMUNE_ADDR_GAME_SDATA_START \
    SUSAMUNE_MEM1_ADDR(0x80409008u, 0x8040c778u, 0x80403ed8u)
#define SUSAMUNE_ADDR_GAME_SDATA_END \
    SUSAMUNE_MEM1_ADDR(0x804097acu, 0x8040cc00u, 0x80404360u)
#define SUSAMUNE_ADDR_GAME_SBSS_START \
    SUSAMUNE_MEM1_ADDR(0x8040a208u, 0x8040e090u, 0x80405758u)
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

// Arena window reserved for the injected mod.
#define SUSAMUNE_ADDR_MOD_BASE \
    SUSAMUNE_MEM1_ADDR(0x80426020u, 0x80429800u, 0x80420d60u)
#define SUSAMUNE_MOD_REGION_SIZE 0x8000u
#define SUSAMUNE_SCRATCH 0x40u // idk
#define SUSAMUNE_MOD_BLOB_MAX_SIZE (SUSAMUNE_MOD_REGION_SIZE - SUSAMUNE_SCRATCH)

// The `.guicfg` section (gui_config.hxx), pinned at the very base of the blob
// so its address survives a rebuild: the web configurator ships the user's GUI
// appearance as a Gecko code writing literal addresses inside it, and the QF
// timer's asm caves reach its state half the same way. link_mod.py passes the
// matching --section-start and starts the code after it; MUST equal
// patches.gui_block_size.
#define SUSAMUNE_ADDR_GUI_BLOCK SUSAMUNE_ADDR_MOD_BASE
#define SUSAMUNE_GUI_BLOCK_SIZE 0xA0u

// The mod links at __ArenaLo, but OSInit hands the debug stack back to the
// arena when no debug monitor is present (BI2DebugFlag < 2), leaving the
// runtime __OSArenaLo at ALIGN32(_stack_addr) -- exactly this much BELOW
// __ArenaLo in all three regions. getArenaLo() reserves from the runtime
// value, so it must cover this gap as well as the region itself, or the root
// heap starts 0x2000 INTO the blob. link_mod.py re-checks the gap per region.
#define SUSAMUNE_DEBUG_STACK_SIZE 0x2000u
#define SUSAMUNE_ARENA_RESERVE_SIZE \
    (SUSAMUNE_MOD_REGION_SIZE + SUSAMUNE_DEBUG_STACK_SIZE)

// This is an optional Quick-Freeze practice-code heap flag, not a game-map symbol.
#define SUSAMUNE_ADDR_QF_TIMER_RESET \
    SUSAMUNE_MEM1_ADDR(0x817f00b3u, 0x817f00b3u, 0x817f00b3u)

// Scratch for the mod's asm caves. A cave can only reach a *fixed* address --
// it has no way to find a mod global -- so this is the top 16 bytes of the
// mod's own reserved arena window, [arena_lo, arena_lo + mod_region_size) from
// scripts/patches.py, which getArenaLo() keeps the game's heap out of. The
// blob starts at arena_lo; link_mod.py enforces mod_blob_max_size (this
// address minus the base) on every build and prints the section layout, so
// growing into this is a build failure rather than silent corruption.
//
// Do NOT put mod scratch in the practice codes' region at 0x817f0000+: that
// sits ABOVE __ArenaHi (0x81700000), where the apploader's FST and, on
// console, Nintendont's cheat/code-handler area live. It is only free when a
// .gct and its handler have been loaded and have lowered the arena top --
// susamune reserves nothing there.
#define SUSAMUNE_ADDR_MOD_SCRATCH \
    (SUSAMUNE_ADDR_MOD_BASE + SUSAMUNE_MOD_BLOB_MAX_SIZE)

// One word: the frame of the last TShine::touchPlayer, for No Shine Get
// Animation's debounce (features.cpp).
#define SUSAMUNE_ADDR_SHINE_TOUCH_FRAME (SUSAMUNE_ADDR_MOD_SCRATCH + 0x0u)

#endif // SUSAMUNE_ADDRESSES_HXX
