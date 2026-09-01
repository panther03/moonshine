#!/usr/bin/env python3
"""Contracts for the classic sup39 controller overlay and Creation editor."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "input_display.cpp"


class ClassicInputDisplayContracts(unittest.TestCase):
    def test_reference_layout_and_palette_remain_the_defaults(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("const int kDesignW = 182;", source)
        self.assertIn("const int kDesignH = 120;", source)
        self.assertIn(
            "return CreationStyle{16, 314, 100, 255, 0, 0, 0, 0x7f, 100, 0};",
            source,
        )
        self.assertIn("mCfg.startVisible   = 1;", source)
        self.assertIn("mVisible           = true;", source)
        for geometry in (
            "p.button(138, 66, 18",
            "p.button(113, 89, 9",
            "p.button(164, 50, 8",
            "p.button(119, 41, 8",
            "p.button(144, 34, 6",
            "p.button(91, 64, 5",
            "p.fillCircle(32 + mx, 52 - my, 12",
            "p.fillCircle(64 + cx, 92 - cy, 12",
        ):
            self.assertIn(geometry, source)
        self.assertIn("{238, 238, 238}, {255, 211,   0}, { 46, 229, 184}", source)
        self.assertIn("{255,  26,  26}", source)
        self.assertIn("{148, 148, 255}", source)

    def test_creation_styling_wraps_the_classic_renderer(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("mEditor.begin(&mStyle, mColors, mBackupRgb", source)
        self.assertIn("void InputDisplay::adoptStyle", source)
        self.assertIn("void InputDisplay::stageStyleInto", source)
        self.assertIn("SUSAMUNE_INPUT_STYLE_ALL", source)

    def test_sticks_use_the_reference_processed_travel(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("JUTGamePad::mPadMStick[0]", source)
        self.assertIn("JUTGamePad::mPadSStick[0]", source)
        self.assertIn("main.mStickX * 14.0f", source)
        self.assertIn("sub.mStickY * 14.0f", source)
        self.assertNotIn("raw.mStickX, -100, 100", source)

    def test_small_start_press_has_a_visible_center(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("down && scale(radius) <= 2", source)
        self.assertIn("menu->fillBox(x(lx) - 1, y(ly) - 1, 3, 3, c);", source)


if __name__ == "__main__":
    unittest.main()
