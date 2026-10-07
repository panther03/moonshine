"""Final Moonshine release branding and language selection."""
from pathlib import Path
import re
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import package_launcher

ROOT = Path(__file__).resolve().parents[1]
class ReleaseBrandingTests(unittest.TestCase):
    def test_final_branding_in_visible_game_surfaces(self):
        for path in ("src/menu.cpp", "src/crash_report.cpp"):
            source = (ROOT / path).read_text()
            with self.subTest(path=path):
                self.assertIn("Moonshine", source)
                self.assertIn("V2.3.3 Frame By Frame", source)
                visible = '\n'.join(re.findall(r'"([^"\n]*)"', source))
                self.assertNotIn("FOXTROT", visible)
                self.assertNotIn("pre-release", visible.lower())
        menu = (ROOT / "src/menu.cpp").read_text()
        self.assertIn('drawText("Moonshine", PANEL_X', menu)
        self.assertIn('textWidth("V2.3.3 Frame By Frame", FOOT_SZ)', menu)
        meta = (ROOT / "launcher/meta.xml.j2").read_text()
        self.assertIn("<name>Moonshine Launcher</name>", meta)
        self.assertNotIn("The House Always Wins", meta)
    def test_release_guides_replace_historical_tester_material(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        self.assertIn("doc/release-notes-v2.3.3.md", cmake)
        self.assertNotIn("doc/foxtrot-tester-checklist.md", cmake)
        packer = (ROOT / "scripts/package_launcher.py").read_text()
        for name in ("guide-en.md", "guide-ja.md"):
            self.assertIn(name, packer)
            guide = (ROOT / "doc" / name).read_text(encoding='utf-8')
            self.assertIn("Moonshine V2.3.3 Frame By Frame", guide)
            self.assertNotIn("FOXTROT", guide)
            self.assertNotIn("pre-release", guide.lower())
            self.assertNotIn("プレリリース", guide)
            self.assertNotIn("BUILD_PENDING", guide)
            self.assertIn("apps/moonshine_launcher", guide)
        self.assertIn("日本語版", (ROOT / "doc/guide-ja.md").read_text(encoding='utf-8').splitlines()[0])
        self.assertIn("decode_crash.py", packer)
    def test_launcher_zip_contains_complete_codec_licenses(self):
        with tempfile.TemporaryDirectory(prefix="moonshine-package-") as temporary:
            work = Path(temporary)
            boot = work / "boot.dol"
            boot.write_bytes(b"test loader")
            mod = work / "mod_us.bin"
            mod.write_bytes(b"test mod")
            archive = work / "launcher.zip"
            with patch.object(package_launcher, "render_meta", return_value="<app/>"):
                result = package_launcher.main([
                    "--boot-dol", str(boot), "--out-zip", str(archive),
                    "--mod-bins", str(mod)])
            self.assertEqual(result, 0)
            with zipfile.ZipFile(archive) as packaged:
                prefix = package_launcher.APP_NAME + "/"
                self.assertEqual(packaged.read(prefix + "language.txt"), b"en\n")
                self.assertEqual(prefix, "apps/moonshine_launcher/")
                self.assertFalse(any("TESTING" in name or "RC1" in name for name in packaged.namelist()))
                self.assertEqual(packaged.read(prefix + "licenses/miniz-LICENSE.txt"),
                                 (ROOT / "vendor/miniz/LICENSE").read_bytes())
                self.assertEqual(packaged.read(prefix + "licenses/lz4-LICENSE.txt"),
                                 (ROOT / "vendor/lz4/LICENSE").read_bytes())
                self.assertEqual(packaged.read(prefix + "boot.dol"), boot.read_bytes())
                self.assertEqual(packaged.read(prefix + "mod_us.bin"), mod.read_bytes())
                for name in ("guide-en.md", "guide-ja.md"):
                    self.assertEqual(packaged.read(prefix + name), (ROOT / "doc" / name).read_bytes())
    def test_japanese_package_explicitly_selects_language(self):
        with tempfile.TemporaryDirectory(prefix="moonshine-language-") as temporary:
            work = Path(temporary)
            boot, asset, archive = (work / name for name in ("boot.dol", "ja_ui.bin", "app.zip"))
            boot.write_bytes(b"test loader")
            asset.write_bytes(b"validated Japanese asset")
            with patch.object(package_launcher, "render_meta", return_value="<app/>"), \
                 patch("gen_japanese_ui.build", return_value=(asset.read_bytes(), {})):
                package_launcher.main(["--boot-dol", str(boot), "--out-zip", str(archive),
                                       "--japanese-ui", str(asset), "--language", "ja"])
            with zipfile.ZipFile(archive) as packaged:
                self.assertEqual(packaged.read("apps/moonshine_launcher/language.txt"), b"ja\n")
                self.assertEqual(packaged.read("apps/moonshine_launcher/ja_ui.bin"), asset.read_bytes())
    def test_japanese_package_requires_its_game_asset(self):
        with self.assertRaises(SystemExit), patch("sys.stderr"):
            package_launcher.main(["--boot-dol", "unused", "--out-zip", "unused", "--language", "ja"])
if __name__ == "__main__": unittest.main()
