/*

Nintendont (Loader) - Playing Gamecubes in Wii mode on a Wii U

Copyright (C) 2015-2016  FIX94

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation version 2.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

*/
#include <gccore.h>
#include "exi.h"
#include "dip.h"
#include "global.h"
#include "TRI.h"
#include "ff_utf8.h"
#include "wdvd.h"

static const char CARD_NAME_GP1[] = "/apps/gc_devo/GP1.bin";
static const char CARD_NAME_GP2[] = "/apps/gc_devo/GP2.bin";
static const char CARD_NAME_GP2J[] = "/apps/gc_devo/GP2J.bin";
static const char CARD_NAME_AX[] = "/apps/gc_devo/AX.bin";

static const char SETTINGS_AX_RVC[] = "/apps/gc_devo/AX_RVCsettings.bin";
static const char SETTINGS_AX_RVD[] = "/apps/gc_devo/AX_RVDsettings.bin";
static const char SETTINGS_AX_RVE[] = "/apps/gc_devo/AX_RVEsettings.bin";
static const char SETTINGS_YAKRVB[] = "/apps/gc_devo/YAKRVBsettings.bin";
static const char SETTINGS_YAKRVC[] = "/apps/gc_devo/YAKRVCsettings.bin";
static const char SETTINGS_VS3V02[] = "/apps/gc_devo/VS3V02settings.bin";
static const char SETTINGS_VS4JAP[] = "/apps/gc_devo/VS4JPNsettings.bin";
static const char SETTINGS_VS4EXP[] = "/apps/gc_devo/VS4EXPsettings.bin";
static const char SETTINGS_VS4V06JAP[] = "/apps/gc_devo/VS4V06JPNsettings.bin";
static const char SETTINGS_VS4V06EXP[] = "/apps/gc_devo/VS4V06EXPsettings.bin";

typedef struct {
	u32 dolOffset;
	const char *name;
	const char *savePath;
	u16 saveSize;
	bool needsAxCard;
} TriforceGame;

static const TriforceGame TriforceGames[] = {
	{0x210320, "Mario Kart Arcade GP (ENG Feb 14 2006 13:09:48)",
		CARD_NAME_GP1, 0x45, false},
	{0x25C0AC, "Mario Kart Arcade GP 2 (ENG Feb 7 2007 02:47:24)",
		CARD_NAME_GP2, 0x45, false},
	{0x25C664, "Mario Kart Arcade GP 2 (JPN Feb 6 2007 20:29:25)",
		CARD_NAME_GP2J, 0x45, false},
	{0x181E60, "F-Zero AX (Rev C)", SETTINGS_AX_RVC, 0x2A, true},
	{0x1821C4, "F-Zero AX (Rev D)", SETTINGS_AX_RVD, 0x2A, true},
	{0x18275C, "F-Zero AX (Rev E)", SETTINGS_AX_RVE, 0x2A, true},
	{0x01C2DF4, "Virtua Striker 3 Ver 2002", SETTINGS_VS3V02, 0x12, false},
	{0x01CF1C4, "Virtua Striker 4 (Japan)", SETTINGS_VS4JAP, 0x2B, false},
	{0x1C51E4, "Virtua Striker 4 (Export) (GDT-0014)",
		SETTINGS_VS4EXP, 0x2B, false},
	{0x1C5514, "Virtua Striker 4 (Export) (GDT-0015)",
		SETTINGS_VS4EXP, 0x2B, false},
	{0x24A4C8, "Virtua Striker 4 Ver 2006 (Japan) (Rev B)",
		SETTINGS_VS4V06JAP, 0x2E, false},
	{0x24B248, "Virtua Striker 4 Ver 2006 (Japan) (Rev D)",
		SETTINGS_VS4V06JAP, 0x2E, false},
	{0x20D7E8, "Virtua Striker 4 Ver 2006 (Export)",
		SETTINGS_VS4V06EXP, 0x2B, false},
	{0x26B3F4, "Gekitou Pro Yakyuu (Rev B)", SETTINGS_YAKRVB, 0xF5, false},
	{0x26D9B4, "Gekitou Pro Yakyuu (Rev C)", SETTINGS_YAKRVC, 0x100, false},
};

