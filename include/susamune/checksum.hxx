#ifndef _SUSAMUNE_CHECKSUM_HXX
#define _SUSAMUNE_CHECKSUM_HXX

#include <Dolphin/types.h>

namespace Checksum {

u32 crc32(const void *data, u32 size, u32 zeroOffset = 0,
          u32 zeroSize = 0);

}  // namespace Checksum

#endif  // _SUSAMUNE_CHECKSUM_HXX
