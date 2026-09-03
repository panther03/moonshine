"""Contracts for the compact GameCube IPL patch tables."""

from __future__ import annotations

import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "launcher/loader/source/ipl.c"


EXPECTED = {
    "kEnglishUs12a": [
        (0x8130B906, 38), (0x8130B926, 10), (0x8130B92E, 39),
        (0x8130B932, 15), (0x8130B93A, 7), (0x8130B93E, 1),
        (0x8130B946, 4), (0x8130B94A, 45), (0x8130B952, 46),
        (0x8130B956, 42), (0x8130B95E, 40), (0x8130B962, 43),
        (0x8130B976, 31), (0x8130B97A, 29), (0x8130B982, 30),
        (0x8130B98E, 80),
    ],
    "kEnglishUs12b": [
        (0x8130B91E, 38), (0x8130B93E, 10), (0x8130B946, 39),
        (0x8130B94A, 15), (0x8130B952, 7), (0x8130B956, 1),
        (0x8130B95E, 4), (0x8130B962, 45), (0x8130B96A, 46),
        (0x8130B96E, 42), (0x8130B976, 40), (0x8130B97A, 43),
        (0x8130B98E, 31), (0x8130B992, 29), (0x8130B99A, 30),
        (0x8130B9A6, 80),
    ],
    "kEnglishUs11": [
        (0x8130B592, 38), (0x8130B5B2, 10), (0x8130B5BA, 39),
        (0x8130B5BE, 15), (0x8130B5C6, 7), (0x8130B5CA, 1),
        (0x8130B5D2, 4), (0x8130B5D6, 45), (0x8130B5DE, 46),
        (0x8130B5E2, 42), (0x8130B5EA, 40), (0x8130B5EE, 43),
        (0x8130B602, 31), (0x8130B606, 29), (0x8130B60E, 30),
        (0x8130B61A, 80),
    ],
    "kEnglishUs10": [
        (0x8130B40A, 38), (0x8130B412, 38), (0x8130B416, 39),
        (0x8130B422, 15), (0x8130B42E, 7), (0x8130B43A, 1),
        (0x8130B446, 4), (0x8130B44E, 45), (0x8130B452, 46),
        (0x8130B45A, 42), (0x8130B45E, 40), (0x8130B466, 43),
        (0x8130B476, 31), (0x8130B47E, 29), (0x8130B482, 30),
        (0x8130B48E, 80),
    ],
}

EXPECTED_PAL_ANIMATION = {
    0x8130F300: [
        (0x8130F306, 10, 2), (0x8130F30A, 255, 2),
        (0x8130F316, 7, 2), (0x8130F31A, 6, 2),
        (0x8130F31E, 5, 2), (0x8130F322, 16, 2),
        (0x8130F326, 18, 2), (0x8130F32A, 20, 2),
        (0x8130F332, 40, 2), (0x8130F336, 60, 2),
        (0x8130F370, 0xB3E30016, 4), (0x8130F378, 0x9963000B, 4),
        (0x8130F37C, 0x9BC3001C, 4), (0x8130F380, 0x9BA30037, 4),
        (0x8130F384, 0x99430035, 4),
    ],
    0x8130F1C0: [
        (0x8130F1C6, 10, 2), (0x8130F1CA, 255, 2),
        (0x8130F1D6, 7, 2), (0x8130F1DA, 6, 2),
        (0x8130F1DE, 5, 2), (0x8130F1E2, 16, 2),
        (0x8130F1E6, 18, 2), (0x8130F1EA, 20, 2),
        (0x8130F1F2, 40, 2), (0x8130F1F6, 60, 2),
        (0x8130F230, 0xB3E30016, 4), (0x8130F238, 0x9963000B, 4),
        (0x8130F23C, 0x9BC3001C, 4), (0x8130F240, 0x9BA30037, 4),
        (0x8130F244, 0x99430035, 4),
    ],
}

FILTER_ADDRESSES = (
    0x8137ECEA, 0x8137F16A, 0x8137DA22, 0x8135DE12,
    0x813824A2, 0x81381002, 0x8137D942,
)

JINGLE_ADDRESSES = {
    0x81303184: 1,
    0x8130319C: 1,
    0x81302DE8: 3,
    0x81302F00: 1,
    0x81302F50: 1,
}



