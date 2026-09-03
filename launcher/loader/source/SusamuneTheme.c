#include <malloc.h>
#include <png.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff_utf8.h"
#include "exi.h"
#include "global.h"
#include "SusamuneTheme.h"

#define THEME_BACKGROUND_WIDTH  1024u
#define THEME_BACKGROUND_HEIGHT 480u
#define THEME_BACKGROUND_BYTES  \
	(THEME_BACKGROUND_WIDTH * THEME_BACKGROUND_HEIGHT * 4u)
#define THEME_PNG_ROW_BYTES     (THEME_BACKGROUND_WIDTH * 4u)
#define THEME_PATH_MAX          512u
#define THEME_WARNING_MAX       160u

typedef struct
{
	FIL *file;
	FRESULT ioResult;
	char error[80];
} ThemePngReader;

static GRRLIB_texImg *sCustomBackground;
static float sPanOffset;
static float sPanDirection = -1.0f;
static size_t sHeapPeak;
static char sWarning[THEME_WARNING_MAX];
static bool sSolidFallback = true;
static ThemePngReader sPngReader;

static void SampleHeap(void)
{
	const struct mallinfo info = mallinfo();

	if (info.uordblks > sHeapPeak)
		sHeapPeak = info.uordblks;
}

static u32 ReadBE32(const u8 *p)
{
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) |
	       ((u32)p[2] << 8) | (u32)p[3];
}

static void LogHeap(const char *where)
{
	const struct mallinfo info = mallinfo();

	SampleHeap();
	gprintf("Susamune theme heap %s: live=%u free=%u sampled_peak=%u\n",
		where, (unsigned int)info.uordblks, (unsigned int)info.fordblks,
		(unsigned int)sHeapPeak);
}

static bool BuildThemeDirectory(char *out, size_t outSize, const char *device,
	const char *launchDir)
{
	const char *dir = launchDir;
	const char *colon;
	size_t dirLen;
	int written;

	if (out == NULL || outSize == 0 || device == NULL)
		return false;
	if (strcmp(device, "sd") != 0 && strcmp(device, "usb") != 0)
		return false;

	if (dir == NULL || dir[0] == '\0')
		dir = "/apps/moonshine_launcher/";
	colon = strchr(dir, ':');
	if (colon != NULL)
		dir = colon + 1;
	if (dir[0] == '\0')
		dir = "/";
	if (dir[0] != '/')
		return false;
	if (strchr(dir, ':') != NULL || strchr(dir, '\\') != NULL ||
	    strstr(dir, "../") != NULL || strstr(dir, "/..") != NULL)
		return false;

	dirLen = strlen(dir);
	written = snprintf(out, outSize, "%s:%s%stheme", device, dir,
		(dirLen > 0 && dir[dirLen - 1] == '/') ? "" : "/");
	return written > 0 && (size_t)written < outSize;
}

static bool BuildThemePath(char *out, size_t outSize, const char *device,
	const char *launchDir, const char *leaf)
{
	char directory[THEME_PATH_MAX];
	int written;

	if (leaf == NULL || strchr(leaf, '/') != NULL || strchr(leaf, '\\') != NULL)
		return false;
	if (!BuildThemeDirectory(directory, sizeof(directory), device, launchDir))
		return false;
	written = snprintf(out, outSize, "%s/%s", directory, leaf);
	return written > 0 && (size_t)written < outSize;
}

static bool EnsureThemeDirectory(const char *device, const char *launchDir)
{
	char path[THEME_PATH_MAX];
	FRESULT result;

	if (!BuildThemeDirectory(path, sizeof(path), device, launchDir))
	{
		snprintf(sWarning, sizeof(sWarning),
			"Theme folder path is invalid.\nUsing the stock background.");
		gprintf("Susamune theme: invalid launcher directory\n");
		return false;
	}
	result = f_mkdir_char(path);
	if (result != FR_OK && result != FR_EXIST)
	{
		snprintf(sWarning, sizeof(sWarning),
			"Theme folder unavailable (I/O %d).\nUsing the stock background.",
			(int)result);
		gprintf("Susamune theme: could not create %s (%d)\n", path, (int)result);
		return false;
	}
	return true;
}

