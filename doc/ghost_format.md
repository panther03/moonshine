# Ghost file format

Status: V6 adds FLUDD observations. V3/V4/V5 remain readable with their original
byte semantics and 15-minute bounds; V1/V2 test ghosts remain unsupported.
Files without teaching data continue to export as V4.

## V6 FLUDD extension

V6 keeps the V5 pose/segment/split layout and requires feature bits 7. Its SGTI
section uses version 2, at most 36,000 inputs, and a 24-byte input stride: the
unchanged 16-byte controller record followed by `SusamuneGhostFluddSample`.
New recordings allow 71,928 QF (approximately ten minutes) and 17,983 poses.
Legacy files retain their own duration, pose, and input bounds. Re-export keeps
V6 observations; V5 inputs are never reinterpreted at the new stride.

The eight observation bytes are: mode (nozzle 0–5 in bits 0–2, present bit 3,
actually spraying bit 4), three signed emitter offsets relative to Mario in
two-unit steps, three bytes packing unsigned 12-bit yaw and signed 12-bit pitch,
and unsigned emission power. Angles use 4,096 steps per turn; pitch is bounded
to a quarter-turn in either direction. An absent observation is all zero;
non-firing observations have zero power. Unknown flags and invalid nozzles,
angles, section/file pairings, lengths or checksums are rejected before install.

Capture observes successful retail emission without changing its result or RNG.
Each frame retains one accepted emitter observation (the last jet for multi-jet
nozzles). Water particles are a bounded, visual recreation with ground splashes;
they do not reproduce every original droplet, wall collision or sound and never
interact with gameplay. Old ghosts have no invented FLUDD observations.

Both legacy 54,000 × 16 and V6 36,000 × 24 streams fit the same 864,000-byte
payload. All physical banks, mailbox extents and state capacity are unchanged.
Snapshot 17's existing third ghost-prefix span uses the track's checked stride.
Storage protocol 5 and its acknowledgement/ownership rules remain unchanged.

## V5 teaching extension

V5 retains the 256-byte V4 header, 2048-byte route table, attachment codec and
16-byte pose samples. Its version is 5 and `requiredFeatures` is 3. Immediately
after `sampleDataOffset + sampleDataSize`, it appends a big-endian `SGTI` section:

| Offset | Bytes | Value |
|---:|---:|---|
| 0 | 4 | `SGTI` magic |
| 4 | 2 | Section version 1 |
| 6 | 2 | Header size 32 |
| 8 | 4 | Input count, 0–54,000 |
| 12 | 4 | Split count, 0–6 |
| 16 | 4 | Flags: bit 0 means input capacity was reached |
| 20 | 4 | CRC-32 of input and split records |
| 24 | 8 | Reserved, zero |

Inputs follow the section header, then splits; there is no padding or trailing
data. Each 16-byte input is absolute QF (`u32`) followed by the 12-byte
`SusamunePracticeInput`: buttons (`u16`), main X/Y and C X/Y (`s8`), L/R and
analog A/B (`u8`), controller error (`s8`), and reserved flags (`u8`, zero).
Only defined pad-button bits are allowed. Inputs must increase strictly and lie
inside the pose track's QF bounds. They represent the controller sample consumed
by a gameplay update, not one input per pose or per internal simulation tick.
Playback holds the most recent recorded sample in its matching scene segment;
no input is invented before the first sample or after a truncated stream.

Each 12-byte split stores absolute QF (`u32`), registry schema hash (`u32`), route
ID (`u16`), endpoint ordinal (`u8`), and reserved zero (`u8`). Splits must share a
nonzero schema and route, have consecutive ordinals beginning at zero, and have
nondecreasing QF timestamps within the track. Equal timestamps are allowed.
Only endpoints already accepted by the existing split capture are recorded.
Ghost comparison requires the route, endpoint and schema to match. Validated
portable imports keep their recorded QF timestamps across JP, US and PAL, and
re-export preserves their split records. Legacy files supply no intermediate
ghost comparison values. Automatic last-attempt and last-success ghosts use
the same checkpoint comparison as a selected library target.

