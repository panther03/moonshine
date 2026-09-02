#!/usr/bin/env python3
"""Focused host contracts for PR6 Full Reds and catalogue projection."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
ENTRIES = (ROOT / "src" / "iling_entries.inc").read_text(encoding="utf-8")
ILING = (ROOT / "src" / "iling.cpp").read_text(encoding="utf-8")
MENU = (ROOT / "src" / "menu.cpp").read_text(encoding="utf-8")
SPLITS = (ROOT / "src" / "split_stats.cpp").read_text(encoding="utf-8")
SPLIT_API = (ROOT / "include" / "susamune" / "split_stats.hxx").read_text(
    encoding="utf-8"
)
HEADER = (ROOT / "include" / "susamune" / "susamune_cfg.h").read_text(
    encoding="utf-8"
)
RECORDS = (ROOT / "src" / "records.cpp").read_text(encoding="utf-8")
STAGE_LOADER = (ROOT / "src" / "stage_loader.cpp").read_text(
    encoding="utf-8"
)


class FullRedsContracts(unittest.TestCase):
    def test_entries_are_append_only_with_new_pb_slots(self) -> None:
        rows = re.findall(
            r'^SHINE_FULL_REDS\("([^"]+)",\s*([^\n]+)\)$',
            ENTRIES,
            re.MULTILINE,
        )
        self.assertEqual(len(rows), 10)
        self.assertLess(ENTRIES.index('SHINE_ROUTE("Gelato GBS"'),
                        ENTRIES.index('SHINE_FULL_REDS("Bianco 3'))
        slots = [int(row.rsplit(",", 2)[-2].strip()) for _name, row in rows]
        self.assertEqual(slots, list(range(126, 136)))
        self.assertIn("const int kEntryFullRedsFirst = 122;", ILING)
        self.assertIn("const int kEntryFullRedsLast = 131;", ILING)
        self.assertIn('static_assert(kEntryCount == 132', ILING)

    def test_projection_groups_appended_entries_without_renumbering(self) -> None:
        self.assertRegex(
            ILING,
            r"kInsertAfter\[\]\s*=\s*\{\s*"
            r"4, 9, 19, 27, 37, 41, 48, 55, 59, 73, 84\s*\}",
        )
        self.assertRegex(
            ILING,
            r"kInsertedEntry\[\]\s*=\s*\{\s*"
            r"122, 123, 124, 125, 121, 126, 127, 128, 129, 130, 131\s*\}",
        )
        loader = MENU[MENU.index("class StageLoaderTab") :]
        self.assertIn("ILing::menuEntryAt(position)", loader)
        self.assertIn("ILing::beginsMenuGroup(position)", loader)
        self.assertIn("ILing::menuGroupName(position)", loader)

    def test_full_reds_prepare_replay_and_finish_exactly(self) -> None:
        self.assertIn("int fullRedsBaseShine(int entry)", ILING)
        self.assertIn("applyOverlayFlag(0x10000u + fullRedsBase, true, false)",
                      ILING)
        result = ILING[ILING.index("int entryForResult(u8 result)") :]
        result = result[: result.index("bool readOverlayFlag")]
        self.assertIn("fullRedsBaseShine(sSelectedEntry) >= 0", result)
        same_episode = ILING[ILING.index("bool sameEpisodeShine") :]
        same_episode = same_episode[: same_episode.index("const char *label")]
        self.assertIn("return completedEntry == selectedEntry;", same_episode)

    def test_full_reds_force_fludd_in_every_secret(self) -> None:
        start = ILING[ILING.index("bool start(int entry,") :]
        start = start[: start.index("void cancelWarpStart")]
        full_reds = start.index("if (fullRedsBaseShine(entry) >= 0)")
        no_fludd = start.index(
            "ENTRY_CLEAR_RESULT | ENTRY_CARRY_OVERLAY", full_reds
        )
        self.assertLess(full_reds, no_fludd)
        self.assertIn(
            "gSettings.set(SETTING_FLUDD_SECRETS, 2);",
            start[full_reds:no_fludd],
        )

    def test_pinna_six_park_secret_keeps_full_route_identity(self) -> None:
        self.assertIn(
            'SHINE_FULL("Pinna 6 (Full)", 0x0D, 3, 5, 35, GROUP_PINNA)',
            ENTRIES,
        )
        self.assertIn(
            'SHINE_FULL_REDS("Pinna 6 Full Reds", 0x0D, 3, 5, 39, '
            'GROUP_PINNA, 131, 35)',
            ENTRIES,
        )
        internal = ILING[ILING.index("bool isInternalScene") :]
        internal = internal[: internal.index("int entryForStartScene")]
        self.assertRegex(
            internal,
            r"start\.area == TGameSequence::AREA_PINNAPARCO &&\s*"
            r"start\.episode == 3 && start\.gameInt3 == 5 &&\s*"
            r"scene\.mAreaID == 0x29 && scene\.mEpisodeID == 0",
        )

        setup = ILING[ILING.index("void beforeStageSetup()") :]
        setup = setup[: setup.index("void onStageSetup()")]
        carry = setup.split(
            "if (!isPlazaEntry(sSelectedEntry) &&", 1
        )[1].split("clearAttempt();", 1)[0]
        self.assertIn("isInternalScene(sAttemptStart, scene)", carry)
        self.assertIn("applyEntryOverlay(sSelectedEntry);", carry)
        self.assertIn("return;", carry)

    def test_full_reds_result_updates_direct_pb_and_stage_loader(self) -> None:
        result = ILING[ILING.index("int entryForResult(u8 result)") :]
        result = result[: result.index("bool readOverlayFlag")]
        selected = result.split("if (validEntry(sSelectedEntry))", 1)[1]
        selected = selected.split("const Entry &gbs", 1)[0]
        self.assertIn("fullRedsBaseShine(sSelectedEntry) >= 0", selected)
        self.assertIn("selected.result == result", selected)
        self.assertIn("return sSelectedEntry;", selected)

        record = ILING[ILING.index("void recordResult(int entry") :]
        record = record[: record.index("}  // namespace")]
        for call in (
            "Records::onILResult(entry,",
            "StageLoader::onILResult(entry, qf, sRecordsEligible);",
            "recordPB(entry, qf);",
            "SplitStats::onILResult(entry, qf);",
        ):
            self.assertIn(call, record)

        loader = STAGE_LOADER[STAGE_LOADER.index("void onILResult(") :]
        loader = loader[: loader.index("void onILWarpCancelled")]
        self.assertIn("const int expected = expectedResultEntry();", loader)
        self.assertIn("if (entry != expected && !episodeShine)", loader)
        self.assertIn("queueSuccess(", loader)

    def test_bonus_and_hundred_routes_are_streak_choices_again(self) -> None:
        selectable = ILING[ILING.index("bool streakEntrySelectable") :]
        selectable = selectable[: selectable.index("bool sameEpisodeShine")]
        self.assertNotIn("isBonusShine", selectable)
        self.assertIn("entry >= 0 && entry < kEntryCount", selectable)

    def test_split_identities_are_appended_and_persisted(self) -> None:
        for route, entry in enumerate(range(122, 132), start=122):
            self.assertRegex(SPLIT_API, rf"=\s*{route},")
            self.assertIn(f"{{{275 + route - 122}, {entry}, 0}}", SPLITS)
        self.assertIn("ROUTE_COUNT = 132", SPLIT_API)
        self.assertRegex(HEADER, r"SUSAMUNE_SPLIT_STATS_VERSION\s+8u")
        self.assertRegex(HEADER, r"SUSAMUNE_SPLIT_STATS_VERSION_V7\s+7u")
        self.assertIn("struct SusamuneSplitStatsFileV7", HEADER)
        self.assertIn("ReadSplitStatsV7File", (ROOT / "launcher" / "kernel" /
                      "SusamuneCfg.c").read_text(encoding="utf-8"))

    def test_playlists_accept_the_appended_entry_ids(self) -> None:
        self.assertIn("SUSAMUNE_STAGE_PLAYLIST_ROUTE_COUNT 132u", HEADER)

    def test_appended_routes_keep_their_course_stats(self) -> None:
        body = RECORDS.split("World worldForEntry", 1)[1].split(
            "bool worldPBSummary", 1
        )[0]
        expected = {
            "WORLD_BIANCO": (122, 123),
            "WORLD_RICCO": (124,),
            "WORLD_GELATO": (125,),
            "WORLD_PINNA": (126, 127),
            "WORLD_SIRENA": (128, 129),
            "WORLD_NOKI": (130,),
            "WORLD_PIANTA": (131,),
        }
        for world, entries in expected.items():
            for entry in entries:
                self.assertRegex(
                    body,
                    rf"entry == {entry}(?: \|\| entry == \d+)?\) return {world};",
                )


if __name__ == "__main__":
    unittest.main()
