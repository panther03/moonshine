#ifndef SUSAMUNE_GHOST_FORMAT_H
#define SUSAMUNE_GHOST_FORMAT_H

// Canonical ghost files and optional portable bundle indexes. Every integer is
// big endian. Readers must decode untrusted bytes instead of casting them.

#define SUSAMUNE_GHOST_FILE_MAGIC        0x53474846u  // 'SGHF'
#define SUSAMUNE_GHOST_FILE_VERSION_V1   1u
#define SUSAMUNE_GHOST_FILE_VERSION_V2   2u
#define SUSAMUNE_GHOST_FILE_VERSION_V3   3u
#define SUSAMUNE_GHOST_FILE_VERSION_V4   4u
#define SUSAMUNE_GHOST_FILE_VERSION_V5   5u
#define SUSAMUNE_GHOST_FILE_VERSION_V6   6u
#define SUSAMUNE_GHOST_FILE_VERSION      SUSAMUNE_GHOST_FILE_VERSION_V6
#define SUSAMUNE_GHOST_FILE_HEADER_SIZE  0x100u
#define SUSAMUNE_GHOST_FILE_CHECKSUM_OFFSET    0x0Cu
#define SUSAMUNE_GHOST_HEADER_CHECKSUM_OFFSET  0x10u
#define SUSAMUNE_GHOST_PAYLOAD_CHECKSUM_OFFSET 0x14u

// SGIX is for future host/import-export bundles. Console storage uses fixed
// A/B slot envelopes and never reads SGIX.
#define SUSAMUNE_GHOST_INDEX_MAGIC       0x53474958u  // 'SGIX'
#define SUSAMUNE_GHOST_INDEX_VERSION     1u
#define SUSAMUNE_GHOST_INDEX_HEADER_SIZE 0x80u
#define SUSAMUNE_GHOST_INDEX_ENTRY_SIZE  0x80u
#define SUSAMUNE_GHOST_INDEX_FILE_CHECKSUM_OFFSET    0x14u
#define SUSAMUNE_GHOST_INDEX_ENTRIES_CHECKSUM_OFFSET 0x18u

// Reflected CRC-32/ISO-HDLC. The format document defines zeroed fields and
// coverage; these constants are sufficient for both PPC and ARM validators.
#define SUSAMUNE_GHOST_CHECKSUM_CRC32 1u
#define SUSAMUNE_GHOST_CRC32_POLY      0xEDB88320u
#define SUSAMUNE_GHOST_CRC32_INIT      0xFFFFFFFFu
#define SUSAMUNE_GHOST_CRC32_XOR_OUT   0xFFFFFFFFu

#define SUSAMUNE_GHOST_GAME_ID_JP  0x474D534Au  // 'GMSJ'
#define SUSAMUNE_GHOST_GAME_ID_US  0x474D5345u  // 'GMSE'
#define SUSAMUNE_GHOST_GAME_ID_PAL 0x474D5350u  // 'GMSP'

#define SUSAMUNE_GHOST_REGION_JP    0u
#define SUSAMUNE_GHOST_REGION_US    1u
#define SUSAMUNE_GHOST_REGION_PAL   2u
#define SUSAMUNE_GHOST_REGION_COUNT 3u
#define SUSAMUNE_GHOST_DISC_REVISION 0u

