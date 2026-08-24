#!/usr/bin/env python3
"""Host contracts for the Pinna 8 parent/rollercoaster route."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
ENTRIES = ROOT / "src" / "iling_entries.inc"
ILING = ROOT / "src" / "iling.cpp"
WARP_WHEEL = ROOT / "src" / "warp_wheel.cpp"


class PinnaEightContracts(unittest.TestCase):
    def test_collected_shine_is_hidden_while_the_route_loads(self) -> None:
        entries = ENTRIES.read_text(encoding="utf-8")
        source = ILING.read_text(encoding="utf-8")
        self.assertIn(
            'SHINE_CLEAR("Pinna 8", 0x0D, 5, 7, 37, GROUP_PINNA)',
            entries,
        )
        self.assertNotIn(
            'SHINE("Pinna 8", 0x0D, 5, 7, 37, GROUP_PINNA)',
            entries,
        )
        self.assertIn("if (flags == ENTRY_CLEAR_RESULT) return 0;", source)

    def test_rollercoaster_return_keeps_the_parent_attempt_armed(self) -> None:
        source = ILING.read_text(encoding="utf-8")
        internal = source.split("bool isInternalScene", 1)[1].split(
            "int entryForStartScene", 1
        )[0]
        self.assertIn("start.area == TGameSequence::AREA_PINNAPARCO", internal)
        self.assertIn("start.episode == 5", internal)
        self.assertRegex(
            internal,
            r"scene\.mAreaID\s*==\s*0x3A\s*&&\s*"
            r"scene\.mEpisodeID\s*==\s*0",
        )

        helper = source.split("bool isPinnaEightReturn", 1)[1].split(
            "bool acceptsSelectedOriginScene", 1
        )[0]
        self.assertIn(
            "sameDest(sAttemptStart, kEntries[kEntryPinna8].start)", helper
        )
        self.assertRegex(
            helper,
            r"previous\.mAreaID\s*==\s*0x3A\s*&&\s*"
            r"previous\.mEpisodeID\s*==\s*0",
        )

        setup = source.split("void beforeStageSetup()", 1)[1].split(
            "void onStageSetup()", 1
        )[0]
        start_scene = re.search(
            r"if \(sceneMatches\(scene, sAttemptStart\)\) \{(.*?)\n\s*\}",
            setup,
            re.DOTALL,
        )
        self.assertIsNotNone(start_scene)
        assert start_scene is not None
        self.assertIn(
            "sAttemptReady = isPinnaEightReturn(scene);",
            start_scene.group(1),
        )

    def test_instant_reset_after_the_coaster_uses_the_park_entrance(self) -> None:
        source = WARP_WHEEL.read_text(encoding="utf-8")
        restart = source.split("void restart(bool keepSpawn)", 1)[1].split(
            "void restartFull()", 1
        )[0]
        self.assertIn("cur.mAreaID == TGameSequence::AREA_PINNAPARCO", restart)
        self.assertIn("cur.mEpisodeID == 5", restart)
        self.assertNotIn("mPrevScene", restart)
        self.assertIn("keepSpawn && !pinnaEight", restart)
        self.assertIn("armWarp(dest, keepSpawn && !pinnaEight, pinnaEight)", restart)
        self.assertIn("if (pinnaEight) sSource = dest;", restart)

    def test_selected_pinna_eight_starts_with_a_default_self_source(self) -> None:
        source = ILING.read_text(encoding="utf-8")
        start = source.split("bool start(int entry, u32 approvedDiscardToken)", 1)[1]
        start = start.split("bool start(int entry)", 1)[0]
        self.assertIn("if (entry == kEntryPinna8)", start)
        self.assertIn(
            "LevelWarp::warpFromGuarded(item.start, item.start,", start
        )

    def test_every_mod_owned_pinna_eight_warp_clears_the_coaster_phase(self) -> None:
        source = WARP_WHEEL.read_text(encoding="utf-8")
        apply_dest = source.split("void applyDest", 1)[1].split(
            "void overrideSourceForDefaultSpawn", 1
        )[0]
        self.assertIn("kPinnaEightCompleteFlag = 0x30005", source)
        self.assertIn("dest.area == TGameSequence::AREA_PINNAPARCO", apply_dest)
        self.assertIn("dest.episode == 5", apply_dest)
        self.assertIn("== 7", apply_dest)
        self.assertIn(
            "flags->setBool(false, kPinnaEightCompleteFlag);", apply_dest
        )


if __name__ == "__main__":
    unittest.main()
