"""Source contracts for small game-side code-size refactors."""

from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def _source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def _function(source: str, signature: str) -> str:
    match = re.search(signature + r"\s*\{", source)
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    start = match.end() - 1
    depth = 0
    for end in range(start, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start : end + 1]
    raise AssertionError(f"unterminated function: {signature}")


class GhostStorageRefactorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = _source("src/ghost_storage.cpp")

    def test_shared_helpers_stay_out_of_line(self) -> None:
        for name in (
            "requestIdle",
            "requestImportedRefresh",
            "loadTrack",
            "requestPersonalCommand",
            "catalogEntry",
            "copyCatalogName",
        ):
            with self.subTest(name=name):
                self.assertRegex(
                    self.source,
                    rf"__attribute__\(\(noinline\)\)[^{{;]*\b{name}\(",
                )

    def test_refresh_wrappers_keep_distinct_commands(self) -> None:
        refresh = _function(self.source, r"bool refreshImported\(\)")
        scan = _function(self.source, r"bool scanImports\(\)")
        self.assertIn(
            "requestImportedRefresh(SUSAMUNE_GHOST_CMD_LIST)", refresh
        )
        self.assertIn(
            "requestImportedRefresh(SUSAMUNE_GHOST_CMD_IMPORT_SCAN)", scan
        )

        helper = _function(
            self.source, r"bool requestImportedRefresh\(u16 command\)"
        )
        self.assertIn("if (!requestIdle()) return false;", helper)
        self.assertIn("sImportedRefreshQueued = false;", helper)
        self.assertIn("return beginImportedRefresh(command);", helper)

    def test_all_load_wrappers_preserve_namespace_and_destination(self) -> None:
        expected = {
            r"bool load\(int slot\)":
                "loadTrack(false, slot, LOAD_DESTINATION_RACE)",
            r"bool loadObserver\(int slot, bool secondary\)":
                "loadTrack(false, slot,",
            r"bool loadImported\(int slot\)":
                "loadTrack(true, slot,",
            r"bool loadImportedObserver\(int slot, bool secondary\)":
                "loadTrack(true, slot,",
        }
        for signature, call in expected.items():
            with self.subTest(signature=signature):
                self.assertIn(call, _function(self.source, signature))

        helper = _function(
            self.source,
            r"bool loadTrack\(bool imported, int slot,\s*u8 destination\)",
        )
        self.assertIn(
            "imported ? SUSAMUNE_GHOST_IMPORTED_PROFILE : sProfile", helper
        )
        self.assertIn("if (!requestAllowed(profile, slot, true))", helper)
        self.assertIn("sPendingLoadDestination = destination;", helper)
        self.assertRegex(helper, r"imported\s*\?\s*0\s*:\s*sCatalog\[slot\]\.generation")

    def test_catalog_access_and_names_share_one_validator(self) -> None:
        wrappers = {
            r"bool copySlotName\(int slot, char \*out, u32 size\)":
                "copyCatalogName(false, slot, out, size)",
            r"bool copyImportedSlotName\(int slot, char \*out, u32 size\)":
                "copyCatalogName(true, slot, out, size)",
            r"const SusamuneGhostSlotInfo \*slot\(int slot\)":
                "catalogEntry(false, slot)",
            r"const SusamuneGhostSlotInfo \*importedSlot\(int slot\)":
                "catalogEntry(true, slot)",
        }
        for signature, call in wrappers.items():
            with self.subTest(signature=signature):
                self.assertIn(call, _function(self.source, signature))

        copier = _function(
            self.source,
            r"bool copyCatalogName\(bool imported, int slot,\s*char \*out, u32 size\)",
        )
        self.assertLess(copier.index("out[0] = '\\0';"),
                        copier.index("catalogEntry(imported, slot)"))
        self.assertIn("SUSAMUNE_GHOST_SLOT_UNSAFE", copier)
        self.assertIn("SUSAMUNE_GHOST_SLOT_PRESENT", copier)

    def test_personal_delete_and_export_share_request_setup(self) -> None:
        remove = _function(self.source, r"bool remove\(int slot\)")
        export = _function(self.source, r"bool exportShare\(int slot\)")
        self.assertIn(
            "requestPersonalCommand(slot, SUSAMUNE_GHOST_CMD_DELETE, kDeleting)",
            remove,
        )
        self.assertIn(
            "requestPersonalCommand(slot, SUSAMUNE_GHOST_CMD_EXPORT,",
            export,
        )

        helper = _function(
            self.source,
            r"bool requestPersonalCommand\(int slot, u16 command,\s*"
            r"const char \*status\)",
        )
        self.assertIn("requestAllowed(sProfile, slot, true)", helper)
        self.assertIn(
            "beginRequest(command, sProfile, static_cast<u16>(slot), 0, 0, status)",
            helper,
        )


