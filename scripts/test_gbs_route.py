#!/usr/bin/env python3
"""Host contracts for the standalone Gelato GBS route."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
ILING = ROOT / "src" / "iling.cpp"
ENTRIES = ROOT / "src" / "iling_entries.inc"
STAGE_LOADER = ROOT / "src" / "stage_loader.cpp"
RECORDS = ROOT / "src" / "records.cpp"
SETTINGS = ROOT / "include" / "susamune" / "settings_list.h"
DESCS = ROOT / "src" / "settings_descs.inc"
QFT_HEADER = ROOT / "include" / "susamune" / "qft_timer.hxx"
QFT_SOURCE = ROOT / "src" / "qft_timer.cpp"


def array_values(source: str, name: str) -> tuple[int, ...]:
    match = re.search(
        rf"const\s+u8\s+{name}\[\]\s*=\s*\{{(.*?)\}};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"missing {name}")
    body = re.sub(r"//.*", "", match.group(1))
    return tuple(int(value) for value in re.findall(r"\d+", body))


class GelatoGbsContracts(unittest.TestCase):
    def test_catalog_alias_is_append_only_and_distinct(self) -> None:
        entries = ENTRIES.read_text(encoding="utf-8").rstrip()
        iling = ILING.read_text(encoding="utf-8")
        self.assertTrue(
            entries.endswith(
                'SHINE_ROUTE("Gelato GBS", 4, 0, 0, 27, '
                "GROUP_ANY_PERCENT, 125)"
            )
        )
        self.assertEqual(iling.count("#define SHINE_ROUTE"), 4)
        self.assertEqual(iling.count("#undef SHINE_ROUTE"), 4)
        self.assertIn("const int kEntryGelatoGbs = 121;", iling)
        self.assertIn("const int kPbSlotCount = 126;", iling)
        self.assertIn('"SE\\0NE\\0CE\\0GGBS"', iling)
        self.assertIn("if (i == kEntryGelatoGbs) continue;", iling)
        self.assertRegex(
            iling,
            r"acceptsAnySelectedOrigin\(selected\)\s*\|\|\s*"
            r"sSelectedEntry == kEntryGelatoGbs",
        )
        result = iling[iling.index("int entryForResult(u8 result)") :]
        result = result[: result.index("bool readOverlayFlag")]
        selected = result.index("if (validEntry(sSelectedEntry))")
        physical_g1 = result.index("sameDest(sAttemptStart, gbs.start)")
        fallback = result.index("for (int i = 0; i < kEntryCount; i++)")
        self.assertLess(selected, physical_g1)
        self.assertLess(physical_g1, fallback)
        skip_origins = iling[
            iling.index("bool acceptsSkipOrigin") :
            iling.index("int entryForResult")
        ]
        self.assertNotIn("item.result == 27", skip_origins)
        self.assertIn(
            'SHINE("Gelato 8", 4, 7, 7, 27, GROUP_GELATO)',
            entries,
        )
        theory = array_values(iling, "kAnyPercentTheorySlots")
        self.assertEqual(theory[:8], (1, 2, 3, 4, 5, 6, 125, 26))

    def test_fast_any_uses_route_but_legacy_action_survives(self) -> None:
        source = STAGE_LOADER.read_text(encoding="utf-8")
        match = re.search(
            r"constexpr\s+u8\s+kFastAny\[\]\s*=\s*\{(.*?)\};",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        assert match is not None
        entries = tuple(int(value) for value in re.findall(r"\d+", match.group(1)))
        self.assertEqual(entries[4], 121)
        self.assertNotIn("kFastAnyGelatoPosition", source)
        self.assertIn(
            "if (entry == SUSAMUNE_STAGE_PLAYLIST_ACTION_GELATO_1) return 121;",
            source,
        )

    def test_records_cover_direct_g8_and_gbs_separately(self) -> None:
        source = RECORDS.read_text(encoding="utf-8")
        any_slots = array_values(source, "kAnySlots")
        all_slots = array_values(source, "kAllSlots")
        self.assertEqual(len(any_slots), 55)
        self.assertEqual(any_slots[13:15], (125, 26))
        self.assertEqual(len(all_slots), 122)
        self.assertEqual(all_slots.count(27), 1)
        self.assertEqual(all_slots.count(125), 1)
        self.assertIn("{2900, 121, 125}", source)
        self.assertIn("{0, 121, 5, STREAK_FINISH, 0}", source)
        self.assertIn("{2700, 121, 10, STREAK_QFT, 0}", source)
        self.assertIn("const int routeEntry = canonicalResultEntry(entry);", source)
        self.assertIn("if (entry == 121) return WORLD_GELATO;", source)
        self.assertIn("{60, 60, 675, 675}", source)

    def test_moving_platform_freeze_is_append_only(self) -> None:
        setting_rows = re.findall(
            r"X\((SETTING_[A-Z0-9_]+),\s*\"[^\"]+\"\)",
            SETTINGS.read_text(encoding="utf-8"),
        )
        self.assertEqual(
            setting_rows[-3:],
            [
                "SETTING_TIMER_FREEZE_MOVING_PLATFORM",
                "SETTING_LEVEL_SPLITS",
                "SETTING_STREAK_AUTO_RESET",
            ],
        )
        self.assertEqual(
            DESCS.read_text(encoding="utf-8").strip().splitlines()[-3:],
            [
                'SBOOL("Freeze: moving platform", 1, SETTING_CAT_TIMER)',
                'SBOOL("Level splits", 1, SETTING_CAT_TIMER)',
                'SBOOL("Streak auto-reset", 1, SETTING_CAT_CUSTOM)',
            ],
        )
        self.assertIn("void freezeEvent();", QFT_HEADER.read_text(encoding="utf-8"))
        qft = QFT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("void QFTTimer::freezeEvent()", qft)
        self.assertIn("sState->freezeQf = gpMarDirector->unk5C;", qft)
        self.assertIn("sState->freezeFrames = duration;", qft)


if __name__ == "__main__":
    unittest.main()
