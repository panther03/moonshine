# QFT / QSFT — gecko line ↔ port correspondence

This maps **every line** of the upstream `qft` (v1.5) and `qfst` (v0.1) gecko
codes onto the part of `src/qftimer.cpp` that handles it. IL runs are timed with
these codes, so this is the artefact that says the port is a port.

Sources:

- `~/src/gct-generator/Codes.xml`, `<id>qft</id>` and `<id>qfst</id>` — the raw
  gecko lines, one `<source>` per revision.
- `~/src/gct-generator/site/.vuepress/components/codes/qft/codegen.js`,
  `qfst/codegen.js`, `asm.js`, `text.js` — the generator that turns a user's
  layout into the config half of those lines.
- `~/src/gct-generator/site/.vuepress/components/codes/qft/code/GMSJ01.js` etc.
  — the per-revision hook addresses.

Addresses below are **GMSJ01**. Every one of them appears in `gSites` as a
`SUSAMUNE_MEM1_ADDR(jp, us, pal)` triple; the US/PAL columns are the same
`<source version=…>` blocks read the same way.

Disassembly convention: `0x817F0000` is upstream's scratch page. The port's
equivalent is `gGuiBlock`, pinned at the mod's link base
(`SUSAMUNE_ADDR_GUI_BLOCK`), so every `0x817F00xx` below reads as an offset into
`SusamuneQfState` and every `0x817F0094`/`0x0110`/`0x039C`/`0x03B0` as one into
`SusamuneGuiConfig`.

## State-word map

| upstream | `SusamuneQfState` | what it is |
|---|---|---|
| `0x817F00B2` `u8`  | `stopped`      | the run is over; `base` is final |
| `0x817F00B3` `u8`  | `resetPending` | restart on the next stage load |
| `0x817F00B4` `s32` | `base`         | quarterframes banked before this stage |
| `0x817F00B8` `s32` | `freezeQf`     | quarterframe the display is held at |
| `0x817F00BC` `s32` | `freezeCtr`    | frames of hold left; `-1` = indefinitely |
| `0x817F03CA` `u16` | `count`        | splits recorded |
| `0x817F03CC` `s32` | `lastQf`       | quarterframe of the last split |
| `0x817F03D0` `s32[16]` | `entries`  | the split ring |
| — | `caveScratch` | port-only: the `r4` spill the `changePlayerStatus` cave needs |

`stopped` and `resetPending` are adjacent in both, because two hooks clear both
with one `sth`.

| upstream | `SusamuneGuiConfig` |
|---|---|
| `0x817F0094` 4×`s32` rect + `0x817F0120` colour | `qft.bgX0..bgY1`, `qft.bg` |
| `0x817F00A4` `"%u:%02u.%03u"` | the literal in `susamuneQfDraw` |
| `0x817F0110` `{s16 x, s16 y, u32 fontSize, u32 top, u32 bot}` | `qft` |
| `0x817F039C` rect + `0x817F03AC` colour | `qfst.bgX0..bgY1`, `qfst.bg` |
| `0x817F03B0` drawTextOpt | `qfst` |
| `0x817F03C0` `"%2d.%03d"` | the literal in `susamuneQfDraw` |

The port stores the rect as four `s16` and widens to the `s32` `JDrama::TRect`
at the call (`drawBg`, [qftimer.cpp:421](../src/qftimer.cpp:421)); the config is
a published interface and 8 bytes an element is worth keeping.

---

# `qft`

## 1. `C20ECE44` — `TMarDirector::direct+0x88`, the stage-load reset

```
981A0260  stb  r0, 0x260(r26)     <- displaced original (unk260 = 1)
3CE0817F  lis  r7, 0x817F
880700B3  lbz  r0, 0xB3(r7)       ; resetPending
2C000000  cmpwi r0, 0
38000000  li   r0, 0
900700BC  stw  r0, 0xBC(r7)       ; freezeCtr = 0, always
41820010  beq  +0x10
B00700B2  sth  r0, 0xB2(r7)       ; stopped = resetPending = 0
3800FFFC  li   r0, -4
900700B4  stw  r0, 0xB4(r7)       ; base = -4
```

→ **`qfTimerOnStageLoad()`**, [qftimer.cpp:491](../src/qftimer.cpp:491). **Not
an injection.** `onSetup` is the mod's hook on the `bl setupObjects` two
instructions earlier, and the decomp shows the pair is straight-line
(`setupObjects(); unk260 = 1;` in `MarDirectorDirect.cpp`), so it is the same
point in the same frame.

