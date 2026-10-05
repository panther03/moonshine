#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "dip.h"
#include "exi.h"
#include "ff_utf8.h"
#include "global.h"
#include "SusamuneShadowAsset.h"

#include "susamune/ghost_model_asset.h"
#include "susamune/ciso_reader.h"
#include "susamune/data_paths.h"

#define SHADOW_INPUT_SIZE         0x8000u
#define SHADOW_CACHE_SIZE         0x1000u
#define SHADOW_HISTORY_SIZE       0x1000u
#define SHADOW_RARC_META_MAX      0x10000u
#define SHADOW_DECODED_MIN_SIZE   0x40000u
#define SHADOW_DECODED_MAX_SIZE   0x600000u
#define SHADOW_COMPRESSED_MAX_SIZE 0x600000u
#define SHADOW_FST_MAX_SIZE       0x400000u
#define SHADOW_GAME_MAX_SIZE      0x57058000u
#define SHADOW_NAME_MAX           64u

typedef enum ShadowSourceKind {
	SHADOW_SOURCE_FILE,
	SHADOW_SOURCE_REAL_DISC
} ShadowSourceKind;

typedef struct ShadowReader {
	ShadowSourceKind kind;
	FIL file;
	bool fileOpen;
	u32 discCommand;
	u64 size;
	bool ciso;
	u64 cacheStart[2];
	u32 cacheSize[2];
	u32 nextCache;
} ShadowReader;

typedef struct ShadowInput {
	ShadowReader *reader;
	u64 start;
	u32 size;
	u32 position;
	u32 buffered;
	u32 bufferPosition;
	u8 buffer[SHADOW_INPUT_SIZE];
} ShadowInput;

typedef struct ShadowDecoder {
	ShadowInput input;
	u8 history[SHADOW_HISTORY_SIZE];
	u8 metadata[SHADOW_RARC_META_MAX];
	u32 outputSize;
	u32 outputPosition;
	u32 dataBase;
	u32 bmdSource;
	u32 btkSource;
	u32 bmdCaptured;
	u32 btkCaptured;
	bool rarcReady;
	const struct AssetSpec *spec;
} ShadowDecoder;

typedef struct FstEntry {
	u32 nameAndType;
	u32 offset;
	u32 size;
} FstEntry;

typedef struct ShadowAssetHeader {
	u32 magic;
	u16 version;
	u16 headerSize;
	s32 status;
	u32 totalSize;
	u32 bmdOffset;
	u32 bmdSize;
	u32 payloadChecksum;
	u32 reserved;
} ShadowAssetHeader;

typedef struct AssetSpec {
	const char *label;
	const char *archivePath;
	const char *archiveLeaf;
	const char *nodeName;
	const char *bmdName;
	const char *btkName;
	volatile u8 *base;
	volatile u8 *payload;
	u32 magic;
	u32 bufferSize;
	u32 totalSize;
	u32 bmdOffset;
	u32 bmdSize;
	u32 btkOffset;
	u32 btkSize;
	u32 payloadChecksum;
	u32 bmdChecksum;
	u32 btkChecksum;
} AssetSpec;

typedef char ShadowAssetHeaderSize[
	sizeof(ShadowAssetHeader) == SUSAMUNE_GHOST_SHADOW_ASSET_HEADER_SIZE ? 1 : -1];

static u8 sDiscReadScratch[SHADOW_INPUT_SIZE + 0x20] ATTRIBUTE_ALIGN(32);
static u8 sReadCache[2][SHADOW_CACHE_SIZE] ATTRIBUTE_ALIGN(32);
static unsigned short sCisoMap[SUSAMUNE_CISO_MAP_COUNT];

