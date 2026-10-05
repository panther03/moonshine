#ifndef MOONSHINE_LAYOUT_PROFILE_H
#define MOONSHINE_LAYOUT_PROFILE_H

#include "susamune/susamune_cfg.h"

#define MOONSHINE_LAYOUT_COUNT 5u
#define MOONSHINE_LAYOUT_NAME_SIZE 16u
#define MOONSHINE_LAYOUT_MAGIC 0x4D4C5052u
#define MOONSHINE_LAYOUT_VERSION 2u
#define MOONSHINE_LAYOUT_V1_FILE_SIZE 2256u
#define MOONSHINE_LAYOUT_MAILBOX_MAGIC 0x4D4C4D42u
#define MOONSHINE_LAYOUT_MAILBOX_VERSION 2u
#define MOONSHINE_LAYOUT_CFG_FLAG 0x800000u
#define MOONSHINE_LAYOUT_MAILBOX_OFFSET 0x6800u
#define MOONSHINE_LAYOUT_MAILBOX_SIZE 0x1000u
#define MOONSHINE_LAYOUT_DIRECTORY "/layouts"

#define MOONSHINE_LAYOUT_LIST 1u
#define MOONSHINE_LAYOUT_SAVE 2u
#define MOONSHINE_LAYOUT_LOAD 3u
#define MOONSHINE_LAYOUT_ERROR_INVALID 0x10001u
#define MOONSHINE_LAYOUT_ERROR_CHANGED 0x10002u
#define MOONSHINE_LAYOUT_ERROR_EMPTY 0x10003u
#define MOONSHINE_LAYOUT_SETTINGS_COUNT 142u

struct MoonshineLayoutPayload {
    unsigned char settings[MOONSHINE_LAYOUT_SETTINGS_COUNT];
    unsigned char reserved[2];
    struct SusamuneInputDisplayCfg input;
    struct SusamuneInputStyleCfg inputStyle;
    struct SusamuneMetadataDisplayCfg metadata;
    struct SusamuneMetadataStyleCfg metadataStyle;
    struct SusamuneQftDisplayCfg qft;
    struct SusamuneCreationCfg creation;
    struct SusamuneWallkickStyleCfg wallkick;
    struct SusamuneMovementStyleCfg movement;
    struct SusamuneNativeTimerStyleCfg nativeTimer;
    struct SusamuneMarioColorsCfg mario;
    struct SusamuneFluddColorsCfg fludd;
    struct SusamunePracticeDisplayStyleCfg practiceDisplays;
};

struct MoonshineLayoutFile {
    unsigned int magic;
    unsigned short version;
    unsigned short bytes;
    unsigned int generation;
    unsigned int checksum;
    char name[MOONSHINE_LAYOUT_NAME_SIZE];
    struct MoonshineLayoutPayload layout;
};

struct MoonshineLayoutMailbox {
    unsigned int magic;
    unsigned int version;
    unsigned int requestSeq;
    unsigned int operation;
    unsigned int slot;
    unsigned int expectedGeneration;
    unsigned int reservedControl[2];
    // A pending request owns the payload until the reply is published.
    unsigned int ackSeq;
    unsigned int status;
    unsigned int presentMask;
    unsigned int badMask;
    unsigned int generations[MOONSHINE_LAYOUT_COUNT];
    char names[MOONSHINE_LAYOUT_COUNT][MOONSHINE_LAYOUT_NAME_SIZE];
    unsigned int reservedReply[3];
    struct MoonshineLayoutFile file;
};

static inline unsigned int MoonshineLayoutChecksum(const struct MoonshineLayoutFile *file) {
    const unsigned char *bytes = (const unsigned char *)file;
    unsigned int hash = 2166136261u, i;
    if (!((file->version == 1u && file->bytes == MOONSHINE_LAYOUT_V1_FILE_SIZE) ||
          (file->version == MOONSHINE_LAYOUT_VERSION && file->bytes == sizeof(*file)))) return 0;
    for (i = 0; i < file->bytes; ++i) {
        unsigned char value = i >= 12u && i < 16u ? 0 : bytes[i];
        hash = (hash ^ value) * 16777619u;
    }
    return hash;
}

static inline int MoonshineLayoutValid(const struct MoonshineLayoutFile *file) {
    return file->magic == MOONSHINE_LAYOUT_MAGIC &&
           ((file->version == 1u && file->bytes == MOONSHINE_LAYOUT_V1_FILE_SIZE) ||
            (file->version == MOONSHINE_LAYOUT_VERSION && file->bytes == sizeof(*file))) &&
           file->generation != 0 &&
           file->name[MOONSHINE_LAYOUT_NAME_SIZE - 1] == '\0' &&
           file->checksum == MoonshineLayoutChecksum(file);
}

// Upgrade only the checked transfer copy; the existing journal stays intact.
static inline void MoonshineLayoutUpgrade(struct MoonshineLayoutFile *file) {
    if (file->version != 1u) return;
    SusamunePracticeDisplayStyleFromWallkick(&file->layout.practiceDisplays, &file->layout.wallkick);
    file->version = MOONSHINE_LAYOUT_VERSION;
    file->bytes = sizeof(*file);
    file->checksum = MoonshineLayoutChecksum(file);
}

#define MOONSHINE_LAYOUT_PHYS_PTR ((struct MoonshineLayoutMailbox *) \
    (SUSAMUNE_MEM2_CFG_PHYS_BASE + MOONSHINE_LAYOUT_MAILBOX_OFFSET))
#if IS_EMULATOR
#define MOONSHINE_LAYOUT_PPC_PTR ((struct MoonshineLayoutMailbox *) \
    (SUSAMUNE_DOLPHIN_RUNTIME_PPC_BASE + MOONSHINE_LAYOUT_MAILBOX_OFFSET))
#else
#define MOONSHINE_LAYOUT_PPC_PTR ((struct MoonshineLayoutMailbox *) \
    (SUSAMUNE_MEM2_CFG_PPC_BASE + MOONSHINE_LAYOUT_MAILBOX_OFFSET))
#endif

typedef char moonshine_layout_payload_size[(sizeof(struct MoonshineLayoutPayload) == 2352) ? 1 : -1];
typedef char moonshine_layout_file_size[(sizeof(struct MoonshineLayoutFile) == 2384) ? 1 : -1];
typedef char moonshine_layout_v1_prefix[(__builtin_offsetof(struct MoonshineLayoutFile, layout.practiceDisplays) == MOONSHINE_LAYOUT_V1_FILE_SIZE) ? 1 : -1];
typedef char moonshine_layout_reply_alignment[(__builtin_offsetof(struct MoonshineLayoutMailbox, ackSeq) == 32) ? 1 : -1];
typedef char moonshine_layout_payload_alignment[(__builtin_offsetof(struct MoonshineLayoutMailbox, file) == 160) ? 1 : -1];
typedef char moonshine_layout_mailbox_size[(sizeof(struct MoonshineLayoutMailbox) <= MOONSHINE_LAYOUT_MAILBOX_SIZE) ? 1 : -1];
typedef char moonshine_layout_mailbox_gap[(MOONSHINE_LAYOUT_MAILBOX_OFFSET >= 0x6000u + SUSAMUNE_FOXTROT_MENU_RUNTIME_SIZE &&
    MOONSHINE_LAYOUT_MAILBOX_OFFSET + MOONSHINE_LAYOUT_MAILBOX_SIZE <= SUSAMUNE_SPLIT_STATS_CFG_OFFSET) ? 1 : -1];

#endif