`base = -4` is what makes the clock read `0:00.000` on the last black frame of
the load rather than starting one frame in.

## 2. `C22069E0` — `TGCConsole2::perform`, the draw hook

The site is `addi r0, r3, -0x64B8` in the function's epilogue: an elided
destructor writing a vtable pointer into a stack object two instructions before
the frame is torn down. **Upstream drops the displaced original**, and so does
the port — see `CV_DRAW`, [qftimer.cpp:250](../src/qftimer.cpp:250), where
`origAt` aims at the branch slot so the captured original is overwritten.

**This is the single most important line in the code.** It is why the timers
appear in exactly the situations upstream's do — the console is the in-game HUD,
so no `perform` means no timer on the title screen, the file select, or the
black frames of a stage transition — and it is why the quarterframe is sampled
at the same point in the frame as upstream's.

**The port hooks it for the state machine, not for the pixels.**
`susamuneQfTick()` runs here: the freeze counter, the 99:59.999 latch and the
section split, exactly as below. It records the total it computed and the fact
that the console drew, and `qfTimerDraw()` renders from `afterDraw` on the
strength of that. Same frame, same numbers, same 2D space; the only thing that
moves is *when in the frame* the geometry is submitted — see divergence 11.

Note also that JDrama calls `perform` once per perform list, so this hook — and
upstream's — runs **more than once per frame**. The freeze counter therefore
decrements more than once per frame in both.

```
3C60817F  lis  r3, 0x817F
60640120  ori  r4, r3, 0x120       ; &bgColour
38630094  addi r3, r3, 0x94        ; &rect
3D808020  lis  r12, 0x8020
398C1EA8  addi r12, r12, 0x1EA8    ; ScrnFader.cpp's file-local fill_rect
7D8803A6  mtlr r12
4E800021  blrl
```
→ `drawBg(cfg.qft)`, [qftimer.cpp:421](../src/qftimer.cpp:421). Same function,
reached by address (`qfFillRect`) because it is `static` and therefore not in
the linker scripts.

```
3C60817F  lis  r3, 0x817F
888300B2  lbz  r4, 0xB2(r3)        ; stopped
810300B4  lwz  r8, 0xB4(r3)        ; base
2C040000  cmpwi r4, 0
40A20030  bne  cap                 ; stopped -> total = base
808300BC  lwz  r4, 0xBC(r3)        ; freezeCtr
2C040000  cmpwi r4, 0
40A20010  bne  frozen
810D97E8  lwz  r8, gpMarDirector(r13)
8108005C  lwz  r8, 0x5C(r8)        ; live quarterframe
48000010  b    add
frozen:
3884FFFF  addi r4, r4, -1
908300BC  stw  r4, 0xBC(r3)        ; freezeCtr--
810300B8  lwz  r8, 0xB8(r3)        ; freezeQf
add:
800300B4  lwz  r0, 0xB4(r3)
7D080214  add  r8, r8, r0
cap:
3CE0000A  lis  r7, 0xA
60E7F9B0  ori  r7, r7, 0xF9B0      ; 719280 == 99:59.999
7C074000  cmpw r7, r8
40A00010  bge  fmt
7CE83B78  mr   r8, r7
98E300B2  stb  r7, 0xB2(r3)        ; stopped = 0xB0, the constant's low byte
90E300B4  stw  r7, 0xB4(r3)        ; base = 719280
```
→ the first half of **`susamuneQfDraw()`**,
[qftimer.cpp:519](../src/qftimer.cpp:519), statement for statement, including
`stopped = 0xB0` (a `stb` of a word constant — the port writes the same byte
rather than a tidier `1`, because it is the value the code leaves behind).

```
1D0803E9  mulli r8, r8, 1001
38000078  li   r0, 120
7D080396  divwu r8, r8, r0         ; ms
380003E8  li   r0, 1000
7D280396  divwu r9, r8, r0         ; sec
7C0901D6  mullw r0, r9, r0
7CE04050  subf r7, r0, r8          ; ms % 1000
3800003C  li   r0, 60
7CA90396  divwu r5, r9, r0         ; min
7C0501D6  mullw r0, r5, r0
7CC04850  subf r6, r0, r9          ; sec % 60
```
→ `qfToMs()` + the three divisions,
[qftimer.cpp:439](../src/qftimer.cpp:439) and
[:544](../src/qftimer.cpp:544). `divwu`, not `divw`: the first ticks after a
reset make `total` negative (`base` is `-4`), and upstream lets that wrap rather
than go negative. `qfToMs` casts to `u32` before dividing for exactly that
reason.