static const AssetSpec sAssets[] = {
	{
		"Shadow", "data/scene/mare6.szs", "mare6.szs", "kagemario",
		"default.bmd", "kagemario_scroll.btk",
		(volatile u8*)SUSAMUNE_GHOST_SHADOW_STAGING_PPC_PTR,
		(volatile u8*)SUSAMUNE_GHOST_SHADOW_STAGING_PPC_PTR +
			SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE,
		SUSAMUNE_GHOST_SHADOW_ASSET_MAGIC,
		SUSAMUNE_GHOST_SHADOW_ASSET_BUFFER_SIZE,
		SUSAMUNE_GHOST_SHADOW_ASSET_SIZE,
		SUSAMUNE_GHOST_SHADOW_BMD_OFFSET,
		SUSAMUNE_GHOST_SHADOW_BMD_SIZE,
		SUSAMUNE_GHOST_SHADOW_BTK_OFFSET,
		SUSAMUNE_GHOST_SHADOW_BTK_SIZE,
		SUSAMUNE_GHOST_SHADOW_PAYLOAD_CRC32,
		SUSAMUNE_GHOST_SHADOW_BMD_CRC32,
		SUSAMUNE_GHOST_SHADOW_BTK_CRC32
	},
	{
		"Piantissimo", "data/scene/mare4.szs", "mare4.szs", "pad",
		"monteman_model.bmd", NULL,
		(volatile u8*)SUSAMUNE_GHOST_PIANTA_STAGING_PPC_PTR,
		(volatile u8*)SUSAMUNE_GHOST_PIANTA_STAGING_PPC_PTR +
			SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE,
		SUSAMUNE_GHOST_PIANTA_ASSET_MAGIC,
		SUSAMUNE_GHOST_PIANTA_ASSET_BUFFER_SIZE,
		SUSAMUNE_GHOST_PIANTA_ASSET_SIZE,
		SUSAMUNE_GHOST_PIANTA_BMD_OFFSET,
		SUSAMUNE_GHOST_PIANTA_BMD_SIZE,
		SUSAMUNE_GHOST_PIANTA_ASSET_SIZE, 0,
		SUSAMUNE_GHOST_PIANTA_PAYLOAD_CRC32,
		SUSAMUNE_GHOST_PIANTA_BMD_CRC32, 0
	}
};

static u16 ReadBE16(const u8 *p)
{
	return (u16)(((u16)p[0] << 8) | p[1]);
}

static u32 ReadBE32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8) | (u32)p[3];
}

static bool RangeFits(u32 offset, u32 size, u32 limit)
{
	return (u64)offset + size <= limit;
}

static void PublishStatus(const AssetSpec *spec, s32 status)
{
	ShadowAssetHeader header;

	memset(&header, 0, SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE);
	header.magic = spec->magic;
	header.version = SUSAMUNE_GHOST_MODEL_ASSET_VERSION;
	header.headerSize = SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE;
	header.status = status;
	memcpy((void *)spec->base, &header, SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE);
	DCFlushRange((void *)spec->base, SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE);
}

static void PublishReady(const AssetSpec *spec)
{
	ShadowAssetHeader header;

	memset(&header, 0, SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE);
	header.magic = spec->magic;
	header.version = SUSAMUNE_GHOST_MODEL_ASSET_VERSION;
	header.headerSize = SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE;
	header.status = SUSAMUNE_GHOST_MODEL_STATUS_READY;
	header.totalSize = spec->totalSize;
	header.bmdOffset = spec->bmdOffset;
	header.bmdSize = spec->bmdSize;
	header.payloadChecksum = spec->payloadChecksum;
	memcpy((void *)spec->base, &header, SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE);
	DCFlushRange((void *)spec->base, SUSAMUNE_GHOST_MODEL_ASSET_HEADER_SIZE);
}

static bool ReaderRawRead(ShadowReader *reader, u64 offset, void *data, u32 size)
{
	if (size == 0)
		return true;
	if (reader->size != 0 && (offset > reader->size || size > reader->size - offset))
		return false;

	if (reader->kind == SHADOW_SOURCE_FILE)
	{
		UINT got = 0;
		if (f_lseek(&reader->file, (FSIZE_t)offset) != FR_OK ||
		    f_read(&reader->file, data, size, &got) != FR_OK || got != size)
			return false;
		return true;
	}
	else
	{
		const u64 alignedOffset = offset & ~(u64)31u;
		const u32 difference = (u32)(offset - alignedOffset);
		const u32 alignedSize = (difference + size + 31u) & ~31u;
		if (alignedSize > sizeof(sDiscReadScratch))
			return false;
		memset(sDiscReadScratch, 0, alignedSize);
		ReadRealDisc(sDiscReadScratch, alignedOffset, alignedSize,
			reader->discCommand);
		memcpy(data, sDiscReadScratch + difference, size);
		return true;
	}
}

static bool ReaderLogicalRead(ShadowReader *reader, u64 offset, void *data,
	u32 size)
{
	u8 *out = (u8*)data;
	if (!reader->ciso)
		return ReaderRawRead(reader, offset, data, size);
	while (size != 0)
	{
		unsigned long long physical;
		int empty;
		u32 amount = SusamuneCisoSpan(sCisoMap, offset, size,
			reader->size, &physical, &empty);
		if (amount == 0)
			return false;
		if (empty)
			memset(out, 0, amount);
		else if (!ReaderRawRead(reader, physical, out, amount))
			return false;
		out += amount;
		offset += amount;
		size -= amount;
	}
	return true;
}

