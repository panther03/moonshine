#!/usr/bin/env python3
"""Guards for fixed MEM2 owners that are safe only in this build profile."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class Mem2OwnershipTests(unittest.TestCase):
    def test_memory_card_emulation_remains_compile_disabled(self) -> None:
        main = (ROOT / "launcher/loader/source/main.c").read_text(
            encoding="utf-8"
        )
        match = re.search(
            r"#ifndef ENABLE_MEMCARD_EMU(?P<body>.*?)#endif",
            main,
            re.DOTALL,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertIn(
            "ncfg->Config &= ~(NIN_CFG_MEMCARDEMU | NIN_CFG_MC_MULTI);",
            body,
        )
        self.assertIn("ncfg->MemCardBlocks = 0;", body)

        build_files = list((ROOT / "launcher").rglob("CMakeLists.txt"))
        build_files += list((ROOT / "launcher").rglob("*.cmake"))
        for path in build_files:
            with self.subTest(path=path.relative_to(ROOT)):
                self.assertNotIn(
                    "ENABLE_MEMCARD_EMU",
                    path.read_text(encoding="utf-8", errors="replace"),
                )

    def test_disabled_guard_covers_the_known_extent_mismatch(self) -> None:
        mem2 = (ROOT / "include/susamune/mem2_map.h").read_text(
            encoding="utf-8"
        )
        match = re.search(
            r"#define\s+NIN_MEM2_DISC_CACHE_SIZE\s+(0x[0-9A-Fa-f]+)u",
            mem2,
        )
        self.assertIsNotNone(match)
        disc_cache_size = int(match.group(1), 16)
        max_card_size = 1 << (5 + 19)
        self.assertEqual(disc_cache_size, 3 * 1024 * 1024)
        self.assertEqual(max_card_size, 16 * 1024 * 1024)
        self.assertGreater(max_card_size, disc_cache_size)

        card = (ROOT / "launcher/kernel/GCNCard.c").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "GCNCard_base = (u8*)NIN_MEM2_DISC_CACHE_PHYS_BASE", card
        )


if __name__ == "__main__":
    unittest.main()
