#ifndef SUSAMUNE_CFG_H
#define SUSAMUNE_CFG_H

#include "susamune/mem2_map.h"
#include "susamune/settings_list.h"
#include "susamune/binds_list.h"

// =====================================================================
// susamune_cfg.h
//
// The handoff block that carries persisted settings, ILing PBs and global
// progress between the Nintendont ARM kernel and the mod running on the PPC.
// Independent doorbells ask the kernel to write the ini or binary journals.
// Lives at SUSAMUNE_MEM2_CFG_* (mem2_map.h).
//
// Shared by three toolchains, so this header is plain C with no type
// dependencies -- it uses the built-in types directly rather than u32/u16,
// which each toolchain spells in its own header. Both the ARM kernel and
// the PPC are built big-endian, so the struct needs no byte swapping.
//
// Flow:
//   boot  -- kernel loads the ini and current region's PB journal, zeroes the
//            sequence fields, then flushes the block.
//   init  -- mod invalidates, validates and copies both payloads into private
//            live caches.
//   save  -- mod publishes one payload before bumping its saveSeq. The kernel
//            services that doorbell and answers through its status + ackSeq.
//
// CACHE-LINE OWNERSHIP (this is why the padding exists): the PPC flushes
// whole 32-byte cache lines, so a field the ARM writes must never share a
// line with a field the PPC writes -- a PPC writeback would otherwise clobber
// the ARM's newer value with a stale copy. Line 1 is therefore owned
// exclusively by the kernel and the mod only ever invalidates-and-reads it.
// =====================================================================

#define SUSAMUNE_CFG_MAGIC        0x53434647u  // 'SCFG'
// Bump whenever values[]/binds[] change meaning at a given index -- i.e. on any
// removal or reorder in settings_list.h / binds_list.h. Since the mod now ships
// as mod_<region>.bin separately from the launcher, a user can pair a new mod
// with an old kernel; the version check is what makes that combination fall
// back to defaults rather than scramble every setting.
//   1 -> 2: SETTING_INTRO_SKIP removed, shifting the settings after it.
#define SUSAMUNE_CFG_VERSION      2u

// Capacity of values[] / binds[]. Fixed (rather than SETTING_COUNT /
// BIND_COUNT) so the block size is stable as entries are added: a kernel and a
// mod built at different times still agree on the layout, and count/bindCount
// say how much of each is meaningful.
#define SUSAMUNE_CFG_MAX_SETTINGS 128
#define SUSAMUNE_CFG_MAX_BINDS    64

// Value meaning "the ini had no entry for this setting" -- the mod leaves the
// compiled-in default in place. Also what an absent/unparsable ini yields.
#define SUSAMUNE_CFG_UNSET        0xFFu
// Same, for a bind. 0 is a real value ("unbound"), so the sentinel has to be
// distinct from it.
#define SUSAMUNE_CFG_BIND_UNSET   0xFFFFu

// Input Display has values wider than the generic one-byte settings table
// (screen coordinates and an RGB colour), so it owns one compact, versioned
// payload at the end of the handoff block. Each field has its own unset value:
// a newer mod can therefore keep its defaults when paired with an older ini,
// while a newer launcher can safely preserve a field it does not yet expose.
#define SUSAMUNE_INPUT_CFG_MAGIC       0x53494443u  // 'SIDC'
#define SUSAMUNE_INPUT_CFG_VERSION     1u
#define SUSAMUNE_INPUT_CFG_U8_UNSET    0xFFu
#define SUSAMUNE_INPUT_CFG_U16_UNSET   0xFFFFu

#define SUSAMUNE_INPUT_VALUES_OFF      0u
#define SUSAMUNE_INPUT_VALUES_STICKS   1u
#define SUSAMUNE_INPUT_VALUES_FULL     2u

#define SUSAMUNE_INPUT_SOURCE_RAW      0u
#define SUSAMUNE_INPUT_SOURCE_PROCESSED 1u

#define SUSAMUNE_INPUT_VALUES_BELOW    0u
#define SUSAMUNE_INPUT_VALUES_ABOVE    1u
#define SUSAMUNE_INPUT_VALUES_INSIDE   2u

struct SusamuneInputDisplayCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short x;
    unsigned short y;
    unsigned char  startVisible;
    unsigned char  scale;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  brightness;
    unsigned char  valueMode;
    unsigned char  valueSource;
    unsigned char  valuePlacement;
    unsigned char  reserved[12];
};

// Appended styling keeps the established 32-byte Input Display payload and
// every later mailbox offset stable.
#define SUSAMUNE_INPUT_STYLE_MAGIC       0x53495343u  // 'SISC'
#define SUSAMUNE_INPUT_STYLE_VERSION     1u
#define SUSAMUNE_INPUT_COLOR_MAIN_STICK  0u
#define SUSAMUNE_INPUT_COLOR_C_STICK     1u
#define SUSAMUNE_INPUT_COLOR_A           2u
#define SUSAMUNE_INPUT_COLOR_B           3u
#define SUSAMUNE_INPUT_COLOR_X           4u
#define SUSAMUNE_INPUT_COLOR_Y           5u
#define SUSAMUNE_INPUT_COLOR_L           6u
#define SUSAMUNE_INPUT_COLOR_R           7u
#define SUSAMUNE_INPUT_COLOR_START       8u
#define SUSAMUNE_INPUT_COLOR_Z           9u
#define SUSAMUNE_INPUT_COLOR_VALUES      10u
#define SUSAMUNE_INPUT_COLOR_TRIGGER_OUTLINE 11u
#define SUSAMUNE_INPUT_COLOR_COUNT       12u

#define SUSAMUNE_INPUT_STYLE_OPACITY     (1u << 0)
#define SUSAMUNE_INPUT_STYLE_PADDING     (1u << 1)
#define SUSAMUNE_INPUT_STYLE_COLOR(i)    (1u << (2u + (i)))
#define SUSAMUNE_INPUT_STYLE_ALL         ((1u << (2u + SUSAMUNE_INPUT_COLOR_COUNT)) - 1u)

struct SusamuneInputStyleCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short present;
    unsigned char  elementOpacity;
    unsigned char  padding;
    unsigned char  rgb[SUSAMUNE_INPUT_COLOR_COUNT][3];
    unsigned char  reserved[18];
};

// Shared Creation payload for native HUD colours and three heapless
// custom text overlays. It is appended, so every older mailbox offset stays
// stable when a launcher and mod from adjacent builds are paired.
#define SUSAMUNE_CREATION_CFG_MAGIC       0x53435243u  // 'SCRC'
#define SUSAMUNE_CREATION_CFG_VERSION     1u
#define SUSAMUNE_CREATION_COLOR_COUNT     25u
#define SUSAMUNE_CREATION_WORD_COUNT      3u
#define SUSAMUNE_CREATION_WORD_CHARS      32u
#define SUSAMUNE_CREATION_WORD_TEXT_SIZE  (SUSAMUNE_CREATION_WORD_CHARS + 1u)
#define SUSAMUNE_CREATION_COLOR(index)    (1u << (index))

#define SUSAMUNE_CREATION_LEGACY_WATER_TEXT 0u
#define SUSAMUNE_CREATION_FLUDD_WATER      1u
#define SUSAMUNE_CREATION_TIMER_BG         2u
#define SUSAMUNE_CREATION_COIN_BG          3u
#define SUSAMUNE_CREATION_RED_BG           4u
#define SUSAMUNE_CREATION_BLUE_BG          5u
#define SUSAMUNE_CREATION_LIVES_BG         6u
#define SUSAMUNE_CREATION_SHINES_BG        7u
#define SUSAMUNE_CREATION_LIFE_TEXT         8u
#define SUSAMUNE_CREATION_TIMER_CHAR_FIRST  9u
#define SUSAMUNE_CREATION_TIMER_CHAR_COUNT 13u
#define SUSAMUNE_CREATION_TIMER_LABEL      22u
#define SUSAMUNE_CREATION_LEGACY_MARIO_HAT  23u
#define SUSAMUNE_CREATION_MENU_BG           24u
#define SUSAMUNE_CREATION_RECENT_STYLE_MAGIC 0x5249u  // 'RI'
#define SUSAMUNE_CREATION_SAVESTATE_STYLE_MAGIC 0x5353u  // 'SS'
#define SUSAMUNE_CREATION_ACHIEVEMENT_STYLE_MAGIC 0x4150u  // 'AP'
#define SUSAMUNE_CREATION_STAGE_SESSION_STYLE_MAGIC 0x53u  // 'S'

#define SUSAMUNE_WALLKICK_STYLE_MAGIC       0x53574B44u  // 'SWKD'
#define SUSAMUNE_WALLKICK_STYLE_VERSION     2u
#define SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT 7u
#define SUSAMUNE_NOTIFICATION_STYLE_MAGIC   0x4Eu  // 'N'

#define SUSAMUNE_MOVEMENT_STYLE_MAGIC        0x534D5653u  // 'SMVS'
#define SUSAMUNE_MOVEMENT_STYLE_VERSION      1u
#define SUSAMUNE_ROLLOUT_STYLE_COLOR_COUNT   5u
#define SUSAMUNE_DUST_STYLE_COLOR_COUNT      7u
#define SUSAMUNE_MOVEMENT_STYLE_COLOR_COUNT  7u

struct SusamuneCreationWordCfg {
    unsigned short x;
    unsigned short y;
    unsigned char  scale;
    unsigned char  textA;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  textBrightness;
    unsigned char  padding;
    unsigned char  visible;
    unsigned char  length;
    char           text[SUSAMUNE_CREATION_WORD_TEXT_SIZE];
    unsigned char  rgb[SUSAMUNE_CREATION_WORD_CHARS][3];
    unsigned char  reserved;
};

