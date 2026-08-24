#!/usr/bin/env python3
"""Host contracts for the Gelato 6 fish and blue-coin bird patterns."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SETTINGS = ROOT / "include" / "susamune" / "settings_list.h"
DESCS = ROOT / "src" / "settings_descs.inc"
FISH = ROOT / "src" / "gelato_patterns.cpp"
BIRDS = ROOT / "src" / "pattern_selector.cpp"
RUNTIME = ROOT / "src" / "rng_control.cpp"
PATCHES = ROOT / "scripts" / "patches.py"


class GelatoPatternTests(unittest.TestCase):
    def test_settings_are_append_only_numbered_patterns(self) -> None:
        rows = re.findall(
            r'X\((SETTING_[A-Z0-9_]+),\s*"[^"]+"\)',
            SETTINGS.read_text(encoding="utf-8"),
        )
        self.assertEqual(
            rows[-2:],
            [
                "SETTING_GELATO_RED_COIN_FISH_PATTERN",
                "SETTING_GELATO_BLUE_BIRD_PATTERN",
            ],
        )
        descs = DESCS.read_text(encoding="utf-8")
        self.assertIn(
            'SCHOICE("Gelato 6 red-coin fish", 0, CHOICES_GELATO_PATTERN',
            descs,
        )
        self.assertIn(
            'SCHOICE("Blue-coin birds", 0, CHOICES_GELATO_PATTERN',
            descs,
        )

    def test_fish_hook_covers_all_three_revisions(self) -> None:
        source = PATCHES.read_text(encoding="utf-8").lower()
        row = source.split("'sym': 'susamunegelatofishrandomnext'", 1)[0]
        row = row.rsplit("{", 1)[1]
        for address in (0x80363E48, 0x80005DDC, 0x80005DDC):
            self.assertIn(f"0x{address:08x}", row)
        self.assertIn("'type': patchtype.bl", source.split(
            "'sym': 'susamunegelatofishrandomnext'", 1
        )[1].split("}", 1)[0])

    def test_fish_patterns_preserve_retail_roll_and_only_choose_first_turn(self) -> None:
        source = FISH.read_text(encoding="utf-8")
        wrapper = source.split(
            'extern "C" int susamuneGelatoFishRandomNext', 1
        )[1]
        self.assertEqual(wrapper.count("getRandomNextIndex"), 1)
        self.assertLess(
            wrapper.index("getRandomNextIndex"),
            wrapper.index("SETTING_GELATO_RED_COIN_FISH_PATTERN"),
        )
        self.assertIn('"kaiyu16"', source)
        self.assertIn('"kaiyu17"', source)
        self.assertIn("mAreaID != kGelatoArea", wrapper)
        self.assertIn("mEpisodeID != kGelatoSixEpisode", wrapper)
        self.assertIn("previous != -1", wrapper)
        self.assertIn("filter != (u32)-1", wrapper)
        self.assertIn("node->mNeighborCount != 2", wrapper)

    def test_four_patterns_cover_the_two_by_two_direction_choices(self) -> None:
        patterns = {
            pattern: tuple(
                ((pattern - 1) >> (1 - actor_index)) & 1
                for actor_index in range(2)
            )
            for pattern in range(1, 5)
        }
        self.assertEqual(
            patterns,
            {1: (0, 0), 2: (0, 1), 3: (1, 0), 4: (1, 1)},
        )
        formula = "((pattern - 1) >> (1 - actorIndex)) & 1"
        self.assertIn(formula, FISH.read_text(encoding="utf-8"))
        self.assertIn(formula, BIRDS.read_text(encoding="utf-8"))

    def test_blue_birds_are_actor_and_blue_coin_scoped_across_courses(self) -> None:
        source = BIRDS.read_text(encoding="utf-8")
        selector = source.split("int selectedBlueCoinBirdNode", 1)[1].split(
            "int selectedNode", 1
        )[0]
        self.assertIn("SETTING_GELATO_BLUE_BIRD_PATTERN", selector)
        self.assertIn("kAnimalBirdType = 0x00800001u", source)
        self.assertIn("kBlueCoinType = 0x20000010u", source)
        self.assertIn("actor + 0x150", selector)
        self.assertNotIn("mAreaID", selector)
        self.assertNotIn("mEpisodeID", selector)
        self.assertNotIn("mRailName", selector)
        self.assertIn("blueBirdSlot(enemy->mKeyName)", selector)
        self.assertIn("previous != -1", selector)
        self.assertIn("node->mNeighborCount != 2", selector)

    def test_blue_bird_override_still_advances_retail_rng(self) -> None:
        source = BIRDS.read_text(encoding="utf-8")
        branch = source.split("if (blueBird >= 0)", 1)[1].split(
            "} else {", 1
        )[0]
        self.assertEqual(branch.count("getRandomNextIndex"), 1)
        self.assertIn("next = blueBird", branch)

    def test_patterns_follow_non_invalidating_practice_policy(self) -> None:
        source = RUNTIME.read_text(encoding="utf-8")
        invalidation = source.split("bool rngControlInvalidatesIl()", 1)[1].split(
            'extern "C"', 1
        )[0]
        self.assertNotIn("SETTING_GELATO_RED_COIN_FISH_PATTERN", invalidation)
        self.assertNotIn("SETTING_GELATO_BLUE_BIRD_PATTERN", invalidation)


if __name__ == "__main__":
    unittest.main()