#define SUSAMUNE_GHOST_PROFILE_COUNT             4u
// Historical SGIX host-index bound; console libraries use paged catalogs.
#define SUSAMUNE_GHOST_PROFILE_MAX_ENTRIES       48u
#define SUSAMUNE_GHOST_IMPORTED_PROFILE          4u
#define SUSAMUNE_GHOST_MAX_DURATION_QF           107892u
#define SUSAMUNE_GHOST_QF_MAX                    0x7FFFFFFFu
#define SUSAMUNE_GHOST_MIN_SAMPLE_COUNT          2u
#define SUSAMUNE_GHOST_MAX_SAMPLE_COUNT          26974u
#define SUSAMUNE_GHOST_POSE_SAMPLE_SIZE          16u
#define SUSAMUNE_GHOST_TRANSFORM_INTERVAL_QF     4u
#define SUSAMUNE_GHOST_POSITION_SCALE            8u
#define SUSAMUNE_GHOST_MAX_POSITION_FIXED        8000000
#define SUSAMUNE_GHOST_ANIMATION_COUNT           336u
#define SUSAMUNE_GHOST_ANIMATION_ID_MAX          335u
#define SUSAMUNE_GHOST_ANIMATION_PHASE_MAX       4095u
#define SUSAMUNE_GHOST_ANIMATION_RESERVED_MASK   0x7u
#define SUSAMUNE_GHOST_V4_ANIMATION_PHASE_MAX    255u
#define SUSAMUNE_GHOST_V4_ANIMATION_PHASE_SHIFT  7u
#define SUSAMUNE_GHOST_V4_YOSHI_SHIFT            4u
#define SUSAMUNE_GHOST_V4_YOSHI_MASK             0x7u
#define SUSAMUNE_GHOST_V4_YOSHI_NONE             0u
#define SUSAMUNE_GHOST_V4_YOSHI_GREEN            1u
#define SUSAMUNE_GHOST_V4_YOSHI_ORANGE           2u
#define SUSAMUNE_GHOST_V4_YOSHI_PURPLE           3u
#define SUSAMUNE_GHOST_V4_YOSHI_PINK             4u
#define SUSAMUNE_GHOST_V4_YOSHI_UNKNOWN          5u
#define SUSAMUNE_GHOST_V4_HELD_INDEX_MASK        0xFu
#define SUSAMUNE_GHOST_V4_HELD_UNKNOWN           0xFu
#define SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE      0x695E0u
#define SUSAMUNE_GHOST_V1_MAX_PAYLOAD_SIZE       0x695E0u
#define SUSAMUNE_GHOST_V1_MAX_FILE_SIZE          0x696E0u

#define SUSAMUNE_GHOST_V2_MAX_SEGMENTS           64u
#define SUSAMUNE_GHOST_V2_SEGMENT_SIZE           0x20u
#define SUSAMUNE_GHOST_V2_SEGMENT_TABLE_OFFSET   0x100u
#define SUSAMUNE_GHOST_V2_SEGMENT_TABLE_SIZE     0x800u
#define SUSAMUNE_GHOST_V2_SAMPLE_DATA_OFFSET     0x900u
#define SUSAMUNE_GHOST_V2_MAX_PAYLOAD_SIZE       0x69DE0u
#define SUSAMUNE_GHOST_V2_MAX_FILE_SIZE          0x69EE0u

// V3 always carries the fixed segment table. Its samples add animation while
// retaining the V1/V2 byte budget and 15-minute capacity.
#define SUSAMUNE_GHOST_V3_MAX_SEGMENTS           64u
#define SUSAMUNE_GHOST_V3_SEGMENT_SIZE           0x20u
#define SUSAMUNE_GHOST_V3_SEGMENT_TABLE_OFFSET   0x100u
#define SUSAMUNE_GHOST_V3_SEGMENT_TABLE_SIZE     0x800u
#define SUSAMUNE_GHOST_V3_SAMPLE_DATA_OFFSET     0x900u
#define SUSAMUNE_GHOST_V3_MAX_PAYLOAD_SIZE       0x69DE0u
#define SUSAMUNE_GHOST_V3_MAX_FILE_SIZE          0x69EE0u

// V4 repacks the same fixed sample/table budget with attachment state.
#define SUSAMUNE_GHOST_V4_MAX_SEGMENTS           64u
#define SUSAMUNE_GHOST_V4_SEGMENT_SIZE           0x20u
#define SUSAMUNE_GHOST_V4_SEGMENT_TABLE_OFFSET   0x100u
#define SUSAMUNE_GHOST_V4_SEGMENT_TABLE_SIZE     0x800u
#define SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET     0x900u
#define SUSAMUNE_GHOST_V4_MAX_PAYLOAD_SIZE       0x69DE0u
#define SUSAMUNE_GHOST_V4_MAX_FILE_SIZE          0x69EE0u

