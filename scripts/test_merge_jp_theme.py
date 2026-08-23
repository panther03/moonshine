import tempfile
import unittest
from pathlib import Path

import merge_jp_theme


class MergeJpThemeTests(unittest.TestCase):
    def test_only_jp_visual_values_change(self) -> None:
        base = """[nintendont]\nversion = pal\n[input_display_jp]\nx = 10\nbackground_r = 0\n[metadata_display_jp]\ntext_g = 255\nformat = QF <QF>\n[qft_display_jp]\ntext_1_rgb = 1,2,3\nleading_zero = 1\n[creation_jp]\nwallkick_1_rgb = 4,5,6\nwallkick_x = 20\n[settings_jp]\nlevel_splits = 1\n"""
        theme = """[nintendont]\nversion = jp\n[input_display_jp]\nx = 99\nbackground_r = 7\n[metadata_display_jp]\ntext_g = 244\nformat = changed\n[qft_display_jp]\ntext_1_rgb = 244,194,64\nleading_zero = 0\n[creation_jp]\nwallkick_1_rgb = 255,244,214\nwallkick_x = 99\n[settings_jp]\nlevel_splits = 0\n"""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            base_path = root / "base.ini"
            theme_path = root / "theme.ini"
            output_path = root / "out.ini"
            base_path.write_text(base, encoding="utf-8")
            theme_path.write_text(theme, encoding="utf-8")

            changes = merge_jp_theme.merge(base_path, theme_path, output_path)
            result = output_path.read_text(encoding="utf-8")

        self.assertEqual(len(changes), 4)
        self.assertIn("version = pal", result)
        self.assertIn("x = 10", result)
        self.assertIn("background_r = 7", result)
        self.assertIn("text_g = 244", result)
        self.assertIn("format = QF <QF>", result)
        self.assertIn("text_1_rgb = 244,194,64", result)
        self.assertIn("leading_zero = 1", result)
        self.assertIn("wallkick_1_rgb = 255,244,214", result)
        self.assertIn("wallkick_x = 20", result)
        self.assertIn("level_splits = 1", result)


if __name__ == "__main__":
    unittest.main()
