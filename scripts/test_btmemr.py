#!/usr/bin/env python3
"""Regression tests for the ARM Bluetooth heap's size alignment."""

from pathlib import Path
import unittest


class BluetoothHeapTests(unittest.TestCase):
    def test_malloc_rounds_every_unaligned_size_up(self) -> None:
        alignment = 4
        header_size = 12
        for requested in range(1, 257):
            rounded = requested
            if rounded % alignment:
                rounded += alignment - (
                    (rounded + header_size) % alignment
                )
            with self.subTest(requested=requested):
                self.assertGreaterEqual(rounded, requested)
                self.assertEqual(rounded % alignment, 0)
                self.assertEqual(
                    rounded,
                    (requested + alignment - 1) & ~(alignment - 1),
                )

    def test_kernel_malloc_uses_the_alignment_modulus(self) -> None:
        root = Path(__file__).resolve().parents[1]
        source = (root / "launcher/kernel/lwbt/btmemr.c").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "((size+SIZEOF_STRUCT_MEM)%MEM_ALIGNMENT)", source
        )
        self.assertNotIn(
            "((size+SIZEOF_STRUCT_MEM)%SIZEOF_STRUCT_MEM)", source
        )


if __name__ == "__main__":
    unittest.main()