#define SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_COUNT 7u
#define SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE  6u
#define SUSAMUNE_GHOST_V4_ATTACHMENT_HELD_OVERFLOW    0x0001u
#define SUSAMUNE_GHOST_V4_ATTACHMENT_FLAGS             \
    SUSAMUNE_GHOST_V4_ATTACHMENT_HELD_OVERFLOW

// Transfer/storage bounds cover every currently supported canonical version.
#include "susamune/ghost_teaching.h"
static inline unsigned int SusamuneGhostInputStride(unsigned int version) {
    return version == SUSAMUNE_GHOST_FILE_VERSION_V6
        ? SUSAMUNE_GHOST_V6_INPUT_SAMPLE_SIZE : SUSAMUNE_GHOST_INPUT_SAMPLE_SIZE;
}
static inline unsigned int SusamuneGhostInputLimit(unsigned int version) {
    return version == SUSAMUNE_GHOST_FILE_VERSION_V6
        ? SUSAMUNE_GHOST_V6_INPUT_MAX_COUNT : SUSAMUNE_GHOST_INPUT_MAX_COUNT;
}
static inline unsigned int SusamuneGhostDurationLimit(unsigned int version) {
    return version == SUSAMUNE_GHOST_FILE_VERSION_V6
        ? SUSAMUNE_GHOST_V6_MAX_DURATION_QF : SUSAMUNE_GHOST_MAX_DURATION_QF;
}
static inline unsigned int SusamuneGhostPoseLimit(unsigned int version) {
    return version == SUSAMUNE_GHOST_FILE_VERSION_V6
        ? SUSAMUNE_GHOST_V6_MAX_SAMPLE_COUNT : SUSAMUNE_GHOST_MAX_SAMPLE_COUNT;
}
#define SUSAMUNE_GHOST_V5_MAX_FILE_SIZE \
    (SUSAMUNE_GHOST_V4_MAX_FILE_SIZE + SUSAMUNE_GHOST_TEACHING_MAX_SIZE)
#define SUSAMUNE_GHOST_MAX_PAYLOAD_SIZE \
    (SUSAMUNE_GHOST_V5_MAX_FILE_SIZE - SUSAMUNE_GHOST_FILE_HEADER_SIZE)
#define SUSAMUNE_GHOST_MAX_FILE_SIZE SUSAMUNE_GHOST_V5_MAX_FILE_SIZE
#define SUSAMUNE_GHOST_INDEX_MAX_FILE_SIZE        0x1880u

#define SUSAMUNE_GHOST_V2_SEGMENT_COUNT_OFFSET       184u
#define SUSAMUNE_GHOST_V2_SEGMENT_SIZE_OFFSET        186u
#define SUSAMUNE_GHOST_V2_SEGMENT_TABLE_OFFSET_FIELD 188u
#define SUSAMUNE_GHOST_V2_SEGMENT_TABLE_SIZE_OFFSET  192u
#define SUSAMUNE_GHOST_V2_SAMPLE_DATA_OFFSET_FIELD   196u
#define SUSAMUNE_GHOST_V2_SAMPLE_DATA_SIZE_OFFSET    200u
#define SUSAMUNE_GHOST_V2_SEGMENT_CHECKSUM_OFFSET    204u

#define SUSAMUNE_GHOST_V3_SEGMENT_COUNT_OFFSET       184u
#define SUSAMUNE_GHOST_V3_SEGMENT_SIZE_OFFSET        186u
#define SUSAMUNE_GHOST_V3_SEGMENT_TABLE_OFFSET_FIELD 188u
#define SUSAMUNE_GHOST_V3_SEGMENT_TABLE_SIZE_OFFSET  192u
#define SUSAMUNE_GHOST_V3_SAMPLE_DATA_OFFSET_FIELD   196u
#define SUSAMUNE_GHOST_V3_SAMPLE_DATA_SIZE_OFFSET    200u
#define SUSAMUNE_GHOST_V3_SEGMENT_CHECKSUM_OFFSET    204u

