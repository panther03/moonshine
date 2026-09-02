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
ILING_HEADER = ROOT / "include" / "susamune" / "iling.hxx"
MENU = ROOT / "src" / "menu.cpp"


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
        self.assertIn(
            'SHINE_ROUTE("Gelato GBS", 4, 0, 0, 27, '
            "GROUP_ANY_PERCENT, 125)",
            entries,
        )
        self.assertEqual(iling.count("#define SHINE_ROUTE"), 4)
        self.assertEqual(iling.count("#undef SHINE_ROUTE"), 4)
        self.assertIn("const int kEntryGelatoGbs = 121;", iling)
        self.assertIn(
            "const int kPbSlotCount = SUSAMUNE_ILING_PB_SLOT_COUNT;",
            iling,
        )
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

    def test_ils_tab_projects_gbs_into_the_gelato_section(self) -> None:
        iling = ILING.read_text(encoding="utf-8")
        header = ILING_HEADER.read_text(encoding="utf-8")
        menu = MENU.read_text(encoding="utf-8")

        self.assertRegex(
            iling,
            r"kMenuGroupFirst\[GROUP_COUNT\]\s*=\s*\{\s*"
            r"0, 15, 28, 43, 59, 74, 88, 101, 103, 105, 121\s*\}",
        )
        self.assertIn("121, 126, 127, 128, 129, 130, 131", iling)
        for declaration in (
            "int menuEntryAt(int position);",
            "int menuPositionOf(int entry);",
            "int jumpMenuGroup(int position, int direction);",
            "bool beginsMenuGroup(int position);",
            "const char *menuGroupName(int position);",
        ):
            self.assertIn(declaration, header)
        ils_tab = menu[
            menu.index("class ILingTab") : menu.index("class GhostsTab")
        ]
        self.assertIn("ILing::menuEntryAt(position)", ils_tab)
        self.assertIn("ILing::beginsMenuGroup(position)", ils_tab)
        self.assertIn("ILing::menuGroupName(position)", ils_tab)
        self.assertIn("ILing::jumpMenuGroup(position, direction)", ils_tab)
        loader_tab = menu[menu.index("class StageLoaderTab") :]
        self.assertIn("ILing::menuEntryAt(position)", loader_tab)
        self.assertIn("ILing::beginsMenuGroup(position)", loader_tab)

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
        first = setting_rows.index("SETTING_TIMER_FREEZE_MOVING_PLATFORM")
        self.assertEqual(
            setting_rows[first : first + 3],
            [
                "SETTING_TIMER_FREEZE_MOVING_PLATFORM",
                "SETTING_LEVEL_SPLITS",
                "SETTING_STREAK_AUTO_RESET",
            ],
        )
        desc_lines = DESCS.read_text(encoding="utf-8").strip().splitlines()
        first_desc = desc_lines.index(
            'SBOOL("Freeze: moving platform", 1, SETTING_CAT_TIMER)'
        )
        self.assertEqual(
            desc_lines[first_desc : first_desc + 3],
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
