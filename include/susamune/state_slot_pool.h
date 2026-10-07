#ifndef SUSAMUNE_STATE_SLOT_POOL_H
#define SUSAMUNE_STATE_SLOT_POOL_H

enum { STATE_SLOT_POOL_COUNT = 3 };
enum {
    STATE_SLOT_REPLACE_REFUSE,
    STATE_SLOT_REPLACE_STAGED,
    STATE_SLOT_REPLACE_RECOMPRESS,
};
typedef struct StateSlotPoolEntry {
    unsigned int offset;
    unsigned int size;
} StateSlotPoolEntry;
typedef struct StateSlotPool {
    StateSlotPoolEntry slots[STATE_SLOT_POOL_COUNT];
    unsigned int used;
} StateSlotPool;

static inline int StateSlotPoolValid(const StateSlotPool *pool,
                                     unsigned int capacity) {
    if (!pool || pool->used > capacity) return 0;
    unsigned int count = 0;
    for (unsigned int i = 0; i < STATE_SLOT_POOL_COUNT; ++i) {
        const StateSlotPoolEntry *entry = &pool->slots[i];
        if (!entry->size) {
            if (entry->offset) return 0;
            continue;
        }
        if (entry->offset > pool->used ||
            entry->size > pool->used - entry->offset) return 0;
        ++count;
    }
    unsigned int cursor = 0, seen = 0;
    for (unsigned int step = 0; step < count; ++step) {
        unsigned int matches = 0;
        for (unsigned int i = 0; i < STATE_SLOT_POOL_COUNT; ++i) {
            if (!(seen & (1u << i)) && pool->slots[i].size &&
                pool->slots[i].offset == cursor) {
                seen |= 1u << i;
                ++matches;
            }
        }
        if (matches != 1) return 0;
        for (unsigned int i = 0; i < STATE_SLOT_POOL_COUNT; ++i) {
            if (pool->slots[i].size && pool->slots[i].offset == cursor) {
                cursor += pool->slots[i].size;
                break;
            }
        }
    }
    return cursor == pool->used;
}

typedef unsigned int StateSlotPoolWord __attribute__((__may_alias__));
typedef __UINTPTR_TYPE__ StateSlotPoolAddress;
typedef char StateSlotPoolWordMustBeFourBytes[sizeof(StateSlotPoolWord) == 4 ? 1 : -1];

static inline StateSlotPoolWord StateSlotPoolMergeWords(StateSlotPoolWord first,
                                                       StateSlotPoolWord second,
                                                       unsigned int shift) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return (first << shift) | (second >> (32 - shift));
#else
    return (first >> shift) | (second << (32 - shift));
#endif
}

/* Aligned loads/stores only. Byte edges keep every read inside its span,
 * including when adjacent logical banks are physically far apart. */
#ifdef __cplusplus
// Pool moves, codec output and filtered restores share one checked copy body.
__attribute__((noinline)) inline
#else
static inline
#endif
void StateSlotPoolCopyForward(unsigned char *dst,
                                            const unsigned char *src,
                                            unsigned int size) {
    while (size && ((StateSlotPoolAddress)dst & 3u)) {
        *dst++ = *src++;
        --size;
    }
    const unsigned int misalignment = (StateSlotPoolAddress)src & 3u;
    if (!misalignment) {
        StateSlotPoolWord *out = (StateSlotPoolWord *)dst;
        const StateSlotPoolWord *in = (const StateSlotPoolWord *)src;
        while (size >= 16) {
            out[0] = in[0]; out[1] = in[1]; out[2] = in[2]; out[3] = in[3];
            out += 4; in += 4; size -= 16;
        }
        while (size >= 4) { *out++ = *in++; size -= 4; }
        dst = (unsigned char *)out;
        src = (const unsigned char *)in;
    } else if (size >= 4) {
        /* Consume a word before rounding the source pointer backwards. */
        for (unsigned int i = 0; i < 4; ++i) dst[i] = src[i];
        dst += 4; src += 4; size -= 4;
        if (size >= 8 - misalignment) {
            const StateSlotPoolWord *in = (const StateSlotPoolWord *)(src - misalignment);
            StateSlotPoolWord *out = (StateSlotPoolWord *)dst;
            StateSlotPoolWord first = *in++;
            do {
                const StateSlotPoolWord second = *in++;
                *out++ = StateSlotPoolMergeWords(first, second, misalignment * 8);
                first = second;
                src += 4; size -= 4;
            } while (size >= 8 - misalignment);
            dst = (unsigned char *)out;
        }
    }
    while (size) { *dst++ = *src++; --size; }
}

static inline void StateSlotPoolCopyBackward(unsigned char *dst,
                                             const unsigned char *src,
                                             unsigned int size) {
    dst += size;
    src += size;
    while (size && ((StateSlotPoolAddress)dst & 3u)) {
        *--dst = *--src;
        --size;
    }
    const unsigned int misalignment = (StateSlotPoolAddress)src & 3u;
    if (!misalignment) {
        StateSlotPoolWord *out = (StateSlotPoolWord *)dst;
        const StateSlotPoolWord *in = (const StateSlotPoolWord *)src;
        while (size >= 16) {
            out[-1] = in[-1]; out[-2] = in[-2]; out[-3] = in[-3]; out[-4] = in[-4];
            out -= 4; in -= 4; size -= 16;
        }
        while (size >= 4) { *--out = *--in; size -= 4; }
        dst = (unsigned char *)out;
        src = (const unsigned char *)in;
    } else if (size >= 4) {
        for (unsigned int i = 0; i < 4; ++i) *--dst = *--src;
        size -= 4;
        if (size >= 4 + misalignment) {
            const StateSlotPoolWord *in = (const StateSlotPoolWord *)(src - misalignment);
            StateSlotPoolWord *out = (StateSlotPoolWord *)dst;
            StateSlotPoolWord second = *in;
            do {
                const StateSlotPoolWord first = *--in;
                *--out = StateSlotPoolMergeWords(first, second, misalignment * 8);
                second = first;
                src -= 4; size -= 4;
            } while (size >= 4 + misalignment);
            dst = (unsigned char *)out;
        }
    }
    while (size) { *--dst = *--src; --size; }
}

