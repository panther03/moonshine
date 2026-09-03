/*

Susamune mod loading (Nintendont loader side).

The mod is no longer compiled into the kernel: it ships as mod_jp.bin /
mod_us.bin / mod_pal.bin next to this loader's boot.dol, and one launcher
serves all three disc revisions. The loader is the right side to read it --
it already knows ncfg->GameID for every boot path (game list, di:, Wii VC)
and still has the FAT devices mounted, whereas the kernel only learns the
disc id after it has taken the SD card over from the DI thread.

We stage the file in the MEM2 window described by mod_bin.h; the kernel's
PatchSusamune() validates it against the running GAME_ID and copies the code
into MEM1. The header is always written, with a zeroed magic when there is no
file, because MEM2 survives across app launches and a stale blob from a
previous boot would otherwise be injected into an unrelated game.

*/

#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include "global.h"
#include "exi.h"
#include "ff_utf8.h"
#include "SusamuneMod.h"

#include "susamune/mod_bin.h"

extern char launch_dir[MAXPATHLEN];

static bool ValidModFile(const struct SusamuneModHeader *header, u32 gameID,
	u32 fileSize)
{
	u32 payloadSize;

	if (fileSize < SUSAMUNE_MOD_HEADER_SIZE ||
		header->magic != SUSAMUNE_MOD_MAGIC ||
		header->version != SUSAMUNE_MOD_VERSION ||
		header->gameId != gameID ||
		header->baseAddr != SUSAMUNE_MOD_BASE_FOR_GAME_ID(gameID) ||
		header->arenaReserve != SUSAMUNE_ARENA_RESERVE_SIZE ||
		header->codeSize > header->memSize ||
		header->memSize > SUSAMUNE_MOD_BLOB_MAX_SIZE ||
		(header->codeSize & 3) || (header->memSize & 3))
		return false;

	payloadSize = fileSize - SUSAMUNE_MOD_HEADER_SIZE;
	if (header->codeSize > payloadSize)
		return false;
	payloadSize -= header->codeSize;
	if (header->writeCount > payloadSize / 8)
		return false;
	return payloadSize == header->writeCount * 8;
}

void SusamuneLoadMod(u32 gameID)
{
	struct SusamuneModHeader *dst = SUSAMUNE_MOD_PPC_PTR;
	const char *region = SUSAMUNE_MOD_REGION_TAG(gameID);
	char path[MAXPATHLEN];
	FIL fd;
	UINT read = 0;
	FSIZE_t sizeOnDisk;
	int written;
	u32 size;

	/* Invalidate any blob left in MEM2 by a previous launch first, so every
	 * early return below leaves the kernel with "no mod". */
	memset(dst, 0, sizeof(*dst));
	DCFlushRange(dst, sizeof(*dst));

	if (region == NULL)
		return;  /* not one of ours */

	/* launch_dir is empty when loaded over the network; fall back to the
	 * conventional install path, same as titles.txt / meta.xml do. */
	written = snprintf(path, sizeof(path), "%s" SUSAMUNE_MOD_FILE_FMT,
		launch_dir[0] != 0 ? launch_dir : "/apps/moonshine_launcher/", region);
	if ((unsigned int)written >= sizeof(path))
		return;

	if (f_open_char(&fd, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		gprintf("Susamune: %s not found, running unmodified\r\n", path);
		return;
	}

	sizeOnDisk = fd.obj.objsize;
	if (sizeOnDisk < SUSAMUNE_MOD_HEADER_SIZE ||
		sizeOnDisk > SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE)
	{
		gprintf("Susamune: %s has a bad size (%llu)\r\n", path,
			(unsigned long long)sizeOnDisk);
		f_close(&fd);
		return;
	}
	size = (u32)sizeOnDisk;

	if (f_read(&fd, dst, size, &read) != FR_OK || read != size)
	{
		gprintf("Susamune: failed to read %s\r\n", path);
		memset(dst, 0, sizeof(*dst));
		read = sizeof(*dst);
	}
	else if (!ValidModFile(dst, gameID, size))
	{
		gprintf("Susamune: %s has an invalid manifest\r\n", path);
		memset(dst, 0, sizeof(*dst));
		read = sizeof(*dst);
	}
	f_close(&fd);

	DCFlushRange(dst, read);
	gprintf("Susamune: staged " SUSAMUNE_MOD_FILE_FMT " (%u bytes)\r\n",
		region, read);
}
