/*

Susamune settings persistence (Nintendont kernel side).

The mod cannot touch the SD card: file I/O lives on the ARM, and the PPC only
sees the emulated GameCube hardware. So settings move through the MEM2 handoff
block described in susamune_cfg.h:

  boot  -- SusamuneCfgInit() parses /susamune.ini into the block, before the
           game is patched and long before the first frame.
  save  -- the mod stages values into the block and bumps saveSeq;
           SusamuneCfgService() notices, rewrites the ini, and answers through
           status + ackSeq.

The ini is plain text. [settings_<region>] holds the in-game options, keyed by
the stable names in settings_list.h -- shared with the mod, so the key table
here cannot drift from the mod's SettingId order. [binds_<region>] holds one
button combination per configurable action, keyed by binds_list.h and written as
`+`-joined button tokens ("X+DUp") from the same shared list.
[input_display_<region>] holds its wider position/colour configuration.
[metadata_display_<region>] holds the native metadata overlay, including its
optional hand-authored text template. [qft_display_<region>] holds the compact
QFT readout's Creation style. [creation_<region>] holds native HUD
colours and the three custom text overlays.
[nintendont] holds
the launcher's own options -- game version, per-version disc image paths, and
the Nintendont settings that used to live in nincfg.bin -- and belongs to
the loader (SusamuneIni.c); this side only copies it through.

The file lives on the device the launcher was run from, which is not
necessarily the one the game is read from; SusamuneCfgIniPath() (main.c) names
the drive it ended up on.

ILing PBs use a separate fixed binary journal on that same device. Two files
per region alternate generations so an interrupted write leaves one valid
copy, and their independent doorbell avoids rewriting the ini after every PB.
Achievements and statistics use another two-generation journal with no region
suffix. Its achievement bits are global, while its three stat banks preserve
JP/US/PAL values for both regional and combined totals.

One launcher now serves GMSJ/GMSE/GMSP, and each keeps its own settings and
binds, hence the region tag on the section names. Only the running version's
sections are ever parsed: the handoff block holds one set of values, not three.
Consequently a save cannot simply re-emit the file from the block -- it would
drop the other versions. Instead it reads the old file back and copies every
line through verbatim, substituting freshly written sections in place of this
version's five. Sections the launcher does not know stay byte-for-byte intact.

NOTE: keys the launcher does not recognise *inside our own five sections* are
still dropped, since those sections are regenerated wholesale.

*/

#include "SusamuneCfg.h"
#include "string.h"
#include "alloc.h"
#include "debug.h"
#include "ff_utf8.h"

#include "susamune/susamune_cfg.h"
#include "susamune/mod_bin.h"

// Set by DIinit() from the disc header; SusamuneCfgInit() runs after it.
extern u32 GAME_ID;

// The ini key table, generated from the same list that defines the mod's
// SettingId enum. Index == SettingId == index into SusamuneCfg::values.
#define SUSAMUNE_SETTING_KEY(id, key) key,
static const char *const SettingKeys[] = { SUSAMUNE_SETTING_LIST(SUSAMUNE_SETTING_KEY) };
#undef SUSAMUNE_SETTING_KEY

#define SETTING_KEY_COUNT ((u32)(sizeof(SettingKeys) / sizeof(SettingKeys[0])))
typedef char SettingKeyCountFitsCfg[
    SETTING_KEY_COUNT <= SUSAMUNE_CFG_MAX_SETTINGS ? 1 : -1];

// Same, for the running disc's [binds_<region>] section.
#define SUSAMUNE_BIND_KEY(id, key) key,
static const char *const BindKeys[] = { SUSAMUNE_BIND_LIST(SUSAMUNE_BIND_KEY) };
#undef SUSAMUNE_BIND_KEY

#define BIND_KEY_COUNT ((u32)(sizeof(BindKeys) / sizeof(BindKeys[0])))
typedef char BindKeyCountFitsCfg[
    BIND_KEY_COUNT <= SUSAMUNE_CFG_MAX_BINDS ? 1 : -1];

// Button bit <-> ini token. The third list field is the mod's font glyph.
struct BindButton { u16 bit; const char *token; };
#define SUSAMUNE_BIND_BUTTON_ROW(bit, token, display) { (u16)(bit), token },
static const struct BindButton BindButtons[] = { SUSAMUNE_BIND_BUTTON_LIST(SUSAMUNE_BIND_BUTTON_ROW) };
#undef SUSAMUNE_BIND_BUTTON_ROW

#define BIND_BUTTON_COUNT ((u32)(sizeof(BindButtons) / sizeof(BindButtons[0])))

static const char *const InputColorKeys[SUSAMUNE_INPUT_COLOR_COUNT] =
{
	"main_stick_rgb", "c_stick_rgb", "a_rgb", "b_rgb", "x_rgb", "y_rgb",
	"l_rgb", "r_rgb", "start_rgb", "z_rgb", "value_rgb",
	"trigger_outline_rgb"
};

static const char *const CreationColorKeys[SUSAMUNE_CREATION_COLOR_COUNT] =
{
	// Slot zero is retained so older Creation payloads keep their layout.
	"water_text_rgb", "fludd_tank_rgb", "timer_streak_rgb",
	"coin_streak_rgb", "red_streak_rgb", "blue_streak_rgb",
	"lives_streak_rgb", "shines_streak_rgb", "life_counter_rgb",
	"timer_normal_1_rgb", "timer_normal_2_rgb", "timer_normal_3_rgb",
	"timer_normal_4_rgb", "timer_normal_5_rgb", "timer_normal_6_rgb",
	"timer_rush_1_rgb", "timer_rush_2_rgb", "timer_rush_3_rgb",
	"timer_rush_4_rgb", "timer_separator_1_rgb", "timer_separator_2_rgb",
	"timer_separator_3_rgb", "timer_label_rgb", "mario_hat_rgb",
	"menu_background_rgb"
};

// Enough for the whole file: the settings plus display payloads for all
// three versions, section headers, and the comment banner.
// A file larger than this is refused rather than truncated (see WriteIniFile).
#define SUSAMUNE_INI_BUF_SIZE 49152

// Longest section name we build: "settings" + '_' + "pal" + NUL.
#define SUSAMUNE_SECTION_NAME_MAX 24

static bool CfgReady = false;
static u32  CfgAckSeq = 0;

#define SUSAMUNE_PB_FILE_COUNT 2
#define SUSAMUNE_PB_PATH_SIZE  40

static u32 PbAckSeq = 0;
static u32 PbGeneration = 0;
static s32 PbActiveFile = -1;
static bool PbReady = false;
static char PbPaths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];

static u32 ProgressAckSeq = 0;
static u32 ProgressGeneration = 0;
static s32 ProgressActiveFile = -1;
static bool ProgressReady = false;
static char ProgressPaths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
static u32 StagePlaylistAckSeq = 0;
static u32 StagePlaylistGeneration = 0;
static s32 StagePlaylistActiveFile = -1;
static bool StagePlaylistReady = false;
static char StagePlaylistPaths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
static char StagePlaylistV1Paths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
static u32 StagePlaylistCommittedRevisions[SUSAMUNE_STAGE_PLAYLIST_COUNT];
static u32 StagePlaylistCommittedHashes[SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT];
static u16 StagePlaylistCommittedBestMask = 0;
static u32 StageTargetAckSeq = 0;
static u32 StageTargetGeneration = 0;
static s32 StageTargetActiveFile = -1;
static bool StageTargetReady = false;
static char StageTargetPaths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
static u32 SplitStatsAckSeq = 0;
static u32 SplitStatsGeneration = 0;
static s32 SplitStatsActiveFile = -1;
static bool SplitStatsReady = false;
static char SplitStatsPaths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
static union
{
	struct SusamuneSplitStatsFile current;
	struct SusamuneSplitStatsFileV6 v6;
	struct SusamuneSplitStatsFileV5 v5;
	struct SusamuneSplitStatsFileV4 v4;
	struct SusamuneSplitStatsFileV3 v3;
	struct SusamuneSplitStatsFileV2 v2;
	struct SusamuneSplitStatsFileV1 v1;
} SplitStatsFileScratch;
static struct SusamuneSplitStatsPayloadV2 SplitStatsV2Selected;
static struct SusamuneSplitStatsPayloadV1 SplitStatsV1Selected;

enum PbReadResult
{
	PB_READ_INVALID,
	PB_READ_VALID,
	PB_READ_UNSAFE
};

// "[settings_jp]" and friends for the running disc, built once in
// SusamuneCfgInit. Empty for a game we have no mod for, which is also what
// leaves CfgReady false.
static char SettingsSection[SUSAMUNE_SECTION_NAME_MAX];
static char BindsSection[SUSAMUNE_SECTION_NAME_MAX];
static char InputDisplaySection[SUSAMUNE_SECTION_NAME_MAX];
static char MetadataDisplaySection[SUSAMUNE_SECTION_NAME_MAX];
static char QftDisplaySection[SUSAMUNE_SECTION_NAME_MAX];
static char CreationSection[SUSAMUNE_SECTION_NAME_MAX];

// name + '_' + region tag, e.g. "settings" + "jp".
static void BuildSectionName(char *out, const char *base, const char *region)
{
	u32 n = 0;

	while (*base)
		out[n++] = *base++;
	out[n++] = SUSAMUNE_INI_SECTION_SEPARATOR;
	while (*region)
		out[n++] = *region++;
	out[n] = '\0';
}

static struct SusamuneCfg *CfgBlock(void)
{
	return SUSAMUNE_CFG_PHYS_PTR;
}

static struct SusamuneProgressCfg *ProgressBlock(void)
{
	return SUSAMUNE_PROGRESS_PHYS_PTR;
}

static struct SusamuneStagePlaylistsCfg *StagePlaylistBlock(void)
{
	return SUSAMUNE_STAGE_PLAYLIST_PHYS_PTR;
}

static struct SusamuneStageTargetsCfg *StageTargetBlock(void)
{
	return SUSAMUNE_STAGE_TARGETS_PHYS_PTR;
}

static struct SusamuneSplitStatsCfg *SplitStatsBlock(void)
{
	return SUSAMUNE_SPLIT_STATS_PHYS_PTR;
}

// ---------------------------------------------------------------------
// ILing PB binary files
// ---------------------------------------------------------------------

static u32 PbHashWord(u32 hash, u32 value)
{
	return (hash ^ value) * 16777619u;
}

static u32 PbChecksum(const struct SusamuneILingPbFile *file)
{
	u32 hash = 2166136261u;
	u32 i;

	hash = PbHashWord(hash, ((u32)file->version << 16) | file->count);
	hash = PbHashWord(hash, file->gameId);
	hash = PbHashWord(hash, file->generation);
	for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
		hash = PbHashWord(hash, (u32)file->values[i]);
	return hash;
}

static bool PbGenerationIsNewer(u32 candidate, u32 current)
{
	return (s32)(candidate - current) > 0;
}

static bool PbValueIsValid(s32 value)
{
	return value >= SUSAMUNE_ILING_PB_UNSET &&
	       value <= SUSAMUNE_ILING_PB_MAX_QF;
}

static enum PbReadResult ReadPbFile(const char *path,
	                               struct SusamuneILingPbFile *file)
{
	FIL f;
	UINT read = 0;
	u32 i;
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
	{
		dbgprintf("Susamune: could not read PB file %s (%d)\r\n", path, ret);
		return PB_READ_UNSAFE;
	}

	if (f_size(&f) != sizeof(*file))
	{
		closeRet = f_close(&f);
		if (closeRet != FR_OK)
			return PB_READ_UNSAFE;
		dbgprintf("Susamune: ignored invalid PB file %s (size)\r\n", path);
		return PB_READ_INVALID;
	}

	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
	{
		dbgprintf("Susamune: ignored unreadable PB file %s\r\n", path);
		return PB_READ_UNSAFE;
	}

	if (file->magic == SUSAMUNE_ILING_PB_FILE_MAGIC &&
	    file->version != SUSAMUNE_ILING_PB_VERSION)
	{
		dbgprintf("Susamune: unsupported PB file %s (version %u)\r\n",
		          path, file->version);
		return PB_READ_UNSAFE;
	}

	if (file->magic != SUSAMUNE_ILING_PB_FILE_MAGIC ||
	    file->count == 0 || file->count > SUSAMUNE_ILING_PB_MAX_SLOTS ||
	    file->gameId != GAME_ID || file->checksum != PbChecksum(file))
	{
		dbgprintf("Susamune: ignored invalid PB file %s (header)\r\n", path);
		return PB_READ_INVALID;
	}

	for (i = 0; i < file->count; i++)
	{
		if (!PbValueIsValid(file->values[i]))
		{
			dbgprintf("Susamune: ignored invalid PB file %s (value)\r\n", path);
			return PB_READ_INVALID;
		}
	}
	return PB_READ_VALID;
}

static u32 PbProfilesChecksum(const struct SusamuneILingProfilesFile *file)
{
	u32 hash = 2166136261u;
	u32 p;
	u32 i;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->profileCount << 8) |
	                         file->activeProfile);
	hash = PbHashWord(hash, ((u32)file->slotCount << 16) | file->nameSize);
	hash = PbHashWord(hash, file->gameId);
	hash = PbHashWord(hash, file->generation);
	for (p = 0; p < SUSAMUNE_ILING_PROFILE_COUNT; p++)
		for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
			hash = PbHashWord(hash, (u32)file->values[p][i]);
	for (p = 0; p < SUSAMUNE_ILING_CUSTOM_NAME_COUNT; p++)
		for (i = 0; i < SUSAMUNE_ILING_PROFILE_NAME_SIZE; i++)
			hash = (hash ^ (u8)file->customNames[p][i]) * 16777619u;
	return hash;
}

static bool PbProfileNameIsValid(const char *name)
{
	u32 i;
	for (i = 0; i < SUSAMUNE_ILING_PROFILE_NAME_SIZE; i++)
	{
		const u8 c = (u8)name[i];
		if (c == 0)
			return true;
		if (c < 0x20 || c > 0x7E)
			return false;
	}
	return false;
}

static enum PbReadResult ReadPbProfilesFile(
	const char *path, struct SusamuneILingProfilesFile *file)
{
	FIL f;
	UINT read = 0;
	u32 p;
	u32 i;
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	if (f_size(&f) != sizeof(*file))
	{
		closeRet = f_close(&f);
		return closeRet == FR_OK ? PB_READ_INVALID : PB_READ_UNSAFE;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;

	if (file->magic == SUSAMUNE_ILING_PROFILE_FILE_MAGIC &&
	    file->version != SUSAMUNE_ILING_PROFILE_VERSION)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_ILING_PROFILE_FILE_MAGIC ||
	    file->profileCount != SUSAMUNE_ILING_PROFILE_COUNT ||
	    file->activeProfile >= SUSAMUNE_ILING_PROFILE_COUNT ||
	    file->slotCount == 0 ||
	    file->slotCount > SUSAMUNE_ILING_PB_MAX_SLOTS ||
	    file->nameSize != SUSAMUNE_ILING_PROFILE_NAME_SIZE ||
	    file->gameId != GAME_ID ||
	    file->checksum != PbProfilesChecksum(file))
		return PB_READ_INVALID;

	for (p = 0; p < SUSAMUNE_ILING_PROFILE_COUNT; p++)
		for (i = 0; i < file->slotCount; i++)
			if (!PbValueIsValid(file->values[p][i]))
				return PB_READ_INVALID;
	for (p = 0; p < SUSAMUNE_ILING_CUSTOM_NAME_COUNT; p++)
		if (!PbProfileNameIsValid(file->customNames[p]))
			return PB_READ_INVALID;
	return PB_READ_VALID;
}

static void InitPbProfileDefaults(struct SusamuneILingProfilesCfg *profiles)
{
	u32 p;
	u32 i;
	memset(profiles, 0, sizeof(*profiles));
	profiles->magic = SUSAMUNE_ILING_PROFILE_MAGIC;
	profiles->version = SUSAMUNE_ILING_PROFILE_VERSION;
	profiles->profileCount = SUSAMUNE_ILING_PROFILE_COUNT;
	profiles->activeProfile = 0;
	profiles->slotCount = SUSAMUNE_ILING_PB_SLOT_COUNT;
	profiles->nameSize = SUSAMUNE_ILING_PROFILE_NAME_SIZE;
	for (p = 0; p < SUSAMUNE_ILING_PROFILE_COUNT; p++)
		for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
			profiles->values[p][i] = SUSAMUNE_ILING_PB_UNSET;
	strcpy(profiles->customNames[0], "Custom 1");
	strcpy(profiles->customNames[1], "Custom 2");
}

static bool InitPbFiles(struct SusamuneCfg *cfg, const char *region)
{
	struct SusamuneILingPbCfg *pbs = &cfg->ilingPbs;
	struct SusamuneILingProfilesCfg *profiles = &cfg->ilingProfiles;
	struct SusamuneILingPbFile legacyFile;
	struct SusamuneILingProfilesFile file;
	u32 i;
	u32 p;
	u32 fileIndex;
	bool legacySafe = true;
	bool safe = true;
	s32 legacyActive = -1;
	u32 legacyGeneration = 0;

	pbs->magic   = SUSAMUNE_ILING_PB_MAGIC;
	pbs->version = SUSAMUNE_ILING_PB_VERSION;
	pbs->count   = SUSAMUNE_ILING_PB_SLOT_COUNT;
	for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
		pbs->values[i] = SUSAMUNE_ILING_PB_UNSET;

	_sprintf(PbPaths[0], "%s/susamune_pbs_v1_%s_a.bin",
	         SusamuneCfgStoragePrefix(), region);
	_sprintf(PbPaths[1], "%s/susamune_pbs_v1_%s_b.bin",
	         SusamuneCfgStoragePrefix(), region);

	for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
	{
		enum PbReadResult readResult = ReadPbFile(PbPaths[fileIndex], &legacyFile);
		if (readResult == PB_READ_UNSAFE)
		{
			legacySafe = false;
			continue;
		}
		if (readResult != PB_READ_VALID)
			continue;
		if (legacyActive >= 0 &&
		    !PbGenerationIsNewer(legacyFile.generation, legacyGeneration))
			continue;

		for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
			pbs->values[i] = SUSAMUNE_ILING_PB_UNSET;
		for (i = 0; i < legacyFile.count; i++)
			pbs->values[i] = legacyFile.values[i];
		pbs->count = legacyFile.count > SUSAMUNE_ILING_PB_SLOT_COUNT
		                 ? legacyFile.count : SUSAMUNE_ILING_PB_SLOT_COUNT;
		legacyGeneration = legacyFile.generation;
		legacyActive = (s32)fileIndex;
	}

	InitPbProfileDefaults(profiles);
	_sprintf(PbPaths[0], "%s/susamune_pbs_v2_%s_a.bin",
	         SusamuneCfgStoragePrefix(), region);
	_sprintf(PbPaths[1], "%s/susamune_pbs_v2_%s_b.bin",
	         SusamuneCfgStoragePrefix(), region);
	PbGeneration = 0;
	PbActiveFile = -1;
	for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
	{
		enum PbReadResult readResult =
			ReadPbProfilesFile(PbPaths[fileIndex], &file);
		if (readResult == PB_READ_UNSAFE)
		{
			safe = false;
			continue;
		}
		if (readResult != PB_READ_VALID)
			continue;
		if (PbActiveFile >= 0 &&
		    !PbGenerationIsNewer(file.generation, PbGeneration))
			continue;

		profiles->activeProfile = file.activeProfile;
		profiles->slotCount = file.slotCount > SUSAMUNE_ILING_PB_SLOT_COUNT
			? file.slotCount : SUSAMUNE_ILING_PB_SLOT_COUNT;
		for (p = 0; p < SUSAMUNE_ILING_PROFILE_COUNT; p++)
			for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
				profiles->values[p][i] = file.values[p][i];
		for (p = 0; p < SUSAMUNE_ILING_CUSTOM_NAME_COUNT; p++)
			memcpy(profiles->customNames[p], file.customNames[p],
			       SUSAMUNE_ILING_PROFILE_NAME_SIZE);
		PbGeneration = file.generation;
		PbActiveFile = (s32)fileIndex;
	}

	if (PbActiveFile < 0)
	{
		if (!legacySafe)
			safe = false;
		else if (legacyActive >= 0)
		{
			for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
				profiles->values[0][i] = pbs->values[i];
			profiles->saveSeq = 1;
		}
	}

	PbAckSeq = 0;
	PbReady = safe;
	if (!safe)
		dbgprintf("Susamune: PB persistence disabled to preserve unreadable files\r\n");
	return safe;
}

static int WritePbProfilesFile(const struct SusamuneILingProfilesCfg *profiles)
{
	struct SusamuneILingProfilesFile file;
	FIL f;
	UINT wrote = 0;
	u32 i;
	u32 p;
	u32 target = PbActiveFile == 0 ? 1u : 0u;
	int ret;
	int closeRet;

	if (profiles->profileCount != SUSAMUNE_ILING_PROFILE_COUNT ||
	    profiles->activeProfile >= SUSAMUNE_ILING_PROFILE_COUNT ||
	    profiles->slotCount == 0 ||
	    profiles->slotCount > SUSAMUNE_ILING_PB_MAX_SLOTS ||
	    profiles->nameSize != SUSAMUNE_ILING_PROFILE_NAME_SIZE)
		return FR_INVALID_PARAMETER;
	for (p = 0; p < SUSAMUNE_ILING_PROFILE_COUNT; p++)
		for (i = 0; i < profiles->slotCount; i++)
			if (!PbValueIsValid(profiles->values[p][i]))
				return FR_INVALID_PARAMETER;
	for (p = 0; p < SUSAMUNE_ILING_CUSTOM_NAME_COUNT; p++)
		if (!PbProfileNameIsValid(profiles->customNames[p]))
			return FR_INVALID_PARAMETER;

	memset(&file, 0, sizeof(file));
	file.magic      = SUSAMUNE_ILING_PROFILE_FILE_MAGIC;
	file.version    = SUSAMUNE_ILING_PROFILE_VERSION;
	file.profileCount = SUSAMUNE_ILING_PROFILE_COUNT;
	file.activeProfile = profiles->activeProfile;
	file.slotCount = profiles->slotCount;
	file.nameSize = SUSAMUNE_ILING_PROFILE_NAME_SIZE;
	file.gameId     = GAME_ID;
	file.generation = PbGeneration + 1;
	for (p = 0; p < SUSAMUNE_ILING_PROFILE_COUNT; p++)
		for (i = 0; i < SUSAMUNE_ILING_PB_MAX_SLOTS; i++)
			file.values[p][i] = profiles->values[p][i];
	for (p = 0; p < SUSAMUNE_ILING_CUSTOM_NAME_COUNT; p++)
		memcpy(file.customNames[p], profiles->customNames[p],
		       SUSAMUNE_ILING_PROFILE_NAME_SIZE);
	file.checksum = PbProfilesChecksum(&file);

	ret = f_open_char(&f, PbPaths[target], FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
		return ret;

	ret = f_write(&f, &file, sizeof(file), &wrote);
	if (ret == FR_OK && wrote != sizeof(file))
		ret = FR_DISK_ERR;
	closeRet = f_close(&f);
	if (ret == FR_OK && closeRet != FR_OK)
		ret = closeRet;

	if (ret == FR_OK)
	{
		PbGeneration = file.generation;
		PbActiveFile = (s32)target;
	}
	return ret;
}

// ---------------------------------------------------------------------
// Global achievement/statistics binary files
// ---------------------------------------------------------------------

static u32 ProgressChecksum(const struct SusamuneProgressFile *file)
{
	u32 hash = 2166136261u;
	u32 region;
	u32 i;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         file->achievementBytes);
	hash = PbHashWord(hash, ((u32)file->statCount << 16) |
	                         file->regionCount);
	hash = PbHashWord(hash, file->generation);
	for (i = 0; i < SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES; i++)
		hash = (hash ^ file->achievements[i]) * 16777619u;
	for (region = 0; region < SUSAMUNE_PROGRESS_REGION_COUNT; region++)
		for (i = 0; i < SUSAMUNE_PROGRESS_STAT_COUNT; i++)
			hash = PbHashWord(hash, file->stats[region][i]);
	return hash;
}

