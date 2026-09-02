#!/usr/bin/env python3
"""Host contracts for the launcher mod-bin capacity boundaries."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
GEN_PATH = ROOT / "scripts" / "gen_mod_bin.py"

spec = importlib.util.spec_from_file_location("gen_mod_bin_test", GEN_PATH)
assert spec and spec.loader
gen_mod_bin = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen_mod_bin)


def manifest(code_size: int, write_count: int, memory_size: int | None = None) -> dict:
    return {
        "code": "00" * code_size,
        "size": code_size,
        "memory_size": code_size if memory_size is None else memory_size,
        "game_id": 0x474D534A,
        "base_addr": 0x80426020,
        "region_reserve": 0x82000,
        "writes": [(0x80000000, 0)] * write_count,
    }


class ModBinCapacityTests(unittest.TestCase):
    def test_shared_capacity_constants(self) -> None:
        self.assertEqual(gen_mod_bin.MAGIC, 0x534D4F44)
        self.assertEqual(gen_mod_bin.VERSION, 2)
        self.assertEqual(gen_mod_bin.HEADER_SIZE, 32)
        self.assertEqual(gen_mod_bin.BLOB_MAX_SIZE, 0x58000)
        self.assertEqual(gen_mod_bin.STAGED_FILE_MAX_SIZE, 0x5F000)

    def test_current_write_count_fits_at_raw_cap(self) -> None:
        packed = gen_mod_bin.build_mod_bin(manifest(0x58000, 22))
        self.assertEqual(len(packed), 0x580D0)

    def test_v2_header_separates_file_prefix_from_runtime_image(self) -> None:
        packed = gen_mod_bin.build_mod_bin(manifest(8, 1, 0x100))
        header = struct.unpack(">8I", packed[:gen_mod_bin.HEADER_SIZE])
        self.assertEqual(header[1], 2)
        self.assertEqual(header[4], 8)
        self.assertEqual(header[5], 1)
        self.assertEqual(header[7], 0x100)
        self.assertEqual(len(packed), gen_mod_bin.HEADER_SIZE + 8 + 8)
        self.assertEqual(packed[-8:], struct.pack(">II", 0x80000000, 0))

    def test_initialized_prefix_cannot_exceed_runtime_image(self) -> None:
        with self.assertRaisesRegex(ValueError, "initialized code"):
            gen_mod_bin.build_mod_bin(manifest(8, 0, 4))

    def test_manifest_code_size_must_match_payload(self) -> None:
        value = manifest(8, 0)
        value["size"] = 4
        with self.assertRaisesRegex(ValueError, "manifest code size"):
            gen_mod_bin.build_mod_bin(value)

    def test_boolean_runtime_size_is_not_an_integer_size(self) -> None:
        value = manifest(0, 0)
        value["memory_size"] = True
        with self.assertRaisesRegex(ValueError, "runtime image size"):
            gen_mod_bin.build_mod_bin(value)

    def test_runtime_image_cap_is_strict(self) -> None:
        with self.assertRaisesRegex(ValueError, "MEM1 working cap"):
            gen_mod_bin.build_mod_bin(manifest(4, 0, 0x58004))

    def test_both_sizes_must_be_word_aligned(self) -> None:
        with self.assertRaisesRegex(ValueError, "code blob"):
            gen_mod_bin.build_mod_bin(manifest(3, 0, 4))
        with self.assertRaisesRegex(ValueError, "runtime image"):
            gen_mod_bin.build_mod_bin(manifest(4, 0, 5))

    def test_raw_cap_is_strict(self) -> None:
        with self.assertRaisesRegex(ValueError, "MEM1 working cap"):
            gen_mod_bin.build_mod_bin(manifest(0x58004, 0))

    def test_staged_file_ceiling_is_end_exclusive(self) -> None:
        exact_writes = (0x5F000 - gen_mod_bin.HEADER_SIZE - 0x58000) // 8
        self.assertEqual(
            len(gen_mod_bin.build_mod_bin(manifest(0x58000, exact_writes))),
            0x5F000,
        )
        with self.assertRaisesRegex(ValueError, "reset-safe ceiling"):
            gen_mod_bin.build_mod_bin(manifest(0x58000, exact_writes + 1))


class ModBinConsumerContractTests(unittest.TestCase):
    def test_exfat_size_is_checked_before_narrowing(self) -> None:
        source = (ROOT / "launcher" / "loader" / "source" /
                  "SusamuneMod.c").read_text()
        read_pos = source.index("sizeOnDisk = fd.obj.objsize")
        bound_pos = source.index(
            "sizeOnDisk > SUSAMUNE_MOD_STAGED_FILE_MAX_SIZE", read_pos)
        cast_pos = source.index("size = (u32)sizeOnDisk", bound_pos)
        self.assertLess(read_pos, bound_pos)
        self.assertLess(bound_pos, cast_pos)

    def test_all_consumers_bound_the_runtime_image(self) -> None:
        paths = (
            ROOT / "launcher" / "loader" / "source" / "SusamuneMod.c",
            ROOT / "launcher" / "kernel" / "Patch.c",
            ROOT / "launcher" / "kernel" / "SusamuneCrash.c",
        )
        for path in paths:
            source = path.read_text()
            with self.subTest(path=path.name):
                self.assertRegex(source, r"codeSize\s*>\s*\w+->memSize")
                self.assertRegex(
                    source,
                    r"memSize\s*>\s*SUSAMUNE_MOD_BLOB_MAX_SIZE",
                )
                self.assertRegex(source, r"memSize\s*&\s*3")

    def test_kernel_reconstructs_and_syncs_the_full_image(self) -> None:
        source = (ROOT / "launcher" / "kernel" / "Patch.c").read_text()
        self.assertIn(
            "memset((void*)(base + hdr->codeSize), 0, "
            "hdr->memSize - hdr->codeSize)",
            source,
        )
        self.assertIn("sync_after_write((void*)base, hdr->memSize)", source)


if __name__ == "__main__":
    unittest.main()
