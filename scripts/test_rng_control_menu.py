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
            'SBOOL("Fruit every other roll", 0, SETTING_CAT_CUSTOM)', descs
        )
        self.assertIn(
            'SBOOL("Disable tornado", 0, SETTING_CAT_CUSTOM)', descs
        )
        self.assertIn(
            'SCHOICE("Route", 0, CHOICES_PETEY_ROUTE, SETTING_CAT_CUSTOM)',
            descs,
        )
        self.assertIn(
            'SCHOICE("Random speed", 0, CHOICES_RICCO_CRANE_SPEED, '
            "SETTING_CAT_CUSTOM)",
            descs,
        )
        self.assertIn(
            'SCHOICE("Fruit", 0, CHOICES_RICCO_FRUIT_MACHINE, '
            "SETTING_CAT_CUSTOM)",
            descs,
        )
        self.assertIn('"RNG Control\\0Shined"', menu)
        self.assertIn("const u8 kRngCategory = SETTING_CAT_COUNT + 1;", menu)
        self.assertIn("id == SETTING_ANY_FRUIT_YOSHI", menu)
        self.assertIn("id == SETTING_PATTERN_SELECTOR", menu)
        self.assertIn("id == SETTING_KING_BOO_ALWAYS_FRUIT", menu)
        self.assertIn("id == SETTING_PETEY_NO_TORNADO", menu)
        self.assertIn("id == SETTING_PETEY_ROUTE", menu)
        self.assertIn("id == SETTING_RICCO_CRANE_SPEED", menu)
        self.assertIn("id == SETTING_RICCO_FRUIT_MACHINE", menu)
        self.assertIn(
            "FEEDBACK\\0KING BOO\\0PETEY\\0RICCO CRANE\\0", menu
        )
        self.assertIn('"RICCO FRUIT MACHINE"', menu)
        self.assertIn("practice, savestate, timer, gameplay, rng,", menu)
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