static enum PbReadResult ReadProgressFile(
	const char *path, struct SusamuneProgressFile *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = sizeof(file->magic) + sizeof(file->version);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		// Inspect a recognized future header before rejecting its size. An old
		// launcher must never treat both generations of a newer format as junk
		// and overwrite them with a blank V1 journal.
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_PROGRESS_FILE_MAGIC &&
		    file->version != SUSAMUNE_PROGRESS_VERSION)
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;

	if (file->magic == SUSAMUNE_PROGRESS_FILE_MAGIC &&
	    file->version != SUSAMUNE_PROGRESS_VERSION)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_PROGRESS_FILE_MAGIC ||
	    file->achievementBytes != SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES ||
	    file->statCount != SUSAMUNE_PROGRESS_STAT_COUNT ||
	    file->regionCount != SUSAMUNE_PROGRESS_REGION_COUNT ||
	    file->checksum != ProgressChecksum(file))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static void InitProgressDefaults(struct SusamuneProgressCfg *progress)
{
	memset(progress, 0, sizeof(*progress));
	progress->magic = SUSAMUNE_PROGRESS_MAGIC;
	progress->version = SUSAMUNE_PROGRESS_VERSION;
	progress->achievementBytes = SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES;
	progress->statCount = SUSAMUNE_PROGRESS_STAT_COUNT;
	progress->regionCount = SUSAMUNE_PROGRESS_REGION_COUNT;
}

static bool InitProgressFiles(struct SusamuneProgressCfg *progress)
{
	struct SusamuneProgressFile file;
	u32 fileIndex;
	bool safe = true;

	InitProgressDefaults(progress);
	_sprintf(ProgressPaths[0], "%s/susamune_progress_v1_a.bin",
	         SusamuneCfgStoragePrefix());
	_sprintf(ProgressPaths[1], "%s/susamune_progress_v1_b.bin",
	         SusamuneCfgStoragePrefix());
	ProgressGeneration = 0;
	ProgressActiveFile = -1;
	for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
	{
		enum PbReadResult readResult =
			ReadProgressFile(ProgressPaths[fileIndex], &file);
		if (readResult == PB_READ_UNSAFE)
		{
			safe = false;
			continue;
		}
		if (readResult != PB_READ_VALID)
			continue;
		if (ProgressActiveFile >= 0 &&
		    !PbGenerationIsNewer(file.generation, ProgressGeneration))
			continue;

		memcpy(progress->achievements, file.achievements,
		       sizeof(progress->achievements));
		memcpy(progress->stats, file.stats, sizeof(progress->stats));
		ProgressGeneration = file.generation;
		ProgressActiveFile = (s32)fileIndex;
	}

	ProgressAckSeq = 0;
	ProgressReady = safe;
	if (safe)
		progress->flags |= SUSAMUNE_PROGRESS_FLAG_WRITABLE;
	else
		dbgprintf("Susamune: progress persistence disabled to preserve unreadable files\r\n");
	return safe;
}

static int WriteProgressFile(const struct SusamuneProgressCfg *progress)
{
	struct SusamuneProgressFile file;
	FIL f;
	UINT wrote = 0;
	u32 target = ProgressActiveFile == 0 ? 1u : 0u;
	int ret;
	int closeRet;

	if (progress->magic != SUSAMUNE_PROGRESS_MAGIC ||
	    progress->version != SUSAMUNE_PROGRESS_VERSION ||
	    progress->achievementBytes != SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES ||
	    progress->statCount != SUSAMUNE_PROGRESS_STAT_COUNT ||
	    progress->regionCount != SUSAMUNE_PROGRESS_REGION_COUNT)
		return FR_INVALID_PARAMETER;

	memset(&file, 0, sizeof(file));
	file.magic = SUSAMUNE_PROGRESS_FILE_MAGIC;
	file.version = SUSAMUNE_PROGRESS_VERSION;
	file.achievementBytes = SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES;
	file.statCount = SUSAMUNE_PROGRESS_STAT_COUNT;
	file.regionCount = SUSAMUNE_PROGRESS_REGION_COUNT;
	file.generation = ProgressGeneration + 1;
	memcpy(file.achievements, progress->achievements,
	       sizeof(file.achievements));
	memcpy(file.stats, progress->stats, sizeof(file.stats));
	file.checksum = ProgressChecksum(&file);

	ret = f_open_char(&f, ProgressPaths[target], FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
		return ret;
	ret = f_write(&f, &file, sizeof(file), &wrote);
	if (ret == FR_OK && wrote != sizeof(file))
		ret = FR_DISK_ERR;
	closeRet = f_close(&f);
	if (ret == FR_OK && closeRet != FR_OK)
		ret = closeRet;

	if (ret == FR_OK)
	{
		ProgressGeneration = file.generation;
		ProgressActiveFile = (s32)target;
	}
	return ret;
}

// ---------------------------------------------------------------------
// Stage Loader custom-playlist binary files
// ---------------------------------------------------------------------

static bool StagePlaylistBytesZero(const u8 *bytes, u32 size)
{
	u32 i;
	for (i = 0; i < size; i++)
		if (bytes[i] != 0)
			return false;
	return true;
}

static bool StagePlaylistActionAt(
	const u8 actions[SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES], u32 position)
{
	return (actions[position >> 3] & (1u << (position & 7))) != 0;
}

static bool StagePlaylistActionAllowed(u8 entry)
{
	return entry == SUSAMUNE_STAGE_PLAYLIST_ACTION_BIANCO_1 ||
	       entry == SUSAMUNE_STAGE_PLAYLIST_ACTION_GELATO_1 ||
	       entry == SUSAMUNE_STAGE_PLAYLIST_ACTION_PIANTA_5;
}

static u32 StagePlaylistContentHash(
	u32 playlistId, u32 revision, u8 count,
	const u8 entries[SUSAMUNE_STAGE_PLAYLIST_CAPACITY],
	const u8 actions[SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES])
{
	u32 hash = 2166136261u;
	u32 i;

	hash = PbHashWord(hash,
		(SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA << 24) |
		((playlistId & 0xffu) << 16) | count);
	hash = PbHashWord(hash, revision);
	for (i = 0; i < SUSAMUNE_STAGE_PLAYLIST_CAPACITY; i++)
	{
		hash = (hash ^ entries[i]) * 16777619u;
		hash = (hash ^ (StagePlaylistActionAt(actions, i) ? 1u : 0u)) *
		       16777619u;
	}
	return hash;
}

static bool StagePlaylistV1PayloadValid(
	const u8 counts[SUSAMUNE_STAGE_PLAYLIST_COUNT],
	const u8 entries[SUSAMUNE_STAGE_PLAYLIST_COUNT]
	                [SUSAMUNE_STAGE_PLAYLIST_CAPACITY])
{
	u32 slot;
	u32 i;

	for (slot = 0; slot < SUSAMUNE_STAGE_PLAYLIST_COUNT; slot++)
	{
		if (counts[slot] > SUSAMUNE_STAGE_PLAYLIST_CAPACITY)
			return false;
		for (i = 0; i < counts[slot]; i++)
			if (entries[slot][i] >= SUSAMUNE_STAGE_PLAYLIST_ROUTE_COUNT)
				return false;
		for (; i < SUSAMUNE_STAGE_PLAYLIST_CAPACITY; i++)
			if (entries[slot][i] != 0)
				return false;
	}
	return true;
}

static bool StagePlaylistPayloadValid(
	const u8 counts[SUSAMUNE_STAGE_PLAYLIST_COUNT],
	const u8 entries[SUSAMUNE_STAGE_PLAYLIST_COUNT]
	                [SUSAMUNE_STAGE_PLAYLIST_CAPACITY],
	const u8 actions[SUSAMUNE_STAGE_PLAYLIST_COUNT]
	                [SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES],
	const u32 revisions[SUSAMUNE_STAGE_PLAYLIST_COUNT],
	const u32 contentHashes[SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT],
	const u32 bestQf[SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT]
	                [SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT])
{
	u32 slot;
	u32 position;
	u32 region;
	u32 id;

	if (!StagePlaylistV1PayloadValid(counts, entries))
		return false;
	for (slot = 0; slot < SUSAMUNE_STAGE_PLAYLIST_COUNT; slot++)
	{
		for (position = 0; position < counts[slot]; position++)
			if (StagePlaylistActionAt(actions[slot], position) &&
			    !StagePlaylistActionAllowed(entries[slot][position]))
				return false;
		for (; position < SUSAMUNE_STAGE_PLAYLIST_CAPACITY; position++)
			if (StagePlaylistActionAt(actions[slot], position))
				return false;
		if (contentHashes[SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT + slot] !=
		    StagePlaylistContentHash(
			    SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT + slot,
			    revisions[slot], counts[slot], entries[slot], actions[slot]))
			return false;
	}
	for (region = 0; region < SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT; region++)
	{
		for (id = 0; id < SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT; id++)
		{
			const u32 best = bestQf[region][id];
			if (best != SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET && best == 0)
				return false;
			if (id >= SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT &&
			    counts[id - SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT] == 0 &&
			    best != SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET)
				return false;
		}
	}
	return true;
}

static bool StagePlaylistIdentityChangesValid(
	const u32 revisions[SUSAMUNE_STAGE_PLAYLIST_COUNT],
	const u32 contentHashes[SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT],
	const u32 bestQf[SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT]
	                [SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT])
{
	u32 id;
	u32 region;
	bool changed;

	for (id = 0; id < SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT; id++)
	{
		changed = contentHashes[id] != StagePlaylistCommittedHashes[id];
		if (id >= SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT)
			changed = changed ||
				revisions[id - SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT] !=
				StagePlaylistCommittedRevisions[
					id - SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT];
		if (!changed ||
		    !(StagePlaylistCommittedBestMask & (1u << id)))
			continue;
		for (region = 0; region < SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT;
		     region++)
			if (bestQf[region][id] !=
			    SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET)
				return false;
	}
	return true;
}

static void StagePlaylistRememberIdentities(
	const u32 revisions[SUSAMUNE_STAGE_PLAYLIST_COUNT],
	const u32 contentHashes[SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT],
	const u32 bestQf[SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT]
	                [SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT])
{
	u32 id;
	u32 region;

	memcpy(StagePlaylistCommittedRevisions, revisions,
	       sizeof(StagePlaylistCommittedRevisions));
	memcpy(StagePlaylistCommittedHashes, contentHashes,
	       sizeof(StagePlaylistCommittedHashes));
	StagePlaylistCommittedBestMask = 0;
	for (region = 0; region < SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT; region++)
		for (id = 0; id < SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT; id++)
			if (bestQf[region][id] !=
			    SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET)
				StagePlaylistCommittedBestMask |= 1u << id;
}

static u32 StagePlaylistV1Checksum(
	const struct SusamuneStagePlaylistsFileV1 *file)
{
	u32 hash = 2166136261u;
	u32 slot;
	u32 i;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->slotCount << 8) | file->capacity);
	hash = PbHashWord(hash, file->generation);
	for (slot = 0; slot < SUSAMUNE_STAGE_PLAYLIST_COUNT; slot++)
	{
		hash = (hash ^ file->counts[slot]) * 16777619u;
		for (i = 0; i < SUSAMUNE_STAGE_PLAYLIST_CAPACITY; i++)
			hash = (hash ^ file->entries[slot][i]) * 16777619u;
	}
	return hash;
}

static u32 StagePlaylistChecksum(const struct SusamuneStagePlaylistsFile *file)
{
	u32 hash = 2166136261u;
	u32 slot;
	u32 region;
	u32 id;
	u32 i;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->slotCount << 8) | file->capacity);
	hash = PbHashWord(hash, file->generation);
	hash = PbHashWord(hash, ((u32)file->builtinCount << 24) |
	                         ((u32)file->regionCount << 16) |
	                         ((u32)file->actionBytes << 8) |
	                         file->actionSchema);
	for (slot = 0; slot < SUSAMUNE_STAGE_PLAYLIST_COUNT; slot++)
	{
		hash = (hash ^ file->counts[slot]) * 16777619u;
		for (i = 0; i < SUSAMUNE_STAGE_PLAYLIST_CAPACITY; i++)
			hash = (hash ^ file->entries[slot][i]) * 16777619u;
		for (i = 0; i < SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES; i++)
			hash = (hash ^ file->actions[slot][i]) * 16777619u;
		hash = PbHashWord(hash, file->revisions[slot]);
	}
	for (id = 0; id < SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT; id++)
		hash = PbHashWord(hash, file->contentHashes[id]);
	for (region = 0; region < SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT; region++)
		for (id = 0; id < SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT; id++)
			hash = PbHashWord(hash, file->bestQf[region][id]);
	return hash;
}

static enum PbReadResult ReadStagePlaylistV1File(
	const char *path, struct SusamuneStagePlaylistsFileV1 *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = sizeof(file->magic) + sizeof(file->version);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_STAGE_PLAYLIST_FILE_MAGIC &&
		    file->version != SUSAMUNE_STAGE_PLAYLIST_VERSION_V1)
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_STAGE_PLAYLIST_FILE_MAGIC &&
	    file->version != SUSAMUNE_STAGE_PLAYLIST_VERSION_V1)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_STAGE_PLAYLIST_FILE_MAGIC ||
	    file->slotCount != SUSAMUNE_STAGE_PLAYLIST_COUNT ||
	    file->capacity != SUSAMUNE_STAGE_PLAYLIST_CAPACITY ||
	    !StagePlaylistBytesZero(file->reserved0,
	                            sizeof(file->reserved0)) ||
	    !StagePlaylistBytesZero(file->reserved1,
	                            sizeof(file->reserved1)) ||
	    file->checksum != StagePlaylistV1Checksum(file) ||
	    !StagePlaylistV1PayloadValid(file->counts, file->entries))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static enum PbReadResult ReadStagePlaylistFile(
	const char *path, struct SusamuneStagePlaylistsFile *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = sizeof(file->magic) + sizeof(file->version);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_STAGE_PLAYLIST_FILE_MAGIC &&
		    file->version != SUSAMUNE_STAGE_PLAYLIST_VERSION)
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_STAGE_PLAYLIST_FILE_MAGIC &&
	    file->version != SUSAMUNE_STAGE_PLAYLIST_VERSION)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_STAGE_PLAYLIST_FILE_MAGIC ||
	    file->slotCount != SUSAMUNE_STAGE_PLAYLIST_COUNT ||
	    file->capacity != SUSAMUNE_STAGE_PLAYLIST_CAPACITY ||
	    file->builtinCount != SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT ||
	    file->regionCount != SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT ||
	    file->actionBytes != SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES ||
	    file->actionSchema != SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA ||
	    !StagePlaylistBytesZero(file->reserved0,
	                            sizeof(file->reserved0)) ||
	    !StagePlaylistBytesZero(file->reserved1,
	                            sizeof(file->reserved1)) ||
	    file->checksum != StagePlaylistChecksum(file) ||
	    !StagePlaylistPayloadValid(
		    file->counts, file->entries, file->actions, file->revisions,
		    file->contentHashes, file->bestQf))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static void InitStagePlaylistDefaults(
	struct SusamuneStagePlaylistsCfg *playlists)
{
	u32 slot;
	u32 region;
	u32 id;

	memset(playlists, 0, sizeof(*playlists));
	playlists->magic = SUSAMUNE_STAGE_PLAYLIST_MAGIC;
	playlists->version = SUSAMUNE_STAGE_PLAYLIST_VERSION;
	playlists->slotCount = SUSAMUNE_STAGE_PLAYLIST_COUNT;
	playlists->capacity = SUSAMUNE_STAGE_PLAYLIST_CAPACITY;
	playlists->builtinCount = SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT;
	playlists->regionCount = SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT;
	playlists->actionBytes = SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES;
	playlists->actionSchema = SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA;
	for (slot = 0; slot < SUSAMUNE_STAGE_PLAYLIST_COUNT; slot++)
		playlists->contentHashes[SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT + slot] =
			StagePlaylistContentHash(
				SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT + slot, 0, 0,
				playlists->entries[slot], playlists->actions[slot]);
	for (region = 0; region < SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT; region++)
		for (id = 0; id < SUSAMUNE_STAGE_PLAYLIST_TOTAL_COUNT; id++)
			playlists->bestQf[region][id] =
				SUSAMUNE_STAGE_PLAYLIST_BEST_UNSET;
}

static bool InitStagePlaylistFiles(
	struct SusamuneStagePlaylistsCfg *playlists)
{
	struct SusamuneStagePlaylistsFile file;
	struct SusamuneStagePlaylistsFileV1 v1File;
	u32 fileIndex;
	u32 v1Generation = 0;
	s32 v1ActiveFile = -1;
	u32 slot;
	bool safe = true;

	InitStagePlaylistDefaults(playlists);
	_sprintf(StagePlaylistPaths[0], "%s/susamune_stage_playlists_v2_a.bin",
	         SusamuneCfgStoragePrefix());
	_sprintf(StagePlaylistPaths[1], "%s/susamune_stage_playlists_v2_b.bin",
	         SusamuneCfgStoragePrefix());
	_sprintf(StagePlaylistV1Paths[0], "%s/susamune_stage_playlists_v1_a.bin",
	         SusamuneCfgStoragePrefix());
	_sprintf(StagePlaylistV1Paths[1], "%s/susamune_stage_playlists_v1_b.bin",
	         SusamuneCfgStoragePrefix());
	StagePlaylistGeneration = 0;
	StagePlaylistActiveFile = -1;
	for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
	{
		enum PbReadResult readResult =
			ReadStagePlaylistFile(StagePlaylistPaths[fileIndex], &file);
		if (readResult == PB_READ_UNSAFE)
		{
			safe = false;
			continue;
		}
		if (readResult != PB_READ_VALID)
			continue;
		if (StagePlaylistActiveFile >= 0 &&
		    !PbGenerationIsNewer(file.generation,
		                         StagePlaylistGeneration))
			continue;

		memcpy(playlists->counts, file.counts,
		       sizeof(file) - __builtin_offsetof(
		           struct SusamuneStagePlaylistsFile, counts));
		StagePlaylistGeneration = file.generation;
		StagePlaylistActiveFile = (s32)fileIndex;
	}
	if (StagePlaylistActiveFile < 0 && safe)
	{
		for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
		{
			enum PbReadResult readResult =
				ReadStagePlaylistV1File(StagePlaylistV1Paths[fileIndex],
				                            &v1File);
			if (readResult == PB_READ_UNSAFE)
			{
				safe = false;
				continue;
			}
			if (readResult != PB_READ_VALID)
				continue;
			if (v1ActiveFile >= 0 &&
			    !PbGenerationIsNewer(v1File.generation, v1Generation))
				continue;

			memcpy(playlists->counts, v1File.counts,
			       sizeof(playlists->counts));
			memcpy(playlists->entries, v1File.entries,
			       sizeof(playlists->entries));
			v1Generation = v1File.generation;
			v1ActiveFile = (s32)fileIndex;
		}
		if (safe && v1ActiveFile >= 0)
		{
			for (slot = 0; slot < SUSAMUNE_STAGE_PLAYLIST_COUNT; slot++)
				playlists->contentHashes[
					SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT + slot] =
					StagePlaylistContentHash(
						SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT + slot,
						0, playlists->counts[slot],
						playlists->entries[slot],
						playlists->actions[slot]);
			// Ring the V2 doorbell once. The V1 generations remain untouched.
			playlists->saveSeq = 1;
		}
	}

	StagePlaylistAckSeq = 0;
	StagePlaylistReady = safe;
	if (safe)
		playlists->flags |= SUSAMUNE_STAGE_PLAYLIST_FLAG_WRITABLE;
	else
		dbgprintf("Susamune: playlist persistence disabled to preserve unreadable files\r\n");
	StagePlaylistRememberIdentities(playlists->revisions,
	                                playlists->contentHashes,
	                                playlists->bestQf);
	return safe;
}

static int WriteStagePlaylistFile(
	const struct SusamuneStagePlaylistsCfg *playlists)
{
	struct SusamuneStagePlaylistsFile file;
	FIL f;
	UINT wrote = 0;
	u32 target = StagePlaylistActiveFile == 0 ? 1u : 0u;
	int ret;
	int syncRet;
	int closeRet;

	if (playlists->magic != SUSAMUNE_STAGE_PLAYLIST_MAGIC ||
	    playlists->version != SUSAMUNE_STAGE_PLAYLIST_VERSION ||
	    playlists->slotCount != SUSAMUNE_STAGE_PLAYLIST_COUNT ||
	    playlists->capacity != SUSAMUNE_STAGE_PLAYLIST_CAPACITY ||
	    playlists->builtinCount != SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT ||
	    playlists->regionCount != SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT ||
	    playlists->actionBytes != SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES ||
	    playlists->actionSchema != SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA ||
	    !StagePlaylistBytesZero(playlists->reserved,
	                            sizeof(playlists->reserved)) ||
	    !StagePlaylistPayloadValid(
		    playlists->counts, playlists->entries, playlists->actions,
		    playlists->revisions, playlists->contentHashes,
		    playlists->bestQf) ||
	    !StagePlaylistIdentityChangesValid(
		    playlists->revisions, playlists->contentHashes,
		    playlists->bestQf))
		return FR_INVALID_PARAMETER;

	memset(&file, 0, sizeof(file));
	file.magic = SUSAMUNE_STAGE_PLAYLIST_FILE_MAGIC;
	file.version = SUSAMUNE_STAGE_PLAYLIST_VERSION;
	file.slotCount = SUSAMUNE_STAGE_PLAYLIST_COUNT;
	file.capacity = SUSAMUNE_STAGE_PLAYLIST_CAPACITY;
	file.generation = StagePlaylistGeneration + 1;
	file.builtinCount = SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT;
	file.regionCount = SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT;
	file.actionBytes = SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES;
	file.actionSchema = SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA;
	memcpy(file.counts, playlists->counts,
	       sizeof(file) - __builtin_offsetof(
	           struct SusamuneStagePlaylistsFile, counts));
	file.checksum = StagePlaylistChecksum(&file);

	ret = f_open_char(&f, StagePlaylistPaths[target],
	                  FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
		return ret;
	ret = f_write(&f, &file, sizeof(file), &wrote);
	if (ret == FR_OK && wrote != sizeof(file))
		ret = FR_DISK_ERR;
	syncRet = f_sync(&f);
	if (ret == FR_OK && syncRet != FR_OK)
		ret = syncRet;
	closeRet = f_close(&f);
	if (ret == FR_OK && closeRet != FR_OK)
		ret = closeRet;

	if (ret == FR_OK)
	{
		StagePlaylistGeneration = file.generation;
		StagePlaylistActiveFile = (s32)target;
		StagePlaylistRememberIdentities(file.revisions,
		                                file.contentHashes,
		                                file.bestQf);
	}
	return ret;
}

// ---------------------------------------------------------------------
// Per-level Stage Loader target binary files
// ---------------------------------------------------------------------

static bool StageTargetValueValid(s32 value)
{
	return value >= SUSAMUNE_STAGE_TARGET_UNSET &&
	       value <= SUSAMUNE_ILING_PB_MAX_QF;
}

static u32 StageTargetChecksum(const struct SusamuneStageTargetsFile *file)
{
	u32 hash = 2166136261u;
	u32 slot;

	hash = PbHashWord(hash, ((u32)file->version << 16) | file->slotCount);
	hash = PbHashWord(hash, file->gameId);
	hash = PbHashWord(hash, file->generation);
	for (slot = 0; slot < SUSAMUNE_STAGE_TARGET_SLOT_COUNT; slot++)
		hash = PbHashWord(hash, (u32)file->targets[slot]);
	return hash;
}

static enum PbReadResult ReadStageTargetFile(
	const char *path, struct SusamuneStageTargetsFile *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 slot;
	u32 prefixSize = sizeof(file->magic) + sizeof(file->version);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_STAGE_TARGET_FILE_MAGIC &&
		    file->version != SUSAMUNE_STAGE_TARGET_VERSION)
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_STAGE_TARGET_FILE_MAGIC &&
	    file->version != SUSAMUNE_STAGE_TARGET_VERSION)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_STAGE_TARGET_FILE_MAGIC ||
	    file->slotCount != SUSAMUNE_STAGE_TARGET_SLOT_COUNT ||
	    file->gameId != GAME_ID ||
	    !StagePlaylistBytesZero(file->reserved, sizeof(file->reserved)) ||
	    file->checksum != StageTargetChecksum(file))
		return PB_READ_INVALID;
	for (slot = 0; slot < SUSAMUNE_STAGE_TARGET_SLOT_COUNT; slot++)
		if (!StageTargetValueValid(file->targets[slot]))
			return PB_READ_INVALID;
	return PB_READ_VALID;
}

