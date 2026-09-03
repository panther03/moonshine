// =====================================================================
// features.cpp
//
// The patch tables behind the ported gecko codes. A Feature is a SettingId
// plus Patch rows; each Patch overwrites (part of) one word in game memory.
// The original word is captured from the live game on first apply, so "Off"
// restores the retail instruction without hardcoding it per region.
//
// Addresses use SUSAMUNE_MEM1_ADDR(jp, us, pal) so one table serves all three
// revisions, and live here rather than addresses.hxx since each is meaningless
// outside its feature row.
//
// Source codes are in ../../src/gct-generator/Codes.xml; see
// doc/gecko_porting.md for the porting method and when NOT to use these
// tables (anything with real control flow belongs in C instead).
// =====================================================================

#include "susamune/features.hxx"
#include "susamune/ghost.hxx"
#include "susamune/settings.hxx"
#include "susamune/stage_loader.hxx"
#include "susamune/addresses.hxx"  // SUSAMUNE_MEM1_ADDR
#include "susamune/mem2_map.h"
#include "Dolphin/OS.h"            // DCFlushRange, ICInvalidateRange

namespace {

// One write to a 32-bit word. All sites are in 0x80xxxxxx MEM1, so that fixed
// byte carries the feature boundary and the row's runtime state. The low two
// address bits remain free for the halfword-select flags.
constexpr u32 kMaskHi           = 0x00000001u;
constexpr u32 kMaskLo           = 0x00000002u;
constexpr u32 kPatchAddrMask    = 0x00FFFFFFu;
constexpr u32 kPatchFeatureMask = 0x1F000000u;
constexpr u32 kPatchEarly       = 0x20000000u;
constexpr u32 kPatchCaptured    = 0x40000000u;
constexpr u32 kPatchOn          = 0x80000000u;
constexpr u32 kMem1Base         = 0x80000000u;

struct Patch {
    u32 addrState;
    u32 value;
};

static_assert(sizeof(Patch) == 8, "Patch rows must stay two words");
static_assert((kPatchAddrMask & (kPatchFeatureMask | kPatchEarly |
                                 kPatchCaptured | kPatchOn)) == 0,
              "patch address and metadata overlap");

template <u32 Addr>
constexpr u32 encodedPatchAddr() {
    static_assert((Addr & 0xFF000000u) == kMem1Base,
                  "patch site must be in 0x80xxxxxx MEM1");
    return Addr & kPatchAddrMask;
}

template <SettingId Id, bool Early>
constexpr u32 featureStart() {
    static_assert((u32)Id < 31u, "patch feature id exceeds five-bit tag");
    return ((u32)Id + 1u) << 24 | (Early ? kPatchEarly : 0u);
}

inline u32 patchAddr(const Patch &p) {
    u32 addr = p.addrState & kPatchAddrMask;
    return addr == 0 ? 0 : kMem1Base | (addr & ~(kMaskHi | kMaskLo));
}
inline u32 patchMask(const Patch &p) {
    if (p.addrState & kMaskHi) return 0xFFFF0000u;
    if (p.addrState & kMaskLo) return 0x0000FFFFu;
    return 0xFFFFFFFFu;
}

// Whole-word patch: `val` is the on-state word.
#define FWORD(jp, us, pal, val) \
    { encodedPatchAddr<SUSAMUNE_MEM1_ADDR(jp, us, pal)>(), (val) }
#define FBEGIN(id, jp, us, pal, val)                                      \
    { encodedPatchAddr<SUSAMUNE_MEM1_ADDR(jp, us, pal)>() |               \
          featureStart<id, false>(),                                      \
      (val) }
#define FBEGIN_EARLY(id, jp, us, pal, val)                                \
    { encodedPatchAddr<SUSAMUNE_MEM1_ADDR(jp, us, pal)>() |               \
          featureStart<id, true>(),                                       \
      (val) }
// High-halfword-only patch.
#define FHALFHI(jp, us, pal, val) \
    { encodedPatchAddr<SUSAMUNE_MEM1_ADDR(jp, us, pal)>() | kMaskHi, (val) }

// Patch at a hardcoded *heap* address (one region's, so these live inside a
// per-region #if). The mod raises the game's arena floor by
// SUSAMUNE_ARENA_RESERVE_SIZE, so every bottom-anchored heap allocation sits
// that much higher than the address a gecko code hardcodes -- add it back.
// PatchSusamuneGeckoCodes() in the launcher does the same to the .gct when
// these codes are run as gecko instead of ported.
#define FHEAP(addr, val) \
    { encodedPatchAddr<(addr) + SUSAMUNE_ARENA_RESERVE_SIZE>(), (val) }
#define FHEAPLO(addr, val) \
    { encodedPatchAddr<(addr) + SUSAMUNE_ARENA_RESERVE_SIZE>() | kMaskLo, \
      (val) }

// A patch row whose address is filled in at runtime (PAL Fast Text, below).
// applyPatches() skips rows that are still unresolved.
#define FUNRESOLVED() { 0u, 0u }

// ---------------------------------------------------------------------
// Flat patch stream. A tagged row starts a feature and every following
// untagged row belongs to it. Addresses are (jp=GMSJ01, us=GMSE01,
// pal=GMSP01); the on-state words come straight from Codes.xml.
// ---------------------------------------------------------------------

Patch gFeaturePatches[] = {
// Infinite Lives: nop the store that decrements the life counter.
//   jp 040EBD80 / us 04298814 / pal 042906AC  = 60000000 (nop)
    FBEGIN(SETTING_INFINITE_LIVES, 0x800EBD80, 0x80298814, 0x802906AC,
           0x60000000),

// Unlock Nozzles: force the "is this nozzle box unlocked?" query to return
// 1 (li r3,1 ; blr).
//   jp 040E79F8 / us 0429443C / pal 0428C254 = 38600001 (li r3,1)
//   jp 040E79FC / us 04294440 / pal 0428C258 = 4E800020 (blr)
    FBEGIN(SETTING_UNLOCK_NOZZLES, 0x800E79F8, 0x8029443C, 0x8028C254,
           0x38600001),
    FWORD(0x800E79FC, 0x80294440, 0x8028C258, 0x4E800020),

// Unlock Yoshi: two gecko C6 "insert branch" writes that skip the yoshi-lock
// checks. C6 writes a `b` to the target; here the targets are addr+0x34 and
// addr+0x1C, so the encoded branches are the same on every region.
//   jp C6193F58->80193F8C / us C61BBF70->801BBFA4 / pal C61B3E28->801B3E5C
//     = b +0x34 = 48000034
//   jp C6193F9C->80193FB8 / us C61BBFB4->801BBFD0 / pal C61B3E6C->801B3E88
//     = b +0x1C = 4800001C
    FBEGIN(SETTING_UNLOCK_YOSHI, 0x80193F58, 0x801BBF70, 0x801B3E28,
           0x48000034),
    FWORD(0x80193F9C, 0x801BBFB4, 0x801B3E6C, 0x4800001C),

// Any Fruit Opens Yoshi Eggs: nop the check that the offered fruit matches.
//   jp 041948E8 / us 041BC900 / pal 041B47B8 = 60000000 (nop)
    FBEGIN(SETTING_ANY_FRUIT_YOSHI, 0x801948E8, 0x801BC900, 0x801B47B8,
           0x60000000),

// Infinite Juice: nop the store that drains Yoshi's juice.
//   jp 0414DB88 / us 0426E810 / pal 0426659C = 60000000 (nop)
    FBEGIN(SETTING_INFINITE_JUICE, 0x8014DB88, 0x8026E810, 0x8026659C,
           0x60000000),

// Enable Exit Area Everywhere: C6 branch (b +0xC) skipping the check that
// gates the "Exit Area" pause option to normal stages.
//   jp C6218054->80218060 / us C6156B78->80156B84 / pal C614BB94->8014BBA0
//     = b +0xC = 4800000C
    FBEGIN(SETTING_EXIT_AREA_EVERYWHERE, 0x80218054, 0x80156B78,
           0x8014BB94, 0x4800000C),

// FMV Skips: force the "have I seen this FMV?" checks to return 1 so any FMV
// can be skipped on first view.
//   jp 0410AF5C / us 042B5EF4 / pal 042ADE20 = 38600001 (li r3,1)
//   jp 0410AFC0 / us 042B5E8C / pal 042ADE88 = 38600001 (li r3,1)
    FBEGIN(SETTING_FMV_SKIPS, 0x8010AF5C, 0x802B5EF4, 0x802ADE20,
           0x38600001),
    FWORD(0x8010AFC0, 0x802B5E8C, 0x802ADE88, 0x38600001),

// Intro Skip: boot straight to the title screen, no logos and no intro movie.
// FBEGIN_EARLY, i.e. applied from onAppInit. featuresApply() does run during the
// logo -- gameLoop() is what onUpdate hooks, and proc() runs it for the logo
// state too -- but it runs *after* director->direct(), and by the time it first
// fires the logo director has already committed to playing out. Console-tested:
// on the per-frame pass alone the logos still show.
//
//   direct_nlogo+0x24  the `beq` picking mState 2 (wait for fade-out) becomes
//                      an unconditional branch, so the logo never plays out.
//   direct+0x114       take the branch one instruction earlier, into
//                      `li r31, 4`, so direct() reports APP_STATE_DONE instead
//                      of advancing to the Dolby logo.
//   proc+0x248         the APP_STATE_DONE case, five words of `mNextArea.set(
//                      15, 0, 0)` before it falls through into the movie body.
//                      Replaced by mCurrArea = (AREA_OPTION, 0, 0) plus a
//                      branch back to the APP_STATE_GAMEPLAY body, which runs
//                      that area as a stage -- i.e. the title screen.
//
// The mCurrArea write is what makes this safe where the old jump-table port
// was not: APP_STATE_DONE is also where the console reset button lands, and
// naming the destination here sends reset to the title rather than to a reload
// of whatever stage was live. See doc/gecko_porting.md.
//
// Progressive/50-60Hz mode is chosen in the logo director's B-held path, so it
// cannot be changed while this is on -- boot once with it off to set it.
//   jp 040E8C68 / us 042956AC / pal 0428D4C4 = 480002C4 (pal 48000264)
//   jp 040E90DC / us 04295B20 / pal 0428D9B8 = 48000014
//   jp 060F9FE4 / us 062A65E0 / pal 0629E51C, 0x14 bytes
    FBEGIN_EARLY(
        SETTING_INTRO_SKIP, 0x800E8C68, 0x802956AC, 0x8028D4C4,
        SUSAMUNE_MEM1_ADDR(0x480002C4u, 0x480002C4u, 0x48000264u)),
    FWORD(0x800E90DC, 0x80295B20, 0x8028D9B8, 0x48000014),
    FWORD(0x800F9FE4, 0x802A65E0, 0x8029E51C, 0x38600F00),  // li r3, 0xF00
    FWORD(0x800F9FE8, 0x802A65E4, 0x8029E520, 0x38000000),  // li r0, 0
    FWORD(0x800F9FEC, 0x802A65E8, 0x8029E524, 0xB07F000E),  // sth r3, 0xE(r31)
    FWORD(0x800F9FF0, 0x802A65EC, 0x8029E528, 0xB01F0010),  // sth r0, 0x10(r31)
    FWORD(0x800F9FF4, 0x802A65F0, 0x8029E52C,
          SUSAMUNE_MEM1_ADDR(0x4BFFFEB0u, 0x4BFFFEB0u, 0x4BFFFE94u)),  // b back

// Respawn One-Time Shines: a C6-style branch plus two gecko 02 halfword
// writes that flip conditional branches to unconditional (high half -> 4800).
//   jp 041BF378 / us 041E792C / pal 041DF804 = 48000050 (b +0x50)
//   jp 021BFA48 / us 021E7FFC / pal 021DFED4 = 4800 (hi16) -> b
//   jp 021D72E8 / us 021FF85C / pal 021F7740 = 4800 (hi16) -> b
    FBEGIN(SETTING_RESPAWN_SHINES, 0x801BF378, 0x801E792C, 0x801DF804,
           0x48000050),
    FHALFHI(0x801BFA48, 0x801E7FFC, 0x801DFED4, 0x48000000),
    FHALFHI(0x801D72E8, 0x801FF85C, 0x801F7740, 0x48000000),

// Fruit Never Time Out: overwrite the fruit despawn-timer limit (a datum in
// sdata) with INT_MAX so fruit never expires.
//   jp 044091A8 / us 0440C918 / pal 04404078 = 7FFFFFFF
    FBEGIN(SETTING_FRUIT_NEVER_TIMEOUT, 0x804091A8, 0x8040C918,
           0x80404078, 0x7FFFFFFF),

// Mute Background Music: replace the BGM volume load with fsub f1,f1,f1 (=0).
//   jp 0417FF58 / us 04016A34 / pal 04016A90 = FC210828
    FBEGIN(SETTING_MUTE_BGM, 0x8017FF58, 0x80016A34, 0x80016A90,
           0xFC210828),

// Shine Outfit: force Mario's outfit id to the shine-shirt value. The shirt
// branch itself is owned by the appearance choice writer below so legacy and
// tri-state settings cannot race over the same instruction.
//   jp 04120D1C / us 04241FD4 / pal 04239C88 = 60000004 (ori r0,r0,4)
//   jp 04120D20 / us 04241FD8 / pal 04239C8C = B01D0004 (sth r0,4(r29))
    FBEGIN(SETTING_SHINE_OUTFIT, 0x80120D1C, 0x80241FD4, 0x80239C88,
           0x60000004),
    FWORD(0x80120D20, 0x80241FD8, 0x80239C8C, 0xB01D0004),

// Shiny Shines: branch (b +0x4C) past the "already collected?" test so every
// shine renders yellow.
//   jp 04194E24 / us 041BCE3C / pal 041B4CF4 = 4800004C
    FBEGIN(SETTING_SHINY_SHINES, 0x80194E24, 0x801BCE3C, 0x801B4CF4,
           0x4800004C),

// Shadow Mario HP Meter: nop the instruction that suppresses the HP bar, so
// it shows whenever Shadow Mario is hit by water.
//   jp 04253748 / us 0403FD94 / pal 0403FBE4 = 60000000 (nop)
    FBEGIN(SETTING_SHADOW_MARIO_HP, 0x80253748, 0x8003FD94, 0x8003FBE4,
           0x60000000),

// -- Companion static writes for the asm-hook codes below (kAsmHooks). These
//    are the non-C2 lines of those gecko codes; they share the code's setting
//    so the whole feature toggles together. --

// Free Pause: gecko C6 branch (b +0xC) that skips a pause-gate check, paired
// with the Free Pause hook below.
//   jp C60EB06C->800EB078 / us C6297AB0->80297ABC / pal C628F948->8028F954
    FBEGIN(SETTING_FREE_PAUSE, 0x800EB06C, 0x80297AB0, 0x8028F948,
           0x4800000C),

// Disable Blue Coin Flag: nop the store that sets the collected flag; paired
// with the kBlueCoinAsm hook (which zeroes the count field).
//   jp 040E7B20 / us 04294564 / pal 0428C37C = 60000000 (nop)
    FBEGIN(SETTING_DISABLE_BLUE_COIN, 0x800E7B20, 0x80294564, 0x8028C37C,
           0x60000000),

// Never Pause IGT: two blr writes that early-return the timer-pause paths;
// paired with the kNeverPauseAsm hook.
//   jp 04092968 / us 04348048 / pal 043402A4 = 4E800020 (blr)
//   jp 0420CA84 / us 0414B628 / pal 041402B8 = 4E800020 (blr)
    FBEGIN(SETTING_NEVER_PAUSE_IGT, 0x80092968, 0x80348048, 0x803402A4,
           0x4E800020),
    FWORD(0x8020CA84, 0x8014B628, 0x801402B8, 0x4E800020),

// Force Plaza Events: three "b +0x18" branches and two nops that reroute the
// plaza-event setup; paired with the Force Plaza Events hook below.
//   jp 0410C4C8 / us 042B7810 / pal 042AF7E0 = 48000018 (b +0x18)
//   jp 0410C514 / us 042B785C / pal 042AF82C = 48000018 (b +0x18)
//   jp 0410C57C / us 042B78C4 / pal 042AF894 = 48000018 (b +0x18)
//   jp 0410C5A8 / us 042B78F0 / pal 042AF8C0 = 60000000 (nop)
//   jp 0410C5F8 / us 042B7940 / pal 042AF910 = 60000000 (nop)
    FBEGIN(SETTING_FORCE_PLAZA_EVENTS, 0x8010C4C8, 0x802B7810,
           0x802AF7E0, 0x48000018),
    FWORD(0x8010C514, 0x802B785C, 0x802AF82C, 0x48000018),
    FWORD(0x8010C57C, 0x802B78C4, 0x802AF894, 0x48000018),
    FWORD(0x8010C5A8, 0x802B78F0, 0x802AF8C0, 0x60000000),
    FWORD(0x8010C5F8, 0x802B7940, 0x802AF910, 0x60000000),

// Fast Text (from DPad Functions, mode bit 0x008). Forces the dialog/talk
// system to a fixed short message ("!!!") instead of the real text, by
// replacing three load instructions (Talk2D2 / EventWatcher) with immediates.
// Off restores the originals (captured), so this is a plain BOOL patch.
//   jp 80215290 / us 80153DA0 / pal 80148D20 = 38000000 (li r0, 0)
//   jp 80214610 / us 8015317C / pal 80147F98 = 38005000 (li r0, 0x5000)
//   jp 800E4888 / us 80291340 / pal 802890CC = 60000000 (nop)
//
// The forced message is only short *text* because the gecko's unconditional
// tail lines plant it in the loaded message buffer; without them the fixed
// message id resolves to whatever happens to sit there. Those writes target a
// hardcoded heap address, so they need the arena-reserve fixup (FHEAP).
//   jp 028D8A7E 00028149 -> "!" (Shift-JIS 8149) x3 at 808D8A7E..83
//      048D8A84 00000000 -> terminator
//   us 048D3A3C 21000000 -> "!" + terminator (US/PAL fonts map ASCII directly)
//   pal one buffer per language, resolved at runtime -- see kFastTextPalMsg.
#if defined(SUSAMUNE_VERSION_JP)
#define FAST_TEXT_MSG_ROWS                            \
    FHEAPLO(0x808D8A7Cu, 0x00008149u),                \
    FHEAP(0x808D8A80u, 0x81498149u),                  \
    FHEAP(0x808D8A84u, 0x00000000u),
#elif defined(SUSAMUNE_VERSION_US)
#define FAST_TEXT_MSG_ROWS FHEAP(0x808D3A3Cu, 0x21000000u),
#else
#define FAST_TEXT_MSG_ROWS FUNRESOLVED(),
#endif

    FBEGIN(SETTING_FAST_TEXT, 0x80215290, 0x80153DA0, 0x80148D20,
           0x38000000),
    FWORD(0x80214610, 0x8015317C, 0x80147F98, 0x38005000),
    FWORD(0x800E4888, 0x80291340, 0x802890CC, 0x60000000),
    FAST_TEXT_MSG_ROWS
#undef FAST_TEXT_MSG_ROWS

// Disable Z Menu: skip the updateGameMode path that opens the stock map.
// Z remains available to mod binds and every other game input path.
//   jp C60EB020->800EB02C / us C6297A64->80297A70 / pal C628F8FC->8028F908
    FBEGIN(SETTING_DISABLE_Z_MENU, 0x800EB020, 0x80297A64, 0x8028F8FC,
           0x4800000C),
};

constexpr int kNumFeaturePatches =
    (int)(sizeof(gFeaturePatches) / sizeof(gFeaturePatches[0]));
constexpr int kNumEarlyPatches = 7;
constexpr int kNumPatches      = kNumFeaturePatches - kNumEarlyPatches;
#if defined(SUSAMUNE_VERSION_JP)
static_assert(kNumFeaturePatches == 42 && kNumPatches == 35,
              "JP patch stream shape changed");
#else
static_assert(kNumFeaturePatches == 40 && kNumPatches == 33,
              "US/PAL patch stream shape changed");
#endif

#if defined(SUSAMUNE_VERSION_PAL)
// PAL ships one message file per language and each loads at its own address,
// so the gecko compares a language word before writing. Indexed by that word;
// values are the gecko's, addresses still unrelocated (FHEAP is not usable in
// a plain table, the reserve is added in resolveFastTextPalMsg).
//   0474E87C 21000000 / 0474E9F4 21210000 / 0474ED38 00000000 /
//   0474EE04 A1000000 / 0474EBDC 21210000
const struct {
    u32 addr;
    u32 val;
} kFastTextPalMsg[] = {
    { 0x8074E87Cu, 0x21000000u },  // English  "!"
    { 0x8074E9F4u, 0x21210000u },  // German   "!!"
    { 0x8074ED38u, 0x00000000u },  // French   (terminator only)
    { 0x8074EE04u, 0xA1000000u },  // Spanish  "\xA1"
    { 0x8074EBDCu, 0x21210000u },  // Italian  "!!"
};

// 20570B7C in dpad_pal.txt: the language word the five writes are gated on.
#define FAST_TEXT_PAL_LANG_ADDR 0x80570B7Cu
#endif

// A feature that has to be installed before the app state machine runs at all.
// featuresApplyEarly() (from onAppInit) writes these rows once and the
// per-frame pass ignores its rows entirely. Its sites have already run by the
// time the menu exists, which makes restoring them pointless. Off simply does
// not write, and the next boot loads the game's own code from disc.
// Retail word at each patch site, flattened across the per-frame features in
// stream order and captured lazily on first apply. Capture/install state lives
// in addrState because Fast Text's heap words can legitimately be zero.
#define gPatchOrig (*reinterpret_cast<u32 (*)[kNumPatches]>( \
    SUSAMUNE_MEM2_FEATURE_RUNTIME_PPC_BASE))
