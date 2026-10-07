#ifndef SUSAMUNE_STATE_CRC_HXX
#define SUSAMUNE_STATE_CRC_HXX

namespace StateCrc {

const unsigned int kWorkspaceBytes = 4096;
const unsigned int kWorkspaceAlignment = 4;

// Borrows the same 4 KiB of the existing idle codec workspace.
// Initialize again after any codec operation;
// retain exclusive ownership until every checksum span has been consumed.
bool init(void *workspace, unsigned int workspaceBytes);

// Same unfinished-CRC seed/result as SusamuneStateCrcUpdate. The initialized
// workspace and nonempty input must be valid, disjoint, caller-owned ranges.
unsigned int update(const void *workspace, unsigned int crc,
                    const void *data, unsigned int size);

}
#endif
