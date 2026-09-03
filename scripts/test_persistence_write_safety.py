#!/usr/bin/env python3
"""Source contracts for lossless copy-through persistence writes."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
KERNEL = ROOT / "launcher" / "kernel" / "SusamuneCfg.c"
LOADER = ROOT / "launcher" / "loader" / "source" / "SusamuneIni.c"
FF_UTF8_C = ROOT / "launcher" / "fatfs" / "ff_utf8.c"
FF_UTF8_H = ROOT / "launcher" / "fatfs" / "ff_utf8.h"
SETTINGS = ROOT / "src" / "settings.cpp"
SETTINGS_EMULATOR = ROOT / "src" / "settings_emulator.cpp"
MENU = ROOT / "src" / "menu.cpp"
RECORDS = ROOT / "src" / "records.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class IniCopyThroughSafetyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.kernel_source = KERNEL.read_text(encoding="utf-8")
        cls.loader_source = LOADER.read_text(encoding="utf-8")
        cls.kernel = function_body(
            cls.kernel_source,
            "static int WriteIniFile(const struct SusamuneCfg *cfg)",
        )
        cls.loader = function_body(
            cls.loader_source,
            "int SusamuneIniSave(const char *device)",
        )
        cls.kernel_commit = function_body(
            cls.kernel_source, "static int CommitIniFile("
        )
        cls.loader_commit = function_body(
            cls.loader_source, "static int CommitIniFile("
        )
        cls.kernel_recover = function_body(
            cls.kernel_source, "static int RecoverIniFile("
        )
        cls.loader_recover = function_body(
            cls.loader_source, "static int RecoverIniFile("
        )
        cls.kernel_aliases = function_body(
            cls.kernel_source, "static int CheckIniAliases("
        )
        cls.loader_aliases = function_body(
            cls.loader_source, "static int CheckIniAliases("
        )

    def assert_copy_through_read_is_lossless(self, body: str) -> None:
        read_only = body.index("if (f.obj.attr & AM_RDO)")
        read = body.index("ret = f_read(&f, buf")
        validation = body.index(
            "if (ret != FR_OK || read != fileSize || closeRet != FR_OK)"
        )
        temp_write = body.index(
            "f_open_char(&f, tempPath, FA_WRITE | FA_CREATE_ALWAYS)"
        )
        self.assertLess(read_only, read)
        self.assertLess(read, validation)
        self.assertLess(validation, temp_write)
        self.assertEqual(body.count("read = 0;"), 1)
        self.assertIn(
            "else if (ret != FR_NO_FILE && ret != FR_NO_PATH)", body
        )
        self.assertIn("return closeRet != FR_OK ? closeRet : FR_DISK_ERR;", body)
        self.assertIn("return closeRet == FR_OK ? FR_DENIED : closeRet;", body)
        self.assertNotIn(
            "f_open_char(&f, path, FA_WRITE | FA_CREATE_ALWAYS)", body
        )
        self.assertLess(body.rindex("ret = f_close(&f)"),
                        body.index("CommitIniFile("))

    def assert_atomic_commit_order(self, body: str) -> None:
        remove_stale = body.index("f_unlink_char(backupPath)")
        park_original = body.index("f_rename_char(path, backupPath)")
        install_temp = body.index("f_rename_char(tempPath, path)")
        restore_original = body.index("f_rename_char(backupPath, path)")
        self.assertLess(remove_stale, park_original)
        self.assertLess(park_original, install_temp)
        self.assertLess(install_temp, restore_original)
        self.assertNotIn("f_unlink_char(path)", body)

    def assert_startup_recovery(self, source: str, body: str,
                                load_signature: str) -> None:
        main_probe = body.index("IniPathExists(path, &exists)")
        backup_probe = body.index("IniPathExists(backupPath, &exists)")
        restore = body.index("f_rename_char(backupPath, path)")
        temp_probe = body.index("IniPathExists(tempPath, &exists)")
        self.assertLess(main_probe, backup_probe)
        self.assertLess(backup_probe, restore)
        self.assertLess(restore, temp_probe)
        # An orphan temp may be a partial first write, so it is never promoted.
        self.assertNotIn("f_rename_char(tempPath, path)", body)

        load = function_body(source, load_signature)
        recover = load.index("RecoverIniFile(")
        open_main = load.index("FA_READ | FA_OPEN_EXISTING")
        self.assertLess(recover, open_main)

    def test_kernel_never_truncates_after_an_incomplete_source_read(self) -> None:
        self.assert_copy_through_read_is_lossless(self.kernel)

    def test_launcher_never_truncates_after_an_incomplete_source_read(self) -> None:
        self.assert_copy_through_read_is_lossless(self.loader)

    def test_both_writers_park_the_original_before_installing_temp(self) -> None:
        self.assert_atomic_commit_order(self.kernel_commit)
        self.assert_atomic_commit_order(self.loader_commit)

    def test_both_startup_paths_restore_an_interrupted_backup(self) -> None:
        self.assert_startup_recovery(
            self.kernel_source,
            self.kernel_recover,
            "void SusamuneCfgInit(void)",
        )
        self.assert_startup_recovery(
            self.loader_source,
            self.loader_recover,
            "void SusamuneIniLoad(const char *device)",
        )

    def test_rename_cross_links_are_refused_before_any_mutation(self) -> None:
        for source, recover, commit, aliases, writer_signature in (
            (
                self.kernel_source,
                self.kernel_recover,
                self.kernel_commit,
                self.kernel_aliases,
                "static int WriteIniFile(const struct SusamuneCfg *cfg)",
            ),
            (
                self.loader_source,
                self.loader_recover,
                self.loader_commit,
                self.loader_aliases,
                "int SusamuneIniSave(const char *device)",
            ),
        ):
            self.assertEqual(aliases.count("IniPathCluster("), 3)
            self.assertEqual(aliases.count("== tempCluster"), 1)
            self.assertEqual(aliases.count("== backupCluster"), 2)
            self.assertIn("return FR_INT_ERR;", aliases)

            recover_guard = recover.index("CheckIniAliases(")
            recover_probe = recover.index("IniPathExists(path, &exists)")
            recover_rename = recover.index("f_rename_char(backupPath, path)")
            self.assertLess(recover_guard, recover_probe)
            self.assertLess(recover_guard, recover_rename)

            commit_guard = commit.index("CheckIniAliases(")
            commit_unlink = commit.index("f_unlink_char(backupPath)")
            commit_rename = commit.index("f_rename_char(path, backupPath)")
            self.assertLess(commit_guard, commit_unlink)
            self.assertLess(commit_guard, commit_rename)

            writer = function_body(source, writer_signature)
            self.assertLess(writer.index("RecoverIniFile(path)"),
                            writer.index("FA_CREATE_ALWAYS"))

    def test_utf8_fatfs_rename_wrapper_preserves_both_paths(self) -> None:
        header = FF_UTF8_H.read_text(encoding="utf-8")
        source = FF_UTF8_C.read_text(encoding="utf-8")
        self.assertIn(
            "FRESULT f_rename_char(const char* path_old, const char* path_new);",
            header,
        )
        rename = function_body(source, "FRESULT f_rename_char(")
        old_convert = rename.index("char_to_wchar(path_old)")
        preserve = rename.index("memcpy(old_path, scratch.path")
        new_convert = rename.index("char_to_wchar(path_new)")
        call = rename.index("f_rename(old_path, scratch.path)")
        self.assertLess(old_convert, preserve)
        self.assertLess(preserve, new_convert)
        self.assertLess(new_convert, call)

    def test_acknowledged_write_errors_retry_quietly(self) -> None:
        for path in (SETTINGS, SETTINGS_EMULATOR):
            source = path.read_text(encoding="utf-8")
            poll = function_body(source, "SettingsSaveState Settings::pollSave()")
            retry_gate = poll.index(
                "mSaveState == SETTINGS_SAVE_ERROR && mDirty"
            )
            save = poll.index("save();", retry_gate)
            pending_gate = poll.index(
                "mSaveState != SETTINGS_SAVE_PENDING", retry_gate
            )
            self.assertLess(retry_gate, save)
            self.assertLess(save, pending_gate)
            self.assertIn("mDirty = true;", poll)
            self.assertIn("mSaveWaitFrames = kSaveRetryFrames;", poll)

        menu = function_body(
            MENU.read_text(encoding="utf-8"), "void Menu::pollSettingsSave()"
        )
        self.assertLess(menu.index("gSettings.pollSave()"),
                        menu.index("if (!mSaveWatch)"))

    def test_quarantined_fatfs_aliases_are_not_retried_forever(self) -> None:
        source = SETTINGS.read_text(encoding="utf-8")
        poll = function_body(source, "SettingsSaveState Settings::pollSave()")
        self.assertIn("const u32 kFatFsInternalError = 2;", source)
        self.assertGreaterEqual(
            poll.count("mLastError != kFatFsInternalError"), 2
        )

    def test_init_reads_cannot_author_defaults_after_a_transient_failure(self) -> None:
        kernel_init = function_body(
            self.kernel_source, "void SusamuneCfgInit(void)"
        )
        read = kernel_init.index("ret = f_read(&f, buf")
        complete = kernel_init.index(
            "ret == FR_OK && read == fileSize && closeRet == FR_OK"
        )
        parse = kernel_init.index("ParseIni(buf, cfg)")
        self.assertLess(read, complete)
        self.assertLess(complete, parse)
        self.assertIn(
            "cfg->flags |= SUSAMUNE_CFG_FLAG_SETTINGS_READ_ERROR",
            kernel_init,
        )
        self.assertIn(
            "mLastError = kFatFsInternalError",
            SETTINGS.read_text(encoding="utf-8"),
        )
        self.assertIn("CfgReady  = settingsReadSafe;", kernel_init)
        self.assertEqual(
            kernel_init.count("cfg->flags |= SUSAMUNE_CFG_FLAG_NO_CONFIG"), 1
        )

        loader_load = function_body(
            self.loader_source, "void SusamuneIniLoad(const char *device)"
        )
        self.assertLess(loader_load.index("LoadSafe = false;"),
                        loader_load.index("RecoverIniFile(path)"))
        self.assertLess(loader_load.index("ret = f_read(&f, buf"),
                        loader_load.index(
                            "ret != FR_OK || read != fileSize || closeRet != FR_OK"
                        ))
        self.assertLess(loader_load.index(
                            "ret != FR_OK || read != fileSize || closeRet != FR_OK"
                        ), loader_load.index("ParseIni(buf)"))
        needs_write = function_body(
            self.loader_source, "bool SusamuneIniNeedsWrite(void)"
        )
        writable = function_body(
            self.loader_source, "bool SusamuneIniWritable(const char *device)"
        )
        save = function_body(
            self.loader_source, "int SusamuneIniSave(const char *device)"
        )
        self.assertIn("LoadSafe && !SawSection", needs_write)
        self.assertIn("if (!LoadSafe)", writable)
        self.assertIn("if (!LoadSafe)", save)

    def test_exfat_file_sizes_cannot_wrap_past_the_copy_buffer(self) -> None:
        for body in (
            self.kernel,
            self.loader,
            function_body(self.kernel_source, "void SusamuneCfgInit(void)"),
            function_body(self.loader_source,
                          "void SusamuneIniLoad(const char *device)"),
        ):
            self.assertIn("FSIZE_t fileSize", body)
            self.assertNotIn("(u32)f_size(&f)", body)

    def test_generated_output_cannot_commit_past_the_next_boot_read_cap(self) -> None:
        for body, cap in (
            (self.kernel, "SUSAMUNE_INI_BUF_SIZE"),
            (self.loader, "SUSA_INI_BUF_SIZE"),
        ):
            output_cap = body.index(f"f_size(&f) >= {cap}",
                                    body.index("if (!wrote"))
            final_close = body.rindex("ret = f_close(&f)")
            commit = body.index("CommitIniFile(")
            self.assertLess(output_cap, final_close)
            self.assertLess(final_close, commit)
            self.assertIn("err = FR_NOT_ENOUGH_CORE;", body[output_cap:final_close])

    def test_timeout_keeps_the_original_request_pending(self) -> None:
        for path in (SETTINGS, SETTINGS_EMULATOR):
            poll = function_body(
                path.read_text(encoding="utf-8"),
                "SettingsSaveState Settings::pollSave()",
            )
            self.assertIn("return SETTINGS_SAVE_TIMEOUT;", poll)
            self.assertNotIn("mSaveState = SETTINGS_SAVE_TIMEOUT", poll)
            self.assertIn("mSaveWaitFrames <= kSaveTimeoutFrames", poll)
            self.assertIn("mSaveState = SETTINGS_SAVE_OK;", poll)
            for dirty in (
                "mDirty", "gBinds.dirty()", "gInputDisplay.dirty()",
                "gMetadataDisplay.dirty()", "gQftDisplay.dirty()",
                "gCreationExtras.dirty()",
            ):
                self.assertIn(dirty, poll)
            self.assertIn("save();", poll[poll.index("mSaveState = SETTINGS_SAVE_OK;"):])

    def test_current_achievement_runtime_widths_are_enforced(self) -> None:
        records = RECORDS.read_text(encoding="utf-8")
        self.assertIn("Records::ACHIEVEMENT_ID_END <= 0x100", records)
        self.assertIn("Records::ACHIEVEMENT_ACTIVE_COUNT <= 0xff", records)


if __name__ == "__main__":
    unittest.main()
