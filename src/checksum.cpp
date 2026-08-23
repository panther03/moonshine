#include "susamune/checksum.hxx"

#include "susamune/ghost_format.h"

namespace Checksum {

u32 crc32(const void *data, u32 size, u32 zeroOffset, u32 zeroSize) {
    static const u32 table[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
        0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
        0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu,
    };
    const u8 *bytes = static_cast<const u8 *>(data);
    u32 crc = SUSAMUNE_GHOST_CRC32_INIT;
    for (u32 i = 0; i < size; i++) {
        const u8 value = i >= zeroOffset && i - zeroOffset < zeroSize
            ? 0
            : bytes[i];
        crc = (crc >> 4) ^ table[(crc ^ value) & 0x0fu];
        crc = (crc >> 4) ^ table[(crc ^ (value >> 4)) & 0x0fu];
    }
    return crc ^ SUSAMUNE_GHOST_CRC32_XOR_OUT;
}

}  // namespace Checksum
