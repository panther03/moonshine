"""Keep the source-free Dolphin layouts in sync with the runtime hooks."""

from __future__ import annotations

import importlib.util
import json
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
KNOWN_DISC_CRC32 = {
    "jp": "C3B17583",
    "us": "771AD977",
    "pal": "4C1D3641",
}


def _load_patches_module():
    path = ROOT / "scripts/patches.py"
    spec = importlib.util.spec_from_file_location("susamune_patches", path)
    if spec is None or spec.loader is None:
        raise AssertionError("could not load scripts/patches.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class IsoLayoutHookTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.patches = _load_patches_module()

    def test_checked_in_layouts_cover_every_runtime_write(self) -> None:
        for region in KNOWN_DISC_CRC32:
            with self.subTest(region=region):
                layout = json.loads(
                    (ROOT / f"data/iso_layout_{region}.json").read_text(
                        encoding="utf-8"
                    )
                )
                expected = []
                for patch in self.patches.patches:
                    address = patch[region]
                    expected.extend(
                        address + offset * 4
                        for offset in range(1 + patch.get("nop_count", 0))
                    )
                actual = [hook["address"] for hook in layout["hooks"]]

                self.assertEqual(actual, expected)
                self.assertEqual(len(actual), len(set(actual)))
                self.assertTrue(all(address % 4 == 0 for address in actual))
                self.assertTrue(
                    all(hook["iso_offset"] % 4 == 0 for hook in layout["hooks"])
                )

    def test_layouts_name_the_verified_retail_discs(self) -> None:
        for region, crc32 in KNOWN_DISC_CRC32.items():
            with self.subTest(region=region):
                layout = json.loads(
                    (ROOT / f"data/iso_layout_{region}.json").read_text(
                        encoding="utf-8"
                    )
                )
                self.assertEqual(layout["region"], region)
                self.assertEqual(layout["game_id"], self.patches.game_id[region])
                self.assertEqual(layout["base_addr"], self.patches.base_addr[region])
                self.assertEqual(
                    layout["mod_region_size"], self.patches.mod_region_size
                )
                self.assertEqual(layout["source_crc32"], crc32)


if __name__ == "__main__":
    unittest.main()