static void InitStageTargetDefaults(struct SusamuneStageTargetsCfg *targets)
{
	u32 slot;

	memset(targets, 0, sizeof(*targets));
	targets->magic = SUSAMUNE_STAGE_TARGET_MAGIC;
	targets->version = SUSAMUNE_STAGE_TARGET_VERSION;
	targets->slotCount = SUSAMUNE_STAGE_TARGET_SLOT_COUNT;
	for (slot = 0; slot < SUSAMUNE_STAGE_TARGET_SLOT_COUNT; slot++)
		targets->targets[slot] = SUSAMUNE_STAGE_TARGET_UNSET;
}

static void InitStageTargetFiles(struct SusamuneStageTargetsCfg *targets,
	                             const char *region)
{
	struct SusamuneStageTargetsFile file;
	u32 fileIndex;
	bool safe = true;

	InitStageTargetDefaults(targets);
	_sprintf(StageTargetPaths[0], "%s/susamune_stage_targets_%s_a.bin",
	         SusamuneCfgStoragePrefix(), region);
	_sprintf(StageTargetPaths[1], "%s/susamune_stage_targets_%s_b.bin",
	         SusamuneCfgStoragePrefix(), region);
	StageTargetGeneration = 0;
	StageTargetActiveFile = -1;
	for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
	{
		enum PbReadResult readResult =
			ReadStageTargetFile(StageTargetPaths[fileIndex], &file);
		if (readResult == PB_READ_UNSAFE)
		{
			safe = false;
			continue;
		}
		if (readResult != PB_READ_VALID)
			continue;
		if (StageTargetActiveFile >= 0 &&
		    !PbGenerationIsNewer(file.generation, StageTargetGeneration))
			continue;

		memcpy(targets->targets, file.targets, sizeof(targets->targets));
		StageTargetGeneration = file.generation;
		StageTargetActiveFile = (s32)fileIndex;
	}

	StageTargetAckSeq = 0;
	StageTargetReady = safe;
	if (safe)
		targets->flags |= SUSAMUNE_STAGE_TARGET_FLAG_WRITABLE;
	else
		dbgprintf("Susamune: stage-target persistence disabled to preserve unreadable files\r\n");
}

static int WriteStageTargetFile(const struct SusamuneStageTargetsCfg *targets)
{
	struct SusamuneStageTargetsFile file;
	FIL f;
	UINT wrote = 0;
	u32 slot;
	u32 target = StageTargetActiveFile == 0 ? 1u : 0u;
	int ret;
	int syncRet;
	int closeRet;

	if (targets->magic != SUSAMUNE_STAGE_TARGET_MAGIC ||
	    targets->version != SUSAMUNE_STAGE_TARGET_VERSION ||
	    targets->slotCount != SUSAMUNE_STAGE_TARGET_SLOT_COUNT)
		return FR_INVALID_PARAMETER;
	for (slot = 0; slot < SUSAMUNE_STAGE_TARGET_SLOT_COUNT; slot++)
		if (!StageTargetValueValid(targets->targets[slot]))
			return FR_INVALID_PARAMETER;

	memset(&file, 0, sizeof(file));
	file.magic = SUSAMUNE_STAGE_TARGET_FILE_MAGIC;
	file.version = SUSAMUNE_STAGE_TARGET_VERSION;
	file.slotCount = SUSAMUNE_STAGE_TARGET_SLOT_COUNT;
	file.gameId = GAME_ID;
	file.generation = StageTargetGeneration + 1;
	memcpy(file.targets, targets->targets, sizeof(file.targets));
	file.checksum = StageTargetChecksum(&file);

	ret = f_open_char(&f, StageTargetPaths[target],
	                  FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
		return ret;
	ret = f_write(&f, &file, sizeof(file), &wrote);
	if (ret == FR_OK && wrote != sizeof(file))
		ret = FR_DISK_ERR;
	syncRet = f_sync(&f);
	if (ret == FR_OK && syncRet != FR_OK)
		ret = syncRet;
	closeRet = f_close(&f);
	if (ret == FR_OK && closeRet != FR_OK)
		ret = closeRet;
	if (ret == FR_OK)
	{
		StageTargetGeneration = file.generation;
		StageTargetActiveFile = (s32)target;
	}
	return ret;
}

// ---------------------------------------------------------------------
// Regional IL split/statistics binary files
// ---------------------------------------------------------------------

static const u16 SplitRouteFirst[SUSAMUNE_SPLIT_STATS_ROUTE_COUNT] =
{
	0, 4, 7, 12, 16, 21, 24, 28, 32, 36,
	38, 39, 43, 45, 51, 55, 57, 62, 67, 72,
	75, 77, 82, 84, 88, 90, 93, 96, 102, 106,
	108, 110, 111, 114, 116, 120, 123, 127, 129, 131,
	134, 139, 142, 145, 151, 154, 160, 163, 166, 171,
	175, 178, 183, 187, 189, 194, 197, 199, 203, 208,
	212, 214, 215, 216, 217, 218, 219, 220, 221, 222,
	223, 224, 225, 226, 227, 228, 229, 230, 231, 232,
	233, 234, 235, 236, 237, 238, 239, 240, 241, 242,
	243, 244, 245, 246, 247, 248, 249, 250, 251, 252,
	253, 254, 255, 256, 257, 258, 259, 260, 261, 262,
	263, 264, 265, 266, 267, 268, 269, 270, 271, 272,
	273, 274
};

static const u8 SplitRouteCount[SUSAMUNE_SPLIT_STATS_ROUTE_COUNT] =
{
	4, 3, 5, 4, 5, 3, 4, 4, 4, 2,
	1, 4, 2, 6, 4, 2, 5, 5, 5, 3,
	2, 5, 2, 4, 2, 3, 3, 6, 4, 2,
	2, 1, 3, 2, 4, 3, 4, 2, 2, 3,
	5, 3, 3, 6, 3, 6, 3, 3, 5, 4,
	3, 5, 4, 2, 5, 3, 2, 4, 5, 4,
	2,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1
};

static const u16 SplitV6RouteFirst[SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT] =
{
	0, 4, 7, 12, 16, 21, 24, 28, 32, 36,
	38, 39, 43, 45, 50, 54, 56, 61, 66, 71,
	74, 76, 81, 83, 87, 89, 92, 95, 101, 105,
	107, 109, 110, 113, 115, 119, 122, 126, 128, 130,
	133, 138, 141, 144, 150, 153, 159, 162, 165, 170,
	174, 177, 182, 186, 188, 193, 196, 198, 202, 207,
	211, 213, 214, 215, 216, 217, 218, 219, 220, 221,
	222, 223, 224, 225, 226, 227, 228, 229, 230, 231,
	232, 233, 234, 235, 236, 237, 238, 239, 240, 241,
	242, 243, 244, 245, 246, 247, 248, 249, 250, 251,
	252, 253, 254, 255, 256, 257, 258, 259, 260, 261,
	262, 263, 264, 265, 266, 267, 268, 269, 270, 271,
	272, 273
};

static const u8 SplitV6RouteCount[SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT] =
{
	4, 3, 5, 4, 5, 3, 4, 4, 4, 2,
	1, 4, 2, 5, 4, 2, 5, 5, 5, 3,
	2, 5, 2, 4, 2, 3, 3, 6, 4, 2,
	2, 1, 3, 2, 4, 3, 4, 2, 2, 3,
	5, 3, 3, 6, 3, 6, 3, 3, 5, 4,
	3, 5, 4, 2, 5, 3, 2, 4, 5, 4,
	2,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1
};

static const u16 SplitV5RouteFirst[SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT] =
{
	0, 4, 7, 12, 16, 21, 24, 28, 32, 36,
	38, 41, 45, 47, 52, 56, 58, 63, 68, 73,
	76, 78, 83, 85, 89, 91, 94, 97, 103, 107,
	109, 111, 114, 117, 119, 123, 126, 130, 132, 134,
	137, 142, 145, 148, 154, 157, 163, 166, 169, 174,
	178, 181, 186, 190, 192, 197, 200, 202, 206, 211,
	215
};

static const u8 SplitV5RouteCount[SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT] =
{
	4, 3, 5, 4, 5, 3, 4, 4, 4, 2,
	3, 4, 2, 5, 4, 2, 5, 5, 5, 3,
	2, 5, 2, 4, 2, 3, 3, 6, 4, 2,
	2, 3, 3, 2, 4, 3, 4, 2, 2, 3,
	5, 3, 3, 6, 3, 6, 3, 3, 5, 4,
	3, 5, 4, 2, 5, 3, 2, 4, 5, 4,
	2
};

static const u16 SplitV4RouteFirst[SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT] =
{
	0, 4, 7, 12, 16, 21, 24, 28, 32, 36,
	38, 41, 45, 47, 53, 57, 59, 64, 69, 74,
	77, 79, 84, 86, 90, 92, 95, 98, 104, 108,
	110, 112, 115, 118, 120, 124, 127, 131, 133, 135,
	138, 144, 148, 151, 157, 160, 166, 169, 172, 177,
	181, 184, 189, 193, 195, 200, 203, 205, 209, 214,
	218
};

static const u8 SplitV4RouteCount[SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT] =
{
	4, 3, 5, 4, 5, 3, 4, 4, 4, 2,
	3, 4, 2, 6, 4, 2, 5, 5, 5, 3,
	2, 5, 2, 4, 2, 3, 3, 6, 4, 2,
	2, 3, 3, 2, 4, 3, 4, 2, 2, 3,
	6, 4, 3, 6, 3, 6, 3, 3, 5, 4,
	3, 5, 4, 2, 5, 3, 2, 4, 5, 4,
	2
};

static const u16 SplitV3RouteFirst[SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT] =
{
	0, 4, 7, 12, 16, 21, 24, 28, 32, 36,
	38, 41, 45, 47, 53, 57, 59, 64, 69, 74,
	77, 79, 84, 86, 90, 92, 95, 98, 104, 108,
	110, 112, 115, 118, 120, 125, 128, 131, 133, 135,
	138, 144, 148, 151, 157, 160, 166, 168, 171, 176,
	180, 183, 188, 192, 194, 199, 202, 204, 207
};

static const u8 SplitV3RouteCount[SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT] =
{
	4, 3, 5, 4, 5, 3, 4, 4, 4, 2,
	3, 4, 2, 6, 4, 2, 5, 5, 5, 3,
	2, 5, 2, 4, 2, 3, 3, 6, 4, 2,
	2, 3, 3, 2, 5, 3, 3, 2, 2, 3,
	6, 4, 3, 6, 3, 6, 2, 3, 5, 4,
	3, 5, 4, 2, 5, 3, 2, 3, 5
};

static const u8 SplitV2RouteFirst[SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT] =
{
	0, 4, 7, 12, 16, 21, 24, 28, 32, 36
};

static const u8 SplitV2RouteCount[SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT] =
{
	4, 3, 5, 4, 5, 3, 4, 4, 4, 2
};

static const u8 SplitV1RouteFirst[SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT] =
{
	0, 3, 5
};

static const u8 SplitV1RouteCount[SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT] =
{
	3, 2, 4
};

static bool SplitStatsBytesZero(const u8 *bytes, u32 size)
{
	u32 i;
	for (i = 0; i < size; i++)
		if (bytes[i] != 0)
			return false;
	return true;
}

static bool SplitStatsQfValid(u32 value)
{
	return value == SUSAMUNE_SPLIT_STATS_QF_UNSET ||
	       value <= (u32)SUSAMUNE_ILING_PB_MAX_QF;
}

static bool SplitStatsPayloadValid(
	const struct SusamuneSplitStatsPayload *payload)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_ROUTE_COUNT; route++)
		{
			if (payload->routeStats[region][route].finishes >
			    payload->routeStats[region][route].attempts)
				return false;
		}
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT;
		     segment++)
			if (!SplitStatsQfValid(payload->bestQf[region][segment]))
				return false;

		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_ROUTE_COUNT;
			     route++)
			{
				const u32 identity =
					payload->pbIdentityQf[region][profile][route];
				const u32 first = SplitRouteFirst[route];
				const u32 end = first + SplitRouteCount[route];
				if (!SplitStatsQfValid(identity))
					return false;
				for (segment = first; segment < end; segment++)
				{
					const u32 value =
						payload->pbQf[region][profile][segment];
					if (!SplitStatsQfValid(value))
						return false;
				}
			}
		}
	}
	return true;
}

static u32 SplitStatsChecksum(const struct SusamuneSplitStatsFile *file)
{
	const struct SusamuneSplitStatsPayload *payload = &file->payload;
	u32 hash = 2166136261u;
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->routeCount << 8) |
	                         file->regionCount);
	hash = PbHashWord(hash, ((u32)file->segmentCount << 16) |
	                         ((u32)file->profileCount << 8) |
	                         file->headerReserved);
	hash = PbHashWord(hash, file->payloadBytes);
	hash = PbHashWord(hash, file->schemaHash);
	hash = PbHashWord(hash, file->generation);
	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_ROUTE_COUNT; route++)
		{
			const struct SusamuneSplitRouteStats *stats =
				&payload->routeStats[region][route];
			hash = PbHashWord(hash, stats->attempts);
			hash = PbHashWord(hash, stats->finishes);
			hash = PbHashWord(hash, stats->golds);
		}
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_ROUTE_COUNT; route++)
			hash = PbHashWord(hash, payload->playedQf[region][route]);
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT;
		     segment++)
			hash = PbHashWord(hash, payload->bestQf[region][segment]);
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_ROUTE_COUNT;
			     route++)
				hash = PbHashWord(
					hash,
					payload->pbIdentityQf[region][profile][route]);
			for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT;
			     segment++)
				hash = PbHashWord(
					hash, payload->pbQf[region][profile][segment]);
		}
	}
	return hash;
}

static enum PbReadResult ReadSplitStatsFile(
	const char *path, struct SusamuneSplitStatsFile *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = __builtin_offsetof(
		struct SusamuneSplitStatsFile, generation);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
		    (file->version != SUSAMUNE_SPLIT_STATS_VERSION ||
		     file->schemaHash != SUSAMUNE_SPLIT_STATS_SCHEMA_HASH))
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version != SUSAMUNE_SPLIT_STATS_VERSION)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version == SUSAMUNE_SPLIT_STATS_VERSION &&
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_SCHEMA_HASH)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_SPLIT_STATS_FILE_MAGIC ||
	    file->routeCount != SUSAMUNE_SPLIT_STATS_ROUTE_COUNT ||
	    file->segmentCount != SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT ||
	    file->regionCount != SUSAMUNE_SPLIT_STATS_REGION_COUNT ||
	    file->profileCount != SUSAMUNE_SPLIT_STATS_PROFILE_COUNT ||
	    file->headerReserved != 0 ||
	    file->payloadBytes != sizeof(file->payload) ||
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_SCHEMA_HASH ||
	    !SplitStatsBytesZero(file->reserved0, sizeof(file->reserved0)) ||
	    !SplitStatsBytesZero(file->reserved1, sizeof(file->reserved1)) ||
	    !SplitStatsBytesZero(file->tailPad, sizeof(file->tailPad)) ||
	    file->checksum != SplitStatsChecksum(file) ||
	    !SplitStatsPayloadValid(&file->payload))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static bool SplitStatsV6PayloadValid(
	const struct SusamuneSplitStatsPayloadV6 *payload)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT; route++)
		{
			if (payload->routeStats[region][route].finishes >
			    payload->routeStats[region][route].attempts)
				return false;
		}
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V6_SEGMENT_COUNT;
		     segment++)
			if (!SplitStatsQfValid(payload->bestQf[region][segment]))
				return false;

		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT;
			     route++)
			{
				const u32 identity =
					payload->pbIdentityQf[region][profile][route];
				const u32 first = SplitV6RouteFirst[route];
				const u32 end = first + SplitV6RouteCount[route];
				if (!SplitStatsQfValid(identity))
					return false;
				for (segment = first; segment < end; segment++)
					if (!SplitStatsQfValid(
					        payload->pbQf[region][profile][segment]))
						return false;
			}
		}
	}
	return true;
}

static u32 SplitStatsV6Checksum(
	const struct SusamuneSplitStatsFileV6 *file)
{
	const struct SusamuneSplitStatsPayloadV6 *payload = &file->payload;
	u32 hash = 2166136261u;
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->routeCount << 8) |
	                         file->regionCount);
	hash = PbHashWord(hash, ((u32)file->segmentCount << 16) |
	                         ((u32)file->profileCount << 8) |
	                         file->headerReserved);
	hash = PbHashWord(hash, file->payloadBytes);
	hash = PbHashWord(hash, file->schemaHash);
	hash = PbHashWord(hash, file->generation);
	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT; route++)
		{
			const struct SusamuneSplitRouteStats *stats =
				&payload->routeStats[region][route];
			hash = PbHashWord(hash, stats->attempts);
			hash = PbHashWord(hash, stats->finishes);
			hash = PbHashWord(hash, stats->golds);
		}
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT; route++)
			hash = PbHashWord(hash, payload->playedQf[region][route]);
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V6_SEGMENT_COUNT;
		     segment++)
			hash = PbHashWord(hash, payload->bestQf[region][segment]);
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT;
			     route++)
				hash = PbHashWord(
					hash,
					payload->pbIdentityQf[region][profile][route]);
			for (segment = 0;
			     segment < SUSAMUNE_SPLIT_STATS_V6_SEGMENT_COUNT; segment++)
				hash = PbHashWord(
					hash, payload->pbQf[region][profile][segment]);
		}
	}
	return hash;
}

static enum PbReadResult ReadSplitStatsV6File(
	const char *path, struct SusamuneSplitStatsFileV6 *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = __builtin_offsetof(
		struct SusamuneSplitStatsFileV6, generation);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
		    (file->version != SUSAMUNE_SPLIT_STATS_VERSION_V6 ||
		     file->schemaHash != SUSAMUNE_SPLIT_STATS_V6_SCHEMA_HASH))
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version != SUSAMUNE_SPLIT_STATS_VERSION_V6)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version == SUSAMUNE_SPLIT_STATS_VERSION_V6 &&
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V6_SCHEMA_HASH)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_SPLIT_STATS_FILE_MAGIC ||
	    file->routeCount != SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT ||
	    file->segmentCount != SUSAMUNE_SPLIT_STATS_V6_SEGMENT_COUNT ||
	    file->regionCount != SUSAMUNE_SPLIT_STATS_REGION_COUNT ||
	    file->profileCount != SUSAMUNE_SPLIT_STATS_PROFILE_COUNT ||
	    file->headerReserved != 0 ||
	    file->payloadBytes != sizeof(file->payload) ||
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V6_SCHEMA_HASH ||
	    !SplitStatsBytesZero(file->reserved0, sizeof(file->reserved0)) ||
	    !SplitStatsBytesZero(file->reserved1, sizeof(file->reserved1)) ||
	    !SplitStatsBytesZero(file->tailPad, sizeof(file->tailPad)) ||
	    file->checksum != SplitStatsV6Checksum(file) ||
	    !SplitStatsV6PayloadValid(&file->payload))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static void MigrateSplitStatsV6(
	struct SusamuneSplitStatsPayload *dst,
	const struct SusamuneSplitStatsPayloadV6 *src)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 local;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V6_ROUTE_COUNT; route++)
		{
			const bool b2 = route == 13;
			dst->routeStats[region][route] = src->routeStats[region][route];
			dst->playedQf[region][route] = src->playedQf[region][route];
			if (b2)
				dst->routeStats[region][route].golds = 0;
			for (local = b2 ? 1u : 0u;
			     local < SplitV6RouteCount[route]; local++)
			{
				const u32 newLocal = b2 ? local + 1 : local;
				dst->bestQf[region][SplitRouteFirst[route] + newLocal] =
					src->bestQf[region]
					           [SplitV6RouteFirst[route] + local];
			}

			for (profile = 0;
			     profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT; profile++)
			{
				if (b2)
					continue;
				dst->pbIdentityQf[region][profile][route] =
					src->pbIdentityQf[region][profile][route];
				for (local = 0;
				     local < SplitV6RouteCount[route]; local++)
				{
					dst->pbQf[region][profile]
					         [SplitRouteFirst[route] + local] =
						src->pbQf[region][profile]
							         [SplitV6RouteFirst[route] + local];
				}
			}
		}
	}
}

static bool SplitStatsV5PayloadValid(
	const struct SusamuneSplitStatsPayloadV5 *payload)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT;
		     route++)
			if (payload->routeStats[region][route].finishes >
			    payload->routeStats[region][route].attempts)
				return false;
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V5_SEGMENT_COUNT;
		     segment++)
			if (!SplitStatsQfValid(payload->bestQf[region][segment]))
				return false;
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT;
			     route++)
			{
				const u32 identity =
					payload->pbIdentityQf[region][profile][route];
				const u32 first = SplitV5RouteFirst[route];
				const u32 end = first + SplitV5RouteCount[route];
				if (!SplitStatsQfValid(identity))
					return false;
				for (segment = first; segment < end; segment++)
					if (!SplitStatsQfValid(
					        payload->pbQf[region][profile][segment]))
						return false;
			}
		}
	}
	return true;
}

static u32 SplitStatsV5Checksum(
	const struct SusamuneSplitStatsFileV5 *file)
{
	const struct SusamuneSplitStatsPayloadV5 *payload = &file->payload;
	u32 hash = 2166136261u;
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->routeCount << 8) |
	                         file->regionCount);
	hash = PbHashWord(hash, ((u32)file->segmentCount << 16) |
	                         ((u32)file->profileCount << 8) |
	                         file->headerReserved);
	hash = PbHashWord(hash, file->payloadBytes);
	hash = PbHashWord(hash, file->schemaHash);
	hash = PbHashWord(hash, file->generation);
	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT;
		     route++)
		{
			const struct SusamuneSplitRouteStats *stats =
				&payload->routeStats[region][route];
			hash = PbHashWord(hash, stats->attempts);
			hash = PbHashWord(hash, stats->finishes);
			hash = PbHashWord(hash, stats->golds);
		}
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V5_SEGMENT_COUNT;
		     segment++)
			hash = PbHashWord(hash, payload->bestQf[region][segment]);
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT;
			     route++)
				hash = PbHashWord(
					hash,
					payload->pbIdentityQf[region][profile][route]);
			for (segment = 0;
			     segment < SUSAMUNE_SPLIT_STATS_V5_SEGMENT_COUNT; segment++)
				hash = PbHashWord(
					hash, payload->pbQf[region][profile][segment]);
		}
	}
	return hash;
}

static enum PbReadResult ReadSplitStatsV5File(
	const char *path, struct SusamuneSplitStatsFileV5 *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = __builtin_offsetof(
		struct SusamuneSplitStatsFileV5, generation);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
		    (file->version != SUSAMUNE_SPLIT_STATS_VERSION_V5 ||
		     file->schemaHash != SUSAMUNE_SPLIT_STATS_V5_SCHEMA_HASH))
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version != SUSAMUNE_SPLIT_STATS_VERSION_V5)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version == SUSAMUNE_SPLIT_STATS_VERSION_V5 &&
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V5_SCHEMA_HASH)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_SPLIT_STATS_FILE_MAGIC ||
	    file->routeCount != SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT ||
	    file->segmentCount != SUSAMUNE_SPLIT_STATS_V5_SEGMENT_COUNT ||
	    file->regionCount != SUSAMUNE_SPLIT_STATS_REGION_COUNT ||
	    file->profileCount != SUSAMUNE_SPLIT_STATS_PROFILE_COUNT ||
	    file->headerReserved != 0 ||
	    file->payloadBytes != sizeof(file->payload) ||
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V5_SCHEMA_HASH ||
	    !SplitStatsBytesZero(file->reserved0, sizeof(file->reserved0)) ||
	    !SplitStatsBytesZero(file->reserved1, sizeof(file->reserved1)) ||
	    !SplitStatsBytesZero(file->tailPad, sizeof(file->tailPad)) ||
	    file->checksum != SplitStatsV5Checksum(file) ||
	    !SplitStatsV5PayloadValid(&file->payload))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static bool SplitStatsV5RouteBecameTerminal(u32 route)
{
	return route == 10 || route == 31;
}