```
3D80817F  lis  r12, 0x817F
618C0238  ori  r12, r12, 0x238     ; the `drawText` dependency blob
7D8803A6  mtlr r12
388300A4  addi r4, r3, 0xA4        ; fmt
38630110  addi r3, r3, 0x110       ; opt
4E800021  blrl
```
→ `drawLine(cfg.qft, 0, "%lu:%02lu.%03lu", …)`,
[qftimer.cpp:429](../src/qftimer.cpp:429). `drawLine` **is** the `drawText`
blob, transcribed — see "The `drawText` dependency" below. The format is
upstream's `"%u:%02u.%03u"` with `l` added because `u32` is `unsigned long` on
this target; it consumes the same four bytes per conversion.

```
38610E90  addi r3, r1, 0xE90
3D808003 398C5228 7D8803A6 4E800021   ; J2DGrafContext::setup2D
```
→ **`qfSetup2D(ctx)`**, [qftimer.cpp:551](../src/qftimer.cpp:551). Kept, and
**load-bearing rather than tidy-up.** `JUTResFont::drawChar_scale` ends with

```c
GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_CLR_RGBA, GX_F32, 0);
```

so drawing text switches `VTXFMT0`'s position format to `f32`. `fill_rect`
writes `GXPosition3s16` — six bytes where the GP now expects twelve. Without
this call between the two elements, the section timer's background desyncs the
FIFO and Dolphin reports `GFX FIFO: Unknown Opcode`.

Because it belongs *between* the two elements and the port folds both hooks into
one function, it lives in C rather than in the cave, and the cave passes the
context in instead:

```
addi r3, r1, 0xE90     ; the J2DOrthoGraph in perform's frame
bl   susamuneQfDraw
b    site+4
```

The `0xE90` is `perform`'s frame offset of that object, baked per revision as
`kPerformCtx` ([qftimer.cpp:100](../src/qftimer.cpp:100)) — `0xE90` / `0xBD0` /
`0xBE4`, read straight out of the three `<source>` blocks.

## 3. `C20EFA30` — `~TMarDirector`, banking the stage's ticks

```
3CA0817F  lis  r5, 0x817F
A00500B2  lhz  r0, 0xB2(r5)        ; stopped and resetPending, one halfword
2C000000  cmpwi r0, 0
40820014  bne  skip
800500B4  lwz  r0, 0xB4(r5)
80C3005C  lwz  r6, 0x5C(r3)
7C003214  add  r0, r0, r6
900500B4  stw  r0, 0xB4(r5)
skip:
7C0802A6  mflr r0                  <- displaced original
```

→ `CV_DTOR`, [qftimer.cpp:270](../src/qftimer.cpp:270) — instruction for
instruction, with `lis r5` retargeted at `gGuiBlock` and the original captured
at install.

This is what carries the clock across an area change. An earlier revision of
this port folded it into `qfTimerOnStageLoad` by caching `unk5C` every frame and
adding it in at the next load; that is **not** equivalent, because the director
keeps ticking between the last drawn frame and its destructor, and the
difference lands in the banked total of a multi-area run.

## 4. `C20EDB30` — `fireGetStar+0x48`, the shine grab

```
3CA0817F  lis  r5, 0x817F
80C500B4  lwz  r6, 0xB4(r5)        ; base
8003005C  lwz  r0, 0x5C(r3)
7CC60214  add  r6, r6, r0
38C60004  addi r6, r6, 4
54C6003A  rlwinm r6, r6, 0, 0, 29  ; round up to the frame boundary
90C500B4  stw  r6, 0xB4(r5)
38C0FFFF  li   r6, -1
B0C500B2  sth  r6, 0xB2(r5)        ; stopped = resetPending = 0xFF
```
→ `CV_STAR`, [qftimer.cpp:189](../src/qftimer.cpp:189).

## 5. `C21D1F38` — `TBathtub::startDemo+0x21C`, beating Bowser

Identical body; `r3` is already `gpMarDirector` and `r5` is live, so the pointer
goes in `r8`. → `CV_BATH`, [qftimer.cpp:205](../src/qftimer.cpp:205).

## 6. `C22257CC` — `TCardLoad::changeScene+0x1278`

