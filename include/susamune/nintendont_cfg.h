#ifndef SUSAMUNE_NINTENDONT_CFG_H
#define SUSAMUNE_NINTENDONT_CFG_H

// Susamune-specific fields carried in Nintendont's existing NIN_CFG handoff.
// The launcher writes this cached-PPC copy before boot; the ARM kernel sees the
// same block through its physical alias.
#define SUSAMUNE_NIN_CFG_CONFIG_PPC_ADDR 0x93004008u
#define SUSAMUNE_NIN_CFG_BIT_DISABLE_RUMBLE 20
#define SUSAMUNE_NIN_CFG_DISABLE_RUMBLE \
    (1u << SUSAMUNE_NIN_CFG_BIT_DISABLE_RUMBLE)

// The ARM starts after the loader stages this payload and normally clears DIMM.
#define SUSAMUNE_NIN_CFG_BIT_GCN_IPL_STAGED 21
#define SUSAMUNE_NIN_CFG_GCN_IPL_STAGED \
    (1u << SUSAMUNE_NIN_CFG_BIT_GCN_IPL_STAGED)

#endif  // SUSAMUNE_NINTENDONT_CFG_H
