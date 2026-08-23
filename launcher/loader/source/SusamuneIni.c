/*

Susamune launcher settings, loader side. See SusamuneIni.h for what lives here
and why nincfg.bin is gone.

The write path is a copy-through, for the same reason the kernel's is: this
side only knows [nintendont], and re-emitting the file from what we parsed
would delete the mod's per-region settings, binds and overlay sections. So the
old file is read back and every line outside our own section lands in the
output unchanged.

*/
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "diskio.h"
#include "exi.h"
#include "SusamuneIni.h"
#include "ff_utf8.h"
#include "susamune/susamune_cfg.h"

// Whole-file buffer. Same ceiling as the kernel's (SusamuneCfg.c): a file
// bigger than this is refused rather than truncated, since a partial
// copy-through would silently drop a game version's settings.
#define SUSA_INI_BUF_SIZE 32768

#define SUSA_SECTION_NAME_MAX 24

SusamuneIni gIni;

// Set by SusamuneIniLoad when the file has no [nintendont] section yet, so the
// caller can author one -- the defaults live here, and the user should be able
// to find and hand-edit the keys without having visited every menu first.
static bool SawSection = false;

// Defaults for a first run: JP selected, nothing configured, read speed and
// progressive enabled, everything else off/auto.
static const SusamuneIni kIniDefaults =
{
	.version          = SUSA_VER_JP,
	.path             = { "", "", "" },
	.autoboot         = 0,
	.nativeControls   = 0,
	.unlockReadSpeed  = 1,
	.enableCheats     = 0,
	.forceProgressive = 1,
	.disableRumble    = 0,
	.language         = NIN_LAN_AUTO,
};

static const char *const kVersionTags[SUSA_VER_COUNT]  = { "jp", "us", "pal" };
static const char *const kVersionNames[SUSA_VER_COUNT] = { "JP", "US", "PAL" };
static const u32 kVersionGameIDs[SUSA_VER_COUNT] =
{
	0x474D534A,	// GMSJ
	0x474D5345,	// GMSE
	0x474D5350,	// GMSP
};

// language = <token>. Index is the NIN_LAN_* value; auto is handled separately
// because it is -1.
static const char *const kLanguageTokens[NIN_LAN_LAST] =
{
	"english", "german", "french", "spanish", "italian", "dutch"
};

const char *SusaVersionTag(u8 version)
{
	return kVersionTags[version < SUSA_VER_COUNT ? version : SUSA_VER_JP];
}

const char *SusaVersionName(u8 version)
{
	return kVersionNames[version < SUSA_VER_COUNT ? version : SUSA_VER_JP];
}

u32 SusaVersionGameID(u8 version)
{
	return kVersionGameIDs[version < SUSA_VER_COUNT ? version : SUSA_VER_JP];
}

// ---------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------

