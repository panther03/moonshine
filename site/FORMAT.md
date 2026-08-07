# GUI appearance config — the Gecko-code contract

The configurator emits a **Gecko code**, not a file. The mod ships its own
appearance defaults in a config block pinned at a fixed address; a user's layout
is a block write over the fields they changed.

This replaces the earlier `susamune_gui.bin` design. The reason is distribution:
runners already load a `.gct`, and adding a second file the launcher has to find
buys nothing that a few lines of Gecko do not. It also means a partial config is
free — anything the code does not write keeps the compiled-in default.

`include/susamune/gui_config.hxx` is the other half of this contract. Change one,
change the other.

## Where the block lives

The mod links into MEM1 at each revision's `__ArenaLo`, and `link_mod.py` pins the
`.guicfg` section at exactly that address (`--section-start`), ahead of all the
code. That is what makes the addresses below stable: the block does not move when
the mod is rebuilt, only when `arena_lo` or `patches.gui_block_size` changes.

| version  | `__ArenaLo` | config base (`+0x60`) |
|----------|-------------|------------------------|
| GMSJ01   | `0x80426020` | `0x80426080` |
| GMSE01   | `0x80429800` | `0x80429860` |
| GMSP01   | `0x80420D60` | `0x80420DC0` |

The `0x60` is the QF timer's private runtime state, which sits *before* the
config so the config can grow towards the end of the reservation without moving.
A Gecko code must never write into it.

**The code is therefore version-specific**, unlike the old blob. The version
selector in the configurator picks both the preview font metrics and these
addresses.

## Layout

All offsets are from the config base. Every field is big-endian.

| off  | type  | field              | notes |
|------|-------|--------------------|-------|
| 0x00 | `TextStyle` | `qft`        | Quarterframe Timer |
| 0x1C | `u16` | `qftFreezeFrames`  | how long the timer holds after a trigger, in frames. 0 disables freezing |
| 0x1E | `u16` | pad                | |
| 0x20 | `TextStyle` | `qfst`       | Quarterframe Section Timer |
| 0x3C | | *(unallocated)*        | see "Not implemented yet" |

### `TextStyle` — 28 bytes

| off  | type  | field        | notes |
|------|-------|--------------|-------|
| 0x00 | `s16` | `x`          | game 2D space, see "Coordinates" |
| 0x02 | `s16` | `y`          | text **baseline**, not the top edge |
| 0x04 | `s16` | `bgX0`       | background rect, already resolved |
| 0x06 | `s16` | `bgY0`       | |
| 0x08 | `s16` | `bgX1`       | |
| 0x0A | `s16` | `bgY1`       | |
| 0x0C | `u16` | `fontSize`   | 20 = the font's native size |
| 0x0E | `u16` | pad          | zero |
| 0x10 | `u32` | `fgTop`      | RGBA8888 |
| 0x14 | `u32` | `fgBottom`   | RGBA8888; equals `fgTop` when there is no gradient |
| 0x18 | `u32` | `bg`         | RGBA8888. Alpha 0 means "draw no background" |

**The rect is resolved here, not in the mod.** The configurator measures the
text with the game's own font metrics, which is what the upstream generator
does too, and the formula is upstream's `getFillRectParams` verbatim: the four
padding offsets the inspector edits are relative to the measured text box.
Resolving on this side keeps font metrics out of the mod's draw path entirely.

The text measured is the element's **placeholder**, not the live string
(`0:00.000` for the timer, its sixteen-line block for the section timer), so the
box does not twitch as the digits change and the section timer's box is always
sized for a full run's worth of splits.

## Coordinates

**One space for every element**: the game's 2D space, a 640 x 480 layout space
of which the framebuffer shows only the middle 448 rows. **The visible band is
therefore `y` 16..464, not 0..480** — which is why the timer's default baseline
is 456, and why the canvas here is 448 tall with `Y_ORIGIN = 16`. Horizontally
0..640 is the full width. The game's own 2D screens are authored the same way
(GCConsole2 parks a pane "below screen" at `y1 = 465`), and so are the upstream
codes' configs.

`y` is the text **baseline**, not the top edge.

Measured, not inferred: the mod has an `ENABLE_DRAW_CALIBRATION` build option
that overlays a labelled ruler in its own draw space. Two plausible-sounding
derivations from the decomp's render-mode setup and from this page's own
`Y_ORIGIN` both gave the wrong answer before anyone ran it.

## What the code looks like

One `06` block write per configured element, covering only that element's fields:

```
06 <addr & 0x1FFFFFF> <byte count>
<payload, zero-padded to a multiple of 8>
```

The Quarterframe Timer writes 32 bytes at config `+0x00` (its `TextStyle` plus
`qftFreezeFrames`); the Section Timer writes 28 bytes at config `+0x20`. An
element that is not on the canvas is simply not written, and **a layout with
neither of them emits no code at all** — an empty code would only take up room in
the `.gct`.

## Not implemented yet

The configurator still lets you place a Customized Display, Attempt Counter,
Pattern Selector and Controller Input Display, and remembers them, but

> **TODO:** the mod does not render those four yet and no config block is
> reserved for them. They are dropped from the emitted code, with a note in the
> export dialog. When they land they append at config `+0x3C`, which moves
> nothing that already exists — which is the whole point of putting the config
> last in the block.

The Customized Display's design still holds when it gets there: a cell stores a
ready-made printf format string plus an ordered field-id list, because the game
links the full MSL formatter (`vsnprintf` / `__pformatter`, declared in
`include/Dolphin/printf.h`), so rendering a cell costs a hand-built PPC EABI
`va_list` and nothing more.