#define SUSAMUNE_GHOST_V4_SEGMENT_COUNT_OFFSET       184u
#define SUSAMUNE_GHOST_V4_SEGMENT_SIZE_OFFSET        186u
#define SUSAMUNE_GHOST_V4_SEGMENT_TABLE_OFFSET_FIELD 188u
#define SUSAMUNE_GHOST_V4_SEGMENT_TABLE_SIZE_OFFSET  192u
#define SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET_FIELD   196u
#define SUSAMUNE_GHOST_V4_SAMPLE_DATA_SIZE_OFFSET    200u
#define SUSAMUNE_GHOST_V4_SEGMENT_CHECKSUM_OFFSET    204u
#define SUSAMUNE_GHOST_V4_ATTACHMENT_COUNT_OFFSET    208u
#define SUSAMUNE_GHOST_V4_ATTACHMENT_SIZE_OFFSET     209u
#define SUSAMUNE_GHOST_V4_ATTACHMENT_FLAGS_OFFSET    210u
#define SUSAMUNE_GHOST_V4_ATTACHMENT_RESERVED_OFFSET 212u
#define SUSAMUNE_GHOST_V4_ATTACHMENT_TABLE_OFFSET    214u

#define SUSAMUNE_GHOST_RECORDING_TRANSFORM_QF 1u
#define SUSAMUNE_GHOST_RECORDING_POSE_QF      2u
#define SUSAMUNE_GHOST_CODEC_RAW              0u
#define SUSAMUNE_GHOST_CODEC_POSE_ATTACHMENTS 1u

// A future codec must set this required bit. V1 readers support no required
// feature bits and refuse the file instead of guessing how to decode it.
#define SUSAMUNE_GHOST_REQUIRED_EXTENDED_CODEC       0x00000001u
#define SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V1 0u
#define SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V2 0u
#define SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V3 0u
#define SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V4 \
    SUSAMUNE_GHOST_REQUIRED_EXTENDED_CODEC
#define SUSAMUNE_GHOST_REQUIRED_TEACHING 0x00000002u
#define SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V5 \
    (SUSAMUNE_GHOST_REQUIRED_EXTENDED_CODEC | SUSAMUNE_GHOST_REQUIRED_TEACHING)
#define SUSAMUNE_GHOST_REQUIRED_FLUDD 0x00000004u
#define SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V6 \
    (SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V5 | SUSAMUNE_GHOST_REQUIRED_FLUDD)
#define SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES \
    SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V6

// Run flags are advisory metadata. Unknown bits are preserved and ignored.
#define SUSAMUNE_GHOST_RUN_ASSISTED          0x00000001u
#define SUSAMUNE_GHOST_RUN_SAVESTATE_USED    0x00000002u
#define SUSAMUNE_GHOST_RUN_FAST_FORWARD_USED 0x00000004u
#define SUSAMUNE_GHOST_RUN_INCOMPLETE        0x00000008u
#define SUSAMUNE_GHOST_RUN_CUSTOM_ROUTE      0x00000010u
#define SUSAMUNE_GHOST_RUN_TAS               0x00000020u

static inline int SusamuneGhostRunFlagsValid(unsigned int flags) {
    return !(flags & SUSAMUNE_GHOST_RUN_TAS) ||
           (flags & SUSAMUNE_GHOST_RUN_ASSISTED);
}

#define SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE 0x01u
#define SUSAMUNE_GHOST_ROUTE_PARENT_START   0x02u
// INTERNAL_SCENE is present exactly when parentArea is not PARENT_NONE;
// PARENT_START is only valid for that same parent-backed route.
#define SUSAMUNE_GHOST_ROUTE_FLAGS_V1       0x03u
#define SUSAMUNE_GHOST_ROUTE_PARENT_NONE    0xFFu
#define SUSAMUNE_GHOST_ROUTE_VARIANT_NONE   (-1)
#define SUSAMUNE_GHOST_ROUTE_AREA_MAX       0x3Cu
#define SUSAMUNE_GHOST_ROUTE_EPISODE_MAX    9u
#define SUSAMUNE_GHOST_ROUTE_VARIANT_MAX    255
#define SUSAMUNE_GHOST_RESULT_QF_NONE       0xFFFFFFFFu