static void MigrateSplitStatsV5(
	struct SusamuneSplitStatsPayload *dst,
	const struct SusamuneSplitStatsPayloadV5 *src)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 local;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V5_ROUTE_COUNT;
		     route++)
		{
			const bool b2 = route == 13;
			dst->routeStats[region][route].attempts =
				src->routeStats[region][route].attempts;
			dst->routeStats[region][route].finishes =
				src->routeStats[region][route].finishes;
			if (!SplitStatsV5RouteBecameTerminal(route) && !b2)
			{
				dst->routeStats[region][route].golds =
					src->routeStats[region][route].golds;
			}
			for (local = b2 ? 1u : 0u;
			     !SplitStatsV5RouteBecameTerminal(route) &&
			     local < SplitV5RouteCount[route]; local++)
			{
				const u32 newLocal = b2 ? local + 1 : local;
				dst->bestQf[region][SplitRouteFirst[route] + newLocal] =
					src->bestQf[region]
					           [SplitV5RouteFirst[route] + local];
			}

			for (profile = 0;
			     profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT; profile++)
			{
				if (b2)
					continue;
				const u32 identity =
					src->pbIdentityQf[region][profile][route];
				dst->pbIdentityQf[region][profile][route] = identity;
				if (SplitStatsV5RouteBecameTerminal(route))
				{
					dst->pbQf[region][profile]
					          [SplitRouteFirst[route]] = identity;
					continue;
				}
				for (local = 0;
				     local < SplitV5RouteCount[route]; local++)
				{
					dst->pbQf[region][profile]
					          [SplitRouteFirst[route] + local] =
						src->pbQf[region][profile]
						         [SplitV5RouteFirst[route] + local];
				}
			}
		}
	}
}

static bool SplitStatsV4PayloadValid(
	const struct SusamuneSplitStatsPayloadV4 *payload)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT;
		     route++)
			if (payload->routeStats[region][route].finishes >
			    payload->routeStats[region][route].attempts)
				return false;
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V4_SEGMENT_COUNT;
		     segment++)
			if (!SplitStatsQfValid(payload->bestQf[region][segment]))
				return false;
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT;
			     route++)
			{
				const u32 identity =
					payload->pbIdentityQf[region][profile][route];
				const u32 first = SplitV4RouteFirst[route];
				const u32 end = first + SplitV4RouteCount[route];
				if (!SplitStatsQfValid(identity))
					return false;
				for (segment = first; segment < end; segment++)
					if (!SplitStatsQfValid(
					        payload->pbQf[region][profile][segment]))
						return false;
			}
		}
	}
	return true;
}

static u32 SplitStatsV4Checksum(
	const struct SusamuneSplitStatsFileV4 *file)
{
	const struct SusamuneSplitStatsPayloadV4 *payload = &file->payload;
	u32 hash = 2166136261u;
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->routeCount << 8) |
	                         file->regionCount);
	hash = PbHashWord(hash, ((u32)file->segmentCount << 16) |
	                         ((u32)file->profileCount << 8) |
	                         file->headerReserved);
	hash = PbHashWord(hash, file->payloadBytes);
	hash = PbHashWord(hash, file->schemaHash);
	hash = PbHashWord(hash, file->generation);
	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT;
		     route++)
		{
			const struct SusamuneSplitRouteStats *stats =
				&payload->routeStats[region][route];
			hash = PbHashWord(hash, stats->attempts);
			hash = PbHashWord(hash, stats->finishes);
			hash = PbHashWord(hash, stats->golds);
		}
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V4_SEGMENT_COUNT;
		     segment++)
			hash = PbHashWord(hash, payload->bestQf[region][segment]);
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT;
			     route++)
				hash = PbHashWord(
					hash,
					payload->pbIdentityQf[region][profile][route]);
			for (segment = 0;
			     segment < SUSAMUNE_SPLIT_STATS_V4_SEGMENT_COUNT; segment++)
				hash = PbHashWord(
					hash, payload->pbQf[region][profile][segment]);
		}
	}
	return hash;
}

static enum PbReadResult ReadSplitStatsV4File(
	const char *path, struct SusamuneSplitStatsFileV4 *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = __builtin_offsetof(
		struct SusamuneSplitStatsFileV4, generation);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
		    (file->version != SUSAMUNE_SPLIT_STATS_VERSION_V4 ||
		     file->schemaHash != SUSAMUNE_SPLIT_STATS_V4_SCHEMA_HASH))
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version != SUSAMUNE_SPLIT_STATS_VERSION_V4)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version == SUSAMUNE_SPLIT_STATS_VERSION_V4 &&
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V4_SCHEMA_HASH)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_SPLIT_STATS_FILE_MAGIC ||
	    file->routeCount != SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT ||
	    file->segmentCount != SUSAMUNE_SPLIT_STATS_V4_SEGMENT_COUNT ||
	    file->regionCount != SUSAMUNE_SPLIT_STATS_REGION_COUNT ||
	    file->profileCount != SUSAMUNE_SPLIT_STATS_PROFILE_COUNT ||
	    file->headerReserved != 0 ||
	    file->payloadBytes != sizeof(file->payload) ||
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V4_SCHEMA_HASH ||
	    !SplitStatsBytesZero(file->reserved0, sizeof(file->reserved0)) ||
	    !SplitStatsBytesZero(file->reserved1, sizeof(file->reserved1)) ||
	    !SplitStatsBytesZero(file->tailPad, sizeof(file->tailPad)) ||
	    file->checksum != SplitStatsV4Checksum(file) ||
	    !SplitStatsV4PayloadValid(&file->payload))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static bool SplitStatsV4RemovedCheckpoint(u32 route, u32 *local)
{
	if (route == 41)
	{
		*local = 0;
		return true;
	}
	if (route == 40)
	{
		*local = 2;
		return true;
	}
	return false;
}

static u32 SplitStatsMergeQf(u32 first, u32 second)
{
	if (first == SUSAMUNE_SPLIT_STATS_QF_UNSET ||
	    second == SUSAMUNE_SPLIT_STATS_QF_UNSET ||
	    first > (u32)SUSAMUNE_ILING_PB_MAX_QF - second)
		return SUSAMUNE_SPLIT_STATS_QF_UNSET;
	return first + second;
}

static void MigrateSplitStatsV4(
	struct SusamuneSplitStatsPayload *dst,
	const struct SusamuneSplitStatsPayloadV4 *src)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 local;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V4_ROUTE_COUNT;
		     route++)
		{
			u32 removed = 0;
			const bool changed =
				SplitStatsV4RemovedCheckpoint(route, &removed);
			const bool terminal = SplitStatsV5RouteBecameTerminal(route);
			dst->routeStats[region][route].attempts =
				src->routeStats[region][route].attempts;
			dst->routeStats[region][route].finishes =
				src->routeStats[region][route].finishes;
			if (!changed && !terminal)
				dst->routeStats[region][route].golds =
					src->routeStats[region][route].golds;

			for (local = 0; !terminal &&
			                local < SplitRouteCount[route]; local++)
			{
				u32 oldLocal = local;
				if (changed && local == removed)
					continue;
				if (changed && local > removed)
					oldLocal++;
				dst->bestQf[region][SplitRouteFirst[route] + local] =
					src->bestQf[region]
					           [SplitV4RouteFirst[route] + oldLocal];
			}

			for (profile = 0;
			     profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT; profile++)
			{
				const u32 identity =
					src->pbIdentityQf[region][profile][route];
				dst->pbIdentityQf[region][profile][route] = identity;
				if (terminal)
				{
					dst->pbQf[region][profile]
					          [SplitRouteFirst[route]] = identity;
					continue;
				}
				for (local = 0; local < SplitRouteCount[route]; local++)
				{
					u32 value;
					if (changed && local == removed)
					{
						const u32 first = SplitV4RouteFirst[route] +
						                  removed;
						value = SplitStatsMergeQf(
							src->pbQf[region][profile][first],
							src->pbQf[region][profile][first + 1]);
					}
					else
					{
						u32 oldLocal = local;
						if (changed && local > removed)
							oldLocal++;
						value = src->pbQf[region][profile]
						                 [SplitV4RouteFirst[route] +
						                  oldLocal];
					}
					dst->pbQf[region][profile]
					         [SplitRouteFirst[route] + local] = value;
				}
			}
		}
	}
}

static bool SplitStatsV3PayloadValid(
	const struct SusamuneSplitStatsPayloadV3 *payload)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT;
		     route++)
			if (payload->routeStats[region][route].finishes >
			    payload->routeStats[region][route].attempts)
				return false;
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V3_SEGMENT_COUNT;
		     segment++)
			if (!SplitStatsQfValid(payload->bestQf[region][segment]))
				return false;
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT;
			     route++)
			{
				const u32 identity =
					payload->pbIdentityQf[region][profile][route];
				const u32 first = SplitV3RouteFirst[route];
				const u32 end = first + SplitV3RouteCount[route];
				if (!SplitStatsQfValid(identity))
					return false;
				for (segment = first; segment < end; segment++)
					if (!SplitStatsQfValid(
					        payload->pbQf[region][profile][segment]))
						return false;
			}
		}
	}
	return true;
}

static u32 SplitStatsV3Checksum(
	const struct SusamuneSplitStatsFileV3 *file)
{
	const struct SusamuneSplitStatsPayloadV3 *payload = &file->payload;
	u32 hash = 2166136261u;
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->routeCount << 8) |
	                         file->regionCount);
	hash = PbHashWord(hash, ((u32)file->segmentCount << 16) |
	                         ((u32)file->profileCount << 8) |
	                         file->headerReserved);
	hash = PbHashWord(hash, file->payloadBytes);
	hash = PbHashWord(hash, file->schemaHash);
	hash = PbHashWord(hash, file->generation);
	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT;
		     route++)
		{
			const struct SusamuneSplitRouteStats *stats =
				&payload->routeStats[region][route];
			hash = PbHashWord(hash, stats->attempts);
			hash = PbHashWord(hash, stats->finishes);
			hash = PbHashWord(hash, stats->golds);
		}
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V3_SEGMENT_COUNT;
		     segment++)
			hash = PbHashWord(hash, payload->bestQf[region][segment]);
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT;
			     route++)
				hash = PbHashWord(
					hash,
					payload->pbIdentityQf[region][profile][route]);
			for (segment = 0;
			     segment < SUSAMUNE_SPLIT_STATS_V3_SEGMENT_COUNT; segment++)
				hash = PbHashWord(
					hash, payload->pbQf[region][profile][segment]);
		}
	}
	return hash;
}

static enum PbReadResult ReadSplitStatsV3File(
	const char *path, struct SusamuneSplitStatsFileV3 *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = __builtin_offsetof(
		struct SusamuneSplitStatsFileV3, generation);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
		    (file->version != SUSAMUNE_SPLIT_STATS_VERSION_V3 ||
		     file->schemaHash != SUSAMUNE_SPLIT_STATS_V3_SCHEMA_HASH))
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version != SUSAMUNE_SPLIT_STATS_VERSION_V3)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version == SUSAMUNE_SPLIT_STATS_VERSION_V3 &&
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V3_SCHEMA_HASH)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_SPLIT_STATS_FILE_MAGIC ||
	    file->routeCount != SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT ||
	    file->segmentCount != SUSAMUNE_SPLIT_STATS_V3_SEGMENT_COUNT ||
	    file->regionCount != SUSAMUNE_SPLIT_STATS_REGION_COUNT ||
	    file->profileCount != SUSAMUNE_SPLIT_STATS_PROFILE_COUNT ||
	    file->headerReserved != 0 ||
	    file->payloadBytes != sizeof(file->payload) ||
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V3_SCHEMA_HASH ||
	    !SplitStatsBytesZero(file->reserved0, sizeof(file->reserved0)) ||
	    !SplitStatsBytesZero(file->reserved1, sizeof(file->reserved1)) ||
	    !SplitStatsBytesZero(file->tailPad, sizeof(file->tailPad)) ||
	    file->checksum != SplitStatsV3Checksum(file) ||
	    !SplitStatsV3PayloadValid(&file->payload))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static void MigrateSplitStatsV3(
	struct SusamuneSplitStatsPayloadV4 *dst,
	const struct SusamuneSplitStatsPayloadV3 *src)
{
	static const u8 changedRoutes[] = {5, 34, 36, 37, 46, 48, 49, 57};
	u32 region;
	u32 profile;
	u32 route;
	u32 local;
	u32 changed;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V3_ROUTE_COUNT;
		     route++)
		{
			bool reset = false;
			for (changed = 0;
			     changed < sizeof(changedRoutes) / sizeof(changedRoutes[0]);
			     changed++)
				if (route == changedRoutes[changed])
					reset = true;

			dst->routeStats[region][route].attempts =
				src->routeStats[region][route].attempts;
			dst->routeStats[region][route].finishes =
				src->routeStats[region][route].finishes;
			if (reset)
				continue;

			dst->routeStats[region][route].golds =
				src->routeStats[region][route].golds;
			for (local = 0; local < SplitV3RouteCount[route]; local++)
				dst->bestQf[region][SplitV4RouteFirst[route] + local] =
					src->bestQf[region]
					           [SplitV3RouteFirst[route] + local];
			for (profile = 0;
			     profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT; profile++)
			{
				dst->pbIdentityQf[region][profile][route] =
					src->pbIdentityQf[region][profile][route];
				for (local = 0; local < SplitV3RouteCount[route]; local++)
					dst->pbQf[region][profile]
					          [SplitV4RouteFirst[route] + local] =
						src->pbQf[region][profile]
						          [SplitV3RouteFirst[route] + local];
			}
		}
	}
}

static bool SplitStatsV2PayloadValid(
	const struct SusamuneSplitStatsPayloadV2 *payload)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT; route++)
			if (payload->routeStats[region][route].finishes >
			    payload->routeStats[region][route].attempts)
				return false;
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V2_SEGMENT_COUNT;
		     segment++)
			if (!SplitStatsQfValid(payload->bestQf[region][segment]))
				return false;
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT;
			     route++)
			{
				const u32 identity =
					payload->pbIdentityQf[region][profile][route];
				const u32 first = SplitV2RouteFirst[route];
				const u32 end = first + SplitV2RouteCount[route];
				if (!SplitStatsQfValid(identity))
					return false;
				for (segment = first; segment < end; segment++)
					if (!SplitStatsQfValid(
					        payload->pbQf[region][profile][segment]))
						return false;
			}
		}
	}
	return true;
}

static u32 SplitStatsV2Checksum(const struct SusamuneSplitStatsFileV2 *file)
{
	const struct SusamuneSplitStatsPayloadV2 *payload = &file->payload;
	u32 hash = 2166136261u;
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->routeCount << 8) |
	                         file->segmentCount);
	hash = PbHashWord(hash, ((u32)file->regionCount << 24) |
	                         ((u32)file->profileCount << 16) |
	                         file->payloadBytes);
	hash = PbHashWord(hash, file->schemaHash);
	hash = PbHashWord(hash, file->generation);
	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT; route++)
		{
			const struct SusamuneSplitRouteStats *stats =
				&payload->routeStats[region][route];
			hash = PbHashWord(hash, stats->attempts);
			hash = PbHashWord(hash, stats->finishes);
			hash = PbHashWord(hash, stats->golds);
		}
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V2_SEGMENT_COUNT;
		     segment++)
			hash = PbHashWord(hash, payload->bestQf[region][segment]);
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT;
			     route++)
				hash = PbHashWord(
					hash,
					payload->pbIdentityQf[region][profile][route]);
			for (segment = 0;
			     segment < SUSAMUNE_SPLIT_STATS_V2_SEGMENT_COUNT; segment++)
				hash = PbHashWord(
					hash, payload->pbQf[region][profile][segment]);
		}
	}
	return hash;
}

static enum PbReadResult ReadSplitStatsV2File(
	const char *path, struct SusamuneSplitStatsFileV2 *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = __builtin_offsetof(
		struct SusamuneSplitStatsFileV2, generation);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
		    (file->version != SUSAMUNE_SPLIT_STATS_VERSION_V2 ||
		     (file->schemaHash != SUSAMUNE_SPLIT_STATS_V2_SCHEMA_HASH &&
		      file->schemaHash != SUSAMUNE_SPLIT_STATS_PR7_SCHEMA_HASH)))
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version != SUSAMUNE_SPLIT_STATS_VERSION_V2)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version == SUSAMUNE_SPLIT_STATS_VERSION_V2 &&
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V2_SCHEMA_HASH &&
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_PR7_SCHEMA_HASH)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_SPLIT_STATS_FILE_MAGIC ||
	    file->routeCount != SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT ||
	    file->segmentCount != SUSAMUNE_SPLIT_STATS_V2_SEGMENT_COUNT ||
	    file->regionCount != SUSAMUNE_SPLIT_STATS_REGION_COUNT ||
	    file->profileCount != SUSAMUNE_SPLIT_STATS_PROFILE_COUNT ||
	    file->payloadBytes != sizeof(file->payload) ||
	    (file->schemaHash != SUSAMUNE_SPLIT_STATS_V2_SCHEMA_HASH &&
	     file->schemaHash != SUSAMUNE_SPLIT_STATS_PR7_SCHEMA_HASH) ||
	    !SplitStatsBytesZero(file->reserved0, sizeof(file->reserved0)) ||
	    !SplitStatsBytesZero(file->reserved1, sizeof(file->reserved1)) ||
	    file->checksum != SplitStatsV2Checksum(file) ||
	    !SplitStatsV2PayloadValid(&file->payload))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static void MigrateSplitStatsPr7(struct SusamuneSplitStatsPayloadV2 *payload)
{
	static const u8 changedRoutes[] = {4, 5, 6};
	u32 region;
	u32 profile;
	u32 changed;
	u32 segment;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (changed = 0;
		     changed < sizeof(changedRoutes) / sizeof(changedRoutes[0]);
		     changed++)
		{
			const u32 route = changedRoutes[changed];
			const u32 first = SplitV2RouteFirst[route];
			const u32 end = first + SplitV2RouteCount[route];

			/* Attempts and finishes still describe the same full IL. */
			payload->routeStats[region][route].golds = 0;
			for (segment = first; segment < end; segment++)
				payload->bestQf[region][segment] =
					SUSAMUNE_SPLIT_STATS_QF_UNSET;
			for (profile = 0;
			     profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT; profile++)
			{
				payload->pbIdentityQf[region][profile][route] =
					SUSAMUNE_SPLIT_STATS_QF_UNSET;
				for (segment = first; segment < end; segment++)
					payload->pbQf[region][profile][segment] =
						SUSAMUNE_SPLIT_STATS_QF_UNSET;
			}
		}
	}
}

static void MigrateSplitStatsV2(
	struct SusamuneSplitStatsPayloadV4 *dst,
	const struct SusamuneSplitStatsPayloadV2 *src)
{
	static const u8 changedRoutes[] = {1, 2, 5};
	u32 region;
	u32 profile;
	u32 route;
	u32 local;
	u32 changed;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V2_ROUTE_COUNT;
		     route++)
		{
			bool reset = false;
			for (changed = 0;
			     changed < sizeof(changedRoutes) / sizeof(changedRoutes[0]);
			     changed++)
				if (route == changedRoutes[changed])
					reset = true;

			dst->routeStats[region][route].attempts =
				src->routeStats[region][route].attempts;
			dst->routeStats[region][route].finishes =
				src->routeStats[region][route].finishes;
			if (reset)
				continue;

			dst->routeStats[region][route].golds =
				src->routeStats[region][route].golds;
			for (local = 0; local < SplitV2RouteCount[route]; local++)
				dst->bestQf[region][SplitV4RouteFirst[route] + local] =
					src->bestQf[region]
					           [SplitV2RouteFirst[route] + local];
			for (profile = 0;
			     profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT; profile++)
			{
				dst->pbIdentityQf[region][profile][route] =
					src->pbIdentityQf[region][profile][route];
				for (local = 0; local < SplitV2RouteCount[route]; local++)
					dst->pbQf[region][profile]
					          [SplitV4RouteFirst[route] + local] =
						src->pbQf[region][profile]
						          [SplitV2RouteFirst[route] + local];
			}
		}
	}
}

static bool SplitStatsV1PayloadValid(
	const struct SusamuneSplitStatsPayloadV1 *payload)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT; route++)
			if (payload->routeStats[region][route].finishes >
			    payload->routeStats[region][route].attempts)
				return false;
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V1_SEGMENT_COUNT;
		     segment++)
			if (!SplitStatsQfValid(payload->bestQf[region][segment]))
				return false;
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT;
			     route++)
			{
				const u32 identity =
					payload->pbIdentityQf[region][profile][route];
				const u32 first = SplitV1RouteFirst[route];
				const u32 end = first + SplitV1RouteCount[route];
				if (!SplitStatsQfValid(identity))
					return false;
				for (segment = first; segment < end; segment++)
				{
					const u32 value =
						payload->pbQf[region][profile][segment];
					if (!SplitStatsQfValid(value) ||
					    (identity == SUSAMUNE_SPLIT_STATS_QF_UNSET) !=
					        (value == SUSAMUNE_SPLIT_STATS_QF_UNSET))
						return false;
				}
			}
		}
	}
	return true;
}

static u32 SplitStatsV1Checksum(const struct SusamuneSplitStatsFileV1 *file)
{
	const struct SusamuneSplitStatsPayloadV1 *payload = &file->payload;
	u32 hash = 2166136261u;
	u32 region;
	u32 profile;
	u32 route;
	u32 segment;

	hash = PbHashWord(hash, ((u32)file->version << 16) |
	                         ((u32)file->routeCount << 8) |
	                         file->segmentCount);
	hash = PbHashWord(hash, ((u32)file->regionCount << 24) |
	                         ((u32)file->profileCount << 16) |
	                         file->payloadBytes);
	hash = PbHashWord(hash, file->schemaHash);
	hash = PbHashWord(hash, file->generation);
	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT; route++)
		{
			const struct SusamuneSplitRouteStats *stats =
				&payload->routeStats[region][route];
			hash = PbHashWord(hash, stats->attempts);
			hash = PbHashWord(hash, stats->finishes);
			hash = PbHashWord(hash, stats->golds);
		}
		for (segment = 0; segment < SUSAMUNE_SPLIT_STATS_V1_SEGMENT_COUNT;
		     segment++)
			hash = PbHashWord(hash, payload->bestQf[region][segment]);
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT;
			     route++)
				hash = PbHashWord(
					hash,
					payload->pbIdentityQf[region][profile][route]);
			for (segment = 0;
			     segment < SUSAMUNE_SPLIT_STATS_V1_SEGMENT_COUNT; segment++)
				hash = PbHashWord(
					hash, payload->pbQf[region][profile][segment]);
		}
	}
	return hash;
}

static enum PbReadResult ReadSplitStatsV1File(
	const char *path, struct SusamuneSplitStatsFileV1 *file)
{
	FIL f;
	UINT read = 0;
	u32 size;
	u32 prefixSize = sizeof(file->magic) + sizeof(file->version);
	int ret;
	int closeRet;

	ret = f_open_char(&f, path, FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_NO_FILE || ret == FR_NO_PATH)
		return PB_READ_INVALID;
	if (ret != FR_OK)
		return PB_READ_UNSAFE;
	size = (u32)f_size(&f);
	if (size != sizeof(*file))
	{
		memset(file, 0, sizeof(*file));
		if (size >= prefixSize)
			ret = f_read(&f, file, prefixSize, &read);
		closeRet = f_close(&f);
		if (ret != FR_OK || closeRet != FR_OK ||
		    (size >= prefixSize && read != prefixSize))
			return PB_READ_UNSAFE;
		if (size >= prefixSize &&
		    file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
		    file->version != SUSAMUNE_SPLIT_STATS_VERSION_V1)
			return PB_READ_UNSAFE;
		return PB_READ_INVALID;
	}
	ret = f_read(&f, file, sizeof(*file), &read);
	closeRet = f_close(&f);
	if (ret != FR_OK || read != sizeof(*file) || closeRet != FR_OK)
		return PB_READ_UNSAFE;
	if (file->magic == SUSAMUNE_SPLIT_STATS_FILE_MAGIC &&
	    file->version != SUSAMUNE_SPLIT_STATS_VERSION_V1)
		return PB_READ_UNSAFE;
	if (file->magic != SUSAMUNE_SPLIT_STATS_FILE_MAGIC ||
	    file->routeCount != SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT ||
	    file->segmentCount != SUSAMUNE_SPLIT_STATS_V1_SEGMENT_COUNT ||
	    file->regionCount != SUSAMUNE_SPLIT_STATS_REGION_COUNT ||
	    file->profileCount != SUSAMUNE_SPLIT_STATS_PROFILE_COUNT ||
	    file->payloadBytes != sizeof(file->payload) ||
	    file->schemaHash != SUSAMUNE_SPLIT_STATS_V1_SCHEMA_HASH ||
	    !SplitStatsBytesZero(file->reserved0, sizeof(file->reserved0)) ||
	    !SplitStatsBytesZero(file->reserved1, sizeof(file->reserved1)) ||
	    file->checksum != SplitStatsV1Checksum(file) ||
	    !SplitStatsV1PayloadValid(&file->payload))
		return PB_READ_INVALID;
	return PB_READ_VALID;
}