// Whether our word is currently installed. Writes happen on the transition
// only -- NOT whenever the site differs from what we want. Some sites are heap
// words the game rewrites on its own (again Fast Text's message buffer), and
// re-asserting a disabled patch's captured `orig` over those would corrupt
// live game data.

// =====================================================================
// Asm-hook features: ports of the gecko C2 ("insert asm") codes.
//
// A C2 branches from a game site into a block of asm and back to site+4. Each
// cave below holds that asm, minus four padding nops, and the trailing
// 00000000 placeholder every C2 ends with. The placeholder is patched in
// place to `b -> site+4` on first apply. Enabling writes
// `b site->cave`; disabling restores the captured original.
//
// The caves are mutable *initialized* arrays, so they load pre-populated with
// the blob -- no const source, no copy. Region-specific words inside the asm
// are built with SUSAMUNE_MEM1_ADDR so one array serves all three revisions.
// =====================================================================

enum AsmCaveOffset {
    ASM_CAVE_FAST_PIANT = 0,
    ASM_CAVE_THIRD_CHOMPLET = ASM_CAVE_FAST_PIANT + 4,
    ASM_CAVE_NEVER_PAUSE = ASM_CAVE_THIRD_CHOMPLET + 7,
    ASM_CAVE_DEATHLESS = ASM_CAVE_NEVER_PAUSE + 5,
    ASM_CAVE_REPLACE_EP = ASM_CAVE_DEATHLESS + 16,
    ASM_CAVE_BLUE_COIN = ASM_CAVE_REPLACE_EP + 7,
    ASM_CAVE_FREE_PAUSE = ASM_CAVE_BLUE_COIN + 5,
    ASM_CAVE_FORCE_PLAZA = ASM_CAVE_FREE_PAUSE + 14,
    ASM_CAVE_SHINE_NO_FIRE = ASM_CAVE_FORCE_PLAZA + 10,
    ASM_CAVE_SHINE_TOUCH = ASM_CAVE_SHINE_NO_FIRE + 23,
    ASM_CAVE_SHINE_KEEP = ASM_CAVE_SHINE_TOUCH + 11,
    ASM_CAVE_SHINE_RELEASE = ASM_CAVE_SHINE_KEEP + 4,
    ASM_CAVE_STAGE_INTRO = ASM_CAVE_SHINE_RELEASE + 6,
    ASM_CAVE_STAGE_INTRO_BUTTON = ASM_CAVE_STAGE_INTRO + 20,
    ASM_CAVE_END = ASM_CAVE_STAGE_INTRO_BUTTON + 7,
};