// Foreign-region visual playback is allowed only for route ids whose meaning
// and child-parent relation are shared by all three retail revisions.
#define SUSAMUNE_GHOST_PORTABLE_ROUTE_LIST(X) \
    X(0x00u, 0xFFu) X(0x01u, 0xFFu) X(0x02u, 0xFFu) \
    X(0x03u, 0xFFu) X(0x04u, 0xFFu) X(0x05u, 0xFFu) \
    X(0x06u, 0xFFu) X(0x07u, 0x06u) X(0x08u, 0xFFu) \
    X(0x09u, 0xFFu) X(0x0Du, 0x05u) X(0x0Eu, 0x06u) \
    X(0x10u, 0x09u) X(0x14u, 0xFFu) X(0x15u, 0xFFu) \
    X(0x16u, 0xFFu) X(0x17u, 0xFFu) X(0x18u, 0xFFu) \
    X(0x1Du, 0xFFu) X(0x1Eu, 0x03u) X(0x1Fu, 0x09u) \
    X(0x20u, 0x04u) X(0x21u, 0x04u) X(0x28u, 0x06u) \
    X(0x29u, 0x05u) X(0x2Au, 0x08u) X(0x2Cu, 0x09u) \
    X(0x2Eu, 0x02u) X(0x2Fu, 0x02u) X(0x30u, 0x03u) \
    X(0x32u, 0x05u) X(0x33u, 0x06u) X(0x34u, 0xFFu) \
    X(0x37u, 0x02u) X(0x38u, 0x06u) X(0x39u, 0x09u) \
    X(0x3Au, 0x05u) X(0x3Bu, 0x03u) X(0x3Cu, 0xFFu)

#define SUSAMUNE_GHOST_AUTHOR_SIZE       24u
#define SUSAMUNE_GHOST_NAME_SIZE         48u
#define SUSAMUNE_GHOST_PROFILE_NAME_SIZE 16u
#define SUSAMUNE_GHOST_TEXT_MIN           0x20u
#define SUSAMUNE_GHOST_TEXT_MAX           0x7Eu

#define SUSAMUNE_GHOST_INDEX_ENTRY_PINNED    0x0001u
#define SUSAMUNE_GHOST_INDEX_ENTRY_AUTOSAVED 0x0002u

struct SusamuneGhostPoseSample {
    signed short  yaw;
    // Zero for the first sample in a segment; otherwise the prior QF delta.
    unsigned short deltaQf;
    // Signed big-endian 24-bit fixed coordinates at 1/8 game-unit precision.
    unsigned char x[3];
    unsigned char y[3];
    unsigned char z[3];
    // Big-endian u24. V3 is animation 9/phase 12/zero 3; V4 is
    // animation 9/phase 8/Yoshi 3/held-descriptor index 4.
    unsigned char animation[3];
};

struct SusamuneGhostSegment {
    unsigned int   firstSample;
    unsigned int   sampleCount;
    unsigned int   startQf;
    unsigned int   endQf;
    signed int     routeVariant;
    unsigned char  routeArea;
    unsigned char  routeEpisode;
    unsigned char  routeParentArea;
    unsigned char  routeFlags;
    unsigned int   reserved0;
    unsigned int   reserved1;
};

struct SusamuneGhostFileV3Extension {
    unsigned short segmentCount;
    unsigned short segmentSize;
    unsigned int   segmentTableOffset;
    unsigned int   segmentTableSize;
    unsigned int   sampleDataOffset;
    unsigned int   sampleDataSize;
    unsigned int   segmentTableChecksum;
    unsigned int   reserved[12];
};

// These are source-game identifiers, never pointers or region-specific code
// addresses. A future renderer can map the pair using the header's region.
struct SusamuneGhostAttachmentDescriptor {
    unsigned char objectId[4];
    unsigned char nameKey[2];
};

struct SusamuneGhostFileV4Extension {
    unsigned short segmentCount;
    unsigned short segmentSize;
    unsigned int   segmentTableOffset;
    unsigned int   segmentTableSize;
    unsigned int   sampleDataOffset;
    unsigned int   sampleDataSize;
    unsigned int   segmentTableChecksum;
    unsigned char  attachmentCount;
    unsigned char  attachmentSize;
    unsigned short attachmentFlags;
    unsigned short attachmentReserved;
    struct SusamuneGhostAttachmentDescriptor
        attachments[SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_COUNT];
};

