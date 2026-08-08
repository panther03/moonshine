# Porting SMS practice gecko codes into the mod

This is the working guide for turning the Super Mario Sunshine practice
"gecko" codes into native mod features. The end goal (see `doc/gecko_codes.md`
for the wishlist) is that everything a runner currently loads as a `.gct` of
gecko codes becomes a first-class, menu-toggleable part of susamune, correct on
all three supported revisions.

## Sources of truth

- **`../../src/gct-generator/Codes.xml`** — the upstream code database. Each
  `<code>` has a human `<description>` (what it does, and any button binds) and
  one `<source version="…">` block per game revision holding the raw gecko
  lines. This is the format the practice-code website compiles per region.
- **`../../src/gct-generator/ram_map.xlsx`** — a documented SMS RAM map. More
  detailed than the linker map for struct layouts (e.g. the `TFlagManager`
  flag struct). Use it when a code touches a field whose meaning isn't obvious.
- **`../../src/sms`** — the decomp. Canonical for any type layout / code path
  you need to confirm while reversing an asm code.

### Revisions

The XML carries four columns; the mod supports **three**:

| XML `version` | mod `VERS` | notes |
|---|---|---|
| `GMSJ01` | `jp` | |
| `GMSE01` | `us` | |
| `GMSP01` | `pal` | |
| `GMSJ0A` | — | JP revision A; **ignore**, not a supported target |

Every game address in the mod is written through
`SUSAMUNE_MEM1_ADDR(jp, us, pal)` (`include/susamune/addresses.hxx`), which
selects the right column at compile time from the `SUSAMUNE_VERSION_*` define
CMake sets. So a ported code is one table row carrying all three addresses.

## Gecko code types you'll meet

A gecko line is two 32-bit words: `TTAAAAAA VVVVVVVV`. `TT` (top bits of the
first word) is the type; the address operand is `0x80000000 | 0xAAAAAA`.

| Encoding | Meaning | Port as |
|---|---|---|
| `04AAAAAA VVVVVVVV` | write 32-bit `V` at `0x80AAAAAA` | whole-word patch |
| `02AAAAAA 0000VVVV` | write 16-bit `V` at `0x80AAAAAA` | masked (half-word) patch |
| `00AAAAAA 000000VV` | write 8-bit `V` | masked (byte) patch |
| `06AAAAAA NNNNNNNN` + data | write an `N`-byte blob (a "code cave") | see Class B |
| `C6AAAAAA TTTTTTTT` | insert a **branch** at `0x80AAAAAA` to `0xTTTTTTTT` | whole-word patch, value = encoded `b` |
| `C2AAAAAA NNNNNNNN` + asm | **insert assembly**: branch from the site into `N` lines of PPC asm hosted by the code handler, then back | Class B (trampoline) |
| `C0…` | execute assembly once per pass (not injected at a site) | Class B |
| `28/2A/…`, `80/82/86/8A`, `E0000000` | the gecko VM: conditionals, gecko-register loads/stores, block terminators | used by the multi-feature codes (DPad, Nozzle Lock); reverse the *effect*, see Class C |

### Decoding a `C6` branch

`C6` writes a plain `b` (no link). The word to write is
`0x48000000 | ((target - addr) & 0x03FFFFFC)`. Because the practice codes'
branch *distances* are usually identical across revisions (only the absolute
addresses differ), the encoded branch word is the same constant in all three
columns — e.g. "Enable Exit Area Everywhere" is `b +0xC` = `0x4800000C`
everywhere; only the address to write it at changes.

## Prefer reimplementing the behaviour in C

**The mechanisms below reproduce a gecko code's *bytes*; that is a convenience
for simple codes, not the goal.** Replaying someone else's compiled
instructions is opaque, hard to review, and has to be re-derived per revision.
Once a code is more than a couple of writes, the better port is to understand
what it does and write the equivalent **C in the mod**, against the decomp's
types and the mod's own hooks.

Rule of thumb:

- A handful of constant writes → a table row (Class A/C). Cheap and clear.
- Anything with control flow, state, or ordering → **reimplement in C**. The
  two "stateful" codes (Stage Intro Skip, No Shine Get Animation)
  are done this way and are a fraction of the size of their gecko originals.

Reimplementing needs the *semantics*, which come from `../../src/sms`, not from
staring at hex. Resolve the code's addresses to functions via
`maps/<vers>.map`, read those functions in the decomp, and then express the
intent. See "Reimplemented in C" below for three worked examples.

### Never infer behaviour from a name

Every bug in the first pass of the stateful codes came from trusting a label:

- `APP_STATE_MENU` is the **debug** menu, not the title screen.
- `APP_STATE_TITLE` builds the **file-select** screen, not the title screen.
  (The title screen is `APP_STATE_GAMEPLAY` running `AREA_OPTION` as a stage.)
- "No Shine Get **Animation**" does not remove the animation — the animation is
  the point; what it removes is banking the shine and ending the run.

