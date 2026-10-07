# Compressed state codec and staging audit

This describes the current three-slot RAM codec, the SD restore paths that use it,
and their temporary-memory ownership. The archive format and file worker are
covered in [SD savestates](foxtrot-sd-states.md).

## Codec contract and compression modes

`include/susamune/state_codec.hxx` accepts up to 64 input/output spans. The caller
owns the aligned workspace and all buffers. The freestanding miniz configuration
uses no heap, file APIs, or unaligned native word loads. Its workspace remains
319,360 bytes on the host and 319,296 bytes on PowerPC, within the 0x4E000
(312 KiB) allocation, including the inflater's 32 KiB dictionary. The workspace now lives in the unused ghost-transfer gap on a compatible launcher,
returning the full original 320 KiB reservation to the primary state pool.
An older launcher retains a 312 KiB workspace above the smaller primary pool.
The memory layout and workspace are selected together and latched at startup. CMake builds the codec and packed CRC with `-O2`; other sources retain their
own flags. The shared workspace has one owner throughout each transaction.

Saving first uses independent 128 KiB LZ4 blocks with a 32 KiB hash table. The
producer borrows contiguous source blocks and only gathers blocks that cross a
source span. It uses raw blocks when compression would grow them. The decoder
accepts either this bounded frame or the existing zlib stream.

The quick frame starts with big-endian words `0x4D534C34` (MSL4) and `0x20000`
(block size). Each block contains big-endian raw length and packed length; the
high bit of packed length selects raw copy. Raw length must be exactly the next
128 KiB of the caller's expected snapshot, or its shorter final block. Compressed
length must be smaller than raw length; a raw block must match its raw length.
Every block is independent, decoded into the bounded workspace with
`LZ4_decompress_safe`, and then delivered through the existing filtered scatter
sink. Unfiltered contiguous destination blocks decode directly to their final
address; filtered restores always use workspace so protected owner bytes are
never temporarily overwritten. Whole-snapshot length, exact packed consumption and Adler-32 remain checked.
No external dictionary or file-provided destination is accepted.

Quick compression trades some density for speed. If its complete candidate cannot
fit after reclaiming the selected slot, Save retries one-probe greedy miniz
Deflate. Capacity failure can repack retained quick states before trying
eight-probe lazy Deflate, as described below. All modes concatenate the
same static/root/stage and used ghost-prefix spans without a full raw copy.
`MINIZ_PORTABLE_FAST_DEFLATE` uses byte-safe little-endian reads on PowerPC and all
15 hash bits of its existing table. The selected mode is reused for recompression.
A nearly full pool can therefore make Save slower; there is no automatic eviction.

Within one synchronous Save, the three complete measured candidate sizes are
kept on the stack. If retained-state repacking frees some room, a retry skips
compression modes whose measured output still cannot fit. It retries the first
mode that now fits, using the same immutable source and unchanged codec. The
sizes never survive the operation or influence a later save. Private host checks
using two real PAL Plaza capture sets at an artificially pressured capacity
reduced candidate passes from six to four with byte-identical final streams.
Ordinary saves that do not repack are unchanged. This reduces redundant work;
it does not remove the stronger-compression delay or establish a Wii time limit.

Output spans are capacities: the save path supplies the 4 MiB transient area and
up to two free pool-bank spans. On exhaustion the sink counts while discarding
overflow. `OUTPUT_FULL` includes the complete required size, with checked counter
arithmetic; a partial candidate is never retained.

The replacement planner distinguishes a fully staged commit from a replacement
that fits only after reclaiming its old slot. For the latter, the complete first
pass proves the final size before reclamation. With source spans still immutable
and interrupts still disabled, the same producer recompresses directly into the
pool. Raw length, packed length and Adler-32 must match the measured result; a
broken invariant traps rather than resuming with lost state. A normal capacity
refusal preserves all prior slots and sidecars. No slot is evicted automatically.

## Restore validation and the RAM checksum cache

All entry points validate source/destination bounds, span counts, exact output
capacity, and source/workspace/descriptor overlap before writing. Their stream
ownership differs:

| Path | Admission before game writes | Restore pass |
|---|---|---|
| Local RAM state | Produced by this codec; compiled metadata/ranges and current packed CRC rechecked | One writing decode through `decompressVerified` |
| SD file imported into RAM | ARM verifies file CRCs; PPC fully validates the stream before committing the slot; subsequent RAM loads recheck its cached CRC and live owner profile | One writing decode on later RAM loads |
| SD file selected directly for Load | File checks and metadata/owner admission, then full stream validation while staged bytes remain owned | `decompress`: validation decode followed by writing decode |
| SD file with insufficient staging space | Prevalidate a compatible local recovery state, then validate the complete SD stream and checksum each fixed window | Re-read checked windows through `decompressStreamVerified`; a late read error restores the prevalidated local state |