struct SusamuneGhostFileHeader {
    unsigned int   magic;
    unsigned short version;
    unsigned short headerSize;
    unsigned int   fileSize;
    unsigned int   fileChecksum;
    unsigned int   headerChecksum;
    unsigned int   payloadChecksum;
    unsigned int   requiredFeatures;
    unsigned int   runFlags;
    unsigned int   gameId;
    unsigned char  discRevision;
    unsigned char  region;
    unsigned char  sourceProfile;
    unsigned char  recordingMode;
    unsigned char  sampleCodec;
    unsigned char  sampleStride;
    unsigned short sampleIntervalQf;
    unsigned char  routeArea;
    unsigned char  routeEpisode;
    unsigned char  routeParentArea;
    unsigned char  routeFlags;
    signed int     routeVariant;
    unsigned int   resultQf;
    unsigned int   startQf;
    unsigned int   endQf;
    unsigned int   durationQf;
    unsigned int   sampleCount;
    unsigned int   payloadSize;
    unsigned int   createdUnixHi;
    unsigned int   createdUnixLo;
    unsigned int   ghostIdHi;
    unsigned int   ghostIdLo;
    unsigned char  authorLength;
    unsigned char  nameLength;
    unsigned char  profileNameLength;
    unsigned char  checksumKind;
    char           author[SUSAMUNE_GHOST_AUTHOR_SIZE];
    char           name[SUSAMUNE_GHOST_NAME_SIZE];
    char           profileName[SUSAMUNE_GHOST_PROFILE_NAME_SIZE];
    unsigned char  reserved[72];
};

struct SusamuneGhostIndexHeader {
    unsigned int   magic;
    unsigned short version;
    unsigned short headerSize;
    unsigned short entrySize;
    unsigned short entryCount;
    unsigned int   fileSize;
    unsigned int   generation;
    unsigned int   fileChecksum;
    unsigned int   entriesChecksum;
    unsigned int   gameId;
    unsigned char  region;
    unsigned char  profile;
    unsigned char  maxEntries;
    unsigned char  flags;
    unsigned int   totalDurationQf;
    unsigned int   quotaDurationQf;
    unsigned int   createdUnixHi;
    unsigned int   createdUnixLo;
    unsigned char  profileNameLength;
    unsigned char  checksumKind;
    unsigned char  reserved0[2];
    char           profileName[SUSAMUNE_GHOST_PROFILE_NAME_SIZE];
    unsigned char  reserved[56];
};

struct SusamuneGhostIndexEntry {
    unsigned int   ghostIdHi;
    unsigned int   ghostIdLo;
    unsigned int   fileSize;
    unsigned int   fileChecksum;
    unsigned int   durationQf;
    unsigned int   resultQf;
    unsigned int   startQf;
    unsigned int   endQf;
    unsigned int   sampleCount;
    unsigned int   createdUnixHi;
    unsigned int   createdUnixLo;
    signed int     routeVariant;
    unsigned char  routeArea;
    unsigned char  routeEpisode;
    unsigned char  routeParentArea;
    unsigned char  routeFlags;
    unsigned char  nameLength;
    unsigned char  authorLength;
    unsigned short flags;
    char           name[SUSAMUNE_GHOST_NAME_SIZE];
    char           author[SUSAMUNE_GHOST_AUTHOR_SIZE];
};

// Portable layout checks for the PPC mod, ARM kernel and host-side C tools.
typedef char SusamuneGhostPoseSampleSize[
    sizeof(struct SusamuneGhostPoseSample) == SUSAMUNE_GHOST_POSE_SAMPLE_SIZE
        ? 1 : -1];
typedef char SusamuneGhostPoseSampleYawOffset[
    __builtin_offsetof(struct SusamuneGhostPoseSample, yaw) == 0u ? 1 : -1];
typedef char SusamuneGhostPoseSampleDeltaOffset[
    __builtin_offsetof(struct SusamuneGhostPoseSample, deltaQf) == 2u
        ? 1 : -1];