static void MigrateSplitStatsV1(
	struct SusamuneSplitStatsPayloadV4 *dst,
	const struct SusamuneSplitStatsPayloadV1 *src)
{
	u32 region;
	u32 profile;
	u32 route;
	u32 local;

	/* V1 identity named the ordinary IL PB, not this split set. */
	for (region = 0; region < SUSAMUNE_SPLIT_STATS_REGION_COUNT; region++)
	{
		/* B4's coin-5 edit invalidates both segments after coin 1. */
		for (route = 0; route < SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT; route++)
		{
			dst->routeStats[region][route] = src->routeStats[region][route];
			dst->routeStats[region][route].golds = 0;
			for (local = 0; local < SplitV1RouteCount[route] &&
			                route == 0 && local == 0; local++)
				dst->bestQf[region][SplitV4RouteFirst[route] + local] =
					src->bestQf[region][SplitV1RouteFirst[route] + local];
		}
		for (profile = 0; profile < SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
		     profile++)
		{
			for (route = 0; route < SUSAMUNE_SPLIT_STATS_V1_ROUTE_COUNT;
			     route++)
			{
				for (local = 0; local < SplitV1RouteCount[route] &&
				                route == 0 && local == 0; local++)
					dst->pbQf[region][profile]
					         [SplitV4RouteFirst[route] + local] =
						src->pbQf[region][profile]
						         [SplitV1RouteFirst[route] + local];
			}
		}
	}
}

static void ResetSplitStatsPayload(struct SusamuneSplitStatsPayload *payload)
{
	memset(&payload->routeStats, 0, sizeof(payload->routeStats));
	memset(&payload->playedQf, 0, sizeof(payload->playedQf));
	memset(&payload->bestQf, 0xff,
	       sizeof(*payload) -
	           __builtin_offsetof(struct SusamuneSplitStatsPayload, bestQf));
}

static void ResetSplitStatsV4Payload(
	struct SusamuneSplitStatsPayloadV4 *payload)
{
	memset(&payload->routeStats, 0, sizeof(payload->routeStats));
	memset(&payload->bestQf, 0xff,
	       sizeof(*payload) - sizeof(payload->routeStats));
}

static void InitSplitStatsDefaults(struct SusamuneSplitStatsCfg *stats)
{

	memset(stats, 0, sizeof(*stats));
	stats->magic = SUSAMUNE_SPLIT_STATS_MAGIC;
	stats->version = SUSAMUNE_SPLIT_STATS_VERSION;
	stats->routeCount = SUSAMUNE_SPLIT_STATS_ROUTE_COUNT;
	stats->segmentCount = SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT;
	stats->regionCount = SUSAMUNE_SPLIT_STATS_REGION_COUNT;
	stats->profileCount = SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
	stats->payloadBytes = sizeof(stats->payload);
	stats->schemaHash = SUSAMUNE_SPLIT_STATS_SCHEMA_HASH;
	ResetSplitStatsPayload(&stats->payload);
}

static bool InitSplitStatsFiles(struct SusamuneSplitStatsCfg *stats)
{
	struct SusamuneSplitStatsFile *file = &SplitStatsFileScratch.current;
	u32 fileIndex;
	bool safe = true;
	bool migrated = false;

	InitSplitStatsDefaults(stats);
	_sprintf(SplitStatsPaths[0], "%s/susamune_il_stats_v7_a.bin",
	         SusamuneCfgStoragePrefix());
	_sprintf(SplitStatsPaths[1], "%s/susamune_il_stats_v7_b.bin",
	         SusamuneCfgStoragePrefix());
	SplitStatsGeneration = 0;
	SplitStatsActiveFile = -1;
	for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
	{
		enum PbReadResult readResult =
			ReadSplitStatsFile(SplitStatsPaths[fileIndex], file);
		if (readResult == PB_READ_UNSAFE)
		{
			safe = false;
			continue;
		}
		if (readResult != PB_READ_VALID)
			continue;
		if (SplitStatsActiveFile >= 0 &&
		    file->generation == SplitStatsGeneration)
		{
			if (memcmp(&file->payload, &stats->payload,
			           sizeof(file->payload)) != 0)
				safe = false;
			continue;
		}
		if (SplitStatsActiveFile >= 0 &&
		    !PbGenerationIsNewer(file->generation, SplitStatsGeneration))
			continue;

		memcpy(&stats->payload, &file->payload, sizeof(stats->payload));
		SplitStatsGeneration = file->generation;
		SplitStatsActiveFile = (s32)fileIndex;
	}

	if (safe && SplitStatsActiveFile < 0)
	{
		char v6Paths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
		struct SusamuneSplitStatsFileV6 *v6 = &SplitStatsFileScratch.v6;
		struct SusamuneSplitStatsPayloadV6 *selectedPayload =
			(struct SusamuneSplitStatsPayloadV6 *)&stats->payload;
		u32 selectedGeneration = 0;
		s32 selectedFile = -1;

		_sprintf(v6Paths[0], "%s/susamune_il_stats_v6_a.bin",
		         SusamuneCfgStoragePrefix());
		_sprintf(v6Paths[1], "%s/susamune_il_stats_v6_b.bin",
		         SusamuneCfgStoragePrefix());
		for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
		{
			enum PbReadResult readResult =
				ReadSplitStatsV6File(v6Paths[fileIndex], v6);
			if (readResult == PB_READ_UNSAFE)
			{
				safe = false;
				continue;
			}
			if (readResult != PB_READ_VALID)
				continue;
			if (selectedFile >= 0 && v6->generation == selectedGeneration)
			{
				if (memcmp(&v6->payload, selectedPayload,
				           sizeof(*selectedPayload)) != 0)
					safe = false;
				continue;
			}
			if (selectedFile >= 0 &&
			    !PbGenerationIsNewer(v6->generation, selectedGeneration))
				continue;
			memcpy(selectedPayload, &v6->payload, sizeof(*selectedPayload));
			selectedGeneration = v6->generation;
			selectedFile = (s32)fileIndex;
		}
		if (safe && selectedFile >= 0)
		{
			enum PbReadResult readResult =
				ReadSplitStatsV6File(v6Paths[selectedFile], v6);
			if (readResult != PB_READ_VALID ||
			    v6->generation != selectedGeneration ||
			    memcmp(&v6->payload, selectedPayload,
			           sizeof(*selectedPayload)) != 0)
			{
				safe = false;
			}
			else
			{
				ResetSplitStatsPayload(&stats->payload);
				MigrateSplitStatsV6(&stats->payload, &v6->payload);
				SplitStatsGeneration = selectedGeneration;
				migrated = true;
			}
		}
	}

	if (safe && SplitStatsActiveFile < 0 && !migrated)
	{
		char v5Paths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
		struct SusamuneSplitStatsFileV5 *v5 = &SplitStatsFileScratch.v5;
		struct SusamuneSplitStatsPayloadV5 *selectedPayload =
			(struct SusamuneSplitStatsPayloadV5 *)&stats->payload;
		u32 selectedGeneration = 0;
		s32 selectedFile = -1;

		_sprintf(v5Paths[0], "%s/susamune_il_stats_v5_a.bin",
		         SusamuneCfgStoragePrefix());
		_sprintf(v5Paths[1], "%s/susamune_il_stats_v5_b.bin",
		         SusamuneCfgStoragePrefix());
		for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
		{
			enum PbReadResult readResult =
				ReadSplitStatsV5File(v5Paths[fileIndex], v5);
			if (readResult == PB_READ_UNSAFE)
			{
				safe = false;
				continue;
			}
			if (readResult != PB_READ_VALID)
				continue;
			if (selectedFile >= 0 && v5->generation == selectedGeneration)
			{
				if (memcmp(&v5->payload, selectedPayload,
				           sizeof(*selectedPayload)) != 0)
					safe = false;
				continue;
			}
			if (selectedFile >= 0 &&
			    !PbGenerationIsNewer(v5->generation, selectedGeneration))
				continue;
			memcpy(selectedPayload, &v5->payload, sizeof(*selectedPayload));
			selectedGeneration = v5->generation;
			selectedFile = (s32)fileIndex;
		}
		if (safe && selectedFile >= 0)
		{
			enum PbReadResult readResult =
				ReadSplitStatsV5File(v5Paths[selectedFile], v5);
			if (readResult != PB_READ_VALID ||
			    v5->generation != selectedGeneration ||
			    memcmp(&v5->payload, selectedPayload,
			           sizeof(*selectedPayload)) != 0)
			{
				safe = false;
			}
			else
			{
				ResetSplitStatsPayload(&stats->payload);
				MigrateSplitStatsV5(&stats->payload, &v5->payload);
				SplitStatsGeneration = selectedGeneration;
				migrated = true;
			}
		}
	}

	if (safe && SplitStatsActiveFile < 0 && !migrated)
	{
		char v4Paths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
		struct SusamuneSplitStatsFileV4 *v4 = &SplitStatsFileScratch.v4;
		u32 selectedGeneration = 0;
		u32 selectedChecksum = 0;
		s32 selectedFile = -1;

		_sprintf(v4Paths[0], "%s/susamune_il_stats_v4_a.bin",
		         SusamuneCfgStoragePrefix());
		_sprintf(v4Paths[1], "%s/susamune_il_stats_v4_b.bin",
		         SusamuneCfgStoragePrefix());
		for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
		{
			enum PbReadResult readResult =
				ReadSplitStatsV4File(v4Paths[fileIndex], v4);
			if (readResult == PB_READ_UNSAFE)
			{
				safe = false;
				continue;
			}
			if (readResult != PB_READ_VALID)
				continue;
			if (selectedFile >= 0 && v4->generation == selectedGeneration)
			{
				if (v4->checksum != selectedChecksum)
					safe = false;
				continue;
			}
			if (selectedFile >= 0 &&
			    !PbGenerationIsNewer(v4->generation, selectedGeneration))
				continue;
			selectedGeneration = v4->generation;
			selectedChecksum = v4->checksum;
			selectedFile = (s32)fileIndex;
		}
		if (safe && selectedFile >= 0)
		{
			enum PbReadResult readResult =
				ReadSplitStatsV4File(v4Paths[selectedFile], v4);
			if (readResult != PB_READ_VALID ||
			    v4->generation != selectedGeneration ||
			    v4->checksum != selectedChecksum)
			{
				safe = false;
			}
			else
			{
				ResetSplitStatsPayload(&stats->payload);
				MigrateSplitStatsV4(&stats->payload, &v4->payload);
				SplitStatsGeneration = selectedGeneration;
				migrated = true;
			}
		}
	}

	if (safe && SplitStatsActiveFile < 0 && !migrated)
	{
		char v3Paths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
		struct SusamuneSplitStatsFileV3 *v3 = &SplitStatsFileScratch.v3;
		u32 selectedGeneration = 0;
		bool selected = false;

		_sprintf(v3Paths[0], "%s/susamune_il_stats_v3_a.bin",
		         SusamuneCfgStoragePrefix());
		_sprintf(v3Paths[1], "%s/susamune_il_stats_v3_b.bin",
		         SusamuneCfgStoragePrefix());
		for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
		{
			enum PbReadResult readResult =
				ReadSplitStatsV3File(v3Paths[fileIndex], v3);
			if (readResult == PB_READ_UNSAFE)
			{
				safe = false;
				continue;
			}
			if (readResult != PB_READ_VALID)
				continue;
			if (selected && v3->generation == selectedGeneration)
			{
				if (memcmp(&v3->payload, &stats->payload,
				           sizeof(v3->payload)) != 0)
					safe = false;
				continue;
			}
			if (selected &&
			    !PbGenerationIsNewer(v3->generation, selectedGeneration))
				continue;
			memcpy(&stats->payload, &v3->payload, sizeof(v3->payload));
			selectedGeneration = v3->generation;
			selected = true;
		}
		if (safe && selected)
		{
			struct SusamuneSplitStatsPayloadV3 *selectedPayload =
				(struct SusamuneSplitStatsPayloadV3 *)&stats->payload;
			struct SusamuneSplitStatsPayloadV4 *v4Payload =
				&SplitStatsFileScratch.v4.payload;
			ResetSplitStatsV4Payload(v4Payload);
			MigrateSplitStatsV3(v4Payload, selectedPayload);
			ResetSplitStatsPayload(&stats->payload);
			MigrateSplitStatsV4(&stats->payload, v4Payload);
			SplitStatsGeneration = selectedGeneration;
			migrated = true;
		}
	}

	if (safe && SplitStatsActiveFile < 0 && !migrated)
	{
		char v2Paths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
		struct SusamuneSplitStatsFileV2 *v2 = &SplitStatsFileScratch.v2;
		u32 selectedGeneration = 0;
		u32 selectedSchemaHash = SUSAMUNE_SPLIT_STATS_V2_SCHEMA_HASH;
		bool selected = false;

		_sprintf(v2Paths[0], "%s/susamune_il_stats_v2_a.bin",
		         SusamuneCfgStoragePrefix());
		_sprintf(v2Paths[1], "%s/susamune_il_stats_v2_b.bin",
		         SusamuneCfgStoragePrefix());
		for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
		{
			enum PbReadResult readResult =
				ReadSplitStatsV2File(v2Paths[fileIndex], v2);
			if (readResult == PB_READ_UNSAFE)
			{
				safe = false;
				continue;
			}
			if (readResult != PB_READ_VALID)
				continue;
			if (selected && v2->generation == selectedGeneration)
			{
				if (memcmp(&v2->payload, &SplitStatsV2Selected,
				           sizeof(v2->payload)) != 0)
					safe = false;
				if (selectedSchemaHash ==
				        SUSAMUNE_SPLIT_STATS_PR7_SCHEMA_HASH &&
				    v2->schemaHash == SUSAMUNE_SPLIT_STATS_V2_SCHEMA_HASH)
				{
					memcpy(&SplitStatsV2Selected, &v2->payload,
					       sizeof(SplitStatsV2Selected));
					selectedSchemaHash = v2->schemaHash;
				}
				continue;
			}
			if (selected &&
			    !PbGenerationIsNewer(v2->generation, selectedGeneration))
				continue;
			memcpy(&SplitStatsV2Selected, &v2->payload,
			       sizeof(SplitStatsV2Selected));
			selectedGeneration = v2->generation;
			selectedSchemaHash = v2->schemaHash;
			selected = true;
		}
		if (safe && selected)
		{
			struct SusamuneSplitStatsPayloadV4 *v4Payload =
				&SplitStatsFileScratch.v4.payload;
			if (selectedSchemaHash == SUSAMUNE_SPLIT_STATS_PR7_SCHEMA_HASH)
				MigrateSplitStatsPr7(&SplitStatsV2Selected);
			ResetSplitStatsV4Payload(v4Payload);
			MigrateSplitStatsV2(v4Payload, &SplitStatsV2Selected);
			ResetSplitStatsPayload(&stats->payload);
			MigrateSplitStatsV4(&stats->payload, v4Payload);
			SplitStatsGeneration = selectedGeneration;
			migrated = true;
		}
	}

	if (safe && SplitStatsActiveFile < 0 && !migrated)
	{
		char v1Paths[SUSAMUNE_PB_FILE_COUNT][SUSAMUNE_PB_PATH_SIZE];
		struct SusamuneSplitStatsFileV1 *v1 = &SplitStatsFileScratch.v1;
		u32 selectedGeneration = 0;
		bool selected = false;

		_sprintf(v1Paths[0], "%s/susamune_il_stats_v1_a.bin",
		         SusamuneCfgStoragePrefix());
		_sprintf(v1Paths[1], "%s/susamune_il_stats_v1_b.bin",
		         SusamuneCfgStoragePrefix());
		for (fileIndex = 0; fileIndex < SUSAMUNE_PB_FILE_COUNT; fileIndex++)
		{
			enum PbReadResult readResult =
				ReadSplitStatsV1File(v1Paths[fileIndex], v1);
			if (readResult == PB_READ_UNSAFE)
			{
				safe = false;
				continue;
			}
			if (readResult != PB_READ_VALID)
				continue;
			if (selected && v1->generation == selectedGeneration)
			{
				if (memcmp(&v1->payload, &SplitStatsV1Selected,
				           sizeof(v1->payload)) != 0)
					safe = false;
				continue;
			}
			if (selected &&
			    !PbGenerationIsNewer(v1->generation, selectedGeneration))
				continue;
			memcpy(&SplitStatsV1Selected, &v1->payload,
			       sizeof(SplitStatsV1Selected));
			selectedGeneration = v1->generation;
			selected = true;
		}
		if (safe && selected)
		{
			struct SusamuneSplitStatsPayloadV4 *v4Payload =
				&SplitStatsFileScratch.v4.payload;
			ResetSplitStatsV4Payload(v4Payload);
			MigrateSplitStatsV1(v4Payload, &SplitStatsV1Selected);
			ResetSplitStatsPayload(&stats->payload);
			MigrateSplitStatsV4(&stats->payload, v4Payload);
			SplitStatsGeneration = selectedGeneration;
			migrated = true;
		}
	}

	SplitStatsAckSeq = 0;
	memset(stats->reserved, 0, sizeof(stats->reserved));
	memset(stats->tailPad, 0, sizeof(stats->tailPad));
	SplitStatsReady = safe;
	if (safe)
	{
		stats->flags |= SUSAMUNE_SPLIT_STATS_FLAG_WRITABLE;
		if (migrated)
			stats->flags |= SUSAMUNE_SPLIT_STATS_FLAG_MIGRATED;
	}
	else
	{
		ResetSplitStatsPayload(&stats->payload);
		dbgprintf("Susamune: split persistence disabled to preserve unreadable files\r\n");
	}
	return safe;
}

