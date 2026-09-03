"""Exhaustive host oracle for the locale-free FatFs UTF converters."""

from __future__ import annotations

import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
PATH_UNITS = 512
NAME_BYTES = 255 * 3 + 1
REPLACEMENT = 0xFFFD


def _decode_model(data: bytes | None, capacity: int = PATH_UNITS) -> tuple[int, ...] | None:
    if data is None or capacity == 0:
        return None
    data = data.split(b"\0", 1)[0]
    if not data:
        return None

    output: list[int] = []
    cursor = 0
    while cursor < len(data):
        lead = data[cursor]
        cursor += 1
        if lead < 0x80:
            codepoint, minimum, trailing = lead, 0, 0
        elif lead & 0xE0 == 0xC0:
            codepoint, minimum, trailing = lead & 0x1F, 0x80, 1
        elif lead & 0xF0 == 0xE0:
            codepoint, minimum, trailing = lead & 0x0F, 0x800, 2
        elif lead & 0xF8 == 0xF0:
            codepoint, minimum, trailing = lead & 0x07, 0x10000, 3
        else:
            return None

        for _ in range(trailing):
            if cursor >= len(data) or data[cursor] & 0xC0 != 0x80:
                return None
            codepoint = (codepoint << 6) | (data[cursor] & 0x3F)
            cursor += 1
        if (
            codepoint < minimum
            or codepoint > 0x10FFFF
            or 0xD800 <= codepoint <= 0xDFFF
        ):
            return None

        if codepoint >= 0x10000:
            if len(output) + 2 >= capacity:
                return None
            codepoint -= 0x10000
            output.extend((0xD800 | (codepoint >> 10), 0xDC00 | (codepoint & 0x3FF)))
        else:
            if len(output) + 1 >= capacity:
                return None
            output.append(codepoint)
    return tuple(output)


def _encode_model(units: tuple[int, ...] | None, capacity: int = NAME_BYTES) -> bytes:
    if units is None or capacity == 0:
        return b""

    output = bytearray()
    cursor = 0
    while cursor < len(units) and units[cursor] != 0:
        codepoint = units[cursor]
        cursor += 1
        if 0xD800 <= codepoint <= 0xDBFF:
            if cursor < len(units) and 0xDC00 <= units[cursor] <= 0xDFFF:
                codepoint = (
                    0x10000
                    + ((codepoint - 0xD800) << 10)
                    + units[cursor]
                    - 0xDC00
                )
                cursor += 1
            else:
                codepoint = REPLACEMENT
        elif 0xDC00 <= codepoint <= 0xDFFF:
            codepoint = REPLACEMENT

        encoded = chr(codepoint).encode("utf-8")
        if len(output) + len(encoded) >= capacity:
            break
        output.extend(encoded)
    return bytes(output)


def _oracle_units(data: bytes) -> tuple[int, ...] | None:
    data = data.split(b"\0", 1)[0]
    if not data:
        return None
    try:
        text = data.decode("utf-8", "strict")
    except UnicodeDecodeError:
        return None
    raw = text.encode("utf-16-be")
    units = tuple(int.from_bytes(raw[i : i + 2], "big") for i in range(0, len(raw), 2))
    return units if len(units) < PATH_UNITS else None


