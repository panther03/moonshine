#if defined(__powerpc__)
#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"
#endif

#include "../vendor/miniz/state_miniz_config.h"
#include "../vendor/miniz/miniz.c"
#include "../vendor/miniz/miniz_tdef.c"
#include "../vendor/miniz/miniz_tinfl.c"
#include "../vendor/lz4/state_lz4_config.h"
#if defined(__powerpc__)
// Keep vendor functions separate so unused LZ4 entry points can be discarded.
#pragma clang section text="" rodata="" data="" bss=""
#endif
#include "../vendor/lz4/lz4.c"
#if defined(__powerpc__)
#pragma clang section text=".foxtrot.text" rodata=".foxtrot.rodata" data=".foxtrot.data" bss=".foxtrot.bss"
#endif
#include "susamune/state_codec.hxx"

namespace StateCodec {
namespace {

typedef __UINTPTR_TYPE__ Address;
const unsigned int kMaxSize = 0xffffffffu;
struct InflateWorkspace {
    tinfl_decompressor state;
    unsigned char ring[TINFL_LZ_DICT_SIZE];
};
const unsigned int kQuickBlock = 0x20000;
const unsigned int kQuickMagic = 0x4D534C34; // MSL4, independent bounded LZ4 blocks.
struct QuickWorkspace {
    LZ4_stream_t state;
    unsigned char raw[kQuickBlock];
    unsigned char packed[LZ4_COMPRESSBOUND(kQuickBlock)];
};
const unsigned int kWorkSize =
    ((sizeof(tdefl_compressor) > sizeof(InflateWorkspace)
        ? sizeof(tdefl_compressor) : sizeof(InflateWorkspace)) + 31u) & ~31u;
static_assert(sizeof(unsigned int) == 4, "codec sizes must be 32 bits");
static_assert(kWorkSize <= kWorkspaceLimit, "savestate codec workspace exceeded");
static_assert(sizeof(QuickWorkspace) <= kWorkSize, "fast codec workspace exceeded");

bool rangeValid(const void *data, unsigned int size) {
    return !size || (data && reinterpret_cast<Address>(data) <=
                                ~Address(0) - size);
}

bool overlaps(const void *a, unsigned int as, const void *b, unsigned int bs) {
    if (!as || !bs) return false;
    const Address ap = reinterpret_cast<Address>(a);
    const Address bp = reinterpret_cast<Address>(b);
    return ap < bp + bs && bp < ap + as;
}

__attribute__((noinline)) Status checkSource(void *workspace, unsigned int workspaceBytes,
                   const ReadSpan *source, unsigned int count,
                   unsigned int *total) {
    *total = 0;
    if (!workspace || (reinterpret_cast<Address>(workspace) & 31u) ||
        !rangeValid(workspace, workspaceBytes) || !source ||
        !count || count > kMaxSpans ||
        !rangeValid(source, count * sizeof(ReadSpan))) return INVALID_ARGUMENT;
    if (workspaceBytes < kWorkSize) return WORKSPACE_TOO_SMALL;
    if (overlaps(source, count * sizeof(ReadSpan), workspace, workspaceBytes))
        return INVALID_ARGUMENT;
    for (unsigned int i = 0; i < count; ++i) {
        if (!rangeValid(source[i].data, source[i].size) ||
            source[i].size > kMaxSize - *total ||
            overlaps(source[i].data, source[i].size, workspace, workspaceBytes))
            return INVALID_ARGUMENT;
        *total += source[i].size;
    }
    return *total ? SUCCESS : INVALID_ARGUMENT;
}

Status checkOutput(void *workspace, unsigned int workspaceBytes,
                   const ReadSpan *source, unsigned int sourceCount,
                   const WriteSpan *output, unsigned int outputCount,
                   unsigned int *total) {
    *total = 0;
    if (!output || !outputCount || outputCount > kMaxSpans ||
        !rangeValid(output, outputCount * sizeof(WriteSpan)) ||
        overlaps(output, outputCount * sizeof(WriteSpan), workspace, workspaceBytes))
        return INVALID_ARGUMENT;
    for (unsigned int i = 0; i < outputCount; ++i) {
        const WriteSpan &span = output[i];
        if (!rangeValid(span.data, span.size) || span.size > kMaxSize - *total ||
            overlaps(span.data, span.size, workspace, workspaceBytes) ||
            overlaps(span.data, span.size, source, sourceCount * sizeof(ReadSpan)) ||
            overlaps(span.data, span.size, output, outputCount * sizeof(WriteSpan)))
            return INVALID_ARGUMENT;
        for (unsigned int j = 0; j < sourceCount; ++j)
            if (overlaps(span.data, span.size, source[j].data, source[j].size))
                return INVALID_ARGUMENT;
        for (unsigned int j = 0; j < i; ++j)
            if (overlaps(span.data, span.size, output[j].data, output[j].size))
                return INVALID_ARGUMENT;
        *total += span.size;
    }
    return SUCCESS;
}

struct PackSink {
    const WriteSpan *spans;
    unsigned int count;
    unsigned int written;
};

__attribute__((noinline)) int packOutput(const void *data, int length, void *context) {
    PackSink *sink = static_cast<PackSink *>(context);
    if (length < 0 || static_cast<unsigned int>(length) > kMaxSize - sink->written)
        return 0;
    unsigned int offset = sink->written;
    unsigned int remaining = static_cast<unsigned int>(length);
    sink->written += remaining;
    if (!sink->spans) return 1;
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    for (unsigned int i = 0; i < sink->count && remaining; ++i) {
        const WriteSpan &span = sink->spans[i];
        if (offset >= span.size) { offset -= span.size; continue; }
        const unsigned int room = span.size - offset;
        const unsigned int amount = remaining < room ? remaining : room;
        memcpy(static_cast<unsigned char *>(span.data) + offset, bytes, amount);
        bytes += amount;
        remaining -= amount;
        offset = 0;
    }
    // Keep counting after capacity runs out; the caller can preserve old slots.
    return 1;
}

struct ScatterSink {
    const WriteSpan *spans;
    unsigned int count, index, offset;
    CopyBytes copy;
    void *context;