The main payload/file checksums also cover the complete appended section.
Maximum V5 size is 1,297,992 bytes: 433,888 bytes of V4 data, 32 bytes of section
header, 864,000 bytes of inputs and 72 bytes of splits. Parsing validates all
counts, bounds and checksums before installing runtime data. The ARM validator
processes at most 16 KiB per service pass and never splits a record across passes.

Storage mailbox protocol 5 retains the request/response cache lines at their
old addresses and keeps the protocol-4 payload bank at 1,310,720 bytes.
Two independent 917,504-byte banks hold recording and playback inputs. Watch 2
borrows the current recording bank, including after buffer promotion. The PPC
owns a save payload until acknowledgement; the ARM publishes load payloads
before its response cache line. An older launcher protocol disables this new
storage path and controller recording before the new region is touched.

The following tables describe the unchanged V4 prefix. SGIX remains a legacy
optional host catalog; this release's console import/export does not use it.

## Scope

`SGHF` is the canonical, path-independent ghost object. It contains one raw
pose track and enough metadata to reject the wrong game, route, or decoder.
V3/V4 partition one absolute-QFT track into up to 64 ordered route segments; it
does not reset time at a stage load. It contains no console slot number,
storage generation, or filename.

Console storage is a separate layer. It uses A/B slot envelopes below
`/susamune_ghosts/<region>/p<profile>`. The mailbox contract is declared by
`include/susamune/ghost_storage.h`; the kernel owns the envelope and path
implementation. It derives every path from validated numeric region, profile,
slot, and A/B bank values. It never uses imported display text or a portable
filename as a path component.

`SGIX` is a legacy optional host bundle catalog.
The console does not read it, enumerate it, or use it as its source of truth.

All multibyte values in both formats are big-endian. Signed fields use two's
complement. Readers must decode fields from bytes and validate them before
constructing runtime state; casting an untrusted file to the C structs is not a
validation strategy.

The shared constants and layout declarations are in
`include/susamune/ghost_format.h`. `scripts/validate_ghost.py` is the independent
host validator.

## Canonical ghost header (`SGHF`)

The V4 header is exactly `0x100` bytes. Fields through `0xcf` retain V3's
offsets.

| Offset | Size | Field | V4 rule |
|---:|---:|---|---|
| `0x00` | 4 | `magic` | ASCII `SGHF` |
| `0x04` | 2 | `version` | `4` |
| `0x06` | 2 | `headerSize` | `0x100` |
| `0x08` | 4 | `fileSize` | Exact byte length; max `0x69ee0` |
| `0x0c` | 4 | `fileChecksum` | CRC-32 of the complete file |
| `0x10` | 4 | `headerChecksum` | CRC-32 of the header |
| `0x14` | 4 | `payloadChecksum` | CRC-32 of the sample payload |
| `0x18` | 4 | `requiredFeatures` | Extended-codec bit `0x00000001` |
| `0x1c` | 4 | `runFlags` | Advisory eligibility metadata |
| `0x20` | 4 | `gameId` | `GMSJ`, `GMSE`, or `GMSP` |
| `0x24` | 1 | `discRevision` | `0` |
| `0x25` | 1 | `region` | JP `0`, US `1`, PAL `2`; must match `gameId` |
| `0x26` | 1 | `sourceProfile` | `0` through `3` |
| `0x27` | 1 | `recordingMode` | Raw pose/QFT mode `2` |
| `0x28` | 1 | `sampleCodec` | Pose/attachment codec `1` |
| `0x29` | 1 | `sampleStride` | `16` |
| `0x2a` | 2 | `sampleIntervalQf` | `4` |
| `0x2c` | 1 | `routeArea` | Game area ID, at most `0x3c`; segment zero |
| `0x2d` | 1 | `routeEpisode` | Episode `0` through `9` |
| `0x2e` | 1 | `routeParentArea` | Parent area, or `0xff` when absent |
| `0x2f` | 1 | `routeFlags` | Only bits `0x01` and `0x02` |
| `0x30` | 4 | `routeVariant` | `-1` when absent, otherwise `0` through `255` |
| `0x34` | 4 | `resultQf` | Absolute QFT result, or `0xffffffff` |
| `0x38` | 4 | `startQf` | Absolute QFT at the first segment start |
| `0x3c` | 4 | `endQf` | Absolute QFT at the final segment end |
| `0x40` | 4 | `durationQf` | `endQf - startQf`, at most `107892` |
| `0x44` | 4 | `sampleCount` | `2` through `26974` |
| `0x48` | 4 | `payloadSize` | Fixed segment table plus pose samples |
| `0x4c` | 8 | `createdUnixHi/Lo` | Optional unsigned Unix time; zero if unknown |
| `0x54` | 8 | `ghostIdHi/Lo` | Opaque nonzero ID |
| `0x5c` | 1 | `authorLength` | `0` through `24` |
| `0x5d` | 1 | `nameLength` | `1` through `48` |
| `0x5e` | 1 | `profileNameLength` | `0` through `16` |
| `0x5f` | 1 | `checksumKind` | CRC-32 kind `1` |
| `0x60` | 24 | `author` | Length-delimited display text |
| `0x78` | 48 | `name` | Length-delimited display text |
| `0xa8` | 16 | `profileName` | Length-delimited display text |
| `0xb8` | 72 | extension | V4 segment/attachment layout below |

