// =====================================================================
// qftimer.cpp
//
// Quarterframe Timer + Quarterframe Section Timer, transcribed from the
// upstream practice codes (`qft` 1.5 / `qfst` 0.1 in gct-generator's
// Codes.xml). IL runs are timed with these, so this is a translation, not a
// reimplementation: the gecko blocks are quoted next to the C they became, the
// state words are upstream's 0x817F00B2..BF one for one, and the caves are its
// asm with the scratch address swapped for ours.
//
// The timer counts TMarDirector::unk5C, the director's logic-tick counter,
// which advances 120 times a second.
//
// Two of upstream's hooks are not injections here, because the mod already
// runs at those points:
//
//   direct+0x88 (the `unk260 = 1` store) -> qfTimerOnStageLoad(), called from
//     onSetup, which IS the `bl setupObjects` two instructions earlier
//   TApplication::proc+0x34 (section reset) -> the same call. Upstream resets
//     at the top of the app-state loop, which also fires when the file-select
//     director is built; nothing draws the timer between there and the next
//     stage load, so the two are indistinguishable.
//
// Everything else is a real injection at upstream's own address, including the
// draw hook -- see gDrawCave. Eight of the freeze triggers are trailing `blr`s
// and share one cave, which is upstream's trick and is what makes a per-trigger
// toggle one word.
//
// Displaced originals are copied from the site at install time, so no cave
// depends on a revision's encoding.
// =====================================================================

#include "susamune/qftimer.hxx"

#include "susamune/addresses.hxx"
#include "susamune/features.hxx"  // writeGameCode, branchWord
#include "susamune/gui_config.hxx"
#include "susamune/settings.hxx"

#include "Dolphin/OS.h"
#include "SMS/System/Application.hxx"  // gpSystemFont
#include "SMS/System/MarDirector.hxx"

extern "C" void susamuneQfOnPlayerStatus(u32 status);
extern "C" void susamuneQfTick();
// J2DGrafContext::setup2D. The renderer re-enters 2D vertex state with it; see
// "The GX state trap" in doc/qft_correspondence.md.
extern "C" void qfSetup2D(void *) __asm__("setup2D__14J2DGrafContextFv");

// Pinned at the link base by link_mod.py's --section-start, so a published
// Gecko code keeps working across rebuilds. Defaults are upstream's.
// The background rects are resolved, so the defaults carry the JP font's
// measurements -- the widest of the three, by two pixels on the section timer.
__attribute__((section(".guicfg"), used)) SusamuneGuiBlock gGuiBlock = {
    { 0 },
    {
        // qft: bottom-left, white on 50% black
        { 16, 456, 16, 434, 130, 456, 20, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000080u },
        30, 0,
        // qfst: right-hand column, sixteen lines tall, 25% black
        { 533, 150, 529, 133, 596, 347, 13, 0, 0xFFFFFFFFu, 0xFFFFFFFFu, 0x00000040u },
    },
};

