/*

Susamune ghost storage (Nintendont kernel side).

The PPC owns the request cache line and an immutable transfer payload until
acknowledgement. The ARM owns the response line and performs at most one fixed
file-header operation or 16 KiB of payload I/O per DI-idle service pass.

*/

#include "SusamuneGhost.h"
#include "SusamuneCfg.h"
#include "string.h"
#include "debug.h"
#include "ff_utf8.h"

#include "susamune/ghost_format.h"
#include "susamune/data_paths.h"
#include "susamune/ghost_storage.h"
#include "susamune/mod_bin.h"

extern u32 GAME_ID;

#define GHOST_BANK_COUNT 2u
#define GHOST_PATH_SIZE  128u

typedef char ImportGhostPathFits[
	sizeof("1:" MOONSHINE_DATA_ROOT MOONSHINE_GHOSTS_DIR "/import/") - 1u +
	        SUSAMUNE_GHOST_IMPORT_LEAF_SIZE <= GHOST_PATH_SIZE
		? 1 : -1];

enum BankState
{
	BANK_EMPTY,
	BANK_INVALID,
	BANK_VALID,
	BANK_UNSAFE
};

struct BankInfo
{
	u32 generation;
	u32 flags;
	u32 payloadSize;
	u32 durationQf;
	u32 payloadChecksum;
	u8 state;
};

struct SlotCatalog
{
	struct BankInfo bank[GHOST_BANK_COUNT];
	struct SusamuneGhostSlotInfo info;
	s8 activeBank;
	u8 unsafe;
};

struct ImportedSlot
{
	struct SusamuneGhostSlotInfo info;
	char leaf[SUSAMUNE_GHOST_IMPORT_LEAF_SIZE];
};

enum OperationPhase
{
	OP_IDLE,
	OP_VALIDATE_SAVE,
	OP_PERSONAL_SCAN_OPEN,
	OP_PERSONAL_SCAN_NEXT,
	OP_SCAN_ENVELOPES,
	OP_SCAN_HEADERS,
	OP_SCAN_ACTIVE_HEADERS,
	OP_SAVE_OPEN,
	OP_SAVE_DATA,
	OP_SAVE_SYNC_DATA,
	OP_SAVE_COMMIT,
	OP_SAVE_FINISH,
	OP_LOAD_OPEN,
	OP_LOAD_DATA,
	OP_LOAD_CLOSE,
	OP_VALIDATE_LOAD,
	OP_EXPORT_OPEN,
	OP_EXPORT_DATA,
	OP_EXPORT_SYNC_DATA,
	OP_EXPORT_COMMIT,
	OP_EXPORT_FINISH,
	OP_IMPORT_SCAN_OPEN,
	OP_IMPORT_SCAN_NEXT,
	OP_IMPORT_SCAN_FILE_OPEN,
	OP_IMPORT_SCAN_FILE_READ,
	OP_IMPORT_SCAN_FILE_CLOSE,
	OP_DELETE_OPEN,
	OP_DELETE_FINISH,
	OP_DELETE_RECLAIM,
	OP_IMPORTED_DELETE
};

enum ValidateResult
{
	VALIDATE_INVALID,
	VALIDATE_OK,
	VALIDATE_FORWARD
};

enum ReadResult
{
	READ_INVALID,
	READ_MISSING,
	READ_VALID,
	READ_UNSAFE,
	READ_IO
};

struct GhostRequest
{
	u32 seq;
	u16 command;
	u16 profile;
	u32 slot;
	u32 payloadSize;
	u32 flags;
	u32 expectedGeneration;
	char leaf[SUSAMUNE_GHOST_IMPORT_LEAF_SIZE];
};

struct ImportedCatalogWork
{
	struct ImportedSlot catalog[1];
	struct ImportedSlot candidate;
	DIR dir;
	FILINFO entry;
};

// Requests are serialized; switching profiles invalidates the old view.
union GhostCatalogStorage
{
	struct SlotCatalog personal[1];
	struct ImportedCatalogWork imported;
};

// Personal slot scans run while the overlaid directory iterator stays open.
typedef char PersonalCatalogDoesNotOverlapDirectory[
	sizeof(struct SlotCatalog) <=
	        __builtin_offsetof(struct ImportedCatalogWork, dir) ? 1 : -1];

static union GhostCatalogStorage CatalogStorage;
#define Catalog         CatalogStorage.personal
#define ImportedCatalog CatalogStorage.imported.catalog
#define ImportCandidate  CatalogStorage.imported.candidate
#define ImportDir        CatalogStorage.imported.dir
#define ImportEntry      CatalogStorage.imported.entry
static struct GhostRequest Request;
static struct SusamuneGhostStorageEnvelope IoEnvelope;
static FIL IoFile;
static bool IoFileOpen;
static bool GhostSupported;
static bool CatalogReady;
static bool CatalogUnsafe;
static bool ImportedCatalogReady;
static u16 CatalogProfile;
static u16 CatalogCount;
static u16 ImportedCatalogCount;
static u32 CatalogDurationQf;
static u32 GhostAckSeq;
static u32 ScanFile;
static u8 ScanHeaderAttempts;
static int ScanIoError;
static enum OperationPhase Phase;
static u32 IoOffset;
static u32 IoChecksum;
static u8 IoBank;
static u8 LoadAttemptedBanks;
static bool ImportDirOpen;
static u32 ImportCandidateSize;
static u32 ImportPrefixSize;
static struct SusamuneGhostCatalogPage Page;
static bool DirectoryScan;
static bool AutoProbe;
static u32 ScanSlot;
static u32 AutoReuseSlot;
static u32 PageOrdinal;
static u64 PageDuration;


static const u8 *ValidationBytes;
static u32 ValidationSize;
static u32 ValidationOffset;
static u32 ValidationPoseEnd;
static u32 ValidationInputEnd;
static u32 ValidationInputStride;
static u32 ValidationTeachingCrc;
static u32 ValidationPreviousInputQf;
static u32 ValidationInputCount;
static u32 ValidationSplitCount;
static u32 ValidationPreviousSplitQf;
static u32 ValidationPayloadCrc;
static u32 ValidationFileCrc;
static u32 ValidationRawCrc;
static u32 ValidationSample;
static u32 ValidationSegmentDuration;
static u16 ValidationSegment;
static u16 ValidationSegmentCount;
static u16 ValidationVersion;
static u8 ValidationAttachmentCount;
static u16 ValidationAttachmentFlags;

static const char *GhostRegion;
static u8 GhostRegionId;

static volatile struct SusamuneGhostStorageMailbox *GhostBlock(void)
{
	return SUSAMUNE_GHOST_STORAGE_PHYS_PTR;
}

static u16 ReadBe16(const u8 *p)
{
	return ((u16)p[0] << 8) | p[1];
}

static u32 ReadBe32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8) | p[3];
}

static u32 ReadBe24(const u8 *p)
{
	return ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
}

static s32 ReadBeS24(const u8 *p)
{
	u32 value = ReadBe24(p);
	return (s32)(value | ((value & 0x800000u) ? 0xFF000000u : 0u));
}

static s32 ReadBeS32(const u8 *p)
{
	return (s32)ReadBe32(p);
}

static u32 CrcUpdate(u32 crc, const u8 *data, u32 size)
{
	u32 i;
	u32 bit;

	for (i = 0; i < size; i++)
	{
		crc ^= data[i];
		for (bit = 0; bit < 8; bit++)
			crc = (crc >> 1) ^
			      (SUSAMUNE_GHOST_CRC32_POLY & (0u - (crc & 1u)));
	}
	return crc;
}

static u32 CrcUpdateHeader(u32 crc, const u8 *header,
	                       u32 zeroStart, u32 zeroEnd)
{
	u32 i;
	u8 value;

	for (i = 0; i < SUSAMUNE_GHOST_FILE_HEADER_SIZE; i++)
	{
		value = (i >= zeroStart && i < zeroEnd) ? 0 : header[i];
		crc = CrcUpdate(crc, &value, 1);
	}
	return crc;
}

static void WriteBe16(u8 *p, u16 value)
{
	p[0] = (u8)(value >> 8);
	p[1] = (u8)value;
}

static void WriteBe32(u8 *p, u32 value)
{
	p[0] = (u8)(value >> 24);
	p[1] = (u8)(value >> 16);
	p[2] = (u8)(value >> 8);
	p[3] = (u8)value;
}

static void EncodeEnvelope(const struct SusamuneGhostStorageEnvelope *e, u8 *raw)
{
	u32 i;
	WriteBe32(raw, e->magic);
	WriteBe16(raw + 4, e->version);
	WriteBe16(raw + 6, e->headerSize);
	WriteBe32(raw + 8, e->generation);
	WriteBe32(raw + 12, e->flags);
	WriteBe32(raw + 16, e->gameId);
	WriteBe16(raw + 20, e->profile);
	WriteBe16(raw + 22, e->slot);
	WriteBe32(raw + 24, e->payloadSize);
	WriteBe32(raw + 28, e->durationQf);
	WriteBe32(raw + 32, e->payloadChecksum);
	WriteBe32(raw + 36, e->headerChecksum);
	for (i = 0; i < 6; ++i)
		WriteBe32(raw + 40 + i * 4, e->reserved[i]);
}

static void DecodeEnvelope(const u8 *raw, struct SusamuneGhostStorageEnvelope *e)
{
	u32 i;
	e->magic = ReadBe32(raw);
	e->version = ReadBe16(raw + 4);
	e->headerSize = ReadBe16(raw + 6);
	e->generation = ReadBe32(raw + 8);
	e->flags = ReadBe32(raw + 12);
	e->gameId = ReadBe32(raw + 16);
	e->profile = ReadBe16(raw + 20);
	e->slot = ReadBe16(raw + 22);
	e->payloadSize = ReadBe32(raw + 24);
	e->durationQf = ReadBe32(raw + 28);
	e->payloadChecksum = ReadBe32(raw + 32);
	e->headerChecksum = ReadBe32(raw + 36);
	for (i = 0; i < 6; ++i)
		e->reserved[i] = ReadBe32(raw + 40 + i * 4);
}

static int WriteEnvelope(FIL *file,
	                     const struct SusamuneGhostStorageEnvelope *e, UINT *wrote)
{
	u8 raw[SUSAMUNE_GHOST_ENVELOPE_SIZE];
	EncodeEnvelope(e, raw);
	return f_write(file, raw, sizeof(raw), wrote);
}

static u32 EnvelopeChecksum(const struct SusamuneGhostStorageEnvelope *envelope)
{
	u8 raw[SUSAMUNE_GHOST_ENVELOPE_SIZE];
	EncodeEnvelope(envelope, raw);
	WriteBe32(raw + 36, 0);
	return CrcUpdate(SUSAMUNE_GHOST_CRC32_INIT, raw, sizeof(raw)) ^
	       SUSAMUNE_GHOST_CRC32_XOR_OUT;
}

static bool BytesAreZero(const u8 *bytes, u32 size)
{
	u32 i;
	for (i = 0; i < size; i++)
		if (bytes[i] != 0)
			return false;
	return true;
}

static bool RouteIsValid(u8 area, u8 episode, u8 parentArea,
	                     u8 routeFlags, s32 routeVariant)
{
	return area <= SUSAMUNE_GHOST_ROUTE_AREA_MAX &&
	       episode <= SUSAMUNE_GHOST_ROUTE_EPISODE_MAX &&
	       (parentArea == SUSAMUNE_GHOST_ROUTE_PARENT_NONE ||
	        parentArea <= SUSAMUNE_GHOST_ROUTE_AREA_MAX) &&
	       (routeFlags & ~SUSAMUNE_GHOST_ROUTE_FLAGS_V1) == 0 &&
	       (((routeFlags & SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE) != 0) ==
	        (parentArea != SUSAMUNE_GHOST_ROUTE_PARENT_NONE)) &&
	       ((routeFlags & SUSAMUNE_GHOST_ROUTE_PARENT_START) == 0 ||
	        parentArea != SUSAMUNE_GHOST_ROUTE_PARENT_NONE) &&
	       routeVariant >= SUSAMUNE_GHOST_ROUTE_VARIANT_NONE &&
	       routeVariant <= SUSAMUNE_GHOST_ROUTE_VARIANT_MAX;
}

static bool GameRegionPairIsValid(u32 gameId, u8 region)
{
	return (gameId == SUSAMUNE_GHOST_GAME_ID_JP &&
	        region == SUSAMUNE_GHOST_REGION_JP) ||
	       (gameId == SUSAMUNE_GHOST_GAME_ID_US &&
	        region == SUSAMUNE_GHOST_REGION_US) ||
	       (gameId == SUSAMUNE_GHOST_GAME_ID_PAL &&
	        region == SUSAMUNE_GHOST_REGION_PAL);
}