`startQf` and `endQf` are bounded to `0x7fffffff`, matching the signed runtime
clock. `endQf` must not precede `startQf`. A present `resultQf` must lie inside
the inclusive start/end interval. Library duration totals use `durationQf`,
not the absolute clock values or the result.

`routeVariant` preserves `TFlagManager` flag `0x40003`, the parent-episode or
scenario selector that distinguishes internal routes sharing an area ID.
`routeParentArea` records the logical parent when it can be resolved. Route flag
`0x01` marks an internal scene and must be set exactly when `routeParentArea` is
not `0xff`. Flag `0x02` marks parent-start route identity and is invalid without
that parent area. V3/V4 validate this tuple structurally but deliberately do not
embed a game-specific child-to-parent lookup table; custom parent mappings stay
valid within the same area and variant bounds. Unknown route flags are invalid
instead of being guessed.

The display fields contain printable ASCII bytes `0x20` through `0x7e`.
Slash and backslash are rejected even though these strings must never become
paths. Bytes after each declared length must be zero. This deliberately avoids
an ARM-side Unicode decoder and region-font differences; UTF-8 would require a
later version or required feature.

## V4 segment and attachment extension

V4 gives the header's final 72 bytes this exact layout:

| Offset | Size | Field | V4 rule |
|---:|---:|---|---|
| `0xb8` | 2 | `segmentCount` | `1` through `64` |
| `0xba` | 2 | `segmentSize` | `0x20` |
| `0xbc` | 4 | `segmentTableOffset` | `0x100` |
| `0xc0` | 4 | `segmentTableSize` | `0x800` |
| `0xc4` | 4 | `sampleDataOffset` | `0x900` |
| `0xc8` | 4 | `sampleDataSize` | `sampleCount * 16` |
| `0xcc` | 4 | `segmentTableChecksum` | CRC-32 of the complete `0x800` table |
| `0xd0` | 1 | `attachmentCount` | `0` through `7` |
| `0xd1` | 1 | `attachmentSize` | `6` |
| `0xd2` | 2 | `attachmentFlags` | Only held-overflow bit `0x0001` |
| `0xd4` | 2 | reserved | Zero |
| `0xd6` | 42 | attachment descriptors | Seven fixed six-byte rows |

Each active attachment descriptor is a big-endian `u32 mObjectID` followed by
the actor's big-endian `u16 JDrama::TNameRef::mKeyCode`. These values are
address-free source-game identifiers. They are interpreted together with the
header's source region; no vtable, pointer, or code address enters the file.
Active descriptors must be nonzero and unique. Rows after `attachmentCount`
must be zero.

