#!/usr/bin/env python3
"""Contracts for the official Moonshine V2.2.0 package branding."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def text(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class ReleaseBrandingTests(unittest.TestCase):
    def test_launcher_and_game_use_official_branding(self) -> None:
        cmake = text("CMakeLists.txt")
        meta = text("launcher/meta.xml.j2")
        launcher = text("launcher/loader/source/menu.c")
        menu = text("src/menu.cpp")

        self.assertIn('V2.2.0 \\"The House Always Wins\\"', cmake)
        self.assertGreaterEqual(
            cmake.count('V2.2.0 \\"The House Always Wins\\"'), 2
        )
        self.assertIn("<name>Moonshine Launcher</name>", meta)
        self.assertIn('Moonshine V2.2.0 "The House Always Wins".', meta)
        self.assertIn('BuildProduct[] = "Moonshine Launcher"', launcher)
        self.assertIn('"V2.2.0 \\"The House Always Wins\\"."', launcher)
        self.assertIn('drawText("Moonshine V2.2.0"', menu)

        runtime = "\n".join((meta, launcher, menu))
        self.assertNotIn("ZETA", runtime)
        self.assertNotIn("Release Candidate", runtime)
        self.assertNotIn("V2.2.0 RC", runtime)

    def test_release_package_omits_tester_material(self) -> None:
        cmake = text("CMakeLists.txt")
        self.assertIn('"-DLAUNCHER_TEST_LOG="', cmake)
        self.assertIn('"-DLAUNCHER_PATTERN_TEST_LOG="', cmake)
        self.assertIn("doc/v2.2.0-changelog.md", cmake)
        self.assertNotIn("doc/v2.2.0-rc3-changelog.md", cmake)


if __name__ == "__main__":
    unittest.main()
