"""Regression contracts for small V2.2 runtime-memory optimizations."""

from __future__ import annotations

import pathlib
import random
import re
import unittest
import zlib


ROOT = pathlib.Path(__file__).resolve().parents[1]


def _checksum_source() -> str:
    return (ROOT / "src/checksum.cpp").read_text(encoding="utf-8")


def _table() -> list[int]:
    source = _checksum_source()
    body = re.search(r"table\[16\]\s*=\s*\{(.*?)\};", source, re.S)
    if body is None:
        raise AssertionError("CRC table is missing")
    values = [int(value, 16) for value in re.findall(r"0x([0-9A-Fa-f]+)u", body.group(1))]
    if len(values) != 16:
        raise AssertionError("CRC table no longer has 16 entries")
    return values


def _crc32(data: bytes, zero_offset: int = 0, zero_size: int = 0) -> int:
    table = _table()
    crc = 0xFFFFFFFF
    for index, byte in enumerate(data):
        value = 0 if zero_offset <= index < zero_offset + zero_size else byte
        crc = (crc >> 4) ^ table[(crc ^ value) & 0xF]
        crc = (crc >> 4) ^ table[(crc ^ (value >> 4)) & 0xF]
    return crc ^ 0xFFFFFFFF


class ChecksumTests(unittest.TestCase):
    def test_shared_crc_matches_zlib(self) -> None:
        rng = random.Random(0x5A455441)
        for size in (0, 1, 2, 7, 16, 255, 1024):
            payload = bytes(rng.randrange(256) for _ in range(size))
            self.assertEqual(_crc32(payload), zlib.crc32(payload) & 0xFFFFFFFF)

    def test_zeroed_range_matches_materialized_bytes(self) -> None:
        payload = bytes(range(64))
        expected = bytearray(payload)
        expected[12:20] = bytes(8)
        self.assertEqual(_crc32(payload, 12, 8), zlib.crc32(expected) & 0xFFFFFFFF)

    def test_mod_consumers_use_one_implementation(self) -> None:
        for relative in (
            "src/crash_report.cpp",
            "src/ghost.cpp",
            "src/ghost_model.cpp",
        ):
            source = (ROOT / relative).read_text(encoding="utf-8")
            self.assertIn("Checksum::crc32(", source)
            self.assertNotRegex(source, r"\bu32\s+crc32\s*\(")


class RuntimeStorageTests(unittest.TestCase):
    def test_savestate_controller_uses_reserved_mod_storage(self) -> None:
        source = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
        self.assertIn("new (sSavestateManagerStorage) SavestateManager()", source)
        self.assertNotIn("new SavestateManager()", source)

    def test_di_keepalive_reuses_fixed_scratch(self) -> None:
        source = (ROOT / "launcher/kernel/DI.c").read_text(encoding="utf-8")
        self.assertNotIn("DummyBuffer", source)
        self.assertRegex(
            source,
            r"USBStorage_ReadSectors\(read32\(HW_TIMER\) % s_cnt, 1,\s*"
            r"DI_READ_BUFFER\)",
        )

    def test_ipl_font_read_uses_a_mem1_chunk_not_a_full_heap_copy(self) -> None:
        source = (ROOT / "launcher/loader/source/main.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("u8 chunk[0x100] ATTRIBUTE_ALIGN(32);", source)
        self.assertIn("offset < 0x50000u", source)
        self.assertIn("0xD3100000u + offset", source)
        self.assertIn("DCInvalidateRange((void*)0x93100000, 0x50000u)", source)
        self.assertNotRegex(source, r"memalign\(32,\s*0x50000\)")

    def test_multigame_picker_reuses_one_header_buffer(self) -> None:
        source = (ROOT / "launcher/loader/source/main.c").read_text(
            encoding="utf-8"
        )
        start = source.index("static u32 CheckForMultiGameAndRegion")
        end = source.index("static char dev_es", start)
        picker = source[start:end]
        self.assertEqual(picker.count("memalign(32, 0x800)"), 1)
        self.assertIn("u8 *GameHdr = MultiHdr;", picker)
        self.assertIn("char gameNames[15][66];", picker)
        self.assertNotIn("strdup(", picker)
        self.assertNotIn("free(GameHdr)", picker)

    def test_multigame_offset_staging_preserves_order_and_encoding(self) -> None:
        rng = random.Random(0x4D554C54)
        for shifted in (False, True):
            for _ in range(500):
                table = [rng.randrange(0, 1 << 32) if rng.randrange(4) else 0
                         for _ in range(48)]
                old = []
                for raw in table:
                    if raw and len(old) < 15:
                        old.append((raw if shifted else raw >> 2,
                                    False if shifted else bool(raw & 3)))
                staged = [raw for raw in table if raw][:15]
                new = [(raw if shifted else raw >> 2,
                        False if shifted else bool(raw & 3))
                       for raw in staged]
                self.assertEqual(new, old)

    def test_gamecube_ipl_reuses_non_triforce_dimm_memory(self) -> None:
        main = (ROOT / "launcher/loader/source/main.c").read_text(
            encoding="utf-8"
        )
        mem2 = (ROOT / "include/susamune/mem2_map.h").read_text(
            encoding="utf-8"
        )
        kernel_di = (ROOT / "launcher/kernel/DI.c").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "SUSAMUNE_LAUNCHER_GCN_IPL_PPC_BASE   NIN_MEM2_DIMM_PPC_BASE",
            mem2,
        )
        self.assertIn("SUSAMUNE_LAUNCHER_GCN_IPL_SIZE       0x00200000u", mem2)
        self.assertIn(
            "void *iplbuf = (void*)SUSAMUNE_LAUNCHER_GCN_IPL_PPC_BASE;",
            main,
        )
        self.assertNotIn("iplbuf = malloc(GCN_IPL_SIZE)", main)
        self.assertIn("if(IsTRIGame == 0)", main)
        clear = "ncfg->Config &= ~NIN_CFG_GCN_IPL_STAGED;"
        self.assertIn(clear, main)
        self.assertLess(main.index(clear), main.index("TRISetupGames("))
        self.assertRegex(
            main,
            r"useipl = \(result == FR_OK && read == GCN_IPL_SIZE\);\s*"
            r"if \(useipl\)\s*"
            r"ncfg->Config \|= NIN_CFG_GCN_IPL_STAGED;",
        )
        self.assertRegex(
            kernel_di,
            r"if\(!ConfigGetConfig\(NIN_CFG_GCN_IPL_STAGED\)\)\s*"
            r"memset32\( DIMMMemory, 0, NIN_MEM2_DIMM_SIZE \);",
        )

    def test_theme_png_row_is_transient_stack_storage(self) -> None:
        source = (ROOT / "launcher/loader/source/SusamuneTheme.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("u8 pngRow[THEME_PNG_ROW_BYTES] ATTRIBUTE_ALIGN(32);", source)
        self.assertNotIn("static u8 sPngRow", source)
        # Error state crosses png_longjmp and therefore must not be automatic.
        self.assertIn("static ThemePngReader sPngReader;", source)


if __name__ == "__main__":
    unittest.main()