The held-overflow flag permits held index `15` and records that the writer
encountered an unrepresentable identity. This can happen after seven distinct
identities or when the live actor exposes the unidentifiable all-zero pair.
It may remain set if a later end-QF trim removes the relevant sample; overflow
never fails or stops a recording.

The segment table always reserves 64 descriptors. Descriptors after
`segmentCount` are zero. This fixed 2 KiB cost keeps every offset bounded and
makes a future file impossible to reinterpret through a forged variable
offset. Each active descriptor is 32 bytes:

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 4 | first sample index |
| `0x04` | 4 | sample count, at least one |
| `0x08` | 4 | absolute segment start QF |
| `0x0c` | 4 | absolute segment end QF |
| `0x10` | 4 | signed route variant |
| `0x14` | 4 | area, episode, parent area, route flags |
| `0x18` | 8 | reserved, all zero |

Descriptors cover the sample array contiguously and exactly: descriptor zero
starts at sample zero and each later `firstSample` equals the preceding end.
Every QF range is inclusive, bounded to `0x7fffffff`, and ordered so the next
start is at least the previous end. Gaps are valid and represent loading or a
transition with no observable Mario pose. Overlap and time regression are
invalid.

The first descriptor's route and start QF equal the common header route and
`startQf`; the final descriptor ends at header `endQf`. Header `durationQf` is
the global `endQf - startQf`, including transition gaps. It is deliberately not
the sum of segment spans. A present result belongs to the final segment. This
one absolute timeline lets playback wait when live QFT is early or seek into a
saved segment when live QFT is late.

Writers always emit the fixed table, including for a one-segment route. This
keeps one canonical offset. V3 uses the same segment fields and offsets but
requires all 48 bytes after the segment-table checksum to be zero.

## Pose payload

The `0x800` segment table occupies `0x100-0x900`, and V4 sample data begins at
`0x900`. Every sample is exactly 16 bytes:

| Offset | Size | Type | Meaning |
|---:|---:|---|---|
| `0x00` | 2 | `s16` | Game-native Mario yaw |
| `0x02` | 2 | `u16` | QFT delta |
| `0x04` | 3 | signed BE24 | X position multiplied by 8 |
| `0x07` | 3 | signed BE24 | Y position multiplied by 8 |
| `0x0a` | 3 | signed BE24 | Z position multiplied by 8 |
| `0x0d` | 3 | BE24 | Animation ID 9, phase 8, Yoshi state 3, held index 4 |

The first sample of every segment has delta zero. Later deltas are at
least four QF, except that each segment's terminal sample may use one through
three. Deltas within one segment sum exactly to that descriptor's
`endQf - startQf`. A one-sample segment is valid only with equal start/end QF.
No delta spans the unobserved gap between descriptors.

This normalizes the runtime's absolute first timestamp while retaining exact
QFT alignment in `startQf`. Writers must emit the final transform at `endQf`;
readers do not extrapolate a missing tail.

Fixed positions are limited to `-8000000` through `8000000`, equivalent to
plus or minus 1,000,000 game units. V3/V4 allow cadence gaps, including lag, as
long as an individual `u16` delta can represent the gap. A ghost is a visual
reference, not a deterministic simulation; the format makes no promise about
RNG, objects, or physics state.

The animation field is `0..335`. When the Yoshi field is zero it is the shared
`gMarioAnimeData` logical index. Mounted samples instead store the direct
retail rider BCK ID in `0xB6..0xC6`; retail bypasses `gMarioAnimeData` for
these poses.

V4's phase is normalized to `0..255`; the runtime expands it to the renderer's
`0..4095` phase domain. Equal-ID samples interpolate phase along the shortest
modular path; an ID change steps at its recorded four-QF boundary. JP/US/PAL
retail animation maps were verified byte-identical, while the file remains
independent of region-specific animation pointers.

The Yoshi field is zero when not mounted, `1..4` for green, orange, purple,
and pink, and `5` for mounted with an unknown color. Values `6..7` are invalid.
The held index is zero for no held actor, `1..7` for the corresponding header
descriptor, or `15` for held-but-unknown when the overflow flag is set. Values
`8..14`, an index beyond `attachmentCount`, and `15` without the flag are
invalid.