class AttemptCounterRefactorTests(unittest.TestCase):
    def test_shine_and_departure_paths_share_one_saturating_update(self) -> None:
        source = _source("src/attempt_counter.cpp")
        update = _function(
            source, r"void AttemptCounter::update\(bool observerFrame\)"
        )
        self.assertEqual(update.count("0xFFFFu - mSuccessCount"), 1)
        self.assertIn("successEvents = departureEvents;", update)

    def test_shared_update_matches_the_two_original_event_paths(self) -> None:
        counts = (0, 1, 0x7FFF, 0xFFFE, 0xFFFF)
        events = (0, 1, 2, 0xFFFF, 0xFFFFFFFF)
        for count in counts:
            for got_shine in (False, True):
                for shine_events in events:
                    for departure_events in events:
                        with self.subTest(
                            count=count,
                            got_shine=got_shine,
                            shine_events=shine_events,
                            departure_events=departure_events,
                        ):
                            old_count = count
                            old_got = got_shine
                            old_shown = False
                            if shine_events:
                                old_count += min(
                                    shine_events, 0xFFFF - old_count
                                )
                                old_got = True
                                old_shown = True
                            if departure_events and not old_got:
                                old_count += min(
                                    departure_events, 0xFFFF - old_count
                                )
                                old_shown = True

                            new_events = shine_events
                            new_got = got_shine or bool(new_events)
                            if departure_events and not new_got:
                                new_events = departure_events
                            new_count = count
                            new_shown = bool(new_events)
                            if new_events:
                                new_count += min(
                                    new_events, 0xFFFF - new_count
                                )

                            self.assertEqual(
                                (new_count, new_got, new_shown),
                                (old_count, old_got, old_shown),
                            )


class ActionStorageTests(unittest.TestCase):
    def test_egg_hook_lifetime_uses_one_byte(self) -> None:
        source = _source("src/actions.cpp")
        self.assertIn("u8 gEggKillFrames = 0;", source)
        assignments = re.findall(
            r"gEggKillFrames\s*=(?!=)\s*([^;]+);", source
        )
        self.assertEqual(assignments, ["0", "2"])
        self.assertNotRegex(source, r"gEggKillFrames\s*[+]=")


class WallkickDisplayRefactorTests(unittest.TestCase):
    def test_pre_direct_wallslide_flag_is_reused_after_direct(self) -> None:
        source = _source("src/wallkick_display.cpp")
        before = _function(
            source, r"void beforeDirect\(bool active\)"
        )
        after = _function(
            source, r"void afterDirect\(bool active\)"
        )
        self.assertNotIn("sWallslideBefore", source)
        self.assertIn("sWasWallslide = wallslide;", before)
        self.assertIn("const bool trackedWall = sWasWallslide ||", after)

        main = _source("src/main.cpp")
        self.assertEqual(main.count("WallkickDisplay::beforeDirect("), 1)
        self.assertEqual(main.count("WallkickDisplay::afterDirect("), 1)
        self.assertLess(
            main.index("WallkickDisplay::beforeDirect("),
            main.index("WallkickDisplay::afterDirect("),
        )


class SavestateDebugTextTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.savestate = _source("src/savestate.cpp")
        cls.header = _source("include/susamune/savestate.hxx")
        cls.menu = _source("src/menu.cpp")

    def test_debug_label_has_no_owned_textbox_or_heap_path(self) -> None:
        self.assertNotIn("mInfoText", self.savestate)
        self.assertNotIn("mInfoText", self.header)
        self.assertNotIn("new J2DTextBox", self.savestate)
        self.assertNotIn("delete[]", self.savestate)
        self.assertIn('setStatus("ready");', self.savestate)
        self.assertIn("static_assert(sizeof(SavestateManager) == 56", self.header)

    def test_debug_label_keeps_exact_baseline_size_and_color(self) -> None:
        draw = _function(
            self.savestate, r"void SavestateManager::draw\(Menu \*menu\)"
        )
        self.assertRegex(
            draw,
            r"drawTextBaseline\(sStatusBuf,\s*20,\s*60,\s*18,\s*18,\s*"
            r"JUtility::TColor\(255,\s*200,\s*0,\s*255\)\)",
        )

        baseline = _function(
            self.menu,
            r"void Menu::drawTextBaseline\(const char \*s, int x, int y, "
            r"int sizeX, int sizeY,\s*Color color\)",
        )
        self.assertIn("mText.mCharSizeX      = sizeX;", baseline)
        self.assertIn("mText.mCharSizeY      = sizeY;", baseline)
        self.assertIn("mText.mGradientTop    = color;", baseline)
        self.assertIn("mText.mGradientBottom = color;", baseline)
        self.assertIn("mText.draw(x, y);", baseline)
        self.assertNotIn("mFontAscent", baseline)
        self.assertRegex(
            self.menu,
            r"#if ENABLE_SAVESTATE_DBG\s+"
            r"void Menu::drawTextBaseline[\s\S]*?\n#endif",
        )

    def test_snapshot_format_and_sequence_gate_are_unchanged(self) -> None:
        self.assertIn("const u32 kSnapshotVersion = 11u;", self.savestate)
        process = _function(
            self.savestate,
            r"void SavestateManager::processPendingLoad\(\)",
        )
        self.assertLess(process.index("mLoadPending = false;"),
                        process.index("loadState();"))
if __name__ == "__main__":
    unittest.main()