```
2C030001  cmpwi r3, 1              <- displaced original
3C60817F  lis  r3, 0x817F
98A300B3  stb  r5, 0xB3(r3)        ; resetPending = r5
```
→ `CV_CARD`, [qftimer.cpp:286](../src/qftimer.cpp:286). Transcribed verbatim,
including taking the flag from `r5` rather than storing a `1`: `r5` is whatever
the `bl` four instructions earlier left, and the port reproduces that rather
than second-guessing it.

## 7. `C20EBD78` — `TMarDirector::nextStateInitialize+0x56C`

```
389C0001  addi r4, r28, 1          <- displaced original
3CA0817F  lis  r5, 0x817F
988500B3  stb  r4, 0xB3(r5)        ; resetPending = r28 + 1
```
→ `CV_NSTAT`, [qftimer.cpp:285](../src/qftimer.cpp:285). Same treatment.

**These two were missing from the first cut of the port**, which is why the
timer never restarted: nothing but the quit-to-file-select path armed a reset.

## 8. `C20EC72C` — `changeState+0x328`, quit to file select

```
3CA0817F  lis  r5, 0x817F
38600001  li   r3, 1
986500B3  stb  r3, 0xB3(r5)        ; resetPending = 1
807F005C  lwz  r3, 0x5C(r31)
38630003  addi r3, r3, 3
5463003A  rlwinm r3, r3, 0, 0, 29
906500B8  stw  r3, 0xB8(r5)        ; freezeQf
3860FFFF  li   r3, -1
906500BC  stw  r3, 0xBC(r5)        ; freezeCtr = -1, i.e. forever
```
→ `CV_CHGST`, [qftimer.cpp:219](../src/qftimer.cpp:219).

**Upstream drops the instruction it displaces.** That instruction is
`sth r28, 0x94(r1)` with `r28 == 0`, zero-initialising a stack field whose
address is handed to the call three instructions later. The port captures and
replays it, so the field is initialised. This is a deliberate divergence — a
fix, not a behaviour change to the timer.

## 9. `C20ED8F0` — `setNextStage+0x50`, an area change inside a run

```
3CA0817F  lis  r5, 0x817F
980500B3  stb  r0, 0xB3(r5)        ; resetPending = r0, which is 0 here
801E005C  lwz  r0, 0x5C(r30)
30000004  addic r0, r0, 4          ; addic, not addi: rA==0 would read as literal 0
5400003A  rlwinm r0, r0, 0, 0, 29
900500B8  stw  r0, 0xB8(r5)
3800FFFF  li   r0, -1
900500BC  stw  r0, 0xBC(r5)
```
→ `CV_NEXT`, [qftimer.cpp:234](../src/qftimer.cpp:234). Explicitly **not** a
reset, which is what carries the clock through a pipe, a door or a secret
course.

Same dropped-original story as above: the displaced `stb r0, 0x39(r1)` is
another zero-init of a struct that is then passed by pointer, and the port
replays it. The port also stores an explicit `0` instead of relying on `r0`
already being zero.

The `addic` is the one place where a hand-assembled `addi` would silently
change meaning: in D-form, `rA == 0` reads as a literal zero, so
`addi r0, r0, 4` is `li r0, 4`. `ADDIC` exists in the port's macro set for this
one instruction.

## 10. The freeze triggers (generated, not in `<source>`)

`qft/codegen.js` emits these from the eighteen checkboxes. Three shapes:

**a. The shared freezer.** For each enabled trigger at a trailing `blr`, a `C6`
branch to a `06` blob at `0x817F0348`:

```
816D97E8  lwz  r11, gpMarDirector(r13)
3D80817F  lis  r12, 0x817F
816B005C  lwz  r11, 0x5C(r11)
916C00B8  stw  r11, 0xB8(r12)
39600000|frame  li r11, frame
916C00BC  stw  r11, 0xBC(r12)
4E800020  blr
```
→ `CV_FREEZE`, [qftimer.cpp:127](../src/qftimer.cpp:127), and the eight rows of
`gSites` that name it with `caveEnd == 0`
([qftimer.cpp:320](../src/qftimer.cpp:320)). Sharing one cave across a `blr` is
upstream's trick and is what makes a per-trigger toggle cost one word at the
site.

The one difference: `li r11, frame` is a **compile-time constant** upstream and
an `lhz` of `cfg.qftFreezeFrames` here, because the port's freeze duration comes
from the config block a Gecko code can overwrite. `frame == 0` makes upstream
omit every freeze hook; here it leaves `freezeCtr = 0`, which every reader
already treats as "not frozen".