static bool ReaderRead(ShadowReader *reader, u64 offset, void *data, u32 size)
{
	u32 i;
	u64 start;
	u32 amount;
	u64 limit = reader->ciso ?
		(u64)SUSAMUNE_CISO_MAP_COUNT * SUSAMUNE_CISO_BLOCK_SIZE : reader->size;
	if (size == 0)
		return true;
	if (size > 128u)
		return ReaderLogicalRead(reader, offset, data, size);
	for (i = 0; i < 2; ++i)
		if (offset >= reader->cacheStart[i] &&
		    offset - reader->cacheStart[i] <= reader->cacheSize[i] &&
		    size <= reader->cacheSize[i] - (u32)(offset - reader->cacheStart[i]))
		{
			memcpy(data, sReadCache[i] + (u32)(offset - reader->cacheStart[i]), size);
			return true;
		}
	start = offset & ~(u64)(SHADOW_CACHE_SIZE - 1u);
	if (offset - start + size > SHADOW_CACHE_SIZE)
		return ReaderLogicalRead(reader, offset, data, size);
	amount = SHADOW_CACHE_SIZE;
	if (limit != 0)
	{
		if (start >= limit)
			return false;
		if (limit - start < amount)
			amount = (u32)(limit - start);
	}
	if (offset - start + size > amount)
		return false;
	i = reader->nextCache++ & 1u;
	reader->cacheSize[i] = 0;
	if (!ReaderLogicalRead(reader, start, sReadCache[i], amount))
		return false;
	reader->cacheStart[i] = start;
	reader->cacheSize[i] = amount;
	memcpy(data, sReadCache[i] + (u32)(offset - start), size);
	return true;
}

static bool ReadFstEntry(ShadowReader *reader, u64 fstBase, u32 index,
	FstEntry *entry)
{
	u8 raw[12];
	if (!ReaderRead(reader, fstBase + (u64)index * sizeof(raw), raw, sizeof(raw)))
		return false;
	entry->nameAndType = ReadBE32(raw);
	entry->offset = ReadBE32(raw + 4);
	entry->size = ReadBE32(raw + 8);
	return true;
}

static bool ReadFstName(ShadowReader *reader, u64 strings, u32 stringSize,
	u32 nameOffset, char *name)
{
	u32 available;
	u32 length;

	if (nameOffset >= stringSize)
		return false;
	available = stringSize - nameOffset;
	if (available > SHADOW_NAME_MAX)
		available = SHADOW_NAME_MAX;
	if (!ReaderRead(reader, strings + nameOffset, name, available))
		return false;
	for (length = 0; length < available; length++)
	{
		if (name[length] == '\0')
			return true;
	}
	return false;
}

static int FindArchiveInDisc(ShadowReader *reader, u64 gameBase,
	const AssetSpec *spec,
	u64 *archiveOffset, u32 *archiveSize)
{
	const char *const path[] = {"data", "scene", spec->archiveLeaf};
	u8 header[12];
	u8 magic[4];
	u32 fstOffset;
	u32 fstSize;
	u64 fstBase;
	u64 strings;
	u32 entryCount;
	u32 stringSize;
	u32 rangeStart = 1;
	u32 rangeEnd;
	u32 component;
	FstEntry root;

	if (!ReaderRead(reader, gameBase + 0x1c, magic, sizeof(magic)))
		return SUSAMUNE_GHOST_SHADOW_STATUS_READ_FAILED;
	if (ReadBE32(magic) != 0xC2339F3Du)
		return SUSAMUNE_GHOST_SHADOW_STATUS_SOURCE_UNSUPPORTED;
	if (!ReaderRead(reader, gameBase + 0x424, header, 8))
		return SUSAMUNE_GHOST_SHADOW_STATUS_READ_FAILED;
	fstOffset = ReadBE32(header);
	fstSize = ReadBE32(header + 4);
	if (fstSize < 12 || fstSize > SHADOW_FST_MAX_SIZE ||
	    fstOffset > SHADOW_GAME_MAX_SIZE ||
	    fstSize > SHADOW_GAME_MAX_SIZE - fstOffset)
		return SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING;
	fstBase = gameBase + fstOffset;
	if (!ReadFstEntry(reader, fstBase, 0, &root))
		return SUSAMUNE_GHOST_SHADOW_STATUS_READ_FAILED;
	if ((root.nameAndType & 0x01000000u) == 0 || root.size == 0 ||
	    (u64)root.size * 12u > fstSize)
		return SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING;
	entryCount = root.size;
	rangeEnd = entryCount;
	strings = fstBase + (u64)entryCount * 12u;
	stringSize = fstSize - entryCount * 12u;

	for (component = 0; component < 3; component++)
	{
		u32 index = rangeStart;
		bool found = false;
		while (index < rangeEnd)
		{
			FstEntry entry;
			char name[SHADOW_NAME_MAX];
			const bool last = component == 2;
			bool directory;

			if (!ReadFstEntry(reader, fstBase, index, &entry) ||
			    !ReadFstName(reader, strings, stringSize,
				entry.nameAndType & 0x00ffffffu, name))
				return SUSAMUNE_GHOST_SHADOW_STATUS_READ_FAILED;
			directory = (entry.nameAndType & 0x01000000u) != 0;
			if (strcmp(name, path[component]) == 0)
			{
				if (last)
				{
					if (directory || entry.size < 16 ||
					    entry.size > SHADOW_COMPRESSED_MAX_SIZE ||
					    entry.offset > SHADOW_GAME_MAX_SIZE ||
					    entry.size > SHADOW_GAME_MAX_SIZE - entry.offset)
						return SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING;
					*archiveOffset = gameBase + entry.offset;
					*archiveSize = entry.size;
					return 0;
				}
				if (!directory || entry.size <= index + 1 || entry.size > rangeEnd)
					return SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING;
				rangeStart = index + 1;
				rangeEnd = entry.size;
				found = true;
				break;
			}
			if (directory)
			{
				if (entry.size <= index || entry.size > rangeEnd)
					return SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING;
				index = entry.size;
			}
			else
			{
				index++;
			}
		}
		if (!found)
			return SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING;
	}
	return SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING;
}