The current renderer supports Yoshi's base-body model, the five retail
`TResetFruit` models (`0x40000390..0x40000394`), and the exact retail
`TJumpBase` spring (`0x40000017`). Unknown or unproved held actors stay hidden.
It does not record Mario's motion blend, upper-body/FLUDD blend, detailed Yoshi
pose or separate hands/tongue, held-object animation state, hand/accessory
selection, or goop/material state.

The maximum sample data is `26974 * 16 = 431584` bytes (`0x695e0`). V4 adds
the fixed `0x800` table and header for a maximum 433888 bytes (`0x69ee0`),
still below the `0x7ff00` transfer payload. V4 itself sets
`SUSAMUNE_GHOST_REQUIRED_EXTENDED_CODEC` and codec `1`; those values are an
exact pair rather than advisory metadata.

## V3 compatibility

V3 remains a strict read format. It requires version `3`, required features
zero, raw codec `0`, a zero 48-byte extension tail, and the original sample
trailer of animation ID 9, normalized phase 12, and three zero low bits. A V4
reader selects this decoder by version and never treats those validated-zero
bits as attachment data. V4 writers always emit version `4`; a released
V3-only reader sees the newer version before parsing its body and refuses it
as unsupported, preserving the slot instead of corrupting or replacing it.

## Checksums

All checksums use reflected CRC-32/ISO-HDLC:

- polynomial `0xedb88320`;
- initial value `0xffffffff`;
- reflected input and output;
- final XOR `0xffffffff`.

This is the CRC returned by zlib's `crc32` interface.

Coverage is exact:

- `payloadChecksum`: bytes `[headerSize, fileSize)` with no substitutions.
- `headerChecksum`: the `0x100`-byte header with bytes `[0x0c, 0x14)`
  (`fileChecksum` and `headerChecksum`) replaced by zero.
- `fileChecksum`: bytes `[0, fileSize)` with bytes `[0x0c, 0x10)`
  (`fileChecksum` only) replaced by zero. It includes the stored header and
  payload checksums.

A writer calculates the payload checksum, then the header checksum, then the
file checksum. A checksum mismatch is corruption, not an unsupported version.

## Version and corruption behavior

A reader first recognizes the magic and version from the bounded eight-byte
prefix. A recognized newer version or unknown required feature is refused as
unsupported. Console storage must mark that slot unsafe/read-only so an older
kernel cannot replace data it does not understand.

A supported version with impossible sizes, mismatched metadata, invalid text,
unknown unflagged codec values, nonzero reserved bytes, or bad checksums is
corrupt. It is isolated to its slot and never partially activated. LOAD decodes
into an inactive buffer and changes the active ghost only after the complete
file passes validation.

Personal-library ghosts remain region-local: `gameId`, `region`, disc revision,
and source profile must match the running build and selected PB profile.

The global imported pool may use an unmodified revision-zero JP, US, or PAL
file on another region. This is visual pose-and-animation playback, not format
conversion: the source tuple and all metadata remain immutable. A foreign file
is accepted only when its header route, and every SGHF V3/V4 segment route, has a
shared retail meaning. The portable route policy is:

- standalone areas `00, 01, 02, 03, 04, 05, 06, 08, 09, 14, 15, 16, 17, 18,
  1D, 34, 3C`, with parent `FF` and route flags exactly zero;
- child-parent pairs `07->06, 0D->05, 0E->06, 10->09, 1E->03, 1F->09,
  20->04, 21->04, 28->06, 29->05, 2A->08, 2C->09, 2E->02, 2F->02,
  30->03, 32->05, 33->06, 37->02, 38->06, 39->09, 3A->05, 3B->03`.

A listed child must carry `INTERNAL_SCENE`, may also carry `PARENT_START`, and
must carry no other route flag. Episode and variant retain the ordinary
bounded format rules. Any unlisted or mismatched route fails closed. This
policy is shared by the ARM catalog/load validator and PPC race activation.