namespace {

// ---------------------------------------------------------------- caves ----
//
// One `lis rX, block@ha` plus a signed displacement reaches any field; every
// offset is under 0x80 and all three bases stay in range.

const u32 kBlock = SUSAMUNE_ADDR_GUI_BLOCK;
const u32 kHa16  = ((kBlock + 0x8000u) >> 16) & 0xFFFFu;

#define D(off) ((u32)(((s32)(s16)(u16)(kBlock & 0xFFFFu)) + (s32)(off)) & 0xFFFFu)
#define ST(m)  ((int)__builtin_offsetof(SusamuneQfState, m))
#define CF(m)  (SUSAMUNE_GUI_OFF_CONFIG + (int)__builtin_offsetof(SusamuneGuiConfig, m))

#define LIS(rd)          (0x3C000000u | ((u32)(rd) << 21) | kHa16)
#define LWZ(rd, ra, d)   (0x80000000u | ((u32)(rd) << 21) | ((u32)(ra) << 16) | ((u32)(d) & 0xFFFFu))
#define LHZ(rd, ra, d)   (0xA0000000u | ((u32)(rd) << 21) | ((u32)(ra) << 16) | ((u32)(d) & 0xFFFFu))
#define STW(rs, ra, d)   (0x90000000u | ((u32)(rs) << 21) | ((u32)(ra) << 16) | ((u32)(d) & 0xFFFFu))
#define STH(rs, ra, d)   (0xB0000000u | ((u32)(rs) << 21) | ((u32)(ra) << 16) | ((u32)(d) & 0xFFFFu))
#define STB(rs, ra, d)   (0x98000000u | ((u32)(rs) << 21) | ((u32)(ra) << 16) | ((u32)(d) & 0xFFFFu))
#define ADDI(rd, ra, i)  (0x38000000u | ((u32)(rd) << 21) | ((u32)(ra) << 16) | ((u32)(i) & 0xFFFFu))
// addic, not addi: `addi rD,r0,n` is `li rD,n` -- rA == 0 reads as literal zero
// in D-form. Upstream uses addic at the one site whose accumulator is r0.
#define ADDIC(rd, ra, i) (0x30000000u | ((u32)(rd) << 21) | ((u32)(ra) << 16) | ((u32)(i) & 0xFFFFu))
#define LI(rd, i)        ADDI(rd, 0, i)
#define MR(rd, rs)       (0x7C000378u | ((u32)(rs) << 21) | ((u32)(rd) << 16) | ((u32)(rs) << 11))
#define ADD(rd, ra, rb)  (0x7C000214u | ((u32)(rd) << 21) | ((u32)(ra) << 16) | ((u32)(rb) << 11))
#define TRUNC4(ra, rs)   (0x54000000u | ((u32)(rs) << 21) | ((u32)(ra) << 16) | (29u << 1))
#define CMPWI0(ra)       (0x2C000000u | ((u32)(ra) << 16))
#define BNE(off)         (0x40820000u | ((u32)(off) & 0xFFFCu))
#define BLR              0x4E800020u
#define PATCHED          0x60000000u  // filled in by installCaves()

// gpMarDirector's _SDA_BASE_ offset, baked per region so the cave stays one
// instruction (upstream's r13off).
const int kDirSda = SUSAMUNE_MEM1_ADDR(-0x6818, -0x6048, -0x6120);

// Every cave in one array, so install flushes them with one call and the site
// table indexes them with a byte.
enum {
    CV_FREEZE = 0,
    CV_BLUE   = CV_FREEZE + 7,
    CV_TAKE   = CV_BLUE + 9,
    CV_DROP   = CV_TAKE + 8,
    CV_STATUS = CV_DROP + 8,
    CV_STAR   = CV_STATUS + 8,
    CV_BATH   = CV_STAR + 11,
    CV_CHGST  = CV_BATH + 11,
    CV_NEXT   = CV_CHGST + 11,
    CV_DRAW   = CV_NEXT + 11,
    CV_DTOR   = CV_DRAW + 2,
    CV_NSTAT  = CV_DTOR + 10,
    CV_CARD   = CV_NSTAT + 4,
    CV_END    = CV_CARD + 4,
};

u32 gCaves[] = {
    // -- CV_FREEZE: upstream's shared freezer, entered by `b` over a trailing
    //    `blr` (r11/r12 are dead in an epilogue) and returning for it.
    //      lwz r11,gpMarDirector(r13) / lis r12,0x817F / lwz r11,0x5C(r11)
    //      stw r11,0xB8(r12) / li r11,frame / stw r11,0xBC(r12) / blr
    LWZ(11, 13, kDirSda),
    LIS(12),
    LWZ(11, 11, 0x5C),
    STW(11, 12, D(ST(freezeQf))),
    LHZ(11, 12, D(CF(qftFreezeFrames))),
    STW(11, 12, D(ST(freezeCtr))),
    BLR,

    // -- CV_BLUE: TCoinBlue::taken+0x24. At the HEAD of the function, so
    //    upstream rounds the quarterframe up to the next frame boundary. The
    //    original `mr r3,r0` runs first: r0 is gpMarDirector and r3 is what the
    //    rest reads.
    PATCHED,  // +0 original
    LIS(12),
    LWZ(11, 3, 0x5C),
    ADDI(11, 11, 3),
    TRUNC4(11, 11),
    STW(11, 12, D(ST(freezeQf))),
    LHZ(11, 12, D(CF(qftFreezeFrames))),
    STW(11, 12, D(ST(freezeCtr))),
    PATCHED,  // +8 b -> site+4

    // -- CV_TAKE / CV_DROP: TMario::taking+0x98 and dropObject+0x38. Not
    //    epilogues, so each carries its own copy of the freeze -- six words is
    //    cheaper than the machinery a shared call would need. Both sit right
    //    after a call, so r11/r12 are dead.
    LWZ(11, 13, kDirSda),
    LIS(12),
    LWZ(11, 11, 0x5C),
    STW(11, 12, D(ST(freezeQf))),
    LHZ(11, 12, D(CF(qftFreezeFrames))),
    STW(11, 12, D(ST(freezeCtr))),
    PATCHED,  // +6 original
    PATCHED,  // +7 b -> site+4

    LWZ(11, 13, kDirSda),
    LIS(12),
    LWZ(11, 11, 0x5C),
    STW(11, 12, D(ST(freezeQf))),
    LHZ(11, 12, D(CF(qftFreezeFrames))),
    STW(11, 12, D(ST(freezeCtr))),
    PATCHED,  // +6 original
    PATCHED,  // +7 b -> site+4

    // -- CV_STATUS: changePlayerStatus+0x194, the one site behind the seven
    //    status triggers. Upstream open-codes a comparison chain per enabled
    //    trigger; one call into C is smaller and changes at runtime. r29 is the
    //    new status; r4 is live across the site, so it is spilled into the
    //    state block (the site is not re-entered). LR is already dead.
    LIS(12),
    STW(4, 12, D(ST(caveScratch))),
    MR(3, 29),
    PATCHED,  // +3 bl susamuneQfOnPlayerStatus
    LIS(12),
    LWZ(4, 12, D(ST(caveScratch))),
    PATCHED,  // +6 original
    PATCHED,  // +7 b -> site+4

    // -- CV_STAR: fireGetStar+0x48, the shine grab, where the timer stops for
    //    good. r3 is `this`; r5/r6/r0 are dead (the site is `li r6,-1`).
    //      lwz r6,0xB4(r5) / lwz r0,0x5C(r3) / add / addi 4 / rlwinm
    //      stw r6,0xB4(r5) / li r6,-1 / sth r6,0xB2(r5)   <- both flag bytes
    LIS(5),
    LWZ(6, 5, D(ST(base))),
    LWZ(0, 3, 0x5C),
    ADD(6, 6, 0),
    ADDI(6, 6, 4),
    TRUNC4(6, 6),
    STW(6, 5, D(ST(base))),
    LI(6, -1),
    STH(6, 5, D(ST(stopped))),
    PATCHED,  // +9 original
    PATCHED,  // +10 b -> site+4

    // -- CV_BATH: TBathtub::startDemo+0x21C, beating Bowser. Same, but r3 is
    //    already gpMarDirector and r5 is live, so the pointer goes in r8.
    LIS(8),
    LWZ(6, 8, D(ST(base))),
    LWZ(0, 3, 0x5C),
    ADD(6, 6, 0),
    ADDI(6, 6, 4),
    TRUNC4(6, 6),
    STW(6, 8, D(ST(base))),
    LI(6, -1),
    STH(6, 8, D(ST(stopped))),
    PATCHED,  // +9 original
    PATCHED,  // +10 b -> site+4

    // -- CV_CHGST: changeState+0x328, the quit-to-file-select path. Arms a
    //    restart and freezes the display indefinitely (-1 stays non-zero
    //    however far it is counted down). r31 is `this`; r3/r5 are dead.
    LIS(5),
    LI(3, 1),
    STB(3, 5, D(ST(resetPending))),
    LWZ(3, 31, 0x5C),
    ADDI(3, 3, 3),
    TRUNC4(3, 3),
    STW(3, 5, D(ST(freezeQf))),
    LI(3, -1),
    STW(3, 5, D(ST(freezeCtr))),
    PATCHED,  // +9 original
    PATCHED,  // +10 b -> site+4

    // -- CV_NEXT: setNextStage+0x50, an area change inside the same run -- a
    //    pipe, a door, a secret course. Freezes for the load but explicitly
    //    does NOT arm a restart, which is what carries the clock across. r30 is
    //    `this`. The original runs first: it reads r0, which the rest clobbers.
    PATCHED,  // +0 original
    LIS(5),
    LI(0, 0),
    STB(0, 5, D(ST(resetPending))),
    LWZ(0, 30, 0x5C),
    ADDIC(0, 0, 4),
    TRUNC4(0, 0),
    STW(0, 5, D(ST(freezeQf))),
    LI(0, -1),
    STW(0, 5, D(ST(freezeCtr))),
    PATCHED,  // +10 b -> site+4

    // -- CV_DRAW: the tail of TGCConsole2::perform, upstream's draw hook.
    //    The console is the in-game HUD, so reaching this instruction is
    //    exactly the condition under which upstream's timers are visible --
    //    never on the title screen, the file select, or the black frames of a
    //    stage transition -- and it is the point in the frame at which
    //    upstream samples the quarterframe.
    //
    //    This hook runs the state machine only; the pixels go out from
    //    qfTimerDraw, off afterDraw. Upstream draws here, but issuing GX from
    //    inside the console's teardown is not something the mod can afford to
    //    get subtly wrong, and afterDraw is a context it already renders in
    //    every frame. Same frame, same numbers, same 2D space.
    //
    //    The site is `addi r0,r3,-0x64B8` inside an elided destructor: the
    //    store it feeds writes a vtable pointer into a stack object two
    //    instructions before the frame is torn down. Upstream drops it, and so
    //    does this -- origAt aims at the branch slot, which overwrites it.
    PATCHED,  // +0 bl susamuneQfTick
    PATCHED,  // +1 b -> site+4 (and the slot the dropped original lands in)

    // -- CV_DTOR: ~TMarDirector. Banks the stage's ticks into the running
    //    total, unless the timer has already stopped or a restart is armed.
    //      lhz r0,0xB2 / cmpwi / bne / lwz r0,0xB4 / lwz r6,0x5C(r3) / add / stw
    //    One `lhz` covers both flag bytes, which is why they are adjacent.
    LIS(5),
    LHZ(0, 5, D(ST(stopped))),
    CMPWI0(0),
    BNE(0x14),
    LWZ(0, 5, D(ST(base))),
    LWZ(6, 3, 0x5C),
    ADD(0, 0, 6),
    STW(0, 5, D(ST(base))),
    PATCHED,  // +8 original
    PATCHED,  // +9 b -> site+4

    // -- CV_NSTAT: TMarDirector::nextStateInitialize+0x56C, and
    // -- CV_CARD:  TCardLoad::changeScene+0x1278.
    //    The two paths that arm a restart. Both take the flag from whatever the
    //    displaced original left in a register, which is upstream's encoding
    //    verbatim: `addi r4,r28,1` here, `cmpwi r3,1` there.
    PATCHED,  // +0 original (addi r4,r28,1)
    LIS(5),
    STB(4, 5, D(ST(resetPending))),
    PATCHED,  // +3 b -> site+4

    PATCHED,  // +0 original (cmpwi r3,1)
    LIS(3),
    STB(5, 3, D(ST(resetPending))),
    PATCHED,  // +3 b -> site+4
};

// A miscount here silently overlaps two caves, which executes as garbage.
static_assert(sizeof(gCaves) / sizeof(gCaves[0]) == CV_END,
              "gCaves is out of step with its index enum");

// ---------------------------------------------------------------- sites ----
//
// One mutable row per site, so the install and write loops walk a single
// pointer: the captured original lives here rather than in a parallel array.
// `gate` is the SettingId that enables the row, or kAlways / kStatusAny.
struct Site {
    u32 addr;
    u32 orig;     // retail word, captured at install
    u8  cave;     // first word of this site's cave, in gCaves
    u8  caveEnd;  // one past its last, which is always the branch back. 0 for
                  // the rows that share the freezer and own nothing
    u8  origAt;   // where in the cave the displaced original goes
    u8  gate;     // a SettingId, or kAlways / kStatusAny
};

const u8 kAlways    = 0xFF;
const u8 kStatusAny = 0xFE;

Site gSites[] = {
    { SUSAMUNE_MEM1_ADDR(0x80196CB0u, 0x801BEE10u, 0x801B6CC8u), 0, CV_FREEZE, 0,          0, SETTING_QF_FREEZE_YELLOW },  // TCoin::taken
    { SUSAMUNE_MEM1_ADDR(0x801963C4u, 0x801BE524u, 0x801B63DCu), 0, CV_FREEZE, 0,          0, SETTING_QF_FREEZE_RED    },  // TCoinRed::taken
    { SUSAMUNE_MEM1_ADDR(0x80197208u, 0x801BF3C4u, 0x801B727Cu), 0, CV_FREEZE, 0,          0, SETTING_QF_FREEZE_ITEM   },  // TItem::taken
    { SUSAMUNE_MEM1_ADDR(0x80214F00u, 0x80153A34u, 0x801489B4u), 0, CV_FREEZE, 0,          0, SETTING_QF_FREEZE_TALK   },  // TTalk2D2::openTalkWindow
    { SUSAMUNE_MEM1_ADDR(0x800ED89Cu, 0x8029A318u, 0x802921B0u), 0, CV_FREEZE, 0,          0, SETTING_QF_FREEZE_DEMO   },  // fireStartDemoCamera
    { SUSAMUNE_MEM1_ADDR(0x8017A3D4u, 0x80215C6Cu, 0x8020DB50u), 0, CV_FREEZE, 0,          0, SETTING_QF_FREEZE_CLEANED },  // TBaseNPC::emitHappyEffect_
    { SUSAMUNE_MEM1_ADDR(0x801D3380u, 0x801FB7ACu, 0x801F3690u), 0, CV_FREEZE, 0,          0, SETTING_QF_FREEZE_BOWSER },  // TBathtub::quake
    { SUSAMUNE_MEM1_ADDR(0x8014F830u, 0x802704D4u, 0x80268260u), 0, CV_FREEZE, 0,          0, SETTING_QF_FREEZE_YOSHI  },  // TYoshi::ride
    { SUSAMUNE_MEM1_ADDR(0x80196128u, 0x801BE288u, 0x801B6140u), 0, CV_BLUE,  CV_TAKE,    0, SETTING_QF_FREEZE_BLUE   },  // TCoinBlue::taken+0x24
    { SUSAMUNE_MEM1_ADDR(0x8011EAE4u, 0x8023F9A8u, 0x80237734u), 0, CV_TAKE,  CV_DROP,    6, SETTING_QF_FREEZE_TAKE   },  // TMario::taking+0x98
    { SUSAMUNE_MEM1_ADDR(0x80122964u, 0x802437D4u, 0x8023B560u), 0, CV_DROP,  CV_STATUS,  6, SETTING_QF_FREEZE_DROP   },  // TMario::dropObject+0x38
    { SUSAMUNE_MEM1_ADDR(0x801335B8u, 0x802541C8u, 0x8024BF54u), 0, CV_STATUS, CV_STAR,    6, kStatusAny               },  // changePlayerStatus+0x194
    { SUSAMUNE_MEM1_ADDR(0x800EDB30u, 0x8029A5ACu, 0x80292480u), 0, CV_STAR,  CV_BATH,    9, kAlways                  },  // fireGetStar+0x48
    { SUSAMUNE_MEM1_ADDR(0x801D1F38u, 0x801FA380u, 0x801F2258u), 0, CV_BATH,  CV_CHGST,   9, kAlways                  },  // TBathtub::startDemo+0x21C
    { SUSAMUNE_MEM1_ADDR(0x800EC72Cu, 0x802991A8u, 0x80291040u), 0, CV_CHGST, CV_NEXT,    9, kAlways                  },  // changeState+0x328
    { SUSAMUNE_MEM1_ADDR(0x800ED8F0u, 0x8029A36Cu, 0x80292204u), 0, CV_NEXT,  CV_END,     0, kAlways                  },  // setNextStage+0x50
    { SUSAMUNE_MEM1_ADDR(0x802069E0u, 0x801441C0u, 0x80138DFCu), 0, CV_DRAW,  CV_DTOR,    1, kAlways                  },  // TGCConsole2::perform, the draw hook
    { SUSAMUNE_MEM1_ADDR(0x800EFA30u, 0x8029C520u, 0x802943FCu), 0, CV_DTOR,  CV_NSTAT,   8, kAlways                  },  // ~TMarDirector
    { SUSAMUNE_MEM1_ADDR(0x800EBD78u, 0x8029880Cu, 0x802906A4u), 0, CV_NSTAT, CV_CARD,    0, kAlways                  },  // nextStateInitialize+0x56C
    { SUSAMUNE_MEM1_ADDR(0x802257CCu, 0x80164E24u, 0x80159E9Cu), 0, CV_CARD,  CV_END,     0, kAlways                  },  // TCardLoad::changeScene+0x1278
};
const int kNumSites = (int)(sizeof(gSites) / sizeof(gSites[0]));
// The changePlayerStatus row, the one gated by a group rather than a setting.
const int kStatusRow = 11;

// Mario status words that freeze the timer (upstream's statusDB) and the
// setting each belongs to.
const u32 kStatus[] = {
    0x80000387u, 0x00000882u, 0x00000895u, 0x00000896u, 0x3800034Bu,
    0x02000886u, 0x00000884u, 0x00000892u, 0x00000893u,
};
const u8 kStatusGate[] = {
    SETTING_QF_FREEZE_PUT,    SETTING_QF_FREEZE_TRIPLE, SETTING_QF_FREEZE_SPIN,
    SETTING_QF_FREEZE_SPIN,   SETTING_QF_FREEZE_LEDGE,  SETTING_QF_FREEZE_WALLKICK,
    SETTING_QF_FREEZE_BOUNCE, SETTING_QF_FREEZE_ROPE,   SETTING_QF_FREEZE_ROPE,
};
const int kNumStatus = (int)(sizeof(kStatus) / sizeof(kStatus[0]));

bool gInited;
// Set by the perform hook, consumed by the draw: "the console drew this frame,
// and this is the total it computed".
bool gVisible;
s32  gShownTotal;

bool engineOn() {
    return gSettings.getBool(SETTING_QF_TIMER) ||
           gSettings.getBool(SETTING_QF_SECTION_TIMER);
}

__attribute__((noinline)) void installCaves() {
    for (int i = 0; i < kNumSites; i++) {
        Site &s = gSites[i];
        s.orig  = *(volatile u32 *)s.addr;
        if (s.caveEnd == 0) {
            continue;  // shares the freezer, which owns no per-site word
        }
        u32 *c    = &gCaves[s.cave];
        int  last = s.caveEnd - s.cave - 1;
        c[s.origAt] = s.orig;
        c[last]     = branchWord((u32)&c[last], s.addr + 4);
    }
    // The two slots that call somewhere absolute.
    gCaves[CV_STATUS + 3] =
        branchWord((u32)&gCaves[CV_STATUS + 3], (u32)&susamuneQfOnPlayerStatus) | 1u;
    gCaves[CV_DRAW] = branchWord((u32)&gCaves[CV_DRAW], (u32)&susamuneQfTick) | 1u;

    DCFlushRange(gCaves, sizeof(gCaves));
    ICInvalidateRange(gCaves, sizeof(gCaves));
    gInited = true;
}

// ------------------------------------------------------------- rendering ---
//
// Transcribed from upstream's `drawText` library code (the `077F0238` blob the
// two timers depend on) and from the fill_rect its draw hook calls. Not routed
// through Menu: the draw hook runs inside TGCConsole2::perform, in the
// console's own 2D space, not the one afterDraw sets up.

// Storage for a J2DPrint (JSystem/J2D/J2DPrint.cpp, 0x64 bytes). Raw on
// purpose: the game's constructor fills it in, and there is no destructor to
// run (J2DPrint::~J2DPrint is empty).
struct QfPrint {
    u8  head[0x58];
    s32 fontSizeX;  // 0x58, poked after construction; the ctor picks the
    s32 fontSizeY;  // 0x5C  font's native size and upstream overrides it
    u8  tail[0x08];
};

// Declared with their CodeWarrior names so the JUtility::TColor arguments can
// be spelled as the pointers MWCC actually passes them as.
extern "C" void qfPrintCtor(QfPrint *, void *font, int charSpace, int lineHeight,
                            const u32 *fgTop, const u32 *fgBottom)
    __asm__("__ct__8J2DPrintFP7JUTFontiiQ28JUtility6TColorQ28JUtility6TColor");
extern "C" void qfPrintPrint(QfPrint *, int x, int y, u8 alpha, const char *fmt, ...)
    __asm__("print__8J2DPrintFiiUcPCce");
extern "C" void qfPrintInit(QfPrint *) __asm__("initiate__8J2DPrintFv");

// ScrnFader.cpp's `fill_rect(const JDrama::TRect &, JUtility::TColor)`, the one
// upstream's hook calls. File-local, so it is not in the linker scripts and has
// to be an address. Both arguments arrive by pointer; TRect is four s32.
typedef void (*QfFillRect)(const s32 *rect, const u32 *color);
const QfFillRect qfFillRect =
    (QfFillRect)SUSAMUNE_MEM1_ADDR(0x80201EA8u, 0x80140390u, 0x80134F0Cu);

void drawBg(const SusamuneTextStyle &s) {
    const s32 rect[4] = { s.bgX0, s.bgY0, s.bgX1, s.bgY1 };
    qfFillRect(rect, &s.bg);
}

// One line of an element. Both timers print the same shape of number, so they
// share the call: the section timer's format stops after two conversions.
// `line` is upstream's per-line advance, which is the font size exactly.
void drawLine(const SusamuneTextStyle &s, int line, const char *fmt, u32 a, u32 b, u32 c) {
    QfPrint p;
    qfPrintCtor(&p, (void *)gpSystemFont, 0, s.fontSize, &s.fgTop, &s.fgBottom);
    p.fontSizeX = s.fontSize;
    p.fontSizeY = s.fontSize;
    // initiate() -> JUTResFont::setGX(), which is the ONLY thing that turns
    // GX_VA_TEX0 back on in the vertex descriptor. JUTResFont::drawChar_scale
    // writes a GXTexCoord2u16 per vertex unconditionally, and fill_rect clears
    // the descriptor down to POS + CLR0, so without this the glyph quads push
    // four bytes a vertex that the GP never consumes and the FIFO desyncs.
    // Upstream leaves it out and relies on the ambient descriptor; the game's
    // own text path (J2DTextBox::draw) always calls it.
    qfPrintInit(&p);
    qfPrintPrint(&p, s.x, s.y + line * s.fontSize, 0xFF, fmt, a, b, c);
}

// A quarterframe is 1001/120 ms. divwu, as upstream: a negative total (the
// first ticks after a reset, when base is -4) wraps rather than going negative.
u32 qfToMs(s32 qf) { return (u32)(qf * 1001) / 120u; }

}  // namespace