#define FREE_PAUSE_ADDR SUSAMUNE_MEM1_ADDR(0x800EB038u, 0x80297A7Cu, 0x8028F914u)
#define SHINE_TOUCH_LIS (0x3D800000u | (SUSAMUNE_ADDR_SHINE_TOUCH_FRAME >> 16))
#define SHINE_TOUCH_ORI (0x618C0000u | (SUSAMUNE_ADDR_SHINE_TOUCH_FRAME & 0xFFFFu))
#define QFT_STATE_HA ((SUSAMUNE_ADDR_QFT_STATE + 0x8000u) >> 16)
#define QFT_STATE_LO(off) ((SUSAMUNE_ADDR_QFT_STATE + (off)) & 0xFFFFu)
#define ATTEMPT_SERIAL_HA \
    ((SUSAMUNE_ADDR_ATTEMPT_SHINE_SERIAL + 0x8000u) >> 16)
#define ATTEMPT_SERIAL_LO \
    (SUSAMUNE_ADDR_ATTEMPT_SHINE_SERIAL & 0xFFFFu)
#define LAST_SHINE_ID_HA \
    ((SUSAMUNE_ADDR_LAST_SHINE_ID + 0x8000u) >> 16)
#define LAST_SHINE_ID_LO \
    (SUSAMUNE_ADDR_LAST_SHINE_ID & 0xFFFFu)