static bool PortableRouteIsValid(u8 area, u8 episode, u8 parentArea,
	                             u8 routeFlags, s32 routeVariant)
{
	u8 expectedParent;

	switch (area)
	{
#define PORTABLE_ROUTE_CASE(routeArea, parent) \
		case routeArea: expectedParent = parent; break;
		SUSAMUNE_GHOST_PORTABLE_ROUTE_LIST(PORTABLE_ROUTE_CASE)
#undef PORTABLE_ROUTE_CASE
		default: return false;
	}
	if (!RouteIsValid(area, episode, parentArea, routeFlags, routeVariant) ||
	    parentArea != expectedParent)
		return false;
	if (expectedParent == SUSAMUNE_GHOST_ROUTE_PARENT_NONE)
		return routeFlags == 0;
	return (routeFlags & SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE) != 0 &&
	       (routeFlags & ~(SUSAMUNE_GHOST_ROUTE_INTERNAL_SCENE |
	                       SUSAMUNE_GHOST_ROUTE_PARENT_START)) == 0;
}

static bool HeaderTextIsValid(const u8 *field, u32 capacity, u32 length,
	                          bool required)
{
	u32 i;

	if (length > capacity || (required && length == 0))
		return false;
	for (i = 0; i < length; i++)
		if (field[i] < SUSAMUNE_GHOST_TEXT_MIN ||
		    field[i] > SUSAMUNE_GHOST_TEXT_MAX ||
		    field[i] == '/' || field[i] == '\\')
			return false;
	for (; i < capacity; i++)
		if (field[i] != 0)
			return false;
	return true;
}

static bool V4AttachmentsAreValid(const u8 *header)
{
	u8 count = header[SUSAMUNE_GHOST_V4_ATTACHMENT_COUNT_OFFSET];
	u16 flags = ReadBe16(header + SUSAMUNE_GHOST_V4_ATTACHMENT_FLAGS_OFFSET);
	u32 i;
	u32 prior;
	const u8 *descriptor;

	if (count > SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_COUNT ||
	    header[SUSAMUNE_GHOST_V4_ATTACHMENT_SIZE_OFFSET] !=
	        SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE ||
	    (flags & ~SUSAMUNE_GHOST_V4_ATTACHMENT_FLAGS) != 0 ||
	    ReadBe16(header + SUSAMUNE_GHOST_V4_ATTACHMENT_RESERVED_OFFSET) != 0)
		return false;
	for (i = 0; i < SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_COUNT; i++)
	{
		descriptor = header + SUSAMUNE_GHOST_V4_ATTACHMENT_TABLE_OFFSET +
		             i * SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE;
		if (i < count)
		{
			if (BytesAreZero(descriptor,
			                 SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE))
				return false;
			for (prior = 0; prior < i; prior++)
				if (memcmp(descriptor,
				           header + SUSAMUNE_GHOST_V4_ATTACHMENT_TABLE_OFFSET +
				               prior * SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE,
				           SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE) == 0)
					return false;
		}
		else if (!BytesAreZero(
		             descriptor,
		             SUSAMUNE_GHOST_V4_ATTACHMENT_DESCRIPTOR_SIZE))
			return false;
	}
	return true;
}

static enum ValidateResult ValidateCanonicalHeader(const u8 *header,
	                                                u32 available,
	                                                u16 profile,
	                                                bool imported)
{
	u32 fileSize;
	u32 payloadSize;
	u32 sampleCount;
	u32 headerCrc;
	u32 required;
	u32 startQf;
	u32 endQf;
	u32 resultQf;
	s32 routeVariant;
	u16 version;
	u16 segmentCount;
	u32 sourceGameId;
	u8 sourceRegion;
	bool portable;

	if (available < SUSAMUNE_GHOST_FILE_HEADER_SIZE)
		return VALIDATE_INVALID;
	if (ReadBe32(header) != SUSAMUNE_GHOST_FILE_MAGIC)
		return VALIDATE_INVALID;
	version = ReadBe16(header + 4);
	if (version != SUSAMUNE_GHOST_FILE_VERSION_V3 &&
	    version != SUSAMUNE_GHOST_FILE_VERSION_V4 &&
	    version != SUSAMUNE_GHOST_FILE_VERSION_V5 &&
	    version != SUSAMUNE_GHOST_FILE_VERSION_V6)
		return VALIDATE_FORWARD;
	if (ReadBe16(header + 6) != SUSAMUNE_GHOST_FILE_HEADER_SIZE)
		return VALIDATE_INVALID;
	headerCrc = CrcUpdateHeader(SUSAMUNE_GHOST_CRC32_INIT, header, 12, 20) ^
	            SUSAMUNE_GHOST_CRC32_XOR_OUT;
	if (headerCrc != ReadBe32(header + 16))
		return VALIDATE_INVALID;

	fileSize = ReadBe32(header + 8);
	payloadSize = ReadBe32(header + 72);
	sampleCount = ReadBe32(header + 68);
	required = ReadBe32(header + 24);
	if (!SusamuneGhostRunFlagsValid(ReadBe32(header + 28)))
		return VALIDATE_INVALID;
	if ((required & ~(version == SUSAMUNE_GHOST_FILE_VERSION_V6
	                    ? SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V6
	                    : version == SUSAMUNE_GHOST_FILE_VERSION_V5
	                    ? SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V5
	                    : version == SUSAMUNE_GHOST_FILE_VERSION_V4
	                    ? SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V4
	                    : SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V3)) != 0)
		return VALIDATE_FORWARD;
	if (version >= SUSAMUNE_GHOST_FILE_VERSION_V4 &&
	    required != (version == SUSAMUNE_GHOST_FILE_VERSION_V6
	        ? SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V6
	        : version == SUSAMUNE_GHOST_FILE_VERSION_V5
	        ? SUSAMUNE_GHOST_SUPPORTED_REQUIRED_FEATURES_V5
	        : SUSAMUNE_GHOST_REQUIRED_EXTENDED_CODEC))
		return VALIDATE_INVALID;

	if (sampleCount < SUSAMUNE_GHOST_MIN_SAMPLE_COUNT ||
	    sampleCount > SusamuneGhostPoseLimit(version))
		return VALIDATE_INVALID;
	segmentCount = ReadBe16(
		header + SUSAMUNE_GHOST_V4_SEGMENT_COUNT_OFFSET);
	if (segmentCount == 0 ||
	    segmentCount > SUSAMUNE_GHOST_V4_MAX_SEGMENTS ||
	    ReadBe16(header + SUSAMUNE_GHOST_V4_SEGMENT_SIZE_OFFSET) !=
	        SUSAMUNE_GHOST_V4_SEGMENT_SIZE ||
	    ReadBe32(header +
	             SUSAMUNE_GHOST_V4_SEGMENT_TABLE_OFFSET_FIELD) !=
	        SUSAMUNE_GHOST_V4_SEGMENT_TABLE_OFFSET ||
	    ReadBe32(header +
	             SUSAMUNE_GHOST_V4_SEGMENT_TABLE_SIZE_OFFSET) !=
	        SUSAMUNE_GHOST_V4_SEGMENT_TABLE_SIZE ||
	    ReadBe32(header +
	             SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET_FIELD) !=
	        SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET ||
	    ReadBe32(header +
	             SUSAMUNE_GHOST_V4_SAMPLE_DATA_SIZE_OFFSET) !=
	        sampleCount * SUSAMUNE_GHOST_POSE_SAMPLE_SIZE ||
	    payloadSize != fileSize - SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
	    (version >= SUSAMUNE_GHOST_FILE_VERSION_V5
	        ? fileSize < SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET +
	            sampleCount * SUSAMUNE_GHOST_POSE_SAMPLE_SIZE +
	            SUSAMUNE_GHOST_TEACHING_HEADER_SIZE ||
	          fileSize > SUSAMUNE_GHOST_V5_MAX_FILE_SIZE
	        : fileSize != SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET +
	            sampleCount * SUSAMUNE_GHOST_POSE_SAMPLE_SIZE ||
	          fileSize > SUSAMUNE_GHOST_V4_MAX_FILE_SIZE))
		return VALIDATE_INVALID;
	if ((version == SUSAMUNE_GHOST_FILE_VERSION_V3 &&
	     !BytesAreZero(header +
	                         SUSAMUNE_GHOST_V3_SEGMENT_CHECKSUM_OFFSET + 4,
	                   SUSAMUNE_GHOST_FILE_HEADER_SIZE -
	                       SUSAMUNE_GHOST_V3_SEGMENT_CHECKSUM_OFFSET - 4)) ||
	    (version >= SUSAMUNE_GHOST_FILE_VERSION_V4 &&
	     !V4AttachmentsAreValid(header)))
		return VALIDATE_INVALID;

	sourceGameId = ReadBe32(header + 32);
	sourceRegion = header[37];
	portable = imported && sourceGameId != GAME_ID;
	if (header[36] != SUSAMUNE_GHOST_DISC_REVISION ||
	    (!imported &&
	     (sourceGameId != GAME_ID || sourceRegion != GhostRegionId ||
	      header[38] != profile || profile >= SUSAMUNE_GHOST_PROFILE_COUNT)) ||
	    (imported &&
	     (!GameRegionPairIsValid(sourceGameId, sourceRegion) ||
	      header[38] >= SUSAMUNE_GHOST_PROFILE_COUNT)))
		return VALIDATE_INVALID;
	if (header[39] != SUSAMUNE_GHOST_RECORDING_POSE_QF ||
	    header[40] != (version >= SUSAMUNE_GHOST_FILE_VERSION_V4
	                      ? SUSAMUNE_GHOST_CODEC_POSE_ATTACHMENTS
	                      : SUSAMUNE_GHOST_CODEC_RAW) ||
	    header[41] != SUSAMUNE_GHOST_POSE_SAMPLE_SIZE ||
	    ReadBe16(header + 42) != SUSAMUNE_GHOST_TRANSFORM_INTERVAL_QF)
		return VALIDATE_INVALID;
	routeVariant = ReadBeS32(header + 48);
	if (!(portable
	          ? PortableRouteIsValid(header[44], header[45], header[46],
	                                 header[47], routeVariant)
	          : RouteIsValid(header[44], header[45], header[46], header[47],
	                         routeVariant)))
		return VALIDATE_INVALID;
	startQf = ReadBe32(header + 56);
	endQf = ReadBe32(header + 60);
	resultQf = ReadBe32(header + 52);
	if (startQf > SUSAMUNE_GHOST_QF_MAX || endQf > SUSAMUNE_GHOST_QF_MAX ||
	    endQf < startQf || ReadBe32(header + 64) == 0 ||
	    ReadBe32(header + 64) != endQf - startQf ||
	    ReadBe32(header + 64) > SusamuneGhostDurationLimit(version))
		return VALIDATE_INVALID;
	if (resultQf != SUSAMUNE_GHOST_RESULT_QF_NONE &&
	    (resultQf < startQf || resultQf > endQf))
		return VALIDATE_INVALID;
	if (header[95] != SUSAMUNE_GHOST_CHECKSUM_CRC32)
		return VALIDATE_FORWARD;
	if (!HeaderTextIsValid(header + 96, SUSAMUNE_GHOST_AUTHOR_SIZE,
	                       header[92], false) ||
	    !HeaderTextIsValid(header + 120, SUSAMUNE_GHOST_NAME_SIZE,
	                       header[93], true) ||
	    !HeaderTextIsValid(header + 168, SUSAMUNE_GHOST_PROFILE_NAME_SIZE,
	                       header[94], false) ||
	    (ReadBe32(header + 84) == 0 && ReadBe32(header + 88) == 0))
		return VALIDATE_INVALID;

	return VALIDATE_OK;
}

static bool ValidateV3SegmentTable(const u8 *bytes, bool portable)
{
	const u8 *table = bytes + SUSAMUNE_GHOST_V3_SEGMENT_TABLE_OFFSET;
	u16 count = ReadBe16(bytes + SUSAMUNE_GHOST_V3_SEGMENT_COUNT_OFFSET);
	u32 totalSamples = ReadBe32(bytes + 68);
	u32 expectedSample = 0;
	u32 previousEnd = 0;
	u32 firstStart = 0;
	u32 lastStart = 0;
	u32 lastEnd = 0;
	u32 crc;
	u32 i;
	const u8 *segment;
	u32 firstSample;
	u32 sampleCount;
	u32 startQf;
	u32 endQf;
	s32 routeVariant;

	crc = CrcUpdate(SUSAMUNE_GHOST_CRC32_INIT, table,
	                SUSAMUNE_GHOST_V3_SEGMENT_TABLE_SIZE) ^
	      SUSAMUNE_GHOST_CRC32_XOR_OUT;
	if (crc != ReadBe32(bytes +
	                    SUSAMUNE_GHOST_V3_SEGMENT_CHECKSUM_OFFSET) ||
	    !BytesAreZero(table + count * SUSAMUNE_GHOST_V3_SEGMENT_SIZE,
	                  SUSAMUNE_GHOST_V3_SEGMENT_TABLE_SIZE -
	                      count * SUSAMUNE_GHOST_V3_SEGMENT_SIZE))
		return false;

	for (i = 0; i < count; i++)
	{
		segment = table + i * SUSAMUNE_GHOST_V3_SEGMENT_SIZE;
		firstSample = ReadBe32(segment);
		sampleCount = ReadBe32(segment + 4);
		startQf = ReadBe32(segment + 8);
		endQf = ReadBe32(segment + 12);
		routeVariant = ReadBeS32(segment + 16);
		if (firstSample != expectedSample || sampleCount == 0 ||
		    expectedSample > totalSamples ||
		    sampleCount > totalSamples - expectedSample ||
		    startQf > SUSAMUNE_GHOST_QF_MAX ||
		    endQf > SUSAMUNE_GHOST_QF_MAX || endQf < startQf ||
		    (i != 0 && startQf < previousEnd) ||
		    !(portable
		          ? PortableRouteIsValid(segment[20], segment[21], segment[22],
		                                 segment[23], routeVariant)
		          : RouteIsValid(segment[20], segment[21], segment[22],
		                         segment[23], routeVariant)) ||
		    ReadBe32(segment + 24) != 0 || ReadBe32(segment + 28) != 0)
			return false;
		if (i == 0)
		{
			firstStart = startQf;
			if (segment[20] != bytes[44] || segment[21] != bytes[45] ||
			    segment[22] != bytes[46] || segment[23] != bytes[47] ||
			    routeVariant != ReadBeS32(bytes + 48))
				return false;
		}
		expectedSample += sampleCount;
		previousEnd = endQf;
		lastStart = startQf;
		lastEnd = endQf;
	}

	if (expectedSample != totalSamples || firstStart != ReadBe32(bytes + 56) ||
	    lastEnd != ReadBe32(bytes + 60))
		return false;
	if (ReadBe32(bytes + 52) != SUSAMUNE_GHOST_RESULT_QF_NONE &&
	    (ReadBe32(bytes + 52) < lastStart ||
	     ReadBe32(bytes + 52) > lastEnd))
		return false;
	return true;
}

