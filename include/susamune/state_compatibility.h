#ifndef SUSAMUNE_STATE_COMPATIBILITY_H
#define SUSAMUNE_STATE_COMPATIBILITY_H

// Stable identity for the audited snapshot-17 restore contract. A change to
// restore layout or semantics needs a new contract, not a broader whitelist.
#define SUSAMUNE_STATE_COMPATIBILITY_ID 0x4D530011u

#ifdef __cplusplus
inline __attribute__((noinline))
#else
static inline
#endif
int SusamuneStateBuildCompatible(unsigned int gameVersion,
                                               unsigned int savedBuild) {
    if (gameVersion < 1u || gameVersion > 3u) return 0;
    if (savedBuild == SUSAMUNE_STATE_COMPATIBILITY_ID) return 1;
    // Whole mod-file CRCs: public V2.3.1 (0E7FDCB9), V2.3.2 (BD88B673).
    if (gameVersion == 1u) return savedBuild == 0x7D55E8F2u || savedBuild == 0x8C8B60C4u;
    if (gameVersion == 2u) return savedBuild == 0x46F87973u || savedBuild == 0xD784A910u;
    return savedBuild == 0xEA3C3DFDu || savedBuild == 0x96DD313Au;
}

#endif
