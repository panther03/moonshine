#!/usr/bin/env python3
"""Source contracts for bounded launcher archive decompression."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


def hex_define(source: str, name: str) -> int:
    match = re.search(
        rf"^#define\s+{re.escape(name)}\s+(0x[0-9A-Fa-f]+)u?\s*$",
        source,
        re.MULTILINE,
    )
    if not match:
        raise AssertionError(f"missing {name}")
    return int(match.group(1), 16)


class LauncherUnzipContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.global_source = (
            ROOT / "launcher" / "loader" / "source" / "global.c"
        ).read_text()
        cls.main_source = (
            ROOT / "launcher" / "loader" / "source" / "main.c"
        ).read_text()
        cls.mem2 = (ROOT / "include" / "susamune" / "mem2_map.h").read_text()
        cls.kernel_ld = (ROOT / "launcher" / "kernel" / "kernel.ld").read_text()
        cls.unzip_source = (
            ROOT / "launcher" / "loader" / "source" / "unzip" / "unzip.c"
        ).read_text()

    def test_kernel_image_cap_ends_at_the_linker_stack(self) -> None:
        base = hex_define(self.mem2, "NIN_MEM2_KERNEL_PHYS_BASE")
        capacity = hex_define(self.mem2, "NIN_MEM2_KERNEL_IMAGE_SIZE")
        stack = re.search(
            r"stack\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+)", self.kernel_ld
        )
        self.assertIsNotNone(stack)
        self.assertEqual(base + capacity, int(stack.group(1), 16))

    def test_kernel_decompresses_directly_into_its_final_window(self) -> None:
        self.assertIn(
            "unzip_data_into(kernel_zip, kernel_zip_size,\n"
            "\t\t(void*)NIN_MEM2_KERNEL_PPC_BASE, NIN_MEM2_KERNEL_IMAGE_SIZE",
            self.main_source,
        )
        self.assertNotIn("void *kernel_bin", self.main_source)
        self.assertNotIn("memcpy((void*)0x92F00000", self.main_source)

    def test_capacity_is_checked_before_decompression(self) -> None:
        bound = self.global_source.index(
            "file_info.uncompressed_size > output_capacity"
        )
        open_current = self.global_source.index("unzOpenCurrentFile(uf)", bound)
        self.assertLess(bound, open_current)

    def test_unsupported_compression_is_rejected_before_decompression(self) -> None:
        method = self.global_source.index(
            "file_info.compression_method != Z_DEFLATED"
        )
        open_current = self.global_source.index("unzOpenCurrentFile(uf)", method)
        self.assertLess(method, open_current)

    def test_embedded_archives_must_contain_exactly_one_entry(self) -> None:
        global_info = self.global_source.index("unzGetGlobalInfo(uf, &global_info)")
        count = self.global_source.index("global_info.number_entry != 1", global_info)
        file_info = self.global_source.index("unzGetCurrentFileInfo", count)
        self.assertLess(global_info, count)
        self.assertLess(count, file_info)

    def test_partial_reads_and_archive_crc_are_checked(self) -> None:
        self.assertIn("while (total < expected)", self.global_source)
        self.assertIn("if (result <= 0)", self.global_source)
        close = self.global_source.index("result = unzCloseCurrentFile(uf)")
        check = self.global_source.index("if (result != UNZ_OK)", close)
        self.assertLess(close, check)

    def test_minizip_open_handles_metadata_allocation_failure(self) -> None:
        allocation = self.unzip_source.index("s=(unz_s*)ALLOC(sizeof(unz_s))")
        null_check = self.unzip_source.index("if (s==NULL)", allocation)
        dereference = self.unzip_source.index("*s=us", allocation)
        self.assertLess(null_check, dereference)

    def test_minizip_frees_read_buffer_when_inflate_init_fails(self) -> None:
        init = self.unzip_source.index("err=inflateInit2")
        failure = self.unzip_source.index("else", init)
        free_buffer = self.unzip_source.index(
            "TRYFREE(pfile_in_zip_read_info->read_buffer)", failure
        )
        free_metadata = self.unzip_source.index(
            "TRYFREE(pfile_in_zip_read_info);", free_buffer
        )
        error_return = self.unzip_source.index("return err;", free_metadata)
        self.assertLess(free_buffer, free_metadata)
        self.assertLess(free_metadata, error_return)


if __name__ == "__main__":
    unittest.main()