struct SusamuneCreationCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short reserved0;
    unsigned int   colorPresent;
    unsigned char  rgb[SUSAMUNE_CREATION_COLOR_COUNT][3];
    // These established fields now persist the Recent IL overlay layout.
    unsigned char  recentIlScale;
    unsigned short recentIlX;
    unsigned short recentIlY;
    unsigned char  recentIlPositionPresent;
    unsigned char  timerLabelVisible;
    unsigned char  timerLabelVisiblePresent;
    unsigned char  reserved1;
    struct SusamuneCreationWordCfg words[SUSAMUNE_CREATION_WORD_COUNT];
    // Optional V1 tail. reserved0 carries RECENT_STYLE_MAGIC, so a new mod can
    // safely ignore uninitialised tail bytes from an older launcher.
    unsigned char  recentIlTextRgb[3];
    unsigned char  recentIlTextA;
    unsigned char  recentIlBgR;
    unsigned char  recentIlBgG;
    unsigned char  recentIlBgB;
    unsigned char  recentIlBgA;
    unsigned char  recentIlTextBrightness;
    unsigned char  recentIlPadding;
    unsigned char  reserved2[6];
    // Optional cache-line-sized tail for the savestate feedback overlay.
    unsigned short savestateStyleMagic;
    unsigned short savestateX;
    unsigned short savestateY;
    unsigned char  savestateScale;
    unsigned char  savestateTextA;
    unsigned char  savestateBgR;
    unsigned char  savestateBgG;
    unsigned char  savestateBgB;
    unsigned char  savestateBgA;
    unsigned char  savestateTextBrightness;
    unsigned char  savestatePadding;
    unsigned char  savestateTextRgb[3];
    // Position/scale-only achievement banner style in the old reserved tail.
    unsigned char  reservedAchievement0;
    unsigned short achievementStyleMagic;
    unsigned short achievementX;
    unsigned short achievementY;
    unsigned char  achievementScale;
    // Position/scale-only stage-session counter style in the final V1 tail.
    unsigned char  stageSessionStyleMagic;
    unsigned short stageSessionX;
    unsigned short stageSessionY;
    unsigned char  stageSessionScale;
    unsigned char  stageSessionReserved;
};

struct SusamuneWallkickStyleCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short reserved0;
    unsigned short x;
    unsigned short y;
    unsigned char  scale;
    unsigned char  textA;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  textBrightness;
    unsigned char  padding;
    unsigned char  rgb[SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT][3];
    // V2 reuses the old reserved tail. Keep the explicit pad: the shared C,
    // PPC and ARM toolchains must agree on the established 64-byte layout.
    unsigned char  notificationStyleMagic;
    unsigned short toastX;
    unsigned short toastY;
    unsigned char  toastScale;
    unsigned char  reservedNotification0;
    unsigned short pbPopupX;
    unsigned short pbPopupY;
    unsigned char  pbPopupScale;
    unsigned char  reserved1[11];
};

struct SusamuneMovementOverlayStyleCfg {
    unsigned short x;
    unsigned short y;
    unsigned char  scale;
    unsigned char  textA;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  textBrightness;
    unsigned char  padding;
    unsigned char  rgb[SUSAMUNE_MOVEMENT_STYLE_COLOR_COUNT][3];
    unsigned char  reserved[7];
};

struct SusamuneMovementStyleCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short reserved0;
    struct SusamuneMovementOverlayStyleCfg rollout;
    struct SusamuneMovementOverlayStyleCfg dust;
};

// Metadata Display keeps a compact in-game configuration plus an optional
// hand-authored template. The template is edited in susamune.ini; the game
// menu only selects it and edits the live overlay's layout.
#define SUSAMUNE_METADATA_CFG_MAGIC       0x534D4443u  // 'SMDC'
#define SUSAMUNE_METADATA_CFG_VERSION     1u
#define SUSAMUNE_METADATA_FORMAT_SIZE     240u
#define SUSAMUNE_METADATA_FORMAT_UNSET    0xFFu

#define SUSAMUNE_METADATA_LABEL_SHORT     0u
#define SUSAMUNE_METADATA_LABEL_LONG      1u
#define SUSAMUNE_METADATA_LABEL_CUSTOM    2u

#define SUSAMUNE_METADATA_FIELD_X         (1u << 0)
#define SUSAMUNE_METADATA_FIELD_Y         (1u << 1)
#define SUSAMUNE_METADATA_FIELD_Z         (1u << 2)
#define SUSAMUNE_METADATA_FIELD_ANGLE     (1u << 3)
#define SUSAMUNE_METADATA_FIELD_HSPD      (1u << 4)
#define SUSAMUNE_METADATA_FIELD_VSPD      (1u << 5)
#define SUSAMUNE_METADATA_FIELD_QF        (1u << 6)
#define SUSAMUNE_METADATA_FIELD_CANGLE    (1u << 7)
#define SUSAMUNE_METADATA_FIELD_INVINC    (1u << 8)
#define SUSAMUNE_METADATA_FIELD_GOOP      (1u << 9)
#define SUSAMUNE_METADATA_FIELD_SPIN      (1u << 10)
#define SUSAMUNE_METADATA_FIELD_ALL       ((1u << 11) - 1u)

struct SusamuneMetadataDisplayCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short x;
    unsigned short y;
    unsigned short fieldMask;
    unsigned char  startVisible;
    unsigned char  scale;
    unsigned char  labelMode;
    unsigned char  backgroundAlpha;
    char           format[SUSAMUNE_METADATA_FORMAT_SIZE];
};

// Shared presentation fields for the compact QFT readout. `present` lets a
// hand-written ini omit individual fields without reserving 0xFF as an unset
// value, so fully opaque colours remain representable.
#define SUSAMUNE_QFT_DISPLAY_CFG_MAGIC   0x53514643u  // 'SQFC'
#define SUSAMUNE_QFT_DISPLAY_CFG_VERSION 2u
#define SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS  9u

#define SUSAMUNE_QFT_DISPLAY_X           (1u << 0)
#define SUSAMUNE_QFT_DISPLAY_Y           (1u << 1)
#define SUSAMUNE_QFT_DISPLAY_SCALE       (1u << 2)
#define SUSAMUNE_QFT_DISPLAY_TEXT_R      (1u << 3)
#define SUSAMUNE_QFT_DISPLAY_TEXT_G      (1u << 4)
#define SUSAMUNE_QFT_DISPLAY_TEXT_B      (1u << 5)
#define SUSAMUNE_QFT_DISPLAY_TEXT_A      (1u << 6)
#define SUSAMUNE_QFT_DISPLAY_BG_R        (1u << 7)
#define SUSAMUNE_QFT_DISPLAY_BG_G        (1u << 8)
#define SUSAMUNE_QFT_DISPLAY_BG_B        (1u << 9)
#define SUSAMUNE_QFT_DISPLAY_BG_A        (1u << 10)
#define SUSAMUNE_QFT_DISPLAY_TEXT_BRIGHTNESS (1u << 11)
#define SUSAMUNE_QFT_DISPLAY_PADDING     (1u << 12)
#define SUSAMUNE_QFT_DISPLAY_LEADING_ZERO (1u << 13)
#define SUSAMUNE_QFT_DISPLAY_ALL         ((1u << 14) - 1u)
#define SUSAMUNE_QFT_DISPLAY_SLOT(slot)  (1u << (slot))
#define SUSAMUNE_QFT_DISPLAY_ALL_SLOTS   ((1u << SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS) - 1u)

struct SusamuneQftDisplayCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short present;
    unsigned short x;
    unsigned short y;
    unsigned char  scale;
    unsigned char  textR;
    unsigned char  textG;
    unsigned char  textB;
    unsigned char  textA;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  textBrightness;
    unsigned char  padding;
    unsigned char  reservedV1[9];
    unsigned short slotPresent;
    unsigned char  textRgb[SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS][3];
    unsigned char  leadingZero;
    unsigned char  reserved[2];
};

// Creation styling added to Metadata without moving its established 256-byte
// config or the IL PB mailbox that follows it. Version 2 gives standard fields
// stable maximum-width character ranges so changing digits cannot shift rows.
#define SUSAMUNE_METADATA_STYLE_MAGIC       0x534D5343u  // 'SMSC'
#define SUSAMUNE_METADATA_STYLE_VERSION     2u
#define SUSAMUNE_METADATA_STYLE_TEXT_SLOTS  256u
#define SUSAMUNE_METADATA_STYLE_SLOT_BYTES  32u

#define SUSAMUNE_METADATA_STYLE_TEXT_R      (1u << 0)
#define SUSAMUNE_METADATA_STYLE_TEXT_G      (1u << 1)
#define SUSAMUNE_METADATA_STYLE_TEXT_B      (1u << 2)
#define SUSAMUNE_METADATA_STYLE_TEXT_A      (1u << 3)
#define SUSAMUNE_METADATA_STYLE_BG_R        (1u << 4)
#define SUSAMUNE_METADATA_STYLE_BG_G        (1u << 5)
#define SUSAMUNE_METADATA_STYLE_BG_B        (1u << 6)
#define SUSAMUNE_METADATA_STYLE_BG_A        (1u << 7)
#define SUSAMUNE_METADATA_STYLE_BRIGHTNESS  (1u << 8)
#define SUSAMUNE_METADATA_STYLE_PADDING     (1u << 9)
#define SUSAMUNE_METADATA_STYLE_ALL         ((1u << 10) - 1u)

