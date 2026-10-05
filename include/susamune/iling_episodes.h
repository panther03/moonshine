#ifndef SUSAMUNE_ILING_EPISODES_H
#define SUSAMUNE_ILING_EPISODES_H

// Append only: each choice belongs to the IL's stable PB slot.
#define SUSAMUNE_IL_EPISODE_LIST(X) \
    X(100, "bianco_100") \
    X(101, "ricco_100") \
    X(102, "gelato_100") \
    X(103, "pinna_100") \
    X(104, "sirena_100") \
    X(105, "noki_100") \
    X(106, "pianta_100") \
    X(29, "gelato_hidden") \
    X(59, "noki_hidden") \
    X(69, "pianta_hidden") \
    X(126, "bianco_3_full_reds") \
    X(127, "bianco_6_full_reds") \
    X(128, "ricco_4_full_reds") \
    X(129, "gelato_1_full_reds") \
    X(130, "pinna_2_full_reds") \
    X(131, "pinna_6_full_reds") \
    X(132, "sirena_2_full_reds") \
    X(133, "sirena_4_full_reds") \
    X(134, "noki_6_full_reds") \
    X(135, "pianta_5_full_reds") \
    X(112, "gelato_enter_plaza")

#define SUSAMUNE_IL_EPISODE_MAGIC 0x53494550u
#define SUSAMUNE_IL_EPISODE_VERSION 1u
#define SUSAMUNE_IL_EPISODE_COUNT 21u
#define SUSAMUNE_CFG_FLAG_IL_EPISODES 0x00400000u
struct SusamuneILEpisodesCfg {
    unsigned int magic;
    unsigned short version;
    unsigned short count;
    // 0 keeps the catalogue default; 1..8 selects a numbered episode.
    unsigned char episodes[SUSAMUNE_IL_EPISODE_COUNT];
    unsigned char reserved[35];
};
#define SUSAMUNE_IL_EPISODES_CFG_OFFSET 0x1940u
#define SUSAMUNE_IL_EPISODES_PHYS_PTR \
    ((struct SusamuneILEpisodesCfg *)(SUSAMUNE_MEM2_CFG_PHYS_BASE + SUSAMUNE_IL_EPISODES_CFG_OFFSET))
#if defined(IS_EMULATOR) && IS_EMULATOR
#define SUSAMUNE_IL_EPISODES_LIVE_PTR ((struct SusamuneILEpisodesCfg *)0x71900060u)
#else
#define SUSAMUNE_IL_EPISODES_LIVE_PTR \
    ((struct SusamuneILEpisodesCfg *)(SUSAMUNE_MEM2_CFG_PPC_BASE + SUSAMUNE_IL_EPISODES_CFG_OFFSET))
#endif
typedef char susamune_il_episodes_size_check[(sizeof(struct SusamuneILEpisodesCfg) == 64) ? 1 : -1];

#endif