class Utf8OracleTests(unittest.TestCase):
    def test_every_unicode_scalar_round_trips(self) -> None:
        for codepoint in range(1, 0x110000):
            if 0xD800 <= codepoint <= 0xDFFF:
                continue
            payload = chr(codepoint).encode("utf-8")
            expected = _oracle_units(payload)
            actual = _decode_model(payload)
            if actual != expected:
                self.fail(f"decode mismatch for U+{codepoint:04X}: {actual!r} != {expected!r}")
            if _encode_model(actual) != payload:
                self.fail(f"encode mismatch for U+{codepoint:04X}")

    def test_all_overlong_two_and_three_byte_sequences_are_rejected(self) -> None:
        invalid = []
        invalid.extend(bytes((lead, tail)) for lead in (0xC0, 0xC1) for tail in range(0x80, 0xC0))
        invalid.extend(
            bytes((0xE0, second, third))
            for second in range(0x80, 0xA0)
            for third in range(0x80, 0xC0)
        )
        for payload in invalid:
            self.assertIsNone(_oracle_units(payload))
            self.assertIsNone(_decode_model(payload))

    def test_all_overlong_four_byte_sequences_are_rejected(self) -> None:
        for second in range(0x80, 0x90):
            for third in range(0x80, 0xC0):
                for fourth in range(0x80, 0xC0):
                    payload = bytes((0xF0, second, third, fourth))
                    self.assertIsNone(_oracle_units(payload))
                    self.assertIsNone(_decode_model(payload))

    def test_all_utf8_surrogates_are_rejected(self) -> None:
        for second in range(0xA0, 0xC0):
            for third in range(0x80, 0xC0):
                payload = bytes((0xED, second, third))
                self.assertIsNone(_oracle_units(payload))
                self.assertIsNone(_decode_model(payload))

    def test_all_codepoints_above_unicode_are_rejected(self) -> None:
        for second in range(0x90, 0xC0):
            for third in range(0x80, 0xC0):
                for fourth in range(0x80, 0xC0):
                    payload = bytes((0xF4, second, third, fourth))
                    self.assertIsNone(_oracle_units(payload))
                    self.assertIsNone(_decode_model(payload))
        for lead in range(0xF5, 0xF8):
            for second in range(0x80, 0xC0):
                for third in range(0x80, 0xC0):
                    for fourth in range(0x80, 0xC0):
                        self.assertIsNone(_decode_model(bytes((lead, second, third, fourth))))

    def test_invalid_leads_continuations_and_truncation_are_rejected(self) -> None:
        for lead in (*range(0x80, 0xC0), *range(0xF8, 0x100)):
            self.assertIsNone(_decode_model(bytes((lead,))))

        invalid_continuations = (*range(1, 0x80), *range(0xC0, 0x100))
        for valid in (b"\xC2\x80", b"\xE1\x80\x80", b"\xF1\x80\x80\x80"):
            for length in range(1, len(valid)):
                self.assertIsNone(_decode_model(valid[:length]))
            for index in range(1, len(valid)):
                for invalid in invalid_continuations:
                    candidate = bytearray(valid)
                    candidate[index] = invalid
                    self.assertIsNone(_decode_model(bytes(candidate)))

    def test_null_empty_embedded_null_and_capacity_contracts(self) -> None:
        self.assertIsNone(_decode_model(None))
        self.assertIsNone(_decode_model(b""))
        self.assertEqual(_decode_model(b"abc\0ignored"), (ord("a"), ord("b"), ord("c")))
        self.assertEqual(len(_decode_model(b"a" * 511) or ()), 511)
        self.assertIsNone(_decode_model(b"a" * 512))
        self.assertEqual(len(_decode_model("😀".encode() * 255) or ()), 510)
        self.assertIsNone(_decode_model("😀".encode() * 256))

    def test_every_surrogate_pair_and_lone_surrogate_encoding(self) -> None:
        for high in range(0xD800, 0xDC00):
            for low in range(0xDC00, 0xE000):
                codepoint = 0x10000 + ((high - 0xD800) << 10) + low - 0xDC00
                self.assertEqual(_encode_model((high, low)), chr(codepoint).encode())
        replacement = "\uFFFD".encode()
        for surrogate in range(0xD800, 0xE000):
            self.assertEqual(_encode_model((surrogate,)), replacement)

    def test_full_fat_name_has_room_for_worst_case_utf8(self) -> None:
        units = (0xFFFF,) * 255
        encoded = _encode_model(units)
        self.assertEqual(len(encoded), 765)
        self.assertEqual(encoded, "\uFFFF".encode() * 255)
        self.assertEqual(_encode_model(units, 765), "\uFFFF".encode() * 254)


class SourceContractTests(unittest.TestCase):
    def test_paths_and_renderer_are_locale_free(self) -> None:
        wrapper = (ROOT / "launcher/fatfs/ff_utf8.c").read_text(encoding="utf-8")
        main = (ROOT / "launcher/loader/source/main.c").read_text(encoding="utf-8")
        grrlib = (ROOT / "launcher/loader/source/grrlib.c").read_text(encoding="utf-8")
        self.assertNotIn("mbstowcs", wrapper)
        self.assertNotIn("wcstombs", wrapper)
        self.assertNotIn("mbstowcs", grrlib)
        self.assertNotIn("wcstombs", grrlib)
        self.assertNotIn("setlocale(", main)
        self.assertNotIn("<locale.h>", main)

    def test_renderer_decodes_without_per_draw_allocation(self) -> None:
        grrlib = (ROOT / "launcher/loader/source/grrlib.c").read_text(encoding="utf-8")
        start = grrlib.index("static bool Utf8Valid")
        end = grrlib.index("static void DrawBitmap", start)
        renderer = grrlib[start:end]
        self.assertGreaterEqual(renderer.count("ff_utf8_decode_next"), 2)
        self.assertNotIn("malloc(", renderer)
        self.assertNotIn("free(", renderer)
        self.assertNotIn("wchar_t", renderer)
        self.assertNotIn("GRRLIB_PrintfTTFW", grrlib)
        self.assertNotIn("GRRLIB_WidthTTFW", grrlib)

    def test_scratch_and_failure_contracts_are_bounded(self) -> None:
        wrapper = (ROOT / "launcher/fatfs/ff_utf8.c").read_text(encoding="utf-8")
        core = (ROOT / "launcher/fatfs/ff_utf8_core.h").read_text(encoding="utf-8")
        self.assertIn("WCHAR path[512]", wrapper)
        self.assertIn("char name[_MAX_LFN * 3 + 1]", wrapper)
        self.assertGreaterEqual(wrapper.count("return FR_INVALID_NAME;"), 5)
        self.assertIn("value > 0x10ffffu", wrapper)
        self.assertIn("ff_utf8_decode_next(&cursor, &codepoint)", core)
        self.assertIn("codepoint = 0xfffdu", core)
        self.assertIn("used + needed >= capacity", core)
        # Every f_* wrapper calls this decoder; one out-of-line copy is smaller.
        self.assertIn("FF_UTF8_NOINLINE bool ff_utf8_to_utf16", core)


if __name__ == "__main__":
    unittest.main()