static bool InputByte(ShadowInput *input, u8 *value)
{
	if (input->bufferPosition == input->buffered)
	{
		u32 remaining;
		u32 amount;
		if (input->position >= input->size)
			return false;
		remaining = input->size - input->position;
		amount = remaining < SHADOW_INPUT_SIZE ? remaining : SHADOW_INPUT_SIZE;
		if (!ReaderRead(input->reader, input->start + input->position,
			input->buffer, amount))
			return false;
		input->position += amount;
		input->buffered = amount;
		input->bufferPosition = 0;
	}
	*value = input->buffer[input->bufferPosition++];
	return true;
}

static bool RarcStringEquals(const ShadowDecoder *decoder, u32 strings,
	u32 stringSize, u32 offset, const char *expected)
{
	const u32 length = strlen(expected);
	if (offset >= stringSize || length >= stringSize - offset)
		return false;
	return memcmp(decoder->metadata + strings + offset, expected, length) == 0 &&
	       decoder->metadata[strings + offset + length] == '\0';
}

// Parse metadata once without adding its register saves to every decoded byte.
static bool __attribute__((noinline)) ParseRarc(ShadowDecoder *decoder)
{
	const AssetSpec *spec = decoder->spec;
	const u8 *meta = decoder->metadata;
	u32 headerSize;
	u32 archiveSize;
	u32 dataLength;
	u32 nodeCount;
	u32 nodeTable;
	u32 fileCount;
	u32 fileTable;
	u32 stringSize;
	u32 strings;
	u32 kageNode = 0xffffffffu;
	u32 nodeIndex;
	u32 bmdOffset = 0xffffffffu;
	u32 btkOffset = 0xffffffffu;
	u32 firstEntry;
	u32 entryCount;
	u32 entryIndex;

	if (decoder->dataBase > SHADOW_RARC_META_MAX || decoder->dataBase < 0x40 ||
	    memcmp(meta, "RARC", 4) != 0)
		return false;
	archiveSize = ReadBE32(meta + 4);
	headerSize = ReadBE32(meta + 8);
	dataLength = ReadBE32(meta + 0x10);
	if (archiveSize != decoder->outputSize || headerSize != 0x20 ||
	    !RangeFits(decoder->dataBase, dataLength, archiveSize))
		return false;

	nodeCount = ReadBE32(meta + 0x20);
	nodeTable = headerSize + ReadBE32(meta + 0x24);
	fileCount = ReadBE32(meta + 0x28);
	fileTable = headerSize + ReadBE32(meta + 0x2c);
	stringSize = ReadBE32(meta + 0x30);
	strings = headerSize + ReadBE32(meta + 0x34);
	if (nodeCount == 0 || nodeCount > 4096 || fileCount == 0 || fileCount > 65535 ||
	    !RangeFits(nodeTable, nodeCount * 0x10u, decoder->dataBase) ||
	    !RangeFits(fileTable, fileCount * 0x14u, decoder->dataBase) ||
	    !RangeFits(strings, stringSize, decoder->dataBase))
		return false;

	for (nodeIndex = 0; nodeIndex < nodeCount; nodeIndex++)
	{
		const u8 *node = meta + nodeTable + nodeIndex * 0x10u;
		const u32 nameOffset = ReadBE32(node + 4);
		if (RarcStringEquals(decoder, strings, stringSize, nameOffset,
			spec->nodeName))
		{
			if (kageNode != 0xffffffffu)
				return false;
			kageNode = nodeIndex;
		}
	}
	if (kageNode == 0xffffffffu)
		return false;

	firstEntry = ReadBE32(meta + nodeTable + kageNode * 0x10u + 0x0c);
	entryCount = ReadBE16(meta + nodeTable + kageNode * 0x10u + 0x0a);
	if (firstEntry > fileCount || entryCount > fileCount - firstEntry)
		return false;
	for (entryIndex = firstEntry; entryIndex < firstEntry + entryCount; entryIndex++)
	{
		const u8 *entry = meta + fileTable + entryIndex * 0x14u;
		const u32 flagsAndName = ReadBE32(entry + 4);
		const u8 flags = (u8)(flagsAndName >> 24);
		const u32 nameOffset = flagsAndName & 0x00ffffffu;
		const u32 dataOffset = ReadBE32(entry + 8);
		const u32 dataSize = ReadBE32(entry + 0x0c);

		if ((flags & 0x02) != 0)
			continue;
		if (dataOffset > decoder->outputSize - decoder->dataBase ||
		    dataSize > decoder->outputSize - decoder->dataBase - dataOffset)
			return false;
		if (RarcStringEquals(decoder, strings, stringSize, nameOffset,
			spec->bmdName))
		{
			if (bmdOffset != 0xffffffffu || dataSize != spec->bmdSize)
				return false;
			bmdOffset = decoder->dataBase + dataOffset;
		}
		else if (spec->btkName != NULL &&
			RarcStringEquals(decoder, strings, stringSize, nameOffset,
				spec->btkName))
		{
			if (btkOffset != 0xffffffffu || dataSize != spec->btkSize)
				return false;
			btkOffset = decoder->dataBase + dataOffset;
		}
	}
	if (bmdOffset == 0xffffffffu ||
		(spec->btkSize != 0 && btkOffset == 0xffffffffu))
		return false;

	decoder->bmdSource = bmdOffset;
	decoder->btkSource = btkOffset;
	decoder->rarcReady = true;
	return true;
}

