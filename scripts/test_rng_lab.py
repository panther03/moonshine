#!/usr/bin/env python3
"""Host tests for Sunshine RNG stepping and seed selection."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import random
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
LAB_PATH = ROOT / "scripts" / "rng_lab.py"

spec = importlib.util.spec_from_file_location("rng_lab_test", LAB_PATH)
assert spec and spec.loader
lab = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = lab
spec.loader.exec_module(lab)


class LcgTests(unittest.TestCase):
    def test_retail_msl_vectors(self) -> None:
        self.assertEqual(lab.MSL.step(0), 0x00003039)
        self.assertEqual(lab.MSL.step(1), 0x41C67EA6)
        self.assertEqual(lab.msl_step(1), (0x41C67EA6, 0x41C6))
        self.assertEqual(lab.MSL.inverse_multiplier, 0xEEB9EB65)

    def test_jmath_fast_vectors(self) -> None:
        self.assertEqual(lab.JMATH_FAST.step(0), 0x3C6EF35F)
        self.assertEqual(lab.JMATH_FAST.step(1), 0x3C88596C)

    def test_forward_and_previous_are_exact_inverses(self) -> None:
        rng = random.Random(0x484F555345)
        for generator in (lab.MSL, lab.JMATH_FAST):
            for state in (0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF):
                self.assertEqual(generator.previous(generator.step(state)), state)
                self.assertEqual(generator.step(generator.previous(state)), state)
            for _ in range(10_000):
                state = rng.randrange(1 << 32)
                self.assertEqual(generator.previous(generator.step(state)), state)

    def test_affine_jump_matches_iteration_in_both_directions(self) -> None:
        rng = random.Random(0x5A455441)
        for generator in (lab.MSL, lab.JMATH_FAST):
            for _ in range(500):
                state = rng.randrange(1 << 32)
                calls = rng.randrange(-500, 501)
                expected = state
                operation = generator.step if calls >= 0 else generator.previous
                for _ in range(abs(calls)):
                    expected = operation(expected)
                self.assertEqual(generator.advance(state, calls), expected)

    def test_large_jumps_compose_and_rewind(self) -> None:
        state = 0xC001D00D
        for generator in (lab.MSL, lab.JMATH_FAST):
            first = generator.advance(state, 1_234_567_890)
            second = generator.advance(first, -987_654_321)
            self.assertEqual(
                second,
                generator.advance(state, 1_234_567_890 - 987_654_321),
            )
            self.assertEqual(generator.advance(first, -1_234_567_890), state)

    def test_set_next_msl_output_preserves_all_hidden_choices(self) -> None:
        for output in (0, 1, 0x1234, 0x7FFE, 0x7FFF):
            for low16 in (0, 1, 0xBEEF, 0xFFFF):
                for top_bit in (0, 1):
                    before, requested_after = lab.seed_for_next_msl_output(
                        output, low16, top_bit
                    )
                    after, actual = lab.msl_step(before)
                    self.assertEqual(after, requested_after)
                    self.assertEqual(actual, output)

    def test_set_next_msl_output_rejects_invalid_fields(self) -> None:
        for output in (-1, 0x8000):
            with self.assertRaises(ValueError):
                lab.seed_for_next_msl_output(output)
        with self.assertRaises(ValueError):
            lab.seed_for_next_msl_output(0, 0x10000)
        with self.assertRaises(ValueError):
            lab.seed_for_next_msl_output(0, top_bit=2)


if __name__ == "__main__":
    unittest.main()