static bool ValidPngColor(u8 depth, u8 color)
{
	switch (color)
	{
		case PNG_COLOR_TYPE_GRAY:
			return depth == 1 || depth == 2 || depth == 4 ||
			       depth == 8 || depth == 16;
		case PNG_COLOR_TYPE_RGB:
		case PNG_COLOR_TYPE_GRAY_ALPHA:
		case PNG_COLOR_TYPE_RGB_ALPHA:
			return depth == 8 || depth == 16;
		case PNG_COLOR_TYPE_PALETTE:
			return depth == 1 || depth == 2 || depth == 4 || depth == 8;
		default:
			return false;
	}
}

static bool ValidatePngHeader(const u8 *header, u32 size)
{
	static const u8 signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

	if (size < 33u || memcmp(header, signature, sizeof(signature)) != 0)
		return false;
	if (ReadBE32(header + 8) != 13u || memcmp(header + 12, "IHDR", 4) != 0)
		return false;
	if (ReadBE32(header + 16) != THEME_BACKGROUND_WIDTH ||
	    ReadBE32(header + 20) != THEME_BACKGROUND_HEIGHT)
		return false;
	if (!ValidPngColor(header[24], header[25]) || header[26] != 0 ||
	    header[27] != 0 || header[28] > 1)
		return false;
	return true;
}

static void ThemePngError(png_structp png, png_const_charp message)
{
	ThemePngReader *reader = png_get_error_ptr(png);

	if (reader != NULL && reader->error[0] == '\0')
		snprintf(reader->error, sizeof(reader->error), "%s",
			message != NULL ? message : "PNG decode error");
	png_longjmp(png, 1);
}

static void ThemePngWarning(png_structp png, png_const_charp message)
{
	(void)png;
	if (message != NULL)
		gprintf("Susamune theme PNG warning: %s\n", message);
}

static void ThemePngRead(png_structp png, png_bytep data, png_size_t size)
{
	ThemePngReader *reader = png_get_io_ptr(png);
	UINT got = 0;

	if (reader == NULL || reader->file == NULL || size > 0xffffffffu)
		png_error(png, "invalid PNG read");
	reader->ioResult = f_read(reader->file, data, (UINT)size, &got);
	if (reader->ioResult != FR_OK || got != (UINT)size)
		png_error(png, "truncated PNG or storage error");
}

static u32 TextureOffset(u32 x, u32 y)
{
	return (((y & ~3u) << 2) * THEME_BACKGROUND_WIDTH) +
	       ((x & ~3u) << 4) + ((((y & 3u) << 2) + (x & 3u)) << 1);
}

static void UnpackRow(const u8 *texture, u32 y, u8 *row)
{
	u32 x;

	for (x = 0; x < THEME_BACKGROUND_WIDTH; x++)
	{
		const u32 offset = TextureOffset(x, y);
		row[x * 4u + 0u] = texture[offset + 1u];
		row[x * 4u + 1u] = texture[offset + 32u];
		row[x * 4u + 2u] = texture[offset + 33u];
		row[x * 4u + 3u] = texture[offset + 0u];
	}
}

static void PackRow(u8 *texture, u32 y, const u8 *row)
{
	u32 x;

	for (x = 0; x < THEME_BACKGROUND_WIDTH; x++)
	{
		const u32 offset = TextureOffset(x, y);
		texture[offset + 0u] = row[x * 4u + 3u];
		texture[offset + 1u] = row[x * 4u + 0u];
		texture[offset + 32u] = row[x * 4u + 1u];
		texture[offset + 33u] = row[x * 4u + 2u];
	}
}

