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
        self.assertIn('"RNG Control\\0Shined"', menu)
        self.assertIn("const u8 kRngCategory = SETTING_CAT_COUNT + 1;", menu)
        self.assertIn("id == SETTING_ANY_FRUIT_YOSHI", menu)
        self.assertIn("id == SETTING_PATTERN_SELECTOR", menu)
        self.assertIn("practice, savestate, timer, gameplay, rng,", menu)


if __name__ == "__main__":
    unittest.main()
