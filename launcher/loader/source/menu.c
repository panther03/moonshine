/*

Nintendont (Loader) - Playing Gamecubes in Wii mode on a Wii U

Copyright (C) 2013  crediar

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
#include <sys/param.h>
#include "font.h"
#include "exi.h"
#include "global.h"
#include "FPad.h"
#include "Config.h"
#include "update.h"
#include "titles.h"
#include "dip.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <ogc/stm.h>
#include <ogc/consol.h>
#include <ogc/system.h>
//#include <fat.h>
#include <di/di.h>

#include "menu.h"
#include "../../common/include/CommonConfigStrings.h"
#include "ff_utf8.h"
#include "ShowGameInfo.h"
#include "susamune_build_id.h"

// Dark gray for grayed-out menu items.
#define DARK_GRAY 0x666666FF


// Disc format colors.
const u32 DiscFormatColors[8] =
{
	BLACK,		// Full
	0x551A00FF,	// Shrunken (dark brown)
	0x00551AFF,	// Extracted FST
	0x001A55FF,	// CISO
	0x551A55FF,	// Multi-Game
	GRAY,		// undefined
	GRAY,		// undefined
	GRAY,		// undefined
};

extern NIN_CFG* ncfg;

u32 Shutdown = 0;
extern char *launch_dir;

void SetShutdown(void)
{
	Shutdown = 1;
}
void HandleWiiMoteEvent(s32 chan)
{
	SetShutdown();
}
void HandleSTMEvent(u32 event)
{
	*(vu32*)(0xCC003024) = 1;

	switch(event)
	{
		default:
		case STM_EVENT_RESET:
			break;
		case STM_EVENT_POWER:
			SetShutdown();
			break;
	}
}





/**
 * Show a single message screen.
 * @param msg Message.
 */
void ShowMessageScreen(const char *msg)
{
	const char *line;
	const char *end;
	int lines = 1;
	int y;

	for (line = msg; *line; line++)
	{
		if (*line == '\n')
			lines++;
	}
	y = 232 - (lines - 1) * 10;

	ClearScreen();
	PrintInfo();
	line = msg;
	while (line != NULL)
	{
		int len;
		int x;

		end = strchr(line, '\n');
		len = end ? (int)(end - line) : (int)strlen(line);
		x = (640 - len * 10) / 2;
		PrintFormat(DEFAULT_SIZE, BLACK, x, y, "%.*s", len, line);
		y += 20;
		line = end ? end + 1 : NULL;
	}
	GRRLIB_Render();
	ClearScreen();
}

/**
 * Show a single message screen and then exit to loader..
 * @param msg Message.
 * @param ret Return value. If non-zero, text will be printed in red.
 */
void ShowMessageScreenAndExit(const char *msg, int ret)
{
	const int len = strlen(msg);
	const int x = (640 - (len*10)) / 2;
	const u32 color = (ret == 0 ? BLACK : MAROON);

	ClearScreen();
	PrintInfo();
	PrintFormat(DEFAULT_SIZE, color, x, 232, "%s", msg);
	ExitToLoader(ret);
}

/**
 * Show a message screen and wait for the POWER button before exiting to loader.
 * Used in place of the SD/USB device+game picker, which doesn't work with the
 * controller setup on this fork.
 * @param msg Message.
 */
void ShowMessageScreenAndWaitForPower(const char *msg)
{
	static const char PowerLine[] = "Press POWER to exit to Homebrew Channel.";
	const int msgX = (640 - (int)strlen(msg)*10) / 2;
	const int powerX = (640 - (int)sizeof(PowerLine)*10) / 2;

	while (1)
	{
		VIDEO_WaitVSync();
		FPAD_Update();
		if (Shutdown)
			ExitToLoader(0);

		ClearScreen();
		PrintInfo();
		PrintFormat(DEFAULT_SIZE, MAROON, msgX, 232, "%s", msg);
		PrintFormat(DEFAULT_SIZE, MAROON, powerX, 252, "%s", PowerLine);
		GRRLIB_Render();
		ClearScreen();
	}
}

/**
 * Print Nintendont version and system hardware information.
 */
void PrintInfo(void)
{
	const char *consoleType = (isWiiVC ? (IsWiiUFastCPU() ? "WiiVC 5x CPU" : "Wii VC") : (IsWiiUFastCPU() ? "WiiU 5x CPU" : (IsWiiU() ? "Wii U" : "Wii")));
#ifdef NIN_SPECIAL_VERSION
	// "Special" version with customizations. (Not mainline!)
	PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, MENU_POS_Y + 20*0, "Nintendont Loader v%u.%u" NIN_SPECIAL_VERSION " (%s)",
		    NIN_VERSION>>16, NIN_VERSION&0xFFFF, consoleType);
#else
	PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, MENU_POS_Y + 20*0, "Nintendont Loader v%u.%u (%s)",
		    NIN_VERSION>>16, NIN_VERSION&0xFFFF, consoleType);
#endif
	PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, MENU_POS_Y + 20*1, "Built   : " __DATE__ " " __TIME__);
	PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X, MENU_POS_Y + 20*2, "Firmware: %u.%u.%u",
		    *(vu16*)0x80003140, *(vu8*)0x80003142, *(vu8*)0x80003143);
}