#define SIS_CONSOLE_OFF SUSAMUNE_MEM1_ADDR(0x02BCu, 0x02B8u, 0x08DCu)

// The caves tile this pool in hook order. Each final zero becomes b->site+4.
u32 gAsmCaves[] = {
    // Fastest Piantissimo pattern.
    0x8BFA007Cu, 0x23FF000Cu, 0x57FFFFBEu, 0x00000000u,

    // TFireWanwan::isFindMario: instance 1 is the far-right Chomplet.
    0xA183007Cu, 0x2C0C0001u, 0x4082000Cu, 0x38600000u,
    0x4E800020u, 0x7C0802A6u, 0x00000000u,

    // Never Pause IGT.
    0x80030030u, 0x2C000000u, 0x4C820020u, 0x7C0802A6u, 0x00000000u,

    // Deathless Blooper Surfing.
    SUSAMUNE_MEM1_ADDR(0x818D9A10u, 0x818D9D48u, 0x818D9C70u), 0x812C0018u,
    0x814C0014u, 0x554A103Au, 0x7D495214u, 0x858AFFFCu,
    0x800C0000u, 0x6C00803Cu,
    SUSAMUNE_MEM1_ADDR(0x280060C0u, 0x2800E6C8u, 0x28005EB8u), 0x40A20010u,
    0xA00C00F2u, 0x7000FFF6u, 0xB00C00F2u, 0x7C0A4840u,
    0x4181FFDCu, 0x00000000u,

    // Replace Episode Names with their ID.
    SUSAMUNE_MEM1_ADDR(0x80AD97D0u, 0x80AD9FA0u, 0x80AD9EC8u), 0x88A500DFu,
    0x38A50031u, 0x54A5403Eu, 0xB0A60000u, 0x38800080u, 0x00000000u,

    // Disable Blue Coin Flag.
    0x7CA00039u, SUSAMUNE_MEM1_ADDR(0x80AD97D0u, 0x80AD9FA0u, 0x80AD9EC8u),
    0x38800000u, 0x908500D4u, 0x00000000u,

    // Free Pause.
    0x887F007Cu, 0x2803000Fu, 0x41820028u, 0x807F0018u,
    0x80630000u, 0x806300D4u, 0x546307FFu, 0x41820014u,
    0x3C600000u | (FREE_PAUSE_ADDR >> 16),
    0x60630000u | (FREE_PAUSE_ADDR & 0xFFFFu),
    0x7C6803A6u, 0x4E800020u, 0x881F0124u, 0x00000000u,

    // Force Plaza Events.
    SUSAMUNE_MEM1_ADDR(0x806D97D0u, 0x806D9FA0u, 0x806D9EC8u), 0x899D0001u,
    0x558BF7BCu, 0x7D8C5B78u, 0x558C16FAu, 0x3D60FFF3u,
    0x616BFF01u, 0x5D6B6636u, 0x99630070u, 0x00000000u,

    // No Shine Get Animation: winDemo+0x88.
    SHINE_TOUCH_LIS, SHINE_TOUCH_ORI, 0x81630058u, 0x916C0000u,
    0x3D800000u | QFT_STATE_HA,
    0x816C0000u | QFT_STATE_LO(SUSAMUNE_QFT_STATE_OFFSET_OFF),
    0x8003005Cu, 0x7D6B0214u, 0x396B0004u, 0x556B003Au,
    0x916C0000u | QFT_STATE_LO(SUSAMUNE_QFT_STATE_OFFSET_OFF),
    0x3800FFFFu,
    0xB00C0000u | QFT_STATE_LO(SUSAMUNE_QFT_STATE_STOP_OFF),
    0x38000001u,
    0x980C0000u | QFT_STATE_LO(SUSAMUNE_QFT_STATE_STOP_REASON_OFF),
    0x81840134u, 0x3D600000u | LAST_SHINE_ID_HA,
    0x918B0000u | LAST_SHINE_ID_LO, 0x3D600000u | ATTEMPT_SERIAL_HA,
    0x818B0000u | ATTEMPT_SERIAL_LO, 0x398C0001u,
    0x918B0000u | ATTEMPT_SERIAL_LO, 0x00000000u,

    // TShine::touchPlayer entry.
    SHINE_TOUCH_LIS, SHINE_TOUCH_ORI, 0x800C0000u,
    SUSAMUNE_MEM1_ADDR(0x816D97E8u, 0x816D9FB8u, 0x816D9EE0u),
    0x816B0058u, 0x7C005850u, 0x28000004u, 0x916C0000u,
    0x4C810020u, 0x7C0802A6u, 0x00000000u,

    // winDemo+0xA4.
    0x80030064u, 0x5400003Cu, 0x90030064u, 0x00000000u,

    // winDemo+0xAC.
    0x3C000C40u, 0x60000201u, 0x901F007Cu,
    0x38000000u, 0x901F0084u, 0x00000000u,

    // Stage Intro Skip: direct+0x158.
    0x899A0064u, 0x2C0C0001u, 0x40A20040u, 0x819A0074u,
    0x818C0094u, 0x816C0000u | SIS_CONSOLE_OFF, 0x2C0B0003u,
    0x41A1002Cu, 0x41A00018u,
    SUSAMUNE_MEM1_ADDR(0x3D80803Eu, 0x3D80803Fu, 0x3D80803Eu),
    SUSAMUNE_MEM1_ADDR(0x818C6034u, 0x818C9734u, 0x818C10F4u),
    0x39600000u, 0x916C0018u, 0x48000014u, 0x3863000Fu,
    0x907A0054u, 0x3B800000u, 0x48000008u, 0xB01A004Cu, 0x00000000u,

    // Stage Intro Skip: changeState+0x1CC.
    0x807F0074u, 0x80630094u, 0x80630000u | SIS_CONSOLE_OFF,
    0x2C830000u, 0x70000061u, 0x4C423102u, 0x00000000u,
};

