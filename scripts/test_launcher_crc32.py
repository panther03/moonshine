"""Equivalence tests for the loader's compact zlib-compatible CRC32."""

from __future__ import annotations

import pathlib
import random
import re
import unittest
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "launcher/loader/source/SusamuneCrc32.c"


def _table() -> list[int]:
    source = SOURCE.read_text(encoding="utf-8")
    match = re.search(r"kCrcTable\[16\]\s*=\s*\{(.*?)\};", source, re.S)
    if match is None:
        raise AssertionError("compact CRC table is missing")
    values = [int(value, 16)
              for value in re.findall(r"0x([0-9A-Fa-f]+)UL", match.group(1))]
    if len(values) != 16:
        raise AssertionError("compact CRC table must have 16 entries")
    return values


def _crc32(crc: int, payload: bytes | None) -> int:
    if payload is None:
        return 0
    table = _table()
    crc ^= 0xFFFFFFFF
    for byte in payload:
        crc = (crc >> 4) ^ table[(crc ^ byte) & 0xF]
        crc = (crc >> 4) ^ table[(crc ^ (byte >> 4)) & 0xF]
    return (crc ^ 0xFFFFFFFF) & 0xFFFFFFFF


class LauncherCrc32Tests(unittest.TestCase):
    def test_matches_zlib_for_random_payloads(self) -> None:
        rng = random.Random(0x43524332)
        for size in (0, 1, 2, 3, 15, 16, 31, 255, 4096, 65537):
            payload = rng.randbytes(size)
            self.assertEqual(_crc32(0, payload), zlib.crc32(payload) & 0xFFFFFFFF)

    def test_incremental_updates_match_zlib(self) -> None:
        rng = random.Random(0x5A455441)
        payload = rng.randbytes(200_000)
        compact = 0
        reference = 0
        start = 0
        while start < len(payload):
            length = min(rng.randrange(1, 4097), len(payload) - start)
            chunk = payload[start:start + length]
            compact = _crc32(compact, chunk)
            reference = zlib.crc32(chunk, reference) & 0xFFFFFFFF
            start += length
        self.assertEqual(compact, reference)

    def test_zlib_null_buffer_semantics(self) -> None:
        self.assertEqual(_crc32(0x12345678, None), 0)

    def test_encrypted_zip_support_is_compiled_out(self) -> None:
        cmake = (ROOT / "launcher/loader/CMakeLists.txt").read_text(encoding="utf-8")
        unzip = (ROOT / "launcher/loader/source/unzip/unzip.c").read_text(
            encoding="utf-8", errors="replace"
        )
        self.assertRegex(cmake, r"ARCH_IS_BIG_ENDIAN\s+NOUNCRYPT")
        self.assertIn("if (password != NULL)\n        return UNZ_PARAMERROR;", unzip)


if __name__ == "__main__":
    unittest.main()