struct SusamuneMetadataStyleCfg {
    unsigned int   magic;
    unsigned short version;
    unsigned short present;
    unsigned char  textR;
    unsigned char  textG;
    unsigned char  textB;
    unsigned char  textA;
    unsigned char  bgR;
    unsigned char  bgG;
    unsigned char  bgB;
    unsigned char  bgA;
    unsigned char  textBrightness;
    unsigned char  padding;
    unsigned char  reserved0[14];
    unsigned char  slotPresent[SUSAMUNE_METADATA_STYLE_SLOT_BYTES];
    unsigned char  textRgb[SUSAMUNE_METADATA_STYLE_TEXT_SLOTS][3];
};

// susamune.ini had nothing for the running game version -- either the file is
// absent entirely, or it has no [settings_<region>] section for this disc. The
// kernel cannot author defaults itself -- they live in the mod's descriptor
// pools, and duplicating them in the launcher would be a second source of truth
// -- so it raises this flag and the mod answers by issuing a save() during
// init, which fills in this version's sections without disturbing the others.
#define SUSAMUNE_CFG_FLAG_NO_CONFIG 0x1u
// Kernel understands the appended inputDisplay payload and its ini section.
// The flag also prevents a new mod from reading stale bytes there when paired
// with an older launcher that only initialised the shorter struct.
#define SUSAMUNE_CFG_FLAG_INPUT_DISPLAY 0x2u
// Kernel understands metadataDisplay and [metadata_display_<region>].
#define SUSAMUNE_CFG_FLAG_METADATA_DISPLAY 0x4u
// Kernel understands the independent ILing PB payload and save doorbell.
#define SUSAMUNE_CFG_FLAG_ILING_PBS 0x8u
// Kernel understands qftDisplay and [qft_display_<region>].
#define SUSAMUNE_CFG_FLAG_QFT_DISPLAY 0x10u
// Kernel understands Metadata's appended Creation style and character colours.
#define SUSAMUNE_CFG_FLAG_METADATA_STYLE 0x20u
// Kernel understands Input Display's appended Creation colour payload.
#define SUSAMUNE_CFG_FLAG_INPUT_STYLE 0x40u
// Kernel/backend understands [creation_<region>] and the appended payload.
#define SUSAMUNE_CFG_FLAG_CREATION 0x80u
// Kernel/backend understands the appended Wallkick Display Creation style.
#define SUSAMUNE_CFG_FLAG_WALLKICK_STYLE 0x100u
// Kernel/backend understands the appended four-bank IL PB journal.
#define SUSAMUNE_CFG_FLAG_ILING_PROFILES 0x200u
// Kernel/backend exposes the independent global achievement/statistics journal.
#define SUSAMUNE_CFG_FLAG_PROGRESS 0x400u
// Kernel/backend exposes the global Stage Loader custom-playlist journal.
#define SUSAMUNE_CFG_FLAG_STAGE_PLAYLISTS 0x800u
// Kernel/backend exposes the regional IL split/statistics journal.
#define SUSAMUNE_CFG_FLAG_SPLIT_STATS 0x1000u
// Kernel/backend exposes per-region Stage Loader target times.
#define SUSAMUNE_CFG_FLAG_STAGE_TARGETS 0x2000u
// Kernel/backend understands Rollout and Dust Creation styles.
#define SUSAMUNE_CFG_FLAG_MOVEMENT_STYLE 0x4000u

// IL PBs use stable result slots. The original single-profile payload stays
// fixed at 128 values; the active profile format grows append-only beyond it.
#define SUSAMUNE_ILING_PB_MAGIC               0x53495042u  // 'SIPB'
#define SUSAMUNE_ILING_PB_FILE_MAGIC          0x53504246u  // 'SPBF'
#define SUSAMUNE_ILING_PB_VERSION             1u
#define SUSAMUNE_ILING_PB_LEGACY_SLOT_COUNT   126u
#define SUSAMUNE_ILING_PB_LEGACY_MAX_SLOTS    128u
#define SUSAMUNE_ILING_PB_SLOT_COUNT          136u
#define SUSAMUNE_ILING_PB_MAX_SLOTS           136u
#define SUSAMUNE_ILING_PB_UNSET               (-1)
#define SUSAMUNE_ILING_PB_MAX_QF              0x000AF9B0

struct SusamuneILingPbCfg {
    // --- cache line 0: written by the kernel at boot, by the mod on save ---
    unsigned int   magic;
    unsigned short version;
    unsigned short count;
    unsigned int   saveSeq;
    unsigned char  pad0[20];

    // --- cache line 1: written ONLY by the kernel ---
    unsigned int   ackSeq;
    unsigned int   status;
    unsigned char  pad1[24];

    // --- cache lines 2+: written by the kernel at boot, by the mod on save ---
    signed int values[SUSAMUNE_ILING_PB_LEGACY_MAX_SLOTS];
};

// Fixed binary record written by the ARM kernel. Two generations are kept per
// region, so an interrupted write cannot destroy the previous valid PB list.
struct SusamuneILingPbFile {
    unsigned int   magic;
    unsigned short version;
    unsigned short count;
    unsigned int   gameId;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved[12];
    signed int     values[SUSAMUNE_ILING_PB_LEGACY_MAX_SLOTS];
};

#define SUSAMUNE_ILING_PROFILE_MAGIC      0x53495052u  // 'SIPR'
#define SUSAMUNE_ILING_PROFILE_FILE_MAGIC 0x53505246u  // 'SPRF'
#define SUSAMUNE_ILING_PROFILE_VERSION_V1 1u
#define SUSAMUNE_ILING_PROFILE_VERSION    2u
#define SUSAMUNE_ILING_PROFILE_COUNT      4u
#define SUSAMUNE_ILING_CUSTOM_NAME_COUNT  2u
#define SUSAMUNE_ILING_PROFILE_NAME_SIZE  16u

// V1 is also the profile tail embedded in Dolphin CARD record versions 4/5.
// Keep it explicit so those records can be migrated without offset guesses.
struct SusamuneILingProfilesCfgV1 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  profileCount;
    unsigned char  activeProfile;
    unsigned short slotCount;
    unsigned short nameSize;
    unsigned int   saveSeq;
    unsigned char  pad0[16];
    unsigned int   ackSeq;
    unsigned int   status;
    unsigned char  pad1[24];
    signed int values[SUSAMUNE_ILING_PROFILE_COUNT]
                     [SUSAMUNE_ILING_PB_LEGACY_MAX_SLOTS];
    char customNames[SUSAMUNE_ILING_CUSTOM_NAME_COUNT]
                    [SUSAMUNE_ILING_PROFILE_NAME_SIZE];
};

struct SusamuneILingProfilesCfg {
    // --- cache line 0: written by the kernel at boot, by the mod on save ---
    unsigned int   magic;
    unsigned short version;
    unsigned char  profileCount;
    unsigned char  activeProfile;
    unsigned short slotCount;
    unsigned short nameSize;
    unsigned int   saveSeq;
    unsigned char  pad0[16];

    // --- cache line 1: written ONLY by the kernel ---
    unsigned int   ackSeq;
    unsigned int   status;
    unsigned char  pad1[24];

    signed int values[SUSAMUNE_ILING_PROFILE_COUNT]
                     [SUSAMUNE_ILING_PB_MAX_SLOTS];
    char customNames[SUSAMUNE_ILING_CUSTOM_NAME_COUNT]
                    [SUSAMUNE_ILING_PROFILE_NAME_SIZE];
};

struct SusamuneILingProfilesFile {
    unsigned int   magic;
    unsigned short version;
    unsigned char  profileCount;
    unsigned char  activeProfile;
    unsigned short slotCount;
    unsigned short nameSize;
    unsigned int   gameId;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved[8];
    signed int values[SUSAMUNE_ILING_PROFILE_COUNT]
                     [SUSAMUNE_ILING_PB_MAX_SLOTS];
    char customNames[SUSAMUNE_ILING_CUSTOM_NAME_COUNT]
                    [SUSAMUNE_ILING_PROFILE_NAME_SIZE];
};

struct SusamuneILingProfilesFileV1 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  profileCount;
    unsigned char  activeProfile;
    unsigned short slotCount;
    unsigned short nameSize;
    unsigned int   gameId;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved[8];
    signed int values[SUSAMUNE_ILING_PROFILE_COUNT]
                     [SUSAMUNE_ILING_PB_LEGACY_MAX_SLOTS];
    char customNames[SUSAMUNE_ILING_CUSTOM_NAME_COUNT]
                    [SUSAMUNE_ILING_PROFILE_NAME_SIZE];
};

// Achievements are shared across all three revisions. Statistics keep one bank
// per revision so the UI can show both regional and combined values without
// making three files fight over a read-modify-write cycle. Achievement and stat
// indices are persistent storage IDs: append new meanings, never reorder them.
#define SUSAMUNE_PROGRESS_MAGIC             0x53505247u  // 'SPRG'
#define SUSAMUNE_PROGRESS_FILE_MAGIC        0x53504746u  // 'SPGF'
#define SUSAMUNE_PROGRESS_VERSION           1u
#define SUSAMUNE_PROGRESS_ACHIEVEMENT_BITS  512u
#define SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES \
    (SUSAMUNE_PROGRESS_ACHIEVEMENT_BITS / 8u)
#define SUSAMUNE_PROGRESS_STAT_COUNT        64u
#define SUSAMUNE_PROGRESS_REGION_COUNT      3u
#define SUSAMUNE_PROGRESS_REGION_JP         0u
#define SUSAMUNE_PROGRESS_REGION_US         1u
#define SUSAMUNE_PROGRESS_REGION_PAL        2u
#define SUSAMUNE_PROGRESS_FLAG_WRITABLE     0x1u