def _table(source: str, name: str) -> list[tuple[int, int]]:
    match = re.search(
        rf"static const u16 {name}\[\]\s*=\s*\{{(.*?)\}};", source, re.S
    )
    if match is None:
        raise AssertionError(f"missing {name}")
    offsets = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)",
                                                       match.group(1))]
    values_match = re.search(
        r"static const u8 kEnglishValues\[\]\s*=\s*\{(.*?)\};", source, re.S
    )
    if values_match is None:
        raise AssertionError("missing kEnglishValues")
    values = [int(value) for value in re.findall(r"\b(\d+)\b",
                                                  values_match.group(1))]
    if name == "kEnglishUs10":
        values[1] = 38
    return [(0x81300000 + offset, value)
            for offset, value in zip(offsets, values)]


def _array(source: str, c_type: str, name: str) -> list[int]:
    match = re.search(
        rf"static const {c_type} {name}\[\]\s*=\s*\{{(.*?)\}};", source, re.S
    )
    if match is None:
        raise AssertionError(f"missing {name}")
    return [int(value, 0) for value in re.findall(r"0x[0-9A-Fa-f]+|\b\d+\b",
                                                   match.group(1))]


class IplPatchTableTests(unittest.TestCase):
    def test_language_tables_preserve_every_address_and_value(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        for name, expected in EXPECTED.items():
            with self.subTest(name=name):
                self.assertEqual(_table(source, name), expected)

    def test_offsets_fit_the_compact_representation(self) -> None:
        for entries in EXPECTED.values():
            for address, value in entries:
                offset = address - 0x81300000
                self.assertLessEqual(offset, 0xFFFF)
                self.assertLessEqual(value, 0xFF)
                self.assertEqual(address & 1, 0)

    def test_each_detected_revision_uses_one_matching_table(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        for name in EXPECTED:
            second = 38 if name == "kEnglishUs10" else 10
            self.assertEqual(
                source.count(f"ApplyEnglishPatches({name}, {second})"),
                1,
            )
        self.assertEqual(source.count("ApplyEnglishPatches("), len(EXPECTED) + 1)

    def test_pal_animation_tables_preserve_order_width_and_values(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        half_offsets = _array(source, "u8", "kPalAnimHalfOffsets")
        half_values = _array(source, "u8", "kPalAnimHalfValues")
        word_offsets = _array(source, "u8", "kPalAnimWordOffsets")
        word_values = _array(source, "u32", "kPalAnimWordValues")
        for base, expected in EXPECTED_PAL_ANIMATION.items():
            actual = [(base + offset, value, 2)
                      for offset, value in zip(half_offsets, half_values)]
            actual += [(base + offset, value, 4)
                       for offset, value in zip(word_offsets, word_values)]
            self.assertEqual(actual, expected)
            self.assertEqual(source.count(f"ApplyPalAnimation(0x{base:08X})"), 1)

    def test_shared_video_filter_preserves_write_sequence(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        for address in FILTER_ADDRESSES:
            self.assertEqual(
                source.count(f"ApplyVideoFilter(0x{address:08X}, prog, sharp)"),
                1,
            )
            for progressive in (False, True):
                for sharp in (False, True):
                    legacy = []
                    if progressive:
                        legacy += [(address, 0x0408, 2),
                                   (address + 2, 0x0C100C08, 4),
                                   (address + 6, 0x0400, 2)]
                    if sharp:
                        legacy += [(address, 0, 2),
                                   (address + 2, 0x15161500, 4),
                                   (address + 6, 0, 2)]
                    shared = []
                    if progressive:
                        shared += [(address, 0x0408, 2),
                                   (address + 2, 0x0C100C08, 4),
                                   (address + 6, 0x0400, 2)]
                    if sharp:
                        shared += [(address, 0, 2),
                                   (address + 2, 0x15161500, 4),
                                   (address + 6, 0, 2)]
                    self.assertEqual(shared, legacy)
        self.assertIn("if (progressive)", source)
        self.assertIn("if (sharp)", source)

    def test_shared_jingle_write_preserves_addresses_and_gate(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("__attribute__((noinline)) ApplyJingle", source)
        for address, uses in JINGLE_ADDRESSES.items():
            self.assertEqual(
                source.count(f"ApplyJingle(0x{address:08X}, jingle)"), uses
            )
        for jingle in (-1, 0, 1, 2, 0x123):
            legacy = None if jingle <= 0 else 0x38600000 | jingle
            shared = None if jingle <= 0 else 0x38600000 | jingle
            self.assertEqual(shared, legacy)


if __name__ == "__main__":
    unittest.main()