Names in this codebase come from three different reverse-engineering efforts
(susamune's `Context`, the decomp's `APP_STATE_*`, and the upstream code
titles) and none of them agree. Confirm against the function that actually
does the work — for a state machine, the transition function; for a feature,
the code's `<description>` plus the decomp. If a port looks far simpler than
the gecko original, that is evidence the behaviour has been misread, not that
the original is overbuilt.

## Implementation classes

1. **Class A — static memory / instruction writes** (`04`/`02`/`00`/`C6`, and
   `06` data writes to a fixed datum). The code just overwrites game memory
   with a constant. Handled by the `kFeatures` patch table in `features.cpp`.
2. **Class B — asm injections** (`C2`/`C0`, and `06` code-caves that are jumped
   into). The logic lives in asm that runs with a hooked function's register
   state. Implemented via `kAsmHooks` in `features.cpp` (reproduce the asm
   verbatim in a mod cave; toggle a branch at the site — see "Class B: asm
   hooks" below). Use this only when the asm is short and opaque; prefer C.
3. **Bind-driven actions** — codes that *do* something when a button
   combination is pressed rather than toggling state. The behaviour is written
   as C in `actions.cpp`; the combo comes from a configurable bind
   (`binds.*`) instead of the fixed one the gecko hardcodes. See "Bind-driven
   actions" below.
4. **Class C — multi-state options** carved out of a big multi-feature code
   (DPad Functions, Nozzle Lock). These use the gecko VM to store a mode byte
   and apply different writes per mode. Implemented as `kChoiceFeatures` /
   plain patches in `features.cpp` (Nozzle Lock, FLUDD-in-secrets, Fast Text) —
   see "Class C: multi-state options" below.
5. **Reimplemented in C** — no gecko bytes at all; the behaviour is written as
   mod logic driven by a setting. See "Reimplemented in C" below.

## Class A: the `features.cpp` mechanism

`src/features.cpp` holds a table of **features**. Each feature is a `SettingId`
plus a list of **patches**; a patch is a masked write to one 32-bit word:

```c
struct Patch { u32 addr; u32 mask; u32 value; };   // want = (orig & ~mask) | value
```

- `mask = 0xFFFFFFFF` → replace the whole word (a `04` / `C6` / instruction).
- `mask = 0xFFFF0000` → replace only the high half-word (a `02` at a
  word-aligned address, e.g. flipping a conditional branch to unconditional).

`featuresApply()` runs **every frame** from `onUpdate` (mirroring how the gecko
code handler re-applies each frame):

- The **original** word is captured live from the game the first time a patch
  is touched, and is what "Off" restores. This means we never hardcode the
  retail instruction per revision — turning a toggle off always writes back
  exactly what the game shipped, whatever region we're on.
- It early-outs on any patch whose target already holds the desired word, so a
  steady state costs only reads. When a word does change we `DCFlushRange` +
  `ICInvalidateRange` the 4 bytes so instruction patches take effect.

This pass reaches back further than "gameplay" — `proc()` runs `gameLoop()` for
the logo and title app states as well — but not further back than
`director->direct()`, which `onUpdate` calls *before* `featuresApply()`. A
feature whose sites are already directing by then needs the patch installed
sooner. `FEAT_EARLY(...)` marks such a row and `featuresApplyEarly()` — called
from `onAppInit`, the last point before `TApplication::proc()` starts the
app-state machine — writes **only** those rows. Intro Skip is the one today,
and on the per-frame pass alone the logos still play.

Early rows are **write-once**: `featuresApply()` skips them, they hold no slot
in the captured-original / installed-state arrays, and nothing ever restores
them. That is not a shortcut — restoring is meaningless for a site that has
already run by the time the menu can be opened, and a reboot reloads the game's
own code from disc anyway. So turning such a setting off applies at the next
boot, not immediately. Do not widen `FEAT_EARLY` to the whole table: rows that
patch heap words (Fast Text) would capture their "original" out of heap the
game has not filled in yet, and write that garbage back when toggled off.

Because capture is lazy-on-first-apply and `featuresApply()` is the only writer
of these addresses, the first read is guaranteed to be the retail value. All
target addresses are in the main DOL's `.text`/`.data`, resident from boot, so
they're valid to read/write as soon as the game loop is running.

### Adding a Class-A code

1. **Find the code** in `Codes.xml`; read its `<description>` so you know the
   intended effect (and confirm it against the decomp if the asm is doing
   something subtle).
2. **Transcribe the three source columns** (`GMSJ01`, `GMSE01`, `GMSP01`).
   Ignore `GMSJ0A`. For each gecko line work out `(addr, mask, value)`:
   - `04AAAAAA V` → `FWORD(0x80<jp>, 0x80<us>, 0x80<pal>, 0xV)`
   - `02AAAAAA 0000V` at a word-aligned addr → `FMASK(…, 0xFFFF0000, 0xV0000)`
   - `C6AAAAAA T` → `FWORD(…, encodeBranch(addr,target))` (same constant across
     regions when the distances match — verify).
3. **Add a `Patch[]` table** for the code, commenting the raw gecko lines and
   what each patched word is (opcode mnemonic helps future readers).
4. **Add a `SettingId`** by appending a row to `SUSAMUNE_SETTING_LIST`
   (`settings_list.h` — the list is shared with the launcher and is
   **append-only**, since its order is the persisted layout) and a row
   in `kSettingDescs` (`settings.cpp`) — `name`, `SETTING_BOOL`, default `0`
   (Off), and the `SETTING_CAT_*` for the tab it belongs in.
5. **Add a `FEAT(SETTING_…, kYourTable)`** line to `kFeatures`.
6. Build for all three regions and sanity-check the effect in-game.

`kMaxPatches` (currently 64) bounds the flattened patch-state array; bump it if
the table outgrows it (there's a runtime guard, not a hard crash).

### Menu wiring

Settings render generically. `SettingDesc` carries a `SettingCategory`; the
menu builds one `CategorySettingsTab` per category (`menu.cpp`, wired in
`Menu::Menu()`), each of which filters `kSettingDescs` by its category and
renders/edits the matching rows (scrolling if they overflow). So a new toggle
in an existing category needs **no menu change** — just the settings row. A new
category means a new enum value + one `mTabs[…] = new (buf) CategorySettingsTab(
"Title", SETTING_CAT_…)` line and a static buffer next to the others.

Current tabs: `QoL`, `Cosmetic`, `Misc`, `Savestate`, `UI`, `Timer`,
`QF Freeze`, `Binds`, plus the `Warps` and `Stages` debug tabs when
`ENABLE_DEBUG_WARPS` is enabled.

## Class B: asm hooks (`C2` codes)

A `C2AAAAAA NNNNNNNN` code inserts `N` lines of asm at site `0x80AAAAAA`. The
code handler (`launcher/codehandler/codehandler.s`, `_hook1`) does exactly two
writes:

1. at the site, `b site -> asm_block` (over the original instruction), and
2. it **overwrites the last word of the asm block** — the trailing `00000000`
   placeholder every `C2` ends with — with `b -> site+4`.

So the asm as authored already contains any displaced original instruction and
whatever register handling it needs; the handler just runs it and returns to
`site+4`. **We don't need to understand the asm** — we reproduce it byte-for-byte.

`kAsmHooks` in `features.cpp` does this toggleably:

- Each hook has a `site` and a mutable **cave** — one `u32[]` authored with the
  asm words verbatim, *including* the trailing `00000000` placeholder. It's an
  initialized (`.data`) array, so it loads pre-populated and icache-coherent
  with the blob; there is no separate const source and no copy. On first apply
  we patch its last word in place to `b -> site+4` (`branchWord()` uses the
  handler's exact encoding), then flush the cave. `inited` guards this so it
  runs once.
- Enabling writes `b site -> cave` at the site (`onWord`); disabling restores
  the captured original word. Same capture / early-out / `DCFlushRange` +
  `ICInvalidateRange` discipline as Class A (via `writeCode()`).
- The mod is linked in MEM1 within ±32 MB of game code, so both branches are
  reachable.
- **Region-specific asm:** where a `C2`'s asm embeds an address (an sda
  `r13`-relative load, or a `lis`/`ori` pair), build those words with
  `SUSAMUNE_MEM1_ADDR(...)` so one array serves all three revisions — see
  `kFreePauseAsm` (lis/ori rebuilt from the address) and `kDeathlessAsm`
  (per-region words selected directly).
- **Companion static writes:** several of these gecko codes are a `C2` *plus*
  some plain `04`/`C6` lines. Those go in `kFeatures` under the *same*
  `SettingId`, so the whole feature toggles together (e.g. `kNeverPauseIgt`,
  `kForcePlaza`, `kFreePauseBranch`).

### Adding a Class-B (`C2`) code

1. Add a `u32 gCaveFoo[]` (mutable, initialized) with the asm words **including**
   the trailing `00000000` placeholder. Bake any region-specific words via
   `SUSAMUNE_MEM1_ADDR`.
2. Add a `HOOK(SETTING_FOO, <jp>, <us>, <pal> site, gCaveFoo)` row.
3. If the code has non-`C2` lines, add them as a `kFeatures` entry under the
   same `SETTING_FOO`.
4. Add the `SettingId` + `kSettingDescs` row + build all three regions.

## Class C: multi-state options

Some gecko codes bundle several features behind a runtime **mode byte**: the
code reads controller input, ORs mode bits into a scratch address, and each
frame applies a different set of writes per mode bit via gecko conditionals
(`28…`/`E0…`). We don't reproduce the state machine — we expose each wanted
sub-feature as its own setting and apply its word set directly.

`kChoiceFeatures` in `features.cpp` handles `SETTING_CHOICE` sub-features:

- Each choice-feature maps a `SettingId` to `ChoicePatch{addr, mask, vals}`
  rows. By convention **choice 0 is the game default and restores the captured
  original**; choice `c ≥ 1` writes `vals[c-1]`. So the setting's choice-0 label
  must be the normal-behaviour state.
- Nozzle Lock (4-state: Unlocked/Rocket/Turbo/Hover) is one site forced to
  `li r31,<id>`. FLUDD-in-secrets (3-state: Completed/No FLUDD/All secrets) is
  two `TRedCoinSwitch::load` sites.
- A sub-feature that is just on/off (Fast Text) is a plain BOOL in `kFeatures` —
  On writes the forced words, Off restores the original.

### ⚠️ Identify DPad sub-features by address, not by condition order

The DPad Functions code's mode-bit order does **not** line up with the feature
list in its description, and several sub-features touch unrelated-looking
addresses. **Resolve every site address in `maps/<vers>.map` before deciding
what a block does.** For DPad Functions that showed:

- the `8A`/`8C` gecko-register blocks load `gpMarioPos` / `gpCamera` → the
  **position save/load** feature (belongs with savestates, not a toggle);
- the `0x004`/`0x008` block writes into `Talk2D2` / `EventWatcher` → **Fast
  Text** (dialog), *not* FLUDD as the condition order first suggested;
- the `0x401`/`0x402`/`0x404` block writes into `TRedCoinSwitch::load` → the
  actual **FLUDD-in-secrets** 3-state.

Getting this from the raw gecko alone is a trap; the map is authoritative.

## Reimplemented in C

Two codes are ported as mod logic rather than as gecko bytes. Each is worth
reading as a template for the next one.

> **Intro Skip is a Class-A patch (`kIntroSkip`), not a reimplementation.** An
> earlier hand-written port was wrong in a way worth remembering: it repointed
> `TApplication::proc`'s app-state jump table for state 4 at the gameplay case.
> State 4 is `APP_STATE_DONE`, not "the intro movie" — boot merely *reaches*
> the intro through it, because that case sets `mNextArea` to `AREA_OPTION` and
> falls through into the `APP_STATE_MOVIE` body. `proc()` enters the same state
> whenever the app is sent back to the title, which is what the console reset
> button does (`isSomethingPushed()` -> `nextState = APP_STATE_DONE`), so the
> redirect turned every reset into a reload of the current stage. The upstream
> code avoids that by rewriting the case body rather than the dispatch: it sets
> `mCurrArea` to `AREA_OPTION` itself and only then branches into the gameplay
> body, so every path through `APP_STATE_DONE` — reset included — has a
> destination. Anything that patches a shared state-machine entry for a one-off
> boot transition has this problem; check what else reaches the state, and what
> state it leaves behind, before you patch it.

### Stage Intro Skip (`features.cpp`)

*Skip the per-stage intro cutscene.* It is a **skip, not a fast-forward** — the
whole intro is run out inside a single frame.

`TMarDirector::direct` spends a tick budget in a loop, ending it by setting
`mGameState |= 0x4000` (which also gates the draw pass). Two hooks:

- **`direct+0x158`**, on that very store. While `mCurState == 1` and the
  intro-text state `mConsole->unk94->unk2BC < 3`, it refills the budget
  (`r3 + 15`, r3 being the pre-decrement budget), resets the tick counter, and
  **branches past the store** — so the loop keeps running game logic, without
  rendering, until the intro state advances. At `unk2BC == 3` it zeroes
  `mFader->unk18` instead; otherwise the original store runs.
- **`changeState+0x1CC`**, on the `andi.` that tests the skip buttons. It
  `crandc`s the result so that when `unk2BC == 0` the code takes its "player
  pressed skip" path unprompted.

`unk2BC`'s offset differs per revision (JP `0x2BC`, US `0x2B8`, **PAL `0x8DC`**),
as does the fader load, so those words come from the per-region upstream
sources.

> Do **not** confuse this with the separate *Fast Forward* code, which scales
> `direct()`'s `600` literal (2 ticks/frame stock → 8 at 4x, 16 at 8x) and
> applies to the whole game. An earlier version of this feature implemented that
> instead, which merely sped the intro up.

### No Shine Get Animation (`features.cpp`)

*Play the shine grab in full, then stop dead.* Mario should do the spin and fall
to the ground — so the landing can be timed — and from the moment he lands be
back under player control, with no collected-shine animation trailing him and
nothing banked.

Five hooks; four are asm, the fifth is `featuresOnStageLoad()`:

| site | original | replaced with |
|---|---|---|
| `winDemo+0x88` | `gpMarDirector->fireGetStar(shine)` | remember `director->unk58` (frame) |
| `winDemo+0xA4` | `shine->receiveMessage(mario, TAKE)` | `shine->unk64 &= ~1` |
| `winDemo+0xAC` | `mario->mSubState = 1` | `mState = STATE_IDLE; mSubState = 0` |
| `TShine::touchPlayer` entry | prologue | return unless >4 frames since last touch |
| `setupObjects` | — | clear the remembered frame |

Each does one necessary thing, and dropping any of them shows:

- `fireGetStar` is what banks the shine and ends the run.
- **`receiveMessage(TAKE)` is what makes the collected shine animate and follow
  Mario** — leaving it in place is why an earlier version had a shine bobbing
  over his head afterwards. Clearing bit 0 of the shine's flags instead also
  restores its collision so it can be grabbed again.
- `+0xAC` keeps Mario out of winDemo's second phase, which would otherwise
  re-assert the Shine Get animation and stop his process every frame.
- The debounce is required *because* collision comes back: without it Mario
  re-enters the grab on the next frame while still standing in the shine.

The touch timestamp sits at a fixed scratch address
(`SUSAMUNE_ADDR_SHINE_TOUCH_FRAME`), as upstream does. A cave has no way to
find a mod global, so the alternative is patching the address into every
`lis`/`ori` that references it at init -- more machinery than a fixed word in
the scratch region is worth. See `addresses.hxx` for what else lives there.

### Quarterframe Timer + Section Timer (`qftimer.cpp`)

*The IL timer.* Ported from upstream `qft` 1.5 and `qfst` 0.1. It is the most
literal port in the tree — runners submit these numbers, so the state words, the
arithmetic and the hook sites are the originals'.

Both timers count `TMarDirector::unk5C`, the director's logic-tick counter,
which advances 120 times a second. The five state words are upstream's
`0x817F00B2..BF`, one for one, and now live in the pinned block described below.
`unk5C` is *not* `unk58` (the paused-aware frame counter) and not the `& 3`
quarterframe field the Customized Display reads.

**`doc/qft_correspondence.md` maps every line of both gecko codes onto the part
of `qftimer.cpp` that handles it, and lists every deliberate divergence.** Read
that before changing anything here.

**Two of upstream's hooks are not injections here**, because the mod already
owns an indistinguishable point in the frame:

| upstream site | replaced by |
|---|---|
| `direct+0x88` (the `unk260 = 1` store) | `qfTimerOnStageLoad()`, called from `onSetup` — which *is* the `bl setupObjects` two instructions earlier; the decomp shows the pair is straight-line |
| `TApplication::proc+0x34` (section-timer reset) | the same call. Upstream also resets when the file-select director is built, but nothing draws the section timer between there and the next stage load |

**Everything else is a real injection at upstream's own address**, including the
two that an earlier cut of this port got wrong:

- **The draw hook, at the tail of `TGCConsole2::perform`.** Rendering from
  `afterDraw` instead was the single worst mistake in the first version. That
  function is the in-game HUD, so hooking it is what hides both timers on the
  title screen, the file select and the black frames of a transition — for free,
  with nothing having to ask. It is also the console's own 2D space, so the
  upstream coordinates land where upstream puts them, and it samples `unk5C` at
  the same point in the frame, so the digits agree tick for tick.
- **`~TMarDirector`, which banks the outgoing stage's ticks.** Caching `unk5C`
  every frame and folding it in at the next load is *not* the same: the director
  keeps ticking between the last drawn frame and its destructor, and the
  difference lands in the banked total of a multi-area run.

**Eighteen sites.** Eight are trailing `blr`s of a trigger function
(`TCoin::taken`, `TCoinRed::taken`, `TItem::taken`, `TTalk2D2::openTalkWindow`,
`fireStartDemoCamera`, `TBaseNPC::emitHappyEffect_`, `TBathtub::quake`,
`TYoshi::ride`), so one `b` per site into a single seven-word cave that ends in
`blr` covers all of them — upstream's own trick, and what makes per-trigger
toggling cost one word each. Three more triggers need their own cave
(`TCoinBlue::taken+0x24`, which is at the *head* of its function and rounds the
quarterframe up; `TMario::taking+0x98` and `TMario::dropObject+0x38`, which are
three-word `bl`s into the shared freezer). The seven status-based triggers
(put down / triple jump / spin jump / ledge grab / wall kick / bounce / rope
jump) share one site at `changePlayerStatus+0x194` whose cave calls into C —
upstream open-codes one gecko comparison chain per enabled trigger, but a C
dispatch is smaller and lets the set change without rebuilding. (The cave spills
`r4` around the call because a C callee may clobber every volatile; the site
disassembles to show `r4` is the only one live across it.) The remaining seven
are the state machine itself: `fireGetStar+0x48` and `TBathtub::startDemo+0x21C`
stop the timer for good; `changeState+0x328` (quit to file select),
`TCardLoad::changeScene+0x1278` (load a file) and
`nextStateInitialize+0x56C` (die / restart) all arm a restart;
`setNextStage+0x50` deliberately does *not*, which is what carries the clock
across a pipe or a secret course; and `TGCConsole2::perform` draws.

> **The two restart hooks were missing from the first cut**, which is why the
> timer only ever restarted via quit-to-file-select. Both take the flag byte
> from whatever register the displaced original happens to leave — `r28 + 1` at
> one, a post-call `r5` at the other — and the port reproduces that rather than
> substituting a tidy `1`.

**Displaced originals are captured from the site, not hardcoded.** Each cave
that overwrites a real instruction has a placeholder slot that `initCaves()`
fills from the site word, so no cave depends on knowing what a revision encodes
there. Every one of them is a store or an immediate load, hence safe to run from
a different address.

> **Two upstream bugs are not reproduced.** Its `C2` blocks at
> `changeState+0x328` and `setNextStage+0x50` discard the instruction they
> displace. Both are the zero-init of a field in a stack `TGameSequence` that is
> then passed on by pointer, so the game goes on to read a garbage scenario byte
> / flag halfword. Upstream gets away with it because those paths are rare;
> preserving the instruction changes nothing about the timer.

**Appearance is not a setting.** Position, size, colours, gradient, the resolved
background rectangle and the freeze duration live in a config block pinned at
the mod's link base (`include/susamune/gui_config.hxx`, section `.guicfg`, placed by
`link_mod.py`'s `--section-start`). The mod ships the upstream generator's
defaults there; the web configurator (`site/`) emits a Gecko code that overwrites
the fields the user changed. Pinning is the whole point — a published code has
to keep working across mod rebuilds. `site/FORMAT.md` is the contract for those
offsets. The same block also carries the timer's runtime state, because a cave
has no way to find a mod global and reaches it by absolute address; the state is
placed *first* so the config can grow without moving. The background rectangle
is **resolved by the configurator**, as it is in the upstream codes -- that
keeps font metrics out of the mod's draw path entirely.

**Both timers render through the game's own code, not through `Menu`.** The
draw hook runs inside `TGCConsole2::perform`, in the console's 2D space, so the
`J2DOrthoGraph` `afterDraw` sets up does not exist yet — and the appearance has
to match upstream's exactly anyway. So `drawBg` calls ScrnFader.cpp's file-local
`fill_rect` by address (it is `static`, so `map_to_ld.py` never sees it), and
`drawLine` is a transcription of the `drawText` dependency blob: a `J2DPrint`
built in place on `gpSystemFont`, the font size poked into `mFontSizeX/Y` after
construction, `initiate()` deliberately not called, and
`J2DPrint::print(x, y, 0xFF, fmt, ...)` — which is `locate()` plus
`print_alpha_va`, the same two calls the blob makes. Going through
`Menu::drawTextBaseline` (i.e. `J2DTextBox::draw`) instead gets a different font
object, a position matrix, `setSomeColors` and `initiate()`, and the two timers
visibly disagree.

> **`J2DGrafContext::setup2D` between the two elements is not tidy-up.**
> `JUTResFont::drawChar_scale` ends with
> `GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_CLR_RGBA, GX_F32, 0)`, so any text
> draw leaves `VTXFMT0`'s position format as `f32`. `fill_rect` then writes
> `GXPosition3s16` — six bytes where the GP expects twelve — and the FIFO
> desyncs: Dolphin pops `GFX FIFO: Unknown Opcode` and the game hangs. Upstream
> has the call because its two hooks are separate `C2` blocks with a `setup2D`
> at the end of the first; folding them into one C function drops it, and the
> section timer's background is the first thing to fall over. This is the same
> hazard `Menu::fillBox` documents, one layer down.

**On size.** The gct of both codes with every trigger enabled is 1536 bytes, and
that is the yardstick a port has to answer to: the mod region is finite and
there are many codes left to bring over. `qftimer.cpp` is 2561 bytes, of which
860 is the install table and the apply/install loops -- the per-trigger toggling
a static `.gct` does not need. The remaining 1701 is the like-for-like part, and
its 336 bytes of caves are less asm than the gct carries. Write these ports as
transcriptions: a generic hook table with per-row capability flags cost twice
what one mutable `Site` row and two straight loops do.

And **use the game's own code**. `strcpy`/`strncpy`/`strcat`/`strlen`/`memcpy`/
`memset`/`sprintf`/`snprintf`/`vsnprintf` are all resolved by `map_to_ld.py` and
cost the blob nothing. A hand-rolled unsigned formatter and bounded string copy
lived in a `util.cpp` for a while on the theory that they beat varargs; deleting
them for `snprintf` took **256 bytes off the blob**, because one call site
replaces several and the callee is free.

## Bind-driven actions (`binds.*` + `actions.cpp`)

Several codes are not toggles: they act when a button combination is pressed.
Upstream hardcodes the combination; susamune makes it configurable.

### The bind subsystem

`binds.*` is a deliberate mirror of `settings.*`, so the two read the same way:

- one row in `SUSAMUNE_BIND_LIST` (`include/susamune/binds_list.h`) —
  enumerator + stable ini key, shared with the ARM kernel, **append-only**
  because the order is the persisted layout;
- one row in `kBindDescs` (`binds.cpp`) — menu label + default combo;
- one `u16` of live value in `gBinds`.

A bind is a mask of at most four GameCube button bits, drawn from
`SUSAMUNE_BIND_BUTTON_LIST` (A/B/X/Y/L/R/Z/Start + the four d-pad directions —
`0x1F7F`).

Matching is **exact equality** over those bits: the buttons held must be the
bind's and nothing else. That is what almost every gecko code does, through the
gecko VM (`28400D50 0000VVVV` with a zero mask — Fast Forward, DPad Functions,
Mario/Coin Count Savestate) or a plain `cmplwi` (Instant Restart's `0x208`, the
Red Coin / QF Time / In-Game Time savestates' `1` and `2`). Two actions bound to
the same combo both fire; nothing arbitrates.

A few codes want the looser rule instead — Pattern Selector tests a bare
`andi. r0,r0,0x40`, and Spawn Yoshi (`rlwinm r0,r4,0,16,27`) and Manual Attempt
Counter (`andi. r0,r0,0xFFF0`) mask the d-pad nibble out before comparing. That
is what `isHeldSubset()` / `wasPressedSubset()` are for.

Input is read from `JUTGamePad::mPadStatus[0]` — the raw pad sample — not from
the game's `TMarioGamePad`. That is the same place the gecko codes read, and it
keeps binds alive while the game has the player's pad disabled (dialogue,
cutscenes), which is exactly when fast-forward is wanted.

`gBinds.update()` runs once per frame from `onUpdate`, **before**
`director->direct()` — `onUpdateGameMode` runs inside it and asks whether the
menu bind fired this frame, to swallow the pause the menu combo's Start would
otherwise trigger.

Every query reports false for an unbound bind, while the recorder is running,
and from then until the pad is released — a four-button combo commits while
still held, and would otherwise immediately fire what it had just been bound to.
Binds are **not** silenced while the menu is open, since the menu toggle is
itself a bind.

**Recording.** `A` on a row in the menu's Binds tab arms `gBinds`' recorder.
It waits for the pad to go idle (the `A` that started it is still down), then
accumulates held buttons and commits as soon as either a held button is
released or four are down. Only the C-stick is watched by the menu meanwhile —
every real button is a candidate for the combo — so the tab reports
`grabsInput()` and `Menu::update` hands it the pad exclusively, suppressing tab
switching and the `Y`+`Start` close combo. C-stick in any direction cancels;
C-stick left on an idle row clears a bind to "none".

**Persistence** rides on the settings handoff: `SusamuneCfg` gained
`binds[]` + `bindCount` and susamune.ini gained a `[binds]` section, written as
`+`-joined tokens (`regrab_object = X+DUp`) from the shared button list. A
launcher built before binds existed leaves `bindCount` zero — it memsets only
the part of the block it knows about — which reads as "nothing persisted, keep
the defaults", so no version bump was needed.

### Regrab Last Held Object (DPad Functions, mode bit `0x408`)

```
48000000 8040A398   ; pointer = gpMarioAddress
1400007C 00000383   ; *(u32*)(pointer + 0x7C) = 0x383
```

`TMario+0x7C` is `mState` and `0x383` is `MARIO_STATUS_TAKE`, so the whole code
is "force Mario into the pick-up action" — `mHeldObject` survives a throw, so
re-entering TAKE re-attaches whatever he last carried. One C assignment.

Upstream re-writes the word every frame the combo is held, pinning Mario in
TAKE for as long as you hold it; ours fires on the press edge, which is
identical for a tap (`mState` is dispatched on the following frame) without
that side effect.

### Spawn Yoshi

Two `C2` injections upstream.

The first, at `TMario::checkCollision+0x44`, paints the requested colour onto
`mario->mYoshi`, refills its juice, and branches into the middle of that
function's existing "Mario landed on Yoshi" path at `checkCollision+0x1C0` —
past the part that would teleport Mario onto the (unhatched, positionless)
Yoshi. What is left of that path is six lines, and `spawnYoshi()` in
`actions.cpp` just *is* those six lines, with no patch at all:

```c
mModelAngleY = mAngle.y;
if (hasFludd) { stash nozzle + water level for the dismount }
mYoshi->ride();
hasFludd = true;                        // Yoshi's juice runs through the FLUDD
mFludd->changeNozzle(Yoshi, true);
changePlayerStatus(MARIO_STATUS_WAIT, 0, false);
```

The colour ids come from the gecko's `rlwnm r0, 0x63000000, r4, 30, 31` lookup
and match `TYoshi::Color`: green 0, orange 1, purple 2, pink 3. The gate on
`mState & 0x1000` is `checkCollision`'s own early-out, kept so the action can't
fire during a cutscene.

The second injection, at `TEggYoshi::control+0x1C`, is the one thing a
per-frame hook cannot do: it runs **once per egg in the stage**, making the
level's egg vanish and remembering it on the Yoshi (`TYoshi::mEgg`) so the egg
is restored via `startFruit()` when the Yoshi expires. There is no global to
enumerate eggs from, so we keep an injection there — but as a **ten-word
trampoline into C**, not transcribed asm: save LR, `mr r3,r31`, `bl
susamuneOnEggYoshiControl`, restore, run the displaced original, branch back.
Two things make that trampoline safe and cheap:

- the site is the instruction *after* `bl TMapObjBase::control()`, so every
  volatile register (and LR, CR, f0-f13) is dead there and the C callee may
  clobber whatever it likes; `r31` is `this`.
- it is spliced in only while a spawn is armed (`gEggKillFrames`), so the game
  runs unpatched the rest of the time. The countdown exists because
  `TEggYoshi::control` runs inside `director->direct()`, i.e. *before* the next
  `actionsApply()`.

### Fast Forward

```
020ECDE2 00000258   ; default
28400D50 00000201   ; if buttons == B + DPad Left
020ECDE2 00000960
28400D51 00000202   ; else if buttons == B + DPad Right
020ECDE2 000012C0
```

The patched halfword is the immediate of `li r3, 600` at
`TMarDirector::direct+0x24` — the per-frame logic-tick budget the director
spends before it renders — so 2400/4800 run 4x/8x the game logic per rendered
frame. There is nothing to reimplement: this stays a masked halfword write,
applied while the bind is **held** and restored (from the captured original) on
release. Do not confuse it with Stage Intro Skip, which refills the same budget
mid-loop.

### Adding another bind-driven action

1. Append a row to `SUSAMUNE_BIND_LIST` (`binds_list.h`) and a matching row to
   `kBindDescs` (`binds.cpp`) with the default combo from the code's
   `<description>`. Binds also drive the menu toggle and savestate save/load,
   which are not gecko ports at all. The menu tab, the ini keys on both sides and the MEM2
   layout all follow automatically.
2. Write the behaviour in `actions.cpp` gated on `gBinds.wasPressed(...)` or
   `gBinds.isHeld(...)`, and call it from `actionsApply()`.

## Codes ported so far

- **Class A** — Infinite Lives, Unlock Nozzles, Unlock Yoshi, Any Fruit Opens
  Yoshi Eggs, Infinite Juice, Enable Exit Area Everywhere, FMV Skips, Intro
  Skip, Respawn One-Time Shines, Fruit Never Time Out, Fast Text (QoL); Mute
  Background Music, Shine Outfit, Shiny Shines, Shadow Mario HP Meter
  (Cosmetic).
- **Class B (`C2` hooks)** — Free Pause, Disable Blue Coin Flag, Deathless
  Blooper Surfing (QoL); Replace Episode Names (Cosmetic); Fast Piantissimo,
  Never Pause IGT, Force Plaza Events (Misc).
- **Class C (multi-state)** — FLUDD in secrets (QoL, 3-state); Nozzle Lock
  (Misc, 4-state).
- **Reimplemented in C** — Stage Intro Skip, No Shine Get Animation
  (Misc); Quarterframe Timer + Quarterframe Section Timer (Timer / QF Freeze,
  `qftimer.cpp`).
- **Bind-driven actions** — Regrab Last Held Object, Spawn Yoshi (4 colours),
  Fast Forward (4x / 8x). See "Bind-driven actions" above.

All toggles default Off / choice 0; binds default to the combinations their
gecko originals hardcode. See the per-feature comments in `features.cpp` /
`actions.cpp` for the exact gecko-line → patch/C mapping.

This completes both the "Simple On/Off Toggles or Select Options" and the
"Simple Actions/Binds" lists in `doc/gecko_codes.md`.

## Remaining

- **DPad leftovers (not toggles).** The DPad Functions code also carries
  *position save/load* (the `gpMarioPos`/`gpCamera` gecko-register blocks).
  Per `doc/gecko_codes.md` that belongs with the savestate / "Lite Savestate"
  work, so it is intentionally not ported here. (*Regrab last held object*,
  the other leftover, is now a bind — see above.)
- **Gecko-relocation caveat.** The launcher relocates some absolute
  heap-address gecko codes past the mod's arena reservation
  (`PatchSusamuneGeckoCodes`, see AGENTS.md). A **port carries the same
  problem**: most ports are fine (they patch `.text`/`.data` or read live
  pointers), but a patch row that targets a hardcoded heap address must add
  `SUSAMUNE_ARENA_RESERVE_SIZE` itself — that is what `FHEAP`/`FHEAPMASK` in
  `features.cpp` are for. Fast Text is the case in point: its three
  instruction patches are plain `FWORD`s, but the `!!!` text it forces is
  planted directly in the loaded message buffer, so those rows are `FHEAP`s.
  PAL loads one message file per language at a different address, so its row
  is resolved at runtime from the language word (`resolveFastTextPalMsg`) —
  unresolved rows (`addr == 0`) are skipped by `applyPatches`.