typedef char SusamuneGhostPoseSampleXOffset[
    __builtin_offsetof(struct SusamuneGhostPoseSample, x) == 4u ? 1 : -1];
typedef char SusamuneGhostPoseSampleYOffset[
    __builtin_offsetof(struct SusamuneGhostPoseSample, y) == 7u ? 1 : -1];
typedef char SusamuneGhostPoseSampleZOffset[
    __builtin_offsetof(struct SusamuneGhostPoseSample, z) == 10u ? 1 : -1];
typedef char SusamuneGhostPoseSampleAnimationOffset[
    __builtin_offsetof(struct SusamuneGhostPoseSample, animation) == 13u
        ? 1 : -1];
typedef char SusamuneGhostSegmentSize[
    sizeof(struct SusamuneGhostSegment) == SUSAMUNE_GHOST_V3_SEGMENT_SIZE
        ? 1 : -1];
typedef char SusamuneGhostSegmentRouteOffset[
    __builtin_offsetof(struct SusamuneGhostSegment, routeArea) == 20u
        ? 1 : -1];
typedef char SusamuneGhostFileV3ExtensionSize[
    sizeof(struct SusamuneGhostFileV3Extension) == 72u ? 1 : -1];
typedef char SusamuneGhostAttachmentDescriptorSize[
    sizeof(struct SusamuneGhostAttachmentDescriptor) ==
            SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE
        ? 1 : -1];
typedef char SusamuneGhostFileV4ExtensionSize[
    sizeof(struct SusamuneGhostFileV4Extension) == 72u ? 1 : -1];
typedef char SusamuneGhostFileHeaderSize[
    sizeof(struct SusamuneGhostFileHeader) == SUSAMUNE_GHOST_FILE_HEADER_SIZE
        ? 1 : -1];
typedef char SusamuneGhostFileChecksumOffset[
    __builtin_offsetof(struct SusamuneGhostFileHeader, fileChecksum) ==
            SUSAMUNE_GHOST_FILE_CHECKSUM_OFFSET
        ? 1 : -1];
typedef char SusamuneGhostHeaderChecksumOffset[
    __builtin_offsetof(struct SusamuneGhostFileHeader, headerChecksum) ==
            SUSAMUNE_GHOST_HEADER_CHECKSUM_OFFSET
        ? 1 : -1];
typedef char SusamuneGhostPayloadChecksumOffset[
    __builtin_offsetof(struct SusamuneGhostFileHeader, payloadChecksum) ==
            SUSAMUNE_GHOST_PAYLOAD_CHECKSUM_OFFSET
        ? 1 : -1];
typedef char SusamuneGhostFilePayloadOffset[
    __builtin_offsetof(struct SusamuneGhostFileHeader, payloadSize) == 72u
        ? 1 : -1];
typedef char SusamuneGhostFileTextOffset[
    __builtin_offsetof(struct SusamuneGhostFileHeader, author) == 96u
        ? 1 : -1];
typedef char SusamuneGhostFileExtensionOffset[
    __builtin_offsetof(struct SusamuneGhostFileHeader, reserved) ==
            SUSAMUNE_GHOST_V3_SEGMENT_COUNT_OFFSET
        ? 1 : -1];
typedef char SusamuneGhostV3ExtensionChecksumOffset[
    SUSAMUNE_GHOST_V3_SEGMENT_COUNT_OFFSET +
            __builtin_offsetof(struct SusamuneGhostFileV3Extension,
                               segmentTableChecksum) ==
            SUSAMUNE_GHOST_V3_SEGMENT_CHECKSUM_OFFSET
        ? 1 : -1];
typedef char SusamuneGhostV4ExtensionAttachmentOffset[
    SUSAMUNE_GHOST_V4_SEGMENT_COUNT_OFFSET +
            __builtin_offsetof(struct SusamuneGhostFileV4Extension,
                               attachments) ==
            SUSAMUNE_GHOST_V4_ATTACHMENT_TABLE_OFFSET
        ? 1 : -1];
typedef char SusamuneGhostIndexHeaderSize[
    sizeof(struct SusamuneGhostIndexHeader) == SUSAMUNE_GHOST_INDEX_HEADER_SIZE
        ? 1 : -1];