// Keep the alternate pattern in a separate cave. Re-pointing the hook is more
// reliable on console than rewriting an instruction in a live cave.
u32 gPiantissimoSlowCave[] = {
    0x8BFA007Cu, 0x1FFF0005u, 0x57FFFFBEu, 0x00000000u,
};

static_assert(sizeof(gAsmCaves) / sizeof(gAsmCaves[0]) == ASM_CAVE_END,
              "asm cave pool offsets changed");
static_assert(ASM_CAVE_END == 139, "asm cave pool shape changed");

#undef SIS_CONSOLE_OFF
#undef LAST_SHINE_ID_LO
#undef LAST_SHINE_ID_HA
#undef ATTEMPT_SERIAL_LO
#undef ATTEMPT_SERIAL_HA
#undef QFT_STATE_LO
#undef QFT_STATE_HA
#undef SHINE_TOUCH_ORI
#undef SHINE_TOUCH_LIS
#undef FREE_PAUSE_ADDR

// -- No Shine Get Animation ------------------------------------------------
//
// The grab must play out in full (immobile, jump, landing) and then simply
// stop: control back to Mario, shine neither banked nor left floating over his
// head. Four hooks in TMario::winDemo / TShine::touchPlayer do that; the fifth
// upstream hook (clearing the touch timestamp on stage load) is featuresOnStage
// Load() below. Translated:
//
//   winDemo, phase 0, "landed" branch:
//     +0x88  gpMarDirector->fireGetStar(shine)   -> touch frame = dir->unk58
//     +0xA4  shine->receiveMessage(mario, TAKE)  -> shine->unk64 &= ~1
//     +0xAC  mario->mSubState = 1                -> mState = IDLE, mSubState = 0
//   TShine::touchPlayer entry:
//            (prologue)                          -> return early unless more
//                                                   than 4 frames since the
//                                                   last touch
//
// Dropping the TAKE message is what stops the collected-shine animation from
// following Mario around; clearing bit 0 of the shine's flags puts its
// collision back so it can be grabbed again. That in turn is why touchPlayer
// needs the 4-frame debounce -- without it Mario re-enters the grab on the very
// next frame while still standing in the shine.
//
// The touch timestamp lives at a fixed scratch address (addresses.hxx) rather
// than in a mod global: a cave has no way to find one, so a global would have
// to be patched into every lis/ori that references it.
// Attempt Counter's moveStage+0x3c check. The first word is the displaced
// region-specific instruction that materialises gpApplication in r28. The
// original Gecko code compares the two packed scene ids at this exact point;
// publish only genuine differences for the C++ counter to consume.
#define DEPARTURE_SERIAL_HA \
    ((SUSAMUNE_ADDR_ATTEMPT_DEPARTURE_SERIAL + 0x8000u) >> 16)
#define DEPARTURE_SERIAL_LO \
    (SUSAMUNE_ADDR_ATTEMPT_DEPARTURE_SERIAL & 0xFFFFu)
u32 gCaveAttemptDeparture[] = {
    SUSAMUNE_MEM1_ADDR(0x3B836000u, 0x3B839700u, 0x3B8310C0u),
    0xA19C000Eu,               // lhz r12, current scene(r28)
    0xA17C000Au,               // lhz r11, previous scene(r28)
    0x7C0C5800u,               // cmpw r12, r11
    0x41820014u,               // beq -> return to moveStage
    0x3D600000u | DEPARTURE_SERIAL_HA,
    0x818B0000u | DEPARTURE_SERIAL_LO,
    0x398C0001u,
    0x918B0000u | DEPARTURE_SERIAL_LO,
    0x00000000u,
};
#undef DEPARTURE_SERIAL_HA
#undef DEPARTURE_SERIAL_LO

// -- Stage Intro Skip -------------------------------------------------------
//
// Not a fast-forward: it runs the intro to completion inside a single frame.
// TMarDirector::direct spends its tick budget in a loop and ends the loop by
// setting mGameState |= 0x4000; the first hook sits on exactly that store and,
// while the intro is playing, refills the budget and branches *past* it, so the
// loop keeps running game logic (and never reaches the draw pass) until the
// intro-text state advances. The second hook makes changeState take its
// "player pressed skip" path on its own. Translated:
//
//   direct+0x158, replacing `mGameState |= 0x4000`:
//     s = mConsole->unk94->unk2BC            (intro-text state)
//     if (mCurState == 1 && s < 3) { mTickBudget += 20; i = 0; }  // keep going
//     else if (mCurState == 1 && s == 3) { gpApplication.mFader->unk18 = 0; }
//     else                                 { mGameState |= 0x4000; }  // normal
//
//   changeState+0x1CC, replacing the `andi.` that tests the skip buttons:
//     treat "no button pressed" as false when unk2BC == 0, i.e. auto-skip.
//
struct AsmHook {
    u32  site;   // game address whose instruction becomes b->cave
    u32 *cave;   // asm block; its trailing word is patched to b->site+4
    u8   idState;  // low seven bits: SettingId; high bit: currently installed
    u8   n;      // word count of cave
};
static_assert(sizeof(AsmHook) == 12, "asm hook layout changed");

template <SettingId Id>
constexpr u8 encodedHookId() {
    static_assert((u32)Id < 0x80u, "asm hook setting exceeds seven bits");
    return (u8)Id;
}

#define HOOK(id, jp, us, pal, begin, end)                                   \
    { SUSAMUNE_MEM1_ADDR(jp, us, pal), &gAsmCaves[begin], encodedHookId<id>(), \
      (u8)((end) - (begin)) }

