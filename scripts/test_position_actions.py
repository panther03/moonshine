#!/usr/bin/env python3
"""Host contracts for the Dan Salvato save/load position actions."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PositionActionTests(unittest.TestCase):
    def test_bind_ids_are_appended_and_unbound(self) -> None:
        bind_list = (
            ROOT / "include/susamune/binds_list.h"
        ).read_text(encoding="utf-8")
        rows = re.findall(r'X\((BIND_[A-Z0-9_]+),\s*"([^"]+)"\)', bind_list)
        self.assertEqual(
            rows[-2:],
            [
                ("BIND_POSITION_SAVE", "position_save"),
                ("BIND_POSITION_LOAD", "position_load"),
            ],
        )

        descs = (ROOT / "src/binds_descs.inc").read_text(encoding="utf-8")
        self.assertTrue(descs.rstrip().endswith(
            'BIND_DESC("Position: save", 0)\n'
            'BIND_DESC("Position: load", 0)'
        ))

    def test_snapshot_matches_original_twenty_bytes(self) -> None:
        source = (ROOT / "src/actions.cpp").read_text(encoding="utf-8")
        mario = (ROOT / "include/SMS/Player/Mario.hxx").read_text(encoding="utf-8")
        for axis in "XYZ":
            self.assertIn(f"extern s16 *gpMarioAngle{axis};", mario)
            self.assertNotIn(f"extern f32 *gpMarioAngle{axis};", mario)

        snapshot = source.split("struct PositionSnapshot", 1)[1].split("};", 1)[0]
        self.assertIn("TVec3f position;", snapshot)
        self.assertIn("s16    marioAngleY;", snapshot)
        self.assertIn("s16    cameraHorizontalAngle;", snapshot)
        self.assertIn("f32    cameraInterpolateDistance;", snapshot)

        save = source.split("void savePosition()", 1)[1].split(
            "void loadPosition()", 1
        )[0]
        load = source.split("void loadPosition()", 1)[1].split(
            "void positionFromBinds()", 1
        )[0]
        for field in (
            "position",
            "marioAngleY",
            "cameraHorizontalAngle",
            "cameraInterpolateDistance",
        ):
            self.assertIn(f"gPositionSnapshot.{field}", save)
            self.assertIn(f"gPositionSnapshot.{field}", load)
        self.assertIn("gpCamera->mHorizontalAngle", save)
        self.assertIn("gpCamera->mInterpolateDistance", save)
        self.assertIn("gpCamera->mHorizontalAngle", load)
        self.assertIn("gpCamera->mInterpolateDistance", load)

    def test_load_is_guarded_and_invalidates_credit(self) -> None:
        source = (ROOT / "src/actions.cpp").read_text(encoding="utf-8")
        ready = source.split("bool positionActorsReady()", 1)[1].split(
            "void savePosition()", 1
        )[0]
        self.assertIn("stageActive()", ready)
        self.assertIn("gpMarioPos", ready)
        self.assertIn("gpMarioAngleY", ready)
        self.assertIn("gpCamera", ready)

        load = source.split("void loadPosition()", 1)[1].split(
            "void positionFromBinds()", 1
        )[0]
        self.assertLess(
            load.index("!gPositionSnapshotValid || !positionActorsReady()"),
            load.index("ILing::invalidateForAssist();"),
        )
        self.assertEqual(load.count("ILing::invalidateForAssist();"), 1)

        save = source.split("void savePosition()", 1)[1].split(
            "void loadPosition()", 1
        )[0]
        self.assertNotIn("ILing::invalidateForAssist();", save)

    def test_actions_match_original_held_semantics_and_bind_suppression(self) -> None:
        source = (ROOT / "src/actions.cpp").read_text(encoding="utf-8")
        binds = source.split("void positionFromBinds()", 1)[1].split(
            "// =====================================================================", 1
        )[0]
        self.assertIn("gBinds.isHeld(BIND_POSITION_SAVE)", binds)
        self.assertIn("gBinds.isHeld(BIND_POSITION_LOAD)", binds)
        self.assertNotIn("wasPressed", binds)

        apply = source.split("void actionsApply(bool allowBinds)", 1)[1]
        gated = apply.split("if (allowBinds)", 1)[1].split("}", 1)[0]
        self.assertIn("positionFromBinds();", gated)


if __name__ == "__main__":
    unittest.main()