static enum ValidateResult BeginCanonicalValidation(const u8 *bytes,
	                                                  u32 size,
	                                                  u16 profile,
	                                                  bool imported)
{
	enum ValidateResult result =
		ValidateCanonicalHeader(bytes, size, profile, imported);
	bool portable;

	if (result != VALIDATE_OK)
		return result;
	portable = imported && ReadBe32(bytes + 32) != GAME_ID;
	if (ReadBe32(bytes + 8) != size)
		return VALIDATE_INVALID;
	if (!ValidateV3SegmentTable(bytes, portable))
		return VALIDATE_INVALID;

	ValidationPoseEnd = SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET +
	    ReadBe32(bytes + 68) * SUSAMUNE_GHOST_POSE_SAMPLE_SIZE;
	ValidationInputEnd = ValidationPoseEnd;
	ValidationInputCount = 0;
	ValidationSplitCount = 0;
	ValidationPreviousInputQf = 0;
	ValidationPreviousSplitQf = 0;
	ValidationTeachingCrc = SUSAMUNE_GHOST_CRC32_INIT;
	ValidationInputStride = SusamuneGhostInputStride(ReadBe16(bytes + 4));
	if (ReadBe16(bytes + 4) >= SUSAMUNE_GHOST_FILE_VERSION_V5)
	{
		const u8 *teaching = bytes + ValidationPoseEnd;
		if (!SusamuneGhostTeachingHeaderValid(teaching, size - ValidationPoseEnd) ||
		    teaching[5] != (ReadBe16(bytes + 4) == SUSAMUNE_GHOST_FILE_VERSION_V6
		        ? SUSAMUNE_GHOST_TEACHING_FLUDD_VERSION : SUSAMUNE_GHOST_TEACHING_VERSION))
			return VALIDATE_INVALID;
		ValidationInputEnd = ValidationPoseEnd +
		    SUSAMUNE_GHOST_TEACHING_HEADER_SIZE +
		    ReadBe32(teaching + 8) * ValidationInputStride;
	}

	ValidationBytes = bytes;
	ValidationSize = size;
	ValidationOffset = SUSAMUNE_GHOST_V4_SAMPLE_DATA_OFFSET;
	ValidationPayloadCrc = SUSAMUNE_GHOST_CRC32_INIT;
	ValidationFileCrc = CrcUpdateHeader(SUSAMUNE_GHOST_CRC32_INIT,
	                                    bytes, 12, 16);
	ValidationRawCrc = CrcUpdate(SUSAMUNE_GHOST_CRC32_INIT, bytes,
	                             SUSAMUNE_GHOST_FILE_HEADER_SIZE);
	ValidationPayloadCrc = CrcUpdate(
		ValidationPayloadCrc,
		bytes + SUSAMUNE_GHOST_V3_SEGMENT_TABLE_OFFSET,
		SUSAMUNE_GHOST_V3_SEGMENT_TABLE_SIZE);
	ValidationFileCrc = CrcUpdate(
		ValidationFileCrc,
		bytes + SUSAMUNE_GHOST_V3_SEGMENT_TABLE_OFFSET,
		SUSAMUNE_GHOST_V3_SEGMENT_TABLE_SIZE);
	ValidationRawCrc = CrcUpdate(
		ValidationRawCrc,
		bytes + SUSAMUNE_GHOST_V3_SEGMENT_TABLE_OFFSET,
		SUSAMUNE_GHOST_V3_SEGMENT_TABLE_SIZE);
	ValidationSample = 0;
	ValidationSegmentDuration = 0;
	ValidationSegment = 0;
	ValidationSegmentCount = ReadBe16(
		bytes + SUSAMUNE_GHOST_V4_SEGMENT_COUNT_OFFSET);
	ValidationVersion = ReadBe16(bytes + 4);
	ValidationAttachmentCount =
		ValidationVersion >= SUSAMUNE_GHOST_FILE_VERSION_V4
			? bytes[SUSAMUNE_GHOST_V4_ATTACHMENT_COUNT_OFFSET] : 0;
	ValidationAttachmentFlags =
		ValidationVersion >= SUSAMUNE_GHOST_FILE_VERSION_V4
			? ReadBe16(bytes + SUSAMUNE_GHOST_V4_ATTACHMENT_FLAGS_OFFSET) : 0;
	return VALIDATE_OK;
}

// Returns -1 while another bounded chunk remains, otherwise ValidateResult.
static int ContinueCanonicalValidation(void)
{
	u32 phaseEnd = ValidationPoseEnd;
	u32 stride = SUSAMUNE_GHOST_POSE_SAMPLE_SIZE;
	u32 remaining, chunk, end;
	if (ValidationOffset >= ValidationPoseEnd)
	{
		if (ValidationOffset == ValidationPoseEnd) {
			phaseEnd += SUSAMUNE_GHOST_TEACHING_HEADER_SIZE;
			stride = SUSAMUNE_GHOST_TEACHING_HEADER_SIZE;
		} else if (ValidationOffset < ValidationInputEnd) {
			phaseEnd = ValidationInputEnd;
			stride = ValidationInputStride;
		} else {
			phaseEnd = ValidationSize;
			stride = SUSAMUNE_GHOST_SPLIT_SAMPLE_SIZE;
		}
	}
	remaining = phaseEnd - ValidationOffset;
	chunk = remaining > SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE
	              ? SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE : remaining;
	chunk -= chunk % stride;
	end = ValidationOffset + chunk;
	u32 delta;
	u32 segmentFirst;
	u32 segmentSamples;
	u32 segmentDuration;
	bool segmentLast;
	s32 position;
	u32 animation;
	u8 yoshi;
	u8 held;
	const u8 *sample;
	const u8 *segment;

	ValidationPayloadCrc = CrcUpdate(ValidationPayloadCrc,
	                                 ValidationBytes + ValidationOffset, chunk);
	ValidationFileCrc = CrcUpdate(ValidationFileCrc,
	                              ValidationBytes + ValidationOffset, chunk);
	ValidationRawCrc = CrcUpdate(ValidationRawCrc,
	                             ValidationBytes + ValidationOffset, chunk);

	if (ValidationOffset > ValidationPoseEnd)
		ValidationTeachingCrc = CrcUpdate(ValidationTeachingCrc,
		    ValidationBytes + ValidationOffset, chunk);
	while (ValidationOffset < end)
	{
		sample = ValidationBytes + ValidationOffset;
		if (ValidationOffset >= ValidationPoseEnd)
		{
			if (ValidationOffset == ValidationPoseEnd) {
				ValidationOffset += SUSAMUNE_GHOST_TEACHING_HEADER_SIZE;
				continue;
			}
			if (ValidationOffset < ValidationInputEnd) {
				if (!SusamuneGhostTeachingInputValid(sample,
				        ReadBe32(ValidationBytes + 56), ReadBe32(ValidationBytes + 60),
				        ValidationPreviousInputQf, ValidationInputCount == 0) ||
				    (ValidationVersion == SUSAMUNE_GHOST_FILE_VERSION_V6 &&
				     !SusamuneGhostFluddValid(sample + 16)))
					return VALIDATE_INVALID;
				ValidationPreviousInputQf = ReadBe32(sample);
				ValidationInputCount++;
				ValidationOffset += ValidationInputStride;
			} else {
				const u8 *first = ValidationBytes + ValidationInputEnd;
				if (!SusamuneGhostTeachingSplitValid(sample,
				        ReadBe32(ValidationBytes + 56), ReadBe32(ValidationBytes + 60),
				        ValidationPreviousSplitQf, ReadBe16(first + 8),
				        ReadBe32(first + 4), ValidationSplitCount))
					return VALIDATE_INVALID;
				ValidationPreviousSplitQf = ReadBe32(sample);
				ValidationSplitCount++;
				ValidationOffset += SUSAMUNE_GHOST_SPLIT_SAMPLE_SIZE;
			}
			continue;
		}
		position = ReadBeS24(sample + 4);
		if (position < -SUSAMUNE_GHOST_MAX_POSITION_FIXED ||
		    position > SUSAMUNE_GHOST_MAX_POSITION_FIXED)
			return VALIDATE_INVALID;
		position = ReadBeS24(sample + 7);
		if (position < -SUSAMUNE_GHOST_MAX_POSITION_FIXED ||
		    position > SUSAMUNE_GHOST_MAX_POSITION_FIXED)
			return VALIDATE_INVALID;
		position = ReadBeS24(sample + 10);
		if (position < -SUSAMUNE_GHOST_MAX_POSITION_FIXED ||
		    position > SUSAMUNE_GHOST_MAX_POSITION_FIXED)
			return VALIDATE_INVALID;
		animation = ReadBe24(sample + 13);
		if ((animation >> 15) > SUSAMUNE_GHOST_ANIMATION_ID_MAX)
			return VALIDATE_INVALID;
		if (ValidationVersion == SUSAMUNE_GHOST_FILE_VERSION_V3)
		{
			if ((animation & SUSAMUNE_GHOST_ANIMATION_RESERVED_MASK) != 0)
				return VALIDATE_INVALID;
		}
		else
		{
			yoshi = (animation >> SUSAMUNE_GHOST_V4_YOSHI_SHIFT) &
			        SUSAMUNE_GHOST_V4_YOSHI_MASK;
			held = animation & SUSAMUNE_GHOST_V4_HELD_INDEX_MASK;
			if (yoshi > SUSAMUNE_GHOST_V4_YOSHI_UNKNOWN ||
			    (held != 0 && held > ValidationAttachmentCount &&
			     !(held == SUSAMUNE_GHOST_V4_HELD_UNKNOWN &&
			       (ValidationAttachmentFlags &
			        SUSAMUNE_GHOST_V4_ATTACHMENT_HELD_OVERFLOW) != 0)))
				return VALIDATE_INVALID;
		}

		delta = ReadBe16(sample + 2);
		if (ValidationSegment >= ValidationSegmentCount)
			return VALIDATE_INVALID;
		segment = ValidationBytes +
			SUSAMUNE_GHOST_V3_SEGMENT_TABLE_OFFSET +
			ValidationSegment * SUSAMUNE_GHOST_V3_SEGMENT_SIZE;
		segmentFirst = ReadBe32(segment);
		segmentSamples = ReadBe32(segment + 4);
		segmentDuration = ReadBe32(segment + 12) - ReadBe32(segment + 8);
		segmentLast = ValidationSample + 1 ==
			segmentFirst + segmentSamples;
		if (ValidationSample == segmentFirst)
		{
			if (delta != 0)
				return VALIDATE_INVALID;
			ValidationSegmentDuration = 0;
		}
		else
		{
			if (delta == 0 ||
			    (delta < SUSAMUNE_GHOST_TRANSFORM_INTERVAL_QF &&
			     !segmentLast))
				return VALIDATE_INVALID;
			ValidationSegmentDuration += delta;
			if (ValidationSegmentDuration > segmentDuration)
				return VALIDATE_INVALID;
		}
		if (segmentLast)
		{
			if (ValidationSegmentDuration != segmentDuration)
				return VALIDATE_INVALID;
			ValidationSegment++;
		}
		ValidationSample++;
		ValidationOffset += SUSAMUNE_GHOST_POSE_SAMPLE_SIZE;
	}

	if (ValidationOffset < ValidationSize)
		return -1;
	if (ValidationOffset != ValidationSize)
		return VALIDATE_INVALID;

	if (ValidationVersion >= SUSAMUNE_GHOST_FILE_VERSION_V5 &&
	    ((ValidationTeachingCrc ^ SUSAMUNE_GHOST_CRC32_XOR_OUT) !=
	        ReadBe32(ValidationBytes + ValidationPoseEnd + 20) ||
	     ValidationInputCount != ReadBe32(ValidationBytes + ValidationPoseEnd + 8) ||
	     ValidationSplitCount != ReadBe32(ValidationBytes + ValidationPoseEnd + 12)))
		return VALIDATE_INVALID;

	ValidationPayloadCrc ^= SUSAMUNE_GHOST_CRC32_XOR_OUT;
	ValidationFileCrc ^= SUSAMUNE_GHOST_CRC32_XOR_OUT;
	ValidationRawCrc ^= SUSAMUNE_GHOST_CRC32_XOR_OUT;
	if (ValidationSample != ReadBe32(ValidationBytes + 68) ||
	    ValidationSegment != ValidationSegmentCount ||
	    ValidationPayloadCrc != ReadBe32(ValidationBytes + 20) ||
	    ValidationFileCrc != ReadBe32(ValidationBytes + 12))
		return VALIDATE_INVALID;
	return VALIDATE_OK;
}