AsmHook kAsmHooks[] = {
    HOOK(SETTING_FAST_PIANTISSIMO, 0x80256A14u, 0x80043064u, 0x80042EDCu,
         ASM_CAVE_FAST_PIANT, ASM_CAVE_THIRD_CHOMPLET),
    HOOK(SETTING_DISABLE_THIRD_CHOMPLET_AGGRO, 0x8029E508u, 0x8008B578u,
         0x80084C18u, ASM_CAVE_THIRD_CHOMPLET, ASM_CAVE_NEVER_PAUSE),
    HOOK(SETTING_NEVER_PAUSE_IGT, 0x8009292Cu, 0x8034800Cu, 0x80340268u,
         ASM_CAVE_NEVER_PAUSE, ASM_CAVE_DEATHLESS),
    HOOK(SETTING_DEATHLESS_BLOOPER, 0x801397D0u, 0x8025A340u, 0x802520CCu,
         ASM_CAVE_DEATHLESS, ASM_CAVE_REPLACE_EP),
    HOOK(SETTING_REPLACE_EPISODE_NAMES, 0x80232C70u, 0x801727B8u, 0x80168758u,
         ASM_CAVE_REPLACE_EP, ASM_CAVE_BLUE_COIN),
    HOOK(SETTING_DISABLE_BLUE_COIN, 0x800FA12Cu, 0x802A6728u, 0x8029E680u,
         ASM_CAVE_BLUE_COIN, ASM_CAVE_FREE_PAUSE),
    HOOK(SETTING_FREE_PAUSE, 0x800EAF90u, 0x802979D4u, 0x8028F86Cu,
         ASM_CAVE_FREE_PAUSE, ASM_CAVE_FORCE_PLAZA),
    HOOK(SETTING_FORCE_PLAZA_EVENTS, 0x8010C41Cu, 0x802B7764u, 0x802AF734u,
         ASM_CAVE_FORCE_PLAZA, ASM_CAVE_SHINE_NO_FIRE),
    HOOK(SETTING_NO_SHINE_ANIM, 0x80120540u, 0x80241400u, 0x8023918Cu,
         ASM_CAVE_SHINE_NO_FIRE, ASM_CAVE_SHINE_TOUCH),
    HOOK(SETTING_NO_SHINE_ANIM, 0x80195304u, 0x801BD334u, 0x801B51ECu,
         ASM_CAVE_SHINE_TOUCH, ASM_CAVE_SHINE_KEEP),
    HOOK(SETTING_NO_SHINE_ANIM, 0x8012055Cu, 0x8024141Cu, 0x802391A8u,
         ASM_CAVE_SHINE_KEEP, ASM_CAVE_SHINE_RELEASE),
    HOOK(SETTING_NO_SHINE_ANIM, 0x80120564u, 0x80241424u, 0x802391B0u,
         ASM_CAVE_SHINE_RELEASE, ASM_CAVE_STAGE_INTRO),
    HOOK(SETTING_STAGE_INTRO_SKIP, 0x800ECF14u, 0x80299990u, 0x80291828u,
         ASM_CAVE_STAGE_INTRO, ASM_CAVE_STAGE_INTRO_BUTTON),
    HOOK(SETTING_STAGE_INTRO_SKIP, 0x800EC5D0u, 0x8029904Cu, 0x80290EE4u,
         ASM_CAVE_STAGE_INTRO_BUTTON, ASM_CAVE_END),
};

#undef HOOK

const int kNumHooks = (int)(sizeof(kAsmHooks) / sizeof(kAsmHooks[0]));
static_assert(kNumHooks == 14, "asm hook count changed");

#define gHookOrig (*reinterpret_cast<u32 (*)[kNumHooks]>( \
    SUSAMUNE_MEM2_FEATURE_RUNTIME_PPC_BASE + \
    sizeof(u32) * kNumPatches))
bool gHooksInited;
u8 gPiantissimoMode;

bool featureEnabled(SettingId id) {
    return gSettings.getBool(id) &&
           (id != SETTING_FAST_TEXT || !StageLoader::fastTextSuppressed());
}

constexpr u8 kHookIdMask = 0x7Fu;
constexpr u8 kHookOn     = 0x80u;

#if defined(SUSAMUNE_VERSION_PAL)
// Fill in Fast Text's message-buffer row from the live language word. Only
// called while the setting is on, because the word is in the heap: before the
// message data is loaded it reads as whatever was there, and 0 (English) is
// indistinguishable from cleared heap.
//
// The language word is a heap address like the buffers themselves, so it
// should move with the arena reservation -- but the launcher's .gct fixup
// (PatchSusamuneGeckoCodes) relocates only the buffer writes and works on
// console, so try the unreserved address as well rather than pick one blind.
void resolveFastTextPalMsg() {
    constexpr int kFastTextMsgPatch = kNumFeaturePatches - 2;
    static_assert(kFastTextMsgPatch == 38,
                  "PAL Fast Text message row moved");
    Patch &row = gFeaturePatches[kFastTextMsgPatch];
    if (row.addrState & kPatchAddrMask) {
        return;
    }

    u32 lang = *reinterpret_cast<volatile u32 *>(FAST_TEXT_PAL_LANG_ADDR +
                                                 SUSAMUNE_ARENA_RESERVE_SIZE);
    if (lang > 4) {
        lang = *reinterpret_cast<volatile u32 *>(FAST_TEXT_PAL_LANG_ADDR);
    }
    if (lang > 4) {
        return;  // not a language yet; try again next frame
    }

    row.addrState =
        (row.addrState & ~kPatchAddrMask) |
        ((kFastTextPalMsg[lang].addr + SUSAMUNE_ARENA_RESERVE_SIZE) &
         kPatchAddrMask);
    row.value = kFastTextPalMsg[lang].val;
}
#endif

void applyPatches(bool early) {
#if defined(SUSAMUNE_VERSION_PAL)
    if (!early && featureEnabled(SETTING_FAST_TEXT)) {
        resolveFastTextPalMsg();
    }
#endif
    int  idx          = 0;
    bool featureEarly = false;
    bool on           = false;
    for (int pidx = 0; pidx < kNumFeaturePatches; pidx++) {
        Patch &p   = gFeaturePatches[pidx];
        u32    tag = (p.addrState & kPatchFeatureMask) >> 24;
        if (tag != 0) {
            featureEarly = (p.addrState & kPatchEarly) != 0;
            on = featureEnabled((SettingId)(tag - 1));
        }
        if (featureEarly != early) {
            continue;
        }

        u32 addr = patchAddr(p);
        u32 word;
        if (early) {
            // Write-once: an off row has nothing to put back.
            if (!on || addr == 0) {
                continue;
            }
            word = *reinterpret_cast<volatile u32 *>(addr);
        } else {
            // The slot is claimed even by an unresolved row (PAL Fast Text's
            // message buffer), which resolves on a later pass.
            int i = idx++;
            if (addr == 0) {
                continue;
            }

            u32 state = p.addrState;
            if (!(state & kPatchCaptured)) {
                gPatchOrig[i] = *reinterpret_cast<volatile u32 *>(addr);
                state |= kPatchCaptured;
                p.addrState = state;
            }
            if (on == ((state & kPatchOn) != 0)) {
                continue;
            }
            p.addrState = on ? state | kPatchOn : state & ~kPatchOn;
            word        = gPatchOrig[i];
            if (!on) {
                writeGameCode(addr, word);  // restore the retail word
                continue;
            }
        }
        writeGameCode(addr, (word & ~patchMask(p)) | p.value);
    }
}

enum SavestateFeatureState {
    SAVESTATE_FRUIT_TIMEOUT = 1 << 0,
    SAVESTATE_FAST_TEXT     = 1 << 1,
};

u8 savestateFeatureBit(SettingId id) {
    if (id == SETTING_FRUIT_NEVER_TIMEOUT) return SAVESTATE_FRUIT_TIMEOUT;
    if (id == SETTING_FAST_TEXT) return SAVESTATE_FAST_TEXT;
    return 0;
}

