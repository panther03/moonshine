#pragma once
#include "Dolphin/types.h"
class J3DModelData;

namespace GhostMarioModel {
// Bound includes retained unused blocks and allocator alignment, not raw assets.
const u32 kAllocationPreflight = 0x15400u;
const u32 kResourceSize = 117248u;
const u32 kImmutablePrefixSize = 76000u;
const u32 kImmutablePrefixChecksum = 0x0A21E8B8u;
bool prepare(J3DModelData *, u32 heapBegin, u32 heapEnd);
}