static bool GenerationIsNewer(u32 candidate, u32 current)
{
	return (s32)(candidate - current) > 0;
}

static void BuildGhostPath(char *path, u16 profile, u32 slot, u8 bank)
{
	_sprintf(path, "%s" MOONSHINE_GHOSTS_DIR "/%s/p%u/g%02u%c.sgh",
	         SusamuneCfgStoragePrefix(), GhostRegion, profile, slot,
	         bank == 0 ? 'a' : 'b');
}

static void BuildImportDirectory(char *path)
{
	_sprintf(path, "%s" MOONSHINE_GHOSTS_DIR "/%s",
	         SusamuneCfgStoragePrefix(), SUSAMUNE_GHOST_IMPORT_DIRECTORY);
}

static void BuildImportPath(char *path, const char *leaf)
{
	_sprintf(path, "%s" MOONSHINE_GHOSTS_DIR "/%s/%s",
	         SusamuneCfgStoragePrefix(), SUSAMUNE_GHOST_IMPORT_DIRECTORY,
	         leaf);
}

static u8 FoldAscii(u8 value)
{
	return value >= 'A' && value <= 'Z' ? (u8)(value + ('a' - 'A')) : value;
}

static bool ImportLeafIsValid(const WCHAR *source, char *out)
{
	static const char extension[] = SUSAMUNE_GHOST_SHARE_EXTENSION;
	u32 length = 0;
	u32 extensionLength = sizeof(extension) - 1u;
	u32 i;
	u8 value;

	while (source[length] != 0)
	{
		if (length + 1u >= SUSAMUNE_GHOST_IMPORT_LEAF_SIZE ||
		    source[length] < 0x20u || source[length] > 0x7Eu)
			return false;
		value = (u8)source[length];
		if (value == '"' || value == '*' || value == '/' || value == ':' ||
		    value == '<' || value == '>' || value == '?' || value == '\\' ||
		    value == '|')
			return false;
		out[length++] = (char)value;
	}
	if (length <= extensionLength || out[length - 1u] == ' ' ||
	    out[length - 1u] == '.')
		return false;
	out[length] = 0;
	for (i = 0; i < extensionLength; i++)
		if (FoldAscii((u8)out[length - extensionLength + i]) !=
		    FoldAscii((u8)extension[i]))
			return false;
	return true;
}

static const char *RouteAbbreviation(u8 area)
{
	switch (area)
	{
		case 0x00: return "AS";
		case 0x01: return "DP";
		case 0x02: return "BH";
		case 0x03: return "RH";
		case 0x04: return "GB";
		case 0x05: return "PP";
		case 0x06: return "SB";
		case 0x08: return "PV";
		case 0x09: return "NB";
		case 0x34: return "CM";
		case 0x3C: return "BW";
		default: return NULL;
	}
}

static void BuildExportRoute(char *route, const u8 *header)
{
	u8 area = header[44];
	u32 episode = header[45];
	u8 parentArea = header[46];
	s32 variant = ReadBeS32(header + 48);
	const char *abbreviation;

	// Internal starts use the logical parent episode when it was captured.
	if (parentArea != SUSAMUNE_GHOST_ROUTE_PARENT_NONE && variant >= 0)
	{
		area = parentArea;
		episode = (u32)variant;
	}
	abbreviation = RouteAbbreviation(area);
	if (abbreviation != NULL)
		_sprintf(route, "%s%u", abbreviation, episode + 1u);
	else
		_sprintf(route, "A%02XE%02X", area, episode);
}

static void BuildCompactTime(char *time, u32 durationQf)
{
	u32 millis = (u32)(((u64)durationQf * 1001u) / 120u);
	u32 minutes = millis / 60000u;
	u32 seconds = (millis / 1000u) % 60u;

	if (minutes != 0)
		_sprintf(time, "%u%02u%03u", minutes, seconds, millis % 1000u);
	else
		_sprintf(time, "%u%03u", seconds, millis % 1000u);
}

static void BuildExportPath(char *path, u16 profile, const u8 *header)
{
	char route[8];
	char time[8];
	u32 fatTime = get_fattime();
	u32 year = 1980u + ((fatTime >> 25) & 0x7Fu);
	u32 month = (fatTime >> 21) & 0x0Fu;
	u32 day = (fatTime >> 16) & 0x1Fu;

	BuildExportRoute(route, header);
	BuildCompactTime(time, ReadBe32(header + 64));
	_sprintf(path,
	         "%s" MOONSHINE_GHOSTS_DIR "/%s/%s/p%u/"
	         "%04u_%02u_%02u_%s_%s[%08X]%s",
	         SusamuneCfgStoragePrefix(), SUSAMUNE_GHOST_SHARE_DIRECTORY,
	         GhostRegion, profile, year, month, day, route, time,
	         ReadBe32(header + SUSAMUNE_GHOST_FILE_CHECKSUM_OFFSET),
	         SUSAMUNE_GHOST_SHARE_EXTENSION);
}

static enum ReadResult ReadEnvelope(u16 profile, u32 slot, u8 bank,
	                                struct SusamuneGhostStorageEnvelope *envelope,
	                                int *ioError)
{
	char path[GHOST_PATH_SIZE];
	u8 raw[SUSAMUNE_GHOST_ENVELOPE_SIZE];
	FIL file;
	UINT read = 0;
	UINT readSize;
	u32 i;
	FSIZE_t size;
	int ret;
	int closeRet;

	BuildGhostPath(path, profile, slot, bank);
	ret = f_open_char(&file, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE)
		return READ_MISSING;
	if (ret != FR_OK)
	{
		*ioError = ret;
		return READ_IO;
	}

	size = f_size(&file);
	readSize = size < sizeof(raw) ? (UINT)size : sizeof(raw);
	memset(raw, 0, sizeof(raw));
	ret = f_read(&file, raw, readSize, &read);
	closeRet = f_close(&file);
	if (ret != FR_OK || closeRet != FR_OK || read != readSize)
	{
		*ioError = ret != FR_OK ? ret :
		           closeRet != FR_OK ? closeRet : FR_DISK_ERR;
		return READ_IO;
	}

	if (read >= 6 && ReadBe32(raw) == SUSAMUNE_GHOST_ENVELOPE_MAGIC &&
	    ReadBe16(raw + 4) > SUSAMUNE_GHOST_ENVELOPE_VERSION)
		return READ_UNSAFE;
	if (size < sizeof(raw) || ReadBe32(raw) != SUSAMUNE_GHOST_ENVELOPE_MAGIC)
		return READ_INVALID;

	DecodeEnvelope(raw, envelope);
	if ((envelope->version != 1 && envelope->version != SUSAMUNE_GHOST_ENVELOPE_VERSION) ||
	    envelope->headerSize != SUSAMUNE_GHOST_ENVELOPE_SIZE ||
	    envelope->gameId != GAME_ID || envelope->profile != profile ||
	    envelope->slot != (u16)slot ||
	    (envelope->version == 1 ? slot > 0xFFFFu : envelope->reserved[0] != slot) ||
	    (envelope->flags & ~SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE) != 0 ||
	    envelope->payloadSize > SUSAMUNE_GHOST_MAX_FILE_SIZE ||
	    envelope->durationQf > SUSAMUNE_GHOST_MAX_DURATION_QF ||
	    envelope->headerChecksum != EnvelopeChecksum(envelope))
		return READ_INVALID;
	for (i = envelope->version == 1 ? 0 : 1; i < sizeof(envelope->reserved) / sizeof(envelope->reserved[0]); i++)
		if (envelope->reserved[i] != 0)
			return READ_INVALID;

	if ((envelope->flags & SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE) != 0)
	{
		if (envelope->payloadSize != 0 || envelope->durationQf != 0 ||
		    envelope->payloadChecksum != 0 || size != sizeof(*envelope))
			return READ_INVALID;
	}
	else if (envelope->payloadSize < SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
	         size != sizeof(*envelope) + envelope->payloadSize)
	{
		return READ_INVALID;
	}
	return READ_VALID;
}

static void CopyEnvelopeInfo(struct BankInfo *out,
	                         const struct SusamuneGhostStorageEnvelope *envelope)
{
	out->generation = envelope->generation;
	out->flags = envelope->flags;
	out->payloadSize = envelope->payloadSize;
	out->durationQf = envelope->durationQf;
	out->payloadChecksum = envelope->payloadChecksum;
	out->state = BANK_VALID;
}

static bool BanksEquivalent(const struct BankInfo *a,
	                        const struct BankInfo *b)
{
	return a->flags == b->flags && a->payloadSize == b->payloadSize &&
	       a->durationQf == b->durationQf &&
	       a->payloadChecksum == b->payloadChecksum;
}

static void MarkSlotUnsafe(struct SlotCatalog *slot)
{
	memset(&slot->info, 0, sizeof(slot->info));
	slot->unsafe = 1;
	slot->activeBank = -1;
	slot->info.flags = SUSAMUNE_GHOST_SLOT_UNSAFE;
	slot->info.status = SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE;
}

static void RecomputeSlot(void)
{
	struct SlotCatalog *slot = &Catalog[0];
	struct BankInfo *a = &slot->bank[0];
	struct BankInfo *b = &slot->bank[1];
	u32 delta;

	memset(&slot->info, 0, sizeof(slot->info));
	slot->unsafe = 0;
	slot->activeBank = -1;
	if (a->state == BANK_UNSAFE || b->state == BANK_UNSAFE)
	{
		MarkSlotUnsafe(slot);
		return;
	}
	if (a->state != BANK_VALID && b->state != BANK_VALID)
	{
		if (a->state == BANK_INVALID || b->state == BANK_INVALID)
			MarkSlotUnsafe(slot);
		return;
	}
	if (a->state != BANK_VALID)
		slot->activeBank = 1;
	else if (b->state != BANK_VALID)
		slot->activeBank = 0;
	else
	{
		delta = b->generation - a->generation;
		if (delta == 0)
		{
			if (!BanksEquivalent(a, b))
			{
				MarkSlotUnsafe(slot);
				return;
			}
			slot->activeBank = 0;
		}
		else if (delta == 0x80000000u)
		{
			MarkSlotUnsafe(slot);
			return;
		}
		else
			slot->activeBank = GenerationIsNewer(b->generation,
		                                            a->generation) ? 1 : 0;
	}
}

static void RecomputeCatalogTotals(void)
{
	CatalogUnsafe = Catalog[0].unsafe;
	CatalogCount = (Catalog[0].info.flags & SUSAMUNE_GHOST_SLOT_PRESENT) != 0;
	CatalogDurationQf = CatalogCount ? Catalog[0].info.durationQf : 0;
}

static u8 SanitizeText(char *out, u32 capacity, const u8 *in, u32 length)
{
	u32 i;
	u32 count = length < capacity - 1 ? length : capacity - 1;

	for (i = 0; i < count; i++)
		out[i] = in[i] >= 0x20 && in[i] <= 0x7e ? (char)in[i] : '?';
	out[count] = '\0';
	return (u8)count;
}

static void FillInfo(struct SusamuneGhostSlotInfo *info, const u8 *header,
	                 u32 generation, u16 flags)
{
	memset(info, 0, sizeof(*info));
	info->generation = generation;
	info->payloadSize = ReadBe32(header + 8);
	info->gameId = ReadBe32(header + 32);
	info->sampleCount = ReadBe32(header + 68);
	info->durationQf = ReadBe32(header + 64);
	info->resultQf = ReadBe32(header + 52);
	info->requiredFeatures = ReadBe32(header + 24);
	info->runFlags = ReadBe32(header + 28);
	info->routeVariant = ReadBeS32(header + 48);
	info->status = SUSAMUNE_GHOST_STATUS_OK;
	info->flags = SUSAMUNE_GHOST_SLOT_PRESENT | flags;
	info->sampleIntervalQf = ReadBe16(header + 42);
	info->routeArea = header[44];
	info->routeEpisode = header[45];
	info->routeParentArea = header[46];
	info->routeFlags = header[47];
	info->recordingMode = header[39];
	info->sampleCodec = header[40];
	info->discRevision = header[36];
	info->region = header[37];
	info->authorLength = SanitizeText(info->author, sizeof(info->author),
	                                header + 96, header[92]);
	info->nameLength = SanitizeText(info->name, sizeof(info->name),
	                              header + 120, header[93]);
	info->canonicalVersion = (u8)ReadBe16(header + 4);
}

