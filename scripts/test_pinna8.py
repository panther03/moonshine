#!/usr/bin/env python3
"""Host contracts for the Pinna 8 parent/rollercoaster route."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
ENTRIES = ROOT / "src" / "iling_entries.inc"
ILING = ROOT / "src" / "iling.cpp"


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


if __name__ == "__main__":
    unittest.main()
