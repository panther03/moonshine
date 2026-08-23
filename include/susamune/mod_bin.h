#ifndef SUSAMUNE_MOD_BIN_H
#define SUSAMUNE_MOD_BIN_H

#include "susamune/mem2_map.h"

// =====================================================================
// mod_bin.h
//
// The on-disc format of mod_<region>.bin and the MEM2 window it is staged
// in. The mod used to be a byte array compiled into the Nintendont kernel
// (one launcher per game version, and a second copy of the blob resident in
// MEM2 for the whole session). It is now a file next to the launcher's
// boot.dol that the loader reads for the detected disc and the kernel copies
// into MEM1, so one launcher serves GMSJ/GMSE/GMSP.
//
// The hook writes travel with the code: their addresses are per-version, so
// a blob without its write list is not applicable to anything.
//
// Shared by all three toolchains, so this header is plain C with no type
// dependencies. Everything is big-endian on both sides; no swapping.
//
// Flow:
//   loader (PPC) -- knows the game id before booting the kernel; reads
//                   <launch_dir>/mod_<region>.bin into the MEM2 window below
//                   and flushes it. Writes a zeroed header if there is none.
//   kernel (ARM) -- PatchSusamune() validates the header against the running
//                   GAME_ID, copies the initialized prefix to baseAddr,
//                   zeroes its BSS tail, and applies the writes.
//                   SusamuneCfg.c also reads gameId from it to pick which
//                   susamune.ini sections belong to this run.
// =====================================================================

#define SUSAMUNE_MOD_MAGIC   0x534D4F44u  // 'SMOD'
#define SUSAMUNE_MOD_VERSION 2u

// Disc header bytes 0..3 of each supported revision.
#define SUSAMUNE_MOD_GAME_ID_JP  0x474D534Au  // "GMSJ"
#define SUSAMUNE_MOD_GAME_ID_US  0x474D5345u  // "GMSE"
#define SUSAMUNE_MOD_GAME_ID_PAL 0x474D5350u  // "GMSP"

#define SUSAMUNE_MOD_BASE_JP  0x80426020u
#define SUSAMUNE_MOD_BASE_US  0x80429800u
#define SUSAMUNE_MOD_BASE_PAL 0x80420D60u
#define SUSAMUNE_MOD_REGION_SIZE 0x80000u
#define SUSAMUNE_SCRATCH 0x40u
#define SUSAMUNE_MOD_MEM1_WORKING_CAP_SIZE 0x50000u
#define SUSAMUNE_MOD_BLOB_MAX_SIZE 0x50000u
#define SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET 0x50000u
#define SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE 0x20000u
#define SUSAMUNE_MOD_SCRATCH_OFFSET \
    (SUSAMUNE_MOD_REGION_SIZE - SUSAMUNE_SCRATCH)
#define SUSAMUNE_DEBUG_STACK_SIZE 0x2000u
#define SUSAMUNE_ARENA_RESERVE_SIZE \
    (SUSAMUNE_MOD_REGION_SIZE + SUSAMUNE_DEBUG_STACK_SIZE)

#define SUSAMUNE_MOD_BASE_FOR_GAME_ID(gameId)                         \
    ((gameId) == SUSAMUNE_MOD_GAME_ID_JP    ? SUSAMUNE_MOD_BASE_JP   \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_US  ? SUSAMUNE_MOD_BASE_US   \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_PAL ? SUSAMUNE_MOD_BASE_PAL  \
                                             : 0u)

// File layout: this header, then codeSize initialized bytes, then writeCount
// pairs of (addr, val). memSize is the complete zero-initialized runtime image;
// omitting its trailing BSS from the file saves I/O without moving an address.
// codeSize is a multiple of 4, so the write list stays word-aligned.
struct SusamuneModHeader {
    unsigned int magic;         // SUSAMUNE_MOD_MAGIC
    unsigned int version;       // SUSAMUNE_MOD_VERSION
    unsigned int gameId;        // SUSAMUNE_MOD_GAME_ID_*
    unsigned int baseAddr;      // MEM1 address the code is linked at
    unsigned int codeSize;      // initialized bytes present in the file
    unsigned int writeCount;
    unsigned int arenaReserve;  // what getArenaLo() adds; see PatchSusamuneGeckoCodes
    unsigned int memSize;       // full MEM1 image after zero-filling BSS
};

#define SUSAMUNE_MOD_HEADER_SIZE 32u

// The file name for a given disc id, spelled the same way by the build
// (scripts/gen_mod_bin.py) and the loader. Null for a game we have no mod for.
#define SUSAMUNE_MOD_REGION_TAG(gameId)                            \
    ((gameId) == SUSAMUNE_MOD_GAME_ID_JP    ? "jp"                 \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_US  ? "us"                 \
     : (gameId) == SUSAMUNE_MOD_GAME_ID_PAL ? "pal"                \
                                            : (const char *)0)

#define SUSAMUNE_MOD_FILE_FMT "mod_%s.bin"

// Portable compile-time checks (no C11 dependency). The reset-safe file
// ceiling has to fit inside the loader's unchanged staging allocation.
typedef char susamune_mod_header_size_check
    [(sizeof(struct SusamuneModHeader) == SUSAMUNE_MOD_HEADER_SIZE) ? 1 : -1];
typedef char susamune_mod_window_size_check
    [(SUSAMUNE_MEM2_MODBIN_SIZE >= SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE) ? 1 : -1];
typedef char susamune_mod_blob_scratch_check
    [(SUSAMUNE_MOD_BLOB_MAX_SIZE <= SUSAMUNE_MOD_SCRATCH_OFFSET) ? 1 : -1];
typedef char susamune_mod_working_cap_check
    [(SUSAMUNE_MOD_BLOB_MAX_SIZE == SUSAMUNE_MOD_MEM1_WORKING_CAP_SIZE) ? 1 : -1];
typedef char susamune_mod_attachment_offset_check
    [(SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET ==
      SUSAMUNE_MOD_MEM1_WORKING_CAP_SIZE) ? 1 : -1];
typedef char susamune_mod_attachment_bounds_check
    [(SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET +
      SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE <= SUSAMUNE_MOD_SCRATCH_OFFSET) ? 1 : -1];
typedef char susamune_mod_attachment_alignment_check
    [(((SUSAMUNE_MOD_BASE_JP | SUSAMUNE_MOD_BASE_US |
        SUSAMUNE_MOD_BASE_PAL | SUSAMUNE_MOD_ATTACHMENT_HEAP_OFFSET |
        SUSAMUNE_MOD_ATTACHMENT_HEAP_SIZE) & 31u) == 0) ? 1 : -1];
typedef char susamune_mod_staging_vault_check
    [(SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE ==
      SUSAMUNE_GHOST_ASSET_VAULT_OFFSET) ? 1 : -1];
typedef char susamune_mod_file_capacity_check
    [(SUSAMUNE_MOD_HEADER_SIZE + SUSAMUNE_MOD_BLOB_MAX_SIZE <=
      SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE) ? 1 : -1];

#define SUSAMUNE_MOD_PPC_PTR  ((struct SusamuneModHeader *)SUSAMUNE_MEM2_MODBIN_PPC_BASE)
#define SUSAMUNE_MOD_PHYS_PTR ((struct SusamuneModHeader *)SUSAMUNE_MEM2_MODBIN_PHYS_BASE)

#endif  // SUSAMUNE_MOD_BIN_H
