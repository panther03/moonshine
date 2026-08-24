import pathlib
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
            '"Birds & fish (testing)"',
        ):
            self.assertIn(page, menu)
        self.assertNotIn("SETTING_BIANCO_SKEETER_ROUTE", menu)
        self.assertIn('Retail\\0N1-S1-S2-S3', (ROOT / "src/settings.cpp").read_text())
        self.assertIn("const SettingPage kRngPages[]", menu)
        self.assertIn("gameplay, practice, rng, savestate,", menu)
        self.assertIn("if (child->back())", menu)
        self.assertIn(
            "return mSel < settings && ids[mSel] < SETTING_FAVORITES_0;",
            menu,
        )
        self.assertIn(
            "if ((rapid & TMarioGamePad::X) && id < SETTING_FAVORITES_0)",
            menu,
        )


if __name__ == "__main__":
    unittest.main()
