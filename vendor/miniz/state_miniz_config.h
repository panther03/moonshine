#pragma once
#define MINIZ_NO_STDIO
#define MINIZ_NO_TIME
#define MINIZ_NO_MALLOC
#define MINIZ_NO_ARCHIVE_APIS
#define MINIZ_NO_ARCHIVE_WRITING_APIS
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#define USE_EXTERNAL_MZCRC
#define MINIZ_USE_UNALIGNED_LOADS_AND_STORES 0
#define MINIZ_PORTABLE_FAST_DEFLATE 1
#define MINIZ_STATE_ADLER32 1
#define NDEBUG

// Retain the vendored fork's freestanding guard on both PPC and host tests.
#define LM_MINIZ_FREESTANDING
typedef __SIZE_TYPE__ size_t;
typedef signed short int16_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef signed long long int64_t;
typedef unsigned long long uint64_t;
#define NULL 0
#define assert(x) ((void)0)
extern "C" void *memcpy(void *, const void *, size_t);
extern "C" void *memset(void *, int, size_t);
extern "C" int memcmp(const void *, const void *, size_t);

// Fast-parser dictionary fills are disjoint, bounded copies. Share the same
// word-copy implementation as compressed output and retained-state commits.
void StateSlotPoolCopyForward(unsigned char *, const unsigned char *, unsigned int);
#define MINIZ_FAST_COPY(dst, src, size) StateSlotPoolCopyForward(dst, src, size)