## Bounded share-file round trip

Received files are placed, without a rename step, in one user-facing directory
on the launcher's storage device:

`/susamune_ghosts/import/`

The filename is not trusted. A candidate leaf must be at most 95 bytes, consist
only of printable ASCII, end case-insensitively in `.smsghost`, contain none of
`"*/:<>?\|`, and not end in a dot or space. Only the sanitized leaf is cached,
and paths are always rebuilt beneath the fixed import root.

The ARM scans one directory entry or one bounded file operation per DI-idle
service pass. It checks the header and complete fixed segment table before
cataloguing a candidate. Every compatible file can appear in the library;
16 records are returned per page in filesystem directory order. Scanning does
not copy all records or ghost payloads into RAM. LOAD reopens the exact
validated leaf and checks the complete file, including payload and file CRCs,
before changing playback. The leaf plus canonical CRC identifies an imported
selection independently of its page or current directory position.

If a supported-version file changes after prefix scanning and fails full LOAD
payload or CRC validation, its cached sanitized leaf remains as a visible
unsafe imported row. It cannot be loaded, but explicit DELETE may unlink that
one quarantined source. This keeps the published row count consistent and
retains an in-game way to remove a payload-corrupt file. A forward-version
result instead invalidates the catalog; the mandatory rescan omits it, so an
older build cannot delete it.

EXPORT reads and fully validates the selected A/B library generation, then
writes only canonical SGHF bytes to a friendly deterministic leaf:

`YYYY_MM_DD_<route>_<time>[<CRC>].smsghost`

For example, `2026_08_15_BH4_42490[89ABCDEF].smsghost` is a Bianco 4
export whose compact duration is 42.490 seconds. The date comes from FatFS at
export time. Main courses use bounded abbreviations (`BH`, `RH`, `GB`, `PP`,
`SB`, `PV`, and `NB`, plus `AS`, `DP`, `CM`, and `BW` where applicable) and a
one-based episode. An internal start uses the validated parent area and route
variant when available. Other routes use uppercase raw `AxxEyy`.

The compact millisecond value is the exact floor
`durationQf * 1001 / 120`: `42.490` becomes `42490`, and `1:02.345` becomes
`102345`. Arithmetic widens before multiplying. The bracketed suffix is the
stored canonical file CRC as eight uppercase hex digits. No display name,
author, or external filename contributes text to the path.

EXPORT uses create-new semantics: it never truncates or overwrites an existing
leaf. For a new leaf it writes a zero header, writes the remainder in at most
16 KiB per DI-idle pass, syncs it, and commits the real `0x100` header last. An
existing deterministic name is reported as already existing; this prototype
does not compare the existing bytes or call the operation successful. An
interrupted new export can leave an invalid partial leaf for the user to
remove, but cannot damage an earlier export or the A/B library. Friendly
exports remain unmanaged copies outside the personal library. A different date
produces another file; this build does not enumerate or prune export copies.
Full or write-protected media reports an error without changing a library
generation or catalog row.

Imported ghosts remain canonical files in the import directory. They are not
copied into a PB profile or rewritten into an A/B envelope, and they are
visible whichever PB profile is active. Loading or racing does not consume a
personal slot. Explicit imported DELETE unlinks the selected source leaf; no
automatic import, pruning, rename, or replacement occurs.

## Paged console catalog and storage

Personal libraries remain separated by `(running game region, PB profile)`.
The former 45-personal, 12-imported and ten-hour aggregate limits are removed.
Libraries grow with available filesystem space. The individual 15-minute
recording and format bounds remain unchanged. A maximum V5 canonical ghost
occupies 1,297,992 bytes before its 64-byte personal envelope and filesystem
allocation overhead; imports and share copies contain only canonical bytes.