static inline bool __attribute__((always_inline)) EmitByte(ShadowDecoder *decoder, u8 value)
{
	const AssetSpec *spec = decoder->spec;
	const u32 position = decoder->outputPosition;

	if (position >= decoder->outputSize)
		return false;
	if (position < SHADOW_RARC_META_MAX)
		decoder->metadata[position] = value;
	decoder->history[position & (SHADOW_HISTORY_SIZE - 1u)] = value;

	if (decoder->rarcReady)
	{
		if (position >= decoder->bmdSource &&
		    position - decoder->bmdSource < spec->bmdSize)
		{
			spec->payload[position - decoder->bmdSource] = value;
			decoder->bmdCaptured++;
		}
		else if (position >= decoder->btkSource &&
		         position - decoder->btkSource < spec->btkSize)
		{
			spec->payload[spec->bmdSize +
				position - decoder->btkSource] = value;
			decoder->btkCaptured++;
		}
	}

	decoder->outputPosition++;
	if (decoder->outputPosition == 0x20)
	{
		u32 relativeDataBase;
		if (memcmp(decoder->metadata, "RARC", 4) != 0 ||
		    ReadBE32(decoder->metadata + 8) != 0x20)
			return false;
		relativeDataBase = ReadBE32(decoder->metadata + 0x0c);
		if (relativeDataBase > SHADOW_RARC_META_MAX - 0x20u)
			return false;
		decoder->dataBase = 0x20u + relativeDataBase;
		if (decoder->dataBase < 0x40)
			return false;
	}
	if (!decoder->rarcReady && decoder->dataBase != 0 &&
	    decoder->outputPosition == decoder->dataBase && !ParseRarc(decoder))
		return false;
	return true;
}

