#include "susamune/state_crc.hxx"
#include "susamune/state_storage.h"

namespace StateCrc {
namespace {
typedef unsigned int Word __attribute__((__may_alias__));
typedef __UINTPTR_TYPE__ Address;
struct Tables { unsigned int slices[4][256]; };
static_assert(sizeof(Tables) == kWorkspaceBytes, "state CRC workspace size changed");
}

bool init(void *workspace, unsigned int workspaceBytes) {
    if (!workspace || workspaceBytes < kWorkspaceBytes ||
        (reinterpret_cast<Address>(workspace) & (kWorkspaceAlignment - 1)) ||
        reinterpret_cast<Address>(workspace) > ~Address(0) - kWorkspaceBytes)
        return false;
    Tables *tables = static_cast<Tables *>(workspace);
    const unsigned char zero = 0;
    for (unsigned int byte = 0; byte < 256; ++byte) {
        unsigned int value = SusamuneStateCrcUpdate(byte, &zero, 1);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        value = __builtin_bswap32(value);
#endif
        tables->slices[0][byte] = value;
    }
    for (unsigned int slice = 1; slice < 4; ++slice) {
        for (unsigned int byte = 0; byte < 256; ++byte) {
            const unsigned int previous = tables->slices[slice - 1][byte];
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            tables->slices[slice][byte] = (previous << 8) ^ tables->slices[0][previous >> 24];
#else
            tables->slices[slice][byte] = (previous >> 8) ^ tables->slices[0][previous & 255];
#endif
        }
    }
    return true;
}

unsigned int update(const void *workspace, unsigned int crc,
                    const void *data, unsigned int size) {
    if (!size) return crc;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    // Keep the running state and tables in native word order. The public
    // seed/result stays reflected; only these two boundary swaps are needed.
    crc = __builtin_bswap32(crc);
#define CRC_BYTE() crc = (crc << 8) ^ tables->slices[0][(crc >> 24) ^ *bytes++]
#else
#define CRC_BYTE() crc = (crc >> 8) ^ tables->slices[0][(crc ^ *bytes++) & 255]
#endif
    const Tables *tables = static_cast<const Tables *>(workspace);
    const unsigned char *bytes = static_cast<const unsigned char *>(data);
    while (size && (reinterpret_cast<Address>(bytes) & 3)) {
        CRC_BYTE();
        --size;
    }
    while (size >= 4) {
        const Word *words = reinterpret_cast<const Word *>(bytes);
        unsigned int first = crc ^ words[0];
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        crc = tables->slices[3][(first >> 24) & 255] ^
              tables->slices[2][(first >> 16) & 255] ^
              tables->slices[1][(first >> 8) & 255] ^
              tables->slices[0][first & 255];
#else
        crc = tables->slices[3][first & 255] ^
              tables->slices[2][(first >> 8) & 255] ^
              tables->slices[1][(first >> 16) & 255] ^
              tables->slices[0][(first >> 24) & 255];
#endif
        bytes += 4;
        size -= 4;
    }
    while (size) {
        CRC_BYTE();
        --size;
    }
#undef CRC_BYTE
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    crc = __builtin_bswap32(crc);
#endif
    return crc;
}

}
