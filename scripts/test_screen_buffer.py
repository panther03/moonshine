"""Contracts for the launcher's allocation-free retained status frames."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ScreenBufferContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.global_source = (
            ROOT / "launcher" / "loader" / "source" / "global.c"
        ).read_text()
        cls.main_source = (
            ROOT / "launcher" / "loader" / "source" / "main.c"
        ).read_text()
        cls.grrlib = (
            ROOT / "launcher" / "loader" / "source" / "grrlib.c"
        ).read_text()
        cls.update_source = (
            ROOT / "launcher" / "loader" / "source" / "update.c"
        ).read_text()

    def test_status_snapshot_allocation_is_gone(self) -> None:
        combined = self.global_source + self.main_source
        self.assertNotIn("screen_buffer", combined)
        self.assertNotIn("Screen2TextureRGB565", combined)

    def test_update_screen_retains_the_efb(self) -> None:
        start = self.global_source.index("inline void UpdateScreen(void)")
        body = self.global_source[start:self.global_source.index("\n}", start)]
        self.assertIn("GRRLIB_RenderPreserve();", body)
        self.assertNotIn("GRRLIB_Screen2Texture", body)

    def test_render_clear_mode_is_explicit(self) -> None:
        self.assertIn("GX_CopyDisp      (xfb[fb], clear ? GX_TRUE : GX_FALSE)",
                      self.grrlib)
        self.assertIn("GRRLIB_RenderMode(true);", self.grrlib)
        self.assertIn("GRRLIB_RenderMode(false);", self.grrlib)

    def test_async_status_clears_before_drawing_and_then_retains(self) -> None:
        clear = self.main_source.index("ClearScreen();", self.main_source.index(
            "if( STATUS_LOADING == 0xdeadbeef )"))
        draw = self.main_source.index("PrintInfo();", clear)
        retain = self.main_source.index("GRRLIB_RenderPreserve();", draw)
        self.assertLess(clear, draw)
        self.assertLess(draw, retain)

    def test_interactive_prompt_preserves_its_frame(self) -> None:
        self.assertNotIn("DrawBuffer", self.update_source)
        self.assertIn("GRRLIB_RenderPreserve();", self.update_source)


if __name__ == "__main__":
    unittest.main()
