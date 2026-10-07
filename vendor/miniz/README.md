This is the heapless miniz copy from DarkMoonshine's
`lm_diag/vendor/miniz`, copied on 2026-09-08. Its files identify the miniz
11.3.0 API and retain the original copyright and license notices in `LICENSE`.

The existing `LM_MINIZ_FREESTANDING` include guards are preserved. Sunshine's
`state_miniz_config.h` disables allocation, archive, stdio, time, and high-level
zlib APIs and prevents unaligned loads on PowerPC. `src/state_codec.cpp` includes
the three C implementation files in one translation unit, as DarkMoonshine does.

The portable fast parser records the final complete trigram of each match in its
existing hash table. This reduces some savestates enough to avoid the much slower
dense fallback, without changing the Deflate format or workspace size. The dense
parser and decoder retain their existing behavior.
The fast parser's two disjoint dictionary-fill copies use the same bounded
word-copy helper as the state pool and compressed output; no match behavior or
format changes are involved in that substitution.
