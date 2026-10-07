#ifndef SUSAMUNE_STATE_CODEC_HXX
#define SUSAMUNE_STATE_CODEC_HXX

namespace StateCodec {

struct ReadSpan { const void *data; unsigned int size; };
struct WriteSpan { void *data; unsigned int size; };
typedef bool (*ReadWindow)(void *context, unsigned int offset, ReadSpan *out);
struct StreamSource {
    ReadSpan buffer;
    unsigned int packedBytes;
    ReadWindow read;
    void *context;
};
typedef void (*CopyBytes)(void *context, void *destination,
                          const void *source, unsigned int size);
typedef bool (*DirectBytes)(void *context, void *destination, unsigned int size);
enum Status {
    SUCCESS,
    INVALID_ARGUMENT,
    WORKSPACE_TOO_SMALL,
    OUTPUT_FULL,
    CORRUPT_STREAM,
    CODEC_ERROR,
    COMMIT_FAILED,
};
struct Result {
    Status status;
    unsigned int compressedBytes;
    unsigned int rawBytes;
    unsigned int adler32;
};

const unsigned int kMaxSpans = 64;
const unsigned int kWorkspaceLimit = 0x4E000;
const unsigned int kWorkspaceAlignment = 32;
unsigned int workspaceSize();

// Output spans are capacities, concatenated in order. A null output counts only.
// OUTPUT_FULL still reports the complete required size; partial output is invalid.
Result compress(void *workspace, unsigned int workspaceBytes,
                const ReadSpan *source, unsigned int sourceCount,
                const WriteSpan *output, unsigned int outputCount = 2,
                bool compact = false, bool quick = false);

// Re-encode immutable packed bytes without restoring the game. Both workspaces
// and all output spans must be separate from each other and the source.
Result repack(void *workspace, unsigned int workspaceBytes,
              void *packWorkspace, unsigned int packWorkspaceBytes,
              const ReadSpan *source, unsigned int sourceCount,
              const WriteSpan *output, unsigned int outputCount,
              unsigned int expectedRaw, unsigned int expectedAdler,
              bool compact = false);

// Input spans contain exactly the stream's bytes, excluding allocation padding.
Status validate(void *workspace, unsigned int workspaceBytes,
                const ReadSpan *source, unsigned int sourceCount,
                unsigned int expectedRaw, unsigned int expectedAdler);

// Checks destination descriptors as well as the whole stream without writing.
Status validateRestore(void *workspace, unsigned int workspaceBytes,
                  const ReadSpan *source, unsigned int sourceCount,
                  const WriteSpan *output, unsigned int outputCount,
                  unsigned int expectedRaw, unsigned int expectedAdler);

// Validates the whole stream before writing. Source and span descriptors must
// remain immutable, with exclusive workspace ownership, through both passes.
// COMMIT_FAILED means writes may have begun: the caller must not resume gameplay.
// An optional copy policy runs only after validation, and must not fail or mutate
// source/workspace/descriptors. The caller validates its policy before this call.
Status decompress(void *workspace, unsigned int workspaceBytes,
                  const ReadSpan *source, unsigned int sourceCount,
                  const WriteSpan *output, unsigned int outputCount,
                  unsigned int expectedRaw, unsigned int expectedAdler,
                  CopyBytes copy = 0, void *copyContext = 0);

// Only for a locally encoded or fully validated stream whose saved checksum has
// just been rechecked under exclusive ownership. Any failure may follow writes.
// The optional direct policy may permit a whole checked block to bypass copy.
// It must certify that copy would retain no bytes or redirect any writes there.
Status decompressVerified(void *workspace, unsigned int workspaceBytes,
                  const ReadSpan *source, unsigned int sourceCount,
                  const WriteSpan *output, unsigned int outputCount,
                  unsigned int expectedRaw, unsigned int expectedAdler,
                  CopyBytes copy = 0, void *copyContext = 0, DirectBytes direct = 0);

// The reader lends bytes inside its declared buffer until its next call.
// Validation does not write destinations. A writing-pass failure requires a
// separately prevalidated local recovery state; it must never resume partial state.
Status validateStream(void *workspace, unsigned int workspaceBytes,
                  const StreamSource &source, unsigned int expectedRaw,
                  unsigned int expectedAdler);
Status decompressStreamVerified(void *workspace, unsigned int workspaceBytes,
                  const StreamSource &source, const WriteSpan *output,
                  unsigned int outputCount, unsigned int expectedRaw,
                  unsigned int expectedAdler, CopyBytes copy = 0,
                  void *copyContext = 0);

} // namespace StateCodec
#endif
