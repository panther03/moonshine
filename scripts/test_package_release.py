"""Final downloads must match current proof identities and exact language/file scope."""
import copy
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
import warnings
from unittest.mock import patch
import zipfile
import zlib

import package_release as release


def fake_patch(source=0x12345678, target=0xABCDEF01):
    data = b"BPS1fixture" + struct.pack("<II", source, target)
    return data + struct.pack("<I", zlib.crc32(data))


class ReleasePackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="moonshine-final-package-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.root_patch = patch.object(release, "ROOT", self.root)
        self.root_patch.start()
        self.addCleanup(self.root_patch.stop)

    def put(self, name, data):
        path = self.root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_bps_crc_and_exact_proof_identity_are_required(self):
        data = fake_patch()
        source, target, checksum = struct.unpack("<III", data[-12:])
        row = dict(source_crc32=f"{source:08X}", target_crc32=f"{target:08X}",
                   patch_crc32=f"{checksum:08X}", patch_sha256=release.sha(data))
        release.checked_patch(data, row)
        for key in row:
            bad = {**row, key: "0" * len(row[key])}
            with self.subTest(key=key), self.assertRaises(ValueError):
                release.checked_patch(data, bad)
        with self.assertRaises(ValueError):
            release.checked_patch(data[:-1] + bytes([data[-1] ^ 1]), row)

    def test_host_log_requires_complete_current_success(self):
        good = f"Ran {release.HOST_TEST_FLOOR} tests in 9.1s\n\nOK\n"
        self.assertEqual(release.checked_test_log(good),
                         dict(total=release.HOST_TEST_FLOOR,passed=release.HOST_TEST_FLOOR,skipped=0))
        skipped = good.replace("OK", "OK (skipped=2)")
        self.assertEqual(release.checked_test_log(skipped, allow_skipped=True),
                         dict(total=release.HOST_TEST_FLOOR,passed=release.HOST_TEST_FLOOR-2,skipped=2))
        with self.assertRaises(ValueError):
            release.checked_test_log(skipped)
        for bad in (good.replace("OK", "FAILED"), "ERROR: bad\n"+good,
                    "Ran 3 tests in 1s\n\nOK\n", good+"interrupted\n"):
            with self.assertRaises(ValueError):
                release.checked_test_log(bad)

    def test_launcher_dol_must_have_complete_sections_and_executable_entry(self):
        data = bytearray(260)
        for offset, value in ((0, 256), (0x48, 0x80004000), (0x90, 4), (0xE0, 0x80004000)):
            struct.pack_into(">I", data, offset, value)
        release.checked_dol(data)
        for offset, value in ((0, 259), (0x48, 0x91F00000), (0x90, 8), (0xE0, 0x80005000)):
            bad = bytearray(data)
            struct.pack_into(">I", bad, offset, value)
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                release.checked_dol(bad)

    def test_runtime_proof_requires_current_sources_assets_images_and_evidence(self):
        source = self.put("src/live.cpp", b"current source")
        result = self.put("build/run.json", b'{"passed":true}')
        final = dict(jp="11111111", us="22222222", pal="33333333")
        receipt = dict(passed=True, final_image_crc32=final, japanese_image_crc32="44444444",
                       asset_sha256=release.sha(b"asset"), current_sources={"src/live.cpp": release.sha(source.read_bytes())},
                       current_scopes=["Current private Dolphin smoke"], historical_scopes=[],
                       evidence=[release.record(result)], host_tests=release.HOST_TEST_FLOOR)
        with patch.object(release, "required_source_paths", return_value=["src/live.cpp"]):
            release.checked_runtime(receipt, final, "44444444", b"asset", release.HOST_TEST_FLOOR)
            for key, value in (("passed", False), ("final_image_crc32", {}),
                               ("japanese_image_crc32", "old"), ("asset_sha256", "old"),
                               ("current_sources", {}), ("current_scopes", []),
                               ("historical_scopes", None), ("evidence", []), ("host_tests", 3)):
                with self.subTest(key=key), self.assertRaises(ValueError):
                    release.checked_runtime({**receipt, key: value}, final, "44444444", b"asset", release.HOST_TEST_FLOOR)
            source.write_bytes(b"changed source")
            with self.assertRaises(ValueError):
                release.checked_runtime(receipt, final, "44444444", b"asset", release.HOST_TEST_FLOOR)
            source.write_bytes(b"current source")
            result.write_bytes(b"changed evidence")
            with self.assertRaises(ValueError):
                release.checked_runtime(receipt, final, "44444444", b"asset", release.HOST_TEST_FLOOR)

    def test_evidence_paths_cannot_escape_repository(self):
        for name in ("../outside", "C:\\outside", "/outside", "build/../../outside"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                release.evidence_path(name)

    def test_receipts_accept_root_aliases_without_accepting_outside_files(self):
        result = self.put("build/run.json", b'{"passed":true}')
        anchor = self.root / "alias-anchor"
        anchor.mkdir()
        # Reproduce canonical file paths paired with an unresolved root, as in Windows TEMP.
        with patch.object(release, "ROOT", anchor / ".."):
            with self.subTest(operation="record"):
                self.assertEqual(release.record(result), {
                    "path": "build/run.json", "bytes": 15,
                    "sha256": release.sha(b'{"passed":true}')})
            with self.subTest(operation="evidence"):
                self.assertEqual(release.evidence_path("build/run.json"), result.resolve())
            with tempfile.TemporaryDirectory(prefix="moonshine-outside-") as outside:
                other = Path(outside) / "run.json"
                other.write_bytes(result.read_bytes())
                for operation in (release.relative, release.record):
                    with self.subTest(operation=operation.__name__), self.assertRaises(ValueError):
                        operation(other)

    def test_iso_proof_binds_all_segments_hooks_and_japanese_extent(self):
        data, asset = fake_patch(), b"validated asset"
        source, target, checksum = struct.unpack("<III", data[-12:])
        manifest = dict(game_id=0x474D534A, base_addr=0x80426020, writes=[[0x80001000, 0]]*31,
                        segments=[dict(offset=0, code="01020304", memory_size=8),
                                  dict(offset=0x80000, code="05060708", memory_size=12)])
        row = dict(region="jp", source_crc32=f"{source:08X}", target_crc32=f"{target:08X}",
                   patch_crc32=f"{checksum:08X}", patch_sha256=release.sha(data), size=12345,
                   hooks_verified=31, protected_hole_absent=True,
                   segments=[dict(address=hex(manifest["base_addr"]+s["offset"]), initialized_bytes=4,
                                  runtime_bytes=s["memory_size"], sha256=release.sha(bytes.fromhex(s["code"])+bytes(s["memory_size"]-4)))
                             for s in manifest["segments"]],
                   japanese_ui=dict(offset=0x4AA8C0, extent=0x19000, outside_dol=True,
                                    ui_language="en", zeroed=True, bytes=0, sha256=release.sha(bytes(0x19000))))
        layout = dict(source_crc32=row["source_crc32"], iso_size=12345)
        release.checked_iso_row(row, manifest, layout, data, asset, "en")
        translated = copy.deepcopy(row)
        translated["japanese_ui"].update(ui_language="ja", zeroed=False, bytes=len(asset), sha256=release.sha(asset))
        release.checked_iso_row(translated, manifest, layout, data, asset, "ja")
        for candidate, language in ((translated, "en"), (row, "ja")):
            with self.assertRaises(ValueError):
                release.checked_iso_row(candidate, manifest, layout, data, asset, language)
        for mutate in (lambda r:r.update(hooks_verified=30), lambda r:r.update(protected_hole_absent=False),
                       lambda r:r["segments"][1].update(sha256="bad"),
                       lambda r:r["japanese_ui"].update(outside_dol=False)):
            bad = copy.deepcopy(row); mutate(bad)
            with self.assertRaises(ValueError):
                release.checked_iso_row(bad, manifest, layout, data, asset, "en")

    def test_four_exact_language_packages_share_sd_layout_and_current_checklist(self):
        for name, path in release.FILES.items():
            self.put(path, ("Final release: "+name).encode())
        self.put("launcher/themes/japanese/background.png", b"\x89PNG\r\n\x1a\n"+bytes(8)+struct.pack(">II",1024,480))
        args = SimpleNamespace(build_dir=self.root/"build", launcher_boot=self.root/"boot.dol")
        patches = {f"moonshine_{name}.bps": fake_patch(target=index+1)
                   for index, name in enumerate(("jp", "us", "pal", "jp_ja"))}
        prefix = "apps/moonshine_launcher/"
        def app(*args, **kwargs):
            return {prefix+"meta.xml": f"<app><name>Moonshine Launcher</name><version>{release.VERSION}</version></app>".encode(),
                    prefix+"boot.dol": b"same binary", prefix+"language.txt": (kwargs["language"]+"\n").encode()}
        with patch.object(release.package_launcher, "launcher_files", side_effect=app):
            packages = release.archive_contents(args, {"build_checksum":"DEADBEEF"}, patches)
        self.assertEqual(set(packages), {
            "Moonshine_ENGLISH-MENUS_Launcher_V2.3.3_US-PAL-JP.zip",
            "Moonshine_JAPANESE-MENUS_Launcher_V2.3.3_US-PAL-JP.zip",
            "Moonshine_ENGLISH-MENUS_Dolphin_V2.3.3_US-PAL-JP.zip",
            "Moonshine_JAPANESE-MENUS_Dolphin_V2.3.3_JP.zip",
        })
        for name, (language, kind, files) in packages.items():
            self.assertTrue(name.isascii())
            self.assertFalse(any("FOXTROT" in n or "RC1" in n for n in files))
            base = prefix if kind == "launcher" else "moonshine_dolphin/"
            self.assertEqual(files[base+"TESTING.md"], b"Final release: TESTING.md")
            self.assertEqual(files[base+"language.txt"], (language+"\n").encode())
            for entry in release.FILES:
                self.assertIn(base+entry, files)
            if kind == "launcher":
                self.assertEqual("Moonshine data/theme/background.png" in files, language == "ja")
                self.assertIn("README.md", files)
                data_files = {n for n in files if n.startswith("Moonshine data/")}
                self.assertEqual(data_files, {"Moonshine data/theme/background.png"} if language == "ja" else set())
                self.assertFalse(any(n.startswith("Moonshine_Theme/") for n in files))
                self.assertIn("/Moonshine data/moonshine.ini", files["README.md"].decode())
            else:
                actual = {n.split("/")[-1] for n in files if n.endswith(".bps")}
                self.assertEqual(actual, {"moonshine_jp_ja.bps"} if language == "ja" else
                                 {"moonshine_jp.bps", "moonshine_us.bps", "moonshine_pal.bps"})
                self.assertNotIn("Moonshine data/theme/background.png", files)
            readme = files["README.md" if kind == "launcher" else base+"README.md"].decode()
            if language == "en":
                self.assertIn("Supports US, PAL and JP Sunshine, with English Moonshine menus in all three regions", readme)
                self.assertIn("retail game's language is unchanged", readme)
            else:
                self.assertIn("日本語版", readme)
                self.assertIn("してください", readme)
                if kind == "launcher":
                    self.assertIn("Supports US, PAL and JP Sunshine", readme)
                    self.assertIn("Japanese on JP and English on US/PAL", readme)
                else:
                    self.assertIn("For JP Sunshine only, with Japanese Moonshine menus", readme)

    def test_zip_verifier_rejects_extra_duplicate_or_changed_files(self):
        path = self.root/"日本語版.zip"
        files = {"apps/moonshine_launcher/language.txt":b"ja\n", "README.md":b"readme"}
        release.write_zip(path, files)
        original = path.read_bytes()
        release.write_zip(path, files)
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(len(release.checked_zip(path, files)), 2)
        for name, value in (("retail.iso", b"unexpected"), ("README.md", b"changed")):
            path.write_bytes(original)
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", UserWarning)
                with zipfile.ZipFile(path, "a") as archive:
                    archive.writestr(name, value)
            with self.assertRaises(ValueError):
                release.checked_zip(path, files)
        with zipfile.ZipFile(path,"w") as archive:
            for name, value in files.items():
                archive.writestr(name, value+b"tampered")
        with self.assertRaises(ValueError):
            release.checked_zip(path, files)

    def test_candidate_mode_cannot_report_final_verification(self):
        args = SimpleNamespace(mode="candidate", build_dir=self.root/"build",
                               emulator_build_dir=self.root/"build/emu",
                               standard_proof=None, japanese_proof=None, runtime_proof=None)
        args.host_log = self.put("build/tests.log", f"Ran {release.HOST_TEST_FLOOR} tests in 1s\n\nOK\n".encode())
        self.put("build/ja_ui.bin", b"asset")
        checksum = 0
        for region, base in (("jp",0x80426020),("us",0x80429800),("pal",0x80420D60)):
            manifest = dict(game_id=release.GAME_IDS[region], base_addr=base, region_reserve=0xC2000,
                            segments=[dict(offset=0,memory_size=8,code="01020304"),
                                      dict(offset=0x80000,memory_size=8,code="05060708")],
                            writes=[[0x80001000+i*4,0] for i in range(31)])
            mod = release.build_mod_bin(manifest)
            checksum = zlib.crc32(mod, checksum)
            self.put(f"build/mod_{region}.bin",mod)
            for folder in ("build", "build/emu"):
                self.put(f"{folder}/susamune_manifest_{region}.json",json.dumps(manifest).encode())
            self.put(f"data/iso_layout_{region}.json",b"{}")
            self.put(f"build/emu/moonshine_{region}.bps",fake_patch())
        self.put("build/emu/moonshine_jp_ja.bps",fake_patch())
        self.put("build/launcher/shared/gen/susamune_build_id.h",
                 f'#define SUSAMUNE_BUILD_CHECKSUM "{checksum:08X}"\n'.encode())
        dol = bytearray(260)
        for offset, value in ((0,256),(0x48,0x80004000),(0x90,4),(0xE0,0x80004000)):
            struct.pack_into(">I",dol,offset,value)
        args.launcher_boot=self.put("build/boot.dol",dol+f"{checksum:08X}".encode())
        with patch.object(release,"git",side_effect=lambda *a: "" if a[0]=="status" else
                          release.TIMER_BLOB if a[0]=="hash-object" else "commit"), \
             patch.object(release,"build_japanese_ui",return_value=(b"asset",{})), \
             patch.object(release,"build_patch",return_value=(fake_patch(),None)):
            report,_=release.validate_build(args)
            self.assertFalse(report["final_verified"])
            self.assertEqual(report["validation_scope"],"source_build_checked; full_iso_and_runtime_not_checked")
            args.mode="release"
            with self.assertRaisesRegex(ValueError,"requires standard, Japanese and current runtime proofs"):
                release.validate_build(args)
            args.mode="candidate"
            self.put("build/emu/moonshine_jp_ja.bps",b"stale patch")
            with self.assertRaisesRegex(ValueError,"Generated BPS is stale"):
                release.validate_build(args)


if __name__ == "__main__":
    unittest.main()