static void FillSlotInfo(const u8 *header)
{
	struct SlotCatalog *slot = &Catalog[0];
	struct BankInfo *bank = &slot->bank[(u8)slot->activeBank];

	FillInfo(&slot->info, header, bank->generation, 0);
}

static void InsertImportCandidate(void)
{
	if (PageOrdinal >= Page.first && Page.count < SUSAMUNE_GHOST_CATALOG_PAGE_ENTRIES)
	{
		struct SusamuneGhostCatalogEntry *entry = &Page.entries[Page.count++];
		entry->id = 0;
		entry->info = ImportCandidate.info;
		memcpy(entry->leaf, ImportCandidate.leaf, sizeof(entry->leaf));
	}
	++PageOrdinal;
	PageDuration += ImportCandidate.info.durationQf;
}

static enum ReadResult ReadCanonicalHeader(u16 profile, u32 slot,
	                                       u8 bankIndex,
	                                       u8 *header,
	                                       int *ioError)
{
	struct BankInfo *bank = &Catalog[0].bank[bankIndex];
	char path[GHOST_PATH_SIZE];
	FIL file;
	UINT read = 0;
	int ret;
	int closeRet;
	enum ValidateResult validate;

	BuildGhostPath(path, profile, slot, bankIndex);
	ret = f_open_char(&file, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE)
		return READ_INVALID;
	if (ret != FR_OK)
	{
		*ioError = ret;
		return READ_IO;
	}
	if (f_size(&file) !=
	    sizeof(struct SusamuneGhostStorageEnvelope) + bank->payloadSize)
	{
		closeRet = f_close(&file);
		if (closeRet != FR_OK)
		{
			*ioError = closeRet;
			return READ_IO;
		}
		return READ_INVALID;
	}
	ret = f_lseek(&file, sizeof(struct SusamuneGhostStorageEnvelope));
	if (ret == FR_OK)
		ret = f_read(&file, header, SUSAMUNE_GHOST_FILE_HEADER_SIZE, &read);
	closeRet = f_close(&file);
	if (ret != FR_OK || read != SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
	    closeRet != FR_OK)
	{
		*ioError = ret != FR_OK ? ret :
		           closeRet != FR_OK ? closeRet : FR_DISK_ERR;
		return READ_IO;
	}

	validate = ValidateCanonicalHeader(header,
	                                   SUSAMUNE_GHOST_FILE_HEADER_SIZE,
	                                   profile, false);
	if (validate == VALIDATE_FORWARD)
		return READ_UNSAFE;
	if (validate != VALIDATE_OK || ReadBe32(header + 8) != bank->payloadSize ||
	    ReadBe32(header + 64) != bank->durationQf)
		return READ_INVALID;
	return READ_VALID;
}

static void DispatchRequest(void);

static int StorageStatusForResult(int result)
{
	return result == FR_NO_PATH || result == FR_NOT_READY ||
	       result == FR_WRITE_PROTECTED || result == FR_DENIED ||
	       result == FR_INVALID_DRIVE || result == FR_NO_FILESYSTEM
	           ? SUSAMUNE_GHOST_STATUS_STORAGE_UNAVAILABLE
	           : SUSAMUNE_GHOST_STATUS_IO(result);
}

static void PublishBusy(void)
{
	volatile struct SusamuneGhostStorageMailbox *mailbox = GhostBlock();

	mailbox->response.responseMagic = SUSAMUNE_GHOST_STORAGE_MAGIC;
	mailbox->response.protocolVersion = SUSAMUNE_GHOST_STORAGE_VERSION;
	mailbox->response.flags = SUSAMUNE_GHOST_RESPONSE_BUSY;
	mailbox->response.status = SUSAMUNE_GHOST_STATUS_OK;
	mailbox->response.payloadSize = 0;
	mailbox->response.generation = 0;
	mailbox->response.profile = Request.profile;
	sync_after_write((void*)&mailbox->response, 32);
}

static void FinishRequest(int status, u32 payloadSize, u32 generation)
{
	volatile struct SusamuneGhostStorageMailbox *mailbox = GhostBlock();
	mailbox->response.responseMagic = SUSAMUNE_GHOST_STORAGE_MAGIC;
	mailbox->response.protocolVersion = SUSAMUNE_GHOST_STORAGE_VERSION;
	mailbox->response.flags = GhostSupported ? SUSAMUNE_GHOST_RESPONSE_READY : 0;
	mailbox->response.ackSeq = Request.seq;
	mailbox->response.status = status;
	mailbox->response.payloadSize = payloadSize;
	mailbox->response.generation = generation;
	mailbox->response.slot = (Request.command == SUSAMUNE_GHOST_CMD_LIST ||
		Request.command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN) ? 0 : Request.slot;
	mailbox->response.slotCount = status == SUSAMUNE_GHOST_STATUS_OK &&
	(Request.command == SUSAMUNE_GHOST_CMD_LIST ||
		Request.command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN) ? Page.count : 0;
	mailbox->response.profile = Request.profile;
	GhostAckSeq = Request.seq;
	Phase = OP_IDLE;
	sync_after_write((void*)&mailbox->response, 32);
}

static void InvalidateRequestCatalog(void)
{
	if (Request.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE)
		ImportedCatalogReady = false;
	else
		CatalogReady = false;
}

static void FinishIoError(int result)
{
	if (IoFileOpen)
	{
		f_close(&IoFile);
		IoFileOpen = false;
	}
	if (ImportDirOpen)
	{
		f_closedir(&ImportDir);
		ImportDirOpen = false;
	}
	InvalidateRequestCatalog();
	FinishRequest(StorageStatusForResult(result), 0, 0);
}

static bool PersonalSlotValid(u32 slot)
{
	return slot != SUSAMUNE_GHOST_SLOT_AUTO && (slot < 45u || slot >= 48u);
}

static void InitPage(void)
{
	memset(&Page, 0, sizeof(Page));
	Page.magic = SUSAMUNE_GHOST_CATALOG_PAGE_MAGIC;
	Page.version = SUSAMUNE_GHOST_CATALOG_PAGE_VERSION;
	Page.first = Request.command == SUSAMUNE_GHOST_CMD_SAVE ? 0 : Request.slot;
	PageOrdinal = 0;
	PageDuration = 0;
	AutoReuseSlot = SUSAMUNE_GHOST_SLOT_AUTO;
}

static void PublishPage(void)
{
	Page.totalCount = PageOrdinal;
	Page.totalDurationQfLo = (u32)PageDuration;
	Page.totalDurationQfHi = (u32)(PageDuration >> 32);
	memcpy((void*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR, &Page, sizeof(Page));
	sync_after_write((void*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR, sizeof(Page));
	FinishRequest(SUSAMUNE_GHOST_STATUS_OK, sizeof(Page), 0);
}

static void BeginSingleSlot(u32 slot)
{
	memset(Catalog, 0, sizeof(Catalog));
	CatalogReady = false;
	CatalogUnsafe = false;
	CatalogProfile = Request.profile;
	ScanSlot = slot;
	ScanFile = 0;
	ScanHeaderAttempts = 0;
	ScanIoError = FR_OK;
	Phase = OP_SCAN_ENVELOPES;
}

static void BeginCatalogScan(void)
{
	ImportedCatalogReady = false;
	DirectoryScan = Request.command == SUSAMUNE_GHOST_CMD_LIST ||
	                Request.slot == SUSAMUNE_GHOST_SLOT_AUTO;
	if (DirectoryScan)
	{
		InitPage();
		Phase = OP_PERSONAL_SCAN_OPEN;
	}
	else
		BeginSingleSlot(Request.slot);
}

static void PersonalScanOpenPass(void)
{
	char path[GHOST_PATH_SIZE];
	int ret;
	_sprintf(path, "%s" MOONSHINE_GHOSTS_DIR "/%s/p%u",
	         SusamuneCfgStoragePrefix(), GhostRegion, Request.profile);
	ret = f_opendir_char(&ImportDir, path);
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	ImportDirOpen = true;
	Phase = OP_PERSONAL_SCAN_NEXT;
}

static bool ParsePersonalLeaf(const char *leaf, u32 *id, u8 *bank)
{
	const char *p = leaf;
	u32 value = 0;
	char canonical[32];
	if (*p++ != 'g' || *p < '0' || *p > '9') return false;
	while (*p >= '0' && *p <= '9')
	{
		const u32 digit = (u32)(*p++ - '0');
		if (value > (0xFFFFFFFFu - digit) / 10u) return false;
		value = value * 10u + digit;
	}
	if ((*p != 'a' && *p != 'b') || strcmp(p + 1, ".sgh") ||
	    !PersonalSlotValid(value))
		return false;
	*bank = *p == 'b';
	_sprintf(canonical, "g%02u%c.sgh", value, *p);
	if (strcmp(canonical, leaf)) return false;
	*id = value;
	return true;
}

static void PersonalScanNextPass(void)
{
	char leaf[32];
	u32 n;
	u32 id;
	u8 bank;
	int ret;
	memset(&ImportEntry, 0, sizeof(ImportEntry));
	ret = f_readdir(&ImportDir, &ImportEntry);
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	if (!ImportEntry.fname[0])
	{
		ret = f_closedir(&ImportDir);
		ImportDirOpen = false;
		if (ret != FR_OK)
		{
			FinishIoError(ret);
			return;
		}
		DirectoryScan = false;
		if (Request.command == SUSAMUNE_GHOST_CMD_LIST) PublishPage();
		else
		{
			Request.slot = AutoReuseSlot != SUSAMUNE_GHOST_SLOT_AUTO ? AutoReuseSlot :
			               Page.nextSlot == SUSAMUNE_GHOST_SLOT_AUTO ? 0 : Page.nextSlot;
			if (!PersonalSlotValid(Request.slot)) Request.slot = 48;
			AutoProbe = true;
			BeginSingleSlot(Request.slot);
		}
		return;
	}
	if (ImportEntry.fattrib & AM_DIR) return;
	for (n = 0; n < sizeof(leaf); ++n)
	{
		if (ImportEntry.fname[n] > 0x7Fu) return;
		leaf[n] = (char)ImportEntry.fname[n];
		if (!leaf[n]) break;
	}
	if (n == sizeof(leaf) || !ParsePersonalLeaf(leaf, &id, &bank)) return;
	if (id >= Page.nextSlot) Page.nextSlot = id + 1u;
	if (Page.nextSlot == 45u) Page.nextSlot = 48u;
	if (bank)
	{
		char path[GHOST_PATH_SIZE];
		FILINFO other;
		BuildGhostPath(path, Request.profile, id, 0);
		ret = f_stat_char(path, &other);
		if (ret == FR_OK) return;
		if (ret != FR_NO_FILE)
		{
			FinishIoError(ret);
			return;
		}
	}
	BeginSingleSlot(id);
}

static void FinishSingleSlot(void)
{
	CatalogReady = true;
	RecomputeCatalogTotals();
	if (DirectoryScan)
	{
		if (Request.command == SUSAMUNE_GHOST_CMD_SAVE && !CatalogUnsafe &&
			Catalog[0].activeBank >= 0 &&
		    (Catalog[0].bank[(u8)Catalog[0].activeBank].flags &
		         SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE) &&
		    ScanSlot < AutoReuseSlot)
			AutoReuseSlot = ScanSlot;
		if (CatalogCount || CatalogUnsafe)
		{
			if (PageOrdinal >= Page.first && Page.count < SUSAMUNE_GHOST_CATALOG_PAGE_ENTRIES)
			{
				struct SusamuneGhostCatalogEntry *entry = &Page.entries[Page.count++];
				entry->id = ScanSlot;
				entry->info = Catalog[0].info;
			}
			++PageOrdinal;
			PageDuration += CatalogDurationQf;
		}
		Phase = OP_PERSONAL_SCAN_NEXT;
		return;
	}
	if (AutoProbe && (CatalogCount || CatalogUnsafe))
	{
		++Request.slot;
		if (Request.slot == 45u) Request.slot = 48u;
		if (Request.slot == SUSAMUNE_GHOST_SLOT_AUTO)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_STORAGE_UNAVAILABLE, 0, 0);
			return;
		}
		BeginSingleSlot(Request.slot);
		return;
	}
	AutoProbe = false;
	DispatchRequest();
}

