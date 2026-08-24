#!/usr/bin/env python3
"""Host contracts for rollout/dust feedback and the airgrab QFT event."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
MOVEMENT = ROOT / "src" / "movement_display.cpp"
MAIN = ROOT / "src" / "main.cpp"
MENU = ROOT / "src" / "menu.cpp"
SAVESTATE = ROOT / "src" / "savestate.cpp"
QFT = ROOT / "src" / "qft_timer.cpp"
SETTINGS = ROOT / "include" / "susamune" / "settings_list.h"
DESCS = ROOT / "src" / "settings_descs.inc"


def function(source: str, signature: str) -> str:
    match = re.search(signature + r"\s*\{", source)
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    start = match.end() - 1
    depth = 0
    for end in range(start, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start : end + 1]
    raise AssertionError(f"unterminated function: {signature}")


class MovementDisplayContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = MOVEMENT.read_text(encoding="utf-8")

    def test_retail_update_is_bracketed_once(self) -> None:
        main = MAIN.read_text(encoding="utf-8")
        before = "MovementDisplay::beforeDirect(marioActive);"
        direct = "int state = director->direct();"
        after = "MovementDisplay::afterDirect(marioActive);"
        self.assertEqual(main.count(before), 1)
        self.assertEqual(main.count(after), 1)
        self.assertLess(main.index(before), main.index(direct))
        self.assertLess(main.index(direct), main.index(after))

    def test_transition_edges_use_both_status_views(self) -> None:
        before = function(self.source, r"void beforeDirect\(bool active\)")
        after = function(self.source, r"void afterDirect\(bool active\)")
        self.assertIn("sStateBefore = sMario->mState;", before)
        self.assertIn("const u32 previous = sMario->mPrevState;", after)
        self.assertRegex(
            after,
            r"sStateBefore != TMario::STATE_DIVESLIDE\s*&&\s*"
            r"previous == TMario::STATE_DIVE\s*&&\s*"
            r"state == TMario::STATE_DIVESLIDE",
        )
        self.assertRegex(
            after,
            r"sStateBefore != TMario::STATE_DIVEJUMP\s*&&\s*"
            r"previous == TMario::STATE_DIVESLIDE\s*&&\s*"
            r"state == TMario::STATE_DIVEJUMP",
        )
        self.assertIn("sStateBefore == TMario::STATE_DIVE", after)

    def test_rollout_counts_physical_a_from_one_through_five(self) -> None:
        before = function(self.source, r"void beforeDirect\(bool active\)")
        after = function(self.source, r"void afterDirect\(bool active\)")
        raw_a = function(self.source, r"bool rawAHeld\(\)")
        self.assertIn("JUTGamePad::mPadStatus[0].mButton", raw_a)
        self.assertIn("JUTGamePad::A", raw_a)
        self.assertIn("sRolloutFrames = 1;", after)
        self.assertIn("sRolloutFrames < 5", before)
        self.assertIn("sRolloutFrames == 5", before)
        self.assertIn('"1f\\0" "2f\\0" "3f\\0" "4f\\0" "5f"', self.source)

    def test_dust_is_one_based_and_late_after_six(self) -> None:
        before = function(self.source, r"void beforeDirect\(bool active\)")
        after = function(self.source, r"void afterDirect\(bool active\)")
        self.assertIn("sGroundFrames = 0;", after)
        self.assertIn("sGroundFrames < 7", before)
        self.assertIn("sGroundFrames++;", before)
        self.assertIn("landedThisDirect ? 1 : sGroundFrames", after)
        self.assertIn("frames > 6 ? 7 : frames", after)
        self.assertIn(
            "dustEnabled() && (sGroundTracking || landedThisDirect)", after
        )
        self.assertIn("wallkickDisplayLabel(sPopupResult - 1)", self.source)

    def test_dust_popup_survives_until_rollout_result_is_ready(self) -> None:
        show = function(self.source, r"void showPopup\(u8 kind, u8 result\)")
        tick = function(self.source, r"void tickPopup\(\)")
        self.assertIn("sPopupKind != kind", show)
        self.assertIn("sPendingKind = kind;", show)
        self.assertIn("sPendingResult = result;", show)
        self.assertIn("sPopupKind = sPendingKind;", tick)
        self.assertIn("sPopupFrames = kPopupFrames;", tick)

    def test_stage_and_savestate_resets_are_wired(self) -> None:
        main = MAIN.read_text(encoding="utf-8")
        savestate = SAVESTATE.read_text(encoding="utf-8")
        menu = MENU.read_text(encoding="utf-8")
        self.assertEqual(main.count("MovementDisplay::onStageSetup();"), 1)
        self.assertEqual(
            savestate.count("MovementDisplay::onSavestateLoaded();"), 1
        )
        self.assertEqual(menu.count("MovementDisplay::draw(this);"), 1)


class AirgrabFreezeContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = QFT.read_text(encoding="utf-8")

    def test_airgrab_is_only_the_failed_grab_status(self) -> None:
        match = re.search(
            r"kAirgrabStatuses\[\]\s*=\s*\{([^}]*)\}", self.source
        )
        self.assertIsNotNone(match)
        values = re.findall(r"0x([0-9A-Fa-f]+)u", match.group(1))
        self.assertEqual(values, ["00000384"])
        self.assertEqual(self.source.count("SETTING_TIMER_FREEZE_AIRGRAB"), 1)
        self.assertIn(
            "STATUS(SETTING_TIMER_FREEZE_AIRGRAB, kAirgrabStatuses)",
            self.source,
        )

    def test_generated_status_cave_has_worst_case_capacity(self) -> None:
        arrays: dict[str, list[int]] = {}
        for name, body in re.findall(
            r"constexpr u32 (k\w+Statuses)\[\]\s*=\s*\{([^}]*)\};",
            self.source,
        ):
            arrays[name] = [
                int(value, 16)
                for value in re.findall(r"0x([0-9A-Fa-f]+)u", body)
            ]

        groups = re.findall(r"STATUS\([^,]+,\s*(k\w+Statuses)\)", self.source)
        self.assertIn("kAirgrabStatuses", groups)
        statuses = [status for group in groups for status in arrays[group]]
        compare_words = sum(1 if status < 0x10000 else 3 for status in statuses)
        # Every comparison after the first merges CR1 into CR0; six words call
        # the freezer, replay the displaced instruction, and branch back.
        required = compare_words + len(statuses) - 1 + 6
        capacity = int(
            re.search(r"kStatusCaveMax\s*=\s*(\d+)", self.source).group(1)
        )
        self.assertEqual(required, 49)
        self.assertGreaterEqual(capacity, required)

    def test_setting_is_appended_and_uses_existing_timer_defaults(self) -> None:
        settings = SETTINGS.read_text(encoding="utf-8")
        descs = DESCS.read_text(encoding="utf-8")
        self.assertRegex(
            settings,
            r"SETTING_DUST_DISPLAY[^\n]*\\\s*\n\s*"
            r"X\(SETTING_TIMER_FREEZE_AIRGRAB,",
        )
        self.assertIn(
            'SBOOL("Freeze: airgrab", 1, SETTING_CAT_TIMER)', descs
        )


if __name__ == "__main__":
    unittest.main()