Addresses, from `qft/code/GMSJ01.js`: `yellowCoin 80196CB0`, `redCoin 801963C4`,
`item 80197208`, `talk 80214F00`, `demo 800ED89C`, `cleaned 8017A3D4`,
`bowser 801D3380`, `yoshi 8014F830`.

**b. `take` / `drop`,** which are not epilogues, so each gets a `C2` carrying
its own copy plus the displaced original. → `CV_TAKE` / `CV_DROP`,
[qftimer.cpp:153](../src/qftimer.cpp:153). Upstream hardcodes the originals
(`801F0384`, `38000000`); the port captures them.

**c. `blueCoin`,** at the *head* of `TCoinBlue::taken`, so the quarterframe is
rounded up to the next frame boundary before it is stored:

```
7C030378  mr   r3, r0              <- displaced original
80A3005C  lwz  r5, 0x5C(r3)
38A50003  addi r5, r5, 3
54A0003A  rlwinm r0, r5, 0, 0, 29
3CA0817F  lis  r5, 0x817F
900500B8  stw  r0, 0xB8(r5)
38000000|frame  li r0, frame
900500BC  stw  r0, 0xBC(r5)
```
→ `CV_BLUE`, [qftimer.cpp:139](../src/qftimer.cpp:139).

**d. The status triggers.** `put`, `tripleJump`, `spinJump`, `ledgeGrab`,
`wallKick`, `bounce` and `ropeJump` all hook `changePlayerStatus+0x194` and
compare `r29` against a status word; upstream open-codes one comparison chain
per enabled trigger, ending in `beqlrl` into the shared freezer, with
`li r0, 0` as the displaced original.

→ `CV_STATUS` ([qftimer.cpp:175](../src/qftimer.cpp:175)) plus
`susamuneQfOnPlayerStatus()` ([qftimer.cpp:443](../src/qftimer.cpp:443)). **This
is the one hook that is a call into C rather than a transcription**, because the
set of enabled statuses changes at runtime here and is fixed at generation time
upstream. The status words are `statusDB` verbatim
([qftimer.cpp:347](../src/qftimer.cpp:347)):

| setting | status words |
|---|---|
| `put` | `0x80000387` |
| `tripleJump` | `0x00000882` |
| `spinJump` | `0x00000895`, `0x00000896` |
| `ledgeGrab` | `0x3800034B` |
| `wallKick` | `0x02000886` |
| `bounce` | `0x00000884` |
| `ropeJump` | `0x00000892`, `0x00000893` |

The cave spills `r4` into `caveScratch` around the call, because a C callee may
clobber every volatile and upstream's chain clobbers only `r12`. Disassembling
the site shows `r4` is the only volatile live across it — it is stored to
`0x80(r30)` two instructions later — so one spill is sufficient and no more.

---

# `qfst`

## 11. `C20F9DD0` — `TApplication::proc+0x34`, the section reset