// Only these rows live in ranges the savestate restores. Fast Text's first
// three rows are instructions; its remaining high-MEM1 rows are message data.
bool savestateRewindsPatch(SettingId id, u32 addr) {
    return id == SETTING_FRUIT_NEVER_TIMEOUT ||
           (id == SETTING_FAST_TEXT && addr >= 0x80500000u);
}

u8 captureSavestateFeatureState() {
    u8 state = 0;
    SettingId id = SETTING_COUNT;
    for (int pidx = 0; pidx < kNumFeaturePatches; pidx++) {
        const Patch &p = gFeaturePatches[pidx];
        const u32 tag = (p.addrState & kPatchFeatureMask) >> 24;
        if (tag != 0) id = (SettingId)(tag - 1);
        const u32 addr = patchAddr(p);
        if (addr != 0 && savestateRewindsPatch(id, addr) &&
            (p.addrState & kPatchOn)) {
            state |= savestateFeatureBit(id);
        }
    }
    return state;
}

void restoreSavestateFeatureState(u8 savedState) {
#if defined(SUSAMUNE_VERSION_PAL)
    if (featureEnabled(SETTING_FAST_TEXT)) resolveFastTextPalMsg();
#endif
    int idx = 0;
    SettingId id = SETTING_COUNT;
    bool early = false;
    for (int pidx = 0; pidx < kNumFeaturePatches; pidx++) {
        Patch &p = gFeaturePatches[pidx];
        const u32 tag = (p.addrState & kPatchFeatureMask) >> 24;
        if (tag != 0) {
            id = (SettingId)(tag - 1);
            early = (p.addrState & kPatchEarly) != 0;
        }
        if (early) continue;

        const int originalIndex = idx++;
        const u32 addr = patchAddr(p);
        if (addr == 0 || !savestateRewindsPatch(id, addr)) continue;

        const u8 bit = savestateFeatureBit(id);
        const bool savedOn = (savedState & bit) != 0;
        const bool wantOn = featureEnabled(id);
        u32 state = p.addrState;
        if (savedOn == wantOn) {
            p.addrState = wantOn ? state | kPatchOn : state & ~kPatchOn;
            continue;
        }

        if (wantOn) {
            // The restored bytes are the exact off-state from the snapshot.
            // Keep them as the new undo value instead of an older heap word.
            const u32 word = *reinterpret_cast<volatile u32 *>(addr);
            gPatchOrig[originalIndex] = word;
            p.addrState = state | kPatchCaptured | kPatchOn;
            writeGameCode(addr, (word & ~patchMask(p)) | p.value);
        } else {
            // A row cannot have been on at save time without first capturing
            // its undo word. Refuse to guess if the runtime state is damaged.
            if (!(state & kPatchCaptured)) continue;
            writeGameCode(addr, gPatchOrig[originalIndex]);
            p.addrState = state & ~kPatchOn;
        }
    }
}

void applyHooks() {
    bool init = !gHooksInited;
    for (int k = 0; k < kNumHooks; k++) {
        AsmHook &h = kAsmHooks[k];
        const SettingId id = (SettingId)(h.idState & kHookIdMask);

        u8 piantissimoMode = 0;
        bool piantissimoChanged = false;
        u32 *hookCave = h.cave;
        if (id == SETTING_FAST_PIANTISSIMO) {
            piantissimoMode = gSettings.get(id);
            if (piantissimoMode > 2) piantissimoMode = 0;
            piantissimoChanged = piantissimoMode != gPiantissimoMode;
            hookCave = piantissimoMode == 1 ? gPiantissimoSlowCave : h.cave;
        }

        if (init) {
            // The cave already holds the asm (loaded with the blob); patch its
            // trailing placeholder to `b -> site+4` in place, then flush the
            // whole cave so the core can execute it.
            u32 backAddr    = reinterpret_cast<u32>(&h.cave[h.n - 1]);
            h.cave[h.n - 1] = branchWord(backAddr, h.site + 4);

            DCFlushRange(h.cave, h.n * 4);
            ICInvalidateRange(h.cave, h.n * 4);

            gHookOrig[k] = *reinterpret_cast<volatile u32 *>(h.site);

            if (id == SETTING_FAST_PIANTISSIMO) {
                const int slowWords = sizeof(gPiantissimoSlowCave) /
                                      sizeof(gPiantissimoSlowCave[0]);
                u32 backAddr = reinterpret_cast<u32>(
                    &gPiantissimoSlowCave[slowWords - 1]);
                gPiantissimoSlowCave[slowWords - 1] =
                    branchWord(backAddr, h.site + 4);
                DCFlushRange(gPiantissimoSlowCave,
                             sizeof(gPiantissimoSlowCave));
                ICInvalidateRange(gPiantissimoSlowCave,
                                  sizeof(gPiantissimoSlowCave));
            }
        }

        u8 state = h.idState;
        bool on = id == SETTING_FAST_PIANTISSIMO
                      ? piantissimoMode != 0
                      : gSettings.getBool(id);
        // Watch owns its destination and cannot wait on secret-stage input.
        if (id == SETTING_STAGE_INTRO_SKIP && Ghost::observerActive()) {
            on = true;
        }
        if (on == ((state & kHookOn) != 0) && !piantissimoChanged) {
            continue;
        }
        writeGameCode(h.site,
                      on ? branchWord(h.site, reinterpret_cast<u32>(hookCave))
                         : gHookOrig[k]);
        h.idState = on ? state | kHookOn : state & ~kHookOn;
        if (id == SETTING_FAST_PIANTISSIMO) {
            gPiantissimoMode = piantissimoMode;
        }
    }
    if (init) {
        gHooksInited = true;
    }
}

// =====================================================================
// Choice features: multi-state options carved out of Gecko codes. Choice 0
// restores the captured original; higher choices write explicit instructions.
// =====================================================================

// Nozzle Lock. One site: the game's "current nozzle" load becomes li r31,<id>.
//   jp 041494D4 / us 04269F50 / pal 04261CDC
//   Rocket=3BE00001, Turbo=3BE00005, Hover=3BE00004 (li r31, id); Unlocked=orig
// FLUDD in secrets (from DPad Functions, mode bits 0x401/0x402/0x404). Two
// sites in TRedCoinSwitch::load. Value words are region-identical; only the
// addresses differ. No FLUDD nops the load; All secrets branches past the
// gate; Completed restores the captured original beq.
constexpr u32 kChoiceSites[] = {
    SUSAMUNE_MEM1_ADDR(0x801494D4u, 0x80269F50u, 0x80261CDCu),
    SUSAMUNE_MEM1_ADDR(0x80198784u, 0x801C0910u, 0x801B87C8u),
    SUSAMUNE_MEM1_ADDR(0x800EC0F4u, 0x80298B88u, 0x80290A20u),

    // Helmet. QbeRoot's generator says 801211AC for JP's third site, but
    // that is its preceding rlwinm.; the retail branch is at 801211B0.
    SUSAMUNE_MEM1_ADDR(0x80120FB8u, 0x80241E78u, 0x80239C04u),
    SUSAMUNE_MEM1_ADDR(0x8012112Cu, 0x80241FECu, 0x80239D78u),
    SUSAMUNE_MEM1_ADDR(0x801211B0u, 0x80242070u, 0x80239DFCu),
    // Cap.
    SUSAMUNE_MEM1_ADDR(0x80120D34u, 0x80241BF4u, 0x80239980u),
    // Shades.
    SUSAMUNE_MEM1_ADDR(0x80121054u, 0x80241F14u, 0x80239CA0u),
    SUSAMUNE_MEM1_ADDR(0x80121160u, 0x80242020u, 0x80239DACu),
    SUSAMUNE_MEM1_ADDR(0x801211E4u, 0x802420A4u, 0x80239E30u),
    // Shine shirt.
    SUSAMUNE_MEM1_ADDR(0x8012C9B0u, 0x8024D4DCu, 0x80245268u),
};
constexpr u8 kNozzleIds[] = {1, 5, 4};
constexpr u8 kAppearanceFirst[] = {3, 6, 7, 10, 11};
constexpr int kCoreChoicePatches = kAppearanceFirst[0];

