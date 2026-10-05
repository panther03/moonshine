#!/usr/bin/env python3
"""Host contracts for boss-RNG IL invalidation and its visible warning."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RngIlWarningTests(unittest.TestCase):
    def test_boss_controls_do_not_invalidate_or_enable_red_warning(self) -> None:
        source = (ROOT / "src/rng_control.cpp").read_text(encoding="utf-8")
        helper = re.search(r"bool rngControlInvalidatesIl\(\)\s*\{(.*?)\}", (ROOT / "include/susamune/rng_control.hxx").read_text(), re.S)
        self.assertIsNotNone(helper)
        self.assertRegex(helper.group(1), r"return false;\s*$")
        self.assertNotIn("ILing::invalidateForAssist", source)
        self.assertIn("applyPeteyTornadoControl();", source)

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

    def test_rng_does_not_lock_editors_or_recolour_mod_overlays(self) -> None:
        source = (ROOT / "src/menu.cpp").read_text(encoding="utf-8")
        self.assertNotIn("rngControlInvalidatesIl", source)
        self.assertNotIn("Disable boss RNG controls first", source)
        self.assertNotIn("drawInvalidIlWarning", source)
        self.assertNotIn("drawInvalidIlWarning", (ROOT / "src/main.cpp").read_text())
        settings = (ROOT / "src/settings.cpp").read_text()
        blockers = settings.split("const IlPbSetting kIlPbSettings[] =", 1)[1].split("};", 1)[0]
        for name in ("KING_BOO_ALWAYS_FRUIT", "PETEY_NO_TORNADO", "PETEY_ROUTE"):
            self.assertNotIn("SETTING_" + name, blockers)

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
