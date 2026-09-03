#include "SusamuneCrash.h"

#include "SusamuneCfg.h"
#include "ff_utf8.h"
#include "string.h"
#include "susamune/crash_report.h"
#include "susamune/mod_bin.h"
#include "vsprintf.h"

extern u32 GAME_ID;
extern int dbgprintf(const char *fmt, ...);

static struct SusamuneCrashReport Snapshot;
static char BinPaths[2][64];
static char TextPaths[2][64];
static char Line[256];
static u32 AttemptedSeq;
static u32 LastPoll;
static bool CrashEnabled;
static FIL TextFile;
static int TextStatus;

static u32 CrashChecksum(const struct SusamuneCrashReport *report)
{
	const u8 *bytes = (const u8*)report;
	u32 crc = 0xFFFFFFFFu;
	u32 i, bit;
	for (i = 0; i < sizeof(*report); ++i)
	{
		bool checksumByte =
			i >= __builtin_offsetof(struct SusamuneCrashReport, checksum) &&
			i < __builtin_offsetof(struct SusamuneCrashReport, checksum) + 4;
		crc ^= checksumByte ? 0u : bytes[i];
		for (bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return crc ^ 0xFFFFFFFFu;
}

static bool ValidReport(const struct SusamuneCrashReport *report)
{
	return report->magic == SUSAMUNE_CRASH_MAGIC &&
		report->version == SUSAMUNE_CRASH_VERSION &&
		report->reportSize == sizeof(*report) &&
		report->state == SUSAMUNE_CRASH_STATE_READY &&
		report->breadcrumbCount <= SUSAMUNE_CRASH_BREADCRUMB_COUNT &&
		report->stackSize <= SUSAMUNE_CRASH_STACK_SIZE &&
		report->pcWindowSize <= SUSAMUNE_CRASH_CODE_WINDOW_SIZE &&
		report->lrWindowSize <= SUSAMUNE_CRASH_CODE_WINDOW_SIZE &&
		report->directorWindowSize <= SUSAMUNE_CRASH_DIRECTOR_SIZE &&
		report->marioWindowSize <= SUSAMUNE_CRASH_MARIO_SIZE &&
		report->checksum == CrashChecksum(report);
}

static bool GenerationNewer(u32 candidate, u32 current)
{
	return (s32)(candidate - current) > 0;
}

static bool ReadReport(const char *path, struct SusamuneCrashReport *report)
{
	FIL file;
	UINT read = 0;
	int ret = f_open_char(&file, path, FA_READ | FA_OPEN_EXISTING);
	int closeRet;
	if (ret != FR_OK)
		return false;
	ret = f_read(&file, report, sizeof(*report), &read);
	closeRet = f_close(&file);
	return ret == FR_OK && closeRet == FR_OK && read == sizeof(*report) &&
		ValidReport(report);
}

static u32 ModFileSize(const struct SusamuneModHeader *header)
{
	return SUSAMUNE_MOD_HEADER_SIZE + header->codeSize +
		header->writeCount * 8;
}

static u32 ModFileCrc(const struct SusamuneModHeader *header, u32 fileSize)
{
	const u8 *bytes = (const u8*)header;
	u32 crc = 0xFFFFFFFFu;
	u32 i, bit;
	for (i = 0; i < fileSize; ++i)
	{
		crc ^= bytes[i];
		for (bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
	}
	return crc ^ 0xFFFFFFFFu;
}

static bool ValidStagedMod(const struct SusamuneModHeader *header)
{
	u32 codeEnd;
	sync_before_read((void*)header, SUSAMUNE_MOD_HEADER_SIZE);
	if (header->magic != SUSAMUNE_MOD_MAGIC ||
		header->version != SUSAMUNE_MOD_VERSION || header->gameId != GAME_ID ||
		header->baseAddr != SUSAMUNE_MOD_BASE_FOR_GAME_ID(GAME_ID) ||
		header->arenaReserve != SUSAMUNE_ARENA_RESERVE_SIZE ||
		header->codeSize > header->memSize ||
		header->memSize > SUSAMUNE_MOD_BLOB_MAX_SIZE ||
		header->codeSize >
			SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE -
			SUSAMUNE_MOD_HEADER_SIZE ||
		(header->codeSize & 3) || (header->memSize & 3))
		return false;
	codeEnd = SUSAMUNE_MOD_HEADER_SIZE + header->codeSize;
	if (header->writeCount >
		(SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE - codeEnd) / 8)
		return false;
	return ModFileSize(header) <= SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE;
}

void SusamuneCrashInit(void)
{
	struct SusamuneCrashReport *mailbox = SUSAMUNE_CRASH_PHYS_PTR;
	const struct SusamuneModHeader *mod = SUSAMUNE_MOD_PHYS_PTR;
	u32 generation = 0;
	u32 modFileSize;
	u32 i;

	memset(mailbox, 0, sizeof(*mailbox));
	CrashEnabled = SusamuneCfgStorageAvailable();
	AttemptedSeq = 0;
	LastPoll = read32(HW_TIMER);
	_sprintf(BinPaths[0], "%s/susamune_crash_a.bin",
		SusamuneCfgStoragePrefix());
	_sprintf(BinPaths[1], "%s/susamune_crash_b.bin",
		SusamuneCfgStoragePrefix());
	_sprintf(TextPaths[0], "%s/susamune_crash_a.txt",
		SusamuneCfgStoragePrefix());
	_sprintf(TextPaths[1], "%s/susamune_crash_b.txt",
		SusamuneCfgStoragePrefix());

	if (CrashEnabled)
	{
		for (i = 0; i < 2; ++i)
			if (ReadReport(BinPaths[i], &Snapshot) &&
				GenerationNewer(Snapshot.captureSeq, generation))
				generation = Snapshot.captureSeq;
	}

	mailbox->magic = SUSAMUNE_CRASH_MAGIC;
	mailbox->version = SUSAMUNE_CRASH_VERSION;
	mailbox->reportSize = sizeof(*mailbox);
	mailbox->state = CrashEnabled ? SUSAMUNE_CRASH_STATE_ARMED :
		SUSAMUNE_CRASH_STATE_DISABLED;
	mailbox->captureSeq = generation + 1;
	if (mailbox->captureSeq == 0)
		mailbox->captureSeq = 1;
	mailbox->gameId = GAME_ID;
	if (ValidStagedMod(mod))
	{
		modFileSize = ModFileSize(mod);
		sync_before_read((void*)mod, modFileSize);
		mailbox->modFileCrc32 = ModFileCrc(mod, modFileSize);
		mailbox->modFileSize = modFileSize;
		mailbox->modCodeSize = mod->memSize;
		mailbox->modWriteCount = mod->writeCount;
		mailbox->arenaReserve = mod->arenaReserve;
	}
	sync_after_write(mailbox, sizeof(*mailbox));
}

bool SusamuneCrashPending(void)
{
	struct SusamuneCrashReport *mailbox = SUSAMUNE_CRASH_PHYS_PTR;
	if (!CrashEnabled)
		return false;
	if (TimerDiffTicks(LastPoll) < 15820)
		return false;
	LastPoll = read32(HW_TIMER);
	sync_before_read(mailbox, 32);
	return mailbox->magic == SUSAMUNE_CRASH_MAGIC &&
		mailbox->version == SUSAMUNE_CRASH_VERSION &&
		mailbox->reportSize == sizeof(*mailbox) &&
		mailbox->state == SUSAMUNE_CRASH_STATE_READY &&
		mailbox->captureSeq != AttemptedSeq;
}

static const char *ExceptionName(u32 exception)
{
	static const char *names[] = {
		"System reset", "Machine check", "DSI", "ISI",
		"External interrupt", "Alignment", "Program",
		"Floating point unavailable", "Decrementer", "System call",
		"Trace", "Performance monitor", "IABR", "Reserved", "Thermal"
	};
	return exception < sizeof(names) / sizeof(names[0]) ? names[exception] :
		"Unknown";
}

static const char *RegionName(u32 gameId)
{
	if (gameId == SUSAMUNE_MOD_GAME_ID_JP)
		return "JP/GMSJ";
	if (gameId == SUSAMUNE_MOD_GAME_ID_US)
		return "US/GMSE";
	if (gameId == SUSAMUNE_MOD_GAME_ID_PAL)
		return "PAL/GMSP";
	return "unknown";
}

static void Emit(const char *text, u32 length)
{
	UINT wrote = 0;
	if (TextStatus != FR_OK)
		return;
	TextStatus = f_write(&TextFile, text, length, &wrote);
	if (TextStatus == FR_OK && wrote != length)
		TextStatus = FR_DISK_ERR;
}

static void EmitString(const char *text)
{
	Emit(text, strlen(text));
}

static void EmitHex(const char *label, u32 base, const u8 *data, u32 size)
{
	u32 offset, i;
	Emit(Line, _sprintf(Line, "%s base=%08X size=%u\r\n", label, base, size));
	for (offset = 0; offset < size; offset += 16)
	{
		u32 count = size - offset < 16 ? size - offset : 16;
		u32 length = _sprintf(Line, "%08X:", base + offset);
		for (i = 0; i < count; ++i)
			length += _sprintf(Line + length, " %02X", data[offset + i]);
		length += _sprintf(Line + length, "\r\n");
		Emit(Line, length);
	}
}

static int WriteText(u32 target)
{
	u32 i, start;
	int closeRet;
	TextStatus = f_open_char(&TextFile, TextPaths[target],
		FA_WRITE | FA_CREATE_ALWAYS);
	if (TextStatus != FR_OK)
		return TextStatus;

	Emit(Line, _sprintf(Line, "Moonshine crash report v%u\r\n",
		Snapshot.version));
	Emit(Line, _sprintf(Line,
		"generation=%u region=%s game_id=%08X mod_crc32=%08X\r\n",
		Snapshot.captureSeq, RegionName(Snapshot.gameId), Snapshot.gameId,
		Snapshot.modFileCrc32));
	Emit(Line, _sprintf(Line,
		"mod_file_size=%u mod_code_size=%u hook_writes=%u arena_reserve=%u\r\n",
		Snapshot.modFileSize, Snapshot.modCodeSize, Snapshot.modWriteCount,
		Snapshot.arenaReserve));
	Emit(Line, _sprintf(Line,
		"exception=%u (%s) flags=%04X dsisr=%08X dar=%08X\r\n",
		Snapshot.exception, ExceptionName(Snapshot.exception),
		Snapshot.captureFlags, Snapshot.dsisr, Snapshot.dar));
	Emit(Line, _sprintf(Line,
		"srr0=%08X srr1=%08X lr=%08X cr=%08X ctr=%08X xer=%08X\r\n",
		Snapshot.srr0, Snapshot.srr1, Snapshot.lr, Snapshot.cr,
		Snapshot.ctr, Snapshot.xer));
	Emit(Line, _sprintf(Line,
		"time_base=%08X%08X context_mode=%u context_state=%u thread=%08X\r\n",
		Snapshot.timeBaseHigh, Snapshot.timeBaseLow, Snapshot.contextMode,
		Snapshot.contextState, Snapshot.currentThread));

	for (i = 0; i < 32; i += 4)
		Emit(Line, _sprintf(Line,
			"r%02u=%08X r%02u=%08X r%02u=%08X r%02u=%08X\r\n",
			i, Snapshot.gpr[i], i + 1, Snapshot.gpr[i + 1],
			i + 2, Snapshot.gpr[i + 2], i + 3, Snapshot.gpr[i + 3]));

	Emit(Line, _sprintf(Line,
		"app=%08X director=%08X heap=%08X context=%u cutscene=%08X\r\n",
		Snapshot.appAddress, Snapshot.appDirector, Snapshot.appHeap,
		Snapshot.appContext, Snapshot.cutSceneId));
	Emit(Line, _sprintf(Line,
		"scenes prev=%02X/%02X/%04X current=%02X/%02X/%04X next=%02X/%02X/%04X\r\n",
		Snapshot.prevScene >> 24, Snapshot.prevScene >> 16 & 0xFF,
		Snapshot.prevScene & 0xFFFF, Snapshot.currentScene >> 24,
		Snapshot.currentScene >> 16 & 0xFF, Snapshot.currentScene & 0xFFFF,
		Snapshot.nextScene >> 24, Snapshot.nextScene >> 16 & 0xFF,
		Snapshot.nextScene & 0xFFFF));
	Emit(Line, _sprintf(Line,
		"globals mar_director=%08X mario=%08X camera=%08X ready=%u state_area_episode=%08X\r\n",
		Snapshot.marDirector, Snapshot.mario, Snapshot.camera,
		Snapshot.directorReady, Snapshot.directorStateAreaEpisode));
	Emit(Line, _sprintf(Line,
		"director game_state=%08X demo_states=%08X collected_shine=%08X\r\n",
		Snapshot.directorGameState, Snapshot.directorDemoStates,
		Snapshot.directorCollectedShine));

	EmitString("\r\nBreadcrumbs (event 1=app, 2=context, 3=setup-enter, "
		"4=setup-return, 5=stage-ready):\r\n");
	start = Snapshot.breadcrumbSeq - Snapshot.breadcrumbCount;
	for (i = 0; i < Snapshot.breadcrumbCount &&
		i < SUSAMUNE_CRASH_BREADCRUMB_COUNT; ++i)
	{
		const struct SusamuneCrashBreadcrumb *entry =
			&Snapshot.breadcrumbs[(start + i) %
				SUSAMUNE_CRASH_BREADCRUMB_COUNT];
		Emit(Line, _sprintf(Line, "%02u event=%u time=%08X%08X arg0=%08X arg1=%08X\r\n",
			i, entry->event, entry->timeBaseHigh, entry->timeBaseLow,
			entry->arg0, entry->arg1));
	}

	EmitString("\r\nBacktrace:\r\n");
	for (i = 0; i < SUSAMUNE_CRASH_BACKTRACE_COUNT; ++i)
	{
		const struct SusamuneCrashFrame *frame = &Snapshot.backtrace[i];
		if (frame->stackPointer == 0)
			break;
		Emit(Line, _sprintf(Line, "%02u sp=%08X lr=%08X\r\n", i,
			frame->stackPointer, frame->returnAddress));
	}

	EmitString("\r\nMemory windows:\r\n");
	EmitHex("stack", Snapshot.stackBase, Snapshot.stack, Snapshot.stackSize);
	EmitHex("pc", Snapshot.pcWindowBase, Snapshot.pcWindow,
		Snapshot.pcWindowSize);
	EmitHex("lr", Snapshot.lrWindowBase, Snapshot.lrWindow,
		Snapshot.lrWindowSize);
	EmitHex("director", Snapshot.directorWindowBase, Snapshot.directorWindow,
		Snapshot.directorWindowSize);
	EmitHex("mario", Snapshot.marioWindowBase, Snapshot.marioWindow,
		Snapshot.marioWindowSize);

	if (TextStatus == FR_OK)
		TextStatus = f_sync(&TextFile);
	closeRet = f_close(&TextFile);
	if (TextStatus == FR_OK && closeRet != FR_OK)
		TextStatus = closeRet;
	return TextStatus;
}

void SusamuneCrashService(void)
{
	struct SusamuneCrashReport *mailbox = SUSAMUNE_CRASH_PHYS_PTR;
	FIL file;
	UINT wrote = 0;
	u32 target;
	int ret, closeRet;

	sync_before_read(mailbox, sizeof(*mailbox));
	memcpy(&Snapshot, mailbox, sizeof(Snapshot));
	AttemptedSeq = Snapshot.captureSeq;
	if (!ValidReport(&Snapshot))
	{
		dbgprintf("Susamune: rejected invalid crash capture\r\n");
		return;
	}

	target = Snapshot.captureSeq & 1u;
	ret = f_open_char(&file, BinPaths[target], FA_WRITE | FA_CREATE_ALWAYS);
	if (ret == FR_OK)
	{
		ret = f_write(&file, &Snapshot, sizeof(Snapshot), &wrote);
		if (ret == FR_OK && wrote != sizeof(Snapshot))
			ret = FR_DISK_ERR;
		if (ret == FR_OK)
			ret = f_sync(&file);
		closeRet = f_close(&file);
		if (ret == FR_OK && closeRet != FR_OK)
			ret = closeRet;
	}
	if (ret != FR_OK)
	{
		dbgprintf("Susamune: crash binary write failed (%d)\r\n", ret);
		return;
	}

	ret = WriteText(target);
	if (ret != FR_OK)
		dbgprintf("Susamune: crash text write failed (%d)\r\n", ret);
	else
		dbgprintf("Susamune: crash report saved (%u, %s)\r\n",
			Snapshot.captureSeq, TextPaths[target]);
}