constexpr int kNumChoicePatches =
    (int)(sizeof(kChoiceSites) / sizeof(kChoiceSites[0]));
constexpr int kNumAppearances =
    (int)(sizeof(kAppearanceFirst) / sizeof(kAppearanceFirst[0])) - 1;
static_assert(kNumChoicePatches == 11, "choice patch sites changed");
static_assert(kNumAppearances == 4 &&
                  kAppearanceFirst[kNumAppearances] == kNumChoicePatches,
              "appearance ranges must tile the appended choice sites");
static_assert(sizeof(kNozzleIds) / sizeof(kNozzleIds[0]) == 3,
              "nozzle choices changed");
static_assert(SETTING_CAP_APPEARANCE == SETTING_HELMET_APPEARANCE + 1 &&
                  SETTING_SHADES_APPEARANCE ==
                      SETTING_HELMET_APPEARANCE + 2 &&
                  SETTING_SHINE_SHIRT_APPEARANCE ==
                      SETTING_HELMET_APPEARANCE + 3,
              "appearance setting ids must stay contiguous");

#define gChoiceOrig (*reinterpret_cast<u32 (*)[kNumChoicePatches]>( \
    SUSAMUNE_MEM2_FEATURE_RUNTIME_PPC_BASE + \
    sizeof(u32) * (kNumPatches + kNumHooks)))
static_assert(sizeof(u32) *
                  (kNumPatches + kNumHooks + kNumChoicePatches) <=
                  SUSAMUNE_FEATURE_RUNTIME_SIZE,
              "feature originals exceed their MEM2 runtime window");
u8  gChoiceState;
u8  gAppearanceState;

constexpr u8 kChoiceNozzleMask = 0x03u;
constexpr u8 kChoiceFluddMask  = 0x0Cu;
constexpr u8 kChoiceFluddShift = 2u;
constexpr u8 kChoiceCaptured   = 0x10u;
constexpr u8 kAppearanceMask   = 0x03u;
constexpr u8 kAppearanceAlways = 1u;
constexpr u8 kAppearanceNever  = 2u;

void applyChoices() {
    if (!(gChoiceState & kChoiceCaptured)) {
        for (int i = 0; i < kNumChoicePatches; i++) {
            gChoiceOrig[i] = *reinterpret_cast<volatile u32 *>(kChoiceSites[i]);
        }
        gChoiceState |= kChoiceCaptured;
    }

    u8 nozzle = gSettings.get(SETTING_NOZZLE_LOCK);
    if (nozzle != (gChoiceState & kChoiceNozzleMask)) {
        u32 word = gChoiceOrig[0];
        if (nozzle != 0) {
            word = 0x3BE00000u | kNozzleIds[nozzle - 1];
        }
        writeGameCode(kChoiceSites[0], word);
        gChoiceState = (gChoiceState & ~kChoiceNozzleMask) | nozzle;
    }

    u8 fludd = gSettings.get(SETTING_FLUDD_SECRETS);
    u8 currentFludd = (gChoiceState & kChoiceFluddMask) >> kChoiceFluddShift;
    if (fludd != currentFludd) {
        for (int i = 1; i < kCoreChoicePatches; i++) {
            u32 word = gChoiceOrig[i];
            if (fludd == 1) {
                word = 0x60000000u;
            } else if (fludd == 2) {
                word = 0x4800001Cu - (u32)i * 4u;
            }
            writeGameCode(kChoiceSites[i], word);
        }
        gChoiceState = (gChoiceState & ~kChoiceFluddMask) |
                       (fludd << kChoiceFluddShift);
    }

    for (int appearance = 0; appearance < kNumAppearances; appearance++) {
        const u8 shift = (u8)(appearance * 2);
        u8 mode = gSettings.get((SettingId)(SETTING_HELMET_APPEARANCE +
                                             appearance));
        if (mode > kAppearanceNever) {
            mode = 0;
        }
        if (appearance == kNumAppearances - 1 && mode == 0 &&
            gSettings.getBool(SETTING_SHINE_OUTFIT)) {
            mode = kAppearanceAlways;
        }

        const u8 current = (gAppearanceState >> shift) & kAppearanceMask;
        if (mode == current) {
            continue;
        }

        for (int i = kAppearanceFirst[appearance];
             i < kAppearanceFirst[appearance + 1]; i++) {
            u32 word = gChoiceOrig[i];
            if (mode == kAppearanceAlways) {
                word = 0x60000000u;
            } else if (mode == kAppearanceNever) {
                word = 0x48000000u | (word & 0x0000FFFFu);
            }
            writeGameCode(kChoiceSites[i], word);
        }
        gAppearanceState =
            (gAppearanceState & ~(kAppearanceMask << shift)) | (mode << shift);
    }
}

}  // namespace

void writeGameCode(u32 addr, u32 word) {
    *reinterpret_cast<volatile u32 *>(addr) = word;
    DCFlushRange(reinterpret_cast<void *>(addr), 4);
    ICInvalidateRange(reinterpret_cast<void *>(addr), 4);
}

u32 branchWord(u32 from, u32 to) {
    return 0x48000000u | ((to - from) & 0x03FFFFFCu);
}

void featuresApplyEarly() {
    const u32 departureSite =
        SUSAMUNE_MEM1_ADDR(0x800EAB7Cu, 0x802975C0u, 0x8028F458u);
    const int departureWords =
        (int)(sizeof(gCaveAttemptDeparture) / sizeof(gCaveAttemptDeparture[0]));
    const u32 departureBack = reinterpret_cast<u32>(
        &gCaveAttemptDeparture[departureWords - 1]);
    gCaveAttemptDeparture[departureWords - 1] =
        branchWord(departureBack, departureSite + 4);
    DCFlushRange(gCaveAttemptDeparture, departureWords * sizeof(u32));
    ICInvalidateRange(gCaveAttemptDeparture, departureWords * sizeof(u32));
    writeGameCode(departureSite,
                  branchWord(departureSite,
                             reinterpret_cast<u32>(&gCaveAttemptDeparture[0])));

    // Manta Fix v1.2's actual fix: preserve the split Manta's horizontal
    // velocity. Its second Gecko hook only recoloured a HUD stripe so runners
    // could verify the code was active; the native build does not need that.
    writeGameCode(
        SUSAMUNE_MEM1_ADDR(0x800E9050u, 0x80295A94u, 0x8028D92Cu),
        SUSAMUNE_MEM1_ADDR(0xC342FFE4u, 0xC342FFF8u, 0xC342FFF8u));
    applyPatches(true);
}

void featuresApply() {
    applyPatches(false);
    applyHooks();
    applyChoices();
}

void featuresOnStageLoad() {
    // Upstream clears its shine-touch timestamp from a hook in setupObjects;
    // ours lives here. Without it a stale frame number carries into the next
    // stage and can swallow the first shine touch.
    *reinterpret_cast<volatile u32 *>(SUSAMUNE_ADDR_SHINE_TOUCH_FRAME) = 0;
}

u8 featuresSavestateState() { return captureSavestateFeatureState(); }

void featuresOnSavestateLoaded(u8 savedState) {
    restoreSavestateFeatureState(savedState);
}