    unsigned char *reserve(unsigned int size) {
        if (copy) return NULL;
        while (index < count && offset == spans[index].size) { ++index; offset = 0; }
        if (index == count || size > spans[index].size - offset) return NULL;
        unsigned char *destination = static_cast<unsigned char *>(spans[index].data) + offset;
        offset += size;
        return destination;
    }

    bool put(const unsigned char *bytes, unsigned int size) {
        while (size) {
            while (index < count && offset == spans[index].size) {
                ++index;
                offset = 0;
            }
            if (index == count) return false;
            const unsigned int room = spans[index].size - offset;
            const unsigned int amount = size < room ? size : room;
            void *destination = static_cast<unsigned char *>(spans[index].data) + offset;
            if (copy) copy(context, destination, bytes, amount);
            else memcpy(destination, bytes, amount);
            bytes += amount;
            size -= amount;
            offset += amount;
        }
        return true;
    }
};

typedef bool (*EmitBytes)(void *, const void *, unsigned int);

bool deflateBytes(void *context, const void *bytes, unsigned int size) {
    return tdefl_compress_buffer(static_cast<tdefl_compressor *>(context),
                                bytes, size, TDEFL_NO_FLUSH) == TDEFL_STATUS_OKAY;
}

struct SpanReader {
    const ReadSpan *spans;
    unsigned int count, index, offset;
    const StreamSource *stream;
    unsigned int logical;

