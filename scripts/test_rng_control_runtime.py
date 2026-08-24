#!/usr/bin/env python3
"""Host contracts for the V2.2 boss and Ricco-crane RNG controls."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
SETTINGS = ROOT / "include" / "susamune" / "settings_list.h"
RUNTIME = ROOT / "src" / "rng_control.cpp"
PATTERN = ROOT / "src" / "pattern_selector.cpp"
PATCHES = ROOT / "scripts" / "patches.py"


class RngControlRuntimeTests(unittest.TestCase):
    def test_new_persistence_ids_are_append_only(self) -> None:
        rows = re.findall(
            r'X\((SETTING_[A-Z0-9_]+),\s*"[^"]+"\)',
            SETTINGS.read_text(encoding="utf-8"),
        )
        self.assertEqual(
            rows[-9:-4],
            [
                "SETTING_KING_BOO_ALWAYS_FRUIT",
                "SETTING_PETEY_NO_TORNADO",
                "SETTING_PETEY_ROUTE",
                "SETTING_RICCO_CRANE_SPEED",
                "SETTING_RICCO_FRUIT_MACHINE",
            ],
        )
        self.assertEqual(
            rows[-4:],
            [
                "SETTING_BIANCO_SKEETER_ROUTE",
                "SETTING_ROLLOUT_DISPLAY",
                "SETTING_DUST_DISPLAY",
                "SETTING_TIMER_FREEZE_AIRGRAB",
            ],
        )

    def test_narrow_hook_sites_cover_every_region(self) -> None:
        source = PATCHES.read_text(encoding="utf-8").lower()
        expected = {
            "susamuneforcekingboofruit": (0x802D0F78, 0x800BE8E8, 0x800B7F88),
            "gcraneupdownrandshim": (0x801A5ED0, 0x801CE318, 0x801C61D0),
            "gcranerotyrandshim": (0x801A625C, 0x801CE6A4, 0x801C655C),
            "gbiancoskeeterrandshim": (0x8033DFA4, 0x8012C6C8, 0x80125BCC),
        }
        for symbol, addresses in expected.items():
            self.assertIn(f"'sym': '{symbol}'", source)
            for address in addresses:
                self.assertIn(f"0x{address:08x}", source)

    def test_cranes_reuse_one_retail_roll_in_exact_bands(self) -> None:
        source = RUNTIME.read_text(encoding="utf-8")
        up_down = source.split(
            'extern "C" int susamuneCraneUpDownRandImpl', 1
        )[1].split('extern "C" int susamuneCraneRotYRandImpl', 1)[0]
        rot_y = source.split(
            'extern "C" int susamuneCraneRotYRandImpl', 1
        )[1].split('extern "C" int susamuneBiancoSkeeterRandImpl', 1)[0]
        self.assertEqual(up_down.count("const int retail = rand();"), 1)
        self.assertEqual(rot_y.count("const int retail = rand();"), 1)
        self.assertIn("choice * 20 - 19 + ((u32)roll * 20u >> 15)", source)
        self.assertIn("0.0015f * (f32)percent", source)
        self.assertIn("retailSpeed * (f32)percent * 0.01f", source)
        self.assertIn("if (percent) *speed = retailSpeed;", source)

        for choice in range(1, 6):
            values = {
                choice * 20 - 19 + ((roll * 20) >> 15)
                for roll in range(0x8000)
            }
            self.assertEqual(min(values), (choice - 1) * 20 + 1)
            self.assertEqual(max(values), choice * 20)
            self.assertEqual(len(values), 20)

    def test_petey_route_and_tornado_are_semantic(self) -> None:
        runtime = RUNTIME.read_text(encoding="utf-8")
        pattern = PATTERN.read_text(encoding="utf-8")
        self.assertIn("0x408000F0u", runtime)
        self.assertIn("0x60000000u", runtime)
        self.assertIn("SETTING_PETEY_NO_TORNADO", runtime)
        self.assertIn("SETTING_PETEY_ROUTE", pattern)
        self.assertIn("SUSAMUNE_VT_BOSS_PAKKUN", pattern)
        self.assertIn("mAreaID == 2", pattern)
        self.assertIn("mEpisodeID == 4", pattern)
        table = re.search(
            r"const u8 kPeteyPath\[\]\s*=\s*\{([^}]*)\}", pattern, re.DOTALL
        )
        self.assertIsNotNone(table)
        assert table is not None
        values = tuple(int(v) for v in re.findall(r"\b\d+\b", table.group(1)))
        self.assertEqual(
            values,
            (1, 2, 3, 10, 3, 4, 7, 8, 12, 10, 8, 4, 16, 3, 11, 12, 9, 3),
        )
        self.assertEqual((values[3], values[10], values[8], values[12]),
                         (10, 8, 12, 16))

    def test_bianco_skeeter_is_instance_scoped_and_rng_neutral(self) -> None:
        source = RUNTIME.read_text(encoding="utf-8")
        wrapper = source.split(
            'extern "C" int susamuneBiancoSkeeterRandImpl', 1
        )[1].split('extern "C" void susamuneCraneUpDownControl', 1)[0]
        self.assertEqual(wrapper.count("const int retail = rand();"), 1)
        self.assertIn("return retail;", wrapper)
        self.assertIn("kSkeeterRouteRolls[choice - 1]", wrapper)
        self.assertIn("0x458641E5u, 0x450FC000u, 0xC6221F8Du", source)
        self.assertIn("0x0000u, 0x7000u, 0x7FFFu", source)
        self.assertIn("gpMarDirector->mAreaID != kBiancoArea", source)
        self.assertIn("gpMarDirector->mEpisodeID > 1", source)
        self.assertNotIn("ILing::invalidateForAssist", wrapper)
        self.assertNotIn("rngControlInvalidatesIl", wrapper)

    def test_king_boo_alternates_only_after_a_completed_result(self) -> None:
        source = RUNTIME.read_text(encoding="utf-8")
        wrapper = source.split(
            'extern "C" void susamuneForceKingBooFruit', 1
        )[1]
        self.assertIn("(u32)reel >= 3u", wrapper)
        self.assertIn("bytes + 0x19D", wrapper)
        self.assertIn("bytes + 0x1AB", wrapper)
        self.assertIn("bytes + 0x1A0", wrapper)
        self.assertIn("bytes + 0x1A4", wrapper)
        self.assertIn("bytes + 0x198 + reel", wrapper)
        self.assertIn("bytes + 0x1A8 + reel", wrapper)
        self.assertIn("boss + 0x1A8", wrapper)
        self.assertIn("kKingBooFruit = 2", source)
        self.assertIn("kKingBooNoFruit = 3", source)
        self.assertIn("*state ^= kKingBooNextNoFruit", wrapper)
        self.assertIn("void rngControlOnSavestateLoaded()", source)
        self.assertIn("keepRestored", wrapper)

        savestate = (ROOT / "src" / "savestate.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("rngControlOnSavestateLoaded();", savestate)


if __name__ == "__main__":
    unittest.main()
