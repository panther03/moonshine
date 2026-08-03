# `susamune_gui.bin` — GUI layout blob

The configurator emits one binary file describing **what UI elements to draw and
where**. It is not a patch and contains no PPC code: the mod renders every
element itself through `Menu::drawText` / `fillBox` / `fillPoly`.

Both the ARM kernel and the PPC are big-endian, so every field below is
big-endian and the mod can cast straight into the buffer with no swapping.

The file is **region-independent**. The region selector in the configurator only
picks which font sheet the preview measures with; nothing region-specific is
serialised.

## Loading

Same path as `mod_<region>.bin`: the Nintendont loader reads the file from
`launch_dir` into a MEM2 window and the mod walks it once at startup. Nothing in
`susamune.ini` or `struct SusamuneCfg` changes.

## Coordinates

**One space for every element**: the game's 2D ortho space, with the visible
area spanning `x` 0..600 and `y` 16..464. That is the space the upstream
generator's own inputs declare (`x min 0 max 600`, `y min 16 max 464`) and the
one its text path uses unmodified.

This deliberately drops two quirks in the upstream *controller* code, which
stores `y - 16` and whose preview scales `x` by 0.9375 as if it were in a
640-wide space. Since the mod reimplements the renderer rather than feeding the
old blob, there is no reason to carry a second convention: a configurator where
dragging two elements to the same spot puts them in the same spot is worth more
than byte compatibility with a code we no longer emit. The mod applies whatever
fixed offset its own draw path needs, once, for all element types.

## Layout

All offsets are from the start of the structure. Every element is padded so its
size is a multiple of 4; the file as a whole is therefore 4-byte aligned
throughout and `u16`/`u32` reads are always naturally aligned.

### Header — 16 bytes

| off | type  | field     | notes                                       |
|-----|-------|-----------|---------------------------------------------|
| 0x0 | `u32` | `magic`   | `'SGUI'` = `0x53475549`                     |
| 0x4 | `u16` | `version` | currently `1`                               |
| 0x6 | `u16` | `count`   | number of elements that follow               |
| 0x8 | `u32` | `size`    | total file size, header included             |
| 0xC | `u32` | `crc32`   | CRC-32 (IEEE) of every byte after this field |

A reader rejects the file unless `magic` matches, `version` is understood, and
`size` equals the actual byte count.

### Element header — 4 bytes

| off | type  | field   | notes                                        |
|-----|-------|---------|----------------------------------------------|
| 0x0 | `u8`  | `type`  | see the element table below                  |
| 0x1 | `u8`  | `flags` | bit 0 = enabled. Other bits reserved, zero   |
| 0x2 | `u16` | `size`  | whole element incl. this header, multiple of 4 |

`size` is what makes this TLV: a reader that does not know a `type` skips
`size` bytes and carries on, so a newer configurator never breaks an older mod.

Only enabled elements are written, so `flags` bit 0 is always 1 today. It exists
so a future "keep the config but hide it" state costs no format change.

### `TextStyle` — 24 bytes

Shared by element types 0x01–0x05. This is the same set of knobs the upstream
generator calls `TextConfig`.

| off | type  | field        | notes                                          |
|-----|-------|--------------|------------------------------------------------|
| 0x00| `s16` | `x`          | game 2D space, see "Coordinates" below          |
| 0x02| `s16` | `y`          | text **baseline**, not the top edge             |
| 0x04| `u8`  | `fontSize`   | 20 = the font's native size                     |
| 0x05| `u8`  | `styleFlags` | bit 0 = vertical gradient. Other bits zero      |
| 0x06| `s8`  | `bgLeft`     | background padding, outward from the text box   |
| 0x07| `s8`  | `bgRight`    |                                                 |
| 0x08| `s8`  | `bgTop`      |                                                 |
| 0x09| `s8`  | `bgBottom`   |                                                 |
| 0x0A| `u16` | `reserved`   | zero                                            |
| 0x0C| `u32` | `fgTop`      | RGBA8888                                        |
| 0x10| `u32` | `fgBottom`   | RGBA8888; equals `fgTop` when bit 0 is clear    |
| 0x14| `u32` | `bg`         | RGBA8888. Alpha 0 means "draw no background"    |

**The mod measures the text and derives the background rect itself.** The
upstream gecko codes could not do that -- they had no font metrics at patch time
-- so they baked a resolved `x0,y0,x1,y1` rectangle into the blob. Storing the
four `s8` offsets instead is both smaller and correct when the rendered string
is a different width than the preview's placeholder.

## Elements