typedef char SusamuneGhostIndexFileChecksumOffset[
    __builtin_offsetof(struct SusamuneGhostIndexHeader, fileChecksum) ==
            SUSAMUNE_GHOST_INDEX_FILE_CHECKSUM_OFFSET
        ? 1 : -1];
typedef char SusamuneGhostIndexEntriesChecksumOffset[
    __builtin_offsetof(struct SusamuneGhostIndexHeader, entriesChecksum) ==
            SUSAMUNE_GHOST_INDEX_ENTRIES_CHECKSUM_OFFSET
        ? 1 : -1];
typedef char SusamuneGhostIndexEntrySize[
    sizeof(struct SusamuneGhostIndexEntry) == SUSAMUNE_GHOST_INDEX_ENTRY_SIZE
        ? 1 : -1];
typedef char SusamuneGhostIndexTextOffset[
    __builtin_offsetof(struct SusamuneGhostIndexEntry, name) == 56u
        ? 1 : -1];
typedef char SusamuneGhostMaxFileFitsTransfer[
    SUSAMUNE_GHOST_MAX_FILE_SIZE <= 0x13E000u ? 1 : -1];
typedef char SusamuneGhostRawPayloadLimit[
    SUSAMUNE_GHOST_MAX_SAMPLE_COUNT * SUSAMUNE_GHOST_POSE_SAMPLE_SIZE ==
            SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE
        ? 1 : -1];
typedef char SusamuneGhostV1FileLimit[
    SUSAMUNE_GHOST_FILE_HEADER_SIZE + SUSAMUNE_GHOST_V1_MAX_PAYLOAD_SIZE ==
            SUSAMUNE_GHOST_V1_MAX_FILE_SIZE
        ? 1 : -1];
typedef char SusamuneGhostV2FileLimit[
    SUSAMUNE_GHOST_FILE_HEADER_SIZE + SUSAMUNE_GHOST_V2_MAX_PAYLOAD_SIZE ==
            SUSAMUNE_GHOST_V2_MAX_FILE_SIZE
        ? 1 : -1];
typedef char SusamuneGhostV2PayloadLimit[
    SUSAMUNE_GHOST_V2_SEGMENT_TABLE_SIZE +
            SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE ==
            SUSAMUNE_GHOST_V2_MAX_PAYLOAD_SIZE
        ? 1 : -1];
typedef char SusamuneGhostV3FileLimit[
    SUSAMUNE_GHOST_FILE_HEADER_SIZE + SUSAMUNE_GHOST_V3_MAX_PAYLOAD_SIZE ==
            SUSAMUNE_GHOST_V3_MAX_FILE_SIZE
        ? 1 : -1];
typedef char SusamuneGhostV3PayloadLimit[
    SUSAMUNE_GHOST_V3_SEGMENT_TABLE_SIZE +
            SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE ==
            SUSAMUNE_GHOST_V3_MAX_PAYLOAD_SIZE
        ? 1 : -1];
typedef char SusamuneGhostV4FileLimit[
    SUSAMUNE_GHOST_FILE_HEADER_SIZE + SUSAMUNE_GHOST_V4_MAX_PAYLOAD_SIZE ==
            SUSAMUNE_GHOST_V4_MAX_FILE_SIZE
        ? 1 : -1];
typedef char SusamuneGhostV4PayloadLimit[
    SUSAMUNE_GHOST_V4_SEGMENT_TABLE_SIZE +
            SUSAMUNE_GHOST_MAX_SAMPLE_DATA_SIZE ==
            SUSAMUNE_GHOST_V4_MAX_PAYLOAD_SIZE
        ? 1 : -1];
typedef char SusamuneGhostIndexLimit[
    SUSAMUNE_GHOST_INDEX_HEADER_SIZE +
            SUSAMUNE_GHOST_PROFILE_MAX_ENTRIES *
                SUSAMUNE_GHOST_INDEX_ENTRY_SIZE ==
            SUSAMUNE_GHOST_INDEX_MAX_FILE_SIZE
        ? 1 : -1];
#endif  // SUSAMUNE_GHOST_FORMAT_H
