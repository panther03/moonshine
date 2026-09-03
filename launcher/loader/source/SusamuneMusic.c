#include <asndlib.h>
#include <mp3player.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "ff_utf8.h"
#include "exi.h"
#include "global.h"
#include "susamune/mem2_map.h"
#include "SusamuneMp3Validate.h"
#include "SusamuneMusic.h"

#define MUSIC_PATH_MAX    512u
#define MUSIC_WARNING_MAX 160u
#define MUSIC_BUFFER      ((void *)SUSAMUNE_LAUNCHER_MUSIC_PPC_BASE)

static s32 sBufferSize;
static bool sAudioReady;
static bool sPlaying;
static char sWarning[MUSIC_WARNING_MAX];

#if SUSAMUNE_THEME_BGM_MAX_SIZE > SUSAMUNE_LAUNCHER_MUSIC_SIZE
#error "Theme MP3 cap exceeds its transient MEM2 window"
#endif

static void ReleaseMusicBuffer(u32 span)
{
	if (span != 0)
		DCInvalidateRange(MUSIC_BUFFER, span);
	sBufferSize = 0;
}

static bool BuildMusicPath(char *out, size_t outSize, const char *device,
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
	if (dir[0] != '/' || strchr(dir, ':') != NULL ||
	    strchr(dir, '\\') != NULL || strstr(dir, "../") != NULL ||
	    strstr(dir, "/..") != NULL)
		return false;

	dirLen = strlen(dir);
	written = snprintf(out, outSize, "%s:%s%stheme/bgm.mp3", device, dir,
		(dirLen > 0 && dir[dirLen - 1] == '/') ? "" : "/");
	return written > 0 && (size_t)written < outSize;
}

static void LogHeap(const char *where)
{
	const struct mallinfo info = mallinfo();

	gprintf("Susamune music heap %s: live=%u free=%u mem2_buffer=%u\n", where,
		(unsigned int)info.uordblks, (unsigned int)info.fordblks,
		(unsigned int)(sBufferSize > 0 ? sBufferSize : 0));
}

void SusamuneMusicInit(void)
{
	if (sAudioReady)
		return;
	// ASND must own the one-time AUDIO/DSP setup; a prior manual init breaks it.
	ASND_Init();
	MP3Player_Init();
	sAudioReady = true;
	gprintf("Susamune music: ASND/MP3 ready; playback deferred\n");
}

bool SusamuneMusicLoad(const char *launcherDevice, const char *launchDir)
{
	char path[MUSIC_PATH_MAX];
	FILINFO info;
	FIL file;
	FRESULT result;
	UINT got = 0;
	u32 allocationSize;

	sWarning[0] = '\0';
	if (sBufferSize > 0)
		return true;
	if (!BuildMusicPath(path, sizeof(path), launcherDevice, launchDir))
	{
		snprintf(sWarning, sizeof(sWarning),
			"Theme music path is invalid.\nContinuing without music.");
		return false;
	}
	result = f_stat_char(path, &info);
	if (result == FR_NO_FILE || result == FR_NO_PATH)
		return false;
	if (result != FR_OK)
	{
		snprintf(sWarning, sizeof(sWarning),
			"Theme MP3 unreadable (I/O %d).\nContinuing without music.",
			(int)result);
		return false;
	}
	if (info.fsize < 8u || info.fsize > SUSAMUNE_THEME_BGM_MAX_SIZE)
	{
		snprintf(sWarning, sizeof(sWarning),
			"Theme MP3 is empty or over 4 MiB.\nContinuing without music.");
		gprintf("Susamune music rejected %s: size %llu (max %u)\n", path,
			(unsigned long long)info.fsize,
			(unsigned int)SUSAMUNE_THEME_BGM_MAX_SIZE);
		return false;
	}

	allocationSize = ((u32)info.fsize + 31u) & ~31u;
	// FatFS may use CPU copies or DMA; start uncached and publish either path.
	DCInvalidateRange(MUSIC_BUFFER, allocationSize);
	result = f_open_char(&file, path, FA_READ | FA_OPEN_EXISTING);
	if (result == FR_OK)
	{
		result = f_read(&file, MUSIC_BUFFER, (UINT)info.fsize, &got);
		f_close(&file);
	}
	DCFlushRange(MUSIC_BUFFER, allocationSize);
	if (result != FR_OK || got != (UINT)info.fsize)
	{
		ReleaseMusicBuffer(allocationSize);
		snprintf(sWarning, sizeof(sWarning),
			"Theme MP3 read failed (I/O %d).\nContinuing without music.",
			(int)result);
		return false;
	}
	sBufferSize = (s32)info.fsize;
	if (!SusamuneMp3Validate((const u8 *)MUSIC_BUFFER, (u32)sBufferSize))
	{
		ReleaseMusicBuffer(allocationSize);
		snprintf(sWarning, sizeof(sWarning),
			"Theme BGM is not a valid MP3.\nContinuing without music.");
		gprintf("Susamune music rejected %s: no consecutive MP3 frames\n", path);
		return false;
	}
	LogHeap("staged");
	gprintf("Susamune music staged %s (%u bytes)\n", path,
		(unsigned int)sBufferSize);
	return true;
}

bool SusamuneMusicStart(void)
{
	if (sPlaying)
		return true;
	if (sBufferSize <= 0)
		return false;

	// The IOS reload and Nintendont kernel setup can interrupt the decoder.
	// The caller waits until both are stable, giving it one lifetime per launch.
	if (!sAudioReady ||
	    MP3Player_PlayBuffer(MUSIC_BUFFER, sBufferSize, NULL) < 0)
	{
		snprintf(sWarning, sizeof(sWarning),
			"Theme MP3 could not start.\nContinuing without music.");
		gprintf("Susamune music: delayed start failed\n");
		return false;
	}
	sPlaying = true;
	gprintf("Susamune music: playback started\n");
	return true;
}

void SusamuneMusicService(void)
{
	if (!sPlaying || sBufferSize <= 0 || MP3Player_IsPlaying())
		return;
	if (MP3Player_PlayBuffer(MUSIC_BUFFER, sBufferSize, NULL) < 0)
	{
		sPlaying = false;
		gprintf("Susamune music: loop restart failed\n");
	}
}

const char *SusamuneMusicWarning(void)
{
	return sWarning;
}

void SusamuneMusicShutdown(void)
{
	if (sAudioReady)
	{
		const bool shouldStop = sPlaying || MP3Player_IsPlaying();

		// Disable looping before waiting on the decoder.
		sPlaying = false;
		// MP3Player_Stop joins its live decoder before ASND and the input vanish.
		if (shouldStop)
			MP3Player_Stop();
		ASND_End();
		sAudioReady = false;
	}
	if (sBufferSize > 0)
		ReleaseMusicBuffer(((u32)sBufferSize + 31u) & ~31u);
	LogHeap("shutdown");
}