extern "C" void susamuneQfOnPlayerStatus(u32 status) {
    for (int i = 0; i < kNumStatus; i++) {
        if (kStatus[i] != status) {
            continue;
        }
        if (gSettings.getBool((SettingId)kStatusGate[i]) && gpMarDirector) {
            gGuiBlock.qf.freezeQf  = gpMarDirector->unk5C;
            gGuiBlock.qf.freezeCtr = gGuiBlock.cfg.qftFreezeFrames;
        }
        return;
    }
}

void qfTimerApply() {
    if (!gInited) {
        installCaves();
    }

    // Which rows want to be installed, as a bitmask, so the write loop walks
    // one pointer and tests one bit.
    u32 want = 0;
    if (engineOn()) {
        for (int i = 0; i < kNumSites; i++) {
            const u8 g = gSites[i].gate;
            if (g == kAlways || (g != kStatusAny && gSettings.getBool((SettingId)g))) {
                want |= 1u << i;
            }
        }
        for (int i = 0; i < kNumStatus; i++) {
            if (gSettings.getBool((SettingId)kStatusGate[i])) {
                want |= 1u << kStatusRow;
                break;
            }
        }
    }

    for (int i = 0; i < kNumSites; i++) {
        Site &s    = gSites[i];
        u32   word = s.orig;
        if (want & (1u << i)) {
            word = branchWord(s.addr, (u32)&gCaves[s.cave]);
        }
        if (*(volatile u32 *)s.addr != word) {
            writeGameCode(s.addr, word);
        }
    }
}

