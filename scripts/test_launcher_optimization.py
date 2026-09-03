import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class LauncherOptimizationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        cls.superbuild = (ROOT / "launcher" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        cls.loader = (ROOT / "launcher" / "loader" / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )

    def test_default_is_os_and_setting_reaches_only_wii_group(self):
        self.assertIn(
            'set(SUSAMUNE_LOADER_OPTIMIZATION "-Os" CACHE STRING', self.root
        )
        self.assertIn(
            'set(SUSAMUNE_LOADER_OPTIMIZATION "-Os" CACHE STRING',
            self.superbuild,
        )
        self.assertIn(
            'set(SUSAMUNE_LOADER_OPTIMIZATION "-Os" CACHE STRING', self.loader
        )
        root_launcher_block = self.root.split(
            "ExternalProject_Add(nintendont_launcher", 1
        )[1]
        arm_block = self.superbuild.split("ExternalProject_Add(arm", 1)[1].split(
            "ExternalProject_Add(wii", 1
        )[0]
        wii_block = self.superbuild.split("ExternalProject_Add(wii", 1)[1]
        forwarded = (
            '"-DSUSAMUNE_LOADER_OPTIMIZATION:STRING='
            '${SUSAMUNE_LOADER_OPTIMIZATION}"'
        )
        self.assertIn(forwarded, root_launcher_block)
        self.assertNotIn("SUSAMUNE_LOADER_OPTIMIZATION", arm_block)
        self.assertIn(forwarded, wii_block)

    def test_loader_accepts_only_measured_modes(self):
        self.assertIn(
            'SUSAMUNE_LOADER_OPTIMIZATION STREQUAL "-O3"', self.loader
        )
        self.assertIn(
            'SUSAMUNE_LOADER_OPTIMIZATION STREQUAL "-Os"', self.loader
        )
        self.assertIn("${SUSAMUNE_LOADER_OPTIMIZATION} -g", self.loader)
        self.assertNotIn("-O3 -g", self.loader)


if __name__ == "__main__":
    unittest.main()