| type | element                    | multiplicity | body                                |
|------|----------------------------|--------------|-------------------------------------|
| 0x01 | Customized Display cell    | many         | `TextStyle` + format block          |
| 0x02 | Quarterframe Timer         | one          | `TextStyle` + `u16 freezeDuration` + `u16` pad |
| 0x03 | Quarterframe Section Timer | one          | `TextStyle`                         |
| 0x04 | Attempt Counter            | one          | `TextStyle` + `u16 duration` + `u16` pad |
| 0x05 | Pattern Selector           | one          | `TextStyle`                         |
| 0x06 | Controller Input Display   | one          | 12 bytes, see below                 |

Durations are in frames.

Types 0x02–0x05 draw a string the mod composes itself, so only the styling is
configurable. The pattern table behind Pattern Selector, and the button/stick
geometry behind the controller display, stay baked into the mod exactly as they
are baked into the upstream codes.

### 0x01 — Customized Display cell

`TextStyle`, then:

| off | type   | field        | notes                                   |
|-----|--------|--------------|-----------------------------------------|
| 0x18| `u8`   | `fieldCount` | number of format arguments               |
| 0x19| `u8`   | `fmtLen`     | bytes of `fmt`, NUL included             |
| 0x1A| `u8[]` | `fields`     | `fieldCount` field ids, in argument order|
| ... | `char[]`| `fmt`       | `fmtLen` bytes, NUL-terminated           |
| ... |        | padding      | zeroes, up to a multiple of 4            |

`fmt` is a **ready-to-use printf format string** and `fields` names the arguments
it consumes, in order. The configurator does all the parsing: its `<x|.0|39.39>`
markup is resolved at generation time into `%.0f` plus field id 1, and literal
`%` in user text is escaped to `%%`.

This is why the format costs the mod almost nothing. The game links the full MSL
formatter -- `snprintf` / `vsnprintf` / `__pformatter`, already declared in
`include/Dolphin/printf.h` and resolved by `map_to_ld.py` -- so rendering a cell
is: walk `fields`, spill each value into a hand-built PPC EABI `va_list` (GPR
save area at +0, FPR save area at +0x20), call `vsnprintf`, draw the result.
There is no formatter to write and no format string to interpret.

`fmt` is encoded in the game's font codepage, so a byte is not necessarily
ASCII; `fmtLen` counts bytes, not characters. Newlines are literal `0x0A`.

### 0x06 — Controller Input Display

| off | type  | field       | notes                                        |
|-----|-------|-------------|----------------------------------------------|
| 0x00| `s16` | `x`         | top-left corner, same space as `TextStyle`   |
| 0x02| `s16` | `y`         |                                              |
| 0x04| `u16` | `height`    | 120 is native; width follows at 182/120 ratio |
| 0x06| `u8`  | `lineWidth` | stroke weight, in the same 1/6 units upstream uses |
| 0x07| `u8`  | reserved    | zero                                          |
| 0x08| `u32` | `bg`        | RGBA8888                                      |

#### Field ids

Ids are stable — append only, never renumber.

| id | name     | C type   | source                          | note                        |
|----|----------|----------|---------------------------------|-----------------------------|
| 1  | `x`      | `float`  | `gpMarioOriginal` + 0x10        |                             |
| 2  | `y`      | `float`  | `gpMarioOriginal` + 0x14        |                             |
| 3  | `z`      | `float`  | `gpMarioOriginal` + 0x18        |                             |
| 4  | `angle`  | `u16`    | `gpMarioOriginal` + 0x96        |                             |
| 5  | `hspd`   | `float`  | `gpMarioOriginal` + 0xB0        |                             |
| 6  | `vspd`   | `float`  | `gpMarioOriginal` + 0xA8        |                             |
| 7  | `qf`     | `u32`    | `gpMarDirector` + 0x58          | `& 3`                       |
| 8  | `cangle` | `u16`    | `gpCamera` + 0xA6               | `- 0x8000`                  |
| 9  | `invinc` | `u16`    | `gpMarioOriginal` + 0x14C       | `>> 2`, QF to frames        |
| 10 | `goop`   | `s32`    | `getPollutionDegree(gpPollution)` | call                      |
| 11 | `spin`   | `u32`    | `checkStickRotate(gpMarioOriginal)` | call; renders as a glyph |

`float` arguments are varargs-promoted to `double`, so they occupy an FPR save
slot; everything else takes a GPR slot.

## Sharing

The configurator also offers the identical bytes base64-encoded, for pasting
into chat. Decoding that text yields the file verbatim.