static bool DecodePng(FIL *file, u8 *texture, char *error, size_t errorSize)
{
	ThemePngReader *reader = &sPngReader;
	png_structp png;
	png_infop info;
	png_uint_32 width;
	png_uint_32 height;
	png_size_t rowBytes;
	int bitDepth;
	int colorType;
	int interlace;
	int passes;
	int pass;
	u32 y;
	u8 pngRow[THEME_PNG_ROW_BYTES] ATTRIBUTE_ALIGN(32);

	memset(reader, 0, sizeof(*reader));
	reader->file = file;
	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, reader,
		ThemePngError, ThemePngWarning);
	if (png == NULL)
	{
		snprintf(error, errorSize, "PNG decoder allocation failed");
		return false;
	}
	info = png_create_info_struct(png);
	if (info == NULL)
	{
		png_destroy_read_struct(&png, NULL, NULL);
		snprintf(error, errorSize, "PNG decoder allocation failed");
		return false;
	}
	if (setjmp(png_jmpbuf(png)))
	{
		png_destroy_read_struct(&png, &info, NULL);
		snprintf(error, errorSize, "%s",
			reader->error[0] != '\0' ? reader->error : "PNG decode failed");
		return false;
	}

	png_set_read_fn(png, reader, ThemePngRead);
	png_set_user_limits(png, THEME_BACKGROUND_WIDTH, THEME_BACKGROUND_HEIGHT);
	png_set_chunk_cache_max(png, 64u);
	png_set_chunk_malloc_max(png, 256u * 1024u);
	png_set_crc_action(png, PNG_CRC_ERROR_QUIT, PNG_CRC_ERROR_QUIT);
	png_read_info(png, info);
	if (!png_get_IHDR(png, info, &width, &height, &bitDepth, &colorType,
		&interlace, NULL, NULL) || width != THEME_BACKGROUND_WIDTH ||
	    height != THEME_BACKGROUND_HEIGHT)
		png_error(png, "background must be exactly 1024x480");

	if (bitDepth == 16)
		png_set_strip_16(png);
	if (colorType == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
		png_set_expand_gray_1_2_4_to_8(png);
	if (png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if (colorType == PNG_COLOR_TYPE_GRAY ||
	    colorType == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	if ((colorType & PNG_COLOR_MASK_ALPHA) == 0 &&
	    !png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_filler(png, 0xffu, PNG_FILLER_AFTER);

	passes = png_set_interlace_handling(png);
	png_read_update_info(png, info);
	SampleHeap();
	rowBytes = png_get_rowbytes(png, info);
	if (png_get_bit_depth(png, info) != 8 ||
	    png_get_channels(png, info) != 4 || rowBytes != THEME_PNG_ROW_BYTES)
		png_error(png, "unsupported PNG pixel layout");

	memset(texture, 0, THEME_BACKGROUND_BYTES);
	for (pass = 0; pass < passes; pass++)
	{
		for (y = 0; y < THEME_BACKGROUND_HEIGHT; y++)
		{
			if (pass > 0)
				UnpackRow(texture, y, pngRow);
			else
				memset(pngRow, 0, sizeof(pngRow));
			png_read_row(png, pngRow, NULL);
			PackRow(texture, y, pngRow);
			if (pass == 0 && y == 0)
				SampleHeap();
		}
	}
	png_read_end(png, info);
	SampleHeap();
	png_destroy_read_struct(&png, &info, NULL);
	return true;
}

static void ActivateSolidFallback(GRRLIB_texImg **backgroundPtr)
{
	if (backgroundPtr != NULL && *backgroundPtr != NULL)
	{
		if (*backgroundPtr == sCustomBackground)
			sCustomBackground = NULL;
		GRRLIB_FreeTexture(*backgroundPtr);
		*backgroundPtr = NULL;
	}
	sSolidFallback = true;
}

static bool LoadBackground(const char *path, GRRLIB_texImg **backgroundPtr)
{
	FILINFO fileInfo;
	FIL file;
	GRRLIB_texImg *texture;
	u8 header[33];
	UINT got = 0;
	FRESULT result;
	bool validHeader;
	char decodeError[80];

	result = f_stat_char(path, &fileInfo);
	if (result == FR_NO_FILE || result == FR_NO_PATH)
		return false;
	if (result != FR_OK)
	{
		snprintf(sWarning, sizeof(sWarning),
			"Theme PNG unreadable (I/O %d).\nUsing the stock background.",
			(int)result);
		return false;
	}
	if (fileInfo.fsize == 0 || fileInfo.fsize > SUSAMUNE_THEME_BACKGROUND_MAX_SIZE)
	{
		snprintf(sWarning, sizeof(sWarning),
			"Theme PNG is empty or over 2 MiB.\nUsing the stock background.");
		gprintf("Susamune theme rejected %s: size %llu (max %u)\n", path,
			(unsigned long long)fileInfo.fsize,
			(unsigned int)SUSAMUNE_THEME_BACKGROUND_MAX_SIZE);
		return false;
	}

	result = f_open_char(&file, path, FA_READ | FA_OPEN_EXISTING);
	if (result != FR_OK)
	{
		snprintf(sWarning, sizeof(sWarning),
			"Theme PNG unreadable (I/O %d).\nUsing the stock background.",
			(int)result);
		gprintf("Susamune theme could not open %s (%d)\n", path, (int)result);
		return false;
	}
	result = f_read(&file, header, sizeof(header), &got);
	validHeader = result == FR_OK && got == sizeof(header) &&
		ValidatePngHeader(header, sizeof(header));
	if (validHeader)
		result = f_lseek(&file, 0);
	if (!validHeader || result != FR_OK)
	{
		f_close(&file);
		snprintf(sWarning, sizeof(sWarning),
			"Theme PNG must be a valid 1024x480 PNG.\nUsing the stock background.");
		gprintf("Susamune theme rejected %s: header or I/O error %d\n",
			path, (int)result);
		return false;
	}

	texture = *backgroundPtr;
	if (texture == NULL)
	{
		texture = calloc(1, sizeof(*texture));
		if (texture == NULL)
		{
			f_close(&file);
			ActivateSolidFallback(backgroundPtr);
			snprintf(sWarning, sizeof(sWarning),
				"Theme texture setup failed.\nUsing the stock background.");
			return false;
		}
		*backgroundPtr = texture;
	}
	if (texture == sCustomBackground)
		sCustomBackground = NULL;
	free(texture->data);
	texture->data = NULL;
	LogHeap("before-texture");
	texture->data = memalign(32, THEME_BACKGROUND_BYTES);
	if (texture->data == NULL)
	{
		f_close(&file);
		ActivateSolidFallback(backgroundPtr);
		snprintf(sWarning, sizeof(sWarning),
			"Theme PNG needs 1.9 MiB free.\nUsing the stock background.");
		return false;
	}

	decodeError[0] = '\0';
	if (!DecodePng(&file, texture->data, decodeError, sizeof(decodeError)))
	{
		f_close(&file);
		ActivateSolidFallback(backgroundPtr);
		snprintf(sWarning, sizeof(sWarning),
			"Theme PNG decode failed:\n%.48s\nUsing the stock background.",
			decodeError);
		gprintf("Susamune theme could not decode %s: %s\n", path, decodeError);
		return false;
	}
	f_close(&file);

	texture->w = THEME_BACKGROUND_WIDTH;
	texture->h = THEME_BACKGROUND_HEIGHT;
	GRRLIB_SetHandle(texture, 0, 0);
	GRRLIB_FlushTex(texture);
	sCustomBackground = texture;
	sSolidFallback = false;
	sPanOffset = 0.0f;
	sPanDirection = -1.0f;
	sWarning[0] = '\0';
	LogHeap("custom-background");
	gprintf("Susamune theme loaded %s (%llu bytes)\n", path,
		(unsigned long long)fileInfo.fsize);
	return true;
}

bool SusamuneThemeLoad(const char *launcherDevice, const char *launchDir,
	GRRLIB_texImg **backgroundPtr)
{
	char path[THEME_PATH_MAX];
	bool directoryReady;
	bool loaded = false;

	sWarning[0] = '\0';
	if (backgroundPtr == NULL)
	{
		sSolidFallback = true;
		snprintf(sWarning, sizeof(sWarning),
			"Theme target unavailable.\nUsing the stock background.");
		return false;
	}
	ActivateSolidFallback(backgroundPtr);
	directoryReady = EnsureThemeDirectory(launcherDevice, launchDir);
	LogHeap("before-load");
	if (BuildThemePath(path, sizeof(path), launcherDevice, launchDir,
		"background.png"))
		loaded = LoadBackground(path, backgroundPtr);
	else if (directoryReady)
		snprintf(sWarning, sizeof(sWarning),
			"Theme PNG path is too long.\nUsing the stock background.");
	if (!loaded)
		gprintf("Susamune theme: using procedural stock background\n");
	return loaded;
}

const char *SusamuneThemeWarning(void)
{
	return sWarning;
}

bool SusamuneThemeDrawBackground(u8 alpha, f32 xScale, int xPos)
{
	const float imageWidth = THEME_BACKGROUND_WIDTH * xScale;
	const float screenWidth = (float)rmode->fbWidth;
	float minPan;
	float maxPan;
	float x;
	int stockWidth;
	int stockRight;
	u8 nearAlpha;
	u8 diagonalAlpha;

	if (sSolidFallback)
	{
		if (alpha != 0)
		{
			stockWidth = (int)(640.0f * xScale);
			stockRight = xPos + stockWidth;
			if (xPos > 0)
				GRRLIB_Rectangle(0, 0, xPos, 480,
					RGBA(222, 223, 224, alpha), true);
			if (stockRight < (int)rmode->fbWidth)
				GRRLIB_Rectangle(stockRight, 0,
					rmode->fbWidth - stockRight, 480,
					RGBA(222, 223, 224, alpha), true);
			GRRLIB_Rectangle(xPos, 0, stockWidth, 480,
				RGBA(255, 255, 255, alpha), true);
		}
		return true;
	}
	if (sCustomBackground == NULL || alpha == 0 || imageWidth <= 0.0f)
		return false;
	if (imageWidth > screenWidth)
	{
		maxPan = -(float)xPos;
		minPan = screenWidth - imageWidth - (float)xPos;
		if (sPanOffset < minPan)
			sPanOffset = minPan;
		if (sPanOffset > maxPan)
			sPanOffset = maxPan;
		sPanOffset += sPanDirection * 0.25f;
		if (sPanOffset <= minPan)
		{
			sPanOffset = minPan;
			sPanDirection = 1.0f;
		}
		else if (sPanOffset >= maxPan)
		{
			sPanOffset = maxPan;
			sPanDirection = -1.0f;
		}
	}
	else
	{
		sPanOffset = 0.0f;
	}

	x = (float)xPos + sPanOffset;
	nearAlpha = (u8)((u32)alpha * 48u / 100u);
	diagonalAlpha = (u8)((u32)alpha * 30u / 100u);
	GRRLIB_DrawImg(x, 0, sCustomBackground, 0, xScale, 1, RGBA(80, 80, 80, alpha));
	GRRLIB_DrawImg(x - 1, 0, sCustomBackground, 0, xScale, 1,
		RGBA(80, 80, 80, nearAlpha));
	GRRLIB_DrawImg(x + 1, 0, sCustomBackground, 0, xScale, 1,
		RGBA(80, 80, 80, nearAlpha));
	GRRLIB_DrawImg(x, -1, sCustomBackground, 0, xScale, 1,
		RGBA(80, 80, 80, nearAlpha));
	GRRLIB_DrawImg(x, 1, sCustomBackground, 0, xScale, 1,
		RGBA(80, 80, 80, nearAlpha));
	GRRLIB_DrawImg(x - 1, -1, sCustomBackground, 0, xScale, 1,
		RGBA(80, 80, 80, diagonalAlpha));
	GRRLIB_DrawImg(x + 1, -1, sCustomBackground, 0, xScale, 1,
		RGBA(80, 80, 80, diagonalAlpha));
	GRRLIB_DrawImg(x - 1, 1, sCustomBackground, 0, xScale, 1,
		RGBA(80, 80, 80, diagonalAlpha));
	GRRLIB_DrawImg(x + 1, 1, sCustomBackground, 0, xScale, 1,
		RGBA(80, 80, 80, diagonalAlpha));
	return true;
}

void SusamuneThemeShutdown(GRRLIB_texImg **backgroundPtr)
{
	if (sCustomBackground != NULL)
	{
		if (backgroundPtr != NULL && *backgroundPtr == sCustomBackground)
			*backgroundPtr = NULL;
		GRRLIB_FreeTexture(sCustomBackground);
		sCustomBackground = NULL;
	}
	sSolidFallback = false;
	LogHeap("shutdown");
}