    const unsigned char *peek(unsigned int *size) {
        *size = 0;
        if (stream) {
            if (logical == stream->packedBytes) return NULL;
            ReadSpan next = {};
            if (!stream->read(stream->context, logical, &next) || !next.size ||
                next.size > stream->packedBytes - logical || !rangeValid(next.data, next.size)) return NULL;
            const Address begin = reinterpret_cast<Address>(stream->buffer.data);
            const Address address = reinterpret_cast<Address>(next.data);
            if (address < begin || address - begin > stream->buffer.size ||
                next.size > stream->buffer.size - (address - begin)) return NULL;
            *size = next.size;
            return static_cast<const unsigned char *>(next.data);
        }
        while (index < count && offset == spans[index].size) { ++index; offset = 0; }
        if (index == count) return NULL;
        *size = spans[index].size - offset;
        return static_cast<const unsigned char *>(spans[index].data) + offset;
    }

    void skip(unsigned int size) {
        if (stream) logical += size;
        else offset += size;
    }

    __attribute__((noinline)) const unsigned char *take(unsigned int size, unsigned char *scratch) {
        unsigned int room;
        const unsigned char *data = peek(&room);
        if (data && size <= room) {
            skip(size);
            return data;
        }
        unsigned char *next = scratch;
        while (size && data) {
            const unsigned int amount = size < room ? size : room;
            memcpy(next, data, amount);
            next += amount;
            size -= amount;
            skip(amount);
            if (size) data = peek(&room);
        }
        return size ? NULL : scratch;
    }
};

unsigned int readWord(const unsigned char *p) {
    return (unsigned(p[0]) << 24) | (unsigned(p[1]) << 16) | (unsigned(p[2]) << 8) | p[3];
}

bool packWord(PackSink &sink, unsigned int value) {
    const unsigned char bytes[4] = {static_cast<unsigned char>(value >> 24),
        static_cast<unsigned char>(value >> 16), static_cast<unsigned char>(value >> 8),
        static_cast<unsigned char>(value)};
    return packOutput(bytes, sizeof(bytes), &sink) != 0;
}

Result quickPack(void *workspace, const ReadSpan *source, unsigned int sourceCount,
                 const WriteSpan *output, unsigned int outputCount,
                 unsigned int rawBytes, unsigned int capacity) {
    QuickWorkspace *work = static_cast<QuickWorkspace *>(workspace);
    SpanReader reader = {source, sourceCount, 0, 0};
    PackSink sink = {output, outputCount, 0};
    unsigned int adler = MZ_ADLER32_INIT;
    if (!packWord(sink, kQuickMagic) || !packWord(sink, kQuickBlock))
        return {CODEC_ERROR, 0, rawBytes, 0};
    for (unsigned int done = 0; done < rawBytes;) {
        const unsigned int raw = rawBytes - done < kQuickBlock ? rawBytes - done : kQuickBlock;
        const unsigned char *bytes = reader.take(raw, work->raw);
        if (!bytes) return {CODEC_ERROR, 0, rawBytes, 0};
        const int packed = LZ4_compress_fast_extState(&work->state,
            reinterpret_cast<const char *>(bytes), reinterpret_cast<char *>(work->packed),
            raw, sizeof(work->packed), 1);
        if (packed <= 0) return {CODEC_ERROR, 0, rawBytes, 0};
        adler = mz_adler32(adler, bytes, raw);
        const bool plain = static_cast<unsigned int>(packed) >= raw;
        if (!packWord(sink, raw) || !packWord(sink, plain ? raw | 0x80000000u : static_cast<unsigned int>(packed)))
            return {CODEC_ERROR, 0, rawBytes, 0};
        if (!packOutput(plain ? bytes : work->packed, plain ? raw : packed, &sink))
            return {CODEC_ERROR, 0, rawBytes, 0};
        done += raw;
    }
    return {output && sink.written > capacity ? OUTPUT_FULL : SUCCESS,
            sink.written, rawBytes, adler};
}

Status quickPass(void *workspace, const ReadSpan *source, unsigned int sourceCount,
                 unsigned int compressedBytes, const WriteSpan *output,
                 unsigned int outputCount, unsigned int expectedRaw,
                 unsigned int expectedAdler, CopyBytes copy, void *copyContext,
                 const StreamSource *stream = NULL,
                 EmitBytes emit = NULL, void *emitContext = NULL) {
    QuickWorkspace *work = static_cast<QuickWorkspace *>(workspace);
    SpanReader reader = {source, sourceCount, 0, 0, stream, 0};
    ScatterSink sink = {output, outputCount, 0, 0, copy, copyContext};
    unsigned char header[8];
    if (compressedBytes < 8) return CORRUPT_STREAM;
    const unsigned char *words = reader.take(8, header);
    if (!words || readWord(words) != kQuickMagic || readWord(words + 4) != kQuickBlock)
        return CORRUPT_STREAM;
    unsigned int consumed = 8, decoded = 0, adler = MZ_ADLER32_INIT;
    while (decoded < expectedRaw) {
        if (compressedBytes - consumed < 8) return CORRUPT_STREAM;
        words = reader.take(8, header);
        if (!words) return CORRUPT_STREAM;
        const unsigned int raw = readWord(words), size = readWord(words + 4);
        const bool plain = (size & 0x80000000u) != 0;
        const unsigned int packed = size & 0x7fffffffu;
        const unsigned int block = expectedRaw - decoded < kQuickBlock ? expectedRaw - decoded : kQuickBlock;
        consumed += 8;
        if (raw != block || !packed || packed > compressedBytes - consumed ||
            (plain ? packed != raw : packed >= raw)) return CORRUPT_STREAM;
        const unsigned char *bytes = reader.take(packed, work->packed);
        if (!bytes) return CORRUPT_STREAM;
        // Filtered restores must never write old runtime-owner bytes directly.
        unsigned char *direct = output && !plain ? sink.reserve(raw) : NULL;
        if (!plain) {
            if (LZ4_decompress_safe(reinterpret_cast<const char *>(bytes),
                    reinterpret_cast<char *>(direct ? direct : work->raw), packed, raw) != static_cast<int>(raw))
                return CORRUPT_STREAM;
            bytes = direct ? direct : work->raw;
        }
        adler = mz_adler32(adler, bytes, raw);
        if (emit && !emit(emitContext, bytes, raw)) return CODEC_ERROR;
        if (output && !direct && !sink.put(bytes, raw)) return CODEC_ERROR;
        consumed += packed;
        decoded += raw;
    }
    return consumed == compressedBytes && adler == expectedAdler ? SUCCESS : CORRUPT_STREAM;
}

Status inflatePass(void *workspace, const ReadSpan *source,
                   unsigned int sourceCount, unsigned int compressedBytes,
                   const WriteSpan *output, unsigned int outputCount,
                   unsigned int expectedRaw, unsigned int expectedAdler,
                   CopyBytes copy = 0, void *copyContext = 0,
                   const StreamSource *stream = NULL,
                   EmitBytes emit = NULL, void *emitContext = NULL) {
    SpanReader probe = {source, sourceCount, 0, 0, stream, 0};
    unsigned char prefix[4];
    const unsigned char *magic = compressedBytes >= 4 ? probe.take(4, prefix) : NULL;
    if (magic && readWord(magic) == kQuickMagic)
        return quickPass(workspace, source, sourceCount, compressedBytes, output,
                         outputCount, expectedRaw, expectedAdler, copy, copyContext,
                         stream, emit, emitContext);
    InflateWorkspace *work = static_cast<InflateWorkspace *>(workspace);
    tinfl_init(&work->state);
    ScatterSink sink = {output, outputCount, 0, 0, copy, copyContext};
    unsigned int input = 0, decoded = 0;
    SpanReader reader = {source, sourceCount, 0, 0, stream, 0};
    const unsigned char empty = 0;
    for (;;) {
        unsigned int available;
        const unsigned char *next = reader.peek(&available);
        if (!next && input != compressedBytes) return CORRUPT_STREAM;
        if (!next) next = &empty;
        size_t consumed = available;
        const unsigned int ringOffset = decoded & (TINFL_LZ_DICT_SIZE - 1);
        size_t produced = TINFL_LZ_DICT_SIZE - ringOffset;
        unsigned int flags = TINFL_FLAG_PARSE_ZLIB_HEADER;
        if (consumed < compressedBytes - input) flags |= TINFL_FLAG_HAS_MORE_INPUT;
        const size_t offered = consumed;
        const tinfl_status status = tinfl_decompress(&work->state, next, &consumed,
            work->ring, work->ring + ringOffset, &produced, flags);
        if (consumed > offered || produced > TINFL_LZ_DICT_SIZE - ringOffset ||
            produced > expectedRaw - decoded) return CORRUPT_STREAM;
        input += static_cast<unsigned int>(consumed);
        reader.skip(static_cast<unsigned int>(consumed));
        if (emit && !emit(emitContext, work->ring + ringOffset,
                          static_cast<unsigned int>(produced))) return CODEC_ERROR;
        if (output && !sink.put(work->ring + ringOffset, static_cast<unsigned int>(produced)))
            return CODEC_ERROR;
        decoded += static_cast<unsigned int>(produced);
        if (status == TINFL_STATUS_DONE)
            return input == compressedBytes && decoded == expectedRaw &&
                   tinfl_get_adler32(&work->state) == expectedAdler
                ? SUCCESS : CORRUPT_STREAM;
        if (status < 0 || (!consumed && !produced) ||
            (status == TINFL_STATUS_NEEDS_MORE_INPUT && input == compressedBytes))
            return CORRUPT_STREAM;
    }
}

} // namespace

unsigned int workspaceSize() { return kWorkSize; }

Result compress(void *workspace, unsigned int workspaceBytes,
                const ReadSpan *source, unsigned int sourceCount,
                const WriteSpan *output, unsigned int outputCount, bool compact, bool quick) {
    Result result = {INVALID_ARGUMENT, 0, 0, 0};
    result.status = checkSource(workspace, workspaceBytes, source, sourceCount,
                                &result.rawBytes);
    if (result.status != SUCCESS) return result;
    unsigned int capacity = 0;
    if (output) {
        result.status = checkOutput(workspace, workspaceBytes, source, sourceCount,
                                    output, outputCount, &capacity);
        if (result.status != SUCCESS) return result;
    }
    if (quick) return quickPack(workspace, source, sourceCount, output, outputCount,
                                result.rawBytes, capacity);
    tdefl_compressor *state = static_cast<tdefl_compressor *>(workspace);
    PackSink sink = {output, outputCount, 0};
    const unsigned int probes = compact ? 8 : 1 | TDEFL_GREEDY_PARSING_FLAG;
    if (tdefl_init(state, packOutput, &sink, TDEFL_WRITE_ZLIB_HEADER | probes) !=
        TDEFL_STATUS_OKAY) { result.status = CODEC_ERROR; return result; }
    for (unsigned int i = 0; i < sourceCount; ++i) {
        if (source[i].size && tdefl_compress_buffer(state, source[i].data,
                source[i].size, TDEFL_NO_FLUSH) != TDEFL_STATUS_OKAY) {
            result.status = CODEC_ERROR;
            return result;
        }
    }
    if (tdefl_compress_buffer(state, NULL, 0, TDEFL_FINISH) != TDEFL_STATUS_DONE) {
        result.status = CODEC_ERROR;
        return result;
    }
    result.compressedBytes = sink.written;
    result.adler32 = tdefl_get_adler32(state);
    result.status = output && sink.written > capacity ? OUTPUT_FULL : SUCCESS;
    return result;
}

Result repack(void *workspace, unsigned int workspaceBytes,
              void *packWorkspace, unsigned int packWorkspaceBytes,
              const ReadSpan *source, unsigned int sourceCount,
              const WriteSpan *output, unsigned int outputCount,
              unsigned int expectedRaw, unsigned int expectedAdler, bool compact) {
    Result result = {INVALID_ARGUMENT, 0, 0, 0};
    unsigned int packedBytes, ignored, capacity = 0;
    result.status = checkSource(workspace, workspaceBytes, source, sourceCount, &packedBytes);
    if (result.status != SUCCESS) return result;
    result.status = checkSource(packWorkspace, packWorkspaceBytes, source, sourceCount, &ignored);
    if (result.status != SUCCESS) return result;
    if (!expectedRaw || overlaps(workspace, workspaceBytes, packWorkspace, packWorkspaceBytes)) {
        result.status = INVALID_ARGUMENT;
        return result;
    }
    if (output) {
        result.status = checkOutput(workspace, workspaceBytes, source, sourceCount,
                                    output, outputCount, &capacity);
        if (result.status != SUCCESS) return result;
        result.status = checkOutput(packWorkspace, packWorkspaceBytes, source, sourceCount,
                                    output, outputCount, &ignored);
        if (result.status != SUCCESS) return result;
    }
    tdefl_compressor *state = static_cast<tdefl_compressor *>(packWorkspace);
    PackSink sink = {output, outputCount, 0};
    const unsigned int probes = compact ? 8 : 1 | TDEFL_GREEDY_PARSING_FLAG;
    if (tdefl_init(state, packOutput, &sink, TDEFL_WRITE_ZLIB_HEADER | probes) != TDEFL_STATUS_OKAY) {
        result.status = CODEC_ERROR;
        return result;
    }
    result.status = inflatePass(workspace, source, sourceCount, packedBytes, NULL, 0,
                                expectedRaw, expectedAdler, NULL, NULL, NULL, deflateBytes, state);
    if (result.status != SUCCESS) return result;
    if (tdefl_compress_buffer(state, NULL, 0, TDEFL_FINISH) != TDEFL_STATUS_DONE ||
        tdefl_get_adler32(state) != expectedAdler) {
        result.status = CODEC_ERROR;
        return result;
    }
    result.compressedBytes = sink.written;
    result.rawBytes = expectedRaw;
    result.adler32 = expectedAdler;
    result.status = output && sink.written > capacity ? OUTPUT_FULL : SUCCESS;
    return result;
}

Status validate(void *workspace, unsigned int workspaceBytes,
                const ReadSpan *source, unsigned int sourceCount,
                unsigned int expectedRaw, unsigned int expectedAdler) {
    unsigned int compressedBytes;
    const Status status = checkSource(workspace, workspaceBytes, source, sourceCount,
                                      &compressedBytes);
    if (status != SUCCESS) return status;
    if (!expectedRaw) return INVALID_ARGUMENT;
    return inflatePass(workspace, source, sourceCount, compressedBytes, NULL, 0,
                       expectedRaw, expectedAdler);
}

__attribute__((noinline)) Status validateRestore(void *workspace, unsigned int workspaceBytes,
                  const ReadSpan *source, unsigned int sourceCount,
                  const WriteSpan *output, unsigned int outputCount,
                  unsigned int expectedRaw, unsigned int expectedAdler) {
    unsigned int compressedBytes, capacity;
    Status status = checkSource(workspace, workspaceBytes, source, sourceCount,
                                &compressedBytes);
    if (status != SUCCESS) return status;
    status = checkOutput(workspace, workspaceBytes, source, sourceCount,
                         output, outputCount, &capacity);
    if (status != SUCCESS) return status;
    if (!expectedRaw || capacity != expectedRaw) return INVALID_ARGUMENT;
    return inflatePass(workspace, source, sourceCount, compressedBytes, NULL, 0,
                       expectedRaw, expectedAdler);
}

Status decompress(void *workspace, unsigned int workspaceBytes,
                  const ReadSpan *source, unsigned int sourceCount,
                  const WriteSpan *output, unsigned int outputCount,
                  unsigned int expectedRaw, unsigned int expectedAdler,
                  CopyBytes copy, void *copyContext) {
    Status status = validateRestore(workspace, workspaceBytes, source, sourceCount,
        output, outputCount, expectedRaw, expectedAdler);
    if (status != SUCCESS) return status;
    unsigned int compressedBytes = 0;
    for (unsigned int i = 0; i < sourceCount; ++i) compressedBytes += source[i].size;
    // Validated immutable input makes the second pass identical. Never disguise
    // a broken ownership invariant as a harmless preflight rejection.
    status = inflatePass(workspace, source, sourceCount, compressedBytes,
                         output, outputCount, expectedRaw, expectedAdler,
                         copy, copyContext);
    return status == SUCCESS ? SUCCESS : COMMIT_FAILED;
}

Status decompressVerified(void *workspace, unsigned int workspaceBytes,
                  const ReadSpan *source, unsigned int sourceCount,
                  const WriteSpan *output, unsigned int outputCount,
                  unsigned int expectedRaw, unsigned int expectedAdler,
                  CopyBytes copy, void *copyContext) {
    unsigned int compressedBytes, capacity;
    Status status = checkSource(workspace, workspaceBytes, source, sourceCount, &compressedBytes);
    if (status != SUCCESS) return status;
    status = checkOutput(workspace, workspaceBytes, source, sourceCount, output, outputCount, &capacity);
    if (status != SUCCESS) return status;
    if (!expectedRaw || capacity != expectedRaw) return INVALID_ARGUMENT;
    status = inflatePass(workspace, source, sourceCount, compressedBytes,
                         output, outputCount, expectedRaw, expectedAdler, copy, copyContext);
    return status == SUCCESS ? SUCCESS : COMMIT_FAILED;
}

Status validateStream(void *workspace, unsigned int workspaceBytes,
                  const StreamSource &source, unsigned int expectedRaw,
                  unsigned int expectedAdler) {
    unsigned int capacity;
    if (!source.read || !source.packedBytes || !expectedRaw ||
        overlaps(&source, sizeof(source), workspace, workspaceBytes)) return INVALID_ARGUMENT;
    Status status = checkSource(workspace, workspaceBytes, &source.buffer, 1, &capacity);
    if (status != SUCCESS) return status;
    if (overlaps(&source, sizeof(source), source.buffer.data, source.buffer.size)) return INVALID_ARGUMENT;
    return inflatePass(workspace, NULL, 0, source.packedBytes, NULL, 0,
                       expectedRaw, expectedAdler, NULL, NULL, &source);
}

Status decompressStreamVerified(void *workspace, unsigned int workspaceBytes,
                  const StreamSource &source, const WriteSpan *output,
                  unsigned int outputCount, unsigned int expectedRaw,
                  unsigned int expectedAdler, CopyBytes copy, void *copyContext) {
    unsigned int capacity;
    if (!source.read || !source.packedBytes || !expectedRaw ||
        overlaps(&source, sizeof(source), workspace, workspaceBytes)) return INVALID_ARGUMENT;
    Status status = checkSource(workspace, workspaceBytes, &source.buffer, 1, &capacity);
    if (status != SUCCESS) return status;
    if (overlaps(&source, sizeof(source), source.buffer.data, source.buffer.size) ||
        overlaps(output, outputCount * sizeof(WriteSpan), source.buffer.data, source.buffer.size)) return INVALID_ARGUMENT;
    status = checkOutput(workspace, workspaceBytes, &source.buffer, 1, output, outputCount, &capacity);
    if (status != SUCCESS) return status;
    if (capacity != expectedRaw) return INVALID_ARGUMENT;
    for (unsigned int i = 0; i < outputCount; ++i)
        if (overlaps(output[i].data, output[i].size, &source, sizeof(source))) return INVALID_ARGUMENT;
    status = inflatePass(workspace, NULL, 0, source.packedBytes, output, outputCount,
                        expectedRaw, expectedAdler, copy, copyContext, &source);
    return status == SUCCESS ? SUCCESS : COMMIT_FAILED;
}

} // namespace StateCodec