void qfTimerOnStageLoad() {
    SusamuneQfState &st = gGuiBlock.qf;

    // direct+0x88: the freeze always clears, the timer only restarts when
    // something armed it. -4 is what makes the clock read 0:00.000 on the last
    // black frame of the load.
    //   lbz r0,0xB3 / cmpwi 0 / li r0,0 / stw r0,0xBC / beq
    //   sth r0,0xB2 / li r0,-4 / stw r0,0xB4
    st.freezeCtr = 0;
    if (st.resetPending) {
        st.stopped      = 0;
        st.resetPending = 0;
        st.base         = -4;
    }

    // TApplication::proc+0x34: li r29,4 / stw r29,0x3CC / li r29,0 / sth r29,0x3CA
    if (!gSettings.getBool(SETTING_QF_SECTION_KEEP)) {
        st.lastQf = 4;
        st.count  = 0;
    }
}

// Upstream's two draw hooks, minus the drawing: called from CV_DRAW, i.e. from
// the tail of TGCConsole2::perform, which is where upstream advances the freeze
// counter and records a split. Reaching it is also what makes the timers
// visible this frame.
//
// Note that JDrama calls perform more than once per frame (once per perform
// list), so this runs more than once per frame -- as upstream's hook does.
extern "C" void susamuneQfTick() {
    SusamuneQfState &st = gGuiBlock.qf;

    s32 total = st.base;
    if (!st.stopped) {
        s32 live = gpMarDirector ? gpMarDirector->unk5C : 0;
        if (st.freezeCtr != 0) {
            st.freezeCtr--;
            live = st.freezeQf;
        }
        total = live + st.base;
    }
    if (total > 719280) {  // 99:59.999; upstream latches the timer here
        total      = 719280;
        st.base    = 719280;
        st.stopped = 0xB0;  // the constant's low byte, as upstream stores it
    }

    // The section-timer hook, which runs straight after and reads that state.
    // One entry per freeze, not one per frozen frame: freezeCtr stays non-zero
    // for the whole hold, but lastQf catches up on the first frame of it.
    if (st.freezeCtr != 0 && st.freezeQf > st.lastQf) {
        st.entries[st.count & 15] = st.freezeQf - st.lastQf;
        st.lastQf                 = st.freezeQf;
        st.count++;
    }

    gShownTotal = total;
    gVisible    = true;
}