static int WriteSplitStatsFile(const struct SusamuneSplitStatsCfg *stats)
{
	struct SusamuneSplitStatsFile *file = &SplitStatsFileScratch.current;
	FIL f;
	UINT wrote = 0;
	u32 target = SplitStatsActiveFile == 0 ? 1u : 0u;
	int ret;
	int syncRet;
	int closeRet;

	if (stats->magic != SUSAMUNE_SPLIT_STATS_MAGIC ||
	    stats->version != SUSAMUNE_SPLIT_STATS_VERSION ||
	    stats->routeCount != SUSAMUNE_SPLIT_STATS_ROUTE_COUNT ||
	    stats->segmentCount != SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT ||
	    stats->regionCount != SUSAMUNE_SPLIT_STATS_REGION_COUNT ||
	    stats->profileCount != SUSAMUNE_SPLIT_STATS_PROFILE_COUNT ||
	    stats->headerReserved != 0 ||
	    stats->payloadBytes != sizeof(stats->payload) ||
	    stats->schemaHash != SUSAMUNE_SPLIT_STATS_SCHEMA_HASH ||
	    !SplitStatsBytesZero(stats->reserved, sizeof(stats->reserved)) ||
	    !SplitStatsBytesZero(stats->tailPad, sizeof(stats->tailPad)) ||
	    !SplitStatsPayloadValid(&stats->payload))
		return FR_INVALID_PARAMETER;

	memset(file, 0, sizeof(*file));
	file->magic = SUSAMUNE_SPLIT_STATS_FILE_MAGIC;
	file->version = SUSAMUNE_SPLIT_STATS_VERSION;
	file->routeCount = SUSAMUNE_SPLIT_STATS_ROUTE_COUNT;
	file->regionCount = SUSAMUNE_SPLIT_STATS_REGION_COUNT;
	file->segmentCount = SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT;
	file->profileCount = SUSAMUNE_SPLIT_STATS_PROFILE_COUNT;
	file->headerReserved = 0;
	file->payloadBytes = sizeof(file->payload);
	file->schemaHash = SUSAMUNE_SPLIT_STATS_SCHEMA_HASH;
	file->generation = SplitStatsGeneration + 1;
	memcpy(&file->payload, &stats->payload, sizeof(file->payload));
	file->checksum = SplitStatsChecksum(file);

	ret = f_open_char(&f, SplitStatsPaths[target],
	                  FA_WRITE | FA_CREATE_ALWAYS);
	if (ret != FR_OK)
		return ret;
	ret = f_write(&f, file, sizeof(*file), &wrote);
	if (ret == FR_OK && wrote != sizeof(*file))
		ret = FR_DISK_ERR;
	syncRet = f_sync(&f);
	if (ret == FR_OK && syncRet != FR_OK)
		ret = syncRet;
	closeRet = f_close(&f);
	if (ret == FR_OK && closeRet != FR_OK)
		ret = closeRet;

	if (ret == FR_OK)
	{
		SplitStatsGeneration = file->generation;
		SplitStatsActiveFile = (s32)target;
	}
	return ret;
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

// Parse an unsigned decimal. Returns false on anything that is not all digits,
// so a typo in a hand-edited ini leaves the mod's default in place rather than
// silently applying a garbage value.
static bool ParseU8(const char *s, u8 *out)
{
	u32 v = 0;

	if (*s == '\0')
		return false;

	for (; *s; s++)
	{
		if (*s < '0' || *s > '9')
			return false;
		v = v * 10 + (u32)(*s - '0');
		if (v > 254)  /* 0xFF is reserved for SUSAMUNE_CFG_UNSET */
			return false;
	}

	*out = (u8)v;
	return true;
}

static bool ParseU16(const char *s, u16 *out)
{
	u32 v = 0;

	if (*s == '\0')
		return false;
	for (; *s; s++)
	{
		if (*s < '0' || *s > '9')
			return false;
		v = v * 10 + (u32)(*s - '0');
		if (v > 65534)  /* 0xFFFF is the unset sentinel */
			return false;
	}
	*out = (u16)v;
	return true;
}

static s32 FindSettingKey(const char *key)
{
	u32 i;

	for (i = 0; i < SETTING_KEY_COUNT; i++)
	{
		if (strcmp(key, SettingKeys[i]) == 0)
			return (s32)i;
	}
	return -1;
}

static s32 FindBindKey(const char *key)
{
	u32 i;

	for (i = 0; i < BIND_KEY_COUNT; i++)
	{
		if (strcmp(key, BindKeys[i]) == 0)
			return (s32)i;
	}
	return -1;
}

static char LowerChar(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

// Length-delimited, case-insensitive compare against a NUL-terminated token,
// so the file may spell buttons "dup" or "DUp" interchangeably.
static bool TokenEquals(const char *s, u32 len, const char *token)
{
	u32 i;

	for (i = 0; i < len; i++)
	{
		if (token[i] == '\0' || LowerChar(s[i]) != LowerChar(token[i]))
			return false;
	}
	return token[len] == '\0';
}

// Parse "X+DUp" into a button mask. Returns false on any unrecognised token, so
// a typo leaves the mod's compiled-in default in place rather than silently
// producing a half-bind. The literal "none" (and an empty value) is a valid
// unbound entry.
static bool ParseBindMask(const char *s, u16 *out)
{
	u16 mask = 0;

	if (*s == '\0' || TokenEquals(s, (u32)strlen(s), SUSAMUNE_BIND_NONE_TOKEN))
	{
		*out = 0;
		return true;
	}

	while (*s)
	{
		const char *end = s;
		u32         len;
		u32         i;
		bool        found = false;

		while (*end && *end != SUSAMUNE_BIND_SEPARATOR)
			end++;
		len = (u32)(end - s);

		for (i = 0; i < BIND_BUTTON_COUNT; i++)
		{
			if (TokenEquals(s, len, BindButtons[i].token))
			{
				mask |= BindButtons[i].bit;
				found = true;
				break;
			}
		}
		if (!found)
			return false;

		s = (*end == SUSAMUNE_BIND_SEPARATOR) ? end + 1 : end;
	}

	*out = mask;
	return true;
}

// Which section the parser is currently inside. Anything that is not one of
// this version's six sections -- [nintendont], or another version's settings --
// is SECTION_OTHER and left alone.
enum IniSection {
	SECTION_OTHER,
	SECTION_SETTINGS,
	SECTION_BINDS,
	SECTION_INPUT_DISPLAY,
	SECTION_METADATA_DISPLAY,
	SECTION_QFT_DISPLAY,
	SECTION_CREATION
};

static enum IniSection ClassifySection(const char *name)
{
	if (strcmp(name, SettingsSection) == 0)
		return SECTION_SETTINGS;
	if (strcmp(name, BindsSection) == 0)
		return SECTION_BINDS;
	if (strcmp(name, InputDisplaySection) == 0)
		return SECTION_INPUT_DISPLAY;
	if (strcmp(name, MetadataDisplaySection) == 0)
		return SECTION_METADATA_DISPLAY;
	if (strcmp(name, QftDisplaySection) == 0)
		return SECTION_QFT_DISPLAY;
	if (strcmp(name, CreationSection) == 0)
		return SECTION_CREATION;
	return SECTION_OTHER;
}

static void ApplyInputDisplayKey(struct SusamuneInputDisplayCfg *cfg,
				 const char *key, const char *text)
{
	u8  v8;
	u16 v16;

	if (strcmp(key, "x") == 0)
	{
		if (ParseU16(text, &v16)) cfg->x = v16;
	}
	else if (strcmp(key, "y") == 0)
	{
		if (ParseU16(text, &v16)) cfg->y = v16;
	}
	else if (ParseU8(text, &v8))
	{
		if (strcmp(key, "start_visible") == 0) cfg->startVisible = v8;
		else if (strcmp(key, "scale") == 0) cfg->scale = v8;
		else if (strcmp(key, "background_r") == 0) cfg->bgR = v8;
		else if (strcmp(key, "background_g") == 0) cfg->bgG = v8;
		else if (strcmp(key, "background_b") == 0) cfg->bgB = v8;
		else if (strcmp(key, "background_alpha") == 0) cfg->bgA = v8;
		else if (strcmp(key, "brightness") == 0) cfg->brightness = v8;
		else if (strcmp(key, "value_mode") == 0) cfg->valueMode = v8;
		else if (strcmp(key, "value_source") == 0) cfg->valueSource = v8;
		else if (strcmp(key, "value_position") == 0) cfg->valuePlacement = v8;
	}
}

static void CopyMetadataFormat(char *dst, const char *src)
{
	u32 i = 0;

	while (i + 1 < SUSAMUNE_METADATA_FORMAT_SIZE && src[i])
	{
		dst[i] = src[i];
		i++;
	}
	dst[i] = '\0';
}

static void ApplyMetadataDisplayKey(struct SusamuneMetadataDisplayCfg *cfg,
				    const char *key, const char *text)
{
	u8  v8;
	u16 v16;

	if (strcmp(key, "format") == 0)
	{
		CopyMetadataFormat(cfg->format, text);
	}
	else if (strcmp(key, "x") == 0)
	{
		if (ParseU16(text, &v16)) cfg->x = v16;
	}
	else if (strcmp(key, "y") == 0)
	{
		if (ParseU16(text, &v16)) cfg->y = v16;
	}
	else if (strcmp(key, "fields") == 0)
	{
		if (ParseU16(text, &v16)) cfg->fieldMask = v16;
	}
	else if (ParseU8(text, &v8))
	{
		if (strcmp(key, "start_visible") == 0) cfg->startVisible = v8;
		else if (strcmp(key, "scale") == 0) cfg->scale = v8;
		else if (strcmp(key, "label_mode") == 0) cfg->labelMode = v8;
		else if (strcmp(key, "background_alpha") == 0) cfg->backgroundAlpha = v8;
	}
}

static bool ParseQftU8(const char *s, u8 *out)
{
	u32 v = 0;

	if (*s == '\0')
		return false;
	for (; *s; s++)
	{
		if (*s < '0' || *s > '9')
			return false;
		v = v * 10 + (u32)(*s - '0');
		if (v > 255)
			return false;
	}
	*out = (u8)v;
	return true;
}

static bool ParseQftRgb(const char *s, u8 out[3])
{
	u32 channel;

	for (channel = 0; channel < 3; channel++)
	{
		u32 v = 0;
		bool any = false;

		while (*s == ' ' || *s == '\t') s++;
		while (*s >= '0' && *s <= '9')
		{
			any = true;
			v = v * 10 + (u32)(*s++ - '0');
			if (v > 255) return false;
		}
		if (!any) return false;
		out[channel] = (u8)v;
		while (*s == ' ' || *s == '\t') s++;
		if (channel < 2)
		{
			if (*s++ != ',') return false;
		}
	}
	return *s == '\0';
}

static void ApplyInputStyleKey(struct SusamuneInputStyleCfg *cfg,
			       const char *key, const char *text)
{
	u8 value;
	u8 rgb[3];
	u32 i;

	if (strcmp(key, "element_alpha") == 0 && ParseQftU8(text, &value))
	{
		cfg->elementOpacity = value;
		cfg->present |= SUSAMUNE_INPUT_STYLE_OPACITY;
		return;
	}
	if (strcmp(key, "padding") == 0 && ParseQftU8(text, &value))
	{
		cfg->padding = value;
		cfg->present |= SUSAMUNE_INPUT_STYLE_PADDING;
		return;
	}
	for (i = 0; i < SUSAMUNE_INPUT_COLOR_COUNT; i++)
	{
		if (strcmp(key, InputColorKeys[i]) == 0 && ParseQftRgb(text, rgb))
		{
			cfg->rgb[i][0] = rgb[0];
			cfg->rgb[i][1] = rgb[1];
			cfg->rgb[i][2] = rgb[2];
			cfg->present |= SUSAMUNE_INPUT_STYLE_COLOR(i);
			return;
		}
	}
}

static void ApplyQftDisplayKey(struct SusamuneQftDisplayCfg *cfg,
			       const char *key, const char *text)
{
	u8  v8;
	u16 v16;
	u32 i;
	u8  rgb[3];

	if (strlen(key) == 10 && memcmp(key, "text_", 5) == 0 &&
	    key[5] >= '1' && key[5] <= '9' && strcmp(key + 6, "_rgb") == 0 &&
	    ParseQftRgb(text, rgb))
	{
		i = (u32)(key[5] - '1');
		cfg->textRgb[i][0] = rgb[0];
		cfg->textRgb[i][1] = rgb[1];
		cfg->textRgb[i][2] = rgb[2];
		cfg->slotPresent |= SUSAMUNE_QFT_DISPLAY_SLOT(i);
		return;
	}

	if (strcmp(key, "x") == 0)
	{
		if (ParseU16(text, &v16))
		{
			cfg->x = v16;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_X;
		}
	}
	else if (strcmp(key, "y") == 0)
	{
		if (ParseU16(text, &v16))
		{
			cfg->y = v16;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_Y;
		}
	}
	else if (ParseQftU8(text, &v8))
	{
		if (strcmp(key, "scale") == 0)
		{
			cfg->scale = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_SCALE;
		}
		else if (strcmp(key, "text_r") == 0)
		{
			cfg->textR = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_R;
		}
		else if (strcmp(key, "text_g") == 0)
		{
			cfg->textG = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_G;
		}
		else if (strcmp(key, "text_b") == 0)
		{
			cfg->textB = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_B;
		}
		else if (strcmp(key, "text_alpha") == 0)
		{
			cfg->textA = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_A;
		}
		else if (strcmp(key, "background_r") == 0)
		{
			cfg->bgR = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_BG_R;
		}
		else if (strcmp(key, "background_g") == 0)
		{
			cfg->bgG = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_BG_G;
		}
		else if (strcmp(key, "background_b") == 0)
		{
			cfg->bgB = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_BG_B;
		}
		else if (strcmp(key, "background_alpha") == 0)
		{
			cfg->bgA = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_BG_A;
		}
		else if (strcmp(key, "text_brightness") == 0 ||
		         strcmp(key, "brightness") == 0)
		{
			cfg->textBrightness = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_TEXT_BRIGHTNESS;
		}
		else if (strcmp(key, "padding") == 0)
		{
			cfg->padding = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_PADDING;
		}
		else if (strcmp(key, "leading_zero") == 0)
		{
			cfg->leadingZero = v8;
			cfg->present |= SUSAMUNE_QFT_DISPLAY_LEADING_ZERO;
		}
	}
}

static bool ParseMetadataRgbKey(const char *key, u32 *slot)
{
	u32 value = 0;
	const char *p;

	if (memcmp(key, "char_", 5) != 0)
		return false;
	p = key + 5;
	if (*p < '0' || *p > '9')
		return false;
	while (*p >= '0' && *p <= '9')
	{
		value = value * 10 + (u32)(*p++ - '0');
		if (value > SUSAMUNE_METADATA_STYLE_TEXT_SLOTS)
			return false;
	}
	if (value == 0 || strcmp(p, "_rgb") != 0)
		return false;
	*slot = value - 1;
	return true;
}

static void ApplyMetadataStyleKey(struct SusamuneMetadataStyleCfg *cfg,
				  const char *key, const char *text)
{
	u8 rgb[3];
	u8 v8;
	u32 slot;

	if (ParseMetadataRgbKey(key, &slot) && ParseQftRgb(text, rgb))
	{
		cfg->textRgb[slot][0] = rgb[0];
		cfg->textRgb[slot][1] = rgb[1];
		cfg->textRgb[slot][2] = rgb[2];
		cfg->slotPresent[slot >> 3] |= (u8)(1u << (slot & 7));
		return;
	}
	if (!ParseQftU8(text, &v8))
		return;

	if (strcmp(key, "text_r") == 0)
	{
		cfg->textR = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_TEXT_R;
	}
	else if (strcmp(key, "text_g") == 0)
	{
		cfg->textG = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_TEXT_G;
	}
	else if (strcmp(key, "text_b") == 0)
	{
		cfg->textB = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_TEXT_B;
	}
	else if (strcmp(key, "text_alpha") == 0)
	{
		cfg->textA = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_TEXT_A;
	}
	else if (strcmp(key, "background_r") == 0)
	{
		cfg->bgR = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BG_R;
	}
	else if (strcmp(key, "background_g") == 0)
	{
		cfg->bgG = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BG_G;
	}
	else if (strcmp(key, "background_b") == 0)
	{
		cfg->bgB = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BG_B;
	}
	else if (strcmp(key, "background_alpha") == 0)
	{
		cfg->bgA = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BG_A;
	}
	else if (strcmp(key, "text_brightness") == 0 || strcmp(key, "brightness") == 0)
	{
		cfg->textBrightness = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_BRIGHTNESS;
	}
	else if (strcmp(key, "padding") == 0)
	{
		cfg->padding = v8;
		cfg->present |= SUSAMUNE_METADATA_STYLE_PADDING;
	}
}

static bool ParseCreationWordKey(const char *key, u32 *word, const char **field)
{
	if (memcmp(key, "word", 4) != 0 || key[4] < '1' || key[4] > '3' ||
	    key[5] != '_')
		return false;
	*word = (u32)(key[4] - '1');
	*field = key + 6;
	return true;
}

static bool ParseCreationCharKey(const char *field, u32 *slot)
{
	u32 value = 0;
	const char *p;

	if (memcmp(field, "char_", 5) != 0)
		return false;
	p = field + 5;
	if (*p < '0' || *p > '9')
		return false;
	while (*p >= '0' && *p <= '9')
	{
		value = value * 10 + (u32)(*p++ - '0');
		if (value > SUSAMUNE_CREATION_WORD_CHARS)
			return false;
	}
	if (value == 0 || strcmp(p, "_rgb") != 0)
		return false;
	*slot = value - 1;
	return true;
}

static void ApplyCreationKey(struct SusamuneCreationCfg *cfg,
	                         const char *key, const char *text)
{
	u8 rgb[3];
	u8 v8;
	u16 v16;
	u32 i;
	u32 word;
	u32 slot;
	const char *field;

	for (i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++)
	{
		if (strcmp(key, CreationColorKeys[i]) == 0 && ParseQftRgb(text, rgb))
		{
			cfg->rgb[i][0] = rgb[0];
			cfg->rgb[i][1] = rgb[1];
			cfg->rgb[i][2] = rgb[2];
			cfg->colorPresent |= SUSAMUNE_CREATION_COLOR(i);
			return;
		}
	}
	if (strcmp(key, "show_timer_label") == 0 && ParseQftU8(text, &v8))
	{
		cfg->timerLabelVisible = v8 ? 1 : 0;
		cfg->timerLabelVisiblePresent = 1;
		return;
	}
	if (strcmp(key, "recent_ils_x") == 0 && ParseU16(text, &v16))
	{
		cfg->recentIlX = v16;
		cfg->recentIlPositionPresent = 1;
		return;
	}
	if (strcmp(key, "recent_ils_y") == 0 && ParseU16(text, &v16))
	{
		cfg->recentIlY = v16;
		cfg->recentIlPositionPresent = 1;
		return;
	}
	if (strcmp(key, "recent_ils_scale") == 0 && ParseQftU8(text, &v8))
	{
		cfg->recentIlScale = v8;
		cfg->recentIlPositionPresent = 1;
		return;
	}
	if (strcmp(key, "recent_ils_text_rgb") == 0 && ParseQftRgb(text, rgb))
	{
		cfg->recentIlTextRgb[0] = rgb[0];
		cfg->recentIlTextRgb[1] = rgb[1];
		cfg->recentIlTextRgb[2] = rgb[2];
		return;
	}
	if (strcmp(key, "recent_ils_background_rgb") == 0 && ParseQftRgb(text, rgb))
	{
		cfg->recentIlBgR = rgb[0];
		cfg->recentIlBgG = rgb[1];
		cfg->recentIlBgB = rgb[2];
		return;
	}
	if (strcmp(key, "recent_ils_text_alpha") == 0 && ParseQftU8(text, &v8))
	{
		cfg->recentIlTextA = v8;
		return;
	}
	if (strcmp(key, "recent_ils_background_alpha") == 0 &&
	    ParseQftU8(text, &v8))
	{
		cfg->recentIlBgA = v8;
		return;
	}
	if (strcmp(key, "recent_ils_text_brightness") == 0 &&
	    ParseQftU8(text, &v8))
	{
		cfg->recentIlTextBrightness = v8;
		return;
	}
	if (strcmp(key, "recent_ils_padding") == 0 && ParseQftU8(text, &v8))
	{
		cfg->recentIlPadding = v8;
		return;
	}
	if (strcmp(key, "savestate_feedback_x") == 0 && ParseU16(text, &v16))
	{
		cfg->savestateX = v16;
		return;
	}
	if (strcmp(key, "savestate_feedback_y") == 0 && ParseU16(text, &v16))
	{
		cfg->savestateY = v16;
		return;
	}
	if (strcmp(key, "savestate_feedback_scale") == 0 &&
	    ParseQftU8(text, &v8))
	{
		cfg->savestateScale = v8;
		return;
	}
	if (strcmp(key, "savestate_feedback_text_rgb") == 0 &&
	    ParseQftRgb(text, rgb))
	{
		cfg->savestateTextRgb[0] = rgb[0];
		cfg->savestateTextRgb[1] = rgb[1];
		cfg->savestateTextRgb[2] = rgb[2];
		return;
	}
	if (strcmp(key, "savestate_feedback_text_alpha") == 0 &&
	    ParseQftU8(text, &v8))
	{
		cfg->savestateTextA = v8;
		return;
	}
	if (strcmp(key, "savestate_feedback_background_rgb") == 0 &&
	    ParseQftRgb(text, rgb))
	{
		cfg->savestateBgR = rgb[0];
		cfg->savestateBgG = rgb[1];
		cfg->savestateBgB = rgb[2];
		return;
	}
	if (strcmp(key, "savestate_feedback_background_alpha") == 0 &&
	    ParseQftU8(text, &v8))
	{
		cfg->savestateBgA = v8;
		return;
	}
	if (strcmp(key, "savestate_feedback_text_brightness") == 0 &&
	    ParseQftU8(text, &v8))
	{
		cfg->savestateTextBrightness = v8;
		return;
	}
	if (strcmp(key, "savestate_feedback_padding") == 0 &&
	    ParseQftU8(text, &v8))
	{
		cfg->savestatePadding = v8;
		return;
	}
	if (strcmp(key, "achievement_popup_x") == 0 && ParseU16(text, &v16))
	{
		cfg->achievementX = v16;
		return;
	}
	if (strcmp(key, "achievement_popup_y") == 0 && ParseU16(text, &v16))
	{
		cfg->achievementY = v16;
		return;
	}
	if (strcmp(key, "achievement_popup_scale") == 0 &&
	    ParseQftU8(text, &v8))
	{
		cfg->achievementScale = v8;
		return;
	}
	if (strcmp(key, "stage_session_counter_x") == 0 &&
	    ParseU16(text, &v16))
	{
		cfg->stageSessionStyleMagic =
			SUSAMUNE_CREATION_STAGE_SESSION_STYLE_MAGIC;
		cfg->stageSessionX = v16;
		return;
	}
	if (strcmp(key, "stage_session_counter_y") == 0 &&
	    ParseU16(text, &v16))
	{
		cfg->stageSessionStyleMagic =
			SUSAMUNE_CREATION_STAGE_SESSION_STYLE_MAGIC;
		cfg->stageSessionY = v16;
		return;
	}
	if (strcmp(key, "stage_session_counter_scale") == 0 &&
	    ParseQftU8(text, &v8))
	{
		cfg->stageSessionStyleMagic =
			SUSAMUNE_CREATION_STAGE_SESSION_STYLE_MAGIC;
		cfg->stageSessionScale = v8;
		return;
	}
	if (!ParseCreationWordKey(key, &word, &field))
		return;
	if (strcmp(field, "text") == 0)
	{
		u32 length = (u32)strlen(text);
		if (length > SUSAMUNE_CREATION_WORD_CHARS)
			length = SUSAMUNE_CREATION_WORD_CHARS;
		memcpy(cfg->words[word].text, text, length);
		cfg->words[word].text[length] = '\0';
		cfg->words[word].length = (u8)length;
		return;
	}
	if (strcmp(field, "text_rgb") == 0 && ParseQftRgb(text, rgb))
	{
		for (i = 0; i < SUSAMUNE_CREATION_WORD_CHARS; i++)
		{
			cfg->words[word].rgb[i][0] = rgb[0];
			cfg->words[word].rgb[i][1] = rgb[1];
			cfg->words[word].rgb[i][2] = rgb[2];
		}
		return;
	}
	if (ParseCreationCharKey(field, &slot) && ParseQftRgb(text, rgb))
	{
		cfg->words[word].rgb[slot][0] = rgb[0];
		cfg->words[word].rgb[slot][1] = rgb[1];
		cfg->words[word].rgb[slot][2] = rgb[2];
		return;
	}
	if ((strcmp(field, "x") == 0 || strcmp(field, "y") == 0) &&
	    ParseU16(text, &v16))
	{
		if (field[0] == 'x') cfg->words[word].x = v16;
		else cfg->words[word].y = v16;
		return;
	}
	if (!ParseQftU8(text, &v8))
		return;
	if (strcmp(field, "scale") == 0) cfg->words[word].scale = v8;
	else if (strcmp(field, "text_alpha") == 0) cfg->words[word].textA = v8;
	else if (strcmp(field, "background_r") == 0) cfg->words[word].bgR = v8;
	else if (strcmp(field, "background_g") == 0) cfg->words[word].bgG = v8;
	else if (strcmp(field, "background_b") == 0) cfg->words[word].bgB = v8;
	else if (strcmp(field, "background_alpha") == 0) cfg->words[word].bgA = v8;
	else if (strcmp(field, "text_brightness") == 0) cfg->words[word].textBrightness = v8;
	else if (strcmp(field, "padding") == 0) cfg->words[word].padding = v8;
	else if (strcmp(field, "visible") == 0) cfg->words[word].visible = v8;
}

static void ApplyWallkickStyleKey(struct SusamuneWallkickStyleCfg *cfg,
	                              const char *key, const char *text)
{
	u8 rgb[3];
	u8 v8;
	u16 v16;
	u32 i;
	char expected[24];

	if (strcmp(key, "wallkick_x") == 0 && ParseU16(text, &v16)) cfg->x = v16;
	else if (strcmp(key, "wallkick_y") == 0 && ParseU16(text, &v16)) cfg->y = v16;
	else if (strcmp(key, "wallkick_scale") == 0 && ParseQftU8(text, &v8)) cfg->scale = v8;
	else if (strcmp(key, "notification_x") == 0 && ParseU16(text, &v16)) cfg->toastX = v16;
	else if (strcmp(key, "notification_y") == 0 && ParseU16(text, &v16)) cfg->toastY = v16;
	else if (strcmp(key, "notification_scale") == 0 && ParseQftU8(text, &v8)) cfg->toastScale = v8;
	else if (strcmp(key, "il_pb_popup_x") == 0 && ParseU16(text, &v16)) cfg->pbPopupX = v16;
	else if (strcmp(key, "il_pb_popup_y") == 0 && ParseU16(text, &v16)) cfg->pbPopupY = v16;
	else if (strcmp(key, "il_pb_popup_scale") == 0 && ParseQftU8(text, &v8)) cfg->pbPopupScale = v8;
	else if (strcmp(key, "wallkick_text_alpha") == 0 && ParseQftU8(text, &v8)) cfg->textA = v8;
	else if (strcmp(key, "wallkick_background_rgb") == 0 && ParseQftRgb(text, rgb))
	{
		cfg->bgR = rgb[0]; cfg->bgG = rgb[1]; cfg->bgB = rgb[2];
	}
	else if (strcmp(key, "wallkick_background_alpha") == 0 && ParseQftU8(text, &v8)) cfg->bgA = v8;
	else if (strcmp(key, "wallkick_text_brightness") == 0 && ParseQftU8(text, &v8)) cfg->textBrightness = v8;
	else if (strcmp(key, "wallkick_padding") == 0 && ParseQftU8(text, &v8)) cfg->padding = v8;
	else
	{
		for (i = 0; i < SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT; i++)
		{
			_sprintf(expected, "wallkick_%u_rgb", i + 1);
			if (strcmp(key, expected) == 0 && ParseQftRgb(text, rgb))
			{
				cfg->rgb[i][0] = rgb[0];
				cfg->rgb[i][1] = rgb[1];
				cfg->rgb[i][2] = rgb[2];
				return;
			}
		}
	}
}

// Whether the file already carries settings for this game version. When it does
// not, the mod is asked to author them (SUSAMUNE_CFG_FLAG_NO_CONFIG).
static bool SawSettingsSection = false;

static void ParseIni(char *text, struct SusamuneCfg *cfg)
{
	char *line = text;
	enum IniSection section = SECTION_OTHER;

	SawSettingsSection = false;

	while (*line)
	{
		char *next;
		char *eq;
		s32   idx;
		u8    value;
		u16   mask;

		// Split off this line.
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
				char *name;
				*close = '\0';
				name = Trim(line + 1);
				section = ClassifySection(name);
				if (section == SECTION_SETTINGS)
					SawSettingsSection = true;
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

		if (section == SECTION_SETTINGS)
		{
			idx = FindSettingKey(Trim(line));
			if (idx >= 0 && ParseU8(Trim(eq + 1), &value))
				cfg->values[idx] = value;
		}
		else if (section == SECTION_BINDS)
		{
			idx = FindBindKey(Trim(line));
			if (idx >= 0 && ParseBindMask(Trim(eq + 1), &mask))
				cfg->binds[idx] = (u16)(mask & SUSAMUNE_BIND_BUTTON_MASK);
		}
		else if (section == SECTION_INPUT_DISPLAY)
		{
			ApplyInputDisplayKey(&cfg->inputDisplay, Trim(line), Trim(eq + 1));
			ApplyInputStyleKey(&cfg->inputStyle, Trim(line), Trim(eq + 1));
		}
		else if (section == SECTION_METADATA_DISPLAY)
		{
			ApplyMetadataDisplayKey(&cfg->metadataDisplay, Trim(line), Trim(eq + 1));
			ApplyMetadataStyleKey(&cfg->metadataStyle, Trim(line), Trim(eq + 1));
		}
		else if (section == SECTION_QFT_DISPLAY)
		{
			ApplyQftDisplayKey(&cfg->qftDisplay, Trim(line), Trim(eq + 1));
		}
		else if (section == SECTION_CREATION)
		{
			ApplyCreationKey(&cfg->creation, Trim(line), Trim(eq + 1));
			ApplyWallkickStyleKey(&cfg->wallkickStyle, Trim(line), Trim(eq + 1));
		}

		line = next;
	}
}

// ---------------------------------------------------------------------
// Serialisation
// ---------------------------------------------------------------------

// Render a bind mask as "X+DUp", or the "none" token when it has no buttons.
static u32 FormatBindMask(char *buf, u16 mask)
{
	u32 n = 0;
	u32 i;

	for (i = 0; i < BIND_BUTTON_COUNT; i++)
	{
		const char *p;

		if (!(mask & BindButtons[i].bit))
			continue;
		if (n > 0)
			buf[n++] = SUSAMUNE_BIND_SEPARATOR;
		for (p = BindButtons[i].token; *p; p++)
			buf[n++] = *p;
	}

	if (n == 0)
	{
		const char *p;
		for (p = SUSAMUNE_BIND_NONE_TOKEN; *p; p++)
			buf[n++] = *p;
	}

	buf[n] = '\0';
	return n;
}

// Append to an output file, latching the first error so the caller can check
// once at the end instead of after every line.
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

// Write out this version's [settings_<region>] section, header included.
static void EmitSettingsSection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	char line[96];
	u32  count = cfg->count;
	u32  i;

	if (count > SETTING_KEY_COUNT)
		count = SETTING_KEY_COUNT;

	EmitStr(f, err, "[");
	EmitStr(f, err, SettingsSection);
	EmitStr(f, err, "]\r\n");

	for (i = 0; i < count; i++)
	{
		if (cfg->values[i] == SUSAMUNE_CFG_UNSET)
			continue;
		Emit(f, err, line,
		     (u32)_sprintf(line, "%s = %u\r\n", SettingKeys[i], cfg->values[i]));
	}
}

static void EmitBindsSection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	char line[128];
	char combo[64];
	u32  bindCount = cfg->bindCount;
	u32  i;

	if (bindCount > BIND_KEY_COUNT)
		bindCount = BIND_KEY_COUNT;

	EmitStr(f, err, "[");
	EmitStr(f, err, BindsSection);
	EmitStr(f, err, "]\r\n");

	for (i = 0; i < bindCount; i++)
	{
		if (cfg->binds[i] == SUSAMUNE_CFG_BIND_UNSET)
			continue;
		FormatBindMask(combo, cfg->binds[i]);
		Emit(f, err, line,
		     (u32)_sprintf(line, "%s = %s\r\n", BindKeys[i], combo));
	}
}

