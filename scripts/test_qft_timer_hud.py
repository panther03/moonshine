#!/usr/bin/env python3
"""Host contracts for the Sunshine timer's retail-HUD coexistence."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "include" / "SMS" / "GC2D" / "GCConsole2.hxx"
SOURCE = ROOT / "src" / "qft_timer.cpp"


class QftTimerHudContracts(unittest.TestCase):
    def test_hud_positioning_does_not_depend_on_private_console_offsets(self) -> None:
        header = HEADER.read_text(encoding="utf-8")
        self.assertNotIn("mJetCounterPanel", header)
        self.assertNotIn("mRedCounterPanel", header)
        self.assertNotIn("mTimerPanel;", header)

        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("hudPane(console, '\\0b_0')", source)
        self.assertIn("hudPane(console, '\\0r_0')", source)
        self.assertIn("hudPane(console, '\\0t_0')", source)

    def test_only_the_mod_owned_timer_is_raised(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        detector = source.split("bool missionCounterOnScreen", 1)[1].split(
            "J2DPane *bigTimerPane", 1
        )[0]
        self.assertIn("paneOnScreen(hudPane(console, '\\0b_0'))", detector)
        self.assertIn("paneOnScreen(hudPane(console, '\\0r_0'))", detector)

        position = source.split("void raiseBigTimer", 1)[1].split(
            "void hideBigTimer", 1
        )[0]
        self.assertIn("pane->add(0, -60);", position)
        self.assertNotIn("TExPane *", position)

    def test_retail_timer_and_hide_paths_restore_the_exact_offset(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        update = source.split("void updateBigTimer(bool afterDirect)", 1)[1].split(
            "}  // namespace", 1
        )[0]
        retail = update.split("if (console->mIsTimerMoving)", 1)[1].split(
            "u8 mode", 1
        )[0]
        self.assertNotIn("raiseBigTimer", retail)
        self.assertIn("afterDirect && missionCounterOnScreen(console)", update)
        self.assertIn("raiseBigTimer(console);", update)

        self.assertLess(
            source.index("restoreBigTimerPosition();"),
            source.index("sBigUpdatePass = 0;"),
        )
        self.assertIn("pane->add(0, 60);", source)
        self.assertRegex(source, r"sSavedBigRaised\s*=\s*sBigRaised;")
        self.assertRegex(source, r"sBigRaised\s*=\s*sSavedBigRaised;")


if __name__ == "__main__":
    unittest.main()
