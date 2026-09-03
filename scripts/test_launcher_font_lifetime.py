"""Contracts for ownership of the launcher's memory-backed FreeType face."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class LauncherFontLifetimeContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.global_source = (
            ROOT / "launcher" / "loader" / "source" / "global.c"
        ).read_text()
        cls.main_source = (
            ROOT / "launcher" / "loader" / "source" / "main.c"
        ).read_text()

    def test_backing_buffer_is_freed_after_the_face(self) -> None:
        start = self.global_source.index("void FreeLauncherFont(void)")
        end = self.global_source.index("\n}\n", start)
        body = self.global_source[start:end]
        self.assertLess(body.index("GRRLIB_FreeTTF(myFont)"),
                        body.index("free(font_ttf)"))
        self.assertIn("myFont = NULL", body)
        self.assertIn("font_ttf = NULL", body)

    def test_every_shutdown_path_uses_the_owner(self) -> None:
        self.assertEqual(self.global_source.count("FreeLauncherFont();"), 2)
        self.assertEqual(self.main_source.count("FreeLauncherFont();"), 1)
        self.assertEqual(self.global_source.count("GRRLIB_FreeTTF(myFont)"), 1)
        self.assertNotIn("GRRLIB_FreeTTF(myFont)", self.main_source)


if __name__ == "__main__":
    unittest.main()