static void EmitInputU8(FIL *f, int *err, const char *key, u8 value)
{
	char line[96];
	if (value != SUSAMUNE_INPUT_CFG_U8_UNSET)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitInputU16(FIL *f, int *err, const char *key, u16 value)
{
	char line[96];
	if (value != SUSAMUNE_INPUT_CFG_U16_UNSET)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitMetadataStyleU8(FIL *f, int *err, const char *key, u8 value,
				u16 present, u16 bit)
{
	char line[96];
	if (present & bit)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitInputDisplaySection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	const struct SusamuneInputDisplayCfg *d = &cfg->inputDisplay;
	const struct SusamuneInputStyleCfg *s = &cfg->inputStyle;
	char line[96];
	u32 i;

	EmitStr(f, err, "[");
	EmitStr(f, err, InputDisplaySection);
	EmitStr(f, err, "]\r\n");
	EmitInputU16(f, err, "x", d->x);
	EmitInputU16(f, err, "y", d->y);
	EmitInputU8(f, err, "start_visible", d->startVisible);
	EmitInputU8(f, err, "scale", d->scale);
	EmitInputU8(f, err, "background_r", d->bgR);
	EmitInputU8(f, err, "background_g", d->bgG);
	EmitInputU8(f, err, "background_b", d->bgB);
	EmitInputU8(f, err, "background_alpha", d->bgA);
	EmitInputU8(f, err, "brightness", d->brightness);
	EmitInputU8(f, err, "value_mode", d->valueMode);
	EmitInputU8(f, err, "value_source", d->valueSource);
	EmitInputU8(f, err, "value_position", d->valuePlacement);
	if (s->magic != SUSAMUNE_INPUT_STYLE_MAGIC ||
	    s->version != SUSAMUNE_INPUT_STYLE_VERSION)
		return;
	EmitMetadataStyleU8(f, err, "element_alpha", s->elementOpacity, s->present,
	                    SUSAMUNE_INPUT_STYLE_OPACITY);
	EmitMetadataStyleU8(f, err, "padding", s->padding, s->present,
	                    SUSAMUNE_INPUT_STYLE_PADDING);
	for (i = 0; i < SUSAMUNE_INPUT_COLOR_COUNT; i++)
	{
		if (s->present & SUSAMUNE_INPUT_STYLE_COLOR(i))
		{
			Emit(f, err, line, (u32)_sprintf(
				line, "%s = %u,%u,%u\r\n", InputColorKeys[i],
				s->rgb[i][0], s->rgb[i][1], s->rgb[i][2]));
		}
	}
}

static void EmitMetadataDisplaySection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	const struct SusamuneMetadataDisplayCfg *d = &cfg->metadataDisplay;
	const struct SusamuneMetadataStyleCfg *s = &cfg->metadataStyle;
	u8 backgroundAlpha = d->backgroundAlpha;
	u32 i;
	char line[96];
	if (s->magic == SUSAMUNE_METADATA_STYLE_MAGIC &&
	    s->version == SUSAMUNE_METADATA_STYLE_VERSION &&
	    (s->present & SUSAMUNE_METADATA_STYLE_BG_A) &&
	    (backgroundAlpha == SUSAMUNE_INPUT_CFG_U8_UNSET ||
	     backgroundAlpha == s->bgA))
		backgroundAlpha = s->bgA;

	EmitStr(f, err, "[");
	EmitStr(f, err, MetadataDisplaySection);
	EmitStr(f, err, "]\r\n");
	EmitInputU16(f, err, "x", d->x);
	EmitInputU16(f, err, "y", d->y);
	EmitInputU16(f, err, "fields", d->fieldMask);
	EmitInputU8(f, err, "start_visible", d->startVisible);
	EmitInputU8(f, err, "scale", d->scale);
	EmitInputU8(f, err, "label_mode", d->labelMode);
	if ((u8)d->format[0] != SUSAMUNE_METADATA_FORMAT_UNSET)
	{
		EmitStr(f, err, "format = ");
		EmitStr(f, err, d->format);
		EmitStr(f, err, "\r\n");
	}
	if (s->magic != SUSAMUNE_METADATA_STYLE_MAGIC ||
	    s->version != SUSAMUNE_METADATA_STYLE_VERSION)
	{
		EmitInputU8(f, err, "background_alpha", backgroundAlpha);
		return;
	}
	EmitMetadataStyleU8(f, err, "text_r", s->textR, s->present,
	                    SUSAMUNE_METADATA_STYLE_TEXT_R);
	EmitMetadataStyleU8(f, err, "text_g", s->textG, s->present,
	                    SUSAMUNE_METADATA_STYLE_TEXT_G);
	EmitMetadataStyleU8(f, err, "text_b", s->textB, s->present,
	                    SUSAMUNE_METADATA_STYLE_TEXT_B);
	EmitMetadataStyleU8(f, err, "text_alpha", s->textA, s->present,
	                    SUSAMUNE_METADATA_STYLE_TEXT_A);
	EmitMetadataStyleU8(f, err, "background_r", s->bgR, s->present,
	                    SUSAMUNE_METADATA_STYLE_BG_R);
	EmitMetadataStyleU8(f, err, "background_g", s->bgG, s->present,
	                    SUSAMUNE_METADATA_STYLE_BG_G);
	EmitMetadataStyleU8(f, err, "background_b", s->bgB, s->present,
	                    SUSAMUNE_METADATA_STYLE_BG_B);
	if (s->present & SUSAMUNE_METADATA_STYLE_BG_A)
		Emit(f, err, line, (u32)_sprintf(
			line, "background_alpha = %u\r\n", backgroundAlpha));
	else
		EmitInputU8(f, err, "background_alpha", backgroundAlpha);
	EmitMetadataStyleU8(f, err, "text_brightness", s->textBrightness, s->present,
	                    SUSAMUNE_METADATA_STYLE_BRIGHTNESS);
	EmitMetadataStyleU8(f, err, "padding", s->padding, s->present,
	                    SUSAMUNE_METADATA_STYLE_PADDING);
	for (i = 0; i < SUSAMUNE_METADATA_STYLE_TEXT_SLOTS; i++)
	{
		if (s->slotPresent[i >> 3] & (1u << (i & 7)))
		{
			Emit(f, err, line, (u32)_sprintf(
				line, "char_%u_rgb = %u,%u,%u\r\n", i + 1,
				s->textRgb[i][0], s->textRgb[i][1], s->textRgb[i][2]));
		}
	}
}

static void EmitQftU8(FIL *f, int *err, const char *key, u8 value,
		      u32 present, u32 bit)
{
	char line[96];
	if (present & bit)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitQftU16(FIL *f, int *err, const char *key, u16 value,
		       u32 present, u32 bit)
{
	char line[96];
	if (present & bit)
		Emit(f, err, line, (u32)_sprintf(line, "%s = %u\r\n", key, value));
}

static void EmitQftDisplaySection(FIL *f, int *err, const struct SusamuneCfg *cfg)
{
	const struct SusamuneQftDisplayCfg *d = &cfg->qftDisplay;
	const u16 p = d->present;
	u32 i;
	char line[96];

	EmitStr(f, err, "[");
	EmitStr(f, err, QftDisplaySection);
	EmitStr(f, err, "]\r\n");
	EmitQftU16(f, err, "x", d->x, p, SUSAMUNE_QFT_DISPLAY_X);
	EmitQftU16(f, err, "y", d->y, p, SUSAMUNE_QFT_DISPLAY_Y);
	EmitQftU8(f, err, "scale", d->scale, p, SUSAMUNE_QFT_DISPLAY_SCALE);
	if (d->version == 1 || d->slotPresent == 0)
	{
		EmitQftU8(f, err, "text_r", d->textR, p, SUSAMUNE_QFT_DISPLAY_TEXT_R);
		EmitQftU8(f, err, "text_g", d->textG, p, SUSAMUNE_QFT_DISPLAY_TEXT_G);
		EmitQftU8(f, err, "text_b", d->textB, p, SUSAMUNE_QFT_DISPLAY_TEXT_B);
	}
	else
	{
		for (i = 0; i < SUSAMUNE_QFT_DISPLAY_TEXT_SLOTS; i++)
		{
			if (d->slotPresent & SUSAMUNE_QFT_DISPLAY_SLOT(i))
			{
				Emit(f, err, line, (u32)_sprintf(
					line, "text_%u_rgb = %u,%u,%u\r\n", i + 1,
					d->textRgb[i][0], d->textRgb[i][1], d->textRgb[i][2]));
			}
		}
	}
	EmitQftU8(f, err, "text_alpha", d->textA, p, SUSAMUNE_QFT_DISPLAY_TEXT_A);
	EmitQftU8(f, err, "background_r", d->bgR, p, SUSAMUNE_QFT_DISPLAY_BG_R);
	EmitQftU8(f, err, "background_g", d->bgG, p, SUSAMUNE_QFT_DISPLAY_BG_G);
	EmitQftU8(f, err, "background_b", d->bgB, p, SUSAMUNE_QFT_DISPLAY_BG_B);
	EmitQftU8(f, err, "background_alpha", d->bgA, p, SUSAMUNE_QFT_DISPLAY_BG_A);
	EmitQftU8(f, err, "text_brightness", d->textBrightness, p,
	          SUSAMUNE_QFT_DISPLAY_TEXT_BRIGHTNESS);
	EmitQftU8(f, err, "padding", d->padding, p, SUSAMUNE_QFT_DISPLAY_PADDING);
	EmitQftU8(f, err, "leading_zero", d->leadingZero, p,
	          SUSAMUNE_QFT_DISPLAY_LEADING_ZERO);
}

static void EmitCreationSection(FIL *f, int *err,
	                            const struct SusamuneCfg *cfg)
{
	const struct SusamuneCreationCfg *d = &cfg->creation;
	char key[40];
	char line[160];
	u32 i;
	u32 word;

	EmitStr(f, err, "[");
	EmitStr(f, err, CreationSection);
	EmitStr(f, err, "]\r\n");
	for (i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++)
	{
		if (d->colorPresent & SUSAMUNE_CREATION_COLOR(i))
			Emit(f, err, line, (u32)_sprintf(
				line, "%s = %u,%u,%u\r\n", CreationColorKeys[i],
				d->rgb[i][0], d->rgb[i][1], d->rgb[i][2]));
	}
	if (d->timerLabelVisiblePresent)
		Emit(f, err, line, (u32)_sprintf(line, "show_timer_label = %u\r\n",
			d->timerLabelVisible));
	if (d->recentIlPositionPresent)
	{
		Emit(f, err, line, (u32)_sprintf(line, "recent_ils_x = %u\r\n",
			d->recentIlX));
		Emit(f, err, line, (u32)_sprintf(line, "recent_ils_y = %u\r\n",
			d->recentIlY));
		Emit(f, err, line, (u32)_sprintf(line, "recent_ils_scale = %u\r\n",
			d->recentIlScale));
	}
	if (d->reserved0 == SUSAMUNE_CREATION_RECENT_STYLE_MAGIC)
	{
		Emit(f, err, line, (u32)_sprintf(
			line, "recent_ils_text_rgb = %u,%u,%u\r\n",
			d->recentIlTextRgb[0], d->recentIlTextRgb[1], d->recentIlTextRgb[2]));
		Emit(f, err, line, (u32)_sprintf(line, "recent_ils_text_alpha = %u\r\n",
			d->recentIlTextA));
		Emit(f, err, line, (u32)_sprintf(
			line, "recent_ils_background_rgb = %u,%u,%u\r\n",
			d->recentIlBgR, d->recentIlBgG, d->recentIlBgB));
		Emit(f, err, line, (u32)_sprintf(
			line, "recent_ils_background_alpha = %u\r\n", d->recentIlBgA));
		Emit(f, err, line, (u32)_sprintf(
			line, "recent_ils_text_brightness = %u\r\n",
			d->recentIlTextBrightness));
		Emit(f, err, line, (u32)_sprintf(line, "recent_ils_padding = %u\r\n",
			d->recentIlPadding));
	}
	if (d->savestateStyleMagic ==
	    SUSAMUNE_CREATION_SAVESTATE_STYLE_MAGIC)
	{
		Emit(f, err, line, (u32)_sprintf(
			line, "savestate_feedback_x = %u\r\n", d->savestateX));
		Emit(f, err, line, (u32)_sprintf(
			line, "savestate_feedback_y = %u\r\n", d->savestateY));
		Emit(f, err, line, (u32)_sprintf(
			line, "savestate_feedback_scale = %u\r\n", d->savestateScale));
		Emit(f, err, line, (u32)_sprintf(
			line, "savestate_feedback_text_rgb = %u,%u,%u\r\n",
			d->savestateTextRgb[0], d->savestateTextRgb[1],
			d->savestateTextRgb[2]));
		Emit(f, err, line, (u32)_sprintf(
			line, "savestate_feedback_text_alpha = %u\r\n",
			d->savestateTextA));
		Emit(f, err, line, (u32)_sprintf(
			line, "savestate_feedback_background_rgb = %u,%u,%u\r\n",
			d->savestateBgR, d->savestateBgG, d->savestateBgB));
		Emit(f, err, line, (u32)_sprintf(
			line, "savestate_feedback_background_alpha = %u\r\n",
			d->savestateBgA));
		Emit(f, err, line, (u32)_sprintf(
			line, "savestate_feedback_text_brightness = %u\r\n",
			d->savestateTextBrightness));
		Emit(f, err, line, (u32)_sprintf(
			line, "savestate_feedback_padding = %u\r\n",
			d->savestatePadding));
	}
	if (d->achievementStyleMagic ==
	    SUSAMUNE_CREATION_ACHIEVEMENT_STYLE_MAGIC)
	{
		Emit(f, err, line, (u32)_sprintf(
			line, "achievement_popup_x = %u\r\n", d->achievementX));
		Emit(f, err, line, (u32)_sprintf(
			line, "achievement_popup_y = %u\r\n", d->achievementY));
		Emit(f, err, line, (u32)_sprintf(
			line, "achievement_popup_scale = %u\r\n",
			d->achievementScale));
	}
	if (d->stageSessionStyleMagic ==
	    SUSAMUNE_CREATION_STAGE_SESSION_STYLE_MAGIC)
	{
		Emit(f, err, line, (u32)_sprintf(
			line, "stage_session_counter_x = %u\r\n",
			d->stageSessionX));
		Emit(f, err, line, (u32)_sprintf(
			line, "stage_session_counter_y = %u\r\n",
			d->stageSessionY));
		Emit(f, err, line, (u32)_sprintf(
			line, "stage_session_counter_scale = %u\r\n",
			d->stageSessionScale));
	}
	{
		const struct SusamuneWallkickStyleCfg *w = &cfg->wallkickStyle;
		Emit(f, err, line, (u32)_sprintf(line, "wallkick_x = %u\r\n", w->x));
		Emit(f, err, line, (u32)_sprintf(line, "wallkick_y = %u\r\n", w->y));
		Emit(f, err, line, (u32)_sprintf(line, "wallkick_scale = %u\r\n", w->scale));
		Emit(f, err, line, (u32)_sprintf(line, "wallkick_text_alpha = %u\r\n", w->textA));
		Emit(f, err, line, (u32)_sprintf(
			line, "wallkick_background_rgb = %u,%u,%u\r\n",
			w->bgR, w->bgG, w->bgB));
		Emit(f, err, line, (u32)_sprintf(
			line, "wallkick_background_alpha = %u\r\n", w->bgA));
		Emit(f, err, line, (u32)_sprintf(
			line, "wallkick_text_brightness = %u\r\n", w->textBrightness));
		Emit(f, err, line, (u32)_sprintf(line, "wallkick_padding = %u\r\n", w->padding));
		for (i = 0; i < SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT; i++)
			Emit(f, err, line, (u32)_sprintf(
				line, "wallkick_%u_rgb = %u,%u,%u\r\n", i + 1,
				w->rgb[i][0], w->rgb[i][1], w->rgb[i][2]));
		Emit(f, err, line, (u32)_sprintf(
			line, "notification_x = %u\r\n", w->toastX));
		Emit(f, err, line, (u32)_sprintf(
			line, "notification_y = %u\r\n", w->toastY));
		Emit(f, err, line, (u32)_sprintf(
			line, "notification_scale = %u\r\n", w->toastScale));
		Emit(f, err, line, (u32)_sprintf(
			line, "il_pb_popup_x = %u\r\n", w->pbPopupX));
		Emit(f, err, line, (u32)_sprintf(
			line, "il_pb_popup_y = %u\r\n", w->pbPopupY));
		Emit(f, err, line, (u32)_sprintf(
			line, "il_pb_popup_scale = %u\r\n", w->pbPopupScale));
	}
	for (word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++)
	{
		const struct SusamuneCreationWordCfg *w = &d->words[word];
		const u32 n = word + 1;
		Emit(f, err, line, (u32)_sprintf(line, "word%u_text = %s\r\n", n, w->text));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_visible = %u\r\n", n, w->visible));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_x = %u\r\n", n, w->x));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_y = %u\r\n", n, w->y));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_scale = %u\r\n", n, w->scale));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_text_alpha = %u\r\n", n, w->textA));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_text_brightness = %u\r\n", n, w->textBrightness));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_background_r = %u\r\n", n, w->bgR));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_background_g = %u\r\n", n, w->bgG));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_background_b = %u\r\n", n, w->bgB));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_background_alpha = %u\r\n", n, w->bgA));
		Emit(f, err, line, (u32)_sprintf(line, "word%u_padding = %u\r\n", n, w->padding));
		Emit(f, err, line, (u32)_sprintf(
			line, "word%u_text_rgb = %u,%u,%u\r\n", n,
			w->rgb[0][0], w->rgb[0][1], w->rgb[0][2]));
		for (i = 1; i < SUSAMUNE_CREATION_WORD_CHARS; i++)
		{
			if (w->rgb[i][0] == w->rgb[0][0] &&
			    w->rgb[i][1] == w->rgb[0][1] &&
			    w->rgb[i][2] == w->rgb[0][2])
				continue;
			_sprintf(key, "word%u_char_%u_rgb", n, i + 1);
			Emit(f, err, line, (u32)_sprintf(
				line, "%s = %u,%u,%u\r\n", key,
				w->rgb[i][0], w->rgb[i][1], w->rgb[i][2]));
		}
	}
}

static const char kIniBanner[] =
	"; Moonshine settings\r\n"
	"; Written by the Moonshine Launcher. Values are edited in-game from the\r\n"
	"; mod menu; the section for the game version you are running is rewritten\r\n"
	"; whenever the menu is closed with changes pending, so comments added\r\n"
	"; inside it are lost. Everything else in this file is preserved.\r\n"
	";\r\n"
	"; Each disc has settings, binds, input_display, metadata_display,\r\n"
	"; qft_display and creation sections.\r\n"
	"; Their suffix is jp = GMSJ, us = GMSE, or pal = GMSP.\r\n"
	"; Metadata format uses \\n for lines and placeholders such as <x> or <HSpd|.2>.\r\n"
	";\r\n"
	"; Bind values are button combinations like Y+Start, or none. menu_toggle\r\n"
	"; is what opens the mod menu -- if you rebind it to something you cannot\r\n"
	"; reproduce, set it back here.\r\n"
	"\r\n"
	"[" SUSAMUNE_INI_SECTION_NINTENDONT "]\r\n";