The kernel retains one working A/B slot and one 16-entry catalog page. The
PPC caches one personal page and one imported page in the existing 7,680-byte
cache window. Neither cache grows with library size. A page refresh scans the
whole selected directory incrementally, so larger libraries take more service
passes. Personal scans read only the 64-byte envelopes and 256-byte canonical
headers; imported scans read the fixed 2,304-byte header/segment prefix. Full
payload validation is deferred until LOAD or EXPORT. The kernel services at
most one directory entry or bounded header operation, or 16 KiB of payload,
per DI-idle pass; ongoing disc reads retain priority.

Protocol 5 keeps the two 32-byte request/response lines and all MEM2 addresses.
Its request has `profile:u16` at offset 12, reserved zero at 14, `slot:u32` at
16, `payloadSize:u32` at 20, flags zero at 24 and `expectedGeneration:u32` at
28. The response field at offset 24 is now `slot:u32`, followed by page count
and profile (`u16` each). Older protocols must not interpret this wire layout.

- LIST and IMPORT_SCAN use request `slot` as a record offset, normally
  `0, 16, 32, ...`; payload and expected generation are zero. Page responses
  have response `slot=0`, `generation=0` and the actual page entry count.
- SAVE with `slot=0xffffffff` asks the kernel to choose an unused personal ID.
  A tombstoned ID is reused with its next A/B generation. Otherwise allocation
  appends after existing filenames, with safe hole probing at the ID limit.
  The successful response returns the chosen ID. Explicit SAVE still refuses
  an occupied or unsafe ID; there is no overwrite flag.
- Personal LOAD, DELETE and EXPORT carry a stable `u32` ID and its expected
  A/B generation. Imported LOAD and DELETE carry ID zero, a 96-byte
  NUL-terminated exact leaf payload and the canonical CRC as expected
  generation. Zero generations and zero CRCs are valid values. Identity checks
  run again before action; a changed selection fails instead of selecting
  whichever record now occupies a page row.
- Error responses have zero payload size and page count. Complete load bytes
  are published before the response acknowledgement. Request payloads remain
  immutable while the request is outstanding.

The 3,680-byte `SusamuneGhostCatalogPage` has this layout, with big-endian
integers on both console processors:

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 4 | `SGPG` magic |
| `0x04` | 2 | page format version `1` |
| `0x06` | 2 | count, `0..16` |
| `0x08` | 4 | requested first record offset |
| `0x0c` | 4 | full directory record count |
| `0x10` | 8 | aggregate duration, low word then high word |
| `0x18` | 4 | next append ID hint; not an action identity |
| `0x1c` | 4 | reserved flags, zero |
| `0x20` | 3648 | sixteen 228-byte catalog entries |

Each entry is a `u32` stable ID, the unchanged 128-byte
`SusamuneGhostSlotInfo`, and a 96-byte import leaf (zero for personal rows).
Unused entries are entirely zero. A page past the current directory end is
empty and still reports the full count and 64-bit duration. Personal rows with
no readable bank remain visible as unsafe entries and are never reused as
empty space. Payload corruption discovered only by LOAD does not authorize
exporting or activating a different A/B generation.

## Personal A/B envelope (`SGEN`)

Canonical SGHF bytes remain unchanged and contain no console slot ID.
Personal files keep `/susamune_ghosts/<region>/p<profile>/gNN[a|b].sgh`, where
NN is an unsigned decimal ID with a minimum width of two. IDs `45..47` remain
reserved and their historical files are ignored; newer IDs begin at 48. This
reserved gap is not a capacity limit. ID `0xffffffff` is the SAVE_AUTO sentinel.

The current envelope version is 2, still exactly 64 bytes. Version 1 remains
readable for its original 16-bit IDs. Version 2 stores the low 16 bits in the
old slot field and the complete 32-bit ID at offset 40. Every remaining
reserved word is zero. This extends the namespace without changing any
canonical V3/V4/V5 file or legacy A/B filename.

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 4 | `SGEN` magic |
| `0x04` | 2 | envelope version `1` or `2` |
| `0x06` | 2 | header size `64` |
| `0x08` | 4 | wrapping generation |
| `0x0c` | 4 | flags; bit 0 is tombstone |
| `0x10` | 4 | source game ID |
| `0x14` | 2 | personal profile `0..3` |
| `0x16` | 2 | slot ID, low 16 bits |
| `0x18` | 4 | canonical payload size |
| `0x1c` | 4 | canonical duration QF |
| `0x20` | 4 | CRC-32 of exact canonical bytes |
| `0x24` | 4 | envelope CRC-32, with this word zero |
| `0x28` | 4 | full slot ID in V2; zero in V1 |
| `0x2c` | 20 | reserved, zero |