static bool DecodeArchive(ShadowReader *reader, u64 offset, u32 size,
	const AssetSpec *spec, ShadowDecoder *decoder, int *failureStatus)
{
	u8 header[16];
	u8 code = 0;
	u8 mask = 0;

	memset(decoder, 0, sizeof(*decoder));
	decoder->spec = spec;
	decoder->input.reader = reader;
	decoder->input.start = offset;
	decoder->input.size = size;
	if (!ReaderRead(reader, offset, header, sizeof(header)))
	{
		*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_READ_FAILED;
		return false;
	}
	if (memcmp(header, "Yaz0", 4) != 0)
	{
		*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
		return false;
	}
	if (ReadBE32(header + 8) != 0 || ReadBE32(header + 12) != 0)
	{
		*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
		return false;
	}
	decoder->outputSize = ReadBE32(header + 4);
	if (decoder->outputSize < SHADOW_DECODED_MIN_SIZE ||
	    decoder->outputSize > SHADOW_DECODED_MAX_SIZE)
	{
		*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
		return false;
	}
	decoder->input.position = sizeof(header);

	while (decoder->outputPosition < decoder->outputSize)
	{
		if (mask == 0)
		{
			if (!InputByte(&decoder->input, &code))
			{
				*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
				return false;
			}
			mask = 0x80;
		}

		if ((code & mask) != 0)
		{
			u8 literal;
			if (!InputByte(&decoder->input, &literal) || !EmitByte(decoder, literal))
			{
				*failureStatus = decoder->dataBase != 0 ?
					SUSAMUNE_GHOST_SHADOW_STATUS_BAD_RARC :
					SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
				return false;
			}
		}
		else
		{
			u8 first;
			u8 second;
			u32 distance;
			u32 length;
			u32 i;
			if (!InputByte(&decoder->input, &first) ||
			    !InputByte(&decoder->input, &second))
			{
				*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
				return false;
			}
			distance = (((u32)first & 0x0fu) << 8) | second;
			distance++;
			length = first >> 4;
			if (length == 0)
			{
				u8 extra;
				if (!InputByte(&decoder->input, &extra))
				{
					*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
					return false;
				}
				length = (u32)extra + 0x12u;
			}
			else
			{
				length += 2u;
			}
			if (distance > decoder->outputPosition ||
			    length > decoder->outputSize - decoder->outputPosition)
			{
				*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
				return false;
			}
			for (i = 0; i < length; i++)
			{
				const u8 value = decoder->history[
					(decoder->outputPosition - distance) &
					(SHADOW_HISTORY_SIZE - 1u)];
				if (!EmitByte(decoder, value))
				{
					*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_RARC;
					return false;
				}
			}
		}
		mask >>= 1;
	}

	if (decoder->input.position - decoder->input.buffered +
	        decoder->input.bufferPosition != size)
	{
		*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
		return false;
	}
	if (!decoder->rarcReady || decoder->bmdCaptured != spec->bmdSize ||
	    decoder->btkCaptured != spec->btkSize)
	{
		*failureStatus = SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING;
		return false;
	}
	return true;
}

static bool BuildExtractedPath(char *out, size_t outSize, const char *device,
	const char *gamePath, const AssetSpec *spec)
{
	const size_t pathLength = strlen(gamePath);
	const int written = snprintf(out, outSize, "%s:%s%s%s", device, gamePath,
		(pathLength > 0 && gamePath[pathLength - 1] == '/') ? "" : "/",
		spec->archivePath);
	return written > 0 && (size_t)written < outSize;
}