void qfTimerDraw(void *ortho) {
    const bool visible = gVisible;
    gVisible           = false;  // one frame of console draw buys one frame of timer
    if (!visible) {
        return;
    }

    const SusamuneGuiConfig &cfg = gGuiBlock.cfg;
    const SusamuneQfState   &st  = gGuiBlock.qf;

    // Every fill_rect needs the flat 2D vertex state, and every text draw needs
    // the textured one that drawLine's initiate() establishes. See "The GX
    // state trap" in doc/qft_correspondence.md -- getting this wrong desyncs
    // the FIFO rather than just looking wrong.
    qfSetup2D(ortho);

    if (gSettings.getBool(SETTING_QF_TIMER)) {
        drawBg(cfg.qft);
        const u32 ms  = qfToMs(gShownTotal);
        const u32 sec = ms / 1000u;
        const u32 min = sec / 60u;
        drawLine(cfg.qft, 0, "%lu:%02lu.%03lu", min, sec - min * 60u, ms - sec * 1000u);
        qfSetup2D(ortho);
    }

    if (gSettings.getBool(SETTING_QF_SECTION_TIMER)) {
        drawBg(cfg.qfst);
        const int first = (st.count >> 4) ? st.count - 16 : 0;
        for (int i = first, line = 0; i < (int)st.count; i++, line++) {
            const u32 ms  = qfToMs(st.entries[i & 15]);
            const u32 sec = ms / 1000u;
            drawLine(cfg.qfst, line, "%2lu.%03lu", sec, ms - sec * 1000u, 0);
        }
        qfSetup2D(ortho);
    }
}