// Rewrite the ini with this version's six sections replaced.
//
// The whole point of the copy-through is that the other versions' settings are
// never materialised: they exist only as the text we are reading back here.
// Everything outside our six sections -- other regions, [nintendont], comments,
// blank lines -- lands in the output unchanged and in its original order.
static int WriteIniFile(const struct SusamuneCfg *cfg)
{
	FIL   f;
	char *buf;
	char *line;
	UINT  read = 0;
	int   ret;
	int   err = FR_OK;
	bool  skipping = false;
	bool  wroteSettings = false;
	bool  wroteBinds = false;
	bool  wroteInputDisplay = false;
	bool  wroteMetadataDisplay = false;
	bool  wroteQftDisplay = false;
	bool  wroteCreation = false;

	buf = (char*)malloca(SUSAMUNE_INI_BUF_SIZE, 32);
	if (buf == NULL)
		return FR_NOT_ENOUGH_CORE;
	buf[0] = '\0';

	ret = f_open_char(&f, SusamuneCfgIniPath(), FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_OK)
	{
		// Refuse rather than truncate: a partial copy-through would silently
		// delete another version's settings.
		if (f_size(&f) >= SUSAMUNE_INI_BUF_SIZE)
		{
			f_close(&f);
			free(buf);
			return FR_NOT_ENOUGH_CORE;
		}
		if (f_read(&f, buf, SUSAMUNE_INI_BUF_SIZE - 1, &read) != FR_OK)
			read = 0;
		buf[read] = '\0';
		f_close(&f);
	}

	ret = f_open_char(&f, SusamuneCfgIniPath(), FA_WRITE | FA_CREATE_ALWAYS);
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

		text = Trim(line);  // also drops the \r of a CRLF file

		if (text[0] == '[')
		{
			char *close = strchr(text, ']');
			if (close)
			{
				// Copy the name out rather than punching a NUL into the line:
				// the OTHER branch below has to emit it back verbatim.
				char sect[SUSAMUNE_SECTION_NAME_MAX];
				enum IniSection kind = SECTION_OTHER;
				u32  len = (u32)(close - text - 1);

				if (len < sizeof(sect))
				{
					memcpy(sect, text + 1, len);
					sect[len] = '\0';
					kind = ClassifySection(Trim(sect));
				}

				skipping = (kind != SECTION_OTHER);
				if (kind == SECTION_SETTINGS)
				{
					EmitSettingsSection(&f, &err, cfg);
					wroteSettings = true;
				}
				else if (kind == SECTION_BINDS)
				{
					EmitBindsSection(&f, &err, cfg);
					wroteBinds = true;
				}
				else if (kind == SECTION_INPUT_DISPLAY)
				{
					EmitInputDisplaySection(&f, &err, cfg);
					wroteInputDisplay = true;
				}
				else if (kind == SECTION_METADATA_DISPLAY)
				{
					EmitMetadataDisplaySection(&f, &err, cfg);
					wroteMetadataDisplay = true;
				}
				else if (kind == SECTION_QFT_DISPLAY)
				{
					EmitQftDisplaySection(&f, &err, cfg);
					wroteQftDisplay = true;
				}
				else if (kind == SECTION_CREATION)
				{
					EmitCreationSection(&f, &err, cfg);
					wroteCreation = true;
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

	// Absent sections (a fresh file, or a version this card has not run yet)
	// are appended.
	if (!wroteSettings)
	{
		EmitStr(&f, &err, "\r\n");
		EmitSettingsSection(&f, &err, cfg);
	}
	if (!wroteBinds)
	{
		EmitStr(&f, &err, "\r\n");
		EmitBindsSection(&f, &err, cfg);
	}
	if (!wroteInputDisplay)
	{
		EmitStr(&f, &err, "\r\n");
		EmitInputDisplaySection(&f, &err, cfg);
	}
	if (!wroteMetadataDisplay)
	{
		EmitStr(&f, &err, "\r\n");
		EmitMetadataDisplaySection(&f, &err, cfg);
	}
	if (!wroteQftDisplay)
	{
		EmitStr(&f, &err, "\r\n");
		EmitQftDisplaySection(&f, &err, cfg);
	}
	if (!wroteCreation)
	{
		EmitStr(&f, &err, "\r\n");
		EmitCreationSection(&f, &err, cfg);
	}

	ret = f_close(&f);
	if (err == FR_OK && ret != FR_OK)
		err = ret;
	free(buf);
	return err;
}

// ---------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------

static void InitCreationDefaults(struct SusamuneCreationCfg *cfg)
{
	u32 i;
	u32 word;

	cfg->magic = SUSAMUNE_CREATION_CFG_MAGIC;
	cfg->version = SUSAMUNE_CREATION_CFG_VERSION;
	cfg->reserved0 = SUSAMUNE_CREATION_RECENT_STYLE_MAGIC;
	cfg->colorPresent = 0;
	cfg->recentIlScale = 100;
	cfg->recentIlX = 382;
	cfg->recentIlY = 92;
	cfg->recentIlPositionPresent = 0;
	cfg->timerLabelVisible = 1;
	cfg->timerLabelVisiblePresent = 0;
	cfg->reserved1 = 0;
	cfg->recentIlTextRgb[0] = 255;
	cfg->recentIlTextRgb[1] = 255;
	cfg->recentIlTextRgb[2] = 255;
	cfg->recentIlTextA = 255;
	cfg->recentIlBgR = 12;
	cfg->recentIlBgG = 20;
	cfg->recentIlBgB = 34;
	cfg->recentIlBgA = 205;
	cfg->recentIlTextBrightness = 100;
	cfg->recentIlPadding = 10;
	memset(cfg->reserved2, 0, sizeof(cfg->reserved2));
	cfg->savestateStyleMagic = SUSAMUNE_CREATION_SAVESTATE_STYLE_MAGIC;
	cfg->savestateX = 30;
	cfg->savestateY = 418;
	cfg->savestateScale = 80;
	cfg->savestateTextA = 255;
	cfg->savestateBgR = 0;
	cfg->savestateBgG = 0;
	cfg->savestateBgB = 0;
	cfg->savestateBgA = 200;
	cfg->savestateTextBrightness = 100;
	cfg->savestatePadding = 8;
	cfg->savestateTextRgb[0] = 255;
	cfg->savestateTextRgb[1] = 255;
	cfg->savestateTextRgb[2] = 255;
	cfg->achievementStyleMagic =
		SUSAMUNE_CREATION_ACHIEVEMENT_STYLE_MAGIC;
	cfg->reservedAchievement0 = 0;
	cfg->achievementX = 115;
	cfg->achievementY = 96;
	cfg->achievementScale = 100;
	cfg->stageSessionStyleMagic =
		SUSAMUNE_CREATION_STAGE_SESSION_STYLE_MAGIC;
	cfg->stageSessionX = 570;
	cfg->stageSessionY = 40;
	cfg->stageSessionScale = 100;
	cfg->stageSessionReserved = 0;
	for (i = 0; i < SUSAMUNE_CREATION_COLOR_COUNT; i++)
	{
		cfg->rgb[i][0] = 255;
		cfg->rgb[i][1] = 255;
		cfg->rgb[i][2] = 255;
	}
	cfg->rgb[SUSAMUNE_CREATION_MENU_BG][0] = 24;
	cfg->rgb[SUSAMUNE_CREATION_MENU_BG][1] = 28;
	cfg->rgb[SUSAMUNE_CREATION_MENU_BG][2] = 40;
	for (word = 0; word < SUSAMUNE_CREATION_WORD_COUNT; word++)
	{
		struct SusamuneCreationWordCfg *w = &cfg->words[word];
		w->x = 220;
		w->y = (u16)(80 + word * 42);
		w->scale = 100;
		w->textA = 255;
		w->bgR = w->bgG = w->bgB = 0;
		w->bgA = 128;
		w->textBrightness = 100;
		w->padding = 2;
		w->visible = 0;
		w->length = (u8)_sprintf(w->text, "Custom Text %u", word + 1);
		for (i = 0; i < SUSAMUNE_CREATION_WORD_CHARS; i++)
			w->rgb[i][0] = w->rgb[i][1] = w->rgb[i][2] = 255;
	}
}

static void InitWallkickStyleDefaults(struct SusamuneWallkickStyleCfg *cfg)
{
	u32 i;
	memset(cfg, 0, sizeof(*cfg));
	cfg->magic = SUSAMUNE_WALLKICK_STYLE_MAGIC;
	cfg->version = SUSAMUNE_WALLKICK_STYLE_VERSION;
	cfg->x = 300;
	cfg->y = 106;
	cfg->scale = 90;
	cfg->textA = 255;
	cfg->bgA = 185;
	cfg->textBrightness = 100;
	cfg->padding = 5;
	cfg->notificationStyleMagic = SUSAMUNE_NOTIFICATION_STYLE_MAGIC;
	cfg->toastX = 20;
	cfg->toastY = 412;
	cfg->toastScale = 100;
	cfg->pbPopupX = 320;
	cfg->pbPopupY = 42;
	cfg->pbPopupScale = 100;
	for (i = 0; i < SUSAMUNE_WALLKICK_STYLE_COLOR_COUNT; i++)
		cfg->rgb[i][0] = cfg->rgb[i][1] = cfg->rgb[i][2] = 255;
}

void SusamuneCfgInit(void)
{
	struct SusamuneCfg *cfg = CfgBlock();
	struct SusamuneProgressCfg *progress = ProgressBlock();
	struct SusamuneStagePlaylistsCfg *playlists = StagePlaylistBlock();
	struct SusamuneStageTargetsCfg *targets = StageTargetBlock();
	struct SusamuneSplitStatsCfg *splitStats = SplitStatsBlock();
	const char *region = SUSAMUNE_MOD_REGION_TAG(GAME_ID);
	FIL   f;
	char *buf;
	UINT  read;
	u32   i;
	int   ret;

	// Zero the block unconditionally: it survives across app launches, and a
	// stale one left by an earlier boot would be adopted wholesale by a mod
	// that happens to be running now.
	memset(cfg, 0, sizeof(struct SusamuneCfg));
	memset(progress, 0, sizeof(struct SusamuneProgressCfg));
	memset(playlists, 0, sizeof(struct SusamuneStagePlaylistsCfg));
	memset(targets, 0, sizeof(struct SusamuneStageTargetsCfg));
	memset(splitStats, 0, sizeof(struct SusamuneSplitStatsCfg));
	CfgReady = false;
	PbReady = false;
	ProgressReady = false;
	StagePlaylistReady = false;
	StageTargetReady = false;
	SplitStatsReady = false;

	if (!SusamuneCfgStorageAvailable())
	{
		// The launcher device was different from drive 0 and could not be
		// mounted. Zero magic advertises an unsupported backend to the mod.
		sync_after_write(cfg, sizeof(struct SusamuneCfg));
		sync_after_write(progress, sizeof(struct SusamuneProgressCfg));
		sync_after_write(playlists, sizeof(struct SusamuneStagePlaylistsCfg));
		sync_after_write(targets, sizeof(struct SusamuneStageTargetsCfg));
		sync_after_write(splitStats, sizeof(struct SusamuneSplitStatsCfg));
		return;
	}

	if (region == NULL)
	{
		// Not one of the supported discs, so there is no mod asking for
		// settings and no section of the ini that belongs to this run. Leaving
		// magic zeroed is what makes the mod (if any) report "no launcher".
		sync_after_write(cfg, sizeof(struct SusamuneCfg));
		sync_after_write(progress, sizeof(struct SusamuneProgressCfg));
		sync_after_write(playlists, sizeof(struct SusamuneStagePlaylistsCfg));
		sync_after_write(targets, sizeof(struct SusamuneStageTargetsCfg));
		sync_after_write(splitStats, sizeof(struct SusamuneSplitStatsCfg));
		CfgReady = false;
		return;
	}

	BuildSectionName(SettingsSection, SUSAMUNE_INI_SECTION_SETTINGS, region);
	BuildSectionName(BindsSection, SUSAMUNE_INI_SECTION_BINDS, region);
	BuildSectionName(InputDisplaySection, SUSAMUNE_INI_SECTION_INPUT_DISPLAY, region);
	BuildSectionName(MetadataDisplaySection, SUSAMUNE_INI_SECTION_METADATA_DISPLAY, region);
	BuildSectionName(QftDisplaySection, SUSAMUNE_INI_SECTION_QFT_DISPLAY, region);
	BuildSectionName(CreationSection, SUSAMUNE_INI_SECTION_CREATION, region);

	for (i = 0; i < SUSAMUNE_CFG_MAX_SETTINGS; i++)
		cfg->values[i] = SUSAMUNE_CFG_UNSET;
	for (i = 0; i < SUSAMUNE_CFG_MAX_BINDS; i++)
		cfg->binds[i] = SUSAMUNE_CFG_BIND_UNSET;
	cfg->inputDisplay.magic          = SUSAMUNE_INPUT_CFG_MAGIC;
	cfg->inputDisplay.version        = SUSAMUNE_INPUT_CFG_VERSION;
	cfg->inputDisplay.x              = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->inputDisplay.y              = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->inputDisplay.startVisible   = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.scale          = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.bgR            = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.bgG            = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.bgB            = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.bgA            = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.brightness     = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.valueMode      = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.valueSource    = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->inputDisplay.valuePlacement = SUSAMUNE_INPUT_CFG_U8_UNSET;

	memset(&cfg->metadataDisplay, SUSAMUNE_METADATA_FORMAT_UNSET,
	       sizeof(cfg->metadataDisplay));
	cfg->metadataDisplay.magic        = SUSAMUNE_METADATA_CFG_MAGIC;
	cfg->metadataDisplay.version      = SUSAMUNE_METADATA_CFG_VERSION;
	cfg->metadataDisplay.x            = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->metadataDisplay.y            = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->metadataDisplay.fieldMask    = SUSAMUNE_INPUT_CFG_U16_UNSET;
	cfg->metadataDisplay.startVisible = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->metadataDisplay.scale        = SUSAMUNE_INPUT_CFG_U8_UNSET;
	cfg->metadataDisplay.labelMode    = SUSAMUNE_INPUT_CFG_U8_UNSET;

	cfg->qftDisplay.magic   = SUSAMUNE_QFT_DISPLAY_CFG_MAGIC;
	cfg->qftDisplay.version = SUSAMUNE_QFT_DISPLAY_CFG_VERSION;
	cfg->qftDisplay.present = 0;
	cfg->qftDisplay.slotPresent = 0;
	cfg->metadataStyle.magic = SUSAMUNE_METADATA_STYLE_MAGIC;
	cfg->metadataStyle.version = SUSAMUNE_METADATA_STYLE_VERSION;
	cfg->metadataStyle.present = 0;
	cfg->inputStyle.magic = SUSAMUNE_INPUT_STYLE_MAGIC;
	cfg->inputStyle.version = SUSAMUNE_INPUT_STYLE_VERSION;
	cfg->inputStyle.present = 0;
	InitCreationDefaults(&cfg->creation);
	InitWallkickStyleDefaults(&cfg->wallkickStyle);

	cfg->magic     = SUSAMUNE_CFG_MAGIC;
	cfg->version   = SUSAMUNE_CFG_VERSION;
	cfg->count     = (u16)SETTING_KEY_COUNT;
	cfg->bindCount = (u16)BIND_KEY_COUNT;
	cfg->flags     = SUSAMUNE_CFG_FLAG_INPUT_DISPLAY |
	                 SUSAMUNE_CFG_FLAG_METADATA_DISPLAY |
	                 SUSAMUNE_CFG_FLAG_QFT_DISPLAY |
	                 SUSAMUNE_CFG_FLAG_METADATA_STYLE |
	                 SUSAMUNE_CFG_FLAG_INPUT_STYLE |
	                 SUSAMUNE_CFG_FLAG_CREATION |
	                 SUSAMUNE_CFG_FLAG_WALLKICK_STYLE;
	if (InitPbFiles(cfg, region))
		cfg->flags |= SUSAMUNE_CFG_FLAG_ILING_PBS |
		              SUSAMUNE_CFG_FLAG_ILING_PROFILES;
	if (InitProgressFiles(progress))
		cfg->flags |= SUSAMUNE_CFG_FLAG_PROGRESS;
	if (InitStagePlaylistFiles(playlists))
		cfg->flags |= SUSAMUNE_CFG_FLAG_STAGE_PLAYLISTS;
	InitStageTargetFiles(targets, region);
	cfg->flags |= SUSAMUNE_CFG_FLAG_STAGE_TARGETS;
	InitSplitStatsFiles(splitStats);
	// Publish even a read-only snapshot so the UI can distinguish an unsafe
	// future/unreadable journal from a launcher with no backend.
	cfg->flags |= SUSAMUNE_CFG_FLAG_SPLIT_STATS;

	ret = f_open_char(&f, SusamuneCfgIniPath(), FA_READ | FA_OPEN_EXISTING);
	if (ret == FR_OK)
	{
		buf = (char*)malloca(SUSAMUNE_INI_BUF_SIZE, 32);
		if (buf != NULL)
		{
			read = 0;
			if (f_read(&f, buf, SUSAMUNE_INI_BUF_SIZE - 1, &read) != FR_OK)
				read = 0;
			buf[read] = '\0';
			ParseIni(buf, cfg);
			free(buf);
		}
		f_close(&f);
		if (!SawSettingsSection)
		{
			// The file exists but has never been written by this game version.
			cfg->flags |= SUSAMUNE_CFG_FLAG_NO_CONFIG;
		}
		dbgprintf("Susamune: loaded " SUSAMUNE_INI_PATH " [%s]\r\n", SettingsSection);
	}
	else
	{
		// Every value stays UNSET, so the mod keeps its compiled-in defaults
		// and -- seeing this flag -- writes the file out for us.
		cfg->flags |= SUSAMUNE_CFG_FLAG_NO_CONFIG;
		dbgprintf("Susamune: no " SUSAMUNE_INI_PATH " (%d), using defaults\r\n", ret);
	}

	sync_after_write(cfg, sizeof(struct SusamuneCfg));
	sync_after_write(progress, sizeof(struct SusamuneProgressCfg));
	sync_after_write(playlists, sizeof(struct SusamuneStagePlaylistsCfg));
	sync_after_write(targets, sizeof(struct SusamuneStageTargetsCfg));
	sync_after_write(splitStats, sizeof(struct SusamuneSplitStatsCfg));

	CfgAckSeq = 0;
	CfgReady  = true;
}

bool SusamuneCfgPending(void)
{
	struct SusamuneCfg *cfg = CfgBlock();

	if (!CfgReady)
		return false;

	// Line 0 only: the mod owns it, and reading just that keeps this cheap
	// enough to call every pass of the main loop.
	sync_before_read(cfg, 32);
	return cfg->saveSeq != CfgAckSeq;
}

void SusamuneCfgService(void)
{
	struct SusamuneCfg *cfg = CfgBlock();
	u32 seq;
	int ret;

	// Keep the independent PB payload out of this cache transaction.
	sync_before_read(cfg, 32);
	sync_before_read(cfg->values,
	                 sizeof(cfg->values) + sizeof(cfg->binds) +
	                 sizeof(cfg->inputDisplay) + sizeof(cfg->metadataDisplay));
	sync_before_read(&cfg->qftDisplay,
	                 sizeof(cfg->qftDisplay) + sizeof(cfg->metadataStyle) +
	                 sizeof(cfg->inputStyle) + sizeof(cfg->creation) +
	                 sizeof(cfg->wallkickStyle));
	seq = cfg->saveSeq;

	ret = WriteIniFile(cfg);
	if (ret != FR_OK)
		dbgprintf("Susamune: failed to write " SUSAMUNE_INI_PATH " (%d)\r\n", ret);

	cfg->status = (u32)ret;
	cfg->ackSeq = seq;
	CfgAckSeq   = seq;

	// Line 1 only. Flushing the whole struct would write our stale copy of
	// values[] back over whatever the mod has staged since (see the cache-line
	// ownership note in susamune_cfg.h).
	sync_after_write(&cfg->ackSeq, 32);
}

static bool PbSavePending(struct SusamuneILingProfilesCfg *profiles)
{
	if (!PbReady)
		return false;
	sync_before_read(profiles, 32);
	if (profiles->magic != SUSAMUNE_ILING_PROFILE_MAGIC ||
	    profiles->version != SUSAMUNE_ILING_PROFILE_VERSION)
		return false;
	return profiles->saveSeq != PbAckSeq;
}

static bool ProgressSavePending(struct SusamuneProgressCfg *progress)
{
	if (!ProgressReady)
		return false;
	sync_before_read(progress, 32);
	if (progress->magic != SUSAMUNE_PROGRESS_MAGIC ||
	    progress->version != SUSAMUNE_PROGRESS_VERSION ||
	    progress->achievementBytes != SUSAMUNE_PROGRESS_ACHIEVEMENT_BYTES ||
	    progress->statCount != SUSAMUNE_PROGRESS_STAT_COUNT ||
	    progress->regionCount != SUSAMUNE_PROGRESS_REGION_COUNT)
		return false;
	return progress->saveSeq != ProgressAckSeq;
}

static bool StagePlaylistSavePending(
	struct SusamuneStagePlaylistsCfg *playlists)
{
	if (!StagePlaylistReady)
		return false;
	sync_before_read(playlists, 32);
	if (playlists->magic != SUSAMUNE_STAGE_PLAYLIST_MAGIC ||
	    playlists->version != SUSAMUNE_STAGE_PLAYLIST_VERSION ||
	    playlists->slotCount != SUSAMUNE_STAGE_PLAYLIST_COUNT ||
	    playlists->capacity != SUSAMUNE_STAGE_PLAYLIST_CAPACITY ||
	    playlists->builtinCount != SUSAMUNE_STAGE_PLAYLIST_BUILTIN_COUNT ||
	    playlists->regionCount != SUSAMUNE_STAGE_PLAYLIST_REGION_COUNT ||
	    playlists->actionBytes != SUSAMUNE_STAGE_PLAYLIST_ACTION_BYTES ||
	    playlists->actionSchema != SUSAMUNE_STAGE_PLAYLIST_ACTION_SCHEMA)
		return false;
	return playlists->saveSeq != StagePlaylistAckSeq;
}

static bool StageTargetSavePending(struct SusamuneStageTargetsCfg *targets)
{
	if (!StageTargetReady)
		return false;
	sync_before_read(targets, 32);
	if (targets->magic != SUSAMUNE_STAGE_TARGET_MAGIC ||
	    targets->version != SUSAMUNE_STAGE_TARGET_VERSION ||
	    targets->slotCount != SUSAMUNE_STAGE_TARGET_SLOT_COUNT)
		return false;
	return targets->saveSeq != StageTargetAckSeq;
}

static bool SplitStatsSavePending(struct SusamuneSplitStatsCfg *stats)
{
	if (!SplitStatsReady)
		return false;
	sync_before_read(stats, 32);
	if (stats->magic != SUSAMUNE_SPLIT_STATS_MAGIC ||
	    stats->version != SUSAMUNE_SPLIT_STATS_VERSION ||
	    stats->routeCount != SUSAMUNE_SPLIT_STATS_ROUTE_COUNT ||
	    stats->segmentCount != SUSAMUNE_SPLIT_STATS_SEGMENT_COUNT ||
	    stats->regionCount != SUSAMUNE_SPLIT_STATS_REGION_COUNT ||
	    stats->profileCount != SUSAMUNE_SPLIT_STATS_PROFILE_COUNT ||
	    stats->headerReserved != 0 ||
	    stats->payloadBytes != sizeof(stats->payload) ||
	    stats->schemaHash != SUSAMUNE_SPLIT_STATS_SCHEMA_HASH)
		return false;
	return stats->saveSeq != SplitStatsAckSeq;
}

bool SusamunePbPending(void)
{
	struct SusamuneCfg *cfg = CfgBlock();
	struct SusamuneILingProfilesCfg *profiles = &cfg->ilingProfiles;
	struct SusamuneProgressCfg *progress = ProgressBlock();
	struct SusamuneStagePlaylistsCfg *playlists = StagePlaylistBlock();
	struct SusamuneStageTargetsCfg *targets = StageTargetBlock();
	struct SusamuneSplitStatsCfg *splitStats = SplitStatsBlock();

	if (!CfgReady)
		return false;
	return PbSavePending(profiles) || ProgressSavePending(progress) ||
	       StagePlaylistSavePending(playlists) || StageTargetSavePending(targets) ||
	       SplitStatsSavePending(splitStats);
}

void SusamunePbService(void)
{
	struct SusamuneCfg *cfg = CfgBlock();
	struct SusamuneILingProfilesCfg *profiles = &cfg->ilingProfiles;
	struct SusamuneProgressCfg *progress = ProgressBlock();
	struct SusamuneStagePlaylistsCfg *playlists = StagePlaylistBlock();
	struct SusamuneStageTargetsCfg *targets = StageTargetBlock();
	struct SusamuneSplitStatsCfg *splitStats = SplitStatsBlock();
	u32 seq;
	int ret;

	if (!CfgReady)
		return;

	// Binary journals share this main-loop service slot. Preserve PB priority;
	// progress and playlists follow on later idle passes when several ring.
	if (PbSavePending(profiles))
	{
		seq = profiles->saveSeq;
		sync_before_read(profiles->values,
		                 sizeof(profiles->values) + sizeof(profiles->customNames));

		ret = WritePbProfilesFile(profiles);
		if (ret != FR_OK)
			dbgprintf("Susamune: failed to write ILing PBs (%d)\r\n", ret);

		profiles->status = (u32)ret;
		profiles->ackSeq = seq;
		PbAckSeq = seq;
		sync_after_write(&profiles->ackSeq, 32);
		return;
	}

	if (ProgressSavePending(progress))
	{
		seq = progress->saveSeq;
		sync_before_read(progress->achievements,
		                 sizeof(progress->achievements) +
		                 sizeof(progress->stats));
		ret = WriteProgressFile(progress);
		if (ret != FR_OK)
			dbgprintf("Susamune: failed to write progress (%d)\r\n", ret);

		progress->status = (u32)ret;
		progress->ackSeq = seq;
		ProgressAckSeq = seq;
		sync_after_write(&progress->ackSeq, 32);
		return;
	}

	if (StagePlaylistSavePending(playlists))
	{
		seq = playlists->saveSeq;
		sync_before_read(playlists->counts,
		                 sizeof(*playlists) - __builtin_offsetof(
		                     struct SusamuneStagePlaylistsCfg, counts));
		ret = WriteStagePlaylistFile(playlists);
		if (ret != FR_OK)
			dbgprintf("Susamune: failed to write stage playlists (%d)\r\n", ret);

		playlists->status = (u32)ret;
		playlists->ackSeq = seq;
		StagePlaylistAckSeq = seq;
		sync_after_write(&playlists->ackSeq, 32);
		return;
	}

	if (StageTargetSavePending(targets))
	{
		seq = targets->saveSeq;
		sync_before_read(targets->targets, sizeof(targets->targets));
		ret = WriteStageTargetFile(targets);
		if (ret != FR_OK)
			dbgprintf("Susamune: failed to write stage targets (%d)\r\n", ret);

		targets->status = (u32)ret;
		targets->ackSeq = seq;
		StageTargetAckSeq = seq;
		sync_after_write(&targets->ackSeq, 32);
		return;
	}

	if (!SplitStatsSavePending(splitStats))
		return;
	seq = splitStats->saveSeq;
	sync_before_read(&splitStats->payload, sizeof(splitStats->payload) +
	                 sizeof(splitStats->reserved) +
	                 sizeof(splitStats->tailPad));
	ret = WriteSplitStatsFile(splitStats);
	if (ret != FR_OK)
		dbgprintf("Susamune: failed to write IL split stats (%d)\r\n", ret);

	splitStats->status = (u32)ret;
	splitStats->ackSeq = seq;
	SplitStatsAckSeq = seq;
	sync_after_write(&splitStats->ackSeq, 32);
}