static bool OpenGameFile(ShadowReader *reader, const char *device,
	const char *gamePath)
{
	char path[512];
	const int written = snprintf(path, sizeof(path), "%s:%s", device, gamePath);
	if (written <= 0 || (size_t)written >= sizeof(path) ||
	    f_open_char(&reader->file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return false;
	reader->kind = SHADOW_SOURCE_FILE;
	reader->fileOpen = true;
	reader->size = reader->file.obj.objsize;
	return true;
}

static bool OpenExtractedArchive(ShadowReader *reader, const char *device,
	const char *gamePath, const AssetSpec *spec)
{
	char path[512];
	if (!BuildExtractedPath(path, sizeof(path), device, gamePath, spec) ||
	    f_open_char(&reader->file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return false;
	reader->kind = SHADOW_SOURCE_FILE;
	reader->fileOpen = true;
	reader->size = reader->file.obj.objsize;
	return true;
}

static int ValidatePayload(const AssetSpec *spec)
{
	int status = 0;
	uLong checksum;
	uLong bmdChecksum;
	uLong btkChecksum;
	if (memcmp((const void *)spec->payload, "J3D2bmd3", 8) != 0 ||
	    ReadBE32((const u8 *)spec->payload + 8) != spec->bmdSize ||
	    (spec->btkSize != 0 &&
	     (memcmp((const void *)(spec->payload + spec->bmdSize),
		"J3D1btk1", 8) != 0 ||
	      ReadBE32((const u8 *)spec->payload + spec->bmdSize + 8) !=
		  spec->btkSize)))
	{
		status = SUSAMUNE_GHOST_SHADOW_STATUS_RESOURCE_MISSING;
		goto done;
	}
	bmdChecksum = crc32(0L, Z_NULL, 0);
	bmdChecksum = crc32(bmdChecksum, (const Bytef *)spec->payload,
		spec->bmdSize);
	checksum = bmdChecksum;
	btkChecksum = crc32(0L, Z_NULL, 0);
	if (spec->btkSize != 0)
	{
		checksum = crc32(checksum,
			(const Bytef *)spec->payload + spec->bmdSize,
			spec->btkSize);
		btkChecksum = crc32(btkChecksum,
			(const Bytef *)spec->payload + spec->bmdSize,
			spec->btkSize);
	}
	if ((u32)checksum != spec->payloadChecksum ||
	    (u32)bmdChecksum != spec->bmdChecksum ||
	    (u32)btkChecksum != spec->btkChecksum)
		status = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_CHECKSUM;
done:
	return status;
}

// Disposable loader-only cache. The exact disc ID/revision and compiled asset
// identity are checked before any cached bytes can become a staged model.
typedef struct AssetCacheHeader {
	u32 magic, version, game[2], assetMagic, bmdSize, btkSize, payloadChecksum;
} AssetCacheHeader;

static bool BuildAssetCachePath(char *path, u32 capacity, const char *device,
	const u8 *discHeader, const AssetSpec *spec, AssetCacheHeader *header)
{
	int length;
	if (memcmp(discHeader, "GMS", 3) ||
	    (discHeader[3] != 'J' && discHeader[3] != 'E' && discHeader[3] != 'P') ||
	    memcmp(discHeader + 4, "01", 2) ||
	    ReadBE32(discHeader + 0x1c) != 0xC2339F3Du)
		return false;
	*header = (AssetCacheHeader){0x4D474143u, 1,
		{ReadBE32(discHeader), ReadBE32(discHeader + 4)}, spec->magic,
		spec->bmdSize, spec->btkSize, spec->payloadChecksum};
	length = snprintf(path, capacity, "%s:" MOONSHINE_DATA_ROOT
		"/cache/ghost_%08lx_%08lx_%08lx.bin", device,
		(unsigned long)header->game[0], (unsigned long)header->game[1],
		(unsigned long)header->payloadChecksum);
	if (length > 0 && (u32)length < capacity) return true;
	if (capacity) path[0] = 0;
	return false;
}

static bool ReadAssetCache(const char *path, const AssetCacheHeader *expected,
	const AssetSpec *spec, bool load)
{
	FIL file;
	AssetCacheHeader header;
	UINT got = 0;
	u32 offset = 0, size = spec->bmdSize + spec->btkSize;
	bool valid = false;
	if (f_open_char(&file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
		return false;
	if (file.obj.objsize != sizeof(header) + size ||
	    f_read(&file, &header, sizeof(header), &got) != FR_OK ||
	    got != sizeof(header) || memcmp(&header, expected, sizeof(header)))
		goto done;
	while (offset < size)
	{
		const u32 count = size - offset < SHADOW_INPUT_SIZE ?
			size - offset : SHADOW_INPUT_SIZE;
		void *destination = load ? (void *)(spec->payload + offset) : sDiscReadScratch;
		if (f_read(&file, destination, count, &got) != FR_OK || got != count ||
		    (!load && memcmp(destination, (const void *)(spec->payload + offset), count)))
			goto done;
		offset += count;
	}
	valid = !load || ValidatePayload(spec) == 0;
done:
	if (f_close(&file) != FR_OK) valid = false;
	return valid;
}

static void WriteAssetCache(const char *path, const char *device,
	const AssetCacheHeader *header, const AssetSpec *spec)
{
	FIL file;
	FILINFO info;
	char temporary[192], directory[80];
	UINT wrote = 0;
	FRESULT result;
	bool valid;
	const u32 size = spec->bmdSize + spec->btkSize;
	int length = snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	if (length <= 0 || (u32)length >= sizeof(temporary)) return;
	length = snprintf(directory, sizeof(directory), "%s:" MOONSHINE_DATA_ROOT "/cache", device);
	if (length <= 0 || (u32)length >= sizeof(directory)) return;
	result = f_mkdir_char(directory);
	if (result != FR_OK && result != FR_EXIST) return;
	if (f_open_char(&file, temporary, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return;
	valid = f_write(&file, header, sizeof(*header), &wrote) == FR_OK &&
		wrote == sizeof(*header) &&
		f_write(&file, (const void *)spec->payload, size, &wrote) == FR_OK &&
		wrote == size && f_sync(&file) == FR_OK;
	if (f_close(&file) != FR_OK) valid = false;
	// Verify the closed temporary against the already validated extraction.
	// Reading it into scratch preserves that good extraction even on failure.
	if (!valid || !ReadAssetCache(temporary, header, spec, false)) goto done;
	result = f_stat_char(path, &info);
	if (result == FR_OK)
	{
		if ((info.fattrib & AM_DIR) || f_unlink_char(path) != FR_OK) goto done;
	}
	else if (result != FR_NO_FILE && result != FR_NO_PATH) goto done;
	if (f_rename_char(temporary, path) == FR_OK) return;
done:
	(void)f_unlink_char(temporary);
}

static void StageAsset(const AssetSpec *spec, const char *gameDevice,
	const char *gamePath, u32 discCommand, u32 isoShift, bool wiiVcInternal)
{
	ShadowReader reader;
	ShadowDecoder *decoder = NULL;
	u64 archiveOffset = 0;
	u32 archiveSize = 0;
	int status = SUSAMUNE_GHOST_SHADOW_STATUS_SOURCE_UNSUPPORTED;
	char cachePath[160] = {0};
	AssetCacheHeader cacheHeader;
	bool discSource = false;
	// ApplyToNinCFG retains the already-mounted launcher/data volume even
	// when GetRootDevice has switched to a game image on the other device.
	const char *cacheDevice = (ncfg->Config & NIN_CFG_CFG_ON_USB) ? "usb" : "sd";

	PublishStatus(spec, status);
	memset(&reader, 0, sizeof(reader));
	if (wiiVcInternal || gameDevice == NULL || gamePath == NULL)
		goto done;

	if (discCommand != 0)
	{
		reader.kind = SHADOW_SOURCE_REAL_DISC;
		reader.discCommand = discCommand;
		discSource = true;
	}
	else if (IsSupportedFileExt(gamePath))
	{
		u8 magic[8];
		if (!OpenGameFile(&reader, gameDevice, gamePath))
		{
			status = SUSAMUNE_GHOST_SHADOW_STATUS_OPEN_FAILED;
			goto done;
		}
		if (!ReaderRead(&reader, 0, magic, sizeof(magic)))
		{
			status = SUSAMUNE_GHOST_SHADOW_STATUS_READ_FAILED;
			goto done;
		}
		if (memcmp(magic, "CISO", 4) == 0)
		{
			u8 header[8u + SUSAMUNE_CISO_MAP_COUNT];
			if (!ReaderRawRead(&reader, 0, header, sizeof(header)) ||
			    !SusamuneCisoMapInit(sCisoMap, header, sizeof(header), reader.size))
			{
				status = SUSAMUNE_GHOST_SHADOW_STATUS_SOURCE_UNSUPPORTED;
				goto done;
			}
			reader.ciso = true;
			reader.cacheSize[0] = reader.cacheSize[1] = 0;
		}
		discSource = true;
	}
	else
	{
		if (!OpenExtractedArchive(&reader, gameDevice, gamePath, spec))
		{
			status = SUSAMUNE_GHOST_SHADOW_STATUS_OPEN_FAILED;
			goto done;
		}
		if (reader.size == 0 || reader.size > SHADOW_COMPRESSED_MAX_SIZE)
		{
			status = SUSAMUNE_GHOST_SHADOW_STATUS_BAD_YAZ0;
			goto done;
		}
		archiveSize = (u32)reader.size;
		status = 0;
	}
	if (discSource)
	{
		u8 identity[32];
		if (ReaderRead(&reader, (u64)isoShift << 2, identity, sizeof(identity)) &&
		    BuildAssetCachePath(cachePath, sizeof(cachePath), cacheDevice, identity,
			spec, &cacheHeader) && ReadAssetCache(cachePath, &cacheHeader, spec, true))
		{
			gprintf("Moonshine %s model cache hit\n", spec->label);
			goto ready;
		}
		status = FindArchiveInDisc(&reader, (u64)isoShift << 2, spec,
			&archiveOffset, &archiveSize);
	}
	if (status != 0)
		goto done;

	decoder = memalign(32, sizeof(*decoder));
	if (decoder == NULL)
	{
		status = SUSAMUNE_GHOST_SHADOW_STATUS_READ_FAILED;
		goto done;
	}
	{
		const struct mallinfo info = mallinfo();
		gprintf("Susamune %s decode heap: live=%u free=%u transient=%u\n",
			spec->label,
			(unsigned int)info.uordblks, (unsigned int)info.fordblks,
			(unsigned int)sizeof(*decoder));
	}
	if (!DecodeArchive(&reader, archiveOffset, archiveSize, spec, decoder,
		&status))
		goto done;

	status = ValidatePayload(spec);
	if (status != 0) goto done;
	if (cachePath[0]) WriteAssetCache(cachePath, cacheDevice, &cacheHeader, spec);

ready:
	DCFlushRange((void *)spec->payload, spec->bmdSize + spec->btkSize);
	PublishReady(spec);
	status = SUSAMUNE_GHOST_SHADOW_STATUS_READY;

done:
	if (reader.fileOpen)
		f_close(&reader.file);
	free(decoder);
	if (status != SUSAMUNE_GHOST_SHADOW_STATUS_READY)
		PublishStatus(spec, status);
	gprintf("Susamune %s asset staging status %d\n", spec->label, status);
}

void SusamuneStageShadowAsset(const char *gameDevice, const char *gamePath,
	u32 discCommand, u32 isoShift, bool wiiVcInternal)
{
	u32 i;
	for (i = 0; i < sizeof(sAssets) / sizeof(sAssets[0]); ++i)
		StageAsset(&sAssets[i], gameDevice, gamePath, discCommand, isoShift,
			wiiVcInternal);
}