```
3C60817F  lis  r3, 0x817F
3BA00004  li   r29, 4
93A303CC  stw  r29, 0x3CC(r3)      ; lastQf = 4
3BA00000  li   r29, 0
B3A303CA  sth  r29, 0x3CA(r3)      ; count = 0
```
→ the tail of `qfTimerOnStageLoad()`,
[qftimer.cpp:505](../src/qftimer.cpp:505), behind `SETTING_QF_SECTION_KEEP`
(the port's "never reset" toggle — a feature on top of upstream).

**Not an injection.** `proc+0x34` is the top of the app-state loop, so upstream
also resets when the file-select director is built; the port only resets on a
stage load. Nothing draws the section timer between those two points, because
`TGCConsole2::perform` does not run outside a stage, so the two are
indistinguishable.

## 12. `C22069E4` — the section timer's draw hook

Injected at the instruction *after* `qft`'s, so it runs immediately afterwards
and reads the state `qft` has just updated. Same elided-destructor site, same
dropped original. The port folds both hooks into the one `susamuneQfDraw()`.

```
3821FFD0 BF210008                  ; frame + stmw r25 — the port's C prologue
3F20817F  lis  r25, 0x817F
AB9903B2  lha  r28, 0x3B2(r25)     ; opt.y, saved
3BF903D0  addi r31, r25, 0x3D0     ; &entries
A3D903CA  lhz  r30, 0x3CA(r25)     ; count
7F9DE378  mr   r29, r28            ; the running y cursor
835903B4  lwz  r26, 0x3B4(r25)     ; opt.fontSize — also the line advance
```
→ the section-timer block of `susamuneQfDraw()`,
[qftimer.cpp:551](../src/qftimer.cpp:551). Upstream writes the advancing `y`
back into the config each line and restores it at the end; the port computes
`s.y + line * s.fontSize` in `drawLine` instead
([qftimer.cpp:435](../src/qftimer.cpp:435)) and never touches the config. Same
positions, and the config stays read-only, which matters because a Gecko code
owns it.

```
80D900BC  lwz  r6, 0xBC(r25)       ; freezeCtr
28060000  cmplwi r6, 0
41A2002C  beq  draw
809900B8  lwz  r4, 0xB8(r25)       ; freezeQf
80B903CC  lwz  r5, 0x3CC(r25)      ; lastQf
7C042800  cmpw r4, r5
40A1001C  ble  draw                ; signed: only a forward split counts
7C052050  subf r0, r5, r4
57CC16BA  rlwinm r12, r30, 2, 26, 29  ; (count & 15) * 4
7C1F612E  stwx r0, r31, r12        ; entries[count & 15] = freezeQf - lastQf
909903CC  stw  r4, 0x3CC(r25)      ; lastQf = freezeQf
3BDE0001  addi r30, r30, 1
B3D903CA  sth  r30, 0x3CA(r25)     ; count++
```
→ [qftimer.cpp:535](../src/qftimer.cpp:535). One entry per freeze, not one per
frozen frame: `freezeCtr` stays non-zero for the whole hold, but `lastQf` is
advanced to `freezeQf` on the first frame, so the `>` guard fails afterwards.

```
3879039C  addi r3, r25, 0x39C
389903AC  addi r4, r25, 0x3AC
3D808020 398C1EA8 7D8803A6 4E800021   ; fill_rect
```
→ `drawBg(cfg.qfst)`, [qftimer.cpp:552](../src/qftimer.cpp:552). Once, before
the loop — the rect is sized for a full sixteen-line block regardless of how
many splits exist, which is why the configurator measures the placeholder text
and not the live string.

```
57DBE13F  rlwinm. r27, r30, 28, 4, 31   ; count >> 4
41820008  beq  +8
3B7EFFF0  addi r27, r30, -0x10          ; first = count - 16
loop:
7C1BF040  cmplw r27, r30
4080004C  bge  done
576316BA  rlwinm r3, r27, 2, 26, 29
7C1F182E  lwzx r0, r31, r3              ; entries[i & 15]
1D6003E9 38000078 7D6B0396              ; * 1001 / 120  -> ms
380003E8 7CAB0396                       ; / 1000        -> sec
1C0503E8 7CC05850                       ; ms % 1000
387903B0  addi r3, r25, 0x3B0           ; opt
389903C0  addi r4, r25, 0x3C0           ; fmt "%2d.%03d"
39990238 7D8803A6 4E800021              ; drawText
7FBDD214  add  r29, r29, r26            ; y += fontSize
3B7B0001  addi r27, r27, 1
B3B903B2  sth  r29, 0x3B2(r25)
4BFFFFB4  b    loop
done:
B39803B2  sth  r28, 0x3B2(r25)          ; restore opt.y
BB210008 38210030                        ; epilogue
```

Note what is *not* here: upstream's section timer ends without a `setup2D`, so
it hands `VTXFMT0` on to the next drawer in the textured `f32` layout. That is
the same trap as above, one step further out, and it only survives because the
fader and the next console frame set their own formats. The port adds the
restore — see divergence 9.
→ [qftimer.cpp:553](../src/qftimer.cpp:553). The ring shows the **last sixteen**
splits: `first = (count >> 4) ? count - 16 : 0`, then `entries[i & 15]`.

---

# The `drawText` dependency (`077F0238`)

Both codes list `drawText` as a dependency; it is a `06` blob, not a hook. Its
documented interface is

```c
typedef struct { int16_t x, y; uint32_t fontSize, colorTop, colorBot; } DrawTextOpt;
void drawText(DrawTextOpt *opt, const char *fmt, ...);
```

which is `SusamuneTextStyle`'s first four words plus the two colours — the
layout in `gui_config.hxx` is that struct with the background rect merged in.

What the blob actually does, once the varargs save area is built:

```
li    r9, 0x200                     ; va_list header
lwz   r6, 4(r31)                    ; opt->fontSize
lwz   r4, gpSystemFont(r13)
addi  r8, r31, 0xC                  ; &opt->colorBot
addi  r7, r31, 8                    ; &opt->colorTop
li    r5, 0
addi  r3, r1, 8
bl    J2DPrint::J2DPrint(JUTFont*, int, int, TColor, TColor)
lwz   r9, 4(r31)
stw   r9, 0x64(r1)                  ; this->mFontSizeY = fontSize
stw   r9, 0x60(r1)                  ; this->mFontSizeX = fontSize
lha   r10, 0(r31)
stw   r10, 0x24(r1)                 ; this->unk1C = x
psq_l  f0, 0(r31), 0, 5             ; (x, y) as s16 -> two floats
psq_st f0, 0x2C(r1), 0, 0           ; this->mCursorH / mCursorV
li    r0, 0
stw   r0, 0x34(r1)                  ; this->unk2C = 0
bl    J2DPrint_print_alpha_va(this, 0xFF, fmt, va)
```

→ **`drawLine()`**, [qftimer.cpp:429](../src/qftimer.cpp:429):

```c
QfPrint p;
qfPrintCtor(&p, (void *)gpSystemFont, 0, s.fontSize, &s.fgTop, &s.fgBottom);
p.fontSizeX = s.fontSize;
p.fontSizeY = s.fontSize;
qfPrintPrint(&p, s.x, s.y + line * s.fontSize, 0xFF, fmt, a, b, c);
```

Point by point:

- **Same font**: `gpSystemFont`, the `JUTResFont*` at `_SDA_BASE_-0x6808`, passed
  straight in as the `JUTFont*`.
- **Same constructor**, and the `TColor` arguments are declared as `const u32 *`
  because that is how CodeWarrior passes a 4-byte class with a copy constructor
  — the blob loads `r7`/`r8` with `&colorTop` / `&colorBot`, not with the
  values.
- **Same font size**, poked into `mFontSizeX` / `mFontSizeY` at `0x58` / `0x5C`
  *after* construction, because the constructor sets them to the font's native
  size.
- **Same cursor.** The blob writes `unk1C`, `mCursorH`, `mCursorV`, `unk2C`
  directly; `J2DPrint::print(x, y, opacity, fmt, …)` is `locate(x, y)` followed
  by the same `print_alpha_va`, and `locate` writes exactly those four fields.
  It also writes `unk20`, which only `printReturn` reads.
- **`initiate()` is called; the blob does not call it.** This is the one place
  the port cannot follow upstream — see "The GX state trap" below.
- **`y` is the baseline.** `J2DPrint::parse` hands `mCursorV` straight to
  `JUTFont::drawChar_scale`.

The port's earlier renderer went through `Menu::drawTextBaseline`, i.e. through
`J2DTextBox::draw` — which builds its own `J2DPrint`, calls `setFontSize`,
`setSomeColors` **and** `initiate()`, loads a position matrix from the pane, and
prints at `(0, 0)` under that matrix. Different font object, different GX
colours, different origin. That is what made the two timers disagree on screen.

## The GX state trap

Three functions are involved and they disagree about the vertex format:

| | vertex descriptor | num texgens | `VTXFMT0` POS |
|---|---|---|---|
| `J2DGrafContext::setup2D` | POS + CLR0, **TEX0 off** | 0 | `S16` |
| `fill_rect` | POS + CLR0, **TEX0 off** | 0 | *(unchanged)* |
| `JUTResFont::setGX` (via `J2DPrint::initiate`) | POS + CLR0 + **TEX0** | 1 | `S16` |
| `JUTResFont::drawChar_scale` | *(unchanged)* | — | sets `F32` itself |

`drawChar_scale` writes `GXPosition3f32` + `GXColor1u32` + **`GXTexCoord2u16`**
per vertex, unconditionally. `fill_rect` writes `GXPosition3s16` +
`GXColor1u32`. Each only works under the descriptor its own path establishes:

- **fill after text** → 6 bytes written where the GP expects 12 (`F32` POS).
- **text after fill** → 20 bytes written where the GP expects 16 (no `TEX0`,
  0 texgens).

Either way the GP resynchronises on the wrong byte and Dolphin reports
`GFX FIFO: Unknown Opcode`. The `0xC0` it reports is the top byte of an `f32`
position being read as a command.

The upstream hook calls `fill_rect` and then `drawText` **without** an
`initiate()` between them, which is the second case above. The port calls
`initiate()` in `drawLine` — what the game's own text path
(`J2DTextBox::draw`) does every time — and `setup2D` before each `fill_rect` —
what `Menu::fillBox` already does for the same reason. Neither changes a
coordinate: `J2DOrthoGraph::setLookat` leaves `mPosMtx` as the identity, and
`setup2D` does not touch the projection (`setPort` does).

`initiate()` calls `mFont->setGX(unk3C, unk40)`, and `private_initiate` has
already set those to `0` / `0xFFFFFFFF`, which is exactly the pair that takes
`JUTResFont::setGX`'s plain `GX_MODULATE` branch. The gradient still comes from
`parse`'s `setGradColor(unk8, unkC)`, i.e. from `fgTop` / `fgBottom`, so the
colours are unaffected.

## Background rectangles

The rect is resolved by the configurator, not by the mod, and the formula is
`getFillRectParams` in `asm.js` verbatim:

```js
x0 = x - bgLeft
y0 = y - fontSize - bgTop
x1 = x + ceil(width  * fontSize / 20) + bgRight
y1 = y - fontSize + ceil(height * fontSize / 20) + bgBot
```

where `{width, height}` is `measureText(placeholder, version)` — the game's own
per-glyph advances, from `charInfo-{JP,US,EU}.json`. `site/js/gecko.js`
implements the same function against the same tables. The mod ships the JP
result as its compiled-in default, which is why `gGuiBlock`'s defaults carry
`{16, 434, 130, 456}` and `{529, 133, 596, 347}`.

## Defaults

`qft/codegen.js` and `qfst/codegen.js` `defaultConfig`, reproduced exactly in
`gGuiBlock` ([qftimer.cpp:52](../src/qftimer.cpp:52)):

| | qft | qfst |
|---|---|---|
| `x`, `y` | 16, 456 | 533, 150 |
| `fontSize` | 20 | 13 |
| fg | `#FFFFFF` α255 | `#FFFFFF` α255 |
| bg | `#000000` α128 | `#000000` α64 |
| bg padding L/R/T/B | 0, 2, 2, 0 | 4, 3, 4, 2 |
| `freezeDuration` | 30 | — |

Freeze defaults: every trigger on **except yellow coin**. The port's settings
defaults match.

---

# Divergences, in one list

Everything below is a deliberate difference. There is nothing else.

| # | difference | why |
|---|---|---|
| 1 | `direct+0x88` and `proc+0x34` are not injections | the mod already runs at an indistinguishable point |
| 2 | freeze duration is an `lhz` of the config, not an immediate | it is Gecko-writable here |
| 3 | the status chain is a call into C | the enabled set changes at runtime; fixed at generation time upstream |
| 4 | displaced originals are captured at install, not hardcoded | region independence — and it incidentally fixes upstream's two dropped stack initialisers at `changeState+0x328` and `setNextStage+0x50` |
| 5 | the section timer's `y` is computed, not written back into the config | the config is owned by a Gecko code and must stay read-only |
| 6 | `qfst` reset is behind `SETTING_QF_SECTION_KEEP`; both timers have visibility toggles | the mod's own features |
| 7 | the two draw hooks are one C function, so upstream's `setup2D` between them happens after both | `fill_rect` and `drawChar_scale` each set the vertex state they need, so nothing observes the gap |
| 8 | `%lu` for `%u`, `%2lu` for `%2d` | `u32` is `unsigned long` on this target; same four bytes per conversion |
| 9 | `setup2D` before each `fill_rect` and after the section timer, neither of which upstream has | see "The GX state trap". Upstream's own hook draws a background under whatever vertex state the console left, and hands the textured layout on to the next drawer |
| 10 | `J2DPrint::initiate()` before printing, which upstream's `drawText` skips | same. It is the only call that turns `GX_VA_TEX0` back on, and `fill_rect` two lines earlier has just turned it off |
| 11 | the geometry is submitted from `afterDraw`, not from inside the `perform` hook | the hook still decides *whether* and *what* to draw, so visibility and the sampled quarterframe are unchanged. Drawing from inside the console's teardown proved fragile; `afterDraw` is a context the mod already renders in every frame. The visible consequence is that the timer now composites over the screen fader instead of under it |

## Known interaction

**No Shine Get Animation** replaces the `bl fireGetStar` at `winDemo+0x88`, and
the QFT stop hook lives *inside* `fireGetStar`. With that feature on, the timer
never stops on a shine. Recorded in `doc/todo.md`.