static void ScanEnvelopePass(void)
{
	struct SusamuneGhostStorageEnvelope envelope;
	enum ReadResult result;
	if (ScanFile == 2)
	{
		ScanFile = 0;
		Phase = OP_SCAN_HEADERS;
		return;
	}
	result = ReadEnvelope(Request.profile, ScanSlot, (u8)ScanFile, &envelope, &ScanIoError);
	if (result == READ_IO)
	{
		FinishIoError(ScanIoError);
		return;
	}
	if (result == READ_VALID)
		CopyEnvelopeInfo(&Catalog[0].bank[ScanFile], &envelope);
	else
		Catalog[0].bank[ScanFile].state = result == READ_UNSAFE ? BANK_UNSAFE :
		                                result == READ_MISSING ? BANK_EMPTY : BANK_INVALID;
	++ScanFile;
}

static void ScanHeaderPass(void)
{
	u8 header[SUSAMUNE_GHOST_FILE_HEADER_SIZE];
	enum ReadResult result;
	u8 bank;
	if (ScanFile == 2)
	{
		RecomputeSlot();
		Phase = OP_SCAN_ACTIVE_HEADERS;
		return;
	}
	bank = (u8)ScanFile++;
	if (Catalog[0].bank[bank].state != BANK_VALID ||
		(Catalog[0].bank[bank].flags & SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE)) return;
	result = ReadCanonicalHeader(Request.profile, ScanSlot, bank, header, &ScanIoError);
	if (result == READ_IO)
	{
		FinishIoError(ScanIoError);
		return;
	}
	if (result != READ_VALID)
		Catalog[0].bank[bank].state = result == READ_UNSAFE ? BANK_UNSAFE : BANK_INVALID;
}

static void ScanActiveHeaderPass(void)
{
	u8 header[SUSAMUNE_GHOST_FILE_HEADER_SIZE];
	enum ReadResult result;
	struct SlotCatalog *slot = &Catalog[0];
	u8 bank;
	if (slot->unsafe || slot->activeBank < 0 ||
	    (slot->bank[(u8)slot->activeBank].flags & SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE))
	{
		FinishSingleSlot();
		return;
	}
	bank = (u8)slot->activeBank;
	if (ScanHeaderAttempts & (1u << bank))
	{
		slot->bank[bank].state = BANK_INVALID;
		RecomputeSlot();
		return;
	}
	ScanHeaderAttempts |= (u8)(1u << bank);
	result = ReadCanonicalHeader(Request.profile, ScanSlot, bank, header, &ScanIoError);
	if (result == READ_IO)
	{
		FinishIoError(ScanIoError);
		return;
	}
	if (result != READ_VALID)
	{
		slot->bank[bank].state = result == READ_UNSAFE ? BANK_UNSAFE : BANK_INVALID;
		RecomputeSlot();
		return;
	}
	FillSlotInfo(header);
	FinishSingleSlot();
}

static void BeginImportScan(void)
{
	CatalogReady = false;
	memset(ImportedCatalog, 0, sizeof(ImportedCatalog));
	memset(&ImportCandidate, 0, sizeof(ImportCandidate));
	ImportedCatalogReady = false;
	ImportedCatalogCount = 0;
	DirectoryScan = Request.command == SUSAMUNE_GHOST_CMD_LIST || Request.command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN;
	if (DirectoryScan)
	{
		InitPage();
		Phase = OP_IMPORT_SCAN_OPEN;
	} else {
		memcpy(ImportCandidate.leaf, Request.leaf, sizeof(ImportCandidate.leaf));
		Phase = OP_IMPORT_SCAN_FILE_OPEN;
	}
}

static void EnsureCatalog(void)
{
	if (Request.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE) BeginImportScan();
	else BeginCatalogScan();
}

static void PrepareEnvelope(struct SusamuneGhostStorageEnvelope *envelope,
	                        u32 flags,
	                        u32 generation, u32 payloadSize,
	                        u32 durationQf, u32 payloadChecksum)
{
	memset(envelope, 0, sizeof(*envelope));
	envelope->magic = SUSAMUNE_GHOST_ENVELOPE_MAGIC;
	envelope->version = SUSAMUNE_GHOST_ENVELOPE_VERSION;
	envelope->headerSize = sizeof(*envelope);
	envelope->generation = generation;
	envelope->flags = flags;
	envelope->gameId = GAME_ID;
	envelope->profile = Request.profile;
	envelope->slot = (u16)Request.slot;
	envelope->reserved[0] = Request.slot;
	envelope->payloadSize = payloadSize;
	envelope->durationQf = durationQf;
	envelope->payloadChecksum = payloadChecksum;
	envelope->headerChecksum = EnvelopeChecksum(envelope);
}

static void StartLoadBank(u8 bank)
{
	IoBank = bank;
	LoadAttemptedBanks |= (u8)(1u << bank);
	IoOffset = 0;
	IoChecksum = SUSAMUNE_GHOST_CRC32_INIT;
	Phase = OP_LOAD_OPEN;
}

static void StartImportedLoad(void)
{
	IoOffset = 0;
	IoChecksum = SUSAMUNE_GHOST_CRC32_INIT;
	Phase = OP_LOAD_OPEN;
}

static void QuarantineImportedSlot(int status)
{
	memset(&ImportedCatalog[0].info, 0,
	       sizeof(ImportedCatalog[0].info));
	ImportedCatalog[0].info.flags =
		SUSAMUNE_GHOST_SLOT_PRESENT |
		SUSAMUNE_GHOST_SLOT_IMPORTED |
		SUSAMUNE_GHOST_SLOT_UNSAFE;
	ImportedCatalog[0].info.status = status;
}

static void FailCorruptLoad(void)
{
	struct SlotCatalog *slot = &Catalog[0];
	u8 next;

	if (Request.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE)
	{
		QuarantineImportedSlot(SUSAMUNE_GHOST_STATUS_INVALID_FILE);
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_FILE, 0, 0);
		return;
	}

	slot->bank[IoBank].state = BANK_INVALID;
	RecomputeSlot();
	RecomputeCatalogTotals();
	if (slot->unsafe)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE, 0, 0);
		return;
	}
	if (slot->activeBank >= 0)
	{
		next = (u8)slot->activeBank;
		if (slot->bank[next].generation != Request.expectedGeneration)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_FILE, 0, 0);
			return;
		}
		if ((slot->bank[next].flags &
		     SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE) == 0 &&
		    (LoadAttemptedBanks & (1u << next)) == 0)
		{
			StartLoadBank(next);
			return;
		}
	}
	FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_FILE, 0, 0);
}

static void DispatchRequest(void)
{
	struct SlotCatalog *slot;
	struct BankInfo *active;
	u32 generation;
	u32 duration;

	if (Request.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE)
	{
		if (!ImportedCatalogReady)
		{
			BeginImportScan();
			return;
		}
		if (Request.command == SUSAMUNE_GHOST_CMD_LIST ||
		    Request.command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN)
		{
			PublishPage();
			return;
		}
		if (ImportedCatalogCount == 0 ||
		    (ImportedCatalog[0].info.flags &
		     SUSAMUNE_GHOST_SLOT_PRESENT) == 0)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_NOT_FOUND, 0, 0);
			return;
		}
		if (ImportedCatalog[0].info.generation != Request.expectedGeneration)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_FILE, 0, 0);
			return;
		}
		if (Request.command == SUSAMUNE_GHOST_CMD_DELETE)
		{
			Phase = OP_IMPORTED_DELETE;
			return;
		}
		if ((ImportedCatalog[0].info.flags &
		     SUSAMUNE_GHOST_SLOT_UNSAFE) != 0)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE, 0, 0);
			return;
		}
		if (Request.command == SUSAMUNE_GHOST_CMD_LOAD)
		{
			StartImportedLoad();
			return;
		}
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_REQUEST, 0, 0);
		return;
	}

	if (!CatalogReady || CatalogProfile != Request.profile)
	{
		BeginCatalogScan();
		return;
	}
	if (Request.command == SUSAMUNE_GHOST_CMD_LIST)
	{
		PublishPage();
		return;
	}

	slot = &Catalog[0];
	if (Request.command != SUSAMUNE_GHOST_CMD_SAVE && slot->activeBank >= 0 &&
		slot->bank[(u8)slot->activeBank].generation != Request.expectedGeneration) {
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_FILE, 0, 0);
		return;
	}
	if (slot->unsafe)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE, 0, 0);
		return;
	}
	if (Request.command == SUSAMUNE_GHOST_CMD_LOAD ||
	    Request.command == SUSAMUNE_GHOST_CMD_EXPORT)
	{
		if (slot->activeBank < 0)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_NOT_FOUND, 0, 0);
			return;
		}
		active = &slot->bank[(u8)slot->activeBank];
		if ((active->flags & SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE) != 0)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_NOT_FOUND, 0,
			              active->generation);
			return;
		}
		LoadAttemptedBanks = 0;
		StartLoadBank((u8)slot->activeBank);
		return;
	}

	// An unreadable bank makes mutation of this slot ambiguous.
	if (CatalogUnsafe)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE, 0, 0);
		return;
	}
	if (Request.command == SUSAMUNE_GHOST_CMD_DELETE)
	{
		if (slot->activeBank < 0 ||
		    (slot->bank[(u8)slot->activeBank].flags &
		     SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE) != 0)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_NOT_FOUND, 0, 0);
			return;
		}
		generation = slot->bank[(u8)slot->activeBank].generation + 1u;
		IoBank = (u8)(1 - slot->activeBank);
		PrepareEnvelope(&IoEnvelope, SUSAMUNE_GHOST_ENVELOPE_TOMBSTONE,
		                generation, 0, 0, 0);
		Phase = OP_DELETE_OPEN;
		return;
	}
	if (Request.command != SUSAMUNE_GHOST_CMD_SAVE ||
	    !PersonalSlotValid(Request.slot))
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_SLOT, 0, 0);
		return;
	}
	if ((slot->info.flags & SUSAMUNE_GHOST_SLOT_PRESENT) != 0)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_SLOT_OCCUPIED, 0,
		              slot->info.generation);
		return;
	}


	duration = ReadBe32((const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR + 64);
	if (slot->activeBank >= 0)
	{
		generation = slot->bank[(u8)slot->activeBank].generation + 1u;
		IoBank = (u8)(1 - slot->activeBank);
	}
	else
	{
		generation = 1;
		IoBank = 0;
	}
	PrepareEnvelope(&IoEnvelope, 0, generation, Request.payloadSize,
	                duration, ValidationRawCrc);
	Phase = OP_SAVE_OPEN;
}

