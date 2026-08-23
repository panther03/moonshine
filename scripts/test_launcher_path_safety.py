"""Contracts for bounded launcher paths and legacy patch-file reads."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def build_device_path(device: str, path: str, suffix: str, capacity: int):
    result = f"{device}:{path}{suffix}"
    return result if len(result.encode("utf-8")) < capacity else None


def build_sibling_path(device: str, path: str, name: str, capacity: int):
    slash = path.rfind("/")
    if slash < 0:
        return None
    return build_device_path(device, path[: slash + 1], name, capacity)


class LauncherPathSafetyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.main = (ROOT / "launcher/loader/source/main.c").read_text()

    def test_capacity_edges_fail_closed(self) -> None:
        self.assertEqual(build_device_path("usb", "/game.iso", "", 260),
                         "usb:/game.iso")
        self.assertEqual(len(build_device_path("usb", "/" + "a" * 253,
                                               "", 260) or ""), 258)
        self.assertIsNone(build_device_path("usb", "/" + "a" * 254,
                                            "", 259))
        self.assertIsNone(build_device_path("usb", "/" + "a" * 253,
                                            "sys/boot.bin", 260))

    def test_sibling_replaces_the_image_name(self) -> None:
        self.assertEqual(
            build_sibling_path("sd", "/games/SMS/game.iso", "patch.bin", 260),
            "sd:/games/SMS/patch.bin",
        )
        self.assertIsNone(build_sibling_path("sd", "game.iso", "patch.bin", 260))

    def test_source_uses_one_bounded_builder(self) -> None:
        self.assertIn("static bool BuildDevicePathN", self.main)
        self.assertIn("static bool BuildSiblingPath", self.main)
        for stale in (
            'snprintf(cheatPath',
            'snprintf(GamePath',
            'snprintf(filepath, sizeof(filepath), "%smeta.xml"',
        ):
            self.assertNotIn(stale, self.main)
        self.assertGreaterEqual(self.main.count("BuildSiblingPath("), 5)
        self.assertGreaterEqual(self.main.count("BuildDevicePath("), 5)

    def test_partial_patch_reads_never_publish(self) -> None:
        self.assertIn("result == FR_OK && read == (UINT)CodeFD.obj.objsize",
                      self.main)
        self.assertIn("*patch_cntAddr = 0;", self.main)
        self.assertIn("if (CMem != NULL)", self.main)

    def test_multigame_region_copy_targets_the_value(self) -> None:
        self.assertIn("memcpy(BI2region, wdvdTmpBuf, sizeof(*BI2region));",
                      self.main)
        self.assertNotIn("memcpy(&BI2region, wdvdTmpBuf", self.main)
        bound = self.main.index("title >= sizeof(SMC_TITLES)")
        lookup = self.main.index("SMC_TITLES[title]", bound)
        self.assertLess(bound, lookup)


if __name__ == "__main__":
    unittest.main()