`sPackedChecksums[3]` lives only in mod-owned RAM, outside `StoredState`, the game
snapshot and every archive. A successful local save records a CRC over the exact
committed packed bytes. A successful import records the receipt-verified payload
CRC only after complete stream validation and slot commit. Initialization/clear
invalidate the corresponding local entries. A file cannot supply a trusted-cache
flag or use its own checksum to opt into the fast restore path.

Packed CRC uses slicing-by-four tables rebuilt in the first 4 KiB of the idle
codec workspace. Codec use invalidates those scratch tables; each checksum
reinitializes them. No additional persistent table or heap allocation is needed.

Immediately before a RAM restore, under the save/load mutation guard, with GX
finished, interrupts disabled and audio DMA muted, `packedChecksum` rereads the
selected slot across its physical banks. A mismatch is refused before any restore
writes. Matching bytes may use `decompressVerified` because their producer or
full import validation has already established stream validity. This CRC is a
local integrity check, not cryptographic authentication of arbitrary files.
Metadata tags, slot identity/generation, compiled destination spans, scenario
checks and durable-owner checks remain independent requirements.

`validate` decodes into bounded workspace, checks stream structure and expected
Adler-32 (plus the embedded checksum for zlib), and requires exact compressed
consumption and decoded length.
`decompress` runs that pass before its scatter-writing pass. `decompressVerified`
omits only the redundant first inflate; its writing pass still verifies the
stream's end, length and checksum. Any failure after writing may have begun is
`COMMIT_FAILED`. Immutable RAM/staged-source failures trap. The streamed SD path
has a separately prevalidated local recovery state and restores it before any
gameplay resumes; failure of that immutable recovery path also traps.

`validateRestore` includes destination descriptor/overlap checks while decoding
without writes. Stream readers lend bytes only inside a declared fixed buffer;
the codec bounds every returned fragment against that buffer and the exact
packed length. Validation and writing are separate calls because each second-pass
SD window must match its first-pass checksum before being consumed. Stream state,
output descriptors and codec workspace cannot overlap the replaceable buffer.

Source bytes and descriptors remain immutable, and the workspace exclusively
owned, for the entire operation. An optional retained-owner copy callback is
preflighted by the caller, runs only during commit writes, and cannot fail or
change source/workspace/descriptors. Imported and direct SD states use this
filter so old runtime-owner bytes are never written temporarily and repaired
later.

Pool movement uses overlap-aware aligned word copies across logical bank
boundaries. Retained compressed bytes are PPC-only until export, so saving and
compaction do not eagerly flush the pool. Export flushes each exact source span
before publishing the ARM request; import receipt ownership is unchanged.

After a successful restore, game spans use `DCStoreRange` for GPU/DMA visibility.
The PPC retains its populated data cache rather than invalidating the whole
restored heap and immediately refetching it. No executable text is restored.

## Validation and measured scope

`scripts/test_state_codec.py` exercises production quick, fast and compact paths with
short inputs, dictionary wrapping, scattered boundaries, exact-full and overflow
capacities, malformed/truncated/appended streams, checksum/length failures,
copy filtering and overlap rejection. The private retail capture is checked
against independent Python zlib/LZ4 decoders and byte-exact roundtrips. Quick
frames include malformed block lengths, offsets, extensions, truncation, trailing
bytes and checksum failures, all rejected before the first restore write. The slot,
queue, archive and recompression tests cover the RAM trust cache, capacity
fallback, pinned selections and preservation of other states.

Build **00B63258** (US Dolphin **782E8578**) includes the relocated workspace and
contiguous RAM decode. The final fixture measured quick Save **0.625 s** and RAM
Load **0.391 s**, with exact position/QFT/native restoration and working Step and
Resume. Filling three slots then measured **0.500 / 0.531 / 2.140 s** to save. The
third state fit fast Deflate with the larger pool, avoiding the intermediate
build's compact fallback. Quick-state loads were **0.281–0.328 s**; that third
Deflate state loaded in **0.797–0.813 s**. These are single-scene emulator wall
times with input/sample overhead, not console SD timings or an all-scene bound.

Overwriting the middle slot took **1.016 s** and preserved the other slots' full
metadata/payloads. All three restored their exact position/timer after compaction,
and their PPC CRCs matched independent Python CRCs. Second-bank corruption was
refused before game writes; restoring the original byte recovered the state.
The four final proofs are under `build/foxtrot-speed-final` (`results.json`,
`replay-results.json`, `slots-results.json`, `overwrite-results.json`).