struct SusamuneProgressCfg {
    // --- cache line 0: written by the kernel at boot, by the mod on save ---
    unsigned int   magic;
    unsigned short version;
    unsigned short achievementBytes;
    unsigned short statCount;
    unsigned char  regionCount;
    unsigned char  flags;
    unsigned int   saveSeq;
    unsigned char  pad0[16];

    // --- cache line 1: written ONLY by the kernel ---
    unsigned int   ackSeq;
    unsigned int   status;
    unsigned char  pad1[24];

    // The mod stages these from a separate live copy, flushes the whole payload,
    // then bumps saveSeq. They stay immutable until ackSeq catches up; changes
    // made meanwhile remain dirty in the live copy for a later save.
    // Bit N is (achievements[N >> 3] & (1 << (N & 7))).
    unsigned char achievements[SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES];
    unsigned int  stats[SUSAMUNE_PROGRESS_REGION_COUNT]
                       [SUSAMUNE_PROGRESS_STAT_COUNT];
};

// The launcher-device journal has no game id or region suffix: every disc
// revision reads and updates this same record. Alternating generations make a
// power loss during one write fall back to the other complete generation.
struct SusamuneProgressFile {
    unsigned int   magic;
    unsigned short version;
    unsigned short achievementBytes;
    unsigned short statCount;
    unsigned char  regionCount;
    unsigned char  reserved0;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved[12];
    unsigned char achievements[SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES];
    unsigned int  stats[SUSAMUNE_PROGRESS_REGION_COUNT]
                       [SUSAMUNE_PROGRESS_STAT_COUNT];
};

#define SUSAMUNE_STAGE_PLAYLIST_MAGIC      0x53504C53u  // 'SPLS'
#define SUSAMUNE_STAGE_PLAYLIST_FILE_MAGIC 0x53504C46u  // 'SPLF'
#define SUSAMUNE_STAGE_PLAYLIST_VERSION_V1 1u
#define SUSAMUNE_STAGE_PLAYLIST_VERSION    2u
#define SUSAMUNE_STAGE_PLAYLIST_COUNT      7u
#define SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT 3u
#define SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT \
    (SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT + SUSAMUNE_STAGE_PLAYLIST_COUNT)
#define SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT 3u
#define SUSAMUNE_STAGE_PLAYLIST_CAPACITY   120u
#define SUSAMUNE_STAGE_PLAYLIST_ROUTE_COUNT 132u
#define SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES \
    (SUSAMUNE_STAGE_PLAYLIST_CAPACITY / 8u)
#define SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA 1u
#define SUSAMUNE_STAGE_PLAYLIST_ACTION_BIANCO_1 0u
#define SUSAMUNE_STAGE_PLAYLIST_ACTION_GELATO_1 25u
#define SUSAMUNE_STAGE_PLAYLIST_ACTION_PIANTA_5 82u
#define SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET 0xFFFFFFFFu
#define SUSAMUNE_STAGE_PLAYLIST_FLAG_WRITABLE 0x1u

struct SusamuneStagePlaylistsCfg {
    // --- cache line 0: written by the kernel at boot, by the mod on save ---
    unsigned int   magic;
    unsigned short version;
    unsigned char  slotCount;
    unsigned char  capacity;
    unsigned int   saveSeq;
    unsigned int   flags;
    unsigned char  builtinCount;
    unsigned char  regionCount;
    unsigned char  actionBytes;
    unsigned char  actionSchema;
    unsigned char  pad0[12];

    // --- cache line 1: written ONLY by the kernel ---
    unsigned int   ackSeq;
    unsigned int   status;
    unsigned char  pad1[24];

    // Publish this entire payload before saveSeq, then leave it immutable
    // until the kernel acknowledges that sequence.
    unsigned char counts[SUSAMUNE_STAGE_PLAYLIST_COUNT];
    unsigned char entries[SUSAMUNE_STAGE_PLAYLIST_COUNT]
                         [SUSAMUNE_STAGE_PLAYLIST_CAPACITY];
    // A set bit is interpreted from its route id: B1 expects B2, G1 expects
    // G8, and PV5 suppresses Fast Text. Actions remain position-owned.
    unsigned char actions[SUSAMUNE_STAGE_PLAYLIST_COUNT]
                         [SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES];
    // Incremented on every custom-slot overwrite, including identical data.
    unsigned int revisions[SUSAMUNE_STAGE_PLAYLIST_COUNT];
    // Built-ins are reconciled by the PPC; custom hashes are validated here.
    unsigned int contentHashes[SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT];
    unsigned int bestQf[SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT]
                       [SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT];
    unsigned char reserved[12];
};

// Retained only to import the PR3 route-order journal. V2 writes use separate
// filenames, so rollback never sees a partially upgraded V1 record.
struct SusamuneStagePlaylistsFileV1 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  slotCount;
    unsigned char  capacity;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved0[16];
    unsigned char counts[SUSAMUNE_STAGE_PLAYLIST_COUNT];
    unsigned char entries[SUSAMUNE_STAGE_PLAYLIST_COUNT]
                         [SUSAMUNE_STAGE_PLAYLIST_CAPACITY];
    unsigned char reserved1[17];
};

struct SusamuneStagePlaylistsFile {
    unsigned int   magic;
    unsigned short version;
    unsigned char  slotCount;
    unsigned char  capacity;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  builtinCount;
    unsigned char  regionCount;
    unsigned char  actionBytes;
    unsigned char  actionSchema;
    unsigned char  reserved0[12];
    unsigned char counts[SUSAMUNE_STAGE_PLAYLIST_COUNT];
    unsigned char entries[SUSAMUNE_STAGE_PLAYLIST_COUNT]
                         [SUSAMUNE_STAGE_PLAYLIST_CAPACITY];
    unsigned char actions[SUSAMUNE_STAGE_PLAYLIST_COUNT]
                         [SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES];
    unsigned int revisions[SUSAMUNE_STAGE_PLAYLIST_COUNT];
    unsigned int contentHashes[SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT];
    unsigned int bestQf[SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT]
                       [SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT];
    unsigned char reserved1[12];
};

// Last-used Streaking target for each stable IL PB slot. This has its own
// regional journal: target edits must not rewrite playlists or PB profiles.
#define SUSAMUNE_STAGE_TARGET_MAGIC          0x53544754u  // 'STGT'
#define SUSAMUNE_STAGE_TARGET_FILE_MAGIC     0x53544746u  // 'STGF'
#define SUSAMUNE_STAGE_TARGET_VERSION_V1     1u
#define SUSAMUNE_STAGE_TARGET_VERSION        2u
#define SUSAMUNE_STAGE_TARGET_SLOT_COUNT_V1  128u
#define SUSAMUNE_STAGE_TARGET_SLOT_COUNT     136u
#define SUSAMUNE_STAGE_TARGET_UNSET          (-1)
#define SUSAMUNE_STAGE_TARGET_FLAG_WRITABLE  0x1u

struct SusamuneStageTargetsCfg {
    // --- cache line 0: written by the kernel at boot, by the mod on save ---
    unsigned int   magic;
    unsigned short version;
    unsigned short slotCount;
    unsigned int   saveSeq;
    unsigned int   flags;
    unsigned char  pad0[16];

    // --- cache line 1: written ONLY by the kernel ---
    unsigned int   ackSeq;
    unsigned int   status;
    unsigned char  pad1[24];

    signed int targets[SUSAMUNE_STAGE_TARGET_SLOT_COUNT];
};

struct SusamuneStageTargetsFile {
    unsigned int   magic;
    unsigned short version;
    unsigned short slotCount;
    unsigned int   gameId;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved[12];
    signed int targets[SUSAMUNE_STAGE_TARGET_SLOT_COUNT];
};

struct SusamuneStageTargetsFileV1 {
    unsigned int   magic;
    unsigned short version;
    unsigned short slotCount;
    unsigned int   gameId;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved[12];
    signed int targets[SUSAMUNE_STAGE_TARGET_SLOT_COUNT_V1];
};

// Stable route IDs are append-only. Older flat segment layouts remain explicit
// so migrations can copy unchanged routes by route-local segment identity.
#define SUSAMUNE_SPLIT_STATS_MAGIC          0x53535453u  // 'SSTS'
#define SUSAMUNE_SPLIT_STATS_FILE_MAGIC     0x53535446u  // 'SSTF'
#define SUSAMUNE_SPLIT_STATS_VERSION_V1     1u
#define SUSAMUNE_SPLIT_STATS_VERSION_V2     2u
#define SUSAMUNE_SPLIT_STATS_VERSION_V3     3u
#define SUSAMUNE_SPLIT_STATS_VERSION_V4     4u
#define SUSAMUNE_SPLIT_STATS_VERSION_V5     5u
#define SUSAMUNE_SPLIT_STATS_VERSION_V6     6u
#define SUSAMUNE_SPLIT_STATS_VERSION_V7     7u
#define SUSAMUNE_SPLIT_STATS_VERSION        8u
#define SUSAMUNE_SPLIT_STATS_ROUTE_COUNT    132u
#define SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT  285u
#define SUSAMUNE_SPLIT_STATS_REGION_COUNT   3u
#define SUSAMUNE_SPLIT_STATS_PROFILE_COUNT  4u
#define SUSAMUNE_SPLIT_STATS_SCHEMA_HASH    0x1AF7E430u
#define SUSAMUNE_SPLIT_STATS_V8_PREVIOUS_SCHEMA_HASH 0xD0AAE2E5u
#define SUSAMUNE_SPLIT_STATS_V7_ROUTE_COUNT   122u
#define SUSAMUNE_SPLIT_STATS_V7_SEGMENT_COUNT 275u
#define SUSAMUNE_SPLIT_STATS_V7_SCHEMA_HASH   0x8ADD6B7Du
// Earlier V7 layouts are accepted only to clear changed Bianco 2 segments.
#define SUSAMUNE_SPLIT_STATS_PREVIOUS_SCHEMA_HASH 0xB933B5ABu
#define SUSAMUNE_SPLIT_STATS_LEGACY_BIANCO_SCHEMA_HASH 0x4499A650u
#define SUSAMUNE_SPLIT_STATS_QF_UNSET       0xFFFFFFFFu
#define SUSAMUNE_SPLIT_STATS_FLAG_WRITABLE  0x1u
#define SUSAMUNE_SPLIT_STATS_FLAG_MIGRATED  0x2u