static inline void StateSlotPoolMove(unsigned char *dst,
                                      const unsigned char *src,
                                      unsigned int size) {
    if (dst == src || !size) return;
    if ((StateSlotPoolAddress)dst < (StateSlotPoolAddress)src)
        StateSlotPoolCopyForward(dst, src, size);
    else
        StateSlotPoolCopyBackward(dst, src, size);
}

static inline int StateSlotPoolPlanReplace(const StateSlotPool *pool,
                                           unsigned int capacity,
                                           unsigned int slot,
                                           unsigned int packedSize,
                                           unsigned int stagingSize) {
    if (!StateSlotPoolValid(pool, capacity) || slot >= STATE_SLOT_POOL_COUNT ||
        !packedSize) return STATE_SLOT_REPLACE_REFUSE;
    const unsigned int retained = pool->used - pool->slots[slot].size;
    if (packedSize > capacity - retained) return STATE_SLOT_REPLACE_REFUSE;
    if (packedSize <= stagingSize ||
        packedSize - stagingSize <= capacity - pool->used)
        return STATE_SLOT_REPLACE_STAGED;
    return STATE_SLOT_REPLACE_RECOMPRESS;
}

static inline int StateSlotPoolCanCommit(const StateSlotPool *pool,
                                         unsigned int capacity,
                                         unsigned int slot,
                                         unsigned int packedSize,
                                         unsigned int stagingSize) {
    return StateSlotPoolPlanReplace(pool, capacity, slot, packedSize, stagingSize) ==
           STATE_SLOT_REPLACE_STAGED;
}

static inline int StateSlotPoolClear(StateSlotPool *pool, unsigned char *bytes,
                                     unsigned int capacity, unsigned int slot) {
    if (!bytes || slot >= STATE_SLOT_POOL_COUNT ||
        !StateSlotPoolValid(pool, capacity) || !pool->slots[slot].size) return 0;
    const unsigned int offset = pool->slots[slot].offset;
    const unsigned int size = pool->slots[slot].size;
    StateSlotPoolMove(bytes + offset, bytes + offset + size,
                      pool->used - offset - size);
    for (unsigned int i = 0; i < STATE_SLOT_POOL_COUNT; ++i)
        if (pool->slots[i].size && pool->slots[i].offset > offset)
            pool->slots[i].offset -= size;
    pool->slots[slot].offset = pool->slots[slot].size = 0;
    pool->used -= size;
    return 1;
}

/* Destructive: call only after a complete count/checksum pass, while its input
 * remains immutable. The second compression must match before publication. */
static inline int StateSlotPoolReclaimForReplace(StateSlotPool *pool,
                                                 unsigned char *bytes,
                                                 unsigned int capacity,
                                                 unsigned int slot,
                                                 unsigned int packedSize) {
    if (!bytes || StateSlotPoolPlanReplace(pool, capacity, slot, packedSize, 0) ==
                      STATE_SLOT_REPLACE_REFUSE) return 0;
    if (pool->slots[slot].size)
        return StateSlotPoolClear(pool, bytes, capacity, slot);
    return 1;
}

static inline int StateSlotPoolCommitPrepared(StateSlotPool *pool,
                                             unsigned int capacity,
                                             unsigned int slot,
                                             unsigned int packedSize) {
    if (!StateSlotPoolValid(pool, capacity) || slot >= STATE_SLOT_POOL_COUNT ||
        pool->slots[slot].size || !packedSize || packedSize > capacity - pool->used)
        return 0;
    pool->slots[slot].offset = pool->used;
    pool->slots[slot].size = packedSize;
    pool->used += packedSize;
    return 1;
}

/* Compression writes staging first, then the unused pool tail. Nothing live
 * is reclaimed until the entire candidate and final capacity are known. */
static inline int StateSlotPoolCommit(StateSlotPool *pool, unsigned char *bytes,
                                      unsigned int capacity, unsigned int slot,
                                      unsigned int packedSize,
                                      const unsigned char *staging,
                                      unsigned int stagingSize) {
    if (!bytes || (stagingSize && !staging) ||
        !StateSlotPoolCanCommit(pool, capacity, slot, packedSize, stagingSize))
        return 0;
    const unsigned int oldUsed = pool->used;
    if (pool->slots[slot].size)
        StateSlotPoolClear(pool, bytes, capacity, slot);
    const unsigned int first = packedSize < stagingSize ? packedSize : stagingSize;
    /* Move the tail first: a larger replacement may overwrite its old source. */
    StateSlotPoolMove(bytes + pool->used + first, bytes + oldUsed,
                      packedSize - first);
    StateSlotPoolCopyForward(bytes + pool->used, staging, first);
    pool->slots[slot].offset = pool->used;
    pool->slots[slot].size = packedSize;
    pool->used += packedSize;
    return 1;
}

#endif