static void SaveOpenPass(void)
{
	char path[GHOST_PATH_SIZE];
	struct SusamuneGhostStorageEnvelope blank;
	UINT wrote = 0;
	int ret;

	BuildGhostPath(path, Request.profile, Request.slot, IoBank);
	ret = f_open_char(&IoFile, path, FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	IoFileOpen = true;
	memset(&blank, 0, sizeof(blank));
	ret = f_write(&IoFile, &blank, sizeof(blank), &wrote);
	if (ret != FR_OK || wrote != sizeof(blank))
	{
		FinishIoError(ret != FR_OK ? ret : FR_DISK_ERR);
		return;
	}
	IoOffset = 0;
	Phase = OP_SAVE_DATA;
}

static void SaveDataPass(void)
{
	u32 remaining = Request.payloadSize - IoOffset;
	u32 chunk = remaining > SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE
	              ? SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE : remaining;
	UINT wrote = 0;
	int ret;

	if (chunk == 0)
	{
		Phase = OP_SAVE_SYNC_DATA;
		return;
	}
	ret = f_write(&IoFile, (const void*)(SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR + IoOffset),
	              chunk, &wrote);
	if (ret != FR_OK || wrote != chunk)
	{
		FinishIoError(ret != FR_OK ? ret : FR_DISK_ERR);
		return;
	}
	IoOffset += chunk;
	if (IoOffset == Request.payloadSize)
		Phase = OP_SAVE_SYNC_DATA;
}

static void SaveSyncDataPass(void)
{
	int ret = f_sync(&IoFile);
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	Phase = OP_SAVE_COMMIT;
}

static void SaveCommitPass(void)
{
	UINT wrote = 0;
	int ret = f_lseek(&IoFile, 0);

	if (ret == FR_OK)
		ret = WriteEnvelope(&IoFile, &IoEnvelope, &wrote);
	if (ret != FR_OK || wrote != sizeof(IoEnvelope))
	{
		FinishIoError(ret != FR_OK ? ret : FR_DISK_ERR);
		return;
	}
	Phase = OP_SAVE_FINISH;
}

static void SaveFinishPass(void)
{
	struct SlotCatalog *slot = &Catalog[0];
	int ret = f_sync(&IoFile);
	int closeRet = f_close(&IoFile);

	IoFileOpen = false;
	if (ret != FR_OK || closeRet != FR_OK)
	{
		InvalidateRequestCatalog();
		FinishRequest(StorageStatusForResult(ret != FR_OK ? ret : closeRet),
		              0, 0);
		return;
	}
	CopyEnvelopeInfo(&slot->bank[IoBank], &IoEnvelope);
	RecomputeSlot();
	if (slot->activeBank != (s8)IoBank)
	{
		MarkSlotUnsafe(slot);
		RecomputeCatalogTotals();
		FinishRequest(SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE, 0, 0);
		return;
	}
	FillSlotInfo((const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR);
	RecomputeCatalogTotals();
	FinishRequest(SUSAMUNE_GHOST_STATUS_OK, 0, IoEnvelope.generation);
}

static void LoadOpenPass(void)
{
	struct BankInfo *bank = NULL;
	char path[GHOST_PATH_SIZE];
	u32 payloadSize;
	u32 dataOffset;
	int ret;
	int closeRet;

	if (Request.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE)
	{
		BuildImportPath(path, ImportedCatalog[0].leaf);
		payloadSize = ImportedCatalog[0].info.payloadSize;
		dataOffset = 0;
	}
	else
	{
		bank = &Catalog[0].bank[IoBank];
		BuildGhostPath(path, Request.profile, Request.slot, IoBank);
		payloadSize = bank->payloadSize;
		dataOffset = sizeof(struct SusamuneGhostStorageEnvelope);
	}
	ret = f_open_char(&IoFile, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE)
	{
		FailCorruptLoad();
		return;
	}
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	IoFileOpen = true;
	if (f_size(&IoFile) != (FSIZE_t)dataOffset + payloadSize)
	{
		closeRet = f_close(&IoFile);
		IoFileOpen = false;
		if (closeRet != FR_OK)
			FinishRequest(StorageStatusForResult(closeRet), 0, 0);
		else
			FailCorruptLoad();
		return;
	}
	ret = f_lseek(&IoFile, dataOffset);
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	IoOffset = 0;
	IoChecksum = SUSAMUNE_GHOST_CRC32_INIT;
	Phase = OP_LOAD_DATA;
}

static void LoadDataPass(void)
{
	u32 payloadSize = Request.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE
		? ImportedCatalog[0].info.payloadSize
		: Catalog[0].bank[IoBank].payloadSize;
	u32 remaining = payloadSize - IoOffset;
	u32 chunk = remaining > SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE
	              ? SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE : remaining;
	UINT read = 0;
	int ret;

	if (chunk == 0)
	{
		Phase = OP_LOAD_CLOSE;
		return;
	}
	ret = f_read(&IoFile, (void*)(SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR + IoOffset), chunk, &read);
	if (ret != FR_OK || read != chunk)
	{
		FinishIoError(ret != FR_OK ? ret : FR_DISK_ERR);
		return;
	}
	IoChecksum = CrcUpdate(IoChecksum,
	                       (const u8*)(SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR + IoOffset), chunk);
	IoOffset += chunk;
	if (IoOffset == payloadSize)
		Phase = OP_LOAD_CLOSE;
}

static void LoadClosePass(void)
{
	struct SlotCatalog *slot = NULL;
	struct BankInfo *bank = NULL;
	bool imported = Request.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
	u32 payloadSize;
	enum ValidateResult validate;
	int closeRet = f_close(&IoFile);

	IoFileOpen = false;
	if (closeRet != FR_OK)
	{
		FinishRequest(StorageStatusForResult(closeRet), 0, 0);
		return;
	}
	IoChecksum ^= SUSAMUNE_GHOST_CRC32_XOR_OUT;
	if (imported)
		payloadSize = ImportedCatalog[0].info.payloadSize;
	else
	{
		slot = &Catalog[0];
		bank = &slot->bank[IoBank];
		payloadSize = bank->payloadSize;
	}
	if (!imported && IoChecksum != bank->payloadChecksum)
	{
		FailCorruptLoad();
		return;
	}
	validate = BeginCanonicalValidation((const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR,
	                                    payloadSize, Request.profile,
	                                    imported);
	if (validate == VALIDATE_FORWARD)
	{
		if (imported)
			ImportedCatalogReady = false;
		else
		{
			bank->state = BANK_UNSAFE;
			RecomputeSlot();
			RecomputeCatalogTotals();
		}
		FinishRequest(SUSAMUNE_GHOST_STATUS_FORWARD_VERSION, 0, 0);
		return;
	}
	if (validate != VALIDATE_OK)
	{
		FailCorruptLoad();
		return;
	}
	Phase = OP_VALIDATE_LOAD;
}

static void ValidateLoadPass(void)
{
	bool imported = Request.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE;
	struct SlotCatalog *slot = imported ? NULL : &Catalog[0];
	struct BankInfo *bank = imported ? NULL : &slot->bank[IoBank];
	u32 payloadSize = imported
		? ImportedCatalog[0].info.payloadSize : bank->payloadSize;
	int validate = ContinueCanonicalValidation();

	if (validate < 0)
		return;
	if (validate != VALIDATE_OK)
	{
		FailCorruptLoad();
		return;
	}
	if (!imported && ValidationRawCrc != bank->payloadChecksum)
	{
		FailCorruptLoad();
		return;
	}
	if (imported && ReadBe32((const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR + 12) != Request.expectedGeneration)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_FILE, 0, 0);
		return;
	}
	if (imported)
	{
		FillInfo(&ImportedCatalog[0].info,
		         (const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR, Request.expectedGeneration,
		         SUSAMUNE_GHOST_SLOT_IMPORTED);
		}
	else
	{
		FillSlotInfo((const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR);
		RecomputeCatalogTotals();
	}
	if (Request.command == SUSAMUNE_GHOST_CMD_EXPORT)
	{
		Phase = OP_EXPORT_OPEN;
		return;
	}
	sync_after_write((void*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR, payloadSize);
	FinishRequest(SUSAMUNE_GHOST_STATUS_OK, payloadSize,
	              imported ? ReadBe32((const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR +
	                                  SUSAMUNE_GHOST_FILE_CHECKSUM_OFFSET)
	                       : bank->generation);
}

static void ExportOpenPass(void)
{
	char path[GHOST_PATH_SIZE];
	u8 blank[SUSAMUNE_GHOST_FILE_HEADER_SIZE];
	UINT wrote = 0;
	int ret;

	BuildExportPath(path, Request.profile, (const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR);
	ret = f_open_char(&IoFile, path, FA_WRITE | FA_CREATE_NEW);
	if (ret != FR_OK)
	{
		if (ret == FR_EXIST)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_SLOT_OCCUPIED, 0, 0);
			return;
		}
		FinishIoError(ret);
		return;
	}
	IoFileOpen = true;
	memset(blank, 0, sizeof(blank));
	ret = f_write(&IoFile, blank, sizeof(blank), &wrote);
	if (ret != FR_OK || wrote != sizeof(blank))
	{
		FinishIoError(ret != FR_OK ? ret : FR_DISK_ERR);
		return;
	}
	IoOffset = SUSAMUNE_GHOST_FILE_HEADER_SIZE;
	Phase = OP_EXPORT_DATA;
}

static void ExportDataPass(void)
{
	u32 remaining = ValidationSize - IoOffset;
	u32 chunk = remaining > SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE
	              ? SUSAMUNE_GHOST_STORAGE_CHUNK_SIZE : remaining;
	UINT wrote = 0;
	int ret;

	if (chunk == 0)
	{
		Phase = OP_EXPORT_SYNC_DATA;
		return;
	}
	ret = f_write(&IoFile, (const void*)(SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR + IoOffset),
	              chunk, &wrote);
	if (ret != FR_OK || wrote != chunk)
	{
		FinishIoError(ret != FR_OK ? ret : FR_DISK_ERR);
		return;
	}
	IoOffset += chunk;
	if (IoOffset == ValidationSize)
		Phase = OP_EXPORT_SYNC_DATA;
}

static void ExportSyncDataPass(void)
{
	int ret = f_sync(&IoFile);
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	Phase = OP_EXPORT_COMMIT;
}

static void ExportCommitPass(void)
{
	UINT wrote = 0;
	int ret = f_lseek(&IoFile, 0);

	if (ret == FR_OK)
		ret = f_write(&IoFile, (const void*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR,
		              SUSAMUNE_GHOST_FILE_HEADER_SIZE, &wrote);
	if (ret != FR_OK || wrote != SUSAMUNE_GHOST_FILE_HEADER_SIZE)
	{
		FinishIoError(ret != FR_OK ? ret : FR_DISK_ERR);
		return;
	}
	Phase = OP_EXPORT_FINISH;
}

static void ExportFinishPass(void)
{
	struct BankInfo *bank = &Catalog[0].bank[IoBank];
	int ret = f_sync(&IoFile);
	int closeRet = f_close(&IoFile);

	IoFileOpen = false;
	if (ret != FR_OK || closeRet != FR_OK)
	{
		InvalidateRequestCatalog();
		FinishRequest(StorageStatusForResult(ret != FR_OK ? ret : closeRet),
		              0, 0);
		return;
	}
	FinishRequest(SUSAMUNE_GHOST_STATUS_OK, 0, bank->generation);
}

static void ImportScanOpenPass(void)
{
	char path[GHOST_PATH_SIZE];
	int ret;

	BuildImportDirectory(path);
	ret = f_opendir_char(&ImportDir, path);
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	ImportDirOpen = true;
	Phase = OP_IMPORT_SCAN_NEXT;
}

static void ImportScanNextPass(void)
{
	int ret;
	int closeRet;

	memset(&ImportEntry, 0, sizeof(ImportEntry));
	ret = f_readdir(&ImportDir, &ImportEntry);
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	if (ImportEntry.fname[0] == 0)
	{
		closeRet = f_closedir(&ImportDir);
		ImportDirOpen = false;
		if (closeRet != FR_OK)
		{
			FinishRequest(StorageStatusForResult(closeRet), 0, 0);
			return;
		}
		ImportedCatalogReady = true;
		PublishPage();
		return;
	}
	if ((ImportEntry.fattrib & AM_DIR) != 0)
		return;

	memset(&ImportCandidate, 0, sizeof(ImportCandidate));
	if (!ImportLeafIsValid(ImportEntry.fname, ImportCandidate.leaf))
		return;
	if (ImportEntry.fsize < SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
	    ImportEntry.fsize > SUSAMUNE_GHOST_MAX_FILE_SIZE ||
	    ImportEntry.fsize > SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE)
		return;
	ImportCandidateSize = (u32)ImportEntry.fsize;
	Phase = OP_IMPORT_SCAN_FILE_OPEN;
}

static void ImportScanFileOpenPass(void)
{
	char path[GHOST_PATH_SIZE];
	int ret;
	int closeRet;

	BuildImportPath(path, ImportCandidate.leaf);
	ret = f_open_char(&IoFile, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE)
	{
		if (DirectoryScan) Phase = OP_IMPORT_SCAN_NEXT;
		else FinishRequest(SUSAMUNE_GHOST_STATUS_NOT_FOUND, 0, 0);
		return;
	}
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	IoFileOpen = true;
	if (!DirectoryScan) ImportCandidateSize = (u32)f_size(&IoFile);
	if (ImportCandidateSize < SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
		ImportCandidateSize > SUSAMUNE_GHOST_MAX_FILE_SIZE ||
		f_size(&IoFile) != ImportCandidateSize)
	{
		closeRet = f_close(&IoFile);
		IoFileOpen = false;
		if (closeRet != FR_OK)
			FinishIoError(closeRet);
		else
			if (DirectoryScan) Phase = OP_IMPORT_SCAN_NEXT;
		else FinishRequest(SUSAMUNE_GHOST_STATUS_NOT_FOUND, 0, 0);
		return;
	}
	ImportPrefixSize = ImportCandidateSize < SUSAMUNE_GHOST_V3_SAMPLE_DATA_OFFSET
		? ImportCandidateSize : SUSAMUNE_GHOST_V3_SAMPLE_DATA_OFFSET;
	Phase = OP_IMPORT_SCAN_FILE_READ;
}

static void ImportScanFileReadPass(void)
{
	UINT read = 0;
	int ret = f_read(&IoFile, (void*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR,
	                 ImportPrefixSize, &read);

	if (ret != FR_OK || read != ImportPrefixSize)
	{
		FinishIoError(ret != FR_OK ? ret : FR_DISK_ERR);
		return;
	}
	Phase = OP_IMPORT_SCAN_FILE_CLOSE;
}

static void ImportScanFileClosePass(void)
{
	const u8 *bytes = (const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR;
	enum ValidateResult validate;
	bool portable;
	int closeRet = f_close(&IoFile);

	IoFileOpen = false;
	if (closeRet != FR_OK)
	{
		FinishIoError(closeRet);
		return;
	}
	validate = ValidateCanonicalHeader(bytes, ImportCandidateSize,
	                                   SUSAMUNE_GHOST_IMPORTED_PROFILE,
	                                   true);
	if (validate != VALIDATE_OK ||
	    ReadBe32(bytes + 8) != ImportCandidateSize ||
	    ImportPrefixSize < SUSAMUNE_GHOST_V3_SAMPLE_DATA_OFFSET)
	{
		if (DirectoryScan) Phase = OP_IMPORT_SCAN_NEXT;
		else FinishRequest(SUSAMUNE_GHOST_STATUS_NOT_FOUND, 0, 0);
		return;
	}
	portable = ReadBe32(bytes + 32) != GAME_ID;
	if (!ValidateV3SegmentTable(bytes, portable))
	{
		if (DirectoryScan) Phase = OP_IMPORT_SCAN_NEXT;
		else FinishRequest(SUSAMUNE_GHOST_STATUS_NOT_FOUND, 0, 0);
		return;
	}
	FillInfo(&ImportCandidate.info, bytes, ReadBe32(bytes + 12),
	         SUSAMUNE_GHOST_SLOT_IMPORTED);
	if (DirectoryScan)
	{
		InsertImportCandidate();
		Phase = OP_IMPORT_SCAN_NEXT;
	} else {
		ImportedCatalog[0] = ImportCandidate;
		ImportedCatalogCount = 1;
		ImportedCatalogReady = true;
		DispatchRequest();
	}
}

static void DeleteOpenPass(void)
{
	char path[GHOST_PATH_SIZE];
	UINT wrote = 0;
	int ret;

	BuildGhostPath(path, Request.profile, Request.slot, IoBank);
	ret = f_open_char(&IoFile, path, FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	IoFileOpen = true;
	ret = WriteEnvelope(&IoFile, &IoEnvelope, &wrote);
	if (ret != FR_OK || wrote != sizeof(IoEnvelope))
	{
		FinishIoError(ret != FR_OK ? ret : FR_DISK_ERR);
		return;
	}
	Phase = OP_DELETE_FINISH;
}

static void DeleteFinishPass(void)
{
	struct SlotCatalog *slot = &Catalog[0];
	int ret = f_sync(&IoFile);
	int closeRet = f_close(&IoFile);

	IoFileOpen = false;
	if (ret != FR_OK || closeRet != FR_OK)
	{
		InvalidateRequestCatalog();
		FinishRequest(StorageStatusForResult(ret != FR_OK ? ret : closeRet),
		              0, 0);
		return;
	}
	CopyEnvelopeInfo(&slot->bank[IoBank], &IoEnvelope);
	RecomputeSlot();
	if (slot->activeBank != (s8)IoBank)
		MarkSlotUnsafe(slot);
	RecomputeCatalogTotals();
	if (slot->unsafe)
		FinishRequest(SUSAMUNE_GHOST_STATUS_SLOT_UNSAFE, 0, 0);
	else
		Phase = OP_DELETE_RECLAIM;
}

static void DeleteReclaimPass(void)
{
	char path[GHOST_PATH_SIZE];
	int ret;

	// The committed tombstone wins even if power fails during reclamation.
	BuildGhostPath(path, Request.profile, Request.slot, (u8)(1u - IoBank));
	ret = f_unlink_char(path);
	if (ret != FR_OK && ret != FR_NO_FILE)
	{
		FinishIoError(ret);
		return;
	}
	InvalidateRequestCatalog();
	FinishRequest(SUSAMUNE_GHOST_STATUS_OK, 0, IoEnvelope.generation);
}

static void ImportedDeletePass(void)
{
	char path[GHOST_PATH_SIZE];
	int ret;

	BuildImportPath(path, ImportedCatalog[0].leaf);
	ret = f_unlink_char(path);
	if (ret == FR_NO_FILE)
	{
		ImportedCatalogReady = false;
		FinishRequest(SUSAMUNE_GHOST_STATUS_NOT_FOUND, 0, 0);
		return;
	}
	if (ret != FR_OK)
	{
		FinishIoError(ret);
		return;
	}
	ImportedCatalogReady = false;
	FinishRequest(SUSAMUNE_GHOST_STATUS_OK, 0, 0);
}

static void StartRequest(void)
{
	volatile struct SusamuneGhostStorageMailbox *mailbox = GhostBlock();
	const volatile struct SusamuneGhostStorageRequest *request =
		&mailbox->request;
	enum ValidateResult validate;

	sync_before_read((void*)request, 32);
	if (request->requestSeq == GhostAckSeq)
		return;
	Request.seq = request->requestSeq;
	Request.command = request->command;
	Request.profile = request->profile;
	Request.slot = request->slot;
	Request.payloadSize = request->payloadSize;
	Request.flags = request->flags;
	Request.expectedGeneration = request->expectedGeneration;
	AutoProbe = false;
	PublishBusy();

	if (request->requestMagic != SUSAMUNE_GHOST_STORAGE_MAGIC ||
	    request->protocolVersion != SUSAMUNE_GHOST_STORAGE_VERSION ||
	    request->reserved != 0 || Request.flags != 0)
	{
		FinishRequest(request->requestMagic == SUSAMUNE_GHOST_STORAGE_MAGIC &&
		              request->protocolVersion > SUSAMUNE_GHOST_STORAGE_VERSION
		                  ? SUSAMUNE_GHOST_STATUS_FORWARD_VERSION
		                  : SUSAMUNE_GHOST_STATUS_INVALID_REQUEST,
		              0, 0);
		return;
	}
	if (!GhostSupported)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_STORAGE_UNAVAILABLE, 0, 0);
		return;
	}
	if (Request.command < SUSAMUNE_GHOST_CMD_SAVE ||
	    Request.command > SUSAMUNE_GHOST_CMD_IMPORT_SCAN)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_REQUEST, 0, 0);
		return;
	}
	if (Request.profile > SUSAMUNE_GHOST_IMPORTED_PROFILE)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_SLOT, 0, 0);
		return;
	}
	if (Request.command == SUSAMUNE_GHOST_CMD_LIST || Request.command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN)
	{
		if (Request.payloadSize || Request.expectedGeneration ||
			(Request.command == SUSAMUNE_GHOST_CMD_IMPORT_SCAN && Request.profile != SUSAMUNE_GHOST_IMPORTED_PROFILE)) {
			FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_REQUEST, 0, 0);
			return;
		}
		EnsureCatalog();
		return;
	}
	if (Request.profile == SUSAMUNE_GHOST_IMPORTED_PROFILE)
	{
		char normalized[SUSAMUNE_GHOST_IMPORT_LEAF_SIZE];
		WCHAR wide[SUSAMUNE_GHOST_IMPORT_LEAF_SIZE];
		u32 length;
		if ((Request.command != SUSAMUNE_GHOST_CMD_LOAD && Request.command != SUSAMUNE_GHOST_CMD_DELETE) ||
			Request.payloadSize != sizeof(Request.leaf)) {
			FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_REQUEST, 0, 0);
			return;
		}
		sync_before_read((void*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR, sizeof(Request.leaf));
		memcpy(Request.leaf, (const void*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR, sizeof(Request.leaf));
		for (length = 0; length < sizeof(Request.leaf); ++length)
		{
			wide[length] = (u8)Request.leaf[length];
			if (!wide[length]) break;
		}
		if (length == sizeof(Request.leaf) || !ImportLeafIsValid(wide, normalized) || strcmp(normalized, Request.leaf))
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_REQUEST, 0, 0);
			return;
		}
		EnsureCatalog();
		return;
	}
	if (!PersonalSlotValid(Request.slot) &&
		!(Request.command == SUSAMUNE_GHOST_CMD_SAVE && Request.slot == SUSAMUNE_GHOST_SLOT_AUTO)) {
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_SLOT, 0, 0);
		return;
	}
	if (Request.command != SUSAMUNE_GHOST_CMD_SAVE)
	{
		if (Request.payloadSize)
		{
			FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_REQUEST, 0, 0);
			return;
		}
		EnsureCatalog();
		return;
	}
	if (Request.expectedGeneration)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_REQUEST, 0, 0);
		return;
	}
	if (Request.payloadSize < SUSAMUNE_GHOST_FILE_HEADER_SIZE ||
	    Request.payloadSize > SUSAMUNE_GHOST_MAX_FILE_SIZE ||
	    Request.payloadSize > SUSAMUNE_GHOST_STORAGE_PAYLOAD_SIZE)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_PAYLOAD_TOO_LARGE, 0, 0);
		return;
	}

	sync_before_read((void*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR, Request.payloadSize);
	validate = BeginCanonicalValidation((const u8*)SUSAMUNE_GHOST_STORAGE_DATA_PHYS_PTR,
	                                    Request.payloadSize,
	                                    Request.profile, false);
	if (validate == VALIDATE_FORWARD)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_FORWARD_VERSION, 0, 0);
		return;
	}
	if (validate != VALIDATE_OK)
	{
		FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_FILE, 0, 0);
		return;
	}
	Phase = OP_VALIDATE_SAVE;
}