#define SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT   3u
#define SUSAMUNE_SPLIT_STATS_V1_SEGMENT_COUNT 9u
#define SUSAMUNE_SPLIT_STATS_V1_SCHEMA_HASH   0x51ADB070u
#define SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT   10u
#define SUSAMUNE_SPLIT_STATS_V2_SEGMENT_COUNT 38u
#define SUSAMUNE_SPLIT_STATS_V2_SCHEMA_HASH   0xE70B57F8u
// Exact PR7 V2 schema accepted only for migration into the current layout.
#define SUSAMUNE_SPLIT_STATS_PR7_SCHEMA_HASH  0xB9B6E310u
#define SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT   59u
#define SUSAMUNE_SPLIT_STATS_V3_SEGMENT_COUNT 212u
#define SUSAMUNE_SPLIT_STATS_V3_SCHEMA_HASH   0xD452F6FDu
#define SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT   61u
#define SUSAMUNE_SPLIT_STATS_V4_SEGMENT_COUNT 220u
#define SUSAMUNE_SPLIT_STATS_V4_SCHEMA_HASH   0x2E5CC875u
#define SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT   61u
#define SUSAMUNE_SPLIT_STATS_V5_SEGMENT_COUNT 217u
#define SUSAMUNE_SPLIT_STATS_V5_SCHEMA_HASH   0xA91743AAu
#define SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT   122u
#define SUSAMUNE_SPLIT_STATS_V6_SEGMENT_COUNT 274u
#define SUSAMUNE_SPLIT_STATS_V6_SCHEMA_HASH   0xF7EAA0C4u

struct SusamuneSplitRouteStats {
    unsigned int attempts;
    unsigned int finishes;
    unsigned int golds;
};

