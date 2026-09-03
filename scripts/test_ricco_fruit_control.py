#!/usr/bin/env python3
"""Source contracts for Ricco's durian-only fruit launcher control."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "src" / "ricco_fruit.cpp"
PATCHES = ROOT / "scripts" / "patches.py"
MAIN = ROOT / "src" / "main.cpp"


class RiccoFruitControlTests(unittest.TestCase):
    def test_all_three_selectors_and_first_velocity_are_hooked(self) -> None:
        source = PATCHES.read_text(encoding="utf-8").lower()
        sites = (
            (0x801A50F8, 0x801CD540, 0x801C53F8),
            (0x801A5214, 0x801CD65C, 0x801C5514),
            (0x801A5330, 0x801CD778, 0x801C5630),
            (0x801A5464, 0x801CD8AC, 0x801C5764),
        )
        self.assertEqual(source.count("'sym': 'griccofruitrandshim'"), 4)
        for regions in sites:
            for address in regions:
                self.assertIn(f"0x{address:08x}", source)

        # The second velocity roll must remain a direct retail rand call.
        for address in (0x801A54A4, 0x801CD8EC, 0x801C57A4):
            self.assertNotIn(f"'jp': 0x{address:08x}", source)
            self.assertNotIn(f"'us': 0x{address:08x}", source)
            self.assertNotIn(f"'pal': 0x{address:08x}", source)

    def test_one_retail_roll_is_preserved_per_reached_site(self) -> None:
        source = RUNTIME.read_text(encoding="utf-8")
        self.assertEqual(source.count("const int retail = rand();"), 1)
        self.assertIn("const int kDurianRoll = 0x6000;", source)
        self.assertTrue(0x4CCD <= 0x6000 <= 0x6666)
        for offset in ("0x134", "0x250", "0x36C", "0x4A0"):
            self.assertIn(f"kFireObj + {offset}", source)
        self.assertNotIn("kFireObj + 0x4E0", source)

    def test_retry_state_clears_on_success_failure_and_stage_setup(self) -> None:
        source = RUNTIME.read_text(encoding="utf-8")
        main = MAIN.read_text(encoding="utf-8")
        self.assertGreaterEqual(source.count("sPendingLauncher = nullptr;"), 4)
        self.assertIn("riccoFruitControlBeforeStageSetup();", main)
        self.assertIn("riccoFruitControlInit();", main)
        velocity = source.split("site == kFireObj + 0x4A0", 1)[1]
        self.assertIn("return retail;", velocity)
        self.assertNotIn("return kDurianRoll", velocity)


if __name__ == "__main__":
    unittest.main()