void SusamuneGhostInit(void)
{
	volatile struct SusamuneGhostStorageMailbox *mailbox = GhostBlock();

	memset((void*)mailbox, 0, SUSAMUNE_GHOST_STORAGE_HEADER_SIZE);
	GhostRegion = SUSAMUNE_MOD_REGION_TAG(GAME_ID);
	GhostRegionId = GAME_ID == SUSAMUNE_GHOST_GAME_ID_JP
		? SUSAMUNE_GHOST_REGION_JP
		: GAME_ID == SUSAMUNE_GHOST_GAME_ID_US
		? SUSAMUNE_GHOST_REGION_US : SUSAMUNE_GHOST_REGION_PAL;
	GhostSupported = SusamuneCfgStorageAvailable() && GhostRegion != NULL;
	GhostAckSeq = 0;
	CatalogReady = false;
	CatalogUnsafe = false;
	ImportedCatalogReady = false;
	ImportedCatalogCount = 0;
	IoFileOpen = false;
	ImportDirOpen = false;
	Phase = OP_IDLE;

	mailbox->response.responseMagic = SUSAMUNE_GHOST_STORAGE_MAGIC;
	mailbox->response.protocolVersion = SUSAMUNE_GHOST_STORAGE_VERSION;
	mailbox->response.flags = GhostSupported
		? SUSAMUNE_GHOST_RESPONSE_READY : 0;
	mailbox->response.ackSeq = 0;
	mailbox->response.status = GhostSupported
		? SUSAMUNE_GHOST_STATUS_OK
		: SUSAMUNE_GHOST_STATUS_STORAGE_UNAVAILABLE;
	mailbox->response.slotCount = 0;
	sync_after_write((void*)mailbox, SUSAMUNE_GHOST_STORAGE_HEADER_SIZE);
}

bool SusamuneGhostPending(void)
{
	volatile struct SusamuneGhostStorageMailbox *mailbox = GhostBlock();

	if (Phase != OP_IDLE)
		return true;
	sync_before_read((void*)&mailbox->request, 32);
	return mailbox->request.requestSeq != GhostAckSeq;
}

void SusamuneGhostService(void)
{
	int validate;

	switch (Phase)
	{
		case OP_IDLE:
			StartRequest();
			break;
		case OP_VALIDATE_SAVE:
			validate = ContinueCanonicalValidation();
			if (validate < 0)
				break;
			if (validate != VALIDATE_OK)
				FinishRequest(SUSAMUNE_GHOST_STATUS_INVALID_FILE, 0, 0);
			else
				EnsureCatalog();
			break;
		case OP_PERSONAL_SCAN_OPEN:
			PersonalScanOpenPass();
			break;
		case OP_PERSONAL_SCAN_NEXT:
			PersonalScanNextPass();
			break;
		case OP_SCAN_ENVELOPES:
			ScanEnvelopePass();
			break;
		case OP_SCAN_HEADERS:
			ScanHeaderPass();
			break;
		case OP_SCAN_ACTIVE_HEADERS:
			ScanActiveHeaderPass();
			break;
		case OP_SAVE_OPEN:
			SaveOpenPass();
			break;
		case OP_SAVE_DATA:
			SaveDataPass();
			break;
		case OP_SAVE_SYNC_DATA:
			SaveSyncDataPass();
			break;
		case OP_SAVE_COMMIT:
			SaveCommitPass();
			break;
		case OP_SAVE_FINISH:
			SaveFinishPass();
			break;
		case OP_LOAD_OPEN:
			LoadOpenPass();
			break;
		case OP_LOAD_DATA:
			LoadDataPass();
			break;
		case OP_LOAD_CLOSE:
			LoadClosePass();
			break;
		case OP_VALIDATE_LOAD:
			ValidateLoadPass();
			break;
		case OP_EXPORT_OPEN:
			ExportOpenPass();
			break;
		case OP_EXPORT_DATA:
			ExportDataPass();
			break;
		case OP_EXPORT_SYNC_DATA:
			ExportSyncDataPass();
			break;
		case OP_EXPORT_COMMIT:
			ExportCommitPass();
			break;
		case OP_EXPORT_FINISH:
			ExportFinishPass();
			break;
		case OP_IMPORT_SCAN_OPEN:
			ImportScanOpenPass();
			break;
		case OP_IMPORT_SCAN_NEXT:
			ImportScanNextPass();
			break;
		case OP_IMPORT_SCAN_FILE_OPEN:
			ImportScanFileOpenPass();
			break;
		case OP_IMPORT_SCAN_FILE_READ:
			ImportScanFileReadPass();
			break;
		case OP_IMPORT_SCAN_FILE_CLOSE:
			ImportScanFileClosePass();
			break;
		case OP_DELETE_OPEN:
			DeleteOpenPass();
			break;
		case OP_DELETE_FINISH:
			DeleteFinishPass();
			break;
		case OP_DELETE_RECLAIM:
			DeleteReclaimPass();
			break;
		case OP_IMPORTED_DELETE:
			ImportedDeletePass();
			break;
	}
}