extern bool wiiVCInternal;

static void CreateTriforceFile(const char *filePath, u32 size)
{
	char fullPath[64];
	int written = snprintf(fullPath, sizeof(fullPath), "%s:%s",
		GetRootDevice(), filePath);

	if ((unsigned int)written >= sizeof(fullPath))
		return;
	CreateNewFile(fullPath, size);
}

static u32 DOLRead32(u32 loc, u32 DOLOffset, FIL *f, u32 CurDICMD)
{
	u32 BufAtOffset = 0;
	if(wiiVCInternal)
	{
		WDVD_FST_LSeek(DOLOffset+loc);
		if (WDVD_FST_Read(wdvdTmpBuf, 4) != 4)
			return 0;
		memcpy(&BufAtOffset, wdvdTmpBuf, 4);
	}
	else if(f != NULL)
	{
		UINT read;
		if (f_lseek(f, DOLOffset+loc) != FR_OK ||
			f_read(f, &BufAtOffset, 4, &read) != FR_OK || read != 4)
			return 0;
	}
	else if(CurDICMD)
		ReadRealDisc((u8*)&BufAtOffset, DOLOffset+loc, 4, CurDICMD);
	return BufAtOffset;
}

u32 TRISetupGames(char *Path, u32 CurDICMD, u32 ISOShift)
{
	u32 res = 0;
	u32 DOLOffset = 0;
	FIL f;
	FIL *fp = NULL;
	UINT read;
	FRESULT fres = FR_DISK_ERR;
	u32 i;

	if(CurDICMD)
	{
		ReadRealDisc((u8*)&DOLOffset, 0x420+ISOShift, 4, CurDICMD);
		DOLOffset+=ISOShift;
	}
	else if(wiiVCInternal)
	{
		if (WDVD_FST_OpenDisc(0) != 0)
			return 0;
		WDVD_FST_LSeek(0x420+ISOShift);
		if (WDVD_FST_Read(wdvdTmpBuf, 4) != 4)
		{
			WDVD_FST_Close();
			return 0;
		}
		memcpy(&DOLOffset, wdvdTmpBuf, 4);
		DOLOffset+=ISOShift;
	}
	else
	{
		char FilePath[260];
		int written = snprintf(FilePath, sizeof(FilePath), "%s:%s",
			GetRootDevice(), Path);
		if ((unsigned int)written >= sizeof(FilePath))
			return 0;
		fres = f_open_char(&f, FilePath, FA_READ|FA_OPEN_EXISTING);
		if (fres == FR_OK)
		{
			if (f_lseek(&f, 0x420+ISOShift) != FR_OK ||
				f_read(&f, &DOLOffset, 4, &read) != FR_OK || read != 4)
			{
				f_close(&f);
				return 0;
			}
			DOLOffset+=ISOShift;
		}
		else
		{
			written = snprintf(FilePath, sizeof(FilePath),
				"%s:%ssys/main.dol", GetRootDevice(), Path);
			if ((unsigned int)written >= sizeof(FilePath))
				return 0;
			fres = f_open_char(&f, FilePath, FA_READ|FA_OPEN_EXISTING);
		}

		if (fres != FR_OK)
			return 0;
		fp = &f;
	}

	for (i = 0; i < sizeof(TriforceGames) / sizeof(TriforceGames[0]); ++i)
	{
		const TriforceGame *game = &TriforceGames[i];
		if (DOLRead32(game->dolOffset, DOLOffset, fp, CurDICMD) != 0x386000A8)
			continue;

		res = 1;
		gprintf("TRI:%s\r\n", game->name);
		if (game->needsAxCard)
			CreateTriforceFile(CARD_NAME_AX, 0xCF);
		CreateTriforceFile(game->savePath, game->saveSize);
		break;
	}

	if(wiiVCInternal)
		WDVD_FST_Close();
	else if (fres == FR_OK)
		f_close(&f);
	return res;
}
