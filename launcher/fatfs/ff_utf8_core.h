#ifndef _FATFS_UTF8_CORE
#define _FATFS_UTF8_CORE

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__GNUC__)
#define FF_UTF8_NOINLINE __attribute__((noinline))
#else
#define FF_UTF8_NOINLINE
#endif

static FF_UTF8_NOINLINE bool ff_utf8_to_utf16(const char *input,
	                                           uint16_t *output,
	                          size_t capacity)
{
	const char *cursor = input;
	size_t used = 0;

	if (input == NULL || output == NULL || capacity == 0 || *cursor == '\0')
		return false;

	while (*cursor != '\0')
	{
		uint32_t codepoint;

		if (!ff_utf8_decode_next(&cursor, &codepoint))
			return false;

		if (codepoint >= 0x10000u)
		{
			if (used + 2 >= capacity)
				return false;
			codepoint -= 0x10000u;
			output[used++] = (uint16_t)(0xd800u | (codepoint >> 10));
			output[used++] = (uint16_t)(0xdc00u | (codepoint & 0x3ffu));
		}
		else
		{
			if (used + 1 >= capacity)
				return false;
			output[used++] = (uint16_t)codepoint;
		}
	}

	output[used] = 0;
	return true;
}

static size_t ff_utf16_to_utf8(const uint16_t *input, char *output,
	                            size_t capacity)
{
	size_t used = 0;

	if (output == NULL || capacity == 0)
		return 0;
	if (input == NULL)
	{
		output[0] = '\0';
		return 0;
	}

	while (*input != 0)
	{
		uint32_t codepoint = *input++;
		size_t needed;

		if (codepoint >= 0xd800u && codepoint <= 0xdbffu)
		{
			uint32_t low = *input;
			if (low >= 0xdc00u && low <= 0xdfffu)
			{
				input++;
				codepoint = 0x10000u + ((codepoint - 0xd800u) << 10) +
				            (low - 0xdc00u);
			}
			else
			{
				codepoint = 0xfffdu;
			}
		}
		else if (codepoint >= 0xdc00u && codepoint <= 0xdfffu)
		{
			/* Keep directory browsing safe if corrupt media has a lone surrogate. */
			codepoint = 0xfffdu;
		}

		if (codepoint < 0x80u)
			needed = 1;
		else if (codepoint < 0x800u)
			needed = 2;
		else if (codepoint < 0x10000u)
			needed = 3;
		else
			needed = 4;
		if (used + needed >= capacity)
			break;

		if (needed == 1)
		{
			output[used++] = (char)codepoint;
		}
		else if (needed == 2)
		{
			output[used++] = (char)(0xc0u | (codepoint >> 6));
			output[used++] = (char)(0x80u | (codepoint & 0x3fu));
		}
		else if (needed == 3)
		{
			output[used++] = (char)(0xe0u | (codepoint >> 12));
			output[used++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
			output[used++] = (char)(0x80u | (codepoint & 0x3fu));
		}
		else
		{
			output[used++] = (char)(0xf0u | (codepoint >> 18));
			output[used++] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
			output[used++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
			output[used++] = (char)(0x80u | (codepoint & 0x3fu));
		}
	}

	output[used] = '\0';
	return used;
}

#undef FF_UTF8_NOINLINE

#endif /* _FATFS_UTF8_CORE */