SAVE writes the inactive bank with a zero envelope, streams and syncs the
payload, then writes and syncs the real envelope last. Interrupted writes do
not replace a readable committed bank. Equal generations with conflicting
metadata, ambiguous half-range generation differences, future formats and
unreadable slots fail closed. A corrupt bank header can fall back to a valid
other bank; full-payload failure cannot silently change the selected identity.

DELETE commits and syncs a newer tombstone before unlinking the old payload
bank. A power failure during reclamation cannot revive that old ghost. The
small tombstone remains for safe ID reuse and generation continuity; a future
SAVE_AUTO can reuse it. If reclamation fails, the service reports the I/O
error and the committed tombstone still protects the deletion. Library,
import and export operations never prune unrelated files.

## Portable bundle index (`SGIX`)

SGIX V1 is a legacy optional host bundle index. Its 48-entry and ten-hour
fields below describe that historical format only. The console does not read
SGIX and none of these limits apply to protocol-5 libraries. Existing host
validation remains available for old bundles.

The header is `0x80` bytes:

| Offset | Size | Field |
|---:|---:|---|
| `0x00` | 4 | `SGIX` magic |
| `0x04` | 2 | version `1` |
| `0x06` | 2 | header size `0x80` |
| `0x08` | 2 | entry size `0x80` |
| `0x0a` | 2 | entry count, at most 48 |
| `0x0c` | 4 | exact file size |
| `0x10` | 4 | wrapping generation |
| `0x14` | 4 | file CRC, with this field zeroed |
| `0x18` | 4 | CRC of all entry bytes |
| `0x1c` | 4 | game ID |
| `0x20` | 4 | region, profile, max entries `48`, flags `0` |
| `0x24` | 4 | cached total duration |
| `0x28` | 4 | quota duration `4315684` |
| `0x2c` | 8 | creation Unix time, high word then low word |
| `0x34` | 1 | profile-name length |
| `0x35` | 1 | checksum kind `1` |
| `0x36` | 2 | zero |
| `0x38` | 16 | profile name |
| `0x48` | 56 | zero |

Each `0x80`-byte entry contains, in order: the 64-bit ghost ID; file size and
CRC; duration, result, start and end QF; sample count; creation time; route
variant; four route bytes; name and author lengths; 16-bit entry flags; a
48-byte name; and a 24-byte author. The only SGIX V1 entry flags are pinned
`0x0001` and autosaved `0x0002`. Cached file size requires the V3/V4 fixed-table
shape; bundle validation then requires an exact match with the referenced SGHF.

A portable exporter may name a ghost `g_<16 lower-case hex digits>.smsghost` using
the opaque ID, after checking for ID collisions. That convention is host-only.
Display names never become filenames. A bundle reader validates every file and
all cached entry fields before accepting the catalog.

Wrapping generation comparison uses the half-range rule: candidate `a` is
newer than `b` when `(a - b) mod 2^32` is nonzero and less than `2^31`. Exactly
`2^31` apart is ambiguous and refused. A corrupt newer copy may fall back to a
valid older copy, but the presence of a recognized forward-version copy blocks
fallback and overwrite.

## Host validation

Validate one or more canonical files:

```text
python scripts/validate_ghost.py ghost.smsghost
```

Validate SGIX plus every host-export file it references:

```text
python scripts/validate_ghost.py export.sgi --bundle-dir exported_ghosts
```

Run the format tests:

```text
python scripts/test_ghost_format.py
```

The validator returns exit code 0 for valid input, 2 for corruption or policy
violations, and 3 for a recognized unsupported version or feature.