The separate private **BE0BF918** image exercises current production PPC SD
validation/restore through a host-supplied ARM receipt. A real 6,533,821-byte MSL4
state restored exact position/QFT and left all three RAM entries unchanged. A
malformed first block with corrected archive checksums was refused without world
or slot changes; Step worked after the valid restore. The adapter verifies the
compiled and selected pool/workspace/protocol before writing. This is not an
actual SD-transfer benchmark or a new reboot test. Evidence lives in
`build/foxtrot-speed-sd-proof/direct-sd-results.json`.

Intermediate build **8118304C** (US Dolphin **BDDD0EC6**) passed the same bounded fixture
in `build/foxtrot-speed-8118304C/results.json`: quick Save **0.641 s**, RAM Load
**0.484 s**, exact position/QFT/native display restore, six Steps and Resume.
Separate three-slot checks measured quick saves at **0.516–0.546 s**, quick loads
at **0.406–0.468 s**, and the crowded third slot's compact fallback at **6.015 s**
to save / **0.765–0.797 s** to load. This is a real capacity trade-off: the quick
states use more bytes, leaving the third slot to use stronger compression.

Overwriting the middle occupied slot took **1.000 s**, kept the other two states'
metadata/payloads byte-identical, and restored all three exact positions/timers
after compaction. Each PPC packed CRC matched an independent Python CRC. Corruption
in the second physical bank refused before game writes; restoring the correct
byte allowed the original state to load again. Evidence is in `slots-results.json`
and `overwrite-results.json` beside that intermediate smoke result.

These measurements include controller and display-sampling overhead and describe
one development-emulator scene, not Wii timing or an all-scene guarantee. The
prior comparable fixture is recorded below for context.

Historical US Bianco fixture measurements in Dolphin 2606a JIT:

| Version | Save | RAM Load |
|---|---:|---:|
| Prior release `5B0EC1B1` | 4.063 s | 1.343 s |
| Fast producer before the RAM-load change, US `F8C4117C` | 1.797 s | 1.422 s |
| Final producer and RAM-load path, US `9726D59B` | 1.891 s | 0.906 s |

The final US image corresponds to console build `4D8AD165`.
`build/foxtrot-fast-load/results.json` records the final restore's exact position,
QFT and native timer, followed by a working Step and Resume. RAM Load fell from
about 1.4 to 0.9 seconds in this sample. These are development-emulator wall times
for one scene and setup, not Wii measurements, controlled benchmark averages or
an all-scene speed guarantee. Compact capacity retries can still take longer.
Older host-only codec comparisons remain in
`build/foxtrot-codec-bench/results.json` as historical evidence, not measurements
of the current end-to-end path.

`build/foxtrot-sd-direct/results.json` records direct SD restoration of exact
position/QFT with all three RAM states unchanged, and rejection of a malformed
zlib stream despite correct file CRCs. Its ARM file receipt was supplied by a
host adapter; its post-delivery duration is not SD-card transfer time. Private
retail memory captures and extracted payloads must never enter release packages.

## Metadata storage

The three slot records and the uncommitted candidate occupy a fixed PPC-only
window in the recording input bank's unused tail: Wii `0x91C11F00`, Dolphin
`0x712D2F00`, with a 53,504-byte bound. The complete 54,000 input samples end
exactly before this window. Recording, imports and ghost-prefix restoration
copy only their checked input counts. Ghost model assets, the packed state
pool, codec workspace and MEM1 reserve stay at their existing addresses.

Console admission is latched before the first metadata initialization. It
requires the current configuration version, expanded-pool and relocated-codec
capabilities, and a valid ghost protocol 5 response. An older or mismatched
launcher disables savestates; no metadata is read or written at the new address.
Dolphin owns its separate window without a launcher. The guard does not
invalidate the configuration request cache line, which also contains PPC-owned
settings fields. Only the ARM-owned ghost response line is invalidated.

`scripts/test_state_metadata_memory.py` checks refusal without touching the
window, bounded initialization, adjacent canaries, independent Dolphin admission
and metadata access ordering. `sLiveArchiveProfile` remains in MEM1, in the
existing upper linker span.

## Crowded memory slots

An ordinary save first tries the quick format and fast Deflate. If neither fits,
it tries shrinking retained quick states to fast Deflate before spending a
compact pass on the incoming state. This avoids a full dense compression pass
when the crowded pool needs retained-state repacking anyway. The incoming
compact fallback and compact retained-state fallbacks remain available afterward.

Re-encoding retained slots keeps their complete compressed bytes
intact until a smaller replacement has been produced
and checked. The second compressor borrows the first 312 KiB of the existing
4 MiB temporary area; its output uses the remaining temporary area and unused
pool tail. No retained state is reclaimed to stage its own replacement.

The fallback tries each originally quick retained slot at most once with fast
Deflate. Every retained slot can then get one compact attempt, including Deflate
states from an earlier save or import. This extra save time occurs only after
a capacity refusal. It regenerates the new candidate only when the recovered
space could hold its previously measured candidate size, and
stops when the save succeeds. Ordinary saves that fit do no retained-state work.

