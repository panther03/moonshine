#!/usr/bin/env python3
"""Host contracts for boss-RNG IL invalidation and its visible warning."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RngIlWarningTests(unittest.TestCase):
    def test_only_boss_controls_invalidate(self) -> None:
        source = (ROOT / "src/rng_control.cpp").read_text(encoding="utf-8")
        helper = re.search(
            r"bool rngControlInvalidatesIl\(\)\s*\{(.*?)\n\}",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(helper)
        assert helper is not None
        body = helper.group(1)
        self.assertIn("SETTING_KING_BOO_ALWAYS_FRUIT", body)
        self.assertIn("SETTING_PETEY_NO_TORNADO", body)
        self.assertIn("SETTING_PETEY_ROUTE", body)
        self.assertNotIn("SETTING_RICCO_CRANE_SPEED", body)
        self.assertNotIn("SETTING_RICCO_FRUIT_MACHINE", body)
        self.assertIn(
            "if (rngControlInvalidatesIl()) ILing::invalidateForAssist();",
            source,
        )

    def test_native_hud_red_is_snapshotted_and_reversible(self) -> None:
        source = (ROOT / "src/creation_extras.cpp").read_text(encoding="utf-8")
        self.assertIn("sHudBeforeWarning", source)
        self.assertIn("sWaterBeforeWarning", source)
        self.assertIn("makeRed(picture->mColorMask);", source)
        self.assertIn("makeRed(picture->mColorOverlay);", source)
        self.assertIn("mWaterLeftPanelColor", source)
        self.assertIn("mWaterRightPanelColor", source)
        self.assertIn("loadRgb(picture->mColorMask", source)
        self.assertIn("loadRgb(picture->mColorOverlay", source)
        self.assertIn("sHudWarningSnapshotValid", source)
        stage_setup = source.split("void CreationExtras::onStageSetup()", 1)[1]
        self.assertLess(
            stage_setup.index("snapshotWarningColors(mHudPictures);"),
            stage_setup.index("applyHud();"),
        )
        self.assertIn("void CreationExtras::onSavestateLoaded()", source)

        savestate = (ROOT / "src/savestate.cpp").read_text(encoding="utf-8")
        self.assertIn("gCreationExtras.onSavestateLoaded();", savestate)

    def test_creation_is_locked_and_overlays_are_red(self) -> None:
        source = (ROOT / "src/menu.cpp").read_text(encoding="utf-8")
        self.assertIn(
            "bool available() const override { return !rngControlInvalidatesIl(); }",
            source,
        )
        self.assertIn('available ? nullptr : "Disabled"', source)
        self.assertIn('toast("Disable boss RNG controls first")', source)
        self.assertIn("color.r = 255;", source)
        self.assertIn("color.g = 0;", source)
        self.assertIn("color.b = 0;", source)
        self.assertIn("color = warningText(color, mShown);", source)
        self.assertIn("warningForeground(color, mShown)", source)
        self.assertIn('const char *text = "INVALID IL";', source)
        self.assertIn(
            "if (invalidBefore != rngControlInvalidatesIl())", source
        )

    def test_invalid_label_is_the_last_overlay(self) -> None:
        source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        after_draw = source.split('extern "C" void afterDraw()', 1)[1]
        self.assertLess(
            after_draw.index("WarpWheel::draw();"),
            after_draw.index("gMenu->drawInvalidIlWarning();"),
        )

    def test_successful_spawn_and_regrab_invalidate(self) -> None:
        source = (ROOT / "src/actions.cpp").read_text(encoding="utf-8")
        regrab = source.split("void regrabLastHeldObject()", 1)[1].split(
            "// =====================================================================", 1
        )[0]
        self.assertEqual(regrab.count("ILing::invalidateForAssist();"), 1)
        self.assertLess(
            regrab.index("!gLastHeldObject || !gBinds.isHeld"),
            regrab.index("ILing::invalidateForAssist();"),
        )
        self.assertLess(
            regrab.index("ILing::invalidateForAssist();"),
            regrab.index("mario->mGrabTarget = gLastHeldObject;"),
        )

        spawn = source.split("void spawnYoshi(u8 color)", 1)[1].split(
            "void spawnYoshiFromBinds()", 1
        )[0]
        self.assertEqual(spawn.count("ILing::invalidateForAssist();"), 1)
        self.assertLess(
            spawn.index("if (!yoshi)"),
            spawn.index("ILing::invalidateForAssist();"),
        )
        self.assertLess(
            spawn.index("gEggKillFrames = 2"),
            spawn.index("ILing::invalidateForAssist();"),
        )


if __name__ == "__main__":
    unittest.main()
