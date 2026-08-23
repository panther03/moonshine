// Nintendont: FatFs UTF-8 wrapper functions.
#include "ff_utf8.h"
#include <stdbool.h>
#include <stdint.h>

#include "ff_utf8_core.h"

int ff_utf8_decode_next(const char **input, uint32_t *codepoint)
{
	const uint8_t *cursor;
	uint32_t value;
	uint32_t minimum;
	uint8_t lead;
	unsigned int trailing;
	unsigned int i;

	if (input == NULL || *input == NULL || codepoint == NULL)
		return false;
	cursor = (const uint8_t *)*input;
	lead = *cursor++;
	if (lead < 0x80u)
	{
		if (lead == 0)
			return false;
		value = lead;
		minimum = 0;
		trailing = 0;
	}
	else if ((lead & 0xe0u) == 0xc0u)
	{
		value = lead & 0x1fu;
		minimum = 0x80u;
		trailing = 1;
	}
	else if ((lead & 0xf0u) == 0xe0u)
	{
		value = lead & 0x0fu;
		minimum = 0x800u;
		trailing = 2;
	}
	else if ((lead & 0xf8u) == 0xf0u)
	{
		value = lead & 0x07u;
		minimum = 0x10000u;
		trailing = 3;
	}
	else
	{
		return false;
	}

	for (i = 0; i < trailing; i++)
	{
		uint8_t next = *cursor++;
		if ((next & 0xc0u) != 0x80u)
			return false;
		value = (value << 6) | (next & 0x3fu);
	}
	/* The minimum rejects overlong forms; surrogates are not scalars. */
	if (value < minimum || value > 0x10ffffu ||
	    (value >= 0xd800u && value <= 0xdfffu))
		return false;

	*input = (const char *)cursor;
	*codepoint = value;
	return true;
}

// Temporary path/name buffer.
// NOT REENTRANT OR THREAD-SAFE!
static union {
	WCHAR path[512];
	char name[_MAX_LFN * 3 + 1];
} scratch;

/**
 * Convert a UTF-8 string to WCHAR (UTF-16).
 * Uses the shared scratch buffer. (NOT REENTRANT OR THREAD-SAFE!)
 * @param str UTF-8 string.
 * @return True if converted; false if string is empty or invalid.
 */
static inline bool char_to_wchar(const char *str)
{
	return ff_utf8_to_utf16(str, (uint16_t *)scratch.path,
	                        sizeof(scratch.path) / sizeof(scratch.path[0]));
}

/**
 * Convert a UTF-16 string (WCHAR) to UTF-8.
 * Uses the shared scratch buffer. (NOT REENTRANT OR THREAD-SAFE!)
 * @param wcs WCHAR string.
 * @return UTF-8 string. (STATIC BUFFER; use immediately, DO NOT FREE!)
 */
const char *wchar_to_char(const WCHAR *wcs)
{
	ff_utf16_to_utf8((const uint16_t *)wcs, scratch.name,
	                 sizeof(scratch.name));
	return scratch.name;
}

FRESULT f_open_char(FIL* fp, const char* path, BYTE mode)
{
	if (!char_to_wchar(path))
		return FR_INVALID_NAME;
	return f_open(fp, scratch.path, mode);
}

FRESULT f_mount_char(FATFS* fs, const char* path, BYTE opt)
{
	if (!char_to_wchar(path))
		return FR_INVALID_NAME;
	return f_mount(fs, scratch.path, opt);
}

FRESULT f_stat_char(const char* path, FILINFO* fno)
{
	if (!char_to_wchar(path))
		return FR_INVALID_NAME;
	return f_stat(scratch.path, fno);
}

#if !_FS_READONLY
FRESULT f_mkdir_char(const char* path)
{
	if (!char_to_wchar(path))
		return FR_INVALID_NAME;
	return f_mkdir(scratch.path);
}

FRESULT f_unlink_char(const char* path)
{
	if (!char_to_wchar(path))
		return FR_INVALID_NAME;
	return f_unlink(scratch.path);
}
#endif /* !_FS_READONLY */

#if _FS_RPATH >= 1
#if _VOLUMES >= 2
FRESULT f_chdrive_char(const char* path)
{
	if (!char_to_wchar(path))
		return FR_INVALID_NAME;
	return f_chdrive(scratch.path);
}
#endif /* _VOLUMES >= 2 */

FRESULT f_chdir_char(const char* path)
{
	if (!char_to_wchar(path))
		return FR_INVALID_NAME;
	return f_chdir(scratch.path);
}
#endif /* _FS_RPATH >= 1 */

#if _FS_MINIMIZE <= 1
FRESULT f_opendir_char(DIR* dp, const char* path)
{
	if (!char_to_wchar(path))
		return FR_INVALID_NAME;
	return f_opendir(dp, scratch.path);
}
#endif /* _FS_MINIMIZE <= 1 */