Repacking checks the old packed CRC and the decoded size/Adler checksum. A
successful replacement updates only packed size, metadata checksum and packed
CRC. Slot generations, QFT/IL/ghost/input sidecars, SD-backed status and replay
references remain unchanged. If the requested save ultimately fails, existing
states still restore their original content; some may now use a smaller encoding.
This increases usable capacity without changing the memory map or ghost limits.

`scripts/test_retained_state_repacking.py` executes the production capacity
fallback, codec and two-bank commit with bounded host buffers. It checks a third
save recovered by shrinking an earlier quick state, output-capacity refusal,
corrupt retained CRCs, malformed packed streams, later saves with already-Deflate
neighbors, unchanged slot identities and preserved decoded content.

## Timing and mission countdowns

`rebaseMissionStopwatch` shifts the mission timer's absolute start timestamp by
the time spent blocked. Save calls it on success and failure; success includes
compression, checksum and pool commit. Load rebases the restored stopwatch so
elapsed time rewinds to the saved value. A rejected load preserves live elapsed
time rather than adopting any saved time. SD transfer uses its transaction start
for the full blocked interval; the direct-restore path retains that ownership
through its post-draw load. These adjustments follow retail
`OSCheckStopwatch`'s `total + (now - last)` calculation while active. QFT's normal
timing formulas remain separate from this mission stopwatch.

`scripts/test_savestate_stopwatch.py` compiles the production helper and covers
live elapsed/countdown preservation, normal progress afterward, restored elapsed
time, stopped watches, absent director and tick values above 32 bits. Queue and
archive tests additionally cover the different save/load/refusal time origins.

## Temporary 4 MiB lifetime

The audited PPC range `[0x91300000,0x91700000)` joins SegaBoot's 1 MiB and DIMM's
3 MiB. These are existing allocations, not newly discovered unowned memory.
Their use as transient Sunshine save staging is supported by these source paths:

- `include/susamune/mem2_map.h:15`: SegaBoot and DIMM are adjacent. The real-disc
  and ISO caches finish at 0x91300000, while DI scratch starts at 0x91700000.
  `launcher/kernel/ISO.c:38` and `RealDI.c:42` use those bounded cache constants.
- `launcher/loader/source/main.c:2169`: the validated Sunshine disc IDs bypass
  Triforce probing. SegaBoot staging is in the separate Triforce branch.
- `launcher/loader/source/ipl.c:164`: a normal IPL is decoded and copied out of
  DIMM into MEM1 at 0x81300000. It is flushed before returning. The loader's
  `main.c:2706` finishes this work before transferring execution to MEM1. No
  running game retains this DIMM source as its IPL or font backing storage.
  `launcher/kernel/EXI.c:58` keeps ROM fonts separately at physical 0x13100000.
- `launcher/kernel/DI.c:171`: DIMM clearing belongs to `DIinit(true)` at kernel
  launch. The disc-change path invokes `DIinit(false)` and skips that clearing.
- `launcher/kernel/DI.c`: media-board reads require `TRIGame`; SegaBoot reads
  require `useipltri`. Its formerly unconditional 0xAA arcade write now explicitly
  rejects GMSJ01/GMSE01/GMSP01 through the existing unsupported-command shutdown
  before touching any DMA source or DIMM bytes. Other discs retain their behavior.
- `launcher/kernel/Patch.c:4370`: warm patching uses the already copied MEM1 IPL
  when requested and clears `useipl` at the tail. Cache/SI resets and
  `launcher/kernel/TRI.c:88`'s GCAM reset do not reclaim SegaBoot or DIMM.

RAM saving uses this overlay only after Sunshine's normal readiness checks,
for one synchronous transaction with interrupts disabled. SD reads use the same
range under the separate receipt-confirmed transfer lock: gameplay remains held,
other save/load mutations are excluded, and a direct-load candidate stays owned
until its post-draw restore or rejection. It must never hold an authoritative
retained slot. A fresh loader/kernel launch can reclaim it;
the new run must invalidate its state catalog. The console memory-card-emulation
build exclusion remains essential: its larger allocation would exceed the
shared disc-cache region (`scripts/test_mem2_ownership.py`).

`scripts/test_state_staging.py` runs the production disc guard and unsupported
handler for the three regions, verifies ordinary reads and other games retain
their behavior, and checks the relevant dispatch/reset ownership ordering.
Its three tests and the two existing MEM2 ownership tests passed. The modified
DI source also compiled as an ARM object with its production flags. Console
timing and untested reset/scenario combinations still need hardware feedback.
Basic SD restoration after a Wii reboot was confirmed by the user on the previous
build; that does not prove every staging owner or current file-control path.
