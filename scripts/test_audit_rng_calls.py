#!/usr/bin/env python3
"""Host tests for the retail PPC RNG-call auditor."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
AUDIT_PATH = ROOT / "scripts" / "audit_rng_calls.py"

spec = importlib.util.spec_from_file_location("audit_rng_calls_test", AUDIT_PATH)
assert spec and spec.loader
audit = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = audit
spec.loader.exec_module(audit)


def branch(pc: int, target: int, *, link: bool = True, absolute: bool = False) -> int:
    displacement = target if absolute else target - pc
    if displacement & 3:
        raise ValueError("branch target is not word-aligned")
    if not -(1 << 25) <= displacement < (1 << 25):
        raise ValueError("branch target is out of range")
    return (
        0x48000000
        | (displacement & 0x03FFFFFC)
        | (2 if absolute else 0)
        | (1 if link else 0)
    )


def dol_with_text(address: int, words: list[int]) -> bytes:
    header = bytearray(0x100)
    struct.pack_into(">I", header, 0x00, len(header))
    struct.pack_into(">I", header, 0x48, address)
    struct.pack_into(">I", header, 0x90, len(words) * 4)
    return bytes(header) + b"".join(struct.pack(">I", word) for word in words)


class DirectCallTests(unittest.TestCase):
    def test_relative_calls_and_tail_branch_filter(self) -> None:
        base = 0x80001000
        target = 0x80002000
        data = dol_with_text(
            base,
            [
                branch(base, target),
                branch(base + 4, target, link=False),
                0x60000000,
            ],
        )
        self.assertEqual(list(audit.direct_calls(data, target, False)), [base])
        self.assertEqual(
            list(audit.direct_calls(data, target, True)), [base, base + 4]
        )

    def test_negative_relative_displacement(self) -> None:
        base = 0x80001000
        target = 0x80000000
        data = dol_with_text(base, [branch(base, target)])
        self.assertEqual(list(audit.direct_calls(data, target, False)), [base])

    def test_absolute_link_branch(self) -> None:
        base = 0x80001000
        target = 0x00100000
        data = dol_with_text(base, [branch(base, target, absolute=True)])
        self.assertEqual(list(audit.direct_calls(data, target, False)), [base])

    def test_other_targets_and_opcodes_are_ignored(self) -> None:
        base = 0x80001000
        data = dol_with_text(
            base,
            [branch(base, 0x80002000), 0x4E800021, 0x60000000],
        )
        self.assertEqual(list(audit.direct_calls(data, 0x80003000, True)), [])


class SymbolTests(unittest.TestCase):
    def test_map_parser_and_smallest_containing_symbol(self) -> None:
        lines = (
            "  00000000 000020 80001000 1 outer Object.a\n"
            "  00000000 000008 80001008 1 inner Object.a\n"
            "  00000000 000000 80001010 1 zero Object.a\n"
            "  00000000 000004 70000000 1 outside Object.a\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "retail.map"
            path.write_text(lines, encoding="utf-8")
            symbols = audit.read_symbols(path)

        self.assertEqual([symbol.name for symbol in symbols], ["outer", "inner"])
        self.assertEqual(audit.containing_symbol(symbols, 0x80001004).name, "outer")
        self.assertEqual(audit.containing_symbol(symbols, 0x8000100C).name, "inner")
        self.assertIsNone(audit.containing_symbol(symbols, 0x80002000))


if __name__ == "__main__":
    unittest.main()