static bool IsSpace(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Trim in place; returns the first non-space character and terminates the
// string after the last one.
static char *Trim(char *s)
{
	char *end;

	while (*s && IsSpace(*s))
		s++;

	end = s + strlen(s);
	while (end > s && IsSpace(end[-1]))
		end--;
	*end = '\0';

	return s;
}

// A typo leaves the default in place rather than applying a garbage value.
static bool ParseBool(const char *s, u8 *out)
{
	if (strcmp(s, "0") == 0) { *out = 0; return true; }
	if (strcmp(s, "1") == 0) { *out = 1; return true; }
	return false;
}

static bool ParseLanguage(const char *s, s32 *out)
{
	u32 i;

	if (strcasecmp(s, "auto") == 0)
	{
		*out = NIN_LAN_AUTO;
		return true;
	}
	for (i = 0; i < NIN_LAN_LAST; i++)
	{
		if (strcasecmp(s, kLanguageTokens[i]) == 0)
		{
			*out = (s32)i;
			return true;
		}
	}
	return false;
}

static bool ParseVersion(const char *s, u8 *out)
{
	u32 i;

	for (i = 0; i < SUSA_VER_COUNT; i++)
	{
		if (strcasecmp(s, kVersionTags[i]) == 0)
		{
			*out = (u8)i;
			return true;
		}
	}
	return false;
}

// One key = value line inside [nintendont].
static void ApplyKey(const char *key, const char *value)
{
	u32 i;

	for (i = 0; i < SUSA_VER_COUNT; i++)
	{
		char pathKey[16];
		snprintf(pathKey, sizeof(pathKey), "path_%s", kVersionTags[i]);
		if (strcmp(key, pathKey) == 0)
		{
			// A blank value is the documented "not configured" state, and is
			// what the file is first written with.
			strncpy(gIni.path[i], value, SUSA_PATH_MAX - 1);
			gIni.path[i][SUSA_PATH_MAX - 1] = '\0';
			return;
		}
	}

	if (strcmp(key, "version") == 0)
		ParseVersion(value, &gIni.version);
	else if (strcmp(key, "autoboot") == 0)
		ParseBool(value, &gIni.autoboot);
	else if (strcmp(key, "native_controls") == 0)
		ParseBool(value, &gIni.nativeControls);
	else if (strcmp(key, "unlock_read_speed") == 0)
		ParseBool(value, &gIni.unlockReadSpeed);
	else if (strcmp(key, "enable_cheats") == 0)
		ParseBool(value, &gIni.enableCheats);
	else if (strcmp(key, "force_progressive") == 0)
		ParseBool(value, &gIni.forceProgressive);
	else if (strcmp(key, "disable_rumble") == 0)
		ParseBool(value, &gIni.disableRumble);
	else if (strcmp(key, "language") == 0)
		ParseLanguage(value, &gIni.language);
}

static void ParseIni(char *text)
{
	char *line = text;
	bool inSection = false;

	while (*line)
	{
		char *next;
		char *eq;

		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		else
			next = line + strlen(line);

		line = Trim(line);

		if (*line == '\0' || *line == ';' || *line == '#')
		{
			line = next;
			continue;
		}

		if (*line == '[')
		{
			char *close = strchr(line, ']');
			if (close)
			{
				*close = '\0';
				inSection = (strcmp(Trim(line + 1),
						    SUSAMUNE_INI_SECTION_NINTENDONT) == 0);
				if (inSection)
					SawSection = true;
			}
			line = next;
			continue;
		}

		eq = strchr(line, '=');
		if (eq == NULL)
		{
			line = next;
			continue;
		}
		*eq = '\0';

		if (inSection)
			ApplyKey(Trim(line), Trim(eq + 1));

		line = next;
	}
}

// ---------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------

static void Emit(FIL *f, int *err, const char *s, u32 len)
{
	UINT wrote;
	int  ret;

	if (*err != FR_OK)
		return;

	ret = f_write(f, s, len, &wrote);
	if (ret == FR_OK && wrote != len)
		ret = FR_DISK_ERR;
	*err = ret;
}

static void EmitStr(FIL *f, int *err, const char *s)
{
	Emit(f, err, s, (u32)strlen(s));
}

static const char *LanguageToken(s32 language)
{
	if (language < 0 || language >= NIN_LAN_LAST)
		return "auto";
	return kLanguageTokens[language];
}

static void EmitNintendontSection(FIL *f, int *err)
{
	char line[SUSA_PATH_MAX + 32];
	u32  i;

	EmitStr(f, err, "[" SUSAMUNE_INI_SECTION_NINTENDONT "]\r\n");

	snprintf(line, sizeof(line), "version = %s\r\n", SusaVersionTag(gIni.version));
	EmitStr(f, err, line);

	for (i = 0; i < SUSA_VER_COUNT; i++)
	{
		snprintf(line, sizeof(line), "path_%s = %s\r\n",
			 kVersionTags[i], gIni.path[i]);
		EmitStr(f, err, line);
	}

	snprintf(line, sizeof(line), "autoboot = %u\r\n", gIni.autoboot);
	EmitStr(f, err, line);
	snprintf(line, sizeof(line), "native_controls = %u\r\n", gIni.nativeControls);
	EmitStr(f, err, line);
	snprintf(line, sizeof(line), "unlock_read_speed = %u\r\n", gIni.unlockReadSpeed);
	EmitStr(f, err, line);
	snprintf(line, sizeof(line), "enable_cheats = %u\r\n", gIni.enableCheats);
	EmitStr(f, err, line);
	snprintf(line, sizeof(line), "force_progressive = %u\r\n", gIni.forceProgressive);
	EmitStr(f, err, line);
	snprintf(line, sizeof(line), "disable_rumble = %u\r\n", gIni.disableRumble);
	EmitStr(f, err, line);
	snprintf(line, sizeof(line), "language = %s\r\n", LanguageToken(gIni.language));
	EmitStr(f, err, line);
}

static const char kIniBanner[] =
	"; Moonshine settings\r\n"
	"; [nintendont] is written by the Moonshine Launcher and is regenerated\r\n"
	"; whenever you leave one of its menus with changes pending, so comments\r\n"
	"; added inside it are lost. Everything else in this file is preserved.\r\n"
	";\r\n"
	"; path_<region> is where that version's disc image lives, including the\r\n"
	"; device (sd:/games/game.iso). Leave it blank to be prompted in the\r\n"
	"; launcher, or set it to `di` to boot from the disc drive.\r\n"
	";\r\n"
	"; With autoboot = 1 the launcher skips its menu and boots the version\r\n"
	"; named by `version` straight away. Hold B at startup for the menu.\r\n"
	";\r\n"
	"; In-game settings, binds and overlay sections are written by the mod.\r\n"
	"; Their suffix is jp = GMSJ, us = GMSE, or pal = GMSP.\r\n"
	"\r\n";

// ---------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------

static void BuildPath(char *out, u32 size, const char *device)
{
	snprintf(out, size, "%s:" SUSAMUNE_INI_PATH, device);
}

static BYTE DeviceForName(const char *device)
{
	return strcmp(device, "usb") == 0 ? DEV_USB : DEV_SD;
}

bool SusamuneIniNeedsWrite(void)
{
	return !SawSection;
}

bool SusamuneIniWritable(const char *device)
{
	char path[32];
	char probePath[32];
	FIL f;
	FILINFO info;
	FRESULT ret;
	UINT wrote;
	u32 i;
	BYTE dev = DeviceForName(device);

	BuildPath(path, sizeof(path), device);
	ret = f_stat_char(path, &info);
	if (ret == FR_OK && (info.fattrib & AM_RDO))
		return false;
	if (ret != FR_OK && ret != FR_NO_FILE)
	{
		if (ret == FR_DISK_ERR || ret == FR_INT_ERR)
			RemountDevice(dev);
		return false;
	}

	// FatFS writes through a RAM cache, so opening a file is not a write test.
	for (i = 0; i < 4; i++)
	{
		snprintf(probePath, sizeof(probePath), "%s:/.susa-wr%u.tmp", device, i);
		ret = f_open_char(&f, probePath, FA_WRITE | FA_CREATE_NEW);
		if (ret != FR_EXIST)
			break;
	}
	if (ret != FR_OK)
	{
		if (ret == FR_DISK_ERR || ret == FR_INT_ERR)
			RemountDevice(dev);
		return false;
	}

	ret = f_write(&f, "S", 1, &wrote);
	if (ret == FR_OK && wrote != 1)
		ret = FR_DISK_ERR;
	if (f_close(&f) != FR_OK)
		ret = FR_DISK_ERR;
	if (ret == FR_OK)
		ret = f_unlink_char(probePath);
	if (ret != FR_OK)
	{
		// Discard the ghost directory/FAT changes left in the RAM cache.
		RemountDevice(dev);
		return false;
	}
	return true;
}

void SusamuneIniLoad(const char *device)
{
	char  path[32];
	FIL   f;
	char *buf;
	UINT  read = 0;

	gIni = kIniDefaults;
	SawSection = false;

	BuildPath(path, sizeof(path), device);
	if (f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
	{
		gprintf("Susamune: no %s, using defaults\n", path);
		return;
	}

	buf = (char*)malloc(SUSA_INI_BUF_SIZE);
	if (buf != NULL)
	{
		if (f_read(&f, buf, SUSA_INI_BUF_SIZE - 1, &read) != FR_OK)
			read = 0;
		buf[read] = '\0';
		ParseIni(buf);
		free(buf);
	}
	f_close(&f);
}

int SusamuneIniSave(const char *device)
{
	char  path[32];
	FIL   f;
	char *buf;
	char *line;
	UINT  read = 0;
	int   ret;
	int   err = FR_OK;
	bool  skipping = false;
	bool  wroteSection = false;

	buf = (char*)malloc(SUSA_INI_BUF_SIZE);
	if (buf == NULL)
		return FR_NOT_ENOUGH_CORE;
	buf[0] = '\0';

	BuildPath(path, sizeof(path), device);

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_OK)
	{
		// Refuse rather than truncate -- see the buffer note above.
		if (f_size(&f) >= SUSA_INI_BUF_SIZE)
		{
			f_close(&f);
			free(buf);
			return FR_NOT_ENOUGH_CORE;
		}
		if (f_read(&f, buf, SUSA_INI_BUF_SIZE - 1, &read) != FR_OK)
			read = 0;
		buf[read] = '\0';
		f_close(&f);
	}

	ret = f_open_char(&f, path, FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
	{
		free(buf);
		return ret;
	}

	if (read == 0)
		Emit(&f, &err, kIniBanner, (u32)(sizeof(kIniBanner) - 1));

	line = buf;
	while (*line)
	{
		char *next;
		char *text;

		next = strchr(line, '\n');
		if (next)
			*next++ = '\0';
		else
			next = line + strlen(line);

		text = Trim(line);	// also drops the \r of a CRLF file

		if (text[0] == '[')
		{
			char *close = strchr(text, ']');
			if (close)
			{
				// Copy the name out rather than punching a NUL into the
				// line: the pass-through branch has to emit it verbatim.
				char sect[SUSA_SECTION_NAME_MAX];
				u32  len = (u32)(close - text - 1);
				bool ours = false;

				if (len < sizeof(sect))
				{
					memcpy(sect, text + 1, len);
					sect[len] = '\0';
					ours = (strcmp(Trim(sect),
						       SUSAMUNE_INI_SECTION_NINTENDONT) == 0);
				}

				skipping = ours;
				if (ours)
				{
					EmitNintendontSection(&f, &err);
					wroteSection = true;
				}
			}
		}

		if (!skipping)
		{
			EmitStr(&f, &err, text);
			EmitStr(&f, &err, "\r\n");
		}
		line = next;
	}

	if (!wroteSection)
	{
		EmitStr(&f, &err, "\r\n");
		EmitNintendontSection(&f, &err);
	}

	ret = f_close(&f);
	if (err == FR_OK && ret != FR_OK)
		err = ret;
	free(buf);

	if (err != FR_OK)
		RemountDevice(DeviceForName(device));
	return err;
}
