import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class RngControlMenuTests(unittest.TestCase):
    def test_rng_settings_have_a_dedicated_menu(self):
        settings = (ROOT / "include/susamune/settings.hxx").read_text()
        descs = (ROOT / "src/settings_descs.inc").read_text()
        menu = (ROOT / "src/menu.cpp").read_text()

        self.assertNotIn("SETTING_CAT_RNG", settings)
        self.assertIn(
            'SBOOL("Any fruit opens Yoshi eggs", 0, SETTING_CAT_CUSTOM)', descs
        )
        self.assertIn(
            'SBOOL("Pattern selector", 0, SETTING_CAT_CUSTOM)', descs
        )
        self.assertIn(
            'SBOOL("King Boo fruit cycle", 0, SETTING_CAT_CUSTOM)', descs
        )
        self.assertIn(
            'SBOOL("Disable Petey tornado", 0, SETTING_CAT_CUSTOM)', descs
        )
        self.assertIn(
            'SCHOICE("Petey flight route", 0, CHOICES_PETEY_ROUTE, '
            'SETTING_CAT_CUSTOM)',
            descs,
        )
        self.assertIn(
            'SCHOICE("Crane speed", 0, CHOICES_RICCO_CRANE_SPEED, '
            "SETTING_CAT_CUSTOM)",
            descs,
        )
        self.assertIn(
            'SCHOICE("Fruit machine", 0, CHOICES_RICCO_FRUIT_MACHINE, '
            "SETTING_CAT_CUSTOM)",
            descs,
        )
        self.assertIn('RNG controls\\0Shined"', menu)
        self.assertIn("const u8 kRngCategory = SETTING_CAT_COUNT + 1;", menu)
        for setting in (
            "SETTING_ANY_FRUIT_YOSHI",
            "SETTING_PATTERN_SELECTOR",
            "SETTING_KING_BOO_ALWAYS_FRUIT",
            "SETTING_PETEY_NO_TORNADO",
            "SETTING_PETEY_ROUTE",
            "SETTING_RICCO_CRANE_SPEED",
            "SETTING_RICCO_FRUIT_MACHINE",
            "SETTING_GELATO_RED_COIN_FISH_PATTERN",
            "SETTING_GELATO_BLUE_BIRD_PATTERN",
        ):
            self.assertIn(setting, menu)
        for page in (
            '"General patterns"',
            '"Boss fights"',
            '"Ricco Harbor"',
            '"Birds and fish (testing)"',
        ):
            self.assertIn(page, menu)
        self.assertNotIn("SETTING_BIANCO_SKEETER_ROUTE", menu)
        self.assertIn('Retail\\0N1-S1-S2-S3', (ROOT / "src/settings.cpp").read_text())
        self.assertIn("const SettingPage kRngPages[]", menu)
        self.assertIn("gameplay, practice, rng, savestate,", menu)
        self.assertIn("if (child->back())", menu)

    def test_every_live_rng_control_can_be_shined(self):
        settings = (ROOT / "include/susamune/settings.hxx").read_text()
        setting_list = (ROOT / "include/susamune/settings_list.h").read_text()
        implementation = (ROOT / "src/settings.cpp").read_text()
        descs = (ROOT / "src/settings_descs.inc").read_text()
        menu = (ROOT / "src/menu.cpp").read_text()

        rows = re.findall(r"X\((SETTING_[A-Z0-9_]+),", setting_list)
        self.assertEqual(rows[-4], "SETTING_RNG_FAVORITES")
        self.assertEqual(rows[-3:], [
            "SETTING_HIDDEN_ITEM_LABELS",
            "SETTING_HURTBOX_TARGET",
            "SETTING_RICCO_RACE_CHECKPOINTS",
        ])
        live_size = (((((len(rows) + 3) & ~1) + 5) & ~3) + 8)
        mem2 = (ROOT / "include/susamune/mem2_map.h").read_text()
        slot = re.search(
            r"SUSAMUNE_CONFIG_SETTINGS_SIZE\s+0x([0-9A-Fa-f]+)u", mem2
        )
        self.assertIsNotNone(slot)
        assert slot is not None
        self.assertLessEqual(live_size, int(slot.group(1), 16))
        self.assertIn('SBOOL("", 0, SETTING_CAT_HIDDEN)', descs)
        self.assertEqual(descs.strip().splitlines()[-1],
                         'SBOOL("Ricco 2 checkpoints", 0, SETTING_CAT_UI)')
        self.assertIn("static bool favoriteable(SettingId id);", settings)
        self.assertIn("id == SETTING_RNG_FAVORITES", implementation)
        self.assertIn("value &= 0x7F;", implementation)
        self.assertIn(
            "id >= SETTING_KING_BOO_ALWAYS_FRUIT &&\n"
            "        id <= SETTING_RICCO_FRUIT_MACHINE",
            implementation,
        )
        self.assertIn(
            "id >= SETTING_GELATO_RED_COIN_FISH_PATTERN &&\n"
            "        id <= SETTING_GELATO_BLUE_BIRD_PATTERN",
            implementation,
        )
        self.assertIn("storage = SETTING_RNG_FAVORITES;", implementation)
        self.assertIn(
            "Settings::favoriteable((SettingId)ids[mSel])",
            menu,
        )
        self.assertIn(
            "if ((rapid & TMarioGamePad::X) && Settings::favoriteable(id))",
            menu,
        )
        self.assertIn("const int count = SETTING_COUNT;", menu)
        self.assertIn("? Settings::favoriteable(id) &&", menu)


if __name__ == "__main__":
    unittest.main()