struct SusamuneSplitStatsPayloadV1 {
    struct SusamuneSplitRouteStats routeStats[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                                                [SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT];
    unsigned int bestQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                       [SUSAMUNE_SPLIT_STATS_V1_SEGMENT_COUNT];
    unsigned int pbIdentityQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                             [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                             [SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT];
    unsigned int pbQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                     [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                     [SUSAMUNE_SPLIT_STATS_V1_SEGMENT_COUNT];
};

struct SusamuneSplitStatsFileV1 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  routeCount;
    unsigned char  segmentCount;
    unsigned char  regionCount;
    unsigned char  profileCount;
    unsigned short payloadBytes;
    unsigned int   schemaHash;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved0[8];
    struct SusamuneSplitStatsPayloadV1 payload;
    unsigned char reserved1[8];
};

struct SusamuneSplitStatsPayloadV2 {
    struct SusamuneSplitRouteStats routeStats[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                                                [SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT];
    unsigned int bestQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                       [SUSAMUNE_SPLIT_STATS_V2_SEGMENT_COUNT];
    unsigned int pbIdentityQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                             [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                             [SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT];
    unsigned int pbQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                     [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                     [SUSAMUNE_SPLIT_STATS_V2_SEGMENT_COUNT];
};

struct SusamuneSplitStatsFileV2 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  routeCount;
    unsigned char  segmentCount;
    unsigned char  regionCount;
    unsigned char  profileCount;
    unsigned short payloadBytes;
    unsigned int   schemaHash;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved0[8];
    struct SusamuneSplitStatsPayloadV2 payload;
    unsigned char reserved1[16];
};

struct SusamuneSplitStatsPayloadV3 {
    struct SusamuneSplitRouteStats routeStats[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                                                [SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT];
    unsigned int bestQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                       [SUSAMUNE_SPLIT_STATS_V3_SEGMENT_COUNT];
    unsigned int pbIdentityQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                             [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                             [SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT];
    unsigned int pbQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                     [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                     [SUSAMUNE_SPLIT_STATS_V3_SEGMENT_COUNT];
};

struct SusamuneSplitStatsFileV3 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  routeCount;
    unsigned char  regionCount;
    unsigned short segmentCount;
    unsigned char  profileCount;
    unsigned char  headerReserved;
    unsigned int   payloadBytes;
    unsigned int   schemaHash;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved0[4];
    struct SusamuneSplitStatsPayloadV3 payload;
    unsigned char reserved1[16];
    unsigned char tailPad[4];
};

struct SusamuneSplitStatsPayloadV4 {
    struct SusamuneSplitRouteStats routeStats[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                                                [SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT];
    unsigned int bestQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                       [SUSAMUNE_SPLIT_STATS_V4_SEGMENT_COUNT];
    unsigned int pbIdentityQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                             [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                             [SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT];
    unsigned int pbQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                     [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                     [SUSAMUNE_SPLIT_STATS_V4_SEGMENT_COUNT];
};

struct SusamuneSplitStatsFileV4 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  routeCount;
    unsigned char  regionCount;
    unsigned short segmentCount;
    unsigned char  profileCount;
    unsigned char  headerReserved;
    unsigned int   payloadBytes;
    unsigned int   schemaHash;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved0[4];
    struct SusamuneSplitStatsPayloadV4 payload;
    unsigned char reserved1[16];
    unsigned char tailPad[28];
};

struct SusamuneSplitStatsPayloadV5 {
    struct SusamuneSplitRouteStats routeStats[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                                                [SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT];
    unsigned int bestQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                       [SUSAMUNE_SPLIT_STATS_V5_SEGMENT_COUNT];
    // A route/profile total owns the complete checkpoint-to-finish PB set.
    unsigned int pbIdentityQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                             [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                             [SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT];
    unsigned int pbQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                     [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                     [SUSAMUNE_SPLIT_STATS_V5_SEGMENT_COUNT];
};

struct SusamuneSplitStatsFileV5 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  routeCount;
    unsigned char  regionCount;
    unsigned short segmentCount;
    unsigned char  profileCount;
    unsigned char  headerReserved;
    unsigned int   payloadBytes;
    unsigned int   schemaHash;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved0[4];
    struct SusamuneSplitStatsPayloadV5 payload;
    unsigned char reserved1[16];
    unsigned char tailPad[208];
};

struct SusamuneSplitStatsPayloadV6 {
    struct SusamuneSplitRouteStats routeStats[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                                                [SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT];
    unsigned int playedQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                         [SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT];
    unsigned int bestQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                       [SUSAMUNE_SPLIT_STATS_V6_SEGMENT_COUNT];
    unsigned int pbIdentityQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                             [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                             [SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT];
    unsigned int pbQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                     [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                     [SUSAMUNE_SPLIT_STATS_V6_SEGMENT_COUNT];
};

struct SusamuneSplitStatsFileV6 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  routeCount;
    unsigned char  regionCount;
    unsigned short segmentCount;
    unsigned char  profileCount;
    unsigned char  headerReserved;
    unsigned int   payloadBytes;
    unsigned int   schemaHash;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved0[4];
    struct SusamuneSplitStatsPayloadV6 payload;
    unsigned char reserved1[16];
    unsigned char tailPad[440];
};

struct SusamuneSplitStatsPayloadV7 {
    struct SusamuneSplitRouteStats routeStats[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                                                [SUSAMUNE_SPLIT_STATS_V7_ROUTE_COUNT];
    unsigned int playedQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                         [SUSAMUNE_SPLIT_STATS_V7_ROUTE_COUNT];
    unsigned int bestQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                       [SUSAMUNE_SPLIT_STATS_V7_SEGMENT_COUNT];
    unsigned int pbIdentityQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                             [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                             [SUSAMUNE_SPLIT_STATS_V7_ROUTE_COUNT];
    unsigned int pbQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                     [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                     [SUSAMUNE_SPLIT_STATS_V7_SEGMENT_COUNT];
};

struct SusamuneSplitStatsFileV7 {
    unsigned int   magic;
    unsigned short version;
    unsigned char  routeCount;
    unsigned char  regionCount;
    unsigned short segmentCount;
    unsigned char  profileCount;
    unsigned char  headerReserved;
    unsigned int   payloadBytes;
    unsigned int   schemaHash;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved0[4];
    struct SusamuneSplitStatsPayloadV7 payload;
    unsigned char reserved1[16];
    unsigned char tailPad[380];
};

struct SusamuneSplitStatsPayload {
    struct SusamuneSplitRouteStats routeStats[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                                                [SUSAMUNE_SPLIT_STATS_ROUTE_COUNT];
    // Region-wide and PB-profile-independent. QF saturates after roughly
    // 414 days per IL while retaining the timer's native precision.
    unsigned int playedQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                         [SUSAMUNE_SPLIT_STATS_ROUTE_COUNT];
    unsigned int bestQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                       [SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT];
    unsigned int pbIdentityQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                             [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                             [SUSAMUNE_SPLIT_STATS_ROUTE_COUNT];
    unsigned int pbQf[SUSAMUNE_SPLIT_STATS_REGION_COUNT]
                     [SUSAMUNE_SPLIT_STATS_PROFILE_COUNT]
                     [SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT];
};

struct SusamuneSplitStatsCfg {
    // --- cache line 0: written by the kernel at boot, by the mod on save ---
    unsigned int   magic;
    unsigned short version;
    unsigned char  routeCount;
    unsigned char  regionCount;
    unsigned short segmentCount;
    unsigned char  profileCount;
    unsigned char  headerReserved;
    unsigned int   payloadBytes;
    unsigned int   schemaHash;
    unsigned int   saveSeq;
    unsigned int   flags;
    unsigned char  pad0[4];

    // --- cache line 1: written ONLY by the kernel ---
    unsigned int   ackSeq;
    unsigned int   status;
    unsigned char  pad1[24];

    struct SusamuneSplitStatsPayload payload;
    unsigned char reserved[16];
    unsigned char tailPad[100];
};

struct SusamuneSplitStatsFile {
    unsigned int   magic;
    unsigned short version;
    unsigned char  routeCount;
    unsigned char  regionCount;
    unsigned short segmentCount;
    unsigned char  profileCount;
    unsigned char  headerReserved;
    unsigned int   payloadBytes;
    unsigned int   schemaHash;
    unsigned int   generation;
    unsigned int   checksum;
    unsigned char  reserved0[4];
    struct SusamuneSplitStatsPayload payload;
    unsigned char reserved1[16];
    unsigned char tailPad[36];
};

struct SusamuneCfg {
    // --- cache line 0: written by the kernel at boot, by the mod on save ---
    unsigned int   magic;
    unsigned short version;
    unsigned short count;    // entries of values[] the writer filled in
    unsigned int   saveSeq;  // mod -> kernel: bump to request an ini write
    unsigned int   flags;    // SUSAMUNE_CFG_FLAG_*
    // Entries of binds[] the writer filled in. Zero from a kernel built before
    // binds existed (it memsets only as much of the block as it knows about),
    // which is exactly the "no persisted binds -- keep the defaults" answer.
    unsigned short bindCount;
    unsigned char  pad0[14];

    // --- cache line 1: written ONLY by the kernel ---
    unsigned int   ackSeq;   // kernel -> mod: echoes saveSeq once written
    unsigned int   status;   // kernel -> mod: 0 = ok, else the FatFS FRESULT
    unsigned char  pad1[24];

    // --- cache line 2+: written by the kernel at boot, by the mod on save ---
    unsigned char  values[SUSAMUNE_CFG_MAX_SETTINGS];
    unsigned short binds[SUSAMUNE_CFG_MAX_BINDS];
    struct SusamuneInputDisplayCfg inputDisplay;
    struct SusamuneMetadataDisplayCfg metadataDisplay;
    struct SusamuneILingPbCfg ilingPbs;
    // Appended after the independent PB mailbox so its established offsets do
    // not move when an old launcher and a new mod are paired (or vice versa).
    struct SusamuneQftDisplayCfg qftDisplay;
    struct SusamuneMetadataStyleCfg metadataStyle;
    struct SusamuneInputStyleCfg inputStyle;
    struct SusamuneCreationCfg creation;
    struct SusamuneWallkickStyleCfg wallkickStyle;
    struct SusamuneILingProfilesCfg ilingProfiles;
    // Optional tail: older launchers stop at ilingProfiles.
    struct SusamuneMovementStyleCfg movementStyle;
};

#define SUSAMUNE_CFG_PPC_PTR  ((struct SusamuneCfg *)SUSAMUNE_MEM2_CFG_PPC_BASE)
#define SUSAMUNE_CFG_PHYS_PTR ((struct SusamuneCfg *)SUSAMUNE_MEM2_CFG_PHYS_BASE)

// Keep progress outside SusamuneCfg itself at a fixed ABI offset. Do not derive
// this from sizeof(SusamuneCfg): later settings payloads may grow without moving
// a progress mailbox shared with an adjacent launcher version. The gap also
// preserves the v1.1 Dolphin CARD record format.
#define SUSAMUNE_PROGRESS_CFG_OFFSET 0x2000u
#define SUSAMUNE_PROGRESS_PPC_PTR \
    ((struct SusamuneProgressCfg *)(SUSAMUNE_MEM2_CFG_PPC_BASE + \
                                    SUSAMUNE_PROGRESS_CFG_OFFSET))
#define SUSAMUNE_PROGRESS_PHYS_PTR \
    ((struct SusamuneProgressCfg *)(SUSAMUNE_MEM2_CFG_PHYS_BASE + \
                                    SUSAMUNE_PROGRESS_CFG_OFFSET))

// Fixed gap before progress. This remains separate from SusamuneCfg so older
// launchers can keep their established struct size and offsets.
#define SUSAMUNE_STAGE_PLAYLIST_CFG_OFFSET 0x1420u
#define SUSAMUNE_STAGE_PLAYLIST_PPC_PTR \
    ((struct SusamuneStagePlaylistsCfg *)(SUSAMUNE_MEM2_CFG_PPC_BASE + \
                                          SUSAMUNE_STAGE_PLAYLIST_CFG_OFFSET))
#define SUSAMUNE_STAGE_PLAYLIST_PHYS_PTR \
    ((struct SusamuneStagePlaylistsCfg *)(SUSAMUNE_MEM2_CFG_PHYS_BASE + \
                                          SUSAMUNE_STAGE_PLAYLIST_CFG_OFFSET))

#define SUSAMUNE_STAGE_TARGETS_PPC_PTR \
    ((struct SusamuneStageTargetsCfg *)SUSAMUNE_MEM2_STAGE_TARGETS_PPC_BASE)
#define SUSAMUNE_STAGE_TARGETS_PHYS_PTR \
    ((struct SusamuneStageTargetsCfg *)SUSAMUNE_CONSOLE_STAGE_TARGETS_PHYS_BASE)

// The mailbox ends immediately before the live PB mirror. SplitStats keeps its
// mutable copy in mod BSS so the larger all-IL schema does not need two copies
// in this 64 KiB handoff window.
#define SUSAMUNE_SPLIT_STATS_CFG_OFFSET 0x8280u
#define SUSAMUNE_SPLIT_STATS_PPC_PTR \
    ((struct SusamuneSplitStatsCfg *)(SUSAMUNE_MEM2_CFG_PPC_BASE + \
                                      SUSAMUNE_SPLIT_STATS_CFG_OFFSET))
#define SUSAMUNE_SPLIT_STATS_PHYS_PTR \
    ((struct SusamuneSplitStatsCfg *)(SUSAMUNE_MEM2_CFG_PHYS_BASE + \
                                      SUSAMUNE_SPLIT_STATS_CFG_OFFSET))

// Path of the ini, at the root of whichever device holds it. That is the device
// the launcher was run from, which the kernel may have had to mount as a second
// volume -- see SusamuneCfgIniPath().
#define SUSAMUNE_INI_PATH "/susamune.ini"

// Section headers. [nintendont] holds the launcher's own options (game version,
// per-version disc image paths, and the Nintendont settings that used to live in
// nincfg.bin); the loader owns it and the kernel only copies it through.
#define SUSAMUNE_INI_SECTION_NINTENDONT "nintendont"
//
// Settings and binds are per game version, because a JP route's binds and a PAL
// route's are not the same thing and one launcher now serves all three discs:
// the section names carry the region tag (settings_jp, binds_pal, ...). Only the
// running version's sections are ever parsed or materialised -- the block in
// MEM2 holds exactly one set of values -- so a rewrite copies the other
// versions' sections through as text rather than round-tripping them.
#define SUSAMUNE_INI_SECTION_SETTINGS   "settings"
// One `key = A+DUp` line per configurable action; the button tokens are
// SUSAMUNE_BIND_BUTTON_LIST (binds_list.h), so both sides spell them the same
// way.
#define SUSAMUNE_INI_SECTION_BINDS      "binds"
// Position, scale, colour and optional numeric pad readout. This cannot use
// the generic settings table because several values exceed one byte.
#define SUSAMUNE_INI_SECTION_INPUT_DISPLAY "input_display"
// Native position/angle/speed/QF overlay. Its `format` key is an optional
// custom template containing live-value placeholders such as <x> and <HSpd>.
#define SUSAMUNE_INI_SECTION_METADATA_DISPLAY "metadata_display"
// Compact three-decimal QFT readout presentation, edited from Creation.
#define SUSAMUNE_INI_SECTION_QFT_DISPLAY "qft_display"
// Native HUD colours plus the custom text overlays.
#define SUSAMUNE_INI_SECTION_CREATION "creation"
// settings_jp / binds_pal / ...
#define SUSAMUNE_INI_SECTION_SEPARATOR  '_'

// Portable compile-time checks (no C11 dependency): a negative array size
// fails the build if the layout the three toolchains agree on ever drifts.
typedef char susamune_cfg_line0_check[(__builtin_offsetof(struct SusamuneCfg, ackSeq) == 32) ? 1 : -1];
typedef char susamune_cfg_line2_check[(__builtin_offsetof(struct SusamuneCfg, values) == 64) ? 1 : -1];
typedef char susamune_cfg_binds_check[(__builtin_offsetof(struct SusamuneCfg, binds) == 192) ? 1 : -1];
typedef char susamune_input_cfg_size_check[(sizeof(struct SusamuneInputDisplayCfg) == 32) ? 1 : -1];
typedef char susamune_cfg_input_check[(__builtin_offsetof(struct SusamuneCfg, inputDisplay) == 320) ? 1 : -1];
typedef char susamune_metadata_cfg_size_check[(sizeof(struct SusamuneMetadataDisplayCfg) == 256) ? 1 : -1];
typedef char susamune_cfg_metadata_check[(__builtin_offsetof(struct SusamuneCfg, metadataDisplay) == 352) ? 1 : -1];
typedef char susamune_qft_display_cfg_size_check[(sizeof(struct SusamuneQftDisplayCfg) == 64) ? 1 : -1];
typedef char susamune_qft_display_v1_tail_check[(__builtin_offsetof(struct SusamuneQftDisplayCfg, slotPresent) == 32) ? 1 : -1];
typedef char susamune_metadata_style_cfg_size_check[(sizeof(struct SusamuneMetadataStyleCfg) == 832) ? 1 : -1];
typedef char susamune_metadata_style_slots_check[(__builtin_offsetof(struct SusamuneMetadataStyleCfg, textRgb) == 64) ? 1 : -1];
typedef char susamune_input_style_cfg_size_check[(sizeof(struct SusamuneInputStyleCfg) == 64) ? 1 : -1];
typedef char susamune_creation_word_cfg_size_check[(sizeof(struct SusamuneCreationWordCfg) == 144) ? 1 : -1];
typedef char susamune_creation_cfg_size_check[(sizeof(struct SusamuneCreationCfg) == 576) ? 1 : -1];
typedef char susamune_achievement_style_offset_check[(__builtin_offsetof(struct SusamuneCreationCfg, achievementStyleMagic) == 562) ? 1 : -1];
typedef char susamune_achievement_x_offset_check[(__builtin_offsetof(struct SusamuneCreationCfg, achievementX) == 564) ? 1 : -1];
typedef char susamune_stage_session_magic_offset_check[(__builtin_offsetof(struct SusamuneCreationCfg, stageSessionStyleMagic) == 569) ? 1 : -1];
typedef char susamune_stage_session_x_offset_check[(__builtin_offsetof(struct SusamuneCreationCfg, stageSessionX) == 570) ? 1 : -1];
typedef char susamune_stage_session_y_offset_check[(__builtin_offsetof(struct SusamuneCreationCfg, stageSessionY) == 572) ? 1 : -1];
typedef char susamune_stage_session_scale_offset_check[(__builtin_offsetof(struct SusamuneCreationCfg, stageSessionScale) == 574) ? 1 : -1];
typedef char susamune_stage_session_tail_offset_check[(__builtin_offsetof(struct SusamuneCreationCfg, stageSessionReserved) == 575) ? 1 : -1];
typedef char susamune_wallkick_style_cfg_size_check[(sizeof(struct SusamuneWallkickStyleCfg) == 64) ? 1 : -1];
typedef char susamune_movement_overlay_style_cfg_size_check[(sizeof(struct SusamuneMovementOverlayStyleCfg) == 40) ? 1 : -1];
typedef char susamune_movement_style_cfg_size_check[(sizeof(struct SusamuneMovementStyleCfg) == 88) ? 1 : -1];
typedef char susamune_notification_style_offset_check[(__builtin_offsetof(struct SusamuneWallkickStyleCfg, notificationStyleMagic) == 41) ? 1 : -1];
typedef char susamune_notification_toast_offset_check[(__builtin_offsetof(struct SusamuneWallkickStyleCfg, toastX) == 42) ? 1 : -1];
typedef char susamune_notification_pb_offset_check[(__builtin_offsetof(struct SusamuneWallkickStyleCfg, pbPopupX) == 48) ? 1 : -1];
typedef char susamune_iling_pb_cfg_ack_check[(__builtin_offsetof(struct SusamuneILingPbCfg, ackSeq) == 32) ? 1 : -1];
typedef char susamune_iling_pb_cfg_values_check[(__builtin_offsetof(struct SusamuneILingPbCfg, values) == 64) ? 1 : -1];
typedef char susamune_iling_pb_cfg_size_check[(sizeof(struct SusamuneILingPbCfg) == 576) ? 1 : -1];
typedef char susamune_iling_pb_file_values_check[(__builtin_offsetof(struct SusamuneILingPbFile, values) == 32) ? 1 : -1];
typedef char susamune_iling_pb_file_size_check[(sizeof(struct SusamuneILingPbFile) == 544) ? 1 : -1];
typedef char susamune_cfg_iling_pb_check[(__builtin_offsetof(struct SusamuneCfg, ilingPbs) == 608) ? 1 : -1];
typedef char susamune_cfg_qft_display_check[(__builtin_offsetof(struct SusamuneCfg, qftDisplay) == 1184) ? 1 : -1];
typedef char susamune_cfg_metadata_style_check[(__builtin_offsetof(struct SusamuneCfg, metadataStyle) == 1248) ? 1 : -1];
typedef char susamune_cfg_input_style_check[(__builtin_offsetof(struct SusamuneCfg, inputStyle) == 2080) ? 1 : -1];
typedef char susamune_cfg_creation_check[(__builtin_offsetof(struct SusamuneCfg, creation) == 2144) ? 1 : -1];
typedef char susamune_cfg_wallkick_style_check[(__builtin_offsetof(struct SusamuneCfg, wallkickStyle) == 2720) ? 1 : -1];
typedef char susamune_iling_profiles_cfg_values_check[(__builtin_offsetof(struct SusamuneILingProfilesCfg, values) == 64) ? 1 : -1];
typedef char susamune_iling_profiles_cfg_names_check[(__builtin_offsetof(struct SusamuneILingProfilesCfg, customNames) == 2240) ? 1 : -1];
typedef char susamune_iling_profiles_cfg_size_check[(sizeof(struct SusamuneILingProfilesCfg) == 2272) ? 1 : -1];
typedef char susamune_iling_profiles_v1_cfg_names_check[(__builtin_offsetof(struct SusamuneILingProfilesCfgV1, customNames) == 2112) ? 1 : -1];
typedef char susamune_iling_profiles_v1_cfg_size_check[(sizeof(struct SusamuneILingProfilesCfgV1) == 2144) ? 1 : -1];
typedef char susamune_iling_profiles_file_values_check[(__builtin_offsetof(struct SusamuneILingProfilesFile, values) == 32) ? 1 : -1];
typedef char susamune_iling_profiles_file_names_check[(__builtin_offsetof(struct SusamuneILingProfilesFile, customNames) == 2208) ? 1 : -1];
typedef char susamune_iling_profiles_file_size_check[(sizeof(struct SusamuneILingProfilesFile) == 2240) ? 1 : -1];
typedef char susamune_iling_profiles_v1_file_names_check[(__builtin_offsetof(struct SusamuneILingProfilesFileV1, customNames) == 2080) ? 1 : -1];
typedef char susamune_iling_profiles_v1_file_size_check[(sizeof(struct SusamuneILingProfilesFileV1) == 2112) ? 1 : -1];
typedef char susamune_cfg_iling_profiles_check[(__builtin_offsetof(struct SusamuneCfg, ilingProfiles) == 2784) ? 1 : -1];
typedef char susamune_cfg_movement_style_check[(__builtin_offsetof(struct SusamuneCfg, movementStyle) == 5056) ? 1 : -1];
typedef char susamune_cfg_expanded_size_check[(sizeof(struct SusamuneCfg) == 5144) ? 1 : -1];
typedef char susamune_progress_cfg_ack_check[(__builtin_offsetof(struct SusamuneProgressCfg, ackSeq) == 32) ? 1 : -1];
typedef char susamune_progress_cfg_achievements_check[(__builtin_offsetof(struct SusamuneProgressCfg, achievements) == 64) ? 1 : -1];
typedef char susamune_progress_cfg_stats_check[(__builtin_offsetof(struct SusamuneProgressCfg, stats) == 128) ? 1 : -1];
typedef char susamune_progress_cfg_size_check[(sizeof(struct SusamuneProgressCfg) == 896) ? 1 : -1];
typedef char susamune_progress_file_achievements_check[(__builtin_offsetof(struct SusamuneProgressFile, achievements) == 32) ? 1 : -1];
typedef char susamune_progress_file_stats_check[(__builtin_offsetof(struct SusamuneProgressFile, stats) == 96) ? 1 : -1];
typedef char susamune_progress_file_size_check[(sizeof(struct SusamuneProgressFile) == 864) ? 1 : -1];
typedef char susamune_stage_playlist_cfg_ack_check[(__builtin_offsetof(struct SusamuneStagePlaylistsCfg, ackSeq) == 32) ? 1 : -1];
typedef char susamune_stage_playlist_cfg_counts_check[(__builtin_offsetof(struct SusamuneStagePlaylistsCfg, counts) == 64) ? 1 : -1];
typedef char susamune_stage_playlist_cfg_entries_check[(__builtin_offsetof(struct SusamuneStagePlaylistsCfg, entries) == 71) ? 1 : -1];
typedef char susamune_stage_playlist_cfg_actions_check[(__builtin_offsetof(struct SusamuneStagePlaylistsCfg, actions) == 911) ? 1 : -1];
typedef char susamune_stage_playlist_cfg_revisions_check[(__builtin_offsetof(struct SusamuneStagePlaylistsCfg, revisions) == 1016) ? 1 : -1];
typedef char susamune_stage_playlist_cfg_hashes_check[(__builtin_offsetof(struct SusamuneStagePlaylistsCfg, contentHashes) == 1044) ? 1 : -1];
typedef char susamune_stage_playlist_cfg_bests_check[(__builtin_offsetof(struct SusamuneStagePlaylistsCfg, bestQf) == 1084) ? 1 : -1];
typedef char susamune_stage_playlist_cfg_size_check[(sizeof(struct SusamuneStagePlaylistsCfg) == 1216) ? 1 : -1];
typedef char susamune_stage_playlist_v1_counts_check[(__builtin_offsetof(struct SusamuneStagePlaylistsFileV1, counts) == 32) ? 1 : -1];
typedef char susamune_stage_playlist_v1_size_check[(sizeof(struct SusamuneStagePlaylistsFileV1) == 896) ? 1 : -1];
typedef char susamune_stage_playlist_file_counts_check[(__builtin_offsetof(struct SusamuneStagePlaylistsFile, counts) == 32) ? 1 : -1];
typedef char susamune_stage_playlist_file_entries_check[(__builtin_offsetof(struct SusamuneStagePlaylistsFile, entries) == 39) ? 1 : -1];
typedef char susamune_stage_playlist_file_actions_check[(__builtin_offsetof(struct SusamuneStagePlaylistsFile, actions) == 879) ? 1 : -1];
typedef char susamune_stage_playlist_file_revisions_check[(__builtin_offsetof(struct SusamuneStagePlaylistsFile, revisions) == 984) ? 1 : -1];
typedef char susamune_stage_playlist_file_hashes_check[(__builtin_offsetof(struct SusamuneStagePlaylistsFile, contentHashes) == 1012) ? 1 : -1];
typedef char susamune_stage_playlist_file_bests_check[(__builtin_offsetof(struct SusamuneStagePlaylistsFile, bestQf) == 1052) ? 1 : -1];
typedef char susamune_stage_playlist_file_size_check[(sizeof(struct SusamuneStagePlaylistsFile) == 1184) ? 1 : -1];
typedef char susamune_stage_playlist_payload_size_check[
    (sizeof(struct SusamuneStagePlaylistsCfg) -
         __builtin_offsetof(struct SusamuneStagePlaylistsCfg, counts) ==
     sizeof(struct SusamuneStagePlaylistsFile) -
         __builtin_offsetof(struct SusamuneStagePlaylistsFile, counts)) ? 1 : -1];
typedef char susamune_stage_targets_cfg_values_check[
    (__builtin_offsetof(struct SusamuneStageTargetsCfg, targets) == 0x40) ? 1 : -1];
typedef char susamune_stage_targets_cfg_size_check[
    (sizeof(struct SusamuneStageTargetsCfg) == SUSAMUNE_STAGE_TARGETS_CFG_SIZE) ? 1 : -1];
typedef char susamune_stage_targets_file_values_check[
    (__builtin_offsetof(struct SusamuneStageTargetsFile, targets) == 0x20) ? 1 : -1];
typedef char susamune_stage_targets_file_size_check[
    (sizeof(struct SusamuneStageTargetsFile) == 0x240) ? 1 : -1];
typedef char susamune_stage_targets_v1_file_size_check[
    (sizeof(struct SusamuneStageTargetsFileV1) == 0x220) ? 1 : -1];
typedef char susamune_split_route_stats_size_check[
    (sizeof(struct SusamuneSplitRouteStats) == 12) ? 1 : -1];
typedef char susamune_split_v1_payload_size_check[
    (sizeof(struct SusamuneSplitStatsPayloadV1) == 0x318) ? 1 : -1];
typedef char susamune_split_v1_file_size_check[
    (sizeof(struct SusamuneSplitStatsFileV1) == 0x340) ? 1 : -1];
typedef char susamune_split_v2_payload_size_check[
    (sizeof(struct SusamuneSplitStatsPayloadV2) == 0xC30) ? 1 : -1];
typedef char susamune_split_v2_file_size_check[
    (sizeof(struct SusamuneSplitStatsFileV2) == 0xC60) ? 1 : -1];
typedef char susamune_split_v3_payload_size_check[
    (sizeof(struct SusamuneSplitStatsPayloadV3) == 0x450C) ? 1 : -1];
typedef char susamune_split_v3_file_size_check[
    (sizeof(struct SusamuneSplitStatsFileV3) == 0x4540) ? 1 : -1];
typedef char susamune_split_v4_payload_size_check[
    (sizeof(struct SusamuneSplitStatsPayloadV4) == 0x4794) ? 1 : -1];
typedef char susamune_split_v4_file_size_check[
    (sizeof(struct SusamuneSplitStatsFileV4) == 0x47E0) ? 1 : -1];
typedef char susamune_split_v5_payload_size_check[
    (sizeof(struct SusamuneSplitStatsPayloadV5) == 0x46E0) ? 1 : -1];
typedef char susamune_split_v5_file_size_check[
    (sizeof(struct SusamuneSplitStatsFileV5) == 0x47E0) ? 1 : -1];
typedef char susamune_split_v6_payload_size_check[
    (sizeof(struct SusamuneSplitStatsPayloadV6) == 0x6DF8) ? 1 : -1];
typedef char susamune_split_v6_file_size_check[
    (sizeof(struct SusamuneSplitStatsFileV6) == 0x6FE0) ? 1 : -1];
typedef char susamune_split_v7_payload_size_check[
    (sizeof(struct SusamuneSplitStatsPayloadV7) == 0x6E34) ? 1 : -1];
typedef char susamune_split_v7_file_size_check[
    (sizeof(struct SusamuneSplitStatsFileV7) == 0x6FE0) ? 1 : -1];
typedef char susamune_split_payload_size_check[
    (sizeof(struct SusamuneSplitStatsPayload) == 0x744C) ? 1 : -1];
typedef char susamune_split_cfg_ack_check[
    (__builtin_offsetof(struct SusamuneSplitStatsCfg, ackSeq) == 0x20) ? 1 : -1];
typedef char susamune_split_cfg_payload_check[
    (__builtin_offsetof(struct SusamuneSplitStatsCfg, payload) == 0x40) ? 1 : -1];
typedef char susamune_split_cfg_size_check[
    (sizeof(struct SusamuneSplitStatsCfg) == 0x7500) ? 1 : -1];
typedef char susamune_split_file_payload_check[
    (__builtin_offsetof(struct SusamuneSplitStatsFile, payload) == 0x20) ? 1 : -1];
typedef char susamune_split_file_size_check[
    (sizeof(struct SusamuneSplitStatsFile) == 0x74A0) ? 1 : -1];
typedef char susamune_progress_alignment_check[(SUSAMUNE_PROGRESS_CFG_OFFSET % 32 == 0) ? 1 : -1];
typedef char susamune_stage_playlist_alignment_check[(SUSAMUNE_STAGE_PLAYLIST_CFG_OFFSET % 32 == 0) ? 1 : -1];
typedef char susamune_split_stats_alignment_check[(SUSAMUNE_SPLIT_STATS_CFG_OFFSET % 32 == 0) ? 1 : -1];
typedef char susamune_progress_cfg_gap_check[(sizeof(struct SusamuneCfg) <= SUSAMUNE_PROGRESS_CFG_OFFSET) ? 1 : -1];
typedef char susamune_stage_playlist_cfg_gap_check[
    (sizeof(struct SusamuneCfg) <= SUSAMUNE_STAGE_PLAYLIST_CFG_OFFSET &&
     SUSAMUNE_STAGE_PLAYLIST_CFG_OFFSET +
             sizeof(struct SusamuneStagePlaylistsCfg) <=
         SUSAMUNE_PROGRESS_CFG_OFFSET) ? 1 : -1];
typedef char susamune_split_stats_console_gap_check[
    (SUSAMUNE_SPLIT_STATS_CFG_OFFSET + sizeof(struct SusamuneSplitStatsCfg) ==
          SUSAMUNE_MEM2_PB_LIVE_PPC_BASE - SUSAMUNE_MEM2_CFG_PPC_BASE) ? 1 : -1];
typedef char susamune_split_stats_dolphin_gap_check[
    (SUSAMUNE_SPLIT_STATS_CFG_OFFSET + sizeof(struct SusamuneSplitStatsCfg) ==
     SUSAMUNE_DOLPHIN_PB_LIVE_PPC_BASE - SUSAMUNE_DOLPHIN_RUNTIME_PPC_BASE) ? 1 : -1];
typedef char susamune_split_stats_stage_target_gap_check[
    (SUSAMUNE_CONSOLE_STAGE_TARGETS_PPC_BASE +
             SUSAMUNE_STAGE_TARGETS_CFG_SIZE + SUSAMUNE_STAGE_TARGETS_LIVE_SIZE <=
         SUSAMUNE_MEM2_CFG_PPC_BASE + SUSAMUNE_SPLIT_STATS_CFG_OFFSET &&
     SUSAMUNE_DOLPHIN_STAGE_TARGETS_PPC_BASE +
             SUSAMUNE_STAGE_TARGETS_CFG_SIZE + SUSAMUNE_STAGE_TARGETS_LIVE_SIZE <=
         SUSAMUNE_DOLPHIN_RUNTIME_PPC_BASE + SUSAMUNE_SPLIT_STATS_CFG_OFFSET) ? 1 : -1];
typedef char susamune_split_stats_menu_text_gap_check[
    (SUSAMUNE_CONSOLE_MENU_TEXT_PPC_BASE + SUSAMUNE_MENU_TEXT_SIZE <=
         SUSAMUNE_MEM2_CFG_PPC_BASE + SUSAMUNE_SPLIT_STATS_CFG_OFFSET &&
     SUSAMUNE_DOLPHIN_MENU_TEXT_PPC_BASE + SUSAMUNE_MENU_TEXT_SIZE <=
         SUSAMUNE_DOLPHIN_RUNTIME_PPC_BASE + SUSAMUNE_SPLIT_STATS_CFG_OFFSET) ? 1 : -1];
typedef char susamune_progress_space_check[
    (SUSAMUNE_MEM2_CFG_PPC_BASE + SUSAMUNE_PROGRESS_CFG_OFFSET +
         sizeof(struct SusamuneProgressCfg) <=
     SUSAMUNE_CONSOLE_RECORDS_PPC_BASE) ? 1 : -1];
typedef char susamune_cfg_size_check[
    (sizeof(struct SusamuneCfg) <=
     SUSAMUNE_MEM2_PB_LIVE_PPC_BASE - SUSAMUNE_MEM2_CFG_PPC_BASE) ? 1 : -1];

#endif  // SUSAMUNE_CFG_H