void PrintSusamuneBuild(void)
{
	static const char BuildProduct[] = "Moonshine Launcher ZETA";
	static const char BuildTitle[] =
		"V2.2.0 Pre-Release 3 \"The House Always Wins\".";
	static const char BuildChecksum[] = "[" SUSAMUNE_BUILD_CHECKSUM "].";
	PrintFormat(DEFAULT_SIZE, BLACK,
	            640 - MENU_POS_X - ((int)sizeof(BuildProduct) - 1) * 10,
	            406, "%s", BuildProduct);
	PrintFormat(DEFAULT_SIZE, BLACK,
	            640 - MENU_POS_X - ((int)sizeof(BuildTitle) - 1) * 10,
	            426, "%s", BuildTitle);
	PrintFormat(DEFAULT_SIZE, BLACK,
	            640 - MENU_POS_X - ((int)sizeof(BuildChecksum) - 1) * 10,
	            446, "%s", BuildChecksum);
}

/**
 * Print button actions.
 * Call this function after PrintInfo().
 *
 * If any button action is NULL, that button won't be displayed.
 *
 * @param btn_home	[in,opt] Home button action.
 * @param btn_a		[in,opt] A button action.
 * @param btn_b		[in,opt] B button action.
 * @param btn_x1	[in,opt] X/1 button action.
 */
void PrintButtonActions(const char *btn_home, const char *btn_a, const char *btn_b, const char *btn_x1)
{
	if (btn_home) {
		PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X + 430, MENU_POS_Y + 20*0, "Home: %s", btn_home);
	}
	if (btn_a) {
		PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X + 430, MENU_POS_Y + 20*1, "A   : %s", btn_a);
	}
	if (btn_b) {
		PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X + 430, MENU_POS_Y + 20*2, "B   : %s", btn_b);
	}
	if (btn_x1) {
		PrintFormat(DEFAULT_SIZE, BLACK, MENU_POS_X + 430, MENU_POS_Y + 20*3, "X/1 : %s", btn_x1);
	}
}

void ReconfigVideo(GXRModeObj *vidmode)
{
	if(ncfg->VideoScale >= 40 && ncfg->VideoScale <= 120)
		vidmode->viWidth = ncfg->VideoScale + 600;
	else
		vidmode->viWidth = 640;
	vidmode->viXOrigin = (720 - vidmode->viWidth) / 2;

	if(ncfg->VideoOffset >= -20 && ncfg->VideoOffset <= 20)
	{
		if((vidmode->viXOrigin + ncfg->VideoOffset) < 0)
			vidmode->viXOrigin = 0;
		else if((vidmode->viXOrigin + ncfg->VideoOffset) > 80)
			vidmode->viXOrigin = 80;
		else
			vidmode->viXOrigin += ncfg->VideoOffset;
	}
	VIDEO_Configure(vidmode);
}

/**
 * Print a LoadKernel() error message.
 *
 * This function does NOT force a return to loader;
 * that must be handled by the caller.
 * Caller must also call UpdateScreen().
 *
 * @param iosErr IOS loading error ID.
 * @param err Return value from the IOS function.
 */
void PrintLoadKernelError(LoadKernelError_t iosErr, int err)
{
	ClearScreen();
	PrintInfo();
	PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*4, "Failed to load IOS58 from NAND:");

	switch (iosErr)
	{
		case LKERR_UNKNOWN:
		default:
			// TODO: Add descriptions of more LoadKernel() errors.
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*5, "LoadKernel() error %d occurred, returning %d.", iosErr, err);
			break;

		case LKERR_ES_GetStoredTMDSize:
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*5, "ES_GetStoredTMDSize() returned %d.", err);
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*7, "This usually means IOS58 is not installed.");
			if (IsWiiU())
			{
				// No IOS58 on Wii U should never happen...
				PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*9, "WARNING: On Wii U, a missing IOS58 may indicate");
				PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*10, "something is seriously wrong with the vWii setup.");
			}
			else
			{
				// TODO: Check if we're using System 4.3.
				PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*9, "Please update to Wii System 4.3 and try running");
				PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*10, "Nintendont again.");
			}
			break;

		case LKERR_TMD_malloc:
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*5, "Unable to allocate memory for the IOS58 TMD.");
			break;

		case LKERR_ES_GetStoredTMD:
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*5, "ES_GetStoredTMD() returned %d.", err);
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*7, "WARNING: IOS58 may be corrupted.");
			break;

		case LKERR_IOS_Open_shared1_content_map:
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*5, "IOS_Open(\"/shared1/content.map\") returned %d.", err);
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*7, "This usually means Nintendont was not started with");
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*8, "AHB access permissions.");
			// FIXME: Create meta.xml if it isn't there?
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*10, "Please ensure that meta.xml is present in your Nintendont");
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*11, "application directory and that it contains a line");
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*12, "with the tag <ahb_access/> .");
			break;

		case LKERR_IOS_Open_IOS58_kernel:
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*5, "IOS_Open(IOS58 kernel) returned %d.", err);
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*7, "WARNING: IOS58 may be corrupted.");
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*8, "It's no use! Saifogeo won't work!");
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*10, "CHAJIRU SAIFODON!!");
			break;

		case LKERR_IOS_Read_IOS58_kernel:
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*5, "IOS_Read(IOS58 kernel) returned %d.", err);
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*7, "WARNING: IOS58 may be corrupted.");
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*9, "Sagashita koe ga kikoete kureba Chiisana yume ga kagayaki ni naru Hohoemi ni sono hikari Itsuka te wo nobashiteku");
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*11, "TODOKETE Setsunasa ni wa namae o tsukeyou ka Snow Halation");
			PrintFormat(DEFAULT_SIZE, MAROON, MENU_POS_X, MENU_POS_Y + 20*14, "Amar, sentir Son cosas que no tengo si no existes tu Si me haces ver el cielo siempre m�s azul Y empiezo a comprender a toda plenitud Lo que es morir en vida Cuando tu me abrazas Que acaba el mundo Que no pasa nada si est�s t�");
			break;
	}
}
